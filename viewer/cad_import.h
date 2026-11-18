/* cad_import.h - DXF / STEP / IGES import (cad_import.c, formats/cad_import.md) */
#ifndef CAD_IMPORT_H
#define CAD_IMPORT_H
#include "mcam_load.h"
#include "occt_prim.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { CAD_NONE = 0, CAD_DXF = 1, CAD_STEP = 2, CAD_IGES = 3, CAD_DWG = 4, CAD_STL = 5 };
/* the converters' options: conv* globals, defaults from the user's configuration keys (mcamxm.config): 3000 Edge curves, 3001 repair, 3002 untrimmable
   surface level, 3003 Solids, 3007 Blank "Paper Space", 3008 Break drafting, 3009 IGES file tolerance, 3010 IGES unit conversion, 3011 stitching, 3019 STL import as lines, 3020 system colour */
typedef struct {
    int get_solids;     /* convGetSolids (3003) */
    int get_edges;      /* convGetEdges (3000) */
    int repair;         /* convRepairSolids (3001) */
    int stitch;         /* convGetSolidsViaStitching (3011) */
    int bad_surf_level; /* convBadSurfLev (3002) */
    int blank_ps;       /* convDWGBlankPS (3007) */
    int break_df;       /* convDWGBreakDF (3008) */
    int iges_tol;       /* convIGESOverrideTol (3009) */
    int iges_units;     /* convIGESOverrideUnits (3010) */
    int ignore_colors;  /* convImportIgnoreColors (3020) */
    int stl_lines;      /* convSTLImportLines (3019): mcstl.dll's "Import as Lines" (else "Import as STL Mesh") */
} CadImportOpts;
int cad_import_kind(const char *path);                          /* CAD_*; 0 = not a CAD file */
const char *cad_kind_name(int kind);
int cad_tol_setup(double tol);                                  /* the system tolerance (tol_exref) */
/* imports into f (an empty new part for Open, the current one for Merge); color / level = the current main colour / level.
   Returns 1 with msg = the report (what was made, what was left out), 0 with msg = why */
int cad_import_file(McamFile *f, const char *path, int kind, const CadImportOpts *o, int color, int level, char *msg, size_t msgn);
/* faces -> SURF_ID records; provided by the front end core (cui_surf.c, srf_faces_make) */
int cad_make_surfaces(McamFile *f, const OpFace *faces, int n, int level, int color);
#ifdef __cplusplus
}
#endif
#endif
