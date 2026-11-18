/* cam3d_blend - classic Surface Finish Blend (mcmsurf op 109 = 0x6d; ribbon msurf_blend_toolbar -> msurf_assoc(0x6d)).
 * Decode and citations: formats/blend.md ("bl N.N"). Standalone engine: the caller hands it the two chains as 3D polylines (in
 * chain order and direction), the drive surfaces as triangles and the op's record; it returns the moves.
 * Decoded (Mastercam code): the chain break (mccore break_chain_to_points / FUN_1801ea9c0), the rulings (make_loft_surf end rows,
 * FUN_180031ed0), the pass count and the per-ruling 3D (or 2D) spacing (FUN_180030fb0 / FUN_180030a90), the record fields
 * (FUN_1800caff0, msurf_map case 0x6d), the max short gap (msurf_map l.340445).
 * Ours, labelled (a SUBSTITUTE checked against Mastercam's stored toolpaths, formats/blend.md 6): the projection onto the drive
 * (FUN_18028e8b0 / the EMM path FUN_180278890 are NOT DECODED) = an exact drop of the ball tool along -Z onto the triangles; the
 * points the projection adds between the pass points; the point filter. Measured (from the stored NCI, not from code): the
 * approach / retract, the short-gap link shape, the pass order (One way: every pass in the chain direction; Zigzag: odd passes reversed).
 * Refused here (NOT DECODED): gap types other than Broken (FUN_180140d80's tags 0x10005 / 0x10006 / 0x10008 are geometry for a later stage), tools other than ball. The caller refuses check surfaces
 * (they switch Mastercam to the non-EMM projection FUN_180293f40, bl 3.1) and tool / construction planes other than Top (the lm frame
 * FUN_1800cc7b0 is applied as the identity here). */
#ifndef CAM3D_BLEND_H
#define CAM3D_BLEND_H
#include <stddef.h>
#include <stdint.h>
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double stepover;     /* record 0x1420 (lm +0x10; <= 1e-6 -> 0.05) */
    double frac;         /* 0x1438 "% of stepover" along the chains (lm +0x20; <= system tol -> 1.0) */
    int method;          /* 0x1428 (short): 0 Zigzag, 1 One way, 2 Spiral (lm +0x48) */
    int along;           /* 0x142a: 1 Along, 0 Across (lm +0x40) */
    int two_d;           /* 0x142b: 1 2D, 0 3D (lm +0x44) */
    int quick;           /* 0x1440: 3D projection Quick (lm +0x50); does not change the passes (bl 3.4) */
    double tol;          /* 0x1240 total tolerance (lm +8) */
    double D, r;         /* tool diameter 0x350 / corner radius 0x358 */
    int tool_type;       /* 0x34c */
    double stock;        /* 0x2e0 stock to leave on the drive */
    int tip;             /* 0x1239 compensate to tip (1) / centre (0) */
    double max_gap;      /* max short gap: 0x1298 x D with 0x1288, else 0x1290 (msurf_map l.340021-340024, l.340445) */
    int gap_type;        /* (0x12a0 % 100) % 10 (l.340025) */
    double clearance, feed_plane;   /* the approach's heights (absolute clearance, incremental feed plane) */
    double clr_h, ret_h, fp_h, ret_feed;   /* the record's heights for a long gap (0x170 / 0x180 / 0x190) and the retract rate 0x388 */
    int clr_on, clr_inc, ret_on, ret_inc, fp_inc, rapid_ret, long_gapchk;   /* 0x179 / 0x178, 0x189 / 0x188, 0x198, 0x1a9, 0x12a3 */
    double feed, plunge;
} BlendParams;

typedef struct { double *p; int n; } BlendPoly;   /* 3D points x y z, n points */

/* the record fields above from an op record (record = op struct + 0x68); 0 with why = a NOT DECODED / unsupported case */
int blend_gap_planes(const BlendParams *bp, double z, double *plane, double *feed);   /* the long-gap heights at a point of height z; 1 = through the plane (FUN_180142420) */
int blend_params_from_record(const uint8_t *rec, uint32_t n, BlendParams *bp, char *why, size_t whyn);

/* a stored op's chain `chain_no` (TP kind 7 references, in index order, rev flags) as a polyline: lines, arcs (XY and view arcs),
   points, NURBS / parametric spline curves (mcsurf.c mcs_curve_eval). step = the largest chord of the curve pieces. 0 = missing */
int blend_chain_from_tp(const McamFile *f, int op_id, int chain_no, double step, BlendPoly *out, char *err, size_t errn);
/* the same from F.ents indices with rev flags (cam2d.h Chain .ent / .rev / .n: points, lines, arcs), for an op made here */
int blend_chain_from_ents(const McamFile *f, const int *ent, const int *rev, int n, double step, BlendPoly *out, char *err, size_t errn);
/* the op's drive surfaces (TP kind 4, class 0x100) as triangles (mcsurf.c mcs_mesh, deflection defl); returns the count, -1 err */
int blend_drive_from_tp(const McamFile *f, int op_id, double defl, double **tri, char *err, size_t errn);

/* the toolpath: chains c0 / c1 (the op's first / second chain), drive triangles tri (9 doubles each). Moves malloc'd into *mv.
   Returns 1, or 0 with err. info (may be NULL): pts per chain, passes */
typedef struct { int npts, npass; double maxlen; } BlendInfo;
int blend_generate(const BlendParams *bp, const BlendPoly *c0, const BlendPoly *c1, const double *tri, int ntri,
                   Move **mv, int *nmv, BlendInfo *info, char *err, size_t errn);
void blend_poly_free(BlendPoly *p);

#ifdef __cplusplus
}
#endif
#endif
