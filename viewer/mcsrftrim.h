/* mcsrftrim.h - Mastercam's face trimming core (mccore.dll MaFace::TrimToLoopsNew and the MaTrimSurf* kernels): viewer/mcsrftrim.c.
 * formats/surfaces_trim_decode.md section 2. */
#ifndef MCSRFTRIM_H
#define MCSRFTRIM_H
#include "mcsrfint.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int n; double (*p)[2]; } MiPoly;       /* a loop's polyline (MaLoop::GetLoopPolyLine), the surface's normalised (u, v) */
typedef struct { int nl; MiPoly *l; } MiTrimFace;         /* l[0] the outer loop, then the inner ones */
typedef struct {
    int count, deleted, nfaces;   /* the faces made (MaTrimSurf +0xc0), the surfaces put on the delete list (+0x48), the faces */
    MiTrimFace *faces;
    int has_keep; double keep[2]; int nin; MiPoly *in;   /* GenUVKeepPoint's (u, v) and the loops given to TrimToLoopsNew (the oracle prints them) */
    const char *refused;          /* a path that is NOT DECODED / not ported was needed: no result */
} MiTrimRes;
/* MaTrimSurfToPlane::DoTrim 0x1804cacd0 on one surface (trim_surfs_to_a_plane 0x18056ed00): the face from the stored loops (nl 0: untrimmed),
   plane = (unit normal, d), keep = the point on the kept side. Returns DoTrim's value. */
int mi_trim_to_plane(ISrf *s, int nl, const int *np, double (*const *lp)[2], double chord, double tol, const double plane[4], const double keep[3],
                     int delun, int split, int keepm, int extend, int interf, MiTrimRes *R);
void mi_trim_res_free(MiTrimRes *R);
/* a surface given to the Trim kernels: the NURBS (knots normalised) and its stored loops as the record holds them (normalised like the knots;
   nl 0 = an untrimmed surface) */
typedef struct { ISrf *s; int nl; const int *np; double (*const *lp)[2]; } MiTrimIn;
/* MaTrimSurfToSurf::DoTrim 0x1804cbc30 (trim_surfs_to_surfs 0x180564220 after its set swap): the surfaces A[0..nA) against the surface B; keepA /
   keepB the model-space keep points (eval_surf of the picks), trimA = Trim option < 2, trimB = (option & ~2) == 0; RA[i] per A surface, RB for B.
   Returns DoTrim's value. */
int mi_trim_to_surfs(MiTrimIn *A, int nA, const double keepA[3], MiTrimIn *B, const double keepB[3], int trimA, int trimB, int split, int keepm,
                     int extend, double chord, MiTrimRes *RA, MiTrimRes *RB);
#ifdef __cplusplus
}
#endif
#endif
