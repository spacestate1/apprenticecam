/* mccsurf.c - Wireframe > Curve Surface: mccad sketcher_curve_6 -> mccore project_chains_onto_surfs (0x180569380). formats/wireframe.md
   "Curve Surface". A transliteration of mccore's msf ("multi surface") projection code, working on entity images with mccore's `ent`
   layout (ent +0x68 = the class body; surfaces: surf_type at +0x68, nbs_type at surf +0x98); decompiles in decomp/curvesurf/
   (cs1.c .. cs6.c, asm1.txt for the stack arguments Ghidra lost). Function names in the comments are the DLL's.

   The globals mirror mccore's statics: grid 1 (the surface: DAT_1813fb930 nu, fb934 nv, fb948 points, fb960 uv, fb978 hit flags,
   fbd58 min / max, fbb80 the surf_type header), grid 2 (the curve: fb938 count, fb950 points, fb968 parameters, fbd88 min / max);
   tol, surf_step_chord_dev, _DAT_1812f39c8 (the projection distance = sketcher_curve_6's surf_step_dist argument, static 0.1, written
   only by project_chains_onto_surfs: decomp/curvesurf/symw.txt), msf_find_multi, msf_join_tol, surf_use_chord_dev, surf_step_dist. */
#include "mccsurf.h"
#include "mcam_load.h"
#include "mcsurf.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ENT 0x2938
#define D(p, o) (*(double *)((uint8_t *)(p) + (o)))
#define I32(p, o) (*(int32_t *)((uint8_t *)(p) + (o)))
#define U8(p, o) (*(uint8_t *)((uint8_t *)(p) + (o)))
#define PTR(p, o) (*(uint8_t **)((uint8_t *)(p) + (o)))

/* ------------------------------------------------------------------------------------------------ state (mccore statics) */
static double tol, surf_step_chord_dev, surf_step_dist, msf_join_tol, proj_dist /* _DAT_1812f39c8 */, curve_min_step;
static int is_metric, surf_use_chord_dev, msf_find_multi;
static int g1nu, g1nv; static double (*g1p)[3], (*g1uv)[2]; static uint8_t *g1hit; static double g1mm[6], g1hdr[0x98 / 8];
static int g2n; static double (*g2p)[3], *g2t; static double g2mm[6];
static int max_spline_pts = 4000;        /* CSplineNodeMgr::AllocateMem: DAT_1813c8000 = 4000 (decomp/curvesurf/cs5.c) */
static int extrap_guard;                 /* DAT_1813f20a9 (eval_surf_extrap's recursion flag) */

/* ------------------------------------------------------------------------------------------------ vectors */
static double dist3(const double *a, const double *b) { double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return sqrt(y * y + x * x + z * z); }
static void unit3(double *v) { double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l == 0.0) { v[0] = v[1] = v[2] = 0; } else { v[0] /= l; v[1] /= l; v[2] /= l; } }   /* FUN_18000f2f0 */
/* UnitNormal 0x1804b7480: a x b, divided by its length (1e-12 when shorter) */
static int unit_normal(const double *a, const double *b, double *n) {
    n[0] = b[2] * a[1] - b[1] * a[2]; n[1] = b[0] * a[2] - a[0] * b[2]; n[2] = a[0] * b[1] - b[0] * a[1];
    double l = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); int e = 0;
    if (l < tol) { double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); if (tol <= la) { double lb = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]); e = (tol <= lb) + 2; } else e = 1; }
    double d = l < 1e-12 ? 1e-12 : l;
    n[0] /= d; n[1] /= d; n[2] /= d; return e;
}
/* det3 0x1804b7a80 */
static double det3(const double *a, const double *b, const double *c) {
    return (((b[0] * a[2] * c[1] + (b[2] * a[1] * c[0] + a[0] * b[1] * c[2])) - a[2] * b[1] * c[0]) - b[0] * a[1] * c[2]) - b[2] * a[0] * c[1];
}
/* dist_pt_l_3d 0x180013bd0 (l = a, b) */
static double dist_pt_l_3d(const double *p, const double *l) {
    double ax = l[0], ay = l[1], az = l[2], dx = l[3] - ax, dy = l[4] - ay, dz = l[5] - az;
    double cx = (p[1] - ay) * dz - (p[2] - az) * dy, cy = (p[2] - az) * dx - dz * (p[0] - ax), cz = (p[0] - ax) * dy - (p[1] - ay) * dx;
    double L = sqrt(dx * dx + dy * dy + dz * dz);
    if (fabs(L) <= tol) return sqrt((ay - p[1]) * (ay - p[1]) + (ax - p[0]) * (ax - p[0]) + (az - p[2]) * (az - p[2]));
    return fabs(sqrt(cy * cy + cx * cx + cz * cz) / L);
}
/* dist_pt_l 0x1804b32a0 (l = a, b in 2D) */
static double dist_pt_l(const double *p, const double *l) {
    double px = p[0] - l[0], py = p[1] - l[1], dy = l[3] - l[1], dx = l[2] - l[0], L = sqrt(dy * dy + dx * dx);
    if (L <= tol) return sqrt(py * py + px * px);
    double d = (px * dy - py * dx) / L; return d < 0.0 ? -d : d;
}
/* FUN_1804b47c0: the parameter of p's foot on the segment l (0 when shorter than tol) */
static double seg_param(const double *p, const double *l) {
    double dx = l[3] - l[0], dy = l[4] - l[1], dz = l[5] - l[2], L2 = dy * dy + dx * dx + dz * dz;
    if (tol * tol < L2) return (dy * (p[1] - l[1]) + dx * (p[0] - l[0]) + dz * (p[2] - l[2])) / L2;
    return 0.0;
}

/* ------------------------------------------------------------------------------------------------ images */
/* a part record -> an image with the variable data behind the pointers mccore keeps there (alloc_nurbsurf_data_ptr / alloc_surf_data_ptr
   / alloc_nurbs_data_ptr, decomp/curvesurf/cs3.c): SURF kind 0 knots +0x118, points +0x120; kind 10 uv data +0x110, base +0x100 */
static uint8_t *mk_image(const uint8_t *r, uint32_t n) {
    uint8_t *im = calloc(1, ENT); if (!im) return NULL;
    int cls = (int)*(const uint32_t *)r; size_t fixed = n;
    if (cls == 0x100 && r[0x68] == 0) fixed = 0x118; else if (cls == 0x100 && r[0x68] == 10) fixed = 0x138;
    if (fixed > n) fixed = n;
    memcpy(im, r, fixed < ENT ? fixed : ENT);
    if (n > fixed) {
        uint8_t *d = malloc(n - fixed + 16); if (!d) { free(im); return NULL; }
        memcpy(d, r + fixed, n - fixed);
        if (cls == 0x100 && r[0x68] == 0) { int nku = I32(im, 0x100), nkv = I32(im, 0x104); PTR(im, 0x118) = d; PTR(im, 0x120) = d + 8 * (nku + nkv); }
        else PTR(im, 0x110) = d;
    }
    return im;
}
static void free_image(uint8_t *im) {
    if (!im) return;
    if (I32(im, 0) == 0x100 && im[0x68] == 0) free(PTR(im, 0x118));
    else if (I32(im, 0) == 0x100 && im[0x68] == 10) free(PTR(im, 0x110));
    free(im);
}

/* ------------------------------------------------------------------------------------------------ surface evaluation */
/* eval_surf kind 0 (0x18047ba50: order 0 FUN_1805b08d0, else FUN_1805b1f10 -> FUN_1805b0fb0 + FUN_1804b7f40; decomp/partline/ev_dec.c), the
   same transliteration as viewer/mcpartline_mesh.c (nurb_span .. nb_quotient, checked bit for bit there). The nbs_type at surf +0x98:
   +0 nku, +4 nkv, +8 ncu, +0xc ncv, +0x10 values per point, +0x11 / +0x12 degrees, +0x13 bit 0 non-rational; knots at +0x18, points
   at +0x20 (u fastest; rational ones (w, wx, wy, wz)). E: P, Su, Sv, Suu, Suv, Svv, Suuu, Suuv, Suvv, Svvv (eval_surf's p_3d array, as
   calc_surf_norm reads it) up to `order` (3 at most) */
/* nurb_interval (0x1805b0630, ev_dec.c l.2084): the knot span of t (clamped to the vector; a knot counts from 1e-10 below it), a
   zero-length span moved down to the last knot below t */
static int nurb_span(double t, const double *K, int nk, int deg) {
    if (t < K[0]) t = K[0];
    if (K[nk - 1] < t) t = K[nk - 1];
    int i3 = deg + 1, i4 = nk - i3, i1;
    for (long l = i3; ; l++, i3++) {
        i1 = i4; if (!(l <= i4)) break;
        i1 = i3; if (!(K[l] - 1e-10 <= t)) break;
    }
    i1 = i1 - 1;
    int r = i1;
    if (K[i1 + 1] - K[i1] <= 1e-10) {
        int j = nk - 1; long l = j;
        if (0 < j) for (;;) { r = j; if (!(t <= K[l] + 1e-10)) break; l--; j--; if (l < 1) return i1; }
    }
    return r;
}
/* one level s of the Cox - de Boor triangle as FUN_1805aeb50 / FUN_1805aeff0 do it: N[i] = (t - K[i]) / (K[i+s] - K[i]) * N[i] (0 when
   either difference is below 1e-12), then + (K[i+s+1] - t) * N[i+1] / (K[i+s+1] - K[i+1]) when that product and that difference are
   both at least 1e-12 */
static void cdb_level(double t, const double *K, int nk, int span, int s, double *N) {
    int i = span - s; if (i < 0) i = 0;
    for (; i <= span; i++) {
        if (!(i <= nk - 2 - s)) continue;
        double d1 = t - K[i], d2 = K[i + s] - K[i], val;
        if (fabs(d2) < 1e-12 || fabs(d1) < 1e-12) { val = 0.0; N[i] = 0.0; }
        else { val = (d1 / d2) * N[i]; N[i] = val; }
        double e = K[i + s + 1], den = e - K[i + 1], num = (e - t) * N[i + 1];
        if (1e-12 <= fabs(den) && 1e-12 <= fabs(num)) N[i] = num / den + val;
    }
}
/* FUN_1805aeb50 (ev_dec.c l.827): the degree deg basis N[0 .. nk) */
static void basis_eval(double t, const double *K, int nk, int span, int deg, double *N) {
    for (int i = 0; i < nk; i++) N[i] = 0.0;
    N[span] = 1.0;
    for (int s = 1; s <= deg; s++) cdb_level(t, K, nk, span, s, N);
}
/* FUN_1805aeff0 (ev_dec.c l.2132): the basis functions of every degree 0 .. deg, degree s at out[s(s+1)/2 ..] = N[span - s .. span] */
static void basis_all(double t, const double *K, int nk, int span, int deg, double *N, double *out) {
    for (int i = 0; i < nk; i++) N[i] = 0.0;
    N[span] = 1.0; out[0] = 1.0;
    int k = 1;
    for (int s = 1; s <= deg; s++) {
        cdb_level(t, K, nk, span, s, N);
        int i = span - s; if (i < 0) i = 0;
        for (; i <= span; i++) out[k++] = N[i];
    }
}
typedef struct { int nku, nkv, ncu, ncv, vpp, du, dv, nonrat; const double *K, *cp; } NbRec;
static int nb_open(const uint8_t *s, NbRec *r) {
    r->nku = I32(s, 0x98); r->nkv = I32(s, 0x9c); r->ncu = I32(s, 0xa0); r->ncv = I32(s, 0xa4);
    r->vpp = U8(s, 0xa8); r->du = U8(s, 0xa9); r->dv = U8(s, 0xaa); r->nonrat = U8(s, 0xab) & 1;
    r->K = *(const double **)(s + 0xb0); r->cp = *(const double **)(s + 0xb8);
    if (!r->K || !r->cp || r->vpp < 1 || r->vpp > 4 || r->du < 1 || r->dv < 1 || r->du > 14 || r->dv > 14 || r->nku < r->du + 2 || r->nkv < r->dv + 2 || r->ncu < 1 || r->ncv < 1) return -1;
    return 0;
}
/* FUN_1805b08d0 (ev_dec.c l.1010): order 0: the sum over v (outer) and u (inner) of P * (Nu * Nv), / w when rational (not when |w| <= 1e-12) */
static void nb_point(const NbRec *r, double u, double v, double out[3]) {
    int us = nurb_span(u, r->K, r->nku, r->du), u0 = us - r->du; if (u0 < 0) u0 = 0;
    int vs = nurb_span(v, r->K + r->nku, r->nkv, r->dv), v0 = vs - r->dv; if (v0 < 0) v0 = 0;
    double Nb[512], *Nu = r->nku + r->nkv + 2 <= 512 ? Nb : calloc((size_t)r->nku + r->nkv + 2, sizeof(double)), *Nv = Nu + r->nku;
    basis_eval(u, r->K, r->nku, us, r->du, Nu);
    basis_eval(v, r->K + r->nku, r->nkv, vs, r->dv, Nv);
    double acc[4] = { 0, 0, 0, 0 };
    for (int j = v0; j <= vs; j++) {
        if (!(j < r->ncv)) continue;
        for (int i = u0; i <= us; i++) {
            if (!(i < r->ncu)) continue;
            double w = Nu[i] * Nv[j];
            for (int c = 0; c < r->vpp; c++) acc[c] = r->cp[((size_t)r->ncu * j + i) * r->vpp + c] * w + acc[c];
        }
    }
    if (Nu != Nb) free(Nu);
    if (r->nonrat) for (int c = 0; c < r->vpp && c < 3; c++) out[c] = acc[c];
    else if (fabs(acc[0]) <= 1e-12) { for (int c = 1; c < r->vpp; c++) out[c - 1] = acc[c]; }
    else for (int c = 1; c < r->vpp; c++) out[c - 1] = acc[c] / acc[0];
}
/* FUN_1805b0fb0 (ev_dec.c l.1403): the homogeneous derivatives H[(k+l)(k+l+1)/2 + l][0 .. vpp) for k + l <= d: the span's control net
   copied (FUN_1804b7b20) and differenced in place k times along u, then l times along v, then the sum over u (outer) and v (inner) of
   (Nu * Nv) * P with the degree p - k / q - l basis of FUN_1805aeff0 */
static void nb_derivs(const NbRec *r, int d, double u, double v, double H[][4]) {
    int p = r->du, q = r->dv, vpp = r->vpp;
    const double *KU = r->K, *KV = r->K + r->nku;
    int us = nurb_span(u, KU, r->nku, p), vs = nurb_span(v, KV, r->nkv, q);
    int u0 = us - p; if (u0 < 0) u0 = 0;
    int v0 = vs - q; if (v0 < 0) v0 = 0;
    double Nb[512], *Nt = r->nku + r->nkv + 2 <= 512 ? Nb : calloc((size_t)r->nku + r->nkv + 2, sizeof(double));
    double NuA[136], NvA[136], B[15 * 15 * 4];   /* degrees <= 14 (nb_open) */
    basis_all(u, KU, r->nku, us, p, Nt, NuA);
    basis_all(v, KV, r->nkv, vs, q, Nt, NvA);
    size_t rowl = (size_t)(p + 1) * vpp;
#define BB(J, I, c) B[(size_t)(J) * rowl + (size_t)(I) * vpp + (c)]
    for (int k = 0; k <= d; k++) {
        for (int j = 0; j <= q; j++) for (int i = 0; i <= p; i++) for (int c = 0; c < vpp; c++)   /* FUN_1804b7b20 */
            BB(j, i, c) = r->cp[(((size_t)(j + v0) * r->ncu) + u0 + i) * vpp + c];
        int iv = p;
        for (int f0 = 0; f0 < k; f0++, iv--) {
            for (int I = p; I > f0; I--) {
                if (!((p - f0) + I + u0 < r->nku)) continue;
                double den = KU[(p - f0) + I + u0] - KU[I + u0];
                double cf = fabs(den) <= 1e-12 ? 0.0 : (double)iv / den;
                for (int j = 0; j <= q; j++) for (int c = 0; c < vpp; c++) BB(j, I, c) = (BB(j, I, c) - BB(j, I - 1, c)) * cf;
            }
        }
        for (int l = 0; l <= d - k; l++) {
            if (l > 0 && l <= q) {
                int a = q + 1 - l;
                for (int J = q; J >= l; J--) {
                    if (!(J + a + v0 < r->nkv)) continue;
                    double den = KV[a + J + v0] - KV[J + v0];
                    double cf = fabs(den) <= 1e-12 ? 0.0 : (double)a / den;
                    for (int i = 0; i <= p; i++) for (int c = 0; c < vpp; c++) BB(J, i, c) = (BB(J, i, c) - BB(J - 1, i, c)) * cf;
                }
            }
            double acc[4] = { 0, 0, 0, 0 };
            for (int I = k; I <= p; I++) {
                double nu = NuA[(p - k) * (p - k + 1) / 2 + (I - k)];
                for (int J = l; J <= q; J++) {
                    double w = nu * NvA[(q - l) * (q - l + 1) / 2 + (J - l)];
                    for (int c = 0; c < vpp; c++) acc[c] = w * BB(J, I, c) + acc[c];
                }
            }
            double *h = H[(k + l) * (k + l + 1) / 2 + l];
            for (int c = 0; c < vpp; c++) h[c] = acc[c];
        }
    }
#undef BB
    if (Nt != Nb) free(Nt);
}
/* FUN_1804b7f40 (ev_dec.c l.330): the rational derivatives from the homogeneous ones H[e] = (w, wx, wy, wz) (w = H[0][0], |w| < 1e-12 -> 1e-12) */
static void nb_quotient(double H[][4], int d, double D[][3]) {
    double w = H[0][0]; if (fabs(w) < 1e-12) w = 1e-12;
    for (int c = 0; c < 3; c++) D[0][c] = H[0][1 + c] / w;
    if (d > 0) for (int c = 0; c < 3; c++) {
        D[1][c] = (H[1][1 + c] - D[0][c] * H[1][0]) / w;
        D[2][c] = (H[2][1 + c] - H[2][0] * D[0][c]) / w;
    }
    if (d > 1) for (int c = 0; c < 3; c++) {
        D[3][c] = ((H[3][1 + c] - D[1][c] * 2.0 * H[1][0]) - H[3][0] * D[0][c]) / w;
        D[4][c] = (((H[4][1 + c] - D[1][c] * H[2][0]) - D[2][c] * H[1][0]) - D[0][c] * H[4][0]) / w;
        D[5][c] = ((H[5][1 + c] - D[2][c] * 2.0 * H[2][0]) - D[0][c] * H[5][0]) / w;
    }
    if (d > 2) for (int c = 0; c < 3; c++) {
        D[6][c] = (((H[6][1 + c] - D[3][c] * 3.0 * H[1][0]) - D[1][c] * 3.0 * H[3][0]) - H[6][0] * D[0][c]) / w;
        D[7][c] = (((((H[7][1 + c] - D[4][c] * 2.0 * H[1][0]) - H[2][0] * D[3][c]) - D[1][c] * 2.0 * H[4][0]) - H[3][0] * D[2][c]) - D[0][c] * H[7][0]) / w;
        D[8][c] = (((((H[8][1 + c] - D[4][c] * 2.0 * H[2][0]) - D[5][c] * H[1][0]) - D[2][c] * 2.0 * H[4][0]) - D[1][c] * H[5][0]) - H[8][0] * D[0][c]) / w;
        D[9][c] = (((H[9][1 + c] - D[5][c] * 3.0 * H[2][0]) - D[2][c] * 3.0 * H[5][0]) - H[9][0] * D[0][c]) / w;
    }
}
/* FUN_1805b1f10 with param_6 0 (no rescaling to the knot range): non-rational H copied, rational FUN_1804b7f40 */
static int eval_nbs(const uint8_t *s, int order, double u, double v, double *E) {
    NbRec r; if (nb_open(s, &r)) return -1;
    if (order <= 0) { nb_point(&r, u, v, E); return 0; }
    if (order > 3) order = 3;
    int ne = (order + 1) * (order + 2) / 2;
    double H[10][4]; memset(H, 0, sizeof H);
    nb_derivs(&r, order, u, v, H);
    if (!r.nonrat) nb_quotient(H, order, (double (*)[3])E);
    else for (int e = 0; e < ne; e++) for (int c = 0; c < 3; c++) E[3 * e + c] = H[e][c];
    return 0;
}
/* eval_surf 0x18047ba50: -1 without the surf_type +6 bit 0x40; kind 10 evaluates its base (+0x98); kinds other than 0 / 10: -2 */
static int eval_surf(const uint8_t *s, int order, double u, double v, double *E) {
    if (!(U8(s, 6) & 0x40)) return -1;
    if (s[0] == 0) return eval_nbs(s, order, u, v, E);
    if (s[0] == 10) { const uint8_t *b = PTR(s, 0x98); return b ? eval_surf(b + 0x68, order, u, v, E) : -2; }
    return -2;
}
/* eval_surf_extrap 0x18047bf10: outside the range of a non-periodic direction the point is extended along the boundary
   derivative (*inu / *inv = 0); a periodic one wraps (+6 bits 0 / 1). Near a boundary (0.01 in the parameter) the point is
   taken as on it when the boundary evaluation is within 10 tol (25.4 x in mm) */
static int eval_surf_extrap(const uint8_t *s, int order, double u, double v, char *inu, char *inv, double *out) {
    double tol10 = tol * 10.0;
    if (!(U8(s, 6) & 0x40)) return -1;
    if (is_metric) tol10 *= 25.4;
    for (int k = 0; k < order; k++) { out[3 + 3 * k] = 0; out[4 + 3 * k] = 0; out[5 + 3 * k] = 1.0; }   /* v_z_3d into the derivative slots */
    double u0 = D(s, 8), u1 = D(s, 0x10), v0 = D(s, 0x18), v1 = D(s, 0x20); int fl = U8(s, 6);
    *inu = 1; *inv = 1;
    double du = 0.0, dv = 0.0;
    if (u < u0) { if (!(fl & 1)) { *inu = 0; du = u - u0; u = u0; } else { do { u += u1 - u0; du = 0.0; } while (u < u0); } }
    if (u1 < u) { if (!(fl & 1)) { *inu = 0; du = u - u1; u = u1; } else { do u -= u1 - u0; while (u1 < u); } }
    if (v < v0) { if (!(fl & 2)) { *inv = 0; dv = v - v0; v = v0; } else { do { v += v1 - v0; dv = 0.0; } while (v < v0); } }
    if (v1 < v) { if (!(fl & 2)) { *inv = 0; dv = v - v1; v = v1; } else { do v -= v1 - v0; while (v1 < v); } }
    int ord = order; if (ord == 0 && (!*inu || !*inv)) ord = 1;
    double E[18]; memset(E, 0, sizeof E);
    int r = eval_surf(s, ord, u, v, E); if (r) return r;
    memcpy(out, E, 24);
    if (order > 0) { memcpy(out + 3, E + 3, 48); if (order > 1) memcpy(out + 9, E + 9, 72); }
    if (!*inu) for (int q = 0; q < 3; q++) out[q] += E[3 + q] * du;
    if (!*inv) for (int q = 0; q < 3; q++) out[q] += E[6 + q] * dv;
    int g = extrap_guard;
    if (!*inu && !extrap_guard) {
        double d = u - u0;
        if (0.01 < fabs(d) || fabs(d) == 0.01) {
            d = u - u1;
            if (fabs(d) <= 0.01 && fabs(d) != 0.01) {
                char a, b; double P[21]; extrap_guard = 1; eval_surf_extrap(s, 0, u1, v, &a, &b, P); g = 0; extrap_guard = 0;
                if (dist3(P, out) <= tol10) { *inu = 1; u = u1; }
            }
        } else {
            char a, b; double P[21]; extrap_guard = 1; eval_surf_extrap(s, 0, u0, v, &a, &b, P); extrap_guard = g;
            if (dist3(P, out) <= tol10) { *inu = 1; u = u0; }
        }
    }
    if (!*inv && !g) {
        double w = v0;
        if ((fabs(v - w) <= 0.01 && fabs(v - w) != 0.01) || (w = v1, fabs(v - w) <= 0.01 && fabs(v - w) != 0.01)) {
            char a, b; double P[21]; extrap_guard = 1; eval_surf_extrap(s, 0, u, w, &a, &b, P); extrap_guard = 0;
            if (dist3(P, out) <= tol10) *inv = 1;
        }
    }
    return 0;
}
/* FUN_1806faa70 with offset 0 (the only use here: FUN_1805ed4a0 param_5 = 0): eval_surf_extrap order 2 into E (P, Su, Sv, Suu,
   Suv, Svv) */
static void eval_offset0(const uint8_t *s, double u, double v, char *inu, char *inv, double *E) {
    double B[21]; memset(B, 0, sizeof B);
    eval_surf_extrap(s, 2, u, v, inu, inv, B);
    memcpy(E, B, 18 * sizeof(double));
}
/* surf_curv_dir 0x18047ce10 (FUN_18047c530 the first fundamental form, FUN_18047c8b0 the second with UnitNormal(Su, Sv)): the normal
   curvature in the direction d */
static double surf_curv_dir(const double *E, const double *d) {
    double e = E[4] * E[4] + E[3] * E[3] + E[5] * E[5], f = E[4] * E[7] + E[6] * E[3] + E[5] * E[8], g = E[7] * E[7] + E[6] * E[6] + E[8] * E[8];
    double N[3]; unit_normal(E + 3, E + 6, N);
    double L = N[1] * E[10] + N[0] * E[9] + N[2] * E[11], M = N[1] * E[13] + N[0] * E[12] + N[2] * E[14], Nn = N[1] * E[16] + N[0] * E[15] + N[2] * E[17];
    double a = d[0], b = d[1];
    /* the numerator's sums as the code adds them (disassembly 18047ce96 / 18047ceb7: b Nn b + (M 2 a b + a L a); the decompile prints them
       left to right) */
    return (b * Nn * b + (M * 2.0 * a * b + a * L * a)) / (f * 2.0 * a * b + a * e * a + b * g * b);
}
/* FUN_1805ed4a0 (decomp/curvesurf/cs2.c 5085): the point P projected along the surface normal from the start uv (Newton with the
   curvature correction, 56 steps at most); view projection (param_2 >= 0) and the offset are not used by this command.
   Out: uv, the surface point S, *inu / *inv (on the range), *ok */
static void project_core0(const uint8_t *s, const double *P, const double *uv0, double *uv, char *inu, char *inv, char *ok, double *Sout);
/* tests: CSURF_PROJ_TRACE prints each projection as tools/peloader --csurf does with the same variable (FUN_1805ed4a0 hooked) */
static void project_core(const uint8_t *s, const double *P, const double *uv0, double *uv, char *inu, char *inv, char *ok, double *Sout) {
    double u0[2] = { uv0[0], uv0[1] };
    project_core0(s, P, u0, uv, inu, inv, ok, Sout);
    if (getenv("CSURF_PROJ_TRACE")) fprintf(stderr, "PROJ P %a %a %a uv0 %a %a -> uv %a %a ok %d inu %d inv %d\n", P[0], P[1], P[2], u0[0], u0[1], uv[0], uv[1], *ok, *inu, *inv);
}
static void project_core0(const uint8_t *s, const double *P, const double *uv0, double *uv, char *inu, char *inv, char *ok, double *Sout) {
    double t = tol; if (is_metric) t *= 25.4;
    double du_r = D(s, 0x10) - D(s, 8), dv_r = D(s, 0x20) - D(s, 0x18);
    double uvl[2] = { uv0[0], uv0[1] }, sc_u = 1.0, sc_v = 1.0, prevSu[3] = { 0 }, prevSv[3] = { 0 };
    int stop = 0, it = 0; *ok = 0;
    double E[18], d[3];
    for (;;) {
        eval_offset0(s, uvl[0], uvl[1], inu, inv, E);
        d[0] = P[0] - E[0]; d[1] = P[1] - E[1]; d[2] = P[2] - E[2];
        if (sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) < t) break;
        double N[3]; unit_normal(E + 3, E + 6, N);
        double dl = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), dn[3] = { 0, 0, 0 };   /* the sums in the order of the code (disassembly 1805ed76b-1805ed788) */
        if (dl != 0.0) { dn[0] = d[0] / dl; dn[1] = d[1] / dl; dn[2] = d[2] / dl; }
        double c = fabs(N[1] * dn[1] + N[0] * dn[0] + N[2] * dn[2]); if (1.0 < c) c = 1.0;
        double ang = acos(c);
        if (!(1e-06 < fabs(ang)) && fabs(ang) != 1e-06) break;   /* along the normal: done */
        if (*ok) goto fin;
        if (it != 0) {
            sc_u = sqrt(E[3] * E[3] + E[4] * E[4] + E[5] * E[5]) / sqrt(prevSu[0] * prevSu[0] + prevSu[1] * prevSu[1] + prevSu[2] * prevSu[2]);
            sc_v = sqrt(E[6] * E[6] + E[7] * E[7] + E[8] * E[8]) / sqrt(prevSv[0] * prevSv[0] + prevSv[1] * prevSv[1] + prevSv[2] * prevSv[2]);
        }
        double a[3] = { E[3] * sc_u, E[4] * sc_u, E[5] * sc_u }, b[3] = { E[6] * sc_v, E[7] * sc_v, E[8] * sc_v };
        double dt = det3(a, b, N);
        if (getenv("CSURF_IT") && fabs(uv0[0] - atof(getenv("CSURF_IT"))) < 1e-12) { fprintf(stderr, "  EOFF u %a v %a E", uvl[0], uvl[1]); for (int i_ = 0; i_ < 18; i_++) fprintf(stderr, " %a", E[i_]); fprintf(stderr, "\n"); }   /* tests: as the oracle's CSURF_PROJ_U0 */
        if (1e-12 <= fabs(dt)) {
            double step[2] = { det3(d, b, N) / dt, det3(a, d, N) / dt };
            double k = surf_curv_dir(E, step), q = 1.0 - k * ((N[0] * d[0] + N[1] * d[1] + N[2] * d[2]) * 1.25);   /* k (dot x 1.25): 1805edb03 / 1805edb0b */
            if (1e-12 <= fabs(q) && fabs(q) != 1e-12) { step[0] /= q; step[1] /= q; }
            if ((du_r <= fabs(step[0]) && fabs(step[0]) != du_r) || dv_r < fabs(step[1])) stop = 1;
            else {
                uvl[0] += step[0]; uvl[1] += step[1];
                if (fabs(step[1]) + fabs(step[0]) < 1e-06 && 10 < it) { stop = 1; *ok = 1; }
            }
        } else { *ok = 0; stop = 1; }
        if (0x37 < it) stop = 1;
        it++;
        memcpy(prevSu, E + 3, 24); memcpy(prevSv, E + 6, 24);
        if (stop) { if (!*ok) return; goto fin; }
        if (U8(s, 6) & 1) { while (uvl[0] < D(s, 8) - 1e-06) uvl[0] += du_r; while (D(s, 0x10) + 1e-06 < uvl[0]) uvl[0] -= du_r; }
        if (U8(s, 6) & 2) { while (uvl[1] < D(s, 0x18) - 1e-06) uvl[1] += dv_r; while (D(s, 0x20) + 1e-06 < uvl[1]) uvl[1] -= dv_r; }
    }
    *ok = 1;
fin:
    if (fabs(uvl[0] - D(s, 8)) <= 1e-06 && fabs(uvl[0] - D(s, 8)) != 1e-06) { *inu = 1; uvl[0] = D(s, 8); }
    if (fabs(uvl[0] - D(s, 0x10)) < 1e-06) { *inu = 1; uvl[0] = D(s, 0x10); }
    if (fabs(uvl[1] - D(s, 0x18)) <= 1e-06 && fabs(uvl[1] - D(s, 0x18)) != 1e-06) { *inv = 1; uvl[1] = D(s, 0x18); }
    if (fabs(uvl[1] - D(s, 0x20)) < 1e-06) { *inv = 1; uvl[1] = D(s, 0x20); }
    uv[0] = uvl[0]; uv[1] = uvl[1];
    memcpy(Sout, E, 24);
}
/* in_polygon 0x18046c960: 1 inside, -1 outside, 0 on the boundary (1e-10); loops = (start, count) pairs, closed back */
static int in_polygon(int nl, const double *pts, const int32_t *loops, const double *p) {
    int odd = 0; double y = p[1] - 1e-10, xl = p[0] - 1e-10, xh = p[0] + 1e-10;
    for (int l = 0; l < nl; l++) {
        long i = loops[2 * l], last = loops[2 * l + 1] + loops[2 * l];
        double qx = pts[2 * last], qy = pts[2 * last + 1];
        for (; i <= last; i++) {
            double x1 = pts[2 * i], y1 = pts[2 * i + 1], dy = y1 - qy;
            if (1e-10 <= dy || 1e-10 <= -dy) {
                if ((qy < y && y <= y1) || (y1 <= y && y < qy)) {
                    double xi = ((p[1] - qy) * (x1 - qx)) / dy + qx, dd = p[0] - xi;
                    if (dd < 1e-10 && -dd < 1e-10) return 0;
                    if (p[0] < xi) odd ^= 1;
                }
            } else {
                double e = qy - p[1];
                if (e < 1e-10 && (-e <= 1e-10 && -e != 1e-10) && ((xl <= qx && x1 <= xh) || (qx <= xh && xl <= x1))) return 0;
            }
            qx = x1; qy = y1;
        }
    }
    return odd ? 1 : -1;
}
static int trim_in(const uint8_t *s, const double *uv) {   /* in_polygon on a trimmed surf_type: +0xc0 loops, +0xa8 the uv data, +0xa4 points */
    const double *pts = *(const double **)(s + 0xa8);
    return in_polygon(I32(s, 0xc0), pts, (const int32_t *)(pts + 2 * I32(s, 0xa4)), uv);
}
/* project_pt_onto_surf 0x1805edee0 (param_2 = -1, param_3 = 1 here): FUN_1805ed4a0, then a trimmed surface's loop */
static int project_pt(const uint8_t *s, const double *P, const double *uv0, double *uv, double *S) {
    char inu = 0, inv = 0, ok = 0;
    project_core(s, P, uv0, uv, &inu, &inv, &ok, S);
    if (ok && s[0] == 10) return trim_in(s, uv) != -1;
    return ok;
}
/* FUN_1806f71a0: the uv against the range (within 1e-8 snapped onto it); bits 8 u < u0, 2 u > u1, 1 v < v0, 4 v > v1 (clamped when clamp) */
static int range_bits(const uint8_t *s, int clamp, double *uv) {
    int b = 0;
    if (1e-08 <= fabs(uv[0] - D(s, 8))) { if (fabs(uv[0] - D(s, 0x10)) < 1e-08) uv[0] = D(s, 0x10); } else uv[0] = D(s, 8);
    if (D(s, 8) <= uv[0]) { if (D(s, 0x10) < uv[0]) { if (clamp) uv[0] = D(s, 0x10); b = 2; } }
    else { if (clamp) uv[0] = D(s, 8); b = 8; }
    if (1e-08 <= fabs(uv[1] - D(s, 0x18))) { if (fabs(uv[1] - D(s, 0x20)) < 1e-08) uv[1] = D(s, 0x20); } else uv[1] = D(s, 0x18);
    if (uv[1] < D(s, 0x18)) { if (clamp) uv[1] = D(s, 0x18); return b | 1; }
    if (D(s, 0x20) < uv[1]) { if (clamp) uv[1] = D(s, 0x20); b |= 4; }
    return b;
}
/* FUN_18053a9f0: a uv within 0.01 of a boundary whose boundary point is within surf_step_chord_dev moves onto it (its range bit
   cleared, the point replaced); returns whether one moved */
static int snap_edges(int *bits, const uint8_t *s, double *uv, double *P) {
    int moved = 0; char a, b; double E[21];
    if (fabs(uv[1] - D(s, 0x18)) <= 0.01 && fabs(uv[1] - D(s, 0x18)) != 0.01) {
        eval_surf_extrap(s, 2, uv[0], D(s, 0x18), &a, &b, E);
        if (dist3(E, P) <= surf_step_chord_dev) { *bits &= 0xfffe; uv[1] = D(s, 0x18); moved = 1; memcpy(P, E, 24); }
    }
    if (fabs(uv[0] - D(s, 0x10)) <= 0.01 && fabs(uv[0] - D(s, 0x10)) != 0.01) {
        eval_surf_extrap(s, 2, D(s, 0x10), uv[1], &a, &b, E);
        if (dist3(E, P) <= surf_step_chord_dev) { *bits &= 0xfffd; uv[0] = D(s, 0x10); moved = 1; memcpy(P, E, 24); }
    }
    if (fabs(uv[1] - D(s, 0x20)) <= 0.01 && fabs(uv[1] - D(s, 0x20)) != 0.01) {
        eval_surf_extrap(s, 2, uv[0], D(s, 0x20), &a, &b, E);
        if (dist3(E, P) <= surf_step_chord_dev) { *bits &= 0xfffb; uv[1] = D(s, 0x20); moved = 1; memcpy(P, E, 24); }
    }
    if (fabs(uv[0] - D(s, 8)) <= 0.01 && fabs(uv[0] - D(s, 8)) != 0.01) {
        eval_surf_extrap(s, 2, D(s, 8), uv[1], &a, &b, E);
        if (dist3(E, P) <= surf_step_chord_dev) { *bits &= 0xfff7; uv[0] = D(s, 8); memcpy(P, E, 24); return 1; }
    }
    return moved;
}
/* check_edge 0x18047b620 / check_seam 0x18047b990 */
static int check_edge(const uint8_t *s, double *uv, double *P) {
    int r = 0; char a, b; double E[21];
    double d = uv[0] - D(s, 8);
    if (0.01 < fabs(d) || fabs(d) == 0.01) {
        if (fabs(uv[0] - D(s, 0x10)) < 0.01) {
            eval_surf_extrap(s, 0, D(s, 0x10), uv[1], &a, &b, E);
            if (dist3(P, E) < surf_step_chord_dev) { r = 2; memcpy(P, E, 24); uv[0] = D(s, 0x10); }
        }
    } else {
        eval_surf_extrap(s, 0, D(s, 8), uv[1], &a, &b, E);
        if (dist3(P, E) < surf_step_chord_dev) { r = 8; memcpy(P, E, 24); uv[0] = D(s, 8); }
    }
    d = uv[1] - D(s, 0x18);
    if (0.01 < fabs(d) || fabs(d) == 0.01) {
        if (fabs(uv[1] - D(s, 0x20)) < 0.01) {
            eval_surf_extrap(s, 0, uv[0], D(s, 0x20), &a, &b, E);
            if (dist3(P, E) < surf_step_chord_dev) { r |= 4; memcpy(P, E, 24); uv[1] = D(s, 0x20); }
        }
    } else {
        eval_surf_extrap(s, 0, uv[0], D(s, 0x18), &a, &b, E);
        if (dist3(P, E) < surf_step_chord_dev) { r |= 1; memcpy(P, E, 24); uv[1] = D(s, 0x18); }
    }
    return r;
}
static int check_seam(const uint8_t *s, double *uv, double *P) {
    int a = 0, b = 0;
    if (U8(s, 6) & 1) { int e = check_edge(s, uv, P); if (e & 2 || e & 8) a = 1; }
    if (U8(s, 6) & 2) { int e = check_edge(s, uv, P); b = (e & 1) || (e & 4); }
    return a || b;
}
/* FUN_180556520: on a periodic direction, a uv at the seam (and the previous one not) is wrapped when wrap; the bits as range_bits */
static int seam_wrap(int wrap, const uint8_t *s, double *uv, double *uvp, double *P, double *Pp) {
    int c3 = check_seam(s, uv, P), c4 = 0;
    if (uvp && Pp) c4 = check_seam(s, uvp, Pp);
    int r = 0;
    if ((U8(s, 6) & 1) && (!c3 || !c4)) {
        double u0 = D(s, 8), u = uv[0];
        if (u0 + 1e-10 < u) { r = 0; if (D(s, 0x10) - 1e-10 <= u) { r = 2; if (wrap) uv[0] = u - (D(s, 0x10) - u0); } }
        else { r = 8; if (wrap) uv[0] = (D(s, 0x10) - u0) + u; }
    }
    if ((U8(s, 6) & 2) && (!c3 || !c4)) {
        double v0 = D(s, 0x18), v = uv[1];
        if (v <= v0 + 1e-10) { r |= 1; if (wrap) uv[1] = (D(s, 0x20) - v0) + v; }
        else if (D(s, 0x20) - 1e-10 < v) { r |= 4; if (wrap) uv[1] = v - (D(s, 0x20) - v0); }
    }
    return r;
}
/* FUN_180551630: a seam crossing (bits) moves uv to the other side (to = 0) or back (to = 1) */
static void seam_side(int to, int bits, double *uv, const uint8_t *s) {
    if (bits & 1) uv[1] = to ? D(s, 0x18) : D(s, 0x20);
    if (bits & 2) uv[0] = to ? D(s, 0x10) : D(s, 8);
    if (bits & 4) uv[1] = to ? D(s, 0x20) : D(s, 0x18);
    if (bits & 8) uv[0] = to ? D(s, 8) : D(s, 0x10);
}
/* FUN_1805353a0: inside the trim loops (not outside) / inside the range */
static int in_face(const uint8_t *s, const double *uv) {
    if (s[0] == 10) return trim_in(s, uv) != -1;
    double q[2] = { uv[0], uv[1] }; return range_bits(s, 0, q) == 0;
}
/* FUN_180534200: the surface between two uv points (at 1/2, 3/4 and 1/4) within dev of the chord between their points */
static int chord_on_surf(const uint8_t *s, const double *uva, const double *uvb, const double *Pa, const double *Pb, double dev) {
    double l[6] = { Pa[0], Pa[1], Pa[2], Pb[0], Pb[1], Pb[2] }, E[21]; char a, b;
    eval_surf_extrap(s, 2, (uva[0] + uvb[0]) * 0.5, (uvb[1] + uva[1]) * 0.5, &a, &b, E);
    if (!(dist_pt_l_3d(E, l) <= dev)) return 0;
    eval_surf_extrap(s, 2, uva[0] * 0.25 + uvb[0] * 0.75, uva[1] * 0.25 + uvb[1] * 0.75, &a, &b, E);
    if (!(dist_pt_l_3d(E, l) <= dev)) return 0;
    eval_surf_extrap(s, 2, uva[0] * 0.75 + uvb[0] * 0.25, uva[1] * 0.75 + uvb[1] * 0.25, &a, &b, E);
    return dist_pt_l_3d(E, l) <= dev;
}

/* ------------------------------------------------------------------------------------------------ curves */
typedef struct { const uint8_t *r; uint32_t n; int cls; const double *vm; CurveRec cr; } Crv;
static double line_len(const uint8_t *r) {   /* length_ent / FUN_180553740 of a line: sqrt(dx^2 + dy^2 + dz^2) */
    double x = D(r, 0x80) - D(r, 0x68), y = D(r, 0x88) - D(r, 0x70), z = D(r, 0x90) - D(r, 0x78); return sqrt(x * x + y * y + z * z);
}
static void crv_t1t2(const Crv *c, double *t1, double *t2) {   /* curve_t1t2 0x1803fc9a0 */
    if (c->cls == 2 || c->cls == 4) { *t1 = 0.0; *t2 = 1.0; return; }
    *t1 = D(c->r, 0x98); *t2 = D(c->r, 0xa0);
}
static void v2w(const Crv *c, const double *v, double *w) {   /* view_to_world: world = M^T v (formats/mcam.md "Arc views") */
    if (!c->vm) { w[0] = v[0]; w[1] = v[1]; w[2] = v[2]; return; }
    const double *m = c->vm;
    w[0] = v[1] * m[3] + v[0] * m[0] + v[2] * m[6]; w[1] = v[1] * m[4] + v[0] * m[1] + v[2] * m[7]; w[2] = v[1] * m[5] + v[0] * m[2] + v[2] * m[8];
}
/* eval_curve 0x1803fcd10: o[0..2] the point, o[3..5] / o[6..8] the derivatives (order <= 2) */
static void eval_crv(const Crv *c, int order, double t, double *o) {
    memset(o, 0, 9 * sizeof(double));
    if (c->cls == 2) {
        const uint8_t *r = c->r; double s = 1.0 - t;
        for (int q = 0; q < 3; q++) { o[q] = s * D(r, 0x68 + 8 * q) + t * D(r, 0x80 + 8 * q); if (order > 0) o[3 + q] = D(r, 0x80 + 8 * q) - D(r, 0x68 + 8 * q); }
    } else if (c->cls == 4) {
        const uint8_t *r = c->r; double R = D(r, 0xb0), sw = D(r, 0xc0), a = t * sw + D(r, 0xb8);
        double rv[3] = { cos(a) * R, sin(a) * R, 0.0 }, v[3] = { rv[0] + D(r, 0x98), rv[1] + D(r, 0xa0), D(r, 0xa8) + 0.0 };
        v2w(c, v, o);
        for (int k = 1; k <= order; k++) {   /* each order: the previous vector turned 90 degrees, times the sweep */
            double q[3] = { -rv[1] * sw, rv[0] * sw, 0.0 }; v2w(c, q, o + 3 * k); rv[0] = q[0]; rv[1] = q[1];
        }
    } else {   /* eval_curve 0x1803fcd10: class 8 eval_spline, class 0x80 eval_nurbs(nb, order, 1, t, out, 0) (mcspline.c) */
        double d[3][3]; mcs_spline_eval_d(&c->cr, t, order > 2 ? 2 : order, d);
        memcpy(o, d, 9 * sizeof(double));
    }
}
/* length_ent 0x180016e60 (splines: CurveLength -> MaCurve::Length, 16-point Gauss-Legendre of |C'| over the range: the nodes and
   weights of mccore .rdata 0x180d5b330 / 0x180d5b3b0, as viewer/mcsrfcrv.c iso_length) */
static double crv_length(const Crv *c) {
    if (c->cls == 2) return line_len(c->r);
    if (c->cls == 4) return fabs(D(c->r, 0xc0)) * D(c->r, 0xb0);
    static const double X[16] = { 0.00529953250417514, 0.0277124884633838, 0.067184398806084206, 0.122297795822499, 0.191061877798678, 0.27099161117138598, 0.35919822461037099, 0.45249374508118101, 0.54750625491881899, 0.64080177538962901, 0.72900838882861396, 0.808938122201322, 0.877702204177501, 0.93281560119391604, 0.97228751153661597, 0.99470046749582497 };
    static const double W_[16] = { 0.013576229705877, 0.031126761969323902, 0.0475792558412464, 0.062314485627766897, 0.074797994408288299, 0.084578259697501199, 0.091301707522461806, 0.094725305227534196, 0.094725305227534196, 0.091301707522461806, 0.084578259697501199, 0.074797994408288299, 0.062314485627766897, 0.0475792558412464, 0.031126761969323902, 0.013576229705877 };
    double t1, t2, sum = 0; crv_t1t2(c, &t1, &t2);
    if (c->cls == 0x80) {
        /* MaNURBSpline::Length (0x180450e70, decomp/curves/ma_classes.c; CurveLength -> vtable +0xc0 MaCurve::Length(void) -> +0xc8 with
           the curve's range): the 16-point rule (nodes / weights .data 0x1812f0880 / 0x1812f0900, the same values) on each span between
           GetUniqueKnots (0x18044eaf0: K[degree], the knots K[order] .. K[nk - order - 1] that differ from the one before, K[nk - order];
           MaNURBSpline +0x130 knots, +0x134 order, +0x138 order - 1) clipped to the range; MaCurve::Evaluate's derivative there */
        int nk = I32(c->r, 0xc8), ord = U8(c->r, 0xd0) + 1; const double *K = (const double *)(c->r + 0xd8);
        if (nk < 2 * ord) return 0.0;
        double *U = malloc(sizeof(double) * (size_t)nk); if (!U) return 0.0;
        double last = K[ord - 1]; int nu = 0; U[nu++] = last;
        for (int i = ord; i < nk - ord; i++) if (K[i] != last) { U[nu++] = K[i]; last = K[i]; }
        U[nu++] = K[nk - ord];
        for (int k = 0; k + 1 < nu; k++) {
            double k0 = U[k], k1 = U[k + 1];
            if (!(t1 < k1 && k0 < t2)) continue;
            double a = k0 < t1 ? t1 : k0, b = t2 < k1 ? t2 : k1;
            for (int i = 0; i < 16; i++) { double o[9]; eval_crv(c, 1, (b - a) * X[i] + a, o); sum = sqrt(o[3] * o[3] + o[4] * o[4] + o[5] * o[5]) * (b - a) * W_[i] + sum; }
        }
        free(U);
        return sum;
    }
    for (int i = 0; i < 16; i++) { double o[9]; eval_crv(c, 1, fabs(t2 - t1) * X[i] + t1, o); sum += sqrt(o[3] * o[3] + o[4] * o[4] + o[5] * o[5]) * fabs(t2 - t1) * W_[i]; }
    return sum;
}
/* FUN_180553740: the parameter `step` further along (back when rev): lines / arcs by their length (clamped to 0..1), splines
   brk_spline_const_err(ent, tol, step, 100, rev) (viewer/mcspline.c: min_step tol, max_step step, chord 100, systol tol) */
static void step_along(const Crv *c, double step, int rev, double *t) {
    if (c->cls == 2 || c->cls == 4) {
        double L = c->cls == 2 ? line_len(c->r) : fabs(D(c->r, 0xc0) * D(c->r, 0xb0));
        if (!rev) { *t = step / L + *t; if (1.0 < *t) *t = 1.0; }
        else { *t = *t - step / L; if (*t < 0.0) *t = 0.0; }
    } else mcs_brk_spline_const_err(&c->cr, tol, step, 100.0, rev, tol, t);
}

/* ------------------------------------------------------------------------------------------------ grids */
/* add_to_minmax 0x1805313c0: grown only by more than tol */
static void add_to_minmax(const double *p, double *mm) {
    for (int k = 0; k < 3; k++) {
        if (p[k] <= mm[2 * k] - tol && mm[2 * k] - tol != p[k]) mm[2 * k] = p[k];
        if (tol + mm[2 * k + 1] < p[k]) mm[2 * k + 1] = p[k];
    }
}
static void mm_init(double *mm) { for (int k = 0; k < 3; k++) { mm[2 * k] = 1e38; mm[2 * k + 1] = -1e38; } }
/* FUN_18054e400: the boxes a (x0 x1 y0 y1 z0 z1) and b overlap within tol */
static int boxes_overlap(const double *a, const double *b) {
    for (int k = 0; k < 6; k += 2) { double d = b[k] - tol; if ((a[k + 1] <= d && d != a[k + 1]) || (tol + b[k + 1] < a[k])) return 0; }
    return 1;
}
/* FUN_180541730 / FUN_1805419b0: the u / v grid counts (kinds 0 and 10 over a kind-0 base; find_base_surf) */
static int grid_count(const uint8_t *s, int dir) {
    int n;
    if (s[0] == 0) {
        if (dir == 0) n = (U8(s, 0xa9) + 1) * (I32(s, 0xa0) - 1);
        else { n = (U8(s, 0xaa) + 1) * (I32(s, 0xa4) - 1); if (!(U8(s, 0xab) & 1)) n *= 4; }
    } else if (s[0] == 10 && PTR(s, 0x98) && PTR(s, 0x98)[0x68] == 0) {
        const uint8_t *b = PTR(s, 0x98) + 0x68;
        n = grid_count(b, dir) - 1;
        int o = dir == 0 ? 8 : 0x18;
        int k = (int)(short)((short)(int)((D(s, o + 8) - D(s, o)) / ((D(b, o + 8) - D(b, o)) / (double)n)) + 1);
        if (k < n) n = k;
    } else return 0x13 + 1;
    if (n < 0x32) { if (n < 9) n = 9; } else n = 0x31;
    return n + 1;
}
static void free_grids(void) { free(g1p); free(g1uv); free(g1hit); g1p = NULL; g1uv = NULL; g1hit = NULL; free(g2p); free(g2t); g2p = NULL; g2t = NULL; }
/* init_surf_const(e, 0) 0x18047c600 / eval_surf_const 0x18047bd50 on a NURBS surface (kind 0), as build_grid runs them (decomp/
   chain_srfedge_undo.c build_grid: init_surf_const(psVar12, 0) - kind 0 always succeeds (calloc aside) - then eval_surf_const per grid
   point; for a trimmed surface psVar12 = its base): FUN_1805b2280 keeps the v basis (computed first at the surface's own (u0, v0) by
   init_surf_const, redone when v moves by 1e-12 or more), FUN_1805b1aa0 the u basis per point; both FUN_1805b06f0 (Cox - de Boor, a
   quotient over a span under 1e-12 or at t within 1e-12 of its knot is 0, a value under 1e-8 set to 0); sum v outer, u inner, / w when
   rational (|w| <= 1e-12: not divided). The same transliteration as mcsrfdl.c sconst_eval (oracle-checked there, tools/tests/srfdl_oracle.sh). */
static void basis_cut(double t, const double *K, int nk, int span, int deg, double *N) {   /* FUN_1805b06f0 */
    int i0 = span - deg; if (i0 < 0) i0 = 0;
    for (int i = i0; i <= span + 1; i++) N[i] = 0.0;
    N[span] = 1.0;
    for (int s = 1; s <= deg; s++) {
        int i = span - s; if (i < 0) i = 0;
        for (; i <= span; i++) {
            if (!(i <= nk - 2 - s)) continue;
            double a = K[i], b = K[i + s], val;
            if (1e-12 > fabs(a - b) || 1e-12 > fabs(t - a)) { val = 0.0; N[i] = 0.0; }
            else { val = ((t - a) / (b - a)) * N[i]; N[i] = val; }
            double cc = K[i + 1], d = K[i + s + 1];
            if (!(1e-12 > fabs(cc - d)) && !(1e-12 > fabs(t - d))) { val = val + ((d - t) * N[i + 1]) / (d - cc); N[i] = val; }
            if (1e-08 > fabs(val)) N[i] = 0.0;
        }
    }
}
typedef struct { NbRec r; double fix, *Nu, *Nv; int vs, ok; } SConst0;
static int sconst0_init(SConst0 *c, const uint8_t *e) {   /* init_surf_const(e, 0): FUN_1805b2280 at (u0, v0) */
    memset(c, 0, sizeof *c);
    if (e[0] != 0 || nb_open(e, &c->r)) return 0;
    c->Nu = calloc((size_t)c->r.nku + 2, sizeof(double)); c->Nv = calloc((size_t)c->r.nkv + 2, sizeof(double));
    if (!c->Nu || !c->Nv) { free(c->Nu); free(c->Nv); return 0; }
    c->fix = D(e, 0x18); c->vs = nurb_span(c->fix, c->r.K + c->r.nku, c->r.nkv, c->r.dv); basis_cut(c->fix, c->r.K + c->r.nku, c->r.nkv, c->vs, c->r.dv, c->Nv);
    return c->ok = 1;
}
static void sconst0_free(SConst0 *c) { free(c->Nu); free(c->Nv); memset(c, 0, sizeof *c); }
static void sconst0_eval(SConst0 *c, double u, double v, double *P) {
    const NbRec *r = &c->r; const double *KU = r->K, *KV = r->K + r->nku;
    if (!(fabs(v - c->fix) < 1e-12)) { c->fix = v; c->vs = nurb_span(v, KV, r->nkv, r->dv); basis_cut(v, KV, r->nkv, c->vs, r->dv, c->Nv); }   /* FUN_1805b2280 */
    int us = nurb_span(u, KU, r->nku, r->du); basis_cut(u, KU, r->nku, us, r->du, c->Nu);                                                   /* FUN_1805b1aa0 */
    double acc[4] = { 0, 0, 0, 0 };
    int i0 = us - r->du; if (i0 < 0) i0 = 0;
    int j0 = c->vs - r->dv; if (j0 < 0) j0 = 0;
    for (int j = j0; j <= c->vs; j++) {
        if (!(j < r->ncv)) continue;
        for (int i = i0; i <= us; i++) {
            if (!(i < r->ncu)) continue;
            double w = c->Nu[i] * c->Nv[j];
            for (int k = 0; k < r->vpp; k++) acc[k] = r->cp[((size_t)r->ncu * j + i) * r->vpp + k] * w + acc[k];
        }
    }
    if (r->nonrat) for (int k = 0; k < r->vpp && k < 3; k++) P[k] = acc[k];
    else if (fabs(acc[0]) <= 1e-12) { for (int k = 1; k < r->vpp; k++) P[k - 1] = acc[k]; }
    else for (int k = 1; k < r->vpp; k++) P[k - 1] = acc[k] / acc[0];
}
/* build_grid(1, surf, 0) 0x180532b30: the points by eval_surf_const after init_surf_const(base, 0) (kind 0 / a trimmed surface's kind 0
   base), else eval_surf */
static int build_grid1(const uint8_t *s) {
    free(g1p); free(g1uv); free(g1hit); g1p = NULL; g1uv = NULL; g1hit = NULL;
    int nu = grid_count(s, 0), nv = grid_count(s, 1);
    g1p = calloc((size_t)nu * nv, sizeof *g1p); g1uv = calloc((size_t)nu * nv, sizeof *g1uv); g1hit = calloc((size_t)(nv - 1) * (nu - 1) + 1, 1);
    if (!g1p || !g1uv || !g1hit) return 0;
    double u1 = D(s, 0x10), u0 = D(s, 8), dv = (D(s, 0x20) - D(s, 0x18)) / (double)(nv - 1);
    const uint8_t *e = s[0] == 10 ? PTR(s, 0x98) + 0x68 : s;
    mm_init(g1mm);
    SConst0 sc; sconst0_init(&sc, e);
    double v = D(s, 0x18); int k = 0;
    for (int j = 0; j < nv; j++) {
        double u = D(s, 8);
        for (int i = 0; i < nu; i++, k++) {
            if (sc.ok) sconst0_eval(&sc, u, v, g1p[k]);
            else { double E[18]; eval_surf(e, 0, u, v, E); memcpy(g1p[k], E, 24); }
            add_to_minmax(g1p[k], g1mm);
            g1uv[k][0] = u; g1uv[k][1] = v;
            u += (u1 - u0) / (double)(nu - 1); if (D(s, 0x10) < u) u = D(s, 0x10);
        }
        v += dv; if (D(s, 0x20) < v) v = D(s, 0x20);
    }
    sconst0_free(&sc);
    memcpy(g1hdr, s, sizeof g1hdr); g1nu = nu; g1nv = nv;
    return nu * nv;
}
/* FUN_180533170(2, curve): the curve's grid - an arc 1 + |sweep| / 15 deg + 1, a spline order x segments + 1, a NURBS (degree + 1)
   x (knots - 1) + 1, a line 2; more when the length per step exceeds curve_min_step x 100 */
static int build_grid2(const Crv *c) {
    free(g2p); free(g2t); g2p = NULL; g2t = NULL;
    int n;
    if (c->cls == 4) n = (int)(short)(int)(fabs(D(c->r, 0xc0)) / 0.26179938779914935 + 2.0);
    else if (c->cls == 8) n = U8(c->r, 0xb4) * I32(c->r, 0xb0) + 1;
    else if (c->cls == 0x80) n = (U8(c->r, 0xd0) + 1) * (I32(c->r, 0xc8) - 1) + 1;
    else n = 2;
    double L = crv_length(c);
    if (curve_min_step * 100.0 < L / (double)(n - 1)) {
        L = L / (curve_min_step * 100.0);
        if (L <= 2147483647.0) n = (int)(0.0 <= L ? floor(L + 0.5) : ceil(L - 0.5)) + 2; else n = 0x7fffffff;
    }
    if (n < 2 || n > 10000000) return 0;
    g2p = calloc((size_t)n, sizeof *g2p); g2t = calloc((size_t)n, sizeof *g2t); if (!g2p || !g2t) return 0;
    double t1, t2; crv_t1t2(c, &t1, &t2); double dt = t2 - t1, t = t1;
    mm_init(g2mm);
    for (int k = 0; k < n; k++) {
        double o[9]; eval_crv(c, 0, t, o); memcpy(g2p[k], o, 24); add_to_minmax(g2p[k], g2mm); g2t[k] = t;
        t += dt / (double)(n - 1); if (t2 < t) t = t2;
    }
    g2n = n; return n;
}
/* FUN_180541d40 / FUN_180541de0 / FUN_180541410: the grid cell of a uv (g1hdr = the surf_type copy: [1] u0 [2] u1 [3] v0 [4] v1) */
static int cell_i(const double *uv) { int s = (short)(int)((uv[0] - g1hdr[1]) / ((g1hdr[2] - g1hdr[1]) / (double)(g1nu - 1))); if (g1nu - 2 < s) return g1nu - 2; return s < 0 ? 0 : s; }
static int cell_j(const double *uv) { int s = (short)(int)((uv[1] - g1hdr[3]) / ((g1hdr[4] - g1hdr[3]) / (double)(g1nv - 1))); if (g1nv - 2 < s) return g1nv - 2; return s < 0 ? 0 : s; }
static int cell_of(const double *uv) { return cell_j(uv) * (g1nu - 1) + cell_i(uv); }
/* FUN_1805513e0: the cells at uv and at uv +- 1e-4 marked hit */
static void mark_hit(const double *uv) {
    if (!g1hit) return;
    int c0 = cell_of(uv); if (c0 < 0) return;
    g1hit[c0] = 1;
    for (int k = 0; k < 4; k++) {
        double q[2] = { uv[0], uv[1] };
        if (k == 0) q[0] = uv[0] + 0.0001; else if (k == 1) q[1] = uv[1] + 0.0001; else if (k == 2) q[0] = uv[0] - 0.0001; else q[1] = uv[1] - 0.0001;
        int c = cell_of(q); if (c != c0 && c >= 0) g1hit[c] = 1;
    }
}
/* FUN_1805391c0: with msf_find_multi, the cells along a uv segment marked hit */
static void mark_segment(const double *a, const double *b) {
    if (!msf_find_multi || !g1hit) return;
    int i1 = cell_i(a), j1 = cell_j(a), i2 = cell_i(b), j2 = cell_j(b);
    int n = abs(j1 - j2) * abs(i1 - i2) * 4 + 0x10;
    double s = 1.0 / (double)n, du = b[0] - a[0], dv = b[1] - a[1], q[2] = { a[0], a[1] };
    for (int k = 1; k < n; k++) { q[0] += du * s; q[1] += dv * s; mark_hit(q); }
    mark_hit(b);
}
/* FUN_1805551c0 + FUN_18053aef0: a triangle's box grown by d */
static void tri_box(const double *p0, const double *p1, const double *p2, double d, double *mm) {
    const double *P[3] = { p0, p1, p2 };
    for (int k = 0; k < 3; k++) { mm[2 * k] = mm[2 * k + 1] = P[0][k]; }
    for (int i = 1; i < 3; i++) for (int k = 0; k < 3; k++) { if (P[i][k] < mm[2 * k]) mm[2 * k] = P[i][k]; if (mm[2 * k + 1] < P[i][k]) mm[2 * k + 1] = P[i][k]; }
    mm[0] -= d; mm[1] += d; mm[2] -= d; mm[3] += d; mm[5] += d; mm[4] -= d;
}

/* ------------------------------------------------------------------------------------------------ the pieces found so far (sp_structure, 0x78 each) */
/* sp_structure fields kept: +8 / +0x20 the uv ends, the CONS (+0x38: body, uv points), +0x48 / +0x50 the base / surface (srf, base
   surf_type), +1 alive, +0x5c / +0x5e the links (-1 free, -3 on a periodic seam: no link), +0x60 / +0x68 their distances, +0x70 / +0x71;
   seq = the order the surface curves were stored in (the database order) */
typedef struct { double uv0[2], uv1[2]; double P0[3], P1[3]; int n; double (*uv)[2]; uint8_t body[0x88]; int srf; const uint8_t *base;
                 int alive, l5c, l5e, seq; double d60, d68; uint8_t f70, f71; } Piece;
static Piece *pc; static int npc, cappc, pc_seq;

/* FUN_180531890 (the start-search acceptance of a later piece) is not decoded: a second search on the same surface is refused */
static int notported;

/* FUN_18053e300 (cs1.c 39): the first point of the curve (from parameter t0) whose projection onto a triangle of grid 1 lies within
   dist: the grid triangles whose box (grown by dist) meets a curve-grid segment's box; the segment's middle parameter projected from
   the triangle's uv centroid (0.33333). check: the cells hit already skipped and the found point checked against the pieces made. */
static int start_search(const uint8_t *s, const Crv *c, double t0, double dist, int check, int npieces, double *uv, double *tout) {
    if (!boxes_overlap(g1mm, g2mm)) return 0;
    const uint8_t *s2 = s[0] == 10 ? PTR(s, 0x98) + 0x68 : s;   /* local_2b90: a trimmed surface's base (the loops checked by FUN_1805353a0 after) */
    int cell = 0, nu = g1nu, idx = 0;
    for (int row = 0; row < g1nv - 1; row++) {
        int tri = 0;
        for (int k = 0; k < nu * 2 - 2; k++) {
            tri = !tri;
            if (!check || !tri || !g1hit[cell]) {
                const double *P0 = g1p[idx], *P1, *P2, *q0 = g1uv[idx], *q1, *q2;
                if (tri) { P1 = g1p[idx + 1]; P2 = g1p[idx + nu]; q1 = g1uv[idx + 1]; q2 = g1uv[idx + nu]; }
                else { P1 = g1p[idx + nu]; P2 = g1p[idx + nu - 1]; q1 = g1uv[idx + nu]; q2 = g1uv[idx + nu - 1]; }
                double box[6]; tri_box(P0, P1, P2, dist, box);
                if (boxes_overlap(box, g2mm)) {
                    double cuv[2] = { ((q0[0] + q1[0]) + q2[0]) * 0.33333, ((q0[1] + q1[1]) + q2[1]) * 0.33333 };
                    for (int sg = 0; sg < g2n - 1; sg++) {
                        double sb[6] = { g2p[sg][0], g2p[sg][0], g2p[sg][1], g2p[sg][1], g2p[sg][2], g2p[sg][2] };
                        add_to_minmax(g2p[sg + 1], sb);
                        if (!boxes_overlap(box, sb)) continue;
                        double t = (g2t[sg + 1] + g2t[sg]) * 0.5; *tout = t;
                        if (!(t0 <= t)) continue;
                        double o[9], S[3]; eval_crv(c, 0, t, o);
                        if (!project_pt(s2, o, cuv, uv, S) || !(dist3(S, o) < dist)) continue;
                        if (check) {   /* FUN_1805344b0: a hit cell, a point near a piece's end, then FUN_180531890 (not decoded) */
                            if (g1hit[cell_of(uv)]) continue;
                            int near = 0; double lim = surf_step_chord_dev * 0.5 * 10.0;
                            for (int q = 0; q < npieces && !near; q++) near = dist3(pc[npc - npieces + q].P0, S) < lim || dist3(pc[npc - npieces + q].P1, S) < lim;
                            if (near) continue;
                            if (npieces) { notported = 1; return 0; }
                        }
                        if (in_face(s, uv)) return 1;
                    }
                }
                if (!tri) { g1hit[cell++] = 1; continue; }
                idx++;
            } else { idx++; k++; tri = 0; g1hit[cell++] = 1; }
        }
        idx++;
    }
    return 0;
}

/* FUN_180534b60: a parametric spline (class 8) steps onto its segment ends when their projection is within lim */
static void spline8_snap(const uint8_t *s, const Crv *c, double lim, int rev, double *S, double *uv, double *t) {
    if (c->cls != 8) return;
    double tt = rev ? floor(*t) : ceil(*t), o[9], q[2], P[3]; char a, b, ok;
    eval_crv(c, 0, tt, o);
    project_core(s, o, uv, q, &a, &b, &ok, P);
    if (ok && dist3(S, P) < lim) { memcpy(S, P, 24); uv[0] = q[0]; uv[1] = q[1]; *t = tt; }
}
/* the curve point at t, extended along the end tangent beyond its range (FUN_18054e8e0's evaluation) */
static void crv_point_ext(const Crv *c, double t, double t1, double t2, double *P) {
    double o[9];
    if (t1 <= t) {
        if (t <= t2) { eval_crv(c, 0, t, o); memcpy(P, o, 24); return; }
        eval_crv(c, 1, t2, o); unit3(o + 3); double d = t - t2;
        P[0] = o[0] + o[3] * d; P[1] = o[1] + o[4] * d; P[2] = o[2] + o[5] * d; return;
    }
    eval_crv(c, 1, t1, o); unit3(o + 3); double d = t - t1;
    P[0] = o[0] + o[3] * d; P[1] = o[1] + o[4] * d; P[2] = o[2] + o[5] * d;
}
/* FUN_180554400 (cs2.c 2144): the curve leaves the face between the previous point (uvp, tp) and the current one: bisection on the
   step (from the previous point) until the projection lands on the boundary (in_polygon 0) or the step interval is below tol / 10 */
static int edge_refine(const uint8_t *s, const Crv *c, int rev, const double *uvp, double tp, double maxd, double *step, double *uv, double *t, double *S) {
    double d5 = 0.0, d1 = *step;
    const double *pts; const int32_t *loops; int nl;
    double rect[10]; int32_t rl[2] = { 0, 3 };
    if (s[0] == 10) { pts = *(const double **)(s + 0xa8); nl = I32(s, 0xc0); loops = (const int32_t *)(pts + 2 * I32(s, 0xa4)); }
    else {
        rect[0] = D(s, 8); rect[1] = D(s, 0x18); rect[2] = D(s, 0x10); rect[3] = D(s, 0x18); rect[4] = D(s, 0x10); rect[5] = D(s, 0x20);
        rect[6] = D(s, 8); rect[7] = D(s, 0x20); rect[8] = D(s, 8); rect[9] = D(s, 0x18); pts = rect; nl = 1; loops = rl;
    }
    int it = 0; double q[2], E[21]; char a, b;
    for (;;) {
        const double *src = it == 0 ? uvp : uv; q[0] = src[0]; q[1] = src[1];
        eval_surf_extrap(s, 2, q[0], q[1], &a, &b, E);
        int bits = range_bits(s, 0, q); snap_edges(&bits, s, q, E);
        int r = in_polygon(nl, pts, loops, q);
        if (r == 0) break;
        it++;
        if (1 < it) {
            double d3, d4; int sv;
            do {
                uv[0] = uvp[0]; uv[1] = uvp[1]; *t = tp;
                d3 = (d5 + d1) * 0.5;
                step_along(c, d3, rev, t);
                double o[9]; eval_crv(c, 0, *t, o);
                int ok = project_pt(s, o, uvp, uv, S);
                d4 = d3;
                if (!ok || maxd < dist3(S, o)) { sv = -1; d3 *= 0.5; }
                else {
                    int bb = range_bits(s, 0, uv);
                    if (snap_edges(&bb, s, uv, S)) { double q2[2] = { uv[0], uv[1] }; project_pt(s, o, q2, uv, S); }
                    sv = in_polygon(nl, pts, loops, uv);
                    if (sv != -1 && (d4 = d1, sv == 1)) d5 = d3;
                }
            } while (sv != 0 && (d1 = d4, tol * 0.1 < fabs(d5 - d4)));
            *step = d3;
            return sv == 0;
        }
    }
    uv[0] = q[0]; uv[1] = q[1];
    if (it == 0) *t = tp;
    memcpy(S, E, 24);
    if (it == 0) *step = 0.0;
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the march (FUN_18054e8e0, cs1.c 1085) */
typedef struct { int n, cap; double (*P)[3], (*UV)[2]; int *cnt; int ncnt, capcnt; } MarchOut;
static int out_append(MarchOut *o, int n, double (*P)[3], double (*UV)[2]) {   /* FUN_1805310a0 / FUN_180531030 + the vector push (FUN_18003c410) */
    if (o->n + n > o->cap) { int c = (o->n + n) * 2; void *a = realloc(o->P, sizeof *o->P * c), *b = realloc(o->UV, sizeof *o->UV * c); if (!a || !b) return 0; o->P = a; o->UV = b; o->cap = c; }
    memcpy(o->P + o->n, P, sizeof *P * n); memcpy(o->UV + o->n, UV, sizeof *UV * n);
    if (o->ncnt == o->capcnt) { int c = o->capcnt ? o->capcnt * 2 : 8; void *a = realloc(o->cnt, sizeof *o->cnt * c); if (!a) return 0; o->cnt = a; o->capcnt = c; }
    o->cnt[o->ncnt++] = n; o->n += n;
    return 1;
}
static void rev_pts(int n, double (*P)[3], double (*UV)[2]) {   /* reverse_real_array */
    for (int i = 0, j = n - 1; i < j; i++, j--) { double a[3], b[2]; memcpy(a, P[i], 24); memcpy(P[i], P[j], 24); memcpy(P[j], a, 24); memcpy(b, UV[i], 16); memcpy(UV[i], UV[j], 16); memcpy(UV[j], b, 16); }
}
/* the arguments as project's call (asm1.txt 1805691c7): s, the curve, the start uv and parameter, mode (param_11 of FUN_180568ca0 = 0),
   seglen = surf_step_chord_dev * 0.5 * 10, maxd = the projection distance; the uv points always kept (param_9 = 1). Pieces go to o,
   trange = the parameter range reached. */
static int march(const uint8_t *s, const Crv *c, const double *uv_start, double t0, int mode, double seglen, double maxd, int p15, MarchOut *o, double *trange) {
    int bVar12 = 0, bVar13 = 1, bVar14, bVar15, bVar16, bVar17;
    int dir = 0 /* local_367 */, reversed = 0 /* local_362 */, iVar28, local_350, local_294 = 0, local_34c = 0, local_364, local_310;
    double dVar9 = surf_step_chord_dev * 0.5, local_268 = dVar9, local_260 = dVar9 * 0.01, dVar5, puVar11 = 0, local_2f8 = 0, local_270 = 0.0, local_218 = 0, local_220 = 0, local_200 = 0;
    double local_258[2] = { 0, 0 }, local_278 = t0, local_308[2] = { uv_start[0], uv_start[1] }, local_2e0[3], P[3], local_1e8[2], local_248;
    double local_338[2], local_348, local_288[2], prev_t, local_228, local_328[3], local_240, local_238, puVar10, local_210, dVar7 = 0;
    int sVar20 = 0, local_360 = 0, ret = 0;
    double (*buf)[3] = NULL, (*ubuf)[2] = NULL;
    o->n = 0; o->ncnt = 0; trange[0] = 1e38; trange[1] = -1e38;
    { double oo[9]; eval_crv(c, 0, t0, oo); memcpy(P, oo, 24); }
    if (!project_pt(s, P, uv_start, local_308, local_2e0) || !(dist3(local_2e0, P) <= maxd)) return 0;
    mark_hit(local_308);
    seam_wrap(0, s, local_308, NULL, local_2e0, NULL);
    local_360 = range_bits(s, 0, local_308);
    if (snap_edges(&local_360, s, local_308, local_2e0)) local_34c = 1;
    if (local_360 != 0) return 0;
    if (s[0] == 10 && trim_in(s, local_308) == -1) return 0;
    buf = calloc((size_t)max_spline_pts + 1, sizeof *buf); ubuf = calloc((size_t)max_spline_pts + 1, sizeof *ubuf);
    if (!buf || !ubuf) { free(buf); free(ubuf); return 0; }
    memcpy(buf[0], local_2e0, 24); memcpy(ubuf[0], local_308, 16);
    iVar28 = 1; local_350 = 1;
    if (t0 < trange[0]) trange[0] = t0;
    if (trange[1] <= t0 && t0 != trange[1]) trange[1] = t0;
    memcpy(local_1e8, local_308, 16); local_248 = t0;
    mark_hit(local_308);
    memcpy(local_338, local_308, 16); local_348 = t0;
    crv_t1t2(c, &local_240, &local_238);
    dVar5 = dVar9 * 0.01;
    bVar15 = 0;
L_ec9c:
    memcpy(local_288, local_338, 16); prev_t = local_348; local_228 = local_348;
    local_310 = 0; local_364 = 0; bVar16 = 0; bVar17 = 0; bVar14 = bVar12;
L_ecd6:
    local_2f8 = surf_step_dist;
    if (surf_use_chord_dev) local_2f8 = iVar28 == 1 ? dVar9 * 10.0 : puVar11 * 1.1;
    local_210 = dVar5 * 2.0; puVar10 = local_210; bVar12 = bVar14;
    for (;;) {
        for (;;) {
            bVar14 = bVar15;
            puVar11 = local_2f8;
            step_along(c, local_2f8, dir, &local_348);
            crv_point_ext(c, local_348, local_240, local_238, P);
            char inu, inv, ok;
            project_core(s, P, local_288, local_338, &inu, &inv, &ok, local_328);
            spline8_snap(s, c, puVar10, dir, local_328, local_338, &local_348);
            if (ok && dist3(local_328, P) <= maxd) break;
L_f117:
            local_2f8 = puVar11 * 0.5; puVar11 = local_2f8;
            if (local_2f8 <= dVar5) goto L_fdcc;
            memcpy(local_338, local_288, 16); local_348 = prev_t; bVar15 = bVar14;
        }
        double *last = buf[iVar28 - 1];
        if (dist3(local_328, last) < tol) goto L_fdcc;
        if (surf_use_chord_dev && dVar9 * 2.0 < puVar11) {
            double dev = 0.0;
            if (!(c->cls == 1 || c->cls == 2)) {
                double A[9], M[9], B[9]; eval_crv(c, 0, prev_t, A); eval_crv(c, 0, (prev_t + local_348) * 0.5, M); eval_crv(c, 0, local_348, B);
                double l[6] = { A[0], A[1], A[2], B[0], B[1], B[2] }; dev = dist_pt_l_3d(M, l);
            }
            if ((surf_step_chord_dev <= dev && dev != surf_step_chord_dev) || !chord_on_surf(s, local_288, local_338, last, local_328, dVar9)) goto L_f117;
        }
        sVar20 = seam_wrap(1, s, local_338, local_288, local_328, last);
        if (dist3(local_328, last) < tol) goto L_fdcc;
        local_360 = range_bits(s, 0, local_338);
        if (!(local_34c & 1) && snap_edges(&local_360, s, local_338, local_328) && dist3(local_328, last) < tol) goto L_fdcc;
        puVar10 = puVar11;
        bVar12 = 0;
        if (!(local_364 & 1) && (sVar20 != 0 || local_360 != 0 || (s[0] == 10 && (bVar12 = 0, trim_in(s, local_338) == -1)))) {
            local_34c |= 1;
            edge_refine(s, c, dir, local_288, prev_t, maxd, &local_2f8, local_338, &local_348, local_328);
            if (sVar20 == 0) local_364 |= 1; else seam_side(1, sVar20, local_338, s);
            puVar10 = local_2f8; bVar12 = 1;
            if (fabs(local_2f8) <= tol && tol != fabs(local_2f8)) {
                memcpy(buf[iVar28 - 1], local_328, 24); memcpy(ubuf[iVar28 - 1], local_338, 16);
                goto L_f94e;
            }
        }
        if ((fabs(local_348 - local_240) <= 1e-06 && fabs(local_348 - local_240) != 1e-06) || (fabs(local_348 - local_238) <= 1e-06 && fabs(local_348 - local_238) != 1e-06)) local_364 |= 2;
        dVar7 = dist3(local_328, last);
        double dd;   /* uVar31 / uVar33: the step's length from here on */
        if (dVar7 < tol) {
L_fd5a:
            bVar12 = bVar14;
            if (bVar16) { memcpy(buf[iVar28 - 1], local_328, 24); memcpy(ubuf[iVar28 - 1], local_338, 16); }
            goto L_fdcc;
        }
        dd = dVar7;
        {
            double d9 = dist3(local_328, local_2e0);
            if (tol <= d9) {
                double d5 = dist3(last, local_2e0);
                if ((3 < iVar28 || !bVar13) && d9 < dVar7 && d5 < dVar7) {
                    double l[6] = { last[0], last[1], last[2], local_328[0], local_328[1], local_328[2] };
                    double dl = dist_pt_l_3d(local_2e0, l), tl = seg_param(local_2e0, l);
                    if (0.0 <= tl && tl <= 1.0 && dl < local_268 * 10.0) {   /* the start point between: the curve closed */
                        memcpy(local_328, local_2e0, 24);
                        if (!bVar12) memcpy(local_338, local_308, 16);
                        local_348 = local_278; bVar14 = 1; goto L_f578;
                    }
                }
                if (bVar14) goto L_f578;
            } else {
                bVar14 = 1;
L_f578:
                dd = dist3(local_328, last);
                if (dd < tol * 5.0) goto L_fd5a;
            }
        }
        if (bVar16) local_270 += dd;
        if (iVar28 < 2) goto L_f7fb;
        {
            double a[3] = { buf[iVar28 - 2][0] - last[0], buf[iVar28 - 2][1] - last[1], buf[iVar28 - 2][2] - last[2] }, b[3] = { local_328[0] - last[0], local_328[1] - last[1], local_328[2] - last[2] };
            unit3(a); unit3(b);
            if (b[0] * a[0] + b[1] * a[1] + b[2] * a[2] <= 0.93969) goto L_f7fb;
        }
        bVar12 = 0; bVar14 = 0;   /* turned back on itself: half the step from the previous point */
        local_2f8 = puVar10 * 0.5; puVar11 = local_2f8;
        if (local_2f8 <= local_260) goto L_fdcc;
        memcpy(local_338, local_288, 16); local_348 = local_228; prev_t = local_228;
        puVar10 = local_210; bVar15 = bVar14; dVar9 = local_268; dVar5 = local_260;
        continue;
L_f7fb:
        if (bVar16 && local_364 == local_310 && seglen < local_270) {
            local_350 = local_294; iVar28 = local_294; puVar11 = local_218; bVar12 = bVar14; goto L_fdcc;
        }
        if (iVar28 == 1) local_220 = puVar10;
        memcpy(buf[iVar28], local_328, 24); memcpy(ubuf[iVar28], local_338, 16);
        iVar28++;
        { double q[2] = { local_338[0], local_338[1] }, S2[3] = { local_328[0], local_328[1], local_328[2] };
          local_350 = iVar28;
          if (snap_edges(&local_360, s, q, S2)) local_34c |= 1; else local_34c &= ~1; }
        if (local_348 < trange[0]) trange[0] = local_348;
        if (trange[1] <= local_348 && local_348 != trange[1]) trange[1] = local_348;
        memcpy(local_1e8, local_338, 16); local_248 = local_348;
        mark_segment(local_288, local_338);
        dVar5 = local_260; dVar9 = local_268;
L_f94e:
        if (sVar20 != 0) { seam_side(0, sVar20, local_338, s); mark_hit(local_338); }
        memcpy(local_288, local_338, 16); prev_t = local_348; local_228 = local_348;
        puVar10 = puVar11;
        if (bVar14 || local_364 == 3) goto L_fa4d;
        if (bVar16) goto L_fa5d;
        if (local_364 == 0 || mode == 1) goto L_f9d7;
        if (mode == 3) { if ((local_34c & 1) && !(local_34c & 2)) goto L_f9d7; }
        else if (mode == 2 && !(local_364 & 1)) {
            bVar17 = 0; bVar16 = 1; local_310 = local_364; local_270 = 0.0; memcpy(local_258, local_338, 16); local_200 = local_348;
            puVar10 = seglen * 0.3; local_294 = iVar28; local_218 = puVar11; goto L_fa5d;
        }
L_fa4d:
        bVar17 = 1;
        if (bVar13) goto L_fa5d;
        goto L_piece;
L_f9d7:
        bVar17 = 0;
L_fa5d:
        puVar11 = puVar10;
        if (sVar20 == 0 && iVar28 < max_spline_pts) goto L_fd4a;
L_piece:
        {   /* the piece ends here */
            int sVar21 = 0, iVar29 = 0;
            if (sVar20 != 0) {
                while (!bVar17) {
                    if (o->ncnt <= sVar21) break;
                    double lim = surf_step_chord_dev * 0.5 * 10.0;
                    if (dist3(o->P[iVar29], buf[iVar28 - 1]) < lim) bVar17 = 1;
                    else if (dist3(o->P[o->cnt[sVar21] - 1 + iVar29], buf[iVar28 - 1]) < lim) bVar17 = 1;
                    iVar29 += o->cnt[sVar21]; sVar21++; iVar28 = local_350;
                }
            }
            if (1 < iVar28) {
                if (reversed) { rev_pts(iVar28, buf, ubuf); reversed = 0; }
                if (!out_append(o, iVar28, buf, ubuf)) goto done;
            }
            memcpy(buf[0], local_328, 24); memcpy(ubuf[0], local_338, 16);
            if (1 < iVar28 || sVar20 != 0) bVar13 = 0;
            iVar28 = 1; local_350 = 1;
        }
L_fd4a:
        bVar12 = bVar14; bVar15 = bVar14;
        if (bVar17) goto L_fdcc;
        goto L_ecd6;
    }
L_fdcc:
    if (dir != 0 || bVar12) goto L_ff60;
    memcpy(local_338, local_308, 16); local_348 = local_278;   /* back to the start, the other way */
    if (iVar28 != 1) {
        if (bVar13) {
            rev_pts(iVar28, buf, ubuf);
            memcpy(local_2e0, buf[1], 24);
            puVar11 = local_220;
            if (bVar16) { memcpy(local_308, local_258, 16); local_278 = local_200; }
            else { memcpy(local_308, local_1e8, 16); local_278 = local_248; }
            goto L_ff32;
        }
        if (1 < iVar28 && !out_append(o, iVar28, buf, ubuf)) goto done;
    }
    bVar13 = 1;
    memcpy(buf[0], local_2e0, 24); memcpy(ubuf[0], local_308, 16);
    iVar28 = 1; local_350 = 1;
L_ff32:
    reversed = 1; dir = 1; dVar9 = local_268; dVar5 = local_260; bVar15 = bVar14;
    goto L_ec9c;
L_ff60:
    if (1 < iVar28) {
        if (reversed != p15) rev_pts(iVar28, buf, ubuf);
        out_append(o, iVar28, buf, ubuf);
    }
    ret = 1;
done:
    free(buf); free(ubuf);
    return ret;
}

/* ------------------------------------------------------------------------------------------------ surface curves */
/* the uv curve (order 2 through the uv points, array_2d_to_nurbs 0x18023d330: knots 0, 0, 1, .., n - 1, n - 1) at t, with its
   derivatives (degree 1: the second is 0) */
static void uvcrv_eval(int n, const double (*uv)[2], double t, double *o) {
    memset(o, 0, 9 * sizeof(double));
    if (n < 2) { if (n == 1) { o[0] = uv[0][0]; o[1] = uv[0][1]; } return; }
    int i = (int)floor(t); if (i < 0) i = 0; if (i > n - 2) i = n - 2;
    double f = t - i;
    o[0] = uv[i][0] + (uv[i + 1][0] - uv[i][0]) * f; o[1] = uv[i][1] + (uv[i + 1][1] - uv[i][1]) * f;
    o[3] = uv[i + 1][0] - uv[i][0]; o[4] = uv[i + 1][1] - uv[i][1];
}
/* FUN_1802db430 / FUN_1803ec480: the surface curve at t (order <= 2): the uv curve, then the surface; the second derivative as the
   DLL builds it (du (Suu du + Suv dv) + dv (Suv du + Svv dv) + u'' (Su + Sv)) */
static int cons_eval(const uint8_t *s, int n, const double (*uv)[2], int order, double t, double *o) {
    if (order > 1) order = 2;
    double c[9], E[18]; uvcrv_eval(n, uv, t, c);
    memset(E, 0, sizeof E);
    int r = eval_surf(s, order, c[0], c[1], E);
    memcpy(o, E, 24);
    if (order > 0) for (int q = 0; q < 3; q++) o[3 + q] = E[3 + q] * c[3] + E[6 + q] * c[4];
    if (order > 1) for (int q = 0; q < 3; q++) {
        double a = E[9 + q] * c[3] + E[12 + q] * c[4];
        o[6 + q] = c[3] * a + (E[12 + q] * c[3] + E[15 + q] * c[4]) * c[4];
        o[6 + q] = c[6] * (E[3 + q] + E[6 + q]) + o[6 + q];
    }
    return r;
}
/* FUN_18054d4f0 (cs2.c 3759): the uv points of a piece thinned (join_keep_first static 0): leading points outside the range (after
   FUN_18053a9f0's snapping) skipped; a point dropped while the surface point is within 0.9 surf_step_chord_dev of the 3D chord and
   the uv point within 1e-8 of the uv chord; points still outside the range left out. msf_join_em (static 0) closure join not run. */
static int thin_piece(const uint8_t *s, int n, double (*uv)[2]) {
    if (n < 2) return n;
    if (n <= 2) return n;
    int k = 0; double E[18], A[3], B[3], Auv[2], Buv[2];
    int bits = range_bits(s, 0, uv[k]);
    while (bits != 0) { eval_surf(s, 0, uv[k][0], uv[k][1], E); snap_edges(&bits, s, uv[k], E); if (bits == 0) break; k++; if (k >= n) return n; bits = range_bits(s, 0, uv[k]); }
    memcpy(Auv, uv[k], 16); eval_surf(s, 0, Auv[0], Auv[1], E); memcpy(A, E, 24);
    k++; if (k >= n) return n;
    bits = range_bits(s, 0, uv[k]);
    while (bits != 0) { eval_surf(s, 0, uv[k][0], uv[k][1], E); snap_edges(&bits, s, uv[k], E); if (bits == 0) break; k++; if (k >= n) return n; bits = range_bits(s, 0, uv[k]); }
    memcpy(Buv, uv[k], 16); eval_surf(s, 0, Buv[0], Buv[1], E); memcpy(B, E, 24);
    int m = 2, out = 2;
    while (k < n - 1) {
        k++;
        double Cuv[2] = { uv[k][0], uv[k][1] }, C[3];
        bits = range_bits(s, 0, Cuv);
        eval_surf(s, 0, Cuv[0], Cuv[1], E); memcpy(C, E, 24);
        if (bits == 0 || (snap_edges(&bits, s, Cuv, C), bits == 0)) {
            double l[6] = { A[0], A[1], A[2], C[0], C[1], C[2] }, dev = dist_pt_l_3d(B, l);
            if (dev < surf_step_chord_dev * 0.9) { double l2[4] = { Auv[0], Auv[1], Cuv[0], Cuv[1] }; if (!(dist_pt_l(Buv, l2) <= 1e-08)) dev = 1e38; }
            if (surf_step_chord_dev * 0.9 <= dev) { uv[m][0] = Cuv[0]; uv[m][1] = Cuv[1]; m++; memcpy(A, B, 24); memcpy(Auv, Buv, 16); }
            else { uv[m - 1][0] = Cuv[0]; uv[m - 1][1] = Cuv[1]; }
            memcpy(B, C, 24); memcpy(Buv, Cuv, 16);
        }
        out = m;
    }
    return out;
}
/* put_cons_in_db 0x1802da530 (cs2.c 171): the CONS body (cons_type): +0 / +0x18 the surface points at t1 / t2 of the uv curve, +0x30
   / +0x38 t1 / t2 (0, n - 1), +0x40 the largest |C' x C''| / |C'| sampled from t1 in steps of sqrt(t2 - t1) / pi (DAT_1812d9064 static
   1), +0x48 / +0x50 / +0x58 / +0x60 the uv curve's end points (u, v), +0x68 surface / +0x70 uv curve references (filled by the
   caller's records), +0x78 0, +0x80 closed (ends within tol), +0x81 1 (FUN_18054d4f0 local_28ef) */
static void cons_body(const uint8_t *s, int n, const double (*uv)[2], uint8_t *b) {
    memset(b, 0, 0x88);
    double t1 = 0.0, t2 = (double)(n - 1), o[9];
    D(b, 0x30) = t1; D(b, 0x38) = t2;
    D(b, 0x48) = uv[0][0]; D(b, 0x50) = uv[0][1]; D(b, 0x58) = uv[n - 1][0]; D(b, 0x60) = uv[n - 1][1];
    cons_eval(s, n, uv, 0, t1, o); memcpy(b, o, 24);
    cons_eval(s, n, uv, 0, t2, o); memcpy(b + 0x18, o, 24);
    D(b, 0x40) = 0.0;
    double st = sqrt(t2 - t1), t = t1;
    if (t1 <= t2) do {
        cons_eval(s, n, uv, 2, t, o);
        double d2 = o[3] * o[3] + o[4] * o[4] + o[5] * o[5];
        if (1e-12 < d2) {
            double z = o[7] * o[3] - o[6] * o[4], y = o[6] * o[5] - o[8] * o[3], x = o[8] * o[4] - o[7] * o[5];
            double k = sqrt((x * x + y * y + z * z) / d2);
            if (D(b, 0x40) <= k && k != D(b, 0x40)) D(b, 0x40) = k;
        }
        t += st / 3.141592653589793;
    } while (t <= t2);
    b[0x80] = dist3((const double *)b, (const double *)(b + 0x18)) < tol;
    b[0x81] = 1;
}

/* ------------------------------------------------------------------------------------------------ FUN_180568ca0 (cs1.c 5586) */
static int add_piece(int srf, int n, double (*uv)[2], const uint8_t *s) {
    if (npc == cappc) { int c = cappc ? cappc * 2 : 8; Piece *p = realloc(pc, sizeof *pc * c); if (!p) return 0; pc = p; cappc = c; }
    Piece *p = &pc[npc]; memset(p, 0, sizeof *p);
    p->uv = malloc(sizeof *p->uv * (n > 0 ? n : 1)); if (!p->uv) return 0;
    memcpy(p->uv, uv, sizeof *uv * n); p->n = n; p->srf = srf;
    cons_body(s, n, (const double (*)[2])p->uv, p->body);
    memcpy(p->uv0, uv[0], 16); memcpy(p->uv1, uv[n - 1], 16); memcpy(p->P0, p->body, 24); memcpy(p->P1, p->body + 0x18, 24);
    p->base = s[0] == 10 ? PTR(s, 0x98) + 0x68 : s; p->alive = 1; p->seq = ++pc_seq; p->d60 = p->d68 = 0;
    /* FUN_1805636d0 (asm1.txt 180563ced-180563d7e): an end on a periodic direction's boundary of the base (+6 bit 0 u, bit 1 v; within
       1e-8 of u0 / u1, v0 / v1) gets -3 (no link), else -1 */
    for (int e = 0; e < 2; e++) {
        const double *q = e ? p->uv1 : p->uv0; const uint8_t *b = p->base; int fl = U8(b, 6), v = -1;
        if ((fl & 1) && (fabs(q[0] - D(b, 8)) < 1e-08 || fabs(q[0] - D(b, 0x10)) < 1e-08)) v = -3;
        else if ((fl & 2) && (fabs(q[1] - D(b, 0x18)) < 1e-08 || fabs(q[1] - D(b, 0x20)) < 1e-08)) v = -3;
        if (e) p->l5e = v; else p->l5c = v;
    }
    npc++;
    return 1;
}
/* one curve on one surface (s: the surface's image surf_type; srf its index) */
static int project_on_surf(const uint8_t *s, int srf, const Crv *c, int p15) {
    int first = 1, had_prev = 0, any = 0, start_pc = npc;
    double prev_uv[2] = { 0, 0 }, t1, t2;
    int save_multi = msf_find_multi; msf_find_multi = 0;   /* param_7 = 0 */
    memset(g1hit, 0, (size_t)(g1nv - 1) * (g1nu - 1));   /* clear_hits */
    crv_t1t2(c, &t1, &t2);
    double t = t1;
    for (;;) {
        double uv[2], tt;
        int found = start_search(s, c, t, proj_dist, !first, npc - start_pc, uv, &tt);
        if (notported) break;
        if (had_prev && sqrt((uv[0] - prev_uv[0]) * (uv[0] - prev_uv[0]) + (uv[1] - prev_uv[1]) * (uv[1] - prev_uv[1])) < 1e-08) found = 0;
        had_prev = 1; memcpy(prev_uv, uv, 16);
        if (!found) break;
        first = 0;
        mark_hit(uv);
        MarchOut o = { 0 }; double tr[2];
        march(s, c, uv, tt, 0, surf_step_chord_dev * 0.5 * 10.0, proj_dist, p15, &o, tr);
        int stored = 0;
        if (o.n > 1) {   /* FUN_1805636d0 (cs1.c 2104), param_10 = 0: each piece of more than one point thinned and stored on surface 1 */
            int off = 0;
            for (int q = 0; q < o.ncnt; q++) {
                int cnt = o.cnt[q];
                if (1 < cnt) {
                    int m = thin_piece(s, cnt, o.UV + off);
                    if (1 < m && add_piece(srf, m, o.UV + off, s)) stored = 1;
                }
                off += cnt;
            }
        }
        free(o.P); free(o.UV); free(o.cnt);
        if (stored) any = 1;
        t = tr[1];
        if (stored) break;   /* param_7 (msf_find_multi) 0: one piece run */
    }
    msf_find_multi = save_multi;
    return any;
}


/* ------------------------------------------------------------------------------------------------ views (the pieces' order) */
static const CsViews *views;
static int view_np;                      /* 1: no view found or made for the first piece end (NOT DECODED) */
/* calc_surf_norm (mccore 0x1801637e0, decomp/partline/ev_dec.c l.3-260), param_7 0: N = Su x Sv (N0 = Sv2 Su1 - Sv1 Su2, N1 = Sv0 Su2 -
   Su0 Sv2, N2 = Su0 Sv1 - Sv0 Su1); the limit = min(metric ? 1e-4 : 4e-6, (surf +0x70)^2). |N| above it: N, 0. Else (a pole or a
   degenerate edge): with |Su| and |Sv| both at least the limit and their angle's |cos| below cos(0.08725), N as it is, 0; else the
   direction to step: u when |Su| < limit, or both long and |Suu| > |Svv| (eval_surf order 2), else v; dv = the range / 100, negated
   unless the parameter is below the range's end - 50 dv; then up to 50 steps (the parameter moved by dv each time, h = step x dv):
   eval_surf order 3 there, N = Su x Sv + (a + b) h (u: a = Su x Suv, b = Suu x Sv; v: a = Su x Svv, b = Suv x Sv); still within the
   limit: + (2 b' + a' + c') h^2 / 2 (u: a' = Su x Suuv, b' = Suu x Suv, c' = Suuu x Sv; v: a' = Su x Svvv, b' = Suv x Svv, c' =
   Suvv x Sv); above the limit: N, 0; after 50 steps still within it: N = 0, 1. (The same steps as viewer/mcsurf_nrm.c mcsn_norm.) */
static void cross_(const double *a, const double *b, double *n) { n[0] = a[1] * b[2] - a[2] * b[1]; n[1] = a[2] * b[0] - a[0] * b[2]; n[2] = a[0] * b[1] - a[1] * b[0]; }
static double len_(const double *a) { return sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
static int surf_norm(const uint8_t *s, const double *Su, const double *Sv, double u, double v, double *N) {
    double lim = is_metric ? 1e-4 : 4e-6, sz = D(s, 0x70) * D(s, 0x70);
    if (sz <= lim && lim != sz) lim = sz;
    N[0] = Sv[2] * Su[1] - Sv[1] * Su[2]; N[1] = Sv[0] * Su[2] - Su[0] * Sv[2]; N[2] = Su[0] * Sv[1] - Sv[0] * Su[1];
    if (lim < len_(N)) return 0;
    int bu = 0, bv = 0, fwd; double dv, E[30];
    double lsu = len_(Su);
    if (lim <= lsu) {
        double lsv = len_(Sv);
        if (lim <= lsv) {
            double c = (Sv[0] * Su[0] + Sv[1] * Su[1] + Sv[2] * Su[2]) / (lsv * lsu), c0 = cos(0.08725);
            if (fabs(c) <= c0 && c0 != fabs(c)) return 0;
            eval_surf(s, 2, u, v, E);
            if (len_(E + 9) <= len_(E + 15)) goto v_dir;
        }
        bu = 1; dv = (D(s, 0x10) - D(s, 8)) / 100.0; fwd = u < D(s, 0x10) - dv * 50.0;
    } else {
v_dir:  dv = (D(s, 0x20) - D(s, 0x18)) / 100.0; fwd = v < D(s, 0x20) - dv * 50.0; bv = 1;
    }
    if (!fwd) dv = -dv;
    for (int st = 1;; st++) {
        if (bu) u += dv; else if (bv) v += dv;
        double h = (double)st * dv, a[3], b[3], c[3];
        eval_surf(s, 3, u, v, E);
        const double *eu = E + 3, *ev = E + 6, *euu = E + 9, *euv = E + 12, *evv = E + 15;
        N[0] = ev[2] * eu[1] - ev[1] * eu[2]; N[1] = ev[0] * eu[2] - eu[0] * ev[2]; N[2] = eu[0] * ev[1] - ev[0] * eu[1];
        if (bu) { cross_(eu, euv, a); cross_(euu, ev, b); } else { cross_(eu, evv, a); cross_(euv, ev, b); }
        for (int k = 0; k < 3; k++) N[k] = (b[k] + a[k]) * h + N[k];
        if (!(len_(N) <= lim)) return 0;
        if (bu) { cross_(eu, E + 21, a); cross_(euu, euv, b); cross_(E + 18, ev, c); } else { cross_(eu, E + 27, a); cross_(euv, evv, b); cross_(E + 24, ev, c); }
        double h2 = h * 0.5 * h;
        for (int k = 0; k < 3; k++) N[k] = (b[k] * 2.0 + a[k] + c[k]) * h2 + N[k];
        if (lim < len_(N)) return 0;
        if (50 < st + 1) { N[0] = N[1] = N[2] = 0; return 1; }
    }
}
/* calc_surf_unit_norm (mccore 0x180164290, decomp/curves/core1.c 4390): calc_surf_norm, failed: N = 0 (its code returned); else N / |N| when
   |N| > 1e-12, else N = 0, 1 */
static int surf_unit_norm(const uint8_t *s, const double *Su, const double *Sv, double u, double v, double *N) {
    int e = surf_norm(s, Su, Sv, u, v, N);
    if (e) { N[0] = N[1] = N[2] = 0; return e; }
    double l = len_(N);
    if (1e-12 < fabs(l)) { N[0] /= l; N[1] /= l; N[2] /= l; return 0; }
    N[0] = N[1] = N[2] = 0; return 1;
}
/* make_view_k (mccore 0x180017080, decomp/mccore_make_view_k.c): k = n / |n| (0 when |n| = 0), |k| < 0.5 fails; idx = the smaller |k_i| of
   i = 0, 1 only (the loop's sVar5 < 2; equal keeps 0); e = v_z_3d with e[idx] = 1; y = unit(k x e) written as (e2 k1 - e1 k2, e0 k2 - e2 k0,
   e1 k0 - e0 k1); x = unit(y x k); rows x, y, k. v_z_3d (mccore ?v_z_3d@@3Vp_3di@@A at 0x181371ca8): past the end of .data's file
   bytes (raw size 0xc6c00 from 0x1812ab000: zero filled at load), and no instruction of mccore's .text stores to it (the 20 lea of its
   address pass it as a const p_3d & argument; the mastercam.dll uses of its import read it): (0, 0, 0) */
static int make_view_k(const double *n, double *m) {
    double k0 = n[0], k1 = n[1], k2 = n[2], d = sqrt(k0 * k0 + k1 * k1 + k2 * k2);
    if (d == 0.0) { k0 = 0; k1 = 0; k2 = 0; } else { k0 /= d; k1 /= d; k2 /= d; }
    if (!(0.5 <= sqrt(k1 * k1 + k0 * k0 + k2 * k2))) return 0;
    double k[3] = { k0, k1, k2 }, best = 0x1.2ced32a16a1b1p+126;   /* the loop's seed 0x47d2ced32a16a1b1 */
    int idx = 0;
    for (int i = 0; i < 2; i++) if (i == 0 || (fabs(k[i]) <= best && best != fabs(k[i]))) { best = fabs(k[i]); idx = i; }
    double e[3] = { 0, 0, 0 }; e[idx] = 1.0;
    double y0 = e[2] * k1 - e[1] * k2, y2 = e[1] * k0 - e[0] * k1, y1 = e[0] * k2 - e[2] * k0;
    d = sqrt(y1 * y1 + y0 * y0 + y2 * y2);
    if (d == 0.0) { y0 = 0; y1 = 0; y2 = 0; } else { y0 /= d; y1 /= d; y2 /= d; }
    double x0 = y1 * k2 - y2 * k1, x1 = y2 * k0 - y0 * k2, x2 = y0 * k1 - y1 * k0;
    d = sqrt(x0 * x0 + x1 * x1 + x2 * x2);
    if (d == 0.0) { x0 = 0; x1 = 0; x2 = 0; } else { x0 /= d; x1 /= d; x2 /= d; }
    m[0] = x0; m[1] = x1; m[2] = x2; m[3] = y0; m[4] = y1; m[5] = y2; m[6] = k0; m[7] = k1; m[8] = k2;
    return 1;
}
/* find_view_k (mastercam.dll 0x180337960, decomp/mastercam_findviewk.c l.3-66) with create 1: views 1 .. n_views in order, d = k_v . n
   (k_v the view's z row, n as given), e = | |d| - 1 |: e <= 1e-12 that view at once; else the one of least e below 2e-7 (the first of
   equals); none: make_view_k(n) and AddMCView(m, 2e-7). Returns 0 when nothing is found or made (*view untouched, as the DLL) */
static int find_view_k(const double *n, int *view) {
    int nv = views->count(views->ctx), bv = -1; double best = 0.0;
    for (int s = 1; s <= nv; s++) {
        double m[9]; if (!views->get(views->ctx, s, m)) continue;   /* (the table here is by number; the DLL's is contiguous) */
        double d = m[7] * n[1] + n[0] * m[6] + m[8] * n[2], e = fabs(fabs(d) - 1.0);
        if (e <= 1e-12) { *view = s; return 1; }
        if (e < 2e-07 && (bv < 0 || e < fabs(fabs(best) - 1.0))) { bv = s; best = d; }
    }
    if (bv > -1) { *view = bv; return 1; }
    double m[9];
    if (!make_view_k(n, m)) return 0;
    int v = views->add(views->ctx, m);
    if (v <= 0) return 0;
    *view = v; return 1;
}
/* world_to_view (mastercam.dll 0x180341b30, decomp/mastercam_selpre.c): view < 2 (or past the table): a copy; else o_i = m_i0 w0 + m_i1 w1 + m_i2 w2 */
static void world_to_view(const double *w, int view, double *o) {
    double m[9], t[3];
    if (view < 2 || !views->get(views->ctx, view, m)) { t[0] = w[0]; t[1] = w[1]; t[2] = w[2]; }
    else for (int i = 0; i < 3; i++) t[i] = m[3 * i] * w[0] + m[3 * i + 1] * w[1] + m[3 * i + 2] * w[2];
    o[0] = t[0]; o[1] = t[1]; o[2] = t[2];
}

/* ------------------------------------------------------------------------------------------------ the pieces' order, links and joins */
/* project_chains_onto_surfs LAB_180569df8 (decomp/curves/core1.c 1646-1800, msf_proj_cview 0): of the chain's pieces, the one whose end
   is nearest the chain's start (the 2D distance in the view of the surface normal there: calc_surf_unit_norm, find_view_k(n, .., create 1),
   world_to_view of the surface point and of the chain's start, l.1726-1740 - and the 3D distance, within proj_dist, the end's direction (the
   piece's tangent, reversed at its end) more than 0.25 along the chain's start tangent) goes first (FUN_180554d40 swaps the two sp
   entries); a tie by the tangents */
static void swap_pieces(int a, int b) { Piece t = pc[a]; pc[a] = pc[b]; pc[b] = t; }
static void order_pieces(const Crv *c) {
    if (npc < 1) return;
    double t1, t2, o[9], S0[3], T0[3]; crv_t1t2(c, &t1, &t2);
    memcpy(S0, c->r + 0x68, 24);   /* the chain entity's stored start (local_7e90 = ent +0x68) and tan_on_ent_from_t(min_t_on_ent) */
    eval_crv(c, 1, t1, o); memcpy(T0, o + 3, 24); unit3(T0);
    /* the decompile's variables: bVar6 a tie, bVar7 a candidate, sVar21 the best piece, local_80c0 its end, local_80b0 the best before it,
       local_80b2 / local_80a8 the tie's piece / end, best2 / best3 its 2D / 3D distances (uVar33 / uVar35), prev3 the piece's first end's 3D
       distance (uVar29) */
    int bVar6 = 0, bVar7 = 0, sVar21 = 0, local_80c0 = 0, local_80b0 = 0, local_80b2 = 0, local_80a8 = 0;
    double best2 = 0, best3 = 0, prev3 = 0, d2 = 0;
    int cview = 0, have_view = 0;   /* local_80ac[0] */
    for (int k = 0; k < npc; k++) {
        const uint8_t *bs = pc[k].base;
        for (int e = 0; e < 2; e++) {
            const double *uv = e == 0 ? pc[k].uv0 : pc[k].uv1; double E[18], tan[9], N[3];
            eval_surf(bs, 1, uv[0], uv[1], E);
            cons_eval(bs, pc[k].n, (const double (*)[2])pc[k].uv, 1, e == 0 ? 0.0 : (double)(pc[k].n - 1), tan);
            double tv[3] = { tan[3], tan[4], tan[5] }; unit3(tv); if (e) { tv[0] = -tv[0]; tv[1] = -tv[1]; tv[2] = -tv[2]; }
            /* l.1726-1740: calc_surf_unit_norm(surf, Su, Sv, u, v, n, 0); find_view_k(n, &view, 1, &found) (view keeps its last value when
               nothing is found: its first value is uninitialised in the DLL - NOT DECODED, refused); the surface point and the chain's start
               world_to_view'ed into it; the 2D distance of their x, y */
            surf_unit_norm(bs, E + 3, E + 6, uv[0], uv[1], N);   /* its return is not read: a failed normal is 0, which find_view_k cannot match or make */
            if (!find_view_k(N, &cview) && !have_view) { view_np = 1; return; }
            have_view = 1;
            double Pv[3], Sv[3]; world_to_view(E, cview, Pv); world_to_view(S0, cview, Sv);
            d2 = sqrt((Sv[0] - Pv[0]) * (Sv[0] - Pv[0]) + (Sv[1] - Pv[1]) * (Sv[1] - Pv[1]));
            double d[3] = { S0[0] - E[0], S0[1] - E[1], S0[2] - E[2] }, d3 = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            int sVar11 = sVar21;
            if (e == 0 || fabs(tol) < fabs(d3 - prev3) || fabs(tol) == fabs(d3 - prev3)) {
                if (bVar7) {
                    if (d3 <= proj_dist && 0.25 < tv[1] * T0[1] + tv[0] * T0[0] + tv[2] * T0[2]) {
                        if ((fabs(d2 - best2) <= fabs(tol) && fabs(tol) != fabs(d2 - best2)) || (fabs(d3 - best3) <= fabs(tol) && fabs(tol) != fabs(d3 - best3))) {
                            bVar6 = 1; local_80b2 = k; local_80a8 = e;
                            if ((fabs(d2) <= tol && tol != fabs(d2)) && (fabs(best2) <= tol && tol != fabs(best2))) goto check;   /* LAB_18056a30f */
                        } else if ((d2 <= best2 && best2 != d2) || (d3 <= best3 && best3 != d3)) goto newbest;
                    }
                } else {
newbest:            /* LAB_18056a2e0 */
                    bVar7 = 1; best2 = d2; best3 = d3; sVar11 = k; local_80c0 = e; local_80b0 = sVar21;
                }
            } else if (k == 0) bVar7 = 0;   /* both ends of the piece the same point */
            else { sVar11 = local_80b0; if (k != sVar21 && (sVar11 = sVar21, bVar6) && k == local_80b2) bVar6 = 0; }
            sVar21 = sVar11;
            if (e == 0) prev3 = d3;
        }
        if (bVar6) {
check:
            if ((fabs(d2) <= tol && tol != fabs(d2)) && (fabs(best2) <= tol && tol != fabs(best2))) break;
        }
    }
    int first = sVar21;
    if (bVar6) {   /* the tie: the end whose direction is more along the start tangent */
        double ta[9], tb[9];
        cons_eval(pc[sVar21].base, pc[sVar21].n, (const double (*)[2])pc[sVar21].uv, 1, local_80c0 ? (double)(pc[sVar21].n - 1) : 0.0, ta);
        cons_eval(pc[local_80b2].base, pc[local_80b2].n, (const double (*)[2])pc[local_80b2].uv, 1, local_80a8 ? (double)(pc[local_80b2].n - 1) : 0.0, tb);
        double a[3] = { ta[3], ta[4], ta[5] }, b[3] = { tb[3], tb[4], tb[5] };
        if (local_80c0) { a[0] = -a[0]; a[1] = -a[1]; a[2] = -a[2]; }
        if (local_80a8) { b[0] = -b[0]; b[1] = -b[1]; b[2] = -b[2]; }
        unit3(a); unit3(b);
        if (a[0] * T0[0] + a[1] * T0[1] + a[2] * T0[2] < b[1] * T0[1] + b[0] * T0[0] + b[2] * T0[2]) first = local_80b2;
    }
    /* setc5negchain1sts (the chosen end is the piece's end): read only with join_keep_first (static 0) */
    if (0 < first) swap_pieces(0, first);
}
/* FUN_180535ea0 (cs2.c 1154, param_1 = 1): the four end pairs of pieces i and j (3D ends evaluated on the base surface; a uv distance
   over a tenth of the u range counts as 1e38), each within msf_join_tol links them unless the ends already hold a nearer link */
static void unlink_from(int o, int me) { if (o >= 0) { if (pc[o].l5c == me) pc[o].l5c = -1; else if (pc[o].l5e == me) pc[o].l5e = -1; } }
static void link_pair(int i, int j) {
    Piece *A = &pc[i], *B = &pc[j]; const uint8_t *s = A->base; double E[18], As[3], Ae[3], Bs[3], Be[3];
    eval_surf(s, 0, A->uv0[0], A->uv0[1], E); memcpy(As, E, 24); eval_surf(s, 0, A->uv1[0], A->uv1[1], E); memcpy(Ae, E, 24);
    eval_surf(s, 0, B->uv0[0], B->uv0[1], E); memcpy(Bs, E, 24); eval_surf(s, 0, B->uv1[0], B->uv1[1], E); memcpy(Be, E, 24);
    double ma[6], mb[6]; mm_init(ma); mm_init(mb); add_to_minmax(As, ma); add_to_minmax(Ae, ma); add_to_minmax(Bs, mb); add_to_minmax(Be, mb);
    double jt = msf_join_tol;
    for (int q = 0; q < 6; q += 2) { ma[q] -= jt; ma[q + 1] += jt; mb[q] -= jt; mb[q + 1] += jt; }
    if (!boxes_overlap(ma, mb)) return;
    double ur = (D(s, 0x10) - D(s, 8)) * 0.1;
    /* start - start */
    double d = sqrt((Bs[1] - As[1]) * (Bs[1] - As[1]) + (Bs[0] - As[0]) * (Bs[0] - As[0]) + (Bs[2] - As[2]) * (Bs[2] - As[2]));
    if (ur < hypot(B->uv0[0] - A->uv0[0], B->uv0[1] - A->uv0[1])) d = 1e38;
    if (d <= jt && jt != d) {
        int s3 = A->l5c, ok = 0;
        if (s3 == -1) ok = 1; else if (s3 >= 0) ok = d <= A->d60 && A->d60 != d;
        if (ok) { ok = 0; if (B->l5c == -1) ok = 1; else if (B->l5c >= 0) ok = d <= B->d60 && B->d60 != d; }
        if (ok) { unlink_from(s3, i); unlink_from(B->l5c, j); A->l5c = j; B->d60 = d; B->l5c = i; A->d60 = d; A->f70 = 0; B->f70 = 0; }
    }
    /* start - end */
    d = sqrt((Be[1] - As[1]) * (Be[1] - As[1]) + (Be[0] - As[0]) * (Be[0] - As[0]) + (Be[2] - As[2]) * (Be[2] - As[2]));
    if (ur < hypot(B->uv1[0] - A->uv0[0], B->uv1[1] - A->uv0[1])) d = 1e38;
    if (d <= jt && jt != d) {
        int s3 = A->l5c, ok = 0;
        if (s3 == -1) ok = 1; else if (s3 >= 0) ok = d <= A->d60 && A->d60 != d;
        if (ok) { ok = 0; if (B->l5e == -1) ok = 1; else if (B->l5e >= 0) ok = d <= B->d68 && B->d68 != d; }
        if (ok) { unlink_from(s3, i); unlink_from(B->l5e, j); A->l5c = j; B->d68 = d; B->l5e = i; A->d60 = d; A->f70 = 1; B->f71 = 0; }
    }
    /* end - start */
    d = sqrt((Bs[1] - Ae[1]) * (Bs[1] - Ae[1]) + (Bs[0] - Ae[0]) * (Bs[0] - Ae[0]) + (Bs[2] - Ae[2]) * (Bs[2] - Ae[2]));
    if (ur < hypot(B->uv0[0] - A->uv1[0], B->uv0[1] - A->uv1[1])) d = 1e38;
    if (d <= jt && jt != d) {
        int s3 = A->l5e, ok = 0;
        if (s3 == -1) ok = 1; else if (s3 >= 0) ok = d <= A->d68 && A->d68 != d;
        if (ok) { ok = 0; if (B->l5c == -1) ok = 1; else if (B->l5c >= 0) ok = d <= B->d60 && B->d60 != d; }
        if (ok) { unlink_from(s3, i); unlink_from(B->l5c, j); A->l5e = j; B->d60 = d; B->l5c = i; A->d68 = d; A->f71 = 0; B->f70 = 1; }
    }
    /* end - end */
    d = sqrt((Be[1] - Ae[1]) * (Be[1] - Ae[1]) + (Be[0] - Ae[0]) * (Be[0] - Ae[0]) + (Be[2] - Ae[2]) * (Be[2] - Ae[2]));
    if (ur < hypot(B->uv1[0] - A->uv1[0], B->uv1[1] - A->uv1[1])) d = 1e38;
    if (d <= jt && jt != d) {
        int s3 = A->l5e, ok = 0;
        if (s3 == -1) ok = 1; else if (s3 >= 0) ok = d <= A->d68 && A->d68 != d;
        if (ok) { ok = 0; if (B->l5e == -1) ok = 1; else if (B->l5e >= 0) ok = d <= B->d68 && B->d68 != d; }
        if (ok) { unlink_from(s3, i); unlink_from(B->l5e, j); A->l5e = j; B->d68 = d; B->l5e = i; A->d68 = d; A->f71 = 1; B->f71 = 1; }
    }
}
/* FUN_1805358b0(n, sp, 1) (cs1.c 3328): links reset (>= 0 -> -1, 1e38), an end outside the base's range (FUN_1806f71a0) unlinkable,
   then FUN_180535ea0 for each pair on one surface (+0x5a tag, +0x48 base, +0x50 surface the same) */
static void link_pieces(void) {
    for (int i = 0; i < npc; i++) if (pc[i].alive) {
        if (pc[i].l5c >= 0) { pc[i].l5c = -1; pc[i].d60 = 1e38; }
        if (pc[i].l5e >= 0) { pc[i].l5e = -1; pc[i].d68 = 1e38; }
    }
    for (int i = 0; i < npc - 1; i++) {
        if (!pc[i].alive) continue;
        double q[2] = { pc[i].uv0[0], pc[i].uv0[1] }; if (range_bits(pc[i].base, 0, q)) pc[i].l5c = -1;
        q[0] = pc[i].uv1[0]; q[1] = pc[i].uv1[1]; if (range_bits(pc[i].base, 0, q)) pc[i].l5e = -1;
        for (int j = i + 1; j < npc; j++) if (pc[j].alive && pc[j].srf == pc[i].srf) link_pair(i, j);
    }
}
/* FUN_1805474e0 (cs1.c 326, join_keep_first static 0): from each live piece with a link, the linked pieces followed (a piece whose start
   link is the way on is taken reversed, the end tangents must not turn back - dot < -0.995 stops), their uv points appended (the shared
   point once); the joined points' ends averaged when they meet (a closed curve: within msf_join_tol in 3D and a tenth of the range in
   uv), then FUN_18054d4f0 on the base (thinned, a new surface curve); the joined pieces deleted. Returns 0 when a case is not ported. */
static int join_pieces(void) {
    if (npc < 2) return 1;
    int maxn = max_spline_pts;
    double (*buf)[2] = calloc((size_t)maxn + 1, sizeof *buf); if (!buf) return 0;
    int n0 = npc;
    for (int k = 0; k < n0; k++) {
        Piece *P = &pc[k];
        if (!P->alive || (P->l5c < 0 && P->l5e < 0)) continue;
        if (P->n >= maxn) goto unlink;
        /* the closed-piece test at the start (both links negative: skipped) cannot hold here: the links were checked above */
        int cnt, bVar13 = 0, nxt; double end[3], et[3], o[9];
        const uint8_t *bs = P->base;
        if (P->l5e < 0) {   /* reversed: the free end first */
            for (int i = 0; i < P->n; i++) { buf[i][0] = P->uv[P->n - 1 - i][0]; buf[i][1] = P->uv[P->n - 1 - i][1]; }
            memcpy(end, P->P0, 24);
            cons_eval(bs, P->n, (const double (*)[2])P->uv, 1, 0.0, o); memcpy(et, o + 3, 24); unit3(et); et[0] = -et[0]; et[1] = -et[1]; et[2] = -et[2];
            int t = P->l5c; P->l5c = P->l5e; P->l5e = t; uint8_t f = P->f70; P->f70 = P->f71; P->f71 = f;   /* the sp ends (+8 / +0x20) stay */
        } else {
            memcpy(buf, P->uv, 16 * (size_t)P->n);
            memcpy(end, P->P1, 24);
            cons_eval(bs, P->n, (const double (*)[2])P->uv, 1, (double)(P->n - 1), o); memcpy(et, o + 3, 24); unit3(et);
        }
        nxt = P->l5e; cnt = P->n;
        while (nxt >= 0 && nxt != k) {
            Piece *M = &pc[nxt];
            if (!M->alive) break;
            if (maxn <= M->n - 1 + cnt) break;
            int rev;
            double ds = dist3(end, M->P0), de = dist3(end, M->P1);
            if (dist3(M->P0, M->P1) < msf_join_tol && !(ds <= msf_join_tol) && !(de <= msf_join_tol)) { M->l5c = M->l5e = -1; break; }
            if (ds <= msf_join_tol) {
                if (de <= msf_join_tol) rev = de < ds; else rev = 0;
            } else rev = 1;
            double mt[3];
            if (rev) { cons_eval(bs, M->n, (const double (*)[2])M->uv, 1, (double)(M->n - 1), o); memcpy(mt, o + 3, 24); unit3(mt); mt[0] = -mt[0]; mt[1] = -mt[1]; mt[2] = -mt[2]; }
            else { cons_eval(bs, M->n, (const double (*)[2])M->uv, 1, 0.0, o); memcpy(mt, o + 3, 24); unit3(mt); }
            if (mt[1] * et[1] + mt[0] * et[0] + mt[2] * et[2] < -0.995) { M->l5c = M->l5e = -1; break; }
            if (rev) {
                for (int i = 0; i < M->n - 1; i++) { buf[cnt + i][0] = M->uv[M->n - 2 - i][0]; buf[cnt + i][1] = M->uv[M->n - 2 - i][1]; }
                memcpy(end, M->P0, 24);
                cons_eval(bs, M->n, (const double (*)[2])M->uv, 1, 0.0, o); memcpy(et, o + 3, 24); unit3(et); et[0] = -et[0]; et[1] = -et[1]; et[2] = -et[2];
            } else {
                memcpy(buf + cnt, M->uv + 1, 16 * (size_t)(M->n - 1));
                memcpy(end, M->P1, 24);
                cons_eval(bs, M->n, (const double (*)[2])M->uv, 1, (double)(M->n - 1), o); memcpy(et, o + 3, 24); unit3(et);
            }
            cnt += M->n - 1;
            if (!bVar13) { bVar13 = 1; P->alive = 0; }
            M->alive = 0;
            nxt = rev ? M->l5c : M->l5e;
        }
        if (bVar13) {
            double E[18], A[3], B[3];
            eval_surf(bs, 0, buf[0][0], buf[0][1], E); memcpy(A, E, 24);
            eval_surf(bs, 0, buf[cnt - 1][0], buf[cnt - 1][1], E); memcpy(B, E, 24);
            double du = buf[0][0] - buf[cnt - 1][0], lu = (D(bs, 0x10) - D(bs, 8)) * 0.1, lv = (D(bs, 0x20) - D(bs, 0x18)) * 0.1;
            if (fabs(du) <= lu && lu != fabs(du) && fabs(buf[0][1] - buf[cnt - 1][1]) <= lv && lv != fabs(buf[0][1] - buf[cnt - 1][1]) && dist3(B, A) <= msf_join_tol) {
                double mu = (buf[0][0] + buf[cnt - 1][0]) * 0.5, mv = (buf[cnt - 1][1] + buf[0][1]) * 0.5;
                buf[0][0] = mu; buf[0][1] = mv; buf[cnt - 1][0] = mu; buf[cnt - 1][1] = mv;
            }
            int m = thin_piece(bs, cnt, buf);   /* FUN_18054d4f0 on the base; the new surface curve replaces sp[k] (alive again, its uv ends) */
            if (1 < m) {
                double (*nu)[2] = malloc(sizeof *nu * m); if (!nu) { free(buf); return 0; }
                memcpy(nu, buf, 16 * (size_t)m); free(P->uv); P->uv = nu; P->n = m;
                cons_body(bs, m, (const double (*)[2])P->uv, P->body);
                memcpy(P->uv0, buf[0], 16); memcpy(P->uv1, buf[m - 1], 16); memcpy(P->P0, P->body, 24); memcpy(P->P1, P->body + 0x18, 24);
                P->alive = 1; P->seq = ++pc_seq;
            }
        }
        continue;
unlink:
        if (P->l5c >= 0) P->l5c = -1;
        if (P->l5e >= 0) P->l5e = -1;
    }
    free(buf);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the command's kernel */
int cs_project(const CsCurve *cv, const CsSurf *ss, int ns, const CsParams *pp, CsResult **res, char *msg, size_t msgn) {
    *res = NULL; if (msg && msgn) msg[0] = 0;
    tol = pp->tol; surf_step_chord_dev = pp->chord; curve_min_step = pp->min_step; is_metric = pp->metric;
    /* project_chains_onto_surfs (cs1.c 1130) with sketcher_curve_6's arguments (decomp/mccad_sketch.c 86-89): (chain, 0,
       surf_step_dist, 1, surf_step_chord_dev, surf_step_chord_dev, 0, 0, 1, 0) */
    surf_step_dist = 0.1; proj_dist = 0.1; surf_use_chord_dev = 1; msf_find_multi = 1; msf_join_tol = pp->chord;
    notported = 0; npc = 0; pc_seq = 0; view_np = 0; views = pp->views;
    if (!views) { snprintf(msg, msgn, "Curve Surface: no view table (find_view_k)"); return -1; }
    Crv c; memset(&c, 0, sizeof c);
    c.r = cv->rec; c.n = cv->n; c.cls = (int)*(const uint32_t *)cv->rec; c.vm = cv->vm;
    c.cr.cls = (uint32_t)c.cls; c.cr.rec = (uint8_t *)cv->rec; c.cr.n = cv->n;
    if (c.cls != 2 && c.cls != 4 && c.cls != 8 && c.cls != 0x80) { snprintf(msg, msgn, "Curve Surface: entity class %#x is not a line, arc or spline", c.cls); return -1; }
    if ((c.cls == 8 || c.cls == 0x80) && !mcs_spline_ok(&c.cr)) { snprintf(msg, msgn, "Curve Surface: the spline record cannot be evaluated here (NOT PORTED)"); return -1; }
    if (!build_grid2(&c)) { snprintf(msg, msgn, "Curve Surface: out of memory"); return -1; }
    uint8_t **img = calloc((size_t)ns + 1, sizeof *img), **bimg = calloc((size_t)ns + 1, sizeof *bimg);
    int rc = 0;
    for (int i = 0; i < ns && rc >= 0; i++) {
        const uint8_t *r = ss[i].rec; int kind = r[0x68];
        if (kind != 0 && kind != 10) continue;   /* other kinds: the caller refuses them (only NURBS and trimmed NURBS are evaluated here) */
        img[i] = mk_image(r, ss[i].n);
        if (kind == 10) {
            if (!ss[i].base || ss[i].base[0x68] != 0) { free_image(img[i]); img[i] = NULL; continue; }
            bimg[i] = mk_image(ss[i].base, ss[i].nbase); PTR(img[i], 0x100) = bimg[i];
        }
        const uint8_t *s = img[i] + 0x68;
        if (!build_grid1(s)) { rc = -1; snprintf(msg, msgn, "Curve Surface: out of memory"); break; }
        /* FUN_18053aed0(1, dist), get_grid_minmax, FUN_18054e400 (asm1.txt 180569a9b-180569b37) */
        g1mm[0] -= proj_dist; g1mm[1] += proj_dist; g1mm[2] -= proj_dist; g1mm[3] += proj_dist; g1mm[5] += proj_dist; g1mm[4] -= proj_dist;
        if (!boxes_overlap(g1mm, g2mm)) continue;
        project_on_surf(s, i, &c, 0);
        if (notported) { rc = -1; snprintf(msg, msgn, "Curve Surface: a second piece of the curve on one surface (FUN_180531890) is NOT DECODED"); }
    }
    /* the chain's pieces ordered (LAB_180569df8), then FUN_1805474b0 -> FUN_1805474e0: with two or more pieces FUN_1805358b0's links and the
       joins (pieces on different surfaces are never linked: FUN_1805358b0 compares +0x48 / +0x50) */
    if (getenv("CSURF_DBG")) for (int k = 0; k < npc; k++) fprintf(stderr, "piece %d srf %d n %d uv (%.6f %.6f)-(%.6f %.6f) links %d %d\n", k, pc[k].srf, pc[k].n, pc[k].uv0[0], pc[k].uv0[1], pc[k].uv1[0], pc[k].uv1[1], pc[k].l5c, pc[k].l5e);   /* tests */
    if (rc >= 0) order_pieces(&c);
    if (rc >= 0 && view_np) { rc = -1; snprintf(msg, msgn, "Curve Surface: no view for the surface normal at the first piece end (find_view_k found none: the DLL then reads an uninitialised view number) NOT DECODED"); }
    if (rc >= 0 && npc >= 2) { link_pieces(); if (!join_pieces()) { rc = -1; snprintf(msg, msgn, "Curve Surface: out of memory"); } }
    int nalive = 0; for (int k = 0; k < npc; k++) nalive += pc[k].alive;
    if (rc >= 0 && nalive > 0) {   /* the surface curves left, in the order they were stored */
        CsResult *r = calloc((size_t)nalive, sizeof *r); int m = 0;
        for (int sq = 1; sq <= pc_seq; sq++) for (int k = 0; k < npc; k++) if (pc[k].alive && pc[k].seq == sq) {
            memcpy(r[m].body, pc[k].body, 0x88); r[m].srf = pc[k].srf; r[m].npts = pc[k].n; r[m].uv = pc[k].uv; pc[k].uv = NULL; m++; }
        *res = r; rc = m;
    }
    for (int k = 0; k < npc; k++) free(pc[k].uv);
    npc = 0;
    for (int i = 0; i < ns; i++) { free_image(img[i]); free_image(bimg[i]); }
    free(img); free(bimg); free_grids();
    return rc;
}
void cs_free(CsResult *r, int n) { if (!r) return; for (int k = 0; k < n; k++) free(r[k].uv); free(r); }

/* ------------------------------------------------------------------------------------------------ display (mastercam.dll FUN_180237990) */
static uint8_t *surf_images(const CsSurf *s, uint8_t **base) {
    *base = NULL;
    const uint8_t *r = s->rec;
    if (r[0x68] == 10) { if (!s->base || s->base[0x68] != 0) return NULL; uint8_t *im = mk_image(r, s->n); *base = mk_image(s->base, s->nbase); PTR(im, 0x100) = *base; return im; }
    if (r[0x68] != 0) return NULL;
    return mk_image(r, s->n);
}
static int uv_points(const uint8_t *uvc, uint32_t nuv, double (**uv)[2]) {   /* an order-2 2D NURBS (fetch_cons_n_and_pts: its control points) */
    if (nuv < 0xd8 || *(const uint32_t *)uvc != 0x80 || uvc[0xd0] != 1 || uvc[0xd1] != 2) return 0;
    int nk = I32(uvc, 0xc8), ncp = I32(uvc, 0xcc);
    if (ncp < 2 || 0xd8 + 8 * ((size_t)nk + 2 * (size_t)ncp) > nuv) return 0;
    *uv = malloc(sizeof **uv * ncp); if (!*uv) return 0;
    memcpy(*uv, uvc + 0xd8 + 8 * nk, 16 * (size_t)ncp);
    return ncp;
}
int cs_cons_eval(const uint8_t *uvcrv, uint32_t nuv, const CsSurf *s, double t, double p[3]) {
    double (*uv)[2] = NULL; int n = uv_points(uvcrv, nuv, &uv); if (!n) return -1;
    uint8_t *b, *im = surf_images(s, &b); if (!im) { free(uv); return -1; }
    double o[9]; int r = cons_eval(im + 0x68, n, (const double (*)[2])uv, 0, t, o); memcpy(p, o, 24);
    free_image(im); free_image(b); free(uv);
    return r;
}
/* FUN_180237990 (decomp/curvesurf/mc_drawcons.c): an order-2 uv curve is drawn segment by segment, each in (int)(chord / size x 20)
   (at least 1) equal uv steps, size = the surface's surf_type +0x70 (the base's when trimmed: put_cons_in_db stores the base);
   cons +0x81 0 draws nothing. Higher orders (the else branch: curvature steps) are NOT PORTED (no corpus uv curve has them). */
int cs_cons_polyline(const uint8_t *cons, uint32_t ncons, const uint8_t *uvcrv, uint32_t nuv, const CsSurf *s, double (**pts)[3]) {
    *pts = NULL;
    if (ncons < 0x68 + 0x88 || !cons[0x68 + 0x81]) return 0;
    double (*uv)[2] = NULL; int n = uv_points(uvcrv, nuv, &uv); if (!n) return 0;
    uint8_t *b, *im = surf_images(s, &b); if (!im) { free(uv); return 0; }
    const uint8_t *sf = im + 0x68; double size = D(sf, 0x70);
    double t1 = D(uvcrv, 0x98), t2 = D(uvcrv, 0xa0);
    int s0 = (short)(int)(t1 + 1e-05) + 1, s1 = (short)(int)(t2 - 1e-05) + 1, cap = 256, np = 0;
    double (*out)[3] = malloc(sizeof *out * cap);
    if (!out || s1 < s0) { free(out); free(uv); free_image(im); free_image(b); return 0; }
    for (int k = s0; k <= s1; k++) {   /* segment k from point k - 1 to k, the curve's ends at t1 / t2 */
        double a[2], e[2], E[18], Pa[3], Pe[3];
        if (k == s0) { double o[9]; uvcrv_eval(n, (const double (*)[2])uv, t1, o); a[0] = o[0]; a[1] = o[1]; } else { a[0] = uv[k - 1][0]; a[1] = uv[k - 1][1]; }
        if (k == s1) { double o[9]; uvcrv_eval(n, (const double (*)[2])uv, t2, o); e[0] = o[0]; e[1] = o[1]; } else { e[0] = uv[k][0]; e[1] = uv[k][1]; }
        eval_surf(sf, 0, a[0], a[1], E); memcpy(Pa, E, 24);
        eval_surf(sf, 0, e[0], e[1], E); memcpy(Pe, E, 24);
        int m = (short)(int)((dist3(Pe, Pa) / size) * 20.0), steps = 1 < m ? m : 1;
        double du = (e[0] - a[0]) / steps, dv = (e[1] - a[1]) / steps, q[2] = { a[0], a[1] };
        if (k != s0) { q[0] += du; q[1] += dv; }
        for (int j = k != s0; j <= steps; j++) {
            if (np + 1 >= cap) { cap *= 2; void *x = realloc(out, sizeof *out * cap); if (!x) break; out = x; }
            if (j == 0) memcpy(out[np], Pa, 24);
            else if (j == steps) memcpy(out[np], Pe, 24);
            else { eval_surf(sf, 0, q[0], q[1], E); memcpy(out[np], E, 24); }
            np++; q[0] += du; q[1] += dv;
        }
    }
    free(uv); free_image(im); free_image(b);
    *pts = out; return np;
}
/* the loader's drawing hook (mcam_load.h mcam_cons_poly): set when this file is linked */
static int cons_poly_hook(const uint8_t *cons, uint32_t ncons, const uint8_t *uv, uint32_t nuv, const uint8_t *srec, uint32_t sn, const uint8_t *base, uint32_t bn, double (**pts)[3]) {
    CsSurf s = { srec, sn, base, bn };
    return cs_cons_polyline(cons, ncons, uv, nuv, &s, pts);
}
__attribute__((constructor)) static void cons_poly_init(void) { mcam_cons_poly = cons_poly_hook; }

/* ------------------------------------------------------------------------------------------------ gl_Hit_Surfs' (u, v) (formats/chaining.md
   "The shading-on pick (port)") */
/* find_neighbor (0x1805eccf0, decomp/chain_srfedge_undo.c) with param_4 = -1 (no view): over the grid's cells (j < nv - 1, i < nu - 1) the
   point of the cell's edges a-b (t1 of FUN_1804b47c0, clamped 0 .. 1) then a-c from there (t2) nearest p (sqrt(dy^2 + dx^2 + dz^2), strictly
   nearer); the (u, v) the same way from the grid's (u, v). Returns 1 (found) when the nearest is below 1e38; the point eval_surf gives
   there (param_9) is not used by gl_Hit_Surfs */
static int find_neighbor_nv(const uint8_t *s, int nu, int nv, const double (*P)[3], const double (*UV)[2], const double *p, double *uvo) {
    (void)s;
    double best = 1e38;
    for (int j = 0; j < nv - 1; j++) for (int i = 0; i < nu - 1; i++) {
        int ia = j * nu + i; const double *a = P[ia], *b = P[ia + 1], *c = P[ia + nu];
        double lab[6] = { a[0], a[1], a[2], b[0], b[1], b[2] }, lac[6] = { a[0], a[1], a[2], c[0], c[1], c[2] };
        double t1 = seg_param(p, lab), t2 = seg_param(p, lac), R[3], Q[3];
        if (0.0 < t1) {
            if (t1 < 1.0) { R[0] = (b[0] - a[0]) * t1 + a[0]; R[1] = a[1] + (b[1] - a[1]) * t1; R[2] = a[2] + (b[2] - a[2]) * t1; }
            else { R[0] = b[0]; R[1] = b[1]; R[2] = b[2]; t1 = 1.0; }
        } else { R[0] = a[0]; R[1] = a[1]; R[2] = a[2]; t1 = 0.0; }
        if (0.0 < t2) {
            double dz = c[2] - a[2], dx = c[0] - a[0], dy = c[1] - a[1];
            if (t2 < 1.0) { dx = dx * t2; dy = dy * t2; dz = dz * t2; } else t2 = 1.0;
            Q[0] = R[0] + dx; Q[1] = R[1] + dy; Q[2] = R[2] + dz;
        } else { Q[0] = R[0]; Q[1] = R[1]; Q[2] = R[2]; t2 = 0.0; }
        double d = sqrt((p[1] - Q[1]) * (p[1] - Q[1]) + (p[0] - Q[0]) * (p[0] - Q[0]) + (p[2] - Q[2]) * (p[2] - Q[2]));
        if (best < d || best == d) continue;
        const double *ua = UV[ia], *ub = UV[ia + 1], *uc = UV[ia + nu]; double U[2];
        if (t1 == 0.0) { U[0] = ua[0]; U[1] = ua[1]; }
        else if (t1 == 1.0) { U[0] = ub[0]; U[1] = ub[1]; }
        else { U[0] = (ub[0] - ua[0]) * t1 + ua[0]; U[1] = (ub[1] - ua[1]) * t1 + ua[1]; }
        if (t2 == 0.0) { uvo[0] = U[0]; uvo[1] = U[1]; }
        else {
            double x = uc[0] - ua[0], y = uc[1] - ua[1];
            if (t2 <= 1.0 && t2 != 1.0) { x = x * t2; y = y * t2; }
            uvo[0] = x + U[0]; uvo[1] = y + U[1];
        }
        best = d;
    }
    return best <= 1e38 && best != 1e38;
}
/* gl_Hit_Surfs' tail (mastercam.dll FUN_180224220, asm 180224b7a..180224d88; decomp/surfpick/mc_pick.c l.440-484): on the hit
   surface build_grid(1, surf, 0); try k = 0 .. 5: k > 0 project_pt_onto_surf(surf, -1, 0, hit, uv0, uv, S) (FUN_1805ed4a0, then
   param_3 = 0: both parameters inside the range, a trimmed surface's in_polygon != -1), accepted (the loop ends) unless
   |hit - S|^2 (dy^2 + dx^2 + dz^2) > gl_tolerance x 400 x gl_tolerance; k = 0: fill_grid / find_neighbor(.., -1, ..) give uv0 (none:
   no try at all); after a failed try k = 1 .. 4 uv0 = the corners ((1 - a) u1 + a u0, (1 - b) v1 + b v0), a = 0.75 for k = 1, 2 else 0.25,
   b = 0.75 for k = 1, 3 else 0.25 (.rdata 0x1805ced30 / 0x1805ced18); free_grid. uv = the last (u, v) FUN_1805ed4a0 wrote (its success
   path; *uv_set = 0 when none did - the DLL's caller then reads its own buffer). Returns 1 accepted, 0 none, -1 not ported (a surface
   other than kind 0 / a trimmed one on a kind 0 base) / out of memory. */
int cs_glhit_uv(const CsSurf *ss, const CsParams *pp, const double hit[3], double gl_tol, double uv[2], int *uv_set) {
    *uv_set = 0;
    tol = pp->tol; surf_step_chord_dev = pp->chord; curve_min_step = pp->min_step; is_metric = pp->metric;
    uint8_t *b, *im = surf_images(ss, &b); if (!im) return -1;
    const uint8_t *s = im + 0x68;
    int acc = 0;
    if (!build_grid1(s)) { free_image(im); free_image(b); free_grids(); return -1; }
    double uv0[2] = { 0, 0 };
    for (int k = 0; k < 6; k++) {
        int r = 0;
        if (0 < k) {
            char inu = 0, inv = 0, ok = 0; double q[2] = { NAN, NAN }, S[3] = { 0, 0, 0 };
            project_core(s, hit, uv0, q, &inu, &inv, &ok, S);
            if (!isnan(q[0]) || !isnan(q[1])) { uv[0] = q[0]; uv[1] = q[1]; *uv_set = 1; }
            r = ok && inu && inv;                                   /* project_pt_onto_surf with param_3 = 0 */
            if (r && s[0] == 10) r = trim_in(s, q) != -1;
            if (r) {
                double x = hit[0] - S[0], y = hit[1] - S[1], z = hit[2] - S[2], d2 = y * y + x * x + z * z;
                if (!(d2 > gl_tol * 400.0 * gl_tol)) { acc = 1; break; }
                r = 0;
            }
        }
        if (k == 0 && !find_neighbor_nv(s, g1nu, g1nv, (const double (*)[3])g1p, (const double (*)[2])g1uv, hit, uv0)) k = 6;
        if (!r && (unsigned short)(k - 1) <= 3) {
            double a = (unsigned short)(k - 1) <= 1 ? 0.75 : 0.25, bb = ((k - 1) & 0xfffd) == 0 ? 0.75 : 0.25;
            uv0[0] = (1.0 - a) * D(s, 0x10) + D(s, 8) * a;
            uv0[1] = (1.0 - bb) * D(s, 0x20) + D(s, 0x18) * bb;
        }
    }
    free_grids(); free_image(im); free_image(b);
    return acc;
}
/* eval_surf(surf, 0, u, v) (select_ent_univ's point of the shaded hit, mastercam.dll 0x18020f4a0 l.820): 0 ok */
int cs_surf_point(const CsSurf *ss, double u, double v, double P[3]) {
    uint8_t *b, *im = surf_images(ss, &b); if (!im) return -1;
    double E[18]; int r = eval_surf(im + 0x68, 0, u, v, E); if (!r) memcpy(P, E, 24);
    free_image(im); free_image(b);
    return r;
}
