/* ifk_tpconv.h - HBE records <-> tp_ents around CSpinBdryArcFitter (mcmsurf FUN_18020c830 / FUN_18020cb90), see ifk_tpconv.c */
#ifndef IFK_TPCONV_H
#define IFK_TPCONV_H
#include <stdint.h>
#include "ifk_hbechain.h"
#ifdef __cplusplus
extern "C" {
#endif
#define IFK_TPENT 0x78

/* FUN_18020c830(conv, &count, 1): the filter's output records -> tp_ent line images (calloc'd n + 1 entries; free());
   tol = mccore ?tol@@3NA, colour = mccore ?lathe_cfg_stock_color@@3EA */
uint8_t *ifk_hbe_to_tpents(const IfkHbe68 *rec, int n, double tol, uint8_t colour, short *count);
/* FUN_18020cb90(conv, tp, &count, 1): tp_ents -> records in out (room for 2 x count), passes by hmmSiluetBndyDat[0x4c] when
   spin (dat[0x4d] != 0): 0 as is, 1 mirrored in y, 2 both; old_count = the filter's record count (+0x164; fewer -> 0).
   Returns 1, 0 (nothing / too few), -1 (a profile value other than 0..2: NOT modelled) */
int ifk_tpents_to_hbe(const uint8_t *tp, short count, int spin, int profile, int old_count, IfkHbe68 *out, int *nout);
#ifdef __cplusplus
}
#endif
#endif
