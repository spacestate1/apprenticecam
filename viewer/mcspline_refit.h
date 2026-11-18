/* mcspline_refit.h - Mastercam 2017's Refit Spline engine on parametric cubic splines (mccore.dll MaCubicSpline / MaCompositeCurve::Refit; driver mccadutil FUN_1801347c0),
 * see mcspline_refit.c for sources and what is NOT ported. Pure numerics, no UI. */
#ifndef MCSPLINE_REFIT_H
#define MCSPLINE_REFIT_H
#include <stdint.h>
typedef struct {
    double chord_dev;    /* FUN_1801347c0 param_2[0] -> MaCompositeCurve::Refit param_1, MakeSmoothFitCubicSpline param_1 */
    double angle_deg;    /* param_2[1] -> Refit param_2; also the HasSharpNodes / BreakSplineAtSlopeDiscontinuities angle of the driver */
    double fair_dist;    /* param_2[2] -> clamped by *param_3 (max_fair), Refit param_3 (BlendAndFairNodes param_2) */
    int allow_more_nodes;/* byte param_2[3] (+0x18): when 0 the driver refuses (status 1) a smooth curve whose refit has more nodes than the original */
    int break_at_sharp;  /* int param_2[4] (+0x20) == 1: spline with HasSharpNodes(angle) is broken at its slope discontinuities, not refit */
    double max_fair;     /* *param_3 (FUN_1801347c0 clamps fair_dist to min(fair_dist, *param_3)) */
    double tol;          /* the DLL global tol (mccore ?tol@@3NA) */
    int metric;          /* IsMetric() (mccore.dll DAT_18142d400): the GetClosedInfo tolerance 0.0001 / 0.0025 */
    int nurbs;           /* cfg_createNurbs: the output class of the break_at_sharp branch only (the refit branch always stores a NURBS: MaCubicSpline::ConvertToNURB + MakeMCEnt) */
    double step_chord_dev; /* mccore.dll global surf_step_chord_dev: HandlePiecewiseLinear's fit on load, MakeSplineOrNURBCurve's fallback */
    int no_load_fit;     /* test only: LoadCurve(ent, false): a degree 1 NURBS stays a polyline (the driver always loads with true); exercises FairPiecewiseLinearToCubicSpline */
} McrParams;
typedef struct { uint32_t cls; uint8_t *rec; uint32_t n; } McrRec;
typedef struct { int nrec; McrRec *rec; int broke; int clamped; } McrResult;   /* broke: driver *param_4; clamped: Refit returned 2 (BlendAndFairNodes overlap) */
/* Refit one spline record (class 8 order 4 or NURBS class 0x80, the file layout of mcsf_spline_rec / formats/mcam.md). 0 ok; 1 failure (the driver's status); -1 not ported input (see mcspline_refit.c). */
int mcr_refit(const uint8_t *rec, uint32_t n, const McrParams *p, McrResult *out);
void mcr_free(McrResult *r);
int mcr_cubic_break(int *ns, double **C, double **U, double t, int keep_first);   /* MaCubicSpline::Break: 0 broke (the arrays replaced, malloc'd) */
int mcr_nurbs_break(int *ncp, int ord, double **K, double **P4, double t, int keep_first);   /* MaNURBSpline::Break of a non-rational curve (x y z w per point, kept homogeneous) */
int mcr_poly_pts_spline(int n, const double (*P)[3], double **C, double **U);   /* MaCubicSpline(const MaPolyLine *): the segments (12 algebraic coefficients each) and the u list; returns the segment count */
/* Helix / Spiral as Mastercam stores them (MaHelix / MaSpiral::MakeMCSplineEntities, mccad FUN_180089040): r, revs (spiral: irevs), pitch, taper, initial angle (helix); pitches initial / final (spiral); dir 1 = Y negated */
typedef struct { double c[3], x[3], y[3], z[3], r, revs, pitch, taper_deg, init_deg, v0, v1, h0, h1; int irevs, dir, spiral; } McrHelix;
int mcr_helix(const McrHelix *in, double tol, double chord, int metric, McrResult *out);   /* 0 ok */
#endif
