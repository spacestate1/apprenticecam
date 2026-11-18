/* mcsrfext.c - Surfaces > Modify > Extend (SurfaceExtend, mccad 0x180339600 -> SurfaceExtendApp FUN_180337330): the surface extension of
   extend_surf (mccore 0x18032fb30) for NURBS surfaces (kind 0) = FUN_18032d240, its self-intersection test (FUN_18032cd00 / FUN_1803331a0 /
   FUN_18032c670), the edge choice of the app (FUN_180338540) and extend_trimmed_surf's loop rewrite (0x18032fff0).
   Dumps: decomp/surftrim/core_ext.c (FUN_18032d240 and its helpers), ext_nl.asm (FUN_180331a30 / FUN_180332680), ext_chk.asm, ext_surf.asm,
   apps.c (FUN_180337330 / FUN_180338540), ext_geo.c (lp_ints / ll_ints_3d). formats/surfaces_tab_plan.md "E. Extend".
   Record layout (formats/mcam.md): kind 0 = ent +0x100 nku, nkv, ncu, ncv (i32), +0x110 values per point, +0x111 / +0x112 degrees, +0x113
   bit 0 non-rational, tail at +0x118: u knots, v knots, the net u fastest ((w, wx, wy, wz) when rational). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mcsrfext.h"

static double rd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static void wd(uint8_t *p, double v) { memcpy(p, &v, 8); }
static int32_t r32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void w32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
int mcsb_nurbs_ders(const uint8_t *r, uint32_t n, double u, double v, double o[6][3]);   /* mcsurf_build.c: eval_surf modes 0..2 on NURBS */
int mcsb_view_k(const double k[3], double M[9]);                                       /* make_view_k */

/* ------------------------------------------------------------------------------------------------ geometry helpers (mccore) */
/* lp_ints (0x1804b3ed0): the line a -> b against the plane (normal n through p); t along a -> b */
static int lp_ints(const double n[3], const double p[3], const double a[3], const double b[3], double tol, double o[3], double *t) {
    double dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2], den = n[1] * dy + n[0] * dx + n[2] * dz;
    if (fabs(den) < tol * tol) {
        double ln = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), ld = sqrt(dx * dx + dy * dy + dz * dz);
        if (ln == 0.0 || ld == 0.0 || fabs(den) / (ld * ln) < tol) return 0;
    }
    *t = (n[0] * (p[0] - a[0]) + n[1] * (p[1] - a[1]) + n[2] * (p[2] - a[2])) / den;
    o[0] = dx * *t + a[0]; o[1] = dy * *t + a[1]; o[2] = dz * *t + a[2];
    return 1;
}
/* ll_ints_3d (0x1804b3af0): the points of closest approach of the lines a1 -> b1 and a2 -> b2 (each lp_ints against the plane through
   the other line containing c = d1 x d2) */
static int ll_ints_3d(const double a1[3], const double b1[3], const double a2[3], const double b2[3], double tol, double p1[3], double p2[3]) {
    double d1[3] = { b1[0] - a1[0], b1[1] - a1[1], b1[2] - a1[2] }, d2[3] = { b2[0] - a2[0], b2[1] - a2[1], b2[2] - a2[2] };
    double c[3] = { d2[2] * d1[1] - d2[1] * d1[2], d1[2] * d2[0] - d2[2] * d1[0], d2[1] * d1[0] - d1[1] * d2[0] };
    double n80[3] = { c[1] * d1[2] - c[2] * d1[1], c[2] * d1[0] - c[0] * d1[2], c[0] * d1[1] - c[1] * d1[0] };
    double n98[3] = { c[1] * d2[2] - c[2] * d2[1], c[2] * d2[0] - c[0] * d2[2], c[0] * d2[1] - c[1] * d2[0] }, t;
    if (!lp_ints(n98, a2, a1, b1, tol, p1, &t)) return 0;
    return lp_ints(n80, a1, a2, b2, tol, p2, &t);
}
static void unit3(double v[3]) {   /* FUN_18000f2f0 */
    double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l == 0.0) { v[0] = v[1] = v[2] = 0.0; } else { v[0] /= l; v[1] /= l; v[2] /= l; }
}

/* ------------------------------------------------------------------------------------------------ the row extension */
/* FUN_180331420: a point (homogeneous (w, wx, wy, wz) when rat) as Cartesian */
static void cart(int rat, const double *P, double o[3]) {
    if (!rat) { o[0] = P[0]; o[1] = P[1]; o[2] = P[2]; return; }
    double iw = 1.0 / P[0]; o[0] = iw * P[1]; o[1] = iw * P[2]; o[2] = iw * P[3];
}
/* FUN_180331480: the Cartesian derivative of a (rational) curve from its homogeneous point P and derivative D */
static void cart_d(int rat, const double *P, const double *D, double o[3]) {
    if (!rat) { o[0] = D[0]; o[1] = D[1]; o[2] = D[2]; return; }
    double C[3]; cart(1, P, C);
    for (int k = 0; k < 3; k++) o[k] = (D[k + 1] - D[0] * C[k]) / P[0];
}
/* FUN_18032d060: the quadratic Bezier P (3 points of n values) at t: mode 0 the point, 1 the derivative, 2 the second derivative */
static void qbez(int mode, int n, const double *P, double t, double *o) {
    for (int k = 0; k < n; k++) {
        double p0 = P[k], p1 = P[n + k], p2 = P[2 * n + k];
        /* the operation order of the code (decomp/surftrim/d060 disassembly: t * t and t * 2 first, the last two terms summed first) */
        if (mode == 0) o[k] = ((p0 - p1 * 2.0) + p2) * (t * t) + ((p1 - p0) * (t * 2.0) + p0);
        else if (mode == 1) o[k] = ((p0 - p1 * 2.0) + p2) * (t * 2.0) + (p1 - p0) * 2.0;
        else o[k] = ((p0 - p1 * 2.0) + p2) * 2.0;
    }
}
/* FUN_1803316b0: the length of the quadratic by 100 chords (t from 0 in steps of 0.01, accumulated) */
static double qlen(int n, const double *P) {
    double t = 0.0, len = 0.0, pr[3] = { 0, 0, 0 };
    for (int i = 0; i <= 100; i++) {
        double h[4], c[3]; qbez(0, n, P, t, h); cart(n == 4, h, c);
        if (i > 0) { double dy = pr[1] - c[1], dx = pr[0] - c[0], dz = pr[2] - c[2]; len += sqrt(dy * dy + dx * dx + dz * dz); }
        pr[0] = c[0]; pr[1] = c[1]; pr[2] = c[2];
        t += 0.01;
    }
    return len;
}
/* FUN_180331070: the continuation point of E along the circle the direction dir and the neighbour Q suggest: d = E - Q (length L, unit e);
   the plane normal n = dir x e; |n| >= 1e-6: in the view of n (find_view_k) the angle of e reflected about dir (2 a1 - a2), at distance L
   (the view z of e kept); else straight on along e */
static void reflect_pt(const double E[3], const double dir[3], const double Q[3], double o[3]) {
    double d[3] = { E[0] - Q[0], E[1] - Q[1], E[2] - Q[2] };
    double L = sqrt(d[1] * d[1] + d[0] * d[0] + d[2] * d[2]), e[3];
    if (L == 0.0) { e[0] = e[1] = e[2] = 0.0; } else { e[0] = d[0] / L; e[1] = d[1] / L; e[2] = d[2] / L; }
    double n[3] = { dir[1] * e[2] - dir[2] * e[1], dir[2] * e[0] - dir[0] * e[2], dir[0] * e[1] - dir[1] * e[0] };
    double nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (1e-06 <= nl) {
        n[0] /= nl; n[1] /= nl; n[2] /= nl;
        double M[9]; mcsb_view_k(n, M);   /* find_view_k(n, .., create 1): a view with z = n [ours: make_view_k's frame; the angles are taken
                                               about n, so any frame with that z gives the same point] */
        double dv[3], ev[3];
        for (int r = 0; r < 3; r++) { dv[r] = M[3 * r] * dir[0] + M[3 * r + 1] * dir[1] + M[3 * r + 2] * dir[2]; ev[r] = M[3 * r] * e[0] + M[3 * r + 1] * e[1] + M[3 * r + 2] * e[2]; }
        double a1 = 0.0, a2 = 0.0;
        if (1e-12 <= dv[1] * dv[1] + dv[0] * dv[0]) { a1 = atan2(dv[1], dv[0]); if (a1 < 0.0) a1 += 6.283185307179586; }
        if (1e-12 <= ev[1] * ev[1] + ev[0] * ev[0]) { a2 = atan2(ev[1], ev[0]); if (a2 < 0.0) a2 += 6.283185307179586; }
        double a = a1 * 2.0 - a2, v[3] = { cos(a) * L, sin(a) * L, ev[2] }, w[3];
        for (int r = 0; r < 3; r++) w[r] = M[r] * v[0] + M[3 + r] * v[1] + M[6 + r] * v[2];   /* view_to_world */
        o[0] = w[0] + E[0]; o[1] = w[1] + E[1]; o[2] = w[2] + E[2];
    } else { o[0] = E[0] + e[0] * L; o[1] = E[1] + e[1] * L; o[2] = E[2] + e[2] * L; }
}
/* FUN_180332680: the quadratic P re-made over its parameters 0 .. r (r > 1 extends it): the end points C0 and the point at r, the middle
   control point where the end tangents meet (ll_ints_3d; else the middle of the ends); rational: the middle weight = the arc's
   sin(half angle) when the weights differ and P is a circular arc (FUN_18032d020 / FUN_180331820), the weight at r when they differ and
   it is not, else 1 */
static int circ_arc(const double *P) {   /* FUN_180331820 */
    double c0[3], c1[3], c2[3]; cart(1, P, c0); cart(1, P + 4, c1); cart(1, P + 8, c2);
    double a[3] = { c0[0] - c1[0], c0[1] - c1[1], c0[2] - c1[2] }, b[3] = { c2[0] - c1[0], c2[1] - c1[1], c2[2] - c1[2] };
    unit3(a); unit3(b);
    double w = sqrt((1.0 - (b[0] * a[0] + b[1] * a[1] + b[2] * a[2])) * 0.5) - P[4];
    return fabs(w) < 0.0001;
}
static void requad(double r, int rat, double *P, double tol) {
    int n = rat ? 4 : 3;
    double D0[4], Pr[4], Dr[4], C0[3], T0[3], Cr[3], Tr[3], p1[3], p2[3], M[3];
    qbez(1, n, P, 0.0, D0); qbez(0, n, P, r, Pr); qbez(1, n, P, r, Dr);
    cart(rat, P, C0); cart_d(rat, P, D0, T0);
    double E0[3] = { T0[0] + C0[0], T0[1] + C0[1], T0[2] + C0[2] };
    cart(rat, Pr, Cr); cart_d(rat, Pr, Dr, Tr);
    double Er[3] = { Cr[0] + Tr[0], Cr[1] + Tr[1], Cr[2] + Tr[2] };
    if (ll_ints_3d(C0, E0, Cr, Er, tol, p1, p2)) { for (int k = 0; k < 3; k++) M[k] = (p2[k] + p1[k]) * 0.5; }
    else for (int k = 0; k < 3; k++) M[k] = (Cr[k] + C0[k]) * 0.5;
    if (!rat) { memcpy(P, C0, 24); memcpy(P + 3, M, 24); memcpy(P + 6, Cr, 24); return; }
    double w = 1.0;
    if (fabs(P[0] - P[4]) > 0.0001 || fabs(P[0] - P[8]) > 0.0001) {   /* FUN_18032d020 */
        if (circ_arc(P)) {
            double a[3] = { C0[0] - M[0], C0[1] - M[1], C0[2] - M[2] }, b[3] = { Cr[0] - M[0], Cr[1] - M[1], Cr[2] - M[2] };
            unit3(a); unit3(b);
            w = sqrt((1.0 - ((b[1] * a[1] + b[0] * a[0]) + b[2] * a[2])) * 0.5);
        } else w = Pr[0];
    }
    P[0] = 1.0; P[1] = C0[0]; P[2] = C0[1]; P[3] = C0[2];
    P[4] = w; P[5] = M[0] * w; P[6] = M[1] * w; P[7] = M[2] * w;
    P[8] = 1.0; P[9] = Cr[0]; P[10] = Cr[1]; P[11] = Cr[2];
}
/* FUN_180331a30 (Non-linear): from the end point E of three control points (at = 0: the last of P[0..2], continuing after it; 1: P[0],
   continuing before it) a quadratic [E, the reflection of the neighbour through E, the second neighbour's direction reflected about the
   first (FUN_180331070)], rescaled (FUN_180332680) until its 100-chord length is len (tol; it stops when the length stops changing, < 1e-8,
   or the start tangent turns back), then raised to degree p (FUN_18032cf50); the p new points after E */
#define MSE_NONLIN_CAP 100000   /* ours: FUN_180331a30's loop has no bound; a quadratic of (almost) zero length makes it oscillate forever in
                                   mccore.dll too (peloader --ext, a corpus case at a pole: L 4e-7 <-> 3800 ...). Past this count the
                                   extension fails here instead of hanging (user-approved deviation S-10, formats/user_decisions.md) */
static int ext_nonlin(int at, const double *P, int rat, int p, double *out, double len, double tol) {
    int n = rat ? 4 : 3;
    const double *E = at == 0 ? P + 2 * n : P; int step = at == 0 ? -n : n;
    double B[12]; memcpy(B, E, sizeof(double) * n);
    double CE[3]; cart(rat, E, CE);
    double dir[3] = { 0, 0, 0 };
    const double *Q = E;
    for (int s = 0; s < 2; s++) {
        Q += step;
        double CQ[3], nw[3]; cart(rat, Q, CQ);
        if (s == 0) {
            double d[3] = { CE[0] - CQ[0], CE[1] - CQ[1], CE[2] - CQ[2] };
            nw[0] = CE[0] + d[0]; nw[1] = CE[1] + d[1]; nw[2] = CE[2] + d[2];
            double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l == 0.0) dir[0] = dir[1] = dir[2] = 0.0; else { dir[0] = d[0] / l; dir[1] = d[1] / l; dir[2] = d[2] / l; }
        } else reflect_pt(CE, dir, CQ, nw);
        double *o = B + n * (s + 1);
        if (rat) { double w = Q[0]; o[0] = w; o[1] = nw[0] * w; o[2] = nw[1] * w; o[3] = nw[2] * w; }
        else { o[0] = nw[0]; o[1] = nw[1]; o[2] = nw[2]; }
    }
    double L = qlen(n, B), D[4], T0[3];
    qbez(1, n, B, 0.0, D); cart_d(rat, B, D, T0);
    for (long it = 0;; it++) {
        if (it >= MSE_NONLIN_CAP) return 0;
        if (!(fabs(len - L) > tol)) break;
        double prev = L;
        requad(len / L, rat, B, tol);
        L = qlen(n, B);
        if (fabs(L - prev) < 1e-08) break;
        double T[3]; qbez(1, n, B, 0.0, D); cart_d(rat, B, D, T);
        if (!(0.0 <= T0[1] * T[1] + T0[0] * T[0] + T0[2] * T[2])) { requad(len / prev, rat, B, tol); break; }
    }
    if (p == 2) { memcpy(out, B + n, sizeof(double) * 2 * n); return 1; }
    double *cur = calloc((size_t)(p + 1) * n, sizeof(double)), *nx = calloc((size_t)(p + 1) * n, sizeof(double));
    memcpy(cur, B, sizeof(double) * 3 * n);
    for (int dg = 3; dg <= p; dg++) {   /* FUN_18032cf50: degree dg - 1 -> dg */
        int q = dg - 1;
        memcpy(nx, cur, sizeof(double) * n);
        for (int i = 1; i <= q; i++) { double a = (double)i / (double)(q + 1), b = 1.0 - (double)i / (double)(q + 1);
            for (int k = 0; k < n; k++) nx[i * n + k] = a * cur[(i - 1) * n + k] + b * cur[i * n + k]; }   /* FUN_180332480 */
        memcpy(nx + (q + 1) * n, cur + q * n, sizeof(double) * n);
        memcpy(cur, nx, sizeof(double) * (size_t)(q + 2) * n);
    }
    memcpy(out, cur + n, sizeof(double) * (size_t)p * n);
    free(cur); free(nx);
    return 1;
}
/* FUN_180332080 (Linear): p points evenly along the end leg of the control polygon (at = 0: after B = P[1] along B - A; 1: before
   A = P[0] against it), the last at distance len; rational: the end point's weight */
static void ext_lin(int at, const double *P, int rat, int p, double *out, double len) {
    double A[3], B[3], W = 0;
    if (!rat) { A[0] = P[0]; A[1] = P[1]; A[2] = P[2]; B[0] = P[3]; B[1] = P[4]; B[2] = P[5]; }
    else { double ib = 1.0 / P[4], ia = 1.0 / P[0];
        B[0] = ib * P[5]; B[1] = ib * P[6]; B[2] = ib * P[7]; A[0] = ia * P[1]; A[1] = ia * P[2]; A[2] = ia * P[3];
        W = at == 0 ? P[4] : P[0]; }
    double d[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (l == 0.0) d[0] = d[1] = d[2] = 0.0; else { d[0] /= l; d[1] /= l; d[2] /= l; }
    const double *base = at == 0 ? B : A;
    int n = rat ? 4 : 3;
    for (int k = 1; k <= p; k++) {
        double t = (double)k / (double)p, s = at == 0 ? t * len : t * -1.0 * len;
        double x = d[0] * s + base[0], y = d[1] * s + base[1], z = d[2] * s + base[2];
        double *o = out + (size_t)(k - 1) * n;
        if (rat) { o[0] = W; o[1] = x * W; o[2] = y * W; o[3] = z * W; } else { o[0] = x; o[1] = y; o[2] = z; }
    }
}

/* ------------------------------------------------------------------------------------------------ FUN_18032d240 */
uint8_t *mse_extend_nurbs(const uint8_t *rec, uint32_t n, int dir, int end, double len, const double *plane, int linear, double tol,
                          double uv8[8], uint32_t *on, int *err) {
    *err = 2;
    if (n < 0x118 || rec[0x68] != 0) return NULL;
    int nku = r32(rec + 0x100), nkv = r32(rec + 0x104), ncu = r32(rec + 0x108), ncv = r32(rec + 0x10c), d = rec[0x110], p = rec[0x111], q = rec[0x112];
    int rat = !(rec[0x113] & 1);
    if (nku <= 0 || nkv <= 0 || ncu <= 1 || ncv <= 1 || p < 1 || q < 1 || (d != 3 && d != 4) || (rat && d != 4)
        || 0x118 + 8 * ((size_t)nku + nkv + (size_t)d * ncu * ncv) > n) return NULL;
    const uint8_t *S = rec + 0x68;
    double u0 = rd(S + 8), u1 = rd(S + 0x10), v0 = rd(S + 0x18), v1 = rd(S + 0x20);
    double *K = malloc(sizeof(double) * (size_t)(nku + nkv)), *C = malloc(sizeof(double) * (size_t)d * ncu * ncv);
    for (int i = 0; i < nku + nkv; i++) K[i] = rd(rec + 0x118 + 8 * (size_t)i);
    for (size_t i = 0; i < (size_t)d * ncu * ncv; i++) C[i] = rd(rec + 0x118 + 8 * ((size_t)nku + nkv + i));
    int nku2 = nku, nkv2 = nkv, ncu2 = ncu, ncv2 = ncv;
    if (dir == 0) { nku2 += p; ncu2 += p; } else { nkv2 += q; ncv2 += q; }
    double *K2 = calloc((size_t)(nku2 + nkv2 + 2 * (p + q) + 4), sizeof(double)), *C2 = calloc((size_t)d * ncu2 * ncv2, sizeof(double));
    int pl = plane != NULL; double L = pl ? 1.0 : len;
    *err = 1;
    /* the knots and the strip corners (param_10) */
    if (dir == 0 && end == 0) {
        uv8[0] = u0; uv8[1] = v0; uv8[2] = u0; uv8[3] = v1; uv8[4] = u0 - 1.0; uv8[5] = v0; uv8[6] = u0 - 1.0; uv8[7] = v1;
        for (int i = 0; i <= p; i++) K2[i] = K[i] - 1.0;
        for (int i = 1; i < nku + nkv; i++) K2[i + p] = K[i];
        u0 = u0 - 1.0;
    } else if (dir == 0) {
        uv8[0] = u1; uv8[1] = v0; uv8[2] = u1; uv8[3] = v1; uv8[4] = u1 + 1.0; uv8[5] = v0; uv8[6] = u1 + 1.0; uv8[7] = v1;
        int i0 = nku - 1; double kl = K[i0];
        while (i0 > 0 && fabs(K[i0 - 1] - kl) < tol) i0--;   /* the decompile's "|k - last| <= tol && != tol" */
        i0--;
        for (int i = 0; i <= i0; i++) K2[i] = K[i];
        for (int i = i0 + 1; i <= i0 + p; i++) K2[i] = kl;
        for (int i = i0 + p + 1; i <= i0 + 2 * p + 1; i++) K2[i] = kl + 1.0;
        for (int i = 0; i < nkv; i++) K2[i0 + 2 * p + 2 + i] = K[nku + i];
        u1 = u1 + 1.0;
    } else if (end == 0) {
        uv8[0] = u0; uv8[1] = v0; uv8[2] = u1; uv8[3] = v0; uv8[4] = u0; uv8[5] = v0 - 1.0; uv8[6] = u1; uv8[7] = v0 - 1.0;
        for (int i = 0; i < nku; i++) K2[i] = K[i];
        for (int i = nku; i <= nku + q; i++) K2[i] = K[i] - 1.0;
        for (int i = nku + q + 1; i < nku + nkv + q; i++) K2[i] = K[i - q];
        v0 = v0 - 1.0;
    } else {
        uv8[0] = u0; uv8[1] = v1; uv8[2] = u1; uv8[3] = v1; uv8[4] = u0; uv8[5] = v1 + 1.0; uv8[6] = u1; uv8[7] = v1 + 1.0;
        for (int i = 0; i < nku + nkv - 1; i++) K2[i] = K[i];
        for (int i = nku + nkv - 1; i < nku + nkv + q; i++) K2[i] = K[nku + nkv - 1] + 1.0;
        v1 = v1 + 1.0;
    }
    /* the control points, row by row (u) or column by column (v) */
    int deg = dir == 0 ? p : q, cnt = dir == 0 ? ncu : ncv, lines = dir == 0 ? ncv : ncu;
    int nonlin = !linear && cnt > 2 && deg > 1;
    double *in = malloc(sizeof(double) * 3 * d), *nw = malloc(sizeof(double) * (size_t)deg * d);
    for (int j = 0; j < lines; j++) {
#define CP(k) (dir == 0 ? C + (size_t)d * ((size_t)j * ncu + (k)) : C + (size_t)d * ((size_t)(k) * ncu + j))
#define CP2(k) (dir == 0 ? C2 + (size_t)d * ((size_t)j * ncu2 + (k)) : C2 + (size_t)d * ((size_t)(k) * ncu2 + j))
        if (pl) {   /* FUN_180331530 x 2 + FUN_1803315e0: the end leg (inner -> end point) against the plane, t >= 0; the length = |end - hit| */
            const double *pa = end == 0 ? CP(1) : CP(cnt - 2), *pb = end == 0 ? CP(0) : CP(cnt - 1);
            double A[3], B[3], X[3], t; cart(rat, pa, A); cart(rat, pb, B);
            if (!lp_ints(plane + 3, plane, A, B, tol, X, &t) || !(0.0 <= t)) { free(in); free(nw); free(K); free(C); free(K2); free(C2); return NULL; }
            L = sqrt((B[0] - X[0]) * (B[0] - X[0]) + (B[1] - X[1]) * (B[1] - X[1]) + (B[2] - X[2]) * (B[2] - X[2]));
        }
        int k0 = end == 0 ? 0 : cnt - (nonlin ? 3 : 2), kn = nonlin ? 3 : 2;
        for (int k = 0; k < kn; k++) memcpy(in + (size_t)k * d, CP(k0 + k), sizeof(double) * d);
        if (nonlin) { if (!ext_nonlin(end == 0 ? 1 : 0, in, rat, deg, nw, L, tol)) { free(in); free(nw); free(K); free(C); free(K2); free(C2); *err = 4; return NULL; } }
        else ext_lin(end == 0 ? 1 : 0, in, rat, deg, nw, L);
        if (end == 0) {   /* the new points before the old, nearest first reversed */
            for (int k = 0; k < deg; k++) memcpy(CP2(k), nw + (size_t)(deg - 1 - k) * d, sizeof(double) * d);
            for (int k = 0; k < cnt; k++) memcpy(CP2(deg + k), CP(k), sizeof(double) * d);
        } else {
            for (int k = 0; k < cnt; k++) memcpy(CP2(k), CP(k), sizeof(double) * d);
            for (int k = 0; k < deg; k++) memcpy(CP2(cnt + k), nw + (size_t)k * d, sizeof(double) * d);
        }
#undef CP
#undef CP2
    }
    free(in); free(nw);
    *on = 0x118 + 8 * (uint32_t)((size_t)nku2 + nkv2 + (size_t)d * ncu2 * ncv2);
    uint8_t *R = calloc(1, *on);
    if (!R) { free(K); free(C); free(K2); free(C2); *err = 3; return NULL; }
    memcpy(R, rec, 0x68);                       /* the entity header: the caller sets the class / attributes (put_surf_in_db) */
    uint8_t *S2 = R + 0x68;
    S2[0] = 0; S2[1] = S[1]; S2[2] = S[2];
    uint16_t fl; memcpy(&fl, S + 6, 2); fl = (uint16_t)((fl & 0xfffc & 0xfffb) | 0x40); memcpy(S2 + 6, &fl, 2);
    wd(S2 + 8, u0); wd(S2 + 0x10, u1); wd(S2 + 0x18, v0); wd(S2 + 0x20, v1);
    w32(R + 0x100, nku2); w32(R + 0x104, nkv2); w32(R + 0x108, ncu2); w32(R + 0x10c, ncv2);
    R[0x110] = (uint8_t)d; R[0x111] = (uint8_t)p; R[0x112] = (uint8_t)q; R[0x113] = (uint8_t)(rec[0x113] & 7);
    for (int i = 0; i < nku2 + nkv2; i++) wd(R + 0x118 + 8 * (size_t)i, K2[i]);
    for (size_t i = 0; i < (size_t)d * ncu2 * ncv2; i++) wd(R + 0x118 + 8 * ((size_t)nku2 + nkv2 + i), C2[i]);
    free(K); free(C); free(K2); free(C2);
    *err = 0;
    return R;
}

/* ------------------------------------------------------------------------------------------------ the self-intersection test */
/* FUN_18032d170: eval_surf mode 1 + calc_surf_unit_norm (Su x Sv, unit), reversed with the header's +6 bit 2 */
static int unit_normal(const uint8_t *rec, uint32_t n, double u, double v, double N[3]) {
    double o[6][3]; if (mcsb_nurbs_ders(rec, n, u, v, o)) return 0;
    N[0] = o[1][1] * o[2][2] - o[1][2] * o[2][1]; N[1] = o[1][2] * o[2][0] - o[1][0] * o[2][2]; N[2] = o[1][0] * o[2][1] - o[1][1] * o[2][0];
    unit3(N);
    if (rec[0x6e] & 4) { N[0] = -N[0]; N[1] = -N[1]; N[2] = -N[2]; }
    return 1;
}
static int steps02(double du, double dv) {   /* round(|d| / 0.2), at least 1 */
    double x = sqrt(du * du + dv * dv) / 0.2;
    x = 0.0 <= x ? floor(x + 0.5) : ceil(x - 0.5);
    short s = (short)(int)x; return s == 0 ? 1 : s;
}
/* FUN_1803331a0: the normals along a -> b (steps of about 0.2 in (u, v)); 0 when one turns against the previous one */
static int seg_normals(const uint8_t *rec, uint32_t n, const double a[2], const double b[2], double prev[3]) {
    double du = b[0] - a[0], dv = b[1] - a[1]; int s = steps02(du, dv);
    double su = du * (1.0 / (double)s), sv = (1.0 / (double)s) * dv, u = a[0], v = a[1]; int first = 0;
    for (int k = 0; k <= s; k++) {
        double N[3];
        if (unit_normal(rec, n, u, v, N)) {
            if (first && N[1] * prev[1] + N[0] * prev[0] + N[2] * prev[2] < 0.0) return 0;
            prev[0] = N[0]; prev[1] = N[1]; prev[2] = N[2]; first = 1;
        }
        u += su; v += sv;
    }
    return 1;
}
static int edge_tan(const uint8_t *rec, uint32_t n, double u, double v, int idx, double t[3]) {
    double o[6][3]; if (mcsb_nurbs_ders(rec, n, u, v, o)) { t[0] = t[1] = t[2] = 0; return 0; }
    t[0] = o[idx][0]; t[1] = o[idx][1]; t[2] = o[idx][2]; unit3(t); return 1;
}
int mse_selfint(const uint8_t *rec, uint32_t n, const double uv8[8], int dir) {
    /* FUN_18032cd00 (args A = param_10[0], C = [4], D = [6], B = [2]): the normals along A -> D -> B -> C */
    const double *pts[4] = { uv8, uv8 + 6, uv8 + 2, uv8 + 4 };
    double prev[3] = { 0, 0, 0 };
    for (int s = 0; s < 3; s++) if (!seg_normals(rec, n, pts[s], pts[s + 1], prev)) return 1;   /* status 0: the question */
    /* FUN_18032c670 (A, B, C, D, dir): the turning of the edge tangent (Sv for a u edge, Su for a v edge) summed along A -> B and C -> D;
       more than pi apart: the question */
    const double *A = uv8, *B = uv8 + 2, *Cc = uv8 + 4, *D = uv8 + 6;
    int idx = dir == 0 ? 2 : 1, s = steps02(D[0] - Cc[0], D[1] - Cc[1]);
    double inv = 1.0 / (double)s, scd[2] = { (D[0] - Cc[0]) * inv, (D[1] - Cc[1]) * inv }, sab[2] = { (B[0] - A[0]) * inv, (B[1] - A[1]) * inv };
    double c[2] = { Cc[0], Cc[1] }, a[2] = { A[0], A[1] }, tc[3], ta[3], sumc = 0.0, suma = 0.0;
    edge_tan(rec, n, c[0], c[1], idx, tc); edge_tan(rec, n, a[0], a[1], idx, ta);
    for (int k = 1; k <= s; k++) {
        c[0] += scd[0]; c[1] += scd[1]; a[0] += sab[0]; a[1] += sab[1];
        double t1[3], t2[3]; edge_tan(rec, n, c[0], c[1], idx, t1);
        sumc += acos(t1[0] * tc[0] + t1[1] * tc[1] + t1[2] * tc[2]);
        edge_tan(rec, n, a[0], a[1], idx, t2);
        suma += acos(t2[0] * ta[0] + t2[1] * ta[1] + t2[2] * ta[2]);
        if (3.141592653589793 < fabs(sumc - suma)) return 1;
        memcpy(tc, t1, sizeof tc); memcpy(ta, t2, sizeof ta);
    }
    return 0;
}

/* tests (tools/tests/srfext_test.c dbg): FUN_180331a30 and FUN_1803316b0 alone */
int mse_dbg_nonlin(int at, const double *P, int rat, int p, double *out, double len, double tol) { return ext_nonlin(at, P, rat, p, out, len, tol); }
double mse_dbg_qlen(int n, const double *P) { return qlen(n, P); }

/* ------------------------------------------------------------------------------------------------ the edge (FUN_180338540) */
void mse_edge_untrimmed(const uint8_t *rec, const double uv[2], int *dir, int *end) {
    const uint8_t *S = rec + 0x68;
    double u0 = rd(S + 8), u1 = rd(S + 0x10), v0 = rd(S + 0x18), v1 = rd(S + 0x20), vm = (v1 + v0) * 0.5, u = uv[0], v = uv[1];
    if (u - (u1 + u0) * 0.5 <= 0.0) {
        double fu = (u - u0) / (u1 - u0);
        if (v - vm <= 0.0) { if ((v - v0) / (v1 - v0) <= fu) { *dir = 1; *end = 0; return; } }
        else if ((v1 - v) / (v1 - v0) <= fu) { *dir = 1; *end = 1; return; }
        *dir = 0; *end = 0; return;
    }
    double gu = (u1 - u) / (u1 - u0);
    if (0.0 < v - vm) { if (gu < (v1 - v) / (v1 - v0)) { *dir = 0; *end = 1; return; } *dir = 1; *end = 1; return; }
    if (gu < (v - v0) / (v1 - v0)) { *dir = 0; *end = 1; return; }
    *dir = 1; *end = 0;
}
