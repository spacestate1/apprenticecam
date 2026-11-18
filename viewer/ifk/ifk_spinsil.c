/* ifk_spinsil.c - Turn Profile kernel, port of IFSpinsil::Run (ifspinsil.dll) + Stitch (interfacial02.dll) +
 * Shell2D. Every rule cites the decompile (decomp/interfacial/ dumps, formats/interfacial.md 6 / 6.1 / 7).
 *
 * Data structures are ours (arrays with indices instead of pooled linked records). Where the original iterates a
 * container ordered by heap pointers (the EdgeSet edge tree keyed by vertex pointers, FUN_1800373c0) we use
 * the emulated address of the record instead (vaddr: a pool block hands out decreasing addresses; block order is the
 * heap's, assumed increasing; formats/interfacial.md 8 / 12). Lists the original
 * builds by push-front (FUN_180005000) are iterated from the newest entry, as there. */
#include "ifk_spinsil.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPS_IN 9.5367431640625e-07          /* 2^-20, EdgeSet / IFSpinsil::Run */
#define ULP53 1.1102230246251565e-16         /* 2^-53, FUN_180004030 */
#define ULP52 2.220446049250313e-16          /* 2^-52, FUN_1800042a0 / FUN_180004180 */

/* ------------------------------------------------------------------ growable int list (push = push-front) */
typedef struct { int *v; int n, cap; } IList;
static void il_push(IList *l, int x)
{
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 4; l->v = realloc(l->v, (size_t)l->cap * sizeof(int)); }
    l->v[l->n++] = x;
}
/* front = newest = v[n-1] */
static int il_find(const IList *l, int x) { for (int i = l->n - 1; i >= 0; i--) if (l->v[i] == x) return i; return -1; }
static void il_remove_at(IList *l, int i) { memmove(l->v + i, l->v + i + 1, (size_t)(l->n - i - 1) * sizeof(int)); l->n--; }
static void il_free(IList *l) { free(l->v); l->v = NULL; l->n = l->cap = 0; }

/* ------------------------------------------------------------------ filtered numbers (value, error / 2^-53) */
typedef struct { double v, e; } Fn;
static Fn f_exact(double x) { Fn r = { x, 0.0 }; return r; }                       /* FUN_18003fe40 */
static Fn f_abs(double x) { Fn r = { x, fabs(x) }; return r; }                      /* FUN_18003fdd0 */
static Fn f_dd(double a, double b) { Fn r; r.v = a - b; r.e = fabs(r.v); return r; } /* FUN_180003e00 */
static Fn f_fd(Fn a, double b) { Fn r; r.v = a.v - b; r.e = a.e + fabs(r.v); return r; } /* FUN_180003f10 */
static Fn f_sub(Fn a, Fn b) { Fn r; r.v = a.v - b.v; r.e = a.e + b.e + fabs(r.v); return r; } /* FUN_180003e80 */
static Fn f_add(Fn a, Fn b) { Fn r; r.v = a.v + b.v; r.e = a.e + b.e + fabs(r.v); return r; } /* FUN_180003fa0 */
static Fn f_mul(Fn a, Fn b)                                                          /* FUN_180004030 */
{
    Fn r; double aa = fabs(a.v), ab = fabs(b.v);
    r.v = a.v * b.v;
    r.e = ab * a.e + aa * b.e + a.e * b.e * ULP53 + fabs(r.v);
    return r;
}
static Fn f_div(Fn a, Fn b)                                                          /* FUN_180004180 */
{
    Fn r; double ab = fabs(b.v), be = b.e;
    r.v = a.v / b.v;
    r.e = (a.e + fabs(r.v) * b.e) / (ab - be * ULP52) + fabs(r.v);
    return r;
}
static int f_maybe0(Fn a) { return fabs(a.v) <= a.e * ULP52; }                      /* FUN_1800042a0 */

/* ------------------------------------------------------------------ sign-magnitude big integers (FUN_18000e*) */
#define BL 12
typedef struct { unsigned char pos; uint32_t l[BL]; } Big;   /* pos = sign byte (1 = positive), limbs LSB first */
static int g_overflow;                                       /* DAT_18005eb80 */

static void big_from_double(Big *b, double v, double unit)   /* FUN_180004f20 */
{
    double q = v / unit, hi;
    memset(b, 0, sizeof *b);
    if (v <= 0.0) { b->pos = 0; q = -q; } else b->pos = 1;
    hi = floor(q / 4294967296.0);
    b->l[0] = (uint32_t)(int64_t)(q - hi * 4294967296.0);
    if (4294967296.0 <= hi) g_overflow = 1;
    b->l[1] = (uint32_t)(int64_t)hi;
}
static void mag_sub(uint32_t *d, const uint32_t *a, const uint32_t *b, int top)   /* FUN_18000f090 */
{
    int borrow = a[0] < b[0];
    d[0] = a[0] - b[0];
    for (int i = 1; i <= top; i++) {
        if (borrow) { borrow = (a[i] == 0) || (a[i] - 1 < b[i]); d[i] = a[i] - b[i] - 1; }
        else { borrow = a[i] < b[i]; d[i] = a[i] - b[i]; }
    }
}
/* FUN_18000ed50: d = a (+/-) b where bsign is b's effective sign byte */
static void big_addsub(Big *d, const Big *a, unsigned char bsign, const uint32_t *bl, int n)
{
    Big r; memset(&r, 0, sizeof r);
    if (a->pos == bsign) {
        int carry = (uint32_t)~a->l[0] < bl[0];
        r.l[0] = a->l[0] + bl[0];
        for (int i = 1; i < n; i++) {
            if (carry) { carry = (~a->l[i] == 0) || (~a->l[i] - 1 < bl[i]); r.l[i] = a->l[i] + 1 + bl[i]; }
            else { carry = ~a->l[i] < bl[i]; r.l[i] = a->l[i] + bl[i]; }
        }
        if (carry) g_overflow = 1;
        r.pos = a->pos;
    } else {
        int i = n;
        while (--i >= 0 && a->l[i] == bl[i]) r.l[i] = 0;
        if (i < 0) r.pos = 1;
        else if (bl[i] < a->l[i]) { mag_sub(r.l, a->l, bl, i); r.pos = a->pos; }
        else { mag_sub(r.l, bl, a->l, i); r.pos = bsign; }
    }
    *d = r;
}
static void big_sub(Big *d, const Big *a, const Big *b, int n) { big_addsub(d, a, (unsigned char)(b->pos == 0), b->l, n); } /* e850 */
static void big_add(Big *d, const Big *a, const Big *b, int n) { big_addsub(d, a, b->pos, b->l, n); }                       /* e930 */
static void big_mul(Big *d, const Big *a, int n, const Big *b, int m)   /* FUN_18000eac0 (schoolbook, exact) */
{
    Big r; memset(&r, 0, sizeof r);
    for (int j = 0; j < m; j++) {
        uint64_t carry = 0;
        for (int i = 0; i < n; i++) {
            uint64_t t = (uint64_t)a->l[i] * b->l[j] + r.l[i + j] + carry;
            r.l[i + j] = (uint32_t)t; carry = t >> 32;
        }
        r.l[j + n] = (uint32_t)carry;
    }
    r.pos = (unsigned char)(a->pos == b->pos);
    *d = r;
}
static int big_sign(const Big *a, int n)   /* FUN_18000e8b0 */
{
    for (int i = 0; i < n; i++) if (a->l[i] != 0) return a->pos ? 1 : -1;
    return 0;
}
static int mag_cmp(const Big *a, const Big *b, int n)   /* FUN_18000f000 */
{
    for (int i = n - 1; i >= 0; i--) { if (b->l[i] < a->l[i]) return 1; if (a->l[i] < b->l[i]) return -1; }
    return 0;
}
static int big_cmp(const Big *a, const Big *b, int n)   /* FUN_18000ea10 */
{
    int sa = big_sign(a, n), sb = big_sign(b, n);
    if (sa == -1) return sb == -1 ? -mag_cmp(a, b, n) : -1;
    if (sa == 1) return sb == 1 ? mag_cmp(a, b, n) : 1;
    return -sb;
}
static double big_to_double(const Big *a, int n, double scale)   /* FUN_180004cc0 (from the disassembly) */
{
    double sum = 0.0, s = scale;
    for (int i = 0; i < n; i++) { sum = sum + (double)a->l[i] * s; s = s * 4294967296.0; }
    return a->pos ? sum : -sum;
}

/* ================================================================== Stitch (interfacial02, section 7) */
typedef struct { double p[3]; int pinned, rep, open; } SNode;
typedef struct { int a, b; } EK;
static int cmp_ek(const void *x, const void *y)
{
    const EK *p = x, *q = y;
    return p->a != q->a ? (p->a < q->a ? -1 : 1) : (p->b != q->b ? (p->b < q->b ? -1 : 1) : 0);
}
static const SNode *g_snodes;
static int cmp_snode_x(const void *x, const void *y)
{
    double a = g_snodes[*(const int *)x].p[0], b = g_snodes[*(const int *)y].p[0];
    return a < b ? -1 : a > b ? 1 : (*(const int *)x < *(const int *)y ? -1 : 1);
}

static void stitch(const IfkMesh *in, double tol, int **out_tri, int *out_ntri, SNode **out_nodes, int *nn)
{
    int N = in->nnode, T = in->ntri;
    SNode *nd = calloc((size_t)N, sizeof *nd);
    double tol2 = tol * tol;
    /* Stitch::AddNode / AddTriangle prepend (180006410 / 1800065c0): Stitch iterates nodes and triangles newest
     * first, i.e. in reverse input order. Node k of the iteration = input node N-1-k. */
    for (int i = 0; i < N; i++) { memcpy(nd[i].p, in->node + 3 * i, 3 * sizeof(double)); nd[i].rep = -1; }

    /* 1. pre-pass FUN_18000b3a0, mode 0: all pinned; nodes of edges used by < 2 triangles: open, unpinned.
     *    TransferToStitch passes pinned = false (FUN_1800011c0), so nothing is re-pinned at the end. */
    {
        EK *ek = malloc((size_t)T * 3 * sizeof *ek);
        int ne = 0;
        for (int t = 0; t < T; t++)
            for (int k = 0; k < 3; k++) {
                int a = in->tri[3 * t + k], b = in->tri[3 * t + (k + 1) % 3];
                ek[ne].a = a < b ? a : b; ek[ne].b = a < b ? b : a; ne++;
            }
        qsort(ek, (size_t)ne, sizeof *ek, cmp_ek);
        for (int i = 0; i < N; i++) nd[i].pinned = 1;
        for (int i = 0; i < ne;) {
            int j = i; while (j < ne && ek[j].a == ek[i].a && ek[j].b == ek[i].b) j++;
            if (j - i < 2) { nd[ek[i].a].open = nd[ek[i].b].open = 1; nd[ek[i].a].pinned = nd[ek[i].b].pinned = 0; }
            i = j;
        }
        free(ek);
    }

    /* 2-3. node merge FUN_18000a560, greedy in Stitch's iteration order (reverse input order); the octree query is
     *      complete (straddling queries are cloned, FUN_18000d980), so a sorted sweep on x gives the same sets. */
    {
        int *ord = malloc((size_t)N * sizeof(int));
        for (int i = 0; i < N; i++) ord[i] = i;
        g_snodes = nd;
        qsort(ord, (size_t)N, sizeof(int), cmp_snode_x);
        int *pos = malloc((size_t)N * sizeof(int));
        for (int i = 0; i < N; i++) pos[ord[i]] = i;
        for (int k = 0; k < N; k++) {
            int i = N - 1 - k;
            if (nd[i].rep >= 0) continue;
            nd[i].rep = i;
            if (nd[i].pinned) continue;
            const double *P = nd[i].p;
            for (int dir = -1; dir <= 1; dir += 2)
                for (int s = pos[i] + dir; s >= 0 && s < N; s += dir) {
                    SNode *q = &nd[ord[s]];
                    double dx = q->p[0] - P[0];
                    if (dx * dx > tol2) break;
                    if (q->rep >= 0 || q->pinned) continue;
                    double dy = q->p[1] - P[1], dz = q->p[2] - P[2];
                    if (dy * dy + dx * dx + dz * dz <= tol2) q->rep = i;   /* order as in a560 */
                }
        }
        free(ord); free(pos);
    }

    /* 4. remap triangles, drop degenerate ones. Phase 4 walks Stitch's list (reverse input) and pushes each kept
     *    triangle to the front of a new list, which phase 6 walks (FUN_180009d80 l.93 / l.234): input order. */
    int *tri = malloc((size_t)T * 3 * sizeof(int)), nt = 0;
    for (int t = 0; t < T; t++) {
        int a = nd[in->tri[3 * t]].rep, b = nd[in->tri[3 * t + 1]].rep, c = nd[in->tri[3 * t + 2]].rep;
        if (a == b || a == c || b == c) continue;
        tri[3 * nt] = a; tri[3 * nt + 1] = b; tri[3 * nt + 2] = c; nt++;
    }

    /* 5-6. T-junctions (FUN_18000a7a0 / FUN_180005b80) and the fan split (FUN_18000aa60) are applied by the caller
     *      through stitch_tjunctions(). */
    *out_tri = tri; *out_ntri = nt; *out_nodes = nd; *nn = N;
}

/* FUN_180005b80: P strictly inside segment AB within tol (tol2 = tol^2) and not within tol of an end. */
static int near_edge(const double *P, double tol2, const double *A, const double *B)
{
    double d[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
    double len2 = d[1] * d[1] + d[0] * d[0] + d[2] * d[2];
    double dot = (P[1] - A[1]) * d[1] + d[0] * (P[0] - A[0]) + (P[2] - A[2]) * d[2];
    if (!(0.0 < dot && dot < len2)) return 0;
    double t = dot / len2;
    double F[3] = { A[0] + d[0] * t, A[1] + t * d[1], A[2] + t * d[2] };
    double ex = P[0] - F[0], ey = P[1] - F[1], ez = P[2] - F[2];
    if (tol2 < ey * ey + ex * ex + ez * ez) return 0;
    if (t * t * len2 < tol2 &&
        (P[1] - A[1]) * (P[1] - A[1]) + (P[0] - A[0]) * (P[0] - A[0]) + (P[2] - A[2]) * (P[2] - A[2]) < tol2) return 0;
    if ((1.0 - t) * (1.0 - t) * len2 < tol2 &&
        (P[1] - B[1]) * (P[1] - B[1]) + (P[0] - B[0]) * (P[0] - B[0]) + (P[2] - B[2]) * (P[2] - B[2]) < tol2) return 0;
    return 1;
}

/* ================================================================== EdgeSet (ifspinsil, section 6 item 2) */
typedef struct { double p[3]; int v2; } ENode;            /* 3D node + its 2D vertex (node+0x18) */
typedef struct { double x, r; int sv; } EVert;            /* 2D vertex; sv = Shell2D vertex cache (node[2]) */
typedef struct { int a, b; int flag; IList tris; int alive; } EEdge;   /* key (a < b), flag bits 1 / 2 */
typedef struct { int n[3]; int visited; } ETri;           /* original (pre-split) triangle */

typedef struct {
    ENode *nd; int nnd, cnd;
    EVert *vx; int nvx, cvx;
    EEdge *ed; int ned, ced;
    ETri *tr; int ntr, ctr;
    double tol;                                           /* EdgeSet+0x60 */
    /* hash of 2D vertices by snapped coordinates; hash of edges by key */
    int *h; int hcap;
    int *eh; int ehcap;
} ESet;

static uint64_t hkey(double x, double r)
{
    uint64_t a, b; memcpy(&a, &x, 8); memcpy(&b, &r, 8);
    a ^= b * 0x9e3779b97f4a7c15ULL; a ^= a >> 29; a *= 0xbf58476d1ce4e5b9ULL; a ^= a >> 32;
    return a;
}
static void eh_grow(ESet *s)
{
    int cap = s->hcap ? s->hcap * 2 : 1024;
    int *h = malloc((size_t)cap * sizeof(int));
    for (int i = 0; i < cap; i++) h[i] = -1;
    for (int i = 0; i < s->nvx; i++) {
        uint64_t k = hkey(s->vx[i].x, s->vx[i].r) & (uint64_t)(cap - 1);
        while (h[k] >= 0) k = (k + 1) & (uint64_t)(cap - 1);
        h[k] = i;
    }
    free(s->h); s->h = h; s->hcap = cap;
}
/* Emulated address of 2D vertex i (the pointer the EdgeSet keys use, FUN_180037450 / FUN_1800373c0): the vertex pool
 * (EdgeSet+0x28, blocks of 1000, FUN_180010a00) is filled by FUN_180012930 pushing every record of a new block on the
 * free-list head, so a block hands out decreasing addresses; FUN_180037a60 frees the record again when the vertex
 * already exists (FUN_180001e00), so only unique vertices use one: vertex i (creation order among unique vertices) is
 * record 999 - i % 1000 of block i / 1000. Blocks are assumed at increasing addresses (heap order NOT DECODED). */
static int vaddr(int i) { return (i / 1000) * 1000 + 999 - i % 1000; }
#define VLESS(p, q) (vaddr(p) < vaddr(q))

/* FUN_180037a60: snap down to the 2^-20 grid (floor, FUN_180004420), deduplicate */
static int e_vertex(ESet *s, double x, double r)
{
    x = floor(x / EPS_IN) * EPS_IN;
    r = floor(r / EPS_IN) * EPS_IN;
    if ((s->nvx + 1) * 2 > s->hcap) eh_grow(s);
    uint64_t k = hkey(x, r) & (uint64_t)(s->hcap - 1);
    while (s->h[k] >= 0) {
        EVert *v = &s->vx[s->h[k]];
        if (v->x == x && v->r == r) return s->h[k];
        k = (k + 1) & (uint64_t)(s->hcap - 1);
    }
    if (s->nvx == s->cvx) { s->cvx = s->cvx ? s->cvx * 2 : 1024; s->vx = realloc(s->vx, (size_t)s->cvx * sizeof(EVert)); }
    s->vx[s->nvx].x = x; s->vx[s->nvx].r = r; s->vx[s->nvx].sv = -1;
    s->h[k] = s->nvx;
    return s->nvx++;
}
/* slot 1, FUN_1800385c0: keep the 3D node, map it to (x, sqrt(y^2 + z^2)), r < 1e-7 -> 0 */
static int e_node(ESet *s, const double *p)
{
    if (s->nnd == s->cnd) { s->cnd = s->cnd ? s->cnd * 2 : 1024; s->nd = realloc(s->nd, (size_t)s->cnd * sizeof(ENode)); }
    ENode *n = &s->nd[s->nnd];
    memcpy(n->p, p, 3 * sizeof(double));
    double r = sqrt(p[1] * p[1] + p[2] * p[2]);
    if (r < 1e-07) r = 0.0;
    n->v2 = e_vertex(s, p[0], r);
    return s->nnd++;
}
static uint64_t ekey(int a, int b) { uint64_t k = ((uint64_t)(uint32_t)a << 32) | (uint32_t)b; k ^= k >> 31; k *= 0x9e3779b97f4a7c15ULL; return k ^ (k >> 29); }
static void ee_grow(ESet *s)
{
    int cap = s->ehcap ? s->ehcap * 2 : 2048;
    int *h = malloc((size_t)cap * sizeof(int));
    for (int i = 0; i < cap; i++) h[i] = -1;
    for (int i = 0; i < s->ned; i++) {
        if (!s->ed[i].alive) continue;
        uint64_t k = ekey(s->ed[i].a, s->ed[i].b) & (uint64_t)(cap - 1);
        while (h[k] >= 0) k = (k + 1) & (uint64_t)(cap - 1);
        h[k] = i;
    }
    free(s->eh); s->eh = h; s->ehcap = cap;
}
/* FUN_1800378d0: add edge from -> to (2D vertices), flag = mark the direction, tri = original triangle or -1.
 * Key = the vertex pair in pointer order (FUN_180037450); ours = creation order of the vertices. */
static void e_edge(ESet *s, int from, int to, int flag, int tri)
{
    if (from == to) return;
    int a = VLESS(from, to) ? from : to, b = VLESS(from, to) ? to : from;   /* FUN_180037450: smaller pointer first */
    if ((s->ned + 1) * 2 > s->ehcap) ee_grow(s);
    uint64_t k = ekey(a, b) & (uint64_t)(s->ehcap - 1);
    int e = -1;
    while (s->eh[k] >= 0) {
        EEdge *x = &s->ed[s->eh[k]];
        if (x->a == a && x->b == b) { e = s->eh[k]; break; }
        k = (k + 1) & (uint64_t)(s->ehcap - 1);
    }
    if (e < 0) {
        if (s->ned == s->ced) { s->ced = s->ced ? s->ced * 2 : 1024; s->ed = realloc(s->ed, (size_t)s->ced * sizeof(EEdge)); }
        e = s->ned++;
        memset(&s->ed[e], 0, sizeof(EEdge));
        s->ed[e].a = a; s->ed[e].b = b; s->ed[e].alive = 1;
        s->eh[k] = e;
    }
    EEdge *E = &s->ed[e];
    if (flag) E->flag |= (from == a) ? 1 : 2;
    if (E->flag == 3) il_free(&E->tris);           /* FUN_180004e00 */
    else if (tri >= 0 && il_find(&E->tris, tri) < 0) il_push(&E->tris, tri);
}

/* triangle geometry FUN_18000fdf0 */
typedef struct { const double *v[3]; double e[3][3]; double n[3]; int j, k, dom; double proj[3]; double dist; int onedge; } TGeo;
static void v_sub(double *r, const double *a, const double *b) { r[0] = a[0] - b[0]; r[1] = a[1] - b[1]; r[2] = a[2] - b[2]; }
static double v_dot(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void v_cross(double *r, const double *a, const double *b)
{
    double x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    r[0] = x; r[1] = y; r[2] = z;
}
static double v_norm(double *r, const double *a)   /* FUN_18000f670 */
{
    double l = sqrt(v_dot(a, a));
    if (l == 0.0) { r[0] = r[1] = r[2] = 0.0; return 0.0; }
    r[0] = a[0] / l; r[1] = a[1] / l; r[2] = a[2] / l;
    return l;
}
static void tgeo(TGeo *g, const double *a, const double *b, const double *c)
{
    g->v[0] = a; g->v[1] = b; g->v[2] = c;
    for (int i = 0; i < 3; i++) { v_sub(g->e[i], g->v[(i + 1) % 3], g->v[i]); v_norm(g->e[i], g->e[i]); }
    v_cross(g->n, g->e[0], g->e[1]); v_norm(g->n, g->n);
    g->j = 1; g->k = 2; g->dom = 0;
    double m = fabs(g->n[0]);
    for (int i = 1; i < 3; i++) if (m < fabs(g->n[i])) { g->dom = i; g->j = (i + 1) % 3; g->k = (i + 2) % 3; m = fabs(g->n[i]); }
}
/* FUN_180010050: P inside the triangle (projected along the normal), tolerance tol on the edge tests */
static int tinside(TGeo *g, const double *P, double tol)
{
    if (g->n[g->dom] == 0.0) return 0;
    double w[3]; v_sub(w, g->v[0], P);                          /* FUN_18000f250(w, v0, P) */
    g->dist = v_dot(g->n, w);
    g->proj[0] = g->dist * g->n[0]; g->proj[1] = g->dist * g->n[1]; g->proj[2] = g->dist * g->n[2];
    g->proj[0] = g->proj[0] + P[0]; g->proj[1] = g->proj[1] + P[1]; g->proj[2] = g->proj[2] + P[2];
    /* FUN_18000f330 then FUN_18000f2c0(proj, proj, P): proj = (n . (v0 - P)) n + P, P projected on the plane */
    g->onedge = 0;
    for (int i = 0; i < 3; i++) {
        double q[3]; v_sub(q, g->proj, g->v[i]); v_norm(q, q);
        double a = g->e[i][g->j] * q[g->k], b = g->e[i][g->k] * q[g->j];
        if (fabs(a - b) <= tol) g->onedge = 1;
        else if ((b < a) != (0.0 < g->n[g->dom])) return 0;
    }
    if (g->dist < 0.0) g->dist = -g->dist;
    return 1;
}
/* FUN_18000f880: parameter t on line B + t Bd of its closest point to line A + s Ad: the largest component of
 * Bd x Ad picks the index pair (j, k) (local_50 / local_30); the kept magnitude (local_38) is updated by the second
 * compare and used by the third. */
static double line_par(const double *A, const double *Ad, const double *B, const double *Bd)
{
    int k = 1, j;
    double den = Bd[0] * Ad[1] - Ad[0] * Bd[1], keep = fabs(den);
    double d2 = Bd[0] * Ad[2] - Ad[0] * Bd[2];
    if (keep < fabs(d2)) { k = 2; keep = fabs(d2); den = d2; }
    double d3 = Bd[1] * Ad[2] - Ad[1] * Bd[2];
    if (keep < fabs(d3)) { k = 2; den = d3; }
    j = keep < fabs(d3);
    if (den == 0.0) return 0.0;
    return ((A[j] - B[j]) * Ad[k] + (B[k] - A[k]) * Ad[j]) / den;
}
/* FUN_18000fcb0: distance between line P + s D and line Q + t E; *s_on_PD = parameter on P + s D */
static double line_line(const double *P, const double *D, const double *Q, const double *E, double *s_on_PD)
{
    double n[3], w[3], pp[3];
    v_cross(n, D, E); v_norm(n, n);
    v_sub(w, P, Q);
    double h = v_dot(n, w);
    pp[0] = P[0] - h * n[0]; pp[1] = P[1] - h * n[1]; pp[2] = P[2] - h * n[2];
    *s_on_PD = line_par(Q, E, pp, D);
    return fabs(h);
}
static const double AXO[3] = { 0, 0, 0 }, AXD[3] = { 1, 0, 0 };
static int below0(double s) { return s < 0.0 || s - 0.0 <= 1e-07; }   /* FUN_18000f510(s, 0, 1e-7) */
static int above1(double s) { return 1.0 < s || 1.0 - s <= 1e-07; }   /* FUN_18000f570(s, 1, 1e-7) */

/* FUN_180037bb0: the point of edge a -> b closest to the axis, when interior, becomes a node */
static int e_axis_split(ESet *s, int a, int b)
{
    double d[3], s_par;
    const double *A = s->nd[a].p;
    v_sub(d, s->nd[b].p, A);
    if (d[1] == 0.0 && d[2] == 0.0) return -1;
    line_line(A, d, AXO, AXD, &s_par);
    if (below0(s_par) || above1(s_par)) return -1;
    double P[3] = { s_par * d[0] + A[0], s_par * d[1] + A[1], s_par * d[2] + A[2] };
    return e_node(s, P);
}

/* slot 3, FUN_180037d50 */
static void e_triangle(ESet *s, int n0, int n1, int n2)
{
    if (s->ntr == s->ctr) { s->ctr = s->ctr ? s->ctr * 2 : 1024; s->tr = realloc(s->tr, (size_t)s->ctr * sizeof(ETri)); }
    int T = s->ntr++;
    s->tr[T].n[0] = n0; s->tr[T].n[1] = n1; s->tr[T].n[2] = n2; s->tr[T].visited = 0;

    /* queue of sub-triangles (FUN_1800114d0 append / FUN_180008390 pop front) */
    int (*q)[3] = NULL; int qh = 0, qn = 0, qc = 0;
#define QPUSH(A, B, C) do { if (qn == qc) { qc = qc ? qc * 2 : 16; q = realloc(q, (size_t)qc * sizeof *q); } \
        q[qn][0] = (A); q[qn][1] = (B); q[qn][2] = (C); qn++; } while (0)
    {
        double nrm[3], e1[3], e2[3];
        const double *P0 = s->nd[n0].p, *P1 = s->nd[n1].p, *P2 = s->nd[n2].p;
        v_sub(e1, P2, P1); v_sub(e2, P0, P1); v_cross(nrm, e1, e2);   /* FUN_18000f740 */
        int hit = -1;
        double den = v_dot(nrm, AXD);                                  /* FUN_18000f800 */
        if (den != 0.0) {
            double w[3]; v_sub(w, P0, AXO);
            double t = v_dot(nrm, w) / den;
            double P[3] = { t, 0.0, 0.0 };
            TGeo g; tgeo(&g, P0, P1, P2);
            if (tinside(&g, P, 1e-07) && !g.onedge) hit = e_node(s, P);
        }
        if (hit < 0) QPUSH(n0, n1, n2);
        else { QPUSH(n0, n1, hit); QPUSH(n1, n2, hit); QPUSH(n2, n0, hit); }
    }
    while (qh < qn) {
        int t[3] = { q[qh][0], q[qh][1], q[qh][2] }; qh++;
        int i, m = -1;
        for (i = 0; i < 3; i++) {
            m = e_axis_split(s, t[i], t[(i + 1) % 3]);
            if (m >= 0) { QPUSH(t[i], m, t[(i + 2) % 3]); QPUSH(m, t[(i + 1) % 3], t[(i + 2) % 3]); break; }
        }
        if (i < 3) continue;
        int A = s->nd[t[0]].v2, B = s->nd[t[1]].v2, C = s->nd[t[2]].v2;
        /* FUN_18000f7a0 / FUN_180013fe0: z of (C - B) x (A - B) with error bound */
        double ux = s->vx[C].x - s->vx[B].x, uy = s->vx[C].r - s->vx[B].r;
        double wx = s->vx[A].x - s->vx[B].x, wy = s->vx[A].r - s->vx[B].r;
        Fn p1 = f_abs(ux * wy), p2 = f_abs(uy * wx);
        Fn z = f_sub(p1, p2);
        if (f_maybe0(z)) { e_edge(s, A, B, 0, T); e_edge(s, B, C, 0, T); e_edge(s, C, A, 0, T); }
        else if (0.0 <= z.v) { e_edge(s, A, B, 1, T); e_edge(s, B, C, 1, T); e_edge(s, C, A, 1, T); }
        else { e_edge(s, B, A, 1, T); e_edge(s, C, B, 1, T); e_edge(s, A, C, 1, T); }
    }
    free(q);
#undef QPUSH
}

/* FUN_180037540: section point of edge (a, b) at x */
static void sect(double x, const double *a, const double *b, double out[2][3], int *cnt)
{
    if (*cnt < 2 && (x < a[0] || x == a[0] || x < b[0] || x == b[0]) && (a[0] <= x || b[0] <= x) && a[0] != b[0]) {
        double t = (x - a[0]) / (b[0] - a[0]);
        double *p = out[*cnt];
        p[0] = t * (b[0] - a[0]) + a[0]; p[1] = t * (b[1] - a[1]) + a[1]; p[2] = t * (b[2] - a[2]) + a[2];
        (*cnt)++;
    }
}
/* FUN_1800376a0: minimum radius of the triangle's cross-section at x */
static double rmin_at(const double *a, const double *b, const double *c, double x)
{
    double p[2][3] = { { 0 } }; int cnt = 0;
    sect(x, a, b, p, &cnt); sect(x, b, c, p, &cnt); sect(x, c, a, p, &cnt);
    double r = sqrt(p[0][1] * p[0][1] + p[0][2] * p[0][2]);
    double r2 = sqrt(p[1][1] * p[1][1] + p[1][2] * p[1][2]);
    if (r2 < r) r = r2;
    double d[3], s; v_sub(d, p[1], p[0]);
    double dist = line_line(p[0], d, AXO, AXD, &s);
    if (!below0(s) && !above1(s) && dist < r) r = dist;
    if (r < 1e-07) r = 0.0;
    return r;
}
/* FUN_18000fb00: squared distance of P to segment AB (2D points with z = 0) */
static double seg_d2(double px, double py, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay, L = dx * dx + dy * dy;
    double wx = px - ax, wy = py - ay, W = wx * wx + wy * wy, D = dx * wx + dy * wy;
    if (D <= 0.0) return W;
    if (D <= L) { if (L == 0.0) return W; double r = W - D * D / L; return r < 0.0 ? 0.0 : r; }
    double ux = px - bx, uy = py - by; return ux * ux + uy * uy;
}
/* FUN_180038690: inner outline of a triangle (bisection in x on r_min) */
static void e_inner(ESet *s, int T)
{
    const int *n = s->tr[T].n;
    int lo = s->nd[n[0]].v2, hi = lo;
    for (int k = 1; k < 3; k++) {
        int v = s->nd[n[k]].v2;
        if (s->vx[v].x < s->vx[lo].x) lo = v;
        if (s->vx[hi].x < s->vx[v].x) hi = v;
    }
    if (fabs(s->vx[lo].x - s->vx[hi].x) <= 1e-05) return;
    int (*st)[2] = NULL; int sn = 0, sc = 0;
#define SPUSH(A, B) do { if (sn == sc) { sc = sc ? sc * 2 : 16; st = realloc(st, (size_t)sc * sizeof *st); } \
        st[sn][0] = (A); st[sn][1] = (B); sn++; } while (0)
    SPUSH(lo, hi);
    while (sn > 0) {   /* push-front / pop-front = LIFO */
        int A = st[sn - 1][0], B = st[sn - 1][1]; sn--;
        if (fabs(s->vx[A].x - s->vx[B].x) <= 1e-05) { e_edge(s, A, B, 0, -1); continue; }
        double mid = (s->vx[A].x + s->vx[B].x) / 2.0;
        double r = rmin_at(s->nd[n[0]].p, s->nd[n[1]].p, s->nd[n[2]].p, mid);
        double d = sqrt(seg_d2(mid, r, s->vx[A].x, s->vx[A].r, s->vx[B].x, s->vx[B].r));
        if (d < s->tol || d == s->tol) e_edge(s, A, B, 0, -1);
        else { int V = e_vertex(s, mid, r); SPUSH(A, V); SPUSH(V, B); }
    }
    free(st);
#undef SPUSH
}

/* edges in key order (the EdgeSet tree, in-order) */
static const EEdge *g_eedges;
static int cmp_ekey(const void *x, const void *y)
{
    const EEdge *p = &g_eedges[*(const int *)x], *q = &g_eedges[*(const int *)y];   /* FUN_1800373c0 on pointers */
    if (p->a != q->a) return VLESS(p->a, q->a) ? -1 : 1;
    if (p->b != q->b) return VLESS(p->b, q->b) ? -1 : 1;
    return *(const int *)x < *(const int *)y ? -1 : 1;
}
static int *edges_in_key_order(ESet *s, int *n, int skip3)
{
    int *o = malloc(sizeof(int) * (size_t)(s->ned + 1)), m = 0;
    for (int i = 0; i < s->ned; i++) if (s->ed[i].alive && !(skip3 && s->ed[i].flag == 3)) o[m++] = i;
    g_eedges = s->ed;
    qsort(o, (size_t)m, sizeof(int), cmp_ekey);
    *n = m;
    return o;
}
typedef struct { double x, lo, hi; int vlo, vhi; } Iv;
static int cmp_iv(const void *x, const void *y)
{
    const Iv *a = x, *b = y;
    if (a->x != b->x) return a->x < b->x ? -1 : 1;
    if (a->lo != b->lo) return a->lo < b->lo ? -1 : 1;
    return 0;
}
/* slot 5, FUN_180038ae0 */
static void e_end(ESet *s)
{
    /* the triangles of the edges with flag != 3 (key order, each edge's list from its front), marked once and
     * pushed to the front of a list (local_168); that list is then popped from the front: last collected first */
    int n, *o = edges_in_key_order(s, &n, 1);
    int *col = NULL, nc = 0;
    for (int k = 0; k < n; k++) {
        EEdge *E = &s->ed[o[k]];
        for (int i = E->tris.n - 1; i >= 0; i--) {
            int T = E->tris.v[i];
            if (!s->tr[T].visited) { s->tr[T].visited = 1; col = realloc(col, sizeof(int) * (size_t)(nc + 1)); col[nc++] = T; }
        }
    }
    for (int i = nc - 1; i >= 0; i--) e_inner(s, col[i]);
    free(col); free(o);

    /* vertical edges (equal x, flag != 3): removed, grouped by x, r-intervals unioned (touching merges); each union
     * is added back twice (FUN_180010740 is a multiset insert) with flag 0 */
    o = edges_in_key_order(s, &n, 1);
    Iv *iv = NULL; int ni = 0;
    for (int k = 0; k < n; k++) {
        EEdge *E = &s->ed[o[k]];
        if (s->vx[E->a].x != s->vx[E->b].x) continue;
        E->alive = 0;
        int lo = E->a, hi = E->b;
        if (s->vx[hi].r < s->vx[lo].r || s->vx[hi].r == s->vx[lo].r) { int t = lo; lo = hi; hi = t; }
        iv = realloc(iv, (size_t)(ni + 1) * sizeof *iv);
        iv[ni].x = s->vx[lo].x; iv[ni].lo = s->vx[lo].r; iv[ni].hi = s->vx[hi].r; iv[ni].vlo = lo; iv[ni].vhi = hi; ni++;
    }
    free(o);
    qsort(iv, (size_t)ni, sizeof *iv, cmp_iv);
    for (int i = 0; i < ni;) {
        int j = i + 1, vlo = iv[i].vlo, vhi = iv[i].vhi; double hi = iv[i].hi;
        while (j < ni && iv[j].x == iv[i].x && !(hi < iv[j].lo)) { if (hi < iv[j].hi) { hi = iv[j].hi; vhi = iv[j].vhi; } j++; }
        for (int rep = 0; rep < 2; rep++) {
            if (s->ned == s->ced) { s->ced = s->ced * 2 + 16; s->ed = realloc(s->ed, (size_t)s->ced * sizeof(EEdge)); }
            EEdge *E = &s->ed[s->ned++]; memset(E, 0, sizeof *E);
            E->a = VLESS(vlo, vhi) ? vlo : vhi; E->b = VLESS(vlo, vhi) ? vhi : vlo; E->alive = 1;
        }
        i = j;
    }
    free(iv);
}

/* ================================================================== Shell2D (section 6 items 3-4) */
typedef struct {
    Fn c[2];            /* x, y (+0..+0x18) */
    int ea, eb;         /* input edges of a crossing (+0x20 / +0x28); -1 = input vertex */
    int parent;         /* +0x38 */
    int ex_ok; Big X, Y, D;   /* exact (+0x40): input: X, Y 2 limbs in g units; crossing: X, Y 6 limbs g^3, D 4 limbs g^2 */
    IList inc;          /* incident edges (+0x48) */
    int stamp, stamp2;  /* +0x50 and the sort stamp */
    int outid;          /* +0x30 in Run's output loop */
    int committed, rec3;   /* +0x30 during the walk (Shell2D+0x68 mode): the record FUN_18003a890 makes for a crossing
                              on a chosen edge (kept by the rollback, found again through map +0x70); rec3 = its [3] */
    int stamp3;         /* +0x50 in FUN_18003a5e0 (DAT_18005eb08) */
} SVert;
typedef struct {
    int a, b;           /* ends */
    int in;             /* input edge (+0x10) */
    IList hits;         /* +0x18 */
    IList groups;       /* +0x20: the EdgeQuadtree groups holding this input edge, head = last element (push-front);
                           FUN_18003cd90 pops them, so "not resolved yet" = non-empty */
    char visited, rev;  /* +0x28 / +0x29 */
    int stamp;          /* +0x2c */
    int comp;           /* +0x48 (input edges) */
    double bb[4];       /* bounding box of the input edge (candidate search) */
    IList saved;        /* +0x38 record (input edges, Shell2D+0x68 mode): the groups saved by FUN_18003cd90 (push-front) */
    char in80;          /* record +8: on the list +0x80 of edges to restore (FUN_18003a050) */
    char dead;          /* freed by the rollback FUN_18003a5e0 */
} SEdge;

typedef struct {
    double g, g2, g3;   /* Shell2D[0..2] = eps^2, eps^4, eps^6 (FUN_18003dad0 squares its eps) */
    SVert *v; int nv, cv;
    SEdge *e; int ne, ce;
    int *comp_ext; char *comp_done; int ncomp, cur;
    IList out;          /* +0x40, push-front */
    int axis;           /* DAT_18005eb20 */
    int stamp;          /* DAT_18005eb38 / eb50 / eb08 */
    int ok;             /* local_170 */
    char err[160];
    int *cand; int ncand;   /* scratch */
    IList *grp; int ngrp;   /* EdgeQuadtree groups (FUN_18003aca0): each group's edges, head first */
    /* Shell2D+0x68 = 1 (IFSpinsil::Run sets it with FUN_180001280 right after FUN_18003dad0): each walk step is undone
     * by FUN_18003a5e0 except what FUN_18003a890 committed */
    IList sub38;        /* +0x38: sub-edges made or reused by FUN_18003a9e0 in this step (push-front) */
    IList split80;      /* +0x80: edges with hits / split in this step (FUN_18003a050, push-front) */
    IList x20;          /* +0x20: crossing vertices made or reused in this step (push-front) */
    struct KMap *m70;   /* map +0x70: committed crossing vertex by its input-edge pair (FUN_1800399c0 key) */
    struct KMap *m78;   /* map +0x78: committed sub-edge by (its two ends, its input edge) (FUN_1800398c0 key) */
} Shell;

/* the two maps of the Shell2D+0x68 mode: keys are unordered pairs (+ an int); only equality matters (lookups) */
typedef struct KMap { int (*k)[3]; int *v; int cap, n; } KMap;
static uint64_t km_h(const int *k) { uint64_t h = (uint64_t)(uint32_t)k[0] * 0x9e3779b97f4a7c15ULL ^ (uint64_t)(uint32_t)k[1] * 0xc2b2ae3d27d4eb4fULL ^ (uint64_t)(uint32_t)k[2] * 0x165667b19e3779f9ULL; return h ^ (h >> 29); }
static int km_find(const KMap *m, int a, int b, int c)
{
    if (!m || !m->cap) return -1;
    int k[3] = { a < b ? a : b, a < b ? b : a, c };
    for (uint64_t i = km_h(k) & (uint64_t)(m->cap - 1);; i = (i + 1) & (uint64_t)(m->cap - 1)) {
        if (m->v[i] < 0) return -1;
        if (m->k[i][0] == k[0] && m->k[i][1] == k[1] && m->k[i][2] == k[2]) return m->v[i];
    }
}
static void km_put(KMap *m, int a, int b, int c, int val)
{
    if ((m->n + 1) * 2 > m->cap) {
        KMap o = *m; m->cap = o.cap ? 2 * o.cap : 1024; m->n = 0;
        m->k = malloc(sizeof(int[3]) * (size_t)m->cap); m->v = malloc(sizeof(int) * (size_t)m->cap);
        for (int i = 0; i < m->cap; i++) m->v[i] = -1;
        for (int i = 0; i < o.cap; i++) if (o.v[i] >= 0) km_put(m, o.k[i][0], o.k[i][1], o.k[i][2], o.v[i]);
        free(o.k); free(o.v);
    }
    int k[3] = { a < b ? a : b, a < b ? b : a, c };
    uint64_t i = km_h(k) & (uint64_t)(m->cap - 1);
    while (m->v[i] >= 0) {
        if (m->k[i][0] == k[0] && m->k[i][1] == k[1] && m->k[i][2] == k[2]) return;   /* FUN_180005f80 / 180006110: kept */
        i = (i + 1) & (uint64_t)(m->cap - 1);
    }
    memcpy(m->k[i], k, sizeof k); m->v[i] = val; m->n++;
}

static void sh_err(Shell *S, const char *m) { if (!S->err[0]) snprintf(S->err, sizeof S->err, "%s", m); }
static int sv_new(Shell *S)
{
    if (S->nv == S->cv) { S->cv = S->cv ? S->cv * 2 : 1024; S->v = realloc(S->v, (size_t)S->cv * sizeof(SVert)); }
    SVert *v = &S->v[S->nv]; memset(v, 0, sizeof *v);
    v->ea = v->eb = -1; v->parent = -1; v->outid = -1;
    return S->nv++;
}
static int se_new(Shell *S)
{
    if (S->ne == S->ce) { S->ce = S->ce ? S->ce * 2 : 1024; S->e = realloc(S->e, (size_t)S->ce * sizeof(SEdge)); }
    SEdge *e = &S->e[S->ne]; memset(e, 0, sizeof *e);
    e->comp = -1;
    return S->ne++;
}
static int vfind(Shell *S, int v)   /* FUN_180039af0 (path compression) */
{
    int r = v;
    while (S->v[r].parent >= 0) r = S->v[r].parent;
    while (S->v[v].parent >= 0 && S->v[v].parent != r) { int n = S->v[v].parent; S->v[v].parent = r; v = n; }
    return r;
}

/* FUN_18003adb0: exact representation */
static void sv_exact(Shell *S, int vi)
{
    SVert *v = &S->v[vi];
    if (v->ex_ok) return;
    v->ex_ok = 1;
    if (v->ea < 0) {
        big_from_double(&v->X, v->c[0].v, S->g);
        big_from_double(&v->Y, v->c[1].v, S->g);
        return;
    }
    SEdge *A = &S->e[v->ea], *B = &S->e[v->eb];
    int a0 = A->a, a1 = A->b, b0 = B->a, b1 = B->b;
    sv_exact(S, a0); sv_exact(S, a1); sv_exact(S, b0); sv_exact(S, b1);
    v = &S->v[vi];
    Big dax, day, dbx, dby, t1, t2, den, nx, ny, num, X, Y;
    big_sub(&dax, &S->v[a1].X, &S->v[a0].X, 2); big_sub(&day, &S->v[a1].Y, &S->v[a0].Y, 2);
    big_sub(&dbx, &S->v[b1].X, &S->v[b0].X, 2); big_sub(&dby, &S->v[b1].Y, &S->v[b0].Y, 2);
    big_mul(&t1, &dax, 2, &dby, 2); big_mul(&t2, &day, 2, &dbx, 2);
    big_sub(&den, &t2, &t1, 4);
    int ds = big_sign(&den, 4);
    if (ds == 0) sh_err(S, "Shell2D error 2: bad intersection.");
    big_sub(&nx, &S->v[a0].X, &S->v[b0].X, 2); big_sub(&ny, &S->v[a0].Y, &S->v[b0].Y, 2);
    big_mul(&t1, &nx, 2, &dby, 2); big_mul(&t2, &ny, 2, &dbx, 2);
    big_sub(&num, &t1, &t2, 4);
    big_mul(&t1, &S->v[a0].X, 2, &den, 4); big_mul(&t2, &num, 4, &dax, 2); big_add(&X, &t1, &t2, 6);
    big_mul(&t1, &S->v[a0].Y, 2, &den, 4); big_mul(&t2, &num, 4, &day, 2); big_add(&Y, &t1, &t2, 6);
    if (ds < 0) { X.pos = !X.pos; Y.pos = !Y.pos; den.pos = !den.pos; }   /* FUN_18000e980 */
    v->X = X; v->Y = Y; v->D = den;
    Fn fx = f_abs(big_to_double(&X, 6, S->g3)), fy = f_abs(big_to_double(&Y, 6, S->g3));
    Fn fd = f_abs(big_to_double(&den, 4, S->g2));
    v->c[0] = f_div(fx, fd); v->c[1] = f_div(fy, fd);
}

/* line record FUN_18003c040: base and other end of the INPUT edge, oriented like edge e (FUN_18003be10) */
typedef struct { Fn dx, dy; int base, other; int ein; } Line;
static int sv_cmp(Shell *S, int a, int b);
static int same_dir_as_input(Shell *S, int ei)   /* FUN_18003be10 */
{
    SEdge *e = &S->e[ei];
    if (e->in == ei) return 1;
    SEdge *in = &S->e[e->in];
    SVert *ia = &S->v[in->a], *ib = &S->v[in->b];
    int sgn;
    if (ib->c[0].v < ia->c[0].v || ib->c[0].v == ia->c[0].v) {
        if (ia->c[0].v < ib->c[0].v || ia->c[0].v == ib->c[0].v) {
            if (ib->c[1].v < ia->c[1].v || ib->c[1].v == ia->c[1].v) {
                if (ia->c[1].v < ib->c[1].v || ia->c[1].v == ib->c[1].v) { sh_err(S, "Shell2D error 8: invalid edge."); sgn = 0; S->axis = 1; }
                else { sgn = 1; S->axis = 1; }
            } else { sgn = -1; S->axis = 1; }
        } else { sgn = 1; S->axis = 0; }
    } else { sgn = -1; S->axis = 0; }
    return sv_cmp(S, e->a, e->b) == sgn;
}
static void mkline(Shell *S, Line *L, int ei)
{
    SEdge *in = &S->e[S->e[ei].in];
    if (same_dir_as_input(S, ei)) { L->base = in->a; L->other = in->b; }
    else { L->base = in->b; L->other = in->a; }
    L->dx = f_dd(S->v[L->other].c[0].v, S->v[L->base].c[0].v);
    L->dy = f_dd(S->v[L->other].c[1].v, S->v[L->base].c[1].v);
    L->ein = S->e[ei].in;
}
/* FUN_180039eb0: filtered orientation of P against L; *certain; *val = the filtered value */
static int orient_f(Shell *S, const Line *L, int pi, int *certain, Fn *val)
{
    SVert *P = &S->v[pi], *B = &S->v[L->base];
    Fn ux, uy;
    if (P->ea < 0) { ux = f_dd(P->c[0].v, B->c[0].v); uy = f_dd(P->c[1].v, B->c[1].v); }
    else { ux = f_fd(P->c[0], B->c[0].v); uy = f_fd(P->c[1], B->c[1].v); }
    Fn a = f_mul(ux, L->dy), b = f_mul(uy, L->dx);
    *val = f_sub(a, b);
    if (f_maybe0(*val)) { *certain = val->e == 0.0; return 0; }
    *certain = 1;
    return val->v <= 0.0 ? -1 : 1;
}
/* FUN_18003b370: exact orientation */
static int orient_x(Shell *S, const Line *L, int pi)
{
    sv_exact(S, pi); sv_exact(S, L->base); sv_exact(S, L->other);
    SVert *P = &S->v[pi], *B = &S->v[L->base], *O = &S->v[L->other];
    Big dx, dy, ux, uy, t1, t2, r;
    big_sub(&dx, &O->X, &B->X, 2); big_sub(&dy, &O->Y, &B->Y, 2);   /* FUN_18003a0b0 */
    if (P->ea < 0) {
        big_sub(&ux, &P->X, &B->X, 2); big_sub(&uy, &P->Y, &B->Y, 2);
        big_mul(&t1, &ux, 2, &dy, 2); big_mul(&t2, &uy, 2, &dx, 2);
        big_sub(&r, &t1, &t2, 4);
        return big_sign(&r, 4);
    }
    Big bx, by;
    big_mul(&bx, &P->D, 4, &B->X, 2); big_mul(&by, &P->D, 4, &B->Y, 2);
    big_sub(&ux, &P->X, &bx, 6); big_sub(&uy, &P->Y, &by, 6);
    big_mul(&t1, &ux, 6, &dy, 2); big_mul(&t2, &uy, 6, &dx, 2);
    big_sub(&r, &t1, &t2, 8);
    return big_sign(&r, 8);
}
static int orient(Shell *S, const Line *L, int pi)   /* FUN_18003b610 */
{
    int c; Fn v;
    int s = orient_f(S, L, pi, &c, &v);
    if (!c) s = orient_x(S, L, pi);
    return s;
}
/* FUN_18003b670: exact compare of coordinate ax */
static int sv_cmp_exact(Shell *S, int a, int b, int ax)
{
    int r;
    if (S->v[a].ea < 0 || S->v[b].ea < 0) {
        int in = a, ot = b;
        if (S->v[a].ea >= 0) { in = b; ot = a; }
        sv_exact(S, ot); sv_exact(S, in);
        Big t; big_mul(&t, &S->v[ot].D, 4, ax ? &S->v[in].Y : &S->v[in].X, 2);
        r = big_cmp(&t, ax ? &S->v[ot].Y : &S->v[ot].X, 6);
        if (a != in) r = -r;
    } else {
        sv_exact(S, a); sv_exact(S, b);
        Big t1, t2;
        big_mul(&t1, &S->v[b].D, 4, ax ? &S->v[a].Y : &S->v[a].X, 6);
        big_mul(&t2, &S->v[a].D, 4, ax ? &S->v[b].Y : &S->v[b].X, 6);
        r = big_cmp(&t1, &t2, 10);
    }
    return r == 0 ? 0 : (r < 0 ? -1 : 1);
}
static int sv_cmp(Shell *S, int a, int b)   /* FUN_18003b890 on axis S->axis */
{
    Fn *pa = &S->v[a].c[S->axis], *pb = &S->v[b].c[S->axis];
    Fn d = f_sub(*pa, *pb);
    if (!f_maybe0(d)) return (pb->v < pa->v || pb->v == pa->v) ? 1 : -1;
    if (pa->e == 0.0 && pb->e == 0.0) return 0;
    return sv_cmp_exact(S, a, b, S->axis);
}
static int input_axis(Shell *S, int ei)   /* FUN_18003a1d0 */
{
    SEdge *in = &S->e[S->e[ei].in];
    if (S->v[in->a].c[0].v == S->v[in->b].c[0].v) {
        if (S->v[in->a].c[1].v == S->v[in->b].c[1].v) { sh_err(S, "Shell2D error 5: invalid edge."); return -1; }
        return 1;
    }
    return 0;
}

/* FUN_18003a440: merge vertex q into p */
static void sv_union(Shell *S, int p, int q)
{
    if (S->v[p].ea >= 0 && S->v[q].ea < 0) { int t = p; p = q; q = t; }   /* an input vertex stays the root */
    if (p == q) return;
    S->v[q].parent = p;
    S->stamp += 2;
    for (int i = 0; i < S->v[p].inc.n; i++) S->e[S->v[p].inc.v[i]].stamp = S->stamp;
    while (S->v[q].inc.n > 0) {
        int e = S->v[q].inc.v[S->v[q].inc.n - 1]; S->v[q].inc.n--;
        if (S->e[e].stamp != S->stamp) il_push(&S->v[p].inc, e);
    }
}
static int add_inc(Shell *S, int v, int e)   /* FUN_18003a3c0 */
{
    if (il_find(&S->v[v].inc, e) >= 0) return 0;
    il_push(&S->v[v].inc, e);
    return 1;
}
static void rm_inc(Shell *S, int v, int e)   /* FUN_18003a300 */
{
    int i = il_find(&S->v[v].inc, e);
    if (i < 0) { sh_err(S, "Shell2D error 9: corrupt edge record."); return; }
    il_remove_at(&S->v[v].inc, i);
}
static int sub_edge(Shell *S, int a, int b, int in)   /* FUN_18003a9e0 + FUN_18003a270 */
{
    /* +0x68: a sub-edge committed earlier with the same ends and input edge is reused: pushed again on its stored
     * ends' lists (not their roots) and on +0x38 */
    int e = km_find(S->m78, a, b, in);
    if (e >= 0) {
        il_push(&S->v[S->e[e].a].inc, e); il_push(&S->v[S->e[e].b].inc, e);
        il_push(&S->sub38, e);
        return e;
    }
    e = se_new(S);
    S->e[e].a = a; S->e[e].b = b; S->e[e].in = in;
    il_push(&S->v[a].inc, e); il_push(&S->v[b].inc, e);
    il_push(&S->sub38, e);
    return e;
}
/* FUN_18003a050: E onto +0x80 once (record +8) */
static void mark80(Shell *S, int E)
{
    if (S->e[E].in80) return;
    S->e[E].in80 = 1;
    il_push(&S->split80, E);
}

/* FUN_18003ba10: collinear end check */
static void colin_end(Shell *S, int p, int ei)
{
    int c1 = sv_cmp(S, p, S->e[ei].a);
    if (c1 == 0) { sv_union(S, vfind(S, p), vfind(S, S->e[ei].a)); return; }
    int c2 = sv_cmp(S, p, S->e[ei].b);
    if (c2 == 0) { sv_union(S, vfind(S, p), vfind(S, S->e[ei].b)); return; }
    if (c1 != c2) { int r = vfind(S, p); if (add_inc(S, r, ei)) il_push(&S->e[ei].hits, r); }
}
/* FUN_18003bb30 */
static int classify(Shell *S, int s1, int s2, int A, int *touch, int B)
{
    if (s1 == 0) {
        if (s2 == 0) {
            S->axis = input_axis(S, A);
            colin_end(S, S->e[A].a, B); colin_end(S, S->e[A].b, B);
            colin_end(S, S->e[B].a, A); colin_end(S, S->e[B].b, A);
            return 1;
        }
        *touch = S->e[A].a;
    } else if (s2 == 0) *touch = S->e[A].b;
    else return s1 == s2;
    int t = vfind(S, *touch);
    return t == vfind(S, S->e[B].a) || t == vfind(S, S->e[B].b);
}
/* FUN_18003c5e0: test edge E (line LE) against F */
static void pair_test(Shell *S, int E, const Line *LE, int F)
{
    int unc = 0, tF = -1, tE = -1, c1, c2;
    Fn v1, v2, vE;
    int s1 = orient_f(S, LE, S->e[F].a, &c1, &v1);
    if (!c1) { int r = vfind(S, S->e[F].a); if (r == vfind(S, S->e[E].a) || r == vfind(S, S->e[E].b)) c1 = 1; }
    int s2 = orient_f(S, LE, S->e[F].b, &c2, &v2);
    if (!c2) { int r = vfind(S, S->e[F].b); if (r == vfind(S, S->e[E].a) || r == vfind(S, S->e[E].b)) c2 = 1; }
    if (!c1 || !c2) unc = 1;
    else if (classify(S, s1, s2, F, &tF, E)) return;
    Line LF; mkline(S, &LF, F);
    s1 = orient_f(S, &LF, S->e[E].a, &c1, &vE);
    s2 = orient_f(S, &LF, S->e[E].b, &c2, &v2);
    if (!c1 || !c2) unc = 1;
    else if (classify(S, s1, s2, E, &tE, F)) return;
    if (unc) {
        s1 = orient_x(S, LE, S->e[F].a); s2 = orient_x(S, LE, S->e[F].b);
        if (classify(S, s1, s2, F, &tF, E)) return;
        s1 = orient_x(S, &LF, S->e[E].a); s2 = orient_x(S, &LF, S->e[E].b);
        if (classify(S, s1, s2, E, &tE, F)) return;
    }
    mark80(S, F);   /* +0x68: FUN_18003a050(F) */
    if (tE < 0) {
        if (tF < 0) {
            /* +0x68: the crossing of these two input edges committed earlier (map +0x70) is used again */
            int nv = km_find(S->m70, S->e[E].in, S->e[F].in, 0);
            if (nv < 0) {
                nv = sv_new(S);
                SVert *x = &S->v[nv];
                x->ea = S->e[E].in; x->eb = S->e[F].in;
                Fn a = f_mul(LE->dx, LF.dy), b = f_mul(LE->dy, LF.dx);
                Fn den = f_sub(b, a);
                if (f_maybe0(den)) sv_exact(S, nv);
                else {
                    Fn t = f_div(vE, den);
                    SVert *ea = &S->v[S->e[E].a];
                    S->v[nv].c[0] = f_add(ea->c[0], f_mul(t, LE->dx));
                    S->v[nv].c[1] = f_add(ea->c[1], f_mul(t, LE->dy));
                }
            }
            il_push(&S->x20, nv);
            il_push(&S->e[E].hits, nv); il_push(&S->e[F].hits, nv);
            il_push(&S->v[nv].inc, E); il_push(&S->v[nv].inc, F);
        } else {
            int r = vfind(S, tF);
            if (add_inc(S, r, E)) il_push(&S->e[E].hits, r);
        }
    } else if (tF < 0) {
        int r = vfind(S, tE);
        if (add_inc(S, r, F)) il_push(&S->e[F].hits, r);
    } else sv_union(S, vfind(S, tE), vfind(S, tF));
}


/* FUN_18003cd90: resolve edge E (all crossings with its candidates, then split) */
static int g_trace_res;   /* IFK_TRACE_RES (debugging) */
static void resolve(Shell *S, int E)
{
    if (g_trace_res)   /* IFK_TRACE_RES: the call order, as tools/peloader --tpif logs the DLL's (TPIF_TRACE_RESOLVE) */
        fprintf(stderr, "RES %.17g %.17g %.17g %.17g %d\n", S->v[S->e[E].a].c[0].v, S->v[S->e[E].a].c[1].v,
                S->v[S->e[E].b].c[0].v, S->v[S->e[E].b].c[1].v, S->e[E].groups.n > 0);
    if (S->e[E].groups.n == 0) return;   /* no group left: nothing to do (the whole body is under this test) */
    /* +0x68: the groups are copied (pushed front) to the record +0x38, E goes on +0x80 (FUN_18003a050) */
    for (int gi = S->e[E].groups.n - 1; gi >= 0; gi--) il_push(&S->e[E].saved, S->e[E].groups.v[gi]);
    mark80(S, E);
    Line LE; mkline(S, &LE, E);
    S->stamp += 2; S->e[E].stamp = S->stamp;
    /* pop E's groups from the front; each group's edges from its head; a partner is tested once (stamp +0x2c) and only
     * while it still has groups itself */
    while (S->e[E].groups.n > 0) {
        int G = S->e[E].groups.v[--S->e[E].groups.n];
        for (int k = 0; k < S->grp[G].n; k++) {
            int F = S->grp[G].v[k];
            if (S->e[F].stamp == S->stamp || S->e[F].groups.n == 0) continue;
            S->e[F].stamp = S->stamp;
            pair_test(S, E, &LE, F);
        }
    }
    if (S->e[E].hits.n == 0) return;
    int a = vfind(S, S->e[E].a), b = vfind(S, S->e[E].b), in = S->e[E].in;
    rm_inc(S, a, E); rm_inc(S, b, E);
    if (S->e[E].hits.n == 1) {
        int h = vfind(S, S->e[E].hits.v[0]);
        rm_inc(S, h, E);
        S->e[E].hits.n = 0;
        sub_edge(S, a, h, in); sub_edge(S, h, b, in);
        return;
    }
    S->axis = input_axis(S, E);
    S->stamp += 2;
    int *set = malloc((size_t)(S->e[E].hits.n + 2) * sizeof(int)); int ns = 0;
    while (S->e[E].hits.n > 0) {
        int h = vfind(S, S->e[E].hits.v[--S->e[E].hits.n]);
        if (S->v[h].stamp2 == S->stamp) continue;
        S->v[h].stamp2 = S->stamp;
        rm_inc(S, h, E);
        int j, dup = -1;
        for (j = 0; j < ns; j++) { int c = sv_cmp(S, h, set[j]); if (c == 0) { dup = j; break; } if (c < 0) break; }
        if (dup >= 0) { sv_union(S, vfind(S, set[dup]), h); continue; }
        memmove(set + j + 1, set + j, (size_t)(ns - j) * sizeof(int)); set[j] = h; ns++;
    }
    for (int q = 0; q < 2; q++) {   /* FUN_180008400: insert the ends */
        int v = q ? b : a, j;
        for (j = 0; j < ns; j++) if (sv_cmp(S, v, set[j]) < 0) break;
        memmove(set + j + 1, set + j, (size_t)(ns - j) * sizeof(int)); set[j] = v; ns++;
    }
    int fwd = set[0] == a;
    for (int j = 1; j < ns; j++) {
        if (fwd) sub_edge(S, vfind(S, set[j - 1]), vfind(S, set[j]), in);
        else sub_edge(S, vfind(S, set[j]), vfind(S, set[j - 1]), in);
    }
    free(set);
}

/* FUN_18003a890 (+0x68): commit the chosen edge E: each end whose root is a crossing not committed yet gets its
 * record (+0x30, [3] = E's input edge) and goes into map +0x70 under its input-edge pair; a sub-edge goes into map
 * +0x78 under (its stored ends, its input edge) */
static void commit(Shell *S, int E)
{
    for (int k = 0; k < 2; k++) {
        int r = vfind(S, k ? S->e[E].b : S->e[E].a);
        if (S->v[r].ea >= 0 && !S->v[r].committed) {
            S->v[r].committed = 1; S->v[r].rec3 = S->e[E].in;
            if (!S->m70) S->m70 = calloc(1, sizeof(KMap));
            km_put(S->m70, S->v[r].ea, S->v[r].eb, 0, r);
        }
    }
    if (S->e[E].in != E) {
        if (!S->m78) S->m78 = calloc(1, sizeof(KMap));
        km_put(S->m78, S->e[E].a, S->e[E].b, S->e[E].in, E);
    }
}
/* FUN_18003a5e0 (+0x68): undo the step except what was committed */
static void rollback(Shell *S)
{
    while (S->sub38.n > 0) {   /* the sub-edges of this step leave their ends' lists; unvisited ones are freed */
        int se = S->sub38.v[--S->sub38.n];
        rm_inc(S, vfind(S, S->e[se].a), se); rm_inc(S, vfind(S, S->e[se].b), se);
        if (!S->e[se].visited) S->e[se].dead = 1;
    }
    for (int i = S->split80.n - 1; i >= 0; i--) {   /* the hits still listed on the +0x80 edges are dropped */
        int E = S->split80.v[i];
        S->stamp += 2;   /* DAT_18005eb08 */
        while (S->e[E].hits.n > 0) {
            int h = vfind(S, S->e[E].hits.v[--S->e[E].hits.n]);
            if (S->v[h].stamp3 != S->stamp) { S->v[h].stamp3 = S->stamp; rm_inc(S, h, E); }
        }
    }
    S->x20.n = 0;   /* the crossings of this step not committed are freed (records are not reused in the port) */
    while (S->split80.n > 0) {   /* split edges get their groups back and rejoin their ends' lists */
        int E = S->split80.v[--S->split80.n];
        S->e[E].in80 = 0;
        if (S->e[E].groups.n == 0) {
            while (S->e[E].saved.n > 0) il_push(&S->e[E].groups, S->e[E].saved.v[--S->e[E].saved.n]);   /* FUN_180004ee0 */
            il_push(&S->v[vfind(S, S->e[E].a)].inc, E); il_push(&S->v[vfind(S, S->e[E].b)].inc, E);
        }
    }
}

/* FUN_18003c120 */
static int on_edge(Shell *S, int ei, int p)
{
    if (p == S->e[ei].a || p == S->e[ei].b) return 1;
    Line L; mkline(S, &L, ei);
    if (orient(S, &L, p) != 0) return 0;
    S->axis = input_axis(S, ei);
    int c1 = sv_cmp(S, p, S->e[ei].a);
    if (c1 == 0) return 1;
    int c2 = sv_cmp(S, p, S->e[ei].b);
    return c2 == 0 || c1 != c2;
}
static void e_reverse(Shell *S, int ei) { int t = S->e[ei].a; S->e[ei].a = S->e[ei].b; S->e[ei].b = t; S->e[ei].rev = !S->e[ei].rev; }

typedef struct { int e, v; } Item;

/* FUN_18003d4c0: seed of component c; pushes two items */
static int seed(Shell *S, int c, Item *items, int *nitems)
{
    int sv = S->comp_ext[c];
    S->stamp += 2;
    int *cand = NULL; int nc = 0;
    if (S->v[sv].inc.n > 0) {
        int e0 = S->v[sv].inc.v[S->v[sv].inc.n - 1];
        for (int gi = S->e[e0].groups.n - 1; gi >= 0; gi--) {   /* the edges of e0's groups (+0x20 from its head) */
            IList *G = &S->grp[S->e[e0].groups.v[gi]];
            for (int k = 0; k < G->n; k++) {
                int F = G->v[k];
                if (S->e[F].stamp == S->stamp) continue;
                if (on_edge(S, F, sv)) { cand = realloc(cand, (size_t)(nc + 1) * sizeof(int)); cand[nc++] = F; }
                S->e[F].stamp = S->stamp;
            }
        }
    }
    int sd = -1, f8 = 0;
    for (int i = nc - 1; i >= 0; i--) {   /* list built push-front */
        int e = cand[i]; Line L; mkline(S, &L, e); f8 = 0;
        for (int j = nc - 1; j >= 0; j--) {
            if (cand[j] == e) continue;
            for (int k = 0; k < 2; k++) {
                int s = orient(S, &L, k ? S->e[cand[j]].b : S->e[cand[j]].a);
                if (s > 0) f8 |= 1; else if (s < 0) f8 |= 2;
            }
            if (f8 == 3) break;
        }
        if (f8 != 3) { sd = e; break; }
    }
    free(cand);
    if (sd < 0) { sh_err(S, "Shell2D error 6: unable to find seed edge."); return 0; }
    if (f8 == 0) {
        SEdge *in = &S->e[S->e[sd].in];
        if (S->v[in->a].c[0].v == S->v[in->b].c[0].v) { S->axis = 1; f8 = sv_cmp(S, S->e[sd].a, S->e[sd].b) == 1 ? 1 : 2; }
        else f8 = 2;
    }
    if (f8 == 1) e_reverse(S, sd);
    int n38 = S->sub38.n;
    resolve(S, sd);
    int start = vfind(S, sv), ue = -1;
    if (S->sub38.n == n38) { if (vfind(S, S->e[sd].a) == start || vfind(S, S->e[sd].b) == start) ue = sd; }
    else for (int k = S->sub38.n - 1; k >= n38; k--) {   /* +0x38 from its head down to the old head */
        int se = S->sub38.v[k];
        if (vfind(S, S->e[se].a) == start || vfind(S, S->e[se].b) == start) { ue = se; break; }
    }
    if (ue < 0) { sh_err(S, "Shell2D error 6: unable to find seed edge."); return 0; }
    items[(*nitems)++] = (Item){ ue, start };
    int other = vfind(S, S->e[ue].a) == start ? vfind(S, S->e[ue].b) : vfind(S, S->e[ue].a);
    items[(*nitems)++] = (Item){ ue, other };
    S->e[ue].visited = 1;
    il_push(&S->out, ue);
    commit(S, ue); rollback(S);   /* +0x68 */
    return 1;
}

/* the incumbent record of the turn choice (local_158 ...) */
typedef struct { int best, cur, bestfar, curfar, prev; Line LE; int cls, back, dc; } Pick;
static int b980(Shell *S, int prev, int far, int v, int E)   /* FUN_18003b980 */
{
    S->axis = input_axis(S, E);
    return sv_cmp(S, prev, v) == sv_cmp(S, far, v);
}
static int c230(Shell *S, Pick *p, int v)   /* FUN_18003c230 */
{
    Line L; mkline(S, &L, p->best);
    int s = S->e[p->cur].in == S->e[p->best].in ? 0 : orient(S, &L, p->curfar);
    int end = p->dc ? S->e[p->best].b : S->e[p->best].a;
    if (vfind(S, end) == v) s = -s;
    return s;
}
static void replace(Pick *p, int cls) { p->cls = cls; p->best = p->cur; p->bestfar = p->curfar; }
static void c2f0(Shell *S, int E, Pick *p, int v)   /* FUN_18003c2f0 */
{
    int c = S->e[p->cur].in == S->e[E].in ? 0 : orient(S, &p->LE, p->curfar);
    char bvis = S->e[p->best].visited;
    if (c == 0) {
        if (b980(S, p->prev, p->curfar, v, E)) {
            if (p->cls == 0 && p->back && !bvis) { replace(p, 0); p->back = 1; }
        } else if (p->cls < 0 || (p->cls == 0 && (p->back || !bvis))) { replace(p, 0); p->back = 0; }
    } else if (c < 1) {
        if (p->cls < 0) { int t = c230(S, p, v); if (0 < t || (!bvis && t == 0)) replace(p, -1); }
        else if (p->cls == 0 && p->back) replace(p, -1);
    } else if (p->cls < 1) replace(p, 1);
    else { int t = c230(S, p, v); if (0 < t || (!bvis && t == 0)) replace(p, 1); }
}
static int excluded(Shell *S, int dc, int e, int v)   /* FUN_180039b50 */
{
    if (!S->e[e].visited) return 0;
    return vfind(S, dc ? S->e[e].b : S->e[e].a) == v;
}

/* FUN_18003e860 */
static int walk(Shell *S)
{
    S->ok = 1;
    Item *st = malloc(sizeof(Item) * 16); int sn = 0, sc = 16;
    /* components: tree keyed by FUN_18003bfa0 (extreme vertices on x; equal goes right), iterated from the
     * rightmost node to its predecessors (FUN_180005e30 / FUN_180005e80): descending, later insertion first */
    int *asc = malloc(sizeof(int) * (size_t)(S->ncomp + 1)), *co = malloc(sizeof(int) * (size_t)(S->ncomp + 1));
    for (int i = 0; i < S->ncomp; i++) {
        int j = 0;
        S->axis = 0;
        while (j < i && sv_cmp(S, S->comp_ext[i], S->comp_ext[asc[j]]) != -1) j++;   /* left only when -1 */
        memmove(asc + j + 1, asc + j, sizeof(int) * (size_t)(i - j)); asc[j] = i;
    }
    for (int i = 0; i < S->ncomp; i++) co[i] = asc[S->ncomp - 1 - i];
    free(asc);
    for (int ci = 0; ci < S->ncomp; ci++) {
        int c = co[ci];
        if (S->comp_done[c]) continue;
        sn = 0;
        if (sc < 2) { sc = 16; st = realloc(st, sizeof(Item) * (size_t)sc); }
        if (!seed(S, c, st, &sn)) { S->ok = 0; break; }
        /* queue: append / pop front */
        int head = 0;
        while (head < sn) {
            Item it = st[head++];
            int E = it.e;
            S->comp_done[S->e[S->e[E].in].comp] = 1;
            {   /* +0x68: the item vertex's committed edge (record [3]) or else the head of its list */
                SVert *iv = &S->v[it.v];
                int e0 = iv->committed ? iv->rec3 : (iv->inc.n > 0 ? iv->inc.v[iv->inc.n - 1] : -1);
                if (e0 >= 0) resolve(S, e0);
            }
            for (int k = 0; k < 2; k++) {
                int v = vfind(S, it.v);
                int n = S->v[v].inc.n;
                /* the incident list (newest first) is pushed to the front of local_c8 and popped from its front:
                 * oldest first */
                int *tmp = malloc(sizeof(int) * (size_t)(n + 1));
                for (int i = 0; i < n; i++) tmp[i] = S->v[v].inc.v[i];
                for (int i = 0; i < n; i++) resolve(S, tmp[i]);
                free(tmp);
            }
            Pick p; memset(&p, 0, sizeof p); p.best = -1;
            mkline(S, &p.LE, E);
            int v = vfind(S, it.v);
            if (vfind(S, S->e[E].a) == v) { p.dc = 0; p.prev = vfind(S, S->e[E].b); }
            else { p.dc = 1; p.prev = vfind(S, S->e[E].a); }
            IList *inc = &S->v[v].inc;
            int i = inc->n - 1;
            for (; i >= 0; i--) {
                int e = inc->v[i];
                if (e == E || excluded(S, p.dc, e, v)) continue;
                p.cur = p.best = e;
                p.curfar = p.bestfar = vfind(S, S->e[e].a) == v ? vfind(S, S->e[e].b) : vfind(S, S->e[e].a);
                p.cls = S->e[E].in == S->e[e].in ? 0 : orient(S, &p.LE, p.bestfar);
                if (p.cls == 0) p.back = b980(S, p.prev, p.bestfar, v, E);
                i--;
                break;
            }
            for (; i >= 0; i--) {
                int e = inc->v[i];
                if (e == E || excluded(S, p.dc, e, v)) continue;
                p.cur = e;
                p.curfar = vfind(S, S->e[e].a) == v ? vfind(S, S->e[e].b) : vfind(S, S->e[e].a);
                c2f0(S, E, &p, v);
            }
            if (p.best < 0) S->ok = 0;
            else if (!S->e[p.best].visited) {
                S->e[p.best].visited = 1;
                if (sn == sc) { sc *= 2; st = realloc(st, sizeof(Item) * (size_t)sc); }
                st[sn++] = (Item){ p.best, p.bestfar };
                il_push(&S->out, p.best);
                commit(S, p.best);   /* +0x68: FUN_18003a890 */
                if (vfind(S, p.dc ? S->e[p.best].b : S->e[p.best].a) == v) e_reverse(S, p.best);
            }
            rollback(S);   /* +0x68: FUN_18003a5e0 after each step */
        }
    }
    free(st); free(co);
    /* end of FUN_18003e860 (ifspinsil_all.c l.48504-48509, the branch taken when no component is left; the only
     * return): every edge of the output list +0x40 gets its two ends replaced by their union-find roots
     * (FUN_180039af0), so Run's output loop writes one node per merged point (oracle: the cone apex is one node) */
    for (int i = 0; i < S->out.n; i++) {
        SEdge *e = &S->e[S->out.v[i]];
        e->a = vfind(S, e->a); e->b = vfind(S, e->b);
    }
    return S->ok;
}

/* ================================================================== Stitch phases 5-6 + emission */
typedef struct { int a, b, cnt; IList tj; } TE;
static int cmp_te(const void *x, const void *y)
{
    const TE *p = x, *q = y;
    return p->a != q->a ? (p->a < q->a ? -1 : 1) : (p->b != q->b ? (p->b < q->b ? -1 : 1) : 0);
}
typedef struct { SNode *sn; TE *te; int ne; int *enode; ESet *E; } Emit;
static TE *te_find(Emit *m, int a, int b)
{
    TE key; memset(&key, 0, sizeof key); key.a = a < b ? a : b; key.b = a < b ? b : a;
    return bsearch(&key, m->te, (size_t)m->ne, sizeof *m->te, cmp_te);
}
static void emit_tri(Emit *m, int a, int b, int c)   /* FUN_18000b010: nodes on first use, then the triangle */
{
    int cc[3] = { a, b, c };
    static int tr = -1; if (tr < 0) tr = getenv("IFK_TRACE_ST") != NULL;   /* debugging: Stitch's output to the EdgeSet */
    for (int z = 0; z < 3; z++) if (m->enode[cc[z]] < 0) {
        m->enode[cc[z]] = e_node(m->E, m->sn[cc[z]].p);
        if (tr) fprintf(stderr, "ST N %d %.17g %.17g %.17g\n", m->enode[cc[z]], m->sn[cc[z]].p[0], m->sn[cc[z]].p[1], m->sn[cc[z]].p[2]);
    }
    if (tr) fprintf(stderr, "ST T %d %d %d\n", m->enode[a], m->enode[b], m->enode[c]);
    e_triangle(m->E, m->enode[a], m->enode[b], m->enode[c]);
}
/* FUN_18000aa60: split triangle (c0, c1, c2) along its first edge (0, 1, 2) that still has pending T-junction nodes
 * (the edge flag +0x28 is set for the triangle's three original edges by FUN_18000b1d0 and cleared when used);
 * pending[k] = edge k is an original edge of the triangle. Middle fan triangles are emitted; *first / *last get
 * the end pieces (with their own pending edge). Returns 1 if split. */
typedef struct { int c[3]; int pend[3]; } Piece;
/* FUN_18000aea0 makes each fan piece and frees it again (returns NULL) when two of its corners are the same node: such a
 * piece is neither emitted nor split further. *hf / *hl = the first / last piece exists. */
static int piece_ok(int a, int b, int c) { return a != b && b != c && c != a; }
static int split_piece(Emit *m, Piece *t, Piece *first, Piece *last, int *hf, int *hl)
{
    for (int k = 0; k < 3; k++) {
        if (!t->pend[k]) continue;
        t->pend[k] = 0;
        int a = t->c[k], b = t->c[(k + 1) % 3], o = t->c[(k + 2) % 3];
        TE *f = te_find(m, a, b);
        if (!f || f->tj.n == 0) continue;
        /* sort the edge's nodes by (P - a) . (b - a) ascending (BST, FUN_180014c30 / FUN_18000f7b0) */
        double d[3]; v_sub(d, m->sn[b].p, m->sn[a].p);
        int nl = f->tj.n; int *L = malloc(sizeof(int) * (size_t)nl); double *K = malloc(sizeof(double) * (size_t)nl);
        for (int u = 0; u < nl; u++) {
            double w[3]; L[u] = f->tj.v[nl - 1 - u]; v_sub(w, m->sn[L[u]].p, m->sn[a].p);
            K[u] = w[1] * d[1] + w[0] * d[0] + w[2] * d[2];
        }
        for (int x = 1; x < nl; x++) for (int y = x; y > 0 && K[y] < K[y - 1]; y--) {
            double tk = K[y]; K[y] = K[y - 1]; K[y - 1] = tk; int ti = L[y]; L[y] = L[y - 1]; L[y - 1] = ti;
        }
        /* first piece (a, n1, o): its pending edge is (o, a) = original edge k+2 */
        first->c[0] = a; first->c[1] = L[0]; first->c[2] = o;
        first->pend[0] = 0; first->pend[1] = 0; first->pend[2] = t->pend[(k + 2) % 3];
        *hf = piece_ok(a, L[0], o);
        for (int x = 1; x < nl; x++) if (piece_ok(L[x - 1], L[x], o)) emit_tri(m, L[x - 1], L[x], o);
        /* last piece (nk, b, o): its pending edge is (b, o) = original edge k+1 */
        last->c[0] = L[nl - 1]; last->c[1] = b; last->c[2] = o;
        last->pend[0] = 0; last->pend[1] = t->pend[(k + 1) % 3]; last->pend[2] = 0;
        *hl = piece_ok(L[nl - 1], b, o);
        free(L); free(K);
        return 1;
    }
    return 0;
}
static void emit_with_tj(Emit *m, int a, int b, int c)   /* FUN_18000b1d0 */
{
    Piece t = { { a, b, c }, { 1, 1, 1 } }, f, l;
    int hf = 0, hl = 0;
    if (!split_piece(m, &t, &f, &l, &hf, &hl)) { emit_tri(m, a, b, c); return; }
    Piece *ends[2] = { &f, &l }; int has[2] = { hf, hl };
    for (int z = 0; z < 2; z++) {
        if (!has[z]) continue;   /* NULL piece */
        Piece f2, l2; int h2f = 0, h2l = 0;
        if (split_piece(m, ends[z], &f2, &l2, &h2f, &h2l)) {
            if (h2f) emit_tri(m, f2.c[0], f2.c[1], f2.c[2]);
            if (h2l) emit_tri(m, l2.c[0], l2.c[1], l2.c[2]);
        } else emit_tri(m, ends[z]->c[0], ends[z]->c[1], ends[z]->c[2]);
    }
}

/* ================================================================== Shell2D::EdgeQuadtree (FUN_18003e370)
 * The candidate groups of the crossing search, built at the start of the walk FUN_18003e860 and then thrown away; only
 * the groups survive (edge +0x20, Shell2D +0x48). Ported as decompiled because the order of the groups and of the edges
 * in them decides which edge of a pair is "E" in FUN_18003c5e0 (the crossing vertex is computed from E's line, the
 * union keeps E's end as the root), the seed candidate order (FUN_18003d4c0) and so the output order. */
typedef struct { double c[2]; } QPt;                          /* QuadtreeEdgePoint (+0 x, +8 y) */
typedef struct { int p3, p4, edge, next; char fresh; } QEnt;  /* QTreeEdge: points [3] / [4], +8 the Shell2D edge,
                                                                 +0x10 "never clipped"; next = the list link */
typedef struct { int ch[4], head, count; short f20, i22; char done; double x, y, size; } QNode;
typedef struct { QPt *pt; int npt, cpt; QEnt *en; int nen, cen; QNode *nd; int nnd, cnd; double min, tol; } QTree;
static int qt_pt(QTree *q, double x, double y)
{
    if (q->npt == q->cpt) { q->cpt = q->cpt ? 2 * q->cpt : 1024; q->pt = realloc(q->pt, sizeof(QPt) * (size_t)q->cpt); }
    q->pt[q->npt].c[0] = x; q->pt[q->npt].c[1] = y; return q->npt++;
}
static int qt_ent(QTree *q, int a, int b)   /* FUN_180009000 on a fresh QTreeEdge */
{
    if (q->nen == q->cen) { q->cen = q->cen ? 2 * q->cen : 1024; q->en = realloc(q->en, sizeof(QEnt) * (size_t)q->cen); }
    QEnt *e = &q->en[q->nen]; e->p3 = a; e->p4 = b; e->edge = -1; e->next = -1; e->fresh = 0; return q->nen++;
}
static int qt_node(QTree *q)   /* FUN_18000d070 + FUN_18000dab0: count 0, +0x20 0, +0x48 0, +0x22 -1, empty list */
{
    if (q->nnd == q->cnd) { q->cnd = q->cnd ? 2 * q->cnd : 256; q->nd = realloc(q->nd, sizeof(QNode) * (size_t)q->cnd); }
    QNode *n = &q->nd[q->nnd]; memset(n, 0, sizeof *n); n->head = -1; n->i22 = -1; n->ch[0] = n->ch[1] = n->ch[2] = n->ch[3] = -1;
    return q->nnd++;
}
static int qt_near(double a, double b, double tol) { return (a <= b ? b - a : a - b) <= tol; }   /* FUN_180004320 */
static int qt_cut(QTree *q, int e, int ax, double v)   /* FUN_180009060 */
{
    const double *p3 = q->pt[q->en[e].p3].c, *p4 = q->pt[q->en[e].p4].c;
    double d[2] = { p4[0] - p3[0], p4[1] - p3[1] };
    int o = (ax + 1) & 1;
    double t = (v - p3[ax]) / d[ax];
    double c[2]; c[ax] = v; c[o] = p3[o] + t * d[o];
    return qt_pt(q, c[0], c[1]);
}
/* FUN_1800051a0 (QTreeEdge vslot 0): side of the split line ax = v; 0 = straddles, *clone = the piece for the high side */
static int qt_classify(QTree *q, int e, int ax, double v, int *clone)
{
    double a = q->pt[q->en[e].p3].c[ax], b = q->pt[q->en[e].p4].c[ax], tol = q->tol;
    if (qt_near(a, v, tol)) {
        if (qt_near(b, v, tol)) *clone = qt_ent(q, q->en[e].p3, q->en[e].p4);
        else if (b <= v) *clone = qt_ent(q, q->en[e].p3, q->en[e].p3);
        else { *clone = qt_ent(q, q->en[e].p3, q->en[e].p4); q->en[e].p4 = q->en[e].p3; }
    } else if (a <= v) {
        if (qt_near(b, v, tol)) *clone = qt_ent(q, q->en[e].p4, q->en[e].p4);
        else if (b <= v) return -1;
        else { int m = qt_cut(q, e, ax, v); *clone = qt_ent(q, m, q->en[e].p4); q->en[e].p4 = m; }
    } else {
        if (qt_near(b, v, tol)) { *clone = qt_ent(q, q->en[e].p3, q->en[e].p4); q->en[e].p3 = q->en[e].p4; }
        else if (v < b) return 1;
        else { int m = qt_cut(q, e, ax, v); *clone = qt_ent(q, m, q->en[e].p3); q->en[e].p3 = m; }
    }
    return 0;
}
/* FUN_18000d870: distribute src's entries along axis ax at v; > 0 move to dst (push-front), 0 clone to dst */
static void qt_distribute(QTree *q, int ax, int src, int dst, double v, short flag)
{
    q->nd[dst].f20 = q->nd[src].f20;
    int prev = -1, cur = q->nd[src].head;
    while (cur >= 0) {
        int nxt = q->en[cur].next, cl = -1;
        int r = qt_classify(q, cur, ax, v, &cl);
        if (r < 1) {
            if (r < 0) { if (q->en[cur].fresh) q->nd[src].f20 |= flag; }
            else {
                q->en[cur].fresh = 0; q->en[cl].fresh = 0;
                q->en[cl].next = q->nd[dst].head; q->nd[dst].head = cl; q->nd[dst].count++;
                q->en[cl].edge = q->en[cur].edge;
            }
            prev = cur;
        } else {
            if (cur == q->nd[src].head) q->nd[src].head = q->en[cur].next; else q->en[prev].next = q->en[cur].next;
            q->nd[src].count--;
            q->en[cur].next = q->nd[dst].head; q->nd[dst].head = cur; q->nd[dst].count++;
            if (q->en[cur].fresh) q->nd[dst].f20 |= flag;
        }
        cur = nxt;
    }
}
/* FUN_180009180: mark done; split when the cell is larger than the smallest cell (slot 2 FUN_180005bf0) */
static void qt_split(QTree *q, int n)
{
    q->nd[n].done = 1;
    if (!(q->min <= q->nd[n].size && q->nd[n].size != q->min)) return;
    q->nd[n].i22 = 0;
    double h = q->nd[n].size / 2.0;
    for (int k = 0; k < 4; k++) { int c = qt_node(q); q->nd[n].ch[k] = c; q->nd[c].size = h; }
    QNode *N = &q->nd[n];
    int c0 = N->ch[0], c1 = N->ch[1], c2 = N->ch[2], c3 = N->ch[3];
    q->nd[c0].x = N->x; q->nd[c0].y = N->y;
    q->nd[c1].x = N->x + h; q->nd[c1].y = N->y + h;
    q->nd[c2].x = q->nd[c1].x; q->nd[c2].y = N->y;
    q->nd[c3].x = N->x; q->nd[c3].y = q->nd[c1].y;
    q->nd[c0].head = N->head; N->head = -1;   /* FUN_18000e430 */
    q->nd[c0].count = N->count;
    qt_distribute(q, 0, c0, c2, q->nd[c1].x, 1);
    qt_distribute(q, 1, c0, c3, q->nd[c1].y, 2);
    qt_distribute(q, 1, c2, c1, q->nd[c1].y, 2);
    N = &q->nd[n];
    int half = N->count / 2;   /* slot 3 FUN_180005c30 */
    if (N->f20 != 3 && (half < q->nd[c0].count || half < q->nd[c1].count || half < q->nd[c2].count || half < q->nd[c3].count))
        for (int k = 0; k < 4; k++) q->nd[N->ch[k]].done = 1;
}
static void qtree_groups(Shell *S, int ninput)
{
    QTree q; memset(&q, 0, sizeof q);
    q.tol = 1e-7;   /* FUN_1800066f0(.., 1000, 0x3e7ad7f29abcaf48) */
    for (int i = 0; i < ninput; i++) il_free(&S->e[i].groups);
    if (S->nv == 0 || ninput == 0) return;
    /* box of all vertices (Shell2D +0x18 list); the max is only updated when the value is not a new min */
    double x0 = S->v[0].c[0].v, y0 = S->v[0].c[1].v, x1 = x0, y1 = y0;
    for (int i = 1; i < S->nv; i++) {
        double x = S->v[i].c[0].v, y = S->v[i].c[1].v;
        if (x0 < x || x0 == x) { if (x1 < x) x1 = x; } else x0 = x;
        if (y0 < y || y0 == y) { if (y1 < y) y1 = y; } else y0 = y;
    }
    int root = qt_node(&q);
    /* one QTreeEdge per input edge, the Shell2D +0x30 list (push-front) walked from its head and each pushed front on
     * the root's list (FUN_180005710): the root list is in creation order; points per vertex (FUN_180006a20) */
    int *vp = malloc(sizeof(int) * (size_t)S->nv);
    for (int i = 0; i < S->nv; i++) vp[i] = -1;
    for (int i = ninput - 1; i >= 0; i--) {
        int a = S->e[i].a, b = S->e[i].b;
        if (vp[a] < 0) vp[a] = qt_pt(&q, S->v[a].c[0].v, S->v[a].c[1].v);
        if (vp[b] < 0) vp[b] = qt_pt(&q, S->v[b].c[0].v, S->v[b].c[1].v);
        int en = qt_ent(&q, vp[a], vp[b]);
        q.en[en].edge = i; q.en[en].fresh = 1;
        q.en[en].next = q.nd[root].head; q.nd[root].head = en; q.nd[root].count++;
    }
    free(vp);
    double side = (y1 - y0) < (x1 - x0) ? x1 - x0 : y1 - y0;   /* FUN_1800043f0 = max */
    q.nd[root].x = x0; q.nd[root].y = y0; q.nd[root].size = side;   /* FUN_180005790 */
    q.min = side;   /* FUN_180009400: side / 2^(ceil(log n / log 4) + 1) */
    if (q.nd[root].count != 0) q.min = side / pow(2.0, (double)((int)ceil(log((double)q.nd[root].count) / log(4.0)) + 1));
    /* FUN_180005820: depth first (stack), children in order 0..3 */
    q.nd[root].f20 = 3;
    int *st = malloc(sizeof(int) * 64), sn = 0, sc = 64;
    st[sn++] = root;
    while (sn > 0) {
        int n = st[sn - 1];
        if (!q.nd[n].done) qt_split(&q, n);
        if (q.nd[n].i22 == 4) sn--;
        else if (q.nd[n].i22 < 0) {   /* never split: slot 0 FUN_18003aca0 */
            sn--;
            if (q.nd[n].count > 1) {
                if (S->ngrp % 256 == 0) S->grp = realloc(S->grp, sizeof(IList) * (size_t)(S->ngrp + 256));
                int g = S->ngrp++; memset(&S->grp[g], 0, sizeof(IList));
                int cnt = 0; for (int e = q.nd[n].head; e >= 0; e = q.en[e].next) cnt++;
                int *tmp = malloc(sizeof(int) * (size_t)(cnt + 1)), k = 0;
                for (int e = q.nd[n].head; e >= 0; e = q.en[e].next) tmp[k++] = q.en[e].edge;
                for (int i = cnt - 1; i >= 0; i--) il_push(&S->grp[g], tmp[i]);   /* popped from the leaf, pushed front */
                for (int i = 0; i < cnt; i++) il_push(&S->e[tmp[i]].groups, g);    /* group pushed front on each edge */
                free(tmp);
            }
        } else {
            while (q.nd[n].i22 < 4) {
                int c = q.nd[n].ch[q.nd[n].i22];
                q.nd[n].i22++;
                if (q.nd[c].count != 0) {
                    if (sn == sc) { sc *= 2; st = realloc(st, sizeof(int) * (size_t)sc); }
                    st[sn++] = c;
                    break;
                }
            }
        }
    }
    free(st); free(q.pt); free(q.en); free(q.nd);
}

/* ================================================================== IFSpinsil::Run */
int ifk_spinsil_run(const IfkMesh *in, double tol, IfkCurves *out)
{
    memset(out, 0, sizeof *out);
    g_overflow = 0;

    /* Stitch::Run(tol, "Preprocessing", false), phases 1-4 */
    int *tri, nt, nn; SNode *sn;
    stitch(in, tol, &tri, &nt, &sn, &nn);

    /* phase 5: edges of the remapped triangles; T-junction nodes on edges not used by exactly two triangles */
    ESet E; memset(&E, 0, sizeof E);
    E.tol = tol;                                    /* FUN_180039460 stores Run's tolerance at +0x60 */
    Emit m; memset(&m, 0, sizeof m);
    m.sn = sn; m.E = &E;
    m.enode = malloc(sizeof(int) * (size_t)(nn + 1));
    for (int i = 0; i < nn; i++) m.enode[i] = -1;
    {
        TE *te = calloc((size_t)nt * 3 + 1, sizeof *te); int ne = 0;
        for (int t = 0; t < nt; t++) for (int k = 0; k < 3; k++) {
            int a = tri[3 * t + k], b = tri[3 * t + (k + 1) % 3];
            te[ne].a = a < b ? a : b; te[ne].b = a < b ? b : a; ne++;
        }
        qsort(te, (size_t)ne, sizeof *te, cmp_te);
        int w = 0;
        for (int i = 0; i < ne;) { int j = i; while (j < ne && te[j].a == te[i].a && te[j].b == te[i].b) j++; te[w] = te[i]; te[w].cnt = j - i; w++; i = j; }
        ne = w;
        /* the octree objects are the representatives of unpinned nodes (FUN_180009d80) */
        int *obj = NULL, no = 0;
        for (int i = 0; i < nn; i++) if (sn[i].rep == i && !sn[i].pinned) { obj = realloc(obj, sizeof(int) * (size_t)(no + 1)); obj[no++] = i; }
        g_snodes = sn;
        qsort(obj, (size_t)no, sizeof(int), cmp_snode_x);
        double tol2 = tol * tol;
        for (int i = 0; i < ne; i++) {
            if (te[i].cnt == 2) continue;
            const double *A = sn[te[i].a].p, *B = sn[te[i].b].p;
            double xlo = (A[0] < B[0] ? A[0] : B[0]) - tol, xhi = (A[0] < B[0] ? B[0] : A[0]) + tol;
            int lo = 0, hi = no;
            while (lo < hi) { int mid = (lo + hi) / 2; if (sn[obj[mid]].p[0] < xlo) lo = mid + 1; else hi = mid; }
            for (int k = lo; k < no && sn[obj[k]].p[0] <= xhi; k++) {
                int o = obj[k]; if (o == te[i].a || o == te[i].b) continue;
                if (near_edge(sn[o].p, tol2, A, B)) il_push(&te[i].tj, o);
            }
        }
        free(obj);
        m.te = te; m.ne = ne;
        /* phase 6: per triangle in input order */
        for (int t = 0; t < nt; t++) emit_with_tj(&m, tri[3 * t], tri[3 * t + 1], tri[3 * t + 2]);
        for (int i = 0; i < ne; i++) il_free(&te[i].tj);
        free(te);
    }
    e_end(&E);

    /* Shell2D: two insertion passes (original, mirrored (x, -r)), the EdgeSet iterated in key order */
    Shell S; memset(&S, 0, sizeof S);
    S.g = EPS_IN * EPS_IN; S.g2 = S.g * S.g; S.g3 = S.g2 * S.g;   /* FUN_18003dad0: eps^2, eps^4, eps^6 */
    int no; int *ord = edges_in_key_order(&E, &no, 1);
    S.comp_ext = calloc(2, sizeof(int)); S.comp_done = calloc(2, 1);
    for (int pass = 0; pass < 2; pass++) {
        S.cur = S.ncomp++; S.comp_ext[S.cur] = -1;
        for (int i = 0; i < E.nvx; i++) E.vx[i].sv = -1;
        for (int k = 0; k < no; k++) {
            EEdge *ee = &E.ed[ord[k]];
            int vv[2] = { ee->a, ee->b };
            for (int z = 0; z < 2; z++) {
                EVert *ev = &E.vx[vv[z]];
                if (ev->sv >= 0) continue;
                double x = floor(ev->x / S.g) * S.g, y = floor((pass ? -ev->r : ev->r) / S.g) * S.g;   /* FUN_18003bce0 */
                int nv = sv_new(&S);
                S.v[nv].c[0] = f_exact(x); S.v[nv].c[1] = f_exact(y);
                ev->sv = nv;
                if (S.comp_ext[S.cur] < 0) S.comp_ext[S.cur] = nv;
                else { S.axis = 0; if (sv_cmp(&S, nv, S.comp_ext[S.cur]) == 1) S.comp_ext[S.cur] = nv; }
            }
            int a = E.vx[ee->a].sv, b = E.vx[ee->b].sv;
            if (S.v[a].c[0].v == S.v[b].c[0].v && S.v[a].c[1].v == S.v[b].c[1].v) continue;   /* FUN_18003ab80 */
            int e = se_new(&S);
            S.e[e].a = a; S.e[e].b = b; S.e[e].in = e; S.e[e].comp = S.cur;
            il_push(&S.v[a].inc, e); il_push(&S.v[b].inc, e);
            double x0 = S.v[a].c[0].v, x1 = S.v[b].c[0].v, y0 = S.v[a].c[1].v, y1 = S.v[b].c[1].v;
            S.e[e].bb[0] = x0 < x1 ? x0 : x1; S.e[e].bb[2] = x0 < x1 ? x1 : x0;
            S.e[e].bb[1] = y0 < y1 ? y0 : y1; S.e[e].bb[3] = y0 < y1 ? y1 : y0;
        }
    }
    free(ord);
    /* FUN_18003c000 inserts each closed component into the component tree +0x58 (FUN_180005d50) comparing extreme
     * vertices (FUN_18003bfa0 -> FUN_18003b890): a component that received no edge (every EdgeSet edge interior or
     * collapsed, e.g. a part smaller than the tolerance) has a NULL extreme vertex and the DLL faults there with an
     * access violation during "Preprocessing" (seen with tools/peloader --tpif). No defined result: IFK_SPIN_FAULT. */
    int fault = 0;
    for (int c = 0; c < S.ncomp; c++) if (S.comp_ext[c] < 0 && S.ncomp > 1) fault = 1;
    if (fault) snprintf(out->err, sizeof out->err, "access violation in FUN_18003bfa0 (component without vertex)");
    else {
    /* the walk FUN_18003e860 starts with FUN_18003e370: the EdgeQuadtree groups of the input edges */
    qtree_groups(&S, S.ne);
    g_trace_res = getenv("IFK_TRACE_RES") != NULL;

    walk(&S);
    out->cracked = !S.ok;
    out->overflow = g_overflow;
    if (S.err[0]) snprintf(out->err, sizeof out->err, "%s", S.err);

    /* output (end of IFSpinsil::Run): list +0x40 from its front (newest), nodes on first use as (x, y, 0) */
    for (int i = S.out.n - 1; i >= 0; i--) {
        SEdge *e = &S.e[S.out.v[i]];
        int ends[2] = { e->a, e->b };
        for (int z = 0; z < 2; z++) {
            SVert *v = &S.v[ends[z]];
            if (v->outid < 0) {
                out->node = realloc(out->node, sizeof(double) * 3 * (size_t)(out->nnode + 1));
                out->node[3 * out->nnode] = v->c[0].v; out->node[3 * out->nnode + 1] = v->c[1].v; out->node[3 * out->nnode + 2] = 0.0;
                v->outid = out->nnode++;
            }
        }
        out->seg = realloc(out->seg, sizeof(int) * 2 * (size_t)(out->nseg + 1));
        out->seg[2 * out->nseg] = S.v[e->a].outid; out->seg[2 * out->nseg + 1] = S.v[e->b].outid; out->nseg++;
    }
    }

    for (int i = 0; i < S.nv; i++) il_free(&S.v[i].inc);
    for (int i = 0; i < S.ne; i++) il_free(&S.e[i].hits);
    for (int i = 0; i < S.ne; i++) { il_free(&S.e[i].groups); il_free(&S.e[i].saved); }
    il_free(&S.sub38); il_free(&S.split80); il_free(&S.x20);
    if (S.m70) { free(S.m70->k); free(S.m70->v); free(S.m70); }
    if (S.m78) { free(S.m78->k); free(S.m78->v); free(S.m78); }
    for (int i = 0; i < S.ngrp; i++) il_free(&S.grp[i]);
    free(S.v); free(S.e); free(S.comp_ext); free(S.comp_done); free(S.grp); il_free(&S.out);
    for (int i = 0; i < E.ned; i++) il_free(&E.ed[i].tris);
    free(E.nd); free(E.vx); free(E.ed); free(E.tr); free(E.h); free(E.eh);
    free(m.enode); free(tri); free(sn);
    return fault ? IFK_SPIN_FAULT : 0;
}

void ifk_curves_free(IfkCurves *c) { free(c->node); free(c->seg); memset(c, 0, sizeof *c); }
void ifk_mesh_free(IfkMesh *m) { free(m->node); free(m->tri); memset(m, 0, sizeof *m); }

/* ================================================================== files */
int ifk_read_bin_surface(const char *path, IfkMesh *m)
{
    memset(m, 0, sizeof *m);
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    char hdr[14]; int32_t nn, nt;
    if (fread(hdr, 1, 14, f) != 14 || fread(&nn, 4, 1, f) != 1 || fread(&nt, 4, 1, f) != 1 || nn < 0 || nt < 0) { fclose(f); return -2; }
    m->nnode = nn; m->ntri = nt;
    m->node = malloc(sizeof(double) * 3 * (size_t)(nn + 1)); m->tri = malloc(sizeof(int) * 3 * (size_t)(nt + 1));
    if (fread(m->node, 24, (size_t)nn, f) != (size_t)nn) { fclose(f); return -3; }
    for (int t = 0; t < nt; t++) {
        int32_t ix[3]; unsigned char fl;
        if (fread(ix, 4, 3, f) != 3 || fread(&fl, 1, 1, f) != 1) { fclose(f); return -4; }
        for (int k = 0; k < 3; k++) { if (ix[k] < 1 || ix[k] > nn) { fclose(f); return -5; } m->tri[3 * t + k] = ix[k] - 1; }
    }
    fclose(f);
    return 0;
}
int ifk_write_bin_surface(const char *path, const IfkMesh *m, unsigned char flag)
{
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    int32_t nn = m->nnode, nt = m->ntri;
    fwrite("binary surface", 1, 14, f); fwrite(&nn, 4, 1, f); fwrite(&nt, 4, 1, f);
    fwrite(m->node, 24, (size_t)nn, f);
    for (int t = 0; t < nt; t++) {
        int32_t ix[3] = { m->tri[3 * t] + 1, m->tri[3 * t + 1] + 1, m->tri[3 * t + 2] + 1 };
        fwrite(ix, 4, 3, f); fwrite(&flag, 1, 1, f);
    }
    return fclose(f) == 0 ? 0 : -2;
}
int ifk_read_stl(const char *path, IfkMesh *m)
{
    memset(m, 0, sizeof *m);
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char h[84];
    if (sz >= 84 && fread(h, 1, 84, f) == 84) {
        uint32_t n; memcpy(&n, h + 80, 4);
        if ((long)(84 + 50 * (long)n) == sz) {
            m->ntri = (int)n; m->nnode = 3 * (int)n;
            m->node = malloc(sizeof(double) * 9 * (size_t)(n + 1)); m->tri = malloc(sizeof(int) * 3 * (size_t)(n + 1));
            for (uint32_t t = 0; t < n; t++) {
                unsigned char r[50]; float fl[12];
                if (fread(r, 1, 50, f) != 50) { fclose(f); return -2; }
                memcpy(fl, r, 48);
                for (int k = 0; k < 9; k++) m->node[9 * t + k] = fl[3 + k];
                for (int k = 0; k < 3; k++) m->tri[3 * t + k] = 3 * (int)t + k;
            }
            fclose(f); return 0;
        }
    }
    fseek(f, 0, SEEK_SET);
    char line[512]; int cap = 0;
    while (fgets(line, sizeof line, f)) {
        double x, y, z;
        if (sscanf(line, " vertex %lf %lf %lf", &x, &y, &z) == 3) {
            if (m->nnode == cap) { cap = cap ? cap * 2 : 768; m->node = realloc(m->node, sizeof(double) * 3 * (size_t)cap); }
            m->node[3 * m->nnode] = x; m->node[3 * m->nnode + 1] = y; m->node[3 * m->nnode + 2] = z; m->nnode++;
        }
    }
    fclose(f);
    m->ntri = m->nnode / 3; m->nnode = m->ntri * 3;
    m->tri = malloc(sizeof(int) * 3 * (size_t)(m->ntri + 1));
    for (int i = 0; i < m->nnode; i++) m->tri[i] = i;
    return m->ntri > 0 ? 0 : -3;
}

/* ================================================================== ComputeMC transform (section 6.1) */
double ifk_spinsil_transform(const double lo[3], const double hi[3], double tol, int big,
                             double t[3], double *s, int *translated, int *scaled)
{
    return ifk_mc_transform(lo, hi, tol, big, 1, t, s, translated, scaled);
}
double ifk_mc_transform(const double lo[3], const double hi[3], double tol, int big, int spin,
                        double t[3], double *s, int *translated, int *scaled)
{
    double lim = big ? 500.0 : 20.0, nudge = big ? 0.1 : 0.04;
    t[0] = t[1] = t[2] = 0.0; *s = 1.0; *translated = *scaled = 0;
    for (int i = 0; i < 3; i++) {                      /* getTransVec */
        double c = (hi[i] + lo[i]) * 0.5;
        if (lim < fabs(c)) t[i] = c <= 0.0 ? fabs(c) : -c;
    }
    if (t[0] != 0.0 || t[1] != 0.0 || t[2] != 0.0) {    /* CheckDataLocation */
        t[1] = t[1] + nudge; t[0] = t[0] + nudge * 5.0;
        if (spin) t[1] = t[2] = 0.0;                    /* ComputeMCSpinsil::adjustTransVec (18031c9a0); ComputeMC's and
                                                           ComputeMCSilhouette's are the same empty function (RVA 0x4630, ret) */
        *translated = 1;
    }
    double p = 5e-05 / tol;                             /* CheckDataSize / checkToleranceValues */
    int ok = 5e-05 <= tol;
    double sf = 1.0; int use = 0;
    if (!ok) { sf = p; use = 1; }
    else {
        double ex = hi[0] - lo[0], ey = hi[1] - lo[1], ez = hi[2] - lo[2];
        if (3950.0 < ex || 3950.0 < ey || 3950.0 < ez) {   /* GetScaleFactor */
            double m = fabs(ez) < fabs(ey) ? ey : ez;
            double ax = fabs(ex);
            if (ax < fabs(m) || ax == fabs(m)) ax = fabs(ey) <= fabs(ez) ? fabs(ez) : fabs(ey);
            double g = 3950.0 / (0.0 + ax);
            if (p < g) { sf = g; use = 1; }
        }
    }
    if (use && sf != 1.0) { *s = sf; *scaled = 1; }
    return tol * *s;
}
