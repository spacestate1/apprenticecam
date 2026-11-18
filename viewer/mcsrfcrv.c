/* mcsrfcrv.c - curves extracted from surfaces (Wireframe > Curves), as mccore's MaSurface / MaCurve make them.
 * Decoded from mccore.dll (decomp/curves/: core1.c, core2.c, core3.c, ma_classes.c; formats/wireframe.md "Curves on surfaces").
 * Ported: the iso curve of a NURBS surface (MaNURBSurf::Flowline 0x18045b9c0), its cuts by a trimmed surface's loops
 * (MaFace::IsoCurveTrimPoints 0x1804162a0, MaSurface::IsoCurve 0x1804876f0) and the simplification to a line or an arc
 * (MaCurve::Simplify 0x180408f80). The other surface classes (MaBicubicSurf::Flowline, the MaGenericSurf / MaOffsetSurf path
 * MaSurface::Flowline 0x1804845f0: BinaryCurveFitParams + a fitted cubic spline) are NOT PORTED: refused by the caller. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcsrfcrv.h"
#include "mcsurf.h"
#include "mcsurf_build.h"
#include "mcspline_fit.h"
#include "mcsrfint.h"

static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int64_t rd64(const uint8_t *p) { int64_t v; memcpy(&v, p, 8); return v; }
static void wdd(uint8_t *p, double v) { memcpy(p, &v, 8); }

/* a NURBS curve being built: knots, control points as (w, wx, wy, wz) (the file's order, formats/mcam.md "NURBS surface") */
typedef struct { int deg, ncp, nk, rat; double *k, *p, t1, t2; } NC;
static void nc_free(NC *c) { free(c->k); free(c->p); memset(c, 0, sizeof *c); }

/* de Boor in the homogeneous space on n control points of 4 values */
static void deboor(int deg, const double *K, int nc, const double *P, double t, double o[4]) {
    if (t < K[deg]) t = K[deg]; if (t > K[nc]) t = K[nc];
    int s = deg; while (s < nc - 1 && !(t < K[s + 1])) s++;
    double d[26][4];
    for (int j = 0; j <= deg; j++) memcpy(d[j], P + 4 * (s - deg + j), 32);
    for (int r = 1; r <= deg; r++) for (int j = deg; j >= r; j--) {
        int i = s - deg + j; double a0 = K[i], a1 = K[i + deg - r + 1], a = a1 > a0 ? (t - a0) / (a1 - a0) : 0;
        for (int k = 0; k < 4; k++) d[j][k] = (1 - a) * d[j - 1][k] + a * d[j][k];
    }
    memcpy(o, d[deg], 32);
}
static void nc_eval(const NC *c, double t, double p[3]) {
    double o[4]; deboor(c->deg, c->k, c->ncp, c->p, t, o);
    double w = c->rat ? o[0] : 1.0; if (!(fabs(w) > 1e-300)) w = 1.0;
    for (int k = 0; k < 3; k++) p[k] = o[k + 1] / w;
}
/* the first derivative with respect to t in model space (rational: quotient rule on the homogeneous curve). Evaluate(1, t) of
   MaNURBSpline: a position + its tangent (MaCurve::Evaluate 0x180400b00) - the formula here is the textbook derivative of the
   same curve */
static void nc_deriv(const NC *c, double t, double p[3], double d[3]) {
    double t0 = c->k[c->deg], t1 = c->k[c->ncp]; if (t < t0) t = t0; if (t > t1) t = t1;
    int s = c->deg; while (s < c->ncp - 1 && !(t < c->k[s + 1])) s++;
    double a[4], D[4] = { 0, 0, 0, 0 };
    deboor(c->deg, c->k, c->ncp, c->p, t, a);
    if (c->deg > 0) {
        /* the derivative of the homogeneous curve: the degree - 1 curve on the control differences */
        double q[26][4];
        for (int j = 0; j < c->deg; j++) {
            int i = s - c->deg + j; double dk = c->k[i + c->deg + 1] - c->k[i + 1];
            for (int m = 0; m < 4; m++) q[j][m] = dk > 0 ? c->deg * (c->p[4 * (i + 1) + m] - c->p[4 * i + m]) / dk : 0;
        }
        for (int r = 1; r < c->deg; r++) for (int j = c->deg - 1; j >= r; j--) {
            int i = s - c->deg + 1 + j; double a0 = c->k[i], a1 = c->k[i + c->deg - r], u = a1 > a0 ? (t - a0) / (a1 - a0) : 0;
            for (int m = 0; m < 4; m++) q[j][m] = (1 - u) * q[j - 1][m] + u * q[j][m];
        }
        memcpy(D, q[c->deg - 1], 32);
    }
    double w = c->rat ? a[0] : 1.0, dw = c->rat ? D[0] : 0.0;
    for (int k = 0; k < 3; k++) { p[k] = a[k + 1] / w; d[k] = c->rat ? (D[k + 1] - dw * p[k]) / w : D[k + 1]; }
}

/* MaNURBSurf::Flowline (0x18045b9c0): param_1 = 1 the curve at constant u along v (the v knots, this+0xa0 degree), 2 at constant v
   along u. Each of its control points is the de Boor point of a row (u fastest in the record) at the parameter, in homogeneous
   space (MaNURBSpline::SetVerts + SubdivisionPoint); range the surface's (SetMinMaxParams this+0x30 / 0x38, or 0x20 / 0x28). */
static int surf_iso(const Surf *s, int dir, double par, NC *o) {
    const uint8_t *r = s->rec;
    int nku = rd32(r + 0x100), nkv = rd32(r + 0x104), ncu = rd32(r + 0x108), ncv = rd32(r + 0x10c), vpp = r[0x110], du = r[0x111], dv = r[0x112];
    int rat = !(r[0x113] & 1);
    const double *KU = (const double *)(r + 0x118), *KV = KU + nku;
    const uint8_t *CP = r + 0x118 + 8 * ((size_t)nku + nkv);
    double dom[4]; mcs_surf_domain(s, dom);
    int alng = dir == 1 ? ncv : ncu, cross = dir == 1 ? ncu : ncv, dalng = dir == 1 ? dv : du, dcross = dir == 1 ? du : dv, nkalng = dir == 1 ? nkv : nku;
    const double *Ka = dir == 1 ? KV : KU, *Kc = dir == 1 ? KU : KV;
    double *row = malloc(sizeof(double) * 4 * cross);
    o->deg = dalng; o->ncp = alng; o->nk = nkalng; o->rat = rat;
    o->k = malloc(sizeof(double) * nkalng); memcpy(o->k, Ka, sizeof(double) * nkalng);
    o->p = malloc(sizeof(double) * 4 * alng);
    for (int a = 0; a < alng; a++) {
        for (int c = 0; c < cross; c++) {
            size_t idx = dir == 1 ? (size_t)a * ncu + c : (size_t)c * ncu + a;
            const uint8_t *q = CP + 8 * (size_t)vpp * idx;
            if (vpp == 4) for (int m = 0; m < 4; m++) row[4 * c + m] = rdd(q + 8 * m);
            else { row[4 * c] = 1; for (int m = 0; m < 3; m++) row[4 * c + 1 + m] = rdd(q + 8 * m); }
        }
        deboor(dcross, Kc, cross, row, par, o->p + 4 * a);
    }
    free(row);
    o->t1 = dir == 1 ? dom[2] : dom[0]; o->t2 = dir == 1 ? dom[3] : dom[1];
    return 0;
}

/* knot insertion (Boehm) of t into the curve, r times. MaNURBSpline::Break / MaCurve::Truncate (0x180409990) cut the curve at the
   trim parameters; the cut itself is the standard refinement [the DLL's Break was not read: the same curve piece either way] */
static void nc_insert(NC *c, double t, int times) {
    for (int z = 0; z < times; z++) {
        int s = c->deg; while (s < c->ncp - 1 && !(t < c->k[s + 1])) s++;
        double *K = malloc(sizeof(double) * (c->nk + 1)), *P = malloc(sizeof(double) * 4 * (c->ncp + 1));
        memcpy(K, c->k, sizeof(double) * (s + 1)); K[s + 1] = t; memcpy(K + s + 2, c->k + s + 1, sizeof(double) * (c->nk - s - 1));
        for (int i = 0; i <= s - c->deg; i++) memcpy(P + 4 * i, c->p + 4 * i, 32);
        for (int i = s - c->deg + 1; i <= s; i++) {
            double a = (t - c->k[i]) / (c->k[i + c->deg] - c->k[i]);
            for (int m = 0; m < 4; m++) P[4 * i + m] = (1 - a) * c->p[4 * (i - 1) + m] + a * c->p[4 * i + m];
        }
        for (int i = s + 1; i <= c->ncp; i++) memcpy(P + 4 * i, c->p + 4 * (i - 1), 32);
        free(c->k); free(c->p); c->k = K; c->p = P; c->nk++; c->ncp++;
    }
}
static int nc_mult(const NC *c, double t) { int m = 0; for (int i = 0; i < c->nk; i++) if (fabs(c->k[i] - t) < 1e-12) m++; return m; }
/* the piece [a, b] as a clamped curve (MaNURBSpline::MakeMCEnt: Truncate when IsTrimmed; 0x180451bd0) */
static void nc_sub(const NC *c, double a, double b, NC *o) {
    NC w = *c; w.k = malloc(sizeof(double) * c->nk); memcpy(w.k, c->k, sizeof(double) * c->nk); w.p = malloc(sizeof(double) * 4 * c->ncp); memcpy(w.p, c->p, sizeof(double) * 4 * c->ncp);
    int ma = nc_mult(&w, a); if (ma < w.deg) nc_insert(&w, a, w.deg - ma);
    int mb = nc_mult(&w, b); if (mb < w.deg) nc_insert(&w, b, w.deg - mb);
    int ka = -1, kb = -1, m2a = 0;
    for (int i = 0; i < w.nk; i++) { if (fabs(w.k[i] - a) < 1e-12) { if (ka < 0) ka = i; m2a++; } if (fabs(w.k[i] - b) < 1e-12 && kb < 0) kb = i; }
    /* a knot of multiplicity deg: the curve passes through the control point before it; deg + 1 (a clamped end): the first of the run */
    int ia = ka + m2a - w.deg - 1, ib = kb - 1, n = ib - ia + 1, ni = kb - (ka + m2a);
    o->deg = w.deg; o->rat = w.rat; o->ncp = n; o->nk = n + w.deg + 1;
    o->k = malloc(sizeof(double) * o->nk); o->p = malloc(sizeof(double) * 4 * n);
    for (int i = 0; i <= w.deg; i++) { o->k[i] = a; o->k[o->nk - 1 - i] = b; }
    memcpy(o->k + w.deg + 1, w.k + ka + m2a, sizeof(double) * ni);
    memcpy(o->p, w.p + 4 * ia, sizeof(double) * 4 * n);
    o->t1 = a; o->t2 = b;
    free(w.k); free(w.p);
}

/* the unique knots inside the range in order (MaNURBSpline::GetUniqueKnots) */
static int nc_unique(const NC *c, double *u) {
    int n = 0; for (int i = c->deg; i <= c->ncp; i++) if (n == 0 || fabs(c->k[i] - u[n - 1]) > 1e-12) u[n++] = c->k[i];
    return n;
}
static double d3(const double a[3], const double b[3]) { return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }
static double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const double a[3], const double b[3], double o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static void unit3(double v[3]) { double l = sqrt(dot3(v, v)); if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; } else v[0] = v[1] = v[2] = 0; }

/* MaNURBSpline::IsLinear (0x1804500a0): degree 1 with two points (order 2, ncp 2) is a line; else over every span of the unique knots:
   the derivatives at its two ends parallel (|D0 x D1| <= 1e-6, the raw derivatives) and both along the chord (cos >= 0.999999) */
static int nc_is_linear(const NC *c) {
    if (c->deg == 1 && c->ncp == 2) return 1;
    double *u = malloc(sizeof(double) * c->nk); int nu = nc_unique(c, u), ok = 1;
    for (int i = 0; i + 1 < nu && ok; i++) {
        double p0[3], p1[3], d0[3], d1[3], cx[3]; nc_deriv(c, u[i], p0, d0); nc_deriv(c, u[i + 1], p1, d1);
        cross3(d0, d1, cx); if (sqrt(dot3(cx, cx)) > 1e-6) { ok = 0; break; }
        double td0[3] = { d0[0], d0[1], d0[2] }, td1[3] = { d1[0], d1[1], d1[2] }, ch[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
        unit3(td0); unit3(td1); unit3(ch);
        if (dot3(td0, ch) < 0.999999 || dot3(td1, ch) < 0.999999) ok = 0;
    }
    free(u); return ok;
}
/* MaNURBSpline::IsPlanar(void) -> IsPlanar(1e-6) 0x180450530: not a line (vtable +0x30 false for a line curve); the first three non-collinear
   points of the control net (|cross| > 1e-6) give the plane; every control point within 1e-6 of it. */
static int nc_is_planar(const NC *c, double n[3], double *dd) {
    if (nc_is_linear(c)) return 0;
    double P0[3], P1[3], p[3], cx[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++) { P0[k] = c->p[4 * 0 + 1 + k] / (c->rat ? c->p[0] : 1); P1[k] = c->p[4 * 1 + 1 + k] / (c->rat ? c->p[4] : 1); }
    int found = 0;
    for (int i = 2; i < c->ncp && !found; i++) {
        for (int k = 0; k < 3; k++) p[k] = c->p[4 * i + 1 + k] / (c->rat ? c->p[4 * i] : 1);
        double a[3] = { P1[0] - P0[0], P1[1] - P0[1], P1[2] - P0[2] }, b[3] = { p[0] - P0[0], p[1] - P0[1], p[2] - P0[2] };
        double e[3]; cross3(b, a, e); if (sqrt(dot3(e, e)) > 1e-6) { memcpy(cx, e, sizeof e); found = 1; }
    }
    if (!found) return 0;
    unit3(cx);
    double d = dot3(cx, P0);   /* FUN_1803d1260(&plane, 3): the plane's constant through P0 [the callee not read; the first point lies on it] */
    for (int i = 0; i < c->ncp; i++) {
        for (int k = 0; k < 3; k++) p[k] = c->p[4 * i + 1 + k] / (c->rat ? c->p[4 * i] : 1);
        if (fabs(dot3(cx, p) - d) > 1e-6) return 0;
    }
    memcpy(n, cx, sizeof cx); *dd = d; return 1;
}
/* MaCurve::BinaryPointsAtTol (0x1803fe780, param_3 1: tangent test, param_2 = the angle, here 10 degrees, param_4 = 1500 points): the
   parameter list starts [t1, t2]; a span is accepted when its 3 interior samples at 1/4, 1/2, 3/4 of the span are within the angle of the
   end tangents and of each other, and the chords to the 1/4 and 3/4 samples within the angle of the end tangents; otherwise its midpoint
   is inserted. n == 3 on a closed curve: retry with the angle halved, three times. */
static int binary_pts(const NC *c, double ang_deg, int max, double **ppar, double (**ppts)[3], int *pn) {
    double ang = ang_deg;
    for (int retry = 0; retry < 4; retry++) {
        int n = 2, cap = max + 2; double *T = malloc(sizeof(double) * cap); T[0] = c->t1; T[1] = c->t2;
        double cs = cos(ang * 0.017453292519943295);
        int i = 0, fail = 0;
        while (i < n - 1) {
            double a = T[i], b = T[i + 1], A[3], B[3], Ps[3], Pe[3], D[3];
            nc_deriv(c, a, Ps, A); nc_deriv(c, b, Pe, B); unit3(A); unit3(B);
            int ok = 1;
            for (int k = 0; k < 3 && ok; k++) {
                double f = (k + 1) * 0.25, Pk[3]; nc_deriv(c, a + (b - a) * f, Pk, D); unit3(D);
                /* |B.A| >= cos; |A.D| >= cos; |B.D| >= cos; the 1/4 chord along A, the 3/4 chord along B (abs values, l.1803ff3c1 flows) */
                if (fabs(dot3(B, A)) < cs || fabs(dot3(A, D)) < cs || fabs(dot3(B, D)) < cs) { ok = 0; break; }
                if (k == 0) { double ch[3] = { Pk[0] - Ps[0], Pk[1] - Ps[1], Pk[2] - Ps[2] }; unit3(ch); if (fabs(dot3(A, ch)) < cs) ok = 0; }
                if (k == 2) { double ch[3] = { Pe[0] - Pk[0], Pe[1] - Pk[1], Pe[2] - Pk[2] }; unit3(ch); if (fabs(dot3(B, ch)) < cs) ok = 0; }
            }
            if (ok) { i++; continue; }
            if (n >= max) { fail = 1; break; }
            memmove(T + i + 2, T + i + 1, sizeof(double) * (n - i - 1));
            T[i + 1] = (a + b) * 0.5; n++;
        }
        if (!fail && n == 3 && retry < 3) {
            double s[3], e[3]; nc_eval(c, c->t1, s); nc_eval(c, c->t2, e);
            if (d3(s, e) <= 1e-12) { free(T); ang *= 0.5; continue; }
        }
        *ppar = T; *pn = n; if (ppts) { double (*P)[3] = malloc(sizeof(double[3]) * n); for (int k = 0; k < n; k++) nc_eval(c, T[k], P[k]); *ppts = P; }
        return fail ? 1 : 0;
    }
    return 1;
}
/* MaArc::MaArc(p_3d, p_3d, p_3d): the circle through three points */
static int arc3(const double a[3], const double b[3], const double c[3], double C[3], double *r, double N[3]) {
    double ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ac[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] }, n[3];
    cross3(ab, ac, n); double n2 = dot3(n, n); if (n2 < 1e-24) return 0;
    double abm = dot3(ab, ab), acm = dot3(ac, ac), t1[3], t2[3];
    cross3(n, ab, t1); cross3(ac, n, t2);
    for (int k = 0; k < 3; k++) C[k] = a[k] + (acm * t1[k] + abm * t2[k]) / (2 * n2);
    *r = d3(C, a); memcpy(N, n, sizeof n); unit3(N);
    return 1;
}
/* the distance from a point to the arc from s through m to e (MaArc::PointMinDist): on the sweep the radial distance, off it the nearer end */
static double arc_dist(const double C[3], double r, const double N[3], const double s[3], double sw, const double p[3]) {
    double u[3] = { s[0] - C[0], s[1] - C[1], s[2] - C[2] }, v[3], q[3] = { p[0] - C[0], p[1] - C[1], p[2] - C[2] };
    unit3(u); cross3(N, u, v);
    double x = dot3(q, u), y = dot3(q, v), a = atan2(y, x); if (a < 0) a += 2 * M_PI;
    if (a > sw) { double e[3]; for (int k = 0; k < 3; k++) e[k] = C[k] + r * (cos(sw) * u[k] + sin(sw) * v[k]); double d1 = d3(p, s), d2 = d3(p, e); return d1 < d2 ? d1 : d2; }
    double qh = sqrt(x * x + y * y), dz = dot3(q, N);
    return sqrt((qh - r) * (qh - r) + dz * dz);
}
/* MaCurve::IsCircular (0x180403940): planar; the points at the angle tolerance (BinaryPointsAtTol(0, 10 deg, 1, 1500); fewer than 20 of a
   non-polyline: 20 uniform) within surf_step_chord_dev of the arc through the start, middle, end - radially and by PointMinDist. A closed
   curve: its two halves separately. */
static int check_arc(const NC *c, const double s[3], const double m[3], const double e[3], double tol, double *ppar, int np, double (*pts)[3]) {
    double C[3], r, N[3]; if (!arc3(s, m, e, C, &r, N)) return 0;
    double u[3] = { s[0] - C[0], s[1] - C[1], s[2] - C[2] }, v[3], q[3] = { e[0] - C[0], e[1] - C[1], e[2] - C[2] }, w[3] = { m[0] - C[0], m[1] - C[1], m[2] - C[2] };
    unit3(u); cross3(N, u, v);
    double ae = atan2(dot3(q, v), dot3(q, u)); if (ae < 0) ae += 2 * M_PI;
    double am = atan2(dot3(w, v), dot3(w, u)); if (am < 0) am += 2 * M_PI;
    double sw = am <= ae ? ae : ae - 2 * M_PI; /* the sweep through m */
    if (sw < 0) { N[0] = -N[0]; N[1] = -N[1]; N[2] = -N[2]; sw = -sw; }
    (void)ppar;
    for (int i = 0; i < np; i++) {
        double d = d3(C, pts[i]);
        if (fabs(d - r) > tol) return 0;
        if (arc_dist(C, r, N, s, sw, pts[i]) > tol) return 0;
    }
    return 1;
}
static double closed_tol = 1e-4;
static int nc_points(const NC *c, double t1, double t2, double (**ppts)[3], int *pn) {
    NC sub = *c; sub.t1 = t1; sub.t2 = t2;
    double *par = NULL; double (*pts)[3] = NULL; int n = 0;
    binary_pts(&sub, 10.0, 1500, &par, &pts, &n);
    free(par);
    if (n < 20 && !(c->deg == 1 && c->ncp > 2)) {
        free(pts); n = 20; pts = malloc(sizeof(double[3]) * n);
        for (int i = 0; i < n; i++) nc_eval(c, t1 + (t2 - t1) * i / 19.0, pts[i]);
    }
    *ppts = pts; *pn = n; return 0;
}
/* MaCurve::IsClosed(-1) -> GetClosedInfo (0x1804022b0): tol -1 = 1e-4 inch / 0.0025 mm; closed when the ends are within tol and the start is farther from the middle parameter's
   point than from the end; periodic (bit 3 of the record, tangents within cos 10 degrees of each other) only then */
static int nc_closed_info(const NC *c, double tol, int *per) {
    double a[3], b[3], m[3], d0[3], d1[3], p0[3], p1[3];
    nc_deriv(c, c->t1, p0, d0); nc_deriv(c, c->t2, p1, d1); memcpy(a, p0, sizeof a); memcpy(b, p1, sizeof b); unit3(d0); unit3(d1);
    nc_eval(c, (c->t1 + c->t2) * 0.5, m);
    double ch = d3(a, b); *per = 0;
    if (ch > tol || d3(m, a) <= ch) return 0;
    if (dot3(d0, d1) >= cos(0.1745329251994329)) *per = 1;
    return 1;
}
/* the circular test and, when circular, the arc: closed / open */
static int nc_circular(const NC *c, double tol, double p1[3], double p2[3], double p3[3], int *closed_out) {
    double n[3], dd, C[3], r, N[3]; if (!nc_is_planar(c, n, &dd)) return 0;
    double s[3], m[3], e[3], tm = (c->t1 + c->t2) * 0.5;
    nc_eval(c, c->t1, s); nc_eval(c, tm, m); nc_eval(c, c->t2, e);
    int perb, closed = nc_closed_info(c, closed_tol, &perb);
    double (*pts)[3]; int np;
    *closed_out = closed;
    if (closed) {   /* MaCurve::ConvertToArc (0x1803fff50): the arc through start, the quarter parameter and the middle; delta 2 pi */
        double tq1 = (c->t1 + tm) * 0.5, tq3 = (tm + c->t2) * 0.5, m1[3], m2[3];
        nc_eval(c, tq1, m1); nc_eval(c, tq3, m2);
        nc_points(c, c->t1, tm, &pts, &np); int ok = check_arc(c, s, m1, m, tol, NULL, np, pts); free(pts);
        if (ok) { nc_points(c, tm, c->t2, &pts, &np); ok = check_arc(c, m, m2, e, tol, NULL, np, pts); free(pts); }
        if (!ok || !arc3(s, m1, m, C, &r, N)) return 0;
        memcpy(p1, s, 24); memcpy(p2, m1, 24); memcpy(p3, m, 24);
        return 1;
    }
    nc_points(c, c->t1, c->t2, &pts, &np); int ok = check_arc(c, s, m, e, tol, NULL, np, pts); free(pts);
    if (!ok || !arc3(s, m, e, C, &r, N)) return 0;
    memcpy(p1, s, 24); memcpy(p2, m, 24); memcpy(p3, e, 24);
    return 1;
}

/* the entity from a curve piece: MaCurve::Simplify (0x180408f80) to a line (IsLinear -> ConvertToLine) or an arc (IsCircular ->
   ConvertToArc), else MaCurve::MakeSplineOrNURBCurve (0x1804059a0): with cfg_createNurbs (surface type 2, the default) the NURBS itself
   (MaNURBSpline::MakeMCEnt -> put_nurbs_in_db); cfg_createNurbs off (ConvertToCubicSpline) is NOT PORTED - callers check first. */
/* the matrix of view v: the part's stored record of it, else the built-in one */
static int view_mat(const McamFile *f, int v, double T[9]) { for (int i = 0; i < f->nfviews; i++) if (f->fviews[i].n == v) { memcpy(T, f->fviews[i].m, sizeof(double) * 9); return 1; } return mcam_std_view(v, T); }
static int want_nurbs = 1, emit_piecewise;   /* emit_piecewise: MaCurve::IsPiecewiseLinearNURBCurve true: MakeSurfaceEdgeCurve / MakeAllSurfaceEdgeCurves skip Simplify (order 2, ncp > 2) */
static int emit(McamFile *f, const NC *c, double tol, double systol, int level, int color, int *made, int cap, int *out, const char **why) {
    double s[3], e[3]; nc_eval(c, c->t1, s); nc_eval(c, c->t2, e);
    if (!emit_piecewise && nc_is_linear(c)) {
        if (d3(s, e) <= systol) return 1;   /* MaLine::MakeMCEnt (0x18042b870), as MaArc::MakeMCEnt: MaCurve::IsDegenerate(tol) - a line's middle derivative is its length */
        { V3 a = { s[0], s[1], s[2] }, b = { e[0], e[1], e[2] }; int ke = mcam_new_ent(f, E_LINE, a, b, (V3){ 0, 0, 0 }, 0, 0, 0, level, color); if (*made < cap) out[(*made)++] = ke; }
        return 1;
    }
    double p1[3], p2[3], p3[3]; int cl;
    if (!emit_piecewise && nc_circular(c, tol, p1, p2, p3, &cl)) {
        /* MaCurve::ConvertToArc -> MaArc(p, p, p) = MakeArcFrom3Points (0x1803d1d80): k = unit((p1 - p2) x (p2 - p3)); find_view_k (mastercam.dll 0x180337960): views 1.. in order, | |k . z| - 1 | <= 1e-12 at once, else the best within 2e-7, else
           make_view_k + AddMCView; the arc in that view by cr_a_3p (0x1804b5c90): the start angle at p1 restricted to 0 .. 2 pi and the signed sweep of find_sweep_angle (0x1804b6880); a closed curve's delta is 2 pi (SetDeltaAngle). MaArc::MakeMCEnt
           -> store_the_arc (mccad 0x17c150) store_ent keeps the sweep as it is: stored arcs have negative sweeps (PARALLELSTEEP ent 38: -4.46 deg in view 5). */
        double a[3] = { p1[0] - p2[0], p1[1] - p2[1], p1[2] - p2[2] }, b_[3] = { p2[0] - p3[0], p2[1] - p3[1], p2[2] - p3[2] }, k_[3], M[9], Cc[3], Nn[3], rr;
        cross3(a, b_, k_); unit3(k_);
        int vn = 0, best = 0; double bestd = 2e-7;
        /* n_views starts at 18 (mastercam.dll init, decomp/mastercam_stdviews_init.c:28): views 1 .. 18 are built in; a part's stored record of one of them is the view Mastercam has */
        for (int v = 1; v <= 18 && !vn; v++) { double T[9]; if (!view_mat(f, v, T)) continue; double d = fabs(fabs(k_[0] * T[6] + k_[1] * T[7] + k_[2] * T[8]) - 1); if (d <= 1e-12) { vn = v; memcpy(M, T, sizeof M); } else if (d < bestd) { bestd = d; best = v; } }
        if (!vn) for (int i = 0; i < f->nfviews && !vn; i++) { if (f->fviews[i].n <= 18) continue; const double *T = f->fviews[i].m; double d = fabs(fabs(k_[0] * T[6] + k_[1] * T[7] + k_[2] * T[8]) - 1); if (d <= 1e-12) { vn = f->fviews[i].n; memcpy(M, T, sizeof M); } else if (d < bestd) { bestd = d; best = f->fviews[i].n; } }
        if (!vn && best) { vn = best; view_mat(f, best, M); }
        if (!vn) { if (!mcsb_view_k(k_, M) || (vn = mcam_add_view(f, M, 2e-7)) <= 0) { *why = "the arc's view could not be made"; return 0; } }
        if (!arc3(p1, p2, p3, Cc, &rr, Nn)) { *why = "degenerate arc"; return 0; }
        double a0 = 0, ae = 0, am = 0, cv[3], q[3];
        for (int k = 0; k < 3; k++) cv[k] = M[3 * k] * Cc[0] + M[3 * k + 1] * Cc[1] + M[3 * k + 2] * Cc[2];
#define VA(P, A) do { for (int k = 0; k < 3; k++) q[k] = M[3 * k] * (P)[0] + M[3 * k + 1] * (P)[1] + M[3 * k + 2] * (P)[2]; A = atan2(q[1] - cv[1], q[0] - cv[0]); if (A < 0) A += 2 * M_PI; } while (0)
        VA(p1, a0); VA(p2, am); VA(p3, ae);
#undef VA
        if (a0 >= 2 * M_PI) a0 -= 2 * M_PI;   /* restrict_angle(0, 2 pi) (cr_a_3p) */
        double d1 = am - a0, d2 = ae - a0; while (d1 < 0) d1 += 2 * M_PI; while (d2 < 0) d2 += 2 * M_PI; if (d1 >= 2 * M_PI) d1 -= 2 * M_PI; if (d2 >= 2 * M_PI) d2 -= 2 * M_PI;
        if (d2 <= d1) d2 -= 2 * M_PI;   /* find_sweep_angle */
        double sw = cl ? 2 * M_PI : d2;
        int ke = vn == 1 ? mcam_new_ent(f, E_ARC, (V3){ 0, 0, 0 }, (V3){ 0, 0, 0 }, (V3){ Cc[0], Cc[1], Cc[2] }, rr, a0, sw, level, color)
                         : mcam_new_arc_view_w(f, vn, M, (V3){ Cc[0], Cc[1], Cc[2] }, rr, a0, sw, level, color);
        if (ke >= 0 && *made < cap) out[(*made)++] = ke;
        return 1;
    }
    (void)systol;
    NC trunc_; int trunc = 0;
    if (c->t1 > c->k[0] || c->t2 < c->k[c->nk - 1]) { nc_sub(c, c->t1, c->t2, &trunc_); c = &trunc_; trunc = 1; }   /* MaNURBSpline::MakeMCEnt 0x180451bd0 / ConvertToCubicSpline 0x180445850: IsTrimmed -> MaCurve::Truncate (0x180409950, by Break: the curve between the trim parameters) */
    if (!want_nurbs) {
        /* MaCurve::MakeSplineOrNURBCurve (0x1804059a0): cfg_createNurbs off -> MaNURBSpline::ConvertToCubicSpline (0x180445850): degree 3 and not rational (else the vtable +0x70 FitCubicApproxCurve fallback: NOT PORTED); per unique knot span the Bezier points
           (Evaluate's this+0x158 row, here by knot insertion): MaCubicCurve from P0 = B0, P1 = B3, T0 = 3 (B1 - B0), T1 = 3 (B3 - B2) (ConvertToAlgebraic) -> the put_spline_in_db record (class 8) */
        if (c->deg != 3 || c->rat) { *why = "a curve of this degree / rational as a parametric spline (MaCurve::FitCubicApproxCurve) is NOT PORTED"; return 0; }
        int nk = c->nk, ncp = c->ncp, m = nk, nq = 0; double *K = malloc(sizeof(double) * (nk + nk * 3 + 8)), *Q = malloc(sizeof(double) * 3 * (ncp + nk * 3 + 8)), *uq = malloc(sizeof(double) * nk); int *mult = malloc(sizeof(int) * nk);
        memcpy(K, c->k, sizeof(double) * nk); for (int i = 0; i < ncp; i++) { Q[i * 3] = c->p[4 * i + 1]; Q[i * 3 + 1] = c->p[4 * i + 2]; Q[i * 3 + 2] = c->p[4 * i + 3]; }
        for (int i = 0; i < nk; i++) { if (!nq || K[i] != uq[nq - 1]) { uq[nq] = K[i]; mult[nq] = 1; nq++; } else mult[nq - 1]++; }
        for (int q = 1; q < nq - 1; q++) for (int r = mult[q]; r < 3; r++) {
            double t = uq[q]; int ks = 0; while (K[ks + 1] <= t) ks++;
            double *N = malloc(sizeof(double) * 3 * (ncp + 2));
            for (int i = 0; i <= ks - 3; i++) memcpy(N + i * 3, Q + i * 3, 24);
            for (int i = ks - 2; i <= ks; i++) { double den = K[i + 3] - K[i], a = den == 0 ? 0 : (t - K[i]) / den; for (int z = 0; z < 3; z++) N[i * 3 + z] = a * Q[i * 3 + z] + (1 - a) * Q[(i - 1) * 3 + z]; }
            for (int i = ks; i < ncp; i++) memcpy(N + (i + 1) * 3, Q + i * 3, 24);
            memcpy(Q, N, sizeof(double) * 3 * (ncp + 1)); free(N); ncp++;
            for (int i = m; i > ks + 1; i--) K[i] = K[i - 1]; K[ks + 1] = t; m++;
        }
        int nseg = nq - 1; double *coef = malloc(sizeof(double) * 12 * nseg);
        for (int i = 0; i < nseg; i++) { double *B = Q + i * 3 * 3, T0[3], T1[3]; for (int z = 0; z < 3; z++) { T0[z] = (B[3 + z] - B[z]) * 3.0; T1[z] = (B[9 + z] - B[6 + z]) * 3.0; } mcsf_pt_tan_coefs(B, B + 9, T0, T1, 1.0, coef + 12 * i); }
        int pb2, closed = nc_closed_info(c, closed_tol, &pb2); uint32_t rn; uint8_t *rec = mcsf_spline_rec(coef, nseg, s, e, closed, systol, &rn);
        free(K); free(Q); free(uq); free(mult); free(coef);
        if (!rec) { *why = "the parametric spline record failed"; return 0; }
        int kk = mcam_new_curve(f, rec, rn, level, color); free(rec);
        if (kk >= 0 && *made < cap) out[(*made)++] = kk;
        if (trunc) nc_free(&trunc_);
        return 1;
    }
    /* put_nurbs_in_db's record: class 0x80, knots and points as MakenbsSplineType writes them */
    int vals = c->rat ? 4 : 3;
    uint32_t n = 0xd8 + 8 * (uint32_t)(c->nk + vals * c->ncp);
    uint8_t *r_ = calloc(1, n); uint32_t cls = 0x80; memcpy(r_, &cls, 4);
    memcpy(r_ + 0xc8, &c->nk, 4); memcpy(r_ + 0xcc, &c->ncp, 4); r_[0xd0] = (uint8_t)c->deg; r_[0xd1] = (uint8_t)vals;
    int perb2; int closed = nc_closed_info(c, closed_tol, &perb2);
    /* nb_type +0x6a (MakenbsSplineType 0x1804524b0): bit 1 closed, bit 2 polynomial, bit 3 the end tangents agree */
    uint16_t fl = (uint16_t)((closed ? 2 : 0) | (perb2 ? 8 : 0) | (c->rat ? 0 : 4)); memcpy(r_ + 0xd2, &fl, 2);
    memcpy(r_ + 0xd8, c->k, sizeof(double) * c->nk);
    { uint8_t *q = r_ + 0xd8 + 8 * c->nk; for (int i = 0; i < c->ncp; i++) { if (c->rat) { memcpy(q, c->p + 4 * i, 32); q += 32; } else { memcpy(q, c->p + 4 * i + 1, 24); q += 24; } } }
    /* put_nurbs_in_db (0x1802daaf0): an end at the knot's end: its control point's stored values (x y z after the weight of a rational point); else eval_nurbs */
    { double se[3], ee[3]; memcpy(se, s, sizeof se); memcpy(ee, e, sizeof ee);
      if (fabs(c->t1 - c->k[0]) < 1e-8) for (int k = 0; k < 3; k++) se[k] = c->p[1 + k];
      if (fabs(c->t2 - c->k[c->nk - 1]) < 1e-8) for (int k = 0; k < 3; k++) ee[k] = c->p[4 * (c->ncp - 1) + 1 + k];
      wdd(r_ + 0x68, se[0]); wdd(r_ + 0x70, se[1]); wdd(r_ + 0x78, se[2]); wdd(r_ + 0x80, ee[0]); wdd(r_ + 0x88, ee[1]); wdd(r_ + 0x90, ee[2]); }
    wdd(r_ + 0x98, c->t1); wdd(r_ + 0xa0, c->t2);
    /* put_nurbs_in_db: the curvature maximum over the parameter (nb_type +0x40), as mcsb_arc_nurbs_rec does */
    CurveRec cr = { 0, 0x80, r_, n };
    /* put_nurbs_in_db: degree < 2: 1e-9; else nb_parm_curvature (0x1805b0300: |C' x C''| / |C'| per unit of the normalised parameter, 0 below |C'| 1e-6) at 2 ncp - 2 steps, kept under 1e9 */
    double cmax = 0, dt = (c->t2 - c->t1) / (double)(c->ncp * 2 - 2), sc = 1.0 / (c->t2 - c->t1);
    if (c->deg >= 2) for (double tt = c->t1; tt <= c->t2; tt += dt) {
        double dv[3][3]; if (mcs_spline_eval_d(&cr, tt, 2, dv)) continue;
        double a_[3] = { dv[1][0] * sc, dv[1][1] * sc, dv[1][2] * sc }, b_[3] = { dv[2][0] * sc * sc, dv[2][1] * sc * sc, dv[2][2] * sc * sc }, cx[3];
        double la = sqrt(dot3(a_, a_)), kk = 0;
        if (la > 1e-6) { cross3(a_, b_, cx); kk = sqrt(dot3(cx, cx)) / la; }
        if (cmax < kk && kk < 1e9) cmax = kk;
    }
    if (cmax < 1e-9) cmax = 1e-9;
    wdd(r_ + 0xa8, cmax);
    int kk = mcam_new_curve(f, r_, n, level, color); free(r_);
    if (kk >= 0 && *made < cap) out[(*made)++] = kk;
    if (trunc) nc_free(&trunc_);
    return 1;
}

/* a non-rational curve from the intersection kernel (mcsrfint.c) to the database: MaCurve::Simplify else MakeSplineOrNURBCurve (emit); the knots already 0 .. 1 */
int mcsc_nurbs_cfg = 1;   /* cfg_createNurbs (the command sets it; surface type config 10014, cui_spline.c sp_nurbs_cfg) */
int mcsc_emit_mi_range(McamFile *f, int deg, int ncp, int nk, const double *k, const double *p, double t0, double t1, double tol, double systol, int level, int color, int *out, int cap, int *made, const char **why);
int mcsc_emit_mi(McamFile *f, int deg, int ncp, int nk, const double *k, const double *p, double tol, double systol, int level, int color, int *out, int cap, int *made, const char **why) { return mcsc_emit_mi_range(f, deg, ncp, nk, k, p, 0.0, 1.0, tol, systol, level, color, out, cap, made, why); }
/* as mcsc_emit_mi with the curve's trim range t0, t1 (the Simplify tests of MaCurve::Simplify 0x180408f80 see the whole NURBS: MaNURBSpline::IsLinear 0x1804500a0 over its unique knots, MaCurve::IsCircular over the range; the record is the truncated curve) */
int mcsc_emit_mi_range(McamFile *f, int deg, int ncp, int nk, const double *k, const double *p, double t0, double t1, double tol, double systol, int level, int color, int *out, int cap, int *made, const char **why) {
    NC c; memset(&c, 0, sizeof c); c.deg = deg; c.ncp = ncp; c.nk = nk; c.rat = 0; c.k = malloc(sizeof(double) * nk); c.p = malloc(sizeof(double) * ncp * 4); c.t1 = t0; c.t2 = t1;
    memcpy(c.k, k, sizeof(double) * nk); for (int i = 0; i < ncp; i++) { c.p[4 * i] = 1.0; c.p[4 * i + 1] = p[3 * i]; c.p[4 * i + 2] = p[3 * i + 1]; c.p[4 * i + 3] = p[3 * i + 2]; }
    want_nurbs = mcsc_nurbs_cfg; closed_tol = mcam_is_metric(f) ? 0.0025 : 0.0001;
    int r = emit(f, &c, tol, systol, level, color, made, cap, out, why); nc_free(&c); return r;
}

/* ---- solid edges: ps_edge_to_mc_curve (mcgeomsld 0x1800434d0) of one edge (the OCCT edge data, occt_edges.cpp): FUN_180043850 for a line (> tol long) / circle, FUN_180043e40 -> FUN_180044820
   (PK_CURVE_make_bcurve + PK_BCURVE_ask: here occt_fill_curve's GeomConvert) else. local_227 (PK_EDGE_ask_geometry's sense) = along: not along the curve the ends swap and the arc's sweep is negative;
   the arc's view: make_view_k(the circle's axis) -> find_view_m(create): mcsb_view_k, the reserved views by mx33 within 2e-7 [ours: find_view_m's tolerance], else mcam_add_view. FUN_180043e40's fallback
   (OUBSED when the bcurve is periodic with a zero start derivative) has no OCCT counterpart and is not reached. */
static void restrict_ang(double lo, double hi, double *a) { while (*a < lo) *a += 2 * M_PI; while (*a >= hi) *a -= 2 * M_PI; }   /* restrict_angle */
int mcsc_emit_edge(McamFile *f, int type, const double p[6], const double c[3], const double n[3], double r, int deg, int nk, int ncp, int rat, const double *k, const double *cp, int along, int pk_along,
                   double systol, int level, int color, int *out, int cap, int *made, const char **why) {
    if (type == 1) {                                     /* FUN_180043850 :304: the line; shorter than tol: param_4 = 1, nothing */
        double a[3], b[3]; for (int j = 0; j < 3; j++) { a[j] = along ? p[j] : p[3 + j]; b[j] = along ? p[3 + j] : p[j]; }
        if (d3(a, b) < systol) return 1;
        if (*made < cap) out[(*made)++] = mcam_new_ent(f, E_LINE, (V3){ a[0], a[1], a[2] }, (V3){ b[0], b[1], b[2] }, (V3){ 0, 0, 0 }, 0, 0, 0, level, color);
        return 1;
    }
    if (type == 2) {                                     /* FUN_180043850 :314-411 */
        double M[9], kk[3] = { n[0], n[1], n[2] }, q[3], cv[3], sv[3], ev[3], a0, ae, sw;
        if (!mcsb_view_k(kk, M)) { *why = "the arc's view could not be made"; return 0; }
        int vn = 0; for (int v = 1; v <= 8 && !vn; v++) { double T[9]; if (!mcam_std_view(v, T)) continue; int eq = 1; for (int i = 0; i < 9; i++) if (fabs(T[i] - M[i]) > 2e-7) eq = 0; if (eq) vn = v; }
        if (!vn && (vn = mcam_add_view(f, M, 2e-7)) <= 0) { *why = "the arc's view could not be made"; return 0; }
#define WTV(P, O) do { for (int j = 0; j < 3; j++) q[j] = M[3 * j] * (P)[0] + M[3 * j + 1] * (P)[1] + M[3 * j + 2] * (P)[2]; memcpy(O, q, sizeof q); } while (0)
        double st[3], en[3]; for (int j = 0; j < 3; j++) { st[j] = along ? p[j] : p[3 + j]; en[j] = along ? p[3 + j] : p[j]; }
        WTV(c, cv); WTV(st, sv); WTV(en, ev);
#undef WTV
        a0 = atan2(sv[1] - cv[1], sv[0] - cv[0]); ae = atan2(ev[1] - cv[1], ev[0] - cv[0]);
        restrict_ang(0.0, 2 * M_PI, &a0);
        if (pk_along) restrict_ang(a0 + 1e-6, a0 + 2 * M_PI + 1e-6, &ae); else restrict_ang(a0 - 2 * M_PI - 1e-6, a0 - 1e-6, &ae);   /* :337 the sweep negative when the edge is against the stored curve */
        sw = ae - a0;
        if (fabs(sw) * r < systol) return 1;              /* :357: length below tol: nothing */
        int ke = vn == 1 ? mcam_new_ent(f, E_ARC, (V3){ 0, 0, 0 }, (V3){ 0, 0, 0 }, (V3){ c[0], c[1], c[2] }, r, a0, sw, level, color)
                         : mcam_new_arc_view_w(f, vn, M, (V3){ c[0], c[1], c[2] }, r, a0, sw, level, color);
        if (ke >= 0 && *made < cap) out[(*made)++] = ke;
        return 1;
    }
    if (type != 3 || nk < 2 || ncp < 2) { *why = "unsupported edge curve"; return 0; }
    if (deg == 1 && ncp == 2 && !rat) {                  /* FUN_180044820 :1602-1659: a degree-1 two-point b-curve is a line (below tol: nothing) */
        double a[3] = { cp[1] / cp[0], cp[2] / cp[0], cp[3] / cp[0] }, b[3] = { cp[5] / cp[4], cp[6] / cp[4], cp[7] / cp[4] };
        if (d3(a, b) < systol) return 1;
        if (*made < cap) out[(*made)++] = mcam_new_ent(f, E_LINE, (V3){ a[0], a[1], a[2] }, (V3){ b[0], b[1], b[2] }, (V3){ 0, 0, 0 }, 0, 0, 0, level, color);
        return 1;
    }
    NC nc = { deg, ncp, nk, rat, malloc(sizeof(double) * nk), malloc(sizeof(double) * 4 * ncp), k[0], k[nk - 1] };
    memcpy(nc.k, k, sizeof(double) * nk); memcpy(nc.p, cp, sizeof(double) * 4 * ncp);
    int sp = emit_piecewise, sw2 = want_nurbs;           /* put_nurbs_in_db directly (no MaCurve::Simplify on this path) */
    emit_piecewise = 1; want_nurbs = 1; closed_tol = mcam_is_metric(f) ? 0.0025 : 0.0001;
    int r2 = emit(f, &nc, systol, systol, level, color, made, cap, out, why);
    emit_piecewise = sp; want_nurbs = sw2; nc_free(&nc);
    return r2;
}
/* surface_open_edges: the record of an untrimmed NURBS (kind 0) for occt_open_edges; 0 for any other kind. A trimmed surface (kind 10) never reaches here: decision N9-28 (formats/user_decisions.md), refused by the caller until true trim curves exist */
int mcsc_open_surf(const McamFile *f, const Surf *s, int *nu, int *nv, int *du, int *dv, int *nku, int *nkv, const double **ku, const double **kv, double **cp) {
    (void)f;
    if (s->kind != 0) return 0;
    const uint8_t *r = s->rec; int ncu = rd32(r + 0x108), ncv = rd32(r + 0x10c), vpp = r[0x110];
    *nku = rd32(r + 0x100); *nkv = rd32(r + 0x104); *nu = ncu; *nv = ncv; *du = r[0x111]; *dv = r[0x112];
    *ku = (const double *)(r + 0x118); *kv = *ku + *nku;
    const uint8_t *CP = r + 0x118 + 8 * ((size_t)*nku + *nkv);
    *cp = malloc(sizeof(double) * 4 * (size_t)ncu * ncv);
    for (size_t i = 0, n = (size_t)ncu * ncv; i < n; i++) { const uint8_t *q = CP + 8 * (size_t)vpp * i; double *o = *cp + 4 * i; if (vpp == 4) for (int m = 0; m < 4; m++) o[m] = rdd(q + 8 * m); else { o[0] = 1; for (int m = 0; m < 3; m++) o[1 + m] = rdd(q + 8 * m); } }
    return 1;
}

/* the NURBS base of a surface (itself, or the base of a kind 10): 0 / NULL = refused (the other kinds: NOT PORTED) */
static const Surf *iso_base(const McamFile *f, const Surf *s, const char **why) {
    const Surf *b = s;
    if (s->kind == 10) { b = s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL; if (!b) { *why = "the trimmed surface's base was not found"; return NULL; } }
    if (b->kind != 0) { *why = "iso curves of this surface kind (MaBicubicSurf / MaSurface::Flowline fit, kinds 1, 2, 4, 6, 7, 8) are NOT PORTED"; return NULL; }
    return b;
}

/* ---------------------------------------------------------------------------- MaSurface::PointMinDist */
/* the surface as MaSurface sees it: the base of a trimmed surface (LoadTrimmedSurf -> MaFace base), its domain (this+0x20..0x38), the seam / pole flags of
   GetEdgeInfo 0x180485b90 (MaSurface::CheckU0Degenerate 0x180480620 etc.: tol 1e-4 inch, 0.0025 mm; closed seam tangents within cos 5 deg) */
typedef struct { const McamFile *f; const Surf *s; double d[4], tol; int deg[4], per[2]; } PM;
static void pm_raw(const PM *m, double u, double v, double p[3]) { if (mcs_surf_eval(m->f, m->s, u, v, p)) p[0] = p[1] = p[2] = 0; }
/* NURBS: basis functions with their first two derivatives at u in span s (the textbook recurrence). Mastercam's MaNURBSurf::Evaluate is analytic too; the same quantities */
static int ns_span(int p, const double *K, int n, double u) { if (u <= K[p]) return p; if (u >= K[n]) return n - 1; int s = p; while (s < n - 1 && !(u < K[s + 1])) s++; return s; }
static void ns_ders(int p, const double *K, double u, int s, double N[3][26]) {
    double nd[26][26], left[26], right[26]; nd[0][0] = 1;
    for (int j = 1; j <= p; j++) { left[j] = u - K[s + 1 - j]; right[j] = K[s + j] - u; double sv = 0;
        for (int r = 0; r < j; r++) { nd[j][r] = right[r + 1] + left[j - r]; double tmp = nd[j][r] != 0 ? nd[r][j - 1] / nd[j][r] : 0; nd[r][j] = sv + right[r + 1] * tmp; sv = left[j - r] * tmp; }
        nd[j][j] = sv; }
    for (int j = 0; j <= p; j++) N[0][j] = nd[j][p];
    for (int r = 0; r <= p; r++) { double a[3][26]; int s1 = 0, s2 = 1; a[0][0] = 1;
        for (int k = 1; k <= 2 && k <= p; k++) { double d = 0; int rk = r - k, pk = p - k;
            if (r >= k) { a[s2][0] = nd[pk + 1][rk] != 0 ? a[s1][0] / nd[pk + 1][rk] : 0; d = a[s2][0] * nd[rk][pk]; }
            int j1 = rk >= -1 ? 1 : -rk, j2 = r - 1 <= pk ? k - 1 : p - r;
            for (int j = j1; j <= j2; j++) { a[s2][j] = nd[pk + 1][rk + j] != 0 ? (a[s1][j] - a[s1][j - 1]) / nd[pk + 1][rk + j] : 0; d += a[s2][j] * nd[rk + j][pk]; }
            if (r <= pk) { a[s2][k] = nd[pk + 1][r] != 0 ? -a[s1][k - 1] / nd[pk + 1][r] : 0; d += a[s2][k] * nd[r][pk]; }
            N[k][r] = d; int sw = s1; s1 = s2; s2 = sw; } }
    double fac = p; for (int k = 1; k <= 2; k++) { for (int j = 0; j <= p; j++) N[k][j] = k <= p ? N[k][j] * fac : 0; fac *= p - k; }
}
/* S, Su, Sv, Suu, Svv, Suv of a NURBS surface in the record's (w, wx, wy, wz) space, then the quotient rule */
static void pm_nurbs(const PM *m, double u, double v, double o[6][3]) {
    const uint8_t *r = m->s->rec; int nku = rd32(r + 0x100), ncu = rd32(r + 0x108), ncv = rd32(r + 0x10c), vpp = r[0x110], du = r[0x111], dv = r[0x112], rat = !(r[0x113] & 1);
    const double *KU = (const double *)(r + 0x118), *KV = KU + nku; const uint8_t *CP = r + 0x118 + 8 * ((size_t)nku + rd32(r + 0x104));
    int su = ns_span(du, KU, ncu, u), sv = ns_span(dv, KV, ncv, v); double Nu[3][26], Nv[3][26]; ns_ders(du, KU, u, su, Nu); ns_ders(dv, KV, v, sv, Nv);
    double A[3][3][4]; memset(A, 0, sizeof A);
    for (int j = 0; j <= dv; j++) for (int i = 0; i <= du; i++) {
        const uint8_t *q = CP + 8 * (size_t)vpp * ((size_t)(sv - dv + j) * ncu + (su - du + i)); double c[4];
        if (vpp == 4) { c[0] = rat ? rdd(q) : 1.0; for (int k = 0; k < 3; k++) c[k + 1] = rdd(q + 8 * (1 + k)); } else { c[0] = 1; for (int k = 0; k < 3; k++) c[k + 1] = rdd(q + 8 * k); }
        for (int a = 0; a <= 2; a++) for (int b = 0; b <= 2 - a; b++) for (int k = 0; k < 4; k++) A[a][b][k] += Nu[a][i] * Nv[b][j] * c[k];
    }
    double w = A[0][0][0], wu = A[1][0][0], wv = A[0][1][0], wuu = A[2][0][0], wvv = A[0][2][0], wuv = A[1][1][0]; if (!(fabs(w) > 1e-300)) w = 1;
    for (int k = 0; k < 3; k++) {
        double S = A[0][0][k + 1] / w, Su = (A[1][0][k + 1] - wu * S) / w, Sv = (A[0][1][k + 1] - wv * S) / w;
        o[0][k] = S; o[1][k] = Su; o[2][k] = Sv; o[3][k] = (A[2][0][k + 1] - 2 * wu * Su - wuu * S) / w; o[4][k] = (A[0][2][k + 1] - 2 * wv * Sv - wvv * S) / w;
        o[5][k] = (A[1][1][k + 1] - wu * Sv - wv * Su - wuv * S) / w;
    }
}
static void pm_du(const PM *m, double u, double v, double o[3]) {
    if (m->s->kind == 0) { double d[6][3]; pm_nurbs(m, u, v, d); memcpy(o, d[1], 24); return; }
    double h = (m->d[1] - m->d[0]) * 1e-6, a = u - h, b = u + h, pa[3], pb[3];
    if (a < m->d[0]) { a = m->d[0]; b = a + 2 * h; } if (b > m->d[1]) { b = m->d[1]; a = b - 2 * h; }
    pm_raw(m, a, v, pa); pm_raw(m, b, v, pb); for (int k = 0; k < 3; k++) o[k] = (pb[k] - pa[k]) / (b - a);
}
static void pm_dv(const PM *m, double u, double v, double o[3]) {
    if (m->s->kind == 0) { double d[6][3]; pm_nurbs(m, u, v, d); memcpy(o, d[2], 24); return; }
    double h = (m->d[3] - m->d[2]) * 1e-6, a = v - h, b = v + h, pa[3], pb[3];
    if (a < m->d[2]) { a = m->d[2]; b = a + 2 * h; } if (b > m->d[3]) { b = m->d[3]; a = b - 2 * h; }
    pm_raw(m, u, a, pa); pm_raw(m, u, b, pb); for (int k = 0; k < 3; k++) o[k] = (pb[k] - pa[k]) / (b - a);
}
/* ExtEvaluate 0x1804835b0: a periodic parameter wraps by its period; beyond the range of a non periodic one the surface is extended (ExtensionEvaluate, not
   read: ours linear along the tangent at the boundary) */
static void pm_wrap(const PM *m, double *u, double *v) {
    if (m->per[0]) { double w = m->d[1] - m->d[0]; while (*u < m->d[0]) *u += w; while (*u > m->d[1]) *u -= w; }
    if (m->per[1]) { double w = m->d[3] - m->d[2]; while (*v < m->d[2]) *v += w; while (*v > m->d[3]) *v -= w; }
}
static void pm_eval(const PM *m, double u0, double v0, double p[3], double su[3], double sv[3]) {
    double u = u0, v = v0; pm_wrap(m, &u, &v);
    double uc = u < m->d[0] ? m->d[0] : u > m->d[1] ? m->d[1] : u, vc = v < m->d[2] ? m->d[2] : v > m->d[3] ? m->d[3] : v;
    pm_raw(m, uc, vc, p); pm_du(m, uc, vc, su); pm_dv(m, uc, vc, sv);
    for (int k = 0; k < 3; k++) p[k] += (u - uc) * su[k] + (v - vc) * sv[k];
}
static void pm_second(const PM *m, double u0, double v0, double suu[3], double svv[3], double suv[3]) {
    double u = u0, v = v0; pm_wrap(m, &u, &v);
    if (u < m->d[0]) u = m->d[0]; if (u > m->d[1]) u = m->d[1]; if (v < m->d[2]) v = m->d[2]; if (v > m->d[3]) v = m->d[3];
    if (m->s->kind == 0) { double d[6][3]; pm_nurbs(m, u, v, d); memcpy(suu, d[3], 24); memcpy(svv, d[4], 24); memcpy(suv, d[5], 24); return; }
    double hu = (m->d[1] - m->d[0]) * 1e-4, hv = (m->d[3] - m->d[2]) * 1e-4, a[3], b[3], c[3];
    double ua = u - hu, ub = u + hu; if (ua < m->d[0]) { ua = m->d[0]; ub = ua + 2 * hu; } if (ub > m->d[1]) { ub = m->d[1]; ua = ub - 2 * hu; }
    double va = v - hv, vb = v + hv; if (va < m->d[2]) { va = m->d[2]; vb = va + 2 * hv; } if (vb > m->d[3]) { vb = m->d[3]; va = vb - 2 * hv; }
    double um = (ua + ub) * 0.5, vm = (va + vb) * 0.5, du = ub - um, dv = vb - vm;
    pm_raw(m, ua, v, a); pm_raw(m, um, v, b); pm_raw(m, ub, v, c); for (int k = 0; k < 3; k++) suu[k] = (a[k] - 2 * b[k] + c[k]) / (du * du);
    pm_raw(m, u, va, a); pm_raw(m, u, vm, b); pm_raw(m, u, vb, c); for (int k = 0; k < 3; k++) svv[k] = (a[k] - 2 * b[k] + c[k]) / (dv * dv);
    double aa[3], ab[3], ba[3], bb[3]; pm_raw(m, ua, va, aa); pm_raw(m, ua, vb, ab); pm_raw(m, ub, va, ba); pm_raw(m, ub, vb, bb);
    for (int k = 0; k < 3; k++) suv[k] = (bb[k] - ba[k] - ab[k] + aa[k]) / ((ub - ua) * (vb - va));
}
/* CheckU0Degenerate / U1 / V0 / V1 (0x180480620..): 5 points along the edge, every consecutive pair within tol. CheckUClosedOrPeriodic 0x180480940: the two edges coincide within
   tol at 5 points and the cross tangents agree within cos 5 degrees. */
static void pm_init(PM *m, const McamFile *f, const Surf *s) {
    const Surf *b = s;
    if (s->kind == 10 && s->n >= 0x108) { const Surf *bb = mcs_surf(f, rd64(s->rec + 0x100)); if (bb) b = bb; }
    m->f = f; m->s = b; mcs_surf_domain(b, m->d);
    m->tol = mcam_is_metric(f) ? 0.0025 : 0.0001;
    double p[5][3];
    for (int e = 0; e < 4; e++) {
        for (int i = 0; i < 5; i++) { double a = (double)i / 4; pm_raw(m, e < 2 ? (e ? m->d[1] : m->d[0]) : m->d[0] + (m->d[1] - m->d[0]) * a, e < 2 ? m->d[2] + (m->d[3] - m->d[2]) * a : (e == 2 ? m->d[2] : m->d[3]), p[i]); }
        int deg = 1; for (int i = 0; i < 4; i++) if (d3(p[i], p[i + 1]) > m->tol) { deg = 0; break; }
        m->deg[e] = deg;
    }
    for (int dir = 0; dir < 2; dir++) {
        int per = 1; double cs5 = cos(5.0 * 0.017453292519943295);
        for (int i = 0; i < 5 && per; i++) { double a = (double)i / 4, Pa[3], Pb[3], Ta[3], Tb[3];
            if (dir == 0) { double vv = m->d[2] + (m->d[3] - m->d[2]) * a; pm_raw(m, m->d[0], vv, Pa); pm_raw(m, m->d[1], vv, Pb); pm_du(m, m->d[0], vv, Ta); pm_du(m, m->d[1], vv, Tb); }
            else { double uu = m->d[0] + (m->d[1] - m->d[0]) * a; pm_raw(m, uu, m->d[2], Pa); pm_raw(m, uu, m->d[3], Pb); pm_dv(m, uu, m->d[2], Ta); pm_dv(m, uu, m->d[3], Tb); }
            if (d3(Pa, Pb) > m->tol) { per = 0; break; }
            unit3(Ta); unit3(Tb); if (dot3(Ta, Tb) < cs5) per = 0; }
        m->per[dir] = per;
    }
    if (m->deg[0] || m->deg[1]) m->per[0] = 0; if (m->deg[2] || m->deg[3]) m->per[1] = 0;   /* GetEdgeInfo: a pole clears the seam flags */
}
/* MaSurface::TessellateUntrimmed (0x18048d4c0): the parameter list of the iso curves in direction dir (1: constant u, the list in v; 2: constant v, the list in u) at the five quarter values of the other parameter, each bisected from [lo, hi] (the mode-0 chord test at 1/4, 1/2, 3/4 of a span
   against chord_tol, mode 1 the tangent and surface-normal tests: |T0.T1| and Tk against T0 / T1 (no abs) at cos(ang), the normals |N0.N1|, |Nk.N0|, |Nk.N1|, then Tk at f - 1/12 against T0 and T1 - skipped while a tangent at the span's ends is shorter than 1e-7 inch / 2.5e-6 mm);
   a span that fails is halved. The first iso's list is kept, the others' interior values are inserted strictly between neighbours. More than max values: -1. */
static void pm_iso(const PM *m, int dir, double other, double t, double p[3], double T[3], double N[3]) {
    double u = dir == 1 ? other : t, v = dir == 1 ? t : other, S[3], su[3], sv[3];
    pm_raw(m, u, v, S); pm_du(m, u, v, su); pm_dv(m, u, v, sv);
    memcpy(p, S, 24); memcpy(T, dir == 1 ? sv : su, 24); cross3(su, sv, N); unit3(N);
}
static int pm_tess(const PM *m, const double dom[4], double chord_tol, double ang, int mode, int dir, double *o, int max) {
    double oa = dir == 1 ? dom[0] : dom[2], ob = dir == 1 ? dom[1] : dom[3], lo = dir == 1 ? dom[2] : dom[0], hi = dir == 1 ? dom[3] : dom[1];
    double step = (ob - oa) * 0.25, cs = cos(ang * 0.017453292519943295), small = mcam_is_metric(m->f) ? 2.5e-6 : 1e-7, F[3] = { 0.25, 0.5, 0.75 };
    double *T = malloc(sizeof(double) * (max + 2)); int total = 0;
    for (int q = 0; q < 5; q++) {
        double other = oa + step * q; int n = 2; T[0] = lo; T[1] = hi;
        for (int i = 0; i < n - 1; ) {
            double a = T[i], b = T[i + 1], Ps[3], Ts[3], Ns[3], Pe[3], Te[3], Ne[3], ch[3];
            pm_iso(m, dir, other, a, Ps, Ts, Ns); pm_iso(m, dir, other, b, Pe, Te, Ne);
            double ls = sqrt(dot3(Ts, Ts)), le = sqrt(dot3(Te, Te)); int skip = ls < small || le < small; unit3(Ts); unit3(Te);
            for (int k = 0; k < 3; k++) ch[k] = Pe[k] - Ps[k];
            double chl = sqrt(dot3(ch, ch)); if (chl > 0) for (int k = 0; k < 3; k++) ch[k] /= chl;
            int ok = 1;
            for (int k = 0; k < 3 && ok; k++) {
                double f = a + (b - a) * F[k], Pk[3], Tk[3], Nk[3]; pm_iso(m, dir, other, f, Pk, Tk, Nk);
                if (mode == 0) {
                    double d[3] = { Pk[0] - Ps[0], Pk[1] - Ps[1], Pk[2] - Ps[2] }, c[3], dev;
                    if (chl < 1e-6) dev = sqrt(dot3(d, d)); else { cross3(d, ch, c); dev = sqrt(dot3(c, c)); }
                    if (dev > chord_tol) ok = 0;
                } else if (mode == 1 && !skip) {
                    unit3(Tk);
                    if (fabs(dot3(Ts, Te)) < cs || dot3(Tk, Ts) < cs || dot3(Tk, Te) < cs || fabs(dot3(Ns, Ne)) < cs || fabs(dot3(Nk, Ns)) < cs || fabs(dot3(Nk, Ne)) < cs) ok = 0;
                    else { double g = a + (b - a) * (F[k] - 0.08333333333333333), P2[3], T2[3], N2[3]; pm_iso(m, dir, other, g, P2, T2, N2); unit3(T2); if (dot3(T2, Ts) < cs || dot3(T2, Te) < cs) ok = 0; }
                }
            }
            if (ok) { i++; continue; }
            if (n >= max) { free(T); return -1; }
            memmove(T + i + 2, T + i + 1, sizeof(double) * (n - i - 1)); T[i + 1] = (a + b) * 0.5; n++;
        }
        if (q == 0) { memcpy(o, T, sizeof(double) * n); total = n; }
        else {
            int k = 1;
            for (int s = 1; s < n - 1; s++) {
                while (k < total && o[k] <= T[s]) k++;
                if (k >= total) break;
                if (T[s] <= o[k - 1]) continue;
                if (total >= max) { free(T); return -1; }
                memmove(o + k + 1, o + k, sizeof(double) * (total - k)); o[k] = T[s]; total++;
            }
        }
    }
    free(T); return total;
}
/* |N| of MaSurface::Evaluate(1, ..) (the slot +0x18, EvaluateSrf ma_classes.c:64020) at (u, v): Su x Sv in MaSurface's parameters (NURBS: knots normalised, ksu, ksv the file's knot ranges), through MaSurface::VerifyNormal (0x180490ad0: |N| <= 1e-6, up to 6 times: the parameters nudged 1e-6 toward the middle) */
static double pm_normal_mag(const PM *m, double u, double v, double ksu, double ksv) {
    double mu = (m->d[0] + m->d[1]) * 0.5, mv = (m->d[2] + m->d[3]) * 0.5, hu = 1e-6 * ksu, hv = 1e-6 * ksv, su[3], sv[3], n[3];
    for (int tries = 0; ; tries++) {
        pm_du(m, u, v, su); pm_dv(m, u, v, sv); for (int k = 0; k < 3; k++) { su[k] *= ksu; sv[k] *= ksv; }
        cross3(su, sv, n); double nm = sqrt(dot3(n, n)), lu = sqrt(dot3(su, su)), lv = sqrt(dot3(sv, sv));
        if (!(nm <= 1e-6 && tries < 6)) return nm;
        if (lu <= 1e-6 || lv <= 1e-6) {
            if (1e-6 < lu) { if (u < mu) u += hu; if (!(u < mu)) u -= hu; continue; }
        } else { if (u < mu) u += hu; if (mu <= u) u -= hu; }
        if (v < mv) v += hv;
        if (mv <= v) v -= hv;
    }
}
/* MaSurfHuntingGrid::AdjustRangeForPolesAndSeams (0x180494920, ma_classes.c:92506): a pole's side moves in by |N| at the middle of that edge, at most 1e-4 of the range; then each periodic direction moves in by 1e-4 of the (already adjusted) range at both ends. GenGrid puts the range back afterwards (:93157), so this only shapes the seed grid. */
static void pm_seed_range(const PM *m, double o[4]) {
    double ksu = 1, ksv = 1;
    if (m->s->kind == 0) { const uint8_t *r = m->s->rec; int nku = rd32(r + 0x100), nkv = rd32(r + 0x104); const double *KU = (const double *)(r + 0x118), *KV = KU + nku; ksu = KU[nku - 1] - KU[0]; ksv = KV[nkv - 1] - KV[0]; if (!(ksu > 0)) ksu = 1; if (!(ksv > 0)) ksv = 1; }
    memcpy(o, m->d, sizeof(double) * 4);
    double du = m->d[1] - m->d[0], dv = m->d[3] - m->d[2], mu = (m->d[0] + m->d[1]) * 0.5, mv = (m->d[2] + m->d[3]) * 0.5;
    if (m->deg[0]) { double a = o[0] + pm_normal_mag(m, m->d[0], mv, ksu, ksv) * ksu, b = du * 1e-4 + m->d[0]; o[0] = b < a ? b : a; }
    if (m->deg[1]) { double a = o[1] - pm_normal_mag(m, m->d[1], mv, ksu, ksv) * ksu, b = m->d[1] - du * 1e-4; o[1] = a < b ? b : a; }
    if (m->deg[2]) { double a = o[2] + pm_normal_mag(m, mu, m->d[2], ksu, ksv) * ksv, b = dv * 1e-4 + m->d[2]; o[2] = b < a ? b : a; }
    if (m->deg[3]) { double a = o[3] - pm_normal_mag(m, mu, m->d[3], ksu, ksv) * ksv, b = m->d[3] - dv * 1e-4; o[3] = a < b ? b : a; }
    double du2 = o[1] - o[0], dv2 = o[3] - o[2];
    if (m->per[0]) { o[0] += du2 * 1e-4; o[1] -= du2 * 1e-4; }
    if (m->per[1]) { o[2] += dv2 * 1e-4; o[3] -= dv2 * 1e-4; }
}
/* MaSurfHuntingGrid::GenGrid (0x180495470): TessellateUntrimmed(0, 10 deg, mode 1) in each direction over the range AdjustRangeForPolesAndSeams leaves (pm_seed_range), at least 5 values (quarters), or 10 x 10 when it fails.
   A NURBS (a kind 0 record, a trimmed surface's base): the DLL's grid itself - mcsrfint.c grid_build (GenGrid, AdjustRangeForPolesAndSeams, TessellateUntrimmed transliterated) on the surface as
   LoadSurf builds it (mcsc_load_nbs: MaNURBSurf's DB constructor), its values taken back to the record's knots (the regeneration undone, the normalisation's inverse: K0 + x (Kn - K0)).
   Other kinds keep pm_tess (ours: their evaluators are mcsrfcrv.c's, not ported to mcsrfint.c). */
int mcsc_load_nbs(const McamFile *f, const Surf *b, ISrf *o, void *R, double k0[2], double ks[2]);
static int pm_list(const PM *m, int along_u, double *o, int cap) {
    if (m->s->kind == 0) {
        ISrf q; MiRegen RG; memset(&RG, 0, sizeof RG); double k0[2], ks[2]; int r = mcsc_load_nbs(m->f, m->s, &q, &RG, k0, ks);
        if (r >= 0) {
            const double *gu, *gv; int nu, nv; mi_grid_lists(&q, &gu, &nu, &gv, &nv);
            int d = along_u ? 0 : 1, n = along_u ? nu : nv; const double *L = along_u ? gu : gv; if (cap < n) n = cap;
            for (int i = 0; i < n; i++) { double x = L[i]; if (r & (1 << d)) x = mi_regen_unmap(&RG, d, x); o[i] = k0[d] + x * ks[d]; }
            mi_regen_free(&RG); free(q.KU); free(q.KV); free(q.V); srf_mi_free(&q);
            return n;
        }
    }
    double dom[4]; pm_seed_range(m, dom);
    int n = pm_tess(m, dom, 0.0, 10.0, 1, along_u ? 2 : 1, o, cap);
    double lo = along_u ? dom[0] : dom[2], hi = along_u ? dom[1] : dom[3];
    if (n < 0) { n = 10; for (int i = 0; i < 10; i++) o[i] = lo + (hi - lo) * i / 9; }
    else if (n < 5) { n = 5; for (int i = 0; i < 5; i++) o[i] = lo + (hi - lo) * i / 4; }
    return n;
}
/* the iteration of MaSurface::PointMinDist (0x180488080): each step takes the smaller (per parameter) of the step to the tangent-plane foot of the point (Cramer in the tangent plane) and the
   Newton step on the squared distance; with degenerate edges their mean; limited to the parameter range; from the second iteration a secant correction; 50 iterations; stops when the foot
   is reached (distance squared <= 1e-18 inch / 2.5e-17 mm) or the point is along the normal (|d x N| < 1e-6), or the parameters moved less than 1e-10 after 30 iterations. */
int mcsc_uv_of_point(const McamFile *f, const Surf *s, const double P[3], double *uo, double *vo) {
    PM m; pm_init(&m, f, s);
    if (!(m.d[1] > m.d[0]) || !(m.d[3] > m.d[2])) return 0;
    double Lu[5000], Lv[5000]; int nu = pm_list(&m, 1, Lu, 5000), nv = pm_list(&m, 0, Lv, 5000);
    double bu = (m.d[0] + m.d[1]) * 0.5, bv = (m.d[2] + m.d[3]) * 0.5, bd = 1e31;
    for (int i = 0; i < nu; i++) { if ((m.deg[0] && i == 0) || (m.deg[1] && i == nu - 1)) continue;
        for (int j = 0; j < nv; j++) { if ((m.deg[2] && j == 0) || (m.deg[3] && j == nv - 1)) continue;
            double q[3]; pm_raw(&m, Lu[i], Lv[j], q); double dd = (q[0] - P[0]) * (q[0] - P[0]) + (q[1] - P[1]) * (q[1] - P[1]) + (q[2] - P[2]) * (q[2] - P[2]);
            if (dd < bd) { bd = dd; bu = Lu[i]; bv = Lv[j]; } } }
    int has_deg = m.deg[0] || m.deg[1] || m.deg[2] || m.deg[3];
    double u = bu, v = bv, eps = mcam_is_metric(f) ? 2.5e-17 : 1e-18, du_r = m.d[1] - m.d[0], dv_r = m.d[3] - m.d[2], up = 0, vp = 0, dpu = 0, dpv = 0;
    for (int it = 0; it < 50; it++) {
        double S[3], su[3], sv[3], suu[3], svv[3], suv[3], n[3]; pm_eval(&m, u, v, S, su, sv);
        cross3(su, sv, n); unit3(n);
        double d0[3] = { S[0] - P[0], S[1] - P[1], S[2] - P[2] }, nd = dot3(n, P) - dot3(n, S);
        double Q[3] = { P[0] - nd * n[0], P[1] - nd * n[1], P[2] - nd * n[2] }, qs[3] = { Q[0] - S[0], Q[1] - S[1], Q[2] - S[2] }, nx[3], ny[3], cr[3];
        cross3(d0, n, cr);
        if (dot3(qs, qs) <= eps || sqrt(dot3(cr, cr)) < 1e-6) break;
        cross3(n, sv, nx); cross3(n, su, ny);
        double a = dot3(nx, su), b = dot3(ny, sv); if (fabs(a) < 1e-20) a = 1e-20; if (fabs(b) < 1e-20) b = 1e-20;
        double c1 = dot3(qs, nx) / a, c2 = dot3(qs, ny) / b;   /* the tangent-plane step in (u, v) */
        pm_second(&m, u, v, suu, svv, suv);
        double E = dot3(su, su) + dot3(d0, suu), G = dot3(sv, sv) + dot3(d0, svv), F = dot3(su, sv) + dot3(d0, suv), e = dot3(d0, su), g = dot3(d0, sv), det = G * E - F * F;
        double n1 = 0, n2 = 0; if (fabs(det) > 1e-300) { n1 = -(G * e - F * g) / det; n2 = -(E * g - F * e) / det; }
        double st_u, st_v; int lim = 0;
        if (has_deg) { if (it < 20) { st_u = (n1 + c1) * 0.5; st_v = (n2 + c2) * 0.5; } else { st_u = n1; st_v = n2; } }
        else { st_u = fabs(n1) <= fabs(c1) ? n1 : c1; st_v = fabs(n2) <= fabs(c2) ? n2 : c2; }
        if (fabs(st_u) > du_r) { st_u = st_u < 0 ? -du_r : du_r; lim = 1; }
        if (fabs(st_v) > dv_r) { st_v = st_v < 0 ? -dv_r : dv_r; lim = 1; }
        if (has_deg) { double lu = du_r * 0.25, lv = dv_r * 0.25; if (fabs(st_u) > lu) { st_u = st_u < 0 ? -lu : lu; lim = 1; } if (fabs(st_v) > lv) { st_v = st_v < 0 ? -lv : lv; lim = 1; } }
        double su0 = st_u, sv0 = st_v;
        if (it > 0 && !lim) {
            if (fabs(su0 - dpu) >= 1e-20) { double c = fabs((u - up) / (su0 - dpu)) * su0; if (fabs(c) < du_r) st_u = c; }
            if (fabs(sv0 - dpv) >= 1e-20) { double c = fabs((v - vp) / (sv0 - dpv)) * sv0; if (fabs(c) < dv_r) st_v = c; }
        }
        dpu = su0; dpv = sv0; up = u; vp = v;
        double nu_ = u + st_u, nv_ = v + st_v;
        if (m.deg[0] && nu_ < m.d[0]) nu_ = m.d[0]; if (m.deg[1] && nu_ > m.d[1]) nu_ = m.d[1];
        if (m.deg[2] && nv_ < m.d[2]) nv_ = m.d[2]; if (m.deg[3] && nv_ > m.d[3]) nv_ = m.d[3];
        double mu = fabs(nu_ - u), mv = fabs(nv_ - v); u = nu_; v = nv_;
        if (it > 30 && mu < 1e-10 && mv < 1e-10) break;
    }
    pm_wrap(&m, &u, &v);
    /* param_7 of the caller (CreateConstantParameterCurves: 0) would LimitUVToRange; the iso parameter stays where the iteration ended */
    /* DEVIATION approved by the user, N9-25 in formats/user_decisions.md. This fallback is ours, not Mastercam's code: the DLL itself misses 12 of 399 points on ent 42 (PARALLELSTEEP.mcx-9, tools/peloader --pmd:
       MaSurface::PointMinDist returns 1 and keeps its last iterate; a pole at u0 and a near-zero Sv), the port of its iteration without this block misses 16 (7 of them the same points), and on an ordinary surface (ent 43) they agree on 399 of 399.
       When the point is not reached within the edge tolerance a damped Gauss-Newton from a dense grid takes over and the closer of the two results is kept: picks near poles stay on the surface where Mastercam's would not. MCSC_NOGUARD=1 turns it off for the oracle comparison. */
    double qe[3]; pm_raw(&m, u, v, qe);
    if (d3(qe, P) > m.tol && !getenv("MCSC_NOGUARD")) {
        double gu = u, gv = v, gd = 1e300; int G = 128;
        for (int i = 0; i <= G; i++) for (int j = 0; j <= G; j++) {
            double uu = m.d[0] + du_r * i / G, vv = m.d[2] + dv_r * j / G, q[3]; pm_raw(&m, uu, vv, q);
            double dd = d3(q, P); if (dd < gd) { gd = dd; gu = uu; gv = vv; } }
        for (int it = 0; it < 60; it++) {
            double S[3], su[3], sv[3]; pm_eval(&m, gu, gv, S, su, sv);
            double d0[3] = { P[0] - S[0], P[1] - S[1], P[2] - S[2] }, a = dot3(su, su), b_ = dot3(su, sv), c = dot3(sv, sv), e = dot3(d0, su), g = dot3(d0, sv), det = a * c - b_ * b_;
            if (!(fabs(det) > 1e-30)) break;
            double du = (e * c - g * b_) / det, dv = (a * g - b_ * e) / det, lam = 1, cur = sqrt(dot3(d0, d0));
            int ok = 0;
            for (int k = 0; k < 24; k++, lam *= 0.5) {
                double nu_ = fmin(m.d[1], fmax(m.d[0], gu + lam * du)), nv_ = fmin(m.d[3], fmax(m.d[2], gv + lam * dv)), q[3]; pm_raw(&m, nu_, nv_, q);
                if (d3(q, P) < cur) { gu = nu_; gv = nv_; ok = 1; break; }
            }
            if (!ok || (fabs(lam * du) < 1e-12 && fabs(lam * dv) < 1e-12)) break;
        }
        double qg[3]; pm_raw(&m, gu, gv, qg);
        if (d3(qg, P) < d3(qe, P)) { u = gu; v = gv; }
    }
    *uo = u; *vo = v; return 1;
}

/* eval_surf (mccore 0x18047ba50, derivative order 2 in dyno_surf) + calc_surf_unit_norm (0x180164290): position, Su, Sv, unit normal Su x Sv at (u, v) of the base; used by dyno_surf (cui_dsurf.c). Analytic for NURBS, numeric otherwise (pm_du / pm_dv, ours). */
int mcsc_surf_frame(const McamFile *f, const Surf *s, double u, double v, double P[3], double Su[3], double Sv[3], double N[3]) {
    PM m; pm_init(&m, f, s); if (!(m.d[1] > m.d[0]) || !(m.d[3] > m.d[2])) return 0;
    pm_eval(&m, u, v, P, Su, Sv); cross3(Su, Sv, N); unit3(N); return 1;
}

/* ---- Curve One Edge / All Edges */
/* ---- a trimmed surface's edge curves: MaFace::MaFace (0x180413ba0) -> MaLoop(surface, polyline, surf_step_chord_dev, break angle) (0x18042eb50), MaLoop::GetEdgeCrvs(false), MaParameterCurve::GetEdgeSplineNoFit (0x180465b80). All in MaSurface's (u, v): a NURBS base's knots normalised to 0 .. 1 (DB constructor 0x180455dd0; MaPolyLine::UVTransform 0x1804754e0 maps the stored points with the same linear map). */
typedef struct { PM m; double k0[2], ks[2], d[4], ek0[2], eks[2]; int rep; Surf bc; uint8_t *brec; double *oldU[2], *newU[2]; int nU[2]; } FE;
typedef struct { int n; double (*p)[2]; } FP;
static void fe_eval(const FE *e, const double uv[2], double P[3]) { pm_raw(&e->m, e->ek0[0] + uv[0] * e->eks[0], e->ek0[1] + uv[1] * e->eks[1], P); }
/* the tangent in model space along (du, dv) at (u, v), direction only (MaCurveOnSurface::Evaluate(1, t): Su du / dt + Sv dv / dt) */
static void fe_dir(const FE *e, const double uv[2], const double d[2], double T[3]) {
    double su[3], sv[3], P[3]; pm_eval(&e->m, e->ek0[0] + uv[0] * e->eks[0], e->ek0[1] + uv[1] * e->eks[1], P, su, sv);
    for (int k = 0; k < 3; k++) T[k] = su[k] * e->eks[0] * d[0] + sv[k] * e->eks[1] * d[1]; unit3(T);
}
/* MaNURBSurf's DB constructor (0x180455dd0): a maximum knot multiplicity >= the degree and (not rational, or a degree below 3): this[0x1c]: LoadUntrimmedSurf(.., true) regenerates the knots of each such direction (bit 0 U, bit 1 V) and MaFace converts the points (ConvertUVParams 0x18046ea70). */
static int fe_reparam(const Surf *b) {
    if (b->kind != 0) return 0;
    const uint8_t *r = b->rec; int nku = rd32(r + 0x100), nkv = rd32(r + 0x104), du = r[0x111], dv = r[0x112], rat = !(r[0x113] & 1); const double *KU = (const double *)(r + 0x118), *KV = KU + nku;
    int mu = 0, mv = 0;
    for (int i = du + 1; i < nku - du - 1; ) { int j = i; while (j + 1 < nku - du - 1 && KU[j + 1] == KU[i]) j++; if (j - i + 1 > mu) mu = j - i + 1; i = j + 1; }
    for (int i = dv + 1; i < nkv - dv - 1; ) { int j = i; while (j + 1 < nkv - dv - 1 && KV[j + 1] == KV[i]) j++; if (j - i + 1 > mv) mv = j - i + 1; i = j + 1; }
    if (!((du <= mu || dv <= mv) && (!rat || du < 3 || dv < 3))) return 0;
    return (du <= mu ? 1 : 0) | (dv <= mv ? 2 : 0);
}
/* MaNURBSurf::RegenNonUniformUKnotsFromDerivRatios 0x18045ea10 / V: for each unique knot of the other direction the iso curve's MaNURBSpline::RegenNonUniformKnotsFromDerivRatios 0x180452940 (each interior unique knot: the ratio of the first Bezier leg of the span after it to the last Bezier leg of the span before it (the legs below 1e-12 counted as 1), cumulated and normalised), averaged over the isos; the multiplicities kept */
static int uniq(const double *K, int nk, double *U) { int n = 0; for (int i = 0; i < nk; i++) if (n == 0 || K[i] != U[n - 1]) U[n++] = K[i]; return n; }
/* MaNURBSpline::GetUniqueKnots (0x18044eaf0): the distinct knots of the range only: K[degree], then K[order] .. K[nk - order - 1], then K[nk - order] (an unclamped knot vector's outer knots are not in the list) */
static int uniq_dom(const double *K, int nk, int deg, double *U) { int o = deg + 1, n = 1; double v = K[deg]; U[0] = v; for (int i = o; i < nk - o; i++) if (K[i] != v) { U[n++] = K[i]; v = K[i]; } U[n++] = K[nk - o]; return n; }
static void fe_regen(const Surf *b, int dir, double *K_new, double *U_old, double *U_new, int *nuq) {
    const uint8_t *r = b->rec; int nku = rd32(r + 0x100), nkv = rd32(r + 0x104); const double *KU = (const double *)(r + 0x118), *KV = KU + nku;
    const double *Ka = dir == 0 ? KU : KV, *Kb = dir == 0 ? KV : KU; int nka = dir == 0 ? nku : nkv, nkb = dir == 0 ? nkv : nku, deg = dir == 0 ? r[0x111] : r[0x112];
    double *Ub = malloc(sizeof(double) * nkb), *U = malloc(sizeof(double) * nka), *t = malloc(sizeof(double) * nka), *sum = calloc(nka, sizeof(double)); int nb = uniq_dom(Kb, nkb, dir == 0 ? r[0x112] : r[0x111], Ub), n = uniq_dom(Ka, nka, deg, U);
    double ka0 = Ka[0], kas = Ka[nka - 1] - Ka[0];
    for (int iv = 0; iv < nb; iv++) {
        NC c = { 0 }; surf_iso(b, dir == 0 ? 2 : 1, Ub[iv], &c);
        t[0] = 0; t[1] = 1; double sp = 1;
        for (int j = 1; j + 1 < n; j++) {
            /* MaCurve::Evaluate leaves the span's Bezier points in the table (+0x158, ConvertSegmentToBezier): the last leg of the span before the knot, the first of the span after it, each point divided by its weight */
            NC bl = { 0 }, br = { 0 }; nc_sub(&c, U[j - 1], U[j], &bl); nc_sub(&c, U[j], U[j + 1], &br);
            double a[3], e[3]; for (int k = 0; k < 3; k++) { a[k] = bl.p[4 * deg + 1 + k] / bl.p[4 * deg] - bl.p[4 * (deg - 1) + 1 + k] / bl.p[4 * (deg - 1)]; e[k] = br.p[4 + 1 + k] / br.p[4] - br.p[1 + k] / br.p[0]; } nc_free(&bl); nc_free(&br);
            double la = sqrt(dot3(a, a)), lr = sqrt(dot3(e, e)); if (la < 1e-12) la = 1; if (lr < 1e-12) lr = 1;
            sp *= lr / la; t[j + 1] = t[j] + sp;
        }
        if (n > 2) for (int j = 1; j < n; j++) t[j] /= t[n - 1];
        for (int j = 0; j < n; j++) sum[j] += t[j];
        nc_free(&c);
    }
    for (int j = 0; j < n; j++) { U_new[j] = sum[j] / nb; U_old[j] = (U[j] - ka0) / kas; } U_new[0] = 0; U_new[n - 1] = 1; *nuq = n;
    /* RegenNonUniformUKnotsFromDerivRatios ma_classes.c:67864-67917: the first order knots 0, each interior unique knot repeated by its multiplicity, the last order knots 1 */
    int m = 0; for (int i = 0; i <= deg; i++) K_new[m++] = 0;
    for (int j = 1; j + 1 < n; j++) { int mu = 0; for (int i = 0; i < nka; i++) if (Ka[i] == U[j]) mu++; for (int q = 0; q < mu && m < nka; q++) K_new[m++] = U_new[j]; }
    for (int i = nka - deg - 1; i < nka; i++) K_new[i] = 1;
    free(Ub); free(U); free(t); free(sum);
}
/* MaNURBSurf::GlobalUVToLocalUV 0x18045cc40 + LocalUVToGlobalUV (ConvertUVParams): the span of a value in the unique knots, the same fraction of the new span */
static double fe_map(const double *U, const double *V, int n, double x) { int i = 1; while (i < n && !(x <= U[i])) i++; if (i == n) i--; double d = U[i] - U[i - 1]; if (d < 1e-12) d = 1e-12; return V[i - 1] + (x - U[i - 1]) / d * (V[i] - V[i - 1]); }
static void fe_init(FE *e, const McamFile *f, const Surf *s) {
    memset(e, 0, sizeof *e);
    pm_init(&e->m, f, s); const Surf *b = e->m.s; e->k0[0] = e->k0[1] = 0; e->ks[0] = e->ks[1] = 1;
    if (b->kind == 0) { const uint8_t *r = b->rec; int nku = rd32(r + 0x100), nkv = rd32(r + 0x104); const double *KU = (const double *)(r + 0x118), *KV = KU + nku; e->k0[0] = KU[0]; e->ks[0] = KU[nku - 1] - KU[0]; e->k0[1] = KV[0]; e->ks[1] = KV[nkv - 1] - KV[0]; }
    for (int i = 0; i < 2; i++) { e->d[2 * i] = (e->m.d[2 * i] - e->k0[i]) / e->ks[i]; e->d[2 * i + 1] = (e->m.d[2 * i + 1] - e->k0[i]) / e->ks[i]; e->ek0[i] = e->k0[i]; e->eks[i] = e->ks[i]; }
    e->rep = b->kind == 0 ? fe_reparam(b) : 0;
    if (e->rep) {   /* the regenerated copy of the base: its knots, normalised; the range mapped; PM on the copy */
        const uint8_t *r = b->rec; int nk[2] = { rd32(r + 0x100), rd32(r + 0x104) }; e->brec = malloc(b->n); memcpy(e->brec, r, b->n); e->bc = *b; e->bc.rec = e->brec;
        double *KU = (double *)(e->brec + 0x118), *KV = KU + nk[0];
        for (int i = 0; i < nk[0]; i++) KU[i] = (KU[i] - e->k0[0]) / e->ks[0]; for (int i = 0; i < nk[1]; i++) KV[i] = (KV[i] - e->k0[1]) / e->ks[1];
        for (int i = 0; i < 2; i++) { e->oldU[i] = malloc(sizeof(double) * nk[i]); e->newU[i] = malloc(sizeof(double) * nk[i]); e->nU[i] = uniq(i ? (const double *)((const uint8_t *)r + 0x118) + nk[0] : (const double *)(r + 0x118), nk[i], e->oldU[i]);
            double k0 = i ? ((const double *)(r + 0x118))[nk[0]] : ((const double *)(r + 0x118))[0], ks = i ? e->ks[1] : e->ks[0]; for (int j = 0; j < e->nU[i]; j++) { e->oldU[i][j] = (e->oldU[i][j] - k0) / ks; e->newU[i][j] = e->oldU[i][j]; } }
        for (int i = 0; i < 2; i++) if (e->rep & (1 << i)) { double *K = i ? KV : KU; fe_regen(b, i, K, e->oldU[i], e->newU[i], &e->nU[i]); }
        double nd[4]; for (int i = 0; i < 2; i++) { nd[2 * i] = fe_map(e->oldU[i], e->newU[i], e->nU[i], e->d[2 * i]); nd[2 * i + 1] = fe_map(e->oldU[i], e->newU[i], e->nU[i], e->d[2 * i + 1]); }
        wdd(e->brec + 0x70, nd[0]); wdd(e->brec + 0x78, nd[1]); wdd(e->brec + 0x80, nd[2]); wdd(e->brec + 0x88, nd[3]);
        pm_init(&e->m, f, &e->bc); memcpy(e->d, nd, sizeof nd); for (int i = 0; i < 2; i++) { e->ek0[i] = 0; e->eks[i] = 1; }
    }
}
static void fe_free(FE *e) { free(e->brec); for (int i = 0; i < 2; i++) { free(e->oldU[i]); free(e->newU[i]); } }
/* a stored (u, v) in the surface's own terms: unchanged, or (regenerated surface) normalised and mapped as MaPolyLine::UVTransform / ConvertUVParams do (MaFace::MaFace ma_classes.c:29489) */
static void fe_uv_to_surf(const FE *e, double uv[2]) { if (!e->rep) return; uv[0] = fe_map(e->oldU[0], e->newU[0], e->nU[0], (uv[0] - e->k0[0]) / e->ks[0]); uv[1] = fe_map(e->oldU[1], e->newU[1], e->nU[1], (uv[1] - e->k0[1]) / e->ks[1]); }
/* the file's (u, v) of the arrow as MaSurface's (u, v): eval_surf, then MaSurface::PointMinDist on the regenerated surface (core1.c:56-63, :208-215); unchanged when no regeneration */
static void fe_arrow(const FE *e, const McamFile *f, const Surf *raw, double *u, double *v) { if (!e->rep) return; double P[3]; if (mcs_surf_eval(f, raw, *u, *v, P)) return; mcsc_uv_of_point(f, &e->bc, P, u, v); }
/* a trimmed surface's loops: formats/mcam.md "Trimmed surface" (uv polyline points, +0x10c points, +0x128 loops, +0x138 data); the loop polyline
   MaLoop::GetLoopPolyLine is these points [the MaLoop rebuild from a polyline is taken as the stored points] */
static int trim_iso_points(const FE *e, const Surf *s, int dir, double par, double **out, int *npair) {
    const uint8_t *r = s->rec; if (s->n < 0x138) return 0;
    int m = rd32(r + 0x10c), nl = rd32(r + 0x128); if (m <= 0 || nl <= 0 || 0x138 + 16 * (size_t)m + 8 * (size_t)nl > s->n) return 0;
    const uint8_t *t = r + 0x138; double *h = NULL; int nh = 0, ch = 0;
    double dup = 1e-10;
    for (int l = 0; l < nl; l++) {
        int st = rd32(t + 16 * (size_t)m + 8 * l), cn = rd32(t + 16 * (size_t)m + 8 * l + 4);
        /* MaLoop::GetLoopPolyLine (0x1804322d0 -> MakeLoopPolyLine 0x1804331e0): the loop's parameter curves' polylines (MaParameterCurve +0x138: each built from the stored uv points)
           joined, the last point the first. MaPolyLine::PlaneIntersection (0x180471cd0) on the line u or v = par: every segment with d0 d1 <= 0 (a closed polyline's first segment
           is skipped when its start is on the line - the last segment has it), the segment after one ending exactly on the line is skipped */
        int nv = cn + 1; double (*P)[2] = malloc(sizeof(double[2]) * (nv + 1));
        for (int i = 0; i < nv; i++) { memcpy(P[i], t + 16 * (size_t)(st + i), 16); fe_uv_to_surf(e, P[i]); }
        memcpy(P[nv], P[0], 16);
        int seg = 1;
        while (seg <= nv) {
            double d0 = (dir == 1 ? P[seg - 1][0] : P[seg - 1][1]) - par, d1 = (dir == 1 ? P[seg][0] : P[seg][1]) - par;
            int skip = seg == 1 && d0 == 0.0;
            if (!skip && d0 * d1 <= 0.0 && fabs(d0 - d1) > 1e-20) {
                double f = d0 / (d0 - d1), u = P[seg - 1][0] + (P[seg][0] - P[seg - 1][0]) * f, v = P[seg - 1][1] + (P[seg][1] - P[seg - 1][1]) * f;
                int dupf = 0;
                for (int k = 0; k < nh; k++) { double du = h[2 * k] - u, dv = h[2 * k + 1] - v; if (sqrt(du * du + dv * dv) <= dup) { dupf = 1; break; } }
                if (!dupf) { if (nh == ch) { ch = ch ? ch * 2 : 16; h = realloc(h, sizeof(double) * 2 * ch); } h[2 * nh] = u; h[2 * nh + 1] = v; nh++; }
                if (d1 == 0.0) seg++;
            }
            seg++;
        }
        free(P);
    }
    if (nh < 2) { free(h); return 0; }
    /* MaPolyLine::Sort2DPoints(pts, n, dir == 1): along the free coordinate */
    for (int i = 1; i < nh; i++) { double ku = h[2 * i], kv = h[2 * i + 1], key = dir == 1 ? kv : ku; int j = i - 1;
        while (j >= 0 && (dir == 1 ? h[2 * j + 1] : h[2 * j]) > key) { h[2 * (j + 1)] = h[2 * j]; h[2 * (j + 1) + 1] = h[2 * j + 1]; j--; }
        h[2 * (j + 1)] = ku; h[2 * (j + 1) + 1] = kv; }
    if (nh & 1) nh--;
    *npair = nh / 2;
    double *o = malloc(sizeof(double) * nh);
    for (int i = 0; i < nh; i++) o[i] = dir == 1 ? h[2 * i + 1] : h[2 * i];
    free(h); *out = o; return 1;
}

/* MaSurface::IsoCurve (0x1804876f0) for a NURBS base: the untrimmed curve, or one piece per pair of loop crossings. LoadSurf(.., true, ..) (CreateConstantParameterCurves core1.c:55, CreateSurfaceFlowLines core2.c:43, MakeSurfaceEdgeCurve :207, MakeAllSurfaceEdgeCurves core3.c:48)
   makes the regenerated surface when the knots ask for it (MaNURBSurf ctor ma_classes.c:62400 / 62438 / 62485, fe_reparam): then the iso is taken on e->bc and par and the loops are in its parameters. */
static int iso_pieces(const FE *e, const Surf *s, int dir, double par, NC *out, int cap) {
    NC full = { 0 }; surf_iso(e->m.s, dir, par, &full);
    /* MaNURBSurf (the DB constructor 0x180455dd0 :150-230) works on knots normalised to 0 .. 1, so the iso curve's knots and range are normalised, and Flowline's result has them: the record written carries the normalised values */
    double k0 = full.k[0], ks = full.k[full.nk - 1] - k0; if (!(ks > 0)) ks = 1;
    for (int i = 0; i < full.nk; i++) full.k[i] = (full.k[i] - k0) / ks;
    full.t1 = (full.t1 - k0) / ks; full.t2 = (full.t2 - k0) / ks;
    int n = 0;
    if (s->kind == 10) {
        double *pt = NULL; int np = 0;
        if (trim_iso_points(e, s, dir, par, &pt, &np)) {
            for (int i = 0; i < np && n < cap; i++) {
                double a = pt[2 * i], bb = pt[2 * i + 1];
                a = (a - k0) / ks; bb = (bb - k0) / ks; nc_sub(&full, a < bb ? a : bb, a < bb ? bb : a, &out[n]); n++;
            }
            free(pt);
        }
    } else {
        if (full.t1 > full.k[full.deg] + 1e-12 || full.t2 < full.k[full.ncp] - 1e-12) nc_sub(&full, full.t1, full.t2, &out[n++]);
        else { out[n++] = full; full.k = NULL; full.p = NULL; }
    }
    free(full.k); free(full.p);
    return n;
}
/* MaSurface::BoundaryClosestToUVPoint (0x1804800a0): U0, U1, V0, V1, a degenerate boundary skipped; the first of equal distances; 1 .. 4 (1 with distance 0 when all are degenerate) */
static void fe_boundary(const FE *e, double u, double v, int *which, double *dist) {
    *which = 1; *dist = 0; double best = 1e31;
    for (int i = 0; i < 4; i++) { if (e->m.deg[i]) continue;
        double a0 = i < 2 ? e->d[i] : e->d[0], b0 = i < 2 ? e->d[2] : e->d[i], a1 = i < 2 ? a0 : e->d[1], b1 = i < 2 ? e->d[3] : b0, ex = a1 - a0, ey = b1 - b0, l2 = ex * ex + ey * ey, t = l2 > 0 ? ((u - a0) * ex + (v - b0) * ey) / l2 : 0; if (t < 0) t = 0; if (t > 1) t = 1;
        double d = hypot(u - a0 - ex * t, v - b0 - ey * t); if (d < best) { best = d; *dist = d; *which = i + 1; } }
}
/* MaSurface::IsUVOnEdge 0x180487340: within 5e-5 of the range of the nearest boundary; IsUVCorner 0x180486bf0: exact corners */
static int fe_on_edge(const FE *e, double u, double v) { int w; double d; fe_boundary(e, u, v, &w, &d); return w < 3 ? d < (e->d[1] - e->d[0]) * 5e-05 : d < (e->d[3] - e->d[2]) * 5e-05; }
static int fe_corner(const FE *e, double u, double v) { return (u == e->d[0] || u == e->d[1]) && (v == e->d[2] || v == e->d[3]); }
/* MaPolyLine::RefineByChordHeightToSurface 0x180472a00: a span whose points at 1/4, 1/3, 1/2, 2/3, 3/4 of (u, v) leave its 3D chord by more than tol is halved (the middle in (u, v)); capacity max(5000, 2 n) */
static void fe_refine(const FE *e, FP *L, double tol) {
    int cap = L->n * 2 > 5000 ? L->n * 2 : 5000; double (*Q)[2] = malloc(sizeof(double[2]) * cap); memcpy(Q, L->p, sizeof(double[2]) * L->n); int n = L->n;
    static const double F[5] = { 0.25, 0.3333333333333333, 0.5, 0.6666666666666666, 0.75 };
    for (int i = 0; i < n - 1; ) {
        double d[2] = { Q[i + 1][0] - Q[i][0], Q[i + 1][1] - Q[i][1] }, Pa[3], Pb[3], t[3]; fe_eval(e, Q[i], Pa); fe_eval(e, Q[i + 1], Pb);
        for (int k = 0; k < 3; k++) t[k] = Pb[k] - Pa[k]; unit3(t); double tl = sqrt(dot3(t, t)); int bad = 0;
        for (int k = 0; k < 5 && !bad; k++) { double q[2] = { Q[i][0] + F[k] * d[0], Q[i][1] + F[k] * d[1] }, Pq[3], w[3], c[3], dist; fe_eval(e, q, Pq); for (int m = 0; m < 3; m++) w[m] = Pq[m] - Pa[m];
            if (tl < 1e-05) dist = sqrt(dot3(w, w)); else { cross3(w, t, c); dist = sqrt(dot3(c, c)); } if (tol < dist) bad = 1; }
        if (!bad) { i++; continue; }
        if (n >= cap) break;
        memmove(Q + i + 2, Q + i + 1, sizeof(double[2]) * (n - i - 1)); Q[i + 1][0] = Q[i][0] + d[0] * 0.5; Q[i + 1][1] = Q[i][1] + d[1] * 0.5; n++;
    }
    if (n > L->n) { free(L->p); L->p = Q; L->n = n; } else free(Q);
}
static void fp_add(FP **o, int *no, const double (*p)[2], int n) { *o = realloc(*o, sizeof(FP) * (*no + 1)); FP *x = &(*o)[(*no)++]; x->n = n; x->p = malloc(sizeof(double[2]) * n); memcpy(x->p, p, sizeof(double[2]) * n); }
/* MaPolyLine::IsLinear 0x1804705f0: consecutive unit directions, |cross| <= 1e-6 */
static int fp_linear(const FP *L) {
    for (int i = 2; i < L->n; i++) { double a[2] = { L->p[i - 1][0] - L->p[i - 2][0], L->p[i - 1][1] - L->p[i - 2][1] }, b[2] = { L->p[i][0] - L->p[i - 1][0], L->p[i][1] - L->p[i - 1][1] }, la = hypot(a[0], a[1]), lb = hypot(b[0], b[1]);
        if (la != 0) { a[0] /= la; a[1] /= la; } else a[0] = a[1] = 0; if (lb != 0) { b[0] /= lb; b[1] /= lb; } else b[0] = b[1] = 0; if (1e-06 < fabs(b[1] * a[0] - a[1] * b[0])) return 0; }
    return 1;
}
/* MaPolyLine::JoinsSmoothly 0x180470990 (the end of a, the start of b): 3D ends within 1e-4, the 3D chords' directions within the angle */
static int fp_join(const FE *e, const FP *a, const FP *b, double ang) {
    double p0[3], p1[3], q0[3], q1[3], u[3], v[3], g[3];
    fe_eval(e, a->p[a->n - 1], p1); fe_eval(e, a->p[a->n - 2], p0); fe_eval(e, b->p[1], q1); fe_eval(e, b->p[0], q0);
    for (int k = 0; k < 3; k++) { u[k] = p1[k] - p0[k]; v[k] = q1[k] - q0[k]; g[k] = q0[k] - p1[k]; } unit3(u); unit3(v);
    return sqrt(dot3(g, g)) <= 0.0001 && cos(ang * 0.017453292519943295) < dot3(u, v);
}
/* MaPolyLine::BreakAtAngleAndSrfBoundary 0x18046d960; a closed loop's last and first pieces joined when they meet smoothly within 20 degrees and neither is linear (Concatenate 0x18046e940) */
static void fe_break(const FE *e, const FP *L, double ang, FP **o, int *no) {
    double cs = cos(ang * 0.017453292519943295); int n = L->n, st = 0, i = 1; const double (*P)[2] = (const double (*)[2])L->p;
    for (;;) {
        double A[3], B[3], C[3], d1[3], d2[3]; fe_eval(e, P[i - 1], A); fe_eval(e, P[i], B); for (int k = 0; k < 3; k++) d1[k] = B[k] - A[k]; unit3(d1);
        int on1 = fe_on_edge(e, (P[i - 1][0] + P[i][0]) * 0.5, (P[i - 1][1] + P[i][1]) * 0.5), crn = fe_corner(e, P[i][0], P[i][1]);
        if (n - 1 <= i) {
            int cnt = i - st + 1; double dd = hypot(P[st][0] - P[st + 1][0], P[st][1] - P[st + 1][1]);
            if (cnt != 2 || ((e->d[1] - e->d[0]) * 1e-06 <= dd && (e->d[3] - e->d[2]) * 1e-06 <= dd)) fp_add(o, no, P + st, cnt);
            if (*no >= 2) { FP *f = &(*o)[0], *l = &(*o)[*no - 1];
                if (fp_join(e, l, f, 20.0) && !fp_linear(l) && !fp_linear(f)) { int m = l->n + f->n - 1; l->p = realloc(l->p, sizeof(double[2]) * m); memcpy(l->p + l->n, f->p + 1, sizeof(double[2]) * (f->n - 1)); l->n = m; free(f->p); memmove(f, f + 1, sizeof(FP) * (*no - 1)); (*no)--; } }
            return;
        }
        fe_eval(e, P[i + 1], C); for (int k = 0; k < 3; k++) d2[k] = C[k] - B[k]; unit3(d2);
        int on2 = fe_on_edge(e, (P[i][0] + P[i + 1][0]) * 0.5, (P[i][1] + P[i + 1][1]) * 0.5);
        if (dot3(d1, d2) < cs || on1 != on2 || crn) { fp_add(o, no, P + st, i - st + 1); st = i; }
        i++;
    }
}
/* MaLoop::ConcatenateAllSmoothlyJoinedPCurves 0x1804308c0 with MaParameterCurve::JoinsSmoothly(.., 5 degrees) 0x180466d60: the surface tangents of the one-sided (u, v) directions at the join, the ends within 1e-4, neither end a corner */
static int fp_pjoin(const FE *e, const FP *a, const FP *b) {
    const double *p1 = a->p[a->n - 1], *p0 = a->p[a->n - 2], *q0 = b->p[0], *q1 = b->p[1]; double da[2] = { p1[0] - p0[0], p1[1] - p0[1] }, db[2] = { q1[0] - q0[0], q1[1] - q0[1] }, Pa[3], Pb[3], g[3], Ta[3], Tb[3];
    fe_eval(e, p1, Pa); fe_eval(e, q0, Pb); for (int k = 0; k < 3; k++) g[k] = Pb[k] - Pa[k];
    if (sqrt(dot3(g, g)) > 0.0001 || fe_corner(e, p1[0], p1[1]) || fe_corner(e, q0[0], q0[1])) return 0;
    fe_dir(e, p1, da, Ta); fe_dir(e, q0, db, Tb); return cos(5.0 * 0.017453292519943295) < dot3(Ta, Tb);
}
static void fe_concat(const FE *e, FP **o, int *no) {
    if (*no < 2) return;
    FP *P = *o, *R = malloc(sizeof(FP) * *no); int n = *no, nr = 0; FP cur = P[0];
    for (int i = 1; i < n; i++) { if (fp_pjoin(e, &cur, &P[i])) { int m = cur.n + P[i].n - 1; cur.p = realloc(cur.p, sizeof(double[2]) * m); memcpy(cur.p + cur.n, P[i].p + 1, sizeof(double[2]) * (P[i].n - 1)); cur.n = m; free(P[i].p); } else { R[nr++] = cur; cur = P[i]; } }
    R[nr++] = cur; free(P); *o = R; *no = nr;
}
/* CreateConstantParameterCurves (mccore 0x18023c8e0): the iso curve at the picked (u, v): Direction U (local_2e50 0: param_4 != 0, IsoCurve(1, u)), V (1:
   IsoCurve(2, v)), Both (2: param_5 set: then the other). Each piece: Simplify or MakeSplineOrNURBCurve (emit). */
int mcsc_const_param(McamFile *f, const Surf *s, double u, double v, int direction, double tol, double systol, int level, int color, int nurbs_wanted, int *out, int cap, int *nmade, char *msg, size_t msgn) {
    want_nurbs = nurbs_wanted; closed_tol = mcam_is_metric(f) ? 0.0025 : 0.0001;
    const char *why = NULL; const Surf *b = iso_base(f, s, &why);
    if (!b) { snprintf(msg, msgn, "Curve Constant Parameter: %s", why); return 0; }
    int isos[2], ni = 0;
    if (direction != 1) isos[ni++] = 1;   /* U or Both: IsoCurve(1, u) */
    if (direction == 1) isos[ni++] = 2;   /* V */
    if (direction == 2) isos[ni++] = 2;   /* Both: then IsoCurve(2, v) */
    *nmade = 0; FE e; fe_init(&e, f, s); fe_arrow(&e, f, b, &u, &v);
    for (int i = 0; i < ni; i++) {
        NC pcs[64]; memset(pcs, 0, sizeof pcs);
        int np = iso_pieces(&e, s, isos[i], isos[i] == 1 ? u : v, pcs, 64);
        for (int k = 0; k < np; k++) {
            /* MaCurve::IsDegenerate (0x180404570; the MakeMCEnt tests): a curve whose middle derivative is <= tol: nothing */
            double mp[3], md[3], tm = (pcs[k].t1 + pcs[k].t2) * 0.5; nc_deriv(&pcs[k], tm, mp, md);
            if (sqrt(dot3(md, md)) > systol) if (!emit(f, &pcs[k], tol, systol, level, color, nmade, cap, out, &why)) { snprintf(msg, msgn, "Curve Constant Parameter: %s", why); nc_free(&pcs[k]); for (int q = k + 1; q < np; q++) nc_free(&pcs[q]); fe_free(&e); return 0; }
            nc_free(&pcs[k]);
        }
    }
    fe_free(&e);
    if (*nmade == 0) snprintf(msg, msgn, "Curve Constant Parameter: no curve at this parameter");
    return *nmade > 0;
}

/* the pieces of one stored loop: the points with the first repeated (MaFace 0x180413ba0), then MaLoop(surface, polyline, chord, angle) */
static int fe_loop(const FE *e, const Surf *s, int l, double chord, double ang, FP **o) {
    const uint8_t *r = s->rec; int m = rd32(r + 0x10c), nl = rd32(r + 0x128); const uint8_t *t = r + 0x138; *o = NULL; int no = 0; (void)nl;
    int st = rd32(t + 16 * (size_t)m + 8 * l), cn = rd32(t + 16 * (size_t)m + 8 * l + 4);
    if (l > 0 && cn + 1 == 1) return 0;   /* MaFace: an inner loop of one point is dropped */
    FP L; L.n = cn + 2; L.p = malloc(sizeof(double[2]) * L.n);
    for (int i = 0; i < L.n; i++) { double v[2]; memcpy(v, t + 16 * (size_t)(st + (i <= cn ? i : 0)), 16); L.p[i][0] = (v[0] - e->k0[0]) / e->ks[0]; L.p[i][1] = (v[1] - e->k0[1]) / e->ks[1];
        if (e->rep) { L.p[i][0] = fe_map(e->oldU[0], e->newU[0], e->nU[0], L.p[i][0]); L.p[i][1] = fe_map(e->oldU[1], e->newU[1], e->nU[1], L.p[i][1]); } }
    fe_refine(e, &L, chord); fe_break(e, &L, ang, o, &no); fe_concat(e, o, &no); free(L.p);
    /* MaLoop::CheckForClosure 0x180430210 (with a valid loop): a last end within 0.001 of the first start in (u, v), and nearer to it than the last piece's middle: the end moved onto the start */
    if (no > 0) { FP *f0 = &(*o)[0], *l0 = &(*o)[no - 1]; double *e1 = l0->p[l0->n - 1], gap = hypot(e1[0] - f0->p[0][0], e1[1] - f0->p[0][1]), tot = 0, h = 0, mid[2]; for (int i = 0; i + 1 < l0->n; i++) tot += hypot(l0->p[i + 1][0] - l0->p[i][0], l0->p[i + 1][1] - l0->p[i][1]);
        memcpy(mid, l0->p[0], sizeof mid); for (int i = 0; i + 1 < l0->n; i++) { double sl = hypot(l0->p[i + 1][0] - l0->p[i][0], l0->p[i + 1][1] - l0->p[i][1]); if (h + sl >= tot * 0.5) { double fr = sl > 0 ? (tot * 0.5 - h) / sl : 0; mid[0] = l0->p[i][0] + fr * (l0->p[i + 1][0] - l0->p[i][0]); mid[1] = l0->p[i][1] + fr * (l0->p[i + 1][1] - l0->p[i][1]); break; } h += sl; }
        if (0.0 < gap && gap < 0.001 && gap < hypot(mid[0] - f0->p[0][0], mid[1] - f0->p[0][1])) { e1[0] = f0->p[0][0]; e1[1] = f0->p[0][1]; } }
    return no;
}
/* one piece: MaPolyLine -> MaParameterCurve (GenPiecewiseLinearPCurve 0x180465290) -> GetEdgeSplineNoFit: the surface points at the knots, MaNURBSpline(points, n, 1, true, false) = CrvThroughVertices 0x180445eb0 + RegenNonUniformKnotsFromDerivRatios 0x180452940 (each knot span proportional to the 3D chord, a chord below 1e-12 counted as 1; normalised) */
static void fe_curve(const FE *e, const FP *L, NC *c) {
    memset(c, 0, sizeof *c); c->deg = 1; c->ncp = L->n; c->nk = L->n + 2; c->rat = 0; c->t1 = 0; c->t2 = 1;
    c->k = malloc(sizeof(double) * c->nk); c->p = malloc(sizeof(double) * 4 * c->ncp);
    for (int i = 0; i < L->n; i++) { c->p[4 * i] = 1; fe_eval(e, L->p[i], c->p + 4 * i + 1); }
    double *t = malloc(sizeof(double) * L->n), sp = 1, lp = 0; t[0] = 0; t[1] = 1;
    for (int i = 1; i + 1 < L->n; i++) {
        double a[3] = { c->p[4 * i + 1] - c->p[4 * (i - 1) + 1], c->p[4 * i + 2] - c->p[4 * (i - 1) + 2], c->p[4 * i + 3] - c->p[4 * (i - 1) + 3] }, b[3] = { c->p[4 * (i + 1) + 1] - c->p[4 * i + 1], c->p[4 * (i + 1) + 2] - c->p[4 * i + 2], c->p[4 * (i + 1) + 3] - c->p[4 * i + 3] };
        double la = sqrt(dot3(a, a)), lb = sqrt(dot3(b, b)); if (la < 1e-12) la = 1; if (lb < 1e-12) lb = 1; sp *= lb / la; t[i + 1] = t[i] + sp; }
    (void)lp; double T = t[L->n - 1]; c->k[0] = c->k[1] = 0; for (int i = 1; i < L->n - 1; i++) c->k[i + 1] = t[i] / T; c->k[c->nk - 1] = c->k[c->nk - 2] = 1; free(t);
}
static int fe_emit(McamFile *f, const FE *e, const FP *L, double tol, double systol, int level, int color, int *out, int cap, int *nmade, const char **why) {
    NC c; fe_curve(e, L, &c); emit_piecewise = c.ncp > 2; int ok = emit(f, &c, tol, systol, level, color, nmade, cap, out, why); emit_piecewise = 0; nc_free(&c); return ok;
}
/* MaFace::GetEdgeCrvs 0x180415ab0: the outer loop's pieces, then each inner loop's */
static int fe_all(McamFile *f, const Surf *s, double chord, double ang, double tol, double systol, int level, int color, int *out, int cap, int *nmade, const char **why) {
    FE e; fe_init(&e, f, s); int nl = rd32(s->rec + 0x128);
    for (int l = 0; l < nl; l++) { FP *P = NULL; int n = fe_loop(&e, s, l, chord, ang, &P); for (int i = 0; i < n; i++) { if (!fe_emit(f, &e, &P[i], tol, systol, level, color, out, cap, nmade, why)) { fe_free(&e); return 0; } free(P[i].p); } free(P); }
    fe_free(&e); return 1;
}
/* the distance from (u, v) to a piece (MaPolyLine::PointMinDist over the pcurve's facets, MaLoop::PointMinDist 0x180433b00) */
static double fp_dist(const FP *L, double u, double v) { double best = 1e31; for (int i = 0; i + 1 < L->n; i++) { double a0 = L->p[i][0], b0 = L->p[i][1], ex = L->p[i + 1][0] - a0, ey = L->p[i + 1][1] - b0, l2 = ex * ex + ey * ey, t = l2 > 0 ? ((u - a0) * ex + (v - b0) * ey) / l2 : 0; if (t < 0) t = 0; if (t > 1) t = 1; double d = hypot(u - a0 - ex * t, v - b0 - ey * t); if (d < best) best = d; } return best; }
/* MaSurface::FindClosestEdgeToUVPoint 0x180483d20 on a trimmed surface: the outer loop's nearest piece (the last of equal distances: MaLoop::PointMinDist "<="), an inner loop only when strictly nearer */
static int fe_one(McamFile *f, const Surf *s, double ur, double vr, double chord, double ang, double tol, double systol, int level, int color, int *out, int cap, int *nmade, const char **why) {
    FE e; fe_init(&e, f, s); int nl = rd32(s->rec + 0x128), bl = -1, bi = 0; double u = (ur - e.k0[0]) / e.ks[0], v = (vr - e.k0[1]) / e.ks[1], best = 1e31; FP *bP = NULL; int bn = 0;
    if (e.rep) { u = ur; v = vr; fe_arrow(&e, f, mcs_surf(f, rd64(s->rec + 0x100)), &u, &v); }   /* MakeSurfaceEdgeCurve core1.c:208-217: eval_surf, PointMinDist on the regenerated surface (the pieces are in its parameters) */
    for (int l = 0; l < nl; l++) { FP *P = NULL; int n = fe_loop(&e, s, l, chord, ang, &P); double lb = 1e31; int li = 0; for (int i = 0; i < n; i++) { double d = fp_dist(&P[i], u, v); if (d <= lb) { lb = d; li = i; } }
        if (n && (l == 0 ? lb < 1e31 : lb < best)) { for (int i = 0; i < bn; i++) free(bP[i].p); free(bP); bP = P; bn = n; bi = li; bl = l; best = lb; } else { for (int i = 0; i < n; i++) free(P[i].p); free(P); } }
    int ok = 1; if (bl >= 0) ok = fe_emit(f, &e, &bP[bi], tol, systol, level, color, out, cap, nmade, why);
    for (int i = 0; i < bn; i++) free(bP[i].p); free(bP); fe_free(&e); return ok;
}
/* tests (ours): the pieces of every loop as the --edg oracle prints them */
int mcsc_edg_ours(const McamFile *f, const Surf *s, const char *path, double chord, double ang) {
    FE e; fe_init(&e, f, s); FILE *o = fopen(path, "w"); if (!o) return 0; int nl = rd32(s->rec + 0x128);
    if (e.rep) { const uint8_t *r = e.brec; int nku = rd32(r + 0x100), nkv = rd32(r + 0x104); const double *KU = (const double *)(r + 0x118), *KV = KU + nku; fprintf(o, "regen u"); for (int i = 0; i < nku; i++) fprintf(o, " %.15g", KU[i]); fprintf(o, "\nregen v"); for (int i = 0; i < nkv; i++) fprintf(o, " %.15g", KV[i]); fprintf(o, "\n"); }
    for (int l = 0; l < nl; l++) { FP *P = NULL; int n = fe_loop(&e, s, l, chord, ang, &P); fprintf(o, "loop %d pieces %d\n", l, n);
        for (int i = 0; i < n; i++) { NC c; fe_curve(&e, &P[i], &c); fprintf(o, "curve %d %d\nnurbs ncp %d nk %d order 2\n", l, i, c.ncp, c.nk); for (int k = 0; k < c.nk; k++) fprintf(o, "k %.15g\n", c.k[k]); for (int k = 0; k < c.ncp; k++) fprintf(o, "p %.15g %.15g %.15g 1\n", c.p[4 * k + 1], c.p[4 * k + 2], c.p[4 * k + 3]); nc_free(&c); free(P[i].p); } free(P); }
    fe_free(&e); fclose(o); return 1;
}

/* tests (ours): the case + our pieces of every eligible trimmed surface on a NURBS base (at most 8 per part), and the counts of the others */
void mcsc_edg_batch(const McamFile *f, const char *dir, double ang, double chord, double tol) {
    int total = 0, nurbs = 0, rep = 0, other = 0, w = 0;
    for (int i = 0; i < f->nents; i++) { const Ent *E = &f->ents[i]; if (E->type != E_SURF) continue; const Surf *s = &f->surfs[E->srf]; if (s->kind != 10) continue; total++;
        const Surf *b = s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL; if (!b || b->kind != 0) { other++; continue; } if (fe_reparam(b)) rep++; nurbs++;
        if (w < 8 && nurbs % 3 == 1) { char p[1200], q[1300]; snprintf(p, sizeof p, "%s_e%d.case", dir, i); snprintf(q, sizeof q, "%s.ours", p); if (mcsc_edg_case(f, s, p, ang, chord, tol)) { mcsc_edg_ours(f, s, q, chord, ang); w++; } } }
    printf("      0 edgbatch trimmed %d nurbs %d reparam %d otherbase %d written %d\n", total, nurbs, rep, other, w);
}
/* tests (ours): every stored order 2 NURBS curve (the form of a trimmed edge curve) against the pieces computed here for each trimmed surface, at the two break angles: a piece with the same point count whose points agree within 1e-4 */
void mcsc_edg_scan(const McamFile *f, double chord) {
    static const double A[2] = { 30.0, 1.0 }; int nc = 0, matched = 0, near = 0, tested = 0; double worst = 0;
    for (int ic = 0; ic < f->nents; ic++) { const Ent *E = &f->ents[ic]; if (E->type != E_SPLINE || E->crv < 0 || E->crv >= f->ncrvs) continue; const CurveRec *cr = &f->crvs[E->crv]; if (cr->cls != 0x80 || cr->rec[0xd0] != 1) continue; int ncp = rd32(cr->rec + 0xcc), vals = cr->rec[0xd1], nk = rd32(cr->rec + 0xc8); if (ncp < 3) continue; nc++;
        const uint8_t *q = cr->rec + 0xd8 + 8 * (size_t)nk; double p0[3], pm[3]; for (int k = 0; k < 3; k++) { p0[k] = rdd(q + 8 * ((vals == 4 ? 1 : 0) + k)); pm[k] = rdd(q + 8 * ((size_t)(ncp / 2) * vals + (vals == 4 ? 1 : 0) + k)); }
        int got = 0;
        for (int is = 0; is < f->nsurfs && !got; is++) { const Surf *s = &f->surfs[is]; if (s->off || s->ent < 0 || s->kind != 10) continue; const Surf *b = s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL; if (!b) continue;
            double u, v, qq[3]; if (!mcsc_uv_of_point(f, b, pm, &u, &v) || mcs_surf_eval(f, b, u, v, qq) || d3(qq, pm) > 1e-3) continue; tested++;
            FE e; fe_init(&e, f, s); int nl = rd32(s->rec + 0x128);
            for (int a = 0; a < 2 && !got; a++) for (int l = 0; l < nl && !got; l++) { FP *P = NULL; int n = fe_loop(&e, s, l, chord, A[a], &P);
                for (int i = 0; i < n; i++) { if (P[i].n == ncp && !got) { NC c; fe_curve(&e, &P[i], &c); double m1 = 0, m2 = 0; for (int j = 0; j < ncp; j++) for (int k = 0; k < 3; k++) { double x = rdd(q + 8 * ((size_t)j * vals + (vals == 4 ? 1 : 0) + k)), d1 = fabs(x - c.p[4 * j + 1 + k]), d2 = fabs(x - c.p[4 * (ncp - 1 - j) + 1 + k]); if (d1 > m1) m1 = d1; if (d2 > m2) m2 = d2; } double md = m1 < m2 ? m1 : m2; if (md < 1e-4) { got = 1; if (md > worst) worst = md; printf("      %d edgscan match surf %d loop %d ang %g ncp %d dev %.2e\n", ic, s->ent, l, A[a], ncp, md); } else if (md < 1e-2) near++; nc_free(&c); } free(P[i].p); } free(P); }
            fe_free(&e); }
        if (got) matched++; }
    printf("      0 edgscan stored order-2 curves %d matched %d near-misses %d candidates tested %d worst %.2e\n", nc, matched, near, tested, worst);
}

/* the surface kinds the edge curves are ported for: an untrimmed NURBS, and a trimmed surface (its base through MaFace's loops: positions only, so any base the evaluators cover) */
int mcsc_edge_supported(const McamFile *f, const Surf *s, char *msg, size_t msgn) {
    if (s->kind == 0) return 1;
    if (s->kind == 10) { const Surf *b = s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL; if (!b) { snprintf(msg, msgn, "Curve edges: the trimmed surface's base was not found"); return 0; }
        double d[4], p[3]; mcs_surf_domain(b, d); if (b->kind != 10 && mcs_surf_kind_ok(b) && !mcs_surf_eval(f, b, (d[0] + d[1]) * 0.5, (d[2] + d[3]) * 0.5, p)) return 1;   /* the loops only need positions: an evaluator must exist (pm_raw would give the origin) */
        snprintf(msg, msgn, "Curve edges: this trimmed surface's base kind (%d) has no evaluator here (NOT PORTED)", b->kind); return 0; }
    else snprintf(msg, msgn, "Curve edges of this surface kind (%d) are NOT PORTED (MaBicubicSurf / MaSurface::Flowline)", s->kind);
    (void)f; return 0;
}
/* the iso at the boundary: dir 1 = at constant u along v (Flowline(1, u)), 2 = at constant v along u; each piece goes through Simplify / MakeSplineOrNURBCurve (emit): MaCurve::IsPiecewiseLinearNURBCurve (ma_classes.c:25119: a NURBS of order 2 with more than 2 points, the iso of a degree 1 surface) skips Simplify (MakeSurfaceEdgeCurve core1.c:220-224, MakeAllSurfaceEdgeCurves core3.c:60-66) */
static int edge_iso(McamFile *f, const FE *e, const Surf *s, int dir, double par, double tol, double systol, int level, int color, int *out, int cap, int *nmade, const char **why) {
    NC pcs[4]; memset(pcs, 0, sizeof pcs); int np = iso_pieces(e, s, dir, par, pcs, 4);
    for (int k = 0; k < np; k++) { emit_piecewise = pcs[k].deg == 1 && pcs[k].ncp > 2; int ok = emit(f, &pcs[k], tol, systol, level, color, nmade, cap, out, why); emit_piecewise = 0; if (!ok) { for (int q = k; q < np; q++) nc_free(&pcs[q]); return 0; } nc_free(&pcs[k]); }
    return 1;
}
/* MaSurface::BoundaryClosestToUVPoint (0x1804800a0) + FindClosestEdgeToUVPoint (0x180483d20): the nearest non degenerate boundary to (u, v) in the (u, v) plane (U0, U1, V0, V1 in order; the first of equal distances), then Flowline at it: which 1 / 2: dir 1 at u0 / u1, which 3 / 4: dir 2 at v0 / v1. MakeSurfaceEdgeCurve (0x1802735c0): eval_surf at the (u, v) of the arrow, PointMinDist back to (u, v) - the arrow's own (u, v) here (the round trip is the same point on the surface). */
int mcsc_edge_one(McamFile *f, const Surf *s, double u, double v, double ang, double tol, double systol, int level, int color, int nurbs_wanted, int *out, int cap, int *nmade, char *msg, size_t msgn) {
    want_nurbs = nurbs_wanted; closed_tol = mcam_is_metric(f) ? 0.0025 : 0.0001; *nmade = 0;
    if (s->kind == 10) { const char *w2 = NULL; if (!fe_one(f, s, u, v, tol, ang, tol, systol, level, color, out, cap, nmade, &w2)) { snprintf(msg, msgn, "Curve One Edge: %s", w2 ? w2 : "failed"); return 0; } if (!*nmade) snprintf(msg, msgn, "Curve One Edge: no curve"); return *nmade > 0; }
    FE fe; fe_init(&fe, f, s); PM m = fe.m; const char *why = NULL; int which = 1; double best = 1e31; fe_arrow(&fe, f, s, &u, &v);   /* the arrow's (u, v) on MaSurface's parameters (core1.c:208-215) */
    for (int i = 0; i < 4; i++) {
        if (m.deg[i]) continue;
        double a0 = i < 2 ? (i ? m.d[1] : m.d[0]) : m.d[0], b0 = i < 2 ? m.d[2] : (i == 2 ? m.d[2] : m.d[3]), a1 = i < 2 ? a0 : m.d[1], b1 = i < 2 ? m.d[3] : b0;
        double ex = a1 - a0, ey = b1 - b0, l2 = ex * ex + ey * ey, t = l2 > 0 ? ((u - a0) * ex + (v - b0) * ey) / l2 : 0; if (t < 0) t = 0; if (t > 1) t = 1;
        double qx = a0 + ex * t, qy = b0 + ey * t, d = hypot(u - qx, v - qy);
        if (d < best) { best = d; which = i + 1; }
    }
    int dir = which <= 2 ? 1 : 2; double par = which == 1 ? m.d[0] : which == 2 ? m.d[1] : which == 3 ? m.d[2] : m.d[3];
    if (!edge_iso(f, &fe, s, dir, par, tol, systol, level, color, out, cap, nmade, &why)) { snprintf(msg, msgn, "Curve One Edge: %s", why); fe_free(&fe); return 0; }
    fe_free(&fe);
    if (!*nmade) snprintf(msg, msgn, "Curve One Edge: no curve");
    return *nmade > 0;
}
/* MaSurface::GetEdgeCrvs (0x180485930): the boundaries in the order v0, v1, u0, u1: a degenerate one is skipped (IsV0Degenerate etc.), v1 and u1 also when that direction is periodic. MakeAllSurfaceEdgeCurves (0x1802732f0): each through Simplify / MakeSplineOrNURBCurve (Fit arcs and lines: the same result here: its Simplify variant only differs for a piecewise linear NURBS, which goes to MakeSplineOrNURBCurve anyway). */
int mcsc_edge_all(McamFile *f, const Surf *s, double ang, double tol, double systol, int level, int color, int nurbs_wanted, int *out, int cap, int *nmade, char *msg, size_t msgn) {
    want_nurbs = nurbs_wanted; closed_tol = mcam_is_metric(f) ? 0.0025 : 0.0001; *nmade = 0;
    if (s->kind == 10) { const char *w2 = NULL; if (!fe_all(f, s, tol, ang, tol, systol, level, color, out, cap, nmade, &w2)) { snprintf(msg, msgn, "Curve All Edges: %s", w2 ? w2 : "failed"); return 0; } if (!*nmade) snprintf(msg, msgn, "Curve All Edges: no curve"); return *nmade > 0; }
    FE fe; fe_init(&fe, f, s); PM m = fe.m; const char *why = NULL;
    static const int ord[4] = { 2, 3, 0, 1 };   /* m.deg[] / d[]: 0 U0, 1 U1, 2 V0, 3 V1 */
    for (int i = 0; i < 4; i++) {
        int e = ord[i]; if (m.deg[e]) continue;
        if (e == 3 && m.per[1]) continue;   /* v1 of a periodic v */
        if (e == 1 && m.per[0]) continue;   /* u1 of a periodic u */
        int dir = e <= 1 ? 1 : 2; double par = m.d[e];
        if (!edge_iso(f, &fe, s, dir, par, tol, systol, level, color, out, cap, nmade, &why)) { snprintf(msg, msgn, "Curve All Edges: %s", why); fe_free(&fe); return 0; }
    }
    fe_free(&fe);
    if (!*nmade) snprintf(msg, msgn, "Curve All Edges: no curve");
    return *nmade > 0;
}

/* the bounds of a trimmed surface in the free parameters of a flowline (MaFace +0x10 .. +0x28, taken from the stored loop points' extents): lo / hi of u (dir 1) or v (dir 2) */
static void face_range(const FE *e, const Surf *s, int dir, double *lo, double *hi) {
    const uint8_t *r = s->rec; int m = rd32(r + 0x10c), nl = rd32(r + 0x128), first = 1;
    if (m <= 0 || nl <= 0 || 0x138 + 16 * (size_t)m + 8 * (size_t)nl > s->n) return;
    const uint8_t *t = r + 0x138; double a = 0, b = 0;
    for (int i = 0; i < m; i++) { double v[2]; memcpy(v, t + 16 * (size_t)i, 16); fe_uv_to_surf(e, v); double x = dir == 1 ? v[0] : v[1]; if (first) { a = b = x; first = 0; } else { if (x < a) a = x; if (x > b) b = x; } }
    if (!first) { *lo = a; *hi = b; }
}
/* CreateSurfaceFlowLines (mccore 0x18023cc20): method 2 Number: MaSurface::GenerateFlowlinesByEqualParametericIntervals (0x1804855e0). direction 0 U = IsoCurve(1, u) lines (IsDirection1 true: param_2 false), 1 V = IsoCurve(2, v).
   The parameter from the domain, on a trimmed surface from the face's extents clamped to the domain, in n - 1 equal steps; a periodic direction skips the last value; on a trimmed surface the first / last moved in by 1e-9 of the
   range; a piece whose middle derivative is <= 1e-4 inch / 0.0025 mm dropped (MaCurve::IsDegenerate); each piece simplified (Simplify / MakeSplineOrNURBCurve, MakeMCEnt's degeneracy at the system tolerance). */
int mcsc_flow_number(McamFile *f, const Surf *s, int n, int direction, double tol, double systol, int level, int color, int nurbs_wanted, int *out, int cap, int *nmade, char *msg, size_t msgn) {
    want_nurbs = nurbs_wanted; closed_tol = mcam_is_metric(f) ? 0.0025 : 0.0001;
    const char *why = NULL; const Surf *b = iso_base(f, s, &why);
    if (!b) { snprintf(msg, msgn, "Curve Flowline: %s", why); return 0; }
    FE fe; fe_init(&fe, f, s); PM m = fe.m;   /* m: the regenerated surface when the knots ask for it (LoadSurf(.., true, 30.0) core2.c:43) */
    int dir = direction ? 2 : 1, idx = dir == 1 ? 0 : 2;
    double lo = m.d[idx], hi = m.d[idx + 1];
    if (s->kind == 10) { double a = lo, c = hi; face_range(&fe, s, dir, &a, &c); if (a > lo) lo = a; if (c < hi) hi = c; }
    if (n < 2) n = 2;
    double step = (hi - lo) / (n - 1), range = m.d[idx + 1] - m.d[idx], par = lo, degen = closed_tol;
    int per = m.per[dir - 1]; *nmade = 0;
    for (int i = 0; i < n; i++) {
        if (per && i == n - 1) break;
        double use = par;
        if (s->kind == 10) { if (i == 0) use += range * 1e-9; else if (i == n - 1) use -= range * 1e-9; par = use; }
        NC pcs[64]; memset(pcs, 0, sizeof pcs);
        int np = iso_pieces(&fe, s, dir, use, pcs, 64);
        for (int k = 0; k < np; k++) {
            double mp[3], md[3], tm = (pcs[k].t1 + pcs[k].t2) * 0.5; nc_deriv(&pcs[k], tm, mp, md);
            double dl = sqrt(dot3(md, md));
            if (dl > degen && dl > systol) if (!emit(f, &pcs[k], tol, systol, level, color, nmade, cap, out, &why)) { snprintf(msg, msgn, "Curve Flowline: %s", why); for (int q = k; q < np; q++) nc_free(&pcs[q]); fe_free(&fe); return 0; }
            nc_free(&pcs[k]);
        }
        par += step; if (par > hi) par = hi;
    }
    fe_free(&fe);
    if (*nmade == 0) snprintf(msg, msgn, "Curve Flowline: no curve");
    return *nmade > 0;
}

/* tests (ours): every stored spline against every NURBS / trimmed-NURBS surface: a spline lying on a surface along a constant u or v line is compared with the iso curve built here */
void mcsc_scan(const McamFile *f) {
    for (int ic = 0; ic < f->nents; ic++) {
        const Ent *E = &f->ents[ic]; if (E->type != E_SPLINE || E->crv < 0 || E->crv >= f->ncrvs) continue;
        const CurveRec *cr = &f->crvs[E->crv]; if (!mcs_spline_ok(cr)) continue;
        double t1, t2, P[11][3], cb[6] = { 1e300, -1e300, 1e300, -1e300, 1e300, -1e300 }; mcs_spline_t1t2(cr, &t1, &t2);
        for (int i = 0; i <= 10; i++) { if (mcs_curve_eval(f, cr, t1 + (t2 - t1) * i / 10, P[i])) goto next; for (int k = 0; k < 3; k++) { if (P[i][k] < cb[2 * k]) cb[2 * k] = P[i][k]; if (P[i][k] > cb[2 * k + 1]) cb[2 * k + 1] = P[i][k]; } }
        for (int is = 0; is < f->nsurfs; is++) {
            const Surf *s = &f->surfs[is]; if (s->off || s->ent < 0 || (s->kind != 0 && s->kind != 10)) continue;
            const Surf *b = s; if (s->kind == 10) { b = s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL; if (!b || b->kind != 0) continue; }
            double d[4], sb[6] = { 1e300, -1e300, 1e300, -1e300, 1e300, -1e300 }, tolb = 0.05; mcs_surf_domain(b, d);
            if (!(d[1] > d[0]) || !(d[3] > d[2])) continue;
            for (int i = 0; i <= 8; i++) for (int j = 0; j <= 8; j++) { double q[3]; if (mcs_surf_eval(f, b, d[0] + (d[1] - d[0]) * i / 8, d[2] + (d[3] - d[2]) * j / 8, q)) continue; for (int k = 0; k < 3; k++) { if (q[k] < sb[2 * k]) sb[2 * k] = q[k]; if (q[k] > sb[2 * k + 1]) sb[2 * k + 1] = q[k]; } }
            int in = 1; for (int k = 0; k < 3; k++) { double m = (sb[2 * k + 1] - sb[2 * k]) * tolb + 1e-3; if (cb[2 * k] < sb[2 * k] - m || cb[2 * k + 1] > sb[2 * k + 1] + m) in = 0; }
            if (!in) continue;
            double ur[2] = { 1e300, -1e300 }, vr[2] = { 1e300, -1e300 }, dmax = 0; int ok = 1;
            for (int i = 0; i <= 10 && ok; i++) { double u, v, q[3]; if (!mcsc_uv_of_point(f, b, P[i], &u, &v) || mcs_surf_eval(f, b, u, v, q)) { ok = 0; break; }
                double dd = d3(q, P[i]); if (dd > 1e-4) { ok = 0; break; } if (dd > dmax) dmax = dd;
                if (u < ur[0]) ur[0] = u; if (u > ur[1]) ur[1] = u; if (v < vr[0]) vr[0] = v; if (v > vr[1]) vr[1] = v; }
            if (!ok) continue;
            int isu = ur[1] - ur[0] < (d[1] - d[0]) * 1e-5, isv = vr[1] - vr[0] < (d[3] - d[2]) * 1e-5;
            if (!isu && !isv) continue;
            NC nc = { 0 }; surf_iso(b, isu ? 1 : 2, isu ? (ur[0] + ur[1]) * 0.5 : (vr[0] + vr[1]) * 0.5, &nc);
            int deg = cr->cls == 0x80 ? cr->rec[0xd0] : -1, ncp = cr->cls == 0x80 ? rd32(cr->rec + 0xcc) : rd32(cr->rec + 0xb0);
            double cpd = -1, cpr = -1;
            int vals = cr->cls == 0x80 ? cr->rec[0xd1] : 0;
            if (cr->cls == 0x80 && deg == nc.deg && ncp == nc.ncp && (vals == 3 || vals == 4)) {
                const uint8_t *q = cr->rec + 0xd8 + 8 * (size_t)rd32(cr->rec + 0xc8); cpd = 0; cpr = 0;
                for (int i = 0; i < ncp; i++) for (int k = 0; k < 3; k++) {
                    double s0 = rdd(q + 8 * ((size_t)i * vals + (vals == 4 ? 1 : 0) + k));
                    double wf = vals == 4 ? rdd(q + 8 * (size_t)i * vals) : 1.0, wg = nc.p[4 * i], wr = nc.p[4 * (ncp - 1 - i)];
                    double e1 = fabs(s0 / wf - nc.p[4 * i + 1 + k] / wg), e2 = fabs(s0 / wf - nc.p[4 * (ncp - 1 - i) + 1 + k] / wr);
                    if (e1 > cpd) cpd = e1; if (e2 > cpr) cpr = e2; }
            }
            /* the stored curve's knots against the iso curve's (each normalised to 0 .. 1): same count and max difference, or how many of the stored interior knots are among the iso's */
            int same_k = -1; double kdiff = -1; int sub = 0, nint = 0;
            if (cr->cls == 0x80) {
                int nks = rd32(cr->rec + 0xc8); const uint8_t *qk = cr->rec + 0xd8; double k0s = rdd(qk), kns = rdd(qk + 8 * (size_t)(nks - 1)), k0i = nc.k[0], kni = nc.k[nc.nk - 1];
                if (kns > k0s && kni > k0i) {
                    if (nks == nc.nk) { same_k = 1; kdiff = 0; for (int i = 0; i < nks; i++) { double e = fabs((rdd(qk + 8 * (size_t)i) - k0s) / (kns - k0s) - (nc.k[i] - k0i) / (kni - k0i)); if (e > kdiff) kdiff = e; } }
                    for (int i = 1; i + 1 < nks; i++) { double x = (rdd(qk + 8 * (size_t)i) - k0s) / (kns - k0s); nint++; for (int j = 0; j < nc.nk; j++) if (fabs(x - (nc.k[j] - k0i) / (kni - k0i)) < 1e-6) { sub++; break; } }
                }
            }
            double covered = isu ? (vr[1] - vr[0]) / (d[3] - d[2]) : (ur[1] - ur[0]) / (d[1] - d[0]);
            printf("      %d crvscan srf ent %d kind %d iso %s %.6f dev %.2e | stored class 0x%x deg %d ncp %d | iso deg %d ncp %d | ctrl diff %.2e rev %.2e | knots same %d diff %.2e sub %d/%d | covers %.3f\n", ic, s->ent, s->kind, isu ? "u" : "v", isu ? ur[0] : vr[0], dmax, cr->cls, deg, ncp, nc.deg, nc.ncp, cpd, cpr, same_k, kdiff, sub, nint, covered);
            nc_free(&nc); break;
        }
        next:;
    }
}

/* tests (ours): the case file of tools/peloader --pmd for the NURBS surface s (21 x 19 interior points of the domain), and our result (uv, distance) for the same points next to it */
int mcsc_pmd_case(const McamFile *f, const Surf *s, const char *path) {
    if (s->kind != 0) return 0;
    const uint8_t *r = s->rec; int nku = rd32(r + 0x100), nkv = rd32(r + 0x104), ncu = rd32(r + 0x108), ncv = rd32(r + 0x10c), vpp = r[0x110], du = r[0x111], dv = r[0x112], rat = !(r[0x113] & 1);
    FILE *o = fopen(path, "w"); char p2[1200]; snprintf(p2, sizeof p2, "%s.ours", path); FILE *o2 = fopen(p2, "w"); if (!o || !o2) return 0;
    double d[4]; mcs_surf_domain(s, d); int NU = 21, NV = 19;
    fprintf(o, "%d %d %d %d %d %d %.17g %.17g %.17g %.17g %d\n", ncu, ncv, du + 1, dv + 1, rat, mcam_is_metric(f) ? 1 : 0, d[0], d[1], d[2], d[3], NU * NV);
    for (int i = 0; i < nku + nkv; i++) fprintf(o, "%.17g\n", rdd(r + 0x118 + 8 * i));
    const uint8_t *CP = r + 0x118 + 8 * ((size_t)nku + nkv);
    for (int i = 0; i < ncu * ncv; i++) { const uint8_t *q = CP + 8 * (size_t)vpp * i; double c[4]; if (vpp == 4) { c[3] = rat ? rdd(q) : 1.0; for (int k = 0; k < 3; k++) c[k] = rdd(q + 8 * (1 + k)); } else { c[3] = 1; for (int k = 0; k < 3; k++) c[k] = rdd(q + 8 * k); } fprintf(o, "%.17g %.17g %.17g %.17g\n", c[0], c[1], c[2], c[3]); }
    for (int i = 0; i < NU; i++) for (int j = 0; j < NV; j++) {
        double u = d[0] + (d[1] - d[0]) * (i + 0.5) / NU, v = d[2] + (d[3] - d[2]) * (j + 0.5) / NV, p[3], uo, vo, q[3];
        mcs_surf_eval(f, s, u, v, p); fprintf(o, "%.17g %.17g %.17g\n", p[0], p[1], p[2]);
        mcsc_uv_of_point(f, s, p, &uo, &vo); mcs_surf_eval(f, s, uo, vo, q);
        fprintf(o2, "%d %.17g %.17g %.17g %.17g %.17g\n", i * NV + j, uo, vo, d3(q, p), u, v);
    }
    fclose(o); fclose(o2); return 1;
}

static int fe_reparam(const Surf *b);
/* tests (ours): the case file of tools/peloader --edg for the trimmed surface s (on a NURBS base): the base, the loops as MaFace passes them (the first point repeated) */
int mcsc_edg_case(const McamFile *f, const Surf *s, const char *path, double ang, double chord, double tol) {
    if (s->kind != 10) return 0;
    const Surf *b = s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL; if (!b || b->kind != 0) return 0;
    const uint8_t *r = b->rec; int nku = rd32(r + 0x100), nkv = rd32(r + 0x104), ncu = rd32(r + 0x108), ncv = rd32(r + 0x10c), vpp = r[0x110], du = r[0x111], dv = r[0x112], rat = !(r[0x113] & 1);
    FILE *o = fopen(path, "w"); if (!o) return 0; double d[4]; mcs_surf_domain(b, d);
    int m = rd32(s->rec + 0x10c), nl = rd32(s->rec + 0x128); const uint8_t *t = s->rec + 0x138;
    fprintf(o, "%d %d %d %d %d %d %.17g %.17g %.17g %.17g %d %.17g %.17g %.17g %d\n", ncu, ncv, du + 1, dv + 1, rat, mcam_is_metric(f) ? 1 : 0, d[0], d[1], d[2], d[3], nl, ang, chord, tol, fe_reparam(b));
    for (int i = 0; i < nku + nkv; i++) fprintf(o, "%.17g\n", rdd(r + 0x118 + 8 * i));
    const uint8_t *CP = r + 0x118 + 8 * ((size_t)nku + nkv);
    for (int i = 0; i < ncu * ncv; i++) { const uint8_t *q = CP + 8 * (size_t)vpp * i; double c[4]; if (vpp == 4) { c[3] = rat ? rdd(q) : 1.0; for (int k = 0; k < 3; k++) c[k] = rdd(q + 8 * (1 + k)); } else { c[3] = 1; for (int k = 0; k < 3; k++) c[k] = rdd(q + 8 * k); } fprintf(o, "%.17g %.17g %.17g %.17g\n", c[0], c[1], c[2], c[3]); }
    for (int l = 0; l < nl; l++) {
        int st = rd32(t + 16 * (size_t)m + 8 * l), cn = rd32(t + 16 * (size_t)m + 8 * l + 4);
        fprintf(o, "%d\n", cn + 2);
        for (int i = 0; i < cn + 2; i++) { double v[2]; memcpy(v, t + 16 * (size_t)(st + (i <= cn ? i : 0)), 16); fprintf(o, "%.17g %.17g\n", v[0], v[1]); }
    }
    fclose(o); return 1;
}

/* MaCurve::Length(a, b, ..) 0x180405570: 16-point Gauss-Legendre (the nodes and weights of mccore .rdata 0x180d5b330 / 0x180d5b3b0) of |C'| over [a, b] */
static double iso_length(const PM *m, int dir, double other, double a, double b) {
    static const double X[16] = { 0.00529953250417514, 0.0277124884633838, 0.067184398806084206, 0.122297795822499, 0.191061877798678, 0.27099161117138598, 0.35919822461037099, 0.45249374508118101, 0.54750625491881899, 0.64080177538962901, 0.72900838882861396, 0.808938122201322, 0.877702204177501, 0.93281560119391604, 0.97228751153661597, 0.99470046749582497 };
    static const double W_[16] = { 0.013576229705877, 0.031126761969323902, 0.0475792558412464, 0.062314485627766897, 0.074797994408288299, 0.084578259697501199, 0.091301707522461806, 0.094725305227534196, 0.094725305227534196, 0.091301707522461806, 0.084578259697501199, 0.074797994408288299, 0.062314485627766897, 0.0475792558412464, 0.031126761969323902, 0.013576229705877 };
    double sum = 0, s = fabs(b - a);
    for (int i = 0; i < 16; i++) { double t = a + s * X[i], P[3], T[3], N[3]; pm_iso(m, dir, other, t, P, T, N); sum += W_[i] * s * sqrt(dot3(T, T)); }
    return sum;
}
/* MaCurve::ParamAtArcLength (0x180406910), forward: the parameter D past t0 toward tend; 1 when the rest is shorter: the first guess by the remaining length, up to 15 Newton steps on the length, clamped */
static int iso_param_at(const PM *m, int dir, double other, double D, double t0, double tend, double *out) {
    double L = iso_length(m, dir, other, t0, tend);
    if (!(D <= L)) return 1;
    double t = t0 + (D / L) * (tend - t0);
    for (int it = 0; it < 15; it++) {
        double l = iso_length(m, dir, other, t0, t); if (fabs(D - l) <= 1e-12) break;
        double P[3], T[3], N[3]; pm_iso(m, dir, other, t, P, T, N); double sp = sqrt(dot3(T, T)); if (!(sp > 0)) break;
        t += (D - l) / sp; if (t < t0) t = t0; if (t > tend) t = tend;
    }
    *out = t; return 0;
}
/* MaSurface::GenerateFlowlinesByChordHeight 0x180484ec0 (values: TessellateUntrimmed(chord, 0, 0, the other direction, 5000) over the whole domain) and ByDistance 0x180485250 (GenUVListByArcLength 0x180484bc0: from the parameter's low end stepping the longer of the two boundary iso curves by the distance, the high end last). Values outside the face's extents dropped; a periodic direction drops the last; on a trimmed surface the first / last moved in by 1e-9 of the range. */
int mcsc_flow_list(McamFile *f, const Surf *s, int method, double value, int direction, double tol, double systol, int level, int color, int nurbs_wanted, int *out, int cap, int *nmade, char *msg, size_t msgn) {
    want_nurbs = nurbs_wanted; closed_tol = mcam_is_metric(f) ? 0.0025 : 0.0001;
    const char *why = NULL; const Surf *b = iso_base(f, s, &why);
    if (!b) { snprintf(msg, msgn, "Curve Flowline: %s", why); return 0; }
    FE fe; fe_init(&fe, f, s); PM m = fe.m;   /* the regenerated surface when the knots ask for it (LoadSurf(.., true, 30.0) core2.c:43) */
    int dir = direction ? 2 : 1, idx = dir == 1 ? 0 : 2, per = m.per[dir - 1];
    double lo = m.d[idx], hi = m.d[idx + 1], range = hi - lo, l0 = lo, h0 = hi;
    if (s->kind == 10) { double a = lo, c = hi; face_range(&fe, s, dir, &a, &c); if (a > lo) lo = a; if (c < hi) hi = c; }
    static double V[5000]; int nv = 0;
    if (method == 0) { nv = pm_tess(&m, m.d, value, 0.0, 0, dir == 1 ? 2 : 1, V, 5000); if (nv < 0) nv = 0; }
    else {
        int idx2 = dir == 1 ? 2 : 0, isodir = dir == 1 ? 2 : 1;   /* the boundary iso curves: constant v (for u values), constant u (for v values) */
        double a0 = m.d[idx2], a1 = m.d[idx2 + 1];
        double L0 = iso_length(&m, isodir, a0, l0, h0), L1 = iso_length(&m, isodir, a1, l0, h0), other = L1 <= L0 ? a0 : a1, maxL = L1 <= L0 ? L0 : L1;
        V[nv++] = lo;
        if (maxL < value) V[nv++] = hi;
        else { double t = lo, tn; while (iso_param_at(&m, isodir, other, value, t, h0, &tn) == 0) { V[nv++] = tn; t = tn; if (nv >= 5000) break; } if (nv < 5000) V[nv++] = hi; }
    }
    *nmade = 0; double degen = closed_tol;
    for (int i = 0; i < nv; i++) {
        double val = V[i]; if (val < lo || val > hi) continue;
        if (per && i == nv - 1) continue;
        if (s->kind == 10) { if (i == 0) val += range * 1e-9; else if (i == nv - 1) val -= range * 1e-9; }
        NC pcs[64]; memset(pcs, 0, sizeof pcs);
        int np = iso_pieces(&fe, s, dir, val, pcs, 64);
        for (int k = 0; k < np; k++) {
            double mp[3], md[3], tm = (pcs[k].t1 + pcs[k].t2) * 0.5; nc_deriv(&pcs[k], tm, mp, md);
            double dl = sqrt(dot3(md, md));
            if (dl > degen && dl > systol) if (!emit(f, &pcs[k], tol, systol, level, color, nmade, cap, out, &why)) { snprintf(msg, msgn, "Curve Flowline: %s", why); for (int q = k; q < np; q++) nc_free(&pcs[q]); fe_free(&fe); return 0; }
            nc_free(&pcs[k]);
        }
    }
    fe_free(&fe);
    if (*nmade == 0) snprintf(msg, msgn, "Curve Flowline: no curve");
    return *nmade > 0;
}

/* the data of MaFace::MaFace (0x180413ba0) for the intersection kernel: the base surface (a kind 0 NURBS), the knot-regeneration flags of its DB constructor (fe_reparam) and the stored loops with their first point repeated; 0 when the record is not a trimmed NURBS */
int mcsc_face_data(const McamFile *f, const Surf *s, const Surf **base, int *rep, int *nl, int **np, double (***pts)[2]) {
    if (s->kind != 10) return 0;
    const Surf *b = s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL; if (!b || b->kind != 0) return 0;
    int m = rd32(s->rec + 0x10c), L = rd32(s->rec + 0x128); const uint8_t *t = s->rec + 0x138;
    *base = b; *rep = fe_reparam(b); *nl = L; *np = malloc(sizeof(int) * (L + 1)); *pts = malloc(sizeof(double (*)[2]) * (L + 1));
    for (int l = 0; l < L; l++) {
        int st = rd32(t + 16 * (size_t)m + 8 * l), cn = rd32(t + 16 * (size_t)m + 8 * l + 4);
        (*np)[l] = cn + 2; (*pts)[l] = malloc(sizeof(double[2]) * (cn + 2));
        for (int i = 0; i < cn + 2; i++) memcpy((*pts)[l][i], t + 16 * (size_t)(st + (i <= cn ? i : 0)), 16);
    }
    return 1;
}
int mcsc_surf_reparam(const Surf *b) { return fe_reparam(b); }
/* MaSurface::LoadSurf(eptr, true, ..) 0x1804879d0's MaNURBSurf of a kind 0 record: the raw knots, control points and stored range given to the DB
   constructor 0x180455dd0 (mcsrfint.c mi_db_knots: the knots normalised by products, the range clamped, the 1e-10 nudges, the knots regenerated from
   the derivative ratios when this[0x1c]); k0 / ks the raw first knot and span per direction (the stored loop points' normalisation); R (may be NULL)
   the old / new unique knots for loop points. Returns mi_db_knots' value (bits 0-1 the regenerated directions), -1 when the record is not read. */
int mcsc_load_nbs(const McamFile *f, const Surf *b, ISrf *o, void *R, double k0[2], double ks[2]) {
    const uint8_t *r = b->rec;
    if (b->kind != 0 || b->n < 0x118) return -1;
    int nku = rd32(r + 0x100), nkv = rd32(r + 0x104), ncu = rd32(r + 0x108), ncv = rd32(r + 0x10c), vpp = r[0x110], du = r[0x111], dv = r[0x112], rat = !(r[0x113] & 1);
    if (nku != ncu + du + 1 || nkv != ncv + dv + 1 || (vpp != 3 && vpp != 4) || 0x118 + 8 * ((size_t)nku + nkv + (size_t)vpp * ncu * ncv) > b->n) return -1;
    memset(o, 0, sizeof *o); o->ncu = ncu; o->ncv = ncv; o->ku = du + 1; o->kv = dv + 1; o->rat = rat; o->metric = mcam_is_metric(f); o->rev = (r[0x6e] & 4) != 0;
    o->KU = malloc(sizeof(double) * nku); o->KV = malloc(sizeof(double) * nkv); o->V = malloc(sizeof(double) * 4 * (size_t)ncu * ncv);
    for (int i = 0; i < nku; i++) o->KU[i] = rdd(r + 0x118 + 8 * i);
    for (int i = 0; i < nkv; i++) o->KV[i] = rdd(r + 0x118 + 8 * (nku + i));
    const uint8_t *CP = r + 0x118 + 8 * ((size_t)nku + nkv);
    for (int i = 0; i < ncu * ncv; i++) { const uint8_t *c = CP + 8 * (size_t)vpp * i; double *q = o->V + 4 * i;
        if (vpp == 4) { q[3] = rat ? rdd(c) : 1.0; for (int k = 0; k < 3; k++) q[k] = rdd(c + 8 * (1 + k)); } else { q[3] = 1.0; for (int k = 0; k < 3; k++) q[k] = rdd(c + 8 * k); } }
    if (k0) { k0[0] = o->KU[0]; k0[1] = o->KV[0]; }
    if (ks) { ks[0] = o->KU[nku - 1] - o->KU[0]; ks[1] = o->KV[nkv - 1] - o->KV[0]; }
    double H[4]; mcs_surf_domain(b, H);
    return mi_db_knots(o, H, 1, R);
}
/* MaCurve::Truncate of a cut before MaCurve::JoinCurves: MaNURBSpline::JoinSplines (0x180450940) truncates each trimmed curve */
void mcsc_truncate_mi(int *deg, int *ncp, int *nk, double **k, double **p, double t0, double t1) {
    NC c; memset(&c, 0, sizeof c); c.deg = *deg; c.ncp = *ncp; c.nk = *nk; c.k = *k; c.p = malloc(sizeof(double) * 4 * *ncp); for (int i = 0; i < *ncp; i++) { c.p[4 * i] = 1.0; memcpy(c.p + 4 * i + 1, *p + 3 * i, 24); } c.t1 = t0; c.t2 = t1;
    NC o; nc_sub(&c, t0, t1, &o); free(c.p); free(*p); free(*k);
    *deg = o.deg; *ncp = o.ncp; *nk = o.nk; *k = o.k; *p = malloc(sizeof(double) * 3 * o.ncp); for (int i = 0; i < o.ncp; i++) memcpy(*p + 3 * i, o.p + 4 * i + 1, 24); free(o.p);
}
