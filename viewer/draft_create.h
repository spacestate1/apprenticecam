/* draft_create - Mastercam 2017's creation of a LINEAR dimension record (mccore calc_linear_dimension @18024c480 and its callees),
 * ported from the decompile for the Drafting tab (Horizontal / Vertical / Parallel / Smart-linear). The function fills the 0x810-byte
 * D_ID record image (ent 0x68 header + 0x7a8 body) the way Mastercam fills its drag entity on each mouse move; the record is what
 * draw_dimension later replays (viewer/draft.c). Specs: ../formats/drafting/{create,draw,records,text}.md, port notes and the corpus
 * oracle in ../formats/drafting/create_port.md. Text extents need the stroke fonts (fontN.fnt) of the user's install. */
#ifndef DRAFT_CREATE_H
#define DRAFT_CREATE_H
#include <stdint.h>

typedef struct DcFonts DcFonts;         /* stroke-font advance widths (text.md 2.1), fonts 0..7 */
DcFonts *dc_fonts_load(const char *dir); /* fontN.fnt / FONTN.FNT, N = 0.. (FUN_180365a90 stops at the first missing); NULL: none */
void dc_fonts_free(DcFonts *f);

/* the drafting session globals calc_linear_dimension reads (create.md 5; names = mccore exports / dd_global fields) */
typedef struct {
    uint8_t fa[0x60];                   /* the dimension font_attr as init_attribs_dim builds it (DAT_1813d5630): height +0x28, aspect +0x30,
                                           char / line spacing +0x38 / +0x40, font +0x5c, path bits ... (+0x10 angle is computed) */
    double arrow_h, arrow_w;            /* arrow_head_height / arrow_head_width */
    int arrow_type;                     /* DAT_1813d61fa = dd_global+2, dimension arrowhead style (3 = none) */
    int arrows_out;                     /* _dd_global (dd_global+0): 0 inside, 1 outside */
    int wit_vis;                        /* DAT_1813d61fe (dd_global+6) visible witness lines 0..3 */
    int ldr_vis;                        /* DAT_1813d6200 (dd_global+8) visible leaders 0..3 */
    int ldr_style;                      /* DAT_1813d6202 (dd_global+0xa) leader style 0/1 */
    int keep_text;                      /* DAT_1813d6212 (dd_global+0x1a; name NOT DECODED): FUN_180252850 skips the re-format */
    int text_orient;                    /* DAT_1812d980c config 12004 "Text orientation" (0 aligned, else horizontal) */
    int auto_align;                     /* auto_alignment (config 11004, centre the text) */
    double sel_dist;                    /* sel_dist (centring snap radius x2; 0 = no snap) */
    double gap, ext;                    /* witness_gap / witness_extension */
    double scale;                       /* draft_scale */
    int coord_fmt, frac_units, digits, lead0, trail0, comma;   /* coordinate_format, fraction_units, num_digits, leading_zeroes, trailing_zeroes, comma_period */
    int diam_fmt, radius_fmt;           /* DAT_1813d5628 diameter symbol style, radius_format */
    int tol_mode;                       /* DAT_1813d6210 linear tolerance mode: only 0 is ported */
    int constr3d;                       /* constr_view_3d() */
    int color, level; uint8_t attrib[16]; /* main_color / main_level / main_attrib (header) */
    uint8_t ver[2];                     /* DAT_18143d54a/b version stamp (InitDIM) */
    /* read only by dc_init_dim (InitDIM / FUN_180249820 / InitDT / InitDM; the regeneration oracle starts from the stored record) */
    double tol_h;                       /* tol_text_height (config 12016): the tolerance font height (InitDT) */
    double pos_tol, neg_tol;            /* pos_linear_tolerance / neg_linear_tolerance (config 11021 / 11017) */
    int din_tol, din_chr, din_val;      /* linear_din_toler / linear_din_chr / linear_din_val (config 11014 / 11013 / 11015) */
    int metric;                         /* IsMetric() (InitDM: dm +0x1a) */
} DcSettings;

/* the globals init_attribs_dim @1802524b0 builds the dimension font_attr from (decomp/draft/core_create.c 607-645): config 12005
   dim_text_height, 12000 chr_exp, 12001 chr_rot, 12015 dim_text_slant_angle, 12012 dim_text_angle, 12006 extra_sp, 13000 extra_ln_sp,
   12007 font, 12013 text_bx, 12014 text_path, 12022 space_sty; alt_height / alt_chr_exp / alt_font / alt_color / fill / fill_color
   have no config item (core_cfgdraft.c writes none): their .data initializers */
typedef struct {
    double height, aspect, rot, slant, angle, extra_sp, extra_ln_sp, alt_h, alt_aspect;
    int font, text_bx, text_path, space_sty, alt_font, alt_color, fill, fill_color;
} DcDimFont;
void dc_attribs_dim(uint8_t fa[0x60], const DcDimFont *g);   /* init_attribs_dim: FA's other bits are kept (zero it first) */
/* InitDIM @180249730 + FUN_180249820 (core_draftwf.c 3-49, 1542-1596): the empty dimension record a creation starts from (memset,
   class 0x40, version stamp, colour / level / attributes, view (0 -> 1), scale, tolerances, gap / extension, two 'K' aPoints at the
   origin, anchor 1, InitDimWL x 4, InitDT, InitDM, DIN, comma / zeros / digits, construction 3-D, auto alignment 1). S->fa = the
   dimension font (dc_attribs_dim) */
void dc_init_dim(uint8_t E[0x810], const DcSettings *S, int view);
/* aPointInitWorld @1800243e0 (core_draft_rec.c 96-163) without the 'O' (plane origin) branch: kind, world point (aPointTestTol), the
   two entity slots 0, +0x60 (0,0,0), +0x78 (1,1,0); aPointSaveEntSel @180024560 (core_draft_rec.c 168-183): slot I = eptr, t, 0 */
void dc_apoint_world(uint8_t ap[0x98], const double p[3], int kind);
void dc_apoint_ent(uint8_t ap[0x98], int slot, uint64_t eptr, double t);
/* determine_h_or_v @180240300 (core_create.c 3): the P1 -> P2 line (view coordinates) rotated by -HVP is horizontal (0) / vertical (1)
   within AbsAngleTheSame, else -1 */
int dc_line_hv(const double p1v[3], const double p2v[3], double hvp);

typedef struct {
    uint8_t ap1[0x98], ap2[0x98];       /* the two definition aPoints (world position at +8), in pick order */
    double cursor[3];                   /* the sketched text point, WORLD (df_calc_sketched_point) */
    int view;                           /* view number (FUN_1802579d0: 0 -> graphics view: NOT PORTED, give one) */
    double vm[9];                       /* that view's matrix rows (world_to_view: v[i] = sum_j vm[3i+j] w[j]) */
    int orient;                         /* 0 H, 1 V, 2 parallel (in: requested, out: as calc_linear left it) */
    double hvp;                         /* DfHvpAngle (degrees) */
    int value_mode;                     /* 0 linear, 1 radius, 2 diameter symbol */
    const char *tmpl;                   /* the ent's text template when DAT_1813d8d40 (FUN_180310b30, ent+0x7a0) is set; NULL at creation */
    int ctrl;                           /* GetAsyncKeyState(VK_CONTROL) held */
} DcInput;

typedef struct {
    int valid;                          /* calc_linear_dimension *param_9 */
    int swapped;                        /* *param_8 */
    int orient;                         /* *param_6 after the collapse (record +0x68 is FUN_180251290's re-derivation) */
    int not_ported;                     /* 1: a branch that is NOT PORTED was needed (tolerance mode, TrueType, format), E unchanged */
    const char *why;
    double text_w, text_h;              /* extents of the final value text (FUN_18030f410) */
    int style;                          /* circular (draft_create_circ.c): the style calc_circular_dimension used (0 quadrant, 2 radial) */
} DcResult;

/* calc_linear_angle @18024c280: the reference angle (degrees) of a linear dimension (0 / 90 / the P1P2 view angle, + DfHvpAngle) */
double dc_linear_angle(const uint8_t ap1[0x98], const uint8_t ap2[0x98], const double vm[9], int orient, double hvp);
/* calc_linear_dimension: fills E (0x810 bytes: ent header 0x68 + body 0x7a8; bytes Mastercam leaves as stack garbage are 0) */
DcResult dc_calc_linear(uint8_t E[0x810], const DcInput *in, const DcSettings *S, const DcFonts *F);
/* the helpers of this port the ORDINATE port (viewer/draft_ord.c) shares: get_ext; FUN_18030f410 (value text + extents: kind, symbol
   style, value, the template; returns 1, 0 = get_ext failed, -1 = NOT PORTED with *why); box_limits @18024b680; make_wl_single
   @180254950 on a v8_wit_ldr image (its points at +0x20 are normalised in place); FUN_180252790's slot font */
int dc_text_ext(const DcFonts *F, const char *s, const uint8_t fa[0x60], double *h, double *w);
int dc_value_text(const DcSettings *S, const DcFonts *F, int kind, int sym, double value, const char *tmpl, char text[0x387],
                  double ws[3], double hs[3], double *W, double *H, int *nstr, const char **why);
void dc_box_limits(int mode, int nstr, double pad, const double *hs, const double *ws, double *bx, double *by);
void dc_make_wl(uint8_t wl[0x98], int kind, int has, int type, double h, double wd);
void dc_slot_font(uint8_t dst[0x60], const uint8_t fa[0x60], double ang);
#endif
