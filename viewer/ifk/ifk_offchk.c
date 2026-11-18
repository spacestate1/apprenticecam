/* ifk_offchk.c - mcmsurf FUN_18021ff80 (CMutlCvrtIffToHbeFile's offset check, Silhouette Boundary only) and the pock_bound
 * it builds: FUN_18020c6d0 (RecordBoundaries_Add_PockBnd, flags), FUN_18020c5a0 (entities, length), FUN_18020da60 (entity
 * boxes); the curve box at converter +0xb0 comes from FUN_180213ee0 (FUN_180242280 init) / FUN_18021f540 / FUN_18020dbc0 /
 * FUN_18020c1c0 (FUN_180252970 per record end). formats/interfacial.md 14.3. MCCECalcOffset itself: NOT DECODED. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "ifk_offchk.h"

static void box_add(const double *p, double b[4])   /* FUN_180252970: x, y; strict compares */
{
    for (int c = 0; c < 2; c++) { if (p[c] < b[c]) b[c] = p[c]; if (b[2 + c] < p[c]) b[2 + c] = p[c]; }
}

int ifk_offchk_bound(const IfkHbe68 *rec, int n, int parent, double conv_a0, IfkPockBound *out)
{
    memset(out, 0, sizeof *out);
    out->ent = calloc((size_t)(n > 0 ? n : 1), sizeof *out->ent);
    if (!out->ent) return -1;
    out->n = n;
    out->box[0] = out->box[1] = 1e38; out->box[2] = out->box[3] = -1e38;   /* FUN_180242280 */
    double len = 0.0;
    for (int i = 0; i < n; i++) {
        IfkPbEnt *e = &out->ent[i];
        e->index = i; e->kind = 'L'; e->tag = 0x4d;
        e->s[0] = rec[i].p0[0]; e->s[1] = rec[i].p0[1]; e->e[0] = rec[i].p1[0]; e->e[1] = rec[i].p1[1];
        double dx = e->e[0] - e->s[0], dy = e->e[1] - e->s[1];
        len = sqrt(dx * dx + dy * dy) + len;
        /* FUN_18020da60 'L': box from the start, then the end with <= / strict compares */
        e->box[0] = e->s[0]; e->box[1] = e->s[1]; e->box[2] = e->s[0]; e->box[3] = e->s[1];
        for (int c = 0; c < 2; c++) {
            double v = e->e[c];
            if (e->box[c] <= v) { if (e->box[2 + c] < v) e->box[2 + c] = v; }
            else e->box[c] = v;
        }
        box_add(rec[i].p0, out->box); box_add(rec[i].p1, out->box);
    }
    out->len = len;
    out->b0 = 1; out->b1 = 0; out->b2 = 0; out->b3 = parent == 0; out->b4 = 0;
    out->offset = -fabs(conv_a0);
    return 0;
}

void ifk_offchk_free(IfkPockBound *b) { free(b->ent); memset(b, 0, sizeof *b); }

int ifk_offchk(const IfkHbe68 *rec, int n, int parent, double conv_a0, int spin,
               int (*offset_ok)(const IfkPockBound *, void *), void *ctx)
{
    if (spin) return 1;                       /* converter +0x12 = (spin == 0): the check runs for Silhouette Boundary only */
    if (!offset_ok) return IFK_OFFCHK_NOT_DECODED;
    IfkPockBound b;
    if (ifk_offchk_bound(rec, n, parent, conv_a0, &b)) return -2;
    int r = offset_ok(&b, ctx) ? 1 : 0;
    ifk_offchk_free(&b);
    return r;
}
