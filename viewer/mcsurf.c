/* mcsurf.c - positions on Mastercam's curve and surface entities, as mccore evaluates them (eval_curve / eval_surf,
 * decomp/surfkinds/evalsurf.c). Used to mesh the surface kinds that are not NURBS (occt_mesh_surf builds them from these
 * positions) and the trimmed surfaces on such bases. Records are the entities as read (mcam_load.c Surf / CurveRec);
 * references between entities are eptrs (ent +0x20) in the file, as get_ent_from_eptr loads them.
 * Only positions (eval_surf / eval_curve with 0 derivatives) are ported. */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "mcsurf.h"
#include "occt_mesh.h"

static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int64_t rd64(const uint8_t *p) { int64_t v; memcpy(&v, p, 8); return v; }

/* kind 1 (FUN_1805eb9c0, evalsurf.c l.1124; pps_type at ent +0x100): i32 nu, nv patches, u8 u order (+8), v order (+9); the
   coefficients follow the 0x10-byte struct at ent +0x110 (measured: the record length): per patch (u index fastest, then v)
   v order x u order x 3 doubles, power basis, [v power][u power][xyz]. The global u picks the patch floor(u) (clamped to the last,
   the rest added to the local parameter), the local u = u - floor(u); likewise v. Horner in u then v. */
static int eval_pps(const uint8_t *r, uint32_t n, double u, double v, double p[3]) {
    if (n < 0x110) return -1;
    int nu = rd32(r + 0x100), nv = rd32(r + 0x104), ou = r[0x108], ov = r[0x109];
    if (nu < 1 || nv < 1 || ou < 1 || ov < 1 || 0x110 + 8 * (size_t)ou * ov * 3 * nu * nv > n) return -1;
    double fu = u >= 0 ? floor(u) : ceil(u); int iu = (int)fu; if (u >= 0) u -= iu;
    if (iu >= 0) { if (iu > nu - 1) { u += iu - nu + 1; iu = nu - 1; } } else iu = 0;
    double fv = v >= 0 ? floor(v) : ceil(v); int iv = (int)fv; if (v >= 0) v -= iv;
    if (iv >= 0) { if (iv > nv - 1) { v += iv - nv + 1; iv = nv - 1; } } else iv = 0;
    size_t per = (size_t)ou * ov * 3;
    const uint8_t *c = r + 0x110 + 8 * (per * ((size_t)nu * iv + iu));
    double acc[3] = { 0, 0, 0 };
    for (int j = ov - 1; j >= 0; j--) {
        double q[3] = { 0, 0, 0 };
        for (int i = ou - 1; i >= 0; i--) for (int k = 0; k < 3; k++) q[k] = q[k] * u + rdd(c + 8 * (3 * ((size_t)j * ou + i) + k));
        for (int k = 0; k < 3; k++) acc[k] = acc[k] * v + q[k];
    }
    memcpy(p, acc, sizeof acc); return 0;
}

/* kind 2 (eval_revsurf 0x18047a110, evalsurf.c l.3287; rev_type at ent +0x100): +0x00 axis direction, +0x18 a point of the axis,
   +0x30 / +0x38 start / end angle, +0x48 the profile curve (eptr). C = eval_curve(profile, u); its foot on the axis F = A + d
   ((C - A) . d); with r = C - F the point is F + r cos v + (d x r) sin v (v = the angle in radians). */
static int eval_rev(const McamFile *f, const uint8_t *r, uint32_t n, double u, double v, double p[3]) {
    if (n < 0x150) return -1;
    const CurveRec *c = mcs_curve(f, rd64(r + 0x148)); if (!c) return -1;
    double C[3]; if (mcs_curve_eval(f, c, u, C)) return -1;
    double d[3], A[3]; for (int k = 0; k < 3; k++) { d[k] = rdd(r + 0x100 + 8 * k); A[k] = rdd(r + 0x118 + 8 * k); }
    double h = (C[0] - A[0]) * d[0] + (C[1] - A[1]) * d[1] + (C[2] - A[2]) * d[2], F[3], R[3];
    for (int k = 0; k < 3; k++) { F[k] = d[k] * h + A[k]; R[k] = C[k] - F[k]; }
    double X[3] = { d[1] * R[2] - d[2] * R[1], d[2] * R[0] - d[0] * R[2], d[0] * R[1] - d[1] * R[0] }, cs = cos(v), sn = sin(v);
    for (int k = 0; k < 3; k++) p[k] = R[k] * cs + F[k] + X[k] * sn;
    return 0;
}

/* kind 0 (NURBS; formats/mcam.md "NURBS surface", as occt_mesh.cpp nurbs_surface): de Boor in u then v on the stored knots,
   homogeneous points, u index fastest */
static int eval_nurbs_surf(const uint8_t *r, uint32_t n, double u, double v, double p[3]) {
    if (n < 0x118) return -1;
    int nku = rd32(r + 0x100), nkv = rd32(r + 0x104), ncu = rd32(r + 0x108), ncv = rd32(r + 0x10c), vpp = r[0x110], du = r[0x111], dv = r[0x112], rat = !(r[0x113] & 1);
    if (du < 1 || dv < 1 || du > 25 || dv > 25 || ncu < du + 1 || ncv < dv + 1 || nku != ncu + du + 1 || nkv != ncv + dv + 1 || (vpp != 3 && vpp != 4)
        || 0x118 + 8 * ((size_t)nku + nkv + (size_t)vpp * ncu * ncv) > n) return -1;
    const uint8_t *KU = r + 0x118, *KV = KU + 8 * nku, *C = KV + 8 * nkv;
    #define K(A, i) rdd((A) + 8 * (size_t)(i))
    if (u < K(KU, du)) u = K(KU, du); if (u > K(KU, ncu)) u = K(KU, ncu);
    if (v < K(KV, dv)) v = K(KV, dv); if (v > K(KV, ncv)) v = K(KV, ncv);
    int su = du; while (su < ncu - 1 && !(u < K(KU, su + 1))) su++;
    int sv = dv; while (sv < ncv - 1 && !(v < K(KV, sv + 1))) sv++;
    double row[26][4];
    for (int jj = 0; jj <= dv; jj++) {
        int j = sv - dv + jj; double d[26][4];
        for (int ii = 0; ii <= du; ii++) {
            const uint8_t *q = C + 8 * (size_t)vpp * ((size_t)j * ncu + (su - du + ii));
            if (vpp == 4) { double w = rat ? rdd(q) : 1.0; d[ii][3] = w; for (int k = 0; k < 3; k++) d[ii][k] = rdd(q + 8 * (1 + k)); }
            else { d[ii][3] = 1.0; for (int k = 0; k < 3; k++) d[ii][k] = rdd(q + 8 * k); }
        }
        for (int rr = 1; rr <= du; rr++) for (int ii = du; ii >= rr; ii--) {
            int i = su - du + ii; double a0 = K(KU, i), a1 = K(KU, i + du - rr + 1), a = a1 > a0 ? (u - a0) / (a1 - a0) : 0;
            for (int k = 0; k < 4; k++) d[ii][k] = (1 - a) * d[ii - 1][k] + a * d[ii][k];
        }
        memcpy(row[jj], d[du], sizeof row[jj]);
    }
    for (int rr = 1; rr <= dv; rr++) for (int jj = dv; jj >= rr; jj--) {
        int j = sv - dv + jj; double a0 = K(KV, j), a1 = K(KV, j + dv - rr + 1), a = a1 > a0 ? (v - a0) / (a1 - a0) : 0;
        for (int k = 0; k < 4; k++) row[jj][k] = (1 - a) * row[jj - 1][k] + a * row[jj][k];
    }
    #undef K
    double w = row[dv][3]; if (!(fabs(w) > 1e-300)) return -1;
    for (int k = 0; k < 3; k++) p[k] = row[dv][k] / w;   /* non-rational: w = 1 */
    return 0;
}

/* kind 4 (FUN_18042cad0, evalsurf.c l.2737; at ent +0x100): +0 i16 mode (1 linear in v, 2 the Hermite blend below), +4 i32 curves
   (rows), +8 i32 u slots per row, +0xc i32 records; the tail after the 0x10-byte struct (ent +0x110, formats/mcam.md "kind 4 loft:
   +0x10, (n*m + k*8)*4"): rows x slots i32 record indices, then 0x20-byte records {f64 u0, f64 s, eptr curve, 8 bytes}. Row r of
   slot j = trunc(u) (clamped) runs records idx[r*slots+j] .. the next slot's (the record count for the last); the last record with
   u0 <= u (else the first) gives the curve parameter t = (s > 0 ? t1 : t2) + (u - u0) s (curve_t1t2; a point: t 0). Rows: i = trunc(v)
   (the last row less one, >= 0), w = v - i. Mode 1: (1 - w) Q_i + w Q_i+1; mode 2: the spans of l.2973-3080. Layout of the stored
   index array: [inferred] from the size formula (the decompile reads it through a pointer). */
static int eval_loft(const McamFile *f, const uint8_t *r, uint32_t n, double u, double v, double p[3]) {
    if (n < 0x110) return -1;
    int16_t mode; memcpy(&mode, r + 0x100, 2);
    int nc = rd32(r + 0x104), ns = rd32(r + 0x108), nrec = rd32(r + 0x10c);
    if (nc < 2 || ns < 1 || nrec < 1 || 0x110 + 4 * (size_t)nc * ns + 0x20 * (size_t)nrec > n || (mode != 1 && mode != 2)) return -1;
    const uint8_t *idx = r + 0x110, *rec = idx + 4 * (size_t)nc * ns;
    int j = (int)u; if (j > ns - 1) j = ns - 1; if (j < 0) j = 0;
    int i = (int)v; if (i == nc - 1) i--; if (i < 0) i = 0; double w = v - i;
    double Q[4][3]; int have[4] = { 0, 0, 0, 0 };
    for (int q = 0; q < 4; q++) {
        int row = i - 1 + q; if (row < 0 || row >= nc) continue;
        if (mode == 1 && (q == 0 || q == 3)) continue;
        int slot = row * ns + j, a = rd32(idx + 4 * (size_t)slot), b = slot + 1 < nc * ns ? rd32(idx + 4 * (size_t)(slot + 1)) : nrec;
        if (a < 0 || a >= nrec || b <= a) return -1;
        int k = a; for (int t = b - 1; t >= a; t--) if (rdd(rec + 0x20 * (size_t)t) <= u) { k = t; break; }
        const uint8_t *R = rec + 0x20 * (size_t)k;
        const CurveRec *c = mcs_curve(f, rd64(R + 0x10)); if (!c) return -1;
        double t1, t2, tt; mcs_spline_t1t2(c, &t1, &t2);
        double sl = rdd(R + 8); tt = c->cls == 0x1 ? 0 : (sl > 0 ? t1 : t2) + (u - rdd(R)) * sl;
        if (mcs_curve_eval(f, c, tt, Q[q])) return -1;
        have[q] = 1;
    }
    double c0, c1, c2, c3;
    if (mode == 1) { for (int k = 0; k < 3; k++) p[k] = (1 - w) * Q[1][k] + w * Q[2][k]; return 0; }
    #define DST(A, B) sqrt((Q[A][0] - Q[B][0]) * (Q[A][0] - Q[B][0]) + (Q[A][1] - Q[B][1]) * (Q[A][1] - Q[B][1]) + (Q[A][2] - Q[B][2]) * (Q[A][2] - Q[B][2]))
    const double tol = 1e-9;   /* the decompile's tol (a global) is NOT DETERMINED; a near-zero guard here */
    if (!have[0] && have[3]) {          /* first span (l.2973-3002): Q1 = Q_i, Q2 = Q_i+1, Q3 = Q_i+2 */
        double dd = DST(3, 1), a = dd < tol ? 0 : DST(2, 1) / dd;
        for (int k = 0; k < 3; k++) p[k] = Q[1][k] * (1 + (a - 2) * w + (1 - a) * w * w) + Q[2][k] * (2 * w - w * w) + Q[3][k] * a * (w * w - w);
        return 0;
    }
    if (have[0] && !have[3]) {          /* last span (l.3004-3033) */
        double dd = DST(2, 0), c = dd < tol ? 0 : DST(2, 1) / dd;
        for (int k = 0; k < 3; k++) p[k] = Q[0][k] * c * (w * w - w) + Q[1][k] * (1 - w * w) + Q[2][k] * (c * w + (1 - c) * w * w);
        return 0;
    }
    if (!have[0] && !have[3]) return -1;   /* mode 2 with two curves: NOT DETERMINED */
    { double da = DST(2, 0), db = DST(3, 1), a = da < tol ? 0 : DST(2, 1) / da, b = db < tol ? 0 : DST(2, 1) / db;   /* middle span (l.3036-3080) */
      c0 = -a * w * (1 - w) * (1 - w); c1 = 1 + (b - 3) * w * w + (2 - b) * w * w * w; c2 = a * w + (3 - 2 * a) * w * w + (a - 2) * w * w * w; c3 = b * (w * w * w - w * w);
      for (int k = 0; k < 3; k++) p[k] = c0 * Q[0][k] + c1 * Q[1][k] + c2 * Q[2][k] + c3 * Q[3][k]; }
    #undef DST
    return 0;
}

/* kind 6 (FUN_180412490, evalsurf.c l.329; at ent +0x100): +0 vector N, +0x18 f64 d, +0x20 the curve (eptr). C = eval_curve(u), C'
   its derivative (here a central difference - ours), Dir = unit(C' x N) ((-N/|N|) x C', l.465/484-503; v_z_3d when degenerate, l.509 =
   (0, 0, 0): zero in the image, never written, decision S-3). |d| < 1e12: P = C + v (N - d |N| Dir); else P = C + v sign(d) |N| Dir (l.474-479, 756, 795). */
static int eval_draft(const McamFile *f, const uint8_t *r, uint32_t n, double u, double v, double p[3]) {
    if (n < 0x128) return -1;
    const CurveRec *c = mcs_curve(f, rd64(r + 0x120)); if (!c) return -1;
    double C[3], a[3], b[3], t1, t2; mcs_spline_t1t2(c, &t1, &t2);
    if (mcs_curve_eval(f, c, u, C)) return -1;
    double h = (t2 - t1) * 1e-6; if (!(fabs(h) > 0)) h = 1e-6;
    if (mcs_curve_eval(f, c, u + h, a) || mcs_curve_eval(f, c, u - h, b)) return -1;
    double D1[3] = { (a[0] - b[0]) / (2 * h), (a[1] - b[1]) / (2 * h), (a[2] - b[2]) / (2 * h) }, N[3], dd = rdd(r + 0x118);
    for (int k = 0; k < 3; k++) N[k] = rdd(r + 0x100 + 8 * k);
    double nl = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]); if (!(nl > 0)) return -1;
    double m[3] = { -N[0] / nl, -N[1] / nl, -N[2] / nl };
    double Dir[3] = { m[1] * D1[2] - m[2] * D1[1], m[2] * D1[0] - m[0] * D1[2], m[0] * D1[1] - m[1] * D1[0] };
    double dl = sqrt(Dir[0] * Dir[0] + Dir[1] * Dir[1] + Dir[2] * Dir[2]);
    if (fabs(Dir[0]) < 1e-12 && fabs(Dir[1]) < 1e-12 && fabs(Dir[2]) < 1e-12) { Dir[0] = 0; Dir[1] = 0; Dir[2] = 0; }   /* v_z_3d = (0, 0, 0) (decision S-3) */
    else for (int k = 0; k < 3; k++) Dir[k] /= dl;
    if (fabs(dd) < 1e12) for (int k = 0; k < 3; k++) p[k] = C[k] + v * (N[k] - dd * nl * Dir[k]);
    else { double sg = dd < 0 ? -1 : 1; for (int k = 0; k < 3; k++) p[k] = C[k] + v * sg * nl * Dir[k]; }
    return 0;
}

/* kind 7 (FUN_18041c730, evalsurf.c l.1311 + FUN_1803fca60 l.4645; at ent +0x100): curves C0, C1, C2 (eptrs +0, +8, +0x10) at u; n =
   unit((C1 - C0) x (C2 - C0)) (|n| < 1e-12: P = C0); r = C1 - C0, cos = ((C2 - C0) . r) / |r|^2 (acos, 0 / pi past +-0.999999999999);
   P = C0 + Rot(r, n, theta v) (Rodrigues, FUN_180114560 mccore_filter.c l.3895) */
static int eval_arcsweep(const McamFile *f, const uint8_t *r, uint32_t n, double u, double v, double p[3]) {
    if (n < 0x118) return -1;
    double C[3][3];
    for (int i = 0; i < 3; i++) { const CurveRec *c = mcs_curve(f, rd64(r + 0x100 + 8 * i)); if (!c || mcs_curve_eval(f, c, u, C[i])) return -1; }
    double A[3], B[3]; for (int k = 0; k < 3; k++) { A[k] = C[1][k] - C[0][k]; B[k] = C[2][k] - C[0][k]; }
    double nn[3] = { A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0] }, l = sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
    if (!(l >= 1e-12)) { memcpy(p, C[0], sizeof C[0]); return 0; }
    for (int k = 0; k < 3; k++) nn[k] /= l;
    double rr = A[0] * A[0] + A[1] * A[1] + A[2] * A[2]; if (!(rr > 0)) { memcpy(p, C[0], sizeof C[0]); return 0; }
    double cs = (B[0] * A[0] + B[1] * A[1] + B[2] * A[2]) / rr, th = cs > 0.999999999999 ? 0 : cs < -0.999999999999 ? M_PI : acos(cs), an = th * v;
    double dn = nn[0] * A[0] + nn[1] * A[1] + nn[2] * A[2], P[3], X[3];
    for (int k = 0; k < 3; k++) P[k] = A[k] - dn * nn[k];
    X[0] = nn[1] * P[2] - nn[2] * P[1]; X[1] = nn[2] * P[0] - nn[0] * P[2]; X[2] = nn[0] * P[1] - nn[1] * P[0];
    for (int k = 0; k < 3; k++) p[k] = C[0][k] + dn * nn[k] + cos(an) * P[k] + sin(an) * X[k];
    return 0;
}

static int mcs_surf_eval_d(const McamFile *f, const Surf *s, double u, double v, double p[3], int depth);
/* kind 8 (FUN_180460cd0, evalsurf.c l.1911 + find_base_surf l.5262; at ent +0x100): +0x18 f64 offset, +0x20 the base surface (eptr).
   Nested offsets add up, a trimmed base is followed to its own base (no offset); P = S_base(u, v) + total N / |N| with N = Su x Sv
   (calc_surf_norm, no orientation flag). Su / Sv here: central differences over a millionth of the range (ours). */
static int eval_offset(const McamFile *f, const Surf *s, double u, double v, double p[3], int depth) {
    double tot = 0; const Surf *b = s;
    for (int g = 0; g < 32; g++) {
        if (b->kind == 8 && b->n >= 0x128) { tot += rdd(b->rec + 0x118); const Surf *nb = mcs_surf(f, rd64(b->rec + 0x120)); if (!nb || nb == b) return -1; b = nb; }
        else if (b->kind == 10 && b->n >= 0x108) { const Surf *nb = mcs_surf(f, rd64(b->rec + 0x100)); if (!nb || nb == b) return -1; b = nb; }
        else break;
    }
    if (b->kind == 8 || b->kind == 10) return -1;
    double d[4]; mcs_surf_domain(b, d);
    double hu = (d[1] - d[0]) * 1e-6, hv = (d[3] - d[2]) * 1e-6, P0[3], a[3], c[3], e[3], g2[3];
    if (!(hu > 0) || !(hv > 0)) return -1;
    if (mcs_surf_eval_d(f, b, u, v, P0, depth + 1)) return -1;
    double u1 = u + hu > d[1] ? u : u + hu, u0 = u - hu < d[0] ? u : u - hu, v1 = v + hv > d[3] ? v : v + hv, v0 = v - hv < d[2] ? v : v - hv;
    if (mcs_surf_eval_d(f, b, u1, v, a, depth + 1) || mcs_surf_eval_d(f, b, u0, v, c, depth + 1) ||
        mcs_surf_eval_d(f, b, u, v1, e, depth + 1) || mcs_surf_eval_d(f, b, u, v0, g2, depth + 1)) return -1;
    double Su[3], Sv[3]; for (int k = 0; k < 3; k++) { Su[k] = (a[k] - c[k]) / (u1 - u0); Sv[k] = (e[k] - g2[k]) / (v1 - v0); }
    double N[3] = { Su[1] * Sv[2] - Su[2] * Sv[1], Su[2] * Sv[0] - Su[0] * Sv[2], Su[0] * Sv[1] - Su[1] * Sv[0] }, l = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
    if (!(l > 0)) return -1;
    for (int k = 0; k < 3; k++) p[k] = P0[k] + tot * N[k] / l;
    return 0;
}

int mcs_surf_eval(const McamFile *f, const Surf *s, double u, double v, double p[3]) { return mcs_surf_eval_d(f, s, u, v, p, 0); }
static int mcs_surf_eval_d(const McamFile *f, const Surf *s, double u, double v, double p[3], int depth) {
    if (depth > 16) return -1;
    switch (s->kind) {
    case 0: return eval_nurbs_surf(s->rec, s->n, u, v, p);
    case 1: return eval_pps(s->rec, s->n, u, v, p);
    case 2: return eval_rev(f, s->rec, s->n, u, v, p);
    case 4: return eval_loft(f, s->rec, s->n, u, v, p);
    case 6: return eval_draft(f, s->rec, s->n, u, v, p);
    case 7: return eval_arcsweep(f, s->rec, s->n, u, v, p);
    case 8: return eval_offset(f, s, u, v, p, depth);
    case 10: { const Surf *b = s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL; return b && b != s && b->kind != 10 ? mcs_surf_eval_d(f, b, u, v, p, depth + 1) : -1; }
    default: return -1;
    }
}
/* the surface's parameter range: ent +0x70 u0, u1, v0, v1 (the base range a trimmed surface's uv points are in, MaFace::MaFace;
   equal to the knot range of a NURBS surface and to 0..nu / 0..nv of a kind 1, the angles of a kind 2 - measured) */
void mcs_surf_domain(const Surf *s, double d[4]) { for (int k = 0; k < 4; k++) d[k] = s->n >= 0x90 ? rdd(s->rec + 0x70 + 8 * k) : 0.0; }
int mcs_surf_kind_ok(const Surf *s) { int k = s->kind; return k == 0 || k == 1 || k == 2 || k == 4 || k == 6 || k == 7 || k == 8 || k == 10; }

/* the surface's triangles (and boundary segments): occt_mesh_surf with the base of a trimmed surface and these positions for the
   kinds that are not NURBS */
typedef struct { const McamFile *f; const Surf *s; } McsCtx;
static int mcs_ev(void *c, double u, double v, double p[3]) { McsCtx *x = c; return mcs_surf_eval(x->f, x->s, u, v, p); }
int mcs_mesh(const McamFile *f, const Surf *s, double defl, double **tri, double **seg, int *nseg, char *err, size_t errn) {
    const Surf *b = s->kind == 10 && s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL;
    McsCtx x = { f, b ? b : s };
    return occt_mesh_surf(s->rec, s->n, b ? b->rec : NULL, b ? b->n : 0, mcs_ev, &x, defl, tri, seg, nseg, err, errn);
}

/* ------------------------------------------------------------------------------------------------ wireframe display */
/* surf_division (mccore 0x1806f9530, decomp/surfdraw/surfdiv_core.c l.3): the flowline spacing (*s3) and sample step (*s4) for a pass
   (pass 1: u = const lines sampled along v; pass 0: v = const lines along u). The surf header is ent +0x68 (h): +8 u0, +0x10 u1,
   +0x18 v0, +0x20 v1, +0x70 a size, +0x78 / +0x80 the u / v curvature; the base of an offset / trimmed surface gives them
   (find_base_surf). wk = get_wk_height (the view's height in model units), xs / ys = get_x_scr_scl / get_y_scr_scl (pixels per
   unit), tol = the system tolerance (config 1000). Counts by wk against the size: > 20 size 1 / 3, > 12 size 2 / 6, > 4 size 4 / 12,
   else 8 / 36 (12 / 36 when the size >= 4 wk). A direction whose curvature is <= tol: range / n1, x 2 (the spaced lines) or x 0.5
   (the samples); else sqrt(3.2 / (curvature * scale)) capped at half the range, x 4 for the lines, at least range / count, rounded
   to whole divisions. Kind 1 with > 20 patches: x 0.25; kind 4: v sample 1, u spacing 1 / ceil(1 / s); kind 7 pass 0: v 0.5. A
   density (ent +0x69) > 1 divides both. */
static const Surf *base_for_division(const McamFile *f, const Surf *s) {
    const Surf *b = s;
    for (int g = 0; g < 32 && b; g++) {
        if (b->kind == 8 && b->n >= 0x128) b = mcs_surf(f, rd64(b->rec + 0x120));
        else if (b->kind == 10 && b->n >= 0x108) b = mcs_surf(f, rd64(b->rec + 0x100));
        else return b;
    }
    return NULL;
}
int mcs_surf_division(const McamFile *f, const Surf *s, int pass, double wk, double xs, double ys, double tol, double *s3, double *s4) {
    *s3 = *s4 = 0.1;
    const Surf *b = base_for_division(f, s);
    if (!b || b->n < 0xf0 || s->n < 0xf0) return 0;
    const uint8_t *h = b->rec + 0x68;
    double size = rdd(s->rec + 0x68 + 0x70);
    int n1, n2;
    if (wk > size * 20) { n1 = 1; n2 = 3; } else if (wk > size * 12) { n1 = 2; n2 = 6; } else if (wk > size * 4) { n1 = 4; n2 = 12; }
    else { n2 = 0x24; n1 = wk * 4 <= size ? 0xc : 8; }
    double u0 = rdd(h + 8), u1 = rdd(h + 0x10), v0 = rdd(h + 0x18), v1 = rdd(h + 0x20), cu = rdd(h + 0x78), cv = rdd(h + 0x80);
    double ur = u1 - u0, vr = v1 - v0, a, c;
    if (cu <= tol) { a = ur / n1 * (pass == 0 ? 0.5 : 2.0); }
    else { double sc = pass == 0 ? ys : xs; a = sqrt(3.2 / (cu * sc)); if (ur * 0.5 <= a) a = ur * 0.5;
           int nn = n2; if (pass == 1) { a *= 4; nn = n1; } if (a <= ur / nn) a = ur / nn; a = ur / ceil(ur / a); }
    if (cv <= tol) { c = vr / n1 * (pass != 1 ? 2.0 : 0.5); }
    else { double sc = pass == 1 ? ys : xs; c = sqrt(3.2 / (cv * sc)); if (vr * 0.5 <= c) c = vr * 0.5;
           int nn = n2; if (pass == 0) { c *= 4; nn = n1; } if (c <= vr / nn) c = vr / nn; c = vr / ceil(vr / c); }
    if (b->kind == 1) { int nu = rd32(b->rec + 0x100), nv = rd32(b->rec + 0x104); if (pass == 0 && nu > 0x14) a *= 0.25; else if (pass == 1 && nv > 0x14) c *= 0.25; }
    else if (b->kind == 4) { if (pass == 0) c = 1.0; else { double d = ceil(1.0 / a); a = 1.0 / d; } }
    else if (b->kind == 7 && pass == 0) c = 0.5;
    int dens = s->rec[0x69];
    if (dens > 1) { a /= dens; c /= dens; }
    *s3 = a; *s4 = c;
    return 1;
}

typedef struct { float *p; int np, cap; int *cnt; int nc, ccap; } PolyBuf;
static void pb_pt(PolyBuf *b, const double q[3]) {
    if (b->np == b->cap) { b->cap = b->cap ? b->cap * 2 : 1024; b->p = realloc(b->p, sizeof(float) * 3 * b->cap); }
    b->p[3 * b->np] = (float)q[0]; b->p[3 * b->np + 1] = (float)q[1]; b->p[3 * b->np + 2] = (float)q[2]; b->np++;
}
static void pb_end(PolyBuf *b, int n) {
    if (n < 2) { b->np -= n; return; }
    if (b->nc == b->ccap) { b->ccap = b->ccap ? b->ccap * 2 : 256; b->cnt = realloc(b->cnt, sizeof(int) * b->ccap); }
    b->cnt[b->nc++] = n;
}
/* one iso line: u = c (along v) or v = c (along u), from a to b sampling every step, the last sample at b (FUN_180238320 loops) */
static void iso_line(const McamFile *f, const Surf *g, int along_v, double c, double a, double b, double step, PolyBuf *out) {
    if (!(step > 0)) return;
    int n = 0; double t = a;
    for (int k = 0; k < 100000; k++) {
        int last = 0; if (t >= b) { t = b; last = 1; }
        double p[3]; if (!mcs_surf_eval(f, g, along_v ? c : t, along_v ? t : c, p)) { pb_pt(out, p); n++; }
        if (last) break;
        t += step;
    }
    pb_end(out, n);
}
/* the wireframe of an untrimmed surface (FUN_180238320 mastercam.dll, decomp/surfdraw/srfdraw.c l.180): pass 1 the u = const lines at
   the surf_division spacing from u0 (the last at u1), pass 0 the v = const lines; full = 0 (render_cfg 4 and ent +0x6e bits 0 / 1
   clear: the outline of shaded mode) spaces them a whole range apart (the boundary), a density 0 half a range; kind 7: the u sample
   step at most 2 in pass 0. */
static void wire_untrimmed(const McamFile *f, const Surf *s, int full, double wk, double xs, double ys, double tol, PolyBuf *out) {
    const uint8_t *h = s->rec + 0x68; double u0 = rdd(h + 8), u1 = rdd(h + 0x10), v0 = rdd(h + 0x18), v1 = rdd(h + 0x20), a, c;
    if (mcs_surf_division(f, s, 1, wk, xs, ys, tol, &a, &c)) {
        if (full) { if (h[1] == 0) a = (u1 - u0) * 0.5; } else a = u1 - u0;
        if (a > 0) for (double u = u0;; u += a) { int last = u >= u1; if (last) u = u1; iso_line(f, s, 1, u, v0, v1, c, out); if (last) break; }
    }
    if (mcs_surf_division(f, s, 0, wk, xs, ys, tol, &a, &c)) {
        if (full) { if (h[1] == 0) c = (v1 - v0) * 0.5; } else c = v1 - v0;
        if (s->kind == 7 && a > 2.0) a = 2.0;
        if (c > 0) for (double v = v0;; v += c) { int last = v >= v1; if (last) v = v1; iso_line(f, s, 0, v, u0, u1, a, out); if (last) break; }
    }
}
/* a trimmed surface's loops in uv (formats/mcam.md "Trimmed surface"); pts / starts / counts point into the record */
static int trim_loops(const Surf *s, int *m, int *nl) {
    if (s->n < 0x138) return 0;
    *m = rd32(s->rec + 0x10c); *nl = rd32(s->rec + 0x128);
    return *m > 0 && *nl > 0 && 0x138 + 16 * (size_t)*m + 8 * (size_t)*nl <= s->n;
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
/* even-odd inside test of (u, v) against all loops (in_polygon) */
static int in_loops(const Surf *s, int m, int nl, double u, double v) {
    const uint8_t *t = s->rec + 0x138; int in = 0;
    for (int l = 0; l < nl; l++) {
        int st = rd32(t + 16 * (size_t)m + 8 * l), cn = rd32(t + 16 * (size_t)m + 8 * l + 4);
        for (int i = st; i <= st + cn && i + 1 <= st + cn + 1 && i + 1 < m + 1; i++) {
            int j = i + 1 > st + cn ? st : i + 1;
            double a[2], b[2]; memcpy(a, t + 16 * (size_t)i, 16); memcpy(b, t + 16 * (size_t)j, 16);
            if ((a[1] > v) != (b[1] > v) && u < a[0] + (v - a[1]) * (b[0] - a[0]) / (b[1] - a[1])) in = !in;
        }
    }
    return in;
}
/* the wireframe of a trimmed surface (FUN_180238ee0 mastercam.dll, srfdraw.c l.899): the loops (FUN_1802389d0: here the stored uv
   points through the base - not decoded further, ours), then per pass the iso lines from min(u0 of the trimmed and of the base) one
   spacing in (a density 0 or a spacing over 0.6 of the range: one line at the middle), each cut by the loops (hit_polygon: the crossings
   along the line, at most 256, clipped to the range; an odd count drops the first crossing when the middle of the first pair is
   outside (in_polygon), else the last when the middle of the last pair is outside) and drawn between pairs of crossings. */
static void wire_trimmed(const McamFile *f, const Surf *s, double wk, double xs, double ys, double tol, PolyBuf *out) {
    const Surf *g = s->n >= 0x108 ? mcs_surf(f, rd64(s->rec + 0x100)) : NULL; int m, nl;
    if (!g || g == s || g->kind == 10 || !trim_loops(s, &m, &nl)) return;
    const uint8_t *t = s->rec + 0x138, *h = s->rec + 0x68, *gh = g->rec + 0x68;
    for (int l = 0; l < nl; l++) {   /* the loops */
        int st = rd32(t + 16 * (size_t)m + 8 * l), cn = rd32(t + 16 * (size_t)m + 8 * l + 4), n = 0;
        if (st < 0 || st + cn >= m) continue;
        for (int i = st; i <= st + cn + 1; i++) { int k = i > st + cn ? st : i; double uv[2], p[3]; memcpy(uv, t + 16 * (size_t)k, 16);
            if (!mcs_surf_eval(f, g, uv[0], uv[1], p)) { pb_pt(out, p); n++; } }
        pb_end(out, n);
    }
    double u0 = rdd(h + 8), u1 = rdd(h + 0x10), v0 = rdd(h + 0x18), v1 = rdd(h + 0x20);
    for (int pass = 1; pass >= 0; pass--) {
        double a, c; if (!mcs_surf_division(f, s, pass, wk, xs, ys, tol, &a, &c)) continue;
        double lo = pass ? fmin(u0, rdd(gh + 8)) : fmin(v0, rdd(gh + 0x18)), hi = pass ? fmax(u1, rdd(gh + 0x10)) : fmax(v1, rdd(gh + 0x20));
        double sp = pass ? a : c, smp = pass ? c : a, r0 = pass ? u0 : v0, r1 = pass ? u1 : v1, q0 = pass ? v0 : u0, q1 = pass ? v1 : u1;
        if (!(sp > 0) || !(smp > 0)) continue;
        for (double x = lo + sp; ; x += sp) {
            int single = s->rec[0x69] == 0 || sp > (r1 - r0) * 0.6;
            if (single) x = (r0 + r1) * 0.5;
            if (!single && x >= hi) break;
            double hit[256]; int nh = 0;
            for (int l = 0; l < nl; l++) {
                int st = rd32(t + 16 * (size_t)m + 8 * l), cn = rd32(t + 16 * (size_t)m + 8 * l + 4);
                for (int i = st; i <= st + cn; i++) {
                    int j = i + 1 > st + cn ? st : i + 1; double A[2], B[2]; memcpy(A, t + 16 * (size_t)i, 16); memcpy(B, t + 16 * (size_t)j, 16);
                    double ax = pass ? A[0] : A[1], bx = pass ? B[0] : B[1], ay = pass ? A[1] : A[0], by = pass ? B[1] : B[0];
                    if ((ax > x) != (bx > x) && nh < 256) hit[nh++] = ay + (x - ax) * (by - ay) / (bx - ax);
                }
            }
            qsort(hit, nh, sizeof(double), cmpd);
            int k0 = 0; for (int k = 0; k < nh; k++) { if (hit[k] < q0) hit[k] = q0; if (hit[k] > q1) hit[k] = q1; }
            if (nh & 1) {
                double mu = (hit[0] + hit[1]) * 0.5;
                if (!in_loops(s, m, nl, pass ? x : mu, pass ? mu : x)) k0 = 1;
                else { double ml = (hit[nh - 1] + hit[nh - 2]) * 0.5; if (!in_loops(s, m, nl, pass ? x : ml, pass ? ml : x)) nh--; }
            }
            for (int k = k0; k + 1 < nh; k += 2) iso_line(f, g, pass, x, hit[k], hit[k + 1], smp, out);
            if (single) break;
        }
    }
}
int mcs_wireframe(const McamFile *f, const Surf *s, int shaded_outline, double wk, double xs, double ys, double tol, float **pts, int **cnt, int *npoly) {
    PolyBuf b = { 0 };
    if (s->kind == 10) wire_trimmed(f, s, wk, xs, ys, tol, &b);
    else {
        uint16_t fl; memcpy(&fl, s->rec + 0x6e, 2);
        int full = !(shaded_outline && !(fl & 1) && !(fl & 2));
        wire_untrimmed(f, s, full, wk, xs, ys, tol, &b);
    }
    *pts = b.p; *cnt = b.cnt; *npoly = b.nc;
    return b.nc;
}
