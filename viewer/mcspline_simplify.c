/* mcspline_simplify.c - Simplify Spline's engine: mccadutil convert_ent_to_line_or_arc (0x1800539c0) and its fit FUN_180054020, with the mccore /
 * mastercam kernels it calls (decomp/wfacc/core_kernels2.c, decomp/mccore_inters.c, decomp/mccore_filter.c, decomp/surfkinds/evalsurf.c,
 * decomp/mastercam_findviewk.c, decomp/mastercam_viewtrans.c; disassembly of mccadutil 0x1800536f0..0x180054d40, the view 0 / argument
 * registers Ghidra lost in FUN_180054020). Pure numerics: no McamFile, no entity. Each block cites the function it ports; the magic numbers are its. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mcspline_simplify.h"
#include "mcsurf.h"

#define TWO_PI 6.283185307179586   /* 0x180177808 */
static double rd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static int ri(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
typedef double V[3];
static void cp3(double *a, const double *b) { a[0] = b[0]; a[1] = b[1]; a[2] = b[2]; }

/* the fields the engine reads / temporarily rewrites (ent +0x68 / +0x80 stored ends, +0x98 / +0xa0 parameter range, curve_endpoints / curve_t1t2): a copy, so
   the record is never touched (convert_ent_to_line_or_arc's fill_in_ent(ent, eptr) restores the real ent the same way) */
typedef struct { double e0[3], e1[3], t1, t2; const CurveRec *c; } Rec;

/* ---- views: find_view_k 0x180337960 (mastercam), world_to_view 0x180341b30 / view_to_world 0x180341520, arc_ep_3d 0x18032d730 / systypes 0x180001210 ---- */
#define NVADD 64
typedef struct { const McsSimpView *st; int nst; McsSimpView add[NVADD]; int nadd, base, n_views; } Vt;
int mcam_std_view(int n, double m[9]);                  /* mcam_load.c */
static int view_mat(const Vt *v, int n, double m[9]) {  /* the view table entry: stored views first (mcspline.c view_to_world), then the reserved 1..8; views past the
                                                           table's start come from AddMCView */
    if (n > v->base && n - v->base <= v->nadd) { memcpy(m, v->add[n - v->base - 1].m, 72); return 1; }
    for (int k = 0; k < v->nst; k++) if (v->st[k].n == n) { memcpy(m, v->st[k].m, 72); return 1; }
    return mcam_std_view(n, m);
}
/* world_to_view (view < 2 or no table entry: copy); v[i] = m[i][0] w[0] + m[i][1] w[1] + m[i][2] w[2] */
static void w2v(const Vt *vt, int n, const double w[3], double v[3]) {
    double m[9], o[3];
    if (n < 2 || !view_mat(vt, n, m)) { cp3(o, w); }
    else for (int i = 0; i < 3; i++) o[i] = m[3 * i] * w[0] + m[3 * i + 1] * w[1] + m[3 * i + 2] * w[2];
    cp3(v, o);
}
/* view_to_world: w[k] = v[0] m[0][k] + v[1] m[1][k] + v[2] m[2][k] */
static void v2w(const Vt *vt, int n, const double v[3], double w[3]) {
    double m[9], o[3];
    if (n < 2 || !view_mat(vt, n, m)) { cp3(o, v); }
    else for (int k = 0; k < 3; k++) o[k] = v[1] * m[3 + k] + v[0] * m[k] + v[2] * m[6 + k];
    cp3(w, o);
}
/* make_view_k (mccore 0x180017080): the matrix of a view whose normal (row 3) is k: row 2 = v x k for the axis v of k's smallest component (the first
   of equals, of the 2 tests of the loop: sVar5 < 2, as the DLL; the DLL starts from the global v_z_3d = (0, 0, 0), which only then sets the axis: BSS, written by nothing), row 1 = row 2 x k;
   returns 0 when k is not unit (|k| < 0.5: collinear points) */
static int make_view_k(const double p[3], double m[9]) {
    double nx = p[0], ny = p[1], nz = p[2], d = sqrt(nx * nx + ny * ny + nz * nz);
    if (d == 0.0) { nz = 0; ny = 0; nx = 0; } else { nx /= d; ny /= d; nz /= d; }
    if (!(0.5 <= sqrt(ny * ny + nx * nx + nz * nz))) return 0;     /* the 3 points collinear: n = 0 */
    double k[3] = { nx, ny, nz }, best = 0x1.2ced32a16a1b1p+126;   /* 0x47d2ced32a16a1b1 */
    int imin = 0;
    for (int i = 0; i < 2; i++) {
        double a = k[i];
        if (i == 0 || (fabs(a) <= best && best != fabs(a))) { best = fabs(a); imin = i; }
    }
    double v[3] = { 0, 0, 0 }; v[imin] = 1.0;
    double y0 = v[2] * ny - v[1] * nz, y2 = v[1] * nx - v[0] * ny, y1 = v[0] * nz - v[2] * nx;
    d = sqrt(y1 * y1 + y0 * y0 + y2 * y2);
    if (d == 0.0) { y2 = 0; y0 = 0; y1 = 0; } else { y0 /= d; y1 /= d; y2 /= d; }
    double x0 = y1 * nz - y2 * ny, x1 = y2 * nx - y0 * nz, x2 = y0 * ny - y1 * nx;
    d = sqrt(x0 * x0 + x1 * x1 + x2 * x2);
    if (d == 0.0) { x0 = 0; x1 = 0; x2 = 0; } else { x0 /= d; x1 /= d; x2 /= d; }
    m[0] = x0; m[1] = x1; m[2] = x2; m[3] = y0; m[4] = y1; m[5] = y2; m[6] = nx; m[7] = ny; m[8] = nz;
    return 1;
}
/* find_view_k: the first view 1..n_views whose normal (row 3) is parallel to n within 1e-12 (|dot| - 1), else the best within 2e-7, else (create) make_view_k +
   AddMCView (a new view; mx33_match and the Planes dialog bookkeeping have no numeric effect here: the candidate failed the 2e-7 normal test first). The
   view table entry's row 3 is the entry's +0x60 pointer in the DLL (the unit normal the dot test needs). */
static void find_view_k(Vt *vt, const double n[3], int *view, int create, int *ok) {
    *ok = 0;
    double best = 0.0; int bv = -1;
    for (int s = 1; s <= vt->n_views; s++) {                   /* the reserved views 1..8, the part's, those made in this call, in number order */
        double m[9]; if (!view_mat(vt, s, m)) continue;        /* (the app's view numbers are not consecutive; the DLL's table is, and stops at an empty entry) */
        double d = m[7] * n[1] + n[0] * m[6] + m[8] * n[2];
        double e = fabs(fabs(d) - 1.0);
        if (e <= 1e-12) { *view = s; *ok = 1; return; }
        if (e < 2e-07 && (bv < 0 || e < fabs(fabs(best) - 1.0))) { bv = s; best = d; }
    }
    if (bv > -1) { *view = bv; *ok = 1; return; }
    if (create) {
        double m[9];
        if (!make_view_k(n, m)) return;                        /* *ok = 0: AddMCView not called */
        if (vt->nadd < NVADD) {
            vt->n_views++; McsSimpView *a = &vt->add[vt->n_views - vt->base - 1];
            if (vt->n_views - vt->base > vt->nadd) vt->nadd = vt->n_views - vt->base;
            a->n = vt->n_views; memcpy(a->m, m, sizeof m);
            *view = vt->n_views; *ok = 1;
        }
    }
}

/* restrict_angle (mccore 0x1804b4440) */
static void restrict_angle(double lo, double hi, double *a) {
    double d = *a;
    if (hi < d) { do d = d + -6.283185307179586; while (hi < d); *a = d; }
    d = *a;
    if (d < lo) { do d = d + 6.283185307179586; while (d < lo); *a = d; }
}
/* find_sweep_angle (mccore 0x1804b6880; FUN_180054cd0 of mccadutil is the same) */
static double sweep_angle(double a, double b, double c) {
    restrict_angle(0.0, TWO_PI, &a); restrict_angle(0.0, TWO_PI, &b); restrict_angle(0.0, TWO_PI, &c);
    b = b - a; restrict_angle(0.0, TWO_PI, &b);
    c = c - a; restrict_angle(0.0, TWO_PI, &c);
    if (c <= b) c = c - TWO_PI;
    return c;
}
static double ang_of(double dy, double dx) {                   /* the atan2 block of cr_a_3p / FUN_180054020: 1e-12 <= dx^2 + dy^2 else 0, [0, 2pi) */
    if (1e-12 <= dy * dy + dx * dx) { double a = atan2(dy, dx); if (a < 0.0) a = a + TWO_PI; return a; }
    return 0.0;
}

/* ---- 2D: t_on_line 0x1804b48a0, l_hv_ints 0x180002330, l_ints 0x1804b3480, center_3pa 0x1804b5200, cr_a_3p 0x1804b5c90 (mccore) ---- */
typedef struct { double x, y, r, a0, sw; } A2;
static void t_on_line(double systol, double *t, const double p[2], const double l[4]) {
    double dx = l[2] - l[0], dy = l[3] - l[1], d = dy * dy + dx * dx;
    if (systol * systol < d) *t = (dy * (p[1] - l[1]) + dx * (p[0] - l[0])) / d;
    else *t = 0.0;
}
static int l_hv_ints(int k, const double *a, const double *b, double out[2], double *ta, double *tb) {
    const double *p3, *p4;
    if (k == 2) { p3 = a + 3; p4 = a + 1; } else { p3 = a + 2; p4 = a; }
    double d1 = *p3, d2 = *p4;
    if (1e-12 <= fabs(d1 - d2)) {
        if (k == 1) { p3 = b + 3; p4 = b + 1; } else { p3 = b + 2; p4 = b; }
        double d7 = *p3 - *p4;
        if (1e-12 <= fabs(d7)) {
            if (k == 2) { p3 = b + 3; p4 = b + 1; } else { p3 = b + 2; p4 = b; }
            double d6 = sqrt((*p3 - *p4) * (*p3 - *p4) + d7 * d7);
            if (1e-12 <= d6 && 1.5e-10 <= fabs(d7 / d6)) {
                const double *b0 = k == 1 ? b + 1 : b, *a0 = k == 1 ? a + 1 : a;
                d7 = (*a0 - *b0) / d7;
                *tb = d7;
                out[0] = (b[2] - b[0]) * d7 + b[0];
                double *ov = k == 2 ? out + 1 : out;
                out[1] = (b[3] - b[1]) * *tb + b[1];
                const double *ax = k == 2 ? a + 1 : a;
                *ta = (*ov - *ax) / (d1 - d2);
                return 1;
            }
        }
    }
    return 0;
}
static void l_ints(double systol, const double *L1, const double *L2, double out[2], double *t1, double *t2, int *ok) {
    double d17 = L1[3] - L1[1], d22 = L1[2] - L1[0], d19 = d17 * d17 + d22 * d22;
    if (1e-24 <= d19) {
        if (1e-24 <= (d22 * d22) / d19) {
            if (1e-24 <= (d17 * d17) / d19) {
                double y1 = L2[1], y2 = L2[3], x1 = L2[0];
                double d4 = (L2[2] - x1) * (L2[2] - x1), d15 = (y2 - y1) * (y2 - y1), d20 = d15 + d4;
                if (1e-24 <= d20) {
                    if (1e-24 <= d4 / d20) {
                        if (1e-24 <= d15 / d20) {
                            double d5 = x1 - L1[0], d18 = systol * systol, d21 = x1 - L2[2], Y1 = L1[1];
                            *ok = 1;
                            double dy2 = y1 - y2, den = dy2 * d22 - d21 * d17, num = d5 * dy2 - (y1 - Y1) * d21, d16 = dy2 * d17 + d21 * d22;
                            int gt, eq;
                            if (d18 < fabs(d16) || d18 == fabs(d16)) { eq = systol == fabs(den / d16); gt = systol < fabs(den / d16); }
                            else { eq = d18 == fabs(den); gt = d18 < fabs(den); }
                            if (gt || eq) {
                                num = num / den; *t1 = num;
                                double ox = d22 * num, oy = d17 * num;
                                out[0] = L1[0] + ox; out[1] = oy + L1[1];
                                *t2 = ((y1 - Y1) * d22 - d5 * d17) / den;
                            } else {
                                *ok = 0;
                                double ln = sqrt(d21 * d21 + dy2 * dy2);
                                if (systol < fabs(ln)) {
                                    double dd = num / ln;
                                    int a = fabs(dd) <= systol, b = systol != fabs(dd);
                                    *ok = a && b;
                                    if (*ok) {
                                        double mn = 0x1.2ced32a16a1b1p+126;     /* the nearest end points of the two lines: FUN_1804b3480's double loop */
                                        int s8 = 1, s9 = 1;
                                        for (int i = 0; i < 2; i++) {
                                            double px = L1[2 * i], py = L1[2 * i + 1];
                                            for (int j = 0; j < 2; j++) {
                                                double d = sqrt((L2[2 * j + 1] - py) * (L2[2 * j + 1] - py) + (L2[2 * j] - px) * (L2[2 * j] - px));
                                                if (d <= mn && mn != d) { mn = d; s8 = j; s9 = i; }
                                            }
                                        }
                                        double oy2 = (L2[2 * s8 + 1] + L1[2 * s9 + 1]) * 0.5;
                                        out[0] = (L2[2 * s8] + L1[2 * s9]) * 0.5; out[1] = oy2;
                                        t_on_line(systol, t1, out, L1); t_on_line(systol, t2, out, L2);
                                    }
                                }
                            }
                        } else *ok = l_hv_ints(1, L2, L1, out, t2, t1);
                    } else *ok = l_hv_ints(2, L2, L1, out, t2, t1);
                } else *ok = 0;
            } else *ok = l_hv_ints(1, L1, L2, out, t1, t2);
        } else *ok = l_hv_ints(2, L1, L2, out, t1, t2);
    } else *ok = 0;
}
/* center_3pa (param_5 = the "too close" flag, set 1 first, unused by the caller here) */
static void center_3pa(double systol, const double *p1, const double *p2, const double *p3, double out[2], int *ok, int *parallel) {
    *ok = 0; *parallel = 0;
    double d4 = p2[1] - p1[1], d7 = p2[0] - p1[0], d8 = p2[0], d9 = p2[1], x1 = p1[0], y1 = p1[1];
    d4 = sqrt(d4 * d4 + d7 * d7);
    if (systol <= d4) {
        double x3 = p3[0], y3 = p3[1], d8b = x3 - d8, d9b = y3 - d9;
        d8b = sqrt(d9b * d9b + d8b * d8b);
        if (systol <= d8b) {
            double d2b = y1 - y3, d1b = x1 - x3;
            double dd = sqrt(d2b * d2b + d1b * d1b);
            if (systol <= dd) {
                double L1[4], L2[4];
                L1[0] = (p1[0] + p2[0]) * 0.5; double my1 = (p1[1] + p2[1]) * 0.5; L1[1] = my1;
                double my2 = (p3[1] + p2[1]) * 0.5, mx2 = (p3[0] + p2[0]) * 0.5;
                L2[0] = mx2; L2[1] = my2;
                L1[2] = -(p2[1] - p1[1]) + L1[0]; L1[3] = (p2[0] - p1[0]) + my1;
                L2[2] = -(p3[1] - p2[1]) + mx2; L2[3] = (p3[0] - p2[0]) + my2;
                double ta = 0, tb = 0;
                l_ints(systol, L1, L2, out, &ta, &tb, ok);
                *parallel = !*ok;
            }
        }
    }
}
static void cr_a_3p(double systol, const double *p1, const double *p2, const double *p3, A2 *a, int *ok) {
    double c[2] = { a->x, a->y };
    int par;
    center_3pa(systol, p1, p2, p3, c, ok, &par);
    a->x = c[0]; a->y = c[1];
    if (*ok) {
        a->r = sqrt((p1[1] - a->y) * (p1[1] - a->y) + (p1[0] - a->x) * (p1[0] - a->x));
        double s = ang_of(p1[1] - a->y, p1[0] - a->x);
        double x0 = a->x;
        double m = ang_of(p2[1] - a->y, p2[0] - x0);
        double e = ang_of(p3[1] - a->y, p3[0] - x0);
        restrict_angle(0.0, TWO_PI, &s);
        a->a0 = s;
        a->sw = sweep_angle(s, m, e);
    }
}

/* ---- FUN_1800538c0: round *v to a fraction k/s (s = 2..10) or an integer when within tol of it; a later fraction wins (the loop does not stop) ---- */
static void snap(double tol, double *v) {
    double f = floor(*v), d = *v - f;
    if (d < tol) { *v = f; return; }
    if (fabs(1.0 - d) < tol) { *v = f + 1.0; return; }
    for (short s = 2; s < 0xb; s++)
        for (short k = 1; k < s; k++) {
            double q = (double)(int)k * (1.0 / (double)(int)s);
            if (fabs(d - q) < tol) *v = q + f;
        }
}

/* eval_curve(ent, 0, t) of the record: parametric splines by mcs_curve_eval (eval_spline / eval_poly); NURBS by eval_nurbs 0x1805ae2d0 itself (mcspline.c's version is de Boor,
   not the DLL's arithmetic): nurb_interval 0x1805b0630, the basis FUN_1805aeb50, the sum over the deg+1 points (the record's dims: 1 2 z for dim 2, as z stays 0 here while the
   DLL leaves the caller's stale z: such records are not expected at +0xd1 = dim 3) */
static double knot(const uint8_t *r, int i) { return rd(r + 0xd8 + 8 * (size_t)i); }
static int nurb_interval(double t, const uint8_t *r, int nk, int deg) {
    if (t < knot(r, 0)) t = knot(r, 0);
    if (knot(r, nk - 1) < t) t = knot(r, nk - 1);
    int i3 = deg + 1, i4 = nk - i3, i1 = i4;
    for (int l = i3; (i1 = i4, l <= i4) && (i1 = i3, knot(r, l) - 1e-10 <= t); l++) i3++;
    i1 = i1 - 1;
    int res = i1;
    if (knot(r, i1 + 1) - knot(r, i1) <= 1e-10) {
        int j = nk - 1, ij = j;
        if (0 < ij) {
            for (;;) {
                res = ij;
                if (!(t <= knot(r, j) + 1e-10)) break;
                j--; ij--;
                if (j < 1) return i1;
            }
        }
    }
    return res;
}
static void nurbs_basis(double t, const uint8_t *r, int nk, int span, int deg, double *N) {   /* FUN_1805aeb50 (N has nk slots, the DLL's static buffer) */
    memset(N, 0, sizeof(double) * nk);
    N[span] = 1.0;
    for (int k = 1; k <= deg; k++) {
        int i0 = span - k < 0 ? 0 : span - k;
        for (int i = i0; i <= span; i++)
            if (i <= nk - 2 - k) {
                double a = knot(r, i), d12 = t - a, d11 = knot(r, k + i) - a, v;
                if (fabs(d11) < 1e-12 || fabs(d12) < 1e-12) { v = 0.0; N[i] = 0.0; }
                else { v = (d12 / d11) * N[i]; N[i] = v; }
                double kk = knot(r, i + k + 1), d13 = kk - knot(r, i + 1), n1 = (kk - t) * N[i + 1];
                if (1e-12 <= fabs(d13) && 1e-12 <= fabs(n1)) N[i] = n1 / d13 + v;
            }
    }
}
static int eval_nurbs(const CurveRec *c, double t, double p[3]) {
    const uint8_t *r = c->rec;
    int nk = ri(r + 0xc8), np = ri(r + 0xcc), deg = r[0xd0], vpp = r[0xd1], rat = (~(r[0xd2] >> 2)) & 1;
    double Nst[2000], *N = nk <= 2000 ? Nst : malloc(sizeof(double) * (size_t)nk);
    if (!N) return -1;
    int s = nurb_interval(t, r, nk, deg);
    nurbs_basis(t, r, nk, s, deg, N);
    const uint8_t *cps = r + 0xd8 + 8 * (size_t)nk;
    double a[4] = { 0, 0, 0, 0 };
    int i0 = s - deg < 0 ? 0 : s - deg;
    for (int d = 0; d < vpp; d++)
        for (int i = i0; i <= s; i++) if (i < np) a[d] = N[i] * rd(cps + 8 * ((size_t)i * vpp + d)) + a[d];
    p[0] = p[1] = p[2] = 0;
    if (!rat) for (int d = 0; d < vpp && d < 3; d++) p[d] = a[d];
    else if (fabs(a[0]) <= 1e-05) for (int d = 1; d < vpp; d++) p[d - 1] = a[d];
    else for (int d = 1; d < vpp; d++) p[d - 1] = a[d] / a[0];
    if (N != Nst) free(N);
    return 0;
}
static void ev(const CurveRec *c, double t, double p[3]) {
    if (c->cls == 0x80) eval_nurbs(c, t, p);
    else mcs_curve_eval(NULL, c, t, p);
}

/* ---- dist_pt_l_3d (mccore 0x180013bd0) and FUN_1800536f0, the 3-point line test ---- */
static void dist_pt_l_3d(double systol, const double *p, const double *a, const double *b, double *out) {
    double y0 = a[1], x0 = a[0], z0 = a[2], d9 = b[0] - x0, d7 = b[1] - y0, d8 = b[2] - z0;
    double d5 = (p[1] - y0) * d8 - (p[2] - z0) * d7, d3 = (p[2] - z0) * d9 - d8 * (p[0] - x0), d4 = (p[0] - x0) * d7 - (p[1] - y0) * d9;
    d7 = sqrt(d9 * d9 + d7 * d7 + d8 * d8);
    double d;
    if (fabs(d7) <= systol) d = sqrt((y0 - p[1]) * (y0 - p[1]) + (x0 - p[0]) * (x0 - p[0]) + (z0 - p[2]) * (z0 - p[2]));
    else { d = sqrt(d3 * d3 + d5 * d5 + d4 * d4); d = fabs(d / d7); }
    *out = d;
}
static int line_check(double tol, double systol, const double *A, const double *B, const double *C) {
    double cx = C[0], cy = C[1], cz = C[2];
    double d = sqrt((cx - A[0]) * (cx - A[0]) + (cy - A[1]) * (cy - A[1]) + (cz - A[2]) * (cz - A[2]));
    if (tol < d) {
        d = sqrt((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2]));
        if (tol < d) {
            double e = sqrt((cy - B[1]) * (cy - B[1]) + (cx - B[0]) * (cx - B[0]) + (cz - B[2]) * (cz - B[2]));
            if (tol < e) { dist_pt_l_3d(systol, B, A, C, &d); return d <= tol; }
        }
        return 1;
    }
    d = sqrt((cx - B[0]) * (cx - B[0]) + (cy - B[1]) * (cy - B[1]) + (cz - B[2]) * (cz - B[2]));
    return d <= tol;
}

/* FUN_180053e30: the plane of A, B, C (the normal as here, unit, or 0), its view (find_view_k, making one if needed), the 3 points in that view, the circle through them
   (cr_a_3p); z = A in the view (the *param_6 written after the first world_to_view) */
static int fit_3p(Vt *vt, double systol, const double *A, const double *B, const double *C, A2 *arc, int *view, double *z) {
    double d10 = A[2] - B[2], d11 = B[2] - C[2], d8 = A[1] - B[1], d3 = B[1] - C[1], d2 = A[0] - B[0], d9 = B[0] - C[0];
    double nx = d11 * d8 - d3 * d10, ny = d10 * d9 - d11 * d2, nz = d3 * d2 - d8 * d9;
    double l = sqrt(nx * nx + ny * ny + nz * nz), n[3];
    if (l == 0.0) { n[0] = n[1] = n[2] = 0.0; } else { n[0] = nx / l; n[1] = ny / l; n[2] = nz / l; }
    int ok;
    find_view_k(vt, n, view, 1, &ok);
    if (!ok) return 0;
    double a[3], b[3], c[3];
    w2v(vt, *view, A, a); *z = a[2];
    w2v(vt, *view, B, b);
    w2v(vt, *view, C, c);
    cr_a_3p(systol, a, b, c, arc, &ok);
    return ok;
}

/* the a_3d (arc_2d_to_3d mccore 0x180012ae0, arc_ep_3d mastercam 0x18032d730 -> systypes 0x180001210 with the view's matrix) */
typedef struct { double p1[3], p2[3], c[3], r, a0, sw; int view; } Arc;
static void arc_ep(const Vt *vt, Arc *a) {
    double m[9]; int have = view_mat(vt, a->view, m);
    double v0[3], v1[3], end = a->sw + a->a0, s = sin(a->a0), c = cos(a->a0);
    v0[0] = c * a->r + a->c[0]; v0[1] = s * a->r + a->c[1]; v0[2] = a->c[2];
    double e = cos(end), f = sin(end);
    v1[0] = e * a->r + a->c[0]; v1[1] = f * a->r + a->c[1]; v1[2] = a->c[2];
    if (!have) { cp3(a->p1, v0); cp3(a->p2, v1); return; }
    for (int k = 0; k < 3; k++) {
        a->p1[k] = v0[1] * m[3 + k] + v0[0] * m[k] + v0[2] * m[6 + k];
        a->p2[k] = v1[1] * m[3 + k] + v1[0] * m[k] + v1[2] * m[6 + k];
    }
}

/* FUN_180053890: a piecewise-linear curve: class 8 of order 2 (+0xb4), class 0x80 of degree 1 (+0xd0) */
static int is_poly(const CurveRec *c) {
    return (c->cls == 8 && c->rec[0xb4] == 2) || (c->cls == 0x80 && c->rec[0xd0] == 1);
}

/* FUN_180054020: line or arc from the record */
static int fit(const Rec *R, Vt *vt, double tol, double systol, int *is_line, double line[6], Arc *out) {
    const CurveRec *c = R->c;
    double snp = tol * 0.1;                                    /* 0x180178098 */
    *is_line = 0;
    int poly = is_poly(c);
    if (poly && ((c->cls == 8 && ri(c->rec + 0xb0) == 1) || (c->cls == 0x80 && ri(c->rec + 0xcc) == 2))) {
        cp3(line, R->e0); cp3(line + 3, R->e1);                /* the single straight span: curve_endpoints, *is_line = 1 */
        *is_line = 1; return 1;
    }
    double t0 = R->t1, t1 = R->t2, tm = (t1 + t0) * 0.5;       /* curve_t1t2 */
    double A[3], B[3], C[3];
    ev(c, t0, A); ev(c, tm, B); ev(c, t1, C);
    int lin = line_check(tol, systol, A, B, C);
    A2 arc; memset(&arc, 0, sizeof arc);
    int view0 = 0, view = 0;
    double z = 0.0, z0 = 0.0, r0 = 0.0, cc0 = 0.0, cc1 = 0.0;
    int arcok = fit_3p(vt, systol, A, B, C, &arc, &view0, &z);
    if (arcok) {
        r0 = arc.r; z0 = z; view = view0; cc0 = arc.x; cc1 = arc.y;
        snap(snp, &r0); snap(snp, &cc0); snap(snp, &cc1); snap(snp, &z0);
    }
    if (!lin && !arcok) return 0;
    int n = poly ? (c->cls == 8 ? ri(c->rec + 0xb0) + 1 : ri(c->rec + 0xcc)) : 0x65;
    double dt = (t1 - t0) / (double)(n + -1), t = dt + t0;
    for (; t <= t1 - dt; t = t + dt) {
        ev(c, t, B);
        if (lin) lin = line_check(tol, systol, A, B, C);
        if (arcok) {
            int save_nv = vt->n_views;                         /* PlanesTemporaryStart + local_200 = n_views */
            double zc;
            arcok = fit_3p(vt, systol, A, B, C, &arc, &view0, &z);
            if (arcok) {
                snap(snp, &arc.r); snap(snp, &arc.x); snap(snp, &arc.y); snap(snp, &z);
                zc = z;
                arcok = view0 == view && tol >= fabs(r0 - arc.r) && systol >= fabs(z0 - z);
                if (arcok) { double d = sqrt((arc.y - cc1) * (arc.y - cc1) + (arc.x - cc0) * (arc.x - cc0)); arcok = tol >= d; }
            } else zc = z;
            if (!arcok) {                                      /* near the circle at this sample? then B moves onto it, the fit goes on */
                double Bv[3];
                w2v(vt, view, B, Bv);
                zc = Bv[2]; z = Bv[2];
                double dx = Bv[0] - cc0, dy = Bv[1] - cc1, l = sqrt(dx * dx + dy * dy);
                double ux, uy;
                if (l == 0.0) { uy = 0.0; ux = 0.0; } else { ux = dx / l; uy = dy / l; }
                double ang = 0.0;
                if (1e-12 <= uy * uy + ux * ux) { ang = atan2(uy, ux); if (ang < 0.0) ang = ang + TWO_PI; }
                double qx = cos(ang) * r0 + cc0, qy = sin(ang) * r0 + cc1;
                double d = sqrt((Bv[1] - qy) * (Bv[1] - qy) + (Bv[0] - qx) * (Bv[0] - qx));
                if (d <= tol * 2.0 && fabs(zc - z0) <= systol) {
                    double p[3] = { qx, qy, z0 };
                    arcok = 1;
                    v2w(vt, view, p, B);
                    goto arc_good;
                }
                vt->n_views = save_nv;
                goto next;
            }
arc_good:
            snap(snp, &r0); snap(snp, &cc0); snap(snp, &cc1); snap(snp, &z0);
            r0 = arc.r * 0.05 + r0 * 0.95;                     /* 0x18017e4c0, 0x18017e4d8 */
            z0 = z0 * 0.95 + zc * 0.05;
            cc0 = cc0 * 0.95 + arc.x * 0.05;
            cc1 = cc1 * 0.95 + arc.y * 0.05;
            snap(snp, &r0); snap(snp, &cc0); snap(snp, &cc1); snap(snp, &z0);
            if (poly) {                                        /* mid-span chord points stay within 5 tol of the circle */
                double X[3], v[3];
                ev(c, t - dt, X);
                double m1[3] = { (X[0] + B[0]) * 0.5, (X[1] + B[1]) * 0.5, (X[2] + B[2]) * 0.5 };
                w2v(vt, view, m1, v);
                double d = sqrt((cc1 - v[1]) * (cc1 - v[1]) + (cc0 - v[0]) * (cc0 - v[0]));
                if (fabs(d - r0) <= tol * 5.0) {
                    ev(c, dt + t, X);
                    double m2[3] = { (X[0] + B[0]) * 0.5, (X[1] + B[1]) * 0.5, (X[2] + B[2]) * 0.5 };
                    w2v(vt, view, m2, v);
                    d = sqrt((cc1 - v[1]) * (cc1 - v[1]) + (cc0 - v[0]) * (cc0 - v[0]));
                    if (!(fabs(d - r0) <= tol * 5.0)) arcok = 0;
                } else arcok = 0;
            }
            if (!arcok) vt->n_views = save_nv;
        }
next:
        if (!lin && !arcok) return 0;
    }
    if (lin) {
        cp3(line, R->e0); cp3(line + 3, R->e1);                /* curve_endpoints */
        *is_line = 1; return 1;
    }
    if (!arcok) return 0;
    /* the arc: arc_2d_to_3d(out, view, z0, { centre, r, the last cr_a_3p's start / sweep }): the a_2d's centre and radius replaced first by the final ones */
    arc.x = cc0; arc.y = cc1; arc.r = r0;
    out->view = view; out->c[0] = cc0; out->c[1] = cc1; out->c[2] = z0; out->r = r0;
    out->a0 = arc.a0; out->sw = arc.sw;
    arc_ep(vt, out);
    double p[3], v[3], a0, a1, a2;
    ev(c, t0, p); w2v(vt, view, p, v);
    a0 = ang_of(v[1] - arc.y, v[0] - arc.x);                   /* the angle block of FUN_180054020 */
    ev(c, tm, p); w2v(vt, view, p, v);
    a1 = ang_of(v[1] - arc.y, v[0] - arc.x);
    ev(c, t1, p); w2v(vt, view, p, v);
    a2 = ang_of(v[1] - arc.y, v[0] - arc.x);
    restrict_angle(0.0, TWO_PI, &a0);
    out->a0 = a0;
    out->sw = sweep_angle(a0, a1, a2);
    if (fabs(out->sw) * out->r < systol) return 0;
    return 1;
}

/* convert_ent_to_line_or_arc: an open curve is one fit; a closed one (ends within tol of each other) is split in the middle (at a knot for piecewise linear curves, at
   t = 0.54321 t1 + 0.45679 t2 otherwise) and the two half arcs must agree */
int mcs_simplify_curve(const CurveRec *c, double tol, double systol, const McsSimpView *views, int nviews, McsSimplify *out) {
    memset(out, 0, sizeof *out);
    const uint8_t *r = c->rec;
    if (c->cls != 8 && c->cls != 0x80) return 0;
    Vt vt; memset(&vt, 0, sizeof vt);
    vt.st = views; vt.nst = nviews;
    vt.base = 8;
    for (int k = 0; k < nviews; k++) if (views[k].n > vt.base) vt.base = views[k].n;
    vt.n_views = vt.base;
    Rec R;
    memcpy(R.e0, r + 0x68, 24); memcpy(R.e1, r + 0x80, 24);    /* curve_endpoints: ent +0x68, +0x80 */
    R.t1 = rd(r + 0x98); R.t2 = rd(r + 0xa0); R.c = c;         /* curve_t1t2 */
    Arc a, a2; memset(&a, 0, sizeof a); memset(&a2, 0, sizeof a2);
    double d = sqrt((R.e1[1] - R.e0[1]) * (R.e1[1] - R.e0[1]) + (R.e1[0] - R.e0[0]) * (R.e1[0] - R.e0[0]) + (R.e1[2] - R.e0[2]) * (R.e1[2] - R.e0[2]));
    int ok = 0, isl = 0;
    if (systol <= d && d != systol) {
        ok = fit(&R, &vt, tol, systol, &isl, out->line, &a);
        out->is_line = isl;
    } else {
        double t1 = R.t1, t2 = R.t2, tm;
        if (!is_poly(c)) tm = t1 * 0.54321 + t2 * 0.45679;      /* 0x18017e4d0, 0x18017e4c8 */
        else if (c->cls == 8) {
            int n = ri(r + 0xb0);
            if (n < 3) return 0;
            tm = (t1 + t2) * 0.5;
            if (n % 2) tm = ((t1 + t2) * ((double)(n - 1) * 0.5)) / (double)n;
        } else {
            int n = ri(r + 0xcc) - 1;
            if (n < 3) return 0;
            if (n % 2) n = ri(r + 0xcc) - 2;
            tm = knot(r, (n / 2) + 1);
        }
        double P[3]; ev(c, tm, P);
        R.t2 = tm; cp3(R.e1, P);                               /* ent +0xa0, +0x80 */
        ok = 0;
        int f1 = fit(&R, &vt, tol, systol, &isl, out->line, &a);
        if (f1 && !isl) {
            R.t1 = R.t2; R.t2 = t2;
            cp3(R.e1, R.e0); cp3(R.e0, P);                     /* the second half: ent +0x98 = the split, +0xa0 = t2, +0x80 = ent's start, +0x68 = the split point */
            int f2 = fit(&R, &vt, tol, systol, &isl, out->line, &a2);
            if (f2 && !isl && a.view == a2.view) {
                double dc = sqrt((a2.c[1] - a.c[1]) * (a2.c[1] - a.c[1]) + (a2.c[0] - a.c[0]) * (a2.c[0] - a.c[0]) + (a2.c[2] - a.c[2]) * (a2.c[2] - a.c[2]));
                if (dc < tol && fabs(a.r - a2.r) < tol) {
                    a.sw = a.sw > 0.0 ? TWO_PI : -TWO_PI;       /* the full circle, sign of the first half's sweep */
                    cp3(a.p2, a.p1);
                    a.c[0] = (a2.c[0] + a.c[0]) * 0.5; a.c[1] = (a2.c[1] + a.c[1]) * 0.5; a.c[2] = (a2.c[2] + a.c[2]) * 0.5;
                    a.r = a.r * 0.5 + a2.r * 0.5;
                    snap(tol, &a.r); snap(tol, &a.c[0]); snap(tol, &a.c[1]); snap(tol, &a.c[2]);
                    ok = 1;
                }
            }
        }
        out->is_line = isl;
    }
    if (ok && !out->is_line) {
        cp3(out->a_p1, a.p1); cp3(out->a_p2, a.p2); cp3(out->c, a.c);
        out->r = a.r; out->a0 = a.a0; out->sw = a.sw; out->view = a.view;
        out->view_is_new = a.view > vt.base;
        view_mat(&vt, a.view, out->view_m);
    }
    return ok;
}
