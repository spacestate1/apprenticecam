/* cui_draft_ang.c - the Drafting tab's Angular command (doDraft_DIMENSION_ANGULAR 0x9472, type 1) in the dimension command loop
 * (cui_draft.c / cui_draft_dim.c dispatch here): the picks of FUN_180049ee0 / FUN_18004a620 / FUN_18004b070 (two lines, a line + a
 * point + a typed angle, three points, an arc), the set-up FUN_180042a20 / FUN_180042040 / FUN_180042750 / FUN_180042bf0 /
 * FUN_180042210, the placement drag FUN_180041c20 (determine_ang_quad, calc_ang_dimension = viewer/draft_create_ang.c), accept
 * FUN_180042930 -> FUN_180041f30(1). Spec: ../../formats/drafting/create.md 10; decompiles decomp/draft/ (cad_linear.c, ang_cad_a1.c,
 * ang_cad_a2.c, circ_cad.c). "ours" marks glue of this program. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "cui_draft_int.h"
#include "draft_create_ang.h"
#include "mcam_saveops.h"
#include "draft.h"

#define PI_ 3.141592653589793
#define TWOPI_ 6.283185307179586
#define TOL_ (mc_systol.tol)                                      /* mccore tol (config 1000, mc_systol) */

double cui_script_real; int cui_script_real_ok = 1;
int dcx_l_ints(const double *L1, const double *L2, double *pt);     /* draft_create.c (l_ints @1804b3480) */
int dr_l_ints(const double *L1, const double *L2, double *pt) { return dcx_l_ints(L1, L2, pt); }

/* ------------------------------------------------------------------------------------------------ state (mccad globals) */
static int da_n;                                        /* DAT_1806960e8 picks */
static int da_e6;                                       /* DAT_1806960e6: the first pick was a line */
static uint8_t da_slot[4][0x98]; static int da_sent[4]; /* DAT_180698a30 / ac8 / b60 / bf8 (+ the entity each references, -1) */
static double da_typed;                                 /* DAT_180698cb0: the last typed angle (radians; .bss 0 at start) */
static DcAngInput da_in;                                /* the record-order aPoints, lines, vlines, quadrant, flip ... */
static double da_vertex[2], da_ang[4]; static short da_code[4];
static int da_lock, da_first;                           /* DAT_1806a4c20 (-> DAT_180692744), DAT_180649626 */
static int da_arc; static double da_arcmid[3];          /* DAT_180649630 (0 / 1: an arc, sweep > pi; 2 none) and DAT_180692748 */
static DcAngInput da_live_in; static int da_live_ok;
/* the angular globals calc_ang_dimension reads beside DcSettings (config writers core_cfgdraft.c:730-750, 909) */
static int ga_fmt, ga_digits, ga_tolm, ga_dtol, ga_dchr, ga_dval, ga_pig, ga_units = -1; static double ga_ptol, ga_ntol;
static double cfgd_(int key, double def) { char v[128]; mcam_config_value(dr_S.metric, key, v, sizeof v); return v[0] ? atof(v) : def; }
static int cfgi_(int key, int def) { char v[128]; mcam_config_value(dr_S.metric, key, v, sizeof v); if (!v[0]) return def; if (v[0] == 'y' || v[0] == 'Y') return 1; if (v[0] == 'n' || v[0] == 'N') return 0; return atoi(v); }
static void ga_load(void) {
    if (ga_units == dr_S.metric) return;
    ga_units = dr_S.metric;
    ga_fmt = cfgi_(11000, 0); ga_dchr = cfgi_(11001, 0); ga_dtol = cfgi_(11002, 0); ga_dval = cfgi_(11003, 0);   /* DAT_1813d5624, angular_din_* */
    ga_tolm = cfgi_(11007, 0); if (ga_tolm == 3) ga_tolm = 1; else if (ga_tolm > 3) ga_tolm = 0;                   /* DAT_1813d620e (InitDM's clamp) */
    ga_ntol = cfgd_(11016, 0); ga_digits = cfgi_(11019, 0); ga_ptol = cfgd_(11020, 0); ga_pig = cfgi_(14011, 0);   /* df_ang_add_pigtail */
}

/* the prompts: FUN_180049560 type 1 (cad_linear.c:399-483): 27720 "Create dimension, angular:" + 0 picks 27721 / 27722; 1 pick (a point)
   27710; 1 pick (a line) 27723 / 27728; 2 picks 27724. Placement FUN_1800422d0 (ang_cad_a1.c 105): 27720 + 27055 (+ 27070 '~': left out,
   NOT PORTED - as cui_draft.c) */
void da_prompt(void) {
    if (dr_placing) snprintf(cam_msg, sizeof cam_msg, "Create dimension, angular:\nSketch the text position for the angular dimension");
    else if (dr_live >= 0 && !da_n) snprintf(cam_msg, sizeof cam_msg, "Modify current dimension or begin creating a new dimension");
    else if (!da_n) snprintf(cam_msg, sizeof cam_msg, "Create dimension, angular:\nSelect an arc (non-circle) to create the angular dimension\nSelect a line or the pivot point");
    else if (da_n == 1 && !da_e6) snprintf(cam_msg, sizeof cam_msg, "Create dimension, angular:\nSelect the second point");
    else if (da_n == 1) snprintf(cam_msg, sizeof cam_msg, "Create dimension, angular:\nSelect another line (non-parallel) to create the angular dimension\nSelect base point for relative method");
    else snprintf(cam_msg, sizeof cam_msg, "Create dimension, angular:\nSelect the final point to create the angular dimension");
}
/* FUN_180048b10(1, -1) -> FUN_18004b820: InitDIM(drag), DfHvpAngle 0, no picks; FUN_180049c60 case 1: flags 0xb, mask 7 */
void da_restart(void) {
    da_n = 0; da_e6 = 0; dr_npick = 0; dr_placing = 0; dr_drag_ok = 0; dr_Pent[0] = dr_Pent[1] = -1;
    for (int i = 0; i < 4; i++) da_sent[i] = -1;
    ga_load();
    dc_init_dim(dr_drag, &dr_S, dr_view_no());
    da_prompt(); dr_scene();
}
/* get_event's needs (FUN_180049c60 case 1): 0 picks: points + lines + arcs (mask 7); after a line: points + lines (mask 3); after a
   point: points only (flags 9); placement: a position */
int da_need(void) { return dr_placing ? 't' : (da_n == 1 && !da_e6) || da_n == 2 ? 'p' : 'e'; }

static void w2v_(const double w[3], double v[3]) { for (int i = 0; i < 3; i++) v[i] = dr_view[3 * i] * w[0] + dr_view[3 * i + 1] * w[1] + dr_view[3 * i + 2] * w[2]; }
static void v2w_(const double v[3], double w[3]) { for (int i = 0; i < 3; i++) w[i] = dr_view[i] * v[0] + dr_view[3 + i] * v[1] + dr_view[6 + i] * v[2]; }
static void ap_ref(uint8_t ap[0x98], int e) {           /* the entity an aPoint references: its eptr, or (new geometry, no eptr yet) the session
                                                           token the save replaces (mcam_saveops.c; ours) */
    uint64_t ep = e >= 0 && F.ents[e].eptr ? (uint64_t)F.ents[e].eptr : e >= 0 ? MCAM_EPTR_TOKEN((uint64_t)e) : 0; memcpy(ap + 0x28, &ep, 8);
}

/* FUN_180042a20 (circ_cad.c) / FUN_180042040 / FUN_180042750 / FUN_180042bf0 / FUN_180042210 (ang_cad_a1.c, ang_cad_a2.c): InitDIM,
   vlines off, DfHvpAngle 0, lock 0, the slots copied, +0x2e8 = 0xc; the record order (FLAG 0: [s0, s2, s1, s3]; 1: [s1, s0, s0, s2]);
   df_angular_set_ep_angles (27258 when it fails); the quadrant: an arc -> arrows inside, flip = sweep > pi, lock on, determine_ang_quad
   at the chord middle; else ang_code2quad(0xc) (no quadrant with that code: mccoreres 27015 "Warning - invalid quadrant or code.",
   FUN_180242fa0 -> FUN_180242ff0 dbmmess_mbok, quadrant 1) */
static int da_setup(int flag) {
    dc_init_dim(dr_drag, &dr_S, dr_view_no()); dr_drag[0x308] = 1;
    da_lock = 0; da_first = 1; da_in.flip = 0;
    memset(&da_in, 0, sizeof da_in);
    static const int o0[4] = { 0, 2, 1, 3 }, o1[4] = { 1, 0, 0, 2 };
    const int *o = flag ? o1 : o0;
    for (int k = 0; k < 4; k++) memcpy(da_in.ap[k], da_slot[o[k]], 0x98);
    da_in.view = dr_view_no(); memcpy(da_in.vm, dr_view, sizeof da_in.vm);
    if (!dc_ang_set_ep((const uint8_t (*)[0x98])da_in.ap, da_in.vm, da_in.lines, da_vertex, da_ang, da_code, &da_in.vl)) {
        snprintf(cam_msg, sizeof cam_msg, "Warning - cannot calculate angular orientation"); return 0; }   /* 27258 (0x6a7a) */
    if (da_arc == 0 || da_arc == 1) {                    /* FUN_180042210 */
        dr_S.arrows_out = 0; da_first = 0; da_in.flip = da_arc == 1; da_lock = 1;
        double mv[3]; w2v_(da_arcmid, mv); int code;
        da_in.quad = dc_ang_quad(da_vertex, mv, da_ang, da_code, &code);
        da_arc = 2;                                     /* FUN_180042280 */
    } else {
        int q = 0; for (int k = 1; k < 5 && !q; k++) if (dc_ang_quad_code(k, da_code) == 0xc) q = k;
        if (!q) { if (cui_hooks.message) cui_hooks.message("Mastercam", "Warning - invalid quadrant or code."); q = 1; }
        da_in.quad = q;
    }
    dr_placing = 1; dr_drag_ok = 0; dr_npick = 2;
    da_prompt();
    return 1;
}

/* FUN_180049ee0 type 1 (cad_linear.c:615-830): a point event */
static void da_point(const uint8_t ap[0x98], int ent) {
    if (da_n == 0) { memcpy(da_slot[0], ap, 0x98); da_sent[0] = ent; da_n = 1; da_e6 = 0; da_prompt(); return; }
    if (da_n == 1 && !da_e6) { memcpy(da_slot[1], ap, 0x98); da_sent[1] = ent; da_n = 2; da_prompt(); return; }
    if (da_n == 2) {                                    /* the final point: FUN_1800426f0 -> FUN_180042a20(slots, 1) */
        memcpy(da_slot[2], ap, 0x98); da_sent[2] = ent; da_arc = 2;
        if (!da_setup(1)) { da_n = 2; }
        return;
    }
    /* a line, then a point: 27833 "Enter angle in degrees" (get_angle -> RequestRealInputModal, default the last angle); OK: the second
       line from the point along the angle (restrict_angle 0..2 pi), 100 x tol long in the view; slot 3 = slot 2 with +0x58 = 1, +0x60 =
       the base point, +0x78 = the view offset (summed when +0x58 was set), world = base + view_to_world(offset); l_ints of the two lines,
       none -> 27832; DfAssoc off -> all four 'K'; FUN_180042720 -> FUN_180042a20(slots, 0). Cancel: the picks stay */
    double a = da_typed * 57.29577951308233;
    snprintf(cam_msg, sizeof cam_msg, "Enter angle in degrees");
    int ok = cui_hooks.ask_real ? cui_hooks.ask_real("Enter angle in degrees", &a) : (cui_script_real_ok ? (a = cui_script_real, 1) : 0);
    if (!ok) { da_prompt(); return; }
    da_typed = a * 0.01745329251994329;
    while (da_typed > TWOPI_) da_typed -= TWOPI_;
    while (da_typed < 0.0) da_typed += TWOPI_;
    memcpy(da_slot[2], ap, 0x98); da_sent[2] = ent;
    double p[3], pv[3]; memcpy(p, da_slot[2] + 8, 24); w2v_(p, pv);
    double dx = cos(da_typed) * TOL_ * 100.0, dy = sin(da_typed) * TOL_ * 100.0, q2[3] = { pv[0] + dx, pv[1] + dy, pv[2] };
    memcpy(da_slot[3], da_slot[2], 0x98); da_sent[3] = ent;
    double base[3], off[3], offw[3];
    if (!da_slot[2][0x58]) { memcpy(base, da_slot[3] + 8, 24); off[0] = dx; off[1] = dy; off[2] = 0.0; }
    else { memcpy(base, da_slot[3] + 0x60, 24); memcpy(off, da_slot[3] + 0x78, 24); off[0] += dx; off[1] += dy; }
    memcpy(da_slot[3] + 0x60, base, 24); memcpy(da_slot[3] + 0x78, off, 24);
    v2w_(off, offw); double w[3] = { base[0] + offw[0], base[1] + offw[1], base[2] + offw[2] }; memcpy(da_slot[3] + 8, w, 24);
    da_slot[3][0x58] = 1;
    double l1v[2][3], L1[4], L2[4], ip[2]; double s0[3], s1[3]; memcpy(s0, da_slot[0] + 8, 24); memcpy(s1, da_slot[1] + 8, 24); w2v_(s0, l1v[0]); w2v_(s1, l1v[1]);
    L1[0] = l1v[0][0]; L1[1] = l1v[0][1]; L1[2] = l1v[1][0]; L1[3] = l1v[1][1]; L2[0] = pv[0]; L2[1] = pv[1]; L2[2] = q2[0]; L2[3] = q2[1];
    if (!dr_l_ints(L1, L2, ip)) { snprintf(cam_msg, sizeof cam_msg, "Warning - intersection not found"); return; }   /* 27832 */
    if (!dr_assoc) for (int k = 0; k < 4; k++) { double x[3]; memcpy(x, da_slot[k] + 8, 24); dc_apoint_world(da_slot[k], x, 'K'); da_sent[k] = -1; }
    da_arc = 2;
    da_setup(0);
}
/* a line picked (FUN_18004a620 case 2 / FUN_18004b070, cad_linear.c:2934-2964 / 3280-3320): 27013 when its ends coincide in the view;
   the first: slots 0 / 1 '1' / '2' (t 0 / 1), e6 = 1; the second: slots 2 / 3, l_ints with the first (none: 27832), FUN_180042a20(0) */
static void da_line(int e) {
    const Ent *en = &F.ents[e];
    double a[3] = { en->a.x, en->a.y, en->a.z }, b[3] = { en->b.x, en->b.y, en->b.z }, av[3], bv[3];
    w2v_(a, av); w2v_(b, bv);
    if (fabs(av[0] - bv[0]) < 5e-5 && fabs(av[1] - bv[1]) < 5e-5) { snprintf(cam_msg, sizeof cam_msg, "Line is parallel to Z axis."); return; }
    int s = da_n == 0 ? 0 : 2;
    dc_apoint_world(da_slot[s], a, dr_assoc ? '1' : 'K'); dc_apoint_world(da_slot[s + 1], b, dr_assoc ? '2' : 'K');
    da_sent[s] = da_sent[s + 1] = -1;
    if (dr_assoc) { dc_apoint_ent(da_slot[s], 0, 0, 0.0); dc_apoint_ent(da_slot[s + 1], 0, 0, 1.0); ap_ref(da_slot[s], e); ap_ref(da_slot[s + 1], e); da_sent[s] = da_sent[s + 1] = e; }
    if (da_n == 0) { da_n = 1; da_e6 = 1; da_prompt(); return; }
    double p[4][3]; for (int k = 0; k < 4; k++) { double w[3]; memcpy(w, da_slot[k] + 8, 24); w2v_(w, p[k]); }
    double L1[4] = { p[0][0], p[0][1], p[1][0], p[1][1] }, L2[4] = { p[2][0], p[2][1], p[3][0], p[3][1] }, ip[2];
    if (!dr_l_ints(L1, L2, ip)) { snprintf(cam_msg, sizeof cam_msg, "Warning - intersection not found"); return; }   /* 27832 */
    da_arc = 2;
    da_setup(0);
}
/* an arc picked (FUN_18004a620 case 4, cad_linear.c:2985-3036): |sweep| >= 2 pi or ~ pi -> 27834; slots 'C' centre / '1' start t 0 /
   '2' end t 1 ('K' x 3 with DfAssoc off); FUN_1800422a0(sweep > pi, the chord middle); FUN_1800426f0 -> FUN_180042a20(slots, 1). The
   'C' slot's t is a stack value in Mastercam (adStack_80[2]): 0 here */
static void da_arc_pick(int e) {
    const Ent *en = &F.ents[e];
    if (en->vw || !en->planar) { snprintf(cam_msg, sizeof cam_msg, "Warning - arc is not parallel to construction plane."); return; }   /* 27012 */
    double sw = fabs(en->sw);
    if (TWOPI_ < sw || sw == TWOPI_ || fabs(sw - PI_) <= 1e-06) { snprintf(cam_msg, sizeof cam_msg, "Warning - cannot dimension a circle"); return; }   /* 27834 */
    double c[3] = { en->c.x, en->c.y, en->c.z }, a[3] = { en->a.x, en->a.y, en->a.z }, b[3] = { en->b.x, en->b.y, en->b.z };
    dc_apoint_world(da_slot[0], c, dr_assoc ? 'C' : 'K'); dc_apoint_world(da_slot[1], a, dr_assoc ? '1' : 'K'); dc_apoint_world(da_slot[2], b, dr_assoc ? '2' : 'K');
    for (int k = 0; k < 3; k++) da_sent[k] = -1;
    if (dr_assoc) { dc_apoint_ent(da_slot[1], 0, 0, 0.0); dc_apoint_ent(da_slot[2], 0, 0, 1.0); for (int k = 0; k < 3; k++) { ap_ref(da_slot[k], e); da_sent[k] = e; } }
    da_arcmid[0] = (a[0] + b[0]) * 0.5; da_arcmid[1] = (a[1] + b[1]) * 0.5; da_arcmid[2] = (a[2] + b[2]) * 0.5;
    da_arc = PI_ < sw ? 1 : 0;
    da_n = 3; da_setup(1);
}

/* FUN_180041c20 (cad_linear.c:1633-1719): a mouse move */
static int da_compute(const double cur[3], int ctrl) {
    double tv[3]; w2v_(cur, tv); int code;
    int q = dc_ang_quad(da_vertex, tv, da_ang, da_code, &code);   /* determine_ang_quad */
    if (!da_lock || da_first) da_in.quad = q;
    memcpy(da_in.cursor, cur, sizeof da_in.cursor);
    da_in.pigtail = ga_pig; da_in.ang_fmt = ga_fmt; da_in.digits = ga_digits; da_in.tol_mode = ga_tolm; da_in.pos_tol = ga_ptol; da_in.neg_tol = ga_ntol;
    da_in.din_tol = ga_dtol; da_in.din_chr = ga_dchr; da_in.din_val = ga_dval; da_in.metric = dr_S.metric;
    memcpy(&da_in.text_h, dr_S.fa + 0x28, 8); memcpy(&da_in.extra_ln_sp, dr_S.fa + 0x40, 8);   /* dim_text_height / extra_ln_sp (the font's) */
    da_in.ctrl = ctrl;
    DcSettings S = dr_S; S.sel_dist = SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1); S.color = geo_color; S.level = geo_level;
    DcResult R = dc_calc_ang(dr_drag, &da_in, &S, dr_fonts_get());
    if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); dr_drag_ok = 0; dr_scene(); return -1; }
    dr_drag_ok = R.valid;
    if (!R.valid) {                                     /* 27010 + FUN_180041b10 (the dimension abandoned, vlines off) */
        snprintf(cam_msg, sizeof cam_msg, "Warning - cannot calculate dimension");
        da_n = 0; da_e6 = 0; dr_npick = 0; dr_placing = 0; dr_scene();
        return 0;
    }
    da_first = 0;
    dr_scene();
    return 1;
}
int da_move(const double world[3], int ctrl) {
    if (!dr_placing) return 0;
    double c[3] = { world[0], world[1], grid_z() };
    da_compute(c, ctrl);
    return 1;
}
/* a left click: placement -> FUN_180042930 -> FUN_180041f30(1) (store, as cui_draft_dim.c); else the AutoCursor point (get_event mode
   bit 1, FUN_1804e3b90 as cui_draft.c dr_click), a line (mask 2 / 3), an arc (first pick only, mask 7) or a 'K' point */
int da_click(float mx, float my, const double w[3], int ctrl) {
    if (dr_placing) {
        if (!dr_drag_ok) { double c[3]; dr_sketched(mx, my, w, c); if (da_compute(c, ctrl) <= 0) return 1; }
        da_live_in = da_in; da_live_ok = 1;
        dr_Pent[0] = da_sent[0]; dr_Pent[1] = -1;       /* the save patches every aPoint's entity token itself (mcam_saveops.c) */
        dr_store(); da_restart();
        return 1;
    }
    if (dr_live >= 0) dr_commit_live();
    V3 p = { w[0], w[1], grid_z() };
    geo_snap_ptr(mx, my); int sn = !ctrl && geo_snap(&p, NULL, -1, 1); geo_snap_ptr_off();
    uint8_t ap[0x98]; int ent = -1;
    if (sn) { dr_ac_apoint(ap, &ent, p); if (ent >= 0) ap_ref(ap, ent); da_point(ap, ent); }
    else {
        int e = (da_n == 0 || (da_n == 1 && da_e6)) ? dr_pick_line(mx, my) : -1;
        int ea = da_n == 0 && e < 0 ? dd_pick_arc_any(mx, my) : -1;
        if (e >= 0) da_line(e);
        else if (ea >= 0) da_arc_pick(ea);
        else { double q[3] = { w[0], w[1], grid_z() }; dc_apoint_world(ap, q, 'K'); da_point(ap, -1); }
    }
    if (!dr_placing && strncmp(cam_msg, "Warning", 7) && strncmp(cam_msg, "Line", 4)) da_prompt();
    dr_scene();
    return 1;
}
/* panel (FUN_180042530 refresh: 1 arrows, 2 witness, 3 centred, 0xc digits (num_digits_ang), 0xf box, 7 lock, 0x10 flip; FUN_180042620
   keys). Live: Leaders Inside / Outside (FUN_18012af20 case 1), Centered text (3), Loc_k (7). Greyed: witness (the Both / Right / Left
   order is a hypothesis, create.md 2.5), the digits (id 0xc goes to FUN_18012bca0, its body NOT DECODED for angular), id 0x10 (which
   panel control posts 16: NOT DECODED), the rest as Circular */
static int pa_arrows, pa_center, pa_lock, pa_dummy;
void da_panel_body(const CuiPanelOps *ops, void *u) {
    pa_arrows = dr_S.arrows_out == 1; pa_center = dr_S.auto_align; pa_lock = da_lock;
    int t = -1, dg = ga_digits; double ang = 0;
    if (!ops->tab || ops->tab(u, 0, "Basic")) {
        if (ops->exp(u, "Entity")) { ops->label(u, "Mode:"); ops->radio(u, "_Auto", &t, 0, 0); ops->radio(u, "_Horizontal", &t, 1, 0); ops->radio(u, "_Vertical", &t, 2, 0);
            ops->num(u, "Orientation an_gle:", "angle", &ang, NULL, 0, -360, 360); ops->check(u, "_Maintain perpendicular to arc center", &pa_dummy, 0);
            ops->check(u, "Loc_k", &pa_lock, 1); ops->end(u); }
        if (ops->exp(u, "Arc Symbol")) { ops->radio(u, "Radi_us", &t, 0, 0); ops->radio(u, "_Diameter", &t, 1, 0); ops->check(u, "A_pply to linear dimension", &pa_dummy, 0); ops->end(u); }
        if (ops->exp(u, "_Witness lines")) { ops->radio(u, "_Both", &t, 0, 0); ops->radio(u, "_Right", &t, 1, 0); ops->radio(u, "_Left", &t, 2, 0); ops->end(u); }
        if (ops->exp(u, "Leaders")) { ops->label(u, "Arrows:"); ops->radio(u, "_Inside", &pa_arrows, 0, 1); ops->radio(u, "_Outside", &pa_arrows, 1, 1); ops->end(u); }
        if (ops->exp(u, "Format")) { ops->toggle(u, "_Font", 1, -1); ops->toggle(u, "_Edit text", 1, -1); ops->toggle(u, "Heigh_t", 1, -1);
            ops->inum(u, "De_cimal places:", "digits", &dg, NULL, 0, 0, 8); ops->end(u); }
    }
    if (!ops->tab || ops->tab(u, 1, "Advanced")) {
        if (ops->exp(u, "Dimension Text")) { ops->check(u, "_Centered text", &pa_center, 1); ops->label(u, "Edit Mode:");
            ops->radio(u, "P_lace", &t, 0, 0); ops->radio(u, "_Edit", &t, 1, 0); ops->end(u); }
        if (ops->exp(u, "Dimension Box")) { ops->check(u, "_Show when dragging", &pa_dummy, 0); ops->end(u); }
        if (ops->exp(u, "Settings")) { ops->toggle(u, "_Options", 1, -1); ops->toggle(u, "_Update", 1, -1); ops->end(u); }
    }
}
void da_panel_sync(void) {
    int ch = 0;
    if ((pa_arrows ? 1 : 0) != (dr_S.arrows_out == 1)) { dr_S.arrows_out = pa_arrows ? 1 : 0; ch = 1; }
    if (pa_center != dr_S.auto_align) { dr_S.auto_align = pa_center; ch = 1; }
    if (pa_lock != da_lock) da_lock = pa_lock;
    if (!ch) return;
    if (dr_live >= 0 && !dr_placing && da_live_ok && F.dnew[dr_live].E[0x308] == 1) {   /* the live entity recomputed (as cui_draft.c) */
        DcSettings S = dr_S; S.sel_dist = SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1); S.color = F.dnew[dr_live].E[6]; S.level = (int)F.dnew[dr_live].E[8];
        uint8_t E[0x810]; memcpy(E, F.dnew[dr_live].E, 0x810);
        DcResult R = dc_calc_ang(E, &da_live_in, &S, dr_fonts_get());
        if (R.valid) memcpy(F.dnew[dr_live].E, E, 0x810);
        dr_scene();
    }
}
/* ------------------------------------------------------------------------------------------------ tests (ours) */
static double D8(const uint8_t *b, int o) { double v; memcpy(&v, b + o, 8); return v; }
static int I16_(const uint8_t *b, int o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static const uint8_t *file_dim(DraftDoc *dd, int type, int want, int *count) {   /* the open part's dimension records of TYPE */
    int k = 0; const uint8_t *hit = NULL; uint32_t n;
    for (int i = 0; i < draft_count(dd); i++) { const uint8_t *E = draft_record(dd, i, &n);
        if (E && n == 0x810 && E[0] == 0x40 && E[0x308] == type) { if (k == want) hit = E; k++; } }
    if (count) *count = k;
    return hit;
}
/* the session globals from a record (DraftGlobalsFromDim @180308cc0 core_draftwf.c 540-720, as tools/tests/draft_ang_test.c) */
static void da_globals_from(const uint8_t *Es) {
    DcSettings *S = &dr_S;
    memcpy(S->fa, Es + 0x5d8, 0x60);
    const uint8_t *l1 = Es + 0x4a8, *l2 = Es + 0x540, *l = l1[0] && l1[0x18] != 3 ? l1 : l2[0] && l2[0x18] != 3 ? l2 : l1[0] ? l1 : l2;
    S->arrow_h = D8(l, 8); S->arrow_w = D8(l, 0x10); S->arrow_type = l[0x18];
    S->arrows_out = I16_(Es, 0x7b0); S->ldr_style = I16_(Es, 0x7b4); S->wit_vis = I16_(Es, 0x7ac); S->ldr_vis = I16_(Es, 0x7ae);
    S->text_orient = fabs(D8(Es, 0x5e8)) < 5e-5; S->auto_align = Es[0x7c9];
    S->gap = D8(Es, 0x350); S->ext = D8(Es, 0x358); S->scale = D8(Es, 0x330); S->frac_units = I16_(Es, 0x7c2); S->lead0 = Es[0x7cc]; S->trail0 = Es[0x7cf]; S->comma = Es[0x7cb];
    ga_load(); ga_fmt = I16_(Es, 0x7c0); ga_digits = Es[0x7cd]; ga_pig = Es[0x2f8]; int tm = I16_(Es, 0x7b6); ga_tolm = tm == 3 ? 1 : tm > 3 ? 0 : tm;
    ga_ptol = D8(Es, 0x340); ga_ntol = D8(Es, 0x348); ga_dtol = Es[0x7c6]; ga_dchr = Es[0x7c7]; ga_dval = Es[0x7c8];
    dr_saved = dr_S;
}
static void da_cmp(int want) {
    DraftDoc *dd = draft_load(F.path); int n = 0; const uint8_t *Es = file_dim(dd, 1, want, &n);
    if (!Es || !F.ndnew) { printf("    angcmp: nothing to compare (%d file dims, %d made)\n", n, F.ndnew); draft_free(dd); return; }
    const uint8_t *Eo = F.dnew[F.ndnew - 1].E; int nd = 0, nref = 0; char why[2000] = ""; size_t L = 0;
#define DF(...) do { nd++; L += (size_t)snprintf(why + L, L < sizeof why ? sizeof why - L : 0, __VA_ARGS__); } while (0)
#define CLOSE(a, b) (fabs((a) - (b)) <= 1e-6 * (1 + fabs(b)))
    for (int j = 0; j < 4; j++) { const uint8_t *a = Eo + 0x68 + 0x98 * j, *b = Es + 0x68 + 0x98 * j;
        for (int k = 0; k < 3; k++) if (!CLOSE(D8(a, 8 + 8 * k), D8(b, 8 + 8 * k))) { DF(" aP%d position;", j); break; }
        if (a[0] != b[0] || memcmp(a + 0x28, b + 0x28, 8)) nref++; }
    if (!CLOSE(D8(Eo, 0x338), D8(Es, 0x338))) DF(" value %.6f/%.6f;", D8(Eo, 0x338), D8(Es, 0x338));
    if (strncmp((const char *)Eo + 0x650, (const char *)Es + 0x650, 0x58)) DF(" text '%.20s'/'%.20s';", Eo + 0x650, Es + 0x650);
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x638 + 8 * k), D8(Es, 0x638 + 8 * k))) { DF(" textpos (%.5f,%.5f)/(%.5f,%.5f);", D8(Eo, 0x638), D8(Eo, 0x640), D8(Es, 0x638), D8(Es, 0x640)); break; }
    if (!CLOSE(D8(Eo, 0x5e8), D8(Es, 0x5e8))) DF(" textangle;");
    static const int wo[4] = { 0x378, 0x410, 0x4a8, 0x540 }; static const char *wn[4] = { "wit1", "wit2", "arc1", "arc2" };
    for (int q = 0; q < 4; q++) { const uint8_t *a = Eo + wo[q], *b = Es + wo[q];
        int bad = a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3] || a[0x18] != b[0x18] || I16_(a, 0x1a) != I16_(b, 0x1a);
        int nv = b[1] >= 2 ? 7 : (I16_(b, 0x1a) > 4 ? 4 : I16_(b, 0x1a)) * 3;   /* arc: centre + radius / start / sweep */
        for (int i = 0; !bad && i < nv; i++) bad = !CLOSE(D8(a, 0x20 + 8 * i), D8(b, 0x20 + 8 * i));
        if (bad) DF(" %s;", wn[q]); }
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x310 + 8 * k), D8(Es, 0x310 + 8 * k))) { DF(" placement;"); break; }
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x360 + 8 * k), D8(Es, 0x360 + 8 * k))) { DF(" offsets;"); break; }
    if (Eo[0x2e8] != Es[0x2e8] || Eo[0x2e9] != Es[0x2e9] || Eo[0x2ea] != Es[0x2ea]) DF(" quadrant %d/%d flip %d/%d;", Eo[0x2e8], Es[0x2e8], Eo[0x2e9], Es[0x2e9]);
#undef DF
#undef CLOSE
    printf("    angcmp %d: %s%s%s\n", want, nd ? "DIFFER" : nref ? "EQUAL-LAYOUT" : "EQUAL", why, nref ? " (aPoint references differ)" : "");
    draft_free(dd);
}
/* dimreal:V | dimreal:cancel (the answer of the next get_angle / get_real), angglobals:N, angredo:N (a placement mouse move at the
   point FUN_180303b70's regeneration gives the open part's angular dimension N: dc_ang_regen_cursor about the vertex of the session's
   picks), angcmp:N */
int da_script(const char *st, const char *arg) {
    if (!strcmp(st, "dimreal") && arg) { if (!strcmp(arg, "cancel")) cui_script_real_ok = 0; else { cui_script_real_ok = 1; cui_script_real = atof(arg); } return 1; }
    if (strcmp(st, "angglobals") && strcmp(st, "angredo") && strcmp(st, "angcmp")) return 0;
    if (!arg) { printf("    (%s: argument missing)\n", st); return 1; }
    if (!strcmp(st, "angcmp")) { if (loaded && F.path[0]) da_cmp(atoi(arg)); else printf("    (no part file)\n"); return 1; }
    DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *Es = file_dim(dd, 1, atoi(arg), NULL);
    if (!Es) printf("    (no angular dimension %s)\n", arg);
    else if (!strcmp(st, "angglobals")) { dr_globals_load(); da_globals_from(Es); }
    else if (!dr_placing) printf("    (angredo: no placement)\n");
    else { double cv[3], cw[3]; dc_ang_regen_cursor(Es, da_vertex, da_ang, da_code, cv); v2w_(cv, cw); da_compute(cw, 0); }
    draft_free(dd); return 1;
}
