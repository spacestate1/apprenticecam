/* lathe_stock.h - Mastercam 2017 lathe stock boundaries (mcmachinedef.dll BOUNDARYCLASS, mclathe.dll LATHEPROJECTCLASS) for the
 * Lathe Face "from stock" geometry. Spec: ../formats/lathe.md section 7.2 / 7.3 (every rule cites its function). */
#ifndef LATHE_STOCK_H
#define LATHE_STOCK_H
#include <stddef.h>
#include <stdint.h>

/* one boundary entity (tp_ent, 0x78 bytes: +0 i32 type 2 line / 4 arc, +0x10 p_3d start, +0x28 p_3d end, arcs: +0x40 centre,
   +0x58 radius, +0x60 start angle, +0x68 sweep, rad) in the boundary's 2D frame: x = lathe Z, y = lathe X (radius) */
typedef struct { int type; double p1[2], p2[2], c[2], r, sa, sw; } LsEnt;
/* a boundary polygon (BOUNDARYCLASS polygon: entities + the extents SetExtents keeps at +0x30 .. +0x48) */
typedef struct { LsEnt *e; int n; double minx, miny, maxx, maxy; } LsBdry;

/* GetFrom_lbdry (mcmachinedef 0x1800346d0) + Scale(k, k + 0x1b, 1 / MathScale) (0x18003b060, ScaleBdryEnt 0x180046c20; MathScale
   = 1000, InitTolerances 0x180037620): the lathe_bdry record (LBDRY_ID ent +0x68, `n` bytes) as boundary polygon 0x1b. 0 = refused */
int ls_from_lbdry(const uint8_t *rec, uint32_t n, double tol, LsBdry *b, char *err, size_t errn);
void ls_free(LsBdry *b);
/* BOUNDARYCLASS::YStatus 0x18003d790: 1 all of the boundary at y >= 0 (beyond tol), -1 all below, 0 both */
int ls_ystatus(const LsBdry *b, double tol);
/* BOUNDARYCLASS::GetMaxY 0x180034fb0 / GetMinY 0x180035db0 (raw: last argument 1): the greatest (least) y below (above) LIM of the
   boundary on the vertical lines x = X1, X2 (one line when |X1 - X2| <= tol) and of its entities' max (min) y points with x in
   [X1, X2]. 1 = found */
int ls_get_max_y(const LsBdry *b, double x1, double x2, double lim, double tol, double *out);
int ls_get_min_y(const LsBdry *b, double x1, double x2, double lim, double tol, double *out);

#ifdef LATHE_STOCK_MCAM
#include "mcam_load.h"
/* LATHEPROJECTCLASS::GetPrvOpBdrys 0x18010dc40 (lathe_toolpaths_start 0x18019fe10 for a new op) -> GetPrvOpBdrysOpIdn 0x18010deb0
   -> GetComponentsFromDb 0x18010d120: the left-spindle stock (component 0) the op OP_ID sees - that of the last op before it in
   the active lathe machine group that has lathe boundaries (op +0xb40 / +0xb44), else the group's own (+0x16ac). OP_ID 0 = a new op
   appended to the group. *src = the op id it came from (0x7fffffff = the group). 0 = refused (err) */
int lathe_op_stock(const McamFile *f, int op_id, double tol, LsBdry *b, int *src, char *err, size_t errn);
#endif
#endif
