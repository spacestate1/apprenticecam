/* cui_draft_dim.c - the Drafting tab's Dimension-group commands beyond Horizontal / Vertical / Parallel (cui_draft.c), run in the same
 * command loop (mccad FUN_18003f7f0) and the same wireframe command frame (cui_geo.c). Here: Circular (doDraft_DIMENSION_CIRCULAR,
 * 0x9475): the arc pick, the placement drag (FUN_18004bd60: determine_cl, the leader-mode bookkeeping, calc_circular_dimension =
 * viewer/draft_create_circ.c), accept / store (FUN_18004cd20 -> FUN_18004c170), its function-panel controls (FUN_18004c840 /
 * FUN_18004c950). Spec: ../../formats/drafting/create.md 8; decompiles decomp/draft/ (cad_*.c, circ_cad.c = this command's dumps).
 * "ours" marks glue of this program. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "cui_draft_int.h"
#include "draft_create_circ.h"
#include "draft.h"
#include "scene.h"

#define TWOPI_ 6.283185307179586

int dd_cmd(int c) { return c == G_DIMC || c == G_DIMA || c == G_DIMB || c == G_DIMCH || c == G_DIMT || c == G_DIMPP || c == G_DIMPT; }
const char *dd_label(int c) { return c == G_DIMC ? "Circular" : c == G_DIMA ? "Angular" : c == G_DIMB ? "Baseline" : c == G_DIMCH ? "Chained" : c == G_DIMT ? "Tangent" : c == G_DIMPP ? "Perpendicular" : c == G_DIMPT ? "Point" : ""; }   /* the ribbon labels (ui/layout.txt l.297 / 299) */
int dd_find_ft(const char *ft) { return !ft ? 0 : !strcmp(ft, "doDraft_DIMENSION_CIRCULAR") ? G_DIMC : !strcmp(ft, "doDraft_DIMENSION_ANGULAR") ? G_DIMA :
    !strcmp(ft, "doDraft_DIMENSION_BASELINE") ? G_DIMB : !strcmp(ft, "doDraft_DIMENSION_CHAINED") ? G_DIMCH : !strcmp(ft, "doDraft_DIMENSION_TANGENT") ? G_DIMT : !strcmp(ft, "doDraft_DIMENSION_PERPENDICULAR") ? G_DIMPP : !strcmp(ft, "doDraft_DIMENSION_POINT") ? G_DIMPT : 0; }

/* ------------------------------------------------------------------------------------------------ the state (mccad globals) */
static int dd_arc = -1;                                 /* the picked arc (DAT_180698cd0 = its copy), F.ents index */
static DcCircInput dd_in;                               /* the calc_circular_dimension inputs of the last move */
static int dd_diam;                                     /* DAT_180698cc4: 1 diameter / 0 radius (FUN_18004d250: the arc is a full circle) */
static int dd_style = 2, dd_quad;                       /* DAT_1806496cc (*param_4), DAT_180698cc8 (quadrant code) */
static int dd_lock;                                     /* DAT_1806a4c20 (panel Loc_k, id 7): determine_cl not re-run */
static int dd_first;                                    /* DAT_1806496dc: the first move after the pick */
static int f_dd, f_de, f_df, f_e0, f_e4, f_e8;          /* DAT_1806496dd / de / df / e0 / e4 / e8: the leader-mode bookkeeping */
static DcCircInput dd_live_in;                          /* the inputs of the live entity (recomputed by panel changes, as cui_draft.c) */
static int dd_live_ok;

static double sel_dist_w(void) { return SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1); }   /* as cui_draft.c dr_compute */

/* prompts. FUN_180049560 (cad_linear.c:359-585) type 2: 27706 "Create dimension, circular: " + 27066 "Select an arc to create a circular
   dimension"; placement FUN_18004c430 (circ_cad.c): 27054 "Sketch the text position for the dimension" (+ 27070 "Hold '~' ...": left
   out, the '~' formatting pick is NOT PORTED - as cui_draft.c); the live entity: mccoreres 5887 (cad_rundraft.c:3496-3519) */
void dd_prompt(void) {
    if (geo_cmd == G_DIMPT) { dp4_prompt(); return; }   /* cui_draft_pt.c */
    if (geo_cmd == G_DIMT || geo_cmd == G_DIMPP) { dt_prompt(); return; }   /* cui_draft_tan.c */
    if (geo_cmd == G_DIMA) { da_prompt(); return; }    /* cui_draft_ang.c */
    if (geo_cmd == G_DIMB || geo_cmd == G_DIMCH) { db_prompt(); return; }   /* cui_draft_base.c */
    if (dr_placing) snprintf(cam_msg, sizeof cam_msg, "Sketch the text position for the dimension");
    else if (dr_live >= 0) snprintf(cam_msg, sizeof cam_msg, "Modify current dimension or begin creating a new dimension");
    else snprintf(cam_msg, sizeof cam_msg, "Create dimension, circular: \nSelect an arc to create a circular dimension");
}
/* FUN_180048b10(2, -1) -> FUN_18004b820 (cad_linear.c:904): InitDIM(drag_ent), DfHvpAngle = 0, no picks; FUN_180049c60 type 2: get_event
   flags 10, mask 4 (arcs only) */
void dd_restart(void) {
    if (geo_cmd == G_DIMPT) { dp4_restart(); return; }   /* cui_draft_pt.c */
    if (geo_cmd == G_DIMT || geo_cmd == G_DIMPP) { dt_restart(); return; }   /* cui_draft_tan.c */
    if (geo_cmd == G_DIMA) { da_restart(); return; }
    if (geo_cmd == G_DIMB || geo_cmd == G_DIMCH) { db_restart(); return; }
    dr_npick = 0; dr_placing = 0; dr_drag_ok = 0; dd_arc = -1; dr_Pent[0] = dr_Pent[1] = -1;
    dc_init_dim(dr_drag, &dr_S, dr_view_no());
    dd_prompt(); dr_scene();
}
int dd_need(void) { if (geo_cmd == G_DIMPT) return dp4_need(); if (geo_cmd == G_DIMT || geo_cmd == G_DIMPP) return dt_need(); return geo_cmd == G_DIMA ? da_need() : geo_cmd == G_DIMB || geo_cmd == G_DIMCH ? db_need() : dr_placing ? 't' : 'e'; }

/* the arc under the cursor (get_event mask 4; pick_entity's rule, cui_sel.c, over the arcs) */
int dd_pick_arc_any(float mx, float my) {
    int best = -1; double bd = SEL_APERTURE_PX;
    for (int e = 0; e < F.nents; e++) {
        const Ent *en = &F.ents[e]; if (en->type != E_ARC || en->blanked || !ENT_LV_SHOWN(en)) continue;
        for (int pi = en->first_poly; pi < en->first_poly + en->npoly; pi++) {
            const Poly *p = &F.polys[pi]; float ax = 0, ay = 0;
            for (int k = 0; k < p->count; k++) {
                V3 q = F.pts[p->first + k]; float sx, sy;
                if (!scene_to_screen(vp_w, vp_h, q.x, q.y, q.z, &sx, &sy)) continue;
                sx += vp_x; sy += vp_y;
                if (k) { double d = seg_dist(mx, my, ax, ay, sx, sy); if (d < bd) { bd = d; best = e; } }
                ax = sx; ay = sy;
            }
        }
    }
    return best;
}

/* the arc entity as calc_circular_dimension reads it (A_ID +0x98 centre in its view, +0xb0 r, +0xb8 / +0xc0 start / sweep, +0xc8 view):
   an XY arc (Ent vw 0) is a Top-view arc (view coordinates = world). eptr: Mastercam's arc always has one; a new arc gets its eptr at
   the save (mcam_saveops.c) - ours passes 1 meanwhile so the aPoint is the associative 'C' (the save writes the real one) */
static void dd_arc_in(int e, DcCircInput *in) {
    const Ent *en = &F.ents[e];
    memset(in, 0, sizeof *in);
    in->centre[0] = en->c.x; in->centre[1] = en->c.y; in->centre[2] = en->c.z; in->r = en->r; in->start = en->a0; in->sweep = en->sw;
    in->arc_view = 1; in->eptr = en->eptr ? (uint64_t)en->eptr : 1; in->view = dr_view_no(); memcpy(in->vm, dr_view, sizeof in->vm);
    in->assoc = dr_assoc;
}

/* FUN_18004a620 case 4 (cad_linear.c:2964-3083) for type 2 + FUN_18004d250 / FUN_18004d300 / FUN_18004d750 (circ_cad.c) */
static void dd_arc_event(int e) {
    const Ent *en = &F.ents[e];
    if (en->vw || !en->planar) {                        /* views_parallel(arc view, active view) */
        if (!en->vw) { snprintf(cam_msg, sizeof cam_msg, "Warning - arc is not parallel to construction plane."); return; }   /* 27012 */
        /* an arc of another view: parallel (Bottom) -> map_arc_to_parallel_view, else constr_view_3d -> the construction plane becomes
           the arc's view: both NOT PORTED */
        snprintf(cam_msg, sizeof cam_msg, "Warning - arc is not parallel to construction plane."); return;
    }
    dd_diam = fabs(fabs(en->sw) - TWOPI_) < 1e-6;      /* FUN_18004d250: AbsAngleTheSame(sweep, 2 pi) (param_2 = 8: kept) */
    f_dd = f_df = f_e0 = f_de = 1; f_e4 = f_e8 = 3;     /* FUN_18004d750 */
    dc_init_dim(dr_drag, &dr_S, dr_view_no());          /* InitDIM(drag_ent) */
    f_e8 = dr_S.ldr_vis; f_df = 0;                       /* DAT_1806496e8 = dd_global+8, df = 0 */
    dd_lock = 0; dd_style = 2; dd_first = 1; dd_quad = 0; /* a4c20 = 0, cc = 2, dc = 1, cc8 = 0; DfHvpAngle = 0 (param_4 = 0) */
    dd_arc = e; dr_Pent[0] = e; dr_Pent[1] = -1;
    dr_npick = 1; dr_placing = 1; dr_drag_ok = 0;       /* FUN_18004cb70: df_phantom_on of the arc (the phantom display: NOT PORTED) */
    dd_prompt();
}

/* FUN_18004bd60 (circ_cad.c): one mouse move of the placement */
static int dd_compute(const double cur[3], int ctrl) {
    double tv[3]; for (int i = 0; i < 3; i++) tv[i] = dr_view[3 * i] * cur[0] + dr_view[3 * i + 1] * cur[1] + dr_view[3 * i + 2] * cur[2];
    dd_arc_in(dd_arc, &dd_in);
    if (!dd_lock || dd_first) dd_style = dc_determine_cl(&dd_in, tv, 0.0, dd_diam, &dd_quad);
    if (dd_style == 2) {                                /* the leader mode for the radial style */
        if (dr_S.ldr_vis < 1) dr_S.ldr_vis = 0;
        else if (dr_S.arrows_out == 1) dr_S.ldr_vis = f_dd ? 1 : f_e0;
        else dr_S.ldr_vis = f_de ? 3 : f_e4;
    } else dr_S.ldr_vis = f_df ? 3 : f_e8;
    memcpy(dd_in.cursor, cur, sizeof dd_in.cursor);
    dd_in.diameter = dd_diam; dd_in.style = dd_style; dd_in.quad = dd_quad; dd_in.hvp = 0.0; dd_in.ctrl = ctrl;
    DcSettings S = dr_S; S.sel_dist = sel_dist_w(); S.color = geo_color; S.level = geo_level;   /* main_color / main_level */
    DcResult R = dc_calc_circular(dr_drag, &dd_in, &S, dr_fonts_get());
    if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); dr_drag_ok = 0; dr_scene(); return -1; }
    dd_style = R.style;                                 /* a failed quadrant style comes back radial (*param_4 = 2) */
    dr_drag_ok = R.valid;
    if (!R.valid) {                                     /* FUN_18012ae10(0x6982) + FUN_18004bbf0: the dimension abandoned */
        snprintf(cam_msg, sizeof cam_msg, "Warning - cannot calculate dimension");   /* 27010 */
        dr_npick = 0; dr_placing = 0; dd_arc = -1; dr_scene();
        return 0;
    }
    dd_first = 0;
    dr_scene();
    return 1;
}
int dd_move(const double world[3], int ctrl) {
    if (geo_cmd == G_DIMPT) return dp4_move(world, ctrl);   /* cui_draft_pt.c */
    if (geo_cmd == G_DIMT || geo_cmd == G_DIMPP) return dt_move(world, ctrl);   /* cui_draft_tan.c */
    if (geo_cmd == G_DIMA) return da_move(world, ctrl);
    if (geo_cmd == G_DIMB || geo_cmd == G_DIMCH) return db_move(world, ctrl);   /* the drag-edit of a new baseline dimension */
    if (!dr_placing || dd_arc < 0) return 0;
    double c[3] = { world[0], world[1], grid_z() };
    dd_compute(c, ctrl);
    return 1;
}

/* a left click. Placement: event 4 -> FUN_18004cd20 -> FUN_18004c170(1) (circ_cad.c): the leader mode back (dd_global+8 =
   DAT_1806496e8), UndoTransStart, df_save_drag_ent, UndoTransCommit; then the command restarts (as cui_draft.c dr_click / dr_store:
   the stored dimension is the live one). Otherwise the arc pick (get_event 10 / mask 4: entity events only, no point events) */
int dd_click(float mx, float my, const double w[3], int ctrl) {
    if (geo_cmd == G_DIMPT) return dp4_click(mx, my, w, ctrl);   /* cui_draft_pt.c */
    if (geo_cmd == G_DIMT || geo_cmd == G_DIMPP) return dt_click(mx, my, w, ctrl);   /* cui_draft_tan.c */
    if (geo_cmd == G_DIMA) return da_click(mx, my, w, ctrl);
    if (geo_cmd == G_DIMB || geo_cmd == G_DIMCH) return db_click(mx, my, w, ctrl);
    if (dr_placing) {
        if (!dr_drag_ok) { double c[3]; dr_sketched(mx, my, w, c); if (dd_compute(c, ctrl) <= 0) return 1; }
        dr_S.ldr_vis = f_e8;
        dd_live_in = dd_in; dd_live_ok = 1;
        dr_store(); dd_restart();
        return 1;
    }
    if (dr_live >= 0) dr_commit_live();                 /* a new pick commits the live entity (cad_rundraft.c:3404-3434) */
    int e = dd_pick_arc_any(mx, my);
    if (e >= 0) dd_arc_event(e); else dd_prompt();
    dr_scene();
    return 1;
}

int dd_esc(void) { return geo_cmd == G_DIMB || geo_cmd == G_DIMCH ? db_esc() : -1; }
int dd_button(int code) { return geo_cmd == G_DIMB || geo_cmd == G_DIMCH ? db_button(code) : -1; }

/* ------------------------------------------------------------------------------------------------ the function panel */
/* FUN_18004c840 (circ_cad.c) refreshes ids 1 (arrows), 3 (centred), 0xf (box), 0xc (digits), 7 (lock), 0xd (Arc Symbol = cc4), and
   for the quadrant style 2 (witness) / 8 (angle). Live here: Leaders _Inside / _Outside (FUN_18012af20 case 1 + FUN_18004c950's leader-
   mode bookkeeping), _Centered text (3), De_cimal places (12), Loc_k (7: DAT_1806a4c20), Arc Symbol Radi_us / _Diameter (13: cc4,
   FUN_18004c950 case 0xd). Mode / angle (FUN_18004c950: id 8 ignored for the radial style; 5 / 6 not handled), the rest: greyed */
static int pc_arrows, pc_center, pc_digits, pc_lock, pc_sym, pc_dummy;
void dd_panel_body(const CuiPanelOps *ops, void *u) {
    if (geo_cmd == G_DIMPT) { dp4_panel_body(ops, u); return; }   /* cui_draft_pt.c */
    if (geo_cmd == G_DIMT || geo_cmd == G_DIMPP) { dt_panel_body(ops, u); return; }   /* cui_draft_tan.c */
    if (geo_cmd == G_DIMA) { da_panel_body(ops, u); return; }
    if (geo_cmd == G_DIMB || geo_cmd == G_DIMCH) return;   /* FUN_18003c3d0 opens no function panel */
    pc_arrows = dr_S.arrows_out == 1; pc_center = dr_S.auto_align; pc_digits = dr_S.digits; pc_lock = dd_lock; pc_sym = dd_diam;
    int t = -1; double ang = 0;
    if (!ops->tab || ops->tab(u, 0, "Basic")) {
        if (ops->exp(u, "Entity")) {
            ops->label(u, "Mode:");
            ops->radio(u, "_Auto", &t, 0, 0); ops->radio(u, "_Horizontal", &t, 1, 0); ops->radio(u, "_Vertical", &t, 2, 0);
            ops->num(u, "Orientation an_gle:", "angle", &ang, NULL, 0, -360, 360);
            ops->check(u, "_Maintain perpendicular to arc center", &pc_dummy, 0);
            ops->check(u, "Loc_k", &pc_lock, 1);
            ops->end(u);
        }
        if (ops->exp(u, "Arc Symbol")) { ops->radio(u, "Radi_us", &pc_sym, 0, 1); ops->radio(u, "_Diameter", &pc_sym, 1, 1); ops->check(u, "A_pply to linear dimension", &pc_dummy, 0); ops->end(u); }
        if (ops->exp(u, "_Witness lines")) { t = -1; ops->radio(u, "_Both", &t, 0, 0); ops->radio(u, "_Right", &t, 1, 0); ops->radio(u, "_Left", &t, 2, 0); ops->end(u); }
        if (ops->exp(u, "Leaders")) { ops->label(u, "Arrows:"); ops->radio(u, "_Inside", &pc_arrows, 0, 1); ops->radio(u, "_Outside", &pc_arrows, 1, 1); ops->end(u); }
        if (ops->exp(u, "Format")) { ops->toggle(u, "_Font", 1, -1); ops->toggle(u, "_Edit text", 1, -1); ops->toggle(u, "Heigh_t", 1, -1);
            ops->inum(u, "De_cimal places:", "digits", &pc_digits, NULL, 1, 0, 8); ops->end(u); }
    }
    if (!ops->tab || ops->tab(u, 1, "Advanced")) {
        if (ops->exp(u, "Dimension Text")) { ops->check(u, "_Centered text", &pc_center, 1); ops->label(u, "Edit Mode:");
            t = -1; ops->radio(u, "P_lace", &t, 0, 0); ops->radio(u, "_Edit", &t, 1, 0); ops->end(u); }
        if (ops->exp(u, "Dimension Box")) { ops->check(u, "_Show when dragging", &pc_dummy, 0); ops->end(u); }
        if (ops->exp(u, "Settings")) { ops->toggle(u, "_Options", 1, -1); ops->toggle(u, "_Update", 1, -1); ops->end(u); }
    }
}
/* FUN_18004c950 (circ_cad.c) after FUN_18012af20 (cad_linear2.c:244): the panel values into the globals */
void dd_panel_sync(void) {
    if (geo_cmd == G_DIMPT) { dp4_panel_sync(); return; }   /* cui_draft_pt.c */
    if (geo_cmd == G_DIMT || geo_cmd == G_DIMPP) { dt_panel_sync(); return; }   /* cui_draft_tan.c */
    if (geo_cmd == G_DIMA) { da_panel_sync(); return; }
    if (geo_cmd == G_DIMB || geo_cmd == G_DIMCH) return;
    int ch = 0, ao0 = dr_S.arrows_out, lv0 = dr_S.ldr_vis;
    if ((pc_arrows ? 1 : 0) != (dr_S.arrows_out == 1)) {    /* id 1 */
        dr_S.arrows_out = pc_arrows ? 1 : 0; ch = 1;
        if (dr_placing && dd_style == 2) {
            if (dr_S.arrows_out != ao0 && dr_S.ldr_vis == lv0) {
                if (dr_S.arrows_out == 1) { f_dd = 0; int v = f_e0; f_e4 = lv0; dr_S.ldr_vis = v; }
                else { f_de = 0; int v = f_e4; f_e0 = lv0; dr_S.ldr_vis = v; }
            }
            int lv = dr_S.ldr_vis;
            if (lv != lv0) {
                if (lv < 1) { f_e0 = f_dd = f_e4 = f_de = 0; }
                else if (dr_S.arrows_out == 1) { f_dd = 0; f_e0 = lv; }
                else { f_de = 0; f_e4 = lv; }
            }
        } else if (dr_placing && dr_S.ldr_vis != lv0) { f_df = 0; f_e8 = dr_S.ldr_vis; }
    }
    if (pc_center != dr_S.auto_align) { dr_S.auto_align = pc_center; ch = 1; }   /* id 3 */
    if (pc_digits != dr_S.digits && pc_digits >= 0 && pc_digits <= 8) { dr_S.digits = pc_digits; ch = 1; }   /* id 12 */
    if (pc_lock != dd_lock) dd_lock = pc_lock;                                    /* id 7 */
    if ((pc_sym != 0) != dd_diam) { dd_diam = pc_sym != 0; ch = 1; }              /* id 0xd */
    if (!ch) return;
    if (dr_live >= 0 && !dr_placing && dd_live_ok && F.dnew[dr_live].E[0x308] == 2) {   /* the live entity recomputed (as cui_draft.c dr_panel_sync) */
        DcSettings S = dr_S; S.sel_dist = sel_dist_w(); S.color = F.dnew[dr_live].E[6]; S.level = (int)F.dnew[dr_live].E[8];
        DcCircInput in = dd_live_in; in.diameter = dd_diam;
        uint8_t E[0x810]; memcpy(E, F.dnew[dr_live].E, 0x810);
        DcResult R = dc_calc_circular(E, &in, &S, dr_fonts_get());
        if (R.valid) { memcpy(F.dnew[dr_live].E, E, 0x810); dd_live_in = in; }
        dr_scene();
    }
}

/* ------------------------------------------------------------------------------------------------ tests (ours) */
static double D8(const uint8_t *b, int o) { double v; memcpy(&v, b + o, 8); return v; }
static int I16_(const uint8_t *b, int o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static const uint8_t *file_circ(DraftDoc *dd, int want, int *count) {   /* the open part's circular dimension records */
    int k = 0; const uint8_t *hit = NULL; uint32_t n;
    for (int i = 0; i < draft_count(dd); i++) { const uint8_t *E = draft_record(dd, i, &n);
        if (E && n == 0x810 && E[0] == 0x40 && E[0x308] == 2) { if (k == want) hit = E; k++; } }
    if (count) *count = k;
    return hit;
}
/* circglobals:N - the session globals from the open part's circular dimension N (DraftGlobalsFromDim, as tools/tests/draft_circ_test.c;
   arrows forced outside by a small radius (FUN_18024f3c0 l.4947: +0x7b0 = 1 with leader mode 3) were inside in the globals) */
static void dd_globals_from(const uint8_t *Es) {
    DcSettings *S = &dr_S;
    memcpy(S->fa, Es + 0x5d8, 0x60);
    const uint8_t *l1 = Es + 0x4a8, *l2 = Es + 0x540, *l = l1[0] && l1[0x18] != 3 ? l1 : l2[0] && l2[0x18] != 3 ? l2 : l1[0] ? l1 : l2;
    S->arrow_h = D8(l, 8); S->arrow_w = D8(l, 0x10); S->arrow_type = l[0x18];
    S->arrows_out = I16_(Es, 0x7b0); S->ldr_style = I16_(Es, 0x7b4); S->ldr_vis = I16_(Es, 0x7ae); S->wit_vis = 3;
    if (S->arrows_out == 1 && S->ldr_vis == 3) S->arrows_out = 0;
    S->text_orient = fabs(D8(Es, 0x5e8)) < 5e-5; S->auto_align = Es[0x7c9];
    S->scale = D8(Es, 0x330); S->coord_fmt = I16_(Es, 0x7c0); S->frac_units = I16_(Es, 0x7c2); S->digits = Es[0x7cd]; S->lead0 = Es[0x7cc]; S->trail0 = Es[0x7cf]; S->comma = Es[0x7cb];
    S->diam_fmt = S->radius_fmt = I16_(Es, 0x7b2); S->tol_mode = I16_(Es, 0x7b6) == 3 ? 1 : I16_(Es, 0x7b6) > 3 ? 0 : I16_(Es, 0x7b6);
    S->pos_tol = D8(Es, 0x340); S->neg_tol = D8(Es, 0x348); S->din_tol = Es[0x7c6]; S->din_chr = Es[0x7c7]; S->din_val = Es[0x7c8];
    dr_saved = dr_S;
}
static void dd_cmp(int want) {
    DraftDoc *dd = draft_load(F.path); int n = 0; const uint8_t *Es = file_circ(dd, want, &n);
    if (!Es || !F.ndnew) { printf("    circcmp: nothing to compare (%d file dims, %d made)\n", n, F.ndnew); draft_free(dd); return; }
    const uint8_t *Eo = F.dnew[F.ndnew - 1].E; int nd = 0, nref = 0; char why[2000] = ""; size_t L = 0;
#define DF(...) do { nd++; L += (size_t)snprintf(why + L, L < sizeof why ? sizeof why - L : 0, __VA_ARGS__); } while (0)
#define CLOSE(a, b) (fabs((a) - (b)) <= 1e-6 * (1 + fabs(b)))
    uint64_t ea, eb; memcpy(&ea, Eo + 0x90, 8); memcpy(&eb, Es + 0x90, 8);
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x70 + 8 * k), D8(Es, 0x70 + 8 * k))) { DF(" aPoint position;"); break; }
    if (Eo[0x68] != Es[0x68]) nref++;
    if (Eo[0x100] != Es[0x100] || Eo[0x101] != Es[0x101] || Eo[0x110] != Es[0x110]) DF(" style %d/%d diam %d/%d quad %d/%d;", Eo[0x100], Es[0x100], Eo[0x101], Es[0x101], Eo[0x110], Es[0x110]);
    if (!CLOSE(D8(Eo, 0x338), D8(Es, 0x338))) DF(" value %.6f/%.6f;", D8(Eo, 0x338), D8(Es, 0x338));
    if (strncmp((const char *)Eo + 0x650, (const char *)Es + 0x650, 0x58)) DF(" text '%.20s'/'%.20s';", Eo + 0x650, Es + 0x650);
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x638 + 8 * k), D8(Es, 0x638 + 8 * k))) { DF(" textpos (%.5f,%.5f)/(%.5f,%.5f);", D8(Eo, 0x638), D8(Eo, 0x640), D8(Es, 0x638), D8(Es, 0x640)); break; }
    if (!CLOSE(D8(Eo, 0x5e8), D8(Es, 0x5e8))) DF(" textangle;");
    if (Eo[0x378] != Es[0x378] || Eo[0x410] != Es[0x410]) DF(" witness on;");
    static const int wo[2] = { 0x4a8, 0x540 }; static const char *wn[2] = { "ldr1", "ldr2" };
    for (int q = 0; q < 2; q++) { const uint8_t *a = Eo + wo[q], *b = Es + wo[q]; if (!a[0] && !b[0]) continue;
        int bad = a[0] != b[0] || a[1] != b[1] || a[3] != b[3] || a[4] != b[4] || a[0x18] != b[0x18] || I16_(a, 0x1a) != I16_(b, 0x1a) || !CLOSE(D8(a, 8), D8(b, 8)) || !CLOSE(D8(a, 0x10), D8(b, 0x10));
        int np = I16_(b, 0x1a); if (np > 4) np = 4;
        for (int i = 0; !bad && i < np * 3; i++) bad = !CLOSE(D8(a, 0x20 + 8 * i), D8(b, 0x20 + 8 * i));
        if (bad) DF(" %s;", wn[q]); }
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x310 + 8 * k), D8(Es, 0x310 + 8 * k))) { DF(" placement;"); break; }
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x360 + 8 * k), D8(Es, 0x360 + 8 * k))) { DF(" offsets (%.5f,%.5f)/(%.5f,%.5f);", D8(Eo, 0x360), D8(Eo, 0x368), D8(Es, 0x360), D8(Es, 0x368)); break; }
    if (I16_(Eo, 0x7ae) != I16_(Es, 0x7ae) || I16_(Eo, 0x7b0) != I16_(Es, 0x7b0)) DF(" leader mode %d/%d arrows %d/%d;", I16_(Eo, 0x7ae), I16_(Es, 0x7ae), I16_(Eo, 0x7b0), I16_(Es, 0x7b0));
#undef DF
#undef CLOSE
    printf("    circcmp %d: %s%s%s\n", want, nd ? "DIFFER" : nref ? "EQUAL-LAYOUT" : "EQUAL", why, nref ? " (aPoint kind differs)" : "");
    draft_free(dd);
}
/* one line per circular record: the fields the oracle compares (ours, tests) */
static void dd_line(const char *tag, const uint8_t *E) {
    char t[0x60]; memcpy(t, E + 0x650, 0x58); t[0x58] = 0;
    printf("    %s: style %d diam %d '%c' (%.4f,%.4f,%.4f) value %.6f text '%s' at (%.4f,%.4f) angle %.4f place (%.4f,%.4f) offs (%.4f,%.4f) lv %d ao %d",
           tag, E[0x100], E[0x101], E[0x68], D8(E, 0x70), D8(E, 0x78), D8(E, 0x80), D8(E, 0x338), t, D8(E, 0x638), D8(E, 0x640), D8(E, 0x5e8), D8(E, 0x310), D8(E, 0x318),
           D8(E, 0x360), D8(E, 0x368), I16_(E, 0x7ae), I16_(E, 0x7b0));
    static const int wo[2] = { 0x4a8, 0x540 };
    for (int q = 0; q < 2; q++) { const uint8_t *w = E + wo[q]; int n = I16_(w, 0x1a); if (n > 4) n = 4;
        printf(" | ldr%d on %d vis %d type %d n %d", q + 1, w[0], w[3], w[0x18], I16_(w, 0x1a));
        for (int i = 0; w[0] && i < n; i++) printf(" (%.4f,%.4f)", D8(w, 0x20 + 0x18 * i), D8(w, 0x28 + 0x18 * i)); }
    printf("\n");
}
/* circdump (the session's circular dimensions), circfile (the open part's), circarc, circglobals:N, circredo:N (a placement mouse move at the cursor Mastercam's regeneration FUN_180303e20 gives the open part's circular
   dimension N: the arc centre + the text offsets), circcmp:N (the last dimension made against it), circpanel:K=V (arrows / center /
   digits / lock / diameter) */
int dd_script(const char *st, const char *arg) {
    if (da_script(st, arg)) return 1;                   /* cui_draft_ang.c: dimreal */
    if (db_script(st, arg)) return 1;                   /* cui_draft_base.c: basepick */
    if (dt_script(st, arg)) return 1;                   /* cui_draft_tan.c */
    if (dp4_script(st, arg)) return 1;                  /* cui_draft_pt.c */
    if (!strcmp(st, "circarc") && arg) {                /* ours: circarc:CX,CY,R,A0,SW - an XY arc (degrees) as geoline adds a line */
        double a[5] = { 0, 0, 1, 0, 360 }; sscanf(arg, "%lf,%lf,%lf,%lf,%lf", &a[0], &a[1], &a[2], &a[3], &a[4]);
        if (!loaded && cui_hooks.new_part) cui_hooks.new_part();
        double a0 = a[3] * 0.017453292519943295, sw = a[4] * 0.017453292519943295;
        V3 c = { a[0], a[1], 0 }, p = { a[0] + a[2] * cos(a0), a[1] + a[2] * sin(a0), 0 }, q = { a[0] + a[2] * cos(a0 + sw), a[1] + a[2] * sin(a0 + sw), 0 };
        geo_add(E_ARC, p, q, c, a[2], a0, sw); scene_reupload(); return 1; }
    if (!strncmp(st, "circ", 4) && strcmp(st, "circarc") && strcmp(st, "circdump") && strcmp(st, "circfile") && strcmp(st, "circglobals") && strcmp(st, "circredo") && strcmp(st, "circcmp") && strcmp(st, "circpanel")) return 0;
    if (!strncmp(st, "circ", 4) && strcmp(st, "circdump") && strcmp(st, "circfile") && !arg) { printf("    (%s: argument missing)\n", st); return 1; }
    if (!strcmp(st, "circdump")) { int k = 0; for (int i = 0; i < F.ndnew; i++) if (F.dnew[i].on && F.dnew[i].E[0x308] == 2) { char tag[32]; snprintf(tag, sizeof tag, "C%d", k++); dd_line(tag, F.dnew[i].E); } return 1; }
    if (!strcmp(st, "circfile")) { DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; int n = 0; file_circ(dd, -1, &n);
        for (int k = 0; k < n; k++) { char tag[32]; snprintf(tag, sizeof tag, "C%d", k); dd_line(tag, file_circ(dd, k, NULL)); } draft_free(dd); return 1; }
    if (!strcmp(st, "circglobals")) { DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *E = file_circ(dd, atoi(arg), NULL);
        if (E) { dr_globals_load(); dd_globals_from(E); } else printf("    (no circular dimension %s)\n", arg); draft_free(dd); return 1; }
    if (!strcmp(st, "circredo")) {
        DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *Es = file_circ(dd, atoi(arg), NULL);
        if (!Es || !dr_placing || dd_arc < 0) printf("    (circredo: %s)\n", Es ? "no placement" : "no such dimension");
        else {
            double ox = D8(Es, 0x360), oy = D8(Es, 0x368), a = 1e-12 <= ox * ox + oy * oy ? atan2(oy, ox) : 0.0; if (a < 0) a += TWOPI_;
            double deg = a * 57.29577951308233 + D8(Es, 0x108), dist = sqrt(ox * ox + oy * oy), R0 = 0.0 <= dist - D8(Es, 0x118) ? dist - D8(Es, 0x118) + F.ents[dd_arc].r : dist;
            double cv[3] = { F.ents[dd_arc].c.x + cos(deg * 0.01745329251994329) * R0, F.ents[dd_arc].c.y + sin(deg * 0.01745329251994329) * R0, D8(Es, 0x370) }, cw[3];
            for (int i = 0; i < 3; i++) cw[i] = dr_view[i] * cv[0] + dr_view[3 + i] * cv[1] + dr_view[6 + i] * cv[2];
            dd_compute(cw, 0);
        }
        draft_free(dd); return 1;
    }
    if (!strcmp(st, "circcmp")) { if (loaded && F.path[0]) dd_cmp(atoi(arg)); else printf("    (no part file)\n"); return 1; }
    if (!strcmp(st, "circpanel")) {
        char k[32] = ""; const char *eq = strchr(arg, '='); int v = eq ? atoi(eq + 1) : 0;
        snprintf(k, sizeof k, "%.*s", eq ? (int)(eq - arg) : (int)strlen(arg), arg);
        pc_arrows = dr_S.arrows_out == 1; pc_center = dr_S.auto_align; pc_digits = dr_S.digits; pc_lock = dd_lock; pc_sym = dd_diam;
        if (!strcmp(k, "arrows")) pc_arrows = v; else if (!strcmp(k, "center")) pc_center = v; else if (!strcmp(k, "digits")) pc_digits = v;
        else if (!strcmp(k, "lock")) pc_lock = v; else if (!strcmp(k, "diameter")) pc_sym = v; else printf("    (unknown panel value %s)\n", k);
        dd_panel_sync(); return 1;
    }
    return 0;
}
