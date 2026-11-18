/* mcpartline_mesh.c - make_surf_polygons (mccore 0x1806fbda0) as FUN_18054be80 (0x18054be80, parting_line's caller) uses it: the surface's triangles.
 * Spec: ../formats/wireframe.md "Curve Parting Line"; decompiles decomp/partline/core_tess.c, core_pl.c, mesh_curv.c, mesh_basis.c, core_evalcache.c;
 * disassembly decomp/partline/tess_asm.txt, msp_asm*.txt, mesh_asm2..5.txt (Ghidra loses most stack arguments in this engine and sometimes
 * re-associates sums: the arguments and the evaluation order below are the disassembly's, cited by instruction address).
 * Oracle: tools/peloader --partline (mccore_pl.c), tools/partline_port.c compares: the 91 sample cases and 22 synthetic records (non-uniform
 * knots, merged spans, rational) give the same triangles, the same (u, v) and the same xyz bit for bit.
 *
 * Path ported (the only one FUN_18054be80 -> make_surf_polygons('\x01','\x01', ent, step, 0, 0, ..) takes for an untrimmed kind 0 surface):
 *   make_surf_polygons 1806fc06e -> FUN_18058de20(-1, -1, 0, &v_z, &v_z, &v_z, 0.0, 0.0, 1, 1, 0, ent, step, step, 0, 0, step * 0.1, 0, 0, 0, &tri, &xyz, &uv, ..)
 *   (msp_asm.txt 1806fbe70..1806fc06e) -> FUN_18057a980 (msp_asm2.txt 18058df9c..18058e02a): in FUN_18057a980's frame in_stack_40 = in_stack_48 = 0.0,
 *   in_stack_50 = 0, in_stack_68 = 1, in_stack_70 = 1 (uv wanted), in_stack_78 = 0, in_stack_80 = ent, in_stack_88 = step, in_stack_90 = step * 0.1,
 *   in_stack_98 = 0, in_stack_a0 = 0, in_stack_d0 = 0. With step > 0 the branch at 18057b06f..18057b0d2 is taken (local_2d87 = 1, local_2d28 = 1,
 *   local_2d86 = 0): one FUN_180581f00 pass, the counts, the grid, the triangles (LAB_18057b7a2 .. LAB_18057d932, one pass of the do-loop).
 * Trimmed surfaces (kind 10 over a kind 0 base, formats/interfacial.md 12.8): the same grid on the base inside the trim loops' box, the loops
 * and the cells in mcpartline_trim.inc / mcpartline_trim2.inc / mcpartline_trim3.inc (included below). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "mcpartline.h"
#include "mcsurf_nrm.h"
#include "mcsurf.h"

/* CVTTSD2SI: truncation, 0x80000000 for NaN / out of range (the DLL's (int) of its ceil results) */
static int cvtt(double x) { return (x != x || x >= 2147483648.0 || x < -2147483648.0) ? (int)0x80000000u : (int)x; }
static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int64_t rd64(const uint8_t *p) { int64_t v; memcpy(&v, p, 8); return v; }

typedef struct {
    const McamFile *f; const Surf *s;
    double tol;              /* the global tol (0x1812b5b58) */
    const char *err;         /* first failure (NOT PORTED / NOT DECODED / eval failed) */
    int use_off; double off; /* make_surf_model (plm_model): the kind 8 copy's distance is not 0 - ev8 runs FUN_180460cd0 in full (ev8_off) */
    int base_eval;           /* plm_model's FUN_18057dd40 / FUN_1805858b0 / FUN_180589880: eval_surf of the surface itself (plm_eval) */
    const Surf *hdr;         /* the vertex pass of a trimmed surface: the kind 10 surface calc_surf_unit_norm is given (plm_unit_norm_h) */
} Ctx;
static int ev8_off(Ctx *c, int order, double u, double v, double D[][3]);
typedef struct { int nu, nv, ou, ov; const uint8_t *c; } PpRec;   /* a kind 1 record (pp_open, below) */
static int pp_open(const Surf *s, PpRec *r);
/* FUN_180460cd0's base evaluation (eval_surf of the base) and calc_surf_norm, bit for bit (plm_eval / plm_norm: kinds 0, 1; 8 / 10 followed). Until
   2026-11-14 kind 0 went through mcsurf_nrm, whose last bits differ: invisible in the step counts, visible in a kind 8 surface's vertices */

static int b_eval(const McamFile *f, const Surf *b, int order, double u, double v, double D[][3])
{
    return plm_eval(f, b, order, u, v, D);
}
static int b_norm(const McamFile *f, const Surf *b, const double Su[3], const double Sv[3], double u, double v, double N[3])
{
    return plm_norm(f, b, Su, Sv, u, v, N);
}
static int c_eval(Ctx *c, int order, double u, double v, double D[][3]) { return b_eval(c->f, c->s, order, u, v, D); }
static int c_norm(Ctx *c, const double Su[3], const double Sv[3], double u, double v, double N[3]) { return b_norm(c->f, c->s, Su, Sv, u, v, N); }
static int off_eval(const McamFile *f, const Surf *b, double d, int order, double u, double v, double D[][3], const char **err);

/* eval_surf (0x18047ba50) on the kind 8 offset surface FUN_180581f00 builds on the stack (180582091..180582127: the base header copied, +0xb0 the
 * distance XMM6 = param_2 = local_188 * +-1 + local_2b78 = 0.0 (in_stack_40 = in_stack_48 = 0, find_base_surface of a kind 0: 0), +0xb8 the base's eptr,
 * kind byte 8). eval_surf case 8 -> FUN_180460cd0 (core_evalcache.c l.131): base eval_surf at order + 1 (max 3), calc_surf_norm(base, Su, Sv, u, v, N, 0);
 * P = d * N/|N| + B, the derivatives = d * (terms of N/|N|) + B's. With d = 0.0 and |N| finite the products are +-0 and the results are B's own
 * values bit for bit, so B's derivatives are returned; calc_surf_norm's failure (FUN_180460cd0 returns it and eval_surf's output stays unwritten)
 * stops the port. */
static int ev8(Ctx *c, int order, double u, double v, double D[][3])
{
    if (c->err) return -1;
    if (c->use_off) return ev8_off(c, order, u, v, D);
    if (c->base_eval) {                                                       /* plm_model's FUN_18057dd40: eval_surf on the surface itself */
        if (plm_eval(c->f, c->s, order, u, v, D)) { c->err = "make_surf_model: eval_surf of the kind 0 surface failed"; return -1; }
        return 0;
    }
    double B[10][3];
    int o1 = order + 1; if (o1 > 3) o1 = 3;                                   /* FUN_180460cd0: sVar8 = param_2 + 1; if (2 < param_2) sVar8 = 3 */
    if (c_eval(c, o1, u, v, B)) { c->err = "Curve Parting Line mesh: eval_surf of the kind 0 surface failed"; return -1; }
    double N[3];
    if (c_norm(c, B[1], B[2], u, v, N)) { c->err = "Curve Parting Line mesh: calc_surf_norm failed in the offset evaluation (FUN_180460cd0) NOT PORTED"; return -1; }
    double l = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
    if (!(l > 0) || !isfinite(l)) { c->err = "Curve Parting Line mesh: zero normal in the offset evaluation (FUN_180460cd0) NOT PORTED"; return -1; }
    int nd = (order + 1) * (order + 2) / 2;
    memcpy(D, B, sizeof(double) * 3 * (size_t)nd);
    return 0;
}

/* FUN_180460cd0 (core_evalcache.c l.131-880) in full, for make_surf_model's distance d != 0 (plm_model; surf_classic3.md 12.9): base B = eval_surf
 * at order + 1 (max 3), N = calc_surf_norm(Su, Sv), L = |N| (sqrt((x^2 + y^2) + z^2)), n = N / L, P = d n + S; order >= 1: Nu = Suu x Sv + Su x Suv,
 * Nv = Suv x Sv + Su x Svv (l.322-340), nu = (Nu - (n.Nu) n) / L, nv likewise (l.344-412), Pu = d nu + Su, Pv = d nv + Sv; order >= 2: Nuu = 2 Suu x Suv
 * + Su x Suuv + Suuu x Sv, Nvv = 2 Suv x Svv + Su x Svvv + Suvv x Sv, Nuv = Suu x Svv + Su x Suvv + Suuv x Sv (l.448-640), nuu = ((Nuu - (Nuu.n +
 * nu.Nu) n) - 2 (n.Nu) nu) / L, nvv = ((Nvv - (Nvv.n + nv.Nv) n) - 2 (n.Nv) nv) / L, nuv = (((Nuv - (Nuv.n + nv.Nu) n) - (n.Nu) nv) - (n.Nv) nu)
 * / L (l.641-766), Puu / Puv / Pvv = d (..) + Suu / Suv / Svv; order 3: the base's third derivatives copied (l.840ff). The term orders are the
 * decompile's; the distance is find_base_surf's offset of the copy (0 for a kind 0 base) + its +0xb0 (l.271-273). */
static int off_eval(const McamFile *f, const Surf *b, double d, int order, double u, double v, double D[][3], const char **err)
{
    double B[10][3];
    int o1 = order + 1; if (o1 > 3) o1 = 3;
    if (b_eval(f, b, o1, u, v, B)) { *err = "make_surf_model: eval_surf of the surface failed"; return -1; }
    double N[3];
    if (b_norm(f, b, B[1], B[2], u, v, N)) { *err = "make_surf_model: calc_surf_norm failed in the offset evaluation (FUN_180460cd0) NOT PORTED"; return -1; }
    const double *S = B[0], *Su = B[1], *Sv = B[2], *Suu = B[3], *Suv = B[4], *Svv = B[5], *Suuu = B[6], *Suuv = B[7], *Suvv = B[8], *Svvv = B[9];
    double L = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]), n[3];
    for (int k = 0; k < 3; k++) { n[k] = N[k] / L; D[0][k] = d * n[k] + S[k]; }
    if (order == 0) return 0;
    double Nu[3], Nv[3];
    Nu[0] = (Sv[2] * Suu[1] - Sv[1] * Suu[2]) + (Suv[2] * Su[1] - Suv[1] * Su[2]);
    Nu[1] = (Sv[0] * Suu[2] - Sv[2] * Suu[0]) + (Suv[0] * Su[2] - Su[0] * Suv[2]);
    Nu[2] = (Sv[1] * Suu[0] - Sv[0] * Suu[1]) + (Su[0] * Suv[1] - Suv[0] * Su[1]);
    Nv[0] = (Sv[2] * Suv[1] - Sv[1] * Suv[2]) + (Su[1] * Svv[2] - Su[2] * Svv[1]);
    Nv[1] = (Sv[0] * Suv[2] - Sv[2] * Suv[0]) + (Su[2] * Svv[0] - Su[0] * Svv[2]);
    Nv[2] = (Sv[1] * Suv[0] - Sv[0] * Suv[1]) + (Su[0] * Svv[1] - Su[1] * Svv[0]);
    double a3 = n[0] * Nu[0] + n[1] * Nu[1] + n[2] * Nu[2];                  /* dVar3 */
    double a16 = n[1] * Nv[1] + n[0] * Nv[0] + n[2] * Nv[2];                 /* dVar16 */
    double nu[3], nv[3];
    for (int k = 0; k < 3; k++) {
        nu[k] = (Nu[k] - a3 * n[k]) / L; nv[k] = (Nv[k] - a16 * n[k]) / L;
        D[1][k] = d * nu[k] + Su[k]; D[2][k] = d * nv[k] + Sv[k];
    }
    if (order == 1) return 0;
    double X[3], Y[3], Z[3], Nuu[3], Nvv[3], Nuv[3];
    X[0] = Su[1] * Suuv[2] - Su[2] * Suuv[1]; X[1] = Su[2] * Suuv[0] - Su[0] * Suuv[2]; X[2] = Su[0] * Suuv[1] - Su[1] * Suuv[0];
    Y[0] = Suu[1] * Suv[2] - Suu[2] * Suv[1]; Y[1] = Suu[2] * Suv[0] - Suv[2] * Suu[0]; Y[2] = Suv[1] * Suu[0] - Suu[1] * Suv[0];
    Z[0] = Sv[2] * Suuu[1] - Sv[1] * Suuu[2]; Z[1] = Sv[0] * Suuu[2] - Sv[2] * Suuu[0]; Z[2] = Sv[1] * Suuu[0] - Sv[0] * Suuu[1];
    for (int k = 0; k < 3; k++) Nuu[k] = Y[k] * 2.0 + X[k] + Z[k];
    X[0] = Su[1] * Svvv[2] - Su[2] * Svvv[1]; X[1] = Su[2] * Svvv[0] - Su[0] * Svvv[2]; X[2] = Su[0] * Svvv[1] - Su[1] * Svvv[0];
    Y[0] = Suv[1] * Svv[2] - Suv[2] * Svv[1]; Y[1] = Suv[2] * Svv[0] - Suv[0] * Svv[2]; Y[2] = Suv[0] * Svv[1] - Suv[1] * Svv[0];
    Z[0] = Sv[2] * Suvv[1] - Sv[1] * Suvv[2]; Z[1] = Sv[0] * Suvv[2] - Sv[2] * Suvv[0]; Z[2] = Sv[1] * Suvv[0] - Sv[0] * Suvv[1];
    for (int k = 0; k < 3; k++) Nvv[k] = Y[k] * 2.0 + X[k] + Z[k];
    X[0] = Su[1] * Suvv[2] - Su[2] * Suvv[1]; X[1] = Su[2] * Suvv[0] - Su[0] * Suvv[2]; X[2] = Su[0] * Suvv[1] - Su[1] * Suvv[0];
    Y[0] = Suu[1] * Svv[2] - Suu[2] * Svv[1]; Y[1] = Suu[2] * Svv[0] - Svv[2] * Suu[0]; Y[2] = Svv[1] * Suu[0] - Suu[1] * Svv[0];
    Z[0] = Sv[2] * Suuv[1] - Sv[1] * Suuv[2]; Z[1] = Sv[0] * Suuv[2] - Sv[2] * Suuv[0]; Z[2] = Sv[1] * Suuv[0] - Sv[0] * Suuv[1];
    for (int k = 0; k < 3; k++) Nuv[k] = Y[k] + X[k] + Z[k];
    double tuu = Nuu[1] * n[1] + Nuu[0] * n[0] + Nuu[2] * n[2] + nu[1] * Nu[1] + nu[0] * Nu[0] + nu[2] * Nu[2];
    double tvv = Nvv[1] * n[1] + Nvv[0] * n[0] + Nvv[2] * n[2] + nv[1] * Nv[1] + nv[0] * Nv[0] + nv[2] * Nv[2];
    double tuv = Nuv[1] * n[1] + Nuv[0] * n[0] + Nuv[2] * n[2] + nv[1] * Nu[1] + nv[0] * Nu[0] + nv[2] * Nu[2];
    for (int k = 0; k < 3; k++) {
        double nuu = ((Nuu[k] - tuu * n[k]) - a3 * 2.0 * nu[k]) / L;
        double nvv = ((Nvv[k] - tvv * n[k]) - a16 * 2.0 * nv[k]) / L;
        double nuv = (((Nuv[k] - tuv * n[k]) - a3 * nv[k]) - a16 * nu[k]) / L;
        D[3][k] = d * nuu + Suu[k]; D[4][k] = d * nuv + Suv[k]; D[5][k] = d * nvv + Svv[k];
    }
    if (order == 3) for (int e = 6; e < 10; e++) for (int k = 0; k < 3; k++) D[e][k] = B[e][k];
    return 0;
}
static int ev8_off(Ctx *c, int order, double u, double v, double D[][3])
{
    return off_eval(c->f, c->s, 0.0 + c->off, order, u, v, D, &c->err);       /* the copy's distance: find_base_surf of its base (0.0) + its +0xb0 */
}

/* FUN_18058aa60 (core_tess.c l.8188): the idx-th knot span of length > 1e-12 inside [lo, hi]: its start and width */
static int span_of(double lo, double hi, const double *K, int nk, int idx, double *start, double *width)
{
    int i = 0;
    for (int k = 0; k + 1 < nk; k++) {   /* the DLL walks the knots without a bound; the port stops at the vector's end */
        double b = K[k + 1]; if (hi < K[k + 1]) b = hi;
        double a = K[k]; if (hi < K[k]) a = hi;
        if (b < lo) b = lo;
        if (a < lo) a = lo;
        if (1e-12 < fabs(b - a)) {
            if (i == idx) { *start = a; *width = b - a; return 0; }
            i++;
        }
    }
    return -1;
}

/* FUN_180578b30 (core_tess.c l.8385): 0 = the knots are not uniform (*n = the number of spans), 1 = treat them as uniform */
static int knots_uniform(double lo, double hi, int *n, int nk, const double *K)
{
    short cnt = 0; double mn = 0.0, mx = 0.0;
    if (nk - 1 < 1) return 1;
    for (short s = 0; ; s++) {
        double b = K[s + 1], a = K[s];
        if (hi < b) b = hi;
        if (hi < a) a = hi;
        if (b < lo) b = lo;
        if (a < lo) a = lo;
        double d = b - a, ad = fabs(d);
        if (1e-12 < ad) {
            cnt++;
            if (cnt == 1) { mn = ad; mx = ad; }
            else {
                if (ad < mn) mn = ad;
                if (!(ad <= mx)) mx = ad;
            }
        }
        if (nk - 1 <= (int)(short)(s + 1)) {
            if (0 < cnt && (mn + mx) * 0.005 + 1e-12 < mx - mn) { *n = cnt; return 0; }
            return 1;
        }
    }
}

/* FUN_1805869d0 (mesh_asm3.txt 1805869d0): the chord of an arc of curvature k whose sagitta is h */
static double chord_of(double k, double h)
{
    double kk = k;
    if (!(k > 1e-10)) kk = 1e-10;                                             /* COMISD XMM0, [0x180d0d960] = 1e-10; JA */
    double p = kk * h;
    if (fabs(p) > 1.0) return 0.0;
    return sin(acos(1.0 - p)) * 2.0 / kk;                                    /* 0x180b9375a acos, 0x180b930ce sin, * 2.0, / k */
}

/* UnitNormal (0x1804b7480, decomp/wfacc/core_kernels2.c l.2658) */
static void unit_normal(const double a[3], const double b[3], double n[3])
{
    double x = b[2] * a[1] - b[1] * a[2], y = b[0] * a[2] - a[0] * b[2], z = a[0] * b[1] - b[0] * a[1];
    double l = sqrt(x * x + y * y + z * z);
    if (l < 1e-12) l = 1e-12;
    n[0] = x / l; n[1] = y / l; n[2] = z / l;
}

/* surf_curv_dir (0x18047ce10; mesh_asm4.txt 18047ce46..18047cec4): the normal curvature in the parameter direction d.
 * FUN_18047c530: E, F, G; FUN_18047c8b0: L, M, N on UnitNormal(Su, Sv) */
static double surf_curv_dir(const double D[][3], const double d[2])
{
    const double *Su = D[1], *Sv = D[2];
    double E = (Su[1] * Su[1] + Su[0] * Su[0]) + Su[2] * Su[2];
    double F = (Su[1] * Sv[1] + Sv[0] * Su[0]) + Su[2] * Sv[2];
    double G = (Sv[1] * Sv[1] + Sv[0] * Sv[0]) + Sv[2] * Sv[2];
    double n[3]; unit_normal(Su, Sv, n);
    double L = (n[1] * D[3][1] + n[0] * D[3][0]) + n[2] * D[3][2];
    double M = (n[1] * D[4][1] + n[0] * D[4][0]) + n[2] * D[4][2];
    double N = (n[1] * D[5][1] + n[0] * D[5][0]) + n[2] * D[5][2];
    double a = d[0], b = d[1];
    double num = (b * N) * b + ((((M * 2.0) * a) * b) + (a * L) * a);
    double den = ((((F * 2.0) * a) * b) + (a * E) * a) + (b * G) * b;
    return num / den;
}

/* curv (0x1804b7990; mesh_asm4.txt): |d1 x d2| / |d1|^3 */
static double curv3(const double d1[3], const double d2[3])
{
    double X = (d1[0] * d1[0] + d1[1] * d1[1]) + d1[2] * d1[2];
    double cx = d2[1] * d1[2] - d2[2] * d1[1], cy = d2[2] * d1[0] - d2[0] * d1[2], cz = d2[0] * d1[1] - d2[1] * d1[0];
    double s = sqrt((cy * cy + cx * cx) + cz * cz);
    return s / (sqrt(X) * X);
}

static double dist3(const double a[3], const double b[3])   /* sqrt((dx^2 + dy^2) + dz^2): the order every caller here uses (z added last) */
{
    double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrt((dx * dx + dy * dy) + dz * dz);
}

/* FUN_180589880 (mesh_asm3.txt 180589880..180589d83): march from (u, v) by (du, dv) / 10, nine steps; the length walked, and the curvature of the
 * circle through P and the first three points each more than step from the ones before (0 when not found) */
static void march_curv(Ctx *c, const double P[3], double u, double v, double du, double dv, double step, double *curv, double *len)
{
    double prev[3] = { P[0], P[1], P[2] }, A[3] = { 0, 0, 0 }, Bp[3] = { 0, 0, 0 }, r1 = 0.0, r2 = 0.0;
    int b5 = 0, b6 = 0;
    *curv = 0.0; *len = 0.0;
    double ddu = du / 10.0, ddv = dv / 10.0;                                  /* [0x180cfa818] = 10.0 */
    u += ddu; v += ddv;
    for (short k = 0; ; ) {
        double X[10][3]; if (ev8(c, 0, u, v, X)) return;
        double dx = prev[0] - X[0][0], dy = prev[1] - X[0][1], dz = prev[2] - X[0][2];
        *len = sqrt((dy * dy + dx * dx) + dz * dz) + *len;                    /* 1805899c4..180589a17 */
        if (!b5) {
            r1 = dist3(X[0], P);                                              /* 180589a25 */
            if (r1 > step) { b5 = 1; memcpy(A, X[0], sizeof A); }
        } else if (!b6) {
            r2 = dist3(X[0], A);                                              /* 180589aaa */
            if (r2 > step && dist3(X[0], P) > step) { b6 = 1; memcpy(Bp, X[0], sizeof Bp); }
        } else {
            double r3 = dist3(X[0], Bp);                                      /* 180589b34 */
            if (r3 > step && dist3(X[0], A) > step) {                         /* 180589bd6: the two circle estimates, the smaller */
                double a0 = A[0] - P[0], a1 = A[1] - P[1], a2 = A[2] - P[2];
                double b0 = Bp[0] - P[0], b1 = Bp[1] - P[1], b2 = Bp[2] - P[2];
                double c0 = b2 * a1 - b1 * a2, c1 = a2 * b0 - b2 * a0, c2 = b1 * a0 - a1 * b0;
                double k1 = sqrt((c1 * c1 + c0 * c0) + c2 * c2) * 2.0;
                double lb = sqrt((b0 * b0 + b1 * b1) + b2 * b2);
                k1 = k1 / ((lb * r1) * r2);
                double e0 = Bp[0] - A[0], e1 = Bp[1] - A[1], e2 = Bp[2] - A[2];
                double f0 = X[0][0] - A[0], f1 = X[0][1] - A[1], f2 = X[0][2] - A[2];
                double g0 = f2 * e1 - f1 * e2, g1 = e2 * f0 - f2 * e0, g2 = f1 * e0 - e1 * f0;
                double k2 = sqrt((g1 * g1 + g0 * g0) + g2 * g2) * 2.0;
                double lf = sqrt((f0 * f0 + f1 * f1) + f2 * f2);
                k2 = k2 / ((lf * r2) * r3);
                *curv = (k2 > k1) ? k1 : k2;                                  /* COMISD XMM8, XMM7; JA; MOVAPS XMM7, XMM8 */
                return;
            }
        }
        u += ddu; v += ddv;                                                   /* 180589ba7 */
        memcpy(prev, X[0], sizeof prev);
        k++;
        if (k >= 9) return;
    }
}

/* the test FUN_180585d10 / FUN_180585fd0 make before taking the curvature at the point (else they march, FUN_180589880) */
static int needs_march(double step, double chord, double len)
{
    if (step > chord || step > len) return 1;
    if (chord > len && fabs(len - chord) / chord > 0.4) return 1;            /* [0x180cfa7a8] = 0.4 */
    if (len > chord && fabs(len - chord) / chord > 2.0) return 1;
    return 0;
}

/* FUN_180585d10 (mesh_asm2.txt 180585d10..180585fc2): one corner's segment length S and length L along u (k 0) or v (k 1); the ratio L / S kept
 * in *best when larger. The return value (the caller keeps its maximum in [RBP-0x68], overwritten unused at 1805836f9) is not needed. */
static void corner_len(Ctx *c, double D[][3], const double dir[2], double span, int k, double step, double chord, double u, double v,
                       double *best, double *S, double *L)
{
    const double *D1 = D[k + 1];
    double len = sqrt((D1[0] * D1[0] + D1[1] * D1[1]) + D1[2] * D1[2]);
    double sl = len * span;
    double Sr, Lr = chord;
    if (needs_march(step, chord, sl)) {                                      /* 180585eb4 */
        double cv, ln; march_curv(c, D[0], u, v, span * dir[0], span * dir[1], step, &cv, &ln);
        if (c->err) return;
        Sr = chord_of(cv, step); if (step * 2.0 > Sr) Sr = step * 2.0;
        Lr = ln;
    } else {                                                                  /* 180585e0a */
        double acd = fabs(surf_curv_dir(D, dir)), kk = acd;
        double cvv = curv3(D1, D[2 * k + 3]);
        if (cvv > acd) { double l2 = sqrt((D1[0] * D1[0] + D1[1] * D1[1]) + D1[2] * D1[2]); if (l2 > step) kk = cvv; }
        Sr = chord_of(kk, step); if (step * 2.0 > Sr) Sr = step * 2.0;
        if (sl > chord) Lr = sl;
    }
    double r = Lr / Sr;                                                       /* 180585f70 */
    if (r > *best) { *best = r; *S = Sr; *L = Lr; }
}

/* FUN_180585fd0 (mesh_asm2.txt 180585fd0..180586875): the diagonal direction (a, b) at the four corners A, B, C, D; the largest L / S given to
 * the u or the v slot when it beats both */
static void diag_len(Ctx *c, double A[][3], double B[][3], double C[][3], double D[][3], double a, double b, double step, double chord1, double chord2,
                     double u, double v, double *bestU, double *SU, double *LU, double *bestV, double *SV, double *LV)
{
    double d[2];
    double h = sqrt(a * a + b * b);
    if (h == 0.0) { d[0] = 0.0; d[1] = 0.0; } else { d[0] = a / h; d[1] = b / h; }
    #define DLEN(P) do { double lu_ = sqrt((P[1][0] * P[1][0] + P[1][1] * P[1][1]) + P[1][2] * P[1][2]) * a; \
                         double lv_ = sqrt((P[2][0] * P[2][0] + P[2][1] * P[2][1]) + P[2][2] * P[2][2]) * b; \
                         len = sqrt(lv_ * lv_ + lu_ * lu_); } while (0)
    double len, best, Sb, Lb, two = step * 2.0, cv, ln, k, Lc;
    /* A, direction (a, b) from (u, v) */
    DLEN(A);
    if (needs_march(step, chord1, len)) {
        march_curv(c, A[0], u, v, a, b, step, &cv, &ln); if (c->err) return;
        k = chord_of(cv, step); if (two > k) k = two; Lc = ln;
    } else {
        k = chord_of(fabs(surf_curv_dir(A, d)), step); if (two > k) k = two;
        Lc = chord1; if (len > chord1) Lc = len;
    }
    Sb = k; Lb = Lc; best = Lc / k;                                           /* 180586247: XMM11, XMM12, [RBP+0x60] */
    d[0] = -d[0];
    /* B, direction (-a, b) from (u + a, v) */
    DLEN(B);
    if (needs_march(step, chord2, len)) {
        march_curv(c, B[0], a + u, v, -a, b, step, &cv, &ln); if (c->err) return;
        k = chord_of(cv, step); if (two > k) k = two; Lc = ln;
    } else {
        k = chord_of(fabs(surf_curv_dir(B, d)), step); if (two > k) k = two;
        Lc = chord2; if (len > chord2) Lc = len;
    }
    if (Lc / k > best) { Lb = Lc; best = Lc / k; Sb = k; }                    /* 180586385 */
    d[0] = -d[0]; d[1] = -d[1];
    /* C, direction (a, -b) from (u, v + b) */
    DLEN(C);
    if (needs_march(step, chord2, len)) {
        march_curv(c, C[0], u, b + v, a, -b, step, &cv, &ln); if (c->err) return;
        k = chord_of(cv, step); if (two > k) k = two; Lc = ln;
    } else {
        k = chord_of(fabs(surf_curv_dir(C, d)), step); if (two > k) k = two;
        Lc = chord2; if (len > chord2) Lc = len;
    }
    if (Lc / k > best) { best = Lc / k; Sb = k; Lb = Lc; } else Lc = Lb;     /* 1805864c8 / 1805865e8: XMM7 = the best's L */
    d[0] = -d[0];
    /* D, direction (-a, -b) from (u + a, v + b) */
    DLEN(D);
    if (needs_march(step, chord1, len)) {
        march_curv(c, D[0], a + u, b + v, -a, -b, step, &cv, &ln); if (c->err) return;
        k = chord_of(cv, step); if (two > k) k = two; Lc = ln;
    } else {
        k = chord_of(fabs(surf_curv_dir(D, d)), step); if (two > k) k = two;
        Lc = chord1; if (len > chord1) Lc = len;
    }
    if (Lc / k > best) { best = Lc / k; Lb = Lc; Sb = k; }                    /* 1805867a3 */
    #undef DLEN
    if (best > 1e-05 && best > *bestU && best > *bestV) {                     /* 1805867d4 */
        double *Sp, *Lp;
        if (*bestU > *bestV) { *bestU = best; Sp = SU; Lp = LU; }
        else { *bestV = best; Sp = SV; Lp = LV; }
        double s = *Lp / best;
        *Sp = s;
        if (1e-05 > s) { *Sp = Sb; *Lp = Lb; }
    }
}

/* FUN_18059a450 (mesh_asm3.txt 18059a450..18059b170): the middle one of the nu x nv sub-cells of the inset cell; nu or nv grown until its two
 * triangles hold the mid points (and the sub-cell's centre) within tol */
static int flat_cells(Ctx *c, double tol, double u0, double du, double v0, double dv, short *pnu, short *pnv)
{
    short nu = *pnu, nv = *pnv;
    double uc = du * 0.5 + u0, vc = dv * 0.5 + v0;                           /* [RBP+0x1a0], [RBP-0x30] */
    #define BIG(w) (fabs((w)[0]) > tol || fabs((w)[1]) > tol || fabs((w)[2]) > tol)
    #define NRM(w) do { double l_ = sqrt(((w)[0] * (w)[0] + (w)[1] * (w)[1]) + (w)[2] * (w)[2]); \
                        if (l_ == 0.0) (w)[0] = (w)[1] = (w)[2] = 0.0; else { (w)[0] /= l_; (w)[1] /= l_; (w)[2] /= l_; } } while (0)
    #define DOTN(Q, O, n) ((((Q)[1] - (O)[1]) * (n)[1] + ((Q)[0] - (O)[0]) * (n)[0]) + ((Q)[2] - (O)[2]) * (n)[2])
    for (int guard = 0; ; guard++) {
        if (guard > 100000 || nu <= 0 || nv <= 0) { c->err = "Curve Parting Line mesh: FUN_18059a450 did not settle (short overflow) NOT PORTED"; return -1; }
        int flat = 1, b7 = 0, b8 = 0;
        double hu = du / (double)(int)nu, hv = dv / (double)(int)nv;
        double hu2 = hu * 0.5, au = uc - hu2;
        double hv2 = hv * 0.5, av = vc - hv2;
        double P00[10][3], P10[10][3], P01[10][3], P11[10][3], PC[10][3], Q[10][3];
        if (ev8(c, 0, au, av, P00)) return -1;
        double bu = au + hu; if (ev8(c, 0, bu, av, P10)) return -1;
        double bv = av + hv; if (ev8(c, 0, au, bv, P01)) return -1;
        if (ev8(c, 0, bu, bv, P11)) return -1;
        double cv = hv2 + av, cu = hu2 + au;
        if (ev8(c, 0, cu, cv, PC)) return -1;
        double n1[3] = { 0, 0, 0 }, n2[3] = { 0, 0, 0 };
        double e1[3] = { P10[0][0] - P00[0][0], P10[0][1] - P00[0][1], P10[0][2] - P00[0][2] };
        if (BIG(e1)) {                                                        /* 18059a6ec */
            double e2[3] = { P01[0][0] - P00[0][0], P01[0][1] - P00[0][1], P01[0][2] - P00[0][2] };
            if (BIG(e2)) {                                                    /* 18059a7ba */
                NRM(e1); NRM(e2);
                n1[0] = e2[2] * e1[1] - e2[1] * e1[2]; n1[1] = e2[0] * e1[2] - e2[2] * e1[0]; n1[2] = e2[1] * e1[0] - e2[0] * e1[1];
                if (BIG(n1)) {                                                /* 18059a94a */
                    b7 = 1; NRM(n1);
                    double dd = DOTN(P11[0], P00[0], n1);
                    if (!(tol < fabs(dd))) goto check;                        /* 18059aa62: BL = 0, to 18059adb4 */
                }
            }
        }
        {                                                                     /* 18059aaaa: the triangle P11, P01, P10 */
            double f1[3] = { P01[0][0] - P11[0][0], P01[0][1] - P11[0][1], P01[0][2] - P11[0][2] };
            if (BIG(f1)) {
                double f2[3] = { P10[0][0] - P11[0][0], P10[0][1] - P11[0][1], P10[0][2] - P11[0][2] };
                if (BIG(f2)) {
                    NRM(f1); NRM(f2);
                    n2[0] = f2[2] * f1[1] - f2[1] * f1[2]; n2[1] = f2[0] * f1[2] - f2[2] * f1[0]; n2[2] = f2[1] * f1[0] - f2[0] * f1[1];
                    if (BIG(n2)) { b8 = 1; NRM(n2); }
                }
            }
        }
    check:                                                                    /* 18059adab */
        if (b7 && fabs(DOTN(PC[0], P00[0], n1)) > tol) flat = 0;
        if (b8 && fabs(DOTN(PC[0], P11[0], n2)) > tol) goto refine;          /* 18059ae9e */
        if (flat) { *pnu = nu; *pnv = nv; return 0; }                         /* 18059aea5 -> 18059b150 */
    refine: {
        double dU = 0.0, dV = 0.0, t;
        if (ev8(c, 0, cu, av, Q)) return -1;                                 /* 18059aecf: the bottom edge's middle */
        if (b7) dU = fabs(DOTN(Q[0], P00[0], n1));
        if (b8) { t = fabs(DOTN(Q[0], P11[0], n2)); if (t > dU) dU = t; }
        if (ev8(c, 0, au, cv, Q)) return -1;                                 /* 18059afcc: the left edge's middle */
        if (b7) dV = fabs(DOTN(Q[0], P00[0], n1));
        if (b8) { t = fabs(DOTN(Q[0], P11[0], n2)); if (t > dV) dV = t; }
        if ((dU > tol || dV > tol) && (dU > dV * 100.0 || dV > dU * 100.0)) { /* 18059b0ae; [0x180cf5578] = 100.0 */
            if (dU > dV) nu++; else nv++;                                     /* 18059b145 */
        } else if (nu < nv) nu++; else nv++;                                  /* 18059b0de */
        }
    }
    #undef BIG
    #undef NRM
    #undef DOTN
}

/* dist_pt_l_3d (0x180013bd0, mesh_curv.c l.3) */
static double dist_pt_line(const Ctx *c, const double p[3], const double s[3], const double e[3])
{
    double dx = e[0] - s[0], dy = e[1] - s[1], dz = e[2] - s[2];
    double cx = (p[1] - s[1]) * dz - (p[2] - s[2]) * dy;
    double cy = (p[2] - s[2]) * dx - dz * (p[0] - s[0]);
    double cz = (p[0] - s[0]) * dy - (p[1] - s[1]) * dx;
    double l = sqrt(dx * dx + dy * dy + dz * dz);
    if (fabs(l) <= c->tol)
        return sqrt((s[1] - p[1]) * (s[1] - p[1]) + (s[0] - p[0]) * (s[0] - p[0]) + (s[2] - p[2]) * (s[2] - p[2]));
    return fabs(sqrt(cy * cy + cx * cx + cz * cz) / l);
}

/* FUN_180578990 (mesh_asm3.txt 180578990..180578b1d): the points p3, p4 on the line p2-p5 (or p2 = p5) and p7, p8 on p6-p9 (or p6 = p9), within t */
static int on_lines(const Ctx *c, double t, const double p2[3], const double p3[3], const double p4[3], const double p5[3],
                    const double p6[3], const double p7[3], const double p8[3], const double p9[3])
{
    if (fabs(p2[0] - p5[0]) > t || fabs(p2[1] - p5[1]) > t || fabs(p2[2] - p5[2]) > t) {
        if (dist_pt_line(c, p3, p2, p5) > t) return 0;
        if (dist_pt_line(c, p4, p2, p5) > t) return 0;
    }
    if (!(fabs(p6[0] - p9[0]) > t || fabs(p6[1] - p9[1]) > t || fabs(p6[2] - p9[2]) > t)) return 1;
    if (dist_pt_line(c, p7, p6, p9) > t) return 0;
    if (dist_pt_line(c, p8, p6, p9) > t) return 0;
    return 1;
}

/* FUN_180581f00 (tess_asm.txt 180581f00..180583477 the cells, mesh_asm2.txt 1805834ad..18058515f the totals and the merge pass), called at
 * 18057b444 with CL = 1, XMM1 = 0.0 (the offset), R8 = the ent, XMM3 = [0x20] = step, words 0, 0, nsu, nsv, the u / v knots (or NULL),
 * the box u0 u1 v0 v1 (XMM8..XMM11 = the base surf +8..+0x20, 18057ae69), Su, Sv, Lu, Lv (filled 1.0 / 0.0 at 18057b20b..18057b3a0).
 * param_6 = param_7 = 0: the FUN_18059c680 branches are not taken. */
static int steps_581f00(Ctx *c, double stepu, double stepv, int nsu, int nsv, const double *uk, int nuk, const double *vk, int nvk,
                        double u0b, double u1b, double v0b, double v1b, double *Su, double *Sv, double *Lu, double *Lv)
{
    const uint8_t *r = c->s->rec;
    double U0 = rdd(r + 0x70), U1 = rdd(r + 0x78), V0 = rdd(r + 0x80), V1 = rdd(r + 0x88);   /* the kind 8 copy's +8..+0x20 = [RBP+0x268..0x280] */
    double du = (U1 - U0) / (double)nsu;                                      /* 18058213f: [RBP+0x110] */
    double dv = (V1 - V0) / (double)nsv;                                      /* 18058217f: [RBP-0x40] */
    double ucur = U0;                                                         /* XMM13 */
    for (int i = 0; i < nsu; i++) {
        if (uk && span_of(U0, U1, uk, nuk, i, &ucur, &du)) { c->err = "Curve Parting Line mesh: FUN_18058aa60 ran out of u knots NOT DECODED"; return -1; }
        double vcur = V0;                                                     /* 180582276 */
        for (int j = 0; j < nsv; j++) {
            if (vk && span_of(V0, V1, vk, nvk, j, &vcur, &dv)) { c->err = "Curve Parting Line mesh: FUN_18058aa60 ran out of v knots NOT DECODED"; return -1; }
            /* 180582337: the cell against the box */
            if (u1b < ucur || ucur + du < u0b || v1b < vcur || vcur + dv < v0b) { vcur += dv; continue; }
            double Cn[10][3];
            if (ev8(c, 1, du * 0.5 + ucur, dv * 0.5 + vcur, Cn)) return -1;  /* 1805823c3 */
            double lu = sqrt((Cn[1][0] * Cn[1][0] + Cn[1][1] * Cn[1][1]) + Cn[1][2] * Cn[1][2]), eu = lu;
            if (lu > 1e-06) eu = stepu / lu;
            if (9.999999999999999e-06 > eu) eu = 9.999999999999999e-06;
            if (eu > du / 3.0) eu = du / 3.0;
            double lv = sqrt((Cn[2][0] * Cn[2][0] + Cn[2][1] * Cn[2][1]) + Cn[2][2] * Cn[2][2]), ev = lv;
            if (lv > 1e-06) ev = stepv / lv;
            if (9.999999999999999e-06 > ev) ev = 9.999999999999999e-06;
            if (ev > dv / 3.0) ev = dv / 3.0;
            double uA = eu + ucur, vA = ev + vcur, uB = (ucur + du) - eu, vC = (vcur + dv) - ev;
            double A[10][3], B[10][3], C[10][3], D[10][3];
            if (ev8(c, 2, uA, vA, A) || ev8(c, 2, uB, vA, B) || ev8(c, 2, uA, vC, C) || ev8(c, 2, uB, vC, D)) return -1;   /* 18058254c..1805825f2 */
            /* 180582628..1805826ca: calc_surf_unit_norm at the corners -> [RBP+0x228] .. read only by FUN_18059c680 (param_6 / param_7 != 0): skipped */
            double dAB = dist3(B[0], A[0]), dCD = dist3(D[0], C[0]), dAC = dist3(C[0], A[0]), dBD = dist3(D[0], B[0]);
            double bestU = 0.0, SU = 1.0, LU = 0.0, bestV = 0.0, SV = 1.0, LV = 0.0;
            double dir[2] = { 1.0, 0.0 };
            double spu = du - eu * 2.0, spv = dv - ev * 2.0;                  /* 18058299e, 180582dbf */
            corner_len(c, A, dir, spu, 0, stepu, dAB, uA, vA, &bestU, &SU, &LU);
            corner_len(c, C, dir, spu, 0, stepu, dCD, uA, vC, &bestU, &SU, &LU);
            dir[0] = -dir[0];
            corner_len(c, B, dir, spu, 0, stepu, dAB, uB, vA, &bestU, &SU, &LU);
            corner_len(c, D, dir, spu, 0, stepu, dCD, uB, vC, &bestU, &SU, &LU);
            dir[0] = 0.0; dir[1] = 1.0;                                       /* 180582d74 */
            corner_len(c, A, dir, spv, 1, stepv, dAC, uA, vA, &bestV, &SV, &LV);
            corner_len(c, B, dir, spv, 1, stepv, dBD, uB, vA, &bestV, &SV, &LV);
            dir[1] = -dir[1];
            corner_len(c, C, dir, spv, 1, stepv, dAC, uA, vC, &bestV, &SV, &LV);
            corner_len(c, D, dir, spv, 1, stepv, dBD, uB, vC, &bestV, &SV, &LV);
            if (c->err) return -1;
            diag_len(c, A, B, C, D, spu, spv, stepu, dist3(D[0], A[0]), dist3(C[0], B[0]), uA, vA, &bestU, &SU, &LU, &bestV, &SV, &LV);   /* 180583287 */
            if (c->err) return -1;
            if (bestV > Lv[j] / Sv[j]) { Sv[j] = SV; Lv[j] = LV; }            /* 18058328c */
            if (bestU > Lu[i] / Su[i]) { Su[i] = SU; Lu[i] = LU; }
            short nU = (short)cvtt(ceil(Lu[i] / Su[i])), nV = (short)cvtt(ceil(Lv[j] / Sv[j]));   /* 1805832ef: FUN_18058f140 = ceil */
            short nU2 = nU, nV2 = nV;
            if (flat_cells(c, stepu, uA, spu, vA, spv, &nU2, &nV2)) return -1;   /* 18058335e */
            if (nU < nU2) Su[i] = Lu[i] / (double)(int)nU2 + stepu;          /* 180583367 */
            if (nV < nV2) Sv[j] = Lv[j] / (double)(int)nV2 + stepv;          /* 18058338f */
            vcur += dv;                                                       /* 180583444 */
        }
        ucur += du;                                                           /* 18058348a */
    }

    /* 1805834ad..1805836c2: the totals over the spans in the box: ceil(sum L / max S) (max S below 1e-5: 1.0) */
    double w = (U1 - U0) / (double)nsu, st = U0, sumL = 0.0, maxS = 0.0;
    for (int i = 0; i < nsu; i++) {
        if (uk) span_of(U0, U1, uk, nuk, i, &st, &w);
        if (!(u1b < st) && !(st + w < u0b)) { sumL = sumL + Lu[i]; if (Su[i] > maxS) maxS = Su[i]; }
        st += w;
    }
    if (nsu <= 0 || 1e-05 > maxS) maxS = 1.0;
    int totU = cvtt(ceil(sumL / maxS));                                        /* 1805835ce: [RBP+8], R12D */
    w = (V1 - V0) / (double)nsv; st = V0; sumL = 0.0; maxS = 0.0;
    for (int j = 0; j < nsv; j++) {
        if (vk) span_of(V0, V1, vk, nvk, j, &st, &w);
        if (!(v1b < st) && !(st + w < v0b)) { sumL = sumL + Lv[j]; if (Sv[j] > maxS) maxS = Sv[j]; }
        st += w;
    }
    if (nsv <= 0 || 1e-05 > maxS) maxS = 1.0;
    int totV = cvtt(ceil(sumL / maxS));                                        /* 1805836c2: R15D */

    /* 1805836da..18058444a: neighbouring u spans of one segment each merged while the merged strip (over the whole v box) stays within step */
    double dvb = v1b - v0b;                                                   /* [RBP-0x68] */
    {
        double wd = (U1 - U0) / (double)nsu, s0 = U0, pw = 0.0, accL = 0.0, minS = 0.0, mw = 0.0, ep = 0.0, dm1 = 0.0, dm3 = 0.0;
        int on = 0;                                                           /* R14B */
        for (int i = 0; i < nsu; i++) {
            if (uk) span_of(U0, U1, uk, nuk, i, &s0, &wd);
            if (i > 0 && !(s0 + wd < u0b) && !(u1b < s0)) {
                short n0 = (short)cvtt(ceil(Lu[i - 1] / Su[i - 1])), n1 = (short)cvtt(ceil(Lu[i] / Su[i]));
                if (n0 == 1 && n1 == n0) {
                    double E1[10][3], E2[10][3], E3[10][3], E4[10][3], M1[10][3], M2[10][3], M3[10][3], M4[10][3];
                    if (on) {                                                 /* 180583800: one more span on the merged strip */
                        double Ls = Lu[i] + accL;
                        if (minS > Ls && Su[i] > Ls) {
                            double ec = wd * 0.05, ev5 = dvb * 0.05, vlo = ev5 + v0b;
                            double ust = s0 - mw, ulo = ust + ep, uhi = (s0 + wd) - ec, vhi = (dvb + v0b) - ev5;
                            if (ev8(c, 2, ulo, vlo, E1) || ev8(c, 2, uhi, vlo, E2) || ev8(c, 2, ulo, vhi, E3) || ev8(c, 2, uhi, vhi, E4)) return -1;
                            double um1 = s0 - mw * 0.5, um2 = ec + s0;
                            if (ev8(c, 0, um1, vlo, M1) || ev8(c, 0, um2, vlo, M2) || ev8(c, 0, um1, vhi, M3) || ev8(c, 0, um2, vhi, M4)) return -1;
                            double d1 = dist3(E4[0], E1[0]), d2 = dist3(E3[0], E2[0]);
                            double Lsum = accL + Lu[i], Smin = Su[i]; if (!(minS > Su[i])) Smin = minS;
                            double ratio = Lsum / Smin, bV = 0.0, SVx = 1.0, LVx = 0.0;
                            diag_len(c, E1, E2, E3, E4, ((mw - ep) + wd) - ec, dvb - ev5 * 2.0, stepu, d1, d2, ust + ec, vlo,
                                     &ratio, &Smin, &Lsum, &bV, &SVx, &LVx);
                            if (c->err) return -1;
                            double dm2 = dist3(M2[0], E2[0]), dm4 = dist3(M4[0], E4[0]);
                            if (!(fabs(dm1 - dm2) > stepu) && !(fabs(dm3 - dm4) > stepu) && !((short)cvtt(ceil(ratio)) > (short)totV)
                                && on_lines(c, stepu, E1[0], M1[0], M2[0], E2[0], E3[0], M3[0], M4[0], E4[0])) {
                                if (!(Su[i] > minS)) minS = Su[i];            /* 180583d49 */
                                accL = accL + Lu[i];
                                mw = mw + wd;
                            } else {
                                Su[i] = accL * 0.95 + Lu[i];                  /* 180583d79: [0x180d11288] = 0.95 */
                                on = 0;
                            }
                        } else on = 0;                                        /* 180583d98 */
                    } else {                                                  /* 180583da0: the first pair */
                        double Ls = Lu[i] + Lu[i - 1];
                        if (Su[i - 1] > Ls && Su[i] > Ls) {
                            double ep0 = pw * 0.05, ec = wd * 0.05, ev5 = dvb * 0.05, vlo = ev5 + v0b;
                            double ust = s0 - pw, ulo = ust + ep0, uhi = (s0 + wd) - ec, vhi = (dvb + v0b) - ev5;
                            ep = ep0;                                         /* [RBP+0xd8] */
                            if (ev8(c, 2, ulo, vlo, E1) || ev8(c, 2, uhi, vlo, E2) || ev8(c, 2, ulo, vhi, E3) || ev8(c, 2, uhi, vhi, E4)) return -1;
                            double um1 = s0 - ep0, um2 = ec + s0;
                            if (ev8(c, 0, um1, vlo, M1) || ev8(c, 0, um2, vlo, M2) || ev8(c, 0, um1, vhi, M3) || ev8(c, 0, um2, vhi, M4)) return -1;
                            double d1 = dist3(E4[0], E1[0]), d2 = dist3(E3[0], E2[0]);
                            double Lsum = Lu[i - 1] + Lu[i], Smin = Su[i]; if (!(Su[i - 1] > Su[i])) Smin = Su[i - 1];
                            double ratio = Lsum / Smin, bV = 0.0, SVx = 1.0, LVx = 0.0;
                            diag_len(c, E1, E2, E3, E4, ((pw - ep0) + wd) - ec, dvb - ev5 * 2.0, stepu, d1, d2, ust + ec, vlo,
                                     &ratio, &Smin, &Lsum, &bV, &SVx, &LVx);   /* 1805841b5 */
                            if (c->err) return -1;
                            dm1 = dist3(M1[0], E1[0]); dm3 = dist3(M3[0], E3[0]);   /* [RBP+0x118], [RBP+0x128] */
                            double dm2 = dist3(M2[0], E2[0]), dm4 = dist3(M4[0], E4[0]);
                            if (!(fabs(dm1 - dm2) > stepu) && !(fabs(dm3 - dm4) > stepu) && !((short)cvtt(ceil(ratio)) > (short)totV)
                                && on_lines(c, stepu, E1[0], M1[0], M2[0], E2[0], E3[0], M3[0], M4[0], E4[0])) {
                                minS = Su[i - 1]; if (!(Su[i] > minS)) minS = Su[i];   /* 180584392 */
                                accL = Lu[i - 1] + Lu[i];
                                mw = wd + pw;
                                on = 1;
                            } else
                                Su[i - 1] = Lu[i] * 0.95 + Lu[i - 1];        /* 1805843da */
                        }
                    }
                }
            }
            pw = wd;                                                          /* 1805843fe */
            s0 += wd;
        }
    }
    /* 18058444a..18058515f: the same along v (the strip over the whole u box; the counts compared with totU) */
    {
        double dub = u1b - u0b;                                               /* 180584470: [RBP+0x6e0] */
        double wd = (V1 - V0) / (double)nsv, s0 = V0, pw = 0.0, accL = 0.0, minS = 0.0, mw = 0.0, ep = 0.0, dm1 = 0.0, dm2s = 0.0;
        int on = 0;                                                           /* SIL */
        for (int j = 0; j < nsv; j++) {
            if (vk) span_of(V0, V1, vk, nvk, j, &s0, &wd);
            if (j > 0 && !(s0 + wd < v0b) && !(v1b < s0)) {
                short n0 = (short)cvtt(ceil(Lv[j - 1] / Sv[j - 1])), n1 = (short)cvtt(ceil(Lv[j] / Sv[j]));
                if (n0 == 1 && n1 == n0) {
                    double E1[10][3], E2[10][3], E3[10][3], E4[10][3], M1[10][3], M2[10][3], M3[10][3], M4[10][3];
                    if (on) {                                                 /* 18058456c */
                        double Ls = Lv[j] + accL;
                        if (minS > Ls && Sv[j] > Ls) {
                            double eu5 = dub * 0.05, ec = wd * 0.05, vst = s0 - mw, vlo = vst + ep, ulo = eu5 + u0b;
                            double vhi = (s0 + wd) - ec, uhi = (dub + u0b) - eu5;
                            if (ev8(c, 2, ulo, vlo, E1) || ev8(c, 2, ulo, vhi, E3) || ev8(c, 2, uhi, vlo, E2) || ev8(c, 2, uhi, vhi, E4)) return -1;
                            double vm1 = s0 - mw * 0.5, vm2 = ec + s0;
                            if (ev8(c, 0, ulo, vm1, M1) || ev8(c, 0, uhi, vm1, M2) || ev8(c, 0, ulo, vm2, M3) || ev8(c, 0, uhi, vm2, M4)) return -1;
                            double d1 = dist3(E4[0], E1[0]), d2 = dist3(E3[0], E2[0]);
                            double Lsum = accL + Lv[j], Smin = Sv[j]; if (!(minS > Sv[j])) Smin = minS;
                            double ratio = Lsum / Smin, bU = 0.0, SUx = 1.0, LUx = 0.0;
                            diag_len(c, E1, E2, E3, E4, dub - eu5 * 2.0, ((mw - ep) + wd) - ec, stepu, d1, d2, ulo, vst + ec,
                                     &bU, &SUx, &LUx, &ratio, &Smin, &Lsum);   /* 180584962 */
                            if (c->err) return -1;
                            double dm3 = dist3(M3[0], E3[0]), dm4 = dist3(M4[0], E4[0]);
                            if (!(fabs(dm1 - dm3) > stepu) && !(fabs(dm2s - dm4) > stepu) && !((short)cvtt(ceil(ratio)) > (short)totU)
                                && on_lines(c, stepu, E1[0], M1[0], M3[0], E3[0], E2[0], M2[0], M4[0], E4[0])) {
                                if (!(Sv[j] > minS)) minS = Sv[j];            /* 180584a9c */
                                accL = accL + Lv[j];
                                mw = mw + wd;
                            } else {
                                Sv[j] = accL * 0.95 + Lv[j];                  /* 180584acd */
                                on = 0;
                            }
                        } else on = 0;                                        /* 180584ae4 */
                    } else {                                                  /* 180584aec */
                        double Ls = Lv[j] + Lv[j - 1];
                        if (Sv[j - 1] > Ls && Sv[j] > Ls) {
                            double eu5 = dub * 0.05, ec = wd * 0.05, ep0 = pw * 0.05, vst = s0 - pw, vlo = vst + ep0, ulo = eu5 + u0b;
                            double vhi = (s0 + wd) - ec, uhi = (dub + u0b) - eu5;
                            ep = ep0;                                         /* [RBP+0xe0] */
                            if (ev8(c, 2, ulo, vlo, E1) || ev8(c, 2, ulo, vhi, E3) || ev8(c, 2, uhi, vlo, E2) || ev8(c, 2, uhi, vhi, E4)) return -1;
                            double vm1 = s0 - ep0, vm2 = ec + s0;
                            if (ev8(c, 0, ulo, vm1, M1) || ev8(c, 0, uhi, vm1, M2) || ev8(c, 0, ulo, vm2, M3) || ev8(c, 0, uhi, vm2, M4)) return -1;
                            double d1 = dist3(E4[0], E1[0]), d2 = dist3(E3[0], E2[0]);
                            double Lsum = Lv[j] + Lv[j - 1], Smin = Sv[j]; if (!(Sv[j - 1] > Sv[j])) Smin = Sv[j - 1];
                            double ratio = Lsum / Smin, bU = 0.0, SUx = 1.0, LUx = 0.0;
                            diag_len(c, E1, E2, E3, E4, dub - eu5 * 2.0, ((pw - ep0) + wd) - ec, stepu, d1, d2, ulo, vst + ec,
                                     &bU, &SUx, &LUx, &ratio, &Smin, &Lsum);   /* 180584ef1 */
                            if (c->err) return -1;
                            dm1 = dist3(M1[0], E1[0]); dm2s = dist3(M2[0], E2[0]);   /* [RBP+0x118], [RBP+0x128] */
                            double dm3 = dist3(M3[0], E3[0]), dm4 = dist3(M4[0], E4[0]);
                            if (!(fabs(dm1 - dm3) > stepu) && !(fabs(dm2s - dm4) > stepu) && !((short)cvtt(ceil(ratio)) > (short)totU)
                                && on_lines(c, stepu, E1[0], M1[0], M3[0], E3[0], E2[0], M2[0], M4[0], E4[0])) {
                                minS = Sv[j - 1]; if (!(Sv[j] > minS)) minS = Sv[j];   /* 1805850cb */
                                accL = Lv[j] + Lv[j - 1];
                                mw = wd + pw;
                                on = 1;
                            } else
                                Sv[j - 1] = Lv[j] * 0.95 + Lv[j - 1];        /* 18058510f */
                        }
                    }
                }
            }
            pw = wd;                                                          /* 180585127 */
            s0 += wd;
        }
    }
    return 0;
}

/* FUN_18058e2a0 (mesh_asm3.txt 18058e2a0..18058e4b9): neighbouring one-segment spans whose lengths add up below both segment lengths: the earlier
 * one's count set to 0 (merged into the next point). Called at 18057ba6c / 18057bbc9 with (counts, L, S, n, knots, hi, lo, the surface's start,
 * box lo, box hi). */
static void merge_counts(short *cnt, const double *L, const double *S, int n, const double *K, int nk, double hi, double lo, double start,
                         double blo, double bhi)
{
    double w = (hi - lo) / (double)n, acc = 0.0, mn = 0.0;
    int on = 0;
    for (int i = 0; i < n; i++) {
        if (K) span_of(lo, hi, K, nk, i, &start, &w);
        if (i > 0 && !(start + w < blo) && !(bhi < start) && cnt[i - 1] == 1 && cnt[i] == 1) {
            if (on) {
                double t = acc + L[i];
                if (mn > t && S[i] > t) { cnt[i - 1] = 0; if (!(S[i] > mn)) mn = S[i]; acc = acc + L[i]; }
                else on = 0;
            } else {
                double t = L[i] + L[i - 1];
                if (S[i - 1] > t && S[i] > t) { cnt[i - 1] = 0; mn = S[i - 1]; if (!(S[i] > mn)) mn = S[i]; acc = L[i] + L[i - 1]; on = 1; }
            }
        }
        start = start + w;
    }
}

/* FUN_18057e440 (mesh_asm3.txt 18057e440..18057e626): the parameters, each span cut in its count of equal parts; out[0] = lo, out[tot] = hi */
static void params_of(int n, const double *K, int nk, const short *cnt, double lo, double hi, double blo, double bhi, int tot, double *out)
{
    if (n == 0) return;
    out[0] = lo;
    double w = (hi - lo) / (double)n, x = lo;
    int k = 1;
    for (int i = 0; i < n; i++) {
        if (K) span_of(lo, hi, K, nk, i, &x, &w);
        if (cnt[i] <= 0) x = x + w;
        else { double h = w / (double)(int)cnt[i]; for (int m = 0; m < cnt[i]; m++) { x = x + h; out[k++] = x; } }
    }
    out[tot] = hi;
    x = out[0];
    for (int i = 0; i < n; i++) {
        if (K) span_of(lo, hi, K, nk, i, &x, &w);
        double e = w + x;
        if (blo > e) out[0] = e;
        if (x > bhi) { out[tot] = x; return; }
        x = e;
    }
}

/* FUN_180599580 (core_tess.c l.5880): interior parameters closer than 2e-10 to a neighbour removed */
static void dedup(int *n, double *p)
{
    for (;;) {
        int i = 1;
        for (;;) {
            if (*n <= i) return;
            if (p[i] < p[i - 1] + 2e-10 || p[i + 1] < p[i] + 2e-10) break;
            i++;
        }
        for (int m = i + 1; m < *n + 1; m++) p[m - 1] = p[m];
        *n -= 1;
    }
}

/* nurb_interval (0x1805b0630, mesh_basis.c): the knot span of t (clamped to the vector), a zero-length span moved down to the last knot below t */
static int nurb_span(double t, const double *K, int nk, int deg)
{
    if (!K) return 0;
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
        if (0 < j) {
            for (;;) {
                r = j;
                if (!(t <= K[l] + 1e-10)) break;
                l--; j--;
                if (l < 1) return i1;
            }
        }
    }
    return r;
}

/* FUN_1805b06f0 (mesh_asm5.txt): the basis functions N[span - deg .. span] at t (Cox - de Boor in place, absolute indices; a quotient over a span
 * shorter than 1e-12 or at t within 1e-12 of its knot is 0; a value below 1e-8 is set to 0) */
static void basis_const(double t, const double *K, int nk, int span, int deg, double *N)
{
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

/* init_surf_const (0x18047c600) kind 0 -> FUN_1805b2280(nurbs, 1, u0, v0, &DAT_1813f20b0, buf): the u basis kept for the last u (redone when u moves
 * by 1e-12 or more); eval_surf_const (0x18047bd50) kind 0 -> FUN_1805b1aa0 (mesh_curv.c l.898): the v basis, sum Nu[i] Nv[j] P[j][i] (v outer,
 * u inner), / w when rational (|w| <= 1e-12: not divided). Numerically not eval_surf (FUN_1805b08d0): the 1e-8 cut above, so its own port. */
typedef struct { const double *KU, *KV; const uint8_t *cp; int nku, nkv, ncu, ncv, vpp, du, dv, nonrat; int have; double u; int uspan; double *Nu, *Nv; } SConst;
static int sconst_init(SConst *c, const Surf *s)
{
    memset(c, 0, sizeof *c);
    const uint8_t *r = s->rec;
    c->nku = rd32(r + 0x100); c->nkv = rd32(r + 0x104); c->ncu = rd32(r + 0x108); c->ncv = rd32(r + 0x10c);
    c->vpp = r[0x110]; c->du = r[0x111]; c->dv = r[0x112]; c->nonrat = r[0x113] & 1;
    if (c->vpp < 1 || c->vpp > 4 || c->nku < c->du + 2 || c->nkv < c->dv + 2 || c->ncu < 1 || c->ncv < 1
        || 0x118 + 8 * ((size_t)c->nku + c->nkv + (size_t)c->vpp * c->ncu * c->ncv) > s->n) return -1;
    double *k = malloc(sizeof(double) * ((size_t)c->nku + c->nkv));
    for (int i = 0; i < c->nku + c->nkv; i++) k[i] = rdd(r + 0x118 + 8 * (size_t)i);
    c->KU = k; c->KV = k + c->nku; c->cp = r + 0x118 + 8 * ((size_t)c->nku + c->nkv);
    c->Nu = calloc((size_t)c->nku + 2, sizeof(double)); c->Nv = calloc((size_t)c->nkv + 2, sizeof(double));
    return 0;
}
static void sconst_free(SConst *c) { free((void *)c->KU); free(c->Nu); free(c->Nv); memset(c, 0, sizeof *c); }
static void sconst_eval(SConst *c, double u, double v, double out[3])
{
    if (!c->have || !(fabs(u - c->u) < 1e-12)) {                              /* FUN_1805b2280 */
        c->have = 1; c->u = u;
        c->uspan = nurb_span(u, c->KU, c->nku, c->du);
        basis_const(u, c->KU, c->nku, c->uspan, c->du, c->Nu);
    }
    int vs = nurb_span(v, c->KV, c->nkv, c->dv);                              /* FUN_1805b1aa0 */
    basis_const(v, c->KV, c->nkv, vs, c->dv, c->Nv);
    double acc[4] = { 0, 0, 0, 0 };
    int i0 = c->uspan - c->du; if (i0 < 0) i0 = 0;
    int j0 = vs - c->dv; if (j0 < 0) j0 = 0;
    for (int j = j0; j <= vs; j++) {
        if (!(j < c->ncv)) continue;
        for (int i = i0; i <= c->uspan; i++) {
            if (!(i < c->ncu)) continue;
            double w = c->Nu[i] * c->Nv[j];
            const uint8_t *q = c->cp + 8 * (size_t)c->vpp * ((size_t)c->ncu * j + i);
            for (int k = 0; k < c->vpp; k++) acc[k] = rdd(q + 8 * (size_t)k) * w + acc[k];
        }
    }
    /* p_3d::operator[] (FUN_18000dd10): index 2 -> z, 1 -> y, anything else -> x */
    #define IX(k) ((k) == 2 ? 2 : (k) == 1 ? 1 : 0)
    if (!c->nonrat) {
        if (fabs(acc[0]) <= 1e-12) { for (int k = 0; k + 2 <= c->vpp; k++) out[IX(k)] = acc[k + 1]; }
        else { for (int k = 0; k + 2 <= c->vpp; k++) out[IX(k)] = acc[k + 1] / acc[0]; }
    } else for (int k = 0; k < c->vpp; k++) out[IX(k)] = acc[k];
    #undef IX
}

#include "mcpartline_trim.inc"
#include "mcpartline_trim2.inc"
#include "mcpartline_trim3.inc"

int plm_mesh(const McamFile *f, const Surf *s, double step, PlMesh *m, const char **why)
{
    memset(m, 0, sizeof *m);
    if (why) *why = NULL;
    #define FAIL(msg) do { if (why) *why = (msg); goto fail; } while (0)
    short *cu = NULL, *cvn = NULL; double *Su = NULL, *Sv = NULL, *Lu = NULL, *Lv = NULL, *pu = NULL, *pv = NULL, *KU = NULL, *KV = NULL;
    PltTrim T; memset(&T, 0, sizeof T);
    const Surf *s10 = NULL;
    if (s && s->kind == 10 && s->rec) {                                       /* find_base_surface (18057ad98): the base of a trimmed surface */
        const Surf *b = mcam_surf_base(f, s);
        if (!b || b->kind != 0) { if (why) *why = "trimmed surface mesh: base other than an untrimmed NURBS (kind 0) NOT PORTED"; return -1; }
        if (!(s->rec[0x6e] & 0x40)) { if (why) *why = "trimmed surface mesh: eval_surf refuses the surface (+6 bit 0x40 clear) NOT PORTED"; return -1; }
        s10 = s; s = b;
    }
    if (!s || s->kind != 0 || !s->rec || s->n < 0x118) {
        if (why) *why = "Curve Parting Line mesh: make_surf_polygons for a surface other than an untrimmed NURBS (kind 0) NOT PORTED";
        return -1;
    }
    if (!(step > 0)) { if (why) *why = "Curve Parting Line mesh: step <= 0 (FUN_18057a980's other branch) NOT PORTED"; return -1; }
    Ctx c; memset(&c, 0, sizeof c); c.f = f; c.s = s; c.tol = mc_systol.tol;
    const uint8_t *r = s->rec;
    double tol01 = step * 0.1;                                                 /* in_stack_90 = step x 0.1 (make_surf_polygons) */
    double u0b = rdd(r + 0x70), u1b = rdd(r + 0x78), v0b = rdd(r + 0x80), v1b = rdd(r + 0x88);   /* 18057ae69: the base surf +8..+0x20 */
    if (s10) {
        const char *w = NULL;
        int k = plt_read(&c, s10, tol01, &T, &w);                             /* FUN_180591650 (18057ae4b) */
        if (k < 0) { if (why) *why = w ? w : c.err; plt_free(&T); return -1; }
        if (k == 0) { plt_free(&T); return 0; }                               /* no loop: make_surf_polygons returns no triangles */
        plt_dump("4", &T);
        u0b = T.ub[0]; u1b = T.ub[1]; v0b = T.vb[0]; v1b = T.vb[1];            /* 18057ae8d..18057af09: the loops' box */
        for (int l = 1; l < T.nl; l++) {
            if (u0b > T.ub[2 * l]) u0b = T.ub[2 * l];
            if (T.ub[2 * l + 1] > u1b) u1b = T.ub[2 * l + 1];
            if (v0b > T.vb[2 * l]) v0b = T.vb[2 * l];
            if (T.vb[2 * l + 1] > v1b) v1b = T.vb[2 * l + 1];
        }
    }
    /* get_evaluation_grid (0x18058a4f0) case 0: (ncu - 1) / degree spans each way, or the knot spans when they are not uniform (FUN_180578b30); at least 2 */
    int nku = rd32(r + 0x100), nkv = rd32(r + 0x104), ncu = rd32(r + 0x108), ncv = rd32(r + 0x10c), degu = r[0x111], degv = r[0x112];
    if (degu == 0 || degv == 0 || nku < 2 || nkv < 2 || 0x118 + 8 * ((size_t)nku + nkv) > s->n)
        FAIL("Curve Parting Line mesh: get_evaluation_grid on a bad NURBS record (the DLL asserts) NOT PORTED");
    double U0 = rdd(r + 0x70), U1 = rdd(r + 0x78), V0 = rdd(r + 0x80), V1 = rdd(r + 0x88);
    KU = malloc(sizeof(double) * nku); KV = malloc(sizeof(double) * nkv);
    for (int i = 0; i < nku; i++) KU[i] = rdd(r + 0x118 + 8 * (size_t)i);
    for (int i = 0; i < nkv; i++) KV[i] = rdd(r + 0x118 + 8 * ((size_t)nku + i));
    int nsu = (ncu - 1) / degu, nsv = (ncv - 1) / degv;
    const double *uk = KU, *vk = KV;
    if (knots_uniform(U0, U1, &nsu, nku, KU)) uk = NULL;
    if (knots_uniform(V0, V1, &nsv, nkv, KV)) vk = NULL;
    if (nsu < 2) nsu = 2;
    if (nsv < 2) nsv = 2;
    Su = malloc(sizeof(double) * nsu); Lu = malloc(sizeof(double) * nsu); Sv = malloc(sizeof(double) * nsv); Lv = malloc(sizeof(double) * nsv);
    cu = malloc(sizeof(short) * nsu); cvn = malloc(sizeof(short) * nsv);
    for (int i = 0; i < nsu; i++) { Lu[i] = 0.0; Su[i] = 1.0; }              /* 18057b20b..18057b2be */
    for (int j = 0; j < nsv; j++) { Lv[j] = 0.0; Sv[j] = 1.0; }
    if (steps_581f00(&c, step, step, nsu, nsv, uk, nku, vk, nkv, u0b, u1b, v0b, v1b, Su, Sv, Lu, Lv)) { if (why) *why = c.err; goto fail; }

    /* 18057b941..18057bbef: the counts ceil(L / S); none at all: S[0] = max(S[0], tol), L[0] = S[0]; FUN_18058e2a0 merges */
    int totu = 0, totv = 0;
    for (int i = 0; i < nsu; i++) cu[i] = (short)cvtt(ceil(Lu[i] / Su[i]));
    { int sum = 0; for (int i = 0; i < nsu; i++) sum += cu[i];
      if (sum < 1) { if (c.tol > Su[0]) Su[0] = c.tol; Lu[0] = Su[0]; for (int i = 0; i < nsu; i++) cu[i] = (short)cvtt(ceil(Lu[i] / Su[i])); } }
    /* FUN_18058e2a0's start (18057ba25 / 18057bb85: [R13 + 8] / [R13 + 0x18], R13 = the entity's own surf_type ent +0x68): a trimmed surface's
       own range (+0x70 / +0x80, its loops' box), not the base's; it counts only for uniform spans (span_of recomputes it otherwise) */
    double ust = s10 ? rdd(s10->rec + 0x70) : U0, vst = s10 ? rdd(s10->rec + 0x80) : V0;
    merge_counts(cu, Lu, Su, nsu, uk, nku, U1, U0, ust, u0b, u1b);
    for (int i = 0; i < nsu; i++) totu += cu[i];
    for (int j = 0; j < nsv; j++) cvn[j] = (short)cvtt(ceil(Lv[j] / Sv[j]));
    { int sum = 0; for (int j = 0; j < nsv; j++) sum += cvn[j];
      if (sum < 1) { if (c.tol > Sv[0]) Sv[0] = c.tol; Lv[0] = Sv[0]; for (int j = 0; j < nsv; j++) cvn[j] = (short)cvtt(ceil(Lv[j] / Sv[j])); } }
    merge_counts(cvn, Lv, Sv, nsv, vk, nkv, V1, V0, vst, v0b, v1b);
    for (int j = 0; j < nsv; j++) totv += cvn[j];
    if (totu < 1 || totv < 1) FAIL("Curve Parting Line mesh: no segments after the merge NOT DECODED");
    /* 18057bc8c..18057bce2: FUN_18057e440 the parameters, FUN_180599580 the near duplicates out */
    pu = malloc(sizeof(double) * (totu + 2 + T.nl)); pv = malloc(sizeof(double) * (totv + 2 + T.nl));   /* 18057b769: + loops (local_2c60 + 1) */
    params_of(nsu, uk, nku, cu, U0, U1, u0b, u1b, totu, pu);
    params_of(nsv, vk, nkv, cvn, V0, V1, v0b, v1b, totv, pv);
    dedup(&totu, pu); dedup(&totv, pv);
    if (s10) {
        if (plt_loops(&c, &T, tol01, u0b, u1b, v0b, v1b, &totu, &totv, pu, pv) < 0) { if (why) *why = c.err ? c.err : "trimmed surface mesh: loops NOT PORTED"; goto fail; }
        plt_dump("0", &T);
        if (plt_mesh(&c, &T, totu, totv, pu, pv, m)) { if (why) *why = c.err ? c.err : "trimmed surface mesh NOT PORTED"; goto fail; }
        free(KU); free(KV); free(Su); free(Sv); free(Lu); free(Lv); free(cu); free(cvn); free(pu); free(pv);
        plt_free(&T);
        return m->ntri;
    }

    /* the grid points (18057c9ec..18057cb10: u outer, v inner, FUN_18057a510 -> eval_surf_const (init_surf_const 18057c8c4 succeeds for kind 0) and
       the (u, v) into the uv array), the two triangles per cell (18057d0f0..18057d1b0: b, b + nv + 1, b + nv + 2 and b, b + nv + 2, b + 1) */
    int nu = totu, nv = totv;
    m->nv = (nu + 1) * (nv + 1); m->ntri = 2 * nu * nv;
    m->xyz = malloc(sizeof(double) * 3 * (size_t)m->nv); m->uv = malloc(sizeof(double) * 2 * (size_t)m->nv); m->tri = malloc(sizeof(int) * 3 * (size_t)m->ntri);
    {
        SConst sc;
        if (sconst_init(&sc, s)) FAIL("Curve Parting Line mesh: init_surf_const on a bad NURBS record NOT PORTED");
        for (int i = 0, k = 0; i <= nu; i++) for (int j = 0; j <= nv; j++, k++) {
            sconst_eval(&sc, pu[i], pv[j], m->xyz + 3 * (size_t)k);    /* FUN_18057a510 (param_4 = local_2d78 = 1) -> eval_surf_const */
            m->uv[2 * (size_t)k] = pu[i]; m->uv[2 * (size_t)k + 1] = pv[j];   /* FUN_18057a510 LAB_18057a908: param_20[k] = (u, v) */
        }
        sconst_free(&sc);
    }
    for (int i = 0, t = 0, b = 0; i < nu; i++, b++) for (int j = 0; j < nv; j++, b++) {
        m->tri[t++] = b; m->tri[t++] = b + nv + 1; m->tri[t++] = b + nv + 2;
        m->tri[t++] = b; m->tri[t++] = b + nv + 2; m->tri[t++] = b + 1;
    }
    free(KU); free(KV); free(Su); free(Sv); free(Lu); free(Lv); free(cu); free(cvn); free(pu); free(pv);
    return m->ntri;
fail:
    free(KU); free(KV); free(Su); free(Sv); free(Lu); free(Lv); free(cu); free(cvn); free(pu); free(pv);
    plt_free(&T);
    plm_free(m);
    return -1;
    #undef FAIL
}
void plm_free(PlMesh *m) { free(m->tri); free(m->xyz); free(m->uv); memset(m, 0, sizeof *m); }

/* ---- make_surf_model (mccore 0x18058dc30, surf_classic3.md 12.9): FUN_18057a980(-1, -1, 0, 1, f1, f2, f3, d, h, c, d2, h2, r, 0, 1, ent, tol,
 * tol, 0, 0, &mms, ..) (asm 18058dc30..18058de1a; in FUN_18057a980's frame in_stack_28..38 = the frame, 40 = d, 48 = h, 50 = c, 58 / 60 = d2 / h2,
 * 68 = r, 70 = 0 (no uv), 78 = 1 (normals), 80 = ent, 88 = 90 = tol, 98 = a0 = 0, d0 = 3 (boundary edges), param_4 = 1 (rot_view)). ---- */

static void rot_view3(double p[3], const double *F)   /* rot_view (0x180593a20, core_tess.c l.6609): (y f.y + x f.x) + z f.z per row */
{
    double x = p[0], y = p[1], z = p[2];
    p[0] = y * F[1] + x * F[0] + z * F[2];
    p[1] = y * F[4] + x * F[3] + z * F[5];
    p[2] = y * F[7] + x * F[6] + z * F[8];
}

/* FUN_18057a510 (core_tess.c l.1800) with param_6 = 1 (in_stack_78): eval_surf order 1, calc_surf_unit_norm, both through rot_view (param_8 =
 * local_2d60 = 1); 0 ok */
static int model_vertex(Ctx *c, const double *F, double u, double v, double P[3], double N[3])
{
    double E[10][3];
    const Surf *es = c->hdr ? c->hdr : c->s;                                 /* the entity itself (FUN_18057a510 gets local_2d00): eval_surf follows a
                                                                              trimmed surface to its base (case 10) and evaluates an offset one (case 8) */
    if (plm_eval(c->f, es, 1, u, v, E)) { c->err = "make_surf_model: eval_surf of the surface failed"; return -1; }
    plm_unit_norm(c->f, es, E[1], E[2], u, v, N);                           /* its failure leaves N = 0 (calc_surf_unit_norm), not tested here */
    P[0] = E[0][0]; P[1] = E[0][1]; P[2] = E[0][2];
    rot_view3(P, F); rot_view3(N, F);
    return 0;
}

/* FUN_1805858b0 (msm4.c l.3; FUN_18058e180 passes P1, P2, du, dv, step, chord = |P2 - P1|, surf, u, v - asm 18058e1d0..18058e22b): the L / S
 * ratio of a segment from both ends, as corner_len but along d = (du, dv) / |(du, dv)| from P1 and -d from P2 (the march from P2 starts at
 * (du + u, dv + v) going (-du, -dv)); chord_of takes |surf_curv_dir| (ANDPS at 180585a8e / 180585bd3) */
static double seg_ratio(Ctx *c, double P1[][3], double P2[][3], double du, double dv, double step, double chord, double u, double v)
{
    double h = sqrt(du * du + dv * dv), d[2];
    if (h == 0.0) { d[0] = 0.0; d[1] = 0.0; } else { d[0] = du / h; d[1] = dv / h; }
    double best = 0.0;
    for (int e = 0; e < 2; e++) {
        double (*P)[3] = e ? P2 : P1;
        double lu = sqrt(P[1][0] * P[1][0] + P[1][1] * P[1][1] + P[1][2] * P[1][2]);
        double lv = sqrt(P[2][0] * P[2][0] + P[2][1] * P[2][1] + P[2][2] * P[2][2]);
        double len = sqrt(lv * dv * lv * dv + lu * du * lu * du);
        double S, L;
        if (needs_march(step, chord, len)) {
            double cv, ln;
            if (e == 0) march_curv(c, P[0], u, v, du, dv, step, &cv, &ln);
            else march_curv(c, P[0], du + u, dv + v, -du, -dv, step, &cv, &ln);
            if (c->err) return -1.0;
            S = chord_of(cv, step); if (S < step * 2.0) S = step * 2.0;
            L = ln;
        } else {
            S = chord_of(fabs(surf_curv_dir(P, d)), step); if (S < step * 2.0) S = step * 2.0;
            L = chord; if (chord < len) L = len;
        }
        double r = L / S;
        if (e == 0 || best < r) best = r;
        d[0] = -d[0]; d[1] = -d[1];
    }
    return best;
}

/* FUN_18057dd40 (msm2.c l.127) as FUN_18057a980 calls it on an untrimmed surface's grid outline (asm 18057c5bb: CL = 1, XMM1 = tol, one loop):
 * after each point, the segment to the next (the last to the first) evaluated at 1 % and 99 % (eval_surf order 2), FUN_18058e180 (|du|, |dv|
 * of those two parameters, asm 18057e0a3..18057e0b1): n = 1 when the ratio <= 1.02 else ceil; n - 1 points (q - p) (i / n) + p inserted.
 * The arena's room (edge_fit(0x10) less the points) bounds the insertions in the DLL: not modelled (as plm_mesh). 0 ok. */
static int refine_outline(Ctx *c, double tol, double **lp, int *n)
{
    const double *p = *lp; int np = *n, cap = np * 4 + 16, m = 0;
    double *o = malloc(sizeof(double) * 2 * (size_t)cap);
    int sv_off = c->use_off, sv_base = c->base_eval; c->use_off = 0; c->base_eval = 1;
    for (int i = 0; i < np; i++) {
        int j = i != np - 1 ? i + 1 : 0;
        if (m + 1 > cap) { cap *= 2; o = realloc(o, sizeof(double) * 2 * (size_t)cap); }
        o[2 * m] = p[2 * i]; o[2 * m + 1] = p[2 * i + 1]; m++;
        double u1 = (p[2 * j] - p[2 * i]) * 0.01 + p[2 * i], v1 = (p[2 * j + 1] - p[2 * i + 1]) * 0.01 + p[2 * i + 1];
        double u9 = (p[2 * j] - p[2 * i]) * 0.99 + p[2 * i], v9 = (p[2 * j + 1] - p[2 * i + 1]) * 0.99 + p[2 * i + 1];
        double E1[10][3], E2[10][3];
        if (ev8(c, 2, u1, v1, E1) || ev8(c, 2, u9, v9, E2)) { free(o); c->use_off = sv_off; c->base_eval = sv_base; return -1; }
        double ch = dist3(E2[0], E1[0]);                                      /* FUN_18058e180: sqrt((dx^2 + dy^2) + dz^2), P2 - P1 */
        double r = seg_ratio(c, E1, E2, fabs(u9 - u1), fabs(v9 - v1), tol, ch, u1, v1);
        if (c->err) { free(o); c->use_off = sv_off; c->base_eval = sv_base; return -1; }
        int k = r <= 1.02 ? 1 : (short)cvtt(ceil(r));
        for (int t = 1; t < k; t++) {
            if (m + 1 > cap) { cap *= 2; o = realloc(o, sizeof(double) * 2 * (size_t)cap); }
            o[2 * m] = (p[2 * j] - p[2 * i]) * ((double)t / (double)k) + p[2 * i];
            o[2 * m + 1] = (p[2 * j + 1] - p[2 * i + 1]) * ((double)t / (double)k) + p[2 * i + 1];
            m++;
        }
    }
    c->use_off = sv_off; c->base_eval = sv_base;
    free(*lp); *lp = o; *n = m;
    return 0;
}

/* the cell loop of FUN_18057a980 (18057cf80.., disassembly 18057d11e..18057d634) as far as the boundary edges (in_stack_d0 = 3): cells u
   outer, v inner, b = i (nv + 1) + j the cell's first grid vertex (the stored grid, *in_stack_e8 != 0). No trim loop (local_2d7c = 0): per
   cell on the grid's border FUN_18059c040 for its outer sides, in the order bottom (j = 0), right (i = nu - 1), top (j = nv - 1), left
   (i = 0), with the indices and parameters the call sites pass (18057d3e0, 18057d4bd, 18057d583, 18057d62f - the right side's second u is
   u[i], not u[i + 1]: as the DLL passes it). With the outline loop (local_2d7c = 1): the border cells go through FUN_1805913b0 and
   FUN_180592030 (18057d153..18057d315, as make_surf_polygons' trimmed cells, mcpartline_trim3.inc) whose ears write the loop's edges; the
   inner cells write none. 0 ok. */
static int model_edges(Ctx *c, PlModel *m)
{
    int nu = m->nu, nv = m->nv, ng = (nu + 1) * (nv + 1);
    const double *pu = m->u, *pv = m->v;
    int ltb[2] = { 0, m->nlp - 1 };
    EdgeOut eo; memset(&eo, 0, sizeof eo);
    eo.P = m->pts; eo.N = m->nrm; eo.tol = mc_systol.tol; eo.tb = ltb; eo.nl = 1; eo.base = ng;
    Cell C; memset(&C, 0, sizeof C);
    int *tri = NULL, ntri = 0, cap = 0, ret = 0;
    if (m->nl) {
        C.p = m->lp; C.tb = ltb; C.nl = 1; C.base = ng;
        C.mk = calloc((size_t)m->nlp + 1, sizeof(short));
        C.cap = m->nlp + 4;
        C.op = malloc(sizeof(double) * 2 * (size_t)(C.cap + 1)); C.oi = malloc(sizeof(int) * (size_t)(C.cap + 1));
        C.pt = malloc(sizeof(int) * 2 * (size_t)(C.cap + 8));
    }
    for (int i = 0; i < nu && !ret; i++)
        for (int j = 0; j < nv; j++) {
            int b = i * (nv + 1) + j;
            int inner = 0 < i && i < nu - 1 && 0 < j && j < nv - 1;
            if (!m->nl) {
                if (inner) continue;
                if (j == 0) edge_rec(&eo, b, b + nv + 1, b + nv + 2, pu[i], pv[0], pu[i + 1], pv[0]);
                if (i == nu - 1) edge_rec(&eo, b + nv + 1, b + nv + 2, b, pu[i + 1], pv[j], pu[i], pv[j + 1]);
                if (j == nv - 1) edge_rec(&eo, b + nv + 2, b + 1, b, pu[i + 1], pv[j + 1], pu[i], pv[j + 1]);
                if (i == 0) edge_rec(&eo, b + 1, b, b + nv + 2, pu[0], pv[j + 1], pu[0], pv[j]);
            } else if (!inner) {
                double R[8] = { pu[i], pv[j], pu[i + 1], pv[j], pu[i + 1], pv[j + 1], pu[i], pv[j + 1] };
                C.R = R; C.ci[0] = b; C.ci[1] = b + nv + 1; C.ci[2] = b + nv + 2; C.ci[3] = b + 1;
                int npc = cell_clip(&C, 0);
                if (cell_tris(&C, npc, &tri, &ntri, &cap, &eo)) {
                    c->err = "make_surf_model edges: an ear touching a point while not strict (FUN_180599690 / FUN_1805999f0) NOT PORTED"; ret = -1; break;
                }
            }
        }
    free(C.mk); free(C.op); free(C.oi); free(C.pt); free(tri);
    if (ret) { free(eo.e); return -1; }
    m->edge = eo.e; m->nedge = eo.ne;
    return 0;
}

/* fill_mach_model_buffer's triangles for the whole grid as one block (mccore 18057e9c0 -> FUN_18057ebd0, msm2.c / msm1.c l.479): cells u
   outer, v inner; a cell on the grid's border of a model with the outline loop (mms +0x5c != 0) goes through FUN_18057ece0 (FUN_1805913b0
   with the loop table / points, nloops = mms +0x5c, outer = mms +0x60 = 0, base = the block's grid count +0x64) and FUN_18057eeb0
   (mcpartline_trim3.inc cell_tris_fill; FUN_1806fb650 it may call only draws); every other cell FUN_18057f3c0(buf, 1, 0): (b, b + nv + 1, b + nv + 2),
   (b, b + nv + 2, b + 1). The triangle store's room (+0x60) only splits the work into calls: not modelled. 0 ok. */
int plm_model_tris(const PlModel *m, int **tri, int *ntri, const char **why)
{
    int nu = m->nu, nv = m->nv, ng = (nu + 1) * (nv + 1), cap = 0, ret = 0;
    int ltb[2] = { 0, m->nlp - 1 };
    *tri = NULL; *ntri = 0;
    Cell C; memset(&C, 0, sizeof C);
    if (m->nl) {
        C.p = m->lp; C.tb = m->tb ? m->tb : ltb; C.nl = m->tb ? m->nl : 1; C.base = ng;
        C.mk = calloc((size_t)m->nlp + 1, sizeof(short));
        C.cap = m->nlp + 4;
        C.op = malloc(sizeof(double) * 2 * (size_t)(C.cap + 1)); C.oi = malloc(sizeof(int) * (size_t)(C.cap + 1));
        C.pt = malloc(sizeof(int) * 2 * (size_t)(C.cap + 8));
    }
    for (int i = 0; i < nu && !ret; i++)
        for (int j = 0; j < nv; j++) {
            int b = i * (nv + 1) + j;
            /* FUN_18057ebd0 (msm1.c l.479): with loops (mms +0x5c != 0) the cell goes to FUN_18057ece0 / FUN_18057eeb0 when the surface is a
               trimmed one (**(buf +8) == 10: the ent's kind byte) or the cell is on the grid's border */
            if (m->nl && (m->tb || !(0 < i && i < nu - 1 && 0 < j && j < nv - 1))) {
                double R[8] = { m->u[i], m->v[j], m->u[i + 1], m->v[j], m->u[i + 1], m->v[j + 1], m->u[i], m->v[j + 1] };
                C.R = R; C.ci[0] = b; C.ci[1] = b + nv + 1; C.ci[2] = b + nv + 2; C.ci[3] = b + 1;
                int npc = cell_clip(&C, m->tb ? m->outer : 0);                /* FUN_18057ece0: FUN_1805913b0 with outer = mms +0x60 */
                if (cell_tris_fill(&C, npc, tri, ntri, &cap)) {
                    if (why) *why = "fill_mach_model_buffer: an ear touching a point while not strict (FUN_180599690 / FUN_1805999f0) NOT PORTED"; ret = -1; break;
                }
                continue;
            }
            if (*ntri + 2 > cap) { cap = cap ? 2 * cap : 256; *tri = realloc(*tri, sizeof(int) * 3 * (size_t)cap); }
            int *t = *tri + 3 * (size_t)*ntri;
            t[0] = b; t[1] = b + nv + 1; t[2] = b + nv + 2; t[3] = b; t[4] = b + nv + 2; t[5] = b + 1;
            *ntri += 2;
        }
    free(C.mk); free(C.op); free(C.oi); free(C.pt);
    if (ret) { free(*tri); *tri = NULL; *ntri = 0; }
    return ret;
}

/* make_surf_model on a trimmed surface (kind 10 over a kind 0 base) with d < tol and h < tol - the HST machine model (mcmsurf FUN_18012a310 ->
   FUN_180224190 -> FUN_180229ba0 with d = h = 0, formats/hst_linker.md 20): FUN_18057a980 takes for it the arm make_surf_polygons takes on kind 10
   (plm_mesh, formats/interfacial.md 12.8) with these differences, all from FUN_18057a980's arguments (make_surf_model 18058dc30, msm1.c):
   - in_stack_90 = tol (make_surf_polygons: step x 0.1): FUN_180591650's and the loop refinement's tolerance (core_tess.c l.356, plt_read /
     plt_loops);
   - FUN_18057dd40(0, tol, ..) on the trim loops runs only when tol <= in_stack_40 = d (core_tess.c l.958-961): not here (refused otherwise);
   - the steps: tol <= d, h false -> one FUN_180581f00 pass on set B with side 1 (plm_model's else arm: local_2d87 = 1, local_2d86 = 0), the
     kind 8 copy at distance +local_188 = 3h + d plus local_2b78 = 0 (a kind 0 base); mode 1 (LAB_18057ced1, d and h < tol);
   - the vertex pass: FUN_18057a510 with param_6 = 1 on every grid point then every loop point (core_tess.c l.1261 / l.1304): init_surf_const
     has no kind 10 case (local_2d78 = 0), so eval_surf order 1 of the base + calc_surf_unit_norm, through the frame (rot_view); the frame box
     mms +8..+0x30 reset to +-1e38 (l.1234-1240) and widened by a point outside it; a grid point (param_12 = 0, asm 18057c9e6) not when it
     lies outside the loops (in_polygon(nloops, ..) == -1, FUN_18057a510 l.1866-1903), a loop point (param_12 = 1, asm 18057cb79) always;
   - mms +0x50 = the triangles FUN_180592030 makes in the cell loop (iVar26, l.1706 / l.1751: kind 10 keeps the count of the cells; in_stack_98 = 0
     lets the cell loop run), with in_stack_d0 = 3 the ears' boundary edges (ear_edge); +0x5c = the loop count, +0x60 = the outer loop.
   No loop (FUN_180591650 finds none): no triangles, mms +0x50 = 0 (FUN_180229ba0 then skips the surface, l.364707). */
/* the surface kinds the model port evaluates bit for bit (plm_eval / plm_norm): 0 NURBS, 1 parametric */
static const Surf *base_of(const McamFile *f, const Surf *s, double *acc);
static int model_kind(const Surf *s)
{
    if (s->kind == 0) return s->n >= 0x118;
    if (s->kind == 1) { PpRec p; return pp_open(s, &p) == 0; }
    return 0;
}

/* get_evaluation_grid (0x18058a4f0, core_tess.c l.3126): the spans FUN_180581f00 walks. Case 0: (ncu - 1) / degree each way, or the knot spans
   when they are not uniform (FUN_180578b30 = knots_uniform; *param_4 / *param_5 the knot vectors, NULL when uniform); case 1: the patch counts
   D+0 / D+4 (each at least 2), the knot pointers left at the caller's NULL (msm1.c l.1053-1054: uniform spans). At least 2 spans. 0 ok */
static int eval_grid(const Surf *s, double **KU, double **KV, int *nku, int *nkv, const double **uk, const double **vk, int *nsu, int *nsv)
{
    const uint8_t *r = s->rec;
    *KU = *KV = NULL; *uk = *vk = NULL; *nku = *nkv = 0;
    if (s->kind == 1) {
        *nsu = rd32(r + 0x100); if (*nsu < 2) *nsu = 2;
        *nsv = rd32(r + 0x104); if (*nsv < 2) *nsv = 2;
        return 0;
    }
    int ku = rd32(r + 0x100), kv = rd32(r + 0x104), ncu = rd32(r + 0x108), ncv = rd32(r + 0x10c), degu = r[0x111], degv = r[0x112];
    if (degu == 0 || degv == 0 || ku < 2 || kv < 2 || 0x118 + 8 * ((size_t)ku + kv) > s->n) return -1;
    *nku = ku; *nkv = kv;
    *KU = malloc(sizeof(double) * ku); *KV = malloc(sizeof(double) * kv);
    for (int i = 0; i < ku; i++) (*KU)[i] = rdd(r + 0x118 + 8 * (size_t)i);
    for (int i = 0; i < kv; i++) (*KV)[i] = rdd(r + 0x118 + 8 * ((size_t)ku + i));
    *nsu = (ncu - 1) / degu; *nsv = (ncv - 1) / degv;
    *uk = *KU; *vk = *KV;
    if (knots_uniform(rdd(r + 0x70), rdd(r + 0x78), nsu, ku, *KU)) *uk = NULL;
    if (knots_uniform(rdd(r + 0x80), rdd(r + 0x88), nsv, kv, *KV)) *vk = NULL;
    if (*nsu < 2) *nsu = 2;
    if (*nsv < 2) *nsv = 2;
    return 0;
}

static int model_trim(const McamFile *f, const Surf *s10, const PlModelIn *in, PlModel *m, const char **why)
{
    double lb78 = 0.0;
    const Surf *s = base_of(f, s10, &lb78);                                   /* find_base_surface (0x1805819f0): through kind 10 and 8, local_2b78 */
    if (!s || !s->rec || (s->kind != 0 && s->kind != 1)) { if (why) *why = "make_surf_model on a trimmed surface: base other than kind 0 (NURBS) / 1 (parametric) or an offset of them NOT PORTED"; return -1; }
    if (!(s10->rec[0x6e] & 0x40)) { if (why) *why = "make_surf_model on a trimmed surface: eval_surf refuses the surface (+6 bit 0x40 clear) NOT PORTED"; return -1; }
    if (!model_kind(s)) { if (why) *why = "make_surf_model on a trimmed surface: bad base record"; return -1; }
    if (!(in->d < in->tol && in->h < in->tol))
        { if (why) *why = "make_surf_model on a trimmed surface with tol <= d or h (FUN_18057dd40(0, ..) on the trim loops, core_tess.c l.958) NOT PORTED"; return -1; }
    #define TFAIL(msg) do { if (why && !*why) *why = (msg); goto tfail; } while (0)
    short *cu = NULL, *cvn = NULL; double *Su = NULL, *Sv = NULL, *Lu = NULL, *Lv = NULL, *pu = NULL, *pv = NULL, *KU = NULL, *KV = NULL;
    int *tri = NULL; Cell C; memset(&C, 0, sizeof C);
    PltTrim T; memset(&T, 0, sizeof T);
    Ctx c; memset(&c, 0, sizeof c); c.f = f; c.s = s; c.tol = mc_systol.tol; c.hdr = s10;
    const uint8_t *r = s->rec;
    const double *F = in->frame, tol = in->tol;
    { const char *w = NULL;
      int k = plt_read(&c, s10, tol, &T, &w);                                    /* FUN_180591650(in_stack_90 = tol, ..) (core_tess.c l.356) */
      if (k < 0) { if (why) *why = w ? w : c.err; plt_free(&T); return -1; }
      if (k == 0) { plt_free(&T); m->mode = 1; return 0; } }                    /* no loop: no triangles */
    double u0b = T.ub[0], u1b = T.ub[1], v0b = T.vb[0], v1b = T.vb[1];          /* 18057ae8d..18057af09: the loops' box */
    for (int l = 1; l < T.nl; l++) {
        if (u0b > T.ub[2 * l]) u0b = T.ub[2 * l];
        if (T.ub[2 * l + 1] > u1b) u1b = T.ub[2 * l + 1];
        if (v0b > T.vb[2 * l]) v0b = T.vb[2 * l];
        if (T.vb[2 * l + 1] > v1b) v1b = T.vb[2 * l + 1];
    }
    double U0 = rdd(r + 0x70), U1 = rdd(r + 0x78), V0 = rdd(r + 0x80), V1 = rdd(r + 0x88);
    int nku = 0, nkv = 0, nsu = 0, nsv = 0;
    const double *uk = NULL, *vk = NULL;
    if (eval_grid(s, &KU, &KV, &nku, &nkv, &uk, &vk, &nsu, &nsv))             /* get_evaluation_grid on the base (find_base_surface) */
        TFAIL("make_surf_model: get_evaluation_grid on a bad record (the DLL asserts) NOT PORTED");
    Su = malloc(sizeof(double) * nsu); Lu = malloc(sizeof(double) * nsu); Sv = malloc(sizeof(double) * nsv); Lv = malloc(sizeof(double) * nsv);
    cu = malloc(sizeof(short) * nsu); cvn = malloc(sizeof(short) * nsv);
    for (int i = 0; i < nsu; i++) { Lu[i] = 0.0; Su[i] = 1.0; }
    for (int j = 0; j < nsv; j++) { Lv[j] = 0.0; Sv[j] = 1.0; }
    c.use_off = 1; c.off = (in->h * 3.0 + in->d) + lb78;                        /* side 1: +local_188 + local_2b78 (18057b3a6..18057b3b3) */
    if (steps_581f00(&c, tol, tol, nsu, nsv, uk, nku, vk, nkv, u0b, u1b, v0b, v1b, Su, Sv, Lu, Lv)) TFAIL(c.err);
    int totu = 0, totv = 0;
    for (int i = 0; i < nsu; i++) cu[i] = (short)cvtt(ceil(Lu[i] / Su[i]));
    { int sum = 0; for (int i = 0; i < nsu; i++) sum += cu[i];
      if (sum < 1) { if (c.tol > Su[0]) Su[0] = c.tol; Lu[0] = Su[0]; for (int i = 0; i < nsu; i++) cu[i] = (short)cvtt(ceil(Lu[i] / Su[i])); } }
    merge_counts(cu, Lu, Su, nsu, uk, nku, U1, U0, rdd(s10->rec + 0x70), u0b, u1b);   /* FUN_18058e2a0's start: the trimmed surface's own +0x70 (18057ba25) */
    for (int i = 0; i < nsu; i++) totu += cu[i];
    for (int j = 0; j < nsv; j++) cvn[j] = (short)cvtt(ceil(Lv[j] / Sv[j]));
    { int sum = 0; for (int j = 0; j < nsv; j++) sum += cvn[j];
      if (sum < 1) { if (c.tol > Sv[0]) Sv[0] = c.tol; Lv[0] = Sv[0]; for (int j = 0; j < nsv; j++) cvn[j] = (short)cvtt(ceil(Lv[j] / Sv[j])); } }
    merge_counts(cvn, Lv, Sv, nsv, vk, nkv, V1, V0, rdd(s10->rec + 0x80), v0b, v1b);   /* ... and +0x80 (18057bb85) */
    for (int j = 0; j < nsv; j++) totv += cvn[j];
    if (totu < 1 || totv < 1) TFAIL("make_surf_model: no segments after the merge NOT DECODED");
    pu = malloc(sizeof(double) * (totu + 2 + T.nl)); pv = malloc(sizeof(double) * (totv + 2 + T.nl));   /* 18057b769: + loops (island lines) */
    params_of(nsu, uk, nku, cu, U0, U1, u0b, u1b, totu, pu);
    params_of(nsv, vk, nkv, cvn, V0, V1, v0b, v1b, totv, pv);
    dedup(&totu, pu); dedup(&totv, pv);
    if (plt_loops(&c, &T, tol, u0b, u1b, v0b, v1b, &totu, &totv, pu, pv) < 0) TFAIL(c.err ? c.err : "make_surf_model on a trimmed surface: loops NOT PORTED");
    int nu = totu, nv = totv, ng = (nu + 1) * (nv + 1);
    int nlp = T.nl > 0 ? T.tb[2 * (T.nl - 1)] + T.tb[2 * (T.nl - 1) + 1] + 1 : 0;   /* local_2d48 */
    /* the vertex pass: grid points (u outer, v inner) then the loop points in table order; the box */
    for (int k = 0; k < 3; k++) { m->box[2 * k] = 1e38; m->box[2 * k + 1] = -1e38; }
    m->npts = ng + nlp;
    m->pts = malloc(sizeof(double) * 3 * ((size_t)m->npts + 1)); m->nrm = malloc(sizeof(double) * 3 * ((size_t)m->npts + 1));
    for (int k = 0; k < m->npts; k++) {
        double uu, vv, P[3], Nn[3];
        if (k < ng) { uu = pu[k / (nv + 1)]; vv = pv[k % (nv + 1)]; } else { uu = PX(T.p, k - ng); vv = PY(T.p, k - ng); }
        if (model_vertex(&c, F, uu, vv, P, Nn)) TFAIL(c.err);
        memcpy(m->pts + 3 * (size_t)k, P, sizeof P); memcpy(m->nrm + 3 * (size_t)k, Nn, sizeof Nn);
        int inside = m->box[0] <= P[0] && P[0] <= m->box[1] && m->box[2] <= P[1] && P[1] <= m->box[3] && m->box[4] <= P[2] && P[2] <= m->box[5];
        if (inside) continue;
        double q2[2] = { uu, vv };
        if (k < ng && in_polygon(T.nl, T.p, T.tb, q2) == -1) continue;
        for (int a = 0; a < 3; a++) {
            if (P[a] < m->box[2 * a]) m->box[2 * a] = P[a];
            if (m->box[2 * a + 1] < P[a]) m->box[2 * a + 1] = P[a];
        }
    }
    /* the cell loop (18057cf80..18057d932): per cell FUN_1805913b0 + FUN_180592030 (the count -> mms +0x50; with in_stack_d0 = 3 the edges) */
    EdgeOut eo; memset(&eo, 0, sizeof eo);
    eo.P = m->pts; eo.N = m->nrm; eo.tol = mc_systol.tol; eo.tb = T.tb; eo.nl = T.nl; eo.base = ng;
    C.p = T.p; C.tb = T.tb; C.nl = T.nl; C.base = ng;
    C.mk = calloc((size_t)nlp + T.np + 1, sizeof(short));
    C.cap = nlp + 4;
    C.op = malloc(sizeof(double) * 2 * (size_t)(C.cap + 1)); C.oi = malloc(sizeof(int) * (size_t)(C.cap + 1));
    C.pt = malloc(sizeof(int) * 2 * (size_t)(C.cap + 8));
    int ntri = 0, cap = 0;
    for (int i = 0, b = 0; i < nu; i++, b++)
        for (int j = 0; j < nv; j++, b++) {
            double R[8] = { pu[i], pv[j], pu[i + 1], pv[j], pu[i + 1], pv[j + 1], pu[i], pv[j + 1] };
            C.R = R; C.ci[0] = b; C.ci[1] = b + nv + 1; C.ci[2] = b + nv + 2; C.ci[3] = b + 1;
            int npc = cell_clip(&C, T.outer);
            if (cell_tris(&C, npc, &tri, &ntri, &cap, in->edges ? &eo : NULL)) { free(eo.e); TFAIL("make_surf_model on a trimmed surface: an ear touching a point while not strict (FUN_180599690 / FUN_1805999f0) NOT PORTED"); }
        }
    free(tri); tri = NULL;
    free(C.mk); free(C.op); free(C.oi); free(C.pt); memset(&C, 0, sizeof C);
    m->nu = nu; m->nv = nv; m->mode = 1; m->ntri = ntri;
    m->u = pu; m->v = pv; pu = pv = NULL;
    m->nl = T.nl; m->nlp = nlp; m->outer = T.outer;
    m->lp = malloc(sizeof(double) * 2 * ((size_t)nlp + 1)); memcpy(m->lp, T.p, sizeof(double) * 2 * (size_t)nlp);
    m->tb = malloc(sizeof(int) * 2 * ((size_t)T.nl + 1)); memcpy(m->tb, T.tb, sizeof(int) * 2 * (size_t)T.nl);
    m->edge = eo.e; m->nedge = eo.ne;
    if (!in->edges && !in->verts) { free(m->pts); free(m->nrm); m->pts = m->nrm = NULL; m->npts = 0; }
    free(KU); free(KV); free(Su); free(Sv); free(Lu); free(Lv); free(cu); free(cvn); plt_free(&T);
    return 0;
tfail:
    free(KU); free(KV); free(Su); free(Sv); free(Lu); free(Lv); free(cu); free(cvn); free(pu); free(pv); free(tri);
    free(C.mk); free(C.op); free(C.oi); free(C.pt);
    plt_free(&T);
    plm_model_free(m);
    return -1;
    #undef TFAIL
}

int plm_model(const McamFile *f, const Surf *s, const PlModelIn *in, PlModel *m, const char **why)
{
    memset(m, 0, sizeof *m);
    if (why) *why = NULL;
    #define MFAIL(msg) do { if (why) *why = (msg); goto mfail; } while (0)
    short *cu = NULL, *cvn = NULL; double *A = NULL, *pu = NULL, *pv = NULL, *KU = NULL, *KV = NULL, *lp = NULL; int nlp = 0;
    if (s && s->kind == 10 && s->rec) {
        if (in->c) { if (why) *why = "make_surf_model with a second tool shape (in_stack_50 != 0: l.364700 param_9) NOT PORTED"; return -1; }
        if (in->r == 2) { if (why) *why = "make_surf_model with r = 2 (local_2d28 == 2) NOT PORTED"; return -1; }
        return model_trim(f, s, in, m, why);
    }
    /* find_base_surface (0x1805819f0, core_tess.c; called at msm1.c l.1097): the base local_2c08 through kind 8 (offsets summed into local_2b78) and 10 */
    double lb78 = 0.0;
    const Surf *bs = s && s->rec ? (s->kind == 8 ? base_of(f, s, &lb78) : s) : NULL;
    if (!bs || !bs->rec || !model_kind(bs)) {
        if (why) *why = "make_surf_model on a surface other than kind 0 (NURBS) / 1 (parametric), an offset (kind 8) or a trimmed one (10) over them NOT PORTED";
        return -1;
    }
    if (bs != s && (in->tol <= in->d || in->tol <= in->h)) {
        if (why) *why = "make_surf_model on an offset surface (kind 8) with tol <= d or h (the side test / outline refinement on it) NOT PORTED"; return -1;
    }
    if (in->c) { if (why) *why = "make_surf_model with a second tool shape (in_stack_50 != 0: l.364700 param_9) NOT PORTED"; return -1; }
    if (in->tol * 2.0 < in->h) { if (why) *why = "make_surf_model with h > 2 tol (FUN_1805851e0: a tool other than a ball) NOT PORTED"; return -1; }
    if (in->r == 2) { if (why) *why = "make_surf_model with r = 2 (local_2d28 == 2) NOT PORTED"; return -1; }
    Ctx c; memset(&c, 0, sizeof c); c.f = f; c.s = bs; c.tol = mc_systol.tol;   /* the steps: the kind 8 copy of the base (FUN_180581f00) */
    if (bs != s) c.hdr = s;                                                   /* the vertex pass: the entity itself (model_vertex) */
    const uint8_t *r = s->rec;
    const double *F = in->frame, tol = in->tol;
    double u0b = rdd(r + 0x70), u1b = rdd(r + 0x78), v0b = rdd(r + 0x80), v1b = rdd(r + 0x88);   /* FUN_180591650 untrimmed: the surf +8..+0x20 */
    double U0 = rdd(bs->rec + 0x70), U1 = rdd(bs->rec + 0x78), V0 = rdd(bs->rec + 0x80), V1 = rdd(bs->rec + 0x88);   /* the base's (FUN_18057e440 gets
                                                                              local_2c08 +8, l.1524) */
    double ust = u0b, vst = v0b;                                              /* FUN_18058e2a0's start: the entity's own (R13, 18057ba25 / 18057bb85) */
    int nku = 0, nkv = 0, nsu = 0, nsv = 0;
    const double *uk = NULL, *vk = NULL;
    if (eval_grid(bs, &KU, &KV, &nku, &nkv, &uk, &vk, &nsu, &nsv))
        MFAIL("make_surf_model: get_evaluation_grid on a bad record (the DLL asserts) NOT PORTED");
    /* the two array sets (get_storage 18057b00a..): A = local_2bb0 / 2bc8 (L / S u), 2ba8 / 2bd8 (v); B = local_2bd0 / 2bc0, 2be8 / 2ca0 */
    A = malloc(sizeof(double) * 4 * ((size_t)nsu + nsv));
    double *LuA = A, *SuA = A + nsu, *LuB = A + 2 * nsu, *SuB = A + 3 * nsu, *LvA = A + 4 * nsu, *SvA = LvA + nsv, *LvB = LvA + 2 * nsv, *SvB = LvA + 3 * nsv;
    cu = malloc(sizeof(short) * nsu); cvn = malloc(sizeof(short) * nsv);
    double *Lu, *Su, *Lv, *Sv;
    int side, testing, pass2, first = 1, mode = 1;                            /* local_2d87, local_2d86, local_2d75, local_2d58, local_2cd0 */
    char flag1 = 0, flag2 = 0, *flagp;                                        /* local_2d76, local_2d74, local_2bf8 */
    if (tol <= in->d || tol <= in->h) {                                       /* l.397-411 (in_stack_50 = 0) */
        char t = 0;                                                           /* FUN_18058abd0(0, surf, u0, v0, 1, frame, .., &local_2d76) (asm 18057b0f0..18057b15d) */
        double E[10][3], Nn[3];
        if (plm_eval(f, s, 1, u0b, v0b, E)) MFAIL("make_surf_model: eval_surf of the kind 0 surface failed");
        plm_unit_norm(f, s, E[1], E[2], u0b, v0b, Nn); rot_view3(Nn, F);
        if (Nn[2] > 1e-05) t = 1;
        else if (-1e-05 <= Nn[2]) {                                           /* flat at the corner: the middle of the range */
            double um = (rdd(r + 0x78) + rdd(r + 0x70)) * 0.5, vm = (rdd(r + 0x88) + rdd(r + 0x80)) * 0.5;
            if (plm_eval(f, s, 1, um, vm, E)) MFAIL("make_surf_model: eval_surf of the kind 0 surface failed");
            plm_unit_norm(f, s, E[1], E[2], um, vm, Nn); rot_view3(Nn, F);
            if (1e-05 < Nn[2]) t = 1; else if (-1e-05 <= Nn[2]) t = 0; else t = 0;
        }
        side = (t == 0);
        Lu = LuA; Su = SuA; Lv = LvA; Sv = SvA; testing = (in->r != 2); flagp = &flag1; pass2 = 0;
    } else {
        side = 1; Lu = LuB; Su = SuB; Lv = LvB; Sv = SvB; testing = 0; flag2 = 1; flagp = &flag2; pass2 = 1;
    }
    flag1 = 0;
    double off188 = in->h * 3.0 + in->d;                                      /* local_188 */
    int nu = 0, nv = 0;
top:                                                                          /* LAB_18057b1f5 */
    for (int i = 0; i < nsu; i++) { Lu[i] = 0.0; Su[i] = 1.0; }
    for (int j = 0; j < nsv; j++) { Lv[j] = 0.0; Sv[j] = 1.0; }
    c.use_off = 1; c.off = (side ? off188 : -off188) + lb78;                  /* 18057b3a6..18057b3b3: XMM1 = +-local_188 + local_2b78 */
    if (steps_581f00(&c, tol, tol, nsu, nsv, uk, nku, vk, nkv, u0b, u1b, v0b, v1b, Su, Sv, Lu, Lv)) { if (why) *why = c.err; goto mfail; }
recount:                                                                      /* LAB_18057b7a2 */
    {
        int totu = 0, totv = 0;
        for (int i = 0; i < nsu; i++) cu[i] = (short)cvtt(ceil(Lu[i] / Su[i]));
        { int sum = 0; for (int i = 0; i < nsu; i++) sum += cu[i];
          if (sum < 1) { if (c.tol > Su[0]) Su[0] = c.tol; Lu[0] = Su[0]; for (int i = 0; i < nsu; i++) cu[i] = (short)cvtt(ceil(Lu[i] / Su[i])); } }
        merge_counts(cu, Lu, Su, nsu, uk, nku, U1, U0, ust, u0b, u1b);
        for (int i = 0; i < nsu; i++) totu += cu[i];
        for (int j = 0; j < nsv; j++) cvn[j] = (short)cvtt(ceil(Lv[j] / Sv[j]));
        { int sum = 0; for (int j = 0; j < nsv; j++) sum += cvn[j];
          if (sum < 1) { if (c.tol > Sv[0]) Sv[0] = c.tol; Lv[0] = Sv[0]; for (int j = 0; j < nsv; j++) cvn[j] = (short)cvtt(ceil(Lv[j] / Sv[j])); } }
        merge_counts(cvn, Lv, Sv, nsv, vk, nkv, V1, V0, vst, v0b, v1b);
        for (int j = 0; j < nsv; j++) totv += cvn[j];
        if (totu < 1 || totv < 1) MFAIL("make_surf_model: no segments after the merge NOT DECODED");
        free(pu); free(pv);
        pu = malloc(sizeof(double) * (totu + 2)); pv = malloc(sizeof(double) * (totv + 2));
        params_of(nsu, uk, nku, cu, U0, U1, u0b, u1b, totu, pu);
        params_of(nsv, vk, nkv, cvn, V0, V1, v0b, v1b, totv, pv);
        dedup(&totu, pu); dedup(&totv, pv);
        nu = totu; nv = totv;
        /* l.939-1063 (untrimmed, tol <= d): the grid's outline as one loop - u[0..nu-1] at v[0], v[0..nv-1] at u[nu], u[nu..1] at v[nv],
           v[nv..1] at u[0] - refined by FUN_18057dd40; kept (one trim loop, local_2d7c = 1) only when it gained points */
        free(lp); lp = NULL; nlp = 0;
        if (tol <= in->d) {
            int n0 = 2 * (nu + nv), q = 0;
            lp = malloc(sizeof(double) * 2 * (size_t)n0);
            for (int i = 0; i < nu; i++) { lp[2 * q] = pu[i]; lp[2 * q + 1] = pv[0]; q++; }
            for (int j = 0; j < nv; j++) { lp[2 * q] = pu[nu]; lp[2 * q + 1] = pv[j]; q++; }
            for (int i = nu; i > 0; i--) { lp[2 * q] = pu[i]; lp[2 * q + 1] = pv[nv]; q++; }
            for (int j = nv; j > 0; j--) { lp[2 * q] = pu[0]; lp[2 * q + 1] = pv[j]; q++; }
            nlp = q;
            if (refine_outline(&c, tol, &lp, &nlp)) { if (why) *why = c.err; goto mfail; }
            if (!(n0 < nlp)) { free(lp); lp = NULL; nlp = 0; }
        }
    }
    /* the vertex pass (18057c8c4..18057cb10): the box reset to +-1e38 (mms +8..+0x30), FUN_18057a510 per grid point (u outer, v inner) */
    for (int k = 0; k < 3; k++) { m->box[2 * k] = 1e38; m->box[2 * k + 1] = -1e38; }
    if (in->edges || in->verts) {                                             /* the vertex arrays (param_6 / param_7 of FUN_18059c040) of this pass */
        free(m->pts); free(m->nrm);
        m->npts = (nu + 1) * (nv + 1) + nlp;
        m->pts = malloc(sizeof(double) * 3 * (size_t)m->npts); m->nrm = malloc(sizeof(double) * 3 * (size_t)m->npts);
    }
    {
        int ltb[2] = { 0, nlp - 1 };
        for (int i = 0; i <= nu + (nlp > 0); i++) for (int j = 0; j <= (i <= nu ? nv : nlp - 1); j++) {
            /* the grid points (u outer, v inner), then the loop's points (l.1290-1313) */
            double uu = i <= nu ? pu[i] : lp[2 * j], vv = i <= nu ? pv[j] : lp[2 * j + 1], P[3], Nn[3];
            if (model_vertex(&c, F, uu, vv, P, Nn)) { if (why) *why = c.err; goto mfail; }
            if (in->edges || in->verts) { size_t k = i <= nu ? (size_t)i * (nv + 1) + j : (size_t)(nu + 1) * (nv + 1) + j;
                             memcpy(m->pts + 3 * k, P, sizeof P); memcpy(m->nrm + 3 * k, Nn, sizeof Nn); }
            char fl = 0; int s3 = 0x37;
            if (testing) {                                                    /* FUN_18057a510 param_21 / 22 (asm 18057c9ec: local_2d86, local_2d87) */
                fl = side == 0 ? !(1e-05 < Nn[2]) : !(Nn[2] < -1e-05);
                if (fl && nlp > 0) { double q2[2] = { uu, vv }; if (in_polygon(1, lp, ltb, q2) == -1) { fl = 0; s3 = 0x21; } else s3 = 0x2c; }
            }
            /* the box: a point outside it widens it; a grid point (param_12 = 0, asm 18057c9e6) with a loop only when inside the loop
               (l.1866-1903), a loop point (param_12 = 1, asm 18057cb79) always */
            int inside = m->box[0] <= P[0] && P[0] <= m->box[1] && m->box[2] <= P[1] && P[1] <= m->box[3] && m->box[4] <= P[2] && P[2] <= m->box[5];
            int skip = inside;
            if (!skip && nlp > 0 && i <= nu) {
                if (s3 == 0x37) { double q2[2] = { uu, vv }; if (in_polygon(1, lp, ltb, q2) == -1) skip = 1; }
                else if (s3 == 0x21) skip = 1;
            }
            if (!skip) for (int k = 0; k < 3; k++) {
                if (P[k] < m->box[2 * k]) m->box[2 * k] = P[k];
                if (m->box[2 * k + 1] < P[k]) m->box[2 * k + 1] = P[k];
            }
            if (testing && fl) {                                              /* 18057ca53..: *local_2bf8 = 1 */
                *flagp = 1;
                if (!pass2) goto flip;
                testing = 0;
            }
        }
    }
    if (!pass2) goto flip;                                                    /* l.1314 */
    if (!first) goto done;                                                    /* LAB_18057ceda */
    first = 0; testing = 0;
    if (!flag1) {
        mode = side ? 0 : 2;
        if (in->d < tol && in->h < tol) mode = 1;                             /* LAB_18057ced1 */
        goto done;
    }
    if (!flag2) {                                                             /* l.1331-1336: back to set A */
        Lu = LuA; Su = SuA; Lv = LvA; Sv = SvA;
        mode = side ? 2 : 0;
        goto recount;
    }
    for (int i = 0; i < nsu; i++) {                                           /* l.1339-1364: per span the set with the larger count, into set B */
        if (LuA[i] / SuA[i] <= LuB[i] / SuB[i]) { Lu[i] = LuB[i]; Su[i] = SuB[i]; } else { Lu[i] = LuA[i]; Su[i] = SuA[i]; }
    }
    for (int j = 0; j < nsv; j++) {
        if (LvA[j] / SvA[j] <= LvB[j] / SvB[j]) { Lv[j] = LvB[j]; Sv[j] = SvB[j]; } else { Lv[j] = LvA[j]; Sv[j] = SvA[j]; }
    }
    mode = 1;
    goto recount;
flip:                                                                         /* LAB_18057cdd8 / LAB_18057cde4 */
    side = !side; Lu = LuB; Su = SuB; Lv = LvB; Sv = SvB;
    testing = (in->r != 2); flag2 = 0; flagp = &flag2; pass2 = 1;
    goto top;
done:
    m->nu = nu; m->nv = nv; m->mode = mode; m->ntri = 2 * nu * nv + (nlp > 0 ? nlp - 2 * (nu + nv) : 0);   /* local_2cb0 (l.1067-1070) */
    m->u = pu; m->v = pv; pu = pv = NULL; m->nl = nlp > 0; m->nlp = nlp; m->lp = lp; lp = NULL;
    if (in->edges && model_edges(&c, m)) { if (why) *why = c.err; goto mfail; }
    free(KU); free(KV); free(A); free(cu); free(cvn);
    return 0;
mfail:
    free(KU); free(KV); free(A); free(cu); free(cvn); free(pu); free(pv); free(lp);
    plm_model_free(m);
    return -1;
    #undef MFAIL
}
void plm_model_free(PlModel *m) { free(m->u); free(m->v); free(m->lp); free(m->pts); free(m->nrm); free(m->edge); free(m->tb); memset(m, 0, sizeof *m); }

void plm_edge_bytes(const PlEdge *e, unsigned char o[0xb0])   /* FUN_18059c520's record (disassembly 18059c537..18059c65b: the frame rsp+0x20) */
{
    memset(o, 0, 0xb0);
    memcpy(o, e->P1, 24); memcpy(o + 0x18, &e->i1, 4); memcpy(o + 0x20, e->P2, 24); memcpy(o + 0x38, &e->i2, 4); memcpy(o + 0x40, e->pl, 24);
    memcpy(o + 0x58, e->prm, 32); memcpy(o + 0x78, e->N1, 24); memcpy(o + 0x90, e->N2, 24); o[0xa8] = (unsigned char)e->flag;
}

/* ---- eval_surf / calc_surf_unit_norm for kind 0, bit for bit (decomp/partline/ev_dec.c; disassembly ev_asm.txt) ---- */

typedef struct { const double *K; const uint8_t *cp; int nku, nkv, ncu, ncv, vpp, du, dv, nonrat; double *Kbuf; } NbRec;
static int nb_open(const Surf *s, NbRec *r)
{
    memset(r, 0, sizeof *r);
    if (!s || s->kind != 0 || !s->rec || s->n < 0x118) return -1;
    const uint8_t *q = s->rec;
    r->nku = rd32(q + 0x100); r->nkv = rd32(q + 0x104); r->ncu = rd32(q + 0x108); r->ncv = rd32(q + 0x10c);
    r->vpp = q[0x110]; r->du = q[0x111]; r->dv = q[0x112]; r->nonrat = q[0x113] & 1;
    if (r->vpp < 1 || r->vpp > 4 || r->du < 1 || r->dv < 1 || r->nku < r->du + 2 || r->nkv < r->dv + 2 || r->ncu < 1 || r->ncv < 1
        || 0x118 + 8 * ((size_t)r->nku + r->nkv + (size_t)r->vpp * r->ncu * r->ncv) > s->n) return -1;
    r->Kbuf = malloc(sizeof(double) * ((size_t)r->nku + r->nkv));
    for (int i = 0; i < r->nku + r->nkv; i++) r->Kbuf[i] = rdd(q + 0x118 + 8 * (size_t)i);
    r->K = r->Kbuf; r->cp = q + 0x118 + 8 * ((size_t)r->nku + r->nkv);
    return 0;
}
static double nb_cp(const NbRec *r, size_t idx) { return rdd(r->cp + 8 * idx); }

/* one level s of the Cox - de Boor triangle as FUN_1805aeb50 / FUN_1805aeff0 do it (no 1e-8 cut, unlike FUN_1805b06f0): N[i] = (t - K[i]) /
   (K[i+s] - K[i]) * N[i] (0 when either difference is below 1e-12), then + (K[i+s+1] - t) * N[i+1] / (K[i+s+1] - K[i+1]) when that product and
   that difference are both at least 1e-12 */
static void cdb_level(double t, const double *K, int nk, int span, int s, double *N)
{
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
static void basis_eval(double t, const double *K, int nk, int span, int deg, double *N)
{
    for (int i = 0; i < nk; i++) N[i] = 0.0;
    N[span] = 1.0;
    for (int s = 1; s <= deg; s++) cdb_level(t, K, nk, span, s, N);
}
/* FUN_1805aeff0 (ev_dec.c l.2132): the basis functions of every degree 0 .. deg, degree s at out[s(s+1)/2 ..] = N[span - s .. span] */
static void basis_all(double t, const double *K, int nk, int span, int deg, double *N, double *out)
{
    for (int i = 0; i < nk; i++) N[i] = 0.0;
    N[span] = 1.0; out[0] = 1.0;
    int k = 1;
    for (int s = 1; s <= deg; s++) {
        cdb_level(t, K, nk, span, s, N);
        int i = span - s; if (i < 0) i = 0;
        for (; i <= span; i++) out[k++] = N[i];
    }
}

/* p_3d::operator[] (FUN_18000dd10): 2 -> z, 1 -> y, anything else -> x */
#define PIX(k) ((k) == 2 ? 2 : (k) == 1 ? 1 : 0)

/* FUN_1805b08d0 (ev_dec.c l.1010): eval_surf order 0: sum over v (outer) and u (inner) of P * (Nu * Nv), / w when rational (not when |w| <= 1e-12) */
static void nb_point(const NbRec *r, double u, double v, double out[3])
{
    int us = nurb_span(u, r->K, r->nku, r->du), u0 = us - r->du; if (u0 < 0) u0 = 0;
    int vs = nurb_span(v, r->K + r->nku, r->nkv, r->dv), v0 = vs - r->dv; if (v0 < 0) v0 = 0;
    double *Nu = calloc((size_t)r->nku + r->nkv + 2, sizeof(double)), *Nv = Nu + r->nku;
    basis_eval(u, r->K, r->nku, us, r->du, Nu);
    basis_eval(v, r->K + r->nku, r->nkv, vs, r->dv, Nv);
    double acc[4] = { 0, 0, 0, 0 };
    for (int j = v0; j <= vs; j++) {
        if (!(j < r->ncv)) continue;
        for (int i = u0; i <= us; i++) {
            if (!(i < r->ncu)) continue;
            double w = Nu[i] * Nv[j];
            for (int c = 0; c < r->vpp; c++) acc[c] = nb_cp(r, ((size_t)r->ncu * j + i) * r->vpp + c) * w + acc[c];
        }
    }
    free(Nu);
    if (r->nonrat) for (int c = 0; c < r->vpp; c++) out[PIX(c)] = acc[c];
    else if (fabs(acc[0]) <= 1e-12) { for (int c = 1; c < r->vpp; c++) out[PIX(c - 1)] = acc[c]; }
    else for (int c = 1; c < r->vpp; c++) out[PIX(c - 1)] = acc[c] / acc[0];
}

/* FUN_1805b0fb0 (ev_dec.c l.1403): the homogeneous derivatives H[(k+l)(k+l+1)/2 + l][0 .. vpp) for k + l <= d: the span's control net copied
   (FUN_1804b7b20) and differenced in place k times along u, then l times along v (NURBS Book A3.7), then the sum over u (outer) and v (inner)
   of (Nu * Nv) * P with the degree p - k / q - l basis of FUN_1805aeff0 */
static void nb_derivs(const NbRec *r, int d, double u, double v, double H[][4])
{
    int p = r->du, q = r->dv, vpp = r->vpp;
    const double *KU = r->K, *KV = r->K + r->nku;
    int us = nurb_span(u, KU, r->nku, p), vs = nurb_span(v, KV, r->nkv, q);
    int u0 = us - p; if (u0 < 0) u0 = 0;
    int v0 = vs - q; if (v0 < 0) v0 = 0;
    double *Nt = calloc((size_t)r->nku + r->nkv + 2, sizeof(double));
    double *NuA = calloc((size_t)(p + 1) * (p + 2) / 2 + 1, sizeof(double)), *NvA = calloc((size_t)(q + 1) * (q + 2) / 2 + 1, sizeof(double));
    basis_all(u, KU, r->nku, us, p, Nt, NuA);
    basis_all(v, KV, r->nkv, vs, q, Nt, NvA);
    size_t rowl = (size_t)(p + 1) * vpp;
    double *B = malloc(sizeof(double) * rowl * (q + 1));
    #define BB(J, I, c) B[(size_t)(J) * rowl + (size_t)(I) * vpp + (c)]
    for (int k = 0; k <= d; k++) {
        for (int j = 0; j <= q; j++) for (int i = 0; i <= p; i++) for (int c = 0; c < vpp; c++)   /* FUN_1804b7b20 */
            BB(j, i, c) = nb_cp(r, (((size_t)(j + v0) * r->ncu) + u0 + i) * vpp + c);
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
    free(B); free(Nt); free(NuA); free(NvA);
}

/* FUN_1804b7f40 (ev_dec.c l.330): the rational derivatives from the homogeneous ones H[e] = (w, wx, wy, wz) (w = H[0][0], |w| < 1e-12 -> 1e-12) */
static void nb_quotient(double H[][4], int d, double D[][3])
{
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

/* eval_surf (0x18047ba50) case 0: order 0 FUN_1805b08d0, else FUN_1805b1f10 (param_6 = 0: no rescaling to the knot range) */
static int nb_eval(const NbRec *r, const Surf *s, int order, double u, double v, double D[][3])
{
    if (!(s->rec[0x6e] & 0x40)) return -1;                                   /* eval_surf: surf +6 bit 0x40 clear -> -1 */
    if (order <= 0) { nb_point(r, u, v, D[0]); return 0; }
    if (order > 3) return -1;
    int ne = (order + 1) * (order + 2) / 2;
    double H[10][4]; memset(H, 0, sizeof H);
    nb_derivs(r, order, u, v, H);
    if (!r->nonrat) nb_quotient(H, order, D);                                 /* FUN_1805b1f10: rational -> FUN_1804b7f40 */
    else for (int e = 0; e < ne; e++) for (int c = 0; c < 3; c++) D[e][c] = H[e][c];
    return 0;
}

/* ---- eval_surf for kind 1 (parametric: patches of power-basis polynomials), bit for bit (decomp/surfkinds/evalsurf.c) ----
 * The record (formats/surfaces.md 2): D+0 (ent +0x100) nu, +4 nv patches, +8 / +9 the u / v orders (bytes), the coefficients at ent +0x110 (in
 * memory a pointer at surf +0xa8), per patch (u fastest, patch (iu, iv) at (nu iv + iu) x ou ov 3 doubles) [v power][u power][xyz]. */
static int pp_open(const Surf *s, PpRec *r)
{
    memset(r, 0, sizeof *r);
    if (!s || s->kind != 1 || !s->rec || s->n < 0x110) return -1;
    const uint8_t *q = s->rec;
    r->nu = rd32(q + 0x100); r->nv = rd32(q + 0x104); r->ou = q[0x108]; r->ov = q[0x109]; r->c = q + 0x110;
    if (r->nu < 1 || r->nv < 1 || r->ou < 1 || r->ov < 1 || r->ov > 16 || 0x110 + 8 * (size_t)r->ou * r->ov * 3 * (size_t)r->nu * r->nv > s->n) return -1;
    return 0;
}
static double pp_c(const PpRec *r, size_t i) { return rdd(r->c + 8 * i); }

/* the patch of a parameter and the local parameter (FUN_1805ec000 / FUN_1805eb9c0): k = floor (t >= 0) or ceil (t < 0) truncated; t >= 0:
   t - k; k < 0: patch 0 (t kept); k > n - 1: patch n - 1, t + ((k - n) + 1) */
static int pp_patch(double *t, int k, int n)
{
    if (0.0 <= *t) *t = *t - (double)k;
    int i = 0;
    if (-1 < k) { i = k; if (n - 1 < k) { *t = *t + (double)((k - n) + 1); i = n - 1; } }
    return i;
}

/* eval_poly_deriv (mccore 0x1805eb630, evalsurf.c l.4054) with param_6 = 1: the polynomial of degree deg whose coefficient of power k of
   coordinate d is c[k * nc * stride + d], and its derivatives up to nder at t: out[j * nc + d]. Horner per coordinate with the derivative
   terms (e[j] = t e[j] + e[j - 1] for j = min(deg - k, nder) .. 1, then e[0] = t e[0] + c[k]), then e[j] x j! (j >= 2, the factorial built
   as 1 x 2 x 3 ..) */
static void pp_poly(const double *c, int deg, int nc, int stride, int nder, double t, double *out)
{
    double e[4][8];
    int st = nc * stride;
    for (int d = 0; d < nc; d++) for (int j = 0; j <= nder; j++) e[d][j] = j == 0 ? c[st * deg + d] : 0.0;
    for (int d = 0; d < nc; d++)
        for (int k = deg - 1; k >= 0; k--) {
            int m = deg - k; if (nder < m) m = nder;
            for (int j = m; j > 0; j--) e[d][j] = t * e[d][j] + e[d][j - 1];
            e[d][0] = t * e[d][0] + c[st * k + d];
        }
    double fct = 1.0;
    for (int j = 2; j <= nder; j++) { fct = fct * (double)j; for (int d = 0; d < nc; d++) e[d][j] = fct * e[d][j]; }
    for (int d = 0; d < nc; d++) for (int j = 0; j <= nder; j++) out[j * nc + d] = e[d][j];
}

/* eval_surf case 1 (0x18047ba50): order 0 FUN_1805eb9c0 (evalsurf.c l.1124: the u index through a short, the patch's coefficients from its last
   one backwards: per v row from the highest, Horner in u, then the rows Horner in v), order >= 1 FUN_1805ec000 (l.79; param_6 = 0 on every
   make_surf_model / calc_surf_norm call, so no scaling by nu / nv): per v row eval_poly_deriv in u (order derivatives), then per u derivative i
   eval_poly_deriv in v over the rows (order - i derivatives), into D[(i + j)(i + j + 1) / 2 + j] */
static int pp_eval(const PpRec *r, int order, double u, double v, double D[][3])
{
    if (order <= 0) {
        double fu = 0.0 <= u ? floor(u) : ceil(u);
        int iu = pp_patch(&u, (int)(short)cvtt(fu), r->nu);
        double fv = 0.0 <= v ? floor(v) : ceil(v);
        int iv = pp_patch(&v, cvtt(fv), r->nv);
        size_t per = (size_t)r->ov * r->ou * 3, q = per * ((size_t)r->nu * iv + iu + 1) - 1;   /* pdVar10: the patch's last double */
        double o[3], l[3];
        for (int d = 2; d >= 0; d--) o[d] = pp_c(r, q--);
        for (int a = r->ou - 2; a >= 0; a--) for (int d = 2; d >= 0; d--) o[d] = u * o[d] + pp_c(r, q--);
        for (int b = r->ov - 2; b >= 0; b--) {
            for (int d = 2; d >= 0; d--) l[d] = pp_c(r, q--);
            for (int a = r->ou - 2; a >= 0; a--) for (int d = 2; d >= 0; d--) l[d] = u * l[d] + pp_c(r, q--);
            for (int d = 2; d >= 0; d--) o[d] = v * o[d] + l[d];
        }
        D[0][0] = o[0]; D[0][1] = o[1]; D[0][2] = o[2];
        return 0;
    }
    if (order > 3) return -1;
    double fu = 0.0 <= u ? floor(u) : ceil(u);
    int iu = pp_patch(&u, cvtt(fu), r->nu);
    double fv = 0.0 <= v ? floor(v) : ceil(v);
    int iv = pp_patch(&v, cvtt(fv), r->nv);
    size_t per = (size_t)r->ov * r->ou * 3;
    double *pc = malloc(sizeof(double) * per), mem[(16 + 1) * 4 * 3];
    for (size_t i = 0; i < per; i++) pc[i] = pp_c(r, per * ((size_t)r->nu * iv + iu) + i);
    for (int b = 0; b < r->ov; b++) pp_poly(pc + (size_t)b * r->ou * 3, r->ou - 1, 3, 1, order, u, mem + b * (order + 1) * 3);
    double *scr = mem + (order + 1) * r->ov * 3;
    for (int i = 0; i <= order; i++) {
        pp_poly(mem + i * 3, r->ov - 1, 3, order + 1, order - i, v, scr);
        for (int j = 0; j <= order - i; j++) { int n = i + j, k = n * (n + 1) / 2 + j; D[k][0] = scr[3 * j]; D[k][1] = scr[3 * j + 1]; D[k][2] = scr[3 * j + 2]; }
    }
    free(pc);
    return 0;
}

/* find_base_surf (mccore 0x1806f75b0, evalsurf.c l.5262) from s: through kind 8 (offset ent +0x118 added as o + acc, base eptr +0x120) and
   kind 10 (base eptr +0x100); *acc starts at 0.0. The first surface of another kind, NULL when a link is missing / loops */
static const Surf *base_of(const McamFile *f, const Surf *s, double *acc)
{
    *acc = 0.0;
    for (int g = 0; s && g < 64; g++) {
        if (s->kind == 8) { if (s->n < 0x128) return NULL; *acc = rdd(s->rec + 0x118) + *acc; s = mcs_surf(f, rd64(s->rec + 0x120)); }
        else if (s->kind == 10) { if (s->n < 0x108) return NULL; s = mcs_surf(f, rd64(s->rec + 0x100)); }
        else return s;
    }
    return NULL;
}

int plm_eval(const McamFile *f, const Surf *s, int order, double u, double v, double D[][3])
{
    if (!s || !s->rec || s->n <= 0x6e || !(s->rec[0x6e] & 0x40)) return -1;    /* eval_surf: +6 bit 0x40 clear -> -1 */
    if (s->kind == 1) {
        PpRec p; if (pp_open(s, &p)) return -1;
        return pp_eval(&p, order, u, v, D);
    }
    if (s->kind == 10) {                                                     /* eval_surf case 10: eval_surf of the base entity */
        if (s->n < 0x108) return -1;
        const Surf *b = mcs_surf(f, rd64(s->rec + 0x100));
        if (!b || b == s) return -1;
        return plm_eval(f, b, order, u, v, D);
    }
    if (s->kind == 8) {                                                      /* eval_surf case 8: FUN_180460cd0 (evalsurf.c l.1911) */
        if (s->n < 0x128) return -1;
        double acc; const Surf *b0 = mcs_surf(f, rd64(s->rec + 0x120));      /* l.2001: get_ent_from_eptr(D +0x20) */
        if (!b0 || b0 == s) return -1;
        const Surf *b = base_of(f, b0, &acc);                                /* l.2042: find_base_surf on that copy */
        if (!b) return -1;
        const char *err = NULL;
        return off_eval(f, b, acc + rdd(s->rec + 0x118), order, u, v, D, &err) ? -1 : 0;   /* local_320[0] + D +0x18 */
    }
    NbRec r; if (nb_open(s, &r)) return -1;
    int e = nb_eval(&r, s, order, u, v, D);
    free(r.Kbuf);
    return e;
}

/* eval_surf's dispatch (0x18047ba50) for the kinds ported bit for bit: 0 (r), 1, 8 and 10 over them */
static int any_eval(const McamFile *f, const NbRec *r, const Surf *s, int order, double u, double v, double D[][3])
{
    if (s->kind != 0) return plm_eval(f, s, order, u, v, D) ? -1 : 0;
    return nb_eval(r, s, order, u, v, D);
}

/* calc_surf_norm (0x1801637e0, ev_dec.c l.3), param_7 = 0: N = Su x Sv; at or below the limit (metric 1e-4 / inch 4e-6, or the
   size (+0x70)^2 when smaller) the Taylor walk of up to 50 steps of 1/100 of the domain along u or v. The function itself has no kind
   switch (its derivatives come from eval_surf): kinds 0 and 1 here */
static int nb_norm(const McamFile *f, const NbRec *r, const Surf *s, const Surf *h, const double a[3], const double b[3], double u, double v, double N[3])
{
    const uint8_t *q = h->rec;                                                 /* the domain +0x70..+0x88 and the size +0xd8 of the surface calc_surf_norm was given (a trimmed one: its own) */
    double lim = mcam_is_metric(f) ? 1e-4 : 4e-6;
    double sz = rdd(q + 0xd8); sz = sz * sz;
    if (sz < lim) lim = sz;
    N[0] = b[2] * a[1] - b[1] * a[2]; N[1] = b[0] * a[2] - a[0] * b[2]; N[2] = a[0] * b[1] - b[0] * a[1];
    if (lim < sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2])) return 0;
    int bu = 0, bv = 0; double dd;
    double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (lim <= la) {
        double lb = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
        if (lim <= lb) {
            double cs = (b[0] * a[0] + b[1] * a[1] + b[2] * a[2]) / (lb * la);
            if (fabs(cs) < cos(0.08725)) return 0;
            double E[10][3]; if (any_eval(f, r, s, 2, u, v, E)) return 1;
            double l3 = sqrt(E[3][0] * E[3][0] + E[3][1] * E[3][1] + E[3][2] * E[3][2]), l5 = sqrt(E[5][0] * E[5][0] + E[5][1] * E[5][1] + E[5][2] * E[5][2]);
            if (l3 <= l5) goto vdir;
        }
        bu = 1; dd = (rdd(q + 0x78) - rdd(q + 0x70)) / 100.0;
        if (!(u < rdd(q + 0x78) - dd * 50.0)) dd = -dd;
    } else {
    vdir:
        dd = (rdd(q + 0x88) - rdd(q + 0x80)) / 100.0;
        if (!(v < rdd(q + 0x88) - dd * 50.0)) dd = -dd;
        bv = 1;
    }
    for (short sv = 1; ; ) {
        if (bu) u = u + dd; else if (bv) v = v + dd;
        double h = (double)(int)sv * dd, E[10][3];
        if (any_eval(f, r, s, 3, u, v, E)) return 1;
        const double *Su = E[1], *Sv = E[2], *Suu = E[3], *Suv = E[4], *Svv = E[5];
        N[0] = Sv[2] * Su[1] - Sv[1] * Su[2]; N[1] = Sv[0] * Su[2] - Su[0] * Sv[2]; N[2] = Su[0] * Sv[1] - Sv[0] * Su[1];
        double A1[3] = { 0, 0, 0 }, B1[3] = { 0, 0, 0 };
        if (bu) {          /* Suu x Sv, Su x Suv */
            A1[0] = Sv[2] * Suu[1] - Sv[1] * Suu[2]; A1[1] = Sv[0] * Suu[2] - Sv[2] * Suu[0]; A1[2] = Sv[1] * Suu[0] - Sv[0] * Suu[1];
            B1[0] = Suv[2] * Su[1] - Suv[1] * Su[2]; B1[1] = Suv[0] * Su[2] - Suv[2] * Su[0]; B1[2] = Suv[1] * Su[0] - Suv[0] * Su[1];
        } else if (bv) {   /* Suv x Sv, Su x Svv */
            A1[0] = Suv[1] * Sv[2] - Suv[2] * Sv[1]; A1[1] = Suv[2] * Sv[0] - Suv[0] * Sv[2]; A1[2] = Suv[0] * Sv[1] - Suv[1] * Sv[0];
            B1[0] = Svv[2] * Su[1] - Svv[1] * Su[2]; B1[1] = Svv[0] * Su[2] - Su[0] * Svv[2]; B1[2] = Su[0] * Svv[1] - Svv[0] * Su[1];
        }
        for (int c = 0; c < 3; c++) N[c] = (A1[c] + B1[c]) * h + N[c];
        int degen = 1;
        if (sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]) <= lim) {
            double A2[3] = { 0, 0, 0 }, B2[3] = { 0, 0, 0 }, C2[3] = { 0, 0, 0 };
            if (bu) {      /* Suu x Suv, Su x Suuv, Suuu x Sv */
                const double *T = E[6], *W = E[7];
                A2[0] = Suv[2] * Suu[1] - Suv[1] * Suu[2]; A2[1] = Suv[0] * Suu[2] - Suv[2] * Suu[0]; A2[2] = Suv[1] * Suu[0] - Suv[0] * Suu[1];
                B2[0] = W[2] * Su[1] - W[1] * Su[2]; B2[1] = W[0] * Su[2] - W[2] * Su[0]; B2[2] = W[1] * Su[0] - W[0] * Su[1];
                C2[0] = T[1] * Sv[2] - T[2] * Sv[1]; C2[1] = T[2] * Sv[0] - T[0] * Sv[2]; C2[2] = T[0] * Sv[1] - T[1] * Sv[0];
            } else if (bv) { /* Suv x Svv, Su x Svvv, Suvv x Sv */
                const double *T = E[8], *W = E[9];
                A2[0] = Suv[1] * Svv[2] - Suv[2] * Svv[1]; A2[1] = Suv[2] * Svv[0] - Suv[0] * Svv[2]; A2[2] = Suv[0] * Svv[1] - Suv[1] * Svv[0];
                B2[0] = W[2] * Su[1] - W[1] * Su[2]; B2[1] = W[0] * Su[2] - W[2] * Su[0]; B2[2] = W[1] * Su[0] - W[0] * Su[1];
                C2[0] = T[1] * Sv[2] - T[2] * Sv[1]; C2[1] = T[2] * Sv[0] - T[0] * Sv[2]; C2[2] = T[0] * Sv[1] - T[1] * Sv[0];
            }
            double h2 = h * 0.5 * h;
            for (int c = 0; c < 3; c++) N[c] = ((A2[c] * 2.0 + B2[c]) + C2[c]) * h2 + N[c];
            if (lim < sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2])) degen = 0;
        } else degen = 0;
        if (!degen) return 0;
        sv++;
        if (0x32 < sv) { N[0] = N[1] = N[2] = 0.0; return 1; }
    }
}

/* calc_surf_norm (0x1801637e0) itself (kinds 0 / 1): 0 ok, 1 no normal, -1 not ported */
int plm_norm(const McamFile *f, const Surf *s, const double Su[3], const double Sv[3], double u, double v, double N[3])
{
    NbRec r; memset(&r, 0, sizeof r);
    if (s && s->kind == 1) { PpRec p; if (pp_open(s, &p)) return -1; }
    else if ((!s || s->kind == 0) && nb_open(s, &r)) return -1;
    int e = nb_norm(f, &r, s, s, Su, Sv, u, v, N);
    free(r.Kbuf);
    return e;
}

/* calc_surf_unit_norm (0x180164290, ev_dec.c l.288) */
int plm_unit_norm(const McamFile *f, const Surf *s, const double Su[3], const double Sv[3], double u, double v, double N[3])
{
    return plm_unit_norm_h(f, s, s, Su, Sv, u, v, N);
}
/* calc_surf_unit_norm on a trimmed surface h (kind 10) whose base is s: calc_surf_norm reads h's own header (the domain +0x70..+0x88 for the
   walk's step and direction, the size +0xd8 for the limit) while eval_surf(h, ..) evaluates the base (eval_surf case 10) - FUN_18057a510 passes
   the entity's own surf_type (local_2d00, msm1.c l.2003 / l.2047) */
int plm_unit_norm_h(const McamFile *f, const Surf *h, const Surf *s, const double Su[3], const double Sv[3], double u, double v, double N[3])
{
    NbRec r; memset(&r, 0, sizeof r);
    if (s && s->kind == 1) { PpRec p; if (pp_open(s, &p)) { N[0] = N[1] = N[2] = 0.0; return -1; } }
    else if ((!s || s->kind == 0) && nb_open(s, &r)) { N[0] = N[1] = N[2] = 0.0; return -1; }
    int e = nb_norm(f, &r, s, h, Su, Sv, u, v, N);
    free(r.Kbuf);
    if (e) { N[0] = N[1] = N[2] = 0.0; return e; }
    double l = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
    if (1e-12 < fabs(l)) { N[0] = N[0] / l; N[1] = N[1] / l; N[2] = N[2] / l; return 0; }
    N[0] = N[1] = N[2] = 0.0; return 1;
}

/* FUN_1806f4b90 (msm3.c l.506): the distinct knot values strictly inside the vector's end runs (1e-8) whose next deg - 1 knots equal them
 * (multiplicity >= deg; every knot for deg < 2), in order; returns the count, *out malloc'd */
static int full_mult_knots(int deg, int nk, const double *K, double **out)
{
    *out = NULL;
    int a = 1, fa = 0, i;
    const double *p = K;
    for (;;) { i = a; if (1e-08 < fabs(p[1] - p[0])) fa = 1; a++; if (fa || !(a < nk - 1)) break; p++; }
    int b = nk - 2, fb = 0, last;
    p = K + (nk - 2);
    for (;;) { last = b; if (1e-08 < fabs(p[0] - p[1])) fb = 1; b--; p--; if (fb || !(1 < b)) break; }
    if (!fa || !fb) return 0;
    int n = 0; double *v = NULL;
    for (long l = i; l <= (long)(last - deg) + 1; l++) {
        double x = K[l]; int ok = 1;
        if (deg >= 2) for (int q = 1; q <= deg - 1; q++) if (1e-08 < fabs(K[l + q] - x)) ok = 0;
        if (!ok) continue;
        for (int q = 0; q < n; q++) if (fabs(v[q] - x) < 1e-08) ok = 0;
        if (ok) { v = realloc(v, sizeof(double) * (size_t)(n + 1)); v[n++] = x; }
    }
    *out = v;
    return n;
}

/* the normal break across one knot line at (u, v), +-1e-6 along u (dir 0) or v (dir 1): calc_surf_norm (param_7 = 0) after eval_surf order 1;
 * the second normal of the u test is asked at u + 1000000.0 (asm: addsd xmm3, [0x180d3f798] = 1000000.0, before 1801637e0 - the parameter
 * only steers calc_surf_norm's degenerate walk); a break when both |N| > 1e-8, |cos| < 0.99999 and acos > ang */
static int knot_break(const McamFile *f, const Surf *s, int dir, double u, double v, double ang)
{
    double E[10][3], N1[3], N2[3];
    NbRec r; if (nb_open(s, &r)) return 0;
    double a1 = dir ? u : u - 1e-06, b1 = dir ? v - 1e-06 : v, a2 = dir ? u : u + 1e-06, b2 = dir ? v + 1e-06 : v;
    nb_eval(&r, s, 1, a1, b1, E); nb_norm(f, &r, s, s, E[1], E[2], a1, b1, N1);
    double l1 = sqrt(N1[0] * N1[0] + N1[1] * N1[1] + N1[2] * N1[2]);
    nb_eval(&r, s, 1, a2, b2, E); nb_norm(f, &r, s, s, E[1], E[2], dir ? u : u + 1000000.0, b2, N2);
    free(r.Kbuf);
    double l2 = sqrt(N2[0] * N2[0] + N2[1] * N2[1] + N2[2] * N2[2]);
    if (!(1e-08 < l1) || !(1e-08 < l2)) return 0;
    double cs = (N2[0] * N1[0] + N2[1] * N1[1] + N2[2] * N1[2]) / (l2 * l1);
    return fabs(cs) < 0.99999 && ang < fabs(acos(cs));
}

/* check_for_ridge(ent, 5.0) (mccore 0x18047b940) -> check_surf_for_internal_sharps -> FUN_1806f5510 case kind 0 (msm3.c l.3-230; the kind 0
 * arm ends with a JMP to the exit at 1806f6504 - Ghidra's fall-through into kind 1 is not in the code): per u knot of full multiplicity
 * inside (u0 + 1e-5, u1 - 1e-5) the break across it at every distinct v knot inside (v0 + 1e-5, v1 - 1e-5), or at the v middle when there is
 * none; the same with u and v swapped. Range = the entity's own +0x70..+0x88. 1 = a ridge (FUN_1806f4760 recorded one), 0 none, -1 not
 * ported (kinds other than 0) */
int plm_ridge(const McamFile *f, const Surf *s, double deg_angle)
{
    if (!s || !s->rec || s->kind != 0 || s->n < 0x118) return -1;
    const uint8_t *q = s->rec;
    double ang = deg_angle * 0.01745329251994329;
    double u0 = rdd(q + 0x70), u1 = rdd(q + 0x78), v0 = rdd(q + 0x80), v1 = rdd(q + 0x88);
    int nku = rd32(q + 0x100), nkv = rd32(q + 0x104), du = q[0x111], dv = q[0x112];
    if (0x118 + 8 * ((size_t)nku + nkv) > s->n) return -1;
    double *K = malloc(sizeof(double) * ((size_t)nku + nkv));
    for (int i = 0; i < nku + nkv; i++) K[i] = rdd(q + 0x118 + 8 * (size_t)i);
    int found = 0;
    for (int dir = 0; dir < 2 && !found; dir++) {
        const double *KA = dir ? K + nku : K, *KB = dir ? K : K + nku;
        int na = dir ? nkv : nku, nb = dir ? nku : nkv, dg = dir ? dv : du;
        double a0 = dir ? v0 : u0, a1 = dir ? v1 : u1, b0 = dir ? u0 : v0, b1 = dir ? u1 : v1;
        double *kn = NULL; int nkn = full_mult_knots(dg, na, KA, &kn);
        for (int k = 0; k < nkn && !found; k++) {
            double x = kn[k];
            if (!(a0 + 1e-05 <= x && x <= a1 - 1e-05)) continue;
            int none = 1; double prev = 0.0;
            for (int l = 0; l < nb && !found; l++) {
                if (l >= 1 && !(1e-06 <= fabs(prev - KB[l]))) continue;
                prev = KB[l];
                if (!(b0 + 1e-05 <= prev && prev <= b1 - 1e-05)) continue;
                none = 0;
                found = dir ? knot_break(f, s, 1, prev, x, ang) : knot_break(f, s, 0, x, prev, ang);
            }
            if (none && !found) { double y = (b0 + b1) * 0.5; found = dir ? knot_break(f, s, 1, y, x, ang) : knot_break(f, s, 0, x, y, ang); }
        }
        free(kn);
    }
    free(K);
    return found;
}
