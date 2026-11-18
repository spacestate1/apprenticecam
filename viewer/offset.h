/* offset.h - C API over Clipper 6 for 2D CAM (closed polygons, coordinates in model units) */
#ifndef OFFSET_H
#define OFFSET_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { double *xy; int n; } CPath;      /* n points, xy interleaved, implicitly closed */
typedef struct { CPath *p; int n; } CPaths;

CPaths cl_offset(const CPaths *in, double delta, double arc_tol);   /* round joins; + grows outer boundaries */
CPaths cl_union_evenodd(const CPaths *in);                          /* region from nested loops (islands = holes) */
double cl_area(const CPath *p);                                     /* signed area, > 0 = counter-clockwise */
CPaths cl_offset_lines(const CPaths *in, double delta, int closed, double arc_tol);   /* area swept by a disc */
CPaths cl_difference(const CPaths *a, const CPaths *b);                             /* a (even-odd) minus b (non-zero) */
CPaths cl_clip_open(const CPaths *lines, const CPaths *clip, int keep_inside);  /* open polylines inside / outside a region */
CPaths cl_bool(const CPaths *a, const CPaths *b, int op);          /* 0 union, 1 intersect, 2 a - b, 3 xor (non-zero) */
CPaths cl_offset_nz(const CPaths *in, double delta, double arc_tol); /* offset without re-normalising (non-zero input) */
double cl_total_area(const CPaths *p);
CPaths cl_offset_join(const CPaths *in, double delta, int join, double arc_tol);  /* non-zero region; join 0 round, 1 sharp, 2 square */
CPaths cl_group(const CPaths *in, int **owner);   /* outers (CCW) each followed by its holes (CW); owner[i] = its outer's index */                              /* signed sum: outer CCW +, holes CW - */
void cl_free(CPaths *ps);
int cl_error_take(void);                                             /* 1 = since the last call some input was invalid or Clipper failed */
#ifdef __cplusplus
}
#endif
#endif
