/* mcsurf_build.h - put_surf_in_db's header fields of a new SURF_ID record (mcsurf_build.c) */
#ifndef MCSURF_BUILD_H
#define MCSURF_BUILD_H
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif
int mcsb_closed(const McamFile *f, uint8_t *rec, uint32_t n, int dir);           /* surf_closed: dir 1 u (bit 0), 2 v (bit 1) */
int mcsb_extent(const McamFile *f, uint8_t *rec, uint32_t n, int mode, double tol);   /* surf_extent: mode 1 box, 2 curvature; -1 */
int mcsb_finish(const McamFile *f, uint8_t *rec, uint32_t n, int shaded, double tol);
uint8_t *mcsb_arc_curve_rec(const McamFile *f, const Ent *en, double tol, uint32_t *n);   /* Roll: an arc as a class 0x80 NURBS record (malloc'd) */   /* put_surf_in_db's header fields */
uint8_t *mcsb_revolve(const McamFile *f, const Ent *pf, int64_t pf_eptr, V3 la, V3 lb, int64_t axis_eptr, double pick_t, double start,
                      double end, int reverse, int surface_type, int shaded, double tol, uint32_t *n, const char **err);   /* create_revs, one profile */
uint8_t *mcsb_draft(const McamFile *f, const Ent *cv, int64_t cv_eptr, const double N[3], double d, uint16_t fl, int surface_type,
                    int shaded, double tol, uint32_t *n, const char **err);   /* Draft: FUN_180126b90's kind 6 surface (as NURBS: FUN_180266ed0) */
uint8_t *mcsb_arc_wall(const McamFile *f, const double C0[3], const double C1[3], const double x[3], const double y[3], const double z[3],
                       double r0, double r1, double st0, double sw0, double st1, double sw1, int flip, int shaded, double tol, uint32_t *n);   /* Extrude: FUN_18001ddd0 */
uint8_t *mcsb_trim_rec(const McamFile *f, int64_t base_eptr, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt, uint16_t flags, int shaded, double tol, uint32_t *n);   /* a kind 10 record (one loop each, in order) */
uint8_t *mcsb_xform(const McamFile *f, const uint8_t *rec, uint32_t n, const double m[9], const double t[3], int type, int64_t new_base);   /* xform_store_ent, kinds 0 / 1 / 10 */
uint8_t *mcsb_xform_curve(const uint8_t *rec, uint32_t n, const double m[9], const double t[3], int type, double fac);   /* FUN_1808d7f00 / FUN_1808da030 */
uint8_t *mcsb_arc_nurbs_rec(const McamFile *f, const Ent *en, double tol, uint32_t *n);   /* convert_arc_to_nb + put_nurbs_in_db (a non-uniform scale) */
/* Surfaces > Simple: one face of the solid (occt_prim.h OpFace) as ps_face_to_mc_surf makes it */
uint8_t *mcsb_prim_nurbs(const McamFile *f, const void *face, int shaded, double tol, uint32_t *n);           /* the kind 0 record */
int mcsb_prim_loops(const McamFile *f, const uint8_t *base, uint32_t bn, const void *face, double ***pts, int **cnt);   /* uv polygons */
int mcsb_prim_untrimmed(const McamFile *f, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt);   /* the base is the face */
uint8_t *mcsb_prim_trim(const McamFile *f, int64_t base_eptr, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt,
                        int shaded, double tol, uint32_t *n);                                                        /* the kind 10 record */
uint8_t *mcsb_plane4(const McamFile *f, const double c[4][3], const double cz[3], int shaded, double tol, uint32_t *n);   /* create_nurbsurf_from_4_points */
uint8_t *mcsb_flat_trim(const McamFile *f, int64_t base_eptr, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt,
                        int shaded, double tol, uint32_t *n);                                                        /* Flat Boundary's kind 10 */
int mcsb_loft_alike(const McamFile *f, const Ent *const *E, const int *rev, const int *ne, int nch, double tol, int *deg, int *rat);   /* all_chains_alike (E / rev: chain c at c * 4096) */
uint8_t *mcsb_loft_alike_ruled(const McamFile *f, const Ent *const *E, const int *rev, const int *ne, int nch, int d, int rat,
                               const double cz[3], int shaded, double tol, uint32_t *n);   /* FUN_180701c20 */
void mcsb_normal_by(uint8_t *rec, uint32_t n, double u, double v, const double cz[3]);   /* Loft: the normal bit */
int mcsb_view_k(const double k[3], double M[9]);                                                                    /* make_view_k */
int mcsb_prim_outward(const McamFile *f, uint8_t *rec, uint32_t n, const double C[3]);                            /* FUN_18000d7d0 */
#ifdef __cplusplus
}
#endif
#endif
