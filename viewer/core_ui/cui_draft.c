/* cui_draft.c - the Drafting tab's first command set: Horizontal, Vertical, Parallel (linear dimensions) as Mastercam 2017's dimension
 * command loop runs them (mccad FUN_18003f7f0 and its state functions), the dimension record made by the port of mccore
 * calc_linear_dimension (viewer/draft_create.c), the session's drafting entities (McamFile.dnew: drawn by scene.c, saved by
 * mcam_saveops.c, kept in the project), their undo, the function panel's model (the decoded controls) and the script steps.
 * Spec: ../../formats/drafting/create.md ("create.md N" below); decompiles decomp/draft/ ("file:line"); the click / event rules of
 * mccore FUN_1804e3b90 / FUN_1804e1bb0 and mastercam dl_search_fast are cited where used.
 *
 * Runs inside the wireframe command frame (cui_geo.c: geo_cmd = G_DIMH / G_DIMV / G_DIMP, as the Transform commands do): geo_start
 * -> dr_begin, geo_need / geo_click / geo_cursor_update / geo_cancel / geo_end / geo_panel_button / geo_panel_body -> dr_*.
 * "ours" marks glue of this program (no counterpart in Mastercam's code). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "core_ui.h"
#include "draft_create.h"
#include "mcam_saveops.h"
#include "draft.h"
#include "cui_draft_int.h"

/* ------------------------------------------------------------------------------------------------ the commands */
/* RunDraftCommand -> FUN_1800392d0 (cad_rundraft.c:290): 0x9476 Horizontal, 0x9481 Vertical, 0x947d Parallel run FUN_18003f7f0;
   the per-command start FUN_180040ed0 (cad_dimcmd.c:441-512): type 0 (linear), sub 0 / 1 / 2 (create.md 2.1) */
int dr_cmd(int c) { return c == G_DIMH || c == G_DIMV || c == G_DIMP || dd_cmd(c) || dn_cmd(c); }   /* + Note (cui_draft_note.c) */
static int dr_sub(int c) { return c == G_DIMV ? 1 : c == G_DIMP ? 2 : 0; }
const char *dr_label(int c) { return c == G_DIMH ? "Horizontal" : c == G_DIMV ? "Vertical" : c == G_DIMP ? "Parallel" : dd_cmd(c) ? dd_label(c) : dn_cmd(c) ? "Note" : ""; }   /* the ribbon labels (ui/layout.txt) */
int dr_find_ft(const char *ft) {                       /* the ribbon's FT function names (layout.txt l.293-301) */
    if (!ft) return 0;
    if (!strcmp(ft, "doDraft_DIMENSION_HORIZONTAL")) return G_DIMH;
    if (!strcmp(ft, "doDraft_DIMENSION_VERTICAL")) return G_DIMV;
    if (!strcmp(ft, "doDraft_DIMENSION_PARALLEL")) return G_DIMP;
    if (dn_find_ft(ft)) return dn_find_ft(ft);          /* cui_draft_note.c: Note */
    return dd_find_ft(ft);                              /* cui_draft_dim.c: the other Dimension-group commands */
}

/* ------------------------------------------------------------------------------------------------ the session's drafting globals */
/* the globals calc_linear_dimension / InitDIM read (create.md 5), from the user's config (the writer FUN_1801d44c0 / d49c0 / d6530 /
   d86a0 / d4850, decomp/draft/core_cfgdraft.c:718-916, gives item number -> global; the reader is taken as symmetric, create.md 5).
   Loaded at the first command in these units and kept for the session (Mastercam's globals); the panel changes them. */
DcSettings dr_S, dr_saved;                       /* the globals, and DraftGlobalsSave's copy (create.md 2.6) */
static DcDimFont dr_font;
static int dr_S_units = -1;                             /* the units dr_S was loaded for (0 inch / 1 metric), -1 none */
int dr_assoc = 1;                                /* DfAssoc, config 15003 */
static DcFonts *dr_fonts; static int dr_fonts_tried;
/* the part's units: a new part (F.metric -1, units not set yet) is metric, as the engines and the defaults library take it
   (cam2d_dyn.c / cam_fileop.c defaults_file: Mill_MM) - it used to read the inch config (text height 0.25 on a mm part) */
static int dr_metric(void) { return mcam_is_metric(&F); }   /* IsMetric() (formats/units.md) */
static double cfgd(int key, double def) { char v[128]; mcam_config_value(dr_metric(), key, v, sizeof v); return v[0] ? atof(v) : def; }
static int cfgi(int key, int def) { char v[128]; mcam_config_value(dr_metric(), key, v, sizeof v); if (!v[0]) return def; if (v[0] == 'y' || v[0] == 'Y') return 1; if (v[0] == 'n' || v[0] == 'N') return 0; return atoi(v); }
void dr_globals_load(void) {
    int metric = dr_metric();
    if (dr_S_units == metric) return;
    dr_S_units = metric;
    DcSettings *S = &dr_S; memset(S, 0, sizeof *S);
    /* [dimension attributes] / [dimension text] / [leaders/witness] / [settings]; the default when the item is missing is the
       global's .data initializer where known (mccore exports: tol_text_height 0.125, alt_height / alt_chr_exp 1.0, the rest 0) */
    S->auto_align = cfgi(11004, 0); S->comma = cfgi(11005, 0); S->coord_fmt = cfgi(11006, 0);
    S->tol_mode = cfgi(11008, 0); if (S->tol_mode == 3) S->tol_mode = 1; else if (S->tol_mode > 3) S->tol_mode = 0;   /* InitDM's clamp (core_draft_rec2.c:604-609) */
    S->diam_fmt = cfgi(11009, 0); S->scale = cfgd(11010, 1.0); S->frac_units = cfgi(11011, 0);
    S->lead0 = cfgi(11012, 0); S->din_chr = cfgi(11013, 0); S->din_tol = cfgi(11014, 0); S->din_val = cfgi(11015, 0);
    S->neg_tol = cfgd(11017, 0); S->digits = cfgi(11018, 0); S->pos_tol = cfgd(11021, 0); S->radius_fmt = cfgi(11022, 0); S->trail0 = cfgi(11023, 0);
    S->text_orient = cfgi(12004, 0); S->tol_h = cfgd(12016, 0.125);
    S->arrow_h = cfgd(14000, 0); S->arrow_w = cfgd(14001, 0); S->arrows_out = cfgi(14002, 0); S->arrow_type = cfgi(14003, 0);
    S->ldr_vis = cfgi(14004, 0); S->ldr_style = cfgi(14005, 0); S->wit_vis = cfgi(14007, 0);
    S->ext = cfgd(14012, 0); S->gap = cfgd(14013, 0);
    dr_assoc = cfgi(15003, 1);
    /* init_attribs_dim's globals (draft_create.h DcDimFont) */
    DcDimFont *g = &dr_font; memset(g, 0, sizeof *g);
    g->aspect = cfgd(12000, 0); g->rot = cfgd(12001, 0); g->height = cfgd(12005, 0); g->extra_sp = cfgd(12006, 0); g->font = cfgi(12007, 0);
    g->angle = cfgd(12012, 0); g->text_bx = cfgi(12013, 0); g->text_path = cfgi(12014, 0); g->slant = cfgd(12015, 0); g->space_sty = cfgi(12022, 0);
    g->extra_ln_sp = cfgd(13000, 0); g->alt_h = 1.0; g->alt_aspect = 1.0;
    memset(S->fa, 0, sizeof S->fa); dc_attribs_dim(S->fa, g);
    S->metric = metric;
    /* main_color / main_level (the wireframe commands' main attributes, cui_geo.c geo_color / geo_level); main_attrib: the line
       attributes this program's records carry (mcam_saveops.c head: width 1.0, styles 1) */
    { double w = 1.0; int16_t one = 1; memcpy(S->attrib, &w, 8); memcpy(S->attrib + 8, &one, 2); memcpy(S->attrib + 10, &one, 2); memcpy(S->attrib + 12, &one, 2); }
    S->ver[0] = 0x13; S->ver[1] = 0x69;                    /* DAT_18143d54a/b: Mastercam 2017's stamp (records.md 1: 17.69 = 0x13 / 0x69 in the
                                                              corpus); the save writes the base part's stamp over it */
}
const DcFonts *dr_fonts_get(void) {               /* the stroke fonts (as scene.c: paths.conf "fonts", else <config>/fonts) */
    if (!dr_fonts_tried) {
        dr_fonts_tried = 1; char d[1100];
        const char *k = site_path("fonts");
        dr_fonts = dc_fonts_load(k[0] ? k : config_file(d, sizeof d, "fonts"));
    }
    return dr_fonts;
}

/* ------------------------------------------------------------------------------------------------ the command state */
int dr_npick;                                    /* DAT_1806960e8: definition points picked (0 / 1) */
int dr_placing;                                  /* placement (DAT_1806496b8 = 0 after FUN_180044d20) */
static uint8_t dr_ap[2][0x98]; static int dr_ent[2];   /* DAT_180698a30 / ac8 (picks), the entity each references (-1) */
static uint8_t dr_P[2][0x98]; int dr_Pent[2];   /* DAT_180692d30 / dc8: P1 / P2 of the placement */
static double dr_Pv[2][3];                              /* DAT_180692fb0 / fc8: P1 / P2 in view coordinates (FUN_180043f20) */
static int dr_orient, dr_locked, dr_hvlock, dr_retest; /* DAT_180649648, DAT_180692ce8, DAT_180692ce7, DAT_180649643 */
static double dr_hvp;                                   /* DfHvpAngle */
uint8_t dr_drag[0x810]; int dr_drag_ok;  /* drag_ent and GetDfDragEntValid */
static DcInput dr_in;                                   /* the inputs of the last computation (the live entity is recomputed from them) */
int dr_live = -1;                                /* F.dnew index of the live entity (DAT_1806925f1), -1 none */
static DcInput dr_live_in;
double dr_view[9];                               /* the active (construction) view: Top here (cui_planes.c: the Cplane stays Top) */
int dr_live_on(void) { return dr_live >= 0; }

int dr_view_no(void) { return 1; }               /* MastercamPlanes::GetActiveViewNo: view 1 (Top) */
static void w2v(const double w[3], double v[3]) { for (int i = 0; i < 3; i++) v[i] = dr_view[3 * i] * w[0] + dr_view[3 * i + 1] * w[1] + dr_view[3 * i + 2] * w[2]; }

/* what the scene draws: the stored entities (the live one in live_entity_color, config 5026), and the drag entity while it is valid
   (df_draw_drag_ent: in its own colour, the main colour) */
void dr_scene(void) {
    if (dr_live >= F.ndnew) dr_live = -1;               /* another document */
    int n = 0;
    for (int i = 0; i < F.ndnew; i++) n += F.dnew[i].on;
    int drag = geo_cmd && dr_cmd(geo_cmd) && !dn_cmd(geo_cmd) && dr_placing && dr_drag_ok;
    uint8_t *b = n + drag ? malloc((size_t)(n + drag) * 0x810) : NULL; int k = 0;
    for (int i = 0; i < F.ndnew; i++) if (F.dnew[i].on) { memcpy(b + (size_t)k * 0x810, F.dnew[i].E, 0x810); if (i == dr_live) b[(size_t)k * 0x810 + 6] = (uint8_t)cfg_live_color; k++; }
    if (drag) memcpy(b + (size_t)k++ * 0x810, dr_drag, 0x810);
    scene_draft_session(b, k, 0x810);
    free(b); overlay_dirty = 1;
}

/* the prompts (create.md 2.2 FUN_180049560 / 2.3 FUN_180043660 / 2.6; lines joined with "\n", mccad 0x18040e028). The placement
   prompt's 27070 "Hold '~' to use another dimension's formatting" is left out: the '~' formatting pick is NOT PORTED */
static void dr_prompt(void) {
    if (dn_cmd(geo_cmd)) return;                        /* cui_draft_note.c keeps its own */
    if (dd_cmd(geo_cmd)) { dd_prompt(); return; }      /* cui_draft_dim.c */
    if (dr_placing) snprintf(cam_msg, sizeof cam_msg, "Create dimension, linear:\nSketch the text position for the linear dimension");   /* 27726 / mccoreres 5886 */
    else if (dr_live >= 0 && !dr_npick) snprintf(cam_msg, sizeof cam_msg, "Modify current dimension or begin creating a new dimension");   /* mccoreres 5887 (cad_rundraft.c:3496-3519) */
    else snprintf(cam_msg, sizeof cam_msg, "Create dimension, linear:\n%s", dr_npick ? "Specify the second endpoint" : "Specify the first endpoint");   /* 27726 / 27701 / 27702 */
}
const char *dr_prompt_text(void) { dr_prompt(); return cam_msg; }

/* FUN_18004b820 (cad_linear.c:904) via FUN_180040ed0: InitDIM(drag_ent), DfHvpAngle = 0, no picks */
static void dr_restart(void) {
    if (dd_cmd(geo_cmd)) { dd_restart(); return; }     /* cui_draft_dim.c */
    dr_npick = 0; dr_placing = 0; dr_drag_ok = 0; dr_hvp = 0; dr_ent[0] = dr_ent[1] = -1;
    dc_init_dim(dr_drag, &dr_S, dr_view_no());
    dr_prompt(); dr_scene();
}

/* the live entity is committed (cad_rundraft.c:3404-3434 / LAB_1800404fa 3253-3278): its colour back to the main colour (it is
   kept in the main colour here, drawn live), DraftGlobalsRestore (panel changes made for it rolled back; Edit Mode Edit, which skips
   it, is not ported), DraftGlobalsSave */
void dr_commit_live(void) {
    if (dr_live < 0) return;
    dr_live = -1; dr_S = dr_saved; dr_saved = dr_S;
    dr_scene();
}

int dr_begin(int c) {
    mcam_std_view(dr_view_no(), dr_view);
    if (!dr_fonts_get()) {                              /* the text extents need the stroke fonts (calc_linear_dimension -> get_ext) */
        cui_message("Drafting", "The drafting commands need Mastercam's stroke fonts (font0.fnt ..) from the user's install: run tools/setup_from_install.py or set \"fonts\" in paths.conf.");
        return 0;
    }
    dr_globals_load();
    dr_saved = dr_S;                                    /* DraftGlobalsSave at the start (cad_rundraft.c:3110-3160 set-up) */
    dr_live = -1;
    if (dn_cmd(c)) { dr_npick = 0; dr_placing = 0; dr_drag_ok = 0; return dn_begin(); }   /* Note: its dialog first (cui_draft_note.c) */
    dr_restart();
    return 1;
}
void dr_end(void) {                                     /* the loop's exit (LAB_1800406f9): the live entity committed, the drag entity erased */
    if (dn_cmd(geo_cmd)) dn_end();                      /* Note: the note being placed is dropped */
    dr_commit_live();
    dr_npick = 0; dr_placing = 0; dr_drag_ok = 0;
    dr_scene();
}

/* ------------------------------------------------------------------------------------------------ placement */
/* FUN_180044d20 (cad_linear.c:2531) + FUN_180044c50 / FUN_180043f20 (cad_linear2.c:198, cad_linear3.c:86): orientation = sub
   (H / V / P), locked (H / V / P buttons), DfHvpAngle 0, P1 / P2 copied and taken to view coordinates, "re-test H/V" set */
static void dr_enter_placement(void) {
    int sub = dr_sub(geo_cmd);
    dr_orient = sub > 2 ? 2 : sub; dr_locked = 1; dr_hvlock = 0; dr_retest = 1; dr_hvp = 0;
    memcpy(dr_P, dr_ap, sizeof dr_P); dr_Pent[0] = dr_ent[0]; dr_Pent[1] = dr_ent[1];
    for (int k = 0; k < 2; k++) { double w[3]; memcpy(w, dr_P[k] + 8, 24); w2v(w, dr_Pv[k]); }
    dc_init_dim(dr_drag, &dr_S, dr_view_no()); dr_drag_ok = 0;
    dr_placing = 1; dr_prompt();
}

/* FUN_180042e50 (cad_linear.c:99-216): one mouse move during placement. T = the sketched point (df_calc_sketched_point: the grid
   snap when Snap to Grid is on, else map_cursor_to_3d - here the cursor on the construction plane), view coordinates; the first move
   after a (re)start runs determine_h_or_v (a P1 -> P2 line that is itself H / V sets that orientation and locks it; else the lock of
   the H / V / P buttons); calc_linear_dimension into the drag entity; invalid -> 27010 and the dimension is abandoned (FUN_180042ca0) */
static int dr_compute(const double cur[3], int ctrl) {
    double tv[3]; w2v(cur, tv);
    if (dr_retest) {
        int hv = dc_line_hv(dr_Pv[0], dr_Pv[1], dr_hvp);   /* determine_h_or_v (core_create.c 3-44) */
        dr_hvlock = hv >= 0; if (hv >= 0) dr_orient = hv;
        dr_locked = 1;                                  /* ce7 set, or DAT_1806a4c20 = 1 for the H / V / P buttons (FUN_180044d20) */
        dr_retest = 0;
    }
    /* not locked: determine_hvp (Smart only - NOT reached for these buttons) */
    memset(&dr_in, 0, sizeof dr_in);
    memcpy(dr_in.ap1, dr_P[0], 0x98); memcpy(dr_in.ap2, dr_P[1], 0x98);
    memcpy(dr_in.cursor, cur, sizeof dr_in.cursor);
    dr_in.view = dr_view_no(); memcpy(dr_in.vm, dr_view, sizeof dr_in.vm);
    dr_in.orient = dr_orient; dr_in.hvp = dr_hvp;
    dr_in.value_mode = 0;                               /* "Apply to linear dimension" (DAT_180692ceb) off: FUN_180044e70 sets it only for two
                                                           points on one arc (never with these picks) */
    dr_in.ctrl = ctrl;
    DcSettings S = dr_S;
    S.sel_dist = SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1);   /* sel_dist in world units (cui_snap.c) */
    S.color = geo_color; S.level = geo_level;           /* main_color / main_level (InitDIM) */
    dc_init_dim(dr_drag, &S, dr_view_no());
    DcResult R = dc_calc_linear(dr_drag, &dr_in, &S, dr_fonts_get());
    if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); dr_drag_ok = 0; return -1; }
    dr_orient = R.orient;                               /* *param_6 as calc left it */
    dr_drag_ok = R.valid;
    if (!R.valid) {
        snprintf(cam_msg, sizeof cam_msg, "Warning - cannot calculate dimension");   /* FUN_18012ae10(0x6982) = 27010 */
        dr_npick = 0; dr_placing = 0; dr_hvp = 0;        /* FUN_180042ca0: drag entity erased, DfHvpAngle 0; the loop restarts */
        dr_scene();
        return 0;
    }
    dr_scene();
    return 1;
}
/* df_calc_sketched_point (core_create.c:447): the grid point when Snap to Grid is on (sel_grid_search: cui_sel.c cui_cursor_world's
   rule), else the cursor on the construction plane */
void dr_sketched(float mx, float my, const double w[3], double out[3]) {
    double gx = w[0], gy = w[1];
    if (grid.snap) {
        double rx = gx, ry = gy; grid_round(&rx, &ry); float sx, sy; int is_near = 1;
        if (grid.snap_mode == 1 && scene_to_screen(vp_w, vp_h, rx, ry, 0, &sx, &sy)) is_near = hypotf(sx + vp_x - mx, sy + vp_y - my) < SEL_APERTURE_PX * 0.5;
        if (is_near) { gx = rx; gy = ry; }
    }
    out[0] = gx; out[1] = gy; out[2] = grid_z();
}
/* the cursor of a mouse move (cui_geo.c geo_cursor_update): WORLD = the cursor on the construction plane, grid-snapped when Snap to
   Grid snapped it (cui_cursor_world) */
int dr_move(const double world[3], int ctrl) {
    if (dn_cmd(geo_cmd)) return dn_move(world, ctrl);   /* cui_draft_note.c */
    if (dd_cmd(geo_cmd)) return dd_move(world, ctrl);   /* cui_draft_dim.c */
    if (!dr_placing) return 0;
    double c[3] = { world[0], world[1], grid_z() };    /* the construction depth (formats/ui.md: a missed click gives it) */
    dr_compute(c, ctrl);
    return 1;
}

/* click (event 4) in placement: FUN_180044030 -> FUN_180043280(1) (cad_linear.c:989-1031): UndoTransStart, df_save_drag_ent
   (store_ent of the drag entity as last computed), UndoTransCommit; the new entity becomes the live one (app+0x5a set by the ribbon
   thunks, cad_rundraft.c:3477) and the command restarts (FUN_180040ed0). The drag entity is computed at the click's position first
   (ours: Mastercam's last mouse-move event is at the same position) */
void dr_store(void) {
    if (!dr_drag_ok) return;
    F.dnew = realloc(F.dnew, sizeof *F.dnew * (size_t)(F.ndnew + 1));
    struct DraftNew *d = &F.dnew[F.ndnew];
    memcpy(d->E, dr_drag, 0x810); d->ent[0] = dr_Pent[0]; d->ent[1] = dr_Pent[1]; d->on = 1;
    /* df_save_drag_ent (core_create.c:548): not associative (FUN_1802efd70) -> DfTurnOffApAssoc + DfRemoveMTID: NOT DECODED -
       the aPoints are written as made (create.md 6) */
    URec r; memset(&r, 0, sizeof r); r.kind = U_DRAFT; r.idx = F.ndnew; undo_push(r);
    dr_live = F.ndnew; dr_live_in = dr_in;
    F.ndnew++; doc_dirty = 1;
}

/* ------------------------------------------------------------------------------------------------ picks */
/* the aPoint of a point event: FUN_1804e3b90 (mccore, the click handler; scratch decompile ev_handlers.c, see create.md 2.2):
   with point events in the get_event mode (bit 1) the AutoCursor point wins (FUN_1804e27d0 -> SetupPointEvent, Ctrl / Alt not
   held); its aPoint is FUN_1804e1bb0's (decomp/mccore_autocursor2.c:651-791): the snap code -> kind, then aPointSaveEntSel(slot 0,
   the snapped entity, t) unless the kind is K / O / V. Codes decoded with their t (mastercam dl_search_fast, decomp/
   mastercam_dlsearch.c:826-886): a point entity 'P' t 0.0, a LINE's start '1' t 0.0 / end '2' t 1.0. The other snaps (arc ends -
   t = the display list's float parameter -, midpoints, centres, quadrants, intersections, origin, grid ...) take a free 'K' point
   at the snapped position here (their t / entity slots NOT DECODED: the dimension is not associative to them) */
void dr_ac_apoint(uint8_t ap[0x98], int *ent, V3 p) {
    double w[3] = { p.x, p.y, p.z };
    *ent = -1;
    int e = geo_snap_feat_ent, f = geo_snap_feat;
    if (dr_assoc && e >= 0 && e < F.nents && F.ents[e].type == E_POINT && geo_snap_kind == GK_POINT) {
        dc_apoint_world(ap, w, 'P'); dc_apoint_ent(ap, 0, (uint64_t)F.ents[e].eptr, 0.0); *ent = e; return; }
    if (dr_assoc && e >= 0 && e < F.nents && F.ents[e].type == E_LINE && geo_snap_kind == GK_END && (f == 0 || f == 1)) {
        dc_apoint_world(ap, w, f ? '2' : '1'); dc_apoint_ent(ap, 0, (uint64_t)F.ents[e].eptr, f ? 1.0 : 0.0); *ent = e; return; }
    dc_apoint_world(ap, w, 'K');
}
/* FUN_180049ee0 (cad_linear.c:615-745): a point event: aPointTestTol, slot n; the second point: both to view coordinates, equal ->
   refused (-1); else FUN_180044d20 (fromLine 0) */
static void dr_point_event(const uint8_t ap[0x98], int ent) {
    memcpy(dr_ap[dr_npick], ap, 0x98); dr_ent[dr_npick] = ent;
    if (dr_npick == 0) { dr_npick = 1; dr_prompt(); return; }
    double a[3], b[3], w[3];
    memcpy(w, dr_ap[0] + 8, 24); w2v(w, a); memcpy(w, dr_ap[1] + 8, 24); w2v(w, b);
    if (fabs(a[0] - b[0]) < 5e-5 && fabs(a[1] - b[1]) < 5e-5 && fabs(a[2] - b[2]) < 5e-5) { dr_prompt(); return; }   /* pt_equal: refused */
    dr_enter_placement();
}
/* FUN_18004a620 case 2 (cad_linear.c:2934-2964): a line as the first pick - refused with 27013 when its ends coincide in the view
   (FUN_18012be10); aPoints '1' / '2' (t 0.0 / 1.0, the line's eptr), 'K' when DfAssoc is off; then FUN_180044d20 (fromLine 1) */
static void dr_line_event(int e) {
    const Ent *en = &F.ents[e];
    double a[3] = { en->a.x, en->a.y, en->a.z }, b[3] = { en->b.x, en->b.y, en->b.z }, av[3], bv[3];
    w2v(a, av); w2v(b, bv);
    if (fabs(av[0] - bv[0]) < 5e-5 && fabs(av[1] - bv[1]) < 5e-5) { snprintf(cam_msg, sizeof cam_msg, "Line is parallel to Z axis."); return; }
    dc_apoint_world(dr_ap[0], a, dr_assoc ? '1' : 'K'); dc_apoint_world(dr_ap[1], b, dr_assoc ? '2' : 'K');
    dr_ent[0] = dr_ent[1] = -1;
    if (dr_assoc) { dc_apoint_ent(dr_ap[0], 0, (uint64_t)en->eptr, 0.0); dc_apoint_ent(dr_ap[1], 0, (uint64_t)en->eptr, 1.0); dr_ent[0] = dr_ent[1] = e; }
    dr_npick = 2; dr_enter_placement();
}
/* the line under the cursor for get_event's entity mask 3 (points, lines; FUN_180049c60 cad_linear.c:2685): select_ent_univ's rule
   (cui_sel.c pick_entity: the nearest within the aperture) over the lines (point entities are reached through the AutoCursor's 'P') */
int dr_pick_line(float mx, float my) {
    int best = -1; double bd = SEL_APERTURE_PX;
    for (int e = 0; e < F.nents; e++) {
        const Ent *en = &F.ents[e]; if (en->type != E_LINE || en->blanked || !ENT_LV_SHOWN(en)) continue;
        float ax, ay, bx, by;
        if (!scene_to_screen(vp_w, vp_h, en->a.x, en->a.y, en->a.z, &ax, &ay) || !scene_to_screen(vp_w, vp_h, en->b.x, en->b.y, en->b.z, &bx, &by)) continue;
        double d = seg_dist(mx, my, ax + vp_x, ay + vp_y, bx + vp_x, by + vp_y);
        if (d < bd) { bd = d; best = e; }
    }
    return best;
}
/* a left click in the view (cui_geo.c geo_click): W = the world point under it on the construction plane. Event kind by
   FUN_1804e3b90: AutoCursor point (mode bit 1) -> point event; else an entity of the mask (mode bit 2; first pick only, get_event
   flags 0xb / 9, create.md 2.2) -> entity event; else (mode bit 1) map_cursor_to_3d -> a 'K' point event. In placement: event 4. */
int dr_click(float mx, float my, const double w[3], int ctrl) {
    if (dn_cmd(geo_cmd)) { if (!dn_click(mx, my, w, ctrl)) geo_end(); return 1; }   /* cui_draft_note.c: 0 = the command ends */
    if (dd_cmd(geo_cmd)) return dd_click(mx, my, w, ctrl);   /* cui_draft_dim.c */
    if (dr_placing) {                                   /* flags 0xc: the sketch event (SetupSketchEvent) = accept: the drag entity as the last
                                                           mouse move computed it; none yet (no move since the second pick): computed at the
                                                           click's sketched point first (ours) */
        if (!dr_drag_ok) { double c[3]; dr_sketched(mx, my, w, c); if (dr_compute(c, ctrl) <= 0) return 1; }
        dr_store(); dr_restart();
        return 1;
    }
    if (dr_live >= 0) dr_commit_live();                 /* a new pick commits the live entity (cad_rundraft.c:3404-3434) */
    V3 p = { w[0], w[1], grid_z() };
    geo_snap_ptr(mx, my); int sn = !ctrl && geo_snap(&p, NULL, -1, 1); geo_snap_ptr_off();   /* FUN_1804e27d0: the AutoCursor (no rubber band) */
    uint8_t ap[0x98]; int ent = -1;
    if (sn) { dr_ac_apoint(ap, &ent, p); dr_point_event(ap, ent); }
    else {
        int e = dr_npick == 0 ? dr_pick_line(mx, my) : -1;
        if (e >= 0) dr_line_event(e);
        else { double q[3] = { w[0], w[1], grid_z() }; dc_apoint_world(ap, q, 'K'); dr_point_event(ap, -1); }
    }
    if (!dr_placing && cam_msg[0] != 'L') dr_prompt();
    dr_scene();
    return 1;
}
/* what geo_need reports: the first pick takes entities too (auto-highlight on: TurnOnAutoHighlight for entity prompts), the second
   a point, the placement a position ('t', no AutoCursor - get_event 0xc with auto-highlight off, create.md 2.3) */
int dr_need(void) { return dn_cmd(geo_cmd) ? dn_need() : dd_cmd(geo_cmd) ? dd_need() : dr_placing ? 't' : dr_npick ? 'p' : 'e'; }   /* 's' (Note):
                                                           the sketched point with the live AutoCursor (cui_geo.c geo_cursor_update) */

/* Esc (get_event 2, cad_rundraft.c:3291-3306): a dimension being dragged (local_7a8 = 2) is cancelled and the command restarts;
   otherwise the command ends (a first pick is dropped, the live entity committed). 1 = handled (the command goes on) */
int dr_esc(void) {
    if (dn_cmd(geo_cmd)) return dn_esc();               /* cui_draft_note.c */
    if (dd_cmd(geo_cmd)) { int r = dd_esc(); if (r >= 0) return r; }   /* cui_draft_dim.c (-1: as here) */
    if (dr_placing) { dr_restart(); return 1; }
    return 0;
}
/* the panel's buttons: codes 3 / 4 end the command, 5 commits and restarts (cad_rundraft.c:3314-3320, 3442-3452); which of the
   FunctionPanel's OK / OK-New / Cancel posts 23 / 24 / 25 is NOT DECODED (create.md 2.5) - here OK and Cancel end the command (3 / 4),
   OK-New commits the live entity and restarts (5). Every exit commits the live entity (LAB_1800404fa) */
int dr_button(int code) {
    if (dn_cmd(geo_cmd)) return dn_button(code);        /* cui_draft_note.c */
    if (dd_cmd(geo_cmd)) { int r = dd_button(code); if (r >= 0) return r; }   /* cui_draft_dim.c (-1: as here) */
    if (code == 1) { dr_commit_live(); dr_restart(); return 1; }
    return 0;                                           /* 0 / 2: geo_end */
}

/* ------------------------------------------------------------------------------------------------ the function panel */
/* DraftingSmartDimensionsManager (mccad.ui draftingsmartdimensionsmanagerview.baml; ids create.md 2.5). Live here: Mode
   _Horizontal / _Vertical (ids 5 / 6, FUN_180043cc0: orientation 0 / 1, DfHvpAngle 0, locked; ignored when the points are H / V
   aligned), Leaders _Inside / _Outside (id 1: _dd_global), _Centered text (id 3: auto_alignment), De_cimal places (id 12:
   num_digits). The rest is shown greyed: NOT DECODED or not ported (create.md 2.5 / 6). A change re-runs the computation of the
   dimension being placed (next mouse move) or of the live one (FUN_180038ed0 -> FUN_1800386f0) */
static int pv_mode, pv_arrows, pv_center, pv_digits, pv_dummy, pv_lock;
static double pv_ang;
void dr_panel_body(const CuiPanelOps *ops, void *u) {
    if (dn_cmd(geo_cmd)) { dn_panel_body(ops, u); return; }   /* cui_draft_note.c: every control disabled */
    if (dd_cmd(geo_cmd)) { dd_panel_body(ops, u); return; }   /* cui_draft_dim.c */
    pv_mode = dr_orient == 1 ? 2 : dr_orient == 0 ? 1 : 0; pv_arrows = dr_S.arrows_out == 1; pv_center = dr_S.auto_align; pv_digits = dr_S.digits;
    pv_lock = dr_locked; pv_ang = dr_hvp;
    int t = -1;                                        /* greyed radio groups whose value is NOT DECODED / not ported: none checked */
    if (!ops->tab || ops->tab(u, 0, "Basic")) {
        if (ops->exp(u, "Entity")) {
            ops->label(u, "Mode:");
            ops->radio(u, "_Auto", &pv_mode, 0, 0); ops->radio(u, "_Horizontal", &pv_mode, 1, 1); ops->radio(u, "_Vertical", &pv_mode, 2, 1);
            ops->num(u, "Orientation an_gle:", "angle", &pv_ang, NULL, 0, -360, 360);
            ops->check(u, "_Maintain perpendicular to arc center", &pv_dummy, 0);
            ops->check(u, "Loc_k", &pv_lock, 0);
            ops->end(u);
        }
        if (ops->exp(u, "Arc Symbol")) { t = -1; ops->radio(u, "Radi_us", &t, 0, 0); ops->radio(u, "_Diameter", &t, 1, 0); ops->check(u, "A_pply to linear dimension", &pv_dummy, 0); ops->end(u); }
        if (ops->exp(u, "_Witness lines")) { t = -1; ops->radio(u, "_Both", &t, 0, 0); ops->radio(u, "_Right", &t, 1, 0); ops->radio(u, "_Left", &t, 2, 0); ops->end(u); }
        if (ops->exp(u, "Leaders")) { ops->label(u, "Arrows:"); ops->radio(u, "_Inside", &pv_arrows, 0, 1); ops->radio(u, "_Outside", &pv_arrows, 1, 1); ops->end(u); }
        if (ops->exp(u, "Format")) { ops->toggle(u, "_Font", 1, -1); ops->toggle(u, "_Edit text", 1, -1); ops->toggle(u, "Heigh_t", 1, -1);
            ops->inum(u, "De_cimal places:", "digits", &pv_digits, NULL, 1, 0, 8); ops->end(u); }
    }
    if (!ops->tab || ops->tab(u, 1, "Advanced")) {
        if (ops->exp(u, "Dimension Text")) { ops->check(u, "_Centered text", &pv_center, 1); ops->label(u, "Edit Mode:");
            t = -1; ops->radio(u, "P_lace", &t, 0, 0); ops->radio(u, "_Edit", &t, 1, 0); ops->end(u); }
        if (ops->exp(u, "Dimension Box")) { ops->check(u, "_Show when dragging", &pv_dummy, 0); ops->end(u); }
        if (ops->exp(u, "Settings")) { ops->toggle(u, "_Options", 1, -1); ops->toggle(u, "_Update", 1, -1); ops->end(u); }
    }
}
const char *const *dr_panel_tabs(int *n) { static const char *const two[2] = { "Basic", "Advanced" }; *n = 2; return two; }
/* the panel values back into the globals (the builder edited the pv_* copies): FUN_18012af20 cases 1 / 3 / 12, FUN_180043cc0 5 / 6 */
void dr_panel_sync(void) {
    if (dn_cmd(geo_cmd)) return;                        /* Note: no live control */
    if (dd_cmd(geo_cmd)) { dd_panel_sync(); return; }   /* cui_draft_dim.c */
    int ch = 0;
    if ((pv_arrows ? 1 : 0) != (dr_S.arrows_out == 1)) { dr_S.arrows_out = pv_arrows ? 1 : 0; ch = 1; }
    if (pv_center != dr_S.auto_align) { dr_S.auto_align = pv_center; ch = 1; }
    if (pv_digits != dr_S.digits && pv_digits >= 0 && pv_digits <= 8) { dr_S.digits = pv_digits; ch = 1; }
    int want = pv_mode == 1 ? 0 : pv_mode == 2 ? 1 : -1;
    if (want >= 0 && want != dr_orient && dr_placing && !dr_hvlock && fabs(dr_hvp) < 5e-5) { dr_orient = want; dr_hvp = 0; dr_locked = 1; ch = 1; }
    if (!ch) return;
    if (dr_live >= 0 && !dr_placing && !dr_npick) {     /* FUN_180038ed0 -> FUN_1800386f0: the live entity recomputed */
        DcSettings S = dr_S; S.sel_dist = SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1); S.color = F.dnew[dr_live].E[6]; S.level = (int)F.dnew[dr_live].E[8];
        uint8_t E[0x810]; dc_init_dim(E, &S, dr_view_no());
        DcResult R = dc_calc_linear(E, &dr_live_in, &S, dr_fonts_get());
        if (R.valid) memcpy(F.dnew[dr_live].E, E, 0x810);
        dr_scene();
    }
}

/* ------------------------------------------------------------------------------------------------ undo, project file */
void dr_undo(int idx, int undo) {                       /* U_DRAFT: the dimension store_ent made (one UndoTrans) */
    if (idx < 0 || idx >= F.ndnew) return;
    F.dnew[idx].on = !undo;
    if (undo && idx == dr_live) dr_live = -1;
    dr_scene();
}
/* ours: the project file keeps the session's drafting entities - DIM ENT0 ENT1 HEX(record) */
void dr_write(FILE *f) {
    for (int i = 0; loaded && i < F.ndnew; i++) {
        if (!F.dnew[i].on) continue;
        fprintf(f, "DIM %d %d ", F.dnew[i].ent[0], F.dnew[i].ent[1]);
        for (int k = 0; k < 0x810; k++) fprintf(f, "%02x", F.dnew[i].E[k]);
        fputc('\n', f);
    }
}
int dr_read_line(const char *ln) {
    if (strncmp(ln, "DIM ", 4)) return 0;
    int e0 = -1, e1 = -1, n = 0;
    if (sscanf(ln + 4, "%d %d %n", &e0, &e1, &n) < 2) return 1;
    if (e0 >= 0) e0 = cam_load_ent_remap(e0);           /* an older project's indices (cam2d.h); ent[1] <= -2 is an ordinate base, not an entity */
    if (e1 >= 0) e1 = cam_load_ent_remap(e1);
    const char *h = ln + 4 + n; if (strlen(h) < 2 * 0x810) return 1;
    F.dnew = realloc(F.dnew, sizeof *F.dnew * (size_t)(F.ndnew + 1));
    struct DraftNew *d = &F.dnew[F.ndnew++];
    for (int k = 0; k < 0x810; k++) { unsigned v = 0; sscanf(h + 2 * k, "%2x", &v); d->E[k] = (uint8_t)v; }
    d->ent[0] = e0 < F.nents ? e0 : -1; d->ent[1] = e1 < F.nents ? e1 : -1; d->on = 1;
    return 1;
}

/* ------------------------------------------------------------------------------------------------ tests (ours) */
static double D8(const uint8_t *b, int o) { double v; memcpy(&v, b + o, 8); return v; }
static int I16_(const uint8_t *b, int o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static void dump_rec(const char *tag, const uint8_t *E) {
    char t[0x60]; memcpy(t, E + 0x650, 0x58); t[0x58] = 0;
    uint64_t e1, e2; memcpy(&e1, E + 0x70 + 0x28, 8); memcpy(&e2, E + 0x108 + 0x28, 8);
    printf("    %s: type %d orient %d hvp %.4f col %d lev %d view %d P1 '%c' (%.4f,%.4f,%.4f)%s t %.1f P2 '%c' (%.4f,%.4f,%.4f)%s t %.1f value %.6f text '%s' at (%.4f,%.4f) angle %.4f h %.4f\n",
           tag, E[0x308], E[0x68], D8(E, 0x1a0), E[6], (int)E[8], I16_(E, 0x328), E[0x70] ? E[0x70] : '?', D8(E, 0x78), D8(E, 0x80), D8(E, 0x88), e1 ? " eptr" : "", D8(E, 0x70 + 0x30),
           E[0x108] ? E[0x108] : '?', D8(E, 0x110), D8(E, 0x118), D8(E, 0x120), e2 ? " eptr" : "", D8(E, 0x108 + 0x30), D8(E, 0x338), t, D8(E, 0x638), D8(E, 0x640), D8(E, 0x5e8), D8(E, 0x600));
    static const char *wn[4] = { "wit1", "ldr1", "ldr2", "wit2" }; static const int wo[4] = { 0x378, 0x4a8, 0x540, 0x410 };
    for (int k = 0; k < 4; k++) {
        const uint8_t *w = E + wo[k]; int n = I16_(w, 0x1a); if (n > 4) n = 4;
        printf("      %s on %d vis %d arrow %d type %d n %d:", wn[k], w[0], w[3], w[4], w[0x18], I16_(w, 0x1a));
        for (int i = 0; i < n; i++) printf(" (%.4f,%.4f)", D8(w, 0x20 + 0x18 * i), D8(w, 0x28 + 0x18 * i));
        printf("\n");
    }
    printf("      placement (%.4f,%.4f) offsets (%.4f,%.4f) anchor %d gap %.4f ext %.4f arrows %d wv %d lv %d ls %d digits %d auto %d\n", D8(E, 0x310), D8(E, 0x318), D8(E, 0x360), D8(E, 0x368), E[0x1a8],
           D8(E, 0x350), D8(E, 0x358), I16_(E, 0x7b0), I16_(E, 0x7ac), I16_(E, 0x7ae), I16_(E, 0x7b4), E[0x7cd], E[0x7c9]);
}
void dr_dump(void) {
    printf("    draft cmd '%s' picks %d placing %d live %d orient %d drag %d dims %d\n", dr_cmd(geo_cmd) ? dr_label(geo_cmd) : "-", dr_npick, dr_placing, dr_live, dr_orient, dr_drag_ok, F.ndnew);
    for (int i = 0; i < F.ndnew; i++) { char tag[32]; snprintf(tag, sizeof tag, "dim %d%s%s", i, F.dnew[i].on ? "" : " (undone)", i == dr_live ? " (live)" : ""); dump_rec(tag, F.dnew[i].E);
        printf("      refs %d %d\n", F.dnew[i].ent[0], F.dnew[i].ent[1]); }
    if (dr_placing && dr_drag_ok) dump_rec("drag", dr_drag);
}
/* the open part file's linear dimension records (draft.c draft_load: the record walk with the out-of-band blocks) */
static const uint8_t *file_lin(DraftDoc *dd, int want, int *count) {
    int k = 0; const uint8_t *hit = NULL; uint32_t n;
    for (int i = 0; i < draft_count(dd); i++) { const uint8_t *E = draft_record(dd, i, &n);
        if (E && n == 0x810 && E[0] == 0x40 && E[0x308] == 0) { if (k == want) hit = E; k++; } }
    if (count) *count = k;
    return hit;
}
static void dump_file(const char *path) {
    printf("    %s\n", draft_msg[0] ? draft_msg : "drafting: none");
    DraftDoc *dd = draft_load(path); int n = 0; file_lin(dd, -1, &n);
    for (int k = 0; k < n; k++) { char tag[32]; snprintf(tag, sizeof tag, "file dim %d", k); dump_rec(tag, file_lin(dd, k, NULL)); }
    draft_free(dd);
}
/* dimglobals:N - the session globals from the open part's linear dimension N: DraftGlobalsFromDim @180308cc0 (core_draftwf.c
   540-700), the mapping the corpus oracle uses (tools/tests/draft_create_test.c run) - what Mastercam's '~' + pick of that dimension
   does (ToDimGlobalsFromDraft, create.md 2.5); ours as a step (the '~' pick is not ported) */
static int dr_globals_from(const uint8_t *Es) {
    DcSettings *S = &dr_S;
    memcpy(S->fa, Es + 0x5d8, 0x60);
    const uint8_t *l1 = Es + 0x4a8, *l2 = Es + 0x540, *l = l1[0] && l1[0x18] != 3 ? l1 : l2[0] && l2[0x18] != 3 ? l2 : l1[0] ? l1 : l2;
    S->arrow_h = D8(l, 8); S->arrow_w = D8(l, 0x10); S->arrow_type = l[0x18];
    S->arrows_out = I16_(Es, 0x7b0); S->ldr_style = I16_(Es, 0x7b4); S->wit_vis = I16_(Es, 0x7ac); S->ldr_vis = I16_(Es, 0x7ae);
    S->text_orient = fabs(D8(Es, 0x5e8)) < 5e-5; S->auto_align = Es[0x7c9];
    S->gap = D8(Es, 0x350); S->ext = D8(Es, 0x358); S->scale = D8(Es, 0x330);
    S->coord_fmt = I16_(Es, 0x7c0); S->frac_units = I16_(Es, 0x7c2); S->digits = Es[0x7cd]; S->lead0 = Es[0x7cc]; S->trail0 = Es[0x7cf]; S->comma = Es[0x7cb];
    S->diam_fmt = S->radius_fmt = I16_(Es, 0x7b2); S->tol_mode = I16_(Es, 0x7b6) == 3 ? 1 : I16_(Es, 0x7b6) > 3 ? 0 : I16_(Es, 0x7b6);
    S->tol_h = D8(Es, 0x6a8 + 0x28); S->pos_tol = D8(Es, 0x340); S->neg_tol = D8(Es, 0x348);
    S->din_tol = Es[0x7c6]; S->din_chr = Es[0x7c7]; S->din_val = Es[0x7c8]; S->metric = I16_(Es, 0x7ba) == 1;
    dr_saved = dr_S;
    return 1;
}
/* dimcmp:N - the last dimension made here against the open part's linear dimension N: the oracle's fields (draft_create_test.c
   compare) + the aPoints (kind, world point, entity slot 0 eptr / t) */
static void dr_cmp(int want) {
    DraftDoc *dd = draft_load(F.path); int n = 0; const uint8_t *Es = file_lin(dd, want, &n);
    if (!Es || !F.ndnew) { printf("    dimcmp: nothing to compare (%d file dims, %d made)\n", n, F.ndnew); draft_free(dd); return; }
    const uint8_t *Eo = F.dnew[F.ndnew - 1].E; int nd = 0; char why[2000] = ""; size_t L = 0;
#define DF(...) do { nd++; L += (size_t)snprintf(why + L, L < sizeof why ? sizeof why - L : 0, __VA_ARGS__); } while (0)
#define CLOSE(a, b) (fabs((a) - (b)) <= 1e-6 * (1 + fabs(b)))
    int nref = 0; char refw[400] = ""; size_t RL = 0;          /* aPoint references (kind / entity / t) at the same world point: counted apart */
    for (int j = 0; j < 2; j++) { const uint8_t *a = Eo + 0x70 + 0x98 * j, *b = Es + 0x70 + 0x98 * j;
        uint64_t ea, eb; memcpy(&ea, a + 0x28, 8); memcpy(&eb, b + 0x28, 8);
        if (!CLOSE(D8(a, 8), D8(b, 8)) || !CLOSE(D8(a, 0x10), D8(b, 0x10))) DF(" aP%d position;", j + 1);
        else if (a[0] != b[0] || ea != eb || !CLOSE(D8(a, 0x30), D8(b, 0x30)) || !CLOSE(D8(a, 0x18), D8(b, 0x18))) { nref++;   /* also: another entity at the same Top-view point (z) */
            RL += (size_t)snprintf(refw + RL, RL < sizeof refw ? sizeof refw - RL : 0, " aP%d '%c'/%llx/%.3f z %.4f vs '%c'/%llx/%.3f z %.4f;", j + 1, a[0], (unsigned long long)ea, D8(a, 0x30), D8(a, 0x18), b[0], (unsigned long long)eb, D8(b, 0x30), D8(b, 0x18)); } }
    if (Eo[0x68] != Es[0x68]) DF(" orient %d/%d;", Eo[0x68], Es[0x68]);
    if (!CLOSE(D8(Eo, 0x1a0), D8(Es, 0x1a0))) DF(" hvp;");
    if (!CLOSE(D8(Eo, 0x338), D8(Es, 0x338))) DF(" value %.6f/%.6f;", D8(Eo, 0x338), D8(Es, 0x338));
    if (strncmp((const char *)Eo + 0x650, (const char *)Es + 0x650, 0x58)) DF(" text '%.20s'/'%.20s';", Eo + 0x650, Es + 0x650);
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x638 + 8 * k), D8(Es, 0x638 + 8 * k))) { DF(" textpos (%.5f,%.5f)/(%.5f,%.5f);", D8(Eo, 0x638), D8(Eo, 0x640), D8(Es, 0x638), D8(Es, 0x640)); break; }
    if (!CLOSE(D8(Eo, 0x5e8), D8(Es, 0x5e8))) DF(" textangle;");
    static const int wo[4] = { 0x378, 0x410, 0x4a8, 0x540 }; static const char *wn[4] = { "wit1", "wit2", "ldr1", "ldr2" };
    for (int q = 0; q < 4; q++) { const uint8_t *a = Eo + wo[q], *b = Es + wo[q]; int bad = a[0] != b[0] || a[1] != b[1] || a[3] != b[3] || a[4] != b[4] || a[0x18] != b[0x18] || I16_(a, 0x1a) != I16_(b, 0x1a)
            || !CLOSE(D8(a, 8), D8(b, 8)) || !CLOSE(D8(a, 0x10), D8(b, 0x10));
        int np = I16_(b, 0x1a); if (np > 4) np = 4;
        for (int i = 0; !bad && i < np * 3; i++) bad = !CLOSE(D8(a, 0x20 + 8 * i), D8(b, 0x20 + 8 * i));
        if (bad) DF(" %s;", wn[q]); }
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x310 + 8 * k), D8(Es, 0x310 + 8 * k))) { DF(" placement;"); break; }
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x360 + 8 * k), D8(Es, 0x360 + 8 * k))) { DF(" offsets (%.5f,%.5f)/(%.5f,%.5f);", D8(Eo, 0x360), D8(Eo, 0x368), D8(Es, 0x360), D8(Es, 0x368)); break; }
    if (Eo[0x1a8] != Es[0x1a8]) DF(" anchor %d/%d;", Eo[0x1a8], Es[0x1a8]);
#undef DF
#undef CLOSE
    printf("    dimcmp %d: %s%s%s%s\n", want, nd ? "DIFFER" : nref ? "EQUAL-LAYOUT" : "EQUAL", why, nref ? " refs:" : "", refw);
    draft_free(dd);
}
/* dimdrag:X,Y (a mouse move at world X,Y during placement), dimdump, dimfile (the open part's records), dimpanel:K=V (a panel value:
   arrows / center / digits / mode), dimglobals:N / dimcmp:N (the open part's linear dimension N: its format as the globals / compared
   with the last dimension made here) */
int dr_script(const char *st, const char *arg) {
    if (dd_script(st, arg)) return 1;                   /* cui_draft_dim.c: circ* steps */
    if (dn_script(st, arg)) return 1;                   /* cui_draft_note.c: note* steps */
    if (!strcmp(st, "dimdrag") && arg) { double x = 0, y = 0; sscanf(arg, "%lf,%lf", &x, &y); double w[3] = { x, y, 0 }; if (!dr_move(w, 0)) printf("    (no placement)\n"); return 1; }
    if (!strcmp(st, "dimdump")) { dr_dump(); return 1; }
    if (!strcmp(st, "dimfile")) { if (loaded && F.path[0]) dump_file(F.path); else printf("    (no part file)\n"); return 1; }
    if (!strcmp(st, "dimglobals") && arg) { DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *E = file_lin(dd, atoi(arg), NULL);
        if (E) { dr_globals_load(); dr_globals_from(E); } else printf("    (no linear dimension %s)\n", arg); draft_free(dd); return 1; }
    if (!strcmp(st, "dimredo") && arg) {               /* dimredo:N - a placement mouse move at the cursor Mastercam's regeneration gives the open
                                                           part's linear dimension N (FUN_1803049e0, core_dimlayout.c 2980: the anchor definition point
                                                           + the text offsets rotated by calc_linear_angle; the corpus oracle's cursor) */
        DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *Es = file_lin(dd, atoi(arg), NULL);
        if (!Es || !dr_placing) printf("    (dimredo: %s)\n", Es ? "no placement" : "no such dimension");
        else {
            double vm[9]; mcam_std_view(I16_(Es, 0x328) ? I16_(Es, 0x328) : 1, vm);
            double a = dc_linear_angle(Es + 0x70, Es + 0x108, vm, Es[0x68], D8(Es, 0x1a0)) * 0.01745329251994329, o[2] = { D8(Es, 0x360), D8(Es, 0x368) }, ro[2];
            if (fabs(a) >= 5e-5 * 0.01745329251994329) { ro[0] = o[0] * cos(a) - o[1] * sin(a); ro[1] = o[1] * cos(a) + o[0] * sin(a); } else { ro[0] = o[0]; ro[1] = o[1]; }
            const uint8_t *ap = Es[0x1a8] != 1 ? Es + 0x108 : Es + 0x70; double w[3], av[3], cv[3], cw[3]; memcpy(w, ap + 8, 24);
            for (int i = 0; i < 3; i++) av[i] = vm[3 * i] * w[0] + vm[3 * i + 1] * w[1] + vm[3 * i + 2] * w[2];
            cv[0] = av[0] + ro[0]; cv[1] = av[1] + ro[1]; cv[2] = D8(Es, 0x370);
            for (int i = 0; i < 3; i++) cw[i] = vm[i] * cv[0] + vm[3 + i] * cv[1] + vm[6 + i] * cv[2];
            dr_compute(cw, 0);
        }
        draft_free(dd); return 1;
    }
    if (!strcmp(st, "dimcmp") && arg) { if (loaded && F.path[0]) dr_cmp(atoi(arg)); else printf("    (no part file)\n"); return 1; }
    if (!strcmp(st, "dimpanel") && arg) {
        char k[32] = ""; const char *eq = strchr(arg, '='); int v = eq ? atoi(eq + 1) : 0;
        snprintf(k, sizeof k, "%.*s", eq ? (int)(eq - arg) : (int)strlen(arg), arg);
        pv_mode = dr_orient == 1 ? 2 : dr_orient == 0 ? 1 : 0; pv_arrows = dr_S.arrows_out == 1; pv_center = dr_S.auto_align; pv_digits = dr_S.digits;
        if (!strcmp(k, "arrows")) pv_arrows = v; else if (!strcmp(k, "center")) pv_center = v; else if (!strcmp(k, "digits")) pv_digits = v; else if (!strcmp(k, "mode")) pv_mode = v;
        else printf("    (unknown panel value %s)\n", k);
        dr_panel_sync(); return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ for the Ordinate commands */
/* the session's drafting globals / stroke fonts / DfAssoc, shared with cui_draft_ord.c (Mastercam's globals are one set) */
DcSettings *dr_globals(void) { dr_globals_load(); return &dr_S; }
const DcFonts *dr_stroke_fonts(void) { return dr_fonts_get(); }
int dr_assoc_on(void) { return dr_assoc; }
