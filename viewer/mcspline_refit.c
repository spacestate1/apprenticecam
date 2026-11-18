/* mcspline_refit.c - Mastercam 2017's Refit Spline engine on parametric cubic splines.
 * Sources (Ghidra decompiles of mccore.dll, cited by function name): MaCompositeCurve::Refit, MaCubicSpline::{MakeSmoothFitCubicSpline, BlendAndFairNodes, JoinSplinesSmoothly,
 * JoinSplines, CopySplineToThis, Break, Concatenate, Length, ConvertToNURB, ConvertToC1NURB, ConvertToG0G1NURB, GetTListAtSlopeDiscontinuities, HasSharpNodes, GetSharpNodes,
 * IsSmooth, MakeSplineType, SetEndDerivsPeriodic, FixBad*Derivative, GenUList, BuildPtVtSpline, ...}, MaCubicCurve::{BestFitDerivs, GenBestFitCoeff, ConvertToAlgebraic / Geometric,
 * Truncate}, MaCurve::{BinaryCurveFitParams, FitCubicApproxCurve, Truncate, ParamAtArcLength, PointMinDist, GetClosedInfo, LimitTToRange, BreakSplineAtSlopeDiscontinuities},
 * and the driver mccadutil.dll FUN_1801347c0. Oracle: tools/tests/spline_refit_oracle.sh (the real mccore.dll under tools/peloader).
 *
 * Class 0x80 input: MaNURBSpline::{MakeNURBSplineFromNbType, HandlePiecewiseLinear, FindKnotSpan, ConvertSegmentToBezier, EvaluateRationalBezierCurve, Break, SubdivideSegment, HasSharpNodes,
 * GetTListAtSlopeDiscontinuities, ConvertToCubicSpline, RegenNonUniformKnotsFromDerivRatios, MakeSmoothFitCubicSpline, FairPiecewiseLinearToCubicSpline}, MaCubicSpline::{FairSplineTangents, RemoveDuplicatePoints},
 * MaCurve::{BinaryPointsAtTol, PointMinDist(true)}, MaLine::PointMinDist.
 * NOT ported (mcr_refit returns -1): order != 4 class 8 records (MaGenericCurve / convert_entity_to_nurbs), NURBS with stacked control points over several Bezier spans (CheckForStackedEndVertices: ConvertBezierToNURB,
 * MaNURBSpline::JoinSplines), lines / arcs in the composite. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mcspline_refit.h"
#include "mcspline_fit.h"
#include "mcsurf.h"

#define DEG2RAD 0.01745329251994329            /* MaCubicSpline::MakeSmoothFitCubicSpline, HasSharpNodes */

/* MaCubicCurve algebraic coefficients: x3 x2 x1 x0 y3 y2 y1 y0 z3 z2 z1 z0 (MaCubicSpline::EvaluateCrv) */
typedef struct { double c[12]; } Cu;
/* a MaCubicSpline: ns segments, coefficients 12 per segment, ulist[ns+1], t0 / t1 / dt = MaCurve +0x28 +0x30 +0x38 */
/* kind 5: a MaNURBSpline (the NURBS fields below); off = MaCurve +0x110 */
typedef struct { int kind, ns; double *c, *u; double t0, t1, dt, off; int known, closed, periodic; int ncp, nk, ord, rat, f128, f129, span; double *V, *K, *B; const struct Hx *hx; } Sp;
typedef struct { Cu cu; double t0, t1, dt; int known, closed, periodic; } SegC;   /* a MaCubicCurve used as a curve (BinaryCurveFitParams, PointMinDist) */

static int g_metric;

static Sp *sp_new(int ns) {          /* MaCubicSpline::AllocArrays */
    Sp *s = calloc(1, sizeof *s);
    s->kind = 6; s->ns = ns; s->c = calloc((size_t)(ns > 0 ? ns : 1) * 12, 8); s->u = calloc((size_t)ns + 1, 8);
    s->t0 = 0; s->t1 = 1; s->dt = 1;
    return s;
}
static void sp_free(Sp *s) { if (s) { free(s->c); free(s->u); free(s->V); free(s->K); free(s->B); free(s); } }
static double u_max(const Sp *s) { return s->kind == 5 ? s->K[s->nk - 1] : s->u[s->ns]; }   /* TMaxAllowable (MaCubicSpline / MaNURBSpline) */
static double u_min(const Sp *s) { return s->kind == 5 ? s->K[0] : s->u[0]; }               /* TMinAllowable */
static void sp_closed(Sp *s, double tol, int *closed, int *periodic);
static double sp_tmid(const Sp *s);
static int is_trimmed(const Sp *s) { return s->dt != (u_max(s) - u_min(s)); }   /* MaCurve::IsTrimmed */
static void set_dirty(Sp *s) { s->known = s->closed = s->periodic = 0; }        /* MaCurve::SetDirty: InitClosedInfo */
/* MaCurve::SetMinMaxParams(double,double), SetStartParamOffset: t1 > t2 on a periodic curve rotates the parameter */
static void set_range(Sp *s, double a, double b) {
    if (b < a) {
        int c = 0, pe = 0; sp_closed(s, -1.0, &c, &pe);
        if (!pe) return;
        if (u_min(s) <= a && a <= u_max(s)) { s->off = a; set_dirty(s); }
        double d2 = u_min(s), d3 = u_max(s);
        b = (d3 - a) + d2 + b;
        a = d2;
    } else if (b <= a) return;
    s->t1 = b; s->t0 = a; s->dt = b - a;
}
static void sp_gen_ulist(Sp *s) {    /* MaCubicSpline::GenUList */
    int ns = s->ns;
    s->u[0] = 0.0; if (ns >= 1) s->u[1] = 1.0;
    if (2 < ns + 1) {
        double r = 1.0;
        for (int i = 2; i < ns + 1; i++) {
            const double *c = s->c + (size_t)(i - 1) * 12, *p = s->c + (size_t)(i - 2) * 12;
            double d = sqrt(c[6] * c[6] + c[2] * c[2] + c[10] * c[10]);   /* dVar9 (y) dVar7 (x) dVar8 (z): sqrt(y*y + x*x + z*z) */
            if (d < 1e-20) d = 1e-20;
            double ex = p[0] * 3.0 + p[1] * 2.0 + p[2], ey = p[4] * 3.0 + p[5] * 2.0 + p[6], ez = p[8] * 3.0 + p[9] * 2.0 + p[10];
            double e = sqrt(ey * ey + ex * ex + ez * ez);
            double a = e; if (e < 0.0) a = fabs(e);
            if (a < 1e-20) e = 1e-20;
            r = r * (d / e);
            s->u[i] = r + s->u[i - 1];
        }
        for (int i = 1; i < ns + 1; i++) s->u[i] = s->u[i] / s->u[ns];
        s->u[0] = 0.0; s->u[ns] = 1.0;
    }
}
static void sp_alg(Sp *s, int i) {       /* one segment of MaCubicSpline::ConvertToAlgebraic: geometric P0 P1 D0 D1 per axis to algebraic */
    double *c = s->c + (size_t)i * 12;
    for (int k = 0; k < 12; k += 4) {
        double p0 = c[k], p1 = c[k + 1], d0 = c[k + 2], d1 = c[k + 3];
        double a2 = ((p1 - p0) * 3.0 - d0 * 2.0) - d1, a3 = (p0 - p1) * 2.0 + d0 + d1;
        c[k] = a3; c[k + 1] = a2; c[k + 2] = d0; c[k + 3] = p0;
    }
}
/* MaCubicCurve::ConvertToAlgebraic (geometric P0 P1 D0 D1 in Cu to algebraic) */
static void cu_alg(Cu *q) {
    for (int k = 0; k < 12; k += 4) {
        double p0 = q->c[k], d0 = q->c[k + 2], p1 = q->c[k + 1], d1 = q->c[k + 3];
        double a2 = ((p1 - p0) * 3.0 - d0 * 2.0) - d1, a3 = (p0 - p1) * 2.0 + d0 + d1;
        q->c[k] = a3; q->c[k + 1] = a2; q->c[k + 2] = d0; q->c[k + 3] = p0;
    }
}
/* MaCubicCurve::ConvertToGeometric: a3 a2 a1 a0 to P0 P1 D0 D1 per axis */
static void cu_geo(Cu *q) {
    for (int k = 0; k < 12; k += 4) {
        double a3 = q->c[k], a2 = q->c[k + 1], a1 = q->c[k + 2], a0 = q->c[k + 3];
        double d1 = a2 * 2.0 + a1 + a3 * 3.0;
        q->c[k] = a0; q->c[k + 1] = a1 + a0 + a2 + a3; q->c[k + 2] = a1; q->c[k + 3] = d1;
    }
}
/* MaCubicCurve::EvaluateCrv with one derivative at the raw parameter */
static void cu_eval1(const Cu *q, double t, double p[3], double d[3]) {
    for (int k = 0; k < 3; k++) {
        const double *a = q->c + k * 4;
        p[k] = ((t * a[0] + a[1]) * t + a[2]) * t + a[3];
        d[k] = ((t * 1.5) * a[0] + a[1]) * (t * 2.0) + a[2];
    }
}
static void nb_eval(const Sp *cs, int nd, double t, double p[3], double d[3]);
/* kind 7: a MaHelix / MaSpiral (see mcr_helix at the end of this file) */
struct Hx;
static void hx_eval(const struct Hx *h, int nd, double t, double p[3], double d[3]);
/* MaCubicSpline::EvaluateCrv (Evaluate at the raw parameter, 0 or 1 derivative) */
static void cs_eval(const Sp *s, int nd, double t, double p[3], double d[3]) {
    int i = 1, n = s->ns + 1;
    for (int j = 1; j < n; j++) { if (t <= s->u[j]) { i = j; goto found; } i = j + 1; }
found:
    if (i == n) i--;
    const double *c = s->c + (size_t)(i - 1) * 12;
    double dv = s->u[i] - s->u[i - 1];
    if (dv < 1e-12) dv = 1e-12;
    double tt = (t - s->u[i - 1]) / dv;
    for (int k = 0; k < 3; k++) p[k] = ((c[k * 4] * tt + c[k * 4 + 1]) * tt + c[k * 4 + 2]) * tt + c[k * 4 + 3];
    if (nd > 0) { double a = tt * 1.5, b = tt * 2.0, inv = 1.0 / dv;
        for (int k = 0; k < 3; k++) d[k] = ((c[k * 4] * a + c[k * 4 + 1]) * b + c[k * 4 + 2]) * inv; }
}
/* MaCurve::Evaluate (the +0x110 start offset of a rotated periodic curve) */
static void sp_eval(const Sp *s, int nd, double t, double p[3], double d[3]) {
    if (s->off != 0.0) { double v = t + s->off; t = v; if (u_max(s) < v) t = v - u_max(s); }
    if (s->kind == 7) { hx_eval(s->hx, nd, t, p, d); return; }
    if (s->kind == 5) nb_eval(s, nd, t, p, d); else cs_eval(s, nd, t, p, d);
}
static void unit3(double v[3]) {        /* FUN_18000f2f0 / the "== 0.0" normalise (the p_3d normalise of this class) */
    double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l == 0.0) { v[0] = v[1] = v[2] = 0.0; return; }
    v[0] /= l; v[1] /= l; v[2] /= l;
}
/* MaCubicSpline::GlobalTToLocalT */
static void sp_local(const Sp *s, double t, int *seg, double *lt) {
    int i = 1, n = s->ns + 1, j = 1;
    for (j = 1; j < n; j++) { i = j; if (t <= s->u[j]) break; i = j + 1; }
    if (i == n) i--;
    *seg = i - 1;
    double a = s->u[i - 1], dv = s->u[i] - a;
    if (dv < 1e-12) dv = 1e-12;
    *lt = (t - a) / dv;
}
static void sp_getseg(const Sp *s, int i, Cu *q) { memcpy(q->c, s->c + (size_t)i * 12, 96); }   /* GetSegment */
static void sp_setseg(Sp *s, const Cu *q, int i) { memcpy(s->c + (size_t)i * 12, q->c, 96); }   /* SetSegment */

/* MaCubicCurve::BestFitDerivs */
static void best_fit_derivs(const double p0[3], const double d0[3], const double p1[3], const double d1[3], double *a, double *b) {
    double dy = p1[1] - p0[1], dx = p1[0] - p0[0], dz = p1[2] - p0[2];
    double L = sqrt(dy * dy + dx * dx + dz * dz);
    if (1e-12 <= L) {
        double inv = 1.0 / L, dd = L * 2.0;
        double c0 = dy * inv * d0[1] + dx * inv * d0[0] + dz * inv * d0[2];
        double c1 = dy * inv * d1[1] + dx * inv * d1[0] + dz * inv * d1[2];
        if (0.0 <= c1) c1 = dd / (c1 + 1.0); else c1 = (1.0 - c1) * dd;
        *a = c1;
        if (0.0 <= c0) *b = dd / (c0 + 1.0); else *b = (1.0 - c0) * dd;
    } else { *a = 1e-12; *b = 1e-12; }
}
/* MaCubicCurve::GenBestFitCoeff */
static void gen_best_fit(Cu *q, const double p0[3], const double d0[3], const double p1[3], const double d1[3]) {
    double a = 0.0, b = 0.0;
    best_fit_derivs(p0, d0, p1, d1, &a, &b);
    for (int k = 0; k < 3; k++) { q->c[k * 4] = p0[k]; q->c[k * 4 + 1] = p1[k]; q->c[k * 4 + 2] = a * d0[k]; q->c[k * 4 + 3] = b * d1[k]; }
    cu_alg(q);
}
/* MaCubicCurve::Truncate(double,double): Evaluate(1) at both ends, derivatives scaled by the span, ConvertToAlgebraic */
static void cu_truncate(Cu *q, double a, double b) {
    double pa[3], da[3], pb[3], db[3], s = b - a;
    cu_eval1(q, a, pa, da);
    for (int k = 0; k < 3; k++) da[k] = da[k] * s;
    cu_eval1(q, b, pb, db);
    for (int k = 0; k < 3; k++) { q->c[k * 4] = pa[k]; q->c[k * 4 + 1] = pb[k]; q->c[k * 4 + 2] = da[k]; q->c[k * 4 + 3] = db[k] * s; }
    cu_alg(q);
}
/* MaCurve::GetClosedInfo on a parametric curve with TMid = (t0 + t1) / 2 */
static void closed_info(int (*ev)(const void *, double, double *, double *), const void *self, double t0, double t1, double tmid, int *closed, int *periodic, double tol) {
    double p0[3], d0[3], p1[3], d1[3], pm[3], dm[3];
    ev(self, t0, p0, d0);
    { double l = sqrt(d0[0] * d0[0] + d0[1] * d0[1] + d0[2] * d0[2]); if (l == 0.0) d0[0] = d0[1] = d0[2] = 0.0; else { d0[0] /= l; d0[1] /= l; d0[2] /= l; } }
    ev(self, t1, p1, d1);
    { double l = sqrt(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]); if (l == 0.0) d1[0] = d1[1] = d1[2] = 0.0; else { d1[0] /= l; d1[1] /= l; d1[2] /= l; } }
    ev(self, tmid, pm, dm);
    *closed = *periodic = 0;
    double s = sqrt((p1[1] - p0[1]) * (p1[1] - p0[1]) + (p1[0] - p0[0]) * (p1[0] - p0[0]) + (p1[2] - p0[2]) * (p1[2] - p0[2]));
    if (!(tol < s)) {
        double m = sqrt((pm[1] - p0[1]) * (pm[1] - p0[1]) + (pm[0] - p0[0]) * (pm[0] - p0[0]) + (pm[2] - p0[2]) * (pm[2] - p0[2]));
        if (!(m <= s)) {
            *closed = 1;
            if (cos(0.1745329251994329) <= d1[1] * d0[1] + d1[0] * d0[0] + d1[2] * d0[2]) *periodic = 1;
        }
    }
}
static int ev_sp1(const void *self, double t, double *p, double *d) { sp_eval((const Sp *)self, 1, t, p, d); return 0; }
static int ev_cu1(const void *self, double t, double *p, double *d) { cu_eval1((const Cu *)self, t, p, d); return 0; }
static void sp_closed(Sp *s, double tol, int *closed, int *periodic) {    /* MaCurve::IsClosed / GetClosedInfo with the cached +0x23.. (param_1 == -1: 0.0001 inch, 0.0025 metric) */
    if (!s->known) {
        double t = tol == -1.0 ? (g_metric ? 0.0025 : 0.0001) : tol;
        closed_info(ev_sp1, s, s->t0, s->t1, sp_tmid(s), &s->closed, &s->periodic, t);
        s->known = 1;
    }
    *closed = s->closed; if (periodic) *periodic = s->periodic;
}
static void segc_set(SegC *q) { q->t0 = 0.0; q->t1 = 1.0; q->dt = 1.0; q->known = 0; }   /* MaCubicCurve's constructor: MaCurve 0..1 */
static void segc_closed(SegC *q, int *closed, int *periodic) {   /* GetClosedInfo(-1.0) */
    if (!q->known) { closed_info(ev_cu1, &q->cu, q->t0, q->t1, (q->t0 + q->t1) * 0.5, &q->closed, &q->periodic, g_metric ? 0.0025 : 0.0001); q->known = 1; }
    if (closed) *closed = q->closed;
    *periodic = q->periodic;
}
/* MaCurve::LimitTToRange on a MaCubicCurve (BinaryCurveFitParams, PointMinDist) */
static void segc_limit(SegC *q, double *t) {
    int pe = 0; segc_closed(q, NULL, &pe);
    if (!pe) { if (*t <= q->t0 && q->t0 != *t) *t = q->t0; if (q->t1 < *t) *t = q->t1; return; }
    double d = *t;
    if (d < q->t0) { do { double p = q->dt; double v = *t; *t = p + v; } while (*t < q->t0); return; }
    if (q->t1 <= d && d != q->t1) { do { double v = *t - q->dt; *t = v; } while (q->t1 <= *t && *t != q->t1); return; }
}
/* MaCurve::PointMinDist with param_1 = false (the local search only), on a MaCubicCurve: returns the distance */
static double segc_point_min_dist(SegC *q, const double P[3], double t) {
    int it = 0; double prev_val = 0.0, prev_t = 0.0, pos[3], d[3];
    segc_limit(q, &t);
    double tmid = (q->t0 + q->t1) * 0.5;
    cu_eval1(&q->cu, tmid, pos, d);
    if (1e-12 <= sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])) {
        do {
            double hs = q->dt * 0.0001, a[3], da[3], b[3], db[3];
            cu_eval1(&q->cu, t - hs, a, da);
            cu_eval1(&q->cu, t + hs, b, db);
            cu_eval1(&q->cu, t, pos, d);
            double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l < 1e-20) l = 1e-20;
            l = 1.0 / l;
            double ux = d[0] * l, uy = d[1] * l, uz = d[2] * l;
            double f = (pos[0] * ux + pos[1] * uy + pos[2] * uz) - (ux * P[0] + uy * P[1] + uz * P[2]);
            if (fabs(f) < 1e-12) break;
            double g = (a[1] * uy + a[0] * ux + a[2] * uz) - (b[1] * uy + b[0] * ux + b[2] * uz);
            if (fabs(g) < 1e-20) g = 1e-20;
            g = (f * 0.0002) / g;
            double step = g;
            if (0 < it && 1e-20 <= fabs(prev_val - g)) {
                double r = (t - prev_t) / (prev_val - g);
                if (0.0 < r && fabs(r * g) < q->dt) step = r * g;
            }
            if (fabs(step) < 1e-12) break;
            double dt = q->dt, v = step;
            if (dt < fabs(step)) { v = dt; if (step < dt) v = -dt; }
            prev_t = t; t = t + v;
            segc_limit(q, &t);
            it++;
            prev_val = g;
        } while (it < 15);
        double dy = pos[1] - P[1], dx = pos[0] - P[0], dz = pos[2] - P[2];
        return sqrt(dy * dy + dx * dx + dz * dz);
    }
    /* the zero speed at TMid: the point at TMid (LimitTToRange above kept t, the decompile reports TMid's point) */
    double dy = pos[1] - P[1], dx = pos[0] - P[0], dz = pos[2] - P[2];
    return sqrt(dy * dy + dx * dx + dz * dz);
}
/* MaCurve::BinaryCurveFitParams: bisect [t0, t1] until the cubic Hermite (GenBestFitCoeff) of each span lies within param_1 of the curve at 1/4, 1/2, 3/4 */
static int binary_curve_fit_params_seed(const Sp *s, double chord, int maxp, double *t, int *np, const double *seed, int nseed) {
    double p0[3], d0[3], p1[3], d1[3];
    if (!seed) { *np = 3; t[0] = s->t0; t[1] = sp_tmid(s); t[2] = s->t1; }   /* param_5 == NULL */
    else { *np = nseed; memcpy(t, seed, (size_t)nseed * 8); }                /* param_5 / param_6: the caller's parameters */
    static const double fr[3] = { 0.25, 0.5, 0.75 };
    int last = *np - 1, first_eval = 1; long i = 0;
    for (;;) {
        if (i >= last) return 0;
        double e = t[i + 1];
        if (first_eval) sp_eval(s, 1, t[i], p0, d0);
        first_eval = 0;
        sp_eval(s, 1, e, p1, d1);
        double u0[3] = { d0[0], d0[1], d0[2] }, u1[3] = { d1[0], d1[1], d1[2] };
        unit3(u0); unit3(u1);
        SegC q; segc_set(&q);
        gen_best_fit(&q.cu, p0, u0, p1, u1);
        double dist = 0;
        int split = 0;
        for (int k = 0; k < 3; k++) {
            double f = fr[k], P[3], D[3];
            sp_eval(s, 0, (t[i + 1] - t[i]) * f + t[i], P, D);
            dist = segc_point_min_dist(&q, P, f);
            if (chord < dist) { split = 1; break; }
        }
        if (!split) {
            memcpy(p0, p1, sizeof p0); memcpy(d0, d1, sizeof d0);
            i++;
            continue;
        }
        long j = *np - 1;
        for (; i < j; j--) t[j + 1] = t[j];
        *np = *np + 1;
        if (maxp - 1 <= *np) return 1;
        t[i + 1] = (t[i + 2] + t[i]) * 0.5;
        last = *np - 1;
    }
}
static int binary_curve_fit_params(const Sp *s, double chord, int maxp, double *t, int *np) { return binary_curve_fit_params_seed(s, chord, maxp, t, np, NULL, 0); }
/* ------------------------------------------------------------------ MaNURBSpline (class 0x80 input): knots +0x148 normalised to 0..1, vertices +0x140 homogeneous (x y z w), span cache +0x150 and the Bezier segment +0x158 */
static void kn_unique(const Sp *s, double *U, int *n);
static Sp *sp_copy(const Sp *a);
/* MaNURBSpline::FindKnotSpan */
static void nb_find_span(const Sp *s, double *pt, int snap, int *span) {
    const double *K = s->K;
    int ord = s->ord, ip = ord, hi = s->nk - ord, iv = 0, snapped = 0;
    double t = *pt, e = K[s->nk - 1];
    if (K[0] <= t) {
        while (t <= e) {
            if (ip <= hi) { long l = ip; do { if (t <= K[l]) break; l++; ip++; } while (l <= hi); }
            iv = ip; if (ip == hi + 1) iv = ip - 1;
            if (!snap || snapped) goto done;
            e = K[ip]; double b = K[ip - 1], d8 = e - t, d9 = t - b;
            if ((d8 <= 0.0 || 1e-10 <= d8) && (d9 <= 0.0 || 1e-10 <= d9)) goto done;
            if (d8 <= 0.0 || 1e-10 <= d8) *pt = b; else *pt = e;
            ip = ord; t = *pt; e = K[s->nk - 1]; hi = s->nk - ip; snapped = 1;
            if (t < K[0]) break;
        }
    }
    iv = ip; if (e < *pt) iv = hi;
done:
    iv -= ord; if (iv < 0) iv = 0;
    *span = iv;
}
/* MaNURBSpline::ConvertSegmentToBezier(deg, knots + span, in place): raise the multiplicity of the span's ends; each new point is a*P + b*Q with a = La/(La+Rb), the sums clamped at 1e-20 */
static void nb_to_bezier(int p, const double *K, double *Q) {
    double L[27], R[27], ua = K[p];
    for (int i = 0; i < p; i++) { L[i] = ua - K[i + 1]; R[i] = K[p + i + 1] - ua; }
    for (int r = 0; r < p - 1; r++) for (int j = 0; j < p - 1 - r; j++) {
        double s = L[j + r] + R[j]; if (fabs(s) < 1e-20) s = 1e-20;
        double a = L[j + r] * (1.0 / s), b = R[j] * (1.0 / s);
        for (int k = 0; k < 4; k++) Q[j * 4 + k] = a * Q[(j + 1) * 4 + k] + b * Q[j * 4 + k];
    }
    double ub = K[p + 1], h = ub - ua;
    for (int i = 0; i < p; i++) R[i] = K[p + i + 1] - ub;
    for (int r = 1; r < p; r++) for (int j = p; j > r; j--) {
        double v = R[j - r], s = v + h; if (fabs(s) < 1e-20) s = 1e-20;
        double b = v * (1.0 / s), a = h * (1.0 / s);
        for (int k = 0; k < 4; k++) Q[j * 4 + k] = b * Q[(j - 1) * 4 + k] + a * Q[j * 4 + k];
    }
}
/* MaNURBSpline::EvaluateRationalBezierCurve (nder <= 1) */
static void nb_bez_eval(int nder, double t, int p, const double ab[2], const double *B, double O[2][4]) {
    double Q[27][4];
    if (p < nder) { for (int k = 0; k < 4; k++) O[p + 1 > 1 ? 1 : p + 1][k] = 0.0; nder = p; }
    double b = ab[1], a = ab[0], d = b - a;
    if (fabs(d) < 1e-20) d = 1e-20;
    double h = t - a, tt = h / d;
    if (b - tt <= tt - a) {
        for (int i = 0; i <= p; i++) memcpy(Q[i], B + (p - i) * 4, 32);
        for (int m = p; m > 0; m--) for (int j = 0; j < m; j++) for (int k = 0; k < 4; k++) Q[j][k] = (Q[j][k] - Q[j + 1][k]) * tt + Q[j + 1][k];
        memcpy(O[0], Q[0], 32);
        if (nder) {
            double e = h; if (fabs(e) < 1e-20) e = 1e-20;
            for (int kk = 0; kk < nder; kk++) { double f = (double)(p - kk) * (1.0 / e);
                for (int j = nder; j > kk; j--) for (int k = 0; k < 4; k++) Q[j][k] = (Q[j - 1][k] - Q[j][k]) * f;
                memcpy(O[kk + 1], Q[kk + 1], 32); }
        }
    } else {
        for (int i = 0; i <= p; i++) memcpy(Q[i], B + i * 4, 32);
        for (int m = p; m > 0; m--) for (int j = 0; j < m; j++) for (int k = 0; k < 4; k++) Q[j][k] = (Q[j + 1][k] - Q[j][k]) * tt + Q[j][k];
        memcpy(O[0], Q[0], 32);
        if (nder) {
            double e = b - t; if (fabs(e) < 1e-20) e = 1e-20;
            for (int kk = 0; kk < nder; kk++) { double f = (double)(p - kk) * (1.0 / e);
                for (int j = nder; j > kk; j--) for (int k = 0; k < 4; k++) Q[j][k] = (Q[j][k] - Q[j - 1][k]) * f;
                memcpy(O[kk + 1], Q[kk + 1], 32); }
        }
    }
}
/* MaNURBSpline::EvaluateCrv (0 or 1 derivative) */
static void nb_eval(const Sp *cs, int nd, double t, double p[3], double d[3]) {
    Sp *s = (Sp *)cs;   /* the span cache */
    int sp = 0, deg = s->ord - 1;
    nb_find_span(s, &t, 0, &sp);
    if (sp != s->span) {
        memcpy(s->B, s->V + (size_t)sp * 4, (size_t)s->ord * 32);
        s->span = sp;
        nb_to_bezier(deg, s->K + sp, s->B);
    }
    double ab[2] = { s->K[(size_t)s->span + deg], s->K[(size_t)s->ord + s->span] }, O[2][4];
    nb_bez_eval(nd, t, deg, ab, s->B, O);
    double w = O[0][3]; if (w < 1e-20) w = 1e-20;
    double inv = 1.0 / w;
    for (int k = 0; k < 3; k++) p[k] = O[0][k] * inv;
    if (nd > 0) for (int k = 0; k < 3; k++) d[k] = (O[1][k] - O[1][3] * p[k]) * inv;
}
/* MaNURBSpline::GetUniqueKnots */
static void kn_unique(const Sp *s, double *U, int *n) {
    double v = s->K[s->ord - 1];
    U[0] = v; *n = 1;
    for (int i = s->ord; i < s->nk - s->ord; i++) { double x = s->K[i]; if (x != v) { U[(*n)++] = x; v = x; } }
    U[(*n)++] = s->K[s->nk - s->ord];
}
static int kn_mult(const Sp *s, double x) {      /* MaNURBSpline::GetMultiplicityOfKnot */
    int m = 0; for (int i = 0; i < s->nk; i++) if (x == s->K[i]) m++;
    return s->ord < m ? s->ord : m;
}
static int kn_max_mult(const Sp *s) {            /* MaNURBSpline::GetMaxKnotMultiplicity */
    double *U = malloc((size_t)s->nk * 8); int n = 0, r = 0;
    kn_unique(s, U, &n); free(U);
    if (2 < n) {
        int c = 0; double v = s->K[s->ord - 1];
        for (int i = s->ord; i < s->nk - (s->ord - 1); i++) {
            double x = s->K[i];
            if (x == v) c++;
            else { if (r < c + 1) r = c + 1; c = 0; v = x; }
        }
    }
    return r;
}
/* MaCurve::IsPiecewiseLinearNURBCurve */
static int nb_is_polyline(const Sp *s) { return s->kind == 5 && s->ord == 2 && 2 < s->ncp; }
/* MaNURBSpline::TMid */
static double nb_tmid(const Sp *s) {
    double m = (s->t0 + s->t1) * 0.5;
    if (nb_is_polyline(s) && 2 < s->ncp) {
        double best = 1e+30, kb = 0.0;
        int found = 0;
        for (int i = 1; i < s->nk - 1; i++) {
            double k = s->K[i];
            if (s->t0 <= k && k <= s->t1) { double dd = m - k; if (fabs(dd) < best) { best = fabs(dd); kb = k; found = 1; } }
        }
        if (found && kb != 0.0 && best != 1e+30) m = kb;
    }
    return m;
}
static void nb_set_knots(Sp *s, const double *K, int n) {   /* MaNURBSpline::SetKnots (normalises, SetDirty, the cache) */
    if (n != s->nk) { free(s->K); s->nk = n; s->K = malloc((size_t)n * 8); }
    memcpy(s->K, K, (size_t)n * 8);
    double a = s->K[0], b = s->K[n - 1];
    for (int i = 0; i < n; i++) s->K[i] = (s->K[i] - a) * (1.0 / (b - a));
    set_dirty(s); s->span = -1;
}
static void nb_set_verts(Sp *s, const double *V, int n) {   /* MaNURBSpline::SetVerts */
    if (n != s->ncp) { free(s->V); s->ncp = n; s->V = malloc((size_t)n * 32); }
    memcpy(s->V, V, (size_t)n * 32);
    set_dirty(s); s->span = -1;
}
/* MaNURBSpline::RegenNonUniformKnotsFromDerivRatios: the Bezier caches of the spans next to a node give the speed ratios */
static void nb_regen_knots(Sp *s) {
    double *U = malloc((size_t)s->nk * 8), *W = malloc((size_t)s->nk * 8), p[3], d[3];
    int n = 0, o = s->ord;
    kn_unique(s, U, &n);
    W[0] = 0.0; W[1] = 1.0;
    if (2 < n) {
        double r = 1.0;
        for (int i = 1; i < n - 1; i++) {
            nb_eval(s, 0, (U[i] - U[i - 1]) * 0.5 + U[i - 1], p, d);
            const double *a1 = s->B + (o - 1) * 4, *a2 = s->B + (o - 2) * 4;
            double x1 = a1[0] / a1[3], y1 = a1[1] / a1[3], z1 = a1[2] / a1[3], x2 = a2[0] / a2[3], y2 = a2[1] / a2[3], z2 = a2[2] / a2[3];
            double d1 = sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2) + (z1 - z2) * (z1 - z2));
            if (d1 < 1e-12) d1 = 1.0;
            nb_eval(s, 0, (U[i + 1] - U[i]) * 0.5 + U[i], p, d);
            const double *b1 = s->B + 4, *b0 = s->B;
            x1 = b1[0] / b1[3]; y1 = b1[1] / b1[3]; z1 = b1[2] / b1[3]; x2 = b0[0] / b0[3]; y2 = b0[1] / b0[3]; z2 = b0[2] / b0[3];
            double d2 = sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2) + (z1 - z2) * (z1 - z2));
            if (d2 < 1e-12) d2 = 1.0;
            r = r * (d2 / d1);
            W[i + 1] = r + W[i];
        }
        for (int i = 1; i < n; i++) W[i] = W[i] / W[n - 1];
        W[0] = 0.0; W[n - 1] = 1.0;
    }
    double *NK = malloc((size_t)s->nk * 8);
    int w = 0;
    for (int i = 0; i < o; i++) NK[w++] = 0.0;
    for (int i = 1; i < n - 1; i++) { int m = kn_mult(s, U[i]); for (int j = 0; j < m; j++) NK[w++] = W[i]; }
    for (int i = s->nk - o; i < s->nk; i++) NK[i] = 1.0;
    nb_set_knots(s, NK, s->nk);
    free(NK); free(U); free(W);
}
/* MaNURBSpline::MaNURBSpline(int, int, bool) */
static Sp *nb_new(int ncp, int ord, int rat) {
    Sp *s = sp_new(0); s->kind = 5;
    s->ncp = ncp; s->ord = ord; s->nk = ncp + ord; s->rat = rat; s->span = -1;
    s->V = calloc((size_t)ncp, 32); s->K = calloc((size_t)s->nk, 8); s->B = calloc((size_t)ord, 32);
    return s;
}
/* MaNURBSpline::Break (keep_first as MaCubicSpline::Break) */
static int nb_break(Sp *s, double t, int keep_first, Sp **second) {
    *second = NULL;
    if (t == s->K[0] || t == s->K[s->nk - 1]) return 1;
    int sp = 0, ord = s->ord;
    nb_find_span(s, &t, 1, &sp);
    if (t == s->K[0] || t == s->K[s->nk - 1]) return 1;
    double A[27 * 4], L[27 * 4], R[27 * 4], Q[28][27][4];
    memcpy(A, s->V + (size_t)sp * 4, (size_t)ord * 32);
    int n1 = ord + sp, mult = 0;
    for (int i = n1 - 1; i <= s->nk - ord; i++) if (s->K[i] == t && ord - 1 < i && i < s->nk - ord) mult++;
    if (ord < mult) mult = ord;
    { int p = ord - 1; const double *K = s->K + sp;    /* MaNURBSpline::SubdivideSegment(p, knots + span, A, t, left, right) */
      double a[27], b[27];
      for (int i = 0; i < p; i++) { a[i + 1] = t - K[i + 1]; b[i + 1] = K[i + 1 + p] - t; }
      memcpy(Q[0], A, (size_t)(p + 1) * 32);
      for (int r = 1; r <= p; r++) for (int j = r; j <= p; j++) {
          double dR = b[1 + j - r], dL = a[j], inv = 1.0 / (dR + dL);
          for (int k = 0; k < 4; k++) Q[r][j][k] = (Q[r - 1][j][k] * dL + Q[r - 1][j - 1][k] * dR) * inv;
      }
      for (int j = 0; j <= p; j++) { memcpy(L + j * 4, Q[j][j], 32); memcpy(R + j * 4, Q[p - j][p], 32); }
    }
    double *V1 = malloc((size_t)n1 * 32), *K1 = malloc((size_t)(ord + n1) * 8);
    int pre = n1 - ord;
    if (0 < pre) memcpy(V1, s->V, (size_t)pre * 32);
    memcpy(V1 + (size_t)pre * 4, L, (size_t)ord * 32);
    for (int i = 0; i < n1; i++) K1[i] = s->K[i];
    for (int i = n1; i < n1 + ord; i++) K1[i] = t;
    { int nk = ord + n1; double d = K1[0], inv = 1.0 / (K1[nk - 1] - d); for (int i = 0; i < nk; i++) K1[i] = (K1[i] - d) * inv; }
    int n2 = (s->ncp - mult - n1) + ord;
    double *V2 = malloc((size_t)n2 * 32), *K2 = malloc((size_t)(ord + n2) * 8);
    memcpy(V2, R + mult * 4, (size_t)(ord - mult) * 32);
    { int c = (n2 - ord) + mult; if (0 < c) memcpy(V2 + (size_t)(ord - mult) * 4, s->V + (size_t)n1 * 4, (size_t)c * 32); }
    for (int i = 0; i < ord; i++) K2[i] = t;
    for (int i = mult + n1, w = ord; i < ord + s->ncp; i++, w++) K2[w] = s->K[i];
    { int nk = ord + n2; double d = K2[0], inv = 1.0 / (K2[nk - 1] - d); for (int i = 0; i < nk; i++) K2[i] = (K2[i] - d) * inv; }
    Sp *o = nb_new(keep_first ? n2 : n1, ord, s->rat);
    free(s->V); free(s->K); s->f128 = s->f129 = 0; s->span = -1;
    if (keep_first) { s->V = V1; s->K = K1; s->ncp = n1; s->nk = ord + n1; nb_set_verts(o, V2, n2); nb_set_knots(o, K2, ord + n2); free(V2); free(K2); }
    else { s->V = V2; s->K = K2; s->ncp = n2; s->nk = ord + n2; nb_set_verts(o, V1, n1); nb_set_knots(o, K1, ord + n1); free(V1); free(K1); }
    set_dirty(s);
    set_range(s, u_min(s), u_max(s));
    set_range(o, u_min(o), u_max(o));
    *second = o;
    return 0;
}
static void nb_copy_to(Sp *d, const Sp *s) {    /* MaNURBSpline::CopyNURBToThis */
    free(d->V); free(d->K); free(d->B);
    d->rat = s->rat; d->f128 = s->f128; d->f129 = s->f129; d->nk = s->nk; d->ncp = s->ncp; d->ord = s->ord; d->span = s->span;
    d->V = malloc((size_t)s->ncp * 32); d->K = malloc((size_t)s->nk * 8); d->B = malloc((size_t)s->ord * 32);
    memcpy(d->V, s->V, (size_t)s->ncp * 32); memcpy(d->K, s->K, (size_t)s->nk * 8); memcpy(d->B, s->B, (size_t)s->ord * 32);
    set_range(d, s->t0, s->t1);
}
/* MaCurve::BinaryPointsAtTol(0.0, angle, 1, 5000, ...) (the hunting points' parameter list: the curve is bisected until the tangents at 1/4, 1/2, 3/4 turn less than angle against the chords and ends) */
static int binary_points(const Sp *s, double ang, int maxp, double *t, int *np) {
    if (nb_is_polyline(s)) {                    /* MaNURBSpline::GetUniqueKnots: the polyline's nodes */
        double *U = malloc((size_t)s->nk * 8); int n = 0, w = 1;
        kn_unique(s, U, &n);
        t[0] = s->t0;
        for (int i = 0; i < n; i++) if (s->t0 < U[i] && U[i] < s->t1) t[w++] = U[i];
        t[w] = s->t1; *np = w + 1;
        free(U);
        return 0;
    }
    double (*SP)[3] = malloc((size_t)maxp * 24), (*ST)[3] = malloc((size_t)maxp * 24);
    int retries = 0;
    for (;;) {
        double co = cos(ang * DEG2RAD);
        t[0] = s->t0; t[1] = s->t1; *np = 2;
        double P0[3], D0[3], P1[3], D1[3], T0[3] = { 0, 0, 0 }, T1[3] = { 0, 0, 0 }, Dt[3];
        int sp = 0;
        sp_eval(s, 1, t[0], P0, D0);
        sp_eval(s, 1, t[1], P1, D1);
        { double l = sqrt(D0[0] * D0[0] + D0[1] * D0[1] + D0[2] * D0[2]); if (l != 0.0) { T0[0] = D0[0] / l; T0[1] = D0[1] / l; T0[2] = D0[2] / l; } }
        { double l = sqrt(D1[0] * D1[0] + D1[1] * D1[1] + D1[2] * D1[2]); if (l != 0.0) { T1[0] = D1[0] / l; T1[1] = D1[1] / l; T1[2] = D1[2] / l; } }
        long i = 0;
        while (i < *np - 1) {
            double Pmid[3] = { 0, 0, 0 }, Tmid[3] = { 0, 0, 0 }, Q1[3] = { 0, 0, 0 }, Q3[3] = { 0, 0, 0 };
            int split = 0, havem = 0;
            for (int k = 0; k < 3 && !split; k++) {
                static const double FR[3] = { 0.25, 0.5, 0.75 };
                double p[3], d[3];
                sp_eval(s, 1, (t[i + 1] - t[i]) * FR[k] + t[i], p, d);
                if (k == 0) memcpy(Q1, p, 24);
                else if (k == 2) memcpy(Q3, p, 24);
                else { havem = 1; memcpy(Pmid, p, 24); double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (l != 0.0) { Tmid[0] = d[0] / l; Tmid[1] = d[1] / l; Tmid[2] = d[2] / l; } else { Tmid[0] = Tmid[1] = Tmid[2] = 0.0; } }
                double c1 = fabs(T1[0] * T0[0] + T1[1] * T0[1] + T1[2] * T0[2]);
                int ok = 0;
                if (co <= c1) {
                    double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), ux = 0, uy = 0, uz = 0;
                    if (l != 0.0) { ux = d[0] / l; uy = d[1] / l; uz = d[2] / l; }
                    double c2 = fabs(T0[0] * ux + uy * T0[1] + uz * T0[2]);
                    if (co <= c2 && co <= fabs(T1[0] * ux + uy * T1[1] + uz * T1[2])) {
                        if (k == 1) ok = 1;
                        else if (k == 0) {
                            double dx = Q1[0] - P0[0], dy = Q1[1] - P0[1], dz = Q1[2] - P0[2], l2 = sqrt(dx * dx + dy * dy + dz * dz), ax = 0, ay = 0, az = 0;
                            if (l2 != 0.0) { az = dz / l2; ay = dy / l2; ax = dx / l2; }
                            if (co <= fabs(T0[0] * ax + T0[1] * ay + T0[2] * az)) ok = 1;
                        } else {
                            double ax = P1[0] - Q3[0], ay = P1[1] - Q3[1], az = P1[2] - Q3[2], l2 = sqrt(ay * ay + ax * ax + az * az);
                            if (l2 == 0.0) { ax = ay = az = 0.0; } else { ax /= l2; ay /= l2; az /= l2; }
                            if (co <= fabs(T1[0] * ax + T1[1] * ay + T1[2] * az)) ok = 1;
                        }
                    }
                }
                if (!ok) split = 1;
            }
            (void)Dt;
            if (!split) {                 /* accepted: the end becomes the next start; pop the stacked end */
                memcpy(P0, P1, 24); memcpy(T0, T1, 24);
                if (sp > 0) { sp--; memcpy(P1, SP[sp], 24); memcpy(T1, ST[sp], 24); }
                i++;
                continue;
            }
            memcpy(SP[sp], P1, 24); memcpy(ST[sp], T1, 24); sp++;
            if (havem) { memcpy(P1, Pmid, 24); memcpy(T1, Tmid, 24); }
            else {
                double p[3], d[3];
                sp_eval(s, 1, (t[i + 1] - t[i]) * 0.5 + t[i], p, d);
                memcpy(P1, p, 24);
                double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (l == 0.0) { T1[1] = T1[2] = 0.0; } else { T1[0] = d[0] / l; T1[2] = d[2] / l; T1[1] = d[1] / l; }
            }
            for (long j = *np - 1; i < j; j--) t[j + 1] = t[j];
            *np = *np + 1;
            if (maxp <= *np) { free(SP); free(ST); return 1; }
            t[i + 1] = (t[i + 2] + t[i]) * 0.5;
        }
        if (*np == 3 && retries < 3) {
            retries++;
            double a[3], b[3], dd[3];
            sp_eval(s, 0, s->t0, a, dd);
            sp_eval(s, 0, s->t1, b, dd);
            double dist = sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
            if (0.0 <= dist && dist <= 1e-12) { ang = ang * 0.5; continue; }
        }
        free(SP); free(ST);
        return 0;
    }
}
/* MaCurve::LimitTToRange on a spline */
static void sp_limit(Sp *s, double *t) {
    int c = 0, pe = 0; sp_closed(s, -1.0, &c, &pe);
    if (!pe) { if (*t <= s->t0 && s->t0 != *t) *t = s->t0; if (s->t1 < *t) *t = s->t1; return; }
    if (*t < s->t0) { do { *t = s->dt + *t; } while (*t < s->t0); return; }
    if (s->t1 <= *t && *t != s->t1) { do { *t = *t - s->dt; } while (s->t1 <= *t && *t != s->t1); }
}
/* MaLine::PointMinDist */
static void line_min_dist(const double a[3], const double b[3], const double P[3], double *dist, double *s) {
    double dz = b[2] - a[2], dx = b[0] - a[0], dy = b[1] - a[1], L = sqrt(dy * dy + dx * dx + dz * dz);
    double px = P[0] - a[0], py = P[1] - a[1], pz = P[2] - a[2], l2 = sqrt(py * py + px * px + pz * pz);
    if (1e-06 < L) {
        double ux = 0, uy = 0, uz = 0, vx = 0, vy = 0, vz = 0;
        if (l2 != 0.0) { ux = px / l2; uy = py / l2; uz = pz / l2; }
        if (L != 0.0) { vx = dx / L; vy = dy / L; vz = dz / L; }
        double c = vy * uy + vx * ux + vz * uz, f = c * l2;
        if (fabs(c) == 1.0 || l2 == 0.0) *dist = 0.0;
        else { c = 1.0 - c * c; if (1e-20 <= c) *dist = sqrt(c) * l2; else *dist = 0.0; }
        if (1e-20 < L) { *s = f / L; return; }
    } else *dist = l2;
    *s = 0.0;
}
/* MaCurve::PointMinDist(true, ...) on a spline: hunting points (MaCurveHuntingPoints::GenCrvPoints), then the Newton search of MaCurve::PointMinDist; the parameter out */
static double sp_point_min_param(Sp *s, const double P[3]) {
    double *u = malloc(5000 * 8), (*h)[3] = NULL, tmid = sp_tmid(s); int n = 0;
    double t0 = s->t0, dtt = s->t1 - s->t0;
    if (binary_points(s, 10.0, 5000, u, &n) == 0) {
        if (n < 5) { n = 5; double t = t0; for (int i = 0; i < 5; i++) { u[i] = t; t = t + dtt * 0.25; } }
    } else { n = 0x14; double t = t0; for (int i = 0; i < 20; i++) { u[i] = t; t = t + dtt / 19.0; } }
    h = malloc((size_t)n * 24);
    { double d[3]; for (int i = 0; i < n; i++) sp_eval(s, 0, u[i], h[i], d); }
    double best = 1e+31, tb = tmid, dist = 1e+31;
    for (int i = 0; i < n; i++) {
        double d2 = (P[0] - h[i][0]) * (P[0] - h[i][0]) + (P[1] - h[i][1]) * (P[1] - h[i][1]) + (P[2] - h[i][2]) * (P[2] - h[i][2]);
        if (d2 < best) { best = d2; tb = u[i]; }
    }
    dist = best;
    if (1e-20 < best) dist = sqrt(best);
    for (int i = 0; i < n - 1; i++) {
        double l = 0, f = 0;
        line_min_dist(h[i], h[i + 1], P, &l, &f);
        if (0.0 <= f && f <= 1.0 && l < dist) { double uu = (u[i + 1] - u[i]) * f; tb = u[i] + uu; dist = l; }
    }
    free(u); free(h);
    double t = tb, prev_val = 0.0, prev_t = 0.0, pos[3], d[3];
    int it = 0;
    sp_limit(s, &t);
    double tm2 = sp_tmid(s);
    sp_eval(s, 1, tm2, pos, d);
    if (1e-12 <= sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])) {
        do {
            double hs = s->dt * 0.0001, a[3], da[3], b[3], db[3];
            sp_eval(s, 0, t - hs, a, da);
            sp_eval(s, 0, t + hs, b, db);
            sp_eval(s, 1, t, pos, d);
            double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l < 1e-20) l = 1e-20;
            l = 1.0 / l;
            double ux = d[0] * l, uy = d[1] * l, uz = d[2] * l;
            double fv = (pos[0] * ux + pos[1] * uy + pos[2] * uz) - (ux * P[0] + uy * P[1] + uz * P[2]);
            if (fabs(fv) < 1e-12) break;
            double g = (a[1] * uy + a[0] * ux + a[2] * uz) - (b[1] * uy + b[0] * ux + b[2] * uz);
            if (fabs(g) < 1e-20) g = 1e-20;
            g = (fv * 0.0002) / g;
            double step = g;
            if (0 < it && 1e-20 <= fabs(prev_val - g)) { double r = (t - prev_t) / (prev_val - g); if (0.0 < r && fabs(r * g) < s->dt) step = r * g; }
            if (fabs(step) < 1e-12) break;
            double dv = s->dt, v = step;
            if (dv < fabs(step)) { v = dv; if (step < dv) v = -dv; }
            prev_t = t; t = t + v;
            sp_limit(s, &t);
            it++;
            prev_val = g;
        } while (it < 15);
        return t;
    }
    return tm2;
}
static double sp_tmid(const Sp *s) { return s->kind == 5 ? nb_tmid(s) : (s->t0 + s->t1) * 0.5; }   /* vtable +0x100: MaNURBSpline::TMid / MaCurve::TMid */
/* MaCubicSpline::BuildPtVtSpline (the 3-argument constructor) */
static Sp *sp_build_ptvt(int n, double (*P)[3], double (*T)[3]) {
    Sp *s = sp_new(n - 1);
    for (int i = 0; i < s->ns; i++) {
        double a = 0, b = 0;
        double ti[3] = { T[i][0], T[i][1], T[i][2] }, tj[3] = { T[i + 1][0], T[i + 1][1], T[i + 1][2] };
        unit3(ti); unit3(tj);
        best_fit_derivs(P[i], ti, P[i + 1], tj, &a, &b);
        double *c = s->c + (size_t)i * 12;
        for (int k = 0; k < 3; k++) { c[k * 4] = P[i][k]; c[k * 4 + 1] = P[i + 1][k]; c[k * 4 + 2] = a * ti[k]; c[k * 4 + 3] = b * tj[k]; }
    }
    for (int i = 0; i < s->ns; i++) sp_alg(s, i);
    sp_gen_ulist(s);
    return s;
}
static Sp *sp_copy(const Sp *a);
/* MaCurve::FitCubicApproxCurve (param_3 / not a NURBS: the general path): points and unit tangents at the BinaryCurveFitParams parameters, BuildPtVtSpline */
static Sp *nb_to_cubic(Sp *s);
static Sp *fit_cubic_approx(const Sp *s, double chord, int *err, int param_3) {
    *err = 0;
    if (s->kind == 5 && !param_3 && s->ord == 4 && !s->rat) {   /* the shortcut: MaNURBSpline::ConvertToCubicSpline */
        Sp *cp = sp_copy((Sp *)s); Sp *r0 = nb_to_cubic(cp); sp_free(cp);
        if (r0) return r0;
    }
    double *t = malloc(5000 * 8);
    double (*P)[3] = malloc(5000 * 24), (*T)[3] = malloc(5000 * 24);
    int n = 0;
    *err = binary_curve_fit_params(s, chord, 5000, t, &n);
    Sp *r = NULL;
    if (!*err) {
        for (int i = 0; i < n; i++) sp_eval(s, 1, t[i], P[i], T[i]);
        for (int i = 0; i < n; i++) { unit3(T[i]); }
        r = sp_build_ptvt(n, P, T);
    }
    free(t); free(P); free(T);
    return r;
}
/* the copy constructor MaCubicSpline(const MaCubicSpline&) */
static Sp *nb_new(int ncp, int ord, int rat);
static Sp *sp_copy(const Sp *a) {
    if (a->kind == 5) {
        Sp *n = nb_new(a->ncp, a->ord, a->rat);
        n->f128 = a->f128; n->f129 = a->f129; n->span = a->span;
        memcpy(n->V, a->V, (size_t)a->ncp * 32); memcpy(n->K, a->K, (size_t)a->nk * 8); memcpy(n->B, a->B, (size_t)a->ord * 32);
        n->t0 = a->t0; n->t1 = a->t1; n->dt = a->dt; n->off = a->off;
        return n;
    }
    Sp *s = sp_new(a->ns);
    s->off = a->off;
    memcpy(s->c, a->c, (size_t)a->ns * 96); memcpy(s->u, a->u, ((size_t)a->ns + 1) * 8);
    s->t0 = a->t0; s->t1 = a->t1; s->dt = a->dt;
    return s;
}
/* CopySplineToThis */
static void sp_copy_to(Sp *dst, const Sp *src) {
    free(dst->c); free(dst->u);
    dst->ns = src->ns; dst->c = malloc((size_t)src->ns * 96); dst->u = malloc(((size_t)src->ns + 1) * 8);
    memcpy(dst->c, src->c, (size_t)src->ns * 96); memcpy(dst->u, src->u, ((size_t)src->ns + 1) * 8);
    set_range(dst, src->t0, src->t1);
    set_dirty(dst);
}
/* MaCubicSpline::Break(double, bool, MaCurve*&): keep_first: this keeps the first part and *second gets the second; otherwise the opposite (the Truncate of MaCurve frees *second).
   0 = broke; 1 = no break (t at TMin / TMax). */
static int nb_break(Sp *s, double t, int keep_first, Sp **second);
static int sp_break(Sp *s, double t, int keep_first, Sp **second) {
    if (s->kind == 5) return nb_break(s, t, keep_first, second);
    *second = NULL;
    if (t == u_min(s) || t == u_max(s)) return 1;
    int k = 0; double lt = 0;
    sp_local(s, t, &k, &lt);
    int iseg = k, knot = 0;                 /* bVar13: the break is at a node, no segment is cut */
    double frac = 1.0;
    if (s->ns < 2) frac = lt;
    else if (1e-12 <= t - s->u[k]) {
        if ((1e-12 <= s->u[k + 1] - t) || (s->ns - 1 <= k)) frac = lt;
        else { knot = 1; frac = 1.0; }
    } else {
        if (k < 1) frac = lt;
        else { knot = 1; iseg = k - 1; frac = 1.0; }
    }
    Cu a, b;
    sp_getseg(s, iseg, &a);
    cu_truncate(&a, 0.0, frac);
    int n1 = iseg + 1;
    double *c1 = malloc((size_t)n1 * 96), *u1 = malloc(((size_t)iseg + 2) * 8);
    memcpy(c1, s->c, (size_t)iseg * 96);
    memcpy(c1 + (size_t)iseg * 12, a.c, 96);
    memcpy(u1, s->u, (size_t)n1 * 8);
    u1[iseg + 1] = t;
    { double d0 = u1[0], inv = 1.0 / (t - d0); for (int i = 0; i < iseg + 2; i++) u1[i] = (u1[i] - d0) * inv; }
    sp_getseg(s, iseg, &b);
    int n2;
    if (knot) { n2 = (s->ns - iseg) - 1; if (n2 < 1) n2 = 1; }
    else { cu_truncate(&b, frac, 1.0); n2 = s->ns - iseg; }
    double *c2 = malloc((size_t)n2 * 96), *u2 = malloc(((size_t)n2 + 1) * 8);
    if (knot) memcpy(c2, s->c + ((size_t)iseg + 1) * 12, (size_t)n2 * 96);
    else { memcpy(c2, b.c, 96); if (n2 > 1) memcpy(c2 + 12, s->c + ((size_t)iseg + 1) * 12, (size_t)(n2 - 1) * 96); }
    u2[0] = t;
    if (!knot) memcpy(u2 + 1, s->u + n1, (size_t)n2 * 8);
    else memcpy(u2 + 1, s->u + (size_t)iseg + 2, (size_t)n2 * 8);
    { double d0 = u2[0], inv = 1.0 / (u2[n2] - d0); for (int i = 0; i < n2 + 1; i++) u2[i] = (u2[i] - d0) * inv; }
    Sp *o = sp_new(0); free(o->c); free(o->u); o->t0 = 0; o->t1 = 1; o->dt = 1;
    if (keep_first) { o->ns = n2; o->c = c2; o->u = u2; free(s->c); free(s->u); s->ns = n1; s->c = c1; s->u = u1; }
    else { o->ns = n1; o->c = c1; o->u = u1; free(s->c); free(s->u); s->ns = n2; s->c = c2; s->u = u2; }
    *second = o;
    set_dirty(s);
    set_range(s, u_min(s), u_max(s));
    return 0;
}
/* MaCurve::Truncate(double,double) (virtual +0x88 of MaCubicSpline): periodic trims never occur on this path (param_1 < param_2 here) */
static void sp_concat(Sp *s, const Sp *o);
static int sp_truncate_ab(Sp *s, double a, double b) {
    double d = s->t0;
    if (d < a && a < d + 1e-12) a = d;
    d = s->t1;
    if (b < d && d - 1e-12 < b) b = d;
    if (a - u_min(s) <= 1e-12) a = u_min(s);
    if (u_max(s) - b <= 1e-12) b = u_max(s);
    if (a < u_min(s)) a = u_min(s);
    if (u_max(s) < b) b = u_max(s);
    if (a == b) return 1;
    int pe = 0, cl = 0;
    if (b < a) { sp_closed(s, -1.0, &cl, &pe); if (!pe) { double t = a; a = b; b = t; } }
    if (a == u_min(s) && b == u_max(s) && !is_trimmed(s)) return 1;
    Sp *r = NULL;
    if (a == u_min(s)) { if (sp_break(s, b, 1, &r) != 0) return 1; }
    else if (b == u_max(s)) { if (sp_break(s, a, 0, &r) != 0) return 1; }
    else if (pe && b < a) {                      /* MaCurve::Truncate: a trim across the seam of a periodic curve: the piece after a, the piece before b, Concatenate */
        Sp *c = sp_copy(s);
        if (sp_break(s, a, 0, &r) != 0) { sp_free(c); return 1; }
        sp_free(r); r = NULL;
        if (sp_break(c, b, 1, &r) != 0) { sp_free(c); return 1; }
        sp_free(r); r = NULL;
        if (s->kind == 5) { sp_free(c); return 1; }  /* MaNURBSpline::Concatenate across a seam: not reachable from the Refit driver (NURBS trims never rotate past the seam there) */
        sp_concat(s, c);
        sp_free(c);
        goto done;
    } else {
        double tmax = u_max(s);
        if (sp_break(s, a, 0, &r) == 0) { sp_free(r); r = NULL; }
        if (sp_break(s, (b - a) / (tmax - a), 1, &r) != 0) { sp_free(r); goto done; }
    }
    sp_free(r);
done:
    set_dirty(s);
    set_range(s, u_min(s), u_max(s));
    return 0;
}
static void sp_truncate_if_trimmed(Sp *s) { if (is_trimmed(s)) sp_truncate_ab(s, s->t0, s->t1); }   /* MaCurve::Truncate(void) */
/* MaCubicSpline::GetTListAtSlopeDiscontinuities: nodes with a slope break above angle (degrees); empty when smooth (cf. HasSharpNodes) or one segment */
static int nb_slope_breaks(const Sp *s, double angle, double **list);
static int sp_slope_breaks(const Sp *s, double angle, double **list) {
    if (s->kind == 5) return nb_slope_breaks(s, angle, list);
    *list = NULL;
    if (s->ns == 1) return 0;
    double c = cos(angle * DEG2RAD);
    Cu a, b; double pa[3], da[3], pb[3], db[3];
    int sharp = 0;
    /* IsSmooth(param_1) (IsDegenerate at the middle first: MaCurve::IsDegenerate(1e-12)) */
    { double p[3], d[3]; sp_eval(s, 1, (s->t0 + s->t1) * 0.5, p, d); if (sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) <= 1e-12) return 0; }
    for (int i = 1; i < s->ns; i++) {
        double u = s->u[i];
        if (s->t0 <= u && u <= s->t1) {
            sp_getseg(s, i - 1, &a); sp_getseg(s, i, &b);
            cu_eval1(&a, 1.0, pa, da); cu_eval1(&b, 0.0, pb, db);
            unit3(da); unit3(db);
            if (db[0] * da[0] + db[1] * da[1] + db[2] * da[2] < c) { sharp = 1; break; }
        }
    }
    if (!sharp) return 0;
    double *L = malloc(((size_t)s->ns + 1) * 8);
    int n = 0;
    for (int i = 1; i < s->ns; i++) {
        sp_getseg(s, i - 1, &a); sp_getseg(s, i, &b);
        cu_eval1(&a, 1.0, pa, da); cu_eval1(&b, 0.0, pb, db);
        unit3(da); unit3(db);
        if (db[0] * da[0] + db[1] * da[1] + db[2] * da[2] < c) {
            double u = s->u[i];
            if (s->t0 < u && u < s->t1) L[n++] = u;
        }
    }
    *list = L;
    return n;
}
/* MaCubicSpline::IsSmooth(double) (IsDegenerate at TMid: smooth; else !HasSharpNodes(angle)) */
static int nb_has_sharp(const Sp *s, double angle);
static int sp_is_smooth(const Sp *s, double angle) {
    double p[3], d[3];
    sp_eval(s, 1, sp_tmid(s), p, d);
    if (sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) <= 1e-12) return 1;
    if (s->kind == 5) return !nb_has_sharp(s, angle);
    if (s->ns == 1) return 1;
    double c = cos(angle * DEG2RAD);
    Cu a, b; double pa[3], da[3], pb[3], db[3];
    for (int i = 1; i < s->ns; i++) {
        double u = s->u[i];
        if (s->t0 <= u && u <= s->t1) {
            sp_getseg(s, i - 1, &a); sp_getseg(s, i, &b);
            cu_eval1(&a, 1.0, pa, da); cu_eval1(&b, 0.0, pb, db);
            unit3(da); unit3(db);
            if (db[0] * da[0] + db[1] * da[1] + db[2] * da[2] < c) return 0;
        }
    }
    return 1;
}
/* MaCubicSpline::IsSmooth(void) / HasSharpNodes(void): all interior nodes, 0.1 degree */
static int sp_is_smooth0(const Sp *s) {
    double c = cos(0.001745329251994329);
    if (s->ns != 1)
        for (int i = 0; i < s->ns - 1; i++) {
            Cu a, b; double pa[3], da[3], pb[3], db[3];
            sp_getseg(s, i, &a); sp_getseg(s, i + 1, &b);
            cu_eval1(&a, 1.0, pa, da); cu_eval1(&b, 0.0, pb, db);
            unit3(da); unit3(db);
            if (db[0] * da[0] + db[1] * da[1] + db[2] * da[2] < c) return 0;
        }
    return 1;
}
/* MaCubicSpline::Length (Gauss on each segment within [a, b]) */
static const double GX[9] = { 0x1.04d4d140defacp-6, 0x1.4fceec3cbf598p-4, 0x1.8be85bf1004eep-3, 0x1.59fb748dbaad5p-2, 0x1.0000000000000p-1, 0x1.530245b922a95p-1, 0x1.9d05e903bfec4p-1, 0x1.d60622786814dp-1, 0x1.f7d95975f9083p-1 };
static const double GW[9] = { 0x1.4ce65f803eedap-5, 0x1.71f7a9b222bb8p-4, 0x1.0add87c8274ecp-3, 0x1.3fd7e9838d502p-3, 0x1.522a43f65486ep-3, 0x1.3fd7e9838d502p-3, 0x1.0add87c8274ecp-3, 0x1.71f7a9b222bb8p-4, 0x1.4ce65f803eedap-5 };
static double sp_length(const Sp *s, double t1, double t2) {
    double sum = 0.0;
    for (int i = 0; i < s->ns; i++) {
        double ui = s->u[i], vi = s->u[i + 1];
        if (t1 < vi && ui < t2) {
            double a = 0.0, b = 1.0;
            if (ui < t1 || t2 < vi) {
                if (t1 < ui) { if (ui < t1 || vi < t2) continue; b = (t2 - ui) / (vi - ui); a = 0.0; }
                else if (t2 < vi) { a = (t1 - ui) / (vi - ui); b = (t2 - ui) / (vi - ui); }
                else a = (t1 - ui) / (vi - ui);
            }
            Cu q; sp_getseg(s, i, &q);
            for (int k = 0; k < 9; k++) {
                double w = GW[k], p[3], d[3];
                cu_eval1(&q, (b - a) * GX[k] + a, p, d);
                double sp = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                sum = sp * (b - a) * w + sum;
            }
        }
    }
    return sum;
}
/* MaCurve::ParamAtArcLength: 0 ok, 1 past the curve */
static int sp_param_at_arc(const Sp *s, double f, double t, double *out) {
    *out = t;
    double lo = s->t0, hi = s->t1, a = t, b = hi, tt = lo;
    int pos = 0.0 <= f;
    if (!pos) { a = lo; b = t; }
    double L = sp_length(s, a, b);
    if (fabs(f) <= L) {
        double step = (fabs(f) / L) * (b - a);
        if (pos) tt = t + step;
        else if (f < 0.0) tt = t - step;
        double t3 = tt;
        for (int i = 0; i < 15; i++) {
            double p0 = a, p1 = t3;
            if (!pos) { p0 = t3; p1 = b; }
            double L2 = sp_length(s, p0, p1);
            if (fabs(fabs(f) - L2) <= 1e-12) break;
            double pp[3], d[3];
            sp_eval(s, 1, t3, pp, d);
            double sp = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (pos) t3 = t3 + (fabs(f) - L2) / sp; else t3 = t3 - (fabs(f) - L2) / sp;
            if (t3 < a) t3 = a;
            if (b < t3) t3 = b;
        }
        *out = t3;
        return 0;
    }
    return 1;
}
static void sp_move_start(Sp *s, const double p[3]) {   /* MaCubicSpline::MoveStartPoint */
    Cu q; sp_getseg(s, 0, &q);
    cu_geo(&q);
    q.c[0] = p[0]; q.c[4] = p[1]; q.c[8] = p[2];
    cu_alg(&q);
    sp_setseg(s, &q, 0);
    set_dirty(s);
}
static void sp_move_end(Sp *s, const double p[3]) {     /* MaCubicSpline::MoveEndPoint */
    Cu q; sp_getseg(s, s->ns - 1, &q);
    cu_geo(&q);
    q.c[1] = p[0]; q.c[5] = p[1]; q.c[9] = p[2];
    cu_alg(&q);
    sp_setseg(s, &q, s->ns - 1);
    set_dirty(s);
}
/* MaCubicSpline::Concatenate */
static void sp_concat(Sp *s, const Sp *o) {
    int n = s->ns + o->ns;
    double *c = malloc((size_t)n * 96), *u = malloc(((size_t)n + 1) * 8);
    memcpy(c, s->c, (size_t)s->ns * 96);
    memcpy(c + (size_t)s->ns * 12, o->c, (size_t)o->ns * 96);
    free(s->c); free(s->u);
    s->ns = n; s->c = c; s->u = u;
    sp_gen_ulist(s);
    set_dirty(s);
}
/* MaCubicSpline::JoinSplines(MaCurve const**, int) */
static void sp_join(Sp *s, Sp **list, int n) {
    if (n != 0) {
        sp_truncate_if_trimmed(s);
        for (int i = 0; i < n; i++) {
            Sp *q = list[i];
            sp_truncate_if_trimmed(q);
            double a[3], da[3], b[3], db[3], m[3];
            sp_eval(s, 0, s->t1, a, da);
            sp_eval(q, 0, q->t0, b, db);
            for (int k = 0; k < 3; k++) m[k] = (a[k] + b[k]) * 0.5;
            sp_move_end(s, m);
            sp_move_start(q, m);
            sp_concat(s, q);
        }
    }
}
/* MaCubicSpline::SetEndDerivsPeriodic */
static void sp_end_derivs_periodic(Sp *s) {
    if (is_trimmed(s)) return;
    int i0 = 0, i1 = 1; double lt, tt;
    tt = s->u[0]; sp_local(s, tt, &i0, &lt);
    Cu a, b;
    sp_getseg(s, i0, &a);
    tt = s->u[s->ns]; sp_local(s, tt, &i1, &lt);
    sp_getseg(s, i1, &b);
    double p[3], da[3], db[3];
    cu_eval1(&a, 0.0, p, da);
    double la = sqrt(da[0] * da[0] + da[1] * da[1] + da[2] * da[2]);
    double ua[3] = { 0, 0, 0 };
    if (la != 0.0) { ua[0] = da[0] / la; ua[1] = da[1] / la; ua[2] = da[2] / la; }
    cu_eval1(&b, 1.0, p, db);
    double lb = sqrt(db[0] * db[0] + db[1] * db[1] + db[2] * db[2]);
    double ub[3] = { 0, 0, 0 };
    if (lb != 0.0) { ub[0] = db[0] / lb; ub[1] = db[1] / lb; ub[2] = db[2] / lb; }
    double mx = ub[0] + ua[0], my = ub[1] + ua[1], mz = ub[2] + ua[2];
    double lm = sqrt(mx * mx + my * my + mz * mz);
    double nx = 0, ny = 0, nz = 0;
    if (lm != 0.0) { nx = mx / lm; ny = my / lm; nz = mz / lm; }
    double sa[3] = { nx * la, ny * la, nz * la }, ea[3] = { nx * lb, ny * lb, nz * lb };
    cu_geo(&a); for (int k = 0; k < 3; k++) a.c[k * 4 + 2] = sa[k]; cu_alg(&a);
    cu_geo(&b); for (int k = 0; k < 3; k++) b.c[k * 4 + 3] = ea[k]; cu_alg(&b);
    sp_setseg(s, &a, i0); sp_setseg(s, &b, i1);
    set_dirty(s);
}
/* MaCurve::SnapEndPointsClosed (MaCubicSpline::IsClosed(-1.0)) */
static void sp_snap_closed(Sp *s) {
    int c = 0;
    sp_closed(s, -1.0, &c, NULL);
    if (c) {
        double a[3], d[3], b[3], m[3];
        sp_eval(s, 0, s->t0, a, d);
        sp_eval(s, 0, s->t1, b, d);
        for (int k = 0; k < 3; k++) m[k] = (a[k] + b[k]) * 0.5;
        sp_move_start(s, m);
        sp_move_end(s, m);
    }
}
/* one-segment spline MaCubicSpline(const MaCubicCurve*) */
static Sp *sp_from_cu(const Cu *q) { Sp *s = sp_new(1); memcpy(s->c, q->c, 96); s->u[0] = 0.0; s->u[1] = 1.0; return s; }
/* MaCubicCurve::SetEndDeriv / SetStartDeriv on a local copy */
static void cu_set_end_deriv(Cu *q, const double v[3]) { cu_geo(q); for (int k = 0; k < 3; k++) q->c[k * 4 + 3] = v[k]; cu_alg(q); }
static void cu_set_start_deriv(Cu *q, const double v[3]) { cu_geo(q); for (int k = 0; k < 3; k++) q->c[k * 4 + 2] = v[k]; cu_alg(q); }
typedef struct { double a, b; } TPair;
/* MaCubicSpline::BlendAndFairNodes(param_1 chord (unused by the body), param_2 fair distance, blend list, n, smooth list, m). Returns 0 ok or 2 when two blend spans overlapped (bVar6). */
static int blend_and_fair(Sp *s, double fair, const double *blend, int nb, const double *smooth, int ms) {
    for (int i = 0; i < ms; i++) {
        double t = smooth[i];
        int a = 0, b = 0; double lt;
        sp_local(s, t - 1e-12, &a, &lt);
        Cu qa; sp_getseg(s, a, &qa);
        sp_local(s, t + 1e-12, &b, &lt);
        Cu qb; sp_getseg(s, b, &qb);
        double p[3], d[3], ma, mb;
        cu_eval1(&qa, 1.0, p, d);
        ma = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        double ua[3] = { 0, 0, 0 }, ub[3] = { 0, 0, 0 };
        if (ma != 0.0) { ua[0] = d[0] / ma; ua[1] = d[1] / ma; ua[2] = d[2] / ma; }
        cu_eval1(&qb, 0.0, p, d);
        mb = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (mb != 0.0) { ub[0] = d[0] / mb; ub[1] = d[1] / mb; ub[2] = d[2] / mb; }
        double x = ua[0] + ub[0], y = ua[1] + ub[1], z = ua[2] + ub[2];
        double l = sqrt(y * y + x * x + z * z);
        double nx = 0, ny = 0, nz = 0;
        if (l != 0.0) { nx = x / l; ny = y / l; nz = z / l; }
        double e[3] = { nx * ma, ny * ma, nz * ma }, st[3] = { nx * mb, ny * mb, nz * mb };
        cu_set_end_deriv(&qa, e);
        cu_set_start_deriv(&qb, st);
        sp_setseg(s, &qa, a);
        sp_setseg(s, &qb, b);
    }
    int overlap = 0;
    double prev = 0.0;
    if (nb > 0) {
        Sp **arcs = calloc((size_t)nb, sizeof(Sp *));
        TPair *pr = calloc((size_t)nb, sizeof(TPair));
        int na = 0;
        for (int i = 0; i < nb; i++) {
            double t = blend[i], a = 0, b = 0;
            if (sp_param_at_arc(s, -fair, t - 1e-12, &a) == 0) {
                if (a < prev) { a = prev; overlap = 1; }
                double pa[3], da[3];
                sp_eval(s, 1, a, pa, da);
                unit3(da);
                int r2 = sp_param_at_arc(s, fair, t + 1e-12, &b);
                prev = b;
                if (r2 == 0) {
                    double pb[3], db[3];
                    sp_eval(s, 1, b, pb, db);
                    unit3(db);
                    Cu q; gen_best_fit(&q, pa, da, pb, db);
                    arcs[na] = sp_from_cu(&q);
                    pr[na].a = a; pr[na].b = b;
                    na++;
                }
            }
        }
        if (na == 0) { free(arcs); free(pr); return 0; }
        Sp **lst = calloc(2 * (size_t)na + 2, sizeof(Sp *));
        int nl = 0;
        Sp *c = sp_copy(s);
        sp_truncate_ab(c, c->t0, pr[0].a);
        lst[nl++] = c;
        lst[nl++] = arcs[0];
        for (int j = 1; j < na; j++) {
            double b0 = pr[j - 1].b, a1 = pr[j].a;
            if (1e-12 < a1 - b0) {
                Sp *c2 = sp_copy(s);
                sp_truncate_ab(c2, b0, a1);
                lst[nl++] = c2;
            }
            lst[nl++] = arcs[j];
        }
        { Sp *c2 = sp_copy(s);
          sp_truncate_ab(c2, pr[na - 1].b, c2->t1);
          lst[nl++] = c2; }
        sp_copy_to(s, lst[0]);
        if (nl > 1) sp_join(s, lst + 1, nl - 1);
        for (int j = 0; j < nl; j++) sp_free(lst[j]);
        free(lst); free(arcs); free(pr);
    }
    int closed = 0, peri = 0;
    sp_closed(s, -1.0, &closed, &peri);
    if (!peri) {
        /* MaCurve::IsClosed(0.0) (vt+0x18): GetClosedInfo is cached (+0x23) by the IsPeriodic(-1.0) just above, the 0.0 is not used */
        if (closed && nb >= 1) {
            double a = 0, b = 0;
            if (sp_param_at_arc(s, -fair, s->t1, &a) == 0) {
                double pa[3], da[3];
                sp_eval(s, 1, a, pa, da);
                unit3(da);
                if (sp_param_at_arc(s, fair, s->t0, &b) == 0) {
                    double pb[3], db[3];
                    sp_eval(s, 1, b, pb, db);
                    unit3(db);
                    Cu q; gen_best_fit(&q, pa, da, pb, db);
                    Sp *arc = sp_from_cu(&q);
                    sp_truncate_ab(s, b, a);
                    sp_concat(s, arc);
                    sp_free(arc);
                }
            }
        }
    } else sp_end_derivs_periodic(s);
    return overlap ? 2 : 0;
}
/* MaCubicSpline::MakeSmoothFitCubicSpline */
static int make_smooth_fit(const Sp *s, double chord, double angle, double fair, Sp **out) {
    double c = cos(angle * DEG2RAD);
    Sp *q = sp_copy(s);
    *out = q;
    sp_truncate_if_trimmed(q);
    double *L = NULL;
    int n = sp_slope_breaks(q, 0.1, &L);
    if (n == 0) { sp_free(q); *out = NULL; free(L); return 1; }
    double *bl = malloc((size_t)n * 8), *sm = malloc((size_t)n * 8);
    int nb = 0, ms = 0;
    for (int i = 0; i < n; i++) {
        double p[3], d1[3], d2[3];
        sp_eval(q, 1, L[i] - 1e-12, p, d1);
        double l1 = sqrt(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]);
        double u1[3] = { 0, 0, 0 };
        if (l1 != 0.0) { u1[0] = d1[0] / l1; u1[1] = d1[1] / l1; u1[2] = d1[2] / l1; }
        sp_eval(q, 1, L[i] + 1e-12, p, d2);
        double l2 = sqrt(d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2]);
        double u2[3] = { 0, 0, 0 };
        if (l2 != 0.0) { u2[0] = d2[0] / l2; u2[1] = d2[1] / l2; u2[2] = d2[2] / l2; }
        if (c < u2[1] * u1[1] + u2[0] * u1[0] + u2[2] * u1[2]) sm[ms++] = L[i];
        else bl[nb++] = L[i];
    }
    int r = blend_and_fair(q, fair, bl, nb, sm, ms);
    free(bl); free(sm); free(L);
    if (0 < ms) {
        int e = 0;
        Sp *q2 = fit_cubic_approx(q, chord, &e, 1);
        if (q2) { sp_copy_to(q, q2); sp_free(q2); }
    }
    return r;
}
static Sp *sp_cubic_to_nurb(Sp *s);
/* MaNURBSpline::HasSharpNodes(double), GetTListAtSlopeDiscontinuities */
static int nb_has_sharp(const Sp *s, double angle) {
    if (s->ncp == s->ord) return 0;
    double co = cos(angle * DEG2RAD);
    if (kn_max_mult(s) < s->ord - 1) return 0;
    double *U = malloc((size_t)s->nk * 8); int n = 0, r = 0;
    kn_unique(s, U, &n);
    if (n >= 3) for (int i = 1; i < n - 1; i++) {
        double x = U[i], p[3], a[3], b[3];
        if (s->t0 <= x && x <= s->t1) {
            sp_eval(s, 1, x - 1e-06, p, a);
            sp_eval(s, 1, U[i] + 1e-06, p, b);
            unit3(a); unit3(b);
            if (a[0] * b[0] + b[1] * a[1] + b[2] * a[2] < co) { r = 1; break; }
        }
    }
    free(U);
    return r;
}
static int nb_slope_breaks(const Sp *s, double angle, double **list) {
    *list = NULL;
    if (s->ncp == s->ord) return 0;
    if (kn_max_mult(s) < s->ord - 1) return 0;
    double *U = malloc((size_t)s->nk * 8); int n = 0, c = 0;
    kn_unique(s, U, &n);
    if (n >= 3) {
        double co = cos(angle * DEG2RAD);
        double *L = malloc((size_t)s->nk * 8);
        *list = L;
        for (int i = 1; i < n - 1; i++) {
            double p[3], a[3], b[3];
            sp_eval(s, 1, U[i] - 1e-06, p, a);
            sp_eval(s, 1, U[i] + 1e-06, p, b);
            unit3(a); unit3(b);
            double x = U[i];
            if (a[0] * b[0] + b[1] * a[1] + b[2] * a[2] < co && s->t0 <= x && x != s->t0 && x < s->t1) L[c++] = x;
        }
    }
    free(U);
    return c;
}
/* MaNURBSpline::ConvertToCubicSpline: 0 when not order 4 nonrational */
static Sp *nb_to_cubic(Sp *s) {
    if (s->ord - 1 != 3 || s->rat == 1) return NULL;
    sp_truncate_if_trimmed(s);
    double *U = malloc((size_t)s->nk * 8), p[3], d[3]; int n = 0;
    kn_unique(s, U, &n);
    int ns = n - 1;
    double *C = malloc((size_t)ns * 96);
    for (int i = 0; i < ns; i++) {
        nb_eval(s, 0, (U[i + 1] + U[i]) * 0.5, p, d);
        const double *B = s->B;
        Cu q;
        for (int k = 0; k < 3; k++) {
            q.c[k * 4] = B[k]; q.c[k * 4 + 1] = B[12 + k];
            q.c[k * 4 + 2] = (B[4 + k] - B[k]) * 3.0; q.c[k * 4 + 3] = (B[12 + k] - B[8 + k]) * 3.0;
        }
        cu_alg(&q);
        memcpy(C + (size_t)i * 12, q.c, 96);
    }
    Sp *c = sp_new(ns);
    memcpy(c->c, C, (size_t)ns * 96);
    sp_gen_ulist(c);
    set_range(c, s->t0, s->t1);
    free(U); free(C);
    return c;
}
/* MaCubicSpline::RemoveDuplicatePoints + FairSplineTangents, the constructor (int, pts, tangents, flags) */
static void fair_spline_tangents(double (*Q)[3], double (*T)[3], double (*R)[3], const int *f, int n) {
    if (n < 2) return;
    for (int i = 0; i < n; i++) {
        if ((unsigned)(f[i] - 1) < 2) {
            double a = T[i][0], l = sqrt(a * a + T[i][1] * T[i][1] + T[i][2] * T[i][2]); if (l < 1e-20) l = 1e-20;
            l = 1.0 / l;
            T[i][0] = a * l; T[i][1] = l * T[i][1]; T[i][2] = l * T[i][2];
            if (f[i] != 2) { memcpy(R[i], T[i], 24); continue; }
        }
        R[i][0] = R[i][1] = R[i][2] = 0.0;
    }
    double *c = malloc((size_t)n * 8), (*e)[3] = malloc((size_t)n * 24);
    for (int i = 0; i < n - 1; i++) {
        double x = Q[i + 1][0] - Q[i][0], y = Q[i + 1][1] - Q[i][1], z = Q[i + 1][2] - Q[i][2];
        double l = sqrt(y * y + x * x + z * z); if (l < 1e-20) l = 1e-20;
        c[i] = l; l = 1.0 / l;
        e[i][0] = x * l; e[i][1] = y * l; e[i][2] = z * l;
    }
    if (n == 2) {
        int bv = 0, a = 0, b = 0, doit = 1;
        double sg = 1.0;
        if (f[0] == 1) { if (f[1] == 1) { doit = 0; } else { sg = 1.0; a = 0; b = 1; } }
        else { if (f[1] != 1) { bv = 1; doit = 0; } else { sg = -1.0; a = 1; b = 0; } }
        if (doit) {
            double dt = e[0][1] * R[a][1] + R[a][0] * e[0][0] + e[0][2] * R[a][2];
            if (fabs(dt) < 1e-20) dt = 1e-20;
            double k = ((c[0] * 0.5) / dt) * sg, ng = -sg;
            double x = ((k * R[a][0] + Q[a][0]) - Q[b][0]) * ng, y = ((k * R[a][1] + Q[a][1]) - Q[b][1]) * ng, z = ((k * R[a][2] + Q[a][2]) - Q[b][2]) * ng;
            double l = sqrt(x * x + y * y + z * z); if (l < 1e-20) l = 1e-20;
            l = 1.0 / l; R[b][0] = x * l; R[b][1] = y * l; R[b][2] = z * l;
        }
        if (f[0] != 1 && f[1] == 1) { }
        for (int i = 0; i < 2; i++) {
            if (bv) memcpy(R[i], e[0], 24);
            if (f[i] == 2) {
                double dt = T[i][1] * R[i][1] + R[i][0] * T[i][0] + T[i][2] * R[i][2];
                R[i][0] = R[i][0] - dt * T[i][0]; R[i][1] = R[i][1] - dt * T[i][1];
                double z = R[i][2] - dt * T[i][2]; R[i][2] = z;
                double a0 = R[i][0], l = sqrt(a0 * a0 + R[i][1] * R[i][1] + z * z); if (l < 1e-20) l = 1e-20;
                l = 1.0 / l; R[i][0] = a0 * l; R[i][1] = l * R[i][1]; R[i][2] = l * R[i][2];
            }
        }
        free(c); free(e);
        return;
    }
    for (int iter = 0;;) {
        int bv = 0, bw = 0, pass = 1, start = 1;
        for (;;) {
            for (int k = start; k < n - 1; k += 2) {
                int fk = f[k], compute = 0;
                double x = 0, y = 0, z = 0;
                if (iter == 0 && fk == 1) compute = 1;
                else if (fk != 1) {
                    if (fk == 2) compute = 1;
                    else {
                        y = e[k][1]; x = e[k][0]; z = e[k][2];
                        if (fabs(y * e[k - 1][1] + x * e[k - 1][0] + z * e[k - 1][2]) < 0.999999999999) compute = 1;
                    }
                }
                if (!(fk == 1 && !compute) && (compute || fk != 1)) {
                    if (compute) {
                        double px = Q[k][0], py = Q[k][1], pz = Q[k][2], S[2][3];
                        for (int side = 0; side < 2; side++) {
                            double sign = side == 0 ? 1.0 : -1.0;
                            int ci = side == 0 ? k - 1 : k, ri = side == 0 ? k - 1 : k + 1;
                            double dt = e[ci][1] * R[ri][1] + e[ci][0] * R[ri][0] + e[ci][2] * R[ri][2];
                            if (1.0 <= fabs(dt) && fabs(dt) != 1.0) dt = dt / fabs(dt);
                            double v = fabs(dt * 2.0 + 1.0); if (v < 1e-20) v = 1e-20;
                            v = (sign * c[ci]) / v;
                            if (iter == 0 && f[ri] != 1) v = 0.0;
                            double tz = (v * R[ri][2] + Q[ri][2]) - pz, tx = (v * R[ri][0] + Q[ri][0]) - px, ty = (v * R[ri][1] + Q[ri][1]) - py;
                            double l = sqrt(ty * ty + tx * tx + tz * tz); if (l < 1e-20) l = 1e-20;
                            double opp = side == 0 ? c[k] : c[k - 1];
                            S[side][0] = (tx / l) * opp; S[side][1] = (ty / l) * opp; S[side][2] = (tz / l) * opp;
                        }
                        x = S[1][0] - S[0][0]; y = S[1][1] - S[0][1]; z = S[1][2] - S[0][2];
                        bv = bw;
                        if (fk == 2) {
                            double dt = y * T[k][1] + x * T[k][0] + z * T[k][2];
                            x = x - dt * T[k][0]; y = y - dt * T[k][1]; z = z - dt * T[k][2];
                        }
                    }
                    double l = sqrt(x * x + y * y + z * z); if (l < 1e-20) l = 1e-20;
                    l = 1.0 / l; x = x * l; y = y * l; z = z * l;
                    if (fk == 1) {
                        if (y * R[k][1] + x * R[k][0] + z * R[k][2] < 0.0) { R[k][0] = R[k][0] * -1.0; R[k][1] = R[k][1] * -1.0; R[k][2] = R[k][2] * -1.0; }
                    } else {
                        if (!bv && y * R[k][1] + x * R[k][0] + z * R[k][2] < 0.999999999) { bv = 1; bw = 1; }
                        R[k][0] = x; R[k][1] = y; R[k][2] = z;
                    }
                }
            }
            if (pass == 2) break;
            pass = 2; start = 2;
            if (!(3 < n)) break;
        }
        for (int i = 0; i < 2; i++) {
            int idx = i == 1 ? n - 1 : 0, ci = i == 1 ? n - 2 : 0, ni = i == 1 ? n - 2 : 1;
            if (f[idx] != 1) {
                double sg = i == 0 ? -1.0 : 1.0;
                double dt = R[ni][1] * e[ci][1] + R[ni][0] * e[ci][0] + R[ni][2] * e[ci][2];
                if (fabs(dt) < 1e-20) dt = 1e-20;
                double k = ((c[ci] * 0.5) / dt) * sg, ng = -sg;
                double x = ((k * R[ni][0] + Q[ni][0]) - Q[idx][0]) * ng, y = ((k * R[ni][1] + Q[ni][1]) - Q[idx][1]) * ng, z = ((k * R[ni][2] + Q[ni][2]) - Q[idx][2]) * ng;
                if (f[idx] == 2) {
                    double d2 = y * T[idx][1] + x * T[idx][0] + z * T[idx][2];
                    x = x - d2 * T[idx][0]; y = y - d2 * T[idx][1]; z = z - d2 * T[idx][2];
                }
                double l = sqrt(x * x + y * y + z * z); if (l < 1e-20) l = 1e-20;
                l = 1.0 / l; R[idx][0] = x * l; R[idx][1] = y * l; R[idx][2] = z * l;
            }
        }
        if ((n == 3 && f[0] == 0 && f[2] == 0) || !bw) break;
        iter++;
        if (iter > 0x13) break;
    }
    free(c); free(e);
}
static Sp *sp_fair_ctor(int n, double (*P)[3], double (*T)[3], int *f) {   /* MaCubicSpline(int, pts, tangents, flags) */
    if (2 < n) {                                          /* RemoveDuplicatePoints */
        int w = 1;
        for (int i = 1; i < n; i++) {
            double dx = P[i][0] - P[w - 1][0], dy = P[i][1] - P[w - 1][1], dz = P[i][2] - P[w - 1][2];
            if (1e-12 < dy * dy + dx * dx + dz * dz) { memcpy(P[w], P[i], 24); memcpy(T[w], T[i], 24); f[w] = f[i]; w++; }
        }
        n = w;
        if (n == 1) { n = 2; memcpy(P[1], P[0], 24); memcpy(T[1], T[0], 24); f[1] = f[0]; }
    }
    double (*R)[3] = malloc((size_t)n * 24);
    fair_spline_tangents(P, T, R, f, n);
    Sp *s = sp_build_ptvt(n, P, R);
    free(R);
    return s;
}
/* MaNURBSpline::FairPiecewiseLinearToCubicSpline */
static Sp *fair_piecewise(const Sp *s0, double angle) {
    if (!nb_is_polyline(s0)) return NULL;
    Sp *s = sp_copy(s0);
    double co = cos(angle * DEG2RAD), *U = malloc((size_t)s->nk * 8), pp[3], d[3];
    sp_truncate_if_trimmed(s);
    int n = 0; kn_unique(s, U, &n);
    double (*P)[3] = malloc((size_t)n * 24), (*T)[3] = calloc((size_t)n, 24);
    int *f = calloc((size_t)n, 4);
    for (int i = 0; i < n; i++) sp_eval(s, 0, U[i], P[i], d);
    for (int i = 1; i < n - 1; i++) {
        double a[3], b[3];
        sp_eval(s, 1, U[i] - 1e-12, pp, a); unit3(a);
        sp_eval(s, 1, U[i] + 1e-12, pp, b); unit3(b);
        if (co <= b[0] * a[0] + b[1] * a[1] + b[2] * a[2]) f[i] = 0;
        else { f[i] = 1; double x = b[0] + a[0], y = b[1] + a[1], z = b[2] + a[2], l = sqrt(x * x + y * y + z * z);
            if (l == 0.0) { x = y = z = 0.0; } else { x = x / l; y = y / l; z = z / l; }
            T[i][0] = x; T[i][1] = y; T[i][2] = z; }
    }
    Sp *r = sp_fair_ctor(n, P, T, f);
    free(U); free(P); free(T); free(f); sp_free(s);
    return r;
}
/* MaNURBSpline::MakeSmoothFitCubicSpline: smooth pieces (ConvertToCubicSpline for cubic non rational, else FitCubicApproxCurve) joined by JoinSplinesSmoothly */
static int nb_make_smooth(Sp *s, double chord, double angle, double fair, Sp **out) {
    *out = NULL;
    double *L = NULL; int cnt = nb_slope_breaks(s, 0.1, &L);
    if (cnt == 0 || !L) { free(L); return 1; }
    int np = cnt + 1, status = 0;
    Sp **res = calloc((size_t)np, sizeof(Sp *));
    for (int i = 0; i < np; i++) {
        Sp *q = sp_copy(s);
        sp_truncate_ab(q, i == 0 ? q->t0 : L[i - 1], i == np - 1 ? q->t1 : L[i]);
        Sp *r = NULL; int e = 0;
        if (q->ord == 4 && q->rat == 0) r = nb_to_cubic(q);
        else r = fit_cubic_approx(q, chord, &e, 1);
        sp_free(q);
        if (e || !r) { status = 1; break; }
        res[i] = r;
    }
    free(L);
    if (status) { for (int i = 0; i < np; i++) sp_free(res[i]); free(res); return 1; }
    Sp *r0 = res[0];
    sp_join(r0, res + 1, np - 1);
    Sp *f = NULL;
    make_smooth_fit(r0, chord, angle, fair, &f);    /* JoinSplinesSmoothly: its status is not used here (MaNURBSpline::MakeSmoothFitCubicSpline) */
    if (f) { sp_copy_to(r0, f); sp_free(f); }
    for (int i = 1; i < np; i++) sp_free(res[i]);
    free(res);
    *out = r0;
    return 0;
}
/* the polyline NURBS: MaPolyLine(pts, n, 3) with WeedOutDegenerateSegments (a segment shorter than tol is dropped), MaNURBSpline(MaPolyLine*) */
static Sp *nb_polyline(double (*P)[3], int n0, double tol) {
    double (*Q)[3] = malloc((size_t)n0 * 24); int n = 1;
    memcpy(Q[0], P[0], 24);
    double la[3]; memcpy(la, P[0], 24);
    for (int i = 1; i < n0; i++) {
        double dy = P[i][1] - la[1], dx = P[i][0] - la[0], dz = P[i][2] - la[2];
        double l = sqrt(dy * dy + dx * dx + dz * dz);
        if (tol <= l) { memcpy(Q[n], P[i], 24); memcpy(la, P[i], 24); n++; }
    }
    Sp *s = nb_new(n, 2, 0);
    for (int i = 0; i < n; i++) { s->V[i * 4] = Q[i][0]; s->V[i * 4 + 1] = Q[i][1]; s->V[i * 4 + 2] = Q[i][2]; s->V[i * 4 + 3] = 1.0; }
    free(Q);
    s->K[0] = 0.0; s->K[1] = 0.0;
    double v = 1.0;
    for (int i = 1; i < n - 1; i++) { s->K[1 + i] = v; v = v + 1.0; }
    s->K[s->nk - 2] = v; s->K[s->nk - 1] = v;
    double a = s->K[0], b = s->K[s->nk - 1];
    for (int i = 0; i < s->nk; i++) s->K[i] = (s->K[i] - a) * (1.0 / (b - a));
    set_range(s, 0.0, 1.0);
    nb_regen_knots(s);
    set_range(s, 0.0, 1.0);
    return s;
}
/* MaNURBSpline::FixStackedEndVertices (only the single span case; the several span case is refused by nb_from_record) */
static int nb_fix_stacked(Sp *s) {
    if (s->ord - 1 < 2) return 0;
    double f = 1.0 / (double)(s->ord - 1);
    double *V = s->V;
    int n = s->ncp;
    #define XY(i, k) (V[(i) * 4 + (k)] / V[(i) * 4 + 3])
    { double a0 = XY(1, 0) - XY(0, 0), a1 = XY(1, 1) - XY(0, 1), a2 = XY(1, 2) - XY(0, 2);
      if (!(1e-12 < sqrt(a1 * a1 + a0 * a0 + a2 * a2))) {
        double w = V[n * 4 - 1], d0 = XY(n - 1, 0) - XY(0, 0), d1 = XY(n - 1, 1) - XY(0, 1), d2 = XY(n - 1, 2) - XY(0, 2);
        (void)w;
        if (!(1e-12 <= sqrt(d1 * d1 + d0 * d0 + d2 * d2))) return 1;
        double w1 = V[7], x = XY(1, 0), y = XY(1, 1), z = XY(1, 2);
        V[4] = (x + d0 * f) * w1; V[5] = (y + d1 * f) * w1; V[6] = (z + d2 * f) * w1; V[7] = w1;
      } }
    { double a0 = XY(n - 2, 0) - XY(n - 1, 0), a1 = XY(n - 2, 1) - XY(n - 1, 1), a2 = XY(n - 2, 2) - XY(n - 1, 2);
      if (sqrt(a1 * a1 + a0 * a0 + a2 * a2) <= 1e-12) {
        double d0 = XY(0, 0) - XY(n - 1, 0), d1 = XY(0, 1) - XY(n - 1, 1), d2 = XY(0, 2) - XY(n - 1, 2);
        if (sqrt(d1 * d1 + d0 * d0 + d2 * d2) < 1e-12) return 1;
        double w2 = V[(n - 2) * 4 + 3], x = XY(n - 2, 0), y = XY(n - 2, 1), z = XY(n - 2, 2);
        V[(n - 2) * 4] = (x + d0 * f) * w2; V[(n - 2) * 4 + 1] = (y + d1 * f) * w2; V[(n - 2) * 4 + 2] = (z + d2 * f) * w2; V[(n - 2) * 4 + 3] = w2;
      } }
    #undef XY
    s->span = -1; set_dirty(s);
    return 0;
}
/* MaNURBSpline(ent) + MakeNURBSplineFromNbType (class 0x80 record: knots at +0xd8, points after them; w first when rational) */
static Sp *nb_from_record(const uint8_t *rec, uint32_t n, double tol, int load_fit, double step, int *err) {
    int32_t nk, ncp; memcpy(&nk, rec + 0xc8, 4); memcpy(&ncp, rec + 0xcc, 4);
    int deg = rec[0xd0], vpp = rec[0xd1], flags = rec[0xd2];
    if (nk < 2 || ncp < 1 || nk != ncp + deg + 1 || 0xd8u + 8u * ((uint32_t)nk + (uint32_t)vpp * ncp) > n) { *err = -1; return NULL; }
    const double *K0 = (const double *)(rec + 0xd8), *C = K0 + nk;
    double t1, t2, sp0[3], ep0[3];
    memcpy(&t1, rec + 0x98, 8); memcpy(&t2, rec + 0xa0, 8);
    memcpy(sp0, rec + 0x68, 24); memcpy(ep0, rec + 0x80, 24);
    int rat = (~(flags >> 2)) & 1;
    double (*rd)[4] = malloc((size_t)ncp * 32);
    for (int i = 0; i < ncp; i++) {
        const double *q = C + (size_t)i * vpp;
        if (vpp == 2) { rd[i][0] = q[0]; rd[i][1] = q[1]; rd[i][2] = 0.0; rd[i][3] = 1.0; }
        else if (vpp == 3) {
            if (rat == 1) { rd[i][0] = q[1]; rd[i][1] = q[2]; rd[i][2] = 0.0; rd[i][3] = q[0]; }
            else { rd[i][0] = q[0]; rd[i][1] = q[1]; rd[i][2] = q[2]; rd[i][3] = 1.0; }
        } else if (vpp == 4 && rat == 1) { rd[i][0] = q[1]; rd[i][1] = q[2]; rd[i][2] = q[3]; rd[i][3] = q[0]; }
        else { free(rd); *err = -1; return NULL; }
    }
    if (deg == 1 && ncp > 2) {            /* MaNURBSpline::HandlePiecewiseLinear */
        double (*P)[3] = malloc((size_t)ncp * 24);
        for (int i = 0; i < ncp; i++) {
            if (rat == 1 && vpp != 2) { P[i][0] = rd[i][0] / rd[i][3]; P[i][1] = rd[i][1] / rd[i][3]; P[i][2] = vpp == 3 ? 0.0 : rd[i][2] / rd[i][3]; }
            else { P[i][0] = rd[i][0]; P[i][1] = rd[i][1]; P[i][2] = rd[i][2]; }
        }
        Sp *poly = nb_polyline(P, ncp, tol);
        free(P);
        Sp *s = NULL;
        int e = 0;
        if (load_fit) {
            Sp *cub = fit_cubic_approx(poly, step, &e, 0);
            if (!e && cub) { Sp *nb = sp_cubic_to_nurb(cub); sp_free(cub); s = nb_new(1, 2, 0); nb_copy_to(s, nb); sp_free(nb); }
        }
        if (!s) { s = nb_new(1, 2, 0); nb_copy_to(s, poly); }
        sp_free(poly);
        double a = 0.0, b = 1.0;
        if (t1 != K0[0]) a = sp_point_min_param(s, sp0);
        if (t2 != K0[nk - 1]) b = sp_point_min_param(s, ep0);
        set_range(s, a, b);
        free(rd);
        return s;
    }
    Sp *s = nb_new(ncp, deg + 1, rat);
    s->f128 = (flags >> 1) & 1; s->f129 = (flags >> 3) & 1;
    for (int i = 0; i < ncp; i++) memcpy(s->V + (size_t)i * 4, rd[i], 32);
    free(rd);
    memcpy(s->K, K0, (size_t)nk * 8);
    double k0 = s->K[0], kl = s->K[nk - 1], inv = 1.0 / (kl - k0);
    for (int i = 0; i < nk; i++) s->K[i] = (s->K[i] - k0) * inv;
    int mm = kn_max_mult(s);
    if (mm == s->ord - 1) {               /* CheckForMaxMultEqualOrder */
        int o = s->ord, nn = s->nk;
        for (int i = o; i < nn - o; i += o) {
            double x = s->K[i];
            if (kn_mult(s, x) != o - 1) break;
            if (1e-06 <= s->K[i + o - 1] - x) break;
            if (s->K[i + o] - s->K[i + o - 1] <= 1e-06) break;
            s->K[i + o - 1] = x;
        }
    }
    int stacked = 0;
    {                                     /* HasDuplicateVertices + CheckForStackedEndVertices: Get3DControlPoints as positions */
        for (int i = 0; i < s->ncp - 1; i++) {
            double wa = s->V[i * 4 + 3], wb = s->V[(i + 1) * 4 + 3];
            double dx = s->V[(i + 1) * 4] / wb - s->V[i * 4] / wa, dy = s->V[(i + 1) * 4 + 1] / wb - s->V[i * 4 + 1] / wa, dz = s->V[(i + 1) * 4 + 2] / wb - s->V[i * 4 + 2] / wa;
            if (sqrt(dx * dx + dy * dy + dz * dz) < 1e-12) { stacked = 1; break; }
        }
        if (stacked) {
            double *U = malloc((size_t)s->nk * 8); int un = 0;
            kn_unique(s, U, &un); free(U);
            if (un >= 3) { sp_free(s); *err = -1; return NULL; }   /* several Bezier spans with stacked vertices (ConvertBezierToNURB, MaNURBSpline::JoinSplines, DeleteSplineData): not ported */
            nb_fix_stacked(s);
        }
    }
    if (stacked) mm = kn_max_mult(s);
    if (mm < s->ord - 1) {
        double a = (t1 - k0) * inv, b = (t2 - k0) * inv;
        set_range(s, a, b);
    } else {
        nb_regen_knots(s);
        double a = 0.0, b = 1.0;
        if (t1 != K0[0]) a = sp_point_min_param(s, sp0);
        if (t2 != K0[nk - 1]) b = sp_point_min_param(s, ep0);
        set_range(s, a, b);
    }
    return s;
}
/* MaCubicSpline(ent) on a non-trimmed order 4 record, the record's cubic coefficients to the algebraic internal order, segments of no speed dropped */
static Sp *sp_from_record(const uint8_t *rec, uint32_t n, int *err) {
    if (0) return NULL;
    int32_t ns0; int ord = rec[0xb4], dim = rec[0xb5];
    memcpy(&ns0, rec + 0xb0, 4);
    if (ord != 4 || (dim != 2 && dim != 3) || ns0 < 1 || 0xc0u + 8u * (uint32_t)ns0 * ord * dim > n) { *err = -1; return NULL; }
    double t1, t2;
    memcpy(&t1, rec + 0x98, 8); memcpy(&t2, rec + 0xa0, 8);
    Sp *s = sp_new(ns0);
    int w = 0, nsn = ns0;
    for (int i = 0; i < ns0; i++) {
        double c[12];
        memcpy(c, rec + 0xc0 + (size_t)i * 8 * 4 * dim, (size_t)(dim * 4) * 8);
        double x3, x2, x1, x0, y3, y2, y1, y0, z3 = 0, z2 = 0, z1 = 0, z0 = 0;
        if (dim == 2) { x0 = c[0]; y0 = c[1]; x1 = c[2]; y1 = c[3]; x2 = c[4]; y2 = c[5]; x3 = c[6]; y3 = c[7]; }
        else { x0 = c[0]; y0 = c[1]; z0 = c[2]; x1 = c[3]; y1 = c[4]; z1 = c[5]; x2 = c[6]; y2 = c[7]; z2 = c[8]; x3 = c[9]; y3 = c[10]; z3 = c[11]; }
        double dVar5 = sqrt(z1 * z1 + y1 * y1 + x1 * x1);
        double dVar7 = x2 * 2.0 + x3 * 3.0 + x1, dVar6 = y2 * 2.0 + y3 * 3.0 + y1, dVar8 = z2 * 2.0 + z3 * 3.0 + z1;
        dVar6 = sqrt(dVar6 * dVar6 + dVar7 * dVar7 + dVar8 * dVar8);
        if (dVar5 < 1e-12 || dVar6 < 1e-12) { nsn--; continue; }
        double *o = s->c + (size_t)w * 12;
        o[0] = x3; o[1] = x2; o[2] = x1; o[3] = x0; o[4] = y3; o[5] = y2; o[6] = y1; o[7] = y0; o[8] = z3; o[9] = z2; o[10] = z1; o[11] = z0;
        w++;
    }
    s->ns = nsn;
    if (nsn < 1) { sp_free(s); *err = 1; return NULL; }
    sp_gen_ulist(s);
    {   /* MaCubicSpline(ent): a trimmed record: MaCurve::PointMinDist of the stored end points (hunting points), SetMinMaxParams */
        double a = 0.0, b = 1.0, sp0[3], ep0[3];
        memcpy(sp0, rec + 0x68, 24); memcpy(ep0, rec + 0x80, 24);
        if (t1 != 0.0) a = sp_point_min_param(s, sp0);
        if (t2 != (double)ns0) b = sp_point_min_param(s, ep0);
        set_range(s, a, b);
    }
    if (is_trimmed(s)) return s;
    /* MaCubicSpline(ent): FixBadStart / EndDerivative when the end speed is below 1e-12 */
    double p[3], d[3];
    sp_eval(s, 1, s->t0, p, d);
    if (sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) < 1e-12) { /* FixBadStartDerivative */
        double t = s->t0; double dv = s->dt;
        while (sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) <= 1e-05) {
            if (dv * 0.1 + s->t0 < t) goto after_start;
            t = t + dv * 1e-05;
            sp_eval(s, 1, t, p, d);
        }
        double e[3] = { d[0], d[1], d[2] }, spd = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        unit3(e);
        { double u1 = s->u[1], u0 = s->u[0];
          Cu q; sp_getseg(s, 0, &q);
          double a0[3], ad[3], b0[3], bd[3];
          cu_eval1(&q, 0.0, a0, ad);
          cu_eval1(&q, 1.0, b0, bd);
          double b1[3] = { bd[0], bd[1], bd[2] }; unit3(b1);
          double aa = 0, bb = 0;
          best_fit_derivs(a0, e, b0, b1, &aa, &bb);
          double v = (aa + spd * (u1 - u0)) * 0.5;
          double st[3] = { e[0] * v, e[1] * v, e[2] * v };
          cu_set_start_deriv(&q, st);
          sp_setseg(s, &q, 0); }
    }
after_start:
    sp_eval(s, 1, s->t1, p, d);
    if (sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) < 1e-12) { /* FixBadEndDerivative */
        double t = s->t1, dv = s->dt;
        while (sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) <= 1e-05) {
            if (t < s->t1 - dv * 0.1) goto after_end;
            t = t - dv * 1e-05;
            sp_eval(s, 1, t, p, d);
        }
        double e[3] = { d[0], d[1], d[2] }, spd = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        unit3(e);
        { int last = s->ns - 1; double u1 = s->u[s->ns], u0 = s->u[last];
          Cu q; sp_getseg(s, last, &q);
          double a0[3], ad[3], b0[3], bd[3];
          cu_eval1(&q, 1.0, b0, bd);
          cu_eval1(&q, 0.0, a0, ad);
          double a1[3] = { ad[0], ad[1], ad[2] }; unit3(a1);
          double aa = 0, bb = 0;
          best_fit_derivs(a0, a1, b0, e, &aa, &bb);
          double v = (spd * (u1 - u0) + bb) * 0.5;
          double st[3] = { e[0] * v, e[1] * v, e[2] * v };
          cu_set_end_deriv(&q, st);
          sp_setseg(s, &q, last); }
    }
after_end:
    return s;
}
/* MaCubicSpline::HasSharpNodes(double) (vtable +0xe8): the driver's and the composite's test, nodes within [t0, t1] */
static int sp_has_sharp(const Sp *s, double angle) {
    if (s->kind == 5) return nb_has_sharp(s, angle);
    if (s->ns == 1) return 0;
    double c = cos(angle * DEG2RAD);
    for (int i = 1; i < s->ns; i++) {
        double u = s->u[i];
        if (s->t0 <= u && u <= s->t1) {
            Cu a, b; double pa[3], da[3], pb[3], db[3];
            sp_getseg(s, i - 1, &a); sp_getseg(s, i, &b);
            cu_eval1(&a, 1.0, pa, da); cu_eval1(&b, 0.0, pb, db);
            unit3(da); unit3(db);
            if (db[0] * da[0] + db[1] * da[1] + db[2] * da[2] < c) return 1;
        }
    }
    return 0;
}
/* MaCompositeCurve::Refit (with PreProcessDiscontinuousSplines of MaCompositeCurve's constructor, GenCurveFromCurves, breaking the spline at slope discontinuities above 0.1 degree):
   0 = ok, 1 failure, 2 = BlendAndFairNodes reported overlapping blend spans from JoinSplinesSmoothly (the driver then writes back the clamped fair distance), 3 = one piece, not smooth: the curve is dropped by Refit and CArray access throws. */
static int composite_refit(const Sp *s0, double chord, double angle, double fair, Sp **out) {
    *out = NULL;
    double *L = NULL;
    int nb = 0;
    Sp **pcs;
    int np;
    if (!sp_is_smooth(s0, 0.1)) nb = sp_slope_breaks(s0, 0.1, &L);   /* HasDiscontinuousSplines (!IsSmooth(0.1), dumped) + BreakSplineAtSlopeDiscontinuities(0.1) */
    np = nb > 0 ? nb + 1 : 1;
    pcs = calloc((size_t)np, sizeof(Sp *));
    if (nb > 0) { for (int i = 0; i <= nb; i++) {
        Sp *q = sp_copy(s0);
        sp_truncate_ab(q, i == 0 ? q->t0 : L[i - 1], i == nb ? q->t1 : L[i]);
        pcs[i] = q;
    } } else pcs[0] = sp_copy(s0);
    free(L);
    Sp **res = calloc((size_t)np, sizeof(Sp *));
    int nres = 0, status = 0;
    for (int i = 0; i < np; i++) {
        Sp *q = pcs[i], *r = NULL;
        if (q->kind == 5) {                                   /* MaCompositeCurve::Refit, CurveType 5 */
            if (nb_is_polyline(q)) { r = fair_piecewise(q, angle); if (!r) { status = 1; goto fail; } }
            else if (sp_is_smooth(q, 0.1)) { int e = 0; r = fit_cubic_approx(q, chord, &e, !(q->ord - 1 == 3 && q->ncp == 4 && !q->rat)); if (e || !r) { status = 1; goto fail; } }   /* MaCompositeCurve::Refit, the call at 0x1803ea3bf (lost by the decompile): param_3 = !(degree 3, 4 control points, non rational) */
            else { int rr = nb_make_smooth(q, chord, angle, fair, &r); if (rr == 1) { status = 1; goto fail; } }
        } else if (sp_is_smooth(q, 0.1)) {
            if (q->ns != 1) { int e = 0; r = fit_cubic_approx(q, chord, &e, 0); if (e) { status = 1; goto fail; } }
            else r = sp_copy(q);
        } else {
            int rr = make_smooth_fit(q, chord, angle, fair, &r);
            if (rr == 1) { status = 1; goto fail; }
            if (rr == 2) { status = 3; sp_free(r); goto fail; }
        }
        res[nres++] = r;
    }
    if (nres == 1) {
        sp_snap_closed(res[0]);
        int c = 0, pe = 0;
        sp_closed(res[0], -1.0, &c, &pe);
        if (pe) sp_end_derivs_periodic(res[0]);
        *out = res[0]; res[0] = NULL;
    } else if (nres > 1) {
        Sp *r0 = res[0]; res[0] = NULL;
        sp_join(r0, res + 1, nres - 1);                     /* JoinSplinesSmoothly: JoinSplines + MakeSmoothFitCubicSpline + CopySplineToThis */
        Sp *f = NULL;
        status = make_smooth_fit(r0, chord, angle, fair, &f);
        if (f) { sp_copy_to(r0, f); sp_free(f); }
        for (int i = 1; i < nres; i++) { sp_free(res[i]); res[i] = NULL; }
        *out = r0;
    }
fail:
    for (int i = 0; i < np; i++) { sp_free(pcs[i]); sp_free(res[i]); }
    free(pcs); free(res);
    return status;
}
/* MaCubicSpline::ConvertToNURB -> ConvertToC1NURB / ConvertToG0G1NURB + MaNURBSpline's InitSplineData + MakenbsSplineType (flags: closed bit 1, periodic bit 3, bit 2 = non rational); record of class 0x80 via mcsf_nurbs_rec */
static Sp *sp_cubic_to_nurb(Sp *s) {
    sp_truncate_if_trimmed(s);
    double *P, *K; int ncp, nk;
    if (sp_is_smooth0(s)) {                 /* MaCubicSpline::ConvertToC1NURB */
        ncp = s->ns * 2 + 2; nk = ncp + 4;
        P = malloc((size_t)ncp * 24); K = malloc((size_t)nk * 8);
        int w = 0;
        for (int i = 0; i < s->ns; i++) {
            Cu q; sp_getseg(s, i, &q);
            cu_geo(&q);                     /* GetBezierVertices: P0 P1 D0 D1 per axis */
            double B[4][3];
            for (int k = 0; k < 3; k++) { double p0 = q.c[k * 4], p1 = q.c[k * 4 + 1], d0 = q.c[k * 4 + 2], d1 = q.c[k * 4 + 3];
                B[0][k] = p0; B[3][k] = p1; B[1][k] = d0 * 0.3333333333333333 + p0; B[2][k] = p1 - d1 * 0.3333333333333333; }
            int a, b;
            if (s->ns == 1) { a = 0; b = 4; }
            else if (i == 0) { a = 0; b = 3; }
            else if (i == s->ns - 1) { a = 1; b = 4; }
            else { a = 1; b = 3; }
            for (int j = a; j < b; j++) { memcpy(P + 3 * w, B[j], 24); w++; }
        }
        int ki = 0;
        K[ki++] = s->u[0]; K[ki++] = s->u[0];
        for (int i = 0; i < s->ns + 1; i++) { K[ki++] = s->u[i]; K[ki++] = s->u[i]; }
        K[ki++] = s->u[s->ns]; K[ki++] = s->u[s->ns];
    } else {                                /* MaCubicSpline::ConvertToG0G1NURB */
        double c = cos(0.001745329251994329);
        double *sh = malloc(((size_t)s->ns + 1) * 8);
        int ns_sh = 0;
        for (int i = 0; i < s->ns - 1; i++) {
            Cu a, b; double pa[3], da[3], pb[3], db[3];
            sp_getseg(s, i, &a); sp_getseg(s, i + 1, &b);
            cu_eval1(&a, 1.0, pa, da); cu_eval1(&b, 0.0, pb, db);
            unit3(da); unit3(db);
            if (db[0] * da[0] + db[1] * da[1] + db[2] * da[2] < c) sh[ns_sh++] = s->u[i + 1];
        }
        ncp = ns_sh + (s->ns + 1) * 2; nk = ncp + 4;
        P = malloc((size_t)ncp * 24); K = malloc((size_t)nk * 8);
        int w = 0;
        for (int i = 0; i < s->ns; i++) {
            Cu q; sp_getseg(s, i, &q);
            cu_geo(&q);
            double B[4][3];
            for (int k = 0; k < 3; k++) { double p0 = q.c[k * 4], p1 = q.c[k * 4 + 1], d0 = q.c[k * 4 + 2], d1 = q.c[k * 4 + 3];
                B[0][k] = p0; B[3][k] = p1; B[1][k] = d0 * 0.3333333333333333 + p0; B[2][k] = p1 - d1 * 0.3333333333333333; }
            int a, b, sharp = 0;
            double uv = s->u[i + 1];
            for (int j = 0; j < ns_sh; j++) if (uv == sh[j]) { sharp = 1; break; }
            if (s->ns == 1) { a = 0; b = 4; }
            else if (i == 0) { a = 0; b = sharp ? 4 : 3; }
            else { a = 1; if (i == s->ns - 1) b = 4; else b = sharp ? 4 : 3; }
            for (int j = a; j < b; j++) { memcpy(P + 3 * w, B[j], 24); w++; }
        }
        int ki = 0;
        K[ki++] = s->u[0]; K[ki++] = s->u[0]; K[ki++] = s->u[0]; K[ki++] = s->u[0];
        for (int i = 1; i < s->ns; i++) {
            int sharp = 0;
            for (int j = 0; j < ns_sh; j++) if (s->u[i] == sh[j]) { sharp = 1; break; }
            K[ki++] = s->u[i]; K[ki++] = s->u[i]; if (sharp) K[ki++] = s->u[i];
        }
        K[ki++] = s->u[s->ns]; K[ki++] = s->u[s->ns]; K[ki++] = s->u[s->ns]; K[ki++] = s->u[s->ns];
        free(sh);
    }
    /* MaNURBSpline::InitSplineData: weights 1, knots normalised to k[0] / k[last], the trim mapped */
    Sp *nb = nb_new(ncp, 4, 0);
    for (int i = 0; i < ncp; i++) { nb->V[i * 4] = 1.0 * P[i * 3]; nb->V[i * 4 + 1] = 1.0 * P[i * 3 + 1]; nb->V[i * 4 + 2] = 1.0 * P[i * 3 + 2]; nb->V[i * 4 + 3] = 1.0; }
    memcpy(nb->K, K, (size_t)nk * 8);
    double k0 = nb->K[0], inv = 1.0 / (nb->K[nk - 1] - k0);
    for (int i = 0; i < nk; i++) nb->K[i] = (nb->K[i] - k0) * inv;
    set_range(nb, (s->t0 - k0) * inv, (s->t1 - k0) * inv);
    free(P); free(K);
    return nb;
}
/* MaNURBSpline::MakeMCEnt (Truncate when trimmed, IsDegenerate(tol) refuses) / MakenbsSplineType: closed bit 1, periodic bit 3, bit 2 non rational; points x y z or w x y z. NULL when degenerate */
static uint8_t *nb_record(Sp *nb, double tol, uint32_t *rn) {
    sp_truncate_if_trimmed(nb);
    { double p[3], d[3]; sp_eval(nb, 1, sp_tmid(nb), p, d); if (sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) <= tol) return NULL; }
    int closed = 0, periodic = 0;
    nb->known = 0;
    sp_closed(nb, -1.0, &closed, &periodic);
    int vpp = nb->rat ? 4 : 3;
    double *C = malloc((size_t)nb->ncp * vpp * 8);
    for (int i = 0; i < nb->ncp; i++) {
        if (nb->rat) { C[i * 4] = nb->V[i * 4 + 3]; C[i * 4 + 1] = nb->V[i * 4]; C[i * 4 + 2] = nb->V[i * 4 + 1]; C[i * 4 + 3] = nb->V[i * 4 + 2]; }
        else { C[i * 3] = nb->V[i * 4]; C[i * 3 + 1] = nb->V[i * 4 + 1]; C[i * 3 + 2] = nb->V[i * 4 + 2]; }
    }
    McsfNb o = { nb->ncp, nb->nk, nb->ord - 1, 3, nb->rat, closed, nb->K, C };
    uint8_t *r = mcsf_nurbs_rec(&o, tol, rn);
    if (r) r[0xd2] = (uint8_t)((closed ? 2 : 0) | (nb->rat ? 0 : 4) | (periodic ? 8 : 0));
    free(C);
    return r;
}
/* MaCubicSpline::ConvertToNURB + MaNURBSpline::MakeMCEnt */
static uint8_t *sp_to_nurb_rec(Sp *s, double tol, uint32_t *rn) {
    Sp *nb = sp_cubic_to_nurb(s);
    uint8_t *r = nb_record(nb, tol, rn);
    sp_free(nb);
    return r;
}
/* MaCubicSpline::MakeSplineType (the class 8 record): coefficients to [power ascending][xyz] (put_spline_in_db via mcsf_spline_rec) */
static uint8_t *sp_to_spline_rec(Sp *s, double tol, uint32_t *rn) {
    sp_truncate_if_trimmed(s);
    double *c = malloc((size_t)s->ns * 96);
    for (int i = 0; i < s->ns; i++) {
        const double *a = s->c + (size_t)i * 12; double *o = c + (size_t)i * 12;
        o[0] = a[3]; o[1] = a[7]; o[2] = a[11]; o[3] = a[2]; o[4] = a[6]; o[5] = a[10]; o[6] = a[1]; o[7] = a[5]; o[8] = a[9]; o[9] = a[0]; o[10] = a[4]; o[11] = a[8];
    }
    double p0[3], p1[3], d[3];
    sp_eval(s, 0, s->t0, p0, d); sp_eval(s, 0, s->t1, p1, d);
    int closed = 0, per = 0;
    sp_closed(s, -1.0, &closed, &per);
    uint8_t *r = mcsf_spline_rec(c, s->ns, p0, p1, closed, tol, rn);
    free(c);
    return r;
}
static void push_rec(McrResult *o, uint32_t cls, uint8_t *r, uint32_t n) {
    o->rec = realloc(o->rec, (size_t)(o->nrec + 1) * sizeof(McrRec));
    o->rec[o->nrec].cls = cls; o->rec[o->nrec].rec = r; o->rec[o->nrec].n = n; o->nrec++;
}
void mcr_free(McrResult *r) { if (r) { for (int i = 0; i < r->nrec; i++) free(r->rec[i].rec); free(r->rec); r->rec = NULL; r->nrec = 0; } }
/* the driver (mccadutil.dll FUN_1801347c0) on one spline */
int mcr_refit(const uint8_t *rec, uint32_t n, const McrParams *p, McrResult *out) {
    memset(out, 0, sizeof *out);
    uint32_t cls;
    memcpy(&cls, rec, 4);
    if (cls != 8 && cls != 0x80) return -1;
    if (cls == 8 && rec[0xb4] != 4) return -1;     /* MaCurve::LoadCurve: an order != 4 record goes through MaGenericCurve / convert_entity_to_nurbs: not ported */
    int err = 0;
    g_metric = p->metric;
    Sp *s = cls == 8 ? sp_from_record(rec, n, &err) : nb_from_record(rec, n, p->tol, !p->no_load_fit, p->step_chord_dev, &err);
    if (!s) return err;
    double fair = p->fair_dist;
    if (p->max_fair < fair) fair = p->max_fair;
    if (!(p->break_at_sharp) && !(p->tol <= p->fair_dist)) { sp_free(s); return 1; }
    int sharp = sp_has_sharp(s, p->angle_deg);
    if (sharp && p->break_at_sharp) {
        double *L = NULL;
        int nl = sp_slope_breaks(s, p->angle_deg, &L);
        if (nl > 0) {
            out->broke = 1;
            for (int i = 0; i <= nl; i++) {           /* MaCurve::BreakSplineAtSlopeDiscontinuities + MakeSplineOrNURBCurve (cfg_createNurbs) */
                Sp *q = sp_copy(s);
                sp_truncate_ab(q, i == 0 ? s->t0 : L[i - 1], i == nl ? s->t1 : L[i]);
                uint32_t rn = 0; uint8_t *r = NULL; uint32_t rcls = 0;
                if (q->kind == 5) {
                    if (p->nurbs) { r = nb_record(q, p->tol, &rn); rcls = 0x80; }
                    else {
                        Sp *c = nb_to_cubic(q); int e = 0;
                        if (!c) c = fit_cubic_approx(q, p->step_chord_dev, &e, 0);
                        if (c) { r = sp_to_spline_rec(c, p->tol, &rn); rcls = 8; sp_free(c); }
                    }
                } else if (p->nurbs) { r = sp_to_nurb_rec(q, p->tol, &rn); rcls = 0x80; }
                else { r = sp_to_spline_rec(q, p->tol, &rn); rcls = 8; }
                if (r) push_rec(out, rcls, r, rn);
                sp_free(q);
            }
        }
        free(L); sp_free(s);
        return 0;
    }
    Sp *q = NULL;
    int r = composite_refit(s, p->chord_dev, p->angle_deg, fair, &q);
    if (r == 1 || (!q && r != 3)) { sp_free(q); sp_free(s); return 1; }
    if (r == 3) { sp_free(q); sp_free(s); return 1; }  /* one curve, not smooth: BlendAndFairNodes' 2 keeps the curve off the array, CArray access throws (Ordinal_2300) in MaCompositeCurve::Refit */
    if (r == 2) out->clamped = 1;                       /* JoinSplinesSmoothly's 2: the driver's *param_3 = fair, the curve is still stored */
    /* FUN_1801347c0: ConvertToNURB (vt+0x150), the refused "more nodes" check (type 5: control points, type 6: segments), MakeMCEnt (vt+0x130) */
    Sp *nb = sp_cubic_to_nurb(q);
    int more = 0;
    if (!p->allow_more_nodes && !sharp) {
        if (s->kind == 5) more = s->ncp < nb->ncp;
        else more = s->ns + 1 < q->ns + 1;
    }
    if (more) { sp_free(nb); sp_free(q); sp_free(s); return 1; }
    uint32_t rn = 0;
    uint8_t *rr = nb_record(nb, p->tol, &rn);
    sp_free(nb); sp_free(q); sp_free(s);
    if (!rr) return 1;
    push_rec(out, 0x80, rr, rn);
    return 0;
}

/* ---- Helix and Spiral (mccore.dll, decomp/mccad_shapes_core.c): MaHelix::MaHelix @180a79540 :548, EvaluateCrv @180a79d30 :842, ConvertToNURB @180a79b90 :745, FitCubicApproxCurve @180a7a140 :934,
   MakeMCSplineEntities @180a7a7c0 :1271; MaSpiral::MaSpiral @180a7abf0 :1561, EvaluateCrv @180a7aec0 :1665; the per-revolution loop of mccad FUN_180089040 (decomp/mccad_cmds/create_spiral.c:540-660).
   Oracle: tools/tests/helix_oracle.sh (the real mccore.dll under tools/peloader, ConvertToNURB / TrimMultiple). */
typedef struct Hx { double C[3], X[3], Y[3], Z[3], R, pitch, revs, tap, v0, v1, h0, h1; int spiral; } Hx;
#define HX_2PI 6.283185307179586
/* MaHelix::EvaluateCrv / MaSpiral::EvaluateCrv: the position and first derivative */
static void hx_eval(const Hx *h, int nd, double t, double p[3], double d[3]) {
    double a = h->revs * HX_2PI, th = a * t, c = cos(th), sn = sin(th);
    if (h->spiral) {
        double dv = h->v1 - h->v0, dh;
        if (h->revs <= 1.0) dh = h->h1 - h->h0;
        else { double q = (h->revs - 1.0) * HX_2PI; dv /= q; dh = (h->h1 - h->h0) / q; }
        double vv = dv * th + h->v0, hh = dh * th + h->h0, RR = hh * th + h->R;
        for (int k = 0; k < 3; k++) p[k] = RR * h->X[k] * c + h->C[k] + RR * h->Y[k] * sn + vv * h->Z[k] * th;
        if (nd > 0) { double v2 = dv * th + vv, h2 = dh * th + hh, xc = RR * c, xs = RR * sn, w = h2 * c - xs, u = h2 * sn + xc;
            for (int k = 0; k < 3; k++) d[k] = (u * h->Y[k] + w * h->X[k] + v2 * h->Z[k]) * a; }
        return;
    }
    double RR = h->R, tn = 0.0;
    if (h->tap != 0.0) { tn = sin(h->tap) / cos(h->tap); RR = th * h->pitch * tn + RR; }
    for (int k = 0; k < 3; k++) p[k] = RR * h->X[k] * c + h->C[k] + RR * h->Y[k] * sn + h->pitch * h->Z[k] * th;
    if (nd > 0) { double d1 = h->tap != 0.0 ? tn * h->pitch : 0.0, q = d1 * sn, w = d1 * c, c2 = c * RR, s2 = sn * RR, a1 = c2 + q, a2 = w - s2;
        for (int k = 0; k < 3; k++) d[k] = (a2 * h->X[k] + a1 * h->Y[k] + h->pitch * h->Z[k]) * a; }
}
/* MaHelix::ConvertToNURB on the piece [ta, tb]: 1 + 8 revs dt seed parameters (at least 2), FitCubicApproxCurve(surf_step_chord_dev) = BinaryCurveFitParams, points and unit tangents, MaCubicSpline(n, P, T) = BuildPtVtSpline, MaCubicSpline::ConvertToNURB */
static Sp *hx_nurb(const Hx *h, double ta, double tb, double chord) {
    double dt = tb - ta; int n = 1 - (int)(h->revs * -8.0 * dt); if (n < 2) n = 2;
    double *seed = malloc((size_t)n * 8), v = ta; seed[0] = v;
    for (int i = 1; i < n; i++) { v += dt / (double)(n - 1); seed[i] = v; }
    seed[n - 1] = tb;
    Sp q; memset(&q, 0, sizeof q); q.kind = 7; q.hx = h; q.t0 = ta; q.t1 = tb; q.dt = dt;
    double *t = malloc(5000 * 8), (*P)[3] = malloc(5000 * 24), (*T)[3] = malloc(5000 * 24);
    int np = 0, err = binary_curve_fit_params_seed(&q, chord, 5000, t, &np, seed, n > 5000 ? 5000 : n);
    Sp *nb = NULL;
    if (!err) {
        for (int i = 0; i < np; i++) { sp_eval(&q, 1, t[i], P[i], T[i]); unit3(T[i]); }
        Sp *cu = sp_build_ptvt(np, P, T);
        nb = sp_cubic_to_nurb(cu);
        sp_free(cu);
    }
    free(seed); free(t); free(P); free(T);
    return nb;
}
static int hx_push_piece(McrResult *out, const Hx *h, double ta, double tb, double chord, double tol) {
    Sp *nb = hx_nurb(h, ta, tb, chord); if (!nb) return 1;
    uint32_t rn = 0; uint8_t *r = nb_record(nb, tol, &rn); sp_free(nb);
    if (!r) return 0;                              /* MaNURBSpline::MakeMCEnt: a degenerate piece stores nothing */
    push_rec(out, 0x80, r, rn);
    return 0;
}
/* MaHelix::MakeMCSplineEntities: revs <= 50 one curve; else ceil(revs / 50) pieces of 50 / revs (MaCurve::TrimMultiple, the last clamped to t1), each ConvertToNURB + MakeMCEnt */
static int hx_make(const Hx *h, double chord, double tol, McrResult *out) {
    if (h->revs <= 50.0) return hx_push_piece(out, h, 0.0, 1.0, chord, tol);
    int n = (int)ceil(h->revs / 50.0); double dt = 50.0 / h->revs, a = 0.0;
    for (int i = 0; i < n; i++) { double b = a + dt; if (1.0 < b) b = 1.0; if (hx_push_piece(out, h, a, b, chord, tol)) return 1; a = b; }
    return 0;
}
int mcr_helix(const McrHelix *in, double tol, double chord, int metric, McrResult *out) {
    g_metric = metric; memset(out, 0, sizeof *out);
    Hx h; memset(&h, 0, sizeof h); memcpy(h.C, in->c, 24); memcpy(h.X, in->x, 24); memcpy(h.Y, in->y, 24); memcpy(h.Z, in->z, 24);
    h.R = fabs(in->r); h.spiral = in->spiral;
    if (in->dir) for (int k = 0; k < 3; k++) h.Y[k] = h.Y[k] * -1.0;   /* MaHelix::MaHelix: param_10 negates Y */
    if (!in->spiral) {
        h.pitch = in->pitch / HX_2PI; h.revs = fabs(in->revs); h.tap = in->taper_deg * 0.01745329251994329;
        double ang = in->init_deg * 0.01745329251994329;
        if (ang != 0.0) {      /* HingeMatrix3X3(Z, angle) applied to X and Y */
            double l = sqrt(h.Z[0] * h.Z[0] + h.Z[1] * h.Z[1] + h.Z[2] * h.Z[2]), z0 = 0, z1 = 0, z2 = 0;
            if (l != 0.0) { z0 = h.Z[0] / l; z1 = h.Z[1] / l; z2 = h.Z[2] / l; }
            double c = cos(ang), s = sin(ang), e = 1.0 - c, a = z2 * z0 * e, b = z1 * z0 * e, g = z2 * z1 * e, m[9];
            m[0] = (1.0 - z0 * z0) * c + z0 * z0; m[1] = b - z2 * s; m[3] = z2 * s + b; m[2] = z1 * s + a; m[6] = a - z1 * s; m[8] = (1.0 - z2 * z2) * c + z2 * z2; m[4] = (1.0 - z1 * z1) * c + z1 * z1; m[7] = z0 * s + g; m[5] = g - z0 * s;
            double X[3], Y[3];
            X[0] = m[1] * h.X[1] + m[0] * h.X[0] + m[2] * h.X[2]; X[1] = m[3] * h.X[0] + m[4] * h.X[1] + m[5] * h.X[2]; X[2] = m[7] * h.X[1] + m[6] * h.X[0] + m[8] * h.X[2];
            Y[0] = m[1] * h.Y[1] + m[0] * h.Y[0] + m[2] * h.Y[2]; Y[1] = m[3] * h.Y[0] + m[4] * h.Y[1] + m[5] * h.Y[2]; Y[2] = m[6] * h.Y[0] + m[7] * h.Y[1] + m[8] * h.Y[2];
            memcpy(h.X, X, 24); memcpy(h.Y, Y, 24);
        }
        return hx_make(&h, chord, tol, out);
    }
    /* the Spiral (mccad FUN_180089040): the frame as above with no initial angle; R is |R| */
    double revs = fabs((double)in->irevs), v0 = in->v0, v1 = in->v1, h0 = in->h0, h1 = in->h1;
    if (h0 == h1 && v0 == v1) {
        h.revs = revs; h.v0 = v0 / HX_2PI; h.v1 = v1 / HX_2PI; h.h0 = h0 / HX_2PI; h.h1 = h1 / HX_2PI;
        return hx_make(&h, chord, tol, out);
    }
    double dh = 0.0, dv = 0.0, R = fabs(in->r), org[3]; memcpy(org, h.C, 24);
    if (revs > 1.0) { dh = (h1 - h0) / (revs - 1.0); dv = (v1 - v0) / (revs - 1.0); }
    h.revs = 1.0;
    for (int i = 0; (double)i < revs; i++) {       /* one NURBS per revolution: constant pitches v, h on it, then origin += v Z, R += h, v += dv, h += dh */
        memcpy(h.C, org, 24); h.R = R; h.v0 = h.v1 = v0 / HX_2PI; h.h0 = h.h1 = h0 / HX_2PI;
        if (hx_push_piece(out, &h, 0.0, 1.0, chord, tol)) return 1;
        for (int k = 0; k < 3; k++) org[k] = v0 * h.Z[k] + org[k];
        R += h0; h0 += dh; v0 += dv;
    }
    return 0;
}

/* MaCubicSpline(const MaPolyLine *) 0x1803f0490 = BuildPolyPtsSpline 0x1803f1e30: SetLinearTangents 0x1803fbd00 (an interior point whose
   incoming chord (from the last corner) and outgoing chord are within 0.999999 (dot) gets the incoming direction, flag 1; the last interior
   one also sets the end point), FairSplineTangents, BuildPtVtSpline (no RemoveDuplicatePoints). For viewer/mcsrftrim.c's MaParameterCurve
   (surface, polyline, tol, tol) 0x180464620. *C: 12 algebraic coefficients per segment (MaCubicSpline +0x130), *U: the u list (+0x138). */
int mcr_poly_pts_spline(int n, const double (*Pin)[3], double **C, double **U) {
    double (*P)[3] = malloc((size_t)n * 24), (*T)[3] = calloc((size_t)n, 24), (*R)[3] = malloc((size_t)n * 24); int *f = calloc((size_t)n, sizeof(int));
    memcpy(P, Pin, (size_t)n * 24);
    if (2 < n) {
        double a[3] = { P[0][0], P[0][1], P[0][2] };
        for (int i = 1; i < n - 1; i++) {
            double e[3] = { P[i][0] - a[0], P[i][1] - a[1], P[i][2] - a[2] }, g[3] = { P[i + 1][0] - P[i][0], P[i + 1][1] - P[i][1], P[i + 1][2] - P[i][2] };
            unit3(e); unit3(g);
            if (0.999999 <= g[1] * e[1] + g[0] * e[0] + g[2] * e[2]) {
                memcpy(T[i], e, 24); f[i] = 1;
                if (i == n - 2) { memcpy(T[i + 1], e, 24); f[i + 1] = 1; }
            } else memcpy(a, P[i], 24);
        }
    }
    fair_spline_tangents(P, T, R, f, n);
    Sp *sp = sp_build_ptvt(n, P, R);
    int ns = sp->ns; *C = malloc(sizeof(double) * 12 * (ns > 0 ? ns : 1)); *U = malloc(sizeof(double) * (ns + 1));
    memcpy(*C, sp->c, sizeof(double) * 12 * ns); memcpy(*U, sp->u, sizeof(double) * (ns + 1));
    sp_free(sp); free(P); free(T); free(R); free(f);
    return ns;
}

/* MaCubicSpline::Break / MaNURBSpline::Break on plain arrays, for viewer/mcsrftrim.c's MaParameterCurve::Break 0x180464a30 (the uv curve broken,
   this keeping the first part (keep_first) or the second; the parameters renormalised to 0 .. 1). 0 broke (the arrays replaced, malloc'd), 1 not. */
int mcr_cubic_break(int *ns, double **C, double **U, double t, int keep_first) {
    Sp *sp = sp_new(*ns); memcpy(sp->c, *C, sizeof(double) * 12 * *ns); memcpy(sp->u, *U, sizeof(double) * (*ns + 1));
    sp->t0 = sp->u[0]; sp->t1 = sp->u[*ns]; sp->dt = sp->t1 - sp->t0;
    Sp *o = NULL; int r = sp_break(sp, t, keep_first, &o);
    if (r == 0) { *ns = sp->ns; *C = malloc(sizeof(double) * 12 * sp->ns); *U = malloc(sizeof(double) * (sp->ns + 1)); memcpy(*C, sp->c, sizeof(double) * 12 * sp->ns); memcpy(*U, sp->u, sizeof(double) * (sp->ns + 1)); }
    sp_free(o); sp_free(sp); return r;
}
int mcr_nurbs_break(int *ncp, int ord, double **K, double **P4, double t, int keep_first) {
    Sp *sp = nb_new(*ncp, ord, 0);
    memcpy(sp->K, *K, sizeof(double) * (*ncp + ord));
    memcpy(sp->V, *P4, sizeof(double) * 4 * *ncp);
    sp->t0 = sp->K[0]; sp->t1 = sp->K[sp->nk - 1]; sp->dt = sp->t1 - sp->t0;
    Sp *o = NULL; int r = nb_break(sp, t, keep_first, &o);
    if (r == 0) {
        *ncp = sp->ncp; *K = malloc(sizeof(double) * sp->nk); memcpy(*K, sp->K, sizeof(double) * sp->nk);
        *P4 = malloc(sizeof(double) * 4 * sp->ncp); memcpy(*P4, sp->V, sizeof(double) * 4 * sp->ncp);   /* the Verts as the subdivision left them (x y z w, w of a non-rational curve not exactly 1) */
    }
    sp_free(o); sp_free(sp); return r;
}
