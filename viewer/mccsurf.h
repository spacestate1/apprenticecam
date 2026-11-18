/* mccsurf.h - Wireframe > Curve Surface (mccad sketcher_curve_6 -> mccore project_chains_onto_surfs 0x180569380): curves lying on
   surfaces turned into surface curves (CONS_ID 0x400 records with a 2D order-2 NURBS parameter curve). formats/wireframe.md
   "Curve Surface". The kernel (mccsurf.c) is a transliteration of mccore's msf code on entity images laid out as mccore's `ent`. */
#ifndef MCCSURF_H
#define MCCSURF_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* a curve to project: a part record (line 2, arc 4, NURBS 0x80, parametric spline 8: the ent bytes after the u32 length) and,
   for an arc in a view >= 2, that view's matrix rows (view_to_world); vm = NULL: view 1 (identity) */
typedef struct { const uint8_t *rec; uint32_t n; const double *vm; } CsCurve;
/* a surface: its record and, for a trimmed one (kind 10), its base record */
typedef struct { const uint8_t *rec; uint32_t n; const uint8_t *base; uint32_t nbase; } CsSurf;
/* one surface curve made: the CONS body (0x88 bytes: start, end, t1, t2, curvature, uv ends, +0x68 surface / +0x70 uv curve
   references left 0, +0x80 closed, +0x81 1), the surface it lies on (index into the CsSurf list; its base when trimmed: put_cons_in_db
   stores the base) and the uv points of the order-2 NURBS (array_2d_to_nurbs: knots 0, 0, 1, .., n - 1, n - 1) */
typedef struct { uint8_t body[0x88]; int srf; int npts; double (*uv)[2]; } CsResult;

/* mastercam.dll's view table (mcViews, entries 1 .. n_views: the 18 built in, the part's, those added this session) as find_view_k
   (0x180337960) and world_to_view (0x180341b30) read it, for the pieces' order (project_chains_onto_surfs LAB_180569df8):
   count() = n_views; get(v, m) = view v's matrix rows x, y, z (0: no entry numbered v); add(m) = AddMCView(m, 2e-7) (0x18031df40):
   the number of the first view matching m within 2e-7 element by element (mx33_match), else a new view n_views + 1 (<= 0: failed) */
typedef struct {
    int (*count)(void *ctx);
    int (*get)(void *ctx, int v, double m[9]);
    int (*add)(void *ctx, const double m[9]);
    void *ctx;
} CsViews;

typedef struct {
    double tol;          /* config 1000 (mccore tol) */
    double chord;        /* config 1007 surf_step_chord_dev (sketcher_curve_6 passes it as the chord and the join tolerance) */
    double min_step;     /* config 1004 curve_min_step (FUN_180533170's curve grid) */
    int metric;          /* IsMetric() */
    const CsViews *views;   /* the view table (required: NULL refuses) */
} CsParams;

/* project one curve as sketcher_curve_6 does (one chain of one entity). Returns the number of surface curves made (n, *res
   malloc'd, free with cs_free), 0 when the curve lies on no surface (the original is kept), -1 when a case is not ported (msg). */
int cs_project(const CsCurve *c, const CsSurf *s, int ns, const CsParams *p, CsResult **res, char *msg, size_t msgn);
void cs_free(CsResult *r, int n);

/* a surface curve's points for display (mastercam.dll FUN_180237990): the uv polyline of the order-2 parameter curve evaluated
   on the surface; returns the count (pts malloc'd: x y z), 0 when not drawable here */
int cs_cons_polyline(const uint8_t *cons, uint32_t ncons, const uint8_t *uvcrv, uint32_t nuv, const CsSurf *s, double (**pts)[3]);
/* a point of a surface curve at parameter t (FUN_1803ec480: the uv curve at t, then the surface); 0 ok */
int cs_cons_eval(const uint8_t *uvcrv, uint32_t nuv, const CsSurf *s, double t, double p[3]);
/* gl_Hit_Surfs' (u, v) on the hit surface (mastercam.dll FUN_180224220's tail: build_grid / find_neighbor / project_pt_onto_surf(surf,
   -1, 0, ..), formats/chaining.md "The shading-on pick (port)"): p->tol / p->metric as mccore's tol / IsMetric(), gl_tol = config 7004.
   Returns 1 when a projection was accepted (within 20 gl_tol of the hit), 0 none, -1 not ported (kinds other than 0 / 10 on a kind 0
   base). uv = the last (u, v) the projection wrote (*uv_set 0: none wrote one) */
int cs_glhit_uv(const CsSurf *s, const CsParams *p, const double hit[3], double gl_tol, double uv[2], int *uv_set);
/* eval_surf(surf, 0, u, v): 0 ok */
int cs_surf_point(const CsSurf *s, double u, double v, double P[3]);
#ifdef __cplusplus
}
#endif
#endif
