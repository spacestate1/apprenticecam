/* draft_create_pt - Mastercam 2017's POINT dimension (Drafting > Dimension > Point, dimension type 4): mccore calc_point_dimension
 * @18024ed10 and its callees (the value string FUN_18030f410 kind 7 -> FUN_180310700, the text slot FUN_180251460, the leader
 * FUN_180250bb0, make_wl_single, FUN_18023d590 case 4), ported from the decompile (decomp/draft/core_calcdim.c 2133-2377) with the
 * call arguments Ghidra truncated read in the asm (decomp/draft/pt_calc.s, pt_f410.s, pt_250bb0.s). Fills the 0x810-byte D_ID record
 * image like the drag entity on each mouse move of the Point command (mccad FUN_180048be0 default branch). The regeneration's cursor
 * (FUN_180305b40) is here too. Spec: ../formats/drafting/create.md (Point), records.md 2.1 (type 4). */
#ifndef DRAFT_CREATE_PT_H
#define DRAFT_CREATE_PT_H
#include <stddef.h>
#include "draft_create.h"

typedef struct {
    uint8_t ap[0x98];                   /* the point's aPoint (DAT_180698a30, slot 0; world position at +8) */
    double cursor[3];                   /* the sketched text point, WORLD (slot 1 +8 = DAT_180698ad0, df_calc_sketched_point) */
    int view;                           /* the active view (FUN_1802579d0: 0 -> the graphics view: NOT PORTED, give one) */
    double vm[9];                       /* its matrix rows (world_to_view) */
    int use3d, label, absolute;         /* ptdim_use3d @1812d9867 (config 12011 "3D?"), ptdim_label @1812d9866 (12010 "3D label?"),
                                           ptdim_abs @1813d5a38 (12009 "Absolute?") (writers core_cfgdraft.c:812-814) */
    int origin_on; double origin_v[3];  /* MastercamPlanes::ActiveOriginIsOn(Cplane) and GetActiveOriginInViewCoordinates(Cplane, view) */
    int coord_disp;                     /* MastercamPlanes::GetCoordinateDisplay (CoordDisp @18001a180): 0 X Y Z, 1 lathe X (radius) Z,
                                           2 lathe D (diameter) Z */
    const char *tmpl;                   /* the record's text template when +0x7a0 is set (FUN_180310b30); NULL at creation */
} DcPtInput;

/* calc_point_dimension: fills E (0x810 bytes; the bytes the function does not write are kept, as in Mastercam's drag entity).
   R.valid = *param_5 (get_ext and FUN_18023dad0 succeeded). R.not_ported: linear tolerance modes 1-3, view 0, coordinate formats 2-4,
   TrueType text (E unchanged) */
DcResult dc_calc_point(uint8_t E[0x810], const DcPtInput *in, const DcSettings *S, const DcFonts *F);
/* FUN_180310700: the coordinate string of the values V (already x draft_scale): "X 1.5, Y 2" (LABEL), ", Z .." (USE3D); lathe display
   (COORD_DISP 1 / 2): "X r, Z x" / "D 2r, Z x". Numbers by FUN_18030ee50 (FMT 0 decimal / 1 scientific). 0 = format NOT PORTED */
int dc_point_value(char *out, size_t on, const double v[3], int label, int use3d, int coord_disp, int fmt, int digits, int lead,
                   int trail, int comma);
/* FUN_180305b40 (the regeneration of a type-4 record, FUN_1803042b0 case 4): the cursor calc_point_dimension is called with =
   view_to_world(world_to_view(aPoint) + text offsets +0x360 / +0x368, depth +0x370) */
void dc_point_regen_cursor(const uint8_t *E, const double vm[9], double cursor[3]);
#endif
