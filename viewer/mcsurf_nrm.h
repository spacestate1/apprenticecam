/* mcsurf_nrm.h - surface derivatives, the surface normal and the point-to-surface projection of mccore (mcsurf_nrm.c) */
#ifndef MCSURF_NRM_H
#define MCSURF_NRM_H
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif
/* eval_surf: D[] = S, Su, Sv, Suu, Suv, Svv, Suuu, Suuv, Suvv, Svvv (the first (order+1)(order+2)/2 written). 0 ok; -1 bad record;
   -2 the derivative order not ported for this surface kind (kinds 4, 6, 7, 8 above order 1) */
int mcsn_eval(const McamFile *f, const Surf *s, int order, double u, double v, double D[][3]);
/* calc_surf_norm / calc_surf_unit_norm: 0 ok; 1 the normal not found (degenerate); -2 a higher derivative of the kind not ported */
int mcsn_norm(const McamFile *f, const Surf *s, const double Su[3], const double Sv[3], double u, double v, double N[3]);
int mcsn_unit_norm(const McamFile *f, const Surf *s, const double Su[3], const double Sv[3], double u, double v, double N[3]);
/* project_pt_onto_surf(surf, view, allow_outside = 1, P, uv0, uv, Sout): the view's Z through P, Newton from uv0. V = the view's
   rows (x, y, z of the view in world). 1 found; 0 not. Trimmed: uv inside the trim loops */
int mcsn_project(const McamFile *f, const Surf *s, const double V[9], const double P[3], const double uv0[2], double uv[2], double Sout[3]);
/* FUN_1801c90d0's surface branch (Point Dynamic / Line Normal): the step of the running point (u, v) for a mouse move or a click.
   TV = the graphics view's rows, CV = the Cplane's rows. proj = 1: Pw (world) projected along the Cplane's Z; 0: the view-plane delta
   vd moved through Su, Sv. Result: uv moved and clamped / held, S (the point on the surface), N (unit, reversed with bit 4). 0 ok;
   1: the normal failed at a higher derivative not ported (msg names it) */
typedef struct { double uv[2], last[2], S[3], N[3], su2[2], sv2[2]; int shown; } McsnTrack;
int mcsn_track(const McamFile *f, const Surf *s, McsnTrack *t, const double TV[9], const double CV[9], int proj, const double Pw[3], const double view_xy[2], char *msg, size_t msgn);
/* variants with the trim loops given apart (trim = a kind 10 Surf record holding the loops, for a solid face's temporary surface; NULL: s's own) */
int mcsn_project_t(const McamFile *f, const Surf *s, const Surf *trim, const double V[9], const double P[3], const double uv0[2], double uv[2], double Sout[3]);
int mcsn_track_t(const McamFile *f, const Surf *s, const Surf *trim, McsnTrack *t, const double TV[9], const double CV[9], int proj, const double Pw[3], const double view_xy[2], char *msg, size_t msgn);
#ifdef __cplusplus
}
#endif
#endif
