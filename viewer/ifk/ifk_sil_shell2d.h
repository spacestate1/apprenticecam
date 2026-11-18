/* ifk_sil_shell2d.h - the small Shell2D of interfacial11.dll that Silhouette's test-point classifier
 * (FUN_180022e80, the ">= 2 facets listed" branch) builds per test point, and FUN_180021830 ("the test point lies
 * on this output edge"). Port and function-by-function comparison with the ifspinsil Shell2D: ifk_sil_shell2d.c. */
#ifndef IFK_SIL_SHELL2D_H
#define IFK_SIL_SHELL2D_H

#include "ifk_planecut.h"   /* PcSD (exact homogeneous values) */

typedef struct IfkS2 IfkS2;

IfkS2 *ifk_s2_new(double g);                       /* FUN_1800043f0(g): grid g * g */
void ifk_s2_free(IfkS2 *s);
void ifk_s2_reset(IfkS2 *s);                       /* FUN_180007cc0: everything released, the grid kept */
void ifk_s2_begin(IfkS2 *s);                       /* +0x60 = FUN_18000a1a0(+0xc8): a new component */
int  ifk_s2_vertex(IfkS2 *s, double x, double y);  /* FUN_180004840 -> vertex id (no dedup: the caller caches) */
void ifk_s2_segment(IfkS2 *s, int a, int b);       /* FUN_1800049a0 (equal ends -> nothing) */
void ifk_s2_end(IfkS2 *s);                         /* FUN_180008ed0(+0x58, +0x60); +0x60 = 0 */
int  ifk_s2_process(IfkS2 *s);                     /* FUN_180004a50: 1 / 0 (no continuation found), -1 = not ported */

/* Test point: P points at the caller's live record (x, ex, y, ey); re-read after every exact() call (the test
 * point's slot 0, interfacial03 FUN_1800148b0, overwrites those doubles). exact() gives the homogeneous exact
 * coordinates X, Y, W (W > 0; value = X / W, Y / W). */
typedef void (*IfkS2Exact)(void *user, PcSD *X, PcSD *Y, PcSD *W);
/* walks the output list +0x40 from its head, FUN_180021830 on each; 1 at the first edge that holds, else 0 */
int  ifk_s2_point_on_output(IfkS2 *s, const double *P, IfkS2Exact exact, void *user);

const char *ifk_s2_error(IfkS2 *s);               /* first "Shell2D error N" message or NULL */
int  ifk_s2_checkpoints(IfkS2 *s);                 /* number of "IFSilhouette checkpoint 1" dialog messages
                                                      (FUN_180022260 exact branch) since the last reset */
int  ifk_s2_noutput(IfkS2 *s);                     /* output list length (diagnostics) */
void ifk_s2_output_edge(IfkS2 *s, int k, double xy[4]);   /* k-th edge from the head: (x0, y0, x1, y1) */

#endif
