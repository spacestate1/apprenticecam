/* ifk_mcside.h - the Mastercam side of Wireframe Turn Profile / Silhouette Boundary (mcmsurf.dll
 * mucbMakeIffSiluetBndy 180231800 and its helpers; formats/interfacial.md 3b and 10). File-level ports, no app wiring
 * (decision N11-5). */
#ifndef IFK_MCSIDE_H
#define IFK_MCSIDE_H

#include "ifk_spinsil.h"
#include "ifk_curvexfer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FUN_18022feb0: box of all STL vertices (box_lo = box[0..2], box_hi = box[3..5], start +-1e38); returns the
 * triangle count (0 = file unreadable or empty). The mesh is the one get_first / get_next_stl_triangle return. */
int ifk_stl_box(const IfkMesh *stl, double box[6]);

/* FUN_180232e10: the axis frame of the two axis points axis[0..2], axis[3..5] (hmmSiluetBndyDat+0x18..0x47):
 * m[0..2] = unit direction d, m[3..5] = UnitNormal(h, d), m[6..8] = d x m[3..5], m[9..11] = axis[0..2]. */
void ifk_axis_frame(const double axis[6], double m[12]);
/* FUN_180230670: out = R^T (p - o) with R's rows m[0..2], m[3..5], m[6..8] and o = m[9..11]. */
void ifk_axis_to_kernel(const double p[3], const double m[12], double out[3]);
/* mccore xform_pt (1808eba20) with that matrix: out = R^T p + o. */
void ifk_xform_pt(double p[3], const double m[12]);

/* FUN_1802339f0(in, axis, out, fill): copy a binary IFF surface with every node through ifk_axis_to_kernel; with
 * fill (mucbMakeIffSiluetBndy's bool argument) every triangle is preceded by two triangles to the axis. 1 = ok. */
int ifk_axis_transform_surface(const char *in, const double axis[6], const char *out, int fill);
/* FUN_1802340e0(in, axis, out): HBE records (0x68 bytes) with their points through ifk_xform_pt. 1 = ok. */
int ifk_axis_back_hbe(const char *in, const double axis[6], const char *out);

/* mucbTransformStl(0, 0, 1.0, out, in) for an active view other than Top: binary STL, every vertex through
 * world_to_view with the view's matrix rows vm (v[i] = vm[3i] w.x + vm[3i+1] w.y + vm[3i+2] w.z), normal =
 * facet_normal normalised, floats. Returns the triangle count written, < 0 on error. */
int ifk_transform_stl_view(const IfkMesh *stl, const double vm[9], const char *out);

/* One HBE record (0x68 bytes in the file): p[0], p[1] (+0x00, +0x18), p[2] (+0x30, arcs), type +0x60 (1 = line,
 * 2 = arc), first +0x61 (first record of its curve). */
typedef struct { double p[3][3]; unsigned char type, first; } IfkHbeRec;
typedef struct { IfkHbeRec *r; int n; } IfkHbeCurve;

/* CMutlCvrtIffToHbeFile's record building for one curve of the curve file (FUN_180213ee0 with FUN_18021d7b0,
 * FUN_18021f540, FUN_18020dbc0, FUN_18020c1c0): spin = hmmSiluetBndyDat[0x4d] != 0, systol = mccore's tol, scale =
 * the converter's +0xa8 (1.0). Returns the records before the filter (FUN_18020de20, not ported). */
int ifk_hbe_records(const IfkCurveFile *cf, int curve, int spin, double systol, double scale, IfkHbeCurve *out);

#ifdef __cplusplus
}
#endif
#endif
