/* ifk_surfpoly.h - the STL that feeds Turn Profile / Silhouette Boundary (formats/interfacial.md 10.2 and 12): mccore wr_stl (0x1806d2e60) ->
 * surf_to_stl (0x1806d22e0) -> make_surf_polygons (0x1806fbda0), written as mucbMakeIffSiluetBndy asks for it (ASCII, quiet, no view matrix).
 * The triangles come from viewer/mcpartline_mesh.c plm_mesh (the port of make_surf_polygons, bit-exact on untrimmed NURBS surfaces and
 * on trimmed ones over them, interfacial.md 12.8);
 * the text from MSVCR120's printf "%f" (ifk_msvc_f). */
#ifndef IFK_SURFPOLY_H
#define IFK_SURFPOLY_H
#include <stdio.h>
#include "../mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif

/* MSVCR120 printf("%.*f", prec, v) (vfprintf_s -> _cftof: $I10_OUTPUT 17 digits, _fptostr half-up; see ifk_surfpoly.c). Returns the length. */
int ifk_msvc_f(char *out, size_t outsz, double v, int prec);

/* where the facets go: f = the file; raw = 1 writes each SysFile::Printf call as tools/peloader --stl prints it ("P " + the text with every %f
   replaced by the exact double as %a), for the oracle comparison */
typedef struct { FILE *f; int raw; } IfkStlOut;

/* surf_to_stl(ent, tol, file, ascii = 1, draw = 0, M, &err, &esc) on one surface: M = the x_matrix (rows 0..8, origin 9..11) wr_stl built
   (identity and origin 0 without a view: ifk_wr_stl). Returns the facet count, -1 when the surface is not ported (why says what). */
int ifk_surf_to_stl(const McamFile *F, const Surf *s, double tol, const double M[12], IfkStlOut *o, const char **why);

/* wr_stl(tol, file, name, -1, 1 (ASCII), 1 (quiet), 0 (no view), 0, 0) on the surfaces sel[0..n-1]: the entities wr_stl's walk takes, in database
   order (the caller filters: selected (ent +4 bit 4), not blanked (bit 0x100), on a visible level - see ifk_surfpoly.c). Writes "solid ascii",
   the facets, "end solid"; nothing at all when n == 0 (wr_stl posts warning 0x153b only when not quiet). Returns the facet count, -1 not ported. */
int ifk_wr_stl(const McamFile *F, const Surf *const *sel, int n, double tol, IfkStlOut *o, const char **why);

#ifdef __cplusplus
}
#endif
#endif
