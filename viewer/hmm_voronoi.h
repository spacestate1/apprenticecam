/* hmm_voronoi - port of mchmm.dll's own Voronoi library (McHmm "voronoi\VoronoiFunctions.cpp"), the construction that the
 * Z-profile level evaluation FUN_180407c10 (hmm_zprofile.c l.180-258) runs on each level's loops before Corner rounding /
 * the boundary offset (formats/hst3d_port_plan.md 7 "W3", user decision N13-12).
 *
 * The records keep the DLL's layouts (std::list nodes: +0 next, +8 prev, +0x10 payload; the payload offsets are the DLL's),
 * so the port reads like the decompile it comes from and its graph can be compared with the DLL's one record by record
 * (tools/peloader hmm_vor.c, tools/tests/hmm_voronoi_oracle.sh). See hmm_voronoi.c for the record layouts. */
#ifndef HMM_VORONOI_H
#define HMM_VORONOI_H
#include <stdint.h>

typedef struct {
    double x, y;
    uint64_t kind;   /* FGridC entry bits & 0xfffffffff0000000 (FUN_18004f610): 0, 0x40000000, ... (FUN_180546680) */
} HvVert;

typedef struct {
    const HvVert *v;
    int n;           /* distinct vertices; the closing entry (= v[0]) is implied as in FUN_180409f40's closed FGridC */
} HvPoly;

typedef struct {
    double recip;    /* VoronoiParallelVanishingDistanceReciprocal 0x1bc7 (default 1e-6): graph +0x80 = 1 / recip */
    double sbl;      /* VoronoiSmallBisectorLength 0x1bc8 (default 2e-5): graph +0x88 */
    double sbs;      /* VoronoiSmallBisectorSplit 0x1bc9 (default 2e-5): graph +0x90 */
    double ptol;     /* VoronoiPerturbationTolerance 0x1bc6 (default 0.002): FUN_18054a440 param_4 */
} HvParams;

typedef struct HvGraph HvGraph;

void hv_params_default(HvParams *p);
/* FUN_1805475b0 + FUN_18054a440: polys[0] is the outer profile (clockwise in input order, FUN_180406660), the others
   islands (counter-clockwise). Returns 1 on success, 0 when the construction and its 7 perturbed retries all fail. */
HvGraph *hv_new(const HvParams *p);
int hv_build(HvGraph *g, const HvPoly *polys, int npoly);
void hv_free(HvGraph *g);

/* raw access for the dump / the consumers (hmm_voronoi.c documents the payload offsets) */
unsigned char *hv_graph_raw(HvGraph *g);    /* the 0xb0-byte graph record of FUN_1805475b0 */

/* FUN_18054a640: the offset radius r0 (VorProfileFlatRad 0x1bee) moved up by RadPushUp 0x1bca within RadPushUpLimit 0x1bcb
   (FUN_180407c10 calls it when r0 != 0) */
double hv_push_radius(HvGraph *g, double r0, double pushup, double limit);
/* FUN_18054b480: label the regions of clearance above r (sites +0x58, nodes +0x84, edges +0x58; the sites' crossing edges
   +0x48 / +0x50); returns their count */
int hv_regions(HvGraph *g, double r);

/* FUN_18054ec00 (+ FUN_1805538a0 / FUN_1805540e0 / FUN_180554e80): the compact copy of region reg at offset radius r (clearances
   minus r, the sites' lines / circles moved in by r, a vertex node at each site's crossing of r, the loops of crossings with their
   diameters, the bottleneck tree); writes the region's index fields into the graph (sites +0x30, edges +0x48, nodes +0x80).
   The copy keeps the DLL's 0xf8-byte layout (hmm_voronoi.c). Returns 0 where the DLL would assert or write through NULL. */
typedef struct HvCopy HvCopy;
HvCopy *hv_copy_new(void);
int hv_copy(HvCopy *c, HvGraph *g, int reg, double r);
unsigned char *hv_copy_raw(HvCopy *c);
void hv_copy_free(HvCopy *c);

/* the pass writer FUN_180557f60 writes through (Eva +0x4f0 in McHmm): FUN_1801192a0 start (point, flag, kind 0x1e / 0x1f),
   FUN_1801194d0 line to, FUN_1801195f0 arc to (end point, centre, radius, two bytes, flag), FUN_180121900 end. The flag is 0
   where the span lies on a site whose vertex-kind byte (graph site +0x5c, copy site +0x44) is set and FUN_180406b00's param_9
   is 0, else 1. */
typedef struct {
    void *ctx;
    void (*start)(void *ctx, const double *p, int flag, int kind);
    void (*line)(void *ctx, const double *p, int flag);
    void (*arc)(void *ctx, const double *p, const double *c, double r, int b5, int b6, int flag);
    void (*end)(void *ctx);
} HvWriter;
/* FUN_180557f60 for one spec {B, best, t, tau} (MinDeviation 0x1bd4, MaxRadius 0x1bd2 (-1: no cap), MinRadius 0x1c00,
   MinArcChordLength 0x1bd3), with Corner rounding when tau (CornerTolerance) != t; the copy's +0x78 (VoronoiPolyEpsilon 0x1bce)
   is the corner tolerance of FUN_180556ec0. 1 written, 0 where the DLL would assert or corrupt its list */
int hv_offset_loop(HvCopy *c, const HvWriter *w, unsigned char *B, unsigned char *best, double t, double tau, double mindev,
                   double maxrad, double minrad, double minchord, int kind, int p9);
/* FUN_180555f60 (t = 0) + FUN_180406b00's spec loop (kind 0): every loop of the region offset by the copy's r' */
int hv_offset_region(HvCopy *c, const HvWriter *w, double tau, double mindev, double maxrad, double minrad, double minchord, int p9);
/* the libm of the Corner rounding's arc split (mchmm.dll imports MSVCR120's sin / cos / atan2; default glibc's) */
void hv_set_libm(double (*s)(double), double (*c)(double), double (*a2)(double, double));

/* mchmm's arc library (FUN_180051d20 sweep, FUN_180051e00 point at a parameter, FUN_180052110 parameter of a point; arc = centre,
   radius, start, end, direction byte) and FUN_18005ab30 (squared distance of p from the segment a b, 3D), on hv_set_libm's libm */
double hv_arc_sweep(const double *c, double r, const double *s, const double *e, int dir);
void hv_arc_point(double *o, const double *c, double r, const double *s, const double *e, int dir, double f);
double hv_arc_param(const double *c, double r, const double *s, const double *e, int dir, const double *p);
double hv_seg_d2(const double *p, const double *a, const double *b);
/* FUN_18000b4e0 (lerp, a itself when a == b) and the cos of hv_set_libm's libm */
void hv_lerp(double *o, double t, const double *a, const double *b);
double hv_libm_cos(double x);

#endif
