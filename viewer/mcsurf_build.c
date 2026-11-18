/* mcsurf_build.c - the header fields Mastercam's put_surf_in_db computes for a new surface record (formats/surfaces_tab_plan.md
   "C. Revolve"; decomp/surfpick/core_dist.c): surf_closed, surf_extent with FUN_1806f8ce0 (degenerate edges), FUN_1806fa880 /
   surf_curv_geom (curvature maxima) and FUN_1806f9480 (centre / radius). Offsets are surf_type offsets (S = ent +0x68): +0 kind,
   +6 u16 flags, +8 u0, +0x10 u1, +0x18 v0, +0x20 v1, +0x28 / +0x40 bbox min / max, +0x58 centre, +0x70 radius, +0x78 / +0x80 / +0x88 /
   +0x90 curvature maxima, +0x98 the kind's data (D).
   Ours: eval_surf's derivatives (modes 1 / 2) are one-sided differences of mcs_surf_eval's positions (mcsurf.c computes positions
   only). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mcsurf.h"
#include "mcsurf_build.h"
#include "occt_prim.h"

static double rd(const uint8_t *b) { double v; memcpy(&v, b, 8); return v; }
static void wdd(uint8_t *b, double v) { memcpy(b, &v, 8); }
static int32_t r32(const uint8_t *b) { int32_t v; memcpy(&v, b, 4); return v; }
static uint16_t r16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static void w16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }

/* the basis functions of degree p at u and their first / second derivatives (ders[k][j], k <= 2) on span sp (Piegl & Tiller A2.3; ours,
   the standard algorithm - eval_surf's own NURBS derivative code is not dumped) */
static void basis_ders(const double *U, int sp, int p, double u, double ders[3][26]) {
    double ndu[26][26], left[26], right[26], a[2][26];
    ndu[0][0] = 1.0;
    for (int j = 1; j <= p; j++) {
        left[j] = u - U[sp + 1 - j]; right[j] = U[sp + j] - u; double saved = 0.0;
        for (int r = 0; r < j; r++) {
            ndu[j][r] = right[r + 1] + left[j - r];
            double t = ndu[j][r] != 0 ? ndu[r][j - 1] / ndu[j][r] : 0;
            ndu[r][j] = saved + right[r + 1] * t; saved = left[j - r] * t;
        }
        ndu[j][j] = saved;
    }
    for (int j = 0; j <= p; j++) ders[0][j] = ndu[j][p];
    for (int r = 0; r <= p; r++) {
        int s1 = 0, s2 = 1; a[0][0] = 1.0;
        for (int k = 1; k <= 2; k++) {
            double d = 0.0; int rk = r - k, pk = p - k;
            if (k > p) { ders[k][r] = 0; continue; }
            if (r >= k) { a[s2][0] = ndu[pk + 1][rk] != 0 ? a[s1][0] / ndu[pk + 1][rk] : 0; d = a[s2][0] * ndu[rk][pk]; }
            int j1 = rk >= -1 ? 1 : -rk, j2 = r - 1 <= pk ? k - 1 : p - r;
            for (int j = j1; j <= j2; j++) { a[s2][j] = ndu[pk + 1][rk + j] != 0 ? (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][rk + j] : 0; d += a[s2][j] * ndu[rk + j][pk]; }
            if (r <= pk) { a[s2][k] = ndu[pk + 1][r] != 0 ? -a[s1][k - 1] / ndu[pk + 1][r] : 0; d += a[s2][k] * ndu[r][pk]; }
            ders[k][r] = d; int t = s1; s1 = s2; s2 = t;
        }
    }
    for (int k = 1, r = p; k <= 2; k++) { for (int j = 0; j <= p; j++) ders[k][j] *= r; r *= (p - k); }
}
/* nurb_interval (mccore 0x1805b0630, decomp/surfrev/nbs_ders.c): the span of u in knots K[0..nk-1] of degree p - clamped to the
   knot range; i runs from p + 1 while i <= nk - p - 1 and K[i] - 1e-10 <= u (a knot within 1e-10 above u counts as passed), the
   span = the last i - 1; a span of length <= 1e-10 is replaced by the last j (from nk - 1 down) with u > K[j] + 1e-10 */
static int nurb_interval(double u, const double *K, int nk, int p) {
    if (u < K[0]) u = K[0];
    if (K[nk - 1] < u) u = K[nk - 1];
    int i = p + 1, hi = nk - (p + 1), r;
    while (i <= hi && K[i] - 1e-10 <= u) i++;
    r = (i <= hi ? i : hi + 1) - 1;
    if (r >= 0 && r + 1 < nk && K[r + 1] - K[r] <= 1e-10) {
        int j = nk - 1;
        while (j > 0 && u <= K[j] + 1e-10) j--;
        if (j > 0 || u > K[0] + 1e-10) r = j; else return r;
    }
    return r;
}
/* a kind 0 (NURBS) record's point and derivatives up to order 2 in raw (u, v), exact (rational: Piegl & Tiller A4.4). The span as
   mcsurf.c's evaluation finds it (the last knot span with u >= its start, the parameter clamped to the knot range). o[0] P, o[1] Su,
   o[2] Sv, o[3] Suu, o[4] Suv, o[5] Svv. -1 = not a readable NURBS record */
static int nurbs_ders(const uint8_t *r, uint32_t n, double u, double v, double o[6][3]) {
    if (n < 0x118) return -1;
    int nku = r32(r + 0x100), nkv = r32(r + 0x104), ncu = r32(r + 0x108), ncv = r32(r + 0x10c), vpp = r[0x110], du = r[0x111], dv = r[0x112], rat = !(r[0x113] & 1);
    if (du < 1 || dv < 1 || du > 25 || dv > 25 || ncu < du + 1 || ncv < dv + 1 || nku != ncu + du + 1 || nkv != ncv + dv + 1 || (vpp != 3 && vpp != 4)
        || 0x118 + 8 * ((size_t)nku + nkv + (size_t)vpp * ncu * ncv) > n) return -1;
    double *U = malloc(sizeof(double) * (size_t)(nku + nkv)), *V = U + nku;
    for (int i = 0; i < nku + nkv; i++) U[i] = rd(r + 0x118 + 8 * (size_t)i);
    const uint8_t *C = r + 0x118 + 8 * (size_t)(nku + nkv);
    if (u < U[0]) u = U[0]; if (u > U[nku - 1]) u = U[nku - 1];   /* nurb_interval's clamp */
    if (v < V[0]) v = V[0]; if (v > V[nkv - 1]) v = V[nkv - 1];
    int su = nurb_interval(u, U, nku, du), sv = nurb_interval(v, V, nkv, dv);
    double Nu[3][26], Nv[3][26]; basis_ders(U, su, du, u, Nu); basis_ders(V, sv, dv, v, Nv);
    double A[3][3][4]; memset(A, 0, sizeof A);       /* homogeneous derivatives A[k][l] (x, y, z, w) */
    for (int jj = 0; jj <= dv; jj++) for (int ii = 0; ii <= du; ii++) {
        const uint8_t *q = C + 8 * (size_t)vpp * ((size_t)(sv - dv + jj) * ncu + (su - du + ii));
        double P[4];
        if (vpp == 4) { P[3] = rat ? rd(q) : 1.0; for (int k = 0; k < 3; k++) P[k] = rd(q + 8 * (1 + k)); }
        else { P[3] = 1.0; for (int k = 0; k < 3; k++) P[k] = rd(q + 8 * k); }
        if (!rat && vpp == 4) P[3] = 1.0;
        for (int k = 0; k <= 2; k++) for (int l = 0; k + l <= 2; l++) { double b = Nu[k][ii] * Nv[l][jj]; for (int c = 0; c < 4; c++) A[k][l][c] += b * P[c]; }
    }
    free(U);
    double w = A[0][0][3]; if (!(fabs(w) > 1e-300)) return -1;
    double S[3][3][3]; static const int bin[3][3] = { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 2, 1 } };
    for (int k = 0; k <= 2; k++) for (int l = 0; k + l <= 2; l++) for (int c = 0; c < 3; c++) {
        double x = A[k][l][c];
        for (int j = 1; j <= l; j++) x -= bin[l][j] * A[0][j][3] * S[k][l - j][c];
        for (int i = 1; i <= k; i++) { x -= bin[k][i] * A[i][0][3] * S[k - i][l][c]; double t = 0; for (int j = 1; j <= l; j++) t += bin[l][j] * A[i][j][3] * S[k - i][l - j][c]; x -= bin[k][i] * t; }
        S[k][l][c] = x / w;
    }
    for (int c = 0; c < 3; c++) { o[0][c] = S[0][0][c]; o[1][c] = S[1][0][c]; o[2][c] = S[0][1][c]; o[3][c] = S[2][0][c]; o[4][c] = S[1][1][c]; o[5][c] = S[0][2][c]; }
    return 0;
}

/* eval_surf(S, mode, u, v, out): mode 0 the point, 1 + Su, Sv, 2 + Suu, Suv, Svv (out[0..5]). Ours: one-sided differences toward
   the inside of the range (forward, backward at the upper end) - the span on the right of a knot, as a span search takes it */
static int ev(const McamFile *f, const uint8_t *rec, uint32_t n, int mode, double u, double v, double o[6][3]) {
    if (rec[0x68] == 0) return nurbs_ders(rec, n, u, v, o);   /* NURBS: exact */
    Surf s; memset(&s, 0, sizeof s); s.rec = (uint8_t *)rec; s.n = n; s.kind = rec[0x68]; s.ent = -1;
    if (mcs_surf_eval(f, &s, u, v, o[0])) return -1;
    if (!mode) return 0;
    const uint8_t *S = rec + 0x68;
    double ru = rd(S + 0x10) - rd(S + 8), rv = rd(S + 0x20) - rd(S + 0x18);
    double hu = ru * 1e-5, hv = rv * 1e-5;
    if (hu <= 0) hu = 1e-7; if (hv <= 0) hv = 1e-7;
    if (u + 2 * hu > rd(S + 0x10)) hu = -hu;           /* at the upper end: backward */
    if (v + 2 * hv > rd(S + 0x20)) hv = -hv;
    double pu1[3], pu2[3], pv1[3], pv2[3], puv[3];
    if (mcs_surf_eval(f, &s, u + hu, v, pu1) || mcs_surf_eval(f, &s, u + 2 * hu, v, pu2) || mcs_surf_eval(f, &s, u, v + hv, pv1)
        || mcs_surf_eval(f, &s, u, v + 2 * hv, pv2) || mcs_surf_eval(f, &s, u + hu, v + hv, puv)) return -1;
    for (int k = 0; k < 3; k++) {                      /* second-order one-sided first derivatives */
        o[1][k] = (-3 * o[0][k] + 4 * pu1[k] - pu2[k]) / (2 * hu);
        o[2][k] = (-3 * o[0][k] + 4 * pv1[k] - pv2[k]) / (2 * hv);
    }
    if (mode < 2) return 0;
    for (int k = 0; k < 3; k++) {
        o[3][k] = (o[0][k] - 2 * pu1[k] + pu2[k]) / (hu * hu);
        o[4][k] = (puv[k] - pu1[k] - pv1[k] + o[0][k]) / (hu * hv);
        o[5][k] = (o[0][k] - 2 * pv1[k] + pv2[k]) / (hv * hv);
    }
    return 0;
}

/* FUN_1806f8ce0 (mccore 0x1806f8ce0): flag 0x80 cleared; a trimmed surface takes its base's bits 0x80 / 0x100 .. 0x800; else per
   edge (v0, v1 at mid u: |Su|; u0, u1 at mid v: |Sv|, sums of absolute components) < 0.001 sets 0x100 / 0x200 / 0x400 / 0x800 (and
   0x80), and below 1e-8 the edge moves inward by tol * 0.1 / |the other derivative| (taken again at 0.123 x the mid parameter when
   that is below tol) when that derivative exceeds tol */
static void degen(const McamFile *f, uint8_t *rec, uint32_t n, double tol) {
    uint8_t *S = rec + 0x68; uint16_t fl = r16(S + 6) & 0xff7f; w16(S + 6, fl);
    double t = tol * 0.1;
    if (S[0] == 10) {
        Surf tmp; memset(&tmp, 0, sizeof tmp); tmp.rec = rec; tmp.n = n; tmp.kind = 10;
        const Surf *b = mcam_surf_base(f, &tmp);
        if (b && b->n > 0x6f) w16(S + 6, (uint16_t)(fl ^ ((fl ^ r16(b->rec + 0x6e)) & 0xf80)));
        return;
    }
    static const struct { int bit, uedge, hi, deriv; } E[4] = { { 8, 0, 0, 1 }, { 9, 0, 1, 1 }, { 10, 1, 0, 2 }, { 11, 1, 1, 2 } };
    for (int e = 0; e < 4; e++) {
        double o[6][3];
        double mid = E[e].uedge ? (rd(S + 0x18) + rd(S + 0x20)) * 0.5 : (rd(S + 8) + rd(S + 0x10)) * 0.5;
        uint32_t eo = E[e].uedge ? (E[e].hi ? 0x10 : 8) : (E[e].hi ? 0x20 : 0x18);
        double edge = rd(S + eo);
        if (E[e].uedge ? ev(f, rec, n, 1, edge, mid, o) : ev(f, rec, n, 1, mid, edge, o)) continue;
        const double *D = o[E[e].deriv], *O = o[3 - E[e].deriv];
        double s1 = fabs(D[0]) + fabs(D[1]) + fabs(D[2]);
        fl = r16(S + 6) & (uint16_t)~(1u << E[e].bit);
        if (s1 < 0.001) fl |= (uint16_t)(1u << E[e].bit) | 0x80;
        w16(S + 6, fl);
        if (s1 < 1e-8) {
            double g = sqrt(O[0] * O[0] + O[1] * O[1] + O[2] * O[2]);
            if (g < tol) {
                if (E[e].uedge ? ev(f, rec, n, 1, edge, mid * 0.123, o) : ev(f, rec, n, 1, mid * 0.123, edge, o)) continue;
                O = o[3 - E[e].deriv]; g = sqrt(O[0] * O[0] + O[1] * O[1] + O[2] * O[2]);
            }
            if (g > tol) wdd(S + eo, E[e].hi ? edge - t / g : edge + t / g);
        }
    }
}

/* surf_closed (mccore 0x18047c980): dir 1 = closed in u (bit 0: the u0 and u1 edges meet), dir 2 = closed in v (bit 1). A trimmed
   surface copies its base's bit; kind 9 is never closed. The edges are compared at steps of 0.05 x the range (0.5 for kinds 2 / 7, the
   whole range for kind 6 in dir 1; dir 2 is never closed for kinds 6 / 7), points more than 1e-4 apart = open. find_base_surf is
   taken for the kind test (an offset surface tests its base's kind). */
static int base_kind(const McamFile *f, const uint8_t *rec, uint32_t n) {
    Surf tmp; memset(&tmp, 0, sizeof tmp); tmp.rec = (uint8_t *)rec; tmp.n = n; tmp.kind = rec[0x68];
    const Surf *s = &tmp;
    for (int g = 0; g < 16 && s->kind == 8 && s->n >= 0x128; g++) {   /* find_base_surf: an offset's base (D+0x20) */
        int64_t be; memcpy(&be, s->rec + 0x120, 8); const Surf *b = mcs_surf(f, be); if (!b) break; s = b;
    }
    return s->kind;
}
int mcsb_closed(const McamFile *f, uint8_t *rec, uint32_t n, int dir) {
    uint8_t *S = rec + 0x68; int closed = 1;
    if (S[0] == 10) {
        Surf tmp; memset(&tmp, 0, sizeof tmp); tmp.rec = rec; tmp.n = n; tmp.kind = 10;
        const Surf *b = mcam_surf_base(f, &tmp);
        closed = b && b->n > 0x6f ? (r16(b->rec + 0x6e) >> (dir - 1)) & 1 : 0;
    } else if (S[0] == 9) closed = 0;
    else {
        int k = base_kind(f, rec, n);
        double u0 = rd(S + 8), u1 = rd(S + 0x10), v0 = rd(S + 0x18), v1 = rd(S + 0x20), step;
        if (dir == 1) {
            if (fabs(v1 - v0) < 1e-12) closed = 0;
            else {
                step = v1 - v0; if (k != 6) step *= (k == 7 || k == 2) ? 0.5 : 0.05;
                for (double v = v0; closed && v <= v1; v += step) {
                    double a[6][3], b[6][3];
                    if (ev(f, rec, n, 0, u0, v, a) || ev(f, rec, n, 0, u1, v, b)) { closed = 0; break; }
                    if (sqrt((a[0][0] - b[0][0]) * (a[0][0] - b[0][0]) + (a[0][1] - b[0][1]) * (a[0][1] - b[0][1]) + (a[0][2] - b[0][2]) * (a[0][2] - b[0][2])) > 1e-4) closed = 0;
                }
            }
        } else {
            if (fabs(u1 - u0) < 1e-12 || k == 6 || k == 7) closed = 0;
            else {
                step = (u1 - u0) * 0.05;
                for (double u = u0; closed && u <= u1; u += step) {
                    double a[6][3], b[6][3];
                    if (ev(f, rec, n, 0, u, v0, a) || ev(f, rec, n, 0, u, v1, b)) { closed = 0; break; }
                    if (sqrt((a[0][0] - b[0][0]) * (a[0][0] - b[0][0]) + (a[0][1] - b[0][1]) * (a[0][1] - b[0][1]) + (a[0][2] - b[0][2]) * (a[0][2] - b[0][2])) > 1e-4) closed = 0;
                }
            }
        }
    }
    uint16_t fl = r16(S + 6) & (uint16_t)~(1u << (dir - 1));
    w16(S + 6, (uint16_t)(fl | (closed ? 1u << (dir - 1) : 0)));
    return closed;
}

/* FUN_1806fa880: |Su x Suu| / |Su| and |Sv x Svv| / |Sv| (0 when |S.|^2 <= 1e-10) */
static void curv_uv(double o[6][3], double *cu, double *cv) {
    const double *su = o[1], *sv = o[2], *suu = o[3], *svv = o[5];
    double x[3] = { su[1] * suu[2] - su[2] * suu[1], su[2] * suu[0] - su[0] * suu[2], su[0] * suu[1] - su[1] * suu[0] };
    double l = su[0] * su[0] + su[1] * su[1] + su[2] * su[2];
    *cu = l > 1e-10 ? sqrt((x[0] * x[0] + x[1] * x[1] + x[2] * x[2]) / l) : 0;
    double y[3] = { sv[1] * svv[2] - sv[2] * svv[1], sv[2] * svv[0] - sv[0] * svv[2], sv[0] * svv[1] - sv[1] * svv[0] };
    l = sv[0] * sv[0] + sv[1] * sv[1] + sv[2] * sv[2];
    *cv = l > 1e-10 ? sqrt((y[0] * y[0] + y[1] * y[1] + y[2] * y[2]) / l) : 0;
}
/* surf_curv_geom (mccore 0x18047ced0): the principal curvatures k1 >= k2 from the first / second fundamental forms (FUN_18047c530: E, F,
   G; FUN_18047c8b0: L, M, N with UnitNormal(Su, Sv); decomp/surfpick/core_dist.c, decomp/surfrev/unitnormal.c) */
static void curv_geom(double o[6][3], double *k1, double *k2) {
    const double *su = o[1], *sv = o[2];
    double E = su[0] * su[0] + su[1] * su[1] + su[2] * su[2], Fm = su[0] * sv[0] + su[1] * sv[1] + su[2] * sv[2], G = sv[0] * sv[0] + sv[1] * sv[1] + sv[2] * sv[2];
    double nn[3] = { su[1] * sv[2] - su[2] * sv[1], su[2] * sv[0] - su[0] * sv[2], su[0] * sv[1] - su[1] * sv[0] };
    double nl = sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
    *k1 = *k2 = 0; if (nl < 1e-12) nl = 1e-12;            /* UnitNormal (mccore 0x1804b7480): Su x Sv / |.|, divided by 1e-12 below that */
    for (int k = 0; k < 3; k++) nn[k] /= nl;
    double L = o[3][0] * nn[0] + o[3][1] * nn[1] + o[3][2] * nn[2], M = o[4][0] * nn[0] + o[4][1] * nn[1] + o[4][2] * nn[2], N = o[5][0] * nn[0] + o[5][1] * nn[1] + o[5][2] * nn[2];
    double d = E * G - Fm * Fm;
    if (fabs(d) < 1e-10) return;
    double H = ((N * E + L * G) * 0.5 - M * Fm) / d, K = (L * N - M * M) / d, q = H * H - K;
    if (fabs(q) < 1e-12) { *k1 = *k2 = H; return; }
    if (q < 0) return;
    q = sqrt(q); *k1 = H + q; *k2 = H - q;
}

/* the grid of surf_extent by kind (find_base_surf's kind) */
static void grid_n(const McamFile *f, const uint8_t *rec, uint32_t n, int *nu, int *nv) {
    const uint8_t *S = rec + 0x68, *D = S + 0x98;
    int k = base_kind(f, rec, n);
    const uint8_t *B = rec; uint32_t bn = n;
    if (k != S[0]) {                                   /* the base's own data (an offset surface) */
        Surf tmp; memset(&tmp, 0, sizeof tmp); tmp.rec = (uint8_t *)rec; tmp.n = n; tmp.kind = S[0];
        const Surf *s = &tmp;
        for (int g = 0; g < 16 && s->kind == 8 && s->n >= 0x128; g++) { int64_t be; memcpy(&be, s->rec + 0x120, 8); const Surf *b = mcs_surf(f, be); if (!b) break; s = b; }
        B = s->rec; bn = s->n; D = B + 0x100; (void)bn;
    }
    switch (k) {
    case 0: *nu = (int16_t)(int)sqrt((double)(r32(D + 8) + 4) * 8.0); if (*nu < 16 && D[0x11] > 1) *nu = 16;
            *nv = (int16_t)(int)sqrt((double)(r32(D + 0xc) + 4) * 8.0); if (*nv < 16 && D[0x12] > 1) *nv = 16; break;
    case 1: *nu = (int16_t)(int)sqrt((double)(r32(D) + 5) * 8.0); if (*nu < 16 && D[8] > 2) *nu = 16;
            *nv = (int16_t)(int)sqrt((double)(r32(D + 4) + 5) * 8.0); if (*nv < 16 && D[9] > 2) *nv = 16; break;
    case 2: { *nu = 10; double r = (rd(B + 0x68 + 0x20) - rd(B + 0x68 + 0x18)) * 2.0; r = r >= 0 ? floor(r + 0.5) : ceil(r - 0.5); *nv = (int16_t)(int)r * 2 + 2; } break;
    case 4: *nu = 10; *nv = r32(D + 4) - 1; if ((int16_t)r16(D) > 1) *nv *= 2; break;
    case 6: *nu = 10; *nv = 1; break;
    case 7: { int64_t ce; memcpy(&ce, D, 8); const CurveRec *c = mcs_curve(f, ce); *nv = 3; *nu = c && c->n >= 0xb4 ? r32(c->rec + 0xb0) : 5; } break;
    default: *nu = *nv = 5;
    }
}

/* surf_extent (mccore 0x1806f99e0) for an untrimmed surface: FUN_1806f8ce0; the grid u0 .. u1 / v0 .. v1 at (range / count) steps,
   the last at the end; mode bit 1 = the bounding box (+0x28 / +0x40), bit 2 = the curvature maxima (+0x78 = max FUN_1806fa880 u,
   +0x80 = max v, +0x88 = max(k1, 0), +0x90 = max(-k2, 0)); kind 7: +0x78 at least the centre curve record's +0xa8 [local_2a00 of the ent at
   +0x2a00 - 0x2aa8]; then FUN_1806f9480 (centre = the box middle +0x58, radius = half its diagonal +0x70). A trimmed surface (kind 10:
   its uv points) is NOT PORTED here: -1. */
/* surf_extent's trimmed branch (kind 10; decomp/surfpick/core_dist.c l.253-552): the range = the base's, or with D+9 set (ent +0x109) the
   uv points' min / max; mode bit 1: the base evaluated at the outer loop's points 0 .. count (the loop table after the m points: loop 0's
   count at +4), and between two consecutive points 6 more at sevenths when both lie on the same range edge (within 1e-4) or share u or
   v (within 1e-8); the curvature fields: the base's (below) */
static int extent_trim(const McamFile *f, uint8_t *rec, uint32_t n, int mode) {
    uint8_t *S = rec + 0x68;
    if (n < 0x138) return -1;
    Surf tmp; memset(&tmp, 0, sizeof tmp); tmp.rec = rec; tmp.n = n; tmp.kind = 10;
    const Surf *b = mcam_surf_base(f, &tmp); if (!b || b->n < 0x100) return -1;
    int m = r32(rec + 0x10c), nl = r32(rec + 0x128);
    if (m < 1 || nl < 1 || 0x138 + 16 * (size_t)m + 8 * (size_t)nl > n) return -1;
    const uint8_t *P = rec + 0x138, *Lt = P + 16 * (size_t)m;
    if (rec[0x109] == 0) memcpy(S + 8, b->rec + 0x70, 32);
    else {
        double r[4] = { 1e38, -1e38, 1e38, -1e38 };
        for (int i = 0; i < m; i++) { double u = rd(P + 16 * i), v = rd(P + 16 * i + 8); if (u <= r[0]) r[0] = u; if (u >= r[1]) r[1] = u; if (v <= r[2]) r[2] = v; if (v >= r[3]) r[3] = v; }
        for (int k = 0; k < 4; k++) wdd(S + 8 + 8 * k, r[k]);
    }
    degen(f, rec, n, 0);                               /* FUN_1806f8ce0: the base's bits */
    if (mode & 1) {
        int np = r32(Lt + 4) + 1; if (np > m) np = m;
        double lo[3] = { 1e38, 1e38, 1e38 }, hi[3] = { -1e38, -1e38, -1e38 }, bu0 = rd(b->rec + 0x70), bu1 = rd(b->rec + 0x78), bv0 = rd(b->rec + 0x80), bv1 = rd(b->rec + 0x88);
        double pu = 0, pv = 0; int pe_u = 0, pe_v = 0;
#define ACCP(U, V) do { double o_[6][3]; if (!ev(f, b->rec, b->n, 0, (U), (V), o_)) for (int k_ = 0; k_ < 3; k_++) { if (o_[0][k_] < lo[k_]) lo[k_] = o_[0][k_]; if (o_[0][k_] > hi[k_]) hi[k_] = o_[0][k_]; } } while (0)
        for (int i = 0; i < np; i++) {
            double u = rd(P + 16 * i), v = rd(P + 16 * i + 8);
            ACCP(u, v);
            int eu = fabs(u - bu0) < 1e-4 ? 1 : fabs(u - bu1) < 1e-4 ? 2 : 0, evv = fabs(v - bv0) < 1e-4 ? 1 : fabs(v - bv1) < 1e-4 ? 2 : 0;
            if (i > 0 && ((eu && eu == pe_u) || (evv && evv == pe_v) || fabs(u - pu) < 1e-8 || fabs(v - pv) < 1e-8))
                for (int k = 1; k <= 6; k++) ACCP(pu + (u - pu) * k / 7.0, pv + (v - pv) * k / 7.0);
            pu = u; pv = v; pe_u = eu; pe_v = evv;
        }
#undef ACCP
        for (int k = 0; k < 3; k++) { wdd(S + 0x28 + 8 * k, lo[k]); wdd(S + 0x40 + 8 * k, hi[k]); }
    }
    memcpy(S + 0x78, b->rec + 0x68 + 0x78, 0x20);    /* the curvature fields = the base's: what find_base_surf's copy leaves in that
                                                        branch's locals - measured (tools/tests/surfhdr_test.c: 982 corpus trimmed surfaces) */
    double h[3], r = 0;                                /* FUN_1806f9480 */
    for (int k = 0; k < 3; k++) { h[k] = (rd(S + 0x40 + 8 * k) - rd(S + 0x28 + 8 * k)) * 0.5; r += h[k] * h[k]; }
    wdd(S + 0x70, sqrt(r));
    for (int k = 0; k < 3; k++) wdd(S + 0x58 + 8 * k, h[k] + rd(S + 0x28 + 8 * k));
    return 0;
}
int mcsb_extent(const McamFile *f, uint8_t *rec, uint32_t n, int mode, double tol) {
    uint8_t *S = rec + 0x68;
    if (S[0] == 10) return extent_trim(f, rec, n, mode);
    degen(f, rec, n, tol);
    int nu, nv; grid_n(f, rec, n, &nu, &nv);
    double u0 = rd(S + 8), u1 = rd(S + 0x10), v0 = rd(S + 0x18), v1 = rd(S + 0x20);
    double du = (u1 - u0) / nu, dv = (v1 - v0) / nv;
    if (du < 1e-12 || dv < 1e-12) return -1;
    double lo[3] = { 1e38, 1e38, 1e38 }, hi[3] = { -1e38, -1e38, -1e38 }, cu = 0, cv = 0, k1m = 0, k2m = 0;
    int em = (mode & 2) ? 2 : 0, endu = 0;
    for (double u = u0; !endu; u += du) {
        if (u >= u1) { endu = 1; u = u1; }
        int endv = 0;
        for (double v = v0; !endv; v += dv) {
            if (v >= v1) { endv = 1; v = v1; }
            double o[6][3]; if (ev(f, rec, n, em, u, v, o)) return -1;
            if (mode & 1) for (int k = 0; k < 3; k++) { if (o[0][k] < lo[k]) lo[k] = o[0][k]; if (o[0][k] > hi[k]) hi[k] = o[0][k]; }
            if (mode & 2) {
                double a, b; curv_uv(o, &a, &b); if (a >= cu) cu = a; if (b >= cv) cv = b;
                double k1, k2; curv_geom(o, &k1, &k2); if (k1 >= k1m) k1m = k1; if (-k2 >= k2m) k2m = -k2;
            }
        }
    }
    if (mode & 1) for (int k = 0; k < 3; k++) { wdd(S + 0x28 + 8 * k, lo[k]); wdd(S + 0x40 + 8 * k, hi[k]); }
    if (mode & 2) { wdd(S + 0x78, cu); wdd(S + 0x80, cv); wdd(S + 0x88, k1m < 0 ? 0 : k1m); wdd(S + 0x90, k2m >= 0 ? k2m : 0); }
    if (base_kind(f, rec, n) == 7) {
        int64_t ce; memcpy(&ce, S + 0x98, 8); const CurveRec *c = mcs_curve(f, ce);
        if (c && c->n >= 0xb0 && rd(S + 0x78) < rd(c->rec + 0xa8)) wdd(S + 0x78, rd(c->rec + 0xa8));
    }
    /* FUN_1806f9480 */
    double h[3], r = 0;
    for (int k = 0; k < 3; k++) { h[k] = (rd(S + 0x40 + 8 * k) - rd(S + 0x28 + 8 * k)) * 0.5; r += h[k] * h[k]; }
    wdd(S + 0x70, sqrt(r));
    for (int k = 0; k < 3; k++) wdd(S + 0x58 + 8 * k, h[k] + rd(S + 0x28 + 8 * k));
    return 0;
}

/* ------------------------------------------------------------------------------------------------ NURBS conversion */
/* nb_type (mcam.md "NURBS curve") as built here: dim 3 xyz (non-rational) or 4 (w, x, y, z) with x y z Cartesian until homogenised */
typedef struct { int nk, ncp, deg, dim, rat; double *k, *p; } Nb;   /* k: malloc'd (nb_free) */
static void nb_free(Nb *b) { free(b->k); free(b->p); b->k = b->p = NULL; }
#define NB_MAXCP 4096
static void cross3(const double a[3], const double b[3], double o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }

/* convert_line_to_nb (mccadutil 0x1800593d0, decomp/surfrev/util_conv.c): degree 1, 3 values, non-rational (flags 0x14), knots 0 0 1 1 */
static int line_nb(const V3 a, const V3 b, Nb *o) {
    o->nk = 4; o->ncp = 2; o->deg = 1; o->dim = 3; o->rat = 0; o->k = malloc(sizeof(double) * 4); o->k[0] = o->k[1] = 0; o->k[2] = o->k[3] = 1;
    o->p = malloc(sizeof(double) * 6); o->p[0] = a.x; o->p[1] = a.y; o->p[2] = a.z; o->p[3] = b.x; o->p[4] = b.y; o->p[5] = b.z;
    return 0;
}
/* convert_arc_to_nb (mccadutil 0x1800588a0): degree 2, (w, x, y, z), rational. N = the view's z, reversed for a sweep <= tol; d = start
   - centre. |sweep| within tol of 2 pi: 9 points, knots 0 0 0 1 1 2 2 3 3 4 4 4, quadrants C + d + q (q = N x d) ... with w sqrt(1/2);
   within tol of pi: 5 points, knots 0 0 0 1 1 2 2 2; below pi: 3 points, knots 0 0 0 1 1 1, P1 = C + (d + e) / (1 + cos), w = sqrt((1 +
   cos) / 2), e = end - centre; above pi: the middle m = -(d + e) / sqrt(2 (1 + cos)) splits it into two such halves. tol = the system
   tolerance. */
static int arc_nb(V3 C, double r, const double N0[3], V3 s, V3 e, double sw, double tol, Nb *o) {
    double N[3] = { N0[0], N0[1], N0[2] };
    if (sw <= tol) { N[0] = -N[0]; N[1] = -N[1]; N[2] = -N[2]; }
    double as = fabs(sw), full = fabs(as - 2 * M_PI), half = fabs(as - M_PI), h = sqrt(0.5);
    o->deg = 2; o->dim = 4; o->rat = 1;
    o->ncp = full <= tol ? 9 : half <= tol || as > M_PI ? 5 : 3; o->nk = o->ncp + 3;
    static const double K9[12] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4 }, K5[8] = { 0, 0, 0, 1, 1, 2, 2, 2 }, K3[6] = { 0, 0, 0, 1, 1, 1 };
    o->k = malloc(sizeof(double) * o->nk); memcpy(o->k, o->ncp == 9 ? K9 : o->ncp == 5 ? K5 : K3, sizeof(double) * o->nk);
    double *P = o->p = calloc((size_t)o->ncp * 4, sizeof(double));
    double d[3] = { s.x - C.x, s.y - C.y, s.z - C.z }, q[3], c[3] = { C.x, C.y, C.z };
    cross3(N, d, q);
#define SETP(i, w, X, Y, Z) do { P[4 * (i)] = (w); P[4 * (i) + 1] = (X); P[4 * (i) + 2] = (Y); P[4 * (i) + 3] = (Z); } while (0)
    SETP(0, 1.0, s.x, s.y, s.z);
    int i = 1;
    if (full <= tol || half <= tol) {
        SETP(1, h, c[0] + d[0] + q[0], c[1] + d[1] + q[1], c[2] + d[2] + q[2]);
        SETP(2, 1.0, c[0] + q[0], c[1] + q[1], c[2] + q[2]);
        SETP(3, h, c[0] - d[0] + q[0], c[1] - d[1] + q[1], c[2] - d[2] + q[2]); i = 4;
        if (full <= tol) {
            SETP(4, 1.0, c[0] - d[0], c[1] - d[1], c[2] - d[2]);
            SETP(5, h, c[0] - d[0] - q[0], c[1] - d[1] - q[1], c[2] - d[2] - q[2]);
            SETP(6, 1.0, c[0] - q[0], c[1] - q[1], c[2] - q[2]);
            SETP(7, h, c[0] + d[0] - q[0], c[1] + d[1] - q[1], c[2] + d[2] - q[2]); i = 8;
        }
    } else {
        double r2 = r * r, E[3] = { e.x - C.x, e.y - C.y, e.z - C.z };
        double cs = (E[0] * d[0] + E[1] * d[1] + E[2] * d[2]) / r2 + 1.0;
        if (as < M_PI) {
            double w = sqrt(cs * 0.5), f = 1.0 / cs;
            SETP(1, w, c[0] + (E[0] + d[0]) * f, c[1] + (E[1] + d[1]) * f, c[2] + (E[2] + d[2]) * f); i = 2;
        } else {
            double g = -1.0 / sqrt(cs * 2.0), m[3] = { (E[0] + d[0]) * g, (E[1] + d[1]) * g, (E[2] + d[2]) * g };
            double c2 = (m[0] * d[0] + m[1] * d[1] + m[2] * d[2]) / r2 + 1.0, w = sqrt(c2 * 0.5), f = 1.0 / c2;
            SETP(1, w, c[0] + (m[0] + d[0]) * f, c[1] + (m[1] + d[1]) * f, c[2] + (m[2] + d[2]) * f);
            SETP(2, 1.0, c[0] + m[0], c[1] + m[1], c[2] + m[2]);
            SETP(3, w, c[0] + (m[0] + E[0]) * f, c[1] + (m[1] + E[1]) * f, c[2] + (m[2] + E[2]) * f); i = 4;
        }
    }
    SETP(i, 1.0, e.x, e.y, e.z);
#undef SETP
    return 0;
}
/* convert_2darc_to_nurbdata (mccore 0x18025f5e0, decomp/surfcreate/core_helpers.c l.150): the same construction in the plane, z = h,
   from (cx, cy, r, start angle a, sweep s) - r <= tol: every point at the centre (w sqrt(1/2) / 1) */
static int arc2d_nb(double cx, double cy, double r, double a, double s, double hz, double tol, double *P, double *K, int *ncp, int *nk) {
    double as = fabs(s), full = fabs(as - 2 * M_PI), hw = sqrt(0.5);
    *ncp = full <= tol ? 9 : (tol > fabs(as - M_PI) || as > M_PI) ? 5 : 3; *nk = *ncp + 3;
    K[0] = K[1] = K[2] = 0; K[3] = K[4] = 1;
    if (full <= tol) { K[5] = K[6] = 2; K[7] = K[8] = 3; K[9] = K[10] = K[11] = 4; }
    else if (as <= M_PI - tol) K[5] = 1;
    else K[5] = K[6] = K[7] = 2;
#define SP(i, w, X, Y) do { P[4 * (i)] = (w); P[4 * (i) + 1] = (X); P[4 * (i) + 2] = (Y); P[4 * (i) + 3] = hz; } while (0)
    SP(0, 1.0, cos(a) * r + cx, sin(a) * r + cy);
    double dx = cos(a) * r, dy = sin(a) * r, qx = -dy, qy = dx; int i = 1;
    if (full <= tol || fabs(as - M_PI) < tol) {
        SP(1, hw, dx + cx + qx, dy + cy + qy); SP(2, 1.0, cx - dy, cy + dx); SP(3, hw, cx - dx + qx, cy - dy + qy); i = 4;
        if (full <= tol) { SP(4, 1.0, cx - dx, cy - dy); SP(5, hw, cx - dx - qx, cy - dy - qy); SP(6, 1.0, cx + dy, cy - dx); SP(7, hw, cx + dx - qx, cy + dy - qy); i = 8; }
    } else {
        double ex = cos(a + s) * r, ey = sin(a + s) * r, fx = 0, fy = 0, cs = 0;
        if (tol < r) { fx = ex + dx; fy = ey + dy; cs = (ey * dy + ex * dx) / (r * r) + 1.0; }
        if (as < M_PI) {
            if (r <= tol) SP(1, hw, cx, cy);
            else SP(1, sqrt(cs * 0.5), fx / cs + cx, fy / cs + cy);
            i = 2;
        } else if (r <= tol) { SP(1, hw, cx, cy); SP(2, 1.0, cx, cy); SP(3, hw, cx, cy); i = 4; }
        else {
            double g = -1.0 / sqrt(cs * 2.0), mx = fx * g, my = fy * g, c2 = (my * dy + mx * dx) / (r * r) + 1.0, w = sqrt(c2 * 0.5);
            SP(1, w, (mx + dx) / c2 + cx, (my + dy) / c2 + cy); SP(2, 1.0, mx + cx, my + cy); SP(3, w, (mx + ex) / c2 + cx, (my + ey) / c2 + cy); i = 4;
        }
    }
    SP(i, 1.0, cos(a + s) * r + cx, sin(a + s) * r + cy);
#undef SP
    return 0;
}
/* make_view_k (mccore 0x180017080, decomp/mccore_make_view_k.c): rows x, y, k for the unit axis k: e = v_z_3d = (0, 0, 0) (mccore
   0x181371ca8: zero in the image, never written - decomp/surfextr/vz*.txt; decision S-3) with e[i] = 1 for the smaller |k[i]| of i = 0, 1;
   y = unit(k x e), x = unit(y x k). 0 when |k| < 0.5 after normalising (a zero axis). */
static int view_k(const double k0[3], double M[9]) {
    double l = sqrt(k0[0] * k0[0] + k0[1] * k0[1] + k0[2] * k0[2]); if (l == 0) return 0;
    double k[3] = { k0[0] / l, k0[1] / l, k0[2] / l }, e[3] = { 0, 0, 0 }, y[3], x[3];   /* e = v_z_3d = (0, 0, 0) (decision S-3) */
    e[fabs(k[1]) < fabs(k[0]) ? 1 : 0] = 1.0;
    cross3(k, e, y); l = sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]); for (int i = 0; i < 3; i++) y[i] = l ? y[i] / l : 0;
    cross3(y, k, x); l = sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]); for (int i = 0; i < 3; i++) x[i] = l ? x[i] / l : 0;
    memcpy(M, x, 24); memcpy(M + 3, y, 24); memcpy(M + 6, k, 24);
    return 1;
}
/* the profile entity as nb_type: lines and arcs as converted above; NURBS curves (class 0x80) from their record (nb_type at ent +0x68:
   +0x60 nknots, +0x64 ncp, +0x68 degree, +0x69 values, +0x6a bit 2 = non-rational; tail knots then points, rational ones (w, wx, wy, wz) -
   made Cartesian here as convert_revsurf_to_nbs reads them), parametric splines (class 8) as convert_spline_to_nb. */
static int ent_nb(const McamFile *f, const Ent *en, double tol, Nb *o) {
    memset(o, 0, sizeof *o);
    if (en->type == E_LINE) return line_nb(en->a, en->b, o);
    if (en->type == E_ARC) {
        double N[3] = { 0, 0, 1 };
        if (en->vw) { N[0] = en->vm[6]; N[1] = en->vm[7]; N[2] = en->vm[8]; }
        V3 s = mcam_arc_pt(en, en->a0), e = mcam_arc_pt(en, en->a0 + en->sw);
        arc_nb(en->c, en->r, N, s, e, en->sw, tol, o);
        for (int i = 0; i < o->ncp; i++) for (int c = 1; c < 4; c++) o->p[4 * i + c] *= o->p[4 * i];   /* nbrat_3d_to_4d */
        return 0;
    }
    if (en->type == E_SPLINE && en->cls == 0x8 && en->crv >= 0 && en->crv < f->ncrvs) {
        /* convert_spline_to_nb (mccadutil 0x180059520) -> convert_ppspline_to_nbspline (mccore 0x180261480, decomp/mccore_curves.c):
           degree = order - 1, 3 values, non-rational (flags 0x14, bit 1 = closed); knots 0, each segment break i = 0 .. nseg repeated
           degree times, nseg (count degree (nseg + 1) + 2); per segment the power-basis coefficients times FUN_18026c490's matrix = its
           Bezier points (b_i = sum_k<=i C(i,k) / C(d,k) a_k - the matrix itself is not dumped: the unique Bezier form, ours), segments
           sharing their end points (degree x nseg + 1 points). A dimension-2 record (xy, ent +0xb5) gets z = 0 (the decompile reads 3). */
        const CurveRec *c = &f->crvs[en->crv]; const uint8_t *R = c->rec;
        if (c->n < 0xc0) return -1;
        int ns = r32(R + 0xb0), ord = R[0xb4], dim = R[0xb5], d = ord - 1;
        if (ns < 1 || ord < 2 || ord > 26 || dim < 2 || dim > 3 || 0xc0 + 8 * (size_t)ns * ord * dim > c->n || d * ns + 1 > NB_MAXCP) return -1;
        o->deg = d; o->dim = 3; o->rat = 0; o->ncp = d * ns + 1; o->nk = d * (ns + 1) + 2;
        o->k = malloc(sizeof(double) * o->nk); int q = 0; o->k[q++] = 0; for (int i = 0; i <= ns; i++) for (int j = 0; j < d; j++) o->k[q++] = i; o->k[q++] = ns;
        o->p = calloc((size_t)o->ncp * 3, sizeof(double));
        double C[27][27]; for (int i = 0; i <= 26; i++) { C[i][0] = 1; for (int k = 1; k <= i; k++) C[i][k] = C[i - 1][k - 1] + (k < i ? C[i - 1][k] : 0); }
        for (int sg = 0; sg < ns; sg++) {
            const uint8_t *cf = R + 0xc0 + 8 * ((size_t)ord * dim * sg);
            for (int i = 0; i <= d; i++) for (int cc = 0; cc < 3; cc++) {
                double b = 0; if (cc < dim) for (int k = 0; k <= i; k++) b += C[i][k] / C[d][k] * rd(cf + 8 * ((size_t)k * dim + cc));
                o->p[3 * (sg * d + i) + cc] = b;
            }
        }
        return 0;
    }
    if (en->type == E_SPLINE && en->cls == 0x80 && en->crv >= 0 && en->crv < f->ncrvs) {
        const CurveRec *c = &f->crvs[en->crv]; const uint8_t *R = c->rec;
        if (c->n < 0xd8) return -1;
        int nk = r32(R + 0xc8), ncp = r32(R + 0xcc), dim = R[0xd1];
        if (nk < 2 || nk > NB_MAXCP + 30 || ncp < 2 || ncp > NB_MAXCP || (dim != 3 && dim != 4) || 0xd8 + 8 * ((size_t)nk + (size_t)dim * ncp) > c->n) return -1;
        o->nk = nk; o->ncp = ncp; o->deg = R[0xd0]; o->dim = dim; o->rat = !(R[0xd2] & 4) && dim == 4;
        o->k = malloc(sizeof(double) * nk); for (int i = 0; i < nk; i++) o->k[i] = rd(R + 0xd8 + 8 * i);
        o->p = malloc(sizeof(double) * (size_t)dim * ncp);
        for (int i = 0; i < dim * ncp; i++) o->p[i] = rd(R + 0xd8 + 8 * (nk + i));
        return 0;
    }
    return -1;
}

/* convert_revsurf_to_nbs (mccore 0x180266570, decomp/surfcreate/core_nurbs.c l.4763) + create_nurbsurf_from_surf's kind 2 tail (decomp/
   surfrev/core_conv.c): the revolved surface (axis point A, unit axis k, angles v0 .. v1) of the profile as a kind 0 record: u = the
   profile (its knots, degree, points), v = degree 2 arcs (convert_2darc_to_nurbdata in the axis frame make_view_k: each profile point's
   radius / angle about the axis, its height), the net u fastest, homogeneous (w, wx, wy, wz) (nbsrat_3d_to_4d); header flags = the
   kind 2 surface's bits 0..2 + 0x40; the range = the first / last u and v knots. Returns the record (0x118 + tail), *n its size. */
static uint8_t *rev_nbs(const Nb *pf, const double A[3], const double k[3], double v0, double v1, uint16_t fl2, double tol, uint32_t *n) {
    double M[9]; if (!view_k(k, M)) return NULL;
    double ax = M[0] * A[0] + M[1] * A[1] + M[2] * A[2], ay = M[3] * A[0] + M[4] * A[1] + M[5] * A[2], sw = v1 - v0;
    double P[36], K[12]; int ncv = 0, nkv = 0;
    int ncu = pf->ncp, nku = pf->nk;
    double (*net)[4] = NULL;
    for (int i = 0; i < ncu; i++) {
        double p[3], w = 1.0;
        if (pf->dim == 4) {                         /* (w, x, y, z): Cartesian unless w is 0 or 1 (the 1e-8 / 1e-5 test) */
            const double *q = pf->p + 4 * i; w = q[0];
            if (fabs(w) <= 1e-8 || fabs(w - 1.0) <= 1e-5) { p[0] = q[1]; p[1] = q[2]; p[2] = q[3]; }
            else { p[0] = q[1] / w; p[1] = q[2] / w; p[2] = q[3] / w; }
            if (!pf->rat) w = 1.0;
        } else { const double *q = pf->p + 3 * i; p[0] = q[0]; p[1] = q[1]; p[2] = q[2]; }
        double lx = M[0] * p[0] + M[1] * p[1] + M[2] * p[2], ly = M[3] * p[0] + M[4] * p[1] + M[5] * p[2], lz = M[6] * p[0] + M[7] * p[1] + M[8] * p[2];
        double dx = lx - ax, dy = ly - ay, r2 = dx * dx + dy * dy, r = sqrt(r2), ang = 0;
        if (r2 >= 1e-12) { ang = atan2(dy, dx); if (ang < 0) ang += 2 * M_PI; }
        arc2d_nb(ax, ay, r, ang + v0, sw, lz, tol, P, K, &ncv, &nkv);
        if (!net) net = calloc((size_t)ncu * ncv, sizeof *net);
        for (int j = 0; j < ncv; j++) {
            const double *q = P + 4 * j;
            double X = M[0] * q[1] + M[3] * q[2] + M[6] * q[3], Y = M[1] * q[1] + M[4] * q[2] + M[7] * q[3], Z = M[2] * q[1] + M[5] * q[2] + M[8] * q[3];
            double W = pf->rat ? w * q[0] : q[0];
            double *o = net[(size_t)j * ncu + i]; o[0] = W; o[1] = X * W; o[2] = Y * W; o[3] = Z * W;   /* nbsrat_3d_to_4d */
        }
    }
    if (!net) return NULL;
    *n = 0x118 + 8 * (uint32_t)(nku + nkv + 4 * ncu * ncv);
    uint8_t *R = calloc(1, *n);
    uint32_t cls = 0x100; memcpy(R, &cls, 4);
    uint8_t *S = R + 0x68;
    S[0] = 0;
    w16(S + 6, (uint16_t)((fl2 & 7) | 0x40));
    memcpy(R + 0x100, &nku, 4); memcpy(R + 0x104, &nkv, 4); memcpy(R + 0x108, &ncu, 4); memcpy(R + 0x10c, &ncv, 4);
    R[0x110] = 4; R[0x111] = (uint8_t)pf->deg; R[0x112] = 2; R[0x113] = 0;   /* rational: bit 0 clear (+0x13 & 0xf8) */
    uint8_t *T = R + 0x118;
    for (int i = 0; i < nku; i++) wdd(T + 8 * i, pf->k[i]);
    for (int j = 0; j < nkv; j++) wdd(T + 8 * (nku + j), K[j]);
    for (size_t q = 0; q < (size_t)ncu * ncv; q++) for (int c = 0; c < 4; c++) wdd(T + 8 * (nku + nkv + 4 * q + c), net[q][c]);
    free(net);
    wdd(S + 8, pf->k[0]); wdd(S + 0x10, pf->k[nku - 1]); wdd(S + 0x18, K[0]); wdd(S + 0x20, K[nkv - 1]);
    return R;
}

/* put_surf_in_db's fields (mccore 0x1802db160): +1 density = surf_density (factory 1, mccore FUN_1801b7d30), +2 shaded = 1 with shading on
   and shade-all; main_attrib at ent +0x10 (the writer's defaults, mcam_saveops.c head()); surf_closed u, v; surf_extent mode 3 (DAT_1812d9064
   set: the stored corpus records carry the curvature fields - measured, tools/tests/surfhdr_test.c); refused when a range < 1e-12 */
int mcsb_finish(const McamFile *f, uint8_t *rec, uint32_t n, int shaded, double tol) {
    uint8_t *S = rec + 0x68;
    if (rd(S + 0x10) - rd(S + 8) < 1e-12 || rd(S + 0x20) - rd(S + 0x18) < 1e-12) return -1;
    S[1] = 1; S[2] = shaded ? 1 : 0;
    wdd(rec + 0x10, 1.0); w16(rec + 0x18, 1); w16(rec + 0x1a, 1); w16(rec + 0x1c, 1);
    mcsb_closed(f, rec, n, 1); mcsb_closed(f, rec, n, 2);
    return mcsb_extent(f, rec, n, 3, tol);
}

/* create_revs (mccad 0x18008cd20, decomp/surfcreate/revs.c) for one profile entity: the end angle brought into start < end <= start +
   2 pi; the axis = the axis line's start -> end (pick t >= 0.5: end -> start, from the end), unit, negated when reversed; flags 0x40 | 2
   (end - start >= 2 pi - 1e-5) | 4 (the profile's derivative at its middle . axis > 0: eval_revsurf mode 1's +0x18 at (mid u, v0)) | 1
   (the profile's ends within tol). surface_type 2 (config 10014, factory 2): create_nurbsurf_from_surf -> rev_nbs; 0: the kind 2 record
   (D+0 axis, +0x18 point, +0x30 / +0x38 angles, +0x40 axis eptr, +0x48 profile eptr; u = curve_t1t2); 1 (parametric): NOT DECODED.
   Returns the record (malloc'd) or NULL with *err. */
uint8_t *mcsb_revolve(const McamFile *f, const Ent *pf, int64_t pf_eptr, V3 la, V3 lb, int64_t axis_eptr, double pick_t, double start,
                      double end, int reverse, int surface_type, int shaded, double tol, uint32_t *n, const char **err) {
    *err = NULL;
    while (end <= start) end += 2 * M_PI;
    while (end - start > 2 * M_PI) end -= 2 * M_PI;
    V3 P = pick_t >= 0.5 ? lb : la, Q = pick_t >= 0.5 ? la : lb;
    double k[3] = { Q.x - P.x, Q.y - P.y, Q.z - P.z }, l = sqrt(k[0] * k[0] + k[1] * k[1] + k[2] * k[2]);
    if (l == 0) { *err = "zero-length axis"; return NULL; }
    for (int i = 0; i < 3; i++) k[i] = k[i] / l * (reverse ? -1 : 1);
    double A[3] = { P.x, P.y, P.z };
    uint16_t fl = 0x40 | (end - start >= 6.283175307179587 ? 2 : 0);
    /* the profile's derivative at its middle and its ends */
    double t1 = 0, t2 = 1, d[3] = { 0, 0, 0 }; V3 e0 = pf->a, e1 = pf->b;
    if (pf->type == E_LINE) { d[0] = pf->b.x - pf->a.x; d[1] = pf->b.y - pf->a.y; d[2] = pf->b.z - pf->a.z; }
    else if (pf->type == E_ARC) {
        V3 m = mcam_arc_pt(pf, pf->a0 + pf->sw * 0.5), m2 = mcam_arc_pt(pf, pf->a0 + pf->sw * 0.5001);
        d[0] = m2.x - m.x; d[1] = m2.y - m.y; d[2] = m2.z - m.z;
        e0 = mcam_arc_pt(pf, pf->a0); e1 = mcam_arc_pt(pf, pf->a0 + pf->sw);
    } else if (pf->type == E_SPLINE && pf->crv >= 0 && pf->crv < f->ncrvs) {
        const CurveRec *c = &f->crvs[pf->crv]; mcs_spline_t1t2(c, &t1, &t2);
        double tm = (t1 + t2) * 0.5, h = (t2 - t1) * 1e-5, a[3], b[3];
        if (mcs_curve_eval(f, c, tm - h, a) || mcs_curve_eval(f, c, tm + h, b)) { *err = "the profile is not evaluated here"; return NULL; }
        for (int i = 0; i < 3; i++) d[i] = b[i] - a[i];
    } else { *err = "the profile kind is not ported"; return NULL; }
    if (d[0] * k[0] + d[1] * k[1] + d[2] * k[2] > 0) fl |= 4;
    if (sqrt((e1.x - e0.x) * (e1.x - e0.x) + (e1.y - e0.y) * (e1.y - e0.y) + (e1.z - e0.z) * (e1.z - e0.z)) < tol) fl |= 1;
    uint8_t *R = NULL;
    if (surface_type == 2) {
        Nb nb; if (ent_nb(f, pf, tol, &nb)) { *err = "the profile could not be converted"; return NULL; }
        R = rev_nbs(&nb, A, k, start, end, fl, tol, n);
        nb_free(&nb);
        if (!R) { *err = "the conversion failed"; return NULL; }
        if (pf->type == E_SPLINE) { wdd(R + 0x68 + 8, t1); wdd(R + 0x68 + 0x10, t2); }   /* create_nurbsurf_from_surf: a spline profile's range (ent +0x98 / +0xa0, classes 8 and 0x80) */
    } else if (surface_type == 0) {
        *n = 0x150; R = calloc(1, *n); uint32_t cls = 0x100; memcpy(R, &cls, 4);
        uint8_t *S = R + 0x68; S[0] = 2; w16(S + 6, fl);
        wdd(S + 8, t1); wdd(S + 0x10, t2); wdd(S + 0x18, start); wdd(S + 0x20, end);
        for (int i = 0; i < 3; i++) { wdd(R + 0x100 + 8 * i, k[i]); wdd(R + 0x118 + 8 * i, A[i]); }
        wdd(R + 0x130, start); wdd(R + 0x138, end); memcpy(R + 0x140, &axis_eptr, 8); memcpy(R + 0x148, &pf_eptr, 8);
    } else { *err = "surface type 1 (parametric, make_approx_parametric_surface) is NOT DECODED"; return NULL; }
    if (mcsb_finish(f, R, *n, shaded, tol)) { free(R); *err = "a parameter range below 1e-12"; return NULL; }
    return R;
}

/* tests (tools/tests/surfrev_test.c): a stored kind 2 surface converted as create_nurbsurf_from_surf does (its profile, axis, angles) */
uint8_t *mcsb_rev_of_kind2(const McamFile *f, const Surf *s, double tol, uint32_t *n) {
    if (s->kind != 2 || s->n < 0x150) return NULL;
    int64_t pe; memcpy(&pe, s->rec + 0x148, 8);
    const Ent *pf = NULL; for (int i = 0; i < f->nents && !pf; i++) if (f->ents[i].type && f->ents[i].eptr == pe) pf = &f->ents[i];
    if (!pf) return NULL;
    Nb nb; if (ent_nb(f, pf, tol, &nb)) return NULL;
    double k[3], A[3]; for (int i = 0; i < 3; i++) { k[i] = rd(s->rec + 0x100 + 8 * i); A[i] = rd(s->rec + 0x118 + 8 * i); }
    uint8_t *R = rev_nbs(&nb, A, k, rd(s->rec + 0x130), rd(s->rec + 0x138), r16(s->rec + 0x6e), tol, n);
    nb_free(&nb);
    if (R && pf->type == E_SPLINE) { double t1, t2; mcs_spline_t1t2(&f->crvs[pf->crv], &t1, &t2); wdd(R + 0x70, t1); wdd(R + 0x78, t2); }
    return R;
}

/* ------------------------------------------------------------------------------------------------ Draft (kind 6) */
/* FUN_180126b90's surface (mccad, decomp/surfextr/extr.c l.1959; formats/surfaces_tab_plan.md "C. Draft"): the kind 6 surf_type
   +6 = fl (bit 0 the curve closed, bit 2 the reversed / negative-length bit, | 0x40), u = curve_t1t2, v 0 .. 1, D (+0x98) = N (3
   doubles), +0x18 d, +0x20 the curve eptr. surface_type (config 10014) 2 = create_nurbsurf_from_surf (mccore 0x180268750, decomp/
   surfcreate/core_nurbs.c l.3): FUN_180266ca0 case 6 -> FUN_1802641d0 (l.2010) picks the exact conversion FUN_180266ed0 (l.3556) for
   |d| <= 1e-5, a line, or an arc whose view z is parallel to unit(N) (|dot| >= 0.99999); every other curve (a spline or a skew arc
   with a draft angle) goes to the approximation FUN_1802647b0 (FUN_1802699a0 / FUN_18026a290 / pt_tan_to_sp_coefs / proj_pt_on_coeff,
   decomp/surfextr/core_draft.c) - NOT PORTED (refused). 0 = the kind 6 record itself; 1 = make_approx_parametric_surface (NOT DECODED).
   FUN_180266ed0: u = the curve's nb_type (its knots / degree / values, +0x13 bit 0 = its non-rational flag), v = degree 1 (knots 0 0 1
   1); row 0 = the curve's control points; row 1: no draft (|d| <= 1e-5) each point + N (rational and |w - 1| > 1e-5: (P / w + N) w);
   with a draft L = |N|, n = N / L, p = unit(C'(t1) x n) (eval_curve mode 1 at t1; all components < 1e-12: v_z_3d = 0), s = -d L (|d| >=
   1e12: s = 0 and L negated when d < 0): a line -> its ends + N + s p (vertical: + L p); an arc -> the arc of radius r + s (vertical:
   r + L), sign reversed when (centre - C(t1)) . p > 1e-5, at the depth z + (N in the arc's view).z (vertical: z), its nb_type
   (convert_arc_to_nb + nbrat_3d_to_4d). Header (the create_nurbsurf_from_surf tail): +6 = (fl & 7) | 0x40, the range = the first /
   last knots, a spline curve's u = its curve_t1t2 (ent +0x98 / +0xa0). Ours: C'(t1) of a spline by a forward difference (only its
   direction is used). */
uint8_t *mcsb_draft(const McamFile *f, const Ent *cv, int64_t cv_eptr, const double N[3], double d, uint16_t fl, int surface_type,
                    int shaded, double tol, uint32_t *n, const char **err) {
    *err = NULL;
    double t1 = 0, t2 = 1;
    const CurveRec *cr = NULL;
    if (cv->type == E_SPLINE) { if (cv->crv < 0 || cv->crv >= f->ncrvs) { *err = "the curve record is missing"; return NULL; } cr = &f->crvs[cv->crv]; mcs_spline_t1t2(cr, &t1, &t2); }
    else if (cv->type != E_LINE && cv->type != E_ARC) { *err = "the entity is not a line, arc or spline"; return NULL; }
    uint8_t *R = NULL;
    if (surface_type == 0) {
        *n = 0x150; R = calloc(1, *n); uint32_t cls = 0x100; memcpy(R, &cls, 4);
        uint8_t *S = R + 0x68; S[0] = 6; w16(S + 6, fl);
        wdd(S + 8, t1); wdd(S + 0x10, t2); wdd(S + 0x18, 0.0); wdd(S + 0x20, 1.0);
        for (int i = 0; i < 3; i++) wdd(R + 0x100 + 8 * i, N[i]);
        wdd(R + 0x118, d); memcpy(R + 0x120, &cv_eptr, 8);
    } else if (surface_type == 2) {
        double L = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]); if (!(L > 0)) { *err = "a zero draft vector"; return NULL; }
        double un[3] = { N[0] / L, N[1] / L, N[2] / L };
        double vz[3] = { 0, 0, 1 }; if (cv->type == E_ARC && cv->vw) { vz[0] = cv->vm[6]; vz[1] = cv->vm[7]; vz[2] = cv->vm[8]; }
        int exact = 1;                                    /* FUN_1802641d0 */
        if (fabs(d) > 1e-5) {
            if (cv->type == E_SPLINE) exact = 0;
            else if (cv->type == E_ARC && fabs(un[0] * vz[0] + un[1] * vz[1] + un[2] * vz[2]) < 0.99999) exact = 0;
        }
        if (!exact) { *err = "a drafted spline / skew arc (the approximation FUN_1802647b0) is NOT PORTED"; return NULL; }
        Nb c; if (ent_nb(f, cv, tol, &c)) { *err = "the curve could not be converted"; return NULL; }
        int nonrat = cv->type == E_LINE || (cv->type == E_SPLINE && (cv->cls == 0x8 || (c.dim == 4 ? !c.rat : 1)));
        if (c.dim == 4 && nonrat) { nb_free(&c); *err = "a non-rational 4-value NURBS curve is NOT PORTED"; return NULL; }
        int dim = c.dim, ncu = c.ncp, nku = c.nk;
        double *row1 = malloc(sizeof(double) * (size_t)dim * ncu);
        if (fabs(d) <= 1e-5) {
            for (int i = 0; i < ncu; i++) {
                const double *q = c.p + (size_t)dim * i; double *o = row1 + (size_t)dim * i;
                if (dim == 3) { for (int k = 0; k < 3; k++) o[k] = q[k] + N[k]; continue; }
                double w = q[0]; o[0] = w;
                for (int k = 0; k < 3; k++) o[1 + k] = (nonrat || fabs(w - 1.0) <= 1e-5) ? q[1 + k] + N[k] : (q[1 + k] / w + N[k]) * w;
            }
        } else {
            int vert = !(fabs(d) < 1e12);
            double s5 = L, s6 = 0; if (vert) { if (-d > 0) s5 = -L; } else s6 = -d * L;
            double P0[3], D1[3];
            if (cv->type == E_LINE) { P0[0] = cv->a.x; P0[1] = cv->a.y; P0[2] = cv->a.z; D1[0] = cv->b.x - cv->a.x; D1[1] = cv->b.y - cv->a.y; D1[2] = cv->b.z - cv->a.z; }
            else {                                       /* the arc at t1: its start, d/dt (a0 + t sw) */
                V3 s = mcam_arc_pt(cv, cv->a0); P0[0] = s.x; P0[1] = s.y; P0[2] = s.z;
                double ux[3] = { 1, 0, 0 }, uy[3] = { 0, 1, 0 }; if (cv->vw) for (int k = 0; k < 3; k++) { ux[k] = cv->vm[k]; uy[k] = cv->vm[3 + k]; }
                for (int k = 0; k < 3; k++) D1[k] = cv->sw * cv->r * (-sin(cv->a0) * ux[k] + cos(cv->a0) * uy[k]);
            }
            double p[3]; cross3(D1, un, p);
            int deg = fabs(p[0]) < 1e-12 && fabs(p[1]) < 1e-12 && fabs(p[2]) < 1e-12;
            double q[3];
            if (!deg) { double l = sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]); for (int k = 0; k < 3; k++) p[k] /= l;
                        if (cv->type == E_ARC) { q[0] = cv->c.x - P0[0]; q[1] = cv->c.y - P0[1]; q[2] = cv->c.z - P0[2]; } else memcpy(q, p, sizeof q); }
            else { p[0] = 0; p[1] = 0; p[2] = 0; memcpy(q, p, sizeof q); }   /* v_z_3d = (0, 0, 0): mccore 0x181371ca8 is zero and never written (decomp/surfextr/vz*.txt) */
            if (cv->type == E_LINE) {
                for (int k = 0; k < 3; k++) {
                    double a = k == 0 ? cv->a.x : k == 1 ? cv->a.y : cv->a.z, b = k == 0 ? cv->b.x : k == 1 ? cv->b.y : cv->b.z;
                    row1[k] = vert ? s5 * p[k] + a : s6 * p[k] + N[k] + a; row1[3 + k] = vert ? s5 * p[k] + b : b + N[k] + s6 * p[k];
                }
            } else {
                double s = vert ? s5 : s6;
                if (q[0] * p[0] + q[1] * p[1] + q[2] * p[2] > 1e-5) s = -s;
                double dz = vert ? 0 : N[0] * vz[0] + N[1] * vz[1] + N[2] * vz[2];
                Ent a2 = *cv; a2.r = cv->r + s; a2.c.x = cv->c.x + dz * vz[0]; a2.c.y = cv->c.y + dz * vz[1]; a2.c.z = cv->c.z + dz * vz[2];
                Nb c2; V3 s0 = mcam_arc_pt(&a2, a2.a0), e0 = mcam_arc_pt(&a2, a2.a0 + a2.sw);
                arc_nb(a2.c, a2.r, vz, s0, e0, a2.sw, tol, &c2);
                for (int i = 0; i < c2.ncp; i++) for (int k = 1; k < 4; k++) c2.p[4 * i + k] *= c2.p[4 * i];   /* nbrat_3d_to_4d */
                if (c2.ncp != ncu) { nb_free(&c2); nb_free(&c); free(row1); *err = "the offset arc's structure differs"; return NULL; }
                memcpy(row1, c2.p, sizeof(double) * 4 * ncu); nb_free(&c2);
            }
        }
        *n = 0x118 + 8 * (uint32_t)(nku + 4 + dim * ncu * 2);
        R = calloc(1, *n); uint32_t cls = 0x100; memcpy(R, &cls, 4);
        uint8_t *S = R + 0x68; S[0] = 0; w16(S + 6, (uint16_t)((fl & 7) | 0x40));
        int nkv = 4, ncv = 2;
        memcpy(R + 0x100, &nku, 4); memcpy(R + 0x104, &nkv, 4); memcpy(R + 0x108, &ncu, 4); memcpy(R + 0x10c, &ncv, 4);
        R[0x110] = (uint8_t)dim; R[0x111] = (uint8_t)c.deg; R[0x112] = 1; R[0x113] = nonrat ? 1 : 0;
        uint8_t *T = R + 0x118;
        for (int i = 0; i < nku; i++) wdd(T + 8 * i, c.k[i]);
        static const double KV[4] = { 0, 0, 1, 1 };
        for (int j = 0; j < 4; j++) wdd(T + 8 * (nku + j), KV[j]);
        T += 8 * (nku + 4);
        for (int i = 0; i < dim * ncu; i++) { wdd(T + 8 * i, c.p[i]); wdd(T + 8 * (dim * ncu + i), row1[i]); }
        wdd(S + 8, c.k[0]); wdd(S + 0x10, c.k[nku - 1]); wdd(S + 0x18, 0.0); wdd(S + 0x20, 1.0);
        if (cv->type == E_SPLINE) { wdd(S + 8, t1); wdd(S + 0x10, t2); }
        nb_free(&c); free(row1);
    } else { *err = "surface type 1 (parametric, make_approx_parametric_surface) is NOT DECODED"; return NULL; }
    if (mcsb_finish(f, R, *n, shaded, tol)) { free(R); *err = "a parameter range below 1e-12"; return NULL; }
    return R;
}

/* ------------------------------------------------------------------------------------------------ Extrude: an arc wall */
/* FUN_18001ddd0 (mccad, decomp/surfextr/extr.c l.5126): the bottom and the top arc (centres C0 / C1 in the frame x, y, z, radii r0 /
   r1, each its own start / sweep: FUN_180018690 copies the node's) each convert_arc_to_nb + nbrat_3d_to_4d; the kind 0 record: u = the arc (its knots, degree 2, 4 values,
   rational: +0x13 & 0xf8), v = degree 1, knots 0 0 1 1, row 0 = the bottom arc's points, row 1 = the top's (the bottom's count: they
   must match here); the range u 0 .. the last u knot, v 0 .. the last v knot; +6 = 0x40 | (flip & 1) << 2. */
uint8_t *mcsb_arc_wall(const McamFile *f, const double C0[3], const double C1[3], const double x[3], const double y[3], const double z[3],
                       double r0, double r1, double st0, double sw0, double st1, double sw1, int flip, int shaded, double tol, uint32_t *n) {
    Nb a[2]; const double *C[2] = { C0, C1 }; double r[2] = { r0, r1 }, sts[2] = { st0, st1 }, sws[2] = { sw0, sw1 };
    for (int i = 0; i < 2; i++) {
        V3 c = { C[i][0], C[i][1], C[i][2] }, s, e; double st = sts[i], sw = sws[i];
        s.x = c.x + r[i] * (cos(st) * x[0] + sin(st) * y[0]); s.y = c.y + r[i] * (cos(st) * x[1] + sin(st) * y[1]); s.z = c.z + r[i] * (cos(st) * x[2] + sin(st) * y[2]);
        e.x = c.x + r[i] * (cos(st + sw) * x[0] + sin(st + sw) * y[0]); e.y = c.y + r[i] * (cos(st + sw) * x[1] + sin(st + sw) * y[1]); e.z = c.z + r[i] * (cos(st + sw) * x[2] + sin(st + sw) * y[2]);
        arc_nb(c, r[i], z, s, e, sw, tol, &a[i]);
        for (int k = 0; k < a[i].ncp; k++) for (int j = 1; j < 4; j++) a[i].p[4 * k + j] *= a[i].p[4 * k];   /* nbrat_3d_to_4d */
    }
    if (a[0].ncp != a[1].ncp) { nb_free(&a[0]); nb_free(&a[1]); return NULL; }
    int nku = a[0].nk, ncu = a[0].ncp, nkv = 4, ncv = 2;
    *n = 0x118 + 8 * (uint32_t)(nku + nkv + 4 * ncu * ncv);
    uint8_t *R = calloc(1, *n); uint32_t cls = 0x100; memcpy(R, &cls, 4);
    uint8_t *S = R + 0x68; S[0] = 0; w16(S + 6, (uint16_t)(0x40 | ((flip & 1) << 2)));
    memcpy(R + 0x100, &nku, 4); memcpy(R + 0x104, &nkv, 4); memcpy(R + 0x108, &ncu, 4); memcpy(R + 0x10c, &ncv, 4);
    R[0x110] = 4; R[0x111] = (uint8_t)a[0].deg; R[0x112] = 1; R[0x113] = 0;
    uint8_t *T = R + 0x118; static const double KV[4] = { 0, 0, 1, 1 };
    for (int i = 0; i < nku; i++) wdd(T + 8 * i, a[0].k[i]);
    for (int j = 0; j < 4; j++) wdd(T + 8 * (nku + j), KV[j]);
    T += 8 * (nku + nkv);
    for (int i = 0; i < 4 * ncu; i++) { wdd(T + 8 * i, a[0].p[i]); wdd(T + 8 * (4 * ncu + i), a[1].p[i]); }
    wdd(S + 8, 0.0); wdd(S + 0x10, a[0].k[nku - 1]); wdd(S + 0x18, 0.0); wdd(S + 0x20, 1.0);
    nb_free(&a[0]); nb_free(&a[1]);
    if (mcsb_finish(f, R, *n, shaded, tol)) { free(R); return NULL; }
    return R;
}

/* tests: surf_extent's grid of a record with each point's FUN_1806fa880 u value (tools/tests/surfhdr_test.c PROBE=) */
void mcsb_probe(const McamFile *f, const uint8_t *rec0, uint32_t n) {
    uint8_t *rec = malloc(n); memcpy(rec, rec0, n); uint8_t *S = rec + 0x68;
    int nu, nv; grid_n(f, rec, n, &nu, &nv);
    if (getenv("KNOTU")) {   /* KNOTU=i: cu at u = knot i +- 1e-9, v = v0 */
        int ki = atoi(getenv("KNOTU")); double k = rd(rec + 0x118 + 8 * ki);
        for (int sg = -1; sg <= 1; sg++) { double o[6][3], x, y; if (!ev(f, rec, n, 2, k + sg * 1e-9, rd(S + 0x18), o)) { curv_uv(o, &x, &y); printf("  u %.12g cu %.10g\n", k + sg * 1e-9, x); } }
        printf("  u0 + du = %.17g, knot %.17g\n", rd(S + 8) + (rd(S + 0x10) - rd(S + 8)) / nu, k);
        free(rec); return;
    }
    if (getenv("GRID")) {   /* GRID=1: the max cu for every grid nu, nv in 2..40 that reaches the stored value within 1e-6 relative */
        double st = rd(rec0 + 0x68 + 0x78);
        for (int a = 2; a <= 40; a++) for (int b = 2; b <= 40; b++) {
            double U0 = rd(S + 8), U1 = rd(S + 0x10), V0 = rd(S + 0x18), V1 = rd(S + 0x20), m = 0; int eu = 0;
            for (double u = U0; !eu; u += (U1 - U0) / a) { if (u >= U1) { eu = 1; u = U1; } int evv = 0;
                for (double v = V0; !evv; v += (V1 - V0) / b) { if (v >= V1) { evv = 1; v = V1; } double o[6][3], x, y; if (ev(f, rec, n, 2, u, v, o)) continue; curv_uv(o, &x, &y); if (x >= m) m = x; } }
            if (fabs(m - st) <= 1e-6 * st) printf("  grid %d x %d: max cu %.10g (stored %.10g)\n", a, b, m, st);
        }
        free(rec); return;
    }
    double u0 = rd(S + 8), u1 = rd(S + 0x10), v0 = rd(S + 0x18), v1 = rd(S + 0x20), du = (u1 - u0) / nu, dv = (v1 - v0) / nv;
    printf("kind %d nu %d nv %d range %.17g %.17g %.17g %.17g", S[0], nu, nv, u0, u1, v0, v1);
    if (S[0] == 0) { printf(" deg %d %d ncp %d %d knots u:", rec[0x111], rec[0x112], r32(rec + 0x108), r32(rec + 0x10c));
        for (int i = 0; i < r32(rec + 0x100); i++) printf(" %g", rd(rec + 0x118 + 8 * i)); }
    printf("\n");
    int endu = 0;
    for (double u = u0; !endu; u += du) { if (u >= u1) { endu = 1; u = u1; } int endv = 0;
        for (double v = v0; !endv; v += dv) { if (v >= v1) { endv = 1; v = v1; } double o[6][3], a, b; if (ev(f, rec, n, 2, u, v, o)) continue; curv_uv(o, &a, &b);
            if (getenv("PK1")) { double k1, k2; curv_geom(o, &k1, &k2); double N[3]; cross3(o[1], o[2], N);
                if (k1 > 1 || -k2 > 1) printf("  u %.6f v %.6f k1 %.6g k2 %.6g |N| %.3g |Su| %.3g |Sv| %.3g\n", u, v, k1, k2, sqrt(N[0]*N[0]+N[1]*N[1]+N[2]*N[2]), sqrt(o[1][0]*o[1][0]+o[1][1]*o[1][1]+o[1][2]*o[1][2]), sqrt(o[2][0]*o[2][0]+o[2][1]*o[2][1]+o[2][2]*o[2][2])); continue; }
            if (a > 0.5 * rd(rec0 + 0x68 + 0x78)) printf("  u %.6f v %.6f cu %.6g cv %.6g |Su| %.6g\n", u, v, a, b, sqrt(o[1][0]*o[1][0]+o[1][1]*o[1][1]+o[1][2]*o[1][2])); } }
    free(rec);
}

/* tests: an entity's nb_type (ent_nb) against mcs_curve_eval at 200 parameters (the nb's knot range mapped onto curve_t1t2); the
   largest distance, -1 when not converted */
double mcsb_nb_check(const McamFile *f, const Ent *en) {
    Nb nb; if (ent_nb(f, en, 5e-5, &nb)) return -1;
    double t1 = 0, t2 = 1; const CurveRec *c = en->crv >= 0 && en->crv < f->ncrvs ? &f->crvs[en->crv] : NULL;
    if (c) mcs_spline_t1t2(c, &t1, &t2);
    double worst = 0;
    for (int s = 0; s <= 200; s++) {
        double fr = s / 200.0, t = t1 + fr * (t2 - t1), u = en->cls == 0x80 ? t : nb.k[nb.deg] + fr * (nb.k[nb.ncp] - nb.k[nb.deg]), p[3], q[4] = { 0, 0, 0, 0 };
        if (c && mcs_curve_eval(f, c, t, p)) { nb_free(&nb); return -1; }
        int sp = nurb_interval(u, nb.k, nb.nk, nb.deg); double N[3][26]; basis_ders(nb.k, sp, nb.deg, u, N);
        for (int i = 0; i <= nb.deg; i++) { const double *P = nb.p + nb.dim * (sp - nb.deg + i); double w = nb.dim == 4 ? P[0] : 1;
            for (int k = 0; k < 3; k++) q[k] += N[0][i] * (nb.dim == 4 ? P[1 + k] : P[k]); q[3] += N[0][i] * w; }
        for (int k = 0; k < 3; k++) q[k] /= q[3];
        double d = sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2])); if (d > worst) worst = d;
    }
    nb_free(&nb); return worst;
}

/* ------------------------------------------------------------------------------------------------ Transform */
/* xform_store_ent's surface branch (mccore FUN_1808db320, decomp/xform/surf_store.c) for one record: kind 0 FUN_1808d7c00 (every control
   point through the matrix as a Cartesian point - rational ones divided by w, transformed, multiplied back), kind 1 FUN_1808d8570 (each
   patch's constant coefficient through the whole matrix, the others through its linear part: [v power][u power][dim]), kind 10
   FUN_1808dbac0 (the caller transformed the base: new_base is its new eptr; the uv points unchanged). xform_type (mccoreres 3916-3919):
   0 mirror (surf_flip_normal), 1 rotate, 2 / 4 scale, 3 translate. Then FUN_1808db6e0: types 2 / 3 move the stored box corners and
   centre through the matrix (type 2 also: radius and +0x78 / +0x80 x |s|, +0x88 / +0x90 / |s|, s = XfScaleFromMatrix - here the first
   column's length), other types surf_extent(1). Kinds 2 / 4 / 6 / 7 / 8 transform their curves (FUN_1808d9950, FUN_1808d7220,
   FUN_1808db920, FUN_1808d6f80, FUN_1808d8280): NOT PORTED - NULL. m: rows (world = m . p + t). */
static void xf_p(const double m[9], const double t[3], const double p[3], double o[3]) {
    for (int r = 0; r < 3; r++) o[r] = m[3 * r] * p[0] + m[3 * r + 1] * p[1] + m[3 * r + 2] * p[2] + (t ? t[r] : 0);
}
uint8_t *mcsb_xform(const McamFile *f, const uint8_t *rec0, uint32_t n, const double m[9], const double t[3], int type, int64_t new_base) {
    if (n < 0x100) return NULL;
    uint8_t *rec = malloc(n); memcpy(rec, rec0, n); uint8_t *S = rec + 0x68;
    if (S[0] == 0) {
        if (n < 0x118) { free(rec); return NULL; }
        int nku = r32(rec + 0x100), nkv = r32(rec + 0x104), ncu = r32(rec + 0x108), ncv = r32(rec + 0x10c), vpp = rec[0x110], rat = !(rec[0x113] & 1);
        size_t base = 0x118 + 8 * (size_t)(nku + nkv);
        if ((vpp != 3 && vpp != 4) || base + 8 * (size_t)vpp * ncu * ncv > n) { free(rec); return NULL; }
        for (size_t q = 0; q < (size_t)ncu * ncv; q++) {
            uint8_t *c = rec + base + 8 * vpp * q; double p[3], o[3];
            if (vpp == 4) {
                double w = rd(c); if (!rat) w = 1;
                for (int k = 0; k < 3; k++) p[k] = rat ? rd(c + 8 * (1 + k)) / w : rd(c + 8 * (1 + k));
                xf_p(m, t, p, o); for (int k = 0; k < 3; k++) wdd(c + 8 * (1 + k), rat ? o[k] * w : o[k]);
            } else { for (int k = 0; k < 3; k++) p[k] = rd(c + 8 * k); xf_p(m, t, p, o); for (int k = 0; k < 3; k++) wdd(c + 8 * k, o[k]); }
        }
    } else if (S[0] == 1) {
        int nu = r32(rec + 0x100), nv = r32(rec + 0x104), ou = rec[0x108], ov = rec[0x109], dim = rec[0x10a];
        if (dim < 3 || 0x110 + 8 * (size_t)nu * nv * ou * ov * dim > n) { free(rec); return NULL; }
        uint8_t *c = rec + 0x110;
        for (int pt = 0; pt < nu * nv; pt++) for (int iv = 0; iv < ov; iv++) for (int iu = 0; iu < ou; iu++, c += 8 * dim) {
            double p[3] = { rd(c), rd(c + 8), rd(c + 16) }, o[3]; xf_p(m, iu == 0 && iv == 0 ? t : NULL, p, o);
            for (int k = 0; k < 3; k++) wdd(c + 8 * k, o[k]);
        }
    } else if (S[0] == 10) { memcpy(rec + 0x100, &new_base, 8); }
    else { free(rec); return NULL; }
    if (type == 0) S[6] ^= 4;                          /* surf_flip_normal */
    if (type == 2 || type == 3) {
        for (int c = 0; c < 3; c++) { double p[3] = { rd(S + 0x28 + 0x18 * c), rd(S + 0x30 + 0x18 * c), rd(S + 0x38 + 0x18 * c) }, o[3]; xf_p(m, t, p, o);
            for (int k = 0; k < 3; k++) wdd(S + 0x28 + 0x18 * c + 8 * k, o[k]); }
        if (type == 2) {
            double s = sqrt(m[0] * m[0] + m[3] * m[3] + m[6] * m[6]);
            wdd(S + 0x70, rd(S + 0x70) * s); wdd(S + 0x78, rd(S + 0x78) * s); wdd(S + 0x80, rd(S + 0x80) * s);
            if (s > 0) { wdd(S + 0x88, rd(S + 0x88) / s); wdd(S + 0x90, rd(S + 0x90) / s); }
        }
    } else if (mcsb_extent(f, rec, n, 1, 0)) { free(rec); return NULL; }
    return rec;
}

/* ------------------------------------------------------------------------------------------------ Surfaces > Simple (faces of a solid) */
/* ps_face_to_mc_surf (mcgeomsld 0x180045200, decomp/surfprim/sb2s.c) for one face of the solid occt_prim.cpp built: create_nurb_surf
   (kind 0; rational: homogeneous w, wx, wy, wz, u fastest; the face's sense against the surface: +0x6e bit 4), linearize the trimming
   curves (FUN_18003d840: create_uv_nurb_curve + linearize_uv_nurb_to_surf), then FUN_18003da10 (check_untrimmed / create_trim_from_uvs). */
uint8_t *mcsb_prim_nurbs(const McamFile *f, const void *face, int shaded, double tol, uint32_t *n) {
    const OpFace *o = face; int vpp = o->rat ? 4 : 3;
    *n = 0x118 + 8 * (uint32_t)(o->nku + o->nkv + vpp * o->ncu * o->ncv);
    uint8_t *R = calloc(1, *n); uint32_t cls = 0x100; memcpy(R, &cls, 4);
    uint8_t *S = R + 0x68; S[0] = 0;
    w16(S + 6, (uint16_t)(0x40 | (o->reversed ? 4 : 0)));
    memcpy(R + 0x100, &o->nku, 4); memcpy(R + 0x104, &o->nkv, 4); memcpy(R + 0x108, &o->ncu, 4); memcpy(R + 0x10c, &o->ncv, 4);
    R[0x110] = (uint8_t)vpp; R[0x111] = (uint8_t)o->udeg; R[0x112] = (uint8_t)o->vdeg; R[0x113] = o->rat ? 0 : 1;
    uint8_t *T = R + 0x118;
    for (int i = 0; i < o->nku; i++) wdd(T + 8 * i, o->ku[i]);
    for (int j = 0; j < o->nkv; j++) wdd(T + 8 * (o->nku + j), o->kv[j]);
    for (size_t q = 0; q < (size_t)vpp * o->ncu * o->ncv; q++) wdd(T + 8 * (o->nku + o->nkv + q), o->cp[q]);
    wdd(S + 8, o->ku[o->udeg]); wdd(S + 0x10, o->ku[o->ncu]); wdd(S + 0x18, o->kv[o->vdeg]); wdd(S + 0x20, o->kv[o->ncv]);
    if (mcsb_finish(f, R, *n, shaded, tol)) { free(R); return NULL; }
    return R;
}
/* create_uv_nurb_curve (mccore 0x180262270, decomp/surfprim/storetrim.c): a class 0x80 record - +0xc8 nk, +0xcc nc, +0xd0 degree,
   +0xd1 values per point (2: u, v with +0xd2 bit 2 = non-rational; 3: w, w u, w v), +0x98 / +0xa0 the range (FUN_18003d840: the
   spcurve's interval = here the knot range), +0x68 / +0x80 the end points */
static uint8_t *uv_curve_rec(const OpUv *c, uint32_t *n) {
    int vpp = c->rat ? 3 : 2;
    *n = 0xd8 + 8 * (uint32_t)(c->nk + vpp * c->nc);
    uint8_t *r = calloc(1, *n); uint32_t cls = 0x80; memcpy(r, &cls, 4);
    memcpy(r + 0xc8, &c->nk, 4); memcpy(r + 0xcc, &c->nc, 4); r[0xd0] = (uint8_t)c->deg; r[0xd1] = (uint8_t)vpp; r[0xd2] = c->rat ? 0 : 4;
    for (int i = 0; i < c->nk; i++) wdd(r + 0xd8 + 8 * i, c->k[i]);
    for (int i = 0; i < c->nc; i++) {
        const double *p = c->cp + (c->rat ? 3 : 2) * i; uint8_t *q = r + 0xd8 + 8 * (c->nk + vpp * i);
        if (c->rat) { wdd(q, p[2]); wdd(q + 8, p[0] * p[2]); wdd(q + 16, p[1] * p[2]); } else { wdd(q, p[0]); wdd(q + 8, p[1]); }
    }
    wdd(r + 0x98, c->k[c->deg]); wdd(r + 0xa0, c->k[c->nc]);
    CurveRec cr = { 0, 0x80, r, *n }; double p[3];
    if (!mcs_curve_eval(NULL, &cr, c->k[c->deg], p)) { wdd(r + 0x68, p[0]); wdd(r + 0x70, p[1]); }
    if (!mcs_curve_eval(NULL, &cr, c->k[c->nc], p)) { wdd(r + 0x80, p[0]); wdd(r + 0x88, p[1]); }
    return r;
}
/* Transform > Roll (do_xform_roll's FUN_18002f170, formats/wireframe.md "Roll - port spec"): an arc as convert_entity_to_nurbs ->
   convert_arc_to_nb + nbrat_3d_to_4d (ent_nb above) in a class 0x80 record (+0xc8 nk, +0xcc ncp, +0xd0 degree, +0xd1 = 4 values per
   point (w, wx, wy, wz), +0x98 / +0xa0 the knot range, +0x68 / +0x80 the end points), for brk_spline_const_err */
uint8_t *mcsb_arc_curve_rec(const McamFile *f, const Ent *en, double tol, uint32_t *n) {
    Nb o; if (en->type != E_ARC || ent_nb(f, en, tol, &o)) return NULL;
    *n = 0xd8 + 8 * (uint32_t)(o.nk + 4 * o.ncp);
    uint8_t *r = calloc(1, *n); uint32_t cls = 0x80; memcpy(r, &cls, 4);
    memcpy(r + 0xc8, &o.nk, 4); memcpy(r + 0xcc, &o.ncp, 4); r[0xd0] = (uint8_t)o.deg; r[0xd1] = 4; r[0xd2] = 0;
    for (int i = 0; i < o.nk; i++) wdd(r + 0xd8 + 8 * i, o.k[i]);
    for (int i = 0; i < 4 * o.ncp; i++) wdd(r + 0xd8 + 8 * (o.nk + i), o.p[i]);
    wdd(r + 0x98, o.k[o.deg]); wdd(r + 0xa0, o.k[o.ncp]);
    V3 s = mcam_arc_pt(en, en->a0), e = mcam_arc_pt(en, en->a0 + en->sw);
    wdd(r + 0x68, s.x); wdd(r + 0x70, s.y); wdd(r + 0x78, s.z); wdd(r + 0x80, e.x); wdd(r + 0x88, e.y); wdd(r + 0x90, e.z);
    free(o.k); free(o.p);
    return r;
}
/* ------------------------------------------------------------------------------------------------ Transform of spline curves */
/* xform_pt (mccore 0x1808eba20): p' = m p + t (the x_matrix rows transposed: m row-major as cui_xform.c's XM) */
static void xpt(const double m[9], const double t[3], double p[3], int lin) {
    double q[3] = { m[0] * p[0] + m[1] * p[1] + m[2] * p[2], m[3] * p[0] + m[4] * p[1] + m[5] * p[2], m[6] * p[0] + m[7] * p[1] + m[8] * p[2] };
    for (int i = 0; i < 3; i++) p[i] = q[i] + (lin ? 0 : t[i]);
}
/* xform_store_ent's curve cases (decomp/xform/mccore_xform.c l.1182 / l.1309 / l.1319) on a class 8 / 0x80 record (malloc'd copy; NULL =
   not transformed). type = the xform_type (DAT_18143d5d0), fac = DAT_18143d668 (FUN_1808d8820's param_11 times itself per pass: the
   Scale dialog's factor / the units ratio).
   - NURBS FUN_1808d7f00 (l.3689): only when the values per point (+0x69) are 4 for a rational (+0x6a bit 2 clear) / 3 for a polynomial
     curve - else unchanged (its 0 return is not checked: the copy is stored as it was); the stored start / end (+0x68 / +0x80) and every
     control point through xform_pt (rational: (w, wx, wy, wz) divided by w, transformed, multiplied back); scale (type 2): +0xa8
     (nb_type +0x40, put_nurbs_in_db's curvature maximum) x |fac|; the knots copied.
   - parametric spline FUN_1808da030 (l.4109): every segment's (+0xb0) coefficient vectors (order +0xb4, stride +0xb5 values): the
     constant one through the whole xform_pt, the others through its linear part (the matrix with no translation); scale: +0xa8 x |fac|.
     xform_pt reads / writes three values at the stride, so only a dimension-3 record is taken here (2: NOT PORTED - NULL). The
     start / end: the decompile transforms them in place (xform_pt(param_1), (param_1 + 0x18)) and then writes back the copy taken
     before (local_2990..) - whether the stored ends end up transformed is NOT DECODED (ours: transformed, = the curve's own ends). */
uint8_t *mcsb_xform_curve(const uint8_t *rec0, uint32_t n, const double m[9], const double t[3], int type, double fac) {
    if (n < 0xc0) return NULL;
    uint32_t cls; memcpy(&cls, rec0, 4);
    uint8_t *r = malloc(n); if (!r) return NULL; memcpy(r, rec0, n);
    double p[3];
    if (cls == 0x80) {
        if (n < 0xd8) { free(r); return NULL; }
        int nk = r32(r + 0xc8), nc = r32(r + 0xcc), vpp = r[0xd1], rat = !(r[0xd2] & 4);
        if (nk < 0 || nc < 0 || 0xd8 + 8 * ((size_t)nk + (size_t)vpp * nc) > n) { free(r); return NULL; }
        if (vpp != (rat ? 4 : 3)) return r;                     /* FUN_1808d7f00 returns 0: unchanged */
        for (int e = 0; e < 2; e++) { for (int i = 0; i < 3; i++) p[i] = rd(r + 0x68 + 0x18 * e + 8 * i); xpt(m, t, p, 0); for (int i = 0; i < 3; i++) wdd(r + 0x68 + 0x18 * e + 8 * i, p[i]); }
        uint8_t *C = r + 0xd8 + 8 * (size_t)nk;
        for (int k = 0; k < nc; k++) {
            uint8_t *q = C + 8 * (size_t)vpp * k; double w = rat ? rd(q) : 1.0; const uint8_t *v = q + (rat ? 8 : 0);
            for (int i = 0; i < 3; i++) p[i] = rat ? rd(v + 8 * i) / w : rd(v + 8 * i);
            xpt(m, t, p, 0);
            for (int i = 0; i < 3; i++) wdd(q + (rat ? 8 : 0) + 8 * i, rat ? w * p[i] : p[i]);
        }
        if (type == 2) wdd(r + 0xa8, rd(r + 0xa8) * fabs(fac));
        return r;
    }
    if (cls == 0x8) {
        int ns = r32(r + 0xb0), ord = r[0xb4], dim = r[0xb5];
        if (dim != 3 || ns < 0 || 0xc0 + 8 * (size_t)ns * ord * dim > n) { free(r); return NULL; }
        for (int e = 0; e < 2; e++) { for (int i = 0; i < 3; i++) p[i] = rd(r + 0x68 + 0x18 * e + 8 * i); xpt(m, t, p, 0); for (int i = 0; i < 3; i++) wdd(r + 0x68 + 0x18 * e + 8 * i, p[i]); }
        for (int s = 0; s < ns; s++) for (int k = 0; k < ord; k++) {
            uint8_t *q = r + 0xc0 + 8 * ((size_t)ord * dim * s + (size_t)k * dim);
            for (int i = 0; i < 3; i++) p[i] = rd(q + 8 * i);
            xpt(m, t, p, k != 0);
            for (int i = 0; i < 3; i++) wdd(q + 8 * i, p[i]);
        }
        if (type == 2) wdd(r + 0xa8, rd(r + 0xa8) * fabs(fac));
        return r;
    }
    free(r); return NULL;
}
/* xform_store_ent's arc under a non-uniform scale (l.1253-1316): convert_arc_to_nb (mccadutil 0x1800588a0, decomp/surfrev/util_conv.c l.63-140:
   nb +0 / +0x18 the arc's start / end, +0x30 t1 = 0, +0x38 t2 = the last knot, +0x40 = the radius, +0x68 u16 0x402 (degree 2, 4 values),
   +0x6a flags 0x11 (| 2 for a full circle), +0x60 / +0x64 the counts) + nbrat_3d_to_4d, then put_nurbs_in_db (mccore 0x1802daaf0,
   decomp/xform/mccore_xform.c l.5747): the start / end again (the first / last control point's coordinates when t1 / t2 are the end
   knots - their w is 1 for these points), +0x40 := the largest nb_parm_curvature (gaps/core2b.c: |d1 x d2| / |d1| with the derivatives
   scaled to t2 - t1 = 1; 0 when |d1| <= 1e-6) at t1 + i (t2 - t1) / (2 ncp - 2) that is below 1e9, at least 1e-9 (degree < 2: 1e-9),
   check_nurb_knots (no change on clamped knots); the record fields nb +0x48..+0x5f are left 0 (convert_arc_to_nb does not write them
   and xform_store_ent's buffer is zeroed [inferred]). eval_nurbs' two char arguments (1, 1) are NOT DECODED: ours = derivatives on the
   raw parameter (mcs_spline_eval_d). Returns the class 0x80 record (malloc'd), then transformed by mcsb_xform_curve. */
uint8_t *mcsb_arc_nurbs_rec(const McamFile *f, const Ent *en, double tol, uint32_t *n) {
    Nb o; if (en->type != E_ARC || ent_nb(f, en, tol, &o)) return NULL;
    *n = 0xd8 + 8 * (uint32_t)(o.nk + 4 * o.ncp);
    uint8_t *r = calloc(1, *n); if (!r) { free(o.k); free(o.p); return NULL; }
    uint32_t cls = 0x80; memcpy(r, &cls, 4);
    memcpy(r + 0xc8, &o.nk, 4); memcpy(r + 0xcc, &o.ncp, 4); r[0xd0] = 2; r[0xd1] = 4;
    r[0xd2] = (uint8_t)(0x11 | (fabs(fabs(en->sw) - 2 * M_PI) < tol ? 2 : 0));   /* |sweep| - 2 pi within tol (strictly): closed */
    for (int i = 0; i < o.nk; i++) wdd(r + 0xd8 + 8 * i, o.k[i]);
    for (int i = 0; i < 4 * o.ncp; i++) wdd(r + 0xd8 + 8 * (o.nk + i), o.p[i]);
    double t1 = 0, t2 = o.k[o.nk - 1];
    wdd(r + 0x98, t1); wdd(r + 0xa0, t2);
    const double *P0 = o.p + 1, *P1 = o.p + 4 * (o.ncp - 1) + 1;   /* put_nurbs_in_db: t1 / t2 are the end knots: the control points' x y z */
    for (int i = 0; i < 3; i++) { wdd(r + 0x68 + 8 * i, P0[i]); wdd(r + 0x80 + 8 * i, P1[i]); }
    CurveRec cr = { 0, 0x80, r, *n };
    double cmax = 0, dt = (t2 - t1) / (double)(o.ncp * 2 - 2), s = 1.0;   /* nb_parm_curvature: eval_nurbs' param_6 = 1: x knot range, then 1 / (t2 - t1): 1 on an untrimmed arc (t1, t2 = the end knots); corrected 2026-11-09 */
    for (double tt = t1; tt <= t2; tt += dt) {
        double d[3][3]; if (mcs_spline_eval_d(&cr, tt, 2, d)) continue;
        double a[3] = { d[1][0] * s, d[1][1] * s, d[1][2] * s }, b[3] = { d[2][0] * s * s, d[2][1] * s * s, d[2][2] * s * s }, c[3];
        double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), k = 0;
        if (la > 1e-6) { cross3(a, b, c); k = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) / la; }
        if (cmax < k && k < 1e9) cmax = k;
    }
    if (cmax < 1e-9) cmax = 1e-9;
    wdd(r + 0xa8, cmax);
    free(o.k); free(o.p);
    return r;
}
/* eval_surf with FUN_1806f72a0's clamp of (u, v) into the surface's range */
static void ev_clamp(const McamFile *f, const uint8_t *rec, uint32_t n, double uv[2], double P[3]) {
    const uint8_t *S = rec + 0x68;
    if (uv[0] < rd(S + 8) || rd(S + 0x10) < uv[0]) uv[0] = uv[0] < rd(S + 8) ? rd(S + 8) : rd(S + 0x10);
    if (uv[1] < rd(S + 0x18) || rd(S + 0x20) < uv[1]) uv[1] = uv[1] < rd(S + 0x18) ? rd(S + 0x18) : rd(S + 0x20);
    double o[6][3]; if (ev(f, rec, n, 0, uv[0], uv[1], o)) { P[0] = P[1] = P[2] = 0; return; }
    for (int k = 0; k < 3; k++) P[k] = o[0][k];
}
/* CurveLength (MaCurve's length, vtable +0xc0): NOT DECODED - ours: Gauss-Legendre (5 points) per knot span */
static double uv_length(const CurveRec *c, const double *K, int nk) {
    static const double x[5] = { -0.9061798459386640, -0.5384693101056831, 0, 0.5384693101056831, 0.9061798459386640 },
                        w[5] = { 0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891 };
    double L = 0;
    for (int i = 0; i + 1 < nk; i++) { double a = K[i], b = K[i + 1]; if (!(b > a)) continue;
        for (int g = 0; g < 5; g++) { double o[3][3]; if (mcs_spline_eval_d(c, 0.5 * (a + b) + 0.5 * (b - a) * x[g], 1, o)) continue;
            L += 0.5 * (b - a) * w[g] * sqrt(o[1][0] * o[1][0] + o[1][1] * o[1][1]); } }
    return L;
}
static int colinear2(const double a[2], const double b[2], const double c[2]) {    /* check_colinear (p_2d), mccore 0x180666430 */
    double d1[2] = { b[0] - a[0], b[1] - a[1] }, d2[2] = { c[0] - b[0], c[1] - b[1] }, l1 = hypot(d1[0], d1[1]), l2 = hypot(d2[0], d2[1]);
    if (l1 <= 1e-8 || l2 <= 1e-8) return 1;
    if ((d2[0] * d1[0] + d2[1] * d1[1]) / (l2 * l1) < 0.99985) return 0;
    double e[2] = { c[0] - a[0], c[1] - a[1] }, le = hypot(e[0], e[1]);   /* dist_pt_l: b to the line a -> c */
    return (le > 0 ? fabs((b[0] - a[0]) * e[1] - (b[1] - a[1]) * e[0]) / le : l1) < 1e-8;
}
static int colinear3(const double a[3], const double b[3], const double c[3], double tol) {   /* check_colinear (p_3d), 0x1806665c0 */
    double d1[3], d2[3], e[3], x[3]; for (int k = 0; k < 3; k++) { d1[k] = b[k] - a[k]; d2[k] = c[k] - b[k]; e[k] = c[k] - a[k]; }
    double l1 = sqrt(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]), l2 = sqrt(d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2]);
    if (l1 <= tol || l2 <= tol) return 1;
    if ((d2[0] * d1[0] + d2[1] * d1[1] + d2[2] * d1[2]) / (l2 * l1) < 0.99985) return 0;
    double le = sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]); cross3(d1, e, x);   /* dist_pt_l_3d */
    return (le > 0 ? sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]) / le : l1) < tol * 2.0;
}
/* FUN_1806f8ad0: drop the points between two neighbours they are colinear with (in uv, then on the surface) from index 2 on */
static void drop_colinear(const McamFile *f, const uint8_t *b, uint32_t bn, double *P, int *m, double tol) {
    if (*m < 3) return;
    double A[2] = { P[0], P[1] }, B[2] = { P[2], P[3] }; int w = 2, dropped = 0;
    for (int i = 2; i < *m; i++) {
        double C[2] = { P[2 * i], P[2 * i + 1] }; int col = colinear2(A, B, C);
        if (col) { double a[2] = { A[0], A[1] }, bb[2] = { B[0], B[1] }, c[2] = { C[0], C[1] }, pa[3], pb[3], pc[3];
            ev_clamp(f, b, bn, a, pa); ev_clamp(f, b, bn, bb, pb); ev_clamp(f, b, bn, c, pc); col = colinear3(pa, pb, pc, tol); }
        if (!col) { P[2 * w] = C[0]; P[2 * w + 1] = C[1]; w++; A[0] = B[0]; A[1] = B[1]; }
        else { P[2 * (w - 1)] = C[0]; P[2 * (w - 1) + 1] = C[1]; dropped = 1; }
        B[0] = C[0]; B[1] = C[1];
    }
    if (dropped) *m = w;
}
/* linearize_uv_nurb_to_surf (mccore 0x1806f78c0, decomp/surfprim/linuv.c l.3): the curve's start (unless within 1e-8 of the last point),
   then steps of brk_spline_const_err (min L * 1e-4, max L / 2, curve_chord_dev) accepted when within 1e-6 of the last parameter, the
   3D chord <= 5 tol, the uv chord <= 2 L * 1e-4, or the 3D middle within surf_step_chord_dev of the chord's - else halved, and after
   an accepted halved step the walk goes back up through the failed ones; the end itself is not added; FUN_1806f8ad0 at the end */
static int lin_uv(const McamFile *f, const uint8_t *b, uint32_t bn, const CurveRec *c, const double *K, int nk, double **P, int *m, int *cap,
                  const McSysTol *st) {
    int first = *m; double t0, t1; mcs_spline_t1t2(c, &t0, &t1);
    double L = uv_length(c, K, nk), mins = L * 1e-4, maxs = L * 0.5;
#define PUSH(U, V) do { if (*m + 1 >= *cap) { *cap = *m + 100; *P = realloc(*P, sizeof(double) * 2 * (size_t)*cap); } (*P)[2 * *m] = (U); (*P)[2 * *m + 1] = (V); (*m)++; } while (0)
    double uvA[3], uvPrev[2], Pprev[3], P3[3];
    if (mcs_curve_eval(NULL, c, t0, uvA)) return -1;
    double q[2] = { uvA[0], uvA[1] }; ev_clamp(f, b, bn, q, Pprev);
    if (*m == 0 || hypot((*P)[2 * (*m - 1)] - uvA[0], (*P)[2 * (*m - 1) + 1] - uvA[1]) > 1e-8) PUSH(uvA[0], uvA[1]);
    uvPrev[0] = uvA[0]; uvPrev[1] = uvA[1];
    double tPrev = t0, t = t0, tFail = 0, tFirst = 0; int last = 0, bis = 0, guard = 0;
    do {
        mcs_brk_spline_const_err(c, mins, maxs, st->curve_chord_dev, 0, st->tol, &t);
        if (t1 - 1e-8 <= t) { last = 1; t = t1; }
        for (;;) {
            if (++guard > 200000) return -1;
            double uv[3]; if (mcs_curve_eval(NULL, c, t, uv)) return -1;
            uvA[0] = uv[0]; uvA[1] = uv[1]; double cl[2] = { uv[0], uv[1] }; ev_clamp(f, b, bn, cl, P3);
            int accept = fabs(t - tPrev) <= 1e-6;
            if (!accept) {
                double d3 = sqrt((Pprev[0] - P3[0]) * (Pprev[0] - P3[0]) + (Pprev[1] - P3[1]) * (Pprev[1] - P3[1]) + (Pprev[2] - P3[2]) * (Pprev[2] - P3[2]));
                accept = d3 <= st->tol * 5.0 || hypot(uvPrev[0] - uvA[0], uvPrev[1] - uvA[1]) <= mins * 2.0;
            }
            if (!accept) {
                double mu[3], Pm[3]; if (mcs_curve_eval(NULL, c, (tPrev + t) * 0.5, mu)) return -1;
                double cm[2] = { mu[0], mu[1] }; ev_clamp(f, b, bn, cm, Pm);
                double dx = Pm[0] - (P3[0] + Pprev[0]) * 0.5, dy = Pm[1] - (P3[1] + Pprev[1]) * 0.5, dz = Pm[2] - (P3[2] + Pprev[2]) * 0.5;
                accept = sqrt(dx * dx + dy * dy + dz * dz) < st->surf_step_chord_dev;
            }
            if (!accept) { if (!bis) tFirst = t; tFail = t; t = (tPrev + t) * 0.5; bis = 1; continue; }
            if (last && !bis) break;                                  /* the end: not added */
            PUSH(uvA[0], uvA[1]);
            tPrev = t; memcpy(Pprev, P3, sizeof P3); uvPrev[0] = uvA[0]; uvPrev[1] = uvA[1];
            if (!bis) break;
            t = tFail; double dd = tFail - tFirst; tFail = tFirst;    /* back up through the failed parameters */
            if (fabs(dd) < 1e-6) bis = 0;
        }
    } while (!last);
#undef PUSH
    int k = *m - first; drop_colinear(f, b, bn, *P + 2 * first, &k, st->tol); *m = first + k;
    return 0;
}
/* the face's loops as uv polygons (FUN_18003d840 per loop); every loop closed by its last curve's end point (measured: the stored loops
   end with it - CAMSHAFT.mcam surface 10 point 112 = the loop's end, 5e-16 from its start; where Mastercam adds it is NOT DECODED) */
int mcsb_prim_loops(const McamFile *f, const uint8_t *base, uint32_t bn, const void *face, double ***pts, int **cnt) {
    const OpFace *o = face; const McSysTol *st = mc_systol_units(mcam_is_metric(f));
    *pts = calloc(o->nloops ? o->nloops : 1, sizeof(double *)); *cnt = calloc(o->nloops ? o->nloops : 1, sizeof(int));
    int ci = 0;
    for (int l = 0; l < o->nloops; l++) {
        double *P = NULL; int m = 0, cap = 0;
        for (int k = 0; k < o->loop_ncrv[l]; k++, ci++) {
            uint32_t n; uint8_t *r = uv_curve_rec(&o->crv[ci], &n); CurveRec c = { 0, 0x80, r, n };
            int bad = lin_uv(f, base, bn, &c, o->crv[ci].k, o->crv[ci].nk, &P, &m, &cap, st);
            if (!bad && k == o->loop_ncrv[l] - 1) { double e[3]; if (!mcs_curve_eval(NULL, &c, o->crv[ci].k[o->crv[ci].nc], e)) {
                P = realloc(P, sizeof(double) * 2 * (size_t)(m + 1)); P[2 * m] = e[0]; P[2 * m + 1] = e[1]; m++; } }
            free(r);
            if (bad) { free(P); for (int j = 0; j < l; j++) free((*pts)[j]); free(*pts); free(*cnt); *pts = NULL; *cnt = NULL; return -1; }
        }
        (*pts)[l] = P; (*cnt)[l] = m;
    }
    return o->nloops;
}
/* check_edge (mccore 0x18047b620): the edges of the range (u0 8, u1 2, v0 1, v1 4) the point lies on - within 0.01 in the parameter and
   surf_step_chord_dev on the surface (the point moved onto it) */
static int on_edge(const McamFile *f, const uint8_t *b, uint32_t bn, double uv[2], double P[3], double sdev) {
    const uint8_t *S = b + 0x68; int r = 0; double o[6][3];
#define D3(Q) sqrt((P[0] - (Q)[0]) * (P[0] - (Q)[0]) + (P[1] - (Q)[1]) * (P[1] - (Q)[1]) + (P[2] - (Q)[2]) * (P[2] - (Q)[2]))
    if (fabs(uv[0] - rd(S + 8)) < 0.01) { if (!ev(f, b, bn, 0, rd(S + 8), uv[1], o) && D3(o[0]) < sdev) { r = 8; memcpy(P, o[0], 24); uv[0] = rd(S + 8); } }
    else if (fabs(uv[0] - rd(S + 0x10)) < 0.01) { if (!ev(f, b, bn, 0, rd(S + 0x10), uv[1], o) && D3(o[0]) < sdev) { r = 2; memcpy(P, o[0], 24); uv[0] = rd(S + 0x10); } }
    if (fabs(uv[1] - rd(S + 0x18)) < 0.01) { if (!ev(f, b, bn, 0, uv[0], rd(S + 0x18), o) && D3(o[0]) < sdev) { r |= 1; memcpy(P, o[0], 24); uv[1] = rd(S + 0x18); } }
    else if (fabs(uv[1] - rd(S + 0x20)) < 0.01) { if (!ev(f, b, bn, 0, uv[0], rd(S + 0x20), o) && D3(o[0]) < sdev) { r |= 4; memcpy(P, o[0], 24); uv[1] = rd(S + 0x20); } }
#undef D3
    return r;
}
/* FUN_18003da10's untrimmed cases: one loop and check_untrimmed (mccore 0x1808b0f30: every point on an edge of the range and the
   four corners met), or create_trim_from_uvs's own (one loop, every point within 1e-8 of a corner, all four met) - the base is then
   the face (unblanked) */
int mcsb_prim_untrimmed(const McamFile *f, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt) {
    if (nl != 1) return 0;
    const uint8_t *S = b + 0x68; double u0 = rd(S + 8), u1 = rd(S + 0x10), v0 = rd(S + 0x18), v1 = rd(S + 0x20);
    double sdev = mc_systol_units(mcam_is_metric(f))->surf_step_chord_dev;
    int c00 = 0, c10 = 0, c11 = 0, c01 = 0, all = 1;
    for (int i = 0; i < cnt[0]; i++) {
        double uv[2] = { pts[0][2 * i], pts[0][2 * i + 1] }, o[6][3]; if (ev(f, b, bn, 0, uv[0], uv[1], o)) return 0;
        double P[3] = { o[0][0], o[0][1], o[0][2] };
        if (!on_edge(f, b, bn, uv, P, sdev)) { all = 0; break; }
        int au = fabs(uv[0] - u0) < 1e-8, bu = fabs(uv[0] - u1) < 1e-8, av = fabs(uv[1] - v0) < 1e-8, bv = fabs(uv[1] - v1) < 1e-8;
        if (au && av) c00 = 1; else if (bu && av) c10 = 1; else if (au && bv) c01 = 1; else if (bu && bv) c11 = 1;
    }
    if (all && c00 && c10 && c01 && c11) return 1;
    int bits = 0;                                        /* create_trim_from_uvs: the corner test on the stored points */
    for (int i = 0; i < cnt[0]; i++) {
        double u = pts[0][2 * i], v = pts[0][2 * i + 1];
        if (fabs(u - u0) < 1e-8 && fabs(v - v0) < 1e-8) bits |= 1; else if (fabs(u - u1) < 1e-8 && fabs(v - v0) < 1e-8) bits |= 2;
        else if (fabs(u - u0) < 1e-8 && fabs(v - v1) < 1e-8) bits |= 4; else if (fabs(u - u1) < 1e-8 && fabs(v - v1) < 1e-8) bits |= 8;
        else return 0;
    }
    return bits == 0xf;
}
/* create_trim_from_uvs (mccore 0x1808b1210, decomp/surfprim/linuv2.c l.467) + store_new_trim_surf_ext (decomp/surfprim/storetrim2.c):
   kind 10, the range = the uv points' box (+0x109 = 1), flags 0x70 with the base's normal bit 4, +0x100 the base's eptr, +0x108 =
   min(loops, 255), +0x10c m (all points), +0x128 loops; the points: the loop holding the smallest u first, then the others in order,
   each table entry { start, count - 2 }; then put_surf_in_db: reduce_trimsurf_data (each loop's points colinear in uv and on the
   surface dropped, core_dist.c l.481) and the header (mcsb_finish). The base must be in f (its eptr). */
uint8_t *mcsb_prim_trim(const McamFile *f, int64_t base_eptr, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt,
                        int shaded, double tol, uint32_t *n) {
    int m = 0, outer = 0; double lo_u = 1e300, r[4] = { 1e300, -1e300, 1e300, -1e300 };
    for (int l = 0; l < nl; l++) for (int i = 0; i < cnt[l]; i++) {
        double u = pts[l][2 * i], v = pts[l][2 * i + 1]; m++;
        if (u < lo_u) { lo_u = u; outer = l; }
        if (u < r[0]) r[0] = u; if (u > r[1]) r[1] = u; if (v < r[2]) r[2] = v; if (v > r[3]) r[3] = v;
    }
    /* reduce_trimsurf_data per loop first (it only shortens the loops: the table is rebuilt from the kept points) */
    double **Q = calloc(nl, sizeof *Q); int *qc = calloc(nl, sizeof *qc); int changed = 0;
    for (int l = 0; l < nl; l++) {
        int c = cnt[l] - 1; if (c < 1) c = cnt[l];             /* the loop's points 0 .. count (the closing duplicate not visited) */
        Q[l] = malloc(sizeof(double) * 2 * (size_t)(cnt[l] + 1)); int w = 0;
        double A[2] = { 0, 0 }, B[2] = { 0, 0 };
        for (int i = 0; i <= c; i++) {
            double C[2] = { i == c ? pts[l][0] : pts[l][2 * i], i == c ? pts[l][1] : pts[l][2 * i + 1] };
            int col = 0;
            if (i >= 2 && colinear2(A, B, C)) { double a[2] = { A[0], A[1] }, bb[2] = { B[0], B[1] }, cc[2] = { C[0], C[1] }, pa[3], pb[3], pc[3];
                ev_clamp(f, b, bn, a, pa); ev_clamp(f, b, bn, bb, pb); ev_clamp(f, b, bn, cc, pc); col = colinear3(pa, pb, pc, tol); }
            if (!col) { if (i < c) { Q[l][2 * w] = C[0]; Q[l][2 * w + 1] = C[1]; w++; } if (i > 0) { A[0] = B[0]; A[1] = B[1]; } }
            else { Q[l][2 * (w - 1)] = C[0]; Q[l][2 * (w - 1) + 1] = C[1]; changed = 1; }
            B[0] = C[0]; B[1] = C[1];
        }
        qc[l] = w;
    }
    int *start = calloc(nl, sizeof *start), *count = calloc(nl, sizeof *count), mm = 0;
    int ord[nl > 0 ? nl : 1], no = 0; ord[no++] = outer; for (int l = 0; l < nl; l++) if (l != outer) ord[no++] = l;
    for (int k = 0; k < nl; k++) { int l = ord[k];
        if (changed) { start[k] = k ? start[k - 1] + count[k - 1] + 2 : 0; count[k] = qc[l] - 1; mm = start[k] + count[k] + 1; }
        else { start[k] = mm; count[k] = cnt[l] - 2; mm += cnt[l]; } }
    if (!changed) mm = m;
    *n = 0x138 + 16 * (uint32_t)mm + 8 * (uint32_t)nl;
    uint8_t *R = calloc(1, *n); uint32_t cls = 0x100; memcpy(R, &cls, 4);
    uint8_t *S = R + 0x68; S[0] = 10;
    w16(S + 6, (uint16_t)(0x70 | (r16(b + 0x6e) & 4)));
    for (int k = 0; k < 4; k++) wdd(S + 8 + 8 * k, r[k]);
    memcpy(R + 0x100, &base_eptr, 8); R[0x108] = (uint8_t)(nl < 255 ? nl : 255); R[0x109] = 1;
    memcpy(R + 0x10c, &mm, 4); memcpy(R + 0x128, &nl, 4);
    uint8_t *P = R + 0x138, *Lt = P + 16 * (size_t)mm;
    for (int k = 0; k < nl; k++) { int l = ord[k];
        if (changed) memcpy(P + 16 * (size_t)start[k], Q[l], sizeof(double) * 2 * (size_t)qc[l]);
        else memcpy(P + 16 * (size_t)start[k], pts[l], sizeof(double) * 2 * (size_t)cnt[l]);
        memcpy(Lt + 8 * k, &start[k], 4); memcpy(Lt + 8 * k + 4, &count[k], 4); }
    for (int l = 0; l < nl; l++) free(Q[l]); free(Q); free(qc); free(start); free(count);
    if (mcsb_finish(f, R, *n, shaded, tol)) { free(R); return NULL; }
    return R;
}
/* FUN_18000d7d0 (mccad, decomp/surfprim/blkbuild.c l.3; Block only): eval_surf mode 1 at the middle of the range (a trimmed surface: its
   base) - the direction it takes is the output's +0x18 = Su (FUN_1805b1f10's layout: point, Su, Sv; decomp/surfprim/evsurf1.c),
   normalised; d = |P - C| (C = the solid's box centre, FUN_180014580); when P + dir d / 2 is nearer C than d, +0x6e bit 4 toggles */
int mcsb_prim_outward(const McamFile *f, uint8_t *rec, uint32_t n, const double C[3]) {
    const uint8_t *S = rec + 0x68; const uint8_t *e = rec; uint32_t en = n;
    if (S[0] == 10) { Surf t; memset(&t, 0, sizeof t); t.rec = rec; t.n = n; t.kind = 10; const Surf *b = mcam_surf_base(f, &t); if (!b) return 0; e = b->rec; en = b->n; }
    double u = (rd(S + 8) + rd(S + 0x10)) * 0.5, v = (rd(S + 0x18) + rd(S + 0x20)) * 0.5, o[6][3];
    if (ev(f, e, en, 1, u, v, o)) return 0;
    double N[3] = { o[1][0], o[1][1], o[1][2] }, l = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
    if (l == 0) N[0] = N[1] = N[2] = 0; else for (int k = 0; k < 3; k++) N[k] /= l;
    double d = sqrt((o[0][0] - C[0]) * (o[0][0] - C[0]) + (o[0][1] - C[1]) * (o[0][1] - C[1]) + (o[0][2] - C[2]) * (o[0][2] - C[2])), t2 = 0;
    for (int k = 0; k < 3; k++) { double q = o[0][k] + N[k] * d * 0.5 - C[k]; t2 += q * q; }
    if (sqrt(t2) < d) { w16(rec + 0x6e, r16(rec + 0x6e) ^ 4); return 1; }
    return 0;
}
/* make_view_k (rows x, y, k) for callers outside (cui_geo.c: the torus minor-radius centre, FUN_18002d030) */
int mcsb_view_k(const double k[3], double M[9]) { return view_k(k, M); }

/* ------------------------------------------------------------------------------------------------ Flat Boundary */
/* create_nurbsurf_from_4_points (mccore 0x180268700, decomp/surfflat/trimflat.c): kind 0, a bilinear 2 x 2 net p0 p1 / p2 p3 (u
   along p0 -> p1, v along p0 -> p2), range 0 .. 1, flags 0x40; FUN_1806f8a00: +6 bit 2 (the normal reversed) when det(Su, Sv, the
   Cplane's z) < 0 at the middle; then put_surf_in_db (mcsb_finish) */
uint8_t *mcsb_plane4(const McamFile *f, const double c[4][3], const double cz[3], int shaded, double tol, uint32_t *n) {
    *n = 0x118 + 8 * (4 + 4 + 3 * 4);
    uint8_t *R = calloc(1, *n); uint32_t cls = 0x100; memcpy(R, &cls, 4);
    int two = 2, four = 4; memcpy(R + 0x100, &four, 4); memcpy(R + 0x104, &four, 4); memcpy(R + 0x108, &two, 4); memcpy(R + 0x10c, &two, 4);
    R[0x110] = 3; R[0x111] = 1; R[0x112] = 1; R[0x113] = 1;
    static const double K[4] = { 0, 0, 1, 1 };
    for (int i = 0; i < 4; i++) { wdd(R + 0x118 + 8 * i, K[i]); wdd(R + 0x118 + 8 * (4 + i), K[i]); }
    for (int q = 0; q < 4; q++) for (int k = 0; k < 3; k++) wdd(R + 0x118 + 8 * (8 + 3 * q + k), c[q][k]);
    uint8_t *S = R + 0x68; wdd(S + 8, 0); wdd(S + 0x10, 1); wdd(S + 0x18, 0); wdd(S + 0x20, 1);
    double su[3], sv[3], N[3]; for (int k = 0; k < 3; k++) { su[k] = c[1][k] - c[0][k]; sv[k] = c[2][k] - c[0][k]; }
    cross3(su, sv, N);
    w16(S + 6, (uint16_t)(0x40 | ((N[0] * cz[0] + N[1] * cz[1] + N[2] * cz[2]) < 0 ? 4 : 0)));
    if (mcsb_finish(f, R, *n, shaded, tol)) { free(R); return NULL; }
    return R;
}
/* a kind 10 record over base b (eptr base_eptr): the loops in the order given, each { start, count - 2 }; the range (u0 u1 v0 v1; NULL
   = the points' box), flags, ent +0x124 (b124); +0x109 1, +0x108 min(loops, 255); put_surf_in_db: reduce_trimsurf_data and the header */
static uint8_t *trim_rec(const McamFile *f, int64_t base_eptr, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt,
                         const int *ord, const double *range, uint16_t flags, int b124, int shaded, double tol, uint32_t *n) {
    int m = 0; double r[4] = { 1e300, -1e300, 1e300, -1e300 };
    for (int l = 0; l < nl; l++) for (int i = 0; i < cnt[l]; i++) {
        double u = pts[l][2 * i], v = pts[l][2 * i + 1]; m++;
        if (u < r[0]) r[0] = u; if (u > r[1]) r[1] = u; if (v < r[2]) r[2] = v; if (v > r[3]) r[3] = v;
    }
    if (range) memcpy(r, range, sizeof r);
    double **Q = calloc(nl, sizeof *Q); int *qc = calloc(nl, sizeof *qc); int changed = 0;
    for (int l = 0; l < nl; l++) {                       /* reduce_trimsurf_data (core_dist.c l.481) */
        int c = cnt[l] - 1; if (c < 1) c = cnt[l];
        Q[l] = malloc(sizeof(double) * 2 * (size_t)(cnt[l] + 1)); int w = 0;
        double A[2] = { 0, 0 }, B[2] = { 0, 0 };
        for (int i = 0; i <= c; i++) {
            double C[2] = { i == c ? pts[l][0] : pts[l][2 * i], i == c ? pts[l][1] : pts[l][2 * i + 1] };
            int col = 0;
            if (i >= 2 && colinear2(A, B, C)) { double a[2] = { A[0], A[1] }, bb[2] = { B[0], B[1] }, cc[2] = { C[0], C[1] }, pa[3], pb[3], pc[3];
                ev_clamp(f, b, bn, a, pa); ev_clamp(f, b, bn, bb, pb); ev_clamp(f, b, bn, cc, pc); col = colinear3(pa, pb, pc, tol); }
            if (!col) { if (i < c) { Q[l][2 * w] = C[0]; Q[l][2 * w + 1] = C[1]; w++; } if (i > 0) { A[0] = B[0]; A[1] = B[1]; } }
            else { Q[l][2 * (w - 1)] = C[0]; Q[l][2 * (w - 1) + 1] = C[1]; changed = 1; }
            B[0] = C[0]; B[1] = C[1];
        }
        qc[l] = w;
    }
    int *start = calloc(nl, sizeof *start), *count = calloc(nl, sizeof *count), mm = 0;
    for (int k = 0; k < nl; k++) { int l = ord[k];
        if (changed) { start[k] = k ? start[k - 1] + count[k - 1] + 2 : 0; count[k] = qc[l] - 1; mm = start[k] + count[k] + 1; }
        else { start[k] = mm; count[k] = cnt[l] - 2; mm += cnt[l]; } }
    if (!changed) mm = m;
    *n = 0x138 + 16 * (uint32_t)mm + 8 * (uint32_t)nl;
    uint8_t *R = calloc(1, *n); uint32_t cls = 0x100; memcpy(R, &cls, 4);
    uint8_t *S = R + 0x68; S[0] = 10;
    w16(S + 6, flags);
    for (int k = 0; k < 4; k++) wdd(S + 8 + 8 * k, r[k]);
    memcpy(R + 0x100, &base_eptr, 8); R[0x108] = (uint8_t)(nl < 255 ? nl : 255); R[0x109] = 1; R[0x124] = (uint8_t)b124;
    memcpy(R + 0x10c, &mm, 4); memcpy(R + 0x128, &nl, 4);
    uint8_t *P = R + 0x138, *Lt = P + 16 * (size_t)mm;
    for (int k = 0; k < nl; k++) { int l = ord[k];
        if (changed) memcpy(P + 16 * (size_t)start[k], Q[l], sizeof(double) * 2 * (size_t)qc[l]);
        else memcpy(P + 16 * (size_t)start[k], pts[l], sizeof(double) * 2 * (size_t)cnt[l]);
        memcpy(Lt + 8 * k, &start[k], 4); memcpy(Lt + 8 * k + 4, &count[k], 4); }
    for (int l = 0; l < nl; l++) free(Q[l]); free(Q); free(qc); free(start); free(count);
    if (mcsb_finish(f, R, *n, shaded, tol)) { free(R); return NULL; }
    return R;
}
/* trim_surf_from_flat_chains' record (mccore 0x1808ac9d0): the base's surf struct with kind 10 - its flags (0x40 | the normal bit), the
   range 1/12 .. 11/12 both ways (the boundary inside the base's 10 % margins), ent +0x124 = 1; the loops as given (the outer first) */
uint8_t *mcsb_flat_trim(const McamFile *f, int64_t base_eptr, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt,
                        int shaded, double tol, uint32_t *n) {
    static const double rg[4] = { 0.08333333333333334, 0.9166666666666666, 0.08333333333333334, 0.9166666666666666 };   /* 0x3fb5555555555556 / 0x3fed555555555555 */
    int ord[nl > 0 ? nl : 1]; for (int k = 0; k < nl; k++) ord[k] = k;
    return trim_rec(f, base_eptr, b, bn, nl, pts, cnt, ord, rg, r16(b + 0x6e), 1, shaded, tol, n);
}

/* ------------------------------------------------------------------------------------------------ Loft: the alike chains (ruled, NURBS) */
/* all_chains_alike (mccore 0x1806fc220) for ruled + surface type 2: the chains' curve counts equal (FUN_1806fc690); no chain mixing
   degrees among its arcs / splines (FUN_1806fe4e0); the k-th curves of all chains the same class - arcs the same point count
   (FUN_180702c00: 9 a full circle, 5 a half or more, else 3), parametric splines the same segments and order (FUN_180703a80), NURBS
   curves the same counts / degree / values per point / rationality / knots (1e-5, FUN_180702c90) and direction; a parametric spline
   3D (+0xb5 != 2) over 0 .. segments, a NURBS curve 3D over its knot range; after the first chain a spline's degree = the degree so
   far (FUN_1807039f0: lines 1, arcs 2 + rational, splines order - 1, NURBS degree + rational when bit 2 is clear). */
static int arc_cls(const Ent *e, double tol) { double s = fabs(e->sw); if (fabs(s - 2 * M_PI) < tol) return 9; if (fabs(s - M_PI) < tol || s > M_PI) return 5; return 3; }
static void deg_acc(const McamFile *f, const Ent *e, int *deg, int *rat) {
    if (e->type == E_LINE) { if (*deg < 1) *deg = 1; }
    else if (e->type == E_ARC) { if (*deg < 2) *deg = 2; *rat = 1; }
    else if (e->crv >= 0 && e->crv < f->ncrvs) { const uint8_t *R = f->crvs[e->crv].rec;
        if (e->cls == 8) { if (*deg < R[0xb4] - 1) *deg = R[0xb4] - 1; }
        else { if (*deg < R[0xd0]) *deg = R[0xd0]; if (!(R[0xd2] & 4)) *rat = 1; } }
}
int mcsb_loft_alike(const McamFile *f, const Ent *const *E, const int *rev, const int *ne, int nch, double tol, int *deg, int *rat) {
    *deg = 0; *rat = 0;
    for (int c = 1; c < nch; c++) if (ne[c] != ne[0]) return 0;
    for (int c = 0; c < nch; c++) {                     /* FUN_1806fe4e0 */
        int lo = 30000, hi = 0;
        for (int i = 0; i < ne[c]; i++) { const Ent *e = E[c * 4096 + i]; int d = -1;
            if (e->type == E_ARC) d = 2;
            else if (e->type == E_SPLINE && e->crv >= 0 && e->crv < f->ncrvs) d = e->cls == 8 ? f->crvs[e->crv].rec[0xb4] - 1 : f->crvs[e->crv].rec[0xd0];
            if (d >= 0) { if (d < lo) lo = d; if (d > hi) hi = d; } }
        if (lo < 30000 && hi > 0 && hi - lo > 0) return 0;
    }
    for (int k = 0; k < ne[0]; k++) {
        const Ent *e0 = E[k];
        for (int c = 0; c < nch; c++) {
            const Ent *e = E[c * 4096 + k];
            if (e->type == E_SPLINE) {
                if (e->crv < 0 || e->crv >= f->ncrvs) return 0;
                const uint8_t *R = f->crvs[e->crv].rec; double t1, t2; mcs_spline_t1t2(&f->crvs[e->crv], &t1, &t2);
                if (e->cls == 8) { if (R[0xb5] == 2 || fabs(t1) > 1e-6 || fabs(t2 - r32(R + 0xb0)) > 1e-6) return 0; }
                else { int nk = r32(R + 0xc8); if (R[0xd1] == 2 || fabs(t1 - rd(R + 0xd8)) > 1e-6 || fabs(t2 - rd(R + 0xd8 + 8 * (nk - 1))) > 1e-6) return 0; }
                if (c > 0) { int d = e->cls == 8 ? R[0xb4] - 1 : R[0xd0], rr = e->cls == 8 ? 0 : !(R[0xd2] & 4);
                    if (d != *deg || (e->cls == 8 ? *rat : rr != *rat)) return 0; }
            }
            if (c > 0) {
                if (e->type != e0->type || (e->type == E_SPLINE && e->cls != e0->cls)) return 0;
                if (e->type == E_ARC && arc_cls(e, tol) != arc_cls(e0, tol)) return 0;
                if (e->type == E_SPLINE && e->cls == 8) { const uint8_t *a = f->crvs[e->crv].rec, *b = f->crvs[e0->crv].rec; if (r32(a + 0xb0) != r32(b + 0xb0) || a[0xb4] != b[0xb4]) return 0; }
                if (e->type == E_SPLINE && e->cls == 0x80) { const uint8_t *a = f->crvs[e->crv].rec, *b = f->crvs[e0->crv].rec; int nk = r32(a + 0xc8);
                    if (rev[c * 4096 + k] != rev[k] || r32(a + 0xcc) != r32(b + 0xcc) || nk != r32(b + 0xc8) || a[0xd0] != b[0xd0] || a[0xd1] != b[0xd1] || ((a[0xd2] ^ b[0xd2]) & 4)) return 0;
                    for (int q = 0; q < nk; q++) if (fabs(rd(a + 0xd8 + 8 * q) - rd(b + 0xd8 + 8 * q)) > 1e-5) return 0; }
            }
            deg_acc(f, e, deg, rat);
        }
    }
    return 1;
}
/* FUN_180701c20 (ruled = 1): each chain one NURBS curve - lines FUN_1806fda60 (degree d, d + 1 even points, knots 0.. / 1..), arcs
   convert_arc_to_nb + nbrat_3d_to_4d, splines convert_spline_to_nb / the NURBS record, made rational (w 1, FUN_1806fdcf0) when the
   surface is; reversed ones FUN_1802633a0 (points reversed, knots kmax - k); joined (FUN_1806fd910: the next knots + the last, the
   shared point once); the first chain's knots for u; v degree 1 over the chains (knots 0 0 1 .. n-1 n-1); +0x6e |= 0x40, the normal
   reversed when (Su x Sv)(0.5, 0.5) . the Cplane z < 0 */
uint8_t *mcsb_loft_alike_ruled(const McamFile *f, const Ent *const *E, const int *rev, const int *ne, int nch, int d, int rat,
                               const double cz[3], int shaded, double tol, uint32_t *n) {
    int vpp = rat ? 4 : 3, cap = 0; double *K = NULL; int nk = 0, ncu = 0; double *P = NULL;
    for (int c = 0; c < nch; c++) {
        int cn = 0; double *row = NULL; int rowcap = 0;
        double *KK = NULL; int knk = 0;
        for (int i = 0; i < ne[c]; i++) {
            const Ent *e = E[c * 4096 + i]; Nb nb; memset(&nb, 0, sizeof nb);
            if (e->type == E_LINE) {                    /* FUN_1806fda60 */
                nb.deg = d; nb.ncp = d + 1; nb.nk = 2 * d + 2; nb.dim = vpp; nb.rat = rat;
                nb.k = malloc(sizeof(double) * nb.nk); for (int q = 0; q <= d; q++) { nb.k[q] = 0; nb.k[d + 1 + q] = 1; }
                nb.p = malloc(sizeof(double) * vpp * nb.ncp);
                for (int q = 0; q <= d; q++) { double s = (double)q / d, *o = nb.p + vpp * q; int off = 0; if (rat) { o[0] = 1; off = 1; }
                    o[off] = (1 - s) * e->a.x + s * e->b.x; o[off + 1] = (1 - s) * e->a.y + s * e->b.y; o[off + 2] = (1 - s) * e->a.z + s * e->b.z; }
            } else {
                if (ent_nb(f, e, tol, &nb)) { free(row); free(KK); free(P); free(K); return NULL; }
                if (rat && nb.dim == 3) {                 /* FUN_1806fdcf0 */
                    double *np = malloc(sizeof(double) * 4 * nb.ncp); for (int q = 0; q < nb.ncp; q++) { np[4 * q] = 1; memcpy(np + 4 * q + 1, nb.p + 3 * q, 24); }
                    free(nb.p); nb.p = np; nb.dim = 4; nb.rat = 1; }
            }
            if (rev[c * 4096 + i]) {                      /* FUN_1802633a0 */
                for (int a = 0, b = nb.ncp - 1; a < b; a++, b--) for (int q = 0; q < nb.dim; q++) { double t = nb.p[nb.dim * a + q]; nb.p[nb.dim * a + q] = nb.p[nb.dim * b + q]; nb.p[nb.dim * b + q] = t; }
                double km = nb.k[nb.nk - 1]; for (int a = 0, b = nb.nk - 1; a <= b; a++, b--) { double ta = km - nb.k[a], tb = km - nb.k[b]; nb.k[a] = tb; nb.k[b] = ta; }
            }
            /* points: the shared one once */
            int start = cn ? 1 : 0; if (cn + nb.ncp > rowcap) { rowcap = (cn + nb.ncp) * 2; row = realloc(row, sizeof(double) * vpp * rowcap); }
            for (int q = start; q < nb.ncp; q++) memcpy(row + vpp * cn++, nb.p + nb.dim * q, sizeof(double) * vpp);
            if (cn == nb.ncp - start && start == 0) {} 
            /* knots: FUN_1806fd910 */
            if (!knk) { KK = malloc(sizeof(double) * nb.nk); memcpy(KK, nb.k, sizeof(double) * nb.nk); knk = nb.nk; }
            else { double last = KK[knk - 1]; KK = realloc(KK, sizeof(double) * (knk + nb.nk));
                for (int s = 1; s < nb.nk; s++) KK[knk - 2 + s - d] = last + nb.k[s];
                knk = knk + nb.nk - d - 2; }
            nb_free(&nb);
        }
        if (c == 0) { K = KK; nk = knk; ncu = cn; P = malloc(sizeof(double) * vpp * ncu * nch); }
        else { free(KK); if (cn != ncu) { free(row); free(P); free(K); return NULL; } }
        memcpy(P + (size_t)vpp * ncu * c, row, sizeof(double) * vpp * ncu); free(row);
        (void)cap;
    }
    int nkv = nch + 2, ncv = nch;
    *n = 0x118 + 8 * (uint32_t)(nk + nkv + vpp * ncu * ncv);
    uint8_t *R = calloc(1, *n); uint32_t cls = 0x100; memcpy(R, &cls, 4);
    memcpy(R + 0x100, &nk, 4); memcpy(R + 0x104, &nkv, 4); memcpy(R + 0x108, &ncu, 4); memcpy(R + 0x10c, &ncv, 4);
    R[0x110] = (uint8_t)vpp; R[0x111] = (uint8_t)d; R[0x112] = 1; R[0x113] = rat ? 0 : 1;
    uint8_t *T = R + 0x118;
    for (int i = 0; i < nk; i++) wdd(T + 8 * i, K[i]);
    double V[nkv]; V[0] = 0; for (int i = 0; i < nch; i++) V[1 + i] = i; V[nkv - 1] = nch - 1;
    for (int i = 0; i < nkv; i++) wdd(T + 8 * (nk + i), V[i]);
    for (size_t q = 0; q < (size_t)vpp * ncu * ncv; q++) wdd(T + 8 * (nk + nkv + q), P[q]);
    uint8_t *S = R + 0x68; S[0] = 0;
    wdd(S + 8, K[0]); wdd(S + 0x10, K[nk - 1]); wdd(S + 0x18, 0); wdd(S + 0x20, nch - 1);
    free(K); free(P);
    w16(S + 6, 0x40);
    double o[6][3];
    if (!nurbs_ders(R, *n, 0.5, 0.5, o)) { double N[3]; cross3(o[1], o[2], N); if (N[0] * cz[0] + N[1] * cz[1] + N[2] * cz[2] < 0) w16(S + 6, 0x44); }
    if (mcsb_finish(f, R, *n, shaded, tol)) { free(R); return NULL; }
    return R;
}
/* the normal bit of a new NURBS record: reversed when (Su x Sv)(u, v) . cz < 0 (loft_ruled_maker's builders) */
void mcsb_normal_by(uint8_t *rec, uint32_t n, double u, double v, const double cz[3]) {
    double o[6][3]; if (nurbs_ders(rec, n, u, v, o)) return;
    double N[3]; cross3(o[1], o[2], N);
    uint16_t fl = r16(rec + 0x6e); if (N[0] * cz[0] + N[1] * cz[1] + N[2] * cz[2] < 0) fl |= 4; else fl &= (uint16_t)~4; w16(rec + 0x6e, fl);
}

/* for mcsurf_tonurbs.c: the static helpers above */
int mcsb_ent_nb(const McamFile *f, const Ent *en, double tol, int *nk, int *ncp, int *deg, int *dim, int *rat, double **k, double **p) {
    Nb o; if (ent_nb(f, en, tol, &o)) return -1;
    *nk = o.nk; *ncp = o.ncp; *deg = o.deg; *dim = o.dim; *rat = o.rat; *k = o.k; *p = o.p;
    return 0;
}
int mcsb_arc2d_nb(double cx, double cy, double r, double a, double s, double hz, double tol, double *P, double *K, int *ncp, int *nk) { return arc2d_nb(cx, cy, r, a, s, hz, tol, P, K, ncp, nk); }
int mcsb_nurbs_ders(const uint8_t *r, uint32_t n, double u, double v, double o[6][3]) { return nurbs_ders(r, n, u, v, o); }
void mcsb_curv_geom(double o[6][3], double *k1, double *k2) { curv_geom(o, k1, k2); }
int mcsb_nurb_interval(double u, const double *K, int nk, int p) { return nurb_interval(u, K, nk, p); }
uint8_t *mcsb_trim_rec(const McamFile *f, int64_t base_eptr, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt, uint16_t flags, int shaded, double tol, uint32_t *n) {
    int ord[nl > 0 ? nl : 1]; for (int k = 0; k < nl; k++) ord[k] = k;
    return trim_rec(f, base_eptr, b, bn, nl, pts, cnt, ord, NULL, flags, 1, shaded, tol, n);
}
