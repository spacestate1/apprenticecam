/* mcbiarc.c - break_spline_arcs (mccore.dll @180160440): a planar spline as tangent arcs (biarcs), with lines where an arc pair cannot be made.
 * Helpers, all from the same dump: brk_spline_const_err (ported, mcsurf_build.c), FUN_180161780 (keep extending a flat stretch), FUN_180160d30 (the 2D normal), FUN_180161910 (the biarc of one stretch),
 * FUN_180161410 / FUN_180160f40 (line + arcs across an inflection), FUN_180160de0, FUN_180162360, FUN_180160c20, FUN_1801623a0, FUN_180160be0, FUN_180160ef0, solve_2x2, l_ints, proj_pt_ln, rotate_pt.
 * The hidden arguments of FUN_180161780 are from the disassembly (180160866-180160882): [RSP+0x20] = tol * 10.0 (param_5), [RSP+0x28] = XMM8 = 1e+38 (param_6), XMM3 = param_2 of break_spline_arcs (param_4).
 * FUN_180161910 is called with param_7 = param_8 = 0 (18016090a-1801608f3, MOV byte ptr ... 0x0): the branches on them are left out. Oracle: tools/tests/biarc_oracle.sh. */
#include <math.h>
#include <string.h>
#include "mcbiarc.h"

#define TAU_ 6.283185307179586

typedef struct { const CurveRec *c; double tol; } Ctx;
/* FUN_180160ce0 with view = Top: eval_gen_spline(ent, order, t) through world_to_2d (the caller has checked the spline lies in a plane parallel to the view) */
static void ev(const Ctx *k, int order, double t, double o[2]) {
    double d[3][3]; memset(d, 0, sizeof d); if (mcs_spline_eval_d(k->c, t, order, d)) { o[0] = o[1] = 0; return; } o[0] = d[order][0]; o[1] = d[order][1];
}
/* FUN_180160c20: |curvature| in the view */
static double curv(const Ctx *k, double t) {
    double a[2], b[2]; ev(k, 1, t, a); ev(k, 2, t, b);
    double l = sqrt(a[0] * a[0] + a[1] * a[1]);
    return fabs((b[1] * a[0] - b[0] * a[1]) / (l * l * l));
}
/* FUN_180162360: the radius, 5e+37 when the curvature is under tol */
static double rad(const Ctx *k, double t) { double c = curv(k, t); return k->tol < c ? 1.0 / c : 5e+37; }
/* FUN_180160ef0: flip a so that a . b >= 0 */
static void same_side(double a[2], const double b[2]) { if (b[1] * a[1] + a[0] * b[0] < 0.0) { a[0] = a[0] * -1.0; a[1] = a[1] * -1.0; } }
/* FUN_18000f270 */
static void unit(double a[2]) { double l = sqrt(a[0] * a[0] + a[1] * a[1]); if (l != 0.0) { a[0] /= l; a[1] /= l; } else { a[0] = a[1] = 0.0; } }
/* FUN_180160d30: the unit normal on the concave side at t */
static void normal_at(const Ctx *k, double t, double n[2]) {
    double d1[2], d2[2]; ev(k, 1, t, d1); n[0] = -d1[1]; n[1] = d1[0]; ev(k, 2, t, d2); same_side(n, d2); unit(n);
}
/* FUN_180160de0: Newton on the curvature, 4 steps from the middle; a step that leaves [t0, t1] goes back to the middle */
static double mid_t(const Ctx *k, double t0, double t1) {
    double m = (t0 + t1) * 0.5, x = m;
    for (int i = 0; i < 4; i++) {
        double a = curv(k, x - 0.01), b = curv(k, x), d = b - a;
        if (k->tol <= fabs(d) && fabs(d) != k->tol) x = x - (b * 0.01) / d;
        if (x < t0 || t1 < x) x = m;
    }
    return x;
}
/* FUN_180160be0: x not under tol and 0.2 < ref / x < 5 */
static int fair(double x, double ref, double tol) { if (x < tol) return 0; return 0.2 < ref / x && ref / x < 5.0; }
/* FUN_1801623a0: the arc's start and sweep from the two end radial directions (each from the centre: minus the normal), always positive */
static void arc_ang(const double a[2], const double b[2], McbPiece *o) {
    double ax = a[0] * -1.0, ay = a[1] * -1.0, bx = b[0] * -1.0, by = b[1] * -1.0, s = 0.0, e = 0.0;
    if (1e-12 <= ay * ay + ax * ax) { s = atan2(ay, ax); if (s < 0.0) s += TAU_; }
    if (1e-12 <= by * by + bx * bx) { e = atan2(by, bx); if (e < 0.0) e += TAU_; }
    double sw = fabs(e - s); if (3.141592653589793 < sw) sw = TAU_ - sw;
    o->sw = sw; o->sa = 0.0 <= by * ax - bx * ay ? s : e;
}
/* l_ints (mccore_filter.c:3957; the unbounded lines of the two segments) */
static int l_ints_(const double a[4], const double b[4], double o[2], double tol) {
    double dy1 = a[3] - a[1], dx1 = a[2] - a[0], l1 = dy1 * dy1 + dx1 * dx1; if (!(1e-24 <= l1)) return 0;
    double dx2 = b[0] - b[2], dy2 = b[1] - b[3], l2 = dx2 * dx2 + dy2 * dy2; if (!(1e-24 <= l2)) return 0;
    double det = dy2 * dx1 - dx2 * dy1, dot = dy2 * dy1 + dx2 * dx1, tt = tol * tol, ok;
    double n = (b[0] - a[0]) * dy2 - (b[1] - a[1]) * dx2;
    if (tt <= fabs(dot)) ok = tol <= fabs(det / dot); else ok = tt <= fabs(det);
    if (!ok) return 0;
    double t = n / det; o[0] = a[0] + dx1 * t; o[1] = a[1] + dy1 * t; return 1;
}
/* proj_pt_ln: t unclamped, false only for a zero line */
static int proj_ln_(const double p[2], const double l[4], double o[2], double *t) {
    double dx = l[2] - l[0], dy = l[3] - l[1], n = dy * dy + dx * dx; if (n == 0.0) return 0;
    double u = ((p[1] - l[1]) * dy + (p[0] - l[0]) * dx) / n; o[0] = (1.0 - u) * l[0] + u * l[2]; o[1] = (1.0 - u) * l[1] + u * l[3]; *t = u; return 1;
}
/* FUN_180161410: two arcs joined by a straight piece; flag 7 unless a chord end projects inside the line */
static int join_line(const double P0[2], const double P1[2], const double n0[2], const double n1[2], double R0, double R1, double dir[2], double L[4]) {
    double y0 = R0 * n0[1] + P0[1], x0 = R0 * n0[0] + P0[0], x1 = R1 * n1[0] + P1[0], y1 = R1 * n1[1] + P1[1];
    double ex = x0 - x1, ey = y0 - y1, l = sqrt(ex * ex + ey * ey), ux, uy;
    if (l == 0.0) ux = uy = 0.0; else { ux = ex / l; uy = ey / l; }
    double co = (R0 + R1) / l;
    if (!(fabs(co) < 0.995 || fabs(co) == 0.995)) return 0;
    double si = sqrt(1.0 - co * co);
    double ax = ux * co - uy * si, ay = uy * co + ux * si, bx = ux * co + uy * si, by = uy * co - ux * si;   /* rotate_pt about (0, 0): +si (local_d8), -si (local_e8) */
    if (!(n0[1] * ay + n0[0] * ax <= n0[1] * by + n0[0] * bx)) { dir[0] = ax; dir[1] = ay; } else { dir[0] = bx; dir[1] = by; }
    L[0] = x0 + -R0 * dir[0]; L[1] = -R0 * dir[1] + y0; L[2] = R1 * dir[0] + x1; L[3] = R1 * dir[1] + y1;
    double t = 0, f[2]; int ok = proj_ln_(P0, L, f, &t);
    if (!ok || t < 0.0 || 1.0 < t) { ok = proj_ln_(P1, L, f, &t); if (!ok || t < 0.0 || 1.0 < t) return 7; }
    return 0;
}
/* FUN_180160f40: one arc and a straight piece across an inflection whose two radii differ by more than 5 : 1 */
static int join_one(const double P0[2], const double P1[2], const double n0[2], const double n1[2], double R0, double R1, McbPiece *arc, double L[4], double tol) {
    double a[4] = { P0[0], P0[1], n0[1] + P0[0], -n0[0] + P0[1] }, b[4] = { P1[0], P1[1], n1[1] + P1[0], -n1[0] + P1[1] }, I[2];
    if (!l_ints_(a, b, I, tol)) return 0;
    double dn[2] = { n0[0] - n1[0], n0[1] - n1[1] }, L3[4] = { I[0], I[1], dn[0] + I[0], dn[1] + I[1] }, B[4], m[2];
    if (R1 <= R0) { B[0] = P1[0]; B[1] = P1[1]; B[2] = n1[0] + P1[0]; B[3] = n1[1] + P1[1]; memcpy(L, a, sizeof a); m[0] = n1[0]; m[1] = n1[1]; }
    else { B[0] = P0[0]; B[1] = P0[1]; B[2] = n0[0] + P0[0]; B[3] = n0[1] + P0[1]; memcpy(L, b, sizeof b); m[0] = n0[0]; m[1] = n0[1]; }
    double C[2]; if (!l_ints_(L3, B, C, tol)) return 0;
    double d = sqrt((B[1] - C[1]) * (B[1] - C[1]) + (B[0] - C[0]) * (B[0] - C[0])), v[2] = { C[0] - B[0], C[1] - B[1] };
    if (m[0] * v[0] + m[1] * v[1] < 0.0) { m[0] *= -1.0; m[1] *= -1.0; }
    double Q[2], f[2], t = 0; proj_ln_(C, L, Q, &t); double seg[4] = { P0[0], P0[1], P1[0], P1[1] };
    t = 0; proj_ln_(Q, seg, f, &t);
    if (0.0 < t && t < 1.0) { L[2] = Q[0]; L[3] = Q[1];
        if (0.0 < d) {
            double w[2] = { C[0] - Q[0], C[1] - Q[1] }; same_side(w, m); unit(w);
            arc->r = d; arc->cx = B[0] + m[0] * d; arc->cy = m[1] * d + B[1]; arc_ang(m, w, arc); return 4;
        }
    }
    return 0;
}
/* FUN_180161910: the biarc between t0 and t1 (n0 / n1: FUN_180160d30 at each end). Results as flags a1, a2, l */
static void biarc(const Ctx *k, double t0, double t1, const double n0[2], const double n1[2], McbPiece *A1, McbPiece *A2, McbPiece *LN, int *f1, int *f2, int *fl) {
    double P0[2], P1[2], tol = k->tol; ev(k, 0, t0, P0); ev(k, 0, t1, P1);
    double q = (t1 - t0) * 0.25, Rq0 = rad(k, q + t0), Rq1 = rad(k, t1 - q);
    *f1 = *f2 = *fl = 0;
    double dy = P1[1] - P0[1], dx = P1[0] - P0[0], len = sqrt(dx * dx + dy * dy), chord[2] = { dx, dy }, u[2];
    if (len == 0.0) u[0] = u[1] = 0.0; else { u[0] = dx / len; u[1] = dy / len; }
    double side = (n1[0] * dy - n1[1] * dx) * (n0[0] * dy - n0[1] * dx);
    double R1 = 0.0, R2 = 0.0, d[2] = { u[0], u[1] }; int fl_ = 0;
    if (0.0 <= side) {
        double x = n0[0] * Rq0 + n1[0] * Rq1, y = n0[1] * Rq0 + n1[1] * Rq1, l = sqrt(y * y + x * x);
        if (l == 0.0) { d[0] = d[1] = 0.0; } else { d[0] = x / l; d[1] = y / l; }
        fl_ = 0xb;
    } else {
        double tm = mid_t(k, t0, t1);
        R1 = rad(k, (t0 * 2.0 + tm) * 0.3333); R2 = rad(k, (t1 * 2.0 + tm) * 0.3333);
        double rt = R2 / R1;
        if (5.0 < rt || rt < 0.2) {
            double L[4]; int c = join_one(P0, P1, n0, n1, R1, R2, A1, L, tol);
            if (c) { LN->x1 = L[0]; LN->y1 = L[1]; LN->x2 = L[2]; LN->y2 = L[3]; *f1 = 1; *fl = 1; return; }
        }
        double L[4]; fl_ = join_line(P0, P1, n0, n1, R1, R2, d, L);
        if (!fl_) {
            double gy = -rt * n1[1] + n0[1], gx = -rt * n1[0] + n0[0], s = -(gy * u[1] + gx * u[0]);
            double hx = gx + u[0] * s, hy = gy + u[1] * s, h2 = rt + 1.0, l2 = sqrt(hx * hx + hy * hy), root = h2 * h2 - l2 * l2;
            if (root <= 0.0) goto line_only;
            root = sqrt(root);
            d[0] = u[0] * root + hx * -1.0; d[1] = u[1] * root + hy * -1.0; unit(d);
            double vy = d[1] * h2 + gy, vx = gx + d[0] * h2, mid[2]; normal_at(k, tm, mid);
            if (0.9962 < fabs(mid[1] * d[1] + mid[0] * d[0])) { fl_ = 0xb; goto have_flags; }
            double dv = sqrt(vx * vx + vy * vy); R1 = (len / dv) * 0.9; R2 = rt * R1;
            fl_ = join_line(P0, P1, n0, n1, R1, R2, d, L);
            if (!fl_) goto line_only;
        }
        if (fl_ & 4) { LN->x1 = L[0]; LN->y1 = L[1]; LN->x2 = L[2]; LN->y2 = L[3]; }
    }
have_flags:;
    same_side(d, n0); double nm[2] = { d[0], d[1] }; unit(nm); double nm0[2] = { nm[0], nm[1] };
    if (side < 0.0) { nm[0] *= -1.0; nm[1] *= -1.0; }
    if (fl_ & 8) {
        double a[2] = { n0[0] - nm0[0], n0[1] - nm0[1] }, b[2] = { nm[0] - n1[0], nm[1] - n1[1] }, det = a[0] * b[1] - a[1] * b[0];
        if (fabs(det) < 1e-12) goto line_only;   /* solve_2x2 refuses; the DLL then reads two uninitialised locals (local_e8 / local_e0) - no arc from them */
        R1 = (chord[0] * b[1] - b[0] * chord[1]) / det; R2 = (a[0] * chord[1] - a[1] * chord[0]) / det;
        if (!fair(R1, Rq0, tol) || !fair(R2, Rq1, tol)) goto line_only;
    }
    if (!(R1 <= tol) && (fl_ & 1)) { A1->r = R1; A1->cx = R1 * n0[0] + P0[0]; A1->cy = R1 * n0[1] + P0[1]; arc_ang(n0, nm0, A1); *f1 = 1; }
    if (!(R2 <= tol) && (fl_ & 2)) { A2->r = R2; A2->cx = R2 * n1[0] + P1[0]; A2->cy = R2 * n1[1] + P1[1]; arc_ang(nm, n1, A2); *f2 = 1; }
    *fl = (fl_ >> 2) & 1;
    return;
line_only:
    LN->x1 = P0[0]; LN->y1 = P0[1]; LN->x2 = P1[0]; LN->y2 = P1[1]; *fl = 1; *f1 = *f2 = 0;
}

/* FUN_180161780: go on extending while the stretch's end is inside the curve and the chord (>= tol * 10) stays under 1e+38 and flat to param_4 */
static int keep_going(const Ctx *k, double t0, double t, double e, double minch, double maxch) {
    double a, b; mcs_spline_t1t2(k->c, &a, &b);
    if (!(a + 1e-08 < t0 && a + 1e-08 < t && t0 < b - 1e-08 && t < b - 1e-08)) return 0;
    double P[3][3], Q[3][3], M[3][3]; if (mcs_spline_eval_d(k->c, t0, 0, P) || mcs_spline_eval_d(k->c, t, 0, Q)) return 0;
    double dx = Q[0][0] - P[0][0], dy = Q[0][1] - P[0][1], dz = Q[0][2] - P[0][2], ch = sqrt(dx * dx + dy * dy + dz * dz);
    if (!(minch <= ch)) return 0;
    if (maxch < ch) return 1;
    if (mcs_spline_eval_d(k->c, (t0 + t) * 0.5, 0, M)) return 0;
    double cx = (M[0][1] - P[0][1]) * dz - (M[0][2] - P[0][2]) * dy, cy = (M[0][2] - P[0][2]) * dx - dz * (M[0][0] - P[0][0]), cz = (M[0][0] - P[0][0]) * dy - (M[0][1] - P[0][1]) * dx, dd;
    if (fabs(ch) <= k->tol) dd = sqrt((P[0][0] - M[0][0]) * (P[0][0] - M[0][0]) + (P[0][1] - M[0][1]) * (P[0][1] - M[0][1]) + (P[0][2] - M[0][2]) * (P[0][2] - M[0][2]));   /* dist_pt_l_3d */
    else dd = fabs(sqrt(cy * cy + cx * cx + cz * cz) / ch);
    return dd < e;
}

int mcb_break_spline_arcs(const CurveRec *c, double err, double tol, McbPiece *out, int cap) {
    Ctx k = { c, tol }; double t1, t2; mcs_spline_t1t2(c, &t1, &t2);
    double t0 = t1; int n = 0;
    do {
        double t = t0;
        do { mcs_brk_spline_const_err(c, tol * 10.0, 1e+38, err, 0, tol, &t); } while (keep_going(&k, t0, t, err, tol * 10.0, 1e+38));
        double n0[2], n1[2]; normal_at(&k, t0, n0); normal_at(&k, t, n1);
        McbPiece a1, a2, l; memset(&a1, 0, sizeof a1); memset(&a2, 0, sizeof a2); memset(&l, 0, sizeof l); int f1, f2, fl;
        biarc(&k, t0, t, n0, n1, &a1, &a2, &l, &f1, &f2, &fl);
        /* break_spline_arcs :158-214: arc 1, arc 2, the line, each stored when length_ent > tol */
        if (f1 && tol < a1.r * a1.sw) { a1.kind = 1; if (n < cap) out[n] = a1; n++; }
        if (f2 && tol < a2.r * a2.sw) { a2.kind = 1; if (n < cap) out[n] = a2; n++; }
        if (fl && tol < hypot(l.x2 - l.x1, l.y2 - l.y1)) { l.kind = 0; if (n < cap) out[n] = l; n++; }
        if (!(t > t0)) break;   /* ours: brk_spline_const_err cannot return t0 on a healthy spline; guards a degenerate record */
        t0 = t;
    } while (t0 < t2 - 1e-06);
    return n;
}
