/* ifk_offchk.h - the Silhouette Boundary offset check of mcmsurf's HBE converter: FUN_18021ff80 with FUN_18020c6d0 /
 * FUN_18020c5a0 / FUN_18020da60 (formats/interfacial.md 14.3). The pock_bound it hands to MCCECalcOffset is ported here;
 * whether MCCECalcOffset(1, -|d|, bound, &out, 0x100, 0.0, 1e-6, 1e-6) returns a shape is NOT DECODED (the MCCE offset core,
 * formats/mcce_engines.md 6.13): the caller supplies it, or gets IFK_OFFCHK_NOT_DECODED. */
#ifndef IFK_OFFCHK_H
#define IFK_OFFCHK_H
#include "ifk_hbechain.h"
#ifdef __cplusplus
extern "C" {
#endif

/* one 0x88-byte pock_bound entity as FUN_18020c5a0 fills it */
typedef struct {
    int index;           /* +0x00 */
    char kind;           /* +0x08 'L' */
    int tag;             /* +0x0c 0x4d */
    double s[2], e[2];   /* +0x18 / +0x20 start x, y; +0x28 / +0x30 end x, y (z dropped) */
    double box[4];       /* +0x40 min x, +0x48 min y, +0x50 max x, +0x58 max y (FUN_18020da60) */
} IfkPbEnt;
typedef struct {
    IfkPbEnt *ent; int n;    /* +0x10 / +0x0c */
    double len;              /* +0x48 total 2D length */
    double box[4];           /* +0x20..+0x38 = converter +0xb0..+0xc8: the curve's 2D box (FUN_180213ee0 / FUN_180252970) */
    unsigned char b0, b1, b2, b3, b4;   /* +0 = 1, +1 = 0, +2 = 0, +3 = (curve header +0x148 == 0), +4 = 0 */
    double offset;           /* the MCCECalcOffset distance: -|converter +0xa0| (= -|dat+0x10 x 10|) */
} IfkPockBound;

/* FUN_18020c6d0: the bound of one curve's unfiltered records (all lines, +0x80 of the converter) */
int ifk_offchk_bound(const IfkHbe68 *rec, int n, int parent, double conv_a0, IfkPockBound *out);
void ifk_offchk_free(IfkPockBound *b);

#define IFK_OFFCHK_NOT_DECODED (-1)
/* FUN_18021ff80: 1 keep the curve, 0 drop it; spin (Turn Profile, converter +0x12 = 0): always 1. offset_ok answers
   "does MCCECalcOffset return a shape" (NOT DECODED here); NULL -> IFK_OFFCHK_NOT_DECODED */
int ifk_offchk(const IfkHbe68 *rec, int n, int parent, double conv_a0, int spin,
               int (*offset_ok)(const IfkPockBound *, void *), void *ctx);

#ifdef __cplusplus
}
#endif
#endif
