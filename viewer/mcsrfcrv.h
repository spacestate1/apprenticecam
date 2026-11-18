/* mcsrfcrv.h - curves extracted from surfaces (mcsrfcrv.c, formats/wireframe.md "Curves on surfaces") */
#ifndef MCSRFCRV_H
#define MCSRFCRV_H
#include <stdio.h>
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif
int mcsc_uv_of_point(const McamFile *f, const Surf *s, const double P[3], double *u, double *v);
/* CreateConstantParameterCurves: direction 0 U, 1 V, 2 Both; the entity indices in out (cap), count *nmade; 0 on refusal with msg */
int mcsc_const_param(McamFile *f, const Surf *s, double u, double v, int direction, double tol, double systol, int level, int color, int nurbs_wanted /* cfg_createNurbs: surface type config 10014 == 2 */, int *out, int cap, int *nmade, char *msg, size_t msgn);
int mcsc_flow_number(McamFile *f, const Surf *s, int n, int direction, double tol, double systol, int level, int color, int nurbs_wanted, int *out, int cap, int *nmade, char *msg, size_t msgn);
int mcsc_flow_list(McamFile *f, const Surf *s, int method, double value, int direction, double tol, double systol, int level, int color, int nurbs_wanted, int *out, int cap, int *nmade, char *msg, size_t msgn);
extern int mcsc_nurbs_cfg;   /* cfg_createNurbs for mcsc_emit_mi */
int mcsc_emit_mi(McamFile *f, int deg, int ncp, int nk, const double *k, const double *p, double tol, double systol, int level, int color, int *out, int cap, int *made, const char **why);
int mcsc_emit_mi_range(McamFile *f, int deg, int ncp, int nk, const double *k, const double *p, double t0, double t1, double tol, double systol, int level, int color, int *out, int cap, int *made, const char **why);   /* with the curve's trim range */
int mcsc_surf_reparam(const Surf *b);   /* MaNURBSurf's DB constructor regenerates the knots (bit 0 U, bit 1 V) */
int mcsc_face_data(const McamFile *f, const Surf *s, const Surf **base, int *rep, int *nl, int **np, double (***pts)[2]);   /* the base NURBS and the stored loops of a trimmed surface */
struct ISrf_;
int mcsc_load_nbs(const McamFile *f, const Surf *b, struct ISrf_ *o, void *R /* MiRegen * or NULL */, double k0[2], double ks[2]);   /* LoadSurf(.., true, ..): the DB constructor (mcsrfint.c mi_db_knots) */
void mcsc_truncate_mi(int *deg, int *ncp, int *nk, double **k, double **p, double t0, double t1);   /* MaCurve::Truncate */
int mcsc_surf_frame(const McamFile *f, const Surf *s, double u, double v, double P[3], double Su[3], double Sv[3], double N[3]);   /* eval_surf + calc_surf_unit_norm for dyno_surf */
/* Curve One Edge / All Edges (MakeSurfaceEdgeCurve 0x1802735c0, MakeAllSurfaceEdgeCurves 0x1802732f0): untrimmed NURBS only (the trimmed-surface loop path is NOT PORTED) */
int mcsc_edge_one(McamFile *f, const Surf *s, double u, double v, double ang, double tol, double systol, int level, int color, int nurbs_wanted, int *out, int cap, int *nmade, char *msg, size_t msgn);
int mcsc_edge_all(McamFile *f, const Surf *s, double ang, double tol, double systol, int level, int color, int nurbs_wanted, int *out, int cap, int *nmade, char *msg, size_t msgn);
int mcsc_edge_supported(const McamFile *f, const Surf *s, char *msg, size_t msgn);
int mcsc_edg_case(const McamFile *f, const Surf *s, const char *path, double ang, double chord, double tol); int mcsc_edg_ours(const McamFile *f, const Surf *s, const char *path, double chord, double ang);   /* tests: the case file of tools/peloader --edg */
void mcsc_edg_batch(const McamFile *f, const char *dir, double ang, double chord, double tol); void mcsc_edg_scan(const McamFile *f, double chord);   /* tests */
int mcsc_emit_edge(McamFile *f, int type, const double p[6], const double c[3], const double n[3], double r, int deg, int nk, int ncp, int rat, const double *k, const double *cp, int along, int pk_along, double systol, int level, int color, int *out, int cap, int *made, const char **why);   /* ps_edge_to_mc_curve */
int mcsc_open_surf(const McamFile *f, const Surf *s, int *nu, int *nv, int *du, int *dv, int *nku, int *nkv, const double **ku, const double **kv, double **cp);
int mcsc_pmd_case(const McamFile *f, const Surf *s, const char *path);
void mcsc_scan(const McamFile *f);   /* tests */
#ifdef __cplusplus
}
#endif
#endif
