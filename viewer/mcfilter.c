/* mcfilter.c - the geometry filter: mccore.dll FUN_180948e60 (stream of chords into arcs / lines), FUN_180947310 (extend_buffer, the debug strings' name), FUN_18094bc40 (the output gt), with cr_a_3p, center_3pa, find_sweep_angle, l_ints; pt_on_arc (systypes).
 * Called by FUN_1800ee5d0 (filter_pock_boundaries, decomp: 1800ee905-1800ee936) with the limit 1000 (FUN_1809495b0), the tolerance, rmin = 0.001, rmax = 1500.0. Chords of an arc: ceil(|sw| / (2 acos((r - tol / 2) / r))).
 * z is always 0 here (gts are 2D); the z branches of FUN_180947310 are unreachable and left out (|z0 - z1| >= tol, :170). Feeds are all 0 (the "failed different feed" tests never fire).
 * Oracle: tools/tests/filter_oracle.sh (the real FUN_180948e60 under tools/peloader). */
#include <math.h>
#include <string.h>
#include "mcfilter.h"
#define TAU_ 6.283185307179586
typedef struct { int n, maxn, empty, kind; double p[1000][2]; double s[2], e[2], c[2]; } Fb;   /* DAT_18143dd50 count, DAT_18143dd70 points, DAT_18131891a, DAT_181443b50 (1 line, 2 CW, 3 CCW), b38 start, b70 end, b88 centre */
static void restr(double *a) { while (TAU_ < *a) *a -= TAU_; while (*a < 0.0) *a += TAU_; }   /* restrict_angle(0, 2 pi) */
static double ang_of(double y, double x) { if (1e-12 <= y * y + x * x) { double a = atan2(y, x); if (a < 0.0) a += TAU_; return a; } return 0.0; }
static int l_ints_(const double a[4], const double b[4], double o[2], double tol) {   /* l_ints */
    double dy1 = a[3] - a[1], dx1 = a[2] - a[0], l1 = dy1 * dy1 + dx1 * dx1; if (!(1e-24 <= l1)) return 0;
    double dx2 = b[0] - b[2], dy2 = b[1] - b[3], l2 = dx2 * dx2 + dy2 * dy2; if (!(1e-24 <= l2)) return 0;
    double det = dy2 * dx1 - dx2 * dy1, dot = dy2 * dy1 + dx2 * dx1, tt = tol * tol, ok, n = (b[0] - a[0]) * dy2 - (b[1] - a[1]) * dx2;
    if (tt <= fabs(dot)) ok = tol <= fabs(det / dot); else ok = tt <= fabs(det);
    if (!ok) return 0; double t = n / det; o[0] = a[0] + dx1 * t; o[1] = a[1] + dy1 * t; return 1;
}
typedef struct { double cx, cy, r, sa, sw; } Arc;
/* cr_a_3p / center_3pa: 0 when two points are under tol apart or the bisectors do not meet */
static int cr_a_3p(const double p1[2], const double p2[2], const double p3[2], Arc *o, double tol) {
    if (!(tol <= hypot(p2[1] - p1[1], p2[0] - p1[0])) || !(tol <= hypot(p3[1] - p2[1], p3[0] - p2[0])) || !(tol <= hypot(p1[1] - p3[1], p1[0] - p3[0]))) return 0;
    double m1[2] = { (p1[0] + p2[0]) * 0.5, (p1[1] + p2[1]) * 0.5 }, m2[2] = { (p3[0] + p2[0]) * 0.5, (p3[1] + p2[1]) * 0.5 };
    double A[4] = { m1[0], m1[1], -(p2[1] - p1[1]) + m1[0], (p2[0] - p1[0]) + m1[1] }, B[4] = { m2[0], m2[1], -(p3[1] - p2[1]) + m2[0], (p3[0] - p2[0]) + m2[1] }, c[2];
    if (!l_ints_(A, B, c, tol)) return 0;
    o->cx = c[0]; o->cy = c[1]; o->r = sqrt((p1[1] - c[1]) * (p1[1] - c[1]) + (p1[0] - c[0]) * (p1[0] - c[0]));
    double a1 = ang_of(p1[1] - c[1], p1[0] - c[0]), a2 = ang_of(p2[1] - c[1], p2[0] - c[0]), a3 = ang_of(p3[1] - c[1], p3[0] - c[0]); restr(&a1); o->sa = a1;
    double a = a1, b = a2, d = a3; restr(&a); restr(&b); restr(&d); b -= a; restr(&b); d -= a; restr(&d); if (d <= b) d -= TAU_; o->sw = d;   /* find_sweep_angle */
    return 1;
}
/* pt_on_arc (systypes 0x1800014e0, as viewer/cam2d_pock_zz.c zz_pt_on_arc) */
static int on_arc(const double p[2], const Arc *e, double tol) {
    if (!(fabs(hypot(e->cx - p[0], e->cy - p[1]) - e->r) <= tol)) return 0;
    double dx = p[0] - e->cx, dy = p[1] - e->cy, a = 0.0; if (1e-12 <= dy * dy + dx * dx) { a = atan2(dy, dx); if (a < 0) a += TAU_; }
    double lo, hi, da = 1e-12 < e->r ? tol / e->r : 0.0; if (e->sw <= 0) { hi = e->sa; lo = hi + e->sw; } else { lo = e->sa; hi = lo + e->sw; }
    while (hi + da < a) a -= TAU_; while (a < lo - da) a += TAU_;
    return lo - da <= a && a <= hi + da;
}
/* one point on the candidate arc: off the circle by more than tol, or outside the sweep -> 0 (FUN_180947310 :411-480, :489-556) */
static int arc_pt(const double q[2], const Arc *a, double tol, double stol) {
    double vx = q[0] - a->cx, vy = q[1] - a->cy, d = vx * vx + vy * vy; if (0.0 < d) d = sqrt(d);
    double d3 = a->r - d; if (tol <= fabs(d3) && fabs(d3) != tol) return 0;
    if (stol <= d) { vx = vx / d * a->r; vy = vy / d * a->r; } else { vx = a->r + a->cx; vy = a->cy; }
    double g = ang_of(vy, vx);
    if (a->sw <= 0.0) { if (a->sa < g) g -= TAU_; if (!(a->sa + a->sw <= g)) return 0; }
    else { if (g < a->sa) g += TAU_; if (a->sa + a->sw < g) return 0; }
    return !(tol < fabs(d3));   /* the "combined" distance: sqrt(dz^2 + d3^2), dz = 0 */
}
/* FUN_180947310 extend_buffer: the buffer plus the new end as one arc, or one line; 1 and the move updated, 0 not */
static int extend(Fb *b, const double E[2], double tol, double rmin, double rmax, double stol, double nctol) {
    if (b->empty) return 0;
    if (b->n + 1 == b->maxn) return 0;
    double d = hypot(E[0] - b->p[0][0], E[1] - b->p[0][1]);
    if (!(tol <= d) && b->kind >= 2 && b->kind <= 3) return 0;
    double mid[2] = { b->p[0][0], b->p[0][1] };
    for (int s = b->n - 1; s >= 0; s--) if (tol <= hypot(E[0] - b->p[s][0], E[1] - b->p[s][1])) { mid[0] = b->p[s][0]; mid[1] = b->p[s][1]; break; }
    if (tol <= hypot(mid[0] - b->p[0][0], mid[1] - b->p[0][1])) {
        Arc a; if (cr_a_3p(b->p[0], mid, E, &a, stol)) {
            if (!(a.r < rmin || rmax < a.r) && on_arc(mid, &a, nctol) && (fabs(a.sw) < 4.71238898038469 || fabs(a.sw) == 4.71238898038469)) {
                int ok = 1;
                for (int s = 0; s < b->n && ok; s++) {
                    const double *P = b->p[s], *Q = s == b->n - 1 ? E : b->p[s + 1]; double M[2] = { (Q[0] + P[0]) * 0.5, (Q[1] + P[1]) * 0.5 };
                    if (s != 0 && !arc_pt(P, &a, tol, stol)) ok = 0;
                    if (ok && !arc_pt(M, &a, tol, stol)) ok = 0;
                }
                if (ok) { double sa2 = a.sa + a.sw; b->kind = 0.0 < a.sw ? 3 : 2; b->c[0] = a.cx; b->c[1] = a.cy; b->e[0] = cos(sa2) * a.r + a.cx; b->e[1] = sin(sa2) * a.r + a.cy; b->s[0] = cos(a.sa) * a.r + a.cx; b->s[1] = sin(a.sa) * a.r + a.cy; return 1; }
            }
        }
    }
    double dx = E[0] - b->p[0][0], dy = E[1] - b->p[0][1], L = sqrt(dy * dy + dx * dx);   /* :649-714: the line */
    if (L < stol) return 0;
    double ux = dx / L, uy = dy / L;
    for (int s = 1; s < b->n; s++) {
        double dot = (b->p[s][1] - b->p[0][1]) * uy + (b->p[s][0] - b->p[0][0]) * ux; if ((dot <= -tol && -tol != dot) || (L + tol < dot)) return 0;
        double qx = ux * dot + b->p[0][0] - b->p[s][0], qy = dot * uy + b->p[0][1] - b->p[s][1]; if (tol < sqrt(qy * qy + qx * qx)) return 0;
    }
    b->kind = 1; b->e[0] = E[0]; b->e[1] = E[1]; b->s[0] = b->p[0][0]; b->s[1] = b->p[0][1]; return 1;
}
/* FUN_18094bc40: the gt of the current move */
static void emit(const Fb *b, McflGt *g, int cap, int *out, int *flag) {
    *flag = 0; if (cap < *out + 1) return;
    McflGt *q = &g[*out]; memset(q, 0, sizeof *q);
    if (b->kind == 1) { q->k = 'L'; q->a = b->s[0]; q->b = b->s[1]; q->c = b->e[0]; q->d = b->e[1]; }
    else if (b->kind == 3) { q->k = 'A'; q->a = b->c[0]; q->b = b->c[1];
        double dx = b->s[0] - b->c[0], dy = b->s[1] - b->c[1], t = dy * dy + dx * dx; q->c = sqrt(t); double a0 = 1e-12 <= t ? ang_of(dy, dx) : 0.0; q->d = a0; if (6.283185307178586 < q->d) q->d = 0.0;
        double ex = b->e[0] - b->c[0], ey = b->e[1] - b->c[1], a1; if (ey * ey + ex * ex < 1e-12) a1 = 0.0; else { a1 = atan2(ey, ex); if (a1 < 0.0) a1 += TAU_; if (6.283185307178586 < a1) a1 = 0.0; }
        q->e = fabs(a1 - q->d); if (a1 < q->d) q->e = TAU_ - q->e; }
    else if (b->kind == 2) { q->k = 'A'; q->a = b->c[0]; q->b = b->c[1];
        double dx = b->s[0] - b->c[0], dy = b->s[1] - b->c[1], t = dy * dy + dx * dx; q->c = sqrt(t); double a0 = 1e-12 <= t ? ang_of(dy, dx) : 0.0; q->d = a0; if (q->d < 1e-12) q->d = TAU_;
        double ex = b->e[0] - b->c[0], ey = b->e[1] - b->c[1], a1; if (ey * ey + ex * ex < 1e-12) a1 = TAU_; else { a1 = atan2(ey, ex); if (a1 < 0.0) a1 += TAU_; if (a1 < 1e-12) a1 = TAU_; }
        q->e = a1 - q->d; if (0.0 < q->e) q->e = -(TAU_ - q->e); if (6.283185307178586 < q->d) q->d = 0.0; }
    *flag = 1; (*out)++;
}
/* FUN_180948e60: the gts are read and rewritten in place (the output index can pass the input index, as in the DLL: the array is read live) */
int mcfl_filter(McflGt *g, int *n, double tol, double rmin, double rmax, double stol, double nctol) {
    static Fb b; int nin = *n, out = 0, flag = 0, first = 1; if (nin < 1) return 0;
    for (int i = 0; i < nin; i++) if (g[i].k == 'A') { double r = g[i].c; if (r < rmin) rmin = r; if (rmax < r) rmax = r; }
    b.maxn = 1000; b.empty = 1; b.kind = 1; b.n = 0;
    for (int i = 0; i < *n; i++) {
        double cur[2] = { 0, 0 }, next[2] = { 0, 0 }, c[2] = { 0, 0 }, R = 1.0, X = 0.0, da = 3.141592653589793; int steps = 0;
        if (g[i].k == 'L') { cur[0] = g[i].a; cur[1] = g[i].b; next[0] = g[i].c; next[1] = g[i].d; c[0] = cur[0]; c[1] = cur[1]; steps = 1; }
        else if (g[i].k == 'A') {
            R = g[i].c; double sw = g[i].e, st = acos((R - tol * 0.5) / R); steps = (int)(unsigned)ceil(fabs(sw) / (st * 2.0)); da = sw / (double)steps;
            cur[0] = cos(g[i].d) * R + g[i].a; cur[1] = sin(g[i].d) * R + g[i].b; X = da + g[i].d; next[0] = cos(X) * R + g[i].a; next[1] = sin(X) * R + g[i].b; c[0] = g[i].a; c[1] = g[i].b;
        } else continue;
        for (int k = 0; k < steps; k++) {
            int ok = extend(&b, next, tol, rmin, rmax, stol, nctol);
            if (!ok && !b.empty) { Fb t = b; int f; emit(&t, g, nin, &out, &f); }   /* the DLL does not test the flag here */
            if (!ok || b.empty) { b.n = 1; b.p[0][0] = cur[0]; b.p[0][1] = cur[1]; b.s[0] = cur[0]; b.s[1] = cur[1]; b.kind = 1; b.e[0] = next[0]; b.e[1] = next[1]; b.empty = 0; }
            first = 0;
            b.p[b.n][0] = next[0]; b.p[b.n][1] = next[1]; b.n++;
            X += da; cur[0] = next[0]; cur[1] = next[1];
            next[0] = cos(X) * R + c[0]; next[1] = sin(X) * R + c[1];
        }
    }
    if (!first) { emit(&b, g, nin, &out, &flag); if (!flag) return 0; b.empty = 1; }
    *n = out; return 1;
}
