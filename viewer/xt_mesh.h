/* xt_mesh - read Parasolid binary transmit data (as embedded in Mastercam solids) and tessellate it.
 * The node layout comes from a Parasolid schema file (sch_<key>.s_t, shipped with Mastercam in PSLDDATA);
 * see ../tools/xt_parse.py for the format notes. Supported faces: plane, cylinder (lines/circles as edges). */
#ifndef XT_MESH_H
#define XT_MESH_H
#include <stddef.h>
#include <stdint.h>
#include "btopo.h"

typedef struct XtSchema XtSchema;

XtSchema *xt_schema_load(const char *path);          /* NULL on failure */
void xt_schema_free(XtSchema *s);
XtSchema *xt_schema_for(const uint8_t *xb, size_t n);   /* schema named in the blob's header, searched in known folders (cached) */
XtSchema *xt_schema_find(const uint8_t *xb, size_t n, char *path_out, size_t path_n);   /* the same + the schema file's path ("" if none) */

typedef struct {
    float *tri;          /* 3 vertices per triangle, each: x y z nx ny nz (metres, model space) */
    int ntri;
    int nfaces, nfaces_meshed, nnodes;
    char error[128];     /* non-empty if parsing failed */
} XtMesh;

int xt_mesh(const XtSchema *s, const uint8_t *xb, size_t n, XtMesh *out);   /* 1 = ok */
void xt_mesh_free(XtMesh *m);
/* the body's FACE nodes in XT node index order (= xt_mesh's face order, = xt_occt's face_of keys): returns the count (-1 when the
   blob does not parse: err gets the reason, 128 bytes); *node_idx (may be NULL) = malloc'd node indices, the caller frees it */
int xt_faces(const XtSchema *s, const uint8_t *xb, size_t n, int **node_idx, char *err);

typedef struct {     /* one edge of a loop, mm: type 2 line a->b, 4 arc (XY plane: centre c, radius r, start angle a0, sweep sw) */
    int type; double a[3], b[3], c[3], r, a0, sw;
} XtEdge;
int xt_cylinders(const XtSchema *s, const uint8_t *xb, size_t n, double *xyr, int max);   /* vertical cylinders x y r (mm) */
int xt_top_loop(const XtSchema *s, const uint8_t *xb, size_t n, XtEdge **out);   /* edge count; *out malloc'd */
int xt_top_face_loops(const XtSchema *s, const uint8_t *xb, size_t n, XtEdge **out, int *loop_n, int max_loops,
                      int *nloops, int *outer);   /* every loop of that face (xt_mesh.c) */

/* the B-rep edges of a body for the AutoCursor (core_ui cui_snap.c; formats/xt.md "Edges"): every EDGE node whose
   curve is a LINE or a CIRCLE (other curve classes, circles with sense '-' and edges with damaged fins are left out
   and counted in *nskip). Coordinates x scale (the part's model units). pts: a line's two vertices; a circle's
   points every <= 5 degrees from its start (a ring edge: from x_axis round, first point repeated at the end).
   arc: 0 line, 1 circle arc between two vertices, 2 ring edge (no vertex); c / ax / xa / r = the CIRCLE (centre,
   normal, x_axis, radius), t0 .. t1 the arc's angles about ax from xa (t0 in [0, 2 pi); ring 0 .. 2 pi). */
typedef struct { double *pts; int np, arc; double c[3], ax[3], xa[3], r, t0, t1; } XtBEdge;
int xt_brep_edges(const XtSchema *s, const uint8_t *xb, size_t n, double scale, XtBEdge **out, int *nskip);
void xt_bedges_free(XtBEdge *e, int n);
/* the body's faces / loops / fins over xt_brep_edges' edge numbers (btopo.h, solid chaining); returns the face count */
int xt_brep_topo(const XtSchema *s, const uint8_t *xb, size_t n, double scale, BTopo *t);

#endif
