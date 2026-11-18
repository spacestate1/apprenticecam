/* mcfilter.h - see mcfilter.c */
#ifndef MCFILTER_H
#define MCFILTER_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { char k; char pad[15]; double a, b, c, d, e; } McflGt;   /* the 0x38-byte gt: 'L' x1 y1 x2 y2; 'A' cx cy r sa sw (signed) */
/* FUN_180948e60: 1 and *n the new count; g is rewritten in place; tol the filter tolerance, stol tol_exref, nctol nc_tol */
int mcfl_filter(McflGt *g, int *n, double tol, double rmin, double rmax, double stol, double nctol);
#ifdef __cplusplus
}
#endif
#endif
