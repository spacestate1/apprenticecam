/* pgeo.h - exact 2D line / arc loops (Mastercam's pock_bound / gt model) for the CAM engines.
 *
 * A gt (mccore "gt", 0x38 bytes inside each 0x88-byte pock_bound entity, decomp/mcmill_all.c): type +0 'L' / 'A' / 'P',
 * line +0x10 start, +0x20 end; arc +0x10 centre, +0x20 radius, +0x28 start angle, +0x30 sweep (signed, radians)
 * (FUN_1801ce540 l.319158, find_helix_center FUN_1801c04d0 l.310452). A pock_bound is a closed list of gts with an
 * island flag (+2) and a direction flag (+3).
 *
 * The offset (gls_offset) is a SUBSTITUTE for mccore ccomp_pock / offset_pock_bounds / mcce MCCECalcOffset (their
 * internals are not decoded): the exact offset region of line / arc loops (round joins = the disc sweep), computed by
 * Clipper on finely sampled loops for the topology, then every resulting vertex is assigned to the exact offset element
 * it lies on (offset line, concentric arc, corner arc about an input vertex) and the loop is rebuilt from those
 * elements with exact intersections. Corners whose turn is below min_fil (ccomp's minimum fillet angle,
 * set_min_fil_angle) are extended to a sharp corner instead of rolled.
 */
#ifndef PGEO_H
#define PGEO_H
#include "offset.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { double x, y; } PV;
typedef struct { char t; PV a, b; PV c; double r, sa, sw; } Gt;   /* 'L' a -> b; 'A' c r sa sw; 'P' a */
typedef struct { Gt *e; int n, cap; int island; int tag; } Gl;     /* closed loop (end of e[n-1] = start of e[0]) */
typedef struct { Gl *l; int n, cap; } Gls;

static inline PV pv(double x, double y) { PV r = { x, y }; return r; }
Gt gt_line(PV a, PV b);
Gt gt_arc(PV c, double r, double sa, double sw);
PV gt_pt(const Gt *e, double t);           /* eval_gt_pt: t in [0, 1] */
PV gt_tan(const Gt *e, double t);          /* unit tangent in the direction of travel (eval_gt_tan) */
PV gt_start(const Gt *e);
PV gt_end(const Gt *e);
double gt_len(const Gt *e);                /* length_gt */
Gt gt_rev(const Gt *e);
Gt gt_sub(const Gt *e, double t0, double t1);
double gt_t_near(const Gt *e, PV q);       /* t_on_gt: parameter of the nearest point, clamped to [0, 1] */

void gl_push(Gl *l, Gt e);
void gl_free(Gl *l);
Gl gl_copy(const Gl *l);
double gl_area(const Gl *l);               /* signed, exact for arcs (> 0 counter-clockwise) */
double gl_len(const Gl *l);
void gl_reverse(Gl *l);
void gl_rotate(Gl *l, int i);              /* reorder_pock_bound: start at entity i */
void gl_split_start(Gl *l, int i, double t);   /* start at gt_pt(e[i], t) (the entity is split) */
void gl_bbox(const Gl *l, double bb[4]);
double gl_closest(const Gl *l, PV q, int *ei, double *t, PV *p);   /* distance to the loop, entity / parameter / point */
CPath gl_sample(const Gl *l, double tol);  /* polygon within tol of the loop (vertices on the loop) */

void gls_push(Gls *s, Gl l);               /* takes ownership */
void gls_free(Gls *s);
Gls gls_copy(const Gls *s);
CPaths gls_sample(const Gls *s, double tol);
int gls_inside(const Gls *s, PV q, double tol);   /* even-odd point in region */
/* offset of the region bounded by the loops (outer loops counter-clockwise, islands clockwise; the region is on the
   left of every loop): d > 0 grows it, d < 0 shrinks it. tol = the chord tolerance used for the topology. */
Gls gls_offset(const Gls *in, double d, double min_fil, double tol);
/* the non-island loops' union minus the island loops' union (ours, see pgeo.c) */
Gls gls_merge(const Gls *in, double min_fil, double tol);
Gls gls_from_cpaths(const CPaths *c);      /* polygons as line loops (island = clockwise) */
int gl_cross(const Gl *a, const Gl *b, double tol);   /* do the two loops cross or touch (sampled) */
int gt_cross(const Gt *a, const Gt *b, double tol);   /* transversal intersection of two entities (tangency excluded) */

#ifdef __cplusplus
}
#endif
#endif
