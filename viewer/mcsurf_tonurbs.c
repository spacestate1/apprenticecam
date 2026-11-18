/* mcsurf_tonurbs.c - a surface entity as a NURBS surface: the surface branch of Wireframe > Spline Convert to NURBS (mccadutil convert_to_nurbs
   0x1800595e0 -> convert_entity_to_nurbs 0x180059150, surf 0x100 -> mccore create_nurbsurf_from_surf 0x180268750 -> FUN_180266ca0 -> put_surf_in_db).
   decomp/surfcreate/core_nurbs.c unless stated otherwise; S = ent +0x68 (surf_type), D = ent +0x100 (the kind's data).

   Kind 1: FUN_180265e70. Kind 2: convert_revsurf_to_nbs (mcsb_rev_of_kind2 in mcsurf_build.c). Kind 6: FUN_1802641d0 picks FUN_180266ed0 (exact) or
   FUN_1802647b0 (bicubic, FUN_1802699a0 breaks, FUN_18026a290 rows). Kind 7: FUN_180264a30 and FUN_18026acf0, FUN_180264310 picks by the curvature
   radius range. Kinds 8 / 10 on a non-NURBS base: mcstn_base (the blanked NURBS base) then mcstn_rebuild (make_new_trim, mccadutil 0x18005a9b0, ported below with smart_project_onto_surf).
   NOT PORTED, refused with *why: kind 4 (FUN_180265980: MaCompositeCurve / MaBicubicSurf; FUN_1802655f0 path); a kind 10 on an offset of a non-NURBS base with a net offset (make_new_trim
   onto a kind 8 target: FUN_180460cd0 modes 1 / 2); a kind 10 whose direct base is itself offset / trimmed; a degenerate column of the second kind 7 construction. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mcsurf.h"
#include "mcsurf_build.h"
#include "mcsurf_tonurbs.h"

/* mcsurf_build.c */
uint8_t *mcsb_rev_of_kind2(const McamFile *f, const Surf *s, double tol, uint32_t *n);
uint8_t *mcsb_trim_rec(const McamFile *f, int64_t base_eptr, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt, uint16_t flags, int shaded, double tol, uint32_t *n);
int mcsb_nurb_interval(double u, const double *K, int nk, int p);
int mcsb_ent_nb(const McamFile *f, const Ent *en, double tol, int *nk, int *ncp, int *deg, int *dim, int *rat, double **k, double **p);
int mcsb_arc2d_nb(double cx, double cy, double r, double a, double s, double hz, double tol, double *P, double *K, int *ncp, int *nk);
int mcsb_nurbs_ders(const uint8_t *r, uint32_t n, double u, double v, double o[6][3]);
void mcsb_curv_geom(double o[6][3], double *k1, double *k2);

static double rd(const uint8_t *b) { double v; memcpy(&v, b, 8); return v; }
static int64_t r64(const uint8_t *b) { int64_t v; memcpy(&v, b, 8); return v; }
static int32_t r32(const uint8_t *b) { int32_t v; memcpy(&v, b, 4); return v; }
static uint16_t r16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static void wdd(uint8_t *b, double v) { memcpy(b, &v, 8); }
static void w32(uint8_t *b, int32_t v) { memcpy(b, &v, 4); }

static void cross(const double a[3], const double b[3], double o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static double dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static double len(const double a[3]) { return sqrt(dot(a, a)); }
/* FUN_18000f1d0 / FUN_18000f2f0 (decomp/surfcreate/core_nurbs.c): a zero vector stays zero */
static void unit3(double a[3]) { double l = len(a); if (l == 0.0) { a[0] = a[1] = a[2] = 0.0; } else { a[0] /= l; a[1] /= l; a[2] /= l; } }

/* ------------------------------------------------------------------------------------------------ the kind 0 record */
/* check_nurb_knots (0x1805ae200, put_surf_in_db): an end knot off by more than 1e-8 from an otherwise fully multiple end is pulled in */
static void check_knots(int nk, int deg, double *K) {
    if (K[0] <= K[1] - 1e-8 && K[1] - 1e-8 != K[0]) {
        int ok = 1; for (int i = 2; i <= deg; i++) if (fabs(K[i] - K[1]) > 1e-8) { ok = 0; break; }
        if (ok) K[0] = K[1];
    }
    if (K[nk - 1] > K[nk - 2] + 1e-8) {
        int ok = 1; for (int i = 2; i <= deg; i++) if (fabs(K[nk - 1 - i] - K[nk - 2]) > 1e-8) { ok = 0; break; }
        if (ok) K[nk - 1] = K[nk - 2];
    }
}
/* the nbs_type of ent +0x100 (formats/mcam.md "NURBS surface"): +0 / +4 knot counts, +8 / +0xc control counts, +0x10 values per point, +0x11 / +0x12 degrees, +0x13 flags
   (bit 0 = non-rational); the knots then the points follow at +0x118. create_nurbsurf_from_surf's else branch: the flags = the source's bits 0..2 + 0x40, the range = the first / last u and v knots. */
static uint8_t *make_nbs(const Surf *s, int nku, int nkv, int ncu, int ncv, int du, int dv, int vpp, int nonrat, const double *Ku, const double *Kv, const double *P, uint32_t *rn) {
    *rn = 0x118 + 8 * (uint32_t)(nku + nkv + vpp * ncu * ncv);
    uint8_t *R = calloc(1, *rn); if (!R) return NULL;
    uint32_t cls = 0x100; memcpy(R, &cls, 4);
    uint8_t *S = R + 0x68;
    S[0] = 0;
    uint16_t fl = (uint16_t)((r16(s->rec + 0x6e) & 7) | 0x40); S[6] = (uint8_t)fl; S[7] = (uint8_t)(fl >> 8);
    w32(R + 0x100, nku); w32(R + 0x104, nkv); w32(R + 0x108, ncu); w32(R + 0x10c, ncv);
    R[0x110] = (uint8_t)vpp; R[0x111] = (uint8_t)du; R[0x112] = (uint8_t)dv; R[0x113] = nonrat ? 1 : 0;
    double *T = malloc(sizeof(double) * (size_t)(nku + nkv)); if (!T) { free(R); return NULL; }
    memcpy(T, Ku, sizeof(double) * nku); memcpy(T + nku, Kv, sizeof(double) * nkv);
    check_knots(nku, du, T); check_knots(nkv, dv, T + nku);    /* put_surf_in_db (after the range is taken from the knots below) */
    wdd(S + 8, Ku[0]); wdd(S + 0x10, Ku[nku - 1]); wdd(S + 0x18, Kv[0]); wdd(S + 0x20, Kv[nkv - 1]);
    uint8_t *O = R + 0x118; for (int i = 0; i < nku + nkv; i++) wdd(O + 8 * i, T[i]);
    O += 8 * (size_t)(nku + nkv); for (int i = 0; i < vpp * ncu * ncv; i++) wdd(O + 8 * i, P[i]);
    free(T);
    return R;
}

/* ------------------------------------------------------------------------------------------------ kind 1 */
/* FUN_18026c490 (decomp/surfloft/loft4.c l.139): the n x n power -> Bezier matrix (row, column; literal, including the anti-diagonal fill) */
static void bez_mat(int n, double *m) {
    memset(m, 0, sizeof(double) * n * n);
    if (n == 2) { m[0] = 1; m[2] = 1; m[3] = 1; return; }
    if (n == 3) { m[0] = 1; m[3] = 1; m[4] = 0.5; m[6] = m[7] = m[8] = 1; return; }
    if (n == 4) { m[0] = 1; m[4] = 1; m[5] = 1.0 / 3.0; m[8] = 1; m[9] = 2.0 / 3.0; m[10] = 1.0 / 3.0; m[12] = m[13] = m[14] = m[15] = 1; return; }
    short sVar8 = 1, sVar9 = 1;
    for (int r = 0; r < n; r++) { if (sVar8 < n) { short t = sVar9; for (int k = 0; k < n - sVar8; k++) m[t++] = 0; } sVar9 = (short)(sVar9 + n + 1); sVar8++; }
    int iVar4 = n, u1 = 0, u6 = 0;
    do {
        short a = (short)u6; short b = (short)((n + 1) * a), c = (short)((n - 1) * (n + 1) - b);
        if (u1 < iVar4) {
            short d = a;
            do {
                if (a == 0) { m[c] = 1; m[b] = 1; }
                else { double v = ((double)(short)(d + (1 - a)) * m[b - 1]) / (double)iVar4; m[c] = v; m[b] = v; }
                d++; b = (short)(b + n); c--;
            } while (d < iVar4);
        }
        u6 = (short)(a + 1); u1 = (short)(a + 1); iVar4 = n - u1;
    } while (u1 <= iVar4);
}
/* FUN_180265e70 (core_nurbs.c l.4020): a parametric surface: per patch the power coefficients B_u M B_v^T, shared patch edges overwritten by the later patch, non-rational; knots: 0, each
   patch boundary repeated degree times, nu (nv) */
static uint8_t *conv_pps(const Surf *s, uint32_t *rn, const char **why) {
    const uint8_t *r = s->rec;
    if (s->n < 0x110) { *why = "kind 1 record too short"; return NULL; }
    int nu = r32(r + 0x100), nv = r32(r + 0x104), ou = r[0x108], ov = r[0x109];
    if (nu < 1 || nv < 1 || ou < 2 || ov < 2 || ou > 26 || ov > 26 || 0x110 + 8 * (size_t)ou * ov * 3 * nu * nv > s->n) { *why = "kind 1 surface: bad patch counts or orders"; return NULL; }
    int du = ou - 1, dv = ov - 1, ncu = nu * du + 1, ncv = nv * dv + 1, nku = (nu + 1) * du + 2, nkv = (nv + 1) * dv + 2;
    {   /* FUN_180269120 / check_for_realloc (needs 1 - (int)(T / -48.0) patches, grown to it + 0x14 by FUN_180028fe0 past CSurfDataMgr::AllocateMem's 4000), then FUN_180265e70: the knots fill up to
           get_max_n_surf_patches and fail ("Cannot make surface") unless the count after u is < max - 2 and the count after v < max. The DLL's maximum only grows in a session: 4000 here. */
        int T0 = ncv * ncu * 3 + nkv + nku, need = 1 - (int)((double)T0 / -48.0), mx = need > 4000 ? need + 0x14 : 4000, cu = 1 + (nu + 1) * du;
        if (!(cu < mx - 2) || !(cu + 2 + (nv + 1) * dv < mx)) { *why = "Cannot make surface (FUN_180265e70: the knots exceed get_max_n_surf_patches)"; return NULL; } }
    double *Ku = malloc(sizeof(double) * nku), *Kv = malloc(sizeof(double) * nkv), *P = calloc((size_t)ncu * ncv * 3, sizeof(double));
    double *Bu = malloc(sizeof(double) * ou * ou), *Bv = malloc(sizeof(double) * ov * ov), *M = malloc(sizeof(double) * ou * ov), *T = malloc(sizeof(double) * ou * ov), *O = malloc(sizeof(double) * ou * ov);
    uint8_t *R = NULL;
    if (!Ku || !Kv || !P || !Bu || !Bv || !M || !T || !O) { *why = "out of memory"; goto done; }
    int q = 0; Ku[q++] = 0; for (int i = 0; i <= nu; i++) for (int j = 0; j < du; j++) Ku[q++] = i; Ku[q++] = nu;
    q = 0; Kv[q++] = 0; for (int i = 0; i <= nv; i++) for (int j = 0; j < dv; j++) Kv[q++] = i; Kv[q++] = nv;
    bez_mat(ou, Bu); bez_mat(ov, Bv);
    const uint8_t *C = r + 0x110;
    for (int j = 0; j < nv; j++) for (int i = 0; i < nu; i++) for (int k = 0; k < 3; k++) {
        const uint8_t *pc = C + 8 * (3 * (size_t)ou * ov * ((size_t)j * nu + i));
        for (int a = 0; a < ou; a++) for (int b = 0; b < ov; b++) M[a * ov + b] = rd(pc + 8 * (3 * ((size_t)b * ou + a) + k));
        for (int a = 0; a < ou; a++) for (int b = 0; b < ov; b++) { double x = 0; for (int e = 0; e < ou; e++) x += Bu[a * ou + e] * M[e * ov + b]; T[a * ov + b] = x; }
        for (int a = 0; a < ou; a++) for (int b = 0; b < ov; b++) { double x = 0; for (int e = 0; e < ov; e++) x += T[a * ov + e] * Bv[b * ov + e]; O[a * ov + b] = x; }
        for (int a = 0; a < ou; a++) for (int b = 0; b < ov; b++) P[3 * (((size_t)j * dv + b) * ncu + (i * du + a)) + k] = O[a * ov + b];
    }
    R = make_nbs(s, nku, nkv, ncu, ncv, du, dv, 3, 1, Ku, Kv, P, rn);
done:
    free(Ku); free(Kv); free(P); free(Bu); free(Bv); free(M); free(T); free(O);
    return R;
}

/* ------------------------------------------------------------------------------------------------ curves */
typedef struct { const CurveRec *c; const Ent *en; double t1, t2; } Crv;
static const Ent *ent_of(const McamFile *f, int64_t eptr) { for (int i = 0; i < f->nents; i++) if (f->ents[i].type && f->ents[i].eptr == eptr) return &f->ents[i]; return NULL; }
static int crv_get(const McamFile *f, int64_t eptr, Crv *o) { o->c = mcs_curve(f, eptr); o->en = ent_of(f, eptr); if (!o->c || !o->en) return -1; mcs_spline_t1t2(o->c, &o->t1, &o->t2); return 0; }
/* eval_curve (0x1803fcd10): arcs angle = start + t sweep (view_to_world, the frame vm / world XY); o[0] P, o[1] the derivative, o[2] the second */
static int crv_d(const Crv *c, double t, int nd, double o[3][3]) {
    uint32_t cl = c->c->cls;
    if (cl == 8 || cl == 0x80) return mcs_spline_eval_d(c->c, t, nd, o);
    const Ent *e = c->en; memset(o, 0, 9 * sizeof(double));
    if (cl == 2) { double a[3] = { e->a.x, e->a.y, e->a.z }, b[3] = { e->b.x, e->b.y, e->b.z }; for (int k = 0; k < 3; k++) { o[0][k] = a[k] + t * (b[k] - a[k]); o[1][k] = b[k] - a[k]; } return 0; }
    if (cl == 4) {
        double ang = e->a0 + t * e->sw, rc = e->r * cos(ang), rs = e->r * sin(ang);
        double m0[3] = { 1, 0, 0 }, m1[3] = { 0, 1, 0 }; if (e->vw) { memcpy(m0, e->vm, 24); memcpy(m1, e->vm + 3, 24); }
        for (int k = 0; k < 3; k++) {
            double ck = k == 0 ? e->c.x : k == 1 ? e->c.y : e->c.z;
            o[0][k] = ck + rc * m0[k] + rs * m1[k];
            o[1][k] = e->sw * (-rs * m0[k] + rc * m1[k]);
            o[2][k] = -e->sw * e->sw * (rc * m0[k] + rs * m1[k]);
        }
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------------------------------------ kind 6 */
/* FUN_180412490 (decomp/surfkinds/evalsurf.c l.329) modes 0 / 1: P and Su. a = -N / |N|; X = a x C'; Dir = unit X (v_z_3d +Z when every component < 1e-12), dDir = (Y - (Dir . Y) Dir) / |X| with
   Y = a x C''; |d| >= 1e12 (bVar2 NAN / bVar3 / bVar4): P = C + v L Dir, L = |N| (negated when -d > 0); else P = C + v (N + (-d |N|) Dir) */
static int draft_ev(const uint8_t *D, const Crv *c, double u, double v, int mode, double P[3], double Su[3]) {
    double o[3][3]; if (crv_d(c, u, mode + 1, o)) return -1;
    double N[3] = { rd(D), rd(D + 8), rd(D + 16) }, L = len(N), d = rd(D + 0x18), md = -d, a[3] = { -N[0] / L, -N[1] / L, -N[2] / L }, X[3], Dir[3], Y[3] = { 0, 0, 0 }, dDir[3] = { 0, 0, 0 };
    cross(a, o[1], X);
    int big = isnan(fabs(md)) || fabs(md) == 1e12 || fabs(md) > 1e12, degen = !(fabs(X[0]) >= 1e-12 || fabs(X[1]) >= 1e-12 || fabs(X[2]) >= 1e-12);
    double off = 0; if (big) { if (0 < md) L = -L; } else off = md * L;
    if (!degen) { double m = len(X); Dir[0] = X[0] / m; Dir[1] = X[1] / m; Dir[2] = X[2] / m; if (mode) { cross(a, o[2], Y); double dp = dot(Dir, Y); for (int k = 0; k < 3; k++) dDir[k] = (Y[k] - dp * Dir[k]) / m; } }
    else { Dir[0] = 0; Dir[1] = 0; Dir[2] = 1; dDir[0] = 0; dDir[1] = 0; dDir[2] = 1; }
    for (int k = 0; k < 3; k++) {
        if (big) { P[k] = L * v * Dir[k] + o[0][k]; if (mode) Su[k] = L * v * dDir[k] + o[1][k]; }
        else { P[k] = (off * Dir[k] + N[k]) * v + o[0][k]; if (mode) Su[k] = (off * dDir[k]) * v + o[1][k]; }
    }
    return 0;
}
/* pt_tan_to_sp_coefs (0x180665510, h = 1): the cubic's power coefficients c[0..11] (per power, xyz) */
static void hermite(const double a[3], const double b[3], const double t0[3], const double t1[3], double c[12]) {
    for (int k = 0; k < 3; k++) { c[k] = a[k]; c[3 + k] = t0[k]; c[6 + k] = 3 * (b[k] - a[k]) - 2 * t0[k] - t1[k]; c[9 + k] = 2 * (a[k] - b[k]) + t0[k] + t1[k]; }
}
/* eval_poly / eval_poly_deriv (0x1805eb520 / 0x1805eb630), 3 values, order 4 */
static void poly_at(const double c[12], double s, double o[3]) { for (int k = 0; k < 3; k++) o[k] = ((c[9 + k] * s + c[6 + k]) * s + c[3 + k]) * s + c[k]; }
static void polyd_at(const double c[12], double s, double o[3]) { for (int k = 0; k < 3; k++) { double d1 = 0.0, d0 = c[9 + k]; for (int j = 2; j >= 0; j--) { d1 = s * d1 + d0; d0 = s * d0 + c[3 * j + k]; } o[k] = d1; } }   /* eval_poly_deriv's synthetic division */
/* proj_pt_on_coeff (decomp via stn dump): the Newton steps from s = 0.5, clamped twice at an end, 100 steps at most */
static void proj_coef(const double p[3], const double c[12], double *s, double *dist, double tol) {
    int b3 = 0, b2 = 0; *s = 0.5;
    for (int i = 0; ; ) {
        double q[3]; poly_at(c, *s, q); double e[3] = { p[0] - q[0], p[1] - q[1], p[2] - q[2] }; *dist = sqrt(e[1] * e[1] + e[0] * e[0] + e[2] * e[2]);   /* proj_pt_on_coeff's order of operations (non-converging Newton runs depend on it) */
        if (*dist < tol) return;
        double D[3]; polyd_at(c, *s, D); double g = D[1] * e[1] + D[0] * e[0] + D[2] * e[2];
        if (fabs(g) < 2e-07) return;
        double l = sqrt(D[1] * D[1] + D[0] * D[0] + D[2] * D[2]); double ns = g / (l * l) + *s; *s = ns;
        if (ns <= 1.0) {
            if (ns < 0.0) { b2 = 0; *s = 0.0; if (b3) { double t = 0.0; poly_at(c, t, q); double e2[3] = { p[0] - q[0], p[1] - q[1], p[2] - q[2] }; *dist = len(e2); return; } b3 = 1; }
        } else {
            b3 = 0; *s = 1.0; if (b2) { double t = 1.0; poly_at(c, t, q); double e2[3] = { p[0] - q[0], p[1] - q[1], p[2] - q[2] }; *dist = len(e2); return; } b2 = 1;
        }
        i++; if (99 < i) return;
    }
}
/* FUN_180266ed0 (core_nurbs.c l.3556): the surface between the curve (v = 0) and its copy moved by N plus an offset (v = 1): line and arc with |d| > 1e-5, any curve otherwise. */
static uint8_t *draft_exact(const McamFile *f, const Surf *s, const Crv *c, double tol, uint32_t *rn, const char **why) {
    const uint8_t *D = s->rec + 0x100; const Ent *en = c->en; uint32_t cl = c->c->cls;
    double N[3] = { rd(D), rd(D + 8), rd(D + 16) }, d = rd(D + 0x18);
    int nk, ncp, deg, dim, rat; double *K = NULL, *P = NULL, *K2 = NULL, *P2 = NULL;
    if (mcsb_ent_nb(f, en, tol, &nk, &ncp, &deg, &dim, &rat, &K, &P)) { *why = "kind 6: the curve could not be converted"; return NULL; }
    int vpp = dim, nonrat = !rat, ok = 0;
    double *Pp = malloc(sizeof(double) * (size_t)vpp * ncp * 2); uint8_t *R = NULL;
    if (!Pp) { *why = "out of memory"; goto done; }
    memcpy(Pp, P, sizeof(double) * (size_t)vpp * ncp);
    if (fabs(d) > 1e-5) {
        double L = len(N), md = -d, nh[3] = { N[0] / L, N[1] / L, N[2] / L };
        int nan_ = isnan(fabs(md)), bg3 = fabs(md) == 1e12, bg4 = fabs(md) > 1e12, big = nan_ || bg3 || bg4;
        double dv6 = 0, dv5 = L;
        if (big) { if (0.0 <= md && md != 0.0) dv5 = -dv5; } else dv6 = md * L;
        double o[3][3]; if (crv_d(c, c->t1, 1, o)) { *why = "kind 6: the curve could not be evaluated"; goto done; }
        double am[3] = { -nh[0], -nh[1], -nh[2] }, Dir[3]; cross(am, o[1], Dir);
        int degen = !(fabs(Dir[0]) >= 1e-12 || fabs(Dir[1]) >= 1e-12 || fabs(Dir[2]) >= 1e-12);   /* parallel to the draft direction at the start: Dir = v_z_3d (the whole vector, oracle) */
        if (degen) { Dir[0] = 0; Dir[1] = 0; Dir[2] = 1; } else unit3(Dir);
        if (cl == 2) {
            V3 a = en->a, b = en->b; double pa[3] = { a.x, a.y, a.z }, pb[3] = { b.x, b.y, b.z };
            for (int i = 0; i < 3; i++) {
                if (big) { Pp[vpp * ncp + i] = dv5 * Dir[i] + pa[i]; Pp[vpp * ncp + 3 + i] = dv5 * Dir[i] + pb[i]; }
                else { Pp[vpp * ncp + i] = dv6 * Dir[i] + N[i] + pa[i]; Pp[vpp * ncp + 3 + i] = pb[i] + N[i] + dv6 * Dir[i]; }
            }
        } else if (cl == 4) {
            double center_to[3] = { en->c.x - o[0][0], en->c.y - o[0][1], en->c.z - o[0][2] };   /* view_to_world(centre) - C(t1); v_z_3d when degenerate */
            if (degen) { center_to[0] = 0; center_to[1] = 0; center_to[2] = 1; }
            if (bg4 || bg3) dv6 = dv5;
            if (1e-05 < dot(center_to, Dir)) dv6 = -dv6;
            Ent e2 = *en; e2.r = en->r + dv6;
            if (!big) { double kz[3] = { 0, 0, 1 }; if (en->vw) memcpy(kz, en->vm + 6, 24); double h = dot(N, kz); e2.c.x += kz[0] * h; e2.c.y += kz[1] * h; e2.c.z += kz[2] * h; }
            int a2, b2, c2, dd2, r2;
            if (mcsb_ent_nb(f, &e2, tol, &a2, &b2, &c2, &dd2, &r2, &K2, &P2) || b2 != ncp || dd2 != dim) { *why = "kind 6: the offset arc could not be converted"; goto done; }
            memcpy(Pp + vpp * ncp, P2, sizeof(double) * (size_t)vpp * ncp);
        } else { *why = "kind 6: a spline with an offset takes the other path"; goto done; }
    } else {
        for (int i = 0; i < ncp; i++) {
            const double *q = P + vpp * i; double *t = Pp + vpp * ncp + vpp * i; int o = 0; double w = 1.0;
            if (!nonrat) { w = q[0]; t[o++] = w; }
            for (int k = 0; k < 3; k++) { double x = q[o]; t[o] = (nonrat || fabs(w - 1.0) <= 1e-05) ? x + N[k] : (x / w + N[k]) * w; o++; }
        }
    }
    {   double Kv[4] = { 0, 0, 1, 1 };
        R = make_nbs(s, nk, 4, ncp, 2, deg, 1, vpp, nonrat, K, Kv, Pp, rn);
        if (R && (cl == 8 || cl == 0x80)) { wdd(R + 0x68 + 8, c->t1); wdd(R + 0x68 + 0x10, c->t2); }   /* create_nurbsurf_from_surf: kind 6 with FUN_1802641d0 true */
        if (R) ok = 1; }
done:
    free(K); free(P); free(K2); free(P2); free(Pp);
    if (!ok && !*why) *why = "kind 6: conversion failed";
    return R;
}
/* FUN_1802699a0: the span ends in the curve's range; a span halves until the middle point at v = 0 and 1 is within 0.0001 (inch) / 0.0025 (mm) of the cubic through its ends */
static int draft_breaks(const McamFile *f, const uint8_t *D, const Crv *c, double tol, double *B, int *pcnt, const char **why) {
    double err = mcam_is_metric(f) ? 0.0025 : 0.0001;    /* IsMetric */
    int cnt = 0;
    double t = c->t1, t2 = c->t2;
    if (t < t2) for (;;) {
        double u1 = t2;
        for (int iv = 0; iv < 2; iv++) {
            double v = iv, P0[3], S0[3], P1[3], S1[3], Pm[3], cf[12], sp, dist;
            if (draft_ev(D, c, t, v, 1, P0, S0)) { *why = "kind 6: the curve could not be evaluated"; return -1; }
            unit3(S0);
            for (;;) {
                if (draft_ev(D, c, u1, v, 1, P1, S1)) { *why = "kind 6: the curve could not be evaluated"; return -1; }
                unit3(S1);
                double e[3] = { P1[0] - P0[0], P1[1] - P0[1], P1[2] - P0[2] }, L = len(e);
                double T0[3] = { S0[0] * L, S0[1] * L, S0[2] * L }, T1[3] = { S1[0] * L, S1[1] * L, S1[2] * L };
                hermite(P0, P1, T0, T1, cf);
                if (draft_ev(D, c, (u1 + t) * 0.5, v, 0, Pm, S1)) { *why = "kind 6: the curve could not be evaluated"; return -1; }
                proj_coef(Pm, cf, &sp, &dist, tol);
                if ((dist < err && 0.0 <= sp && sp <= 1.0) || (fabs(u1 - t) <= 1e-12 && fabs(u1 - t) != 1e-12)) break;
                u1 = u1 * 0.5 + t * 0.5;
            }
        }
        if (cnt >= 506) { *why = "kind 6: more than 506 spans (the stack buffer of FUN_1802699a0)"; return -1; }
        B[cnt++] = u1; t = u1; if (!(u1 < t2)) break;
    }
    *pcnt = cnt;
    return 0;
}
/* FUN_1802647b0 + FUN_18026a290: bicubic in u, linear in v, non-rational */
static uint8_t *draft_general(const McamFile *f, const Surf *s, const Crv *c, double tol, uint32_t *rn, const char **why) {
    const uint8_t *D = s->rec + 0x100; double B[506]; int cnt = 0;   /* local_fd8 (0xfd8 bytes) */
    if (draft_breaks(f, D, c, tol, B, &cnt, why)) return NULL;
    if (cnt == 0) { *why = "kind 6: empty parameter range"; return NULL; }
    int ncu = cnt * 3 + 1, nku = cnt * 3 + 5;
    double *Ku = malloc(sizeof(double) * nku), *P = calloc((size_t)ncu * 2 * 3, sizeof(double)), Kv[4] = { 0, 0, 1, 1 }; uint8_t *R = NULL;
    if (!Ku || !P) { *why = "out of memory"; goto done; }
    { int q = 0; Ku[q++] = 0; for (int i = 0; i <= cnt; i++) for (int j = 0; j < 3; j++) Ku[q++] = i; Ku[q++] = cnt; }
    for (int iv = 0; iv < 2; iv++) {   /* FUN_18026a290(v), row iv */
        double v = iv, P0[3], T0[3], P1[3], T1[3], cf[12];
        if (draft_ev(D, c, c->t1, v, 1, P0, T0)) { *why = "kind 6: the curve could not be evaluated"; goto done; }
        unit3(T0);
        for (int i = 0; i < cnt; i++) {
            if (draft_ev(D, c, B[i], v, 1, P1, T1)) { *why = "kind 6: the curve could not be evaluated"; goto done; }
            unit3(T1);
            double e[3] = { P1[0] - P0[0], P1[1] - P0[1], P1[2] - P0[2] }, L = len(e);
            double A[3] = { T0[0] * L, T0[1] * L, T0[2] * L }, B3[3] = { T1[0] * L, T1[1] * L, T1[2] * L };
            hermite(P0, P1, A, B3, cf);
            /* convert_coeffs_to_cpts (mcspline_chain.c), shared ends overwritten by the later segment (FUN_180261c50) */
            double *o = P + 3 * ((size_t)iv * ncu + (size_t)i * 3);
            for (int k = 0; k < 3; k++) {
                o[k] = cf[k]; o[3 + k] = cf[3 + k] / 3.0 + cf[k]; o[6 + k] = (cf[3 + k] * 2.0 + cf[6 + k]) / 3.0 + cf[k]; o[9 + k] = cf[3 + k] + cf[k] + cf[6 + k] + cf[9 + k];
            }
            memcpy(P0, P1, 24); memcpy(T0, T1, 24);
        }
    }
    R = make_nbs(s, nku, 4, ncu, 2, 3, 1, 3, 1, Ku, Kv, P, rn);
done:
    free(Ku); free(P);
    return R;
}
/* FUN_180266ca0 case 6, FUN_1802641d0: the curve's plane test */
static uint8_t *conv_draft(const McamFile *f, const Surf *s, double tol, uint32_t *rn, const char **why) {
    if (s->n < 0x128) { *why = "kind 6 record too short"; return NULL; }
    const uint8_t *D = s->rec + 0x100; Crv c; if (crv_get(f, r64(D + 0x20), &c)) { *why = "kind 6: the curve was not found"; return NULL; }
    double N[3] = { rd(D), rd(D + 8), rd(D + 16) }, d = rd(D + 0x18); int exact = 1;
    uint32_t cl = c.c->cls;
    if (cl != 2 && cl != 4 && cl != 8 && cl != 0x80) { *why = "kind 6: unsupported curve class"; return NULL; }
    if (fabs(d) > 1e-5) {
        if (cl == 8 || cl == 0x80) exact = 0;
        else if (cl == 4) { double nh[3] = { N[0], N[1], N[2] }, z[3] = { 0, 0, 1 }; unit3(nh); if (c.en->vw) memcpy(z, c.en->vm + 6, 24); if (!(0.99999 <= fabs(dot(nh, z)))) exact = 0; }
    }
    return exact ? draft_exact(f, s, &c, tol, rn, why) : draft_general(f, s, &c, tol, rn, why);
}

/* ------------------------------------------------------------------------------------------------ kind 7 */
/* FUN_180114560: Rodrigues rotation of x about the unit n */
static void rotv(const double x[3], const double n[3], double an, double o[3]) {
    double dn = dot(n, x), p[3], q[3], c = cos(an), s = sin(an);
    for (int k = 0; k < 3; k++) p[k] = x[k] - dn * n[k];
    cross(n, p, q);
    for (int k = 0; k < 3; k++) o[k] = dn * n[k] + c * p[k] + s * q[k];
}
/* FUN_18041c730 mode 1, the u derivative: FUN_18041ced0 with the sweep from asin |a x b| / (|a| |b|) (clamped at 0.999999 to +-pi/2); below |a x b| 1e-12 the C0 derivative */
static int sweep_su(const Crv C[3], double u, double v, double Su[3]) {
    double o0[3][3], o1[3][3], o2[3][3];
    if (crv_d(&C[0], u, 1, o0) || crv_d(&C[1], u, 1, o1) || crv_d(&C[2], u, 1, o2)) return -1;
    double a[3] = { o1[0][0] - o0[0][0], o1[0][1] - o0[0][1], o1[0][2] - o0[0][2] }, b[3] = { o2[0][0] - o0[0][0], o2[0][1] - o0[0][1], o2[0][2] - o0[0][2] }, n[3];
    cross(a, b, n); double m = len(n);
    if (!(1e-12 <= m)) { memcpy(Su, o0[1], 24); return 0; }
    double nh[3] = { n[0] / m, n[1] / m, n[2] / m }, sina = m / sqrt(dot(b, b) * dot(a, a)), th;
    th = sina <= 0.999999 ? (-0.999999 <= sina ? asin(sina) : -1.5707963267948966) : 1.5707963267948966;
    double d1[3] = { o1[1][0] - o0[1][0], o1[1][1] - o0[1][1], o1[1][2] - o0[1][2] }, d2[3] = { o2[1][0] - o0[1][0], o2[1][1] - o0[1][1], o2[1][2] - o0[1][2] }, r1[3], r2[3];
    rotv(d1, nh, th * v, r1); rotv(d2, nh, (v - 1.0) * th, r2);
    for (int k = 0; k < 3; k++) Su[k] = o0[1][k] + r1[k] * (1.0 - v) + r2[k] * v;
    return 0;
}
/* the record of a NURBS surface whose knots and homogeneous net follow; FUN_180264310: the (min, max) of 1 / max(|k1|, |k2|) over the 20 x 4 grid at half-cell offsets */
static void rad_range(const uint8_t *R, uint32_t n, double *lo, double *hi) {
    int nku = r32(R + 0x100), nkv = r32(R + 0x104); const uint8_t *K = R + 0x118;
    double du = (rd(K + 8 * (size_t)(nku - 1)) - rd(K)) / 20.0, dv = (rd(K + 8 * (size_t)(nku + nkv - 1)) - rd(K + 8 * (size_t)nku)) * 0.25, u = du * 0.5 + rd(K), v0 = rd(K + 8 * (size_t)nku);
    *lo = 1e38; *hi = 0.0;
    for (int i = 0; i < 20; i++) {
        double v = dv * 0.5 + v0;
        for (int j = 0; j < 4; j++) {
            double o[6][3], k1 = 0, k2 = 0;
            if (!mcsb_nurbs_ders(R, n, u, v, o)) mcsb_curv_geom(o, &k1, &k2);
            double x = fabs(k1) <= fabs(k2) ? k2 : k1;
            if (fabs(x) < 1e38) { x = 1.0 / fabs(x); if (*hi <= x && x != *hi) *hi = x; if (x < *lo) *lo = x; }
            v += dv;
        }
        u += du;
    }
}
/* the cubic in u, quadratic in v rational net: C0, C1, C2 at the spline's control parameters */
static uint8_t *sweep_a(const McamFile *f, const Surf *s, const Crv C[3], double tol, uint32_t *rn, const char **why) {
    int ns = r32(C[0].c->rec + 0xb0), nk1, ncp1, deg1, dim1, rat1, nk2, ncp2, deg2, dim2, rat2; double *K1 = NULL, *P1 = NULL, *K2 = NULL, *P2 = NULL, *Ku = NULL, *Net = NULL, Kv[6] = { 0, 0, 0, 1, 1, 1 }; uint8_t *R = NULL;
    int ncu = ns * 3 + 1, nku = ns * 3 + 5;
    if (ns < 1) { *why = "kind 7: empty spline"; return NULL; }
    if (mcsb_ent_nb(f, C[1].en, tol, &nk1, &ncp1, &deg1, &dim1, &rat1, &K1, &P1) || mcsb_ent_nb(f, C[2].en, tol, &nk2, &ncp2, &deg2, &dim2, &rat2, &K2, &P2) || ncp1 != ncu || ncp2 != ncu || dim1 != 3 || dim2 != 3) { *why = "kind 7: the side curves are not cubic splines of the base curve's segments"; goto done; }
    Ku = malloc(sizeof(double) * nku); Net = calloc((size_t)ncu * 3 * 4, sizeof(double));
    if (!Ku || !Net) { *why = "out of memory"; goto done; }
    { int q = 0; Ku[q++] = 0; for (int i = 0; i <= ns; i++) { Ku[q++] = i; Ku[q++] = i; Ku[q++] = i; } Ku[q++] = ns; }
    { double sub = 0.0, whole = 0.0;
      for (int i = 0; i < ncu; i++) {
        double o[3][3], t = sub + whole; if (crv_d(&C[0], t, 0, o)) { *why = "kind 7: the curve could not be evaluated"; goto done; }
        const double *c1 = P1 + 3 * i, *c2 = P2 + 3 * i; double v1[3] = { c1[0] - o[0][0], c1[1] - o[0][1], c1[2] - o[0][2] }, v2[3] = { c2[0] - o[0][0], c2[1] - o[0][1], c2[2] - o[0][2] }, n[3];
        cross(v1, v2, n);
        double *C0 = Net + 4 * i, *C1m = Net + 4 * ((size_t)ncu + i), *C2p = Net + 4 * ((size_t)2 * ncu + i);
        if (len(v1) < 1e-05 || len(v2) < 1e-05) { C0[0] = C1m[0] = C2p[0] = 1.0; for (int k = 0; k < 3; k++) C0[1 + k] = C1m[1 + k] = C2p[1 + k] = o[0][k]; }
        else {
            if (len(n) < 1e-05) { double d[3][3]; if (crv_d(&C[0], t, 1, d)) { *why = "kind 7: the curve could not be evaluated"; goto done; } memcpy(n, d[1], 24); }
            double l = len(n); if (l != 0.0) for (int k = 0; k < 3; k++) n[k] /= l; else n[0] = n[1] = n[2] = 0.0;
            double M[9]; if (!mcsb_view_k(n, M)) { *why = "kind 7: make_view_k failed"; goto done; }
            double cx = M[0] * o[0][0] + M[1] * o[0][1] + M[2] * o[0][2], cy = M[3] * o[0][0] + M[4] * o[0][1] + M[5] * o[0][2];
            double ax = M[0] * c1[0] + M[1] * c1[1] + M[2] * c1[2] - cx, ay = M[3] * c1[0] + M[4] * c1[1] + M[5] * c1[2] - cy;
            double h1 = M[6] * c1[0] + M[7] * c1[1] + M[8] * c1[2], r = sqrt(ax * ax + ay * ay), st = 0, sw = 0;
            if (1e-12 <= ax * ax + ay * ay) { st = atan2(ay, ax); if (st < 0) st += 2 * M_PI; }
            double bx = M[0] * c2[0] + M[1] * c2[1] + M[2] * c2[2] - cx, by = M[3] * c2[0] + M[4] * c2[1] + M[5] * c2[2] - cy, h2 = M[6] * c2[0] + M[7] * c2[1] + M[8] * c2[2];
            if (1e-12 <= by * by + bx * bx) { sw = atan2(by, bx); if (sw < 0) sw += 2 * M_PI; }
            sw -= st; if (sw <= M_PI) { if (sw < -M_PI) sw += 2 * M_PI; } else sw -= 2 * M_PI;
            double P[36], Kt[12]; int ncv, nkv2; mcsb_arc2d_nb(cx, cy, r, st, sw, (h1 + h2) * 0.5, tol, P, Kt, &ncv, &nkv2);
            C0[0] = 1.0; for (int k = 0; k < 3; k++) C0[1 + k] = c1[k];
            int mi = (ncv - 1) / 2; C1m[0] = P[4 * mi]; double x = P[4 * mi + 1], y = P[4 * mi + 2], z = P[4 * mi + 3];
            C1m[1] = M[0] * x + M[3] * y + M[6] * z; C1m[2] = M[1] * x + M[4] * y + M[7] * z; C1m[3] = M[2] * x + M[5] * y + M[8] * z;
            C2p[0] = 1.0; for (int k = 0; k < 3; k++) C2p[1 + k] = c2[k];
        }
        if (i % 3 == 2) { whole += 1.0; sub = 0.0; } else sub += 0.3333333333;   /* FUN_180264a30: the literal */
      } }
    for (size_t q = 0; q < (size_t)ncu * 3; q++) { double *x = Net + 4 * q; x[1] *= x[0]; x[2] *= x[0]; x[3] *= x[0]; }    /* nbsrat_3d_to_4d */
    R = make_nbs(s, nku, 6, ncu, 3, 3, 2, 4, 0, Ku, Kv, Net, rn);
done:
    free(K1); free(P1); free(K2); free(P2); free(Ku); free(Net);
    return R;
}
/* FUN_180269180: one column: the arc from C1 to C2 about C0 at t in the plane of the three (M^T of convert_2darc_to_nurbdata's points; the matrix step is hidden in the decompile) */
static int sweep_col(const Crv C[3], double t, double tol, double Pts[3][4], int *ncp) {
    double o0[3][3], o1[3][3], o2[3][3];
    if (crv_d(&C[0], t, 1, o0) || crv_d(&C[1], t, 1, o1) || crv_d(&C[2], t, 1, o2)) return -1;
    double v1[3] = { o1[0][0] - o0[0][0], o1[0][1] - o0[0][1], o1[0][2] - o0[0][2] }, v2[3] = { o2[0][0] - o0[0][0], o2[0][1] - o0[0][1], o2[0][2] - o0[0][2] }, n[3];
    if (len(v1) < 1e-05 || len(v2) < 1e-05) return 1;   /* FUN_180269180's degenerate branch does not set *param_3: the caller compares an unset count */
    cross(v1, v2, n);
    if (len(n) < 1e-05) memcpy(n, o0[1], 24);
    double l = len(n); if (l != 0.0) for (int k = 0; k < 3; k++) n[k] /= l; else n[0] = n[1] = n[2] = 0.0;
    double M[9]; if (!mcsb_view_k(n, M)) return -1;
    double cx = M[0] * o0[0][0] + M[1] * o0[0][1] + M[2] * o0[0][2], cy = M[3] * o0[0][0] + M[4] * o0[0][1] + M[5] * o0[0][2];
    double ax = M[0] * o1[0][0] + M[1] * o1[0][1] + M[2] * o1[0][2] - cx, ay = M[3] * o1[0][0] + M[4] * o1[0][1] + M[5] * o1[0][2] - cy, h1 = M[6] * o1[0][0] + M[7] * o1[0][1] + M[8] * o1[0][2], r = sqrt(ax * ax + ay * ay), st = 0, en = 0;
    if (1e-12 <= ax * ax + ay * ay) { st = atan2(ay, ax); if (st < 0) st += 2 * M_PI; }
    double bx = M[0] * o2[0][0] + M[1] * o2[0][1] + M[2] * o2[0][2] - cx, by = M[3] * o2[0][0] + M[4] * o2[0][1] + M[5] * o2[0][2] - cy, h2 = M[6] * o2[0][0] + M[7] * o2[0][1] + M[8] * o2[0][2];
    if (1e-12 <= bx * bx + by * by) { en = atan2(by, bx); if (en < 0) en += 2 * M_PI; }
    double sw = en - st; if (sw <= M_PI) { if (sw < -M_PI) sw += 2 * M_PI; } else sw -= 2 * M_PI;
    double P[36], Kt[12]; int nkv; mcsb_arc2d_nb(cx, cy, r, st, sw, (h2 + h1) * 0.5, tol, P, Kt, ncp, &nkv);
    if (*ncp != 3) return 0;   /* the caller refuses it */
    for (int i = 0; i < 3; i++) { Pts[i][0] = P[4 * i]; double x = P[4 * i + 1], y = P[4 * i + 2], z = P[4 * i + 3]; Pts[i][1] = M[0] * x + M[3] * y + M[6] * z; Pts[i][2] = M[1] * x + M[4] * y + M[7] * z; Pts[i][3] = M[2] * x + M[5] * y + M[8] * z; }
    return 0;
}
/* FUN_18026acf0: the Bezier segments between the base's parameter breaks with the columns' arcs, the tangents along u from FUN_18041c730 (FUN_180264510 at a closed seam) */
static uint8_t *sweep_b(const McamFile *f, const Surf *s, const Crv C[3], double tol, uint32_t *rn, const char **why) {
    int nk, ncp, deg, dim, rat, ns = r32(C[0].c->rec + 0xb0); double *K = NULL, *Pc = NULL, *Net = NULL, Kv[6] = { 0, 0, 0, 1, 1, 1 }; uint8_t *R = NULL;
    if (mcsb_ent_nb(f, C[0].en, tol, &nk, &ncp, &deg, &dim, &rat, &K, &Pc)) { *why = "kind 7: the base curve could not be converted"; return NULL; }
    Net = calloc((size_t)ncp * 3 * 4, sizeof(double));
    if (!Net) { *why = "out of memory"; goto done; }
    { V3 e0 = C[0].en->a, e1 = C[0].en->b; int closed = sqrt((e1.x - e0.x) * (e1.x - e0.x) + (e1.y - e0.y) * (e1.y - e0.y) + (e1.z - e0.z) * (e1.z - e0.z)) < tol;
      double prevxyz[3][3] = { { 0 } }, prevw[3] = { 0 };
      for (int i = 0; i <= ns; i++) {
        double t = (closed && i == ns) ? 0.0 : (double)i, Pts[3][4]; int n3 = 0, rc = sweep_col(C, t, tol, Pts, &n3);
        if (rc == 1) { *why = "kind 7: a degenerate column (FUN_180269180's count is unset there): not decoded"; goto done; }
        if (rc < 0) { *why = "kind 7: the curve could not be evaluated"; goto done; }
        if (n3 != 3) { *why = "kind 7: a sweep of pi or more: the decompile fails the whole conversion (FUN_18026acf0 returns -1)"; goto done; }
        double v = 0.0;
        for (int j = 0; j < 3; j++) {
            double *pp = Net + 4 * ((size_t)j * ncp + (size_t)i * 3); memcpy(pp, Pts[j], 32);
            double w = Pts[j][0], cur[3] = { Pts[j][1], Pts[j][2], Pts[j][3] };
            if (i != 0) {
                double T0[3], T1[3];
                if (!(closed && i == 1)) { if (sweep_su(C, (i - 1.0) + 1e-06, v, T0)) { *why = "kind 7: the curve could not be evaluated"; goto done; } unit3(T0); }
                else { double a[3], b[3]; if (sweep_su(C, 0, v, a) || sweep_su(C, (double)ns, v, b)) { *why = "kind 7: the curve could not be evaluated"; goto done; } unit3(a); unit3(b); for (int k = 0; k < 3; k++) T0[k] = (b[k] + a[k]) * 0.5; unit3(T0); }
                if (!(closed && i == ns)) { if (sweep_su(C, i - 1e-06, v, T1)) { *why = "kind 7: the curve could not be evaluated"; goto done; } unit3(T1); }
                else { double a[3], b[3]; if (sweep_su(C, 0, v, a) || sweep_su(C, (double)ns, v, b)) { *why = "kind 7: the curve could not be evaluated"; goto done; } unit3(a); unit3(b); for (int k = 0; k < 3; k++) T1[k] = (b[k] + a[k]) * 0.5; unit3(T1); }
                double e[3] = { prevxyz[j][0] - cur[0], prevxyz[j][1] - cur[1], prevxyz[j][2] - cur[2] }, cl = len(e) * 0.3333333333333333;
                double *a = Net + 4 * ((size_t)j * ncp + (size_t)(i * 3 - 2)), *b = Net + 4 * ((size_t)j * ncp + (size_t)(i * 3 - 1)), *pv = Net + 4 * ((size_t)j * ncp + (size_t)(i * 3 - 3));
                a[0] = (prevw[j] * 2.0 + w) * 0.3333333333333333;
                for (int k = 0; k < 3; k++) a[1 + k] = cl * T0[k] + pv[1 + k];
                b[0] = (w * 2.0 + prevw[j]) * 0.3333333333333333;
                for (int k = 0; k < 3; k++) b[1 + k] = pp[1 + k] - cl * T1[k];
            }
            memcpy(prevxyz[j], cur, 24); prevw[j] = w; v += 0.5;
        }
      } }
    for (size_t q = 0; q < (size_t)ncp * 3; q++) { double *x = Net + 4 * q; x[1] *= x[0]; x[2] *= x[0]; x[3] *= x[0]; }   /* nbsrat_3d_to_4d */
    R = make_nbs(s, nk, 6, ncp, 3, deg, 2, 4, 0, K, Kv, Net, rn);
done:
    free(K); free(Pc); free(Net);
    return R;
}
/* FUN_180266ca0 case 7 */
static uint8_t *conv_arcsweep(const McamFile *f, const Surf *s, double tol, uint32_t *rn, const char **why) {
    if (s->n < 0x118) { *why = "kind 7 record too short"; return NULL; }
    Crv C[3];
    for (int i = 0; i < 3; i++) if (crv_get(f, r64(s->rec + 0x100 + 8 * i), &C[i]) || C[i].c->cls != 8) { *why = "kind 7: a curve is not a parametric spline (class 8; other classes are not reached by the corpus and would read the wrong fields)"; return NULL; }
    uint32_t n1, n2; uint8_t *A = sweep_a(f, s, C, tol, &n1, why); if (!A) return NULL;
    uint8_t *B = sweep_b(f, s, C, tol, &n2, why); if (!B) { free(A); return NULL; }
    double lo1, hi1, lo2, hi2; rad_range(A, n1, &lo1, &hi1); rad_range(B, n2, &lo2, &hi2);
    if (fabs(hi1 - lo1) <= fabs(hi2 - lo2)) { free(B); *rn = n1; return A; }
    free(A); *rn = n2; return B;
}

/* ------------------------------------------------------------------------------------------------ eval_surf on a record */
/* eval_curve's third derivative where kind 6 mode 2 asks for it (FUN_180412490 reads eval_curve mode + 1): parametric spline: the power basis; arcs: analytic; NURBS: Piegl & Tiller A2.3 on the
   record's knots (ours, the standard algorithm, as mcs_spline_eval_d) */
static double kn(const uint8_t *r, int i) { return rd(r + 0xd8 + 8 * (size_t)i); }
static int nb_span(double t, const uint8_t *r, int nk, int p) {   /* nurb_interval (0x1805b0630) */
    if (t < kn(r, 0)) t = kn(r, 0);
    if (kn(r, nk - 1) < t) t = kn(r, nk - 1);
    int i = p + 1, hi = nk - (p + 1), s;
    while (i <= hi && kn(r, i) - 1e-10 <= t) i++;
    s = (i <= hi ? i : hi + 1) - 1;
    if (s >= 0 && s + 1 < nk && kn(r, s + 1) - kn(r, s) <= 1e-10) { int j = nk - 1; while (j > 0 && t <= kn(r, j) + 1e-10) j--; if (j > 0 || t > kn(r, 0) + 1e-10) s = j; }
    return s;
}
static int nb_d3(const CurveRec *c, double t, double o3[3]) {
    const uint8_t *r = c->rec; int nk = r32(r + 0xc8), nc = r32(r + 0xcc), p = r[0xd0], vpp = r[0xd1], rat = !((r[0xd2] >> 2) & 1), dim = vpp - (rat ? 1 : 0);
    double k0 = kn(r, p), k1 = kn(r, nc); if (t < k0) t = k0; if (t > k1) t = k1;
    int s = nb_span(t, r, nk, p); if (s < p) s = p; if (s > nc - 1) s = nc - 1;
    double left[27], right[27], ndu[27][27], a[2][27], ders[4][27]; int n = p < 3 ? p : 3;
    ndu[0][0] = 1;
    for (int j = 1; j <= p; j++) { left[j] = t - kn(r, s + 1 - j); right[j] = kn(r, s + j) - t; double saved = 0;
        for (int q = 0; q < j; q++) { ndu[j][q] = right[q + 1] + left[j - q]; double tmp = ndu[j][q] != 0 ? ndu[q][j - 1] / ndu[j][q] : 0; ndu[q][j] = saved + right[q + 1] * tmp; saved = left[j - q] * tmp; }
        ndu[j][j] = saved; }
    memset(ders, 0, sizeof ders);
    for (int j = 0; j <= p; j++) ders[0][j] = ndu[j][p];
    for (int q = 0; q <= p; q++) { int s1 = 0, s2 = 1; a[0][0] = 1;
        for (int k = 1; k <= n; k++) { double d = 0; int rk = q - k, pk = p - k;
            if (q >= k) { a[s2][0] = ndu[pk + 1][rk] != 0 ? a[s1][0] / ndu[pk + 1][rk] : 0; d = a[s2][0] * ndu[rk][pk]; }
            int j1 = rk >= -1 ? 1 : -rk, j2 = q - 1 <= pk ? k - 1 : p - q;
            for (int j = j1; j <= j2; j++) { a[s2][j] = ndu[pk + 1][rk + j] != 0 ? (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][rk + j] : 0; d += a[s2][j] * ndu[rk + j][pk]; }
            if (q <= pk) { a[s2][k] = ndu[pk + 1][q] != 0 ? -a[s1][k - 1] / ndu[pk + 1][q] : 0; d += a[s2][k] * ndu[q][pk]; }
            ders[k][q] = d; int tt = s1; s1 = s2; s2 = tt; } }
    { double m = p; for (int k = 1; k <= n; k++) { for (int j = 0; j <= p; j++) ders[k][j] *= m; m *= p - k; } }
    double A[4][4]; memset(A, 0, sizeof A); const uint8_t *C = r + 0xd8 + 8 * (size_t)nk;
    for (int j = 0; j <= p; j++) { const uint8_t *cp = C + 8 * (size_t)vpp * (s - p + j); double w = rat ? rd(cp) : 1.0, P[3] = { 0, 0, 0 };
        for (int k = 0; k < 3 && k < dim; k++) P[k] = rd(cp + 8 * ((rat ? 1 : 0) + k));
        for (int k = 0; k <= n; k++) { for (int e = 0; e < 3; e++) A[k][e] += ders[k][j] * P[e]; A[k][3] += ders[k][j] * w; } }
    double w0 = A[0][3]; if (!(fabs(w0) > 1e-300)) return -1;
    for (int e = 0; e < 3; e++) { double c0 = A[0][e] / w0, c1 = (A[1][e] - A[1][3] * c0) / w0, c2 = (A[2][e] - 2 * A[1][3] * c1 - A[2][3] * c0) / w0; o3[e] = (A[3][e] - 3 * A[1][3] * c2 - 3 * A[2][3] * c1 - A[3][3] * c0) / w0; }
    return 0;
}
static int crv_d3(const Crv *c, double t, int nd, double o[4][3]) {
    double p[3][3]; if (crv_d(c, t, nd > 2 ? 2 : nd, p)) return -1;
    memcpy(o, p, sizeof p); memset(o[3], 0, 24); if (nd < 3) return 0;
    uint32_t cl = c->c->cls;
    if (cl == 2) return 0;
    if (cl == 4) { const Ent *e = c->en; double ang = e->a0 + t * e->sw, rc = e->r * cos(ang), rs = e->r * sin(ang), m0[3] = { 1, 0, 0 }, m1[3] = { 0, 1, 0 }; if (e->vw) { memcpy(m0, e->vm, 24); memcpy(m1, e->vm + 3, 24); }
        double sw3 = e->sw * e->sw * e->sw; for (int k = 0; k < 3; k++) o[3][k] = sw3 * (rs * m0[k] - rc * m1[k]); return 0; }
    if (cl == 0x80) return nb_d3(c->c, t, o[3]);
    if (cl == 8) { const uint8_t *r = c->c->rec; int ns = r32(r + 0xb0), ord = r[0xb4], dim = r[0xb5]; double f = t >= 0 ? floor(t) : ceil(t); int i = (int)f; if (t >= 0) t -= i; if (i < 0) i = 0; else if (i > ns - 1) { t += i - ns + 1; i = ns - 1; }
        const uint8_t *cf = r + 0xc0 + 8 * ((size_t)ord * dim * i);
        for (int d = 0; d < dim; d++) { double x = 0, x1 = 0, x2 = 0, x3 = 0; for (int k = ord - 1; k >= 0; k--) { x3 = x3 * t + 3 * x2; x2 = x2 * t + 2 * x1; x1 = x1 * t + x; x = x * t + rd(cf + 8 * ((size_t)k * dim + d)); } o[3][d] = x3; }
        return 0; }
    return -1;
}
typedef struct { const McamFile *f; const uint8_t *rec; uint32_t n; int kind; Crv c[3]; } Se;
static int se_open(const McamFile *f, const uint8_t *rec, uint32_t n, Se *e) {
    memset(e, 0, sizeof *e); e->f = f; e->rec = rec; e->n = n; e->kind = rec[0x68];
    switch (e->kind) {
    case 0: case 1: return 0;
    case 2: return n >= 0x150 ? crv_get(f, r64(rec + 0x148), &e->c[0]) : -1;
    case 6: return n >= 0x128 ? crv_get(f, r64(rec + 0x120), &e->c[0]) : -1;
    case 7: if (n < 0x118) return -1; for (int i = 0; i < 3; i++) if (crv_get(f, r64(rec + 0x100 + 8 * i), &e->c[i])) return -1; return 0;
    default: return -1;
    }
}
/* eval_surf (0x18047ba50), modes 0 .. 2: o[0] P, o[1] Su, o[2] Sv, o[3] Suu, o[4] Suv, o[5] Svv */
static int se_ev(const Se *e, int mode, double u, double v, double o[6][3]) {
    memset(o, 0, 18 * sizeof(double));
    switch (e->kind) {
    case 0: return mcsb_nurbs_ders(e->rec, e->n, u, v, o);
    case 1: {   /* FUN_1805eb9c0 / FUN_1805ec000 */
        const uint8_t *r = e->rec; int nu = r32(r + 0x100), nv = r32(r + 0x104), ou = r[0x108], ov = r[0x109];
        double fu = u >= 0 ? floor(u) : ceil(u); int iu = (int)fu; if (u >= 0) u -= iu; if (iu >= 0) { if (iu > nu - 1) { u += iu - nu + 1; iu = nu - 1; } } else iu = 0;
        double fv = v >= 0 ? floor(v) : ceil(v); int iv = (int)fv; if (v >= 0) v -= iv; if (iv >= 0) { if (iv > nv - 1) { v += iv - nv + 1; iv = nv - 1; } } else iv = 0;
        size_t per = (size_t)ou * ov * 3; const uint8_t *c = r + 0x110 + 8 * (per * ((size_t)nu * iv + iu));
        double P[3] = { 0 }, Pv[3] = { 0 }, Pvv[3] = { 0 }, U[3] = { 0 }, Uv[3] = { 0 }, Uu[3] = { 0 };
        for (int j = ov - 1; j >= 0; j--) {
            double q0[3] = { 0 }, q1[3] = { 0 }, q2[3] = { 0 };
            for (int i = ou - 1; i >= 0; i--) for (int k = 0; k < 3; k++) { q2[k] = q2[k] * u + 2 * q1[k]; q1[k] = q1[k] * u + q0[k]; q0[k] = q0[k] * u + rd(c + 8 * (3 * ((size_t)j * ou + i) + k)); }
            for (int k = 0; k < 3; k++) { Pvv[k] = Pvv[k] * v + 2 * Pv[k]; Pv[k] = Pv[k] * v + P[k]; P[k] = P[k] * v + q0[k]; Uv[k] = Uv[k] * v + U[k]; U[k] = U[k] * v + q1[k]; Uu[k] = Uu[k] * v + q2[k]; }
        }
        memcpy(o[0], P, 24); memcpy(o[1], U, 24); memcpy(o[2], Pv, 24); memcpy(o[3], Uu, 24); memcpy(o[4], Uv, 24); memcpy(o[5], Pvv, 24);
        return 0; }
    case 2: {   /* eval_revsurf: F = A + d ((C - A) . d), R = C - F, P = F + R cos v + (d x R) sin v */
        const uint8_t *r = e->rec; double d[3], A[3], C[3][3]; for (int k = 0; k < 3; k++) { d[k] = rd(r + 0x100 + 8 * k); A[k] = rd(r + 0x118 + 8 * k); }
        if (crv_d(&e->c[0], u, mode, C)) return -1;
        double cs = cos(v), sn = sin(v), h0 = (C[0][0] - A[0]) * d[0] + (C[0][1] - A[1]) * d[1] + (C[0][2] - A[2]) * d[2], h1 = dot(C[1], d), h2 = dot(C[2], d), R0[3], R1[3], R2[3], X0[3], X1[3], X2[3];
        for (int k = 0; k < 3; k++) { R0[k] = C[0][k] - (d[k] * h0 + A[k]); R1[k] = C[1][k] - d[k] * h1; R2[k] = C[2][k] - d[k] * h2; }
        cross(d, R0, X0); cross(d, R1, X1); cross(d, R2, X2);
        for (int k = 0; k < 3; k++) {
            o[0][k] = R0[k] * cs + (d[k] * h0 + A[k]) + X0[k] * sn; o[1][k] = d[k] * h1 + R1[k] * cs + X1[k] * sn; o[2][k] = -R0[k] * sn + X0[k] * cs;
            o[3][k] = d[k] * h2 + R2[k] * cs + X2[k] * sn; o[4][k] = -R1[k] * sn + X1[k] * cs; o[5][k] = -R0[k] * cs - X0[k] * sn;
        }
        return 0; }
    case 6: {   /* FUN_180412490 */
        const uint8_t *D = e->rec + 0x100; double C[4][3]; if (crv_d3(&e->c[0], u, mode + 1 > 3 ? 3 : mode + 1, C)) return -1;
        double N[3] = { rd(D), rd(D + 8), rd(D + 16) }, L = len(N), md = -rd(D + 0x18), a[3] = { -N[0] / L, -N[1] / L, -N[2] / L }, X[3], Dir[3], dDir[3] = { 0 }, ddDir[3] = { 0 };
        cross(a, C[1], X);
        int big = isnan(fabs(md)) || fabs(md) == 1e12 || fabs(md) > 1e12, degen = !(fabs(X[0]) >= 1e-12 || fabs(X[1]) >= 1e-12 || fabs(X[2]) >= 1e-12);
        double off = 0; if (big) { if (0 < md) L = -L; } else off = md * L;
        if (degen) { Dir[0] = Dir[1] = 0; Dir[2] = 1; dDir[0] = dDir[1] = 0; dDir[2] = 1; ddDir[0] = ddDir[1] = 0; ddDir[2] = 1; }
        else { double m = len(X); for (int k = 0; k < 3; k++) Dir[k] = X[k] / m;
            if (mode) { double Y[3], Y3[3]; cross(a, C[2], Y); double dp = dot(Dir, Y); for (int k = 0; k < 3; k++) dDir[k] = (Y[k] - dp * Dir[k]) / m;
                if (mode > 1) { cross(a, C[3], Y3); double b18 = dot(dDir, Y) + dot(Y3, Dir); for (int k = 0; k < 3; k++) ddDir[k] = ((Y3[k] - b18 * Dir[k]) - dDir[k] * 2.0 * dp) / m; } } }
        for (int k = 0; k < 3; k++) {
            if (big) { o[0][k] = L * v * Dir[k] + C[0][k]; o[1][k] = L * v * dDir[k] + C[1][k]; o[2][k] = L * Dir[k]; o[3][k] = L * v * ddDir[k] + C[2][k]; o[4][k] = L * dDir[k]; }
            else { o[0][k] = (off * Dir[k] + N[k]) * v + C[0][k]; o[1][k] = off * dDir[k] * v + C[1][k]; o[2][k] = off * Dir[k] + N[k]; o[3][k] = off * ddDir[k] * v + C[2][k]; o[4][k] = off * dDir[k]; }
        }
        return 0; }
    case 7: {   /* FUN_18041c730 + FUN_1803fca60 / FUN_18041ced0 */
        double C0[3][3], C1[3][3], C2[3][3]; if (crv_d(&e->c[0], u, mode, C0) || crv_d(&e->c[1], u, mode, C1) || crv_d(&e->c[2], u, mode, C2)) return -1;
        double a[3], b[3], n[3]; for (int k = 0; k < 3; k++) { a[k] = C1[0][k] - C0[0][k]; b[k] = C2[0][k] - C0[0][k]; }
        cross(a, b, n); double m = len(n);
        if (!(1e-12 <= m)) { static const double vz[3] = { 0, 0, 1 }; memcpy(o[0], C0[0], 24); memcpy(o[1], C0[1], 24); memcpy(o[2], vz, 24); memcpy(o[3], C0[2], 24); memcpy(o[4], vz, 24); memcpy(o[5], vz, 24); return 0; }
        double nh[3] = { n[0] / m, n[1] / m, n[2] / m }, cs = dot(b, a) / dot(a, a), th = cs <= 0.999999999999 ? (-0.999999999999 <= cs ? acos(cs) : M_PI) : 0.0, R[3], Sv[3];
        rotv(a, nh, th * v, R);
        for (int k = 0; k < 3; k++) o[0][k] = R[k] + C0[0][k];
        double T[3] = { nh[0] * th, nh[1] * th, nh[2] * th }; cross(T, R, Sv); memcpy(o[2], Sv, 24); cross(T, Sv, o[5]);
        double sina = m / sqrt(dot(b, b) * dot(a, a)), tha = sina <= 0.999999 ? (-0.999999 <= sina ? asin(sina) : -1.5707963267948966) : 1.5707963267948966;
        for (int lv = 1; lv <= (mode > 1 ? 2 : 1); lv++) { double d1[3], d2[3], r1[3], r2[3]; for (int k = 0; k < 3; k++) { d1[k] = C1[lv][k] - C0[lv][k]; d2[k] = C2[lv][k] - C0[lv][k]; }
            rotv(d1, nh, tha * v, r1); rotv(d2, nh, (v - 1.0) * tha, r2); double *q = o[lv == 1 ? 1 : 3]; for (int k = 0; k < 3; k++) q[k] = r1[k] * (1.0 - v) + r2[k] * v + C0[lv][k]; }
        if (mode > 1) { double dS[3] = { o[1][0] - C0[1][0], o[1][1] - C0[1][1], o[1][2] - C0[1][2] }, x[3]; cross(n, dS, x); double f = tha / m; for (int k = 0; k < 3; k++) o[4][k] = x[k] * f; }
        return 0; }
    }
    return -1;
}
static double S_tol, S_chord; static int S_metric;
#define SE_RNG(e, i) rd((e)->rec + 0x70 + 8 * (i))
#define SE_FLAGS(e) r16((e)->rec + 0x6e)
/* eval_surf_extrap (0x18047bf10), for a surface in range or just past it: out of range and not closed the point goes on along the tangent; a point within 0.01 of the edge whose extrapolation is
   within tol x 10 (mm x 25.4) of the edge's point counts as on it */
static int se_extrap(const Se *e, int mode, double u, double v, char *inu, char *inv, double o[6][3], int reent) {
    double u0 = SE_RNG(e, 0), u1 = SE_RNG(e, 1), v0 = SE_RNG(e, 2), v1 = SE_RNG(e, 3), d2 = S_tol * 10.0, du = 0, dv = 0; int fl = SE_FLAGS(e);
    if (!(fl & 0x40)) return -1;
    if (S_metric) d2 *= 25.4;
    *inu = 1; *inv = 1;
    if (u < u0) { if (!(fl & 1)) { *inu = 0; du = u - u0; u = u0; } else { do { u += u1 - u0; du = 0; } while (u < u0); } }
    if (u1 < u) { if (!(fl & 1)) { *inu = 0; du = u - u1; u = u1; } else { do { u -= u1 - u0; } while (u1 < u); } }
    if (v < v0) { if (!(fl & 2)) { *inv = 0; dv = v - v0; v = v0; } else { do { v += v1 - v0; dv = 0; } while (v < v0); } }
    if (v1 < v) { if (!(fl & 2)) { *inv = 0; dv = v - v1; v = v1; } else { do { v -= v1 - v0; } while (v1 < v); } }
    int m2 = mode; if (mode == 0 && (!*inu || !*inv)) m2 = 1;
    double q[6][3]; if (se_ev(e, m2, u, v, q)) return -2;
    memcpy(o, q, sizeof q);
    if (!*inu) for (int k = 0; k < 3; k++) o[0][k] = q[1][k] * du + o[0][k];
    if (!*inv) for (int k = 0; k < 3; k++) o[0][k] = q[2][k] * dv + o[0][k];
    if (!reent) {
        if (!*inu) { double x = u - u0; int near_ = !(fabs(x) > 0.01 || fabs(x) == 0.01); double u2 = near_ ? u0 : u1, y = u - u1;
            if (near_ || (fabs(y) <= 0.01 && fabs(y) != 0.01)) { double p[6][3]; char a, b; se_extrap(e, 0, u2, v, &a, &b, p, 1); double f[3] = { p[0][0] - o[0][0], p[0][1] - o[0][1], p[0][2] - o[0][2] }; if (len(f) <= d2) { *inu = 1; u = u2; } } }
        if (!*inv) { double y = v - v0, ve = 0; int hit = 0; if (fabs(y) <= 0.01 && fabs(y) != 0.01) { ve = v0; hit = 1; } else { y = v - v1; if (fabs(y) <= 0.01 && fabs(y) != 0.01) { ve = v1; hit = 1; } }
            if (hit) { double p[6][3]; char a, b; se_extrap(e, 0, u, ve, &a, &b, p, 1); double f[3] = { p[0][0] - o[0][0], p[0][1] - o[0][1], p[0][2] - o[0][2] }; if (len(f) <= d2) *inv = 1; } }
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ make_new_trim's projection */
static double det3(const double a[3], const double b[3], const double c[3]) {   /* 0x1804b7a80 */
    return ((((b[0] * a[2] * c[1] + b[2] * a[1] * c[0] + a[0] * b[1] * c[2]) - a[2] * b[1] * c[0]) - b[0] * a[1] * c[2]) - b[2] * a[0] * c[1]);
}
static void unit_normal(const double a[3], const double b[3], double n[3]) {   /* UnitNormal (0x1804b7480) */
    cross(a, b, n); double l = len(n); if (l < 1e-12) l = 1e-12; for (int k = 0; k < 3; k++) n[k] /= l;
}
/* FUN_1805ed4a0 with param_2 = -1 (no view), no offset; project_pt_onto_surf (0x1805edee0) with param_3 = 0: 1 = the point lies on the surface inside its range */
static int proj_pt(const Se *e, const double q[3], const double st[2], double uvo[2], double Po[3]) {
    double u = st[0], v = st[1], u0 = SE_RNG(e, 0), u1 = SE_RNG(e, 1), v0 = SE_RNG(e, 2), v1 = SE_RNG(e, 3), ur = u1 - u0, vr = v1 - v0, tm = S_tol * (S_metric ? 25.4 : 1.0), sm_u = 1, sm_v = 1, pSu[3] = { 0 }, pSv[3] = { 0 }, o[6][3];
    int fl = SE_FLAGS(e), done = 0; char inu = 0, inv = 0;
    for (int it = 0; ; ) {
        if (se_extrap(e, 2, u, v, &inu, &inv, o, 0)) return 0;
        double E[3] = { q[0] - o[0][0], q[1] - o[0][1], q[2] - o[0][2] };
        if (len(E) < tm) { done = 1; break; }
        double N[3], ue[3] = { E[0], E[1], E[2] }; unit_normal(o[1], o[2], N); unit3(ue);
        double c = fabs(dot(N, ue)); if (c > 1.0) c = 1.0; double ang = acos(c);
        if (!(fabs(ang) > 1e-06 || fabs(ang) == 1e-06)) { done = 1; break; }
        if (it) { sm_u = len(o[1]) / len(pSu); sm_v = len(o[2]) / len(pSv); }
        double A[3] = { o[1][0] * sm_u, o[1][1] * sm_u, o[1][2] * sm_u }, B[3] = { o[2][0] * sm_v, o[2][1] * sm_v, o[2][2] * sm_v };
        double d = det3(A, B, N); int stop = 0;
        if (fabs(d) >= 1e-12) {
            double du = det3(E, B, N) / d, dv = det3(A, E, N) / d, n2[3], ee = o[1][0] * o[1][0] + o[1][1] * o[1][1] + o[1][2] * o[1][2], ff = dot(o[1], o[2]), gg = dot(o[2], o[2]);
            unit_normal(o[1], o[2], n2);
            double kL = dot(n2, o[3]), kM = dot(n2, o[4]), kN = dot(n2, o[5]);
            double k = (dv * kN * dv + kM * 2.0 * du * dv + du * kL * du) / (ff * 2.0 * du * dv + du * ee * du + dv * gg * dv);   /* surf_curv_dir */
            double den = 1.0 - k * (N[0] * E[0] + N[1] * E[1] + N[2] * E[2]) * 1.25;
            if (fabs(den) > 1e-12) { du /= den; dv /= den; }
            if (fabs(du) > ur || fabs(dv) > vr) stop = 1;
            else { u += du; v += dv; if (fabs(dv) + fabs(du) < 1e-06 && it > 10) { stop = 1; done = 1; } }
        } else stop = 1;
        memcpy(pSu, o[1], 24); memcpy(pSv, o[2], 24);
        if (it > 0x37) stop = 1;
        it++;
        if (stop) { if (!done) return 0; break; }
        if (fl & 1) { while (u < u0 - 1e-06) u += ur; while (u1 + 1e-06 < u) u -= ur; }
        if (fl & 2) { while (v < v0 - 1e-06) v += vr; while (v1 + 1e-06 < v) v -= vr; }
    }
    if (fabs(u - u0) < 1e-06) { inu = 1; u = u0; }
    if (fabs(u - u1) < 1e-06) { inu = 1; u = u1; }
    if (fabs(v - v0) < 1e-06) { inv = 1; v = v0; }
    if (fabs(v - v1) < 1e-06) { inv = 1; v = v1; }
    uvo[0] = u; uvo[1] = v; memcpy(Po, o[0], 24);
    return inu && inv;
}
/* build_grid (0x180532b30) on a kind 0 surface, the counts FUN_180541730 / FUN_1805419b0 ((degree + 1)(points - 1), x 4 in v for a rational surface, 9 .. 49, + 1), grid extents by add_to_minmax with the tol hysteresis */
typedef struct { int nu, nv; double *P, *U, mm[6]; } Grid;
/* FUN_1805b06f0: the basis over span sp of degree p with its small values cut (|N| < 1e-8 -> 0; eval_surf_const, not eval_surf) */
static void basis_const(double u, const double *K, int nk, int sp, int p, double *B) {
    for (int i = (sp - p < 0 ? 0 : sp - p); i <= sp + 1; i++) B[i] = 0;
    B[sp] = 1.0;
    for (int d = 1; d <= p; d++) { int st = sp - d < 0 ? 0 : sp - d;
        for (int i = st; i <= sp; i++) { if (i > nk - 2 - d) continue;
            double a = K[i], b = K[i + d], x, c = K[i + 1], e = K[i + d + 1];
            if (fabs(a - b) < 1e-12 || fabs(u - a) < 1e-12) { x = 0.0; B[i] = 0.0; } else { x = ((u - a) / (b - a)) * B[i]; B[i] = x; }
            if (1e-12 <= fabs(c - e) && 1e-12 <= fabs(u - e)) { x = x + ((e - u) * B[i + 1]) / (e - c); B[i] = x; }
            if (fabs(x) < 1e-08) B[i] = 0.0; } }
}
/* eval_surf_const (0x18047bd50) -> FUN_1805b1aa0 on a kind 0 record */
static int nbs_const(const Se *e, double u, double v, double P[3]) {
    const uint8_t *r = e->rec; int nku = r32(r + 0x100), nkv = r32(r + 0x104), ncu = r32(r + 0x108), ncv = r32(r + 0x10c), vpp = r[0x110], du = r[0x111], dv = r[0x112], rat = !(r[0x113] & 1);
    double *K = malloc(sizeof(double) * (size_t)(nku + nkv + 2)), *Bu = calloc((size_t)nku + 4, sizeof(double)), *Bv = calloc((size_t)nkv + 4, sizeof(double)); if (!K || !Bu || !Bv) { free(K); free(Bu); free(Bv); return -1; }
    for (int i = 0; i < nku + nkv; i++) K[i] = rd(r + 0x118 + 8 * (size_t)i);
    double *V = K + nku; const uint8_t *C = r + 0x118 + 8 * (size_t)(nku + nkv);
    int su = mcsb_nurb_interval(u, K, nku, du), sv = mcsb_nurb_interval(v, V, nkv, dv); basis_const(u, K, nku, su, du, Bu); basis_const(v, V, nkv, sv, dv, Bv);
    double acc[4] = { 0, 0, 0, 0 };
    for (int j = (sv - dv < 0 ? 0 : sv - dv); j <= sv; j++) { if (j >= ncv) continue;
        for (int i = (su - du < 0 ? 0 : su - du); i <= su; i++) { if (i >= ncu) continue;
            const uint8_t *q = C + 8 * (size_t)vpp * ((size_t)j * ncu + i); double w = Bu[i] * Bv[j];
            for (int k = 0; k < vpp; k++) acc[k] += rd(q + 8 * k) * w; } }
    free(K); free(Bu); free(Bv);
    if (rat) { if (fabs(acc[0]) <= 1e-12) { P[0] = acc[1]; P[1] = acc[2]; P[2] = acc[3]; } else { P[0] = acc[1] / acc[0]; P[1] = acc[2] / acc[0]; P[2] = acc[3] / acc[0]; } }
    else { P[0] = acc[0]; P[1] = acc[1]; P[2] = acc[2]; }
    return 0;
}
static int grid_build(const Se *e, Grid *g) {
    const uint8_t *r = e->rec; int du = r[0x111], dv = r[0x112], ncu = r32(r + 0x108), ncv = r32(r + 0x10c), a = (du + 1) * (ncu - 1), b = (dv + 1) * (ncv - 1);
    if (!(r[0x113] & 1)) b *= 4;
    if (a < 0x32) { if (a < 9) a = 9; } else a = 0x31;
    if (b < 0x32) { if (b < 9) b = 9; } else b = 0x31;
    g->nu = a + 1; g->nv = b + 1; g->P = malloc(sizeof(double) * 3 * (size_t)g->nu * g->nv); g->U = malloc(sizeof(double) * 2 * (size_t)g->nu * g->nv); if (!g->P || !g->U) return -1;
    double u0 = SE_RNG(e, 0), u1 = SE_RNG(e, 1), v0 = SE_RNG(e, 2), v1 = SE_RNG(e, 3), su = (u1 - u0) / (double)(g->nu - 1), sv = (v1 - v0) / (double)(g->nv - 1), v = v0, lo[3] = { 1e38, 1e38, 1e38 }, hi[3] = { -1e38, -1e38, -1e38 };
    size_t q = 0;
    for (int j = 0; j < g->nv; j++) { double u = u0;
        for (int i = 0; i < g->nu; i++) { double o[6][3]; if (nbs_const(e, u, v, o[0])) return -1;
            memcpy(g->P + 3 * q, o[0], 24); g->U[2 * q] = u; g->U[2 * q + 1] = v; q++;
            for (int k = 0; k < 3; k++) { if (o[0][k] <= lo[k] - S_tol && lo[k] - S_tol != o[0][k]) lo[k] = o[0][k]; if (hi[k] + S_tol < o[0][k]) hi[k] = o[0][k]; }
            u += su; if (u1 < u) u = u1; }
        v += sv; if (v1 < v) v = v1; }
    for (int k = 0; k < 3; k++) { g->mm[2 * k] = lo[k]; g->mm[2 * k + 1] = hi[k]; }
    return 0;
}
static double t_on_seg(const double p[3], const double a[3], const double b[3]) {   /* FUN_1804b47c0 */
    double d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, l = dot(d, d);
    if (S_tol * S_tol < l) return (d[1] * (p[1] - a[1]) + d[0] * (p[0] - a[0]) + d[2] * (p[2] - a[2])) / l;
    return 0.0;
}
/* find_neighbor (0x1805eccf0), no view: the start (u, v) from the grid cell's two edges nearest the point */
static int find_nb(const Grid *g, const double p[3], double uvo[2]) {
    double best = 1e38; int nu = g->nu;
    for (int j = 0; j < g->nv - 1; j++) for (int i = 0; i < nu - 1; i++) {
        size_t ia = (size_t)j * nu + i, ib = ia + 1, ic = ia + nu; const double *a = g->P + 3 * ia, *b = g->P + 3 * ib, *c = g->P + 3 * ic;
        double t1 = t_on_seg(p, a, b), t2 = t_on_seg(p, a, c), R[3], Q[3];
        if (0.0 < t1) { if (t1 < 1.0) { for (int k = 0; k < 3; k++) R[k] = a[k] + (b[k] - a[k]) * t1; } else { memcpy(R, b, 24); t1 = 1.0; } } else { memcpy(R, a, 24); t1 = 0.0; }
        if (0.0 < t2) { double dl[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] }; if (t2 < 1.0) { for (int k = 0; k < 3; k++) dl[k] *= t2; } else t2 = 1.0; for (int k = 0; k < 3; k++) Q[k] = R[k] + dl[k]; } else { memcpy(Q, R, 24); t2 = 0.0; }
        double f[3] = { p[0] - Q[0], p[1] - Q[1], p[2] - Q[2] }, d = len(f);
        if (best < d || best == d) continue;
        const double *ua = g->U + 2 * ia, *ub = g->U + 2 * ib, *uc = g->U + 2 * ic; double U[2];
        if (t1 == 0.0) { U[0] = ua[0]; U[1] = ua[1]; } else if (t1 == 1.0) { U[0] = ub[0]; U[1] = ub[1]; } else { U[0] = (ub[0] - ua[0]) * t1 + ua[0]; U[1] = (ub[1] - ua[1]) * t1 + ua[1]; }
        if (t2 == 0.0) { uvo[0] = U[0]; uvo[1] = U[1]; }
        else { double x = uc[0] - ua[0], y = uc[1] - ua[1]; if (t2 <= 1.0 && t2 != 1.0) { x *= t2; y *= t2; } uvo[0] = x + U[0]; uvo[1] = y + U[1]; }
        best = d;
    }
    return best < 1e38;
}
/* smart_project_onto_surf (0x18056a720), no view: the points inside the grid's box (grown by param_5 and 0.2 x the radius) projected from the grid's start, then the four quarter points; a result off the point by > param_5 refused */
static int smart_project(const Se *e, int np, const double *pts, double dev, double *uv, int *n) {
    Grid g; memset(&g, 0, sizeof g); *n = 0; if (grid_build(e, &g)) { free(g.P); free(g.U); return -1; }
    double r = rd(e->rec + 0x68 + 0x70) * 0.2, u0 = SE_RNG(e, 0), u1 = SE_RNG(e, 1), v0 = SE_RNG(e, 2), v1 = SE_RNG(e, 3);
    for (int k = 0; k < 3; k++) { g.mm[2 * k] = g.mm[2 * k] - dev; g.mm[2 * k + 1] += dev; g.mm[2 * k] -= r; g.mm[2 * k + 1] += r; }
    for (int i = 0; i < np; i++) {
        const double *p = pts + 3 * i;
        if (!(g.mm[0] - S_tol < p[0] && p[0] < S_tol + g.mm[1] && g.mm[2] - S_tol < p[1] && p[1] < S_tol + g.mm[3] && g.mm[4] - S_tol < p[2] && p[2] < S_tol + g.mm[5])) continue;
        double st[2] = { 0, 0 }, P[3]; int have = 0, ok = 0;
        for (int a = 0; a < 6; a++) {
            if (have) {
                ok = proj_pt(e, p, st, uv + 2 * *n, P);
                if (ok) { double f[3] = { p[0] - P[0], p[1] - P[1], p[2] - P[2] }; if (dev < len(f)) ok = 0; }
                if (ok) { (*n)++; break; }
            }
            if (a == 0) { if (find_nb(&g, p, st)) have = 1; else a = 6; }
            if (!ok && a >= 1 && a <= 4) {
                double au = a <= 2 ? 0.75 : 0.25, av = (a == 1 || a == 3) ? 0.75 : 0.25;
                st[0] = (1.0 - au) * u1 + au * u0; st[1] = (1.0 - av) * v1 + av * v0;
            }
        }
    }
    free(g.P); free(g.U);
    return 0;
}
/* ------------------------------------------------------------------------------------------------ the trim's points */
static int col2(const double a[2], const double b[2], const double c[2]) {   /* check_colinear p_2d (0x180666430) */
    double d1[2] = { b[0] - a[0], b[1] - a[1] }, d2[2] = { c[0] - b[0], c[1] - b[1] }, l1 = sqrt(d1[0] * d1[0] + d1[1] * d1[1]), l2 = sqrt(d2[0] * d2[0] + d2[1] * d2[1]);
    if (l1 <= 1e-08 || l2 <= 1e-08) return 1;
    if (0.99985 <= (d2[0] * d1[0] + d2[1] * d1[1]) / (l2 * l1)) { /* dist_pt_l */ double dx = b[0] - a[0], dy = b[1] - a[1], ex = c[1] - a[1], fx = c[0] - a[0], l = sqrt(ex * ex + fx * fx), d;
        if (l <= S_tol) d = sqrt(dy * dy + dx * dx); else { d = (dx * ex - dy * fx) / l; if (d < 0) d = -d; }
        return d < 1e-08; }
    return 0;
}
static int col3(const double a[3], const double b[3], const double c[3]) {   /* check_colinear p_3d (0x1806665c0) */
    double d1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, d2[3] = { c[0] - b[0], c[1] - b[1], c[2] - b[2] }, l1 = len(d1), l2 = len(d2);
    if (l1 <= S_tol || l2 <= S_tol) return 1;
    if (0.99985 <= dot(d2, d1) / (l2 * l1)) { double ab[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] }, pa[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, x[3], l = len(ab), d; cross(pa, ab, x);   /* dist_pt_l_3d */
        if (fabs(l) <= S_tol) d = len(pa); else d = fabs(len(x) / l);
        return d < S_tol * 2.0; }
    return 0;
}
/* reduce_uv_data (0x1803ec970) */
static void reduce_uv(const Se *e, int *pn, double *uv) {
    int n = *pn, w = 2, ch = 0; if (n < 3) return;
    double A[2] = { uv[0], uv[1] }, B[2] = { uv[2], uv[3] };
    for (int i = 2; i < n; i++) {
        double C[2] = { uv[2 * i], uv[2 * i + 1] }; int col = col2(A, B, C);
        if (col) { double o1[6][3], o2[6][3], o3[6][3]; se_ev(e, 0, A[0], A[1], o1); se_ev(e, 0, B[0], B[1], o2); se_ev(e, 0, C[0], C[1], o3); col = col3(o1[0], o2[0], o3[0]); }
        if (col) { uv[2 * (w - 1)] = C[0]; uv[2 * (w - 1) + 1] = C[1]; ch = 1; }
        else { uv[2 * w] = C[0]; uv[2 * w + 1] = C[1]; w++; A[0] = B[0]; A[1] = B[1]; }
        B[0] = C[0]; B[1] = C[1];
    }
    if (ch) *pn = w;
}
/* calc_chord_ht (0x1805eca80) */
static double chord_ht(const double p0[3], const double t0[3], const double p1[3], const double t1[3]) {
    double d[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] }, l = len(d), c0 = dot(t0, d) / (len(t0) * l), c1 = dot(t1, d) / (len(t1) * l);
    if (!(c0 <= 1.0)) c0 = 1.0; else if (c0 < -1.0) c0 = -1.0;
    if (!(c1 <= 1.0)) c1 = 1.0; else if (c1 < -1.0) c1 = -1.0;
    return (fabs(tan(acos(c1))) + fabs(tan(acos(c0)))) * l * 0.25;
}
/* curv (0x1804b7990) */
static double curv1(const double a[3], const double b[3]) { double x[3]; cross(b, a, x); double l = dot(a, a); return len(x) / (sqrt(l) * l); }
/* FUN_180274d90: the turn at the last stored point within ang (make_new_trim passes 4 pi: always true; kept for the caller's literal) */
static int turn_ok(const double p[3], double ang, int n, const double *pts) {
    if (n < 2) return 1;
    const double *a = pts + 3 * (n - 2), *b = pts + 3 * (n - 1); double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { p[0] - b[0], p[1] - b[1], p[2] - b[2] }; unit3(u); unit3(v);
    return fabs(acos(dot(v, u))) <= ang;
}
/* get_constant_uvpts (0x180274fa0) with no uv output: 3D points along u = const (p5 0) or v = const (p5 1) of the surface from lo to hi, chord deviation dev, appended to pts after *np (0 on entry) */
static int const_uvpts(const Se *e, double dev, int rev, int p5, double cval, const double rg[2], double ang, int maxn, double *tout, int *np, double *pts, int *done) {
    double par[2], step = 0; int first = 1, fin = 0, last = 0; *done = 0;
    double cur = rev ? rg[1] : rg[0], end = rev ? rg[0] : rg[1], o[6][3], prevP[3] = { 0 }, prevT[3] = { 0 };
    par[p5 == 1 ? 1 : 0] = cval; par[p5 == 0 ? 1 : 0] = cur;
    for (;;) {
        if (se_ev(e, 2, par[0], par[1], o)) return -1;
        const double *deriv = p5 == 0 ? o[2] : o[1];
        double T[3] = { deriv[0], deriv[1], deriv[2] }; unit3(T); if (rev) { T[0] = -T[0]; T[1] = -T[1]; T[2] = -T[2]; }
        double *var = &par[p5 == 0 ? 1 : 0];
        if (first) {
            int n = *np; int skip = 0;
            if (n != 0) { const double *q = pts + 3 * (n - 1); double f[3] = { q[0] - o[0][0], q[1] - o[0][1], q[2] - o[0][2] }; if (len(f) <= S_tol) skip = 1; }
            if (!skip) { if (n > 1 && !turn_ok(o[0], ang, n, pts)) { *tout = cur; return 0; }
                memcpy(pts + 3 * n, o[0], 24); (*np)++; first = 0; }
        } else {
            double h = chord_ht(prevP, prevT, o[0], T);
            if (dev * 1.05 < h) { step *= 0.5; *var = cur + step; last = 0; continue; }
            double f[3] = { prevP[0] - o[0][0], prevP[1] - o[0][1], prevP[2] - o[0][2] }, d = len(f);
            int n = *np;
            if (d <= S_tol || (n > 1 && col3(pts + 3 * (n - 2), pts + 3 * (n - 1), o[0]))) memcpy(pts + 3 * (n - 1), o[0], 24);
            else { if (!turn_ok(o[0], ang, n, pts)) { *tout = cur; return 0; } memcpy(pts + 3 * n, o[0], 24); (*np)++; if (maxn <= *np) { *tout = *var; return 0; } }
        }
        cur = *var;
        if (last) fin = 1;
        else {
            double dL = len(deriv);
            if (S_tol <= fabs(dL)) {
                double k = curv1(o[2 - p5], o[4 - p5]);
                if (0.001 <= k) { if (k <= 2.0 / dev) { step = sqrt((2.0 / k - dev) * dev); step = (step * 2.0) / dL; } else step = 1.0 / (k * dL); }
                else step = (rg[1] - rg[0]) / 10.0;
            } else {
                memcpy(prevP, o[0], 24); fin = 1; *var = end;
                if (se_ev(e, 2, par[0], par[1], o)) return -1;
                double f[3] = { o[0][0] - prevP[0], o[0][1] - prevP[1], o[0][2] - prevP[2] }; int n = *np;
                if (len(f) <= S_tol) memcpy(pts + 3 * (n - 1), o[0], 24); else { memcpy(pts + 3 * n, o[0], 24); (*np)++; }
            }
            if (rev) step = -step;
            *var += step;
            if (!rev) { if (end - 1e-07 <= *var) { last = 1; *var = end; } } else { if (*var <= end + 1e-07) { last = 1; *var = end; } }
        }
        memcpy(prevP, o[0], 24); memcpy(prevT, T, 24);
        if (fin) { *done = 1; return 0; }
    }
}
/* check_edge (0x18047b620): a point within 0.01 of a parameter edge and within the chord deviation of the edge's point goes to it; 8 u0, 2 u1, 1 v0, 4 v1 */
static int check_edge(const Se *e, double uv[2], double P[3]) {
    double u0 = SE_RNG(e, 0), u1 = SE_RNG(e, 1), v0 = SE_RNG(e, 2), v1 = SE_RNG(e, 3); int fl = 0; char a, b; double o[6][3], f[3];
    if (fabs(uv[0] - u0) < 0.01) { se_extrap(e, 0, u0, uv[1], &a, &b, o, 0); for (int k = 0; k < 3; k++) f[k] = P[k] - o[0][k]; if (len(f) < S_chord) { fl = 8; memcpy(P, o[0], 24); uv[0] = u0; } }
    else if (fabs(uv[0] - u1) < 0.01) { se_extrap(e, 0, u1, uv[1], &a, &b, o, 0); for (int k = 0; k < 3; k++) f[k] = P[k] - o[0][k]; if (len(f) < S_chord) { fl = 2; memcpy(P, o[0], 24); uv[0] = u1; } }
    if (fabs(uv[1] - v0) < 0.01) { se_extrap(e, 0, uv[0], v0, &a, &b, o, 0); for (int k = 0; k < 3; k++) f[k] = P[k] - o[0][k]; if (len(f) < S_chord) { fl |= 1; memcpy(P, o[0], 24); uv[1] = v0; } }
    else if (fabs(uv[1] - v1) < 0.01) { se_extrap(e, 0, uv[0], v1, &a, &b, o, 0); for (int k = 0; k < 3; k++) f[k] = P[k] - o[0][k]; if (len(f) < S_chord) { fl |= 4; memcpy(P, o[0], 24); uv[1] = v1; } }
    return fl;
}
#define STN_MAXNODES (1 << 20)  /* more than get_max_n_spline_pts would hold in any stored loop; a chunk flush at the DLL's buffer size is NOT modelled (the appended offset is unclear there) */
/* FUN_18005a470: the 3D points between two successive trim points: along an iso line when one parameter is constant and the chord is long (get_constant_uvpts), else just the point */
static int seg_nodes(const Se *b, const double cu[2], const double pu[2], const double cP[3], const double pP[3], double *nodes, int *count, unsigned short **edges, int *ecap, int closed) {
    int s21 = 1;
    if (fabs(cu[0] - pu[0]) >= 0.0001) { if (fabs(cu[1] - pu[1]) >= 0.0001) goto plain; }
    else s21 = 0;
    { double f[3] = { pP[0] - cP[0], pP[1] - cP[1], pP[2] - cP[2] };
      if (S_chord * 10.0 <= len(f)) {
        int e8 = s21; double cv = cu[e8 == 0 ? 1 : 0], pv = pu[e8 == 0 ? 1 : 0], cc = cu[e8 == 1 ? 1 : 0];
        int upw = cv > pv, rev = !upw; double rg[2] = { upw ? pv : cv, upw ? cv : pv }, tout; int nout = 0, done = 0;
        if (const_uvpts(b, S_chord, rev, e8, cc, rg, 12.566370614359167, STN_MAXNODES - *count, &tout, &nout, nodes + 3 * (*count - 1), &done)) return -1;
        if (!done) return -2;
        if (done && nout == 2) goto plain;
        if (closed) {
            unsigned short u1 = (*edges)[*count], u2 = (*edges)[*count - 1];
            int need = *ecap + nout - 2; if (need > *ecap) { *edges = realloc(*edges, sizeof(unsigned short) * (size_t)need); for (int i = *ecap; i < need; i++) (*edges)[i] = 0; *ecap = need; }
            for (int j = 0; j < nout - 2; j++) (*edges)[*count + j] = u1 & u2;
            (*edges)[nout + *count - 2] = u1;
        }
        *count += nout - 1;
        return 0;
      } }
plain:
    memcpy(nodes + 3 * *count, cP, 24); (*count)++;
    return 0;
}
/* FUN_180059cf0: a point that was on an edge of a closed direction of the old base goes to the new base's edge (bit 0x1 of its flags: 2 -> u1, 8 -> u0; bit 0x2: 1 -> v0, 4 -> v1) */
static void snap_edges(int bfl, const Se *t, int n, double *uv, const unsigned short *edges) {
    double u0 = SE_RNG(t, 0), u1 = SE_RNG(t, 1), v0 = SE_RNG(t, 2), v1 = SE_RNG(t, 3);
    for (int i = 0; i < n; i++) {
        if (bfl & 1) { if (edges[i] & 2) uv[2 * i] = u1; if (edges[i] & 8) uv[2 * i] = u0; }
        if (bfl & 2) { if (edges[i] & 1) uv[2 * i + 1] = v0; if (edges[i] & 4) uv[2 * i + 1] = v1; }
    }
}
/* make_new_trim (mccadutil 0x18005a9b0): the old loops sampled on the old base (*b), the 3D points projected on the new one (*t). out: the new uv points, the table { start, points - 2 } per loop. 0, or -1 with *why */
static int make_new_trim(const Se *b, const Se *t, const double *uv, const int32_t *tbl, int nl, double **uvo, int32_t **tblo, int *mo, const char **why) {
    int bfl = SE_FLAGS(b), closed = (bfl & 1) || (bfl & 2), total = 0, ecap = 0, ocap = 4096, nodecap = 0; unsigned short *edges = NULL; double *nodes = NULL, *out = malloc(sizeof(double) * 2 * ocap); int32_t *tb = malloc(sizeof(int32_t) * 2 * (size_t)nl);
    int rc = -1; double *chunk = NULL;
    if (!out || !tb) { *why = "out of memory"; goto done; }
    nodecap = STN_MAXNODES; nodes = malloc(sizeof(double) * 3 * (size_t)nodecap); chunk = malloc(sizeof(double) * 2 * (size_t)nodecap);
    if (!nodes || !chunk) { *why = "out of memory"; goto done; }
    for (int l = 0; l < nl; l++) {
        int st = tbl[2 * l], ct = tbl[2 * l + 1], end = st + ct, count = 0;
        if (closed && ct + 3 > ecap) { edges = realloc(edges, sizeof(unsigned short) * (size_t)(ct + 3)); for (int i = ecap; i < ct + 3; i++) edges[i] = 0; ecap = ct + 3; }
        double pu[2] = { 0, 0 }, pP[3] = { 0 }, cu[2], cP[3], o[6][3];
        for (int i = st; i <= end + 1; i++) {     /* i = end + 1: the closing point, the loop's first (make_new_trim's tail) */
            int idx = i == end + 1 ? st : i; cu[0] = uv[2 * idx]; cu[1] = uv[2 * idx + 1];
            if (se_ev(b, 0, cu[0], cu[1], o)) { *why = "the old base could not be evaluated"; goto done; }
            memcpy(cP, o[0], 24);
            if (closed) { if (count >= ecap) { edges = realloc(edges, sizeof(unsigned short) * (size_t)(count + 16)); for (int k = ecap; k < count + 16; k++) edges[k] = 0; ecap = count + 16; } edges[count] = (unsigned short)check_edge(b, cu, cP); }
            if (i == st) { memcpy(nodes, cP, 24); count = 1; }
            else {
                int r = seg_nodes(b, cu, pu, cP, pP, nodes, &count, &edges, &ecap, closed);
                if (r == -2) { *why = "a trim loop needs more 3D points than this port's buffer (make_new_trim's chunked flush is not modelled)"; goto done; }
                if (r) { *why = "the old base could not be sampled"; goto done; }
                if (count >= nodecap - 8) { *why = "a trim loop needs more 3D points than this port's buffer (make_new_trim's chunked flush is not modelled)"; goto done; }
            }
            memcpy(pu, cu, 16); memcpy(pP, cP, 24);
        }
        int n = 0;
        if (smart_project(t, count, nodes, S_chord * 10.0, chunk, &n)) { *why = "the projection onto the new base failed"; goto done; }
        if (closed) snap_edges(bfl, t, n, chunk, edges);
        reduce_uv(t, &n, chunk);
        if (0x7fff < total + n) { *why = "more than 0x7fff trim points (make_new_trim fails)"; goto done; }
        if (total + n > ocap) { while (total + n > ocap) ocap *= 2; out = realloc(out, sizeof(double) * 2 * ocap); }
        memcpy(out + 2 * total, chunk, sizeof(double) * 2 * n);
        tb[2 * l] = total; tb[2 * l + 1] = n - 2;
        total += n;
    }
    *uvo = out; *tblo = tb; *mo = total; out = NULL; tb = NULL; rc = 0;
done:
    free(out); free(tb); free(edges); free(nodes); free(chunk);
    return rc;
}
/* make_new_trim's in / out for the oracle */
int mcstn_make_new_trim(const McamFile *f, const uint8_t *base, uint32_t bn, const uint8_t *t, uint32_t tn, const double *uv, const int32_t *tbl, int nl, double tol, double **uvo, int32_t **tblo, int *mo, const char **why) {
    Se B, T; S_tol = tol; S_metric = mcam_is_metric(f) != 0; S_chord = mc_systol_units(mcam_is_metric(f))->surf_step_chord_dev;
    if (se_open(f, base, bn, &B)) { *why = "the old base kind is not handled"; return -1; }
    if (se_open(f, t, tn, &T) || T.kind != 0) { *why = "the new base is not NURBS"; return -1; }
    return make_new_trim(&B, &T, uv, tbl, nl, uvo, tblo, mo, why);
}
int mcstn_se_eval(const McamFile *f, const uint8_t *rec, uint32_t n, int mode, double u, double v, double o[6][3]) { Se E; if (se_open(f, rec, n, &E)) return -1; S_tol = mc_systol.tol; return se_ev(&E, mode, u, v, o); }

/* ------------------------------------------------------------------------------------------------ the entry points */
static const Surf *follow_base(const McamFile *f, const Surf *s) {      /* find_base_surf (0x1806f75b0): the eptr of a kind 8 (+0xb8 = ent +0x120) / 10 (ent +0x100) */
    const Surf *b = s;
    for (int g = 0; g < 64 && b && (b->kind == 8 || b->kind == 10); g++) {
        if (b->n < (b->kind == 8 ? 0x128u : 0x108u)) return b;
        const Surf *nb = mcs_surf(f, r64(b->rec + (b->kind == 8 ? 0x120 : 0x100))); if (!nb) return b;
        b = nb;
    }
    return b;
}
static double find_off(const McamFile *f, const Surf *s) {              /* find_base_surf: the offsets added up along the chain */
    double tot = 0; const Surf *b = s;
    for (int g = 0; g < 64 && b && (b->kind == 8 || b->kind == 10); g++) {
        if (b->n < (b->kind == 8 ? 0x128u : 0x108u)) break;
        if (b->kind == 8) tot += rd(b->rec + 0x118);
        b = mcs_surf(f, r64(b->rec + (b->kind == 8 ? 0x120 : 0x100)));
    }
    return tot;
}
int mcstn_rule(const McamFile *f, const Surf *s) {
    if (s->kind == 0) return 0;
    if (s->kind == 8 || s->kind == 10) return follow_base(f, s)->kind == 0 ? 0 : 1;
    return 1;
}
/* FUN_180266ca0: the kind 0 record of a surface kind 1 / 2 / 6 / 7, before put_surf_in_db */
static uint8_t *conv_raw(const McamFile *f, const Surf *s, double tol, uint32_t *rn, const char **why) {
    uint8_t *R = NULL;
    switch (s->kind) {
    case 1: R = conv_pps(s, rn, why); break;
    case 2: R = mcsb_rev_of_kind2(f, s, tol, rn); if (!R) *why = "kind 2: the profile could not be read"; break;
    case 6: R = conv_draft(f, s, tol, rn, why); break;
    case 7: R = conv_arcsweep(f, s, tol, rn, why); break;
    case 4: *why = "kind 4 (ruled / lofted surface): FUN_180265980 (MaCompositeCurve / MaBicubicSurf::ConvertToNURB and FUN_1802655f0) is not ported; absent from the sample corpus"; break;
    default: *why = "surface kind is not handled by create_nurbsurf_from_surf"; break;
    }
    return R;
}
uint8_t *mcstn_convert(const McamFile *f, const Surf *s, int shaded, double tol, uint32_t *rn, const char **why) {
    uint8_t *R = NULL; static const char *dummy; if (!why) why = &dummy; *why = NULL;
    if (s->kind == 0 || ((s->kind == 8 || s->kind == 10) && follow_base(f, s)->kind == 0)) { R = malloc(s->n); if (R) { memcpy(R, s->rec, s->n); *rn = s->n; } return R; }   /* create_nurbsurf_from_surf: copied unchanged */
    if (s->kind == 8 || s->kind == 10) { *why = "offset / trimmed surface on a non-NURBS base: mcstn_base + mcstn_rebuild"; return NULL; }
    R = conv_raw(f, s, tol, rn, why);
    if (R && mcsb_finish(f, R, *rn, shaded, tol)) { free(R); *why = "a parameter range below 1e-12 (put_surf_in_db stores nothing)"; return NULL; }
    return R;
}
/* create_nurbsurf_from_surf, kinds 8 / 10 on a non-NURBS root: FUN_180266ca0 on the root; the surf header = the root's with kind 0 (its bits + 0x40, its bytes 4 / 5), the range = the knots (no profile range here); put_surf_in_db in the
   main attributes with blank = 1 (sel bit 0x100, rec[5] & 1); rewrite_ent with ent +0x28 = 1. Store it first: mcam_new_surf(rec), Surf.blanked = 1, Ent.blanked = 1; its eptr (Ent.septr) goes to mcstn_rebuild. */
uint8_t *mcstn_base(const McamFile *f, const Surf *s, int shaded, double tol, uint32_t *rn, const char **why) {
    static const char *dummy; if (!why) why = &dummy; *why = NULL;
    const Surf *b = follow_base(f, s);
    if (b->kind == 8 || b->kind == 10) { *why = "the base chain of the surface is broken"; return NULL; }
    if (b->kind == 0) { *why = "the base is already NURBS (left alone)"; return NULL; }
    uint8_t *R = conv_raw(f, b, tol, rn, why); if (!R) return NULL;
    uint8_t *S = R + 0x68; const uint8_t *K = R + 0x118; int nku = r32(R + 0x100), nkv = r32(R + 0x104); uint16_t fl = (uint16_t)(r16(b->rec + 0x6e) | 0x40);
    S[4] = b->rec[0x68 + 4]; S[5] = b->rec[0x68 + 5]; S[6] = (uint8_t)fl; S[7] = (uint8_t)(fl >> 8);
    wdd(S + 8, rd(K)); wdd(S + 0x10, rd(K + 8 * (size_t)(nku - 1))); wdd(S + 0x18, rd(K + 8 * (size_t)nku)); wdd(S + 0x20, rd(K + 8 * (size_t)(nku + nkv - 1)));
    if (mcsb_finish(f, R, *rn, shaded, tol)) { free(R); *why = "a parameter range below 1e-12 (put_surf_in_db stores nothing)"; return NULL; }
    R[0x28] = 1; R[0x29] = 0; R[5] |= 1;                                 /* rewrite_ent: ent +0x28 = 1; put_surf_in_db param_4: blanked */
    return R;
}
/* the surface itself on the stored new base (eptr base_eptr, record base): kind 8: the surf header = the base's with kind 8, flags (base's with the source's bit 2) | 0x40, the source's data with the new eptr; kind 10 on a root with
   no net offset (|sum| <= 1e-5): the same with kind 10 and make_new_trim's points. put_surf_in_db in the source's colour / level, then delete the source (delete_ent: a blanked old base with ent +0x28 < 2 and +0x38 < 1 goes with it). */
uint8_t *mcstn_rebuild(const McamFile *f, const Surf *s, int64_t base_eptr, const uint8_t *base, uint32_t bn, int shaded, double tol, uint32_t *rn, const char **why) {
    static const char *dummy; if (!why) why = &dummy; *why = NULL;
    if ((s->kind != 8 && s->kind != 10) || bn < 0x100) { *why = "not an offset / trimmed surface"; return NULL; }
    uint16_t fl = (uint16_t)((((r16(base + 0x6e) ^ r16(s->rec + 0x6e)) & 4) ^ r16(base + 0x6e)) | 0x40);
    if (s->kind == 8) {
        if (s->n < 0x128) { *why = "kind 8 record too short"; return NULL; }
        uint8_t *R = calloc(1, s->n); if (!R) { *why = "out of memory"; return NULL; }
        *rn = s->n; uint32_t cls = 0x100; memcpy(R, &cls, 4); memcpy(R + 0x68, base + 0x68, 0x98); R[0x68] = 8; R[0x68 + 6] = (uint8_t)fl; R[0x68 + 7] = (uint8_t)(fl >> 8);
        memcpy(R + 0x100, s->rec + 0x100, 0x28); memcpy(R + 0x120, &base_eptr, 8);
        if (mcsb_finish(f, R, *rn, shaded, tol)) { free(R); *why = "a parameter range below 1e-12 (put_surf_in_db stores nothing)"; return NULL; }
        return R;
    }
    if (s->n < 0x138) { *why = "kind 10 record too short"; return NULL; }
    if (fabs(find_off(f, s)) > 1e-5) { *why = "a trim on an offset of a non-NURBS base: make_new_trim onto the offset surface needs eval_surf modes 1 / 2 of FUN_180460cd0, not ported (none in the sample corpus)"; return NULL; }
    const Surf *ob = mcs_surf(f, r64(s->rec + 0x100));
    if (!ob || ob->kind == 8 || ob->kind == 10) { *why = "the trim's direct base is itself offset / trimmed: not ported (none in the sample corpus)"; return NULL; }
    int m = r32(s->rec + 0x10c), nl = r32(s->rec + 0x128);
    if (m < 1 || nl < 1 || 0x138 + 16 * (size_t)m + 8 * (size_t)nl > s->n) { *why = "kind 10: bad point or loop counts"; return NULL; }
    double *uv = malloc(sizeof(double) * 2 * (size_t)m), *uvo = NULL; int32_t *tb = malloc(sizeof(int32_t) * 2 * (size_t)nl), *tbo = NULL; int mo = 0; uint8_t *R = NULL; double **pts = NULL; int *cnt = NULL;
    if (!uv || !tb) { *why = "out of memory"; goto end; }
    memcpy(uv, s->rec + 0x138, sizeof(double) * 2 * (size_t)m); memcpy(tb, s->rec + 0x138 + 16 * (size_t)m, sizeof(int32_t) * 2 * (size_t)nl);
    if (mcstn_make_new_trim(f, ob->rec, ob->n, base, bn, uv, tb, nl, tol, &uvo, &tbo, &mo, why)) goto end;
    pts = malloc(sizeof(double *) * (size_t)nl); cnt = malloc(sizeof(int) * (size_t)nl);
    if (!pts || !cnt) { *why = "out of memory"; goto end; }
    for (int l = 0; l < nl; l++) { pts[l] = uvo + 2 * tbo[2 * l]; cnt[l] = tbo[2 * l + 1] + 2; }
    R = mcsb_trim_rec(f, base_eptr, base, bn, nl, pts, cnt, fl, shaded, tol, rn);
    if (!R) { *why = "a parameter range below 1e-12 (put_surf_in_db stores nothing)"; goto end; }
    {   int b124 = R[0x124]; int mm = r32(R + 0x10c); memcpy(R + 0x100, s->rec + 0x100, 0x38); memcpy(R + 0x100, &base_eptr, 8); w32(R + 0x10c, mm); R[0x124] = (uint8_t)b124;   /* the source's trim data but the new base and points */
        R[0x68 + 4] = base[0x68 + 4]; R[0x68 + 5] = base[0x68 + 5];
        if (mcsb_finish(f, R, *rn, shaded, tol)) { free(R); R = NULL; *why = "a parameter range below 1e-12 (put_surf_in_db stores nothing)"; } }
end:
    free(uv); free(tb); free(uvo); free(tbo); free(pts); free(cnt);
    return R;
}
