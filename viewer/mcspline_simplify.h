/* mcspline_simplify.h - the engine of Wireframe > Simplify Spline: mccadutil convert_ent_to_line_or_arc (mcspline_simplify.c) */
#ifndef MCSPLINE_SIMPLIFY_H
#define MCSPLINE_SIMPLIFY_H
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif
/* a part view, numbered n, with its matrix rows: the table find_view_k searches (the part's stored views; views 1..8 come from mcam_std_view) */
typedef struct { int n; double m[9]; } McsSimpView;

typedef struct {
    int is_line;                /* convert_ent_to_line_or_arc's char *param_3: 1 = the l_3d, 0 = the a_3d */
    double line[6];             /* the l_3d: +0x00 first point, +0x18 second point (curve_endpoints) */
    double a_p1[3], a_p2[3];    /* a_3d +0x00 / +0x18: world end points (arc_ep_3d of the last 3-point arc) */
    double c[3];                /* a_3d +0x30: centre in the view (x, y, z) */
    double r, a0, sw;           /* a_3d +0x48 radius, +0x50 start angle, +0x58 sweep */
    int view;                   /* a_3d +0x60: the view found (or made) by find_view_k */
    double view_m[9];           /* that view's matrix rows */
    int view_is_new;            /* 1: find_view_k made the view (AddMCView): the caller must add view_m to the part's views as view `view` */
} McsSimplify;

/* convert_ent_to_line_or_arc on the spline record c (cls 8 / 0x80, as mcs_curve_eval reads it): 1 = converts (see out->is_line), 0 = not. tol = the
   command's tolerance, systol = the system tolerance (mccore tol). views / nviews = the part's stored views (McamFile.fviews), may be NULL / 0. The
   record is not modified (the engine's temporary ent +0x98 / +0xa0 / stored ends work on a local copy, as the DLL's fill_in_ent restores them). */
int mcs_simplify_curve(const CurveRec *c, double tol, double systol, const McsSimpView *views, int nviews, McsSimplify *out);
#ifdef __cplusplus
}
#endif
#endif
