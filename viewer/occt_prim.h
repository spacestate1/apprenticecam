/* occt_prim.h - the solid of Surfaces > Simple (Block / Cylinder) built with OpenCASCADE and its faces as B-spline data (occt_prim.cpp).
 * Mastercam builds a Parasolid body (sld_basic_primitive: PK_BODY_create_solid_block / CRCYSO) and converts each face
 * (SolidBodiesToSurfaces -> ps_face_to_mc_surf: PK_FACE_output_surf_trimmed); here OCCT stands in for Parasolid (user decision S-1,
 * formats/user_decisions.md: same shape, the control nets / knots may differ). The C side (mcsurf_build.c mcsb_prim_faces) makes the
 * SURF_ID records from this data as ps_face_to_mc_surf does. */
#ifndef OCCT_PRIM_H
#define OCCT_PRIM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {            /* a trimming curve in the face's uv: create_uv_nurb_curve's input */
    int deg, nk, nc, rat;   /* knots k[nk] (full vector), nc points */
    double *k, *cp;         /* cp: rat ? (u, v, w) : (u, v) per point (create_uv_nurb_curve's param_7 layout) */
} OpUv;
typedef struct {
    int nku, nkv, ncu, ncv, udeg, vdeg, rat;
    double *ku, *kv, *cp;   /* cp: ncu * ncv points, u fastest: rat ? (w, w x, w y, w z) : (x, y, z) - the SURF_ID kind 0 tail */
    int reversed;           /* the face's sense is against its surface's normal (ps_face_to_mc_surf: the output's sense flag 0) */
    int nloops, *loop_ncrv; /* loops, the outer first; loop l has loop_ncrv[l] curves, in order, in crv */
    OpUv *crv;
} OpFace;
/* kind 0 block: d = length (along ref), width (along axis x ref), height (along axis); org = the centre of its bottom face
   (PK_BODY_create_solid_block). kind 1 cylinder: d = radius, height, start / end angle in degrees from ref about axis; org = the
   centre of its base (CRCYSO). kind 2 sphere: d = radius, start, end angle; org = its centre. kind 3 cone: d = base radius, top
   radius, height, start, end angle; org = the centre of its base. kind 4 torus: d = major, minor radius, start, end angle; org =
   its centre. The angles in degrees from ref about axis. The knots of each face are normalised to 0 .. 1 (measured: Parasolid's B-surfaces, 4711 of the 4781
   corpus bilinear planes) and the uv curves with them. 0 = ok; -1 with err (also: built without OCCT). */
int occt_prim_faces(int kind, const double org[3], const double axis[3], const double ref[3], const double d[6], OpFace **out, int *nout,
                    char *err, size_t errn);
void occt_prim_free(OpFace *f, int n);
void occt_prim_free_one(OpFace *f);    /* one face's arrays */
/* From Solid: the faces of a part solid (its XT blob, xt_occt) in XT FACE node order (= xt_brep_topo's face order); scale = model units
   per metre. Each face converted as occt_prim_faces' (BRepBuilderAPI_NurbsConvert of the body); 0 = ok, -1 with err */
int occt_xt_faces(const uint8_t *xb, size_t n, double scale, OpFace **out, int *nout, char *err, size_t errn);
/* the Plane Selection's solid face (sld_is_surf_planar + sld_check_face_normal, occt_mesh.cpp): planar class, the point / normal at the
   middle of the face's uv box; 0 = ok, -1 with err */
int occt_xt_face_plane(const uint8_t *xb, size_t n, double scale, int fi, int *planar, double P[3], double N[3], char *err, size_t errn);
#ifdef __cplusplus
}
#endif
#endif
