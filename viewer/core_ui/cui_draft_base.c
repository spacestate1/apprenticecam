/* cui_draft_base.c - the Drafting tab's Baseline (0x9473) and Chained (0x9474) commands: mccad FUN_18003c3d0(app, 0 / 1)
 * (cad_rundraft.c:540-720) and mccore BaselineChainProc @180247c60 (decomp/draft/circ_core.c 305-669): pick a linear dimension, then
 * points; every point makes an ordinary linear dimension (calc_linear_dimension, viewer/draft_create.c) from the base / chain end.
 * Spec: ../../formats/drafting/create.md 11. "ours" marks glue of this program. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "cui_draft_int.h"
#include "draft_create_circ.h"
#include "mcam_saveops.h"
#include "draft.h"

#define TWOPI_ 6.283185307179586
#define ANGTOL_ 5.7295779513082324e-05
static double D8(const uint8_t *b, int o) { double v; memcpy(&v, b + o, 8); return v; }

/* BaselineChainProc's statics (DAT_1813c7c10 ..) */
static int bc_chain;                                    /* param_1: 0 baseline, 1 chained */
static int bc_phase;                                    /* FUN_18003c3d0's uVar2: 0 pick the base, 1 the first point, 4 the next ones */
static uint8_t bc_A[0x98], bc_B[0x98], bc_base[0x98];   /* DAT_1813c7c40 / cf0 (the two ends), db0 (the base of the next dimension) */
static double bc_Av[3], bc_Bv[3];                       /* DAT_1813c7d88 / d98 (view) */
static double bc_text[3];                               /* DAT_1813c7c28: the text point (view) */
static double bc_inc[2];                                /* DAT_1813c7c10 / c18: the increments */
static double bc_ang;                                   /* DAT_1813c7c20: the base's dimension-line angle (radians) */
static int bc_tor;                                      /* DAT_1813c7dac: the base's text is not along its line (-> DAT_1812d980c) */
static int bc_nearA;                                    /* DAT_1813c7da8 */
static double bc_basetext[3];                           /* the base record's text point (+0x638; local_77e0 in later calls) */
static int bc_incr; static double bc_ix, bc_iy;
static int bc_incr_force = -1;                           /* tests (ours): step baseincr:0|1 overrides config 15000 */
static double bc_cd8[2];                                /* DAT_1813c7cd8: the incremented text point (.bss: 0 at start; kept between commands) */
/* the drag-edit of the new dimension (FUN_180038c50, baseline without increments): RunDraftCommand(99) -> FUN_180040ed0 case 99 ->
   FUN_1800490a0 (circ_cad.c) -> FUN_180043260 -> FUN_180044c50 / FUN_180043390 (cad_linear2.c:198, cad_linear3.c:34): the record into the
   drag entity, DraftGlobalsSave + DraftGlobalsFromDim, DfHvpAngle = +0x1a0, P1 / P2 = +0x70 / +0x108, orientation +0x68 (0 when rotated),
   value mode +0x1a9, locked; the linear drag FUN_180042e50 (create.md 2.4) */
static int de_on, de_idx, de_retest, de_orient; static DcInput de_in; static DcSettings de_S, de_saved;
static void de_end(int ok);   /* config 15000 Baseline increments?, 15001 X?, 15002 Y? (DAT_1812d97fa / 9850 / 9858) */

static void w2v_(const double w[3], double v[3]) { for (int i = 0; i < 3; i++) v[i] = dr_view[3 * i] * w[0] + dr_view[3 * i + 1] * w[1] + dr_view[3 * i + 2] * w[2]; }
static void v2w_(const double v[3], double w[3]) { for (int i = 0; i < 3; i++) w[i] = dr_view[i] * v[0] + dr_view[3 + i] * v[1] + dr_view[6 + i] * v[2]; }
static void rot_(double px, double py, double cx, double cy, double c, double s, double *ox, double *oy) {   /* rotate_pt @1804b4550 */
    double dx = px - cx, dy = py - cy; *ox = cx + (dx * c - dy * s); *oy = dy * c + dx * s + cy;
}

/* prompts (cad_rundraft.c:603-690): mccadres 27003 "Drafting: " + 27717 "Create dimension, baseline: " / 27718 "Create dimension,
   chained: " + "\n" + 27719 "Select a linear dimension: " (the base) or 27702 "Specify the second endpoint" */
void db_prompt(void) {
    snprintf(cam_msg, sizeof cam_msg, "Drafting: %s\n%s", bc_chain ? "Create dimension, chained: " : "Create dimension, baseline: ",
             bc_phase == 0 ? "Select a linear dimension: " : "Specify the second endpoint");
}
void db_restart(void) {
    if (de_on) de_end(0);
    bc_chain = geo_cmd == G_DIMCH; bc_phase = 0; dr_placing = 0; dr_drag_ok = 0; dr_npick = 0;
    {                                                   /* the config items, read at every start (Mastercam: the globals) */
        char v[128]; mcam_config_value(dr_S.metric, 15000, v, sizeof v); bc_incr = v[0] ? (v[0] == 'y' || v[0] == 'Y' || atoi(v) != 0) : 0;
        if (bc_incr_force >= 0) bc_incr = bc_incr_force;
        mcam_config_value(dr_S.metric, 15001, v, sizeof v); bc_ix = v[0] ? atof(v) : 0;
        mcam_config_value(dr_S.metric, 15002, v, sizeof v); bc_iy = v[0] ? atof(v) : 0;
    }
    db_prompt(); dr_scene();
}
/* get_event: the base pick = flags 2, mask 0x40 (dimensions); the points = flags 1 (a point event) */
int db_need(void) { return de_on ? 't' : bc_phase == 0 ? 'e' : 'p'; }

/* df_calc_orient_ang @180242d60 (core_createport.c): the angle (degrees) folded into (-90, 90]; 90 -> orientation 1 (vertical), angle 0;
   else orientation 0 with the angle (DfCheckAngle, as draft_create.c's FUN_180251290 port) or 0 below the degree tolerance */
static int orient_ang(double *a) {
    double v = *a;
    while (v <= -360.0) v += 360.0;
    while (360.0 <= v) v -= 360.0;
    if (180.0 <= v) v -= 180.0;
    if (90.0 < v) v -= 180.0;
    if (fabs(fabs(v) - 90.0) < ANGTOL_) { *a = 0.0; return 1; }
    if (fabs(v) < ANGTOL_) { *a = 0.0; return 0; }
    double o = v;                                       /* DfCheckAngle (core_create2.c 3) */
    if (fabs(fabs(o) - 360.0) < ANGTOL_) o = v < 0 ? -360.0 : 360.0;
    if (fabs(fabs(o) - 270.0) < ANGTOL_) o = v < 0 ? -270.0 : 270.0;
    if (fabs(fabs(o) - 180.0) < ANGTOL_) o = v < 0 ? -180.0 : 180.0;
    if (fabs(fabs(o) - 90.0) < ANGTOL_) o = v < 0 ? -90.0 : 90.0;
    *a = fabs(o) >= ANGTOL_ ? o : 0.0;
    return 0;
}

/* BaselineChainProc(chain, 0, base): the base (df_linear_format: linear, a circular quadrant style, perpendicular, tangent; else mccoreres
   27070 "BASE is not a linear dimension"); DraftIsDirty -> message 0x69c0 + DfRegenEnt: NOT PORTED (the base taken as clean); the text point
   +0x638, the line angle (leader 1 -> leader 2 first points), the increments, the text-not-along-the-line flag; the two ends by type
   (linear: +0x70 / +0x108; circular quadrant style: quad_to_eps; perpendicular / tangent: NOT PORTED) */
static int db_base(const uint8_t *E) {
    int ty = E[0x308];
    int lin = (ty == 0) || (ty == 2 && E[0x100] == 0) || ty == 5 || ty == 6;   /* df_linear_format (core_calcdim.c 149) */
    if (!lin) { snprintf(cam_msg, sizeof cam_msg, "BASE is not a linear dimension"); return 0; }
    if (ty == 5 || ty == 6) { snprintf(cam_msg, sizeof cam_msg, "a perpendicular / tangent base (NOT PORTED)"); return 0; }
    bc_text[0] = D8(E, 0x638); bc_text[1] = D8(E, 0x640); bc_text[2] = D8(E, 0x648); memcpy(bc_basetext, bc_text, sizeof bc_text);
    bc_inc[0] = bc_ix; bc_inc[1] = bc_iy;
    double dx = D8(E, 0x560) - D8(E, 0x4c8), dy = D8(E, 0x568) - D8(E, 0x4d0);
    bc_ang = 1e-12 <= dy * dy + dx * dx ? atan2(dy, dx) : 0.0; if (bc_ang < 0) bc_ang += TWOPI_;
    bc_tor = fabs(D8(E, 0x5e8) - bc_ang) > 5e-5;
    if (ty == 0) { memcpy(bc_A, E + 0x70, 0x98); memcpy(bc_B, E + 0x108, 0x98); }
    else {                                              /* DfGetCircularArc + quad_to_eps (DfHvpAngle = +0x108) */
        DcCircInput in; memset(&in, 0, sizeof in);
        double w[3]; memcpy(w, E + 0x70, 24); w2v_(w, in.centre); in.centre[2] = D8(E, 0x320);
        in.r = D8(E, 0x118); in.start = D8(E, 0x120); in.sweep = D8(E, 0x128); in.view = in.arc_view = dr_view_no(); memcpy(in.vm, dr_view, sizeof in.vm);
        memcpy(&in.eptr, E + 0x90, 8);
        uint8_t ap[2][0x98]; int o;
        if (!dc_circ_quad_eps(&in, E[0x110], E[0x101], D8(E, 0x108), ap, &o)) { snprintf(cam_msg, sizeof cam_msg, "BASE is not a linear dimension"); return 0; }
        memcpy(bc_A, ap[0], 0x98); memcpy(bc_B, ap[1], 0x98);
    }
    double w[3]; memcpy(w, bc_A + 8, 24); w2v_(w, bc_Av); memcpy(w, bc_B + 8, 24); w2v_(w, bc_Bv);
    bc_phase = 1; db_prompt();
    return 1;
}

/* BaselineChainProc(chain, 1 | 2, ..., the new point): the nearer end along the base line (first point, or every chained point); chained:
   from that end to the new point, the end moves there; baseline: from the far end (the first point decides it); increments on (baseline):
   the text point moves by (+-X, +-Y) by the quadrant of (base text - the point projected on the base line) at the first point; chained
   keeps the base's text point (centred, auto_alignment 1); DfHvpAngle = the line angle (df_calc_orient_ang), DAT_1812d980c = the flag;
   calc_linear_dimension; store_ent in one UndoTrans. Baseline without increments hands the new dimension to the drag-edit
   (FUN_180038c50; db_move / de_end below) before the UndoTrans ends */
static void db_point(uint8_t *ap, int ent) {
    double pw[3], pv[3]; memcpy(pw, ap + 8, 24); w2v_(pw, pv);
    if (bc_phase == 1 || bc_chain) {
        double c = cos(-bc_ang), s = sin(-bc_ang), ax, ay, bx, by, px, py;
        rot_(bc_Av[0], bc_Av[1], bc_basetext[0], bc_basetext[1], c, s, &ax, &ay); rot_(bc_Bv[0], bc_Bv[1], bc_basetext[0], bc_basetext[1], c, s, &bx, &by);
        rot_(pv[0], pv[1], bc_basetext[0], bc_basetext[1], c, s, &px, &py);
        bc_nearA = fabs(px - ax) <= fabs(px - bx) && fabs(px - bx) != fabs(px - ax);
    }
    if (bc_chain) {
        if (!bc_nearA) { memcpy(bc_base, bc_B, 0x98); memcpy(bc_B, ap, 0x98); bc_Bv[0] = pv[0]; bc_Bv[1] = pv[1]; }
        else { memcpy(bc_base, bc_A, 0x98); memcpy(bc_A, ap, 0x98); bc_Av[0] = pv[0]; bc_Av[1] = pv[1]; }
    } else {
        if (bc_phase == 1) memcpy(bc_base, bc_nearA ? bc_B : bc_A, 0x98);
        if (bc_phase == 1) {                            /* the increments' signs (proj_pt_ln of the base text on the A-B line) */
            double lx = bc_Bv[0] - bc_Av[0], ly = bc_Bv[1] - bc_Av[1], L2 = lx * lx + ly * ly;
            double qx = bc_Av[0], qy = bc_Av[1];        /* proj_pt_ln(text, line A-B) */
            if (L2 > 0) { double tt = ((bc_basetext[0] - bc_Av[0]) * lx + (bc_basetext[1] - bc_Av[1]) * ly) / L2; qx = bc_Av[0] + tt * lx; qy = bc_Av[1] + tt * ly; }
            double dx = bc_basetext[0] - qx, dy = bc_basetext[1] - qy, a = 1e-12 <= dx * dx + dy * dy ? atan2(dy, dx) : 0.0;
            if (a < 0) a += TWOPI_;
            while (a > TWOPI_) a -= TWOPI_;
            if (!(a - 1.570796326794896 <= 1e-06 || 1e-06 < a - 3.141592653589793)) { bc_inc[0] = -bc_ix; bc_inc[1] = bc_iy; }
            else if (!(a - 3.141592653589793 <= 1e-06 || 1e-06 < a - 4.71238898038469)) { bc_inc[0] = -bc_ix; bc_inc[1] = -bc_iy; }
            else { bc_inc[0] = bc_ix; bc_inc[1] = !(a - 4.71238898038469 <= 1e-06 || 1e-06 < a - TWOPI_) ? -bc_iy : bc_iy; }
        }
        if (bc_incr) { bc_cd8[0] = bc_text[0] + bc_inc[0]; bc_cd8[1] = bc_text[1] + bc_inc[1]; }   /* DAT_1813c7cd8 = c28 + c10 / c18 */
        bc_text[0] = bc_cd8[0]; bc_text[1] = bc_cd8[1];   /* !chained: c28 = cd8 (increments off: cd8 as it was - the drag-edit places it) */
    }
    DcInput in; memset(&in, 0, sizeof in);
    memcpy(in.ap1, bc_base, 0x98); memcpy(in.ap2, ap, 0x98);
    double tw[3]; v2w_(bc_text, tw); memcpy(in.cursor, tw, sizeof tw);
    in.view = dr_view_no(); memcpy(in.vm, dr_view, sizeof in.vm);
    double hvp = bc_ang * 57.29577951308233; in.orient = orient_ang(&hvp); in.hvp = hvp;
    DcSettings S = dr_S; S.text_orient = bc_tor; if (bc_chain) S.auto_align = 1;
    S.sel_dist = SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1); S.color = geo_color; S.level = geo_level;
    uint8_t E[0x810]; dc_init_dim(E, &S, dr_view_no());
    DcResult R = dc_calc_linear(E, &in, &S, dr_fonts_get());
    if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); return; }
    if (R.swapped) { uint8_t t[0x98]; memcpy(t, bc_base, 0x98); memcpy(bc_base, ap, 0x98); memcpy(ap, t, 0x98); }
    if (!R.valid) { snprintf(cam_msg, sizeof cam_msg, "Warning - cannot calculate dimension"); return; }   /* BaselineChainProc 0 -> the command ends (cad_rundraft.c:654-660): here the message */
    F.dnew = realloc(F.dnew, sizeof *F.dnew * (size_t)(F.ndnew + 1));   /* store_ent (main attributes / colour), one UndoTrans */
    struct DraftNew *d = &F.dnew[F.ndnew]; memcpy(d->E, E, 0x810); d->ent[0] = d->ent[1] = -1; d->on = 1;
    int idx = F.ndnew++; doc_dirty = 1;
    (void)ent;
    if (!bc_chain && !bc_incr) {                        /* out = the new eptr: FUN_180038c50(app, &out, no undo, &ok, 1, &oknew, 0) (cad_rundraft.c:647,
                                                           asm 0x18003c8c9-0x18003c8f2); the UndoTrans stays open until it ends */
        const uint8_t *R0 = F.dnew[idx].E;
        de_saved = dr_S; de_S = dr_S;                   /* DraftGlobalsSave; DraftGlobalsFromDim (the linear fields, as cui_draft.c dimglobals) */
        memcpy(de_S.fa, R0 + 0x5d8, 0x60); de_S.arrows_out = (int16_t)(R0[0x7b0] | R0[0x7b1] << 8); de_S.ldr_style = R0[0x7b4]; de_S.wit_vis = R0[0x7ac]; de_S.ldr_vis = R0[0x7ae];
        de_S.text_orient = fabs(D8(R0, 0x5e8)) < 5e-5; de_S.auto_align = R0[0x7c9]; de_S.digits = R0[0x7cd];
        memset(&de_in, 0, sizeof de_in); memcpy(de_in.ap1, R0 + 0x70, 0x98); memcpy(de_in.ap2, R0 + 0x108, 0x98);
        de_in.hvp = D8(R0, 0x1a0); de_in.value_mode = R0[0x1a9] - 1 < 2 ? R0[0x1a9] : 0;
        de_orient = fabs(de_in.hvp) > ANGTOL_ ? 0 : R0[0x68];
        de_in.view = dr_view_no(); memcpy(de_in.vm, dr_view, sizeof de_in.vm);
        de_on = 1; de_idx = idx; de_retest = 1; dr_placing = 1; dr_drag_ok = 0;
        snprintf(cam_msg, sizeof cam_msg, "Create dimension, linear:\nSketch the text position for the linear dimension");   /* FUN_180043f20 / FUN_180043660 */
        dr_scene(); return;
    }
    URec r; memset(&r, 0, sizeof r); r.kind = U_DRAFT; r.idx = idx; undo_push(r);   /* UndoTransCommit */
    bc_phase = 4; db_prompt(); dr_scene();
}

/* the drag-edit's mouse move: FUN_180042e50 - determine_h_or_v on the first move (an H / V P1-P2 line sets that orientation), else the
   record's orientation, locked; calc_linear_dimension into the dimension (shown in place) */
int db_move(const double world[3], int ctrl) {
    if (!de_on) return 0;
    double c[3] = { world[0], world[1], grid_z() }, p1[3], p2[3], w1[3], w2[3];
    memcpy(w1, de_in.ap1 + 8, 24); memcpy(w2, de_in.ap2 + 8, 24); w2v_(w1, p1); w2v_(w2, p2);
    if (de_retest) { int hv = dc_line_hv(p1, p2, de_in.hvp); if (hv >= 0) de_orient = hv; de_retest = 0; }
    memcpy(de_in.cursor, c, sizeof c); de_in.orient = de_orient; de_in.ctrl = ctrl;
    DcSettings S = de_S; S.sel_dist = SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1); S.color = F.dnew[de_idx].E[6]; S.level = (int)F.dnew[de_idx].E[8];
    uint8_t E[0x810]; memcpy(E, F.dnew[de_idx].E, 0x810);
    DcResult R = dc_calc_linear(E, &de_in, &S, dr_fonts_get());
    if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); return 1; }
    if (R.valid) { memcpy(F.dnew[de_idx].E, E, 0x810); dr_drag_ok = 1; de_orient = R.orient; }
    dr_scene();
    return 1;
}
/* the drag-edit's end. A click (event 4): FUN_180043280(1) writes the dimension back, DraftGlobalsRestore; success -> UndoTransCommit,
   the next point. Esc / the panel's OK, Cancel, OK-New (codes 3 / 4 / 5): FUN_180048800 abandons (the dimension back as stored), success
   0 -> UndoTransTerminate: the transaction undone like the Undo command (both call FUN_1802dfa50: UndoTransEvent @1802dffd0 at
   0x1802e0027, UndoTransTerminate at 0x1802e04c1; decomp/draft/dim2_core.c) - the new dimension is removed; OK-New (code 5 with
   DAT_1806925f2 = 1) then restarts at the base pick, the others end the command (cad_rundraft.c:647-662, 3285-3300, 3436-3452) */
static void de_end(int ok) {
    de_on = 0; dr_placing = 0; dr_drag_ok = 0; dr_S = de_saved;
    if (ok) { URec r; memset(&r, 0, sizeof r); r.kind = U_DRAFT; r.idx = de_idx; undo_push(r); bc_phase = 4; db_prompt(); }
    else F.dnew[de_idx].on = 0;
    dr_scene();
}
int db_esc(void) { if (!de_on) return 0; de_end(0); return 0; }
int db_button(int code) {
    if (!de_on) return -1;
    de_end(0);
    if (code == 1) { db_restart(); return 1; }          /* OK-New */
    return 0;
}

/* the base pick: the dimension under the cursor (select_ent_univ, mask 0x40) = the shared drafting hit-test (viewer/draft_pick.c via
   cui_draft_pick.c); a part record edited here gives its edited bytes (cui_draft_pick) */
static const uint8_t *db_pick_dim(const double w[3]) { CuiDraftHit h; return cui_draft_pick(w, 0x40, &h) ? h.E : NULL; }
int db_click(float mx, float my, const double w[3], int ctrl) {
    if (de_on) { if (!dr_drag_ok) db_move(w, ctrl); de_end(1); return 1; }   /* the drag-edit accepted (event 4) */
    if (bc_phase == 0) {
        const uint8_t *E = db_pick_dim(w);
        if (!E) return 1;                               /* nothing under the cursor: no base yet (ours: the prompt stays) */
        db_base(E); return 1;
    }
    V3 p = { w[0], w[1], grid_z() };                    /* get_event(.., 1): the AutoCursor point, else the cursor ('K'); aPointTestForSolid: no
                                                           solid point kinds here */
    geo_snap_ptr(mx, my); int sn = !ctrl && geo_snap(&p, NULL, -1, 1); geo_snap_ptr_off();
    uint8_t ap[0x98]; int ent = -1;
    if (sn) { dr_ac_apoint(ap, &ent, p); if (ent >= 0 && !F.ents[ent].eptr) { uint64_t t = MCAM_EPTR_TOKEN((uint64_t)ent); memcpy(ap + 0x28, &t, 8); } }
    else { double q[3] = { w[0], w[1], grid_z() }; dc_apoint_world(ap, q, 'K'); }
    db_point(ap, ent);
    return 1;
}
/* tests (ours): basepick:s:N (the session's dimension N) | basepick:f:N (the open part's dimension record N, D_ID order) */
int db_script(const char *st, const char *arg) {
    if (!strcmp(st, "baseincr") && arg) { bc_incr_force = atoi(arg); return 1; }
    if (strcmp(st, "basepick") || !arg) return 0;
    if (!dd_cmd(geo_cmd) || (geo_cmd != G_DIMB && geo_cmd != G_DIMCH)) { printf("    (basepick: no Baseline / Chained command)\n"); return 1; }
    int n = atoi(arg + 2);
    if (arg[0] == 's') { if (n >= 0 && n < F.ndnew && F.dnew[n].on) db_base(F.dnew[n].E); else printf("    (basepick: no session dimension %d)\n", n); return 1; }
    DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; int k = 0; uint32_t len; const uint8_t *hit = NULL;
    for (int i = 0; dd && i < draft_count(dd); i++) { const uint8_t *E = draft_record(dd, i, &len); if (E && len == 0x810 && E[0] == 0x40) { if (k == n) hit = E; k++; } }
    if (hit) db_base(hit); else printf("    (basepick: no file dimension %d)\n", n);
    draft_free(dd); return 1;
}
