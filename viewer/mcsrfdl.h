/* mcsrfdl.h - a surface's display-list points and the wireframe pick on them (mastercam.dll DisplayList.cpp / DlPoint.cpp;
 * formats/chaining.md "Which routine normal picking uses" and "The display-list points (port)") */
#ifndef MCSRFDL_H
#define MCSRFDL_H
#include <stdint.h>
#include "mcam_load.h"
#ifdef __cplusplus
extern "C" {
#endif
/* one point record (FUN_180139360: 0x24 bytes): x y z float, the normal (only with surf_back_draw), u v float, flag (+0x20: 0 = the
   first point of a polyline, 1 = continues, 2 = a polyline's last point / a corner) */
typedef struct { float p[3], n[3], uv[2]; uint8_t flag; } SdlRec;
/* the node's surface part (store_ent_dl's 0xa0-byte node: +0x08 count, +0x0c the loop records' count, +0x10 built, +0x18 the records,
   +0x80 get_y_scr_scl when the loops were made) */
typedef struct { SdlRec *r; int n, cap, nloop; uint8_t built; float scl; } SdlNode;
/* the view as the generation reads it (get_y_scr_scl / get_x_scr_scl = view +0x770 / +0x768, get_wk_height +0x760; render_cfg 4 =
   shaded with outline; tol = tol_exref) */
typedef struct { double y_scr, x_scr, wk; int render_cfg4; double tol; } SdlView;
/* FUN_1801391f0 (store_ent_pl for class 0x100): the node's points; o->built = 1 when the generation returned nonzero. Returns 0 (the
   DLL's path), -1 when a surface evaluation this program cannot do failed (*why) */
int sdl_build(const McamFile *f, const Surf *s, const SdlView *v, SdlNode *o, const char **why);
void sdl_free(SdlNode *o);
/* FUN_18013cbc0: the pick on a built node. vm = the view matrix rows 0 / 1 (view +0x720 / +0x738, SetGrView), clip = view +0x788
   +0x790 +0x798 +0x7a0 (x min, x max, y min, y max: scrn_to_uu of the client rectangle), cur = the cursor in view x / y, best = the
   distance to beat (select_ent_univ's current best). 1 = found: *dist, uv */
int sdl_pick(const SdlNode *nd, const Surf *s, const double vm[6], const double clip[4], const double cur[2], double best, double *dist, double uv[2]);
/* FUN_18013e1e0 for a type 8 node: the min / max of the records' view x / y (x min, x max, y min, y max); 0 = not built */
int sdl_extents(const SdlNode *nd, const double vm[6], double box[4]);
/* dist_to_surf's trimmed branch FUN_1802de180 (mccore), for a trimmed surface without built display-list points: ax = the view's x / y
   iso axes (proj_iso_pt), pk the cursor in view x / y, best the distance to beat; wk / xs / ys / tol surf_division's view inputs
   (get_wk_height, get_x / y_scr_scl, the system tol). 1 found (*dist, uv clamped to the base's domain), 0 none, -1 not ported (why) */
int sdl_dist_trimmed(const McamFile *f, const Surf *s, const double ax[6], const double pk[2], double best, double wk, double xs, double ys, double tol,
                     double *dist, double uv[2], const char **why);
/* surf_screen_box (mastercam.dll 0x180340810) of the entity image r (ent +0x90 / +0xa8 the box, +0xc0 the centre) in the view rows vm:
   x min, y min, x max, y max */
void sdl_screen_box(const uint8_t *r, const double vm[6], double o[4]);
#ifdef __cplusplus
}
#endif
#endif
