/* cam3d_mmodel.h - Constant Scallop stage A after make_surf_model (cam3d_mmodel.c, formats/surf_classic3.md 12.11) */
#ifndef CAM3D_MMODEL_H
#define CAM3D_MMODEL_H
#include "mcam_load.h"
#include "mcpartline.h"
/* one tmpcrn0 record (0x70 bytes, mcmsurf FUN_180224ec0) */
typedef struct { int vtx; double P[3], d1[3], d2[3], N[3]; int flag; } PlCorner;
typedef struct {
    int chained;              /* FUN_180225000's out flag (local_8278): every edge record went into one walk */
    int *order, nord;         /* tmpedg1: the old record index of each record in walk order */
    PlEdge *edge; int nedge;  /* tmpedg0's records after FUN_180225000 (the walk order when chained, else unchanged) */
    PlCorner *c; int nc;      /* tmpcrn0's records of the surface */
} MmCorners;
/* FUN_180225000 on make_surf_model's edges (ne = mms +0x54) over nv vertices; tol = chord x 0.9 x 0.01. 0 ok, -1 NOT DECODED */
int mm_corners(const PlEdge *E, int ne, int nv, double tol, MmCorners *o);
void mm_corners_free(MmCorners *o);
void mm_corner_bytes(const PlCorner *c, unsigned char out[0x70]);
/* tmpmm record / tmpmm1 bytes of one surface (FUN_180229ba0) */
void mm_record(const PlModel *m, long long mm1pos, int edgpos, int ridge, unsigned char out[0x78]);
size_t mm_mm1_bytes(const PlModel *m, unsigned char **out);
/* fill_mach_model_buffer's output as one block (untrimmed with or without the outline loop, or trimmed): grid points then loop points /
   normals (frame) and the cell triangles */
typedef struct { int np, nt; double *pts, *nrm; int *tri; } MmFill;
int mm_fill(const McamFile *f, const Surf *s, const PlModel *m, const double *frame, MmFill *o, const char **why);
void mm_fill_free(MmFill *o);
/* the HST drive mesh of one surface as mcmsurf FUN_180258310 writes it to tmphardmillraw.stl (formats/hst_linker.md 21): chord = msurf_chord_dev
   (record 0x12a0), style4 = UI style 4 (record 0x16bc), sxy / sz = record 0x13c0 / 0x13c8; *out = 9 doubles per triangle (model units, the STL's
   floats / the unit factor); V = the active Cplane's view matrix rows (NULL: Top; hst_linker.md 21.5); zlo / zhi / first: FUN_180258310's running vertex Z range (*first = 1 before the first surface). Returns the
   triangle count (0: the surface gives none), -1 refused (why) */
int mm_hst_surface(const McamFile *f, const Surf *s, double chord, int metric, int style4, double sxy, double sz, double **out,
                   const double *V, double *zlo, double *zhi, int *first, const char **why);
#endif
