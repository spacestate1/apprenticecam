/* mcsrfdl.c - a surface's display-list points and the wireframe pick on them, as mastercam.dll builds and reads them
 * (DisplayList.cpp / DlPoint.cpp; formats/chaining.md "Which routine normal picking uses", "The display-list points (port)").
 * Dumps: decomp/chain_srfedge_dl2.c (FUN_1801391f0, FUN_18013a130, FUN_18013a5e0, FUN_180139360, FUN_18013a040, FUN_1801321c0,
 * FUN_180137d20, FUN_1801323a0, FUN_18013b210), decomp/chain_srfedge_dl_mastercam.c (FUN_180139a60 = surf_div_dl), decomp/surfpick/
 * mc_pick.c (FUN_18013cbc0, FUN_18013edd0, FUN_18013ea20, FUN_18013ef90, FUN_18013b2a0 / b4a0 / b530, surf_screen_box), decomp/
 * chain_srfedge_dl3.c (FUN_1801395e0, FUN_18013b5c0 / b590, FUN_18013e1e0, FUN_180130520, FUN_180130af0) and the disassembly in
 * decomp/chain_srfedge_dl3.asm (the stack arguments Ghidra drops: FUN_18013a040's parameter, FUN_180139360's v / flags). mccore:
 * hit_polygon 0x18046be80, in_polygon 0x18046c960, clip_uv_to_minmax 0x1806f72e0, masort_2d_real_index 0x18047b060, nurb_interval
 * 0x1805b0630, find_base_surf 0x1806f75b0 (decomp/surftrim/core_ext.c, decomp/surfpick/core_dist.c).
 * Values read from the image (MemDump): DAT_180869788 = -0.86 (the corner cosine), zoomscl 0x180869ed8 = 20.0 (never written),
 * surf_back_draw 0x1809a0b7a = 0 (its writers: the backside-shading toggle, not in this program: the normals are not made).
 * Surface positions: eval_surf through mcsn_eval / mcs_surf_eval (eval_surf's ports); eval_surf_const (0x18047bd50) after
 * init_surf_const (0x18047c600) through its own per-kind evaluators (kinds 0 / 1 / 2 / 6 / 7 below; kind 4 refused; other kinds: eval_surf,
 * as the DLL's default case). Dump: decomp/chain_srfedge_const.c (+ .asm). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mcsrfdl.h"
#include "mcsurf.h"
#include "mcsurf_nrm.h"

static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static int64_t rd64(const uint8_t *p) { int64_t v; memcpy(&v, p, 8); return v; }

#define ZOOMSCL 20.0             /* mastercam.dll zoomscl 0x180869ed8 (.data, no writer) */
#define CORNER_COS (-0.86)       /* DAT_180869788 */

typedef struct {
    const McamFile *f; const SdlView *v; SdlNode *o;
    int lc;                      /* DAT_1809294e0: the records of the current polyline */
    double scale;                /* view +0x770 during the generation (the power of two) */
    const char *why; int fail;
    struct SConst *sc;           /* init_surf_const's state (DAT_1813f20b0 ..): NULL = not initialised -> eval_surf */
} G;

/* find_base_surf (mccore 0x1806f75b0): through offset (kind 8: header +0xb8 = ent +0x120) and trimmed (kind 10: +0x98 = ent +0x100)
   surfaces to the base; NULL when an eptr cannot be read (get_ent_from_eptr fails: the DLL returns its code) */
static const Surf *find_base(const McamFile *f, const Surf *s) {
    for (int g = 0; g < 64 && s; g++) {
        if (s->kind == 8) { if (s->n < 0x128) return NULL; s = mcs_surf(f, rd64(s->rec + 0x120)); }
        else if (s->kind == 10) { if (s->n < 0x108) return NULL; s = mcs_surf(f, rd64(s->rec + 0x100)); }
        else return s;
    }
    return NULL;
}

/* FUN_1801321c0(dir, header): the direction is linear (one span of order 2): kind 0 degree 1 with 2 control points, kind 1 order 2 with
   one patch, kind 4 (v: +0x98 short 1, +0x9c 2), kind 6 (v) */
static int lin_dir(int dir, const Surf *s) {
    const uint8_t *r = s->rec; if (s->n < 0x110) return 0;
    int k = r[0x68];
    if (dir == 0) {
        if (k == 0 && r[0x111] == 1 && rd32(r + 0x108) == 2) return 1;
        return k == 1 && r[0x108] == 2 && rd32(r + 0x100) == 1;
    }
    if (k == 0 && r[0x112] == 1 && rd32(r + 0x10c) == 2) return 1;
    if (k == 1 && r[0x109] == 2 && rd32(r + 0x104) == 1) return 1;
    if (k == 4 && rd16(r + 0x100) == 1 && rd32(r + 0x104) == 2) return 1;
    return k == 6;
}

/* nurb_interval (mccore 0x1805b0630) */
static int nurb_interval(double t, const double *K, int nk, int deg) {
    if (!K || nk < 1) return 0;
    if (t < K[0]) t = K[0];
    if (K[nk - 1] < t) t = K[nk - 1];
    int i = deg + 1, hi = nk - i, r;
    for (;;) { r = hi; if (!(i <= hi)) break; r = i; if (!(K[i] - 1e-10 <= t)) break; i++; }
    r -= 1;
    if (r >= 0 && r + 1 < nk && K[r + 1] - K[r] <= 1e-10) {
        int j = nk - 1;
        if (0 < j) { while (t <= K[j] + 1e-10) { j--; if (j < 1) return r; } r = j; }
    }
    return r;
}
static const double *knots(const Surf *s, int *nku, int *nkv) {
    if (s->n < 0x118) return NULL;
    *nku = rd32(s->rec + 0x100); *nkv = rd32(s->rec + 0x104);
    if (*nku < 1 || *nkv < 1 || 0x118 + 8 * ((size_t)*nku + *nkv) > s->n) return NULL;
    return (const double *)(s->rec + 0x118);   /* the record is 8-aligned (mcam_load) */
}
/* FUN_18013a040(header, dir 1 = u / 2 = v, t): kind 0 nurb_interval + 1, kind 1 floor(t) + 1 clamped to 1 .. the patch count, else 0 */
static int knot_idx(const Surf *s, int dir, double t) {
    int k = s->rec[0x68];
    if (k == 0) {
        int nu, nv; const double *K = knots(s, &nu, &nv); if (!K) return 0;
        if (dir == 1) return nurb_interval(t, K, nu, s->rec[0x111]) + 1;
        return nurb_interval(t, K + nu, nv, s->rec[0x112]) + 1;
    }
    if (k != 1) return 0;
    int i = (int)floor(t) + 1;
    if (dir == 1) { if (rd32(s->rec + 0x100) < i) return rd32(s->rec + 0x100); }
    else if (rd32(s->rec + 0x104) < i) return rd32(s->rec + 0x104);
    return i < 1 ? 1 : i;
}
/* the knot the snap moves to (FUN_18013a130 18013a312 / 18013a51c): kind 1 the index itself, kind 0 the knot (v: after the u knots) */
static double knot_val(const Surf *s, int dir, int idx) {
    if (s->rec[0x68] == 1) return (double)idx;
    int nu, nv; const double *K = knots(s, &nu, &nv); if (!K) return 0.0;
    return dir == 1 ? K[idx] : K[nu + idx];
}

/* surf_div_dl (FUN_180139a60): the iso-line spacing and sample step of a pass (dir 1: u = const lines sampled along v, *p3 = the u
   spacing, *p4 = the v step; dir 0: v = const lines, *p3 = the u step, *p4 = the v spacing) at the view's scales; dens = ent +0x69 */
static int surf_div_dl(const McamFile *f, const Surf *s, int dens, const SdlView *v, int dir, double *p3, double *p4) {
    double d8 = 0.1;
    if (dir == 0) *p3 = 0.1 / ZOOMSCL; else { *p3 = 0.1; if (dir == 1) d8 = 0.1 / ZOOMSCL; }
    *p4 = d8;
    double size = rdd(s->rec + 0xd8), wk = v->wk; int n1, n2;
    if (wk <= size * 20.0) {
        if (wk <= size * 12.0) {
            if (wk <= size * 4.0) { n2 = 0x24; n1 = (wk * 4.0 < size || wk * 4.0 == size) ? 0xc : 8; }
            else { n1 = 4; n2 = 0xc; }
        } else { n1 = 2; n2 = 6; }
    } else { n1 = 1; n2 = 3; }
    const Surf *b = find_base(f, s); if (!b || b->n < 0x118) return 0;
    const uint8_t *h = b->rec + 0x68;
    double u0 = rdd(h + 8), u1 = rdd(h + 0x10), v0 = rdd(h + 0x18), v1 = rdd(h + 0x20), cu = rdd(h + 0x78), cv = rdd(h + 0x80), tol = v->tol;
    double r = u1 - u0, a, c;
    if (cu < tol || cu == tol) {
        a = r / ((double)n1 * (dir == 0 ? ZOOMSCL : 1.0)); *p3 = a; a *= dir == 0 ? 0.5 : 2.0;
    } else {
        double sc = dir == 0 ? v->y_scr : v->x_scr, d7;
        a = sqrt(3.2 / (sc * cu)); if (r * 0.5 <= a) a = r * 0.5;
        if (dir == 1) { a *= 4.0; d7 = (double)n1; } else d7 = (double)n2 * ZOOMSCL;
        if (a <= r / d7) a = r / d7;
        a = r / ceil(r / a);
    }
    *p3 = a;
    r = v1 - v0;
    if (cv < tol || cv == tol) {
        c = r / ((double)n1 * (dir == 1 ? ZOOMSCL : 1.0)); *p4 = c; c *= dir != 1 ? 2.0 : 0.5;
    } else {
        double sc = dir == 1 ? v->y_scr : v->x_scr, d7;
        c = sqrt(3.2 / (sc * cv)); if (r * 0.5 <= c) c = r * 0.5;
        if (dir == 0) { c *= 4.0; d7 = (double)n1; } else d7 = (double)n2 * ZOOMSCL;
        if (c <= r / d7) c = r / d7;
        c = r / ceil(r / c);
    }
    *p4 = c;
    int k = h[0];
    if (k == 1) { if (dir == 0) { if (0x14 < rd32(b->rec + 0x100)) *p3 *= 0.25; } else if (dir == 1 && 0x14 < rd32(b->rec + 0x104)) *p4 *= 0.25; }
    else if (k == 4) { if (dir == 0) *p4 = 1.0; else if (dir == 1) *p3 = 1.0 / ceil(1.0 / *p3); }
    else if (k == 7 && dir == 0) *p4 = 0.5;
    if (1 < dens) { if (dir == 1) *p3 /= (double)dens; else if (dir == 0) *p4 /= (double)dens; }
    return 1;
}

/* FUN_18013b210: double -> float, 0 outside 1e-16 .. 1e16 */
static float f_of(double d) { double a = fabs(d); return (a >= 1e16 || a <= 1e-16) ? 0.0f : (float)d; }
static void b210(float *o, const double *p) { o[0] = f_of(p[0]); o[1] = f_of(p[1]); o[2] = f_of(p[2]); }
/* FUN_18013b5c0: a - b with the z difference the other way (b - a; the DLL's) */
static void sub5c0(const float *a, const float *b, float *o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = b[2] - a[2]; }
/* FUN_18013b590 */
static float len590(const float *a) { return sqrtf((a[0] * a[0] + a[1] * a[1]) + a[2] * a[2]); }

/* FUN_180137d20(prev, last, new): a corner at last (asm 180137d20..180137e7b) */
static int corner(const float *pv, const float *ls, const float *nw, double tol) {
    float v1[3], v2[3];
    sub5c0(pv, ls, v1); float l1 = len590(v1);
    if (tol > (double)l1) return 1e-08 > (double)l1;
    sub5c0(nw, ls, v2); float l2 = len590(v2);
    if (tol > (double)l2) return 1e-08 > (double)l2;
    float dot = (v2[0] * v1[0] + v2[1] * v1[1]) + v2[2] * v1[2];
    float cs = dot / (l2 * l1);
    if ((double)cs > CORNER_COS) return 1;
    float r1 = l2 / l1, r2 = l1 / l2, rt = r1 > r2 ? r1 : r2;
    float q = (float)(CORNER_COS + 1.0); q = q / rt; q = (float)((double)q - 1.0);
    return cs > q;
}
/* FUN_1801323a0 "dist_ok"(prev, last, new): the chord sag of the three points times the view scale under 0.99 (asm 1801323a0..1801325d7) */
static int dist_ok(const float *pv, const float *ls, const double *nwd, double tol, double scale) {
    float nf[3], d[3], w[3]; b210(nf, nwd);
    sub5c0(ls, pv, d); float L = len590(d);
    if (tol > (double)L) return 1;
    d[0] = d[0] / L; d[1] = d[1] / L; d[2] = d[2] / L;
    float h = L * 0.5f;
    sub5c0(nf, ls, w); w[0] = w[0] * 0.5f; w[1] = w[1] * 0.5f; w[2] = w[2] * 0.5f;
    float m = len590(w);
    float f6 = (w[1] * d[1] + w[0] * d[0]) + w[2] * d[2];
    float f3 = m * m - f6 * f6;
    if (!(0.0f < f3) && f3 == f3) return 1;
    float sq = sqrtf(f3);
    float x2 = f6 + h; float q = f6 / sq; x2 = x2 * q; x2 = x2 + sq; x2 = x2 * x2; x2 = x2 + h * h;
    float rr = sqrtf(x2);
    float a = (m + h) / rr;
    double c = cos((double)a);
    double t = (1.0 - c) * ((double)rr * scale);
    float tf = (float)fabs(t);
    return 0.99 > (double)tf;
}

/* init_surf_const / eval_surf_const (mccore 0x18047c600 / 0x18047bd50) for a NURBS surface (kind 0): FUN_1805b2280 keeps the basis of
   the fixed parameter (u for init_surf_const(.., 1), v for (.., 0); redone when it moves by 1e-12 or more), FUN_1805b1aa0 the other's
   basis per point (FUN_1805b06f0: Cox - de Boor, a quotient over a span under 1e-12 or at t within 1e-12 of its knot is 0, a value
   under 1e-8 set to 0), sum Nu[i] Nv[j] P[j][i] (v outer, u inner), / w when rational (|w| <= 1e-12: not divided). As mcpartline_mesh.c
   sconst_* (decomp/surftrim/trim_surf_closure.c FUN_1805b2280 / FUN_1805b1aa0), both directions. Kinds 2 / 6 / 7 (2026-11-17, decomp/chain_srfedge_const.{c,asm}): init_surf_const sets
   them up only for dir 1 (u fixed; dir 0 leaves DAT_1813f20b0 = 0 -> eval_surf_const = eval_surf): kind 2 FUN_18047ae20 / FUN_18047ad80,
   kind 6 FUN_180413460 / FUN_180413400, kind 7 FUN_18041d090 / FUN_18041cdb0 (-> FUN_1803fca60, FUN_180114560) - the fixed u's curve
   points cached (DAT_1813f4a10; redone when u moves by 1e-12 or more), eval_curve through mcs_curve_eval / mcs_spline_eval_d (the
   ports of eval_curve 0x1803fcd10). Kind 4 (FUN_18042d9b0 / FUN_18042c080, decompiled in the same dump) is NOT PORTED: no kind 4
   surface in the corpus to check it against (and eval_surf of kind 4 is not ported either) - sdl_build refuses it. */
typedef struct SConst { const double *KU, *KV; const uint8_t *cp; int nku, nkv, ncu, ncv, vpp, du, dv, nonrat; int dir, have; double fix; int fspan; double *Nu, *Nv;
    int kind; int nu, nv, ou, ov, st[4]; const double *co; double *cache;
    const McamFile *f; const uint8_t *dat; const CurveRec *crv[3]; int st1; double cs[16]; } SConst;   /* kinds 2 / 6 / 7: ent +0x100, the curves, DAT_1813f20b0 / DAT_1813f4a10 */   /* kind 1: patches, orders, FUN_1805ec5c0's state / cache */
static int nurb_span(double t, const double *K, int nk, int deg) { return nurb_interval(t, K, nk, deg); }
static void basis_const(double t, const double *K, int nk, int span, int deg, double *N) {   /* FUN_1805b06f0 */
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
static void pp_update(SConst *c, int dir, double u, double v);
static int curve_upd(SConst *c, double u);
static int sconst_init(SConst *c, const McamFile *f, const Surf *s, int dir) {
    memset(c, 0, sizeof *c);
    if (s->kind == 2 || s->kind == 6 || s->kind == 7) {   /* init_surf_const 0x18047c600: only for param_2 != 0, at u0 (surf +8) */
        if (!dir) return -1;
        const uint8_t *r = s->rec;
        static const int off[3][3] = { { 0x148, 0, 0 }, { 0x120, 0, 0 }, { 0x100, 0x108, 0x110 } };   /* rev +0x48, draft +0x20, sweep +0 / +8 / +0x10 */
        int ki = s->kind == 2 ? 0 : s->kind == 6 ? 1 : 2, n = ki == 2 ? 3 : 1;
        if (s->n < (uint32_t)off[ki][n - 1] + 8) return -2;
        c->kind = s->kind; c->f = f; c->dat = r + 0x100;
        for (int i = 0; i < n; i++) { c->crv[i] = mcs_curve(f, rd64(r + off[ki][i])); if (!c->crv[i]) return -2; }
        return curve_upd(c, rdd(r + 0x70)) ? -2 : 0;
    }
    if (s->kind == 4) return dir ? -3 : -1;
    if (s->kind == 1) {   /* FUN_1805ec5c0 (decomp/surftrim/trim_plane_closure.c): ent +0x100 nu, +0x104 nv, +0x108 / +0x109 the u / v orders,
                             the coefficients at +0x110 (per patch, u fastest: [v power][u power][xyz]) */
        const uint8_t *r = s->rec; if (s->n < 0x110) return -1;
        c->kind = 1; c->nu = rd32(r + 0x100); c->nv = rd32(r + 0x104); c->ou = r[0x108]; c->ov = r[0x109];
        if (c->nu < 1 || c->nv < 1 || c->ou < 1 || c->ov < 1 || 0x110 + 8 * (size_t)c->ou * c->ov * 3 * c->nu * c->nv > s->n) return -1;
        c->co = (const double *)(r + 0x110);
        int m = c->nu > c->nv ? c->nu : c->nv, o = c->ou > c->ov ? c->ou : c->ov;
        c->cache = calloc((size_t)m * o * 3 + 2, sizeof(double)); if (!c->cache) return -1;
        c->dir = dir; pp_update(c, dir, rdd(r + 0x70), rdd(r + 0x80));   /* init_surf_const: at (u0, v0) */
        return 0;
    }
    if (s->kind != 0 || s->n < 0x118) return -1;
    const uint8_t *r = s->rec;
    c->nku = rd32(r + 0x100); c->nkv = rd32(r + 0x104); c->ncu = rd32(r + 0x108); c->ncv = rd32(r + 0x10c);
    c->vpp = r[0x110]; c->du = r[0x111]; c->dv = r[0x112]; c->nonrat = r[0x113] & 1;
    if (c->vpp < 1 || c->vpp > 4 || c->nku < c->du + 2 || c->nkv < c->dv + 2 || c->ncu < 1 || c->ncv < 1
        || 0x118 + 8 * ((size_t)c->nku + c->nkv + (size_t)c->vpp * c->ncu * c->ncv) > s->n) return -1;
    c->KU = (const double *)(r + 0x118); c->KV = c->KU + c->nku; c->cp = r + 0x118 + 8 * ((size_t)c->nku + c->nkv);
    c->Nu = calloc((size_t)c->nku + 2, sizeof(double)); c->Nv = calloc((size_t)c->nkv + 2, sizeof(double));
    c->dir = dir;
    return 0;
}
static void sconst_free(SConst *c) { free(c->Nu); free(c->Nv); free(c->cache); memset(c, 0, sizeof *c); }
/* FUN_1805ec5c0(pps, dir, u, v, state, cache): the fixed direction's power sum collapsed per patch of the free one (cache[1..]: patch,
   xyz, free power), redone when the fixed parameter moves by 1e-12 or more; the patch = (int)(t + 1e-8) clamped */
static void pp_update(SConst *c, int dir, double u, double v) {
    int iu = (int)(u + 1e-08), iv = (int)(v + 1e-08), i7 = c->nu - 1, i9 = c->nv - 1;
    if (iu <= c->nu - 1) { i7 = iu; if (iu < 0) i7 = 0; }
    if (iv <= c->nv - 1) { i9 = iv; if (iv < 0) i9 = 0; }
    double t; int fo, s20, s10, i5;
    if (dir == 0) { if (c->st[0] == 2 && fabs(v - c->cache[0]) < 1e-12) return;
                    c->st[0] = 2; c->st[1] = i9; c->st[2] = c->nu; c->st[3] = c->ou; c->cache[0] = v; fo = c->ov; s20 = 3; s10 = c->ou * 3; i5 = i9; t = v; }
    else { if (c->st[0] == 1 && fabs(u - c->cache[0]) < 1e-12) return;
           c->st[0] = 1; c->st[1] = i7; c->st[2] = c->nv; c->st[3] = c->ov; s10 = 3; c->cache[0] = u; fo = c->ou; s20 = c->ou * 3; i5 = i7; t = u; }
    double X = t - (double)i5;
    int b4 = c->ou * c->ov * 3, i7o;
    if (dir == 0) i7o = c->nu * b4 * i9; else { i7o = b4 * i7; b4 = c->nu * b4; }
    const double *L = c->co + i7o;
    int nx1 = 1e-12 <= fabs(X - 1.0);
    double d3 = 0.0, d4 = 0.0;
    if (2 < fo && 1e-12 <= fabs(X) && nx1) { d3 = X * X; if (3 < fo) d4 = X * d3; }
    long w = 1; int b0 = 0;
    for (int pt = 0; pt < c->st[2]; pt++) {
        for (int k = 0; k < 3; k++) {
            for (int j = 0; j < c->st[3]; j++) {
                int idx = j * s20 + b0 + k; double a = L[idx];
                if (1 < fo && 1e-12 <= fabs(X)) {
                    if (nx1) {
                        idx += s10; a = a + X * L[idx];
                        if (2 < fo) { idx += s10; a = a + d3 * L[idx];
                            if (3 < fo) { const double *q = L + s10 + idx; a = a + d4 * *q;
                                for (int sx = 4; sx < fo; sx++) { q += s10; a = a + pow(X, (double)sx) * *q; } } }
                    } else { const double *q = L + idx; for (int m = 1; m < fo; m++) { q += s10; a = a + *q; } }
                }
                c->cache[w++] = a;
            }
        }
        b0 += b4;
    }
}
/* FUN_1805ebcd0: the point from the cache (the free parameter's patch (int)(t + 1e-8) clamped, Horner-free powers, X near 0 / 1 exact) */
static void pp_eval(SConst *c, double u, double v, double out[3]) {
    pp_update(c, c->st[0] == 1, u, v);
    double t = c->st[0] != 1 ? u : v;
    int i4 = (int)(t + 1e-08), i5 = c->st[2] - 1;
    if (i4 <= c->st[2] - 1) { i5 = i4; if (i4 < 0) i5 = 0; }
    int n = c->st[3]; double X = t - (double)i5, d1 = 0.0, d2 = 0.0;
    if (2 < n && 1e-12 <= fabs(X) && 1e-12 <= fabs(X - 1.0)) { d1 = X * X; if (3 < n) d2 = X * d1; }
    int idx = n * i5 * 3 + 1;
    for (int k = 0; k < 3; k++) {
        int i8 = idx + 1; double a = c->cache[idx];
        if (1 < n) {
            if (1e-12 <= fabs(X)) {
                if (1e-12 <= fabs(X - 1.0)) {
                    a = X * c->cache[i8] + a; i8 = idx + 2;
                    if (2 < n) { a = d1 * c->cache[idx + 2] + a; i8 = idx + 3;
                        if (3 < n) { i8 = idx + 4; a = d2 * c->cache[idx + 3] + a;
                            for (int sx = 4; sx < n; sx++) { a = pow(X, (double)sx) * c->cache[i8] + a; i8++; } } }
                } else { for (int m = 1; m < n; m++) { a = a + c->cache[i8]; i8++; } }
            } else i8 = idx + n;
        }
        out[k] = a; idx = i8;
    }
}
/* the fixed u's curve data (u fixed by init_surf_const(.., 1)): kind 2 FUN_18047ae20, kind 6 FUN_180413460, kind 7 FUN_18041d090.
   eval_curve 0x1803fcd10: order 0 mcs_curve_eval, order 1 (kind 6) mcs_spline_eval_d (splines / NURBS) or a line's b - a. 0 ok, -1 an
   eval_curve this port cannot do */
static int curve_upd(SConst *c, double u) {
    double *cs = c->cs; const uint8_t *p = c->dat;
    if (c->st1 == 1 && !(1e-12 <= fabs(u - cs[0]))) return 0;   /* *state == 1 && |u - cache[0]| < 1e-12: kept */
    c->st1 = 1; cs[0] = u;
    if (c->kind == 2) {   /* FUN_18047ae20: rev +0 axis d, +0x18 point A, +0x48 the profile */
        double C[3]; if (mcs_curve_eval(c->f, c->crv[0], u, C)) return -1;
        double d0 = rdd(p), d1 = rdd(p + 8), d2 = rdd(p + 0x10), A0 = rdd(p + 0x18), A1 = rdd(p + 0x20), A2 = rdd(p + 0x28);
        double h = (C[0] - A0) * d0 + (C[1] - A1) * d1 + (C[2] - A2) * d2;
        double F = d0 * h + A0; cs[7] = F; double R0 = C[0] - F;
        F = h * d1 + A1; cs[8] = F; double R1 = C[1] - F;
        F = h * d2 + A2; cs[1] = R0; cs[2] = R1; cs[9] = F; double R2 = C[2] - F; cs[3] = R2;
        cs[4] = R2 * d1 - R1 * d2; cs[5] = R0 * d2 - R2 * d0; cs[6] = R1 * d0 - R0 * d1;
        return 0;
    }
    if (c->kind == 6) {   /* FUN_180413460: draft +0 N, +0x18 d, +0x20 the curve (eval_curve order 1) */
        double o[3][3]; memset(o, 0, sizeof o);
        const CurveRec *cr = c->crv[0];
        if (cr->cls == 0x8 || cr->cls == 0x80) { if (mcs_spline_eval_d(cr, u, 1, o)) return -1; }
        else if (cr->cls == 0x2) { if (mcs_curve_eval(c->f, cr, u, o[0])) return -1; for (int k = 0; k < 3; k++) o[1][k] = rdd(cr->rec + 0x80 + 8 * k) - rdd(cr->rec + 0x68 + 8 * k); }
        else if (cr->cls == 0x1) { if (mcs_curve_eval(c->f, cr, u, o[0])) return -1; }
        else return -1;   /* an arc's first derivative (view_to_world of the turned radius) is not ported here */
        cs[1] = o[0][0]; cs[2] = o[0][1]; cs[3] = o[0][2];
        double N0 = rdd(p), N1 = rdd(p + 8), N2 = rdd(p + 0x10), dd = rdd(p + 0x18);
        double L = sqrt(N0 * N0 + N1 * N1 + N2 * N2), Ls = L, d9;
        int big = 1000000000000.0 <= fabs(-dd);
        if (big) { d9 = 0.0; if (0.0 < -dd) Ls = -L; } else d9 = -dd * L;
        double m0 = -(N0 / L), m1 = -(N1 / L), m2 = -(N2 / L);
        double z = o[1][1] * m0 - o[1][0] * m1, x = o[1][2] * m1 - o[1][1] * m2, y = o[1][0] * m2 - o[1][2] * m0;
        if (1e-12 <= fabs(x) || 1e-12 <= fabs(y) || 1e-12 <= fabs(z)) {
            double l = sqrt(y * y + x * x + z * z), ux, uy, uz = 0.0;
            if (l == 0.0) { ux = 0.0; uy = 0.0; } else { ux = x / l; uy = y / l; uz = z / l; }
            if (big) { cs[4] = ux * Ls; cs[5] = uy * Ls; cs[6] = uz * Ls; }
            else { cs[4] = ux * d9 + N0; cs[5] = uy * d9 + N1; cs[6] = uz * d9 + N2; }
        } else if (big) { cs[4] = cs[5] = cs[6] = 0.0; }
        else { cs[4] = N0; cs[5] = N1; cs[6] = N2; }
        return 0;
    }
    /* kind 7, FUN_18041d090: sweep +0 / +8 / +0x10 the three curves C0, C1, C2; n = (C1 - C0) x (C2 - C0) / |..| (kept when |..| <= 1e-12) */
    for (int i = 0; i < 3; i++) if (mcs_curve_eval(c->f, c->crv[i], u, cs + 1 + 3 * i)) return -1;
    double z = (cs[8] - cs[2]) * (cs[4] - cs[1]) - (cs[7] - cs[1]) * (cs[5] - cs[2]);
    double x = (cs[9] - cs[3]) * (cs[5] - cs[2]) - (cs[8] - cs[2]) * (cs[6] - cs[3]);
    double y = (cs[7] - cs[1]) * (cs[6] - cs[3]) - (cs[9] - cs[3]) * (cs[4] - cs[1]);
    double l = sqrt(y * y + x * x + z * z);
    cs[10] = l;
    if (1e-12 < l) { cs[11] = x / l; cs[12] = y / l; cs[13] = z / l; }
    return 0;
}
/* FUN_180114560: r turned about the unit axis n by a */
static void rot114560(const double *r, const double *n, double a, double *o) {
    double n1 = n[1], n2 = n[2], n0 = n[0];
    double d = n0 * r[0] + n1 * r[1] + n2 * r[2];
    double p6 = r[1] - n1 * d, p5 = r[2] - n2 * d, p4 = r[0] - n0 * d;
    double cv = cos(a), sv = sin(a);
    o[0] = (n1 * p5 - n2 * p6) * sv + p4 * cv + n0 * d;
    o[1] = (n2 * p4 - n0 * p5) * sv + p6 * cv + n1 * d;
    o[2] = (n0 * p6 - n1 * p4) * sv + p5 * cv + n2 * d;
}
/* eval_surf_const for kinds 2 (FUN_18047ad80), 6 (FUN_180413400), 7 (FUN_18041cdb0 -> FUN_1803fca60(C1, C2, C0, n, 0, 0, v, out),
   asm 18041ce17..18041ce9a) */
static int curve_const_eval(SConst *c, double u, double v, double out[3]) {
    if (curve_upd(c, u)) return -1;
    const double *cs = c->cs;
    if (c->kind == 2) {
        double cv = cos(v), sv = sin(v);
        for (int k = 0; k < 3; k++) out[k] = cv * cs[1 + k] + cs[7 + k] + sv * cs[4 + k];
        return 0;
    }
    if (c->kind == 6) { for (int k = 0; k < 3; k++) out[k] = v * cs[4 + k] + cs[1 + k]; return 0; }
    if (1e-12 > cs[10]) { out[0] = cs[1]; out[1] = cs[2]; out[2] = cs[3]; return 0; }   /* 18041cdec: comisd 1e-12, [state + 0x50] */
    const double *p1 = cs + 4, *p2 = cs + 7, *p3 = cs + 1;   /* FUN_1803fca60 */
    double a1 = p1[1] - p3[1], a2 = p1[2] - p3[2], a0 = p1[0] - p3[0];
    double q = ((p2[0] - p3[0]) * a0 + (p2[1] - p3[1]) * a1 + (p2[2] - p3[2]) * a2) / (a0 * a0 + a1 * a1 + a2 * a2), th;
    if (q <= 0.999999999999) { if (-0.999999999999 <= q) th = acos(q); else th = 3.141592653589793; } else th = 0.0;
    double a[3] = { a0, a1, a2 }, R[3];
    rot114560(a, cs + 11, th * v, R);
    out[0] = R[0] + p3[0]; out[1] = R[1] + p3[1]; out[2] = R[2] + p3[2];
    return 0;
}
static void sconst_eval(SConst *c, double u, double v, double out[3]) {
    if (c->kind == 1) { pp_eval(c, u, v, out); return; }
    double fx = c->dir ? u : v;
    if (!c->have || !(fabs(fx - c->fix) < 1e-12)) {                          /* FUN_1805b2280 */
        c->have = 1; c->fix = fx;
        if (c->dir) { c->fspan = nurb_span(u, c->KU, c->nku, c->du); basis_const(u, c->KU, c->nku, c->fspan, c->du, c->Nu); }
        else { c->fspan = nurb_span(v, c->KV, c->nkv, c->dv); basis_const(v, c->KV, c->nkv, c->fspan, c->dv, c->Nv); }
    }
    int us, vs;                                                               /* FUN_1805b1aa0 */
    if (c->dir) { us = c->fspan; vs = nurb_span(v, c->KV, c->nkv, c->dv); basis_const(v, c->KV, c->nkv, vs, c->dv, c->Nv); }
    else { vs = c->fspan; us = nurb_span(u, c->KU, c->nku, c->du); basis_const(u, c->KU, c->nku, us, c->du, c->Nu); }
    double acc[4] = { 0, 0, 0, 0 };
    int i0 = us - c->du; if (i0 < 0) i0 = 0;
    int j0 = vs - c->dv; if (j0 < 0) j0 = 0;
    for (int j = j0; j <= vs; j++) {
        if (!(j < c->ncv)) continue;
        for (int i = i0; i <= us; i++) {
            if (!(i < c->ncu)) continue;
            double w = c->Nu[i] * c->Nv[j];
            const uint8_t *q = c->cp + 8 * (size_t)c->vpp * ((size_t)c->ncu * j + i);
            for (int k = 0; k < c->vpp; k++) acc[k] = rdd(q + 8 * (size_t)k) * w + acc[k];
        }
    }
    #define IX(k) ((k) == 2 ? 2 : (k) == 1 ? 1 : 0)
    if (!c->nonrat) {
        if (fabs(acc[0]) <= 1e-12) { for (int k = 0; k + 2 <= c->vpp; k++) out[IX(k)] = acc[k + 1]; }
        else { for (int k = 0; k + 2 <= c->vpp; k++) out[IX(k)] = acc[k + 1] / acc[0]; }
    } else for (int k = 0; k < c->vpp; k++) out[IX(k)] = acc[k];
    #undef IX
}

/* init_surf_const(h, dir) / free_surf_const */
static void sc_init(G *g, const Surf *h, int dir) {
    if (g->sc) { sconst_free(g->sc); free(g->sc); g->sc = NULL; }
    SConst *c = malloc(sizeof *c); int r = c ? sconst_init(c, g->f, h, dir) : -1;
    if (r == 0) { g->sc = c; return; }
    free(c);
    if (r == -3) { g->why = "a kind 4 surface: its eval_surf_const (FUN_18042c080 / FUN_18042d9b0) is NOT PORTED"; g->fail = 1; }
    else if (r == -2) { g->why = "eval_surf_const: the surface's curve cannot be read or evaluated here (FUN_18047ae20 / FUN_180413460 / FUN_18041d090)"; g->fail = 1; }
}
static void sc_free(G *g) { if (g->sc) { sconst_free(g->sc); free(g->sc); g->sc = NULL; } }

static SdlRec *grow(SdlNode *o) {
    if (o->n == o->cap) { int nc = o->cap ? o->cap * 2 : 256; SdlRec *r = realloc(o->r, sizeof *r * (size_t)nc); if (!r) return NULL; o->r = r; o->cap = nc; }
    return &o->r[o->n];
}
/* FUN_180139360(header, node, dir, u, v, cont, last): one point; dir != 0 -> eval_surf_const (here mcs_surf_eval, see the top) */
static int rec_put(G *g, const Surf *hs, int dir, double u, double v, int cont, int last) {
    SdlNode *o = g->o;
    if (!grow(o)) { g->why = "out of memory"; g->fail = 1; return 0; }
    int cr = 0;
    if (!cont) g->lc = 0;
    double P[3];
    if (dir != 0 && g->sc && g->sc->dat) {                               /* eval_surf_const, kinds 2 / 6 / 7 */
        if (curve_const_eval(g->sc, u, v, P)) { g->why = "eval_surf_const: the surface's curve cannot be evaluated here"; g->fail = 1; return 0; }
    }
    else if (dir != 0 && g->sc) sconst_eval(g->sc, u, v, P);             /* eval_surf_const */
    else {                                                               /* eval_surf (also eval_surf_const without init_surf_const) */
        double D[10][3];
        if (mcsn_eval(g->f, hs, 0, u, v, D) == 0) memcpy(P, D[0], sizeof P);
        else if (mcs_surf_eval(g->f, hs, u, v, P)) { g->why = "the surface's evaluation (eval_surf) for this kind is NOT PORTED"; g->fail = 1; return 0; }
    }
    float nf[3]; b210(nf, P);
    SdlRec *L = o->n ? &o->r[o->n - 1] : NULL, *w;
    if (1 < g->lc) cr = corner(L[-1].p, L->p, nf, g->v->tol);
    if (!last && 1 < g->lc) {
        if (cr) { L->flag = 2; goto app; }
        if (dist_ok(L[-1].p, L->p, P, g->v->tol, g->scale)) { w = L; goto put; }
        goto app;
    } else if (cr) L->flag = 2;
app:
    w = &o->r[o->n++]; g->lc++;
put:
    memcpy(w->p, nf, sizeof nf);
    w->n[0] = w->n[1] = w->n[2] = 0.0f;   /* surf_back_draw 0: no normals */
    w->uv[0] = (float)u; w->uv[1] = (float)v;
    w->flag = cont != 0;
    return 1;
}
static void end_line(SdlNode *o) { if (o->n) o->r[o->n - 1].flag = 2; }   /* *(DAT_1808d6f30 + 0x20) = 2 */

/* FUN_18013a130: an untrimmed surface's points (decompile + asm 18013a130..18013a5e0) */
static int gen_untrimmed(G *g, const Surf *s) {
    const uint8_t *r = s->rec; uint16_t fl = (uint16_t)rd16(r + 0x6e);
    int full = !(g->v->render_cfg4 && !(fl & 1) && !(fl & 2));
    int linU = lin_dir(0, s), linV = lin_dir(1, s), dens = r[0x69];
    double u0 = rdd(r + 0x70), u1 = rdd(r + 0x78), v0 = rdd(r + 0x80), v1 = rdd(r + 0x88), a, c;
    int skip = !surf_div_dl(g->f, s, dens, g->v, 1, &a, &c);
    double x = u0, sp;
    if (full) { double half = (u1 - u0) * 0.5; sp = (a > half || dens < 1) ? half : a; } else sp = u1 - u0;
    double st = c;
    sc_init(g, s, 1);
    if (g->fail) return 0;
    if (!skip) for (int last = 0, it = 0; it < 1000000; it++) {
        if (u1 <= x) { last = 1; x = u1; }
        double y = v0; int k = 0;
        if (!linV) k = knot_idx(s, 2, y);
        int ls = 0, cont = 0;
        do {
            if (v1 <= y) { ls = 1; y = v1; }
            if (!rec_put(g, s, 1, x, y, cont, ls)) return 0;
            if (linV) y = v1;
            else {
                y += st; int k2 = knot_idx(s, 2, y);
                if (k2 != k) { double kv = knot_val(s, 2, k); if (kv - (y - st) > 1e-08) { y = kv; k2 = knot_idx(s, 2, y); } }
                k = k2;
            }
            cont = 1;
        } while (!ls);
        end_line(g->o);
        if (last) break;
        x += sp;
    }
    sc_free(g);
    skip = !surf_div_dl(g->f, s, dens, g->v, 0, &a, &c);
    double y = v0;
    if (full) { double half = (v1 - v0) * 0.5; sp = (c > half || dens < 1) ? half : c; } else sp = v1 - v0;
    st = a; if (r[0x68] == 7 && 2.0 < a) st = 2.0;
    sc_init(g, s, 0);
    if (skip) return 1;
    for (int last = 0, it = 0; it < 1000000; it++) {
        if (v1 <= y) { last = 1; y = v1; }
        double xx = u0; int k = 0;
        if (!linU) k = knot_idx(s, 1, xx);
        int ls = 0, cont = 0;
        do {
            if (u1 <= xx) { ls = 1; xx = u1; }
            if (!rec_put(g, s, 2, xx, y, cont, ls)) return 0;
            if (linU) xx = u1;
            else {
                xx += st; int k2 = knot_idx(s, 1, xx);
                if (k2 != k) { double kv = knot_val(s, 1, k); if (kv - (xx - st) > 1e-08) { xx = kv; k2 = knot_idx(s, 1, xx); } }
                k = k2;
            }
            cont = 1;
        } while (!ls);
        end_line(g->o);
        if (last) break;
        y += sp;
    }
    return 1;
}

/* ---- mccore helpers on a trimmed surface's loops: pts = the uv points, info = (start, count) per loop (the last point = start + count) */
/* in_polygon (0x18046c960): 1 inside, -1 outside, 0 on a loop (within 1e-10) */
static int in_polygon(int nl, const double *pts, const int32_t *info, const double p[2]) {
    int in = 0; double y3 = p[1] - 1e-10, x4 = p[0] - 1e-10, x5 = p[0] + 1e-10;
    for (int l = 0; l < nl; l++) {
        long st = info[2 * l], e = (long)info[2 * l + 1] + info[2 * l];
        double px = pts[2 * e], py = pts[2 * e + 1];
        for (long i = st; i <= e; i++) {
            double cx = pts[2 * i], cy = pts[2 * i + 1], dy = cy - py;
            if (1e-10 <= dy || 1e-10 <= -dy) {
                if ((py < y3 && y3 <= cy) || (cy <= y3 && y3 < py)) {
                    double x = ((p[1] - py) * (cx - px)) / dy + px, d = p[0] - x;
                    if (d < 1e-10 && -d < 1e-10) return 0;
                    if (p[0] < x) in ^= 1;
                }
            } else {
                double d = py - p[1];
                if (d < 1e-10 && -d < 1e-10 && ((x4 <= px && cx <= x5) || (px <= x5 && x4 <= cx))) return 0;
            }
            px = cx; py = cy;
        }
    }
    return in ? 1 : -1;
}
/* hit_polygon (0x18046be80) with param_4 = 1: the crossings of the segment a -> b with the loops (an end within 1e-12 snaps to a / b),
   none within 1e-6 of an earlier one, at most 0x100 */
static int hit_polygon(int nl, const double *pts, const int32_t *info, const double a[2], const double b[2], double *out) {
    int n = 0; double Dx = b[0] - a[0], Dy = b[1] - a[1];
    for (int l = 0; l < nl; l++) {
        long st = info[2 * l], e = (long)info[2 * l + 1] + info[2 * l];
        double qx = pts[2 * e] - a[0], qy = pts[2 * e + 1] - a[1];
        double d20 = qy * Dx - qx * Dy;
        for (long i = st; i <= e; i++) {
            double cx = pts[2 * i] - a[0], cy = pts[2 * i + 1] - a[1];
            double d1 = cy * Dx - cx * Dy;
            if (((d1 <= 0.0 && 0.0 <= d20) || (0.0 <= d1 && d20 <= 0.0)) && 1e-12 < fabs(d1 - d20)) {
                double t = (cy * qx - cx * qy) / (d1 - d20);
                if (-1e-12 < t && t <= 1.000000000001) {
                    double *o = out + 2 * n;
                    if (fabs(t) <= 1e-12) { o[0] = a[0]; o[1] = a[1]; }
                    else if (fabs(1.0 - t) <= 1e-12) { o[0] = b[0]; o[1] = b[1]; }
                    else { o[0] = Dx * t + a[0]; o[1] = t * Dy + a[1]; }
                    if (n == 0) n = 1;
                    else {
                        int dup = 0;
                        for (int j = 0; j < n; j++) if (sqrt((out[2 * j] - o[0]) * (out[2 * j] - o[0]) + (out[2 * j + 1] - o[1]) * (out[2 * j + 1] - o[1])) < 1e-06) { dup = 1; break; }
                        if (!dup) n++;
                        if (n == 0x100) return n;
                    }
                }
            }
            qx = cx; qy = cy; d20 = d1;
        }
    }
    return n;
}
/* clip_uv_to_minmax (0x1806f72e0) */
static void clip_uv(int n, double *p, double u0, double u1, double v0, double v1) {
    for (int i = 0; i < n; i++) {
        if (u0 <= p[2 * i]) { if (u1 < p[2 * i]) p[2 * i] = u1; } else p[2 * i] = u0;
        if (v0 <= p[2 * i + 1]) { if (v1 < p[2 * i + 1]) p[2 * i + 1] = v1; } else p[2 * i + 1] = v0;
    }
}
/* masort_2d_real_index (0x18047b060): idx = the order of a[i * stride + key], a quicksort with the DLL's pivot sequence and
   insertion sort below 8 */
static int masort(const double *a, int n, int stride, int key, int32_t *idx) {
    #define K_(i) a[(long)(i) * stride + key]
    for (int i = 0; i < n; i++) idx[i] = i;
    int stk[0x60]; int sp = 0, lo = 0, hi = n - 1, seed = 0;
    for (;;) {
        while (!(6 < hi - lo)) {
            for (int j = lo + 1; j <= hi; j++) {
                int x = idx[j], k = j - 1;
                while (k >= 0 && !(K_(idx[k]) <= K_(x))) { idx[k + 1] = idx[k]; k--; }
                idx[k + 1] = x;
            }
            if (sp == 0) return 0;
            hi = stk[sp + 1]; lo = stk[sp]; sp -= 2;
        }
        seed = (seed * 0xd3 + 0x67f) % 0x1ec3;
        int p = ((hi - lo + 1) * seed) / 0x1ec3 + lo;
        int piv = idx[p]; idx[p] = idx[lo];
        long r = hi, l = lo, k, j; int pos;
        for (;;) {
            k = r;
            if (0 < r) { do { if (K_(idx[k]) <= K_(piv)) break; k--; } while (0 < k); }
            if (k <= l) { pos = (int)l; break; }
            j = l + 1; idx[l] = idx[k];
            while (j < n) { if (K_(piv) <= K_(idx[j])) break; j++; }
            if (k <= j) { pos = (int)k; break; }
            r = k - 1; idx[k] = idx[j]; l = j;
        }
        idx[pos] = piv;
        if (pos - lo < hi - pos) { stk[sp + 2] = lo; stk[sp + 3] = pos - 1; lo = pos + 1; }
        else { stk[sp + 2] = pos + 1; stk[sp + 3] = hi; hi = pos - 1; }
        sp += 2;
        if (0x4f < sp) return 1;   /* "sort stack overflow" */
    }
    #undef K_
}

typedef struct { int m, nl; const double *pts; const int32_t *info; } Loops;
static int loops_of(const Surf *s, Loops *L) {
    if (s->n < 0x138) return 0;
    L->m = rd32(s->rec + 0x10c); L->nl = rd32(s->rec + 0x128);
    if (L->m < 1 || L->nl < 0 || 0x138 + 16 * (size_t)L->m + 8 * (size_t)L->nl > s->n) return 0;
    L->pts = (const double *)(s->rec + 0x138); L->info = (const int32_t *)(s->rec + 0x138 + 16 * (size_t)L->m);
    for (int l = 0; l < L->nl; l++) { long st = L->info[2 * l], e = (long)L->info[2 * l + 1] + st; if (st < 0 || e < st || e >= L->m) return 0; }
    return 1;
}

/* FUN_1801395e0: a trimmed surface's loops through the base (each straight piece along u or v subdivided at surf_div_dl's step) */
static int gen_loops(G *g, const Surf *s, const Surf *b, const Loops *L) {
    double a0 = 0, b1 = 0, t; int dens = s->rec[0x69];
    surf_div_dl(g->f, s, dens, g->v, 0, &a0, &t);
    surf_div_dl(g->f, s, dens, g->v, 1, &t, &b1);
    int linU = lin_dir(0, b), linV = lin_dir(1, b);
    double su = 1.0, sv = 1.0;
    for (int l = 0; l < L->nl; l++) {
        long st = L->info[2 * l], e = (long)L->info[2 * l + 1] + st;
        double P[2] = { L->pts[2 * e], L->pts[2 * e + 1] };
        if (!rec_put(g, b, 0, P[0], P[1], 0, 0)) return 0;
        for (long i = st; i <= e; i++) {
            double Q[2] = { L->pts[2 * i], L->pts[2 * i + 1] }, du = fabs(Q[0] - P[0]), dv; int n;
            if (0.0001 <= du) {
                dv = Q[1] - P[1];
                if (fabs(dv) < 0.0001) { if (!linU) { n = (int)(short)(int)ceil(du / a0); goto L8b5; } goto L8d6; }
                n = 1; goto L8e4;
            } else {
                if (!linV) { dv = Q[1] - P[1]; n = (int)ceil(fabs(dv) / b1); goto L8b5; }
                goto L8d6;
            }
        L8b5: if (1 < n) { su = (Q[0] - P[0]) / (double)n; sv = dv / (double)n; goto L8e4; }
        L8d6: n = 1; goto L8f0;
        L8e4: if (!(0 < n)) continue;
        L8f0:
            for (int kk = 1; kk <= n; kk++) {
                if (kk >= n) { P[0] = Q[0]; P[1] = Q[1]; } else { P[0] = su + P[0]; P[1] = sv + P[1]; }
                if (!rec_put(g, b, 0, P[0], P[1], 1, kk == n && i == e)) return 0;
            }
        }
        end_line(g->o);
    }
    g->o->nloop = g->o->n; g->o->scl = (float)g->v->y_scr;
    return 1;
}

/* one iso line of a trimmed surface (FUN_18013a5e0's two passes, asm 18013a7f0..18013ac59 / 18013ace0..18013b136): pass 1 (dir 1) the
   u = x line from v0 to v1 of the trimmed range, pass 0 the v = x line; the crossings with the loops (hit_polygon from 0.01 outside the
   range), at most 0x100, clipped to the range; an odd count drops the first (the middle of the first two on a loop: in_polygon 0) or
   else the last (likewise); sorted along the line; the samples at the base's step (knot snap as the untrimmed) between pairs */
static int trim_line(G *g, const Surf *s, const Surf *b, const Loops *L, int pass, double x, double step, int lin, double *H, int32_t *idx) {
    const uint8_t *r = s->rec;
    double u0 = rdd(r + 0x70), u1 = rdd(r + 0x78), v0 = rdd(r + 0x80), v1 = rdd(r + 0x88), A[2], B[2];
    if (pass) { A[0] = x; A[1] = v0 - 0.01; B[0] = x; B[1] = v1 + 0.01; } else { A[0] = u0 - 0.01; A[1] = x; B[0] = u1 + 0.01; B[1] = x; }
    int n = hit_polygon(L->nl, L->pts, L->info, A, B, H);
    if (0x100 < n) n = 0x100;
    clip_uv(n, H, u0, u1, v0, v1);
    if (1 < n && (n % 2) == 1) {
        double mp[2] = { (H[2] + H[0]) * 0.5, (H[3] + H[1]) * 0.5 };
        if (in_polygon(L->nl, L->pts, L->info, mp) == 0) { n--; memmove(H, H + 2, sizeof(double) * 2 * (size_t)n); }
        else { double ml[2] = { (H[2 * (n - 1)] + H[2 * (n - 2)]) * 0.5, (H[2 * (n - 1) + 1] + H[2 * (n - 2) + 1]) * 0.5 };
               if (in_polygon(L->nl, L->pts, L->info, ml) == 0) n--; }
    }
    masort(H, n, 2, pass ? 1 : 0, idx);
    double y = pass ? v0 : u0, ye = pass ? v1 : u1; int key = pass ? 1 : 0, dir = pass ? 2 : 1, ed = pass ? 1 : 2;
    int k = 0; if (!lin) k = knot_idx(b, dir, y);
    int done = n < 2, in = 0, q = 0;
    while (!done) {
        if (ye <= y) { done = 1; y = ye; }
        if (q < n && H[2 * idx[q] + key] <= y) {
            while (q < n) {
                if (y < H[2 * idx[q] + key]) break;
                if (!rec_put(g, b, ed, H[2 * idx[q]], H[2 * idx[q] + 1], in, in != 0)) return 0;
                if (in == 1) end_line(g->o);
                q++; if (q == n) done = 1;
                in = in != 1;
            }
        }
        if (in == 1) { if (!rec_put(g, b, ed, pass ? x : y, pass ? y : x, in, done)) return 0; }
        if (lin) y = ye;
        else {
            y += step; int k2 = knot_idx(b, dir, y);
            if (k2 != k) { double kv = knot_val(b, dir, k); if (1e-08 < kv - (y - step)) { y = kv; k2 = knot_idx(b, dir, y); } }
            k = k2;
        }
    }
    end_line(g->o);
    return 1;
}
/* FUN_18013a5e0: a trimmed surface: the loops (FUN_1801395e0), then (not in the shaded outline) the iso lines between the crossings */
static int gen_trimmed(G *g, const Surf *s) {
    const uint8_t *r = s->rec; uint16_t fl = (uint16_t)rd16(r + 0x6e);
    int full = !(g->v->render_cfg4 && !(fl & 1) && !(fl & 2));
    if (s->n < 0x108) return 0;
    const Surf *b = mcs_surf(g->f, rd64(r + 0x100)); Loops L;
    if (!b || b == s || b->n < 0x118) return 0;   /* get_ent_from_eptr fails */
    if (!loops_of(s, &L)) { g->why = "a trimmed surface whose loop table cannot be read"; g->fail = 1; return 0; }
    if (!gen_loops(g, s, b, &L)) return 0;
    if (!full) return 1;
    /* the base with the trimmed surface's density (local_3d9f = ent +0x69) for surf_div_dl */
    double a, c; int dens = r[0x69];
    double H[2 * 0x100 + 4]; int32_t idx[0x100 + 2];
    int linU = lin_dir(0, b), linV = lin_dir(1, b);
    double bu0 = rdd(b->rec + 0x70), bu1 = rdd(b->rec + 0x78), bv0 = rdd(b->rec + 0x80), bv1 = rdd(b->rec + 0x88);
    double u0 = rdd(r + 0x70), u1 = rdd(r + 0x78), v0 = rdd(r + 0x80), v1 = rdd(r + 0x88);
    surf_div_dl(g->f, b, dens, g->v, 1, &a, &c);
    sc_init(g, b, 1);                    /* init_surf_const(base, 1) at each line (the basis follows the line's u either way) */
    if (g->fail) return 0;
    double lo = u0 <= bu0 ? u0 : bu0, hi = bu1 <= u1 ? u1 : bu1, x = lo + a; int single = 0;
    for (int it = 0; it < 1000000; it++) {
        if (dens == 0 || (u1 - u0) * 0.5 < a) { single = 1; x = (u1 + u0) * 0.5; }
        if (!trim_line(g, s, b, &L, 1, x, c, linV, H, idx)) return 0;
        if (single) break;
        x += a; if (!(x < hi)) break;
    }
    sc_free(g);
    surf_div_dl(g->f, b, dens, g->v, 0, &a, &c);
    sc_init(g, b, 0);
    double st = a; if (b->rec[0x68] == 7 && 2.0 < a) st = 2.0;
    lo = v0 <= bv0 ? v0 : bv0; hi = bv1 <= v1 ? v1 : bv1; x = lo + c; single = 0;
    for (int it = 0; it < 1000000; it++) {
        if (dens == 0 || (v1 - v0) * 0.5 < c) { single = 1; x = (v1 + v0) * 0.5; }
        if (!trim_line(g, s, b, &L, 0, x, st, linU, H, idx)) return 0;
        if (single) break;
        x += c; if (!(x < hi)) break;
    }
    return 1;
}

int sdl_build(const McamFile *f, const Surf *s, const SdlView *v, SdlNode *o, const char **why) {
    o->n = 0; o->nloop = 0; o->built = 0; o->scl = 0;
    if (why) *why = NULL;
    if (!s || !s->rec || s->n < 0x118 || !(s->rec[0x6e] & 0x40)) return 0;   /* *param_1 == 0x100 && (ent +0x6e & 0x40) */
    /* the scales at a power of two: s = 2^(floor(log2((uint)y_scr_scl)) + 2) (0 shifts below 2), wk * 4 * y / s */
    unsigned u2 = (unsigned)(long long)v->y_scr; int c = 0;
    while ((u2 >>= 1) != 0) c++;
    double sc = (double)(1u << ((c + 2) & 0x1f));
    SdlView pv = *v; pv.y_scr = sc; pv.x_scr = sc; pv.wk = (v->wk * 4.0 * v->y_scr) / sc;
    G g; memset(&g, 0, sizeof g); g.f = f; g.v = &pv; g.o = o; g.scale = sc;
    int ok = s->kind == 10 ? gen_trimmed(&g, s) : gen_untrimmed(&g, s);
    sc_free(&g);
    if (g.fail) { if (why) *why = g.why; o->n = 0; return -1; }
    if (ok) o->built = 1;
    return 0;
}
void sdl_free(SdlNode *o) { free(o->r); memset(o, 0, sizeof *o); }

/* FUN_18013b4a0: the view x / y of a record (view +0x720 / +0x738 rows) */
static void proj(const float *p, const double vm[6], double o[2]) {
    o[0] = (double)p[0] * vm[0] + (double)p[1] * vm[1] + (double)p[2] * vm[2];
    o[1] = (double)p[0] * vm[3] + (double)p[1] * vm[4] + (double)p[2] * vm[5];
}
/* FUN_18013b530: the clip outcode */
static uint8_t outcode(const double p[2], const double clip[4]) {
    uint8_t c = 0;
    if (clip[0] <= p[0]) { if (clip[1] < p[0]) c = 2; } else c = 1;
    if (p[1] < clip[2]) c |= 8; else if (clip[3] < p[1]) c |= 4;
    return c;
}
/* FUN_18013b2a0: point - segment distance, t clamped to 0 .. 1 */
static double segdist(const double *p, const double *a, const double *b, double *t) {
    double ex = b[0] - a[0], py = p[1] - a[1], ey = b[1] - a[1], px = p[0] - a[0], l2 = ey * ey + ex * ex, d;
    if (l2 == 0.0) { *t = 0.0; }
    else {
        d = (ex * px + ey * py) / l2; *t = d;
        if (0.0 < d) {
            if (1.0 <= d) { *t = 1.0; return sqrt((p[0] - b[0]) * (p[0] - b[0]) + (p[1] - b[1]) * (p[1] - b[1])); }
            py = py - d * ey; px = px - d * ex;
        } else *t = 0.0;
    }
    return sqrt(px * px + py * py);
}
/* surf_screen_box (mastercam.dll 0x180340810): the surface's box (ent +0x90 / +0xa8) turned into the view, about the centre (+0xc0) */
void sdl_screen_box(const uint8_t *r, const double vm[6], double o[4]) {
    double m0 = vm[0], m1 = vm[1], m2 = vm[2], n0 = vm[3], n1 = vm[4], n2 = vm[5];
    double dx = rdd(r + 0xa8) - rdd(r + 0x90), dy = rdd(r + 0xb0) - rdd(r + 0x98), dz = rdd(r + 0xb8) - rdd(r + 0xa0);
    double a = dy * m1 + dx * m0 + dz * m2, bb = dy * n1 + dx * n0 + dz * n2;
    double a2 = a - dx * 2.0 * m0, b2 = bb - dx * 2.0 * n0, w, h;
    w = fabs(a) < fabs(a2) || fabs(a) == fabs(a2) ? fabs(a2) : fabs(a);
    h = fabs(bb) < fabs(b2) || fabs(bb) == fabs(b2) ? fabs(b2) : fabs(bb);
    a2 = a2 - dy * 2.0 * m1; b2 = b2 - dy * 2.0 * n1;
    if (w <= fabs(a2)) w = fabs(a2);
    if (h <= fabs(b2)) h = fabs(b2);
    a2 = a2 - dz * 2.0 * m2; b2 = b2 - n2 * dz * 2.0;
    if (w <= fabs(a2)) w = fabs(a2);
    if (h <= fabs(b2)) h = fabs(b2);
    double cx = rdd(r + 0xc0) * m0 + rdd(r + 0xc8) * m1 + rdd(r + 0xd0) * m2, cy = rdd(r + 0xc0) * n0 + rdd(r + 0xc8) * n1 + rdd(r + 0xd0) * n2;
    o[0] = cx - w * 0.5; o[1] = cy - h * 0.5; o[2] = cx + w * 0.5; o[3] = cy + h * 0.5;
}

#define NPT 0x1000
int sdl_pick(const SdlNode *nd, const Surf *s, const double vm[6], const double clip[4], const double cur[2], double best, double *dist, double uv[2]) {
    int found = 0;
    const uint8_t *r = s->rec;
    if (r[0x6e] & 0x40) {   /* the bounding circle about the centre (+0xc0, radius +0xd8), then surf_screen_box against the CENTRE's
                               projection (mc_pick.c l.36-50, asm 18013cc69..18013cca7: local_90 / local_88 = proj_iso_pt of the centre) */
        double c[2] = { rdd(r + 0xc0) * vm[0] + rdd(r + 0xc8) * vm[1] + rdd(r + 0xd0) * vm[2], rdd(r + 0xc0) * vm[3] + rdd(r + 0xc8) * vm[4] + rdd(r + 0xd0) * vm[5] };
        double d = sqrt((cur[1] - c[1]) * (cur[1] - c[1]) + (cur[0] - c[0]) * (cur[0] - c[0]));
        if (d > rdd(r + 0xd8)) return 0;
        double bx[4]; sdl_screen_box(r, vm, bx);
        if (bx[0] > c[0] || bx[1] > c[1] || c[0] > bx[2] || c[1] > bx[3]) return 0;
    }
    /* FUN_18013edd0: the entity (+0x6e bit 0x40, a trimmed one's base too) */
    if (!(r[0x6e] & 0x40) || !nd->n) return 0;
    static double P[NPT][2], UV[NPT][2]; static uint8_t C[NPT];
    long at = 0, end = nd->n;
    for (int more = 1; more; ) {   /* FUN_18013ea20(0, 1, 1, 0, 1): one polyline (to the record before a flag 0), at most 0x1000 points */
        int k = 0, done = 0, last = 0;
        for (;;) {
            if (0xfff < k) break;
            if (end <= at + 1) { at = end - 1; last = 1; done = 1; }
            if (!done && nd->r[at + 1].flag == 0) done = 1;
            proj(nd->r[at].p, vm, P[k]); C[k] = outcode(P[k], clip);
            UV[k][0] = (double)nd->r[at].uv[0]; UV[k][1] = (double)nd->r[at].uv[1];
            at++; k++;
            if (done) break;
        }
        if (0xfff < k) at--;
        more = !last;
        for (int i = 1; i < k; i++) {
            if (C[i] & C[i - 1]) continue;
            double t, d = segdist(cur, P[i - 1], P[i], &t);
            if (d < best) { best = d; *dist = d; uv[0] = (UV[i][0] - UV[i - 1][0]) * t + UV[i - 1][0]; uv[1] = (UV[i][1] - UV[i - 1][1]) * t + UV[i - 1][1]; found = 1; }
        }
    }
    return found;
}
int sdl_extents(const SdlNode *nd, const double vm[6], double box[4]) {
    box[0] = 1e38; box[1] = -1e38; box[2] = 1e38; box[3] = -1e38;
    if (!nd->built) return 0;
    for (int i = 0; i < nd->n; i++) {   /* FUN_18013e630 per projected record */
        double p[2]; proj(nd->r[i].p, vm, p);
        if (p[0] < box[0]) box[0] = p[0];
        if (box[1] < p[0]) box[1] = p[0];
        if (p[1] < box[2]) box[2] = p[1];
        if (box[3] < p[1]) box[3] = p[1];
    }
    return 1;
}

/* ================================================================================================ dist_to_surf's trimmed branch
   FUN_1802de180 (mccore; dist_to_surf 0x1802ddba0 calls it for kind 10 after its bounding-circle / surf_screen_box early outs), ported from
   the disassembly (decomp/chain_srfedge_de180.asm 1802de180..1802deed0) checked against the decompile (decomp/chain_srfedge_trim.c l.301-722;
   Ghidra's aiStack_44f0[q] / [q + 1] are idx[q - 2] / idx[q - 1]); callees hit_polygon / clip_uv_to_minmax / masort_2d_real_index (above),
   surf_division 0x1806f9530 on the trimmed surface (mcs_surf_division), init_surf_const / eval_surf_const on the base (SConst above; a kind
   without one: eval_surf), dist_to_polyline 0x1802dd3f0, proj_iso_pt 0x180018170 (ax: the view's x / y iso axes), dist_to_finite_line /
   t_on_line with the system tol. Pass 1: the u = x lines from min(u0 of the trimmed, of the base) + the spacing (density 0 or a spacing over
   0.6 x the trimmed u range: one line at (u1 + u0) / 2) while x < max(u1s); per line hit_polygon from (x, v0 - 0.1) to (x, v1 + 0.1) (at
   most 0x100, clipped to the trimmed domain, sorted by v; no odd-count fix here), lines outside [u0, u1] or without crossings skipped;
   the walk y from v0 at the step: a crossing at or below y is evaluated and appended (the last one ends the walk), and when inside the
   polyline so far goes to dist_to_polyline (hit: uv = (x, interpolated between y - (n - 1 - floor t) step and that + step, the ends
   replaced by the crossings idx[q - 2] (t < 1, q > 1) / idx[q - 1] (t > n - 2))), then the inside flag toggles; between crossings while
   inside the sample (x, y) is appended, at 0x200 points the polyline is tested (uv = (x, the same interpolation without the crossing
   ends)) and restarted from its last point. Pass 0 the same with u and v exchanged (kind 7 base: the u step at most 2). Found: uv
   clamped to the base's domain. The best distance and *dist follow dist_to_polyline's strict improvements. */
static void de_eval(const McamFile *f, const Surf *b, SConst *c, double u, double v, double P[3], int *fail)
{
    if (c && c->dat) { if (curve_const_eval(c, u, v, P)) *fail = 1; return; }
    if (c) { sconst_eval(c, u, v, P); return; }
    double D[10][3];
    if (mcsn_eval(f, b, 0, u, v, D) == 0) memcpy(P, D[0], 3 * sizeof(double));
    else if (mcs_surf_eval(f, b, u, v, P)) *fail = 1;
}
static void de_proj(const double ax[6], const double *p, double o[2])   /* proj_iso_pt */
{
    o[0] = ax[0] * p[0] + ax[1] * p[1] + ax[2] * p[2];
    o[1] = ax[3] * p[0] + ax[4] * p[1] + ax[5] * p[2];
}
static int de_dfl(const double p[2], const double a[2], const double b[2], double maxd, double tol, double *d)   /* dist_to_finite_line */
{
    double dx = b[0] - a[0], dy = b[1] - a[1], L = sqrt(dx * dx + dy * dy), q;
    if (tol <= L) {
        double e = fabs((p[0] - a[0]) * dy - (p[1] - a[1]) * dx) / L;
        *d = e;
        if (maxd <= e) return 0;
        double t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L;
        if (0.0 <= t) {
            if (t <= L) return 1;
            if (L + maxd <= t) return 0;
            q = (b[1] - p[1]) * (b[1] - p[1]) + (b[0] - p[0]) * (b[0] - p[0]);
        } else {
            if (t <= -maxd) return 0;
            q = (a[1] - p[1]) * (a[1] - p[1]) + (a[0] - p[0]) * (a[0] - p[0]);
        }
    } else q = (a[1] - p[1]) * (a[1] - p[1]) + (a[0] - p[0]) * (a[0] - p[0]);
    *d = sqrt(q);
    return *d < maxd;
}
/* dist_to_polyline (0x1802dd3f0): each segment of the projected polyline; a dist_to_finite_line hit within maxd -> *dist, *t = segment index +
   t_on_line clamped to 0 .. 1, *found = 1, maxd = that distance. Returns 1 when any segment hit */
static int de_polyline(int n, double (*P)[3], const double ax[6], const double pk[2], double maxd, double tol, double *dist, double *t, int *found)
{
    double a[2], b[2]; int hit = 0;
    de_proj(ax, P[0], a);
    for (int i = 1; i < n; i++) {
        de_proj(ax, P[i], b);
        double d;
        if (de_dfl(pk, a, b, maxd, tol, &d)) {
            double dx = b[0] - a[0], dy = b[1] - a[1], L2 = dy * dy + dx * dx, s = 0.0;
            if (tol * tol < L2) s = (dy * (pk[1] - a[1]) + dx * (pk[0] - a[0])) / L2;
            if (0.0 <= s) { if (1.0 < s) s = 1.0; } else s = 0.0;
            *dist = d; *t = (double)(i - 1) + s; *found = 1; hit = 1; maxd = d;
        }
        a[0] = b[0]; a[1] = b[1];
    }
    return hit;
}
int sdl_dist_trimmed(const McamFile *f, const Surf *s, const double ax[6], const double pk[2], double best, double wk, double xs, double ys, double tol,
                     double *dist, double uv[2], const char **why)
{
    const uint8_t *r = s->rec;
    if (s->kind != 10 || s->n < 0x138 || !(r[0x6e] & 0x40)) return 0;
    const Surf *b = mcs_surf(f, rd64(r + 0x100));
    if (!b || b == s || b->n < 0x90) return 0;                       /* get_ent_from_eptr fails */
    Loops L; if (!loops_of(s, &L)) { if (why) *why = "a trimmed surface whose loop table cannot be read"; return -1; }
    double u0 = rdd(r + 0x70), u1 = rdd(r + 0x78), v0 = rdd(r + 0x80), v1 = rdd(r + 0x88);
    double bu0 = rdd(b->rec + 0x70), bu1 = rdd(b->rec + 0x78), bv0 = rdd(b->rec + 0x80), bv1 = rdd(b->rec + 0x88);
    int dens = r[0x69], found = 0, fail = 0;
    double (*poly)[3] = malloc(sizeof *poly * 0x200), *H = malloc(sizeof(double) * (2 * 0x100 + 8)); int32_t *idx = malloc(sizeof(int32_t) * (0x100 + 4));
    if (!poly || !H || !idx) { free(poly); free(H); free(idx); if (why) *why = "out of memory"; return -1; }
    for (int pass = 1; pass >= 0 && !fail; pass--) {
        double a = 0.1, c = 0.1;                                      /* surf_division: a the u spacing / step, c the v step / spacing */
        mcs_surf_division(f, s, pass, wk, xs, ys, tol, &a, &c);
        if (pass == 0 && b->kind == 7 && 2.0 < a) a = 2.0;
        double sp = pass ? a : c, st = pass ? c : a;                  /* the line spacing, the sample step */
        double lo = pass ? (u0 <= bu0 ? u0 : bu0) : (v0 <= bv0 ? v0 : bv0), hi = pass ? (bu1 <= u1 ? u1 : bu1) : (bv1 <= v1 ? v1 : bv1);
        double x = lo + sp, xr0 = pass ? u0 : v0, xr1 = pass ? u1 : v1, y0 = pass ? v0 : u0, ye = pass ? v1 : u1;
        int key = pass ? 1 : 0, single = 0;
        SConst *cc = malloc(sizeof *cc); int ci = cc ? sconst_init(cc, f, b, pass) : -1;
        if (ci != 0 && cc) { sconst_free(cc); free(cc); cc = NULL; }
        if (ci == -3) { if (why) *why = "a trimmed surface over a kind 4 surface: its eval_surf_const is NOT PORTED"; fail = 2; break; }
        if (ci == -2) { if (why) *why = "eval_surf_const: the base surface's curve cannot be read or evaluated here"; fail = 2; break; }
        for (int it = 0; it < 1000000 && !fail; it++) {
            if (dens == 0 || (xr1 - xr0) * 0.6 < sp) { single = 1; x = pass ? (u1 + u0) * 0.5 : (v0 + v1) * 0.5; }
            double A[2], B[2];
            if (pass) { A[0] = x; A[1] = v0 - 0.1; B[0] = x; B[1] = v1 + 0.1; } else { A[0] = u0 - 0.1; A[1] = x; B[0] = u1 + 0.1; B[1] = x; }
            int n = hit_polygon(L.nl, L.pts, L.info, A, B, H);
            if (0x100 < n) n = 0x100;
            clip_uv(n, H, u0, u1, v0, v1);
            masort(H, n, 2, key, idx);
            int done = n == 0, in = 0, q = 0, np = 0; double y = y0;
            if (!(xr0 > x) && !(x > xr1) && !done) {
                do {
                    if (ye <= y) { y = ye; done = 1; }
                    if (y < H[2 * idx[q] + key]) {
                        if (in) {
                            double P[3]; de_eval(f, b, cc, pass ? x : y, pass ? y : x, P, &fail);
                            memcpy(poly[np++], P, sizeof P);
                            if (0x200 <= np) {
                                double d = 0, t = 0;
                                if (de_polyline(np, poly, ax, pk, best, tol, &d, &t, &found)) {
                                    best = d; *dist = d; uv[pass ? 0 : 1] = x;
                                    double fl = floor(t), tt = t - fl, bs = y - ((double)np - fl) * st;
                                    uv[pass ? 1 : 0] = (bs + st) * tt + (1.0 - tt) * bs;
                                }
                                memcpy(poly[0], poly[0x1ff], sizeof poly[0]); np = 1;
                            }
                        }
                    } else if (q < n) {
                        do {
                            int k = idx[q];
                            if (y < H[2 * k + key]) break;
                            double P[3]; de_eval(f, b, cc, H[2 * k], H[2 * k + 1], P, &fail);
                            q++; memcpy(poly[np++], P, sizeof P);
                            if (q == n) done = 1;
                            if (in) {
                                int cnt = np; np = 0;
                                double d = 0, t = 0;
                                if (de_polyline(cnt, poly, ax, pk, best, tol, &d, &t, &found)) {
                                    best = d; *dist = d; uv[pass ? 0 : 1] = x;
                                    double fl = floor(t), e0 = y - ((double)(cnt - 1) - fl) * st, e1 = e0 + st;
                                    if (1.0 > t && 1 < q) e0 = H[2 * idx[q - 2] + key];
                                    if ((double)(cnt - 2) < t) e1 = H[2 * idx[q - 1] + key];
                                    double tt = t - fl;
                                    uv[pass ? 1 : 0] = (1.0 - tt) * e0 + e1 * tt;
                                }
                            }
                            in = !in;
                        } while (q < n);
                    }
                    y += st;
                } while (!done && !fail);
            }
            if (single) break;
            x += sp; if (!(x < hi)) break;
        }
        if (cc) { sconst_free(cc); free(cc); }
    }
    free(poly); free(H); free(idx);
    if (fail == 1 && why) *why = "the base surface's evaluation (eval_surf) for this kind is NOT PORTED";
    if (fail) return -1;
    if (found) {
        if (uv[0] < bu0 || bu1 < uv[0]) uv[0] = uv[0] < bu0 ? bu0 : bu1;
        if (uv[1] < bv0 || bv1 < uv[1]) uv[1] = uv[1] < bv0 ? bv0 : bv1;
    }
    return found;
}
