/* mcsurf_tonurbs.h - convert a surface entity to a NURBS surface (Wireframe > Spline Convert to NURBS, the surface branch):
   mccadutil convert_to_nurbs -> convert_entity_to_nurbs (0x100) -> mccore create_nurbsurf_from_surf -> put_surf_in_db. mcsurf_tonurbs.c */
#ifndef MCSURF_TONURBS_H
#define MCSURF_TONURBS_H
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif
/* convert_to_nurbs's rule for a selected surface (mccadutil 0x1800595e0): 0 = left alone, only redrawn (kind 0; kinds 8 / 10 whose
   find_base_surf base is kind 0); 1 = convert_entity_to_nurbs (everything else). */
int mcstn_rule(const McamFile *f, const Surf *s);
/* create_nurbsurf_from_surf + put_surf_in_db (density 1, shaded as given; tol = the system tolerance): the kind 0 record (0x118 + tail, *rn bytes, malloc'd) of a kind 1 / 2 / 6 / 7 surface; a kind 0 surface, or a kind 8 / 10
   on a NURBS base, comes back unchanged (a copy). NULL with *why for what is refused. Kinds 8 / 10 on any other base: mcstn_base, then mcstn_rebuild. */
uint8_t *mcstn_convert(const McamFile *f, const Surf *s, int shaded, double tol, uint32_t *rn, const char **why);
/* kinds 8 / 10 on a non-NURBS base: the new NURBS base (ent +0x28 = 1, sel bit 0x100 set): the UI stores it first (mcam_new_surf on the main attributes, Surf.blanked = Ent.blanked = 1) */
uint8_t *mcstn_base(const McamFile *f, const Surf *s, int shaded, double tol, uint32_t *rn, const char **why);
/* then the offset / trimmed surface on it (base_eptr = the stored base's Ent.septr, base = its record): the UI stores this in the source's colour / level and deletes the source (the delete rule takes a blanked old base with it) */
uint8_t *mcstn_rebuild(const McamFile *f, const Surf *s, int64_t base_eptr, const uint8_t *base, uint32_t bn, int shaded, double tol, uint32_t *rn, const char **why);
#ifdef __cplusplus
}
#endif
#endif
