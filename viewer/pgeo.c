/* pgeo.c - exact line / arc loops and their offset (see pgeo.h for the model and what is decoded / substituted). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pgeo.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TWO_PI (2 * M_PI)

static PV vadd(PV a, PV b) { return pv(a.x + b.x, a.y + b.y); }
static PV vsub(PV a, PV b) { return pv(a.x - b.x, a.y - b.y); }
static PV vmul(PV a, double k) { return pv(a.x * k, a.y * k); }
static double vdot(PV a, PV b) { return a.x * b.x + a.y * b.y; }
static double vcross(PV a, PV b) { return a.x * b.y - a.y * b.x; }
static double vlen(PV a) { return hypot(a.x, a.y); }
static double vdist(PV a, PV b) { return hypot(a.x - b.x, a.y - b.y); }
static double ang_norm(double a) { a = fmod(a, TWO_PI); if (a < 0) a += TWO_PI; return a; }   /* [0, 2 pi) */

Gt gt_line(PV a, PV b) { Gt e; memset(&e, 0, sizeof e); e.t = 'L'; e.a = a; e.b = b; return e; }
Gt gt_arc(PV c, double r, double sa, double sw) { Gt e; memset(&e, 0, sizeof e); e.t = 'A'; e.c = c; e.r = r; e.sa = sa; e.sw = sw; return e; }

PV gt_pt(const Gt *e, double t) {
    if (e->t == 'L') return pv(e->a.x + (e->b.x - e->a.x) * t, e->a.y + (e->b.y - e->a.y) * t);
    if (e->t == 'A') { double a = e->sa + e->sw * t; return pv(e->c.x + e->r * cos(a), e->c.y + e->r * sin(a)); }
    return e->a;
}
PV gt_start(const Gt *e) { return gt_pt(e, 0); }
PV gt_end(const Gt *e) { return gt_pt(e, 1); }
PV gt_tan(const Gt *e, double t) {
    if (e->t == 'L') { PV d = vsub(e->b, e->a); double l = vlen(d); return l > 0 ? vmul(d, 1 / l) : pv(1, 0); }
    if (e->t == 'A') { double a = e->sa + e->sw * t, s = e->sw >= 0 ? 1 : -1; return pv(-sin(a) * s, cos(a) * s); }
    return pv(1, 0);
}
double gt_len(const Gt *e) {
    if (e->t == 'L') return vdist(e->a, e->b);
    if (e->t == 'A') return fabs(e->sw) * e->r;
    return 0;
}
Gt gt_rev(const Gt *e) {
    Gt r = *e;
    if (e->t == 'L') { r.a = e->b; r.b = e->a; }
    else if (e->t == 'A') { r.sa = e->sa + e->sw; r.sw = -e->sw; }
    return r;
}
Gt gt_sub(const Gt *e, double t0, double t1) {
    Gt r = *e;
    if (e->t == 'L') { r.a = gt_pt(e, t0); r.b = gt_pt(e, t1); }
    else if (e->t == 'A') { r.sa = e->sa + e->sw * t0; r.sw = e->sw * (t1 - t0); }
    return r;
}
/* parameter of the nearest point (t_on_gt role), clamped to [0, 1] */
double gt_t_near(const Gt *e, PV q) {
    if (e->t == 'L') {
        PV d = vsub(e->b, e->a); double l2 = vdot(d, d);
        double t = l2 > 0 ? vdot(vsub(q, e->a), d) / l2 : 0; return t < 0 ? 0 : t > 1 ? 1 : t;
    }
    if (e->t == 'A') {
        if (fabs(e->sw) < 1e-15) return 0;
        double a = atan2(q.y - e->c.y, q.x - e->c.x);
        double rel = e->sw > 0 ? ang_norm(a - e->sa) : ang_norm(e->sa - a);   /* along the sweep from the start */
        double sw = fabs(e->sw);
        if (sw >= TWO_PI - 1e-12) return rel / sw;
        if (rel <= sw) return rel / sw;
        /* outside the sweep: the nearer end */
        return (TWO_PI - rel) < (rel - sw) ? 0 : 1;
    }
    return 0;
}

void gl_push(Gl *l, Gt e) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 16; l->e = realloc(l->e, l->cap * sizeof(Gt)); }
    l->e[l->n++] = e;
}
void gl_free(Gl *l) { free(l->e); l->e = NULL; l->n = l->cap = 0; }
Gl gl_copy(const Gl *l) { Gl r = *l; r.cap = l->n ? l->n : 1; r.e = malloc(r.cap * sizeof(Gt)); if (l->n) memcpy(r.e, l->e, l->n * sizeof(Gt)); return r; }

double gl_area(const Gl *l) {
    /* Green: sum over lines of cross(a, b) / 2; arcs: chord term + the segment area r^2 (sw - sin sw) / 2 */
    double A = 0;
    for (int i = 0; i < l->n; i++) {
        const Gt *e = &l->e[i]; PV a = gt_start(e), b = gt_end(e);
        A += vcross(a, b) / 2;
        if (e->t == 'A') A += e->r * e->r * (e->sw - sin(e->sw)) / 2;
    }
    return A;
}
double gl_len(const Gl *l) { double s = 0; for (int i = 0; i < l->n; i++) s += gt_len(&l->e[i]); return s; }
void gl_reverse(Gl *l) {
    for (int i = 0, j = l->n - 1; i < j; i++, j--) { Gt t = l->e[i]; l->e[i] = l->e[j]; l->e[j] = t; }
    for (int i = 0; i < l->n; i++) l->e[i] = gt_rev(&l->e[i]);
}
void gl_rotate(Gl *l, int i) {
    if (l->n < 2 || i <= 0 || i >= l->n) return;
    Gt *t = malloc(l->n * sizeof(Gt));
    for (int k = 0; k < l->n; k++) t[k] = l->e[(i + k) % l->n];
    memcpy(l->e, t, l->n * sizeof(Gt)); free(t);
}
void gl_split_start(Gl *l, int i, double t) {
    if (l->n < 1 || i < 0 || i >= l->n) return;
    double len = gt_len(&l->e[i]);
    if (t * len < 1e-9) { gl_rotate(l, i); return; }
    if ((1 - t) * len < 1e-9) { gl_rotate(l, (i + 1) % l->n); return; }
    Gt a = gt_sub(&l->e[i], 0, t), b = gt_sub(&l->e[i], t, 1);
    Gl r = { 0 }; r.island = l->island; r.tag = l->tag;
    gl_push(&r, b);
    for (int k = 1; k < l->n; k++) gl_push(&r, l->e[(i + k) % l->n]);
    gl_push(&r, a);
    free(l->e); *l = r;
}
void gl_bbox(const Gl *l, double bb[4]) {
    bb[0] = bb[1] = 1e300; bb[2] = bb[3] = -1e300;
    for (int i = 0; i < l->n; i++) {
        const Gt *e = &l->e[i]; PV p[6]; int np = 0;
        p[np++] = gt_start(e); p[np++] = gt_end(e);
        if (e->t == 'A') for (int k = 0; k < 4; k++) {
            double a = k * M_PI / 2, rel = e->sw > 0 ? ang_norm(a - e->sa) : ang_norm(e->sa - a);
            if (rel <= fabs(e->sw)) p[np++] = pv(e->c.x + e->r * cos(a), e->c.y + e->r * sin(a));
        }
        for (int k = 0; k < np; k++) { bb[0] = fmin(bb[0], p[k].x); bb[1] = fmin(bb[1], p[k].y); bb[2] = fmax(bb[2], p[k].x); bb[3] = fmax(bb[3], p[k].y); }
    }
}
double gl_closest(const Gl *l, PV q, int *ei, double *t, PV *p) {
    double best = 1e300;
    for (int i = 0; i < l->n; i++) {
        if (l->e[i].t == 'P') continue;
        double u = gt_t_near(&l->e[i], q); PV x = gt_pt(&l->e[i], u); double d = vdist(x, q);
        if (d < best) { best = d; if (ei) *ei = i; if (t) *t = u; if (p) *p = x; }
    }
    return best;
}

static int arc_steps(double r, double sw, double tol) {
    double asw = fabs(sw);
    if (r <= tol || asw < 1e-12) return 1;
    double h = 2 * acos(1 - fmin(tol / r, 1.0));
    int n = (int)ceil(asw / fmax(h, 1e-4));
    if (n < 1) n = 1;
    if (n > 20000) n = 20000;
    return n;
}
CPath gl_sample(const Gl *l, double tol) {
    CPath c = { NULL, 0 }; int cap = 0;
#define PUSHP(P) do { if (c.n == cap) { cap = cap ? cap * 2 : 64; c.xy = realloc(c.xy, cap * 2 * sizeof(double)); } c.xy[2 * c.n] = (P).x; c.xy[2 * c.n + 1] = (P).y; c.n++; } while (0)
    for (int i = 0; i < l->n; i++) {
        const Gt *e = &l->e[i];
        if (e->t == 'P') continue;
        PV s = gt_start(e); PUSHP(s);
        if (e->t == 'A') { int m = arc_steps(e->r, e->sw, tol); for (int k = 1; k < m; k++) { PV q = gt_pt(e, (double)k / m); PUSHP(q); } }
    }
#undef PUSHP
    if (!c.xy) c.xy = malloc(2 * sizeof(double));
    return c;
}

void gls_push(Gls *s, Gl l) {
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 8; s->l = realloc(s->l, s->cap * sizeof(Gl)); }
    s->l[s->n++] = l;
}
void gls_free(Gls *s) { for (int i = 0; i < s->n; i++) gl_free(&s->l[i]); free(s->l); s->l = NULL; s->n = s->cap = 0; }
Gls gls_copy(const Gls *s) { Gls r = { 0 }; for (int i = 0; i < s->n; i++) gls_push(&r, gl_copy(&s->l[i])); return r; }
CPaths gls_sample(const Gls *s, double tol) {
    CPaths r; r.n = 0; r.p = calloc(s->n ? s->n : 1, sizeof(CPath));
    for (int i = 0; i < s->n; i++) { CPath c = gl_sample(&s->l[i], tol); if (c.n >= 3) r.p[r.n++] = c; else free(c.xy); }
    return r;
}
static int pip_c(const CPath *c, double x, double y) {
    int in = 0;
    for (int i = 0, j = c->n - 1; i < c->n; j = i++) {
        double xi = c->xy[2 * i], yi = c->xy[2 * i + 1], xj = c->xy[2 * j], yj = c->xy[2 * j + 1];
        if (((yi > y) != (yj > y)) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}
int gls_inside(const Gls *s, PV q, double tol) {
    int in = 0;
    for (int i = 0; i < s->n; i++) { CPath c = gl_sample(&s->l[i], tol); in ^= pip_c(&c, q.x, q.y); free(c.xy); }
    return in;
}
Gls gls_from_cpaths(const CPaths *c) {
    Gls r = { 0 };
    for (int i = 0; i < c->n; i++) {
        const CPath *p = &c->p[i]; if (p->n < 3) continue;
        Gl l = { 0 };
        for (int k = 0; k < p->n; k++) { int k2 = (k + 1) % p->n;
            PV a = pv(p->xy[2 * k], p->xy[2 * k + 1]), b = pv(p->xy[2 * k2], p->xy[2 * k2 + 1]);
            if (vdist(a, b) > 1e-12) gl_push(&l, gt_line(a, b)); }
        l.island = cl_area(p) < 0;
        gls_push(&r, l);
    }
    return r;
}

/* segment intersection test between two sampled loops (pock_bound_intersect role) */
static int seg_x(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy) {
    double d1 = (dx - cx) * (ay - cy) - (dy - cy) * (ax - cx), d2 = (dx - cx) * (by - cy) - (dy - cy) * (bx - cx);
    double d3 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax), d4 = (bx - ax) * (dy - ay) - (by - ay) * (dx - ax);
    return ((d1 > 0) != (d2 > 0) || d1 == 0 || d2 == 0) && ((d3 > 0) != (d4 > 0) || d3 == 0 || d4 == 0);
}
int gl_cross(const Gl *a, const Gl *b, double tol) {
    double ba[4], bb[4]; gl_bbox(a, ba); gl_bbox(b, bb);
    if (ba[2] < bb[0] - tol || bb[2] < ba[0] - tol || ba[3] < bb[1] - tol || bb[3] < ba[1] - tol) return 0;
    CPath p = gl_sample(a, tol), q = gl_sample(b, tol); int hit = 0;
    for (int i = 0; i < p.n && !hit; i++) {
        int i2 = (i + 1) % p.n;
        double x0 = fmin(p.xy[2 * i], p.xy[2 * i2]), x1 = fmax(p.xy[2 * i], p.xy[2 * i2]);
        double y0 = fmin(p.xy[2 * i + 1], p.xy[2 * i2 + 1]), y1 = fmax(p.xy[2 * i + 1], p.xy[2 * i2 + 1]);
        for (int j = 0; j < q.n && !hit; j++) {
            int j2 = (j + 1) % q.n;
            if (fmax(q.xy[2 * j], q.xy[2 * j2]) < x0 || fmin(q.xy[2 * j], q.xy[2 * j2]) > x1) continue;
            if (fmax(q.xy[2 * j + 1], q.xy[2 * j2 + 1]) < y0 || fmin(q.xy[2 * j + 1], q.xy[2 * j2 + 1]) > y1) continue;
            hit = seg_x(p.xy[2 * i], p.xy[2 * i + 1], p.xy[2 * i2], p.xy[2 * i2 + 1], q.xy[2 * j], q.xy[2 * j + 1], q.xy[2 * j2], q.xy[2 * j2 + 1]);
        }
    }
    free(p.xy); free(q.xy);
    return hit;
}

/* ------------------------------------------------------------------------------------------------ offset */
/* candidate offset elements: kind 0 offset line (a -> b), 1 offset arc (centre c, radius r, input sweep from sa by sw),
   2 corner arc about an input vertex (radius r, angular span from a0 by the signed turn sp) */
typedef struct { int kind; PV a, b; PV c; double r, sa, sw; double turn; } Cand;

static int cand_dist(const Cand *k, PV q, double slack, double *dist) {
    if (k->kind == 0) {
        PV d = vsub(k->b, k->a); double L = vlen(d); if (L < 1e-15) return 0;
        double t = vdot(vsub(q, k->a), d) / (L * L);
        if (t < -slack / L || t > 1 + slack / L) return 0;
        *dist = fabs(vcross(d, vsub(q, k->a))) / L; return 1;
    }
    double rq = vdist(q, k->c);
    double a = atan2(q.y - k->c.y, q.x - k->c.x);
    double rel = k->sw >= 0 ? ang_norm(a - k->sa) : ang_norm(k->sa - a), sw = fabs(k->sw);
    double angslack = k->r > 1e-12 ? slack / k->r : 0;
    if (sw < TWO_PI - 1e-9 && rel > sw + angslack && TWO_PI - rel > angslack) return 0;
    *dist = fabs(rq - k->r); return 1;
}

/* carrier intersections (lines infinite, circles full); returns the number of points (0..2) */
static int x_line_line(PV a, PV da, PV b, PV db, PV *o) {
    double den = vcross(da, db); if (fabs(den) < 1e-14 * vlen(da) * vlen(db)) return 0;
    double s = vcross(vsub(b, a), db) / den; o[0] = vadd(a, vmul(da, s)); return 1;
}
static int x_line_circ(PV a, PV d, PV c, double r, PV *o) {
    double L = vlen(d); if (L < 1e-15) return 0; PV u = vmul(d, 1 / L);
    PV w = vsub(a, c); double b = vdot(w, u), cc = vdot(w, w) - r * r, disc = b * b - cc;
    if (disc < 0) { if (disc < -1e-6 * r * r - 1e-12) return 0; disc = 0; }
    double s = sqrt(disc);
    o[0] = vadd(a, vmul(u, -b - s)); o[1] = vadd(a, vmul(u, -b + s)); return 2;
}
static int x_circ_circ(PV c1, double r1, PV c2, double r2, PV *o) {
    double d = vdist(c1, c2); if (d < 1e-15) return 0;
    double a = (r1 * r1 - r2 * r2 + d * d) / (2 * d), h2 = r1 * r1 - a * a;
    if (h2 < 0) { if (h2 < -1e-6 * fmax(r1, r2) * fmax(r1, r2) - 1e-12) return 0; h2 = 0; }
    double h = sqrt(h2); PV u = vmul(vsub(c2, c1), 1 / d), m = vadd(c1, vmul(u, a));
    o[0] = vadd(m, pv(-u.y * h, u.x * h)); o[1] = vadd(m, pv(u.y * h, -u.x * h)); return 2;
}
static int cand_x(const Cand *p, const Cand *q, PV near, PV *out) {
    PV o[2]; int n;
    if (p->kind == 0 && q->kind == 0) n = x_line_line(p->a, vsub(p->b, p->a), q->a, vsub(q->b, q->a), o);
    else if (p->kind == 0) n = x_line_circ(p->a, vsub(p->b, p->a), q->c, q->r, o);
    else if (q->kind == 0) n = x_line_circ(q->a, vsub(q->b, q->a), p->c, p->r, o);
    else n = x_circ_circ(p->c, p->r, q->c, q->r, o);
    if (!n) return 0;
    *out = o[0];
    if (n == 2 && vdist(o[1], near) < vdist(o[0], near)) *out = o[1];
    return 1;
}
/* transversal intersections of two entities within both (pock_bound_intersect / ints_on_gt_new role): tangential
   contacts (discriminant within tol) are not counted */
static int ent_on(const Gt *e, PV q, double tol) {
    double t = gt_t_near(e, q); PV x = gt_pt(e, t); return vdist(x, q) <= tol;
}
int gt_cross(const Gt *a, const Gt *b, double tol) {
    PV o[2]; int n = 0;
    if (a->t == 'P' || b->t == 'P') return 0;
    if (a->t == 'L' && b->t == 'L') {
        PV da = vsub(a->b, a->a), db = vsub(b->b, b->a); double den = vcross(da, db);
        if (fabs(den) < 1e-15) return 0;
        double s = vcross(vsub(b->a, a->a), db) / den, u = vcross(vsub(b->a, a->a), da) / den;
        double la = vlen(da), lb = vlen(db);
        return s >= -tol / la && s <= 1 + tol / la && u >= -tol / lb && u <= 1 + tol / lb;
    }
    if (a->t == 'A' && b->t == 'A') {
        double d = vdist(a->c, b->c); if (d < 1e-15) return 0;
        double h2 = a->r * a->r - pow((a->r * a->r - b->r * b->r + d * d) / (2 * d), 2);
        if (h2 <= tol * tol) return 0;                       /* apart or tangent */
        n = x_circ_circ(a->c, a->r, b->c, b->r, o);
    } else {
        const Gt *l = a->t == 'L' ? a : b, *c = a->t == 'L' ? b : a;
        PV d = vsub(l->b, l->a); double L = vlen(d); if (L < 1e-15) return 0;
        PV u = vmul(d, 1 / L), w = vsub(l->a, c->c); double bb = vdot(w, u), disc = bb * bb - (vdot(w, w) - c->r * c->r);
        if (disc <= tol * tol) return 0;
        n = x_line_circ(l->a, d, c->c, c->r, o);
    }
    for (int i = 0; i < n; i++) if (ent_on(a, o[i], tol) && ent_on(b, o[i], tol)) return 1;
    return 0;
}

static double cand_angle(const Cand *k, PV q) { return atan2(q.y - k->c.y, q.x - k->c.x); }

/* one output loop of Clipper -> exact elements. Every polygon edge is assigned to the candidate both its ends and its
   midpoint lie on (an arc chord's midpoint is within the arc tolerance); runs of edges on one candidate become one
   element; the vertex between two runs becomes the exact intersection of their carriers. */
static int edge_cand(const Cand *cd, int nc, PV p, PV q, double thr) {
    PV m = vmul(vadd(p, q), 0.5); int best = -1; double bd = 1e300;
    for (int k = 0; k < nc; k++) {
        double d0, d1, d2;
        if (!cand_dist(&cd[k], p, 4 * thr, &d0) || d0 > thr) continue;
        if (!cand_dist(&cd[k], q, 4 * thr, &d1) || d1 > thr) continue;
        if (!cand_dist(&cd[k], m, 4 * thr, &d2) || d2 > 2 * thr) continue;
        double s = d0 + d1 + d2; if (s < bd) { bd = s; best = k; }
    }
    return best;
}
static Gl rebuild(const CPath *poly, const Cand *cd, int nc, double thr, double tol, double min_fil) {
    Gl out = { 0 };
    int n = poly->n; if (n < 3) return out;
#define PT(i) pv(poly->xy[2 * (((i) % n + n) % n)], poly->xy[2 * (((i) % n + n) % n) + 1])
    int *as = malloc(n * sizeof(int));                       /* as[i]: candidate of the edge i -> i + 1 */
    for (int i = 0; i < n; i++) as[i] = edge_cand(cd, nc, PT(i), PT(i + 1), thr);
    int s0 = -1;                                             /* first edge of a run */
    for (int i = 0; i < n; i++) if (as[i] < 0 || as[i] != as[(i + n - 1) % n]) { s0 = i; break; }
    if (s0 < 0) {                                            /* one candidate all round: a full circle (or a line?!) */
        const Cand *k = &cd[as[0]];
        if (k->kind != 0) {
            /* a full circle starts where its source circle starts (the offset of the input start) */
            PV p0 = PT(0); double a0 = cand_angle(k, p0);
            if (k->kind == 1 && fabs(fabs(k->sw) - TWO_PI) < 1e-9) a0 = k->sa;
            gl_push(&out, gt_arc(k->c, k->r, a0, cl_area(poly) > 0 ? TWO_PI : -TWO_PI));
        }
        free(as); return out;
    }
    /* runs: [rs[g], re[g]) in edge indices relative to s0; candidate rk[g] (-1 = a single unassigned edge) */
    int *rs = malloc((n + 1) * sizeof(int)), *rk = malloc((n + 1) * sizeof(int)), ng = 0;
    for (int j = 0; j < n; j++) {
        int i = (s0 + j) % n;
        if (j == 0 || as[i] < 0 || as[i] != rk[ng - 1]) { rs[ng] = j; rk[ng] = as[i]; ng++; }
    }
    rs[ng] = n;
    /* drop corner arcs that turn less than min_fil (the neighbours then meet sharp) */
    char *drop = calloc(ng, 1);
    for (int g = 0; g < ng; g++) drop[g] = rk[g] >= 0 && cd[rk[g]].kind == 2 && fabs(cd[rk[g]].turn) < min_fil && ng > 2;
    /* unassigned edges bridging two assigned runs (a chord across a tangent joint): replaced by the carriers'
       intersection when it lies on that chain */
    for (int g = 0; g < ng; g++) {
        if (rk[g] >= 0 || drop[g]) continue;
        int e = g; while (e + 1 < ng && rk[e + 1] < 0) e++;
        int pg = (g + ng - 1) % ng, ne = (e + 1) % ng;
        if (rk[pg] < 0 || rk[ne] < 0 || rk[pg] == rk[ne] || ng < 3) { g = e; continue; }
        PV a = PT(s0 + rs[g]), b = PT(s0 + rs[e + 1]), x; double L = 0;
        for (int j = rs[g]; j < rs[e + 1]; j++) L += vdist(PT(s0 + j), PT(s0 + j + 1));
        if (cand_x(&cd[rk[pg]], &cd[rk[ne]], vmul(vadd(a, b), 0.5), &x) && vdist(x, a) < L + 10 * thr && vdist(x, b) < L + 10 * thr)
            for (int k = g; k <= e; k++) drop[k] = 1;
        g = e;
    }
    int *G = malloc(ng * sizeof(int)), m = 0;
    for (int g = 0; g < ng; g++) if (!drop[g]) G[m++] = g;
    PV *J = malloc((m ? m : 1) * sizeof(PV));                /* J[i]: start of kept run i */
    for (int i = 0; i < m; i++) {
        int g = G[(i + m - 1) % m], h = G[i];
        PV v = PT(s0 + rs[h]);                               /* the polygon vertex where run h starts */
        if (rk[g] < 0 || rk[h] < 0 || rk[g] == rk[h]) { J[i] = v; continue; }
        int adjacent = (g + 1) % ng == h;
        PV near = adjacent ? v : vmul(vadd(PT(s0 + rs[g + 1]), v), 0.5), x;
        if (cand_x(&cd[rk[g]], &cd[rk[h]], near, &x) && (adjacent ? vdist(x, v) < 50 * thr : 1)) J[i] = x;
        else J[i] = v;
    }
    for (int i = 0; i < m; i++) {
        int g = G[i]; PV A = J[i], B = J[(i + 1) % m];
        if (rk[g] < 0) { if (vdist(A, B) > 1e-12) gl_push(&out, gt_line(A, B)); continue; }
        const Cand *k = &cd[rk[g]];
        if (k->kind == 0) { if (vdist(A, B) > 1e-12) gl_push(&out, gt_line(A, B)); continue; }
        double turn = 0;                                     /* the run's edges give the direction */
        for (int j = rs[g]; j < rs[g + 1]; j++) {
            PV p0 = PT(s0 + j), p1 = PT(s0 + j + 1);
            turn += atan2(vcross(vsub(p0, k->c), vsub(p1, k->c)), vdot(vsub(p0, k->c), vsub(p1, k->c)));
        }
        double dir = turn >= 0 ? 1 : -1;
        double a0 = cand_angle(k, A), a1 = cand_angle(k, B);
        double sw = dir > 0 ? ang_norm(a1 - a0) : -ang_norm(a0 - a1);
        if (m == 1 && fabs(sw) < 1e-9) sw = dir * TWO_PI;
        if (fabs(sw) * k->r > 1e-12) gl_push(&out, gt_arc(k->c, k->r, a0, sw));
    }
#undef PT
    free(as); free(rs); free(rk); free(G); free(J); free(drop);
    (void)tol;
    return out;
}

static Gls gls_offset_mode(const Gls *in, double d, double min_fil, double tol, int diff) {
    Gls res = { 0 };
    double st = tol * 0.02;                                  /* sampling / arc tolerance for the topology */
    if (st < 2e-5) st = 2e-5;                                /* above Clipper's 1e-5 grid (offset.cpp) */
    CPaths src; src.n = 0; src.p = calloc(in->n ? in->n : 1, sizeof(CPath));
    int *isl = diff ? calloc(in->n ? in->n : 1, sizeof(int)) : NULL;
    int nc = 0, capc = 0; Cand *cd = NULL;
#define ADDC(K) do { if (nc == capc) { capc = capc ? capc * 2 : 64; cd = realloc(cd, capc * sizeof(Cand)); } cd[nc++] = (K); } while (0)
    for (int i = 0; i < in->n; i++) {
        const Gl *l = &in->l[i];
        CPath c = gl_sample(l, st);
        if (c.n >= 3) { if (diff) isl[src.n] = l->island; src.p[src.n++] = c; } else { free(c.xy); continue; }
        for (int j = 0; j < l->n; j++) {
            const Gt *e = &l->e[j]; Cand k; memset(&k, 0, sizeof k);
            if (e->t == 'L') {
                PV t = gt_tan(e, 0), nr = pv(t.y, -t.x);     /* right of travel = outside of the region */
                k.kind = 0; k.a = vadd(e->a, vmul(nr, d)); k.b = vadd(e->b, vmul(nr, d)); ADDC(k);
            } else if (e->t == 'A') {
                double r = e->r + d * (e->sw >= 0 ? 1 : -1);
                if (r > 1e-12) { k.kind = 1; k.c = e->c; k.r = r; k.sa = e->sa; k.sw = e->sw; ADDC(k); }
            } else continue;
            /* corner at the end of e: arc about the vertex from the end normal of e to the start normal of the next */
            const Gt *f = &l->e[(j + 1) % l->n]; if (f->t == 'P') continue;
            PV v = gt_end(e), t1 = gt_tan(e, 1), t2 = gt_tan(f, 0);
            PV n1 = pv(t1.y, -t1.x), n2 = pv(t2.y, -t2.x);
            double turn = atan2(vcross(t1, t2), vdot(t1, t2));
            Cand q; memset(&q, 0, sizeof q); q.kind = 2; q.c = v; q.r = fabs(d); q.turn = turn;
            PV s = vmul(n1, d), e2 = vmul(n2, d);
            q.sa = atan2(s.y, s.x); double sp = atan2(vcross(s, e2), vdot(s, e2)); q.sw = sp;
            if (fabs(d) > 1e-12) ADDC(q);
        }
    }
#undef ADDC
    CPaths o;
    if (diff) {   /* gls_merge: the outer loops' union minus the islands' union (both non-zero filled) */
        CPaths A = { 0 }, B = { 0 }; A.p = calloc(src.n ? src.n : 1, sizeof(CPath)); B.p = calloc(src.n ? src.n : 1, sizeof(CPath));
        for (int i = 0; i < src.n; i++) { if (isl[i]) B.p[B.n++] = src.p[i]; else A.p[A.n++] = src.p[i]; }
        o = cl_bool(&A, &B, 2);
        free(A.p); free(B.p); free(isl);
    } else o = fabs(d) < 1e-12 ? cl_bool(&src, NULL, 0) : cl_offset_nz(&src, d, st);
    double thr = st * 2 + 2e-5;
    for (int i = 0; i < o.n; i++) {
        Gl l = rebuild(&o.p[i], cd, nc, thr, tol, min_fil);
        if (l.n == 0) { gl_free(&l); continue; }
        l.island = cl_area(&o.p[i]) < 0;
        gls_push(&res, l);
    }
    free(cd); cl_free(&src); cl_free(&o);
    return res;
}
Gls gls_offset(const Gls *in, double d, double min_fil, double tol) { return gls_offset_mode(in, d, min_fil, tol, 0); }
/* ours (a SUBSTITUTE for mccore merge_pock_bounds when island loops may reach outside the outer loops, as the High
   Speed extra-loop regions of FUN_1801d00d0): the union of the non-island loops minus the union of the island loops,
   rebuilt exactly as gls_offset at distance 0 */
Gls gls_merge(const Gls *in, double min_fil, double tol) { return gls_offset_mode(in, 0, min_fil, tol, 1); }
