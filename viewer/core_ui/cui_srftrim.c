/* cui_srftrim.c - Surfaces > Modify: Trim to Surfaces / Curves / Plane, Extend, Extend Trimmed Edges (formats/surfaces_tab_plan.md "E.").
   Ported: Extend (SurfaceExtend) on NURBS surfaces, as the function-panel command G_SEXT of cui_geo.c; its kernel is viewer/mcsrfext.c.
   Ported 2026-11-17: Trim to Surfaces (trim_surface_1, G_STRIMS below; kernel viewer/mcsrftrim.c) and Trim to Plane's build (srf_trimp_exec, its
   button greyed: the command around it - SurfaceTrimToPlaneApp's selection, panel and codes - is not ported); Extend's To plane through the
   Plane Selection port (cui_plsel.c, integration int-s1).
   Decoded, NOT PORTED (greyed through cui_surf.c's SRF_FTS / srf_find_ft): trim_surface_2 / 3, ExtendTrimmedEdge; Extend on trimmed
   surfaces (extend_trimmed_surf) and on the other surface kinds.

   SurfaceExtend (mccad 0x180339600) -> SurfaceExtendApp FUN_180337330 (decomp/surftrim/apps.c), panel ISurfaceExtendManager
   (surfaceextendmanagerview.baml; view model mccad.interop SurfaceExtendManagerViewModel: ExtendByDistance = manager vt +0x38 -> +0x30,
   IsLinear vt +0x40 -> +0x40, Distance -> +0x50, KeepOriginalSurface vt +0x50 -> +0x60; FUN_1802daa10 reads them into the app's struct
   {+0 To plane = !ExtendByDistance, +8 Distance, +0x10 Keep, +0x11 IsLinear}). Defaults (FUN_1802dad50, enable_dialog_modality = 1: the
   session statics DAT_18064f460..: .data image By distance, Distance RegisterVariable(&DAT_18064f468, 20.0 metric, 1.0 inch), Keep off,
   Non-linear); FUN_1802dade0 stores them back at the end.
   The loop: prompt mccadres 7454 (0x1d1e) "Select a surface to extend", get_event mask 0x100 (0x1000000a); a surface (event 2): the last
   result committed (UndoTransCommit, or Terminate when nothing was made), prompt 7455 (0x1d1f) "Slide arrow to edge to extend from",
   dyno_surf(mode 3, AutoCursor 1) from the pick's (u, v); FUN_180338540: the edge; kinds 1 (orders > 4) / 2 / 4 / 6 / 7 (and a trimmed
   surface on them): message_box_yes_no 7456 "Surface extension will be NURBS approximation.  Continue?" (convert_entity_to_nurbs: NOT
   PORTED here - refused). Then (By distance with Distance >= tol, or To plane after get_plane_info 7457): UndoTransStart, the selection
   cleared, extend_surf / extend_trimmed_surf (mccore 0x18032fb30 / 0x18032fff0) with colour mode 0 (Keep: main colour / level), 1 (not
   Keep and the surface is used by operations: tp_substitute_ent after tp_blank_surf_yes_no) or 2 (not Keep: the original's colour /
   level); a result: the original deleted unless Keep (delete_ent), repaint, Apply enabled. Panel codes 0 / 1 / 2 (OK / Apply / Cancel):
   UndoTransCommit (Terminate with nothing made) - 0 / 2 end; 4 / 5 (Style) and 7 (Keep) with a changed value and 6 / 8 (the mode / the
   distance): UndoTransTerminate and the build again on the same surface and edge. Esc: the last result committed. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "../mcsurf.h"
#include "../mcsurf_build.h"
#include "../mcsrfcrv.h"
#include "../mcsrfext.h"

static int ext_dir, ext_end;                     /* FUN_180338540's local_ab98 / local_ab9c: the picked edge */
/* To plane (FUN_180337330, decomp/surftrim/apps.c l.22116-22202 / 22440-22530): the plane is the app's local PLANE_sf local_aaa8 (origin
   0, normal (0, 0, 1) = uStack_aa80 1.0, X 0 at the start), passed to extend_surf as (&origin, &normal); local_aba5 = get_plane_info's ok,
   0 at the start. A build in To plane mode asks the plane (get_plane_info(7457 "Select plane to extend to"), the panel disabled around
   it) only while local_aba5 is 0 - a cancelled dialog builds nothing; the mode / distance codes 6 / 8 in To plane mode always ask it
   again and build when it is ok and an edge is known (local_aba6). get_plane_info = the Plane Selection port (cui_plsel.c psel_begin):
   the build runs from its done callback (geo_sext_plane_done in cui_geo.c). */
static PlaneSf ext_pl; static int ext_pl_ok, ext_pl_e = -1;
void srf_ext_reset(void) { memset(&ext_pl, 0, sizeof ext_pl); ext_pl.n[2] = 1.0; ext_pl_ok = 0; ext_pl_e = -1; }
void srf_ext_plane_forget(void) { ext_pl_ok = 0; }   /* codes 6 / 8 in To plane mode: get_plane_info again */
static void ext_pl_done(int ok, PlaneSf *pl, void *ctx) {
    (void)pl; (void)ctx; ext_pl_ok = ok;
    if (ok && ext_pl_e >= 0) geo_sext_plane_done(ext_pl_e);
}

/* the kinds this port extends: extend_surf's case 0 (NURBS); the others are NOT PORTED */
int srf_ext_supported(int e, char *msg, size_t msgn) {
    if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) return 0;
    const Surf *s = &F.surfs[F.ents[e].srf];
    if (s->kind == 0) return 1;
    if (s->kind == 10) snprintf(msg, msgn, "Extend: trimmed surfaces (extend_trimmed_surf, decoded) are NOT PORTED - Select a surface to extend");
    else snprintf(msg, msgn, "Extend: surface kind %d (%s) is NOT PORTED - Select a surface to extend", s->kind,
                  s->kind == 8 ? "offset: FUN_18032eae0" : s->kind == 1 ? "parametric: FUN_18032efb0 / convert_entity_to_nurbs" : "convert_entity_to_nurbs");
    return 0;
}
/* the surface pick: dyno_surf mode 3 with the AutoCursor (param_4 1) from the pick's (u, v) */
int srf_ext_begin(int e, V3 w, char *msg, size_t msgn) {
    const Surf *s = &F.surfs[F.ents[e].srf]; double P[3] = { w.x, w.y, w.z }, u, v;
    if (!mcsc_uv_of_point(&F, s, P, &u, &v)) { snprintf(msg, msgn, "Extend: the surface has no parameter range"); return 0; }
    ds_start(e, 3, 1, u, v);
    return 1;
}
/* the arrow's (u, v) -> the edge (FUN_180338540, untrimmed branch: the nearest edge in the normalised range) */
void srf_ext_edge(int e, double u, double v) {
    const Surf *s = &F.surfs[F.ents[e].srf]; double uv[2] = { u, v };
    mse_edge_untrimmed(s->rec, uv, &ext_dir, &ext_end);
}
/* the build (FUN_180337330 after the edge; extend_surf's kind 0 branch). keep = KeepOriginalSurface, linear = IsLinear, len = Distance.
   Returns 1 when a surface was made (inside mod_begin / mod_end: the new surface and, unless keep, the original deleted = one step). */
int srf_ext_exec(int e, double len, int to_plane, int linear, int keep, int level, int color, char *msg, size_t msgn) {
    if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF || !F.ents[e].type) return 0;
    const McSysTol *stl = mc_systol_units(mcam_is_metric(&F));
    if (to_plane && !ext_pl_ok) {                                         /* get_plane_info first; the build from its callback */
        ext_pl_e = e; psel_begin("Select plane to extend to", &ext_pl, ext_pl_done, NULL, 0);   /* mccadres 7457 (0x1d21) */
        return -1;
    }
    if (!to_plane && !(stl->tol <= len)) return 0;                       /* "if (tol <= Distance) build; else nothing" */
    Surf *s = &F.surfs[F.ents[e].srf];
    if (s->kind != 0) return 0;
    int16_t ops; memcpy(&ops, s->rec + 0x38, 2);
    if (!keep && ops > 0) { snprintf(msg, msgn, "Extend: the surface is used by an operation (tp_substitute_ent NOT PORTED) - Select a surface to extend"); return 0; }
    double uv8[8]; uint32_t n = 0; int err = 0;
    double plane[6] = { ext_pl.o[0], ext_pl.o[1], ext_pl.o[2], ext_pl.n[0], ext_pl.n[1], ext_pl.n[2] };   /* {point, normal} */
    uint8_t *r = mse_extend_nurbs(s->rec, s->n, ext_dir, ext_end, len, to_plane ? plane : NULL, linear, stl->tol, uv8, &n, &err);
    if (!r) {
        if (err == 1) cui_message("Warning", "Extension to plane failed");   /* post_warning mccadres 7459 */
        else if (err == 4) cui_message("Extend", "The Non-linear extension did not converge after 100000 iterations: no surface was made.");   /* ours, decision S-10 (Mastercam loops forever here) */
        else snprintf(msg, msgn, "Extend: the surface record is not read here");
        return 0;
    }
    /* extend_surf: FUN_18032cd00 / FUN_18032c670 -> message_box_yes_no(mccoreres 7458, caption 857 "Warning"); No: nothing */
    if (mse_selfint(r, n, uv8, ext_dir)) {
        int yes = cui_hooks.ask_yes_no ? cui_hooks.ask_yes_no("Warning", "Surface extension has self-intersection - Keep surface anyway?") : 1;
        if (!yes) { free(r); return 0; }
    }
    uint32_t cls = 0x100; memcpy(r, &cls, 4);                             /* *param_2 = 0x100 */
    r[0x6e] = (uint8_t)((r[0x6e] & ~4) | (s->rec[0x6e] & 4));            /* the normal bit of the original */
    if (mcsb_finish(&F, r, n, solid_mode != 0, stl->tol)) { free(r); snprintf(msg, msgn, "Extend: a parameter range below 1e-12"); return 0; }
    /* the colour mode: 0 (Keep) main colour / level, else the original's; put_surf_in_db: the main colour -> surfaceFrontColor */
    int col = keep ? color : s->color, lv = keep ? level : s->level;
    if (col == geo_color && col != cfg_back_color) col = srf_front_color;
    mod_begin();
    mcam_new_surf(&F, r, n, lv, col); free(r);
    if (!keep) { dirty_ent_users(e); mod_del(e); }                       /* delete_ent(original, 1, has ops, ..) */
    mod_end();
    scene_surfs_update(1);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ Trim to Surfaces (trim_surface_1)
   SurfaceTrimToSurfacesApp FUN_18031ed90 (decomp/surftrim/apps.c) with the worker TrimSurfacesToSurfaces (trimwork.c, vftable 0x180417500;
   formats/surfaces_trim_decode.md section 2): select_multi_surfs (mccore 0x180550fd0) - set 1 (prompt mccoreres 7439, Enter), set 2 (7440,
   Enter); checks 7449 / 7450 / 7451 (each: the sets cleared, the selection again); the keep points (prompt mccadres 4870; a surface of the set
   whose point is missing - set 1 when the Trim option < 2 and point 1 is missing, else set 2 - then dyno_surf mode 3 with prompt 5431; the
   point = eval_surf at the arrow's (u, v)); the build = trim_surfs_to_surfs (mccore 0x180564220) -> MaTrimSurfToSurf::DoTrim (viewer/mcsrftrim.c
   mi_trim_to_surfs) -> MaTrimSurf::AddNewTrimmedSrfsToDatabase -> MaFace::Make_MC_Ent 0x180416580 per face (ts_store below).
   Panel (surfacetrimtosurfacesmanagerview.baml): Trim Both / First set / Second set (gp.method 0 / 1 / 2), Extend curves to edge (gp.lines),
   Split model (gp.style), Keep multiple regions (gp.round), Use current attributes (gp.mode), Keep original surface (gp.del).
   NOT PORTED / NOT DECODED here: the Reselect buttons (FUN_18008fa90 / FUN_18008f9e0: whether the keep points are asked again is not read),
   the status-bar 0x8000 codes, the toolpath substitution (tp_substitute_ent: a surface used by an operation is refused when it would be
   replaced), surfaces other than NURBS (kind 0) and trimmed-over-NURBS (kind 10), a base whose knots MaNURBSurf's DB constructor regenerates. */
#include "../mcsrftrim.h"
static int *ts_set[2], ts_n[2], ts_phase, ts_have[2], ts_pick_set = -1;
static double ts_P[2][3];
void srf_trims_begin(void) { for (int k = 0; k < 2; k++) { free(ts_set[k]); ts_set[k] = NULL; ts_n[k] = 0; ts_have[k] = 0; } ts_phase = 0; ts_pick_set = -1; }
int srf_trims_phase(void) { return ts_phase; }
/* FUN_18008eda0: the set whose keep point is needed (Both: both, First set: 1, Second set: 2); -1 = all taken */
static int ts_need(int option) { if (option < 2 && !ts_have[0]) return 0; if ((option & ~2) == 0 && !ts_have[1]) return 1; return -1; }
int srf_trims_ready(int option) { return ts_phase >= 2 && ts_need(option) < 0; }
const char *srf_trims_prompt(int option) {
    if (ts_phase == 0) return "Select first set of surfaces and press <Enter> to continue";    /* mccoreres 7439 */
    if (ts_phase == 1) return "Select second set of surfaces and press <Enter> to continue";   /* mccoreres 7440 */
    if (ds_running()) return "Slide to a location to keep after trimming";                      /* mccadres 5431 */
    if (ts_need(option) >= 0) return "Indicate area to keep - select a surface to be trimmed";  /* mccadres 4870 */
    return "";
}
/* Enter while a set is selected (select_ents' end); 1 = the keep picks start */
int srf_trims_enter(char *msg, size_t msgn) {
    if (ts_phase > 1) return 0;
    int *l = NULL, n = sel_list_surfs(&l);
    free(ts_set[ts_phase]); ts_set[ts_phase] = l; ts_n[ts_phase] = n; sel_clear();
    if (ts_phase == 0) { ts_phase = 1; snprintf(msg, msgn, "%s", srf_trims_prompt(0)); return 0; }
    int n1 = ts_n[0], n2 = ts_n[1], flag = (n1 == 0 && n2 >= 2) || (n2 == 0 && n1 >= 2);   /* select_multi_surfs' flag */
    const char *w = NULL;
    if (flag) w = "Cannot trim - only one set of surfaces selected";                           /* mccadres 7449 */
    else if (n1 + n2 < 2) w = "At least two surfaces must be selected";                       /* mccadres 7450 */
    else if (n1 >= 2 && n2 >= 2) w = "Cannot trim - one set of surfaces must have only one surface";   /* mccadres 7451 */
    if (w) { cui_message("Warning", "%s", w); srf_trims_begin(); snprintf(msg, msgn, "%s", srf_trims_prompt(0)); return 0; }
    ts_phase = 2; ts_have[0] = ts_have[1] = 0; snprintf(msg, msgn, "%s", srf_trims_prompt(0)); return 1;
}
/* a surface picked for a keep point (slot [5] FUN_18008eda0): it must be in the set whose point is missing; dyno_surf mode 3 from the pick */
int srf_trims_click(int e, V3 w, int option, char *msg, size_t msgn) {
    int k = ts_need(option); if (ts_phase < 2 || k < 0 || e < 0 || e >= F.nents || F.ents[e].type != E_SURF) return 0;
    int in = 0; for (int i = 0; i < ts_n[k]; i++) if (ts_set[k][i] == e) in = 1;
    if (!in) return 0;
    const Surf *s = &F.surfs[F.ents[e].srf]; double P[3] = { w.x, w.y, w.z }, u, v;
    if (!mcsc_uv_of_point(&F, s, P, &u, &v)) { snprintf(msg, msgn, "Trim: the surface has no parameter range"); return 0; }
    ts_pick_set = k; ds_start(e, 3, 1, u, v); return 1;
}
/* the arrow's end: the keep point = eval_surf(picked surface, u, v) */
void srf_trims_keep(int e, double u, double v) {
    if (ts_pick_set < 0 || e < 0 || e >= F.nents) return;
    double Su[3], Sv[3], N[3]; if (mcsc_surf_frame(&F, &F.surfs[F.ents[e].srf], u, v, ts_P[ts_pick_set], Su, Sv, N)) ts_have[ts_pick_set] = 1;
    ts_pick_set = -1;
}
/* OK + New (code 1): the selection again */
void srf_trims_restart(void) { srf_trims_begin(); }

typedef struct { int e, be0; uint8_t *brec; uint32_t bn; ISrf s; int nl, *np; double (**lp)[2]; double k0[2], ks[2], H[4]; int rep; } TsSrf;
static void ts_free(TsSrf *t) { free(t->brec); free(t->s.KU); free(t->s.KV); free(t->s.V); srf_mi_free(&t->s); if (t->lp) for (int l = 0; l < t->nl; l++) free(t->lp[l]); free(t->lp); free(t->np); }
static double rdd8(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static int32_t rd32_(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
/* MaSurface::LoadSurf(eptr, true, 30.0) as the oracle builds it (tools/tests/srftrim_test.c): the NURBS with knots normalised, the stored loops
   normalised the same way; 0 + msg = refused */
static int ts_load(int e, TsSrf *t, char *msg, size_t msgn) {
    memset(t, 0, sizeof *t); t->e = e;
    const Surf *s = &F.surfs[F.ents[e].srf], *b = s; int rep = 0, nl = 0, *np = NULL; double (**pts)[2] = NULL;
    if (s->kind == 10) { if (!mcsc_face_data(&F, s, &b, &rep, &nl, &np, &pts)) { snprintf(msg, msgn, "Trim: the trimmed surface's base is not a NURBS surface (NOT PORTED)"); return 0; } }
    else if (s->kind != 0) { snprintf(msg, msgn, "Trim: surface kind %d (MaSurface::LoadSurf of it) is NOT PORTED", s->kind); return 0; }
    else rep = mcsc_surf_reparam(b);
    const uint8_t *r = b->rec;
    int nku = rd32_(r + 0x100), nkv = rd32_(r + 0x104), ncu = rd32_(r + 0x108), ncv = rd32_(r + 0x10c), vpp = r[0x110], du = r[0x111], dv = r[0x112], rat = !(r[0x113] & 1);
    if (b->n < 0x118 || nku != ncu + du + 1 || nkv != ncv + dv + 1 || (vpp != 3 && vpp != 4) || 0x118 + 8 * ((size_t)nku + nkv + (size_t)vpp * ncu * ncv) > b->n) { snprintf(msg, msgn, "Trim: the NURBS record is not read here"); goto refuse; }
    t->be0 = -1; for (int i = 0; i < F.nents; i++) if (F.ents[i].type == E_SURF && &F.surfs[F.ents[i].srf] == b) { t->be0 = i; break; }
    if (t->be0 < 0) { snprintf(msg, msgn, "Trim: the base surface is not in the part"); goto refuse; }
    t->bn = b->n; t->brec = malloc(b->n); memcpy(t->brec, b->rec, b->n); ISrf *q = &t->s; q->ncu = ncu; q->ncv = ncv; q->ku = du + 1; q->kv = dv + 1; q->rat = rat; q->metric = mcam_is_metric(&F);
    q->KU = malloc(sizeof(double) * nku); q->KV = malloc(sizeof(double) * nkv); q->V = malloc(sizeof(double) * 4 * ncu * ncv);
    for (int i = 0; i < nku; i++) q->KU[i] = rdd8(r + 0x118 + 8 * i);
    for (int i = 0; i < nkv; i++) q->KV[i] = rdd8(r + 0x118 + 8 * (nku + i));
    const uint8_t *CP = r + 0x118 + 8 * ((size_t)nku + nkv);
    for (int i = 0; i < ncu * ncv; i++) { const uint8_t *c = CP + 8 * (size_t)vpp * i; double *o = q->V + 4 * i;
        if (vpp == 4) { o[3] = rat ? rdd8(c) : 1.0; for (int k = 0; k < 3; k++) o[k] = rdd8(c + 8 * (1 + k)); } else { o[3] = 1; for (int k = 0; k < 3; k++) o[k] = rdd8(c + 8 * k); } }
    double k0u = q->KU[0], su = q->KU[nku - 1] - k0u, k0v = q->KV[0], sv = q->KV[nkv - 1] - k0v;
    /* MaNURBSurf's DB constructor (0x180455dd0) as LoadSurf(.., true, ..) runs it: the knots and range normalised, this[0x1c] / [0x1d]: the knots
       regenerated (RegenNonUniformU/VKnotsFromDerivRatios, unclamped vectors included) - mcsrfint.c mi_db_knots; the loop points converted
       (ConvertUVParams) through the old / new unique knots */
    mcs_surf_domain(b, t->H);
    MiRegen RG; memset(&RG, 0, sizeof RG); rep = mi_db_knots(q, t->H, 1, &RG) & 3; t->rep = rep;
    q->rev = (s->rec[0x6e] & 4) != 0;
    t->k0[0] = k0u; t->k0[1] = k0v; t->ks[0] = su; t->ks[1] = sv;
    t->nl = nl; t->np = np; t->lp = pts;
    /* the loop points as MaFace's DB constructor 0x180413ba0 maps them: MaPolyLine::UVTransform from the record's range to the surface's (before any
       regeneration) when they differ, then ConvertUVParams through the old / new unique knots */
    int xf = t->H[0] != RG.d0[0] || t->H[1] != RG.d0[1] || t->H[2] != RG.d0[2] || t->H[3] != RG.d0[3];
    for (int l = 0; l < nl; l++) for (int i = 0; i < np[l]; i++) { double a = pts[l][i][0], c = pts[l][i][1];
        if (xf) mi_uv_transform(&a, &c, t->H, RG.d0);
        if (rep) { a = mi_regen_map(&RG, 0, a); c = mi_regen_map(&RG, 1, c); } pts[l][i][0] = a; pts[l][i][1] = c; }
    mi_regen_free(&RG);
    return 1;
refuse:
    if (pts) for (int l = 0; l < nl; l++) free(pts[l]);
    free(pts); free(np); return 0;
}
/* MaFace::Make_MC_Ent 0x180416580 + store_new_trim_surf_ext 0x1808ac5e0 for one face of surface t:
   - a new base (the surface subsetted - a trimmed surface after SubsetToFace -, or Keep original): MaNURBSurf::Make_nbs_type 0x18045e7f0 =
     create_nurb_surf_new with the normalised knots and the surface's current (u, v) range, the original's attributes, stored blanked
     (delete_ent(.., 1, 1)); the loops kept in that normalised range (LimitUVs only: SurfType 1 with a new base: no UVInverseTransform);
   - else the original NURBS is the base: the loops LimitUVs, then UVInverseTransform 0x180475180 from the normalised range back to the
     record's range when they differ (IsUVRangeScaled); store mode 2 -> 1: the base blanked;
   - the record: the base's surf struct as kind 10, range 1/12 .. 11/12, the outer polyline then the inner ones, {start, count - 2}
     (= mcsb_flat_trim's record); a kind 10 original is deleted (store mode 2) unless Keep original.
   The colour / level: Use current attributes: the main ones, else the original's. */
static int ts_store(TsSrf *t, const MiTrimFace *fc, int keep, int usecur, int level, int color, int *base_blanked) {
    const McSysTol *stl = mc_systol_units(mcam_is_metric(&F)); int shaded = solid_mode != 0;
    const Surf *orig = &F.surfs[F.ents[t->e].srf]; int col = usecur ? color : orig->color, lv = usecur ? level : orig->level, okind = orig->kind, ocol = orig->color, olv = orig->level;
    if (col == geo_color && col != cfg_back_color) col = srf_front_color;
    int newbase = okind == 10 || keep || t->rep;   /* IsSubseted (SubsetToFace of a trimmed surface), the regenerated knots (MaSurface +0x1d) or Keep original */
    int be = -1; uint32_t bn = 0; uint8_t *brec = NULL;
    if (newbase) {
        bn = t->bn; brec = malloc(bn); memcpy(brec, t->brec, bn);
        int nku = rd32_(brec + 0x100), nkv = rd32_(brec + 0x104);
        for (int i = 0; i < nku; i++) { double k = t->s.KU[i]; memcpy(brec + 0x118 + 8 * i, &k, 8); }
        for (int i = 0; i < nkv; i++) { double k = t->s.KV[i]; memcpy(brec + 0x118 + 8 * (nku + i), &k, 8); }
        for (int k = 0; k < 4; k++) memcpy(brec + 0x70 + 8 * k, &t->s.d[k], 8);
        if (mcsb_finish(&F, brec, bn, shaded, stl->tol)) { free(brec); return 0; }
        be = mcam_new_surf(&F, brec, bn, olv, ocol);
        Surf *bs = &F.surfs[F.ents[be].srf]; bs->blanked = 1; bs->rec[5] |= 1; F.ents[be].blanked = 1;   /* delete_ent(new base, 1, 1): stored blanked */
    } else {
        be = t->be0; bn = t->bn; brec = malloc(bn); memcpy(brec, t->brec, bn);
    }
    int nl = fc->nl; double **P = calloc(nl, sizeof *P); int *cnt = calloc(nl, sizeof *cnt);
    int scaled = !newbase && (t->s.d[0] != t->H[0] || t->s.d[1] != t->H[1] || t->s.d[2] != t->H[2] || t->s.d[3] != t->H[3]);
    for (int l = 0; l < nl; l++) {
        int n = fc->l[l].n; P[l] = malloc(sizeof(double) * 2 * n); cnt[l] = n;
        for (int i = 0; i < n; i++) {
            double u = fc->l[l].p[i][0], v = fc->l[l].p[i][1];
            if (t->s.d[1] < u || u < t->s.d[0]) u = t->s.d[1] < u ? t->s.d[1] : t->s.d[0];   /* MaSurface::LimitUVToRange 0x180487990 */
            if (t->s.d[3] < v || v < t->s.d[2]) v = t->s.d[3] < v ? t->s.d[3] : t->s.d[2];
            if (scaled) {   /* UVInverseTransform(d0, d1, d2, d3, H0, H1, H2, H3): the translation of the range start, then the scale about it */
                if (t->s.d[0] != t->H[0] || t->s.d[2] != t->H[2]) { u = u + (t->H[0] - t->s.d[0]); v = v + (t->H[2] - t->s.d[2]); }
                double sx = (t->H[1] - t->H[0]) / (t->s.d[1] - t->s.d[0]), sy = (t->H[3] - t->H[2]) / (t->s.d[3] - t->s.d[2]);
                u = sx * u + (t->H[0] - sx * t->H[0]); v = sy * v + (t->H[2] - sy * t->H[2]);
            }
            P[l][2 * i] = u; P[l][2 * i + 1] = v;
        }
    }
    uint32_t tn = 0; uint8_t *tr = mcsb_flat_trim(&F, F.ents[be].septr, brec, bn, nl, P, cnt, shaded, stl->tol, &tn);
    for (int l = 0; l < nl; l++) free(P[l]); free(P); free(cnt); free(brec);
    if (!tr) return 0;
    mcam_new_surf(&F, tr, tn, lv, col); free(tr);
    if (!newbase && !keep && !*base_blanked) { mod_blank(be); *base_blanked = 1; }   /* store mode 1: delete_ent(base, 0, 1) = the base blanked */
    return 1;
}
/* the build (slot [2] FUN_1800903e0 -> trim_surfs_to_surfs): 1 = made */
int srf_trims_exec(int option, int ext, int split, int keepm, int usecur, int keep, int level, int color, char *msg, size_t msgn) {
    if (!srf_trims_ready(option)) return 0;
    const McSysTol *stl = mc_systol_units(mcam_is_metric(&F));
    int *A = ts_set[0], nA = ts_n[0], Bi = ts_n[1] ? ts_set[1][0] : -1, opt = option; double *pA = ts_P[0], *pB = ts_P[1];
    if (ts_n[0] == 1 && ts_n[1] > 1) { A = ts_set[1]; nA = ts_n[1]; Bi = ts_set[0][0]; pA = ts_P[1]; pB = ts_P[0]; opt = opt == 1 ? 2 : opt == 2 ? 1 : opt; }   /* the sets swap */
    if (Bi < 0) return 0;
    int trimA = opt < 2, trimB = (opt & ~2) == 0;
    TsSrf *TA = calloc(nA, sizeof *TA), TB; int na = 0, ok = 1;
    if (!ts_load(Bi, &TB, msg, msgn)) { free(TA); return 0; }
    for (int i = 0; i < nA && ok; i++) { if (A[i] == Bi) continue; if (!ts_load(A[i], &TA[na], msg, msgn)) ok = 0; else na++; }
    if (ok && !keep) {   /* tp_substitute_ent (the toolpaths of a replaced surface) is NOT PORTED: a surface used by an operation is refused */
        for (int i = -1; i < na && ok; i++) { int e = i < 0 ? Bi : TA[i].e; if ((i < 0 ? !trimB : !trimA)) continue; int16_t ops; memcpy(&ops, F.surfs[F.ents[e].srf].rec + 0x38, 2);
            if (ops > 0) { snprintf(msg, msgn, "Trim: a surface is used by an operation (tp_substitute_ent NOT PORTED)"); ok = 0; } }
    }
    int made = 0;
    if (ok) {
        MiTrimIn *in = calloc(na ? na : 1, sizeof *in), inB = { &TB.s, TB.nl, TB.np, (double (*const *)[2])TB.lp };
        for (int i = 0; i < na; i++) { in[i].s = &TA[i].s; in[i].nl = TA[i].nl; in[i].np = TA[i].np; in[i].lp = (double (*const *)[2])TA[i].lp; }
        MiTrimRes *RA = calloc(na ? na : 1, sizeof *RA), RB; memset(&RB, 0, sizeof RB);
        mi_trim_to_surfs(in, na, pA, &inB, pB, trimA, trimB, split, keepm, ext, stl->surf_step_chord_dev, RA, &RB);
        const char *ref = RB.refused; for (int i = 0; i < na && !ref; i++) ref = RA[i].refused;
        if (ref) snprintf(msg, msgn, "Trim: %s", ref);
        else {
            mod_begin();
            for (int i = -1; i < na; i++) {
                MiTrimRes *R = i < 0 ? &RB : &RA[i]; TsSrf *t = i < 0 ? &TB : &TA[i]; int bb = 0, m0 = made;
                for (int k = 0; k < R->nfaces; k++) made += ts_store(t, &R->faces[k], keep, usecur, level, color, &bb);
                if (made > m0 && !keep && F.surfs[F.ents[t->e].srf].kind == 10) { dirty_ent_users(t->e); mod_del(t->e); }   /* store mode 2 on a kind 10: the replaced trimmed surface deleted */
            }
            mod_end();
            if (!made) cui_message("Warning", "Cannot Trim Surface, Selected curves do not form a closed boundary");   /* mccoreres 5494 */
            scene_surfs_update(1);
        }
        for (int i = 0; i < na; i++) mi_trim_res_free(&RA[i]); mi_trim_res_free(&RB); free(RA); free(in);
    }
    for (int i = 0; i < na; i++) ts_free(&TA[i]); ts_free(&TB); free(TA);
    return made > 0;
}

/* ------------------------------------------------------------------------------------------------ Trim to Plane (trim_surface_3)
   SurfaceTrimToPlaneApp FUN_1803287c0: the surfaces (select_ents, mccadres 7494 / 7493), the plane (get_plane_info = the Plane Selection dialog,
   mccore 0x180a2aea0: ported in cui_plsel.c), the build at once (no keep pick). The ribbon button stays greyed: the command around the
   build (FUN_1803287c0's selection, its panel ISurfaceTrimToPlaneManager and codes 0..10, Reselect, erase_plane) is NOT PORTED yet. The
   kernel: trim_surfs_to_a_plane 0x18056ed00 -> MaTrimSurfToPlane::DoTrim per surface (viewer/mcsrftrim.c mi_trim_to_plane), keep point = the
   plane origin + its unit normal (the kept side = the normal's side), then AddNewTrimmedSrfsToDatabase (Make_MC_Ent per face: ts_store) and
   RemoveDeletedSrfsFromDatabase 0x1804c7310 (Delete untrimmed surfaces: the untouched surfaces on the negative side deleted). No new surface:
   post_warning mccoreres 5513. Reachable for the tests through the script step trimplane (cui_geo.c). */
int srf_trimp_exec(const int *ents, int n, const double org[3], const double nrm[3], int delun, int split, int keepm, int usecur, int keep,
                   int level, int color, char *msg, size_t msgn) {
    const McSysTol *stl = mc_systol_units(mcam_is_metric(&F));
    double l = sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]); if (!(l > 0)) return 0;
    double nu[3] = { nrm[0] / l, nrm[1] / l, nrm[2] / l }, pl[4] = { nu[0], nu[1], nu[2], org[0] * nu[0] + org[1] * nu[1] + org[2] * nu[2] }, kp[3] = { org[0] + nu[0], org[1] + nu[1], org[2] + nu[2] };
    TsSrf *T = calloc(n ? n : 1, sizeof *T); MiTrimRes *R = calloc(n ? n : 1, sizeof *R); int nt = 0, ok = 1;
    for (int i = 0; i < n && ok; i++) { if (!ts_load(ents[i], &T[nt], msg, msgn)) ok = 0; else nt++; }
    for (int i = 0; i < nt && ok; i++) {
        mi_trim_to_plane(&T[i].s, T[i].nl, T[i].np, (double (*const *)[2])T[i].lp, stl->surf_step_chord_dev, stl->tol, pl, kp, delun, split, keepm, 1, 1, &R[i]);
        if (R[i].refused) { snprintf(msg, msgn, "Trim: %s", R[i].refused); ok = 0; }
        else if (!keep && (R[i].nfaces || R[i].deleted)) { int16_t ops; memcpy(&ops, F.surfs[F.ents[T[i].e].srf].rec + 0x38, 2); if (ops > 0) { snprintf(msg, msgn, "Trim: a surface is used by an operation (tp_substitute_ent NOT PORTED)"); ok = 0; } }
    }
    int made = 0;
    if (ok) {
        mod_begin();
        for (int i = 0; i < nt; i++) {
            int bb = 0, m0 = made;
            for (int k = 0; k < R[i].nfaces; k++) made += ts_store(&T[i], &R[i].faces[k], keep, usecur, level, color, &bb);
            if (made > m0 && !keep && F.surfs[F.ents[T[i].e].srf].kind == 10) { dirty_ent_users(T[i].e); mod_del(T[i].e); }
            if (R[i].deleted) { dirty_ent_users(T[i].e); mod_del(T[i].e); }   /* RemoveDeletedSrfsFromDatabase: delete_ent */
        }
        mod_end();
        if (!made) cui_message("Warning", "Cannot Trim Surface, Plane does not intersect selected surface(s)");   /* mccoreres 5513 */
        scene_surfs_update(1);
    }
    for (int i = 0; i < nt; i++) { mi_trim_res_free(&R[i]); ts_free(&T[i]); }
    free(T); free(R);
    return made > 0;
}
