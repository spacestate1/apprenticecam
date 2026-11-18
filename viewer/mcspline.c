/* mcspline.c - Mastercam's curve entities: positions (eval_curve) and the spline break into lines (brk_spline_const_err), split
 * out of mcsurf.c so the loaders / tools that link mcam_load.c need no OpenCASCADE (mcsurf.c meshes surfaces through occt_mesh).
 * Specs: ../formats/spline_chain.md, formats/mcam.md. */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "mcsurf.h"

static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int64_t rd64(const uint8_t *p) { int64_t v; memcpy(&v, p, 8); return v; }

const CurveRec *mcs_curve(const McamFile *f, int64_t eptr) {
    for (int i = 0; i < f->ncrvs; i++) if (f->crvs[i].eptr == eptr) return &f->crvs[i];
    return NULL;
}
const Surf *mcs_surf(const McamFile *f, int64_t eptr) {
    for (int i = 0; i < f->nsurfs; i++) if (f->surfs[i].eptr == eptr) return &f->surfs[i];
    return NULL;
}
/* view_to_world (mastercam.dll 0x180341520, formats/mcam.md "Arc views"): views < 2 identity, else world = M^T v with M = the
   view's rows - the reserved views 1..8 (mcam_std_view) or the part's stored ones */
static void view_to_world(const McamFile *f, int view, const double v[3], double w[3]) {
    double sm[9]; const double *m = NULL;
    if (view >= 2) { for (int k = 0; k < f->nfviews; k++) if (f->fviews[k].n == view) { m = f->fviews[k].m; break; }
                     if (!m && mcam_std_view(view, sm)) m = sm; }
    if (!m) { w[0] = v[0]; w[1] = v[1]; w[2] = v[2]; return; }
    for (int k = 0; k < 3; k++) w[k] = v[0] * m[k] + v[1] * m[3 + k] + v[2] * m[6 + k];
}

/* eval_nurbs (mccore 0x1805ae2d0, decomp/mccore_curves.c l.1973) on the nb_type at ent +0x68: +0x60 i32 knots, +0x64 i32 control
   points, +0x68 u8 degree, +0x69 u8 values per point, +0x6a bit 2 clear = rational (weight first); the knots then the points
   follow the 0x70-byte nb_type at ent +0xd8 (formats/mcam.md). Rational points homogeneous, as the NURBS surfaces (mcam.md). The
   parameter is a raw knot value (de Boor on the stored knots). */
static int eval_nurbs_curve(const uint8_t *r, uint32_t n, double t, double p[3]) {
    if (n < 0xd8) return -1;
    int nk = rd32(r + 0xc8), nc = rd32(r + 0xcc), deg = r[0xd0], vpp = r[0xd1], rat = !((r[0xd2] >> 2) & 1);
    if (deg < 1 || deg > 25 || nc < deg + 1 || nk != nc + deg + 1 || vpp < 2 || vpp > 4 || 0xd8 + 8 * ((size_t)nk + (size_t)vpp * nc) > n) return -1;
    const uint8_t *K = r + 0xd8, *C = K + 8 * nk;
    double k0 = rdd(K + 8 * deg), k1 = rdd(K + 8 * nc);
    if (t < k0) t = k0;
    if (t > k1) t = k1;
    int s = deg; while (s < nc - 1 && !(t < rdd(K + 8 * (s + 1)))) s++;
    double d[26][4];
    for (int j = 0; j <= deg; j++) {
        const uint8_t *q = C + 8 * (size_t)vpp * (s - deg + j);
        double w = rat ? rdd(q) : 1.0; int o = rat ? 1 : 0, dim = vpp - o;
        d[j][3] = w;
        for (int k = 0; k < 3; k++) d[j][k] = k < dim ? rdd(q + 8 * (o + k)) : 0.0;
    }
    for (int rr = 1; rr <= deg; rr++) for (int j = deg; j >= rr; j--) {
        int i = s - deg + j; double a0 = rdd(K + 8 * i), a1 = rdd(K + 8 * (i + deg - rr + 1));
        double a = a1 > a0 ? (t - a0) / (a1 - a0) : 0.0;
        for (int k = 0; k < 4; k++) d[j][k] = (1 - a) * d[j - 1][k] + a * d[j][k];
    }
    double w = rat ? d[deg][3] : 1.0; if (!(fabs(w) > 1e-300)) return -1;
    for (int k = 0; k < 3; k++) p[k] = d[deg][k] / w;
    return 0;
}

/* eval_spline (mccore 0x180662eb0, decomp/surfkinds/evalspline.c l.3) on the spline at ent +0x68: +0x48 i32 segments, +0x4c u8
   order, +0x4d u8 dimension; the coefficients follow the 0x58-byte struct at ent +0xc0 (formats/mcam.md S_ID sizes): per segment
   order x dimension doubles, [power ascending][coordinate] (eval_poly l.255: Horner from the top). The segment is floor(t), the last
   one taking the rest; the local parameter is t - floor(t). */
static int eval_spline(const uint8_t *r, uint32_t n, double t, double p[3]) {
    if (n < 0xc0) return -1;
    int ns = rd32(r + 0xb0), ord = r[0xb4], dim = r[0xb5];
    if (ns < 1 || ord < 1 || dim < 1 || dim > 3 || 0xc0 + 8 * (size_t)ns * ord * dim > n) return -1;
    double f = t >= 0 ? floor(t) : ceil(t); int i = (int)f; if (t >= 0) t -= i;
    if (i < 0) i = 0; else if (i > ns - 1) { t += i - ns + 1; i = ns - 1; }
    const uint8_t *c = r + 0xc0 + 8 * ((size_t)ord * dim * i);
    p[0] = p[1] = p[2] = 0;
    for (int d = 0; d < dim; d++) { double a = 0; for (int k = ord - 1; k >= 0; k--) a = a * t + rdd(c + 8 * ((size_t)k * dim + d)); p[d] = a; }
    return 0;
}

/* eval_curve (mccore 0x1803fcd10, evalsurf.c l.4311), positions: point (1) its xyz; line (2) (1 - t) p1 + t p2; arc (4) the angle
   +0xb8 + t * +0xc0 at radius +0xb0 about the view centre +0x98 (z +0xa8), through view_to_world of view +0xc8; NURBS curve (0x80)
   eval_nurbs; parametric spline (8) eval_spline. Classes 0x400 / 0x800: NOT ported (-1). */
int mcs_curve_eval(const McamFile *f, const CurveRec *c, double t, double p[3]) {
    const uint8_t *r = c->rec;
    switch (c->cls) {
    case 0x1: if (c->n < 0x80) return -1; for (int k = 0; k < 3; k++) p[k] = rdd(r + 0x68 + 8 * k); return 0;
    case 0x2: if (c->n < 0x98) return -1; for (int k = 0; k < 3; k++) p[k] = (1 - t) * rdd(r + 0x68 + 8 * k) + t * rdd(r + 0x80 + 8 * k); return 0;
    case 0x4: {
        if (c->n < 0xca) return -1;
        double rad = rdd(r + 0xb0), a = t * rdd(r + 0xc0) + rdd(r + 0xb8);
        double v[3] = { cos(a) * rad + rdd(r + 0x98), sin(a) * rad + rdd(r + 0xa0), rdd(r + 0xa8) + 0.0 };
        int16_t view; memcpy(&view, r + 0xc8, 2);
        view_to_world(f, view, v, p); return 0;
    }
    case 0x80: return eval_nurbs_curve(r, c->n, t, p);
    case 0x8: return eval_spline(r, c->n, t, p);
    default: return -1;
    }
}

/* curve_t1t2 (mccore 0x1803fc9a0, evalsurf.c l.5363): a curve's parameter range - points / lines / arcs 0..1, splines / NURBS
   curves (and class 0x400) ent +0x98 / +0xa0; class 0x800 0 .. (i32 ent +0x70) - 1 */
static void curve_t1t2(const CurveRec *c, double *t1, double *t2) {
    if ((c->cls == 0x8 || c->cls == 0x80) && c->n >= 0xa8) { *t1 = rdd(c->rec + 0x98); *t2 = rdd(c->rec + 0xa0); }
    else { *t1 = 0; *t2 = 1; }
}

/* ================================================================================================ spline curves
 * Parametric splines (S_ID 0x8) and NURBS curves (NB_ID 0x80) as chain_to_ctour_array breaks them into lines for a toolpath
 * (formats/spline_chain.md; decompiles decomp/mccore_spline_brk.c): eval_curve with 1 / 2 derivatives, nurb_interval
 * (mccore 0x1805b0630), brk_spline_const_err (0x180162540) with FUN_180162c70 (speed), FUN_180162cf0 (step estimate) and
 * FUN_180162d60 (curvature), dist_to_finite_line (0x1804b6680) and dist_pt_l_3d (0x180013bd0). */

static double rdk(const uint8_t *r, int i) { return rdd(r + 0xd8 + 8 * (size_t)i); }   /* NURBS knot i (nb_type +0x70 = ent +0xd8) */

int mcs_spline_ok(const CurveRec *c) {
    const uint8_t *r = c->rec;
    if (c->cls == 0x8) {
        if (c->n < 0xc0) return 0;
        int ns = rd32(r + 0xb0), ord = r[0xb4], dim = r[0xb5];
        return ns >= 1 && ord >= 1 && ord <= 26 && dim >= 1 && dim <= 3 && 0xc0 + 8 * (size_t)ns * ord * dim <= c->n;
    }
    if (c->cls == 0x80) {
        if (c->n < 0xd8) return 0;
        int nk = rd32(r + 0xc8), nc = rd32(r + 0xcc), deg = r[0xd0], vpp = r[0xd1];
        return deg >= 1 && deg <= 25 && nc >= deg + 1 && nk == nc + deg + 1 && vpp >= 2 && vpp <= 4 && 0xd8 + 8 * ((size_t)nk + (size_t)vpp * nc) <= c->n;
    }
    return 0;
}
void mcs_spline_t1t2(const CurveRec *c, double *t1, double *t2) { curve_t1t2(c, t1, t2); }
/* the dimension brk_spline_const_err / FUN_180162c70 / FUN_180162d60 test (2 = planar math): S_ID ent +0xb5; NB_ID ent +0xd1
   less one when rational (ent +0xd2 bit 2 clear) */
static int spl_dim(const CurveRec *c) {
    if (c->cls == 0x8) return c->rec[0xb5];
    int v = c->rec[0xd1]; return (c->rec[0xd2] & 4) ? v : v - 1;
}
/* nurb_interval (0x1805b0630): the knot span of t - the last knot <= t + 1e-10 among deg + 1 .. nk - deg - 1, less one; on a
   zero-length span, from the end the first knot index whose knot lies below t - 1e-10 */
static int nurb_interval(double t, const uint8_t *r, int nk, int deg) {
    if (t < rdk(r, 0)) t = rdk(r, 0);
    if (rdk(r, nk - 1) < t) t = rdk(r, nk - 1);
    int lo = deg + 1, hi = nk - lo, l = lo;
    while (l <= hi && rdk(r, l) - 1e-10 <= t) l++;
    int s = (l > hi ? hi : l) - 1, res = s;
    if (rdk(r, s + 1) - rdk(r, s) <= 1e-10) {
        int k = nk - 1;
        if (k > 0) for (;;) { res = k; if (!(t <= rdk(r, k) + 1e-10)) break; k--; if (k < 1) return s; }
    }
    return res;
}
/* ---- eval_nurbs (mccore 0x1805ae2d0, decomp/mccore_curves.c) on a NURBS record (knots and points at +0xd8), param_3 1 (every order up
   to nd <= 2), param_6 0: the exact arithmetic of the DLL (moved from mcspline_chain.c 2026-11-12) (the fit's tangents and the curvature limit depend on it at stacked end points) */
/* nurb_interval (0x1805b0630) */
static int nb_interval(double t, const double *K, int nk, int deg) {
    if (t < K[0]) t = K[0];
    if (K[nk - 1] < t) t = K[nk - 1];
    int lo = deg + 1, hi = nk - lo, l = lo;
    while (l <= hi && K[l] - 1e-10 <= t) l++;
    int s = (l > hi ? hi : l) - 1, res = s;
    if (K[s + 1] - K[s] <= 1e-10) {
        int k = nk - 1;
        while (k > 0) { res = k; if (!(t <= K[k] + 1e-10)) break; k--; if (k < 1) return s; }
    }
    return res;
}
/* FUN_1805aeb50 / FUN_1805aeff0: the B-spline basis of span by levels (N indexed by the absolute knot; tab: the levels of the triangle in order, as FUN_1805afb00 reads it; 1e-12 guards as the decompile) */
static void nb_basis(double t, const double *K, int nk, int span, int deg, double *N, double *tab) {
    N[span] = 1.0;
    if (tab) tab[0] = 1.0;
    int s = 1;
    for (int m = 1; m <= deg; m++) {
        int lim = nk - 2 - m, j0 = span - m >= 0 ? span - m : 0;
        for (int j = j0; j <= span; j++) if (j <= lim) {
            double d1 = K[j + m] - K[j], d2 = t - K[j], a;
            if (fabs(d1) < 1e-12 || fabs(d2) < 1e-12) { a = 0.0; N[j] = 0.0; }
            else { a = (d2 / d1) * N[j]; N[j] = a; }
            double d3 = K[j + m + 1] - K[j + 1], d4 = (K[j + m + 1] - t) * N[j + 1];
            if (1e-12 <= fabs(d3) && 1e-12 <= fabs(d4)) N[j] = d4 / d3 + a;
        }
        if (tab) for (int i = j0; i <= span; i++) tab[s++] = N[i];
    }
}
/* the point (nd = 0) of eval_nurbs: the basis sum, x / w unless |w| <= 1e-5; nd >= 1 FUN_1805afb00 (the differences of the control points, level by level) and FUN_1804b7bd0 (the rational quotient rule); o[d] the derivatives (nd <= 2) */
void mcs_eval_nurbs(const uint8_t *r, int nd, double t, double o[3][3]) {
    int nk = rd32(r + 0xc8), ncp = rd32(r + 0xcc), deg = r[0xd0], vpp = r[0xd1], rat = !((r[0xd2] >> 2) & 1);
    const double *K = (const double *)(r + 0xd8), *P = K + nk;
    int span = nb_interval(t, K, nk, deg), base = span - deg < 0 ? 0 : span - deg, ord = deg + 1;
    memset(o, 0, 9 * sizeof(double));
    if (nd < 1) {
        double N[256], acc[4] = { 0, 0, 0, 0 };
        memset(N, 0, sizeof(double) * ((size_t)nk + 2 < 256 ? (size_t)nk + 2 : 256));
        double *np = N, *hp = NULL;
        if (nk + 2 > 256) { hp = calloc((size_t)nk + 2, 8); np = hp; }
        nb_basis(t, K, nk, span, deg, np, NULL);
        for (int c = 0; c < vpp; c++)
            for (int i = base; i <= span; i++) if (i < ncp) acc[c] = np[i] * P[i * vpp + c] + acc[c];
        free(hp);
        if (!rat) { o[0][0] = acc[0]; o[0][1] = acc[1]; o[0][2] = acc[2]; }
        else if (fabs(acc[0]) < 1e-05 || fabs(acc[0]) == 1e-05) { o[0][0] = acc[1]; o[0][1] = acc[2]; o[0][2] = acc[3]; }
        else { o[0][0] = acc[1] / acc[0]; o[0][1] = acc[2] / acc[0]; o[0][2] = acc[3] / acc[0]; }
        return;
    }
    double N[256], tab[512], A[3][4], rows[32][4];
    memset(N, 0, sizeof(double) * ((size_t)nk + 2 < 256 ? (size_t)nk + 2 : 256));
    double *np = N, *hp = NULL;
    if (nk + 2 > 256) { hp = calloc((size_t)nk + 2, 8); np = hp; }
    memset(A, 0, sizeof A);
    nb_basis(t, K, nk, span, deg, np, tab);
    free(hp);
    for (int d = 0; d <= nd; d++) {
        if (deg < d) continue;
        int s_ = deg - d, top = (s_ + 1) * s_ / 2;
        for (int i = deg; i >= d; i--) {
            /* a chain truncated by the stacked rebuild can leave span past the last control point / knot (nurb_interval's search from the end): the DLL then reads beyond its arrays, the node tables
               (CSplineNodeMgr), zero when fresh; zero here (the value only affects the curvature limit, record +0xa8) */
            if (d == 0) for (int c = 0; c < vpp; c++) rows[i][c] = base + i < ncp ? P[(base + i) * vpp + c] : 0.0;
            else {
                double kb = ord - d + base + i < nk ? K[ord - d + base + i] : 0.0, ka = base + i < nk ? K[base + i] : 0.0;
                double dt = kb - ka;
                if (fabs(dt) <= 1e-12) for (int c = 0; c < vpp; c++) rows[i][c] = 0.0;
                else for (int c = 0; c < vpp; c++) rows[i][c] = (rows[i][c] - rows[i - 1][c]) * ((double)(ord - d) / dt);
            }
            for (int c = 0; c < vpp; c++) A[d][c] = tab[top - d + i] * rows[i][c] + A[d][c];
        }
    }
    if (!rat) { for (int d = 0; d <= nd; d++) for (int c = 0; c < 3; c++) o[d][c] = A[d][c]; return; }
    double w0 = A[0][0];
    if (fabs(w0) < 1e-12) w0 = 1e-12;
    for (int d = 0; d <= nd; d++) {
        double v[3] = { A[d][1], A[d][2], A[d][3] };
        int bi = 1, sd = d;
        for (int k = 1; k <= d; k++) {
            bi = (int)(short)(sd * (short)bi) / k;
            double wd = A[k][0];
            for (int c = 0; c < 3; c++) v[c] = v[c] - (double)(int)(short)bi * o[d - k][c] * wd;
            sd--;
        }
        for (int c = 0; c < 3; c++) o[d][c] = v[c] / w0;
    }
}

/* eval_curve(ent, nd, t) of a spline: o[0] the point, o[1] / o[2] the first / second derivative (nd <= 2). NURBS: eval_nurbs
   (0x1805ae2d0) as mcs_eval_nurbs above (until 2026-11-12 the Piegl / Tiller A2.3 derivatives, equal up to rounding); parametric spline: eval_spline's segment (floor(t), the last one taking the rest,
   decomp/surfkinds/evalspline.c) and the derivatives of its power basis (eval_poly_deriv). 0 ok, -1 bad record. */
int mcs_spline_eval_d(const CurveRec *c, double t, int nd, double o[3][3]) {
    const uint8_t *r = c->rec;
    memset(o, 0, 9 * sizeof(double));
    if (!mcs_spline_ok(c) || nd < 0 || nd > 2) return -1;
    if (c->cls == 0x8) {
        int ns = rd32(r + 0xb0), ord = r[0xb4], dim = r[0xb5];
        double f = t >= 0 ? floor(t) : ceil(t); int i = (int)f; if (t >= 0) t -= i;
        if (i < 0) i = 0; else if (i > ns - 1) { t += i - ns + 1; i = ns - 1; }
        const uint8_t *cf = r + 0xc0 + 8 * ((size_t)ord * dim * i);
        for (int d = 0; d < dim; d++) {
            double p = 0, p1 = 0, p2 = 0;
            for (int k = ord - 1; k >= 0; k--) {
                double a = rdd(cf + 8 * ((size_t)k * dim + d));
                p2 = p2 * t + 2 * p1; p1 = p1 * t + p; p = p * t + a;   /* Horner with the derivatives */
            }
            o[0][d] = p; o[1][d] = p1; o[2][d] = p2;
        }
        return 0;
    }
    mcs_eval_nurbs(r, nd, t, o);   /* eval_curve case 0x80: eval_nurbs(nb, nd, 1, t, out, 0) */
    return 0;
}
/* FUN_180162c70: |eval_curve(ent, 1, t)'| (x, y only for dimension 2); the point at t into P */
static double spl_speed(const CurveRec *c, double t, int dim, double P[3]) {
    double o[3][3]; mcs_spline_eval_d(c, t, 1, o); memcpy(P, o[0], sizeof o[0]);
    double s = o[1][0] * o[1][0] + o[1][1] * o[1][1]; if (dim != 2) s += o[1][2] * o[1][2];
    return sqrt(s);
}
/* FUN_180162d60: |C' x C''| / |C'| (dimension 2: |x' y'' - y' x''| / |C'|_xy); 0 when |C'| < 1e-8 */
static double spl_curv(const CurveRec *c, double t, int dim) {
    double o[3][3]; mcs_spline_eval_d(c, t, 2, o);
    const double *d1 = o[1], *d2 = o[2];
    if (dim == 2) { double n = sqrt(d1[1] * d1[1] + d1[0] * d1[0]); return n >= 1e-8 ? fabs(d2[1] * d1[0] - d2[0] * d1[1]) / n : 0.0; }
    double n = sqrt(d1[1] * d1[1] + d1[0] * d1[0] + d1[2] * d1[2]); if (!(n >= 1e-8)) return 0.0;
    double x = d1[2] * d2[1] - d2[2] * d1[1], y = d2[2] * d1[0] - d1[2] * d2[0], z = d2[1] * d1[0] - d2[0] * d1[1];
    return sqrt(x * x + y * y + z * z) / n;
}
/* FUN_180162cf0: the parameter step for the chord error tol: sqrt(8 tol / k) (1 when k <= 1e-8), clamped to [dmin, dmax] */
static double spl_step(const CurveRec *c, double t, double dmin, double dmax, double tol, int dim) {
    double k = spl_curv(c, t, dim), s = k > 1e-8 ? sqrt(tol * 8.0 / k) : 1.0;
    if (s < dmin) s = dmin;
    if (dmax < s) s = dmax;
    return s;
}
/* testMoldPlusDataHidingInSpline (0x1802bf5c0): a planar parametric spline whose coefficients start with one of four marks */
static int spl_mold_hidden(const CurveRec *c) {
    if (c->cls != 0x8 || c->rec[0xb5] != 2 || c->n < 0xc0 + 16) return 0;
    const uint8_t *p = c->rec + 0xc0;
    return (p[0] == 0xb4 && p[1] == 0xc9 && p[2] == '?' && p[3] == 0xd4 && p[4] == 0xf2 && p[5] == 0xae) ||
           (p[0] == 0xea && p[1] == ':' && p[2] == 0xbc && p[3] == 0x02) ||
           (!p[0] && !p[1] && !p[2] && !p[3] && p[0xc] == '\'' && p[0xd] == 0xa4 && p[0xe] == '1') || !memcmp(p, "MOLDPLUS", 8);
}
static int spl_span8(double t, int rev, int ns) {          /* the segment number brk_spline_const_err compares (S_ID) */
    int i = (int)floor(t), s = i + 1;
    if (rev && fabs(t - i) < 1e-12) s = i;
    if (s > ns) return ns;
    return s < 1 ? 1 : s;
}
/* dist_to_finite_line (0x1804b6680) with the limit 1e38: the distance to the segment's line when the foot lies on it, else to
   the nearer end; a segment shorter than tol: to its start */
static double dist_finite(const double p[2], const double a[2], const double b[2], double tol) {
    double ux = b[0] - a[0], uy = b[1] - a[1], L = sqrt(ux * ux + uy * uy);
    if (!(tol <= L)) return hypot(a[0] - p[0], a[1] - p[1]);
    double d = fabs((p[0] - a[0]) * uy - (p[1] - a[1]) * ux) / L;
    if (1e38 <= d) return d;
    double s = ((p[0] - a[0]) * ux + (p[1] - a[1]) * uy) / L;
    if (0.0 <= s) { if (s <= L) return d; if (L + 1e38 <= s) return d; return hypot(b[0] - p[0], b[1] - p[1]); }
    if (s <= -1e38) return d;
    return hypot(a[0] - p[0], a[1] - p[1]);
}
/* dist_pt_l_3d (0x180013bd0): the distance to the infinite line a-b; |b - a| <= tol: to a */
static double dist_line3(const double p[3], const double a[3], const double b[3], double tol) {
    double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, w[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
    double L = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (fabs(L) <= tol) return sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    double x = w[1] * u[2] - w[2] * u[1], y = w[2] * u[0] - u[2] * w[0], z = w[0] * u[1] - w[1] * u[0];
    return fabs(sqrt(x * x + y * y + z * z) / L);
}
/* brk_spline_const_err (0x180162540): from *t (towards t2, or t1 when rev) the next break parameter for the chord error tol.
   min_step / max_step = curve_min_step / curve_max_step (config 1004 / 1005), systol = tol (config 1000). */
void mcs_brk_spline_const_err(const CurveRec *c, double min_step, double max_step, double tol, int rev, double systol, double *pt) {
    const uint8_t *r = c->rec; double t1, t2; curve_t1t2(c, &t1, &t2);
    double t0 = *pt; int span0, dim = spl_dim(c), is8 = c->cls == 0x8;
    int ns = is8 ? rd32(r + 0xb0) : 0, nk = is8 ? 0 : rd32(r + 0xc8), deg = is8 ? 0 : r[0xd0];
    if (is8) {
        if (spl_mold_hidden(c)) { *pt = rev ? t1 : t2; return; }
        span0 = spl_span8(t0, rev, ns);
    } else span0 = nurb_interval(t0, r, nk, deg) + 1;
    double P0[3], spd = spl_speed(c, t0, dim, P0), dmin, dmax;
    if (fabs(spd) < 1e-12 || 1e12 < fabs(spd)) { dmin = 0.001; dmax = 100.0; }
    else { dmin = min_step / spd; dmax = max_step / spd; }
    double dt = spl_step(c, t0, dmin, dmax, tol, dim);
    double rem = rev ? t0 - t1 : t2 - t0;
    if (rem < dt) dt = rem;
    if (0.1 < dt) {                                      /* probe ahead in steps of 0.1 for a smaller estimate */
        double k = 0.1, prev = 0.0, cur;
        do {
            cur = k;
            double e = spl_step(c, rev ? t0 - cur : cur + t0, dmin, dmax, tol, dim);
            if (e < cur) { dt = 0.0 < prev ? prev : e; break; }
            k = cur + 0.1; prev = cur;
        } while (cur + 0.1 < dt);
    }
    double t = dt <= rem ? (rev ? t0 - dt : dt + t0) : (rev ? t1 : t2);
    int span1 = is8 ? spl_span8(t, rev, ns) : nurb_interval(t, r, nk, deg) + 1;
    if (span1 != span0) {                                /* a segment / knot boundary passed: stop on it */
        double kv = is8 ? (double)span0 : rdk(r, span0);
        if (rev) {
            if (is8) kv += -1.0;
            else {
                int k = span0 - 1;
                if (k < nk - 1) do {
                    if (k < 1 || (!(fabs(rdk(r, k - 1) - kv) < 1e-12) && !(fabs(rdk(r, k) - t0) < 1e-8))) break;
                    k--;
                } while (k < nk - 1);
                kv = rdk(r, k);
            }
        }
        if (1e-8 < fabs(kv - t0) && t1 <= kv && kv <= t2) t = kv;
    }
    /* halve towards t0 until the chord's midpoint deviation is below 1.1 tol and the chord at most 1.1 max_step twice in a row
       (the first of the two is kept), or the step is below 1e-6 / the chord below min_step */
    double saved = 0, tolx = tol * 1.1; int cnt = 0;
    for (;;) {
        double tm = (t + t0) * 0.5, oP[3][3], oM[3][3], d, L;
        mcs_spline_eval_d(c, t, 0, oP); mcs_spline_eval_d(c, tm, 0, oM);
        const double *P = oP[0], *M = oM[0];
        if (dim == 2) { L = sqrt((P[1] - P0[1]) * (P[1] - P0[1]) + (P[0] - P0[0]) * (P[0] - P0[0])); d = dist_finite(M, P0, P, systol); }
        else { L = sqrt((P[1] - P0[1]) * (P[1] - P0[1]) + (P[0] - P0[0]) * (P[0] - P0[0]) + (P[2] - P0[2]) * (P[2] - P0[2])); d = dist_line3(M, P0, P, systol); }
        if (d < tolx && !(max_step * 1.1 < L)) {
            cnt++;
            if (cnt == 1) saved = t;
            else if (cnt > 1) { t = saved; break; }
        } else cnt = 0;
        if (fabs(t - t0) < 1e-6 || L < min_step) break;
        t = tm;
    }
    double a = rdd(r + 0x98);
    if (t < a) t = a;
    else { double b = rdd(r + 0xa0); if (b < t) t = b; }
    *pt = t;
}
