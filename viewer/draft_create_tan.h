/* draft_create_tan - Mastercam 2017's TANGENT (type 6) and PERPENDICULAR (type 5) dimension creation: mccore calc_tangent_dimension
 * @18024f830 / calc_perp_dimension @18024eb60 (both over calc_linear_dimension = viewer/draft_create.c dc_calc_linear) and their
 * helpers calc_tangent_eps @18023f9c0, calc_tangent_rbox @18023fc80 (FUN_18023f7a0 / FUN_18023f400 / FUN_18023ee40), FUN_18023d590
 * cases 5 / 6 with FUN_18023de60 (offsets / anchor), DfGetTangentArc @18023e6a0, the regenerations FUN_180305ca0 (tangent) /
 * FUN_1803056d0 (perpendicular), and the mccad pieces of the two commands that are pure geometry (FUN_18012ac50, FUN_18004e3b0,
 * FUN_18004e290, FUN_180047aa0, FUN_180048460). Decompiles: decomp/draft/tan_core.c, tan_core2.c, tan_cad.c, perp_cad.c (dumped for
 * this port), core_calcdim.c, core_createport.c, cad_linear.c, circ_cad.c. Spec: ../formats/drafting/create.md (Tangent /
 * Perpendicular), records.md 2.1. */
#ifndef DRAFT_CREATE_TAN_H
#define DRAFT_CREATE_TAN_H
#include "draft_create.h"

/* an arc entity as these functions read it (the in-memory A_ID: +0x68 / +0x80 start / end WORLD points, +0x98 centre in the arc's
   VIEW coordinates (z = the arc's depth, +0xa8), +0xb0 r, +0xb8 / +0xc0 start / sweep (radians, view), +0xc8 view, +0x20 eptr) */
typedef struct {
    double ep0[3], ep1[3];              /* +0x68 / +0x80 */
    double c[3];                        /* +0x98 (view) */
    double r, start, sweep;             /* +0xb0 / +0xb8 / +0xc0 */
    int view;                           /* +0xc8 */
    double vm[9];                       /* that view's rows (world_to_view) */
    uint64_t eptr;                      /* +0x20 (0: none) */
} DtArc;

/* how the caller resolves an aPoint's eptr (get_ent_from_eptr / CheckEptrAlive): 0 no entity, 1 an entity that is not an arc,
   4 an arc (filled into *arc) */
typedef int (*DtEntOf)(void *u, uint64_t eptr, DtArc *arc);

/* ---- tangent */
typedef struct {
    int mode;                           /* param_6 (record +0x68): 1 arc + arc, 2 arc + point, 3 / 4 arc + line (4 = the Tangent button) */
    DtArc arc[2];                       /* param_9: the arc(s) (arc[1] for mode 1) */
    uint8_t slot[3][0x98];              /* param_10: the picks (DAT_1806a0b30 ..: 'C' of arc 1, then the point / arc 2's 'C' / the line ends) */
    uint8_t ap1[0x98], ap2[0x98];       /* param_2 / param_3: the two linear definition points (calc_tangent_eps) */
    double cursor[3];                   /* param_4, WORLD */
    int view; double vm[9];             /* param_5 (FUN_1802579d0: 0 -> graphics view NOT PORTED) and its rows */
    int orient;                         /* param_7: 0 H, 1 V (2 refused: the function returns invalid) */
    int sides[2];                       /* param_8: 0 / 180 (DAT_18069b8b4 / b8b6) */
    double hvp;                         /* DfHvpAngle (degrees) */
    int maintain;                       /* DfMaintainTang (record +0x248) */
    int assoc;                          /* DfAssoc (config 15003) */
    const char *tmpl;                   /* the record's text template when +0x7a0 is set (regeneration); NULL at creation */
    int ctrl;                           /* GetAsyncKeyState(VK_CONTROL) held */
    DtEntOf ent_of; void *u;            /* for DfGetTangentArc inside FUN_18023d590 case 6 (NULL: every arc rebuilt from the record) */
} DtTanInput;

/* calc_tangent_rbox: the extreme points (max x, max y, min x, min y in the frame rotated by -HVP about arc 1's centre) of the first
   entity (out1) and the second (out2), their boxes, and the box of both (mn3 / mx3) */
void dc_tan_rbox(int mode, const DtArc arc[2], const uint8_t slot[3][0x98], double hvp,
                 double mn1[2], double mx1[2], uint8_t out1[4][0x98], double mn2[2], double mx2[2], uint8_t out2[4][0x98],
                 double mn3[2], double mx3[2]);
/* calc_tangent_eps: the two linear definition points res[0] / res[1] for ORIENT and SIDES (rbox = 1: out1 / out2 recomputed first);
   returns 0 when the points are aligned across the orientation (no dimension) */
int dc_tan_eps(int mode, const DtArc arc[2], const uint8_t slot[3][0x98], int orient, const int sides[2], int rbox,
               uint8_t out1[4][0x98], uint8_t out2[4][0x98], uint8_t res[2][0x98], double *hvp, int assoc);
/* calc_tangent_dimension (+ FUN_18023d590 case 6): fills E. R.valid = *param_12, R.swapped = *param_11 */
DcResult dc_calc_tangent(uint8_t E[0x810], const DtTanInput *in, const DcSettings *S, const DcFonts *F);
/* FUN_18004e290 (the create set-up) / FUN_180305ca0's DfMaintainTang branch: the "maintain" angle and orientation (df_calc_orient_ang) */
void dc_tan_maintain(int mode, const DtArc *a0, const DtArc *a1, const uint8_t slot[3][0x98], int regen, double depth, double *hvp, int *orient);
/* FUN_18012ac50 (mccad): the orientation from the cursor R (rotated frame) against the box mn3 / mx3 of both entities: beside it ->
   1, above / below -> 0, inside or at a corner -> OLD */
int dc_tan_orient_by_box(const double mn[2], const double mx[2], const double r[2], int old);
/* FUN_18004e3b0 (mccad): the side of entity 1 the cursor is nearer (0 = the max side, 180 = the min side) and its distance */
int dc_tan_side(const double mn[2], const double mx[2], const double r[2], int orient, double *dist);
/* df_rotate_pt_about_pt @180244960: P (view) about C by DEG degrees (|DEG| < tol: unchanged), z kept */
void dc_tan_rot_about(const double p[3], const double c[3], double deg, double o[3]);
/* DfGetTangentArc @18023e6a0 from a record E (0x810): arc IDX (0 / 1) - the associative arc when the record is associative and the
   eptr resolves to an arc of a parallel view, else rebuilt from the record (centre aPoint, depth +0x320, r / start / sweep) */
void dc_tan_get_arc(const uint8_t E[0x810], int idx, const double vm[9], DtEntOf ent_of, void *u, DtArc *out);
/* FUN_180305ca0: the regeneration of a stored tangent dimension ES into EO (a copy of ES first): arcs (DfGetTangentArc), DfHvpAngle /
   orientation (+0x240 / +0x238, or the DfMaintainTang angle), calc_tangent_eps (rbox), the cursor = the anchor point (+0x249) + the
   offsets +0x360 / +0x368 rotated by calc_linear_angle, depth +0x370 (USE_PLACEMENT: the placement +0x310 instead, DAT_1813d8cd2);
   the aPoints are taken as stored (FUN_180303a80's refresh from the entities is not done). S / F as the caller sets them */
/* FUN_180305ca0's cursor (the regeneration's text point, WORLD in arc 1's view) for the record ES with the arcs / aPoints given; 0 =
   calc_tangent_eps failed */
int dc_tan_regen_cursor(const uint8_t Es[0x810], const DtArc arc[2], const uint8_t slot[3][0x98], int assoc, double cw[3]);
DcResult dc_regen_tangent(uint8_t Eo[0x810], const uint8_t Es[0x810], const double vm[9], DtEntOf ent_of, void *u,
                          const DcSettings *S, const DcFonts *F, int ctrl, int use_placement, int assoc);

/* ---- perpendicular */
typedef struct {
    int mode;                           /* param_6 (record +0x68): 1 line + point, 2 two parallel lines */
    uint8_t aps[4][0x98];               /* param_7 (DAT_180695d00 ..: FUN_180048460's order) */
    uint8_t ap1[0x98], ap2[0x98];       /* param_2 / param_3: the two linear definition points (FUN_180047aa0) */
    int idx[2];                         /* param_8: their indices in aps (DAT_1806496ac / ae) */
    double cursor[3];                   /* param_4, WORLD */
    int view; double vm[9];
    int orient;                         /* *param_9 (in / out: calc_linear_dimension's collapse) */
    double hvp;                         /* DfHvpAngle (FUN_1800480c0: find_orient_angle of the first line) */
    const char *tmpl; int ctrl;
} DtPerpInput;
/* calc_perp_dimension: R.orient = *param_9 after calc_linear_dimension */
DcResult dc_calc_perp(uint8_t E[0x810], const DtPerpInput *in, const DcSettings *S, const DcFonts *F);
/* FUN_180048460 + FUN_1800480c0 (mccad): the four aPoints from the picks (mode 1: line s0 s1, point s2 s2; mode 2: s0 s1 s2 s3, the
   second line's ends swapped so that aps[2] is nearer aps[0]) and DfHvpAngle = find_orient_angle(first line) */
void dc_perp_setup(int mode, const uint8_t slot[4][0x98], const double vm[9], uint8_t aps[4][0x98], double *hvp);
/* FUN_180047aa0 (mccad): the two aPoints nearest the cursor TV (view; 3 candidates for mode 1, 4 for mode 2) -> ap1 / ap2, idx;
   returns 1 when the pair changed */
int dc_perp_pick(int mode, const uint8_t aps[4][0x98], const double vm[9], const double tv[3], int idx[2], uint8_t ap1[0x98], uint8_t ap2[0x98]);
/* determine_hvp @180240400 (core_create.c 49-360) with param_5 = 0 (the perpendicular drag): T strictly between P1 and P2 in x -> 0,
   in y -> 1, else PREV (2 -> 1) */
int dc_determine_hvp0(const double p1v[3], const double p2v[3], const double tv[3], int prev, double hvp);
/* df_angle_of_line @180241a00: the angle (degrees, [0, 360)) of the view points A -> B */
double dc_angle_of_line(const double a[3], const double b[3]);
/* df_angle_hv_rel @180241900: 0 / 1 when angle A + B is horizontal / vertical, else -1 */
int dc_angle_hv_rel(double a, double b);
/* FUN_1803056d0: the regeneration of a stored perpendicular dimension (DfHvpAngle = the first line's orientation angle; two lines that
   intersect -> invalid, DAT_1813d8ce4 = 0x6991) */
/* FUN_1803056d0's cursor (WORLD) and DfHvpAngle for the record ES over the aPoints APS; 0 = the two lines intersect (invalid) */
int dc_perp_regen_cursor(const uint8_t Es[0x810], const uint8_t aps[4][0x98], const double vm[9], double *hvp, double cw[3]);
DcResult dc_regen_perp(uint8_t Eo[0x810], const uint8_t Es[0x810], const double vm[9], const DcSettings *S, const DcFonts *F, int ctrl, int use_placement);
#endif
