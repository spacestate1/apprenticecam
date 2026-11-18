/* verify - stock-removal simulation for the 2D mill ops: a dexel (heightfield) model of the stock, replayed
 * against the NcMotion list (the same motions Backplot plays, nc_post.h). Mastercam's own Verify is ModuleWorks
 * (mw*.dll / machsim.dll): there is no Mastercam code to port, so THE ENGINE IS OURS (like geo2d.c): the grid,
 * the sweep and the meshing are our design; the inputs (motions, tool profiles, stock box) are the decoded ones.
 * The window around it is the Mastercam Simulator copy (../formats/simulator.md, core_ui/cui_verify.c, viewer-qt
 * simwin.cpp); the ModuleWorks precision / resolution is NOT DECODED. */
#ifndef VERIFY_H
#define VERIFY_H
#include "mcam_load.h"
#include "nc_post.h"

typedef struct {
    int nx, ny;             /* cells */
    double x0, y0, cell;    /* grid origin (corner of cell 0,0) and pitch */
    double ztop, zbot;      /* stock Z range (ztop = the initial surface everywhere) */
    double *z;              /* nx*ny top-of-material per cell */
    double xmin, ymin, xmax, ymax;   /* stock box (the grid covers it exactly) */
    /* rapid-collision warnings: a rapid move that removed material (positions, capped) */
    double (*warn)[3]; int nwarn, capwarn;
    /* the cut colour key per cell (OURS, the stand-in for ModuleWorks' per-face cut data that its WorkpieceDrawMode 12 / 3 / 2
       colours, formats/simulator.md 9): -1 = never cut (drawn in StockColor), else KEY as it was when a cut last lowered it */
    int *cut; int key;
} VF;

/* tool cutting profile: the tool's lower surface as zsurf(r) = height above the tip at radius r,
   NS samples over [0, rmax]; built from the decoded parametric profile (toolprofile.c tp_polyline) */
#define VF_NS 96
typedef struct { double rmax, zsurf[VF_NS]; } VfTool;

VF *vf_new(double xmin, double ymin, double xmax, double ymax, double ztop, double zbot, double cell);
void vf_free(VF *v);
/* the profile of a part tool; falls back to the bare cylinder of Tool.dia when tp_build fails (ours).
   Returns 0 when the tool has no diameter. */
int vf_tool(const Tool *tl, VfTool *out);
/* cut one motion with TOOL. Rapid motions cut too (a rapid through stock is a real cut) and add a warning.
   Arcs (NCM_CW/CCW in the XY plane) are subdivided to chord <= cell/2 (ours). Returns 1 if material was removed.
   Every cell a cut lowers takes v->key (the caller's: the verified operation's index, formats/simulator.md 9). */
int vf_cut(VF *v, const VfTool *tool, const NcMotion *m);
void vf_reset(VF *v);                           /* back to the uncut stock: z = ztop, no cut keys, no warnings */
double vf_volume_removed(const VF *v);          /* vs the uncut stock, cubic units */
/* mesh of the current surface: *out = malloc'd x y z nx ny nz r g b per vertex, 3 per tri; CELLRGB (nx * ny * 3, or
   NULL) colours each cell, else RGB (NULL = white) colours all; returns vertices. */
int vf_mesh(const VF *v, const float *cellrgb, const float rgb[3], float **out);
/* as vf_mesh; WALL_LOW: an inner wall takes the colour of the cell on its LOW side (the cell whose cut exposed it: the
   cut-colour display) instead of the high side (the Compare display); SIDE_RGB (or NULL = the cell's colour): the colour of
   the stock box's outer side walls (in the cut-colour display they are the uncut stock's sides). Ours. */
int vf_mesh_ex(const VF *v, const float *cellrgb, const float rgb[3], int wall_low, const float *side_rgb, float **out);
/* the target's top Z per cell (TRI: 9 doubles per triangle; -1e300 = not covered), malloc'd; ours */
double *vf_target_top(const VF *v, const double *tri, int ntri);
/* op-boundary snapshots for backward seek */
typedef struct { double *z; int *cut; } VfSnap;
void vf_snap(const VF *v, VfSnap *s);
void vf_restore(VF *v, const VfSnap *s);
void vf_snap_free(VfSnap *s);

/* the stock box for the selected ops: job_setup_stock_corners (mccore.dll 0x1805e4dc0, decoded 2026-10-20):
   box min = origin + OFF, max = min + size, with OFF from the corner selector (group +0x1690). The bottom is
   extended past through-cuts (drill tips below the plate; OURS - the mesh needs a floor). When the sizes are 0
   the box is the motions' bounds grown by the largest tool radius; the top is ZTOP_HINT (the selected ops'
   highest top of stock) when given, else the highest feed-motion point (OURS - Mastercam's no-stock fallback
   is NOT DECODED). Returns 0 when there are no motions. */
int vf_stock_box(const McamFile *f, const NcMotion *m, int n, double max_tool_r, double ztop_hint, double box[6]);
#endif
