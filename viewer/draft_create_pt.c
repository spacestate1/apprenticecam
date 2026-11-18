/* draft_create_pt - Mastercam 2017's POINT dimension creation (mccore calc_point_dimension @18024ed10), see draft_create_pt.h.
 * Sources (mccore.dll, image base 0x180000000): decomp/draft/core_calcdim.c (calc_point_dimension 2133-2377, FUN_18030f410
 * 5805-6412, FUN_180310700 3094-3280, FUN_180251460 6585-6706, FUN_180250bb0 6923-7037, l_offset 6520-6565, make_wl_single
 * 2526-2648, FUN_18023d590 3618-3820), decomp/mccore_filter.c (proj_pt_ln 4912), decomp/draft/ord_core_helpers.c (Equal 1634),
 * decomp/draft/pt_regen.c (FUN_180305b40); the call arguments Ghidra truncated / mis-typed, read in the asm:
 * decomp/draft/pt_calc.s (calc_point_dimension: the FUN_18030f410 / FUN_180251460 / FUN_180250bb0 / make_wl_single calls,
 * 0x18024f112-0x18024f363), pt_f410.s (FUN_18030f410's prologue and its FUN_180310700 call, 0x18030f474-0x18030f549 /
 * 0x18030f9b8-0x18030f9fc), pt_310700.s (FUN_180310700), pt_250bb0.s (FUN_180250bb0). Helpers shared with the linear port are the
 * dcx_* of draft_create.c (draft_create_int.h). "ours" marks glue of this program. */
#include "draft_create_pt.h"
#include "draft_create_int.h"
#include <math.h>
#include <stdio.h>

/* FUN_180310700 (core_calcdim.c 3094-3280; pt_310700.s): param_4 = LABEL (short), param_5 = USE3D, param_6 / 7 / 8 = format /
   fraction units / digits, param_9 / 10 = leading / trailing zeros (FUN_18030ee50's stack arguments, asm 0x18031081a-0x18031084c,
   scale 1.0). LatheXIsX (= CoordDisp() == 0, 0x18001a1d0) picks the branch:
   - X Y Z (0x1803107b9-0x1803108fe): values 0 .. (USE3D != 0) + 1, each "<label> " (labels "X" "Y" "Z": 0x180d405d4 / 0x180d40560 /
     0x180d40564, the space 0x180cf5440) when LABEL, then the number, ", " (0x180d40568) between them;
   - lathe (0x180310903..): lathe_coord_out(v[1]) (core_calcdim.c 9468: LatheXIsDiameter = CoordDisp() == 2 -> 2 v[1], label "D"
     0x180cf7494, else "X"), ", ", "Z" + v[0]; never a third value */
int dc_point_value(char *out, size_t on, const double v[3], int label, int use3d, int coord_disp, int fmt, int digits, int lead,
                   int trail, int comma) {
    char num[200]; size_t k = 0; out[0] = 0;
#define CAT(s) do { int n_ = snprintf(out + k, k < on ? on - k : 0, "%s", (s)); if (n_ > 0) k += (size_t)n_; if (k >= on) k = on - 1; } while (0)
    if (coord_disp == 0) {
        static const char *lab[3] = { "X", "Y", "Z" };
        int last = (use3d != 0) + 1;
        for (int i = 0; i <= last; i++) {
            if (!fmt_num(num, sizeof num, fmt, digits, v[i], lead, trail, comma)) return 0;   /* FUN_18030ee50 */
            if (label) { CAT(lab[i]); CAT(" "); }
            CAT(num);
            if (i < last) CAT(", ");
        }
    } else {
        int diam = coord_disp == 2;                                /* lathe_coord_out: LatheXIsDiameter */
        double r = diam ? v[1] * 2.0 : v[1];
        if (!fmt_num(num, sizeof num, fmt, digits, r, lead, trail, comma)) return 0;
        if (label) { CAT(diam ? "D" : "X"); CAT(" "); }
        CAT(num); CAT(", ");
        if (!fmt_num(num, sizeof num, fmt, digits, v[0], lead, trail, comma)) return 0;
        if (label) { CAT("Z"); CAT(" "); }
        CAT(num);
    }
#undef CAT
    return 1;
}

/* FUN_18030f410 kind 7 with tolerance mode (param_7) 0 (core_calcdim.c 5805-6097): the values x param_25 (draft_scale; 5869-5873),
   digits 0 -> leading and trailing zeros kept (asm 0x18030f474-0x18030f549), FUN_180310700 into the text, the template (DAT_1813d8d40:
   the fresh value replaces "%v" for get_ext, the template is kept; 6013-6024), get_ext with the dimension font DAT_1813d5630: widths
   [0] / heights [0], their running max / sum (param_18 / param_19), the string count 1. 1 ok, 0 get_ext failed (*param_26 = 0),
   -1 NOT PORTED (X->why) */
static int value_text(Ctx *X, const DcPtInput *in, const double val[3], double *w, double *h, double *maxw, double *toth) {
    const DcSettings *S = X->S;
    double v[3] = { val[0] * S->scale, val[1] * S->scale, val[2] * S->scale };   /* GetProdEnableBits() & 1 (demo two_sig): not modelled */
    memset(X->text, 0, TXT); *maxw = *toth = 0; w[0] = w[1] = w[2] = h[0] = h[1] = h[2] = 0;
    int digits = S->digits, lead = digits ? S->lead0 : 1, trail = digits ? S->trail0 : 1;
    if (!dc_point_value(X->text, 0x12d, v, in->label, in->use3d, in->coord_disp, S->coord_fmt, digits, lead, trail, S->comma)) {
        X->why = "coordinate format 2-4 (NOT PORTED)"; return -1; }
    char meas[TXT];
    if (in->tmpl) { replace_v(in->tmpl, X->text, meas, sizeof meas); snprintf(X->text, 0x12d, "%s", in->tmpl); }
    else snprintf(meas, sizeof meas, "%s", X->text);
    double eh, ew;
    if (get_ext(X->F, meas, X->fa, &eh, &ew)) return 0;
    w[0] = ew; h[0] = eh; *toth += eh; *maxw += ew;
    return 1;
}

/* proj_pt_ln @180002780 (decomp/mccore_filter.c 4912) + Equal @1804b3230 (ord_core_helpers.c 1634) + l_offset @1804b3980
   (core_calcdim.c 6520): the line L (x0, y0, x1, y1) moved by DIST along the unit vector from P's projection on it towards P; OUT
   unchanged (ok 0) when P lies on the line */
static void l_offset_(const double L[4], double dist, const double P[2], double out[4]) {
    double dx = L[2] - L[0], dy = L[3] - L[1], n = dy * dy + dx * dx;
    if (n == 0.0) return;                                          /* proj_pt_ln: *param_5 = 0, the projection left undefined (ours: out unchanged) */
    double t = ((P[1] - L[1]) * dy + (P[0] - L[0]) * dx) / n;
    double qx = (1.0 - t) * L[0] + t * L[2], qy = (1.0 - t) * L[1] + t * L[3];
    double ex = P[0] - qx, ey = P[1] - qy;                         /* Equal(q, P): within tol */
    if (ex <= TOL && -TOL <= ex && ey <= TOL && -TOL <= ey && ey * ey + ex * ex < TOL * TOL) return;
    double d = sqrt(ex * ex + ey * ey), ux = 0.0, uy = 0.0;
    if (d != 0.0) { ux = ex / d; uy = ey / d; }
    out[0] = ux * dist + L[0]; out[1] = uy * dist + L[1]; out[2] = ux * dist + L[2]; out[3] = uy * dist + L[3];
}

/* FUN_180250bb0 (core_calcdim.c 6923; pt_250bb0.s): the leader from the point P to the text block centred at T (MAXW x TOTH), four
   points, all at T's depth: [0] P; [2] / [3] the middles of the text-side edge x = T.x +- MAXW/2 (the side of P; P.x <= T.x -> minus,
   asm 0x180250c97-0x180250cdc) moved away from T by (extra_ln_sp + chr_exp) x H and by extra_ln_sp x H (l_offset with -1 x the distance,
   0x180250cdc-0x180250db0); [1] the bend: from P half the distance to [2], horizontally when the angle P - [2] is within pi/4 of 0 /
   pi / 2 pi (0x180250e65-0x180250ee2: |a - pi| <= pi/4, |a - 2 pi| < pi/4, |a| < pi/4), else vertically towards T. Visible, kind 1,
   4 points (0x180250efa-0x180250f03). H = dim_text_height (0x1812d9938), extra_ln_sp (0x1812d9928), chr_exp (0x1812d9920): the
   calc_point_dimension call's stack arguments (asm 0x18024f2a1-0x18024f2e9) */
static void f250bb0(WL *L, double maxw, double toth, const P3 *T, const P3 *P, double H, double els, double ce) {
    for (int i = 0; i < 4; i++) { L->pts[i][0] = T->x; L->pts[i][1] = T->y; L->pts[i][2] = T->z; }
    L->pts[0][0] = P->x; L->pts[0][1] = P->y;
    double x = P->x <= T->x ? T->x - maxw * 0.5 : maxw * 0.5 + T->x;
    double line[4] = { x, T->y - toth * 0.5, x, toth * 0.5 + T->y }, o[4] = { 0, 0, 0, 0 }, tp[2] = { T->x, T->y };
    l_offset_(line, (els + ce) * (H * -1.0), tp, o);
    double x2 = (o[2] + o[0]) * 0.5, y2 = (o[3] + o[1]) * 0.5;
    l_offset_(line, H * els * -1.0, tp, o);
    L->pts[2][0] = x2; L->pts[2][1] = y2;
    L->pts[3][0] = (o[2] + o[0]) * 0.5; L->pts[3][1] = (o[3] + o[1]) * 0.5;
    double dx = P->x - x2, dy = P->y - y2, a = 0.0;
    if (!(1e-12 > dy * dy + dx * dx)) { a = atan2(dy, dx); if (a < 0.0) a += TWOPI; }
    double half = sqrt(dy * dy + dx * dx) * 0.5;
    L->pts[1][0] = P->x; L->pts[1][1] = P->y;
    const double q = 0.7853981633974483, pi = 3.141592653589793;
    if (fabs(a - pi) <= q || fabs(a - TWOPI) < q || fabs(a) < q) L->pts[1][0] = x2 > P->x ? P->x + half : P->x - half;
    else L->pts[1][1] = T->y > P->y ? P->y + half : P->y - half;
    L->b[3] = 1; *npts(L) = 4; L->b[1] = 1;
}

DcResult dc_calc_point(uint8_t E[0x810], const DcPtInput *in, const DcSettings *S, const DcFonts *F) {
    DcResult R; memset(&R, 0, sizeof R);
    /* l.2206-2222 (asm 0x18024ed91-0x18024ee0b): the tolerance mode sVar8 = DAT_1813d6210, mode 1 -> 3 when the tolerances are equal /
       DIN; local_128 = (mode != 0), 2 for mode 1. Only mode 0 is ported (FUN_18030f410 / FUN_180251460's tolerance strings are not) */
    if (S->tol_mode != 0) { R.not_ported = 1; R.why = "linear tolerance mode 1-3 (NOT PORTED)"; return R; }
    if (!in->view) { R.not_ported = 1; R.why = "view 0 (graphics-view fallback NOT PORTED)"; return R; }
    DcInput li; memset(&li, 0, sizeof li); li.view = in->view; memcpy(li.vm, in->vm, sizeof li.vm); li.tmpl = in->tmpl;   /* ours: the
                                                                      context the shared helpers (placement / offsets) read */
    Ctx X; memset(&X, 0, sizeof X); X.S = S; X.F = F; X.in = &li;
    memcpy(X.fa, S->fa, 0x60);                                     /* init_attribs_dim(&DAT_1813d5630) (asm 0x18024f07a) */
    /* l.2224-2232: view coordinates of the point (local_120) and of the cursor (local_100); the depth: the point's when the construction
       view is 3-D, else the cursor's for both (asm 0x18024ee49-0x18024ee64) */
    P3 Pv, Tv;
    w2v(in->vm, (const double *)(const void *)(in->ap + 8), &Pv); w2v(in->vm, in->cursor, &Tv);
    if (S->constr3d) Tv.z = Pv.z; else Pv.z = Tv.z;
    /* l.2233-2287 (asm 0x18024ee69-0x18024f06a): header and body */
    P32(E, 0, 0x40); memset(E + 0x20, 0, 8); E[6] = (uint8_t)S->color; P32(E, 8, (uint32_t)S->level); memcpy(E + 0x10, S->attrib, 16);
    P16(E, 4, 1);
    memcpy(E + 0x68, in->ap, 0x98);                                /* aPointCopy (0x98 bytes, core_calcdim.c 3823) */
    E[0x100] = (uint8_t)in->use3d; E[0x101] = (uint8_t)in->label; E[0x102] = (uint8_t)in->absolute;
    E[0x308] = 4; P16(E, 0x328, in->view); E[0x7ca] = 0; PD(E, 0x330, S->scale);
    PD(E, 0x310, Tv.x); PD(E, 0x318, Tv.y); PD(E, 0x320, Tv.z);
    E[0x7cb] = (uint8_t)S->comma; E[0x7cc] = (uint8_t)S->lead0; E[0x7cf] = (uint8_t)S->trail0; E[0x7d0] = (uint8_t)S->constr3d;
    E[0x7cd] = (uint8_t)S->digits;
    PD(E, 0x338, Pv.x);                                            /* the point's view x only (asm 0x18024ef52) */
    PD(E, 0x340, S->pos_tol); PD(E, 0x348, S->neg_tol); PD(E, 0x350, S->gap); PD(E, 0x358, S->ext);
    E[0x378] = 0; E[0x410] = 0; E[0x4a8] = 0; E[0x540] = 0;
    P32(E, 0x7a2, 0); { uint64_t one = 1; memcpy(E + 0x7a6, &one, 8); }   /* +0x7a6 = 1, +0x7a8 / +0x7aa / +0x7ac (witness mode) = 0 */
    P16(E, 0x7ae, 1); P16(E, 0x7b0, S->arrows_out); P16(E, 0x7b2, 0); P16(E, 0x7b4, S->ldr_style); P16(E, 0x7b6, 0);
    P16(E, 0x7b8, 0);                                              /* local_128 = (DAT_1813d6210 != 0) = 0 */
    P16(E, 0x7ba, S->metric != 0);                                 /* IsMetric */
    P16(E, 0x7c0, S->coord_fmt); P16(E, 0x7c2, S->frac_units);
    E[0x7c6] = (uint8_t)S->din_tol; E[0x7c7] = (uint8_t)S->din_chr; E[0x7c8] = (uint8_t)S->din_val; E[0x7c9] = (uint8_t)S->auto_align;
    P16(E, 0x7d1, 0);
    /* l.2289-2306 (asm 0x18024f07f-0x18024f110): the values = the point's view coordinates (depth as above) less the active origin in
       view coordinates when ActiveOriginIsOn(Cplane); ptdim_abs: the point's world coordinates */
    double val[3] = { Pv.x, Pv.y, Pv.z };
    if (in->origin_on) { val[0] = Pv.x - in->origin_v[0]; val[1] = Pv.y - in->origin_v[1]; val[2] = Pv.z - in->origin_v[2]; }
    if (in->absolute) memcpy(val, in->ap + 8, 24);
    /* FUN_18030f410(&cursor, 7, coordinate_format, fraction_units, ptdim_label, ptdim_use3d, mode, ...) (asm 0x18024f112-0x18024f244) */
    double ws[3], hs[3], maxw, toth;
    int r = value_text(&X, in, val, ws, hs, &maxw, &toth);
    if (r < 0) { R.not_ported = 1; R.why = X.why; return R; }
    if (r == 0) { R.why = "get_ext failed (fonts)"; return R; }    /* *param_5 = 0: nothing more is written (l.2331) */
    /* FUN_180251460(E+0x5d8, fa, angle 0.0, widths, heights, count 1, text, &cursor, mode 0, tol_text_height) (asm 0x18024f254-0x18024f29c;
       core_calcdim.c 6585-6625): box_limits(mode 0), slot 0 at x = T.x - bx + w0 / 2, y = T.y, rotated about T by 0 (FUN_18024b000 =
       rotate_pt), z = T.z; FUN_180252d60 slot 0 (core_dimlayout2.c 165): FUN_180252790(1, 0.0, fa) = the slot font, the position,
       strcpy_bb of the text (core_draftwf.c 1707: unbounded), slot +0x1b8 = 0; one string: no other slot */
    {
        double bx, by, px, py;
        box_limits(0, 1, D(X.fa, 0x40) * D(X.fa, 0x28), hs, ws, &bx, &by);
        rot((Tv.x - bx) + ws[0] * 0.5, Tv.y, Tv.x, Tv.y, cos(0.0), sin(0.0), &px, &py);
        size_t n = strlen(X.text);
        if (0x650 + n + 1 > 0x810) { R.not_ported = 1; R.why = "value text past the record (strcpy_bb overflow, NOT PORTED)"; return R; }
        slot_font(E + 0x5d8, X.fa, 0.0); PD(E, 0x638, px); PD(E, 0x640, py); PD(E, 0x648, Tv.z);
        memcpy(E + 0x650, X.text, n + 1); memset(E + 0x790, 0, 8);
    }
    /* FUN_180250bb0(E+0x4a8, maxw, toth, &cursor, &point, dim_text_height, extra_ln_sp, chr_exp) then make_wl_single(E+0x4a8, 1, 1,
       DAT_1813d61fa, arrow_head_height, arrow_head_width, 0, ...) (asm 0x18024f2a1-0x18024f363; make_wl_single core_calcdim.c 2526:
       param_7 = 0, no separate W_ID). The font globals are the dimension font's (init_attribs_dim copied them: +0x28 / +0x40 / +0x30) */
    {
        WL L; memset(&L, 0, sizeof L); memcpy(L.b, E + 0x4a8, 0x98);
        f250bb0(&L, maxw, toth, &Tv, &Pv, D(X.fa, 0x28), D(X.fa, 0x40), D(X.fa, 0x30));
        make_wl_single(&L, 1, 1, S->arrow_type, S->arrow_h, S->arrow_w);
        memcpy(E + 0x4a8, L.b, 0x98);
    }
    /* FUN_18023d590 case 4 (core_calcdim.c 3700-3706): FUN_18023dad0 the placement point -> body+0x2a8 (E+0x310), then the text offsets
       from the point's view position (world_to_view(body+8): its own depth) at angle 0 (FUN_18023de60 gives 0 for type 4) */
    double pl[3];
    if (!placement(&X, E, pl)) { R.why = "get_ext failed (fonts)"; return R; }
    PD(E, 0x310, pl[0]); PD(E, 0x318, pl[1]); PD(E, 0x320, pl[2]);
    P3 Pr; w2v(in->vm, (const double *)(const void *)(E + 0x70), &Pr);
    offsets(&X, E, &Pr, 0.0);
    R.valid = 1;                                                   /* DraftIsAssoc: no effect on the record */
    { double th, tw; if (!get_ext(F, (const char *)E + 0x650, E + 0x5d8, &th, &tw)) { R.text_w = tw; R.text_h = th; } }
    return R;
}

/* FUN_180305b40 (decomp/draft/pt_regen.c): world_to_view(the aPoint as FUN_180303a80 re-evaluated it) + (+0x360, +0x368), depth +0x370,
   view_to_world in the record's view +0x328 (DAT_1813d8cd2 set: +0x310 instead - the in-place regeneration, not used here) */
void dc_point_regen_cursor(const uint8_t *E, const double vm[9], double cursor[3]) {
    P3 p; w2v(vm, (const double *)(const void *)(E + 0x70), &p);
    double v[3] = { p.x + D(E, 0x360), p.y + D(E, 0x368), D(E, 0x370) };
    for (int i = 0; i < 3; i++) cursor[i] = vm[i] * v[0] + vm[3 + i] * v[1] + vm[6 + i] * v[2];
}
