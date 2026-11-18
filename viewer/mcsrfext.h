/* mcsrfext.h - Surfaces > Modify > Extend: the NURBS surface extension of extend_surf (mccore 0x18032fb30) and its checks (mcsrfext.c,
   formats/surfaces_tab_plan.md "E. Extend") */
#ifndef MCSRFEXT_H
#define MCSRFEXT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* FUN_18032d240: a kind 0 SURF_ID record extended at one edge (dir 0 = a u edge, 1 = a v edge; end 0 = the minimum, 1 = the maximum) by
   one new knot span of parameter length 1; len = the length (By distance), plane = {point, normal} (To plane: a length per row of control
   points) or NULL; linear = the Style (IsLinear). Returns the new record (malloc'd, header fields of FUN_18032d240 set, put_surf_in_db's
   not), *on its size, uv8 = the 4 (u, v) corners of the new strip as FUN_18032d240's param_10; NULL with *err: 1 = "Extension to plane
   failed" (mccadres 7459, the plane missed), 2 = not a NURBS record this reads, 3 = memory, 4 = the Non-linear length iteration did
   not converge (ours: mccore.dll loops forever there, MSE_NONLIN_CAP). tol = the system tolerance (lp_ints). */
uint8_t *mse_extend_nurbs(const uint8_t *rec, uint32_t n, int dir, int end, double len, const double *plane, int linear, double tol,
                          double uv8[8], uint32_t *on, int *err);
/* FUN_18032cd00 + FUN_18032c670 (extend_surf): 1 = the extension self-intersects (the question mccoreres 7458) */
int mse_selfint(const uint8_t *rec, uint32_t n, const double uv8[8], int dir);
/* FUN_180338540 for an untrimmed surface: the edge nearest to (u, v) in the normalised range (dir, end as above) */
void mse_edge_untrimmed(const uint8_t *rec, const double uv[2], int *dir, int *end);
/* (FUN_180338540's trimmed branch and extend_trimmed_surf: decoded in formats/surfaces_trim_decode.md 1.3 / 1.7, NOT PORTED) */
#ifdef __cplusplus
}
#endif
#endif
