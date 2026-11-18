/* mcpartline.h - Wireframe > Curve Parting Line's kernel: mccore parting_line (0x180566740) and its callees, formats/wireframe.md "Curve Parting Line".
 *   mcpartline_mesh.c: make_surf_polygons (0x1806fbda0) as FUN_18054be80 (0x18054be80) calls it: the surface's triangles
 *   mcpartline.c: FUN_180566220 (the points where the normal meets the angle), FUN_180564b70 (the boundary), FUN_180551e00 (the chains),
 *                 FUN_180534ce0 (the seams), FUN_180565bc0 / msf_create_spline (the curves), FUN_180538d30 / FUN_18054d250 (to NURBS / line / arc) */
#ifndef MCPARTLINE_H
#define MCPARTLINE_H
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif
/* the triangles of make_surf_polygons(1, 1, ent, step, 0, 0, ..): ntri triangles (3 vertex indices each), the vertices' xyz and (u, v) */
typedef struct { int ntri, nv; int *tri; double *xyz, *uv; } PlMesh;
/* FUN_18054be80(1, ent, step, ..) -> make_surf_polygons: returns ntri (0: none), -1 not ported for this surface (why says what) */
int plm_mesh(const McamFile *f, const Surf *s, double step, PlMesh *m, const char **why);
void plm_free(PlMesh *m);
/* the nodes on the parting line (CSplineNodeMgr's spline nodes 1 / 2: xyz, uv) in chains: cnt[c] nodes each, in order; bpts = the boundary nodes */
typedef struct { int np, np0, nb, nch; double *P, *UV; short bpts[128]; short cnt[4096]; char cancel; } PlPts;   /* np0: the count before the chains (FUN_180566220 *param_8) */
int plp_alloc(PlPts *pp); void plp_free(PlPts *pp);
/* CSplineNodeMgr's spline nodes 1, the session buffer (CS-5): csn_nodes1(n) = at least n records (AllocateMemNPts), csn_reset = AllocateMem */
double (*csn_nodes1(int n))[3];
void csn_reset(void);
/* FUN_180566220 (with FUN_180564b70, FUN_180551e00, FUN_180534ce0) on the mesh: V = the Cplane's Z, ang = the Angle in radians, chord = surf_step_chord_dev,
   tol = the system tolerance. 0 ok, -1 too many nodes */
int plp_points(const McamFile *f, const Surf *s, const PlMesh *m, const double V[3], double ang, double chord, double tol, int metric, PlPts *pp);
/* one chain's curve: kind 2 a line (line[6]), 8 a parametric spline record (rec / rn, malloc), 0x80 a NURBS record, 4 an arc (simp); listed = FUN_180565bc0
   keeps it in the command's list (a 2-node line is stored but not listed: msf_create_spline leaves its ok flag 0) */
#include "mcspline_simplify.h"
typedef struct { int kind, listed; double line[6]; uint8_t *rec; uint32_t rn; McsSimplify simp; } PlCurve;
/* msf_create_spline (0x18054db20) on n nodes (xyz): chord_mode = surf_use_chord_dev (the panel's Chord height), chord = surf_step_chord_dev. 1 made, 0 none */
int plc_curve(const double *Q, int n, int chord_mode, double chord, double tol, PlCurve *o);
/* FUN_180538d30 -> FUN_18054d250 (chord mode only): a spline that convert_ent_to_line_or_arc (mccadutil) takes at surf_step_chord_dev becomes the line / arc,
   else with nurbs (msf_nurbs_splines = cfg_createNurbs) the NURBS (convert_entity_to_nurbs). Returns 1 when o changed */
int plc_convert(PlCurve *o, double chord, double tol, int nurbs, const McsSimpView *views, int nviews);
void plc_free(PlCurve *o);
/* build_edgecrv_on_surf (mccore 0x180273970) untrimmed branch, as chaining's surface-edge pick runs it (chain_single FUN_1801df0b0 -> FUN_1801e1a50):
   the boundary nearest (u, v) through make_constant_spline (chord height surf_step_chord_dev) and FUN_18054d250. o: kind 1 point, 2 line, 4 arc
   (simp), 8 spline / 0x80 NURBS (rec). 1 made, 0 none, -1 not ported (why) */
int ec_build_edgecrv(const McamFile *f, const Surf *s, double u, double v, double chord, double tol, int nurbs, const McsSimpView *views, int nviews, PlCurve *o, const char **why);
/* build_edgecrv_on_surf(ent, (u, v), chord, ang_deg (chain_section_stop_angle in degrees), 0, list), every surface kind: the curves made, in the
   list's order (o[0] = list[0]: the one chain_single chains); trimmed surfaces (kind 10): the trim-loop walk (several curves when the section stop
   angle breaks it inside an iso run or at 4000 nodes). Returns the count (0 none), -1 not ported (why) */
int ec_build_edgecrv_list(const McamFile *f, const Surf *s, double u, double v, double chord, double ang_deg, double tol, int nurbs, const McsSimpView *views, int nviews, PlCurve *o, int maxo, const char **why);
/* mcpartline_mesh.c: bit-exact kind 0 (untrimmed NURBS) ports of eval_surf (0x18047ba50; order 0 FUN_1805b08d0, order 1..3 FUN_1805b1f10 with
   param_6 = 0) and calc_surf_unit_norm (0x180164290 -> calc_surf_norm 0x1801637e0, param_7 = 0). D[] as mcsn_eval: S, Su, Sv, Suu, Suv, Svv,
   Suuu, Suuv, Suvv, Svvv. Returns: plm_eval 0 ok, -1 not kind 0 / bad record / eval_surf's -1 (+6 bit 0x40 clear);
   plm_unit_norm 0 ok, 1 no normal (N = 0), as mcsn_unit_norm; -1 not kind 0 */
int plm_eval(const McamFile *f, const Surf *s, int order, double u, double v, double D[][3]);
/* make_surf_model (mccore 0x18058dc30; formats/surf_classic3.md 12.9): the machine model's grid of one surface as mcmsurf FUN_180229ba0 asks for
   it (l.364700): tol (in_stack_88 = 90), d / h the normal / horizontal tool offsets, c / d2 / h2 the second shape (only c = 0 ported), r
   (in_stack_68, 1 for op 43), the raster frame f1 f2 f3 (rows, rot_view). Out: the u / v parameters (nu + 1 / nv + 1, the tmpmm1 grid), mode =
   mms +0x64 (local_2cd0: 0 / 2 one side, 1 both), the frame box of the last vertex pass (mms +8..+0x30: xmin xmax ymin ymax zmin zmax), ntri =
   mms +0x50. 0 ok, -1 not ported (why). With edges = 1 (in_stack_d0 = 3, as FUN_180229ba0 asks it) also the vertices of the last pass
   (pts / nrm: the grid points u outer v inner, then the loop points; through the frame) and the boundary edge records FUN_18059c040 /
   FUN_18059c520 write to tmpedg0 (their count = mms +0x54), surf_classic3.md 12.11. */
typedef struct { double tol, d, h, d2, h2; int c, r; double frame[9]; int edges, verts; } PlModelIn;   /* verts: the vertex arrays (pts / nrm)
   without the edge records */
/* one tmpedg0 record (0xb0 bytes, FUN_18059c520): +0 P1, +0x18 i1, +0x20 P2, +0x38 i2, +0x40 the plane normal, +0x58 / +0x60 / +0x68 / +0x70 the
   four parameters FUN_18059c040 was given (its param_2 / 3 / 4 / 5), +0x78 N1, +0x90 N2, +0xa8 the facing flag (+0x1c, +0x3c, +0xa9..+0xaf: the
   DLL's stack bytes, written as 0 by plm_edge_bytes) */
typedef struct { double P1[3]; int i1; double P2[3]; int i2; double pl[3]; double prm[4]; double N1[3], N2[3]; int flag; } PlEdge;
typedef struct { int nu, nv, mode, ntri, nl, nlp; double *u, *v, *lp; double box[6];
                 int npts; double *pts, *nrm; int nedge; PlEdge *edge; int *tb, outer; } PlModel;   /* nl / lp: the trim loops (untrimmed:
   the grid outline refined by FUN_18057dd40, kept when it gained points: one loop, table (0, nlp - 1), tb NULL; trimmed (kind 10): nl loops,
   tb their table (start, count) = FUN_18057a980's local_2d68, outer = mms +0x60 local_2d44), nlp (u, v) points = last start + last count + 1 */
void plm_edge_bytes(const PlEdge *e, unsigned char out[0xb0]);
/* fill_mach_model_buffer's triangles of the model as one block (grid indices, loop points after the grid): 0 ok, -1 not ported (why) */
int plm_model_tris(const PlModel *m, int **tri, int *ntri, const char **why);
int plm_model(const McamFile *f, const Surf *s, const PlModelIn *in, PlModel *m, const char **why);
void plm_model_free(PlModel *m);
/* check_for_ridge(ent, deg) (mccore 0x18047b940; surf_classic3.md 12.9): 1 a normal break above deg across an internal knot line of full
   multiplicity, 0 none, -1 not ported (kinds other than 0) */
int plm_ridge(const McamFile *f, const Surf *s, double deg_angle);
int plm_unit_norm(const McamFile *f, const Surf *s, const double Su[3], const double Sv[3], double u, double v, double N[3]);
/* calc_surf_unit_norm(h, ..) of a trimmed surface h over the base s (h's domain / size, s's derivatives) */
int plm_unit_norm_h(const McamFile *f, const Surf *h, const Surf *s, const double Su[3], const double Sv[3], double u, double v, double N[3]);
/* calc_surf_norm (0x1801637e0) on kinds 0 / 1, bit for bit: 0 ok, 1 no normal, -1 not ported */
int plm_norm(const McamFile *f, const Surf *s, const double Su[3], const double Sv[3], double u, double v, double N[3]);
#ifdef __cplusplus
}
#endif
#endif
