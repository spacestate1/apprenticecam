/* ifk_curvexfer.h - the binary IFF curve file: Mastercam 2017's writer (tpiftools.dll CurveXferMC, the CurveTransfer
 * IFSpinsil::Run writes into), the reader layout of mcmsurf (CMutlCvrtIffToHbeFile, FUN_180212b30 / FUN_18021d5d0 /
 * FUN_18021d7b0) and ComputeMC's in-place rewrite of IFF node blocks (FUN_180005690, used by ScaleBinIFFData /
 * TranslateBinIFFData and so by ComputeMC::ReverseTransformation). formats/interfacial.md sections 3c, 6.1, 9. */
#ifndef IFK_CURVEXFER_H
#define IFK_CURVEXFER_H

#include "ifk_spinsil.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes of ifk_write_bin_curves (0 = file written as CurveXferMC writes it). */
#define IFK_CX_OPEN       (-1)  /* fopen("wb") failed: CurveXferMC writes nothing */
#define IFK_CX_FORMAT     (-2)  /* format not 0 / 1: the constructor writes no header, every AddNode fails */
#define IFK_CX_NODELIMIT  (-3)  /* more than 60000 nodes: AddNode returns NULL, IFSpinsil::Run's AddSegment would then
                                   dereference NULL (no defined Mastercam result); nothing is written */
#define IFK_CX_HANG       (-4)  /* FUN_180330d60 would never terminate (a list whose head is never reached by the map
                                   walk, see formats/interfacial.md 9.2); header + nodes are written, no curves */

typedef struct {
    int format;        /* 0 = "binary curves", 1 = "binary tree_curves" */
    int nnode, nseg;   /* header: CurveXferMC +0xb4 / +0xb8 (reader +0x134 / +0x130) */
    double *node;      /* 3 x nnode */
    int *seg;          /* 2 x nseg, 0-based, the file's segment table */
    int ncurve;
    int *curve_start;  /* ncurve + 1 offsets into cseg */
    int *cseg;         /* per curve its segments as 2 x 0-based node numbers (pairs), concatenated */
    int *curve_x;      /* 2 x ncurve: format 1's two extra int32 per curve (reader +0x144 / +0x148), else 0 */
} IfkCurveFile;

/* CurveXferMC: c = the nodes / segments in the order IFSpinsil::Run hands them over (ifk_spinsil_run's output);
 * format = ComputeMCSpinsil+0x710 (0 or 1). Writes the closed chains only (FUN_180330750 == 1). Returns 0 or IFK_CX_*;
 * *ncurves_out (if not NULL) = number of curves written. */
int ifk_write_bin_curves(const char *path, const IfkCurves *c, int format, int *ncurves_out);

/* Reads a binary curve file with the field layout of mcmsurf's reader (header 13 / 18 bytes by format, first byte 'b';
 * nnode, nseg, nodes; then int32 count [+ 2 x int32 for format 1] + count x (2 x int32 1-based) until EOF). 0 = ok. */
int ifk_read_bin_curves(const char *path, int format, IfkCurveFile *f);
void ifk_curvefile_free(IfkCurveFile *f);

/* FUN_180005690 with IFBinaryFileScaler (v x s per coordinate) or IFBinaryFileTranslator (v + t): rewrites the node
 * block of a binary IFF file ("binary surface" / "binary curves" / "binary tree_curves") in place. 0 = ok. */
int ifk_bin_iff_scale(const char *path, double s);
int ifk_bin_iff_translate(const char *path, const double t[3]);

/* ComputeMC::ReverseTransformation (180315410) for a binary IFF output file: scale by 1.0 / s when scaled
 * (ScaleBinIFFData(.., s, true)), then translate by -t when translated (TranslateBinIFFData(.., t, true)). 0 = ok,
 * else the ComputeMC error code (0x1a scaling, 0x1b translation, 10 = not an IFF surface / curve file). */
int ifk_reverse_transform(const char *path, const double t[3], double s, int translated, int scaled);

#ifdef __cplusplus
}
#endif
#endif
