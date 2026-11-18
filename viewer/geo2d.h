/* geo2d - 2D wireframe construction math for the Wireframe tab (lines and arcs in a plane parallel to XY).
 * Own code: the behaviour follows Mastercam's sketcher / trim / fillet / offset commands (MCCAD.DLL, MCCADUTIL.DLL). */
#ifndef GEO2D_H
#define GEO2D_H

typedef struct { double x, y; } P2;
enum { GE_POINT = 1, GE_LINE = 2, GE_ARC = 4 };
typedef struct {
    int t;              /* GE_POINT / GE_LINE / GE_ARC */
    P2 a, b;            /* start / end */
    P2 c; double r, a0, sw;   /* arc: centre, radius, start angle, sweep (radians, > 0 = counter-clockwise) */
    double z;
} GE;

GE ge_point(P2 p, double z);
GE ge_line(P2 a, P2 b, double z);
GE ge_arc(P2 c, double r, double a0, double sw, double z);   /* any sweep sign: stored counter-clockwise */
int ge_is_circle(const GE *e);                                /* full 360 degree arc */
P2 ge_at(const GE *e, double t);                              /* t 0 = start, 1 = end; outside [0,1] = extension */
P2 ge_dir(const GE *e, double t);                             /* unit tangent in the direction of travel */
double ge_len(const GE *e);
/* parameter of the point of the entity's carrier (infinite line / full circle) nearest p; arcs: the nearer of the two
   ways round from the arc (so a point just before the start gives a small negative t) */
double ge_param(const GE *e, P2 p);
P2 ge_nearest(const GE *e, P2 p, int bounded);                /* nearest point (bounded: on the entity itself) */
double ge_dist(const GE *e, P2 p);                            /* distance to the entity itself */
GE ge_sub(const GE *e, double t0, double t1);                  /* the piece between two parameters (t0 < t1) */
/* intersections of the carriers (lines infinite, arcs full circles): up to 2; tangency counts once */
int ge_intersect(const GE *e1, const GE *e2, P2 out[2]);
int ge_on(const GE *e, P2 p, double tol);                      /* p lies on the entity itself */
int ge_offset(const GE *e, double d, GE *out);                 /* d > 0: to the left of the direction of travel */
/* point side of an entity: +1 left of a line / outside a circle, -1 otherwise */
int ge_side(const GE *e, P2 p);

/* fillet of radius r between e1 (picked at k1) and e2 (picked at k2): the arc and the trim parameters of e1 / e2
   (the tangent points). Returns 0 if there is none. */
int ge_fillet(const GE *e1, P2 k1, const GE *e2, P2 k2, double r, GE *arc, double *t1, double *t2);
/* circles of radius r tangent to two entities (centres, up to 8) */
int ge_circles_tan2(const GE *e1, const GE *e2, double r, P2 out[8]);
/* circles of radius r tangent to an entity and through a point */
int ge_circles_tan_pt(const GE *e, P2 p, double r, P2 out[4]);
/* tangent points on a circle for lines through p (0 if p is inside) */
int ge_tangent_points(P2 c, double r, P2 p, P2 out[2]);
/* shortest line between two entities (bounded): the two end points */
void ge_closest(const GE *e1, const GE *e2, P2 *pa, P2 *pb);

/* trim / extend e at parameter tx keeping the side of parameter tk: the result piece */
GE ge_trim_keep(const GE *e, double tx, double tk);
/* circle through 3 points (0 if collinear) */
int ge_circle3(P2 a, P2 b, P2 c, P2 *o, double *r);
/* arc from a through m to b */
int ge_arc3(P2 a, P2 m, P2 b, double z, GE *out);

#endif
