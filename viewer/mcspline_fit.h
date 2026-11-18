/* mcspline_fit.h - Mastercam's spline fitting (mccore.dll), see mcspline_fit.c */
#ifndef MCSPLINE_FIT_H
#define MCSPLINE_FIT_H
#include <stdint.h>
typedef struct { int ncp, nk, deg, dim, rational, closed; double *k, *cp; } McsfNb;
int mcsf_chords(const double (*Q)[3], int n, double tol, double *t);
void mcsf_pt_tan_coefs(const double p0[3], const double p1[3], const double d0[3], const double d1[3], double h, double out[12]);
int mcsf_coefs_open(double *c, const double (*Q)[3], int n, int end0, const double t0[3], int end1, const double t1[3], double tol);
int mcsf_coefs_closed(double *c, const double (*Q)[3], int n, double tol);
void mcsf_natural_tan(const double (*Q)[3], int start, double out[3], int n, double tol, int *ok);
int mcsf_tan_3p_arc(const double a[3], const double b[3], const double c[3], int start, double out[3], double tol);
int mcsf_nurbs_fit(const double (*Q)[3], int n, int end0, const double t0[3], int end1, const double t1[3], double tol, McsfNb *o);   /* 0 ok; 1 repeated node; 8 memory; 0x1811 too few */
uint8_t *mcsf_spline_rec(const double *coef, int nseg, const double p0[3], const double p1[3], int closed, double tol, uint32_t *rn);
void mcsf_squash_spline(uint8_t *r, const double n[3], double depth);   /* DEVIATION approved by the user, N9-26: a class 8 record projected on a plane */
void mcsf_spline_finish(uint8_t *r, uint32_t n, double step, double tol);   /* +0xa8: the curvature limit (put_spline_in_db step 1, recalc_spline 0.1) */
uint8_t *mcsf_nurbs_rec(const McsfNb *o, double tol, uint32_t *rn);
uint8_t *mcsf_spline_to_nurbs(const uint8_t *src, uint32_t srcn, double tol, uint32_t *rn);   /* class 8 -> 0x80 record (convert_spline_to_nb) */
#endif
