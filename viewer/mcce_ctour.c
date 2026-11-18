#include <stdio.h>
/* mcce_ctour.c - port of mcce.dll MCCECalcContourOffset (decomp/mcce_all.c l.8571-8807 and callees; formats/mcce_ctour_offset.md).
 *
 * PARTIAL PORT: one chain of lines / arcs at a single pass (n = 1, flags 0x40027 / 0x60027 for the contour, 0x101 for Dynamic Mill's link smoothing FUN_180048840), closed or open
 * (retraced, the runs of the forward half): gap closing, the self-crossing split of the input (with SoPockUnionAr over several closed pieces), the raw ring, loop removal, several result rings
 * (FUN_1800d3d30 for an island source with the union, mcce_sobool.inc), the tail. Not ported (refused with the path in McceResult.refused): the corner reversal FUN_1800cb710 beyond a retraced chain. Callers must not guess in its place. The citations are decomp/mcce_all.c line numbers. No writable globals. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mcce_ctour.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- shapes (CSh: entity vector +0x18, dir +0x50, island +0x54, reversed-source mark +0x90 bit 1; pock_chn 0x88 bytes) ---- */
typedef McceEnt Ent;
typedef struct { Ent *e; int n, cap, dir, isl, frame, revmark, src_rev; double box[4];
                 int nsrc, unm, srcisl; } Sh;   /* MCCECalcOffset: the source list CSh+0x78 as its size, how many of its shapes lack the FUN_1800da5e0 mark (+0x90 bit 1), and the first source's +0x54 bit 0 (FUN_1800d4170) */

static void sh_init(Sh *s) { memset(s, 0, sizeof *s); }
static void sh_push(Sh *s, const Ent *e) {
    if (s->n == s->cap) { s->cap = s->cap ? 2 * s->cap : 16; s->e = realloc(s->e, (size_t)s->cap * sizeof(Ent)); }
    s->e[s->n++] = *e;
}
static void sh_insert(Sh *s, int at, const Ent *e) {
    if (s->n == s->cap) { s->cap = s->cap ? 2 * s->cap : 16; s->e = realloc(s->e, (size_t)s->cap * sizeof(Ent)); }
    memmove(s->e + at + 1, s->e + at, (size_t)(s->n - at) * sizeof(Ent));
    s->e[at] = *e; s->n++;
}
static void sh_copy(Sh *d, const Sh *s) { *d = *s; d->e = malloc((size_t)(s->cap ? s->cap : 1) * sizeof(Ent)); memcpy(d->e, s->e, (size_t)s->n * sizeof(Ent)); }

/* mcce::SetChnMinMax (l.9234-9395): the box of an entity */
static void ent_box(Ent *e) {
    double *b = e->box;
    if (e->id == 'L') {
        b[0] = fmin(e->a[0], e->a[2]); b[1] = fmin(e->a[1], e->a[3]); b[2] = fmax(e->a[0], e->a[2]); b[3] = fmax(e->a[1], e->a[3]);
        return;
    }
    double cx = e->a[0], cy = e->a[1], r = e->a[2], sa = e->a[3], sw = e->a[4], x = cx + r * cos(sa), y = cy + r * sin(sa);
    b[0] = b[2] = x; b[1] = b[3] = y;
    x = cx + r * cos(sa + sw); y = cy + r * sin(sa + sw);
    b[0] = fmin(b[0], x); b[2] = fmax(b[2], x); b[1] = fmin(b[1], y); b[3] = fmax(b[3], y);
    double lo = fmin(sa, sa + sw), hi = fmax(sa, sa + sw);
    for (int q = (int)floor(lo / (M_PI / 2)) - 1; q <= (int)ceil(hi / (M_PI / 2)) + 1; q++) {
        double t = q * (M_PI / 2);
        if (t < lo || t > hi) continue;
        x = cx + r * cos(t); y = cy + r * sin(t);
        b[0] = fmin(b[0], x); b[2] = fmax(b[2], x); b[1] = fmin(b[1], y); b[3] = fmax(b[3], y);
    }
}
static void sh_box(Sh *s) {
    if (!s->n) return;
    s->box[0] = s->box[1] = 1e300; s->box[2] = s->box[3] = -1e300;
    for (int i = 0; i < s->n; i++) { double *b = s->e[i].box;
        s->box[0] = fmin(s->box[0], b[0]); s->box[1] = fmin(s->box[1], b[1]); s->box[2] = fmax(s->box[2], b[2]); s->box[3] = fmax(s->box[3], b[3]); }
}
static void e_start(const Ent *e, double *x, double *y) {
    if (e->id == 'A') { *x = e->a[0] + e->a[2] * cos(e->a[3]); *y = e->a[1] + e->a[2] * sin(e->a[3]); }
    else { *x = e->a[0]; *y = e->a[1]; }
}
static void e_end(const Ent *e, double *x, double *y) {
    if (e->id == 'A') { double g = e->a[3] + e->a[4]; *x = e->a[0] + e->a[2] * cos(g); *y = e->a[1] + e->a[2] * sin(g); }
    else { *x = e->a[2]; *y = e->a[3]; }
}
static double e_len(const Ent *e) {
    if (e->id == 'L') return hypot(e->a[2] - e->a[0], e->a[3] - e->a[1]);
    if (e->id == 'A') return fabs(e->a[2] * e->a[4]);
    return 0;
}

/* mcce::RevSh (l.4123-4300): entity order reversed, each entity turned, direction flipped */
static void sh_rev(Sh *s) {
    for (int i = 0, j = s->n - 1; i < j; i++, j--) { Ent t = s->e[i]; s->e[i] = s->e[j]; s->e[j] = t; }
    for (int i = 0; i < s->n; i++) {
        Ent *e = &s->e[i];
        if (e->id == 'L') { double t = e->a[0]; e->a[0] = e->a[2]; e->a[2] = t; t = e->a[1]; e->a[1] = e->a[3]; e->a[3] = t; }
        else if (e->id == 'A') { e->a[3] += e->a[4]; e->a[4] = -e->a[4]; }
    }
    if (s->dir) s->dir = -s->dir;
}

/* the raw-offset state: param_1 of FUN_1800dbce0 (only the fields read here) */
typedef struct { double box[4]; double a[2], b[2], c[2]; } Mitre;   /* MITER 0x50 (FUN_1800c9e50): box, the two ends pulled in by 1e-5 along the chord, the mitre point pushed by 1e-5 */
typedef struct {
    double dist;           /* +0x48: |d_i| */
    double t1;             /* +0x28 / +0x50: nc_tol */
    double t2;             /* +0x30: 0.75 nc_tol */
    double ang_cos;        /* +0x58: cos(angle) */
    unsigned flags;        /* +0x10 */
    int n0;                /* +0x88: the entity count of an open source before its Retrace (0: closed; set by the export, l.8737-8740) */
    double src[2], pe[2], ps[2], prev_end[2];      /* +0x90 source point of the current entity, +0xa0 offset end, +0xb0 offset start, +0xc0 the previous entity's offset end */
    double te[2], ts[2], prev_t[2];                /* +0xd0 end tangent, +0xe0 start tangent, +0xf0 previous end tangent */
    Mitre *mit; int nmit, capmit;                  /* +0x70: the mitre records of this ring (FUN_1800c9e50) */
} St;

/* FUN_1800db950 (l.118671-118763): line to the right of travel by |d| */
static void raw_line(St *st, const Ent *e, Sh *out) {
    double x1 = e->a[0], y1 = e->a[1], x2 = e->a[2], y2 = e->a[3], dx = x2 - x1, dy = y2 - y1, l = sqrt(dy * dy + dx * dx);
    if (!(1e-12 <= l)) return;
    dy /= l; dx /= l;
    double ox = dy * st->dist, oy = -(st->dist * dx);
    st->src[0] = x1; st->src[1] = y1;
    st->prev_end[0] = st->pe[0]; st->prev_end[1] = st->pe[1];
    st->ts[0] = dx; st->ts[1] = dy;
    st->ps[0] = ox + x1; st->ps[1] = oy + y1;
    st->pe[0] = ox + x2; st->pe[1] = oy + y2;
    st->prev_t[0] = st->te[0]; st->prev_t[1] = st->te[1];
    st->te[0] = dx; st->te[1] = dy;
    Ent r; memset(&r, 0, sizeof r);
    r.id = 'L'; r.ce = e->ce | 1; r.src_sh = e->src_sh; r.src_ent = e->src_ent;
    r.a[0] = st->ps[0]; r.a[1] = st->ps[1]; r.a[2] = st->pe[0]; r.a[3] = st->pe[1];
    ent_box(&r);
    sh_push(out, &r);
}

/* FUN_1800dadb0 (l.118311-118506): arc: centre kept, sw > 0 r + |d| else r - |d| */
static void raw_arc(St *st, const Ent *e, Sh *out) {
    double r0 = e->a[2], sw = e->a[4];
    if (!(1e-12 <= fabs(sw) && 1e-12 <= r0)) return;
    double R = sw <= 0.0 ? r0 - st->dist : r0 + st->dist;
    double sa = e->a[3], cx = e->a[0], cy = e->a[1];
    double x0 = cos(sa) * r0 + cx, y0 = sin(sa) * r0 + cy, end = sa + sw;
    double x1 = cos(end) * r0 + cx, y1 = sin(end) * r0 + cy;
    st->src[0] = x0; st->src[1] = y0;
    st->prev_end[0] = st->pe[0]; st->prev_end[1] = st->pe[1];
    st->ps[0] = cos(sa) * R + cx; st->ps[1] = sin(sa) * R + cy;
    st->pe[0] = cos(end) * R + cx; st->pe[1] = sin(end) * R + cy;
    st->prev_t[0] = st->te[0]; st->prev_t[1] = st->te[1];
    /* the radial at the start / end (normalised, source point - centre) turned into the travel tangents */
    double ax = x0 - cx, ay = y0 - cy, al = hypot(ax, ay); if (1e-12 <= al) { ax /= al; ay /= al; }
    double bx = x1 - cx, by = y1 - cy, bl = hypot(bx, by); if (1e-12 <= bl) { bx /= bl; by /= bl; }
    if (sw <= 0.0) { st->ts[0] = ay; st->ts[1] = -ax; st->te[0] = by; st->te[1] = -bx; }
    else { st->ts[0] = -ay; st->ts[1] = ax; st->te[0] = -by; st->te[1] = bx; }
    Ent q; memset(&q, 0, sizeof q);
    q.id = 'A'; q.src_sh = e->src_sh; q.src_ent = e->src_ent; q.a[0] = cx; q.a[1] = cy; q.a[3] = sa; q.a[4] = sw;
    if (-1e-12 <= R) { q.ce = 1; q.a[2] = R < 0.0 ? 0.0 : R; }
    else {
        q.a[2] = fabs(R);
        double g = sa + 3.141592653589793;
        while (3.141592653589793 < g) g += -6.283185307179586;
        while (g < -3.141592653589793) g += 6.283185307179586;
        q.a[3] = g;
        if (0.0001 <= fabs(sw * q.a[2]) && fabs(sw * q.a[2]) != 0.0001) q.ce = 8;
    }
    ent_box(&q);
    sh_push(out, &q);
}


/* ---- entity intersection: the code of viewer/mcce_dyn.c (gp_ent_isect and its callees), which ports mcce::CalcEntIntersect (l.6348-6388),
   FUN_1800a4460, CalcArcLnSegIntersect, CalcLnSegLnSegIntersect, CalcCirCirIntersect, FUN_1800a9000, FUN_1800a4820 and CalcLnLnIntersectPt;
   copied rather than exported so that this file does not touch the Dynamic Mill port */
typedef struct { double x, y; } P2;
static P2 p2(double x, double y) { P2 r = { x, y }; return r; }
static P2 psub(P2 a, P2 b) { return p2(a.x - b.x, a.y - b.y); }
typedef struct { char id; P2 p1, p2_, c; double r, sa, sw; } XE;
static int gp_segseg(double tol, P2 a1, P2 a2, P2 b1, P2 b2, double *s, double *u, P2 *x);
static int gp_lnln(P2 a, P2 da, P2 b, P2 db, double *s, double *u, P2 *x);
/* FUN_1800a9000 (l.92047-92171): circle (c, r) x carrier p + t d. The foot f of the perpendicular from c
   (CalcLnLnIntersectPt with the line c + u (-d.y, d.x), disasm 0x1800a905b-0x1800a90a9; a failure goes to FUN_1800a9490:
   NOT DECODED, ours: none); D = |c - f|^2: inside (r - 1e-14 clamped at 0)^2 or beyond (r + 1e-6)^2 -> two points
   f -+ h d / |d| (none when D > r^2), parameters from the larger |d| component, a point whose distance to c is more than
   1e-6 off r dropped (the second moves first), two parameters within 1e-12 -> one; else (near tangent) one point, t = the
   foot's parameter. FUN_1800a5150 (l.90182-90260) only evaluates angles (no effect). */
static int gp_cirln(P2 c, double r, P2 p, P2 d, double t[2]) {
    t[0] = t[1] = 0.0;
    double s, u; P2 f;
    if (!gp_lnln(p, d, c, p2(-d.y, d.x), &s, &u, &f)) return 0;
    double rr = r - 1e-14, D = (c.y - f.y) * (c.y - f.y) + (c.x - f.x) * (c.x - f.x);
    if (rr < 0.0) rr = 0.0;
    if (!(D <= rr * rr || (r + 1e-06) * (r + 1e-06) <= D)) { t[0] = s; return 1; }
    if (!(D <= r * r)) return 0;
    double h = sqrt(r * r - D), L = sqrt(d.y * d.y + d.x * d.x), ox = d.x * (1.0 / L) * h, oy = d.y * (1.0 / L) * h;
    P2 P[2] = { p2(f.x - ox, f.y - oy), p2(ox + f.x, oy + f.y) };
    if (fabs(d.x) <= fabs(d.y)) { t[0] = (P[0].y - p.y) / d.y; t[1] = (P[1].y - p.y) / d.y; }
    else { t[0] = (P[0].x - p.x) / d.x; t[1] = (P[1].x - p.x) / d.x; }
    int n = 2;
    for (int k = 0; k < n; k++) {
        double q = sqrt((c.y - P[k].y) * (c.y - P[k].y) + (c.x - P[k].x) * (c.x - P[k].x));
        if (fabs(q - r) > 1e-06) { if (k == 0 && n == 2) { P[0] = P[1]; t[0] = t[1]; } n--; k--; }
    }
    if (n == 2 && fabs(t[0] - t[1]) < 1e-12) n = 1;
    return n;
}
/* FUN_1800a4820 (l.89869-89925): the arc parameter of pt (no: pt within 1e-14 of the centre or outside the sweep grown by
   tol / r radians); t = (angle - low end) / |sw|, 1 - that for a clockwise arc; |sw| < 1e-12 -> 0.5 */
static int gp_arc_t(double tol, const XE *a, P2 pt, double *t) {
    if (fabs(pt.x - a->c.x) <= 1e-14 && fabs(pt.y - a->c.y) <= 1e-14) return 0;
    double g = atan2(pt.y - a->c.y, pt.x - a->c.x), hi, lo;
    if (a->sw <= 0.0) { lo = a->sa + a->sw; hi = a->sa; } else { hi = a->sa + a->sw; lo = a->sa; }
    double e = a->r <= 1e-12 ? 0.0 : tol / a->r;
    while (e + hi < g) g = g + -6.283185307179586;
    while (g < lo - e) g = g + 6.283185307179586;
    if (e + hi < g) return 0;
    if (1e-12 <= fabs(a->sw)) { *t = (g - lo) / fabs(a->sw); if (a->sw < 0.0) *t = 1.0 - *t; }
    else *t = 0.5;
    return 1;
}
/* mcce::CalcCirCirIntersect (l.13288-13380): centres d apart (1e-7 <= d <= |r1 + r2| + 1e-7); from the smaller circle's
   centre A towards B: a = (d^2 + r^2 - R^2) / 2d, h^2 = r^2 - a^2 (< -1e-7: none; < 1e-13: the one point M) */
static int gp_circir(P2 c1, double r1, P2 c2, double r2, P2 pt[2]) {
    double dx = c1.x - c2.x, dy = c1.y - c2.y, d = sqrt(dy * dy + dx * dx);
    if (!(d <= fabs(r2 + r1) + 1e-07 && 1e-07 <= d)) return 0;
    P2 A = c1, B = c2; double r = r1, R = r2;
    if (r2 < r1) { A = c2; B = c1; R = r1; r = r2; }
    double ux = B.x - A.x, uy = B.y - A.y, a = ((d * d + r * r) - R * R) / (d * 2.0), L = sqrt(uy * uy + ux * ux);
    if (!(1e-12 <= L)) return 0;
    uy = uy / L; ux = ux / L;
    P2 M = p2(ux * a + A.x, uy * a + A.y);
    double h2 = r * r - a * a;
    if (!(-1e-07 <= h2)) return 0;
    if (1e-13 <= h2) { double h = sqrt(h2); pt[0] = p2(-uy * h + M.x, ux * h + M.y); pt[1] = p2(-uy * -h + M.x, ux * -h + M.y); return 2; }
    pt[0] = M; return 1;
}
/* FUN_1800a46e0 (l.89841-89866): the parameter of pt on the arc's whole circle, no range test: the angle from the start in [0, 2pi]
   ((-2pi, 0] when sw < 0) / sw; no: pt within 1e-14 of the centre or |sw| < 1e-12 */
static int gp_arc_t_ext(const XE *a, P2 pt, double *t) {
    *t = 0.0;
    if (!(1e-14 < fabs(pt.x - a->c.x) || 1e-14 < fabs(pt.y - a->c.y))) return 0;
    double g = atan2(pt.y - a->c.y, pt.x - a->c.x) - a->sa;
    while (6.283185307179586 < g) g += -6.283185307179586;
    while (g < 0.0) g += 6.283185307179586;
    if (a->sw < 0.0) while (0.0 < g) g += -6.283185307179586;
    if (1e-12 <= fabs(a->sw)) { *t = g / a->sw; return 1; }
    return 0;
}
/* the common tail of CalcArcLnSegIntersect (l.8514-8549) and FUN_1800a4460 (l.89800-89835): fl = per-point rejects; two
   kept points closer than tol, or the first one's second parameter outside [0, 1] while ..., collapse to one */
static int gp_isect_tail(double tol, int n, const char fl[2], double t1[2], double t2[2], P2 pt[2]) {
    if (n == 2 && !fl[0]) {
        if (!fl[1]) {
            if (tol * tol <= (pt[0].y - pt[1].y) * (pt[0].y - pt[1].y) + (pt[0].x - pt[1].x) * (pt[0].x - pt[1].x)) return 2;
            if (t2[0] < 0.0 || 1.0 < t2[0] || (0.0 <= t2[1] && t2[1] <= 1.0 && (t1[0] < 0.0 || 1.0 < t1[0]))) goto shift;
        }
        n--;
    } else if (fl[1]) n--;
    if (!fl[0]) return n;
shift:
    if (n - 1 != 0) { pt[0] = pt[1]; t1[0] = t1[1]; t2[0] = t2[1]; }
    return n - 1;
}
/* mcce::CalcArcLnSegIntersect (l.8395-8551) with bool false: FUN_1800a9000 on the line's carrier; per point pt = p + t d;
   kept when (t >= 0 or within tol of p1) and (t <= 1 or within tol of p2), FUN_1800a4820 gives its arc parameter and the
   arc point there is within 1e-6 (squared 1e-12) */
static int gp_arcln(double tol, const XE *arc, const XE *ln, double tA[2], double tL[2], P2 pt[2], int ext) {
    P2 p = ln->p1, d = p2(ln->p2_.x - ln->p1.x, ln->p2_.y - ln->p1.y);
    char fl[2] = { 0, 0 };
    int n = gp_cirln(arc->c, arc->r, p, d, tL);
    for (int k = 0; k < n; k++) {
        pt[k] = p2(d.x * tL[k] + p.x, d.y * tL[k] + p.y);
        /* param_7 true (ext, l.8481-8482): only FUN_1800a46e0, the line's range is not tested */
        int ok = ext ? gp_arc_t_ext(arc, pt[k], &tA[k]) :
                 (0.0 <= tL[k] || (pt[k].y - p.y) * (pt[k].y - p.y) + (pt[k].x - p.x) * (pt[k].x - p.x) <= tol * tol)
              && (tL[k] <= 1.0 || (pt[k].y - ln->p2_.y) * (pt[k].y - ln->p2_.y) + (pt[k].x - ln->p2_.x) * (pt[k].x - ln->p2_.x) <= tol * tol)
              && gp_arc_t(tol, arc, pt[k], &tA[k]);
        if (ok) {
            double g = arc->sw * tA[k] + arc->sa, ex = (cos(g) * arc->r + arc->c.x) - pt[k].x, ey = (sin(g) * arc->r + arc->c.y) - pt[k].y;
            if (1e-12 < ey * ey + ex * ex) ok = 0;
        }
        if (!ok) fl[k] = 1;
    }
    return gp_isect_tail(tol, n, fl, tA, tL, pt);
}
/* FUN_1800a4460 (l.89743-89836) with param_7 0: CalcCirCirIntersect, each point needs FUN_1800a4820 on both arcs */
static int gp_arcarc(double tol, const XE *a, const XE *b, double ta[2], double tb[2], P2 pt[2], int ext) {
    char fl[2] = { 0, 0 };
    int n = gp_circir(a->c, a->r, b->c, b->r, pt);
    for (int k = 0; k < n; k++) {
        if (ext) { if (!gp_arc_t_ext(a, pt[k], &ta[k]) || !gp_arc_t_ext(b, pt[k], &tb[k])) fl[k] = 1; }   /* param_7 = 1 (l.89792-89796) */
        else if (!gp_arc_t(tol, a, pt[k], &ta[k]) || !gp_arc_t(tol, b, pt[k], &tb[k])) fl[k] = 1;
    }
    return gp_isect_tail(tol, n, fl, ta, tb, pt);
}
/* mcce::CalcEntIntersect (l.6348-6388): 'A' 'A' FUN_1800a4460; 'L' 'A' / 'A' 'L' CalcArcLnSegIntersect (arc first);
   'L' 'L' CalcLnSegLnSegIntersect; other pairs none */
static int gp_ent_isect(double tol, const XE *a, const XE *b, double ta[2], double tb[2], P2 pt[2]) {
    if (a->id == 'A' && b->id == 'A') return gp_arcarc(tol, a, b, ta, tb, pt, 0);
    if (a->id == 'L' && b->id == 'A') return gp_arcln(tol, b, a, tb, ta, pt, 0);
    if (a->id == 'A' && b->id == 'L') return gp_arcln(tol, a, b, ta, tb, pt, 0);
    if (a->id == 'L' && b->id == 'L') return gp_segseg(tol, a->p1, a->p2_, b->p1, b->p2_, &ta[0], &tb[0], &pt[0]);
    return 0;
}
/* FUN_1800aad10 (l.92865-92922): the entities' intersection on the carriers (param_7 true): 'A' 'A' FUN_1800a4460; 'L' 'A' / 'A' 'L' CalcArcLnSegIntersect;
   'L' 'L' CalcLnLnIntersectPt of the two carrier lines (no range test, one point) */
static int gp_ent_isect_ext(double tol, const XE *a, const XE *b, double ta[2], double tb[2], P2 pt[2]) {
    if (a->id == 'A' && b->id == 'A') return gp_arcarc(tol, a, b, ta, tb, pt, 1);
    if (a->id == 'L' && b->id == 'A') return gp_arcln(tol, b, a, tb, ta, pt, 1);
    if (a->id == 'A' && b->id == 'L') return gp_arcln(tol, a, b, ta, tb, pt, 1);
    if (a->id == 'L' && b->id == 'L') return gp_lnln(a->p1, psub(a->p2_, a->p1), b->p1, psub(b->p2_, b->p1), &ta[0], &tb[0], &pt[0]);
    return 0;
}
/* FUN_1801066f0 (l.142002-142106): the one-line shape X crossed with the entities of the band nearest to its start y:
   CalcEntIntersect(1e-5) per entity, only its first hit kept {pt, X (0, 0, t), Y (shape, entity, t)}; CRT qsort by the
   X position (LAB_1800074dc = isab_cmp_x); FUN_1801339b0 (l.165443-165490) erases a record equal to its cyclic
   predecessor (same X and Y shapes, FUN_180134800); FUN_1801349c0 links the rings */
/* mcce::CalcLnLnIntersectPt (l.3611-3640) on the carriers (a, da), (b, db), in its own operation order: parallel when
   |den| < 1e-14; the point is a + s da; true only when b + u db lies within 1e-6 of it (squared 1e-12) */
static int gp_lnln(P2 a, P2 da, P2 b, P2 db, double *s, double *u, P2 *x) {
    double den = -(db.y * da.x) - -(db.x * da.y);
    if (fabs(den) < 1e-14) return 0;
    *s = (-((b.x - a.x) * db.y) - -((b.y - a.y) * db.x)) / den;
    *x = p2(*s * da.x + a.x, *s * da.y + a.y);
    *u = ((b.y - a.y) * da.x - (b.x - a.x) * da.y) / den;
    double ey = (*u * db.y + b.y) - x->y, ex = (*u * db.x + b.x) - x->x;
    return ey * ey + ex * ex <= 1e-12;
}
/* mcce::CalcLnSegLnSegIntersect (l.12820-12897): gp_lnln of the segments a1 a2 / b1 b2 and no parameter outside [0, 1]
   farther than tol from the nearer end; s / u are returned unclamped */
static int gp_segseg(double tol, P2 a1, P2 a2, P2 b1, P2 b2, double *s, double *u, P2 *x) {
    if (!gp_lnln(a1, psub(a2, a1), b1, psub(b2, b1), s, u, x)) return 0;
    double t2 = tol * tol;
#define GP_D2(p, q) (((p).y - (q).y) * ((p).y - (q).y) + ((p).x - (q).x) * ((p).x - (q).x))
    if ((*s < 0.0 && t2 < GP_D2(*x, a1)) || (1.0 < *s && t2 < GP_D2(*x, a2)) || (*u < 0.0 && t2 < GP_D2(*x, b1)) || (1.0 < *u && t2 < GP_D2(*x, b2))) return 0;
#undef GP_D2
    return 1;
}
static XE to_xe(const Ent *e) {
    XE x; memset(&x, 0, sizeof x); x.id = e->id;
    if (e->id == 'A') { x.c = p2(e->a[0], e->a[1]); x.r = e->a[2]; x.sa = e->a[3]; x.sw = e->a[4]; }
    else { x.p1 = p2(e->a[0], e->a[1]); x.p2_ = p2(e->a[2], e->a[3]); }
    return x;
}
static int ent_isect(double tol, const Ent *a, const Ent *b, double ta[2], double tb[2], double pt[4]) {
    XE x = to_xe(a), y = to_xe(b); P2 p[2] = { { 0, 0 }, { 0, 0 } };
    int n = gp_ent_isect(tol, &x, &y, ta, tb, p);
    pt[0] = p[0].x; pt[1] = p[0].y; pt[2] = p[1].x; pt[3] = p[1].y;
    return n;
}

/* ---- gap closing: FUN_18012c740 (l.161726-161766) and its callees ---- */
/* FUN_180021580 (l.23115-23152): the point at parameter t on an entity */
static P2 ent_at(const Ent *e, double t) {
    if (e->id == 'L') return p2((e->a[2] - e->a[0]) * t + e->a[0], (e->a[3] - e->a[1]) * t + e->a[1]);
    if (e->id == 'A') { double g = t * e->a[4] + e->a[3]; return p2(cos(g) * e->a[2] + e->a[0], sin(g) * e->a[2] + e->a[1]); }
    return p2(0.0, 0.0);
}
static P2 ent_end(const Ent *e) { double x, y; e_end(e, &x, &y); return p2(x, y); }
static P2 ent_start(const Ent *e) { double x, y; e_start(e, &x, &y); return p2(x, y); }
/* mcce::GetNearestEntPt (l.8324-8364), FUN_1800ad500 (l.94201-94241), mcce::GetNearestArcPt (l.6066-6128): the nearest point of the entity */
static P2 ent_nearest_t(const Ent *e, P2 q, double *pt) {
    double dt = 0.0;
    if (!pt) pt = &dt;
    if (e->id == 'L') {
        double x = e->a[0], y = e->a[1], dx = e->a[2] - x, dy = e->a[3] - y, l2 = dy * dy + dx * dx;
        if (1e-14 <= l2) {
            double u = -(((y - q.y) * dy + (x - q.x) * dx) / l2);
            if (u < 0.0) u = 0.0; else if (1.0 < u) u = 1.0;
            *pt = u;
            return p2(u * dx + x, u * dy + e->a[1]);
        }
        *pt = 0.5;
        return p2((x + e->a[2]) * 0.5, (y + e->a[3]) * 0.5);
    }
    if (e->id != 'A') { *pt = 0.0; return p2(0.0, 0.0); }
    double sw = e->a[4], g;
    if (1.1920928955078125e-07 <= fabs(q.x - e->a[0]) || 1.1920928955078125e-07 <= fabs(q.y - e->a[1])) {
        g = atan2(q.y - e->a[1], q.x - e->a[0]) - e->a[3];
        while (6.283185307179586 < g) g += -6.283185307179586;
        while (g < 0.0) g += 6.283185307179586;
        if (sw < 0.0) while (0.0 < g) g += -6.283185307179586;
        if (fabs(sw) <= fabs(g) && fabs(g) != fabs(sw)) {
            double h = 0.0 <= sw ? sw * 0.5 + 3.141592653589793 : sw * 0.5 - 3.141592653589793;
            g = (fabs(h) <= fabs(g) && fabs(g) != fabs(h)) ? 0.0 : sw;
        }
    } else g = sw * 0.5;
    *pt = fabs(sw) <= 1e-12 ? 0.0 : g / sw;
    return p2(cos(g + e->a[3]) * e->a[2] + e->a[0], sin(g + e->a[3]) * e->a[2] + e->a[1]);
}
static P2 ent_nearest(const Ent *e, P2 q) { return ent_nearest_t(e, q, NULL); }
/* FUN_18013d280 (l.170481-170586): the nearest point over the entities (the box pre-test only skips entities that cannot win) */
static P2 near_ents(P2 q, const Ent *const *ents, int n) {
    P2 best = p2(0.0, 0.0);
    double bd = 0.0;
    int have = 0;
    for (int i = 0; i < n; i++) {
        P2 r = ent_nearest(ents[i], q);
        double d = (q.y - r.y) * (q.y - r.y) + (q.x - r.x) * (q.x - r.x);
        if (!have || d <= bd) { best = r; bd = d; have = 1; }
    }
    return best;
}
/* FUN_18013c4f0 (l.169903-170077) with param_4 = 3: both ends and then, at 3 levels of halving, the points of the piece are within tol of the entities */
static int piece_near(const Ent *piece, const Ent *const *ents, int n, double tol) {
    P2 q = ent_start(piece), r = near_ents(q, ents, n);
    if (tol * tol < (q.y - r.y) * (q.y - r.y) + (q.x - r.x) * (q.x - r.x)) return 0;
    q = ent_end(piece); r = near_ents(q, ents, n);
    if (tol * tol < (q.y - r.y) * (q.y - r.y) + (q.x - r.x) * (q.x - r.x)) return 0;
    int m = 1;
    for (int lev = 0; lev < 3; lev++, m *= 2)
        for (int k = 0, c = 1; k < m; k++, c += 2) {
            q = ent_at(piece, (double)c * (1.0 / (double)(m * 2)));
            r = near_ents(q, ents, n);
            if (tol * tol < (q.y - r.y) * (q.y - r.y) + (q.x - r.x) * (q.x - r.x)) return 0;
        }
    return 1;
}
/* FUN_18012ac00 (l.160786-161062): the extensions an intersection beyond an entity's end would add stay within max(2 gap, 1e-5) of the other entity */
static int ext_stays_near(double gap, const Ent *pv, const Ent *cu, double t1, double t2) {
    double tol = gap * 2.0 <= 1e-05 ? 1e-05 : gap * 2.0;
    int ok1 = 1, ok2 = 1;
    if (1.0 < t1) {
        Ent x = *pv;
        if (pv->id == 'L') {
            double dx = pv->a[2] - pv->a[0], dy = pv->a[3] - pv->a[1];
            x.a[0] = pv->a[0] + dx; x.a[1] = dy + pv->a[1];
            x.a[2] = dx * t1 + pv->a[0]; x.a[3] = dy * t1 + pv->a[1];
        } else if (pv->id == 'A') {
            x.a[4] = (t1 - 1.0) * pv->a[4]; x.a[3] = pv->a[4] + pv->a[3];
        }
        ok1 = piece_near(&x, &cu, 1, tol);
    }
    if (t2 < 0.0 || (cu->id == 'A' && 1.0 < t2)) {
        double t = t2;
        if (cu->id == 'A' && 1.0 < t) t = -fabs((6.283185307179586 - fabs(cu->a[4] * t)) / cu->a[4]);
        Ent x = *cu;
        if (cu->id == 'L') {
            double dx = cu->a[2] - cu->a[0], dy = cu->a[3] - cu->a[1];
            x.a[0] = dx * t + cu->a[0]; x.a[1] = dy * t + cu->a[1];
            x.a[2] = dx * 0.0 + cu->a[0]; x.a[3] = dy * 0.0 + cu->a[1];
        } else if (cu->id == 'A') {
            x.a[4] = (0.0 - t) * cu->a[4]; x.a[3] = cu->a[4] * t + cu->a[3];
        }
        ok2 = piece_near(&x, &pv, 1, tol);
    }
    return ok1 && ok2;
}
/* FUN_18012bcd0 (l.161428-161556): which intersection point to use (the one nearer the gap's midpoint); -1: the intersection is beyond a line's far end, or
   beyond an entity and more than 1.5 gap from the midpoint, or FUN_18012ac00 rejects it */
static int isect_choice(double gap, const Ent *pv, const Ent *cu, int n, const double t1[2], const double t2[2], const P2 pts[2]) {
    if (!n) return -1;
    P2 a = ent_end(pv), b = ent_start(cu);
    double mx = (b.x + a.x) * 0.5, my = (b.y + a.y) * 0.5;
    int k = n == 1 ? 0 : ((my - pts[1].y) * (my - pts[1].y) + (mx - pts[1].x) * (mx - pts[1].x) <= (mx - pts[0].x) * (mx - pts[0].x) + (my - pts[0].y) * (my - pts[0].y));
    if ((pv->id != 'L' || (0.0 < t1[k] || t1[k] == 0.0)) && (cu->id != 'L' || t2[k] <= 1.0)) {
        if (t1[k] < 0.0 || 1.0 < t1[k] || t2[k] < 0.0 || 1.0 < t2[k]) {
            double dx = a.x - b.x, dy = a.y - b.y, ey = my - pts[k].y, ex = mx - pts[k].x;
            if (sqrt(dy * dy + dx * dx) * 0.5 * 3.0 < sqrt(ey * ey + ex * ex)) return -1;
        }
        if (ext_stays_near(gap, pv, cu, t1[k], t2[k])) return k;
    }
    return -1;
}
/* FUN_18012b2b0 (l.161065-161098) + FUN_1800ad400 (l.94147-94196): the point at t on the entity lies within 1e-5 of the line (p, d) */
static int on_line(P2 p, P2 d, const Ent *e, double t) {
    P2 q = ent_at(e, t), f; double s, u;
    if (!gp_lnln(p, d, q, p2(d.y, -d.x), &s, &u, &f)) return 0;
    return sqrt((q.y - f.y) * (q.y - f.y) + (q.x - f.x) * (q.x - f.x)) <= 1e-05;
}
/* FUN_18012b390 (l.161105-161421): a joint not closed by an intersection inside both entities: if the entities are nearly in line at the joint, the ends are snapped:
   line - line the midpoint on both, line - arc the line's end moves to the arc's. Two arcs: never. */
static int snap_collinear(Ent *pv, Ent *cu, const double t1[2], const double t2[2], int sel) {
    if (pv->id == 'A' && cu->id == 'A') return 0;
    if (-1 < sel && 0.0 <= t1[sel] && t1[sel] <= 1.0 && 0.0 <= t2[sel] && t2[sel] <= 1.0) return 0;
    P2 p, v;
    if (pv->id == 'A' || cu->id == 'A') {
        const Ent *a = pv->id == 'A' ? pv : cu;
        p = pv->id == 'A' ? ent_end(pv) : ent_start(cu);
        double g = pv->id == 'A' ? a->a[4] + a->a[3] : a->a[4] * 0.0 + a->a[3];
        double rx = (cos(g) * a->a[2] + a->a[0]) - a->a[0], ry = (sin(g) * a->a[2] + a->a[1]) - a->a[1], sw = a->a[4];
        double vx = rx * fabs(sw), vy = ry * fabs(sw);
        if (sw <= 0.0) { if (0.0 <= sw) v = p2(0.0, 0.0); else v = p2(vy, -vx); }
        else v = p2(-vy, vx);
    } else {
        p = p2(pv->a[0], pv->a[1]);
        P2 ce = ent_end(cu);
        v = p2(ce.x - p.x, ce.y - p.y);
    }
    if (sqrt(v.x * v.x + v.y * v.y) < 1e-08) return 0;
    int near;
    if (pv->id == 'A') near = on_line(p, v, cu, 0.0);
    else if (cu->id == 'A') near = on_line(p, v, pv, 1.0);
    else near = on_line(p, v, pv, 1.0) && on_line(p, v, cu, 0.0);
    if (!near) return 0;
    if (pv->id != 'A') {
        if (cu->id != 'A') {
            P2 m = p2((cu->a[0] + pv->a[2]) * 0.5, (cu->a[1] + pv->a[3]) * 0.5);
            pv->a[2] = m.x; pv->a[3] = m.y; cu->a[0] = m.x; cu->a[1] = m.y;
            ent_box(pv); ent_box(cu);
        } else {
            P2 q = ent_start(cu);
            pv->a[2] = q.x; pv->a[3] = q.y;
            ent_box(pv);
        }
    } else {
        P2 q = ent_end(pv);
        cu->a[0] = q.x; cu->a[1] = q.y;
        ent_box(cu);
    }
    return 1;
}
/* trim the end of pv at t / the start of cu at t (l.160483-160569, l.160682-160769): the extension parameter of an arc t > 1 becomes the matching backward parameter */
static void trim_end(Ent *e, double t) {
    if (e->id == 'L') { e->a[2] = (e->a[2] - e->a[0]) * t + e->a[0]; e->a[3] = (e->a[3] - e->a[1]) * t + e->a[1]; }
    else if (e->id == 'A') e->a[4] *= t;
    ent_box(e);
}
static void trim_start(Ent *e, double t) {
    if (e->id == 'A' && 1.0 < t) t = -fabs((6.283185307179586 - fabs(e->a[4] * t)) / e->a[4]);
    if (e->id == 'L') { e->a[0] += (e->a[2] - e->a[0]) * t; e->a[1] += (e->a[3] - e->a[1]) * t; }
    else if (e->id == 'A') { double s = e->a[4] * t; e->a[3] += s; e->a[4] -= s; }
    ent_box(e);
}
/* FUN_18012a1b0 (l.160388-160573): one joint with a gap >= 1e-10: the intersection of the two entities, else the collinear snap */
static void gap_trim(double thr, Sh *s, int ip, int ic) {
    Ent *pv = &s->e[ip], *cu = &s->e[ic];
    P2 a = ent_end(pv), b = ent_start(cu);
    double gap = sqrt((b.y - a.y) * (b.y - a.y) + (b.x - a.x) * (b.x - a.x));
    if (!(thr <= gap)) return;
    double t1[2] = { 0, 0 }, t2[2] = { 0, 0 }; P2 pts[2] = { { 0, 0 }, { 0, 0 } };
    XE x = to_xe(pv), y = to_xe(cu);
    int n = gp_ent_isect_ext(1e-06, &x, &y, t1, t2, pts);
    int sel = isect_choice(gap, pv, cu, n, t1, t2, pts);
    if (!snap_collinear(pv, cu, t1, t2, sel) && -1 < sel) { trim_end(pv, t1[sel]); trim_start(cu, t2[sel]); }
}
/* FUN_180129530 (l.160037-160381): where the link line of a remaining gap starts and ends: first at the very ends (t = 1, 0), then stepping back from the joint by max(gap, 1e-5)
   per step (at most 5 steps, 0.1 of the entity at most) while the chord stays on the two entities (FUN_18013c4f0 tol, 3) and the sum of the end cosines does not worsen; stops when both
   cosines exceed cos 10 deg */
static void link_ends(double gap, const Ent *pv, const Ent *cu, double *pt1, double *pt2) {
    double tol = gap <= 1e-05 ? 1e-05 : gap;
    double c10 = cos(0.17453292519943295);
    const Ent *pair[2]; pair[0] = pv; pair[1] = cu;
    double l1 = pv->id == 'L' ? sqrt((pv->a[3] - pv->a[1]) * (pv->a[3] - pv->a[1]) + (pv->a[2] - pv->a[0]) * (pv->a[2] - pv->a[0])) : fabs(pv->a[2] * pv->a[4]);
    double l2 = cu->id == 'L' ? sqrt((cu->a[3] - cu->a[1]) * (cu->a[3] - cu->a[1]) + (cu->a[2] - cu->a[0]) * (cu->a[2] - cu->a[0])) : fabs(cu->a[2] * cu->a[4]);
    double st1 = 0.1, st2 = 0.1;
    if (1e-07 < l1) { st1 = tol / l1; if (0.1 < st1) st1 = 0.1; }
    if (1e-07 < l2) { st2 = tol / l2; if (0.1 < st2) st2 = 0.1; }
    double c1p = 0.0, c2p = c10;
    for (int i = 0; ; ) {
        double t1 = 1.0 - (double)i * st1, t2 = (double)i * st2;
        P2 a = ent_at(pv, t1), b = ent_at(cu, t2);
        double cx = b.x - a.x, cy = b.y - a.y, cl = sqrt(cy * cy + cx * cx);
        if (1e-12 <= cl) { cx /= cl; cy /= cl; }
        double tx, ty, ux, uy;
        if (pv->id == 'L') { tx = pv->a[2] - pv->a[0]; ty = pv->a[3] - pv->a[1]; }
        else {
            double sw = pv->a[4], g = sw * t1 + pv->a[3], rx = (cos(g) * pv->a[2] + pv->a[0]) - pv->a[0], ry = (sin(g) * pv->a[2] + pv->a[1]) - pv->a[1];
            ty = rx * fabs(sw); tx = ry * fabs(sw);
            if (sw <= 0.0) { if (0.0 <= sw) tx = ty = 0.0; else ty = -ty; } else tx = -tx;
        }
        double tl = sqrt(ty * ty + tx * tx);
        if (1e-12 <= tl) { tx /= tl; ty /= tl; }
        if (cu->id == 'L') { ux = cu->a[2] - cu->a[0]; uy = cu->a[3] - cu->a[1]; }
        else {
            double sw = cu->a[4], g = sw * t2 + cu->a[3], rx = (cos(g) * cu->a[2] + cu->a[0]) - cu->a[0], ry = (sin(g) * cu->a[2] + cu->a[1]) - cu->a[1];
            uy = rx * fabs(sw); ux = ry * fabs(sw);
            if (sw <= 0.0) { if (0.0 <= sw) ux = uy = 0.0; else uy = -uy; } else ux = -ux;
        }
        double ul = sqrt(uy * uy + ux * ux);
        if (1e-12 <= ul) { ux /= ul; uy /= ul; }
        double c1 = ty * cy + tx * cx, c2 = uy * cy + ux * cx;
        if (i != 0) {
            Ent ln; memset(&ln, 0, sizeof ln);
            ln.id = 'L'; ln.a[0] = a.x; ln.a[1] = a.y; ln.a[2] = b.x; ln.a[3] = b.y;
            if (!piece_near(&ln, pair, 2, tol) || cl < 1e-12 || tl < 1e-12 || ul < 1e-12 || c2 + c1 < c2p + c1p) return;
        }
        *pt1 = t1; *pt2 = t2;
        c1p = c1;
        if ((c10 < c1 && c10 < c2) || (i = i + 1, c2p = c2, 5 < i)) return;
    }
}
/* FUN_18012a720 (l.160580-160778): a joint still open by >= param_1 gets a link line, ce 0x10, no source link, inserted before the current entity */
static void gap_link(double thr, Sh *s, int ip, int *pic) {
    int ic = *pic;
    Ent *pv = &s->e[ip], *cu = &s->e[ic];
    P2 a = ent_end(pv), b = ent_start(cu);
    double gap = sqrt((b.y - a.y) * (b.y - a.y) + (b.x - a.x) * (b.x - a.x));
    if (!(thr <= gap)) return;
    double t1 = 0, t2 = 0;
    link_ends(gap, pv, cu, &t1, &t2);
    P2 q0 = ent_at(pv, t1), q1 = ent_at(cu, t2);
    Ent ln; memset(&ln, 0, sizeof ln);
    ln.id = 'L'; ln.ce = 0x10; ln.src_sh = ln.src_ent = -1;
    ln.a[0] = q0.x; ln.a[1] = q0.y; ln.a[2] = q1.x; ln.a[3] = q1.y;
    ent_box(&ln);
    trim_end(pv, t1);
    trim_start(cu, t2);
    sh_insert(s, ic, &ln);
    *pic = ic + 1;
}
/* FUN_18012c740 (l.161726-161766): the joints of the shape in order; thr_link is 1e-10 when Infinite look ahead is on, 1e-7 when off (FUN_1800da1c0 l.118012-118019) */
static void close_gaps(Sh *s, double thr_link) {
    if (!s->n) return;
    int prev = 0, cur_ref = 1, cur;
    if (s->dir != 0) { prev = s->n - 1; cur_ref = 0; }
    cur = s->dir == 0;
    while (cur < s->n) {
        gap_trim(1e-10, s, prev, cur);
        gap_link(thr_link, s, prev, &cur_ref);
        cur = cur_ref + 1;
        prev = cur_ref;
        cur_ref = cur;
    }
}

/* FUN_1800cb410 (l.109705-109912): one intersection of the previous and the current entity (CalcEntIntersect tol 0): param_1 = trim both there */
static int corner_trim(int trim, int k, Sh *o) {
    int prev = k - 1, cur = k == o->n ? 0 : k;
    double ta[2], tb[2], pt[4];
    int n = ent_isect(0.0, &o->e[prev], &o->e[cur], ta, tb, pt);
    if (n != 1) return 0;
    if (trim) {
        Ent *P = &o->e[prev], *C = &o->e[cur];
        if (P->id == 'L') { P->a[2] = P->a[0] + (P->a[2] - P->a[0]) * ta[0]; P->a[3] = P->a[1] + (P->a[3] - P->a[1]) * ta[0]; }
        else if (P->id == 'A') P->a[4] *= ta[0];
        if (C->id == 'L') { C->a[0] = C->a[0] + (C->a[2] - C->a[0]) * tb[0]; C->a[1] = C->a[1] + (C->a[3] - C->a[1]) * tb[0]; }
        else if (C->id == 'A') { C->a[3] += C->a[4] * tb[0]; C->a[4] -= C->a[4] * tb[0]; }
        ent_box(P); ent_box(C);
    }
    return 1;
}
/* FUN_1800ca220 (l.109073-109143): the corner arc about the source point, radius |d|, from the previous end to the current start; sweep made positive */
static void corner_arc(const St *st, int k, Sh *o) {
    double sa = atan2(st->prev_end[1] - st->src[1], st->prev_end[0] - st->src[0]);
    double ea = atan2(st->ps[1] - st->src[1], st->ps[0] - st->src[0]), sw = ea - sa;
    if (-3.141592653589793 <= sw) { if (3.141592653589793 < sw) sw -= 6.283185307179586; }
    else sw += 6.283185307179586;
    if (sw < 0.0) sw += 6.283185307179586;
    Ent e; memset(&e, 0, sizeof e);
    e.id = 'A'; e.ce = 2; e.src_sh = e.src_ent = -1;
    e.a[0] = st->src[0]; e.a[1] = st->src[1]; e.a[2] = st->dist; e.a[3] = sa; e.a[4] = sw;
    ent_box(&e);
    sh_insert(o, k, &e);
}
/* FUN_1800c9c60 (l.108881-108915): a chord line from the previous end to the current start (ce 4) */
static void corner_chord(const St *st, int k, Sh *o) {
    Ent e; memset(&e, 0, sizeof e);
    e.id = 'L'; e.ce = 4; e.src_sh = e.src_ent = -1;
    e.a[0] = st->prev_end[0]; e.a[1] = st->prev_end[1]; e.a[2] = st->ps[0]; e.a[3] = st->ps[1];
    ent_box(&e);
    sh_insert(o, k, &e);
}
/* mcce::CalcLnLnIntersectPt (l.3611-3650): (p, d) lines; t on the first, u on the second */
static int ln_ln(const double *p1, const double *p2, double *t, double *u, double *x) {
    double det = -(p2[3] * p1[2]) - -(p2[2] * p1[3]);
    if (fabs(det) < 1e-14) return 0;
    double dt = (-((p2[0] - p1[0]) * p2[3]) - -((p2[1] - p1[1]) * p2[2])) / det;
    *t = dt;
    x[0] = dt * p1[2] + p1[0]; x[1] = dt * p1[3] + p1[1];
    double du = ((p2[1] - p1[1]) * p1[2] - (p2[0] - p1[0]) * p1[3]) / det;
    *u = du;
    double ex = (du * p2[2] + p2[0]) - x[0], ey = (du * p2[3] + p2[1]) - x[1];
    return ey * ey + ex * ex <= 1e-12;
}
/* FUN_1800ca460 (l.109144-109262): the mitre: the tangents at the previous end and the current start (radial turned 90 deg) meet ahead of the
   first and behind the second -> two lines (ce 2) through the point; FUN_1800c9e50's record for FUN_1800d62d0 is not kept here */
/* FUN_1800c9e50 (l.108953-109070): the record of an accepted mitre, read by FUN_1800c8920 to tell whether a point lies in the mitred corner; none when the chord or the mitre point's height is below 3e-5 / 2e-5 */
static void miter_record(St *st, const double *x) {
    double px = st->ps[0], py = st->ps[1], ex = st->prev_end[0], ey = st->prev_end[1];
    Mitre m;
    m.box[0] = fmin(fmin(ex, px), x[0]); m.box[1] = fmin(fmin(ey, py), x[1]); m.box[2] = fmax(fmax(ex, px), x[0]); m.box[3] = fmax(fmax(ey, py), x[1]);
    double dx = ex - px, dy = ey - py, l = sqrt(dx * dx + dy * dy);
    if (1e-12 <= l) {
        double ux = dx / l, uy = dy / l;
        P2 p = p2(x[0], x[1]), foot; double s, u;
        /* CalcLnLnIntersectPt((x, (uy, -ux)), (ps, (ux, uy)), &foot) */
        if (gp_lnln(p, p2(uy, -ux), p2(px, py), p2(ux, uy), &s, &u, &foot)) {
            double d2 = (py - ey) * (py - ey) + (px - ex) * (px - ex), f2 = (x[1] - foot.y) * (x[1] - foot.y) + (x[0] - foot.x) * (x[0] - foot.x);
            if (9.000000000000002e-10 <= d2 && 4.0000000000000007e-10 <= f2) {
                m.c[0] = x[0] + uy * 1e-05; m.c[1] = x[1] + -ux * 1e-05;
                m.a[0] = px + ux * 1e-05; m.a[1] = py + uy * 1e-05;
                m.b[0] = ex - ux * 1e-05; m.b[1] = ey - uy * 1e-05;
                if (st->nmit == st->capmit) { st->capmit = st->capmit ? 2 * st->capmit : 8; st->mit = realloc(st->mit, (size_t)st->capmit * sizeof(Mitre)); }
                st->mit[st->nmit++] = m;
            }
        }
    }
}
static int corner_mitre(St *st, int k, Sh *o) {
    double L1[4] = { st->prev_end[0], st->prev_end[1], -(st->prev_end[1] - st->src[1]), st->prev_end[0] - st->src[0] };
    double L2[4] = { st->ps[0], st->ps[1], -(st->ps[1] - st->src[1]), st->ps[0] - st->src[0] };
    double t, u, x[2];
    if (!ln_ln(L1, L2, &t, &u, x)) return 0;
    double dv, dw;
    if (fabs(L1[2]) <= fabs(L1[3])) dv = 1e-12 <= fabs(L1[3]) ? (x[1] - L1[1]) / L1[3] : 0.0;
    else dv = 1e-12 <= fabs(L1[2]) ? (x[0] - L1[0]) / L1[2] : 0.0;
    if (fabs(L2[2]) <= fabs(L2[3])) dw = 1e-12 <= fabs(L2[3]) ? (x[1] - L2[1]) / L2[3] : 0.0;
    else dw = 1e-12 <= fabs(L2[2]) ? (x[0] - L2[0]) / L2[2] : 0.0;
    if (!(0.0 <= dv && dw <= 0.0)) return 0;
    miter_record(st, x);   /* l.109222 */
    Ent e; memset(&e, 0, sizeof e);
    e.id = 'L'; e.ce = 2; e.src_sh = e.src_ent = -1;
    e.a[0] = st->prev_end[0]; e.a[1] = st->prev_end[1]; e.a[2] = x[0]; e.a[3] = x[1];
    ent_box(&e);
    sh_insert(o, k, &e);
    e.a[0] = x[0]; e.a[1] = x[1]; e.a[2] = st->ps[0]; e.a[3] = st->ps[1];
    ent_box(&e);
    sh_insert(o, k + 1, &e);
    return 1;
}

/* FUN_1800ca830 + FUN_1800c9d70 (l.109263-109333 / 108916-108948; return values from the disassembly 0x1800ca830-0x1800caa3b):
   the join of the previous offset entity and the one just added at index k. */
/* FUN_1800cb710 (l.109917-110120) -> FUN_1800caac0 (l.109339-109702), the walk only: from the corner outwards, previous entity backwards and current entity forwards, while each pair
   is the mirror image of the other (start of the previous = end of the current, and the t = 0.5 points equal, squared distance < 1e-10; FUN_180021580). A walk that meets without a difference
   (a retraced chain, FUN_1800caac0 l.109636-109675) returns 0 from FUN_1800caac0 and FUN_1800cb710 returns 1: the arc of FUN_1800c9d70. The rest of FUN_1800cb710 (a difference found: feet FUN_18013d200, FUN_18014a290) is NOT PORTED: refused. */
/* FUN_1800caac0 (l.109339-109702): from the corner at entity i outwards, previous entity backwards and current entity forwards, while each pair is the mirror image of the other (start of the previous = end of the current, and the t = 0.5 points equal, squared distance < 1e-10; FUN_180021580). A difference: the end of the pair that lies on the other's entity (FUN_18013d200: the foot, within 1e-5) gives the section's limits: (*e2, *t2) on the previous side, (*e1, *t1) on the current side; returns 1. A walk that meets (a retraced chain, l.109636-109675) returns 0, except that the last entity, a whole circle (> 355 degrees) of real size, returns 1 with t2 = 1, t1 = 0 */
static int sec_count(const Sh *s, int e1, double t1, int e2, double t2);
static void sec_append(const Sh *s, int e1, double t1, int e2, double t2, Sh *out);
static int reversal_walk(const Sh *src, int i, int *e2, double *t2, int *e1, double *t1) {
    int n = src->n, cur = i, prev = (i == 0 ? n : i) - 1;
    *e2 = prev; *e1 = i; *t2 = 0.0; *t1 = 0.0;
    do {
        *e2 = prev; *e1 = cur;
        P2 a = ent_start(&src->e[prev]), b = ent_end(&src->e[cur]);
        P2 m1 = ent_at(&src->e[prev], 0.5), m2 = ent_at(&src->e[cur], 0.5);
        if (1.0000000000000002e-10 <= (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)) {
            double t;
            P2 f = ent_nearest_t(&src->e[cur], a, &t);
            if ((f.y - a.y) * (f.y - a.y) + (f.x - a.x) * (f.x - a.x) < 1.0000000000000002e-10) { *t2 = 0.0; *t1 = t; return 1; }
            f = ent_nearest_t(&src->e[prev], b, &t);
            if ((f.y - b.y) * (f.y - b.y) + (f.x - b.x) * (f.x - b.x) < 1.0000000000000002e-10) { *t2 = t; *t1 = 1.0; return 1; }
            *t2 = 1.0; *t1 = 0.0;
            return 1;
        }
        if (1.0000000000000002e-10 <= (m1.x - m2.x) * (m1.x - m2.x) + (m1.y - m2.y) * (m1.y - m2.y)) { *t2 = 1.0; *t1 = 0.0; return 1; }
        cur++;
        if (n - 1 < cur) cur = 0;
        *e1 = cur;
        if (cur == prev) return 0;
        prev--;
        if (prev < 0) prev = n - 1;
        *e2 = prev;
    } while (cur != prev || n < 2);
    if (src->e[cur].id == 'A' && 355.0 < fabs(src->e[cur].a[4] * 57.29577951308232) && 1.0000000000000002e-10 < (src->e[cur].a[2] * 2.0) * (src->e[cur].a[2] * 2.0)) { *t2 = 1.0; *t1 = 0.0; return 1; }
    return 0;
}
/* FUN_18014a290 (l.177490-177675) on the section (ei, ti) -> (ej, tj) of s: walk on by dist; the new position and its point; 0 when the section ends first */
static int sec_walk(const Sh *s, int *pei, double *pti, int ej, double tj, double dist, P2 *pt) {
    double ti = *pti < 0.0 ? 0.0 : *pti > 1.0 ? 1.0 : *pti, t2 = tj < 0.0 ? 0.0 : tj > 1.0 ? 1.0 : tj;
    int e = *pei < 0 ? 0 : *pei;
    if (s->n - 1 < e) e = s->n - 1;
    int ej2 = ej < 0 ? 0 : ej;
    if (s->n - 1 < ej2) ej2 = s->n - 1;
    int cnt = sec_count(s, e, ti, ej2, t2);
    int i = 0, got = 0;
    double t = 0.0;
    while (i < cnt) {
        double elen = e_len(&s->e[e]), piece;
        if (cnt == 1) piece = (t2 - ti) * elen;
        else if (i == 0) piece = (1.0 - ti) * elen;
        else if (i == cnt - 1) piece = elen * t2;
        else piece = elen;
        if (dist < piece) {
            t = elen <= 1e-10 ? 0.0 : dist / elen;
            if (i == 0) t += ti;
            if (i != cnt - 1 || t <= t2) got = 1;
            break;
        }
        dist -= piece;
        if (i == cnt - 1) { t = t2; if (dist - 1e-10 < 0.0) got = 1; break; }
        i++;
        e = (e + 1) % s->n;
    }
    if (!got) { *pei = -1; *pti = 0.0; pt->x = pt->y = 0.0; return 0; }
    *pei = e; *pti = t;
    *pt = ent_at(&s->e[e], t);
    return 1;
}
/* FUN_1800cb710 (l.109917-110120): which way a reversal corner turns. 1 (arc, as a retrace) unless the walk finds a difference; then, along the two sides of the section (itself and reversed) at growing distances, the first pair of points apart by more than 1e-5 and off the line through the source point by more than 1e-7 decides: 3 (outer) or 2 (inner) */
static int reversal_kind(const St *st, const Sh *src, int isrc) {
    int e2, e1; double t2, t1;
    if (!reversal_walk(src, isrc, &e2, &t2, &e1, &t1)) return 1;
    Sh f, b; sh_init(&f); sh_init(&b);
    sec_append(src, e1, t1, e2, t2, &f);
    int ret = 1;
    if (f.n) {
        sh_copy(&b, &f);
        sh_rev(&b);
        double tot = 0.0;
        for (int i = 0; i < f.n; i++) tot += e_len(&f.e[i]);
        double half = tot * 0.5, l0 = e_len(&f.e[0]), l1 = e_len(&b.e[0]);
        double l = l0 <= l1 ? l1 : l0;
        if (half <= l) l = half;
        int fe = 0, be = 0, flast = f.n - 1, blast = b.n - 1, iter = 0;
        double ft = 0.0, bt = 0.0, step = 1e-05, dist = 0.0;
        P2 pa, pb;
        int oa = sec_walk(&f, &fe, &ft, flast, 1.0, step, &pa), ob = sec_walk(&b, &be, &bt, blast, 1.0, step, &pb);
        while (oa && ob) {
            if (1.0000000000000002e-10 < (pa.y - pb.y) * (pa.y - pb.y) + (pa.x - pb.x) * (pa.x - pb.x)) {
                double c = (st->src[0] - pb.x) * (pa.y - st->src[1]) - (st->src[1] - pb.y) * (pa.x - st->src[0]);
                if (1e-07 <= fabs(c) && fabs(c) != 1e-07) { ret = 0.0 <= c ? 3 : 2; break; }
            }
            dist += step; step *= 2.0;
            if (half < step + dist || (0x19 < iter && l < dist)) break;
            iter++;
            oa = sec_walk(&f, &fe, &ft, flast, 1.0, step, &pa);
            ob = sec_walk(&b, &be, &bt, blast, 1.0, step, &pb);
        }
    }
    free(f.e); free(b.e);
    return ret;
}

static const char *corner(St *st, int k, Sh *o, const Sh *src, int isrc) {
    double dx = st->prev_end[0] - st->ps[0], dy = st->prev_end[1] - st->ps[1], d2 = dy * dy + dx * dx;
    if (d2 < 9.999999999999998e-15) return NULL;
    double t1x = st->ts[0], t1y = st->ts[1], t2x = st->prev_t[0], t2y = st->prev_t[1];
    double dot = t1y * t2y + t1x * t2x;
    int blunt = dot < 0.0001;
    int ret;
    if (!(cos(3.141492653589793) <= dot)) {
        ret = reversal_kind(st, src, isrc);   /* FUN_1800ca830 l.109327: FUN_1800cb710 */
    } else if (t1y * t2x - t2y * t1x <= 0.0) {
        ret = (blunt || 1e-08 <= d2 || corner_trim(0, k, o)) ? 2 : 0;
    } else {
        int prev_ce = o->e[k - 1].ce, cur_ce = o->e[k < o->n ? k : 0].ce;
        ret = (st->ang_cos < dot && !(prev_ce & 0x10) && !(cur_ce & 0x10)) ? 4 : 3;
    }
    if (ret == 0) return NULL;
    if (ret == 2) {
        if (corner_trim(1, k, o)) return NULL;
        if (!blunt || st->n0 != 0 || (st->flags & 0x20000)) { corner_chord(st, k, o); return NULL; }
    } else if (ret == 4 && corner_mitre(st, k, o)) return NULL;
    corner_arc(st, k, o);
    return NULL;
}

/* FUN_1800da940 (l.118175-118310): a mitre pair (two lines ce & 2) is merged into its neighbours when they are lines */
static void merge_mitres(Sh *s) {
    Sh o; sh_init(&o);
    int n = s->n;
    for (int i = 0; i < n; i++) {
        Ent *e = &s->e[i];
        if (!((e->ce & 2) && e->id == 'L')) { sh_push(&o, e); continue; }
        if (i > 0 && s->e[i - 1].id == 'L' && o.n) { o.e[o.n - 1].a[2] = e->a[2]; o.e[o.n - 1].a[3] = e->a[3]; ent_box(&o.e[o.n - 1]); }
        else sh_push(&o, e);
        int j = (i + 2) % n;
        if (s->e[j].id == 'L') {
            int idx = 0;
            if (j != 0) { sh_push(&o, &s->e[j]); idx = o.n - 1; }
            o.e[idx].a[0] = s->e[i + 1].a[0]; o.e[idx].a[1] = s->e[i + 1].a[1];
            ent_box(&o.e[idx]);
        } else {
            sh_push(&o, &s->e[i + 1]);
            if (j != 0) sh_push(&o, &s->e[j]);
        }
        i += 2;
    }
    free(s->e); s->e = o.e; s->n = o.n; s->cap = o.cap;
}

/* FUN_180122fb0: the next entity index of a shape (closed: wraps) */
static int next_ent(const Sh *s, int *i) {
    if (*i + 1 < s->n) { (*i)++; return 1; }
    if (s->dir != 0) { *i = 0; return 1; }
    return 0;
}
/* FUN_18014b720 (l.178227-178373): the distance along the shape from (i, ti) forward to (j, tj) is within tol */
static int close_forward(const Sh *s, int i, double ti, int j, double tj, double tol) {
    ti = ti < 0 ? 0 : ti > 1 ? 1 : ti; tj = tj < 0 ? 0 : tj > 1 ? 1 : tj;
    if (i != j || tj < ti) {
        const Ent *e = &s->e[i];
        double D;
        if (e->id == 'L') D = (1 - ti) * e_len(e);
        else if (e->id == 'A') D = fabs((e->a[4] - e->a[4] * ti) * e->a[2]);
        else D = 0;
        if (D <= tol) {
            int k = i;
            if (!next_ent(s, &k)) return 0;
            do {
                if (k == j) {
                    const Ent *f = &s->e[j];
                    double D2 = f->id == 'L' ? tj * e_len(f) : f->id == 'A' ? fabs(f->a[4] * tj * f->a[2]) : 0;
                    return D + D2 <= tol;
                }
                D += e_len(&s->e[k]);
            } while (D <= tol && next_ent(s, &k));
        }
        return 0;
    }
    const Ent *e = &s->e[i];
    double D = e->id == 'L' ? (tj - ti) * e_len(e) : e->id == 'A' ? fabs((tj - ti) * e->a[4] * e->a[2]) : 0;
    return D <= tol;
}
/* FUN_18012ff20 (l.163545-163562) */
static int same_point(const Sh *s, int i, double ti, int j, double tj, double tol) {
    return close_forward(s, i, ti, j, tj, tol) || close_forward(s, j, tj, i, ti, tol);
}
/* FUN_18012f620 (l.163304-163463): is there a crossing of the shape's entities? limited = bit 0x20000: only the next three entities of each
   (a wrap at the end of the shape), the Infinite look ahead off test */
static int any_crossing(const Sh *s, int limited) {
    int n = s->n;
    for (int i = 0; i < n - 1; i++) {
        const Ent *a = &s->e[i];
        int j = i + 1, cnt = 0;
        while (j < n) {
            const Ent *b = &s->e[j];
            if (a->box[0] - 1e-05 <= b->box[2] && b->box[0] <= a->box[2] + 1e-05 && a->box[1] - 1e-05 <= b->box[3] && b->box[1] <= a->box[3] + 1e-05) {
                double ta[2], tb[2], pt[4];
                int m = ent_isect(1e-07, a, b, ta, tb, pt);
                for (int k = m - 1; k >= 0; k--)
                    if (!same_point(s, i, ta[k], j, tb[k], 1e-06)) return 1;
            }
            j++;
            if (limited) {
                if (n <= j) j = 0;
                if (++cnt > 3 || j == i) break;
            }
        }
    }
    return 0;
}

/* FUN_180122a40 (l.156181-156283): any contact of the two shapes (boxes within 1e-7, CalcEntIntersect tol 1e-5) */
static int shapes_touch(const Sh *a, const Sh *b) {
    if (!(a->box[0] - 1e-07 <= b->box[2] && b->box[0] <= a->box[2] + 1e-07 && a->box[1] - 1e-07 <= b->box[3] && b->box[1] <= a->box[3] + 1e-07)) return 0;
    for (int i = 0; i < a->n; i++) {
        const Ent *ea = &a->e[i];
        for (int j = 0; j < b->n; j++) {
            const Ent *eb = &b->e[j];
            if (ea->box[0] - 1e-07 <= eb->box[2] && eb->box[0] <= ea->box[2] + 1e-07 && ea->box[1] - 1e-07 <= eb->box[3] && eb->box[1] <= ea->box[3] + 1e-07) {
                double ta[2], tb[2], pt[4];
                if (ent_isect(1e-05, ea, eb, ta, tb, pt)) return 1;
            }
        }
    }
    return 0;
}

/* FUN_180126ac0 (l.158542-158722): signed area of a closed shape, its direction */
static int sh_orient_t(const Sh *s, double tol, double *area) {
    double sum = 0.0, x0, y0;
    e_end(&s->e[s->n - 1], &x0, &y0);
    for (int i = 0; i < s->n; i++) {
        const Ent *e = &s->e[i];
        double xs, ys;
        e_start(e, &xs, &ys);
        double dx = x0 - xs, dy = y0 - ys;
        if (tol * tol < dy * dy + dx * dx) { *area = 0; return 0; }
        if (1.0000000000000001e-20 < dy * dy + dx * dx) sum += (ys + y0) * (x0 - xs);
        if (e->id == 'L') { sum += (e->a[3] + ys) * (xs - e->a[2]); x0 = e->a[2]; y0 = e->a[3]; }
        else { /* FUN_1801270e0 (l.158734-158835): the arc's area term = chord term + circular segment: sum over the pieces between multiples of pi of
                  ((span - sin span) r^2 + (ys + ye)(xs - xe)) * dir (dir +1 CCW, -1 CW); the pieces add up to (sw - sin sw) r^2 + the whole chord term */
            double g = e->a[3] + e->a[4], r = e->a[2], xe = e->a[0] + r * cos(g), ye = e->a[1] + r * sin(g);
            sum += (ye + ys) * (xs - xe) + r * r * (e->a[4] - sin(e->a[4]));
            x0 = xe; y0 = ye;
        }
    }
    *area = fabs(sum * 0.5);
    return 0.0 < sum ? 1 : -1;
}

/* FUN_1800dbce0 (l.118764-119112): the raw offset ring of one closed shape; NULL = a path not ported */
/* FUN_1800db4b0 (l.118508-118670): a shape of one arc (a full circle): the offset arc, direct (no ring trimming); r 0 when the circle
   vanishes (sw < 0: r <= |d| + tol/2, else tested as |d| + 1e-15 with flag 0x100) */
static int raw_circle(St *st, const Sh *in, Sh *out) {
    const Ent *c = &in->e[0];
    double sw = c->a[4], r = c->a[2], lim;
    if (!(st->flags & 0x100)) lim = st->t1 * 0.5 + st->dist; else lim = st->dist + 1e-15;
    if (!(0.0 <= sw) && r <= lim && lim != r) return 0;
    double R = sw <= 0.0 ? r - st->dist : r + st->dist;
    Ent e; memset(&e, 0, sizeof e);
    e.id = 'A'; e.ce = 1; e.src_sh = c->src_sh; e.src_ent = c->src_ent;
    e.a[0] = c->a[0]; e.a[1] = c->a[1]; e.a[2] = R; e.a[3] = c->a[3]; e.a[4] = sw;
    ent_box(&e);
    sh_init(out);
    sh_push(out, &e);
    out->dir = in->dir;
    return 1;
}
static const char *raw_ring(St *st, const Sh *in, Sh *out) {
    sh_init(out);
    int have0 = 0;
    double s_src[2] = { 0 }, s_ps[2] = { 0 }, s_pe[2] = { 0 }, s_ts[2] = { 0 }, s_te[2] = { 0 };
    for (int i = 0; i < in->n; i++) {
        const Ent *e = &in->e[i];
        int before = out->n;
        /* FUN_1800dbce0 l.118986-119005, an open source: from the returning half on no mitres (+0x58 = 1); at its first entity everything built so far (the forward half) is marked 0x20 */
        if (st->n0 != 0 && st->n0 <= i) {
            st->ang_cos = 1.0;
            if (i == st->n0) for (int k = 0; k < out->n; k++) out->e[k].ce |= 0x20;
        }
        if (e->id == 'A') raw_arc(st, e, out);
        else if (e->id == 'L') raw_line(st, e, out);
        if (before == 0) {
            if (out->n && !have0) {
                memcpy(s_src, st->src, 16); memcpy(s_ps, st->ps, 16); memcpy(s_pe, st->pe, 16); memcpy(s_ts, st->ts, 16); memcpy(s_te, st->te, 16);
                have0 = 1;
            }
        } else if (before < out->n) {
            const char *w = corner(st, before, out, in, i);
            if (w) return w;
        }
    }
    if (out->n) {   /* the closing corner (l.119082-119112) */
        st->prev_t[0] = st->te[0]; st->prev_t[1] = st->te[1];
        st->prev_end[0] = st->pe[0]; st->prev_end[1] = st->pe[1];
        memcpy(st->src, s_src, 16); memcpy(st->ps, s_ps, 16); memcpy(st->pe, s_pe, 16); memcpy(st->ts, s_ts, 16); memcpy(st->te, s_te, 16);
        const char *w = corner(st, out->n, out, in, 0);
        if (w) return w;
    }
    for (int i = 0; i < out->n; i++) ent_box(&out->e[i]);
    sh_box(out);
    merge_mitres(out);
    out->dir = -1;
    return NULL;
}

/* FUN_1800c8da0 (l.108468-108548): the "flagged" entities (corners / inverted arcs: ce & 4 or 8) are longer than the rest: the ring is empty */
static int flagged_longer(const Sh *s) {
    double a = 0, b = 0;
    for (int i = 0; i < s->n; i++) {
        if (s->e[i].ce & 0xc) b += e_len(&s->e[i]); else a += e_len(&s->e[i]);
    }
    return a < b;
}

/* FUN_1800da6a0 (l.118079-118125) + FUN_1800d9530 (l.117550-117594): the start = the entity with the lowest source index; one earlier when that
   entity is an arc after a line with ce & 2. Rotate: FUN_18014fd50 (l.180573-180715). */
static void rotate_start(Sh *s) {
    int best = -1, lo = 0x7fffffff;
    for (int i = 0; i < s->n; i++)
        if (s->e[i].src_ent >= 0 && s->e[i].src_ent < lo) { lo = s->e[i].src_ent; best = i; }
    if (best < 0) return;
    if (s->e[best].id == 'A') {
        int p = best == 0 ? s->n - 1 : best - 1;
        if (s->e[p].id == 'L' && (s->e[p].ce & 2)) best = p;
    }
    if (best <= 0 || s->dir == 0) return;
    Ent *t = malloc((size_t)s->n * sizeof(Ent));
    memcpy(t, s->e + best, (size_t)(s->n - best) * sizeof(Ent));
    memcpy(t + (s->n - best), s->e, (size_t)best * sizeof(Ent));
    memcpy(s->e, t, (size_t)s->n * sizeof(Ent));
    free(t);
}

/* ---- the clean-up tail of each result shape: FUN_1800d8d90 (l.117293-117338) ---- */
static void sh_erase(Sh *s, int i) { memmove(s->e + i, s->e + i + 1, (size_t)(s->n - i - 1) * sizeof(Ent)); s->n--; }
/* FUN_1800d8f00 (l.117345-117462): the intersection to use of two entities (the one nearer the midpoint of the end of a and the start of b); -1 as in FUN_18012bcd0 without the FUN_18012ac00 test */
static int tail_choice(const Ent *a, const Ent *b, int n, const double t1[2], const double t2[2], const P2 pts[2]) {
    if (!n) return -1;
    P2 p = ent_end(a), q = ent_start(b);
    double mx = (q.x + p.x) * 0.5, my = (q.y + p.y) * 0.5;
    int k = n == 1 ? 0 : ((my - pts[1].y) * (my - pts[1].y) + (mx - pts[1].x) * (mx - pts[1].x) <= (mx - pts[0].x) * (mx - pts[0].x) + (my - pts[0].y) * (my - pts[0].y));
    if ((a->id != 'L' || (0.0 < t1[k] || t1[k] == 0.0)) && (b->id != 'L' || t2[k] <= 1.0)) {
        if (t1[k] < 0.0 || 1.0 < t1[k] || t2[k] < 0.0 || 1.0 < t2[k]) {
            double dx = p.x - q.x, dy = p.y - q.y, ey = my - pts[k].y, ex = mx - pts[k].x;
            if (sqrt(dy * dy + dx * dx) * 0.5 * 3.0 < sqrt(ey * ey + ex * ex)) return -1;
        }
        return k;
    }
    return -1;
}
/* FUN_1800d8050 (l.116745-116801): arc a, line b: the foot of the arc's centre on the line (not found: the line's start): *t2 on the line (<= 1), *t1 on the arc (radius - 1e-5 ok, FUN_1800a46e0, within half the rest of the circle) */
static int alt_arc_line(const XE *a, const XE *b, double *t1, double *t2) {
    P2 d = psub(b->p2_, b->p1), f; double u;
    if (gp_lnln(b->p1, d, a->c, p2(d.y, -d.x), t2, &u, &f)) { if (1.0 < *t2) return 0; }
    else { *t2 = 0.0; f = b->p1; }
    double dy = f.y - a->c.y, dx = f.x - a->c.x, r = a->r - 1e-05;
    if (r * r <= dy * dy + dx * dx && gp_arc_t_ext(a, f, t1)) {
        double w = fabs(a->sw);
        return fabs(a->sw * *t1) <= (6.283185307179586 - w) * 0.5 + w;
    }
    return 0;
}
/* FUN_1800d8260 (l.116808-116874): line a, arc b: the foot of the arc's centre on the line (not found: the line's end): *t1 on the line (>= 0), *t2 on the arc */
static int alt_line_arc(const XE *a, const XE *b, double *t1, double *t2) {
    P2 d = psub(a->p2_, a->p1), f; double u;
    if (gp_lnln(a->p1, d, b->c, p2(d.y, -d.x), t1, &u, &f)) { if (*t1 < 0.0) return 0; }
    else { *t1 = 1.0; f = a->p2_; }
    double dy = f.y - b->c.y, dx = f.x - b->c.x, r = b->r - 1e-05;
    if (r * r <= dy * dy + dx * dx && gp_arc_t_ext(b, f, t2)) {
        double w = fabs(b->sw);
        return *t2 <= 1.0 || (6.283185307179586 - fabs(b->sw * *t2)) + w <= (6.283185307179586 - w) * 0.5 + w;
    }
    return 0;
}
/* FUN_1800d7940 (l.116580-116738): two arcs (a is not b: the caller has a != b): the points of the two circles on the line of centres */
static int alt_arc_arc(const XE *a, const XE *b, double *t1, double *t2) {
    double ddy = b->c.y - a->c.y, ddx = b->c.x - a->c.x, dc = sqrt(ddy * ddy + ddx * ddx), r1 = a->r, r2 = b->r, ux, uy, vx, vy;
    if (dc < r1 || dc < r2) {
        double dxx, dyy;
        if (r2 <= r1) { dxx = b->c.x - a->c.x; dyy = b->c.y - a->c.y; }
        else { dxx = a->c.x - b->c.x; dyy = a->c.y - b->c.y; }
        double l = sqrt(dyy * dyy + dxx * dxx);
        if (l < 1e-12) {
            double sx, sy;
            if (b->id == 'A') { sx = cos(b->sa) * r2 + b->c.x; sy = sin(b->sa) * r2 + b->c.y; } else return 0;
            dxx = sx - b->c.x; dyy = sy - b->c.y;
            l = sqrt(dyy * dyy + dxx * dxx);
            if (l < 1e-12) return 0;
        }
        ux = dxx / l; uy = dyy / l; vx = ux; vy = uy;
    } else {
        if (dc < 1e-12) return 0;
        ux = ddx / dc; uy = ddy / dc; vx = -ux; vy = -uy;
    }
    if (!gp_arc_t_ext(a, p2(a->c.x + r1 * ux, a->c.y + uy * r1), t1)) return 0;
    if (!gp_arc_t_ext(b, p2(b->c.x + r2 * vx, b->c.y + vy * r2), t2)) return 0;
    double w1 = fabs(a->sw), w2 = fabs(b->sw);
    return fabs(a->sw * *t1) <= (6.283185307179586 - w1) * 0.5 + w1 && (*t2 <= 1.0 || (6.283185307179586 - fabs(b->sw * *t2)) + w2 <= (6.283185307179586 - w2) * 0.5 + w2);
}
/* FUN_1800d8900 (l.117002-117144): no usable intersection: the neighbours' feet. Line / line: when there is no intersection (n = 0), the feet of the perpendiculars through the midpoint of the gap;
   otherwise (or if one is missing) the foot of the start of b on a and of the end of a on b, the one nearer its own end chosen; parameters must keep the far ends (t1 > 0, t2 < 1). n != 0: only line / line. */
static int tail_alt(const Ent *pa, const Ent *pb, int n, const double t1[4], const double t2[2], double *a1, double *a2) {
    *a1 = 0.0; *a2 = 0.0;
    XE a = to_xe(pa), b = to_xe(pb);
    if (a.id == 'L' && b.id == 'L') {
        if (n != 0 && 0.0 < t1[0] && (t2[0] <= 1.0 && t2[0] != 1.0)) return 0;
        P2 p1 = a.p1, p2_ = b.p1, d1 = psub(a.p2_, a.p1), d2 = psub(b.p2_, b.p1), f1 = p2(0, 0), f2 = p2(0, 0), M; double u;
        int r1 = 0, r2 = 0;
        if (n == 0) {
            M = p2((a.p2_.x + b.p1.x) * 0.5, (a.p2_.y + b.p1.y) * 0.5);
            r1 = gp_lnln(p1, d1, M, p2(d1.y, -d1.x), a1, &u, &f1);
            r2 = gp_lnln(p2_, d2, M, p2(d2.y, -d2.x), a2, &u, &f2);
            if (r1 == 1 && r2 == 1) {
                if (*a1 <= 0.0) return 0;
                return !(*a2 == 1.0 || 1.0 < *a2);
            }
        }
        r1 = gp_lnln(p1, d1, b.p1, p2(d1.y, -d1.x), a1, &u, &f1);
        r2 = gp_lnln(p2_, d2, a.p2_, p2(d2.y, -d2.x), a2, &u, &f2);
        if (r1 != 0 && (r2 == 0 || !((f2.y - a.p2_.y) * (f2.y - a.p2_.y) + (f2.x - a.p2_.x) * (f2.x - a.p2_.x) <= (f1.y - b.p1.y) * (f1.y - b.p1.y) + (f1.x - b.p1.x) * (f1.x - b.p1.x)))) {
            *a2 = 0.0; return !(*a1 == 0.0 || *a1 < 0.0);
        }
        if (r1 == 0 && r2 == 0) return 0;
        *a1 = 1.0;
        return !(*a2 == 1.0 || 1.0 < *a2);
    }
    if (n != 0) return 0;
    if (a.id == 'A' && b.id == 'L') return alt_arc_line(&a, &b, a1, a2);
    if (a.id == 'L' && b.id == 'A') return alt_line_arc(&a, &b, a1, a2);
    if (a.id == 'A' && b.id == 'A') return alt_arc_arc(&a, &b, a1, a2);
    return 0;
}
/* FUN_1800d5330 (l.115006-115604): the two trimmed entities (with a link line for a gap > 1e-8, none over tol) and the run a..b they replace are within tol of each other (FUN_18013c4f0, 3 levels) */
static int tail_match(double tb, const Sh *s, int a, int b, double t1, double t2) {
    Sh old; sh_init(&old);
    int k = a;
    for (;;) {
        if (old.n) {
            P2 q0 = ent_end(&old.e[old.n - 1]), q1 = ent_start(&s->e[k]);
            if (1e-16 < (q0.x - q1.x) * (q0.x - q1.x) + (q0.y - q1.y) * (q0.y - q1.y)) {
                Ent l; memset(&l, 0, sizeof l); l.id = 'L'; l.a[0] = q0.x; l.a[1] = q0.y; l.a[2] = q1.x; l.a[3] = q1.y; sh_push(&old, &l);
            }
        }
        sh_push(&old, &s->e[k]);
        if (k == b) break;
        k = (k + 1) % s->n;
    }
    Ent x = s->e[a], y = s->e[b], l;
    if (x.id == 'L') {
        double x1 = x.a[0], y1 = x.a[1], dx = x.a[2] - x1, dy = x.a[3] - y1;
        x.a[2] = dx * t1 + x1; x.a[3] = dy * t1 + y1; x.a[0] = dx * 0.0 + x1; x.a[1] = dy * 0.0 + y1;
    } else if (x.id == 'A') { x.a[4] = (t1 - 0.0) * s->e[a].a[4]; x.a[3] = s->e[a].a[4] * 0.0 + s->e[a].a[3]; }
    if (y.id == 'A' && 1.0 < t2) t2 = -fabs((6.283185307179586 - fabs(y.a[4] * t2)) / y.a[4]);
    if (y.id == 'L') {
        double x1 = y.a[0], y1 = y.a[1], dx = y.a[2] - x1, dy = y.a[3] - y1;
        y.a[0] = dx * t2 + x1; y.a[1] = dy * t2 + y1; y.a[2] = dx + x1; y.a[3] = dy + y1;
    } else if (y.id == 'A') { y.a[4] = (1.0 - t2) * s->e[b].a[4]; y.a[3] = s->e[b].a[4] * t2 + s->e[b].a[3]; }
    P2 q0 = ent_end(&x), q1 = ent_start(&y);
    double dq2 = (q0.x - q1.x) * (q0.x - q1.x) + (q0.y - q1.y) * (q0.y - q1.y);
    int ok = 1;
    Ent const *nn[3]; int nnw = 0;
    memset(&l, 0, sizeof l);
    if (tb < sqrt(dq2)) { free(old.e); return 0; }
    nn[nnw++] = &x;
    if (1e-16 < dq2) { l.id = 'L'; l.a[0] = q0.x; l.a[1] = q0.y; l.a[2] = q1.x; l.a[3] = q1.y; nn[nnw++] = &l; }
    nn[nnw++] = &y;
    const Ent **oo = malloc((size_t)old.n * sizeof(Ent *));
    for (int i = 0; i < old.n; i++) oo[i] = &old.e[i];
    for (int i = 0; i < old.n && ok; i++) if (!piece_near(&old.e[i], nn, nnw, tb)) ok = 0;
    for (int i = 0; i < nnw && ok; i++) if (!piece_near(nn[i], oo, old.n, tb)) ok = 0;
    free(oo); free(old.e);
    return ok;
}
/* FUN_1800d6ad0 (l.115968-116230): a joint between entity a and entity *pb (a run of removed / flagged entities between them): the intersection or the feet, tested with FUN_1800d5330, a trimmed, b trimmed, the
   entities between removed (a wrap-around removes the shape's tail and head) */
static int tail_joint(double tb, Sh *s, int a, int *pb) {
    int b = *pb;
    if (a == b) return 0;
    double t1[4] = { 0, 0, 0, 0 }, t2[2] = { 0, 0 };
    P2 pts[2] = { { 0, 0 }, { 0, 0 } };
    XE x = to_xe(&s->e[a]), y = to_xe(&s->e[b]);
    int n = gp_ent_isect_ext(1e-06, &x, &y, t1, t2, pts);
    int sel = tail_choice(&s->e[a], &s->e[b], n, t1, t2, pts);
    if (sel == -1) {
        if (!tail_alt(&s->e[a], &s->e[b], n, t1, t2, &t1[2], &t1[3])) return 0;
        sel = 0; t1[0] = t1[2]; t2[0] = t1[3];
    }
    if (!tail_match(tb, s, a, b, t1[sel], t2[sel])) return 0;
    trim_end(&s->e[a], t1[sel]);
    trim_start(&s->e[b], t2[sel]);
    if (a < b) {
        while (*pb - 1 != a) { sh_erase(s, *pb - 1); (*pb)--; }
    } else {
        while (s->n - 1 != a) s->n--;
        while (*pb != 0) { sh_erase(s, *pb - 1); (*pb)--; }
    }
    return 1;
}
/* FUN_1800d62d0 (l.115651-115961): entities shorter than t1 (lines) / t2 (arcs): replaced by a line from the previous end to their end (no source link), or removed when that line would be within max(t1, t2); runs are then joined to the
   next entity by FUN_1800d6ad0 */
static void tail_short(const St *st, Sh *s) {
    double tl = st->t1, ta = st->t2, tb = tl <= ta ? ta : tl;
    P2 first = s->n ? ent_start(&s->e[0]) : p2(0.0, 0.0);
    int short_run = 0, last_long = -1, first_long = -1, first_short = 0, c14 = 0, iv17 = -1, i = 0;
    while (i < s->n) {
        iv17 = i;
        Ent *e = &s->e[i];
        double len = e_len(e), thr = e->id == 'L' ? tl : ta;
        if ((e->id == 'L' || e->id == 'A') && !(thr < len || thr == len)) {
            short_run = 1;
            P2 p = i == 0 ? (first_short = 1, first) : ent_end(&s->e[i - 1]);
            P2 q = ent_end(e);
            double dy = p.y - q.y, dx = p.x - q.x, d = sqrt(dy * dy + dx * dx);
            if (d < tb || d == tb) { sh_erase(s, i); iv17 = iv17 - 1; }
            else {
                memset(e, 0, sizeof *e);
                e->id = 'L'; e->src_sh = e->src_ent = -1; e->a[0] = p.x; e->a[1] = p.y; e->a[2] = q.x; e->a[3] = q.y;
                ent_box(e);
            }
        } else {
            if (short_run && last_long != -1) { tail_joint(tb, s, last_long, &i); iv17 = i; }
            last_long = iv17;
            short_run = 0;
            if (first_long == -1) first_long = iv17;
        }
        i = iv17 + 1;
        c14 = first_short;
        iv17 = last_long;
    }
    int first_ref = first_long;
    if (s->dir != 0 && (short_run || c14) && iv17 != -1 && first_long != -1) tail_joint(tb, s, iv17, &first_ref);
    if (s->n) sh_box(s);
}
/* FUN_1800d8a30 (l.117177-117206) / FUN_1800d8990 (l.117149-117172) */
static int ce2_run_before(const Sh *s, int from) {
    if (from < 0) return -1;
    for (int i = from; i < s->n; i++) if (!(s->e[i].ce & 2) && (s->e[(i + 1) % s->n].ce & 2)) return i;
    return -1;
}
static int ce2_run_after(const Sh *s, int a) {
    if (a < 0) return -1;
    int i = a;
    do { int j = (i + 1) % s->n; if ((s->e[i].ce & 2) && !(s->e[j].ce & 2)) return j; i = j; } while (i != a);
    return -1;
}
/* FUN_1800d7100 (l.116238-116573): each run of corner entities (ce & 2) between two others: FUN_1800d6ad0 on the sub-chain replaces the run by the join of its neighbours (tol t1) */
static void tail_corners(const St *st, Sh *s) {
    if (!s->n) return;
    int first = -1, nz = s->n;
    char *del = calloc((size_t)nz, 1);
    int a = ce2_run_before(s, 0), b = ce2_run_after(s, a);
    while (a != -1) {
        if (a == first || b == -1) break;
        if (first == -1) first = a;
        Sh t; sh_init(&t);
        sh_push(&t, &s->e[a]);
        for (int j = a; j != b; ) { j = (j + 1) % s->n; sh_push(&t, &s->e[j]); }
        int k = t.n - 1;
        if (tail_joint(st->t1, &t, 0, &k)) {
            s->e[a] = t.e[0];
            s->e[b] = t.e[k];
            for (int j = (a + 1) % nz; j != b; j = (j + 1) % nz) del[j] = 1;
        }
        free(t.e);
        a = ce2_run_before(s, b);
        b = ce2_run_after(s, a);
    }
    for (int i = nz - 1; i >= 0; i--) if (del[i]) sh_erase(s, i);
    free(del);
    if (s->n) sh_box(s);
}
/* FUN_1800d8d90 (l.117293-117338): flags 0x100 off (FUN_1800d6180 l.115609 removes small closed shapes covered by others: none for a single ring): the two passes for the result shape */
static void tail_shape(const St *st, Sh *s) {
    if (st->flags & 0x100) return;
    tail_short(st, s);
    tail_corners(st, s);
}

/* ---- loop removal on a self-intersecting raw offset ring ---- */
/* the crossing records ISIS (0x38: +0x00 point, +0x10 entity, +0x18 t, +0x20 state, +0x28 section length, +0x30 partner), FUN_18012f620 (l.163308-163458); states: 0 kept, 2 taken into a ring, 3 cut */
typedef struct { P2 pt; int ent; double t; int state; double len; int partner; } Isis;
typedef struct { Isis *r; int n, cap; } IsisL;
static void isis_push(IsisL *l, Isis a) { if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 16; l->r = realloc(l->r, (size_t)l->cap * sizeof(Isis)); } l->r[l->n++] = a; }
/* FUN_18012e7d0 (l.162850-162888): entity; then, when the points differ by more than 1e-15, t; then the partners' entity and t */
static int isis_cmp(const void *x, const void *y, const void *ctx) {
    const Isis *is = ctx, *a = &is[*(const int *)x], *b = &is[*(const int *)y];
    if (b->ent < a->ent) return 1;
    if (a->ent < b->ent) return -1;
    if (1e-15 < fabs(a->pt.x - b->pt.x) || 1e-15 < fabs(a->pt.y - b->pt.y)) { if (a->t < b->t) return -1; if (b->t < a->t) return 1; }
    const Isis *pa = &is[a->partner], *pb = &is[b->partner];
    if (pb->ent < pa->ent) return 1;
    if (pa->ent < pb->ent) return -1;
    if (pb->t <= pa->t) return pb->t < pa->t;
    return -1;
}
/* the comparator LAB_180006dd9 -> 0x1800c7b80 (disassembly 0x1800c7b80-0x1800c7bb2): section length descending */
static int isis_cmp_len(const void *x, const void *y, const void *ctx) {
    const Isis *is = ctx;
    double a = is[*(const int *)x].len, b = is[*(const int *)y].len;
    if (a < b) return 1;
    return a <= b ? 0 : -1;
}
static void ms_swap(char *a, char *b, size_t w) { if (a != b) for (size_t k = 0; k < w; k++) { char t = a[k]; a[k] = b[k]; b[k] = t; } }
/* the Microsoft CRT qsort_s (median of three, CUTOFF 8 shortsort), the order of equal keys of the two lists sorted here (copy of viewer/mcce_dyn.c gp_msqsort with the context argument) */
static void ms_qsort(void *base, size_t num, size_t w, const void *ctx, int (*cmp)(const void *, const void *, const void *)) {
    char *lostk[64], *histk[64]; int stkptr = 0;
    if (num < 2) return;
    char *lo = base, *hi = (char *)base + w * (num - 1);
recurse:;
    size_t size = (size_t)(hi - lo) / w + 1;
    if (size <= 8) {
        char *h = hi;
        while (h > lo) { char *max = lo; for (char *p = lo + w; p <= h; p += w) if (cmp(p, max, ctx) > 0) max = p; ms_swap(max, h, w); h -= w; }
    } else {
        char *mid = lo + (size / 2) * w;
        if (cmp(lo, mid, ctx) > 0) ms_swap(lo, mid, w);
        if (cmp(lo, hi, ctx) > 0) ms_swap(lo, hi, w);
        if (cmp(mid, hi, ctx) > 0) ms_swap(mid, hi, w);
        char *loguy = lo, *higuy = hi;
        for (;;) {
            if (mid > loguy) { do loguy += w; while (loguy < mid && cmp(loguy, mid, ctx) <= 0); }
            if (mid <= loguy) { do loguy += w; while (loguy <= hi && cmp(loguy, mid, ctx) <= 0); }
            do higuy -= w; while (higuy > mid && cmp(higuy, mid, ctx) > 0);
            if (higuy < loguy) break;
            ms_swap(loguy, higuy, w);
            if (mid == higuy) mid = loguy;
        }
        higuy += w;
        if (mid < higuy) { do higuy -= w; while (higuy > mid && cmp(higuy, mid, ctx) == 0); }
        if (mid >= higuy) { do higuy -= w; while (higuy > lo && cmp(higuy, mid, ctx) == 0); }
        if (higuy - lo >= hi - loguy) {
            if (lo < higuy) { lostk[stkptr] = lo; histk[stkptr] = higuy; ++stkptr; }
            if (loguy < hi) { lo = loguy; goto recurse; }
        } else {
            if (loguy < hi) { lostk[stkptr] = loguy; histk[stkptr] = hi; ++stkptr; }
            if (lo < higuy) { hi = higuy; goto recurse; }
        }
    }
    --stkptr;
    if (stkptr >= 0) { lo = lostk[stkptr]; hi = histk[stkptr]; goto recurse; }
}
/* FUN_18012f3b0 (l.163225-163301): the crossings of the ring with itself (limited = bit 0x20000 FUN_18012f620 l.163444-163452: the next four entities only), sorted (FUN_180130060 l.163594-163651 + the comparator FUN_18012e7d0),
   the same crossing found twice removed (FUN_18012ed20 l.163020-163139), records rewritten in list order with renumbered partners (FUN_18012e930 l.162891-163017); FUN_18012fc00 (l.163470-163542) then writes back the t values sorted by
   (entity, t): already in that order after the sort above, so a no-op */
static void isis_build(const Sh *s, int limited, IsisL *out) {
    out->n = 0;
    int n = s->n;
    for (int i = 0; i + 1 < n; i++) {
        const Ent *a = &s->e[i];
        int j = i + 1, cnt = 0;
        while (j < n) {
            const Ent *b = &s->e[j];
            if (a->box[0] - 1e-05 <= b->box[2] && b->box[0] <= a->box[2] + 1e-05 && a->box[1] - 1e-05 <= b->box[3] && b->box[1] <= a->box[3] + 1e-05) {
                double ta[2], tb[2], pt[4];
                int m = ent_isect(1e-07, a, b, ta, tb, pt);
                for (int k = m - 1; k >= 0; k--)
                    if (!same_point(s, i, ta[k], j, tb[k], 1e-06)) {
                        int c = out->n;
                        Isis A; memset(&A, 0, sizeof A); A.pt = p2(pt[2 * k], pt[2 * k + 1]); A.ent = i; A.t = ta[k]; A.partner = c + 1;
                        Isis B = A; B.ent = j; B.t = tb[k]; B.partner = c;
                        isis_push(out, A); isis_push(out, B);
                    }
            }
            j++;
            if (limited) {
                if (n <= j) j = 0;
                if (++cnt > 3 || j == i) break;
            }
        }
    }
    if (!out->n) return;
    int m = out->n, *idx = malloc((size_t)m * sizeof(int));
    for (int k = 0; k < m; k++) idx[k] = k;
    ms_qsort(idx, (size_t)m, sizeof(int), out->r, isis_cmp);
    if (m > 1) for (int k = 0; k < m; k++) {
        const Isis *a = &out->r[idx[k]], *b = &out->r[idx[(k + 1) % m]];
        if (!((a->pt.y - b->pt.y) * (a->pt.y - b->pt.y) + (a->pt.x - b->pt.x) * (a->pt.x - b->pt.x) <= 9.999999999999998e-15 && (close_forward(s, a->ent, a->t, b->ent, b->t, 1e-07) || close_forward(s, b->ent, b->t, a->ent, a->t, 1e-07)))) continue;
        const Isis *pa = &out->r[a->partner], *pb = &out->r[b->partner];
        if (!(close_forward(s, pa->ent, pa->t, pb->ent, pb->t, 1e-07) || close_forward(s, pb->ent, pb->t, pa->ent, pa->t, 1e-07))) continue;
        int q = 0;
        while (q < m && idx[q] != a->partner) q++;
        if (q >= m) continue;
        memmove(&idx[k], &idx[k + 1], (size_t)(m - k - 1) * sizeof(int)); m--;
        int q2 = k <= q ? q - 1 : q;
        memmove(&idx[q2], &idx[q2 + 1], (size_t)(m - q2 - 1) * sizeof(int)); m--;
        k -= q < k - 1 ? 2 : 1;
        if (m < 1) break;
    }
    Isis *r = malloc((m > 0 ? (size_t)m : 1) * sizeof(Isis));
    int *nw = calloc((size_t)out->n, sizeof(int));
    for (int k = 0; k < m; k++) { r[k] = out->r[idx[k]]; nw[idx[k]] = k; }
    for (int k = 0; k < m; k++) r[k].partner = nw[r[k].partner];
    free(out->r); out->r = r; out->n = out->cap = m;
    free(nw); free(idx);
}
/* FUN_180147ed0 (l.176362-176395): the number of entities of the section (no full-loop mode here) */
static int sec_count(const Sh *s, int e1, double t1, int e2, double t2) {
    if (e2 < e1 || (e1 == e2 && t2 < t1)) return e2 + 1 + (s->n - e1);
    return e2 - e1 + 1;
}
static double e_clamp(double t) { return t >= 0.0 ? (t > 1.0 ? 1.0 : t) : 0.0; }
/* FUN_180149d80 (l.177357-177448): length of the section */
static double sec_len(const Sh *s, int e1, double t1, int e2, double t2) {
    t1 = e_clamp(t1); t2 = e_clamp(t2);
    int n = sec_count(s, e1, t1, e2, t2), cur = e1;
    double sum = 0.0;
    for (int k = 0; k < n; k++, cur = (cur + 1) % s->n) {
        double l = e_len(&s->e[cur]);
        if (n == 1) l = (t2 - t1) * l; else if (k == 0) l = (1.0 - t1) * l; else if (k == n - 1) l = l * t2;
        sum += l;
    }
    return sum;
}
/* FUN_18014a290 (l.177494-177673): the point at distance dist along the section (0: none) */
static int sec_point(const Sh *s, int e1, double t1, int e2, double t2, double dist, P2 *out) {
    t1 = e_clamp(t1); t2 = e_clamp(t2);
    int n = sec_count(s, e1, t1, e2, t2), cur = e1, found = 0;
    double rem = dist, tt = 0.0;
    for (int k = 0; k < n; k++) {
        double len = e_len(&s->e[cur]), piece;
        if (n == 1) piece = (t2 - t1) * len;
        else if (k == 0) piece = (1.0 - t1) * len;
        else { piece = len; if (k == n - 1) piece = len * t2; }
        if (rem < piece) {
            double v = len <= 1e-10 ? 0.0 : rem / len;
            if (k == 0) v += t1;
            if (k != n - 1 || v <= t2) { found = 1; tt = v; } else tt = v;
            break;
        }
        rem -= piece;
        if (k == n - 1) { if (rem - 1e-10 < 0.0) { found = 1; tt = t2; } break; }
        cur = (cur + 1) % s->n;
    }
    *out = found ? ent_at(&s->e[cur], tt) : p2(0.0, 0.0);
    return found;
}
/* FUN_18014afd0 (l.177952-178224): the pieces of the section meet the shape src (boxes within 1e-5, CalcEntIntersect tol 1e-6) */
static int sec_hits(const Sh *src, const Sh *ring, int e1, double t1, int e2, double t2) {
    t1 = e_clamp(t1); t2 = e_clamp(t2);
    int n = sec_count(ring, e1, t1, e2, t2), cur = e1;
    for (int k = 0; k < n; k++, cur = (cur + 1) % ring->n) {
        Ent p = ring->e[cur];
        if (n == 1) {
            if (p.id == 'L') { double dx = p.a[2] - p.a[0], dy = p.a[3] - p.a[1], x = p.a[0], y = p.a[1]; p.a[2] = dx * t2 + x; p.a[0] = dx * t1 + x; p.a[3] = dy * t2 + y; p.a[1] = dy * t1 + y; }
            else if (p.id == 'A') { double sa = p.a[3], sw = p.a[4]; p.a[3] = t1 * sw + sa; p.a[4] = (t2 - t1) * sw; }
            ent_box(&p);
        } else if (k == 0) {
            if (p.id == 'L') { p.a[0] += (p.a[2] - p.a[0]) * t1; p.a[1] += (p.a[3] - p.a[1]) * t1; }
            else if (p.id == 'A') { p.a[3] += p.a[4] * t1; p.a[4] -= p.a[4] * t1; }
            ent_box(&p);
        } else if (k == n - 1) {
            if (p.id == 'L') { p.a[2] = (p.a[2] - p.a[0]) * t2 + p.a[0]; p.a[3] = (p.a[3] - p.a[1]) * t2 + p.a[1]; }
            else if (p.id == 'A') p.a[4] *= t2;
            ent_box(&p);
        }
        double b0 = p.box[0] - 1e-05, b1 = p.box[1] - 1e-05, b2 = p.box[2] + 1e-05, b3 = p.box[3] + 1e-05;
        if (b0 <= src->box[2] && src->box[0] <= b2 && b1 <= src->box[3] && src->box[1] <= b3)
            for (int i = 0; i < src->n; i++) {
                const Ent *q = &src->e[i];
                if (b0 <= q->box[2] && q->box[0] <= b2 && b1 <= q->box[3] && q->box[1] <= b3) {
                    double ta[2], tb[2], pt[4];
                    if (ent_isect(1e-06, &p, q, ta, tb, pt)) return 1;
                }
            }
    }
    return 0;
}
/* FUN_1800c8370 (l.108140-108280): the section between two crossings contains a flagged (ce & 4 or 8: chord / inverted arc, FUN_1800c8330 l.108122-108135) part longer than 1e-3 */
/* FUN_1800c8330 (l.108122-108135): a chord / inverted arc (ce & 4 or 8); with look-ahead off on an open source only one marked 0x20 counts */
static int ce_flagged(const St *st, const Ent *e) {
    if (!(e->ce & 0xc)) return 0;
    if ((st->flags & 0x20000) && st->n0 != 0) return (e->ce >> 5) & 1;
    return 1;
}
static int sec_flagged(const St *st, const Sh *s, int e1, double t1, int e2, double t2) {
    int n = sec_count(s, e1, t1, e2, t2);
    if (n == 1) {
        const Ent *e = &s->e[e1];
        if (!ce_flagged(st, e)) return 0;
        return 0.001 < (e_clamp(t2) - e_clamp(t1)) * e_len(e);
    }
    const Ent *f = &s->e[e1];
    if (ce_flagged(st, f) && !((1.0 - e_clamp(t1)) * e_len(f) <= 0.001)) return 1;
    for (int k = (e1 + 1) % s->n; k != e2; k = (k + 1) % s->n) if (ce_flagged(st, &s->e[k])) return 1;
    const Ent *l = &s->e[e2];
    if (ce_flagged(st, l) && 0.001 < e_len(l) * e_clamp(t2)) return 1;
    return 0;
}
/* FUN_1800c8920 (l.108345-108465): the point is within |d| - 1e-5 of the source shape, or inside one of the mitre records */
static int pt_covered(const St *st, const Sh *src, P2 p) {
    double r = st->dist - 1e-05; r *= r;
    for (int i = 0; i < src->n; i++) {
        P2 q = ent_nearest(&src->e[i], p);
        if ((p.y - q.y) * (p.y - q.y) + (p.x - q.x) * (p.x - q.x) <= r) return 1;
    }
    for (int i = 0; i < st->nmit; i++) {
        const Mitre *m = &st->mit[i];
        if (p.x <= m->box[2] && m->box[0] <= p.x && p.y <= m->box[3] && m->box[1] <= p.y &&
            0.0 < (m->b[0] - m->a[0]) * (p.y - m->a[1]) - (m->b[1] - m->a[1]) * (p.x - m->a[0]) &&
            0.0 < (m->c[0] - m->b[0]) * (p.y - m->b[1]) - (m->c[1] - m->b[1]) * (p.x - m->b[0]) &&
            0.0 < (m->a[0] - m->c[0]) * (p.y - m->c[1]) - (m->a[1] - m->c[1]) * (p.x - m->c[0])) return 1;
    }
    return 0;
}
/* FUN_1800c8030 (l.108057-108117): state 3 for the sections to drop: flagged ones and, with Infinite look ahead on, those whose midpoint (at +0x28 / 2 along the section) is covered */
static void isis_mark(const St *st, const Sh *src, const Sh *ring, IsisL *I) {
    for (int i = 0; i < I->n; i++) {
        Isis *a = &I->r[i], *b = &I->r[(i + 1) % I->n];
        if (sec_flagged(st, ring, a->ent, a->t, b->ent, b->t)) { a->state = 3; continue; }
        if (st->flags & 0x20000) continue;
        a->len = sec_len(ring, a->ent, a->t, b->ent, b->t);
        P2 mid; sec_point(ring, a->ent, a->t, b->ent, b->t, a->len * 0.5, &mid);
        if (pt_covered(st, src, mid)) a->state = 3;
    }
}
/* FUN_1800c92b0 (l.108673-108738): a kept section next to two dropped ones (its predecessor and its partner's predecessor, or its successor and the partner of its successor) is dropped too */
static void isis_propagate(IsisL *I) {
    int n = I->n;
    for (;;) {
        int changed = 0, i = 0, j6 = 1, l = 0;
        if (n <= 0) return;
        do {
            Isis *r = I->r;
            if (r[l].state != 3) {
                int i4 = l == 0 ? n : i;
                if (r[i4 - 1].state == 3) {
                    int p = r[l].partner;
                    if (p == 0) p = n;
                    if (r[p - 1].state == 3) { r[l].state = 3; changed = 1; goto next; }
                }
                int k = j6 % n;
                if (r[k].state == 3 && r[r[k].partner].state == 3) {
                    r[l].state = 3; changed = 1;
                    if (1 < l) { i -= 2; j6 -= 2; l -= 2; }
                }
            }
        next:
            i++; j6++; l++;
        } while (i < n);
        if (!changed) return;
    }
}
/* FUN_1800c94b0 (l.108745-108799): a kept section whose partner is kept too is dropped when its point at a quarter of its length is covered or it meets the source (FUN_18014afd0) */
static void isis_quarter(const St *st, const Sh *src, const Sh *ring, IsisL *I) {
    for (int i = 0; i < I->n; i++) {
        Isis *a = &I->r[i];
        if (a->state != 0 || I->r[a->partner].state != 0) continue;
        Isis *b = &I->r[(i + 1) % I->n];
        P2 q; sec_point(ring, a->ent, a->t, b->ent, b->t, a->len * 0.25, &q);
        if (pt_covered(st, src, q) || sec_hits(src, ring, a->ent, a->t, b->ent, b->t)) a->state = 3;
    }
}
/* FUN_180146e20 (l.175722-176061) with param_7 0: the section (e1, t1) -> (e2, t2) appended to out, entities carried with their links; a degenerate first or last piece (< 1e-12) dropped */
static void sec_append(const Sh *s, int e1, double t1, int e2, double t2, Sh *out) {
    if (!s->n) return;
    t1 = e_clamp(t1); t2 = e_clamp(t2);
    int n = sec_count(s, e1, t1, e2, t2);
    if (n != 1) {
        const Ent *a = &s->e[e1];
        double rest = a->id == 'L' ? hypot(a->a[2] - ent_at(a, t1).x, a->a[3] - ent_at(a, t1).y) : a->id == 'A' ? fabs((a->a[4] - a->a[4] * t1) * a->a[2]) : 0;
        if ((a->id != 'L' && a->id != 'A') || rest < 1e-12) { n--; t1 = 0.0; if (e1 + 1 < s->n) e1++; else if (s->dir != 0) e1 = 0; }
        const Ent *b = &s->e[e2];
        double head = b->id == 'L' ? hypot(ent_at(b, t2).x - b->a[0], ent_at(b, t2).y - b->a[1]) : b->id == 'A' ? fabs(b->a[4] * t2 * b->a[2]) : 0;
        if ((b->id != 'L' && b->id != 'A') || head < 1e-12) { n--; t2 = 1.0; if (e2 > 0) e2--; else if (s->dir != 0) e2 = s->n - 1; }
    } else {
        const Ent *a = &s->e[e1];
        double piece = a->id == 'L' ? hypot(ent_at(a, t2).x - ent_at(a, t1).x, ent_at(a, t2).y - ent_at(a, t1).y) : a->id == 'A' ? fabs((t2 - t1) * a->a[4] * a->a[2]) : 0;
        if ((a->id != 'L' && a->id != 'A') || piece < 1e-12) n = 0;
    }
    if (n <= 0) return;
    n = sec_count(s, e1, t1, e2, t2);
    int wrap = e2 < e1 || (e1 == e2 && t2 < t1);
    int old = out->n;
    if (wrap) { for (int k = e1; k < s->n; k++) sh_push(out, &s->e[k]); for (int k = 0; k <= e2; k++) sh_push(out, &s->e[k]); }
    else for (int k = e1; k <= e2; k++) sh_push(out, &s->e[k]);
    int got = out->n - old;
    if (got == 1) {
        Ent *r = &out->e[old]; Ent *b = &s->e[e1];
        if (r->id == 'L') { P2 p0 = ent_at(b, t1), p1 = ent_at(b, t2); r->a[0] = p0.x; r->a[1] = p0.y; r->a[2] = p1.x; r->a[3] = p1.y; }
        else if (r->id == 'A') { double sa = b->a[3], sw = b->a[4]; r->a[3] = sa + sw * t1; r->a[4] = sw * (t2 - t1); }
        ent_box(r);
    } else if (got > 1) {
        Ent *f = &out->e[old], *l = &out->e[out->n - 1], *b0 = &s->e[e1], *b1 = &s->e[e2];
        if (f->id == 'L') { P2 q = ent_at(b0, t1); f->a[0] = q.x; f->a[1] = q.y; }
        else if (f->id == 'A') { f->a[3] += f->a[4] * t1; f->a[4] -= f->a[4] * t1; }
        if (l->id == 'L') { P2 q = ent_at(b1, t2); l->a[2] = q.x; l->a[3] = q.y; }
        else if (l->id == 'A') l->a[4] *= t2;
        ent_box(f); ent_box(l);
    }
}
/* FUN_1801239f0 (l.156732-156740) + FUN_1800c7f00 (l.108015-108050): the last shape is dropped when its box is under tol, or (bit 0x800 off, closed) when its area is under tol^2; otherwise its direction from the area FUN_180126ac0 */
static int shape_kept(const St *st, Sh *r) {
    double area = 0.0;
    sh_box(r);
    if (r->box[2] - r->box[0] < st->t1 && r->box[3] - r->box[1] < st->t1) return 0;
    r->dir = sh_orient_t(r, 0.001, &area);   /* FUN_1800c7f00 l.108029 */
    if ((st->flags >> 11) & 1) return 1;
    if (r->dir == 0) return 1;
    return !(area < st->t1 * st->t1);
}
/* FUN_1800c6c90 (l.107412-107552): one ring from the walk entries start.. (a run of consecutive record numbers = a section of the raw ring from its first record to the record after its last; the walk 0 .. 0 = the whole ring);
   a shape kept (FUN_1800c7f00) is appended to rings */
static void ring_from_walk(const St *st, const Sh *ring, IsisL *I, const int *w, int nw, int start, Sh **rings, int *nr) {
    Sh t; sh_init(&t);
    t.isl = ring->isl;
    int j = start, whole = 0;
    while (j < nw) {
        int last = j;
        I->r[w[j]].state = 2;
        while (last + 1 < nw && w[last + 1] - w[last] == 1) { I->r[w[last + 1]].state = 2; last++; }
        int a = w[j], b = (w[last] + 1) % I->n;
        if (a == 0 && b == 0) { whole = 1; break; }
        sec_append(ring, I->r[a].ent, I->r[a].t, I->r[b].ent, I->r[b].t, &t);
        j = last + 1;
    }
    if (whole) { free(t.e); sh_copy(&t, ring); }
    if (shape_kept(st, &t)) {
        *rings = realloc(*rings, (size_t)(*nr + 1) * sizeof(Sh));
        (*rings)[(*nr)++] = t;
    } else free(t.e);
}
/* FUN_1800c8fa0 (l.108552-108571): the walk closes when the record after its last entry, or that record's partner, is already on the walk */
static int walk_closes(const IsisL *I, const int *w, int nw, int *start) {
    int i = nw - 1, target = (w[i] + 1) % I->n, partner = I->r[target].partner;
    while (i >= 0 && w[i] != target && w[i] != partner) i--;
    *start = i;
    return i >= 0;
}
/* FUN_1800c7100 (l.107557-107738): the kept records by section length (FUN_1800c9050 l.108576-108668); from each, the walk that at each crossing changes to the partner when that section is kept, otherwise goes on; a walk that closes is a ring */
static const char *cut_rings_lookahead(const St *st, const Sh *ring, IsisL *I, Sh **rings, int *nr) {
    int n = I->n, m = 0; size_t sz = n > 0 ? (size_t)n : 1; int *list = malloc(sz * sizeof(int)), *w = malloc((2 * sz + 2) * sizeof(int));
    for (int i = 0; i < n; i++) if (I->r[i].state == 0) list[m++] = i;
    ms_qsort(list, (size_t)m, sizeof(int), I->r, isis_cmp_len);
    const char *why = NULL;
    for (int q = 0; q < m && !why; q++) {
        int u = list[q];
        if (I->r[u].state != 0) continue;
        int nw = 0, cur = u, steps = 0;
        for (;;) {
            if (nw >= 2 * n + 2 || ++steps > 4 * n + 8) { why = "a loop-removal walk that does not close (FUN_1800c7100)"; break; }
            w[nw++] = cur;
            int s;
            if (walk_closes(I, w, nw, &s)) { ring_from_walk(st, ring, I, w, nw, s, rings, nr); break; }
            int un = (cur + 1) % n, pt = I->r[un].partner, sp = I->r[pt].state;
            if (sp == 0) { cur = pt; continue; }
            int sn = I->r[un].state;
            if (sn == 0) { cur = un; continue; }
            if (sn == 2 && I->r[un].len < 1e-06) { cur = un; continue; }
            if (sp == 2 && I->r[pt].len < 1e-06) { cur = pt; continue; }
            break;
        }
    }
    free(list); free(w);
    return why;
}
/* FUN_1800c8750 (l.108285-108338): from the walk position, some entry is cut (state 3) and the section spanned has fewer than 6 entities */
static int walk_cuts(const Sh *ring, const IsisL *I, int pos, const int *a, int na) {
    int p = pos;
    while (p < na && I->r[a[p]].state != 3) p++;
    if (na <= p) return 0;
    const Isis *b0 = &I->r[a[pos]], *b1 = &I->r[(a[na - 1] + 1) % I->n];
    return sec_count(ring, b0->ent, e_clamp(b0->t), b1->ent, e_clamp(b1->t)) < 6;
}
/* FUN_1800c7670 (l.107743-107901), bit 0x20000: each record in order; a crossing back to an earlier position on the list removes the entries since, when FUN_1800c8750 agrees; then one ring from what is left */
static void cut_rings_limited(const St *st, const Sh *ring, IsisL *I, Sh **rings, int *nr) {
    int n = I->n, na = 0; size_t sz = n > 0 ? (size_t)n : 1; int *a = malloc(sz * sizeof(int)), *b = malloc(sz * sizeof(int));
    for (int i = 0; i < n; i++) {
        a[na++] = i;
        b[i] = na - 1;
        int pv = I->r[(i + 1) % n].partner;
        if (pv <= i) {
            int pos = b[pv];
            if (pos >= 0 && walk_cuts(ring, I, pos, a, na)) {
                for (int k = pos; k < na; k++) b[a[k]] = -1;
                na = pos;
            }
        }
    }
    ring_from_walk(st, ring, I, a, na, 0, rings, nr);
    free(a); free(b);
}
/* ---- PtInSh: the point's position against a shape ---- */
/* FUN_180144730 (l.174436-174460): on the entity within tol */
static int pt_on_ent(P2 p, const Ent *e, double tol) {
    if (p.x <= e->box[0] - tol || e->box[2] + tol <= p.x || p.y <= e->box[1] - tol || e->box[3] + tol <= p.y) return 0;
    P2 q = ent_nearest(e, p);
    return (p.y - q.y) * (p.y - q.y) + (p.x - q.x) * (p.x - q.x) <= tol * tol;
}
static unsigned char band_flag(double y, double lo, double hi) { return lo <= y ? (hi < y ? 4 : 2) : 1; }
/* FUN_180145690 (l.174999-175324): an arc near the band split at its extreme y points into monotone pieces, each flagged by its end */
static void pis_arc(double lo, double hi, const Ent *e, Sh *o) {
    double sa = e->a[3], sw = e->a[4], g;
    for (g = sa; 3.141592653589793 < g; g += -6.283185307179586) ;
    for (; g < -3.141592653589793; g += 6.283185307179586) ;
    double d7 = 1.5707963267948966 - g;
    if (-3.141592653589793 <= d7) { if (3.141592653589793 < d7) d7 -= 6.283185307179586; } else d7 += 6.283185307179586;
    if (sw * d7 < 0.0) d7 += 0.0 <= d7 ? -6.283185307179586 : 6.283185307179586;
    double d2 = -1.5707963267948966 - g;
    if (-3.141592653589793 <= d2) { if (3.141592653589793 < d2) d2 -= 6.283185307179586; } else d2 += 6.283185307179586;
    if (sw * d2 < 0.0) d2 += 0.0 <= d2 ? -6.283185307179586 : 6.283185307179586;
    double d8 = d7;
    if (fabs(d2) <= fabs(d7) && fabs(d7) != fabs(d2)) { d8 = d2; d2 = d7; }
    Ent p; memset(&p, 0, sizeof p); p.id = 'A'; p.a[0] = e->a[0]; p.a[1] = e->a[1]; p.a[2] = e->a[2];
    if (!(fabs(d8) <= fabs(sw))) {   /* no extreme in the sweep: a copy of the arc, flagged by its end (l.175251-175322) */
        Ent c = *e; double ey = sin(c.a[4] + c.a[3]) * c.a[2] + c.a[1];
        c.ce = band_flag(ey, lo, hi);
        sh_push(o, &c);
        return;
    }
    p.a[3] = sa; p.a[4] = d8;
    double ey = sin(p.a[4] + p.a[3]) * p.a[2] + p.a[1];
    p.ce = band_flag(ey, lo, hi); ent_box(&p); sh_push(o, &p);
    double g2;
    for (g2 = p.a[4] + p.a[3]; 3.141592653589793 < g2; g2 += -6.283185307179586) ;
    for (; g2 < -3.141592653589793; g2 += 6.283185307179586) ;
    double rest = sw - d8;
    if (fabs(d2) <= fabs(sw) && fabs(sw) != fabs(d2)) {
        p.a[3] = g2; p.a[4] = 0.0 <= sw ? 3.141592653589793 : -3.141592653589793;
        ey = sin(p.a[4] + p.a[3]) * p.a[2] + p.a[1];
        p.ce = band_flag(ey, lo, hi); ent_box(&p); sh_push(o, &p);
        double g3;
        for (g3 = p.a[4] + p.a[3]; 3.141592653589793 < g3; g3 += -6.283185307179586) ;
        for (; g3 < -3.141592653589793; g3 += 6.283185307179586) ;
        rest -= p.a[4]; g2 = g3;
    }
    p.a[3] = g2; p.a[4] = rest;
    ey = sin(p.a[4] + p.a[3]) * p.a[2] + p.a[1];
    p.ce = band_flag(ey, lo, hi); ent_box(&p); sh_push(o, &p);
}
/* FUN_180143460 (l.173745-174030): the copy of the shape flagged by y bands around the point (1 below, 2 in the band, 4 above; skipped runs of entities wholly on one side, bridges where consecutive ends straddle a band edge); 1 = the point is on the shape */
static int pis_collect(P2 pt, const Sh *s, double tol, Sh *o) {
    double y = pt.y, lo = y - tol * 0.9, hi = y + tol * 0.9, lo2 = y - tol * 1.1, hi2 = tol * 1.1 + y;
    char prevf = 2;
    int n = s->n;
    const Ent *pv = &s->e[n - 1];
    for (int i = 0; i < n; i++) {
        const Ent *e = &s->e[i];
        int skip = 0;
        if (prevf == 4) {
            if (!(e->box[1] <= hi2) && !(pv->box[1] == hi2 || pv->box[1] < hi2)) skip = 1;
        } else if (prevf == 1 && e->box[3] <= lo2 && lo2 != e->box[3]) {
            if (!(lo2 == pv->box[3] || lo2 < pv->box[3])) skip = 1;
        }
        if (!skip) {
            if (pt_on_ent(pt, e, tol)) return 1;
            P2 a = ent_end(pv), b = ent_start(e);
            if ((a.y < lo && lo < b.y) || (b.y < lo && lo < a.y) || (a.y < hi && hi < b.y) || (b.y < hi && hi < a.y)) {
                Ent l; memset(&l, 0, sizeof l); l.id = 'L'; l.a[0] = a.x; l.a[1] = a.y; l.a[2] = b.x; l.a[3] = b.y;
                ent_box(&l);
                l.ce = lo <= b.y ? (hi < b.y ? 4 : 2) : 1;
                sh_push(o, &l);
                if (pt_on_ent(pt, &o->e[o->n - 1], tol)) return 1;
            }
            char id = e->id;
            if (id != 'A' || ((e->box[1] <= lo || hi <= e->box[1]) && (e->box[3] <= lo || hi <= e->box[3]) && (y <= e->box[1] || e->box[3] <= y))) {
                double ey = id == 'L' ? e->a[3] : id == 'A' ? sin(e->a[4] + e->a[3]) * e->a[2] + e->a[1] : e->a[1];
                unsigned char f = (lo < ey || lo == ey) ? ((hi <= ey && ey != hi) ? 4 : 2) : 1;
                unsigned char lf = o->n ? o->e[o->n - 1].ce : 2;
                if (f == 2 || f != lf) { Ent c = *e; c.ce = f; sh_push(o, &c); }
            } else pis_arc(lo, hi, e, o);
            prevf = o->n ? (char)o->e[o->n - 1].ce : 2;
        }
        pv = e;
    }
    return 0;
}
/* mcce::PtInSh (l.33-206): 1 on the shape (within tol), 2 inside (odd crossing count of the ray to the right), 4 outside */
static int pt_in_sh(P2 p, const Sh *s, double tol) {
    if (!s->n || p.x <= s->box[0] - tol || s->box[2] + tol <= p.x || p.y <= s->box[1] - tol || s->box[3] + tol <= p.y) return 4;
    Sh o; sh_init(&o);
    if (pis_collect(p, s, tol, &o)) { free(o.e); return 1; }
    int n = o.n;
    /* FUN_1801453d0 (l.174911-174944): when the last entity is in the band, start after the first entity that is not */
    if (n && !(o.e[n - 1].ce & 5))
        for (int i = 0; i < n; i++) if (o.e[i].ce & 5) {
            int st = (i + 1) % n;
            if (st > 0) { Ent *t = malloc((size_t)n * sizeof(Ent)); memcpy(t, o.e + st, (size_t)(n - st) * sizeof(Ent)); memcpy(t + (n - st), o.e, (size_t)st * sizeof(Ent)); memcpy(o.e, t, (size_t)n * sizeof(Ent)); free(t); }
            break;
        }
    Ent ray; memset(&ray, 0, sizeof ray); ray.id = 'L'; ray.a[0] = p.x; ray.a[1] = p.y; ray.a[2] = s->box[2] + 10.0; ray.a[3] = p.y; ent_box(&ray);
    int cnt = 0, i = 0;
    while (n > 0) {
        int u7 = i, u10 = i, u11 = i;
        unsigned char last = i == 0 ? o.e[n - 1].ce : o.e[i - 1].ce;   /* in_R9: the flag at the previous stop (the last entity's at the start) */
        unsigned char b = 0;
        if (i < n) {
            for (;;) {
                b = o.e[u7].ce;
                u10 = u11;
                if (b & 5) { if (b != last) break; u11 = u7 + 1; }
                u10 = u11;
                u7++;
                if (!(u7 < n)) break;
            }
        }
        if (u7 < n) {
            int hit;
            if (u10 == u7) { double ta[2], tb[2], pt4[4]; hit = ent_isect(1e-07, &ray, &o.e[u10], ta, tb, pt4) != 0; }   /* FUN_180144460 (l.174333-174383) */
            else {
                const Ent *e = &o.e[u10];
                double xe = e->id == 'L' ? e->a[2] : e->id == 'A' ? cos(e->a[4] + e->a[3]) * e->a[2] + e->a[0] : e->a[0];
                hit = p.x <= xe && xe != p.x;
            }
            if (hit) cnt++;
        }
        i = u7 + 1;
        if (!(i < n)) break;
    }
    free(o.e);
    return (cnt & 1) ? 2 : 4;
}
/* mcce::PtInSh on a caller's shape as it stands: entity boxes (pock_chn +0x40..+0x58) and the shape box (CSh +0x30..+0x48) as given, not
   recomputed - the DLL reads the stored boxes (l.95-99 the shape box, FUN_180143460 the entity boxes). For viewer/mcce_dyn.c's pt_in_sh. */
int mcce_pt_in_shape_ents(double x, double y, McceEnt *e, int n, const double box[4], double tol) {
    Sh s; sh_init(&s); s.e = e; s.n = n; s.cap = n; memcpy(s.box, box, sizeof s.box);
    P2 p = { x, y };
    return pt_in_sh(p, &s, tol);
}
int mcce_pt_in_shape(double x, double y, const McceShape *in, double tol) {   /* MCCEPtInBnd(6, ...): CopyPockBndsToShAr + UpdMinMaxShAr, then PtInSh */
    Sh s; sh_init(&s); P2 p = { x, y };
    for (int k = 0; k < in->n; k++) { Ent e = in->e[k]; e.ce = 0; ent_box(&e); sh_push(&s, &e); }
    sh_box(&s);
    int r = pt_in_sh(p, &s, tol);
    free(s.e);
    return r;
}
/* FUN_1801489a0 (l.176706-176764) + FUN_180148820 (l.176623-176703): the first PtInSh answer that is not 1 for the section's sample points (1/3, 2/3, 1 of each entity, the last entity's end point skipped); all on the shape: 1; no point: 4 */
static int sec_in_shape(const Sh *sh, int e1, double t1, int e2, double t2, int full, const Sh *S, double tol) {
    int n = (e2 < e1 || (e1 == e2 && t2 < t1) || full) ? e2 + 1 + (sh->n - e1) : e2 - e1 + 1;
    int cur = e1, any = 0;
    for (int k = 0; k < n; k++) {
        for (int st = 0; st < 3; st++) {
            if (st == 2 && !(k + 1 < n)) break;
            double lo = k == 0 ? t1 : 0.0, hi = k == n - 1 ? t2 : 1.0;
            double f = st == 0 ? 0.3333333333333333 : st == 1 ? 0.6666666666666666 : 1.0;
            P2 q = ent_at(&sh->e[cur], (hi - lo) * f + lo);
            any = 1;
            int r = pt_in_sh(q, S, tol);
            if (r != 1) return r;
        }
        cur = (cur + 1) % sh->n;
    }
    return any ? 1 : 4;
}
/* FUN_180133c70 (l.165530-165671), only whether any intersection exists (boxes within 1e-5, CalcEntIntersect tol 1e-5) */
static int shapes_contact(const Sh *a, const Sh *b) {
    if (!(a->box[0] - 1e-05 <= b->box[2] && b->box[0] <= a->box[2] + 1e-05 && a->box[1] - 1e-05 <= b->box[3] && b->box[1] <= a->box[3] + 1e-05)) return 0;
    for (int i = 0; i < a->n; i++) {
        const Ent *ea = &a->e[i];
        if (!(ea->box[0] - 1e-05 <= b->box[2] && b->box[0] <= ea->box[2] + 1e-05 && ea->box[1] - 1e-05 <= b->box[3] && b->box[1] <= ea->box[3] + 1e-05)) continue;
        for (int j = 0; j < b->n; j++) {
            const Ent *eb = &b->e[j];
            if (!(ea->box[0] - 1e-05 <= eb->box[2] && eb->box[0] <= ea->box[2] + 1e-05 && ea->box[1] - 1e-05 <= eb->box[3] && eb->box[1] <= ea->box[3] + 1e-05)) continue;
            double ta[2], tb[2], pt[4];
            if (ent_isect(1e-05, ea, eb, ta, tb, pt)) return 1;
        }
    }
    return 0;
}
/* ISAB, the 0x50-byte record of the intersection list of two shapes (FUN_180133c70 l.165530-165671, field names from FUN_180134d60's "Idx ptI iShA iEntA dPaA iShB iEntB dPaB iNxtB iPrvB"): point, entity / parameter on shape A and on shape B, next / previous in B's order */
typedef struct { P2 pt; int e_a; double t_a; int e_b; double t_b; int nxt_b, prv_b; } Isab;
/* comparators 0x180133630 (A entity, parameter; the shape index, always 0 here, compares first) / 0x180133680 (the same on B, through an index array) */
static int isab_cmp_a(const void *x, const void *y, const void *ctx) {
    (void)ctx; const Isab *a = x, *b = y;
    if (b->e_a > a->e_a) return -1;
    if (b->e_a < a->e_a) return 1;
    if (b->t_a > a->t_a) return -1;
    return b->t_a < a->t_a;
}
static int isab_cmp_b(const void *x, const void *y, const void *ctx) {
    const Isab *is = ctx, *a = &is[*(const int *)x], *b = &is[*(const int *)y];
    if (b->e_b > a->e_b) return -1;
    if (b->e_b < a->e_b) return 1;
    if (b->t_b > a->t_b) return -1;
    return b->t_b < a->t_b;
}
/* FUN_180133c70 (l.165530-165671): every intersection of the two shapes (boxes within 1e-5, CalcEntIntersect tol 1e-5, the results of an entity pair last to first), sorted along A; FUN_180133b30 (l.165483-165525) drops a record equal to the one before it (cyclically) within 1e-10, on the joint of two entities on both shapes (FUN_180134800 l.166105-166150, FUN_18014b720); FUN_1801349c0 (l.166155-166300) gives each its next and previous along B */
static Isab *isab_list(const Sh *a, const Sh *b, int *pn) {
    Isab *L = NULL; int n = 0, cap = 0;
    *pn = 0;
    if (!(a->box[0] - 1e-05 <= b->box[2] && b->box[0] <= a->box[2] + 1e-05 && a->box[1] - 1e-05 <= b->box[3] && b->box[1] <= a->box[3] + 1e-05)) return NULL;
    for (int i = 0; i < a->n; i++) {
        const Ent *ea = &a->e[i];
        double x0 = ea->box[0] - 1e-05, y0 = ea->box[1] - 1e-05, x1 = ea->box[2] + 1e-05, y1 = ea->box[3] + 1e-05;
        if (!(x0 <= b->box[2] && b->box[0] <= x1 && y0 <= b->box[3] && b->box[1] <= y1)) continue;
        for (int j = 0; j < b->n; j++) {
            const Ent *eb = &b->e[j];
            if (!(x0 <= eb->box[2] && eb->box[0] <= x1 && y0 <= eb->box[3] && eb->box[1] <= y1)) continue;
            double ta[2], tb[2], pt[4];
            int k = ent_isect(1e-05, ea, eb, ta, tb, pt);
            for (k--; k >= 0; k--) {
                if (n == cap) { cap = cap ? 2 * cap : 16; L = realloc(L, (size_t)cap * sizeof *L); }
                L[n].pt.x = pt[2 * k]; L[n].pt.y = pt[2 * k + 1]; L[n].e_a = i; L[n].t_a = ta[k]; L[n].e_b = j; L[n].t_b = tb[k]; L[n].nxt_b = L[n].prv_b = 0;
                n++;
            }
        }
    }
    if (!n) return L;
    ms_qsort(L, (size_t)n, sizeof *L, NULL, isab_cmp_a);
    for (int i = 0; i < n; i++) {
        int m = i == 0 ? n : i;
        if (m - 1 == i) continue;
        const Isab *p = &L[m - 1], *q = &L[i];
        if (!(fabs(p->pt.x - q->pt.x) <= 1e-10 && fabs(p->pt.y - q->pt.y) <= 1e-10)) continue;
        if (!(p->e_a == q->e_a || close_forward(a, p->e_a, p->t_a, q->e_a, q->t_a, 1e-10) || close_forward(a, q->e_a, q->t_a, p->e_a, p->t_a, 1e-10))) continue;
        if (!(p->e_b == q->e_b || close_forward(b, p->e_b, p->t_b, q->e_b, q->t_b, 1e-10) || close_forward(b, q->e_b, q->t_b, p->e_b, p->t_b, 1e-10))) continue;
        memmove(L + i, L + i + 1, (size_t)(n - i - 1) * sizeof *L);
        n--; i--;
    }
    if (n) {
        int *idx = malloc((size_t)n * sizeof(int));
        for (int i = 0; i < n; i++) idx[i] = i;
        ms_qsort(idx, (size_t)n, sizeof(int), L, isab_cmp_b);
        for (int k = 0; k < n; k++) { L[idx[k]].nxt_b = idx[k + 1 < n ? k + 1 : 0]; L[idx[k]].prv_b = idx[k ? k - 1 : n - 1]; }
        free(idx);
    }
    *pn = n;
    return L;
}
/* FUN_18013f550 (l.171440-171616): for each entity of a, a point 2e-5 on the shape's inner side of the entity's midpoint; the first one inside a whose PtInSh on b is not 1 decides: inside b (not 4) = 1 */
static int shape_inner_in(const Sh *a, const Sh *b) {
    for (int i = 0; i < a->n; i++) {
        const Ent *e = &a->e[i];
        P2 m; double tx, ty;
        if (e->id == 'L') { m.x = (e->a[0] + e->a[2]) * 0.5; m.y = (e->a[1] + e->a[3]) * 0.5; tx = e->a[2] - e->a[0]; ty = e->a[3] - e->a[1]; }
        else {
            double g = e->a[4] * 0.5 + e->a[3], r = e->a[2], sw = e->a[4];
            m.x = r * cos(g) + e->a[0]; m.y = r * sin(g) + e->a[1];
            tx = r * sin(g) * fabs(sw); ty = r * cos(g) * fabs(sw);
            if (0.0 < sw) tx = -tx; else if (sw < 0.0) ty = -ty; else tx = ty = 0.0;
        }
        double l = hypot(tx, ty);
        if (l < 1e-12) continue;
        tx /= l; ty /= l;
        P2 q;
        if (a->dir == 1) { q.x = -ty * 2e-05 + m.x; q.y = tx * 2e-05 + m.y; } else { q.x = ty * 2e-05 + m.x; q.y = -tx * 2e-05 + m.y; }
        if (pt_in_sh(q, a, 1e-05) != 2) continue;
        int r = pt_in_sh(q, b, 1e-05);
        if (r == 1) continue;
        return r != 4;
    }
    return 0;
}
/* FUN_18013e910 (l.171277-171434): *pa = 1 when a lies inside b, *pb = 1 when b lies inside a. Sections between consecutive intersections (the whole shape when there are none, an empty record, l.171327-171332) tested on the other shape (FUN_1801489a0), first a's along A, then b's along B; a wholly on b's boundary: the larger area is outside (FUN_180126ac0 tol 0.1); FUN_18013f550 for a boundary section against an outside one */
static void shape_in_shape3(const Sh *a, const Sh *b, int *pa, int *pb, int *pe);
static void shape_in_shape(const Sh *a, const Sh *b, int *pa, int *pb) { shape_in_shape3(a, b, pa, pb, NULL); }
/* pe = param_5: set when every section of both shapes lies on the other's boundary (l.171389-171391) */
static void shape_in_shape3(const Sh *a, const Sh *b, int *pa, int *pb, int *pe) {
    *pa = *pb = 0;
    if (pe) *pe = 0;
    if ((b->box[2] <= a->box[0] - 1e-07 && a->box[0] - 1e-07 != b->box[2]) || a->box[2] + 1e-07 < b->box[0] || (b->box[3] <= a->box[1] - 1e-07 && a->box[1] - 1e-07 != b->box[3]) || a->box[3] + 1e-07 < b->box[1]) return;
    int n; Isab *L = isab_list(a, b, &n);
    Isab empty; memset(&empty, 0, sizeof empty);
    Isab *R = n ? L : &empty; int m = n ? n : 1;
    int r1 = 1, r2 = 1;
    for (int i = 0; i < m; i++) {
        int j = i < m - 1 ? i + 1 : 0;
        r1 = sec_in_shape(a, R[i].e_a, R[i].t_a, R[j].e_a, R[j].t_a, i == j, b, 1e-05);
        if (r1 != 1) { if (r1 == 2) { *pa = 1; free(L); return; } break; }
    }
    for (int i = 0; i < m; i++) {
        int k = R[i].nxt_b;
        r2 = sec_in_shape(b, R[i].e_b, R[i].t_b, R[k].e_b, R[k].t_b, i == k, a, 1e-05);
        if (r2 != 1) { if (r2 == 2) { *pb = 1; free(L); return; } break; }
    }
    if (r1 == 1 && r2 == 1) {
        if (pe) *pe = 1;
        double a1 = 0, a2 = 0;
        sh_orient_t(a, 0.1, &a1); sh_orient_t(b, 0.1, &a2);
        if (a1 <= a2) *pa = 1; else *pb = 1;
    } else if (r1 == 1 && r2 == 4) {
        if (shape_inner_in(a, b)) *pa = 1;
    } else if (r2 == 1 && r1 == 4) {
        if (shape_inner_in(b, a)) *pb = 1;
    }
    free(L);
}
/* DetNestLvlShAr (l.7733-7792): the nesting tree of shapes */
typedef struct { Sh *sh; int prev, child, next; } NestNode;
typedef struct { NestNode n[64]; int root; int count; } NestTree;
static int nest_add(NestTree *T, int side, int at, Sh *sh) {   /* FUN_18013f4b0 (l.171230-171250) */
    int k = T->count++;
    T->n[k].sh = sh; T->n[k].prev = at; T->n[k].child = T->n[k].next = -1;
    if (at < 0) T->root = k; else if (side == 0) T->n[at].child = k; else T->n[at].next = k;
    return k;
}
static int nest_cmp(const void *x, const void *y, const void *ctx) {
    (void)ctx;   /* FUN_18013e780: closed shapes first, open last, otherwise equal */
    int az = (*(Sh *const *)x)->dir == 0, bz = (*(Sh *const *)y)->dir == 0;
    if (!az) return bz ? -1 : 0;
    return bz ? 0 : 1;
}
static void nest_walk(NestTree *T, int head, int level, Sh **out, int *no) {   /* FUN_18013fff0 (l.172022-172115) */
    for (int k = head; k >= 0; k = T->n[k].next) {
        T->n[k].sh->isl = level;
        if (T->n[k].child >= 0) nest_walk(T, T->n[k].child, level + 1, out, no);
        if (!(level & 1)) out[(*no)++] = T->n[k].sh;
    }
    if (level & 1) for (int k = head; k >= 0; k = T->n[k].next) out[(*no)++] = T->n[k].sh;
}
/* DetNestLvlShAr + FUN_18013e3e0 (l.171120-171245), with FUN_1801402a0 (l.172216-172300) in the descent (a node containing the shape: descend into it; nodes it contains are moved under it, FUN_18013fec0 l.171960-172020): *slist is reordered, shape's isl = its nesting depth. Returns 0, or -1 if there are more shapes than the tree holds */
static int det_nest(Sh **slist, int n) {
    if (n <= 0) return 0;
    if (n == 1) { slist[0]->isl = 0; return 0; }
    if (n > 64) return -1;
    ms_qsort(slist, (size_t)n, sizeof(Sh *), NULL, nest_cmp);
    NestTree T; T.root = -1; T.count = 0;
    Sh **out = malloc((size_t)n * sizeof(Sh *));
    for (int i = 0; i < n; i++) {
        int cont[64], nc = 0, node = T.root, parent = -1;
        while (node >= 0) {
            nc = 0;
            int down = -1;
            for (int k = node; k >= 0; k = T.n[k].next) {
                int a = 0, b = 0;
                shape_in_shape(T.n[k].sh, slist[i], &a, &b);
                if (a < b) { down = k; nc = 0; break; }
                if (b < a) cont[nc++] = k;
            }
            if (down < 0) break;
            parent = down; node = T.n[down].child;
        }
        if (!nc) {
            if (node < 0) nest_add(&T, 0, parent, slist[i]);
            else { int last = node; while (T.n[last].next >= 0) last = T.n[last].next; nest_add(&T, 1, last, slist[i]); }
        } else {
            int last = node; while (T.n[last].next >= 0) last = T.n[last].next;
            int nw = nest_add(&T, 1, last, slist[i]), prev = -1, c = 0;
            for (int k = node; k >= 0 && c < nc; ) {
                int nx = T.n[k].next;
                if (k == cont[c]) {
                    int p = T.n[k].prev;
                    if (p < 0) T.root = nx; else if (T.n[p].child == k) T.n[p].child = nx; else T.n[p].next = nx;
                    if (nx >= 0) T.n[nx].prev = p;
                    T.n[k].prev = -1; T.n[k].next = -1;
                    if (prev < 0) { T.n[nw].child = k; T.n[k].prev = nw; } else { T.n[prev].next = k; T.n[k].prev = prev; }
                    prev = k; c++;
                }
                k = nx;
            }
        }
    }
    int no = 0;
    nest_walk(&T, T.root, 0, out, &no);
    memcpy(slist, out, (size_t)no * sizeof(Sh *));
    free(out);
    return 0;
}
/* FUN_1800d4660 (l.114569-114591, island source) / FUN_1800d4720 (l.114596-114615, otherwise): whether the ring keeps. Direction by the ring's nesting (src island: even rings CCW, odd rings not; other sources: CW), no contact with the source (FUN_180122a40), and the containment test FUN_18013e910 */
static int ring_keep_lookahead(const Sh *src, const Sh *ring) {
    int a = 0, b = 0;
    if (src->isl) {
        if (!((ring->isl & 1) ? ring->dir != 1 : ring->dir == 1)) return 0;
        if (src->dir != 0 && ring->dir == 0) return 0;
        if (shapes_touch(src, ring)) return 0;
        if (ring->isl & 1) { shape_in_shape(src, ring, &a, &b); if (b) return 0; }
        return 1;
    }
    if (ring->dir != -1) return 0;
    if (shapes_touch(src, ring)) return 0;
    if (src->dir == 0) return 1;
    shape_in_shape(src, ring, &a, &b);
    return b != 0;
}

static void out_push(McceResult *r, Ent *e, int n, int dir, int isl) {
    r->s = realloc(r->s, (size_t)(r->n + 1) * sizeof(McceShape));
    r->s[r->n].e = e; r->s[r->n].n = n; r->s[r->n].dir = dir; r->s[r->n].isl = isl;
    r->n++;
}
/* FUN_18014fd50 (l.180573-180715) on a closed ring: the entity at k becomes the first */
static void rotate_to(Sh *s, int k) {
    if (k <= 0 || k >= s->n || s->dir == 0) return;
    Ent *t = malloc((size_t)s->n * sizeof(Ent));
    memcpy(t, s->e + k, (size_t)(s->n - k) * sizeof(Ent));
    memcpy(t + (s->n - k), s->e, (size_t)k * sizeof(Ent));
    memcpy(s->e, t, (size_t)s->n * sizeof(Ent));
    free(t);
}

/* ---- the side test of a tiny open-chain run ---- */
typedef struct { int ent; double t, p[2]; } ChnPos;   /* the position record of FUN_18013d280 / FUN_18006f1c0: +4 entity, +8 t, +0x10 point */
/* FUN_180021580 (l.16 of the block @ 180021580): the point at t */
static void e_at(const Ent *e, double t, double p[2]) {
    if (e->id == 'L') { p[0] = (e->a[2] - e->a[0]) * t + e->a[0]; p[1] = (e->a[3] - e->a[1]) * t + e->a[1]; }
    else if (e->id == 'A') { double g = t * e->a[4] + e->a[3]; p[0] = cos(g) * e->a[2] + e->a[0]; p[1] = sin(g) * e->a[2] + e->a[1]; }
    else { p[0] = e->a[0]; p[1] = e->a[1]; }
}
/* mcce::GetNearestEntPt (0x180004d31): FUN_1800ad500 for a line, GetNearestArcPt (0x180003b2f) for an arc; p, t */
static void e_near(const Ent *e, const double q[2], double p[2], double *t) {
    if (e->id == 'L') {
        double dx = e->a[2] - e->a[0], dy = e->a[3] - e->a[1], l2 = dy * dy + dx * dx;
        if (1e-14 <= l2) {
            double u = -(((e->a[1] - q[1]) * dy + (e->a[0] - q[0]) * dx) / l2);
            if (u < 0.0) u = 0.0; else if (1.0 < u) u = 1.0;
            p[0] = u * dx + e->a[0]; p[1] = u * dy + e->a[1]; *t = u;
        } else { p[0] = (e->a[0] + e->a[2]) * 0.5; p[1] = (e->a[1] + e->a[3]) * 0.5; *t = 0.5; }
        return;
    }
    if (e->id != 'A') { p[0] = e->a[0]; p[1] = e->a[1]; *t = 0.0; return; }
    double a3, sw = e->a[4];
    if (1.1920928955078125e-07 <= fabs(q[0] - e->a[0]) || 1.1920928955078125e-07 <= fabs(q[1] - e->a[1])) {
        double a = atan2(q[1] - e->a[1], q[0] - e->a[0]) - e->a[3];
        while (6.283185307179586 < a) a += -6.283185307179586;
        while (a < 0.0) a += 6.283185307179586;
        if (sw < 0.0) while (0.0 < a) a += -6.283185307179586;
        a3 = a;
        if (fabs(sw) <= fabs(a) && fabs(a) != fabs(sw)) {
            double h = 0.0 <= sw ? sw * 0.5 + 3.141592653589793 : sw * 0.5 - 3.141592653589793;
            a3 = sw;
            if (fabs(h) <= fabs(a) && fabs(a) != fabs(h)) a3 = 0.0;
        }
    } else a3 = sw * 0.5;
    p[0] = cos(a3 + e->a[3]) * e->a[2] + e->a[0]; p[1] = sin(a3 + e->a[3]) * e->a[2] + e->a[1];
    *t = fabs(sw) <= 1e-12 ? 0.0 : a3 / sw;
}
/* FUN_18013d280 (l.170477-170560): the nearest point on a shape: entities whose box is not farther than the best so far (ties: the later entity) */
static void sh_near(const Sh *s, const double q[2], ChnPos *r) {
    int bi = -1; double best = 0.0, bt = 0.0, bp[2] = { 0, 0 };
    for (int i = 0; i < s->n; i++) {
        const double *b = s->e[i].box; double px = q[0], py = q[1], d, x, y;
        if (px < b[0] || b[2] <= px) {
            x = px < b[0] ? b[0] : b[2];
            if (py < b[1] || b[3] <= py) { y = py < b[1] ? b[1] : b[3]; d = (py - y) * (py - y) + (px - x) * (px - x); }
            else d = (px - x) * (px - x);
        } else if (py < b[1]) d = (py - b[1]) * (py - b[1]);
        else if (py < b[3]) d = 0.0;
        else d = (py - b[3]) * (py - b[3]);
        if (bi == -1 || d <= best) {
            double p[2], t; e_near(&s->e[i], q, p, &t);
            d = (py - p[1]) * (py - p[1]) + (px - p[0]) * (px - p[0]);
            if (bi == -1 || d <= best) { bt = t; bp[0] = p[0]; bp[1] = p[1]; bi = i; best = d; }
        }
    }
    r->ent = bi; r->t = bt; r->p[0] = bp[0]; r->p[1] = bp[1];
}
/* FUN_18006f1c0 (l.62971-63070) with param_1 = 0: from *in forward by len along the shape; 0 = it ends before that (open shape) */
static int chn_forward(const Sh *s, double len, const ChnPos *in, ChnPos *o) {
    int ok = 1, it = 0, done = 0; double left = len < 0.0 ? 0.0 : len;
    *o = *in;
    do {
        double l = e_len(&s->e[o->ent]), rest = (1.0 - o->t) * l;
        if (left < rest) {
            if (1e-12 < l) {
                double v = left / l + o->t;
                o->t = v;
                if (0.0 <= v) { if (1.0 < v) o->t = 1.0; } else o->t = 0.0;
            }
            left = 0.0; done = 1; break;
        }
        o->ent++;
        left -= rest;
        if (s->n <= o->ent) {
            if (s->dir == 0) { o->t = 1.0; o->ent = s->n - 1; if (1e-16 < left) ok = 0; done = 1; break; }
            o->ent = 0;
        }
        o->t = 0.0;
        if (left < 0.0) { done = 1; break; }
        it++;
    } while (it <= s->n);
    if (!done) ok = 0;
    e_at(&s->e[o->ent], o->t, o->p);
    return ok;
}
/* FUN_18006f4f0 (l.63073-63170) with param_1 = 0: backward */
static int chn_backward(const Sh *s, double len, const ChnPos *in, ChnPos *o) {
    int ok = 1, it = 0, done = 0; double left = len;
    *o = *in;
    do {
        double l = e_len(&s->e[o->ent]);
        if (left < l * o->t && 1e-06 < l) { o->t -= left / l; left = 0.0; done = 1; break; }
        o->ent--;
        left -= l * o->t;
        if (o->ent < 0) {
            if (s->dir == 0) { o->ent = 0; o->t = 0.0; if (left <= 1e-16) { done = 1; break; } break; }
            o->ent = s->n - 1;
        }
        o->t = 1.0;
        if (left < 0.0) { done = 1; break; }
        it++;
    } while (it <= s->n);
    if (!done) ok = 0;
    e_at(&s->e[o->ent], o->t, o->p);
    return ok;
}
/* FUN_1800cd350 (l.110788-110930): the new open runs [from, n) of one ring: a run whose box diagonal is within 1e-4 is deleted when it lies on the wrong side of the source (the start of its first entity against the source's
   direction at the nearest point, over 0.001 forward, else backward; left of travel for d > 0); a run for which neither step is possible stays */
static void tiny_side_cull(double d, const Sh *src, Sh *out, int from, int *pn) {
    int want = 0.0 < d ? 1 : -1, n = *pn;
    for (int i = from; i < n; i++) {
        double dx = out[i].box[0] - out[i].box[2], dy = out[i].box[1] - out[i].box[3];
        if (dy * dy + dx * dx <= 1e-08 && out[i].n) {
            double q[2]; e_at(&out[i].e[0], 0.0, q);
            ChnPos a, b;
            sh_near(src, q, &a);
            if (!chn_forward(src, 0.001, &a, &b)) {
                ChnPos c = a;
                if (!chn_backward(src, 0.001, &c, &b)) continue;
                a = b; b = c;
            }
            double v = (b.p[0] - a.p[0]) * (q[1] - a.p[1]) - (q[0] - a.p[0]) * (b.p[1] - a.p[1]);
            int sgn = v <= 0.0 ? (0.0 <= v) - 1 : 1;
            if (sgn != want) { free(out[i].e); memmove(out + i, out + i + 1, (size_t)(n - i - 1) * sizeof(Sh)); n--; i--; }
        }
    }
    *pn = n;
}
/* FUN_1800d6180 (l.115609-115650) with FUN_1800d8b40 (l.117209-117285), the head of the tail FUN_1800d8d90: a shape whose box diagonal is within 1e-4 is deleted when it is empty or its first point lies within 1e-4 of another, larger shape */
static void tiny_cull(Sh *a, int n) {
    for (int i = 0; i < n; i++) {
        double dx = a[i].box[0] - a[i].box[2], dy = a[i].box[1] - a[i].box[3];
        if (!a[i].n || !(dy * dy + dx * dx <= 1e-08)) continue;
        double q[2]; e_at(&a[i].e[0], 0.0, q);
        int del = 0;
        for (int j = 0; j < n && !del; j++) {
            if (j == i || !a[j].n) continue;
            double bx = a[j].box[0] - a[j].box[2], by = a[j].box[1] - a[j].box[3];
            if (!(1e-08 <= by * by + bx * bx)) continue;
            ChnPos r; sh_near(&a[j], q, &r);
            if ((r.p[0] - q[0]) * (r.p[0] - q[0]) + (r.p[1] - q[1]) * (r.p[1] - q[1]) <= 1e-08) del = 1;
        }
        if (del) { a[i].n = 0; a[i].box[0] = a[i].box[1] = a[i].box[2] = a[i].box[3] = 0.0; }
    }
}

/* FUN_1800ce670 (l.111425-111450), an open source. The ring: RevSh when d > 0, FUN_1800da6a0 (rotate_start), FUN_1800ccf20 (l.110643-110680: first and last entity both marked 0x20: the start moves to just after the last unmarked entity);
   FUN_1800cd7e0 (l.110936-111110) cuts it into its runs of entities marked 0x20 (the forward half; it stops at once if the first entity is unmarked), each an open shape (dir 0, or the ring's when it is the whole ring);
   FUN_1800cd350 (l.110788-110930: tiny_side_cull below: a run whose box diagonal is within 1e-4 and that lies on the wrong side of the source is deleted); FUN_1800cead0 (l.111588-111648) orders the shapes (one shape: nothing to order); FUN_1800d8d90 the tail. */
static int piece_key_cmp(const void *x, const void *y, const void *ctx) {
    (void)ctx;
    int a = *(const int *)x, b = *(const int *)y;
    return a < b ? -1 : a > b;
}
static const char *open_pieces(double d, const Sh *src, Sh ring, Sh **pout, int *pnout) {
    if (d > 0) sh_rev(&ring);
    rotate_start(&ring);
    if (ring.n > 1 && (ring.e[0].ce & 0x20) && (ring.e[ring.n - 1].ce & 0x20)) {
        int i = ring.n - 1;
        while (i >= 0 && (ring.e[i].ce & 0x20)) i--;
        if (i >= 0) rotate_to(&ring, i + 1);
    }
    Sh *out = *pout;
    int nout = *pnout;
    const char *why = NULL;
    for (int i = 0; i < ring.n && !why; ) {
        int a = i;
        while (i < ring.n && (ring.e[i].ce & 0x20)) i++;
        if (i == a) break;   /* FUN_1800cd7e0 l.110973-110975 */
        Sh piece; sh_init(&piece);
        for (int k = a; k < i; k++) sh_push(&piece, &ring.e[k]);
        for (int k = 0; k < piece.n; k++) ent_box(&piece.e[k]);
        sh_box(&piece);
        piece.dir = piece.n == ring.n ? ring.dir : 0;
        out = realloc(out, (size_t)(nout + 1) * sizeof(Sh));
        out[nout++] = piece;
        while (i < ring.n && !(ring.e[i].ce & 0x20)) i++;
    }
    free(ring.e);
    if (!why) tiny_side_cull(d, src, out, *pnout, &nout);   /* FUN_1800ce670 l.111450: FUN_1800cd350 over this ring's runs */
    *pout = out; *pnout = nout;
    return why;
}
/* FUN_1800ce670's open branch over the result shapes of one source (the pieces of every shape together), then the order and the tail */
static const char *open_finish(St *st, Sh *out, int nout, const char *why, McceResult *res) {
    if (why) { for (int k = 0; k < nout; k++) free(out[k].e); free(out); return why; }
    /* FUN_1800cead0 (l.111588-111648): with more than one result shape they are ordered by the lowest source entity index of each (FUN_1800d9530; qsort, comparator 0x1800cd020: ascending); a shape without any source sorts last */
    if (nout > 1) {
        struct K { int key, idx; } *K = malloc((size_t)nout * sizeof *K);
        for (int k = 0; k < nout; k++) {
            K[k].key = 0x7fffffff; K[k].idx = k;
            for (int j = 0; j < out[k].n; j++) if (out[k].e[j].src_ent >= 0 && out[k].e[j].src_ent < K[k].key) K[k].key = out[k].e[j].src_ent;
        }
        ms_qsort(K, (size_t)nout, sizeof *K, NULL, piece_key_cmp);
        Sh *o2 = malloc((size_t)nout * sizeof(Sh));
        for (int k = 0; k < nout; k++) o2[k] = out[K[k].idx];
        free(K); free(out); out = o2;
    }
    /* FUN_1800d8d90 (l.117293-117338): the tail on each shape; a shape left empty is dropped */
    for (int k = 0; k < nout; k++) sh_box(&out[k]);
    tiny_cull(out, nout);                               /* FUN_1800d8d90 l.117306: FUN_1800d6180 */
    for (int k = 0; k < nout; k++) {
        if (!out[k].n) { free(out[k].e); continue; }
        tail_shape(st, &out[k]);
        if (!out[k].n) { free(out[k].e); continue; }
        out_push(res, out[k].e, out[k].n, out[k].dir, 0);
    }
    free(out);
    return NULL;
}

/* the region boolean of mcce_sobool.inc (included at the end), for ReconfigSelfIsectSh and FUN_1800d3d30 */
typedef struct { Sh **s; int n, cap; } SL;
static _Thread_local const char *g_so_refused;
static void sl_add(SL *l, Sh *s);
static void sl_fre(SL *l);
static void so_union_ar(double tol, const SL *in, SL *out);
static int so_isect2(double tol, const Sh *a, const Sh *b, SL *out);
/* FUN_180133130 (l.165180-165300): the input shape cut at its self-crossings: the crossing records FUN_18012f3b0 (mode 0); records in turn are put on a walk (for an open shape all but the last); as soon as the partner of the record after the walk's last entry is on the walk (FUN_180131740, l.164328-164348) the entries from there on form a closed piece (FUN_1800c6c90 = ring_from_walk) and leave the walk.
   For an open shape what is left on the walk is an open piece (FUN_180131240, l.164180-164312): the start, the sections of the remaining records, the end; its direction is cleared. The caller of FUN_180133130, ReconfigSelfIsectSh with bit 0x80000 (l.1018-1096): open pieces apart, SoPockUnionAr on the closed ones (one closed piece is left as it is, more than one: NOT PORTED, refused), the open ones appended after them. FUN_18012d040 (the end of FUN_1800da1c0, l.118040-118045) keeps closed shapes first and open ones last. */
static const char *self_split(Sh *tmp, double t1, Sh **plist, int *pn) {
    IsisL I; memset(&I, 0, sizeof I);
    sh_box(tmp);
    isis_build(tmp, 0, &I);
    if (!I.n) { free(I.r); return NULL; }
    St st; memset(&st, 0, sizeof st); st.t1 = t1; st.flags = 0x80020;
    int n = I.n, lim = tmp->dir == 0 ? n - 1 : n, nw = 0, *w = malloc((size_t)(n + 1) * sizeof(int));
    Sh *rings = NULL; int nr = 0, nkept = 0;
    for (int i = 0; i < lim; i++) {
        w[nw++] = i;
        int last = nw - 1, target = I.r[(w[last] + 1) % n].partner, k = last;
        while (k >= 0 && w[k] != target) k--;
        if (k >= 0) {
            ring_from_walk(&st, tmp, &I, w, nw, k, &rings, &nr);
            nw = k;
        }
    }
    nkept = nr;
    if (tmp->dir == 0) {
        Sh t; sh_init(&t);
        t.isl = tmp->isl;
        sec_append(tmp, 0, 0.0, I.r[0].ent, I.r[0].t, &t);
        for (int k = 0; k < nw; k++) sec_append(tmp, I.r[w[k]].ent, I.r[w[k]].t, I.r[w[k] + 1].ent, I.r[w[k] + 1].t, &t);
        sec_append(tmp, I.r[n - 1].ent, I.r[n - 1].t, tmp->n - 1, 1.0, &t);
        if (shape_kept(&st, &t)) {
            t.dir = 0;
            rings = realloc(rings, (size_t)(nr + 1) * sizeof(Sh));
            rings[nr++] = t;
        } else free(t.e);
    }
    free(w); free(I.r);
    /* ReconfigSelfIsectSh bit 0x80000 (l.1018-1096): +0x54 = 0 on every piece, the open ones (dir 0) moved apart, SoPockUnionAr(tol, closed
       pieces) (mcce_sobool.inc so_union_ar) replaces the closed ones, the open ones appended after, then every piece takes the parent's +0x54.
       One closed piece: the union gives it back (DetNestLvlShAr of one shape: level 0). */
    if (nkept > 1) {
        SL in = { 0 }, U = { 0 };
        for (int k = 0; k < nkept; k++) { rings[k].isl = 0; sl_add(&in, &rings[k]); }
        g_so_refused = NULL;
        so_union_ar(t1, &in, &U);
        free(in.s);
        if (g_so_refused) { const char *w2 = g_so_refused; sl_fre(&U); for (int k = 0; k < nr; k++) free(rings[k].e); free(rings); return w2; }
        Sh *r2 = malloc((size_t)(U.n + nr - nkept + 1) * sizeof(Sh)); int n2 = 0;
        for (int k = 0; k < U.n; k++) { r2[n2] = *U.s[k]; r2[n2++].isl = tmp->isl; free(U.s[k]); }
        for (int k = nkept; k < nr; k++) { r2[n2] = rings[k]; r2[n2++].isl = tmp->isl; }
        for (int k = 0; k < nkept; k++) free(rings[k].e);
        free(rings); free(U.s);
        rings = r2; nr = n2;
    }
    if (!nr) { free(rings); return NULL; }
    *plist = rings; *pn = nr;
    return NULL;
}

/* one input shape (after FUN_1800da1c0 and the split): FUN_1800cd040 ... FUN_1800ce670 */
static const char *offset_shape(Sh *ptmp, double d, unsigned fl, double ang, double t1, double t2, McceResult *res) {
    Sh tmp = *ptmp;
    ptmp->e = NULL;
    /* FUN_1800cd040 (l.110684-110781): the region offset e. Open chain (dir 0): e = |d| and the shape retraced: d > 0 RevSh first (l.110756-110762), FUN_18011d800 (l.153634-: the entities, then their reversed copies in reverse order), FUN_1800d98a0 (l.110772, 117684-117700) clears the source link +0x70 on the returning half, dir = 1 */
    int open_ = tmp.dir == 0, n0 = open_ ? tmp.n : 0;
    double e = open_ ? fabs(d) : tmp.dir == 1 ? (d <= 0 ? fabs(d) : -fabs(d)) : (d <= 0 ? -fabs(d) : fabs(d));
    /* FUN_1800da180 / FUN_1800d92f0 / FUN_1800da5e0 (l.117926-117547, 118050-118075): the frame (e > 0) makes this shape an island; outers are CW, islands CCW */
    int island = e > 0.0;
    Sh work; sh_copy(&work, &tmp);
    if (open_) {
        if (d > 0) sh_rev(&work);
        Sh back; sh_copy(&back, &work);
        sh_rev(&back);
        for (int k = 0; k < back.n; k++) { Ent b = back.e[k]; b.src_sh = b.src_ent = -1; sh_push(&work, &b); }
        free(back.e);
        work.dir = 1;
        sh_box(&work);
    }
    int want = island ? 1 : -1;
    work.revmark = 0;
    if (work.dir != want) { sh_rev(&work); work.revmark = 1; }
    work.isl = island; work.frame = 0;
    int rm = work.revmark;
    St st; memset(&st, 0, sizeof st);
    st.flags = fl; st.dist = fabs(e); st.t1 = t1; st.t2 = t2; st.ang_cos = cos(ang * 0.017453292519943295); st.n0 = n0;
    Sh ring;
    const char *why = NULL;
    sh_box(&tmp);
    /* the group test of FUN_1800d50d0 / FUN_1800d4fd0 (l.114883-114998): a shape's box (with a frame: the frame's) must be at least 1.97 |d| across */
    double bx = tmp.box[2] - tmp.box[0], by = tmp.box[3] - tmp.box[1];
    if (!island && sqrt(by * by + bx * bx) < 1.97 * st.dist) { free(tmp.e); free(work.e); return NULL; }
    if (work.n == 1 && work.e[0].id == 'A') {
        if (!raw_circle(&st, &work, &ring)) { free(tmp.e); free(work.e); return NULL; }
        /* the single-arc path adds the shape directly (FUN_1800dbce0 l.118785); only the reversal back applies */
        if (work.revmark) sh_rev(&ring);
        tail_shape(&st, &ring);   /* FUN_1800ce670 l.111484 */
        free(tmp.e); free(work.e);
        if (!ring.n) { free(ring.e); return NULL; }
        out_push(res, ring.e, ring.n, ring.dir, 0);
        return NULL;
    }
    why = raw_ring(&st, &work, &ring);
    if (why) { free(tmp.e); free(work.e); free(st.mit); return why; }
    int limited = (fl >> 17) & 1;
    /* FUN_1800d3d30 / FUN_1800d48b0: FUN_1800d4170 (l.114394-114429) sorts the rings by the source's island flag: a single island-derived ring with the frame alone, or outer-derived rings only, are moved to the result without the boolean; (more rings: refused below) */
    IsisL isis; memset(&isis, 0, sizeof isis);
    isis_build(&ring, limited, &isis);
    Sh *rings = NULL;
    int nr = 0;
    if (!isis.n) {
        /* FUN_1800c8da0 / FUN_1800c7f00 (l.107931, 107937-107975, 108013-108052): an empty ring, a shape smaller than tol, area under tol^2 */
        if (!flagged_longer(&ring) && shape_kept(&st, &ring)) {
            rings = malloc(sizeof(Sh));
            rings[0] = ring; nr = 1;
            ring.e = NULL;
        }
    } else {
        /* FUN_1800c7bc0 (l.107990-107999): the crossing sections are marked, the rings cut; with Infinite look ahead on: the marks spread and the quarter test, then FUN_1800c7100; off: FUN_1800c7670 */
        isis_mark(&st, &work, &ring, &isis);
        const char *w = NULL;
        if (!limited) {
            isis_propagate(&isis);
            isis_quarter(&st, &work, &ring, &isis);
            w = cut_rings_lookahead(&st, &ring, &isis, &rings, &nr);
        } else cut_rings_limited(&st, &ring, &isis, &rings, &nr);
        if (w) { free(isis.r); free(tmp.e); free(ring.e); free(work.e); free(st.mit); for (int i = 0; i < nr; i++) free(rings[i].e); free(rings); return w; }
    }
    /* FUN_1800c7bc0 l.107995 (look-ahead on) / FUN_1800d4400 (l.114481-114564): DetNestLvlShAr, the filter, and in the source's run of rings the even ring dropped with the odd rings before it (its children, FUN_18013fff0's order) */
    Sh **slist = NULL;
    int ns = nr;
    int work_island = work.isl;
    if (nr) {
        slist = malloc((size_t)nr * sizeof(Sh *));
        for (int i = 0; i < nr; i++) { sh_box(&rings[i]); slist[i] = &rings[i]; }
    }
    if (!limited && nr) {
        if (nr > 64 || det_nest(slist, nr) < 0) { free(isis.r); free(tmp.e); free(ring.e); free(work.e); free(st.mit); for (int i = 0; i < nr; i++) free(rings[i].e); free(rings); free(slist); return "too many result rings for the nesting tree (ours: 64)"; }
        sh_box(&work);
        for (int i = 0; i < ns; i++) {
            if (ring_keep_lookahead(&work, slist[i])) continue;
            int st_i = i;
            if (work.isl && !(slist[i]->isl & 1)) { int j = i - 1; while (j >= 0 && (slist[j]->isl & 1)) j--; st_i = j + 1; }
            int cnt = i - st_i + 1;
            for (int k = st_i; k < st_i + cnt; k++) { free(slist[k]->e); slist[k]->e = NULL; slist[k]->n = 0; }
            memmove(slist + st_i, slist + st_i + cnt, (size_t)(ns - st_i - cnt) * sizeof(Sh *));
            ns -= cnt; i = st_i - 1;
        }
        if (det_nest(slist, ns) < 0) ns = 0;
    }
    free(isis.r);
    free(work.e);
    if (ring.e) free(ring.e);
    Sh src; sh_init(&src);                              /* the source shape of FUN_1800ce670 (param_4, the side test of FUN_1800cd350): the chain as it entered FUN_1800cd040, not the retraced copy */
    if (open_) { sh_copy(&src, &tmp); for (int k = 0; k < src.n; k++) ent_box(&src.e[k]); }
    free(tmp.e);
    /* FUN_1800d3d30 (l.114214-114389) for an island source: FUN_1800d4170 splits the rings by the source's island flag; the frame's ring is
       the only outer-derived one (FUN_1800d48b0 / the single frame of FUN_1800d92f0; here a stand-in frame box, ours: only its containing
       everything matters - the real ring is dropped in FUN_1800ce670). One island-derived ring: L60 = it (AppendByMove). Two or more:
       SoPockUnionAr(t1, rings) -> L60 (l.114289; mcce_sobool.inc so_union_ar, DLL-exact on the sobool unit oracle), FUN_1800d42a0
       (l.114432-114478) moves the shapes with +0x54 != 0 to L88, and when there are any FUN_18015b1c0 (l.186989-187020: FUN_1801638f0
       for every frame x L88 shape, so_isect2) replaces L88. The frame alone (+0x90 bit 1, l.114306): Ld8 = L60 + frame, then + L88
       (l.114313), DetNestLvlShAr (l.114314). */
    if (work_island && ns > 1) {
        SL in = { 0 }, U = { 0 }, odd = { 0 }, I = { 0 };
        for (int i = 0; i < ns; i++) sl_add(&in, slist[i]);
        g_so_refused = NULL;
        so_union_ar(st.t1, &in, &U);
        free(in.s);
        if (g_so_refused) { const char *w = g_so_refused; sl_fre(&U); free(src.e); free(st.mit); for (int i = 0; i < nr; i++) free(rings[i].e); free(rings); free(slist); return w; }
        Sh frame; sh_init(&frame); frame.dir = -1; frame.frame = 1;
        double bx0 = 1e300, by0 = 1e300, bx1 = -1e300, by1 = -1e300;
        for (int i = 0; i < ns; i++) { bx0 = fmin(bx0, slist[i]->box[0]); by0 = fmin(by0, slist[i]->box[1]); bx1 = fmax(bx1, slist[i]->box[2]); by1 = fmax(by1, slist[i]->box[3]); }
        double m = 1.0 + (bx1 - bx0) + (by1 - by0);
        double rx[4] = { bx0 - m, bx0 - m, bx1 + m, bx1 + m }, ry[4] = { by0 - m, by1 + m, by1 + m, by0 - m };
        for (int i = 0; i < 4; i++) { Ent f; memset(&f, 0, sizeof f); f.id = 'L'; f.src_sh = f.src_ent = -1; f.a[0] = rx[i]; f.a[1] = ry[i]; f.a[2] = rx[(i + 1) % 4]; f.a[3] = ry[(i + 1) % 4]; ent_box(&f); sh_push(&frame, &f); }
        sh_box(&frame);
        for (int i = 0; i < U.n; i++) if (U.s[i]->isl != 0) { sl_add(&odd, U.s[i]); memmove(U.s + i, U.s + i + 1, (size_t)(U.n - i - 1) * sizeof(Sh *)); U.n--; i--; }   /* FUN_1800d42a0 */
        for (int j = 0; j < odd.n; j++) so_isect2(st.t1, &frame, odd.s[j], &I);   /* FUN_18015b1c0 */
        sl_fre(&odd);
        int k = U.n + 1 + I.n;
        Sh **l2 = malloc((size_t)k * sizeof(Sh *));
        int q = 0;
        for (int i = 0; i < U.n; i++) l2[q++] = U.s[i];
        l2[q++] = &frame;
        for (int i = 0; i < I.n; i++) l2[q++] = I.s[i];
        if (det_nest(l2, k) < 0) { free(l2); free(frame.e); sl_fre(&U); sl_fre(&I); free(src.e); free(st.mit); for (int i = 0; i < nr; i++) free(rings[i].e); free(rings); free(slist); return "too many result rings for the nesting tree (ours: 64)"; }
        for (int i = 0; i < nr; i++) free(rings[i].e);
        free(rings);
        rings = malloc((size_t)k * sizeof(Sh)); slist = realloc(slist, (size_t)k * sizeof(Sh *));
        int o = 0;
        for (int i = 0; i < k; i++) if (l2[i] != &frame) { rings[o] = *l2[i]; slist[o] = &rings[o]; o++; free(l2[i]); }
        nr = ns = o;
        free(U.s); free(I.s); free(frame.e); free(l2);
    }
    free(st.mit); st.mit = NULL;
    if (!ns) { free(src.e); for (int i = 0; i < nr; i++) free(rings[i].e); free(rings); free(slist); return NULL; }
    if (open_) {   /* the source is an open chain: FUN_1800ce670's open branch per shape, the pieces of all of them ordered together */
        Sh *out = NULL; int nout = 0;
        const char *w = NULL;
        for (int i = 0; i < ns && !w; i++) {
            Sh r = *slist[i]; slist[i]->e = NULL;
            w = open_pieces(d, &src, r, &out, &nout);
        }
        for (int i = 0; i < nr; i++) free(rings[i].e);
        free(rings); free(slist); free(src.e);
        return open_finish(&st, out, nout, w, res);
    }
    /* FUN_1800ce670 (l.111406-111494) on each result shape, in the list's order; FUN_1800cead0 (l.111588-111648) orders the shapes by lowest source entity, FUN_1800d8d90 the tail */
    {   Sh *o2 = malloc((size_t)ns * sizeof(Sh));
        for (int i = 0; i < ns; i++) o2[i] = *slist[i];
        free(slist); free(rings);
        rings = o2;
    }
    for (int i = 0; i < ns; i++) { if (rm) sh_rev(&rings[i]); rotate_start(&rings[i]); }
    if (ns > 1) {
        struct K { int key, idx; } *K = malloc((size_t)ns * sizeof *K);
        for (int k = 0; k < ns; k++) {
            K[k].key = 0x7fffffff; K[k].idx = k;
            for (int j = 0; j < rings[k].n; j++) if (rings[k].e[j].src_ent >= 0 && rings[k].e[j].src_ent < K[k].key) K[k].key = rings[k].e[j].src_ent;
        }
        ms_qsort(K, (size_t)ns, sizeof *K, NULL, piece_key_cmp);
        Sh *o3 = malloc((size_t)ns * sizeof(Sh));
        for (int k = 0; k < ns; k++) o3[k] = rings[K[k].idx];
        free(K); free(rings); rings = o3;
    }
    for (int i = 0; i < ns; i++) sh_box(&rings[i]);
    tiny_cull(rings, ns);                               /* FUN_1800d8d90 l.117306: FUN_1800d6180 */
    for (int i = 0; i < ns; i++) {
        if (!rings[i].n) { free(rings[i].e); continue; }
        tail_shape(&st, &rings[i]);
        if (!rings[i].n) { free(rings[i].e); continue; }
        out_push(res, rings[i].e, rings[i].n, rings[i].dir, rings[i].isl);
    }
    free(rings);
    return NULL;
}

/* FUN_180123320 (mcce_all.c @ 180123320): b is a reversed along the same path: same type, a's start on b's end, a's end on b's start, the midpoints together (1e-12 on the squared distances); two arcs must also have opposite sweeps unless a is shorter than 1e-6 */
static int e_reversed(const Ent *a, const Ent *b) {
    if (a->id != b->id) return 0;
    double p[2], q[2];
    e_at(a, 0.0, p); e_at(b, 1.0, q);
    if (!((p[1] - q[1]) * (p[1] - q[1]) + (p[0] - q[0]) * (p[0] - q[0]) <= 1e-12)) return 0;
    e_at(a, 1.0, p); e_at(b, 0.0, q);
    if (!((p[1] - q[1]) * (p[1] - q[1]) + (p[0] - q[0]) * (p[0] - q[0]) <= 1e-12)) return 0;
    e_at(a, 0.5, p); e_at(b, 0.5, q);
    if (!((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) <= 1e-12)) return 0;
    return !(a->id == 'A' && b->id == 'A' && !(a->a[4] * b->a[4] <= 0.0) && !(fabs(a->a[4] * a->a[2]) <= 1e-06));
}
/* FUN_1801230c0 (mcce_all.c @ 1801230c0): an even count above 1 and every entity has a reversed twin among the others */
static int sh_doubled(const Sh *s) {
    if (s->n <= 1 || (s->n & 1)) return 0;
    for (int i = 0; i < s->n; i++) {
        int j = 0;
        while (j == i || !e_reversed(&s->e[i], &s->e[j])) { j++; if (s->n <= j) return 0; }
    }
    return 1;
}

static McceResult contour_offset_impl(const McceShape *in, int nin, double d, unsigned flags, double ang, double gap, double t1, double t2, int cve);
McceResult mcce_contour_offset(const McceShape *in, int nin, double d, unsigned flags, double ang, double gap, double t1, double t2) {
    return contour_offset_impl(in, nin, d, flags, ang, gap, t1, t2, 0);
}
/* the CVe<CSh*> overload called on a list made in the engine (export l.8571-8807: AppendByCopy keeps each shape's +0x50, so an
   open shape (dir 0) stays open without flag bit 1; FUN_1800ce9a0 l.111497 still gives a doubled-up shape the sign of d) */
McceResult mcce_contour_offset_sh(const McceShape *in, int nin, double d, unsigned flags, double ang, double gap, double t1, double t2) {
    return contour_offset_impl(in, nin, d, flags, ang, gap, t1, t2, 1);
}
static McceResult contour_offset_impl(const McceShape *in, int nin, double d, unsigned flags, double ang, double gap, double t1, double t2, int cve) {
    McceResult res = { NULL, 0, NULL };
    if (nin < 1 || !in[0].n) { res.s = calloc(1, sizeof(McceShape)); return res; }
    (void)t2;
    unsigned fl = flags & 0xffffffaf;                /* l.8686-8689 */
    if ((flags >> 17) & 1) fl = flags & 0xffffff8f;
    Sh tmp; sh_init(&tmp);
    for (int k = 0; k < in[0].n; k++) { Ent e = in[0].e[k]; e.ce = 0; e.src_sh = 0; e.src_ent = k; ent_box(&e); sh_push(&tmp, &e); }
    if (sh_doubled(&tmp)) tmp.dir = d <= 0.0 ? 1 : -1;
    else if (cve) tmp.dir = in[0].dir;
    else tmp.dir = in[0].dir == 1 ? 1 : -1;
    tmp.isl = 0;   /* FUN_1800ce9a0 l.111497 (before FUN_1800da1c0): a doubled-up shape (FUN_1801230c0) takes its direction from the sign of d; otherwise CopyPockBndsToShAr: only the bound's ccw byte (l.425-560): an open chain is told by the closedness test below */
    /* FUN_1800d99e0 (l.117732-117760), bit 0x20000 off (FUN_1800da1c0 l.117997-118000): a shape all of whose entities equal the first (FUN_1800d9b40 l.117765-117891: same type, ends within 1e-6, for arcs centre within 1e-6 and sweeps of one sign) is reduced to it */
    if (!(fl & 0x20000) && tmp.n > 1) {
        int all = 1;
        for (int i = 1; i < tmp.n && all; i++) {
            const Ent *a = &tmp.e[i], *b = &tmp.e[0];
            P2 pa = ent_start(a), pb = ent_start(b), qa = ent_end(a), qb = ent_end(b);
            if (a->id != b->id || !(fabs(pa.x - pb.x) < 1e-06) || !(fabs(pa.y - pb.y) < 1e-06) || !(fabs(qa.x - qb.x) < 1e-06) || !(fabs(qa.y - qb.y) < 1e-06)) all = 0;
            else if (a->id == 'A' && (!(fabs(a->a[0] - b->a[0]) < 1e-06) || !(fabs(a->a[1] - b->a[1]) < 1e-06) || a->a[4] * b->a[4] < 0.0)) all = 0;
        }
        if (all) tmp.n = 1;
    }
    /* FUN_1800da1c0 with bit 1 (l.117956-117971): gap < -1e-7 or the shape not closed within gap -> open */
    if (fl & 1) {
        double x0, y0, x1, y1;
        e_start(&tmp.e[0], &x0, &y0); e_end(&tmp.e[tmp.n - 1], &x1, &y1);
        if ((gap < -1e-07 || !(hypot(x0 - x1, y0 - y1) <= gap)) && tmp.dir != 0) tmp.dir = 0;
    }
    /* FUN_18012c2b0 (l.161604-161721, tol 0.0001): an entity of length <= tol whose neighbours meet within tol is removed (n > 2) */
    if (fl & 1) for (int i = 0; i < tmp.n; i++) {
        Ent *e0 = &tmp.e[i];
        if (e_len(e0) <= 0.0001 && tmp.n > 2) {
            int ip = i == 0 ? tmp.n - 1 : i - 1, in_ = i < tmp.n - 1 ? i + 1 : 0;
            double x0, y0, x1, y1;
            e_end(&tmp.e[ip], &x0, &y0); e_start(&tmp.e[in_], &x1, &y1);
            if ((y1 - y0) * (y1 - y0) + (x1 - x0) * (x1 - x0) <= 0.0001 * 0.0001) {
                memmove(tmp.e + i, tmp.e + i + 1, (size_t)(tmp.n - i - 1) * sizeof(Ent));
                tmp.n--; i--;
            }
        }
    }
    if (fl & 1) close_gaps(&tmp, (fl & 0x20000) ? 1e-07 : 1e-10);   /* FUN_1800da1c0 (l.118012-118020) */
    sh_box(&tmp);
    /* FUN_1800da1c0 l.118021-118026, bit 0x20: FUN_180132ef0 (l.165098-165176) replaces the shape by the pieces of ReconfigSelfIsectSh (flags 0x80020), or leaves it */
    Sh *list = NULL;
    int nlist = 0;
    if (fl & 0x20) {
        const char *w = self_split(&tmp, t1, &list, &nlist);
        if (w) { free(tmp.e); res.refused = w; return res; }
    }
    if (!nlist) {
        list = malloc(sizeof(Sh));
        list[0] = tmp; tmp.e = NULL;
        nlist = 1;
    } else { free(tmp.e); tmp.e = NULL; }
    res.s = calloc(1, sizeof(McceShape));
    const char *why = NULL;
    for (int k = 0; k < nlist && !why; k++) {
        /* the loop of the export (l.8730-8769), one shape at a time (the results are appended in the list's order) */
        if (fabs(d) < 1e-06) { out_push(&res, list[k].e, list[k].n, list[k].dir, list[k].isl); list[k].e = NULL; }   /* l.8748-8749: out = copy (no rotation, no reversal) */
        else why = offset_shape(&list[k], d, fl, ang, t1, t2, &res);
        list[k].e = NULL;
    }
    for (int k = 0; k < nlist; k++) free(list[k].e);
    free(list);
    if (why) { mcce_shapes_free(&res); res.refused = why; }
    return res;
}

void mcce_shapes_free(McceResult *r) {
    for (int i = 0; i < r->n; i++) free(r->s[i].e);
    free(r->s);
    r->s = NULL; r->n = 0;
}

#include "mcce_sobool.inc"
#include "mcce_offset.inc"
