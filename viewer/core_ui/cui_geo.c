/* cui_geo.c - the wireframe commands (Wireframe tab: Mastercam's MCCAD.DLL sketcher_* / MCCADUTIL.DLL trim, break,
 * offset commands; ../../formats/wireframe.md) and the model of their function panels (IWireframe*ManagerView,
 * ../../formats/ui_wireframe_panels.txt): the command state machine (geo_start / geo_take / geo_need / geo_steps /
 * geo_run / geo_exec / geo_apply / geo_end / geo_cancel), the live results (geo_live_*), the panel values, locks,
 * defaults and persisted keys (GeoParams gps, geo_defaults, geo_panel_sync, PKEYS / pk_*), the panel contents as a
 * builder walk (geo_panel_body, drawn by each UI through CuiPanelOps), the drawing preview / snap marker overlay
 * (geo_ov_extra), the click / cursor glue (geo_click, geo_cursor_update) and the script steps (cui_geo_script).
 *
 * Moved from viewer/mcamui.c (the "wireframe" and "wireframe commands" sections, handle_click's drawing branch,
 * ov_extra's drawing preview, the main loop's drawing cursor, run_script's geo steps) and viewer/geopanel_ui.inc (the
 * panel model and body) unchanged, with their names and decomp citations (formats/qt_port_plan.md phase 3). */
#include "../mcspline_refit.h"
#include "../mcbiarc.h"
#include "../mcfilter.h"
#include "../mcfillet.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "paths.h"
#include "core_ui.h"
#include "../mcam_saveops.h"
#include "mcsurf_build.h"   /* mcsb_view_k (Simple: the torus) */
#include "mcsurf.h"         /* spline curves: mcs_spline_eval_d, mcs_brk_spline_const_err (Break Many) */

int cfg_live_color = 39;                          /* config 5026 Live entity color (live_entity_color, sktcher_live_color) */

/* ------------------------------------------------------------------------------------------------ wireframe */
/* drawing commands (Wireframe tab, as Mastercam's MCCAD.DLL sketcher_* / MCCADUTIL.DLL trim, break, offset; see
   ../formats/wireframe.md): each command is a list of picks - p a point (grid snap; snapped to existing endpoints /
   centres / points within 8 px, like Mastercam's AutoCursor, or typed X / Y), e an entity, c a chain - and runs when
   they are complete, then waits for the next set. Commands on the selection have an Apply button. */
static const struct { const char *name, *alias; } gcmd[G_NCMD] = {
    [G_POINT] = { "Point Position", NULL }, [G_PT_DYN] = { "Point Dynamic", NULL }, [G_PT_SEG] = { "Point Segment", NULL },
    [G_PT_END] = { "Point Endpoints", NULL }, [G_PT_SMALL] = { "Point Small Arcs", NULL }, [G_BOLT] = { "Bolt Circle", NULL },
    [G_LINE] = { "Line Endpoints", NULL }, [G_LPAR] = { "Line Parallel", NULL }, [G_LPERP] = { "Line Perpendicular", NULL },
    [G_LCLOSE] = { "Line Closest", NULL }, [G_LBIS] = { "Line Bisect", NULL }, [G_LTAN] = { "Line Tangent Through Point", NULL },
    [G_CIRCLE] = { "Circle Center Point", NULL }, [G_ARC3] = { "Arc 3 Points", NULL }, [G_ATAN] = { "Arc Tangent", NULL },
    [G_CEDGE] = { "Circle Edge Point", NULL }, [G_AEND] = { "Arc Endpoints", NULL }, [G_APOLAR] = { "Arc Polar", NULL },
    [G_APOLEND] = { "Arc Polar Endpoints", NULL }, [G_RECT] = { "Rectangle", NULL }, [G_RSHAPE] = { "Rectangular Shapes", NULL },
    [G_POLY] = { "Polygon", NULL }, [G_ELLIPSE] = { "Ellipse", NULL }, [G_HELIX] = { "Helix", NULL }, [G_SPIRAL] = { "Spiral", NULL },
    [G_BBOX] = { "Bounding Box", NULL }, [G_TRIM] = { "Trim Break Extend", "Trim" }, [G_BREAK2] = { "Break Two Pieces", NULL },
    [G_JOIN] = { "Join Entities", NULL }, [G_BREAKX] = { "Break at Intersection", NULL }, [G_BREAKN] = { "Break Many", NULL },
    [G_TRIMMANY] = { "Trim Many", NULL }, [G_BREAKPTS] = { "Break at Points", NULL }, [G_FILLET] = { "Fillet Entities", NULL },
    [G_FILLETC] = { "Fillet Chains", NULL }, [G_CHAMF] = { "Chamfer Entities", NULL }, [G_CHAMFC] = { "Chamfer Chains", NULL },
    [G_OFFSET] = { "Offset", "Entity" }, [G_OFFSETC] = { "Chains", "Offset Chains" }, [G_PROJECT] = { "Project", NULL },
    [G_CLOSEARC] = { "Close Arc", NULL }, [G_BRKCIRC] = { "Break Circles", NULL },
    /* the Transform tab (cui_xform.c): the ribbon labels (ui/layout.txt), MCCADUTIL do_xform_trans / _trans3d / _rotate /
       _movetoorigin / _mirror / _array / _stretch / _scale */
    [G_XTRANS] = { "Translate", NULL }, [G_XTRANS3D] = { "Translate to Plane", NULL }, [G_XROT] = { "Rotate", NULL },
    [G_XORIGIN] = { "Move to Origin", NULL }, [G_XMIRROR] = { "Mirror", NULL }, [G_XARRAY] = { "Rectangular Array", NULL },
    [G_XSTRETCH] = { "Stretch", NULL }, [G_XDIST] = { "Distribute", NULL }, [G_XDYN] = { "Dynamic", NULL }, [G_XSCALE] = { "Scale", NULL }, [G_XROLL] = { "Roll", NULL },
    /* the Drafting tab (cui_draft.c): by their FT functions only - the labels Horizontal / Vertical / Parallel are also the Ordinate
       group's (doDraft_DIMENSION_ORD_*, not ported) and Wireframe labels */
    [G_DIMH] = { "doDraft_DIMENSION_HORIZONTAL", NULL }, [G_DIMV] = { "doDraft_DIMENSION_VERTICAL", NULL }, [G_DIMP] = { "doDraft_DIMENSION_PARALLEL", NULL },
    [G_ORDH] = { "doDraft_DIMENSION_ORD_HORIZ", NULL }, [G_ORDV] = { "doDraft_DIMENSION_ORD_VERT", NULL }, [G_ORDP] = { "doDraft_DIMENSION_ORD_PARA", NULL }, [G_ORDE] = { "doDraft_DIMENSION_ORD_EXIST", NULL }, [G_ORDA] = { "doDraft_DIMENSION_ORD_ALIGN", NULL },
    [G_DIMC] = { "doDraft_DIMENSION_CIRCULAR", NULL },   /* cui_draft_dim.c */
    [G_DIMA] = { "doDraft_DIMENSION_ANGULAR", NULL },    /* cui_draft_ang.c */
    [G_DIMB] = { "doDraft_DIMENSION_BASELINE", NULL }, [G_DIMCH] = { "doDraft_DIMENSION_CHAINED", NULL },   /* cui_draft_base.c */
    [G_DIMT] = { "doDraft_DIMENSION_TANGENT", NULL }, [G_DIMPP] = { "doDraft_DIMENSION_PERPENDICULAR", NULL },   /* cui_draft_tan.c */
    [G_DIMPT] = { "doDraft_DIMENSION_POINT", NULL },     /* cui_draft_pt.c */
    [G_NOTE] = { "doDraft_NOTE", NULL },                 /* cui_draft_note.c */
    [G_SREV] = { "Revolved Surfaces", NULL },
    [G_SDRAFT] = { "Draft Surface", NULL }, [G_SEXTR] = { "Extruded Surface", NULL },   /* Surfaces > Create > Draft / Extrude: started by their FTs (cui_surf.c srf_start); dialog titles mccadres 1084 / 1086 */
    [G_SPLMAN] = { "Spline Manual", NULL }, [G_SPLAUTO] = { "Spline Automatic", NULL }, [G_SPLBLEND] = { "Spline Blended", NULL }, [G_SPLCURV] = { "Spline From Curves", NULL }, [G_X2NURBS] = { "Spline Convert to NURBS", NULL }, [G_UNTRIM] = { "Untrim Spline", NULL }, [G_SIMPL] = { "Simplify Spline", NULL }, [G_MODIFYSP] = { "Modify Spline", NULL }, [G_REFIT] = { "Refit Spline", NULL },   /* cui_spline.c */
    [G_CSLICE] = { "Curve Slice", NULL }, [G_CINT] = { "Curve at Intersection", NULL }, [G_CSURF] = { "Curve Surface", NULL }, [G_SBLOCK] = { "Block", NULL }, [G_SCYL] = { "Cylinder", NULL }, [G_SSPH] = { "Sphere", NULL }, [G_SCONE] = { "Cone", NULL }, [G_STOR] = { "Torus", NULL }, [G_SFLAT] = { "Flat Boundary", NULL }, [G_SSOLID] = { "Surface From Solid", NULL }, [G_SLOFT] = { "Ruled/Lofted Surfaces", NULL }, [G_PTNODES] = { "Point Nodes", NULL }, [G_CVIEWS] = { "Combine Views", NULL }, [G_RGROOVE] = { "Relief Groove", NULL }, [G_LETTERS] = { "Create Letters", NULL }, [G_LNORM] = { "Line Normal", NULL }, [G_CCONST] = { "Curve Constant Parameter", NULL }, [G_CFLOW] = { "Curve Flowline", NULL }, [G_CDYN] = { "Curve Dynamic", NULL }, [G_CEDGE1] = { "Curve One Edge", NULL }, [G_CEDGEA] = { "Curve All Edges", NULL },   /* Surfaces > Simple: started by their FTs only (cui_surf.c) */    /* started by its FT only (cui_surf.c srf_start): no ribbon label matches */
    [G_CPART] = { "Curve Parting Line", NULL },   /* cui_partline.c */
    [G_SNSET] = { "Set Normals", NULL }, [G_SUNTRIM] = { "Untrim", NULL }, [G_SRMBND] = { "Remove Boundary", NULL },   /* cui_srfedit.c: started by their FTs (set_normals / UnTrimSurfaces) */
    [G_SEXT] = { "Surface Extend", NULL },   /* cui_srftrim.c: started by its FT (SurfaceExtend) */
    [G_STRIMS] = { "Trim to Surfaces", NULL },   /* cui_srftrim.c: started by its FT (trim_surface_1) */
    [G_SOFFSET] = { "Surface Offset", NULL },   /* cui_srfedit.c: started by its FT (OffsetSurfaces) */
};
/* Wireframe commands this program does not have (they need splines, surfaces or solid edges) */
static const char *geo_unsupported[] = { "Silhouette Boundary", "Turn Profile", "Raster to Vector", NULL };

int geo_cmd, geo_npick, geo_nent, geo_phase;
V3 geo_pick[4]; int geo_ent[4]; V3 geo_entpt[4];
int geo_pick_ac[4]; static int geo_pick_ac_next;    /* tests: the next geopick is an AutoCursor point */                                 /* the pick was an AutoCursor point (draw_rubber_band's param_2 = 1; the free cursor 0) */
/* live result (sketcher pattern, formats/wireframe.md "Live results"): the result entities (geo_live_l) are created at once
   in the live colour inside an undo step, rebuilt from the same picks (geo_live_in) when a panel value changes, and
   committed (sktcher_main_color + UndoTransCommit) on the next pick, Apply, Done (OK) or another command; Esc (Cancel)
   removes or commits them as each command's decompile does (geo_live_kind) */
typedef struct { int npick, nent; V3 pick[4], entpt[4]; int ent[4]; } GeoIn;
int geo_live, *geo_live_l, geo_live_n; static int geo_live_cap, geo_live_marked, geo_live_nu; static void *geo_live_rec[2];
static int ex_pick_base, ex_combo = 2; static double ex_ax[3] = { 0, 0, 1 };   /* Extrude: the base point pick, the axis combo index shown, the dialog's axis (+0x1588) */
static int geo_live_prefix;                             /* the live result was built from a pick prefix (candidates): a pick drops it, never commits */
static GeoIn geo_live_in, geo_in_now; static unsigned long geo_live_hash;
static void geo_live_start(int from);
static void brkmany_run(void);
static void geo_live_pv_note(void);
static void let_chs_free(void); static void let_take_chains(void);   /* Create Letters live until OK (ours, user decision LET-1) */
/* where a command clears its soft locks (geo_soft_clear_at): the result made by its picks, rebuilt by a panel change, committed by
   the next pick, Apply (panel code 1), the first (centre / base) pick of a result */
enum { SC_MADE, SC_REBUILD, SC_NEXT, SC_APPLY, SC_FIRST, SC_FAIL };
static void geo_soft_clear_at(int where);
/* the sizes a pending Ellipse / Simple primitive has (1: A radius / the first size, 2: B radius / the second): done = known
   (typed and locked, or picked), picked = taken by a pick (that pick is in the pick list); valid while its centre / base point
   is picked (create_ellipse.c :186-230, decomp/surfcreate/prims.c base clicks) */
static int geo_sz_done[3], geo_sz_picked[3];
static int geo_apolar_cw;                          /* Arc Polar: struct +2, createArcPolar's clockwise flag (the rubber band's sign, sketcher_arc_3.c:632) */
static int geo_apolar_st;                          /* Arc Polar: the second pick was taken as the start-angle point (sketcher_arc_3.c state 1 -> 2) */
static int geo_line_nsel, geo_line_enapply;        /* Line Endpoints: the manager's NumberSelected (+0x84) and EnableApply (+0x28) */
static void geo_soft_strip(int c);
static int geo_line_edit_click(V3 w);
static int geo_line_rb(V3 anchor, V3 w, double ang, double len, int lk_ang, int lk_len, V3 *q);
static V3 geo_line_lastpt;
static void geo_axoff_note(V3 p1);
static void geo_cand_show(void);
static int geo_ptdyn_far;                           /* Point Dynamic: measured from the far end (+0x2990, FUN_18034d080 sketcher_point_2.c:2522) */
static double geo_ptdyn_pv; static int geo_ptdyn_plk;   /* the Along value / hard lock the last code 4 saw */
static unsigned long geo_pend_hash;                 /* the panel values at the last pick / pending update (a change = a posted code) */
static unsigned long geo_param_hash(void);
static int geo_rebuilding;                          /* geo_live_update's rebuild: the panel's values, not the picks (Simple's sizes) */
static void geo_live_mark(int i);
static int geo_live_valid(void);
/* the chain commands (geo_chain_cmd): their chain list (the commands' CHAIN *local_4d0 / local_2f0), chain_it running
   (geo_chaining), the panel values the preview was last built from (geo_chain_hash) */
Chain geo_chains[256]; int geo_nchains, geo_chaining; static unsigned long geo_chain_hash;
int geo_panel_req = -1;
static void geo_chains_free(void) { for (int i = 0; i < geo_nchains; i++) cam_chain_free(&geo_chains[i]); geo_nchains = 0; overlay_dirty = 1; }
/* Fillet Chains (sketcher_fillet_2 FUN_18027ce30), Chamfer Chains (sketcher_chamfer_2 FUN_180210bb0), Offset Chains
   (mccadutil do_xform_offset_ctour): chain_it, several chains */
static int prim_need(int which);
static int geo_sz_need(int z);
static int geo_sz_lockdone(int z, int m);
int geo_chain_cmd(int c) { return (c == G_LETTERS && wm_let_chain()) || c == G_FILLETC || c == G_CHAMFC || c == G_OFFSETC || c == G_XROLL || c == G_SREV || c == G_SFLAT || c == G_SLOFT || c == G_SPLCURV || c == G_SDRAFT || c == G_SEXTR; }
int geo_applied;                                    /* a selection command's Apply already ran (OK does not run it again) */
double geo_dia = 0, geo_rw = 0, geo_rh = 0, geo_poly_r = 0; int geo_poly_n = 6, geo_level = 1, geo_color = 1;   /* geo_level / geo_color: the main attributes (level 1 / colour 1 at init_db) */
GeoParams gps[G_NCMD];
#define gp gps[geo_cmd]
static void geo_defaults(int c, int metric);
V3 geo_cursor; int geo_cursor_ok, geo_snapped, geo_snap_ent;
static int geo_inited[G_NCMD];                    /* panel defaults applied: 1 inch, 2 metric (RegisterVariable values change with the part's units) */
const char *geo_name(int c) { return dr_cmd(c) ? dr_label(c) : do_cmd(c) ? do_label(c) : c > 0 && c < G_NCMD ? gcmd[c].name : ""; }

/* the picks the active command needs, in order; "" = works on the selection (Apply) */
const char *geo_steps(void) {
    switch (geo_cmd) {
    case G_POINT: case G_BOLT: case G_APOLEND: case G_HELIX: case G_SPIRAL: return "p";
    case G_RSHAPE: return geo_rw > 0 && geo_rh > 0 ? "p" : "pp";   /* create_rectangle 1-point mode: size picked unless locked */
    case G_APOLAR: {                                     /* FUN_1801afae0 picks: the centre, the start point unless radius and start angle are
                                                           locked (or a start point was already taken: state 2, sketcher_arc_3.c FUN_1801af0b0
                                                           :1578 keeps it), the end point unless the end angle is locked */
        int st = !(gp.lk_r && gp.lk_sa) || geo_apolar_st;
        return st ? (gp.lk_ea ? "pp" : "ppp") : (gp.lk_ea ? "p" : "pp"); }
    case G_POLY: return geo_poly_r > 0 ? "p" : "pp";                  /* create_polygon: centre, radius dragged unless locked */
    case G_ELLIPSE: {                                    /* create_ellipse.c: the centre, then A (rubber band 8) unless A is done, then B (0xf)
                                                           unless B is done; done = locked (hard or soft) and > tol at the centre click
                                                           (:198-230), a size click (:186-197), a dialog change 0x6a (:281-291) */
        static char st[4]; int k = 0; st[k++] = 'p';
        for (int z = 1; z <= 2; z++) if (geo_npick ? geo_sz_need(z) : !geo_sz_lockdone(z, ~0)) st[k++] = 'p';
        st[k] = 0; return st; }
    case G_LNORM: return wm_ln_active() ? "p" : "e";      /* FUN_1803584a0: the pick (mask 0x1000200a, 0x100: surface or solid face), then locations */
    case G_PT_DYN: case G_LPAR: case G_LPERP: case G_LTAN: case G_BREAK2: case G_OFFSET: return "ep";
    case G_PT_SEG: return "e";
    case G_LINE: return gp.len > 0 ? "p" : "pp";
    case G_LBIS: return gp.single ? "ee" : "eep";                 /* Solution Multiple: the four lines, then the third pick keeps one (sketcher_line_3.c:383 / :391-407) */
    case G_LCLOSE: case G_FILLET: case G_CHAMF: return "ee";
    case G_CIRCLE: return geo_dia > 0 ? "p" : "pp";
    case G_ARC3: case G_AEND: return "ppp";            /* Arc Endpoints with the radius locked: the third click picks one of createArcEndpts'
                                                           candidates (FUN_1801a7330 -> FUN_1801997f0 SelectArcToKeep, sketcher_arc_4.c:1279) */
    case G_ATAN: return gp.mode == 6 ? "ee" : gp.mode == 2 ? "eep" : gp.mode == 4 || gp.mode == 5 ? "eee" : "epp";                /* sketcher_arc_6 modes 0 / 1: entity, point, the arc to keep; 6: two entities */
    case G_CEDGE: return gp.mode == 0 ? "pp" : "ppp";
    case G_RECT: return geo_rw > 0 && geo_rh > 0 ? "p" : "pp";
    case G_TRIM: { static const char *s[] = { "ee", "ee", "ee", "eee", "e", "ep", "e" }; return s[gp.mode >= 0 && gp.mode <= 6 ? gp.mode : 0]; }
    case G_TRIMMANY: return geo_phase ? (gp.trimmode ? "ep" : "e") : "";   /* Break: no side pick (FUN_1800dbf20) */
    case G_SRMBND: return "e";                           /* RemoveBoundary: select_surface (mask 0x100), then dyno_surf mode 3 (cui_srfedit.c) */
    case G_CEDGE1: return "e";                           /* FUN_1802a2940: get_event mask 0x100, then dyno_surf mode 3 without the AutoCursor (write_msg 0x1545) */
    case G_CDYN: return "e";                             /* FUN_180282390: get_event mask 0x100 (a surface), then dyno_surf mode 5 (the clicks go through geo_click) */
    case G_CFLOW: return "e";                            /* FUN_180287a40: get_event mask 0x100, no dyno */
    case G_CCONST: return "e";                           /* FUN_1802980f0: get_event mask 0x100 (a surface), then dyno_surf mode 3 (the clicks go through geo_click / the geopick test path) */
    case G_STRIMS: return "";                           /* FUN_18031ed90: select_multi_surfs (Enter), then the keep picks (geo_click: srf_trims_click) */
    case G_SEXT: return "e";                             /* FUN_180337330: get_event mask 0x100, then dyno_surf mode 3 (cui_srftrim.c) */
    case G_SBLOCK: case G_SCYL: case G_SSPH: case G_SCONE: case G_STOR:   /* the panel loops FUN_180011b50 / FUN_180029390 / FUN_18000aff0 /
                                                           FUN_180032a00 / FUN_18002dc80: base point, then each size not locked with a value */
        { static char st[4]; int k = 0; st[k++] = 'p'; if (prim_need(1)) st[k++] = 'p'; if (prim_need(2)) st[k++] = 'p'; st[k] = 0; return st; }
    case G_LETTERS: return !wm_let_ported() ? "" : wm_let_chain() ? "c" : "p";   /* get_event for the start point (FUN_18006d450); mode 15: NOT PORTED, decision N9-21 */
    case G_PTNODES: return "e";                         /* create_spline_points: select_ent mask 0x88 (cui_wfmisc.c) */
    case G_RGROOVE: return wm_rg_picking ? "p" : "";    /* the Select button (FUN_180077be0) */
    case G_FILLETC: case G_CHAMFC: case G_OFFSETC: case G_XROLL: case G_SREV: case G_SFLAT: case G_SLOFT: case G_SPLCURV: case G_SDRAFT: case G_SEXTR: return "c";   /* chain_it (geo_chaining); the side of Offset Chains is the
                                                                     chains' direction (FUN_18001bc50 'L' / 'R'), no pick */
    default: return "";
    }
}
/* Simple's sizes picked (1: block length / width, cylinder / sphere / cone radius, torus radius; 2: block / cylinder / cone height,
   torus minor radius) unless locked with a value; pick k: 0 the base point, else the size it sets */
static int prim_need(int which) {
    if (geo_npick > 0) return geo_sz_need(which);       /* the base point picked: the sizes' done flags (prims.c base clicks) */
    return !geo_sz_lockdone(which, ~0);
}
/* a size still to be picked (its pick is in the list, or it is not known yet) */
static int geo_sz_need(int z) { return geo_sz_picked[z] || !geo_sz_done[z]; }
/* size z known from the panel: locked (lk & mask: ~0 = hard or soft, GEO_LK_HARD = hard only) with a value. Ellipse: A = lock +0x18 and
   +0x10 > tol, B = lock +0x08 and +0x00 > tol (create_ellipse.c :198-230); Simple (prims.c base clicks): Block length + width / height
   (|v|), Cylinder / Cone radius / height, Sphere radius (no second size), Torus radius / minor radius (the thresholds as before) */
static int geo_sz_lockdone(int z, int m) {
    double tol = mc_systol.tol;
    switch (geo_cmd) {
    case G_ELLIPSE: return z == 1 ? (gp.lk_ra & m) && gp.ea_r > tol : (gp.lk_rb & m) && gp.eb_r > tol;
    case G_SBLOCK: return z == 1 ? (gp.lk_len & m) && fabs(gp.v_len) > 1e-9 && (gp.lk_w & m) && fabs(gp.v_w) > 1e-9 : (gp.lk_h & m) && fabs(gp.v_h) > 1e-9;
    case G_SCYL: case G_SCONE: return z == 1 ? (gp.lk_r & m) && gp.v_r > 1e-9 : (gp.lk_h & m) && fabs(gp.v_h) > 1e-9;
    case G_SSPH: return z == 1 ? (gp.lk_r & m) && gp.v_r > 1e-9 : 1;
    case G_STOR: return z == 1 ? (gp.lk_r & m) && gp.v_r > 1e-9 : (gp.lk_w & m) && gp.v_w > 1e-9;
    }
    return 1;
}
static int geo_sz_cmd(int c) { return c == G_ELLIPSE || (c >= G_SBLOCK && c <= G_STOR); }
static int prim_step(int k) { return k == 0 ? 0 : k == 1 && prim_need(1) ? 1 : 2; }
static int prim_top_pick;
static int ss_selecting;                            /* From Solid: the body selection runs (FUN_1803112e0) until Enter */
int srf_solid_selecting(void) { return geo_cmd == G_SSOLID && ss_selecting; }
int geo_has_panel(int c) { return c != G_SRMBND; }   /* RemoveBoundary has no function panel (decision S-5) */
int srf_ned_clicks(void) { return (geo_cmd == G_SNSET && srf_sn_active()) || (geo_cmd == G_SUNTRIM && srf_ut_active()) || (geo_cmd == G_SOFFSET && srf_of_active()); }   /* cui_srfedit.c */
static void geo_of_soft(void);
void srf_ned_enter(void) {
    if (geo_cmd == G_SOFFSET) {                         /* Offset's End Selection: FUN_1802f21b0, then FUN_1802aac90 + the build (cui_srfedit.c) */
        if (srf_of_selecting() && !srf_of_enter(gp.mode, gp.v_dist)) { geo_end(); return; }
        geo_of_soft(); snprintf(cam_msg, sizeof cam_msg, "%s", srf_of_prompt()); return;
    }
    if (geo_cmd == G_SNSET && srf_sn_selecting() && !srf_sn_enter()) geo_end(); else if (geo_cmd == G_SNSET) snprintf(cam_msg, sizeof cam_msg, "%s", srf_sn_prompt()); }   /* FUN_180110f10: nothing selected - the command ends */
void srf_solid_enter(void) {                        /* Enter (0xd) / End Selection: the conversion (FUN_180310fa0) */
    if (!srf_solid_selecting()) return;
    if (!srf_solid_nsel) return;                     /* nothing selected: FUN_1803112e0 returns 0 - the selection goes on */
    ss_selecting = 0; geo_live_prefix = 0; geo_run(); geo_reset_picks();
    snprintf(cam_msg, sizeof cam_msg, "%s", geo_name(geo_cmd));
}                           /* Cone: the Top radius button (598) - the next pick sets the top radius */
/* what the next pick is for: Mastercam's prompt strings (mccadres.dll RT_STRING ids, formats/mccadres_strings.tsv),
   as loaded (CString::LoadString, Ordinal_8010) by each command in mccad.dll (decomp/mccad_cmds/<entry>.c). "" where
   the command's string for that step was not found (fillets, trims and others not traced yet): nothing is invented. */
static void geo_ds_done(void);
static double cc_uv[2];                                  /* Curve Constant Parameter: the (u, v) dyno_surf returned, kept for the panel's rebuilds (bVar9) */
static int ce_sld_s = -1, ce_sld_e = -1;                 /* Curve One Edge: the solid edge of the last pick (event 0x4000) */
static struct { int si, fi; } ce_sel[64]; static int ce_nsel;   /* Curve All Edges: the selected solids (fi < 0) and solid faces (FUN_1802c4a70: 0x10000 + the temp file) */
static int *ce_l, ce_n;                                  /* Curve All Edges: the surfaces of the last build (rebuilt from them after a panel change) */
static int ce_any_trimmed(void) { for (int i = 0; i < ce_n; i++) if (ce_l && ce_l[i] < F.nents && F.ents[ce_l[i]].type == E_SURF && F.surfs[F.ents[ce_l[i]].srf].kind == 10) return 1; return 0; }
static int geo_live_valid(void);
static int ce_ba_on;                                     /* Curve One Edge: Break Angle enabled: 0 at the start, after Esc in the arrow, Apply, an inline Undo; after a build: the surface was trimmed (FUN_1802a2940 vtable +0x48: ed_cad1.c:775, :875, :907, :1007, :1031) */
static int cedge_trimmed(void) { return ce_ba_on && !(geo_live && !geo_live_valid()); }
static void ce_enter(void);
static int ce_built, ce_prompt1;                         /* Curve All Edges / One Edge: the prompt after a build */
static int cd_built;                                     /* Curve Dynamic: a spline was built (the prompt becomes 5969 until OK / Apply) */
const char *geo_prompt(void) {
    if (xf_cmd(geo_cmd)) return xf_prompt();            /* cui_xform.c */
    if (sl_cmd(geo_cmd)) return sl_prompt_text();
    if (si_cmd(geo_cmd)) return si_prompt_text();
    if (pln_cmd(geo_cmd)) return pln_prompt_text();
    if (sp_cmd(geo_cmd)) return sp_prompt_text();
    if (dr_cmd(geo_cmd)) return dr_prompt_text();       /* cui_draft.c */
    if (do_cmd(geo_cmd)) return do_prompt_text();       /* cui_draft_ord.c */
    const char *s = geo_steps(); int k = geo_npick + geo_nent + (geo_nchains > 0);
    switch (geo_cmd) {
    case G_LINE: if (gp.edit_pt == 0 || (gp.edit_pt == 1 && geo_live)) return gp.edit_pt ? "Specify the second endpoint" : "Specify the first endpoint";   /* editing: 0x1a07 / 0x1a08 (FUN_180190200) */
        return k ? "Specify the second endpoint" : "Specify the first endpoint";                       /* sketcher_line_1: 6664 / 6663 */
    case G_LPAR: return k ? (gp.dist > 0 ? "Indicate the offset direction" : "Select the point to place a parallel line through") : "Select a line";   /* sketcher_line_5: 5706 / 5708 / 6667 */
    case G_LPERP: return k ? "Sketch a point" : "Select line, arc, spline or edge";                          /* sketcher_line_4: 5723 / 5721 */
    case G_LCLOSE: return "Select line, arc, or spline";                                                       /* create_a_line_Closest: 5731 */
    case G_LBIS: return k == 2 ? "Select which line to keep" : "Select two lines to bisect";                                                          /* sketcher_line_3: 6665 */
    case G_LTAN: return k ? (gp.len > 0 ? "Select which line to keep" : "Select second endpoint of tangent line or enter a length") : "Select an arc or spline";   /* sketcher_line_6: 6666 / 6672 / 6668 */
    case G_CIRCLE: return k ? "" : "Enter the center point";                                                   /* sketcher_arc_2: 5774 */
    case G_AEND: if (k == 2 && geo_live_prefix) return "Select an arc";   /* SelectArcToKeep 5784 (FUN_1801997f0) */
        /* fall through */
    case G_CEDGE: return k == 0 ? "Enter the first point" : k == 1 ? "Enter the second point" : "Enter the third point";   /* sketcher_arc_1 / _4: 5775-5777 */
    case G_ARC3: return k == 0 ? "Enter the first point" : k == 1 ? "Enter the second point" : "Enter the third point";                                                      /* sketcher_arc_5: 5775 */
    case G_APOLEND: return gp.start_pt ? "Enter the start point" : "Enter the endpoint";   /* sketcher_arc_7.c:214, :300, :408: 0x1692 / 0x1693 (5778 / 5779) by the Start / End radio */
    case G_OFFSET: return k ? "Indicate the offset direction" : "Select the line, arc, spline or curve to offset";   /* do_xform_offset.c:97, :311 mccadutilres 0xf52 / 0xf53 (3922 / 3923) */
    case G_APOLAR: return k == 0 ? "Enter the center point" : k == 1 && !(gp.lk_r && gp.lk_sa) ? "Sketch the initial angle" : "Sketch the final angle";   /* sketcher_arc_3: 5766 / 5769 / 5771 */
    case G_ATAN: if (gp.mode == 2) return k == 0 ? "Select the line to be tangent to the circle" : k == 1 ? "Select the line to put the center of the circle on." : "Select an arc";   /* 5812 / 5814 / 5784 */
        return k == 0 || gp.mode == 6 || gp.mode == 4 || gp.mode == 5 ? "Select the entity that the arc is to be tangent to" : k == 2 ? "Select an arc" : gp.mode == 1 ? "Specify the thru point" : "Specify the tangent point";   /* sketcher_arc_6 FUN_1801b6540 / FUN_1801b48b0 / SelectArcToKeep: 5805 / 5784 / 5811 / 5807 */
    case G_POINT: return "Create point position";   /* sketcher_point_1.c:169 mccadres 0x1a36 (6710); "Sketch a point" 5835 only after an undo (:252) */
    case G_PT_DYN: return k ? "Cursor select point position" : "";                                            /* sketcher_point_2: 8930 */
    case G_PT_SEG: return geo_nent ? "Enter number, distance, or select a new entity." : "Create point along an entity: Select an entity";   /* sketcher_point_4.c:331 0x16ca (5834) */
    case G_PT_SMALL: return "Select arcs/circles, press Enter when done.";                                     /* sketcher_point_6: 5837 */
    case G_BOLT: return "Select position of base point";                                                       /* OnBoltCircle: 5844 */
    case G_RSHAPE: return k ? "Enter width and height or select position of corner." : "Select position of base point";   /* create_rectangle: 5845 / 5844 */
    case G_RECT: return k ? "Select new position for second corner" : gp.anchor_ctr ? "Select position of base point" : "Select position of first corner";   /* sketcher_rect: 5847 / 5844 / 5839 */
    case G_LETTERS: return wm_let_chain() ? "" : wm_let_prompt();   /* 6696 / 6697: FUN_18006d450 */
    case G_POLY: return k ? "Enter radius or select point" : "Select position of base point";                 /* create_polygon: 5853 / 5852 */
    case G_ELLIPSE: return k == 0 ? "Select position of base point" : k == 1 && gp.ea_r <= 0 ? "Enter X axis radius or select point" : "Enter Y axis radius or select point";   /* create_ellipse: 5855 / 5857 / 5856 */
    case G_HELIX: case G_SPIRAL: return "Enter the center point";                                             /* create_helix / create_spiral: 6701 */
    case G_BBOX: return bb_selecting() ? "Select entities, use Ctrl-A to select all" : "";                    /* FUN_1803470e0: mccadres 81 (select_ents) */
    case G_CHAMF: return k ? "Select line" : "Select line or arc";                                             /* sketcher_chamfer_1: 5881 / 5880 */
    case G_CHAMFC: return "Adjust values, OK, Apply, or Chain";                                                /* sketcher_chamfer_2: 5740 */
    case G_SREV: return geo_nchains && !srf_rev_axis_ok() ? "Select the axis of rotation" : "";
    case G_SEXTR: return ex_pick_base ? "Indicate Base Point" : "";                                         /* mccoreres 7442 (GetCoreString(0x1d12)) */
    case G_SBLOCK: { int t = prim_step(k); return t == 0 ? "Select base point for block" : t == 1 ? "Enter length and width or select position of corner"
                                     : t == 2 && s[k] ? "Enter height or select point" : ""; }   /* mccoreres 5917 / 5918 / 5919 */
    case G_SCYL: { int t = prim_step(k); return t == 0 ? "Select base point for cylinder" : t == 1 ? "Enter radius or select point"
                                   : t == 2 && s[k] ? "Enter height or select point" : ""; }     /* mccoreres 5904 / 5905 / 5906 (FUN_180028df0) */
    case G_CEDGE1: return ds_running() ? "Move arrow to Desired Edge of Surface" : ce_prompt1 ? "Set options, select a new surface, press <ENTER> or OK" : "Select a surface";   /* mccoreres 5445 (0x1545) / mccadres 5969 (0x1751 after a build), 5968 (0x1750) */
    case G_CEDGEA: return ce_built ? "Set options, press <ENTER>, or OK" : "Select surfaces, solids, and / or solid faces";   /* mccadres 6016 (0x1780) / 6015 (0x177f), FUN_1802c3f10 / FUN_1802c4f10 */
    case G_CDYN: return ds_running() ? "Select points. Press <Enter> when done" : cd_built ? "Set options, select a new surface, press <ENTER> or OK" : "Select a surface";   /* mccadres 5867 (write_msg_nl 0x16eb, FUN_180282390 before dyno_surf), 5969 (0x1751 after a build) / 5968 (0x1750) */
    case G_LNORM: return wm_ln_active() ? "Select locations to create normal lines. Press Enter to select another surface or face." : "Select a surface or face";   /* mccadres 6707 / 6706 (0x1a33 / 0x1a32) */
    case G_CFLOW: return cd_built ? "Set options, select a new surface, press <ENTER> or OK" : "Select a surface";   /* mccadres 5968 (0x1750), 5969 (0x1751 after a build: decomp/mccad_sketch.c:28600) */
    case G_STRIMS: return srf_trims_prompt(gp.method);   /* mccoreres 7439 / 7440, mccadres 4870 / 5431 (cui_srftrim.c) */
    case G_SEXT: return ds_running() ? "Slide arrow to edge to extend from" : "Select a surface to extend";   /* mccadres 7455 (0x1d1f) / 7454 (0x1d1e) */
    case G_CCONST: return ds_running() ? "Move to desired location" : cd_built ? "Set options, select a new surface, press <ENTER> or OK" : "Select a surface";   /* mccadres 5968 (0x1750), mccoreres 5444 (0x1544 write_msg in FUN_1802980f0, dyno_surf 3) */
    case G_SSOLID: return ss_selecting ? "Select body or face for generating surface geometry. Press Enter when done selecting." : "";   /* mccadres 462 */
    case G_SNSET: return srf_sn_prompt();               /* mccadutilres 1544 / 2383 (cui_srfedit.c) */
    case G_SUNTRIM: return srf_ut_prompt();             /* mccoreres 2377 */
    case G_SOFFSET: return srf_of_prompt();             /* mccadres 7420 / 7421 / 7464 (cui_srfedit.c) */
    case G_SRMBND: return ds_running() ? "Slide to boundary which is to be removed" : "Select a surface:";   /* mccadres 7425 / 4869 */
    case G_SSPH: { int t = prim_step(k); return t == 0 ? "Select base point for sphere" : t == 1 && s[k] ? "Enter radius or select point" : ""; }   /* 5921 / 5922 */
    case G_SCONE: { if (prim_top_pick) return "Enter radius or select point";   /* FUN_1800323c0 param_5 4: 5914 */
        int t = prim_step(k); return t == 0 ? "Select base point for cone" : t == 1 ? "Enter radius or select point"
                                   : t == 2 && s[k] ? "Enter height or select point" : ""; }     /* 5913 / 5914 / 5915 (FUN_1800323c0) */
    case G_STOR: { int t = prim_step(k); return t == 0 ? "Select base point for torus" : t == 1 ? "Enter major radius or select point"
                                   : t == 2 && s[k] ? "Enter minor radius or select point" : ""; }   /* 5924 / 5925 / 5926 (FUN_18002d780) */   /* RevolvedSurfaces: mccadres 7419 (0x1cfb) */
    case G_XROLL:                                       /* do_xform_roll's get_event: mccadutilres 1523 (MC l.660) */
        if (xrl_pick) return xrl_pick == 1 ? "Select the point to translate from" : "";   /* 829: mccoreres 3929 / 3930 (cui_xroll.c xrl_pick) */
        /* fall through */
    case G_OFFSETC: return geo_nchains ? "Modify dialog values or select chain to reverse it." : "";        /* do_xform_offset_ctour.c:120-126: mccadutilres 1523
                                                                                                         (FUN_18000b500(0x5f3)) while the preview is valid, else empty */
    /* mccadutil.dll commands, strings in mccadutilres.dll (formats/mccadutilres_strings.tsv) */
    case G_TRIM:                                    /* sketcher_trim_1 prompt switch (modes as Mastercam's) */
        switch (gp.mode) {
        case TR_AUTO: case TR_1: case TR_2: return k ? "Select the entity to trim/extend to" : "Select the entity to trim/extend";   /* 5949 / 5948 */
        case TR_3: return k == 0 ? "Select the first entity to trim/extend" : k == 1 ? "Select the second entity to trim/extend" : "Select the entity to trim/extend to";   /* 5950 / 5951 / 5949 */
        case TR_DIVIDE: return "Select the curve to divide / delete";                                     /* 5953 */
        case TR_POINT: return k ? "Indicate the trim/extend location" : "Select the entity to trim/extend";   /* 5952 / 5948 */
        default: return "Select the entity to trim/extend";                                               /* mode 6: 5948 */
        }
    case G_TRIMMANY: return !geo_phase ? "Select Curves to Trim or Break" : k ? "Indicate Side of Trimming Curve" : "Select Trimming Curve";   /* sketcher_trim_2: 4452 / 4454 / 4453 */
    case G_BREAKN: return "Select entities to break";                                                     /* sketcher_trim_3: 5956 */
    case G_BREAK2: return k ? "Indicate the break position" : "Select an entity to break";                /* Break2Pieces: 6028 / 6027 */
    case G_BREAKPTS: return "Press enter to end selection and complete the operation.";                  /* BreakAtPoints: 6033 */
    case G_BREAKX: return "Select entities to break, press Enter when done.";                             /* break_at_inters: 7407 */
    case G_CLOSEARC: return "Select an Arc to convert to a full circle";                                  /* close_arc: 5954 */
    case G_BRKCIRC: return "Select circles to break";                                                     /* break_circles: 5960 */
    case G_JOIN: return "Select entities to join";                                                        /* join_selected_entities: 7412 */
    case G_FILLET: return k ? "Fillet: Select another entity" : "Fillet: Select an entity";               /* sketcher_fillet_1: 0x16e7 + (n != 1) */
    default: (void)s; return "";
    }
}

/* chain_it of a chain command: the Chaining dialog comes up (cui_chain.c chain_begin_geo) and the function panel takes no
   input while it runs (Ordinal_4282 = EnableWindow(panel, 0) / (panel, 1) around chain_it: sketcher_fillet_2.c:198-201,
   sketcher_chamfer_2.c:200-203). Prompts: mccadres 4233 "Select chain" (chain_it(0x1089)); Offset Chains chain_it(0xf64)
   with the mccadutil resources current (do_xform_offset_ctour.c:47-57): mccadutilres 3940 is an empty string, so MFC's
   LoadString (FindStringResourceInstance skips empty entries) takes mccoreres 3940 "Offset: select chain" [inferred] */
static int geo_chain_append;                        /* Flat Boundary's Add (FUN_18033dcd0): the new chains join the list */
/* Offset Chains / Roll: their dialogs' +0x5090 (CXfOffsetContourDlg / CXfRollDlg share the Xform dialog base: set by FUN_18000bd80 posting
   0x66, by OnCommand FUN_18000b900 on any BN_CLICKED and by Tab / Enter; cleared by the ctor) and the Apply round. Apply 12321 = FUN_18000b7d0
   (+0x5091 := 1, then OK FUN_18000b230; decomp/xform/gaps/apply_bytes.txt); do_xform_offset_ctour.c l.144 / l.237 and do_xform_roll MX
   l.698 / l.807 loop to chain_it again while +0x5091 is set, the parameters kept (their reset blocks precede the loop). 0x65 with +0x5090
   clear is handled as 100 (offset l.160-163, MX l.704-706); Esc forces 0x65 (bVar1). Enables: Offset Chains FUN_18001db60 -> FUN_18000b030
   (+0x5090 && +0x50a4), Roll FUN_180029da0 (+0x5090 && |diameter| >= tol). Roll's later rounds post 0x66 at once (MX l.688-690: cVar11 =
   the last round's +0x5091); Offset Chains' OnInitDialog posts it in every round. */
static int geo_xmod, geo_xround2;
static void geo_chain_begin(void) {
    if (!geo_chain_append) geo_chains_free(); geo_chaining = 1;
    chain_begin_geo(geo_cmd == G_SPLCURV ? "Select chain" : geo_cmd == G_XROLL ? "Roll: select chains" : geo_cmd == G_OFFSETC ? "Offset: select chain" : geo_cmd == G_SREV ? "Select profile curve(s)" : geo_cmd == G_SDRAFT ? "Select line, arc, or spline" /* chain_it(0x1663) mccadres 5731 */ : geo_cmd == G_SEXTR ? "Select Chain of Lines and Arcs or one closed spline" /* chain_1(0x1d11) mccoreres 7441 */ : geo_cmd == G_SFLAT ? "Select chains to define flat boundary" : geo_cmd == G_SLOFT ? (gp.mode ? "Ruled surface: define contour" : "Lofted surface: define contour") : "Select chain");   /* G_SLOFT: chain_it_sync(ruled_title / loft_title: mccoreres 4868 / 4866 + 4867 + n; chain_sync_msg NOT DECODED: left out) */   /* G_SFLAT: chain_it(0x1cff) 7423 */   /* G_SREV: chain_it(0x1cfa) mccadres 7418 */
}
/* Create Letters: Top of Chain switched on the panel: into chain_it, off: out of it (cui_wfmisc.c) */
void geo_let_chain_changed(void) { if (geo_cmd != G_LETTERS) return; if (wm_let_chain() && !geo_chaining) geo_chain_begin(); else if (!wm_let_chain() && geo_chaining) { chain_dlg_done(); cam_clear_selection(); geo_chaining = 0; geo_chains_free(); } snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; }
/* the Chaining dialog's OK (the UI's cui_hooks.chain_ok while geo_chaining): chain_it returned with the chains. The
   chaining ends as run_chain_dlg's (cleanup_chaining -> FUN_1801ea2b0 writes lastchn.tmp, the chains Last loads:
   chain_keep_last; unselect_chains + EraseArrows: the chains are no longer drawn selected), then
   - Fillet / Chamfer Chains: EnableApply(chains != NULL) (manager vtable +0x30, FUN_18016adf0 / FUN_180176f50 set
     "EnableApply"), the parameters read and the worker run on the chain list (FUN_18027d7d0 create_chain_fillets /
     FUN_1802114b0 make_chamfer_chain: every chain; sketcher_fillet_2.c:204-210, sketcher_chamfer_2.c:206-212);
   - Offset Chains: the dialog and its preview of every chain (do_xform_offset_ctour.c:94-115, FUN_18001bc50). */
void geo_chains_done(void) {
    if (!geo_chaining) return;
    if (ns_chaining) { ns_chains_done(); return; }   /* Geometry Nesting's Add chained parts / sheets (cui_nest.c) */
    chain_keep_last();
    if (!geo_chain_append) geo_chains_free();
    geo_chain_append = 0;
    for (int i = 0; i < ncchains && geo_nchains < 256; i++) geo_chains[geo_nchains++] = cchains[i];   /* the list is handed over */
    ncchains = 0; overlay_dirty = 1;
    geo_chaining = 0;
    /* a new dialog: Offset Chains' OnInitDialog FUN_18001e550 sets +0x5090 and posts 0x66 (decomp/xform/gaps/oninit.c); Roll's FUN_18002b020
       clears it (as every CXf*Dlg's OnInitDialog, vt slot 95) - 0x66 only in the rounds after an Apply */
    if (geo_cmd == G_OFFSETC || geo_cmd == G_XROLL) { geo_xmod = geo_cmd == G_OFFSETC || geo_xround2; geo_xround2 = 0; xrl_pick = 0; xrl_pts_ok = 0; }   /* (+0x10500: the dialog's ctor) */
    if (geo_cmd == G_LETTERS) { if (geo_nchains) let_take_chains(); }   /* ours, LET-1: the letters on these chains built live (the live ones before moved) */
    else if (geo_nchains && (geo_cmd != G_SREV || srf_rev_axis_ok())) { geo_live_prefix = 0; geo_run(); geo_reset_picks(); }
    if (geo_cmd == G_LETTERS && !geo_chaining) { geo_chains_free(); geo_chain_begin(); }   /* create_letters' chain_it loop (FUN_18006cce0 :261) */   /* Revolve: the axis first */
    else if (geo_cmd == G_SREV) snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt());
    snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()[0] ? geo_prompt() : geo_name(geo_cmd));
}
/* the panel's tabs (TAB BasicTabItem / AdvancedTabItem, formats/ui_wireframe_panels.txt): only Bounding Box has a second */
const char *const *geo_panel_tabs(int *n) {
    static const char *const one[1] = { "Basic" }, *const two[2] = { "Basic", "Advanced" };
    if (geo_cmd == G_BBOX) { *n = 2; return two; }
    if (dr_cmd(geo_cmd)) return dr_panel_tabs(n);       /* DraftingSmartDimensionsManagerView: BasicTabItem / AdvancedTabItem */
    if (do_cmd(geo_cmd)) return do_panel_tabs(n);       /* the same panel (cui_draft_ord.c) */
    if (geo_cmd == G_XDYN) { *n = 2; return two; }      /* xformdynamicmanagerview.baml: BasicTabItem / AdvancedTabItem */
    *n = 1; return one;
}
int geo_panel_enabled(void) { if (wm_rg_wait) return 0;   /* AfxSuspendWindow during wait_for_space */ if (xf_cmd(geo_cmd)) return xf_panel_enabled(); if (dn_cmd(geo_cmd)) return 0;   /* Note: greyed (cui_draft_note.c dn_button) */
    if (geo_cmd == G_LETTERS && geo_live) return 1;      /* ours, LET-1: live letters - the panel's Apply / OK / Cancel and settings while chain_it runs again */
    return !geo_chaining && !(geo_cmd == G_BBOX && bb_selecting()); }   /* Bounding Box: greyed while its Manual
                                                                   selection runs (ours: Mastercam suspends / does not service the panel then) */
/* Offset Chains / Roll: Apply = FUN_18000b7d0 (geo_xmod above) */
int geo_panel_apply_enabled(void) { if (geo_cmd == G_LNORM) return wm_ln_enable_apply();   /* EnableApply: the view model vf+0x30(1) at each line, 0 at the selection (FUN_1803584a0), the same moments as NumberSelected */ if (xf_cmd(geo_cmd)) return xf_apply_enabled(); if (dr_cmd(geo_cmd) || do_cmd(geo_cmd)) return 1; if (geo_cmd == G_BBOX) return bb_apply_enabled();
    if (geo_cmd == G_OFFSETC) return !geo_chaining && geo_nchains && geo_xmod;                        /* FUN_18000b030 */
    if (geo_cmd == G_XROLL) return !geo_chaining && geo_nchains && geo_xmod && fabs(xrl.dia) >= mc_systol.tol;   /* FUN_180029da0 (tol_exref: config 1000) */ return !(geo_cmd == G_FILLETC || geo_cmd == G_CHAMFC) || geo_nchains > 0; }
int geo_find(const char *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; geo_unsupported[i]; i++) if (!strcasecmp(name, geo_unsupported[i])) return 0;
    for (int i = 1; i < G_NCMD; i++) if (!strcasecmp(name, gcmd[i].name) || (gcmd[i].alias && !strcasecmp(name, gcmd[i].alias))) return i;
    return 0;
}
static int geo_sext_mode;                              /* Extend: the Mode radio as last seen (code 6; geo_live_update) */
int geo_start(const char *name) {
    int c = geo_find(name);                              /* not ported (geo_unsupported / unknown): greyed in the ribbon */
    if (!c) return 0;
    if (bb_on()) bb_abort();                             /* the Bounding Box panel ends as Cancel (cui_bbox.c) */
    if (xf_on()) xf_abort();                             /* a Transform dialog ends as Cancel (cui_xform.c) */
    if (sl_cmd(geo_cmd)) sl_end();
    if (si_cmd(geo_cmd)) si_end();
    if (pln_cmd(geo_cmd)) pln_end();
    if (sp_cmd(geo_cmd)) sp_end();
    if (dr_cmd(geo_cmd)) dr_end();                       /* a drafting command's loop exit (cui_draft.c) */
    if (do_cmd(geo_cmd)) do_end();                       /* an ordinate command's exit (cui_draft_ord.c) */
    if (geo_live_prefix) geo_live_drop(); else geo_live_commit();   /* another command: the running one's loop exit commits its live result */
    if (!loaded && cui_hooks.new_part) cui_hooks.new_part();   /* nothing open: draw into a new empty part, as Mastercam always has one */
    if (!loaded) return 0;
    if (cui_hooks.geo_started) cui_hooks.geo_started();   /* the UI ends its toolpath chaining / op dialog and shows the panel's tab */
    geo_chains_free(); geo_chaining = 0; geo_panel_req = -1;
    if (geo_inited[c] != (mcam_is_metric(&F) ? 2 : 1)) {          /* first use in these units: the panel defaults (geopanel_ui.inc) */
        geo_inited[c] = mcam_is_metric(&F) ? 2 : 1;
        geo_defaults(c, mcam_is_metric(&F));
    }
    if (c == G_PROJECT && !cfg_constr_3d) gps[c].z = constr_depth;   /* mccad_util_project.c:504-506: in 2D the depth is the construction depth at every dialog init */
    geo_soft_strip(c);
    if (c == G_LINE) { gps[c].edit_pt = 2; geo_line_nsel = 0; geo_line_enapply = 0; }   /* the manager: CurrentlyEditing 2 (FUN_1800bfd20),
                                                           NumberSelected 0, EnableApply(0) (sketcher_line_1.c:506) */
    geo_cmd = c; geo_apolar_cw = 0; geo_reset_picks(); geo_phase = 0; overlay_dirty = 1; geo_applied = 0;
    ac_tmid_clear();                                     /* temp midpoints die with the previous point entry (FUN_180708450) */
    if (c == G_CVIEWS) { geo_cmd = G_NONE; wm_combine_views(); return 1; }   /* combine_views: no panel; 1986 / the message box (cui_wfmisc.c) */
    if (c == G_LETTERS) { wm_let_begin(); let_chs_free(); }   /* create_letters :36-62; ours, LET-1: no live placement yet */
    if (c == G_RGROOVE) wm_rg_begin();                   /* MakeReliefGrooves: dialog init (cui_wfmisc.c) */
    if (c == G_BBOX) bb_begin(0);                        /* FUN_180343ae0's start: settings, entity step, first compute (cui_bbox.c) */
    if (sl_cmd(c)) sl_begin(c);
    if (si_cmd(c)) si_begin(c);
    if (pln_cmd(c)) pln_begin(c);
    if (sp_cmd(c)) sp_begin(c);
    if (c == G_SPLCURV) sp_curv_reset();
    if (dr_cmd(c) && !dr_begin(c)) { geo_cmd = G_NONE; return 0; }   /* the dimension loop's set-up (cui_draft.c) */
    if (do_cmd(c) && !do_begin(c)) { geo_cmd = G_NONE; return 0; }   /* DraftApp::doOrdinate's set-up (cui_draft_ord.c) */
    if (xf_cmd(c)) xf_begin(c);                          /* do_xform_*: select_ents, the dialog (cui_xform.c) */
    geo_panel_sync(); geo_live_pv_note();
    snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()[0] ? geo_prompt() : geo_name(c));
    if (c == G_SREV) srf_rev_reset();
    if (c == G_SEXTR) { srf_extr_reset(); ex_pick_base = 0; ex_combo = gp.entmode; ex_ax[0] = 0; ex_ax[1] = 0; ex_ax[2] = 1; }   /* a new CExtrudeSurfDlg: the axis (0, 0, 1) (ctor +0x1588) */
    if (c == G_SEXT) { srf_ext_reset(); geo_sext_mode = gps[G_SEXT].mode; }   /* FUN_180337330's start: the plane local_aaa8 and its ok flag local_aba5 */
    if (c == G_STRIMS) { srf_trims_begin(); sel_clear(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); }   /* FUN_18031ed90: select_multi_surfs first */
    prim_top_pick = 0;
    if (c == G_SSOLID) { ss_selecting = 1; srf_solid_nsel = 0; }
    if (c == G_SNSET) { gps[c].pts = 0; gps[c].dir = 0; srf_sn_begin(gps[c].pts); snprintf(cam_msg, sizeof cam_msg, "%s", srf_sn_prompt()); }   /* SaveRestoreLastParams FUN_1800acb90: Show normals off (cui_srfedit.c) */
    if (c == G_SUNTRIM) { gps[c].del = 0; srf_ut_begin(gps[c].del); }   /* FUN_1802fcda0: Keep original surface off */
    if (c == G_SRMBND) srf_rb_begin();
    if (c == G_SOFFSET) {                                /* SurfaceOffsetApp::SaveRestoreLastParams FUN_1802ef320 -> FUN_1802ab110 (no dialog modality): Copy, 0.1 inch /
                                                            2.0 mm, not locked (the modal statics DAT_1806a5578..: NOT DECODED) */
        gps[c].mode = 0; gps[c].v_dist = mcam_is_metric(&F) ? 2.0 : 0.1; gps[c].lk_dist = 0;
        srf_of_begin(); snprintf(cam_msg, sizeof cam_msg, "%s", srf_of_prompt()); }
    cd_built = ce_built = ce_prompt1 = ce_ba_on = 0;   /* the curve commands' prompts start at 0x1750 / 0x177f */
    ce_sld_s = ce_sld_e = -1; ce_nsel = 0;
    if (c == G_LNORM) wm_ln_begin();
    if (geo_chain_cmd(c)) geo_chain_begin();
    return 1;
}
/* the rubber-band type of the active drawing command (the init_rubber_band argument in each command's
   decompile). Only Line Endpoints is mapped: 0 = freeform / multi-line (sketcher_line_1.c:2996-3028; the same
   switch gives 35 for the +0x33 mode, 17 for a locked length, 16 for a locked angle, and :2321-2341 gives 32
   for the arc-tangent drag - those panel modes are not ported). The arc commands' types (5 / 6 Circle Edge
   Point, 8 / 12 Circle Center Point, 20 / 8 Arc Polar, 18 Arc Endpoints / Arc 3 Points, 10 Arc Tangent -
   sketcher_arc_*.c) enable only the tangent pass with the arc-rubber-band mechanic (FUN_180131c50:611-718:
   anchor at rubber_band + 0x70, reference direction rotated 90 deg, perpendicular-foot result), whose anchor
   semantics are NOT DECODED -> no anchor modes here. Line Parallel / Perpendicular / Bisect set no rubber-band
   type in their decompiles. The masks below are decoded; with only type 0 mapped they reduce to rb == 0. */
int geo_rb_type(void) { return geo_cmd == G_LINE ? 0 : -1; }

int cfg_pt_style = 1;                                 /* main attribute point style: mcamxm.config 10002 "Point style?" */
void geo_add(int type, V3 a, V3 b, V3 c, double r, double a0, double sw) {
    if (type == E_ARC && sw < 0) { a0 += sw; sw = -sw; }                   /* arcs are stored counter-clockwise */
    int k = mcam_new_ent(&F, type, a, b, c, r, a0, sw, geo_level, geo_color);   /* undo: one step per click (geo_point) */
    if (k >= 0) attr_new_ent(k);                        /* main_attrib: style, width; points the main point style */
    doc_dirty = 1; overlay_dirty = 1;
}
void geo_line(V3 a, V3 b) { V3 z = { 0, 0, 0 }; if (hypot(b.x - a.x, b.y - a.y) > 1e-9 || fabs(b.z - a.z) > 1e-9) geo_add(E_LINE, a, b, z, 0, 0, 0); }

void geo_exec(void);
static V3 geo_last_end; static int geo_last_ok;          /* Line Endpoints, multi-line: the next line starts here */
void geo_reset_picks(void) {
    geo_npick = geo_nent = 0; overlay_dirty = 1;
    memset(geo_sz_done, 0, sizeof geo_sz_done); memset(geo_sz_picked, 0, sizeof geo_sz_picked); geo_apolar_st = 0;
}
/* a pick for the active command: a point, or an entity (e >= 0) clicked at w */
int geo_need(void) {
    /* the chain commands pick their chains in the Chaining dialog (chain_it), not here. Offset Chains then waits for an
       entity (get_event mask 0x8202 / entity mask 0x8e while the preview is valid, do_xform_offset_ctour.c:120-127): a
       chain's entity reverses that chain (FUN_18001ca60) */
    if (geo_cmd == G_SREV) return !geo_chaining && geo_nchains && !srf_rev_axis_ok() ? 'e' : 0;   /* the axis (cui_surf.c) */
    if (geo_cmd == G_XROLL && xrl_pick) return 'p';     /* 829's get_event(point) */
    if (geo_cmd == G_SEXTR && ex_pick_base) return 'p';  /* Extrude's base point (0x67) */
    if (geo_chain_cmd(geo_cmd)) return (geo_cmd == G_OFFSETC || geo_cmd == G_XROLL) && !geo_chaining && geo_nchains ? 'e' : 0;
    if (geo_cmd == G_BBOX) return bb_face_pickable() ? 'e' : 0;   /* a face of the live solid (event 0x2000, cui_bbox.c) */
    if (xf_cmd(geo_cmd)) return xf_need();               /* a dialog button's pick / Stretch's window (cui_xform.c) */
    if (sl_cmd(geo_cmd)) return sl_need();
    if (si_cmd(geo_cmd)) return si_need();
    if (pln_cmd(geo_cmd)) return pln_need();
    if (sp_cmd(geo_cmd)) return sp_need();
    if (dr_cmd(geo_cmd)) return dr_need();               /* cui_draft.c */
    if (do_cmd(geo_cmd)) return do_need();               /* cui_draft_ord.c */
    const char *s = geo_steps(); int k = geo_npick + geo_nent; return k < (int)strlen(s) ? s[k] : 0;
}
/* the live behaviour of each command, read from its decompile (formats/wireframe.md "Live results"; files in decomp/mccad_cmds):
   LV_LIVE     result created in the live colour (sktcher_live_color / store_ent(live_entity_color)) in an open undo step,
               committed (sktcher_main_color + UndoTransCommit) on the next pick, Apply (panel code 1), OK (code 0 = Done) and
               at the loop exit (another command)
   LV_REBUILD  rebuilt from the same picks when a panel value changes (UndoTransTerminate + UndoTransStart and create again,
               or replace_ent / delete_ent + create in the same step)
   LV_PREFIX   that rebuild also when a typed value now replaces the last pick(s) (the rebuild keeps the first pick: centre,
               base point or corner); without it the rebuild needs the same picks the result was made from
   LV_ESC_DROP Esc (Cancel) removes the result (UndoTransTerminate); without it Cancel leaves the loop and the loop-exit block
               commits it
   LV_PICK_MOVES the next pick rebuilds the live result at the new position instead of committing it
   LV_PREVIEW  drawn in the preview colour 15 (8 when the background is 15) instead of the live colour (config 5026)
   LV_MAINCOL  kept in the main colour (the Xform preview's colours are not decoded)
   LV_VALUES   the rebuild is made from the first pick (centre / base point) and the panel's values only, whatever the picks
               were (Ellipse FUN_18007b760 create_ellipse.c:281-291; Simple: the dialog change rebuilds the preview from the base
               point and the struct's sizes, decomp/surfcreate/prims.c :1617-1652 / :3332-3363 / :4006-4032 / :4657-4679 /
               :2441-2477 - the picks wrote their sizes into the panel)
   Not live (left as they were: each run is one committed undo step, or the command is a selection + Apply one whose live
   handling is not decoded / does not fit): Point Endpoints, Point Small
   Arcs, Bounding Box (its own preview and commit: cui_bbox.c), Line Closest, the trim / break / join commands, Offset, Offset Chains and Project (Xform preview,
   colours not decoded) */
enum { LV_LIVE = 1, LV_REBUILD = 2, LV_PREFIX = 4, LV_ESC_DROP = 8, LV_PICK_MOVES = 16, LV_PREVIEW = 32, LV_MAINCOL = 64, LV_VALUES = 128 };
static int geo_live_kind(int c) {
    switch (c) {
    case G_LNORM: return LV_LIVE | LV_REBUILD;                 /* FUN_1803584a0: each line live (sktcher_live_color in FUN_18035a3b0), committed by the next click (disasm 1803594a4-1803594bd), by Apply (9688) and at the exit (977c); panel codes 4, 6, 8 rebuild it (8c90-8cdc, 8e75-8ec1, 8f65-8fb1) */
    case G_PT_DYN: return LV_LIVE;                             /* FUN_18034aeb0 (disasm 18034bd36-18034c017): each point live (sktcher_live_color),
                                                                  committed when the next is built, on Apply, at the loop exit; the panel's code 4
                                                                  rebuild is geo_ptdyn_panel's */
    case G_POINT: return LV_LIVE;                              /* FUN_180196d20 sketcher_point_1.c:303-310 live per click, the next click
                                                                  commits :303-306; code 2 / Esc break -> exit commit :324-327; no rebuild */
    case G_PT_SEG: return LV_LIVE | LV_REBUILD;                /* FUN_18020b240 sketcher_point_4.c:426-475 Terminate + delete_ent + create
                                                                  again; new entity pick commits :288-292; code 2 -> exit commit :512-515 */
    case G_BOLT: return LV_LIVE | LV_REBUILD | LV_ESC_DROP | LV_PICK_MOVES;   /* FUN_180062970 store_ent(live_entity_color);
                                                                  FUN_1800611c0 rebuilds on a dialog change or a centre pick; FUN_180061750
                                                                  commits on 0x66 Apply / 0x68 OK; code 100 / Esc: FUN_1800642b0
                                                                  UndoTransTerminate (OnBoltCircle.c:236-239) */
    case G_LINE: return LV_LIVE | LV_REBUILD;                  /* FUN_1801913b0 sketcher_line_1.c:634-647; replace_ent rebuild
                                                                  (FUN_180190f30 / FUN_1801904e0); next pick :1589-1596; Cancel -> exit
                                                                  commit :2111-2116; multi-line commits each line at once :742-753 */
    case G_LPAR: return LV_LIVE | LV_REBUILD;                  /* FUN_18022a9b0 sketcher_line_5.c:1057-1086 Terminate / Start rebuild;
                                                                  next pick :487-495; code 2 / Esc -> commit :1012-1134 */
    case G_LPERP: return LV_LIVE | LV_REBUILD;                 /* FUN_180203280 sketcher_line_4.c:782; codes 4 / 5 :402-404, FUN_180204d10;
                                                                  next pick :490-495; codes 0 / 2 -> exit commit :902-906 */
    case G_LBIS: return LV_LIVE | LV_REBUILD | LV_ESC_DROP | (gp.single ? 0 : LV_PREFIX);    /* FUN_1801cbb30 sketcher_line_3.c:1724-1731; code 5 rebuild :523-544; next
                                                                  pick :328-338; code 2 -> delete_ent + UndoTransTerminate :473-476, :580-590 */
    case G_LTAN: return LV_LIVE | LV_REBUILD;                  /* FUN_180272630 sketcher_line_6.c:485-490; rebuild :417-419, :600-628; next
                                                                  pick :361-363; codes 0 / 2 -> exit commit :722-731 */
    case G_CIRCLE: return LV_LIVE | LV_REBUILD | LV_PREFIX;    /* FUN_1801bf090 sketcher_arc_2.c:325-330; codes 5 / 6 FUN_1801999a0
                                                                  replace_ent keeping the centre; next pick :306-309; Esc :600-605 commit */
    case G_ARC3: return LV_LIVE;                               /* FUN_1801b90b0 sketcher_arc_5.c:1541-1548; panel codes 4-8 only prompts
                                                                  :438-450; next pick FUN_1801b88d0 :801-806; Esc / code 2 commit :597-602 */
    case G_ATAN: return LV_LIVE | LV_REBUILD | LV_PREFIX;             /* FUN_1801b4b60 sketcher_arc_6.c:443 / :493 / :543 / :690; codes 4 / 5
                                                                  rebuild :819-830, :413-419; next pick :363-367; Esc :851-856 commit;
                                                                  modes 4 / 5 commit at once :570-617 (geo_live_start); the candidate
                                                                  arcs show live before the SelectArcToKeep pick (FUN_1801997f0) */
    case G_CEDGE: return LV_LIVE;                              /* FUN_1801c51b0 sketcher_arc_1.c:765-771 / :887-894; the radius rebuild is
                                                                  for the tangent types only (:1250-1252); next pick :712-731; Esc :1051-1056 */
    case G_AEND: return LV_LIVE | LV_REBUILD;                  /* FUN_1801a7970 sketcher_arc_4.c:401-403; codes 8 / 9 FUN_1801a9280
                                                                  :627-631; next pick :353-357; Esc :866-871 commit */
    case G_APOLAR: return LV_LIVE | LV_REBUILD;                /* FUN_1801afae0: FUN_1801ae880 sketcher_arc_3.c:3173-3180; codes 5-9
                                                                  replace_ent :712-816; next centre pick :2589-2592; Esc :906-911 commit */
    case G_APOLEND: return LV_LIVE | LV_REBUILD;               /* FUN_1801abb30 sketcher_arc_7.c:377-407; codes 7-10 Terminate / Start
                                                                  :305-307; next pick :338-343; Esc :508-512 commit */
    case G_RECT: return LV_LIVE | LV_REBUILD | LV_PREFIX;      /* FUN_1801e3ca0 sketcher_rect.c:558-559 / :630-631 rect_set_rents_color
                                                                  (live); FUN_1801e3af0 rebuild (create_rectangle1(base)); next corner click
                                                                  FUN_1801e34a0 :590-600; Esc :399 -> main colour :862-863 */
    case G_RSHAPE: return LV_LIVE | LV_REBUILD | LV_PREFIX;    /* create_rectangle.c:381-382 / :441-442; FUN_180082950 rebuild :659-683;
                                                                  next base click :393-394; case 100 / exit :484-493 main colour */
    case G_POLY: return LV_LIVE | LV_REBUILD | LV_PREFIX;      /* create_polygon.c FUN_18007e7f0 :772; FUN_18007e960 rebuild from the
                                                                  centre; next centre click :249-252; case 100 -> exit commit :293-301 */
    case G_ELLIPSE: return LV_LIVE | LV_REBUILD | LV_VALUES;   /* create_ellipse.c FUN_18007b380 :1165-1200; FUN_18007b760 rebuild; next
                                                                  centre click :204-209; case 100 -> exit commit :351-361 */
    case G_HELIX: return LV_LIVE | LV_REBUILD;                 /* create_helix.c FUN_180088e00 :854-905; 0x6f rebuild :128-136; next base
                                                                  click :215-222; exit commit :289-308 */
    case G_SPIRAL: return LV_LIVE | LV_REBUILD;                /* create_spiral.c FUN_180089040 :544 / :606; 0x65 rebuild :126-133; next
                                                                  base click :215-221; exit commit :284-303 */
    case G_FILLET: return LV_LIVE | LV_REBUILD;                /* FUN_18026c370 sketcher_fillet_1.c:524-541; codes 4-7 rebuild :459; next
                                                                  pick :318-326; codes 0 / 2 commit :428-436, exit :608-611 */
    case G_CHAMF: return LV_LIVE | LV_REBUILD;                 /* FUN_180218910 mccad_sketch.c: the loop exit commits for panel codes 0 and 2
                                                                  (Esc included), as the other sketcher commands */
    case G_FILLETC: return LV_LIVE | LV_REBUILD | LV_PREVIEW;  /* FUN_18027d7d0 sketcher_fillet_2.c:453-506 (colour 0xf / 8); codes 5-9
                                                                  rebuild :286-291; commit LAB_18027d44e :247-268 on codes 0 / 1 / 2 and
                                                                  get_event != 0 (:237) */
    case G_CHAMFC: return LV_LIVE | LV_REBUILD | LV_PREVIEW;   /* FUN_1802114b0 sketcher_chamfer_2.c:460-507 (main_color 0xf / 8 for the
                                                                  preview); codes 5-7 rebuild :286-291; commit LAB_1802111df :250-273 */
    case G_SBLOCK: case G_SCYL: case G_SSPH: case G_SCONE: case G_STOR: return LV_LIVE | LV_REBUILD | LV_MAINCOL | LV_VALUES;   /* the panel's changes rebuild (FUN_180010830 preview; ours: the
                                                           surfaces themselves, in their colours); the next base point / OK / Apply commit */
    case G_CEDGE1: return LV_LIVE | LV_REBUILD | LV_MAINCOL;   /* FUN_1802a2940: the Constant Parameter loop (sktcher_live_color; panel changes rebuild: FitArcsAndLines; the next pick / Apply / OK commit) */
    case G_CEDGEA: return LV_LIVE | LV_REBUILD | LV_MAINCOL;   /* FUN_1802c3f10: live after FUN_1802c4a70, options rebuild, OK / Apply commit */
    case G_CDYN: return LV_LIVE | LV_REBUILD | LV_MAINCOL;   /* FUN_180282390: the same loop as Constant Parameter (sktcher_live_color; panel changes rebuild from the same points; the next pick / Apply / OK commit) */
    case G_CFLOW: return LV_LIVE | LV_REBUILD | LV_MAINCOL;   /* FUN_180287870 sktcher_live_color after UndoTransStart; the same loop as Constant Parameter */
    case G_STRIMS: return LV_LIVE | LV_REBUILD | LV_MAINCOL;   /* FUN_1800903e0 (slot [2]): UndoTransStart, the build, FUN_180090020 (sktcher_live_color);
                                                            a panel change UndoTransTerminate + the build again; codes 0 / 1 / 2 and the exit (slot [6]) UndoTransCommit */
    case G_SEXT: return LV_LIVE | LV_REBUILD | LV_MAINCOL;   /* FUN_180337330: the build in an open transaction (no live colour); panel
                                                            changes UndoTransTerminate + rebuild on the same surface / edge; the next pick,
                                                            OK / Apply and Esc UndoTransCommit */
    case G_CCONST: return LV_LIVE | LV_REBUILD | LV_MAINCOL;   /* FUN_1802980f0: sktcher_live_color after the build; panel changes rebuild (no dyno, the same uv); the next pick / Apply / OK UndoTransCommit + sktcher_main_color */
    case G_SSOLID: return LV_LIVE | LV_REBUILD | LV_MAINCOL | LV_ESC_DROP;   /* FUN_180310fa0 builds in a transaction; the attribute changes
                                                           re-run FUN_180312040; OK / Apply FUN_180310e60 commits, Cancel UndoTransTerminate */
    case G_SLOFT: return LV_LIVE | LV_REBUILD | LV_MAINCOL;   /* the panel's Type rebuilds; OK / OK-New commit */
    case G_SFLAT: return LV_LIVE | LV_REBUILD | LV_MAINCOL;   /* FUN_18033f400: sktcher_live_color; OK / OK-New FUN_18033dd60 commit
                                                           (UndoTransCommit + sktcher_main_color), Cancel FUN_18033ddb0 UndoTransTerminate */
    case G_SEXTR: return LV_LIVE | LV_REBUILD;          /* ExtrudeSurface: UndoTransStart, FUN_18001f210 (live colour); 0x68 / 0x69 (panel changes) undo_go_back +
                                                           FUN_1800254a0 + rebuild; 100 / 0x65 FUN_180017520 (main colour) + UndoTransCommit */
    case G_SDRAFT: return LV_LIVE | LV_REBUILD;         /* DraftSurface: UndoTransStart, FUN_180125d80, sktcher_live_color; 0x6b / 0x67 (panel changes) undo_go_back +
                                                           rebuild; 100 / 0x65 (OK / Apply) / Esc (OnCancel posts 100) UndoTransCommit + sktcher_main_color */
    case G_SREV: return LV_LIVE | LV_REBUILD;           /* FUN_18008c9e0 sktcher_live_color; panel changes UndoTransTerminate + rebuild; OK / Esc commit (FUN_18008ca80) */
    case G_LETTERS: return LV_LIVE | LV_ESC_DROP | LV_PICK_MOVES;   /* Mastercam: FUN_18006c0b0 UndoTransCommit per placement, no preview. Ours, user
                                                                  decision LET-1 (approved deviation): live until OK, the next placement moves it,
                                                                  Esc drops it, Apply rebuilds it (geo_let_apply) */
    case G_RGROOVE: return LV_LIVE | LV_ESC_DROP | LV_MAINCOL;   /* Preview Groove: FUN_180076420 (delete_all_by_mask); OK: one transaction, the preview is built again */
    case G_SPLCURV: return LV_LIVE | LV_REBUILD | LV_MAINCOL;   /* FUN_1801fc2a0: UndoTransStart per build, Terminate on a panel change, OK / Apply / Cancel commit */
    case G_XROLL: return LV_LIVE | LV_REBUILD | LV_ESC_DROP | LV_MAINCOL;   /* FUN_18002bf70's preview: the results keep their source's
                                                                  attributes; 0x66 rebuild, 0x65 commit, 100 delete (cui_xroll.c) */
    case G_OFFSETC: return LV_LIVE | LV_REBUILD | LV_ESC_DROP | LV_MAINCOL;   /* do_xform_offset_ctour: Xform preview of every chain
                                                                  (FUN_18001bc50, UndoXformTransStart); 0x66 = delete + rebuild (FUN_18001cae0);
                                                                  0x65 commits (FUN_18001c890); 100 deletes the preview (formats/wireframe.md
                                                                  "Live results"); the preview colours are NOT DECODED: main colour */
    case G_BREAKN: return LV_LIVE | LV_REBUILD;            /* FUN_1800b0330 sketcher_trim_3.c: the pieces sktcher_live_color in an open
                                                                  UndoTransStart; codes 4-9 UndoTransTerminate + FUN_1800afcc0 again; codes
                                                                  0 / 1 and the loop exit (code 2 / Esc too) commit: sktcher_main_color +
                                                                  FUN_1800af8d0 + UndoTransCommit; code 1 then select_ents again */
    default: return 0;
    }
}
void geo_run(void) {                               /* the picks are complete: run the command (one undo step) */
    if (geo_live_prefix) geo_live_drop(); else geo_live_commit();   /* prefix candidates are replaced by the chosen result */
    int from = F.nents, nu = nundo;
    GeoIn *in = &geo_in_now; in->npick = geo_npick; in->nent = geo_nent;
    for (int k = 0; k < 4; k++) { in->pick[k] = geo_pick[k]; in->ent[k] = geo_ent[k]; in->entpt[k] = geo_entpt[k]; }
    geo_live_n = 0; geo_live_marked = 0;
    geo_exec();
    geo_soft_clear_at(geo_rebuilding ? SC_REBUILD : geo_cmd == G_AEND && F.nents == from ? SC_FAIL : SC_MADE);   /* each command's soft-lock
                                                           clearing after a build (geo_soft_clear_at; Arc Endpoints' failed make :435) */
    if (nundo == nu) undo_ents_added(from);              /* commands that only add: one undo step per run */
    if (nundo != nu) geo_live_start(from);
    if (geo_chain_cmd(geo_cmd)) {                       /* the loops show their prompt again at every get_event (sketcher_fillet_2.c:214,
                                                           sketcher_chamfer_2.c:216, do_xform_offset_ctour.c:120-127) */
        geo_chain_hash = geo_param_hash();
        snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()[0] ? geo_prompt() : geo_name(geo_cmd));
    }
    scene_reupload();
}
/* ours: the wireframe functions here work in the XY plane (ent_ge); an arc in another plane (Ent.vw) is refused with this */
static void geo_view_arc_msg(void) { snprintf(cam_msg, sizeof cam_msg, "An arc in another plane: this function works in the XY plane here (NOT PORTED)"); }
static int sel_view_arc(const int *l, int n) { for (int i = 0; i < n; i++) if (F.ents[l[i]].type == E_ARC && F.ents[l[i]].vw) return 1; return 0; }
void geo_take(int e, V3 w) {
    if (sl_cmd(geo_cmd)) { sl_take(e, w); return; }
    if (si_cmd(geo_cmd)) { si_take(e, w); return; }
    if (pln_cmd(geo_cmd)) { pln_take(e, w); return; }
    if (geo_cmd == G_LNORM) { wm_ln_take(e, w); return; }   /* FUN_1803584a0 (cui_wfmisc.c) */
    if (geo_cmd == G_CDYN) {                              /* FUN_180282390: event 2 starts dyno_surf (mode 5) on the surface (the rest goes through geo_click / the events) */
        if (ds_running()) return;
        if (e >= 0 && e < F.nents && F.ents[e].type == E_SURF) geo_live_commit();   /* event 2: the last result committed before the arrow (FUN_180282390, ed_cad1.c local_e850 == 2) */
        if (!cdyn_take(e, w)) { if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return; }
        geo_ent[0] = e; geo_entpt[0] = w; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return;
    }
    if (sp_cmd(geo_cmd)) { sp_take(e, w); return; }      /* the spline family (cui_spline.c) */
    if (geo_cmd == G_CFLOW && (e < 0 || e >= F.nents || F.ents[e].type != E_SURF)) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); return; }
    if (geo_cmd == G_CFLOW && !srf_crv_supported(e, cam_msg, sizeof cam_msg)) { geo_reset_picks(); overlay_dirty = 1; return; }
    if (geo_cmd == G_SRMBND) {                            /* RemoveBoundary: select_surface = the surface, then dyno_surf mode 3 without the AutoCursor (cui_srfedit.c) */
        if (ds_running()) { double p[3] = { w.x, w.y, w.z }, uu[2]; ds_uu(p, uu); if (ds_event(0xe, uu, NULL)) geo_ds_done(); return; }
        if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); return; }
        if (!srf_rb_begin_dyno(e, w, cam_msg, sizeof cam_msg)) { overlay_dirty = 1; return; }
        geo_ent[0] = e; geo_entpt[0] = w; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return;
    }
    if (geo_cmd == G_CEDGE1) {                            /* FUN_1802a2940: a surface (mask 0x100; a solid edge, event 0x4000: geo_click / ce_pick_edge), then dyno_surf mode 3, param_4 0 at the pick's (u, v) */
        if (ds_running()) { double p[3] = { w.x, w.y, w.z }, uu[2]; ds_uu(p, uu); if (ds_event(0xe, uu, NULL)) geo_ds_done(); return; }
        if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); return; }
        geo_live_commit();                                /* event 2: the last result committed before the arrow (ed_cad1.c:822-842) */
        if (!srf_crv_edge_supported(e, cam_msg, sizeof cam_msg)) { geo_reset_picks(); overlay_dirty = 1; return; }
        if (!srf_crv_edge_begin(e, w, cam_msg, sizeof cam_msg)) { overlay_dirty = 1; return; }
        geo_ent[0] = e; geo_entpt[0] = w; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return;
    }
    if (geo_cmd == G_SEXT) {                              /* FUN_180337330: a surface (mask 0x100): the last result committed, then dyno_surf mode 3, param_4 1 from the pick's (u, v) (cui_srftrim.c) */
        if (ds_running()) { double p[3] = { w.x, w.y, w.z }, uu[2]; ds_uu(p, uu); if (ds_event(0xf, uu, p)) geo_ds_done(); return; }
        if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); return; }
        geo_live_commit();
        if (!srf_ext_supported(e, cam_msg, sizeof cam_msg)) { geo_reset_picks(); overlay_dirty = 1; return; }
        if (!srf_ext_begin(e, w, cam_msg, sizeof cam_msg)) { overlay_dirty = 1; return; }
        geo_ent[0] = e; geo_entpt[0] = w; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return;
    }
    if (geo_cmd == G_CCONST) {                            /* FUN_1802980f0: a surface (mask 0x100), then dyno_surf mode 3, param_4 1 (the start at the pick's (u, v) as Curve Dynamic's); a geopick while it runs is a click on the AutoCursor at w (tests) */
        if (ds_running()) { double p[3] = { w.x, w.y, w.z }, uu[2]; ds_uu(p, uu); if (ds_event(0xf, uu, p)) geo_ds_done(); return; }
        if (e != DS_TEMP && (e < 0 || e >= F.nents || F.ents[e].type != E_SURF)) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); return; }
        geo_live_commit();                                /* event 2: the last result committed before the arrow (ed_cad2.c:252-270) */
        if (!srf_crv_supported(e, cam_msg, sizeof cam_msg)) { geo_reset_picks(); overlay_dirty = 1; return; }
        if (!srf_crv_const_begin(e, w, cam_msg, sizeof cam_msg)) { overlay_dirty = 1; return; }
        geo_ent[0] = e; geo_entpt[0] = w; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return;
    }
    if (dr_cmd(geo_cmd) || do_cmd(geo_cmd)) return;      /* the drafting picks come through geo_click (cui_draft.c dr_click, cui_draft_ord.c do_click) */
    if (geo_cmd == G_LETTERS && e >= 0 && wm_let_arcpick(e, w)) { if (wm_let_take_cancelled()) { geo_end(); return; } scene_reupload(); return; }
    if (geo_cmd == G_LETTERS && !geo_need()) return;     /* ours, LET-1: Top of Chain takes no point - the live letters stay as they are */   /* FUN_18006d450: a cancelled width prompt ends the loop */
    if (geo_cmd == G_PTNODES && (e < 0 || e >= F.nents || F.ents[e].type != E_SPLINE)) return;   /* select_ent mask 0x88 */
    if (geo_cmd == G_SCONE && prim_top_pick) {           /* the cone's Top radius pick (FUN_180032250 param_2 4): type 8 centred at base + height x
                                                           axis in the cone's view; > tol -> +0x18, Top radius mode (+0x2a 1) */
        int ac = geo_snap_ent != 0 || geo_pick_ac_next; geo_pick_ac_next = 0; prim_top_pick = 0;
        V3 b = geo_live ? geo_live_in.pick[0] : geo_pick[0];
        double A[3], X[3], Y[3]; srf_prim_frame(3, gp.cmode, A, X, Y);
        double d[3] = { w.x - (b.x + gp.v_h * A[0]), w.y - (b.y + gp.v_h * A[1]), w.z - (b.z + gp.v_h * A[2]) }, r;
        if (ac) { double t = d[0] * A[0] + d[1] * A[1] + d[2] * A[2]; r = sqrt(fmax(0, d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - t * t)); }
        else r = hypot(d[0] * vmat[0] + d[1] * vmat[1] + d[2] * vmat[2], d[0] * vmat[3] + d[1] * vmat[4] + d[2] * vmat[5]);
        if (r > mc_systol_units(mcam_is_metric(&F))->tol && r < 1e38) { gp.v_w = r; gp.method = 1; }
        snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); geo_live_update(); overlay_dirty = 1;
        return;
    }
    if (xf_cmd(geo_cmd)) { if (xf_need()) xf_take(e, w); if (geo_cmd) snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()[0] ? geo_prompt() : geo_name(geo_cmd)); overlay_dirty = 1; return; }
    if (geo_cmd == G_SREV) {                             /* Revolve: the axis line (FUN_18008cc40), then the build (FUN_18008c9e0) */
        if (geo_need() != 'e' || !srf_rev_take(e, w)) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); return; }
        geo_run(); geo_reset_picks(); overlay_dirty = 1;
        return;
    }
    if (geo_cmd == G_SEXTR && ex_pick_base) {          /* get_event event 1: farther than tol from the base -> the base, rebuilt; else nothing (the
                                                           result stays removed, as Mastercam's undo_go_back of 0x67) */
        ex_pick_base = 0;
        if (srf_extr_base(w)) { geo_live = 0; geo_live_n = 0; geo_run(); geo_reset_picks(); }
        snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1;
        return;
    }
    if (geo_cmd == G_XROLL && xrl_pick) {              /* 829 (cui_xroll.c): the two points, then 0x66 */
        static double p1[3];
        if (xrl_pick == 1) { p1[0] = w.x; p1[1] = w.y; p1[2] = w.z; xrl_pick = 2; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return; }
        memcpy(xrl.p1, p1, sizeof p1); xrl.p2[0] = w.x; xrl.p2[1] = w.y; xrl.p2[2] = w.z;
        xrl_pick = 0; xrl_pts_ok = 1; xrl.twopt = 1; geo_xmod = 1;
        if (geo_live && geo_live_valid()) geo_live_drop(); else { geo_live = 0; geo_live_n = 0; }
        geo_run(); geo_reset_picks(); overlay_dirty = 1;
        return;
    }
    if (geo_cmd == G_OFFSETC || geo_cmd == G_XROLL) {   /* Offset Chains: event 2 on an entity -> FUN_18001ca60; Roll: FUN_18002cbf0 (MC l.5306) */
        if (!geo_need()) return;
        /* the first chain (list order) holding the entity is reversed (reverse_chain) and the preview rebuilt
           (FUN_18000bd80(0x66) -> FUN_18001cae0: XfDeletePreview + FUN_18001bc50), do_xform_offset_ctour.c:186-189 */
        for (int i = 0; i < geo_nchains; i++) for (int k = 0; k < geo_chains[i].n; k++) if (geo_chains[i].ent[k] == e) {
            chain_reverse(&geo_chains[i]); geo_xmod = 1;     /* FUN_18000bd80(0x66): +0x5090 */
            if (geo_live && geo_live_valid()) geo_live_drop(); else { geo_live = 0; geo_live_n = 0; }
            geo_run(); geo_reset_picks(); overlay_dirty = 1;
            return;
        }
        return;
    }
    if (geo_cmd == G_LINE && gp.edit_pt != 2) { geo_line_edit_click(w); return; }   /* CurrentlyEditing One / Two (sketcher_line_1.c:1240-1466) */
    int had_live = geo_live;
    if (geo_live && geo_nent == 0 && geo_npick == 0) {   /* the next pick commits the live result */
        if ((geo_live_kind(geo_cmd) & LV_PICK_MOVES) || geo_live_prefix) geo_live_drop(); else geo_live_commit();   /* Bolt Circle: FUN_1800611c0; prefix candidates are just choosable previews */
        geo_soft_clear_at(SC_NEXT);                      /* Circle Center Point :319, Arc Endpoints :367, Rectangle :600 */
        if (geo_cmd == G_LINE) geo_line_enapply = 0;     /* sketcher_line_1.c:1595 EnableApply(0) */
    }
    int need = geo_need(); if (!need) return;
    geo_applied = 0;                                     /* a new pick: OK may apply again */
    if (need == 'p') {
        /* Line Endpoints' second point: the rubber band's point when one of length / angle is locked (hard or soft) in Freeform
           (sketcher_line_1.c:1240-1290 -> the rubber band FUN_18018f8c0); no point -> the click is not taken */
        if (geo_cmd == G_LINE && geo_npick == 1 && !gp.horiz && !gp.vert && !(gp.lk_len && gp.lk_ang) && (gp.lk_len || gp.lk_ang)
            && !geo_line_rb(geo_pick[0], w, gp.v_ang * M_PI / 180, gp.v_len, gp.lk_ang != 0, gp.lk_len != 0, &w)) return;
        geo_pick_ac[geo_npick] = geo_snap_ent != 0 || geo_pick_ac_next; geo_pick_ac_next = 0; geo_pick[geo_npick++] = w;
        if (geo_cmd == G_LINE) { geo_line_lastpt = w; geo_line_nsel = geo_npick >= 2 ? 2 : 1; }   /* NumberSelected 1 / 2 (vf+0x90, disasm 1801923a6-1801923bf) */
        if (geo_cmd == G_APOLAR && geo_npick == 2 && !(gp.lk_r && gp.lk_sa)) geo_apolar_st = 1;   /* the start point taken (state 1 -> 2) */
        if (geo_sz_cmd(geo_cmd) && geo_npick == 1) {   /* the centre / base click: the sizes known from the panel */
            for (int z = 1; z <= 2; z++) { geo_sz_done[z] = geo_sz_lockdone(z, ~0); geo_sz_picked[z] = 0; }
            if (geo_cmd != G_ELLIPSE && !(geo_sz_done[1] && geo_sz_done[2])) {
                /* Simple: not everything known -> resetLock, the struct read again: only the hard locks count (prims.c base clicks) */
                geo_soft_clear_at(SC_FIRST);
                for (int z = 1; z <= 2; z++) geo_sz_done[z] = geo_sz_lockdone(z, GEO_LK_HARD);
            }
            if (geo_cmd == G_ELLIPSE) geo_soft_clear_at(SC_FIRST);   /* create_ellipse.c :211, the flags from the struct read before it */
        } else if (geo_sz_cmd(geo_cmd) && geo_npick > 1) {   /* a size click: the first size still to know (create_ellipse.c :186-197) */
            for (int z = 1; z <= 2; z++) if (!geo_sz_done[z]) { geo_sz_done[z] = geo_sz_picked[z] = 1; break; }
        }
    }
    else if (need == 'e') {
        if (e < 0 || e >= F.nents || !F.ents[e].type) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()[0] ? geo_prompt() : geo_name(geo_cmd)); return; }
        if (F.ents[e].type == E_ARC && F.ents[e].vw) { geo_view_arc_msg(); return; }
        geo_ent[geo_nent] = e; geo_entpt[geo_nent++] = w; overlay_dirty = 1;
        if (geo_cmd == G_PT_DYN) {                       /* the measuring end: the one nearest the pick (FUN_18034d080: pick parameter > the middle) */
            GE g = ent_ge(e); geo_ptdyn_far = ge_param(&g, xy(w)) > 0.5; geo_ptdyn_pv = gp.v_dist; geo_ptdyn_plk = gp.lk_dist & GEO_LK_HARD;
        }
    }
    if (geo_cmd == G_RSHAPE && geo_npick == 1 && geo_need()) geo_soft_clear_at(SC_FIRST);   /* create_rectangle.c :417 (made at once: :446) */
    geo_pend_hash = geo_param_hash();
    if (!geo_need()) {
        int prim_once = geo_cmd >= G_SBLOCK && geo_cmd <= G_STOR && geo_npick == 1 && !had_live;
        int pd_e = geo_ent[0]; V3 pd_w = geo_entpt[0];
        geo_run(); geo_reset_picks();
        if (geo_cmd == G_LETTERS) let_chs_free();       /* ours, LET-1: a point placement now (the chains of moved Top of Chain letters dropped) */
        if (geo_cmd == G_LETTERS && wm_let_take_cancelled()) { geo_end(); return; }   /* FUN_18006d450: a cancelled width prompt ends the loop */
        if (geo_cmd == G_PTNODES) { geo_end(); return; }   /* create_spline_points: one spline, then the command ends (cui_wfmisc.c) */
        if (geo_cmd == G_PT_DYN) { geo_ent[0] = pd_e; geo_entpt[0] = pd_w; geo_nent = 1; geo_ptdyn_pv = gp.v_dist; geo_ptdyn_plk = gp.lk_dist & GEO_LK_HARD; }   /* the entity stays picked: more
                                                           clicks, more points (disasm 18034bd36) */
        /* geo_run drops SelectArcToKeep's candidates (geo_live_prefix): the arc kept is made,
                                                           the others delete_ent (decomp/mccad_arctan_helpers.c:93-133) */
        geo_live_prefix = 0;
        if (prim_once) {                                 /* Simple: nothing live and every size known at the base click -> the preview built
                                                           and made at once (prims.c :3207-3239: FUN_180028b60 then FUN_1800270e0; Block / Sphere /
                                                           Cone / Torus the same at their base clicks), then resetLock (:3255) */
            geo_live_commit(); geo_soft_clear_at(SC_FIRST);
        }
        if (geo_cmd == G_LINE && gp.cont && geo_last_ok && !(gp.len > 0)) { geo_pick[0] = geo_last_end; geo_npick = 1; }
        geo_last_ok = 0;
    } else if ((geo_cmd == G_ATAN || geo_cmd == G_AEND || (geo_cmd == G_LBIS && !gp.single)) && !geo_live) {   /* the candidate arcs go live once the defining picks are in
                                                           (SelectArcToKeep, FUN_1801997f0); the keep-pick then picks one */
        geo_cand_show();
    }
    if (geo_cmd) snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()[0] ? geo_prompt() : geo_name(geo_cmd));
    overlay_dirty = 1;
}
/* the SelectArcToKeep candidates once their defining picks are in: Arc Tangent modes 0 / 1 / 2 (sketcher_arc_6.c), Arc Endpoints with
   the radius locked and > tol after the two endpoints (sketcher_arc_4.c :382-400 -> FUN_1801a7330; also codes 8 / 9 with both
   endpoints picked, :664-665 -> LAB_1801a8867). Arc Endpoints' candidates are main-colour entities (store_ent with main_color) */
static void geo_cand_show(void) {
    {
        int okp = geo_cmd == G_LBIS ? geo_nent == 2 : geo_cmd == G_AEND ? geo_npick == 2 && gp.lk_r && gp.v_r > mc_systol.tol
                : (gp.mode == 2 && geo_nent == 2) || ((gp.mode == 0 || gp.mode == 1) && geo_nent == 1 && geo_npick == 1);
        if (okp) {
            int from = F.nents, nu = nundo;
            GeoIn *in = &geo_in_now; in->npick = geo_npick; in->nent = geo_nent;
            for (int k = 0; k < 4; k++) { in->pick[k] = geo_pick[k]; in->ent[k] = geo_ent[k]; in->entpt[k] = geo_entpt[k]; }
            geo_live_n = 0; geo_live_marked = 0;
            geo_exec();
            if (geo_cmd == G_ATAN) geo_soft_clear_at(SC_MADE);   /* FUN_1801b4af0 after the creation (sketcher_arc_6.c :466 / :516 / :566) */
            if (nundo == nu) undo_ents_added(from);
            if (nundo != nu) { geo_live_start(from); geo_live_prefix = 1; geo_live_in = *in; }
            if (geo_cmd == G_AEND) for (int k = 0; k < geo_live_n; k++) { int i = geo_live_l[k]; if (i >= 0 && i < F.nents && F.ents[i].type) F.ents[i].color = geo_color; }
            geo_live_hash = geo_param_hash();
            scene_reupload();
        }
    }
}
void geo_point(V3 p) { geo_take(-1, p); }

void geo_end(void) {
    if (bb_on()) bb_abort();                              /* the Bounding Box panel left without its OK: Cancel (cui_bbox.c) */
    if (xf_on()) xf_abort();                              /* a Transform dialog left without OK / Cancel: Cancel (cui_xform.c) */
    if (dr_cmd(geo_cmd)) dr_end();                        /* the dimension loop's exit (cui_draft.c) */
    if (do_cmd(geo_cmd)) do_end();                        /* doOrdinate's exit (cui_draft_ord.c) */
    if (geo_live_prefix) geo_live_drop(); else geo_live_commit();
    if (geo_cmd == G_LETTERS) let_chs_free();            /* ours, LET-1 */
    if (geo_chaining) { chain_dlg_done(); cam_clear_selection(); geo_chaining = 0; }   /* chain_it cancelled: cancel_chaining, the mode kept (run_chain_dlg LAB_1801f1afa) */
    if (ns_chaining) ns_chain_end();                     /* Geometry Nesting's chain_it cancelled: back to its property sheet (cui_nest.c) */
    geo_chains_free(); geo_panel_req = -1;               /* free_chains(&local_4d0) at the loop exit (sketcher_fillet_2.c:306-309) */
    geo_soft_strip(geo_cmd);                             /* the soft locks end with the panel (geo_soft_strip) */
    if (geo_cmd == G_SNSET) srf_sn_end();                /* FUN_180110e60 (cui_srfedit.c) */
    if (geo_cmd == G_SUNTRIM) srf_ut_end();              /* UndoTransCommit */
    if (geo_cmd == G_SRMBND) srf_rb_end();
    if (geo_cmd == G_SOFFSET) srf_of_end();              /* FUN_1802f0410 (cui_srfedit.c) */
    if (geo_cmd == G_LNORM) wm_ln_end();
    geo_cmd = G_NONE; geo_reset_picks(); geo_phase = 0; geo_live_prefix = 0; overlay_dirty = 1; cam_msg[0] = 0; ac_tmid_clear();
    geo_xmod = geo_xround2 = 0;                          /* Offset Chains / Roll: the dialog's +0x5090 / the Apply round end with the command (the next
                                                            command's first dialog starts from its ctor / OnInitDialog: geo_chains_done) */
}
/* the document is closed or replaced (File > New / Open / Close: scene.c scene_part_closing, before the part is freed): the running
   command ends on the document it was started on - its entity indices (a Note being dragged, live results, picks, chains, the
   Transform lists) mean nothing in the next one (a crash / a part entity turned off by the next click's undo step otherwise; ours:
   File > New / Open are commands of their own in Mastercam, so the command running before them has ended). An open Analyze dialog
   is cancelled the same way. */
static void geo_part_closing(void) {
    if (az_i >= 0) az_close(0);
    analyze_pick = 0;
    if (geo_cmd || geo_chaining) geo_end();
}
__attribute__((constructor)) static void geo_closing_hook(void) { scene_part_closing = geo_part_closing; }
int geo_panel_layout;
static void geo_chain_button(int code);
/* the end of a dyno_surf (Enter, or Cancel / Esc: the same exit 0xb): FUN_180282390 goes on with the points it has */
static void geo_ds_done(void) {
    if (geo_cmd == G_CDYN) {
        cdyn_collect();
        if (cdyn_points() >= 2) { geo_nent = 1; geo_run(); }   /* "1 < sVar8"; fewer points: nothing, no message (geo_nent: the input of the live rebuild) */
        cd_built = 1; geo_reset_picks(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1;
    } else if (geo_cmd == G_CEDGE1) {                     /* FUN_1802a2940: a click builds (MakeSurfaceEdgeCurve at the arrow's (u, v)); Enter / Cancel: nothing, prompt 0x1750 */
        if (!ds_last_aborted()) { ce_sld_s = -1; ds_uv(&cc_uv[0], &cc_uv[1]); geo_nent = 1; geo_run(); ce_prompt1 = 1; ce_ba_on = geo_ent[0] >= 0 && geo_ent[0] < F.nents && F.ents[geo_ent[0]].type == E_SURF && F.surfs[F.ents[geo_ent[0]].srf].kind == 10; } else ce_prompt1 = ce_ba_on = 0;
        geo_reset_picks(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1;
    } else if (geo_cmd == G_SRMBND) {                     /* RemoveBoundary: Esc in dyno_surf ends the command (local_2bf7 0); a click: the removal */
        if (ds_last_aborted()) { geo_end(); return; }
        double u, v; ds_uv(&u, &v); int e = geo_ent[0];
        geo_reset_picks();
        if (!srf_rb_exec(e, u, v)) { geo_end(); return; }
        if (strncmp(cam_msg, "Remove Boundary:", 16)) snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1;
    } else if (geo_cmd == G_STRIMS) {                     /* slot [5] FUN_18008eb40: dyno_surf mode 3 -> eval_surf at (u, v) = the keep point; all taken -> the build */
        if (!ds_last_aborted()) { double u, v; ds_uv(&u, &v); srf_trims_keep(geo_ent[0], u, v); }
        cam_msg[0] = 0; if (srf_trims_ready(gp.method)) { geo_nent = 1; geo_run(); }
        geo_reset_picks(); if (!cam_msg[0]) snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1;
    } else if (geo_cmd == G_SEXT) {                       /* FUN_180337330: dyno_surf's done flag (local_ab56) is not looked at: a click, Enter or Esc
                                                            -> FUN_180338540 at the arrow's last (u, v) and the build; the prompt string was emptied after
                                                            the get_event (Ordinal_1502 "") and is not set again until a commit (codes 0..2) */
        { double u, v; ds_uv(&u, &v); srf_ext_edge(geo_ent[0], u, v); cam_msg[0] = 0; geo_nent = 1; geo_run(); }
        geo_reset_picks(); overlay_dirty = 1;
    } else if (geo_cmd == G_CCONST) {                     /* FUN_1802980f0: "if (local_2ee7[0] == '\0') bVar9 = true": a click builds; Enter / Cancel leave no curve and the prompt 0x1751 */
        if (!ds_last_aborted()) { ds_uv(&cc_uv[0], &cc_uv[1]); geo_nent = 1; geo_run(); }
        cd_built = 1; geo_reset_picks(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1;
    }
}
/* Curve All Edges (FUN_1802c3f10): select_ents' Enter (FUN_1802c4f10, mask 0x10100) -> FUN_1802c4a70 at once. A further Enter is the OK of the options prompt (get_event mask 0x10008008: its exit commits). */
static void ce_build(void) {
    int *l = NULL, n = sel_list_surfs(&l), ok = 1;
    if (!gp.method) for (int i = 0; i < n && ok; i++) ok = srf_crv_edge_supported(l[i], cam_msg, sizeof cam_msg);
    else if (ce_open_refused(l, n, cam_msg, sizeof cam_msg)) ok = 0;   /* decision N9-28 */
    if (!n && !ce_nsel) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); free(l); return; }   /* select_ents: an empty selection goes on */
    if (!ok) { free(l); overlay_dirty = 1; return; }
    if (geo_live && geo_live_valid()) geo_live_drop();   /* FUN_1802c4a70: UndoTransTerminate of the last build (Reselect) */
    free(ce_l); ce_l = l; ce_n = n; int nu = nundo, from = F.nents; geo_live_n = 0; geo_live_marked = 0; geo_exec(); if (nundo == nu) undo_ents_added(from); if (nundo != nu) geo_live_start(from);
    sel_clear(); ce_built = 1; scene_reupload(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1;
}
static void ce_enter(void) { if (ce_built) geo_panel_button(0); else ce_build(); }
void geo_ce_enter(void) { if (geo_cmd == G_CEDGEA) ce_enter(); else if (geo_cmd == G_STRIMS && srf_trims_phase() < 2) { srf_trims_enter(cam_msg, sizeof cam_msg); overlay_dirty = 1; } }
void geo_ds_key(int code) { if (ds_running() && ds_event(code, NULL, NULL)) geo_ds_done(); }
/* the Esc key (ours: apart from the panel's Cancel - the Transform dialogs end as OK while dlg +0x5090 is set, cui_xform.c xf_escape) */
void geo_escape(void) {
    if (xf_on()) { xf_escape(); return; }
    if (geo_cmd == G_XROLL && xrl_pick) { xrl_pick = 0; geo_chain_button(2); return; }   /* 829's get_event returns 2: PostMessage 100 (Cancel) */
    if ((geo_cmd == G_OFFSETC || geo_cmd == G_XROLL) && !geo_chaining && geo_nchains) { geo_chain_button(geo_xmod ? 0 : 2); return; }   /* bVar1: 0x65 forced */
    geo_cancel();
}
void geo_cancel(void) {                            /* Esc / Cancel: geo_live_kind */
    if (ds_running()) { geo_ds_key(0xb); return; }      /* dyno_surf: get_event 2 / Esc ends it as Enter does (dyno_get_event: code 0xb) */
    if (wm_rg_wait) { wm_rg_enter(); return; }           /* RunPump returned: the preview removed (Esc: NOT DECODED beyond the message pump) */
    if (geo_cmd == G_BBOX && bb_dragging()) { bb_drag_end(1); return; }   /* Esc during an arrow drag: code 6 (cancel the drag) */
    if (geo_cmd == G_BBOX && bb_on()) { bb_button(2); return; }   /* the Bounding Box panel's Cancel (result 3, cui_bbox.c) */
    if (xf_on()) { xf_button(2); return; }               /* Esc: a pick's Esc / the dialog's Cancel (100) / select_ents' Esc (cui_xform.c) */
    if (sl_cmd(geo_cmd)) { if (!sl_esc()) geo_end(); return; }
    if (si_cmd(geo_cmd)) { if (!si_esc()) geo_end(); return; }
    if (pln_cmd(geo_cmd)) { if (!pln_esc()) geo_end(); return; }
    if (sp_cmd(geo_cmd)) { if (!sp_esc()) geo_end(); return; }
    if (dr_cmd(geo_cmd)) { if (!dr_esc()) geo_end(); return; }   /* cui_draft.c: a dimension in progress, else the command ends */
    if (do_cmd(geo_cmd)) { if (!do_esc()) geo_end(); return; }   /* cui_draft_ord.c: the text drag, else the command ends */
    if (geo_live_prefix) geo_live_drop();                 /* unchosen candidates are never committed */
    else if (geo_live_kind(geo_cmd) & LV_ESC_DROP) geo_live_drop(); else geo_live_commit();
    geo_end();
}

/* the panel values a live result is built from (gp and the typed sizes) */
/* the soft locks are left out (taken as off, with the values derived from that): a box getting the focus posts no panel code -
   the WPF soft lock has no observer (FUN_1800bfd20 / the managers' ctors) and the MFC CBN_SETFOCUS posts nothing (onSetFocus
   @1804839c0); only the value typed into it does (Value observers / CBN_KILLFOCUS with a changed value) */
static unsigned long geo_param_hash(void) {
    GeoParams sv = gp; double dv[5] = { geo_dia, geo_rw, geo_rh, geo_poly_r, geo_poly_n };
    geo_soft_strip(geo_cmd); geo_panel_sync();
    unsigned long h = 1469598103u; const unsigned char *q = (const unsigned char *)&gp; for (size_t i = 0; i < sizeof gp; i++) h = (h ^ q[i]) * 16777619u;
    double v[5] = { geo_dia, geo_rw, geo_rh, geo_poly_r, geo_poly_n };
    gp = sv; geo_dia = dv[0]; geo_rw = dv[1]; geo_rh = dv[2]; geo_poly_r = dv[3]; geo_poly_n = (int)dv[4];
    q = (const unsigned char *)v;
    for (size_t i = 0; i < sizeof v; i++) h = (h ^ q[i]) * 16777619u;
    if (geo_cmd == G_XROLL) { q = (const unsigned char *)&xrl; for (size_t i = 0; i < sizeof xrl; i++) h = (h ^ q[i]) * 16777619u; }   /* Roll's dialog (cui_xroll.c) */
    return h;
}
static void geo_live_mark(int i) {                    /* entity i is (part of) the command's result */
    if (i < 0) return;
    if (geo_live_n == geo_live_cap) { geo_live_cap = geo_live_cap ? geo_live_cap * 2 : 64; geo_live_l = realloc(geo_live_l, sizeof(int) * geo_live_cap); }
    geo_live_l[geo_live_n++] = i; geo_live_marked = 1;
}
/* the live step is still the last undo step (an Undo meanwhile ended it, as the commands' inline undo UndoTransTerminate does) */
static int geo_live_valid(void) {
    return geo_live && nundo > 0 && nundo == geo_live_nu && (void *)undo_st[nundo - 1].ents == geo_live_rec[0] && (void *)undo_st[nundo - 1].dents == geo_live_rec[1];
}
void geo_live_commit(void) {                   /* sktcher_main_color + UndoTransCommit */
    if (!geo_live) return;
    if (!(geo_cmd >= G_SBLOCK && geo_cmd <= G_SLOFT) && geo_cmd != G_SEXT && geo_cmd != G_STRIMS)     /* Simple / Flat Boundary / From Solid / Loft / Extend: the colours as built (surfaceFrontColor, the main-colour point) */
        for (int k = 0; k < geo_live_n; k++) { int i = geo_live_l[k]; if (i >= 0 && i < F.nents && F.ents[i].type) F.ents[i].color = geo_color; }
    geo_live = 0; geo_live_n = 0; scene_reupload(); overlay_dirty = 1;
}
void geo_live_drop(void) {                     /* UndoTransTerminate: the live step undone, its redo entry discarded */
    if (!geo_live) return;
    if (geo_live_valid()) { do_undo(); if (nredo) urec_free(&redo_st[--nredo]); }
    geo_live = 0; geo_live_n = 0; scene_reupload(); overlay_dirty = 1;
}
/* the panel values the live result was last built / updated from: which one changed = the panel code the manager's
   observer posts (Line Endpoints, WireframeLineEndpointsManager ctor FUN_1800bfd20: Length.Value -> 10, Angle.Value -> 0xc
   (lambdas FUN_1800c24a0 / FUN_1800c24b0); Arc Polar: the radius -> 6 / 7, start angle -> 8, end angle -> 9, by the lock
   each code tests in FUN_1801afae0, sketcher_arc_3.c:722-836) */
static double geo_live_pv[5];
static void geo_live_pv_note(void) {
    if (geo_cmd == G_LINE) { geo_live_pv[0] = gps[G_LINE].v_len; geo_live_pv[1] = gps[G_LINE].v_ang; geo_live_pv[3] = gps[G_LINE].edit_pt; geo_live_pv[4] = gps[G_LINE].v_step; }
    else if (geo_cmd == G_APOLAR) { geo_live_pv[0] = gps[G_APOLAR].r; geo_live_pv[1] = gps[G_APOLAR].sa; geo_live_pv[2] = gps[G_APOLAR].ea; }
}
/* the commands whose lockable fields soft-lock on focus (the Qt panel's FocusIn, GEO_LK_SOFT): every function panel / dialog
   with a lockable size whose clearing is decoded (geo_soft_clear_at). The WPF EvaluatorBox (cncsoftware.controls.dll
   ValueLockBehavior.OnGotKeyboardFocus) and the MFC dialogs' CSkRLCombo (mastercam.dll onSetFocus @1804839c0 -> setLock(1): the
   current lock, the hard lock +0xfc1 untouched; the lock button onBnClick @180483820 toggles the hard one; resetLock @1804841b0
   clears a lock whose hard byte is 0) work the same way. Point Dynamic: geo_ptdyn_panel */
int geo_soft_lock_cmd(int c) {
    switch (c) {
    case G_LINE: case G_APOLAR: case G_CIRCLE: case G_AEND: case G_ATAN: case G_CEDGE: case G_RECT: case G_LPAR: case G_LPERP:
    case G_LBIS: case G_LTAN: case G_PT_SEG: case G_PT_SMALL: case G_POLY: case G_RSHAPE: case G_ELLIPSE: case G_PT_DYN:
    case G_SBLOCK: case G_SCYL: case G_SSPH: case G_SCONE: case G_STOR: return 1;
    }
    return 0;
}
#define SOFT_OFF(lk) ((lk) &= GEO_LK_HARD)
/* a lock button clicked (the panel toggled lk's hard bit). The MFC dialogs' CSkLockCtrl::onBnClick (mastercam.dll @180483820):
   unlocked -> hard = current = 1, soft-locked -> hard, hard-locked -> both 0 - the lock is the hard one only. Block's buttons
   (0x2f3 / 0x2f5 / 0x2e8) also run FUN_18000f6b0: every size's dialog lock byte = its hard lock (a soft lock dropped), PostMessage
   0x6e; case 0x6e (decomp/surfcreate/prims.c :2544-2570, height :2377-2389) sets that size's done flag = lock and |value| > tol
   (it can clear it), then the pending sizes are looked at again (:2575-2600). A size whose pick is already in the list is kept
   here (ours: re-asking for it is NOT PORTED). WPF panels: the soft lock is separate (IsHardLocked only). */
void geo_lock_button(int *lk) {
    if (!geo_cmd || !lk) return;
    int mfc = geo_cmd == G_POLY || geo_cmd == G_RSHAPE || geo_cmd == G_ELLIPSE || (geo_cmd >= G_SBLOCK && geo_cmd <= G_STOR);
    if (mfc) SOFT_OFF(*lk);
    if (geo_cmd == G_SBLOCK) {
        SOFT_OFF(gp.lk_len); SOFT_OFF(gp.lk_w); SOFT_OFF(gp.lk_h);
        int z = lk == &gp.lk_h ? 2 : 1;
        if (geo_npick > 0 && !geo_sz_picked[z]) geo_sz_done[z] = geo_sz_lockdone(z, GEO_LK_HARD);
    }
}
/* the soft locks a command clears, and where (each command's panel / dialog write after its struct's lock bytes are zeroed;
   decomp/mccad_cmds unless noted):
   Line Endpoints  made / rebuilt / code 9 / an endpoint edit: FUN_180191320 (sketcher_line_1.c:4503; from FUN_18018f480 :5003,
                   :2024, FUN_18018ff60 :3885): a field whose hard lock (vf+0x50) is off gets struct lock 0, then the write
                   FUN_1800c4ed0 :3272 sets the soft lock to the struct's lock (SetLocked(l, false) = vf+0x68): a hard-locked
                   field is soft-locked too, the others are unlocked
   Arc Polar       made / rebuilt: FUN_1801af9c0 (sketcher_arc_3.c:1174; from FUN_1801ae880 :3203, :745 / :790 / :836): all
                   struct locks 0, the write FUN_180121a50 :1503 clears the soft locks (the hard ones stay)
   Circle Center Point  made (:350), codes 5 / 6 rebuild (:528): FUN_1801bef70 (sketcher_arc_2.c:996); the next pick (:319),
                   codes 0 / 1 (:583): FUN_1801bf000 (:1239) - both only when neither the radius nor the diameter is hard-locked
                   (mgr+0x58 / +0x68 vf+0x50), then the locks 0 and the write FUN_180119190 (:2387) with 0
   Arc Endpoints   made (:423), the make failed (:435), codes 8 / 9 rebuild (:651), the next first pick (:367), OK / Apply (:731):
                   FUN_1801a7890 / FUN_1801a7900, no hard-lock test (writer FUN_180103a10 with 0)
   Arc Tangent     every creation (sketcher_arc_6.c :466 / :516 / :566 / :596 / :639 / :672): FUN_1801b4af0 (:1258), no test
   Circle Edge Point  every live circle (:793 / :873 / :916 / :966 / :1035) and the code 7 / 8 rebuild (:1272): FUN_1801c5080
                   (sketcher_arc_1.c:3316), only when neither field is hard-locked (its fields are greyed here: the tangent modes)
   Rectangle       the commit FUN_1801e34a0 only (sketcher_rect.c:1750; the next first corner :600, codes 0 / 1 :815):
                   FUN_1801e3c30 (:3095), no test - not after a rebuild
   Line Parallel   made / rebuilt (sketcher_line_5.c :1116): FUN_18022a930 (:1623), the lock 0 when the hard lock is off
   Line Perpendicular  the code 4 rebuild only (sketcher_line_4.c :425 FUN_180203210, no test); after a line is made the
                   write is with 1 (:866-870: the hard lock only), so the soft lock stays
   Line Bisect     :369 / :428 (made), :517 / :566 (rebuilt): FUN_1801cbac0 (sketcher_line_3.c:1543) SetLocked(false, false)
   Line Tangent    :348 / :512 (made), :622 (rebuilt): FUN_1802725c0 (sketcher_line_6.c:1430), no test
   Point Segment   after every build (sketcher_point_4.c :495, the pick's build included): FUN_18020b1d0 (:1545), both, no test
   Point Small Arcs  never (FUN_1801d91f0 :545 zeroes the struct byte without a write: the soft lock stays)
   Polygon         made (create_polygon.c :282), the 0x69 rebuild (:191), Apply 0x6a (:209): FUN_18007ee10 (resetLock)
   Rectangular Shapes  the base click (create_rectangle.c :417 / :397), made (:446 / :310), the 0x67 / 0x6d commit (:262):
                   FUN_1800829e0
   Ellipse         the centre click (create_ellipse.c :211), Apply 0x6b (:302): FUN_18007bcb0 - not after it is made
   Block / Cylinder / Sphere / Cone / Torus  the base click and Apply (decomp/surfcreate/prims.c FUN_180013430 :2301 / :2510,
                   FUN_18002a820 :3255 / :3398, FUN_18000bf90 :1568 / :1670, FUN_180033af0 :4574 / :4700, FUN_18002ee70
                   :3929 / :4053)
   The MFC resetLock (mastercam.dll @1804841b0) clears a lock whose hard byte is 0: SOFT_OFF */
static void geo_soft_clear_at(int w) {
    GeoParams *g = &gp; int mr = w == SC_MADE || w == SC_REBUILD;
    switch (geo_cmd) {
    case G_LINE: if (mr) { g->lk_len = g->lk_len & GEO_LK_HARD ? GEO_LK_HARD | GEO_LK_SOFT : 0; g->lk_ang = g->lk_ang & GEO_LK_HARD ? GEO_LK_HARD | GEO_LK_SOFT : 0; } break;
    case G_APOLAR: if (mr) { SOFT_OFF(g->lk_r); SOFT_OFF(g->lk_sa); SOFT_OFF(g->lk_ea); } break;
    case G_CIRCLE: case G_CEDGE:
        if (!(mr || (geo_cmd == G_CIRCLE && (w == SC_NEXT || w == SC_APPLY))) || (g->lk_r & GEO_LK_HARD)) break;
        /* FUN_1801bf000 (the next pick, codes 0 / 1) also zeroes the struct's radius / diameter: the write FUN_180119190 calls
           SetValue(v, false) before SetLocked, and SetValue(v, false) changes a field only while neither lock is set
           (mastercam.dll @1801930f0) - so an unlocked radius shows 0 after the commit, a soft-locked one keeps its value */
        if (w != SC_MADE && w != SC_REBUILD && !g->lk_r) g->v_r = 0;
        g->lk_r = 0; break;
    case G_AEND:
        if (!(mr || w == SC_NEXT || w == SC_APPLY || w == SC_FAIL)) break;
        /* FUN_1801a7900 (the next first pick :367, the make failed :435, OK / Apply :731) zeroes the values too (writer FUN_180103a10:
           SetValue(v, false) then SetLocked, as above); FUN_1801a7890 (made :423, rebuilt :651) only the locks */
        if ((w == SC_NEXT || w == SC_APPLY || w == SC_FAIL) && !g->lk_r) g->v_r = 0;
        SOFT_OFF(g->lk_r); break;
    case G_ATAN: if (mr) SOFT_OFF(g->lk_r); break;
    case G_RECT: if (w == SC_NEXT || w == SC_APPLY) { SOFT_OFF(g->lk_w); SOFT_OFF(g->lk_h); } break;
    case G_LPAR: case G_PT_DYN: if (mr && !(g->lk_dist & GEO_LK_HARD)) g->lk_dist = 0; break;   /* Point Dynamic: FUN_18034ae30 (sketcher_point_2.c:850)
                                                           after every point and the code 4 rebuild */
    case G_LPERP: if (w == SC_REBUILD) SOFT_OFF(g->lk_len); break;
    case G_LBIS: case G_LTAN: if (mr) SOFT_OFF(g->lk_len); break;
    case G_PT_SEG: if (mr) { SOFT_OFF(g->lk_dist); SOFT_OFF(g->lk_n); } break;
    case G_POLY: if (mr || w == SC_APPLY) SOFT_OFF(g->lk_r); break;
    case G_RSHAPE: if (w == SC_FIRST || w == SC_MADE || w == SC_APPLY) { SOFT_OFF(g->lk_w); SOFT_OFF(g->lk_h); } break;
    case G_ELLIPSE: if (w == SC_FIRST || w == SC_APPLY) { SOFT_OFF(g->lk_ra); SOFT_OFF(g->lk_rb); } break;
    case G_SBLOCK: case G_SCYL: case G_SSPH: case G_SCONE: case G_STOR:
        if (w == SC_FIRST || w == SC_APPLY) { SOFT_OFF(g->lk_len); SOFT_OFF(g->lk_w); SOFT_OFF(g->lk_h); SOFT_OFF(g->lk_r); } break;
    }
    geo_panel_sync();
}
/* the soft locks end with the command: the next run's panel / dialog is a new one (the managers' SaveRestoreLastParams read the
   locks with GetLocked(true) = hard only, e.g. sketcher_line_5.c FUN_1800db3a0 :2769, sketcher_point_4.c FUN_1800ec080 :2040;
   CSkLockCtrl::init @180482340 starts both bytes from the struct's lock) */
static void geo_soft_strip(int c) {
    if (c <= 0 || c >= G_NCMD) return;
    GeoParams *g = &gps[c];
    int *l[] = { &g->lk_r, &g->lk_sa, &g->lk_ea, &g->lk_len, &g->lk_ang, &g->lk_dist, &g->lk_n, &g->lk_w, &g->lk_h, &g->lk_ra, &g->lk_rb };
    for (size_t i = 0; i < sizeof l / sizeof l[0]; i++) SOFT_OFF(*l[i]);
}
/* Offset (FUN_1802f12d0, after every event of FUN_1802f1420's loop): the struct lock = 0 unless the hard lock is on, then written back
   with SetLocked(l, false): the soft lock follows (a hard-locked distance stays soft-locked too) */
static void geo_of_soft(void) { if (geo_cmd == G_SOFFSET) gp.lk_dist = gp.lk_dist & GEO_LK_HARD ? GEO_LK_HARD | GEO_LK_SOFT : 0; }
static void geo_live_start(int from) {                /* after geo_exec made one undo step: the result goes live (geo_live_kind) */
    int lv = geo_live_kind(geo_cmd);
    if (!(lv & LV_LIVE) || (geo_cmd == G_LINE && gp.cont)                      /* multi-line: committed at once (sketcher_line_1.c:742-753) */
        || (geo_cmd == G_ATAN && (gp.mode == 4 || gp.mode == 5)) || !nundo) {  /* Arc Tangent 3 entities: UndoTransCommit at once (sketcher_arc_6.c:570-617) */
        geo_live_n = 0; return;
    }
    if (!geo_live_marked) for (int i = from; i < F.nents; i++) if (F.ents[i].type) geo_live_mark(i);   /* everything the command created */
    if (!geo_live_n) return;
    int col = lv & LV_PREVIEW ? (cfg_back_color == 15 ? 8 : 15) : cfg_live_color;
    if (!(lv & LV_MAINCOL)) for (int k = 0; k < geo_live_n; k++) { int i = geo_live_l[k]; if (i >= 0 && i < F.nents && F.ents[i].type) F.ents[i].color = col; }
    geo_live = 1; geo_live_nu = nundo; geo_live_rec[0] = undo_st[nundo - 1].ents; geo_live_rec[1] = undo_st[nundo - 1].dents;
    geo_live_in = geo_in_now; geo_live_hash = geo_param_hash(); geo_live_pv_note();
}
/* the picks the command needs now (p points, e entities, c chains) */
static void geo_need_counts(int *rp, int *re, int *rc) {
    *rp = *re = *rc = 0;
    for (const char *s = geo_steps(); *s; s++) { if (*s == 'p') (*rp)++; else if (*s == 'e') (*re)++; else if (*s == 'c') (*rc)++; }
}
/* the live result replaced by new geometry in its own undo step (Mastercam: replace_ent on the live entity) */
static void geo_live_replace(int type, V3 a, V3 b, V3 c, double r, double a0, double sw) {
    geo_live_drop();
    int from = F.nents, nu = nundo;
    GeoIn *in = &geo_in_now; memset(in, 0, sizeof *in);
    if (type == E_LINE) { in->npick = 2; in->pick[0] = a; in->pick[1] = b; } else { in->npick = 1; in->pick[0] = c; }
    geo_live_n = 0; geo_live_marked = 0;
    geo_add(type, a, b, c, r, a0, sw);
    if (nundo == nu) undo_ents_added(from);
    if (nundo != nu) geo_live_start(from);
    scene_reupload();
}
/* Line Endpoints / Arc Polar: a panel value changed while the live result is there. These two do not re-run from the
   picks (their picks needed depend on the locks: a typed size leaves fewer of them); the decompiles change the live
   entity itself:
   Line Endpoints FUN_1801913b0 codes 10 (Length) / 0xc (Angle), sketcher_line_1.c:1943-2026: bVar32 = the changed field
   is locked (local_8cb0 = the length lock +0x10, local_8ca0 = the angle lock +0x20, read with GetLocked(false) by
   FUN_1800c3e10 every event :571); with the live line there (bVar31 = local_8cd8 == 0 false) and neither end an entity pick
   (local_8498 / local_8496 high bytes: the tangent picks, not ported) FUN_1801904e0 (:2773). Its CurrentlyEditing
   (+0x00 / +0x01) state 2 (neither; the Endpoints 1 / 2 buttons are not ported): the first end stays, the second goes to
   p1 + L (cos a, sin a), L = the panel length if locked else the line's own, a = the panel angle if locked else the line's
   own (atan2, 0..2pi), z of p1; nothing when the line is shorter than tol. The values the line now has are written back
   (FUN_1800c4ed0: SetValue(v, false) changes only an unlocked field). Horizontal / Vertical (FUN_18018f050 / FUN_18018f100)
   then FUN_180190f30 (:3728): both ends at the axis offset - the Y (X) of p1 when the line was made (:1405-1453; the Axis
   Offset field itself is not ported). Then FUN_180191320 (soft locks). A lock button alone (codes 0xb / 0xd) and the
   other panel codes leave the live line as it is.
   Arc Polar FUN_1801afae0 codes 6 / 7 (radius, the lock +0x10 / +0x20), 8 (start angle, +0x30), 9 (end angle, +0x40),
   sketcher_arc_3.c:722-836, with the live arc and no pick pending (local_778 == 0): FUN_1801b1a50 (:2137) keeps the centre;
   radius = the panel's if its lock is on; with a start or end lock: end = the panel end angle if locked else the arc's end,
   start = the panel start angle if locked, sweep = end - start restricted to 0..2pi (-2pi..0 for a clockwise arc); then
   FUN_1801af9c0 (soft locks). */
/* ---- Line Endpoints (sketcher_line_1.c FUN_1801913b0) */
/* MastercamPlanes::GetCoordinateDisplay: 2 = the lathe diameter display. Ours: the app's planes are mill planes, no lathe coordinate
   display (as cui_draft_pt.c's coord_disp) - 0 */
static int geo_coord_disp(void) { return 0; }
/* the Axis Offset written from p1: its Y (Horizontal) / X (Vertical) minus the active origin (0 here), x2 for Horizontal with the
   diameter display (sketcher_line_1.c:1395-1453, :1405-1441) */
static void geo_axoff_note(V3 p1) { GeoParams *g = &gp; g->v_step = g->horiz ? p1.y * (geo_coord_disp() == 2 ? 2 : 1) : p1.x; }
/* the line's Y / X from the Axis Offset: the active origin + the offset, halved for Horizontal with the diameter display (FUN_180190f30
   :3723: GetCoordinateDisplay() == 2 -> x 0.5) */
static double geo_axoff_world(void) { GeoParams *g = &gp; return g->horiz && geo_coord_disp() == 2 ? g->v_step * 0.5 : g->v_step; }
/* geo_line_lastpt = local_8c18: the point of the last click (raw or the rubber band's) */
/* the rubber band's point for a click when one lock is on (FUN_18018f8c0 sketcher_line_1.c:2954: angle locked -> type 0x10 with the
   angle, length locked -> type 0x11 with the length; Horizontal / Vertical are types 3 / 4, handled where the line is made);
   mastercam.dll draw_rubber_band (decomp/rubber/rb.c): 0x11 (:986) = anchor + L (cos t, sin t), t = the direction to the cursor
   (atan2, 0..2pi); 0x10 (:951) = d = cursor - anchor, t1 = d.x / cos a, t2 = d.y / sin a (-1 when |cos| / |sin| <= tol), no point
   when both < 0, else (t1 <= t2 ? (d.y / tan a, d.y) : (d.x, tan a d.x)) + anchor (:1039). The z is the cursor's. 0 = no point */
static int geo_line_rb(V3 anchor, V3 w, double ang, double len, int lk_ang, int lk_len, V3 *q) {
    double dx = w.x - anchor.x, dy = w.y - anchor.y, tol = mc_systol.tol;
    if (lk_ang && !lk_len) {
        double c = cos(ang), sn = sin(ang), t1 = fabs(c) > tol ? dx / c : -1, t2 = fabs(sn) > tol ? dy / sn : -1;
        if (t1 < 0 && t2 < 0) return 0;
        if (t1 <= t2) dx = dy / tan(ang); else dy = tan(ang) * dx;
        *q = (V3){ anchor.x + dx, anchor.y + dy, w.z }; return 1;
    }
    if (lk_len && !lk_ang) {
        double t = dx * dx + dy * dy >= 1e-12 ? atan2(dy, dx) : 0; if (t < 0) t += 2 * M_PI;
        *q = (V3){ anchor.x + len * cos(t), anchor.y + len * sin(t), w.z }; return 1;
    }
    *q = w; return 1;
}
/* FUN_1801904e0 (sketcher_line_1.c:2768) on the live line i with CurrentlyEditing e (0 One, 1 Two, 2 None) and the point pt
   (local_8c18): One -> p1 = pt and, when length and angle are both locked, p2 = p1 + L (cos a, sin a); Two -> p2 = pt and then
   p1 = p2 - L (cos a, sin a); None -> p1 stays, L = the panel length if locked else the line's, a = the panel angle if locked
   else the line's (atan2, restrict_angle(0, 2pi)), p2 = p1 + L (cos a, sin a); nothing when the result is shorter than tol;
   replace_ent; L and a written back (FUN_1800c4ed0: SetValue(v, false) changes an unlocked field only). Then (axis), Horizontal /
   Vertical: FUN_180190f30 (:3723) - both ends at the active origin (0 here) + the axis offset (Y for Horizontal, X for
   Vertical; halved with the diameter coordinate display, not ported) */
static void geo_line_move(int i, int e, V3 pt, int axis) {
    GeoParams *g = &gp; Ent en = F.ents[i]; V3 a = en.a, b = en.b, z0 = { 0, 0, 0 };
    double La = g->v_len, an0 = g->v_ang * M_PI / 180;
    if (e == 0) { a = pt; if (g->lk_ang && g->lk_len) b = (V3){ a.x + cos(an0) * La, a.y + sin(an0) * La, a.z }; }
    else if (e == 1) { b = pt; if (g->lk_ang && g->lk_len) a = (V3){ b.x - cos(an0) * La, b.y - sin(an0) * La, b.z }; }
    double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z, L = sqrt(dx * dx + dy * dy + dz * dz);
    if (L < mc_systol.tol) return;
    double an = dx * dx + dy * dy >= 1e-12 ? atan2(dy, dx) : 0; if (an < 0) an += 2 * M_PI;   /* + restrict_angle(0, 2pi) */
    if (e == 2) {
        if (g->lk_ang) an = an0;
        if (g->lk_len) L = La;
        if (g->lk_ang || g->lk_len) b = (V3){ a.x + cos(an) * L, a.y + sin(an) * L, a.z };
    }
    if (axis && g->horiz) a.y = b.y = geo_axoff_world(); else if (axis && g->vert) a.x = b.x = geo_axoff_world();   /* FUN_180190f30 */
    if (!g->lk_len) g->v_len = L;
    if (!g->lk_ang) g->v_ang = an * 180 / M_PI;
    geo_live_replace(E_LINE, a, b, z0, 0, 0, 0);
}
/* the Endpoints buttons' codes 7 / 8 / 9 (CurrentlyEditing.Value -> FUN_1800c24c0: 0 -> 7, 1 -> 8, else 9; the ToggleButtons'
   command OnSetCurrentlyEditingCommand sets the value, or None when it is the current one - mccad.interop.dll
   WireframeLineEndpointsManagerViewModel). 7 / 8: FUN_180190200 (:4299) - the rubber band from the other end (not drawn here), the
   prompt 0x1a07 / 0x1a08 (geo_prompt); with the live line EnableApply = (CurrentlyEditing == None) (:1937-1940). 9: FUN_18018fe00
   (:3504) -> FUN_18018ff60 (:3885: the soft locks FUN_180191320, prompt 0x1a07) or, with point 1 picked and point 2 not,
   FUN_180190070 (the soft locks, the rubber band from point 1, prompt 0x1a08); with the live line EnableApply 1 */
static void geo_line_edit_code(int e) {
    if (e == 2) geo_soft_clear_at(SC_REBUILD);
    if (geo_live) geo_line_enapply = e == 2;
    snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt());
}
/* the Line Endpoints rubber band at the cursor c (FUN_18018f8c0 sketcher_line_1.c:2954, mastercam.dll draw_rubber_band
   decomp/rubber/rb.c): anchored at point 1 while point 2 is picked, at the other end of the live line while an end is edited
   (FUN_180190200: code 7 anchors at point 2 with the angle + pi, code 8 at point 1). Length and angle both locked: none (the
   point is computed); Vertical: type 4, Horizontal: type 3 (rb.c :439-470: the cursor's dx / dy along the axis, +-L by its side
   when the length is locked, none when |d| <= 1e-10); angle locked: 0x10, length locked: 0x11 (geo_line_rb); else type 0 (the
   cursor). 1 = the segment a-b */
int geo_line_rb_seg(V3 c, V3 *a, V3 *b) {
    GeoParams *g = &gp; V3 an; double ang = g->v_ang * M_PI / 180;
    if (geo_cmd != G_LINE) return 0;
    int i = geo_live && geo_live_valid() && geo_live_n == 1 ? geo_live_l[0] : -1;
    if (g->edit_pt != 2) {
        if (i < 0 || i >= F.nents || F.ents[i].type != E_LINE) return 0;
        an = g->edit_pt == 0 ? F.ents[i].b : F.ents[i].a; if (g->edit_pt == 0) ang += M_PI;
    } else if (geo_npick == 1) an = geo_pick[0];
    else return 0;
    if (g->lk_len && g->lk_ang) return 0;
    V3 q = c;
    if (g->vert || g->horiz) {
        double d = g->vert ? c.y - an.y : c.x - an.x;
        if (fabs(d) <= 1e-10) return 0;
        if (g->lk_len && g->v_len > mc_systol.tol) d = d >= 0 ? g->v_len : -g->v_len;
        q = g->vert ? (V3){ an.x, an.y + d, an.z } : (V3){ an.x + d, an.y, an.z };
    } else if ((g->lk_len || g->lk_ang) && !geo_line_rb(an, c, ang, g->v_len, g->lk_ang != 0, g->lk_len != 0, &q)) return 0;
    *a = an; *b = q; return 1;
}
/* a panel change without the live line: codes 7 / 8 / 9 as above; codes 10 / 0xc: the rubber band again (:2028-2066, not drawn);
   0xe: nothing (it needs the live line, :2059) */
static void geo_line_panel_pending(void) {
    if (gps[G_LINE].edit_pt != (int)geo_live_pv[3]) geo_line_edit_code(gp.edit_pt);
    geo_live_pv_note();
}
/* a click while CurrentlyEditing is One / Two (sketcher_line_1.c:1240-1466): the point is the click's, or the rubber band's when one
   lock is on or the mode is Horizontal / Vertical (types 3 / 4: the anchor's Y / X); stored into point 1 or point 2; FUN_18018fe00
   clears the soft locks (FUN_18018ff60); with the live line FUN_1801904e0 moves the edited end (with the hard locks left), the axis
   offset is p1's Y / X (Horizontal / Vertical, :1395-1441), CurrentlyEditing None (FUN_1801912c0 :4471, then code 9: the soft
   locks again, prompt 0x1a07), EnableApply 1. Without the live line (param_8 = 1, disasm 180191fb1): point 1 re-picked,
   CurrentlyEditing None, FUN_180190070 (the soft locks, prompt 0x1a08). 1 = the click was taken */
static int geo_line_edit_click(V3 w) {
    GeoParams *g = &gp; int e = g->edit_pt;
    int i = geo_live && geo_live_valid() && geo_live_n == 1 ? geo_live_l[0] : -1;
    if (i >= 0 && (i >= F.nents || F.ents[i].type != E_LINE)) i = -1;
    if (i < 0) {
        if (e == 0 && geo_npick >= 1) { geo_pick[0] = w; geo_line_lastpt = w; }
        g->edit_pt = 2; geo_soft_clear_at(SC_REBUILD); geo_live_pv_note();
        snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1;
        return 1;
    }
    Ent en = F.ents[i]; V3 anchor = e == 0 ? en.b : en.a, q = w;
    if (g->horiz) q.y = anchor.y; else if (g->vert) q.x = anchor.x;   /* types 3 / 4 */
    else if (!(g->lk_len && g->lk_ang) && (g->lk_len || g->lk_ang)
             && !geo_line_rb(anchor, w, g->v_ang * M_PI / 180 + (e == 0 ? M_PI : 0), g->v_len, g->lk_ang != 0, g->lk_len != 0, &q)) return 1;
    geo_line_lastpt = q;
    geo_soft_clear_at(SC_REBUILD);                   /* FUN_18018fe00 -> FUN_18018ff60 -> FUN_180191320 */
    geo_line_move(i, e, q, 0);
    if (g->horiz || g->vert) { V3 p1 = e == 0 ? q : en.a; geo_axoff_note(p1); }   /* :1395-1441 */
    g->edit_pt = 2; geo_soft_clear_at(SC_REBUILD);   /* FUN_1801912c0, then code 9 */
    geo_line_enapply = 1;
    geo_live_hash = geo_param_hash(); geo_live_pv_note();
    snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1;
    return 1;
}
static void geo_live_panel_code(unsigned long h) {
    GeoParams *g = &gp; int i = geo_live_n == 1 ? geo_live_l[0] : -1;
    double pv0 = geo_live_pv[0], pv1 = geo_live_pv[1], pv2 = geo_live_pv[2], pv3 = geo_live_pv[3], pv4 = geo_live_pv[4];
    geo_live_pv_note(); geo_live_hash = h;
    if (i < 0 || i >= F.nents) return;
    Ent e = F.ents[i]; V3 z0 = { 0, 0, 0 };
    if (geo_cmd == G_LINE) {
        if (g->edit_pt != (int)pv3) { geo_line_edit_code(g->edit_pt); geo_live_hash = geo_param_hash(); geo_live_pv_note(); return; }
        if (e.type != E_LINE) return;
        if (g->v_step != pv4) {                      /* code 0xe (:2059-2105): Horizontal / Vertical -> FUN_180190f30; no soft-lock clearing */
            if (!(g->horiz || g->vert)) return;
            V3 a = e.a, b = e.b;
            if (g->horiz) a.y = b.y = geo_axoff_world(); else a.x = b.x = geo_axoff_world();
            geo_live_replace(E_LINE, a, b, z0, 0, 0, 0);
            geo_live_hash = geo_param_hash(); geo_live_pv_note();
            return;
        }
        int c10 = g->v_len != pv0, c12 = g->v_ang != pv1;
        if (!((c12 && g->lk_ang) || (c10 && g->lk_len))) return;   /* bVar32 */
        geo_line_move(i, g->edit_pt, geo_line_lastpt, 1);   /* FUN_1801904e0 then FUN_180190f30 (:1965-1998) */
        geo_soft_clear_at(SC_REBUILD);               /* FUN_180191320 (:2024) */
    } else {
        int c6 = g->r != pv0, c8 = g->sa != pv1, c9 = g->ea != pv2;
        if (!((c6 && g->lk_r) || (c8 && g->lk_sa) || (c9 && g->lk_ea)) || e.type != E_ARC) return;
        double R = e.r, a0 = e.a0, sw = e.sw;
        if (g->lk_r) R = g->r;
        if (g->lk_sa || g->lk_ea) {
            double end = g->lk_ea ? g->ea * M_PI / 180 : a0 + sw, lo = sw >= 0 ? 0 : -2 * M_PI;
            if (g->lk_sa) a0 = g->sa * M_PI / 180;
            sw = end - a0; while (sw < lo) sw += 2 * M_PI; while (sw >= lo + 2 * M_PI) sw -= 2 * M_PI;   /* restrict_angle(lo, lo + 2pi) */
        }
        geo_soft_clear_at(SC_REBUILD);
        geo_live_replace(E_ARC, z0, z0, e.c, R, a0, sw);
    }
    geo_live_hash = geo_param_hash(); geo_live_pv_note();
}
/* Point Dynamic's code 4 (Along Value -> FUN_180351bf0, Along IsHardLocked -> FUN_180351b00; the soft lock posts nothing), the
   entity picked (disasm 18034b6af-18034b9eb): with the live point - UndoTransTerminate and the point again at the box's value from
   the measuring end, whatever the lock, then FUN_18034ae30 (the soft lock); without - locked: a point at the value at once */
static void geo_ptdyn_panel(void) {
    if (!geo_nent) return;
    int hl = gp.lk_dist & GEO_LK_HARD;
    if (gp.v_dist == geo_ptdyn_pv && hl == geo_ptdyn_plk) return;
    geo_ptdyn_pv = gp.v_dist; geo_ptdyn_plk = hl;
    if (geo_live && geo_live_valid()) { geo_live_drop(); geo_rebuilding = 1; geo_npick = 0; geo_run(); geo_rebuilding = 0; }
    else if (gp.lk_dist) { geo_npick = 0; geo_run(); }
    geo_ptdyn_pv = gp.v_dist; overlay_dirty = 1;
}
/* a panel change while a result's picks are pending (no live result): the size a typed (locked) value now gives is done, and
   when nothing is left to pick the result is made at once:
   Circle Center Point codes 5 / 6 with the centre picked and the radius or diameter locked (hard or soft): createCircleRadiusPt
                   (sketcher_arc_2.c :439-475 -> LAB_1801bfce3)
   Rectangle       codes 8 / 9 with the first corner picked, both sizes locked and nonzero (sketcher_rect.c :753-803)
   Polygon         0x69 with the centre picked: done = lock && r > tol -> LAB_18007f8ca builds (create_polygon.c :187-200, :265)
   Rectangular Shapes  0x6a with the base picked and both sizes done (create_rectangle.c :198-215, :292-314)
   Ellipse         0x6a: A / B done |= lock && value > tol, then LAB_18007cb47 makes it when both are (create_ellipse.c :281-291,
                   :309-345)
   Simple          the dialog change: done |= lock && value > tol; the base picked and every size done -> the preview
                   (prims.c :1617-1652, :3332-3363, :4006-4032, :4657-4679, :2441-2477)
   Arc Polar       codes 6 / 7 (radius), 8 (start angle): FUN_1801af0b0 (sketcher_arc_3.c:1578) when the radius is locked, > tol
                   and the start known (start angle locked or the start point picked) - the arc at once when the end angle is
                   locked (FUN_1801ae880), else state 2 (the end point next); code 9: FUN_1801aeed0 (:1209) with the end angle
                   locked and the radius / start known - the arc (:722-836). These are the steps geo_steps gives (geo_apolar_st)
   Line Endpoints  codes 10 / 0xc without a live line: the rubber band again only (sketcher_line_1.c:2028-2066) - not here.
   Arc Endpoints   codes 8 / 9 with two endpoints picked and the radius locked: FUN_1801a7330's candidates (geo_cand_show, :664-665);
                   the click then keeps one (SelectArcToKeep). */
static void geo_pending_update(void) {
    if (!(geo_npick + geo_nent)) return;
    unsigned long h = geo_param_hash(); if (h == geo_pend_hash) return;
    geo_pend_hash = h;
    switch (geo_cmd) {
    case G_CIRCLE: case G_RECT: case G_POLY: case G_RSHAPE: case G_APOLAR: break;
    case G_AEND: geo_cand_show(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return;   /* codes 8 / 9, :664-665 */
    case G_ELLIPSE: case G_SBLOCK: case G_SCYL: case G_SSPH: case G_SCONE: case G_STOR:
        if (geo_npick < 1) return;
        for (int z = 1; z <= 2; z++) if (!geo_sz_picked[z] && geo_sz_lockdone(z, ~0)) geo_sz_done[z] = 1;
        break;
    default: return;
    }
    if (geo_need()) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()[0] ? geo_prompt() : geo_name(geo_cmd)); overlay_dirty = 1; return; }
    geo_live_prefix = 0;
    geo_run(); geo_reset_picks();
    snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()[0] ? geo_prompt() : geo_name(geo_cmd));
    overlay_dirty = 1;
}
/* Extend To plane: get_plane_info's done (cui_srftrim.c ext_pl_done, ok) - the build on the surface / edge it was asked for */
void geo_sext_plane_done(int e) {
    if (geo_cmd != G_SEXT || e < 0 || e >= F.nents || !F.ents[e].type) return;
    geo_reset_picks(); geo_ent[0] = e; geo_nent = 1; cam_msg[0] = 0;
    geo_run(); geo_reset_picks(); overlay_dirty = 1;
}
void geo_live_update(void) {                   /* a panel value changed: rebuild the live result from the same picks */
    if (geo_cmd == G_LETTERS && geo_live) return;        /* ours, LET-1: live letters are rebuilt by Apply only (geo_let_apply) */
    if (geo_cmd == G_SEXT) {                           /* code 6 into To plane: get_plane_info again before the build (apps.c l.22430-22462) */
        if (gps[G_SEXT].mode != geo_sext_mode && gps[G_SEXT].mode == 1) srf_ext_plane_forget();
        geo_sext_mode = gps[G_SEXT].mode;
        if (psel_open) return;
    }
    if (geo_cmd == G_SNSET) { srf_sn_values(gp.pts, gp.dir); return; }   /* codes 5 / 6 (cui_srfedit.c) */
    if (geo_cmd == G_SUNTRIM) { srf_ut_values(gp.del); return; }       /* code 4 */
    if (geo_cmd == G_SOFFSET) { srf_of_values(gp.mode, gp.v_dist); geo_of_soft(); return; }   /* codes 12 / 11 (cui_srfedit.c) */
    if (geo_cmd == G_BBOX) { bb_poll(); return; }        /* the Bounding Box panel's own codes 4 / 5 / 7 / 8 (cui_bbox.c) */
    if (xf_cmd(geo_cmd)) { xf_poll(); return; }          /* the Transform dialogs' edit handlers + 0x66 (cui_xform.c) */
    if (sl_cmd(geo_cmd)) { sl_panel_sync(); return; }
    if (si_cmd(geo_cmd)) { si_panel_sync(); return; }
    if (pln_cmd(geo_cmd)) { pln_panel_sync(); return; }
    if (sp_cmd(geo_cmd)) { sp_panel_sync(); return; }
    if (dr_cmd(geo_cmd)) { dr_panel_sync(); return; }    /* the drafting panel's values (cui_draft.c) */
    if (do_cmd(geo_cmd)) { do_panel_sync(); return; }    /* cui_draft_ord.c */
    if (geo_cmd == G_BREAKN) { if (geo_live && geo_live_valid() && geo_param_hash() != geo_live_hash) { geo_live_drop(); brkmany_run(); } return; }
    if (geo_chain_cmd(geo_cmd)) {
        /* the chain commands rebuild from their chain list whenever it holds chains (bVar13), whether or not the last
           run made anything: Fillet Chains codes 5-9 / Chamfer Chains codes 5-7: FUN_18027d920 / FUN_1802115a0 delete the
           preview, UndoTransTerminate, the worker again (sketcher_fillet_2.c:286-291, sketcher_chamfer_2.c:293-298);
           Offset Chains 0x66 (FUN_18001cae0) */
        if (geo_chaining || !geo_nchains || (geo_cmd == G_SREV && !srf_rev_axis_ok())) return;
        unsigned long h = geo_param_hash(); if (h == geo_chain_hash) return;
        geo_xmod = 1;                                    /* (Offset Chains / Roll) the change posted 0x66: +0x5090 */
        if (geo_live && geo_live_valid()) geo_live_drop(); else { geo_live = 0; geo_live_n = 0; }
        geo_run(); geo_reset_picks();
        return;
    }
    if (!geo_live && geo_cmd == G_LINE) { geo_line_panel_pending(); return; }
    if (geo_cmd == G_PT_DYN) { geo_ptdyn_panel(); return; }
    if (!geo_live) { geo_pending_update(); return; }
    unsigned long h = geo_param_hash(); if (h == geo_live_hash) return;
    if (!geo_live_valid()) { geo_live = 0; geo_live_n = 0; return; }
    if (geo_cmd == G_LINE || geo_cmd == G_APOLAR) { geo_live_panel_code(h); return; }
    int was_prefix = geo_live_prefix;
    GeoIn in = geo_live_in; int rp, re, rc; geo_need_counts(&rp, &re, &rc); (void)rc;
    if (geo_live_kind(geo_cmd) & LV_VALUES) { rp = 1; re = 0; }   /* the first pick and the values (LV_VALUES) */
    int same = rp == in.npick && re == in.nent;
    if (geo_live_kind(geo_cmd) & LV_VALUES) same = in.npick >= 1;
    int prefix = (geo_live_kind(geo_cmd) & LV_PREFIX) && rp <= in.npick && re <= in.nent && rp + re > 0;
    if (was_prefix) { rp = in.npick; re = in.nent; same = 1; prefix = 0; }   /* candidates rebuild from their own partial picks */
    if (!(geo_live_kind(geo_cmd) & LV_REBUILD) || !(same || prefix)) { geo_live_hash = h; return; }   /* kept as it is */
    geo_live_prefix = 0;
    geo_live_drop();
    geo_reset_picks();
    for (int k = 0; k < 4; k++) { geo_pick[k] = in.pick[k]; geo_ent[k] = in.ent[k]; geo_entpt[k] = in.entpt[k]; }
    geo_npick = rp; geo_nent = re;
    geo_rebuilding = 1; geo_run(); geo_rebuilding = 0; geo_reset_picks();
    /* only Arc Tangent's candidates (SelectArcToKeep) are a choice the next pick drops; Circle Center Point / Rectangle /
       Rectangular Shapes / Polygon rebuilt from their first pick are their live result as before: the next pick, OK and
       Esc commit it (sketcher_arc_2.c: FUN_1801999a0 replace_ent on the live circle :508, next pick :306-309, exit :600-605;
       sketcher_rect.c FUN_1801e3af0, next corner :590-600, exit :862-863; create_rectangle.c :393-394 / :484-493;
       create_polygon.c :249-252 / :293-301) */
    geo_live_prefix = was_prefix || (prefix && !same && (geo_cmd == G_ATAN || geo_cmd == G_LBIS));
    if (geo_live) geo_live_in = in;                   /* the picks it was first made from stay the reference */
    else geo_live_hash = h;
}

/* ------------------------------------------------------------------------------------------------ wireframe commands */
/* entity <-> geo2d; edits replace entities (the originals are switched off, so Undo brings them back) */
static int add_ge_keep0;                               /* FUN_18005f380's replace_ent: a zero-length result keeps its record */
static int add_ge_lc(const GE *g, int level, int color) {
    V3 z0 = { 0, 0, 0 };
    if (g->t == GE_LINE) { if (!add_ge_keep0 && hypot(g->b.x - g->a.x, g->b.y - g->a.y) < 1e-9) return -1;
        return mcam_new_ent(&F, E_LINE, (V3){ g->a.x, g->a.y, g->z }, (V3){ g->b.x, g->b.y, g->z }, z0, 0, 0, 0, level, color); }
    if (g->t == GE_ARC) { if (!add_ge_keep0 && (g->r < 1e-9 || fabs(g->sw) < 1e-9)) return -1;
        return mcam_new_ent(&F, E_ARC, z0, z0, (V3){ g->c.x, g->c.y, g->z }, g->r, g->a0, g->sw, level, color); }
    return mcam_new_ent(&F, E_POINT, (V3){ g->a.x, g->a.y, g->z }, (V3){ g->a.x, g->a.y, g->z }, z0, 0, 0, 0, level, color);
}
static int add_ge(const GE *g) { doc_dirty = 1; int k = add_ge_lc(g, geo_level, geo_color); if (k >= 0) attr_new_ent(k); return k; }
/* replace entity i by n pieces with its level / colour */
/* add pieces with entity i's level / colour / styles / width (store_ent with the entity's attributes) */
static void add_attrs(const Ent *src, const GE *pieces, int n) {
    int lv = src->level, co = src->color, ls = src->ln_style, ps = src->pt_style; double w = src->width;
    for (int k = 0; k < n; k++) { int q = add_ge_lc(&pieces[k], lv, co);   /* the pieces keep the entity's attributes */
        if (q >= 0) { Ent *e = &F.ents[q]; e->ln_style = ls >= 1 ? ls : 1; e->pt_style = ps; e->width = w >= 1 ? w : 1;
            for (int p = e->first_poly; p < e->first_poly + e->npoly && p < F.npolys; p++) { F.polys[p].ln_style = (uint8_t)e->ln_style; F.polys[p].width = (uint8_t)e->width; } } }
    doc_dirty = 1;
}
static void add_like(int i, const GE *pieces, int n) { Ent src = F.ents[i]; add_attrs(&src, pieces, n); }
void geo_add_like(int i, const GE *pieces, int n) { add_like(i, pieces, n); }
/* replace entity i by n pieces; piece `keep` is the original record rewritten (replace_ent in mccadutil: the same eptr / id - ours:
   a new record that takes the id and the toolpath chains, mod_pair, as a Move), -1 = every piece new (store_ent) */
static void replace_keep(int i, const GE *pieces, int n, int keep) {
    Ent src = F.ents[i]; int from = F.nents;
    mod_del(i); add_attrs(&src, pieces, n);
    if (keep >= 0 && keep < n) { int q = from; for (int k = 0; k < keep && q < F.nents; k++) q++;   /* add_ge_lc skips degenerate pieces */
        if (q < F.nents && F.nents - from == n) mod_pair(i, q); }
}
/* the common case: one piece is an in-place rewrite (trim_ent_to_t / extend + replace_ent), several are all new */
static void replace_ent(int i, const GE *pieces, int n) { replace_keep(i, pieces, n, n == 1 ? 0 : -1); }
static double gtol(void) { return 1e-6 * (ext > 1 ? ext : 1) + 1e-7; }
#include "cui_geo_mc.inc"
/* the failures Mastercam reports in a message box (post_warning, MCMessageBox) and our NOT PORTED refusals: cam_msg is overwritten by the next prompt */
#define geo_msg(...) cui_message("apprenticeCAM", __VA_ARGS__)
/* FUN_18026fea0's test: the line is stored unless its ends are pt_equal; the shape builders stop there (FUN_180272040, FUN_180271b50, FUN_1802720d0, FUN_1802713f0) */
static int rs_line(V3 a, V3 b) { if (mc_pt_equal(a, b)) return 0; geo_line(a, b); return 1; }

/* intersections of entity e with all other visible entities (both bounded): their parameters on e, sorted */
static int dcmp_ui(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static int cuts_on(int e, const int *with, int nwith, double *t, int cap) {
    GE g = ent_ge(e); int n = 0; double tl = mc_systol.tol;   /* tol (FUN_18004a480, FUN_18004e810) */
    for (int j = 0; j < nwith; j++) {
        int o = with[j]; if (o == e || !F.ents[o].type) continue;
        if (F.ents[o].type == E_ARC && F.ents[o].vw) continue;   /* ours: an arc in another plane is no XY cutter (ent_ge is XY) */
        GE h = ent_ge(o); P2 x[2]; int k;
        if (h.t == GE_POINT) { if (ge_on(&g, h.a, tl) && n < cap) t[n++] = ge_param(&g, h.a); continue; }
        k = ge_intersect(&g, &h, x);
        for (int q = 0; q < k && n < cap; q++) if (ge_on(&g, x[q], tl) && ge_on(&h, x[q], tl)) t[n++] = ge_param(&g, x[q]);
    }
    qsort(t, n, sizeof(double), dcmp_ui);
    return n;
}
/* split entity e at the sorted parameters (ends ignored); circles become arcs between the cuts. keep: -1 = every piece new
   (FUN_180049680 / FUN_180049bf0: store_ent per piece, the original deleted), 0 = the piece at the entity's start keeps the record
   (FUN_18004c400 / FUN_18004bca0 mode 1: the original rewritten up to the point; Break Circles: the first piece) */
static int split_at_keep(int e, const double *t, int n, int keep) {
    GE g = ent_ge(e), pc[130]; int np = 0; double lo = 0, eps = 1e-9;
    int rev = F.ents[e].type == E_ARC && F.ents[e].sw < 0;   /* a clockwise arc's start is the GE's end */
    if (ge_is_circle(&g)) {
        if (n < 1) return 0;
        double la = 0;                                     /* FUN_180049680 break_at_inters.c:1040-1056: the pieces run t1 -> t -> ... -> t2, the seam stays a break: n hits -> n + 1 arcs (a hit within eps of the seam makes no piece) */
        for (int k = 0; k <= n && np < 128; k++) { double b = k < n ? t[k] : 1; if (b - la > eps) { pc[np++] = ge_sub(&g, la, b); la = b; } }
        if (!np) return 0;
        replace_keep(e, pc, np, keep < 0 ? -1 : 0); return 1;
    }
    for (int k = 0; k < n && np < 128; k++) { if (t[k] <= lo + eps || t[k] >= 1 - eps) continue; pc[np++] = ge_sub(&g, lo, t[k]); lo = t[k]; }
    if (!np) return 0;
    pc[np++] = ge_sub(&g, lo, 1);
    replace_keep(e, pc, np, keep < 0 ? -1 : rev ? np - 1 : 0); return 1;
}
static int split_at(int e, const double *t, int n) { return split_at_keep(e, t, n, -1); }
/* nearest intersection of the carriers of a and b to point k (0 if none) */
static int inter_near(const GE *a, const GE *b, P2 k, P2 *out) {
    P2 x[2]; int n = ge_intersect(a, b, x); if (!n) return 0;
    *out = n == 2 && hypot(x[1].x - k.x, x[1].y - k.y) < hypot(x[0].x - k.x, x[0].y - k.y) ? x[1] : x[0]; return 1;
}
/* trim e (picked at k) at point X keeping the side of k; circles need two points (X2) */
static int trim_ent(int e, P2 k, P2 X, const P2 *X2) {
    GE g = ent_ge(e), r;
    if (g.t == GE_POINT) return 0;
    if (ge_is_circle(&g)) {
        if (!X2) return 0;
        double t1 = ge_param(&g, X), t2 = ge_param(&g, *X2), tk = ge_param(&g, k);
        if (t2 < t1) { double s = t1; t1 = t2; t2 = s; }
        r = tk > t1 && tk < t2 ? ge_sub(&g, t1, t2) : ge_sub(&g, t2, t1 + 1);
    } else r = ge_trim_keep(&g, ge_param(&g, X), ge_param(&g, k));
    replace_ent(e, &r, 1); return 1;
}

/* ---- Break (the Trim / Break radio's Break, Trim.Value false: sketcher_trim_1 FUN_1800cdd80, sketcher_trim_2 FUN_1800db2d0;
   formats/wireframe.md "Break mode") */
/* the start Mastercam creeps from: a clockwise arc (negative sweep) is a counter-clockwise GE with its ends swapped */
static P2 ent_start(int e) { GE g = ent_ge(e); return F.ents[e].type == E_ARC && F.ents[e].sw < 0 ? g.b : g.a; }
/* Mastercam's parameter of an entity (an arc's own start / signed sweep) from ours (the GE runs counter-clockwise) */
static double mc_t(int e, double tge) { return F.ents[e].type == E_ARC && F.ents[e].sw < 0 ? 1 - tge : tge; }
/* FUN_18005fa20(ent, n, pick, t0[, t1], p6 = t0) (mccadutil, trim_123's break flag): the parts cut off away from the pick, stored as new
   entities (FUN_18005bf00 / FUN_18005be00 -> store_ent, the entity's attributes) when >= tol. Parameters are Mastercam's (mc_t).
   Lines: b = (t0 < pick && t0 < max_t) || t0 < min_t; per point, b -> start..P, else P..end; b flips for the next point.
   Arcs: n 2 -> the points sorted, b = pick between them; n 1 -> b = pick > t (t outside [-0.01, 1.01]: pick < 0.5); per point A =
   sa + t sw: t0 outside [0, 1] -> b: A..sa, else end..A; inside -> b: sa..A, else A..end; the sweep takes the arc's sign (restrict_angle). */
static void fa20_pieces(int e, double tk, const double *tin, int n) {
    const Ent *en = &F.ents[e]; double tl = mc_systol.tol, t[2] = { tin[0], n > 1 ? tin[1] : 0 }, p6 = tin[0]; GE pc[2]; int np = 0;
    if (en->type == E_LINE) {
        int b = (p6 < tk && p6 < 1) || p6 < 0;
        for (int i = 0; i < n; i++, b = !b) {
            V3 P = { en->a.x + (en->b.x - en->a.x) * t[i], en->a.y + (en->b.y - en->a.y) * t[i], en->a.z + (en->b.z - en->a.z) * t[i] };
            P2 s0 = b ? (P2){ en->a.x, en->a.y } : (P2){ P.x, P.y }, s1 = b ? (P2){ P.x, P.y } : (P2){ en->b.x, en->b.y };
            if (hypot(s1.x - s0.x, s1.y - s0.y) >= tl) pc[np++] = ge_line(s0, s1, en->a.z);
        }
    } else if (en->type == E_ARC) {
        double sa = en->a0, sw = en->sw; int b;
        if (n == 2) { if (t[1] < t[0]) { double q = t[0]; t[0] = t[1]; t[1] = q; } double d = (tk - t[0]) * (tk - t[1]) * (t[1] - t[0]); b = d < 0; }
        else if (p6 > 1.01 || p6 < -0.01) b = !(tk > 0.5) && tk != 0.5;
        else b = !(tk < p6) && tk != p6;
        for (int i = 0; i < n; i++, b = !b) {
            double A = t[i] * sw + sa, st, swp;
            if (p6 > 1 || p6 < 0) { if (b) { st = A; swp = sa - A; } else { st = sa + sw; swp = A - (sa + sw); } }
            else { if (b) { st = sa; swp = A - sa; } else { st = A; swp = sa + sw - A; } }
            st = fmod(st, 2 * M_PI); if (st < 0) st += 2 * M_PI;
            swp = fmod(swp, 2 * M_PI);
            if (sw > 0 && swp < 0) swp = 2 * M_PI - fabs(swp); else if (sw <= 0 && swp > 0) swp = fabs(swp) - 2 * M_PI;
            if (fabs(swp) * en->r < tl) continue;
            P2 c = { en->c.x, en->c.y };
            pc[np++] = swp > 0 ? ge_arc(c, en->r, st, swp, en->c.z) : ge_arc(c, en->r, st + swp, -swp, en->c.z);
        }
    }
    if (np) add_like(e, pc, np);
}
/* trim_123 with the break flag for entity e (picked at k) and its point X (entities 1 / 2, decomp: bVar10 then FUN_18005fa20(e, 1,
   pick, t, .., t), then trim_ent_to_t + replace_ent only for min_t < t < max_t - the original keeps the pick side) */
static int brk123(int e, P2 k, P2 X) {
    GE g = ent_ge(e); if (g.t == GE_POINT) return 0;
    double tl = mc_systol.tol, tx = ge_param(&g, X), tk = ge_param(&g, k); int changed = 0;
    if (hypot(X.x - g.a.x, X.y - g.a.y) >= tl && hypot(X.x - g.b.x, X.y - g.b.y) >= tl) {   /* bVar10 */
        int before = F.nents; double t = mc_t(e, tx); fa20_pieces(e, mc_t(e, tk), &t, 1); changed = F.nents > before; }
    if (tx > 0 && tx < 1 && mc_trim1(e, k, X)) changed = 1;   /* trim_ent_to_t + replace_ent, length >= tol (trim_123) */
    return changed;
}
/* trim_123 with the break flag for the third entity (mode 3): FUN_18005fa20(e, 2, pick, t0, t1, t0); then nothing if both are outside
   (FUN_18005bfd0), else trimmed at t0 when inside, then at t1 when inside (recomputed on the trimmed entity; an arc's t1 within 1e-6
   of 0 / 1 is skipped), keeping the pick side */
static int brk123_3(int e, P2 k, P2 X0, P2 X1) {
    GE g = ent_ge(e); if (g.t == GE_POINT) return 0;
    double tl = mc_systol.tol, t0 = ge_param(&g, X0), t1 = ge_param(&g, X1), tk = ge_param(&g, k);
    int before = F.nents; double tt[2] = { mc_t(e, t0), mc_t(e, t1) }; fa20_pieces(e, mc_t(e, tk), tt, 2); int changed = F.nents > before;
    int in0 = t0 >= 0 && t0 <= 1, in1 = t1 >= 0 && t1 <= 1;
    if (!in0 && !in1) return changed;
    /* sketcher_trim_2.c:2975-3010: inside t0 -> trim_ent_to_t(1, pick, t0), then t1 and the pick recomputed by t_from_pt_2d on the trimmed entity (seeded by the
       old values); inside t1 (an arc: not within 1e-6 of 0 / 1) -> trim_ent_to_t(1, pick, t1); replace_ent */
    Ent M = F.ents[e]; double tt0 = mc_t(e, t0), tt1 = mc_t(e, t1), tp = mc_t(e, tk);
    if (in0) { mc_trim_to_t(&M, 1, tp, tt0, 0, tt0); if (in1) { tt1 = mc_t_seed(&M, tt1, X1); tp = mc_t_seed(&M, tp, k); } }
    if (in1 && (M.type != E_ARC || (fabs(tt1) >= 1e-6 && fabs(1 - tt1) >= 1e-6))) mc_trim_to_t(&M, 1, tp, tt1, 0, tt1);
    GE r = mc_ge(&M); replace_ent(e, &r, 1); changed = 1;
    return changed;
}
/* Modify length's break (FUN_18004c400 / FUN_18004bca0 mode 2): the point at parameter t of e - inside: the original rewritten up to it
   (start..P keeps the record) and P..end new; outside: a new entity from the nearer end to the point (end..P / P..start) */
static int brk_len(int e, double t) {
    GE g = ent_ge(e); if (g.t == GE_POINT) return 0;
    double tl = mc_systol.tol;
    if (t >= 0 && t <= 1) { P2 X = ge_at(&g, t); if (hypot(X.x - g.a.x, X.y - g.a.y) < tl || hypot(X.x - g.b.x, X.y - g.b.y) < tl) return 0;
                            return split_at_keep(e, &t, 1, 0); }
    GE gap = t < 0 ? ge_sub(&g, t, 0) : ge_sub(&g, 1, t);
    if (ge_len(&gap) < tl) return 0;
    add_like(e, &gap, 1); return 1;
}
/* FUN_18004a480 + FUN_1800492d0 (decomp/mccad_cmds/break_at_inters.c): the carrier intersections inside both entities (the ranges
   widened by tol), in the order the parameter seeds find them - intersect_ents returns first the hit nearer the midpoint of the seed
   points (gp_ints, decomp/mccore_inters.c), the first seed being both starts; a hit is recorded for an entity only >= tol from its
   nearer end, the first entity before the second. who[k] = 0 / 1, t[k] = the parameter on that entity. */
static int inter_recs(int e0, int e1, int *who, double *t, int cap) {
    GE g = ent_ge(e0), h = ent_ge(e1); P2 x[2]; int nx = ge_intersect(&g, &h, x), n = 0; double tl = mc_systol.tol;
    if (g.t == GE_POINT || h.t == GE_POINT) return 0;
    P2 s0 = ent_start(e0), s1 = ent_start(e1), M = { (s0.x + s1.x) / 2, (s0.y + s1.y) / 2 };
    if (nx == 2 && hypot(x[1].x - M.x, x[1].y - M.y) < hypot(x[0].x - M.x, x[0].y - M.y)) { P2 q = x[0]; x[0] = x[1]; x[1] = q; }
    for (int k = 0; k < nx; k++) {
        if (!ge_on(&g, x[k], tl) || !ge_on(&h, x[k], tl)) continue;
        const GE *o[2] = { &g, &h };
        for (int w = 0; w < 2 && n < cap; w++) {
            const GE *c = o[w]; int circ = ge_is_circle(c);
            if (!circ && (hypot(x[k].x - c->a.x, x[k].y - c->a.y) < tl || hypot(x[k].x - c->b.x, x[k].y - c->b.y) < tl)) continue;
            who[n] = w; t[n] = ge_param(c, x[k]); n++;
        }
    }
    return n;
}
/* FUN_180049ae0 (Trim Break Extend Auto / 1 entity, Trim Many): the first recorded hit, if it is on the entity to break, breaks it
   there (DAT_1802555d8 = 1); otherwise trim_123(1, ..., break) at the carrier intersection near P */
static int break_first(int e0, int e1, P2 k0, P2 P) {
    int who[8]; double t[8]; int n = inter_recs(e0, e1, who, t, 8);
    if (n && who[0] == 0) return split_at(e0, t, 1);
    GE g = ent_ge(e0), h = ent_ge(e1); P2 X;
    if (g.t == GE_POINT) return 0;
    if (!mc_inter_near(&g, &h, P, &X)) return 0;
    return brk123(e0, k0, X);
}

/* the chain as geo2d entities in travel order (reversed lines swap ends; arcs keep their own direction, see rev) */
static int chain_ges(const Chain *c, GE *g, int *rev, int cap) {
    int n = 0;
    /* a clockwise arc (negative sweep: Mastercam's files, a mirrored arc - cui_xform.c) is a counter-clockwise GE (ge_arc swaps its ends),
       so the chain's direction flag, relative to the entity's own start, flips for it */
    for (int i = 0; i < c->n && n < cap; i++) { const Ent *en = &F.ents[c->ent[i]]; g[n] = ent_ge(c->ent[i]);
        rev[n] = c->rev[i] ^ (en->type == E_ARC && en->sw < 0); n++; }
    return n;
}
static P2 tr_start(const GE *g, int rev) { return rev ? g->b : g->a; }
static P2 tr_end(const GE *g, int rev) { return rev ? g->a : g->b; }
static P2 tr_dir(const GE *g, int rev, int at_end) { P2 d = ge_dir(g, (at_end != rev) ? 1 : 0); if (rev) { d.x = -d.x; d.y = -d.y; } return d; }
/* offset to the left of the travel direction */
static int off_travel(const GE *g, int rev, double d, GE *o) { return ge_offset(g, rev ? -d : d, o); }

/* a chain whose entities all still exist (an entity in two chains of the list was replaced by the first one's
   trim: what Mastercam does then is NOT DECODED, the later chain is skipped here) */
static int geo_chain_live(const Chain *c) {
    for (int k = 0; k < c->n; k++) { int e = c->ent[k]; if (e < 0 || e >= F.nents || !F.ents[e].type) return 0; }
    return c->n > 0;
}
/* fillet / chamfer every corner of a chain: kind 0 fillet (radius gp.r), 1 chamfer (distance gp.dist) */
/* create_chain_fillets (decomp/wfacc/mccadutil_kern3.c:3, FUN_18005f380 :1966): for each joint (cur, next) not tangent (check_tangency) - the pick on each curve 1.1 R from the joint (creep_along_ent, clamped to the curve), find_fil_gt(param_8 = 1), trim_ent_to_t, the trimmed curve re-read for the next joint; filters by the corner direction (use_fil_dir / fil_dir). Lines and arcs here (a spline in the chain: find_fil_ent not ported, that joint stays). The re-fillet of the chain's arcs tangent at both ends (the first pass and askrefillet, mccadutil_kern3.c:128-176, :268-400) is NOT PORTED: as answering No there, with a message. */
static void fillet_chain(const Chain *c) {
    GE g[512]; int rev[512], n = chain_ges(c, g, rev, 512); Ent M[512];
    for (int i = 0; i < n; i++) M[i] = F.ents[c->ent[i]];
    GE add[512]; int nadd = 0, joints = c->closed ? n : n - 1, ud = gp.dir != 0, fd = gp.dir == 1 ? 0 : 1;   /* uVar5: 2 (all) when +4 = 0, else +4 != 1 (sketcher_fillet_2.c :477-480) */
    for (int j = 0; j < joints; j++) {
        int a = j, b = (j + 1) % n;
        if ((M[a].type != E_LINE && M[a].type != E_ARC) || (M[b].type != E_LINE && M[b].type != E_ARC)) continue;
        P2 da = tr_dir(&g[a], rev[a], 1), db = tr_dir(&g[b], rev[b], 0);
        if (fabs(da.x * db.x + da.y * db.y) > 0.99985) continue;       /* check_tangency mccadutil_kern3.c:2215 */
        double La = mc_len(&M[a]), Lb = mc_len(&M[b]), R = gp.r; if (La <= 0 || Lb <= 0) continue;
        double qa = 1.1 * R / La, qb = 1.1 * R / Lb; if (qa > 1) qa = 1; if (qb > 1) qb = 1;
        double pa = rev[a] ? qa : 1 - qa, pb = rev[b] ? 1 - qb : qb;   /* along the GE (counter-clockwise for arcs) */
        if (M[a].type == E_ARC && M[a].sw < 0) pa = 1 - pa;
        if (M[b].type == E_ARC && M[b].sw < 0) pb = 1 - pb;
        int sty = gp.style == 1 || gp.style == 2 ? gp.style : 0;   /* set_fil_small_sweep (sketcher_fillet_2.c:483-492): 0, 1, 2 */
        GE arc; if (mc_fillet_pair(&M[a], &M[b], pa, pb, sty, ud, fd, gp.trim, &arc)) {
            /* FUN_18005f380 :2179-2186: with a direction, - Sweeps stores the arc reversed (start + sweep, -sweep); + Sweeps keeps the positive sweep */
            if (ud && fd == 1 && arc.sw > 0) { arc.a0 += arc.sw; arc.sw = -arc.sw; }
            add[nadd++] = arc; }
    }
    { int nrf = 0; for (int i = 0; i < n; i++) if (F.ents[c->ent[i]].type == E_ARC) { int p = i ? i - 1 : c->closed ? n - 1 : -1, q = i + 1 < n ? i + 1 : c->closed ? 0 : -1;   /* the first pass of create_chain_fillets: tangent to both neighbours, not larger than a neighbouring arc */
          if (p < 0 || q < 0) continue; P2 d0 = tr_dir(&g[p], rev[p], 1), s0 = tr_dir(&g[i], rev[i], 0), d1 = tr_dir(&g[i], rev[i], 1), s1 = tr_dir(&g[q], rev[q], 0);
          if (fabs(d0.x * s0.x + d0.y * s0.y) > 0.99985 && fabs(d1.x * s1.x + d1.y * s1.y) > 0.99985 && !(F.ents[c->ent[p]].type == E_ARC && F.ents[c->ent[p]].r < F.ents[c->ent[i]].r) && !(F.ents[c->ent[q]].type == E_ARC && F.ents[c->ent[q]].r < F.ents[c->ent[i]].r)) nrf++; }
      if (nrf) geo_msg("Fillet Chains: %d tangent arc(s) kept: the re-fillet (askrefillet) is NOT PORTED", nrf); }
    if (!nadd) return;
    if (gp.trim) { add_ge_keep0 = 1; for (int i = 0; i < n; i++) { GE q = mc_ge(&M[i]); replace_ent(c->ent[i], &q, 1); } add_ge_keep0 = 0; }   /* FUN_18005f380 :2151-2169: replace_ent with no length test (a line used up keeps its zero-length record) */
    geo_live_marked = 1;
    for (int i = 0; i < nadd; i++) geo_live_mark(add_ge(&add[i]));
    doc_dirty = 1;
}
static void corner_chain(const Chain *c, int kind) {
    if (kind == 0) { fillet_chain(c); return; }
    GE g[512]; int rev[512], n = chain_ges(c, g, rev, 512);
    double lo[512], hi[512]; GE add[512]; int nadd = 0, done = 0;
    for (int i = 0; i < n; i++) { lo[i] = 0; hi[i] = 1; }
    int joints = c->closed ? n : n - 1;
    for (int j = 0; j < joints; j++) {
        int a = j, b = (j + 1) % n;
        P2 da = tr_dir(&g[a], rev[a], 1), db = tr_dir(&g[b], rev[b], 0);
        if (fabs(da.x * db.x + da.y * db.y) > 0.99985) continue;     /* tangent or a reversal: no corner (check_tangency, decomp/wfacc/mccadutil_kern3.c:2215: |dot| > 0.99985) */
        P2 ka = ge_at(&g[a], 0.5), kb = ge_at(&g[b], 0.5);
        if (kind == 0) {
            GE arc; double ta, tb;
            if (!ge_fillet(&g[a], ka, &g[b], kb, gp.r, &arc, &ta, &tb)) continue;
            if (ta < -1e-9 || ta > 1 + 1e-9 || tb < -1e-9 || tb > 1 + 1e-9) continue;          /* radius too big for the entity */
            if (rev[a]) lo[a] = ta; else hi[a] = ta;
            if (rev[b]) hi[b] = tb; else lo[b] = tb;
            add[nadd++] = arc; done++;
        } else {
            /* chamfer_2_ents (FUN_18023a3c0, decomp/wfacc/core_kernels.c:2719; FUN_18023a860 :3) takes lines and arcs; Width (method 4) lines only */
            if (gp.mode == 1 && (g[a].t != GE_LINE || g[b].t != GE_LINE)) continue;
            P2 X = tr_end(&g[a], rev[a]);
            /* make_chamfer_chain styles (formats/wireframe.md): 0 Distance - both sides = value; 1 Width - the chamfer line
               is value long (equal sides: d = w / (2 sin(phi / 2)), phi the corner angle) */
            double phi = acos(fmax(-1.0, fmin(1.0, -(da.x * db.x + da.y * db.y)))), d = gp.mode == 1 ? gp.dist / (2 * sin(phi / 2)) : gp.dist;
            /* the legs: t = t_int +- d / length (FUN_18023a860 / pt_on_ent_from_t), along each entity from the joint */
            double La = ge_len(&g[a]), Lb = ge_len(&g[b]); if (La <= 1e-9 || Lb <= 1e-9) continue;
            double ta = rev[a] ? d / La : 1 - d / La, tb = rev[b] ? 1 - d / Lb : d / Lb;
            if (ta < 0 || ta > 1 || tb < 0 || tb > 1) continue;
            P2 pa = ge_at(&g[a], ta), pb = ge_at(&g[b], tb);
            if (rev[a]) lo[a] = ta; else hi[a] = ta;
            if (rev[b]) hi[b] = tb; else lo[b] = tb;
            add[nadd++] = ge_line(pa, pb, g[a].z); done++;
        }
    }
    if (!done) { cam_msg[0] = 0; return; }
    /* inside the caller's mod_begin / mod_end: every chain of the list in one undo step (the worker's one
       UndoTransStart, FUN_18027d7d0 / FUN_1802114b0) */
    for (int i = 0; i < n; i++) if (lo[i] > 1e-12 || hi[i] < 1 - 1e-12) { GE p = ge_sub(&g[i], lo[i], hi[i]); replace_ent(c->ent[i], &p, 1); }
    geo_live_marked = 1;                               /* the live result: the fillets / chamfers, not the trimmed pieces */
    for (int k = 0; k < nadd; k++) geo_live_mark(add_ge(&add[k]));
    cam_msg[0] = 0;
}

/* Offset Chains: FUN_18001bc50 -> FUN_18001c220 (decomp/mccad_cmds/do_xform_offset_ctour.c:1299) per chain and side: chain_to_pock_bound, set_min_fil_angle(corners - 1), then for copy k = 1 ..
   copies ccomp_pock(bound, k d, side, min_fil_ang, p6, 0, 0) -> rm_ccomp_pock, the entity by entity store FUN_18001cb20 (decomp/wfacc/offsetc_store.c:664: lines and arcs, length > tol, signed sweep).
   Arguments from the disassembly (decomp/wfacc/offsetc_ctx.txt, 18001bf3c-18001bf77): corners short +0x1c, the byte +0x28 (= p6, 1), linearisation +0x30. Called inside mod_begin / mod_end. */
#include "cam2d_remach.h"
static void offset_chain(const Chain *c, char side, double d) {
    if (d < 1e-9) { cam_msg[0] = 0; return; }
    /* check_chains_3d_view (mccore_chain_range.c:6200-6230, chain +0xc = 0): every line / arc end within chain_plane_tol of the first end's Z, arcs in a parallel view; otherwise ctour_3d -> FUN_18001c4c0 with depth / angle (do_xform_offset_ctour.c), not ported */
    { double z0 = 0; int first = 1; for (int i = 0; i < c->n; i++) { const Ent *E = &F.ents[c->ent[i]]; if (E->type != E_LINE && E->type != E_ARC) continue; double za = E->type == E_LINE ? E->a.z : E->c.z, zb = E->type == E_LINE ? E->b.z : E->c.z; if (first) { z0 = za; first = 0; }
          if (fabs(za - z0) > mc_systol.chain_plane_tol || fabs(zb - z0) > mc_systol.chain_plane_tol || (E->type == E_ARC && E->vw)) { geo_msg("Offset Chains: a 3D chain is NOT PORTED (FUN_18001c4c0)"); return; } } }
    RmB *b = rm_b_new(c->n); double z = c->n ? (F.ents[c->ent[0]].type == E_ARC ? F.ents[c->ent[0]].c.z : F.ents[c->ent[0]].a.z) : 0;
    for (int i = 0; i < c->n; i++) {
        const Ent *E = &F.ents[c->ent[i]]; int r = c->rev[i]; RmEnt *x = &b->e[i];
        if (E->type == E_LINE) { V3 u = r ? E->b : E->a, v = r ? E->a : E->b; x->id = 'L'; x->a[0] = u.x; x->a[1] = u.y; x->a[2] = v.x; x->a[3] = v.y; }
        else if (E->type == E_ARC) { x->id = 'A'; x->a[0] = E->c.x; x->a[1] = E->c.y; x->a[2] = E->r; x->a[3] = r ? E->a0 + E->sw : E->a0; x->a[4] = r ? -E->sw : E->sw; }
        else { rm_b_free_list(&b); geo_msg("Offset Chains: a spline in the chain is NOT PORTED (ccomp_pock's bound holds lines and arcs here)"); return; }
    }
    b->valid = 1; rm_b_info(b);
    rm_tol = mc_systol.tol; rm_nc_tol = mc_systol.nc_tol; rm_chain_tol = mc_systol.chain_tol;
    double minfil = gp.round <= 1 ? M_PI : gp.round == 2 ? 0.785398 : 0.01;   /* set_min_fil_angle(corners - 1, ..): 0 -> pi, 1 -> 0.785398, else 0.01 (mccore_pock2.c:7-20) */
    int copies = gp.copies < 1 ? 1 : gp.copies;
    for (int k = 1; k <= copies; k++) {
        RmB *out = NULL; const char *why = NULL;
        rm_ccomp_pock(b, &out, k * d, side, minfil, gp.ofc_ila != 0, 0, 0, &why);   /* a collapsed offset (no result) = FUN_18001c220 gives up (LAB_18001c49f) */
        if (!out) { if (why) snprintf(cam_msg, sizeof cam_msg, "Offset Chains: MCCECalcContourOffset port refused: %s", why); rm_b_free_list(&b); return; }
        for (RmB *q = out; q; q = q->next) for (int i = 0; i < q->n; i++) {
            RmEnt *x = &q->e[i]; GE r;
            if (x->id == 'L') { r = ge_line((P2){ x->a[0], x->a[1] }, (P2){ x->a[2], x->a[3] }, z); if (ge_len(&r) <= mc_systol.tol) continue; }
            else { memset(&r, 0, sizeof r); r.t = GE_ARC; r.c = (P2){ x->a[0], x->a[1] }; r.r = x->a[2]; r.a0 = x->a[3]; r.sw = x->a[4]; r.z = z; if (fabs(r.sw) * r.r <= mc_systol.tol) continue; }
            add_ge(&r);
        }
        rm_b_free_list(&out);
    }
    rm_b_free_list(&b);
    cam_msg[0] = 0;
}

/* closed outline from local (u, v) points rotated by gp.rot about the anchor */
static V3 loc(P2 o, double u, double v, double z) { double c = cos(gp.rot * M_PI / 180), s = sin(gp.rot * M_PI / 180); return (V3){ o.x + u * c - v * s, o.y + u * s + v * c, z }; }
static void loc_arc(P2 o, double cu, double cv, double r, double a0d, double swd, double z) {
    V3 c = loc(o, cu, cv, z); V3 zz = { 0, 0, 0 };
    geo_add(E_ARC, zz, zz, c, r, (a0d + gp.rot) * M_PI / 180, swd * M_PI / 180);
}

/* FUN_1800340d0 (the cone dialog's sync, decomp/surfcreate/cone_dlg.c): h = the height (x2 both directions); the largest taper =
   atan2(radius, h) in degrees (89.8 deg when within 0.2 deg of 0 or 90); a larger taper is cut to it; with Top radius (+0x3358 1) the
   taper = atan2(radius - top, h) (0 below 0.1 deg), at most the largest; with Taper angle the top radius = radius - tan(taper) h.
   The cut taper goes through the dialog's text (prec_tol digits, the last dropped): NOT DECODED - the value is kept here */
static void prim_cone_sync(void) {
    double h = gp.dir == 2 ? gp.v_h * 2.0 : gp.v_h, a = atan2(gp.v_r, h);
    if (fabs(a) < 0.003490658503988658 || fabs(a) > 1.567305668290908) a = 1.567305668290908;
    double amax = a * 57.29577951308233;
    if (amax < gp.ang) gp.ang = amax;
    amax += 1e-10;
    if (gp.method == 1) {
        double t = atan2(gp.v_r - gp.v_w, h); if (fabs(t) < 0.001745329251994329) t = 0;
        double deg = t * 57.29577951308233; if (amax < deg) deg = amax;
        gp.ang = deg;
    } else gp.v_w = gp.v_r - tan(gp.ang * M_PI / 180) * h;
}
void geo_exec(void) {
    V3 z0 = { 0, 0, 0 }; double z = 0; GE g, h, r; P2 x[8];
    V3 p0 = geo_pick[0], p1 = geo_pick[1], p2v = geo_pick[2];
    int e0 = geo_nent > 0 ? geo_ent[0] : -1, e1 = geo_nent > 1 ? geo_ent[1] : -1, e2 = geo_nent > 2 ? geo_ent[2] : -1;
    P2 k0 = xy(geo_entpt[0]), k1 = xy(geo_entpt[1]), k2 = xy(geo_entpt[2]);
    switch (geo_cmd) {
    /* ---- points */
    case G_POINT: geo_add(E_POINT, p0, p0, z0, 0, 0, 0); break;
    case G_LETTERS: if (wm_let_chain()) wm_let_chain_exec(); else wm_let_exec(p0); break;              /* FUN_18006c0b0 (one placement per point) */
    case G_PTNODES: wm_ptnodes(e0); break;              /* cui_wfmisc.c */
    case G_LNORM: wm_ln_exec(); break;                  /* FUN_18035a3b0 (cui_wfmisc.c) */
    case G_RGROOVE: if (geo_npick) { wm_rg_pick(p0.x, p0.y, p0.z); if (wm_rg_prevsel()) { if (wm_rg_exec()) wm_rg_wait_start(); else cui_message("Relief Groove", "Invalid groove geometry."); } } else wm_rg_exec(); break;   /* FUN_180077be0 / FUN_1800765e0 */
    case G_PT_DYN: g = ent_ge(e0); {                    /* the point along the entity (FUN_18034d310 sketcher_point_2.c:2235: creep_along_ent from the
                                                           measuring end): a click - its parameter (FUN_1801c90d0 :1292: lines and arcs not clamped), the
                                                           Along box written from it (an unlocked box only: SetValue(v, false)); no click (codes 4 / 1) - the
                                                           box's value */
        double L = ge_len(&g), t;
        if (L < 1e-12) break;
        if (geo_npick) { t = ge_param(&g, xy(p0)); double d = (geo_ptdyn_far ? 1 - t : t) * L; if (!gp.lk_dist) gp.v_dist = d; }
        else t = geo_ptdyn_far ? 1 - gp.v_dist / L : gp.v_dist / L;
        P2 q = ge_at(&g, t); V3 v = { q.x, q.y, g.z }; geo_add(E_POINT, v, v, z0, 0, 0, 0); } break;
    case G_PT_SEG: {
        /* createPointsAlongAnEntity(Spaced) (decomp/wfacc/core_kernels2.c:1612, :1675): distance <= tol -> count n >= 1 points 0, L/(n-1), ...; else ceil(L/d) points at 0, d, 2d ... (no point at the far end for an exact multiple),
           from the end nearer the pick (the pick parameter against t1 / t2, the +0x40 bit of the ent: creep_along_ent from the far end, any curve); proj_pt_cp: the construction depth in 2D. FUN_18020b240 sketcher_point_4.c:432-468 writes the other box back. */
        Ent M_ = F.ents[e0]; const CurveRec *sc_ = M_.type == E_SPLINE ? ent_spline(e0) : NULL; if (M_.type != E_LINE && M_.type != E_ARC && !sc_) break;
        double L = sc_ ? spl_length(sc_) : mc_len(&M_); double tl = mc_systol.tol; if (L <= 0) break;
        double d = gp.dist; int n = gp.n, spaced = d > tl; double pk = sc_ ? sp_pick_t(e0, geo_entpt[0]) : mc_t_on(&M_, k0), t1_ = 0, t2_ = 1;   /* the pick's t on a spline: sp_pick_t (cui_spline.c), against curve_t1t2 */
        if (sc_) mcs_spline_t1t2(sc_, &t1_, &t2_);
        int rev = spaced && !(fabs(pk - t1_) <= fabs(pk - t2_));
        if (spaced) { n = (int)(short)(int)ceil(L / d); if (!gp.lk_n) { double q = L / d; gp.v_n = (int)(short)(int)(q >= 0 ? floor(q + 0.5) : ceil(q - 0.5)); } }
        else { if (n < 1) break; d = n >= 2 ? L / (n - 1) : 0; if (!gp.lk_dist) gp.v_dist = d; }
        double s_ = 0; SplCreep cr; if (sc_) spl_creep_init(&cr, sc_, rev);   /* init_creep once, the 0x40 bit = from the far end; creep_along_ent with the running distance */
        for (int k = 0; k < n; k++, s_ += d) { V3 v;
            if (sc_) { double q[3]; spl_creep(&cr, s_, q); v = (V3){ q[0], q[1], q[2] }; }
            else { double t = (rev ? L - s_ : s_) / L; if (M_.type == E_LINE) v = (V3){ M_.a.x + (M_.b.x - M_.a.x) * t, M_.a.y + (M_.b.y - M_.a.y) * t, M_.a.z + (M_.b.z - M_.a.z) * t };
                   else { double an = M_.a0 + t * M_.sw; v = (V3){ M_.c.x + M_.r * cos(an), M_.c.y + M_.r * sin(an), M_.c.z }; } }
            if (!cfg_constr_3d) v.z = constr_depth;
            geo_add(E_POINT, v, v, z0, 0, 0, 0); }
        break; }
    case G_BOLT: {
        /* FUN_180061900 / FUN_180060e60: instance i at start + i * step, step = angleBetween (360 / n with Full Circle);
           reference circle (keepBoltCircle): start = startAngle, sweep 360 (full) or (n - 1) * step capped to 360 */
        double step = gp.full ? 360.0 / (gp.n > 0 ? gp.n : 1) : gp.v_ang;
        for (int k = 0; k < gp.n; k++) {
            double a = (gp.sa + step * k) * M_PI / 180; V3 c = { p0.x + gp.bolt_r * cos(a), p0.y + gp.bolt_r * sin(a), p0.z };
            if (gp.pts) geo_add(E_POINT, c, c, z0, 0, 0, 0);
            if (gp.circles && gp.hole_dia > 0) geo_add(E_ARC, z0, z0, c, gp.hole_dia / 2, 0, 2 * M_PI);
        }
        if (gp.ctr) geo_add(E_POINT, p0, p0, z0, 0, 0, 0);
        if (gp.refcirc) { double sw = gp.full ? 360 : fmin(360, (gp.n - 1) * fabs(step)); if (sw > 1e-9) geo_add(E_ARC, z0, z0, p0, gp.bolt_r, gp.sa * M_PI / 180, (step < 0 ? -sw : sw) * M_PI / 180); }
        break; }
    /* ---- lines */
    case G_LINE: {
        V3 a = p0, b = p1;
        /* FUN_18018f480: Horizontal + length locked -> x +- L (side of the cursor), Vertical -> y +- L; length and angle
           locked -> p1 + L (cos a, sin a); otherwise the picked point */
        if (gp.len > 0 && gp.horiz) b = (V3){ a.x + (p1.x >= a.x ? gp.len : -gp.len), a.y, a.z };
        else if (gp.len > 0 && gp.vert) b = (V3){ a.x, a.y + (p1.y >= a.y ? gp.len : -gp.len), a.z };
        else if (gp.len > 0) { b = (V3){ a.x + gp.len * cos(gp.ang * M_PI / 180), a.y + gp.len * sin(gp.ang * M_PI / 180), a.z }; }
        if (gp.horiz) b.y = a.y; if (gp.vert) b.x = a.x;
        geo_line(a, b); geo_last_end = b; geo_last_ok = 1;
        if (gp.horiz || gp.vert) geo_axoff_note(a);   /* the axis offset = p1's Y / X minus the active origin (0 here),
                                                           written into the panel (sketcher_line_1.c:1395-1453; x2 with the diameter display: not ported) */
        if (!gp.cont) geo_line_enapply = 1;              /* EnableApply(1) with the live line (:784) */
        break; }
    case G_LPAR: g = ent_ge(e0); if (g.t != GE_LINE) { cam_msg[0] = 0; break; }
        { /* a line made at the offset distance (bVar10 0: the distance locked at the pick, sketcher_line_5.c :722) or moved to it by
             code 6 (:839-875) is rebuilt by the other panel codes at the struct's distance (createLineParallelSideOffsetDist :1062-1083)
             whether or not its lock is still on (the soft lock is cleared after the build, FUN_18022a930 :1116) */
          static int off_made; double dd = gp.dist > 0 ? gp.dist : geo_rebuilding && off_made && gp.v_dist > 0 ? gp.v_dist : 0;
          off_made = dd > 0;
          P2 s = xy(p0); double d = dd > 0 ? dd * ge_side(&g, s) : ge_side(&g, s) * hypot(s.x - ge_nearest(&g, s, 0).x, s.y - ge_nearest(&g, s, 0).y);
          if (!(dd > 0) && !(mc_systol.tol <= fabs(d))) { cam_msg[0] = 0; break; }   /* CalcLineParallelThroughPoint (core_kernels2.c:4709-4770): the pick must be tol away */
          if (!(dd > 0) && !gp.lk_dist) gp.v_dist = fabs(d);                         /* the through-point distance back in the panel (sketcher_line_5.c:721-728 FUN_1800db530) */
          /* panel +0x04 (vf+0x68): 0 selected side, 1 the other side, 2 both (the call repeated mirrored) */
          if (gp.dir == 1) d = -d;
          if (ge_offset(&g, d, &r)) add_ge(&r);
          if (gp.dir == 2 && ge_offset(&g, -d, &r)) add_ge(&r); }
        break;
    case G_LPERP: g = ent_ge(e0); if (g.t == GE_POINT) break;
        { P2 s = xy(p0), f = ge_nearest(&g, s, 0); double dx = s.x - f.x, dy = s.y - f.y, l = hypot(dx, dy);
          if (l < 1e-9) { cam_msg[0] = 0; break; }
          if (gp.len > 0) { s.x = f.x + dx / l * gp.len; s.y = f.y + dy / l * gp.len; }
          /* FUN_180204d10 (+0x28): 1 = reflected through the base point (2 base - end), 2 = both */
          P2 s2 = { 2 * f.x - s.x, 2 * f.y - s.y };
          r = ge_line(f, gp.dir == 1 ? s2 : s, g.z); add_ge(&r);
          if (gp.dir == 2) { r = ge_line(f, s2, g.z); add_ge(&r); } }
        break;
    case G_LCLOSE: g = ent_ge(e0); h = ent_ge(e1);
        /* createLineClosest core_kernels2.c:772-850: closer than tol -> a Point at the closest location (:841-844), else a line; 2D construction: both ends at the construction depth (:821-822). ge_closest is 2D (closest_3d_curves on splines and 3D not ported) */
        { P2 a, b; ge_closest(&g, &h, &a, &b); double zz = cfg_constr_3d ? g.z : constr_depth, tl = mc_systol.tol;
          if (hypot(a.x - b.x, a.y - b.y) < tl) { V3 v = { a.x, a.y, zz }; geo_add(E_POINT, v, v, z0, 0, 0, 0); break; }
          r = ge_line(a, b, zz); add_ge(&r); }
        break;
    case G_LBIS: g = ent_ge(e0); h = ent_ge(e1);
        if (g.t != GE_LINE || h.t != GE_LINE) { cam_msg[0] = 0; break; }
        { double zb = cfg_constr_3d ? g.z : constr_depth;           /* createLineBisect core_kernels.c:1575 */
          P2 xb_[2]; int oks_ = mc_inter(&g, &h, (P2){ 0, 0 }, xb_) == 1 && fabs(ge_param(&g, xb_[0])) <= 1e9 && fabs(ge_param(&h, xb_[0])) <= 1e9; if (oks_) x[0] = xb_[0];
          if (!oks_) {                                               /* parallel (l_ints, its tol test, fails or |t| > 1e9, :1594-1596): one line from the midpoint of line 1's start and line 2's nearer end, along line 1 (:1597-1727) */
            if (gp.len <= 0) break;
            P2 ot = hypot(h.b.x - g.a.x, h.b.y - g.a.y) <= hypot(h.a.x - g.a.x, h.a.y - g.a.y) ? h.b : h.a, st = { (g.a.x + ot.x) * 0.5, (g.a.y + ot.y) * 0.5 }, d = ge_dir(&g, 0);
            r = ge_line(st, (P2){ st.x + d.x * gp.len, st.y + d.y * gp.len }, zb); add_ge(&r); break; }
          if (!gp.single) {                                         /* Multiple: createLineBisect's four lines (core_kernels.c:1830-1868 the directions: towards the far end of each line, t <= 0.5 -> its end, else its start; :1876-1985 stores) */
            double tg = ge_param(&g, x[0]), th = ge_param(&h, x[0]), L = gp.len; if (L <= 0) break;
            P2 a = { (tg <= 0.5 ? g.b.x : g.a.x) - x[0].x, (tg <= 0.5 ? g.b.y : g.a.y) - x[0].y }, b = { (th <= 0.5 ? h.b.x : h.a.x) - x[0].x, (th <= 0.5 ? h.b.y : h.a.y) - x[0].y };
            double la = hypot(a.x, a.y), lb = hypot(b.x, b.y); if (la < 1e-9 || lb < 1e-9) break; a.x /= la; a.y /= la; b.x /= lb; b.y /= lb;
            P2 d0 = { a.x + b.x, a.y + b.y }, d1 = { a.x - b.x, a.y - b.y }; double l0 = hypot(d0.x, d0.y), l1 = hypot(d1.x, d1.y), sg[4] = { 1, -1, 1, -1 };
            P2 P_[4]; int have[4]; for (int i = 0; i < 4; i++) { const P2 *dd = i < 2 ? &d0 : &d1; double ll = i < 2 ? l0 : l1; have[i] = ll > 1e-12; if (have[i]) P_[i] = (P2){ x[0].x + sg[i] * dd->x / ll * L, x[0].y + sg[i] * dd->y / ll * L }; }
            if (geo_npick < 1) { for (int i = 0; i < 4; i++) if (have[i]) { r = ge_line(x[0], P_[i], zb); add_ge(&r); } break; }   /* the candidates (the live result before the third pick) */
            int best = -1; double bd = 1e38; for (int i = 0; i < 4; i++) { if (!have[i]) continue; double vx = P_[i].x - x[0].x, vy = P_[i].y - x[0].y, n = vx * vx + vy * vy, t = n > 0 ? ((p0.x - x[0].x) * vx + (p0.y - x[0].y) * vy) / n : 0; if (t < 0) t = 0; else if (t > 1) t = 1; double q = hypot(x[0].x + t * vx - p0.x, x[0].y + t * vy - p0.y); if (q < bd) { bd = q; best = i; } }
            if (best >= 0) { r = ge_line(x[0], P_[best], zb); add_ge(&r); } break; }
          P2 u = ge_dir(&g, 0), v = ge_dir(&h, 0);
          if ((k0.x - x[0].x) * u.x + (k0.y - x[0].y) * u.y < 0) { u.x = -u.x; u.y = -u.y; }
          if ((k1.x - x[0].x) * v.x + (k1.y - x[0].y) * v.y < 0) { v.x = -v.x; v.y = -v.y; }
          P2 b = { u.x + v.x, u.y + v.y }; double l = hypot(b.x, b.y), L = gp.len; if (l < 1e-9 || L <= 0) break;   /* createLineBisect(length) */
          r = ge_line(x[0], (P2){ x[0].x + b.x / l * L, x[0].y + b.y / l * L }, zb); add_ge(&r); }
        break;
    /* Line Tangent Through Point (sketcher_line_6, decomp/mccad_cmds/sketcher_line_6.c): the pick on the arc / spline is
       projected onto it (proj_pt_on_ent) - the tangent point P - with its tangent T there (tan_on_ent_from_t). With a
       length set, the two lines P +- length T are offered ("Select which line to keep"); here the one on the clicked
       side. Without a length the line runs from P along T to the second pick (rubber band type 0x22). */
    case G_LTAN: g = ent_ge(e0); if (g.t != GE_ARC && g.t != GE_LINE) break;
        { P2 P = ge_nearest(&g, k0, 1), T = ge_dir(&g, ge_param(&g, P)), q = xy(p0);
          double s = (q.x - P.x) * T.x + (q.y - P.y) * T.y;
          if (gp.len > 0) s = s < 0 ? -gp.len : gp.len;
          if (fabs(s) < 1e-9) break;
          r = ge_line(P, (P2){ P.x + s * T.x, P.y + s * T.y }, g.z); add_ge(&r); }
        break;
    /* failure messages: only Mastercam's own - "Cannot create the arc" (mccadres 5793, MCMessageBox 0x16a1 in
       sketcher_arc_1 / _2 / _4 / _6), Trim's "The selected objects do not intersect" (trim_123 post_warning 0x1743); lines, fillets and chamfers show none here */
    /* ---- arcs */
    case G_CIRCLE:
        if (geo_dia > 1e-9) geo_add(E_ARC, z0, z0, p0, geo_dia / 2, 0, 2 * M_PI);
        else { double rr = hypot(p1.x - p0.x, p1.y - p0.y); if (rr > mc_systol.tol) { geo_add(E_ARC, z0, z0, p0, rr, 0, 2 * M_PI); if (!gp.lk_r) gp.v_r = rr; } }   /* sketcher_arc_2.c:382-384 tol < radius; FUN_1801c0180 :300 writes the picked radius */
        break;
    case G_ARC3: if (mc_arc3(xy(p0), xy(p1), xy(p2v), p0.z, &r)) add_ge(&r); else geo_msg("Cannot create the arc"); break;   /* FUN_1801b90b0 -> createArc3pt; 0x16a1 (5793) (mccad_kernels.c:302) */
    /* Arc Tangent (sketcher_arc_6, decomp/mccad_cmds/sketcher_arc_6.c + decomp/mccad_arctan_helpers.c; modes as
       formats/wireframe.md). The candidates are FUN_18017bc70's: for each centre, the arc from its first point to its
       second and the complementary one; SelectArcToKeep = the candidate picked (here: nearest to the third click). */
    case G_ATAN: {
        g = ent_ge(e0);
        if (gp.mode == 6) {                       /* two entities: make_fillet with trim off - a fillet arc, entities kept */
            h = ent_ge(e1); GE arc; double t1, t2;
            if (ge_fillet(&g, k0, &h, k1, gp.r, &arc, &t1, &t2)) add_ge(&arc);
            else snprintf(cam_msg, sizeof cam_msg, "Unable to create an arc using this radius");   /* post_warning(0x16b2) */
            break;
        }
        if (gp.mode == 2) {                       /* createArcTangentUsingCenterLine: tangent line offset +-R meets the centre
                                                     line (ll_ints_3d) -> full circles of radius R; the one picked is kept */
            h = ent_ge(e1); if (g.t != GE_LINE || h.t != GE_LINE) break;
            int partial = geo_npick < 1;          /* the keep-pick is still missing: show all candidates (SelectArcToKeep) */
            P2 best = { 0, 0 }; double bd = 1e300; int have = 0;
            for (int sg = 1; sg >= -1; sg -= 2) { GE off; if (!ge_offset(&g, sg * gp.r, &off)) continue;
                P2 X[2]; GE a = off, b = h; a.a = ge_at(&off, -1e6); a.b = ge_at(&off, 1e6); b.a = ge_at(&h, -1e6); b.b = ge_at(&h, 1e6);
                if (ge_intersect(&a, &b, X) < 1) continue;
                GE cir = ge_arc(X[0], gp.r, 0, 2 * M_PI, g.z);
                if (partial) { add_ge(&cir); have = 1; continue; }
                double d = ge_dist(&cir, xy(p0)); if (d < bd) { bd = d; best = X[0]; have = 1; } }
            if (have && !partial) { V3 c = { best.x, best.y, g.z }; geo_add(E_ARC, z0, z0, c, gp.r, 0, 2 * M_PI); }
            else if (!have) snprintf(cam_msg, sizeof cam_msg, "The selected entities do not intersect");   /* MCMessageBox(0x16a9) = mccadres 5801 */
            break;
        }
        if (gp.mode == 4 || gp.mode == 5) {       /* createArcTangentTo3Entities: picks -> circle through them -> centre
                                                     projected on each entity (arcs keep their side), up to 101 rounds,
                                                     until the three radii agree within tol; cr_a_3p; mode 5 full circle */
            GE e3[3] = { ent_ge(e0), ent_ge(e1), ent_ge(geo_ent[2]) }; P2 pp[3] = { k0, k1, xy(geo_entpt[2]) };
            int ok = 0; P2 o; double rr = 0;
            for (int it = 0; it < 101 && !ok; it++) {
                if (!ge_circle3(pp[0], pp[1], pp[2], &o, &rr)) break;
                double d[3];
                for (int i = 0; i < 3; i++) {
                    if (e3[i].t == GE_POINT) { d[i] = hypot(o.x - pp[i].x, o.y - pp[i].y); continue; }
                    P2 old = pp[i]; pp[i] = ge_nearest(&e3[i], o, 0);
                    if (e3[i].t == GE_ARC) { P2 v0 = { old.x - e3[i].c.x, old.y - e3[i].c.y }, v1 = { pp[i].x - e3[i].c.x, pp[i].y - e3[i].c.y };
                        if (v0.x * v1.x + v0.y * v1.y < 0) pp[i] = (P2){ e3[i].c.x - v1.x, e3[i].c.y - v1.y }; }
                    d[i] = hypot(o.x - pp[i].x, o.y - pp[i].y);
                }
                if (fabs(d[0] - d[1]) < mc_systol.tol && fabs(d[0] - d[2]) < mc_systol.tol) ok = 1;   /* mccad_arctan_modes.c:737: tol_exref */
            }
            if (ok) {
                if (gp.mode == 5) { if (ge_circle3(pp[0], pp[1], pp[2], &o, &rr)) { V3 c = { o.x, o.y, g.z }; geo_add(E_ARC, z0, z0, c, rr, 0, 2 * M_PI); } }
                else if (ge_arc3(pp[0], pp[1], pp[2], g.z, &r)) add_ge(&r);
            } else snprintf(cam_msg, sizeof cam_msg, "Cannot create the tangent arc");   /* MCMessageBox(0x16a5) */
            break;
        }
        if (g.t != GE_LINE && g.t != GE_ARC) break;
        P2 C[4], S[4], E[4]; int nc = 0; P2 q = xy(p0); double R = gp.r;
        if (gp.mode == 0) {                       /* createArcTangentToAnEntity: P = q projected, centres P +- R n */
            P2 P = ge_nearest(&g, q, 0), nv;
            if (g.t == GE_LINE) { P2 d = ge_dir(&g, 0); nv = (P2){ -d.y, d.x }; }
            else { double l = hypot(P.x - g.c.x, P.y - g.c.y); if (l < 1e-12) break; nv = (P2){ (P.x - g.c.x) / l, (P.y - g.c.y) / l }; }
            for (int sgn = 1; sgn >= -1; sgn -= 2) { C[nc] = (P2){ P.x + sgn * R * nv.x, P.y + sgn * R * nv.y }; S[nc] = P;
                E[nc] = (P2){ P.x + sgn * 2 * R * nv.x, P.y + sgn * 2 * R * nv.y }; nc++; }
        } else {                                  /* createArcTangentThruPoint: centres = offset by R meets circle (q, R) */
            P2 cc[4]; int n = ge_circles_tan_pt(&g, q, R, cc);
            for (int i = 0; i < n && nc < 4; i++) {
                P2 T;
                if (g.t == GE_LINE) T = ge_nearest(&g, cc[i], 0);
                else { double dl = hypot(cc[i].x - g.c.x, cc[i].y - g.c.y); if (dl < 1e-12) continue;
                       P2 u = { (cc[i].x - g.c.x) / dl, (cc[i].y - g.c.y) / dl };
                       double s = R > g.r && fabs(dl - (R - g.r)) < fabs(dl - (R + g.r)) ? -1 : 1;   /* enclosing: far side */
                       T = (P2){ g.c.x + s * g.r * u.x, g.c.y + s * g.r * u.y }; }
                C[nc] = cc[i]; S[nc] = q; E[nc] = T; nc++;
            }
        }
        /* FUN_18017bc70: per centre the arc start -> end (ccw) and end -> start; keep the one nearest the third click.
           Without the third click (the candidate preview) all of them are added live (SelectArcToKeep, FUN_1801997f0) */
        int partial = geo_npick < 2;
        P2 pick = xy(p1); GE best; double bd = 1e300; int have = 0;
        for (int i = 0; i < nc; i++) {
            double a0 = atan2(S[i].y - C[i].y, S[i].x - C[i].x), a1 = atan2(E[i].y - C[i].y, E[i].x - C[i].x);
            double sw = a1 - a0; while (sw <= 1e-6) sw += 2 * M_PI; while (sw > 2 * M_PI) sw -= 2 * M_PI;
            GE c1 = ge_arc(C[i], R, a0, sw, g.z), c2 = ge_arc(C[i], R, a1, 2 * M_PI - sw, g.z);
            if (partial) { if (fabs(sw) * R >= 1e-9) add_ge(&c1); if (fabs(2 * M_PI - sw) * R >= 1e-9) add_ge(&c2); continue; }
            if (fabs(sw) * R >= 1e-9) { double d = ge_dist(&c1, pick); if (d < bd) { bd = d; best = c1; have = 1; } }
            if (fabs(2 * M_PI - sw) * R >= 1e-9) { double d = ge_dist(&c2, pick); if (d < bd) { bd = d; best = c2; have = 1; } }
        }
        if (partial) { if (!nc) geo_msg("Cannot create the arc"); }
        else if (have) add_ge(&best); else geo_msg("Cannot create the arc");   /* MCMessageBox(0x16a1) */
        break; }
    case G_CEDGE:
        if (gp.mode == 0) { V3 c = { (p0.x + p1.x) / 2, (p0.y + p1.y) / 2, p0.z }; double rr = hypot(p1.x - p0.x, p1.y - p0.y) / 2; if (rr > 1e-9) geo_add(E_ARC, z0, z0, c, rr, 0, 2 * M_PI); }
        else { P2 o; double rr; if (ge_circle3(xy(p0), xy(p1), xy(p2v), &o, &rr)) { V3 c = { o.x, o.y, p0.z }; geo_add(E_ARC, z0, z0, c, rr, 0, 2 * M_PI); } else geo_msg("Cannot create the arc"); }
        break;
    case G_AEND:
        if (gp.r <= 0) { if (mc_arc3(xy(p0), xy(p2v), xy(p1), p0.z, &r)) add_ge(&r); else geo_msg("Cannot create the arc"); break; }
        {   /* radius locked: createArcEndpts (sketcher_arc_4.c:3072-3147): findArcCenters' two centres (aa_ints, decomp/mccore_inters.c
               :1813-1895: c0 left of p1 -> p2, c1 its mirror; |p1p2| - 2r > tol -> "Cannot create the arc" 5793), then FUN_18017bc70
               (decomp/mccad_arctan_helpers.c:398-557) - for each centre the CCW arc p1 -> p2 and its complement p2 -> p1, stored when
               |sweep| r >= tol, in the main colour with selection bit 4 (store_ent(..., 4, main_color ...)): the candidates; the
               third click is SelectArcToKeep's pick (FUN_1801997f0 -> decomp/mccad_arctan_helpers.c:12-135, prompt 5784): the arc
               picked (here: the one passing nearest the click) is kept, the others delete_ent; the kept one goes live */
            P2 a = xy(p0), b = xy(p1), m = { (a.x + b.x) / 2, (a.y + b.y) / 2 }; double hc = hypot(b.x - a.x, b.y - a.y) / 2;
            if (hc < 1e-12 || hc - gp.r > mc_systol.tol) { geo_msg("Cannot create the arc"); break; }
            double hh = gp.r > hc ? sqrt(gp.r * gp.r - hc * hc) : 0; P2 n = { -(b.y - a.y) / (2 * hc), (b.x - a.x) / (2 * hc) };
            int partial = geo_npick < 3;              /* the keep-pick is still missing: every candidate (SelectArcToKeep) */
            GE best; double bd = 1e300;
            for (int s = 1; s >= -1; s -= 2) {        /* c0 (left), then c1 */
                P2 c = { m.x + s * hh * n.x, m.y + s * hh * n.y };
                double ta = atan2(a.y - c.y, a.x - c.x), tb = atan2(b.y - c.y, b.x - c.x), sw = fmod(tb - ta + 4 * M_PI, 2 * M_PI);
                if (sw <= 1e-6) sw += 2 * M_PI;
                GE c1 = ge_arc(c, gp.r, ta, sw, p0.z), c2 = ge_arc(c, gp.r, tb, 2 * M_PI - sw, p0.z);
                if (partial) { if (sw * gp.r >= mc_systol.tol) add_ge(&c1); if ((2 * M_PI - sw) * gp.r >= mc_systol.tol) add_ge(&c2); continue; }
                double d1 = ge_dist(&c1, xy(p2v)), d2 = ge_dist(&c2, xy(p2v));
                if (d1 < bd) { bd = d1; best = c1; } if (d2 < bd) { bd = d2; best = c2; }
            }
            if (!partial) add_ge(&best);
        }
        break;
    case G_APOLAR: {    /* Arc Polar (FUN_1801afae0, formats/wireframe.md): click 1 centre; click 2 start point -> radius and start angle (unless locked); click 3 end point -> end
                           angle (unless locked). Direction: all three typed -> clockwise when end < start (sketcher_arc_3.c:3175-3177); otherwise the rubber band's sign at the end
                           click (sketcher_arc_3.c:632-633 from rubber/rb.c:1094-1100: geo_apolar_cw, latched by geo_apolar_rb). createArcPolar (decomp/wfacc/mccad_kernels.c:449):
                           start in [0, 2pi), sweep restricted to (1e-6, 2pi] or (-2pi, -1e-6) */
        int k_ = 1; double R = gp.r, a0 = gp.sa * M_PI / 180, a1 = gp.ea * M_PI / 180;
        if (!(gp.lk_r && gp.lk_sa) || geo_apolar_st) { V3 s = geo_pick[k_++]; if (!gp.lk_r) R = hypot(s.x - p0.x, s.y - p0.y); if (!gp.lk_sa) a0 = atan2(s.y - p0.y, s.x - p0.x); }
        if (!gp.lk_ea) { V3 e = geo_pick[k_]; a1 = atan2(e.y - p0.y, e.x - p0.x); }
        if (R <= mc_systol.tol) break;                    /* sketcher_arc_3.c:574: the rubber band radius (+0x88) must exceed tol */
        int cw = gp.lk_r && gp.lk_sa && gp.lk_ea ? a1 - a0 < 0 : geo_apolar_cw;
        double st = mc_restrict(0, 2 * M_PI, a0), en = mc_restrict(0, 2 * M_PI, a1), sw = cw ? mc_restrict(-2 * M_PI, -1e-6, en - st) : mc_restrict(1e-6, 2 * M_PI, en - st);
        { GE q; memset(&q, 0, sizeof q); q.t = GE_ARC; q.c = xy(p0); q.r = R; q.a0 = st; q.sw = sw; q.z = p0.z; add_ge(&q); }   /* the signed sweep (geo_add would turn it counter-clockwise) */
        break; }
    case G_APOLEND: { double ak = (gp.start_pt ? gp.sa : gp.ea) * M_PI / 180;   /* Start point: P - r (cos a0, sin a0); End point: P - r (cos a1, sin a1) */
        V3 c = { p0.x - gp.r * cos(ak), p0.y - gp.r * sin(ak), p0.z };
        double sw = mc_restrict(1e-6, 2 * M_PI, (gp.ea - gp.sa) * M_PI / 180);   /* restrict_angle(1e-6, 2pi) sketcher_arc_7.c:380 */
        if (gp.r <= mc_systol.tol) break;                 /* sketcher_arc_7.c:373 */
        geo_add(E_ARC, z0, z0, c, gp.r, gp.sa * M_PI / 180, sw); break; }
    /* ---- shapes */
    case G_RECT: {
        V3 a = p0, c = p1;
        if (geo_rw > 1e-9 && geo_rh > 1e-9) { if (gp.anchor_ctr) { a.x -= geo_rw / 2; a.y -= geo_rh / 2; } c = (V3){ a.x + geo_rw, a.y + geo_rh, a.z }; }
        else if (gp.anchor_ctr) { a = (V3){ 2 * p0.x - p1.x, 2 * p0.y - p1.y, p0.z }; }
        /* FUN_180272040 / FUN_18026e060 (decomp/wfacc/core_kernels.c:2002, |width|, |height| about the centre): top left -> top right -> bottom right -> bottom left -> top left */
        double x0 = fmin(a.x, c.x), x1 = fmax(a.x, c.x), y0 = fmin(a.y, c.y), y1 = fmax(a.y, c.y); V3 TL = { x0, y1, a.z }, TR = { x1, y1, a.z }, BR = { x1, y0, a.z }, BL = { x0, y0, a.z };
        if (rs_line(TL, TR) && rs_line(TR, BR) && rs_line(BR, BL)) rs_line(BL, TL);   /* FUN_180272040: each FUN_18026fea0 stops the rest when its ends are pt_equal */
        break; }
    case G_RSHAPE: {
        /* size: typed (locked) or rect_width_and_height_from_1pt - the cursor delta rotated by -angle; anchor 1 (lower
           left): size = dx / dy, anchor 5 (centre): 2|dx| / 2|dy| (formats/wireframe.md) */
        /* base point anchor 1..9 (FUN_180270fc0 mccad_shapes_core.c:2015-2031, rect_width_and_height_from_1pt :122; default 1; columns 1-3 / 4-6 / 7-9, rows 1,4,7 / 2,5,8 / 3,6,9): centre = base + (+w/2 | 0 | -w/2, +h/2 | 0 | -h/2), rotated by the angle (n != 5); size from the cursor d after -angle: column 1: dx if > tol else 0; column 2: 2|dx|; column 3: |dx| if dx < tol else 0; rows likewise */
        double w = geo_rw, hh = geo_rh, tl = mc_systol.tol; P2 o = xy(p0); z = p0.z; int an = gp.anchor >= 1 && gp.anchor <= 9 ? gp.anchor : gp.anchor_ctr ? 5 : 1, col = (an - 1) / 3, row = (an - 1) % 3;
        if (!(geo_rw > 0 && geo_rh > 0)) {
            double c = cos(-gp.rot * M_PI / 180), s = sin(-gp.rot * M_PI / 180), dx0 = p1.x - p0.x, dy0 = p1.y - p0.y;
            double dx = dx0 * c - dy0 * s, dy = dx0 * s + dy0 * c;
            if (geo_rw <= 0) w = col == 0 ? (dx > tl ? dx : 0) : col == 1 ? 2 * fabs(dx) : (dx < tl ? fabs(dx) : 0);
            if (geo_rh <= 0) hh = row == 0 ? (dy > tl ? dy : 0) : row == 1 ? 2 * fabs(dy) : (dy < tl ? fabs(dy) : 0);
        }
        if (fabs(w) < 1e-9 || fabs(hh) < 1e-9) break;
        if (an != 5) { double ox = col == 0 ? w / 2 : col == 1 ? 0 : -w / 2, oy = row == 0 ? hh / 2 : row == 1 ? 0 : -hh / 2, c = cos(gp.rot * M_PI / 180), sn = sin(gp.rot * M_PI / 180);
            if (fabs(gp.rot) > 1e-12) { o.x += ox * c - oy * sn; o.y += oy * c + ox * sn; } else { o.x += ox; o.y += oy; } }
        w = fabs(w); hh = fabs(hh);
        double X = w / 2, Y = hh / 2;
        if (gp.shape == 0) {                           /* FUN_180272040 + FUN_18026e060: top left -> top right -> bottom right -> bottom left; fillets FUN_18026fce0 only with radius < |w| / 2 - tol and
                                                          < |h| / 2 - tol (create_rectangle2 posts 0x16d1 / 0x16d2, mccad_shapes_core.c:101-110, make_rect_fillets = 0 for that one) */
            double rr = gp.corner_r, tl = mc_systol.tol; int fil = rr > tl;
            if (fil && !(rr < w * 0.5 - tl && rr < hh * 0.5 - tl)) fil = 0;   /* create_rectangle1's path (1-point): FUN_18026fce0's test, no warning (0x16d1 / 0x16d2 belong to the 2-point create_rectangle2, a mode this panel does not have) */
            if (!fil) { if (rs_line(loc(o, -X, Y, z), loc(o, X, Y, z)) && rs_line(loc(o, X, Y, z), loc(o, X, -Y, z)) && rs_line(loc(o, X, -Y, z), loc(o, -X, -Y, z))) rs_line(loc(o, -X, -Y, z), loc(o, -X, Y, z)); }
            else {                                     /* FUN_18026fea0: each line stored then filleted with the previous (FUN_18026d850 / FUN_180272f80); the closing corner last (FUN_18026fce0) */
                geo_line(loc(o, -X + rr, Y, z), loc(o, X - rr, Y, z));
                geo_line(loc(o, X, Y - rr, z), loc(o, X, -Y + rr, z)); loc_arc(o, X - rr, Y - rr, rr, 0, 90, z);
                geo_line(loc(o, X - rr, -Y, z), loc(o, -X + rr, -Y, z)); loc_arc(o, X - rr, -Y + rr, rr, 270, 90, z);
                geo_line(loc(o, -X, -Y + rr, z), loc(o, -X, Y - rr, z)); loc_arc(o, -X + rr, -Y + rr, rr, 180, 90, z);
                loc_arc(o, -X + rr, Y - rr, rr, 90, 90, z);
            }
        } else {
            /* FUN_180271b50 / FUN_1802720d0 / FUN_1802713f0 (decomp/mccad_shapes_core2.c:189, :407, :600): FUN_180272530 swaps width and height when |w| < |h| (core_kernels.c:2067) and the shape is
               built on its long side, then rotated +90 (points) and +90 degrees on the arcs' start angle */
            int r90 = w < hh; double W = r90 ? hh : w, H = r90 ? w : hh, R = W / 2;
            #define PT(u_, v_) loc(o, r90 ? -(v_) : (u_), r90 ? (u_) : (v_), z)
            #define ARC(u_, v_, rd, st, sw) do { loc_arc(o, r90 ? -(v_) : (u_), r90 ? (u_) : (v_), rd, (st) + (r90 ? 90 : 0), sw, z); } while (0)
            if (gp.shape >= 2 && gp.corner_r > tl && gp.corner_r < w * 0.5 - tl && gp.corner_r < hh * 0.5 - tl) geo_msg("Rectangular Shapes: the corner radius on a D or double D shape is NOT PORTED (FUN_180272f80, find_fil_ent)");   /* make_rect_fillets = tol < radius (create_rectangle.c:572) with no shape test; res 6414 control 758 has no shape enable; FUN_18026fce0 / FUN_180272f80 fillet every corner except the tangent ones and Obround (FUN_18026dd90: rect_type 1 returns true) */
            if (gp.shape == 1) {                       /* obround: top left -> right (FUN_18026f6d0 :378), the right arc (start -90), bottom right -> left, the left arc (start 90) */
                double r2 = H / 2;
                if (rs_line(PT(-R + r2, r2), PT(R - r2, r2))) { ARC(R - r2, 0, r2, -90, 180); if (rs_line(PT(R - r2, -r2), PT(-R + r2, -r2))) ARC(-R + r2, 0, r2, 90, 180); }   /* FUN_180271b50 :378-394: FUN_18026f6d0 fails on pt_equal ends and stops: a square gives nothing */
            } else if (gp.shape == 2) {                /* D: flat at y = -H / 2 left -> right (:479-503, :582), the arc centred at y = H / 2 - R through the top */
                double yc = H / 2 - R, hw, st, sw;
                if (H > R) { double d = H - R, a = asin(d / R) * 180 / M_PI; hw = sqrt(R * R - d * d); st = -a; sw = a * 2 + 180; }
                else if (H < R) { double d = R - H, a = asin(d / R) * 180 / M_PI; hw = sqrt(R * R - d * d); st = a; sw = 180 - a * 2; }
                else { hw = R; st = 0; sw = 180; }
                if (rs_line(PT(-hw, -H / 2), PT(hw, -H / 2))) ARC(0, yc, R, st, sw);   /* :582-589 */
            } else {                                   /* double D (:671-787): the top flat left -> right, the right arc (start -a, sweep 2a), the bottom flat right -> left, the left arc */
                double h2 = H / 2; if (h2 >= R) { cam_msg[0] = 0; break; }
                double hw = sqrt(R * R - h2 * h2), a = asin(h2 / R) * 180 / M_PI;
                if (rs_line(PT(-hw, h2), PT(hw, h2))) { ARC(0, 0, R, -a, 2 * a); if (rs_line(PT(hw, -h2), PT(-hw, -h2))) ARC(0, 0, R, 180 - a, 2 * a); }   /* :766-783 */
            }
            #undef PT
            #undef ARC
        }
        break; }
    case G_POLY: {
        int n = geo_poly_n < 3 ? 3 : geo_poly_n; double r0 = geo_poly_r > 0 ? geo_poly_r : hypot(p1.x - p0.x, p1.y - p0.y);
        if (r0 < 1e-9) break; double R = gp.flat ? r0 / cos(M_PI / n) : r0;
        GE sides[361]; P2 o = xy(p0);
        for (int i = 0; i < n; i++) {
            double t0 = (180.0 / n - 90 + gp.rot) * M_PI / 180 + 2 * M_PI * i / n, t1 = t0 + 2 * M_PI / n;   /* flat bottom side */
            sides[i] = ge_line((P2){ o.x + R * cos(t0), o.y + R * sin(t0) }, (P2){ o.x + R * cos(t1), o.y + R * sin(t1) }, p0.z);
        }
        if (gp.corner_r > 1e-9) {                      /* corner fillets */
            GE out[361]; double lo[361], hi[361]; for (int i = 0; i < n; i++) { lo[i] = 0; hi[i] = 1; }
            for (int i = 0; i < n; i++) { int j = (i + 1) % n; GE arc; double ta, tb;
                if (ge_fillet(&sides[i], ge_at(&sides[i], .5), &sides[j], ge_at(&sides[j], .5), gp.corner_r, &arc, &ta, &tb) && ta > 0 && tb < 1) { hi[i] = ta; lo[j] = tb; out[i] = arc; } else out[i].t = 0; }
            for (int i = 0; i < n; i++) { r = ge_sub(&sides[i], lo[i], hi[i]); add_ge(&r); if (out[i].t) add_ge(&out[i]); }
        } else for (int i = 0; i < n; i++) add_ge(&sides[i]);
        break; }
    case G_ELLIPSE: {   /* A / B picked as distances from the centre (rubber band 0x88 / 0x1b8 semantics NOT DECODED) */
        /* the sizes: picked (rubber band 8 / 0xf: the distance from the centre, written into the dialog - FUN_18007b6c0
           create_ellipse.c :186-197) or the dialog's (done from it); a rebuild (0x6a, FUN_18007b760) uses the values only */
        int k_ = 1; double A = gp.ea_r, B = gp.eb_r;
        if (!geo_rebuilding && geo_sz_picked[1]) { A = hypot(geo_pick[k_].x - p0.x, geo_pick[k_].y - p0.y); k_++; gp.ea_r = A; }
        if (!geo_rebuilding && geo_sz_picked[2]) { B = hypot(geo_pick[k_].x - p0.x, geo_pick[k_].y - p0.y); k_++; gp.eb_r = B; }
        if (A < 1e-9 || B < 1e-9) break;
        /* one NURBS (the default output type 0): CalculateEllipse / ellipse_to_nurbs (cui_geo_mc.inc mc_ellipse). Output types 1 (arcs) and 2 (lines) of the dialog are NOT PORTED. */
        { if (gp.ell_out == 1 || gp.ell_out == 2) mod_begin();   /* one transaction (UndoTransStart, create_ellipse.c:1166): the NURBS made and deleted inside it */
          int q = mc_ellipse(p0, A, B, gp.sa, gp.ea, gp.rot); if (q < 0) { if (gp.ell_out == 1 || gp.ell_out == 2) mod_end(); cam_msg[0] = 0; break; }
          /* FUN_18007b380 create_ellipse.c:1175-1196: type 2 break_spline_const_error(.., tolerance), the NURBS deleted */
          if (gp.ell_out == 2) { const CurveRec *sc = ent_spline(q); if (sc) { brk_spline_lines(q, sc, gp.ell_tol); ent_off(q); } mod_end(); }
          /* create_ellipse.c:1175-1181: type 1 break_spline_arcs(0, tolerance, ...), the NURBS then deleted (:1196-1199) */
          if (gp.ell_out == 1) { const CurveRec *sc = ent_spline(q); if (sc) { brk_spline_arcs(q, sc, gp.ell_tol, 0); ent_off(q); } mod_end(); } }
        break; }
    case G_HELIX: case G_SPIRAL: {                     /* MaHelix / MaSpiral -> NURBS: create_helix.c FUN_180088e00 :795, create_spiral.c FUN_180089040 :389 (view axes, |R|, |revs|; viewer/mcspline_refit.c mcr_helix) */
        const double Xv[3] = { 1, 0, 0 }, Yv[3] = { 0, 1, 0 }, Zv[3] = { 0, 0, 1 };
        McrHelix H; memset(&H, 0, sizeof H); memcpy(H.x, Xv, 24); memcpy(H.y, Yv, 24); memcpy(H.z, Zv, 24); H.c[0] = p0.x; H.c[1] = p0.y; H.c[2] = p0.z; H.dir = gp.cw != 0;
        if (geo_cmd == G_HELIX) { H.r = gp.bolt_r; H.revs = gp.revs; H.pitch = gp.pitch; H.taper_deg = gp.taper; H.init_deg = gp.sa; }
        else { H.spiral = 1; H.r = gp.r; H.irevs = (int)gp.revs; H.v0 = gp.zpitch; H.v1 = gp.zpitch2; H.h0 = gp.pitch; H.h1 = gp.pitch2; }
        McrResult R; memset(&R, 0, sizeof R); int bad = mcr_helix(&H, mc_systol.tol, mc_systol.surf_step_chord_dev, mcam_is_metric(&F), &R);
        for (int i = 0; !bad && i < R.nrec; i++) { int q = mcam_new_curve(&F, R.rec[i].rec, R.rec[i].n, geo_level, geo_color); if (q >= 0) { attr_new_ent(q); doc_dirty = 1; overlay_dirty = 1; } }
        mcr_free(&R); if (bad) cam_msg[0] = 0;
        break; }
    case G_TRIM: {
        g = ent_ge(e0);
        if (g.t == GE_POINT) { cam_msg[0] = 0; break; }
        mod_begin();
        if (!gp.trimmode) {                            /* Break (Trim.Value false): FUN_1800cdd80's second switch */
            int ok = 0;
            if (gp.mode == TR_1 || gp.mode == TR_AUTO) { h = ent_ge(e1); ok = break_first(e0, e1, k0, seed_mid(&g, k0, &h, k1)); }   /* FUN_180049ae0, else trim_123(1, .., break): the hit nearer the seed midpoint (intersect_ents) */
            else if (gp.mode == TR_2) {                /* FUN_180049560: both broken at every recorded hit, else trim_123(2, .., break) */
                int who[8]; double t[8]; int n = inter_recs(e0, e1, who, t, 8);
                if (n) { double t0[8], t1[8]; int n0 = 0, n1 = 0;
                         for (int i = 0; i < n; i++) { if (who[i]) t1[n1++] = t[i]; else t0[n0++] = t[i]; }
                         qsort(t0, n0, sizeof(double), dcmp_ui); qsort(t1, n1, sizeof(double), dcmp_ui);
                         ok = (n0 && split_at(e0, t0, n0)) | (n1 && split_at(e1, t1, n1)); }   /* FUN_180049680: all pieces new */
                else { h = ent_ge(e1); P2 X, m = seed_mid(&g, k0, &h, k1);
                       if (mc_inter_near(&g, &h, m, &X)) { ok |= brk123(e0, k0, X); ok |= brk123(e1, k1, X); } }
            } else if (gp.mode == TR_3) {              /* trim_123(3, .., break): the two at their hits on the third, the third at both */
                h = ent_ge(e1); GE t3 = ent_ge(e2); P2 X0, X1;   /* each pair's hit by its seed midpoint (intersect_ents) */
                if (mc_inter_near(&g, &t3, seed_mid(&g, k0, &t3, k2), &X0) && mc_inter_near(&h, &t3, seed_mid(&h, k1, &t3, k2), &X1)) {
                    ok |= brk123(e0, k0, X0);
                    ok |= brk123(e1, k1, X1);
                    ok |= brk123_3(e2, k2, X0, X1);   /* trim_ent_to_t treats a full circle as any arc (decomp/wfacc/mccadutil_kern3.c:1666) */
                }
            } else if (gp.mode == TR_DIVIDE) {         /* FUN_18004e810(.., 0): broken at the nearest hit on each side of the pick */
                int *all, na = sel_list(&all, 1); double t[256]; int n = cuts_on(e0, all, na, t, 256); free(all);
                double tk = ge_param(&g, k0), tl = -1, tr = 2, c[2]; int nc = 0;
                for (int i = 0; i < n; i++) { if (t[i] < tk - 1e-9 && t[i] > tl && t[i] > 1e-9) tl = t[i]; if (t[i] > tk + 1e-9 && t[i] < tr && t[i] < 1 - 1e-9) tr = t[i]; }
                if (tl > 0) c[nc++] = tl; if (tr < 1) c[nc++] = tr;
                ok = nc && split_at_keep(e0, c, nc, 0);   /* FUN_18004bb90 -> mode 1: right point first, then left: start piece keeps the record */
            } else if (gp.mode == TR_POINT) {          /* FUN_18004f9f0: Break Two Pieces' break at the point */
                double t = ge_param(&g, xy(p0));
                { double L = ge_len(&g), tl = mc_systol.tol;   /* FUN_18004f9f0: Break Two Pieces' break (G_BREAK2): two pieces, a circle too */
                  if (t * L >= tl && (1 - t) * L >= tl) { GE pc[2] = { ge_sub(&g, 0, t), ge_sub(&g, t, 1) }; replace_keep(e0, pc, 2, ge_param(&g, k0) <= t ? 0 : 1); ok = 1; } }
            } else if (!ge_is_circle(&g) && fabs(gp.len) > mc_systol.tol) {   /* FUN_18004c290(.., -length) -> FUN_18004c400 / FUN_18004bca0
                                                          mode 2: the point at -length from the picked end (creep_along_ent) */
                double L = ge_len(&g), tk = ge_param(&g, k0), d = fabs(tk - 1) <= fabs(tk) && fabs(tk) != fabs(tk - 1) ? L + gp.len : -gp.len;
                double t = L > 0 ? d / L : 0;
                ok = brk_len(e0, t);
            }
            if (!ok) cam_msg[0] = 0;
            mod_end(); break;
        }
        /* trim_123 (sketcher_trim_2.c:2448, param_7 0: post_warning 0x1743 on no intersection): each pair by intersect_ents with the two picks as seeds
           (mode 1 / Automatic: (0, 1); mode 2: (0, 1), both trimmed; mode 3: (0, 2) and (1, 2)), trim_ent_to_t + replace_ent; the third entity trim_ent_to_t(.., 2, ..) */
        if (gp.mode == TR_1 || gp.mode == TR_AUTO) {   /* Automatic executes trim_123(1, ...) like mode 1 */
            h = ent_ge(e1); P2 X;
            if (!mc_inter_near(&g, &h, seed_mid(&g, k0, &h, k1), &X)) geo_msg("The selected objects do not intersect");
            else mc_trim1(e0, k0, X);
        } else if (gp.mode == TR_2) {
            h = ent_ge(e1); P2 X;
            if (!mc_inter_near(&g, &h, seed_mid(&g, k0, &h, k1), &X)) geo_msg("The selected objects do not intersect");
            else { mc_trim1(e0, k0, X); mc_trim1(e1, k1, X); }
        } else if (gp.mode == TR_3) {
            h = ent_ge(e1); GE t3 = ent_ge(e2); P2 X0, X1;
            if (!mc_inter_near(&g, &t3, seed_mid(&g, k0, &t3, k2), &X0) || !mc_inter_near(&h, &t3, seed_mid(&h, k1, &t3, k2), &X1)) geo_msg("The selected objects do not intersect");
            else {
                mc_trim1(e0, k0, X0); mc_trim1(e1, k1, X1);
                Ent M = F.ents[e2]; double a = mc_t_on(&M, X0), b = mc_t_on(&M, X1);
                mc_trim_to_t(&M, 2, mc_t_on(&M, k2), a, b, a);
                r = mc_ge(&M); replace_ent(e2, &r, 1);
            }
        } else if (gp.mode == TR_DIVIDE) {
            int *all, na = sel_list(&all, 1); double t[256]; int n = cuts_on(e0, all, na, t, 256); free(all);
            double tk = ge_param(&g, k0), tl = -1, tr = 2;
            for (int i = 0; i < n; i++) { if (t[i] < tk - 1e-9 && t[i] > tl && t[i] > 1e-9) tl = t[i]; if (t[i] > tk + 1e-9 && t[i] < tr && t[i] < 1 - 1e-9) tr = t[i]; }
            if (ge_is_circle(&g)) {
                if (n < 2) { cam_msg[0] = 0; break; }
                if (tl < 0) tl = t[n - 1] - 1; if (tr > 1) tr = t[0] + 1;
                r = ge_sub(&g, tr, tl + 1); replace_ent(e0, &r, 1);
            } else {
                GE pc[2]; int np = 0;
                if (tl > 0) pc[np++] = ge_sub(&g, 0, tl);
                if (tr < 1) pc[np++] = ge_sub(&g, tr, 1);
                replace_ent(e0, pc, np);
            }
        } else if (gp.mode == TR_POINT) {
            if (ge_is_circle(&g)) { cam_msg[0] = 0; break; }
            trim_ent(e0, k0, ge_nearest(&g, xy(p0), 0), NULL);
        } else {                                       /* length: extend (or shorten, < 0) the end nearest the pick */
            double L = gp.len, tk = ge_param(&g, k0), dt = L / fmax(ge_len(&g), 1e-12);
            r = tk > 0.5 ? ge_sub(&g, 0, 1 + dt) : ge_sub(&g, -dt, 1);
            if (ge_len(&r) > 1e-9 && !ge_is_circle(&g)) replace_ent(e0, &r, 1);
        }
        mod_end(); break; }
    case G_BREAK2: g = ent_ge(e0); if (g.t == GE_POINT) break;
        { double t = ge_param(&g, xy(p0)), L = ge_len(&g), tl = mc_systol.tol; mod_begin();
          /* FUN_18004f9f0 -> lines FUN_18004fec0 + FUN_1800509e0, arcs (a full circle too, no special case) FUN_18004f180 + FUN_1800505d0 (Break2Pieces.c:406): the
             entity becomes two pieces at the point; a piece under tol (|sweep| r / length) -> no break; the picked side keeps the record (FUN_1800509e0) */
          if (t * L >= tl && (1 - t) * L >= tl) { GE pc[2] = { ge_sub(&g, 0, t), ge_sub(&g, t, 1) }; replace_keep(e0, pc, 2, ge_param(&g, k0) <= t ? 0 : 1); }
          else cam_msg[0] = 0;
          mod_end(); }
        break;
    case G_TRIMMANY: {
        int *l, n = sel_list(&l, 0); h = ent_ge(e0); int done = 0;
        if (sel_view_arc(l, n)) { geo_view_arc_msg(); free(l); break; }
        mod_begin();
        if (!gp.trimmode) {                            /* Break (FUN_1800db2d0 *param_2 == 0): every selected curve, last first: FUN_180049ae0,
                                                          else trim_123(1, .., pick t 0.0, break) - the seed at both starts */
            for (int i = n - 1; i >= 0; i--) {
                if (l[i] == e0 || !F.ents[l[i]].type || F.ents[l[i]].type == E_POINT) continue;
                P2 s0 = ent_start(l[i]), s1 = ent_start(e0), M = { (s0.x + s1.x) / 2, (s0.y + s1.y) / 2 };
                if (break_first(l[i], e0, M, M)) done++;   /* pick t 0.0: the start */
            }
            mod_end(); free(l); geo_phase = 0; sel_clear();
            if (done) cam_msg[0] = 0; else geo_msg("The selected objects do not intersect");   /* trim_123 post_warning(0x1743) */
            break;
        }
        {   /* FUN_1800db2d0 -> FUN_1800dc870 (mc_trim_many_one, cui_geo_mc.inc): every curve last first, trimmer skipped; FUN_1800dbf20's side at the projected pick */
            Ent T = F.ents[e0]; P2 sp = xy(p0); double tp = mc_t_on(&T, sp); P2 u = mc_tan(&T, tp), f = ge_nearest(&h, sp, 0), v = { sp.x - f.x, sp.y - f.y };
            double vl = hypot(v.x, v.y); if (vl > 0) { v.x /= vl; v.y /= vl; }
            int flag = (-mc_systol.tol <= v.y * u.x - v.x * u.y) ^ (gp.dir != 0);   /* cross >= -tol, side flag ^ flip (panel command 4) */
            for (int i = n - 1; i >= 0; i--) {
                if (l[i] == e0 || !F.ents[l[i]].type || F.ents[l[i]].type == E_POINT) continue;
                done += mc_trim_many_one(l[i], e0, tp, flag);
            }
        }
        mod_end(); free(l); geo_phase = 0; sel_clear();
        if (done) cam_msg[0] = 0; else geo_msg("The selected objects do not intersect");   /* sketcher_trim_2 post_warning(0x1743) */ break; }
    case G_FILLET: {
        /* make_fillet (decomp/wfacc/core_kernels.c:928): find_fil_gt(.., param_9 = 1: sketcher_fillet_1.c:529); the arc added, each entity trim_ent_to_t(ent, 1, pick, tangent t, 0, intersection t) (:1283). Style = set_fil_small_sweep (FUN_18026c370): Normal 0, Inverse 1, Circle 2. */
        Ent A_ = F.ents[e0], B_ = F.ents[e1]; GE arc;
        if ((A_.type != E_LINE && A_.type != E_ARC) || (B_.type != E_LINE && B_.type != E_ARC)) {
            /* a spline: make_fillet's second path (core_kernels.c:1113-1216): find_fil_ent, cui_geo_mc.inc; trim_ent_to_t's spline branch for the spline */
            McfEnt E[2]; if (!fe_init(&E[0], e0) || !fe_init(&E[1], e1)) { cam_msg[0] = 0; break; }
            int pla = 1;
            for (int i = 0; i < 2 && pla; i++) { if (E[i].kind != 2) continue; double z0_ = 0, zc = 0, o[3][3]; for (int s = 0; s <= 20 && pla; s++) { if (mcs_spline_eval_d(E[i].c, E[i].t1 + (E[i].t2 - E[i].t1) * s / 20.0, 0, o)) { pla = 0; break; } zc = o[0][2]; if (s == 0) z0_ = zc; else if (fabs(zc - z0_) > mc_systol.tol) pla = 0; } }
            if (!pla) { snprintf(cam_msg, sizeof cam_msg, "Fillet of a spline in a plane other than Top: not ported"); break; }
            { double seed[2] = { fe_seed(&E[0], &A_, k0), fe_seed(&E[1], &B_, k1) }, t[2], tl[2]; McfArc Ar; int sty = gp.style == 1 || gp.style == 2 ? gp.style : 0;
              if (!mcf_find_fil_ent(E, gp.r, mc_systol.tol, seed, sty, t, &Ar)) { cam_msg[0] = 0; break; }
              double z = E[0].kind == 0 ? E[0].az : E[0].kind == 1 ? A_.c.z : 0; { const CurveRec *cc = E[0].kind == 2 ? E[0].c : E[1].kind == 2 ? E[1].c : NULL; if (cc) { double o[3][3], t1, t2; mcs_spline_t1t2(cc, &t1, &t2); if (!mcs_spline_eval_d(cc, t1, 0, o)) z = o[0][2]; } }
              memset(&arc, 0, sizeof arc); arc.t = GE_ARC; arc.c = (P2){ Ar.cx, Ar.cy }; arc.r = Ar.r; arc.a0 = Ar.sa; arc.sw = Ar.sw; arc.z = z;
              mod_begin(); geo_live_marked = 1; geo_live_mark(add_ge(&arc));
              if (gp.trim) { if (!fe_inters(E, t, tl)) { tl[0] = t[0]; tl[1] = t[1]; }   /* :708-717: intersect_ents_spl, else the tangent points */
                  for (int i = 0; i < 2; i++) { int id = i ? e1 : e0; if (E[i].kind == 2) mc_trim_spline(id, seed[i], t[i], tl[i]); else { Ent M = F.ents[id]; mc_trim_to_t(&M, 1, seed[i], t[i], 0.0, tl[i]); mc_trim_store(id, &M); } } }
              mod_end(); }
            break; }
        { int sty = gp.style == 1 || gp.style == 2 ? gp.style : 0;
          mcf_set_lollipop(gp.style == 3, gp.v_dist);   /* style 3 Clearance: set_fil_small_sweep(0), set_lollipop_corner(1), set_clearance_offset(+make_fillet param_4) sketcher_fillet_1.c:510-522, :1447 */
          if (!mc_fillet_pair(&A_, &B_, mc_t_on(&A_, k0), mc_t_on(&B_, k1), sty, 0, 0, gp.trim, &arc)) { mcf_set_lollipop(0, 0); cam_msg[0] = 0; break; }
          mcf_set_lollipop(0, 0);
          mod_begin(); geo_live_marked = 1; geo_live_mark(add_ge(&arc));
          if (gp.trim) { mc_fillet_store(e0, &A_); mc_fillet_store(e1, &B_); }
          mod_end(); }
        break; }
    case G_CHAMF: {   /* mccore chamfer_2_ents (FUN_18023a3c0) -> FUN_18023a860 (legs) -> FUN_18023b800 (trim); formats/wireframe.md */
        g = ent_ge(e0); h = ent_ge(e1);
        int arcs_ok = gp.mode <= 1;                    /* pick mask 6 (lines + arcs) for 1 / 2 Distances, mask 2 (lines) otherwise */
        if (!((g.t == GE_LINE || (arcs_ok && g.t == GE_ARC)) && (h.t == GE_LINE || (arcs_ok && h.t == GE_ARC)))) { cam_msg[0] = 0; break; }
        int nx = ge_intersect(&g, &h, x); if (!nx) { cam_msg[0] = 0; break; }
        P2 X = x[0];                                   /* intersect_ents with the pick params: the intersection nearer the picks */
        if (nx == 2 && hypot(x[1].x - k0.x, x[1].y - k0.y) + hypot(x[1].x - k1.x, x[1].y - k1.y) < hypot(x[0].x - k0.x, x[0].y - k0.y) + hypot(x[0].x - k1.x, x[0].y - k1.y)) X = x[1];
        double Lg = ge_len(&g), Lh = ge_len(&h); if (Lg <= 1e-9 || Lh <= 1e-9) { cam_msg[0] = 0; break; }   /* length <= tol: no chamfer */
        double tg = ge_param(&g, X), th0 = ge_param(&h, X), dg = ge_param(&g, k0) - tg, dh = ge_param(&h, k1) - th0;
        double sg = dg == 0 ? 0 : dg > 0 ? 1 : -1, sh = dh == 0 ? 0 : dh > 0 ? 1 : -1;   /* sign(t_pick - t_int) */
        P2 u = ge_dir(&g, tg), v = ge_dir(&h, th0); u.x *= sg; u.y *= sg; v.x *= sh; v.y *= sh;   /* toward the picked sides */
        double d1 = gp.dist > 0 ? gp.dist : 1, d2 = gp.mode == 1 ? gp.d2 : d1;
        double th = acos(fmax(-1, fmin(1, u.x * v.x + u.y * v.y)));   /* angle between the kept sides at the intersection */
        if (gp.mode == 2) {                            /* method 3: the angle is measured from the first line */
            double a = gp.ang * M_PI / 180;
            d2 = d1 * sin(a) / sin(M_PI - a - th);
        }
        if (gp.mode == 3) { if (sin(th / 2) < 1e-9) { cam_msg[0] = 0; break; } d1 = d2 = gp.chw / (2 * sin(th / 2)); }   /* method 4 Width: W / (2 sin(theta / 2)) */
        P2 pa = ge_at(&g, tg + sg * d1 / Lg), pb = ge_at(&h, th0 + sh * d2 / Lh);   /* t = t_int +- d / length (pt_on_ent_from_t) */
        mod_begin();
        r = ge_line(pa, pb, g.z); geo_live_marked = 1; geo_live_mark(add_ge(&r));   /* the live result: the chamfer line */
        /* FUN_18023b800: trim_ent_to_t(ent, 1, t_pick, t_int) keeps the picked side of the INTERSECTION, then FUN_18023bd00
           moves that end to the leg point - so the far part of each entity stays, wherever along it the pick was */
        if (gp.trim) {
            if (!ge_is_circle(&g)) trim_ent(e0, ge_at(&g, sg >= 0 ? 1 : 0), pa, NULL);
            if (!ge_is_circle(&h)) trim_ent(e1, ge_at(&h, sh >= 0 ? 1 : 0), pb, NULL);
        }
        mod_end(); break; }
    case G_FILLETC: case G_CHAMFC:                       /* every chain of the list (create_chain_fillets / make_chamfer_chain take the list) */
        mod_begin();
        for (int i = 0; i < geo_nchains; i++) if (geo_chain_live(&geo_chains[i])) corner_chain(&geo_chains[i], geo_cmd == G_CHAMFC);
        mod_end(); break;
    case G_OFFSET: {
        /* FUN_180018230 do_xform_offset.c:1223: the stored distance always (:1359), never the cursor; direction DAT_180242820: 0 the picked side, 1 the opposite, 2 both (copies doubled :1392-1396, the picked side first, then the opposite :1415-1431); a result radius <= 0: message 3925 */
        g = ent_ge(e0); if (g.t == GE_POINT) break;
        P2 s = xy(p0); double base = fabs(gp.dist);
        double d = g.t == GE_LINE ? base * ge_side(&g, s) : -base * ge_side(&g, s);   /* outside a circle: larger radius */
        mod_begin();
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 0 ? gp.dir == 1 : gp.dir == 0) continue;
            double sgn = pass == 1 ? -1 : 1;
            for (int k = 1; k <= (gp.copies < 1 ? 1 : gp.copies); k++) {
                if (ge_offset(&g, sgn * d * k, &r)) { if (F.ents[e0].type == E_ARC) { r.a0 = F.ents[e0].a0; r.sw = F.ents[e0].sw; }   /* the entity copied, only the radius replaced (do_xform_offset.c, local_a6c8 = local_7e18): the signed sweep and start stay */
                    add_ge_lc(&r, F.ents[e0].level, F.ents[e0].color); }
                else if (g.t == GE_ARC) { geo_msg("Cannot create an arc with a radius of %.*f", cfg_places >= 0 ? cfg_places : 4, g.r - sgn * d * k); break; }   /* do_xform_offset.c:1569-1574 (3925) */
            }
        }
        if (gp.move) mod_del(e0);
        doc_dirty = 1; mod_end(); break; }
    case G_SBLOCK: case G_SCYL: case G_SSPH: case G_SCONE: case G_STOR: {   /* the builds FUN_18000dbc0 / FUN_1800270e0 / FUN_1800093f0 /
                                                           FUN_180030090 / FUN_18002ba60 (cui_surf.c srf_prim_exec) */
        int kind = geo_cmd == G_SBLOCK ? 0 : geo_cmd == G_SCYL ? 1 : geo_cmd == G_SSPH ? 2 : geo_cmd == G_SCONE ? 3 : 4;
        double tol = mc_systol_units(mcam_is_metric(&F))->tol;
        if (!geo_rebuilding && geo_npick >= 2) {         /* the picks set the sizes they were taken for (written into the panel) */
            /* the rubber bands (mastercam.dll draw_rubber_band 0x180331b30 -> FUN_180331dd0, decomp/rubber/rb.c): d = cursor - centre;
               an AutoCursor point (param_2 1) or the free cursor (0: map_cursor_to_3d, on the cursor ray). Screen axes = the graphics
               view's rows (get_x_iso_axis / get_y_iso_axis = SetGrView's matrix, decomp/rubber/isoaxis.c, setgrview.c). A radius
               (types 8 / 0x1b, +0x88): free = the 2D distance through proj_iso_pt (screen x / y); AutoCursor = the 2D distance in the
               rubber band's view (its normal component dropped) */
            double A[3], X[3], Y[3], V[3] = { vmat[6], vmat[7], vmat[8] }, R[3] = { vmat[0], vmat[1], vmat[2] }, U[3] = { vmat[3], vmat[4], vmat[5] };
            srf_prim_frame(kind, gp.cmode, A, X, Y);
            #define DOT(a, b) ((a)[0] * (b)[0] + (a)[1] * (b)[1] + (a)[2] * (b)[2])
            #define RAD(d, N, ac) ((ac) ? sqrt(fmax(0, DOT(d, d) - DOT(d, N) * DOT(d, N))) : hypot(DOT(d, R), DOT(d, U)))
            int sz1 = prim_step(1) == 1, nh = sz1 ? 3 : 2;
            double Ru[3] = { X[0], X[1], X[2] };         /* the torus minor centre's direction (FUN_18002d030 / FUN_18002d2c0) */
            if (kind == 4) {                             /* FUN_18002d030: make_view_k(axis), the start angle's direction in it */
                double M[9]; if (mcsb_view_k(A, M)) { double c = cos(gp.sa * M_PI / 180), sn = sin(gp.sa * M_PI / 180); for (int k = 0; k < 3; k++) Ru[k] = c * M[k] + sn * M[3 + k]; }
            }
            if (sz1) {
                V3 q = p1; double d[3] = { q.x - p0.x, q.y - p0.y, q.z - p0.z };
                if (kind == 0) {                         /* FUN_1800110c0 (pick or rubber band type 0x15): both points through the screen
                                                           (world_to_view gr view) onto the block view's z = 0 plane (map_cursor_to_3d ->
                                                           map_2d_to_view: along the view direction), the delta rotated by -angle, x2 in x
                                                           for anchors 4 / 5 / 6 and in y for 2 / 5 / 8 */
                    double va = DOT(V, A), dv = DOT(d, V);
                    if (fabs(va) > 1e-12) {
                        double s2[3], k; for (int c = 0; c < 3; c++) s2[c] = d[c] - dv * V[c];   /* the screen-plane delta */
                        k = DOT(s2, A) / va; for (int c = 0; c < 3; c++) s2[c] -= k * V[c];   /* onto the plane normal to A */
                        double dx = DOT(s2, X), dy = DOT(s2, Y);
                        if (fabs(gp.rot) > 1e-12) { double c = cos(-gp.rot * M_PI / 180), sn = sin(-gp.rot * M_PI / 180), x0 = dx; dx = c * x0 - sn * dy; dy = c * dy + sn * x0; }
                        int an = gp.mode; if (an == 4 || an == 6 || an == 5) dx *= 2; if (an == 5 || an == 2 || an == 8) dy *= 2;
                        if (!gp.lk_len && fabs(dx) > 1e-9) gp.v_len = dx;   /* FUN_1800109b0: |value| > tol, not 1e38 */
                        if (!gp.lk_w && fabs(dy) > 1e-9) gp.v_w = dy;
                        srf_blk_anchor_signs(&gp.mode, gp.v_len, gp.v_w);   /* FUN_18000d360 */
                    }
                } else {
                    double N[3] = { A[0], A[1], A[2] };  /* type 8 in the solid's view (FUN_180028df0 / FUN_1800323c0 / FUN_18002d780) */
                    if (kind == 2) { double Xc[3], Yc[3]; srf_prim_frame(0, 2, N, Xc, Yc); }   /* type 0x1b: the Cplane's view (FUN_180331dd0) */
                    double r = RAD(d, N, geo_pick_ac[1]);
                    if (r > 1e-10 && r > tol && r < 1e38) {   /* FUN_180028cd0 / FUN_18000ab90 / FUN_18002d6d0: > tol; FUN_180032250: at
                                                           least +0x90 = tan(taper) h (FUN_180032710; tol with Top radius / below tol) */
                        if (kind == 3) { double h2 = gp.dir == 2 ? 2 * gp.v_h : gp.v_h, m = tan(gp.ang * M_PI / 180) * h2;
                                         if (gp.method == 1 || m < tol) m = tol; gp.v_r = r < m ? m : r; }
                        else gp.v_r = r;
                    }
                    if (kind == 4) {                     /* FUN_18002d2c0: the minor centre's direction = the pick's, in the circle's plane */
                        double t = DOT(d, A), u[3], l; for (int c = 0; c < 3; c++) u[c] = d[c] - t * A[c]; l = sqrt(DOT(u, u));
                        if (l > 1e-12) for (int c = 0; c < 3; c++) Ru[c] = u[c] / l;
                    }
                }
            }
            if (geo_npick >= nh) {
                V3 q = sz1 ? p2v : p1; double d[3] = { q.x - p0.x, q.y - p0.y, q.z - p0.z };
                if (kind == 4) {                         /* type 8 centred on the major circle, in the view normal to axis x direction */
                    double C[3] = { p0.x + gp.v_r * Ru[0], p0.y + gp.v_r * Ru[1], p0.z + gp.v_r * Ru[2] }, N[3];
                    double dc[3] = { q.x - C[0], q.y - C[1], q.z - C[2] };
                    N[0] = A[1] * Ru[2] - A[2] * Ru[1]; N[1] = A[2] * Ru[0] - A[0] * Ru[2]; N[2] = A[0] * Ru[1] - A[1] * Ru[0];
                    double r = RAD(dc, N, geo_pick_ac[nh - 1]);
                    if (r > tol && r < 1e38) gp.v_w = r;
                } else if (kind != 2) {                  /* types 0x17 / 0x1a: d in the graphics view, its depth dropped for the free cursor,
                                                           then into the solid's view: the height = its z (FUN_1800109b0 / FUN_180028cd0 /
                                                           FUN_180032250) */
                    if (!geo_pick_ac[nh - 1]) { double dv = DOT(d, V); for (int c = 0; c < 3; c++) d[c] -= dv * V[c]; }
                    double h = DOT(d, A);
                    if (fabs(h) > 1e-10) gp.v_h = h;
                }
            }
            #undef RAD
            #undef DOT
        }
        if (kind == 3) prim_cone_sync();                 /* FUN_1800340d0 */
        double prm[6] = { 0 };
        switch (kind) {
        case 0: prm[0] = gp.v_len; prm[1] = gp.v_w; prm[2] = gp.v_h; prm[3] = gp.rot; prm[4] = gp.mode; prm[5] = gp.dir; break;
        case 1: prm[0] = gp.v_r; prm[1] = gp.v_h; prm[2] = gp.sa; prm[3] = gp.ea; prm[5] = gp.dir; break;
        case 2: prm[0] = gp.v_r; prm[1] = gp.sa; prm[2] = gp.ea; break;
        case 3: prm[0] = gp.v_r; prm[1] = gp.v_w; prm[2] = gp.v_h; prm[3] = gp.sa; prm[4] = gp.ea; prm[5] = gp.dir; break;
        case 4: prm[0] = gp.v_r; prm[1] = gp.v_w; prm[2] = gp.sa; prm[3] = gp.ea; break;
        }
        if (!srf_prim_exec(kind, p0, prm, gp.cmode, geo_level, geo_color, cam_msg, sizeof cam_msg)) break;
        doc_dirty = 1; break; }
    case G_SLOFT:                                        /* CreateLoftOrRuledSurface (cui_surf.c srf_loft_exec) */
        if (!srf_loft_exec(geo_chains, geo_nchains, gp.mode, geo_level, geo_color, cam_msg, sizeof cam_msg)) break;
        doc_dirty = 1; break;
    case G_CEDGE1: if (ce_sld_s >= 0) { if (ce_sld_edge_one(ce_sld_s, ce_sld_e, geo_level, geo_color, cam_msg, sizeof cam_msg)) doc_dirty = 1; break; }   /* FUN_1802a2940 event 0x4000: sld_convert_tag_to_edges -> ps_edge_to_mc_curve */
    if (!srf_crv_edge_one(geo_ent[0], cc_uv[0], cc_uv[1], gp.v_ang, geo_level, geo_color, cam_msg, sizeof cam_msg)) break; doc_dirty = 1; break;   /* FUN_1802a2940 -> MakeSurfaceEdgeCurve */
    case G_CEDGEA: {                                     /* FUN_1802c4a70 -> MakeAllSurfaceEdgeCurves per selected surface */
        int oo = gp.method, made = 0, n0 = F.nents;
        if (!oo) { for (int i = 0; i < ce_n; i++) if (srf_crv_edge_all(ce_l[i], gp.v_ang, geo_level, geo_color, cam_msg, sizeof cam_msg)) doc_dirty = 1; }
        else if (ce_n) ce_open_edges(ce_l, ce_n, gp.v_tol, geo_level, geo_color, &made, cam_msg, sizeof cam_msg);   /* FUN_1802c4a70 (:978): surface_open_edges over the selected surfaces */
        for (int i = 0; i < ce_nsel; i++) ce_sld_edges(ce_sel[i].si, ce_sel[i].fi, oo, geo_level, geo_color, &made, cam_msg, sizeof cam_msg);   /* the rest of FUN_1802c4a70: solids (0x10000) and the faces of the temp file: sld_convert_tag_to_edges(tag, list, Only Open) */
        if (F.nents > n0) doc_dirty = 1;                   /* the modified flag when curves were made (be9d14b, FUN_1802c4a70) */
        if (oo && !made && !cam_msg[0]) snprintf(cam_msg, sizeof cam_msg, "No open edges found");   /* :997 DisplayWarning(mccadres 0x2ef) [the text of string 751 not read: this wording is ours] */
        break; }
    case G_CDYN: cdyn_build(); break;                        /* FUN_180282390: create_nurbs_from_points / make_spline_from_pts (cui_spline.c sp_fit_points) */
    case G_CFLOW:                                        /* FUN_180287870 -> CreateSurfaceFlowLines (mcsrfcrv.c) */
        cd_built = 1;                                     /* the prompt 0x1751 follows the build (FUN_180287a40) */
        if (!srf_crv_flow_exec(geo_ent[0], gp.method, gp.v_n, gp.v_h, gp.v_dist, gp.dir, gp.v_tol, geo_level, geo_color, cam_msg, sizeof cam_msg)) break;
        doc_dirty = 1; break;
    case G_STRIMS:                                       /* slot [2] FUN_1800903e0 -> trim_surfs_to_surfs (cui_srftrim.c, mcsrftrim.c) */
        if (srf_trims_exec(gp.method, gp.lines, gp.style, gp.round, gp.mode, gp.del, geo_level, geo_color, cam_msg, sizeof cam_msg)) doc_dirty = 1;
        break;
    case G_SEXT:                                         /* FUN_180337330 -> extend_surf (cui_srftrim.c, mcsrfext.c) */
        if (geo_ent[0] < 0 || geo_ent[0] >= F.nents || !F.ents[geo_ent[0]].type) break;
        if (srf_ext_exec(geo_ent[0], gp.v_dist, gp.mode, gp.lines, gp.del, geo_level, geo_color, cam_msg, sizeof cam_msg) > 0) doc_dirty = 1;   /* -1: get_plane_info opened */
        break;
    case G_CCONST:                                       /* FUN_1802980f0 -> CreateConstantParameterCurves (mcsrfcrv.c) */
        if (!srf_crv_const_exec(geo_ent[0], cc_uv[0], cc_uv[1], gp.v_tol, gp.method, geo_level, geo_color, cam_msg, sizeof cam_msg)) break;
        doc_dirty = 1; break;
    case G_SSOLID:                                       /* FUN_180310fa0 + FUN_180312040 (cui_surf.c srf_solid_exec) */
        if (!srf_solid_exec(gp.style, geo_level, cam_msg, sizeof cam_msg)) break;
        doc_dirty = 1; break;
    case G_SFLAT:                                        /* FUN_18033d800 (cui_surf.c srf_flat_exec) */
        if (!srf_flat_exec(geo_chains, geo_nchains, geo_level, geo_color, cam_msg, sizeof cam_msg)) break;
        doc_dirty = 1; break;
    case G_SEXTR: {                                      /* ExtrudeSurface: FUN_18001b2a0 / FUN_180018100 / FUN_1800254a0 + FUN_18001f210 (cui_surf.c srf_extr_exec) */
        double prm[12] = { gp.v_h, gp.v_w, gp.v_r, gp.v_dist, gp.v_ang, ex_ax[0], ex_ax[1], ex_ax[2], gp.dir, gp.inverted, gp.style, 0 };
        int *l = NULL, nl = 0;
        int made = srf_extr_exec(geo_chains, geo_nchains, prm, geo_level, geo_color, &l, &nl, cam_msg, sizeof cam_msg);
        gp.v_dist = prm[3]; srf_extr_axis(ex_ax);                            /* setParams: the dialog's axis */
        if (made) { geo_live_marked = 1; for (int i = 0; i < nl; i++) geo_live_mark(l[i]); doc_dirty = 1; }
        break; }
    case G_SDRAFT:                                       /* DraftSurface's build FUN_180125d80 (cui_surf.c srf_draft_exec); Plane mode NOT PORTED */
        if (gp.method != 0) { snprintf(cam_msg, sizeof cam_msg, "Draft to a plane (get_plane_info, surf_slice) is NOT PORTED"); break; }
        if (!srf_draft_exec(geo_chains, geo_nchains, gp.v_len, gp.v_ang, gp.dir, gp.inverted, gp.dir == 2 && gp.multi, geo_level, geo_color, cam_msg, sizeof cam_msg)) break;
        doc_dirty = 1; break;
    case G_SREV:                                         /* FUN_18008c9e0: create_revs over the chains (cui_surf.c) */
        if (!srf_rev_exec(geo_chains, geo_nchains, gp.sa, gp.ea, gp.dir, geo_level, geo_color, cam_msg, sizeof cam_msg)) break;
        doc_dirty = 1; break;
    case G_SPLCURV: sp_curv_run(); break;                 /* cui_spline.c */
    case G_XROLL: {                                      /* FUN_18002bf70: the rolled copies; Move deletes the originals (OK, MC l.5374) */
        Chain lv[256]; int nl = 0;
        for (int i = 0; i < geo_nchains; i++) if (geo_chain_live(&geo_chains[i])) lv[nl++] = geo_chains[i];
        mod_begin();
        int made = xroll_run(lv, nl, geo_level, geo_color, cam_msg, sizeof cam_msg);
        if (made > 0 && xrl.mode == 0) for (int i = 0; i < nl; i++) for (int k = 0; k < lv[i].n; k++) { int e = lv[i].ent[k]; if (e >= 0 && e < F.nents && F.ents[e].type) mod_del(e); }
        mod_end(); break; }
    case G_OFFSETC:                                      /* FUN_18001bc50: per chain, direction 0 = 'L' (the only one this panel has) */
        mod_begin();
        for (int i = 0; i < geo_nchains; i++) if (geo_chain_live(&geo_chains[i])) {   /* direction +0x18: 0 'L', 1 'R', 2 both (do_xform_offset_ctour.c:985-1000) */
            if (gp.dir != 1) offset_chain(&geo_chains[i], 'L', gp.dist);
            if (gp.dir != 0) offset_chain(&geo_chains[i], 'R', gp.dist); }
        if (gp.move == 1) for (int i = 0; i < geo_nchains; i++) for (int k = 0; k < geo_chains[i].n; k++) mod_del(geo_chains[i].ent[k]);   /* Mode Move (res 6460 radio 100, DAT_1802429e0 = 0): the chains blanked (:1027), UnBlankAndDelete at the end (:727-734) */
        mod_end(); break;
    }
}

/* ---- Break Many on spline curves (S_ID 0x8 / NB_ID 0x80): every piece a line */
static const CurveRec *ent_spline(int e) {
    const Ent *en = &F.ents[e]; if (en->type != E_SPLINE || en->crv < 0 || en->crv >= F.ncrvs) return NULL;
    const CurveRec *c = &F.crvs[en->crv]; return mcs_spline_ok(c) ? c : NULL;
}
static void spl_pt(const CurveRec *c, double t, double p[3]) { double o[3][3]; if (mcs_spline_eval_d(c, t, 0, o)) { p[0] = p[1] = p[2] = 0; return; } memcpy(p, o[0], sizeof(double) * 3); }
static double d3(const double *a, const double *b) { return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }
/* a 3D line with entity e's attributes (store_ent with the entity's attributes) */
static int add_line3(int e, const double *a, const double *b) {
    if (d3(a, b) < 1e-9) return -1;
    Ent src = F.ents[e]; V3 z0 = { 0, 0, 0 };
    int q = mcam_new_ent(&F, E_LINE, (V3){ a[0], a[1], a[2] }, (V3){ b[0], b[1], b[2] }, z0, 0, 0, 0, src.level, src.color);
    if (q >= 0) { Ent *n = &F.ents[q]; n->ln_style = src.ln_style >= 1 ? src.ln_style : 1; n->pt_style = src.pt_style; n->width = src.width >= 1 ? src.width : 1;
        for (int k = n->first_poly; k < n->first_poly + n->npoly && k < F.npolys; k++) { F.polys[k].ln_style = (uint8_t)n->ln_style; F.polys[k].width = (uint8_t)n->width; } doc_dirty = 1; }
    return q;
}
/* creep_rec over a spline (mccore init_creep_info @1802581b0, creep_along_ent -> FUN_180257c10, FUN_180257a40 the clamp, step
   FUN_180013db0 (S_ID) / FUN_180013d50 (NB_ID) from ent +0xa8: sqrt(8 tol / +0xa8) (<= 1; S_ID: 1 when +0xa8 <= tol; NB_ID: (t2 - t1) /
   (8 x the control point count +0xcc) when +0xa8 <= tol); backwards: the step negated, from t2 */
static double rdd_(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static void spl_creep_init(SplCreep *k, const CurveRec *c, int rev) {
    double tol = mc_systol.tol, a8 = rdd_(c->rec + 0xa8);
    memset(k, 0, sizeof *k); k->c = c; k->rev = rev; mcs_spline_t1t2(c, &k->t1, &k->t2);
    if (c->cls == 0x8) k->step = a8 <= tol ? 1.0 : fmin(1.0, sqrt(tol * 8.0 / a8));
    else { int32_t nc; memcpy(&nc, c->rec + 0xcc, 4); k->step = tol < a8 ? sqrt(tol * 8.0 / a8) : (k->t2 - k->t1) / (double)(nc << 3); }
    if (rev) k->step = -k->step;
    k->t = k->tp = rev ? k->t2 : k->t1; spl_pt(c, k->t, k->P);
}
static int spl_clamp(const SplCreep *k, double *t) {
    if (!k->rev) { if (k->t2 <= *t) { *t = k->t2; return 1; } } else if (*t <= k->t1) { *t = k->t1; return 1; }
    return 0;
}
static void spl_creep(SplCreep *k, double d, double out[3]) {
    double tol = mc_systol.tol, tr; int done = d <= k->acc;
    while (!done) {
        k->tp = k->t; k->t += k->step; int end = spl_clamp(k, &k->t);
        double prev[3]; memcpy(prev, k->P, sizeof prev); spl_pt(k->c, k->t, k->P);
        k->seg = d3(prev, k->P); k->acc += k->seg;
        if (end || d < k->acc) done = 1;
        if (end) { k->tp = k->t - k->step; spl_clamp(k, &k->tp); double q[3]; spl_pt(k->c, k->tp, q); k->seg = d3(q, k->P); }
    }
    double r = k->acc - d;
    if (k->seg <= tol) tr = k->t;
    else {
        tr = k->t - (k->t - k->tp) * r / k->seg; double q[3]; spl_pt(k->c, tr, q);
        if (tol < fabs(r - d3(k->P, q))) {
            double a = k->t, fb = r - k->seg, b = k->tp;
            for (int i = 0; i < 20; i++) {
                double m = (a + b) * 0.5; tr = m; spl_pt(k->c, m, q); double f = r - d3(k->P, q);
                if (fabs(f) < tol) break;
                if (fb * f < 0) a = m; else { fb = f; b = m; }
            }
        }
    }
    spl_clamp(k, &tr); spl_pt(k->c, tr, out);
}
/* length_ent -> CurveLength (MaCurve vtable +0xc0: NOT DECODED) - ours: the arc length by Gauss-Legendre (5 points) on 64 spans */
static double spl_length(const CurveRec *c) {
    static const double gx[5] = { -0.9061798459386640, -0.5384693101056831, 0, 0.5384693101056831, 0.9061798459386640 },
                        gw[5] = { 0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891 };
    double t1, t2, L = 0; mcs_spline_t1t2(c, &t1, &t2); int ns = 64; double h = (t2 - t1) / ns;
    for (int i = 0; i < ns; i++) for (int j = 0; j < 5; j++) { double o[3][3]; if (mcs_spline_eval_d(c, t1 + h * (i + 0.5 + 0.5 * gx[j]), 1, o)) continue;
        L += 0.5 * h * gw[j] * sqrt(o[1][0] * o[1][0] + o[1][1] * o[1][1] + o[1][2] * o[1][2]); }
    return L;
}
/* FUN_1800af2a0 on a spline (not an arc: lines between the creep points); from the start (param_4 0.0 nearer t1) */
static int brkmany_spline(int e, const CurveRec *c, double len) {
    double L = spl_length(c), tl = mc_systol.tol, t1, t2; if (L <= 0 || len <= 0) return 0;
    mcs_spline_t1t2(c, &t1, &t2); int fwd = fabs(0.0 - t1) <= fabs(0.0 - t2);
    int n = (int)(short)(int)ceil(L / len); if (L - (n - 1) * len < tl) n--; if (n < 1) n = 1;
    double prev[3], cur[3], E[3] = { F.ents[e].b.x, F.ents[e].b.y, F.ents[e].b.z }, S[3] = { F.ents[e].a.x, F.ents[e].a.y, F.ents[e].a.z };
    for (int k = 0; k <= n; k++) {
        SplCreep cr; spl_creep_init(&cr, c, 0);
        if (k == n) memcpy(cur, fwd ? E : S, sizeof cur); else spl_creep(&cr, fwd ? k * len : L - k * len, cur);
        if (k > 0) add_line3(e, fwd ? prev : cur, fwd ? cur : prev);
        memcpy(prev, cur, sizeof prev);
    }
    return 1;
}
/* break_spline_const_error (mccadutil @180062630): from t1, brk_spline_const_err(curve_min_step, curve_max_step, tol) until t reaches
   t2 - 1e-4; a step longer than tol_sys makes a line, or extends the last one when check_colinear(last start, last end, P) */
static int brk_spline_lines(int e, const CurveRec *c, double tol) {
    double t1, t2, t, Q[3]; mcs_spline_t1t2(c, &t1, &t2); t = t1;
    const McSysTol *st = &mc_systol; int np = 1, cap = 64; double (*p)[3] = malloc(sizeof *p * cap); if (!p) return 0;
    spl_pt(c, t, p[0]);                                   /* p[np - 1] = the last line's end (local_5330), p[np - 2] its start */
    for (int it = 0; it < 1000000 && t - t2 < -0.0001; it++) {
        double t0 = t; mcs_brk_spline_const_err(c, st->curve_min_step, st->curve_max_step, tol, 0, st->tol, &t);
        if (!(t > t0)) break;
        spl_pt(c, t, Q);
        if (!(d3(p[np - 1], Q) > st->tol)) continue;
        int col = 0;
        if (np >= 2) { const double *ls = p[np - 2], *P0 = p[np - 1];   /* check_colinear (cui_xroll.c rl_colinear) */
            double u[3] = { P0[0] - ls[0], P0[1] - ls[1], P0[2] - ls[2] }, v[3] = { Q[0] - P0[0], Q[1] - P0[1], Q[2] - P0[2] };
            double lu = sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]), lv = sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
            if (lu <= st->tol || lv <= st->tol) col = 1;
            else if ((u[0]*v[0]+u[1]*v[1]+u[2]*v[2]) / (lu * lv) >= 0.99985) { double w[3] = { Q[0]-ls[0], Q[1]-ls[1], Q[2]-ls[2] }, lw = sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);
                double cx = u[1]*w[2]-u[2]*w[1], cy = u[2]*w[0]-u[0]*w[2], cz = u[0]*w[1]-u[1]*w[0]; col = lw <= 0 || sqrt(cx*cx+cy*cy+cz*cz) / lw < st->tol * 2; } }
        if (col) memcpy(p[np - 1], Q, sizeof Q);           /* replace_ent: the last line extended to P */
        else { if (np == cap) { cap *= 2; p = realloc(p, sizeof *p * cap); } memcpy(p[np++], Q, sizeof Q); }   /* store_ent: a new line */
    }
    int made = 0; for (int i = 0; i + 1 < np; i++) made += add_line3(e, p[i], p[i + 1]) >= 0;
    free(p); return made > 0;
}

/* break_spline_arcs (mccore @180160440, viewer/mcbiarc.c) of spline e with the chord error err, the pieces stored with its attributes in the decoded order (store_ent: arc 1, arc 2, line of each stretch).
   find_view_from_ents on the two chords start -> middle -> end decides the plane (:90); only a spline in a plane parallel to Top is handled here: anything else -> *param_5 = 0 -> the caller's warning 0x1746 (mccadutilres 5958)
   (3D construction with a flat spline in another plane would go through SplineIsFlat / make_view_k in Mastercam: NOT PORTED, reported the same way). */
/* FilterEntities (mccad @18006f960) -> FilterChains -> FUN_1800e7c90 -> FUN_1800ee5d0 (filter_pock_boundaries) -> FUN_180948e60 (viewer/mcfilter.c, oracle tools/tests/filter_oracle.sh): only when more than one entity (sketcher_trim_3.c:1174). The pieces are chained
   (chain_all_selected; chain_to_pock_bound takes each curve in chain direction, an arc reversed = sa + sw, -sw: break_and_append_ent); the whole chain's gts are filtered, the output stored with the main colour and level (FUN_1800f0970 / store_ent, entities with length <= tol dropped;
   FUN_1800f1330 reverses an entity not continuing the previous end by more than tol); on failure the chain stays as it was (FilterChains :325-328). Inferred, not decoded: the chain start and direction (chain_all_selected's FUN_1801dec60 is not ported; here the first stored piece from the spline's start, along the spline). */
static int flt_rev(McflGt *g, int rev, double tip[2]) {
    if (g->k == 'L') { if (rev) { double x = g->a, y = g->b; g->a = g->c; g->b = g->d; g->c = x; g->d = y; } tip[0] = g->c; tip[1] = g->d; return 1; }
    if (rev) { g->d = g->d + g->e; g->e = -g->e; } double ea = g->d + g->e; tip[0] = g->a + g->c * cos(ea); tip[1] = g->b + g->c * sin(ea); return 1;
}
static void flt_ends(const McflGt *g, double s[2], double e[2]) {
    if (g->k == 'L') { s[0] = g->a; s[1] = g->b; e[0] = g->c; e[1] = g->d; return; }
    s[0] = g->a + g->c * cos(g->d); s[1] = g->b + g->c * sin(g->d); e[0] = g->a + g->c * cos(g->d + g->e); e[1] = g->b + g->c * sin(g->d + g->e);
}
static int brk_filter_store(const McflGt *in, int n, double z, double err) {
    double tl = mc_systol.tol; McflGt *g = malloc(sizeof *g * (size_t)n); if (!g) return 0; memcpy(g, in, sizeof *g * (size_t)n);
    int m = n, ok;
    if (m == 1 && g[0].k == 'A' && !(fabs(g[0].e) < 6.283184307179586 || fabs(g[0].e) == 6.283184307179586)) ok = 1;   /* FUN_1800ee5d0: a full circle is not filtered */
    else ok = mcfl_filter(g, &m, err, 0.001, 1500.0, tl, mc_systol.nc_tol);
    if (!ok || m < 1) { free(g); return 0; }
    double last[2] = { 0, 0 }; int have = 0, made = 0;
    for (int i = 0; i < m; i++) {
        GE r; memset(&r, 0, sizeof r); double L, s[2], e[2]; flt_ends(&g[i], s, e);
        if (g[i].k == 'L') { P2 a = { g[i].a, g[i].b }, b = { g[i].c, g[i].d }; if (have && tl < hypot(a.x - last[0], a.y - last[1])) { P2 t = a; a = b; b = t; } r = ge_line(a, b, z); L = hypot(b.x - a.x, b.y - a.y); }
        else { double a0 = g[i].d, sw = g[i].e; if (have && tl < hypot(s[0] - last[0], s[1] - last[1])) { a0 = a0 + sw; sw = -sw; } r.t = GE_ARC; r.c = (P2){ g[i].a, g[i].b }; r.r = g[i].c; r.a0 = a0; r.sw = sw; r.z = z; L = fabs(sw) * g[i].c; }
        if (L <= tl) continue;
        if (add_ge(&r) >= 0) made++; flt_ends(&g[i], s, e); last[0] = e[0]; last[1] = e[1]; have = 1;
    }
    free(g); return made > 0;
}
static int brk_spline_arcs(int e, const CurveRec *c, double err, int filter) {
    double t1, t2; mcs_spline_t1t2(c, &t1, &t2); double P0[3], Pm[3], P1[3]; spl_pt(c, t1, P0); spl_pt(c, (t1 + t2) * 0.5, Pm); spl_pt(c, t2, P1);
    double tl = mc_systol.tol; if (fabs(Pm[2] - P0[2]) > tl || fabs(P1[2] - P0[2]) > tl) { snprintf(cam_msg, sizeof cam_msg, "Spline does not lie in the construction plane."); return 0; }
    int cap = 1 << 14; McbPiece *pc = malloc(sizeof *pc * cap); if (!pc) return 0;
    int n = mcb_break_spline_arcs(c, err, tl, pc, cap); if (n > cap) n = cap;
    if (filter && n > 1) {   /* chains of pieces joined within chain_tol, each filtered */
        double ct = mc_systol.chain_tol, tip[2] = { P0[0], P0[1] }; McflGt *g = malloc(sizeof *g * (size_t)n); int m = 0, okall = g != NULL;
        for (int i = 0; okall && i <= n; i++) {
            McflGt q; memset(&q, 0, sizeof q); double s[2] = { 0, 0 }, en[2] = { 0, 0 }; int rev = 0, join = 0;
            if (i < n) { if (pc[i].kind) { q.k = 'A'; q.a = pc[i].cx; q.b = pc[i].cy; q.c = pc[i].r; q.d = pc[i].sa; q.e = pc[i].sw; } else { q.k = 'L'; q.a = pc[i].x1; q.b = pc[i].y1; q.c = pc[i].x2; q.d = pc[i].y2; }
                flt_ends(&q, s, en); double ds = hypot(s[0] - tip[0], s[1] - tip[1]), de = hypot(en[0] - tip[0], en[1] - tip[1]); rev = de < ds; join = (rev ? de : ds) <= ct || m == 0; }
            if (i == n || !join) { if (m && !brk_filter_store(g, m, P0[2], err)) { GE *ge = malloc(sizeof(GE) * (size_t)m); if (ge) { for (int k = 0; k < m; k++) { GE r; memset(&r, 0, sizeof r); if (g[k].k == 'L') { P2 a = { g[k].a, g[k].b }, b = { g[k].c, g[k].d }; r = ge_line(a, b, P0[2]); } else { r.t = GE_ARC; r.c = (P2){ g[k].a, g[k].b }; r.r = g[k].c; r.a0 = g[k].d; r.sw = g[k].e; r.z = P0[2]; } ge[k] = r; } add_like(e, ge, m); free(ge); } } m = 0; if (i == n) break; flt_ends(&q, s, en); tip[0] = s[0]; tip[1] = s[1]; rev = 0; }
            flt_rev(&q, rev, tip); g[m++] = q;
        }
        free(g); free(pc); return 1;
    }
    GE *ge = malloc(sizeof(GE) * n ? sizeof(GE) * (size_t)n : 1); if (!ge) { free(pc); return 0; }
    for (int i = 0; i < n; i++) { P2 a = { pc[i].x1, pc[i].y1 }, b = { pc[i].x2, pc[i].y2 }, q = { pc[i].cx, pc[i].cy }; ge[i] = pc[i].kind ? ge_arc(q, pc[i].r, pc[i].sa, pc[i].sw, P0[2]) : ge_line(a, b, P0[2]); }
    if (n) add_like(e, ge, n); free(ge); free(pc); return n > 0;
}

/* Break Many's pieces of one curve (FUN_1800af2a0, called with param_4 0.0 = from the start): ceil(L / len) pieces, the last one
   the remainder (one fewer when the remainder is under tol); lines and chords are lines between creep_along_ent points, an arc
   stays arcs when arcs (param_5 = CreateCurves; 0 for the Tolerance method's arcs) */
static int brkmany_ent(int e, double len, int arcs) {
    GE g = ent_ge(e); if (g.t == GE_POINT) return 0;
    double L = ge_len(&g), tl = mc_systol.tol; if (L <= 0 || len <= 0) return 0;
    int n = (int)ceil(L / len); if (L - (n - 1) * len < tl) n--; if (n < 1) n = 1;
    int rev = F.ents[e].type == E_ARC && F.ents[e].sw < 0;   /* a clockwise arc's start is the GE's end */
    GE *pc = malloc(sizeof(GE) * n); if (!pc) return 0;
    for (int k = 0; k < n; k++) {
        double a = k * len / L, b = k == n - 1 ? 1 : (k + 1) * len / L;
        if (rev) { double s = 1 - b; b = 1 - a; a = s; }
        pc[k] = g.t == GE_ARC && !arcs ? ge_line(ge_at(&g, rev ? b : a), ge_at(&g, rev ? a : b), g.z) : ge_sub(&g, a, b);
    }
    add_like(e, pc, n); free(pc); return 1;
}
/* Break Many (sketcher_trim_3 FUN_1800afcc0; the originals FUN_1800af8d0): methods 0 Number / 1 Tolerance / 2 Exact distance /
   3 Rounded distance; the panel's other boxes get what this run made (FUN_1800bb8f0 pushes the struct back): after Number the two
   distances = the shortest piece length, after a distance method the Number = the fewest pieces */
static int brkmany(const int *l, int n) {
    GeoParams *P = &gp; double tl = mc_systol.tol, cdev = mc_systol.curve_chord_dev; int done = 0;
    if (P->method == 0) P->v_dist = P->v_step = 1e38; else if (P->method >= 2) P->v_n = 0x7fffffff;
    for (int i = 0; i < n; i++) {
        int e = l[i]; if (!F.ents[e].type || F.ents[e].type == E_POINT) continue;
        const CurveRec *sc = ent_spline(e);
        if (F.ents[e].type == E_SPLINE) {                /* S_ID / NB_ID: FUN_1800af2a0's lines, or method 1's break_spline_const_error */
            if (!sc) continue;
            double L = spl_length(sc), len = 0, floor_len = tl / 100.0 + tl; int ok = 0;
            if (P->method == 1) { if (P->v_tol < cdev) P->v_tol = cdev;
                if (P->lines) ok = brk_spline_lines(e, sc, P->v_tol);
                else ok = brk_spline_arcs(e, sc, P->v_tol, 1);   /* sketcher_trim_3.c:1172-1176: break_spline_arcs + FilterEntities when > 1 piece */
            } else {
                if (P->method == 0) { if (P->v_n < 1) P->v_n = 1; len = L / P->v_n; if (len < P->v_dist) P->v_dist = len; if (len < P->v_step) P->v_step = len; }
                else if (P->method == 2) { if (P->v_dist < tl) P->v_dist = tl; len = P->v_dist; int c = (int)(short)(int)floor(L / len + 0.5); if (c < 1) c = 1; if (c < P->v_n) P->v_n = c; }
                else { if (P->v_step < tl) P->v_step = tl; int c = (int)(short)(int)floor(L / P->v_step + 0.5); if (c < 1) c = 1; len = L / c; if (c < P->v_n) P->v_n = c; }
                if (len < floor_len) len = floor_len;
                ok = brkmany_spline(e, sc, len);
            }
            if (!ok) continue;
            if (P->action == 2) mod_blank(e); else if (P->action != 1) mod_del(e);
            done++; continue;
        }
        GE g = ent_ge(e); double L = ge_len(&g), len = 0, floor_len = tl / 100.0 + tl; int arcs = !P->lines, untouched = 0;
        if (P->method == 0) {
            if (P->v_n < 1) P->v_n = 1;
            len = L / P->v_n; if (len < P->v_dist) P->v_dist = len; if (len < P->v_step) P->v_step = len;
            if (len < floor_len) len = floor_len;
        } else if (P->method == 1) {
            if (P->v_tol < cdev) P->v_tol = cdev;
            if (g.t == GE_LINE || (g.t == GE_ARC && !P->lines)) untouched = 1;   /* lines, and arcs kept as arcs: as they are */
            else { double step = sqrt(P->v_tol * 8.0 / g.r); len = L / fabs(g.sw / step); arcs = 0; }
        } else if (P->method == 2) {
            if (P->v_dist < tl) P->v_dist = tl;
            len = P->v_dist; int c = (int)(short)(int)floor(L / len + 0.5); if (c < 1) c = 1; if (c < P->v_n) P->v_n = c;
            if (len < floor_len) len = floor_len;
        } else {
            if (P->v_step < tl) P->v_step = tl;
            int c = (int)(short)(int)floor(L / P->v_step + 0.5); if (c < 1) c = 1;
            len = L / c; if (c < P->v_n) P->v_n = c;
            if (len < floor_len) len = floor_len;
        }
        if (untouched || !brkmany_ent(e, len, arcs)) continue;
        if (P->action == 2) mod_blank(e); else if (P->action != 1) mod_del(e);   /* 0 Delete, 1 Keep, 2 Blank */
        done++;
    }
    P->n = P->v_n;
    return done;
}

/* Break Many's live run on its selection (select_ents' list, kept for the rebuilds: FUN_1800afcc0(param_1, local_4e8, local_4f4)) */
static int *brkm_l, brkm_n;
static void brkmany_run(void) {
    int from = F.nents, nu = nundo;
    geo_live_n = 0; geo_live_marked = 0;
    mod_begin(); brkmany(brkm_l, brkm_n); mod_end();
    if (nundo != nu) geo_live_start(from);
    scene_reupload(); overlay_dirty = 1;
}

/* commands on the selection (Apply) */
void geo_apply(void) {
    if (geo_cmd == G_BBOX) { bb_button(1); return; }     /* the Bounding Box panel's Apply (code 1, cui_bbox.c) */
    if (geo_cmd == G_STRIMS) {                           /* FUN_18031ed90 code 1: commit (slot [3] FUN_18008f660, set 2 reset), then select_multi_surfs again */
        if (geo_live && geo_live_valid()) geo_live_commit();
        srf_trims_restart(); sel_clear(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; geo_applied = 1; return;
    }
    if (geo_cmd == G_CEDGEA) {                            /* FUN_1802c3f10 code 1: the live result committed, select_ents again (FUN_1802c4f10); with nothing live yet, Enter's job */
        if (geo_live && geo_live_valid()) { geo_live_commit(); ce_built = 0; sel_clear(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; geo_applied = 1; return; }
        if (!ce_built) ce_enter();                        /* built with nothing made (no open edges): ce_enter would press OK again and recurse without end (seen 2026-11-12 through
                                                           f17a969's dangling else); the OK then just ends the command */
        return;
    }
    if (geo_cmd == G_BREAKN && geo_live) {               /* code 1 with the pieces live: commit, then select_ents again (an empty
                                                            selection is no "every entity" here: the new selection starts empty) */
        int any = 0; for (int i = 0; i < F.nents && !any; i++) any = is_sel(i);
        if (!any) { geo_live_commit(); geo_applied = 1; cam_msg[0] = 0; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); return; }
    }
    int *l, n = sel_list(&l, geo_cmd == G_PT_END || geo_cmd == G_PT_SMALL || geo_cmd == G_BREAKPTS), done = 0;
    V3 z0 = { 0, 0, 0 };
    if (!n && geo_cmd != G_TRIMMANY) { cam_msg[0] = 0; free(l); return; }
    {   /* nothing selected = every entity (sel_list's all-set): the view arcs are left out of it; refused only when one is
           selected (ours, the XY-only functions) */
        int any = 0; for (int i = 0; i < F.nents && !any; i++) any = is_sel(i);
        if (!any && geo_cmd != G_PT_END && geo_cmd != G_PT_SMALL) { int k = 0; for (int i = 0; i < n; i++) if (!(F.ents[l[i]].type == E_ARC && F.ents[l[i]].vw)) l[k++] = l[i]; n = k; }
        else if (geo_cmd != G_PT_END && geo_cmd != G_PT_SMALL && sel_view_arc(l, n)) { geo_view_arc_msg(); free(l); return; }
    }
    int from = F.nents;
    switch (geo_cmd) {
    case G_PT_END: {
        V3 *have = NULL; int nh = 0;
        /* create_point_endpoints.c:134-137, :184: curve_endpoints of every curve (mask 0xc8e - no points), both ends (duplicates among this run's points dropped by add_repaint_pt); each projected to the construction plane (proj_pt_cp) and stored */
        for (int i = 0; i < n; i++) {
            Ent e = F.ents[l[i]]; if (e.type == E_POINT) continue;
            V3 c[2] = { e.a, e.b };
            for (int k = 0; k < 2; k++) { if (!cfg_constr_3d) c[k].z = constr_depth; int dup = 0; for (int q = 0; q < nh && !dup; q++) dup = mc_pt_equal(have[q], c[k]);   /* add_repaint_pt(pt, 0, 0) (mastercam.dll @18032d160): a point pt_equal to one already in this run's list is not added */
                if (!dup) { have = realloc(have, sizeof(V3) * (size_t)(nh + 1)); have[nh++] = c[k]; geo_add(E_POINT, c[k], c[k], z0, 0, 0, 0); done++; } }
        }
        free(have); undo_ents_added(from); break; }
    case G_PT_SMALL:
        mod_begin();
        for (int i = 0; i < n; i++) { Ent e = F.ents[l[i]]; if (e.type != E_ARC || e.r > gp.maxr + 1e-8) continue;
            if (!gp.partial && !(fabs(fabs(e.sw) - 2 * M_PI) < 1e-8)) continue;           /* partial arcs only when asked for */
            geo_add(E_POINT, e.c, e.c, z0, 0, 0, 0); done++; if (gp.del) mod_del(l[i]); }
        mod_end(); break;
    case G_JOIN: {
        /* JoinEptrs (join_selected_entities.c:300-440): every pair of the same type; A's ends against B's ends (the first closer than tol, in the order A.a B.a, A.a B.b, A.b B.a, A.b B.b); a line: check_colinear(A's far end, the common end, B's far end) (surfpick/core_dist.c), then FUN_180052cf0; an arc: FUN_1800532a0 (centre and radius within tol, same view), then FUN_1800529e0. A is rewritten, B deleted */
        mod_begin();
        double jt = mc_systol.tol; int changed = 1, j3d = 0;
        while (changed) {
            changed = 0;
            for (int a = 0; a < n && !changed; a++) for (int b = a + 1; b < n && !changed; b++) {
                int ia = l[a], ib = l[b]; if (!F.ents[ia].type || !F.ents[ib].type || F.ents[ia].type != F.ents[ib].type) continue;
                Ent A = F.ents[ia], B = F.ents[ib]; if (A.type != E_LINE && A.type != E_ARC) continue;
                V3 Aa = A.a, Ab = A.b, Ba = B.a, Bb = B.b; if (A.type == E_ARC) { GE ga = mc_ge(&A), gb = mc_ge(&B); P2 p[2] = { ge_at(&ga, 0), ge_at(&ga, 1) }, q[2] = { ge_at(&gb, 0), ge_at(&gb, 1) };
                    Aa = (V3){ p[0].x, p[0].y, A.c.z }; Ab = (V3){ p[1].x, p[1].y, A.c.z }; Ba = (V3){ q[0].x, q[0].y, B.c.z }; Bb = (V3){ q[1].x, q[1].y, B.c.z }; }
                if (!(jt <= d3v(Ab, Aa))) continue;                /* :303-311 a closed or empty A takes no partner */
                int ea = 0, eb = 0;
                if (d3v(Ba, Aa) < jt) { ea = 1; eb = 1; } else if (d3v(Bb, Aa) < jt) { ea = 1; eb = 2; } else if (d3v(Ba, Ab) < jt) { ea = 2; eb = 1; } else if (d3v(Bb, Ab) < jt) { ea = 2; eb = 2; }
                if (!ea) continue;
                GE res; int ok = 0;
                if (A.type == E_LINE) {
                    V3 P1 = ea == 1 ? Ab : Aa, P2_ = ea == 1 ? Aa : Ab, P3 = eb == 1 ? Bb : Ba;
                    if (!mc_colinear(P1, P2_, P3)) continue;
                    /* FUN_180052cf0: of the six pairs (A.a B.a, A.a B.b, A.b B.a, A.b B.b, A.a A.b, B.a B.b) A's two ends within tol of each line (a pair under tol counts 0), the longest pair (the first of equals) rewrites A */
                    V3 ps[6][2] = { { Aa, Ba }, { Aa, Bb }, { Ab, Ba }, { Ab, Bb }, { Aa, Ab }, { Ba, Bb } }; int best = -1; double bl = 0;
                    for (int i = 0; i < 6; i++) { double L = d3v(ps[i][1], ps[i][0]), d1 = 0, d2 = 0; if (L > jt) { d1 = mc_dist_pt_line(Aa, ps[i][0], ps[i][1]); d2 = mc_dist_pt_line(Ab, ps[i][0], ps[i][1]); }
                        if (jt < d1 || jt < d2) { best = -1; break; }
                        if (best < 0 || bl < L) { bl = L; best = i; } }
                    if (best < 0) continue;
                    if (fabs(ps[best][0].z - ps[best][1].z) > jt) { j3d = 1; continue; }   /* ours: the store is planar (GE) */
                    res = ge_line(xy(ps[best][0]), xy(ps[best][1]), ps[best][0].z); ok = 1;
                } else {
                    double cd = sqrt((A.c.x - B.c.x) * (A.c.x - B.c.x) + (A.c.y - B.c.y) * (A.c.y - B.c.y) + (A.c.z - B.c.z) * (A.c.z - B.c.z));
                    if (!(cd < jt && fabs(A.r - B.r) < jt && A.vw == B.vw)) continue;   /* FUN_1800532a0 */
                    /* FUN_1800529e0 (:1677): both made positive; gap[s] = the end of s - the start of the other, wrapped to [-pi, pi]; |gap[1]| <= |gap[0]| -> B then A, else A then B; the sweep <= 1e-6: + 2pi, > 2pi + 1e-6: - 2pi */
                    double sa[2] = { A.a0, B.a0 }, sw[2] = { A.sw, B.sw }, gap[2];
                    for (int i = 0; i < 2; i++) if (sw[i] < 0) { sa[i] = mc_restrict(0, 2 * M_PI, sa[i] + sw[i]); sw[i] = -sw[i]; }
                    for (int i = 0; i < 2; i++) { double d = sa[i] + sw[i] - sa[!i]; while (M_PI < d) d -= 2 * M_PI; if (d < -M_PI) d += 2 * M_PI; gap[i] = d; }
                    double st, en; if (fabs(gap[1]) <= fabs(gap[0])) { st = sa[1]; en = sa[0] + sw[0]; } else { st = sa[0]; en = sa[1] + sw[1]; }
                    double sn = en - st; if (sn <= 1e-6) sn += 2 * M_PI; if (6.283186307179586 < sn) sn -= 2 * M_PI;
                    memset(&res, 0, sizeof res); res.t = GE_ARC; res.c = xy(A.c); res.r = A.r; res.a0 = st; res.sw = sn; res.z = A.c.z; ok = 1;
                }
                if (ok) { int k = F.nents; replace_ent(ia, &res, 1); mod_del(ib); l[a] = k; changed = 1; done++; }   /* FUN_180052cf0 / FUN_1800529e0: A rewritten in place (A keeps its slot), B deleted */
            }
        }
        mod_end(); if (j3d) geo_msg("Join: a non-planar line is NOT PORTED"); break; }
    case G_BREAKN:                                       /* the live pieces (brkmany_run); this Apply commits the last ones first (code 1) */
        geo_live_commit();
        free(brkm_l); brkm_l = l; brkm_n = n; l = NULL;
        brkmany_run(); sel_clear(); geo_applied = 1; cam_msg[0] = 0; return;
    case G_BREAKX: case G_BREAKPTS: case G_BRKCIRC: case G_PROJECT: {
        int *with = l, nw = n, *all = NULL;
        if (geo_cmd == G_BREAKPTS) { nw = sel_list(&all, 1); with = all; }   /* points anywhere break the selected entities */
        mod_begin();
        int n0 = n;
        double **px_t = NULL; int *px_n = NULL;
        if (geo_cmd == G_BREAKX) {           /* break_at_inters: every intersection recorded first (FUN_1800492d0), then
                                                all curves broken (FUN_180049680) - breaking as we go hides the originals */
            px_t = malloc(sizeof(double *) * n0); px_n = calloc(n0, sizeof(int));
            for (int i = 0; i < n0; i++) { int e = l[i]; if (!F.ents[e].type || F.ents[e].type == E_POINT) continue;
                px_t[i] = malloc(sizeof(double) * 256); px_n[i] = cuts_on(e, with, nw, px_t[i], 256); }
        }
        for (int i = 0; i < n0; i++) {
            int e = l[i]; if (!F.ents[e].type || F.ents[e].type == E_POINT) continue;
            GE g = ent_ge(e); double t[256]; int nt = 0;
            if (geo_cmd == G_BREAKX) { nt = px_n[i]; if (nt) memcpy(t, px_t[i], nt * sizeof(double)); }
            else if (geo_cmd == G_BREAKPTS) { const Ent *E_ = &F.ents[e]; double tl_ = mc_systol.tol;   /* FUN_18004f9f0 param_7 1 (BreakAtPoints.c:237-404): no break within tol of an end; FUN_18004fec0 / FUN_18004f180: on the curve within tol, t inside [tol, 1 - tol] */
                for (int j = 0; j < nw; j++) if (F.ents[with[j]].type == E_POINT && nt < 256) { V3 P_ = F.ents[with[j]].a; if (d3v(P_, E_->a) < tl_ || d3v(P_, E_->b) < tl_ || !ge_on(&g, xy(P_), tl_)) continue;
                    double q_ = ge_param(&g, xy(P_)), z_ = E_->type == E_LINE ? E_->a.z + (E_->b.z - E_->a.z) * q_ : E_->c.z; if (fabs(P_.z - z_) > tl_ || q_ < tl_ || 1 - tl_ < q_) continue; t[nt++] = q_; }
                qsort(t, nt, sizeof(double), dcmp_ui); }
            else if (geo_cmd == G_BRKCIRC) { if (F.ents[e].type != E_ARC || !(fabs(fabs(F.ents[e].sw) - 2 * M_PI) < 1e-5)) continue;   /* break_circles.c:82 */ int pcs = gp.n < 2 ? 2 : gp.n; for (int k = 0; k < pcs; k++) t[nt++] = (double)k / pcs; }
            else {                                       /* project to the Z depth: copy or move */
                g.z = gp.z;
                add_ge_lc(&g, F.ents[e].level, F.ents[e].color); if (gp.move) mod_del(e); done++; continue;
            }
            if (nt && split_at_keep(e, t, nt, geo_cmd == G_BRKCIRC ? 0 : -1)) done++;   /* break_circles: the circle becomes the first piece */
        }
        if (geo_cmd == G_PROJECT) for (int i = 0; i < n0; i++) { int e = l[i]; if (F.ents[e].type == E_POINT) { GE g = ent_ge(e); g.z = gp.z; add_ge_lc(&g, F.ents[e].level, F.ents[e].color); if (gp.move) mod_del(e); done++; } }
        if (px_t) { for (int i = 0; i < n0; i++) free(px_t[i]); free(px_t); free(px_n); }
        free(all); mod_end(); break; }
    case G_CLOSEARC:                                     /* close_arc.c:61 select_ents mask 4, :70-89 last first: sweep < 2pi -> +2pi (<= 0: -2pi), start kept, replace_ent */
        mod_begin();
        for (int i = n - 1; i >= 0; i--) { Ent M = F.ents[l[i]]; if (M.type != E_ARC || !(M.sw < 2 * M_PI)) continue;
            M.sw = M.sw <= 0 ? -2 * M_PI : 2 * M_PI; GE q = mc_ge(&M); replace_ent(l[i], &q, 1); done++; }
        mod_end(); break;
    case G_TRIMMANY: geo_phase = 1; free(l); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()[0] ? geo_prompt() : "Trim Many"); overlay_dirty = 1; return;
    }
    free(l);
    if (geo_cmd != G_TRIMMANY) sel_clear();
    geo_applied = 1;
    scene_reupload(); overlay_dirty = 1;
    cam_msg[0] = 0;
}

/* ------------------------------------------------------------------------------------------------ panel model */
/* (viewer/geopanel_ui.inc: sources of the panels, defaults and keys)
 *  - the panels are mccad.ui.dll / mccadutil.ui.dll WPF views (IWireframe*ManagerView BAML); their outline (tabs,
 *    expanders, controls, bindings, button set) is ../../formats/ui_wireframe_panels.txt (tools/baml_panel.py). Every
 *    panel there has a FunctionPanel with OK, OK-New (= Apply), Cancel and Help buttons and one "Basic" tab
 *    (Bounding Box also "Advanced").
 *  - default values: the native managers (mccad.dll / mccadutil.dll ObservableModelImpl constructors) register each
 *    EvaluatorBox under a persisted key with a default (IsMetric() ? metric : inch); decompiles in
 *    ../../decomp/mccad_panel_keys.c and ../../decomp/mccadutil_panel_keys.c (tools/ghidra_scripts/StringRefsDecomp.java).
 *    Bool / radio defaults: FUN_1800bd490(..., 0 / 1) in the same constructors, and ../../formats/wireframe.md (native
 *    seeds, BoltCircleParams, polygonOp_params_tag ...).
 *  - commands whose options are MFC dialogs (Bolt Circle 10266, Polygon 6426 / 6427, Rectangular Shapes 6412 / 6414,
 *    Ellipse 6428 / 6429, Helix 6432, Spiral 6431 in mccadres; Offset 6453, Offset Contour 6460, Project 6462 in
 *    mccadutilres) take their titles and button texts from those templates; their value fields are bitmap buttons
 *    whose tooltips are NOT DECODED, so the field labels are the decoded parameter names (formats/wireframe.md).
 * Deviations: the keyed values are kept per unit system in <config dir>/panel_keys.tsv (ours: Mastercam keeps only an
 * autocomplete history per key, REG_KEY\Gui\Panel AutoComplete History, ../../formats/wireframe.md "Panel key storage");
 * the panel title is the command name (the FunctionPanel caption source is NOT DECODED); the EvaluatorBox lock is
 * drawn as a check box; options this program does not implement are greyed. */
#define MI(mm, in) (metric ? (mm) : (in))
#define TOL2 (2 * mc_systol.tol)                      /* WireframeLineEndpoints-Length default = 2 * tol (config 1000) */

/* persisted keys: name -> the panel value it holds */
static const struct { int cmd; const char *key; size_t off; } PKEYS[] = {
    { G_CCONST, "WireframeCurveConstantParameter-ChordHeightTolerance", offsetof(GeoParams, v_tol) },
    { G_CEDGE1, "WireframeCurveOnOneEdge-BreakAngle", offsetof(GeoParams, v_ang) },
    { G_CEDGEA, "WireframeCurveOnAllEdges-BreakAngle", offsetof(GeoParams, v_ang) }, { G_CEDGEA, "WireframeCurveOnAllEdges-OpenEdgesTolerance", offsetof(GeoParams, v_tol) },
    { G_CDYN, "WireframeCurveDynamic-ChordHeightTolerance", offsetof(GeoParams, v_tol) },
    { G_CFLOW, "WireframeCurveFlowline-ChordHeightTolerance", offsetof(GeoParams, v_tol) }, { G_CFLOW, "WireframeCurveFlowline-ChordHeight", offsetof(GeoParams, v_h) }, { G_CFLOW, "WireframeCurveFlowline-Distance", offsetof(GeoParams, v_dist) },
    { G_LNORM, "WireframeLineNormal-Length", offsetof(GeoParams, v_len) },
    { G_PT_DYN, "WireframePointDynamic-AlongDistance", offsetof(GeoParams, v_dist) },
    { G_PT_SEG, "WireframePointSegment-Distance", offsetof(GeoParams, v_dist) },
    { G_PT_SMALL, "WireframePointSmallArcs-MaximumRadius", offsetof(GeoParams, maxr) },
    { G_LINE, "WireframeLineEndpoints-Length", offsetof(GeoParams, v_len) },
    { G_LINE, "WireframeLineEndpoints-Angle", offsetof(GeoParams, v_ang) },
    { G_LPAR, "WireframeLineParallel-OffsetDistance", offsetof(GeoParams, v_dist) },
    { G_LPERP, "WireframeLinePerpendicular-Length", offsetof(GeoParams, v_len) },
    { G_LBIS, "WireframeLineBisect-Length", offsetof(GeoParams, v_len) },
    { G_LTAN, "WireframeLineTangentThroughPoint-Length", offsetof(GeoParams, v_len) },
    { G_CIRCLE, "WireframeCircleCenterpoint-Radius", offsetof(GeoParams, v_r) },
    { G_ATAN, "WireframeArcTangent-Radius", offsetof(GeoParams, v_r) },
    { G_CEDGE, "WireframeCircleEdgePoint-Radius", offsetof(GeoParams, v_r) },
    { G_AEND, "WireframeArcEndpoints-Radius", offsetof(GeoParams, v_r) },
    { G_APOLAR, "WireframeArcPolar-Radius", offsetof(GeoParams, r) },
    { G_APOLAR, "WireframeArcPolar-StartAngle", offsetof(GeoParams, sa) },
    { G_APOLAR, "WireframeArcPolar-EndAngle", offsetof(GeoParams, ea) },
    { G_APOLEND, "WireframeArcPolarEndpoints-Radius", offsetof(GeoParams, r) },
    { G_APOLEND, "WireframeArcPolarEndpoints-StartAngle", offsetof(GeoParams, sa) },
    { G_APOLEND, "WireframeArcPolarEndpoints-EndAngle", offsetof(GeoParams, ea) },
    { G_RECT, "WireframeRectangle-Width", offsetof(GeoParams, v_w) },
    { G_RECT, "WireframeRectangle-Height", offsetof(GeoParams, v_h) },
    { G_FILLET, "WireframeFilletEntities-Radius", offsetof(GeoParams, r) },
    { G_FILLET, "WireframeFilletEntities-ClearanceDistance", offsetof(GeoParams, v_dist) },
    { G_FILLETC, "WireframeFilletChains-Radius", offsetof(GeoParams, r) },
    { G_FILLETC, "WireframeFilletChains-ClearanceDistance", offsetof(GeoParams, v_dist) },
    { G_CHAMF, "WireframeChamferEntities-Distance1", offsetof(GeoParams, dist) },
    { G_CHAMF, "WireframeChamferEntities-Distance2", offsetof(GeoParams, d2) },
    { G_CHAMF, "WireframeChamferEntities-Angle", offsetof(GeoParams, ang) },
    { G_CHAMF, "WireframeChamferEntities-Width", offsetof(GeoParams, chw) },
    { G_CHAMFC, "WireframeChamferChains-Distance", offsetof(GeoParams, v_dist) },
    { G_CHAMFC, "WireframeChamferChains-Width", offsetof(GeoParams, v_w) },
    { G_BREAKN, "WireframeBreakManyPieces-Tolerance", offsetof(GeoParams, v_tol) },
    { G_BREAKN, "WireframeBreakManyPieces-ExactDistance", offsetof(GeoParams, v_dist) },
    { G_BREAKN, "WireframeBreakManyPieces-RoundedDistance", offsetof(GeoParams, v_step) },
    { G_TRIM, "WireframeTrimBreakExtend-ExtensionLength", offsetof(GeoParams, len) },
    /* BoundingBoxManager ctor FUN_180157b00 / FUN_180157cf0 (decomp/mccad_bbox_manager_ctor.c); the read-only Volume's key
       BoundingBox-Volume (FUN_180157c00) is not kept (no typed value) */
    { G_BBOX, "BoundingBox-SizeX", offsetof(GeoParams, bb_sx) },
    { G_BBOX, "BoundingBox-SizeY", offsetof(GeoParams, bb_sy) },
    { G_BBOX, "BoundingBox-SizeZ", offsetof(GeoParams, bb_sz) },
    { G_BBOX, "BoundingBox-Radius", offsetof(GeoParams, bb_r) },
    { G_BBOX, "BoundingBox-Height", offsetof(GeoParams, bb_h) },
};
#define NPKEYS (int)(sizeof PKEYS / sizeof PKEYS[0])
static double pk_store[NPKEYS][2]; static unsigned char pk_have[NPKEYS][2]; static int pk_loaded;
static void pk_file(char *p, size_t n) { char dir[1100]; config_dir_make(dir, sizeof dir); path_join(p, n, dir, "panel_keys.tsv"); }
static void pk_read_all(void) {
    if (pk_loaded) return;
    pk_loaded = 1;
    if (getenv("MCAMUI_SCRIPT")) return;                 /* scripted test runs use the defaults */
    char p[1200]; pk_file(p, sizeof p); FILE *f = fopen(p, "r"); if (!f) return;
    char k[128], u[8]; double v;
    while (fscanf(f, "%127s %7s %lf", k, u, &v) == 3)
        for (int i = 0; i < NPKEYS; i++) if (!strcmp(PKEYS[i].key, k)) { int m = !strcmp(u, "mm"); pk_store[i][m] = v; pk_have[i][m] = 1; }
    fclose(f);
}
static void pk_write_all(void) {
    if (getenv("MCAMUI_SCRIPT")) return;
    char p[1200]; pk_file(p, sizeof p); FILE *f = fopen(p, "w"); if (!f) return;
    for (int i = 0; i < NPKEYS; i++) for (int m = 0; m < 2; m++) if (pk_have[i][m]) fprintf(f, "%s %s %.17g\n", PKEYS[i].key, m ? "mm" : "in", pk_store[i][m]);
    fclose(f);
}
/* after a panel frame: keyed values that changed are stored */
void pk_note(int c) {
    int m = mcam_is_metric(&F), dirty = 0;
    for (int i = 0; i < NPKEYS; i++) if (PKEYS[i].cmd == c) {
        double v = *(double *)((char *)&gps[c] + PKEYS[i].off);
        if (!pk_have[i][m] || pk_store[i][m] != v) { pk_store[i][m] = v; pk_have[i][m] = 1; dirty = 1; }
    }
    if (dirty) pk_write_all();
}

/* first use of a command in these units: its panel model's defaults */
static void geo_defaults(int c, int metric) {
    GeoParams *g = &gps[c]; memset(g, 0, sizeof *g);
    switch (c) {
    case G_POINT: break;                                 /* Mode Point (point style option 0, FUN_1800e5540) */
    case G_LNORM: g->v_len = MI(25, 1); g->pts = 0; g->ctr = 0; g->dir = 0; break;   /* FUN_18035ea60: Length 1.0 / 25.0 (metric); Contact point, End point off; Direction Normal (+0x70 = 0) */
    case G_PT_DYN: g->v_dist = 0; break;                 /* AlongDistance 0 */
    case G_PT_SEG: g->v_dist = 0; g->v_n = 0; break;     /* Distance 0 (key), Number FUN_1800e8dc0(0, 1) */
    case G_PT_SMALL: g->maxr = 0.1; g->partial = 0; g->del = 0; break;   /* MaximumRadius 0.1 both units; bools 0 */
    case G_BOLT:                                         /* BoltCircleParams FUN_18005f660 */
        g->bolt_r = MI(100, 5); g->full = 1; g->n = 8; g->sa = 0; g->v_ang = 45; g->pts = 0; g->circles = 1; g->ctr = 0; g->refcirc = 0;
        g->hole_dia = 2 * MI(5.0, 0.25); break;
    case G_LINE: g->v_len = TOL2; g->v_ang = 0; g->entmode = 0; g->multi = 0; g->tangent = 1; break;   /* FUN_1800bfd20: Tangent bool 1 */
    case G_LPAR: g->v_dist = 0; g->tangent = 0; g->dir = 0; break;
    case G_LPERP: g->v_len = MI(25, 1); g->tangent = 0; g->dir = 0; break;
    case G_LBIS: g->v_len = MI(25, 1); g->single = 1; break;      /* AutoSelectSolution 1 (native +0x00 = 1) */
    case G_LTAN: g->v_len = 0.1; break;                  /* key default 0.1, minimum 1e-4 */
    case G_CIRCLE: g->v_r = 0; g->tangent = 0; break;
    case G_ATAN: g->v_r = MI(25, 1); g->mode = 0; break;  /* Radius 1 / 25 (Diameter 2 / 50) */
    case G_CEDGE: g->v_r = 0; g->cmode = 0; break;
    case G_AEND: g->v_r = 0; g->tangent = 0; break;
    case G_ARC3: g->tangent = 0; break;
    case G_APOLAR: g->r = 0.1; g->sa = 0; g->ea = 0; g->lk_r = g->lk_sa = g->lk_ea = 0; g->tangent = 0; g->inverted = 0; break;   /* Radius 0.1 both units */
    case G_APOLEND: g->r = 0; g->sa = 0; g->ea = 0; g->start_pt = 1; break;   /* IsPickingStart 1 (seed 0x100) */
    case G_RECT: g->v_w = 0; g->v_h = 0; g->anchor_ctr = 0; g->surf = 0; break;
    case G_RSHAPE: g->shape = 0; g->rot = 0; g->corner_r = 0; g->anchor_ctr = 0; g->anchor = 0; g->v_w = g->v_h = 0; break;   /* DAT_18064c670: all zero */
    case G_POLY: g->v_n = 6; g->v_r = 0; g->flat = 1; g->rot = 0; g->corner_r = 0; break;   /* polygonOp_params_tag DAT_18064c3c0 */
    case G_ELLIPSE: g->ea_r = 0; g->eb_r = 0; g->sa = 0; g->ea = 360; g->rot = 0; g->ell_out = 0; g->ell_tol = mc_systol.min_arc_length; break;    /* DAT_18064c0b0; +0x20 the tolerance is set from min_arc_length on every launch (FUN_18007b380 caller, wireframe.md Ellipse table) */
    case G_HELIX: g->bolt_r = MI(20, 1); g->revs = 1; g->pitch = MI(2, 0.1); g->taper = 0; g->sa = 0; g->cw = 0; g->height = g->pitch * g->revs; break;   /* HELIX_PARAMS RegisterVariable */
    case G_SPIRAL: g->r = MI(20, 1); g->revs = 1; g->pitch = g->pitch2 = MI(2, 0.1); g->zpitch = g->zpitch2 = 0; g->cw = 0; g->height = 0; break;  /* SPIRAL_PARAMS */
    case G_BBOX: break;                                  /* the settings struct comes from its static at every start (bb_begin, cui_bbox.c) */
    case G_FILLET: case G_FILLETC: g->r = MI(5.0, 0.25); g->v_dist = 0; g->style = 0; g->dir = 0; g->trim = 1; break;
    case G_CHAMF: g->dist = MI(5.0, 0.25); g->d2 = MI(5.0, 0.25); g->ang = 45; g->chw = MI(5.0, 0.25); g->mode = 0; g->trim = 1; break;
    case G_CHAMFC: g->v_dist = MI(5.0, 0.25); g->v_w = MI(5.0, 0.25); g->mode = 0; g->trim = 1; break;
    case G_TRIM: g->trimmode = 1; g->mode = TR_AUTO; g->len = MI(1.0, 0.1); break;   /* Trim 1, mode 0; ExtensionLength 0.1 in / 1.0 mm */
    case G_TRIMMANY: g->trimmode = 1; g->dir = 0; break;
    case G_BREAKN: g->lines = 0; g->method = 0; g->v_n = 5; g->v_tol = 0.002; g->v_dist = MI(0.5, 0.1); g->v_step = MI(0.5, 0.1); g->action = 0; break;
    case G_BRKCIRC: g->n = 2; break;                     /* get_b_int default 2 */
    case G_OFFSET: g->dist = MI(25, 1); g->copies = 1; g->move = 0; break;   /* DAT_180242810: Copy, 1, RegisterVariable(25, 1) */
    case G_OFFSETC: g->dist = MI(10, 0.5); g->round = 2; g->copies = 1; g->dir = 0; g->move = 0; g->ofc_ila = 1; g->len = MI(0.02, 0.001); g->d2 = MI(0.1, 0.005); g->z = 0; g->ang = 0; g->v_n = 0; break;   /* DAT_1802429e0: distance (do_xform_offset_ctour.c:81), corners +0x1c = 2 (:76), copies 1 (:73), direction 0 (:75) */
    case G_XROLL: xroll_reset(); break;                  /* do_xform_roll's reset block (cui_xroll.c) */
    case G_SREV: g->sa = 0; g->ea = 360; g->dir = 0; break;
    case G_SEXTR:                                        /* CExtrudeSurfDlg's first values (ctor 0x180127bf0 from DAT_18064e530.., decomp/surfextr/statics.txt): Height 1.0
                                                           RegisterVariable(20, 1), Scale 1.0, Rotate 0, Offset 0, Taper 0, the locks 0, the Flip states 0; the axis
                                                           combo +0x15ac = 2 (Z), the radio +0x1550 0 */
        g->v_h = MI(20.0, 1.0); g->v_w = 1.0; g->v_r = 0; g->v_dist = 0; g->v_ang = 0; g->lk_h = g->lk_w = g->lk_r = g->lk_dist = g->lk_ang = 0;
        g->dir = 0; g->inverted = 0; g->style = 0; g->cmode = 0; g->entmode = 2; break;
    case G_SDRAFT:                                       /* the dialog's first values (ctor FUN_180123fe0 from DAT_18064e260.., decomp/surfextr/statics.txt): Length
                                                           mode (e260 = 1 -> +0x1550 0), Length 1.0 and Running Length 1.0 RegisterVariable(20, 1), Angle 0, the locks
                                                           0, Split Draft 0 (e292), the Flip states 0 (e294 / e298) */
        g->method = 0; g->v_len = g->v_dist = MI(20.0, 1.0); g->v_ang = 0; g->lk_len = g->lk_dist = g->lk_ang = 0; g->multi = 0; g->dir = 0; g->inverted = 0;
        g->len = g->v_len; g->dist = g->v_dist; g->ang = g->v_ang; break;
    case G_SBLOCK:                                       /* DAT_180648370 + FUN_1800014a0: sizes 0, anchor 1, axis Z, Surfaces (create_sz_block) */
        g->v_len = g->v_w = g->v_h = 0; g->lk_len = g->lk_w = g->lk_h = 0; g->rot = 0; g->mode = 1; g->cmode = 2; g->dir = 0; g->surf = 1; break;
    case G_SLOFT: g->mode = 1; break;                    /* IsRuled default 1 (FUN_1802b40d0) */
    case G_CEDGE1: g->v_ang = 1.0; g->lines = 0; break;   /* FUN_18021f380: BreakAngle 1.0 (FunctionPanelEditableDouble, the "30.0" of FUN_1802a2940 local_2e80 is overwritten by the panel's value), FitArcsAndLines off (FUN_1800bd490) */
    case G_CEDGEA: { double t0 = mc_systol_units(mcam_is_metric(&F))->tol, st = 0.005; char v0[32]; const char *cv = mcam_config_value(mcam_is_metric(&F), 8003, v0, sizeof v0); if (cv) st = atof(cv);   /* FUN_180224880: BreakAngle 30.0, Only Open Edges off; OpenEdgesTolerance: the larger of tol and sld_stitch_tol (mcgeomsld .data 0.005, config 8003 = FUN_1801b7xxx case 0x1f43, decomp/mccore_wfhighlight.c) */
        g->v_ang = 30.0; g->v_tol = st > t0 ? st : t0; g->lines = 0; g->method = 0; break; }
    case G_CDYN: g->v_tol = mc_systol.surf_step_chord_dev; break;   /* FUN_18023d6c0: the panel value, below tol: surf_step_chord_dev; the value is not used by the spline fit (FUN_180282390) */
    case G_CFLOW: g->v_tol = mc_systol.surf_step_chord_dev; g->v_h = mc_systol.surf_step_chord_dev; g->v_dist = 0.1; g->v_n = 10; g->method = 0; g->dir = 0; break;   /* FUN_180237560: ChordHeightTolerance, ChordHeight = surf_step_chord_dev; Distance 0.1; Number 10 (minimum 2); Method +0x60 -> 0 Chord height; IsDirection1 true (U) */
    case G_STRIMS: g->method = 0; g->lines = 0; g->style = 0; g->round = 1; g->mode = 0; g->del = 0; break;   /* Trim Both, Extend curves to edge 0, Split model 0,
                                                           Keep multiple regions 1, Use current attributes 0, Keep original surface 0 (.data 0x18064f498) */
    case G_SEXT: g->mode = 0; g->v_dist = metric ? 20.0 : 1.0; g->del = 0; g->lines = 0; break;   /* FUN_1802dad50: the statics DAT_18064f460 (.data: By distance,
                                                            Distance RegisterVariable(&DAT_18064f468, 20.0, 1.0), Keep original off, Non-linear) */
    case G_CCONST: g->v_tol = mc_systol.surf_step_chord_dev; g->method = 0; break;   /* FUN_180231780: ChordHeightTolerance = surf_step_chord_dev (registry key WireframeCurveConstantParameter-ChordHeightTolerance), Direction (+0xc -> 0) U */
    case G_SSOLID: g->style = 1; g->del = 0; g->round = 0; break;   /* SurfaceFromSolidsManager (FUN_1802f7870): +0x50 1, +0x60 0, +0x70 0 */
    case G_SNSET: g->pts = 0; g->dir = 0; break;          /* SurfaceNormalSetManager FUN_1800ac120: ShowNormals false; the radio Cplane +Z (cui_srfedit.c) */
    case G_SUNTRIM: g->del = 0; break;                   /* SurfaceUntrimManager FUN_1802fc2f0: KeepOriginalSurface false */
    case G_SOFFSET: g->mode = 0; g->v_dist = metric ? 2.0 : 0.1; g->lk_dist = 0; break;   /* SurfaceOffsetManager FUN_1802a9af0: IsMove false, OffsetDistance 0.1 / 2.0 */
    case G_SSPH:                                         /* DAT_180648030 + FUN_180001360: radius 0, sweep 0 .. 360, axis Z */
        g->v_r = 0; g->lk_r = 0; g->sa = 0; g->ea = 360; g->cmode = 2; g->surf = 1; break;
    case G_SCONE:                                        /* DAT_180649140 + FUN_180001890: sizes 0, Top radius mode (+0x2a 1), sweep 0 .. 360,
                                                           axis Z (+0x90 = tol) */
        g->v_r = g->v_h = g->v_w = 0; g->ang = 0; g->lk_r = g->lk_h = 0; g->method = 1; g->sa = 0; g->ea = 360; g->cmode = 2; g->dir = 0; g->surf = 1; break;
    case G_STOR:                                         /* DAT_180648d70 + FUN_180001740: radii 0, sweep 0 .. 360, axis Z */
        g->v_r = g->v_w = 0; g->lk_r = g->lk_w = 0; g->sa = 0; g->ea = 360; g->cmode = 2; g->surf = 1; break;
    case G_SCYL:                                         /* DAT_180648980 + FUN_1800015f0: sizes 0, sweep 0 .. 360, axis Z */
        g->v_r = g->v_h = 0; g->lk_r = g->lk_h = 0; g->sa = 0; g->ea = 360; g->cmode = 2; g->dir = 0; g->surf = 1; break;               /* FUN_1802cc6c0: 0 / 2 pi (0x401921fb54442d18), Direction 1 */
    case G_PROJECT: g->z = constr_depth; g->move = 0; break;   /* do_xform_project: depth 1e38 at first use = the construction depth (mccad_util_project.c:88, :504) */
    }
    pk_read_all();
    for (int i = 0; i < NPKEYS; i++) if (PKEYS[i].cmd == c && pk_have[i][metric]) *(double *)((char *)g + PKEYS[i].off) = pk_store[i][metric];
}

/* the values the geometry reads, from the panel model (each frame and on start) */
void geo_panel_sync(void) {
    GeoParams *g = &gp;
    switch (geo_cmd) {
    case G_PT_SEG: g->n = g->v_n; g->dist = g->v_dist; break;
    case G_LINE: g->horiz = g->entmode == 1; g->vert = g->entmode == 2; g->ang = g->v_ang; g->cont = g->multi;
        /* FUN_18018f480: length and angle locked -> p1 + L (cos a, sin a); Horizontal / Vertical + length locked -> +-L */
        g->len = g->lk_len && (g->entmode || g->lk_ang) ? g->v_len : 0; break;
    case G_LPAR: g->dist = g->lk_dist ? g->v_dist : 0; break;
    case G_LPERP: case G_LTAN: g->len = g->lk_len ? g->v_len : 0; break;
    case G_LBIS: g->len = g->v_len; break;
    case G_CIRCLE: geo_dia = g->lk_r ? 2 * g->v_r : 0; break;
    case G_ATAN: g->r = g->v_r; break;
    case G_CEDGE: g->mode = g->cmode >= 2 ? 1 : 0; break;
    case G_AEND: g->r = g->lk_r ? g->v_r : 0; break;
    case G_RECT: case G_RSHAPE: geo_rw = g->lk_w ? g->v_w : 0; geo_rh = g->lk_h ? g->v_h : 0; break;
    case G_POLY: geo_poly_n = g->v_n; geo_poly_r = g->lk_r ? g->v_r : 0; break;
    case G_CHAMFC: g->dist = g->mode ? g->v_w : g->v_dist; break;
    case G_BREAKN: g->n = g->v_n; break;
    case G_SEXTR:                                        /* OnAxisXYZChange (0x180128f80): a new combo index sets the axis X (1, 0, 0), Y (0, -1, 0), Z (0, 0, -1),
                                                           the radio 0, then 0x69 */
        if (g->entmode != ex_combo) { static const double av[3][3] = { { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, -1 } };
            if (g->entmode >= 0 && g->entmode <= 2) { memcpy(ex_ax, av[g->entmode], sizeof ex_ax); g->cmode = 0; } ex_combo = g->entmode; }
        break;
    case G_SDRAFT: srf_draft_sync(&g->v_len, &g->v_dist, g->v_ang, &g->len, &g->dist, &g->ang); break;   /* the Running Length handlers (cui_surf.c) */
    }
}

/* ---- the panel contents (formats/ui_wireframe_panels.txt order and texts), walked through the UI's builder: each
   control is reported in order with the variable it edits and whether it is enabled; exp() returns 1 when the
   expander is open (its contents follow, then end()) */
#define fp_exp(ctx, h) ops->exp(u, h)
#define fp_label(ctx, t) ops->label(u, t)
#define fp_radio(ctx, ...) ops->radio(u, __VA_ARGS__)
#define fp_check(ctx, ...) ops->check(u, __VA_ARGS__)
#define fp_toggle(ctx, ...) ops->toggle(u, __VA_ARGS__)
#define fp_num(ctx, ...) ops->num(u, __VA_ARGS__)
#define fp_int(ctx, ...) ops->inum(u, __VA_ARGS__)
#define FP_END ops->end(u)
/* the panel contents of each command (formats/ui_wireframe_panels.txt order and texts) */
void geo_panel_body(const CuiPanelOps *ops, void *u) {
    GeoParams *g = &gp;
    if (xf_cmd(geo_cmd)) { xf_panel_body(ops, u); return; }   /* the Transform dialogs (cui_xform.c) */
    if (sl_cmd(geo_cmd)) { sl_panel_body(ops, u); return; }
    if (si_cmd(geo_cmd)) { si_panel_body(ops, u); return; }
    if (pln_cmd(geo_cmd)) { pln_panel_body(ops, u); return; }
    if (sp_cmd(geo_cmd)) { sp_panel_body(ops, u); return; }
    if (dr_cmd(geo_cmd)) { dr_panel_body(ops, u); return; }   /* DraftingSmartDimensionsManager (cui_draft.c) */
    if (do_cmd(geo_cmd)) { do_panel_body(ops, u); return; }   /* the same panel for the ordinates (cui_draft_ord.c) */
    if (geo_cmd == G_LETTERS) { wm_let_panel(ops, u); return; }   /* CLetDlg 434 (cui_wfmisc.c) */
    if (geo_cmd == G_RGROOVE) { wm_rg_panel(ops, u); return; }   /* CDINGrv (MCCADRES 2516; cui_wfmisc.c) */
    switch (geo_cmd) {
    case G_SPLCURV: sp_curv_panel(ops, u); break;
    case G_POINT:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Mode:"); fp_radio(ctx, "_Point", &g->cmode, 0, 1);
            fp_radio(ctx, "_Thread Point", &g->cmode, 1, 0); fp_radio(ctx, "_Cut Point", &g->cmode, 2, 0); fp_toggle(ctx, "_Reselect", 1, -1); FP_END; }
        break;
    case G_PT_DYN:
        if (fp_exp(ctx, "Position")) { fp_toggle(ctx, "_Reselect", 1, -1); FP_END; }
        /* AlongDistance (manager [8]): IsEnabled set at each entity pick to !surface, off at the start and after Apply (FUN_18034c290
           sketcher_point_2.c:1120, :293, disasm 18034b999); limits 0 .. the length for a closed curve / spline, else +-1e38 */
        if (fp_exp(ctx, "Distance")) { int c = geo_nent > 0 && geo_ent[0] >= 0 && geo_ent[0] < F.nents && F.ents[geo_ent[0]].type == E_ARC && fabs(F.ents[geo_ent[0]].sw) >= 2 * M_PI - 1e-9;
            fp_num(ctx, "_Along:", "along", &g->v_dist, &g->lk_dist, geo_nent > 0, c ? 0 : -1e38, c ? 2 * M_PI * F.ents[geo_ent[0]].r : 1e38); FP_END; }
        break;
    case G_PT_SEG:
        if (fp_exp(ctx, "_Distance")) { fp_num(ctx, NULL, "dist", &g->v_dist, &g->lk_dist, 1, 0, 1e6); FP_END; }
        if (fp_exp(ctx, "_Number")) { fp_int(ctx, NULL, "num", &g->v_n, &g->lk_n, 1, 0, 30000); FP_END; }
        break;
    case G_PT_SMALL:
        if (fp_exp(ctx, "_Maximum Radius")) { fp_num(ctx, NULL, "maxr", &g->maxr, &g->lk_r, 1, 0, 1e6); FP_END; }
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "Include _partial arcs", &g->partial, 1); fp_check(ctx, "_Delete arcs", &g->del, 1); FP_END; }
        break;
    case G_LINE:
        if (fp_exp(ctx, "Entity")) {
            fp_label(ctx, "Mode:");
            fp_radio(ctx, "_Freeform", &g->entmode, 0, 1); fp_check(ctx, "_Tangent", &g->tangent, 0);
            fp_radio(ctx, "_Horizontal", &g->entmode, 1, 1); fp_radio(ctx, "_Vertical", &g->entmode, 2, 1);
            fp_label(ctx, "Type:"); fp_radio(ctx, "T_wo endpoints", &g->multi, 0, 1); fp_radio(ctx, "_Multi-line", &g->multi, 1, 1); FP_END; }
        /* the ToggleButtons _1 / _2: IsChecked = CurrentlyEditing, IsEnabled = EnableEndpoint1 (NumberSelected > 0) / EnableEndpoint2
           (EntityType != Multi-line and NumberSelected > 1) (mccad.interop.dll WireframeLineEndpointsManagerViewModel) */
        if (fp_exp(ctx, "Endpoints")) {
            if (ops->tbtn) ops->tbtn(u, "_1|_2", 2, &g->edit_pt, 2, (geo_line_nsel > 0) | (!g->multi && geo_line_nsel > 1) << 1);
            else fp_toggle(ctx, "", 2, -1);
            FP_END; }
        if (fp_exp(ctx, "Dimensions")) { fp_num(ctx, "_Length:", "len", &g->v_len, &g->lk_len, 1, 0, 1e6);
            fp_num(ctx, "_Angle:", "ang", &g->v_ang, &g->lk_ang, g->entmode == 0, -360, 360); FP_END; }
        /* AxisOffset.IsEnabled: EnableApply and Horizontal / Vertical (FUN_1800c4940: EntityMode 1 / 2) */
        if (fp_exp(ctx, "Axi_s Offset")) { fp_num(ctx, NULL, "axoff", &g->v_step, NULL, geo_line_enapply && (g->entmode == 1 || g->entmode == 2), -1e6, 1e6); FP_END; }
        break;
    case G_LPAR: case G_LPERP:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Mode:"); fp_radio(ctx, "_Point", &g->tangent, 0, 1); fp_toggle(ctx, "_Reselect", 1, -1);
            fp_radio(ctx, "_Tangent", &g->tangent, 1, 0); FP_END; }
        if (geo_cmd == G_LPAR) { if (fp_exp(ctx, "Offset _Distance")) { fp_num(ctx, NULL, "dist", &g->v_dist, &g->lk_dist, 1, 0, 1e6); FP_END; } }
        else if (fp_exp(ctx, "_Length")) { fp_num(ctx, NULL, "len", &g->v_len, &g->lk_len, 1, 0, 1e6); FP_END; }
        if (fp_exp(ctx, "Direction")) { fp_radio(ctx, "_Selected side", &g->dir, 0, 1); fp_radio(ctx, "_Opposite side", &g->dir, 1, 1);
            fp_radio(ctx, "_Both sides", &g->dir, 2, 1); FP_END; }
        break;
    case G_LBIS:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Solution:"); fp_radio(ctx, "_Single", &g->single, 1, 1); fp_radio(ctx, "_Multiple", &g->single, 0, 1); FP_END; }
        if (fp_exp(ctx, "_Length")) { fp_num(ctx, NULL, "len", &g->v_len, &g->lk_len, 1, 0, 1e6); FP_END; }
        break;
    case G_LTAN:
        if (fp_exp(ctx, "Tangent To")) { fp_label(ctx, "_Reference entity:"); fp_toggle(ctx, "...", 1, -1); FP_END; }
        if (fp_exp(ctx, "Endpoints")) { fp_toggle(ctx, "", 2, -1); FP_END; }
        if (fp_exp(ctx, "_Length")) { fp_num(ctx, NULL, "len", &g->v_len, &g->lk_len, 1, 1e-4, 1e6); FP_END; }
        break;
    case G_CIRCLE: case G_AEND: case G_APOLAR:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Mode:"); fp_radio(ctx, "_Manual", &g->tangent, 0, 1); fp_radio(ctx, "_Tangent", &g->tangent, 1, 0); FP_END; }
        if (geo_cmd == G_AEND) { if (fp_exp(ctx, "Position By")) { fp_toggle(ctx, "", 2, -1); FP_END; } }
        else if (fp_exp(ctx, "Center Point")) { fp_toggle(ctx, "_Reselect", 1, -1); FP_END; }
        if (fp_exp(ctx, "Size")) {
            double *rv = geo_cmd == G_APOLAR ? &g->r : &g->v_r; int *lk = &g->lk_r;
            fp_num(ctx, "Radi_us:", "rad", rv, lk, 1, 0, 1e6);
            double d = 2 * *rv;                          /* after the Radius field: a typed radius must not be undone by the stale diameter (phase 3 fix) */
            fp_num(ctx, "_Diameter:", "dia", &d, lk, 1, 0, 2e6); if (fabs(d - 2 * *rv) > 1e-12) *rv = d / 2;   /* Diameter = 2 x Radius */
            FP_END; }
        if (geo_cmd == G_APOLAR) {
            if (fp_exp(ctx, "Angle")) { fp_num(ctx, "Start _angle:", "sa", &g->sa, &g->lk_sa, 1, -360, 360); fp_num(ctx, "_End angle:", "ea", &g->ea, &g->lk_ea, 1, -360, 360); FP_END; }
            if (fp_exp(ctx, "Direction")) { fp_radio(ctx, "De_fined Arc", &g->inverted, 0, 0); fp_radio(ctx, "In_verted Arc", &g->inverted, 1, 0); FP_END; }
        }
        break;
    case G_ARC3:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Mode:"); fp_radio(ctx, "_Point", &g->tangent, 0, 1); fp_radio(ctx, "_Tangent", &g->tangent, 1, 0); FP_END; }
        if (fp_exp(ctx, "Point/Tangent Entity")) { fp_toggle(ctx, "", 3, -1); FP_END; }
        break;
    case G_ATAN: {
        /* TangencyModes = WireframeArcTangentManagerEnum (modes 0..6 in this order); Arc dynamic (3) is not ported */
        static const char *mn[7] = { "Arc one entity", "Arc one point", "Arc centerline", "Arc dynamic", "Arc three entities", "Circle three entities", "Arc two entities" };
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "_Mode:");
            int ix = g->mode >= 0 && g->mode < 7 ? g->mode : 0; ops->combo(u, mn, 7, &ix, 3); if (ix != 3) g->mode = ix; FP_END; }
        if (fp_exp(ctx, "Size")) { fp_num(ctx, "Radi_us:", "rad", &g->v_r, &g->lk_r, 1, 1e-6, 1e6); double d = 2 * g->v_r;   /* d after the Radius field (phase 3 fix) */
            fp_num(ctx, "_Diameter:", "dia", &d, &g->lk_r, 1, 2e-6, 2e6); if (fabs(d - 2 * g->v_r) > 1e-12) g->v_r = d / 2; FP_END; }
        break; }
    case G_CEDGE:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Mode:"); fp_radio(ctx, "2 _Points", &g->cmode, 0, 1); fp_radio(ctx, "2 Points _tangent", &g->cmode, 1, 0);
            fp_radio(ctx, "3 P_oints", &g->cmode, 2, 1); fp_radio(ctx, "3 Points t_angent", &g->cmode, 3, 0); FP_END; }
        if (fp_exp(ctx, "Edge Points")) { fp_toggle(ctx, "", 3, -1); FP_END; }
        if (fp_exp(ctx, "Size")) { double d = 2 * g->v_r; fp_num(ctx, "Radi_us:", "rad", &g->v_r, &g->lk_r, 0, 0, 1e6); fp_num(ctx, "_Diameter:", "dia", &d, &g->lk_r, 0, 0, 2e6); FP_END; }
        break;
    case G_APOLEND:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Mode:"); fp_radio(ctx, "_Start point", &g->start_pt, 1, 1); fp_radio(ctx, "E_nd point", &g->start_pt, 0, 1); FP_END; }
        if (fp_exp(ctx, "Start/End Point")) { fp_toggle(ctx, "_Reselect", 1, -1); FP_END; }
        if (fp_exp(ctx, "Size")) { fp_num(ctx, "Radi_us:", "rad", &g->r, NULL, 1, 0, 1e6); double d = 2 * g->r; fp_num(ctx, "_Diameter:", "dia", &d, NULL, 1, 0, 2e6);
            if (fabs(d - 2 * g->r) > 1e-12) g->r = d / 2; FP_END; }
        if (fp_exp(ctx, "Angle")) { fp_num(ctx, "Start _angle:", "sa", &g->sa, NULL, 1, -360, 360); fp_num(ctx, "_End angle:", "ea", &g->ea, NULL, 1, -360, 360); FP_END; }
        break;
    case G_RECT:
        if (fp_exp(ctx, "Points")) { fp_toggle(ctx, "", 2, -1); FP_END; }
        if (fp_exp(ctx, "Dimensions")) { fp_num(ctx, "_Width:", "w", &g->v_w, &g->lk_w, 1, 0, 1e6); fp_num(ctx, "Heigh_t:", "h", &g->v_h, &g->lk_h, 1, 0, 1e6); FP_END; }
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "_Anchor to center", &g->anchor_ctr, 1); fp_check(ctx, "Create _surface", &g->surf, 0); FP_END; }
        break;
    case G_BBOX: bb_panel_body(ops, u); break;            /* boundingboxmanagerview.baml (cui_bbox.c) */
    case G_FILLET: case G_FILLETC:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Style:"); fp_radio(ctx, "N_ormal", &g->style, 0, 1); fp_radio(ctx, "_Inverse", &g->style, 1, 1);
            fp_radio(ctx, "_Circle", &g->style, 2, 1); fp_radio(ctx, "Cle_arance", &g->style, 3, geo_cmd == G_FILLET); fp_num(ctx, NULL, "clr", &g->v_dist, NULL, geo_cmd == G_FILLET && g->style == 3, 0, 1e6);
            if (geo_cmd == G_FILLET) fp_radio(ctx, "_Relief", &g->style, 4, 0); FP_END; }
        if (geo_cmd == G_FILLETC && fp_exp(ctx, "Chains")) { fp_toggle(ctx, "_Reselect", 1, 4); FP_END; }   /* Button ReselectAllChainsCommand (no IsEnabled): manager vtable +0x90 FUN_18016aa80 posts 0x8038 code 4 */
        if (fp_exp(ctx, "Radi_us")) { fp_num(ctx, NULL, "rad", &g->r, NULL, 1, 1e-6, 1e6); FP_END; }
        if (geo_cmd == G_FILLETC && fp_exp(ctx, "Direction")) { fp_radio(ctx, "All cor_ners", &g->dir, 0, 1); fp_radio(ctx, "+ _Sweeps", &g->dir, 1, 1);
            fp_radio(ctx, "- S_weeps", &g->dir, 2, 1); FP_END; }
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "_Trim entities", &g->trim, 1); FP_END; }
        break;
    case G_CHAMF:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Style:"); fp_radio(ctx, "1 _Distance", &g->mode, 0, 1); fp_radio(ctx, "2 Di_stances", &g->mode, 1, 1);
            fp_radio(ctx, "Distance and an_gle", &g->mode, 2, 1); fp_radio(ctx, "Widt_h", &g->mode, 3, 1); FP_END; }
        if (fp_exp(ctx, "Distance _1")) { fp_num(ctx, NULL, "d1", &g->dist, NULL, g->mode != 3, 1e-6, 1e6); FP_END; }   /* EnableDistance1 */
        if (fp_exp(ctx, "Distance _2")) { fp_num(ctx, NULL, "d2", &g->d2, NULL, g->mode == 1, 1e-6, 1e6); FP_END; }
        if (fp_exp(ctx, "_Angle")) { fp_num(ctx, NULL, "ang", &g->ang, NULL, g->mode == 2, 0, 360); FP_END; }
        if (fp_exp(ctx, "_Width")) { fp_num(ctx, NULL, "w", &g->chw, NULL, g->mode == 3, 1e-6, 1e6); FP_END; }
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "_Trim entities", &g->trim, 1); FP_END; }
        break;
    case G_CHAMFC:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Style:"); fp_radio(ctx, "_Distance", &g->mode, 0, 1); fp_num(ctx, NULL, "d", &g->v_dist, NULL, g->mode == 0, 1e-6, 1e6);
            fp_radio(ctx, "_Width", &g->mode, 1, 1); fp_num(ctx, NULL, "w", &g->v_w, NULL, g->mode == 1, 1e-6, 1e6); FP_END; }
        if (fp_exp(ctx, "Chains")) { fp_toggle(ctx, "_Reselect", 1, 4); FP_END; }   /* ReselectAllChainsCommand: manager vtable +0x80 FUN_180176cc0 posts code 4 */
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "_Trim entities", &g->trim, 0); FP_END; }
        break;
    case G_BREAKN:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Style:"); fp_radio(ctx, "_Create curves", &g->lines, 0, 1); fp_radio(ctx, "Create _lines", &g->lines, 1, 1); FP_END; }
        if (fp_exp(ctx, "Segments")) {
            fp_radio(ctx, "_Number:", &g->method, 0, 1); fp_int(ctx, NULL, "num", &g->v_n, NULL, g->method == 0, 1, 32767);
            fp_radio(ctx, "_Tolerance:", &g->method, 1, 1); fp_num(ctx, NULL, "tol", &g->v_tol, NULL, g->method == 1, 0, 1e6);
            fp_radio(ctx, "_Exact distance:", &g->method, 2, 1); fp_num(ctx, NULL, "exact", &g->v_dist, NULL, g->method == 2, 0, 1e6);
            fp_radio(ctx, "_Rounded distance:", &g->method, 3, 1); fp_num(ctx, NULL, "round", &g->v_step, NULL, g->method == 3, 0, 1e6); FP_END; }
        if (fp_exp(ctx, "Original Curves/Lines")) { fp_radio(ctx, "_Delete", &g->action, 0, 1); fp_radio(ctx, "_Keep", &g->action, 1, 1);
            fp_radio(ctx, "_Blank", &g->action, 2, 1); FP_END; }
        break;
    case G_TRIM: {
        static const char *tn[7] = { "_Auto", "Trim _1 entity", "Trim _2 entities", "Trim _3 entities", "_Divide/delete", "T_rim to point", "_Extend" };
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Mode:"); fp_radio(ctx, "_Trim", &g->trimmode, 1, 1); fp_radio(ctx, "_Break", &g->trimmode, 0, 1);
            fp_label(ctx, "Type:"); int old = g->mode;
            for (int i = 0; i < 7; i++) fp_radio(ctx, tn[i], &g->mode, i, 1);
            if (old != g->mode) geo_reset_picks(); FP_END; }
        if (fp_exp(ctx, "Extension _length")) { fp_num(ctx, NULL, "ext", &g->len, NULL, 1, -1e6, 1e6);   /* no IsEnabled binding */ FP_END; }
        break; }
    case G_TRIMMANY:
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Mode:"); fp_radio(ctx, "_Trim", &g->trimmode, 1, 1); fp_radio(ctx, "_Break", &g->trimmode, 0, 1); FP_END; }
        if (fp_exp(ctx, "Direction")) { fp_radio(ctx, "_Selected side", &g->dir, 0, 1); fp_radio(ctx, "_Opposite side", &g->dir, 1, 1); FP_END; }
        break;
    /* ---- MFC dialog commands: field names from the decoded parameter structs (formats/wireframe.md) */
    case G_BOLT:
        if (fp_exp(ctx, "Radius of Guide Circle")) { fp_num(ctx, NULL, "br", &g->bolt_r, NULL, 1, 1e-6, 1e6); FP_END; }
        if (fp_exp(ctx, "Pattern")) { fp_check(ctx, "Full Circle", &g->full, 1);
            fp_int(ctx, "Number of stops around bolt circle", "n", &g->n, NULL, 1, 1, 30000);
            fp_num(ctx, "Angle swept between stops", "step", &g->v_ang, NULL, !g->full, -360, 360);
            fp_num(ctx, "Start angle", "sa", &g->sa, NULL, 1, -360, 360); FP_END; }
        if (fp_exp(ctx, "Create Entities")) { fp_check(ctx, "Points", &g->pts, 1); fp_check(ctx, "Arcs", &g->circles, 1);
            double hr = g->hole_dia / 2; fp_num(ctx, NULL, "hr", &hr, NULL, g->circles, 0, 1e6); g->hole_dia = 2 * hr;   /* holeRadius +0x50 */
            fp_check(ctx, "Center Point", &g->ctr, 1); fp_check(ctx, "Reference Circle", &g->refcirc, 1); FP_END; }
        break;
    case G_POLY:
        if (fp_exp(ctx, "Polygon")) { fp_int(ctx, "sides", "n", &g->v_n, NULL, 1, 3, 360); fp_num(ctx, "radius", "r", &g->v_r, &g->lk_r, 1, 0, 1e6);
            fp_radio(ctx, "Corner", &g->flat, 0, 1); fp_radio(ctx, "Flat", &g->flat, 1, 1);
            fp_num(ctx, "corner fillet radius", "cr", &g->corner_r, NULL, 1, 0, 1e6); fp_num(ctx, "rotation angle", "rot", &g->rot, NULL, 1, -360, 360); FP_END; }
        break;
    case G_RSHAPE: {
        static const char *sh[4] = { "Rectangle shape", "Obround shape", "D shape", "Double D shape" };
        if (fp_exp(ctx, "Rectangular Shapes Options")) {
            fp_num(ctx, "Width", "w", &g->v_w, &g->lk_w, 1, 0, 1e6); fp_num(ctx, "Height", "h", &g->v_h, &g->lk_h, 1, 0, 1e6);
            fp_num(ctx, "corner fillet radius", "cr", &g->corner_r, NULL, g->shape == 0, 0, 1e6); fp_num(ctx, "rotation angle", "rot", &g->rot, NULL, 1, -360, 360); FP_END; }
        if (fp_exp(ctx, "Shape")) { for (int i = 0; i < 4; i++) fp_radio(ctx, sh[i], &g->shape, i, 1); FP_END; }
        if (fp_exp(ctx, "Anchor")) { { int v_ = g->anchor >= 1 ? g->anchor : g->anchor_ctr ? 5 : 1, v0_ = v_; for (int r_ = 0; r_ < 3; r_++) for (int c_ = 0; c_ < 3; c_++) { char t_[16]; snprintf(t_, sizeof t_, "#cell:%d,%d", r_, c_); fp_radio(ctx, t_, &v_, c_ * 3 + (2 - r_) + 1, 1); } if (v_ != v0_) { g->anchor = v_; g->anchor_ctr = 0; } } FP_END; }   /* res 6414 Anchor group: nine untexted radios (controls 1359 1335 1308 / 1363 1346 1321 / 1366 1353 1328, x 20 / 66 / 112, y 232 / 254 / 278); value = index in that tab order + 1 (create_rectangle.c:612) */   /* anchor positions 1..9 (FUN_180270fc0); the panel's picture buttons have no text */
        break; }
    case G_ELLIPSE:
        if (fp_exp(ctx, "Ellipse")) { fp_num(ctx, "A radius", "a", &g->ea_r, &g->lk_ra, 1, 0, 1e6); fp_num(ctx, "B radius", "b", &g->eb_r, &g->lk_rb, 1, 0, 1e6);   /* CSkRLCombo 0x2fd / 0x303 with lock buttons 0x2fa / 0x300 (CEllipseDlg FUN_180079970) */
            fp_num(ctx, "start angle", "sa", &g->sa, NULL, 1, 0, 360); fp_num(ctx, "end angle", "ea", &g->ea, NULL, 1, 0, 360);
            fp_num(ctx, "rotation angle", "rot", &g->rot, NULL, 1, -360, 360);
            { static const char *ot[3] = { "NURBS", "Arc segments", "Line segments" }; int ix = g->ell_out; ops->combo(u, ot, 3, &ix, -1); g->ell_out = ix; }   /* combo 25477 (mccadres strings 5860-5862); Arc segments: break_spline_arcs (viewer/mcbiarc.c) */
            fp_num(ctx, "Tolerance", "tol", &g->ell_tol, NULL, g->ell_out != 0, mc_systol.chain_tol, 10000); FP_END; }
        break;
    case G_HELIX:
        if (fp_exp(ctx, "Helix")) { fp_num(ctx, "Radius", "r", &g->bolt_r, NULL, 1, 1e-6, 1e6); fp_num(ctx, "Revolutions", "rev", &g->revs, NULL, 1, 1e-6, 1000);
            /* res 0x1920: Height 750 edits pitch = height / revs (FUN_180085a00), the shown Height = pitch x revs (FUN_1800855f0) */
            { double h0 = g->pitch * g->revs; g->height = h0; fp_num(ctx, "Height", "h", &g->height, NULL, 1, -1e6, 1e6); if (g->height != h0 && g->revs > 0) g->pitch = g->height / g->revs; }
            fp_num(ctx, "Pitch", "p", &g->pitch, NULL, 1, -1e6, 1e6); fp_num(ctx, "Taper Angle", "tap", &g->taper, NULL, 1, -90, 90); fp_num(ctx, "initial angle", "sa", &g->sa, NULL, 1, -360, 360);
            fp_radio(ctx, "CW", &g->cw, 1, 1); fp_radio(ctx, "CCW", &g->cw, 0, 1); FP_END; }
        break;
    case G_SPIRAL:
        if (fp_exp(ctx, "Spiral")) { fp_num(ctx, "Radius", "r", &g->r, NULL, 1, 0, 1e6); fp_num(ctx, "Rev./ Height", "rev", &g->revs, NULL, 1, 1, 1000);
            /* res 0x191f: vertical (first group, FUN_180089040 / MaSpiral::EvaluateCrv's Z) Initial / Final Pitch; Height 750 = the sum of the vertical pitches (FUN_180087700), editing it sets both = Height / revs (FUN_180087b10); horizontal (second group, the radial growth) Initial / Final Pitch */
            { int nr = (int)g->revs; double sum = 0, v = g->zpitch, dv = nr > 1 ? (g->zpitch2 - g->zpitch) / (nr - 1) : 0; for (int i_ = 0; i_ < nr; i_++, v += dv) sum += v;
              g->height = sum; fp_num(ctx, "Height", "h", &g->height, NULL, 1, -1e6, 1e6); if (g->height != sum && nr >= 1) g->zpitch = g->zpitch2 = g->height / nr; }
            /* res 6431: the two groups are told apart by their pictures (statics 652 / 654: mccadres bitmaps 5548 side view X-Z, 5549 top view X-Y); each group's fields are "Initial Pitch" / "Final Pitch" */
            ops->label(u, "#img:mccadres_BMP_5548"); fp_num(ctx, "Initial Pitch", "zp", &g->zpitch, NULL, 1, -1e6, 1e6); fp_num(ctx, "Final Pitch", "zp2", &g->zpitch2, NULL, 1, -1e6, 1e6);
            ops->label(u, "#img:mccadres_BMP_5549"); fp_num(ctx, "Initial Pitch", "p", &g->pitch, NULL, 1, -1e6, 1e6); fp_num(ctx, "Final Pitch", "p2", &g->pitch2, NULL, 1, -1e6, 1e6);
            fp_radio(ctx, "CW", &g->cw, 1, 1); fp_radio(ctx, "CCW", &g->cw, 0, 1); FP_END; }
        break;
    case G_OFFSET:
        if (fp_exp(ctx, "Offset")) { fp_radio(ctx, "Move", &g->move, 1, 1); fp_radio(ctx, "Copy", &g->move, 0, 1);
            fp_int(ctx, "copies", "c", &g->copies, NULL, 1, 1, 30000); fp_num(ctx, "distance", "d", &g->dist, NULL, 1, 0, 1e6);
            for (int i_ = 0; i_ < 3; i_++) { char t_[40]; snprintf(t_, sizeof t_, "#img:mccoreres_BMP_%d", 0x463 + i_); fp_radio(ctx, t_, &g->dir, i_, 1); }   /* res 6453 button 1623, MCButton bitmaps 0x463 / 0x464 / 0x465 (do_xform_offset.c:409-412, mccoreres): DAT_180242820 0 / 1 / 2 */
            FP_END; }
        break;
    case G_SSPH: case G_SCONE: case G_STOR: {            /* mccadres 6421 Sphere / 6418 Cone / 6423 Torus (the expanded dialogs; readers
                                                           FUN_18000ad70 / FUN_180032710 / FUN_18002da30) */
        static const char *fl[3] = { "Flip direction (1)", "Flip direction (2)", "Flip direction (both)" };
        if (fp_exp(ctx, geo_cmd == G_SSPH ? "Sphere" : geo_cmd == G_SCONE ? "Cone" : "Torus")) {
            fp_radio(ctx, "_Solids", &g->surf, 0, 0); fp_radio(ctx, "S_urfaces", &g->surf, 1, 1);   /* Solids: NOT PORTED here (Surfaces tab) */
            fp_num(ctx, "_Radius", "r", &g->v_r, &g->lk_r, 1, 0, 1e6);                     /* 740 (+8, lock +0x10 / cone +0x28) */
            if (geo_cmd == G_STOR) fp_num(ctx, "_Minor radius", "mr", &g->v_w, &g->lk_w, 1, 0, 1e6);   /* 751 (+0x18, lock +0x20) */
            if (geo_cmd == G_SCONE) { fp_num(ctx, "H_eight", "h", &g->v_h, &g->lk_h, 1, -1e6, 1e6);   /* 750 (+0x10, lock +0x29) */
                fp_toggle(ctx, fl[g->dir >= 0 && g->dir <= 2 ? g->dir : 0], 1, 6); }      /* 575 */
            FP_END; }
        if (geo_cmd == G_SCONE && fp_exp(ctx, "Top")) {   /* radios 1533 / 1528 (+0x3358: FUN_180031da0 0 = taper angle, FUN_180031e10 1 =
                                                           top radius), combos +0x124a8 angle / +0x15040 radius, button 598 = the pick */
            fp_radio(ctx, "Taper angle", &g->method, 0, 1); fp_num(ctx, "angle", "ta", &g->ang, NULL, g->method == 0, 0, 89.8);
            fp_radio(ctx, "Top radius", &g->method, 1, 1); fp_num(ctx, "radius", "tr", &g->v_w, NULL, g->method == 1, 0, 1e6);
            fp_toggle(ctx, "Top radius (select)", 1, 7); FP_END; }
        if (fp_exp(ctx, "Sweep")) { fp_num(ctx, "Start angle", "sa", &g->sa, NULL, 1, 0, 360); fp_num(ctx, "End angle", "ea", &g->ea, NULL, 1, 0, 360); FP_END; }   /* 595 / 435 */
        if (fp_exp(ctx, "Axis")) { fp_radio(ctx, "X", &g->cmode, 0, 1); fp_radio(ctx, "Y", &g->cmode, 1, 1); fp_radio(ctx, "Z", &g->cmode, 2, 1);   /* 25074 .. 25076 */
            fp_label(ctx, "Line / Two points: NOT PORTED"); FP_END; }   /* 579 / 582 */
        break; }
    case G_SBLOCK: case G_SCYL: {                        /* mccadres 6419 Block / 6415-6416 Cylinder (the expanded dialog) */
        static const char *fl[3] = { "Flip direction (1)", "Flip direction (2)", "Flip direction (both)" };   /* FUN_18000efd0: bitmaps 0x463 / 0x464 / 0x465 */
        int one = 1;
        if (fp_exp(ctx, geo_cmd == G_SBLOCK ? "Block" : "Cylinder")) {
            fp_radio(ctx, "_Solids", &g->surf, 0, 0); fp_radio(ctx, "S_urfaces", &g->surf, 1, 1);   /* Solids: NOT PORTED here (Surfaces tab) */
            if (geo_cmd == G_SBLOCK) { fp_num(ctx, "_Length", "l", &g->v_len, &g->lk_len, 1, -1e6, 1e6); fp_num(ctx, "_Width", "w", &g->v_w, &g->lk_w, 1, -1e6, 1e6); }
            else fp_num(ctx, "_Radius", "r", &g->v_r, &g->lk_r, 1, 0, 1e6);
            fp_num(ctx, "H_eight", "h", &g->v_h, &g->lk_h, 1, -1e6, 1e6);
            fp_toggle(ctx, fl[g->dir >= 0 && g->dir <= 2 ? g->dir : 0], 1, 6);   /* button 575: code 6 here */
            (void)one; FP_END; }
        if (geo_cmd == G_SBLOCK) {
            if (fp_exp(ctx, "Rotation")) { fp_num(ctx, "angle", "rot", &g->rot, NULL, 1, -360, 360); FP_END; }   /* combo 411 (+0x38) */
            if (fp_exp(ctx, "Anchor")) { static const char *an[9] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };   /* 1308 .. 1366: +0x40 */
                for (int i = 0; i < 9; i++) fp_radio(ctx, an[i], &g->mode, i + 1, 1); FP_END; }
        } else if (fp_exp(ctx, "Sweep")) { fp_num(ctx, "Start angle", "sa", &g->sa, NULL, 1, 0, 360); fp_num(ctx, "End angle", "ea", &g->ea, NULL, 1, 0, 360); FP_END; }   /* 595 / 435 */
        if (fp_exp(ctx, "Axis")) { fp_radio(ctx, "X", &g->cmode, 0, 1); fp_radio(ctx, "Y", &g->cmode, 1, 1); fp_radio(ctx, "Z", &g->cmode, 2, 1);   /* 25074 .. 25076 */
            fp_label(ctx, "Line / Two points: NOT PORTED"); FP_END; }   /* 579 / 582 */
        break; }
    case G_SLOFT:                                        /* surfaceruledloftedmanagerview.baml (SurfaceRuledLoftedManager FUN_1802b40d0: IsRuled 1) */
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Type:"); fp_radio(ctx, "R_uled", &g->mode, 1, 1); fp_radio(ctx, "_Lofted", &g->mode, 0, 1); FP_END; }
        if (fp_exp(ctx, "Chains to Sweep")) { fp_toggle(ctx, "_Reselect", 1, 4); FP_END; }
        break;
    case G_LNORM:                                        /* wireframelinenormalmanagerview.baml (formats/ui_wireframe_panels.txt); WireframeLineNormalManager FUN_18035ea60 */
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Points:"); fp_check(ctx, "_Contact point", &g->pts, 1); fp_check(ctx, "_End point", &g->ctr, 1); FP_END; }
        if (fp_exp(ctx, "Position")) { fp_toggle(ctx, "_Reselect", wm_ln_enable_reselect(), 7); FP_END; }   /* EnableReselect = NumberSelected != 0 (the view model; set 1 at each line, 0 at the selection); code 7 */
        if (fp_exp(ctx, "_Length")) { fp_num(ctx, NULL, "length", &g->v_len, NULL, 1, -1e38, 1e38); FP_END; }   /* FUN_180359a20: limits -+1e38 */
        if (fp_exp(ctx, "Direction")) { fp_radio(ctx, "_Normal", &g->dir, 0, 1); fp_radio(ctx, "Re_verse Normal", &g->dir, 1, 1); fp_radio(ctx, "_Both", &g->dir, 2, 1); FP_END; }   /* OffsetDirection 0, 1, 2 */
        break;
    case G_CFLOW:                                        /* wireframecurveflowlinemanagerview.baml */
        if (fp_exp(ctx, "_Chord Height Tolerance")) { fp_num(ctx, "", "tol", &g->v_tol, NULL, 1, 0, 1e6); FP_END; }
        if (fp_exp(ctx, "Curve Quantity")) { fp_radio(ctx, "Chord heigh_t:", &g->method, 0, 1); fp_num(ctx, NULL, "ch", &g->v_h, NULL, g->method == 0, 0, 1e6);
            fp_radio(ctx, "_Distance:", &g->method, 1, 1); fp_num(ctx, NULL, "dist", &g->v_dist, NULL, g->method == 1, 0, 1e6);
            fp_radio(ctx, "_Number:", &g->method, 2, 1); fp_int(ctx, NULL, "num", &g->v_n, NULL, g->method == 2, 2, 32767); FP_END; }
        if (fp_exp(ctx, "Direction")) { fp_radio(ctx, "_U", &g->dir, 0, 1); fp_radio(ctx, "_V", &g->dir, 1, 1); FP_END; }
        break;
    case G_CEDGE1:                                       /* wireframecurveononeedgemanagerview.baml: Break Angle only for a trimmed surface (BreakAngle.IsEnabled, FUN_1802a2940 surf_type == 0xa); Fit arcs and lines */
        if (fp_exp(ctx, "_Break Angle")) { fp_num(ctx, "", "ang", &g->v_ang, NULL, cedge_trimmed(), 0, 180); FP_END; }   /* BreakAngle.IsEnabled: surf_type == 0xa (FUN_1802a2940) */
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "_Fit arcs and lines", &g->lines, 1); FP_END; }
        break;
    case G_CEDGEA:                                       /* wireframecurveonalledgesmanagerview.baml */
        if (fp_exp(ctx, "Entities")) { fp_toggle(ctx, "_Reselect", 1, 4); FP_END; }   /* ReselectAllCommand: code 4 */
        if (fp_exp(ctx, "_Only Open Edges")) { fp_check(ctx, "_Only Open Edges", &g->method, 1); fp_num(ctx, "_Tolerance:", "tol", &g->v_tol, NULL, g->method, 0, 1e6); FP_END; }   /* surface_open_edges (FUN_1802c4a70 param_4[0] = the check, +8 the tolerance, FUN_180224880) */
        if (fp_exp(ctx, "_Break Angle")) { fp_num(ctx, "", "ang", &g->v_ang, NULL, ce_any_trimmed(), 0, 180); FP_END; }   /* FUN_1802c4f10: enabled when a trimmed surface is selected [the binding of +0x70 to BreakAngle is inferred from One Edge] */
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "_Fit arcs and lines", &g->lines, 1); FP_END; }
        break;
    case G_CDYN:                                         /* WireframeCurveDynamicManager (FUN_18023d6c0): the chord height tolerance */
        if (fp_exp(ctx, "_Chord Height Tolerance")) { fp_num(ctx, "", "tol", &g->v_tol, NULL, 1, 0, 1e6); FP_END; }
        break;
    case G_STRIMS:                                       /* surfacetrimtosurfacesmanagerview.baml (TrimOption, ReselectSet1 / 2, the check boxes) */
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Trim:"); fp_radio(ctx, "_Both", &g->method, 0, 1); fp_radio(ctx, "_First set", &g->method, 1, 1); fp_radio(ctx, "_Second set", &g->method, 2, 1); FP_END; }
        if (fp_exp(ctx, "Surface Set 1")) { fp_toggle(ctx, "_Reselect", 0, 4); FP_END; }     /* FUN_18008fa90: NOT DECODED (greyed) */
        if (fp_exp(ctx, "Surface Set 2")) { fp_toggle(ctx, "Reselec_t", 0, 5); FP_END; }     /* FUN_18008f9e0: NOT DECODED (greyed) */
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "_Extend curves to edge", &g->lines, 1); fp_check(ctx, "Split _model", &g->style, 1);
            fp_check(ctx, "Kee_p multiple regions", &g->round, 1); fp_check(ctx, "_Use current attributes", &g->mode, 1); fp_check(ctx, "_Keep original surface", &g->del, 1); FP_END; }   /* the IsEnabled rules: NOT DECODED (all enabled) */
        break;
    case G_SEXT:                                         /* surfaceextendmanagerview.baml */
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Mode:");
            fp_radio(ctx, "By _distance", &g->mode, 0, 1); fp_num(ctx, NULL, "dist", &g->v_dist, NULL, g->mode == 0, 0, 1e38);   /* ExtendByDistance / Distance */
            fp_radio(ctx, "To _plane", &g->mode, 1, 1);                                   /* get_plane_info (cui_plsel.c) before the build */
            fp_label(ctx, "Style:"); fp_radio(ctx, "_Linear", &g->lines, 1, 1); fp_radio(ctx, "_Non-linear", &g->lines, 0, 1); FP_END; }   /* IsLinear */
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "_Keep original surface", &g->del, 1); FP_END; }   /* KeepOriginalSurface */
        break;
    case G_CCONST:                                       /* wireframecurveconstantparametermanagerview.baml */
        if (fp_exp(ctx, "_Chord Height Tolerance")) { fp_num(ctx, "", "tol", &g->v_tol, NULL, 1, 0, 1e6); FP_END; }
        if (fp_exp(ctx, "Direction")) { fp_radio(ctx, "_U", &g->method, 0, 1); fp_radio(ctx, "_V", &g->method, 1, 1); fp_radio(ctx, "_Both", &g->method, 2, 1); FP_END; }
        break;
    case G_SSOLID:                                       /* surfacefromsolidsmanagerview.baml (SurfaceFromSolidsManager FUN_1802f7870) */
        if (fp_exp(ctx, "Selection")) { fp_toggle(ctx, "_Reselect", 1, 4); FP_END; }   /* ReSelectAllCommand */
        if (fp_exp(ctx, "Settings")) {
            fp_check(ctx, "_Keep original solid", &g->del, 1);                       /* +0x60, default 0 */
            fp_label(ctx, "Attributes:"); fp_radio(ctx, "Use _current", &g->style, 1, 1); fp_radio(ctx, "Use _original", &g->style, 0, 1);   /* +0x50, default 1 */
            fp_check(ctx, "_Preprocess solid", &g->round, 0);                         /* +0x70: NOT PORTED (greyed) */
            FP_END; }
        break;
    case G_SNSET:                                        /* surfacenormalsetmanagerview.baml (SurfaceNormalSetApp FUN_180111490's codes) */
        if (fp_exp(ctx, "Surface")) { fp_toggle(ctx, "_Reselect", 1, 4); FP_END; }   /* ReselectAllCommand: code 4 */
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "Show _normals", &g->pts, 1); FP_END; }   /* ShowNormals: code 5 */
        if (fp_exp(ctx, "Direction")) { fp_radio(ctx, "Cplane +_Z", &g->dir, 0, 1); fp_radio(ctx, "_Cplane -Z", &g->dir, 1, 1); FP_END; }   /* IsDirection1: code 6 */
        break;
    case G_SOFFSET:                                      /* surfaceoffsetmanagerview.baml (SurfaceOffsetManager, codes of FUN_1802f1420) */
        if (fp_exp(ctx, "Entity")) { fp_label(ctx, "Mode:"); fp_radio(ctx, "_Copy", &g->mode, 0, 1); fp_radio(ctx, "_Move", &g->mode, 1, 1); FP_END; }   /* IsMove: code 12 */
        if (fp_exp(ctx, "Surfaces")) { fp_toggle(ctx, "_Reselect", 1, 4); FP_END; }   /* ReSelectAllCommand: code 4 */
        if (fp_exp(ctx, "_Offset Distance")) { fp_num(ctx, NULL, "dist", &g->v_dist, &g->lk_dist, 1, -1e38, 1e38); FP_END; }   /* OffsetDistance: code 11 (its validator's limits NOT DECODED) */
        if (fp_exp(ctx, "Computer Analysis")) { fp_label(ctx, "Max + offset:"); fp_label(ctx, "Max - offset:");   /* the texts (codes 14 / 13): empty - the analysis is NOT PORTED */
            fp_toggle(ctx, "Calc_ulate", 0, 9); FP_END; }   /* ScaleCalculateCommand: code 9 - MinMaxPrincipalNormalCurvatures NOT PORTED (greyed) */
        if (fp_exp(ctx, "Direction")) { fp_toggle(ctx, "_Single flip", 1, 7); fp_toggle(ctx, "Cycle / _Next", 1, 8); fp_toggle(ctx, "_Flip", srf_of_flip_enabled(), 10); FP_END; }   /* codes 7 / 8 / 10 (FlipEnabled) */
        break;
    case G_SUNTRIM:                                      /* surfaceuntrimmanagerview.baml */
        if (fp_exp(ctx, "Settings")) { fp_check(ctx, "_Keep original surface", &g->del, 1); FP_END; }   /* KeepOriginalSurface: code 4 */
        break;
    case G_SFLAT:                                        /* surfaceflatboundarymanagerview.baml */
        if (fp_exp(ctx, "Chains")) { fp_toggle(ctx, "_Reselect", 1, 4); fp_toggle(ctx, "_Add", 1, 8); FP_END; }   /* RechainAllCommand (code 4) / AddChainsCommand (5 there; 8 here) */
        break;
    case G_SEXTR: {                                      /* CExtrudeSurfDlg (mccadres dialog 1086 "Extruded Surface", DDX 0x180128520, message map GetThisMessageMap) */
        static const char *hf[3] = { "_Flip Height (1)", "_Flip Height (2)", "_Flip Height (both)" }, *of[2] = { "F_lip Offset (1)", "F_lip Offset (2)" }, *tf[2] = { "Flip Taper A_ngle (1)", "Flip Taper A_ngle (2)" };   /* 575 / 577 / 580: bitmaps 0x463.. */
        if (fp_exp(ctx, "Extruded Surface")) {
            fp_toggle(ctx, "_Select Curve", 1, 4);                                                     /* 990: 0x66 */
            fp_toggle(ctx, "Select _Base Point", 1, 5);                                                /* 544: 0x67, then a point (mccoreres 7442) */
            fp_num(ctx, "Hei_ght", "h", &g->v_h, &g->lk_h, 1, -1e6, 1e6);                             /* 744 lock / 750 */
            fp_toggle(ctx, hf[g->dir >= 0 && g->dir <= 2 ? g->dir : 0], 1, 6);                         /* 575: 0 -> 1 -> 2 (OnFilpHeightPb) */
            fp_num(ctx, "S_cale", "sc", &g->v_w, &g->lk_w, 1, -1e6, 1e6);                             /* 748 / 752 */
            fp_num(ctx, "_Rotate", "rot", &g->v_r, &g->lk_r, 1, -360, 360);                           /* 756 / 758 (spin -360 .. 360) */
            fp_num(ctx, "Offs_et", "off", &g->v_dist, &g->lk_dist, 1, -1e6, 1e6);                     /* 760 / 764 */
            fp_toggle(ctx, of[g->inverted ? 1 : 0], g->v_dist != 0.0, 7);                             /* 577: enabled when the offset is not 0 (OnKillfocusOffsetVal) */
            fp_num(ctx, "_Taper Angle", "tap", &g->v_ang, &g->lk_ang, 1, -360, 360);                  /* 767 / 770 */
            fp_toggle(ctx, tf[g->style ? 1 : 0], g->v_ang != 0.0, 8);                                 /* 580: enabled when the taper is not 0 */
            FP_END; }
        if (fp_exp(ctx, "Axis")) {                                                                     /* group 3668 */
            fp_radio(ctx, "X / Y / Z", &g->cmode, 0, 1);                                               /* 1492 (OnAxisXYZRb) */
            fp_radio(ctx, "X", &g->entmode, 0, 1); fp_radio(ctx, "Y", &g->entmode, 1, 1); fp_radio(ctx, "Z", &g->entmode, 2, 1);   /* combo 884 (DLGINIT items X, Y, Z) */
            fp_radio(ctx, "Axis L_ine (NOT PORTED)", &g->cmode, 1, 0);                                  /* 1295 / 583 OnAxisLnRb / OnAxisLnPb */
            fp_radio(ctx, "Axis _2 Points (NOT PORTED)", &g->cmode, 2, 0);                              /* 25069 / 586 OnAxis2ptRb / OnAxis2ptPb */
            FP_END; }
        break; }
    case G_SDRAFT: {                                     /* CDraftSurfDlg (mccadres dialog 1084 "Draft Surface", DDX FUN_180124570, message map DAT_18064e2a0) */
        static const char *lf[3] = { "Flip (1)", "Flip (2)", "Flip (both)" }, *af[2] = { "Flip (1)", "Flip (2)" };   /* bitmaps 0x463 / 0x464 / 0x465 (FUN_1801249d0 / FUN_1801248e0) */
        int len_on = g->method == 0;                     /* FUN_1801272a0: Length mode enables the length / running length controls, Plane mode the Plane button */
        if (fp_exp(ctx, "Draft Surface")) {
            fp_toggle(ctx, "_Select", 1, 4);             /* 961: 0x66 - the chain again */
            fp_radio(ctx, "_Length", &g->method, 0, 1); fp_radio(ctx, "Pl_ane", &g->method, 1, 1);   /* 1491 / 1294 (+0x1550) */
            fp_num(ctx, "Leng_th", "len", &g->v_len, &g->lk_len, len_on, -1e6, 1e6);              /* 755 lock / 759 */
            fp_toggle(ctx, lf[g->dir >= 0 && g->dir <= 2 ? g->dir : 0], len_on, 6);                /* 575: the length Flip state 0 -> 1 -> 2 */
            fp_num(ctx, "_Running Length", "run", &g->v_dist, &g->lk_dist, len_on, -1e6, 1e6);   /* 763 lock / 766 */
            fp_num(ctx, "An_gle", "ang", &g->v_ang, &g->lk_ang, 1, -360, 360);                   /* 769 lock / 772 (spin limits -360 .. 360) */
            fp_toggle(ctx, af[g->inverted ? 1 : 0], 1, 7);                                        /* 577: the angle Flip state 0 -> 1 */
            fp_toggle(ctx, "Pla_ne (NOT PORTED: get_plane_info)", 0, 9);                          /* 773: 0x68 */
            fp_check(ctx, "Split _Draft", &g->multi, len_on && g->dir == 2);                      /* 962 (+0x1630): enabled with the length Flip state 2 */
            FP_END; }
        break; }
    case G_SREV:                                         /* surfacerevolvedmanagerview.baml */
        if (fp_exp(ctx, "Chains")) { fp_toggle(ctx, "_Reselect", 1, 4); FP_END; }                 /* RechainAllCommand: code 4 (FUN_18008cc00) */
        if (fp_exp(ctx, "Axis of Rotation")) { fp_toggle(ctx, "Reselec_t", 1, 5); FP_END; }       /* SelectAxisCommand: code 5 (FUN_18008cb10) */
        if (fp_exp(ctx, "Angle")) { fp_num(ctx, "St_art angle:", "sa", &g->sa, NULL, 1, -1e6, 1e6); fp_num(ctx, "_End angle:", "ea", &g->ea, NULL, 1, -1e6, 1e6); FP_END; }
        if (fp_exp(ctx, "Direction")) { fp_radio(ctx, "Direction _1", &g->dir, 0, 1); fp_radio(ctx, "Direction _2", &g->dir, 1, 1); FP_END; }
        break;
    case G_XROLL: xroll_panel(ops, u); break;            /* CXfRollDlg (cui_xroll.c) */
    case G_OFFSETC:
        if (fp_exp(ctx, "Mode")) { fp_radio(ctx, "Move", &g->move, 1, 1); fp_radio(ctx, "Copy", &g->move, 0, 1); fp_radio(ctx, "Join", &g->move, 2, 0); fp_radio(ctx, "Slot", &g->move, 3, 0); FP_END; }   /* res 6460 radios 100-103, DAT_1802429e0 */
        if (fp_exp(ctx, "Offset Contour")) { fp_num(ctx, "distance", "d", &g->dist, NULL, 1, 0, 1e6); fp_int(ctx, "copies", "c", &g->copies, NULL, 1, 1, 30000);
            for (int i_ = 0; i_ < 3; i_++) { char t_[40]; snprintf(t_, sizeof t_, "#img:mccoreres_BMP_%d", 0x463 + i_); fp_radio(ctx, t_, &g->dir, i_, 1); }   /* res 6460 Direction button 1623, bitmaps 0x463-0x465; DAT_1802429f8: 0 'L', 1 'R', 2 both (:960 doubles) */
            fp_radio(ctx, "None", &g->round, 1, 1); fp_radio(ctx, "Sharp", &g->round, 2, 1); fp_radio(ctx, "All", &g->round, 3, 1); FP_END; }   /* Corners group (res 6460): DAT_1802429fc, set_min_fil_angle(corners - 1) mccore_cc.c:229 */
        if (fp_exp(ctx, "Offset Contour 3D")) { fp_num(ctx, "depth", "z", &g->z, NULL, 0, -1e6, 1e6); fp_radio(ctx, "Absolute", &g->v_n, 0, 0); fp_radio(ctx, "Incremental", &g->v_n, 1, 0); fp_num(ctx, "angle", "ang", &g->ang, NULL, 0, -90, 90);   /* FUN_18001c4c0 only (a 3D chain) */
            fp_check(ctx, "Infinite Look Ahead", &g->ofc_ila, 1);   /* check 1668, DAT_180242a08 = ccomp_pock param_6 (offsetc_ctx.txt) */
            fp_num(ctx, "Tolerance", "tol", &g->len, NULL, 0, 0, 1e6); fp_num(ctx, "Max depth", "mdp", &g->d2, NULL, 0, 0, 1e6);   /* controls 2500 / 2505: chain_to_pock_bound's linearization (splines, refused) and the 3D variance */
            ops->label(u, "Join, Slot and 3D chains (depth, angle, Tolerance, Max depth): NOT PORTED (FUN_1800204c0, FUN_18001c4c0)"); FP_END; }
        break;
    case G_PROJECT:
        if (fp_exp(ctx, "Project")) { fp_radio(ctx, "Move", &g->move, 1, 1); fp_radio(ctx, "Copy", &g->move, 0, 1);
            fp_num(ctx, "depth", "z", &g->z, NULL, 1, -1e6, 1e6); FP_END; }
        break;
    case G_BRKCIRC:
        if (fp_exp(ctx, "Break Circles")) { fp_int(ctx, "pieces", "n", &g->n, NULL, 1, 2, 32767); FP_END; }   /* get_b_int 2..32767 */
        break;
    default: break;
    }
}
#undef fp_exp
#undef fp_label
#undef fp_radio
#undef fp_check
#undef fp_toggle
#undef fp_num
#undef fp_int
#undef FP_END

/* the panel's buttons (mcamui.c geo_panel_content): code 0 OK, 1 OK-New (Apply), 2 Cancel - the panel codes the
   commands' loops read (formats/wireframe.md "Live results") */
/* the chain commands' panel codes (sketcher_fillet_2.c:228-299, sketcher_chamfer_2.c:230-305; Offset Chains
   do_xform_offset_ctour.c:92-251). No input while chain_it runs (EnableWindow(panel, 0)). */
static void geo_chain_button(int code) {
    if (geo_chaining) return;
    if (geo_cmd == G_SEXTR && code >= 5 && code <= 8) {   /* 544 (0x67: undo_go_back, the point), OnFilpHeightPb 0 -> 1 -> 2, OnFilpOffsetPb / OnFilpAnglePb 0 -> 1 (0x68) */
        if (code == 5) { if (!geo_nchains || !srf_extr_built()) return; if (geo_live && geo_live_valid()) geo_live_drop(); ex_pick_base = 1; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return; }
        if (code == 6) gp.dir = gp.dir >= 2 ? 0 : gp.dir + 1; else if (code == 7) gp.inverted = !gp.inverted; else gp.style = !gp.style;
        geo_live_update(); overlay_dirty = 1; return; }
    if (geo_cmd == G_SDRAFT && (code == 6 || code == 7)) {   /* FUN_1801249d0 (575: 0 -> 1 -> 2 -> 0) / FUN_1801248e0 (577: 0 -> 1 -> 0), then 0x6b (rebuild) */
        if (code == 6) gp.dir = gp.dir >= 2 ? 0 : gp.dir + 1; else gp.inverted = !gp.inverted;
        geo_live_update(); overlay_dirty = 1; return; }
    switch (code) {
    case 1:                                             /* Apply: LAB_18027d44e / LAB_1802111df with chains: commit (sktcher_main_color /
                                                           the main-colour rebuild + UndoTransCommit), free_chains, EnableApply(0), chain_it
                                                           again (bVar4); without chains nothing. Offset Chains: commit (0x65, FUN_18001c890)
                                                           and the outer loop's next chain_it while the dialog's +0x5091 is set (l.144 /
                                                           l.237; that flag's setter is not in the dump: NOT DECODED, Apply greyed) */
        if (geo_cmd == G_OFFSETC || geo_cmd == G_XROLL) {   /* Apply (FUN_18000b7d0): the OK path, then chain_it again (the parameters kept) */
            if (!geo_panel_apply_enabled()) return;
            geo_live_commit(); geo_chains_free(); geo_xround2 = 1; geo_chain_begin();
            return; }
        if (!geo_nchains) return;
        geo_live_commit(); geo_chains_free(); if (geo_cmd == G_SREV) srf_rev_reset(); if (geo_cmd == G_SEXTR) srf_extr_reset(); geo_chain_begin();   /* Revolve: FUN_18008ca80 also clears the axis */
        return;
    case 4:                                             /* Reselect (ReselectAllChainsCommand): delete the preview + UndoTransTerminate, chain_it
                                                           from an empty list (init_chaining); its Esc (2) ends the command */
        if (geo_cmd == G_OFFSETC || geo_cmd == G_XROLL) return;
        geo_live_drop(); geo_chains_free(); if (geo_cmd == G_SEXTR) srf_extr_reset(); geo_chain_begin();
        return;
    case 8:                                             /* Flat Boundary's Add (FUN_18033dcd0): UndoTransTerminate, chain_it, append_chains,
                                                           the build again */
        if (geo_cmd != G_SFLAT) return;
        geo_live_drop(); geo_chain_append = 1; geo_chain_begin();
        return;
    case 5:                                             /* Revolve's axis Reselect (FUN_18008cb10): UndoTransTerminate, the axis pick again */
        if (geo_cmd != G_SREV) return;
        geo_live_drop(); srf_rev_reset(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1;
        return;
    case 9:                                             /* Roll's 829 "Select two points" (cui_xroll.c) */
        if (geo_cmd == G_XROLL && geo_nchains) { xrl_pick = 1; geo_xmod = 1; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; }
        return;
    case 0:                                             /* OK: commit (codes 0 / 2: (code & ~2) == 0), the loop ends */
        if ((geo_cmd == G_OFFSETC || geo_cmd == G_XROLL) && !geo_xmod) { geo_live_drop(); geo_end(); return; }   /* 0x65 with +0x5090 clear = 100 */
        geo_end(); return;
    case 2: geo_cancel(); return;                       /* Cancel: geo_live_kind (Fillet / Chamfer Chains commit; Offset Chains 100 deletes the preview) */
    }
}
/* ---- Create Letters live until OK: ours, user decision LET-1 (formats/user_decisions.md), an APPROVED DEVIATION from Mastercam 2017, whose
   create_letters commits every placement at once (FUN_18006c0b0 / FUN_18006cce0: UndoTransCommit per placement, no preview) and whose later
   setting changes only reach the next placement. Here a placement - a click (wm_let_exec), the arc of the arc alignments (FUN_18006d450,
   wm_let_arcpick) or the chains of Top of Chain (wm_let_chain_exec) - is built as the live result (geo_live_kind G_LETTERS: LV_LIVE, its
   own open undo step in the live colour). The next click / arc / chain moves it (LV_PICK_MOVES: the live letters dropped, built at the new
   placement); Apply (panel code 1) builds it again at the same placement with the panel's settings now; OK (code 0, Enter) commits it as one
   undo step and ends the command (geo_end); Esc / Cancel drops it (LV_ESC_DROP). A settings change alone does not rebuild (geo_live_update).
   The letter geometry itself (fonts, widths, alignment, chain placement, the missing-file list, LETTERS.PRM) is unchanged (cui_wfmisc.c). */
static Chain let_chs[256]; static int let_nchs;          /* the chains the live Top of Chain letters were built on (taken over from geo_chains) */
static void let_chs_free(void) { for (int i = 0; i < let_nchs; i++) cam_chain_free(&let_chs[i]); let_nchs = 0; }
/* the placement built live (geo_run without its pick list: the point p, or the chains let_chs); the live letters before it dropped */
static void let_build(int chain, V3 p) {
    geo_live_drop();
    int from = F.nents, nu = nundo;
    memset(&geo_in_now, 0, sizeof geo_in_now); if (!chain) { geo_in_now.npick = 1; geo_in_now.pick[0] = p; }
    geo_live_n = 0; geo_live_marked = 0;
    if (chain) {                                         /* wm_let_chain_exec reads geo_chains: let_chs lent to it (geo_chains is empty while chain_it runs again) */
        static Chain sv[256]; int n0 = geo_nchains; memcpy(sv, geo_chains, sizeof(Chain) * n0);
        memcpy(geo_chains, let_chs, sizeof(Chain) * let_nchs); geo_nchains = let_nchs;
        wm_let_chain_exec();
        memcpy(geo_chains, sv, sizeof(Chain) * n0); geo_nchains = n0;
    } else wm_let_exec(p);
    if (nundo == nu) undo_ents_added(from);
    if (nundo != nu) geo_live_start(from);
    scene_reupload();
}
/* the arc pick of the arc alignments (wm_let_arcpick): its centre is the placement */
void geo_let_point(V3 p) { let_chs_free(); let_build(0, p); }
/* geo_chains_done: the chains chain_it returned are the placement (they are kept for Apply; chain_it then runs again as create_letters' loop) */
static void let_take_chains(void) {
    let_chs_free();
    memcpy(let_chs, geo_chains, sizeof(Chain) * geo_nchains); let_nchs = geo_nchains; geo_nchains = 0;
    V3 z0 = { 0, 0, 0 }; let_build(1, z0);
    if (!geo_live) let_chs_free();
}
/* Apply: nothing live - nothing; the live letters built again at their placement. A placement of the other kind than the alignment now
   (Top of Chain switched since) is kept as it is: a new click / chain moves it */
static void geo_let_apply(void) {
    if (!geo_live || !geo_live_valid()) { geo_live = 0; geo_live_n = 0; return; }   /* an Undo meanwhile ended it */
    int chain = let_nchs > 0;
    if (chain != (wm_let_chain() != 0)) return;
    char msg[sizeof cam_msg]; snprintf(msg, sizeof msg, "%s", cam_msg);
    let_build(chain, geo_live_in.pick[0]);
    snprintf(cam_msg, sizeof cam_msg, "%s", msg);      /* the prompt stays (the placement prompt / chain_it's) */
    if (!geo_live) let_chs_free();
    if (wm_let_take_cancelled()) geo_end();              /* FUN_18006d450: a cancelled width prompt ends the loop */
    overlay_dirty = 1;
}
void geo_panel_button(int code) {
    if (ds_running()) return;                           /* AfxSuspendThisWindow while dyno_surf runs (FUN_180282390) */
    if ((geo_cmd == G_CDYN || geo_cmd == G_CCONST || geo_cmd == G_CFLOW) && (code == 0 || code == 1)) cd_built = 0;
    if (geo_cmd == G_CEDGE1 && (code == 0 || code == 1)) ce_prompt1 = ce_ba_on = 0;
    if (geo_cmd == G_CEDGEA && code == 4) { if (geo_live && geo_live_valid()) geo_live_drop(); ce_built = 0; sel_clear(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return; }   /* FUN_1802c3f10 code 4: ReselectAllCommand: UndoTransTerminate of the last build (0x1802c4508 in the disassembly), then select_ents again */   /* FUN_180282390 codes 1 / 0: prompt 0x1750 again */
    if (code < 0 && geo_panel_req >= 0) code = geo_panel_req;   /* a body button's code (Reselect) */
    geo_panel_req = -1;
    if (geo_cmd == G_BBOX && bb_button(code)) return;   /* cui_bbox.c: 0 OK, 1 Apply, 2 Cancel, 7 Select */
    if (xf_cmd(geo_cmd)) { xf_button(code); return; }   /* cui_xform.c: 0 OK, 1 Apply, 2 Cancel, the dialog buttons */
    if (sl_cmd(geo_cmd)) { if (!sl_button(code)) geo_end(); return; }
    if (si_cmd(geo_cmd)) { if (!si_button(code)) geo_end(); return; }
    if (pln_cmd(geo_cmd)) { if (!pln_button(code)) geo_end(); return; }
    if (sp_cmd(geo_cmd)) { if (!sp_button(code)) geo_end(); return; }
    if (dr_cmd(geo_cmd)) { if (!dr_button(code)) geo_end(); return; }   /* cui_draft.c: OK-New commits and restarts, OK / Cancel end */
    if (do_cmd(geo_cmd)) { if (!do_button(code)) geo_end(); return; }   /* cui_draft_ord.c */
    if (geo_cmd == G_LETTERS && code == 1) { geo_let_apply(); return; }   /* ours, LET-1: Apply rebuilds the live letters */
    if (geo_cmd == G_LETTERS && geo_chaining && (code == 0 || code == 2)) { if (code == 0) geo_end(); else geo_cancel(); return; }   /* ours, LET-1: OK / Cancel
                                                           while chain_it runs again (the panel is enabled with live letters) */
    if (geo_chain_cmd(geo_cmd)) { geo_chain_button(code); return; }
    if (geo_cmd == G_LNORM && code == 1) { wm_ln_apply(); return; }   /* code 1: Apply (Enter) - FUN_1803584a0 disasm 180358fbb-180359688 */
    if (geo_cmd == G_LNORM && code == 7) { wm_ln_reselect(); return; }   /* code 7 */
    if (geo_cmd == G_RGROOVE) {                          /* CDINGrv: OK (FUN_180077bb0 / FUN_1800765e0), Cancel, Select (7), Preview Groove (8) */
        if (code == 0) { if (geo_live) geo_live_drop(); geo_npick = 0; geo_run(); wm_rg_ok(); geo_end(); }   /* the live preview replaced by the run itself */
        else if (code == 2) { geo_cancel(); }
        else if (code == 7) { if (geo_live) geo_live_drop(); wm_rg_picking = 1; snprintf(cam_msg, sizeof cam_msg, "Select groove position"); overlay_dirty = 1; }   /* 631 */
        else if (code == 8) { if (geo_live) geo_live_drop(); geo_npick = 0; geo_run(); if (geo_live) wm_rg_wait_start(); else cui_message("Relief Groove", "Invalid groove geometry."); overlay_dirty = 1; }   /* FUN_180076420, 630 */
        return; }
    if (geo_cmd == G_SNSET) { if (!srf_sn_button(code)) geo_end(); return; }   /* FUN_180111490: 0 / 2 end (commit), 1 OK-New, 4 Reselect (cui_srfedit.c) */
    if (geo_cmd == G_SUNTRIM) { if (code >= 0 && code < 3) geo_end(); return; }   /* FUN_18030d870: codes 0..2 UndoTransCommit and the loop ends */
    if (geo_cmd == G_SOFFSET) {                          /* FUN_1802f1420's codes (cui_srfedit.c) */
        int r = srf_of_button(code);
        if (r && code == 1 && !gp.lk_dist) gp.v_dist = mcam_is_metric(&F) ? 2.0 : 0.1;   /* code 1: local_560 reset, FUN_1802ab2a0 SetValue(v, false) (unlocked only) */
        if (!r) { geo_end(); return; }
        geo_of_soft(); snprintf(cam_msg, sizeof cam_msg, "%s", srf_of_prompt()); return; }
    if (geo_cmd == G_SSOLID) {                           /* FUN_1803116e0's codes */
        if (code == 4) { geo_live_drop(); srf_solid_nsel = 0; ss_selecting = 1; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return; }   /* ReSelectAll */
        if (code == 0 || code == 1) {                    /* FUN_180310e60: the bodies deleted unless Keep original solid, UndoTransCommit */
            if (geo_live && !gp.del) snprintf(cam_msg, sizeof cam_msg, "Surface From Solid: deleting the original solid is NOT PORTED here - the solid is kept");
            if (code == 0) { geo_end(); return; }
            geo_live_commit(); srf_solid_nsel = 0; ss_selecting = 1; overlay_dirty = 1; return;
        }
        if (code == 2) { geo_cancel(); return; }
        return;
    }
    if (geo_cmd == G_SCONE && code == 7) {               /* the Top radius button (598): FUN_1800323c0 param_5 4 - a radius pick centred at
                                                           base + height x axis (the next view click, geo_take) */
        if (geo_live || geo_npick) { prim_top_pick = 1; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; }
        return; }
    if ((geo_cmd == G_SBLOCK || geo_cmd == G_SCYL || geo_cmd == G_SCONE) && code == 6) {   /* FUN_18000efd0: the flip state 0 -> 1 -> 2 -> 0; 1 = -|height|, else |height| */
        gp.dir = gp.dir >= 2 ? 0 : gp.dir + 1; gp.v_h = gp.dir == 1 ? -fabs(gp.v_h) : fabs(gp.v_h); geo_live_update(); overlay_dirty = 1; return; }
    const char *st = geo_steps();
    if (geo_cmd == G_LINE && (code == 0 || code == 1)) {   /* FUN_18018ea40 (sketcher_line_1.c:3599): only when not editing an endpoint
                                                           (param_2 = cVar27): commit, EnableApply 0, NumberSelected 0, CurrentlyEditing None */
        if (gp.edit_pt != 2) { if (code == 0) geo_end(); return; }   /* code 0 still leaves the loop (exit commit :2111-2116) */
        if (geo_live) { geo_line_nsel = 0; geo_line_enapply = 0; }
    }
    if (geo_cmd == G_PT_DYN && code == 1) {             /* code 1 (disasm 18034b8f0-18034b9eb): the Along box locked and a curve picked - the live point
                                                           discarded, a point at the box's value made and committed; else the live one committed
                                                           (the box disabled); then back to the entity selection */
        if (geo_nent && gp.lk_dist) { if (geo_live) geo_live_drop(); geo_npick = 0; geo_run(); }
        geo_live_commit(); geo_reset_picks(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return;
    }
    if (code == 1) geo_soft_clear_at(SC_APPLY);         /* geo_soft_clear_at: Circle :583, Arc Endpoints :731, Rectangle :815, Polygon :209,
                                                           Ellipse :302, Simple Apply */
    if (code == 1) {                                    /* code 1: commit, the command goes on */
        if (!st[0]) geo_apply(); else if (geo_live) geo_live_commit(); else { geo_reset_picks(); overlay_dirty = 1; }
    } else if (code == 0) {                             /* code 0 */
        if (!st[0] && !geo_applied) geo_apply();        /* Mastercam commits its live / preview result on OK - it does not run the creation twice */
        geo_end();
    } else if (code == 2) geo_cancel();                 /* code 2: geo_live_kind (commit, or remove where decoded) */
}

/* ------------------------------------------------------------------------------------------------ view glue */
/* build_overlay's picked entities (mcamui.c ov_picked): the entities the running command has taken */
int geo_ov_picked(int e) {
    if (geo_cmd) for (int k = 0; k < geo_nent && k < 4; k++) if (geo_ent[k] == e) return 1;
    return 0;
}
/* the drawing preview of mcamui.c ov_extra: picked points and the rubber band, the snap marker. The crosses, the
   rubber-band colours and the grid-snap cross are this program's (the original's GrTemp rubber band is not decoded);
   the entity snap gets the decoded draw_fast_marker square (0x180331460, ov_fast_marker) */
void geo_ov_extra(float **pv, int *pn, int *pcap) {
#define OV(P, R, G, B) ov_pt(pv, pn, pcap, P, R, G, B)
    pp_ov(pv, pn, pcap);                                 /* a point pick's AutoCursor marker (cui_snap.c) */
    xf_ov(pv, pn, pcap);                                 /* the Transform group / result colours (cui_xform.c) */
    if (geo_cmd == G_LNORM) wm_ln_ov(pv, pn, pcap);      /* FUN_1801c99a0: the normal arrow (cui_wfmisc.c) */
    if (geo_cmd && geo_cursor_ok) {                      /* drawing preview: picked points and the rubber band */
        V3 c = geo_cursor;
        for (int i = 0; i < geo_npick; i++) { V3 p = geo_pick[i], a = p, b = p; a.x -= ext * .006f; b.x += ext * .006f; OV(a, 0, .5f, 0); OV(b, 0, .5f, 0); a = p; b = p; a.y -= ext * .006f; b.y += ext * .006f; OV(a, 0, .5f, 0); OV(b, 0, .5f, 0); }
        { V3 ra, rb; if (geo_line_rb_seg(c, &ra, &rb)) { OV(ra, .9f, .3f, 0); OV(rb, .9f, .3f, 0); } }   /* Line Endpoints (geo_line_rb_seg) */
        if (geo_npick && geo_cmd == G_ARC3) { OV(geo_pick[geo_npick - 1], .9f, .3f, 0); OV(c, .9f, .3f, 0); }
        if (geo_npick && geo_cmd == G_RECT) { V3 a = geo_pick[0], c1 = { c.x, a.y, 0 }, c3 = { a.x, c.y, 0 }; OV(a, .9f, .3f, 0); OV(c1, .9f, .3f, 0); OV(c1, .9f, .3f, 0); OV(c, .9f, .3f, 0); OV(c, .9f, .3f, 0); OV(c3, .9f, .3f, 0); OV(c3, .9f, .3f, 0); OV(a, .9f, .3f, 0); }
        if (geo_npick && geo_cmd == G_CIRCLE) { double r = hypot(c.x - geo_pick[0].x, c.y - geo_pick[0].y);
            for (int i = 0; i < 72; i++) { V3 a = { geo_pick[0].x + r * cos(i * M_PI / 36), geo_pick[0].y + r * sin(i * M_PI / 36), 0 }, b = { geo_pick[0].x + r * cos((i + 1) * M_PI / 36), geo_pick[0].y + r * sin((i + 1) * M_PI / 36), 0 }; OV(a, .9f, .3f, 0); OV(b, .9f, .3f, 0); } }
        if (geo_snap_ent && geo_snap_kind != GK_GRID) {      /* the grid-only snap keeps this program's cross marker (ours) */
            ov_fast_marker(pv, pn, pcap, c);
        } else { V3 a = c, b = c; a.x -= ext * .01f; b.x += ext * .01f; OV(a, geo_snapped ? 0.f : .2f, geo_snapped ? .6f : .2f, geo_snapped ? 0.f : .2f); OV(b, geo_snapped ? 0.f : .2f, geo_snapped ? .6f : .2f, geo_snapped ? 0.f : .2f);   /* the grid-snap cross is ours */
          a = c; b = c; a.y -= ext * .01f; b.y += ext * .01f; OV(a, geo_snapped ? 0.f : .2f, geo_snapped ? .6f : .2f, geo_snapped ? 0.f : .2f); OV(b, geo_snapped ? 0.f : .2f, geo_snapped ? .6f : .2f, geo_snapped ? 0.f : .2f); }
    }
#undef OV
}
/* the solid-face branch of the curve commands' pick (get_event mask 0x1000a00a event 0x2000: FUN_1802980f0 ed_cad2.c:279, FUN_180282390 local_e850 == 0x2000, FUN_180287a40; All Edges' select_ents mask 0x10100): refused until it is ported */
static int geo_face_refused(float mx, float my, int e) {
    if ((e >= 0 && e < F.nents && F.ents[e].type == E_SURF) || sld_pick_part_solid(mx, my) < 0) return 0;
    snprintf(cam_msg, sizeof cam_msg, "%s: a solid or solid face is NOT PORTED for this command; pick a surface", geo_name(geo_cmd)); overlay_dirty = 1; return 1;
}
/* a left click in the view while a command waits for a pick (mcamui.c handle_click's drawing branch): w = the world
   point under the click (the UI's scene_pick); a point is AutoCursor-snapped (geo_snap with the last pick as the
   rubber-band anchor, then the consumer-side angular snap - only when nothing snapped, fast_data[9] == 0), an entity
   pick takes the entity under the cursor. 0 = no pick is waiting (the UI goes on with its other click cases). */
static int geo_surface_or_face(float mx, float my, const double w[3], V3 *q) {   /* FUN_180282390 / FUN_1802980f0 get_event mask 0x100, then event 0x2000 (a solid face, sld_face_to_temp_file): surface first [the order of the two is get_event's, NOT DECODED]; a face: DS_TEMP, the pick point on the face */
    int e = pick_entity(mx, my), si, fi; double hp[3]; q->x = w[0]; q->y = w[1]; q->z = w[2];
    if (e >= 0 && F.ents[e].type == E_SURF) return e;
    int k = sld_pick_part_face(mx, my, &si, &fi, hp);
    if (k == 2) { snprintf(cam_msg, sizeof cam_msg, "A solid made in this session: its faces are NOT PORTED (only the part's solids)"); return -2; }
    if (k == 1) { if (ce_face_temp(si, fi, cam_msg, sizeof cam_msg)) { q->x = hp[0]; q->y = hp[1]; q->z = hp[2]; return DS_TEMP; } return -2; }   /* -2: refused, the message kept (geo_take would replace it with the prompt) */
    return e;
}
int geo_click(float mx, float my, const double w[3], int ctrl) {
    if (geo_cmd == G_SNSET || geo_cmd == G_SUNTRIM || geo_cmd == G_SOFFSET) {    /* get_event / select_ents mask 0x100: surfaces only (cui_srfedit.c) */
        int e = pick_entity_surf(mx, my);
        if (e >= 0) { if (geo_cmd == G_SNSET) srf_sn_pick(e); else if (geo_cmd == G_SOFFSET) { srf_of_pick(e); if (!srf_of_selecting()) geo_of_soft(); } else srf_ut_pick(e); }
        overlay_dirty = 1; return 1;
    }
    if (geo_cmd == G_SSOLID) {                           /* From Solid's selection: a body (get_event events 2 / 0x1000; the faces 0x2000 need the
                                                           Selection Bar's solid buttons - NOT PORTED: bodies only) */
        if (!ss_selecting) return 1;
        int si = sld_pick_part_solid(mx, my); if (si < 0) return 1;
        for (int k = 0; k < srf_solid_nsel; k++) if (srf_solid_sel[k] == si) return 1;   /* FUN_180310d80 (the list add) NOT DECODED: no repeats */
        if (srf_solid_nsel < 64) srf_solid_sel[srf_solid_nsel++] = si;
        snprintf(cam_msg, sizeof cam_msg, "%d solid(s) selected - %s", srf_solid_nsel, geo_prompt()); overlay_dirty = 1;
        return 1;
    }
    if (wm_rg_wait) return 1;                            /* wait_for_space: the view is not serviced */
    if (geo_cmd == G_LNORM) {                            /* FUN_1803584a0: mask 0x1000200a 0x100 (surface / solid face), then the location (AutoCursor) */
        if (!wm_ln_active()) { V3 q = { w[0], w[1], w[2] }; wm_ln_click_pick(mx, my, q); return 1; }
        V3 p = { w[0], w[1], constr_depth };
        geo_snap_ptr(mx, my); geo_snap(&p, NULL, -1, 1); geo_snap_ptr_off();   /* the click's point (event 1, get_event 0x10008109) */
        wm_ln_take(-1, p); return 1;
    }
    if (ds_running()) {                                  /* dyno_surf: a click (cui_dsurf.c): event 1 = 0xf with the locked AutoCursor point (param_4 only, the mask's bit 1), else event 4 = 0xe: get_event, decomp/getevent/mccore_getevent_msgs.c:668-740 */
        double uu[2], sp[3] = { geo_cursor.x, geo_cursor.y, geo_cursor.z }; int sn = ds_autocursor() && geo_snap_ent != 0 && geo_snap_kind != GK_GRID; ds_uu(sn ? sp : w, uu);
        if (ds_event(sn ? 0xf : 0xe, uu, sn ? sp : NULL)) geo_ds_done();
        overlay_dirty = 1; return 1;
    }
    if (geo_cmd == G_CEDGEA && !ce_built && geo_face_refused(mx, my, pick_entity(mx, my))) return 1;   /* select_ents mask 0x10100 with allow_solid_surfaces (FUN_1802c4f10): solids and faces, not here */
    if (!(geo_cmd && geo_need())) return 0;
    if (geo_cmd == G_CEDGE1) {                            /* FUN_1802a2940: get_event 0x1000c00a | 0x100: the surface first [the two events' order is get_event's, NOT DECODED, as Line Normal], else a solid edge (0x4000) */
        int e0 = pick_entity(mx, my), si, ei; V3 q = { w[0], w[1], w[2] };
        if (e0 >= 0 && F.ents[e0].type == E_SURF) { geo_take(e0, q); return 1; }
        if (ce_pick_edge(mx, my, &si, &ei)) { ce_sld_s = si; ce_sld_e = ei; geo_ent[0] = -1; geo_nent = 1; geo_run(); ce_prompt1 = 1; geo_reset_picks(); snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; return 1; }
        geo_take(e0, q); return 1; }
    if (geo_cmd == G_CDYN) { V3 q; int e0 = geo_surface_or_face(mx, my, w, &q); if (e0 == -2) { overlay_dirty = 1; return 1; } geo_take(e0, q); return 1; }   /* FUN_180282390: the surface or solid face pick */
    if (geo_cmd == G_CFLOW) { V3 q = { w[0], w[1], w[2] }; int e = pick_entity(mx, my); if (geo_face_refused(mx, my, e)) return 1; geo_take(e, q); return 1; }
    if (geo_cmd == G_STRIMS) { if (srf_trims_phase() < 2) return 0;   /* select_multi_surfs: the ordinary selection, Enter ends a set */
        V3 q = { w[0], w[1], w[2] }; int e0 = pick_entity(mx, my); if (srf_trims_click(e0, q, gp.method, cam_msg, sizeof cam_msg)) { geo_ent[0] = e0; geo_entpt[0] = q; } overlay_dirty = 1; return 1; }
    if (geo_cmd == G_SEXT) { V3 q; int e0 = geo_surface_or_face(mx, my, w, &q); if (e0 == -2) { overlay_dirty = 1; return 1; } geo_take(e0, q); return 1; }   /* get_event mask 0x100 (a surface; the filter is in geo_take); while the arrow runs: the click at the point on the surface, as Constant Parameter's */
    if (geo_cmd == G_CCONST) { V3 q; int e0 = geo_surface_or_face(mx, my, w, &q); if (e0 == -2) { overlay_dirty = 1; return 1; } geo_take(e0, q); return 1; }   /* the surface pick, then dyno_surf: the 3D point on the surface (the world point the click returned; the surface filter is in geo_take) */
    if (geo_cmd == G_BBOX) return bb_click(mx, my);     /* the push-pull face pick (cui_bbox.c) */
    if (sl_cmd(geo_cmd)) return sl_click(mx, my, w, ctrl);
    if (si_cmd(geo_cmd)) return si_click(mx, my, w, ctrl);
    if (pln_cmd(geo_cmd)) return pln_click(mx, my, w, ctrl);
    if (sp_cmd(geo_cmd)) return sp_click(mx, my, w, ctrl);
    if (dr_cmd(geo_cmd)) return dr_click(mx, my, w, ctrl);   /* cui_draft.c */
    if (do_cmd(geo_cmd)) return do_click(mx, my, w, ctrl);   /* cui_draft_ord.c */
    V3 p = { w[0], w[1], grid_z() };
    if (geo_cmd == G_LETTERS && wm_let_wants_arc()) { int e = pick_entity(mx, my); if (e >= 0 && F.ents[e].type == E_ARC) { geo_take(e, p); return 1; } }   /* FUN_18006d450: mask 3 for the arc alignments */
    if (geo_need() == 'p') { p.z = constr_depth;          /* a free click: on the C-plane at the construction depth (Top here: world Z) */
        const V3 *an = geo_npick ? &geo_pick[geo_npick - 1] : NULL;
        geo_snap_ptr(mx, my); int sn = geo_snap(&p, an, geo_rb_type(), 1); geo_snap_ptr_off();   /* around the click */
        if (!sn) sn = geo_ang_snap(&p, an, geo_rb_type(), ctrl) ? -1 : 0;   /* angular only when nothing snapped (fast_data[9] == 0) */
        if (getenv("MCAMUI_SNAP_DEBUG")) fprintf(stderr, "snap: click %.3f,%.3f -> %.3f,%.3f kind=%d (anchor %d)\n", w[0], w[1], p.x, p.y, sn ? geo_snap_kind : 0, an != NULL);
        if (!cfg_constr_3d) p.z = constr_depth;           /* 2D construction: every point at the depth (cui_attr.c) */
        geo_point(p); }
    else geo_take(pick_entity(mx, my), p);
    return 1;
}
/* the drawing cursor on a mouse move in the view (mcamui.c main loop): raw = the unrounded cursor on the grid plane
   (geo_snap runs its own grid pass), mouse_snapped = cui_cursor_world's grid snap */
/* Arc Polar's rubber band 0x14 (rubber/rb.c:1053-1106): while the cursor is within 0.1 of arc length from the start point the sign follows the drag - end angle minus start
   angle, both in [0, 2pi), |d| r < 0.1 -> clockwise when d < 0; it latches once the cursor is further away (sign of the radius put_rubber_band_double, FUN_1801af4b0) */
static void geo_apolar_rb(V3 c) {
    if (geo_cmd != G_APOLAR) return;
    int typed = gp.lk_r && gp.lk_sa, have = typed ? geo_npick == 1 : geo_npick == 2; if (!have) return;
    V3 ct = geo_pick[0]; double R = gp.r, a0 = gp.sa * M_PI / 180;
    if (!typed) { V3 s = geo_pick[1]; R = hypot(s.x - ct.x, s.y - ct.y); a0 = atan2(s.y - ct.y, s.x - ct.x); }
    double dx = c.x - ct.x, dy = c.y - ct.y, en = 0; if (dx * dx + dy * dy >= 1e-12) { en = atan2(dy, dx); if (en < 0) en += 2 * M_PI; }
    if (a0 < 0) a0 += 2 * M_PI;
    double d = en - a0; if (fabs(d) * R < 0.1) geo_apolar_cw = d < 0;
}
void geo_cursor_update(const double raw[3], int ctrl, int mouse_snapped) {
    if (!geo_cmd || cui_view_drag) return;             /* not during a view drag (cui_snap.c cui_view_drag) */
    if (sp_cmd(geo_cmd)) sp_move();                     /* dyno_curve's slide (cui_spline.c) */
    if (ds_running()) {                                 /* dyno_surf (cui_dsurf.c): the arrow follows the cursor; with param_4 the AutoCursor runs on each move (dyno_get_event mask | 1), a locked point reported as the move's position (core1.c:4055-4064), One Edge has none */
        double uu[2]; int sn = 0;
        if (ds_autocursor()) { geo_cursor = (V3){ raw[0], raw[1], 0 }; if (cui_ptr_ok) geo_snap_ptr(cui_ptr_x, cui_ptr_y); geo_snap_ent = geo_snap(&geo_cursor, NULL, -1, 1); geo_snap_ptr_off(); geo_snapped = geo_snap_ent || mouse_snapped; geo_cursor_ok = 1; sn = geo_snap_ent != 0 && geo_snap_kind != GK_GRID; }
        else geo_snap_ent = 0;
        double sp[3] = { geo_cursor.x, geo_cursor.y, geo_cursor.z }; ds_uu(sn ? sp : raw, uu); ds_event(0, uu, sn ? sp : NULL); overlay_dirty = 1; return;
    }
    if ((dr_cmd(geo_cmd) && dr_need() == 't') || (do_cmd(geo_cmd) && do_need() == 't')) {   /* the text placement: no AutoCursor (get_event 0xc, create.md 2.3; the ordinate drag FUN_180046c50 flags 0xc), the sketched point */
        double w[3] = { raw[0], raw[1], raw[2] };
        if (mouse_snapped) grid_round(&w[0], &w[1]);    /* cui_cursor_world's grid snap */
        geo_cursor = (V3){ w[0], w[1], 0 }; geo_snap_ent = 0; geo_snapped = mouse_snapped; geo_cursor_ok = 0;
        if (do_cmd(geo_cmd)) do_move(w, ctrl); else dr_move(w, ctrl);
        overlay_dirty = 1; return;
    }
    if (dr_cmd(geo_cmd) && dr_need() == 's') {           /* the note drag (cui_draft_note.c): the note follows the sketched point and the
                                                           AutoCursor runs live on the same move (get_event 0xd: point bit 1) */
        double w[3] = { raw[0], raw[1], raw[2] };
        if (mouse_snapped) grid_round(&w[0], &w[1]);
        dr_move(w, ctrl);
        geo_cursor = (V3){ raw[0], raw[1], 0 };
        if (cui_ptr_ok) geo_snap_ptr(cui_ptr_x, cui_ptr_y);
        geo_snap_ent = geo_snap(&geo_cursor, NULL, -1, 1);
        geo_snap_ptr_off();
        geo_snapped = geo_snap_ent || mouse_snapped; geo_cursor_ok = 1; overlay_dirty = 1; return;
    }
    const V3 *an = geo_npick ? &geo_pick[geo_npick - 1] : NULL;   /* the rubber-band anchor = the last pick */
    if (geo_cmd == G_LNORM) {                            /* event 0x100 in FUN_1803584a0: the arrow follows the cursor (FUN_1801c90d0) */
        geo_cursor = (V3){ raw[0], raw[1], 0 };
        if (cui_ptr_ok) geo_snap_ptr(cui_ptr_x, cui_ptr_y);
        geo_snap_ent = geo_snap(&geo_cursor, NULL, -1, 1); geo_snap_ptr_off();
        geo_snapped = geo_snap_ent != 0; geo_cursor_ok = 1;
        wm_ln_hover(geo_cursor, geo_snap_ent != 0); overlay_dirty = 1; return;
    }
    geo_cursor = (V3){ raw[0], raw[1], 0 };
    if (cui_ptr_ok) geo_snap_ptr(cui_ptr_x, cui_ptr_y);   /* the snap around the mouse (cui_snap.c geo_snap_ptr) */
    geo_snap_ent = geo_snap(&geo_cursor, an, geo_rb_type(), 1);
    geo_snap_ptr_off();
    int ang = !geo_snap_ent && geo_ang_snap(&geo_cursor, an, geo_rb_type(), ctrl);
    geo_snapped = geo_snap_ent || ang || mouse_snapped; geo_cursor_ok = 1; overlay_dirty = 1;
    geo_apolar_rb(geo_cursor);
}
/* Temporary Midpoints capture key 'N' (opmanres string 360 "Temporary Midpoi&nts" -> the letter after '&',
   FUN_1800f44f0 decomp/mastercam_acpk.c; Alt / Ctrl / Shift all up, FUN_1800f73c0 - the UI checks the keys); gated on
   the mode checkbox here (the original's power-keys flag 362 is not ported). 1 = taken */
int geo_tmid_key(void) {
    if (!(geo_cmd && geo_need() == 'p' && ac_snap[ACS_TMID])) return 0;
    ac_tmid_capture(geo_cursor_ok ? &geo_cursor : NULL);
    return 1;
}
/* mini toolbar Set From Entity (53419, FT set_main_from_entity): the main level / colour from the first selected
   entity (point / line style and width are not modelled) - mcamui.c run_mc_function */
void set_main_from_entity(void) {
    int k = -1; for (int i = 0; ent_sel && i < ent_sel_n; i++) if (ent_sel[i]) { k = i; break; }
    if (k >= 0) { geo_level = F.ents[k].level; geo_color = F.ents[k].color; cam_msg[0] = 0; }
    else cam_msg[0] = 0;
}

/* ------------------------------------------------------------------------------------------------ script steps */
/* the MCAMUI_SCRIPT steps of the wireframe commands (mcamui.c run_script; both UIs, same output):
   draw:CMD|x,y|eN@x,y|cN|apply|keep  gset:K=V,..  geoline:x0,y0,x1,y1  geocmd:NAME  gpdist:D  geodone  geoesc  geoapply
   geolive[:N0]  gpset:K=V  geopick:E,X,Y  geochamf:E0,X,Y,E1,X,Y[,mode,dist]  geoparam:D,W,H; ours: geobtn:N geochains bbenter bbdump constr3d:0|1.
   1 = the step was one. */
int cui_geo_script(const char *st, const char *arg_in) {
    char argbuf[1024]; const char *arg = NULL;
    if (arg_in) { snprintf(argbuf, sizeof argbuf, "%s", arg_in); arg = argbuf; }
    if (!strncmp(st, "draw", 4) && arg) {             /* draw:CMD|x,y|x,y...  e.g. draw:Rectangle|0,0|100,50 */
        char a2[512]; snprintf(a2, sizeof a2, "%s", arg); char *bar = strchr(a2, '|'); if (bar) *bar++ = 0;
        /* picks: x,y = a point; eN@x,y = entity N picked at x,y; cN = the chain through entity N (the chain commands: a
           pick in the Chaining dialog at the entity's midpoint, several cN = several chains, the dialog's OK before the
           next other step / at the end) */
        if (geo_start(a2)) { for (char *q = bar; q && *q; ) {
                if (*q != 'c' && geo_chaining && ncchains) chain_act(1);   /* the Chaining dialog's OK */
                if (*q == 'e' || *q == 'c') {
                    int kind = *q++; int en = (int)strtol(q, &q, 10); double x = 0, y = 0;
                    if (*q == '@') { q++; x = strtod(q, &q); if (*q == ',') q++; y = strtod(q, &q); }
                    if (kind == 'c' && en >= 0 && en < F.nents) { const Ent *E = &F.ents[en]; x = E->type == E_ARC ? E->c.x + E->r * cos(E->a0 + E->sw / 2) : (E->a.x + E->b.x) / 2; y = E->type == E_ARC ? E->c.y + E->r * sin(E->a0 + E->sw / 2) : (E->a.y + E->b.y) / 2; }
                    if (kind == 'c' && geo_chaining) chain_pick_at(en, x, y);
                    else geo_take(en < F.nents ? en : -1, (V3){ x, y, 0 });
                } else if (!strncmp(q, "apply", 5)) { geo_apply(); q += 5; }
                else if (!strncmp(q, "keep", 4)) { q += 4; }
                else { double x = strtod(q, &q); if (*q == ',') q++; double y = strtod(q, &q), z = constr_depth;   /* X,Y: at the construction depth */
                       if (*q == ',' && q[1] && q[1] != '|') { q++; z = strtod(q, &q); }   /* X,Y,Z (ours) */
                       V3 p = { x, y, z }; geo_point(p); }
                if (*q == '|') q++; else break; }
                            if (geo_chaining && ncchains) chain_act(1);
                            if (!bar || !strstr(bar, "keep")) geo_end(); }
        else printf("    (unknown drawing command %s)\n", a2);
    }
    else if (!strcmp(st, "gset") && arg) {                /* gset:dist=5,r=2,mode=1 - the running command's options (after geocmd) */
        /* panel values with their locks set (geo_panel_sync derives the geometry's values from them) */
        if (geo_cmd) { gp.lk_len = gp.lk_ang = gp.lk_dist = gp.lk_r = gp.lk_w = gp.lk_h = gp.lk_n = 1; }
        const struct { const char *k; double *d; int *i; } T[] = {
            { "len", &gp.v_len }, { "ang", &gp.v_ang }, { "dist", &gp.v_dist }, { "r", &gp.v_r }, { "w", &gp.v_w }, { "h", &gp.v_h },
            { "n", NULL, &gp.v_n }, { "sides", NULL, &gp.v_n }, { "poly_r", &gp.v_r }, { "entmode", NULL, &gp.entmode }, { "multi", NULL, &gp.multi },
            { "dir", NULL, &gp.dir }, { "start_pt", NULL, &gp.start_pt }, { "full", NULL, &gp.full }, { "step", &gp.v_ang },
            { "len", &gp.len }, { "ang", &gp.ang }, { "dist", &gp.dist }, { "r", &gp.r }, { "sa", &gp.sa }, { "ea", &gp.ea }, { "rot", &gp.rot },
            { "a", &gp.ea_r }, { "b", &gp.eb_r }, { "pitch", &gp.pitch }, { "revs", &gp.revs }, { "zpitch", &gp.zpitch }, { "zpitch2", &gp.zpitch2 }, { "pitch2", &gp.pitch2 }, { "taper", &gp.taper }, { "ell_tol", &gp.ell_tol }, { "z", &gp.z },
            { "maxr", &gp.maxr }, { "d2", &gp.d2 }, { "corner_r", &gp.corner_r }, { "hole_dia", &gp.hole_dia },
            { "bolt_r", &gp.bolt_r }, { "dia", &geo_dia }, { "w", &geo_rw }, { "h", &geo_rh }, { "poly_r", &geo_poly_r },
            { "n", NULL, &gp.n }, { "copies", NULL, &gp.copies }, { "mode", NULL, &gp.mode }, { "trim", NULL, &gp.trim }, { "anchor_ctr", NULL, &gp.anchor_ctr },
            { "flat", NULL, &gp.flat }, { "horiz", NULL, &gp.horiz }, { "vert", NULL, &gp.vert }, { "pts", NULL, &gp.pts }, { "circles", NULL, &gp.circles },
            { "ctr", NULL, &gp.ctr }, { "single", NULL, &gp.single }, { "del", NULL, &gp.del }, { "move", NULL, &gp.move }, { "shape", NULL, &gp.shape }, { "style", NULL, &gp.style }, { "anchor", NULL, &gp.anchor }, { "ell_out", NULL, &gp.ell_out }, { "round", NULL, &gp.round },
            { "cw", NULL, &gp.cw }, { "cont", NULL, &gp.cont }, { "partial", NULL, &gp.partial }, { "sides", NULL, &geo_poly_n } };
        char a3[512]; snprintf(a3, sizeof a3, "%s", arg);
        for (char *sv = NULL, *kv = strtok_r(a3, ",", &sv); kv; kv = strtok_r(NULL, ",", &sv)) {
            char *eq = strchr(kv, '='); if (!eq) continue; *eq++ = 0; int hit = 0;
            for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) if (!strcmp(T[k].k, kv)) { if (T[k].d) *T[k].d = atof(eq); else *T[k].i = atoi(eq); hit = 1; }
            if (!hit) printf("    (unknown option %s)\n", kv);
        }
    }
    else if (!strcmp(st, "geoline") && arg) { double a[4] = {0}; sscanf(arg, "%lf,%lf,%lf,%lf", &a[0], &a[1], &a[2], &a[3]);   /* geoline:x0,y0,x1,y1 */
        if (!loaded && cui_hooks.new_part) cui_hooks.new_part();
        V3 p = { a[0], a[1], 0 }, q = { a[2], a[3], 0 }, z0 = { 0, 0, 0 }; geo_add(E_LINE, p, q, z0, 0, 0, 0); scene_reupload(); }
    else if (!strcmp(st, "geocmd") && arg) geo_start(arg);   /* geocmd:Chamfer Entities */
    else if (!strcmp(st, "gpdist") && arg) { gp.dist = atof(arg); geo_live_update(); }   /* panel Distance 1 */
    else if (xf_script(st, arg)) ;                        /* the Transform tab's steps (cui_xform.c) */
    else if (dr_script(st, arg)) ;                        /* the Drafting tab's steps (cui_draft.c) */
    else if (do_script(st, arg)) ;                        /* the Ordinate steps (cui_draft_ord.c) */
    else if (!strcmp(st, "geodone") && pln_cmd(geo_cmd)) geo_panel_button(0);   /* Curve Parting Line: the panel OK */
    else if (!strcmp(st, "geoapply") && pln_cmd(geo_cmd)) geo_panel_button(1);   /* its Apply (code 1) */
    else if (!strcmp(st, "geoenter") && pln_cmd(geo_cmd)) pln_enter_key();
    else if (!strcmp(st, "plset") && arg) pln_script(arg);   /* tests: Curve Parting Line (cui_partline.c) */
    else if (!strcmp(st, "geodone")) { if (geo_cmd == G_BBOX || xf_cmd(geo_cmd) || dr_cmd(geo_cmd) || do_cmd(geo_cmd) || sp_cmd(geo_cmd) || sl_cmd(geo_cmd) || si_cmd(geo_cmd) || geo_cmd == G_OFFSETC || geo_cmd == G_XROLL) geo_panel_button(0);   /* Offset Chains / Roll: the OK
                                                                  path (0x65 with +0x5090 clear = 100, geo_chain_button) */ else { if (!geo_steps()[0] && !geo_applied) geo_apply(); geo_end(); } }   /* the panel's OK (code 0) */
    else if (!strcmp(st, "geomove") && arg) { double r[3] = { 0, 0, 0 }; if (sscanf(arg, "%lf,%lf,%lf", &r[0], &r[1], &r[2]) >= 2) geo_cursor_update(r, 0, 0); }   /* tests: a mouse move at a world point (geo_cursor_update, the AutoCursor runs) */
    else if (!strcmp(st, "dsclickw") && arg) { double r[3] = { 0, 0, 0 }; if (sscanf(arg, "%lf,%lf,%lf", &r[0], &r[1], &r[2]) >= 2) geo_click(0, 0, r, 0); }   /* tests: a left click at a world point while dyno_surf runs (geo_click) */
    else if (!strcmp(st, "geomsg")) printf("      0 msg dirty %d ba %d prompt '%s' msg '%s'\n", doc_dirty, cedge_trimmed(), geo_prompt(), cam_msg);   /* tests: the prompt, message, modified flag, One Edge's Break Angle enable */
    else if (!strcmp(st, "srfsetkind") && arg) { int e, k; if (sscanf(arg, "%d,%d", &e, &k) == 2 && e >= 0 && e < F.nents && F.ents[e].type == E_SURF) { Surf *b = &F.surfs[F.ents[e].srf]; if (b->kind == 10) { int64_t ep; memcpy(&ep, b->rec + 0x100, 8); const Surf *q = mcs_surf(&F, ep); if (q) b = (Surf *)q; } b->kind = k; } }   /* tests: the kind of a surface (a trimmed surface's base), a kind with no evaluator */
    else if (!strcmp(st, "dsmove") && arg) { double uu[2]; if (sscanf(arg, "%lf,%lf", &uu[0], &uu[1]) == 2) ds_event(0, uu, NULL); }   /* tests: a move of dyno_surf (view-plane coordinates) */
    else if (!strcmp(st, "edgcase") && arg) { void srf_test_edgcase(int, const char *, double); int e; double a; char p[1024]; if (sscanf(arg, "%d,%lf,%1023s", &e, &a, p) == 3) srf_test_edgcase(e, p, a); }   /* tests: edgcase:E,ANGLE,PATH (tools/peloader --edg) */
    else if (!strcmp(st, "edgbatch") && arg) { void srf_test_edgbatch(const char *, double); double a; char p[1024]; if (sscanf(arg, "%lf,%1023s", &a, p) == 2) srf_test_edgbatch(p, a); }   /* tests: edgbatch:ANGLE,PREFIX */
    else if (!strcmp(st, "cesel") && arg) { int si = -1, fi = -1; sscanf(arg, "%d,%d", &si, &fi); if (ce_nsel < 64) { ce_sel[ce_nsel].si = si; ce_sel[ce_nsel].fi = fi; ce_nsel++; } }   /* tests: Curve All Edges' solid list: cesel:S (body) / cesel:S,F (face) */
    else if (!strcmp(st, "cepick") && arg) { int si = -1, ei = -1; if (sscanf(arg, "%d,%d", &si, &ei) == 2) { ce_sld_s = si; ce_sld_e = ei; geo_ent[0] = -1; geo_nent = 1; geo_run(); ce_prompt1 = 1; geo_reset_picks(); overlay_dirty = 1; } }   /* tests: Curve One Edge's event 0x4000 at solid S, edge E */
    else if (!strcmp(st, "sldscan")) ce_scan();
    else if (!strcmp(st, "cedump") && arg) { int si = atoi(arg); ce_dump_edges(si); }
    else if (!strcmp(st, "cefacet") && arg) { int si, fi; if (sscanf(arg, "%d,%d", &si, &fi) == 2 && ce_face_temp(si, fi, cam_msg, sizeof cam_msg)) { double d[4], c[3] = { 0, 0, 0 }; mcs_surf_domain(ce_temp_surf, d); mcs_surf_eval(&F, ce_temp_surf, (d[0] + d[1]) / 2, (d[2] + d[3]) / 2, c); printf("      0 ceface kind %d dom %.6f %.6f %.6f %.6f mid %.6f %.6f %.6f\n", ce_temp_surf->kind, d[0], d[1], d[2], d[3], c[0], c[1], c[2]); } }   /* tests */
    else if (!strcmp(st, "edgscan")) { void srf_test_edgscan(void); srf_test_edgscan(); }
    else if (!strcmp(st, "dskey") && arg) { if (ds_running() && (arg[0] == 'a' || arg[0] == 'A')) geo_ds_key(0xb); }   /* tests: dyno_get_event's key 'A' (the accept of modes 3 / 5) */
    else if (!strcmp(st, "dsdump")) { double u, v; ds_uv(&u, &v); printf("      0 ds on %d uv %.7f %.7f n %d poly %d\n", ds_running(), u, v, ds_point_count(), ds_debug_poly()); }   /* tests: dyno_surf's state */
    else if (!strcmp(st, "dsclick") && arg && *arg) { double p[3]; if (sscanf(arg, "%lf,%lf,%lf", &p[0], &p[1], &p[2]) == 3) { double uu[2]; ds_uu(p, uu); if (ds_event(0xf, uu, p)) geo_ds_done(); } }   /* tests: a click on the AutoCursor at (x, y, z) */
    else if (!strcmp(st, "dsclick")) { if (ds_event(0xe, NULL, NULL)) geo_ds_done(); }   /* tests: a plain click */
    else if (!strcmp(st, "spset") && arg) sp_script(arg);
    else if (!strcmp(st, "siset") && arg) si_script(arg);   /* tests: Curve at Intersection (cui_srfint.c) */
    else if (!strcmp(st, "slset") && arg) sl_script(arg);   /* tests: Curve Slice plane / options (cui_slice.c) */   /* tests: the spline panels' end conditions (cui_spline.c) */
    else if (!strcmp(st, "geoenter")) { if (ds_running()) geo_ds_key(0xb); else if (geo_cmd == G_CEDGEA || geo_cmd == G_STRIMS) geo_ce_enter(); else if (sp_cmd(geo_cmd)) sp_enter_key(); else if (sl_cmd(geo_cmd)) sl_enter_key(); else if (si_cmd(geo_cmd)) si_enter_key(); }   /* the <Enter> key of the spline commands (FUN_180260ef0 event 0x800, select_ents' End Selection) */
    else if (!strcmp(st, "geoesc")) geo_escape();
    else if (!strcmp(st, "geoapply")) { if (geo_chain_cmd(geo_cmd) || geo_cmd == G_LETTERS || geo_cmd == G_BBOX || xf_cmd(geo_cmd) || dr_cmd(geo_cmd) || do_cmd(geo_cmd) || sp_cmd(geo_cmd) || sl_cmd(geo_cmd) || si_cmd(geo_cmd)) geo_panel_button(1); else if (!geo_steps()[0]) geo_apply(); else geo_live_commit(); }   /* the panel's Apply (code 1) */
    else if (!strcmp(st, "geobtn") && arg) geo_panel_button(atoi(arg));   /* ours: geobtn:N - panel code N (0 OK, 1 Apply, 2 Cancel, 4 Reselect) */
    else if (!strcmp(st, "geochains")) {                  /* ours: the chain command's state and chains */
        printf("    geochains cmd '%s' chaining %d n %d apply %d panel %d\n", geo_name(geo_cmd), geo_chaining, geo_nchains, geo_panel_apply_enabled(), geo_panel_enabled());
        for (int i = 0; i < geo_nchains; i++) { printf("    geochain %d: %s,", i, geo_chains[i].closed ? "closed" : "open");
            for (int k = 0; k < geo_chains[i].n; k++) printf(" %d%s", geo_chains[i].ent[k], geo_chains[i].rev[k] ? "r" : ""); printf("\n"); }
    }
    else if (!strcmp(st, "georb") && arg) {               /* georb:x,y - the Line Endpoints rubber band with the cursor at (x, y) (tests) */
        V3 c = { 0, 0, 0 }, a, b; sscanf(arg, "%lf,%lf", &c.x, &c.y);
        if (geo_cmd == G_APOLAR) geo_apolar_rb(c);   /* the Arc Polar sign (rubber/rb.c) */
        else if (geo_line_rb_seg(c, &a, &b)) printf("    rb (%.4f,%.4f,%.4f)-(%.4f,%.4f,%.4f)\n", a.x, a.y, a.z, b.x, b.y, b.z); else printf("    rb none\n");
    }
    else if (!strcmp(st, "geolive")) {                     /* geolive: the live result list and the colours of the ents after N0 (geolive:N0) */
        printf("    live %d cmd '%s' n %d:", geo_live, geo_name(geo_cmd), geo_live ? geo_live_n : 0);
        for (int k = 0; geo_live && k < geo_live_n; k++) printf(" %d", geo_live_l[k]);
        printf("\n");
        for (int i = arg ? atoi(arg) : 0; i < F.nents; i++) if (F.ents[i].type) printf("    col %d = %d\n", i, F.ents[i].color);
    }
    else if (!strcmp(st, "gpset") && arg) {                /* gpset:NAME=VALUE - a panel value (then the live rebuild) */
        char k[32] = ""; const char *eq = strchr(arg, '='); double x = eq ? atof(eq + 1) : 0;
        snprintf(k, sizeof k, "%.*s", eq ? (int)(eq - arg) : (int)strlen(arg), arg);
        /* the panel values (and locks) as typed into the panel; geo_panel_sync derives the rest */
        if (geo_cmd == G_LETTERS) wm_let_gpset(k, x, eq ? eq + 1 : NULL); else
        if (geo_cmd == G_RGROOVE) wm_rg_gpset(k, x); else
        if (geo_cmd == G_LNORM && (!strcmp(k, "pts") || !strcmp(k, "ctr"))) { if (k[0] == 'p') gp.pts = (int)x; else gp.ctr = (int)x; } else
        if ((geo_cmd == G_SNSET || geo_cmd == G_SUNTRIM) && (!strcmp(k, "pts") || !strcmp(k, "del"))) { if (k[0] == 'p') gp.pts = (int)x; else gp.del = (int)x; } else
        if (geo_cmd == G_SOFFSET && !strcmp(k, "dist")) gp.v_dist = x; else   /* typed, not locked */
        if (geo_cmd == G_XROLL) {                         /* Roll's dialog (cui_xroll.c xrl): mode roll axisx cw dia atol ang type useattr level color */
            if (!strcmp(k, "mode")) xrl.mode = (int)x; else if (!strcmp(k, "roll")) xrl.roll = (int)x; else if (!strcmp(k, "axisx")) xrl.axisx = (int)x;
            else if (!strcmp(k, "cw")) xrl.cw = (int)x; else if (!strcmp(k, "dia")) xrl.dia = x; else if (!strcmp(k, "atol")) xrl.btol = x;
            else if (!strcmp(k, "ang")) xrl.ang = x; else if (!strcmp(k, "type")) xrl.type = (int)x; else if (!strcmp(k, "useattr")) xrl.useattr = (int)x;
            else if (!strcmp(k, "level")) xrl.level = (int)x; else if (!strcmp(k, "color")) xrl.color = (int)x;
        } else
        if (!strcmp(k, "n")) gp.n = gp.v_n = (int)x; else if (!strcmp(k, "r")) { gp.r = gp.v_r = x; gp.lk_r = 1; } else if (!strcmp(k, "len")) { gp.len = gp.v_len = x; gp.lk_len = gp.lk_ang = 1; }
        else if (!strcmp(k, "ang")) gp.ang = gp.v_ang = x; else if (!strcmp(k, "dist")) { gp.dist = gp.v_dist = x; gp.lk_dist = 1; } else if (!strcmp(k, "mode")) gp.mode = (int)x;
        else if (!strcmp(k, "cont")) gp.cont = gp.multi = (int)x; else if (!strcmp(k, "rot")) gp.rot = x; else if (!strcmp(k, "bolt_r")) gp.bolt_r = x;
        else if (!strcmp(k, "cmode")) gp.cmode = (int)x;   /* the Circle Edge Point radio (0/2 = 2 / 3 points) */
        else if (!strcmp(k, "ea_r")) { gp.ea_r = x; gp.lk_ra = x > 0; } else if (!strcmp(k, "eb_r")) { gp.eb_r = x; gp.lk_rb = x > 0; }   /* Ellipse A / B radii (locked) */
        else if (!strcmp(k, "dia")) { gp.v_r = x / 2; gp.lk_r = x > 0; } else if (!strcmp(k, "rw")) { gp.v_w = x; gp.lk_w = x > 0; } else if (!strcmp(k, "rh")) { gp.v_h = x; gp.lk_h = x > 0; }
        else if (!strcmp(k, "poly_r")) { gp.v_r = x; gp.lk_r = x > 0; } else if (!strcmp(k, "poly_n")) gp.v_n = (int)x; else if (!strcmp(k, "trim")) gp.trim = (int)x;
        else if (!strcmp(k, "trimmode")) gp.trimmode = (int)x; else if (!strcmp(k, "dir")) gp.dir = (int)x;   /* Trim / Break, Trim Many's Opposite side */
        else if (!strcmp(k, "method")) gp.method = (int)x; else if (!strcmp(k, "lines")) gp.lines = (int)x; else if (!strcmp(k, "action")) gp.action = (int)x;   /* Break Many */
        else if (!strcmp(k, "tol")) gp.v_tol = x; else if (!strcmp(k, "exact")) gp.v_dist = x; else if (!strcmp(k, "round")) gp.v_step = x;
        /* Bounding Box (cui_bbox.c): the panel's controls; the axis by the radio's letter (X / Y / Z = struct code 2 / 1 / 0) */
        else if (!strcmp(k, "bb_all")) gp.bb_all = (int)x; else if (!strcmp(k, "bb_shape")) gp.bb_shape = (int)x;
        else if (!strcmp(k, "bb_rorg")) gp.bb_rorg = (int)x; else if (!strcmp(k, "bb_corg")) gp.bb_corg = (int)x;
        else if (!strcmp(k, "bb_axis")) { const char *a = eq ? eq + 1 : ""; gp.bb_axis = *a == 'X' || *a == 'x' ? 2 : *a == 'Y' || *a == 'y' ? 1 : 0; }
        else if (!strcmp(k, "bb_center")) gp.bb_center = (int)x;
        else if (!strcmp(k, "bb_sx")) gp.bb_sx = x < 0 ? 0 : x; else if (!strcmp(k, "bb_sy")) gp.bb_sy = x < 0 ? 0 : x; else if (!strcmp(k, "bb_sz")) gp.bb_sz = x < 0 ? 0 : x;
        else if (!strcmp(k, "bb_r")) gp.bb_r = x < 0 ? 0 : x; else if (!strcmp(k, "bb_h")) gp.bb_h = x < 0 ? 0 : x;
        else if (!strcmp(k, "bb_lines")) gp.bb_lines = (int)x; else if (!strcmp(k, "bb_pts")) gp.bb_pts = (int)x; else if (!strcmp(k, "bb_ctr")) gp.bb_ctr = (int)x;
        else if (!strcmp(k, "bb_solids")) gp.bb_solids = (int)x; else if (!strcmp(k, "bb_incr")) gp.bb_incr = (int)x; else if (!strcmp(k, "bb_both")) gp.bb_both = (int)x;
        else if (!strcmp(k, "bb_ctp")) gp.bb_ctp = (int)x; else if (!strcmp(k, "bb_tpcur")) gp.bb_tpcur = (int)x; else if (!strcmp(k, "bb_tpwcs")) gp.bb_tpwcs = (int)x;
        else if (!strcmp(k, "bb_tpname")) snprintf(gp.bb_tpname, sizeof gp.bb_tpname, "%s", eq ? eq + 1 : "");
        geo_panel_sync(); geo_live_update();
    }
    else if (wm_script(st, arg)) { }                      /* tests (cui_wfmisc.c): wmviewarc */
    else if (!strcmp(st, "entids")) { for (int i = 0; i < F.nents; i++) if (F.ents[i].type) printf("      %d id %d\n", i, (int)F.ents[i].idn); }   /* ours (tests): the record ids (mod_pair keeps the original's) */
    else if (!strcmp(st, "entblank")) { for (int i = 0; i < F.nents; i++) if (F.ents[i].type && F.ents[i].blanked) printf("      %d blanked\n", i); }   /* ours (tests): the blanked entities */
    else if (!strcmp(st, "bbenter")) bb_enter();
    else if (!strcmp(st, "ssenter")) srf_solid_enter();   /* ours (tests): Enter = the end of From Solid's selection */
    else if (!strcmp(st, "snenter")) srf_ned_enter();     /* ours (tests): Enter = Set Normals' End Selection */
    else if (!strcmp(st, "sndump")) srf_sn_dump();        /* ours (tests): Set Normals' list and votes */
    else if (!strcmp(st, "ofdump")) srf_of_dump();        /* ours (tests): Offset's lists, distances and arrows (cui_srfedit.c) */
    else if (!strcmp(st, "trimdump")) srf_trim_dump();    /* ours (tests): the trimmed surfaces' loops (cui_srfedit.c) */
    else if (!strcmp(st, "tprefdump")) srf_tp_dump();     /* ours (tests): the stored ops' surface references (cui_srfedit.c) */
    else if (!strcmp(st, "sssel") && arg) { int si = atoi(arg) - 1; if (srf_solid_selecting() && si >= 0 && si < F.nsolids && srf_solid_nsel < 64) srf_solid_sel[srf_solid_nsel++] = si; }   /* ours (tests): select part solid N */          /* ours (tests): Enter = the end of the Bounding Box panel's Manual selection */
    else if (!strcmp(st, "bbdump")) bb_dump();            /* ours (tests): the Bounding Box panel's state */
    else if (!strcmp(st, "ppdump")) pp_dump();
    else if (!strcmp(st, "snapbench")) snap_bench(arg ? atoi(arg) : 400);   /* ours (measurements): ms per mouse-move snap */            /* ours (tests): a point pick's live AutoCursor (cui_snap.c pp_hover) */
    else if (bb_script(st, arg)) { }                      /* ours (tests): bbface:ID, bbclick:X,Y, bbdrag:VALUE, bbdragesc (cui_bbox.c) */
    else if (!strcmp(st, "constr3d") && arg) cfg_constr_3d = atoi(arg) != 0;   /* ours (tests): config 16009 constr_view_is_3d (2D / 3D construction) */
    else if (!strcmp(st, "trimplane") && arg) {        /* tests: trimplane:PX,PY,PZ,NX,NY,NZ[,DELUN,SPLIT,KEEPM,USECUR,KEEP] - Trim to Plane's build on the selected surfaces (the command waits for get_plane_info) */
        double o[3] = { 0 }, nv[3] = { 0, 0, 1 }; int dl = 1, sp = 0, km = 1, uc = 0, kp = 0;
        sscanf(arg, "%lf,%lf,%lf,%lf,%lf,%lf,%d,%d,%d,%d,%d", &o[0], &o[1], &o[2], &nv[0], &nv[1], &nv[2], &dl, &sp, &km, &uc, &kp);
        int *l = NULL, n = sel_list_surfs(&l); int nu = nundo, from = F.nents;
        if (srf_trimp_exec(l, n, o, nv, dl, sp, km, uc, kp, geo_level, geo_color, cam_msg, sizeof cam_msg)) { doc_dirty = 1; sel_clear(); }
        if (nundo == nu && F.nents > from) undo_ents_added(from);
        free(l); scene_reupload(); overlay_dirty = 1; }
    else if (!strcmp(st, "trimkeep") && arg) {          /* tests: trimkeep:E,X,Y,Z - Trim to Surfaces' keep pick on entity E at that point (then dsclick accepts the arrow) */
        int en = -1; double x = 0, y = 0, z = 0; sscanf(arg, "%d,%lf,%lf,%lf", &en, &x, &y, &z); V3 q = { x, y, z };
        if (geo_cmd == G_STRIMS && srf_trims_click(en, q, gp.method, cam_msg, sizeof cam_msg)) { geo_ent[0] = en; geo_entpt[0] = q; }
        snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; }
    else if (!strcmp(st, "geopick") && arg) { int en = -1; double x = 0, y = 0, z = 0; int nv = sscanf(arg, "%d,%lf,%lf,%lf", &en, &x, &y, &z); geo_pick_ac_next = nv == 4; geo_take(en, (V3){ x, y, z });   /* geopick:E,X,Y[,Z]: with Z an AutoCursor (3D) point, else the free cursor */ }
    else if (!strcmp(st, "geochamf") && arg) {          /* geochamf:E0,X,Y,E1,X,Y[,mode,dist] - Chamfer Entities with those picks */
        int e0 = 0, e1 = 0, m = 0; double k[4] = {0}, d = 5;
        sscanf(arg, "%d,%lf,%lf,%d,%lf,%lf,%d,%lf", &e0, &k[0], &k[1], &e1, &k[2], &k[3], &m, &d);
        geo_start("Chamfer Entities"); gp.mode = m; gp.dist = d; gp.chw = d;
        geo_ent[0] = e0; geo_ent[1] = e1; geo_entpt[0] = (V3){ k[0], k[1], 0 }; geo_entpt[1] = (V3){ k[2], k[3], 0 }; geo_nent = 2;
        geo_exec(); geo_cmd = 0; scene_reupload(); }
    else if (!strcmp(st, "geoparam") && arg) { double d = 0, w = 0, h = 0; sscanf(arg, "%lf,%lf,%lf", &d, &w, &h);   /* diameter, width, height (locked when > 0) */
        if (geo_cmd == G_CIRCLE) { gp.v_r = d / 2; gp.lk_r = d > 0; } gp.v_w = w; gp.lk_w = w > 0; gp.v_h = h; gp.lk_h = h > 0; geo_panel_sync(); }
    else return 0;
    return 1;
}
