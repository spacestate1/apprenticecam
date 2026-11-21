/*
 * xt_mesh.c - Parasolid binary transmit (.x_b) reader + tessellator for plane / cylinder faces.
 *
 * File body after "**END_OF_HEADER***...\n":
 *   'B', u32 len + ": TRANSMIT FILE created by modeller version N", u32 len + schema key, u32 user-field size (USFLD)
 *   nodes: u16 type, [u32 count if the node type is variable-length], u16 index, transmitted fields in schema order,
 *          then USFLD int32 user fields; u16 type 1 ends the data.
 *   field encodings: d int32 | f double | p u16 node index (1 = null) | c/l/u 1 byte | n int16 | v 3 doubles |
 *                    b 6 doubles | h 4 doubles | i 2 doubles | t int32
 * Topology used: FACE(loop, surface, sense, next) -> LOOP(halfedge, next) -> HALFEDGE(forward, vertex, edge, sense)
 *   -> EDGE(curve) ; VERTEX(point) -> POINT(pvec) ; surfaces PLANE / CYLINDER, curves LINE / CIRCLE.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "paths.h"
#include "xt_mesh.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXTYPE 400
#define MAXF 64

typedef struct { char name[48]; char type; int xmt, n; } FieldDef;
typedef struct { int defined, var; char name[40]; int nf; FieldDef f[MAXF]; int off[MAXF]; int size; } NodeDef;
struct XtSchema { NodeDef nd[MAXTYPE]; };

static int tsize(char t) {
    switch (t) {
    case 'd': case 't': return 4;
    case 'f': return 8;
    case 'p': case 'n': return 2;
    case 'c': case 'l': case 'u': return 1;
    case 'v': return 24;
    case 'b': return 48;
    case 'h': return 24;   /* xtfix: hvec = its pvec only in transmit files (Parasolid XT Format Reference 2006 p.44; formats/xt_occt_fixes.md) */
    case 'i': return 16;
    default: return -1;
    }
}

long xt_dbg_arcs, xt_dbg_off;

XtSchema *xt_schema_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    XtSchema *s = xcalloc(1, sizeof *s);
    char line[512]; NodeDef *cur = NULL;
    while (fgets(line, sizeof line, f)) {
        int code, a, b, c; char name[64], ftype;
        if (sscanf(line, "%d %63[A-Z_0-9];", &code, name) == 2 && code > 0 && code < MAXTYPE) {
            char *p = strchr(line, ';'); p = p ? strchr(p + 1, ';') : NULL;
            if (p && sscanf(p + 1, "%d %d %d", &a, &b, &c) == 3) {
                cur = &s->nd[code]; cur->defined = 1; cur->var = c; cur->nf = 0;
                snprintf(cur->name, sizeof cur->name, "%s", name);
                continue;
            }
        }
        char fname[64];
        if (cur && sscanf(line, "%63[a-z_0-9]; %c; %d %d %d", fname, &ftype, &a, &b, &c) == 5 && cur->nf < MAXF) {
            FieldDef *fd = &cur->f[cur->nf++];
            snprintf(fd->name, sizeof fd->name, "%s", fname); fd->type = ftype; fd->xmt = a; fd->n = c;
        }
    }
    fclose(f);
    /* byte offsets of transmitted fields (the variable-length last field is not part of the fixed size) */
    for (int t = 0; t < MAXTYPE; t++) {
        NodeDef *d = &s->nd[t];
        if (!d->defined) continue;
        int o = 0, last = -1;
        for (int i = 0; i < d->nf; i++) if (d->f[i].xmt) last = i;
        for (int i = 0; i < d->nf; i++) {
            d->off[i] = -1;
            if (!d->f[i].xmt) continue;
            d->off[i] = o;
            int sz = tsize(d->f[i].type);
            if (sz < 0) { d->size = -1; break; }
            if (d->var && i == last) continue;              /* variable part, counted at parse time */
            o += sz * (d->f[i].n > 1 ? d->f[i].n : 1);
        }
        if (d->size == 0) d->size = o;
    }
    return s;
}
void xt_schema_free(XtSchema *s) { free(s); }

/* ------------------------------------------------------------------------------------------------ nodes */
typedef struct { int type; const uint8_t *p; } Node;
typedef struct { const XtSchema *s; Node *by_idx; int maxidx; } Model;

static int fidx(const XtSchema *s, int type, const char *name) {
    const NodeDef *d = &s->nd[type];
    for (int i = 0; i < d->nf; i++) if (!strcmp(d->f[i].name, name)) return d->off[i];
    return -1;
}
static const uint8_t *fld(const Model *m, const Node *n, const char *name) {
    int o = fidx(m->s, n->type, name);
    return o < 0 ? NULL : n->p + o;
}
static int fptr(const Model *m, const Node *n, const char *name) {
    const uint8_t *q = fld(m, n, name);
    if (!q) return 1;
    return q[0] | q[1] << 8;
}
static double fdbl(const Model *m, const Node *n, const char *name, int k) {
    const uint8_t *q = fld(m, n, name); double d = 0;
    if (q) memcpy(&d, q + 8 * k, 8);
    return d;
}
static int fchr(const Model *m, const Node *n, const char *name) { const uint8_t *q = fld(m, n, name); return q ? q[0] : 0; }
static const Node *node(const Model *m, int idx) {
    if (idx <= 1 || idx > m->maxidx || !m->by_idx[idx].p) return NULL;
    return &m->by_idx[idx];
}
static int is(const Model *m, const Node *n, const char *tname) { return n && !strcmp(m->s->nd[n->type].name, tname); }

static int parse(Model *m, const uint8_t *b, size_t len, char *err) {
    const char *h = mem_mem(b, len, "END_OF_HEADER", 13);
    if (!h) { snprintf(err, 128, "no XT header"); return 0; }
    const uint8_t *q = memchr(h, '\n', b + len - (const uint8_t *)h);
    if (!q || q + 1 >= b + len || q[1] != 'B') { snprintf(err, 128, "not a binary XT"); return 0; }
    size_t o = q + 2 - b;
    uint32_t n1, n2, usfld;                              /* lengths from the file: every step bounds-checked */
    if (o + 4 > len) goto trunc; memcpy(&n1, b + o, 4); if (n1 > len - o - 4) goto trunc; o += 4 + n1;
    if (o + 4 > len) goto trunc; memcpy(&n2, b + o, 4); if (n2 > len - o - 4) goto trunc; o += 4 + n2;
    if (o + 4 > len) goto trunc; memcpy(&usfld, b + o, 4); o += 4;
    if (usfld > 1024) { snprintf(err, 128, "implausible user field count %u", usfld); return 0; }
    m->maxidx = 65536;
    m->by_idx = xcalloc(m->maxidx + 1, sizeof(Node));
    while (o + 2 <= len) {
        int type = b[o] | b[o + 1] << 8; o += 2;
        if (type == 1) return 1;
        if (type <= 0 || type >= MAXTYPE || !m->s->nd[type].defined || m->s->nd[type].size < 0) {
            snprintf(err, 128, "unsupported node type %d at 0x%zx", type, o - 2); return 0;
        }
        const NodeDef *d = &m->s->nd[type];
        uint32_t count = 0;
        if (d->var) { if (o + 4 > len) goto trunc; memcpy(&count, b + o, 4); o += 4; }
        if (o + 2 > len) goto trunc;
        int idx = b[o] | b[o + 1] << 8; o += 2;
        size_t sz = d->size;
        if (d->var) {
            int last = -1;
            for (int i = 0; i < d->nf; i++) if (d->f[i].xmt) last = i;
            if (last >= 0) { if (count > len) goto trunc; sz += (size_t)count * tsize(d->f[last].type); }
        }
        if (sz > len - o || 4 * (size_t)usfld > len - o - sz) { snprintf(err, 128, "truncated node at 0x%zx", o); return 0; }
        m->by_idx[idx].type = type; m->by_idx[idx].p = b + o;
        o += sz + 4 * (size_t)usfld;
    }
    snprintf(err, 128, "no end marker");
    return 0;
trunc:
    snprintf(err, 128, "truncated XT data");
    return 0;
}

/* ------------------------------------------------------------------------------------------------ geometry */
typedef struct { double x, y, z; } P3;
static P3 p3(double x, double y, double z) { P3 r = { x, y, z }; return r; }
static P3 sub(P3 a, P3 b) { return p3(a.x - b.x, a.y - b.y, a.z - b.z); }
static P3 add(P3 a, P3 b) { return p3(a.x + b.x, a.y + b.y, a.z + b.z); }
static P3 mul(P3 a, double k) { return p3(a.x * k, a.y * k, a.z * k); }
static double dot(P3 a, P3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static P3 cross(P3 a, P3 b) { return p3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static P3 norm(P3 a) { double l = sqrt(dot(a, a)); return l > 0 ? mul(a, 1 / l) : a; }
static P3 vec(const Model *m, const Node *n, const char *f) { return p3(fdbl(m, n, f, 0), fdbl(m, n, f, 1), fdbl(m, n, f, 2)); }

typedef struct { P3 *p; int n, cap; int wraps; } Ring;    /* one loop sampled as a closed polyline (last != first) */
static void rpush(Ring *r, P3 p) {
    if (r->n == r->cap) { r->cap = r->cap ? r->cap * 2 : 64; r->p = xrealloc(r->p, r->cap * sizeof(P3)); }
    r->p[r->n++] = p;
}

static P3 vertex_pos(const Model *m, const Node *he) {
    const Node *v = node(m, fptr(m, he, "vertex"));
    if (!v) return p3(NAN, NAN, NAN);
    const Node *pt = node(m, fptr(m, v, "point"));
    return pt ? vec(m, pt, "pvec") : p3(NAN, NAN, NAN);
}

/* sample the curve of halfedge `he` from a to b (exclusive of b); full circle if a/b are NaN (ring edge) */
static void sample_edge(const Model *m, const Node *he, P3 a, P3 b, Ring *r) {
    const Node *e = node(m, fptr(m, he, "edge"));
    const Node *c = e ? node(m, fptr(m, e, "curve")) : NULL;
    if (!c) c = node(m, fptr(m, he, "curve"));
    int fwd = fchr(m, he, "sense") != '-';
    if (is(m, c, "CIRCLE")) {
        P3 ctr = vec(m, c, "centre"), nrm = norm(vec(m, c, "normal")), xa = norm(vec(m, c, "x_axis"));
        P3 ya = cross(nrm, xa);
        double rad = fdbl(m, c, "radius", 0);
        if (fchr(m, c, "sense") == '-') { nrm = mul(nrm, -1); ya = mul(ya, -1); }
        double a0, sw;
        if (!isnan(a.x)) {      /* endpoint consistency check (debug counters) */
            extern long xt_dbg_arcs, xt_dbg_off;
            xt_dbg_arcs++;
            if (fabs(sqrt(dot(sub(a, ctr), sub(a, ctr))) - rad) > 1e-6 || fabs(sqrt(dot(sub(b, ctr), sub(b, ctr))) - rad) > 1e-6) xt_dbg_off++;
        }
        if (isnan(a.x)) { a0 = 0; sw = 2 * M_PI; }
        else {
            P3 da = sub(a, ctr), db = sub(b, ctr);
            a0 = atan2(dot(da, ya), dot(da, xa));
            double a1 = atan2(dot(db, ya), dot(db, xa));
            sw = fwd ? fmod(a1 - a0 + 4 * M_PI, 2 * M_PI) : -fmod(a0 - a1 + 4 * M_PI, 2 * M_PI);
            if (fabs(sw) < 1e-9) sw = fwd ? 2 * M_PI : -2 * M_PI;
        }
        if (isnan(a.x) && !fwd) sw = -sw;
        if (!isfinite(sw) || fabs(sw) > 4 * M_PI) sw = 0;
        int k = (int)ceil(fabs(sw) / (M_PI / 36)); if (k < 1) k = 1;
        for (int i = 0; i < k; i++) {
            double t = a0 + sw * i / k;
            rpush(r, add(ctr, add(mul(xa, rad * cos(t)), mul(ya, rad * sin(t)))));
        }
    } else {
        if (!isnan(a.x)) rpush(r, a);        /* line (or unsupported curve): straight segment */
    }
}

static int loop_ring(const Model *m, const Node *loop, Ring *r) {
    const Node *first = node(m, fptr(m, loop, "halfedge"));
    if (!first) return 0;
    const Node *he = first; int guard = 0;
    do {
        const Node *nx = node(m, fptr(m, he, "forward")), *pv = node(m, fptr(m, he, "backward"));
        /* a fin's vertex is at its forward end: the fin runs from the previous fin's vertex to its own */
        P3 b = vertex_pos(m, he), a = pv ? vertex_pos(m, pv) : b;
        sample_edge(m, he, a, b, r);
        he = nx;
    } while (he && he != first && ++guard < 100000);
    if (guard >= 100000) return 0;           /* damaged topology: the fin chain never closes */
    return r->n >= 2;
}

/* ------------------------------------------------------------------------------------------------ triangulation */
typedef struct { double x, y; } P2;
typedef struct { float *v; int n, cap; } TriBuf;
static void emit(TriBuf *t, P3 p, P3 nrm) {
    if (t->n + 6 > t->cap) { t->cap = t->cap ? t->cap * 2 : 6 * 3 * 1024; t->v = xrealloc(t->v, t->cap * sizeof(float)); }
    float *q = t->v + t->n;
    q[0] = (float)p.x; q[1] = (float)p.y; q[2] = (float)p.z; q[3] = (float)nrm.x; q[4] = (float)nrm.y; q[5] = (float)nrm.z;
    t->n += 6;
}
static double area2(const P2 *p, int n) { double a = 0; for (int i = 0; i < n; i++) { int j = (i + 1) % n; a += p[i].x * p[j].y - p[j].x * p[i].y; } return a; }
static double crs(P2 a, P2 b, P2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }
static int in_tri(P2 p, P2 a, P2 b, P2 c) {
    double d1 = crs(a, b, p), d2 = crs(b, c, p), d3 = crs(c, a, p);
    return d1 >= -1e-14 && d2 >= -1e-14 && d3 >= -1e-14;
}

/* ear clipping of a simple CCW polygon given as indices into pts; appends index triples */
static int earclip(const P2 *pts, int *idx, int n, int *out) {
    int nt = 0, guard = 0;
    long long budget = 300000000;                            /* point-in-triangle tests; damaged data can make n huge */
    while (n > 3 && guard++ < 1000000) {
        int found = 0;
        for (int i = 0; i < n && budget > 0; i++) {
            budget -= n;
            int ia = idx[(i + n - 1) % n], ib = idx[i], ic = idx[(i + 1) % n];
            P2 a = pts[ia], b = pts[ib], c = pts[ic];
            if (crs(a, b, c) <= 1e-18) continue;               /* reflex or degenerate */
            int ok = 1;
            for (int j = 0; j < n && ok; j++) {
                int ij = idx[j];
                if (ij == ia || ij == ib || ij == ic) continue;
                P2 q = pts[ij];
                if ((q.x == a.x && q.y == a.y) || (q.x == b.x && q.y == b.y) || (q.x == c.x && q.y == c.y)) continue;
                if (in_tri(q, a, b, c)) ok = 0;
            }
            if (!ok) continue;
            out[nt * 3] = ia; out[nt * 3 + 1] = ib; out[nt * 3 + 2] = ic; nt++;
            memmove(idx + i, idx + i + 1, (n - i - 1) * sizeof(int)); n--;
            found = 1; break;
        }
        if (!found) {                                        /* numerical trouble: clip the most convex vertex */
            int best = 0; double bv = -1e300;
            for (int i = 0; i < n; i++) {
                double v = crs(pts[idx[(i + n - 1) % n]], pts[idx[i]], pts[idx[(i + 1) % n]]);
                if (v > bv) { bv = v; best = i; }
            }
            out[nt * 3] = idx[(best + n - 1) % n]; out[nt * 3 + 1] = idx[best]; out[nt * 3 + 2] = idx[(best + 1) % n]; nt++;
            memmove(idx + best, idx + best + 1, (n - best - 1) * sizeof(int)); n--;
        }
    }
    if (n == 3) { out[nt * 3] = idx[0]; out[nt * 3 + 1] = idx[1]; out[nt * 3 + 2] = idx[2]; nt++; }
    return nt;
}

/* triangulate loops (2D) with holes: outer = largest |area|, made CCW; holes made CW and bridged in */
static int tri_loops(P2 **loops, int *ln, int nl, P2 **allpts, int **tris) {
    int total = 0; for (int i = 0; i < nl; i++) total += ln[i];
    P2 *pts = xmalloc(total * sizeof(P2));
    int *start = xmalloc(nl * sizeof(int)), o = 0, outer = 0; double best = -1;
    for (int i = 0; i < nl; i++) {
        start[i] = o; memcpy(pts + o, loops[i], ln[i] * sizeof(P2)); o += ln[i];
        double a = fabs(area2(loops[i], ln[i])); if (a > best) { best = a; outer = i; }
    }
    /* polygon index list, holes bridged to the outer boundary at mutually closest vertices */
    int cap = total + 4 * nl + 8, n = 0;
    int *poly = xmalloc(cap * sizeof(int));
    int sgn = area2(loops[outer], ln[outer]) > 0 ? 1 : -1;
    for (int k = 0; k < ln[outer]; k++) poly[n++] = start[outer] + (sgn > 0 ? k : ln[outer] - 1 - k);
    for (int h = 0; h < nl; h++) {
        if (h == outer || ln[h] < 3) continue;
        int hs = area2(loops[h], ln[h]) < 0 ? 1 : -1;         /* traverse hole clockwise */
        double bd = 1e300; int bi = 0, bj = 0;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < ln[h]; j++) {
                P2 a = pts[poly[i]], b = loops[h][j];
                double dd = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
                if (dd < bd) { bd = dd; bi = i; bj = j; }
            }
        int add_n = ln[h] + 2;
        if (n + add_n > cap) { cap = (n + add_n) * 2; poly = xrealloc(poly, cap * sizeof(int)); }
        memmove(poly + bi + 1 + add_n, poly + bi + 1, (n - bi - 1) * sizeof(int));
        for (int k = 0; k <= ln[h]; k++) {
            int jj = hs > 0 ? (bj + k) % ln[h] : (bj - k + ln[h] * 2) % ln[h];
            poly[bi + 1 + k] = start[h] + jj;
        }
        poly[bi + 1 + ln[h] + 1] = poly[bi];
        n += add_n;
    }
    int *out = xmalloc((n + 2) * 3 * sizeof(int));
    int nt = earclip(pts, poly, n, out);
    free(poly); free(start);
    *allpts = pts; *tris = out;
    return nt;
}

static void mesh_plane(const Model *m, const Node *srf, int fsense, Ring *rings, int nr, TriBuf *tb) {
    P3 o = vec(m, srf, "pvec"), nrm = norm(vec(m, srf, "normal")), xa = norm(vec(m, srf, "x_axis"));
    if (fchr(m, srf, "sense") == '-') nrm = mul(nrm, -1);
    if (fsense == '-') nrm = mul(nrm, -1);
    P3 ya = cross(nrm, xa);
    P2 **loops = xmalloc(nr * sizeof(P2 *)); int *ln = xmalloc(nr * sizeof(int));
    for (int i = 0; i < nr; i++) {
        loops[i] = xmalloc(rings[i].n * sizeof(P2)); ln[i] = rings[i].n;
        for (int k = 0; k < rings[i].n; k++) { P3 d = sub(rings[i].p[k], o); loops[i][k].x = dot(d, xa); loops[i][k].y = dot(d, ya); }
    }
    P2 *pts; int *tris;
    int nt = tri_loops(loops, ln, nr, &pts, &tris);
    for (int t = 0; t < nt; t++)
        for (int k = 0; k < 3; k++) {
            P2 q = pts[tris[t * 3 + k]];
            emit(tb, add(o, add(mul(xa, q.x), mul(ya, q.y))), nrm);
        }
    free(pts); free(tris);
    for (int i = 0; i < nr; i++) free(loops[i]);
    free(loops); free(ln);
}

static void mesh_cylinder(const Model *m, const Node *srf, int fsense, Ring *rings, int nr, TriBuf *tb) {
    P3 o = vec(m, srf, "pvec"), ax = norm(vec(m, srf, "axis")), xa = norm(vec(m, srf, "x_axis"));
    double R = fdbl(m, srf, "radius", 0);
    P3 ya = cross(ax, xa);
    double flip = (fchr(m, srf, "sense") == '-') != (fsense == '-') ? -1 : 1;
    /* unwrapped (theta, h) per ring; a ring that turns through 2*pi is a full circle around the cylinder */
    P2 **loops = xmalloc(nr * sizeof(P2 *)); int *ln = xmalloc(nr * sizeof(int)), nwrap = 0;
    for (int i = 0; i < nr; i++) {
        loops[i] = xmalloc(rings[i].n * sizeof(P2)); ln[i] = rings[i].n;
        double prev = 0;
        for (int k = 0; k < rings[i].n; k++) {
            P3 d = sub(rings[i].p[k], o);
            double h = dot(d, ax), th = atan2(dot(d, ya), dot(d, xa));
            if (k && isfinite(th) && isfinite(prev)) { while (th - prev > M_PI) th -= 2 * M_PI; while (th - prev < -M_PI) th += 2 * M_PI; }
            loops[i][k].x = th; loops[i][k].y = h; prev = th;
        }
        double turn = loops[i][ln[i] - 1].x - loops[i][0].x;
        P3 d0 = sub(rings[i].p[0], o); double th0 = atan2(dot(d0, ya), dot(d0, xa));
        double last = loops[i][ln[i] - 1].x, close = th0;
        if (isfinite(close) && isfinite(last)) { while (close - last > M_PI) close -= 2 * M_PI; while (close - last < -M_PI) close += 2 * M_PI; }
        rings[i].wraps = fabs(close - loops[i][0].x) > M_PI;  /* closing the ring adds a full turn */
        (void)turn;
        nwrap += rings[i].wraps;
    }
#define CYL(TH, H) add(o, add(mul(ax, (H)), add(mul(xa, R * cos(TH)), mul(ya, R * sin(TH)))))
#define CYN(TH) mul(add(mul(xa, cos(TH)), mul(ya, sin(TH))), flip)
    if (nwrap >= 2) {
        /* band between two full rings: interpolate each ring's height over a common theta grid */
        int a = -1, b = -1;
        for (int i = 0; i < nr; i++) if (rings[i].wraps) { if (a < 0) a = i; else if (b < 0) b = i; }
        const int N = 72;
        double hA[73], hB[73];
        for (int s = 0; s <= N; s++) {
            double th = 2 * M_PI * s / N;
            for (int which = 0; which < 2; which++) {
                int L = which ? b : a; double best = 1e300, hv = 0;
                for (int k = 0; k < ln[L]; k++) {
                    double d = fabs(remainder(loops[L][k].x - th, 2 * M_PI));
                    if (d < best) { best = d; hv = loops[L][k].y; }
                }
                (which ? hB : hA)[s] = hv;
            }
        }
        for (int s = 0; s < N; s++) {
            double t0 = 2 * M_PI * s / N, t1 = 2 * M_PI * (s + 1) / N;
            P3 a0 = CYL(t0, hA[s]), a1 = CYL(t1, hA[s + 1]), b0 = CYL(t0, hB[s]), b1 = CYL(t1, hB[s + 1]);
            emit(tb, a0, CYN(t0)); emit(tb, a1, CYN(t1)); emit(tb, b1, CYN(t1));
            emit(tb, a0, CYN(t0)); emit(tb, b1, CYN(t1)); emit(tb, b0, CYN(t0));
        }
    } else {
        /* partial cylinder: triangulate in (R*theta, h) */
        for (int i = 0; i < nr; i++) for (int k = 0; k < ln[i]; k++) loops[i][k].x *= R;
        P2 *pts; int *tris;
        int nt = tri_loops(loops, ln, nr, &pts, &tris);
        for (int t = 0; t < nt; t++)
            for (int k = 0; k < 3; k++) {
                P2 q = pts[tris[t * 3 + k]];
                double th = q.x / R;
                emit(tb, CYL(th, q.y), CYN(th));
            }
        free(pts); free(tris);
    }
#undef CYL
#undef CYN
    for (int i = 0; i < nr; i++) free(loops[i]);
    free(loops); free(ln);
}

int xt_mesh(const XtSchema *s, const uint8_t *xb, size_t n, XtMesh *out) {
    memset(out, 0, sizeof *out);
    Model m = { s, NULL, 0 };
    if (!parse(&m, xb, n, out->error)) { free(m.by_idx); return 0; }
    TriBuf tb = { 0 }; int fcap = 0;
    for (int i = 2; i <= m.maxidx; i++) {
        const Node *f = node(&m, i);
        if (!f) continue;
        out->nnodes++;
        if (!is(&m, f, "FACE")) continue;
        if (out->nfaces == fcap) { fcap = fcap ? fcap * 2 : 64; out->fntri = xrealloc(out->fntri, sizeof(int) * (size_t)fcap); }
        int t0 = tb.n;
        out->nfaces++;
        const Node *srf = node(&m, fptr(&m, f, "surface"));
        int fs = fchr(&m, f, "sense");
        Ring rings[64]; int nr = 0;
        int lguard = 0;                                  /* a cyclic loop list must not hang part open */
        for (const Node *lp = node(&m, fptr(&m, f, "loop")); lp && nr < 64 && lguard++ < 4096; lp = node(&m, fptr(&m, lp, "next"))) {
            memset(&rings[nr], 0, sizeof(Ring));
            if (loop_ring(&m, lp, &rings[nr])) nr++; else free(rings[nr].p);
        }
        if (nr && is(&m, srf, "PLANE")) { mesh_plane(&m, srf, fs, rings, nr, &tb); out->nfaces_meshed++; }
        else if (nr && is(&m, srf, "CYLINDER")) { mesh_cylinder(&m, srf, fs, rings, nr, &tb); out->nfaces_meshed++; }
        for (int k = 0; k < nr; k++) free(rings[k].p);
        out->fntri[out->nfaces - 1] = (tb.n - t0) / 18;
    }
    free(m.by_idx);
    out->tri = tb.v; out->ntri = tb.n / 18;
    return 1;
}

void xt_mesh_free(XtMesh *m) { free(m->tri); free(m->fntri); memset(m, 0, sizeof *m); }

/* the FACE nodes of a body in XT node index order = xt_mesh's face order (the loop above) = the keys of xt_occt's face_of
   (viewer/occt/xt_occt.h); sldtoolp.c (S3 of formats/surface_selection.md) names solid faces by this ordinal */
int xt_faces(const XtSchema *s, const uint8_t *xb, size_t n, int **node_idx, char *err) {
    char e[128] = ""; Model m = { s, NULL, 0 };
    if (node_idx) *node_idx = NULL;
    if (!s) { if (err) snprintf(err, 128, "no schema"); return -1; }
    if (!parse(&m, xb, n, e)) { free(m.by_idx); if (err) snprintf(err, 128, "%s", e); return -1; }
    int nf = 0, cap = 0, *ix = NULL;
    for (int i = 2; i <= m.maxidx; i++) {
        const Node *f = node(&m, i);
        if (!f || !is(&m, f, "FACE")) continue;
        if (node_idx) { if (nf == cap) { cap = cap ? cap * 2 : 64; ix = xrealloc(ix, sizeof *ix * (size_t)cap); } ix[nf] = i; }
        nf++;
    }
    free(m.by_idx);
    if (node_idx) *node_idx = ix;
    return nf;
}

/* ------------------------------------------------------------------------------------------------ solid chains */
/* the edges of one loop (from its first halfedge, forward) appended to *out (mm, XY arcs) */
static void loop_edges(Model *mp, const Node *lp, XtEdge **out, int *ne, int *cap) {
#define m (*mp)
    const Node *first = lp ? node(&m, fptr(&m, lp, "halfedge")) : NULL, *he = first; int guard = 0;
    while (he && guard++ < 100000) {
        const Node *pv = node(&m, fptr(&m, he, "backward")), *e = node(&m, fptr(&m, he, "edge"));
        const Node *c = e ? node(&m, fptr(&m, e, "curve")) : NULL;
        P3 b = vertex_pos(&m, he), a = pv ? vertex_pos(&m, pv) : b;
        if (*ne == *cap) { *cap = *cap ? *cap * 2 : 16; *out = xrealloc(*out, *cap * sizeof(XtEdge)); }
        XtEdge *x = &(*out)[*ne]; memset(x, 0, sizeof *x);
        if (is(&m, c, "CIRCLE")) {
            P3 ctr = vec(&m, c, "centre"), nrm = norm(vec(&m, c, "normal"));
            int fwd = (fchr(&m, he, "sense") != '-') == (fchr(&m, c, "sense") != '-');
            int ccw = fwd == (nrm.z > 0);
            x->type = 4; x->c[0] = ctr.x * 1000; x->c[1] = ctr.y * 1000; x->c[2] = ctr.z * 1000; x->r = fdbl(&m, c, "radius", 0) * 1000;
            if (isnan(a.x)) { x->a0 = 0; x->sw = ccw ? 2 * M_PI : -2 * M_PI; a = b = p3(ctr.x + x->r / 1000, ctr.y, ctr.z); }
            else {
                x->a0 = atan2(a.y - ctr.y, a.x - ctr.x); double a1 = atan2(b.y - ctr.y, b.x - ctr.x);
                x->sw = ccw ? fmod(a1 - x->a0 + 4 * M_PI, 2 * M_PI) : -fmod(x->a0 - a1 + 4 * M_PI, 2 * M_PI);
                if (fabs(x->sw) < 1e-9) x->sw = ccw ? 2 * M_PI : -2 * M_PI;
            }
        } else x->type = 2;
        x->a[0] = a.x * 1000; x->a[1] = a.y * 1000; x->a[2] = a.z * 1000;
        x->b[0] = b.x * 1000; x->b[1] = b.y * 1000; x->b[2] = b.z * 1000;
        (*ne)++;
        he = node(&m, fptr(&m, he, "forward"));
        if (he == first) break;
    }
#undef m
}

/* Every loop of the highest planar face whose outward normal is +Z, as lines and XY arcs in mm, loops in the face's
 * XT loop order (FACE.loop, LOOP.next), each in its own order (material on the left: the outer loop counter-clockwise,
 * holes clockwise). loop_n[k] = edge count of loop k (at most max_loops loops), *outer = index of the outer loop (the
 * largest |area|). Returns the edge count; *out malloc'd. */
int xt_top_face_loops(const XtSchema *s, const uint8_t *xb, size_t n, XtEdge **out, int *loop_n, int max_loops, int *nloops, int *outer) {
    *out = NULL; *nloops = 0; *outer = -1;
    char err[128]; Model m = { s, NULL, 0 };
    if (!parse(&m, xb, n, err)) { free(m.by_idx); return 0; }
    const Node *best = NULL; double bz = -1e300;
    for (int i = 2; i <= m.maxidx; i++) {
        const Node *f = node(&m, i);
        if (!f || !is(&m, f, "FACE")) continue;
        const Node *srf = node(&m, fptr(&m, f, "surface"));
        if (!is(&m, srf, "PLANE")) continue;
        double nz = fdbl(&m, srf, "normal", 2);
        if (fchr(&m, srf, "sense") == '-') nz = -nz;
        if (fchr(&m, f, "sense") == '-') nz = -nz;
        double z = fdbl(&m, srf, "pvec", 2);
        if (nz > 0.999999 && z > bz) { bz = z; best = f; }
    }
    int ne = 0, cap = 0;
    if (best) {
        double barea = 0; int lguard = 0;
        for (const Node *lp = node(&m, fptr(&m, best, "loop")); lp && lguard++ < 4096 && *nloops < max_loops; lp = node(&m, fptr(&m, lp, "next"))) {
            Ring r = { 0 };
            if (loop_ring(&m, lp, &r)) {
                double a = 0;
                for (int k = 0; k < r.n; k++) { int q = (k + 1) % r.n; a += r.p[k].x * r.p[q].y - r.p[q].x * r.p[k].y; }
                if (fabs(a) > barea) { barea = fabs(a); *outer = *nloops; }
            }
            free(r.p);
            int n0 = ne; loop_edges(&m, lp, out, &ne, &cap);
            loop_n[(*nloops)++] = ne - n0;
        }
    }
    free(m.by_idx);
    return ne;
}

/* Outer loop of the highest planar face whose outward normal is +Z, as lines and XY arcs in mm in loop order
 * (material on the left).  Used for "Solid Chain N" operation chains, which name the solid but store no edges. */
int xt_top_loop(const XtSchema *s, const uint8_t *xb, size_t n, XtEdge **out) {
    int ln[4096], nl, ol, ne = xt_top_face_loops(s, xb, n, out, ln, 4096, &nl, &ol);
    if (ol < 0) { free(*out); *out = NULL; return 0; }
    int k0 = 0; for (int k = 0; k < ol; k++) k0 += ln[k];
    if (k0) memmove(*out, *out + k0, ln[ol] * sizeof(XtEdge));
    (void)ne;
    return ln[ol];
}

/* Parasolid schema for an XT blob: key from its header (SCH=SCH_<modeller>_<base>), searched in known folders */
/* 3drough: also returns the schema file found (path_out, may be NULL), for the OCCT reader (occt_mesh.cpp) */
XtSchema *xt_schema_find(const uint8_t *xb, size_t n, char *path_out, size_t path_n) {
    static struct { char key[32]; XtSchema *s; char path[2200]; } cache[8]; static int nc;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;   /* Dynamic ops read solid chains on worker threads (cam2d_dyn.c) */
    const char *h = mem_mem(xb, n, "SCH=SCH_", 8);
    char key[32] = "26105";
    if (h) {
        size_t left = n - (size_t)((const uint8_t *)h - xb);
        const char *e = memchr(h, ';', left < 64 ? left : 64);
        const char *u = e ? mem_rchr(h, '_', e - h) : NULL;
        if (u && e - u - 1 < 31) { memcpy(key, u + 1, e - u - 1); key[e - u - 1] = 0; }
    }
    pthread_mutex_lock(&mu);
    for (int i = 0; i < nc; i++) if (!strcmp(cache[i].key, key)) { if (path_out) snprintf(path_out, path_n, "%s", cache[i].path); XtSchema *r = cache[i].s; pthread_mutex_unlock(&mu); return r; }
    char dirs[8][2048]; int nd = 0;                       /* MCAM_SCHEMA_DIR, paths.conf "schema" list, ./schema */
    snprintf(dirs[nd++], 2048, "%s", getenv("MCAM_SCHEMA_DIR") ? getenv("MCAM_SCHEMA_DIR") : ".");
    { char one[1024]; for (const char *r = site_path("schema"); nd < 7 && (r = path_list_next(r, one, sizeof one)); ) if (one[0]) host_path(one, dirs[nd++], 2048); }
    snprintf(dirs[nd++], 2048, "schema");
    XtSchema *s = NULL; char p[2200] = "";
    for (int d = 0; d < nd && !s; d++) {
        snprintf(p, sizeof p, "%s" PATH_SEP_S "sch_%s.s_t", dirs[d], key); s = xt_schema_load(p);
        if (!s) { snprintf(p, sizeof p, "%s" PATH_SEP_S "SCH_%s.S_T", dirs[d], key); s = xt_schema_load(p); }
    }
    if (!s) p[0] = 0;
    if (path_out) snprintf(path_out, path_n, "%s", p);
    if (nc < 8) { snprintf(cache[nc].key, 32, "%s", key); cache[nc].s = s; snprintf(cache[nc].path, sizeof cache[nc].path, "%s", p); nc++; }
    pthread_mutex_unlock(&mu);
    return s;
}
XtSchema *xt_schema_for(const uint8_t *xb, size_t n) { return xt_schema_find(xb, n, NULL, 0); }


/* vertical (axis along Z) cylinder surfaces of a body: x, y, radius in mm; returns the count written */
int xt_cylinders(const XtSchema *s, const uint8_t *xb, size_t n, double *xyr, int max) {
    char err[128]; Model m = { s, NULL, 0 }; int k = 0;
    if (!parse(&m, xb, n, err)) { free(m.by_idx); return 0; }
    for (int i = 2; i <= m.maxidx && k < max; i++) {
        const Node *c = node(&m, i);
        if (!c || !is(&m, c, "CYLINDER")) continue;
        if (fabs(fabs(fdbl(&m, c, "axis", 2)) - 1) > 1e-6) continue;
        xyr[3 * k] = fdbl(&m, c, "pvec", 0) * 1000; xyr[3 * k + 1] = fdbl(&m, c, "pvec", 1) * 1000; xyr[3 * k + 2] = fdbl(&m, c, "radius", 0) * 1000; k++;
    }
    free(m.by_idx);
    return k;
}

/* ------------------------------------------------------------------------------------------------ B-rep edges */
/* EDGE(curve, halfedge) -> HALFEDGE(vertex, other, sense): the edge's fin runs from its partner fin's vertex to its
   own (formats/xt.md: a fin's vertex is at its forward end); a fin with sense '-' runs against its curve. */
int xt_brep_edges(const XtSchema *s, const uint8_t *xb, size_t n, double scale, XtBEdge **out, int *nskip) {
    *out = NULL; if (nskip) *nskip = 0;
    char err[128]; Model m = { s, NULL, 0 };
    if (!parse(&m, xb, n, err)) { free(m.by_idx); return 0; }
    int ne = 0, cap = 0, skip = 0;
    for (int i = 2; i <= m.maxidx; i++) {
        const Node *e = node(&m, i);
        if (!e || !is(&m, e, "EDGE")) continue;
        const Node *c = node(&m, fptr(&m, e, "curve")), *he = node(&m, fptr(&m, e, "halfedge"));
        const Node *ot = he ? node(&m, fptr(&m, he, "other")) : NULL;
        if (!c && he) c = node(&m, fptr(&m, he, "curve"));
        if (!he || !c || !(is(&m, c, "LINE") || is(&m, c, "CIRCLE"))) { skip++; continue; }
        P3 b = vertex_pos(&m, he), a = ot ? vertex_pos(&m, ot) : p3(NAN, NAN, NAN);
        XtBEdge x; memset(&x, 0, sizeof x);
        if (is(&m, c, "LINE")) {
            if (isnan(a.x) || isnan(b.x)) { skip++; continue; }
            x.np = 2; x.pts = xmalloc(6 * sizeof(double));
            x.pts[0] = a.x * scale; x.pts[1] = a.y * scale; x.pts[2] = a.z * scale; x.pts[3] = b.x * scale; x.pts[4] = b.y * scale; x.pts[5] = b.z * scale;
        } else {
            if (fchr(&m, c, "sense") == '-') { skip++; continue; }        /* a reversed circle's parametrisation: NOT DECODED */
            P3 ctr = vec(&m, c, "centre"), nrm = norm(vec(&m, c, "normal")), xa = norm(vec(&m, c, "x_axis")), ya = cross(nrm, xa);
            double rad = fdbl(&m, c, "radius", 0), a0, sw;
            int fwd = fchr(&m, he, "sense") != '-';
            if (isnan(a.x) != isnan(b.x)) { skip++; continue; }
            if (isnan(a.x)) { x.arc = 2; a0 = 0; sw = 2 * M_PI; x.t0 = 0; x.t1 = 2 * M_PI; }
            else {
                P3 da = sub(a, ctr), db = sub(b, ctr);
                double aa = atan2(dot(da, ya), dot(da, xa)), ab = atan2(dot(db, ya), dot(db, xa));
                double ccw = fmod(ab - aa + 4 * M_PI, 2 * M_PI);              /* a -> b about the normal */
                if (fwd) { a0 = aa; sw = ccw; } else { a0 = ab; sw = fmod(aa - ab + 4 * M_PI, 2 * M_PI); }
                if (sw < 1e-12) sw = 2 * M_PI;                                  /* a closed edge with one vertex */
                x.arc = 1; x.t0 = fmod(a0 + 2 * M_PI, 2 * M_PI); x.t1 = x.t0 + sw;
            }
            x.c[0] = ctr.x * scale; x.c[1] = ctr.y * scale; x.c[2] = ctr.z * scale;
            x.ax[0] = nrm.x; x.ax[1] = nrm.y; x.ax[2] = nrm.z; x.xa[0] = xa.x; x.xa[1] = xa.y; x.xa[2] = xa.z; x.r = rad * scale;
            int k = (int)ceil(sw / (M_PI / 36)); if (k < 1) k = 1;              /* xt_mesh's 5 degree sampling */
            x.np = k + 1; x.pts = xmalloc(3 * sizeof(double) * x.np);
            for (int j = 0; j <= k; j++) {
                double t = (fwd || x.arc == 2) ? a0 + sw * j / k : a0 + sw * (k - j) / k;   /* in the fin's direction: pts[0] = start vertex */
                P3 q = add(ctr, add(mul(xa, rad * cos(t)), mul(ya, rad * sin(t))));
                x.pts[3 * j] = q.x * scale; x.pts[3 * j + 1] = q.y * scale; x.pts[3 * j + 2] = q.z * scale;
            }
            if (x.arc == 1) {                                                   /* the ends are the vertices themselves */
                x.pts[0] = a.x * scale; x.pts[1] = a.y * scale; x.pts[2] = a.z * scale;
                x.pts[3 * k] = b.x * scale; x.pts[3 * k + 1] = b.y * scale; x.pts[3 * k + 2] = b.z * scale;
            }
        }
        if (ne == cap) { cap = cap ? cap * 2 : 64; *out = xrealloc(*out, cap * sizeof(XtBEdge)); }
        (*out)[ne++] = x;
    }
    free(m.by_idx);
    if (nskip) *nskip = skip;
    return ne;
}
void xt_bedges_free(XtBEdge *e, int n) { for (int i = 0; i < n; i++) free(e[i].pts); free(e); }

/* ------------------------------------------------------------------------------------------------ topology */
/* xt_brep_topo (btopo.h): the faces / loops / fins of the part's Parasolid body for solid chaining, in XT order (FACE
   nodes in index order, FACE.loop -> LOOP.next, LOOP.halfedge -> HALFEDGE.forward: the order PK_FACE_ask_loops /
   PK_LOOP_ask_fins walk, formats/chaining.md "Solid chaining"). Edge numbers = xt_brep_edges' (the same node walk and
   skips; a skipped edge -> fin e = -1). A fin is positive (PK_FIN_is_positive) when it is its edge's own halfedge -
   xt_brep_edges gives the edge that fin's direction. Normals: PLANE (normal x surface sense x face sense, xt_top_loop's
   rule) and CYLINDER faces whose axis is parallel to Z (normal perpendicular to Z); others unknown. Triangles from
   xt_mesh's face meshing (planes / cylinders), x scale. */
int xt_brep_topo(const XtSchema *s, const uint8_t *xb, size_t n, double scale, BTopo *t) {
    memset(t, 0, sizeof *t);
    char err[128]; Model m = { s, NULL, 0 };
    if (!parse(&m, xb, n, err)) { free(m.by_idx); return 0; }
    int *enum_ = xmalloc(sizeof(int) * (m.maxidx + 1)); for (int i = 0; i <= m.maxidx; i++) enum_[i] = -1;
    int ne = 0;
    for (int i = 2; i <= m.maxidx; i++) {                 /* xt_brep_edges' walk and skips */
        const Node *e = node(&m, i);
        if (!e || !is(&m, e, "EDGE")) continue;
        const Node *c = node(&m, fptr(&m, e, "curve")), *he = node(&m, fptr(&m, e, "halfedge"));
        const Node *ot = he ? node(&m, fptr(&m, he, "other")) : NULL;
        if (!c && he) c = node(&m, fptr(&m, he, "curve"));
        if (!he || !c || !(is(&m, c, "LINE") || is(&m, c, "CIRCLE"))) continue;
        P3 b = vertex_pos(&m, he), a = ot ? vertex_pos(&m, ot) : p3(NAN, NAN, NAN);
        if (is(&m, c, "LINE")) { if (isnan(a.x) || isnan(b.x)) continue; }
        else { if (fchr(&m, c, "sense") == '-') continue; if (isnan(a.x) != isnan(b.x)) continue; }
        enum_[i] = ne++;
    }
    int capf = 0, capl = 0, capi = 0;
    for (int i = 2; i <= m.maxidx; i++) {
        const Node *f = node(&m, i);
        if (!f || !is(&m, f, "FACE")) continue;
        if (t->nface == capf) { capf = capf ? capf * 2 : 64; t->face = xrealloc(t->face, sizeof(TFace) * capf); }
        int fi = t->nface++; TFace *F = &t->face[fi]; memset(F, 0, sizeof *F); F->l0 = t->nloop;
        const Node *srf = node(&m, fptr(&m, f, "surface"));
        int fs = fchr(&m, f, "sense");
        double sg = (fs == '-' ? -1 : 1) * (srf && fchr(&m, srf, "sense") == '-' ? -1 : 1);
        if (is(&m, srf, "PLANE")) { P3 nr = norm(vec(&m, srf, "normal")); F->nrm[0] = sg * nr.x; F->nrm[1] = sg * nr.y; F->nrm[2] = sg * nr.z; F->has_nrm = 1; }
        else if (is(&m, srf, "CYLINDER")) { P3 ax = norm(vec(&m, srf, "axis")); if (fabs(fabs(ax.z) - 1) < 1e-9) { F->has_nrm = 1; F->nrm[0] = 1; } }   /* |normal . Z| = 0; its XY direction is not used */
        Ring rings[64]; int nr = 0, lguard = 0;
        for (const Node *lp = node(&m, fptr(&m, f, "loop")); lp && lguard++ < 4096; lp = node(&m, fptr(&m, lp, "next"))) {
            if (nr < 64) { memset(&rings[nr], 0, sizeof(Ring)); if (loop_ring(&m, lp, &rings[nr])) nr++; else free(rings[nr].p); }
            const Node *first = node(&m, fptr(&m, lp, "halfedge")), *he = first; int guard = 0;
            if (!first) continue;
            if (t->nloop == capl) { capl = capl ? capl * 2 : 64; t->loop = xrealloc(t->loop, sizeof(TLoop) * capl); }
            TLoop *L = &t->loop[t->nloop++]; L->f0 = t->nfin; L->nf = 0; L->face = fi;
            do {
                int ei = fptr(&m, he, "edge"); const Node *e = node(&m, ei);
                if (t->nfin == capi) { capi = capi ? capi * 2 : 256; t->fin = xrealloc(t->fin, sizeof(TFin) * capi); }
                t->fin[t->nfin].e = e && ei <= m.maxidx ? enum_[ei] : -1;
                t->fin[t->nfin].pos = e && node(&m, fptr(&m, e, "halfedge")) == he;
                t->nfin++; L->nf++;
                he = node(&m, fptr(&m, he, "forward"));
            } while (he && he != first && ++guard < 100000);
        }
        F->nl = t->nloop - F->l0;
        TriBuf tb = { 0 };
        if (nr && is(&m, srf, "PLANE")) mesh_plane(&m, srf, fs, rings, nr, &tb);
        else if (nr && is(&m, srf, "CYLINDER")) mesh_cylinder(&m, srf, fs, rings, nr, &tb);
        for (int k = 0; k < nr; k++) free(rings[k].p);
        F->ntri = tb.n / 18; F->tri = xmalloc(sizeof(float) * 9 * (F->ntri ? F->ntri : 1));
        for (int k = 0; k < F->ntri * 3; k++) for (int j = 0; j < 3; j++) F->tri[3 * k + j] = (float)(tb.v[6 * k + j] * scale);
        free(tb.v);
    }
    free(enum_); free(m.by_idx);
    return t->nface;
}
