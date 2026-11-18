/* cui_draft_pt.c - the Drafting tab's Point command (doDraft_DIMENSION_POINT 0x947f, dimension type 4) in the dimension command loop
 * (cui_draft.c / cui_draft_dim.c dispatch here as G_DIMPT): mccad FUN_180040ed0 case 0x947f -> FUN_180048b10(4, -1) (cad_dimcmd.c:441-512)
 * -> FUN_18004b820 (cad_linear.c:904); get_event FUN_180049c60 case 4 (cad_linear.c:2685: flags 9 with no pick, 0xc after it, mask 0);
 * prompts FUN_180049560 case 4 (cad_linear.c:359-585); the point event FUN_180049ee0 (cad_linear.c:615, LAB_18004a417); the drag
 * FUN_180048be0's default branch (cad_dimcmd.c:1239-1355: calc_point_dimension = viewer/draft_create_pt.c); accept FUN_180048940 event 4
 * -> FUN_180049e80 -> FUN_180049250(1) (cad_dimcmd.c:1766 / 526-645, cad_linear.c:316); the panel FUN_180049be0 -> FUN_18012af20
 * (cad_linear.c:2454, cad_linear2.c:244) then FUN_18004b8d0 (the drag recomputed at the last cursor). "ours" marks glue of this program. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "cui_draft_int.h"
#include "draft_create_pt.h"
#include "mcam_saveops.h"
#include "draft.h"

void dp4_prompt(void);
void dp4_restart(void);
int dp4_need(void);
int dp4_move(const double world[3], int ctrl);
int dp4_click(float mx, float my, const double w[3], int ctrl);
void dp4_panel_body(const CuiPanelOps *ops, void *u);
void dp4_panel_sync(void);
int dp4_script(const char *st, const char *arg);

/* ------------------------------------------------------------------------------------------------ state (mccad globals) */
static int dp_n;                                        /* DAT_1806960e8: the point picked (0 / 1) */
static uint8_t dp_slot[0x98]; static int dp_sent = -1;  /* DAT_180698a30 (slot 0) and the entity its aPoint references (-1) */
static double dp_last[3]; static int dp_have_last;      /* the last sketched point (_DAT_180698ca0 -> c90: FUN_18004b8d0 recomputes there) */
static DcPtInput dp_in, dp_live_in; static int dp_live_ok;
/* ptdim_use3d @1812d9867 / ptdim_label @1812d9866 / ptdim_abs @1813d5a38 from the config items their writer FUN_1801d0880 stores
   (core_cfgdraft.c:812-814): 12011 "3D?", 12010 "3D label?", 12009 "Absolute?" (the user's: n / y / n); .data default 0 when missing */
static int gp_use3d, gp_label, gp_abs, gp_units = -1;
static int cfgi_(int key, int def) { char v[128]; mcam_config_value(dr_S.metric, key, v, sizeof v); if (!v[0]) return def; if (v[0] == 'y' || v[0] == 'Y') return 1; if (v[0] == 'n' || v[0] == 'N') return 0; return atoi(v); }
static void gp_load(void) {
    if (gp_units == dr_S.metric) return;
    gp_units = dr_S.metric;
    gp_use3d = cfgi_(12011, 0); gp_label = cfgi_(12010, 0); gp_abs = cfgi_(12009, 0);
}

/* FUN_180049560 type 4: mccadres 27713 "Create dimension, point: " + (no pick) 27064 "Select a point to create a point dimension" /
   (the point picked) 27051 "Sketch the text position for the point dimension", joined by "\n" (0x18040e028); with a live entity and no
   pick the loop's mccoreres 5887 (cad_rundraft.c:3496-3519, as cui_draft.c) */
void dp4_prompt(void) {
    if (dr_live >= 0 && !dp_n) snprintf(cam_msg, sizeof cam_msg, "Modify current dimension or begin creating a new dimension");
    else if (!dp_n) snprintf(cam_msg, sizeof cam_msg, "Create dimension, point: \nSelect a point to create a point dimension");
    else snprintf(cam_msg, sizeof cam_msg, "Create dimension, point: \nSketch the text position for the point dimension");
}
/* FUN_180048b10(4, -1) -> FUN_18004b820: InitDIM(drag_ent), DfDragPtDim (Smart only), lock 0, DfHvpAngle 0, no pick; FUN_180049350 (no
   drag-edit entity); FUN_180049c60 case 4: flags 9, mask 0, SetDfDragEntValid(false), the prompt */
void dp4_restart(void) {
    dp_n = 0; dr_npick = 0; dr_placing = 0; dr_drag_ok = 0; dr_Pent[0] = dr_Pent[1] = -1; dp_sent = -1; dp_have_last = 0;
    gp_load();
    dc_init_dim(dr_drag, &dr_S, dr_view_no());
    dp4_prompt(); dr_scene();
}
/* get_event's needs (FUN_180049c60 case 4): no pick: flags 9 = point events only (mask 0: no entity); after it: flags 0xc, the sketch */
int dp4_need(void) { return dr_placing ? 't' : 'p'; }

static void ap_ref(uint8_t ap[0x98], int e) {           /* as cui_draft_ang.c: the entity's eptr, or the session token the save replaces (ours) */
    uint64_t ep = e >= 0 && F.ents[e].eptr ? (uint64_t)F.ents[e].eptr : e >= 0 ? MCAM_EPTR_TOKEN((uint64_t)e) : 0; memcpy(ap + 0x28, &ep, 8);
}
static void dp_fill(DcPtInput *in, const double cur[3]) {
    memset(in, 0, sizeof *in);
    memcpy(in->ap, dp_slot, 0x98); memcpy(in->cursor, cur, sizeof in->cursor);
    in->view = dr_view_no(); memcpy(in->vm, dr_view, sizeof in->vm);   /* MastercamPlanes::GetActiveViewNo(Cplane) (cad_dimcmd.c:1325) */
    in->use3d = gp_use3d; in->label = gp_label; in->absolute = gp_abs;
    in->origin_on = 0;                                  /* ours: ActiveOriginIsOn(Cplane) - the app's Cplane is Top at the world origin
                                                           (cui_draft.c dr_view_no; cui_ops.c:185) */
    in->coord_disp = 0;                                 /* ours: GetCoordinateDisplay - the app's planes are mill planes (no lathe display) */
}
/* FUN_180048be0 default branch (cad_dimcmd.c:1290-1355): df_calc_sketched_point -> slot 1, calc_point_dimension(drag, slot 0, slot 1's
   point, the active view, &ok); valid: the drag drawn; else mccadres 27010 "Warning - cannot calculate dimension" (FUN_18012ae10(0x6982))
   and FUN_180048800 (the placement abandoned: no pick, the drag erased, *param_8 = 1) */
static int dp_compute(const double cur[3], int ctrl) {
    (void)ctrl;                                         /* calc_point_dimension reads no key state */
    DcPtInput in; dp_fill(&in, cur);
    DcSettings S = dr_S; S.color = geo_color; S.level = geo_level;   /* main_color / main_level */
    DcResult R = dc_calc_point(dr_drag, &in, &S, dr_fonts_get());
    if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); dr_drag_ok = 0; dr_scene(); return -1; }
    dr_drag_ok = R.valid;
    if (!R.valid) {
        snprintf(cam_msg, sizeof cam_msg, "Warning - cannot calculate dimension");
        dp_n = 0; dr_npick = 0; dr_placing = 0; dp_have_last = 0; dr_scene();
        return 0;
    }
    dp_in = in; memcpy(dp_last, cur, sizeof dp_last); dp_have_last = 1;
    dr_scene();
    return 1;
}
int dp4_move(const double world[3], int ctrl) {
    if (!dr_placing) return 0;
    double c[3] = { world[0], world[1], grid_z() };    /* the sketched point on the construction plane (as cui_draft_ang.c da_move) */
    dp_compute(c, ctrl);
    return 1;
}
/* a left click. Placement (flags 0xc: the sketch, event 4): FUN_180049e80 -> FUN_180049250(1): no drag-edit entity -> UndoTransStart,
   df_save_drag_ent, UndoTransCommit; FUN_180048800 then ends this dimension and the loop restarts the command (the stored dimension is
   the live one, as cui_draft.c dr_store). Ours: the drag computed at the click first when no move made it (as cui_draft.c). Otherwise
   the point event (flags 9; FUN_1804e3b90: the AutoCursor point unless Ctrl / Alt, else map_cursor_to_3d's 'K' point): FUN_180049ee0
   LAB_18004a417: aPointTestTol, slot 0, one pick, FUN_180049c60 (flags 0xc, prompt 27051, the drag not valid) */
int dp4_click(float mx, float my, const double w[3], int ctrl) {
    if (dr_placing) {
        if (!dr_drag_ok) { double c[3]; dr_sketched(mx, my, w, c); if (dp_compute(c, ctrl) <= 0) return 1; }
        dp_live_in = dp_in; dp_live_ok = 1;
        dr_Pent[0] = dp_sent; dr_Pent[1] = -1;          /* the save patches the aPoint's entity token itself (mcam_saveops.c, type 4: +0x68) */
        dr_store(); dp4_restart();
        return 1;
    }
    if (dr_live >= 0) dr_commit_live();                 /* a new pick commits the live entity (cad_rundraft.c:3404-3434) */
    V3 p = { w[0], w[1], grid_z() };
    geo_snap_ptr(mx, my); int sn = !ctrl && geo_snap(&p, NULL, -1, 1); geo_snap_ptr_off();
    uint8_t ap[0x98]; int ent = -1;
    if (sn) { dr_ac_apoint(ap, &ent, p); if (ent >= 0) ap_ref(ap, ent); }
    else { double q[3] = { w[0], w[1], grid_z() }; dc_apoint_world(ap, q, 'K'); }
    memcpy(dp_slot, ap, 0x98); dp_sent = ent;           /* (dc_apoint_world applied aPointTestTol) */
    dp_n = 1; dr_npick = 1; dr_placing = 1; dr_drag_ok = 0; dp_have_last = 0;
    dp4_prompt(); dr_scene();
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the function panel */
/* FUN_180049b50 refreshes (with a valid drag) ids 0xf box, 0xc digits, 0x16, 0x15, 0xb, 9, 10; the events go to FUN_180049be0 ->
   FUN_18012af20 (common): 1 arrows (_dd_global -> the record's +0x7b0), 3 centred (auto_alignment -> +0x7c9), 2 witness (dd_global+6:
   not in a point record, +0x7ac = 0), 9 / 10 / 0xb font / edit text / height, 0xf box, 0x15 options, 0x16 update; 0xc (digits) ->
   FUN_18012bca0 (its body NOT DECODED, as for Angular); then FUN_18004b8d0 = FUN_180048be0 at the previous cursor. Live here: Leaders
   Inside / Outside, Centered text; the rest greyed */
static int pp_arrows, pp_center, pp_dummy;
void dp4_panel_body(const CuiPanelOps *ops, void *u) {
    pp_arrows = dr_S.arrows_out == 1; pp_center = dr_S.auto_align;
    int t = -1, dg = dr_S.digits; double ang = 0;
    if (!ops->tab || ops->tab(u, 0, "Basic")) {
        if (ops->exp(u, "Entity")) { ops->label(u, "Mode:"); ops->radio(u, "_Auto", &t, 0, 0); ops->radio(u, "_Horizontal", &t, 1, 0); ops->radio(u, "_Vertical", &t, 2, 0);
            ops->num(u, "Orientation an_gle:", "angle", &ang, NULL, 0, -360, 360); ops->check(u, "_Maintain perpendicular to arc center", &pp_dummy, 0);
            ops->check(u, "Loc_k", &pp_dummy, 0); ops->end(u); }
        if (ops->exp(u, "Arc Symbol")) { ops->radio(u, "Radi_us", &t, 0, 0); ops->radio(u, "_Diameter", &t, 1, 0); ops->check(u, "A_pply to linear dimension", &pp_dummy, 0); ops->end(u); }
        if (ops->exp(u, "_Witness lines")) { ops->radio(u, "_Both", &t, 0, 0); ops->radio(u, "_Right", &t, 1, 0); ops->radio(u, "_Left", &t, 2, 0); ops->end(u); }
        if (ops->exp(u, "Leaders")) { ops->label(u, "Arrows:"); ops->radio(u, "_Inside", &pp_arrows, 0, 1); ops->radio(u, "_Outside", &pp_arrows, 1, 1); ops->end(u); }
        if (ops->exp(u, "Format")) { ops->toggle(u, "_Font", 1, -1); ops->toggle(u, "_Edit text", 1, -1); ops->toggle(u, "Heigh_t", 1, -1);
            ops->inum(u, "De_cimal places:", "digits", &dg, NULL, 0, 0, 8); ops->end(u); }
    }
    if (!ops->tab || ops->tab(u, 1, "Advanced")) {
        if (ops->exp(u, "Dimension Text")) { ops->check(u, "_Centered text", &pp_center, 1); ops->label(u, "Edit Mode:");
            ops->radio(u, "P_lace", &t, 0, 0); ops->radio(u, "_Edit", &t, 1, 0); ops->end(u); }
        if (ops->exp(u, "Dimension Box")) { ops->check(u, "_Show when dragging", &pp_dummy, 0); ops->end(u); }
        if (ops->exp(u, "Settings")) { ops->toggle(u, "_Options", 1, -1); ops->toggle(u, "_Update", 1, -1); ops->end(u); }
    }
}
void dp4_panel_sync(void) {
    int ch = 0;
    if ((pp_arrows ? 1 : 0) != (dr_S.arrows_out == 1)) { dr_S.arrows_out = pp_arrows ? 1 : 0; ch = 1; }   /* FUN_18012af20 case 1 */
    if (pp_center != dr_S.auto_align) { dr_S.auto_align = pp_center; ch = 1; }                             /* case 3 */
    if (!ch) return;
    if (dr_placing && dp_have_last) { double c[3]; memcpy(c, dp_last, sizeof c); dp_compute(c, 0); return; }   /* FUN_18004b8d0 */
    if (dr_live >= 0 && !dr_placing && dp_live_ok && F.dnew[dr_live].E[0x308] == 4) {   /* the live entity recomputed (FUN_180038ed0 ->
                                                                                           FUN_1800386f0, as cui_draft.c) */
        DcSettings S = dr_S; S.color = F.dnew[dr_live].E[6]; S.level = (int)F.dnew[dr_live].E[8];
        uint8_t E[0x810]; memcpy(E, F.dnew[dr_live].E, 0x810);
        DcResult R = dc_calc_point(E, &dp_live_in, &S, dr_fonts_get());
        if (R.valid) memcpy(F.dnew[dr_live].E, E, 0x810);
        dr_scene();
    }
}

/* ------------------------------------------------------------------------------------------------ tests (ours) */
static double D8(const uint8_t *b, int o) { double v; memcpy(&v, b + o, 8); return v; }
static int I16_(const uint8_t *b, int o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static void pt_line(const char *tag, const uint8_t *E) {
    char t[0x60]; memcpy(t, E + 0x650, 0x58); t[0x58] = 0; uint64_t ep; memcpy(&ep, E + 0x68 + 0x28, 8);
    printf("    %s: aPoint '%c' (%.4f,%.4f,%.4f)%s view %d text '%s' at (%.4f,%.4f) 3d %d label %d abs %d offsets (%.4f,%.4f) arrows %d auto %d\n",
           tag, E[0x68] ? E[0x68] : '?', D8(E, 0x70), D8(E, 0x78), D8(E, 0x80), ep ? " eptr" : "", I16_(E, 0x328), t, D8(E, 0x638), D8(E, 0x640),
           E[0x100], E[0x101], E[0x102], D8(E, 0x360), D8(E, 0x368), I16_(E, 0x7b0), E[0x7c9]);
    const uint8_t *w = E + 0x4a8; int n = I16_(w, 0x1a); if (n > 4) n = 4;
    printf("      ldr1 on %d vis %d type %d n %d:", w[0], w[3], w[0x18], I16_(w, 0x1a));
    for (int i = 0; i < n; i++) printf(" (%.4f,%.4f)", D8(w, 0x20 + 0x18 * i), D8(w, 0x28 + 0x18 * i));
    printf("\n");
}
/* the session globals from a record (DraftGlobalsFromDim @180308cc0 core_draftwf.c 540-720: type != 1 -> the linear number globals;
   type 4 -> ptdim_label / use3d / abs from +0x101 / +0x100 / +0x102) */
static void dp_globals_from(const uint8_t *Es) {
    DcSettings *S = &dr_S;
    memcpy(S->fa, Es + 0x5d8, 0x60);
    const uint8_t *l1 = Es + 0x4a8, *l2 = Es + 0x540, *l = l1[0] && l1[0x18] != 3 ? l1 : l2[0] && l2[0x18] != 3 ? l2 : l1[0] ? l1 : l2;
    S->arrow_h = D8(l, 8); S->arrow_w = D8(l, 0x10); S->arrow_type = l[0x18];
    S->arrows_out = I16_(Es, 0x7b0); S->ldr_style = I16_(Es, 0x7b4); S->auto_align = Es[0x7c9];
    S->gap = D8(Es, 0x350); S->ext = D8(Es, 0x358); S->scale = D8(Es, 0x330);
    S->coord_fmt = I16_(Es, 0x7c0); S->frac_units = I16_(Es, 0x7c2); S->digits = Es[0x7cd]; S->lead0 = Es[0x7cc]; S->trail0 = Es[0x7cf]; S->comma = Es[0x7cb];
    S->tol_mode = I16_(Es, 0x7b6) == 3 ? 1 : I16_(Es, 0x7b6) > 3 ? 0 : I16_(Es, 0x7b6); S->pos_tol = D8(Es, 0x340); S->neg_tol = D8(Es, 0x348);
    S->din_tol = Es[0x7c6]; S->din_chr = Es[0x7c7]; S->din_val = Es[0x7c8]; S->constr3d = Es[0x7d0]; S->metric = I16_(Es, 0x7ba) == 1;
    gp_label = (int8_t)Es[0x101]; gp_use3d = Es[0x100]; gp_abs = Es[0x102];
    dr_saved = dr_S;
}
static const uint8_t *file_pt(DraftDoc *dd, int want, int *count) {   /* the open part's point dimension records */
    int k = 0; const uint8_t *hit = NULL; uint32_t n;
    for (int i = 0; i < draft_count(dd); i++) { const uint8_t *E = draft_record(dd, i, &n);
        if (E && n == 0x810 && E[0] == 0x40 && E[0x308] == 4) { if (k == want) hit = E; k++; } }
    if (count) *count = k;
    return hit;
}
/* ptregen (FUN_1803042b0 case 4 -> FUN_180305b40 for every session / open-part point dimension: the globals from the record, the
   cursor from its offsets, calc_point_dimension into a copy; the copy compared with the record), ptdump, ptfile, ptopt:K=V (use3d /
   label / abs: the config globals), ptpanel:K=V (arrows / center), ptglobals:N, ptredo:N (a placement mouse move at the open part's point
   dimension N's regeneration cursor) */
static int regen_one(const uint8_t *Es, char *why, size_t wn) {
    DcSettings save = dr_S, saved2 = dr_saved; int u = gp_use3d, l = gp_label, a = gp_abs;
    dp_globals_from(Es);
    DcPtInput in; memset(&in, 0, sizeof in); memcpy(in.ap, Es + 0x68, 0x98); in.view = I16_(Es, 0x328); mcam_std_view(in.view ? in.view : 1, in.vm);
    in.use3d = gp_use3d; in.label = gp_label; in.absolute = gp_abs;
    dc_point_regen_cursor(Es, in.vm, in.cursor);
    char tb[0x60]; if (Es[0x7a0]) { memcpy(tb, Es + 0x650, 0x58); tb[0x58] = 0; in.tmpl = tb; }
    uint8_t E[0x810]; memcpy(E, Es, 0x810);
    DcSettings S = dr_S; S.color = Es[6]; { int32_t lv; memcpy(&lv, Es + 8, 4); S.level = lv; } memcpy(S.attrib, Es + 0x10, 16);
    DcResult R = dc_calc_point(E, &in, &S, dr_fonts_get());
    dr_S = save; dr_saved = saved2; gp_use3d = u; gp_label = l; gp_abs = a;
    if (!R.valid) { snprintf(why, wn, "%s", R.why ? R.why : "invalid"); return 0; }
    for (int o = 0; o < 0x810; o++) if (E[o] != Es[o]) {
        if (o >= 0x20 && o < 0x28) continue;                /* the eptr (the save writes it) */
        static const int dr[4][2] = { { 0x310, 0x328 }, { 0x360, 0x378 }, { 0x638, 0x650 }, { 0x4c8, 0x528 } };   /* cursor-derived doubles */
        int b = -1; for (int k = 0; k < 4; k++) if (o >= dr[k][0] && o < dr[k][1]) b = dr[k][0] + (o - dr[k][0]) / 8 * 8;
        if (b >= 0 && fabs(D8(E, b) - D8(Es, b)) <= 1e-9 * (1 + fabs(D8(Es, b)))) { o = b + 7; continue; }
        snprintf(why, wn, "byte +0x%x", o); return 0;
    }
    return 1;
}
int dp4_script(const char *st, const char *arg) {
    if (strncmp(st, "pt", 2)) return 0;
    if (!strcmp(st, "ptdump")) { int k = 0; for (int i = 0; i < F.ndnew; i++) if (F.dnew[i].on && F.dnew[i].E[0x308] == 4) { char tag[32]; snprintf(tag, sizeof tag, "P%d%s", k++, i == dr_live ? " (live)" : ""); pt_line(tag, F.dnew[i].E); }
        if (dr_placing && dr_drag_ok) pt_line("drag", dr_drag); return 1; }
    if (!strcmp(st, "ptfile")) { DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; int n = 0; file_pt(dd, -1, &n);
        for (int k = 0; k < n; k++) { char tag[32]; snprintf(tag, sizeof tag, "file P%d", k); pt_line(tag, file_pt(dd, k, NULL)); }
        if (!n) printf("    (no point dimension in the open part)\n"); draft_free(dd); return 1; }
    if (!strcmp(st, "ptregen")) {
        int n = 0, eq = 0; char why[200];
        for (int i = 0; i < F.ndnew; i++) if (F.dnew[i].on && F.dnew[i].E[0x308] == 4) { n++; if (regen_one(F.dnew[i].E, why, sizeof why)) eq++; else printf("    ptregen session P%d: %s\n", n - 1, why); }
        DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; int nf = 0, eqf = 0; file_pt(dd, -1, &nf);
        for (int k = 0; k < nf; k++) { if (regen_one(file_pt(dd, k, NULL), why, sizeof why)) eqf++; else printf("    ptregen file P%d: %s\n", k, why); }
        draft_free(dd);
        printf("    ptregen: session %d / %d, file %d / %d regenerate equal\n", eq, n, eqf, nf); return 1; }
    if (!arg) { printf("    (%s: argument missing)\n", st); return 1; }
    if (!strcmp(st, "ptopt") || !strcmp(st, "ptpanel")) {
        char k[32] = ""; const char *eq = strchr(arg, '='); int v = eq ? atoi(eq + 1) : 0;
        snprintf(k, sizeof k, "%.*s", eq ? (int)(eq - arg) : (int)strlen(arg), arg);
        if (!strcmp(st, "ptopt")) { gp_load(); if (!strcmp(k, "use3d")) gp_use3d = v; else if (!strcmp(k, "label")) gp_label = v; else if (!strcmp(k, "abs")) gp_abs = v; else printf("    (unknown option %s)\n", k); return 1; }
        pp_arrows = dr_S.arrows_out == 1; pp_center = dr_S.auto_align;
        if (!strcmp(k, "arrows")) pp_arrows = v; else if (!strcmp(k, "center")) pp_center = v; else printf("    (unknown panel value %s)\n", k);
        dp4_panel_sync(); return 1;
    }
    if (!strcmp(st, "ptglobals") || !strcmp(st, "ptredo")) {
        DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *Es = file_pt(dd, atoi(arg), NULL);
        if (!Es) printf("    (no point dimension %s)\n", arg);
        else if (!strcmp(st, "ptglobals")) { dr_globals_load(); dp_globals_from(Es); }
        else if (!dr_placing) printf("    (ptredo: no placement)\n");
        else { double vm[9], c[3]; mcam_std_view(I16_(Es, 0x328) ? I16_(Es, 0x328) : 1, vm); dc_point_regen_cursor(Es, vm, c); dp_compute(c, 0); }
        draft_free(dd); return 1;
    }
    return 0;
}
