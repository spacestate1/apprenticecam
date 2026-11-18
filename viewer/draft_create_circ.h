/* draft_create_circ - Mastercam 2017's CIRCULAR (radius / diameter) dimension creation: mccore calc_circular_dimension @18024b810
 * radial branch (FUN_18024f3c0 and its callees), ported from the decompile (decomp/draft/, asm where Ghidra truncated the calls).
 * Fills the 0x810-byte D_ID record image like the drag entity on each mouse move of the Drafting > Circular command (create.md 8). */
#ifndef DRAFT_CREATE_CIRC_H
#define DRAFT_CREATE_CIRC_H
#include "draft_create.h"

typedef struct {
    /* the arc entity (A_ID record body) as calc_circular_dimension reads it: centre in the arc's view coordinates (arc +0x98,
       z = depth), radius +0xb0, start / sweep (radians) +0xb8 / +0xc0, view +0xc8, eptr +0x20 */
    double centre[3], r, start, sweep;
    int arc_view;
    uint64_t eptr;
    double cursor[3];                   /* the sketched text point, WORLD */
    int view;                           /* the active view (FUN_1802579d0: 0 -> graphics view: NOT PORTED) */
    double vm[9];                       /* its matrix rows (world_to_view) */
    int diameter;                       /* param_5: 1 diameter (E+0x101), 0 radius */
    int style;                          /* *param_4: 2 radial (FUN_18024f3c0); 0 quadrant style (quad_to_eps + calc_linear_dimension) */
    int quad;                           /* param_3: the quadrant code of the quadrant style (determine_cl) */
    double hvp;                         /* DfHvpAngle (degrees; 0 for the Circular command, FUN_18004d250 param_4) */
    const char *tmpl;                   /* the record's text template when +0x7a0 is set (FUN_180310b30); NULL at creation */
    int ctrl;                           /* GetAsyncKeyState(VK_CONTROL) held */
    int assoc;                          /* DfAssoc (config 15003): the centre aPoint 'C' + the arc's eptr, else 'K' */
} DcCircInput;

/* calc_circular_dimension: fills E (0x810 bytes); R.style = the style used (a failed quadrant style falls back to 2, as *param_4).
   R.not_ported when a branch outside the port is needed (tolerance modes 1-3, arc view not equal to the active view, TrueType text,
   coordinate formats 2-4) */
DcResult dc_calc_circular(uint8_t E[0x810], const DcCircInput *in, const DcSettings *S, const DcFonts *F);
/* quad_to_eps @180245700 + FUN_180245600: the two quadrant aPoints of quadrant QUAD and the linear orientation; 0 = not on the arc */
int dc_circ_quad_eps(const DcCircInput *in, int quad, int diameter, double hvp, uint8_t ap[2][0x98], int *orient);
/* determine_cl @18023ff80: the style for the text point TV (view): 0 quadrant (*QUAD set) or 2 radial */
int dc_determine_cl(const DcCircInput *in, const double tv[3], double hvp, int diameter, int *quad);
#endif
