/* ifk_spinsil.h - Turn Profile kernel: port of Mastercam 2017's Interfacial IFSpinsil::Run (ifspinsil.dll) with the
 * Stitch preprocessing it calls (interfacial02.dll) and the Shell2D arrangement (both decoded in formats/interfacial.md
 * sections 6 and 7). Not yet checked against the real DLL (decision N11-5: oracle deferred).
 *
 * Input: a triangle mesh (the binary IFF surface Mastercam writes from wr_stl, section 3c; 3 own nodes per triangle).
 * Output: the 2D profile in the (axial x, radial r) plane as nodes (x, r) and segments (node pairs), in the order
 * IFSpinsil::Run hands them to its CurveTransfer. The coordinates are those the kernel sees (after the tpiftools
 * transform, section 6.1; ifk_spinsil_transform applies / reverses it). */
#ifndef IFK_SPINSIL_H
#define IFK_SPINSIL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int nnode, ntri;
    double *node;   /* 3 x nnode, in input (file) order */
    int *tri;       /* 3 x ntri, 0-based node numbers, in input order */
} IfkMesh;

typedef struct {
    int nnode, nseg;
    double *node;   /* 3 x nnode: (x, r, 0) as CurveXferMC stores them */
    int *seg;       /* 2 x nseg, 0-based node numbers */
    int cracked;    /* 1 = "IFSpinsil error 2: cracked periphery." */
    int overflow;   /* 1 = "Shell2D error 4: overflow." */
    char err[160];  /* first Shell2D / Stitch error text, "" if none */
} IfkCurves;

/* IFSpinsil::Run(curves, tol): tol = the tolerance the kernel receives (ComputeMC tol x scale). 0 = ok;
 * IFK_SPIN_FAULT = the DLL dereferences a NULL vertex here (no output, see ifk_spinsil.c). */
#define IFK_SPIN_FAULT (-2)
int ifk_spinsil_run(const IfkMesh *in, double tol, IfkCurves *out);
void ifk_curves_free(IfkCurves *c);
void ifk_mesh_free(IfkMesh *m);

/* Binary IFF surface (ConvertStlFileToBinIFF / TransferBinarySurfaceInput, section 3c). 0 = ok. */
int ifk_read_bin_surface(const char *path, IfkMesh *m);
int ifk_write_bin_surface(const char *path, const IfkMesh *m, unsigned char flag);
/* ASCII or binary STL -> mesh with 3 own nodes per triangle (as ConvertStlFileToBinIFF does, scale 1). */
int ifk_read_stl(const char *path, IfkMesh *m);

/* The tpiftools ComputeMC transform for Spinsil (section 6.1): from the bounding box and tolerance compute the
 * translation t (x only) and scale s; ifk_apply_transform maps v -> (v + t) then x s (ExecDataTranslation runs in
 * CheckDataLocation before CheckDataSize's scaling). big = ComputeMC+0x6ed. Returns the kernel tolerance. */
double ifk_spinsil_transform(const double box_lo[3], const double box_hi[3], double tol, int big,
                             double t[3], double *s, int *translated, int *scaled);
/* The same for ComputeMCSpinsil (spin = 1: adjustTransVec zeroes t.y, t.z) and ComputeMCSilhouette (spin = 0: the base
 * adjustTransVec, an empty function, keeps all three components). */
double ifk_mc_transform(const double box_lo[3], const double box_hi[3], double tol, int big, int spin,
                        double t[3], double *s, int *translated, int *scaled);

#ifdef __cplusplus
}
#endif
#endif
