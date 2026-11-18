/* mcsrfint.h - the surface / plane intersection kernel (mcsrfint.c) */
#ifndef MCSRFINT_H
#define MCSRFINT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ISrf_ {
    int ncu, ncv, ku, kv, rat;
    double *KU, *KV, *V;     /* knots normalised to 0 .. 1 (MaNURBSurf's DB constructor), V = wx wy wz w, u fastest */
    double d[6];             /* MaSurface +0x20 .. +0x48: u0 u1 v0 v1 du dv */
    int metric, edges, rev;   /* rev: normal reversed (MaNURBSurf DB ctor: ent +0x6e bit 2 -> this[0x10]) */
    char deg[4], cl[2], per[2];
    double *gu, *gv; int ngu, ngv;   /* the hunting grid */
    double *gp;              /* its points (ngu x ngv, x y z), MaSurfHuntingGrid +0x18 */
    struct ISrf_ *obase; double odist;   /* MaOffsetSurf: the bottom base surface (MaOffsetSurf::GetBottomBaseSurf 0x180463d00) and the total offset (+0x98); NULL: a NURBS */
} ISrf;
typedef struct { uint8_t cut[0x2a8]; double plane[4], su, sv, chord, chord2, maxstep, minstep; double **P, **T, **UV; int n; char flag248; ISrf *dr; double du, dv; double **UV2; } MaInt;   /* dr: the drive surface of MaSurfSurfIntersect */
typedef struct { int deg, ncp, nk, npts; double *k, *p, t0, t1; } MiCurve;   /* non-rational: p = x y z per control point */
void is_eval(const ISrf *s, int order, double u, double v, double *E);
void is_edges(ISrf *s);
int is_pmd(ISrf *s, int hunt, const double *P, double *out, double *dist, double *u, double *v, int limit);
int mi_pieces(ISrf *s, ISrf **out); void mi_free_pieces(ISrf *p, int n);   /* the smooth pieces */
int mi_plane_piece(ISrf *s, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **out);
void mi_dist_to_plane(ISrf *s, const double pl[4], double *dmin, double *dmax, double *mid);
int mi_surf_surf(ISrf *a, ISrf *b, double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **out);   /* MaSurfSurfIntersect::ComputeIntersections (Curve at Intersection) */
int mi_join_curves(MiCurve **c, int n, double tol, int metric);   /* MaCurve::JoinCurves */
typedef struct MiFace MiFace;   /* MaFace: the loops, on the surface */
typedef struct { double *oldU[2], *newU[2]; int nU[2]; double d0[6]; } MiRegen;   /* d0: the range before the regeneration (the surface LoadUntrimmedSurf(.., false) gives) */
int mi_db_knots(ISrf *s, const double H[4], int regen, MiRegen *R);   /* MaNURBSurf's DB constructor on the raw knots / range: bits 0-1 the regenerated directions, bit 2 this[0x1c] */
void mi_regen_knots(ISrf *s, int rep, MiRegen *R); double mi_regen_map(const MiRegen *R, int dir, double x); void mi_regen_free(MiRegen *R);
double mi_regen_unmap(const MiRegen *R, int dir, double x);   /* the inverse of mi_regen_map */
void mi_uv_transform(double *u, double *v, const double H[4], const double d[6]);   /* MaPolyLine::UVTransform 0x1804754e0 of one point: the record's range H to the surface's range d */
void mi_grid_lists(ISrf *s, const double **gu, int *nu, const double **gv, int *nv);   /* MaSurfHuntingGrid::GenGrid's lists (mcsrfint.c grid_build) */   /* the knots regenerated from the derivative ratios (a surface with a full-multiplicity interior knot) */
MiFace *mi_face_build(ISrf *s, int nl, const int *np, double (*const *lp)[2], double chord, double ang, double chain_tol);   /* the stored loops, normalised to the surface's (u, v); NULL: no outer loop */
void mi_face_subset(MiFace *f, double fraction);   /* SubsetToFace */
int mi_face_reparam(const MiFace *f);   /* MaFace::CheckFaceToSurfaceDeltaUVRatios's reparameterisation would run: not ported */
void mi_face_free(MiFace *f);   /* releases every object of the trim */
int mi_surf_plane_face(ISrf *s, MiFace *F, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **out);   /* per march the whole fitted NURBS with its trimmed t0, t1 */
int mi_plane_piece_face(ISrf *s, MiFace *F, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **out);   /* one smooth piece */
void srf_mi_free(ISrf *s);   /* the hunting grid */
int mi_surf_plane(ISrf *s, const double pl[4], double chord, double chord2, double maxstep, double minstep, int multi, MiCurve **out);
#ifdef __cplusplus
}
#endif
#endif
