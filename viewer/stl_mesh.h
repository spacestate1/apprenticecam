/* stl_mesh.h - STL files: Mastercam's triangle readers and File > Open's STL mesh (formats/cad_import.md 7, viewer/stl_mesh.c) */
#ifndef STL_MESH_H
#define STL_MESH_H
#include <stdio.h>
#include <stdint.h>
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif
/* get_first_stl_triangle / get_next_stl_triangle (mccore 0x1806d1070 / 0x1806d1280): check_ascii_binary, the ASCII reader FUN_1806d0aa0
   (NRM, when not NULL, gets the facet normal of a "facet normal" line; untouched otherwise) and the binary reader FUN_1806d0ec0 */
int stl_is_ascii(FILE *f);
int stl_ascii_tri(FILE *f, double t[3][3], double *nrm);
int stl_bin_tri(FILE *f, double t[3][3]);
typedef struct { FILE *f; int ascii; } StlTriReader;
int stl_tri_first(StlTriReader *r, const char *path, double t[3][3]);   /* opens PATH; 0 = no triangle (r->f NULL when it could not be opened) */
int stl_tri_next(StlTriReader *r, double t[3][3]);
void stl_tri_close(StlTriReader *r);
/* FUN_1806ccb40: 0 the file cannot be opened, 2 binary (the size is the u32 at 0x50 times 0x32 plus 0x54), 1 otherwise (ASCII) */
int stl_file_kind(const char *path);
/* StoreSTLFileAsSTLMesh (mccore 0x1805ccac0) up to store_ent: UtMMFDataMgr::CreateSTLEntryFromFile + CreateSTLEntryFromStream (the triangles),
   the PMESH_ID record (REC, 0x130 bytes: class 0x100000, kind 0, entry -1, the identity matrix DoImport passes, the box of FUN_1805cc560 and its
   flag). Returns the triangle count (> 0), 0 when the file made no entry (Mastercam then makes nothing and says nothing); *ERR = why (ours). */
int stl_mesh_from_file(const char *path, uint8_t rec[0x130], StlPoly **p, int *mixed, char *err, size_t errn);
#ifdef __cplusplus
}
#endif
#endif
