/* occt_mesh.h - triangles of a part's solids for the 3D toolpaths (cam3d_rough.c), from the XT -> OpenCASCADE
 * translator viewer/occt/xt_occt.cpp + OCCT BRepMesh. Decision 3D-2 (formats/user_decisions.md, chosen by the
 * assistant pending user confirmation): the drive mesh is OCCT's, not viewer/xt_mesh.c's (formats/occt_accuracy.md 6:
 * the display mesher covers holes / misses faces). The tessellator is ours (Mastercam's own EvaSurfaceTessellationPlan is
 * not ported, formats/hst3d_port_plan.md 4.8); its deviation is bounded by the deflection passed here.
 * Built with OpenCASCADE when HAVE_OCCT is defined (viewer/Makefile and viewer-qt/CMakeLists.txt detect it); without
 * it every call fails with "built without OpenCASCADE". */
#ifndef OCCT_MESH_H
#define OCCT_MESH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* the XT blob xb (n bytes) -> triangles, 9 doubles each (x y z of the 3 vertices, outward winding), in model units
   (scale = model units per metre: 1000 mm, 1000 / 25.4 inch); deflection = the chordal deviation BRepMesh may leave
   (model units), angular deflection 0.5 rad. Returns the triangle count (>= 0) or -1 with err filled. *tri malloc'd. */
int occt_mesh_xt(const uint8_t *xb, size_t n, double scale, double deflection, double **tri, char *err, size_t errn);
int occt_mesh_available(void);
/* a surface entity's record (SURF_ID, class 0x100; kinds 0 NURBS and 10 trimmed on a NURBS base: base / base_n = the base
   surface's record, the one whose eptr is ent +0x100) -> triangles in model units, as occt_mesh_xt; with seg / nseg (may be
   NULL) also its boundary edges as segments (6 doubles each, malloc'd) */
typedef int (*occt_surf_eval_fn)(void *ctx, double u, double v, double p[3]);   /* 0 = ok */
/* ev / ectx: the positions of the (base) surface when it is not NURBS (mcsurf.c), meshed through a degree 1 B-spline at
   Mastercam's own parameters over its ent +0x70 range */
int occt_mesh_surf(const uint8_t *rec, size_t n, const uint8_t *base, size_t base_n, occt_surf_eval_fn ev, void *ectx, double deflection, double **tri, double **seg, int *nseg, char *err, size_t errn);
/* the XT's BODY.body_type (1 solid, 3 sheet), -1 when it cannot be read (or built without OpenCASCADE) */
int occt_xt_body_type(const uint8_t *xb, size_t n);
#ifdef __cplusplus
}
#endif
#endif
