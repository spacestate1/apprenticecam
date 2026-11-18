/* mcbiarc.h - break_spline_arcs, see mcbiarc.c */
#ifndef MCBIARC_H
#define MCBIARC_H
#include "mcsurf.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int kind; /* 1 arc (centre, radius, start, sweep, radians, sweep > 0), 0 line */ double cx, cy, r, sa, sw, x1, y1, x2, y2; } McbPiece;
/* the 2D pieces of a spline lying in a plane parallel to Top (arcs in store order, as break_spline_arcs stores them); returns the count (may exceed cap: nothing written past it) */
int mcb_break_spline_arcs(const CurveRec *c, double err, double tol, McbPiece *out, int cap);
#ifdef __cplusplus
}
#endif
#endif
