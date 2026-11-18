/* draft_create_circ - Mastercam 2017's circular (radius / diameter) dimension creation, see draft_create_circ.h.
 *
 * Port of mccore.dll (Ghidra VAs, image base 0x180000000); decompiles in decomp/draft/ ("file:line"; circ_*.c = this session's dumps),
 * argument order read in `objdump -d mccore.dll` where Ghidra truncated a call ("asm"):
 *   calc_circular_dimension @18024b810 (core_calcdim.c 672-795), radial branch FUN_18024f3c0 (core_calcdim.c 4816-4970)
 *   FUN_180250f60 (core_dimlayout2.c; args asm 0x180250f60-0x18025124c)              -> f250f60 (in dc_calc_circular)
 *   FUN_18024a3c0 radial layout (core_dimlayout.c 1318; asm 0x18024a3c0-0x18024ae6e) -> f24a3c0
 *   FUN_180251840 text attachment / angle (core_dimlayout.c; asm 0x180251840-0x180251edb) -> f251840
 *   FUN_180256700 the two leaders (circ_core.c = decomp/draft/circ_core.c; asm 0x180256700-0x1802570ac) -> f256700
 *   FUN_18024c160 text box (core_dimgeo.c 3-37) -> box4; FUN_180254bc0 (draft_create.c f254bc0)
 *   FUN_180253540 record assembly (core_dimlayout.c; args asm 0x180251183-0x18025124c) -> f253540
 *   FUN_18023d590 case 2 (core_calcdim.c 3700-3704) + FUN_18023dad0 / FUN_18023e0b0 / FUN_18023de60 (core_createport.c)
 * Not ported: the quadrant style (*param_4 == 0: quad_to_eps + calc_linear_dimension, core_calcdim.c 750-781), map_arc_to_parallel_view
 * (arc view != active view), tolerance modes 1-3, DraftIsAssoc, GetProdEnableBits two_sig rounding (all as draft_create.c). */
#include "draft_create_circ.h"
#include "draft_create_int.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static double ang2(double dx, double dy) {                         /* the inline atan2 idiom: < 1e-12 -> 0, else [0, 2 pi) */
    if (dx * dx + dy * dy < 1e-12) return 0.0;
    double a = atan2(dy, dx); if (a < 0.0) a += TWOPI; return a;
}

/* FUN_18024c160 (core_dimgeo.c 3-37): text box corners c0 (x-, y-), c1 (x+, y-), c2 (x-, y+), c3 (x+, y+) around T (half extents from
   box_limits(mode, nstr, PAD)), grown by PAD4, rotated about T by ANG */
static void box4(int nstr, const P3 *T, int mode, double pad4, double ang, const double *ws, const double *hs, double pad, double *box) {
    double bx, by; box_limits(mode, nstr, pad, hs, ws, &bx, &by);
    box[0] = box[4] = T->x - (bx + pad4); box[1] = box[3] = T->y - (by + pad4);
    box[2] = box[6] = bx + pad4 + T->x; box[5] = box[7] = by + pad4 + T->y;
    double c = cos(ang), s = sin(ang);
    for (int k = 0; k < 4; k++) rot(box[2 * k], box[2 * k + 1], T->x, T->y, c, s, &box[2 * k], &box[2 * k + 1]);
}

typedef struct {                                                   /* the value-text arguments FUN_18024a3c0 hands down */
    Ctx *X; int isr; double r; int mode;
    double ws[3], hs[3], maxw, toth; int nstr; int ok;
} Txt;
static int value_text(Txt *t, int sym) {                           /* FUN_18030f410(T, 4 - is_radius, ..., value r or 2 r) */
    double v = t->isr ? t->r : t->r * 2.0;
    int r = f410(t->X, t->isr ? 3 : 4, sym, v, t->X->text, t->ws, t->hs, &t->maxw, &t->toth, &t->nstr);
    t->ok = r;
    return r;
}

/* FUN_180251840 (asm 0x180251840-0x180251edb): the angle from the centre C to the text, the leader attachment point ATT and SIDE
   (1: box corner c0 nearer C than c1). LS leader style, INSIDE |T - C| < r, TOR text orientation (horizontal), H text height */
static double f251840(int *side, int ls, const P3 *T, const P3 *C, int inside, int tor, Txt *t, int sym, double pad, double h,
                      double *att) {
    double h2 = h * 2.0;
    att[0] = T->x; att[1] = T->y;
    double a = ang2(T->x - C->x, T->y - C->y);
    double box[8];
    if (!ls && inside) { for (int k = 0; k < 4; k++) { box[2 * k] = C->x; box[2 * k + 1] = C->y; } }   /* 0x1802519b2-0x1802519d1 */
    else {
        if (value_text(t, sym) <= 0) return a;                     /* in_stack_f0 = 0: always FUN_18030f410 (asm 0x1802519d6) */
        box4(t->nstr, T, t->mode, ls ? 0.0 : pad, 0.0, t->ws, t->hs, pad, box);   /* DAT_1813d5640 = 0 here (FUN_180250f60) */
    }
    double c0x = box[0], c0y = box[1], c1x = box[2], c1y = box[3], c2y = box[5];
    double d0 = hypot(C->x - c0x, C->y - c0y), d1 = hypot(C->x - c1x, C->y - c1y);
    *side = d1 > d0;                                               /* 0x180251c41 seta */
    if (!inside) {
        if (tor && !ls) { double m = (c2y - c0y) * 0.5 + c0y; c0y = c1y = m; }   /* 0x180251c70 */
        if (*side) {
            if (tor) { c0x -= h2 + pad; a = ang2(c0x - C->x, c0y - C->y); }
            att[0] = c0x; att[1] = c0y; return a;
        }
        if (tor) {
            c1x += h2 + pad;
            double dx = c1x - C->x, dy = c1y - C->y;
            if (dx * dx + dy * dy < 1e-12) { att[0] = c1x; att[1] = c1y; return 0.0; }
            a = ang2(dx, dy);
        }
        att[0] = c1x; att[1] = c1y; return a;
    }
    if (!ls) return a;                                             /* 0x180251e13 */
    if ((C->x > c0x && c0y > C->y) || (c0x > C->x && C->y > c0y)) return ang2(c0x - C->x, c0y - C->y);   /* 0x180251e46 */
    return ang2(c1x - C->x, c1y - C->y);                           /* 0x180251e99 */
}

/* FUN_180256700 (circ_core.c, asm 0x180256700-0x1802570ac): the two leaders A (-> E+0x4a8) and B (-> E+0x540) in the frame rotated by
   -A about C; QP / QM = C +/- r (cos a, sin a); OFF = how far the text moves */
static void f256700(int lv, int ao, int ls, int isr, double pad, int tor, int outside, int side, const double *att, double a,
                    double h, double *Qp, double *Qm, const P3 *T, const P3 *C, const double *box, double *off, WL *A, WL *B) {
    double h2 = h * 2.0;
    if (!ls) { A->b[3] = lv != 2; B->b[3] = lv != 1; }           /* 1: A only, 2: B only, else both */
    else A->b[3] = B->b[3] = 1;
    double c = cos(-a), s = sin(-a), rp[2], rm[2], rt[2], rb[8];
    rot(Qp[0], Qp[1], C->x, C->y, c, s, &rp[0], &rp[1]);
    rot(Qm[0], Qm[1], C->x, C->y, c, s, &rm[0], &rm[1]);
    rot(T->x, T->y, C->x, C->y, c, s, &rt[0], &rt[1]);
    double miny = 0;
    for (int k = 0; k < 4; k++) {
        rot(box[2 * k], box[2 * k + 1], C->x, C->y, c, s, &rb[2 * k], &rb[2 * k + 1]);
        if (k == 0 || rb[2 * k + 1] <= miny) miny = rb[2 * k + 1];
    }
    double (*pa)[3] = A->pts, (*pb)[3] = B->pts;
    pa[0][0] = Qp[0]; pa[0][1] = Qp[1]; pb[0][0] = Qm[0]; pb[0][1] = Qm[1];
    int16_t *na = npts(A), *nb = npts(B); *na = 2; *nb = 2;
    if (!ao) { pa[1][0] = C->x; pa[1][1] = C->y; pb[1][0] = C->x; pb[1][1] = C->y; }
    else {
        double e = ang2(Qp[0] - C->x, Qp[1] - C->y), ce = cos(e), se = sin(e);
        pa[1][0] = ce * h2 + Qp[0]; pa[1][1] = se * h2 + Qp[1];
        pb[1][0] = Qm[0] - ce * h2; pb[1][1] = Qm[1] - se * h2;
        if (ls) { pa[2][0] = C->x; pa[2][1] = C->y; pb[2][0] = C->x; pb[2][1] = C->y; *na = 3; *nb = 3; }
    }
    double xb = rm[0];
    if (isr) { Qm[0] = C->x; Qm[1] = C->y; rm[0] = C->x; xb = C->x; }
    double cb = cos(a), sb = sin(a), deg = -a * DEG;
    if (!ao && !outside && !ls) {                                  /* text inside, arrows inside: both leaders end at the box */
        double lo = rp[0], hi = xb; double (*plo)[3] = pa, (*phi)[3] = pb;
        if (!(rp[0] <= xb)) { lo = xb; hi = rp[0]; plo = pb; phi = pa; }
        B->b[3] = 1;
        if (!isr) { pb[0][0] = Qm[0]; pb[0][1] = Qm[1]; } else { pb[0][0] = C->x; pb[0][1] = C->y; }
        double v = f254bc0(1, rt[0], lo, hi, rb, C->y) - pad;
        if (!(v > lo)) v = lo;
        rot(v, C->y, C->x, C->y, cb, sb, &plo[1][0], &plo[1][1]);
        v = f254bc0(0, rt[0], lo, hi, rb, C->y) + pad;
        if (!(hi > v)) v = hi;
        rot(v, C->y, C->x, C->y, cb, sb, &phi[1][0], &phi[1][1]);
        goto end;
    }
    if (isr) { B->b[3] = 0; *nb = 0; }
    if (outside) {
        int n = ao ? 2 : 3, idx = n - 1;
        pa[idx][0] = att[0]; pa[idx][1] = att[1]; *na = (int16_t)n;
        if (!tor) {
            int dir1 = !ls ? C->x < Qp[0] : !(Qp[0] > C->x);        /* 0x180256f11 / 0x18025704b */
            double v = dir1 ? f254bc0(1, rt[0], rp[0], xb, rb, C->y) - pad : f254bc0(0, rt[0], rp[0], xb, rb, C->y) + pad;
            rot(v, C->y, C->x, C->y, cb, sb, &pa[idx][0], &pa[idx][1]);
            if (!ls) goto end;
            pa[*na][0] = C->x; pa[*na][1] = C->y; (*na)++;
            pb[*nb][0] = C->x; pb[*nb][1] = C->y; (*nb)++;
        } else {
            if (!ls) {
                double d = side ? h2 : -h2;
                pa[n][0] = d + pa[n - 1][0];
            } else {
                pa[n][0] = side ? box[2] + pad : box[0] - pad;     /* local_110 = c1.x, local_108 = c0.x (unrotated) */
                if (ao) {
                    if (!isr) { pb[*nb - 1][0] = Qp[0]; pb[*nb - 1][1] = Qp[1]; }
                    else { pb[0][0] = C->x; pb[0][1] = C->y; pb[1][0] = Qp[0]; pb[1][1] = Qp[1]; B->b[3] = 1; *nb = 2; }
                }
            }
            pa[*na][1] = pa[*na - 1][1]; (*na)++;
        }
    }
    if (ls && (!tor || !outside)) {                                /* 0x180256ff0-0x1802570ac */
        double dd = (C->y - miny) + pad, ad = fabs(deg);
        if (ad < 180.0 || ad > 270.0) rt[1] += dd; else rt[1] -= dd;
    }
end:;
    double ox, oy; rot(rt[0], rt[1], C->x, C->y, cb, sb, &ox, &oy);
    off[0] = ox - T->x; off[1] = oy - T->y;
    if (ls && outside && tor) off[1] += pad;
    for (int i = 0; i < *na && i < 4; i++) pa[i][2] = T->z;        /* FUN_18000dd30(T, 2) */
    for (int i = 0; i < *nb && i < 4; i++) pb[i][2] = T->z;
}

/* FUN_18024a3c0 (asm 0x18024a3c0-0x18024ae6e): T (view, moved in place), C, the arc's start / sweep, r; returns the text angle written
   to DAT_1813d5640 in *FANG (0 when the text is horizontal), the leaders in A / B */
static int f24a3c0(Ctx *X, P3 *T, const P3 *C, double start, double sweep, double r, int isr, int ao, int autoal, int ls, int sym,
                   int lv, int tor, double *fang, Txt *t, WL *A, WL *B) {
    double h = D(X->fa, 0x28), pad = D(X->fa, 0x40) * h;          /* DAT_1813d5658 x DAT_1813d5670 */
    if (lv == 2) lv = 1;
    double d = hypot(C->x - T->x, C->y - T->y);
    int inside = r > d, aoe = ao;
    if (inside) { if (isr) aoe = 0; if (lv > 0) lv = 3; }
    int side = 0; double att[2];
    double a = f251840(&side, ls, T, C, inside, tor, t, sym, pad, h, att);
    if (t->ok < 0) return -1;
    /* the arc's angular range (0x18024a6b0-0x18024a70c): AL = 1 plain [lo, hi], 0 wrapped (outside (lo, hi)) */
    double e = sweep + start, lo, hi; int al = 1;
    if (0.0 > sweep) { lo = e; hi = start; } else { lo = start; hi = e; }
    if (hi > TWOPI) { double x = hi - TWOPI; al = 0; hi = lo; lo = x; }
    if (0.0 > lo) { double x = lo + TWOPI; al = 0; lo = hi; hi = x; }
    int off_arc = 0, opp = 0;
    int in_arc = al ? !(a > hi) && !(lo > a) : (hi <= a || a <= lo);
    if (!in_arc) {
        off_arc = 1;
        int near = 0;
        if (isr && aoe) near = 1;
        else {
            double a2 = a + 3.141592653589793; if (a2 > TWOPI) a2 -= TWOPI;
            int in2 = al ? !(a2 > hi) && !(lo > a2) : (hi <= a2 || a2 <= lo);
            if (in2) { opp = 1; off_arc = 0; a = a2; } else near = 1;
        }
        if (near) {                                                /* 0x18024a8ee: the nearer arc end */
            double dlo = hypot(cos(lo) * r + C->x - T->x, sin(lo) * r + C->y - T->y);
            double dhi = hypot(cos(hi) * r + C->x - T->x, sin(hi) * r + C->y - T->y);
            a = dhi > dlo ? lo : hi;
        }
    }
    double sn = sin(a), cs = cos(a);
    double Qp[2] = { cs * r + C->x, sn * r + C->y }, Qm[2] = { C->x - cs * r, C->y - sn * r };
    if (autoal && inside) {                                        /* 0x18024a7e8 */
        if (isr) {
            double dp = hypot(Qp[0] - T->x, Qp[1] - T->y), dm = hypot(Qm[0] - T->x, Qm[1] - T->y);
            if ((dm > dp && !opp) || off_arc) { T->y = Qp[1] - r * 0.5 * sn; T->x = Qp[0] - r * 0.5 * cs; }
            else { T->y = r * 0.5 * sn + Qm[1]; T->x = r * 0.5 * cs + Qm[0]; }
        } else { T->x = C->x; T->y = C->y; }
    }
    P3 T2 = *T;
    double a0 = a, a1 = a;                                         /* 0x18024aa1b-0x18024aa94 */
    a1 -= (a1 - 3.141592653589793 > 1e-06) ? 3.141592653589793 : 0.0;
    a1 += (a1 - 1.570796326794896 > 1e-06) ? 3.141592653589793 : 0.0;
    *fang = tor ? 0.0 : a1;
    double ap = a1;
    if (opp) { if (a0 > 4.712388980384688) ap = a0 - 3.141592653589793; else { ap = a0; if (1.570796326794896 > a0) ap = a0 + 3.141592653589793; } }
    if (value_text(t, sym) <= 0) return t->ok < 0 ? -1 : 0;        /* in_stack_108 = 0: FUN_18030f410 (0x18024aab2) */
    double box[8];
    box4(t->nstr, T, t->mode, ls ? 0.0 : pad, *fang, t->ws, t->hs, pad, box);
    double off[2];
    f256700(lv, aoe, ls, isr, pad, tor, !inside, side, att, ap, h, Qp, Qm, &T2, C, box, off, A, B);
    if (!ls) { A->b[3] = lv != 2; B->b[3] = lv != 1; }            /* 0x18024ada0 */
    T->x += off[0]; T->y += off[1];
    return 1;
}

/* FUN_180253540 (core_dimlayout.c; args asm 0x180251183-0x18025124c): the record. VALUE r, LV / AO the globals as passed */
static void f253540(Ctx *X, uint8_t *E, int ls, int lv, int ao, int sym, int view, double fang, double r, const P3 *T, int isr,
                    Txt *t, WL *A, WL *B) {
    const DcSettings *S = X->S;
    E[0x7cb] = (uint8_t)S->comma; E[0x7cc] = (uint8_t)S->lead0; E[0x7cf] = (uint8_t)S->trail0; E[0x7cd] = (uint8_t)S->digits;
    E[0x7d0] = (uint8_t)S->constr3d; E[0x7c9] = (uint8_t)S->auto_align; E[0x308] = 2; E[0x7ca] = 0; PD(E, 0x330, S->scale);
    P16(E, 0x328, view);
    PD(E, 0x338, isr ? r : r * 2.0); P16(E, 0x7b2, isr ? S->radius_fmt : S->diam_fmt);
    P32(E, 0x7a6, 0x10001); P16(E, 0x7ae, lv); P32(E, 0x7a2, 0); P16(E, 0x7ac, 0);
    int t1 = S->arrow_type, t2 = S->arrow_type;
    if (ls) { t2 = 3; if (lv != 1) { t2 = S->arrow_type; if (lv == 2) t1 = 3; } }
    if (isr) t2 = 3;
    P16(E, 0x7c0, S->coord_fmt); P16(E, 0x7b0, (int8_t)ao); P16(E, 0x7c2, S->frac_units); P16(E, 0x7b2, sym); P16(E, 0x7b4, ls);
    P16(E, 0x7b6, S->tol_mode);                                    /* tolerance mode 0 (the only one ported): +0x7b8 = 0 */
    PD(E, 0x340, S->pos_tol); PD(E, 0x348, S->neg_tol);            /* FUN_1802579a0(E+0x68, pos / neg_linear_tolerance, DIN) */
    E[0x7c6] = (uint8_t)S->din_tol; E[0x7c7] = (uint8_t)S->din_chr; E[0x7c8] = (uint8_t)S->din_val;
    P16(E, 0x7b8, 0);
    E[0x378] = 0;
    make_wl_single(A, 1, 1, t1, S->arrow_h, S->arrow_w); memcpy(E + 0x4a8, A->b, 0x98);
    /* FUN_180251460 slot 0 (tolerance mode 0): x = T.x - bx + w0 / 2, rotated about T by the text angle (as draft_create.c layout) */
    double tbx, tby, px, py; box_limits(S->tol_mode, t->nstr, D(X->fa, 0x40) * D(X->fa, 0x28), t->hs, t->ws, &tbx, &tby);
    rot((T->x - tbx) + t->ws[0] * 0.5, T->y, T->x, T->y, cos(fang), sin(fang), &px, &py);
    slot_font(E + 0x5d8, X->fa, fang); PD(E, 0x638, px); PD(E, 0x640, py); PD(E, 0x648, T->z);
    memset(E + 0x650, 0, 0x58); { size_t n = strlen(X->text); memcpy(E + 0x650, X->text, n < 0x57 ? n : 0x57); } memset(E + 0x790, 0, 8);
    make_wl_single(B, 1, 1, t2, S->arrow_h, S->arrow_w); memcpy(E + 0x540, B->b, 0x98);
    E[0x410] = 0;
    if (*npts(B) < 2 && isr) { P16(E, 0x7a8, 0); E[0x540] = 0; }
}


/* pt_on_arc (systypes.dll, decomp/pocket_b6/zigzag_decomp_zz_sys.c 3-64): P within TOLR of the arc's circle and inside its sweep (+-
   tol / r) */
static int pt_on_arc(double px, double py, double cx, double cy, double r, double start, double sweep, double tolr) {
    if (fabs(hypot(cx - px, cy - py) - r) > tolr) return 0;
    double a = ang2(px - cx, py - cy), lo, hi, e = 0.0;
    if (sweep <= 0.0) { hi = start; lo = start + sweep; } else { lo = start; hi = start + sweep; }
    if (1e-12 < r) e = tolr / r;
    while (hi + e < a) a -= TWOPI;
    while (a < lo - e) a += TWOPI;
    return !(a < lo - e || hi + e < a);
}
/* df_rotate_pt_about_pt (exported, as draft_create.c rot_deg about a point): DEG degrees about C */
static void rot_about(double px, double py, double cx, double cy, double deg, double *ox, double *oy) {
    if (fabs(deg) >= TOL) { rot(px, py, cx, cy, cos(deg * RAD), sin(deg * RAD), ox, oy); } else { *ox = px; *oy = py; }
}
/* FUN_180245a40 (circ_core.c): the four quadrant points E, N, W, S of the arc (view), rotated by HVP about the centre, and whether
   each lies on the arc (pt_on_arc, tol) */
static void quads(const DcCircInput *in, double hvp, double q[4][3], int on[4]) {
    static const int dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };
    for (int k = 0; k < 4; k++) {
        double x = in->centre[0] + dx[k] * in->r, y = in->centre[1] + dy[k] * in->r;
        if (fabs(hvp) <= TOL) { q[k][0] = x; q[k][1] = y; } else rot_about(x, y, in->centre[0], in->centre[1], hvp, &q[k][0], &q[k][1]);
        q[k][2] = in->centre[2];
        on[k] = pt_on_arc(q[k][0], q[k][1], in->centre[0], in->centre[1], in->r, in->start, in->sweep, TOL);
    }
}
/* determine_cl @18023ff80 (circ_core.c): T (view) outside the circle and inside the band of the quadrant points -> 0 (the quadrant
   style) and the quadrant code; else 2 (radial). DAT_1813c79b0 is 0 (.bss, no writer in mccore: 8 reads only) */
int dc_determine_cl(const DcCircInput *in, const double tv[3], double hvp, int diameter, int *quad) {
    double cx = in->centre[0], cy = in->centre[1], cz = in->centre[2];
    *quad = 0;
    double d = sqrt((cy - tv[1]) * (cy - tv[1]) + (cx - tv[0]) * (cx - tv[0]) + (cz - tv[2]) * (cz - tv[2]));
    if (!(in->r < d)) return 2;
    double q[4][3]; int on[4]; quads(in, hvp, q, on);
    double tx, ty; rot_about(tv[0], tv[1], cx, cy, -hvp, &tx, &ty);
    int band_y = ty <= q[1][1] && q[3][1] <= ty;
    if (!((tx <= q[0][0] || band_y) && (q[2][0] <= tx || band_y))) return 2;
    if (diameter == 1) {
        if (tx <= q[0][0] && q[2][0] <= tx && on[0] && on[2]) { *quad = cy <= ty ? 2 : 4; return 0; }
        if (band_y && on[1] && on[3]) { *quad = cx <= tx ? 1 : 3; return 0; }
    } else if (diameter == 0) {
        double a = ang_rads(cx, cy, tx, ty) * DEG;                 /* df_angle_of_line = df_angle_of_line_rads x 57.29.. (asm 0x180241a00) */
        if (0.0 <= a && a <= 90.0 && on[0] && on[1]) { *quad = (45.0 < a) + 1; return 0; }
        if (90.0 <= a && a <= 180.0 && on[1] && on[2]) { *quad = (135.0 <= a) + 3; return 0; }
        if (180.0 <= a && a <= 270.0 && on[2] && on[3]) { *quad = (225.0 <= a) + 5; return 0; }
        if (270.0 <= a && a <= 360.0 && on[3] && on[0]) { *quad = (315.0 < a) + 7; return 0; }
    }
    return 2;
}
/* quad_to_eps @180245700 (core_calcdim.c 5009-5158) + FUN_180245600 (circ_core.c): the two quadrant aPoints of the quadrant style
   ('A' = the arc at the angle t (degrees + DfHvpAngle), 'K' when the arc has no eptr) and the linear orientation; 0 = those points are
   not on the arc */
static int quad_to_eps(const DcCircInput *in, int quad, int diameter, double hvp, uint8_t ap[2][0x98], int *orient) {
    double q[4][3]; int on[4]; quads(in, hvp, q, on);
    int i0, i1, ok;
    if (diameter == 1) {
        if (quad == 2 || quad == 4) { i0 = 0; i1 = 2; *orient = 0; }
        else if (quad == 1 || quad == 3) { i0 = 1; i1 = 3; *orient = 1; }
        else return 0;
    } else if (diameter == 0) {
        if (quad == 1 || quad == 2) { i0 = 0; i1 = 1; *orient = quad != 2; }
        else if (quad == 3 || quad == 4) { i0 = 1; i1 = 2; *orient = quad != 3; }
        else if (quad == 5 || quad == 6) { i0 = 2; i1 = 3; *orient = quad != 6; }
        else if (quad == 7 || quad == 8) { i0 = 3; i1 = 0; *orient = quad != 7; }
        else return 0;
    } else return 0;
    ok = on[i0] && on[i1];
    int idx[2] = { i0, i1 };
    for (int j = 0; j < 2; j++) {                                  /* aPointInitView(arc view, the point) = view_to_world */
        double w[3]; for (int i = 0; i < 3; i++) w[i] = in->vm[i] * q[idx[j]][0] + in->vm[3 + i] * q[idx[j]][1] + in->vm[6 + i] * q[idx[j]][2];
        dc_apoint_world(ap[j], w, in->eptr ? 'A' : 'K');
        if (in->eptr) dc_apoint_ent(ap[j], 0, in->eptr, idx[j] * 90.0 + hvp);
    }
    return ok;
}

int dc_circ_quad_eps(const DcCircInput *in, int quad, int diameter, double hvp, uint8_t ap[2][0x98], int *orient) { return quad_to_eps(in, quad, diameter, hvp, ap, orient); }

DcResult dc_calc_circular(uint8_t E[0x810], const DcCircInput *in, const DcSettings *S, const DcFonts *F) {
    DcResult R; memset(&R, 0, sizeof R);
    DcInput li; memset(&li, 0, sizeof li); li.view = in->view; memcpy(li.vm, in->vm, sizeof li.vm); li.tmpl = in->tmpl; li.ctrl = in->ctrl;
    Ctx X; memset(&X, 0, sizeof X); X.S = S; X.F = F; X.in = &li; memcpy(X.fa, S->fa, 0x60);
    if (S->tol_mode != 0) { R.not_ported = 1; R.why = "tolerance mode 1-3 (NOT PORTED)"; return R; }
    if (!in->view) { R.not_ported = 1; R.why = "view 0 (graphics-view fallback NOT PORTED)"; return R; }
    if (in->arc_view != in->view) { R.not_ported = 1; R.why = "arc view != active view (views_parallel / map_arc_to_parallel_view NOT PORTED)"; return R; }
    if (in->style == 0) {                                          /* calc_circular_dimension l.750-781: the quadrant style */
        uint8_t ap[2][0x98]; int orient = 0;
        if (quad_to_eps(in, in->quad, in->diameter, in->hvp, ap, &orient)) {
            DcInput q; memset(&q, 0, sizeof q); memcpy(q.ap1, ap[0], 0x98); memcpy(q.ap2, ap[1], 0x98);
            memcpy(q.cursor, in->cursor, sizeof q.cursor); q.view = in->view; memcpy(q.vm, in->vm, sizeof q.vm);
            q.orient = orient; q.hvp = in->hvp; q.value_mode = in->diameter + 1; q.tmpl = in->tmpl; q.ctrl = in->ctrl;
            R = dc_calc_linear(E, &q, S, F);
            if (R.not_ported || !R.valid) return R;
            double w[3]; for (int i = 0; i < 3; i++) w[i] = in->vm[i] * in->centre[0] + in->vm[3 + i] * in->centre[1] + in->vm[6 + i] * in->centre[2];
            E[0x308] = 2;
            uint8_t a0[0x98]; dc_apoint_world(a0, w, in->assoc ? 'C' : 'K'); if (in->assoc) dc_apoint_ent(a0, 0, in->eptr, 0.0);
            memcpy(E + 0x68, a0, 0x98);
            E[0x101] = (uint8_t)in->diameter; E[0x100] = 0; E[0x110] = (uint8_t)in->quad; PD(E, 0x108, in->hvp);
            PD(E, 0x118, in->r); PD(E, 0x120, in->start); PD(E, 0x128, in->sweep);
            double pl[3];                                          /* FUN_18023d590 case 2 */
            if (!placement(&X, E, pl)) { R.valid = 0; R.why = "get_ext failed (fonts)"; return R; }
            PD(E, 0x310, pl[0]); PD(E, 0x318, pl[1]); PD(E, 0x320, pl[2]);
            P3 cv; w2v(in->vm, (const double *)(const void *)(E + 0x70), &cv);
            offsets(&X, E, &cv, D(E, 0x108));
            R.style = 0;
            return R;                                              /* DfTurnOffApAssoc (arc without eptr: never here, ours passes one) / DraftIsAssoc: not ported */
        }
        /* quad_to_eps failed: *param_4 = 2, the radial branch */
    }
    R.style = 2;
    /* FUN_18024f3c0 (core_calcdim.c 4816-4970) */
    P3 T; w2v(in->vm, in->cursor, &T);
    if (S->constr3d) T.z = in->centre[2];                          /* calc_circular_dimension l.763-765: the arc's depth */
    int isr = in->diameter == 0;
    P32(E, 0, 0x40); memset(E + 0x20, 0, 8); E[6] = (uint8_t)S->color; P32(E, 8, (uint32_t)S->level); P16(E, 4, 1);
    E[0x100] = 2; memcpy(E + 0x10, S->attrib, 16); E[0x101] = (uint8_t)!isr;
    {   /* aPointInitView(E+0x68, view, arc centre, 'C'' / 'K') + aPointSaveEntSel(arc, t 0): view_to_world = the transposed rows */
        double w[3]; for (int i = 0; i < 3; i++) w[i] = in->vm[i] * in->centre[0] + in->vm[3 + i] * in->centre[1] + in->vm[6 + i] * in->centre[2];
        uint8_t ap[0x98]; dc_apoint_world(ap, w, in->assoc ? 'C' : 'K');
        if (in->assoc) dc_apoint_ent(ap, 0, in->eptr, 0.0);
        memcpy(E + 0x68, ap, 0x98);
    }
    E[0x110] = 1; PD(E, 0x108, 0.0); PD(E, 0x118, in->r); PD(E, 0x120, in->start); E[0x308] = 2; P16(E, 0x328, in->view); E[0x7ca] = 0;
    PD(E, 0x128, in->sweep); PD(E, 0x330, S->scale);
    E[0x7cb] = (uint8_t)S->comma; E[0x7cc] = (uint8_t)S->lead0; E[0x7cf] = (uint8_t)S->trail0; E[0x7cd] = (uint8_t)S->digits; E[0x7d0] = (uint8_t)S->constr3d;
    P3 C = { in->centre[0], in->centre[1], in->centre[2] };
    double d = hypot(C.y - T.y, C.x - T.x);
    int sym = isr ? S->radius_fmt : S->diam_fmt, autoal = S->auto_align;
    if (!autoal && d <= in->r && !in->ctrl) {                      /* the centring snaps (l.4904-4945) */
        double th = D(S->fa, 0x28), m = th < S->sel_dist ? th : S->sel_dist;   /* dim_text_height = the font height */
        if (isr) {
            if (in->r * 0.5 - m * 0.5 < d && d < m * 0.5 + in->r * 0.5) autoal = 1;
            else if (d < m * 0.5) { T.x = C.x; T.y = C.y; }        /* l.4925-4932: T to the centre (view x, y) */
        } else if (d < m * 0.5) autoal = 1;
    }
    int ao = S->arrows_out;
    if (ao == 0 && S->arrow_type != 3 && in->r - S->arrow_h < 0.0) ao = 1;   /* l.4947-4951 */
    /* FUN_180250f60: init_attribs_dim, DAT_1813d5640 = 0, FUN_180310b30 (template), FUN_18024a3c0, FUN_180253540, FUN_180310b20 */
    DcSettings S2 = *S; S2.auto_align = autoal; X.S = &S2;
    Txt t; memset(&t, 0, sizeof t); t.X = &X; t.isr = isr; t.r = in->r; t.mode = S->tol_mode;
    WL A, B; memset(&A, 0, sizeof A); memset(&B, 0, sizeof B);
    double fang = 0.0;
    int r = f24a3c0(&X, &T, &C, in->start, in->sweep, in->r, isr, ao, autoal, S->ldr_style, sym, S->ldr_vis, S->text_orient != 0, &fang, &t, &A, &B);
    if (r < 0) { R.not_ported = 1; R.why = X.why; return R; }
    if (r == 0) { R.why = "get_ext failed (fonts)"; return R; }
    f253540(&X, E, S->ldr_style, S->ldr_vis, ao, sym, in->view, fang, in->r, &T, isr, &t, &A, &B);
    /* FUN_18023d590 case 2: placement (FUN_18023dad0) and the text offsets from the centre, angle +0x108 (0 -> FUN_18023de60 = 0) */
    double pl[3];
    if (!placement(&X, E, pl)) { R.why = "get_ext failed (fonts)"; return R; }
    PD(E, 0x310, pl[0]); PD(E, 0x318, pl[1]); PD(E, 0x320, pl[2]);
    P3 cv; w2v(in->vm, (const double *)(const void *)(E + 0x70), &cv);
    offsets(&X, E, &cv, D(E, 0x108));
    E[0x7c9] = (uint8_t)S->auto_align;                             /* l.4964-4966: the global restored and stored */
    R.valid = 1;
    { double th, tw; if (!get_ext(F, (const char *)E + 0x650, E + 0x5d8, &th, &tw)) { R.text_w = tw; R.text_h = th; } }
    return R;
}
