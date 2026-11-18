/* Stored Stock Model operations (opcode 139 = 0x8b): reader.
 * Sources (decomp/mcmill_all.c): the dialog pages keep a copy of the op at page +0x2ac[8] + 0x20f0, so
 *   record offset = page offset - 0x2088 (op = record - 0x68; verified on the stored field values, formats/stock_model.md 2);
 *   StockModelStockDefinitionPage MapToDlg (switch on +0x3c10, l.374954-375036) and the fields after it (l.375037-375152);
 *   StockModelSourceOperationsPage FUN_18021d770 (l.370850-370955) and its DDX (l.370546-370549);
 *   the TP references of kind 0x17: StockModelSourceOperationsPage::GetInvalidLatheOps (l.370653-370672) and
 *   FindAssocInitialOpId (l.373961-373990).
 * mccore CStockModel::BuildIffFileFromStockModelParameters (0x1806d53f0) reads the same fields through StockModel_Parameters
 * (the op's record + 0x1228): +0x08 / +0x10 / +0x18 size, +0x20 / +0x28 / +0x30 origin, +0x50 offset, +0x960 shape, +0xe18 anchor.
 * The model itself is NOT read or computed here: the only place it is stored is a TP reference of kind 44 (TP_STOCK_MODEL_RESULT,
 * GetStockModelFromOperation 0x1806da760), and no file in the corpus has one (formats/stock_model.md 1, 2). */
#include <stdint.h>
#include <string.h>
#include "stockmodel.h"

static double rdd(const uint8_t *r, uint32_t n, uint32_t o) { double v = 0; if (o + 8 <= n) memcpy(&v, r + o, 8); return v; }
static int rd32(const uint8_t *r, uint32_t n, uint32_t o) { int32_t v = 0; if (o + 4 <= n) memcpy(&v, r + o, 4); return v; }
static int rd16(const uint8_t *r, uint32_t n, uint32_t o) { int16_t v = 0; if (o + 2 <= n) memcpy(&v, r + o, 2); return v; }
static int rd8(const uint8_t *r, uint32_t n, uint32_t o) { return o < n ? r[o] : 0; }

const char *stockmodel_shape_name(int shape) {
    /* MapToDlg: the stored value -> the radio index of mcmillres dialog 25600, in its tab order: Model 518 / Rectangular 520 /
       Cylindrical 519 / Stock model 521 / File 522 / Lathe operation 523 */
    switch (shape) {
    case 0: return "Rectangular";
    case 1: return "Cylindrical";
    case 2: return "Model";
    case 3: return "File";
    case 4: return "Stock model";
    case 6: return "Model (spindle)";   /* same radio as 2; asserted when the op's +0x2238 flag is clear (l.375032) */
    case 7: return "Lathe operation";
    }
    return "?";
}

int stockmodel_read(const McamFile *f, const Op *o, StockModelOp *sm) {
    memset(sm, 0, sizeof *sm);
    if (o->opcode != 0x8b || !o->raw || o->raw_n < 0x2938) return 0;   /* ent + op = 0x2938 bytes (memset in MapToDlg l.374948) */
    const uint8_t *r = o->raw; uint32_t n = o->raw_n;
    sm->op_id = o->id;
    snprintf(sm->name, sizeof sm->name, "%s", o->comment);            /* Name 3583 = op +0x06 (l.375133; Op.comment = record +0x6e) */
    sm->shape = rd32(r, n, 0x1b88);                                   /* +0x3c10 */
    sm->anchor = rd16(r, n, 0x2040);                                  /* +0x40c8, MapToDlg l.375112 */
    for (int i = 0; i < 3; i++) {
        sm->size[i] = rdd(r, n, 0x1230 + 8u * i);                     /* +0x32b8.. = MCReal +0x5ba0 / +0x6998 / +0x7790 (ctrls 1219 / 1221 / 1223) */
        sm->origin[i] = rdd(r, n, 0x1248 + 8u * i);                   /* +0x32d0.. = MCReal +0x31b8 / +0x3fb0 / +0x4da8 (ctrls 1215 / 1230 / 1231) */
    }
    sm->shape_tol = rdd(r, n, 0x1270);                                /* +0x32f8 = ctrl 1216 "Initial stock shape tol." */
    sm->add_offset = rdd(r, n, 0x1278);                               /* +0x3300 = ctrl 1217 "Additional offset" */
    sm->view = rd16(r, n, 0x7a0);                                     /* +0x2828 = "Stock plane" */
    sm->arc_tol = rdd(r, n, 0x12d0);                                  /* +0x3358 = ctrl 3365 Path arc tolerance */
    sm->path_tol = rdd(r, n, 0x12d8);                                 /* +0x3360 = ctrl 3361 Path tolerance */
    sm->tool_tol = rdd(r, n, 0x12e0);                                 /* +0x3368 = ctrl 3359 Tool shape tolerance */
    sm->compress_tol = rdd(r, n, 0x12e8);                             /* +0x3370 = ctrl 3363 Compress tolerance */
    sm->five_ax_buffer = rd32(r, n, 0x1ba0);                          /* +0x3c28 (FUN_18021d770: 0-3 -> radios Automatic / Low / Medium / High) */
    sm->five_ax_tip = rd8(r, n, 0x229e);                              /* +0x4326 = ctrl 3374 "5-axis tip only" */
    sm->color = rd8(r, n, 0x2298);                                    /* +0x4320 */
    for (int i = 0; i < f->ntp && sm->nsources < 64; i++) {           /* the source ops: kind 0x17, +0x98 op id (TpRef.chain), +0x9c (TpRef.index) */
        const TpRef *t = &f->tp[i];
        if (t->op_id == sm->op_id && t->kind == 0x17) { sm->sources[sm->nsources] = t->chain; sm->source_flag[sm->nsources] = t->index; sm->nsources++; }
    }
    return 1;
}

void stockmodel_print(FILE *out, const StockModelOp *s) {
    fprintf(out, "stockmodel op %d name=\"%s\" shape=%d (%s) view=%d color=%d\n", s->op_id, s->name, s->shape, stockmodel_shape_name(s->shape), s->view, s->color);
    fprintf(out, "  size %.6f %.6f %.6f origin %.6f %.6f %.6f anchor=%d shape_tol=%.6f add_offset=%.6f\n", s->size[0], s->size[1], s->size[2],
            s->origin[0], s->origin[1], s->origin[2], s->anchor, s->shape_tol, s->add_offset);
    fprintf(out, "  arc_tol=%.6f path_tol=%.6f tool_tol=%.6f compress_tol=%.6f 5ax_buffer=%d 5ax_tip=%d\n", s->arc_tol, s->path_tol, s->tool_tol,
            s->compress_tol, s->five_ax_buffer, s->five_ax_tip);
    for (int i = 0; i < s->nsources; i++) fprintf(out, "  source op %d%s\n", s->sources[i], s->source_flag[i] == 1 ? " (initial stock)" : "");
}
