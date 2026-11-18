/* btopo.h - the face / loop / fin topology of a solid body over its B-rep edge list, for solid chaining (formats/
 * chaining.md "Solid chaining"). Mastercam walks Parasolid topology there (mcgeomsld sld_loop_to_chain: PK_LOOP_ask_fins,
 * PK_FIN_ask_edge, PK_FIN_is_positive; ExtractToolPChainInput: PK_FACE_ask_loops). Two sources fill it:
 *   - xt_mesh.c xt_brep_topo: the part file's Parasolid XT (FACE -> LOOP -> HALFEDGE (fin) -> EDGE), in XT order;
 *   - solid25.c body_brep_topo: the bodies made here (Z slabs, no B-rep): rebuilt from the slabs (ours).
 * Edge numbers are those of xt_brep_edges / body_brep_edges for the same body. Loops follow Parasolid's convention: the
 * fins run with the face on their left seen from outside the body (along the face's outward normal). */
#ifndef BTOPO_H
#define BTOPO_H
#include <stdlib.h>

typedef struct { int e, pos; } TFin;          /* edge number (-1 = an edge whose curve is not read: the loop cannot chain); pos = the fin runs along the edge */
typedef struct { int f0, nf, face; } TLoop;   /* fins f0 .. f0 + nf - 1 in loop order; its face */
typedef struct { int l0, nl; double nrm[3]; int has_nrm; float *tri; int ntri; } TFace;   /* loops; outward unit normal (planar faces and
                                                 walls parallel to Z here; has_nrm 0 = not known); triangles (9 floats each, model units) for picking */
typedef struct { TFace *face; int nface; TLoop *loop; int nloop; TFin *fin; int nfin; } BTopo;

static inline void btopo_free(BTopo *t) {
    if (!t) return;
    for (int i = 0; i < t->nface; i++) free(t->face[i].tri);
    free(t->face); free(t->loop); free(t->fin);
    t->face = 0; t->loop = 0; t->fin = 0; t->nface = t->nloop = t->nfin = 0;
}

#endif
