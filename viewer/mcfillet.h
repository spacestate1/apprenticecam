/* mcfillet.h - find_fil_gt, see mcfillet.c */
#ifndef MCFILLET_H
#define MCFILLET_H
typedef struct { int line; double x1, y1, x2, y2; double cx, cy, r, sa, sw; } McfG;   /* gt: a line (0x4c) x1..y2, an arc (0x41) centre, radius, start, sweep (signed) */
typedef struct { double cx, cy, r, sa, sw; double t1[2], t2[2]; int small; } McfArc;  /* the arc; t1 = param_6 (the tangent point on each curve), t2 = param_7 */
/* returns the number of solutions; *sel = the one Mastercam proposes (-1 none); `both` = find_fil_gt's param_8; style = set_fil_small_sweep */
/* gp_ints (mccore_inters.c:512) with its tol tests (l_ints, la_ints: tangent within 2 r tol, aa_ints): x = first hit (nearer the seed), second hit; 0 = no intersection. intersect_ents' carrier hits for Trim, Trim Many and Line Bisect */
int mcf_gp_ints(const McfG *a, const McfG *b, double sx, double sy, double x[4], double tol);
int mcf_find_fil(const McfG g[2], double R, const double pick[2], int style, int both, double tol, double min_arc, McfArc *o, int maxc, int *sel, int use_dir, int fil_dir);   /* use_dir / fil_dir: DAT_180255602 / DAT_180244634 (set_use_fil_dir / set_fil_dir, Fillet Chains' corner direction) */
void mcf_set_lollipop(int on, double clr);   /* the Clearance style: set_lollipop_corner / set_clearance_offset */
void mcf_arc_of(double cx, double cy, double R, const double p0[2], const double p1[2], int style, McfArc *o);   /* FUN_18005c600 */
#include "mcam_load.h"
typedef struct { int kind; /* 0 line, 1 arc, 2 spline */ double ax, ay, az, bx, by, bz; double cx, cy, r, a0, sw; const CurveRec *c; double t1, t2; int closed; } McfEnt;
void mcf_ent_eval(const McfEnt *e, double t, double p[2], double d[3]);
int mcf_find_fil_ent(const McfEnt E[2], double R, double tol, const double seed[2], int style, double t[2], McfArc *arc);   /* find_fil_ent, viewer/mcfillet_ent.c */
#endif
