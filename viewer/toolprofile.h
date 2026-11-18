/* toolprofile - Mastercam 2017 parametric mill tool side profile (tlcore.dll TlProfileBuilderParametric).
 * Spec: ../formats/tool_profiles.md (every rule decoded from tlcore.dll, cited per function there and in toolprofile.c).
 * Axes (spec 0.3): x = radius (>= 0), y = height above the tool tip; the chain starts at (0, 0) (or (0, d) for a face
 * mill) and ends with a line (x, OAL) -> (0, OAL). Nothing here is invented: rules the spec marks NOT DECODED return
 * TP_ERR_NOT_DECODED instead of geometry. */
#ifndef TOOLPROFILE_H
#define TOOLPROFILE_H
#include "mcam_load.h"

/* TlProfileResultCode (toolnetapi/Cnc.Tool.Interop/TlProfileResultCode.cs; spec 0.5). Negative values are this
   port's own (not Mastercam's): output array full, and a branch the spec does not decode. */
enum {
    TP_OK = 0, TP_IllegalToolDiameter = 1, TP_IllegalToolLength = 2, TP_IllegalShoulderLength = 3,
    TP_IllegalFluteLength = 4, TP_IllegalArborDiameter = 5, TP_IllegalFluteShoulderLengths = 6,
    TP_ShoulderLengthCornerRadError = 8, TP_CornerRadiusTooLargeForRadius = 9, TP_IllegalCornerRadius = 10,
    TP_ArborRadiusExceedsToolRadius = 11, TP_TipToolDiameterIntersectionError = 12, TP_ArborDiameterTooLarge = 13,
    TP_CornerRadiusOrFluteLengthError = 14, TP_TipAngleTooSteep = 15, TP_ShoulderAngleTooSteep = 16,
    TP_ShoulderLengthTooShortForDiameter = 18, TP_TipDiameterExceedsToolDiameter = 19, TP_CheckCornerRadius = 20,
    TP_ToolDiameterEqualsArborDiameter = 21, TP_CheckCornerRadiusArborDiameter = 23,
    TP_TaperAngleOrFluteLengthError = 25, TP_CheckTaperAngle = 26, TP_ShoulderDiameterLargerThanArbor = 27,
    TP_ShoulderDiameterCantBeZero = 30, TP_IllegalReducedNeckRadius = 33, TP_ReducedNeckLargerThanShoulderShank = 34,
    TP_NoNeckLength = 35, TP_LowerRadiusLargerThanProfileRadius = 36, TP_ShoulderDiameterLargerThanToolDiameter = 37,
    TP_ThreadPitchTooLargeForDiameter = 38, TP_ChamferDistanceTooLargeForDiameter = 39, TP_ProfileRadiusTooSmall = 40,
    TP_ProfileRadiusTooLarge = 41, TP_LowerRadiusTooLarge = 42, TP_UpperRadiusTooLarge = 43,
    TP_InvalidLowerTaperAngle = 44, TP_InvalidUpperTaperAngle = 45, TP_InvalidReliefTaperAngle = 46,
    TP_ReliefAngleNonZeroWithInvalidDiameters = 47, TP_UnknownError = 99,
    TP_ERR_OVERFLOW = -1,           /* more entities than the caller's array holds */
    TP_ERR_NOT_DECODED = -2         /* buttress thread teeth, dove mill r1 > 0 (spec 3.13 / 3.17 / 6) */
};

typedef struct {                    /* one TlProfileEnt (spec 0.2): type 2 'L' line / 4 'A' arc (as MillTool AddEntToBuffer) */
    int type;
    double x0, y0, x1, y1;          /* end points (arcs: arc_ep_3d of start / start + sweep) */
    double cx, cy, r, a0, sw;       /* arc: centre, radius, start angle, sweep (radians, + = CCW in (x, y)) */
} TpSeg;

typedef struct {                    /* MillToolProfileParams (200 bytes, spec 0.1); spec symbol in [] */
    int ent_color, is_metric;       /* +0x00 / +0x01: not read by any make* function */
    double nc_tol, tol;             /* +0x08 (1e-4), +0x10 (5e-5): tol is used in every comparison (spec 0.4) */
    int type;                       /* +0x18 MCToolType 0..25 */
    int corner_type;                /* +0x1c TlCornerType (face mill: 3 = full radius) */
    double oal, fl, d, dt;          /* +0x20 [OAL], +0x28 flute length [FL], +0x30 diameter [D], +0x38 tip dia [Dt] */
    double r1, r2;                  /* +0x40 / +0x48 corner radius 1 / 2 */
    double a, c, b;                 /* +0x50 angle1 [A], +0x58 angle2 [C], +0x60 angle3 [B] (degrees) */
    double c1, c2;                  /* +0x68 / +0x70 chamfer lengths */
    double lp, dp;                  /* +0x78 pilot length [Lp], +0x80 pilot diameter [Dp] */
    int thread_shape;               /* +0x88 TlThreadShape 0 UN, 1 metric, 2 Acme, 3 Acme stub, 4/5 buttress, 6 NPT */
    double pitch;                   /* +0x90 [P] */
    int shank_type;                 /* +0x98 0 straight, 1 tapered, 2 reduced neck */
    double sl, dsh, tl, dn, da;     /* +0xa0 [SL], +0xa8 [Dsh], +0xb0 taper length [TL], +0xb8 neck [Dn], +0xc0 arbor [Da] */
} TpParams;

/* RetrieveParametricToolProfile / CalcProfile: 0 = ok, else the TlProfileResultCode. *n = entities written (on an
   error: the partial chain, as CalcProfile returns it - spec 0.5). */
int tp_build(const TpParams *p, TpSeg *out, int max, int *n);
/* the outline as (radius, height) points from the tip up, arcs sampled at <= arc_step_rad; returns the point count */
int tp_polyline(const TpSeg *s, int n, double *r, double *z, int max, double arc_step_rad);
/* fill from a Tool: the .NET RetrieveParametricToolProfile arguments per class (formats/toollib_net.md 4.2), from
   the .tooldb columns (Tool.has_extras); a part-file tool has only the common record (tip / taper angle +0x20). */
void tp_params_from_tool(const Tool *t, TpParams *p);
const char *tp_err_name(int code);
#endif
