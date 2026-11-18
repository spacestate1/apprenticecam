/* ifk_sil_classify.h - Silhouette's test-point classifier: PlaneCutToSilhouette (interfacial11, vftable 18021beb8)
 * IO slots 7-10 = FUN_180024cd0 -> FUN_180022c00 (begin), FUN_180022cb0 (add a test point), FUN_180022da0 (build),
 * FUN_180024d70 -> FUN_180022e80(this, ?, 0) (classify) with FUN_180020c10 / FUN_180020d90 (point vs kept facet) and
 * the Shell2D side path (ifk_sil_shell2d.c) + FUN_180021830 for points on the boundary of >= 2 facets.
 * Write-up: formats/interfacial.md 11.8. */
#ifndef IFK_SIL_CLASSIFY_H
#define IFK_SIL_CLASSIFY_H

#include "ifk_planecut.h"
#include "ifk_silhouette_in.h"

typedef struct {
    const IfkSilWalls *w;              /* kept facets (SilhouetteData +0xa0) and their nodes */
    double grid;                       /* DAT_180268368 (2^-20) */
    int n, cap;                        /* test-point records (pool +0xe8) in slot-8 order */
    int *tp;                           /* [0]: the test point, -1 once covered */
    int **fac; int *nfac;              /* [1]: facets listed on it (push order) */
    double box[4];                     /* +0x40..+0x58: test-point box */
    int nshell;                        /* diagnostics: points that needed the Shell2D */
    int nshell_fail;                   /* Shell2D process results != 1 (see ifk_sil_shell2d.h) */
    int overflow;                      /* FUN_180026d90 overflow (TLS) */
    char err[96];                      /* first Shell2D error message */
} IfkSilClassify;

/* fills io with the four slots bound to c (c must outlive ifk_pc_run) */
void ifk_sil_classify_init(IfkSilClassify *c, IfkPcIO *io, const IfkSilWalls *w, double grid);
void ifk_sil_classify_free(IfkSilClassify *c);

#endif
