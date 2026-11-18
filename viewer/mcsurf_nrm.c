/* mcsurf_nrm.c - the surface derivatives, the surface normal and the projection of a point onto a surface, ported from mccore
 * for Wireframe > Line Normal (decisions N9-16, N9-23; decomp: `viewer` comments cite mccore addresses, read from the Ghidra
 * dumps of 2026-11-09, formats/wireframe.md "Line Normal").
 *   eval_surf 0x18047ba50 (per kind: FUN_1805b1f10 NURBS, FUN_1805ec000 kind 1, eval_revsurf 0x18047a110 kind 2, FUN_180412490 kind 6,
 *   FUN_18041c730 / FUN_18041ced0 kind 7, FUN_180460cd0 kind 8), eval_surf_extrap 0x18047bf10, calc_surf_norm 0x1801637e0,
 *   calc_surf_unit_norm 0x180164290, project_pt_onto_surf 0x1805edee0 (FUN_1805ed4a0, FUN_1806faa70), in_polygon 0x18046c960,
 *   hit_polygon 0x18046be80, hold_uv_to_trim 0x1804c41d0, v_lincoeffs 0x1804b8900, FUN_1801c90d0's surface branch (mccad). */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "mcsurf.h"
#include "mcsurf_nrm.h"

static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int64_t rd64(const uint8_t *p) { int64_t v; memcpy(&v, p, 8); return v; }
static void cross(const double a[3], const double b[3], double o[3]) { double x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0]; o[0] = x; o[1] = y; o[2] = z; }
static double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static double norm3(const double a[3]) { return sqrt(dot3(a, a)); }
static const int ix[4][4] = { { 0, 2, 5, 9 }, { 1, 4, 8, -1 }, { 3, 7, -1, -1 }, { 6, -1, -1, -1 } };   /* eval_surf's order: ix[du][dv] */
static const double binom[4][4] = { { 1, 0, 0, 0 }, { 1, 1, 0, 0 }, { 1, 2, 1, 0 }, { 1, 3, 3, 1 } };

/* the basis functions of degree p at knot span sp and their derivatives up to nd (The NURBS Book A2.3) - the routine of FUN_1805b0fb0's
   tensor product: the derivatives of the rational surface follow from the same homogeneous derivatives (FUN_1804b7f40) */
static void basis_ders(int p, const uint8_t *K, int sp, double t, int nd, double ders[4][26]) {
    double ndu[26][26], left[26], right[26], a[2][26];
    ndu[0][0] = 1;
    for (int j = 1; j <= p; j++) {
        left[j] = t - rdd(K + 8 * (size_t)(sp + 1 - j)); right[j] = rdd(K + 8 * (size_t)(sp + j)) - t;
        double saved = 0;
        for (int r = 0; r < j; r++) { ndu[j][r] = right[r + 1] + left[j - r]; double tmp = ndu[r][j - 1] / ndu[j][r]; ndu[r][j] = saved + right[r + 1] * tmp; saved = left[j - r] * tmp; }
        ndu[j][j] = saved;
    }
    for (int j = 0; j <= p; j++) ders[0][j] = ndu[j][p];
    for (int r = 0; r <= p; r++) {
        int s1 = 0, s2 = 1; a[0][0] = 1;
        for (int k = 1; k <= nd; k++) {
            double d = 0; int rk = r - k, pk = p - k;
            if (r >= k) { a[s2][0] = a[s1][0] / ndu[pk + 1][rk]; d = a[s2][0] * ndu[rk][pk]; }
            int j1 = rk >= -1 ? 1 : -rk, j2 = r - 1 <= pk ? k - 1 : p - r;
            for (int j = j1; j <= j2; j++) { a[s2][j] = (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][rk + j]; d += a[s2][j] * ndu[rk + j][pk]; }
            if (r <= pk) { a[s2][k] = -a[s1][k - 1] / ndu[pk + 1][r]; d += a[s2][k] * ndu[r][pk]; }
            ders[k][r] = d; int t2 = s1; s1 = s2; s2 = t2;
        }
    }
    double r = 1;
    for (int k = 1; k <= nd; k++) { r *= p - k + 1; for (int j = 0; j <= p; j++) ders[k][j] *= r; }
}

/* nurb_interval (mccore 0x1805b0630): the span of t: the first knot (from deg + 1) above t + 1e-10, less one (at most nk - deg - 2); on a zero-length span, from the end the first knot below t - 1e-10 */
static int nurb_interval(double t, const uint8_t *K, int nk, int deg) {
    #define KV(i) rdd(K + 8 * (size_t)(i))
    if (t < KV(0)) t = KV(0);
    if (KV(nk - 1) < t) t = KV(nk - 1);
    int top = nk - (deg + 1), j = deg + 1;
    while (j <= top && KV(j) - 1e-10 <= t) j++;
    int r = j - 1, sp = r;
    if (KV(r + 1) - KV(r) <= 1e-10) {
        int i = nk - 1;
        while (i > 0 && t <= KV(i) + 1e-10) i--;
        if (i > 0) sp = i;
    }
    #undef KV
    return sp;
}

/* kind 0: FUN_1805b1f10 -> FUN_1805b0fb0 (homogeneous tensor product; non-rational when +0x13 bit 1), FUN_1804b7f40 (the quotient rule).
   Span selection as mcsurf.c eval_nurbs_surf (the positions); nurb_interval's +-1e-10 is the same span away from a repeated knot. */
static int nrm_nurbs(const uint8_t *r, uint32_t n, int order, double u, double v, double D[][3]) {
    if (n < 0x118) return -1;
    int nku = rd32(r + 0x100), nkv = rd32(r + 0x104), ncu = rd32(r + 0x108), ncv = rd32(r + 0x10c), vpp = r[0x110], du = r[0x111], dv = r[0x112], rat = !(r[0x113] & 1);
    if (du < 1 || dv < 1 || du > 25 || dv > 25 || ncu < du + 1 || ncv < dv + 1 || nku != ncu + du + 1 || nkv != ncv + dv + 1 || (vpp != 3 && vpp != 4)
        || 0x118 + 8 * ((size_t)nku + nkv + (size_t)vpp * ncu * ncv) > n) return -1;
    const uint8_t *KU = r + 0x118, *KV = KU + 8 * nku, *C = KV + 8 * nkv;
    #define KN(A, i) rdd((A) + 8 * (size_t)(i))
    if (u < KN(KU, du)) u = KN(KU, du); if (u > KN(KU, ncu)) u = KN(KU, ncu);
    if (v < KN(KV, dv)) v = KN(KV, dv); if (v > KN(KV, ncv)) v = KN(KV, ncv);
    #undef KN
    int su = nurb_interval(u, KU, nku, du), sv = nurb_interval(v, KV, nkv, dv);   /* FUN_1805b0fb0 */
    double Nu[4][26], Nv[4][26];
    basis_ders(du, KU, su, u, order, Nu); basis_ders(dv, KV, sv, v, order, Nv);
    double A[4][4][4];   /* [du][dv][x y z w] homogeneous */
    for (int k = 0; k <= order; k++) for (int l = 0; l + k <= order; l++) {
        double s[4] = { 0, 0, 0, 0 };
        for (int b = 0; b <= dv; b++) for (int a = 0; a <= du; a++) {
            const uint8_t *q = C + 8 * (size_t)vpp * ((size_t)(sv - dv + b) * ncu + (su - du + a));
            double w, p[3];
            if (vpp == 4) { w = rat ? rdd(q) : 1.0; for (int d = 0; d < 3; d++) p[d] = rdd(q + 8 * (1 + d)); }
            else { w = 1.0; for (int d = 0; d < 3; d++) p[d] = rdd(q + 8 * d); }
            double bf = Nu[k][a] * Nv[l][b];
            s[0] += bf * p[0]; s[1] += bf * p[1]; s[2] += bf * p[2]; s[3] += bf * w;
        }
        memcpy(A[k][l], s, sizeof s);
    }
    /* homogeneous (NURBS Book A4.4); non-rational: w = 1, the terms below are 0 */
    double out[4][4][3];
    if (fabs(A[0][0][3]) < 1e-300) return -1;
    for (int k = 0; k <= order; k++) for (int l = 0; l + k <= order; l++) {
        double p[3]; for (int d = 0; d < 3; d++) p[d] = A[k][l][d];
        for (int i = 0; i <= k; i++) for (int j = 0; j <= l; j++) { if (i == 0 && j == 0) continue;
            double c = binom[k][i] * binom[l][j] * A[i][j][3];
            for (int d = 0; d < 3; d++) p[d] -= c * out[k - i][l - j][d]; }
        for (int d = 0; d < 3; d++) out[k][l][d] = p[d] / A[0][0][3];
    }
    for (int k = 0; k <= order; k++) for (int l = 0; l + k <= order; l++) memcpy(D[ix[k][l]], out[k][l], sizeof out[k][l]);
    return 0;
}

/* kind 1: FUN_1805ec000 (eval_poly_deriv u then v; the global u, v -> patch and local parameter as eval_pps): [v power][u power][xyz] */
static int nrm_pps(const uint8_t *r, uint32_t n, int order, double u, double v, double D[][3]) {
    if (n < 0x110) return -1;
    int nu = rd32(r + 0x100), nv = rd32(r + 0x104), ou = r[0x108], ov = r[0x109];
    if (nu < 1 || nv < 1 || ou < 1 || ov < 1 || 0x110 + 8 * (size_t)ou * ov * 3 * nu * nv > n) return -1;
    double fu = u >= 0 ? floor(u) : ceil(u); int iu = (int)fu; if (u >= 0) u -= iu;
    if (iu >= 0) { if (iu > nu - 1) { u += iu - nu + 1; iu = nu - 1; } } else iu = 0;
    double fv = v >= 0 ? floor(v) : ceil(v); int iv = (int)fv; if (v >= 0) v -= iv;
    if (iv >= 0) { if (iv > nv - 1) { v += iv - nv + 1; iv = nv - 1; } } else iv = 0;
    size_t per = (size_t)ou * ov * 3;
    const uint8_t *c = r + 0x110 + 8 * (per * ((size_t)nu * iv + iu));
    for (int k = 0; k <= order; k++) for (int l = 0; l + k <= order; l++) {
        double a[3] = { 0, 0, 0 };
        for (int j = ov - 1; j >= 0; j--) for (int i = ou - 1; i >= 0; i--) {
            if (i < k || j < l) continue;
            double m = 1; for (int q = 0; q < k; q++) m *= i - q; for (int q = 0; q < l; q++) m *= j - q;
            m *= pow(u, i - k) * pow(v, j - l);
            for (int d = 0; d < 3; d++) a[d] += m * rdd(c + 8 * (3 * ((size_t)j * ou + i) + d));
        }
        memcpy(D[ix[k][l]], a, sizeof a);
    }
    return 0;
}

/* eval_curve with derivatives (order <= 3): points, lines, arcs, parametric splines (eval_poly_deriv), NURBS curves */
static void view_rot(const McamFile *f, int view, const double v[3], double w[3]) {   /* view_to_world (mcspline.c): rows transposed */
    double sm[9]; const double *m = NULL;
    if (view >= 2) { for (int k = 0; k < f->nfviews; k++) if (f->fviews[k].n == view) { m = f->fviews[k].m; break; }
                     if (!m && mcam_std_view(view, sm)) m = sm; }
    if (!m) { w[0] = v[0]; w[1] = v[1]; w[2] = v[2]; return; }
    for (int k = 0; k < 3; k++) w[k] = v[0] * m[k] + v[1] * m[3 + k] + v[2] * m[6 + k];
}
static int curve_ders(const McamFile *f, const CurveRec *c, int order, double t, double D[][3]) {
    const uint8_t *r = c->rec;
    switch (c->cls) {
    case 0x1: if (c->n < 0x80) return -1; for (int d = 0; d < 3; d++) { D[0][d] = rdd(r + 0x68 + 8 * d); for (int k = 1; k <= order; k++) D[k][d] = 0; } return 0;
    case 0x2: if (c->n < 0x98) return -1;
        for (int d = 0; d < 3; d++) { double a = rdd(r + 0x68 + 8 * d), b = rdd(r + 0x80 + 8 * d); D[0][d] = (1 - t) * a + t * b; if (order > 0) D[1][d] = b - a; for (int k = 2; k <= order; k++) D[k][d] = 0; }
        return 0;
    case 0x4: {
        if (c->n < 0xca) return -1;
        double rad = rdd(r + 0xb0), sw = rdd(r + 0xc0), a0 = rdd(r + 0xb8), a = t * sw + a0, cx = rdd(r + 0x98), cy = rdd(r + 0xa0), cz = rdd(r + 0xa8);
        int16_t view; memcpy(&view, r + 0xc8, 2);
        for (int k = 0; k <= order; k++) {
            double sc = pow(sw, k) * rad, ca = cos(a + k * M_PI / 2), sa = sin(a + k * M_PI / 2);
            double v[3] = { sc * ca + (k ? 0 : cx), sc * sa + (k ? 0 : cy), k ? 0 : cz }, w[3];
            view_rot(f, view, v, w); memcpy(D[k], w, sizeof w);
        }
        return 0;
    }
    case 0x8: {   /* eval_spline: segment floor(t), [power][coord] ascending */
        if (c->n < 0xc0) return -1;
        int ns = rd32(r + 0xb0), ord = r[0xb4], dim = r[0xb5];
        if (ns < 1 || ord < 1 || dim < 1 || dim > 3 || 0xc0 + 8 * (size_t)ns * ord * dim > c->n) return -1;
        double fl = t >= 0 ? floor(t) : ceil(t); int i = (int)fl; if (t >= 0) t -= i;
        if (i < 0) i = 0; else if (i > ns - 1) { t += i - ns + 1; i = ns - 1; }
        const uint8_t *co = r + 0xc0 + 8 * ((size_t)ord * dim * i);
        for (int k = 0; k <= order; k++) for (int d = 0; d < 3; d++) {
            double a = 0;
            D[k][d] = 0;
            if (d < dim) { a = 0; for (int m = ord - 1; m >= k; m--) { double ff = 1; for (int q = 0; q < k; q++) ff *= m - q; a += ff * pow(t, m - k) * rdd(co + 8 * ((size_t)m * dim + d)); } D[k][d] = a; }
        }
        return 0;
    }
    case 0x80: {   /* eval_nurbs_deriv: basis derivatives on the stored knots, homogeneous, quotient rule */
        if (c->n < 0xd8) return -1;
        int nk = rd32(r + 0xc8), nc = rd32(r + 0xcc), deg = r[0xd0], vpp = r[0xd1], rat = !((r[0xd2] >> 2) & 1);
        if (deg < 1 || deg > 25 || nc < deg + 1 || nk != nc + deg + 1 || vpp < 2 || vpp > 4 || 0xd8 + 8 * ((size_t)nk + (size_t)vpp * nc) > c->n) return -1;
        const uint8_t *K = r + 0xd8, *C = K + 8 * nk;
        double k0 = rdd(K + 8 * deg), k1 = rdd(K + 8 * nc);
        if (t < k0) t = k0; if (t > k1) t = k1;
        int s = nurb_interval(t, K, nk, deg);
        double N[4][26]; basis_ders(deg, K, s, t, order, N);
        double A[4][4]; memset(A, 0, sizeof A);
        for (int j = 0; j <= deg; j++) {
            const uint8_t *q = C + 8 * (size_t)vpp * (s - deg + j);
            double w = rat ? rdd(q) : 1.0; int o = rat ? 1 : 0, dim = vpp - o, cnt = dim < 3 ? dim : 3;
            for (int k = 0; k <= order; k++) { for (int d = 0; d < cnt; d++) A[k][d] += N[k][j] * rdd(q + 8 * (o + d)); A[k][3] += N[k][j] * w; }
        }
        if (fabs(A[0][3]) < 1e-300) return -1;
        for (int k = 0; k <= order; k++) { double p[3]; for (int d = 0; d < 3; d++) p[d] = A[k][d];
            for (int i = 1; i <= k; i++) { double bc = binom[k][i] * A[i][3]; for (int d = 0; d < 3; d++) p[d] -= bc * D[k - i][d]; }
            for (int d = 0; d < 3; d++) D[k][d] = p[d] / A[0][3]; }
        return 0;
    }
    default: return -1;
    }
}

/* kind 2: eval_revsurf: F = A + d ((C - A) . d), R = C - F, P = F + R cos v + (d x R) sin v; the derivatives in u follow C(u) (F' = d (C' . d), R' = C' - F'), in v rotate by 90 degrees each */
static int nrm_rev(const McamFile *f, const uint8_t *r, uint32_t n, int order, double u, double v, double D[][3]) {
    if (n < 0x150) return -1;
    const CurveRec *c = mcs_curve(f, rd64(r + 0x148)); if (!c) return -1;
    double C[4][3]; if (curve_ders(f, c, order, u, C)) return -1;
    double d[3], A[3]; for (int k = 0; k < 3; k++) { d[k] = rdd(r + 0x100 + 8 * k); A[k] = rdd(r + 0x118 + 8 * k); }
    double h = (C[0][0] - A[0]) * d[0] + (C[0][1] - A[1]) * d[1] + (C[0][2] - A[2]) * d[2];
    for (int k = 0; k <= order; k++) {
        double hk = k ? dot3(C[k], d) : h, F[3], R[3], X[3];
        for (int i = 0; i < 3; i++) F[i] = d[i] * hk + (k ? 0 : A[i]);
        for (int i = 0; i < 3; i++) R[i] = C[k][i] - F[i];
        cross(d, R, X);
        for (int l = 0; l + k <= order; l++) {
            double cv = cos(v + l * M_PI / 2), sv = sin(v + l * M_PI / 2);
            double p[3]; for (int i = 0; i < 3; i++) p[i] = R[i] * cv + X[i] * sv + (l ? 0 : F[i]);
            memcpy(D[ix[k][l]], p, sizeof p);
        }
    }
    return 0;
}

/* kind 6, order <= 1 (FUN_180412490; eval_curve with one more order): N the vector at +0, |N|, m = -N/|N|, X = m x C', Dir = X/|X| (v_z_3d when |X|
   components all < 1e-12); the +0x18 d: |d| < 1e12: P = C + v (N - d |N| Dir), else P = C + v sign(d) |N| Dir. Su = C' + a v DirU (DirU = (X' - (X' . Dir) Dir) / |X|, the unit vector's
   derivative: FUN_180412490 l.2873), Sv = N + a Dir, a = -d |N| (else the second branch: Su = C' + sg |N| v DirU, Sv = sg |N| Dir). */
static int nrm_draft(const McamFile *f, const uint8_t *r, uint32_t n, int order, double u, double v, double D[][3]) {
    if (order > 1) return -2;
    if (n < 0x128) return -1;
    const CurveRec *c = mcs_curve(f, rd64(r + 0x120)); if (!c) return -1;
    double C[4][3]; if (curve_ders(f, c, 2, u, C)) return -1;
    double N[3], dd = rdd(r + 0x118); for (int k = 0; k < 3; k++) N[k] = rdd(r + 0x100 + 8 * k);
    double nl = norm3(N); if (!(nl > 0)) return -1;
    double m[3] = { -N[0] / nl, -N[1] / nl, -N[2] / nl }, X[3], Dir[3], Xp[3], DirU[3] = { 0, 0, 0 };
    cross(m, C[1], X);
    double xl = norm3(X);
    if (fabs(X[0]) < 1e-12 && fabs(X[1]) < 1e-12 && fabs(X[2]) < 1e-12) { Dir[0] = 0; Dir[1] = 0; Dir[2] = 1; }
    else {
        for (int k = 0; k < 3; k++) Dir[k] = X[k] / xl;
        cross(m, C[2], Xp);
        double dp = dot3(Xp, Dir);
        for (int k = 0; k < 3; k++) DirU[k] = (Xp[k] - dp * Dir[k]) / xl;
    }
    if (fabs(dd) < 1e12) {
        double a = -dd * nl;
        for (int k = 0; k < 3; k++) { D[0][k] = C[0][k] + v * (N[k] + a * Dir[k]); if (order) { D[1][k] = C[1][k] + a * v * DirU[k]; D[2][k] = N[k] + a * Dir[k]; } }
    } else {   /* l.2755-2795: bVar4 |d| >= 1e12: sign flips |N| when -d > 0 */
        double sg = dd < 0 ? -1 : 1;   /* the sign as mcsurf.c eval_draft */
        for (int k = 0; k < 3; k++) { D[0][k] = C[0][k] + v * sg * nl * Dir[k]; if (order) { D[1][k] = C[1][k] + v * sg * nl * DirU[k]; D[2][k] = sg * nl * Dir[k]; } }
    }
    return 0;
}

/* FUN_180114560: Rodrigues rotation of r about the unit axis n by a */
static void rot(const double r[3], const double n[3], double a, double o[3]) {
    double c = cos(a), s = sin(a), dn = dot3(n, r), X[3]; cross(n, r, X);
    for (int k = 0; k < 3; k++) o[k] = r[k] * c + X[k] * s + n[k] * dn * (1 - c);
}
/* kind 7, order <= 1: FUN_18041c730: curves C0, C1, C2 at u, n = (C1 - C0) x (C2 - C0) / |..| (|..| < 1e-12: P = C0, Su = C0', Sv = v_z), FUN_1803fca60: P = C0 + Rot(C1 - C0, n, theta v),
   theta = acos(((C2 - C0) . (C1 - C0)) / |C1 - C0|^2); Sv = theta n x (P - C0); FUN_18041ced0: Su = C0' + (1 - v) Rot(C1' - C0', n, a v) + v Rot(C2' - C0', n, (v - 1) a), a = asin(|N| / (|A| |B|)) */
static int nrm_arcsweep(const McamFile *f, const uint8_t *r, uint32_t n, int order, double u, double v, double D[][3]) {
    if (order > 1) return -2;
    if (n < 0x118) return -1;
    double C[3][4][3];
    for (int i = 0; i < 3; i++) { const CurveRec *c = mcs_curve(f, rd64(r + 0x100 + 8 * i)); if (!c || curve_ders(f, c, 1, u, C[i])) return -1; }
    double A[3], B[3]; for (int k = 0; k < 3; k++) { A[k] = C[1][0][k] - C[0][0][k]; B[k] = C[2][0][k] - C[0][0][k]; }
    double nn[3]; cross(A, B, nn); double l = norm3(nn);
    if (!(l >= 1e-12)) { memcpy(D[0], C[0][0], sizeof D[0]); if (order) { memcpy(D[1], C[0][1], sizeof D[1]); D[2][0] = 0; D[2][1] = 0; D[2][2] = 1; } return 0; }
    for (int k = 0; k < 3; k++) nn[k] /= l;
    double rr = dot3(A, A), cs = rr > 0 ? dot3(B, A) / rr : 1, th = cs > 0.999999999999 ? 0 : cs < -0.999999999999 ? M_PI : acos(cs), R[3];
    rot(A, nn, th * v, R);
    for (int k = 0; k < 3; k++) D[0][k] = C[0][0][k] + R[k];
    if (!order) return 0;
    { double thn[3] = { th * nn[0], th * nn[1], th * nn[2] }; cross(thn, R, D[2]); }
    double al = sqrt(dot3(B, B) * dot3(A, A)), sr = al > 0 ? l / al : 0, as = sr <= 0.999999 ? (sr >= -0.999999 ? asin(sr) : -M_PI_2) : M_PI_2;
    double dA[3], dB[3], Ra[3], Rb[3];
    for (int k = 0; k < 3; k++) { dA[k] = C[1][1][k] - C[0][1][k]; dB[k] = C[2][1][k] - C[0][1][k]; }
    rot(dA, nn, as * v, Ra); rot(dB, nn, (v - 1.0) * as, Rb);
    for (int k = 0; k < 3; k++) D[1][k] = C[0][1][k] + (1.0 - v) * Ra[k] + v * Rb[k];
    return 0;
}

static const Surf *base_surf(const McamFile *f, const Surf *s, double *tot) {
    *tot = 0; const Surf *b = s;   /* find_base_surf 0x1806f75b0: nested offsets add, a trimmed base is followed */
    for (int g = 0; g < 32; g++) {
        if (b->kind == 8 && b->n >= 0x128) { *tot += rdd(b->rec + 0x118); const Surf *nb = mcs_surf(f, rd64(b->rec + 0x120)); if (!nb || nb == b) return NULL; b = nb; }
        else if (b->kind == 10 && b->n >= 0x108) { const Surf *nb = mcs_surf(f, rd64(b->rec + 0x100)); if (!nb || nb == b) return NULL; b = nb; }
        else break;
    }
    return b->kind == 8 || b->kind == 10 ? NULL : b;
}
/* kind 8, order <= 1: FUN_180460cd0: base B (find_base_surf, offsets added), P = B + tot n, n = N/|N|, N = Bu x Bv (calc_surf_norm); Su = Bu + tot (Nu - (Nu . n) n)/|N|, Nu = Buu x Bv + Bu x Buv */
static int nrm_offset(const McamFile *f, const Surf *s, int order, double u, double v, double D[][3]) {
    if (order > 1) return -2;
    double tot; const Surf *b = base_surf(f, s, &tot); if (!b) return -1;
    double B[10][3]; if (mcsn_eval(f, b, order + 1, u, v, B)) return -2;
    double N[3]; int e = mcsn_norm(f, b, B[1], B[2], u, v, N); if (e) return e;
    double l = norm3(N); if (!(l > 0)) return -1;
    double nh[3] = { N[0] / l, N[1] / l, N[2] / l };
    for (int k = 0; k < 3; k++) D[0][k] = B[0][k] + tot * nh[k];
    if (!order) return 0;
    double Nu1[3], Nu2[3], Nv1[3], Nv2[3], Nu[3], Nv[3];
    cross(B[3], B[2], Nu1); cross(B[1], B[4], Nu2); cross(B[4], B[2], Nv1); cross(B[1], B[5], Nv2);
    for (int k = 0; k < 3; k++) { Nu[k] = Nu1[k] + Nu2[k]; Nv[k] = Nv1[k] + Nv2[k]; }
    double du = dot3(Nu, nh), dv = dot3(Nv, nh);
    for (int k = 0; k < 3; k++) { D[1][k] = B[1][k] + tot * (Nu[k] - du * nh[k]) / l; D[2][k] = B[2][k] + tot * (Nv[k] - dv * nh[k]) / l; }
    return 0;
}

int mcsn_eval(const McamFile *f, const Surf *s, int order, double u, double v, double D[][3]) {
    if (!(s->n > 0x6e) || !(s->rec[0x6e] & 0x40)) return -1;   /* eval_surf: +6 bit 0x40 clear -> -1 */
    switch (s->kind) {
    case 0: return nrm_nurbs(s->rec, s->n, order, u, v, D);
    case 1: return nrm_pps(s->rec, s->n, order, u, v, D);
    case 2: return nrm_rev(f, s->rec, s->n, order, u, v, D);
    case 6: return nrm_draft(f, s->rec, s->n, order, u, v, D);
    case 7: return nrm_arcsweep(f, s->rec, s->n, order, u, v, D);
    case 8: return nrm_offset(f, s, order, u, v, D);
    case 10: { const Surf *b = s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL; return b && b != s && b->kind != 10 ? mcsn_eval(f, b, order, u, v, D) : -1; }
    case 4: return -2;   /* FUN_18042cad0: not ported (no kind 4 surface in the sample parts, formats/surfaces.md) */
    default: return -1;
    }
}

/* the domain ent +0x70.. (surf +8 .. +0x20) */
static void domain_of(const Surf *s, double d[4]) { mcs_surf_domain(s, d); }

/* calc_surf_norm (0x1801637e0, param_7 '\0' throughout here). N = Su x Sv, the limit min(metric ? 1e-4 : 4e-6, size^2), size = surf +0x70 (ent +0xd8). */
int mcsn_norm(const McamFile *f, const Surf *s, const double Su[3], const double Sv[3], double u, double v, double N[3]) {
    double limit = mcam_is_metric(f) ? 1.0e-4 : 4.0e-6;
    double sz = s->n > 0xd8 + 7 ? rdd(s->rec + 0xd8) : 0;
    if (sz * sz < limit) limit = sz * sz;
    cross(Su, Sv, N);
    double nl = norm3(N);
    if (limit < nl) return 0;                              /* l.116 */
    int degen = 1, bu = 0, bv = 0, sv = 1, bv16;
    double dv = 0, d[4]; domain_of(s, d);
    double lsu = norm3(Su);
    if (limit <= lsu) {
        double lsv = norm3(Sv);
        if (limit <= lsv) {
            double cang = dot3(Su, Sv) / (lsv * lsu), c = cos(0.08725);
            if (fabs(cang) < c) return 0;                  /* l.136-138: not parallel: N stands */
            double D[10][3]; { int e = mcsn_eval(f, s, 2, u, v, D); if (e) return e == -2 ? -2 : 1; }
            if (norm3(D[3]) <= norm3(D[5])) goto v_dir;    /* Suu not larger than Svv */
        }
        bu = 1; dv = (d[1] - d[0]) / 100.0; bv16 = u < d[1] - dv * 50.0;   /* the u direction (l.145-147) */
    } else {
v_dir:  dv = (d[3] - d[2]) / 100.0; bv16 = v < d[3] - dv * 50.0; bv = 1;   /* l.150-153 */
    }
    if (!bv16) dv = -dv;
    for (;;) {                                             /* l.159: at most 50 steps from the point */
        if (!degen) return 0;
        if (bu) u += dv; else if (bv) v += dv;
        double h = sv * dv, D[10][3];
        { int e = mcsn_eval(f, s, 3, u, v, D); if (e) return e == -2 ? -2 : 1; }
        cross(D[1], D[2], N);
        double a[3], b[3];
        if (bu) { cross(D[1], D[4], a); cross(D[3], D[2], b); } else { cross(D[1], D[5], a); cross(D[4], D[2], b); }
        for (int k = 0; k < 3; k++) N[k] += (b[k] + a[k]) * h;
        if (norm3(N) <= limit) {
            double c[3];
            if (bu) { cross(D[1], D[7], a); cross(D[3], D[4], b); cross(D[6], D[2], c); }
            else { cross(D[1], D[9], a); cross(D[4], D[5], b); cross(D[8], D[2], c); }
            double h2 = h * 0.5 * h;
            for (int k = 0; k < 3; k++) N[k] += (b[k] * 2.0 + a[k] + c[k]) * h2;
            if (limit < norm3(N)) degen = 0;
        } else degen = 0;
        if (++sv > 0x32) { if (degen) { N[0] = N[1] = N[2] = 0; } return degen; }
    }
}

/* calc_surf_unit_norm (0x180164290) */
int mcsn_unit_norm(const McamFile *f, const Surf *s, const double Su[3], const double Sv[3], double u, double v, double N[3]) {
    int e = mcsn_norm(f, s, Su, Sv, u, v, N);
    if (e) { N[0] = N[1] = N[2] = 0; return e; }
    double l = norm3(N);
    if (fabs(l) > 1e-12) { N[0] /= l; N[1] /= l; N[2] /= l; return 0; }
    N[0] = N[1] = N[2] = 0; return 1;
}

/* eval_surf order 2 where it is ported, else order 1: the callers (project_core, mcsn_track) read S, Su, Sv only (calc_surf_norm asks for the higher orders itself) */
static int eval2(const McamFile *f, const Surf *s, double u, double v, double D[][3]) {
    int e = mcsn_eval(f, s, 2, u, v, D);
    return e == -2 ? mcsn_eval(f, s, 1, u, v, D) : e;
}
/* eval_surf_extrap (0x18047bf10), order 2: outside the domain (non-periodic) the point on the edge, moved along the first derivative by the excess; periodic: wrapped; flags uin / vin 0 outside */
static int eval_extrap(const McamFile *f, const Surf *s, double u, double v, double D[][3], char *uin, char *vin) {
    double d[4]; domain_of(s, d);
    int ucl = s->rec[0x6e] & 1, vcl = s->rec[0x6e] & 2;
    double ex_u = 0, ex_v = 0; *uin = 1; *vin = 1;
    if (u < d[0]) { if (!ucl) { *uin = 0; ex_u = u - d[0]; u = d[0]; } else while (u < d[0]) u += d[1] - d[0]; }
    if (d[1] < u) { if (!ucl) { *uin = 0; ex_u = u - d[1]; u = d[1]; } else while (d[1] < u) u -= d[1] - d[0]; }
    if (v < d[2]) { if (!vcl) { *vin = 0; ex_v = v - d[2]; v = d[2]; } else while (v < d[2]) v += d[3] - d[2]; }
    if (d[3] < v) { if (!vcl) { *vin = 0; ex_v = v - d[3]; v = d[3]; } else while (d[3] < v) v -= d[3] - d[2]; }
    int e = eval2(f, s, u, v, D); if (e) return e;
    if (!*uin) for (int k = 0; k < 3; k++) D[0][k] += D[1][k] * ex_u;
    if (!*vin) for (int k = 0; k < 3; k++) D[0][k] += D[2][k] * ex_v;
    return 0;
}

/* the trim loops of a trimmed surface: ent +0x10c points, +0x128 loops, the points (uv) from ent +0x138, then the loop table (start, count) */
typedef struct { int nl, m; const uint8_t *pts; const uint8_t *table; } TrimV;
static int trim_view(const Surf *s, const Surf *trim, TrimV *t) {
    const Surf *q = trim ? trim : s;
    if (q->kind != 10 || q->n < 0x138) return 0;
    t->m = rd32(q->rec + 0x10c); t->nl = rd32(q->rec + 0x128);
    if (t->m <= 0 || t->nl <= 0 || 0x138 + 16 * (size_t)t->m + 8 * (size_t)t->nl > q->n) return 0;
    t->pts = q->rec + 0x138; t->table = t->pts + 16 * (size_t)t->m;
    return 1;
}
static void tv_pt(const TrimV *t, int i, double p[2]) { p[0] = rdd(t->pts + 16 * (size_t)i); p[1] = rdd(t->pts + 16 * (size_t)i + 8); }
static void tv_loop(const TrimV *t, int l, int *start, int *count) { *start = rd32(t->table + 8 * (size_t)l); *count = rd32(t->table + 8 * (size_t)l + 4); }

/* in_polygon (0x18046c960): 1 inside, 0 on a segment within 1e-10, -1 outside */
static int in_polygon(const TrimV *t, const double p[2]) {
    int in = 0;
    double xlo = p[0] - 1e-10, xhi = p[0] + 1e-10, y = p[1] - 1e-10;
    for (int l = 0; l < t->nl; l++) {
        int a, c; tv_loop(t, l, &a, &c); int b = a + c; double prev[2]; tv_pt(t, b, prev);
        for (int i = a; i <= b; i++) {
            double cur[2]; tv_pt(t, i, cur);
            double dv = cur[1] - prev[1];
            if (1e-10 <= dv || 1e-10 <= -dv) {
                if ((prev[1] < y && y <= cur[1]) || (cur[1] <= y && y < prev[1])) {
                    double xi = (p[1] - prev[1]) * (cur[0] - prev[0]) / dv + prev[0], dx = p[0] - xi;
                    if (dx < 1e-10 && -dx < 1e-10) return 0;
                    if (p[0] < xi) in ^= 1;
                }
            } else {
                double d = prev[1] - p[1];
                if ((d < 1e-10 && -d <= 1e-10) && ((xlo <= prev[0] && cur[0] <= xhi) || (prev[0] <= xhi && xlo <= cur[0]))) return 0;
            }
            prev[0] = cur[0]; prev[1] = cur[1];
        }
    }
    return in ? 1 : -1;
}
/* hit_polygon (0x18046be80, param_4 = 0: the end of each crossing segment's nearest point on it), up to 256 crossings; o (2 x count), returns the count */
static int hit_polygon(const TrimV *t, const double from[2], const double to[2], double (*o)[2]) {
    int n = 0;
    double du = to[0] - from[0], dv = to[1] - from[1];
    for (int l = 0; l < t->nl; l++) {
        int a, c; tv_loop(t, l, &a, &c); int b = a + c; double q[2]; tv_pt(t, b, q);
        double x0 = q[0] - from[0], y0 = q[1] - from[1], d0 = y0 * du - x0 * dv;
        for (int i = a; i <= b; i++) {
            double cur[2]; tv_pt(t, i, cur);
            double xc = cur[0] - from[0], yc = cur[1] - from[1], dc = yc * du - xc * dv;
            if (((dc <= 0.0 && 0.0 <= d0) || (0.0 <= dc && d0 <= 0.0)) && 1e-12 < fabs(dc - d0)) {
                double s = (yc * x0 - xc * y0) / (dc - d0);
                if (-1e-12 < s && s <= 1.000000000001) {
                    double ex = xc - x0, ey = yc - y0, r = ((du - x0) * ex + (dv - y0) * ey) / (ex * ex + ey * ey), hp[2];
                    if (1e-12 < r) {
                        if (0.999999999999 <= r) { hp[0] = cur[0]; hp[1] = cur[1]; }
                        else { hp[0] = cur[0] - ex * (1.0 - r); hp[1] = cur[1] - (1.0 - r) * ey; }
                    } else { hp[0] = x0 + from[0]; hp[1] = y0 + from[1]; }
                    int dup = 0;
                    for (int k = 0; k < n; k++) if (hypot(o[k][0] - hp[0], o[k][1] - hp[1]) < 1e-06) { dup = 1; break; }
                    if (!dup) { o[n][0] = hp[0]; o[n][1] = hp[1]; n++; if (n == 0x100) return 0; }
                }
            }
            x0 = xc; y0 = yc; d0 = dc;
        }
    }
    return n;
}
/* hold_uv_to_trim (0x1804c41d0, param_2 0, param_3 1): the new (u, v) as given, periodic wrapped; trimmed: hit_polygon, the crossing nearest the old point, flag = a crossing found; here the output (p7) */
static void hold_uv_to_trim(const Surf *s, const TrimV *t, const double from[2], const double to[2], double out[2], int *flag) {
    double d[4]; domain_of(s, d);
    out[0] = to[0]; out[1] = to[1]; *flag = 0;
    if (s->rec[0x6e] & 1) { if (out[0] < d[0]) out[0] = (d[1] - d[0]) + out[0]; else if (d[1] < out[0]) out[0] -= d[1] - d[0]; }   /* FUN_1804c4590 */
    if (s->rec[0x6e] & 2) { if (out[1] < d[2]) out[1] = (d[3] - d[2]) + out[1]; else if (d[3] < out[1]) out[1] -= d[3] - d[2]; }   /* FUN_1804c4190 */
    double hit[256][2]; int n = hit_polygon(t, from, to, hit);
    if (!n) return;
    *flag = 1;
    double bd = hypot(from[1] - hit[0][1], from[0] - hit[0][0]); out[0] = hit[0][0]; out[1] = hit[0][1];
    for (int k = 1; k < n; k++) { double c = hypot(from[1] - hit[k][1], from[0] - hit[k][0]); if (c < bd) { bd = c; out[0] = hit[k][0]; out[1] = hit[k][1]; } }
}

/* FUN_1805ed4a0 with param_2 >= 0 (the view), offset 0 (FUN_1806faa70: eval_surf_extrap, the record copied), called by project_pt_onto_surf with the flags 0, 0, 0.
   The view's Z from V rows; P in view coordinates; Newton on Su*, Sv*, Z (det3), the derivatives scaled by the ratio of their lengths to the last step's;
   stops on tol (found) at the point, on tol in view xy, on |du|+|dv| < 1e-6 after 11 steps, on a step larger than the domain, after 56 steps (not found). */
static int project_core(const McamFile *f, const Surf *s, const double V[9], const double P[3], const double uv0[2], double uv[2], double Sout[3], char *uin, char *vin) {
    double tolv = mc_systol_units(mcam_is_metric(f))->tol, tol = mcam_is_metric(f) ? tolv * 25.4 : tolv;   /* l.752-777: the point test scales the global tol by 25.4 when metric; the view xy test (l.856) uses the global as it is */
    double d[4]; domain_of(s, d);
    double du_rng = d[1] - d[0], dv_rng = d[3] - d[2];
    double Zv[3] = { V[6], V[7], V[8] };
    double pvx = V[0] * P[0] + V[1] * P[1] + V[2] * P[2], pvy = V[3] * P[0] + V[4] * P[1] + V[5] * P[2];
    double uv1[2] = { uv0[0], uv0[1] };
    double scale_u = 1.0, scale_v = 1.0, prev_su = 0, prev_sv = 0;
    int found = 0, stop = 0;
    double Spt[3] = { 0, 0, 0 };
    for (int it = 0; ; it++) {
        double D[10][3]; int e = eval_extrap(f, s, uv1[0], uv1[1], D, uin, vin); if (e) return e;
        memcpy(Spt, D[0], sizeof Spt);
        double R[3] = { P[0] - Spt[0], P[1] - Spt[1], P[2] - Spt[2] };
        if (norm3(R) < tol) { found = 1; break; }
        double vx = V[0] * Spt[0] + V[1] * Spt[1] + V[2] * Spt[2], vy = V[3] * Spt[0] + V[4] * Spt[1] + V[5] * Spt[2];
        double dv = hypot(vx - pvx, vy - pvy);
        if (dv < tolv) { found = 1; break; }
        if (it != 0) { scale_u = norm3(D[1]) / prev_su; scale_v = norm3(D[2]) / prev_sv; }
        prev_su = norm3(D[1]); prev_sv = norm3(D[2]);
        double Sum[3], Svm[3]; for (int k = 0; k < 3; k++) { Sum[k] = D[1][k] * scale_u; Svm[k] = D[2][k] * scale_v; }
        double C1[3]; cross(Svm, Zv, C1); double det = dot3(Sum, C1);   /* det3(Su*, Sv*, Z) */
        if (1e-12 <= fabs(det)) {
            double Rb[3]; cross(Svm, Zv, Rb); double du = dot3(R, Rb) / det;
            double Rc[3]; cross(Zv, Sum, Rc); double dv1 = dot3(R, Rc) / det;   /* det3(Su*, R, Z) */
            if (du_rng < fabs(du) || dv_rng < fabs(dv1)) stop = 1;
            else {
                uv1[0] += du; uv1[1] += dv1;
                if (fabs(dv1) + fabs(du) < 1e-06 && it > 10) { stop = 1; found = 1; }
            }
        } else { found = 0; stop = 1; }
        if (it > 0x37) stop = 1;
        if (stop) { if (!found) return 0; break; }
        if (s->rec[0x6e] & 1) { while (uv1[0] < d[0] - 1e-06) uv1[0] += du_rng; while (d[1] + 1e-06 < uv1[0]) uv1[0] -= du_rng; }
        if (s->rec[0x6e] & 2) { while (uv1[1] < d[2] - 1e-06) uv1[1] += dv_rng; while (d[3] + 1e-06 < uv1[1]) uv1[1] -= dv_rng; }
    }
    if (fabs(uv1[0] - d[0]) <= 1e-06) { *uin = 1; uv1[0] = d[0]; }
    if (fabs(uv1[0] - d[1]) < 1e-06) { *uin = 1; uv1[0] = d[1]; }
    if (fabs(uv1[1] - d[2]) <= 1e-06) { *vin = 1; uv1[1] = d[2]; }
    if (fabs(uv1[1] - d[3]) < 1e-06) { *vin = 1; uv1[1] = d[3]; }
    uv[0] = uv1[0]; uv[1] = uv1[1];
    memcpy(Sout, Spt, sizeof Spt);
    return 1;
}

int mcsn_project_t(const McamFile *f, const Surf *s, const Surf *trim, const double V[9], const double P[3], const double uv0[2], double uv[2], double Sout[3]) {
    char uin, vin; int got = project_core(f, s, V, P, uv0, uv, Sout, &uin, &vin);
    if (got != 1) return 0;
    TrimV t;
    if (trim_view(s, trim, &t)) return in_polygon(&t, uv) != -1;
    return 1;
}
int mcsn_project(const McamFile *f, const Surf *s, const double V[9], const double P[3], const double uv0[2], double uv[2], double Sout[3]) { return mcsn_project_t(f, s, NULL, V, P, uv0, uv, Sout); }

/* v_lincoeffs (0x1804b8900): a u1 + b u2 = p (normal equations) */
static void v_lincoeffs(const double a[2], const double b[2], const double p[2], double *ca, double *cb) {
    double a00 = a[0] * a[0] + a[1] * a[1], a01 = b[0] * a[0] + b[1] * a[1], a11 = b[0] * b[0] + b[1] * b[1];
    double pa = p[0] * a[0] + p[1] * a[1], pb = p[0] * b[0] + p[1] * b[1];
    double det = a11 * a00 - a01 * a01;
    if (fabs(det) < 1e-12) det = det <= 0 ? -1e-12 : 1e-12;
    *ca = (pa * a11 - pb * a01) / det; *cb = (pb * a00 - pa * a01) / det;
}

/* FUN_1801c90d0's surface branch (decomp/mccad_lines_helpers.c l.3054-3206; Point Dynamic and Line Normal share it) */
int mcsn_track_t(const McamFile *f, const Surf *s, const Surf *trim, McsnTrack *t, const double TV[9], const double CV[9], int proj, const double Pw[3], const double view_xy[2], char *msg, size_t msgn) {
    double tol = mc_systol_units(mcam_is_metric(f))->tol;
    double dv[2] = { view_xy[0] - t->last[0], view_xy[1] - t->last[1] };
    t->last[0] = view_xy[0]; t->last[1] = view_xy[1];
    if (fabs(dv[1]) + fabs(dv[0]) <= tol) return 0;             /* l.3054 */
    double prev[2] = { t->uv[0], t->uv[1] }, nw[2];
    int ok = 0;
    if (t->shown) {                                             /* an arrow exists (+0x29e0 != -1, l.3060) */
        double Sp[3]; int found = 0;
        if (proj) found = mcsn_project_t(f, s, trim, CV, Pw, prev, nw, Sp);
        if (!found) { double a, b; v_lincoeffs(t->su2, t->sv2, dv, &a, &b); nw[0] = prev[0] + a; nw[1] = prev[1] + b; }
        double d[4]; domain_of(s, d);
        if (s->rec[0x6e] & 1) { for (;;) { while (nw[0] < d[0]) nw[0] = (d[1] - d[0]) + nw[0]; if (nw[0] <= d[1]) break; nw[0] -= d[1] - d[0]; } }   /* l.3079-3092 */
        if (s->rec[0x6e] & 2) { for (;;) { while (nw[1] < d[2]) nw[1] = (d[3] - d[2]) + nw[1]; if (nw[1] <= d[3]) break; nw[1] -= d[3] - d[2]; } }
        TrimV tv;
        if (trim_view(s, trim, &tv)) {                           /* l.3107: in_polygon, hold_uv_to_trim */
            if (in_polygon(&tv, nw) == -1) { int fl; hold_uv_to_trim(s, &tv, prev, nw, t->uv, &fl); }
            else { t->uv[0] = nw[0]; t->uv[1] = nw[1]; }
        } else {                                                /* clamped to the domain, l.3123-3150 */
            t->uv[0] = nw[0] < d[0] ? d[0] : (d[1] < nw[0] ? d[1] : nw[0]);
            t->uv[1] = d[2] <= nw[1] ? (nw[1] <= d[3] ? nw[1] : d[3]) : d[2];
        }
        ok = 1;
    }
    (void)ok;
    double D[10][3];
    int e = eval2(f, s, t->uv[0], t->uv[1], D);          /* l.3153: order 2 */
    if (e) { if (msg && msgn) snprintf(msg, msgn, e == -2 ? "NOT DECODED: this surface kind's derivatives" : "eval_surf failed"); return e == -2 ? 1 : 2; }
    t->su2[0] = dot3(D[1], TV); t->su2[1] = dot3(D[1], TV + 3);  /* proj_iso_pt */
    t->sv2[0] = dot3(D[2], TV); t->sv2[1] = dot3(D[2], TV + 3);
    int nr = mcsn_unit_norm(f, s, D[1], D[2], t->uv[0], t->uv[1], t->N);
    if (nr == -2) { if (msg && msgn) snprintf(msg, msgn, "NOT DECODED: the normal at a degenerate point of this surface kind (order 2 / 3 derivatives)"); return 1; }
    if (nr) { t->N[0] = 0; t->N[1] = 0; t->N[2] = 1; }           /* l.3161-3165 */
    if (s->rec[0x6e] & 4) { t->N[0] = -t->N[0]; t->N[1] = -t->N[1]; t->N[2] = -t->N[2]; }
    memcpy(t->S, D[0], sizeof t->S);
    return 0;
}
int mcsn_track(const McamFile *f, const Surf *s, McsnTrack *t, const double TV[9], const double CV[9], int proj, const double Pw[3], const double view_xy[2], char *msg, size_t msgn) { return mcsn_track_t(f, s, NULL, t, TV, CV, proj, Pw, view_xy, msg, msgn); }

