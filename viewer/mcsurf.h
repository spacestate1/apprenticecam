/* mcsurf.h - positions on Mastercam curve / surface entities (mcsurf.c) */
#ifndef MCSURF_H
#define MCSURF_H
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif
const CurveRec *mcs_curve(const McamFile *f, int64_t eptr);
const Surf *mcs_surf(const McamFile *f, int64_t eptr);
int mcs_curve_eval(const McamFile *f, const CurveRec *c, double t, double p[3]);       /* 0 ok, -1 not ported / bad */
int mcs_surf_eval(const McamFile *f, const Surf *s, double u, double v, double p[3]);  /* kinds 1, 2 (and 10 on them) */
void mcs_surf_domain(const Surf *s, double d[4]);                                      /* ent +0x70: u0 u1 v0 v1 */
int mcs_surf_kind_ok(const Surf *s);                                                   /* the kind can be meshed */
/* triangles (9 doubles each) and boundary segments (6 each, seg / nseg may be NULL) in model units; -1 with err */
int mcs_mesh(const McamFile *f, const Surf *s, double defl, double **tri, double **seg, int *nseg, char *err, size_t errn);
/* surf_division (the flowline spacing / sample step of a pass) and the surface's wireframe polylines (FUN_180238320 / FUN_180238ee0):
   wk = the view height in model units, xs / ys pixels per unit, tol the system tolerance; shaded_outline = render_cfg 4 (the
   untrimmed surfaces' boundary only). pts: x y z floats, cnt: points per polyline (malloc'd). */
int mcs_surf_division(const McamFile *f, const Surf *s, int pass, double wk, double xs, double ys, double tol, double *s3, double *s4);
int mcs_wireframe(const McamFile *f, const Surf *s, int shaded_outline, double wk, double xs, double ys, double tol, float **pts, int **cnt, int *npoly);
/* spline curves (record classes 0x8 / 0x80; formats/spline_chain.md) */
int mcs_spline_ok(const CurveRec *c);                                                  /* a spline record this code can evaluate */
void mcs_spline_t1t2(const CurveRec *c, double *t1, double *t2);                       /* curve_t1t2: ent +0x98 / +0xa0 */
/* eval_nurbs (mccore 0x1805ae2d0) of a NURBS record (class 0x80): o[0] the point, o[1] / o[2] the derivatives (nd <= 2), the DLL's arithmetic */
void mcs_eval_nurbs(const uint8_t *rec, int nd, double t, double o[3][3]);
int mcs_spline_eval_d(const CurveRec *c, double t, int nd, double o[3][3]);            /* point, 1st, 2nd derivative (nd <= 2) */
/* brk_spline_const_err (mccore 0x180162540): the next break parameter from *t (backwards when rev) */
void mcs_brk_spline_const_err(const CurveRec *c, double min_step, double max_step, double tol, int rev, double systol, double *t);
#ifdef __cplusplus
}
#endif
#endif
