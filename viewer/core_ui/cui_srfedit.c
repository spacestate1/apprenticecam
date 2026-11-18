/* cui_srfedit.c - Surfaces tab edit commands (formats/surfaces_tab_plan.md "D."): Normals > Set (set_normals) and Un-trim
   (UnTrimSurfaces). Both are function-panel commands of cui_geo.c (G_SNSET / G_SUNTRIM); the record edits go through cui_surf.c's
   surface edit transaction (srf_tx_*: put_special_undo(3) / UndoTransStart .. UndoTransCommit). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "../mcsurf.h"
#include "../mcsrfcrv.h"
uint8_t *mcsb_trim_rec(const McamFile *f, int64_t base_eptr, const uint8_t *b, uint32_t bn, int nl, double **pts, const int *cnt, uint16_t flags, int shaded, double tol, uint32_t *n);   /* mcsurf_build.c */

static int16_t r16s(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static int32_t r32s(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static double rdd8(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static void wdd8(uint8_t *p, double v) { memcpy(p, &v, 8); }
static uint16_t selw(const Surf *sf) { return (uint16_t)(sf->rec[4] | sf->rec[5] << 8); }
static void selw_set(Surf *sf, uint16_t sb) { sf->rec[4] = (uint8_t)sb; sf->rec[5] = (uint8_t)(sb >> 8); }
static int surf_ent_ok(int e) { return e >= 0 && e < F.nents && F.ents[e].type == E_SURF && F.ents[e].srf >= 0 && F.ents[e].srf < F.nsurfs; }

/* ================================================================================================ Normals > Set (G_SNSET) */
/* set_normals (mccadutil 0x180112b20) -> SurfaceNormalSetApp (ctor FUN_1801101a0: the list +8 / its count +0x10) FUN_180111490
   (decomp/surfedit/normals.c, setn2.c, setn3.c):
   - select_ents (FUN_180110f10: mccadutilres 1544 (0x608) "Select Surfaces to flip normals toward the Current C-Plane", mask 0x100;
     it ends with End Selection / Enter; nothing selected: the command ends);
   - DoNormals FUN_180110a20: UndoTransStart; per listed surface FUN_180110760: 3 x 3 samples at u0 + i (u1 - u0) / 4, v0 + j (v1 - v0)
     / 4, i, j = 1..3, over the header range (a trimmed surface: its base's range, fill_in_ent of +0x98); FUN_180110d80 = eval_surf mode
     1 + calc_surf_unit_norm on the surface itself, negated with its +6 bit 4; d = N . (GetActiveViewNo(0) = the Cplane's view, row +0x38
     = its Z): d > 0.001 counts "toward", d < -0.001 "away"; away > toward: put_special_undo(3) + surf_flip_normal (bit 4 toggled),
     counted. The selection word: bit 4 off (local_52b4 &= 0xfffb), then FUN_180111fe0 8 (bit 2 off, 8 on) when the samples agree
     (toward >= 1 and away 0, or toward 0 and away >= 1), else 2 (bit 8 off, 2 on); rewrite_ent; list_op_check_ent /
     mc_op_test_and_modify_entity (dirty_ent_users); with Show normals the arrow (display_normal_arrow_for_selection). Then the status
     text mccadutilres 4871 "The surface normal has been reversed on %hd surfaces" and the prompt 2383 (0x94f) "Select a surface to
     flip the normal".
   - The loop (get_event 0x1040801a, mask 0x100): a surface pick (event 2) flips that surface (FUN_180111df0
     SelectSurfaceToChangeNormal: put_special_undo(3), surf_flip_normal, bit 8 -> 2 / 2 -> 8, rewrite_ent, the ops checked). The panel's
     codes: 0 OK (local_484 = 0; FUN_180111320 UndoTransCommit) and 2 (the loop left; FUN_180110e60(.., 0): UndoTransCommit) end; 1
     OK-New (FUN_180111080: commit, the list freed) and 4 Reselect (FUN_180111350: UndoTransTerminate = the flips undone, the list
     freed) select again; 5 Show normals (FUN_180112070: the listed surfaces' arrows, or none); 6 the direction radio (FUN_1801110c0
     OnFlip: every listed surface flipped, bit 8 -> 2 / 2 -> 8). Esc (get_event 2): FUN_180110e60(.., 2): UndoTransCommit.
   - Panel (surfacenormalsetmanagerview.baml): Surface [Reselect], Settings [Show normals], Direction [Cplane +Z / Cplane -Z]. Defaults
     (SurfaceNormalSetManager FUN_1800ac120, decomp/surfedit2/cu_setmgr.c): Show normals = FunctionPanelEditableBool(false);
     SaveRestoreLastParams (FUN_1800acb90) restores false while enable_dialog_modality is 0 (the modal case's static DAT_1802558c0:
     NOT DECODED - ours: off).
   Ours / NOT PORTED: calc_surf_norm's degenerate-point fallback (a sample without a normal counts neither way); the derivatives are
   mcsrfcrv.c's (exact for NURBS bases); the arrows cannot be picked (SelArrow, event 0x400000 FUN_180110ef0: NOT PORTED - pick the
   surface itself); the window list event (0x10, FUN_180111cf0) and surf_back_draw (both sides are drawn here); the status text goes
   to the status bar (cui_status_text = UpdateStatusBarText, decision S-6); the radio starts at Cplane +Z (the field behind IsDirection1 is not read; +Z is what
   DoNormals produces). */
static int *sn_sel, sn_n, sn_cap;
static int sn_selecting, sn_show, sn_dir, sn_active;
static char sn_status[96];
int srf_sn_selecting(void) { return sn_active && sn_selecting; }
int srf_sn_active(void) { return sn_active; }
static void sn_cplane_z(double w[3]) {          /* the Cplane's Z (view_to_world of (0, 0, 1)), as cui_surf.c cplane_dir */
    double z[3] = { 0, 0, 0 }, v[3] = { 0, 0, 1 }, o[3], w0[3];
    int id = plane_active[0] > 0 ? plane_active[0] : 1;
    plane_v2w(id, z, v, o); plane_v2w(id, z, z, w0);
    for (int i = 0; i < 3; i++) w[i] = o[i] - w0[i];
    double l = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]); if (l > 0) for (int i = 0; i < 3; i++) w[i] /= l;
}
/* FUN_180110d80: eval_surf mode 1 + calc_surf_unit_norm at (u, v) on the surface itself (a trimmed one evaluates its base), negated
   with its own +6 bit 4 */
static int sn_normal(const Surf *sf, double u, double v, double N[3]) {
    double P[3], Su[3], Sv[3];
    if (!mcsc_surf_frame(&F, sf, u, v, P, Su, Sv, N)) return 0;
    double l = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]); if (!(l > 1e-12)) return 0;   /* calc_surf_unit_norm's 1e-12 */
    if (sf->rec[0x6e] & 4) for (int k = 0; k < 3; k++) N[k] = -N[k];
    return 1;
}
/* FUN_180110760: the 3 x 3 votes; *keep = away <= toward (a trimmed surface whose base is missing: fill_in_ent fails, *keep 0) */
static void sn_votes(const Surf *sf, int *toward, int *away, int *keep) {
    const Surf *r = sf; *toward = *away = 0;
    if (sf->kind == 10) { int64_t be = 0; if (sf->n >= 0x108) memcpy(&be, sf->rec + 0x100, 8); r = mcs_surf(&F, be); if (!r) { *keep = 0; return; } }
    double d[4], z[3]; mcs_surf_domain(r, d); sn_cplane_z(z);
    for (int i = 1; i <= 3; i++) for (int j = 1; j <= 3; j++) {
        double N[3]; if (!sn_normal(sf, i * (d[1] - d[0]) * 0.25 + d[0], j * (d[3] - d[2]) * 0.25 + d[2], N)) continue;
        double t = N[0] * z[0] + N[1] * z[1] + N[2] * z[2];
        if (t > 0.001) (*toward)++; else if (t < -0.001) (*away)++;
    }
    *keep = *away <= *toward;
}
static void sn_selword(Surf *sf, int bit) {     /* FUN_180111fe0 */
    uint16_t sb = selw(sf);
    if (bit == 8) sb = (uint16_t)((sb & ~2) | 8); else if (bit == 2) sb = (uint16_t)((sb & ~8) | 2);
    selw_set(sf, sb);
}
/* put_special_undo(3) + surf_flip_normal; toggle: FUN_180111df0 / OnFlip's bit 8 -> 2, else 2 -> 8 */
static void sn_flip(int e, int toggle) {
    int s = F.ents[e].srf; Surf *sf = &F.surfs[s]; if (sf->n < 0x70) return;
    srf_tx_save(s);
    sf->rec[0x6e] ^= 4;
    if (toggle) { uint16_t sb = selw(sf); if (sb & 8) sn_selword(sf, 2); else if (sb & 2) sn_selword(sf, 8); }
    sf->edited = 1; dirty_ent_users(e);
}
static void sn_donormals(void) {               /* FUN_180110a20 DoNormals */
    if (!sn_n) return;
    int nflip = 0;
    for (int i = 0; i < sn_n; i++) {
        int e = sn_sel[i]; if (!surf_ent_ok(e)) continue;
        Surf *sf = &F.surfs[F.ents[e].srf]; if (sf->n < 0x70) continue;
        int to, aw, keep; sn_votes(sf, &to, &aw, &keep);
        selw_set(sf, (uint16_t)(selw(sf) & ~4));
        if (!keep) { sn_flip(e, 0); nflip++; }
        sn_selword(sf, ((to >= 1 && aw == 0) || (to == 0 && aw >= 1)) ? 8 : 2);
        sf->edited = 1; dirty_ent_users(e);
    }
    snprintf(sn_status, sizeof sn_status, "The surface normal has been reversed on %hd surfaces", (short)nflip);   /* mccadutilres 4871 */
    cui_status_text(sn_status);                  /* cleartextall + UpdateStatusBarText (FUN_180110a20) */
    doc_dirty = 1; scene_surfs_update(0); overlay_dirty = 1;
}
static char sn_pr[192];
const char *srf_sn_prompt(void) {
    if (sn_selecting) snprintf(sn_pr, sizeof sn_pr, "Select Surfaces to flip normals toward the Current C-Plane");   /* mccadutilres 1544 */
    else snprintf(sn_pr, sizeof sn_pr, "Select a surface to flip the normal");
    return sn_pr;
}
static void sn_prompt(void) { snprintf(cam_msg, sizeof cam_msg, "%s", srf_sn_prompt()); }
static void sn_unhighlight(void) {
    for (int i = 0; ent_sel && i < sn_n; i++) if (sn_sel[i] >= 0 && sn_sel[i] < ent_sel_n && ent_sel[sn_sel[i]]) { ent_sel[sn_sel[i]] = 0; nselected--; }
}
static void sn_clear_list(void) { sn_unhighlight(); sn_n = 0; sn_status[0] = 0; }   /* FUN_180110ec0 + FUN_1801109e0 (set_clr_sel_bits_all) */
void srf_sn_begin(int show) {
    sn_active = 1; sn_selecting = 1; sn_show = show; sn_dir = 0; sn_clear_list(); srf_pick_active = 1;
    sn_prompt(); overlay_dirty = 1;
}
/* a surface pick: while selecting = select_ents (a picked surface joins the list, a listed one leaves it - ours: the toggle of the
   general selection); afterwards FUN_180111df0 */
int srf_sn_pick(int e) {
    if (!sn_active || !surf_ent_ok(e)) return 0;
    if (sn_selecting) {
        for (int i = 0; i < sn_n; i++) if (sn_sel[i] == e) {
            memmove(sn_sel + i, sn_sel + i + 1, sizeof(int) * (size_t)(sn_n - i - 1)); sn_n--;
            if (ent_sel && e < ent_sel_n && ent_sel[e]) { ent_sel[e] = 0; nselected--; }
            sn_prompt(); overlay_dirty = 1; return 1;
        }
        if (sn_n == sn_cap) { sn_cap = sn_cap ? 2 * sn_cap : 16; sn_sel = realloc(sn_sel, sizeof(int) * (size_t)sn_cap); }
        sn_sel[sn_n++] = e;
        if (ent_sel && e < ent_sel_n && !ent_sel[e]) { ent_sel[e] = 1; nselected++; }
        sn_prompt(); overlay_dirty = 1; return 1;
    }
    sn_flip(e, 1);
    doc_dirty = 1; scene_surfs_update(0); sn_prompt(); overlay_dirty = 1;
    return 1;
}
/* End Selection / Enter: FUN_180110f10 is true with a count > 0 -> DoNormals; 0 = nothing selected (the command ends) */
int srf_sn_enter(void) {
    if (!sn_active || !sn_selecting) return 1;
    if (!sn_n) return 0;
    sn_selecting = 0; sn_unhighlight();          /* EmRemoveAllSelObjs */
    sn_donormals(); sn_prompt(); return 1;
}
/* the panel's buttons; 0 = the command ends */
int srf_sn_button(int code) {
    if (!sn_active) return 0;
    if (code == 0 || code == 2) return 0;        /* FUN_180111320 / the loop exit: UndoTransCommit (srf_sn_end) */
    if (code == 1) { srf_tx_commit(); sn_clear_list(); sn_selecting = 1; sn_prompt(); overlay_dirty = 1; return 1; }   /* FUN_180111080 */
    if (code == 4) { srf_tx_cancel(); scene_surfs_update(0); sn_clear_list(); sn_selecting = 1; sn_prompt(); overlay_dirty = 1; return 1; }   /* FUN_180111350 */
    return 1;
}
/* the panel's values: Show normals (code 5, FUN_180112070) / the direction radio (code 6, FUN_1801110c0 OnFlip) */
void srf_sn_values(int show, int dir) {
    if (!sn_active) return;
    if (show != sn_show) { sn_show = show; overlay_dirty = 1; }
    if (dir != sn_dir) {
        sn_dir = dir;
        if (!sn_selecting) {
            for (int i = 0; i < sn_n; i++) if (surf_ent_ok(sn_sel[i])) sn_flip(sn_sel[i], 1);
            doc_dirty = 1; scene_surfs_update(0); overlay_dirty = 1;
        }
    }
}
void srf_sn_end(void) {                         /* FUN_180110e60: UndoTransCommit, the list freed, EnableAutoHighlightNormals(0) */
    if (!sn_active) return;
    srf_tx_commit(); sn_clear_list(); sn_active = 0; sn_selecting = 0; srf_pick_active = 0; overlay_dirty = 1;
}
/* display_normal_arrow_for_selection (mccore 0x180538f50, decomp/surfedit2/core_selarrow.c): at the middle of the surface's own header
   range, eval_surf mode 1 + calc_surf_unit_norm, negated with +6 bit 4; GrArrow length get_n_y_pixels() * 0.1 (ours: a tenth of the
   view height, as cui_surf.c srf_ov), UtColor(0xff00, 0xff, 0) [ours: RGB 0x00ff00 - the constructor's arguments are NOT DECODED];
   the head as srf_ov's (GrArrow's shape NOT DECODED) */
static void sn_arrow(float **pv, int *pn, int *pcap, int s) {
    const Surf *sf = &F.surfs[s]; double d[4]; mcs_surf_domain(sf, d);
    double P[3], Su[3], Sv[3], N[3];
    if (!mcsc_surf_frame(&F, sf, (d[0] + d[1]) * 0.5, (d[2] + d[3]) * 0.5, P, Su, Sv, N)) return;
    if (sf->rec[0x6e] & 4) for (int k = 0; k < 3; k++) N[k] = -N[k];
    double L = scene_view_height() * 0.1, h = L * 0.2;
    V3 a = { P[0], P[1], P[2] }, t = { P[0] + N[0] * L, P[1] + N[1] * L, P[2] + N[2] * L };
    ov_pt(pv, pn, pcap, a, 0, 1, 0); ov_pt(pv, pn, pcap, t, 0, 1, 0);
    double w[3] = { N[1] * vmat[8] - N[2] * vmat[7], N[2] * vmat[6] - N[0] * vmat[8], N[0] * vmat[7] - N[1] * vmat[6] };
    double wl = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]); if (wl < 1e-9) { w[0] = vmat[0]; w[1] = vmat[1]; w[2] = vmat[2]; wl = 1; }
    for (int sg = -1; sg <= 1; sg += 2) {
        V3 q = { t.x - N[0] * h + sg * w[0] / wl * h * 0.5, t.y - N[1] * h + sg * w[1] / wl * h * 0.5, t.z - N[2] * h + sg * w[2] / wl * h * 0.5 };
        ov_pt(pv, pn, pcap, t, 0, 1, 0); ov_pt(pv, pn, pcap, q, 0, 1, 0);
    }
}
void srf_sn_ov(float **pv, int *pn, int *pcap) {
    if (!sn_active || sn_selecting || !sn_show) return;
    for (int i = 0; i < sn_n; i++) if (surf_ent_ok(sn_sel[i]) && !F.surfs[F.ents[sn_sel[i]].srf].off) sn_arrow(pv, pn, pcap, F.ents[sn_sel[i]].srf);
}
void srf_sn_dump(void) {                        /* tests */
    printf("      srf setnormals list %d selecting %d show %d status '%s'\n", sn_n, sn_selecting, sn_show, cui_status);
    for (int i = 0; i < sn_n; i++) if (surf_ent_ok(sn_sel[i])) { const Surf *sf = &F.surfs[F.ents[sn_sel[i]].srf]; int to, aw, k; sn_votes(sf, &to, &aw, &k);
        printf("      srf setnormals ent %d kind %d flag6 %02x sel %04x toward %d away %d\n", sn_sel[i], sf->kind, sf->rec[0x6e], selw(sf), to, aw); }
}

/* ================================================================================================ Un-trim (G_SUNTRIM) */
/* UnTrimSurfaces (mccad 0x18030ea80) -> SurfaceUntrimApp FUN_18030d870 (decomp/surfedit/cad_cmds.c l.1260):
   - the panel (surfaceuntrimmanagerview.baml: Settings [Keep original surface]); KeepOriginalSurface default off (SurfaceUntrimManager
     FUN_1802fc2f0 -> FUN_1800bd490(false); SaveRestoreLastParams FUN_18030d150 -> FUN_1802fcda0 restores false while
     enable_dialog_modality is 0 - the modal case's static DAT_1806a5648 NOT DECODED, ours: off). The mode: Keep -> 0, else 2.
   - the loop: get_event(mccoreres 2377 (0x949) "Select a surface", 0x1000000a, mask 0x100). A surface pick (event 2): UndoTransCommit
     (the previous untrim kept); a trimmed surface (kind 10): FUN_18030dfc0(mode, warn 1): fill_in_ent of its base (+0x100); the base's
     level not visible -> post_warning(Format(mccadres 7424 (0x1d00) "Untrimmed surface is on Level %d, which is not a displayed
     level", level)) (disassembly 0x18030e06b-0x18030e079); UndoTransStart; untrim_a_trimsurf(trimmed, mode, base). Another kind: nothing.
     Code 4 (the check box) with a changed value: UndoTransTerminate (the last untrim undone), FUN_18030dfc0 again on the last pick with
     the new mode and no warning. Codes 0 / 1 / 2 and Esc: UndoTransCommit, the command ends.
   - untrim_a_trimsurf (mccore 0x1808afff0, decomp/surfedit2/core_untrim.c l.882): mode != 0: put_special_undo(2) on the trimmed,
     erased, mode 2: selection word bit 0 off (deleted; mode 1 would blank it: bit 0x100), rewrite_ent; mode 2: the base's references
     (+0x28) - 1; put_special_undo(3) on the base; its bit 0x100 set -> the word &= 0xfe7f (unblanked); +0x6a (surf header +2) = 1 with
     shading on and shade-all (gl_dirty_display), else 0; MaSurface::LoadSurf(base, true, 30): IsSubseted && !IsUVRangeChangedFromOriginal
     -> the base's range (+0x70 / +0x78 / +0x80 / +0x88) = the vtable's UMinAllowable / UMaxAllowable / VMinAllowable / VMaxAllowable;
     ent_is_assoc(trimmed) (+0x38 > 0 or +0x54 > 0) with mode 2: add_undo_eptr_reference + tp_substitute_ent (the operations' references
     move to the base), else rewrite_ent(base); is_solid_dependent_on_curve -> mc_op_modify_entity.
   - The range (decomp/surfedit2/core_masurf.c): a kind 0 base is a MaNURBSurf (LoadUntrimmedSurf): knots normalised to 0 .. 1 ((k - k0)
     / (kN - k0)), the range mapped the same way and clamped to the knots (the DB ctor 0x180455dd0); UMinAllowable = knot[deg],
     UMaxAllowable = knot[nk - order] (0x180460ac0 / 0x180460aa0); IsUSubseted: |du - (UMax - UMin)allowable| > 1e-10 (0x180486b60);
     IsUVRangeChangedFromOriginal (0x1804873e0): du != u1 - u0 or dv != v1 - v0 (du = the mapped range's width);
     CheckSurfaceToSubsetDeltaUVRatios (0x1804804e0): subseted and an allowable / range ratio >= 1000 -> Truncate + reparameterise.
   Mode 2 on a surface used by operations: tp_substitute_ent (above; decision S-9).
   Ours / NOT PORTED (refused with a message): a base that is not kind 0 (MaBicubicSurf / MaGenericSurf / MaOffsetSurf allowables not
   decoded); a NURBS base whose knots MaNURBSurf regenerates (mcsrfcrv.c fe_reparam's condition: RegenNonUniformUKnotsFromDerivRatios);
   the ratio >= 1000 case (Truncate); the solid
   dependency (mc_op_modify_entity: no solid here depends on a surface). The part's level list decides "displayed" (mcam_level_visible). */
static int ut_active, ut_keep, ut_last = -1, ut_pend_mode = -1, ut_pend_trim = -1;

/* ------------------------------------------------------------------------------------------------ tp_substitute_ent */
/* tp_substitute_ent(old, new, copy_refs, update_ops, *count) (mccore 0x180037f10, decomp/surfedit2/core_untrim.c l.2583), called by
   untrim_a_trimsurf (modes 1 after tp_blank_surf_yes_no / 2) and store_new_trim_surf_ext (mode 2: DAT_1813116e4 = 2) with (.., 0, 1,
   ..) when ent_is_assoc(old) (ent +0x38 > 0 or +0x54 > 0; GetProdEnableBits & 6 == 0), after add_undo_eptr_reference(5, old eptr, new
   eptr, old id, new id):
   - MastercamPlanes::SwapEntityIDs(old id, new id, interrogate) - planes made from entities: NOT DECODED here (no plane of this program
     keeps an entity id);
   - copy_refs (0 here): new +0x38 = old +0x38;
   - every association record (get_ent_assoc over the database: the TP_ID records) whose entity id (+0x6c) is the old id: +0x6c = the new
     id, +0x70 = the new ent's +0x30, +0x80 = its eptr; for record kinds (+0x90) 1, 2, 3 and 0x1b also +0xc8 = the id, +0xd0 = the eptr;
     rewrite_ent_assoc; counted;
   - update_ops: the record's op (+0x68, TpOpList::OpByID) when not already dirty (op +0x94 == 0 or +0xf2 == 0 or +0x97 != 0):
     DatabaseRetrieve, +0xf2 = 1, +0x94 = 1, +0x97 = 0 (a nesting op 0x6c: +0x1555 from ParentIsNest), UpdateListAndDB.
   Ours: the stored ops' references (F.tp, the TP_ID records as read) get TpRef.subst1 = the new surface (in force while the old one is
   deleted and the new one lives: tp_ref_subst - undo / redo need no record of their own); the save rewrites the kept TP_ID records
   (mcam_saveops.c); the stored ops get regen (op +0x94, dirty_ent_users). The operations made in this session (CamOp.ref) are
   re-pointed here and follow undo / redo through srf_tp_subst_sync; their CamRef carries no +0x38 count, so "ent_is_assoc" is ours
   for them: a session op naming the surface. Not ported: grp_change_eptr / group lists (store_new_trim_surf_ext's
   list_grp_append_ents), a second substitution of a surface made by an earlier one in the same session after that earlier one is undone. */
typedef struct { CamOp *op; int ref, from, to; } CrSub;
static CrSub *crs; static int ncrs;
static int cr_live(const CamOp *o) { for (int j = 0; j < ncamops; j++) if (camops[j] == o || camops[j]->edit == o) return 1; return 0; }
void srf_tp_subst_sync(void) {
    for (int i = 0; i < ncrs; i++) {
        CrSub *c = &crs[i]; if (!cr_live(c->op) || c->ref >= c->op->nref) continue;
        int on = c->from >= 0 && c->from < F.nsurfs && c->to >= 0 && c->to < F.nsurfs && (F.surfs[c->from].off || F.surfs[c->from].blanked) && !F.surfs[c->to].off;   /* Offset's Move blanks the original */
        int s = on ? c->to : c->from; CamRef *r = &c->op->ref[c->ref];
        if (r->index != s) { r->index = s; r->idn = F.surfs[s].idn; c->op->dirty |= CAM_DIRTY_PARAMS; }
    }
}
static int tp_assoc(int old_e) {                /* ent_is_assoc(old) (+ ours: a session op naming it) */
    const Surf *os = &F.surfs[F.ents[old_e].srf];
    if (r16s(os->rec + 0x38) > 0 || r16s(os->rec + 0x54) > 0) return 1;
    for (int k = 0; k < F.ntp; k++) if ((os->idn && F.tp[k].ent == os->idn && tp_ref_subst(&F, &F.tp[k]) < 0) || tp_ref_subst(&F, &F.tp[k]) == F.ents[old_e].srf) return 1;   /* ours: a stored op's TP record names it
                                                   (the parts this program writes keep +0x38 at 0 for the surfaces made here) */
    for (int j = 0; j < ncamops; j++) { CamOp *o2[2] = { camops[j], camops[j]->edit };
        for (int w = 0; w < 2; w++) if (o2[w]) for (int k = 0; k < o2[w]->nref; k++) if (o2[w]->ref[k].cls == TP_ECLS_SURF && o2[w]->ref[k].index == F.ents[old_e].srf) return 1; }
    return 0;
}
static int tp_substitute(int old_e, int new_e) {
    int os = F.ents[old_e].srf, ns = F.ents[new_e].srf, n = 0; int32_t oid = F.surfs[os].idn;
    for (int k = 0; k < F.ntp; k++) {
        TpRef *t = &F.tp[k]; int cur = tp_ref_subst(&F, t);
        if (cur == os || (cur < 0 && oid && t->ent == oid)) { t->subst1 = ns + 1; n++; }
    }
    dirty_ent_users(old_e);                       /* the stored ops: op +0x94 = 1 (regen) */
    for (int j = 0; j < ncamops; j++) { CamOp *o2[2] = { camops[j], camops[j]->edit };
        for (int w = 0; w < 2; w++) if (o2[w]) for (int k = 0; k < o2[w]->nref; k++) {
            CamRef *r = &o2[w]->ref[k]; if (r->cls != TP_ECLS_SURF || r->index != os) continue;
            crs = realloc(crs, sizeof *crs * (size_t)(ncrs + 1)); crs[ncrs++] = (CrSub){ o2[w], k, os, ns };
            r->index = ns; r->idn = F.surfs[ns].idn; o2[w]->dirty |= CAM_DIRTY_PARAMS; n++;
        } }
    if (cui_hooks.ops_changed) cui_hooks.ops_changed();
    return n;
}
int srf_ut_active(void) { return ut_active; }
const char *srf_ut_prompt(void) { return "Select a surface"; }   /* mccoreres 2377 */
/* MaNURBSurf's allowables / subset test on a kind 0 record: 0 = the range stays, 1 = R = the new range, -1 = NOT PORTED (*why) */
static int ut_range(const Surf *b, double R[4], const char **why) {
    const uint8_t *r = b->rec;
    if (b->kind != 0) { *why = "the base is not a NURBS surface (MaSurface allowables of this kind NOT DECODED)"; return -1; }
    if (b->n < 0x118) { *why = "the base record is short"; return -1; }
    int nk[2] = { r32s(r + 0x100), r32s(r + 0x104) }, nc[2] = { r32s(r + 0x108), r32s(r + 0x10c) }, dg[2] = { r[0x111], r[0x112] };
    if (nk[0] < 2 || nk[1] < 2 || 0x118 + 8 * (size_t)(nk[0] + nk[1]) > b->n) { *why = "the base record is short"; return -1; }
    /* MaNURBSurf's knot regeneration (the DB ctor with param_3 true; mcsrfcrv.c fe_reparam): NOT PORTED here */
    {   int rat = !(r[0x113] & 1), mm[2] = { 0, 0 };
        for (int d = 0; d < 2; d++) { const uint8_t *K = r + 0x118 + 8 * (size_t)(d ? nk[0] : 0); int n = nk[d], p = dg[d];
            for (int i = p + 1; i < n - p - 1; ) { int j = i; while (j + 1 < n - p - 1 && rdd8(K + 8 * (size_t)(j + 1)) == rdd8(K + 8 * (size_t)i)) j++; if (j - i + 1 > mm[d]) mm[d] = j - i + 1; i = j + 1; } }
        if ((dg[0] <= mm[0] || dg[1] <= mm[1]) && (!rat || dg[0] < 3 || dg[1] < 3)) { *why = "MaNURBSurf regenerates this base's knots (NOT PORTED)"; return -1; } }
    double h[4] = { rdd8(r + 0x70), rdd8(r + 0x78), rdd8(r + 0x80), rdd8(r + 0x88) }, A[4], dn[2];
    int sub = 0;
    for (int d = 0; d < 2; d++) {
        const uint8_t *K = r + 0x118 + 8 * (size_t)(d ? nk[0] : 0); int n = nk[d];
        double k0 = rdd8(K), s = 1.0 / (rdd8(K + 8 * (size_t)(n - 1)) - k0);
        double kn0 = (rdd8(K) - k0) * s, knl = (rdd8(K + 8 * (size_t)(n - 1)) - k0) * s;
        double a = (h[2 * d] - k0) * s, e = (h[2 * d + 1] - k0) * s; if (a < kn0) a = kn0; if (knl < e) e = knl;
        A[2 * d] = (rdd8(K + 8 * (size_t)dg[d]) - k0) * s; A[2 * d + 1] = (rdd8(K + 8 * (size_t)(n - dg[d] - 1)) - k0) * s;   /* knot[deg], knot[nk - order] */
        (void)nc;
        dn[d] = e - a;
        if (fabs(dn[d] - (A[2 * d + 1] - A[2 * d])) > 1e-10) sub = 1;
    }
    if (!sub) return 0;
    for (int d = 0; d < 2; d++) if ((A[2 * d + 1] - A[2 * d]) / dn[d] >= 1000.0) { *why = "the range is below 1/1000 of the knots' (MaSurface::Truncate, NOT PORTED)"; return -1; }
    if (dn[0] != h[1] - h[0] || dn[1] != h[3] - h[2]) return 0;   /* IsUVRangeChangedFromOriginal */
    memcpy(R, A, sizeof A); return 1;
}
/* FUN_18030dfc0 + untrim_a_trimsurf; 1 = done (pending in the open transaction) */
static int ut_do(int e, int mode, int warn) {
    if (!surf_ent_ok(e)) return 0;
    Surf *ts = &F.surfs[F.ents[e].srf];
    if (ts->kind != 10 || ts->n < 0x108) return 0;          /* untrim_a_trimsurf: not trimmed -> 0 (FUN_18030d870: nothing for other kinds) */
    int64_t be; memcpy(&be, ts->rec + 0x100, 8);
    const Surf *bc = mcs_surf(&F, be); if (!bc) return 0;      /* fill_in_ent fails */
    int bs = (int)(bc - F.surfs), bent = bc->ent;
    if (bent < 0 || bent >= F.nents) return 0;
    double R[4]; const char *why = NULL; int rr = ut_range(bc, R, &why);
    if (rr < 0) { snprintf(cam_msg, sizeof cam_msg, "Un-trim: %s - %s", why, srf_ut_prompt()); return 0; }
    int assoc = mode == 2 && tp_assoc(e);              /* ent_is_assoc(trimmed), before it goes */
    if (warn && !mcam_level_visible(&F, bc->level)) cui_message("apprenticeCAM", "Untrimmed surface is on Level %d, which is not a displayed level", bc->level);   /* mccadres 7424 */
    Surf *b = &F.surfs[bs];
    srf_tx_save(bs);                                           /* put_special_undo(3) on the base */
    if (mode == 2) { int16_t rc = r16s(b->rec + 0x28); rc--; memcpy(b->rec + 0x28, &rc, 2); }
    uint16_t sb = selw(b); if (sb & 0x100) selw_set(b, (uint16_t)(sb & 0xfe7f));
    b->rec[0x6a] = solid_mode != 0 ? 1 : 0;                   /* gl_shading_on && gl_shade_all (ours: the shaded display, as put_surf_in_db's callers here) */
    if (rr == 1) for (int k = 0; k < 4; k++) wdd8(b->rec + 0x70 + 8 * k, R[k]);
    b->blanked = (selw(b) & 0x100) != 0; mcam_ent_blank(&F, bent, b->blanked);
    b->edited = 1;
    if (mode == 2) ent_off(e);                                 /* put_special_undo(2) + the word's bit 0 off: deleted */
    if (assoc) tp_substitute(e, bent);                         /* add_undo_eptr_reference(5, ..) + tp_substitute_ent(trimmed, base, 0, 1, ..) */
    ut_pend_mode = mode; ut_pend_trim = e;
    dirty_ent_users(e); dirty_ent_users(bent);
    doc_dirty = 1; scene_surfs_update(1); overlay_dirty = 1;
    return 1;
}
/* UndoTransCommit of the pending untrim: one undo step (mode 2: the trimmed surface's deletion with the base's record) */
static void ut_commit(void) {
    if (ut_pend_mode < 0) { srf_tx_commit(); return; }
    if (ut_pend_mode == 2) {
        URec r; memset(&r, 0, sizeof r); r.kind = U_ENTS_MOD;
        r.dents = malloc(sizeof(int)); r.dtypes = malloc(sizeof(int)); r.dents[0] = ut_pend_trim; r.dtypes[0] = E_SURF; r.nd = 1;
        r.sx = srf_tx_take();
        undo_push(r); doc_dirty = 1;
    } else srf_tx_commit();
    ut_pend_mode = -1; ut_pend_trim = -1;
}
static void ut_terminate(void) {                /* UndoTransTerminate of the pending untrim */
    if (ut_pend_mode == 2 && ut_pend_trim >= 0) ent_on(ut_pend_trim, E_SURF);
    srf_tx_cancel(); srf_tp_subst_sync();
    for (int i = 0; i < F.nsurfs; i++) if (F.surfs[i].ent >= 0 && F.surfs[i].ent < F.nents) mcam_ent_blank(&F, F.surfs[i].ent, F.surfs[i].blanked);
    ut_pend_mode = -1; ut_pend_trim = -1; scene_surfs_update(1); overlay_dirty = 1;
}
void srf_ut_begin(int keep) { ut_active = 1; ut_keep = keep; ut_last = -1; ut_pend_mode = -1; ut_pend_trim = -1; srf_pick_active = 1; snprintf(cam_msg, sizeof cam_msg, "%s", srf_ut_prompt()); }
int srf_ut_pick(int e) {
    if (!ut_active) return 0;
    ut_commit();                                               /* event 2: UndoTransCommit */
    snprintf(cam_msg, sizeof cam_msg, "%s", srf_ut_prompt());
    if (!surf_ent_ok(e) || F.surfs[F.ents[e].srf].kind != 10) return 1;
    ut_last = e;                                               /* pDVar11: the last trimmed pick, redone by code 4 */
    ut_do(e, ut_keep ? 0 : 2, 1);
    return 1;
}
/* the check box (code 4): a changed value undoes the last untrim and redoes it with the new mode, no warning */
void srf_ut_values(int keep) {
    if (!ut_active || (keep != 0) == (ut_keep != 0)) return;
    ut_keep = keep;
    if (ut_last >= 0) { ut_terminate(); ut_do(ut_last, ut_keep ? 0 : 2, 0); }   /* UndoTransTerminate, FUN_18030dfc0(.., 0) */
}
void srf_ut_end(void) {                         /* codes 0 / 1 / 2, Esc: UndoTransCommit */
    if (!ut_active) return;
    ut_commit(); ut_active = 0; ut_last = -1; srf_pick_active = 0; overlay_dirty = 1;
}

/* ================================================================================================ Remove Boundary (G_SRMBND) */
/* RemoveBoundary (mccad 0x18008d310, decomp/surfedit/cad_cmds.c l.3): no function panel - a loop until Esc:
   - select_surface (mccore 0x1802deee0, decomp/surfedit2/core_untrim.c l.3): select_ent(mccadres 4869 (0x1305) "Select a surface:", mask
     0x100), then dyno_surf mode 3 without the AutoCursor (param_4 '\0') with the prompt mccadres 7425 (0x1d01) "Slide to boundary which is
     to be removed"; the arrow's (u, v) is the result; Esc in either ends the command (UndoTransTerminate, set_clr_sel_bits).
   - not a trimmed surface (kind 10): post_warning(7426 "Cannot remove boundaries on untrimmed surfaces").
   - the boundary: over the loops in order, each loop's segments p0 -> p1, .., p(N-3) -> p(N-2), p(N-2) -> p0 (N = the stored count + 2,
     the closing duplicate skipped): dist_to_finite_line(uv, segment, best) (mccore 0x1804b6680, decomp/mccore_proj2d.c); when it is
     closer, t_on_line (0x1804b48a0): 0.0001 < t < 0.9999 the perpendicular distance and the point on the segment, else the end's
     (start for t <= 0.0001, end otherwise) distance, closer than the best -> the best (loop, segment); best starts at 1e38 (loop 0,
     segment 1); the search stops at the first best below tol. The point kept (local_2bd8) is the last segment's that passed the first test.
   - find_outer_bndno (0x1808aa250): the loop holding the strictly smallest u (points start .. start + count).
   - the outer picked: boundary_is_untrimmed_outer (0x1808a69e0: from the loop's start, the record's point count of points, each on the
     base's range edges (1e-6) - all four corners met and no interior point) -> post_warning(7428 "Selected Edge is the untrimmed boundary
     and cannot be removed"); else outer_boundary_touches_untrimmed_outer (0x1808ab7f0: an outer point within 1e-6 of a range edge): the
     base evaluated at the kept point, check_edge (0x18047b620: within 0.01 in u / v of a range edge and within surf_step_chord_dev of
     the edge's point) != 0 -> post_warning(7429 "Selected boundary is the original untrimmed boundary and cannot be filled") and the
     command ends (LAB_18008da02). An inner loop picked with more than 2 loops: message_box_yes_no(7427 "Remove all internal
     boundaries?", 5002 "Warning") Yes -> every internal loop goes (-1).
   - UndoTransStart; remove_a_trimsurf_bndy (0x1808ab8c0): the outer picked and not touching the base's range: a new outer = the base's
     range rectangle (u0 v0, u1 v0, u1 v1, u0 v1, u0 v0); then the other loops in their order (with -1: the outer only); one loop left
     that is the untrimmed outer -> the level warning 7424 when the base's level is hidden + untrim_a_trimsurf(trimmed, 2, base); else
     store_new_trim_surf(rec, 2, 0, trimmed) = store_new_trim_surf_ext(.., 2, 0, .., 1, ..) (decomp/surfprim/storetrim2.c): the old
     record's header with the new loops, its normal bit, +0x108 = min(loops, 255), put_surf_in_db with the old colour / level /
     attributes, the base's references + 1 (inc_refs_eptr), the old trimmed surface deleted (delete_ent: its reference - 1); UndoTransCommit.
   Ours / NOT PORTED: the outer loop touching the base's range (FUN_1808abe50, the outer re-routed along the range edges: refused with
   a message); an outer loop that is not the record's first (boundary_is_untrimmed_outer reads on past the point array there: refused);
   (the operations using the surface follow it: tp_substitute_ent, decision S-9); the new record = mcsurf_build.c trim_rec (reduce_trimsurf_data +
   put_surf_in_db's header) with the old record's trim fields +0x109..+0x10b / +0x118..+0x127 copied; the base's reference count is left
   as it is (+1 - 1); no panel, as Mastercam (decision S-5): Esc ends the loop (geo_has_panel). */
static int rb_active;
static const double *rb_pt(const uint8_t *r, int i) { return (const double *)(r + 0x138 + 16 * (size_t)i); }
static int rb_loops(const Surf *s, int *m, int *nl) {
    if (s->kind != 10 || s->n < 0x138) return 0;
    *m = r32s(s->rec + 0x10c); *nl = r32s(s->rec + 0x128);
    return *m > 0 && *nl > 0 && 0x138 + 16 * (size_t)*m + 8 * (size_t)*nl <= s->n;
}
static void rb_loop(const Surf *s, int m, int l, int *st, int *cn) { const uint8_t *t = s->rec + 0x138 + 16 * (size_t)m + 8 * (size_t)l; *st = r32s(t); *cn = r32s(t + 4); }
/* find_base_surf (mccore 0x1806f75b0): down kinds 10 (+0x98) and 8 (+0xb8, its distance summed) to the first other kind */
static const Surf *rb_base(const Surf *s) {
    for (int k = 0; s && k < 32; k++) {
        int64_t ep;
        if (s->kind == 10 && s->n >= 0x108) memcpy(&ep, s->rec + 0x100, 8);
        else if (s->kind == 8 && s->n >= 0x128) memcpy(&ep, s->rec + 0x120, 8);
        else return s;
        s = mcs_surf(&F, ep);
    }
    return NULL;
}
static int rb_outer(const Surf *s, int m, int nl) {   /* find_outer_bndno */
    if (nl < 2) return 0;
    int st, cn; rb_loop(s, m, 0, &st, &cn);
    double mn = 1e38; for (int i = 0; i <= cn; i++) if (st + i < m && rb_pt(s->rec, st + i)[0] < mn) mn = rb_pt(s->rec, st + i)[0];
    int o = 0; double best = mn;
    for (int l = 1; l < nl; l++) { rb_loop(s, m, l, &st, &cn);
        for (int i = 0; i <= cn; i++) if (st + i < m && rb_pt(s->rec, st + i)[0] < mn) mn = rb_pt(s->rec, st + i)[0];
        if (mn < best) { best = mn; o = l; } }
    return o;
}
/* boundary_is_untrimmed_outer over a point array P (count m) with loop start st, the base range d; -1 = reads past the points (NOT PORTED) */
static int rb_untrimmed_outer(const double *P, int m, int st, const double d[4]) {
    if (st != 0) return -1;
    int all = 1, c00 = 0, c10 = 0, c01 = 0, c11 = 0; double u0 = d[0] + 1e-6;
    for (int i = 0; i < m && all; i++) {
        double u = P[2 * (st + i)], v = P[2 * (st + i) + 1];
        if (!(u <= u0 || d[1] - 1e-6 <= u || v <= d[2] + 1e-6 || d[3] - 1e-6 <= v)) { all = 0; break; }
        if (!(u0 < u || d[2] + 1e-6 < v)) c00 = 1;
        else if (!(u < d[1] - 1e-6 || d[2] + 1e-6 < v)) c10 = 1;
        else if (!(u0 < u || v < d[3] - 1e-6)) c01 = 1;
        else if (d[1] - 1e-6 <= u && d[3] - 1e-6 <= v) c11 = 1;
    }
    return all && c00 && c10 && c01 && c11;
}
static int rb_touches(const Surf *s, int m, int o, const double d[4]) {   /* outer_boundary_touches_untrimmed_outer */
    int st, cn; rb_loop(s, m, o, &st, &cn);
    for (int i = 0; i < cn; i++) { const double *p = rb_pt(s->rec, st + i);
        if (fabs(p[0] - d[0]) < 1e-6 || fabs(p[0] - d[1]) < 1e-6 || fabs(p[1] - d[2]) < 1e-6 || fabs(p[1] - d[3]) < 1e-6) return 1; }
    return 0;
}
/* check_edge (mccore 0x18047b620): != 0 when the point is within 0.01 of a range edge in u or v and the edge's point is within
   surf_step_chord_dev (eval_surf_extrap at the edge: ours mcs_surf_eval) */
static int rb_check_edge(const Surf *b, const double d[4], double uv[2], double P[3]) {
    int r = 0; double Q[3], dev = mc_systol.surf_step_chord_dev;
    if (!(0.01 <= fabs(uv[0] - d[0]))) { if (!mcs_surf_eval(&F, b, d[0], uv[1], Q) && sqrt((P[0] - Q[0]) * (P[0] - Q[0]) + (P[1] - Q[1]) * (P[1] - Q[1]) + (P[2] - Q[2]) * (P[2] - Q[2])) < dev) { r = 8; memcpy(P, Q, sizeof Q); uv[0] = d[0]; } }
    else if (fabs(uv[0] - d[1]) < 0.01) { if (!mcs_surf_eval(&F, b, d[1], uv[1], Q) && sqrt((P[0] - Q[0]) * (P[0] - Q[0]) + (P[1] - Q[1]) * (P[1] - Q[1]) + (P[2] - Q[2]) * (P[2] - Q[2])) < dev) { r = 2; memcpy(P, Q, sizeof Q); uv[0] = d[1]; } }
    if (!(0.01 <= fabs(uv[1] - d[2]))) { if (!mcs_surf_eval(&F, b, uv[0], d[2], Q) && sqrt((P[0] - Q[0]) * (P[0] - Q[0]) + (P[1] - Q[1]) * (P[1] - Q[1]) + (P[2] - Q[2]) * (P[2] - Q[2])) < dev) r |= 1; }
    else if (fabs(uv[1] - d[3]) < 0.01) { if (!mcs_surf_eval(&F, b, uv[0], d[3], Q) && sqrt((P[0] - Q[0]) * (P[0] - Q[0]) + (P[1] - Q[1]) * (P[1] - Q[1]) + (P[2] - Q[2]) * (P[2] - Q[2])) < dev) r |= 4; }
    return r;
}
int srf_rb_active(void) { return rb_active; }
void srf_rb_begin(void) { rb_active = 1; srf_pick_active = 1; }
void srf_rb_end(void) { rb_active = 0; srf_pick_active = 0; }
/* the arrow's start: the pick's (u, v) on the surface (select_ent's p_2d; ours mcsc_uv_of_point, as cui_dsurf.c's callers) */
int srf_rb_begin_dyno(int e, V3 w, char *msg, size_t msgn) {
    const Surf *s = &F.surfs[F.ents[e].srf]; double P[3] = { w.x, w.y, w.z }, u, v;
    if (!mcsc_uv_of_point(&F, s, P, &u, &v)) { snprintf(msg, msgn, "Remove Boundary: the surface has no parameter range"); return 0; }
    ds_start(e, 3, 0, u, v); return 1;
}
/* the rest of the loop body after dyno_surf; 0 = the command ends (7429) */
int srf_rb_exec(int e, double u, double v) {
    if (!surf_ent_ok(e)) return 1;
    const Surf *s = &F.surfs[F.ents[e].srf]; int m, nl;
    if (s->kind != 10) { cui_message("apprenticeCAM", "Cannot remove boundaries on untrimmed surfaces"); return 1; }   /* 7426 */
    if (!rb_loops(s, &m, &nl)) return 1;
    const double tol = mc_systol.tol, uv[2] = { u, v };
    double best = 1e38, near[2]; int bl = 0, bseg = 1;
    { const double *p0 = rb_pt(s->rec, 0); near[0] = p0[0]; near[1] = p0[1]; }
    int pi = 0;                                                /* the points walked in order, loop after loop (pauVar15) */
    for (int l = 0; l < nl; l++) {
        int st, cn; rb_loop(s, m, l, &st, &cn); int N = cn + 2;
        const double *first = rb_pt(s->rec, pi), *a = first;
        for (int k = 1; k < N; k++) {
            const double *b = (k != N - 1 && pi + k < m) ? rb_pt(s->rec, pi + k) : first;
            double dx = b[0] - a[0], dy = b[1] - a[1], L = sqrt(dx * dx + dy * dy), d; int fl;
            if (tol <= L) {                                    /* dist_to_finite_line */
                d = fabs((uv[0] - a[0]) * dy - (uv[1] - a[1]) * dx) / L;
                if (best <= d) fl = 0;
                else { double t = ((uv[0] - a[0]) * dx + (uv[1] - a[1]) * dy) / L;
                    if (0.0 <= t) { if (t <= L) fl = 1; else if (L + best <= t) fl = 0; else { d = sqrt((b[1] - uv[1]) * (b[1] - uv[1]) + (b[0] - uv[0]) * (b[0] - uv[0])); fl = d < best; } }
                    else if (t <= -best) fl = 0; else { d = sqrt((a[1] - uv[1]) * (a[1] - uv[1]) + (a[0] - uv[0]) * (a[0] - uv[0])); fl = d < best; } }
            } else { d = sqrt((a[1] - uv[1]) * (a[1] - uv[1]) + (a[0] - uv[0]) * (a[0] - uv[0])); fl = d < best; }
            if (fl) {
                double t = L * L > tol * tol ? (dy * (uv[1] - a[1]) + dx * (uv[0] - a[0])) / (L * L) : 0.0;   /* t_on_line */
                near[0] = b[0]; near[1] = b[1];
                if (0.0001 < t) {
                    if (t < 0.9999) { near[0] = a[0] * (1 - t) + b[0] * t; near[1] = a[1] * (1 - t) + b[1] * t; }
                    else { d = sqrt((near[1] - uv[1]) * (near[1] - uv[1]) + (near[0] - uv[0]) * (near[0] - uv[0])); fl = d < best; }
                } else { near[0] = a[0]; near[1] = a[1]; d = sqrt((a[0] - uv[0]) * (a[0] - uv[0]) + (a[1] - uv[1]) * (a[1] - uv[1])); fl = d < best; }
                if (fl) { best = d; bl = l; bseg = k; }
            }
            a = b;
            if (fabs(best) < tol) break;
        }
        pi += N;
        if (fabs(best) < tol) break;
    }
    (void)bseg;
    const Surf *b = rb_base(s); if (!b) return 1;
    double d[4]; mcs_surf_domain(b, d);
    int o = rb_outer(s, m, nl), sel = bl;
    if (bl == o) {
        int ost, ocn; rb_loop(s, m, o, &ost, &ocn);
        int uo = rb_untrimmed_outer((const double *)(s->rec + 0x138), m, ost, d);
        if (uo < 0) { snprintf(cam_msg, sizeof cam_msg, "Remove Boundary: the outer boundary is not the record's first loop (NOT PORTED) - Select a surface:"); return 1; }
        if (uo) { cui_message("apprenticeCAM", "Selected Edge is the untrimmed boundary and cannot be removed"); return 1; }   /* 7428 */
        if (rb_touches(s, m, o, d)) {
            double P[3], q[2] = { near[0], near[1] };
            if (!mcs_surf_eval(&F, b, q[0], q[1], P) && rb_check_edge(b, d, q, P)) { cui_message("apprenticeCAM", "Selected boundary is the original untrimmed boundary and cannot be filled"); return 0; }   /* 7429: the command ends */
            snprintf(cam_msg, sizeof cam_msg, "Remove Boundary: the outer boundary touches the base's edge (FUN_1808abe50 NOT PORTED) - Select a surface:");
            return 1;
        }
    } else if (nl > 2 && (!cui_hooks.ask_yes_no || cui_hooks.ask_yes_no("Warning", "Remove all internal boundaries?"))) sel = -1;   /* 7427 / 5002 */
    /* remove_a_trimsurf_bndy: the new loops */
    double **pts = calloc((size_t)nl + 1, sizeof *pts); int *cnt = calloc((size_t)nl + 1, sizeof *cnt), nn = 0;
    if (sel >= 0 && sel == o) {                                 /* the base's range as the outer */
        static const int cu[5] = { 0, 1, 1, 0, 0 }, cv[5] = { 2, 2, 3, 3, 2 };
        pts[0] = malloc(sizeof(double) * 10); for (int i = 0; i < 5; i++) { pts[0][2 * i] = d[cu[i]]; pts[0][2 * i + 1] = d[cv[i]]; }
        cnt[0] = 5; nn = 1;
    }
    for (int l = 0; l < nl; l++) {
        if (sel == -1 ? l != o : l == sel) continue;
        int st, cn; rb_loop(s, m, l, &st, &cn); int N = cn + 2; if (st < 0 || st + N > m) continue;
        pts[nn] = malloc(sizeof(double) * 2 * (size_t)N); memcpy(pts[nn], rb_pt(s->rec, st), sizeof(double) * 2 * (size_t)N); cnt[nn++] = N;
    }
    int ok = 1;
    if (nn == 1 && rb_untrimmed_outer(pts[0], cnt[0], 0, d) == 1) {   /* one loop left, the untrimmed outer: untrim_a_trimsurf(.., 2, ..) */
        if (ut_do(e, 2, 1)) ut_commit(); else ok = 0;
    } else {
        int64_t bep; memcpy(&bep, s->rec + 0x100, 8);
        uint16_t fl = (uint16_t)(s->rec[0x6e] | s->rec[0x6f] << 8);
        const McSysTol *stl = mc_systol_units(mcam_is_metric(&F)); uint32_t tn = 0;
        uint8_t *t = mcsb_trim_rec(&F, bep, b->rec, b->n, nn, pts, cnt, fl, solid_mode != 0, stl->tol, &tn);
        if (!t) ok = 0;
        else {
            memcpy(t + 0x109, s->rec + 0x109, 3); memcpy(t + 0x118, s->rec + 0x118, 0x10);   /* the old record's trim fields */
            t[0x6e] = (uint8_t)((t[0x6e] & ~4) | (s->rec[0x6e] & 4));                    /* store_new_trim_surf_ext: the normal bit */
            mod_begin();
            int ne = mcam_new_surf(&F, t, tn, s->level, s->color); free(t);
            if (ne >= 0 && ne < F.nents) { Surf *ns = &F.surfs[F.ents[ne].srf]; memcpy(ns->rec + 0x10, s->rec + 0x10, 0x10); }   /* the old attributes (+0x10 / +0x18) */
            int assoc = tp_assoc(e);
            mod_del(e);
            if (assoc && ne >= 0 && ne < F.nents) tp_substitute(e, ne);   /* store_new_trim_surf_ext: add_undo_eptr_reference(5) + tp_substitute_ent(old, new, 0, 1) */
            mod_end();
            dirty_ent_users(e); doc_dirty = 1; scene_surfs_update(1);
        }
    }
    for (int l = 0; l < nn; l++) free(pts[l]); free(pts); free(cnt);
    if (!ok) snprintf(cam_msg, sizeof cam_msg, "Remove Boundary: the new trimmed surface could not be made - Select a surface:");
    overlay_dirty = 1;
    return 1;
}
void srf_trim_dump(void) {                      /* tests: the loops of every live trimmed surface */
    for (int i = 0; i < F.nsurfs; i++) { const Surf *s = &F.surfs[i]; int m, nl; if (s->off || !rb_loops(s, &m, &nl)) continue;
        printf("      srf trim %d ent %d points %d loops %d:", i, s->ent, m, nl);
        for (int l = 0; l < nl; l++) { int st, cn; rb_loop(s, m, l, &st, &cn); printf(" {%d,%d}", st, cn); }
        printf("\n"); }
}
void srf_tp_dump(void) {                        /* tests: the stored ops' surface references (kind 4) and the surface each resolves to now */
    for (int k = 0; k < F.ntp; k++) { const TpRef *t = &F.tp[k]; if (t->kind != 4 || t->ecls != TP_ECLS_SURF) continue;
        int ss = tp_ref_subst(&F, t), r = ss; if (r < 0) for (int q = 0; q < F.nsurfs; q++) if (F.surfs[q].idn == t->ent) { r = q; break; }
        printf("      op tpref %d ent %d -> surf %d%s%s\n", t->op_id, t->ent, r, ss >= 0 ? " (substituted)" : "", r >= 0 && F.surfs[r].off ? " (deleted)" : ""); }
}

/* ================================================================================================ Offset (G_SOFFSET) */
/* OffsetSurfaces (mccad 0x1802f2ef0) -> SurfaceOffsetApp (ctor FUN_1802ef3c0: +8 the selection, +0x10 its count, +0x18 the distance,
   +0x20 CArray of the surfaces made, +0x48 Move, +0x49 / +0x4a the toolpath question asked / its answer, +0x4c the Cycle index -1)
   FUN_1802f1420 (decomp/surfoffset/cad_offset.c, formats/surfaces_tab_plan.md "D. Offset"):
   - FUN_1802f21b0: select_ents(mccadres 7420 (0x1cfc) "Select surfaces to offset", mask 0x100, End Selection); none: the loop ends.
   - the build FUN_1802efc30(Move, distance): UndoTransStart; per selected surface offset_a_surface(1, ent, distance, 0, &new) (mccore
     0x18026c860, decomp/curves/core1.c l.8453); a valid one joins the list, with Move the original is erased from the display
     (draw_ent_erase); sktcher_live_color(list); EnableApply(1).
   - get_event (0x10008008, | 2 in Single flip, mask 0x100) prompts: Single flip 7421 (0x1cfd) "Select surface to flip", Cycle / Next
     7464 (0x1d28) "Select flip or next", else none (cleartextall).
   - the manager's codes (SurfaceOffsetManager vtable 0x1804972b8, PostMessage 0x8038 FUN_1802ab010): 4 Reselect (+0x88), 7 Single flip
     (+0xa0), 8 Cycle / Next (+0xa8), 9 Calculate (+0x90), 10 Flip (+0x98); the Value observers: 12 IsMove, 11 OffsetDistance, 13 / 14 the
     Max - / + texts (lambdas FUN_1802aa4c0 / aa5a0 / aa560 / aa590). 0 / 1 / 2 OK / OK-New / Cancel.
   offset_a_surface(1, ent, d, 0, out): d negated when the surface's +6 bit 4 (normal reversed) is set; a trimmed surface (10): its base
   (+0x100), then down nested offsets (8: + their +0x118, base +0x120) - the record is made blanked (store flag 1); an offset surface: its
   own +0x118 plus the nested ones, the base found the same way; other kinds: the surface itself. The kind 8 record = the surface's surf
   struct with kind 8, the base's range (8 / 10) or its own, +0x118 = d + the nested offsets, +0x120 = the base; at the middle of the range
   eval_surf(1) + calc_surf_unit_norm -> +0x100 (computed on the struct before put_surf_in_db: its +0xd8 size is the copied one).
   put_surf_in_db(main_color, main_level) (+4 |= 8 on the new record); the base's references + 1 (8 / 10) or the surface's own, the
   surface's +4 bit 2 set. A trimmed surface: a copy of it (kind 10, the loops copied: alloc_trimsurf_data_ptr + memcpy) on the new offset
   surface (put_surf_in_db, +4 |= 8), the offset surface's references + 1 and the base's + 1 again; *out = the copy.
   Ours / NOT PORTED: Calculate (code 9) and the analysis after a single flip with one surface (FUN_1802f07f0: MaSurface::
   MinMaxPrincipalNormalCurvatures over MaSurfHuntingGrid::GenGrid(chord) 0x180495000 = TessellateUntrimmed in its chord mode, not
   transliterated) - greyed, the Max + / - texts stay empty; the status bar's attribute events (0x8000: sktcher_updt_attrib); ea_adjustment
   in sktcher_main_color; the arrow's length / head as srf_ov's (GrArrow); the normal +0x100 through mcpartline_mesh.c's bit-exact
   eval_surf / calc_surf_unit_norm where they cover the base (kinds 0 / 1 and offsets / trimmed over them), else mcsrfcrv.c's frame; the
   header (closed, extents) through mcsurf_build.c mcsb_finish (its numeric derivatives for kinds other than 0, as every new surface). */
#include "../mcpartline.h"
int mcsb_finish(const McamFile *f, uint8_t *rec, uint32_t n, int shaded, double tol);
int mcsb_extent(const McamFile *f, uint8_t *rec, uint32_t n, int mode, double tol);
static int of_active, of_selecting, of_mode, of_move, of_idx = -1, of_flipen, of_asked, of_subst, of_open;   /* of_mode 1 Single flip (bVar6), 2 Cycle (bVar7) */
static double of_dist;                           /* +0x18: the distance the list was built / last changed with */
static int *of_sel, of_n, of_cap, *of_hid;       /* +8 / +0x10 (Ent indices); of_hid: erased from the display (Move) */
static int *of_new, of_nn, of_ncap;              /* +0x20: the surfaces made (Ent indices, compacted as the CArray) */
static char of_pr[96];
int srf_of_active(void) { return of_active; }
int srf_of_selecting(void) { return of_active && of_selecting; }
int srf_of_flip_enabled(void) { return of_active && of_flipen; }
const char *srf_of_prompt(void) {
    if (of_selecting) snprintf(of_pr, sizeof of_pr, "Select surfaces to offset");         /* mccadres 7420 */
    else if (of_mode == 1) snprintf(of_pr, sizeof of_pr, "Select surface to flip");       /* 7421 */
    else if (of_mode == 2) snprintf(of_pr, sizeof of_pr, "Select flip or next");          /* 7464 */
    else of_pr[0] = 0;                                                                     /* cleartextall */
    return of_pr;
}
static void of_prompt(void) { snprintf(cam_msg, sizeof cam_msg, "%s", srf_of_prompt()); }
static Surf *of_surf(int e) { return surf_ent_ok(e) ? &F.surfs[F.ents[e].srf] : NULL; }
static int64_t of_rd64(const uint8_t *p) { int64_t v; memcpy(&v, p, 8); return v; }
/* the selected surface's chain: a trimmed one's base, then down the offsets (their distances x f summed; a trimmed base followed) -
   FUN_1802efd50 / FUN_1802f0120 / FUN_1802f0480 / FUN_1802f0cd0's loops */
static const Surf *of_chain(const Surf *s, double f, double *acc) {
    if (s && s->kind == 10) s = s->n >= 0x108 ? mcs_surf(&F, of_rd64(s->rec + 0x100)) : NULL;
    for (int g = 0; s && s->kind == 8 && g < 64; g++) {
        if (s->n < 0x128) return NULL;
        *acc += f * rdd8(s->rec + 0x118);
        s = mcs_surf(&F, of_rd64(s->rec + 0x120));
        if (s && s->kind == 10) s = s->n >= 0x108 ? mcs_surf(&F, of_rd64(s->rec + 0x100)) : NULL;
    }
    return s;
}
static void of_refs(int s, int d) {             /* fill_in_ent, the ent's +0x28 changed, rewrite_ent (inside the transaction) */
    if (s < 0 || s >= F.nsurfs) return;
    Surf *sf = &F.surfs[s]; if (sf->n < 0x2a) return;
    srf_tx_save(s); sf = &F.surfs[s];
    int16_t rc = r16s(sf->rec + 0x28); rc = (int16_t)(rc + d); memcpy(sf->rec + 0x28, &rc, 2); sf->edited = 1;
}
static void of_color(int e, int c) {            /* the entity's colour (+6) rewritten */
    if (e < 0 || e >= F.nents) return;
    F.ents[e].color = c; if (surf_ent_ok(e)) { Surf *s = &F.surfs[F.ents[e].srf]; s->color = c; s->rec[6] = (uint8_t)c; }
}
/* eval_surf(1) + calc_surf_unit_norm at (u, v): the bit-exact ports where they cover the surface, else mcsrfcrv.c's frame */
static int of_unit_normal(const Surf *s, double u, double v, double P[3], double N[3]) {
    double D[10][3];
    if (!plm_eval(&F, s, 1, u, v, D)) { memcpy(P, D[0], 24); if (plm_unit_norm(&F, s, D[1], D[2], u, v, N) >= 0) return 1; }
    double Su[3], Sv[3]; return mcsc_surf_frame(&F, s, u, v, P, Su, Sv, N);
}
/* offset_a_surface(1, ent, d, 0, &out); returns the Ent made (the trimmed copy for a trimmed surface), -1 none */
static int of_offset_a_surface(int e, double d) {
    const Surf *in = of_surf(e); if (!in || in->n < 0x100) return -1;
    int ink = in->kind, si = F.ents[e].srf;
    uint8_t R[0x130]; memset(R, 0, sizeof R);                    /* the callers' ent buffers are zeroed (memset 0x2938) before get_ent_from_eptr */
    memcpy(R + 0x68, in->rec + 0x68, (in->n < 0x130 ? in->n : 0x130) - 0x68); R[0x68] = 8;   /* local_d0e8: the surf struct, kind 8 */
    if (in->rec[0x6e] & 4) d = -d;
    int64_t base = in->eptr; double acc = 0; int blank = 0, bsi = si;
    if (ink == 10 || ink == 8) {
        if (ink == 8) acc = rdd8(in->rec + 0x118);
        const Surf *b = mcs_surf(&F, of_rd64(in->rec + (ink == 10 ? 0x100 : 0x120))); if (!b) return -1;   /* fill_in_ent fails: return */
        for (int g = 0; b->kind == 8 && g < 64; g++) {            /* the do-while on kind 8 (fill_in_ent of +0x120) */
            if (b->n < 0x128) return -1;
            acc += rdd8(b->rec + 0x118);
            const Surf *nb = mcs_surf(&F, of_rd64(b->rec + 0x120)); if (!nb) return -1;
            b = nb;
        }
        base = b->eptr; memcpy(R + 0x70, b->rec + 0x70, 32);      /* the base's range */
        bsi = (int)(b - F.surfs);
        if (ink == 10) blank = 1;                                 /* cVar4 = 1 */
    }
    wdd8(R + 0x118, d + acc); memcpy(R + 0x120, &base, 8);
    double u = (rdd8(R + 0x78) + rdd8(R + 0x70)) * 0.5, v = (rdd8(R + 0x88) + rdd8(R + 0x80)) * 0.5, P[3], N[3];
    {   Surf t; memset(&t, 0, sizeof t); t.rec = R; t.n = sizeof R; t.kind = 8; t.ent = -1;
        if (!of_unit_normal(&t, u, v, P, N)) N[0] = N[1] = N[2] = 0; }
    for (int k = 0; k < 3; k++) wdd8(R + 0x100 + 8 * k, N[k]);
    int shaded = solid_mode != 0, col = geo_color != cfg_back_color ? srf_front_color : geo_color;   /* put_surf_in_db: main_color -> surfaceFrontColor */
    double tol = mc_systol.tol;
    R[0x6b] = 0;                                                  /* put_surf_in_db: struct bytes 0..3 rebuilt */
    if (mcsb_finish(&F, R, sizeof R, shaded, tol)) return -1;     /* a range < 1e-12: *out = 0 */
    R[4] = (uint8_t)(R[4] | 8);                                   /* the new record's +4 |= 8 */
    int ne = mcam_new_surf(&F, R, sizeof R, geo_level, col);
    if (blank) { Surf *bs = &F.surfs[F.ents[ne].srf]; bs->blanked = 1; bs->rec[5] |= 1; F.ents[ne].blanked = 1; }   /* stored blanked */
    of_refs(bsi, 1);                                              /* param_1: the base's references + 1 (8 / 10) or the surface's own */
    srf_tx_save(si); { Surf *s = &F.surfs[si]; selw_set(s, (uint16_t)(selw(s) | 2)); s->edited = 1; }   /* the surface's +4 |= 2 */
    if (ink != 10) return ne;
    /* the trimmed copy on the new offset surface: the loops as the original's (reduce_trimsurf_data in put_surf_in_db), its header */
    in = &F.surfs[si];
    int m, nl; if (!rb_loops(in, &m, &nl)) return -1;
    double **pts = calloc((size_t)nl, sizeof *pts); int *cnt = calloc((size_t)nl, sizeof *cnt), ok = 1;
    for (int l = 0; l < nl && ok; l++) {
        int st, cn; rb_loop(in, m, l, &st, &cn); int Np = cn + 2;
        if (st < 0 || st + Np > m) { ok = 0; break; }
        pts[l] = malloc(sizeof(double) * 2 * (size_t)Np); memcpy(pts[l], in->rec + 0x138 + 16 * (size_t)st, sizeof(double) * 2 * (size_t)Np); cnt[l] = Np;
    }
    int ct = -1;
    if (ok) {
        const Surf *os = &F.surfs[F.ents[ne].srf]; uint32_t tn = 0;
        uint16_t fl = (uint16_t)(in->rec[0x6e] | in->rec[0x6f] << 8);
        uint8_t *T = mcsb_trim_rec(&F, F.ents[ne].septr, os->rec, os->n, nl, pts, cnt, fl, shaded, tol, &tn);
        if (T) {
            in = &F.surfs[si];
            memcpy(T + 0x6c, in->rec + 0x6c, 2);                  /* struct bytes 4..5 kept */
            memcpy(T + 0x108, in->rec + 0x108, 4); memcpy(T + 0x118, in->rec + 0x118, 0x10);   /* local_d048 / +0x118..: the original's */
            T[0x124] = 1;                                         /* put_surf_in_db: kind 10 */
            T[0x6b] = 0;
            if (!mcsb_finish(&F, T, tn, shaded, tol)) {
                T[4] = (uint8_t)(T[4] | 8);
                ct = mcam_new_surf(&F, T, tn, geo_level, col);
                of_refs(F.ents[ne].srf, 1);                       /* the offset surface's references + 1 */
                of_refs(bsi, 1);                                  /* and the base's again ((kind - 8) & 0xfd == 0) */
            }
            free(T);
        }
    }
    for (int l = 0; l < nl; l++) free(pts[l]); free(pts); free(cnt);
    return ct;                                                    /* *param_5 = the second put_surf_in_db's (NULL when it failed) */
}
static void of_hide(int i, int hide) {          /* draw_ent_erase / draw_ent of the original (Move): the display only */
    if (i < 0 || i >= of_n || !surf_ent_ok(of_sel[i])) return;
    Surf *s = &F.surfs[F.ents[of_sel[i]].srf];
    if (hide && !of_hid[i]) { of_hid[i] = 1; s->off = 1; }
    else if (!hide && of_hid[i]) { of_hid[i] = 0; s->off = 0; }
}
static void of_unhide_all(void) { for (int i = 0; i < of_n; i++) of_hide(i, 0); }
static void of_unhighlight(void) {              /* set_clr_sel_bits(0, 4, 0, 4, 0x100, 1, 0) */
    for (int i = 0; ent_sel && i < ent_sel_n && i < F.nents; i++) if (ent_sel[i] && F.ents[i].type == E_SURF) { ent_sel[i] = 0; nselected--; }
}
/* FUN_1802efc30: UndoTransStart, the surfaces made, sktcher_live_color */
static void of_build(int move, double dist) {
    of_move = move != 0; of_dist = dist; of_nn = 0;
    mod_begin(); of_open = 1;
    for (int i = 0; i < of_n; i++) {
        int ne = of_offset_a_surface(of_sel[i], of_dist);
        if (ne >= 0 && ne < F.nents && F.ents[ne].type) {
            if (of_nn == of_ncap) { of_ncap = of_ncap ? 2 * of_ncap : 16; of_new = realloc(of_new, sizeof(int) * (size_t)of_ncap); }
            of_new[of_nn++] = ne;
            if (of_move) of_hide(i, 1);
        }
    }
    for (int k = 0; k < of_nn; k++) of_color(of_new[k], cfg_live_color);   /* sktcher_live_color */
    doc_dirty = 1; scene_surfs_update(1); overlay_dirty = 1;
}
/* UndoTransTerminate of the open build */
static void of_terminate(void) {
    if (!of_open) return;
    of_unhide_all();
    srf_tx_cancel();
    for (int i = mod_from; mod_from >= 0 && i < F.nents; i++) if (F.ents[i].type) ent_off(i);
    urec_free(&mod_rec); memset(&mod_rec, 0, sizeof mod_rec); mod_from = -1;
    of_open = 0; of_nn = 0;
    for (int i = 0; i < F.nsurfs; i++) if (F.surfs[i].ent >= 0 && F.surfs[i].ent < F.nents) mcam_ent_blank(&F, F.surfs[i].ent, F.surfs[i].blanked);
    scene_surfs_update(1); overlay_dirty = 1;
}
/* FUN_1802efa30 (Move): per original - the toolpath question once (ent_is_assoc, tp_blank_surf_yes_no: mccoreres 5028 / 5188), selection
   bits 4 and 2 off, delete_ent(eptr, 1, 1, ..) = blanked (param_3 1: put_special_undo(4), sel bit 0x100), then with Yes
   add_undo_eptr_reference(5) + tp_substitute_ent(original, made, 0, 1) */
static void of_move_originals(void) {
    for (int i = 0; i < of_n; i++) {
        int e = of_sel[i]; if (!surf_ent_ok(e)) continue;
        if (!of_asked && tp_assoc(e)) {
            of_asked = 1;
            of_subst = cui_hooks.ask_yes_no ? cui_hooks.ask_yes_no("Operations Manager", "Blanking surfaces used in a toolpath.  Update toolpaths to use the new surfaces?") : 1;
        }
        int s = F.ents[e].srf;
        srf_tx_save(s);
        Surf *sf = &F.surfs[s];
        selw_set(sf, (uint16_t)((selw(sf) & ~(4 | 2)) | 0x100));
        sf->blanked = 1; sf->edited = 1; mcam_ent_blank(&F, e, 1);
        if (ent_sel && e < ent_sel_n && ent_sel[e]) { ent_sel[e] = 0; nselected--; }
        if (of_subst && i < of_nn) tp_substitute(e, of_new[i]);
    }
}
/* FUN_1802f0ad0: sktcher_main_color, Move's originals, UndoTransCommit (Terminate with nothing made), set_disp_mult_arrows(0), the lists freed */
static void of_commit(void) {
    if (of_open) {
        for (int k = 0; k < of_nn; k++) of_color(of_new[k], geo_color);   /* sktcher_main_color: main_color */
        of_unhide_all();
        if (of_nn < 1) of_terminate();
        else {
            if (of_move) of_move_originals();
            mod_rec.sx = srf_tx_take();
            mod_end(); of_open = 0;
        }
    }
    of_n = 0; of_nn = 0; of_idx = -1; of_mode = 0;
    scene_surfs_update(1); overlay_dirty = 1;
}
/* FUN_1802f0cd0: the made surfaces' distances (in place): the sign each had against the old distance, the nested offsets added again */
static void of_set_dist(double nd) {
    double od = of_dist; of_dist = nd;
    for (int i = 0; i < of_nn; i++) {
        Surf *c = of_surf(of_new[i]); if (!c) continue;
        Surf *o = c;
        if (c->kind == 10) { o = (Surf *)mcs_surf(&F, of_rd64(c->rec + 0x100)); if (!o) continue; }
        if (o->kind != 8 || o->n < 0x128) continue;
        double nv = (rdd8(o->rec + 0x118) * od >= 0.0 ? 1 : -1) * nd;
        if (i < of_n) of_chain(of_surf(of_sel[i]), 1.0, &nv);
        wdd8(o->rec + 0x118, nv);
        Surf *x = c->kind == 10 ? c : o;                          /* surf_extent(3) of the trimmed copy, or of the offset surface */
        mcsb_extent(&F, x->rec, x->n, 3, mc_systol.tol);
    }
    doc_dirty = 1; scene_surfs_update(1); overlay_dirty = 1;
}
/* FUN_1802f0480: made surface i flipped: -distance + 2 x the nested offsets; the selected bit off; surf_extent(3) */
static void of_flip(int i) {
    if (i < 0 || i >= of_nn || i >= of_n) return;
    Surf *c = of_surf(of_new[i]); if (!c) return;
    Surf *o = c;
    if (c->kind == 10) { o = (Surf *)mcs_surf(&F, of_rd64(c->rec + 0x100)); if (!o) return; }
    if (o->kind != 8 || o->n < 0x128) return;
    double nv = -rdd8(o->rec + 0x118);
    of_chain(of_surf(of_sel[i]), 2.0, &nv);
    wdd8(o->rec + 0x118, nv);
    Surf *x = c->kind == 10 ? c : o;
    selw_set(x, (uint16_t)(selw(x) & ~4));
    mcsb_extent(&F, x->rec, x->n, 3, mc_systol.tol);
    /* one surface selected: FUN_1802f07f0 (the analysis, as Calculate) - NOT PORTED */
    doc_dirty = 1; scene_surfs_update(1); overlay_dirty = 1;
}
/* FUN_1802efd50: the arrow of made surface i (DisplayArrow(P, N, 1 = reversed, 0.1, -2)): the offset surface's middle (a trimmed copy:
   its base), N = calc_surf_unit_norm there; negated when the distance < 0 and again when the surface's distance equals distance + the
   nested offsets; drawn reversed */
static int of_arrow(int i, double P[3], double A[3]) {
    if (i < 0 || i >= of_nn || i >= of_n) return 0;
    const Surf *o = of_surf(of_new[i]); if (!o) return 0;
    if (o->kind == 10) { o = mcs_surf(&F, of_rd64(o->rec + 0x100)); if (!o) return 0; }
    if (o->kind != 8 || o->n < 0x128) return 0;
    double u = (rdd8(o->rec + 0x78) + rdd8(o->rec + 0x70)) * 0.5, v = (rdd8(o->rec + 0x88) + rdd8(o->rec + 0x80)) * 0.5, N[3];
    if (!of_unit_normal(o, u, v, P, N)) return 0;
    double acc = 0; of_chain(of_surf(of_sel[i]), 1.0, &acc);
    if (of_dist < 0.0) for (int k = 0; k < 3; k++) N[k] = -N[k];
    if (rdd8(o->rec + 0x118) == of_dist + acc) for (int k = 0; k < 3; k++) N[k] = -N[k];
    for (int k = 0; k < 3; k++) A[k] = -N[k];
    return 1;
}
void srf_of_ov(float **pv, int *pn, int *pcap) {
    if (!of_active || of_selecting || !of_mode) return;
    float rgb[3]; mc_rgb(cfg_back_color == 2 || cfg_back_color == 10 ? 4 : 0x56, rgb);   /* DisplayArrow colour -2 */
    double L = scene_view_height() * 0.1, h = L * 0.2;
    for (int i = 0; i < of_nn; i++) {
        if (of_mode == 2 && i != of_idx) continue;
        double p[3], n[3]; if (!of_arrow(i, p, n)) continue;
        V3 a = { p[0], p[1], p[2] }, t = { p[0] + n[0] * L, p[1] + n[1] * L, p[2] + n[2] * L };
        ov_pt(pv, pn, pcap, a, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, t, rgb[0], rgb[1], rgb[2]);
        double w[3] = { n[1] * vmat[8] - n[2] * vmat[7], n[2] * vmat[6] - n[0] * vmat[8], n[0] * vmat[7] - n[1] * vmat[6] };
        double wl = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]); if (wl < 1e-9) { w[0] = vmat[0]; w[1] = vmat[1]; w[2] = vmat[2]; wl = 1; }
        for (int sg = -1; sg <= 1; sg += 2) {
            V3 q = { t.x - n[0] * h + sg * w[0] / wl * h * 0.5, t.y - n[1] * h + sg * w[1] / wl * h * 0.5, t.z - n[2] * h + sg * w[2] / wl * h * 0.5 };
            ov_pt(pv, pn, pcap, t, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, q, rgb[0], rgb[1], rgb[2]);
        }
    }
}
static void of_modes_off(void) {                /* bVar6: set_disp_mult_arrows(0) + EraseArrows; bVar7: FlipEnabled(0) + EraseArrows */
    if (of_mode == 2) of_flipen = 0;
    of_mode = 0; overlay_dirty = 1;
}
void srf_of_begin(void) {
    of_active = 1; of_selecting = 1; of_mode = 0; of_idx = -1; of_flipen = 0; of_asked = 0; of_subst = 0; of_open = 0;
    of_n = 0; of_nn = 0; srf_pick_active = 1; of_prompt(); overlay_dirty = 1;
}
/* a surface pick: while selecting = select_ents (toggle, as Set Normals'); in Single flip FUN_1802f1000 (a made surface: flipped; another
   one: its selected bit off); otherwise nothing */
int srf_of_pick(int e) {
    if (!of_active || !surf_ent_ok(e)) return 0;
    if (of_selecting) {
        for (int i = 0; i < of_n; i++) if (of_sel[i] == e) {
            memmove(of_sel + i, of_sel + i + 1, sizeof(int) * (size_t)(of_n - i - 1)); of_n--;
            if (ent_sel && e < ent_sel_n && ent_sel[e]) { ent_sel[e] = 0; nselected--; }
            of_prompt(); overlay_dirty = 1; return 1;
        }
        if (of_n == of_cap) { of_cap = of_cap ? 2 * of_cap : 16; of_sel = realloc(of_sel, sizeof(int) * (size_t)of_cap); of_hid = realloc(of_hid, sizeof(int) * (size_t)of_cap); }
        of_hid[of_n] = 0; of_sel[of_n++] = e;
        if (ent_sel && e < ent_sel_n && !ent_sel[e]) { ent_sel[e] = 1; nselected++; }
        of_prompt(); overlay_dirty = 1; return 1;
    }
    if (of_mode != 1) return 1;
    for (int i = 0; i < of_nn; i++) if (of_new[i] == e) { of_flip(i); of_prompt(); return 1; }   /* FUN_1802f0120 / f0480 / efd50 */
    if (ent_sel && e < ent_sel_n && ent_sel[e]) { ent_sel[e] = 0; nselected--; }               /* +4 & ~4, write_ent_sel */
    overlay_dirty = 1; return 1;
}
/* End Selection: FUN_1802f21b0 true -> FUN_1802aac90 (the panel's IsMove / distance) + the build; 0 = nothing selected (the command ends) */
int srf_of_enter(int move, double dist) {
    if (!of_active || !of_selecting) return 1;
    if (!of_n) return 0;
    of_selecting = 0;
    of_build(move, dist); of_prompt(); return 1;
}
/* the panel's buttons; 0 = the command ends */
int srf_of_button(int code) {
    if (!of_active) return 0;
    if (code == 0 || code == 1 || code == 2) {
        of_modes_off(); of_commit();
        if (code != 1) return 0;
        of_unhighlight(); of_selecting = 1; of_prompt(); return 1;   /* set_clr_sel_bits, EnableApply(0), select_ents again */
    }
    if (of_selecting) return 1;
    if (code == 4) {                                              /* FUN_1802f1190 + FUN_1802f1350 (the Max texts emptied) */
        of_unhighlight(); of_terminate(); of_n = 0; of_nn = 0; of_idx = -1;
        of_modes_off(); of_selecting = 1; of_prompt(); return 1;
    }
    if (code == 7) {                                              /* Single flip: FUN_1802f0fa0 */
        if (of_mode == 2) of_modes_off();
        if (of_n) of_mode = 1;
        of_prompt(); overlay_dirty = 1; return 1;
    }
    if (code == 8) {                                              /* Cycle / Next: FUN_1802f0f50 */
        if (of_mode == 1) of_mode = 0;
        if (of_n) { of_idx++; if (of_idx >= of_n) of_idx = 0; of_mode = 2; of_flipen = 1; }
        of_prompt(); overlay_dirty = 1; return 1;
    }
    if (code == 10) {                                             /* Flip: FUN_1802f0c10 */
        if (of_idx != -1 && of_n) of_flip(of_idx);
        return 1;
    }
    return 1;                                                     /* 9 Calculate: NOT PORTED (greyed) */
}
/* the panel's values: IsMove (code 12, FUN_1802f0b50) and the distance (code 11, FUN_1802f0cd0 when it changed by more than tol) */
void srf_of_values(int move, double dist) {
    if (!of_active || of_selecting || !of_open) return;
    if ((move != 0) != (of_move != 0)) {
        of_modes_off(); of_move = move != 0;
        for (int i = 0; i < of_n; i++) of_hide(i, of_move);
        scene_surfs_update(0); overlay_dirty = 1;
    }
    if (fabs(of_dist - dist) > mc_systol.tol) { of_modes_off(); of_set_dist(dist); }
}
void srf_of_end(void) {                         /* FUN_1802f0410: commit (Terminate with nothing made), the selection cleared */
    if (!of_active) return;
    of_commit(); of_unhighlight();
    of_active = 0; of_selecting = 0; of_mode = 0; of_flipen = 0; srf_pick_active = 0; overlay_dirty = 1;
}
void srf_of_dump(void) {                        /* tests */
    printf("      srf offset sel %d made %d selecting %d move %d dist %.6g mode %d idx %d flip %d prompt '%s'\n", of_n, of_nn, of_selecting, of_move, of_dist, of_mode, of_idx, of_flipen, srf_of_prompt());
    for (int i = 0; i < of_nn; i++) {
        const Surf *c = of_surf(of_new[i]); if (!c) continue;
        const Surf *o = c; if (c->kind == 10) o = mcs_surf(&F, of_rd64(c->rec + 0x100));
        double p[3], a[3]; int ok = of_arrow(i, p, a);
        printf("      srf offset made %d ent %d kind %d dist %.9g refs %d sel %04x", i, of_new[i], c->kind, o && o->kind == 8 ? rdd8(o->rec + 0x118) : 0.0, r16s(c->rec + 0x28), selw(c));
        if (ok) printf(" arrow (%.6f %.6f %.6f) dir (%.6f %.6f %.6f)", p[0], p[1], p[2], a[0], a[1], a[2]);
        printf("\n");
    }
    /* every surface record's references, colour and state; an offset surface's stored normal, distance and base */
    for (int s = 0; s < F.nsurfs; s++) {
        const Surf *sf = &F.surfs[s]; if (sf->n < 0x70) continue;
        int e = sf->ent;
        printf("      srf offset rec %d ent %d kind %d refs %d col %d%s%s", s, e, sf->kind, r16s(sf->rec + 0x28), e >= 0 && e < F.nents ? F.ents[e].color : -1,
               e >= 0 && e < F.nents && !F.ents[e].type ? " deleted" : "", sf->blanked ? " blanked" : "");
        if (sf->kind == 8 && sf->n >= 0x128) {
            const Surf *b = mcs_surf(&F, of_rd64(sf->rec + 0x120));
            printf(" n 0x%x N (%.9f %.9f %.9f) dist %.9g base %d range %.6g %.6g %.6g %.6g", sf->n, rdd8(sf->rec + 0x100), rdd8(sf->rec + 0x108), rdd8(sf->rec + 0x110),
                   rdd8(sf->rec + 0x118), b ? (int)(b - F.surfs) : -1, rdd8(sf->rec + 0x70), rdd8(sf->rec + 0x78), rdd8(sf->rec + 0x80), rdd8(sf->rec + 0x88));
        }
        if (sf->kind == 10 && sf->n >= 0x108) { const Surf *b = mcs_surf(&F, of_rd64(sf->rec + 0x100)); printf(" base %d", b ? (int)(b - F.surfs) : -1); }
        printf("\n");
    }
}
