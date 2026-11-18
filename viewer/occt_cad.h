/* occt_cad.h - STEP / IGES files read with OpenCASCADE (user decision: OCCT's readers instead of a port of Mastercam's converters; everything
 * around the parse follows Mastercam, viewer/cad_import.c, formats/cad_import.md) and the imported solids kept as OCCT BRep text blobs.
 * Without HAVE_OCCT every call fails with err = "built without OpenCASCADE". */
#ifndef OCCT_CAD_H
#define OCCT_CAD_H
#include <stddef.h>
#include <stdint.h>
#include "btopo.h"
#include "occt_prim.h"
#include "xt_mesh.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct OccCad OccCad;
enum { OCC_STEP = 1, OCC_IGES = 2 };
/* reads and transfers the file; scale = model units per mm of OCCT's (metric part 1, inch part 1 / 25.4). NULL with err on failure */
OccCad *occt_cad_read(const char *path, int kind, double scale, char *err, size_t errn);
void occt_cad_free(OccCad *c);
/* classification of the transferred shape */
int occt_cad_nsolids(const OccCad *c);           /* TopAbs_SOLID */
int occt_cad_nshells(const OccCad *c);           /* free shells */
int occt_cad_nfaces(const OccCad *c);            /* free faces (not in a shell) */
int occt_cad_nedges(const OccCad *c);            /* free edges (not in a face) */
int occt_cad_nverts(const OccCad *c);            /* free vertices (not in an edge) */
/* solid i (0-based) as a BRep text blob (malloc'd), n bytes; NULL on failure */
uint8_t *occt_cad_solid_brep(const OccCad *c, int i, size_t *n);
/* the faces of the solid i (solid = 1) or of the whole non-solid shape's faces (solid = 0): OpFace list, as occt_xt_faces */
int occt_cad_faces(const OccCad *c, int solid, OpFace **out, int *nout, char *err, size_t errn);
/* the edge curves: per edge the type (1 line, 2 arc, 3 NURBS) and the data. mode: 0 edges of solids (the file's edge curves option),
   1 free wireframe edges. Line: p[0..5]; arc: c[3], n[3], r, a0 sweep (radians) and xdir[3]; NURBS (deg, nk, k, nc, cp (w x y z per point), rat) */
typedef struct {
    int type;                 /* 1 line, 2 arc, 3 NURBS */
    double p[6];              /* start, end */
    double c[3], n[3], xd[3]; /* arc: centre, normal, x direction */
    double r, a0, sw;         /* arc: radius, start angle from xd about n, sweep > 0 */
    int deg, nk, nc, rat;     /* NURBS */
    double *k, *cp;           /* knots, control points (w, w x, w y, w z), homogeneous */
} OccCurve;
int occt_cad_curves(const OccCad *c, int mode, OccCurve **out, int *n, char *err, size_t errn);
/* the edges of a solid (occt_edges.cpp): one per XT EDGE / BRep edge: c (occt_cad_curves' data, a NURBS already in the edge's direction), along = the XT edge runs along its curve (line / arc: Mastercam's start and sweep from it, FUN_180043850), f = its faces (indices as occt_xt_faces / occt_brep_faces, -1 none), pts = the polyline for picking */
typedef struct { OccCurve c; int along, pk_along, xt_edge, f[2]; double *pts; int np; } OccEdge;   /* along: the edge runs along c (start = c.p[0..2]); pk_along: PK_EDGE_ask_geometry's sense (the edge against the stored curve: an arc's sign of sweep, FUN_180043850); c.n of an arc = the stored circle's axis */
int occt_xt_edges(const uint8_t *xb, size_t n, double scale, OccEdge **out, int *nout, int *nface, char *err, size_t errn);
int occt_brep_edge_list(const uint8_t *b, size_t n, OccEdge **out, int *nout, int *nface, char *err, size_t errn);
void occt_edges_free(OccEdge *e, int n);
/* surface_open_edges: the free edges of the sewn faces; a surface = its untrimmed NURBS net (cp: w, wx, wy, wz per point, nv rows of nu); trimmed surfaces are refused, decision N9-28 */
typedef struct { int nu, nv, du, dv, nku, nkv; const double *ku, *kv, *cp; } OccOpenSurf;
int occt_open_edges(const OccOpenSurf *surfs, int nsurf, double tol, OccEdge **out, int *nout, int *nsheet, char *err, size_t errn);
void occt_cad_curves_free(OccCurve *o, int n);
/* free points */
int occt_cad_points(const OccCad *c, double **xyz, int *n);

/* imported solids: operations on a BRep text blob */
/* triangles (3 vertices per triangle, each x y z nx ny nz, model units, outward), deflection in model units; returns triangle count or -1 */
int occt_brep_mesh(const uint8_t *b, size_t n, double deflection, float **tri, char *err, size_t errn);
/* the same, 9 doubles per triangle, for the 3D toolpaths' drive mesh (occt_mesh_xt) */
int occt_brep_drive(const uint8_t *b, size_t n, double deflection, double **tri, char *err, size_t errn);
int occt_brep_edges(const uint8_t *b, size_t n, XtBEdge **out, int *nskip);   /* lines and circles, as xt_brep_edges */
int occt_brep_topo(const uint8_t *b, size_t n, BTopo *t);                     /* over occt_brep_edges' numbers; returns the face count, 0 on failure */
int occt_brep_faces(const uint8_t *b, size_t n, OpFace **out, int *nout, char *err, size_t errn);
int occt_brep_body_type(const uint8_t *b, size_t n);                          /* 1 solid, 3 sheet (a solid made of open shells) */
#ifdef __cplusplus
}
#endif
#endif
