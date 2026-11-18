/* lathe_face.h - Mastercam 2017 Lathe Face (opcode 66, mclathe.dll CLFaceTp) regenerated from its operation record.
 * Spec: ../formats/lathe.md section 6 (every rule cites its mclathe / mccore function). Pure: no McamFile, no globals. */
#ifndef LATHE_FACE_H
#define LATHE_FACE_H
#include <stddef.h>
#include <stdint.h>
#include "lathe_stock.h"

/* one NCI linear move of the op: lathe X (NCI x, radius as the 2D contour's y) and Z (NCI z); rapid 1 = G0, else G1 at feed
   (the NCI feed: negative = per revolution, as Mastercam's NCI stores it); ctf = the contour flags OutputOnePass passes
   (1 the pass's 2nd record, 2 its last) */
typedef struct { int rapid; double x, z, feed; int ctf; } LfMove;

typedef struct {
    const uint8_t *rec; uint32_t rn;   /* the OP_ID record (ent: op = rec + 0x68), 0x2938 bytes (2017 layout) */
    const double (*pts)[3]; int npts;  /* the op's point associations (TP kind 1, tp_get_all order): world xyz */
    double tol;                        /* the system tolerance (config 1000, tol_exref) */
    const LsBdry *stock;               /* the op's stock polygon 0x1b (lathe_stock.c lathe_op_stock: the left-spindle stock the op sees);
                                          NULL = none (GetOpStockBoundary 0xff) */
    int dialog_ok; double finish_x;    /* 1: the Face page's OK (CLFaceProp::OnOK 0x180042c40) made the face points first: geom +0x88 =
                                          FINISH_X (latheZ_to_wcsX(op +0x13b8)), GetFaceBoundaryFromStock(op, PointsDefined, 0); 0: bnci
                                          only (GetAllGeometry: SetMaxMinPtsFromOp, GetFaceBoundaryFromStock(op, 1, 1)) */
    double (*made)[3]; int *nmade;     /* optional out: the two face points GetFaceBoundaryFromStock made (world), *nmade = 2; 0 = the op's own */
} LfIn;

/* 1 = moves made (*out malloc'd, *nout), 0 = refused / failed with err filled (never a stand-in toolpath) */
int lathe_face_gen(const LfIn *in, LfMove **out, int *nout, char *err, size_t errn);

/* CLFaceProp::InitZFinEd 0x180041d90 (the Face page opening): the Finish Z the edit shows (*z, lathe Z) and its stock range
   [*lo, *hi] (the MCReal's limits are those +- tol). 0 = no stock (the "Use stock" radio is greyed) */
int lathe_face_z_init(const LfIn *in, double *z, double *lo, double *hi);

#endif
