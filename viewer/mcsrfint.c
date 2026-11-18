/* mcsrfint.c - Mastercam's surface / plane intersection (mccore.dll MaSurfPlaneIntersect / MaSurfSurfIntersect), the kernel of Curve Slice and Curve at Intersection (formats/wireframe.md "Curve Slice").
 * A transliteration of the decompiled functions named at each function; the cutting data SRFSRFCUTDATA keeps the DLL's byte offsets (D / I macros) so the code can be read against the decompile.
 * The surface: a MaNURBSurf (knots normalised to 0 .. 1, as the DB constructor 0x180455dd0 does). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mcsrfint.h"
#include "mcsrfint_p.h"
#include "mcspline_refit.h"

/* MaNURBSurf::EvaluateSrf (0x180458ec0) through its Bezier patches: position and derivatives of the rational surface; the polynomial of the last span continues beyond the range */
/* MaNURBSurf::FindUKnotSpan (0x18045b860): the first knot from the degree-th + 1 at or above t ends the span (t at an interior knot: the span on its left); out of the range the first / last span */
int span(const double *K, int n, int deg, double t) { if (t < K[deg]) return deg; if (K[n] < t) { int s = n - 1; while (s > deg && K[s] >= K[s + 1]) s--; return s; } int s = deg; while (s < n - 1 && !(t <= K[s + 1])) s++; while (s < n - 1 && K[s + 1] <= K[s]) s++; return s; }
/* the basis functions and their derivatives to order nmax (at most 3) */
void bders_n(int p, const double *K, double u, int s, int nmax, double N[][26]) {
    double nd[26][26], left[26], right[26]; nd[0][0] = 1;
    for (int j = 1; j <= p; j++) { left[j] = u - K[s + 1 - j]; right[j] = K[s + j] - u; double sv = 0;
        for (int r = 0; r < j; r++) { nd[j][r] = right[r + 1] + left[j - r]; double t = nd[r][j - 1] / nd[j][r]; nd[r][j] = sv + right[r + 1] * t; sv = left[j - r] * t; }
        nd[j][j] = sv; }
    for (int j = 0; j <= p; j++) N[0][j] = nd[j][p];
    for (int r = 0; r <= p; r++) { double a[3][26]; int s1 = 0, s2 = 1; memset(a, 0, sizeof a); a[0][0] = 1;
        for (int k = 1; k <= nmax && k <= p; k++) { double d = 0; int rk = r - k, pk = p - k;
            if (r >= k) { a[s2][0] = a[s1][0] / nd[pk + 1][rk]; d = a[s2][0] * nd[rk][pk]; }
            int j1 = rk >= -1 ? 1 : -rk, j2 = r - 1 <= pk ? k - 1 : p - r;
            for (int j = j1; j <= j2; j++) { a[s2][j] = (a[s1][j] - a[s1][j - 1]) / nd[pk + 1][rk + j]; d += a[s2][j] * nd[rk + j][pk]; }
            if (r <= pk) { a[s2][k] = -a[s1][k - 1] / nd[pk + 1][r]; d += a[s2][k] * nd[r][pk]; }
            N[k][r] = d; int t = s1; s1 = s2; s2 = t; }
        }
    double fac = p; for (int k = 1; k <= nmax; k++) { for (int j = 0; j <= p; j++) N[k][j] = (k <= p) ? N[k][j] * fac : 0; fac *= p - k; }
}
void bders(int p, const double *K, double u, int s, double N[3][26]) { bders_n(p, K, u, s, 2, N); }
/* out: the DLL's p_3d array by byte offset / 8: [0..2] P, [3..5] N (Su x Sv), [6..8] Su, [9..11] Sv, [12..14] Suu, [15..17] Svv, [18..20] Suv */
/* MaSurface::VerifyNormal (0x180490ad0, reversed with the disassembly at 0x180490c1x: the decompiler's branches are unreadable): a normal within 1e-6 is re-evaluated at most 6 times a 1e-6 step towards the middle of the range (of u unless |Su| is the small one, of v unless |Sv| is): mid = (d1 - d0) / 2 + d0 */
int verify_normal(const ISrf *s, const double *E, int *count, double *u, double *v) {
    if (sqrt(E[3] * E[3] + E[4] * E[4] + E[5] * E[5]) > 1e-06 || *count >= 6) return 0;
    (*count)++;
    double mu = (s->d[1] - s->d[0]) * 0.5 + s->d[0], mv = (s->d[3] - s->d[2]) * 0.5 + s->d[2];
    double lu = sqrt(E[6] * E[6] + E[7] * E[7] + E[8] * E[8]), lv = sqrt(E[9] * E[9] + E[10] * E[10] + E[11] * E[11]);
    int nudge_u = lu > 1e-06, nudge_v = lv > 1e-06;
    if (lu > 1e-06 && lv > 1e-06) { nudge_u = 1; nudge_v = 1; } else if (!(lu > 1e-06)) { nudge_u = 0; nudge_v = 1; } else { nudge_u = 1; nudge_v = 0; }
    if (nudge_u) { if (*u < mu) *u += 1e-06; if (mu <= *u) *u -= 1e-06; }
    if (nudge_v) { if (*v < mv) *v += 1e-06; if (mv <= *v) *v -= 1e-06; }
    return 1;
}
/* ---- MaNURBSurf::EvaluateSrf (0x180458ec0) as the DLL computes it: the knot span's patch converted to Bezier form (ConvertNURBPatchToBezier
 * 0x180457ad0: MaNURBSpline::ConvertSegmentToBezier 0x180444aa0 along u for every row, then along v for every column), evaluated by
 * EvaluateRationalBezierPatch (0x180458b10: MaNURBSpline::EvaluateRationalBezierCurve 0x180448b80 along u per row, then along v), then the
 * quotient rule in EvaluateSrf's own operation order (the products by 1 / w, not divisions). The DLL keeps the last patch (+0xf0 / +0xf4 the span,
 * +0xf8 the points); the conversion depends only on the span, so the port converts per call. +0xd0 .. +0xe8 (the knot range the parameters are
 * mapped from) are 0 / 1 for the normalised knots: the mapping (d8 - d0) * u + d0 and the derivative scale factors are 1. ---- */
/* MaNURBSurf::FindUKnotSpan (0x18045b860) / FindVKnotSpan: the patch's first control point (nk = knots, ord = order) */
static int nbs_span(const double *K, int nk, int ord, double t) {
    int hi = nk - ord, i = ord;
    if (t < 0.0 || 1.0 < t) { if (1.0 < t) i = hi; }
    else {
        if (ord <= hi) { long l = ord; do { if (t <= K[l]) break; l++; i++; } while (l <= hi); }
        if (i == hi + 1) { int r = i - 1 - ord; return r < 0 ? 0 : r; }
    }
    int r = i - ord; return r < 0 ? 0 : r;
}
/* MaNURBSpline::ConvertSegmentToBezier (0x180444aa0) on p + 1 homogeneous points in place, K = the knots from the span's first control point:
 * the same arithmetic as mcspline_refit.c nb_to_bezier */
static void nbs_to_bezier(int p, const double *K, double *Q) {
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
/* MaNURBSpline::EvaluateRationalBezierCurve (0x180448b80): nder 0 .. 3 derivatives of the Bezier points B (p + 1, homogeneous) at t on [ab[0], ab[1]];
 * derivatives above the degree are 0 */
static void nbs_bez_eval(int nder, double t, int p, const double ab[2], const double *B, double O[4][4]) {
    double Q[27][4];
    if (p < nder) { for (int i = p + 1; i <= nder; i++) for (int k = 0; k < 4; k++) O[i][k] = 0.0; nder = p; }
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
static void nbs_eval(const ISrf *s, int order, double u, double v, double *E) {
    int nd = order > 3 ? 3 : order, count = 0;
    int ou = s->ku, ov = s->kv, pu = ou - 1, pv = ov - 1;
    for (;;) {
        int su = nbs_span(s->KU, s->ncu + ou, ou, u), sv = nbs_span(s->KV, s->ncv + ov, ov, v);
        double P[26][26][4];   /* [v row][u] */
        for (int r = 0; r < ov; r++) { memcpy(P[r], s->V + 4 * ((size_t)(sv + r) * s->ncu + su), sizeof(double) * 4 * ou); nbs_to_bezier(pu, s->KU + su, &P[r][0][0]); }
        for (int i = 0; i < ou; i++) { double C[26][4]; for (int r = 0; r < ov; r++) memcpy(C[r], P[r][i], 32); nbs_to_bezier(pv, s->KV + sv, &C[0][0]); for (int r = 0; r < ov; r++) memcpy(P[r][i], C[r], 32); }
        /* EvaluateRationalBezierPatch: per row along u, then the rows' values along v; O[k] = the DLL's p_4d out[k]: 0 S, 1 Su, 2 Sv, 3 Suu, 4 Svv, 5 Suv, 6 Suuu, 7 Svvv, 8 Suuv, 9 Suvv */
        double abu[2] = { s->KU[pu + su], s->KU[ou + su] }, abv[2] = { s->KV[pv + sv], s->KV[sv + ov] }, R[4][26][4], O[10][4], X[4][4];
        for (int r = 0; r < ov; r++) { double T[4][4]; nbs_bez_eval(nd, u, pu, abu, &P[r][0][0], T); for (int k = 0; k <= nd; k++) memcpy(R[k][r], T[k], 32); }
        nbs_bez_eval(nd, v, pv, abv, &R[0][0][0], X); memcpy(O[0], X[0], 32);
        if (nd > 0) { memcpy(O[2], X[1], 32); if (nd > 1) { memcpy(O[4], X[2], 32); if (nd > 2) memcpy(O[7], X[3], 32); } }
        if (nd != 0) {
            nbs_bez_eval(nd - 1, v, pv, abv, &R[1][0][0], X); memcpy(O[1], X[0], 32);
            if (nd != 1) {
                memcpy(O[5], X[1], 32); if (nd > 2) memcpy(O[9], X[2], 32);
                nbs_bez_eval(nd == 3, v, pv, abv, &R[2][0][0], X); memcpy(O[3], X[0], 32);
                if (nd > 2) { memcpy(O[8], X[1], 32); nbs_bez_eval(0, v, pv, abv, &R[3][0][0], X); memcpy(O[6], X[0], 32); }
            }
        }
        double inv = 1.0 / O[0][3];
        E[0] = O[0][0] * inv; E[1] = O[0][1] * inv; E[2] = O[0][2] * inv;
        if (nd == 0) return;
        double wu = O[1][3], wv = O[2][3];
        for (int k = 0; k < 3; k++) { E[6 + k] = (O[1][k] - wu * E[k]) * inv; E[9 + k] = (O[2][k] - wv * E[k]) * inv; }
        E[3] = E[11] * E[7] - E[10] * E[8]; E[4] = E[9] * E[8] - E[6] * E[11]; E[5] = E[6] * E[10] - E[9] * E[7];
        if (s->rev) { E[3] *= -1.0; E[4] *= -1.0; E[5] *= -1.0; }   /* this[0x10] == 1 */
        if (verify_normal(s, E, &count, &u, &v)) continue;   /* MaSurface::VerifyNormal 0x180490ad0: evaluated again at the moved (u, v) */
        if (nd == 1) return;
        double wuu = O[3][3], wvv = O[4][3], wuv = O[5][3];
        for (int k = 0; k < 3; k++) {
            E[12 + k] = ((O[3][k] - E[6 + k] * 2.0 * wu) - wuu * E[k]) * inv;
            E[15 + k] = ((O[4][k] - E[9 + k] * 2.0 * wv) - wvv * E[k]) * inv;
            E[18 + k] = (((O[5][k] - wu * E[9 + k]) - wv * E[6 + k]) - wuv * E[k]) * inv;
        }
        if (nd == 2) return;
        double wuuu = O[6][3], wvvv = O[7][3], wuuv = O[8][3], wuvv = O[9][3];
        for (int k = 0; k < 3; k++) {
            E[21 + k] = ((O[6][k] - (wuu * E[6 + k] + wu * E[12 + k]) * 3.0) - wuuu * E[k]) * inv;
            E[24 + k] = ((O[7][k] - (wvv * E[9 + k] + wv * E[15 + k]) * 3.0) - wvvv * E[k]) * inv;
            E[27 + k] = ((((O[8][k] - (wuv * E[6 + k] + wu * E[18 + k]) * 2.0) - wuu * E[9 + k]) - wv * E[12 + k]) - wuuv * E[k]) * inv;
            E[30 + k] = ((((O[9][k] - (wuv * E[9 + k] + wv * E[18 + k]) * 2.0) - wvv * E[6 + k]) - wu * E[15 + k]) - wuvv * E[k]) * inv;
        }
        return;
    }
}
const char *g_refused;   /* a path not ported was met: the result is refused (mi_trim_res, mcsrftrim.c) */
void refuse(const char *why) { if (!g_refused) g_refused = why; }
void offset_ev(const ISrf *s, int order, double u, double v, double *E);
#ifdef SPINT_TRACE
int g_tr_inbase;
#endif
#ifdef MI_TRACE
/* the march-level trace (tools/tests/ssmarch_oracle.sh against tools/peloader --trimss TRIMSS_TRACE, mccore_trim.c): the same lines as the DLL's hooks */
FILE *mi_trf; int mi_tron;
static void is_eval_(const ISrf *s, int order, double u, double v, double *E);
void is_eval(const ISrf *s, int order, double u, double v, double *E) { is_eval_(s, order, u, v, E); static int all = -1; if (all < 0) all = getenv("TRIMSS_TRALL") != NULL; if ((mi_tron || all) && mi_trf && !s->obase) { static int full = -1; fprintf(mi_trf, "EV %d %.17g %.17g %.17g %.17g %.17g", order, u, v, E[0], E[1], E[2]); if (full < 0) full = getenv("TRIMSS_EVFULL") != NULL; if (full && order > 0) for (int k = 3; k < (order == 1 ? 12 : 21); k++) fprintf(mi_trf, " %.17g", E[k]); fprintf(mi_trf, "\n"); } }
void mi_zq(int hunt, const double *P, double u, double v, double d) { if (mi_ztr()) fprintf(mi_trf, "ZQ %d %.17g %.17g %.17g -> %.17g %.17g %.17g\n", hunt, P[0], P[1], P[2], u, v, d); }
int mi_zm_loop;   /* ZM only for MaLoop::PointMinDist's call */
int mi_zec;   /* ZEC: the model curve's evaluations inside a ZN PointMinDist */
int mi_ztr(void) { static int on = -1; if (on < 0) on = getenv("TRIMSS_TRIM") != NULL; return on && mi_trf; }
static uint64_t z_fnv(uint64_t h, const double *p) { const uint8_t *b = (const uint8_t *)p; for (int i = 0; i < 16; i++) { h ^= b[i]; h *= 0x100000001b3ull; } return h; }
void mi_zl(const char *tag, const ILoop *L) {
    if (!L) { fprintf(mi_trf, "%s null\n", tag); return; }
    fprintf(mi_trf, "%s %d", tag, L->n);
    for (int i = 0; i < L->n; i++) {
        const ICv *q = L->pc[i]; const IPoly *p = &q->poly; uint64_t h = 0xcbf29ce484222325ull; int m = p->n;
        for (int k = 0; k < m; k++) { double v[2] = { p->p[k][0], p->p[k][1] }; h = z_fnv(h, v); }
        const ICv *c = q->inner; uint64_t hc = 0;
        if (c && c->kind == 1 && getenv("TRIMSS_ZNB")) { fprintf(mi_trf, " NB %d", c->ncp); for (int k = 0; k < c->ncp; k++) fprintf(mi_trf, " %.17g %.17g", c->P[3 * k], c->P[3 * k + 1]); fprintf(mi_trf, " K"); for (int k = 0; k < c->nk; k++) fprintf(mi_trf, " %.17g", c->K[k]); }
        if (c && c->kind == 1) { hc = 0x84222325cbf29ce4ull; for (int k = 0; k < c->ncp; k++) { double v[2] = { c->P[3 * k], c->P[3 * k + 1] }; hc = z_fnv(hc, v); } for (int k = 0; k + 1 < c->nk; k += 2) hc = z_fnv(hc, c->K + k); }
        if (c && c->kind == 0) { int ns = c->nseg; hc = 0xcbf29ce484222325ull; for (int k = 0; k < 6 * ns; k++) hc = z_fnv(hc, c->C + 2 * k); for (int k = 0; k + 1 <= ns; k += 2) hc = z_fnv(hc, c->U + k); }
        uint64_t hu = 0; if (p->u && getenv("TRIMSS_ZU")) { hu = 0xcbf29ce484222325ull; for (int k = 0; k + 1 < m; k += 2) hu = z_fnv(hu, p->u + k); }
        fprintf(mi_trf, " [%d %016llx %.17g %.17g %.17g %.17g %.17g %.17g c%016llx", m, (unsigned long long)h, q->t0, q->t1, p->p[0][0], p->p[0][1], p->p[m - 1][0], p->p[m - 1][1], (unsigned long long)hc);
        if (hu) fprintf(mi_trf, " u%016llx", (unsigned long long)hu); fprintf(mi_trf, "]");
    }
    fprintf(mi_trf, "\n");
}
void mi_zf(const char *tag, const MiFace *f) { fprintf(mi_trf, "%s %d\n", tag, f->ninner); mi_zl(" o", f->outer); for (int j = 0; j < f->ninner; j++) mi_zl(" i", f->inner[j]); }
#define is_eval is_eval_
static
#endif
void is_eval(const ISrf *s, int order, double u, double v, double *E) {
#ifdef SPINT_TRACE
    if (s->obase) { static int on = -1; if (on < 0) on = getenv("SPINT_EVTR") != NULL; if (on) fprintf(stderr, "EV %d %.15f %.15f\n", order, u, v); g_tr_inbase = 1; offset_ev(s, order, u, v, E); g_tr_inbase = 0; return; }
#endif
    if (s->obase) { offset_ev(s, order, u, v, E); return; }   /* MaOffsetSurf::EvaluateSrf 0x180463740 */
#ifdef SPINT_TRACE
    { static int on = -1; if (on < 0) on = getenv("SPINT_EVTR") != NULL; if (on && !g_tr_inbase) fprintf(stderr, "EV %d %.15f %.15f\n", order, u, v); }
#endif
    nbs_eval(s, order, u, v, E);
}
#ifdef MI_TRACE
#undef is_eval
#endif
double dot(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double len(const double *a) { return sqrt(dot(a, a)); }
void unit(double *v) { double l = len(v); if (l == 0) { v[0] = v[1] = v[2] = 0; } else { v[0] /= l; v[1] /= l; v[2] /= l; } }   /* FUN_18000f2f0 */
double d3(const double *a, const double *b) { double c[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; return len(c); }

/* MaSurface::GetEdgeInfo 0x180485a40: the virtual checks of MaSurface (MaNURBSurf's vtable +0xa0 .. +0xc8, tol 1e-4 inch / 0.0025 mm, cos 5 deg)
 * in the DLL's order (IsSmooth(2.5), this[0x1b], below); a degenerate side clears the other direction's closed / periodic flags. On iso curves (MaSurfaceIsoCurve: dir 1 = u constant, the parameter v; dir 2 = v constant) */
static void bp_unit(const double *d, double *u);
static void edge_closed(ISrf *s, int vdir, double tol, double cs) {   /* CheckUClosedOrPeriodic 0x180480940 / CheckVClosedOrPeriodic 0x1804810b0 */
    Iso c; double t = vdir ? s->d[0] : s->d[2], step = (vdir ? s->d[4] : s->d[5]) * 0.25, mn = 1.1, gap = 0.0, dm = 0.0;
    int dir = vdir ? 2 : 1; double p0 = vdir ? s->d[2] : s->d[0], p1 = vdir ? s->d[3] : s->d[1];
    for (int it = 0; it < 5; it++) {
        double A[21], B[21], M[21], n0[3], n1[3];
        iso_set(&c, s, dir, p0); iso_eval(&c, -1, t, A); bp_unit(A + 12, n0);
        iso_set(&c, s, dir, p1); iso_eval(&c, -1, t, B); bp_unit(B + 12, n1);
        gap = sqrt((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2]));
        iso_set(&c, s, 1, (p1 + p0) * 0.5); iso_eval(&c, -1, t, M);   /* both checks set u = the middle (asm 0x18048139d: edx 1 in the V check, too) */
        dm = sqrt((M[0] - A[0]) * (M[0] - A[0]) + (M[1] - A[1]) * (M[1] - A[1]) + (M[2] - A[2]) * (M[2] - A[2]));
        if (tol < gap) break;
        double d = fabs(n1[0] * n0[0] + n1[1] * n0[1] + n1[2] * n0[2]); if (d < mn) mn = d;
        t = t + step;
    }
    if (gap <= tol && gap < dm) { s->cl[vdir] = 1; if (cs <= mn) s->per[vdir] = 1; }
}
static void edge_degen(ISrf *s, int e, double tol) {   /* CheckU0Degenerate 0x180480620 / U1 0x1804807b0 / V0 0x180480d90 / V1 0x180480f20 */
    Iso c; double P[5][21], t = e < 2 ? s->d[2] : s->d[0], step = e < 2 ? s->d[5] : s->d[4], dd = 0.0;
    iso_set(&c, s, e < 2 ? 1 : 2, s->d[e]);   /* u0 u1 v0 v1 = d[0 .. 3] */
    for (int i = 0; i < 5; i++) { iso_eval(&c, 0, t, P[i]); t = t + step * 0.25; }
    for (int i = 1; i < 5; i++) { dd = sqrt((P[i][1] - P[i - 1][1]) * (P[i][1] - P[i - 1][1]) + (P[i][0] - P[i - 1][0]) * (P[i][0] - P[i - 1][0]) + (P[i][2] - P[i - 1][2]) * (P[i][2] - P[i - 1][2])); if (tol < dd) break; }
    s->deg[e] = dd <= tol;
}
void is_edges(ISrf *s) {
    double tol = s->metric ? 0.0025 : 1e-4, cs = cos(0.08726646259971645);
    s->cl[0] = s->cl[1] = 0; s->per[0] = s->per[1] = 0; s->deg[0] = s->deg[1] = s->deg[2] = s->deg[3] = 0;
    edge_closed(s, 0, tol, cs); edge_closed(s, 1, tol, cs);
    for (int e = 0; e < 4; e++) edge_degen(s, e, tol);
    if ((s->deg[0] || s->deg[1]) && s->cl[0]) { s->cl[0] = 0; s->per[0] = 0; }
    if ((s->deg[2] || s->deg[3]) && s->cl[1]) { s->cl[1] = 0; s->per[1] = 0; }
    s->edges = 1;
    /* then IsSmooth(2.5) -> this[0x1b]: the DLL's (MaSurface::IsSmooth 0x180486870: GenerateFlowlinesAtPatchBoundaries' NURBS flow lines,
       MaNURBSpline::IsSmooth 0x1804508f0 = !HasSharpNodes) evaluates no surface point; the flag is not read by the ported code and mcsrfint.c's
       is_smooth is an approximation (iso tangents at the knots), so it is not called here */
}
#undef U0
#undef U1
#undef V0
#undef V1
#define EDGES(s) do { if (!(s)->edges) is_edges((ISrf *)(s)); } while (0)
int has_deg(ISrf *s) { EDGES(s); return s->deg[0] || s->deg[1] || s->deg[2] || s->deg[3]; }
int has_seams(ISrf *s) { EDGES(s); return s->cl[0] || s->cl[1]; }   /* HasSeams 0x180486620: IsUClosed / IsVClosed */
void limit_uv(const ISrf *s, double *u, double *v) { if (s->d[1] < *u || *u < s->d[0]) *u = s->d[1] < *u ? s->d[1] : s->d[0]; if (s->d[3] < *v || *v < s->d[2]) *v = s->d[3] < *v ? s->d[3] : s->d[2]; }

/* MaSurface::ExtEvaluate (0x1804835b0), not subset: a periodic parameter is wrapped, then Evaluate */
void ext_eval(ISrf *s, int order, double u, double v, double *E) {
    EDGES(s);
    double du = s->d[1] - s->d[0], dv = s->d[3] - s->d[2];
    if (s->per[0]) { while (u < s->d[0]) u += du; while (u > s->d[1]) u -= du; }
    if (s->per[1]) { while (v < s->d[2]) v += dv; while (v > s->d[3]) v -= dv; }
    is_eval(s, order, u, v, E);
}
/* MaSurface::BoundaryClosestToUVPoint (0x1804800a0): the nearest of the non-degenerate sides in (u, v); *e = 1 u0, 2 u1, 3 v0, 4 v1 */
double seg_dist(const double a[2], const double b[2], const double p[2], double *tout) {
    double d[2] = { b[0] - a[0], b[1] - a[1] }, l = hypot(d[0], d[1]), q[2] = { p[0] - a[0], p[1] - a[1] }, ql = hypot(q[0], q[1]);   /* MaLine::PointMinDist 0x18042ba60 */
    if (l <= 1e-6) { *tout = 0; return ql; }
    double ux = 0, uy = 0, ex = d[0] / l, ey = d[1] / l, r; if (ql != 0) { ux = q[0] / ql; uy = q[1] / ql; }
    double dd = ex * ux + ey * uy, dt = dd * ql;
    if (fabs(dd) == 1.0 || ql == 0.0) r = 0; else { double a2 = 1 - dd * dd; r = a2 >= 1e-20 ? sqrt(a2) * ql : 0; }
    *tout = l > 1e-20 ? dt / l : 0; return r;
}
void boundary_uvpt(ISrf *s, const double p[2], int *e, double *dist) {
    double best = 1e31; *dist = 0; *e = 1; double t; EDGES(s);
    for (int i = 0; i < 4; i++) {
        if (s->deg[i]) continue;
        double a[2], b[2];
        if (i < 2) { a[0] = b[0] = i ? s->d[1] : s->d[0]; a[1] = s->d[2]; b[1] = s->d[3]; }
        else { a[1] = b[1] = i == 2 ? s->d[2] : s->d[3]; a[0] = s->d[0]; b[0] = s->d[1]; }
        double d = seg_dist(a, b, p, &t);
        if (d < best) { best = d; *dist = d; *e = i + 1; }
    }
}
/* MaSurface::IsUVOnEdge (0x180487340) */
int uv_on_edge(ISrf *s, double u, double v) { double p[2] = { u, v }, dist; int e; boundary_uvpt(s, p, &e, &dist); if (e > 0) { if (e < 3) return dist < (s->d[1] - s->d[0]) * 5e-05; if (e < 5) return dist < (s->d[3] - s->d[2]) * 5e-05; } return 0; }

/* ---- MaSurface::GenHuntingGrid (0x180484b10) -> MaSurfHuntingGrid(MaSurface *) 0x180494860 -> GenGrid(void) 0x180495470 ---- */
/* a vector divided by its length (x x + y y + z z), 0 when the length is 0; *small set when shorter than sm (TessellateUntrimmed's normalisations) */
static void tu_unit(const double *a, double *o, double sm, int *small) {
    double l = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (small && l < sm) *small = 1;
    if (l == 0.0) { o[0] = o[1] = o[2] = 0.0; } else { o[0] = a[0] / l; o[1] = a[1] / l; o[2] = a[2] / l; }
}
/* MaSurface::TessellateUntrimmed (0x18048d4c0) as GenGrid(void) calls it: param_1 (chord) 0, param_2 10 degrees, param_3 1 (the tangent / normal
 * angle tests; the chord test of param_3 0 is not reached), param_4 = dir (1: the iso curves of constant u at u0 + k du / 4, the list in v; 2: constant
 * v, the list in u), param_5 = max. Per iso curve the list starts as the range's ends; the spans are tested in order on a stack of end states (point,
 * unit tangent, unit normal of MaCurve::Evaluate(-1)): at 1/4, 1/2, 3/4 of the span: |Ta.Tb|, Tm.Ta, Tm.Tb, |Na.Nb| (no abs on the Tm ones), then
 * |Nm.Na|, |Nm.Nb| and the tangent at f - 1/12 against Ta and Tb, all against cos(10 deg); a span whose end tangents or normals were ever shorter
 * than 1e-7 inch / 2.5e-6 mm (the flag stays set for the rest of the iso curve) is accepted untested. A failing span: the end state is pushed, the
 * new end is the evaluation at (b - a) * 0.5 + a (the one at 1/2 when it was made), the list gets (b + a) * 0.5. The first iso curve's list is
 * kept; the next ones' interior values are inserted strictly between neighbours, the scan resuming at the last insertion. Returns 1 when a list
 * reaches max. */
static int tess_untrimmed(ISrf *s, int dir, double *L, int *pn, int max) {
    double sm = s->metric ? 2.5e-6 : 1e-7, cs = cos(10.0 * 0.01745329251994329), F[3] = { 0.25, 0.5, 0.75 };
    double *tmp = malloc(sizeof(double) * max); int tmpn = 0, ret = 0;
    double (*SP)[3] = malloc(sizeof(double[3]) * max), (*ST)[3] = malloc(sizeof(double[3]) * max), (*SN)[3] = malloc(sizeof(double[3]) * max);
    double step = (dir == 1 ? s->d[4] : s->d[5]) * 0.25, par[5];
    par[0] = dir == 1 ? s->d[0] : s->d[2]; par[1] = par[0] + step; par[2] = par[1] + step; par[3] = par[2] + step; par[4] = step + par[3];
    for (int q = 0; q < 5; q++) {
        Iso iso; memset(&iso, 0, sizeof iso); iso_set(&iso, s, dir, par[q]);
        L[0] = dir == 1 ? s->d[2] : s->d[0]; L[1] = dir == 1 ? s->d[3] : s->d[1]; *pn = 2;
        double A[21], B[21], SPt[3], Ta[3], Na[3], EP[3], Tb[3], Nb[3]; int degen = 0;
        iso_eval(&iso, -1, L[0], A); iso_eval(&iso, -1, L[1], B);
        memcpy(SPt, A, 24); memcpy(EP, B, 24);
        tu_unit(A + 3, Ta, sm, &degen); tu_unit(B + 3, Tb, sm, &degen); tu_unit(A + 12, Na, sm, &degen); tu_unit(B + 12, Nb, sm, &degen);
        int i = 0, sp = 0;
        for (;;) {
            if (sqrt(Ta[0] * Ta[0] + Ta[1] * Ta[1] + Ta[2] * Ta[2]) < sm || sqrt(Tb[1] * Tb[1] + Tb[0] * Tb[0] + Tb[2] * Tb[2]) < sm ||
                sqrt(Na[0] * Na[0] + Na[1] * Na[1] + Na[2] * Na[2]) < sm || sqrt(Nb[1] * Nb[1] + Nb[0] * Nb[0] + Nb[2] * Nb[2]) < sm) degen = 1;
            int split = 0, mid = 0; double MP[3], MT[3], MN[3];
            for (int k = 0; k < 3; k++) {
                double M[21]; iso_eval(&iso, -1, (L[i + 1] - L[i]) * F[k] + L[i], M);
                if (k == 1) { mid = 1; memcpy(MP, M, 24); tu_unit(M + 3, MT, sm, NULL); tu_unit(M + 12, MN, sm, NULL); }
                if (degen) continue;
                if (fabs(Ta[1] * Tb[1] + Ta[0] * Tb[0] + Ta[2] * Tb[2]) < cs) { split = 1; break; }
                double Tm[3], Nm[3]; tu_unit(M + 3, Tm, sm, NULL);
                if (Tm[1] * Ta[1] + Tm[0] * Ta[0] + Tm[2] * Ta[2] < cs) { split = 1; break; }
                if (Tm[1] * Tb[1] + Tm[0] * Tb[0] + Tm[2] * Tb[2] < cs || fabs(Na[1] * Nb[1] + Na[0] * Nb[0] + Na[2] * Nb[2]) < cs) { split = 1; break; }
                tu_unit(M + 12, Nm, sm, NULL);
                double a = Nm[1] * Na[1] + Nm[0] * Na[0] + Nm[2] * Na[2], b = Nm[1] * Nb[1] + Nm[0] * Nb[0] + Nm[2] * Nb[2];
                if (!(cs <= fabs(a) && cs <= fabs(b))) { split = 1; break; }
                double M2[21], T2[3]; iso_eval(&iso, -1, (L[i + 1] - L[i]) * (F[k] - 0.08333333333333333) + L[i], M2); tu_unit(M2 + 3, T2, sm, NULL);
                if (T2[1] * Ta[1] + Ta[0] * T2[0] + T2[2] * Ta[2] < cs || T2[1] * Tb[1] + Tb[0] * T2[0] + T2[2] * Tb[2] < cs) { split = 1; break; }
            }
            if (split) {
                memcpy(SP[sp], EP, 24); memcpy(ST[sp], Tb, 24); memcpy(SN[sp], Nb, 24); sp++;
                if (mid) { memcpy(EP, MP, 24); memcpy(Tb, MT, 24); memcpy(Nb, MN, 24); }
                else { double M[21]; iso_eval(&iso, -1, (L[i + 1] - L[i]) * 0.5 + L[i], M); memcpy(EP, M, 24); tu_unit(M + 3, Tb, sm, NULL); tu_unit(M + 12, Nb, sm, NULL); }
                for (int j = *pn - 1; j > i; j--) L[j + 1] = L[j];
                (*pn)++;
                if (*pn == max) { ret = 1; goto done; }
                L[i + 1] = (L[i + 2] + L[i]) * 0.5;
                continue;
            }
            memcpy(SPt, EP, 24); memcpy(Ta, Tb, 24); memcpy(Na, Nb, 24);
            if (sp >= 1) { sp--; memcpy(EP, SP[sp], 24); memcpy(Tb, ST[sp], 24); memcpy(Nb, SN[sp], 24); }
            i++;
            if (*pn - 1 <= i) break;
        }
        (void)SPt;
        if (q == 0) { memcpy(tmp, L, sizeof(double) * *pn); tmpn = *pn; }
        else if (*pn >= 3) {
            int l = 1;
            for (int j = 1; j < *pn - 1; j++) {
                double x = L[j];
                for (int m = l; m < tmpn; m++) {
                    if (tmp[m] <= x || x <= tmp[m - 1]) continue;
                    memmove(tmp + m + 1, tmp + m, sizeof(double) * (tmpn - m)); tmp[m] = x; tmpn++; l = m;
                    if (tmpn == max) { ret = 1; goto done; }
                    break;
                }
            }
        }
    }
    memcpy(L, tmp, sizeof(double) * tmpn); *pn = tmpn;
done:
    free(tmp); free(SP); free(ST); free(SN);
    return ret;
}
/* MaSurfHuntingGrid::AdjustRangeForPolesAndSeams (0x180494920): a degenerate side moves in by |N| of Evaluate(1) at the middle of that side, at
 * most 1e-4 of the range; a periodic direction then moves in by 1e-4 of the (adjusted) range at both ends (SetMinMaxParams 0x1804892e0). 1 = changed */
static int grid_adjust(ISrf *s) {
    double a = s->d[0], b = s->d[1], c = s->d[2], d = s->d[3], E[21], x; int ch = 0;
    EDGES(s);
    if (has_deg(s)) {
        if (s->deg[0]) { is_eval(s, 1, s->d[0], (s->d[3] + s->d[2]) * 0.5, E); a = a + sqrt(E[3] * E[3] + E[4] * E[4] + E[5] * E[5]); x = s->d[4] * 0.0001 + s->d[0]; if (x < a) a = x; }
        if (s->deg[1]) { is_eval(s, 1, s->d[1], (s->d[3] + s->d[2]) * 0.5, E); b = b - sqrt(E[3] * E[3] + E[4] * E[4] + E[5] * E[5]); x = s->d[1] - s->d[4] * 0.0001; if (b < x) b = x; }
        if (s->deg[2]) { is_eval(s, 1, (s->d[1] + s->d[0]) * 0.5, s->d[2], E); c = c + sqrt(E[3] * E[3] + E[4] * E[4] + E[5] * E[5]); x = s->d[5] * 0.0001 + s->d[2]; if (x < c) c = x; }
        if (s->deg[3]) { is_eval(s, 1, (s->d[1] + s->d[0]) * 0.5, s->d[3], E); d = d - sqrt(E[3] * E[3] + E[4] * E[4] + E[5] * E[5]); x = s->d[3] - s->d[5] * 0.0001; if (d < x) d = x; }
        if (a < b && c < d) { s->d[1] = b; s->d[0] = a; s->d[4] = b - a; s->d[3] = d; s->d[2] = c; s->d[5] = d - c; s->edges = 0; }   /* SetMinMaxParams -> SetDirty -> InitEdgeInfo: the edge flags found again on the new range */
        ch = 1;
    }
    EDGES(s);
    if (s->per[0]) { a = a + s->d[4] * 0.0001; b = b - s->d[4] * 0.0001; if (a < b && c < d) { s->d[1] = b; s->d[0] = a; s->d[4] = b - a; s->d[3] = d; s->d[2] = c; s->d[5] = d - c; s->edges = 0; EDGES(s); } ch = 1; }
    if (s->per[1]) { c = c + s->d[5] * 0.0001; d = d - s->d[5] * 0.0001; if (a < b && c < d) { s->d[1] = b; s->d[0] = a; s->d[4] = b - a; s->d[3] = d; s->d[2] = c; s->d[5] = d - c; s->edges = 0; EDGES(s); } ch = 1; }
    return ch;
}
/* MaSurfHuntingGrid::GenGrid (0x180495470): the range adjusted for the tessellation (and put back after), TessellateUntrimmed in v (dir 1) then
 * in u (dir 2); a list of fewer than 5 values becomes 5 at quarter steps (x += range * 0.25); a failure gives 10 x 10 at range / 9 steps; the grid
 * points (Evaluate order 0) kept for EstimateUV and MaMinMaxBox::GenSrfBox. */
void grid_build(ISrf *s) {
    if (s->gu) return; EDGES(s);
    double keep[6]; memcpy(keep, s->d, sizeof keep);
    int adj = grid_adjust(s), nu = 0, nv = 0;
    s->gu = malloc(sizeof(double) * 5000); s->gv = malloc(sizeof(double) * 5000);
    if (tess_untrimmed(s, 1, s->gv, &nv, 5000) == 0 && tess_untrimmed(s, 2, s->gu, &nu, 5000) == 0) {
        if (nu < 5) { nu = 5; double x = s->d[0]; for (int i = 0; i < 5; i++) { s->gu[i] = x; x = x + s->d[4] * 0.25; } }
        if (nv < 5) { nv = 5; double x = s->d[2]; for (int i = 0; i < 5; i++) { s->gv[i] = x; x = x + s->d[5] * 0.25; } }
        s->gp = malloc(sizeof(double) * 3 * (size_t)nu * nv);
        for (int i = 0; i < nu; i++) for (int j = 0; j < nv; j++) { double E[21]; is_eval(s, 0, s->gu[i], s->gv[j], E); memcpy(s->gp + 3 * ((size_t)i * nv + j), E, 24); }
    } else {
        nu = nv = 10; s->gp = malloc(sizeof(double) * 3 * 100);
        double x = s->d[0];
        for (int i = 0; i < 10; i++) { double y = s->d[2]; s->gu[i] = x;
            for (int j = 0; j < 10; j++) { s->gv[j] = y; double E[21]; is_eval(s, 0, x, y, E); memcpy(s->gp + 3 * (i * 10 + j), E, 24); y = y + s->d[5] / 9.0; }
            x = x + s->d[4] / 9.0; }
    }
    s->ngu = nu; s->ngv = nv;
    if (adj) { memcpy(s->d, keep, sizeof keep); s->edges = 0; }
}
/* MaSurface::EstimateUV (0x180482520): the nearest stored grid point, the degenerate sides' rows left out; none: the range's middle */
void estimate_uv(ISrf *s, const double *P, double *u, double *v) {
    grid_build(s);
    double bd = 1e31, bu = (s->d[1] + s->d[0]) * 0.5, bv = (s->d[3] + s->d[2]) * 0.5;
    for (int i = 0; i < s->ngu; i++) { if ((s->deg[0] && i == 0) || (s->deg[1] && i == s->ngu - 1)) continue;
        for (int j = 0; j < s->ngv; j++) { if ((s->deg[2] && j == 0) || (s->deg[3] && j == s->ngv - 1)) continue;
            const double *E = s->gp + 3 * ((size_t)i * s->ngv + j); double dd = (E[0] - P[0]) * (E[0] - P[0]) + (E[1] - P[1]) * (E[1] - P[1]) + (E[2] - P[2]) * (E[2] - P[2]);
            if (dd < bd) { bd = dd; bu = s->gu[i]; bv = s->gv[j]; } } }
    *u = bu; *v = bv;
}
/* MaSurface::PointMinDist (0x180488080). hunt = param_1, limit = param_7. Output: out[0..2] foot, out[3..5] unit normal. Returns 0 / 1 / -1 as the DLL */
#ifdef MI_TRACE
static int is_pmd_(ISrf *s, int hunt, const double *P, double *out, double *dist, double *uo, double *vo, int limit);
int is_pmd(ISrf *s, int hunt, const double *P, double *out, double *dist, double *uo, double *vo, int limit) {
    double u0 = *uo, v0 = *vo; int r = is_pmd_(s, hunt, P, out, dist, uo, vo, limit);
    if (mi_tron && mi_trf) fprintf(mi_trf, "PM %d %d %.17g %.17g %.17g in %.17g %.17g r %d uv %.17g %.17g d %.17g\n", hunt, limit, P[0], P[1], P[2], u0, v0, r, *uo, *vo, *dist);
    return r;
}
#define is_pmd is_pmd_
static
#endif
int is_pmd(ISrf *s, int hunt, const double *P, double *out, double *dist, double *uo, double *vo, int limit) {
    double eps = s->metric ? 2.5e-17 : 1e-18, E[21];
    if (hunt) estimate_uv(s, P, uo, vo);
    else if (has_deg(s)) { if (s->deg[0] && *uo < s->d[0]) *uo = s->d[0]; if (s->deg[1] && s->d[1] < *uo) *uo = s->d[1]; if (s->deg[2] && *vo < s->d[2]) *vo = s->d[2]; if (s->deg[3] && s->d[3] < *vo) *vo = s->d[3]; }
    double u = *uo, v = *vo, ru = s->d[4], rv = s->d[5], dua = du_allow(s), dva = dv_allow(s);   /* local_1b0 / local_170: DeltaUAllowable / DeltaVAllowable */
    double u0p = 0, v0p = 0, dsu = 0, dsv = 0; int it = 0;
    for (it = 0; it < 50; it++) {
        int lim = 0; ext_eval(s, 2, u, v, E);
        double dx = E[0] - P[0], dz = E[2] - P[2], dy = E[1] - P[1], nl = sqrt(E[3] * E[3] + E[4] * E[4] + E[5] * E[5]);
        if (nl < 1e-20) nl = 1e-20;
        nl = 1.0 / nl; double n[3] = { nl * E[3], nl * E[4], nl * E[5] };   /* the products by the inverse (0x180488080) */
        double d0 = n[0] * E[0] + n[1] * E[1] + n[2] * E[2], k = (n[0] * P[0] + n[1] * P[1] + n[2] * P[2]) - d0;
        double Q[3] = { P[0] - k * n[0], P[1] - k * n[1], P[2] - k * n[2] }, qy = Q[1] - E[1], qx = Q[0] - E[0], qz = Q[2] - E[2];
        double cx = n[1] * dz - n[2] * dy, cy = n[2] * dx - n[0] * dz, cz = n[0] * dy - n[1] * dx;
        if (qy * qy + qx * qx + qz * qz <= eps || sqrt(cy * cy + cx * cx + cz * cz) < 1e-06) {
            if (limit) limit_uv(s, &u, &v);
            *uo = u; *vo = v; ext_eval(s, 1, u, v, E);
            { double l = len(E + 3); if (l == 0) E[3] = E[4] = E[5] = 0; else { E[3] /= l; E[4] /= l; E[5] /= l; } }
            *dist = d3(E, P); memcpy(out, E, 48);
            if (*uo < s->d[0] || (s->d[1] <= *uo && *uo != s->d[1]) || *vo < s->d[2] || (s->d[3] <= *vo && *vo != s->d[3])) return -1;
            return 0;
        }
        double dV13 = E[11] * n[1] - E[10] * n[2];        /* dVar13 = Sv.z n.y - Sv.y n.z */
        double dV18 = E[9] * n[2] - n[0] * E[11];         /* dVar18 = Sv.x n.z - n.x Sv.z */
        double dV14 = n[0] * E[10] - E[9] * n[1];         /* dVar14 = n.x Sv.y - Sv.x n.y */
        double dV12 = dV18 * E[7] + dV13 * E[6] + dV14 * E[8];
        if (fabs(dV12) < 1e-20) dV12 = 1e-20;
        double dV4 = n[1] * E[8] - n[2] * E[7];           /* dVar4 = n.y Su.z - n.z Su.y */
        double dV3 = E[6] * n[2] - n[0] * E[8];           /* dVar3 = Su.x n.z - n.x Su.z */
        double step_u_tp = ((qx * dV13 + dV18 * qy + dV14 * qz) / dV12) * dua;
        double dV15 = n[0] * E[7] - E[6] * n[1];          /* dVar15 = n.x Su.y - Su.x n.y */
        double dV18b = dV3 * E[10] + dV4 * E[9] + dV15 * E[11];
        if (fabs(dV18b) < 1e-20) dV18b = 1e-20;
        double dV19 = dy * E[7] + dx * E[6] + dz * E[8];
        double dV16 = dy * E[10] + dx * E[9] + dz * E[11];
        double step_v_tp = ((qx * dV4 + dV3 * qy + dV15 * qz) / dV18b) * dva;
        /* the two sums added last (asm 0x1804886c0 .. 0x180488830: the decompile prints one flat sum) */
        double g11 = (E[7] * E[7] + E[6] * E[6] + E[8] * E[8]) + (dy * E[13] + dx * E[12] + dz * E[14]);
        double g22 = (E[10] * E[10] + E[9] * E[9] + E[11] * E[11]) + (dy * E[16] + dx * E[15] + dz * E[17]);
        double g12 = (E[7] * E[10] + E[9] * E[6] + E[8] * E[11]) + (dy * E[19] + dx * E[18] + dz * E[20]);
        double det = 1.0 / (g22 * g11 - g12 * g12);
        double nu = (g22 * dV19 - g12 * dV16) * det * dua, nv = (g11 * dV16 - g12 * dV19) * det * dva;
        double su_, sv_;
        if (has_deg(s)) {
            if (0x13 < it) { su_ = -nu; sv_ = -nv; }
            else { su_ = (-nu + step_u_tp) * 0.5; sv_ = (-nv + step_v_tp) * 0.5; }
        } else {
            su_ = fabs(-nu) <= fabs(step_u_tp) ? -nu : step_u_tp;
            sv_ = fabs(-nv) <= fabs(step_v_tp) ? -nv : step_v_tp;
        }
        double st_u = su_; if (ru < fabs(su_)) { lim = 1; st_u = su_ < 0.0 ? -ru : ru; }
        double st_v = sv_; if (rv < fabs(sv_)) { lim = 1; st_v = sv_ < 0.0 ? -rv : rv; }
        if (has_deg(s)) {
            double q1 = ru * 0.25; if (q1 < fabs(st_u)) { lim = 1; st_u = st_u < 0.0 ? ru * -0.25 : q1; }
            q1 = rv * 0.25; if (q1 < fabs(st_v)) { lim = 1; st_v = st_v < 0.0 ? rv * -0.25 : q1; }
        }
        double us = st_u, vs = st_v;
        if (0 < it && !lim) {
            if (1e-20 <= fabs(st_u - dsu)) { double c = fabs((u - u0p) / (st_u - dsu)) * st_u; if (fabs(c) < ru) us = c; }
            if (1e-20 <= fabs(st_v - dsv)) { double c = fabs((v - v0p) / (st_v - dsv)) * st_v; if (fabs(c) < rv) vs = c; }
        }
        double nu2 = u + us, nv2 = v + vs;
        dsu = st_u; dsv = st_v; u0p = u; v0p = v;
        if (has_deg(s)) { if (s->deg[0] && nu2 < s->d[0]) nu2 = s->d[0]; if (s->deg[1] && s->d[1] < nu2) nu2 = s->d[1]; if (s->deg[2] && nv2 < s->d[2]) nv2 = s->d[2]; if (s->deg[3] && s->d[3] < nv2) nv2 = s->d[3]; }
        if (0x1e < it && fabs(nu2 - u) < 1e-10 && fabs(nv2 - v) < 1e-10) {
            u = nu2; v = nv2; if (limit) limit_uv(s, &u, &v);
            ext_eval(s, 1, u, v, E); { double l = len(E + 3); if (l != 0) { E[3] /= l; E[4] /= l; E[5] /= l; } }
            *uo = u; *vo = v; *dist = d3(E, P); memcpy(out, E, 48);
            if (*uo < s->d[0] || (s->d[1] <= *uo && *uo != s->d[1]) || *vo < s->d[2] || (s->d[3] <= *vo && *vo != s->d[3])) return -1;
            if (uv_on_edge(s, u, v) && limit == 1) return 0;
            return 1;
        }
        u = nu2; v = nv2;
    }
    if (limit) limit_uv(s, &u, &v);
    ext_eval(s, 1, u, v, E); { double l = len(E + 3); if (l != 0) { E[3] /= l; E[4] /= l; E[5] /= l; } }
    *uo = u; *vo = v; *dist = d3(E, P); memcpy(out, E, 48);
    if (*uo < s->d[0] || (s->d[1] <= *uo && *uo != s->d[1]) || *vo < s->d[2] || (s->d[3] <= *vo && *vo != s->d[3])) return -1;
    if (uv_on_edge(s, u, v) && limit == 1) return 0;
    return 1;
}

/* MaMinMaxBox: the box of points, expanded by 1e-4 inch / 0.0025 mm (Expand 0x1804399a0); operator&& (0x1804390b0): the plane has box corners on both sides (or one on it and one off it) */
#ifdef MI_TRACE
#undef is_pmd
#endif
void box_add(double b[6], const double *p) { for (int k = 0; k < 3; k++) { if (p[k] < b[k]) b[k] = p[k]; if (b[3 + k] < p[k]) b[3 + k] = p[k]; } }
void box_init(double b[6]) { b[0] = b[1] = b[2] = 1e31; b[3] = b[4] = b[5] = -1e31; }
void box_expand(double b[6], int metric) { double e = metric ? 0.0025 : 1e-4; for (int k = 0; k < 3; k++) { b[k] -= e; b[3 + k] += e; } }
int box_plane(const double b[6], const double *pl) {
    int neg = 0, zero = 0, pos = 0;
    for (int i = 0; i < 8; i++) { double c[3] = { b[(i & 1) ? 3 : 0], b[(i & 2) ? 4 : 1], b[(i & 4) ? 5 : 2] }, f = (c[1] * pl[1] + c[0] * pl[0] + c[2] * pl[2]) - pl[3];
        if (f < 0) neg = 1; else if (f == 0) zero = 1; else pos = 1; }
    return (neg && pos) || (zero && (pos || neg));
}
/* ---- MaSurfaceIsoCurve (0x180490f70) ---- */
void iso_eval(const Iso *c, int order, double t, double *o);
/* ---- MaCurve::BinaryPointsAtTol 0x1803fe780 in the angle mode GenCrvPoints 0x18040bc20 asks for (tolerance 0, param_3 = 1, no points):
 * the span [T[i], T[i + 1]] is kept when, at 0.25 / 0.5 / 0.75 of it, the unit tangents at its ends (TS carried from the previous span's end,
 * TE from a stack of pending ends) are within the angle of each other and of the sample's, and the chords start -> first sample and last
 * sample -> end are within the angle of the end tangents; else the end is pushed, the span's end becomes the 0.5 sample (or, when the first
 * sample failed, the evaluation at (b - a) 0.5 + a) while the list gets (b + a) 0.5. cap points at most (-1). A result of 3 points on a curve
 * whose ends are within 1e-12 is redone with half the angle (3 times at most). The unit vectors by division; the radian factor .rdata
 * 0x180d018a8 = 0.01745329251994329 (not the double nearest pi / 180); the last chord's length sums y x z (as the decompile). ---- */
typedef void (*BpEv)(const void *ctx, int order, double t, double *o);   /* MaCurve::Evaluate: o[0..2] P, o[3..5] the derivative */
static void bp_unit(const double *d, double *u) { double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (l == 0.0) { u[0] = u[1] = u[2] = 0.0; } else { u[0] = d[0] / l; u[1] = d[1] / l; u[2] = d[2] / l; } }
static int bin_pts(BpEv ev, const void *ctx, double t0, double t1, double ang, int cap, double *T) {
    static const double F[3] = { 0.25, 0.5, 0.75 };
    double (*SE)[3] = malloc(sizeof(double[3]) * cap), (*STE)[3] = malloc(sizeof(double[3]) * cap);
    for (int redo = 0;;) {
        double cs = cos(ang * 0.01745329251994329), S[3], TS[3], E[3], TE[3], o[21];
        int n = 2, depth = 0, i = 0; T[0] = t0; T[1] = t1;
        ev(ctx, 1, T[0], o); memcpy(S, o, 24); bp_unit(o + 3, TS);
        ev(ctx, 1, T[1], o); memcpy(E, o, 24); bp_unit(o + 3, TE);
        while (i < n - 1) {
            int fail = 0, mid = 0; double Q0[3] = { 0, 0, 0 }, Q2[3] = { 0, 0, 0 }, QM[3] = { 0, 0, 0 }, TM[3] = { 0, 0, 0 };
            for (int k = 0; k < 3 && !fail; k++) {
                double Q[21], u[3]; ev(ctx, 1, (T[i + 1] - T[i]) * F[k] + T[i], Q);
                if (k == 0) memcpy(Q0, Q, 24);
                else if (k == 1) { mid = 1; memcpy(QM, Q, 24); bp_unit(Q + 3, TM); }
                else memcpy(Q2, Q, 24);
                if (!(cs <= fabs(TE[0] * TS[0] + TE[1] * TS[1] + TE[2] * TS[2]))) { fail = 1; break; }
                bp_unit(Q + 3, u);
                if (!(cs <= fabs(TS[0] * u[0] + u[1] * TS[1] + u[2] * TS[2])) || !(cs <= fabs(TE[0] * u[0] + u[1] * TE[1] + u[2] * TE[2]))) { fail = 1; break; }
                if (k == 0) {
                    double dx = Q0[0] - S[0], dy = Q0[1] - S[1], dz = Q0[2] - S[2], l = sqrt(dx * dx + dy * dy + dz * dz), w[3] = { 0, 0, 0 };
                    if (l != 0.0) { w[0] = dx / l; w[1] = dy / l; w[2] = dz / l; }
                    if (!(cs <= fabs(TS[0] * w[0] + TS[1] * w[1] + TS[2] * w[2]))) fail = 1;
                } else if (k == 2) {
                    double dx = E[0] - Q2[0], dy = E[1] - Q2[1], dz = E[2] - Q2[2], l = sqrt(dy * dy + dx * dx + dz * dz), w[3] = { 0, 0, 0 };
                    if (l != 0.0) { w[0] = dx / l; w[1] = dy / l; w[2] = dz / l; }
                    if (!(cs <= fabs(TE[0] * w[0] + TE[1] * w[1] + TE[2] * w[2]))) fail = 1;
                }
            }
            if (!fail) {
                memcpy(S, E, 24); memcpy(TS, TE, 24);
                if (0 < depth) { depth--; memcpy(E, SE[depth], 24); memcpy(TE, STE[depth], 24); }
                i++; continue;
            }
            memcpy(SE[depth], E, 24); memcpy(STE[depth], TE, 24); depth++;
            if (mid) { memcpy(E, QM, 24); memcpy(TE, TM, 24); }
            else { ev(ctx, 1, (T[i + 1] - T[i]) * 0.5 + T[i], o); memcpy(E, o, 24); bp_unit(o + 3, TE); }
            memmove(T + i + 2, T + i + 1, sizeof(double) * (n - i - 1)); n++;
            if (cap <= n) { free(SE); free(STE); return -1; }
            T[i + 1] = (T[i + 2] + T[i]) * 0.5;
        }
        if (n == 3 && redo < 3) {
            redo++; double a[21], b[21]; ev(ctx, 0, t0, a); ev(ctx, 0, t1, b);
            double d = sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
            if (0.0 <= d && d <= 1e-12) { ang = ang * 0.5; continue; }
        }
        free(SE); free(STE); return n;
    }
}
static void bp_iso_ev(const void *ctx, int order, double t, double *o) { iso_eval(ctx, order, t, o); }
static void bp_cv_ev(const void *ctx, int order, double t, double *o) { cv_eval(ctx, order, t, o); }
static void bp_cub_ev(const void *ctx, int order, double t, double *o) { cub_ev(ctx, order, t, o); }

void iso_set(Iso *c, ISrf *s, int dir, double par) { c->s = s; c->dir = dir; c->par = par; if (dir == 1) { c->t0 = s->d[2]; c->t1 = s->d[3]; } else { c->t0 = s->d[0]; c->t1 = s->d[1]; } c->nh = 0; c->hasbox = 0; c->ci = 0; }
/* order 0 or 1 as EvaluateCrv: out [0..2] P; order 1: [3..5] the iso direction's derivative; order -1 also [12..14] = the surface normal (+0x60) */
void iso_eval(const Iso *c, int order, double t, double *o) {
    double E[21]; int a = order < 0 ? -order : order;
    if (c->dir == 1) is_eval(c->s, a, c->par, t, E); else is_eval(c->s, a, t, c->par, E);
    memcpy(o, E, 24); if (a > 0) memcpy(o + 3, c->dir == 1 ? E + 9 : E + 6, 24); if (order < 0) memcpy(o + 12, E + 3, 24);
}
/* MaCurveHuntingPoints::GenCrvPoints (0x18040bc20): BinaryPointsAtTol(0, 10 deg, mode 1, 5000); fewer than 5 points: 5 uniform */
void iso_tangent(const Iso *c, double t, double *p, double *T) { double o[21]; iso_eval(c, 1, t, o); memcpy(p, o, 24); memcpy(T, o + 3, 24); }
void iso_hunt(Iso *c) {
    if (c->nh) return;
    double *T = malloc(sizeof(double) * 5000); int n = bin_pts(bp_iso_ev, c, c->t0, c->t1, 10.0, 5000, T);   /* BinaryPointsAtTol(0, 10, 1, 5000) */
    if (n < 0) { n = 20; double x = c->t0, d = c->t1 - c->t0; for (int i = 0; i < 20; i++) { T[i] = x; x = x + d / 19.0; } }   /* failure: 20 uniform */
    else if (n < 5) { n = 5; double x = c->t0, d = c->t1 - c->t0; for (int i = 0; i < 5; i++) { T[i] = x; x = x + d * 0.25; } }   /* GenCrvPoints 0x18040bc20: x += range * 0.25 */
    c->nh = n; c->hu = T;
    /* MaMinMaxBox::GenCrvBox (0x180439a30): the hunting points and the middles of their intervals */
    box_init(c->box); double E[21];
    for (int i = 0; i < n; i++) { iso_eval(c, 0, T[i], E); box_add(c->box, E); }
    for (int i = 0; i < n - 1; i++) { iso_eval(c, 0, (T[i + 1] + T[i]) * 0.5, E); box_add(c->box, E); }
    box_expand(c->box, c->s->metric); c->hasbox = 1;
}
/* MaCurve::IsClosed(-1) through GetClosedInfo (0x1804022b0): the ends within 1e-4 inch / 0.0025 mm and the start farther from the middle point than from the end */
int iso_closed(const Iso *c) {
    /* Evaluate(1) at the ends, Evaluate(0) at TMid (vtable +0x100); closed unless the gap is above tol or the middle is not farther than the end;
       the result cached in the curve (IsClosed 0x180404090 asks GetClosedInfo once, until SetDirty) */
    if (c->ci) return c->cl;
    double a[21], b[21], m[21], tol = c->s->metric ? 0.0025 : 1e-4; iso_eval(c, 1, c->t0, a); iso_eval(c, 1, c->t1, b); iso_eval(c, 0, (c->t0 + c->t1) * 0.5, m);
    double d = d3(a, b); int r = !(tol < d) && d < d3(a, m);
    ((Iso *)c)->ci = 1; ((Iso *)c)->cl = r; return r;
}
/* MaCurve::PlaneIntersect (0x180406bd0). hunt = param_1; t[] the parameters found (t[0] = the start value when !hunt), *n their count. The bounding box reject is left out: the sign search finds nothing either way when the plane misses the box (only the 2 x 1e-6 inch end test differs) */
void plane_intersect(Iso *c, int hunt, const double *pl, int cap, double *t, int *n) {
    double tol = c->s->metric ? 2.5e-05 : 1e-06, tol9 = c->s->metric ? 2.5e-08 : 1e-09, E[21];
    *n = 0;
    iso_hunt(c); if (!box_plane(c->box, pl)) return;   /* GetMinMaxBox && plane */
    double d1 = c->t1, d2 = c->t0, range = c->t1 - c->t0, tcur, d5 = 0, d6 = 0, d19 = range, d16, Pz[3], dT[3], L2c8 = 0, L2c0 = 0, xx = 0;
    double local_2f0 = d1, local_2f8 = range; int b9 = 0, i11;
    if (hunt) {
        iso_hunt(c);
        tcur = c->hu[0]; iso_eval(c, 0, tcur, E);
        d6 = dot(E, pl) - pl[3]; i11 = 1;
        if (1 < c->nh) {
            d16 = d6;
            do { double t4 = c->hu[i11]; iso_eval(c, 0, t4, E); double d17 = dot(E, pl) - pl[3]; d5 = d17 * d16; if (d5 <= 0.0) break; i11++; tcur = t4; d16 = d17; } while (i11 < c->nh);
        }
        if (tol9 < d5) {
            if (fabs(d6) <= tol * 2.0) { t[*n] = c->t0; (*n)++; }
            if (tol * 2.0 < fabs(d5)) return;
            t[*n] = c->t1; (*n)++; return;
        }
    } else tcur = t[0];
    d19 = d19 * 0.025;
    d5 = tcur - d19; if (d5 < d2) d5 = d2;
    double L2b8 = d19, L2b0 = 0, L2a8 = 0, L2a0;
    iso_eval(c, 0, d5, E);
    d2 = dot(E, pl) - pl[3];
    for (;;) {
        d5 = d5 + d19;
        if (!(d5 <= d1 + d19)) return;
        iso_eval(c, 0, d5, E);
        L2b0 = dot(E, pl) - pl[3]; d16 = L2b0;
        if (!b9) {
            if (tol9 < L2b0 * d2) goto LAB_3fb;
            if (local_2f8 * 0.0051 <= d19) { d5 = d5 - d19; d19 = local_2f8 * 0.005; d16 = d2; goto LAB_3fb; }
            L2a8 = d5 - d19; i11 = 0; L2a0 = local_2f8 * 0.0001; d5 = d5 - local_2f8 * 0.0025;
            do {
                d1 = d5;
                { double h = L2a0, P[3][3]; int k = 0; d5 = h;
                  for (k = 0; k < 3; k++) { double dd = d5; if (k == 2) dd = 0.0; iso_eval(c, k == 2 ? 1 : 0, d1 - dd, E); d5 = -dd; memcpy(P[k], E, 24); if (k == 2) { memcpy(Pz, E, 24); memcpy(dT, E + 3, 24); } d2 = dd; }
                  d5 = pl[1];
                  double d18 = (P[2][0] * pl[0] + P[2][1] * pl[1] + P[2][2] * pl[2]) - pl[3];
                  if (fabs(d18) < tol9) {
                      unit(dT);
                      if (fabs(dot(dT, pl)) <= 1e-12) goto LAB_3c9;
                      if (!iso_closed(c) || *n < 1) goto LAB_3ad;
                      for (int q = 0; q < *n; q++) { double Q[21]; iso_eval(c, 0, t[q], Q); if (d3(Q, Pz) <= tol) goto LAB_3c9; }
                      goto LAB_3ad;
                  }
                  double dd19 = ((P[1][0] * pl[0] + P[1][1] * pl[1] + P[1][2] * pl[2]) - pl[3]) - ((P[0][0] * pl[0] + P[0][1] * pl[1] + P[0][2] * pl[2]) - pl[3]);
                  if (fabs(dd19) <= 1e-20 && fabs(dd19) != 1e-20) dd19 = 1e-20;
                  dd19 = (local_2f8 * 0.0002 * d18) / dd19;
                  if ((0 < i11) && (1e-20 <= fabs(dd19 - L2c8))) { double d16b = fabs((d1 - L2c0) / (dd19 - L2c8)) * dd19, da = fabs(d16b); if ((0.0 < da) && (da < range)) dd19 = d16b; }
                  d5 = d1 - dd19; if (d1 - dd19 < L2a8) d5 = L2a8; if (local_2f0 < d5) d5 = local_2f0;
                  L2c8 = dd19; L2c0 = d1; xx = dd19; }
                i11++;
            } while (i11 < 0x1e);
            goto LAB_3d7;
        }
        b9 = 0;
        goto LAB_3fb;
    LAB_3ad:
        t[*n] = d1 - d2; (*n)++; if (*n == cap) return;
    LAB_3c9:
        d5 = d1;
    LAB_3d7:
        b9 = 1; d5 = d5 + -d2; d19 = L2b8; d16 = L2b0; d1 = local_2f0;
    LAB_3fb:
        d2 = d16;
    }
    (void)xx;
}
/* MaPolyLine::PlaneVectorIntersect (0x1804724e0) in 2D: the line through (u,v) with direction d and the side's plane */
int pvi(const double pl[4], const double p[3], const double d[3], double o[3]) {
    double dn = pl[1] * d[1] + pl[0] * d[0] + pl[2] * d[2]; if (!(1e-12 < fabs(dn) || fabs(dn) == 1e-12)) return 1;
    double k = ((pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2]) - pl[3]) / dn; for (int i = 0; i < 3; i++) o[i] = p[i] - k * d[i]; return 0;
}
/* MaSurface::BoundaryClosestToUVDirection (0x18047f760) */
void bound_dir(ISrf *s, const double uv[2], const double dir[2], int *e, double out[2]) {
    double dl = sqrt(dir[0] * dir[0] + dir[1] * dir[1]), du = 0, dv = 0; if (dl != 0) { du = dir[0] / dl; dv = dir[1] / dl; }
    double p[3] = { uv[0], uv[1], 0 }, d3v[3] = { du, dv, 0 }, h[4], Q[4][3];
    *e = 0; out[0] = s->d[0]; out[1] = s->d[2];
    for (int i = 0; i < 4; i++) {
        double A[3] = { i == 1 ? s->d[1] : s->d[0], i == 3 ? s->d[3] : s->d[2], 0 }, B[3], pl[4];
        if (i < 2) { A[0] = B[0] = i ? s->d[1] : s->d[0]; A[1] = s->d[2]; B[1] = s->d[3]; B[2] = 0; pl[0] = 1; pl[1] = 0; pl[2] = 0; pl[3] = A[0]; }   /* local_218 (1,0,0), local_200 = the side's u */
        else { A[1] = B[1] = i == 2 ? s->d[2] : s->d[3]; A[0] = s->d[0]; B[0] = s->d[1]; B[2] = 0; pl[0] = 0; pl[1] = 1; pl[2] = 0; pl[3] = A[1]; }
        if (pvi(pl, p, d3v, Q[i]) == 0) {
            double ex = B[0] - A[0], ey = B[1] - A[1], l = sqrt(ex * ex + ey * ey);
            double qx = Q[i][0] - A[0], qy = Q[i][1] - A[1], ql = sqrt(qx * qx + qy * qy);
            double nqx = 0, nqy = 0; if (ql != 0) { nqx = qx / ql; nqy = qy / ql; }
            double nex = 0, ney = 0; if (l != 0) { nex = ex / l; ney = ey / l; }
            double r = ((nqx * nex + nqy * ney) * ql) / l;
            if (r < -1e-09 || 1.000000001 < r) h[i] = -1e31;
            else h[i] = (du * Q[i][0] + dv * Q[i][1]) - (du * uv[0] + dv * uv[1]);
        } else h[i] = -1e31;
    }
    double best = 1e31;
    for (int i = 0; i < 4; i++) if (0.0 < h[i]) {
        best = 1e31;
        if (0.0 < h[0] && h[0] < 1e31) { out[0] = Q[0][0]; out[1] = Q[0][1]; *e = 1; best = h[0]; }
        if (0.0 < h[1] && h[1] < best) { out[0] = Q[1][0]; out[1] = Q[1][1]; *e = 2; best = h[1]; }
        if (0.0 < h[2] && h[2] < best) { out[0] = Q[2][0]; out[1] = Q[2][1]; *e = 3; best = h[2]; }
        if (0.0 < h[3] && h[3] < best) { out[0] = Q[3][0]; out[1] = Q[3][1]; *e = 4; }
        return;
    }
    if (h[0] != -1e31 && fabs(h[0]) < 1e31) { out[0] = Q[0][0]; out[1] = Q[0][1]; *e = 1; best = fabs(h[0]); }
    if (h[1] != -1e31 && fabs(h[1]) < best) { out[0] = Q[1][0]; out[1] = Q[1][1]; *e = 2; best = fabs(h[1]); }
    if (h[2] != -1e31 && fabs(h[2]) < best) { out[0] = Q[2][0]; out[1] = Q[2][1]; *e = 3; best = fabs(h[2]); }
    if (h[3] != -1e31 && fabs(h[3]) < best) { out[0] = Q[3][0]; out[1] = Q[3][1]; *e = 4; }
}
/* MaSurface::UnitNormalEvaluate (0x18048f160), order 1: dN/du, dN/dv into o[0..5] */
void unit_normal_ev(const double *E, double *o) {
    double Su[3] = { E[6], E[7], E[8] }, Sv[3] = { E[9], E[10], E[11] }, Suu[3] = { E[12], E[13], E[14] }, Svv[3] = { E[15], E[16], E[17] }, Suv[3] = { E[18], E[19], E[20] };
    double N[3] = { Sv[2] * Su[1] - Sv[1] * Su[2], Sv[0] * Su[2] - Su[0] * Sv[2], Su[0] * Sv[1] - Sv[0] * Su[1] };
    double l = len(N); if (fabs(l) <= 1e-20) l = 1e-20; double inv = 1.0 / l, un[3] = { N[0] * inv, N[1] * inv, N[2] * inv };
    double Nu[3] = { (Suv[2] * Su[1] - Suv[1] * Su[2]) + (Suu[1] * Sv[2] - Suu[2] * Sv[1]), (Suv[0] * Su[2] - Su[0] * Suv[2]) + (Sv[0] * Suu[2] - Suu[0] * Sv[2]), (Su[0] * Suv[1] - Suv[0] * Su[1]) + (Suu[0] * Sv[1] - Sv[0] * Suu[1]) };
    double c1 = un[1] * Nu[1] + Nu[0] * un[0] + un[2] * Nu[2];
    o[0] = (Nu[0] - un[0] * c1) * inv; o[1] = (Nu[1] - un[1] * c1) * inv; o[2] = (Nu[2] - un[2] * c1) * inv;
    double Nv[3] = { (Svv[2] * Su[1] - Svv[1] * Su[2]) + (Sv[2] * Suv[1] - Sv[1] * Suv[2]), (Svv[0] * Su[2] - Su[0] * Svv[2]) + (Sv[0] * Suv[2] - Suv[0] * Sv[2]), (Su[0] * Svv[1] - Svv[0] * Su[1]) + (Suv[0] * Sv[1] - Sv[0] * Suv[1]) };
    double c2 = Nv[0] * un[0] + un[1] * Nv[1] + un[2] * Nv[2];
    o[3] = (Nv[0] - un[0] * c2) * inv; o[4] = (Nv[1] - un[1] * c2) * inv; o[5] = (Nv[2] - un[2] * c2) * inv;
}
/* MaSurface::DuvDtEvaluate (0x180481e00), DeltaU/VAllowable 1 (the normalised range), param_7 = 1 (use the normal's derivative): (du/dt, dv/dt) */
void duvdt(double *E, const double *P, const double *T, double *du, double *dv) {
    double Su[3] = { E[6], E[7], E[8] }, Sv[3] = { E[9], E[10], E[11] };
    E[3] = Sv[2] * Su[1] - Sv[1] * Su[2]; E[4] = Sv[0] * Su[2] - Su[0] * Sv[2]; E[5] = Su[0] * Sv[1] - Sv[0] * Su[1];
    *du = 0; *dv = 0;
    double l = len(E + 3); if (fabs(l) < 1e-20) l = 1e-20;
    double inv = 1.0 / l, n2 = inv * E[5], n1 = inv * E[4], n0 = inv * E[3], a2 = n2 * -1.0, a0 = n0 * -1.0, a1 = n1 * -1.0;
    double dist = (n1 * P[1] + n0 * P[0] + n2 * P[2]) - (n1 * E[1] + n0 * E[0] + n2 * E[2]);
    double o[6]; unit_normal_ev(E, o);
    double e0 = Sv[0] - o[3] * -dist, e1 = Sv[1] - o[4] * -dist, e2 = Sv[2] - o[5] * -dist;
    double f2 = Su[2] - o[2] * -dist, f1 = Su[1] - o[1] * -dist, f0 = Su[0] - o[0] * -dist;
    double dd = a0 * (f1 * e2 - f2 * e1) + (e0 * f2 - f0 * e2) * a1 + (f0 * e1 - e0 * f1) * a2;
    if (fabs(dd) <= 1e-20) dd = 1e-20;
    *du = (a0 * (T[1] * e2 - T[2] * e1) + (e0 * T[2] - T[0] * e2) * a1 + (T[0] * e1 - e0 * T[1]) * a2) / dd;
    *dv = (a0 * (T[2] * f1 - T[1] * f2) + (T[0] * f2 - f0 * T[2]) * a1 + (f0 * T[1] - T[0] * f1) * a2) / dd;
}

/* MaSurface::IsUVNearPole (0x180486c80) */
int uv_near_pole(ISrf *s, const double *p) {
    if (!has_deg(s)) return 0; double E[21], F[21], tol = s->metric ? 0.0025 : 1e-4; is_eval(s, 0, p[0], p[1], E);
    if (s->deg[0]) { is_eval(s, 0, s->d[0], (s->d[3] + s->d[2]) * 0.5, F); if (d3(E, F) <= tol) return 1; }
    if (s->deg[1]) { is_eval(s, 0, s->d[1], (s->d[3] + s->d[2]) * 0.5, F); if (d3(E, F) <= tol) return 1; }
    if (s->deg[2]) { is_eval(s, 0, (s->d[1] + s->d[0]) * 0.5, s->d[2], F); if (d3(E, F) <= tol) return 1; }
    if (s->deg[3]) { is_eval(s, 0, (s->d[1] + s->d[0]) * 0.5, s->d[3], F); if (d3(E, F) <= tol) return 1; }
    return 0;
}

/* ---- the cutting data ---- */
#define D(o) (*(double *)(c + (o)))
#define I(o) (*(int *)(c + (o)))
#define I2 (*(int *)(c + 8))
#define I0 (*(int *)(c))
/* MaSurface::SurfSurfTangentPlane (0x18048d080); statics DAT_1813f59b0[2] */
int lastn[2];   /* DAT_1813f59b0 */
void tangent_plane(ISrf *s, uint8_t *c, int side, double *P, double *plane, int *st) {
    long l8 = side; int it = 0; *st = 0;
    double uu, vv, E[6], dist;
    if (D(0x240) < D(0x238)) { uu = D(l8 * 0x30 + 0x248); vv = D(l8 * 0x30 + 0x260); }
    else { uu = D(l8 * 0x30 + 0x250); vv = D(l8 * 0x30 + 0x268); }
    if (((I2 < 1) || (I0 != -1)) && (I0 + 2 < I(4)) && (I0 != lastn[l8]) && (D(0x240) < D(0x238))) {
        uu = uu + ((uu - D(l8 * 0x30 + 0x250)) * 2.0 - (D(l8 * 0x30 + 0x250) - D(l8 * 0x30 + 600)));
        vv = vv + ((vv - D(l8 * 0x30 + 0x268)) * 2.0 - (D(l8 * 0x30 + 0x268) - D((l8 + 0xd) * 0x30)));
    }
    if (uu < s->d[0]) uu = s->d[0]; if (s->d[1] < uu) uu = s->d[1];
    if (vv < s->d[2]) vv = s->d[2]; if (s->d[3] < vv) vv = s->d[3];
    int r = is_pmd(s, 0, P, (double *)(c + 0x90), &dist, &uu, &vv, 1);
    if (r == 1) { while ((it = it + 1) < 2 && !has_seams(s)) { if (has_deg(s)) break; r = is_pmd(s, 1, P, (double *)(c + 0x90), &dist, &uu, &vv, 1); if (r != 1) break; } }
    (void)E;
    int boundary = 0;
    double xu = uu, xv = vv, b;
    b = s->d[0];
    if ((fabs(uu - b) <= 1e-10 && fabs(uu - b) != 1e-10) || (b = s->d[1], fabs(uu - b) <= 1e-10 && fabs(uu - b) != 1e-10)) { boundary = 1; xu = b; }
    b = s->d[2];
    if (1e-10 < fabs(vv - b) || fabs(vv - b) == 1e-10) {
        b = s->d[3];
        if (1e-10 < fabs(vv - b) || fabs(vv - b) == 1e-10) { if (!boundary) goto skip_eval; }
        else xv = b;
    } else xv = b;
    { double Eb[21]; is_eval(s, 1, xu, xv, Eb); memcpy(c + 0x90, Eb, 48); unit((double *)(c + 0xa8)); }
skip_eval:
    if (xu == s->d[0] || xu == s->d[1]) *st = -1;
    if (xv == s->d[2] || xv == s->d[3]) *st = -1;
    D(l8 * 0x18 + 0x60) = D(0x90); D(l8 * 0x18 + 0x68) = D(0x98); D(l8 * 0x18 + 0x70) = D(0xa0);
    if ((I0 != lastn[l8]) && (I2 != 5)) {
        long l2 = l8 * 6 + 2;
        D(l2 * 8 + 0x248) = D(l8 * 0x30 + 0x250);
        D(l2 * 8 + 0x260) = D(l2 * 8 + 600);
        D(l8 * 0x30 + 0x250) = D(l2 * 8 + 0x238);
        D(l2 * 8 + 600) = D(l2 * 8 + 0x250);
    }
    D(l8 * 0x30 + 0x248) = xu; D(l8 * 0x30 + 0x260) = xv;
    lastn[l8] = I0;
    plane[0] = D(0xa8); plane[1] = D(0xb0); plane[2] = D(0xb8);
    plane[3] = D(0x98) * plane[1] + plane[0] * D(0x90) + plane[2] * D(0xa0);
#ifdef MI_TRACE
    if (mi_tron && mi_trf) fprintf(mi_trf, "TP %d %.17g %.17g %.17g st %d uv %.17g %.17g pl %.17g %.17g %.17g %.17g I0 %d I2 %d\n", side, P[0], P[1], P[2], *st, D(0x248 + side * 0x30), D(0x260 + side * 0x30), plane[0], plane[1], plane[2], plane[3], I0, I2);
#endif
}
/* Invert3X3Matrix (0x1808ec1d0): 0 = done, 1 = determinant below 1e-12; the inverse (rows, the cofactors transposed) */
int inv3(const double *a, double *o) {
    double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);   /* Determinant3X3Matrix */
    if (fabs(det) < 1e-12) return 1; double q = 1.0 / det;
    o[0] = (a[4] * a[8] - a[5] * a[7]) * q; o[3] = (a[6] * a[5] - a[3] * a[8]) * q; o[6] = (a[3] * a[7] - a[6] * a[4]) * q;
    o[1] = (a[2] * a[7] - a[1] * a[8]) * q; o[4] = (a[0] * a[8] - a[2] * a[6]) * q; o[7] = (a[1] * a[6] - a[0] * a[7]) * q;
    o[2] = (a[1] * a[5] - a[2] * a[4]) * q; o[5] = (a[2] * a[3] - a[0] * a[5]) * q; o[8] = (a[0] * a[4] - a[1] * a[3]) * q;
    return 0;
}
/* MaSurface::DeltaUAllowable / DeltaVAllowable (0x180481d80) of a MaNURBSurf (UMaxAllowable 0x180460aa0, UMinAllowable 0x180460ac0: knots [ku - 1] and [ncu]) */
double du_allow(const ISrf *s) { return s->KU[s->ncu] - s->KU[s->ku - 1]; }
double dv_allow(const ISrf *s) { return s->KV[s->ncv] - s->KV[s->kv - 1]; }
/* MaCurve::DeltaTAllowable of an iso curve (MaSurfaceIsoCurve::TMinAllowable / TMaxAllowable 0x180491420 / 0x1804913f0: the other direction's allowable range) */
double dt_allow(const Iso *c) { return c->dir == 1 ? dv_allow(c->s) : du_allow(c->s); }
/* MaSurface::CurveIntersect (0x180481730): Newton on the three parameters: x = t u v; returns 0, 1 = not converged (50 steps or 3 singular matrices), this = the other surface; dt38 = the curve object's +0x38 */
#ifdef MI_TRACE
static int curve_intersect_(ISrf *ss, const Iso *iso, double p2, double p3, double *x, double dt38);
int curve_intersect(ISrf *ss, const Iso *iso, double p2, double p3, double *x, double dt38) {
    double x0[3] = { x[0], x[1], x[2] }; int r = curve_intersect_(ss, iso, p2, p3, x, dt38);
    if (mi_tron && mi_trf) fprintf(mi_trf, "CI %.17g %.17g in %.17g %.17g %.17g r %d x %.17g %.17g %.17g\n", p2, p3, x0[0], x0[1], x0[2], r, x[0], x[1], x[2]);
    return r;
}
#undef curve_intersect
#define curve_intersect curve_intersect_
static
#endif
int curve_intersect(ISrf *ss, const Iso *iso, double p2, double p3, double *x, double dt38) {
    double tol = ss->metric ? 2.5e-17 : 1e-18;
    double dta = dt_allow(iso), dua = du_allow(ss), dva = dv_allow(ss);
    double t = x[0], u = x[1], v = x[2], d4 = 0, d5 = 0, d6 = 0, d10 = 0, d11 = 0, L398 = 0, L358 = 0, L370 = 0, L388 = 0, L368 = 0, L378 = 0, L390 = 0;
    int it = 0, sing = 0;
    do {
        double C[21], E[21]; iso_eval(iso, 1, t, C); ext_eval(ss, 1, u, v, E);
        double R[3] = { C[0] - E[0], C[1] - E[1], C[2] - E[2] };
#ifdef SPINT_TRACE
        if (getenv("SSINT_CIIT")) fprintf(stderr, "IT %d t %.17g u %.17g v %.17g |R|^2 %.5e C %.15g %.15g %.15g E %.15g %.15g %.15g T %.10g %.10g %.10g Su %.10g %.10g %.10g\n", it, t, u, v, R[0]*R[0]+R[1]*R[1]+R[2]*R[2], C[0],C[1],C[2],E[0],E[1],E[2],C[3],C[4],C[5],E[6],E[7],E[8]);
#endif
        if (R[0] * R[0] + R[1] * R[1] + R[2] * R[2] <= tol) goto converged;
        if (10 < it) {
            double uvp[2] = { u, v };
            if (uv_near_pole(ss, uvp) && (fabs(d4) <= 1e-05 && fabs(d4) != 1e-05) && (fabs(d5) <= 1e-05 && fabs(d5) != 1e-05) && (fabs(d6) <= 1e-05 && fabs(d6) != 1e-05)) goto converged;
        }
        double q = 1.0 / dta, r = -1.0 / dua, sq = -1.0 / dva;
        double M[9] = { C[3] * q, C[4] * q, C[5] * q, E[6] * r, E[7] * r, E[8] * r, E[9] * sq, E[10] * sq, E[11] * sq }, A[9];
        double d24, d26, d12, d13, d14, d15;
        if (inv3(M, A) == 0) {
            d4 = R[0]; d5 = R[1]; d6 = R[2];
            d24 = d5 * A[3] + d4 * A[0] + d6 * A[6];
            d11 = d5 * A[4] + d4 * A[1] + d6 * A[7];
            d10 = d5 * A[5] + d4 * A[2] + d6 * A[8];
            d4 = d24; if (dta <= fabs(d24) && fabs(d24) != dta) { d4 = dta; if (d24 < 0.0) d4 = -dta; }
            d5 = d11; if (dua <= fabs(d11) && fabs(d11) != dua) { d5 = dua; if (d11 < 0.0) d5 = -dua; }
            d6 = d10; if (dva <= fabs(d10) && fabs(d10) != dva) { d6 = dva; if (d10 < 0.0) d6 = -dva; }
            d11 = d6; d10 = d5; L398 = d4; d24 = v; d26 = d6; d12 = u; d13 = d5; d14 = t; d15 = d4;
            if (0 < it) {
                if (1e-20 <= fabs(d4 - L358)) { double y = fabs((t - L368) / (d4 - L358)) * d4; if (fabs(y) < dt38) L398 = y; }
                if (1e-20 <= fabs(d5 - L370)) { double y = fabs((u - L378) / (d5 - L370)) * d5; if (fabs(y) < ss->d[4]) d10 = y; }
                if (1e-20 <= fabs(d6 - L388)) { double y = fabs((v - L390) / (d6 - L388)) * d6; if (fabs(y) < ss->d[5]) d11 = y; }
            }
        } else {
            sing++; d24 = L390; d26 = L388; d12 = L378; d13 = L370; d14 = L368; d15 = L358;
            if (2 < sing) { x[0] = t; x[1] = u; x[2] = v; break; }
        }
        L358 = d15; L368 = d14; L370 = d13; L378 = d12; L388 = d26; L390 = d24;
        u = u - d10; v = v - d11;
        d24 = t - L398; if (t - L398 < p2) d24 = p2;
        t = d24; if (p3 < d24) t = p3;
        it++;
        continue;
    converged:
        x[0] = t; x[1] = u; x[2] = v; return 0;
    } while (it < 0x32);
    return 1;
}
#ifdef MI_TRACE
#undef curve_intersect
#define curve_intersect mi_curve_intersect
#endif
/* MaSurface::SurfSurfFinalIntersection (0x180489a30): side 0 = this is the part surface, 1 = the drive surface; other == NULL: the plane, side = 0 */
#ifdef MI_TRACE
static void final_intersect_(ISrf *s, ISrf *other, uint8_t *c, int side);
void final_intersect(ISrf *s, ISrf *other, uint8_t *c, int side) { final_intersect_(s, other, c, side); if (mi_tron && mi_trf) fprintf(mi_trf, "FI %d %.17g %.17g %.17g\n", side, D(0x30), D(0x38), D(0x40)); }
#undef final_intersect
#define final_intersect final_intersect_
static
#endif
void final_intersect(ISrf *s, ISrf *other, uint8_t *c, int side) {
    double uo = D(side * 0x30 + 0x248), u = D(side * 0x30 + 0x250), up = D(side * 0x30 + 600), vo = D(side * 0x30 + 0x260), v = D(side * 0x30 + 0x268), vp = D((side + 0xd) * 0x30);
    double E[21], x[2], hit[2], dirn[2], dT = 0, dU = 0;
    int e = 0, iso_d = 0; double ipar = 0, ostart = 0, ostart2 = 0, oval = 0; int n428 = 0; double t428[101];
    I2 = 5; I0 = -1;
    is_eval(s, 2, u, v, E);
    duvdt(E, (double *)(c + 0x48), (double *)(c + 0x18), &dT, &dU);
    { double l = sqrt(dU * dU + dT * dT); if (l == 0) { dirn[0] = 0; dirn[1] = 0; } else { dirn[0] = dT / l; dirn[1] = dU / l; } }
    x[0] = u; x[1] = v;
    bound_dir(s, x, dirn, &e, hit);
    if (e == 1 || e == 2) { ipar = e == 1 ? s->d[0] : s->d[1]; iso_d = 1; ostart = s->d[2]; ostart2 = s->d[3]; oval = vo; }
    else if (e == 3 || e == 4) { iso_d = 2; ipar = e == 3 ? s->d[2] : s->d[3]; ostart = s->d[0]; ostart2 = s->d[1]; oval = uo; }
    t428[0] = ostart;
    Iso iso = { 0 }; iso_set(&iso, s, iso_d, ipar);
    if (other) {   /* the other surface: CurveIntersect with the seed oval, (u, v) on the other surface */
        double x3[3] = { oval, side == 0 ? D(0x278) : D(0x248), side == 0 ? D(0x290) : D(0x260) };
        if (curve_intersect(other, &iso, ostart, ostart2, x3, ostart2 - ostart) != 0) goto fallback;
        { double o[21]; iso_eval(&iso, -1, x3[0], o); D(0x30) = o[0]; D(0x38) = o[1]; D(0x40) = o[2]; memcpy(c + 0x90, o, 24);
          double n[3] = { o[12], o[13], o[14] }; unit(n); D(0xa8) = n[0]; D(0xb0) = n[1]; D(0xb8) = n[2]; }
        return;
    }
    plane_intersect(&iso, 0, (double *)(c + 0x1f8), 100, t428, &n428);
    if (n428 == 0) goto fallback;
    double tb = t428[0];
    if (n428 > 1) { double best = 1e31; int per = iso_d == 1 ? s->per[1] : s->per[0]; double L = iso.t1 - iso.t0;
        for (int i = 0; i < n428; i++) { double dv_ = oval - t428[i], ad = fabs(dv_);
            int endp = !(L * 5e-05 <= fabs(t428[i] - iso.t0) && L * 5e-05 <= fabs(t428[i] - iso.t1));
            if (per && endp && (fabs(fabs(dv_) - L) < 1e-06 || (fabs(dv_) <= 1e-06 && fabs(dv_) != 1e-06))) ad = 0;
            if (ad < best) { best = ad; tb = t428[i]; } } }
    { double o[21]; iso_eval(&iso, -1, tb, o); D(0x30) = o[0]; D(0x38) = o[1]; D(0x40) = o[2]; memcpy(c + 0x90, o, 24);
      double n[3] = { o[12], o[13], o[14] }; unit(n); D(0xa8) = n[0]; D(0xb0) = n[1]; D(0xb8) = n[2]; }
    return;
fallback: {
    double xu = uo, xv = v;
    int uoe = (uo == s->d[0] || uo == s->d[1]), voe = (vo == s->d[2] || vo == s->d[3]);
    if (uoe && voe) { if (fabs(vp - v) <= fabs(up - u)) { xu = uo; xv = v; } else { xu = u; xv = vo; } }
    else if (!uoe) { if (vo == s->d[2] || (xv = vo, vo == s->d[3])) { xu = u; xv = vo; } }
    double Eb[21]; is_eval(s, 1, xu, xv, Eb); memcpy(c + 0x90, Eb, 48);
    unit((double *)(c + 0xa8));
    double *V90 = (double *)(c + 0x90), *Q = (double *)(c + 0x60 + side * 0x18), Nr[3] = { Eb[3], Eb[4], Eb[5] }, *T18 = (double *)(c + 0x18), *P30 = (double *)(c + 0x30), *P48 = (double *)(c + 0x48);
    double a = (V90[1] - Q[1]) * Nr[2] - (V90[2] - Q[2]) * Nr[1];   /* the raw normal (read before FUN_18000f2f0) */
    double b = (V90[2] - Q[2]) * Nr[0] - Nr[2] * (V90[0] - Q[0]);
    double g = Nr[1] * (V90[0] - Q[0]) - (V90[1] - Q[1]) * Nr[0];
    double l2 = b * b + a * a + g * g;
    if (1e-20 <= l2) {
        double dd = sqrt(l2); dd = 1.0 / dd; double ax = a * dd, ay = b * dd, az = g * dd;
        T18[0] = P30[0] - P48[0]; T18[1] = P30[1] - P48[1]; T18[2] = P30[2] - P48[2];
        double l3 = sqrt(dot(T18, T18));
        if (1e-20 <= l3) {
            l3 = 1.0 / l3; T18[0] *= l3; T18[1] *= l3; T18[2] *= l3;
            double dp = ay * T18[1] + ax * T18[0] + az * T18[2];
            if (1e-20 < fabs(dp) || fabs(dp) == 1e-20) {
                double t2 = ((ay * P48[1] + ax * P48[0] + az * P48[2]) - (ay * V90[1] + ax * V90[0] + az * V90[2])) / dp;
                P30[0] = P48[0] - t2 * T18[0]; P30[1] = P48[1] - t2 * T18[1]; P30[2] = P48[2] - t2 * T18[2];
                goto LAB_1ef;
            }
        }
        P48[0] = P30[0]; P48[1] = P30[1]; P48[2] = P30[2];
    } else {
        P30[0] = V90[0]; P30[1] = V90[1]; P30[2] = V90[2];
    LAB_1ef:
        if (P30[0] * T18[0] + T18[1] * P30[1] + T18[2] * P30[2] < T18[1] * P48[1] + P48[0] * T18[0] + T18[2] * P48[2]) { P30[0] = P48[0]; P30[1] = P48[1]; P30[2] = P48[2]; }
    }
  }
}
#ifdef MI_TRACE
#undef final_intersect
#define final_intersect mi_final_intersect
#endif
/* MaLine::PointMinDist 0x18042ba60 on 3D points (the lengths sum y x z: asm 0x18042baee .. 0x18042bafc) */
void line_pmd(const double *a, const double *b, const double *q, double *dist, double *t) {
    double e[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, L = sqrt(e[1] * e[1] + e[0] * e[0] + e[2] * e[2]), r[3] = { q[0] - a[0], q[1] - a[1], q[2] - a[2] }, R = sqrt(r[1] * r[1] + r[0] * r[0] + r[2] * r[2]);
    if (1e-06 < L) {
        double u[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 }; if (R != 0) { u[0] = r[0] / R; u[1] = r[1] / R; u[2] = r[2] / R; } if (L != 0) { n[0] = e[0] / L; n[1] = e[1] / L; n[2] = e[2] / L; }
        double d = n[1] * u[1] + n[0] * u[0] + n[2] * u[2], pr = d * R;
        if (fabs(d) == 1.0 || R == 0.0) *dist = 0; else { double a2 = 1.0 - d * d; *dist = 1e-20 <= a2 ? sqrt(a2) * R : 0; }
        if (1e-20 < L) { *t = pr / L; return; }
        *t = 0; return;
    }
    *dist = R; *t = 0;
}
/* MaSurface::SurfSurfMarchIntersection (0x18048aa50), param_1 == NULL, param_9 = true. Line numbers in comments are of the compact printout in tools/ (labels kept) */
int march(ISrf *th, ISrf *dr, uint8_t *c, double **L558, double **L4b8, double **L560, double **L568, double **L518, double **L510, int *L508, char *L528, char *L3e8) {
    int L600 = 1, L5d0, L5cc, L5b0, L5c0, L5b4 = 0, L5b8 = 0, L548 = 0, L5bc = 0, L5e0 = 0, L570 = 0, L588 = 0, L4d0, ret, L5ac, iV15;
    char b14 = 0, b22 = 0, b10 = 0, *L528p = L528; double L3c8[3], L3b0, L3b8_, L460[3] = { 0, 0, 0 }, L498[3] = { 0, 0, 0 }, L430[3], L4b0[2] = { 0, 0 }, L418, L550;
    double L590 = 0, L598 = 0, L5a0 = 0, d28 = 0, d29 = 0, d30, d31, d32 = 0, d33, d34, d35 = 0, d1, d2, L5a8 = 0, L520 = 0;
    double E[21], L4c8[2];
    double *ppV23 = (double *)(c + 0x30), *pS21 = (double *)(c + 0x18);
    *L3e8 = 0; L5cc = I(4) + 1; I0 = L5cc; I2 = -4; L5d0 = -4; L4d0 = I(4) - 1; L550 = D(0x230);
    D(0x230) = D(0x228); D(0x238) = 0; D(0x240) = 0; L5b0 = -4; L5c0 = -4; *L528 = 0;
    L430[0] = D(0x30); L430[1] = D(0x38); L430[2] = D(0x40);
    { double v[3] = { D(0x18), D(0x20), D(0x28) }, l = len(v); L3c8[0] = v[0] / l; L3c8[1] = v[1] / l; L3b8_ = v[2] / l; if (l == 0) { L3c8[0] = L3c8[1] = L3b8_ = 0; } }
    double L3c0[3] = { L3c8[0], L3c8[1], L3b8_ };
    L3b0 = L3c0[0] * L430[0] + L430[1] * L3c0[1] + L430[2] * L3c0[2];
    L418 = th->metric ? 50000.0 : 2000.0;
    d31 = 0; d30 = L418; L5ac = 2; L598 = 0; L590 = 0; d35 = 0; d28 = 0;
LAB_ad2a:
    if (b22) {
LAB_ad8c:
        b22 = 0;
        L498[0] = L460[0] * D(0x238) + D(0x48); L498[1] = L460[1] * D(0x238) + D(0x50); L498[2] = L460[2] * D(0x238) + D(0x58);
        D(0x30) = d28 + L498[0]; D(0x38) = d35 + L498[1]; D(0x40) = d31 + L498[2];
        for (;;) {
            tangent_plane(th, c, 0, ppV23, (double *)(c + 0x1d8), &L570); I(0xc) = L570;
            if (dr) { tangent_plane(dr, c, 1, ppV23, (double *)(c + 0x1f8), &L588); I(0x10) = L588; }
            if ((I(4) <= I0) && (I(4) <= I0 + 1) && D(0x238) != D(0x228)) { I(0xc) = 0; I(0x10) = 0; }
            if (I(0xc) == -1 && b14) {
                d31 = D(0x248); d35 = D(0x260);
                if (0 < L5bc) {
                    if (L5bc < 3) { if (((d31 == th->d[0]) || (d31 == th->d[1])) && (th->d[2] <= d35 && d35 != th->d[2])) { int be = th->d[3] == d35, bg = th->d[3] < d35; if (!bg && !be) I(0xc) = 0; } }
                    else if (L5bc < 5 && ((d35 == th->d[2] || d35 == th->d[3]) && (th->d[0] <= d31 && d31 != th->d[0]))) { int be = th->d[1] == d31, bg = th->d[1] < d31; if (!bg && !be) I(0xc) = 0; }
                }
            }
            if (dr && I(0x10) == -1 && b14) {
                d31 = D(0x278); d35 = D(0x290);
                if (0 < L5e0) {
                    if (L5e0 < 3) { if (((d31 == dr->d[0]) || (d31 == dr->d[1])) && (dr->d[2] <= d35 && d35 != dr->d[2])) { int be = dr->d[3] == d35, bg = dr->d[3] < d35; if (!bg && !be) I(0x10) = 0; } }
                    else if (L5e0 < 5 && ((d35 == dr->d[2] || d35 == dr->d[3]) && (dr->d[0] <= d31 && d31 != dr->d[0]))) { int be = dr->d[1] == d31, bg = dr->d[1] < d31; if (!bg && !be) I(0x10) = 0; }
                }
            }
            if (((0 < I(0xc)) || (0 < I(0x10)) || ((0 < I2) && (I0 == -1))) || (I2 != 0)) goto LAB_b438;
            if ((I(0xc) == -1) && (I(0x10) == -1)) {   /* both at a boundary: the nearer boundary point decides which one is ignored (DuvDtEvaluate at the last point, BoundaryClosestToUVDirection) */
                double uvb[2] = { D(0x250), D(0x268) }, E2[21], dU = 0, dV = 0, dn[2], hit[2], d0x, d1x; int eb = 0;
                is_eval(th, 2, uvb[0], uvb[1], E2); duvdt(E2, (double *)(c + 0x48), (double *)(c + 0x18), &dU, &dV);
                { double l = sqrt(dV * dV + dU * dU); if (l == 0) { dn[0] = 0; dn[1] = 0; } else { dn[0] = dU / l; dn[1] = dV / l; } }
                bound_dir(th, uvb, dn, &eb, hit); is_eval(th, 0, hit[0], hit[1], E2); d0x = d3((double *)(c + 0x48), E2);
                uvb[0] = D(0x280); uvb[1] = D(0x298);
                is_eval(dr, 2, uvb[0], uvb[1], E2); duvdt(E2, (double *)(c + 0x48), (double *)(c + 0x18), &dU, &dV);
                { double l = sqrt(dV * dV + dU * dU); if (l == 0) { dn[0] = 0; dn[1] = 0; } else { dn[0] = dU / l; dn[1] = dV / l; } }
                bound_dir(dr, uvb, dn, &eb, hit); is_eval(dr, 0, hit[0], hit[1], E2); d1x = d3((double *)(c + 0x48), E2);
                if (d1x <= d0x) I(0xc) = 0; else I(0x10) = 0;
            }
            if ((I(0xc) == 0) || (L600 < 3)) {
                if ((I(0x10) == 0) || !dr || (L600 < 3)) goto LAB_b438;
                L5d0 = I2; L5cc = I0; *L528p = 1;
                final_intersect(dr, th, c, 1);
            } else {
                L5d0 = I2; L5cc = I0; *L528p = 1;
                final_intersect(th, dr, c, 0);
            }
        }
    }
    D(0x48) = D(0x30); D(0x50) = D(0x38); D(0x58) = D(0x40);
    L460[0] = D(0x18); L460[1] = D(0x20); L460[2] = D(0x28);
    I(0xc) = 0; I(0x10) = 0;
    if (L4d0 == I0) D(0x230) = L550;
    iV15 = I0 - 1; I0 = iV15;
    if (0 < iV15) goto LAB_ad8c;
    goto LAB_cf73;
LAB_b438:
    if ((0 < I(0xc)) || (0 < I(0x10))) { *L508 = L600; D(0x230) = L550; ret = 1; goto out; }
    d31 = D(0x200) * D(0x1e0) + D(0x1f8) * D(0x1d8) + D(0x1e8) * D(0x208);
    d35 = 1.0 - d31 * d31;
    if (d35 < 1e-20) { *L508 = L600; D(0x230) = L550; ret = 2; goto out; }
    d28 = sqrt(d35);
    d32 = D(0x1e8) * D(0x1f8) - D(0x1d8) * D(0x208);
    { double st = D(0x200) * D(0x1d8) - D(0x1e0) * D(0x1f8);
      pS21[0] = D(0x1e0) * D(0x208) - D(0x200) * D(0x1e8); pS21[1] = d32; pS21[2] = st; }
    d28 = 1.0 / (d28 * D(0x218));
    pS21[0] *= d28; pS21[1] *= d28; pS21[2] *= d28;
    iV15 = L5e0;
    if ((I2 < -1) || b14) { if (b14) goto LAB_b9ec; }
    else {
        if (I(0xc) == -1) {
            double u_ = D(0x248), v_ = D(0x260); double L4f0x[3] = { 0, 0, 0 };
            is_eval(th, 1, u_, v_, E);
            L4c8[0] = u_; L4c8[1] = v_;
            if ((u_ == th->d[0] || u_ == th->d[1]) && (v_ == th->d[2] || v_ == th->d[3])) { L4c8[0] = D(0x250); L4c8[1] = D(0x268); }
            boundary_uvpt(th, L4c8, &L5bc, &L5a8);
            if (0 < L5bc) {
                if (L5bc < 3) { L4f0x[0] = E[9]; L4f0x[1] = E[10]; L4f0x[2] = E[11]; unit(L4f0x); }
                else if (L5bc < 5) { L4f0x[0] = E[6]; L4f0x[1] = E[7]; L4f0x[2] = E[8]; unit(L4f0x); }
            }
            if ((0.999999999 < fabs(L4f0x[0] * pS21[0] + L4f0x[1] * pS21[1] + L4f0x[2] * pS21[2])) && (b14 = 1, I2 == 5)) { I2 = L5d0; I0 = L5cc; }
        }
        iV15 = L5e0;
        if (!dr || I(0x10) != -1) goto LAB_b9e1_after;
        {   /* the drive surface's side of the same boundary tangent test */
            double u_ = D(0x278), v_ = D(0x290), L4f0x[3] = { 0, 0, 0 };
            is_eval(dr, 1, u_, v_, E);
            L4c8[0] = u_; L4c8[1] = v_;
            if ((u_ == dr->d[0] || u_ == dr->d[1]) && (v_ == dr->d[2] || v_ == dr->d[3])) { L4c8[0] = D(0x280); L4c8[1] = D(0x298); }
            boundary_uvpt(dr, L4c8, &L5e0, &L5a8);
            if (0 < L5e0) {
                if (L5e0 < 3) { L4f0x[0] = E[9]; L4f0x[1] = E[10]; L4f0x[2] = E[11]; unit(L4f0x); }
                else if (L5e0 < 5) { L4f0x[0] = E[6]; L4f0x[1] = E[7]; L4f0x[2] = E[8]; unit(L4f0x); }
            }
            iV15 = L5e0;
            if (fabs(L4f0x[0] * pS21[0] + L4f0x[1] * pS21[1] + L4f0x[2] * pS21[2]) <= 0.999999999) goto LAB_b9e1_after;
            b14 = 1; if (I2 == 5) { I2 = L5d0; I0 = L5cc; }
            goto LAB_b9ec;
        }
    }
    goto AFTER_B9E;
LAB_b9e1_after:
    if (b14) goto LAB_b9ec;
    goto AFTER_B9E;
LAB_b9ec:
    if (-1 < I2) {
        if (L570 == 0) {
            if (L5bc == 1) { if (th->d[4] * 0.05 < D(0x248) - th->d[0]) b14 = 0; }
            else if (L5bc == 2) { if (th->d[4] * 0.05 < th->d[1] - D(0x248)) b14 = 0; }
            else if (L5bc == 3) { if (th->d[5] * 0.05 < D(0x260) - th->d[2]) b14 = 0; }
            else if (L5bc == 4 && th->d[5] * 0.05 < th->d[3] - D(0x260)) b14 = 0;
        }
        if (dr && b14 && L588 == 0) {
            if (L5e0 == 1) { if (dr->d[4] * 0.05 < D(0x278) - dr->d[0]) b14 = 0; }
            else if (L5e0 == 2) { if (dr->d[4] * 0.05 < dr->d[1] - D(0x278)) b14 = 0; }
            else if (L5e0 == 3) { if (dr->d[5] * 0.05 < D(0x290) - dr->d[2]) b14 = 0; }
            else if (L5e0 == 4 && dr->d[5] * 0.05 < dr->d[3] - D(0x290)) b14 = 0;
        }
    }
AFTER_B9E:
    d29 = L460[2]; d2 = L460[1]; d28 = L460[0];
    d32 = L460[1] * pS21[1] + L460[0] * pS21[0] + L460[2] * pS21[2];
    L5b0 = L5b0 + 1;
#ifdef SPINT_TRACE
    if (getenv("SPINT_STEPTR")) fprintf(stderr, "ST n %d I0 %d I2 %d step %.9f d32 %.12f L5b0 %d L5c0 %d d30 %.6f y %.9f Tcur %.9f %.9f %.9f\n", L600, I0, I2, D(0x238), d32, L5b0, L5c0, d30, ppV23[1], pS21[0], pS21[1], pS21[2]);
#endif
    d1 = d30;
    if ((L5b0 < 0) && (fabs(d32) <= 0.999999999)) {
        d33 = sqrt(1.0 - fabs(d32) * fabs(d32));
        d34 = D(0x238);
        d33 = (d34 * d33) / D(0x220);
        if (1.1 <= d33) {
            if (I2 == 5) {
                D(0x238) = d34 * 0.5; D(0x240) *= 0.5; I2 = L5d0; I0 = L5cc; L5b0 = -4; L5c0 = -4;
                { double uu = D(600), vv = D(0x270), o[6]; b22 = 0; if (!has_seams(th)) { b22 = 0; if (!has_deg(th)) b22 = 1; }
                  int r = is_pmd(th, b22, (double *)(c + 0x48), o, &L5a8, &uu, &vv, 1);
                  if (r == 0) { D(0x248) = uu; D(0x260) = vv; D(0x250) = (uu + D(600)) * 0.5; D(0x268) = (vv + D(0x270)) * 0.5; } }
            } else { d31 = sqrt(d33); D(0x238) = d34 / d31; }
            b22 = 1; d31 = L5a0; d35 = L598; d28 = L590;
            goto LAB_cbfd;
        }
    }
    if (I2 == 5) {
        if ((unsigned)(L600 - 2) < 2 && L548 < 6) {
            L548++; D(0x238) *= 0.5; D(0x240) *= 0.5; I2 = L5d0; I0 = L5cc; b22 = 1; d31 = L5a0; d35 = L598; d28 = L590; goto LAB_cbfd;
        }
    } else {
        d35 = (D(0x210) - d31 * D(0x1f0)) / d35;
        d34 = D(0x1f0) - d35 * d31;
        d31 = ppV23[0] * pS21[0] + pS21[1] * ppV23[1] + ppV23[2] * pS21[2];
        ppV23[0] = d35 * D(0x1f8) + d34 * D(0x1d8) + d31 * pS21[0];
        ppV23[1] = d34 * D(0x1e0) + d35 * D(0x200) + d31 * pS21[1];
        ppV23[2] = d34 * D(0x1e8) + d35 * D(0x208) + d31 * pS21[2];
    }
    iV15 = I2;
    if (iV15 == 5) {
        long l = L600;
        L558[l][0] = ppV23[0]; L558[l][1] = ppV23[1]; L558[l][2] = ppV23[2];
        L4b8[l][0] = pS21[0]; L4b8[l][1] = pS21[1]; L4b8[l][2] = pS21[2];
        if (L560) { L560[l][0] = D(0x248); L560[l][1] = D(0x260); }
        if (L568) { L568[l][0] = D(0x278); L568[l][1] = D(0x290); }
        L600++;
    LAB_cf73:
        D(0x1a8) = D(0x248); D(0x1b0) = D(0x260); D(0x1c8) = D(0x278); D(0x1d0) = D(0x290);
        *L508 = L600; D(0x230) = L550; *L3e8 = b14; ret = 0; goto out;
    }
    if ((iV15 == 0) && (L5c0 = L5c0 + 1, L5c0 < 0)) {
        double dx = ppV23[0] - D(0x48), dy = ppV23[1] - D(0x50), dz = ppV23[2] - D(0x58), l2 = sqrt(dy * dy + dx * dx + dz * dz), ux = dx, uy = dy, uz = dz;
        if (!(l2 <= 0.0)) { double q = 1.0 / l2; uy = dy * q; uz = dz * q; ux = q * dx; }
        d35 = d2 * uy + d28 * ux + d29 * uz;
        if (d35 <= 0.999999999) {
            d35 = sqrt(1.0 - d35 * d35);
            d31 = (l2 * d35) / (D(0x220) * 0.5);
            if (1.1 <= d31) { d31 = sqrt(d31); D(0x238) /= d31; b22 = 1; d31 = L5a0; d35 = L598; d28 = L590; goto LAB_cbfd; }
        }
    }
    L5b0 = -4; L5c0 = -4;
    if (iV15 == 0) {
        L590 = ppV23[0] - L498[0]; L598 = ppV23[1] - L498[1]; L5a0 = ppV23[2] - L498[2]; L4b0[0] = L590; L4b0[1] = L598;
        if (!b10) { b10 = 1; b22 = 1; d31 = L5a0; d35 = L598; d28 = L590; goto LAB_cbfd; }
    }
    d28 = L590; d35 = L598; d31 = L5a0;
    d2 = D(0x238);
    D(0x240) = fabs(d2);
    if (-2 < iV15) {
        d29 = 1.0 - d32 * d32;
        if (2e-09 < d29) {
            d29 = sqrt(d29);
            if (D(0x228) <= d2) d1 = fabs(d2 / d29);
            if (iV15 == -1) d30 = d1;
            if ((1.9 < d30 / d1) || (d30 / d1 < 0.55)) d30 = d1;
            d30 = d1 * 2.0 - d30;
            { double lim = th->metric ? 0.375 : 0.015; if (d30 <= lim && lim != d30) d30 = lim; }
            d2 = sqrt(d30 * D(0x220));
            D(0x238) = d2;
            d30 = D(0x228);
            if (d2 < d30) D(0x238) = d30;
            if (d1 <= 0.1) { d2 = d1 * 10.0; d28 *= d2; d35 *= d2; d31 *= d2; L5a0 = d31; L598 = d35; L590 = d28; }
            d2 = D(0x230);
            if (d2 <= D(0x238) && D(0x238) != d2) D(0x238) = d2;
        } else {
            D(0x238) = D(0x230); d30 = D(0x228); d1 = L418;
            if (d30 <= fabs(d2)) { d2 = fabs(d2) * 1.5; if (d2 <= D(0x238) && D(0x238) != d2) D(0x238) = d2; }
        }
        if ((d30 <= D(0x240)) && (d30 = D(0x240) * 1.5, d30 < D(0x238))) D(0x238) = d30;
        if ((4 < L600) && L560 && L5b4 < 6) {
            double m[21]; is_eval(th, 0, (D(0x248) + L560[L600 - 1][0]) * 0.5, (L560[L600 - 1][1] + D(0x260)) * 0.5, m);
            double a[3] = { L558[L600 - 1][0], L558[L600 - 1][1], L558[L600 - 1][2] }, b[3] = { ppV23[0], ppV23[1], ppV23[2] };
            line_pmd(a, b, m, &L5a8, &L520);
            if ((0.0 < L520) && (L520 < 1.0) && (D(0x220) * 0.1 < L5a8)) {
                d30 = D(0x238); D(0x238) = d30 * 0.5; if (d30 * 0.5 < D(0x228)) D(0x238) = D(0x228);
#ifdef SPINT_TRACE
                if (getenv("SPINT_STEPTR")) fprintf(stderr, "  midpoint halve n %d t %.6f dist %.9e lim %.9e\n", L600, L520, L5a8, D(0x220) * 0.1);
#endif
                b22 = 1; L5b4++; goto LAB_cbfd;
            }
        }
        if ((4 < L600) && L568 && L5b8 < 6) {   /* the same midpoint check on the drive surface */
            double m[21]; is_eval(dr, 0, (D(0x278) + L568[L600 - 1][0]) * 0.5, (L568[L600 - 1][1] + D(0x290)) * 0.5, m);
            double a[3] = { L558[L600 - 1][0], L558[L600 - 1][1], L558[L600 - 1][2] }, b[3] = { ppV23[0], ppV23[1], ppV23[2] };
            line_pmd(a, b, m, &L5a8, &L520);
            if ((0.0 < L520) && (L520 < 1.0) && (D(0x220) * 0.1 < L5a8)) {
                d30 = D(0x238); D(0x238) = d30 * 0.5; if (d30 * 0.5 < D(0x228)) D(0x238) = D(0x228);
                b22 = 1; L5b8++; goto LAB_cbfd;
            }
        }
    }
    iV15 = I2;
    if (iV15 < 0) {
        I2 = iV15 + 1; iV15 = iV15 + 5;
        if (0 < iV15) {
            if (iV15 < 3) { D(0x238) = 0; b22 = 1; }
            else if (iV15 == 3) { D(0x238) = D(0x228); b22 = 0; }
            else if (iV15 == 4) b22 = 1;
        }
    } else {
        if (iV15 == 0) {
            if (d32 + 0.707 <= 0.0) { *L508 = L600; D(0x230) = L550; ret = L5ac; goto out; }
            if ((I0 + 3 < I(4)) && (*L528 == 0) &&
                ((L3c0[0] * D(0x48) + L3c0[1] * D(0x50) + L3c0[2] * D(0x58) - L3b0) * (L3c0[1] * ppV23[1] + L3c0[0] * ppV23[0] + L3c0[2] * ppV23[2] - L3b0) <= 0.0)) {
                double d32x = ppV23[0] - D(0x48), dy_ = ppV23[1] - D(0x50), dz_ = ppV23[2] - D(0x58);
                double ex = d32x, ey = dy_, ez = dz_, l3 = sqrt(d32x * d32x + dy_ * dy_ + dz_ * dz_);
                if (1e-12 < l3) {
                    double q = 1.0 / l3; ey *= q; ex = d32x * q; ez *= q;
                    double den = ey * L3c0[1] + ex * L3c0[0] + ez * L3c0[2];
                    if (1e-15 < den) {
                        double t2 = ((L3c0[1] * D(0x50) + L3c0[0] * D(0x48) + L3c0[2] * D(0x58)) - L3b0) / den;
                        double L3a8[3] = { D(0x48) - t2 * ex, D(0x50) - t2 * ey, D(0x58) - t2 * ez };
                        double q1[3] = { L430[0] - L3a8[0], L430[1] - L3a8[1], L430[2] - L3a8[2] };
                        double p1[3] = { L3a8[0] - D(0x48), L3a8[1] - D(0x50), L3a8[2] - D(0x58) }, p2[3] = { L3a8[0] - ppV23[0], L3a8[1] - ppV23[1], L3a8[2] - ppV23[2] };
                        double dq = len(q1);
                        if ((dq <= l3) && dq < len(p1) && dq < len(p2)) {
                            long l = L600;
                            L558[l][0] = L558[0][0]; L558[l][1] = L558[0][1]; L558[l][2] = L558[0][2];
                            if (L560) { L560[l][0] = L560[0][0]; L560[l][1] = L560[0][1]; }
                            if (L568) { L568[l][0] = L568[0][0]; L568[l][1] = L568[0][1]; }
                            double *t0 = L4b8[0], *t1 = L4b8[1];
                            if ((t0[0] * t1[0] + t1[1] * t0[1] + t0[2] * t1[2]) / (len(t1) * len(t0)) <= 0.0) { t0[0] *= -1.0; t0[1] *= -1.0; t0[2] *= -1.0; }
                            L4b8[l][0] = t0[0]; L4b8[l][1] = t0[1]; L4b8[l][2] = t0[2];
                            L600++; I2 = 5; I0 = -1;
                            D(0x1a8) = D(0x198); D(0x1b0) = D(0x1a0); D(0x1c8) = D(0x1b8); D(0x1d0) = D(0x1c0);
                            *L508 = L600; D(0x230) = L550; *L3e8 = b14; ret = 0; goto out;
                        }
                    }
                }
            }
        }
        long l = L600;
        L558[l][0] = ppV23[0]; L558[l][1] = ppV23[1]; L558[l][2] = ppV23[2];
        L4b8[l][0] = D(0x18); L4b8[l][1] = D(0x20); L4b8[l][2] = D(0x28);
        if (L560) {
            double uu = D(0x248), vv = D(0x260), o[6];
            int r = is_pmd(th, 0, ppV23, o, &L5a8, &uu, &vv, 0);
            if (r == 0) { L560[l][0] = uu; L560[l][1] = vv; D(0x248) = uu; D(0x260) = vv; }
            else { L560[l][0] = D(0x248); L560[l][1] = D(0x260); }
        }
        if (L568) {
            double uu = D(0x278), vv = D(0x290), o[6];
            int r = is_pmd(dr, 0, ppV23, o, &L5a8, &uu, &vv, 0);
            if (r == 0) { L568[l][0] = uu; L568[l][1] = vv; D(0x278) = uu; D(0x290) = vv; }
            else { L568[l][0] = D(0x278); L568[l][1] = D(0x290); }
        }
        if (-1 < I0) L600++;
        if (I(4) + -1 < L600) L600 = I(4) - 1;
        b22 = 0; L5b4 = 0; L5b8 = 0; d31 = L5a0; d35 = L598; d28 = L590;
    }
LAB_cbfd:
    d30 = d1;
    goto LAB_ad2a;
out:
#ifdef SPINT_TRACE
    if (getenv("SPINT_RUNTR")) { fprintf(stderr, "RUN ret %d n %d I0 %d I2 %d flag %d b14 %d\n", ret, *L508, I0, I2, *L528, b14); for (int z = 0; z < *L508; z++) fprintf(stderr, "R %d %.12f %.12f %.12f\n", z, L558[z][0], L558[z][1], L558[z][2]); }
#endif
    (void)L5e0; (void)L588; (void)L5b8; (void)L4b0; (void)L568; (void)L518; (void)L510; (void)d29; (void)d2;
    return ret;
}
/* MaSurface::SurfPlaneInitIntersection (0x1804896b0) */
void plane_init(ISrf *s, const double *pl, int multi, double **seeds, int max, char *flags, int *nseed) {
    Iso iso = { 0 }; double t[100]; int n = 0; *nseed = 0;
    { grid_build(s); double b[6], E[21]; box_init(b);   /* MaMinMaxBox::GenSrfBox (0x18043a1e0): the grid points and the middles of the cells */
      for (int i = 0; i < s->ngu; i++) for (int j = 0; j < s->ngv; j++) box_add(b, s->gp + 3 * ((size_t)i * s->ngv + j));
      for (int i = 0; i < s->ngu - 1; i++) for (int j = 0; j < s->ngv - 1; j++) { is_eval(s, 0, (s->gu[i] + s->gu[i + 1]) * 0.5, (s->gv[j] + s->gv[j + 1]) * 0.5, E); box_add(b, E); }
      box_expand(b, s->metric); if (!box_plane(b, pl)) return; }

    for (int pass = 0; pass < 2; pass++) {
        int cnt = pass ? 0xf : 2;
        for (int dir = 1; dir <= 2; dir++) {
            double base_step = dir == 1 ? s->d[4] : s->d[5], start = dir == 1 ? s->d[0] : s->d[2];
            double lo = dir == 1 ? s->d[2] : s->d[0], hi = dir == 1 ? s->d[3] : s->d[1], margin = (dir == 1 ? s->d[5] : s->d[4]) * 0.0001;
            double step = cnt == 2 ? base_step : base_step * 0.0625, x = cnt == 2 ? start : start + step;
            for (int k = 0; k < cnt; k++) {
                if (cnt == 2 && k == 1) { EDGES(s); if ((dir == 1 ? s->cl[0] : s->cl[1])) continue; }
                iso_set(&iso, s, dir, x);
                plane_intersect(&iso, 1, pl, 100, t, &n);
                if (0 < n) {
                    for (int i = 0; i < n; i++) {
                        flags[*nseed] = (!pass || (hi - margin < t[i]) || (t[i] < margin + lo)) ? 1 : 0;
                        if (dir == 1) { seeds[*nseed][0] = x; seeds[*nseed][1] = t[i]; } else { seeds[*nseed][0] = t[i]; seeds[*nseed][1] = x; }
                        (*nseed)++;
                        if (*nseed == max) return;
                    }
                    if (!multi && 0 < *nseed) return;
                }
                x += step;
            }
        }
    }
}
/* MaSurfPlaneIntersect::InitCutData (0x180495fa0) */
int init_cut(ISrf *s, MaInt *m) {
    uint8_t *c = m->cut; double E[21];
    m->n = 0; double u = m->su, v = m->sv, plane[4]; memcpy(plane, m->plane, 32);
    is_eval(s, 1, u, v, E); double N[3] = { E[3], E[4], E[5] }; unit(N);
    double d = N[1] * plane[1] + N[0] * plane[0] + N[2] * plane[2]; d = 1.0 - d * d;
    if (!(1e-20 < d)) return 2;
    double tz = plane[1] * N[0] - plane[0] * N[1], ty = plane[0] * N[2] - plane[2] * N[0], tx = plane[2] * N[1] - plane[1] * N[2], q = 1.0 / sqrt(d);
    double tz1 = tz * q, tx1 = tx * q, ty1 = ty * q, st = m->minstep; (void)st;
    double P2[3] = { tx1 * m->minstep + E[0], ty1 * m->minstep + E[1], tz1 * m->minstep + E[2] }, o[6], dist;
    double u2 = u, v2 = v;
    int r = is_pmd(s, 0, P2, o, &dist, &u2, &v2, 0);
    if (r >= 1) return 2;
    D(0x218) = 1.0;
    if (r < 0) { D(0x218) = -1.0; tz1 *= -1.0; tx1 *= -1.0; ty1 *= -1.0; }
    D(0x30) = E[0]; D(0x38) = E[1]; D(0x40) = E[2];
    D(0x18) = tx1; D(0x20) = ty1; D(0x28) = tz1;
    m->P[0][0] = E[0]; m->P[0][1] = E[1]; m->P[0][2] = E[2];
    m->T[0][0] = tx1; m->T[0][1] = ty1; m->T[0][2] = tz1;
    m->UV[0][0] = u; m->UV[0][1] = v;
    D(0x248) = u; D(0x250) = u; D(600) = u; D(0x260) = v; D(0x268) = v; D(0x270) = v; D(0x198) = u; D(0x1a0) = v;
    D(0x220) = m->chord2 * 2.0; I(4) = 10000; D(0x230) = m->maxstep; D(0x228) = m->minstep;
    return 0;
}
/* MaSurfSurfIntersect::InitCutData (0x1804a5290) */
int init_cut_ss(ISrf *s, MaInt *m) {
    uint8_t *c = m->cut; ISrf *dr = m->dr; double E1[21], E2[21], o[6], dist;
    m->n = 0; double u1 = m->su, v1 = m->sv, u2 = m->du, v2 = m->dv;
    is_eval(s, 1, u1, v1, E1); is_eval(dr, 1, u2, v2, E2);
    double n1[3] = { E1[3], E1[4], E1[5] }, n2[3] = { E2[3], E2[4], E2[5] }; unit(n1); unit(n2);
    double d = n2[0] * n1[0] + n2[1] * n1[1] + n2[2] * n1[2]; d = 1.0 - d * d;
    if (d <= 1e-20) return 2;
    double tz = n1[0] * n2[1] - n2[0] * n1[1], ty = n1[2] * n2[0] - n2[2] * n1[0], tx = n2[2] * n1[1] - n1[2] * n2[1], q = 1.0 / sqrt(d);
    tz *= q; tx *= q; ty *= q;
    double st = m->minstep, P2[3] = { tx * st + E1[0], ty * st + E1[1], tz * st + E1[2] }, sign = 1.0, uu = u1, vv = v1;
    int r = is_pmd(s, 0, P2, o, &dist, &uu, &vv, 0);
    if (r < 1) {
        ISrf *th0 = dr; uu = u2; vv = v2;
        if (r == 0) {
            r = is_pmd(dr, 0, P2, o, &dist, &uu, &vv, 0);
            if (0 < r) return 2;
            if (r < 0) { th0 = s; uu = u1; vv = v1; goto neg; }
        } else {
        neg:
            sign = -1.0; tz *= -1.0; tx *= -1.0; ty *= -1.0;
            P2[0] = tx * st + E1[0]; P2[1] = ty * st + E1[1]; P2[2] = tz * st + E1[2];
            r = is_pmd(th0, 0, P2, o, &dist, &uu, &vv, 0);
            if (r != 0) return 2;
        }
        D(0x218) = sign;
        D(0x30) = E1[0]; D(0x38) = E1[1]; D(0x40) = E1[2];
        D(0x18) = tx; D(0x20) = ty; D(0x28) = tz;
        m->P[0][0] = E1[0]; m->P[0][1] = E1[1]; m->P[0][2] = E1[2];
        m->T[0][0] = tx; m->T[0][1] = ty; m->T[0][2] = tz;
        m->UV[0][0] = u1; m->UV[0][1] = v1; m->UV2[0][0] = u2; m->UV2[0][1] = v2;
        D(0x248) = u1; D(0x250) = u1; D(600) = u1; D(0x260) = v1; D(0x268) = v1; D(0x270) = v1;
        D(0x278) = u2; D(0x280) = u2; D(0x288) = u2; D(0x290) = v2; D(0x298) = v2; D(0x2a0) = v2;
        D(0x198) = u1; D(0x1a0) = v1; D(0x1b8) = u2; D(0x1c0) = v2;
        D(0x220) = m->chord2 * 2.0; I(4) = 10000; D(0x230) = m->maxstep; D(0x228) = m->minstep;
        return 0;
    }
    return 2;
}
/* MaSurfSurfIntersect::ReverseCutDirectionData (0x1804a5e20): the plane version */
void reverse_cut(MaInt *m) {
    uint8_t *c = m->cut;
    m->P[0][0] = D(0x30); m->P[0][1] = D(0x38); m->P[0][2] = D(0x40);
    D(0x218) *= -1.0; D(0x18) *= -1.0; D(0x20) *= -1.0; D(0x28) *= -1.0;
    m->T[0][0] = D(0x18); m->T[0][1] = D(0x20); m->T[0][2] = D(0x28);
    D(0x198) = D(0x1a8); D(0x1a0) = D(0x1b0);
    m->UV[0][0] = D(0x198); m->UV[0][1] = D(0x1a0);
    if (m->dr) { D(0x1b8) = D(0x1c8); D(0x1c0) = D(0x1d0); m->UV2[0][0] = D(0x1b8); m->UV2[0][1] = D(0x1c0); }   /* the drive surface (this+0xc0) */
    D(0x220) = m->chord2 * 2.0; I(4) = 10000; D(0x230) = m->maxstep; D(0x228) = m->minstep;
    m->flag248 = 1;
}
/* MaSurfSurfIntersect::MarchIntersection (0x1804a5940) */
int march_top(ISrf *s, MaInt *m) {
    char L528 = 0, L3e8 = 0, flag249 = 0; int it = 0, r;
    uint8_t *c = m->cut;
    for (;;) {
        for (;;) {
            it++;
#ifdef MI_TRACE
            if (mi_trf) fprintf(mi_trf, "RUN %d %.17g %.17g %.17g %.17g %.17g %.17g\n", m->n, D(0x30), D(0x38), D(0x40), D(0x18), D(0x20), D(0x28));
#endif
            r = march(s, m->dr, c, m->P, m->T, m->UV, m->dr ? m->UV2 : NULL, NULL, NULL, &m->n, &flag249, &L3e8);
#ifdef MI_TRACE
            if (mi_trf) fprintf(mi_trf, "RUNEND %d %d %d %d\n", r, m->n, flag249, L3e8);
#endif
            if (r != 0) break;
            if (m->n == 10000 - 1) return 2;
            if ((-1 < I0) || (1 < it) || (!((m->flag248 == 0 && flag249 == 1) || (m->flag248 == 1 && flag249 == 1 && L3e8 == 1)))) {
                if (m->n < 3) return 2;
                if (has_deg(s)) {
                    double pp[2];
                    if (s->deg[0] || s->deg[1]) {
                        pp[0] = m->UV[0][0]; pp[1] = m->UV[0][1]; if (uv_near_pole(s, pp)) m->UV[0][1] = m->UV[1][1];
                        pp[0] = m->UV[m->n - 1][0]; pp[1] = m->UV[m->n - 1][1]; if (uv_near_pole(s, pp)) m->UV[m->n - 1][1] = m->UV[m->n - 2][1];
                    }
                    if (s->deg[2] || s->deg[3]) {
                        pp[0] = m->UV[0][0]; pp[1] = m->UV[0][1]; if (uv_near_pole(s, pp)) m->UV[0][0] = m->UV[1][0];
                        pp[0] = m->UV[m->n - 1][0]; pp[1] = m->UV[m->n - 1][1]; if (uv_near_pole(s, pp)) m->UV[m->n - 1][0] = m->UV[m->n - 2][0];
                    }
                }
                if (m->dr && has_deg(m->dr)) {   /* MarchIntersection (0x1804a5940): the same for the drive surface's (u, v), +0x80 */
                    ISrf *d_ = m->dr; double pp[2];
                    if (d_->deg[0] || d_->deg[1]) {
                        pp[0] = m->UV2[0][0]; pp[1] = m->UV2[0][1]; if (uv_near_pole(d_, pp)) m->UV2[0][1] = m->UV2[1][1];
                        pp[0] = m->UV2[m->n - 1][0]; pp[1] = m->UV2[m->n - 1][1]; if (uv_near_pole(d_, pp)) m->UV2[m->n - 1][1] = m->UV2[m->n - 2][1];
                    }
                    if (d_->deg[2] || d_->deg[3]) {
                        pp[0] = m->UV2[0][0]; pp[1] = m->UV2[0][1]; if (uv_near_pole(d_, pp)) m->UV2[0][0] = m->UV2[1][0];
                        pp[0] = m->UV2[m->n - 1][0]; pp[1] = m->UV2[m->n - 1][1]; if (uv_near_pole(d_, pp)) m->UV2[m->n - 1][0] = m->UV2[m->n - 2][0];
                    }
                }
                return 0;
            }
            reverse_cut(m);
        }
        if (r != 2) return r;
        if (m->n < 3) return 2;
        if (3 < it) { flag249 = 1; return 0; }
        m->chord *= 0.5; m->chord2 *= 0.5; m->maxstep *= 0.5;
        if (m->dr) init_cut_ss(s, m); else init_cut(s, m);
    }
    (void)L528;
}

/* ---- MaCubicSpline::CurveFit (0x1803f3f40) ---- */
void best_fit(const double *P0, const double *T0, const double *P1, const double *T1, Cub *C) {
    double a[3] = { P1[0] - P0[0], P1[1] - P0[1], P1[2] - P0[2] }, L = len(a), m0, m1;
    if (1e-12 <= L) {
        double q = 1.0 / L, L2 = L * 2.0, p6 = (a[1] * q * T0[1] + a[0] * q * T0[0] + a[2] * q * T0[2]), p5 = (a[1] * q * T1[1] + a[0] * q * T1[0] + a[2] * q * T1[2]);
        m0 = 0.0 <= p5 ? L2 / (p5 + 1.0) : (1.0 - p5) * L2;
        m1 = 0.0 <= p6 ? L2 / (p6 + 1.0) : (1.0 - p6) * L2;
    } else { m0 = 1e-12; m1 = 1e-12; }
    for (int k = 0; k < 3; k++) { double D0 = m0 * T0[k], D1 = m1 * T1[k], P = P0[k], Q = P1[k]; C->a[k] = (P - Q) * 2.0 + D0 + D1; C->b[k] = ((Q - P) * 3.0 - D0 * 2.0) - D1; C->c[k] = D0; C->d[k] = P; }
}
void cub_ev(const Cub *C, int order, double t, double *o) {
    for (int k = 0; k < 3; k++) { o[k] = ((t * C->a[k] + C->b[k]) * t + C->c[k]) * t + C->d[k]; if (order > 0) o[3 + k] = (t * 1.5 * C->a[k] + C->b[k]) * (t * 2.0) + C->c[k]; }
}
/* MaCurve::PointMinDist (0x180407490) on the cubic over 0 .. 1 */
/* the cubic's hunting points (MaCurve +0x50) as MaCubicSpline::CurveFit 0x1803f3f40 leaves them: its one MaCubicCurve (local_298) gets new
   coefficients from MaCubicCurve::GenBestFitCoeff 0x1803ed9f0, which does not SetDirty, so the points made at the first hunting
   PointMinDist of a CurveFit stay for the later ones (curve_fit resets them per call) */
static struct { int n; double *T, (*P)[3]; } g_cub_hunt;
static void cub_hunt_reset(void) { free(g_cub_hunt.T); free(g_cub_hunt.P); g_cub_hunt.T = NULL; g_cub_hunt.P = NULL; g_cub_hunt.n = 0; }
void cub_pmd(const Cub *C, int hunt, const double *P, double *dist, double *tio) {
    double t;
    if (hunt) {
        /* GenHuntingPoints (only when +0x50 is empty) -> MaCurveHuntingPoints::GenCrvPoints 0x18040bc20: BinaryPointsAtTol(0, 10 deg, mode 1, 5000)
           on the cubic (range 0 .. 1; bin_pts), fewer than 5: 5 at quarter steps, a failure: 20 at range / 19 steps, the points evaluated; then
           the nearest point and the nearest of the chords between the stored points */
        if (!g_cub_hunt.n) {
            double *T = malloc(sizeof(double) * 5000); int n = bin_pts(bp_cub_ev, C, 0.0, 1.0, 10.0, 5000, T);
            if (n < 0) { n = 20; double x = 0.0; for (int i = 0; i < 20; i++) { T[i] = x; x = x + 1.0 / 19.0; } }
            if (n < 5) { n = 5; double x = 0.0; for (int i = 0; i < 5; i++) { T[i] = x; x = x + 0.25; } }
            g_cub_hunt.T = T; g_cub_hunt.P = malloc(sizeof(double[3]) * n); g_cub_hunt.n = n;
            for (int i = 0; i < n; i++) { double O[6]; cub_ev(C, 0, T[i], O); memcpy(g_cub_hunt.P[i], O, 24); }
        }
        int n = g_cub_hunt.n; const double *T = g_cub_hunt.T; double (*HP)[3] = g_cub_hunt.P;
        double best = 1e31, tb = 0.5;
        for (int i = 0; i < n; i++) { const double *O = HP[i]; double dd = (P[0] - O[0]) * (P[0] - O[0]) + (P[1] - O[1]) * (P[1] - O[1]) + (P[2] - O[2]) * (P[2] - O[2]); if (dd < best) { best = dd; tb = T[i]; } }
        if (1e-20 < best || n < 1) best = sqrt(best);
        for (int i = 0; i < n - 1; i++) { double d, r; line_pmd(HP[i], HP[i + 1], P, &d, &r); if (0.0 <= r && r <= 1.0 && d < best) { tb = (T[i + 1] - T[i]) * r + T[i]; best = d; } }
        t = tb;
    } else t = *tio;
    if (t < 0) t = 0; if (t > 1) t = 1;   /* LimitTToRange */
    double O[6], O2[6], ds = 0, tp = 0; int it;
    cub_ev(C, 1, 0.5, O);   /* vtable +0x100 taken as the middle parameter (the start of the iteration when the derivative at it vanishes) */
    if (len(O + 3) >= 1e-12) {
        for (it = 0; it < 0xf; it++) {
            double h = 0.0001, Pa[3], Pb[3]; cub_ev(C, 0, t - h, O2); memcpy(Pa, O2, 24); cub_ev(C, 0, t + h, O2); memcpy(Pb, O2, 24);
            cub_ev(C, 1, t, O); double T[3] = { O[3], O[4], O[5] }, l = len(T); if (l < 1e-20) l = 1e-20; l = 1.0 / l; T[0] *= l; T[1] *= l; T[2] *= l;
            double err = (O[0] * T[0] + O[1] * T[1] + O[2] * T[2]) - (T[0] * P[0] + T[1] * P[1] + T[2] * P[2]);
            if (fabs(err) <= 1e-12 && fabs(err) != 1e-12) break;
            double den = (Pa[1] * T[1] + Pa[0] * T[0] + Pa[2] * T[2]) - (Pb[1] * T[1] + Pb[0] * T[0] + Pb[2] * T[2]);
            if (fabs(den) <= 1e-20 && fabs(den) != 1e-20) den = 1e-20;
            double st = (err * 0.0002) / den, dt = st;
            if (0 < it && 1e-20 <= fabs(ds - st)) { double q = (t - tp) / (ds - st); if (0.0 < q) { q = q * st; if (fabs(q) <= 1.0 && 1.0 != fabs(q)) dt = q; } }
            if (fabs(dt) < 1e-12) break;
            double x = dt; if (1.0 < fabs(dt)) { x = 1.0; if (dt < 1.0) x = -1.0; }
            tp = t; t += x; if (t < 0) t = 0; if (t > 1) t = 1; ds = st;
        }
        *tio = t; *dist = d3(O, P);
    } else { cub_ev(C, 0, 0.5, O); *tio = 0.5; *dist = d3(O, P); }
}
void cub_store(double *dst, const Cub *C) { for (int k = 0; k < 3; k++) { dst[k * 4] = C->a[k]; dst[k * 4 + 1] = C->b[k]; dst[k * 4 + 2] = C->c[k]; dst[k * 4 + 3] = C->d[k]; } }
/* out: nseg x 12 coefficients; returns nseg */
int curve_fit(double **P, double **T, int n, double tol, double *out) {
    Cub C; memset(&C, 0, sizeof C); cub_hunt_reset();
    if (n < 2) return 0;
    if (n == 2) { best_fit(P[0], T[0], P[1], T[1], &C); cub_store(out, &C); return 1; }
    int seg = 0, L324 = 0, i5 = 1, i9 = 2, iter = 0, hunt = 0; double t = 0, dist = 0, last = 0;
    long L310 = 0, L8 = 2;
    for (;;) {
        best_fit(P[L310], T[L310], P[L8], T[L8], &C);
        int i6 = i9;
        for (;;) {
            t = ((double)i5 - (double)L324) / ((double)i9 - (double)L324);
            cub_pmd(&C, hunt, P[i5], &dist, &t);
#ifdef MI_TRACE
            { static int zf = -1; if (zf < 0) zf = getenv("TRIMSS_ZF") != NULL; if (zf && mi_trf) fprintf(mi_trf, "ZF %d %.17g %.17g %.17g t %.17g -> %.17g d %.17g\n", hunt, P[i5][0], P[i5][1], P[i5][2], hunt ? 0.0 : ((double)i5 - (double)L324) / ((double)i9 - (double)L324), t, dist); }
#endif
            if ((t == 0.0 || t == 1.0) && iter < 3) { hunt = 1; iter++; }
            else {
                iter = 0; hunt = 0; last = fabs(dist);
                if (tol < last || tol == last) {
                    L324 = i9 - 1;
                    best_fit(P[L310], T[L310], P[L8 - 1], T[L8 - 1], &C);
                    if (L324 == n - 1) goto store_last;
                    cub_store(out + seg * 12, &C); seg++;
                    L310 = L8 - 1; L8 = L8 + 1; i6 = i9 + 1; i5 = i9;
                    break;
                }
                i5 = i5 + 1;
            }
            i6 = i9;
            if (!(i5 < i9)) break;
        }
        if (last < tol) { i5 = L324 + 1; L8 = L8 + 1; i9 = i6 + 1; }
        else { i9 = i6; if (n - 1 < i6) break; }
        if (!(i9 <= n - 1)) break;
    }
    best_fit(P[L324], T[L324], P[i9 - 1], T[i9 - 1], &C);
store_last:
    cub_store(out + seg * 12, &C);
    return seg + 1;
}
/* MaCubicSpline::GenUList (0x1803f7ce0), nseg segments of 12 coefficients */
void gen_ulist(const double *C, int nseg, double *U) {
    U[0] = 0; U[1] = 1.0; double d3v = 1.0;
    for (int i = 2; i <= nseg; i++) {
        const double *e = C + (i - 2) * 12, *b = C + (i - 1) * 12;
        double l = sqrt(b[2] * b[2] + b[6] * b[6] + b[10] * b[10]); if (l < 1e-20) l = 1e-20;
        double x = e[0] * 3.0 + e[1] * 2.0 + e[2], y = e[4] * 3.0 + e[5] * 2.0 + e[6], z = e[8] * 3.0 + e[9] * 2.0 + e[10], m = sqrt(y * y + x * x + z * z); if (fabs(m) < 1e-20) m = 1e-20;
        d3v *= l / m; U[i] = d3v + U[i - 1];
    }
    if (nseg > 1) { double n = U[nseg]; for (int i = 1; i <= nseg; i++) U[i] /= n; }
}
/* MaCubicSpline::ConvertToC1NURB (0x1803f2ec0) */
void to_c1_nurb(const double *C, int nseg, const double *U, MiCurve *o) {
    int ncp = nseg * 2 + 2, nk = nseg * 2 + 6, w = 0; o->p = malloc(sizeof(double) * ncp * 3); o->k = malloc(sizeof(double) * nk); o->deg = 3; o->ncp = ncp; o->nk = nk; o->t0 = 0; o->t1 = 1;
    for (int i = 0; i < nseg; i++) {
        const double *c = C + i * 12; double V[4][3];   /* MaCubicCurve::GetBezierVertices: ConvertToGeometric, V1 = P0 + T0 / 3, V2 = P1 - T1 / 3 */
        for (int k = 0; k < 3; k++) { double a = c[k * 4], b = c[k * 4 + 1], cc = c[k * 4 + 2], d = c[k * 4 + 3], P0 = d, P1 = cc + d + b + a, T0 = cc, T1 = b * 2.0 + cc + a * 3.0;
            V[0][k] = P0; V[3][k] = P1; V[1][k] = T0 * 0.3333333333333333 + P0; V[2][k] = P1 - T1 * 0.3333333333333333; }
        int from = (nseg == 1) ? 0 : (i ? 1 : 0), to = (nseg == 1) ? 4 : ((i == nseg - 1) ? 4 : 3);
        for (int k = from; k < to; k++) { memcpy(o->p + w * 3, V[k], 24); w++; }
    }
    o->k[0] = U[0]; o->k[1] = U[0];
    int q = 2; for (int i = 0; i <= nseg; i++) { o->k[q++] = U[i]; o->k[q++] = U[i]; }
    o->k[q] = U[nseg]; o->k[q + 1] = U[nseg];
}
/* MaCubicSpline::GetSharpNodes (0x1803f8260, HasSharpNodes 0x1803f8920): the interior nodes where the end tangent of a segment and the start tangent of the next differ by more than 0.1 degree */
int sharp_nodes(const double *C, int nseg, const double *U, double *S) {
    double cs = cos(0.001745329251994329); int n = 0;
    for (int i = 0; i + 1 < nseg; i++) {
        const double *e = C + i * 12, *b = C + (i + 1) * 12;
        double A[3], B[3]; for (int k = 0; k < 3; k++) { A[k] = e[k * 4] * 3.0 + e[k * 4 + 1] * 2.0 + e[k * 4 + 2]; B[k] = b[k * 4 + 2]; }
        unit(A); unit(B);
        if (B[0] * A[0] + B[1] * A[1] + B[2] * A[2] < cs) S[n++] = U[i + 1];
    }
    return n;
}
/* MaCubicSpline::ConvertToG0G1NURB (0x1803f3290): a sharp node keeps the joint point (three-fold knot), the other nodes two-fold; used when GetSharpNodes finds any (MaCubicSpline::ConvertToNURB 0x1803f3dd0: IsSmooth ? C1 : G0G1) */
void to_g0g1_nurb(const double *C, int nseg, const double *U, const double *S, int ns, MiCurve *o) {
    int ncp = ns + (nseg + 1) * 2, w = 0; o->p = malloc(sizeof(double) * ncp * 3); o->k = malloc(sizeof(double) * (ncp + 4)); o->deg = 3; o->ncp = ncp; o->t0 = 0; o->t1 = 1;
    for (int i = 0; i < nseg; i++) {
        const double *c = C + i * 12; double V[4][3];
        for (int k = 0; k < 3; k++) { double a = c[k * 4], b = c[k * 4 + 1], cc = c[k * 4 + 2], d = c[k * 4 + 3], P0 = d, P1 = cc + d + b + a, T0 = cc, T1 = b * 2.0 + cc + a * 3.0;
            V[0][k] = P0; V[3][k] = P1; V[1][k] = T0 * 0.3333333333333333 + P0; V[2][k] = P1 - T1 * 0.3333333333333333; }
        int sharp = 0; for (int q = 0; q < ns; q++) if (U[i + 1] == S[q]) sharp = 1;
        int from = i ? 1 : 0, to = (nseg == 1 || i == nseg - 1) ? 4 : (sharp ? 4 : 3); if (nseg == 1) from = 0;
        for (int k = from; k < to; k++) { memcpy(o->p + w * 3, V[k], 24); w++; }
    }
    int q = 0; for (int i = 0; i < 4; i++) o->k[q++] = U[0];
    for (int i = 1; i < nseg; i++) { int sharp = 0; for (int z = 0; z < ns; z++) if (U[i] == S[z]) sharp = 1; o->k[q++] = U[i]; o->k[q++] = U[i]; if (sharp) o->k[q++] = U[i]; }
    for (int i = 0; i < 4; i++) o->k[q++] = U[nseg];
    o->nk = q; o->ncp = w;
}
/* MaCubicSpline::ConvertToNURB (0x1803f3dd0) */
void to_nurb(const double *C, int nseg, const double *U, MiCurve *o) {
    double S[10002]; int ns = 0;
    if (nseg != 1) ns = sharp_nodes(C, nseg, U, S);
    if (ns == 0) to_c1_nurb(C, nseg, U, o); else to_g0g1_nurb(C, nseg, U, S, ns, o);
}
/* MaSurfSurfIntersect::WeedOutDuplicateStartPoints (0x1804a6a90), plane: seeds from i on lying on the just-marched polyline are dropped */
void weed(ISrf *s, MaInt *m, double **seeds, double **seeds2, int i, int nseed) {
    double minr = s->d[4]; if (s->d[5] <= minr) minr = s->d[5];
    EDGES(s);
    for (; i < nseed; i++) {
        double a = seeds[i][0], b = seeds[i][1];
        if (a == -1.0 && b == -1.0) continue;
        double E[21]; is_eval(s, 0, a, b, E);
        /* MaPolyLine::PointMinDist (0x1804725e0): the nearest vertex, then the nearest perpendicular foot */
        double best = 1e31; int kv = 0;
        for (int k = 0; k < m->n; k++) { double dd = (E[0] - m->P[k][0]) * (E[0] - m->P[k][0]) + (E[1] - m->P[k][1]) * (E[1] - m->P[k][1]) + (E[2] - m->P[k][2]) * (E[2] - m->P[k][2]); if (dd < best) { best = dd; kv = k; } }
        if (best > 1e-20) best = sqrt(best);
        double bl = 1e31, dist; int segl = kv, seg;
        for (int k = 0; k < m->n - 1; k++) { double d, t; line_pmd(m->P[k], m->P[k + 1], E, &d, &t); if (0.0 <= t && t <= 1.0 && d <= bl && bl != d) { bl = d; segl = k; } }
        if (bl < best) { dist = bl; seg = segl; } else { dist = best; seg = kv; }
        if (seg < 0) seg = 0; if (m->n - 2 < seg) seg = m->n - 2;
        if (dist <= m->chord2 * 10.0) {
            double L260;
            if (s->per[0] || s->per[1]) {
                double uv0[3] = { m->UV[seg][0], m->UV[seg][1], 0 }, uv1[3] = { m->UV[seg + 1][0], m->UV[seg + 1][1], 0 }, q[3] = { a, b, 0 }, t;
                line_pmd(uv0, uv1, q, &L260, &t);
                /* IsUVNearSeam 0x180486d00: on a periodic direction near the seam: the distance counts as 0 */
                if ((s->per[0] && (fabs(a - s->d[0]) < s->d[4] * 5e-05 || fabs(a - s->d[1]) < s->d[4] * 5e-05)) || (s->per[1] && (fabs(b - s->d[2]) < s->d[5] * 5e-05 || fabs(b - s->d[3]) < s->d[5] * 5e-05))) L260 = 0.0;
            } else L260 = 0.0;
            if (L260 < minr * 0.5) { seeds[i][0] = -1.0; seeds[i][1] = -1.0; if (seeds2) { seeds2[i][0] = -1.0; seeds2[i][1] = -1.0; } }
        }
    }
}

/* ---- MaSurface::BreakAtSlopeDiscontinuities (0x180480290) ---- */
int mi_pieces(ISrf *s, ISrf **out); void mi_free_pieces(ISrf *p, int n);
/* MaNURBSurf::GetPatchBoundaryUList (0x18045c6f0) / V: the unique knots in the range, the range's ends added when inside the knots' extent */
int patch_list(const ISrf *s, int dir_v, double *o) {
    int deg = (dir_v ? s->kv : s->ku) - 1, nk = (dir_v ? s->ncv + s->kv : s->ncu + s->ku); const double *K = dir_v ? s->KV : s->KU; double lo = dir_v ? s->d[2] : s->d[0], hi = dir_v ? s->d[3] : s->d[1];
    double uq[5000]; int nu = 0; (void)deg;
    uq[nu++] = K[deg]; double cur = K[deg];
    for (int i = deg + 1; i < nk - deg - 1; i++) if (K[i] != cur) { uq[nu++] = K[i]; cur = K[i]; }
    uq[nu++] = K[nk - deg - 1];
    int n = 0; if (uq[0] <= lo && lo != uq[0]) o[n++] = lo;
    for (int i = 0; i < nu; i++) if (lo <= uq[i] && uq[i] <= hi) o[n++] = uq[i];
    if (hi < uq[nu - 1]) o[n++] = hi;
    return n;
}
/* iso curve of the constant-(dir) line: dir 1 const u along v, dir 2 const v along u (Flowline's range: the surface's domain), MaNURBSpline::HasSharpNodes (0x18044f130) */
int unique_knots(const ISrf *s, int along_v, double *uq) {
    int deg = (along_v ? s->kv : s->ku) - 1, nk = along_v ? s->ncv + s->kv : s->ncu + s->ku; const double *K = along_v ? s->KV : s->KU; int nu = 0; double cur = K[deg]; uq[nu++] = cur;
    for (int i = deg + 1; i < nk - deg - 1; i++) if (K[i] != cur) { uq[nu++] = K[i]; cur = K[i]; }
    uq[nu++] = K[nk - deg - 1]; return nu;
}
void iso_tan_at(ISrf *s, int dir, double par, double t, double *T) { double E[21]; if (dir == 1) is_eval(s, 1, par, t, E); else is_eval(s, 1, t, par, E); memcpy(T, dir == 1 ? E + 9 : E + 6, 24); unit(T); }
int iso_degenerate(ISrf *s, int dir, double par) { double E[21], t = dir == 1 ? (s->d[2] + s->d[3]) * 0.5 : (s->d[0] + s->d[1]) * 0.5; if (dir == 1) is_eval(s, 1, par, t, E); else is_eval(s, 1, t, par, E); double *T = dir == 1 ? E + 9 : E + 6; return sqrt(T[0] * T[0] + T[1] * T[1] + T[2] * T[2]) <= 1e-12; }
int iso_sharp_t(ISrf *s, int dir, double par, double ang, double *o) {
    double uq[5000]; int nu = unique_knots(s, dir == 1, uq), n = 0; double lo = dir == 1 ? s->d[2] : s->d[0], hi = dir == 1 ? s->d[3] : s->d[1], cs = cos(ang * 0.01745329251994329); (void)lo; (void)hi;
    if (iso_degenerate(s, dir, par)) return 0;
    for (int i = 1; i < nu - 1; i++) { double A[3], B[3]; iso_tan_at(s, dir, par, uq[i] - 1e-06, A); iso_tan_at(s, dir, par, uq[i] + 1e-06, B); if (A[0] * B[0] + A[1] * B[1] + A[2] * B[2] < cs) { if (o) o[n] = uq[i]; n++; } }
    return n;
}
/* MaSurface::IsSmooth (0x180486870): the flow lines at the patch boundaries, both directions */
int is_smooth(ISrf *s, double ang) {
    if (s->obase) s = s->obase;   /* MaOffsetSurf::IsSmooth 0x180463ea0: the bottom base */
    double L[5000]; int n;
    n = patch_list(s, 0, L); for (int i = 0; i < n; i++) if (iso_sharp_t(s, 1, L[i], ang, NULL)) return 0;
    n = patch_list(s, 1, L); for (int i = 0; i < n; i++) if (iso_sharp_t(s, 2, L[i], ang, NULL)) return 0;
    return 1;
}
int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
/* MaSurface::GetUListAtSlopeDiscontinuities (0x1804860e0) / V (0x180486320): const-v lines give the u values, const-u lines the v values */
int slope_list(ISrf *s, int vlist, double ang, double *o) {
    if (s->obase) s = s->obase;   /* MaOffsetSurf::GetU/VListAtSlopeDiscontinuities 0x180463dc0: the bottom base */
    double L[5000], T[5000]; int n = 2, nl = patch_list(s, vlist ? 0 : 1, L); o[0] = vlist ? s->d[2] : s->d[0]; o[1] = vlist ? s->d[3] : s->d[1];
    for (int i = 0; i < nl; i++) { int nt = iso_sharp_t(s, vlist ? 1 : 2, L[i], ang, T); for (int k = 0; k < nt; k++) { int dup = 0; for (int q = 0; q < n; q++) if (o[q] == T[k]) dup = 1; if (!dup) o[n++] = T[k]; } }
    qsort(o, n, sizeof(double), cmp_d); return n;
}
void (*mi_march_hook)(ISrf *piece, MaInt *m, void *ctx); void *mi_march_ctx;
void (*mi_ss_hook)(ISrf *pa, ISrf *pb, MaInt *m, const MiCurve *whole, void *ctx);
int plane_one(ISrf *s, MiFace *F, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **res, int *ncurves, int *cap);
void part_curves(ISrf *piece, MiFace *F, MaInt *m, const MiCurve *whole, MiCurve **res_, int *ncurves_, int *cap_);
int mi_surf_plane(ISrf *s, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **out) {
    ISrf *pieces; int npc = mi_pieces(s, &pieces), cap = 16, ncurves = 0; MiCurve *res = malloc(sizeof(MiCurve) * cap);
    for (int i = 0; i < npc; i++) plane_one(&pieces[i], NULL, pl, chord, chord2, maxstep, minstep, multi, &res, &ncurves, &cap);
    mi_free_pieces(pieces, npc); *out = res; return ncurves;
}
/* the smooth pieces (BreakAtSlopeDiscontinuities): one when smooth */
int mi_pieces(ISrf *s, ISrf **out) {
    if (s->obase) memcpy(s->obase->d, s->d, sizeof s->d);   /* MaOffsetSurf::SetUMinMaxParams 0x1804641c0: also the base */
    if (is_smooth(s, 2.5)) { ISrf *q = malloc(sizeof(ISrf)); *q = *s; q->gu = q->gv = q->gp = NULL; q->ngu = q->ngv = 0; q->edges = 0; *out = q; return 1; }
    double UL[5000], VL[5000]; int nu = slope_list(s, 0, 2.5, UL), nv = slope_list(s, 1, 2.5, VL), n = 0;
    if (nu == 2 && nv == 2) { *out = NULL; return 0; }
    ISrf *r = malloc(sizeof(ISrf) * (size_t)((nu - 1) * (nv - 1)));
    for (int j = 0; j < nv - 1; j++) for (int i = 0; i < nu - 1; i++) {
        ISrf q = *s; q.gu = q.gv = q.gp = NULL; q.ngu = q.ngv = 0; q.edges = 0;
        double du0 = UL[i] + 1e-12, du1 = UL[i + 1] - 1e-12, dv0 = VL[j] + 1e-12, dv1 = VL[j + 1] - 1e-12;
        if (du0 < du1 && dv0 < dv1) { q.d[0] = du0; q.d[1] = du1; q.d[4] = du1 - du0; q.d[2] = dv0; q.d[3] = dv1; q.d[5] = dv1 - dv0; }   /* MaSurface::SetMinMaxParams (0x1804892e0) */
        r[n++] = q;
    }
    *out = r; return n;
}
void mi_free_pieces(ISrf *p, int n) { for (int i = 0; i < n; i++) { free(p[i].gu); free(p[i].gv); free(p[i].gp); } free(p); }
int mi_plane_piece(ISrf *s, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **out) {
    int cap = 16, nc = 0; MiCurve *res = malloc(sizeof(MiCurve) * cap); plane_one(s, NULL, pl, chord, chord2, maxstep, minstep, multi, &res, &nc, &cap); *out = res; return nc;
}
/* MaSurfHuntingGrid::DistToPlane (0x180494d80) */
void mi_dist_to_plane(ISrf *s, const double pl[4], double *dmin, double *dmax, double *mid) {
    grid_build(s); *dmin = 1e31; *dmax = -1e31; *mid = 0.0; double E[21];
    for (int i = 0; i < s->ngu; i++) for (int j = 0; j < s->ngv; j++) { memcpy(E, s->gp + 3 * ((size_t)i * s->ngv + j), 24); double d = (E[0] * pl[0] + E[1] * pl[1] + E[2] * pl[2]) - pl[3]; if (d < *dmin) *dmin = d; if (*dmax <= d && d != *dmax) *dmax = d; }
    if (fabs(*dmax) < fabs(*dmin)) { double t = *dmin; *dmin = *dmax; *dmax = t; }
    *mid = (*dmin + *dmax) * 0.5;
}
/* MaSurfPlaneIntersect::ComputeIntersectionsMain (0x180495df0) on a smooth piece */
int plane_one(ISrf *s, MiFace *F, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **res_, int *ncurves_, int *cap_) {
    MaInt m; memset(&m, 0, sizeof m); memcpy(m.plane, pl, 32); m.chord = chord; m.chord2 = chord2; m.maxstep = maxstep; m.minstep = minstep;
    double *pool = malloc(sizeof(double) * 10000 * 8), *cpool = malloc(sizeof(double) * 10000 * 12), *ulist = malloc(sizeof(double) * 10002);
    m.P = malloc(sizeof(double *) * 10000); m.T = malloc(sizeof(double *) * 10000); m.UV = malloc(sizeof(double *) * 10000);
    for (int i = 0; i < 10000; i++) { m.P[i] = pool + i * 8; m.T[i] = pool + i * 8 + 3; m.UV[i] = pool + i * 8 + 6; }
    double (*sd)[2] = malloc(sizeof(double[2]) * 1000); double *seeds[1000]; for (int i = 0; i < 1000; i++) seeds[i] = sd[i];
    char flags[1000]; int nseed = 0, ncurves = *ncurves_, cap = *cap_; MiCurve *res = *res_;
#ifdef MI_TRACE
    if (mi_trf) { fprintf(mi_trf, "IN\n"); mi_tron = 1; }
#endif
    plane_init(s, pl, multi, seeds, 1000, flags, &nseed);
#ifdef MI_TRACE
    if (mi_trf) { mi_tron = 0; fprintf(mi_trf, "INEND %d\n", nseed); for (int z = 0; z < nseed; z++) fprintf(mi_trf, "SEED %d %.17g %.17g %d\n", z, seeds[z][0], seeds[z][1], flags[z]); }
#endif
#ifdef SPINT_TRACE
    if (getenv("SPINT_INITDUMP")) { fprintf(stderr, "nseed %d pl %.9f\n", nseed, pl[3]); for (int z = 0; z < nseed; z++) fprintf(stderr, " %d (%.17g, %.17g) %d\n", z, seeds[z][0], seeds[z][1], flags[z]); }
#endif
    for (int i = 0; i < nseed; i++) {
        memset(m.cut, 0, sizeof m.cut); { uint8_t *c = m.cut; memcpy((double *)(c + 0x1f8), pl, 32); }
        m.su = seeds[i][0]; m.sv = seeds[i][1];
        if (m.su == -1.0 && m.sv == -1.0) continue;
        m.flag248 = flags[i];
        m.chord = chord; m.chord2 = chord2; m.maxstep = maxstep; m.minstep = minstep;   /* InitTolerances */
#ifdef MI_TRACE
        if (mi_trf) { fprintf(mi_trf, "MK %.17g %.17g %d\n", m.su, m.sv, m.flag248); mi_tron = 1; }
#endif
        int r = init_cut(s, &m);
        if (r == 0) r = march_top(s, &m);
#ifdef MI_TRACE
        if (mi_trf) { mi_tron = 0; fprintf(mi_trf, "MKEND %d %d\n", r, m.n); for (int k = 0; k < m.n; k++) fprintf(mi_trf, "P %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n", k, m.P[k][0], m.P[k][1], m.P[k][2], m.UV[k][0], m.UV[k][1], 0.0, 0.0); }
#endif
#ifdef SPINT_TRACE
        if (getenv("SPINT_SEEDALL")) { fprintf(stderr, "PORT seed %d r %d n %d", i, r, m.n); for (int z = 0; z < m.n; z++) fprintf(stderr, "\nM %d %.12f %.12f %.12f  %.9f %.9f %.9f  %.12f %.12f", z, m.P[z][0], m.P[z][1], m.P[z][2], m.T[z][0], m.T[z][1], m.T[z][2], m.UV[z][0], m.UV[z][1]); fprintf(stderr, "\n"); }
#endif
        if (r == 0) {
            if (2 < m.n && mi_march_hook) mi_march_hook(s, &m, mi_march_ctx);   /* ComputeIntersectionsMain(true): vtable +0x28 (BuildUnTrimmedIntersectionLoops), mcsrftrim.c */
            else if (2 < m.n) {
                int nseg = curve_fit(m.P, m.T, m.n, m.chord, cpool);
                gen_ulist(cpool, nseg, ulist);
                MiCurve whole; memset(&whole, 0, sizeof whole); to_nurb(cpool, nseg, ulist, &whole); whole.npts = m.n;
                if (F) { part_curves(s, F, &m, &whole, &res, &ncurves, &cap); free(whole.k); free(whole.p); }   /* BuildTrimmedModelSpaceIntersectionCurves 0x180495a90 */
                else { if (ncurves == cap) { cap *= 2; res = realloc(res, sizeof(MiCurve) * cap); } res[ncurves++] = whole; }
            }
            if (i + 1 < nseed) weed(s, &m, seeds, NULL, i + 1, nseed);
        } else if (r == 2) { seeds[i][0] = -1.0; seeds[i][1] = -1.0; }
    }
    free(pool); free(cpool); free(ulist); free(m.P); free(m.T); free(m.UV); free(sd);
    *res_ = res; *ncurves_ = ncurves; *cap_ = cap; return ncurves;
}

/* ---- MaCurve::JoinCurves (0x180405280) on the plane cuts: FindConnectedCurveSets 0x180401560, FindNeighbor 0x1804019b0, MaNURBSpline::JoinSplines 0x180450940 / Concatenate 0x180444420 / RegenNonUniformKnotsFromDerivRatios 0x180452940 ---- */
void mc_eval(const MiCurve *c, double t, double *P, double *D) {
    double N[3][26]; int s = span(c->k, c->ncp, c->deg, t); bders(c->deg, c->k, t, s, N);
    for (int k = 0; k < 3; k++) { P[k] = 0; if (D) D[k] = 0; for (int i = 0; i <= c->deg; i++) { P[k] += N[0][i] * c->p[(s - c->deg + i) * 3 + k]; if (D) D[k] += N[1][i] * c->p[(s - c->deg + i) * 3 + k]; } }
}
void mc_ends(const MiCurve *c, double *a, double *b) { memcpy(a, c->p, 24); memcpy(b, c->p + (c->ncp - 1) * 3, 24); }   /* GetStartPoint / GetEndPoint of a clamped curve */
/* MaCurve::GetClosedInfo (0x1804022b0): ends within tol and the middle point farther from the start than the end */
int mc_closed(const MiCurve *c, double tol) {
    double a[3], b[3], m[3], D[3]; mc_ends(c, a, b); mc_eval(c, (c->k[0] + c->k[c->nk - 1]) * 0.5, m, D);
    return !(tol < d3(a, b)) && !(d3(m, a) <= d3(a, b));
}
int mc_degenerate(const MiCurve *c, double tol) { double m[3], D[3]; mc_eval(c, (c->k[0] + c->k[c->nk - 1]) * 0.5, m, D); return len(D) <= tol; }   /* MaCurve::IsDegenerate (0x180404570) */
/* MaCurve::Reverse (0x1804086e0: lazily flags, applied by MaNURBSpline::Flip 0x18044c770: vertices reversed, knots K[n-1] + K[0] - K[i]) on an untrimmed curve */
void mc_reverse(MiCurve *c) {
    double t0 = c->k[0], t1 = c->k[c->nk - 1];
    for (int i = 0, j = c->nk - 1; i <= j; i++, j--) { double a = c->k[i], b = c->k[j]; c->k[i] = t0 + t1 - b; c->k[j] = t0 + t1 - a; }
    for (int i = 0, j = c->ncp - 1; i < j; i++, j--) for (int k = 0; k < 3; k++) { double t = c->p[i * 3 + k]; c->p[i * 3 + k] = c->p[j * 3 + k]; c->p[j * 3 + k] = t; }
}
/* MaCurve::SharesBothEndPoints (0x180408b90) */
double sq3(const double *a, const double *b) { return (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]); }
int mc_shares_both(const MiCurve *a, const MiCurve *b, double tol, int *rev) {
    double s[3], e[3], bs[3], be[3]; mc_ends(a, s, e); mc_ends(b, bs, be); double t2 = tol * tol; *rev = 0;
    double ss = sq3(bs, s), es = sq3(be, s), se = sq3(bs, e), ee = sq3(be, e);
    if ((ss < t2 || es < t2) && (se < t2 || ee < t2) && !mc_closed(a, -1) && !mc_closed(b, -1)) { if (ss < es) *rev = 1; return 1; }
    return 0;
}
/* MaCurve::FindNeighbor (0x1804019b0): the next curve from a's end (reversed in place when it is entered from its end); NULL when none or ambiguous */
MiCurve *mc_neighbor(MiCurve *a, MiCurve **L, int n, double tol, const int *used) {
    int rev = 0;
    if (n == 2) for (int i = 0; i < 2; i++) if (L[i] != a && used[i] != 1 && mc_shares_both(a, L[i], tol, &rev)) { MiCurve *o = L[i]; if (rev) mc_reverse(o); return o; }
    double aS[3], aE[3]; mc_ends(a, aS, aE); double t2 = tol * tol, bestE = 1e31, bestS = 1e31; MiCurve *to_e = NULL, *to_s = NULL; int count = 0;
    for (int i = 0; i < n; i++) {
        MiCurve *o = L[i]; if (!o || o == a || used[i] == 1) continue;
        count++; double s[3], e[3]; mc_ends(o, s, e);
        double d1 = sq3(s, aE), d2 = sq3(e, aE); if (d2 <= d1) d1 = d2; if (d1 <= bestE && d1 != bestE) { bestE = d1; to_e = o; }
        d1 = sq3(s, aS); d2 = sq3(e, aS); if (d2 <= d1) d1 = d2; if (d1 <= bestS && d1 != bestS) { bestS = d1; to_s = o; }
    }
    if (((t2 + bestS <= bestE && bestE != t2 + bestS) && to_e == to_s) || (bestS <= bestE && t2 <= bestE && bestE != t2) || (bestE <= bestS && t2 <= bestE && bestE != t2) || to_e == NULL) return NULL;
    if (count == 1 && mc_shares_both(a, to_e, tol, &rev)) { if (rev) mc_reverse(to_e); return to_e; }
    { double s[3], e[3]; mc_ends(to_e, s, e); if (sq3(e, aE) < sq3(s, aE)) mc_reverse(to_e); }
    return to_e;
}
int mc_idx(MiCurve **L, int n, const MiCurve *c) { for (int i = 0; i < n; i++) if (L[i] == c) return i; return -1; }
/* MaCurve::FindLastCrvInChain (0x1804018d0): the end of the chain seen from this curve */
MiCurve *mc_last(MiCurve *a, MiCurve **L, int n, double tol, const int *used) {
    MiCurve *q = mc_neighbor(a, L, n, tol, used); if (!q) return a;
    for (int i = 0; i < n; i++) { MiCurve *p = q; q = mc_neighbor(p, L, n, tol, used); if (!q || q == a) return p; }
    return NULL;
}
/* MaCurve::FindAllCrvsInChain (0x180401240) */
int mc_all(MiCurve *a, MiCurve **L, int n, double tol, int *used, MiCurve ***out) {
    MiCurve **S = malloc(sizeof(MiCurve *) * (n + 2)); int ns = 0, guard = 0; MiCurve *q = mc_neighbor(a, L, n, tol, used), *p;
    if (!q) { S[0] = a; used[mc_idx(L, n, a)] = 1; *out = S; return 1; }
    S[ns++] = a; p = q; q = mc_neighbor(p, L, n, tol, used);
    while (q && q != a) { int dup = 0; for (int i = 0; i < ns; i++) if (S[i] == p) dup = 1; if (!dup) S[ns++] = p; guard++; p = q; if (n < guard) break; q = mc_neighbor(q, L, n, tol, used); }
    { int dup = 0; for (int i = 0; i < ns; i++) if (S[i] == p) dup = 1; if (!dup) S[ns++] = p; }
    for (int i = 0; i < ns; i++) { int k = mc_idx(L, n, S[i]); if (k >= 0) used[k] = 1; }
    *out = S; return ns;
}
/* RegenNonUniformKnotsFromDerivRatios (0x180452940): each interior unique knot placed from the ratio of the Bezier end leg before and the start leg after (the legs by knot insertion to full multiplicity: the same Bezier points as ConvertSegmentToBezier, formats/wireframe.md) */
void mc_regen_knots(MiCurve *c) {
    int order = c->deg + 1, nk = c->nk, nu = 0, deg = c->deg; double *uq = malloc(sizeof(double) * nk), *mult = malloc(sizeof(double) * nk);
    for (int i = 0; i < nk; i++) { if (nu == 0 || c->k[i] != uq[nu - 1]) { uq[nu] = c->k[i]; mult[nu] = 1; nu++; } else mult[nu - 1] += 1; }
    int ncp = c->ncp, m = nk; double *K = malloc(sizeof(double) * (nk + nu * deg + 8)), *Q = malloc(sizeof(double) * 3 * (ncp + nu * deg + 8)); memcpy(K, c->k, sizeof(double) * nk); memcpy(Q, c->p, sizeof(double) * 3 * ncp);
    for (int q = 1; q < nu - 1; q++) { double t = uq[q]; int have = (int)mult[q];
        for (int r = have; r < deg; r++) {
            int ks = 0; while (K[ks + 1] <= t) ks++;   /* Boehm: t in [K[ks], K[ks+1]) */
            double *N = malloc(sizeof(double) * 3 * (ncp + 2));
            for (int i = 0; i <= ks - deg; i++) memcpy(N + i * 3, Q + i * 3, 24);
            for (int i = ks - deg + 1; i <= ks; i++) { double den = K[i + deg] - K[i], a = den == 0 ? 0 : (t - K[i]) / den; for (int z = 0; z < 3; z++) N[i * 3 + z] = a * Q[i * 3 + z] + (1 - a) * Q[(i - 1) * 3 + z]; }
            for (int i = ks; i < ncp; i++) memcpy(N + (i + 1) * 3, Q + i * 3, 24);
            memcpy(Q, N, sizeof(double) * 3 * (ncp + 1)); free(N); ncp++;
            for (int i = m; i > ks + 1; i--) K[i] = K[i - 1]; K[ks + 1] = t; m++;
        }
    }
    double *nw = calloc(nu, sizeof(double)), dV = 1.0; nw[0] = 0; nw[1] = 1.0;
    for (int q = 1; q < nu - 1; q++) {   /* the joint of unique knot q is control point q * deg: its left leg | B_deg - B_deg-1 |, its right leg | B_1 - B_0 | (GetUniqueKnots order) */
        int jq = q * deg; double dl = d3(Q + jq * 3, Q + (jq - 1) * 3), dr = d3(Q + (jq + 1) * 3, Q + jq * 3); if (dl < 1e-12) dl = 1.0; if (dr < 1e-12) dr = 1.0;
        dV *= dr / dl; nw[q + 1] = dV + nw[q];
    }
    if (nu > 2) { double e = nw[nu - 1]; for (int i = 1; i < nu; i++) nw[i] /= e; }
    nw[0] = 0; nw[nu - 1] = 1.0;
    int w = 0; for (int i = 0; i < order; i++) c->k[w++] = 0.0;
    for (int q = 1; q < nu - 1; q++) for (int i = 0; i < (int)mult[q]; i++) c->k[w++] = nw[q];
    for (int i = 0; i < order; i++) c->k[w++] = 1.0;
    c->nk = w; free(uq); free(mult); free(K); free(Q); free(nw);
}
/* MaNURBSpline::Concatenate (0x180444420), non-rational */
void mc_concat(MiCurve *a, const MiCurve *b) {
    int nv = a->ncp + b->ncp - 1, order = a->deg + 1, nk = order + nv; double *P = malloc(sizeof(double) * 3 * nv), *K = malloc(sizeof(double) * nk);
    memcpy(P, a->p, sizeof(double) * 3 * a->ncp); memcpy(P + a->ncp * 3, b->p + 3, sizeof(double) * 3 * (b->ncp - 1));
    int w = 0; for (int i = 0; i < a->nk - 1; i++) K[w++] = a->k[i];
    for (int i = order; i < b->nk; i++) K[w++] = b->k[i] + 1.0;
    double k0 = K[0], q = 1.0 / (K[nk - 1] - k0); for (int i = 0; i < nk; i++) K[i] = (K[i] - k0) * q;
    free(a->p); free(a->k); a->p = P; a->k = K; a->ncp = nv; a->nk = nk;
    mc_regen_knots(a);
}
/* MaNURBSpline::JoinSplines (0x180450940): each join moves both ends to the middle of the two (MoveEndPoint / MoveStartPoint), then Concatenate */
void mc_join_into(MiCurve *a, MiCurve **rest, int nrest) {
    for (int i = 0; i < nrest; i++) { MiCurve *b = rest[i]; double e[3], s[3], m[3]; memcpy(e, a->p + (a->ncp - 1) * 3, 24); memcpy(s, b->p, 24); for (int k = 0; k < 3; k++) m[k] = (e[k] + s[k]) * 0.5;
        memcpy(a->p + (a->ncp - 1) * 3, m, 24); memcpy(b->p, m, 24); mc_concat(a, b); }
}
/* MaCurve::JoinCurves / FindConnectedCurveSets (0x180401560): degenerate curves dropped; closed curves alone; the rest chained from a chain end; every set's first curve carries the join. Replaces *cin; returns the new count. tol: msf_join_tol */
int mi_join_curves(MiCurve **cin, int n, double tol, int metric) {
    MiCurve *c = *cin; if (n < 2) return n;
    double tiny = metric ? 2.5e-05 : 1e-06; MiCurve **L = malloc(sizeof(MiCurve *) * n), ***sets = malloc(sizeof(MiCurve **) * (n + 1)); int *cnt = malloc(sizeof(int) * (n + 1)), *used = calloc(n, sizeof(int)), nsets = 0, ndeg = 0;
    for (int i = 0; i < n; i++) L[i] = &c[i];
    for (int i = 0; i < n; i++) if (mc_degenerate(L[i], tiny)) { used[i] = 1; ndeg++; }
    int total = n - ndeg;
    for (int i = 0; i < n; i++) if (used[i] != 1 && mc_closed(L[i], tiny)) { used[i] = 1; sets[nsets] = malloc(sizeof(MiCurve *)); sets[nsets][0] = L[i]; cnt[nsets++] = 1; }
    for (int pass = 0; pass <= n; pass++) {
        for (int i = 0; i < n; i++) { if (used[i] == 1) continue;
            MiCurve *last = mc_last(L[i], L, n, tol, used); if (!last) continue;
            mc_reverse(last);
            MiCurve **S; int ns = mc_all(last, L, n, tol, used, &S); sets[nsets] = S; cnt[nsets++] = ns; }
        int sum = 0; for (int i = 0; i < nsets; i++) sum += cnt[i];
        if (sum == total) break;
    }
    MiCurve *o = malloc(sizeof(MiCurve) * (nsets ? nsets : 1)); int m = 0;
    for (int i = 0; i < nsets; i++) {
        MiCurve *f = sets[i][0], r = *f;   /* Copy (vtable +0x128): the first curve carries the join */
        r.p = malloc(sizeof(double) * 3 * f->ncp); memcpy(r.p, f->p, sizeof(double) * 3 * f->ncp); r.k = malloc(sizeof(double) * f->nk); memcpy(r.k, f->k, sizeof(double) * f->nk);
        mc_join_into(&r, sets[i] + 1, cnt[i] - 1);
        o[m++] = r; free(sets[i]);
    }
    for (int i = 0; i < n; i++) { free(c[i].k); free(c[i].p); }
    free(c); free(L); free(sets); free(cnt); free(used);
    *cin = o; return m;
}

/* ---- Curve Slice on a trimmed surface (formats/wireframe.md "Curve Slice: trimmed surfaces"): MaSurfPlaneIntersect::BuildTrimmedModelSpaceIntersectionCurves (0x180495a90), MaSurfSurfIntersect::BuildTrimmedModelSpacePartCurves (0x1804a2bd0), MaLoop::TrimToFace (0x1804369e0) and what they call ---- */

/* every object of a trimmed cut comes from this list and is released at the end of the plane */
ANode *g_arena;
void *ta(size_t n) { ANode *a = malloc(sizeof(ANode) + n); a->next = g_arena; g_arena = a; return memset(a + 1, 0, n); }
void ta_free(void) { while (g_arena) { ANode *n = g_arena->next; free(g_arena); g_arena = n; } }
double g_chord_dev;   /* surf_step_chord_dev */
double g_chain_tol;   /* chain_tol */

int cv_periodic(ICv *c);
void cv_eval(const ICv *c, int order, double t, double *o);
void cv_dirty(ICv *c) { c->ci = 0; c->nh = 0; c->hu = NULL; c->hp = NULL; c->hasbox = 0; }   /* SetDirty: the hunting points and the box */
double dist3(const double *a, const double *b) { double d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }; return sqrt(d[1] * d[1] + d[0] * d[0] + d[2] * d[2]); }

/* MaCurve::SetMinMaxParams (0x180408960) */
void cv_set_range(ICv *c, double a, double b);
/* MaCurve::SetStartParamOffset (0x180408a80) */
void cv_set_ofs(ICv *c, double v) { if (c->tmin <= v && v <= c->tmax) { c->ofs = v; cv_dirty(c); } }
void cv_set_range(ICv *c, double a, double b) {
    if (a <= b) { if (b <= a) return; }
    else { if (!cv_periodic(c)) return; cv_set_ofs(c, a); b = (c->tmax - a) + c->tmin + b; a = c->tmin; }
    c->t1 = b; c->t0 = a; c->dt = b - a;
}
/* MaCurve::Trim (0x180409820), MaParameterCurve::Trim (0x180467460) */
void pl_trim(IPoly *l, double a, double b, const double *ps, const double *pe);
void cv_trim(ICv *c, double a, double b) {
    if (a == b) return;
    if (c->kind == 4) {
        if (a == c->t0 && b == c->t1) return;
        cv_trim(c->inner, a, b);
        double s[9], e[9]; cv_eval(c->inner, 0, c->inner->t0, s); cv_eval(c->inner, 0, c->inner->t1, e);
        pl_trim(&c->poly, a, b, s, e);
    }
    if (a < c->tmin) a = c->tmin;
    if (c->tmax < b) b = c->tmax;
    cv_set_range(c, a, b); cv_dirty(c);
}
/* MaCubicSpline::EvaluateCrv 0x1803f47f0, MaNURBSpline::EvaluateCrv 0x1804487b0 (FindKnotSpan 0x18044c1d0 = span(); non-rational), MaCurveOnSurface::EvaluateCrv 0x18040d580 (DeltaU/VAllowable 1 on the normalised ranges), MaParameterCurve::EvaluateCrv 0x180464c50; out [0..2] P, [3..5] D; MaCurve::Evaluate 0x180400b00 for the start-parameter offset */
void cv_eval(const ICv *c, int order, double t, double *o) {
    if (c->ofs != 0.0) { double d = t + c->ofs; if (c->tmax < d) d -= c->tmax; t = d; }
    switch (c->kind) {
    case 0: { int i = 1; for (; i <= c->nseg; i++) if (t <= c->U[i]) break; if (i == c->nseg + 1) i--;
        const double *C = c->C + 12 * (i - 1); double h = c->U[i] - c->U[i - 1]; if (h < 1e-12) h = 1e-12; double s = (t - c->U[i - 1]) / h;
        for (int k = 0; k < 3; k++) { o[k] = ((C[4 * k] * s + C[4 * k + 1]) * s + C[4 * k + 2]) * s + C[4 * k + 3]; if (order > 0) o[3 + k] = ((C[4 * k] * (s * 1.5) + C[4 * k + 1]) * (s * 2.0) + C[4 * k + 2]) * (1.0 / h); }   /* C (s 1.5): asm 0x1803f49ad */
        return; }
    case 1: case 2: {   /* MaNURBSpline::EvaluateCrv 0x1804487b0: FindKnotSpan (param_3 false), the span's points (weight 1) ConvertSegmentToBezier,
                           EvaluateRationalBezierCurve on [K[span + p], K[span + ord]], the quotient rule by 1 / w (w below 1e-20: 1e-20) */
        int p = c->deg, ord = p + 1, nk = c->nk, ip = ord, hi = nk - ord, iv; const double *K = c->K; double e = K[nk - 1];
        if (K[0] <= t && t <= e) { if (ip <= hi) { long l = ip; do { if (t <= K[l]) break; l++; ip++; } while (l <= hi); } iv = ip; if (ip == hi + 1) iv = ip - 1; }
        else { iv = ip; if (e < t) iv = hi; }
        iv -= ord; if (iv < 0) iv = 0;
        double B[26][4], O[4][4], ab[2] = { K[iv + p], K[iv + ord] };
        for (int i = 0; i <= p; i++) { B[i][0] = c->P[3 * (iv + i)]; B[i][1] = c->P[3 * (iv + i) + 1]; B[i][2] = c->P[3 * (iv + i) + 2]; B[i][3] = c->W ? c->W[iv + i] : 1.0; }
        nbs_to_bezier(p, K + iv, &B[0][0]);
        int nd = order > 2 ? 2 : order; nbs_bez_eval(nd, t, p, ab, &B[0][0], O);
        double w = O[0][3]; if (w < 1e-20) w = 1e-20; double inv = 1.0 / w;
        for (int k = 0; k < 3; k++) o[k] = O[0][k] * inv;
        if (order > 0) for (int k = 0; k < 3; k++) o[3 + k] = (O[1][k] - O[1][3] * o[k]) * inv;
        if (order > 1) for (int k = 0; k < 3; k++) o[6 + k] = ((O[2][k] - o[3 + k] * 2.0 * O[1][3]) - O[2][3] * o[k]) * inv;
#ifdef MI_TRACE
        if (mi_zec && c->kind == 2 && mi_ztr()) { fprintf(mi_trf, "ZEC %d %.17g %.17g %.17g %.17g", order, t, o[0], o[1], o[2]); if (order > 0) fprintf(mi_trf, " %.17g %.17g %.17g", o[3], o[4], o[5]);
            if (getenv("TRIMSS_ZECB")) { fprintf(mi_trf, " sp %d B", iv); for (int i = 0; i <= p; i++) fprintf(mi_trf, " %.17g %.17g %.17g %.17g", B[i][0], B[i][1], B[i][2], B[i][3]); }
            fprintf(mi_trf, "\n"); }
#endif
        return; }
    case 3: { double uv[9], E[21]; cv_eval(c->inner, order, t, uv); ext_eval(c->s, order, uv[0], uv[1], E); memcpy(o, E, 24); if (order > 0) for (int k = 0; k < 3; k++) o[3 + k] = E[9 + k] * uv[4] + E[6 + k] * uv[3]; return; }
    default: cv_eval(c->inner, order, t, o); return;
    }
}
/* MaCurve::GetClosedInfo (0x1804022b0) with param -1: 1e-4 inch / 0.0025 mm: closed = the ends within tol and the start farther from the middle than from the end; periodic = closed and the end tangents within 10 deg */
void cv_closed_info(ICv *c, int *closed, int *periodic) {
    if (c->ci) { *closed = c->cl; *periodic = c->per; return; }
    double tol = c->metric ? 0.0025 : 0.0001, a[9], b[9], m[9], ts[3], te[3];
    *closed = 0; *periodic = 0; c->ci = 1; c->cl = 0; c->per = 0;
    cv_eval(c, 1, c->t0, a); memcpy(ts, a + 3, 24); unit(ts);
    cv_eval(c, 1, c->t1, b); memcpy(te, b + 3, 24); unit(te);
    cv_eval(c, 0, (c->t1 + c->t0) * 0.5, m);
    double gap = dist3(a, b);
    if (tol < gap || dist3(a, m) <= gap) return;
    *closed = 1; c->cl = 1; if (cos(0.1745329251994329) <= te[1] * ts[1] + te[0] * ts[0] + te[2] * ts[2]) { *periodic = 1; c->per = 1; }
}
int cv_closed(ICv *c) { int cl, p; cv_closed_info(c, &cl, &p); return cl; }
int cv_periodic(ICv *c) { int cl, p; cv_closed_info(c, &cl, &p); return p; }
/* MaCurve::LimitTToRange (0x180405700) */
void limit_t(ICv *c, double *t) {
    if (cv_periodic(c)) {
        if (*t < c->t0) { do { *t += c->dt; } while (*t < c->t0); return; }
        if (c->t1 < *t) { do { *t -= c->dt; } while (c->t1 < *t); }
    } else { if (*t < c->t0) *t = c->t0; if (c->t1 < *t) *t = c->t1; }
}
/* MaCurve::BinaryPointsAtTol (0x1803fe780) with tolerance 0, 10 degrees, mode 1, 5000 points; the piecewise-linear NURBS branch; a closed curve (ends within 1e-12) that gives 3 points redone with half the angle (at most 3 times) */
int cv_bin(ICv *c, double ang, double *T) {
    int n = 2, redo = 0;
    if (c->kind == 1 && c->deg == 1 && c->ncp > 2) {   /* IsPiecewiseLinearNURBCurve 0x180404cd0: the unique knots inside the range (GetUniqueKnots 0x18044eaf0) */
        n = 0; T[n++] = c->t0;
        for (int i = c->deg; i <= c->nk - c->deg - 1; i++) { double k = c->K[i]; if (i > c->deg && k == c->K[i - 1]) continue; if (c->t0 < k && k < c->t1) T[n++] = k; }
        T[n++] = c->t1; return n;
    }
    (void)redo; return bin_pts(bp_cv_ev, c, c->t0, c->t1, ang, 5000, T);
}
/* MaCurveHuntingPoints::GenCrvPoints (0x18040bc20): fewer than 5: 5 uniform; failure: 20 uniform */
void cv_hunt(ICv *c) {
    if (c->nh) return;
    double *T = malloc(sizeof(double) * 5002); int n = cv_bin(c, 10.0, T);
    if (n < 0) { n = 20; double d = (c->t1 - c->t0) / 19.0, v = c->t0; for (int i = 0; i < 20; i++) { T[i] = v; v += d; } }
    else if (n < 5) { n = 5; double v = c->t0, d = (c->t1 - c->t0) * 0.25; for (int i = 0; i < 5; i++) { T[i] = v; v += d; } }
    c->hu = ta(sizeof(double) * n); c->hp = ta(sizeof(double[3]) * n); memcpy(c->hu, T, sizeof(double) * n);
    for (int i = 0; i < n; i++) { double o[9]; cv_eval(c, 0, T[i], o); memcpy(c->hp[i], o, 24); }
    c->nh = n; free(T);
}
/* MaMinMaxBox::GenCrvBox (0x180439a30), Expand (0x1804399a0) */
double *cv_box(ICv *c) {
    if (c->hasbox) return c->box;
    cv_hunt(c); double B[6]; box_init(B);
    for (int i = 0; i < c->nh; i++) box_add(B, c->hp[i]);
    for (int i = 0; i + 1 < c->nh; i++) { double o[9]; cv_eval(c, 0, (c->hu[i + 1] + c->hu[i]) * 0.5, o); box_add(B, o); }
    box_expand(B, c->metric); memcpy(c->box, B, sizeof B); c->hasbox = 1; return c->box;
}
/* MaMinMaxBox::operator&& (0x180439040) */
int box_box(const double *a, const double *b) {
    return (b[0] < a[0] || b[0] < a[3]) && (a[0] < b[0] || a[0] < b[3]) && (b[1] < a[1] || b[1] < a[4]) && (a[1] < b[1] || a[1] < b[4]) && (b[2] < a[2] || b[2] < a[5]) && (a[2] < b[2] || a[2] < b[5]);
}
/* MaCurve::PointMinDist (0x180407490); foot: the point and the unit tangent */
void cv_pmd(ICv *c, int hunt, const double *P, double *dist, double *foot, double *tio) {
    double t = hunt ? 0.0 : *tio;
#ifdef MI_TRACE
    double t_in = hunt ? 0.0 : *tio;
#endif
    if (hunt) {
        cv_hunt(c);
        double best = 1e31; t = (c->t1 + c->t0) * 0.5;
        for (int i = 0; i < c->nh; i++) { double d = (P[0] - c->hp[i][0]) * (P[0] - c->hp[i][0]) + (P[1] - c->hp[i][1]) * (P[1] - c->hp[i][1]) + (P[2] - c->hp[i][2]) * (P[2] - c->hp[i][2]); if (d < best) { best = d; t = c->hu[i]; } }
        if (1e-20 < best || c->nh < 1) best = sqrt(best);
        for (int i = 0; i + 1 < c->nh; i++) {
            double d, u; line_pmd(c->hp[i], c->hp[i + 1], P, &d, &u);
            if (0.0 <= u && u <= 1.0 && d < best) { t = (c->hu[i + 1] - c->hu[i]) * u + c->hu[i]; best = d; }
        }
    }
    double t2 = t; limit_t(c, &t2);
    double O[9]; cv_eval(c, 1, (c->t1 + c->t0) * 0.5, O);
    if (sqrt(O[3] * O[3] + O[4] * O[4] + O[5] * O[5]) < 1e-12) {   /* degenerate: the middle */
        *tio = (c->t1 + c->t0) * 0.5; *dist = dist3(O, P); memcpy(foot, O, 24); double u[3] = { O[3], O[4], O[5] }; unit(u); memcpy(foot + 3, u, 24);
        return;
    }
    double prev = 0.0, prevt = 0.0, Q[9], Tu[3]; int iter = 0;
    for (;;) {
        double h = c->dt * 0.0001, S1[9], S2[9];
        cv_eval(c, 0, t2 - h, S1); cv_eval(c, 0, t2 + h, S2);
        cv_eval(c, 1, t2, Q);
        double l = sqrt(Q[3] * Q[3] + Q[4] * Q[4] + Q[5] * Q[5]); if (l < 1e-20) l = 1e-20; l = 1.0 / l; Tu[0] = Q[3] * l; Tu[1] = Q[4] * l; Tu[2] = Q[5] * l;
        double r = (Q[0] * Tu[0] + Q[1] * Tu[1] + Q[2] * Tu[2]) - (Tu[0] * P[0] + Tu[1] * P[1] + Tu[2] * P[2]);
        if (fabs(r) <= 1e-12 && fabs(r) != 1e-12) break;
        double d = (S1[1] * Tu[1] + S1[0] * Tu[0] + S1[2] * Tu[2]) - (S2[1] * Tu[1] + S2[0] * Tu[0] + S2[2] * Tu[2]);
        if (fabs(d) <= 1e-20 && fabs(d) != 1e-20) d = 1e-20;
        double st = (r * 0.0002) / d, st2 = st;
        if (0 < iter && 1e-20 <= fabs(prev - st)) { double a = (t2 - prevt) / (prev - st); if (0.0 < a) { double b = a * st; if (fabs(b) <= c->dt && c->dt != fabs(b)) st2 = b; } }
        if (fabs(st2) < 1e-12) break;
        double stc = st2; if (c->dt < fabs(st2)) { stc = c->dt; if (st2 < c->dt) stc = -c->dt; }
        prevt = t2; t2 += stc; limit_t(c, &t2); iter++; prev = st;
        if (0xe < iter) break;
    }
    *tio = t2; *dist = dist3(Q, P); memcpy(foot, Q, 24); memcpy(foot + 3, Tu, 24);
#ifdef MI_TRACE
    { static int zm = -1; if (zm < 0) zm = getenv("TRIMSS_ZM") != NULL; if (zm && mi_zm_loop && mi_ztr()) fprintf(mi_trf, "ZM %d %.17g %.17g %.17g t %.17g -> %.17g it %d d %.17g\n", hunt, P[0], P[1], P[2], t_in, t2, iter, *dist); }
#endif
}
/* the point on a curve */
void cv_pt(const ICv *c, double t, double *o) { double E[9]; cv_eval(c, 0, t, E); memcpy(o, E, 24); }
void cv_start(const ICv *c, double *o) { cv_pt(c, c->t0, o); }
void cv_end(const ICv *c, double *o) { cv_pt(c, c->t1, o); }
void cv_mid(const ICv *c, double *o) { cv_pt(c, (c->t1 + c->t0) * 0.5, o); }
/* MaPolyLine::Trim (0x180474e20) */
void pl_trim(IPoly *l, double a, double b, const double *ps, const double *pe) {
    int b3 = 0, b4 = 0, n = l->n;
    if (a < l->u[0]) a = l->u[0];
    if (l->u[n - 1] < b) b = l->u[n - 1];
    if ((a == l->u[0] && b == l->u[n - 1]) || a == b) return;
    int i10 = 0, i14 = 0;
    for (int i = 0; i < n; i++) { if (a <= l->u[i]) { i14 = i; i10 = i; if (a == l->u[i]) b3 = 1; break; } i14 = 0; i10 = 0; }
    int i9 = 0;
    for (;;) {
        if (n <= i14) { i9 = 0; break; }
        if (b <= l->u[i14]) { i9 = i14; if (b == l->u[i14]) b4 = 1; break; }
        i14++;
    }
    int m;
    if (b3 || b4) { m = (i9 - i10) + 1; if (i9 == i10) m = (i9 - i10) + 2; } else m = (i9 - i10) + 2;
    double (*p)[2] = ta(sizeof(double[2]) * m), *u = ta(sizeof(double) * m);
    p[0][0] = ps[0]; p[0][1] = ps[1]; int j = i10 == 0 ? 1 : i10;
    for (int q = 1; q < m - 1; q++, j++) { p[q][0] = l->p[j][0]; p[q][1] = l->p[j][1]; }
    p[m - 1][0] = pe[0]; p[m - 1][1] = pe[1];
    u[0] = a; j = i10 == 0 ? 1 : i10; for (int q = 1; q < m - 1; q++, j++) u[q] = l->u[j]; u[m - 1] = b;
    l->p = p; l->u = u; l->n = m; l->hasbox = 0;
}
/* MaPolyLine::PointMinDist (0x1804725e0), a 2D polyline against a point with z = 0: the distance and the span */
void pl_pmd(const IPoly *l, const double *P, double *dist, double *foot, int *seg) {
    double bd2 = 1e31, bd = 1e31; int bi = 0, biv = 0;
    for (int i = 0; i < l->n; i++) { double dx = P[0] - l->p[i][0], dy = P[1] - l->p[i][1], dz = P[2]; double d = dy * dy + dx * dx + dz * dz; if (d < bd2) { bi = i; bd2 = d; } }
    biv = bi;
    bd = bd2 <= 1e-20 ? bd2 : sqrt(bd2);
    double best = 1e31; int bs = 0; double f[3] = { l->p[bi][0], l->p[bi][1], 0 };
    for (int i = 0; i < l->n - 1; i++) {
        double A[3] = { l->p[i][0], l->p[i][1], 0 }, B[3] = { l->p[i + 1][0], l->p[i + 1][1], 0 }, d, t; line_pmd(A, B, P, &d, &t);
        if (0.0 <= t && t <= 1.0 && d <= best && best != d) { best = d; bs = i; double e[3] = { B[0] - A[0], B[1] - A[1], 0 }, L = len(e); if (L > 1e-6) { double q = (P[0] - A[0]) * e[0] / L + (P[1] - A[1]) * e[1] / L; (void)q; f[0] = A[0] + e[0] / L * q; f[1] = A[1] + e[1] / L * q; } else { f[0] = A[0]; f[1] = A[1]; } }
    }
    if (best < bd) { *dist = best; *seg = bs; foot[0] = f[0]; foot[1] = f[1]; foot[2] = 0; }
    else { *dist = bd; foot[0] = l->p[bi][0]; foot[1] = l->p[bi][1]; foot[2] = 0; *seg = biv; }
    if (*seg < 0) *seg = 0;
    if (l->n - 2 < *seg) *seg = l->n - 2;
}
/* MaPolyLine::IsClosed (0x180470520), dim 2 */
int pl_closed(const IPoly *l) { const double *a = l->p[l->n - 1], *b = l->p[0]; return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1])) == 0.0; }
/* MaMinMaxBox::GenPolyLineBox (0x18043a040) */
double *pl_box(IPoly *l, int metric) {
    if (l->hasbox) return l->box;
    double b[6]; b[0] = b[1] = 1e31; b[3] = b[4] = -1e31; b[2] = b[5] = 0;
    for (int i = 0; i < l->n; i++) { double p[3] = { l->p[i][0], l->p[i][1], 0 }; if (p[0] < b[0]) b[0] = p[0]; if (b[3] < p[0]) b[3] = p[0]; if (p[1] < b[1]) b[1] = p[1]; if (b[4] < p[1]) b[4] = p[1]; }
    box_expand(b, metric); memcpy(l->box, b, sizeof b); l->hasbox = 1; return l->box;
}
/* MaPolyLine::PlaneIntersection (0x180471cd0), as read: for a 2D polyline the degenerate-span branch reads x into both coordinates of the next point */
void pl_plane(IPoly *l, const double *pl, int metric, PPts *o) {
    o->n = 0; if (!box_plane(pl_box(l, metric), pl)) return;
    int cl = pl_closed(l), i = 1, n = l->n;
    while (i < n) {
        int i14 = i - 1, i11 = i;
        double A[3] = { l->p[i14][0], l->p[i14][1], 0 }, B[3] = { l->p[i][0], l->p[i][1], 0 };
        double d5 = (pl[0] * A[0] + pl[1] * A[1] + pl[2] * A[2]) - pl[3], d4 = (pl[0] * B[0] + pl[1] * B[1] + pl[2] * B[2]) - pl[3];
        double dl = sqrt((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]));
        int go = 0;
        if (1e-20 <= dl) go = 1;
        else if (i != n - 1) {
            B[0] = l->p[i + 1][0]; B[1] = l->p[i + 1][0];   /* as read: the x of the point after next in both coordinates */
            d4 = (pl[0] * B[0] + pl[1] * B[1] + pl[2] * B[2]) - pl[3];
            i11 = i + 1; i14 = i; go = 1;
        }
        if (go && ((i11 != 1) || !cl || d5 != 0.0) && d4 * d5 <= 0.0) {
            double d[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, q[3]; unit(d);
            if (pvi(pl, A, d, q) == 0) {
                if (o->n == o->cap) { o->cap = o->cap ? o->cap * 2 : 16; o->p = realloc(o->p, sizeof(double[3]) * o->cap); }
                memcpy(o->p[o->n++], q, 24);
                if (d4 == 0.0) i11++;
            }
        }
        i = i11 + 1;
    }
}
/* MaPolyLine::Sort3DPoints (0x180474090): by coordinate axis, an insertion sort (stable) */
void sort3d(double (*p)[3], int n, int axis) {
    for (int i = 1; i < n; i++) { double q[3]; memcpy(q, p[i], 24); int j = i - 1; while (0 <= j && q[axis] < p[j][axis]) { memcpy(p[j + 1], p[j], 24); j--; } memcpy(p[j + 1], q, 24); }
}
/* MaLoop::TestUVPtInPolyLineWrtX / WrtY (0x180435130, 0x1804353f0): 0 inside, 1 outside; the intersections along the other coordinate on each side counted */
int uv_in_pl(IPoly *l, const double *p3, int metric, int wrt_y) {
    double pl[4] = { wrt_y ? 0 : 1, wrt_y ? 1 : 0, 0, 0 }, cnt1 = 0, cnt2 = 0; pl[3] = p3[wrt_y ? 1 : 0] * 1.0 + p3[wrt_y ? 0 : 1] * 0.0 + p3[2] * 0.0;
    PPts o = { 0 }; pl_plane(l, pl, metric, &o);
    if (o.n < 2) { free(o.p); return 1; }
    sort3d(o.p, o.n, wrt_y ? 0 : 1);
    int axis = wrt_y ? 0 : 1, c1 = 0, c2 = 0; (void)cnt1; (void)cnt2;
    for (int i = 0; i < o.n; i++) { double d = p3[axis] - o.p[i][axis]; if (1e-06 < fabs(d)) { if (d <= 1e-06) c2++; else c1++; } }
    free(o.p);
    if (c1 != 0 && c2 != 0) { int e1 = (c1 / 2) * 2 == c1, e2 = (c2 / 2) * 2 == c2; if ((!e1 && !e1) || (e2 != e1)) return 0; }
    return 1;
}

/* ---- MaParameterCurve / MaLoop ---- */
ICv *cv_new(int kind, int metric) { ICv *c = ta(sizeof(ICv)); c->kind = kind; c->metric = metric; return c; }
ICv *cv_copy(const ICv *c);
ILoop *loop_copy(const ILoop *l);
ICv *pc_of_poly(ISrf *s, const double (*p)[2], int n, double tol);
/* MaLoop::GetLoopPolyLine: MakeLoopPolyLine (0x1804331e0) */
IPoly *loop_poly(ILoop *L) {
    if (L->lp) return L->lp;
    IPoly *q = ta(sizeof(IPoly));
    if (L->n == 1) { const IPoly *a = &L->pc[0]->poly; q->n = a->n; q->p = ta(sizeof(double[2]) * a->n); memcpy(q->p, a->p, sizeof(double[2]) * a->n); q->u = ta(sizeof(double) * a->n); memcpy(q->u, a->u, sizeof(double) * a->n); }
    else { int m = 0; for (int i = 0; i < L->n; i++) m += L->pc[i]->poly.n - 1; m += 1; q->n = m; q->p = ta(sizeof(double[2]) * m); q->u = NULL; m = 0;
        for (int i = 0; i < L->n; i++) { const IPoly *a = &L->pc[i]->poly; int f = i ? 1 : 0; memcpy(q->p + m, a->p + f, sizeof(double[2]) * (a->n - f)); m += a->n - f; }
        const IPoly *a = &L->pc[0]->poly; double se = 1; (void)se; double s0[2] = { a->p[0][0], a->p[0][1] }; const IPoly *b = &L->pc[L->n - 1]->poly; (void)b;
        /* IsClosed(this): the last point set to the first */
        { double P0[9], P1[9]; cv_eval(L->pc[0], 0, L->pc[0]->t0, P0); cv_eval(L->pc[L->n - 1], 0, L->pc[L->n - 1]->t1, P1); if (dist3(P0, P1) < g_chain_tol) { q->p[q->n - 1][0] = s0[0]; q->p[q->n - 1][1] = s0[1]; } } }
    L->lp = q; return q;
}
void loop_dirty(ILoop *L) { L->lp = NULL; L->hasbox = 0; }
/* MaMinMaxBox::GenLoopBox (0x180439f90): the union with the default box (all zeros) of the pcurves' boxes, expanded */
double *loop_box(ILoop *L) {
    if (L->hasbox) return L->box;
    double b[6] = { 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < L->n; i++) { double *o = cv_box(L->pc[i]); for (int k = 0; k < 3; k++) { if (o[k] < b[k]) b[k] = o[k]; if (b[3 + k] < o[3 + k]) b[3 + k] = o[3 + k]; } }
    box_expand(b, L->pc[0]->metric); memcpy(L->box, b, sizeof b); L->hasbox = 1; return L->box;
}
/* MaLoop::IsClosed (0x1804326a0) */
int loop_closed(const ILoop *L) { double a[3], b[3]; cv_start(L->pc[0], a); cv_end(L->pc[L->n - 1], b); return dist3(a, b) < g_chain_tol; }
/* MaLoop::StartPoint / EndPoint (0x180434f50, 0x1804314e0) */
void loop_start(const ILoop *L, double *o) { const IPoly *p = &L->pc[0]->poly; o[0] = p->p[0][0]; o[1] = p->p[0][1]; o[2] = 0; }
void loop_end(const ILoop *L, double *o) { const IPoly *p = &L->pc[L->n - 1]->poly; o[0] = p->p[p->n - 1][0]; o[1] = p->p[p->n - 1][1]; o[2] = 0; }
/* MaLoop::PointMinDist (0x180433b00): the nearest pcurve (the last of equal distances), the middle of the nearest span, PointMinDist on the pcurve */
#ifdef MI_TRACE
static void loop_pmd_raw(ILoop *L, const double *uv, double *dist, int *pidx, double *foot, double *t);
void loop_pmd(ILoop *L, const double *uv, double *dist, int *pidx, double *foot, double *t) { double a = uv[0], b = uv[1]; loop_pmd_raw(L, uv, dist, pidx, foot, t); if (mi_ztr()) { fprintf(mi_trf, "ZP %.17g %.17g -> %d %.17g %.17g\n", a, b, *pidx, *t, *dist); if (getenv("TRIMSS_ZU")) mi_zl(" LL", L); } }
#define Z_LOOP_PMD loop_pmd_raw
static
#else
#define Z_LOOP_PMD loop_pmd
#endif
void Z_LOOP_PMD(ILoop *L, const double *uv, double *dist, int *pidx, double *foot, double *t) {
    double p3[3] = { uv[0], uv[1], 0 }, d = 0, f[3], bd = 1e31; int s, bs = 0; *pidx = 0;
    for (int i = 0; i < L->n; i++) { pl_pmd(&L->pc[i]->poly, p3, &d, f, &s); if (d <= bd) { *pidx = i; bd = d; bs = s; } }
    ICv *pc = L->pc[*pidx]; *t = (pc->poly.u[bs + 1] + pc->poly.u[bs]) * 0.5;
#ifdef MI_TRACE
    mi_zm_loop = 1;
#endif
    cv_pmd(pc, 0, p3, dist, foot, t);
#ifdef MI_TRACE
    mi_zm_loop = 0;
#endif
}
/* MaLoop::UVPointRelative (0x180438770), a real loop (byte +0 == 0): 0 inside, 1 outside, 2 on */
int uv_rel(ILoop *L, const double *uv) {
    if (L->surf) {   /* byte +0 set: the surface's range (u0 u1 v0 v1 of Surface(this) = the first pcurve's surface); on within 1e-6; beyond a periodic direction's range inside when the other is in its range */
        ISrf *s = L->pc[0]->s; EDGES(s); double u = uv[0], v = uv[1];
        if (fabs(u - s->d[0]) <= 1e-06 || fabs(u - s->d[1]) <= 1e-06 || fabs(v - s->d[2]) <= 1e-06 || fabs(v - s->d[3]) <= 1e-06) return 2;
        if (u <= s->d[0] || s->d[1] <= u || v <= s->d[2] || s->d[3] <= v) {
            if (s->per[1] && v_beyond(s, v) && !u_beyond(s, u)) return 0;
            if (s->per[0] && u_beyond(s, u) && !v_beyond(s, v)) return 0;
            return 1;
        }
        return 0;
    }
    double p3[3] = { uv[0], uv[1], 0 }, d = 0, foot[6]; int b = 0, bi = 0, bs = 0, s = 0; double bd = 1e31;
    for (int i = 0; i < L->n; i++) { double f[3]; pl_pmd(&L->pc[i]->poly, p3, &d, f, &s); if (d <= 0.001 && d <= bd && bd != d) { b = 1; bd = d; bs = s; bi = i; } }
    if (b) {
        ICv *pc = L->pc[bi]; double t = (pc->poly.u[bs + 1] + pc->poly.u[bs]) * 0.5, dc; cv_pmd(pc, 0, p3, &dc, foot, &t);
        if (dc <= 1e-06) return 2;
    }
    IPoly *pl = loop_poly(L); int a = uv_in_pl(pl, p3, L->pc[0]->metric, 0), c = uv_in_pl(pl, p3, L->pc[0]->metric, 1);
    if (a != c) return 1;
    return a;
}
/* MaCurve::Length(a, b, ..) 0x180405570: 16-point Gauss-Legendre on [a, b] (nodes and weights of mccore .rdata 0x180d5b330 / 0x180d5b3b0) */
double cv_len(const ICv *c, double a, double b) {
    static const double X[16] = { 0.00529953250417514, 0.0277124884633838, 0.067184398806084206, 0.122297795822499, 0.191061877798678, 0.27099161117138598, 0.35919822461037099, 0.45249374508118101, 0.54750625491881899, 0.64080177538962901, 0.72900838882861396, 0.808938122201322, 0.877702204177501, 0.93281560119391602, 0.97228751153661597, 0.99470046749582497 };
    static const double W[16] = { 0.013576229705877, 0.031126761969323902, 0.0475792558412464, 0.062314485627766897, 0.074797994408288299, 0.084578259697501199, 0.091301707522461806, 0.094725305227534196, 0.094725305227534196, 0.091301707522461806, 0.084578259697501199, 0.074797994408288299, 0.062314485627766897, 0.0475792558412464, 0.031126761969323902, 0.013576229705877 };
    double sum = 0, s = fabs(b - a);
    for (int i = 0; i < 16; i++) { double o[9]; cv_eval(c, 1, a + s * X[i], o); sum += W[i] * s * sqrt(o[3] * o[3] + o[4] * o[4] + o[5] * o[5]); }
    return sum;
}
/* MaCurve::ParamAtArcLength (0x180406910): returns 1 when the curve is shorter than |dist| */
int cv_param_at(const ICv *c, double dist, double t, double *out) {
    int fw = 0.0 <= dist; double lo = t, hi = c->t1, L; *out = t;
    if (dist < 0.0) { lo = c->t0; hi = t; }
    L = cv_len(c, lo, hi);
    if (!(fabs(dist) <= L)) return 1;
    double g = (fabs(dist) / L) * (hi - lo), v = c->t0;
    if (fw) v = t + g; else if (dist < 0.0) v = t - g;
    for (int i = 0; i < 15; i++) {
        double l, a = lo, b = v; if (!(0.0 <= dist) && fw) { a = v; b = hi; }
        if (!(0.0 <= dist)) { a = v; b = hi; }
        l = cv_len(c, a, b);
        if (fabs(fabs(dist) - l) <= 1e-12) break;
        double o[9]; cv_eval(c, 1, v, o); double sp = sqrt(o[3] * o[3] + o[4] * o[4] + o[5] * o[5]);
        if (fw) v += (fabs(dist) - l) / sp; else v -= (fabs(dist) - l) / sp;
        if (v < lo) v = lo;
        if (hi < v) v = hi;
    }
    *out = v; return 0;
}
/* MaLoop::SampleStartDirectionRelativeToLoop (0x180434040, +0.001: .rdata 0x180cf9cb0) and SampleEndDirection (0x180433f50, -0.001: 0x180d5d020) */
int sample_start(ILoop *L, ILoop *o) {
    int i = 0; double u = 0;
    for (; i < L->n; i++) if (cv_param_at(L->pc[i], 0.001, L->pc[i]->t0, &u) == 0) break;
    if (i >= L->n) i = L->n - 1;
    double E[9]; cv_eval(L->pc[i], 0, u, E); return uv_rel(o, E);
}
int sample_end(ILoop *L, ILoop *o) {
    int i = L->n - 1; double u = 0;
    for (; 0 <= i; i--) if (cv_param_at(L->pc[i], -0.001, L->pc[i]->t1, &u) == 0) break;
    if (i < 0) i = 0;
    double E[9]; cv_eval(L->pc[i], 0, u, E); return uv_rel(o, E);
}
/* MaLoopTrimData */
LTD *td_new(int n) { LTD *t = ta(sizeof(LTD)); t->n = n; t->a = ta(sizeof(int) * (n + 4)); t->t = ta(sizeof(double) * (n + 4)); return t; }
void lid_add(LID *d, int a, int b, double ta_, double tb) {   /* MaLoopIntersectData::Add 0x180438c60 */
    if (d->n == d->cap) { d->cap = d->cap ? d->cap * 2 : 16; int *na = ta(sizeof(int) * d->cap * 2); double *nt = ta(sizeof(double) * d->cap * 2); if (d->n) { memcpy(na, d->a, sizeof(int) * d->n); memcpy(na + d->cap, d->b, sizeof(int) * d->n); memcpy(nt, d->ta, sizeof(double) * d->n); memcpy(nt + d->cap, d->tb, sizeof(double) * d->n); } d->a = na; d->b = na + d->cap; d->ta = nt; d->tb = nt + d->cap; }
    d->a[d->n] = a; d->b[d->n] = b; d->ta[d->n] = ta_; d->tb[d->n] = tb; d->n++;
}
void pld_add(PLD *d, int a, int b, const double *q) {
    if (d->n == d->cap) { d->cap = d->cap ? d->cap * 2 : 16; d->a = realloc(d->a, sizeof(int) * d->cap); d->b = realloc(d->b, sizeof(int) * d->cap); d->p = realloc(d->p, sizeof(double[3]) * d->cap); }
    d->a[d->n] = a; d->b[d->n] = b; memcpy(d->p[d->n], q, 24); d->n++;
}
/* MaLine::LineIntersect (0x18042b360), points in 3D; dir = end - start (MaLine::DirCosines 0x18042ae30, not unit) */
void line_int(const double *A, const double *Ad, const double *B, const double *Bd, double *o0, double *o1, double *o2, double *t1, double *t2, int *st) {
    *st = 2; *t1 = -1.0; *t2 = -1.0;
    double d9 = Ad[2], d8 = Ad[1], d2 = Ad[0], d7 = Bd[2], f8 = Bd[0], f0 = Bd[1];
    double d4 = d7 * d8 - f0 * d9, d1 = d9 * f8 - d7 * d2, d3 = f0 * d2 - d8 * f8, d10 = sqrt(d1 * d1 + d4 * d4 + d3 * d3);
    if (d10 < 1e-20) return;
    if (d10 == 0.0) { d3 = d1 = d4 = 0; } else { d4 /= d10; d1 /= d10; d3 /= d10; }
    double d5 = d3 * d8 - d1 * d9, de2 = d9 * d4 - d3 * d2, d11 = d1 * Ad[0] - d8 * d4;
    d10 = sqrt(de2 * de2 + d5 * d5 + d11 * d11);
    if (d10 == 0.0) return;
    if (d10 == 0.0) { d11 = de2 = d5 = 0; } else { d5 /= d10; de2 /= d10; d11 /= d10; }
    double t = ((A[0] - B[0]) * d5 + (A[1] - B[1]) * de2 + (A[2] - B[2]) * d11) / (de2 * f0 + d5 * f8 + d11 * d7);
    o2[0] = f8 * t + B[0]; o2[1] = f0 * t + B[1]; o2[2] = d7 * t + B[2];
    double d2b = f0 * d3 - d1 * d7, d1b = d1 * f8 - f0 * d4, d3b = d7 * d4 - d3 * f8, dn = sqrt(d3b * d3b + d2b * d2b + d1b * d1b);
    if (dn == 0.0) return;
    double s = ((B[3] - A[0]) * (d2b / dn) + (B[4] - A[1]) * (d3b / dn) + (B[5] - A[2]) * (d1b / dn)) / ((d3b / dn) * d8 + (d2b / dn) * d2 + (d1b / dn) * d9);
    o1[0] = d2 * s + A[0]; o1[1] = d8 * s + A[1]; o1[2] = d9 * s + A[2];
    double g = sqrt((o2[1] - o1[1]) * (o2[1] - o1[1]) + (o2[0] - o1[0]) * (o2[0] - o1[0]) + (o2[2] - o1[2]) * (o2[2] - o1[2]));
    if (1e-12 < g) *st = 1; else { memcpy(o0, o1, 24); *st = 0; }
    *t1 = s; *t2 = t;
}
/* MaPolyLine::FindAllPolyLineIntersections (0x18046eeb0), 2D polylines */
void pl_pl(const IPoly *a, const IPoly *b, PLD *out) {
    out->n = 0; double cs = cos(0.00017453292519943288), p0[3] = { 0, 0, 0 };   /* local_408: LineIntersect fills it only on status 0 */
    for (int i14 = 0; i14 < a->n - 1; i14++) {
        double s0[3] = { a->p[i14][0], a->p[i14][1], 0 }, e0[3] = { a->p[i14 + 1][0], a->p[i14 + 1][1], 0 }, d4 = e0[0] - s0[0], d2 = e0[1] - s0[1], d3 = 0.0, L = sqrt(d4 * d4 + d2 * d2 + d3 * d3);
        double nx = d2 * 1.0 - 0.0 * 0.0, ny = d3 * 0.0 - d4 * 1.0, nz = d4 * 0.0 - d2 * 0.0, m = sqrt(nx * nx + ny * ny + nz * nz);   /* direction x (0, 0, 1): (dVar5, dVar3, dStack_438) */
        if (m == 0.0) { nx = ny = nz = 0; } else { nx /= m; ny /= m; nz /= m; }
        double D = ny * s0[1] + nx * s0[0] + nz * s0[2];
        int i13 = 1;
        if (1 < b->n) { int i12 = 0;
            do {
                double s1[3] = { b->p[i12][0], b->p[i12][1], 0 }, e1[3] = { b->p[i13][0], b->p[i13][1], 0 }, L2 = sqrt((e1[0] - s1[0]) * (e1[0] - s1[0]) + (e1[1] - s1[1]) * (e1[1] - s1[1]));
                double f1 = (s1[1] * ny + s1[0] * nx + s1[2] * nz) - D, f2 = (e1[1] * ny + e1[0] * nx + e1[2] * nz) - D, B6[6] = { s1[0], s1[1], 0, e1[0], e1[1], 0 };
                if (f2 * f1 <= 1e-06) {
                    double ad[3] = { d4, d2, d3 }, bd[3] = { e1[0] - s1[0], e1[1] - s1[1], 0 }, p1[3], p2[3], t1, t2; int st;
                    line_int(s0, ad, B6, bd, p0, p1, p2, &t1, &t2, &st);
                    int skip = 0;
                    if (t1 < 0.0 || 1.0 < t1 || t2 < 0.0 || 1.0 < t2) { if (st != 0) skip = 1; } else st = 0;
                    if (!skip) {
                        if (i14 == 0 && t1 < 0.0 && -1e-09 <= t2 && t2 <= 1.000000001 && fabs(L * t1) <= 1e-06 && fabs(L * t1) != 1e-06) { t1 = 0.0; if (0.0 <= t2) { if (1.0 < t2) t2 = 1.0; } else t2 = 0.0; }
                        if (i12 == 0 && t2 <= 0.0 && t2 != 0.0 && -1e-09 <= t1 && t1 <= 1.000000001 && fabs(L2 * t2) <= 1e-06 && fabs(L2 * t2) != 1e-06) { t2 = 0.0; if (0.0 <= t1) { if (1.0 < t1) t1 = 1.0; } else t1 = 0.0; }
                        if (i14 == a->n - 2 && 1.0 < t1 && -1e-09 <= t2 && t2 <= 1.000000001 && (t1 - 1.0) * L < 1e-06) { t1 = 1.0; if (0.0 < t2 || t2 == 0.0) { if (!(t2 < 1.0 || t2 == 1.0)) t2 = 1.0; } else t2 = 0.0; }
                        int ok = 1;
                        if (!(i12 != b->n - 2 || t2 < 1.0 || t2 == 1.0 || t1 < -1e-09 || 1.000000001 < t1 || 1e-06 <= (t2 - 1.0) * L2)) {
                            t2 = 1.0; if (0.0 <= t1) { if (1.0 < t1) { t1 = 1.0; } } else t1 = 0.0;   /* LAB_18046f5fe skipped when t1 was clamped as read */
                        } else { if (t1 < 0.0 || 1.0 < t1 || t2 < 0.0) ok = 0; }
                        if (ok && t2 <= 1.0) {
                            double ua[3] = { ad[0], ad[1], ad[2] }, ub[3] = { bd[0], bd[1], bd[2] }; unit(ua); unit(ub);
                            if (fabs(ub[0] * ua[0] + ub[1] * ua[1] + ub[2] * ua[2]) < cs) {
                                int dup = 0; for (int k = 0; k < out->n; k++) if (sqrt((p0[0] - out->p[k][0]) * (p0[0] - out->p[k][0]) + (p0[1] - out->p[k][1]) * (p0[1] - out->p[k][1]) + (p0[2] - out->p[k][2]) * (p0[2] - out->p[k][2])) < 1e-06) { dup = 1; break; }
                                if (!dup) pld_add(out, i14, i12, p0);
                                if (f1 == 0.0 || f2 == 0.0) { i13++; i12++; }
                            }
                        }
                    }
                }
                i13++; i12++;
            } while (i13 < b->n);
        }
    }
}
/* MaCurve::IntersectCrvCrv (0x1804030c0): 50 iterations; param_1 [lo, hi] of curve a; the guesses t[0] (a), t[1] (b) */
#ifdef MI_TRACE
static void cc_int_raw(ICv *a, const double *lim, ICv *b, double *t, double tol, double *res, int *st);
void cc_int(ICv *a, const double *lim, ICv *b, double *t, double tol, double *res, int *st) { double t0 = t[0], t1 = t[1]; cc_int_raw(a, lim, b, t, tol, res, st); if (mi_ztr()) { fprintf(mi_trf, "ZI %.17g %.17g lim %.17g %.17g tol %g -> %d %.17g %.17g %.17g\n", t0, t1, lim[0], lim[1], tol, *st, t[0], t[1], *res);
    if (getenv("TRIMSS_ZIEV")) { double A[9], B[9]; cv_eval(a, 1, t[0], A); cv_eval(b, 1, t[1], B); fprintf(mi_trf, " ev %.17g %.17g %.17g %.17g | %.17g %.17g %.17g %.17g\n", A[0], A[1], A[3], A[4], B[0], B[1], B[3], B[4]); } } }
#define Z_CC_INT cc_int_raw
static
#else
#define Z_CC_INT cc_int
#endif
void Z_CC_INT(ICv *a, const double *lim, ICv *b, double *t, double tol, double *res, int *st) {
    *st = 1; double best = 1e31, ta = t[0], tb = t[1], foot[6]; int it = 0;
    for (;;) {
        double A[9], B[9]; cv_eval(a, 1, ta, A); cv_eval(b, 1, tb, B);
        double dx = A[0] - B[0], dy = A[1] - B[1], dz = A[2] - B[2], dd = dy * dy + dx * dx + dz * dz;
        if (dd <= tol * tol) { t[0] = ta; t[1] = tb; *res = sqrt(dd); *st = 0; return; }
        double dl = sqrt(dd);
        if (dl <= best && best != dl) { t[0] = ta; t[1] = tb; best = dl; }
        double ax = A[3], ay = A[4], az = A[5], bx = B[3], by = B[4], bz = B[5], l1 = sqrt(ax * ax + ay * ay + az * az), l2 = sqrt(bx * bx + by * by + bz * bz);
        double ux = 0, uy = 0, uz = 0, vx = 0, vy = 0, vz = 0; if (l1 != 0.0) { ux = ax / l1; uy = ay / l1; uz = az / l1; } if (l2 != 0.0) { vx = bx / l2; vy = by / l2; vz = bz / l2; }
        double a1 = uy * uy + ux * ux + uz * uz, a2 = vy * uy + vx * ux + vz * uz, a3 = vy * vy + vx * vx + vz * vz, det = a3 * a1 - a2 * a2, q = 1e-12; if (1e-12 <= fabs(det)) q = det; q = 1.0 / q;
        double r1 = ux * dx + uy * dy + uz * dz, r2 = vx * dx + vy * dy + vz * dz;
        if ((-r1 == 0.0) && (-r2 == 0.0)) {
            double aa = l1 <= 1e-12 ? 0.0 : 1.0 / (ax * ax + ay * ay + az * az), bb = l2 <= 1e-12 ? 0.0 : 1.0 / (bx * bx + by * by + bz * bz);
            ta = ta + (ax * dx + ay * dy + az * dz) * aa; tb = tb - (bx * dx + by * dy + bz * dz) * bb;
        } else {
            double s1 = (-r1 * a3 - -r2 * a2) * q, s2 = (-r1 * a2 - -r2 * a1) * q;
            double p1[3] = { s1 * ux + A[0], s1 * uy + A[1], s1 * uz + A[2] }, p2[3] = { s2 * vx + B[0], s2 * vy + B[1], s2 * vz + B[2] };
            double d0; cv_pmd(a, 0, p1, &d0, foot, &ta); cv_pmd(b, 0, p2, &d0, foot, &tb);
        }
        if (ta < lim[0]) ta = lim[0];
        if (lim[1] < ta) ta = lim[1];
        if (tb < b->t0) tb = b->t0;
        if (b->t1 < tb) tb = b->t1;
        it++;
        if (0x31 < it) { *res = best; return; }
    }
}
/* MaParameterCurve::IsFullyCoincidentWith (0x1804662c0) */
int pc_full_coinc(ICv *a, ICv *b) {
    double tol = a->tol, st[3], en[3], f[6], t = 0, d, E1[21], E2[21];
    cv_start(a, st); is_eval(a->s, 0, st[0], st[1], E1); cv_pmd(b, 1, st, &d, f, &t); is_eval(b->s, 0, f[0], f[1], E2);
    if (tol < dist3(E1, E2)) return 0;
    cv_end(a, en); is_eval(a->s, 0, en[0], en[1], E1); cv_pmd(b, 1, en, &d, f, &t); is_eval(b->s, 0, f[0], f[1], E2);
    if (tol < dist3(E1, E2)) return 0;
    if (2 < a->poly.n) for (int i = 0; i < a->poly.n; i++) {
        double p[3] = { a->poly.p[i][0], a->poly.p[i][1], 0 }; cv_pmd(b, 1, p, &d, f, &t);
        is_eval(a->s, 0, p[0], p[1], E1); is_eval(b->s, 0, f[0], f[1], E2);
        if (tol < dist3(E1, E2)) return 0;
    }
    return 1;
}
/* MaCurve::AlignStartPointWithCurve (0x1803fdff0) */
void cv_align(ICv *c, ICv *o) {
    if (c->ofs != 0.0) cv_set_ofs(c, 0.0);
    double a[3], m[3], b[3], d, f[6], ta0 = 0, tm = 0, tb = 0;
    cv_start(o, a); cv_mid(o, m); cv_end(o, b);
    cv_pmd(c, 1, a, &d, f, &ta0); cv_pmd(c, 1, m, &d, f, &tm); cv_pmd(c, 1, b, &d, f, &tb);
    if (tm <= ta0 || (tb <= tm && (tm <= ta0 || tb <= tm))) cv_set_ofs(c, ta0);
}
/* MaCurve::OverlapRange (0x180405f40) */
int cv_overlap(ICv *th, ICv *o, double tol, double *r1, double *r2, double *dmax, double *davg, double *amax) {
    double d9 = tol * 10.0, f[6], d[6], t[6];
    r1[0] = th->t0; r1[1] = th->t1; r2[0] = o->t0; r2[1] = o->t1; *dmax = -1e31; *amax = 0.0; *davg = 0.0;
    int c1 = cv_closed(th), c2 = cv_closed(o);
    if (c1 && !c2) cv_align(th, o); else if (c2 && !c1) cv_align(o, th);
    double a[3], am[3], b[3], ao[3], amo[3], bo[3], f1[6], f2[6], f3[6], f4[6], f5[6], f6[6];
    cv_start(th, a); cv_mid(th, am); cv_end(th, b);
    t[0] = t[1] = t[2] = t[3] = t[4] = t[5] = 0;
    cv_pmd(o, 1, a, &d[0], f1, &t[0]); cv_pmd(o, 1, b, &d[1], f2, &t[1]); cv_pmd(o, 1, am, &d[2], f3, &t[2]);
    cv_start(o, ao); cv_mid(o, amo); cv_end(o, bo);
    cv_pmd(th, 1, ao, &d[3], f4, &t[3]); cv_pmd(th, 1, bo, &d[4], f5, &t[4]); cv_pmd(th, 1, amo, &d[5], f6, &t[5]);
    int b2 = !(d9 < d[3] || d9 < d[4] || d9 < d[5]), b3 = !(d9 < d[0] || d9 < d[1] || d9 < d[2]);
    (void)f;
    if (!c1) {
        if (!c2) {
            if (b2 && !b3) { cv_pmd(o, 1, f4, &d[0], f1, &t[0]); cv_pmd(o, 1, f5, &d[1], f2, &t[1]); }
            r2[0] = t[0]; r2[1] = t[1];
        } else {
            double v = t[1];
            if ((t[0] < t[2] && t[2] < t[1]) || (t[2] < t[0] && t[1] < t[2])) r2[0] = t[0];
            else { double e = t[0] - o->t0; if (1e-06 < fabs(e) || fabs(e) == 1e-06) { if (fabs(t[1] - o->t1) <= 1e-06 && fabs(t[1] - o->t1) != 1e-06) v = o->t0; r2[0] = t[0]; } else r2[0] = o->t1; }
            r2[1] = v;
        }
    } else { r2[0] = o->t0; r2[1] = o->t1; }
    if (!c2) {
        if (!c1) {
            if (!b2 && b3) { cv_pmd(th, 1, f1, &d[3], f4, &t[3]); cv_pmd(th, 1, f2, &d[4], f5, &t[4]); }
            r1[0] = t[3]; r1[1] = t[4];
        } else {
            double v = t[4];
            if ((t[3] < t[5] && t[5] < t[4]) || (t[5] < t[3] && t[4] < t[5])) r1[0] = t[3];
            else { double e = t[3] - th->t0; if (1e-06 < fabs(e) || fabs(e) == 1e-06) { if (fabs(t[4] - th->t1) <= 1e-06 && fabs(t[4] - th->t1) != 1e-06) v = th->t0; r1[0] = t[3]; } else r1[0] = th->t1; }
            r1[1] = v;
        }
    } else { r1[0] = th->t0; r1[1] = th->t1; }
    if (r1[1] < r1[0]) { double v = r1[0]; r1[0] = r1[1]; r1[1] = v; }
    if (r2[1] < r2[0]) { double v = r2[0]; r2[0] = r2[1]; r2[1] = v; }
    double dt2 = r2[1] - r2[0], dt1 = r1[1] - r1[0];
    if (!(1e-06 < fabs(dt2) || fabs(dt2) == 1e-06) || !(1e-06 < fabs(dt1) || fabs(dt1) == 1e-06)) return 1;
    double v = r2[0];
    for (int i = 0; i < 5; i++) {
        double Q[9], dl, foot[6], tq = 0; cv_eval(o, 1, v, Q); cv_pmd(th, 1, Q, &dl, foot, &tq);
        *davg += dl; if (*dmax <= dl && dl != *dmax) *dmax = dl;
        { double u[3] = { Q[3], Q[4], Q[5] }; unit(u); double x = fabs(u[0] * foot[3] + foot[4] * u[1] + foot[5] * u[2]); if (1.0 < x) x = 1.0; x = acos(x) * 57.29577951308233; if (*amax <= x && x != *amax) *amax = x; }
        v += dt2 * 0.25;
    }
    v = r1[0];
    for (int i = 0; i < 5; i++) {
        double Q[9], dl, foot[6], tq = 0; cv_eval(th, 1, v, Q); cv_pmd(o, 1, Q, &dl, foot, &tq);
        *davg += dl; if (*dmax <= dl && dl != *dmax) *dmax = dl;
        { double u[3] = { Q[3], Q[4], Q[5] }; unit(u); double x = fabs(foot[3] * u[0] + foot[4] * u[1] + foot[5] * u[2]); if (1.0 < x) x = 1.0; x = acos(x) * 57.29577951308233; if (*amax <= x && x != *amax) *amax = x; }
        v += dt1 * 0.25;
    }
    *davg /= 10.0;
    return 0;
}
/* MaParameterCurve::IsPartiallyCoincidentWithLoop (0x180466a70), MaCurveOnSurface (0x18040bfb0) on both pcurves; ranges = the first curve's overlap pairs */
int pc_partial(ICv *pc, ILoop *L, double (**out)[2], int *n) {
    double tol = pc->tol; *n = 0; *out = NULL; double (*q)[2] = NULL; int m = 0, cap = 0;
    ICv cos1; memset(&cos1, 0, sizeof cos1); cos1.kind = 3; cos1.metric = pc->metric; cos1.inner = pc; cos1.s = pc->s; cos1.tmin = pc->tmin; cos1.tmax = pc->tmax; cos1.t0 = pc->t0; cos1.t1 = pc->t1; cos1.dt = cos1.t1 - cos1.t0;
    for (int i = 0; i < L->n; i++) {
        ICv cos2; memset(&cos2, 0, sizeof cos2); cos2.kind = 3; cos2.metric = pc->metric; cos2.inner = L->pc[i]; cos2.s = L->pc[i]->s; cos2.tmin = L->pc[i]->tmin; cos2.tmax = L->pc[i]->tmax; cos2.t0 = L->pc[i]->t0; cos2.t1 = L->pc[i]->t1; cos2.dt = cos2.t1 - cos2.t0;
        double r1[2], r2[2], dmax, davg, amax;
        int r = cv_overlap(&cos1, &cos2, tol, r1, r2, &dmax, &davg, &amax);
        if (r == 0 && dmax <= tol && amax <= 5.0) { if (m == cap) { cap = cap ? cap * 2 : 4; q = realloc(q, sizeof(double[2]) * cap); } q[m][0] = r1[0]; q[m][1] = r1[1]; m++; }
    }
    if (m) { *out = ta(sizeof(double[2]) * m); memcpy(*out, q, sizeof(double[2]) * m); }
    *n = m; free(q); return m != 0;
}
/* MaLoop::Trim (0x180435eb0), Copy of pcurves, Concatenate, ConcatentateLoops (0x180430d00) */
ILoop *loop_of(ICv **pc, int n) { ILoop *L = ta(sizeof(ILoop)); L->n = n; L->pc = ta(sizeof(ICv *) * n); memcpy(L->pc, pc, sizeof(ICv *) * n); return L; }
void loop_concat(ILoop *a, const ILoop *b) {   /* MaLoop::Concatenate 0x1804307a0: copies of the pcurves */
    int n = a->n + b->n; ICv **pc = ta(sizeof(ICv *) * n); for (int i = 0; i < a->n; i++) pc[i] = cv_copy(a->pc[i]); for (int i = 0; i < b->n; i++) pc[a->n + i] = cv_copy(b->pc[i]); a->pc = pc; a->n = n; a->lp = NULL; a->hasbox = 0;
}
double uv_dist(const double *a, const double *b) { return sqrt((b[1] - a[1]) * (b[1] - a[1]) + (b[0] - a[0]) * (b[0] - a[0]) + 0.0); }
void concat_loops(ILoop ***R, int *n) {
    double s[3], e[3];
    loop_start((*R)[0], s); loop_end((*R)[*n - 1], e);
    double d = uv_dist(s, e);
    if (1e-06 < d) {
        if (*n == 2) { double a[3], b[3]; loop_end((*R)[0], a); loop_start((*R)[1], b); if (uv_dist(a, b) <= 1e-06) { loop_concat((*R)[0], (*R)[1]); *n = 1; } }
    } else {
        loop_concat((*R)[*n - 1], (*R)[0]);
        for (int i = 0; i + 1 < *n; i++) (*R)[i] = (*R)[i + 1];
        *n = *n - 1;
    }
}
#ifdef MI_TRACE
static int loop_trim_raw(const ILoop *L, const LTD *d, int *pn, ILoop ***pout, int concat);
int loop_trim(const ILoop *L, const LTD *d, int *pn, ILoop ***pout, int concat) { if (mi_ztr()) { fprintf(mi_trf, "ZR %d %d", concat, d->n); for (int i = 0; i < d->n; i++) fprintf(mi_trf, " %d %.17g", d->a[i], d->t[i]); fprintf(mi_trf, "\n"); mi_zl(" L", L); } int r = loop_trim_raw(L, d, pn, pout, concat); if (mi_ztr()) { fprintf(mi_trf, "ZRE %d %d\n", r, *pn); if (r == 0) for (int i = 0; i < *pn; i++) mi_zl(" l", (*pout)[i]); } return r; }
#define Z_LOOP_TRIM loop_trim_raw
static
#else
#define Z_LOOP_TRIM loop_trim
#endif
int Z_LOOP_TRIM(const ILoop *L, const LTD *d, int *pn, ILoop ***pout, int concat) {
    *pout = NULL; *pn = 0;
    if (1 < d->n) {
        int n = d->n / 2; *pn = n; ILoop **R = ta(sizeof(ILoop *) * (n ? n : 1)); int m = 0;
        for (int k = 0; k < n; k++) {
            int i7 = d->a[2 * k], i3 = d->a[2 * k + 1], i8 = (i3 - i7) + 1; double t1 = d->t[2 * k], t2 = d->t[2 * k + 1];
            if (i8 == 1) {
                if (t1 != t2) { ICv *c = cv_copy(L->pc[i7]); cv_trim(c, t1, t2); R[m++] = loop_new(&c, 1);   /* MaLoop(MaParameterCurve *) 0x18042e5c0: CheckForClosure */ }
            } else {
                ICv **pc = ta(sizeof(ICv *) * i8); int cnt = 0;
                if (t1 != L->pc[i7]->t1) { pc[cnt] = cv_copy(L->pc[i7]); cv_trim(pc[cnt], t1, L->pc[i7]->t1); cnt++; }
                for (int j = i7 + 1; j < i3; j++) pc[cnt++] = cv_copy(L->pc[j]);
                if (L->pc[i3]->t0 != t2) { pc[cnt] = cv_copy(L->pc[i3]); cv_trim(pc[cnt], L->pc[i3]->t0, t2); cnt++; }
                if (0 < cnt) R[m++] = loop_new(pc, cnt);   /* MaLoop(MaParameterCurve **, int) 0x18042e570: CheckForClosure */
            }
        }
        *pn = m;
        if (m != 0) { *pout = R; if (1 < m && concat) concat_loops(pout, pn); return 0; }
    }
    return 1;
}
ICv *cv_copy(const ICv *c) {
    ICv *o = ta(sizeof(ICv)); *o = *c; o->nh = 0; o->hu = NULL; o->hp = NULL; o->hasbox = 0;
    if (c->kind == 0) { o->C = ta(sizeof(double) * 12 * c->nseg); memcpy(o->C, c->C, sizeof(double) * 12 * c->nseg); o->U = ta(sizeof(double) * (c->nseg + 1)); memcpy(o->U, c->U, sizeof(double) * (c->nseg + 1)); }
    else if (c->kind == 1) { o->K = ta(sizeof(double) * c->nk); memcpy(o->K, c->K, sizeof(double) * c->nk); o->P = ta(sizeof(double) * 3 * c->ncp); memcpy(o->P, c->P, sizeof(double) * 3 * c->ncp); if (c->W) { o->W = ta(sizeof(double) * c->ncp); memcpy(o->W, c->W, sizeof(double) * c->ncp); } }
    else if (c->kind == 4) { o->inner = cv_copy(c->inner); o->poly.p = ta(sizeof(double[2]) * c->poly.n); memcpy(o->poly.p, c->poly.p, sizeof(double[2]) * c->poly.n); o->poly.u = ta(sizeof(double) * c->poly.n); memcpy(o->poly.u, c->poly.u, sizeof(double) * c->poly.n); o->poly.hasbox = 0; }
    return o;
}
ILoop *loop_copy(const ILoop *l) {
    ILoop *o = ta(sizeof(ILoop)); o->n = l->n; o->pc = ta(sizeof(ICv *) * l->n); for (int i = 0; i < l->n; i++) o->pc[i] = cv_copy(l->pc[i]);
    if (l->lp) { o->lp = ta(sizeof(IPoly)); o->lp->n = l->lp->n; o->lp->p = ta(sizeof(double[2]) * l->lp->n); memcpy(o->lp->p, l->lp->p, sizeof(double[2]) * l->lp->n); if (l->lp->u) { o->lp->u = ta(sizeof(double) * l->lp->n); memcpy(o->lp->u, l->lp->u, sizeof(double) * l->lp->n); } }
    o->hasbox = l->hasbox; memcpy(o->box, l->box, sizeof o->box); o->surf = l->surf;   /* MaLoop(const MaLoop &) 0x18042e3b0: *this = *param_1 (byte +0) */
    return o;
}
/* MaLoop::Travel (0x180435720); dir: param_3 */
int loop_travel(ILoop *L, const double *a, const double *b, int dir, ILoop **out) {
    *out = NULL; double f[6], d, ta0 = 0, tb = 0; int ia = 0, ib = 0;
    loop_pmd(L, a, &d, &ia, f, &ta0); loop_pmd(L, b, &d, &ib, f, &tb);
    int n = L->n; LTD *td = NULL;
    if (dir) {
        if (ia < ib) td = td_new(2);
        else if (ib < ia) { td = td_new(4); td->a[0] = ia; td->t[0] = ta0; td->a[1] = n - 1; td->t[1] = L->pc[n - 1]->t1; td->a[2] = 0; td->t[2] = L->pc[0]->t0; td->a[3] = ib; td->t[3] = tb; goto trim; }
        else td = td_new(2);
        td->a[0] = ia; td->t[0] = ta0; td->a[1] = ib; td->t[1] = tb;
    } else {
        if (ia < ib) { td = td_new(4); td->a[0] = ib; td->t[0] = tb; td->a[1] = n - 1; td->t[1] = L->pc[n - 1]->t1; td->a[2] = 0; td->t[2] = L->pc[0]->t0; td->a[3] = ia; td->t[3] = ta0; goto trim; }
        if (ib < ia) td = td_new(2); else td = td_new(2);
        td->a[0] = ib; td->t[0] = tb; td->a[1] = ia; td->t[1] = ta0;
    }
trim:;
    int nr = 0; ILoop **R = NULL; int r = loop_trim(L, td, &nr, &R, 1);
    if (r == 1 || nr == 0 || !R) return 1;
    if (r != 2) { *out = R[0]; return 0; }
    return r;
}
/* MaLoop::IsCoincidentWithLoop (0x1804328c0) */
int loop_coinc(ILoop *L, ILoop *o) {
    double tol = L->pc[0]->tol, p[3], a[3], b[3], f[6], d, E1[21], E2[21], tt; int s; ISrf *S = L->pc[0]->s, *So = o->pc[0]->s;
    IPoly *lp = loop_poly(L);
    loop_start(L, a); loop_pmd(o, a, &d, &s, f, &tt); is_eval(S, 0, a[0], a[1], E1); is_eval(So, 0, f[0], f[1], E2);
    double dA = dist3(E1, E2);
    if (dA > tol) return 0;
    loop_end(L, b); loop_pmd(o, b, &d, &s, f, &tt); is_eval(S, 0, b[0], b[1], E1); is_eval(So, 0, f[0], f[1], E2);
    double dB = dist3(E1, E2);
    if (dB > tol) return 0;
    /* the middle of the first pcurve (vtable +0x100 = TMid): the unit tangent there, the foot's tangent */
    double m[9], Tm[3], fm[6]; ICv *p0 = L->pc[0]; cv_eval(p0, 1, (p0->t1 + p0->t0) * 0.5, m); memcpy(Tm, m + 3, 24); unit(Tm);
    double uvm[3] = { m[0], m[1], 0 }; loop_pmd(o, uvm, &d, &s, fm, &tt);
    if (2 < lp->n) for (int i = 0; i < lp->n; i++) {
        p[0] = lp->p[i][0]; p[1] = lp->p[i][1]; p[2] = 0; loop_pmd(o, p, &d, &s, f, &tt);
        is_eval(S, 0, p[0], p[1], E1); is_eval(So, 0, f[0], f[1], E2);
        if (tol < dist3(E1, E2)) return 0;
    }
    ILoop *part = NULL; int ok = loop_travel(o, a, b, 0.0 < fm[3] * Tm[0] + fm[4] * Tm[1] + fm[5] * Tm[2], &part);
    if (ok == 0) {
        IPoly *q = loop_poly(part);
        if (2 < q->n) for (int i = 0; i < q->n; i++) {
            double uv[3] = { q->p[i][0], q->p[i][1], 0 }; loop_pmd(L, uv, &d, &s, f, &tt);
            is_eval(part->pc[0]->s, 0, q->p[i][0], q->p[i][1], E1); is_eval(S, 0, f[0], f[1], E2);
            if (tol < dist3(E1, E2)) return 0;
        }
    }
    return 1;
}
/* MaLoop::FindAllLoopIntersections (0x180431a30) */
#ifdef MI_TRACE
static LID *loop_loop_raw(ILoop *L, ILoop *o);
LID *loop_loop(ILoop *L, ILoop *o) { LID *d = loop_loop_raw(L, o); if (mi_ztr()) { fprintf(mi_trf, "ZX %d", d->n); for (int i = 0; i < d->n; i++) fprintf(mi_trf, " %d %d %.17g %.17g", d->a[i], d->b[i], d->ta[i], d->tb[i]); fprintf(mi_trf, "\n"); } return d; }
#define Z_LOOP_LOOP loop_loop_raw
static
#else
#define Z_LOOP_LOOP loop_loop
#endif
LID *Z_LOOP_LOOP(ILoop *L, ILoop *o) {
    LID *d = ta(sizeof(LID)); int fc[512], nfc = 0;
    if (!box_box(loop_box(L), loop_box(o))) return d;
    for (int i = 0; i < L->n; i++) {
        ICv *pc = L->pc[i]; double *b1 = cv_box(pc->inner);
        for (int j = 0; j < o->n; j++) { ICv *q = o->pc[j]; if (box_box(b1, cv_box(q->inner)) && pc_full_coinc(pc, q)) { if (nfc < 512) fc[nfc++] = i; i++; break; } }
    }
    int start = 0;
    for (int i = 0; i < L->n; i++) {
        int skip = 0; for (int k = 0; k < nfc; k++) if (fc[k] == i) skip = 1;
        if (skip) continue;
        ICv *pc = L->pc[i]; IPoly *pa = &pc->poly; ICv *c1 = pc->inner; double *b1 = cv_box(c1);
        for (int j = 0; j < o->n; j++) {
            ICv *q = o->pc[j]; IPoly *pb = &q->poly; ICv *c2 = q->inner;
            if (!box_box(b1, cv_box(c2))) continue;
            PLD X = { 0 }; pl_pl(pa, pb, &X);
            for (int k = 0; k < X.n; k++) {
                int sa = X.a[k], sb = X.b[k]; double tt[2], lim[2], res = 0, f[6]; int st = 0;
                tt[0] = (pa->u[sa + 1] + pa->u[sa]) * 0.5; tt[1] = (pb->u[sb + 1] + pb->u[sb]) * 0.5;
                lim[0] = sa < 1 ? c1->t0 : pa->u[sa - 1];
                lim[1] = sa < pa->n - 2 ? pa->u[sa + 2] : c1->t1;
                cc_int(c1, lim, c2, tt, 1e-09, &res, &st);
                if (st != 0) { cv_pmd(c1, 0, X.p[k], &res, f, &tt[0]); cv_pmd(c2, 0, X.p[k], &res, f, &tt[1]); }
                if (cv_closed(c1) && tt[0] == c1->t1) tt[0] = c1->t0;
                lid_add(d, i, j, tt[0], tt[1]);
            }
            free(X.a); free(X.b); free(X.p);
        }
        if (0 < d->n - start) { int m = d->n - start; double *v = d->ta + start;
            /* MaNetSurf::SortDoubleArray: only this curve's parameters are sorted, the permutation is discarded */
            for (int a = 1; a < m; a++) { double x = v[a]; int b = a - 1; while (0 <= b && x < v[b]) { v[b + 1] = v[b]; b--; } v[b + 1] = x; }
            start = d->n; }
    }
    if (0 < d->n) {
        LID *r = ta(sizeof(LID)); lid_add(r, d->a[0], d->b[0], d->ta[0], d->tb[0]);
        for (int i = 1; i < d->n; i++) {
            double A[9], B[9]; cv_eval(L->pc[d->a[i - 1]], 0, d->ta[i - 1], A); cv_eval(L->pc[d->a[i]], 0, d->ta[i], B);
            if (1e-06 <= dist3(A, B)) lid_add(r, d->a[i], d->b[i], d->ta[i], d->tb[i]);
        }
        d = r;
    }
    if (0 < d->n && loop_closed(L) && nfc == 0) { int all = 1; for (int i = 0; i < d->n; i++) if (1e-06 <= fabs(d->ta[i]) && 1e-06 <= fabs(d->ta[i] - 1.0)) { all = 0; break; } if (all) d->n = 0; }
    return d;
}
/* MaLoop::TrimToLoop (0x180437750) */
#ifdef MI_TRACE
static int trim_to_loop_raw(ILoop *L, ILoop *o, int p2, LID *ld, int *pn, ILoop ***pout);
int trim_to_loop(ILoop *L, ILoop *o, int p2, LID *ld, int *pn, ILoop ***pout) { int r = trim_to_loop_raw(L, o, p2, ld, pn, pout); if (mi_ztr()) { fprintf(mi_trf, "ZT %d %d %d\n", p2, r, *pn); if (r == 0) for (int i = 0; i < *pn; i++) mi_zl(" l", (*pout)[i]); } return r; }
#define Z_TRIM_TO_LOOP trim_to_loop_raw
static
#else
#define Z_TRIM_TO_LOOP trim_to_loop
#endif
int Z_TRIM_TO_LOOP(ILoop *L, ILoop *o, int p2, LID *ld, int *pn, ILoop ***pout) {
    *pn = 0; *pout = NULL;
    int i8 = L->n, cl = loop_closed(L); double uv[3]; loop_start(L, uv); int Pv4 = uv_rel(o, uv); loop_end(L, uv); int Pv5 = uv_rel(o, uv);
    int i19 = ld->n, i15 = i19; if (i19 == 0) return 3;
    int b21;
    if (p2) { if (Pv4 == 1) i15++; b21 = Pv5 == 1; } else { if (Pv4 == 0) i15++; b21 = Pv5 == 0; }
    if (b21) i15++;
    LTD *td = td_new(i15);
    int l20 = i19, s0 = 0, Pv6;
    if (!((1 < (unsigned)(i19 - 1)) || Pv4 != 2 || Pv5 != 2 || cl)) {
        Pv6 = sample_start(L, o);
        if (!p2) {
            if (Pv6 == 0) { *pout = ta(sizeof(ILoop *)); (*pout)[0] = loop_copy(L); *pn = 1; return 0; }
            if ((unsigned)(Pv6 - 1) < 2) return 2;
        } else {
            if ((unsigned)(Pv6 - 1) < 2) { *pout = ta(sizeof(ILoop *)); (*pout)[0] = loop_copy(L); *pn = 1; return 0; }
            if (Pv6 == 0) return 2;
            goto L948;
        }
    }
    if (Pv4 == 2) goto L948;
    goto Lb81;
L948:
    s0 = 0; Pv6 = sample_start(L, o);
    if (cl) {
        double E[9]; cv_eval(L->pc[ld->a[0]], 0, (ld->ta[1] + ld->ta[0]) * 0.5, E); uv[0] = E[0]; uv[1] = E[1]; uv[2] = 0; int Pv7 = uv_rel(o, uv);
        if ((p2 && Pv6 == 0) || (!p2 && Pv6 == 1)) {
            if (Pv7 == 1) {
                if (ld->ta[0] <= 0.5 && ld->ta[0] != 0.5 && 0 < i19 - 1) { for (int i = 0; i < i19 - 1; i++) { ld->a[i] = ld->a[i + 1]; ld->ta[i] = ld->ta[i + 1]; } }
                ld->a[i19 - 1] = i8 - 1; ld->ta[i19 - 1] = L->pc[L->n - 1]->t1;
            }
        } else if ((p2 && Pv6 == 1) || (!p2 && Pv6 == 0)) {
            if (Pv7 == 1) {
                if (0.5 < ld->ta[l20 - 1] && 1 < l20) { for (int i = 1; i < l20; i++) { ld->a[i] = ld->a[i - 1]; ld->ta[i] = ld->ta[i - 1]; } }   /* as read: ascending */
                ld->a[0] = 0; ld->ta[0] = L->pc[0]->t0;
            }
        }
        goto Lb81b;
    } else if (p2) { if (Pv6 == 0) { s0 = 1; td->n--; } }
    else if (Pv6 == 1) { s0 = 1; td->n--; }
    goto Lcont;
Lb81:
    s0 = 0;   /* LAB_180437b81: iVar15 = 0 */
    goto Lcont;
Lb81b:
    s0 = 0;
Lcont:;
    int i17 = 0, tail;
    if (Pv5 != 2 || cl) {
LBc3:
        i17 = 0;
        if (!p2) { if (Pv4 != 0) goto c07; }
        else { if (Pv4 != 1) goto c07; }
        td->a[0] = 0;
    } else {
        Pv6 = sample_end(L, o);
        if (p2) { if (Pv6 == 0) { i19--; td->n--; } goto LBc3; }
        if (Pv6 != 1) goto LBc3;
        i19--; td->n--;
        i17 = 0; if (Pv4 != 0) goto c07; td->a[0] = 0;
    }
    td->t[0] = L->pc[0]->t0; i17 = 1;
c07:
    for (long q = s0; q < (long)i19; q++) { td->a[i17] = ld->a[q]; td->t[i17] = ld->ta[q]; i17++; }
    tail = p2 ? Pv5 == 1 : Pv5 == 0;
    if (tail) { td->a[i17] = i8 - 1; td->t[i17] = L->pc[L->n - 1]->t1; }
    int rr = loop_trim(L, td, pn, pout, 1); int ret = 3; if (*pn != 0) ret = rr;
    return ret;
}
/* MaFace::IsUVActive (0x180416140) */
int face_active(MiFace *f, const double *uv) {
    int r = uv_rel(f->outer, uv); if (!((r & ~2) == 0)) return 0;
    for (int i = 0; i < f->ninner; i++) { r = uv_rel(f->inner[i], uv); if ((r & ~2) == 0) return 0; }
    return 1;
}
/* MaLoop::TrimToFace (0x1804369e0): 0 = results, 1 = whole (the caller keeps a copy), 2 = none, 3 = interrupted */
int trim_to_face(ILoop *L, MiFace *F, int *pn, ILoop ***pout) {
    *pn = 0; *pout = NULL; int nres = 0, iret = 1;
    ILoop **res = NULL;
    if (loop_coinc(L, F->outer)) { res = ta(sizeof(ILoop *)); res[0] = loop_copy(L); nres = 1; }
    else {
        if (L->n == 1) {
            double (*r)[2]; int m = 0;
            if (pc_partial(L->pc[0], F->outer, &r, &m) && 0 < m) {
                res = ta(sizeof(ILoop *) * m); for (int i = 0; i < m; i++) { res[i] = loop_copy(L); cv_trim(res[i]->pc[0], r[i][0], r[i][1]); }
                nres = m; goto Lactive;
            }
        }
        { int cl = loop_closed(L); LID *ld = loop_loop(L, F->outer); int nc = ld->n;
          if (nc == 0) {
              double uv[3]; loop_start(L, uv); int Pv = uv_rel(F->outer, uv);
              if (Pv != 0) { *pn = 0; *pout = NULL; if (Pv == 2 && !cl) return 1; return 2; }
              res = ta(sizeof(ILoop *)); res[0] = loop_copy(L); nres = 1;
          } else {
              if (nc == 1) {
                  double uv[3]; loop_start(L, uv); int Pv = uv_rel(F->outer, uv); double uv2[3]; loop_end(L, uv2); int Pe = uv_rel(F->outer, uv2);
                  if (cl) goto L_ca3;
                  if (Pv == 2) { if (Pe == 1) return 2; } else if (Pv == 1 && Pe == 2) return 2;
              } else {
              L_ca3:
                  if (cl && 1 < nc && (nc / 2) * 2 != nc) { if (ld->n != 0) ld->n--; }
              }
              int rl = trim_to_loop(L, F->outer, 0, ld, &nres, &res);
              if (rl != 0) return 1;
          }
        }
    }
Lactive:;
    iret = nres;
    if (F->ninner == 0) { *pn = iret; *pout = res; return 0; }
    {
        int total = 0, cap = 8; ILoop **out = malloc(sizeof(ILoop *) * cap);
        for (int r = 0; r < nres; r++) {
            ILoop *cur = res[r]; int iv31 = 0, idx_n = 0; double *ts = NULL; int *as = NULL; int done_p = 0;
            for (int j = 0; j < F->ninner; j++) {
                if (loop_coinc(cur, F->inner[j])) continue;
                if (cur->n == 1) {
                    double (*q)[2]; int m = 0;
                    if (pc_partial(cur->pc[0], F->inner[j], &q, &m) && 0 < m) {
                        for (int k = 0; k < m; k++) { ILoop *c = loop_copy(cur); cv_trim(c->pc[0], q[k][0], q[k][1]); if (total == cap) { cap *= 2; out = realloc(out, sizeof(ILoop *) * cap); } out[total++] = c; }
                        done_p = 1; continue;
                    }
                }
                LID *ld = loop_loop(cur, F->inner[j]);
                if (ld->n != 0) { ts = realloc(ts, sizeof(double) * (iv31 + ld->n)); as = realloc(as, sizeof(int) * (iv31 + ld->n)); memcpy(ts + iv31, ld->ta, sizeof(double) * ld->n); memcpy(as + iv31, ld->a, sizeof(int) * ld->n); iv31 += ld->n; }
            }
            (void)done_p; (void)idx_n;
            if (iv31 == 0) {
                free(ts); free(as);
                double uv[3]; loop_start(cur, uv);
                if (face_active(F, uv)) { if (total == cap) { cap *= 2; out = realloc(out, sizeof(ILoop *) * cap); } out[total++] = cur; }
            } else {
                double (*X)[3] = malloc(sizeof(double[3]) * iv31);
                for (int i = 0; i < iv31; i++) { X[i][0] = as[i]; X[i][1] = ts[i]; X[i][2] = 0; }
                sort3d(X, iv31, 0);   /* Sort3DPoints(.., 0): by pcurve index */
                for (int i = 0; i < iv31; i++) { as[i] = (int)X[i][0]; ts[i] = X[i][1]; }
                free(X);
                /* per pcurve: the parameters sorted (MaLoopTrimData copies, MaNetSurf::SortDoubleArray) */
                for (int i = 0; i < iv31; ) { int j = i; while (j < iv31 && as[j] == as[i]) j++; for (int a = i + 1; a < j; a++) { double x = ts[a]; int b = a - 1; while (i <= b && x < ts[b]) { ts[b + 1] = ts[b]; b--; } ts[b + 1] = x; } i = j; }
                double us[3], ue[3]; loop_start(cur, us); loop_end(cur, ue);
                int b8 = face_active(F, us), b9 = face_active(F, ue), c = iv31 + (b8 ? 1 : 0) + (b9 ? 1 : 0), q = 0;
                LTD *td = td_new(c);
                if (b8) { td->a[0] = 0; td->t[0] = cur->pc[0]->t0; q = 1; }
                for (int i = 0; i < iv31; i++) { td->a[q] = as[i]; td->t[q] = ts[i]; q++; }
                if (b9) { td->a[q] = cur->n - 1; td->t[q] = cur->pc[cur->n - 1]->t1; }
                free(ts); free(as);
                int nt = 0; ILoop **T = NULL; int rr = loop_trim(cur, td, &nt, &T, 1);
                if (rr == 0) for (int i = 0; i < nt; i++) { if (total == cap) { cap *= 2; out = realloc(out, sizeof(ILoop *) * cap); } out[total++] = T[i]; }
            }
        }
        *pn = total;
        if (0 < total) { *pout = ta(sizeof(ILoop *) * total); memcpy(*pout, out, sizeof(ILoop *) * total); }
        free(out);
    }
    return 0;
}
/* a MaParameterCurve from a polyline (0x180464590 -> GenPiecewiseLinearPCurve 0x180465290): MaNURBSpline(MaPolyLine*) 0x180442f30 of degree 1, the uniform knots regenerated from the chord ratios (RegenNonUniformKnotsFromDerivRatios 0x180452940: a span's length proportional to the (u, v) chord, a chord below 1e-12 counted as 1; normalised); GenUVPolyLineFromNURB 0x180465480: the vertices with the unique knots */
ICv *pc_of_poly(ISrf *s, const double (*p)[2], int n, double tol) {
    int nk = n + 2; ICv *c = cv_new(1, s->metric); c->deg = 1; c->ncp = n; c->nk = nk; c->K = ta(sizeof(double) * nk); c->P = ta(sizeof(double) * 3 * n);
    for (int i = 0; i < n; i++) { c->P[3 * i] = p[i][0]; c->P[3 * i + 1] = p[i][1]; c->P[3 * i + 2] = 0; }
    double *t = ta(sizeof(double) * n); t[0] = 0; if (n > 1) t[1] = 1; double sp = 1;
    for (int i = 1; i + 1 < n; i++) {   /* the legs as RegenNonUniformKnotsFromDerivRatios measures them: sqrt(dx dx + dy dy + dz dz) of the points / w (w = 1, z = 0) */
        double ax = p[i][0] - p[i - 1][0], ay = p[i][1] - p[i - 1][1], bx = p[i + 1][0] - p[i][0], by = p[i + 1][1] - p[i][1];
        double la = sqrt(ax * ax + ay * ay), lb = sqrt(bx * bx + by * by); if (la < 1e-12) la = 1; if (lb < 1e-12) lb = 1; sp = sp * (lb / la); t[i + 1] = sp + t[i]; }
    double T = t[n - 1];
    c->K[0] = c->K[1] = 0; for (int i = 1; i < n - 1; i++) c->K[i + 1] = t[i] / T; c->K[nk - 1] = c->K[nk - 2] = 1;
    c->tmin = 0; c->tmax = 1; c->t0 = 0; c->t1 = 1; c->dt = 1;
    ICv *q = cv_new(4, s->metric); q->inner = c; q->s = s; q->tol = tol; q->tmin = 0; q->tmax = 1; q->t0 = 0; q->t1 = 1; q->dt = 1;
    q->poly.n = n; q->poly.p = ta(sizeof(double[2]) * n); memcpy(q->poly.p, p, sizeof(double[2]) * n); q->poly.u = ta(sizeof(double) * n); q->poly.u[0] = 0; for (int i = 1; i < n - 1; i++) q->poly.u[i] = t[i] / T; if (n > 1) q->poly.u[n - 1] = 1;
    return q;
}
/* a MaParameterCurve from a march (0x180464390 -> GenPCurve 0x180464da0 with param_4 = 0): the (u, v) tangents through ExtEvaluate(2) + DuvDtEvaluate(.., true, ..), MaCubicSpline(n, uv, tangents) = BuildPtVtSpline (0x1803f1fa0: unit tangents, MaCubicCurve::BestFitDerivs, ConvertToAlgebraic, GenUList); GenUVPolyLine 0x1804652f0: the spline's nodes */
ICv *pc_of_march(ISrf *s, double **P, double **T, double **UV, int n, double tol) {
    double (*uv)[3] = ta(sizeof(double[3]) * n), (*tn)[3] = ta(sizeof(double[3]) * n);
    for (int i = 0; i < n; i++) {
        double t[3] = { T[i][0], T[i][1], T[i][2] }, E[21], du = 0, dv = 0; unit(t);
        ext_eval(s, 2, UV[i][0], UV[i][1], E);
        duvdt(E, P[i], t, &du, &dv);
        tn[i][0] = du; tn[i][1] = dv; tn[i][2] = 0; unit(tn[i]);
        uv[i][0] = UV[i][0]; uv[i][1] = UV[i][1]; uv[i][2] = 0;
    }
    int nseg = n - 1; ICv *c = cv_new(0, s->metric); c->nseg = nseg; c->C = ta(sizeof(double) * 12 * nseg); c->U = ta(sizeof(double) * (nseg + 1));
    for (int i = 0; i < nseg; i++) { double t0[3], t1[3]; memcpy(t0, tn[i], 24); memcpy(t1, tn[i + 1], 24); unit(t0); unit(t1); Cub Cb; best_fit(uv[i], t0, uv[i + 1], t1, &Cb); cub_store(c->C + 12 * i, &Cb); }
    gen_ulist(c->C, nseg, c->U);
    c->tmin = c->U[0]; c->tmax = c->U[nseg]; c->t0 = c->tmin; c->t1 = c->tmax; c->dt = c->t1 - c->t0;
    ICv *q = cv_new(4, s->metric); q->inner = c; q->s = s; q->tol = tol; q->tmin = c->tmin; q->tmax = c->tmax; q->t0 = c->t0; q->t1 = c->t1; q->dt = c->dt;
    q->poly.n = nseg + 1; q->poly.p = ta(sizeof(double[2]) * (nseg + 1)); q->poly.u = ta(sizeof(double) * (nseg + 1));
    for (int i = 0; i <= nseg; i++) { double o[9]; cv_eval(c, 0, c->U[i], o); q->poly.p[i][0] = o[0]; q->poly.p[i][1] = o[1]; q->poly.u[i] = c->U[i]; }
    return q;
}
/* MaCubicCurve::SetStartPoint / SetEndPoint (0x1803eec60, 0x1803eebc0) on the first / last segment of a MaCubicSpline (MoveStartPoint 0x1803fb480, MoveEndPoint 0x1803fb2f0): to geometric form, the end replaced, back to algebraic */
void cub_move(double *C, const double *v, int end) {
    for (int k = 0; k < 3; k++) {
        double a = C[4 * k], b = C[4 * k + 1], c = C[4 * k + 2], d = C[4 * k + 3], P0 = d, P1 = c + d + b + a, D0 = c, D1 = b * 2.0 + c + a * 3.0;
        if (end) P1 = v[k]; else P0 = v[k];
        C[4 * k] = (P0 - P1) * 2.0 + D0 + D1; C[4 * k + 1] = ((P1 - P0) * 3.0 - D0 * 2.0) - D1; C[4 * k + 2] = D0; C[4 * k + 3] = P0;
    }
}
/* MaLoop::MoveStartPoint / MoveEndPoint (0x1804335a0, 0x180433520) -> MaParameterCurve::MoveStartPoint (0x180467140): not trimmed at this point of the chain (SnapEndPoints runs on the fresh loop) */
void loop_move(ILoop *L, const double *v, int end) {
    ICv *pc = L->pc[end ? L->n - 1 : 0]; if (pc->dt != pc->inner->tmax - pc->inner->tmin) { /* MaCurve::Truncate is not reached on this path */ }
    cv_dirty(pc); cv_dirty(pc->inner);
    if (pc->inner->kind == 0) cub_move(pc->inner->C + 12 * (end ? pc->inner->nseg - 1 : 0), v, end);
    else { int i = end ? pc->inner->ncp - 1 : 0; pc->inner->P[3 * i] = v[0]; pc->inner->P[3 * i + 1] = v[1]; pc->inner->P[3 * i + 2] = v[2]; }
    int i = end ? pc->poly.n - 1 : 0; pc->poly.p[i][0] = v[0]; pc->poly.p[i][1] = v[1];
    if (L->lp) { int j = end ? L->lp->n - 1 : 0; L->lp->p[j][0] = v[0]; L->lp->p[j][1] = v[1]; }
}
/* ---- MaParameterCurve faceting / Break / Truncate and the MaLoop constructors' CheckForClosure (moved from mcsrftrim.c: MaLoop::Trim's
   loops are built by MaLoop(MaParameterCurve *) 0x18042e5c0 / MaLoop(MaParameterCurve **, int) 0x18042e570, both CheckForClosure when valid) ---- */
/* MaCurveOnSurface evaluation (0x18040d580 through MaCurve::Evaluate): the point on the surface (ExtEvaluate) and the (u, v) of the uv curve */
void cos_eval(ISrf *s, const ICv *uvc, double t, double *P, double *uv) {
    double o[9]; cv_eval(uvc, 0, t, o); uv[0] = o[0]; uv[1] = o[1];
    double E[21]; ext_eval(s, 0, o[0], o[1], E); memcpy(P, E, 24);
}
/* MaLine::PointMinDist 0x18042ba60: the distance to the infinite line, the parameter (projection / length) and the foot (start + unit direction * projection) */
void maline_pmd(const double *a, const double *b, const double *q, double *dist, double *t, double *foot) {
    double e[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, L = sqrt(e[1] * e[1] + e[0] * e[0] + e[2] * e[2]);
    double r[3] = { q[0] - a[0], q[1] - a[1], q[2] - a[2] }, R = sqrt(r[1] * r[1] + r[0] * r[0] + r[2] * r[2]);
    if (1e-06 < L) {
        double u[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 };
        if (R != 0.0) { u[0] = r[0] / R; u[1] = r[1] / R; u[2] = r[2] / R; }
        if (L != 0.0) { n[0] = e[0] / L; n[1] = e[1] / L; n[2] = e[2] / L; }
        double d = n[1] * u[1] + n[0] * u[0] + n[2] * u[2], pr = d * R;
        if (fabs(d) == 1.0 || R == 0.0) *dist = 0.0; else { double w = 1.0 - d * d; *dist = 1e-20 <= w ? sqrt(w) * R : 0.0; }
        foot[0] = n[0] * pr + a[0]; foot[1] = n[1] * pr + a[1]; foot[2] = n[2] * pr + a[2];
        if (1e-20 < L) { *t = pr / L; return; }
    } else { *dist = R; foot[0] = a[0]; foot[1] = a[1]; foot[2] = a[2]; }
    *t = 0.0;
}
/* MaCurveOnSurface::BinaryUVPointsAtTol 0x18040ca10: the uv curve's parameters subdivided (5000 at most) until, at 0.2 / 0.4 / 0.6 / 0.8 of each
   span, the surface point is within tol of the model-space chord and of the surface point at the foot on the (u, v) chord (MaLine::PointMinDist,
   ExtEvaluate); the pending ends kept on a stack. A closed result of 3 points is redone with half the tolerance (3 times at most). 0 ok, 1 too many. */
int binary_uv(ISrf *s, const ICv *uvc, double tol, IPoly *out) {
    static const double F[4] = { 0.2, 0.4, 0.6, 0.8 };
    for (int redo = 0;;) {
        int cap = 5000; double *T = malloc(sizeof(double) * cap), (*SP)[3] = malloc(sizeof(double[3]) * cap), (*SU)[2] = malloc(sizeof(double[2]) * cap), (*O)[2] = malloc(sizeof(double[2]) * cap);
        int nT = 2, nst = 0, no = 0; T[0] = uvc->t0; T[1] = uvc->t1;
        double P0[3], U0[2], P1[3], U1[2]; cos_eval(s, uvc, T[0], P0, U0); cos_eval(s, uvc, T[1], P1, U1);
        O[no][0] = U0[0]; O[no][1] = U0[1]; no++;   /* the stack of pending ends starts empty (the end written at [0] is overwritten by the first push) */
        int k = 0, fail = 0;
        while (k < nT - 1) {
            double d[3] = { P1[0] - P0[0], P1[1] - P0[1], P1[2] - P0[2] }, l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l == 0.0) d[0] = d[1] = d[2] = 0.0; else { d[0] /= l; d[1] /= l; d[2] /= l; }
            double dl = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); int bad = 0;
            for (int f = 0; f < 4; f++) {
                double Q[3], QU[2]; cos_eval(s, uvc, (T[k + 1] - T[k]) * F[f] + T[k], Q, QU);
                double w[3] = { Q[0] - P0[0], Q[1] - P0[1], Q[2] - P0[2] }, e;
                if (dl < 1e-06) e = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
                else { double c0 = w[2] * d[1] - w[1] * d[2], c1 = d[2] * w[0] - w[2] * d[0], c2 = w[1] * d[0] - d[1] * w[0]; e = sqrt(c1 * c1 + c0 * c0 + c2 * c2); }
                double a3[3] = { U0[0], U0[1], 0 }, b3[3] = { U1[0], U1[1], 0 }, q3[3] = { QU[0], QU[1], 0 }, dist, tt, ft[3]; maline_pmd(a3, b3, q3, &dist, &tt, ft);
                double E[21]; ext_eval(s, 0, ft[0], ft[1], E);
                double g = sqrt((Q[0] - E[0]) * (Q[0] - E[0]) + (Q[1] - E[1]) * (Q[1] - E[1]) + (Q[2] - E[2]) * (Q[2] - E[2]));
                if (e < g) e = g;
                if (tol < e) { bad = 1; break; }
            }
            if (bad) {
                if (nst == cap) { fail = 1; break; }
                SP[nst][0] = P1[0]; SP[nst][1] = P1[1]; SP[nst][2] = P1[2]; SU[nst][0] = U1[0]; SU[nst][1] = U1[1]; nst++;
                double tm = (T[k + 1] - T[k]) * 0.5 + T[k]; cos_eval(s, uvc, tm, P1, U1);
                memmove(T + k + 2, T + k + 1, sizeof(double) * (nT - k - 1)); nT++;
                if (nT - 1 == 4999) { fail = 1; break; }
                T[k + 1] = (T[k + 2] + T[k]) * 0.5;
                continue;
            }
            memcpy(P0, P1, 24); memcpy(U0, U1, 16); O[no][0] = U1[0]; O[no][1] = U1[1]; no++;
            if (0 < nst) { nst--; memcpy(P1, SP[nst], 24); memcpy(U1, SU[nst], 16); }   /* the next pending end: the stack's top */
            k++;
        }
        free(SP); free(SU);
        if (fail) { free(T); free(O); return 1; }
        if (nT == 3 && redo < 3) {
            redo++;
            double dd = hypot(O[no - 1][0] - O[0][0], O[no - 1][1] - O[0][1]);
            if (0.0 <= dd && dd <= 1e-10) { tol *= 0.5; free(T); free(O); continue; }
        }
        out->n = no; out->p = ta(sizeof(double[2]) * no); memcpy(out->p, O, sizeof(double[2]) * no); out->u = ta(sizeof(double) * no); memcpy(out->u, T, sizeof(double) * no); out->hasbox = 0;
        free(T); free(O); return 0;
    }
}
/* MaParameterCurve::FacetPCurve 0x180464c60 for a cubic spline uv curve: BinaryUVPointsAtTol(tol); failure: GenUVPolyLine 0x1804652f0 (the spline's nodes) */
void facet_pc(ICv *q) {
    ICv *c = q->inner;
    if (binary_uv(q->s, c, q->tol, &q->poly) == 0) return;
    q->poly.n = c->nseg + 1; q->poly.p = ta(sizeof(double[2]) * (c->nseg + 1)); q->poly.u = ta(sizeof(double) * (c->nseg + 1));
    for (int i = 0; i <= c->nseg; i++) { double o[9]; cv_eval(c, 0, c->U[i], o); q->poly.p[i][0] = o[0]; q->poly.p[i][1] = o[1]; q->poly.u[i] = c->U[i]; }
}
ICv *cubic_of(int nseg, double *C, double *U, int metric) {
    ICv *c = cv_new(0, metric); c->nseg = nseg; c->C = ta(sizeof(double) * 12 * nseg); memcpy(c->C, C, sizeof(double) * 12 * nseg); c->U = ta(sizeof(double) * (nseg + 1)); memcpy(c->U, U, sizeof(double) * (nseg + 1));
    c->tmin = c->U[0]; c->tmax = c->U[nseg]; c->t0 = c->tmin; c->t1 = c->tmax; c->dt = c->t1 - c->t0; return c;
}
ICv *pc_wrap(ISrf *s, ICv *c, double tol) {   /* MaParameterCurve(surface, curve, tol) 0x180464500: FacetPCurve, SetMinMaxParams(the curve) */
    ICv *q = cv_new(4, s->metric); q->inner = c; q->s = s; q->tol = tol; q->tmin = c->tmin; q->tmax = c->tmax; q->t0 = c->t0; q->t1 = c->t1; q->dt = c->dt;
    facet_pc(q); return q;
}
/* MaParameterCurve(surface, polyline, tol, tol2) 0x180464620: GenPCurve 0x180465230 = MaCubicSpline(polyline) 0x1803f0490 (BuildPolyPtsSpline) + FacetPCurve with tol2 */
ICv *pc_poly_cubic(ISrf *s, const double (*p)[2], int n, double tol2) {
    double (*P)[3] = malloc(sizeof(double[3]) * n); for (int i = 0; i < n; i++) { P[i][0] = p[i][0]; P[i][1] = p[i][1]; P[i][2] = 0; }
    double *C, *U; int ns = mcr_poly_pts_spline(n, (const double (*)[3])P, &C, &U); free(P);
    ICv *c = cubic_of(ns, C, U, s->metric); free(C); free(U);
    return pc_wrap(s, c, tol2);
}
/* MaCubicCurve::MakeLinear 0x1803ee810 (P0 P1, D0 = D1 = P1 - P0, ConvertToAlgebraic) + MaCubicSpline(const MaCubicCurve *) 0x1803f03d0 (one segment, u 0 .. 1) */
ICv *cubic_line(const double *a, const double *b, int metric) {
    double C[12], U[2] = { 0, 1 };
    for (int k = 0; k < 3; k++) { double p0 = a[k], p1 = b[k], d0 = b[k] - a[k], d1 = d0; C[4 * k] = (p0 - p1) * 2.0 + d0 + d1; C[4 * k + 1] = ((p1 - p0) * 3.0 - d0 * 2.0) - d1; C[4 * k + 2] = d0; C[4 * k + 3] = p0; }
    return cubic_of(1, C, U, metric);
}

ISrf *loop_srf(const ILoop *L) { return L->n > 0 && L->pc[0] ? L->pc[0]->s : NULL; }   /* MaLoop::Surface 0x180434f70 */
int pc_valid(const ICv *q) { return q && q->kind == 4 && q->poly.n > 0 && q->inner && q->s; }   /* MaParameterCurve::IsValid 0x180466d10 */
int loop_valid(const ILoop *L) {   /* MaLoop::IsValid 0x180433160 */
    if (!L || !loop_srf(L) || L->n < 1) return 0;
    for (int i = 0; i < L->n; i++) if (!pc_valid(L->pc[i])) return 0;
    return 1;
}
/* MaParameterCurve::FacetPCurve 0x180464c60 after a change of the uv curve: a degree 1 NURBS (IsPiecewiseLinear 0x180466cd0) gives its vertices with
   the unique knots (GenUVPolyLineFromNURB 0x180465480), a cubic spline BinaryUVPointsAtTol */
void refacet(ICv *q) {
    ICv *c = q->inner;
    if (c->kind == 1 && c->deg == 1) {
        int n = c->ncp; q->poly.n = n; q->poly.p = ta(sizeof(double[2]) * n); q->poly.u = ta(sizeof(double) * n); q->poly.hasbox = 0;
        for (int i = 0; i < n; i++) { q->poly.p[i][0] = c->P[3 * i]; q->poly.p[i][1] = c->P[3 * i + 1]; }
        int m = 0; for (int i = c->deg; i <= c->nk - c->deg - 1 && m < n; i++) { if (i > c->deg && c->K[i] == c->K[i - 1]) continue; q->poly.u[m++] = c->K[i]; }
    } else if (c->kind == 0) facet_pc(q);
    else refuse("MaParameterCurve::FacetPCurve of this curve kind");
}
/* MaParameterCurve::Break 0x180464a30 (the second part is not kept): the uv curve's Break (MaCubicSpline::Break 0x1803f16a0 / MaNURBSpline::Break),
   SetMinMaxParams(TMin, TMax), FacetPCurve */
int pc_break(ICv *q, double t, int keep_first) {
    if (t == q->tmin || t == q->tmax) return 1;
    ICv *c = q->inner; int r;
    if (c->kind == 0) {
        int ns = c->nseg; double *C = malloc(sizeof(double) * 12 * ns), *U = malloc(sizeof(double) * (ns + 1)); memcpy(C, c->C, sizeof(double) * 12 * ns); memcpy(U, c->U, sizeof(double) * (ns + 1));
        double *C0 = C, *U0 = U; r = mcr_cubic_break(&ns, &C, &U, t, keep_first);
        if (r == 0) { c->nseg = ns; c->C = ta(sizeof(double) * 12 * ns); memcpy(c->C, C, sizeof(double) * 12 * ns); c->U = ta(sizeof(double) * (ns + 1)); memcpy(c->U, U, sizeof(double) * (ns + 1)); c->tmin = c->U[0]; c->tmax = c->U[ns]; free(C); free(U); }
        free(C0); free(U0);
    } else if (c->kind == 1) {
        int ncp = c->ncp, ord = c->deg + 1; double *K = malloc(sizeof(double) * c->nk), *P = malloc(sizeof(double) * 4 * ncp); memcpy(K, c->K, sizeof(double) * c->nk);
        for (int i = 0; i < ncp; i++) { memcpy(P + 4 * i, c->P + 3 * i, 24); P[4 * i + 3] = c->W ? c->W[i] : 1.0; }
        double *K0 = K, *P0 = P; r = mcr_nurbs_break(&ncp, ord, &K, &P, t, keep_first);
        if (r == 0) { c->ncp = ncp; c->nk = ncp + ord; c->K = ta(sizeof(double) * c->nk); memcpy(c->K, K, sizeof(double) * c->nk); c->P = ta(sizeof(double) * 3 * ncp); c->W = ta(sizeof(double) * ncp);
            for (int i = 0; i < ncp; i++) { memcpy(c->P + 3 * i, P + 4 * i, 24); c->W[i] = P[4 * i + 3]; }
            c->tmin = c->K[0]; c->tmax = c->K[c->nk - 1]; free(K); free(P); }
        free(K0); free(P0);
    } else { refuse("MaParameterCurve::Break of this curve kind"); return 1; }
    if (r != 0) return r;
    cv_dirty(c); c->ofs = 0; c->t0 = c->tmin; c->t1 = c->tmax; c->dt = c->t1 - c->t0;
    cv_dirty(q); q->tmin = c->tmin; q->tmax = c->tmax; q->t0 = q->tmin; q->t1 = q->tmax; q->dt = q->t1 - q->t0;
    refacet(q);
    return 0;
}
/* MaCurve::Truncate() 0x180409950 -> MaCurve::Truncate(t0, t1) 0x180409990 on a MaParameterCurve: the trimmed range made the whole curve by Breaks */
void pc_truncate(ICv *q) {
    if (q->dt == q->tmax - q->tmin) return;   /* IsTrimmed 0x180405200 */
    double a = q->t0, b = q->t1;
    if (a - q->tmin <= 1e-12) a = q->tmin;
    if (q->tmax - b <= 1e-12) b = q->tmax;
    if (a < q->tmin) a = q->tmin;
    if (q->tmax < b) b = q->tmax;
    if (a == b) return;
    if (b < a) { if (!cv_periodic(q)) { double t = a; a = b; b = t; } else { refuse("MaCurve::Truncate across the seam of a periodic pcurve"); return; } }
    if (a == q->tmin && b == q->tmax && q->dt == q->tmax - q->tmin) return;
    if (a == q->tmin) { if (pc_break(q, b, 1) != 0) return; }
    else if (b == q->tmax) { if (pc_break(q, a, 0) != 0) return; }
    else {
        double tmax = q->tmax;
        pc_break(q, a, 0);
        if (pc_break(q, (b - a) / (tmax - a), 1) != 0) return;
    }
    cv_dirty(q); q->t0 = q->tmin; q->t1 = q->tmax; q->dt = q->t1 - q->t0;
}
/* MaLoop::MoveEndPoint 0x180433520 / MoveStartPoint 0x1804335a0: a trimmed end pcurve truncated first (MaCurve::Truncate), then
   MaParameterCurve::MoveEndPoint 0x1804670e0 (only when not trimmed: the uv curve's end and the polyline's), the loop polyline's end */
void loop_move_tr(ILoop *L, const double *v, int end) {
    ICv *pc = L->pc[end ? L->n - 1 : 0];
    if (pc->dt != pc->tmax - pc->tmin) pc_truncate(pc);
    if (pc->dt != pc->tmax - pc->tmin) { if (L->lp) { int j = end ? L->lp->n - 1 : 0; L->lp->p[j][0] = v[0]; L->lp->p[j][1] = v[1]; } return; }
    loop_move(L, v, end);
}
ILoop *loop_new(ICv **pc, int n) {   /* MaLoop(MaParameterCurve **, int) 0x18042e570: CheckForClosure when valid */
    ILoop *L = loop_of(pc, n);
    if (loop_valid(L)) {   /* MaLoop::CheckForClosure 0x180430210: an end within (0, 0.001) of the start, nearer than the last pcurve's middle, moved onto it */
        double S[9], E[9], M[9]; ICv *a = L->pc[0], *b = L->pc[n - 1];
        cv_eval(a, 0, a->t0, S); cv_eval(b, 0, b->t1, E); cv_eval(b, 0, (b->t1 + b->t0) * 0.5, M);   /* vtable +0x100 TMid (MaCurve::TMid 0x18000f460) */
        double g = dist3(S, E);
        if (0.0 < g && g < 0.001 && g < dist3(S, M)) { double p[3] = { S[0], S[1], S[2] }; loop_move_tr(L, p, 1); }
#ifdef MI_TRACE
        if (mi_ztr()) mi_zl("ZC", L);
#endif
    }
    return L;
}
/* MaLoop::SnapEndPoints (0x180434ad0); `s` = Surface(this) */
#ifdef MI_TRACE
static void loop_snap_raw(ILoop *L, ILoop *o, ISrf *s);
void loop_snap(ILoop *L, ILoop *o, ISrf *s) { loop_snap_raw(L, o, s); if (mi_ztr()) mi_zl("ZS", L); }
#define Z_LOOP_SNAP loop_snap_raw
static
#else
#define Z_LOOP_SNAP loop_snap
#endif
void Z_LOOP_SNAP(ILoop *L, ILoop *o, ISrf *s) {
    int moved = 0;
    for (int e = 0; e < 2; e++) {
        double uv[3]; if (e) loop_end(L, uv); else loop_start(L, uv);
        int r = uv_rel(o, uv);
        if (r != 0 && r != 2) continue;
        double d, f[6], t; int k; loop_pmd(o, uv, &d, &k, f, &t);
        double Es[21], Eo[21]; is_eval(s, 0, uv[0], uv[1], Es); is_eval(o->pc[0]->s, 0, f[0], f[1], Eo);
        double g = dist3(Es, Eo); ISrf *So = o->pc[0]->s; int bu = u_beyond(So, f[0]), bv = v_beyond(So, f[1]);   /* IsU / IsVBeyondRange 0x180486a80 */
        if ((0.0 < d && d <= 1e-05) || (0.0 < g && g <= g_chord_dev) || bu || bv) { double p[3] = { f[0], f[1], 0 }; loop_move_tr(L, p, e); moved = 1; }   /* MaLoop::MoveStart / EndPoint 0x1804335a0 / 0x180433520: a trimmed end pcurve truncated first */
    }
    if (moved) { L->lp = NULL; L->hasbox = 0; }   /* the loop polyline (+0x18) deleted, RegenMinMaxBox */
}
/* the loop of one pcurve (MaLoop(pcurve) 0x18042e4a0) */
/* MaSurfSurfIntersect::CheckUVEndPointsClose (0x1804a3310): the march's first and last (u, v) made equal for a closed march */
void uv_ends_close(ISrf *piece, double **UV, int n) {
    if (n <= 2) return;
    double E0[21], E1[21], Em[21]; is_eval(piece, 0, UV[0][0], UV[0][1], E0); is_eval(piece, 0, UV[n - 1][0], UV[n - 1][1], E1); is_eval(piece, 0, UV[n / 2][0], UV[n / 2][1], Em);
    double d1 = dist3(E0, E1), d2 = dist3(E0, Em), du = sqrt((UV[n - 1][0] - UV[0][0]) * (UV[n - 1][0] - UV[0][0]) + (UV[n - 1][1] - UV[0][1]) * (UV[n - 1][1] - UV[0][1]) + 0.0), tol = piece->metric ? 0.0025 : 0.0001;
    if (d1 < tol && d1 < d2 && du < 0.0001) { UV[n - 1][0] = UV[0][0]; UV[n - 1][1] = UV[0][1]; }
}
/* MaSurfSurfIntersect::BuildTrimmedModelSpacePartCurves (0x1804a2bd0) on one march; *nc curves appended to *res (the copies of the whole NURBS with t0, t1) */
void part_curves(ISrf *piece, MiFace *F, MaInt *m, const MiCurve *whole, MiCurve **res_, int *ncurves_, int *cap_) {
    uv_ends_close(piece, m->UV, m->n);   /* MaSurfPlaneIntersect::BuildTrimmedModelSpaceIntersectionCurves calls it first */
    ICv *pc = pc_of_march(F->s, m->P, m->T, m->UV, m->n, g_chord_dev);
    ILoop *L = loop_new(&pc, 1);   /* MaLoop(MaParameterCurve *) 0x18042e5c0 (BuildTrimmedModelSpacePartCurves +0x15a): CheckForClosure */
    loop_snap(L, F->outer, F->s);
    ILoop *cp = loop_copy(L); ILoop **res = NULL; int nres = 0;
    int r = trim_to_face(cp, F, &nres, &res);
    if (r != 0) { if (r == 2) return; res = ta(sizeof(ILoop *)); res[0] = loop_copy(L); nres = 1; }
    MiCurve *out = *res_; int nc = *ncurves_, cap = *cap_;
    for (int l = 0; l < nres; l++) for (int i = 0; i < res[l]->n; i++) {
        ICv *q = res[l]->pc[i]; double a[9], b[9], E1[21], E2[21], d, foot[6], t0 = 0.0, t1 = 0.0;
        cv_eval(q, 0, q->t0, a); cv_eval(q, 0, q->t1, b);
        is_eval(F->s, 0, a[0], a[1], E1); is_eval(F->s, 0, b[0], b[1], E2);
        ICv *c = cv_new(2, F->s->metric); c->deg = whole->deg; c->ncp = whole->ncp; c->nk = whole->nk; c->K = whole->k; c->P = whole->p; c->tmin = whole->k[0]; c->tmax = whole->k[whole->nk - 1]; c->t0 = c->tmin; c->t1 = c->tmax; c->dt = c->t1 - c->t0;   /* MaNURBSpline::TMinAllowable 0x180455070: the first knot */
        cv_pmd(c, 1, E1, &d, foot, &t0); cv_pmd(c, 1, E2, &d, foot, &t1);
        if (cv_closed(c) && c->dt == (c->tmax - c->tmin)) {   /* vtable +0x18 IsClosed(-1), !IsTrimmed */
            if (fabs(t0 - c->t1) <= 1e-06 && fabs(t0 - c->t1) != 1e-06) t0 = c->t0;
            if (fabs(t1 - c->t0) <= 1e-06 && fabs(t1 - c->t0) != 1e-06) t1 = c->t1;
            if (fabs(t1 - t0) <= 1e-06 && fabs(t1 - t0) != 1e-06) { t0 = c->t0; t1 = c->t1; }
        }
        cv_trim(c, t0, t1);
        if (nc == cap) { cap *= 2; out = realloc(out, sizeof(MiCurve) * cap); }
        MiCurve *o = &out[nc++]; *o = *whole; o->k = malloc(sizeof(double) * whole->nk); memcpy(o->k, whole->k, sizeof(double) * whole->nk); o->p = malloc(sizeof(double) * 3 * whole->ncp); memcpy(o->p, whole->p, sizeof(double) * 3 * whole->ncp); o->t0 = c->t0; o->t1 = c->t1;
    }
    *res_ = out; *ncurves_ = nc; *cap_ = cap;
}
/* MaFace construction: MaLoop(surface, polyline, chord, angle) 0x18042eb50: RefineByChordHeightToSurface, BreakAtAngleAndSrfBoundary, ConcatenateAllSmoothlyJoinedPCurves, CheckForClosure; these repeat mcsrfcrv.c's fe_refine / fe_break / fe_concat / fe_loop on the ISrf (the ported and checked face loops of Curve All Edges) */
void flo_refine(ISrf *s, FPoly *L, double tol) {
    int cap = L->n * 2 > 5000 ? L->n * 2 : 5000, n = L->n; double (*Q)[2] = malloc(sizeof(double[2]) * cap); memcpy(Q, L->p, sizeof(double[2]) * L->n);
    static const double F[5] = { 0.25, 0.3333333333333333, 0.5, 0.6666666666666666, 0.75 };
    for (int i = 0; i < n - 1; ) {
        double d[2] = { Q[i + 1][0] - Q[i][0], Q[i + 1][1] - Q[i][1] }, Pa[21], Pb[21], t[3]; is_eval(s, 0, Q[i][0], Q[i][1], Pa); is_eval(s, 0, Q[i + 1][0], Q[i + 1][1], Pb);
        for (int k = 0; k < 3; k++) t[k] = Pb[k] - Pa[k]; unit(t); double tl = len(t); int bad = 0;
        for (int k = 0; k < 5 && !bad; k++) { double Pq[21], w[3], c[3], dist; is_eval(s, 0, Q[i][0] + F[k] * d[0], Q[i][1] + F[k] * d[1], Pq); for (int m = 0; m < 3; m++) w[m] = Pq[m] - Pa[m];
            if (tl < 1e-05) dist = len(w); else { c[0] = w[1] * t[2] - w[2] * t[1]; c[1] = w[2] * t[0] - w[0] * t[2]; c[2] = w[0] * t[1] - w[1] * t[0]; dist = len(c); } if (tol < dist) bad = 1; }
        if (!bad) { i++; continue; }
        if (n >= cap) break;
        memmove(Q + i + 2, Q + i + 1, sizeof(double[2]) * (n - i - 1)); Q[i + 1][0] = Q[i][0] + d[0] * 0.5; Q[i + 1][1] = Q[i][1] + d[1] * 0.5; n++;
    }
    free(L->p); L->p = Q; L->n = n;
}
void flo_add(FPoly **o, int *no, const double (*p)[2], int n) { *o = realloc(*o, sizeof(FPoly) * (*no + 1)); FPoly *x = &(*o)[(*no)++]; x->n = n; x->p = malloc(sizeof(double[2]) * n); memcpy(x->p, p, sizeof(double[2]) * n); }
int flo_corner(ISrf *s, double u, double v) { return (u == s->d[0] || u == s->d[1]) && (v == s->d[2] || v == s->d[3]); }   /* MaSurface::IsUVCorner 0x180486bf0 */
int flo_linear(const FPoly *L) {   /* MaPolyLine::IsLinear 0x1804705f0 */
    for (int i = 2; i < L->n; i++) { double a[2] = { L->p[i - 1][0] - L->p[i - 2][0], L->p[i - 1][1] - L->p[i - 2][1] }, b[2] = { L->p[i][0] - L->p[i - 1][0], L->p[i][1] - L->p[i - 1][1] }, la = hypot(a[0], a[1]), lb = hypot(b[0], b[1]); if (la != 0) { a[0] /= la; a[1] /= la; } else a[0] = a[1] = 0; if (lb != 0) { b[0] /= lb; b[1] /= lb; } else b[0] = b[1] = 0; if (1e-06 < fabs(b[1] * a[0] - a[1] * b[0])) return 0; }
    return 1;
}
int flo_join(ISrf *s, const FPoly *a, const FPoly *b, double ang) {   /* MaPolyLine::JoinsSmoothly 0x180470990 */
    double p0[21], p1[21], q0[21], q1[21], u[3], v[3], g[3];
    is_eval(s, 0, a->p[a->n - 1][0], a->p[a->n - 1][1], p1); is_eval(s, 0, a->p[a->n - 2][0], a->p[a->n - 2][1], p0); is_eval(s, 0, b->p[1][0], b->p[1][1], q1); is_eval(s, 0, b->p[0][0], b->p[0][1], q0);
    for (int k = 0; k < 3; k++) { u[k] = p1[k] - p0[k]; v[k] = q1[k] - q0[k]; g[k] = q0[k] - p1[k]; } unit(u); unit(v);
    return len(g) <= 0.0001 && cos(ang * 0.017453292519943295) < dot(u, v);
}
void flo_break(ISrf *s, const FPoly *L, double ang, FPoly **o, int *no) {   /* MaPolyLine::BreakAtAngleAndSrfBoundary 0x18046d960 */
    double cs = cos(ang * 0.017453292519943295); int n = L->n, st = 0, i = 1; const double (*P)[2] = (const double (*)[2])L->p;
    for (;;) {
        double A[21], B[21], C[21], d1[3], d2[3]; is_eval(s, 0, P[i - 1][0], P[i - 1][1], A); is_eval(s, 0, P[i][0], P[i][1], B); for (int k = 0; k < 3; k++) d1[k] = B[k] - A[k]; unit(d1);
        int on1 = uv_on_edge(s, (P[i - 1][0] + P[i][0]) * 0.5, (P[i - 1][1] + P[i][1]) * 0.5), crn = flo_corner(s, P[i][0], P[i][1]);
        if (n - 1 <= i) {
            int cnt = i - st + 1; double dd = hypot(P[st][0] - P[st + 1][0], P[st][1] - P[st + 1][1]);
            if (cnt != 2 || ((s->d[1] - s->d[0]) * 1e-06 <= dd && (s->d[3] - s->d[2]) * 1e-06 <= dd)) flo_add(o, no, P + st, cnt);
            if (*no >= 2) { FPoly *f = &(*o)[0], *l = &(*o)[*no - 1];
                if (flo_join(s, l, f, 20.0) && !flo_linear(l) && !flo_linear(f)) { int m = l->n + f->n - 1; l->p = realloc(l->p, sizeof(double[2]) * m); memcpy(l->p + l->n, f->p + 1, sizeof(double[2]) * (f->n - 1)); l->n = m; free(f->p); memmove(f, f + 1, sizeof(FPoly) * (*no - 1)); (*no)--; } }
            return;
        }
        is_eval(s, 0, P[i + 1][0], P[i + 1][1], C); for (int k = 0; k < 3; k++) d2[k] = C[k] - B[k]; unit(d2);
        int on2 = uv_on_edge(s, (P[i][0] + P[i + 1][0]) * 0.5, (P[i][1] + P[i + 1][1]) * 0.5);
        if (dot(d1, d2) < cs || on1 != on2 || crn) { flo_add(o, no, P + st, i - st + 1); st = i; }
        i++;
    }
}
void flo_dir(ISrf *s, const double *uv, const double *d, double *T) { double E[21]; is_eval(s, 1, uv[0], uv[1], E); for (int k = 0; k < 3; k++) T[k] = E[6 + k] * d[0] + E[9 + k] * d[1]; unit(T); }
int flo_pjoin(ISrf *s, const FPoly *a, const FPoly *b) {   /* MaParameterCurve::JoinsSmoothly 0x180466d60, 5 degrees */
    const double *p1 = a->p[a->n - 1], *p0 = a->p[a->n - 2], *q0 = b->p[0], *q1 = b->p[1]; double da[2] = { p1[0] - p0[0], p1[1] - p0[1] }, db[2] = { q1[0] - q0[0], q1[1] - q0[1] }, Pa[21], Pb[21], g[3], Ta[3], Tb[3];
    is_eval(s, 0, p1[0], p1[1], Pa); is_eval(s, 0, q0[0], q0[1], Pb); for (int k = 0; k < 3; k++) g[k] = Pb[k] - Pa[k];
    if (len(g) > 0.0001 || flo_corner(s, p1[0], p1[1]) || flo_corner(s, q0[0], q0[1])) return 0;
    flo_dir(s, p1, da, Ta); flo_dir(s, q0, db, Tb); return cos(5.0 * 0.017453292519943295) < dot(Ta, Tb);
}
void flo_concat(ISrf *s, FPoly **o, int *no) {   /* MaLoop::ConcatenateAllSmoothlyJoinedPCurves 0x1804308c0 */
    if (*no < 2) return; FPoly *P = *o, *R = malloc(sizeof(FPoly) * *no); int n = *no, nr = 0; FPoly cur = P[0];
    for (int i = 1; i < n; i++) { if (flo_pjoin(s, &cur, &P[i])) { int m = cur.n + P[i].n - 1; cur.p = realloc(cur.p, sizeof(double[2]) * m); memcpy(cur.p + cur.n, P[i].p + 1, sizeof(double[2]) * (P[i].n - 1)); cur.n = m; free(P[i].p); } else { R[nr++] = cur; cur = P[i]; } }
    R[nr++] = cur; free(P); *o = R; *no = nr;
}
/* one stored loop (the points with the first repeated): the pieces; MaLoop::CheckForClosure 0x180430210 at the end */
int flo_loop(ISrf *s, double (*pts)[2], int n, double chord, double ang, FPoly **o) {
    FPoly L = { n, pts }; *o = NULL; int no = 0;
    flo_refine(s, &L, chord); flo_break(s, &L, ang, o, &no); flo_concat(s, o, &no); free(L.p);
    if (no > 0) { FPoly *f0 = &(*o)[0], *l0 = &(*o)[no - 1]; double *e1 = l0->p[l0->n - 1], gap = hypot(e1[0] - f0->p[0][0], e1[1] - f0->p[0][1]), tot = 0, h = 0, mid[2]; for (int i = 0; i + 1 < l0->n; i++) tot += hypot(l0->p[i + 1][0] - l0->p[i][0], l0->p[i + 1][1] - l0->p[i][1]);
        memcpy(mid, l0->p[0], sizeof mid); for (int i = 0; i + 1 < l0->n; i++) { double sl = hypot(l0->p[i + 1][0] - l0->p[i][0], l0->p[i + 1][1] - l0->p[i][1]); if (h + sl >= tot * 0.5) { double fr = sl > 0 ? (tot * 0.5 - h) / sl : 0; mid[0] = l0->p[i][0] + fr * (l0->p[i + 1][0] - l0->p[i][0]); mid[1] = l0->p[i][1] + fr * (l0->p[i + 1][1] - l0->p[i][1]); break; } h += sl; }
        if (0.0 < gap && gap < 0.001 && gap < hypot(mid[0] - f0->p[0][0], mid[1] - f0->p[0][1])) { e1[0] = f0->p[0][0]; e1[1] = f0->p[0][1]; } }
    return no;
}
/* MaFace::UVMinMax 0x18041aaa0 (over the outer loop's polyline; a degenerate range uses the surface's) */
void face_uvminmax(MiFace *f) {
    IPoly *p = loop_poly(f->outer); double a = 1e31, b = -1e31, c = 1e31, d = -1e31;
    for (int i = 0; i < p->n; i++) { if (p->p[i][0] < a) a = p->p[i][0]; if (b < p->p[i][0]) b = p->p[i][0]; if (p->p[i][1] < c) c = p->p[i][1]; if (d < p->p[i][1]) d = p->p[i][1]; }
    if (fabs(b - a) < 1e-12) { a = f->s->d[0]; b = f->s->d[1]; }
    if (fabs(d - c) < 1e-12) { c = f->s->d[2]; d = f->s->d[3]; }
    f->uvbox[0] = a; f->uvbox[1] = b; f->uvbox[2] = c; f->uvbox[3] = d;
}
/* MaPolyLine::Area 0x18046d8c0, WindingNumber 0x180475d10 */
double pl_area(const IPoly *l) { double a = 0; for (int i = 0; i < l->n - 1; i++) a += l->p[i + 1][1] * l->p[i][0] - l->p[i][1] * l->p[i + 1][0]; return fabs(a * 0.5); }
double pl_winding(const IPoly *l) {
    double w = 0, vx, vy; int n = l->n; if (n < 4) return 0; int i8 = 0;
    do {
        int i9 = i8 + 1; vy = l->p[i9][1] - l->p[i8][1]; vx = l->p[i9][0] - l->p[i8][0]; double dl = sqrt(vy * vy + vx * vx);
        if (1e-12 < dl) {
            int i11 = i8 + 1; vx /= dl; vy /= dl; int i9b = n - 1; if (i9b <= i8) return w;
            int i10 = i11;
            for (;;) {
                const double *a = l->p[i10]; int i6 = i10 + 1; if (i9b <= i10) i6 = i11; const double *b = l->p[i6];
                double d3 = b[0] - a[0], d2 = b[1] - a[1], d1 = sqrt(d2 * d2 + d3 * d3);
                if (1e-12 <= d1) {
                    double ax = 0, ay = 0; if (d1 != 0.0) { ax = d3 / d1; ay = d2 / d1; }
                    double cr = ay * vx - vy * ax, dt = ay * vy + ax * vx, dc = 1.0; if (dt <= 1.0) { dc = dt; if (dt < -1.0) dc = -1.0; }
                    double ang = acos(dc);
                    if (0.0 <= cr) { if (0.0 < cr) w += ang; } else w -= ang;
                    vx = ax; vy = ay;
                }
                i10++; if (i10 == n) break;
                i9b = n - 1; i8++; if (i9b <= i8) return w;
            }
            return w;
        }
        i8 = i9;
    } while (i8 != n);
    return w;
}
/* MaFace::CheckOuterLoopValid 0x180414eb0, CheckInnerLoopsValid 0x180414d10 */
void face_check(MiFace *f) {
    if (f->ninner == 0) return;
    IPoly *o = loop_poly(f->outer); double a0 = 1e31, a1 = -1e31, b0 = 1e31, b1 = -1e31;
    for (int i = 0; i < o->n; i++) { if (o->p[i][0] < a0) a0 = o->p[i][0]; if (a1 < o->p[i][0]) a1 = o->p[i][0]; if (o->p[i][1] < b0) b0 = o->p[i][1]; if (b1 < o->p[i][1]) b1 = o->p[i][1]; }
    int sw = -1;
    for (int k = 0; k < f->ninner; k++) { IPoly *q = loop_poly(f->inner[k]); double c0 = 1e31, c1 = -1e31, d0 = 1e31, d1 = -1e31;
        for (int i = 0; i < q->n; i++) { if (q->p[i][0] < c0) c0 = q->p[i][0]; if (c1 < q->p[i][0]) c1 = q->p[i][0]; if (q->p[i][1] < d0) d0 = q->p[i][1]; if (d1 < q->p[i][1]) d1 = q->p[i][1]; }
        if (c0 < a0 && a1 < c1 && d0 < b0 && b1 < d1) sw = k; }
    if (sw >= 0) { ILoop *t = f->outer; f->outer = f->inner[sw]; f->inner[sw] = t; }
    int keep = 0;
    for (int k = 0; k < f->ninner; k++) { IPoly *q = loop_poly(f->inner[k]); double area = pl_area(q), w = pl_winding(q); if (1e-06 < area && (1e-06 < fabs(w) || fabs(w) == 1e-06)) f->inner[keep++] = f->inner[k]; }
    f->ninner = keep;
}
MiFace *mi_face_build(ISrf *s, int nl, const int *np, double (*const *lp)[2], double chord, double ang, double chain_tol) {
    g_chord_dev = chord; g_chain_tol = chain_tol;
    MiFace *f = ta(sizeof(MiFace)); f->s = s; f->inner = ta(sizeof(ILoop *) * (nl > 1 ? nl - 1 : 1));
    EDGES(s);
    for (int l = 0; l < nl; l++) {
        if (l > 0 && np[l] == 2) continue;   /* MaFace::MaFace 0x180413ba0: an inner loop of one point is dropped */
        double (*q)[2] = malloc(sizeof(double[2]) * np[l]); memcpy(q, lp[l], sizeof(double[2]) * np[l]); FPoly *o = NULL; int no = flo_loop(s, q, np[l], chord, ang, &o);
        ILoop *L = ta(sizeof(ILoop)); L->n = no; L->pc = ta(sizeof(ICv *) * (no ? no : 1));
        for (int i = 0; i < no; i++) { L->pc[i] = pc_of_poly(s, (const double (*)[2])o[i].p, o[i].n, chord); free(o[i].p); }
        free(o);
        if (l == 0) f->outer = L; else f->inner[f->ninner++] = L;
    }
    if (!f->outer || f->outer->n < 1) { ta_free(); return NULL; }
    face_check(f); face_uvminmax(f);
    if (1000.0 <= s->d[4] / (f->uvbox[1] - f->uvbox[0]) || 1000.0 <= s->d[5] / (f->uvbox[3] - f->uvbox[2])) f->reparam = 1;   /* MaFace::CheckFaceToSurfaceDeltaUVRatios 0x1804148a0: the surface is truncated and the loops reparameterised (NOT PORTED) */
    return f;
}
/* MaSurface::SubsetToFace (0x1804895e0) */
void mi_face_subset(MiFace *f, double fr) {
    ISrf *s = f->s; double du = (f->uvbox[1] - f->uvbox[0]) * fr, dv = (f->uvbox[3] - f->uvbox[2]) * fr;
    double a = f->uvbox[0] - du, b = du + f->uvbox[1], c = f->uvbox[2] - dv, d = dv + f->uvbox[3];
    if (a < s->d[0]) a = s->d[0]; if (s->d[1] < b) b = s->d[1]; if (c < s->d[2]) c = s->d[2]; if (s->d[3] < d) d = s->d[3];
    s->d[0] = a; s->d[1] = b; s->d[2] = c; s->d[3] = d; s->d[4] = b - a; s->d[5] = d - c;   /* SetMinMaxParams 0x1804892e0 */
    s->edges = 0; free(s->gu); free(s->gv); free(s->gp); s->gu = s->gv = s->gp = NULL; s->ngu = s->ngv = 0;
}
void mi_face_free(MiFace *f) { (void)f; ta_free(); }
int mi_face_reparam(const MiFace *f) { return f->reparam; }
int mi_surf_plane_face(ISrf *s, MiFace *F, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **out) {
    ISrf *pieces; int npc = mi_pieces(s, &pieces), cap = 16, ncurves = 0; MiCurve *res = malloc(sizeof(MiCurve) * cap);
    g_chord_dev = chord;
    for (int i = 0; i < npc; i++) plane_one(&pieces[i], F, pl, chord, chord2, maxstep, minstep, multi, &res, &ncurves, &cap);
    mi_free_pieces(pieces, npc); *out = res; return ncurves;
}
/* ---- MaNURBSurf's DB constructor (0x180455dd0): the knots, the range and the knot regeneration (this[0x1c] / [0x1d]) ---- */
/* a MaNURBSpline for the regeneration: order, control points, knots (normalised by SetKnots), the points x y z w */
typedef struct { int ord, n, nk; double *K, *V; } NbC;
/* MaNURBSpline::SetKnots (0x180453740): (K - K0) * (1 / (Kn - K0)) */
static void nbc_set_knots(NbC *c, const double *K, int nk) {
    c->nk = nk; c->K = realloc(c->K, sizeof(double) * nk); memcpy(c->K, K, sizeof(double) * nk);
    double a = c->K[0], b = c->K[nk - 1];
    for (int i = 0; i < nk; i++) c->K[i] = (c->K[i] - a) * (1.0 / (b - a));
}
/* MaNURBSpline::FindKnotSpan (0x18044c1d0) without the snap (param_3 false): the span's first control point */
static int nbc_span(const NbC *c, double t) {
    int ord = c->ord, ip = ord, hi = c->nk - ord, iv; const double *K = c->K; double e = K[c->nk - 1];
    if (K[0] <= t && t <= e) {
        if (ip <= hi) { long l = ip; do { if (t <= K[l]) break; l++; ip++; } while (l <= hi); }
        iv = ip; if (ip == hi + 1) iv = ip - 1;
    } else { iv = ip; if (e < t) iv = hi; }
    iv -= ord; return iv < 0 ? 0 : iv;
}
/* MaNURBSpline::SubdivisionPoint (0x180454d30) -> SegmentSubdivisionPoint (0x180453000): de Boor's triangle on the span's points, each new point
   (P[i] a + P[i - 1] b) * (1 / (b + a)) with a = t - K[i], b = K[i + p + 1 - r] - t (K from the span's first control point) */
static void nbc_subdiv_point(const NbC *c, double t, double out[4]) {
    int sp = nbc_span(c, t), p = c->ord - 1; const double *K = c->K + sp; double Q[26][4];
    for (int i = 0; i <= p; i++) memcpy(Q[i], c->V + 4 * (size_t)(sp + i), 32);
    for (int r = 1; r <= p; r++) for (int i = p; i >= r; i--) {
        double a = t - K[i], b = K[i + p + 1 - r] - t, q = 1.0 / (b + a);
        for (int k = 0; k < 4; k++) Q[i][k] = (Q[i][k] * a + Q[i - 1][k] * b) * q;
    }
    memcpy(out, Q[p], 32);
}
/* MaNURBSpline::GetUniqueKnots (0x18044eaf0): K[deg], the distinct values of K[ord] .. K[nk - ord - 1], K[nk - ord] */
static int nb_unique(const double *K, int nk, int ord, double *U) {
    int n = 1; double v = K[ord - 1]; U[0] = v;
    for (int i = ord; i < nk - ord; i++) if (K[i] != v) { U[n++] = K[i]; v = K[i]; }
    U[n++] = K[nk - ord]; return n;
}
/* MaNURBSpline::RegenNonUniformKnotsFromDerivRatios (0x180452940): per interior unique knot the curve evaluated at the middles of the spans on
   both sides (MaCurve::Evaluate leaves the span's Bezier points, ConvertSegmentToBezier, at +0x158): the last leg before over the first leg
   after (each point divided by its weight; a leg below 1e-12 counts 1), the product cumulated, divided by the last; the knots rebuilt: order
   zeros, each interior unique knot's new value repeated by GetMultiplicityOfKnot (0x18044e780: the equal knots, at most the order), order ones */
static void nbc_regen(NbC *c) {
    int ord = c->ord, p = ord - 1, n; double *U = malloc(sizeof(double) * c->nk), *t = malloc(sizeof(double) * c->nk), B[26][4];
    n = nb_unique(c->K, c->nk, ord, U); t[0] = 0.0; t[1] = 1.0;
    if (2 < n) {
        double d = 1.0;
        for (int j = 1; j < n - 1; j++) {
            double tm = (U[j] - U[j - 1]) * 0.5 + U[j - 1], e[3], f[3], le, lr; int sp = nbc_span(c, tm);
            memcpy(B, c->V + 4 * (size_t)sp, sizeof(double) * 4 * ord); nbs_to_bezier(p, c->K + sp, &B[0][0]);
            for (int k = 0; k < 3; k++) { e[k] = B[p][k] / B[p][3]; f[k] = B[p - 1][k] / B[p - 1][3]; }
            le = sqrt((e[0] - f[0]) * (e[0] - f[0]) + (e[1] - f[1]) * (e[1] - f[1]) + (e[2] - f[2]) * (e[2] - f[2])); if (le < 1e-12) le = 1.0;
            tm = (U[j + 1] - U[j]) * 0.5 + U[j]; sp = nbc_span(c, tm);
            memcpy(B, c->V + 4 * (size_t)sp, sizeof(double) * 4 * ord); nbs_to_bezier(p, c->K + sp, &B[0][0]);
            for (int k = 0; k < 3; k++) { e[k] = B[1][k] / B[1][3]; f[k] = B[0][k] / B[0][3]; }
            lr = sqrt((e[0] - f[0]) * (e[0] - f[0]) + (e[1] - f[1]) * (e[1] - f[1]) + (e[2] - f[2]) * (e[2] - f[2])); if (lr < 1e-12) lr = 1.0;
            d = d * (lr / le); t[j + 1] = d + t[j];
        }
        for (int k = 1; k < n; k++) t[k] = t[k] / t[n - 1];
        t[0] = 0.0; t[n - 1] = 1.0;
    }
    double *K2 = malloc(sizeof(double) * c->nk); int m = 0;
    for (int i = 0; i < ord; i++) K2[m++] = 0.0;
    for (int j = 1; j < n - 1; j++) { int mu = 0; for (int i = 0; i < c->nk; i++) if (U[j] == c->K[i]) mu++; if (ord < mu) mu = ord; for (int q = 0; q < mu && m < c->nk; q++) K2[m++] = t[j]; }
    for (int i = c->nk - ord; i < c->nk; i++) K2[i] = 1.0;
    nbc_set_knots(c, K2, c->nk);
    free(K2); free(U); free(t);
}
/* MaNURBSurf::Flowline (0x18045b9c0): dir 2 = the curve of constant v (each u column's SubdivisionPoint at par, then a curve in u with the U knots),
   dir 1 = the curve of constant u */
static void nbs_flowline(const ISrf *s, int dir, double par, NbC *o) {
    int na = dir == 2 ? s->ncu : s->ncv, nb = dir == 2 ? s->ncv : s->ncu, oa = dir == 2 ? s->ku : s->kv, ob = dir == 2 ? s->kv : s->ku;
    NbC col; memset(&col, 0, sizeof col); col.ord = ob; col.n = nb; nbc_set_knots(&col, dir == 2 ? s->KV : s->KU, nb + ob); col.V = malloc(sizeof(double) * 4 * nb);
    memset(o, 0, sizeof *o); o->ord = oa; o->n = na; o->V = malloc(sizeof(double) * 4 * na);
    for (int i = 0; i < na; i++) {
        for (int j = 0; j < nb; j++) memcpy(col.V + 4 * j, s->V + 4 * (dir == 2 ? (size_t)j * s->ncu + i : (size_t)i * s->ncu + j), 32);
        nbc_subdiv_point(&col, par, o->V + 4 * i);
    }
    nbc_set_knots(o, dir == 2 ? s->KU : s->KV, na + oa);
    free(col.K); free(col.V);
}
/* MaNURBSurf::GetMultiplicityOfUKnot (0x18045c670): the knots equal to x, the scan stopping at the first larger one */
static int nbs_mult(const double *K, int nk, double x) { int m = 0; for (int i = 0; i < nk; i++) { if (x < K[i]) break; if (x == K[i]) m++; } return m; }
/* MaNURBSurf::RegenNonUniformUKnotsFromDerivRatios (0x18045ea10) / V (0x18045edd0): the curves of constant v at the unique V knots (Flowline 2),
   each regenerated; their unique knots summed and divided by the count; the U knots rebuilt: order zeros, each interior unique U knot's new value
   repeated by its multiplicity, order ones (SetUKnots) */
static void nbs_regen_dir(ISrf *s, int dir) {
    int nkA = dir == 0 ? s->ncu + s->ku : s->ncv + s->kv, nkB = dir == 0 ? s->ncv + s->kv : s->ncu + s->ku, oA = dir == 0 ? s->ku : s->kv, oB = dir == 0 ? s->kv : s->ku;
    double *KA = dir == 0 ? s->KU : s->KV, *KB = dir == 0 ? s->KV : s->KU;
    double *Ub = malloc(sizeof(double) * nkB), *sum = NULL, *tmp = malloc(sizeof(double) * nkA), *Ua = malloc(sizeof(double) * nkA); int nb = nb_unique(KB, nkB, oB, Ub), n = 0;
    for (int iv = 0; iv < nb; iv++) {
        NbC c; nbs_flowline(s, dir == 0 ? 2 : 1, Ub[iv], &c); nbc_regen(&c);
        if (!sum) { sum = calloc(c.nk, sizeof(double)); n = nb_unique(c.K, c.nk, c.ord, tmp); }
        nb_unique(c.K, c.nk, c.ord, tmp);
        for (int k = 0; k < n; k++) sum[k] = tmp[k] + sum[k];
        free(c.K); free(c.V);
    }
    for (int k = 0; k < n; k++) sum[k] = sum[k] / (double)nb;
    double *K2 = calloc(nkA, sizeof(double)); int m = oA, nu = nb_unique(KA, nkA, oA, Ua);
    for (int j = 1; j < nu - 1; j++) { int mu = nbs_mult(KA, nkA, Ua[j]); for (int q = 0; q < mu && m < nkA; q++) K2[m++] = sum[j]; }
    for (int i = nkA - oA; i < nkA; i++) K2[i] = 1.0;
    { double a = K2[0], b = K2[nkA - 1]; for (int i = 0; i < nkA; i++) KA[i] = (K2[i] - a) * (1.0 / (b - a)); }   /* SetUKnots / SetVKnots: as SetKnots */
    free(K2); free(Ub); free(sum); free(tmp); free(Ua);
}
/* MaNURBSurf::GetMaxKnotMultiplicity (0x18045c510): the longest run of equal knots among K[deg] .. K[nk - deg - 1] (the run reaching the end is not
   closed), 0 with at most 2 unique knots */
static int nbs_max_mult(const double *K, int nk, int ord) {
    double U[2]; (void)U; int nu = 1, deg = ord - 1; { double v = K[deg]; for (int i = ord; i < nk - ord; i++) if (K[i] != v) { nu++; v = K[i]; } nu++; }
    int mx = 0, run = 0; if (2 < nu) { double pv = K[deg]; for (int i = ord; i < nk - deg; i++) { double x = K[i]; if (x == pv) run++; else { if (mx < run + 1) mx = run + 1; run = 0; } pv = x; } }
    return mx;
}
/* MaNURBSurf::GlobalUVToLocalUV (0x18045cc40) / LocalUVToGlobalUV (0x18045e330) in one direction: the span of x in the unique knots U, the fraction
   (x - U[i-1]) / max(U[i] - U[i-1], 1e-12), then (V[i] - V[i-1]) * f + V[i-1] in the new unique knots */
static void g2l(const double *U, int n, double x, double *f, int *i0) { int i = 1; while (i < n && !(x <= U[i])) i++; if (i == n) i--; double d = U[i] - U[i - 1]; if (d < 1e-12) d = 1e-12; *f = (x - U[i - 1]) / d; *i0 = i - 1; }
static double l2g(const double *V, double f, int i0) { return (V[i0 + 1] - V[i0]) * f + V[i0]; }
void mi_regen_knots(ISrf *s, int rep, MiRegen *R) {
    memset(R, 0, sizeof *R);
    for (int d = 0; d < 2; d++) { int nk = d == 0 ? s->ncu + s->ku : s->ncv + s->kv; R->oldU[d] = malloc(sizeof(double) * nk); R->newU[d] = malloc(sizeof(double) * nk); R->nU[d] = nb_unique(d == 0 ? s->KU : s->KV, nk, d == 0 ? s->ku : s->kv, R->oldU[d]); memcpy(R->newU[d], R->oldU[d], sizeof(double) * R->nU[d]); }
    if (rep & 1) { nbs_regen_dir(s, 0); nb_unique(s->KU, s->ncu + s->ku, s->ku, R->newU[0]); }
    if (rep & 2) { nbs_regen_dir(s, 1); nb_unique(s->KV, s->ncv + s->kv, s->kv, R->newU[1]); }
}
/* MaNURBSurf's DB constructor (0x180455dd0, ma_classes.c:62056) on the record's raw knots (s->KU / KV) and range H (u0 u1 v0 v1): each knot vector
   (K - K0) * (1 / (Kn - K0)), the range likewise and clamped to the knots; the first interior knots within 1e-10 of K[deg] moved up 1e-6 and the last
   ones within 1e-10 of K[nk - ord] down 1e-6 (when the first / last span is empty); GetMaxKnotMultiplicity; this[0x1c] when a direction's
   multiplicity reaches its degree and (not rational or a degree below 3); with regen (LoadSurf's true) and [0x1c] each such direction regenerated
   (U first; the range through GlobalUVToLocalUV / LocalUVToGlobalUV when it is not the knots' whole domain), this[0x1d]. Returns the regenerated
   directions (bit 0 U, bit 1 V); R (may be NULL) the old / new unique knots for the loop points (MaPolyLine::ConvertUVParams). */
int mi_db_knots(ISrf *s, const double H[4], int regen, MiRegen *R) {
    int nku = s->ncu + s->ku, nkv = s->ncv + s->kv, du = s->ku - 1, dv = s->kv - 1, rep = 0; double rg[4]; int dif[2];
    for (int d = 0; d < 2; d++) {
        double *K = d == 0 ? s->KU : s->KV; int nk = d == 0 ? nku : nkv, ord = d == 0 ? s->ku : s->kv, deg = ord - 1;
        double a = K[0], inv = 1.0 / (K[nk - 1] - a);
        for (int i = 0; i < nk; i++) K[i] = (K[i] - a) * inv;
        double lo = (H[2 * d] - a) * inv, hi = (H[2 * d + 1] - a) * inv;
        if (lo < K[0]) lo = K[0]; if (K[nk - 1] < hi) hi = K[nk - 1];
        if (fabs(K[ord] - K[deg]) <= 1e-10 && ord < ord + deg) for (int i = ord; i < deg + ord; i++) if (fabs(K[i] - K[deg]) <= 1e-10) K[i] = K[i] + 1e-06;
        if (fabs(K[nk - ord] - K[nk - ord - 1]) <= 1e-10 && nk - 2 * ord < nk - ord - 1) for (int i = nk - ord - 1; nk - 2 * ord < i; i--) if (fabs(K[nk - ord] - K[i]) <= 1e-10) K[i] = K[i] - 1e-06;
        dif[d] = lo != K[deg] || hi != K[nk - ord];
        rg[2 * d] = lo; rg[2 * d + 1] = hi;
    }
    int mu = nbs_max_mult(s->KU, nku, s->ku), mv = nbs_max_mult(s->KV, nkv, s->kv), f1c = 0;
    if (du <= mu || dv <= mv) f1c = !s->rat || du < 3 || dv < 3;
    if (R) { memset(R, 0, sizeof *R); for (int d = 0; d < 2; d++) { int nk = d == 0 ? nku : nkv; R->oldU[d] = malloc(sizeof(double) * nk); R->newU[d] = malloc(sizeof(double) * nk); R->nU[d] = nb_unique(d == 0 ? s->KU : s->KV, nk, d == 0 ? s->ku : s->kv, R->oldU[d]); memcpy(R->newU[d], R->oldU[d], sizeof(double) * R->nU[d]); } }
    if (R) { memcpy(R->d0, s->d, sizeof R->d0); if (rg[0] < rg[1] && rg[2] < rg[3]) { R->d0[1] = rg[1]; R->d0[0] = rg[0]; R->d0[4] = rg[1] - rg[0]; R->d0[3] = rg[3]; R->d0[2] = rg[2]; R->d0[5] = rg[3] - rg[2]; } }   /* the range of LoadUntrimmedSurf(.., false) */
    if (regen && f1c) {
        for (int d = 0; d < 2; d++) {
            if (!((d == 0 ? du : dv) <= (d == 0 ? mu : mv))) continue;
            double *Uo = malloc(sizeof(double) * nku), *Vo = malloc(sizeof(double) * nkv), f[4]; int ix[4], nuo = nb_unique(s->KU, nku, s->ku, Uo), nvo = nb_unique(s->KV, nkv, s->kv, Vo);
            if (dif[d]) { g2l(Uo, nuo, rg[0], &f[0], &ix[0]); g2l(Vo, nvo, rg[2], &f[1], &ix[1]); g2l(Uo, nuo, rg[1], &f[2], &ix[2]); g2l(Vo, nvo, rg[3], &f[3], &ix[3]); }
            nbs_regen_dir(s, d);
            if (dif[d]) { double *Un = malloc(sizeof(double) * (d == 0 ? nku : nkv)); nb_unique(d == 0 ? s->KU : s->KV, d == 0 ? nku : nkv, d == 0 ? s->ku : s->kv, Un);
                rg[2 * d] = l2g(Un, f[d], ix[d]); rg[2 * d + 1] = l2g(Un, f[2 + d], ix[2 + d]); free(Un); }
            if (R) nb_unique(d == 0 ? s->KU : s->KV, d == 0 ? nku : nkv, d == 0 ? s->ku : s->kv, R->newU[d]);
            free(Uo); free(Vo); rep |= 1 << d;
        }
    }
    if (rg[0] < rg[1] && rg[2] < rg[3]) { s->d[1] = rg[1]; s->d[0] = rg[0]; s->d[4] = rg[1] - rg[0]; s->d[3] = rg[3]; s->d[2] = rg[2]; s->d[5] = rg[3] - rg[2]; }   /* SetMinMaxParams */
    s->edges = 0;
    return rep | (f1c ? 4 : 0);
}
/* MaNURBSurf::GlobalUVToLocalUV 0x18045cc40 + LocalUVToGlobalUV (MaPolyLine::ConvertUVParams): the span in the unique knots, the same fraction of the new span */
double mi_regen_map(const MiRegen *R, int dir, double x) {
    const double *U = R->oldU[dir], *V = R->newU[dir]; int n = R->nU[dir]; int i = 1; while (i < n && !(x <= U[i])) i++; if (i == n) i--; double d = U[i] - U[i - 1]; if (d < 1e-12) d = 1e-12; return V[i - 1] + (x - U[i - 1]) / d * (V[i] - V[i - 1]);
}
/* MaPolyLine::UVTransform 0x1804754e0 (MaFace's DB constructor 0x180413ba0 on the stored loop points when the record's range H differs from the
   surface's d): the translation to the range start when the starts differ (BuildMatrixFromPoints / Invert4X3Matrix / MultiplyTwo4X3Matrices:
   t = u0 - H0), then the scale about the start (ScaleMatrix 0x1808ecf20: s = du / (H1 - H0), the offset -((s - 1) u0)), each applied by
   MaPolyLine::Transform 0x180474ce0 (x' = y m4 + x m0 + m12, y' = x m1 + y m5 + m13) */
void mi_uv_transform(double *u, double *v, const double H[4], const double d[6]) {
    double x = *u, y = *v;
    if (H[0] != d[0] || H[2] != d[2]) { double tx = d[0] + -H[0], ty = d[2] + -H[2], x1 = y * 0.0 + x * 1.0 + tx, y1 = x * 0.0 + y * 1.0 + ty; x = x1; y = y1; }
    double sx = d[4] / (H[1] - H[0]), sy = d[5] / (H[3] - H[2]), ox = -((sx - 1.0) * d[0]), oy = -((sy - 1.0) * d[2]);
    double x2 = y * 0.0 + x * sx + ox, y2 = x * 0.0 + y * sy + oy;
    *u = x2; *v = y2;
}
/* the inverse of mi_regen_map: a regenerated parameter back to the knots before the regeneration (the same span, the same fraction) */
double mi_regen_unmap(const MiRegen *R, int dir, double x) {
    const double *U = R->newU[dir], *V = R->oldU[dir]; int n = R->nU[dir]; int i = 1; while (i < n && !(x <= U[i])) i++; if (i == n) i--; double d = U[i] - U[i - 1]; if (d < 1e-12) d = 1e-12; return V[i - 1] + (x - U[i - 1]) / d * (V[i] - V[i - 1]);
}
/* MaSurfHuntingGrid::GenGrid's parameter lists of a surface (grid_build: AdjustRangeForPolesAndSeams + TessellateUntrimmed) */
void mi_grid_lists(ISrf *s, const double **gu, int *nu, const double **gv, int *nv) { grid_build(s); *gu = s->gu; *nu = s->ngu; *gv = s->gv; *nv = s->ngv; }
void mi_regen_free(MiRegen *R) { for (int d = 0; d < 2; d++) { free(R->oldU[d]); free(R->newU[d]); } }
int mi_plane_piece_face(ISrf *s, MiFace *F, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **out) {
    int cap = 16, nc = 0; MiCurve *res = malloc(sizeof(MiCurve) * cap); g_chord_dev = chord; plane_one(s, F, pl, chord, chord2, maxstep, minstep, multi, &res, &nc, &cap); *out = res; return nc;
}
void srf_mi_free(ISrf *s) { free(s->gu); free(s->gv); free(s->gp); s->gu = s->gv = s->gp = NULL; s->ngu = s->ngv = 0; }

/* MaSurface::UnitNormalEvaluate (0x18048f160), order 2: o[0..2] dN/du, o[3..5] dN/dv, o[6..8] N_uu, o[9..11] N_vv, o[12..14] N_uv; G = the base evaluation to order 3 (p_3d offsets: 0x30 Su, 0x48 Sv, 0x60 Suu, 0x78 Svv, 0x90 Suv, 0xa8 Suuu, 0xc0 Svvv, 0xd8 Suuv, 0xf0 Suvv) */
void unit_normal2(const double *G, double *o) {
#define g(i) G[i]
    double d24 = g(11) * g(7) - g(10) * g(8), d22y = g(9) * g(8) - g(6) * g(11), d23 = g(6) * g(10) - g(9) * g(7);
    double len_ = sqrt(d24 * d24 + d22y * d22y + d23 * d23); if (fabs(len_) <= 1e-20 && fabs(len_) != 1e-20) len_ = 1e-20;
    double d25 = 1.0 / len_, nx = d24 * d25, ny = d22y * d25, nz = d23 * d25;
    double e22 = g(9), e1 = g(12), e2 = g(19), e3 = g(6), e41 = g(13), e4 = g(10), e33 = g(18), e34 = g(7);
    double d37 = (g(20) * g(7) - e2 * g(8)) + (g(13) * g(11) - g(14) * g(10));
    double d40 = (e33 * g(8) - e3 * g(20)) + (e22 * g(14) - e1 * g(11));
    double d2 = (e3 * e2 - e33 * e34) + (e1 * e4 - e22 * e41);
    double d33 = ny * d40 + d37 * nx + nz * d2;
    double d22 = (d37 - nx * d33) * d25, d38 = (d40 - ny * d33) * d25, d34 = (d2 - nz * d33) * d25;
    o[0] = d22; o[1] = d38; o[2] = d34;
    double d41 = (g(17) * g(7) - g(16) * g(8)) + (g(11) * g(19) - g(10) * g(20));
    double d4 = (g(15) * g(8) - g(6) * g(17)) + (g(9) * g(20) - g(18) * g(11));
    double d3 = (g(6) * g(16) - g(15) * g(7)) + (g(18) * g(10) - g(9) * g(19));
    double d35 = d41 * nx + ny * d4 + nz * d3;
    double d1 = (d41 - nx * d35) * d25, d39 = (d4 - ny * d35) * d25, d36 = (d3 - nz * d35) * d25;
    o[3] = d1; o[4] = d39; o[5] = d36;
    double d29 = (g(20) * g(13) - g(19) * g(14)) * 2.0 + (g(11) * g(22) - g(10) * g(23)) + (g(29) * g(7) - g(28) * g(8));
    double d27 = (g(18) * g(14) - g(12) * g(20)) * 2.0 + (g(9) * g(23) - g(21) * g(11)) + (g(27) * g(8) - g(6) * g(29));
    double d26 = (g(12) * g(19) - g(18) * g(13)) * 2.0 + (g(21) * g(10) - g(9) * g(22)) + (g(6) * g(28) - g(27) * g(7));
    double d37b = ny * d27 + d29 * nx + nz * d26 + d37 * d22 + d38 * d40 + d34 * d2;
    o[7] = ((d27 - d38 * d33 * 2.0) - ny * d37b) * d25;
    o[6] = ((d29 - d22 * d33 * 2.0) - nx * d37b) * d25;
    o[8] = ((d26 - d34 * d33 * 2.0) - nz * d37b) * d25;
    double h2 = g(31), h37 = g(9), h40 = g(16), h26 = g(30), h27 = g(19), h29 = g(18), h5 = g(10), h6 = g(15), h7 = g(25), h8 = g(24), h9 = g(6), h10 = g(7);
    double d28 = (h6 * g(20) - h29 * g(17)) * 2.0 + (h37 * g(32) - h26 * g(11)) + (h8 * g(8) - h9 * g(26));
    double d30 = (g(17) * h27 - h40 * g(20)) * 2.0 + (h2 * g(11) - g(32) * g(10)) + (g(26) * g(7) - h7 * g(8));
    double h2b = (h29 * h40 - h6 * h27) * 2.0 + (h26 * h5 - h37 * h2) + (h9 * h7 - h8 * h10);
    double d40b = d41 * d1 + d39 * d4 + d36 * d3 + ny * d28 + d30 * nx + nz * h2b;
    o[10] = ((d28 - d39 * d35 * 2.0) - ny * d40b) * d25;
    o[9] = ((d30 - d1 * d35 * 2.0) - nx * d40b) * d25;
    o[11] = ((h2b - d36 * d35 * 2.0) - nz * d40b) * d25;
    double k40 = (g(17) * g(13) - g(16) * g(14)) + (g(11) * g(28) - g(10) * g(29)) + (g(32) * g(7) - g(31) * g(8));
    double k37 = (g(15) * g(14) - g(12) * g(17)) + (g(9) * g(29) - g(27) * g(11)) + (g(30) * g(8) - g(6) * g(32));
    double k2 = (g(12) * g(16) - g(15) * g(13)) + (g(27) * g(10) - g(9) * g(28)) + (g(6) * g(31) - g(30) * g(7));
    double d41b = d41 * d22 + d38 * d4 + d34 * d3 + ny * k37 + k40 * nx + nz * k2;
    o[13] = (((k37 - d39 * d33) - d38 * d35) - ny * d41b) * d25;
    o[12] = (((k40 - d1 * d33) - d22 * d35) - nx * d41b) * d25;
    o[14] = (((k2 - d36 * d33) - d34 * d35) - nz * d41b) * d25;
#undef g
}
/* MaOffsetSurf::EvaluateSrf (0x180463740): the bottom base with +1 derivative order, the unit normal at the base scaled by the offset added to the point; the first derivatives corrected by the normal's derivatives (MaSurface::UnitNormalEvaluate); the normal from the crossed derivatives (flipped when this[0x10] is set); VerifyNormal's retry at moved parameters. The zero-offset / degenerate branch returns the base's values with the normal's sign from this[0x10] != base[0x10] */
void offset_ev(const ISrf *s, int order, double u, double v, double *E) {
    const ISrf *b = s->obase; double d1 = s->odist, cu = u, cv = v; int n = order, cnt = 0, retry = 0;
    if (d1 != 0.0) n = order + 1;
    do {
        retry = 0;
        double G[33]; memset(G, 0, sizeof G); ext_eval((ISrf *)b, n, cu, cv, G);
        double nz_ = G[10] * G[6] - G[9] * G[7], nx_ = G[11] * G[7] - G[10] * G[8], ny_ = G[9] * G[8] - G[11] * G[6], lN = sqrt(ny_ * ny_ + nx_ * nx_ + nz_ * nz_);
        if (d1 == 0.0 || lN < 1e-20) {
            memcpy(E, G, 24); if (n < 1) return;
            memcpy(E + 6, G + 6, 48); double sg = s->rev != b->rev ? -1.0 : 1.0; E[3] = nx_ * sg; E[4] = ny_ * sg; E[5] = nz_ * sg;
            if (n < 2) return;
            memcpy(E + 12, G + 12, 72); return;
        }
        double il = 1.0 / lN; double nu[3] = { nx_ * il, ny_ * il, nz_ * il };
        for (int k = 0; k < 3; k++) E[k] = nu[k] * d1 + G[k];
        if (order == 0) return;
        double D[15]; memset(D, 0, sizeof D); unit_normal2(G, D);   /* UnitNormalEvaluate, order 2 where order > 1, order 1 otherwise: the first six outputs do not depend on the third derivatives */
        for (int k = 0; k < 3; k++) { E[6 + k] = D[k] * d1 + G[6 + k]; E[9 + k] = D[3 + k] * d1 + G[9 + k]; }
        if (1 < order) for (int k = 0; k < 3; k++) { E[12 + k] = D[6 + k] * d1 + G[12 + k]; E[15 + k] = D[9 + k] * d1 + G[15 + k]; E[18 + k] = D[12 + k] * d1 + G[18 + k]; }
        double N0 = E[11] * E[7] - E[10] * E[8], N1 = E[9] * E[8] - E[6] * E[11], N2 = E[6] * E[10] - E[9] * E[7];
        E[3] = N0; E[4] = N1; E[5] = N2; if (s->rev == 1) { E[3] *= -1.0; E[4] *= -1.0; E[5] *= -1.0; }
        if (verify_normal(s, E, &cnt, &cu, &cv)) retry = 1;
    } while (retry);
}

/* ---- surface / surface: MaSurfSurfIntersect (Curve at Intersection, formats/wireframe.md) ---- */
/* MaSurface::GetMinMaxBox (0x180485ef0) = MaMinMaxBox::GenSrfBox as plane_init; MaMinMaxBox::operator&& (0x180439040) */
void surf_box(ISrf *s, double b[6]) {
    grid_build(s); double E[21]; box_init(b);
    for (int i = 0; i < s->ngu; i++) for (int j = 0; j < s->ngv; j++) box_add(b, s->gp + 3 * ((size_t)i * s->ngv + j));   /* the stored grid points */
    for (int i = 0; i < s->ngu - 1; i++) for (int j = 0; j < s->ngv - 1; j++) { is_eval(s, 0, (s->gu[i] + s->gu[i + 1]) * 0.5, (s->gv[j] + s->gv[j + 1]) * 0.5, E); box_add(b, E); }
    box_expand(b, s->metric);
}
int u_beyond(const ISrf *s, double u) { return !(s->d[0] <= u && u <= s->d[1]); }   /* IsUBeyondRange 0x180486a80 */
int v_beyond(const ISrf *s, double v) { return !(s->d[2] <= v && v <= s->d[3]); }
/* MaSurface::AdjustUVToRange (0x18047d780) */
void adjust_uv(ISrf *s, double *u, double *v) {
    EDGES(s);
    if (!(s->per[0] || s->per[1]) && !has_deg(s)) limit_uv(s, u, v);
    if (*u < s->d[1] || *u == s->d[1]) { if (*u < s->d[0]) { if (s->deg[0] && s->per[1]) *u = s->d[0]; else if (s->per[0]) { do *u += s->d[4]; while (*u < s->d[0]); } } }
    else { if (s->deg[1] && s->per[1]) *u = s->d[1]; else if (s->per[0]) { do *u -= s->d[4]; while (s->d[1] <= *u && *u != s->d[1]); } }
    if (*v < s->d[3] || *v == s->d[3]) { if (*v < s->d[2]) { if (s->deg[2] && s->per[0]) *v = s->d[2]; if (s->per[1]) { do *v += s->d[5]; while (*v < s->d[2]); } } }
    else { if (s->deg[3] && s->per[0]) *v = s->d[3]; if (s->per[1]) { do *v -= s->d[5]; while (s->d[3] <= *v && *v != s->d[3]); } }
}
/* MaSurface::AllCurveIntersections (0x18047d950): the iso curve against the surface s; 46 pieces (MaCurve::Subdivide 0x180409610); out[][3] = t u v */
void all_ci(ISrf *s, Iso *iso, const double sb[6], int max, double (*out)[3], int *nout) {
    double tol = s->metric ? 2.5e-05 : 1e-06; *nout = 0; EDGES(s);
    iso_hunt(iso); if (!box_box(sb, iso->box)) return;
    double T[47], dt = iso->t1 - iso->t0, E[21], P[3], o[6], dist, u = 0, v = 0, u2 = 0, v2 = 0, sd;
    T[0] = iso->t0; for (int i = 0; i < 46; i++) T[i + 1] = T[i] + dt / 46.0; T[46] = iso->t1;
    iso_eval(iso, 0, T[0], E); memcpy(P, E, 24);
    is_pmd(s, 1, P, o, &dist, &u, &v, 1);
    sd = (P[0] * o[3] + P[1] * o[4] + P[2] * o[5]) - (o[3] * o[0] + o[4] * o[1] + o[5] * o[2]);
    for (int i = 0; i < 46; i++) {
        double t0 = T[i], t1 = T[i + 1], sd2;
        iso_eval(iso, 0, t1, E); memcpy(P, E, 24);
        is_pmd(s, 1, P, o, &dist, &u2, &v2, 1);
        double N[3] = { o[3], o[4], o[5] }, d = N[0] * o[0] + N[1] * o[1] + N[2] * o[2];
        sd2 = (P[0] * N[0] + P[1] * N[1] + P[2] * N[2]) - d;
        if (sd2 * sd <= 1e-12) {
            double tm = (t0 + t1) * 0.5;   /* MaCurve::TMid (vtable +0x100) [the method is not in the dumps; the mean of the piece's range: inferred from its use here and in GetArcLengthMidPoint's fallback] */
            iso_eval(iso, 0, tm, E); memcpy(P, E, 24);
            is_pmd(s, 1, P, o, &dist, &u2, &v2, 1);
            double x[3] = { tm, u2, v2 };
#ifdef SPINT_TRACE
            if (getenv("SSINT_CITR")) fprintf(stderr, "CI seed dir %d par %.17g piece %.17g %.17g x %.17g %.17g %.17g\n", iso->dir, iso->par, t0, t1, x[0], x[1], x[2]);
#endif
            if (curve_intersect(s, iso, t0, t1, x, t1 - t0) == 0) {
                int bu = u_beyond(s, x[1]), bv = v_beyond(s, x[2]);
                if ((bu && s->per[0]) || (bv && s->per[1]) || (bu && s->per[1]) || (bv && s->per[0])) adjust_uv(s, &x[1], &x[2]);
                if (t0 <= x[0] && x[0] <= t1 && s->d[0] <= x[1] && x[1] <= s->d[1] && s->d[2] <= x[2] && x[2] <= s->d[3]) {
                    int dup = 0, j;
                    for (j = 0; j < *nout; j++) {
                        double A[21], B[21]; iso_eval(iso, 0, out[j][0], A); iso_eval(iso, 0, x[0], B);
                        if (sqrt((B[1] - A[1]) * (B[1] - A[1]) + (B[0] - A[0]) * (B[0] - A[0]) + (B[2] - A[2]) * (B[2] - A[2])) <= tol) { dup = 1; break; }
                    }
                    if (!dup) {
                        out[*nout][0] = x[0]; out[*nout][1] = x[1]; out[*nout][2] = x[2]; (*nout)++;
                        if (*nout == max) return;
                    }
                }
            }
        }
        sd = sd2;
    }
}
int iso_high_seam(const ISrf *s, int dir, double par) { return (s->per[0] && dir == 1 && par == s->d[1]) || (s->per[1] && dir == 2 && par == s->d[3]); }   /* MaSurfaceIsoCurve::IsHighSeam 0x1804910e0 */
/* MaSurface::SurfSurfInitIntersection (0x18048a460): this = th, param_1 = dr; a[] (u, v on th), b[] (u, v on dr) */
void ss_init(ISrf *th, ISrf *dr, int multi, int max, double **a, double **b, char *flags, int *nseed) {
    double bt[6], bd[6]; *nseed = 0;
    surf_box(th, bt); surf_box(dr, bd);
    if (!box_box(bt, bd)) return;
    EDGES(th); EDGES(dr);
    double out[100][3]; int nr = 0; Iso iso = { 0 };
    for (int pass = 0; pass < 2; pass++) {                                     /* local_e08: the boundaries, then the 15 interior iso lines at 1/16 */
        int cnt = pass ? 0xf : 2;
        for (int side = 0; side < 2; side++) {                                 /* local_e0c: 0: iso curves of dr against th; 1: iso curves of th against dr */
            ISrf *isrf = side == 0 ? dr : th, *other = side == 0 ? th : dr;
            for (int dir = 1; dir <= 2; dir++) {                               /* iVar9 */
                double base = dir == 1 ? isrf->d[4] : isrf->d[5], start = dir == 1 ? isrf->d[0] : isrf->d[2];
                double step = cnt == 2 ? base : base * 0.0625, par = cnt == 2 ? start : start + step;
                for (int k = 0; k < cnt; k++) {
                    free(iso.hu); iso.hu = NULL; iso_set(&iso, isrf, dir, par);
                    if (!iso_high_seam(isrf, dir, par)) {
                        all_ci(other, &iso, side == 0 ? bt : bd, 100, out, &nr);
                        for (int i = 0; i < nr; i++) {
                            double t = out[i][0], u = out[i][1], v = out[i][2]; int okk = 0;
                            if (!u_beyond(other, u) && !v_beyond(other, v)) {
                                if (dir == 1) { if (!u_beyond(isrf, par) && !v_beyond(isrf, t)) okk = 1; }
                                else if (dir == 2) { if (!v_beyond(isrf, par) && !u_beyond(isrf, t)) okk = 1; }
                                else okk = 1;
                            }
                            if (okk) {
                                double uu0 = dir == 1 ? par : t, vv0 = dir == 1 ? t : par;
                                if (side == 0) { a[*nseed][0] = u; a[*nseed][1] = v; b[*nseed][0] = uu0; b[*nseed][1] = vv0; }
                                else { b[*nseed][0] = u; b[*nseed][1] = v; a[*nseed][0] = uu0; a[*nseed][1] = vv0; }
                                flags[*nseed] = pass == 0 ? 1 : 0;
                                (*nseed)++;
                                if (*nseed == max) { free(iso.hu); return; }
                            }
                        }
                        if (!multi && 0 < *nseed) { free(iso.hu); return; }
                    }
                    par += step;
                }
            }
        }
    }
    free(iso.hu);
}
/* IsUVNearSeam (0x180486d00) */
int uv_near_seam(const ISrf *s, double a, double b) { return (s->per[0] && (fabs(a - s->d[0]) < s->d[4] * 5e-05 || fabs(a - s->d[1]) < s->d[4] * 5e-05)) || (s->per[1] && (fabs(b - s->d[2]) < s->d[5] * 5e-05 || fabs(b - s->d[3]) < s->d[5] * 5e-05)); }
/* MaSurfSurfIntersect::ComputeIntersectionsMain (0x1804a3ad0) + BuildTrimmedModelSpaceIntersectionCurves (0x1804a2aa0, the untrimmed branch) on one piece of each surface */
void ss_main(ISrf *s, ISrf *dr, double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **res_, int *ncurves_, int *cap_) {
    MaInt m; memset(&m, 0, sizeof m); m.dr = dr; m.chord = chord; m.chord2 = chord2; m.maxstep = maxstep; m.minstep = minstep;
    double *pool = malloc(sizeof(double) * 10000 * 10), *cpool = malloc(sizeof(double) * 10000 * 12), *ulist = malloc(sizeof(double) * 10002);
    m.P = malloc(sizeof(double *) * 10000); m.T = malloc(sizeof(double *) * 10000); m.UV = malloc(sizeof(double *) * 10000); m.UV2 = malloc(sizeof(double *) * 10000);
    for (int i = 0; i < 10000; i++) { m.P[i] = pool + i * 10; m.T[i] = pool + i * 10 + 3; m.UV[i] = pool + i * 10 + 6; m.UV2[i] = pool + i * 10 + 8; }
    double (*sa)[2] = malloc(sizeof(double[2]) * 1000), (*sb)[2] = malloc(sizeof(double[2]) * 1000); double *sA[1000], *sB[1000]; for (int i = 0; i < 1000; i++) { sA[i] = sa[i]; sB[i] = sb[i]; }
    char flags[1000]; int nseed = 0, ncurves = *ncurves_, cap = *cap_; MiCurve *res = *res_;
#ifdef MI_TRACE
    if (mi_trf) { fprintf(mi_trf, "IN\n"); mi_tron = 1; }
#endif
    ss_init(s, dr, multi, 1000, sA, sB, flags, &nseed);
#ifdef MI_TRACE
    if (mi_trf) { mi_tron = 0; fprintf(mi_trf, "INEND %d\n", nseed); for (int i = 0; i < nseed; i++) fprintf(mi_trf, "SEED %d %.17g %.17g %.17g %.17g %d\n", i, sA[i][0], sA[i][1], sB[i][0], sB[i][1], flags[i]); }
#endif
#ifdef SPINT_TRACE
    if (getenv("SPINT_INITDUMP")) { fprintf(stderr, "ss nseed %d\n", nseed); for (int z = 0; z < nseed; z++) fprintf(stderr, " %d (%.15g, %.15g) (%.15g, %.15g) %d\n", z, sA[z][0], sA[z][1], sB[z][0], sB[z][1], flags[z]); }
#endif
    for (int i = 0; i < nseed; i++) {
        if (sA[i][0] == -1.0 && sA[i][1] == -1.0) continue;
        memset(m.cut, 0, sizeof m.cut);
        m.su = sA[i][0]; m.sv = sA[i][1]; m.du = sB[i][0]; m.dv = sB[i][1];
        m.flag248 = flags[i];
        if (uv_near_seam(s, m.su, m.sv) && m.flag248 == 1) m.flag248 = 0;
        if (m.du == -1.0 && m.dv == -1.0) continue;
        m.chord = chord; m.chord2 = chord2; m.maxstep = maxstep; m.minstep = minstep;   /* InitTolerances */
#ifdef MI_TRACE
        if (mi_trf) { fprintf(mi_trf, "MK %.17g %.17g %.17g %.17g %d\n", m.su, m.sv, m.du, m.dv, m.flag248); mi_tron = 1; }
#endif
        int r = init_cut_ss(s, &m);
        if (r == 0) r = march_top(s, &m);
#ifdef MI_TRACE
        if (mi_trf) { mi_tron = 0; fprintf(mi_trf, "MKEND %d %d\n", r, m.n); for (int k = 0; k < m.n; k++) fprintf(mi_trf, "P %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n", k, m.P[k][0], m.P[k][1], m.P[k][2], m.UV[k][0], m.UV[k][1], m.UV2[k][0], m.UV2[k][1]); }
#endif
#ifdef SPINT_TRACE
        if (getenv("SSINT_SEEDS")) { fprintf(stderr, "S %d r %d n %d\n", i, r, m.n); for (int k = 0; k < m.n; k++) fprintf(stderr, "M %d %d %.12f %.12f %.12f %.12f %.12f %.12f %.12f\n", i, k, m.P[k][0], m.P[k][1], m.P[k][2], m.UV[k][0], m.UV[k][1], m.UV2[k][0], m.UV2[k][1]); continue; }
#endif
        if (r == 0) {
            if (2 < m.n && mi_ss_hook) {   /* BuildTrimmedModelSpaceIntersectionCurves 0x1804a2aa0 (trimmed faces): the march's curve (+0x218) to the hook (mcsrftrim.c) */
                int nseg = curve_fit(m.P, m.T, m.n, m.chord, cpool); gen_ulist(cpool, nseg, ulist);
                MiCurve whole; memset(&whole, 0, sizeof whole); to_nurb(cpool, nseg, ulist, &whole); whole.npts = m.n;
                mi_ss_hook(s, dr, &m, &whole, mi_march_ctx); free(whole.k); free(whole.p);
            } else if (2 < m.n) {
                int nseg = curve_fit(m.P, m.T, m.n, m.chord, cpool);
                gen_ulist(cpool, nseg, ulist);
                if (ncurves == cap) { cap *= 2; res = realloc(res, sizeof(MiCurve) * cap); }
                to_nurb(cpool, nseg, ulist, &res[ncurves]); res[ncurves].npts = m.n; ncurves++;
            }
            if (i + 1 < nseed) weed(s, &m, sA, sB, i + 1, nseed);
        } else if (r == 2) { sA[i][0] = sA[i][1] = -1.0; sB[i][0] = sB[i][1] = -1.0; }
    }
    free(pool); free(cpool); free(ulist); free(m.P); free(m.T); free(m.UV); free(m.UV2); free(sa); free(sb);
    *res_ = res; *ncurves_ = ncurves; *cap_ = cap;
}
/* MaSurfSurfIntersect::ComputeIntersections (0x1804a37a0): the smooth pieces of both surfaces */
int mi_surf_surf(ISrf *a, ISrf *b, double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **out) {
    ISrf *pa, *pb; int npa = mi_pieces(a, &pa), npb = mi_pieces(b, &pb), cap = 16, nc = 0;
    MiCurve *res = malloc(sizeof(MiCurve) * cap);
    for (int i = 0; i < npa; i++) for (int j = 0; j < npb; j++) ss_main(&pa[i], &pb[j], chord, chord2, maxstep, minstep, multi, &res, &nc, &cap);
    mi_free_pieces(pa, npa); mi_free_pieces(pb, npb); *out = res; return nc;
}
