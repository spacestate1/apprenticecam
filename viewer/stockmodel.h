/* Stored Stock Model operations (opcode 139 = 0x8b): reader only (formats/stock_model.md).
 * Generation is not ported: Mastercam computes the model with the third-party Interfacial sweep engine (ifsweep.dll through
 * mckernel TPStockModel::Compute / tpiftools ComputeMCSweep), formats/stock_model.md 3. */
#ifndef STOCKMODEL_H
#define STOCKMODEL_H
#include <stdio.h>
#include "mcam_load.h"

typedef struct {
    int op_id;
    char name[64];
    int shape;                  /* Initial stock shape, record +0x1b88: stockmodel_shape_name */
    int anchor;                 /* origin button of the shape, record +0x2040 */
    double size[3];             /* record +0x1230 / +0x1238 / +0x1240: X, Y, Z */
    double origin[3];           /* record +0x1248 / +0x1250 / +0x1258: X, Y, Z */
    double shape_tol;           /* Initial stock shape tol., record +0x1270 */
    double add_offset;          /* Additional offset, record +0x1278 */
    int view;                   /* Stock plane view number, record +0x7a0 */
    double arc_tol, path_tol, tool_tol, compress_tol;   /* record +0x12d0 / +0x12d8 / +0x12e0 / +0x12e8 */
    int five_ax_buffer;         /* record +0x1ba0: 0 Automatic, 1 Low, 2 Medium, 3 High */
    int five_ax_tip;            /* 5-axis tip only, record +0x229e */
    int color;                  /* record +0x2298 */
    int nsources;               /* TP references of kind 0x17 */
    int sources[64];            /* their +0x98 = source op id */
    int source_flag[64];        /* their +0x9c: 1 = the initial stock operation (FindAssocInitialOpId) */
} StockModelOp;

const char *stockmodel_shape_name(int shape);                         /* the record's shape value -> the dialog's radio text */
int stockmodel_read(const McamFile *f, const Op *o, StockModelOp *sm);   /* 1 = ok; 0 = not a Stock Model op or record too short */
void stockmodel_print(FILE *out, const StockModelOp *sm);
#endif
