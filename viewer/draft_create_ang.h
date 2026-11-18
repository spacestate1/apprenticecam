/* draft_create_ang - Mastercam 2017's creation of an ANGULAR dimension record (mccore calc_ang_dimension @180021a40 and its callees),
 * ported from the decompile like viewer/draft_create.c (the linear port, whose helpers it reuses through draft_create_int.h). Fills
 * the 0x810-byte D_ID image (type ent+0x308 = 1) as Mastercam fills its drag entity on each mouse move of the Angular command, and as
 * its regeneration (FUN_180303b70) rebuilds it. Every rule cites its source in draft_create_ang.c. */
#ifndef DRAFT_CREATE_ANG_H
#define DRAFT_CREATE_ANG_H
#include "draft_create.h"

/* the "vlines" state of mccore (df_ang_vlines_set @180022220 / df_ang_vlines_off @180022210): the two definition lines as set before
   their extension (DAT_181371db0 / DAT_181371dd0), qangles @181371d48, qcodes @181371d40, vlines_saved @181371d68 */
typedef struct {
    int saved;                          /* vlines_saved */
    double l[2][4];                     /* l_2d x0 y0 x1 y1 */
    double qangles[4];
    short qcodes[4];
} DcAngVlines;

/* df_angular_set_ep_angles @180241fa0: the 4 aPoints in record order (ent+0x68, +0x100, +0x198, +0x230; line 0 = ap0 -> ap2, line 1
   = ap1 -> ap3) in view VM -> the two definition lines (view, z 0; sorted / oriented, met at the vertex, extended), the vertex, the 4 ray
   angles and endpoint codes; sets VL (df_ang_vlines_set). 0 = no intersection / degenerate */
int dc_ang_set_ep(const uint8_t ap[4][0x98], const double vm[9], double lines[2][6], double *vertex, double *angles, short *codes,
                  DcAngVlines *vl);
/* determine_ang_quad @18023fe30: the quadrant 1..4 of the cursor (view) around the vertex by the 4 angles; *code = its endpoint code
   (FUN_18023f360; 0xc when none) */
int dc_ang_quad(const double *vertex, const double *cursor_view, const double *angles, const short *codes, int *code);
/* FUN_18023f360 (quadrant -> code 12/14/23/34, 0xc invalid) and ang_code2quad @18023f2f0 (code -> quadrant, 1 when none) */
int dc_ang_quad_code(int quad, const short *codes);
int dc_ang_code2quad(int code, const short *codes);

typedef struct {
    uint8_t ap[4][0x98];                /* calc_ang_dimension param_6, record order (see dc_ang_set_ep) */
    double lines[2][6];                 /* param_2: the lines dc_ang_set_ep returned (view coordinates) */
    DcAngVlines vl;                     /* the vlines state at the time of the call (FUN_180020470 / FUN_1800206f0 read it) */
    int quad;                           /* param_3: 1..4 */
    double cursor[3];                   /* param_4, WORLD */
    int view; double vm[9];             /* param_5 (0: get_gr_view_n, NOT PORTED) and its matrix rows */
    int flip;                           /* param_7 (record +0x2e9; DAT_181371d70): the supplementary side */
    int pigtail;                        /* param_8 df_ang_add_pigtail (config 14011) */
    int ang_fmt;                        /* DAT_1813d5624 (config 11000; record +0x7c0): 0 / 4 degrees, 1 DMS (NOT PORTED), 2 rad, 3 grad */
    int digits;                         /* num_digits_ang (config 11019) */
    int tol_mode;                       /* DAT_1813d620e angular tolerance mode (config 11007): only 0 ported */
    double pos_tol, neg_tol;            /* pos_ / neg_angular_tolerance (11020 / 11016) */
    int din_tol, din_chr, din_val;      /* angular_din_toler / _chr / _val */
    int metric;                         /* IsMetric() -> +0x7ba */
    double text_h;                      /* dim_text_height global (gap-angle and centring snap use the global, not the font) */
    double extra_ln_sp;                 /* extra_ln_sp global (text box pad = extra_ln_sp x dim_text_height) */
    const char *tmpl;                   /* the record's text template when DAT_1813d8d40 (FUN_180310b30) is set; NULL at creation */
    int ctrl;                           /* GetAsyncKeyState(VK_CONTROL) held */
} DcAngInput;

/* calc_ang_dimension: fills E (the header, type 1, the 4 aPoints, the two arcs at +0x4a8 / +0x540 (kind 2), the witnesses +0x378 /
   +0x410, the text, placement / offsets, +0x2c8.. the vertex / depth / radius / quadrant code / flip / ratio / pigtail). Uses from S:
   fa, arrow_h / arrow_w / arrow_type, arrows_out, wit_vis, ldr_vis, ldr_style, text_orient, auto_align, sel_dist, gap, ext, scale,
   lead0 / trail0 / comma, frac_units, constr3d, color / level / attrib. R.valid = *param_9 */
DcResult dc_calc_ang(uint8_t E[0x810], const DcAngInput *in, const DcSettings *S, const DcFonts *F);

/* FUN_180303b70's text point: FUN_180020320 (the point on the arc of radius +0x2e0 at ratio +0x2f0 of quadrant ang_code2quad(+0x2ea)),
   else the vertex + offsets +0x360/+0x368; depth +0x370 (VIEW coordinates out) */
void dc_ang_regen_cursor(const uint8_t *E, const double *vertex, const double *angles, const short *codes, double *out);
#endif
