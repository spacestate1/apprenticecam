/* mcspline_chain.c - the engine of Mastercam's Spline From Curves: one chain of lines, arcs, splines and NURBS curves made into one spline. mccore.dll make_nurbs_from_chains
 * (0x180262750, default kind) and make_spline_from_chain (0x1806604a0, parametric kind), cited by function name. No UI, no database. Oracle: tools/tests/spline_chain_oracle.sh
 * (the real functions under tools/peloader with the database calls hooked). See MCSC_DLL_UNDEFINED in mcspline_chain.h for the inputs on which the DLL's own result is undefined. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mcspline_chain.h"
#include "mcsurf.h"

static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
#define S(n) ((double)(n))

/* ---- geometry helpers */
/* view_to_world (mastercam.dll 0x180341520): views < 2 identity, else world = M^T v */
static void vtw(const McscEnt *e, const double v[3], double w[3]) {
    if (!e->vm) { w[0] = v[0]; w[1] = v[1]; w[2] = v[2]; return; }
    const double *m = e->vm;
    for (int k = 0; k < 3; k++) w[k] = v[0] * m[k] + v[1] * m[3 + k] + v[2] * m[6 + k];
}
/* viewz (mastercam.dll 0x341aa0): the view's z row (view +0x38 / +0x40 / +0x48, the third row of the matrix stored at +0x08); view 1 is the identity */
static void vz(const McscEnt *e, double n[3]) {
    if (!e->vm) { n[0] = 0.0; n[1] = 0.0; n[2] = 1.0; return; }
    n[0] = e->vm[6]; n[1] = e->vm[7]; n[2] = e->vm[8];
}
static CurveRec crec(const McscEnt *e) { CurveRec c; memset(&c, 0, sizeof c); c.cls = e->cls; c.rec = (uint8_t *)e->rec; c.n = e->n; return c; }
static void nb_eval(const uint8_t *r, int nd, double t, double o[3][3]);
/* eval_curve (mccore 0x1803fcd10) positions / first derivative of line (class 2), arc (class 4), spline / NURBS (mcs_spline_eval_d) */
static void ent_pt_tan(const McscEnt *e, double t, double p[3], double tan_[3]) {
    double o[3][3] = { { 0 } };
    if (e->cls == 2) { double u = 1.0 - t; for (int k = 0; k < 3; k++) { o[0][k] = u * e->a[k] + t * e->b[k]; o[1][k] = e->b[k] - e->a[k]; } }
    else if (e->cls == 4) {
        double a = t * e->sw + e->a0, co = cos(a) * e->r, si = sin(a) * e->r, v[3] = { co + e->c[0], si + e->c[1], e->c[2] };
        vtw(e, v, o[0]);
        double v1[3] = { -si * e->sw, co * e->sw, 0.0 };
        vtw(e, v1, o[1]);
    } else if (e->cls == 0x80) {
        double q[3][3];
        if (p) { nb_eval(e->rec, 0, t, q); memcpy(o[0], q[0], 24); }
        if (tan_) { nb_eval(e->rec, 1, t, q); memcpy(o[1], q[1], 24); }
    } else { CurveRec c = crec(e); mcs_spline_eval_d(&c, t, 1, o); }
    if (p) memcpy(p, o[0], 24);
    if (tan_) memcpy(tan_, o[1], 24);
}
static void ent_pt(const McscEnt *e, double t, double p[3]) { ent_pt_tan(e, t, p, NULL); }   /* pt_on_ent_from_t (0x180018d20) */
static void ent_tan(const McscEnt *e, double t, double d[3]) { ent_pt_tan(e, t, NULL, d); }  /* tan_on_ent_from_t (0x1800195a0): eval_curve 1, the derivative */
/* FUN_18000dcf0... : curve_endpoints (0x1803fc830) reads ent +0x68 / +0x80 for classes 2, 4, 8, 0x80 */
static void ent_ends(const McscEnt *e, double a[3], double b[3]) { memcpy(a, e->a, 24); memcpy(b, e->b, 24); }

/* ---- eval_nurbs (mccore 0x1805ae2d0): viewer/mcspline.c mcs_eval_nurbs (moved there 2026-11-12, Curve Surface) */
static void nb_eval(const uint8_t *r, int nd, double t, double o[3][3]) { mcs_eval_nurbs(r, nd, t, o); }
/* nb_parm_curvature (0x1805b0300): eval_nurbs with the derivatives scaled by the knot range (param_6 = 1), then by 1 / (t2 - t1) (record +0x98 / +0xa0): |d1 x d2| / |d1|, 0 when |d1| <= 1e-6 */
double mcsc_nb_curvature(const uint8_t *r, double tt) {
    double o[3][3], t1 = rdd(r + 0x98), t2 = rdd(r + 0xa0);
    int nk = rd32(r + 0xc8);
    const double *K = (const double *)(r + 0xd8);
    double kr = K[nk - 1] - K[0], kr2 = kr * kr, sc = 1.0 / (t2 - t1), sc2 = sc * sc;
    nb_eval(r, 2, tt, o);
    double e1x = o[1][0] * kr, e1y = o[1][1] * kr, e1z = o[1][2] * kr;
    double e0 = o[2][0] * kr2, e1 = o[2][1] * kr2, e2 = o[2][2] * kr2;
    double d3 = e1x * sc, d8 = e1y * sc, d7 = e1z * sc, d1 = e2 * sc2;
    double d5 = sqrt(d7 * d7 + d8 * d8 + d3 * d3);
    if (d5 <= 1e-06) return 0.0;
    double d2 = e1 * sc2 * d3 - e0 * sc2 * d8, d8b = d8 * d1 - e1 * sc2 * d7, d7b = e0 * sc2 * d7 - d3 * d1;
    return sqrt(d7b * d7b + d8b * d8b + d2 * d2) / d5;
}
/* ---- NURBS piece of one entity (FUN_180260910 + the arc / line / spline conversions) */
/* growable output arrays */
typedef struct { double *cp, *k; int capcp, capk; int ncp, nk; } Piece;
static int rcp(Piece *p, int n) { if (n <= p->capcp) return 0; int c = n + 1024; double *x = realloc(p->cp, sizeof(double) * c); if (!x) return -1; p->cp = x; p->capcp = c; return 0; }
static int rk(Piece *p, int n) { if (n <= p->capk) return 0; int c = n + 1024; double *x = realloc(p->k, sizeof(double) * c); if (!x) return -1; p->k = x; p->capk = c; return 0; }

/* FUN_18025f4f0(param_1 = nnew, *n, K): stitch the knots of the next piece at K[*n]: drops its knots' first / previous last 4 (the d = K[*n-1] shift): K[*n + l - 5] = d + K[*n + l], l = 1 .. nnew-1;
   *n += nnew - 5. Empty piece or *n = 0: *n += nnew. */
static void stitch_knots(int nnew, int *n, double *K) {
    int iVar2 = *n;
    if (iVar2 != 0 && nnew != 0) {
        double d = K[iVar2 - 1];
        for (int l = 1; l < nnew; l++) K[iVar2 + l - 5] = d + K[iVar2 + l];
        *n = iVar2 + nnew - 5;
        return;
    }
    *n = iVar2 + nnew;
}
/* FUN_1802633a0(ncp, nk, width, cp, K): reverse: swaps the control points, knots K'[i] = Klast - K[nk-1-i] */
static void reverse_piece(int ncp, int nk, int w, double *cp, double *K) {
    int hi = w * (ncp - 1), lo = 0;
    for (int i = 0; i < ncp / 2; i++, hi -= 2 * w)
        for (int j = 0; j < w; j++) { double t = cp[lo]; cp[lo] = cp[hi]; cp[hi] = t; lo++; hi++; }
    int j = nk - 1, i = 0;
    double d = K[j];
    for (int c = 0; c < (nk + (nk & 1)) / 2; c++, i++, j--) {
        if (i == j) K[i] = d - K[i];
        else { double t = K[i]; K[i] = d - K[j]; K[j] = d - t; }
    }
}
/* FUN_180260c70: a line as a cubic Bezier, (w) x y z, knots 0 x4, 1 x4 */
static void line_nurbs(const double a[3], const double b[3], int rational, double *cp, double *K, int *ncp, int *nk) {
    int d = 0;
    *ncp = 4; *nk = 8;
    if (rational) cp[d++] = 1.0;
    for (int k = 0; k < 3; k++) cp[d++] = a[k];
    if (rational) cp[d++] = 1.0;
    for (int k = 0; k < 3; k++) cp[d++] = (a[k] * 2.0 + b[k]) / 3.0;
    if (rational) cp[d++] = 1.0;
    for (int k = 0; k < 3; k++) cp[d++] = (b[k] * 2.0 + a[k]) / 3.0;
    if (rational) cp[d++] = 1.0;
    for (int k = 0; k < 3; k++) cp[d++] = b[k];
    K[0] = K[1] = K[2] = K[3] = 0.0; K[4] = K[5] = K[6] = K[7] = 1.0;
}
/* a_3d fields used by the arc conversions: centre (view), radius, start, sweep */
typedef struct { const McscEnt *e; double a0, sw; } ArcV;
/* FUN_180261890: one rational cubic arc segment (sw given), w x y z; |sw| < 0.0001: nothing */
static void arc_seg(const ArcV *v, double *cp, double *K, int *ncp, int *nk) {
    const McscEnt *e = v->e;
    if (fabs(v->sw) < 0.0001) { *nk = 0; *ncp = 0; return; }
    *ncp = 4; *nk = 8;
    K[0] = K[1] = K[2] = K[3] = 0.0; K[4] = K[5] = K[6] = K[7] = 1.0;
    double r = e->r, c[3], s[3], p[3];
    double q[3];
    q[0] = cos(v->a0) * r + e->c[0]; q[1] = sin(v->a0) * r + e->c[1]; q[2] = e->c[2];
    vtw(e, q, s);                                      /* local_a0, 98, 90 the start */
    double a = v->a0 + v->sw;
    q[0] = cos(a) * r + e->c[0]; q[1] = sin(a) * r + e->c[1]; q[2] = e->c[2];
    vtw(e, q, p);                                      /* local_b8, b0, a8 the end */
    vtw(e, e->c, c);                                   /* local_d0, c8, c0 the centre */
    double dVar6 = p[1] - c[1], dVar7 = s[2] - c[2], dVar1 = p[2] - c[2], dVar8 = s[1] - c[1], dVar2 = p[0] - c[0];
    double dVar3 = (dVar6 * dVar8 + dVar2 * (s[0] - c[0]) + dVar1 * dVar7) / (r * r) + 1.0;
    double dVar4 = sqrt(dVar3 * 0.5);
    cp[0] = 1.0; cp[1] = s[0]; cp[2] = s[1]; cp[3] = s[2];
    dVar4 = dVar4 * 2.0;
    dVar3 = 1.0 / dVar3;
    double dVar5 = (dVar4 + 1.0) / 3.0;
    dVar2 = dVar4 * (c[0] + (dVar2 + (s[0] - c[0])) * dVar3);
    dVar6 = dVar4 * ((dVar6 + dVar8) * dVar3 + c[1]);
    cp[4] = dVar5;
    dVar4 = dVar4 * ((dVar1 + dVar7) * dVar3 + c[2]);
    cp[6] = (s[1] + dVar6) / 3.0;
    cp[5] = (s[0] + dVar2) / 3.0;
    cp[7] = (s[2] + dVar4) / 3.0;
    cp[8] = dVar5;
    cp[9] = (p[0] + dVar2) / 3.0;
    cp[10] = (p[1] + dVar6) / 3.0;
    cp[11] = (p[2] + dVar4) / 3.0;
    cp[12] = 1.0; cp[13] = p[0]; cp[14] = p[1]; cp[15] = p[2];
}
/* FUN_18025ffb0: an arc of a whole number of quadrants (sw near 90, 180, 270, 360 degrees): sqrt(2) rational segments, rotating about the view z */
static void arc_quadrants(const ArcV *v, double *p, double *K, int *ncp, int *nk) {
    const McscEnt *e = v->e;
    double dVar7 = sqrt(2.0), dVar9 = fabs(v->sw), r = e->r;
    int bVar2 = 5.026548245743669 < dVar9, bVar3 = dVar9 < 2.827433388230814, bVar4 = (!bVar2 && !(dVar9 <= 3.455751918948773)), bVar5 = 0;
    K[0] = K[1] = K[2] = K[3] = 0.0;
    if (bVar2 || bVar4 || bVar3) {
        if (bVar2) {
            *ncp = 13; *nk = 17;
            K[9] = K[8] = K[7] = 2.0; K[12] = K[11] = K[10] = 3.0; K[16] = K[15] = K[14] = K[13] = 4.0;
        } else if (bVar4) {
            *ncp = 10; *nk = 14;
            K[9] = K[8] = K[7] = 2.0; K[13] = K[12] = K[11] = K[10] = 3.0;
        } else {
            *ncp = 4; *nk = 8;
            K[7] = 1.0;
        }
    } else {
        *ncp = 7; *nk = 11;
        K[10] = K[9] = K[8] = K[7] = 2.0;
        bVar5 = 1;
    }
    K[4] = 1.0; K[5] = 1.0; K[6] = 1.0;
    double d9 = v->a0, q[3], s[3], c[3], n[3];
    q[0] = cos(d9) * r + e->c[0]; q[1] = sin(d9) * r + e->c[1]; q[2] = e->c[2];
    vtw(e, q, s);                          /* local_178, 170, 168 */
    vtw(e, e->c, c);                       /* local_110, 108, 100 */
    vz(e, n);                              /* local_f8, f0, e8 */
    p[0] = 1.0;
    double d15 = s[1] - c[1], d17 = s[2] - c[2], d13 = s[0] - c[0];
    double d19 = n[1] * d17 - n[2] * d15, d8 = n[2] * d13 - n[0] * d17, d9c = n[0] * d15 - n[1] * d13;
    double d1 = (dVar7 + 1.0) / 3.0, d12 = d19 + c[0], d10 = d8 + c[1], d11 = d9c + c[2];
    p[1] = s[0]; p[2] = s[1]; p[3] = s[2]; p[4] = d1;
    double d14 = (d19 + s[0]) * dVar7, d16 = (d8 + s[1]) * dVar7, d18 = (d9c + s[2]) * dVar7;
    p[5] = (d14 + s[0]) / 3.0; p[6] = (s[1] + d16) / 3.0; p[7] = (s[2] + d18) / 3.0;
    p[8] = d1;
    p[9] = (d12 + d14) / 3.0; p[10] = (d10 + d16) / 3.0; p[11] = (d11 + d18) / 3.0;
    p[12] = 1.0; p[13] = d12; p[14] = d10; p[15] = d11;
    if (bVar3) return;
    p[16] = d1;
    d13 = c[0] - d13; d15 = c[1] - d15; d17 = c[2] - d17;
    d14 = (d19 + d13) * dVar7; d16 = (d8 + d15) * dVar7; d18 = (d9c + d17) * dVar7;
    p[17] = (d14 + d12) / 3.0; p[18] = (d16 + d10) / 3.0; p[19] = (d18 + d11) / 3.0;
    p[20] = d1;
    p[21] = (d14 + d13) / 3.0; p[22] = (d16 + d15) / 3.0; p[23] = (d18 + d17) / 3.0;
    p[24] = 1.0; p[25] = d13; p[26] = d15; p[27] = d17;
    if (bVar5) return;
    double c1 = c[1] - d8, c2 = c[2] - d9c, c0 = c[0] - d19;
    p[28] = d1;
    d10 = (d13 - d19) * dVar7; d11 = (d15 - d8) * dVar7; d12 = (d17 - d9c) * dVar7;
    p[29] = (d10 + d13) / 3.0; p[30] = (d11 + d15) / 3.0; p[31] = (d12 + d17) / 3.0;
    p[32] = d1;
    p[33] = (d10 + c0) / 3.0; p[34] = (d11 + c1) / 3.0; p[35] = (d12 + c2) / 3.0;
    p[36] = 1.0; p[37] = c0; p[38] = c1; p[39] = c2;
    if (bVar4) return;
    p[40] = d1;
    d13 = (s[0] - d19) * dVar7; d8 = (s[1] - d8) * dVar7; dVar7 = (s[2] - d9c) * dVar7;
    p[41] = (d13 + c0) / 3.0; p[42] = (d8 + c1) / 3.0; p[43] = (dVar7 + c2) / 3.0;
    p[44] = d1;
    p[46] = (d8 + s[1]) / 3.0; p[47] = (dVar7 + s[2]) / 3.0; p[45] = (d13 + s[0]) / 3.0;
    p[48] = 1.0; p[49] = s[0]; p[50] = s[1]; p[51] = s[2];
}
/* FUN_18025fd50 (a_3d, cp, K, *ncp, *nk): the arc, as rational cubics (always 4 values per point): a negative sweep runs a0 + sw .. a0 and is reversed at the end */
static void arc_nurbs(const McscEnt *e, double sys_tol, double *cp, double *K, int *ncp, int *nk) {
    double a0 = e->a0, sw = e->sw;
    int neg = sw < 0.0;
    if (neg) { a0 += sw; sw = -sw; }
    ArcV v = { e, a0, sw };
    if (fabs(sw - 6.283185307179586) < sys_tol || fabs(sw - 4.712388980384688) < sys_tol || fabs(sw - 3.141592653589793) < sys_tol || fabs(sw - 1.570796326794896) < sys_tol) {
        arc_quadrants(&v, cp, K, ncp, nk);
    } else if (sw >= 1.570796326794896) {
        int iVar2 = (sw < 4.712388980384688 || sw == 4.712388980384688) ? (3.141592653589793 <= sw && sw != 3.141592653589793) + 2 : 4;
        int e_ = 0, np = 0;
        v.sw = sw / S(iVar2);
        *ncp = 0; *nk = 0;
        for (int i = 0; i < iVar2; i++) {
            if (0 < i) *ncp = *ncp - 1;
            arc_seg(&v, cp + 4 * (*ncp), K + *nk, &np, &e_);
            *ncp += np;
            if (i < 1) *nk += e_;
            else stitch_knots(e_, nk, K);
            v.a0 += v.sw;
        }
    } else {
        arc_seg(&v, cp, K, ncp, nk);
    }
    if (neg && 0 < *ncp) reverse_piece(*ncp, *nk, 4, cp, K);
}

/* convert_coeffs_to_cpts (0x180260790): the four Bezier points of one power-basis cubic (rational: w = 1 before each) */
static void coeffs_to_cpts(int rational, double *d, const double c[12]) {
    int o = 0;
    if (rational) d[o++] = 1.0;
    d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2]; o += 3;
    if (rational) d[o++] = 1.0;
    d[o] = c[3] / 3.0 + c[0]; d[o + 1] = c[4] / 3.0 + c[1]; d[o + 2] = c[5] / 3.0 + c[2]; o += 3;
    if (rational) d[o++] = 1.0;
    d[o] = (c[3] * 2.0 + c[6]) / 3.0 + c[0]; d[o + 1] = (c[4] * 2.0 + c[7]) / 3.0 + c[1]; d[o + 2] = (c[5] * 2.0 + c[8]) / 3.0 + c[2]; o += 3;
    if (rational) d[o++] = 1.0;
    d[o] = c[3] + c[0] + c[6] + c[9]; d[o + 1] = c[1] + c[4] + c[7] + c[10]; d[o + 2] = c[5] + c[2] + c[8] + c[11];
}
/* FUN_180261c50 (spline, rational, cp, K, *ncp, *nk): the parametric cubic between t1 / t2 as Bezier pieces with the end trims (|fraction| > 0.01 / |end fraction - 1| >= 0.01);
   coef = the coefficient array (ent +0xc0 or the fit's), only the first segment's and the last segment's trims. */
static void spline_nurbs(const double *coef, double t1, double t2, int rational, double *cp, double *K, int *ncp, int *nk) {
    int w = rational ? 4 : 3;
    int i0 = (int)(t1 + 1e-05);
    const double *pd = coef + (int)(short)i0 * 12;
    double f = t1 - S((int)(short)i0), loc[12];
    if (0.01 < fabs(f)) {
        double d4 = 1.0 - f, d5 = f * f, d6 = d4 * d4;
        for (int k = 0; k < 3; k++) {
            double a0 = pd[k], a1 = pd[3 + k], a2 = pd[6 + k], a3 = pd[9 + k];
            loc[k] = a1 * f + a0 + a2 * d5 + a3 * d5 * f;
            loc[9 + k] = a3 * d4 * d6;
            loc[6 + k] = a2 * d6 + f * 3.0 * d6 * a3;
            loc[3 + k] = d4 * a1 + f * 2.0 * d4 * a2 + d5 * 3.0 * d4 * a3;
        }
    } else memcpy(loc, pd, 96);
    coeffs_to_cpts(rational, cp, loc);
    *ncp = 4;
    pd += 12;
    int i1 = (int)(t2 - 1e-05);
    short sVar8 = (short)((short)i1 - (short)i0), sVar17 = 1;
    short uVar9 = (short)(sVar8 - 1);
    if (0 < uVar9) {
        for (int n = uVar9; n; n--) {
            *ncp = *ncp - 1;
            coeffs_to_cpts(rational, cp + w * (*ncp), pd);
            *ncp = *ncp + 4;
            pd += 12;
            sVar17 = sVar8;
        }
    }
    if ((short)i1 != (short)i0) {
        double d = t2 - S((int)(short)i1);
        if (0.01 < fabs(d - 1.0) || fabs(d - 1.0) == 0.01) {
            for (int k = 0; k < 3; k++) { loc[k] = pd[k]; loc[3 + k] = d * pd[3 + k]; loc[6 + k] = d * d * pd[6 + k]; loc[9 + k] = d * d * d * pd[9 + k]; }
        } else memcpy(loc, pd, 96);
        *ncp = *ncp - 1;
        coeffs_to_cpts(rational, cp + w * (*ncp), loc);
        *ncp = *ncp + 4;
        sVar17 = sVar17 + 1;
    }
    K[0] = K[1] = K[2] = K[3] = 0.0;
    *nk = 4;
    for (short s = 1; s < sVar17; s++) { double dk = S((int)s); K[*nk] = dk; K[*nk + 1] = dk; K[*nk + 2] = dk; *nk += 3; }
    double dk = S((int)sVar17);
    K[*nk] = dk; K[*nk + 1] = dk; K[*nk + 2] = dk; K[*nk + 3] = dk;
    *nk += 4;
}

/* FUN_18065cd00 (ent, M, [t_a, t_b], *t, out): the foot of M on the curve from *t, Newton steps on the curve parameter at most 30, |step| < 1e-8 ends it, clamped to the interval (3 clamps at one end end it) */
static void project_pt(const McscEnt *e, const double M[3], double ta, double tb, double *t, double P[3], double sys_tol) {
    double lo = ta, hi = tb, sVar3 = 0, sVar2 = 0;
    if (ta < tb) { hi = tb; lo = ta; }
    if (!(ta < tb)) { hi = ta; lo = tb; }
    ent_pt(e, *t, P);
    double d = sqrt((M[0] - P[0]) * (M[0] - P[0]) + (M[1] - P[1]) * (M[1] - P[1]) + (M[2] - P[2]) * (M[2] - P[2]));
    int iter = 0;
    while (sys_tol < d) {
        double T[3];
        ent_tan(e, *t, T);
        double dV = d / ((T[0] * (P[0] - M[0]) + T[1] * (P[1] - M[1]) + T[2] * (P[2] - M[2])) / d);
        if (0x1e < (short)(iter + 1)) return;
        if (fabs(-dV) < 1e-08) return;
        dV = -dV + *t;
        *t = dV;
        if (dV < hi || dV == hi) {
            if (dV <= lo && lo != dV) {
                *t = lo;
                if (2 < sVar3) return;
                sVar3 = sVar3 + 1;
                sVar2 = 0;
            }
        } else {
            *t = hi;
            if (2 < sVar2) return;
            sVar2 = sVar2 + 1;
            sVar3 = 0;
        }
        ent_pt(e, *t, P);
        d = sqrt((M[0] - P[0]) * (M[0] - P[0]) + (M[1] - P[1]) * (M[1] - P[1]) + (M[2] - P[2]) * (M[2] - P[2]));
        iter = iter + 1;
    }
}

/* CSplineNodeMgr: the node-table size, the DLL's global DAT_1813c8000 (AllocateMem: 4000 at the program's start; the port starts each run from that value, the DLL keeps it across
   commands). put_max_n_spline_pts / AllocateMemNPts: grow to the next 1000 step that holds n + 5, the cap 0x51eb84c never reached here. */
static void grow_max(long *max, long n) {
    if (*max < n && *max <= n + 5) {
        long i = *max + (((unsigned long)(n + 5 - *max)) / 1000 + 1) * 1000;
        if (0x51eb851 < i) i = 0x51eb851;
        *max = i;
        if (0x51eb851 < i) *max = 0x51eb84c;
    }
}

/* FUN_18065cfc0 / FUN_18065d9b0 (the same adaptive fit): the curve (spline / NURBS, parameter range t1 .. t2) as Hermite cubics by chord-parametrised pieces, tolerance fit_tol, sys_tol = the
   global tol (segments no longer than sys_tol are dropped). mode 0 grows the DLL's table up to 0x7fff (FUN_18065cfc0: returns 0 on the limit, the piece is lost), 1 stops at room with *full = 1 (FUN_18065d9b0).
   Returns the segment count (mode 0), writes coefficients into out[12 * i]. */
static int fit_curve(const McscEnt *e, double t1, double t2, int rev, double fit_tol, double sys_tol, int mode, long room, double *out, double *t_stop, long *nseg, int *full, long *max) {
    int cap = 0, cnt = 0, ret = 0;
    if (mode == 0) cap = (int)((*max - 1) / 12);
    double dt = t2 - t1, step = fabs(dt) * 0.1;
    if (fabs(dt) <= step && step != fabs(dt)) step = fabs(dt);
    double base = rev ? t2 : t1, ts = rev ? t2 : t1, tn;
    double P0[3], Pa[3], Pb[3];
    ent_pt(e, t1, Pa);
    ent_pt(e, t2, Pb);
    memcpy(P0, rev ? Pb : Pa, 24);
    double T0[3];
    {
        double d[3];
        ent_tan(e, ts, d);
        if (rev) { d[0] = d[0] * -1.0; d[1] = d[1] * -1.0; d[2] = d[2] * -1.0; }
        double l = mode == 0 ? sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) : sqrt(d[1] * d[1] + d[0] * d[0] + d[2] * d[2]);
        if (l == 0.0) { T0[0] = 0.0; T0[1] = 0.0; T0[2] = 0.0; }
        else { T0[0] = d[0] / l; T0[1] = d[1] / l; T0[2] = d[2] / l; }
    }
    int bVar6 = 0;
    if (mode == 1) { *nseg = 0; *full = 0; }
    for (;;) {
        double st = rev ? -step : step;
        tn = st + base;
        double P1[3];
        if (!rev) {
            if (tn < t2) ent_pt(e, tn, P1);
            else { tn = t2; bVar6 = 1; memcpy(P1, Pb, 24); }
        } else {
            if (t1 < tn) ent_pt(e, tn, P1);
            else { tn = t1; bVar6 = 1; memcpy(P1, Pa, 24); }
        }
        double Tn[3];
        {
            double d[3];
            ent_tan(e, tn, d);
            if (rev) { d[0] = d[0] * -1.0; d[1] = d[1] * -1.0; d[2] = d[2] * -1.0; }
            double l = sqrt(d[1] * d[1] + d[0] * d[0] + d[2] * d[2]);
            if (l == 0.0) { Tn[0] = 0.0; Tn[1] = 0.0; Tn[2] = 0.0; }
            else { Tn[0] = d[0] / l; Tn[1] = d[1] / l; Tn[2] = d[2] / l; }
        }
        int bis = 0;
        double T1[3], te = tn, Pc[3], coefs[12];
        memcpy(T1, Tn, 24);
        for (;;) {
            double d = sqrt((P1[0] - P0[0]) * (P1[0] - P0[0]) + (P1[1] - P0[1]) * (P1[1] - P0[1]) + (P1[2] - P0[2]) * (P1[2] - P0[2]));
            double Ts[3] = { T0[0] * d, T0[1] * d, T0[2] * d }, Te[3] = { T1[0] * d, T1[1] * d, T1[2] * d };
            for (int k = 0; k < 3; k++) {
                coefs[k] = P0[k];
                coefs[3 + k] = Ts[k];
                coefs[6 + k] = ((P1[k] * 3.0 - P0[k] * 3.0) - Ts[k] * 2.0) - Te[k];
                coefs[9 + k] = (P0[k] * 2.0 - P1[k] * 2.0) + Ts[k] + Te[k];
            }
            double M[3];
            for (int k = 0; k < 3; k++) M[k] = coefs[3 + k] * 0.5 + coefs[k] + coefs[6 + k] * 0.25 + coefs[9 + k] * 0.125;
            double mid = (te + ts) * 0.5;
            project_pt(e, M, ts, te, &mid, Pc, sys_tol);
            double dd = sqrt((Pc[0] - M[0]) * (Pc[0] - M[0]) + (Pc[1] - M[1]) * (Pc[1] - M[1]) + (Pc[2] - M[2]) * (Pc[2] - M[2]));
            if (dd < fit_tol) break;
            bis = 1;
            te = (te + ts) * 0.5;
            ent_pt(e, te, P1);
            double dn[3];
            ent_tan(e, te, dn);
            if (rev) { dn[0] = dn[0] * -1.0; dn[1] = dn[1] * -1.0; dn[2] = dn[2] * -1.0; }
            double l = sqrt(dn[1] * dn[1] + dn[0] * dn[0] + dn[2] * dn[2]);
            if (l == 0.0) { T1[0] = 0.0; T1[1] = 0.0; T1[2] = 0.0; }
            else { T1[0] = dn[0] / l; T1[1] = dn[1] / l; T1[2] = dn[2] / l; }
        }
        double c1 = mode == 0
            ? sqrt((P1[1] - P0[1]) * (P1[1] - P0[1]) + (P1[0] - P0[0]) * (P1[0] - P0[0]) + (P1[2] - P0[2]) * (P1[2] - P0[2]))
            : sqrt((P1[0] - P0[0]) * (P1[0] - P0[0]) + (P1[1] - P0[1]) * (P1[1] - P0[1]) + (P1[2] - P0[2]) * (P1[2] - P0[2]));
        if (sys_tol < c1) {
            memcpy(out + cnt * 12, coefs, 96);
            cnt++;
            if (mode == 1) *nseg = cnt;
        }
        if (mode == 0) {
            if (cap <= (short)cnt) {
                if (0x7fff < *max + 1000) return 0;   /* FUN_18065cfc0: `return 0`; the whole piece is lost */
                grow_max(max, *max + 1000);
                cap = (int)((*max - 1) / 12);
            }
        } else {
            if (cnt == room) { *full = 1; *t_stop = te; return cnt; }
        }
        memcpy(P0, P1, 24);
        memcpy(T0, T1, 24);
        ts = te;
        if (!bis) base = tn;
        if (bVar6 && !bis) { ret = cnt; break; }
    }
    return ret;
}

/* FUN_180260910 (ent, reverse, rational, fit_tol, cp, K, *ncp, *nk): the NURBS piece of one entity; the caller's arrays grow in *pc */
static int ent_nurbs_piece(const McscEnt *e, int rational, double fit_tol, double sys_tol, Piece *pc, long *max) {
    pc->ncp = 0; pc->nk = 0;
    if (e->cls == 2) {
        if (rcp(pc, 16) || rk(pc, 8)) return -1;
        line_nurbs(e->a, e->b, rational, pc->cp, pc->k, &pc->ncp, &pc->nk);
    } else if (e->cls == 4) {
        if (rcp(pc, 13 * 4) || rk(pc, 32)) return -1;
        arc_nurbs(e, sys_tol, pc->cp, pc->k, &pc->ncp, &pc->nk);
    } else if (e->cls == 8 && e->n >= 0xc0 && e->rec[0xb4] == 4) {
        int ns = rd32(e->rec + 0xb0);
        if (ns < 1) return -2;
        if (rcp(pc, (3 * ns + 1) * (rational ? 4 : 3)) || rk(pc, 3 * ns + 8)) return -1;
        spline_nurbs((const double *)(e->rec + 0xc0), rdd(e->rec + 0x98), rdd(e->rec + 0xa0), rational, pc->cp, pc->k, &pc->ncp, &pc->nk);
    } else if (e->cls == 0x80) {
        const uint8_t *r = e->rec;
        if (e->n < 0xd8) return -2;
        int nk = rd32(r + 0xc8), ncp = rd32(r + 0xcc), deg = r[0xd0], vpp = r[0xd1], flg = r[0xd2];
        double t1 = rdd(r + 0x98), t2 = rdd(r + 0xa0);
        const double *kn = (const double *)(r + 0xd8), *cp = (const double *)(r + 0xd8 + 8 * (size_t)nk);
        if (deg == 3 && !(sys_tol <= fabs(t1 - kn[0])) && !(sys_tol <= fabs(t2 - kn[nk - 1]))) {
            if (rcp(pc, ncp * 4) || rk(pc, nk)) return -1;
            memcpy(pc->k, kn, sizeof(double) * nk);
            if (!rational || !(flg & 4)) memcpy(pc->cp, cp, sizeof(double) * vpp * ncp);
            else for (int i = 0; i < ncp; i++) { pc->cp[i * 4] = 1.0; pc->cp[i * 4 + 1] = cp[i * 3]; pc->cp[i * 4 + 2] = cp[i * 3 + 1]; pc->cp[i * 4 + 3] = cp[i * 3 + 2]; }
            pc->ncp = ncp; pc->nk = nk;
        } else {
            goto fit;
        }
    } else if (e->cls == 8) {
        if (e->n < 0xc0) return -2;
        goto fit;
    } else return -2;
    if (e->reverse && pc->ncp + pc->nk > 0) reverse_piece(pc->ncp, pc->nk, rational ? 4 : 3, pc->cp, pc->k);
    return 0;
fit:;
    /* convert_entity_to_coeffs2 (0x18065ef10) -> FUN_18065cfc0, then FUN_180261c50 on a transient spline; the fit already ran with reverse, so no reverse here */
    double t1, t2;
    { CurveRec c = crec(e); mcs_spline_t1t2(&c, &t1, &t2); }
    double *co = malloc(96 * (size_t)30000);   /* 0x7fff / 12 segments is the fit's limit */
    if (!co) return -1;
    int ns = fit_curve(e, t1, t2, e->reverse, fit_tol, sys_tol, 0, 0, co, NULL, NULL, NULL, max);
    if (ns == 0) { free(co); return 0; }
    if (rcp(pc, (3 * ns + 1) * (rational ? 4 : 3)) || rk(pc, 3 * ns + 8)) { free(co); return -1; }
    /* FUN_180260910: the transient spline t1 = 0, t2 = nseg */
    spline_nurbs(co, 0.0, S(ns), rational, pc->cp, pc->k, &pc->ncp, &pc->nk);
    free(co);
    return 0;
}

/* FUN_18025f430: the chain has a usable link (*any) and the piece kind: rational when any arc or rational NURBS (ent +0xd1 == 4) */
static int chain_rational(const McscEnt *e, int n, int *any) {
    *any = 0;
    for (int i = 0; i < n; i++) if (e[i].cls & 0x8e) {
        if (!*any) *any = 1;
        if (e[i].cls == 4 || (e[i].cls == 0x80 && e[i].n > 0xd1 && e[i].rec[0xd1] == 4)) return 1;
    }
    return 0;
}

/* ---- MaNURBSpline(nb_type, true): knot normalisation (MakeNURBSplineFromNbType, 0x180451cc0). Only the knots matter: make_nurbs_from_chains copies puVar13[0x29] (this + 0x148) back and
   discards the object. Not reachable here: HandlePiecewiseLinear (degree 1 with > 2 control points: chains are cubic). Evaluate is only a knot-span lookup + ConvertSegmentToBezier here. */
/* ConvertSegmentToBezier (0x180444aa0): Bezier points of knot span [U[p-1], U[p]], in rows of 4 (homogeneous). U = 2p knots starting at param_2[1] (span index + 1) */
static void seg_to_bezier(int p, const double *knots_from_span_plus_1, double (*P)[4]) {
    double U[64], L[32], R[32];
    for (int i = 0; i < 2 * p; i++) U[i] = knots_from_span_plus_1[i];
    double ta = U[p - 1], tb = U[p], h = tb - ta, g = 1e-20;
    for (int j = 0; j < p; j++) { L[j] = ta - U[j]; R[j] = U[p + j] - ta; }
    for (int r = p - 2, k = 0; r >= 0; r--, k++) {
        for (int j = 0; j <= r; j++) {
            double a = R[j], b = L[k + j];
            double d = a + b, m = d;
            if (d < 0.0) m = fabs(d);
            if (m < g) d = g;
            double c1 = b * (1.0 / d), c2 = a * (1.0 / d);
            for (int e = 0; e < 4; e++) P[j][e] = c2 * P[j][e] + c1 * P[j + 1][e];
        }
    }
    for (int j = 0; j < p; j++) R[j] = U[p + j] - tb;
    for (int r = 0; r < p - 1; r++) {
        for (int J = p; J > r + 1; J--) {
            double a = R[J - r - 1], d = a + h, m = d;
            if (d < 0.0) m = fabs(d);
            if (m < g) d = g;
            double c1 = a * (1.0 / d), c2 = h * (1.0 / d);
            for (int e = 0; e < 4; e++) P[J][e] = c1 * P[J - 1][e] + c2 * P[J][e];
        }
    }
}

/* GetUniqueKnots: K[deg] then each K[i] (order <= i < nk - order) differing from the last taken, then K[nk - order] unconditionally */
static int unique_knots(const double *K, int nk, int ord, double *U) {
    double prev = K[ord - 1];
    U[0] = prev;
    int n = 1;
    for (int i = ord; i < nk - ord; i++) if (K[i] != prev) { U[n++] = K[i]; prev = K[i]; }
    U[n] = K[nk - ord];
    return n + 1;
}
/* GetMultiplicityOfKnot: the number of knots equal to v, at most order */
static int knot_mult(const double *K, int nk, int ord, double v) {
    int m = 0;
    for (int i = 0; i < nk; i++) if (K[i] == v) m++;
    return m > ord ? ord : m;
}
/* GetMaxKnotMultiplicity: 0 with two unique knots; else the longest run among K[deg .. nk - deg - 1) */
static int max_mult(const double *K, int nk, int ord) {
    int deg = ord - 1;
    {   /* unique count (GetUniqueKnots with a NULL output) */
        double prev = K[deg]; int n = 1;
        for (int i = ord; i < nk - ord; i++) if (K[i] != prev) { n++; prev = K[i]; }
        if (n + 1 <= 2) return 0;
    }
    int best = 0, run = 0;
    double prev = K[deg];
    for (int i = ord; i < nk - deg; i++) {
        if (K[i] == prev) run++;
        else { if (best < run + 1) best = run + 1; run = 0; }
        prev = K[i];
    }
    return best;
}
/* CheckForMaxMultEqualOrder */
static void max_mult_eq_order(double *K, int nk, int ord) {
    int idx = 2 * ord - 1;
    for (int i = ord; i < nk - ord; i += ord, idx += ord) {
        double d = K[idx - (ord - 1)];
        if (knot_mult(K, nk, ord, d) != ord - 1) return;
        if (1e-06 <= K[idx] - d) return;
        if (K[idx + 1] - K[idx] <= 1e-06) return;
        K[idx] = d;
    }
}
/* FindKnotSpan(t, span, false): first i in [order, nk - order] with t <= K[i] (none: nk - order), less order, not below 0 */
static int find_span(const double *K, int nk, int ord, double t) {
    int i = ord, last = nk - ord;
    while (i <= last && !(t <= K[i])) i++;
    int j = i == last + 1 ? i - 1 : i;
    j = j - ord;
    return j < 0 ? 0 : j;
}
/* RegenNonUniformKnotsFromDerivRatios (0x180452940): the knot spans from the derivative lengths at the joints: the Bezier points of consecutive spans, ratio = |P1 - P0|(next span) / |P[p] - P[p-1]|(this span) */
static int regen_knots(double *K, int nk, int ord, const double *cp4, double *N) {
    double *U = malloc(sizeof(double) * (size_t)nk), *V = malloc(sizeof(double) * (size_t)nk);
    if (!U || !V) { free(U); free(V); return -1; }
    int nu = unique_knots(K, nk, ord, U);
    V[0] = 0.0; V[1] = 1.0;
    if (2 < nu) {
        double acc = 1.0;
        for (int i = 1; i <= nu - 2; i++) {
            int deg = ord - 1;
            double (*Pa)[4] = calloc((size_t)ord, 32), (*Pb)[4] = calloc((size_t)ord, 32);
            if (!Pa || !Pb) { free(Pa); free(Pb); free(U); free(V); return -1; }
            double t = (U[i] - U[i - 1]) * 0.5 + U[i - 1];
            int span = find_span(K, nk, ord, t);
            for (int j = 0; j < ord; j++) memcpy(Pa[j], cp4 + 4 * (size_t)(span + j), 32);
            seg_to_bezier(deg, K + span + 1, Pa);
            double t2 = (U[i + 1] - U[i]) * 0.5 + U[i];
            int span2 = find_span(K, nk, ord, t2);
            for (int j = 0; j < ord; j++) memcpy(Pb[j], cp4 + 4 * (size_t)(span2 + j), 32);
            seg_to_bezier(deg, K + span2 + 1, Pb);
            double a[3], b[3], c[3], d[3];
            for (int e = 0; e < 3; e++) { a[e] = Pa[ord - 1][e] / Pa[ord - 1][3]; b[e] = Pa[ord - 2][e] / Pa[ord - 2][3]; c[e] = Pb[1][e] / Pb[1][3]; d[e] = Pb[0][e] / Pb[0][3]; }
            double l1 = sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
            if (l1 < 1e-12) l1 = 1.0;
            double l2 = sqrt((c[0] - d[0]) * (c[0] - d[0]) + (c[1] - d[1]) * (c[1] - d[1]) + (c[2] - d[2]) * (c[2] - d[2]));
            if (l2 < 1e-12) l2 = 1.0;
            acc = acc * (l2 / l1);
            V[i + 1] = acc + V[i];
            free(Pa); free(Pb);
        }
        double vl = V[nu - 1];
        for (int i = 1; i < nu; i++) V[i] = V[i] / vl;
        V[0] = 0.0;
        V[nu - 1] = 1.0;
    }
    int w = 0;
    for (w = 0; w < ord; w++) N[w] = 0.0;
    for (int j = 1; j < nu - 1; j++) {
        int m = knot_mult(K, nk, ord, U[j]);
        for (int q = 0; q < m; q++) N[w++] = V[j];
    }
    for (int j = nk - ord; j < nk; j++) N[j] = 1.0;
    { double d1 = N[0], d2 = N[nk - 1]; for (int i = 0; i < nk; i++) N[i] = (N[i] - d1) * (1.0 / (d2 - d1)); }
    free(U); free(V);
    return 0;
}
/* ---- the stacked-vertices rebuild (CheckForStackedEndVertices 0x1804440e0) */
typedef struct { int ncp, nk; double *cp4, *K; } Ns;
static void ns_free(Ns *a) { free(a->cp4); free(a->K); a->cp4 = NULL; a->K = NULL; }
/* FixStackedEndVertices (0x18044c340, degree 3 pieces): a control point next to a coincident end point moved one third of the chord to the far end; 1 = all of it coincident (the piece is dropped) */
static int fix_stacked(Ns *s, int deg) {
    double dVar10 = 1.0 / S(deg);
    double *v = s->cp4, *V0 = v, *V1 = v + 4, *Vn = v + 4 * (size_t)(s->ncp - 1), *Vm = Vn - 4;
    double w0 = V0[3], w1 = V1[3];
    double res8 = V0[1] / w0, res10 = V0[2] / w0, d3 = V1[1], d4 = V1[2], d5 = V1[0], d6 = V0[0];
    double a = d3 / w1 - res8, b = d4 / w1 - res10, c = d5 / w1 - d6 / w0;
    if (!(1e-12 < sqrt(a * a + c * c + b * b))) {
        double wn = Vn[3], ty = Vn[1] / wn - res8, tx = Vn[0] / wn - d6 / w0, tz = Vn[2] / wn - res10;
        if (!(1e-12 <= sqrt(ty * ty + tx * tx + tz * tz))) return 1;
        V1[0] = (d5 / w1 + tx * dVar10) * w1;
        V1[1] = (d3 / w1 + ty * dVar10) * w1;
        V1[2] = (d4 / w1 + tz * dVar10) * w1;
        V1[3] = w1;
    }
    {
        double dn = Vn[3], dm = Vm[3], ey = Vm[1], nx = Vn[0], ny = Vn[1], mx = Vm[0], mz = Vm[2], nz = Vn[2] / dn;
        double p = ey / dm - ny / dn, q = mx / dm - nx / dn, r = mz / dm - nz;
        if (sqrt(p * p + q * q + r * r) <= 1e-12) {
            double d14 = V0[3], tx2 = V0[0] / d14 - nx / dn, ty2 = V0[1] / d14 - ny / dn, tz2 = V0[2] / d14 - nz;
            if (sqrt(ty2 * ty2 + tx2 * tx2 + tz2 * tz2) < 1e-12) return 1;
            Vm[0] = (mx / dm + tx2 * dVar10) * dm;
            Vm[1] = (ey / dm + ty2 * dVar10) * dm;
            Vm[2] = (mz / dm + tz2 * dVar10) * dm;
            Vm[3] = dm;
        }
    }
    return 0;
}
/* MaCurve::Evaluate + EvaluateCrv + EvaluateRationalBezierCurve at order 0 */
static void ns_eval(const Ns *s, int ord, double t, double out[3]) {
    int deg = ord - 1, span = find_span(s->K, s->nk, ord, t);
    double P[32][4];
    for (int j = 0; j < ord; j++) memcpy(P[j], s->cp4 + 4 * (size_t)(span + j), 32);
    seg_to_bezier(deg, s->K + span + 1, P);
    double a = s->K[span + deg], b = s->K[span + ord], d = b - a, m = d;
    if (d < 0.0) m = fabs(d);
    if (m < 1e-20) d = 1e-20;
    double dt = t - a, u = dt / d, Q[32][4];
    if (b - u <= u - a) {
        for (int j = 0; j <= deg; j++) memcpy(Q[j], P[deg - j], 32);
        for (int L = deg; L > 0; L--) for (int j = 0; j < L; j++) for (int e = 0; e < 4; e++) Q[j][e] = (Q[j][e] - Q[j + 1][e]) * u + Q[j + 1][e];
    } else {
        memcpy(Q, P, sizeof(double) * 4 * (size_t)ord);
        for (int L = deg; L > 0; L--) for (int j = 0; j < L; j++) for (int e = 0; e < 4; e++) Q[j][e] = (Q[j + 1][e] - Q[j][e]) * u + Q[j][e];
    }
    double w = Q[0][3];
    if (w < 1e-20) w = 1e-20;
    double iw = 1.0 / w;
    for (int e = 0; e < 3; e++) out[e] = Q[0][e] * iw;
}
/* Concatenate (0x180444420): next appended to this, joined at one control point, the knots renormalised, then RegenNonUniformKnotsFromDerivRatios */
static int ns_concat(Ns *t, const Ns *n, int ord) {
    int ncp = t->ncp + n->ncp - 1, nk = ord + ncp;
    double *c = malloc(sizeof(double) * 4 * (size_t)ncp), *K = malloc(sizeof(double) * (size_t)nk), *N = malloc(sizeof(double) * (size_t)nk);
    if (!c || !K || !N) { free(c); free(K); free(N); return -1; }
    memcpy(c, t->cp4, sizeof(double) * 4 * (size_t)t->ncp);
    memcpy(c + 4 * (size_t)t->ncp, n->cp4 + 4, sizeof(double) * 4 * (size_t)(n->ncp - 1));
    memcpy(K, t->K, sizeof(double) * (size_t)(t->nk - 1));
    { double *q = K + (t->nk - 1); for (int i = ord; i < n->nk; i++) *q++ = n->K[i] + 1.0; }
    double d0 = K[0], sc = 1.0 / (K[nk - 1] - d0);
    for (int i = 0; i < nk; i++) K[i] = (K[i] - d0) * sc;
    { double a = K[0], bb = K[nk - 1]; for (int i = 0; i < nk; i++) K[i] = (K[i] - a) * (1.0 / (bb - a)); }  /* SetKnots */
    free(t->cp4); free(t->K);
    t->cp4 = c; t->K = K; t->ncp = ncp; t->nk = nk;
    if (regen_knots(K, nk, ord, c, N)) { free(N); return -1; }
    memcpy(K, N, sizeof(double) * (size_t)nk);
    free(N);
    return 0;
}
/* CheckForStackedEndVertices with >= 3 unique knots: per knot span ConvertBezierToNURB (a Bezier piece, x / w then re-multiplied by w) + FixStackedEndVertices (coincident pieces dropped),
   JoinSplines (each joint moved to the midpoint of the two ends, weight 1) */
static int stacked_rebuild(const double *K, int nk, const double *cp4, int ord, Ns *out) {
    int deg = ord - 1;
    double *U = malloc(sizeof(double) * (size_t)nk);
    Ns *P = NULL;
    if (!U) return -1;
    int nu = unique_knots(K, nk, ord, U), np = 0;
    P = calloc((size_t)nu, sizeof(Ns));
    if (!P) { free(U); return -1; }
    int ret = 0;
    for (int i = 0; i < nu - 1; i++) {
        double t = (U[i] + U[i + 1]) * 0.5, R[32][4];
        int span = find_span(K, nk, ord, t);
        for (int j = 0; j < ord; j++) memcpy(R[j], cp4 + 4 * (size_t)(span + j), 32);
        seg_to_bezier(deg, K + span + 1, R);
        Ns n; n.ncp = ord; n.nk = 2 * ord;
        n.cp4 = malloc(sizeof(double) * 4 * (size_t)ord);
        n.K = malloc(sizeof(double) * (size_t)(2 * ord));
        if (!n.cp4 || !n.K) { ns_free(&n); ret = -1; goto done; }
        for (int j = 0; j < ord; j++) {
            double w = R[j][3], x = R[j][0] / w, y = R[j][1] / w, z = R[j][2] / w;
            n.cp4[j * 4] = w * x; n.cp4[j * 4 + 1] = w * y; n.cp4[j * 4 + 2] = w * z; n.cp4[j * 4 + 3] = w;
        }
        for (int j = 0; j < ord; j++) { n.K[j] = 0.0; n.K[j + ord] = 1.0; }   /* InitSplineData (K - 0) * 1 keeps them */
        if (fix_stacked(&n, deg)) { ns_free(&n); continue; }
        P[np++] = n;
    }
    if (np == 0) { ret = -3; goto done; }       /* CheckForStackedEndVertices: local_d8 < 1 raises (Ordinal_2300) */
    for (int i = 1; i < np; i++) {
        double e[3], s[3], m[3];
        ns_eval(&P[0], ord, 1.0, e);            /* JoinSplines: this + 0x30 = TMax = 1, this_00 + 0x28 = 0 */
        ns_eval(&P[i], ord, 0.0, s);
        for (int k = 0; k < 3; k++) m[k] = (e[k] + s[k]) * 0.5;
        double *v0 = P[0].cp4 + 4 * (size_t)(P[0].ncp - 1);
        v0[0] = m[0]; v0[1] = m[1]; v0[2] = m[2]; v0[3] = 1.0;    /* MoveEndPoint */
        P[i].cp4[0] = m[0]; P[i].cp4[1] = m[1]; P[i].cp4[2] = m[2]; P[i].cp4[3] = 1.0;   /* MoveStartPoint */
        if (ns_concat(&P[0], &P[i], ord)) { ret = -1; goto done; }
    }
    *out = P[0];
    P[0].cp4 = NULL; P[0].K = NULL;
done:
    for (int i = 0; i < np; i++) ns_free(&P[i]);
    free(P); free(U);
    return ret;
}
/* normalise_chain_knots (the constructor), K overwritten (nk values: make_nurbs_from_chains memcpy's lVar12 knots back). cp = the chain's control points, width 3 / 4. Returns 0, -1 memory,
   -3 the rebuild's result is undefined in the DLL (no spline left, or fewer knots than the chain: the memcpy reads past the array) */
static int nb_normalize(double *K, int nk, int ncp, const double *cp, int rational) {
    int ord = 4, deg = 3;                /* make_nurbs_from_chains: local_5360 = 3 */
    double K0 = K[0], KN = K[nk - 1], dVar15 = 1.0 / (KN - K0);
    double *cp4 = malloc(sizeof(double) * 4 * (size_t)ncp);
    if (!cp4) return -1;
    for (int i = 0; i < ncp; i++) {
        if (rational) { cp4[i * 4] = cp[i * 4 + 1]; cp4[i * 4 + 1] = cp[i * 4 + 2]; cp4[i * 4 + 2] = cp[i * 4 + 3]; cp4[i * 4 + 3] = cp[i * 4]; }
        else { cp4[i * 4] = cp[i * 3]; cp4[i * 4 + 1] = cp[i * 3 + 1]; cp4[i * 4 + 2] = cp[i * 3 + 2]; cp4[i * 4 + 3] = 1.0; }
    }
    for (int i = 0; i < nk; i++) K[i] = (K[i] - K0) * dVar15;
    int mm = max_mult(K, nk, ord);
    if (mm == ord - 1) max_mult_eq_order(K, nk, ord);
    /* CheckForUniformKnots: only sets its bool (not used here). CheckForStackedEndVertices: HasDuplicateVertices (x / w, y / w, z / w consecutive, distance < 1e-12) */
    int dup = 0;
    for (int i = 0; i + 1 < ncp; i++) {
        double wa = 1.0 / cp4[i * 4 + 3], wb = 1.0 / cp4[(i + 1) * 4 + 3];
        double ax = wa * cp4[i * 4], ay = wa * cp4[i * 4 + 1], az = wa * cp4[i * 4 + 2], bx = wb * cp4[(i + 1) * 4], by = wb * cp4[(i + 1) * 4 + 1], bz = wb * cp4[(i + 1) * 4 + 2];
        if (sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay) + (bz - az) * (bz - az)) < 1e-12) { dup = 1; break; }
    }
    double *curK = K, *curcp = cp4;
    int curnk = nk, ret = 0;
    Ns st; memset(&st, 0, sizeof st);
    if (dup) {
        /* unique < 3: FixStackedEndVertices changes vertices only, the knots stay. unique >= 3: rebuilt, then MakeNURBSplineFromNbType reads the multiplicity anew */
        double *U = malloc(sizeof(double) * (size_t)nk);
        if (!U) { free(cp4); return -1; }
        int nu = unique_knots(K, nk, ord, U);
        free(U);
        if (nu >= 3) {
            int c = stacked_rebuild(K, nk, cp4, ord, &st);
            if (c) { free(cp4); return c; }
            curK = st.K; curcp = st.cp4; curnk = st.nk;
            mm = max_mult(curK, curnk, ord);
        }
    }
    if (mm >= deg) {
        double *N = malloc(sizeof(double) * (size_t)curnk);
        if (!N || regen_knots(curK, curnk, ord, curcp, N)) { free(N); ns_free(&st); free(cp4); return -1; }
        memcpy(curK, N, sizeof(double) * (size_t)curnk);
        free(N);
    }
    if (curK != K) {
        if (curnk < nk) ret = -3;
        else memcpy(K, curK, sizeof(double) * (size_t)nk);   /* make_nurbs_from_chains: memcpy(local_5358, puVar13[0x29], lVar12 * 8) */
    }
    ns_free(&st);
    free(cp4);
    return ret;
}

int mcsc_nurbs(const McscEnt *e, int n, double fit_tol, double sys_tol, McsfNb *o) {
    int any, rational = chain_rational(e, n, &any);
    if (!any) return MCSC_NONE;
    int vpp = rational ? 4 : 3, iVar20 = 0, knotcnt = 0, iVar18 = 0, first = 1, cpcap = 8000, kcap = 2000;
    double *cps = calloc((size_t)cpcap, 8), *knots = calloc((size_t)kcap, 8);
    Piece pc; memset(&pc, 0, sizeof pc);
    double p_first[3] = { 0, 0, 0 }, p_last[3] = { 0, 0, 0 };
    long nodemax = 4000;
    int ret = MCSC_OK;
    if (!cps || !knots) { ret = MCSC_MEM; goto out; }
    for (int li = 0; li < n; li++) {
        const McscEnt *ent = &e[li];
        if (!(ent->cls & 0x8e)) continue;
        int rev = ent->reverse;
        if (!first) iVar20--;
        { int c = ent_nurbs_piece(ent, rational, fit_tol, sys_tol, &pc, &nodemax); if (c < 0) { ret = c == -1 ? MCSC_MEM : MCSC_BADREC; goto out; } }
        int iVar5 = pc.ncp, iVar4 = pc.nk, iVar21 = iVar20;
        if (!first && iVar5 == 0) iVar21 = iVar20 + 1;
        iVar20 = iVar5 + iVar21;
        if (knotcnt + iVar4 > kcap || iVar20 > cpcap / 4) {   /* make_nurbs_from_chains local_54c8 grows by max + 1000 (the original's 4-per-point size) */
            int m = iVar20 > knotcnt + iVar4 ? iVar20 : knotcnt + iVar4;
            cpcap = (m + 1000) * 4; kcap = m + 1000;
            double *a = realloc(cps, (size_t)cpcap * 8), *b = realloc(knots, (size_t)kcap * 8);
            if (a) cps = a;
            if (b) knots = b;
            if (!a || !b) { ret = MCSC_MEM; goto out; }
        }
        memcpy(cps + vpp * iVar21, pc.cp, sizeof(double) * vpp * iVar5);
        memcpy(knots + knotcnt, pc.k, sizeof(double) * iVar4);
        double A[3], B[3];
        ent_ends(ent, A, B);
        if (!first) {
            memcpy(p_last, rev ? A : B, 24);
            iVar18 = knotcnt;
            if (0 < iVar5 && 0 < iVar4) {
                stitch_knots(iVar4, &knotcnt, knots);
                iVar18 = knotcnt;
            }
        } else {
            memcpy(p_first, rev ? B : A, 24);
            memcpy(p_last, rev ? A : B, 24);
            if (0 < iVar5) first = 0;
            knotcnt = iVar4 + knotcnt;
            iVar18 = knotcnt;
        }
    }
    if (iVar20 < 2 || iVar18 < 2) { ret = MCSC_NONE; goto out; }
    {
        double d = sqrt((p_last[1] - p_first[1]) * (p_last[1] - p_first[1]) + (p_last[0] - p_first[0]) * (p_last[0] - p_first[0]) + (p_last[2] - p_first[2]) * (p_last[2] - p_first[2]));
        o->closed = d < sys_tol;
    }
    {
        int c = nb_normalize(knots, iVar18, iVar20, cps, rational);
        if (c == -3) { ret = MCSC_DLL_UNDEFINED; goto out; }
        if (c < 0) { ret = MCSC_MEM; goto out; }
    }
    o->ncp = iVar20; o->nk = iVar18; o->deg = 3; o->dim = 3; o->rational = rational;
    o->k = knots; o->cp = cps; knots = NULL; cps = NULL;
out:
    free(cps); free(knots); free(pc.cp); free(pc.k);
    return ret;
}

/* FUN_18065f5c0: a line's coefficients: a0 the start, a1 the chord (rev: start b, chord reversed) */
static void line_coeffs(const double *p, int rev, double *c) {
    double d[3], s[3];
    if (!rev) { d[0] = p[3] - p[0]; d[1] = p[4] - p[1]; d[2] = p[5] - p[2]; s[0] = p[0]; s[1] = p[1]; s[2] = p[2]; }
    else { d[0] = p[0] - p[3]; d[1] = p[1] - p[4]; d[2] = p[2] - p[5]; s[0] = p[3]; s[1] = p[4]; s[2] = p[5]; }
    for (int k = 0; k < 3; k++) { c[k] = s[k]; c[3 + k] = d[k]; c[6 + k] = 0.0; c[9 + k] = 0.0; }
}
/* FUN_18065e930: an arc as Hermite cubics; the division count FUN_18065cf60 = (int)(|sw| / (384 tol / (5 r))^0.25) + 1, at least 1 */
static void arc_coeffs(const McscEnt *e, double a0, double sw, double r, const double cc[3], int rev, double fit_tol, double *out, long *nseg) {
    *nseg = 0;
    int div = (int)(fabs(sw) / pow((fit_tol * 384.0) / (r * 5.0), 0.25)) + 1;
    if (div < 1) div = 1;
    double X = rev ? sw + a0 : a0;
    double step = (rev ? -sw : sw) / S(div);
    double Pp[3], Tp[3];
    double si = sin(X), co = cos(X);
    for (int s = 0; s < div; s++) {
        double P0[3], T0[3], q[3];
        if (s == 0) {
            q[0] = co * r + cc[0]; q[1] = si * r + cc[1]; q[2] = cc[2];
            vtw(e, q, P0);
            if (rev == 0 ? (0.0 <= sw) : !(0.0 < sw)) { q[0] = -(si * r); q[1] = co * r; }
            else { q[0] = si * r; q[1] = -(co * r); }
            q[2] = 0.0;
            vtw(e, q, T0);
            T0[0] = T0[0] * fabs(step); T0[1] = T0[1] * fabs(step); T0[2] = T0[2] * fabs(step);
        } else { memcpy(P0, Pp, 24); memcpy(T0, Tp, 24); }
        X = X + step;
        co = cos(X);
        q[0] = co * r + cc[0];
        si = sin(X);
        q[1] = si * r + cc[1];
        q[2] = cc[2];
        double P1[3], T1[3];
        vtw(e, q, P1);
        if (rev == 0 ? (0.0 <= sw) : !(0.0 < sw)) { q[0] = -(si * r); q[1] = co * r; }
        else { q[0] = si * r; q[1] = -(co * r); }
        q[2] = 0.0;
        vtw(e, q, T1);
        T1[0] = T1[0] * fabs(step); T1[1] = T1[1] * fabs(step); T1[2] = T1[2] * fabs(step);
        double *c = out + (*nseg) * 12;
        for (int k = 0; k < 3; k++) {
            c[k] = P0[k]; c[3 + k] = T0[k];
            c[6 + k] = ((P1[k] * 3.0 - P0[k] * 3.0) - T0[k] * 2.0) - T1[k];
            c[9 + k] = (P0[k] * 2.0 - P1[k] * 2.0) + T0[k] + T1[k];
        }
        memcpy(Pp, P1, 24); memcpy(Tp, T1, 24);
        *nseg = *nseg + 1;
    }
}
/* FUN_180550e10: one segment with the parameter reversed */
static void rev_seg(double *c) {
    double t[12];
    t[0] = c[3] + c[0] + c[6] + c[9]; t[1] = c[7] + c[1] + c[4] + c[10]; t[2] = c[5] + c[2] + c[8] + c[11];
    t[3] = (-c[3] - c[6] * 2.0) - c[9] * 3.0; t[4] = (-c[4] - c[7] * 2.0) - c[10] * 3.0; t[5] = (-c[5] - c[8] * 2.0) - c[11] * 3.0;
    t[6] = c[9] * 3.0 + c[6]; t[7] = c[10] * 3.0 + c[7]; t[8] = c[11] * 3.0 + c[8];
    t[9] = -c[9]; t[10] = -c[10]; t[11] = -c[11];
    memcpy(c, t, 96);
}
/* FUN_18065fcb0: an order-4 spline from its stored segments between t1, t2 with the end trims (1e-6 test), the segments in travel order, reversed when rev */
static void spline_seg(const double *co, double t1, double t2, int rev, long room, double *out, double *t_stop, long *nseg, int *full) {
    *full = 0; *nseg = 0;
    short i0 = (short)(int)(t1 + 1e-05), i1 = (short)(int)(t2 - 1e-05);
    const double *pc = co + i0 * 12;
    short slot = rev ? (short)(i1 - i0) : 0;
    double d27 = S((int)i0), f = t1 - d27;
    double *d = out + slot * 12;
    if (1e-06 <= fabs(f) || i0 == i1) {
        double h = t2 - d27;
        if (0.999999 < h) h = 1.0;
        h = h - f;
        double f2 = f * f, h2 = h * h;
        for (int k = 0; k < 3; k++) {
            double a0 = pc[k], a1 = pc[3 + k], a2 = pc[6 + k], a3 = pc[9 + k];
            d[k] = f * a1 + a0 + f2 * a2 + f2 * f * a3;
            d[3 + k] = h * a1 + f * 2.0 * h * a2 + f2 * 3.0 * h * a3;
            d[6 + k] = f * 3.0 * h2 * a3 + h2 * a2;
            d[9 + k] = h2 * h * a3;
        }
    } else memcpy(d, pc, 96);
    if (rev) rev_seg(d);
    if (++*nseg == room) { *full = 1; *t_stop = d27; return; }
    int inc = rev ? -1 : 1;
    slot = (short)(slot + inc);
    pc += 12;
    short mid = (short)(i1 - i0 - 1);
    for (short s = 0; s < mid; s++) {
        d = out + slot * 12;
        memcpy(d, pc, 96);
        if (rev) rev_seg(d);
        if (++*nseg == room) { *full = 1; *t_stop = S((int)s) + 1.0; return; }
        slot = (short)(slot + inc);
        pc += 12;
    }
    if (i1 != i0) {
        double s = t2 - S((int)i1), tv = S((int)mid);   /* FUN_18065fcb0 `(double)(int)sVar16` with sVar16 the loop counter, overwritten by 3 on the trim branch */
        d = out + slot * 12;
        if (1e-06 < fabs(s - 1.0) || fabs(s - 1.0) == 1e-06) {
            for (int k = 0; k < 3; k++) { d[k] = pc[k]; d[3 + k] = s * pc[3 + k]; d[6 + k] = s * s * pc[6 + k]; d[9 + k] = s * s * s * pc[9 + k]; }
            tv = 3.0;
        } else memcpy(d, pc, 96);
        if (rev) rev_seg(d);
        if (++*nseg == room) { *full = 1; *t_stop = tv; }
    }
}

/* convert_entity_to_coeffs (0x18065f030): one entity's cubic coefficients into out, room segments at most; mode 1 / 2: the entity trimmed at the start / end of the chain (t the entity parameter, pt the point); *full, *t_stop: the buffer ran out */
int mcsc_ent_coeffs(const McscEnt *e, double fit_tol, double sys_tol, long room, double *out, double *t_stop, long *nseg, int *full, int mode, double t, const double pt[3]) {
    int rev = e->reverse;
    *full = 0;
    if (e->cls == 2) {
        double p[6];
        memcpy(p, e->a, 24);
        memcpy(p + 3, e->b, 24);
        if (0 < mode) {
            if ((mode == 2) == rev) memcpy(p, pt, 24);
            else memcpy(p + 3, pt, 24);
        }
        *nseg = 1;
        line_coeffs(p, rev, out);
        return 0;
    }
    if (e->cls == 4) {
        double a0 = e->a0, sw = e->sw;
        if (0 < mode) {
            if ((mode == 1) == rev) sw = sw * t;
            else { a0 = a0 + sw * t; sw = sw * (1.0 - t); }
        }
        arc_coeffs(e, a0, sw, e->r, e->c, rev, fit_tol, out, nseg);
        return 0;
    }
    if (e->cls == 8 || e->cls == 0x80) {
        double t1 = rdd(e->rec + 0x98), t2 = rdd(e->rec + 0xa0);
        if (mode == 1) t1 = t; else if (mode == 2) t2 = t;
        if (e->cls == 8 && e->rec[0xb4] == 4) {
            spline_seg((const double *)(e->rec + 0xc0), t1, t2, rev, room, out, t_stop, nseg, full);
            return 0;
        }
        long nmax = 4000;   /* only FUN_18065d9b0's room applies in this mode */
        fit_curve(e, t1, t2, rev, fit_tol, sys_tol, 1, room, out, t_stop, nseg, full, &nmax);
        return 0;
    }
    *nseg = 0;
    return -2;
}

/* make_spline_from_chain body */
int mcsc_spline(const McscEnt *e, int n, int closed, double t_start, double t_end, double fit_tol, double sys_tol,
                double **coef, int *nseg, double p0[3], double p1[3], int *closed_out) {
    long nodemax = 4000;
    long cnt = 0;
    double *buf = calloc((size_t)nodemax * 12, 8), start[3] = { 0, 0, 0 }, end[3] = { 0, 0, 0 };
    if (!buf) return MCSC_MEM;
    int ret = MCSC_OK;
    short mode = 1;
    double t = 0.0;
    /* spline_from_chain_ent(ent, tol, rev, ..., n, buf, start, end, mode, t): returns 0 ok */
    for (int li = 0; li <= n; li++) {
        const McscEnt *ent; int lastpass = 0;
        double tm;
        short m = mode;
        if (li == n) {
            if (!(closed && 1e-12 < t_start) || n == 0) break;
            ent = &e[0]; m = 2; tm = t_start; lastpass = 1;
        } else {
            ent = &e[li];
            if (!closed && li == n - 1) { m = 2; t = t_end; }
            else if (m == 1) t = t_start;
            else if (m == 2) t = t_end;
            tm = t;
        }
        int rev = ent->reverse, bVar3 = 0;
        if (!(ent->cls == 2 || ent->cls == 4 || ent->cls == 8 || ent->cls == 0x80)) { mode = 0; continue; }
        double A[3], B[3], split[3] = { 0, 0, 0 };
        ent_ends(ent, A, B);
        if (m != 1 || tm <= 1e-12) {
            if (cnt == 0) memcpy(start, rev ? B : A, 24);
            if (m == 2 && 1e-12 < tm) { ent_pt(ent, tm, end); bVar3 = 1; }
        } else { ent_pt(ent, tm, start); bVar3 = 1; }
        memcpy(split, m == 1 ? start : end, 24);
        for (;;) {
            double t_stop = 0;
            long got = 0, room = nodemax - cnt - 1;
            int full = 0;
            if (mcsc_ent_coeffs(ent, fit_tol, sys_tol, room, buf + cnt * 12, &t_stop, &got, &full, bVar3 ? m : 0, tm, split) < 0) { ret = MCSC_BADREC; goto out; }
            cnt += got;
            if (!full && cnt <= nodemax - 1) {
                if (m < 2 || tm <= 1e-12) memcpy(end, rev ? A : B, 24);
                break;
            }
            long iVar12 = (short)(int)(S(nodemax) * 0.1);
            if (iVar12 < 1000) iVar12 = 1000;
            long nm = nodemax + iVar12;
            if (0x7fff < nm) nm = 0x7fff;
            if (nm <= nodemax) { ret = MCSC_TOO_LONG; goto out; }
            grow_max(&nodemax, nm);
            double *nb = realloc(buf, (size_t)nodemax * 96);
            if (!nb) { ret = MCSC_MEM; goto out; }
            buf = nb;
        }
        (void)lastpass;
        mode = 0;
    }
    if (0 < cnt) {
        double d = sqrt((end[1] - start[1]) * (end[1] - start[1]) + (end[0] - start[0]) * (end[0] - start[0]) + (end[2] - start[2]) * (end[2] - start[2]));
        *closed_out = d < sys_tol;
        *coef = buf; *nseg = (int)cnt;
        memcpy(p0, buf, 24);
        const double *l = buf + (cnt - 1) * 12;
        for (int k = 0; k < 3; k++) p1[k] = ((l[9 + k] + l[6 + k]) + l[3 + k]) + l[k];   /* eval_spline(nseg): Horner at 1 in the last segment */
        return MCSC_OK;
    }
    ret = MCSC_NONE;
out:
    free(buf);
    return ret;
}

/* check_nurb_knots (0x1805ae200, called by put_nurbs_in_db on the stored knots): an end knot off the repeated run by more than 1e-8 is snapped to it. Reachable when the stacked rebuild leaves more knots than the chain and
   make_nurbs_from_chains copies only the first nk: the last copied knot then belongs to the next run */
static void check_knots(int nk, int deg, double *K) {
    double d = K[1];
    if (K[0] <= d - 1e-08 && d - 1e-08 != K[0]) {
        int ok = 1;
        if (1 < deg) for (int i = 2; i <= deg; i++) if (1e-08 < fabs(K[i] - d)) { ok = 0; break; }
        if (ok) K[0] = d;
    }
    d = K[nk - 2];
    if (K[nk - 1] <= d + 1e-08) return;
    if (1 < deg) for (int c = 2, j = nk - 3; c <= deg; c++, j--) if (1e-08 < fabs(K[j] - d)) return;
    K[nk - 1] = d;
}
/* put_nurbs_in_db parts mcsf_nurbs_rec derives from the knot array's clamped ends: nb +0x30 / +0x38 are K[0] / K[nk - 1] (make_nurbs_from_chains local_5398 / local_5390), the curvature limit runs over them
   (ncp * 2 - 2 steps, the same loop as mcsf_nurbs_rec), and the stored knots pass check_nurb_knots */
static void nurbs_rec_tail(uint8_t *r, const double *K, int nk, int ncp) {
    double t1 = K[0], t2 = K[nk - 1];
    memcpy(r + 0x98, &t1, 8); memcpy(r + 0xa0, &t2, 8);
    {
        double dt = t2 - t1, step = dt / (double)(ncp * 2 - 2), cmax = 0.0;
        for (double tt = t1; tt <= t2; tt = tt + step) {
            double c = mcsc_nb_curvature(r, tt);
            if (cmax < c && c < 1000000000.0) cmax = c;
        }
        if (cmax < 1e-09) cmax = 1e-09;
        else if (!(cmax <= 1000000000.0)) cmax = 1e9;
        memcpy(r + 0xa8, &cmax, 8);
    }
    check_knots(nk, r[0xd0], (double *)(r + 0xd8));
}
/* FUN_180288410 (p, n, depth): p moved along n onto the plane n . x = depth */
static void plane_pt(double p[3], const double n[3], double depth) {
    double d1 = p[2], d2 = n[2], v = depth - (n[1] * p[1] + n[0] * p[0] + d2 * d1);
    double dy = p[1] + n[1] * v, dx = n[0] * v + p[0];
    p[0] = dx; p[1] = dy; p[2] = d1 + d2 * v;
}
/* squash_nurbs_curve (0x1802889e0) on the record: start, end, each control point (x / w, y / w, z / w on the plane, multiplied by w again) */
static void squash_nurbs_rec(uint8_t *r, const McscPlane *pl) {
    double p[3];
    int nk = rd32(r + 0xc8), ncp = rd32(r + 0xcc), vpp = r[0xd1], rat = !((r[0xd2] >> 2) & 1);
    if (vpp != (rat ? 4 : 3)) return;   /* squash_nurbs_curve: param_1[0x69] != (rational + 3): left unchanged */
    memcpy(p, r + 0x68, 24); plane_pt(p, pl->n, pl->depth); memcpy(r + 0x68, p, 24);
    memcpy(p, r + 0x80, 24); plane_pt(p, pl->n, pl->depth); memcpy(r + 0x80, p, 24);
    uint8_t *c = r + 0xd8 + 8 * (size_t)nk;
    for (int i = 0; i < ncp; i++, c += 8 * (size_t)vpp) {
        double v[4]; memcpy(v, c, 8 * (size_t)vpp);
        if (!rat) { memcpy(p, v, 24); plane_pt(p, pl->n, pl->depth); memcpy(v, p, 24); }
        else {
            p[0] = v[1] / v[0]; p[1] = v[2] / v[0]; p[2] = v[3] / v[0];
            plane_pt(p, pl->n, pl->depth);
            double w = v[0];
            v[1] = w * p[0]; v[2] = w * p[1]; v[3] = w * p[2];
        }
        memcpy(c, v, 8 * (size_t)vpp);
    }
}
int mcsc_chain_record(const McscEnt *e, int n, int kind, int closed, double t_start, double t_end, double fit_tol, double sys_tol, const McscPlane *plane, uint8_t **rec, uint32_t *rn) {
    *rec = NULL; *rn = 0;
    if (kind == 0) {
        McsfNb o; memset(&o, 0, sizeof o);
        int c = mcsc_nurbs(e, n, fit_tol, sys_tol, &o);
        if (c != MCSC_OK) return c;
        *rec = mcsf_nurbs_rec(&o, sys_tol, rn);
        if (*rec) nurbs_rec_tail(*rec, o.k, o.nk, o.ncp);
        free(o.k); free(o.cp);
        if (*rec && plane) squash_nurbs_rec(*rec, plane);
        return *rec ? MCSC_OK : MCSC_MEM;
    }
    /* squash_spline (0x180288be0) stores stack garbage in the coefficients: DEVIATION approved by the user, N9-26: projected correctly (mcsf_squash_spline) */
    double *co = NULL, p0[3], p1[3];
    int ns = 0, cl = 0;
    int c = mcsc_spline(e, n, closed, t_start, t_end, fit_tol, sys_tol, &co, &ns, p0, p1, &cl);
    if (c != MCSC_OK) return c;
    *rec = mcsf_spline_rec(co, ns, p0, p1, cl, sys_tol, rn);
    free(co);
    if (*rec && plane) mcsf_squash_spline(*rec, plane->n, plane->depth);
    return *rec ? MCSC_OK : MCSC_MEM;
}

/* FUN_180260910 for the entity oracle */
int mcsc_ent_nurbs(const McscEnt *e, int rational, double fit_tol, double sys_tol, double **cp, double **k, int *ncp, int *nk) {
    Piece p; memset(&p, 0, sizeof p);
    long m = 4000;
    int c = ent_nurbs_piece(e, rational, fit_tol, sys_tol, &p, &m);
    if (c < 0) { free(p.cp); free(p.k); return c; }
    *cp = p.cp; *k = p.k; *ncp = p.ncp; *nk = p.nk;
    return 0;
}
