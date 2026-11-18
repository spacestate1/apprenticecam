/* draft_ord - Mastercam 2017's ORDINATE dimension record (see draft_ord.h).
 *
 * Port of mccore.dll calc_ordinate_dimension @18024d3d0 (decomp/draft/core_calcdim.c 1234-2070), statement by statement from its
 * disassembly (decomp/draft/ord_core_calc_asm.txt, Ghidra DisasmAt 0x18024d3d0-0x18024eb60: the decompile loses the FUN_18030f410 / get_ext / FUN_180251460 arguments
 * and several stores; "asm 0x..." = the instruction cited). Callees:
 *   Point2Plane @180023da0 (core_calcdim.c 6567): world_to_view, z = the depth
 *   dist_pt_l @1804b32a0 (core_calcdim.c 6414): unsigned distance to the infinite line (a line shorter than tol: to its first point)
 *   l_offset @1804b3980 (core_calcdim.c 6520): the line moved by D towards P (fails when P is on the line: Equal @1804b3230)
 *   proj_pt_ln (systypes 0x180002780, decomp/mccore_filter.c 4912): projection on the infinite line (fails for a zero-length line)
 *   box_limits / get_ext / FUN_18030f410 / make_wl_single / FUN_180252790 (viewer/draft_create.c, the linear port's)
 *   FUN_180251460 text slot (core_calcdim.c 6585-6700; tolerance mode 0: the slot at the text point)
 *   FUN_18023d590 case 3 (core_calcdim.c 3618-3720): the placement point from the slot (FUN_18023dad0, core_createport.c 3-77) and the
 *   text offsets from the aPoint (FUN_18023e0b0, core_createport.c 79-117, angle FUN_18023de60 = 0 for type 3)
 *   DfOrdType @1802f1710 (core_draft_rec.c 715-757), FUN_180255830 (decomp/draft/ord_core_helpers.c 1288; the regeneration's text start)
 * Not ported: tolerance modes 1-3 (FUN_18030f410's tolerance strings, the 0x7b6 / 0x7b8 rule is kept), a base with tokens or tolerance
 * strings (DfCheckForTokens -> calc_dim_note), TrueType extents, FUN_1802579d0's graphics-view fallback (view 0), DraftIsAssoc (a pure
 * query for type 3: DimIsAssoc core_draft_rec.c 812-818). */
#include "draft_ord.h"
#include "mcam_load.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOL (mc_systol.tol)                                         /* tol @1812b5b58 (config 1000, mc_systol) */
#define TWOPI 6.283185307179586                                    /* 0x180cf3b18 */
#define HALFPI 1.570796326794896                                   /* 0x180cf3b00 */

static int16_t I16(const uint8_t *b, size_t o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static double D(const uint8_t *b, size_t o) { double v; memcpy(&v, b + o, 8); return v; }
static void PD(uint8_t *b, size_t o, double v) { memcpy(b + o, &v, 8); }
static void P16(uint8_t *b, size_t o, int v) { int16_t s = (int16_t)v; memcpy(b + o, &s, 2); }
static void P32(uint8_t *b, size_t o, uint32_t v) { memcpy(b + o, &v, 4); }
static void P64(uint8_t *b, size_t o, uint64_t v) { memcpy(b + o, &v, 8); }

typedef struct { double x, y; } P2;
typedef struct { P2 a, b; } L2;

static void w2v(const double m[9], const double w[3], double v[3]) {   /* world_to_view */
    v[0] = m[0] * w[0] + m[1] * w[1] + m[2] * w[2]; v[1] = m[3] * w[0] + m[4] * w[1] + m[5] * w[2]; v[2] = m[6] * w[0] + m[7] * w[1] + m[8] * w[2];
}
static void v2w(const double m[9], const double v[3], double w[3]) {   /* view_to_world (the view matrix is orthonormal) */
    for (int i = 0; i < 3; i++) w[i] = m[i] * v[0] + m[3 + i] * v[1] + m[6 + i] * v[2];
}
static P2 p2plane(const double m[9], const double w[3], double depth) {   /* Point2Plane: the view x, y (z = depth) */
    double v[3]; w2v(m, w, v); (void)depth; return (P2){ v[0], v[1] };
}
static double dist_pt_l(P2 p, L2 l) {
    double dx = p.x - l.a.x, dy = p.y - l.a.y, ly = l.b.y - l.a.y, lx = l.b.x - l.a.x, n = sqrt(ly * ly + lx * lx);
    if (n <= TOL) return sqrt(dy * dy + dx * dx);
    double d = (dx * ly - dy * lx) / n;
    return d < 0.0 ? -d : d;
}
static int proj_pt_ln(P2 p, L2 l, P2 *o) {
    double dx = l.b.x - l.a.x, dy = l.b.y - l.a.y, d = dy * dy + dx * dx;
    if (d == 0.0) return 0;
    double t = ((p.y - l.a.y) * dy + (p.x - l.a.x) * dx) / d;
    double y = (1.0 - t) * l.a.y + t * l.b.y;
    o->x = (1.0 - t) * l.a.x + t * l.b.x; o->y = y;
    return 1;
}
static int equal2(P2 a, P2 b) {                                    /* Equal @1804b3230 */
    double dx = b.x - a.x, dy;
    if (dx <= TOL && -TOL <= dx && (dy = b.y - a.y) <= TOL && -TOL <= dy) return dy * dy + dx * dx < TOL * TOL;
    return 0;
}
static int l_offset(L2 l, double dist, P2 p, L2 *o) {
    P2 q = p;                                                      /* (a zero-length line: proj_pt_ln leaves its output - never here) */
    proj_pt_ln(p, l, &q);
    if (equal2(q, p)) return 0;
    double ux = p.x - q.x, uy = p.y - q.y, n = sqrt(ux * ux + uy * uy), vy = 0.0;
    if (n == 0.0) ux = 0.0; else { ux = ux / n; vy = uy / n; }
    o->a.x = ux * dist + l.a.x; o->a.y = vy * dist + l.a.y; o->b.x = ux * dist + l.b.x; o->b.y = vy * dist + l.b.y;
    return 1;
}
static void rot(double px, double py, double cx, double cy, double c, double s, double *ox, double *oy) {   /* rotate_pt @1804b4550 */
    double dx = px - cx, dy = py - cy, y = dy * c + dx * s;
    *ox = cx + (dx * c - dy * s); *oy = y + cy;
}

int do_orient_of(const uint8_t E[0x810]) {                         /* FUN_180046920 (mccad) / FUN_1803053a0 (core_dimlayout.c 3112-3122) */
    double a = D(E, 0x5e8);
    if (!(fabs(a - HALFPI) > 1e-06 || fabs(a - HALFPI) == 1e-06)) return 0;
    return fabs(a) < 1e-06 ? 1 : 2;
}

int do_ord_type(uint8_t E[0x810], const uint8_t *parent) {
    if (E[0x308] != 3) return -1;
    /* (param_1 == the record's own parent eptr: cleared) - the caller passes no self reference */
    if (parent && parent[0] == 0x40 && parent[0x308] == 3) {       /* DfEptrValid, get_ent_from_eptr, selection bit 1 (alive), class 0x40 */
        uint8_t P[0x810]; memcpy(P, parent, 0x810);
        if (do_ord_type(P, NULL) == 1) return 0;                   /* the parent's own parent is not followed (it is a zero ordinate) */
    }
    double o = D(E, 0x100);
    if (TOL < fabs(o) || TOL == fabs(o)) { PD(E, 0x338, o); return 2; }
    PD(E, 0x100, 0.0); PD(E, 0x338, 0.0);
    return 1;
}

/* FUN_18023dad0 (core_createport.c 3-77), tolerance strings none: the placement point from the text slot */
static int placement(const uint8_t *E, const DcFonts *F, double pl[3]) {
    pl[0] = D(E, 0x638); pl[1] = D(E, 0x640); pl[2] = D(E, 0x648);
    double h[3] = { 0, 0, 0 }, w[3] = { 0, 0, 0 }, bx, by;
    if (dc_text_ext(F, (const char *)E + 0x650, E + 0x5d8, &h[0], &w[0])) return 0;
    int m = I16(E, 0x7b6); if (m == 3) m = 1;
    dc_box_limits(m, I16(E, 0x7b8) + 1, D(E, 0x618) * D(E, 0x600), h, w, &bx, &by);
    double a = D(E, 0x5e8), c = cos(a), s = sin(a);
    pl[0] = pl[0] - (c * w[0] + s * h[0]) * 0.5; pl[1] = pl[1] - (s * w[0] + c * h[0]) * 0.5;
    pl[0] = s * by + c * bx + pl[0]; pl[1] = s * bx + c * by + pl[1];
    return 1;
}

void do_text_ends(const uint8_t E[0x810], const DcFonts *F, const double T[3], double before[3], double after[3]) {
    double h[3] = { 0, 0, 0 }, w[3] = { 0, 0, 0 }, bx = 0, by = 0;
    if (!dc_text_ext(F, (const char *)E + 0x650, E + 0x5d8, &h[0], &w[0])) {   /* DfCheckForTokens + get_ext (tokens: the stored text) */
        int m = I16(E, 0x7b6); if (m == 3) m = 1;
        dc_box_limits(m, I16(E, 0x7b8) + 1, D(E, 0x618) * D(E, 0x600), h, w, &bx, &by);
    }
    double a = D(E, 0x5e8), c = cos(a) * bx, s = sin(a) * bx;
    before[0] = T[0] - c; before[1] = T[1] - s; before[2] = T[2];
    after[0] = T[0] + c; after[1] = T[1] + s; after[2] = T[2];
}

DoResult do_calc_ordinate(uint8_t E[0x810], DoInput *in, const DoSettings *O, const DcFonts *F) {
    const DcSettings *S = &O->S;
    DoResult R; memset(&R, 0, sizeof R);
    if (S->tol_mode != 0) { R.not_ported = 1; R.why = "linear tolerance mode 1-3 (NOT PORTED)"; return R; }
    if (!in->view) { R.not_ported = 1; R.why = "view 0 (graphics-view fallback NOT PORTED)"; return R; }
    int autoal = S->auto_align;                                    /* [RBP-0x7c] = auto_alignment (asm 0x18024d551) */
    int h = O->h_aln;                                              /* [RBP-0x50] = h_txt_aln (asm 0x18024d4ee) */
    double old = in->type == 2 ? D(E, 0x100) : 0.0;                /* XMM12 (asm 0x18024d4fb) */
    double fH = D(S->fa, 0x28), fE = D(S->fa, 0x30), fL = D(S->fa, 0x40);   /* dim_text_height, chr_exp, extra_ln_sp (init_attribs_dim) */
    double row = fE * fH + fL * fH;                                /* XMM15 (asm 0x18024d514-0x18024d536) */
    double halfH = fH * 0.5;                                       /* [RBP+0x118] */
    double ang = HALFPI;                                           /* XMM11 */
    int view; double depth, vm[9];
    L2 L; P2 P6;                                                   /* local_698 (the zero line, view) and [RBP+0xa0] (the text-row origin) */
    uint8_t wl[0x98]; memset(wl, 0, sizeof wl);                    /* local_538: the leader being built */
    int orient = in->orient;
    if (in->has_base) {                                            /* asm 0x18024d582-0x18024da1f */
        const uint8_t *B = in->base;
        if (!B || B[0x308] != 3) return R;                         /* -> LAB_18024eae5 */
        memcpy(wl, B + 0x4a8, 0x98);
        view = I16(B, 0x328); depth = D(B, 0x648);
        if (!mcam_std_view(view ? view : 1, vm)) { R.not_ported = 1; R.why = "base view >= 9 (NOT PORTED)"; return R; }
        int nb = I16(B, 0x7b8) + 1, tm = I16(B, 0x7b6);
        L.a = (P2){ D(B, 0x4c8), D(B, 0x4d0) }; L.b = (P2){ D(B, 0x4e0), D(B, 0x4e8) };
        const uint8_t *bfa = B + 0x5d8;
        double bpad = D(bfa, 0x40) * D(bfa, 0x28);
        if (B[0x7a0] && strchr((const char *)B + 0x650, '%')) { R.not_ported = 1; R.why = "base text with tokens (DfCheckForTokens, NOT PORTED)"; return R; }
        if (I16(B, 0x7b8) > 0) { R.not_ported = 1; R.why = "base with tolerance strings (NOT PORTED)"; return R; }
        char bt[0x60]; memcpy(bt, B + 0x650, 0x58); bt[0x58] = 0;
        double hs[3] = { 0, 0, 0 }, ws[3] = { 0, 0, 0 }, bx, by;
        if (dc_text_ext(F, bt, bfa, &hs[0], &ws[0])) { R.why = "get_ext failed (fonts)"; return R; }
        dc_box_limits(tm == 3 ? 1 : tm, nb, bpad, hs, ws, &bx, &by);
        double a = D(bfa, 0x10), c = cos(a), s = sin(a);           /* DAT_1813d5640 = the base text angle */
        double tx = D(B, 0x638), ty = D(B, 0x640);
        double x = tx - (hs[0] * s + ws[0] * c) * 0.5;
        double y = ty - (ws[0] * s + hs[0] * c) * 0.5;
        x = x + (s * by + c * bx);
        y = y + (c * by + s * bx);
        if (!(fabs(a - HALFPI) > 1e-06 || fabs(a - HALFPI) == 1e-06)) orient = 0;
        else if (!(fabs(a) > 1e-06 || fabs(a) == 1e-06)) orient = 1;
        else orient = 2;
        in->orient = orient;                                       /* *param_10 (asm 0x18024d98f / 0x18024d99e) */
        if (h != 1) {                                              /* asm 0x18024d9a2-0x18024da10 */
            if (orient == 0) y = h == 0 ? y - bx : y + bx;
            else if (orient == 1) x = h == 0 ? x - bx : x + bx;
            else { if (h == 0) { x = x - c * bx; y = y - s * bx; } else { x = x + c * bx; y = y + s * bx; } }
        }
        P6 = (P2){ x, y };
    } else {                                                       /* asm 0x18024da24-0x18024db0d */
        depth = in->depth; view = in->view; memcpy(vm, in->vm, sizeof vm);
        L.a = p2plane(vm, in->p4, depth); L.b = p2plane(vm, in->p5, depth); P6 = p2plane(vm, in->p6, depth);
    }
    P2 A = p2plane(vm, (const double *)(const void *)(in->ap + 8), depth);   /* [RBP-0x60] */
    P2 T = p2plane(vm, in->text, depth);                           /* [RBP-0x70] */
    double pad = fL * fH;                                          /* [RBP+0x90] = DAT_1813d5670 * DAT_1813d5658 */
    double d, dx = 0, dy = 0;
    P2 L1 = L.b;                                                   /* XMM9 / XMM10 for orientations 0 / 1 */
    if (orient == 0) { d = A.x - L.a.x; dx = L1.x; dy = L1.y; }
    else if (orient == 1) { d = A.y - L.a.y; ang = 0.0; dx = L1.x; dy = L1.y; }
    else if (orient == 2) {
        if (L.a.x > L.b.x) { dy = L.a.y - L.b.y; dx = L.a.x - L.b.x; } else { dy = L.b.y - L.a.y; dx = L.b.x - L.a.x; }
        d = dist_pt_l(A, L);
        double rx = A.x - L.a.x, ry = A.y - L.a.y;
        if (rx * dy + ry * -dx > 0.0) d = d * -1.0;
        if (dy * dy + dx * dx < 1e-12) ang = 0.0; else { ang = atan2(dy, dx); if (ang < 0.0) ang = ang + TWOPI; }
    } else return R;
    if (!O->neg_sign) d = fabs(d);
    double value = d;
    if (in->type > 0) value = 0.0;
    if (in->type == 2) value = old;
    L2 TL = { P6, P6 };                                            /* [RBP+0x30]: the text row */
    if (orient == 0) TL.b.x = P6.x + 10.0;
    else if (orient == 1) TL.b.y = P6.y + 10.0;
    else { TL.b.x = dy + P6.x; TL.b.y = TL.b.y - dx; }
    double thr = S->sel_dist <= fH ? S->sel_dist : fH;             /* min(sel_dist, dim_text_height) */
    int ok = 1;
    if (in->type == 0) {                                           /* asm 0x18024de33-0x18024df61 */
        L2 OL;
        if (l_offset(L, fabs(d), A, &OL)) {                        /* the leader's straight line through the point */
            if (fabs(dist_pt_l(T, OL)) < thr * 0.5) { P2 t = T; proj_pt_ln(t, OL, &T); }
        }
        if (!autoal && !in->ctrl && fabs(dist_pt_l(T, TL)) < thr * 0.5) autoal = 1;   /* auto_alignment = 1 (kept for +0x7c9) */
        if (autoal) { P2 t = T; ok = proj_pt_ln(t, TL, &T); }
    } else {                                                       /* asm 0x18024dd8a-0x18024de2e */
        int a2 = autoal;
        if (!a2 && !in->ctrl && fabs(dist_pt_l(T, L)) < thr * 0.5) a2 = 1;
        if (a2) { P2 t = T; ok = proj_pt_ln(t, L, &T); }
        /* auto_alignment restored (asm 0x18024de24) */
    }
    (void)ok;                                                      /* FUN_18030f410 sets *param_26 = 1 first */
    /* FUN_180310b30(E): the template flag +0x7a0 (the text kept, the value formatted into it) */
    char tmpl[0x60]; const char *tp = NULL;
    if (E[0x7a0]) { memcpy(tmpl, E + 0x650, 0x58); tmpl[0x58] = 0; tp = tmpl; }
    char text[0x387]; double ws[3], hs[3], W = 0, H = 0; int nstr = 1; const char *why = NULL;
    int r = dc_value_text(S, F, 5, 0, value, tp, text, ws, hs, &W, &H, &nstr, &why);   /* kind 5 (asm 0x18024df86), symbol style 0 */
    if (r < 0) { R.not_ported = 1; R.why = why; return R; }
    if (r == 0) { R.why = "get_ext failed (fonts)"; goto end; }
    P2 T2 = T;                                                     /* [RBP-0x40]: the text centre */
    if (h != 1) {                                                  /* asm 0x18024e0c1-0x18024e1a0 */
        double hw = W * 0.5;
        if (orient == 0) T2.y = h == 0 ? T.y + hw : T.y - hw;
        else if (orient == 1) T2.x = h == 0 ? T.x + hw : T.x - hw;
        else { double c = cos(ang) * hw; if (h == 0) { T2.x = c + T.x; T2.y = T2.y + sin(ang) * hw; } else { T2.x = T.x - c; T2.y = T2.y - sin(ang) * hw; } }
    }
    P2 Q = T;                                                      /* [RBP+0xb0] local_658: the point the text end is projected from */
    wl[3] = 1; P16(wl, 0x1a, 4);
    TL.a = T2; TL.b = T2;
    if (orient == 0) TL.b.x = T2.x + 10.0;
    else if (orient == 1) TL.b.y = T2.y + 10.0;
    else { TL.b.x = T2.x + dy; TL.b.y = TL.b.y - dx; }
    double off = W * 0.5 + halfH;
    L2 OL;
    if (!l_offset(TL, off, A, &OL)) {                              /* the point on the text row: moved 0.01 (asm 0x18024e253-0x18024e2ba) */
        if (orient == 0) A.y = A.y + 0.01; else if (orient == 1) A.x = A.x + 0.01; else { A.x = A.x + 0.01; A.y = A.y + 0.01; }
        if (!l_offset(TL, off, A, &OL)) goto end;
    }
    if (S->ldr_style != 0) {                                       /* asm 0x18024e2c0-0x18024e385 */
        L2 O2;
        if (!l_offset(OL, W + halfH, T2, &O2)) goto end;
        OL = O2;
        double k = H * 0.5 + pad;
        Q.x = sin(ang) * k + T2.x;
        double cy = cos(ang) * k;
        Q.y = orient == 0 ? cy + T2.y : T2.y - cy;
        off = W * 0.5 + halfH;
    }
    P2 p3, p2, p1;
    if (!proj_pt_ln(Q, OL, &p3)) goto end;
    off = off + row;
    if (!l_offset(TL, off, A, &OL) || !proj_pt_ln(Q, OL, &p2)) goto end;
    row = row * 2.0; off = off + row;
    if (!l_offset(TL, off, A, &OL) || !proj_pt_ln(A, OL, &p1)) goto end;
    P2 p0 = A;
    if (O->arrowhd) {                                              /* asm 0x18024e4da-0x18024e585 */
        double ex = p1.x - A.x, ey = p1.y - A.y, a = 0.0;
        if (!(1e-12 > ey * ey + ex * ex)) { a = atan2(ey, ex); if (0.0 > a) a = a + TWOPI; }
        p0.x = cos(a) * S->gap + A.x; p0.y = sin(a) * S->gap + A.y;
    }
    double pts[4][2] = { { p0.x, p0.y }, { p1.x, p1.y }, { p2.x, p2.y }, { p3.x, p3.y } };
    double d0 = dist_pt_l(p0, L), d3 = dist_pt_l(p3, L);
    if (fabs(d0 - d3) < TOL) { pts[1][0] = p3.x; pts[1][1] = p3.y; P16(wl, 0x1a, 2); }   /* straight: two points */
    for (int i = 0; i < 4; i++) { PD(wl, 0x20 + 0x18 * i, pts[i][0]); PD(wl, 0x28 + 0x18 * i, pts[i][1]); PD(wl, 0x30 + 0x18 * i, depth); }
    /* the record (asm 0x18024e60a-0x18024e8dd) */
    P32(E, 0, 0x40); P64(E, 0x20, 0); E[6] = (uint8_t)S->color; P32(E, 8, (uint32_t)S->level); memcpy(E + 0x10, S->attrib, 16); P16(E, 4, 1);
    E[0x308] = 3; PD(E, 0x338, value);
    PD(E, 0x310, T2.x); PD(E, 0x318, T2.y); PD(E, 0x320, depth); P16(E, 0x328, view);
    E[0x7ca] = 0; E[0x7cb] = (uint8_t)S->comma; E[0x7cc] = (uint8_t)S->lead0; E[0x7cf] = (uint8_t)S->trail0; E[0x7cd] = (uint8_t)S->digits;
    E[0x7d0] = (uint8_t)S->constr3d;
    PD(E, 0x330, S->scale); PD(E, 0x340, S->pos_tol); PD(E, 0x348, S->neg_tol); PD(E, 0x350, S->gap); PD(E, 0x358, S->ext);
    E[0x378] = 0; E[0x410] = 0; E[0x4a8] = 0; E[0x540] = 0;
    P32(E, 0x7a2, 0); P64(E, 0x7a6, 1); P32(E, 0x7ae, 1); P16(E, 0x7b2, 0); P16(E, 0x7b4, S->ldr_style);
    P16(E, 0x7ba, S->metric != 0); P16(E, 0x7c0, S->coord_fmt); P16(E, 0x7c2, S->frac_units);
    E[0x7c6] = (uint8_t)S->din_tol; E[0x7c7] = (uint8_t)S->din_chr; E[0x7c8] = (uint8_t)S->din_val; E[0x7c9] = (uint8_t)autoal;
    {   int tm = S->tol_mode;                                      /* DAT_1813d6210 (asm 0x18024e7bd-0x18024e872) */
        P16(E, 0x7b6, tm == 1 ? (nstr == 2 ? 3 : 1) : tm);
        int t6 = I16(E, 0x7b6);
        if (t6 == 0) P16(E, 0x7b8, 0); else if (t6 == 1) P16(E, 0x7b8, 2); else if (t6 == 2) P16(E, 0x7b8, nstr == 3 ? 2 : 1); else if (t6 == 3) P16(E, 0x7b8, 1); }
    /* FUN_1802579a0 (core_calcdim.c 6449): the tolerances and DIN again */
    memcpy(E + 0x68, in->ap, 0x98);                                /* aPointCopy */
    E[0x122] = (uint8_t)h; E[0x124] = (uint8_t)O->neg_sign; E[0x125] = (uint8_t)O->arrowhd;
    PD(E, 0x100, value); PD(E, 0x338, value);
    P64(E, 0x108, in->type == 0 ? in->base_eptr : 0);
    E[0x123] = 0;
    dc_make_wl(wl, 1, 1, S->arrow_type, S->arrow_h, S->arrow_w);   /* make_wl_single(kind 1, arrow, DAT_1813d61fa, arrow_head_height / width) */
    memcpy(E + 0x4a8, wl, 0x98);
    {   /* FUN_180251460 (tolerance mode 0): slot 0 at (T2.x - bx + w0 / 2, T2.y) rotated about T2 by the angle, FUN_180252d60 */
        double bx, by; dc_box_limits(I16(E, 0x7b6) == 3 ? 1 : I16(E, 0x7b6), nstr, D(S->fa, 0x40) * D(S->fa, 0x28), hs, ws, &bx, &by);
        double px, py; rot((T2.x - bx) + ws[0] * 0.5, T2.y, T2.x, T2.y, cos(ang), sin(ang), &px, &py);
        dc_slot_font(E + 0x5d8, S->fa, ang); PD(E, 0x638, px); PD(E, 0x640, py); PD(E, 0x648, depth);
        memset(E + 0x650, 0, 0x58); size_t n = strlen(text); memcpy(E + 0x650, text, n < 0x57 ? n : 0x57); memset(E + 0x790, 0, 8);
    }
    if (in->type > 0) {                                            /* the leader's first points and the text point back to the caller (world) */
        double v[3];
        v[0] = D(wl, 0x20); v[1] = D(wl, 0x28); v[2] = D(wl, 0x30); v2w(vm, v, in->p4);
        v[0] = D(wl, 0x38); v[1] = D(wl, 0x40); v[2] = D(wl, 0x48); v2w(vm, v, in->p5);
        v[0] = T2.x; v[1] = T2.y; v[2] = depth; v2w(vm, v, in->p6);
    }
    {   /* FUN_18023d590 case 3: placement (body+0x2a8) and the offsets from the aPoint (body+0x2f8 / 0x300 / 0x308) */
        double pl[3];
        if (placement(E, F, pl)) {
            PD(E, 0x310, pl[0]); PD(E, 0x318, pl[1]); PD(E, 0x320, pl[2]);
            double av[3]; w2v(vm, (const double *)(const void *)(E + 0x70), av);
            placement(E, F, pl);
            double ox = fabs(av[0] - pl[0]), oy = fabs(av[1] - pl[1]);
            PD(E, 0x360, pl[0] < av[0] ? -ox : ox); PD(E, 0x368, pl[1] < av[1] ? -oy : oy); PD(E, 0x370, D(E, 0x320));
        }
    }
    R.valid = 1;
end:
    E[0x7c9] = (uint8_t)S->auto_align;                             /* asm 0x18024eacf: the saved auto_alignment */
    return R;
}

/* DraftGlobalsFromDim @180308cc0 (core_draftwf.c 540-716): the session globals from dimension record ES (the fields the ordinate
   computation reads; sel_dist, the version stamp and the units-only items are left as they are) */
void do_globals_from_dim(const uint8_t *Es, DoSettings *O) {
    DcSettings *S = &O->S;
    memcpy(S->fa, Es + 0x5d8, 0x60);                               /* init_attribs_dim of the font globals it sets */
    const uint8_t *l1 = Es + 0x4a8, *l2 = Es + 0x540;              /* the arrow: leader 1 if on with an arrow, else leader 2, else 1 */
    if (l1[0] && l1[0x18] != 3) { S->arrow_h = D(l1, 8); S->arrow_w = D(l1, 0x10); S->arrow_type = l1[0x18]; }
    else if (l2[0] && l2[0x18] != 3) { S->arrow_h = D(l2, 8); S->arrow_w = D(l2, 0x10); S->arrow_type = l2[0x18]; }
    else if (l1[0]) { S->arrow_h = D(l1, 8); S->arrow_w = D(l1, 0x10); S->arrow_type = l1[0x18]; }
    else if (l2[0]) { S->arrow_h = D(l2, 8); S->arrow_w = D(l2, 0x10); S->arrow_type = l2[0x18]; }
    S->arrows_out = I16(Es, 0x7b0); S->ldr_style = I16(Es, 0x7b4); S->wit_vis = I16(Es, 0x7ac); S->ldr_vis = I16(Es, 0x7ae);
    S->auto_align = Es[0x7c9];
    S->gap = D(Es, 0x350); S->ext = D(Es, 0x358); S->scale = D(Es, 0x330);
    S->coord_fmt = I16(Es, 0x7c0); S->frac_units = I16(Es, 0x7c2); S->digits = Es[0x7cd]; S->lead0 = Es[0x7cc]; S->trail0 = Es[0x7cf]; S->comma = Es[0x7cb];
    S->diam_fmt = S->radius_fmt = I16(Es, 0x7b2);
    S->tol_mode = I16(Es, 0x7b6) == 3 ? 1 : I16(Es, 0x7b6) > 3 ? 0 : I16(Es, 0x7b6); S->constr3d = Es[0x7d0];
    S->color = Es[6]; memcpy(&S->level, Es + 8, 4); memcpy(S->attrib, Es + 0x10, 16);
    S->tol_h = D(Es, 0x6a8 + 0x28); S->pos_tol = D(Es, 0x340); S->neg_tol = D(Es, 0x348);
    S->din_tol = Es[0x7c6]; S->din_chr = Es[0x7c7]; S->din_val = Es[0x7c8]; S->metric = I16(Es, 0x7ba) == 1;
    if (Es[0x308] == 3) { O->neg_sign = Es[0x124]; O->arrowhd = Es[0x125]; O->h_aln = Es[0x122]; }   /* l.713-716 */
    else O->h_aln = 1;                                             /* l.634 */
}

/* ------------------------------------------------------------------------------------------------ the oc: aligning an ordinate set */
/* FUN_18023d590 case 3 (as in do_calc_ordinate): the placement from the slot, the offsets from the aPoint */
static void place_offsets(uint8_t *E, const DcFonts *F, const double vm[9]) {
    double pl[3];
    if (!placement(E, F, pl)) return;
    PD(E, 0x310, pl[0]); PD(E, 0x318, pl[1]); PD(E, 0x320, pl[2]);
    double av[3]; w2v(vm, (const double *)(const void *)(E + 0x70), av);
    placement(E, F, pl);
    double ox = fabs(av[0] - pl[0]), oy = fabs(av[1] - pl[1]);
    PD(E, 0x360, pl[0] < av[0] ? -ox : ox); PD(E, 0x368, pl[1] < av[1] ? -oy : oy); PD(E, 0x370, D(E, 0x320));
}
/* the record's text extents (DfCheckForTokens + get_ext; tolerance strings NOT PORTED) and box half sizes (box_limits) */
static int text_box(const uint8_t *E, const DcFonts *F, double *h0, double *w0, double *bx, double *by) {
    if (I16(E, 0x7b8) > 0) return 0;
    double hs[3] = { 0, 0, 0 }, ws[3] = { 0, 0, 0 };
    if (dc_text_ext(F, (const char *)E + 0x650, E + 0x5d8, &hs[0], &ws[0])) return 0;
    int m = I16(E, 0x7b6); if (m == 3) m = 1;
    dc_box_limits(m, I16(E, 0x7b8) + 1, D(E, 0x618) * D(E, 0x600), hs, ws, bx, by);
    *h0 = hs[0]; *w0 = ws[0];
    return 1;
}

int do_move_ordinate(uint8_t E[0x810], int code, double p4[3], double p5[3], double p6[3], const DcFonts *F) {
    double vm[9]; int16_t vv = I16(E, 0x328);
    if (!mcam_std_view(vv ? vv : 1, vm)) return 0;
    double depth = D(E, 0x320), half = D(E, 0x600) * 0.5;
    P2 A = p2plane(vm, (const double *)(const void *)(E + 0x70), depth);
    double r = D(E, 0x600) * D(E, 0x608) + D(E, 0x600) * D(E, 0x618);
    double a = D(E, 0x5e8); int oc = 0;
    if (fabs(a - HALFPI) > 1e-06 || fabs(a - HALFPI) == 1e-06) { oc = 1; if (1e-06 <= fabs(a)) oc = 2; }
    double h0, w0, bx, by;
    if (!text_box(E, F, &h0, &w0, &bx, &by)) return 0;
    double c = cos(a), s = sin(a), cb = c * bx, sb = s * bx;
    p4[2] = p5[2] = p6[2] = depth;
    if (code == 0) { p5[0] = cb + p4[0]; p5[1] = sb + p4[1]; p5[2] = p4[2]; p6[0] = cb + p5[0]; p6[1] = sb + p5[1]; p6[2] = p5[2]; }
    else if (code == 1) { p4[0] = p5[0] - cb; p4[1] = p5[1] - sb; p4[2] = p5[2]; p6[0] = cb + p5[0]; p6[1] = sb + p5[1]; p6[2] = p5[2]; }
    else if (code == 2) { p5[0] = p6[0] - cb; p5[1] = p6[1] - sb; p5[2] = p6[2]; p4[0] = p5[0] - cb; p4[1] = p5[1] - sb; p4[2] = p5[2]; }
    else return 0;
    double tx = D(E, 0x638), ty = D(E, 0x640);
    double dx = p5[0] - ((tx - (h0 * s + w0 * c) * 0.5) + s * by + cb);
    double dy = p5[1] - ((ty - (w0 * s + h0 * c) * 0.5) + c * by + sb);
    double dz = p5[2] - depth;
    PD(E, 0x310, dx + D(E, 0x310)); PD(E, 0x318, dy + D(E, 0x318)); PD(E, 0x320, dz + D(E, 0x320));
    PD(E, 0x638, dx + D(E, 0x638)); PD(E, 0x640, dy + D(E, 0x640)); PD(E, 0x648, dz + D(E, 0x648));
    L2 Ln = { A, { c * 5.0 + A.x, s * 5.0 + A.y } };               /* local_308: through the point along the text */
    P16(E, 0x4c2, 4);
    double q[2], z3, z2;
    if (I16(E, 0x7b4) == 0) {
        double x, y;
        if (code == 2) { x = p6[0] + c * half; y = p6[1] + s * half; PD(E, 0x510, x); PD(E, 0x518, y); x += c * r; y += s * r; q[0] = x + c * r * 2.0; q[1] = y + s * r * 2.0; z2 = p6[2]; }
        else { x = p4[0] - c * half; y = p4[1] - s * half; PD(E, 0x510, x); PD(E, 0x518, y); x -= c * r; y -= s * r; q[0] = x - c * r * 2.0; q[1] = y - s * r * 2.0; z2 = p4[2]; }
        PD(E, 0x4f8, x); PD(E, 0x500, y); PD(E, 0x508, z2); z3 = z2;
    } else {
        double k = by + D(E, 0x618) * D(E, 0x600), q4x, q4y, q6x, q6y;
        if (oc == 0) { q4x = k * s + p4[0]; q4y = k * c + p4[1]; q6x = k * s + p6[0]; q6y = k * c + p6[1]; }
        else { q4x = p4[0] - k * s; q4y = p4[1] - k * c; q6x = p6[0] - k * s; q6y = p6[1] - k * c; }
        if (code == 2) {
            PD(E, 0x510, q4x); PD(E, 0x518, q4y);
            double x = c * half + q6x + c * r, y = q6y + s * half + s * r;
            PD(E, 0x4f8, x); PD(E, 0x500, y); PD(E, 0x508, p6[2]);
            q[0] = x + c * r * 2.0; q[1] = y + s * r * 2.0; z3 = p4[2];
        } else {
            PD(E, 0x510, q6x); PD(E, 0x518, q6y); z3 = p6[2];
            double x = (q4x - c * half) - c * r, y = (q4y - s * half) - s * r;
            PD(E, 0x4f8, x); PD(E, 0x500, y); PD(E, 0x508, p4[2]);
            q[0] = x - c * r * 2.0; q[1] = y - s * r * 2.0;
        }
    }
    PD(E, 0x520, z3);
    P2 p1;
    if (!proj_pt_ln((P2){ q[0], q[1] }, Ln, &p1)) return 0;
    PD(E, 0x4e0, p1.x); PD(E, 0x4e8, p1.y);
    PD(E, 0x4c8, A.x); PD(E, 0x4d0, A.y); PD(E, 0x4d8, depth);
    if (E[0x125]) {
        double ex = D(E, 0x4e0) - D(E, 0x4c8), ey = D(E, 0x4e8) - D(E, 0x4d0), an = 0.0;
        if (1e-12 <= ey * ey + ex * ex) { an = atan2(ey, ex); if (an < 0.0) an += TWOPI; }
        PD(E, 0x4c8, cos(an) * D(E, 0x350) + D(E, 0x4c8)); PD(E, 0x4d0, sin(an) * D(E, 0x350) + D(E, 0x4d0));
    }
    if (fabs(dist_pt_l((P2){ D(E, 0x510), D(E, 0x518) }, Ln)) < TOL) {
        PD(E, 0x4e0, D(E, 0x510)); PD(E, 0x4e8, D(E, 0x518)); PD(E, 0x4f0, D(E, 0x520)); P16(E, 0x4c2, 2); }
    PD(E, 0x310, p5[0]); PD(E, 0x318, p5[1]); PD(E, 0x320, p5[2]);
    E[0x122] = (uint8_t)code;
    place_offsets(E, F, vm);
    return 1;
}

int do_oc_order(DoOc *oc, int n, const uint8_t *drag, int orient, const DcFonts *F, DoOcGlob *g) {
    double x0 = D(drag, 0x4c8), y0 = D(drag, 0x4d0), x1 = D(drag, 0x4e0), y1 = D(drag, 0x4e8);
    g->lx0 = x0; g->ly0 = y0; g->lx1 = x1; g->ly1 = y1;
    double dx = x1 - x0, dy = y1 - y0;
    if (sqrt(dx * dx + dy * dy) < TOL) return 0;
    double sx = 0, sy = 0;                                         /* uVar22 / uVar8, dVar2: the side test of a parallel set */
    if (orient == 0) g->ang = HALFPI;
    else if (orient == 1) g->ang = 0.0;
    else {
        if (x1 < x0) { dx = x0 - x1; dy = y0 - y1; }
        sx = -dx; sy = dy; g->ddx = dx; g->ddy = dy;
        if (1e-12 <= dy * dy + dx * dx) { g->ang = atan2(dy, dx); if (g->ang < 0.0) g->ang += TWOPI; } else g->ang = 0.0;
    }
    double h0, w0, bx, by;
    if (!text_box(drag, F, &h0, &w0, &bx, &by)) return 0;
    g->bx = bx; g->by = by; g->pad = fabs(D(drag, 0x600) * D(drag, 0x618));
    for (int i = 0; i < n; i++) {
        const uint8_t *E = oc[i].E; double d;
        if (orient == 0) d = D(E, 0x4c8) - x0;
        else if (orient == 1) d = D(E, 0x4d0) - y0;
        else {
            L2 L = { { x0, y0 }, { x1, y1 } }; P2 q = { D(E, 0x4c8), D(E, 0x4d0) };
            d = dist_pt_l(q, L);
            if (0.0 < (q.y - y0) * sx + (q.x - x0) * sy) d = d * -1.0;
        }
        oc[i].d = d;
    }
    for (int i = 0; i < n; i++) {
        double eh, ew, ebx, eby;
        if (!text_box(oc[i].E, F, &eh, &ew, &ebx, &eby)) return 0;
        oc[i].cbx = cos(g->ang) * ebx; oc[i].sbx = sin(g->ang) * ebx; oc[i].bx = fabs(ebx); oc[i].by = fabs(eby);
    }
    for (int k = 10000; ; ) {                                      /* above: 10001.. by increasing distance */
        int best = -1; double bd = 1e38;
        for (int i = 0; i < n; i++) if (oc[i].idx == 0 && TOL < oc[i].d && oc[i].d <= bd) { best = i; bd = oc[i].d; }
        if (best < 0) break;
        oc[best].idx = (int16_t)++k;
    }
    for (int k = 10000; ; ) {                                      /* below: 9999.. by decreasing value */
        int best = -1; double bd = -1e38;
        for (int i = 0; i < n; i++) if (oc[i].idx == 0 && oc[i].d < TOL && bd <= oc[i].d) { best = i; bd = oc[i].d; }
        if (best < 0) break;
        oc[best].idx = (int16_t)--k;
    }
    return 1;
}

void do_oc_adjust(DoOc *oc, int n, const uint8_t *drag, int orient, const DoOcGlob *g, const DcFonts *F) {
    double P[3][4];
    for (int k = 0; k < 3; k++) { P[k][0] = D(drag, 0x310); P[k][1] = D(drag, 0x318); }
    if (orient == 0) { P[2][1] = P[2][1] + g->bx; P[0][1] = P[0][1] - g->bx; }
    else if (orient == 1) { P[0][0] = P[0][0] - g->bx; P[2][0] = P[2][0] + g->bx; }
    else if (orient == 2) { double c = cos(g->ang) * g->bx; P[0][0] = P[0][0] - c; double s = sin(g->ang); P[2][0] = P[2][0] + c; P[0][1] = P[0][1] - s * g->bx; P[2][1] = P[2][1] + s * g->bx; }
    else return;
    for (int k = 0; k < 3; k++) {
        P[k][2] = P[k][0]; P[k][3] = P[k][1];
        if (orient == 0) P[k][2] = P[k][2] + 10.0;
        else if (orient == 1) P[k][3] = P[k][3] - 10.0;
        else { P[k][2] = g->ddy + P[k][2]; P[k][3] = P[k][3] - g->ddx; }   /* (+7a30, -7a28): asm 0x1802411db-0x18024120f */
    }
    double ldy = P[1][3] - P[1][1], ldx = P[1][2] - P[1][0], len = sqrt(ldx * ldx + ldy * ldy), ux, uy;
    if (len == 0.0) { ux = 0.0; uy = 0.0; } else { ux = ldx / len; uy = ldy / len; }
    int up = 10001, dn = 9999; double posu = g->by, posd = g->by;
    for (;;) {
        int i, pos_side = 0;
        for (i = 0; i < n; i++) { if (oc[i].idx == up) { pos_side = 1; break; } if (oc[i].idx == dn) break; }
        if (i >= n) return;
        uint8_t *E = oc[i].E;
        double gap = g->pad + oc[i].by;
        if (I16(E, 0x7b4) != 0) gap = gap + g->pad;
        double qx = D(E, 0x4c8), qy = D(E, 0x4d0);
        double t = (qy - P[1][1]) * uy + (qx - P[1][0]) * ux, v, pos;
        if (pos_side) {
            if (orient != 0) t = -t;
            v = gap + posu <= t ? t : gap + posu;
            pos = orient != 0 ? -v : v;
            posu = v + oc[i].by; up++;
        } else {
            if (orient == 0) t = -t;
            v = gap + posd <= t ? t : gap + posd;
            pos = orient == 0 ? -v : v;
            posd = v + oc[i].by; dn--;
        }
        double sy = uy * pos, sx = ux * pos;
        double p4[3] = { 0, 0, 0 }, p5[3] = { 0, 0, 0 }, p6[3] = { 0, 0, 0 }; int code;
        if ((qy - P[1][1]) * ldx - (qx - P[1][0]) * ldy <= 0.0) { code = 0; p4[0] = P[0][0] + sx; p4[1] = P[0][1] + sy; }
        else { code = 2; p6[0] = P[2][0] + sx; p6[1] = P[2][1] + sy; }
        if (!do_move_ordinate(E, code, p4, p5, p6, F)) return;
    }
}
