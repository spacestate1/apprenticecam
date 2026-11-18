/* mcspline_chain.h - the engine of Spline From Curves (chain -> one spline), mccore.dll make_nurbs_from_chains / make_spline_from_chain. See mcspline_chain.c */
#ifndef MCSPLINE_CHAIN_H
#define MCSPLINE_CHAIN_H
#include <stdint.h>
#include "mcspline_fit.h"
#ifdef __cplusplus
extern "C" {
#endif

/* one chain link. cls: 2 line, 4 arc, 8 parametric spline, 0x80 NURBS curve (ent +0x00); any other class is skipped (make_nurbs_from_chains
   (local_5338 & 0x8e) != 0). a / b: ent +0x68 / +0x80, start / end (world). Arc: c = ent +0x98 the centre in VIEW coordinates (a_3d +0x30), r +0xb0,
   a0 +0xb8, sw +0xc0 (radians, sw < 0 clockwise), vm = the view's rows (mastercam.dll view_to_world / viewz; NULL = views < 2, identity with z (0,0,1)).
   Spline / NURBS: rec, n = the record as CurveRec (inline coefficients / knots + points). reverse: chain link +0x18 == 1. */
typedef struct {
    int cls;
    double a[3], b[3], c[3];
    double r, a0, sw;
    const double *vm;
    const uint8_t *rec;
    uint32_t n;
    int reverse;
} McscEnt;

/* return codes */
enum { MCSC_OK = 0, MCSC_NONE = 1,    /* the DLL creates nothing: no usable link (FUN_18025f430 *param_2 == 0), or fewer than 2 control points / knots */
       MCSC_DLL_UNDEFINED = 2,        /* the DLL's own result is undefined here, nothing to reproduce: a chain whose stacked-vertices rebuild leaves no piece or fewer knots than the chain
                                         (the caller's memcpy reads past the array, or an MFC exception), or a parametric spline in 2D construction (squash_spline reads an unset stack variable) */
       MCSC_MEM = 3,                  /* calloc / realloc failed (post_warning 0x1813) */
       MCSC_TOO_LONG = 4,             /* spline_from_chain_ent: the spline node table cannot grow (post_warning 0x1574) */
       MCSC_BADREC = 5 };             /* a curve record the port cannot read */

/* make_nurbs_from_chains (mccore 0x180262750), one chain, default kind. fit_tol = the panel Tolerance (param_2, only the non-cubic curves use it), sys_tol = the
   system tolerance (the global tol). On MCSC_OK *o is the raw nb_type (o->k, o->cp malloc'd: free both); the 0x80 record = mcsf_nurbs_rec(o, sys_tol, &n). */
int mcsc_nurbs(const McscEnt *e, int n, double fit_tol, double sys_tol, McsfNb *o);

/* make_spline_from_chain (mccore 0x1806604a0), one chain, parametric kind. closed = CHAIN +8 (byte), t_start / t_end = CHAIN +0x28 / +0x30 (a partial chain's first / last
   entity parameter, 0 = whole). On MCSC_OK *coef = nseg x 12 doubles (malloc'd: free), p0 / p1 the record's start / end (eval_spline 0 and nseg), *closed_out the closed
   bit; the class 8 record = mcsf_spline_rec(*coef, *nseg, p0, p1, *closed_out, sys_tol, &n). */
int mcsc_spline(const McscEnt *e, int n, int closed, double t_start, double t_end, double fit_tol, double sys_tol,
                double **coef, int *nseg, double p0[3], double p1[3], int *closed_out);

/* 2D construction (constr_view_3d() false): the plane the result is squashed onto, n = the construction view's z row (get_view_matrix), depth = constr_depth plus the active origin's view z when
   ActiveOriginIsOn. NULL = 3D construction. */
typedef struct { double n[3]; double depth; } McscPlane;

/* both, as one call producing the entity record (malloc'd, *rn bytes): kind 0 = NURBS (cfg_createNurbs on), 1 = parametric spline. plane: see McscPlane. For the UI. */
int mcsc_chain_record(const McscEnt *e, int n, int kind, int closed, double t_start, double t_end, double fit_tol, double sys_tol, const McscPlane *plane, uint8_t **rec, uint32_t *rn);

double mcsc_nb_curvature(const uint8_t *rec, double t);  /* nb_parm_curvature, for the oracle */
/* entity level, for the oracle: FUN_180260910 (a NURBS piece) and convert_entity_to_coeffs (cubic coefficients). cp holds ncp x vpp doubles (vpp = rational + 3) */
int mcsc_ent_nurbs(const McscEnt *e, int rational, double fit_tol, double sys_tol, double **cp, double **k, int *ncp, int *nk);
int mcsc_ent_coeffs(const McscEnt *e, double fit_tol, double sys_tol, long room, double *out, double *t_stop, long *nseg, int *full, int mode, double t, const double pt[3]);

#ifdef __cplusplus
}
#endif
#endif
