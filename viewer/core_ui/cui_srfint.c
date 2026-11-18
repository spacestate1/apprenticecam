/* cui_srfint.c - Wireframe > Curve at Intersection (sketcher_curve_8, FUN_1802b8960 mccad). Spec: ../../formats/wireframe.md "Curve at Intersection".
 * Panel: wireframecurveintersectionmanagerview.baml; values FUN_18024cb60; loop FUN_1802b8960; intersect_multiple_surfs (mccore 0x180562730) -> viewer/mcsrfint.c mi_surf_surf.
 * Not decoded / not ported, refused with a message: trimmed surfaces (MaLoop::TrimToFace), Offset (offset_a_surface), solid faces. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "../mcsurf.h"
#include "../mcsurf_build.h"
#include "../mcsrfint.h"
#include "../mcsrfcrv.h"

int sp_nurbs_cfg(void);   /* cfg_createNurbs (cui_spline.c) */
/* FUN_1802b8960: local_4a8 = surf_step_chord_dev; local_4a0 / dStack_498 = 0 (Set 1, Set 2 offsets); local_490 = 0x100: byte 0 Join off, byte 1 Find multiple on */
static double si_chord, si_o1, si_o2, si_l_chord, si_l_o1, si_l_o2;
static int si_join, si_multi = 1, si_l_join, si_l_multi;
static int *si_a, si_na, *si_b, si_nb;
static int si_state, si_built, si_todo;   /* si_state: 0 / 1 selecting set 1 / 2, 2 the panel (get_event); si_built = bVar4 (a live result); si_todo = bVar7 (the next Enter or a panel change builds) */
static int si_oneset;   /* cVar5 */

int si_cmd(int c) { return c == G_CINT; }
const char *si_prompt_text(void) {
    if (si_state == 0 || si_state == 3) return "Select first set of surfaces";                /* mccadres 5974 (0x1756), FUN_1802b94e0 */
    if (si_state == 1 || si_state == 4) return "Select second set of surfaces";               /* mccadres 5975 (0x1757), FUN_1802b9730 */
    return si_built ? "Set options, press <ENTER>, or OK" : "Select new sets or OK";   /* mccadres 5986 (0x1762) / 5987 (0x1763) after Apply */
}
static void si_msg(void) { snprintf(cam_msg, sizeof cam_msg, "%s", si_prompt_text()); overlay_dirty = 1; }
int si_begin(int c) { (void)c; si_chord = mc_systol.surf_step_chord_dev; si_o1 = si_o2 = 0; si_join = 0; si_multi = 1; si_na = si_nb = 0; si_state = 0; si_built = 0; si_todo = 0; si_oneset = 0; si_msg(); return 1; }
void si_end(void) { free(si_a); free(si_b); si_a = si_b = NULL; si_na = si_nb = 0; si_state = 0; si_built = 0; }
int si_need(void) { return 0; }
void si_take(int e, V3 p) { (void)e; (void)p; }
int si_click(float mx, float my, const double w[3], int ctrl) { (void)mx; (void)my; (void)w; (void)ctrl; return 0; }
int si_esc(void) { return 0; }

/* FUN_1802b94e0 / FUN_1802b9730 (ours: the current selection and Enter, as Curve All Edges): a set = the selected surfaces */
static int si_take_set(int **dst) {
    int *ls = NULL, n = sel_list_surfs(&ls); free(*dst); *dst = ls; sel_clear(); return n;
}
static void si_oneset_recompute(void) { si_oneset = (2 <= si_na && si_nb == 0) || (si_na == 0 && 2 <= si_nb); }   /* FUN_1802b8960: (iVar8 < 2 || iVar13 != 0) && (iVar8 != 0 || iVar13 < 2) -> cVar5 = 0, else 1 */

/* the ISrf of a surface entity: LoadSurf(ent, true, ..) (intersect_multiple_surfs 0x180562730, asm 0x180562c11 / 0x180562e8d: dl = 1) =
   MaNURBSurf's DB constructor (mcsrfcrv.c mcsc_load_nbs -> mcsrfint.c mi_db_knots: the knots normalised by products, regenerated when this[0x1c]) */
static void si_free(ISrf *o) { free(o->KU); free(o->KV); free(o->V); }
static void si_build(void) {
    /* FUN_1802b9990: msf_nurbs_splines = cfg_createNurbs, surf_step_chord_dev = the panel chord, surf_use_chord_dev = 1, msf_join_em, msf_find_multi */
    if (fabs(si_o1) > mc_systol.tol || fabs(si_o2) > mc_systol.tol) { snprintf(cam_msg, sizeof cam_msg, "Curve at Intersection Offset: offset_a_surface NOT PORTED"); overlay_dirty = 1; return; }
    /* intersect_multiple_surfs (mccore 0x180562730) param_1 = cVar5: the inner loop over the first set from the entity after the outer one, else the second set */
    if (si_na == 0 && si_oneset) { snprintf(cam_msg, sizeof cam_msg, "No Intersections found"); overlay_dirty = 1; return; }   /* the outer get_ent mask 0x14 (selected and set 1) finds nothing: post_warning(0x1760) */
    int metric = mcam_is_metric(&F); double maxstep = metric ? 12.0 : 0.5, minstep = metric ? 0.0025 : 1e-4;   /* MaSurfSurfIntersect ctor arguments: 0x3f647ae147ae147b, 0x40280000.. / 0x3f1a36e2eb1c432d, 0x3fe00000.. */
    MiCurve *all = NULL; int nall = 0, capall = 0, any_trim = 0;
    mcsc_nurbs_cfg = sp_nurbs_cfg();
    mod_begin();
    for (int i = 0; i < si_na && !any_trim; i++) {
        const Surf *sa = &F.surfs[F.ents[si_a[i]].srf]; if (sa->kind != 0) { any_trim = 1; break; }
        for (int j = si_oneset ? i + 1 : 0; j < (si_oneset ? si_na : si_nb); j++) {
            int e2 = si_oneset ? si_a[j] : si_b[j]; if (e2 == si_a[i]) continue;   /* local_a588 != local_7c48 (the same entity skipped) */
            const Surf *sb = &F.surfs[F.ents[e2].srf]; if (sb->kind != 0) { any_trim = 1; break; }
            ISrf A, B; if (mcsc_load_nbs(&F, sa, &A, NULL, NULL, NULL) < 0) { any_trim = 1; break; } if (mcsc_load_nbs(&F, sb, &B, NULL, NULL, NULL) < 0) { si_free(&A); any_trim = 1; break; }
            MiCurve *c; int nc = mi_surf_surf(&A, &B, si_chord, si_chord, maxstep, minstep, si_multi, &c);   /* chord, chord, 1e-4 / 0.0025, 0.5 / 12 (intersect_multiple_surfs) */
            if (nc > 0) { if (nall + nc > capall) { capall = (nall + nc) * 2; all = realloc(all, sizeof(MiCurve) * capall); } memcpy(all + nall, c, sizeof(MiCurve) * nc); nall += nc; }
            free(c); si_free(&A); si_free(&B);
        }
    }
    if (any_trim) { for (int k = 0; k < nall; k++) { free(all[k].k); free(all[k].p); } free(all); mod_end(); snprintf(cam_msg, sizeof cam_msg, "Curve at Intersection: a trimmed or non-NURBS surface: MaLoop::TrimToFace NOT PORTED"); overlay_dirty = 1; return; }
    int total = 0;
    if (si_join && nall) nall = mi_join_curves(&all, nall, metric ? 0.2 : 0.01, metric);   /* msf_join_em: MaCurve::JoinCurves(msf_join_tol) (mccore_systol: 0.2 mm / 0.01 inch) */
    for (int j = 0; j < nall; j++) {
        int made = 0, out[8]; const char *why = NULL;
        mcsc_emit_mi(&F, all[j].deg, all[j].ncp, all[j].nk, all[j].k, all[j].p, si_chord, mc_systol.tol, geo_level, geo_color, out, 8, &made, &why);   /* MaCurve::Simplify else MakeSplineOrNURBCurve, surf_step_chord_dev = the panel value */
        if (!made && why) snprintf(cam_msg, sizeof cam_msg, "%s", why);
        total += made; free(all[j].k); free(all[j].p);
    }
    free(all); mod_end();
    if (nall == 0) { snprintf(cam_msg, sizeof cam_msg, "No Intersections found"); overlay_dirty = 1; return; }   /* post_warning 0x1760 (mccoreres) */
    doc_dirty = 1; si_built = 1; si_l_chord = si_chord; si_l_o1 = si_o1; si_l_o2 = si_o2; si_l_join = si_join; si_l_multi = si_multi; si_msg();
}
static void si_rebuild(void) { do_undo(); if (nredo) urec_free(&redo_st[--nredo]); si_built = 0; si_build(); overlay_dirty = 1; }   /* FUN_1802b8960: every build first UndoTransTerminates the previous one */
/* <Enter> at the two selections, then get_event's return code with bVar7: the build */
void si_enter_key(void) {
    if (si_state == 0) {
        si_na = si_take_set(&si_a);
        if (!si_na) { si_end(); geo_end(); return; }   /* FUN_1802b94e0 false: local_538 = 0, the command ends */
        si_state = 1; si_msg(); return;
    }
    if (si_state == 1) {
        si_nb = si_take_set(&si_b); si_oneset_recompute(); si_todo = 1; si_state = 2;
        si_built = 0; si_build(); si_msg(); return;   /* the first pass: local_524 = 3: the build */
    }
    if (si_state == 3 || si_state == 4) {   /* code 7 / 9's reselection: Enter ends it (select_ents), then bVar7 */
        if (si_state == 3) si_na = si_take_set(&si_a); else si_nb = si_take_set(&si_b);
        if (!si_na && !si_nb) si_todo = 0; else { si_oneset_recompute(); si_todo = 1; }
        si_state = 2; si_msg(); return;
    }
    if (si_todo) { if (si_built) si_rebuild(); else si_build(); }
}
/* FUN_1802b8960's panel codes: 0 OK (ends), 2 Cancel (ends), 1 Apply: commit + selection cleared; 7 / 9: Reselect set 1 / 2 */
int si_button(int code) {
    if (code == 0 || code == 2) return 0;   /* the transaction is committed at the exit; the result stays */
    if (code == 1) { si_built = 0; si_todo = 0; si_na = si_nb = 0; sel_clear(); si_msg(); return 1; }   /* UndoTransCommit, sktcher_main_color, selection bits cleared, prompt 5987 */
    if (code == 7) { si_state = 3; sel_clear(); snprintf(cam_msg, sizeof cam_msg, "Select first set of surfaces"); overlay_dirty = 1; return 1; }
    if (code == 9) { si_state = 4; sel_clear(); snprintf(cam_msg, sizeof cam_msg, "Select second set of surfaces"); overlay_dirty = 1; return 1; }
    return 1;
}
/* a panel change rebuilds when a result is pending (codes 4, 5, 6, 8, 10: LAB_1802b913f with bVar7) */
void si_panel_sync(void) {
    if (si_state != 2 || !si_todo) return;
    if (si_built && si_l_chord == si_chord && si_l_o1 == si_o1 && si_l_o2 == si_o2 && si_l_join == si_join && si_l_multi == si_multi) return;
    if (si_built) si_rebuild(); else si_build();
}
void si_script(const char *arg) {
    double a; int k;
    if (sscanf(arg, "chord=%lf", &a) == 1) si_chord = a;
    else if (sscanf(arg, "o1=%lf", &a) == 1) si_o1 = a;
    else if (sscanf(arg, "o2=%lf", &a) == 1) si_o2 = a;
    else if (sscanf(arg, "join=%d", &k) == 1) si_join = k;
    else if (sscanf(arg, "multi=%d", &k) == 1) si_multi = k;
    si_panel_sync();
}
void si_panel_body(const CuiPanelOps *ops, void *u) {
    if (ops->exp(u, "Surface Set 1")) { ops->toggle(u, "_Reselect", 1, 7); ops->end(u); }   /* Set1ReselectAllCommand: code 7 */
    if (ops->exp(u, "Surface Set 2")) { ops->toggle(u, "Reselec_t", 1, 9); ops->end(u); }   /* Set2ReselectAllCommand: code 9 */
    if (ops->exp(u, "_Chord Height Tolerance")) { ops->num(u, "", "sichord", &si_chord, NULL, 1, 0, 1e6); ops->end(u); }
    if (ops->exp(u, "Offset")) { ops->num(u, "Set _1:", "sio1", &si_o1, NULL, 1, -1e6, 1e6); ops->num(u, "Set _2:", "sio2", &si_o2, NULL, 1, -1e6, 1e6); ops->end(u); }
    if (ops->exp(u, "Settings")) { ops->check(u, "_Join curves into entity", &si_join, 0); ops->check(u, "_Find multiple solutions", &si_multi, 1); ops->end(u); }
}
