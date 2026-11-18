/* mcglhit.h - Mastercam's shading-on surface pick, gl_Hit_Surfs (mastercam.dll FUN_180224220, GuiOglPub.cpp; formats/chaining.md
 * "The shading-on pick (port)"): the cursor ray against the surfaces' shading triangles (the display-list cache FUN_180224100, built by
 * FUN_180136f70 = make_surf_polygons(1, 1, ent, gl_tolerance, 0, 0)), the hit nearest the viewer; its (u, v) by mccsurf.c cs_glhit_uv. */
#ifndef MCGLHIT_H
#define MCGLHIT_H
#include <stdint.h>
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif
/* a surface's node triangles as FUN_180136510 keeps them (no GAF): +0x88 the vertices as floats, +0x98 the indices (x 3 there: here the
   vertex numbers), +0x80 the triangle count; the first two indices swapped when ent +0x6e bit 4 (FUN_180136f70) */
typedef struct { int ntri, nv; int *tri; float *xyz; } GlhMesh;
/* FUN_180136f70 -> make_surf_polygons(1, 1, ent, gl_tol, 0, 0, ..) through viewer/mcpartline_mesh.c plm_mesh: 0 ok (ntri may be 0: no
   triangles, no node), -1 the mesh of this surface is not ported (*why) */
int glh_mesh(const McamFile *f, const Surf *s, double gl_tol, GlhMesh *m, const char **why);
void glh_free(GlhMesh *m);
/* the graphics view as gl_Hit_Surfs reads it: m = its matrix rows x, y, z (the view table entry for world_to_view / view_to_world, the
   Matrix33 at view +0x3e60 for the bounding circle and surf_screen_box - the same rows here); cur = the cursor in view x / y;
   ray = view_to_world of (x, y, 1) and (x, y, 0) (the l_3d lp_ints meets) */
typedef struct { double m[9], cur[2], ray[6]; } GlhView;
void glh_view(GlhView *v, const double m[9], const double cur[2]);
/* the per-surface tests before the triangles: the bounding circle (ent +0xc0 centre, +0xd8 radius) and surf_screen_box against the
   cursor (asm 180224603..18022470a); 1 = the triangles are tried */
int glh_candidate(const Surf *s, const GlhView *v);
/* the best hit so far (the capture block's slots 0x16 found, 0x17 the view z, 0x18 the hit point, 0x19 the eptr - here idx) */
typedef struct { int found; double z, hit[3]; int idx; } GlhBest;
/* FUN_180221770 on each triangle of m (the vertices' floats as doubles): the triangle's view box contains the cursor (FUN_180226780),
   the cursor inside it (FUN_1802267b0), lp_ints of the ray with its plane (normal (V1 - V0) x (V2 - V0) normalised by FUN_180130980;
   tol = mccore's tol), world_to_view; a hit with a larger view z (or the first) replaces the best (idx = the caller's surface) */
void glh_surface(const GlhMesh *m, const GlhView *v, double tol, int idx, GlhBest *b);
#ifdef __cplusplus
}
#endif
#endif
