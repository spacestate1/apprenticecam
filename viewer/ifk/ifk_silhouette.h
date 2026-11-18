/* ifk_silhouette.h - the Silhouette Boundary kernel end to end: port of Mastercam 2017's interfacial11.dll
 * Silhouette::Run (18001f1c0) with the parameter blob IFSilhouette::Run (ifsilhouette 1800264f0) packs for Mastercam
 * (write 1, binary 1, tol, mode 1, min 0, max 90, route 0; formats/interfacial.md 11.1):
 *   input stage (ifk_silhouette_in.c, 11.6) -> PlaneCut with PlaneCutToSilhouette (ifk_planecut.c, ifk_sil_classify.c,
 *   11.7 / 11.8) -> the post-PlaneCut Stitch2D loop (FUN_180011fb0 / FUN_180012340 / FUN_180012e00, union-find merge
 *   into the second tree) -> FUN_180025000 (zero-width outer / hole pairs, FUN_180024600) -> FUN_180024da0 through
 *   Surfer (interfacial01 NewWriteCurves / AddCurveTreeNode / AddCurveTreeBranch / AddWriteCurveNode /
 *   AddWriteCurveEdge / EndWriteCurves) -> Surfer::WriteCurves binary (FUN_180017b20, FUN_180019490, FUN_18001a430,
 *   FUN_180019880), written plain (IFSilhouette's FUN_180026a40 undoes the byte table, 11.4).
 * Write-up: formats/interfacial.md 11.9. Not checked against Mastercam (decision N11-5). */
#ifndef IFK_SILHOUETTE_H
#define IFK_SILHOUETTE_H

#include "ifk_spinsil.h"   /* IfkMesh */

#ifdef __cplusplus
extern "C" {
#endif

#define IFK_SIL_OK          0
#define IFK_SIL_TOLCHECK  (-1)  /* |tol - 0.1234567| <= 1e-6: "May 10 1954 and July 27 1976.", Run exits (l.24232) */
#define IFK_SIL_OVERFLOW  (-2)  /* "IFSilhouette error 2: overflow." (dialog slot +0x40 with 1) */
#define IFK_SIL_NOTPORTED (-3)  /* ifk_pc_run returned -1 (a PlaneCut case this port does not handle) */
#define IFK_SIL_ERR62     (-4)  /* "Surfer error 62: conflicting tree groups." (dialog +0x40 with 1); the file is
                                   closed where the writer stopped (what the DLL does after the message: NOT DECODED) */
#define IFK_SIL_OPEN      (-5)  /* "Surfer error 25: I could not open the write file." */

typedef struct {
    /* the second PlaneCutToSilhouette tree (Run local_3b8) after FUN_180025000; node 0 = its root (+0x18) */
    int nnode;
    int *parent;        /* parent node, -1 for the root, -2 for a node FUN_180025000 removed */
    int npt;
    double *pt;         /* 2 x npt: x, y (FUN_180028290 points, creation order) */
    int nedge;
    int *edge;          /* 3 x nedge: a, b (points), tree node; creation order */

    /* the Surfer write model = the file content (plain) */
    int fnode;          /* Surfer +0x18: nodes written, in creation order */
    double *fxyz;       /* 3 x fnode */
    int fedge;          /* Surfer +0x1c */
    int ncurve;
    int *curve_start;   /* ncurve + 1 offsets into cseg */
    int *cseg;          /* 2 per edge: the two 1-based node numbers as written (after the +0x11 reversal) */
    int *curve_tree;    /* 2 per curve: the tree node id and its parent's id (0 = none), as written */

    /* diagnostics */
    int nlevel;         /* Stitch2D runs (tree nodes with children visited by the breadth-first loop) */
    int nstitch_pt;     /* Stitch2D points made, all levels */
    int nmoved;         /* points whose representative (+0x18) is another point (FUN_180012a70) */
    int ndegenerate;    /* edges dropped by FUN_180012340 because both ends got the same representative */
    int nsplit;         /* edges split at representatives near them (FUN_180012ba0) */
    int nties;          /* equal projection keys among one edge's split points: their order is NOT DECODED */
    int ncancel;        /* opposite edge pairs flagged by FUN_180012e00 */
    int nunion;         /* tree nodes merged by the union-find */
    int nremoved;       /* node pairs removed by FUN_180025000 */
    int nwarn59, nwarn29;   /* Surfer warnings 59 (hanging node) / 29 (odd junction) while writing the nodes */
    int pc_nerr;        /* PlaneCut dialog messages (pc->nerr) */
    char msg[160];      /* first error / message text, "" if none */
} IfkSilResult;

/* Silhouette::Run on a mesh (Surfer::ReadFile's AddNode / AddTriangle order taken as the array order: NOT DECODED,
 * formats/interfacial.md 11.6). tol = the blob tolerance (tpiftools tol x scale). out_path may be NULL (no file:
 * the result only). res may be NULL. Returns IFK_SIL_OK or an IFK_SIL_* code. */
int ifk_silhouette_run(const IfkMesh *in, double tol, const char *out_path, IfkSilResult *res);
void ifk_sil_result_free(IfkSilResult *r);

#ifdef __cplusplus
}
#endif
#endif
