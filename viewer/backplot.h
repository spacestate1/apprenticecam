/* backplot - Mastercam's classic Backplot (NWDViewer) state: position, stepping, conditional stops, Info tab values.
 * Spec: ../formats/backplot.md (sections cited below). The motions come from nc_post.c (what the post outputs). */
#ifndef BACKPLOT_H
#define BACKPLOT_H
#include "nc_post.h"

typedef struct {                 /* conditional stops, dialog 6000 (GeometryModel +0x700, defaults FUN_66d8a460: all off) */
    int at_ops, at_toolchange;   /* 6001 (+0x98), 6013 (+0x99) */
    int at_step; unsigned step;  /* 6002 (+0xd0), 6003 (+0xcc) */
    int at_x, at_y, at_z; double x, y, z;      /* "Stop at XYZ value" 6004-6009 */
    int chg_x, chg_y, chg_z;     /* "Stop at XYZ change" 6010-6012 */
} BpStops;

typedef struct {
    NcMotion *m; int n;
    double *cum;                 /* length along the path at the start of each motion */
    double total;
    int pos;                     /* current motion; the position is its end (slider semantics, section 2.2) */
    double dist;                 /* exact position as a distance along the path (interpolated stepping / play) */
    int vis0, vis1;              /* visible range [vis0, vis1) of motions (Operation List selection, section 8) */
    int *stops; int nstops;      /* motion indices where a conditional stop is (sorted) */
    BpStops cs;
    unsigned char flags[17];     /* display toggles (section 4.1) */
    int interp; double interp_dist;   /* bp_interpolate_steps / bp_interpolate_distance (section 2.5) */
} Backplot;

typedef struct {                 /* Info tab (dialog 4006, section 6) over the visible motions */
    double feed_len, rapid_len, feed_time_s;   /* feed time = sum L / F x 60 (C1..C4 = 1.0) */
    double rapid_time_s; int have_rapid_time;  /* info+0x90 (+0x98): only when the machine's rapid rates are known (13.6) */
    double lo[3], hi[3], fmin, fmax;
    int have_feed;
} BpInfo;

/* mccore [BACKPLOT] globals edited by the Options dialog 1228 (section 13.1) and read by backplot.dll (4.3). Keys and
   factory defaults: section 12.1 (FUN_1803ba450; FUN_1801b7980 is the key -> global setter). Colours are palette indices. */
typedef struct {
    int tool_appearance;                     /* 19000 mill_tool_appearance: 0 Plain, 1 Fluted, 2 Shaded (13.4) */
    int tool_material_on, tool_color, tool_material, tool_as_defined, spin_tool;          /* 19001-19005 */
    int holder_material_on, holder_color, holder_material;                                /* 19006-19008 */
    int color_loop_on_tc, verify_use_motion_color;                                        /* 19010, 19011 */
    int rapid_color, linear_color, cwarc_color, ccwarc_color, verify_color, vec_color;    /* 19013-19018 */
    int vec_on, vec_check1, vec_check2, vec_check3, vec_length_cb; double vec_length;      /* 19019-19024 */
    int fast_steps, cleanup_op, cleanup_tl, fit_toolpath, sim_cutter_comp, home_mill;     /* 19025-19030 */
    int simul_caxis, simul_rot; unsigned long toggles;                                     /* 19031-19033 */
    int interp; double interp_dist; int speed, home_lathe, caxis_top;                      /* 19034-19038 */
    double opacity_active, opacity_inactive;                                               /* 19039, 19040 */
    int cc_in_control_enabled, cc_in_control_active; double cc_in_control_radius;          /* set by FUN_66d19c90 (4.5) */
    double tol;                              /* config 1000 System tolerance (mccore tol): Options edit bounds (13.1) */
} BpOptions;
/* factory defaults FUN_1803ba450 (12.1). The 25.0 / 1.0 vs 1.0 / 0.05 pair depends on the mccore byte 0x1813eb30c
   (NOT DECODED, section 11): the U = 0 values are used here (they equal backplot.dll's ctor values, 4.3). The config
   file (bp_opt_cfg) overrides all of these. */
#define BP_OPT_FACTORY { .tool_appearance = 2, .tool_color = 15, .tool_as_defined = 1, .holder_color = 12, \
    .color_loop_on_tc = 1, .rapid_color = 14, .linear_color = 159, .cwarc_color = 159, .ccwarc_color = 159, \
    .verify_color = 138, .vec_color = 11, .vec_check1 = 1, .vec_length_cb = 1, .vec_length = 1.0, .fast_steps = 10, \
    .sim_cutter_comp = 1, .simul_caxis = 1, .interp_dist = 0.05, .speed = 60, .home_lathe = 1, \
    .opacity_active = 0.5, .opacity_inactive = 0.5, .tol = 0.00005 }
int bp_opt_cfg(BpOptions *o, int key, const char *val);   /* one "NNNNN. label? value" item; 1 = key handled */

/* RapidMoveTime (mcmachinedef.dll @1800ffc80, section 13.6): seconds for a rapid of DELTA (x, y, z). FEED[3] = the
   X / Y / Z axis max feedrates (CLinearAxisComponent::MaxFeedrate, per minute), CTRL0 = the first short of the
   control's nc_path_linear_ctrl, TOL = system tolerance. */
double bp_rapid_move_time(const double delta[3], const double feed[3], int ctrl0, double tol);

void bp_init(Backplot *b, NcMotion *m, int n);      /* takes M */
void bp_free(Backplot *b);
void bp_set_visible(Backplot *b, int v0, int v1);
void bp_seek_motion(Backplot *b, int i);            /* to the end of motion I */
void bp_seek_dist(Backplot *b, double d);
void bp_point(const Backplot *b, double *p);        /* current tool position */
enum { BP_FIRST = 0, BP_PREV_STOP = 1, BP_BACK = 2, BP_FWD = 3, BP_NEXT_STOP = 4, BP_LAST = 5 };
void bp_step(Backplot *b, int mode, int count);     /* FUN_66d84250 */
void bp_compute_stops(Backplot *b);                 /* FUN_66d62070 */
typedef struct {                 /* the machine definition inputs of RapidMoveTime / the tool change time (13.6, 6) */
    int known;                   /* 0: the part's machine record fields are NOT DECODED -> no rapid time */
    double feed[3]; int ctrl0; double avg_tool_change, tol;
} BpMachine;
void bp_info(const Backplot *b, BpInfo *inf);       /* FUN_66d5e740 accumulation (feed / rapid parts) */
void bp_info_rapid(const Backplot *b, const BpMachine *mc, BpInfo *inf);   /* + rapid / tool change time (FUN_67da5c00, FUN_67dbb350) */
#endif
