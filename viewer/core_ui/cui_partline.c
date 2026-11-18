/* cui_partline.c - Wireframe > Curve Parting Line (sketcher_curve_7, FUN_1802be7b0 mccad). Spec: ../../formats/wireframe.md "Curve Parting Line".
 * Panel: wireframecurvepartinglinemanagerview.baml (Curve Quality: Chord height / Distance, Angle); manager WireframeCurvePartingLineManager (ctor FUN_180246410),
 * values FUN_1802473f0, session values WireframeCurvePartingLineApp::SaveRestoreLastParams (FUN_1802476c0 restore, FUN_180247770 save, the statics
 * DAT_1806a53d0..e8 set by the CRT initializer 0x180003950); loop FUN_1802be7b0; kernel mccore parting_line (0x180566740) -> viewer/mcpartline*.c.
 * Not ported, refused with a message: trimmed and non-NURBS surfaces (make_surf_polygons' trim / other kinds, FUN_180564b70's loop branch), solid faces
 * (sld_face_to_temp_file / read_next_solid_surf_from_temp_file). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "../mcsurf.h"
#include "../mcpartline.h"

int sp_nurbs_cfg(void);   /* cfg_createNurbs (cui_spline.c) */

/* the session values (FUN_1802476c0: enable_dialog_modality = 1 (wireframe.md "Bounding Box"), so the statics): DAT_1806a53d0 method (bss 0 = Chord height),
   DAT_1806a53d8 chord (CRT init 180003969: surf_step_chord_dev), DAT_1806a53e0 distance (0.1, [0x18047d2c0]), DAT_1806a53e8 angle (0.0, [0x18047d2c8]);
   RegisterVariable(&chord, 0.02, 0.001), (&distance, 2.0, 0.1): converted on a units change (pln_units) */
static int pl_sinit, pl_smetric, pl_smethod;
static double pl_schord, pl_sdist, pl_sang;
static int pl_method;                 /* SubDivisionMethod: 0 Chord height, 1 Distance (FUN_1802473f0: vt +0x38 == 1 -> 1) */
static double pl_chord, pl_dist, pl_ang;
static double pl_l_chord, pl_l_dist, pl_l_ang; static int pl_l_method;
static int pl_built;                  /* bVar5: a build is wanted / live (Apply pressed once) */
static int pl_live;                   /* bVar3: an undo transaction holds the last build */

int pln_cmd(int c) { return c == G_CPART; }
const char *pln_prompt_text(void) { return pl_live ? "Set options, press <ENTER>, or OK" : "Set Cplane, select surfaces, press Apply when done"; }   /* mccadres 5973 (0x1755) / 5972 (0x1754) */
static void pln_msg(void) { snprintf(cam_msg, sizeof cam_msg, "%s", pln_prompt_text()); overlay_dirty = 1; }
static void pln_units(void) {
    int m = mcam_is_metric(&F);
    if (!pl_sinit) {
        pl_sinit = 1; pl_smetric = m; pl_smethod = 0;
        pl_schord = mc_systol.surf_step_chord_dev;   /* DAT_1806a53d8 = surf_step_chord_dev at the DLL's load [inferred: ours the configured value] */
        pl_sdist = m ? 2.0 : 0.1; pl_sang = 0.0;     /* 0.1 inch; RegisterVariable's metric value 2.0 */
        return;
    }
    if (m == pl_smetric) return;
    if (m) { pl_schord *= 0.02 / 0.001; pl_sdist *= 2.0 / 0.1; } else { pl_schord /= 0.02 / 0.001; pl_sdist /= 2.0 / 0.1; }   /* SetUnits -> FUN_1805f9da0: by metric / inch */
    pl_smetric = m;
}
int pln_begin(int c) {
    (void)c; pln_units();
    pl_method = pl_smethod; pl_chord = pl_schord; pl_dist = pl_sdist; pl_ang = pl_sang;   /* FUN_1802476c0 -> FUN_180247870(model, statics, 1) */
    pl_built = 0; pl_live = 0; pln_msg(); return 1;
}
static void pln_save(void) { pln_units(); pl_smethod = pl_method; pl_schord = pl_chord; pl_sdist = pl_dist; pl_sang = pl_ang; }   /* FUN_180247770 at the loop's end: FUN_1802473f0(model, statics) unless vt +0x18 [taken: always] */
void pln_end(void) { if (pl_sinit) pln_save(); pl_built = 0; pl_live = 0; }   /* another command started: the loop ends as at OK */
int pln_need(void) { return 0; }
void pln_take(int e, V3 p) { (void)e; (void)p; }
int pln_click(float mx, float my, const double w[3], int ctrl) { (void)mx; (void)my; (void)w; (void)ctrl; return 0; }
int pln_esc(void) { pln_save(); return 0; }   /* Esc ends the loop (get_event local_566[0]): the values saved as at OK */

/* the Cplane's Z in world (parting_line 180566884: GetActiveViewNo(Planes, 0)'s matrix +0x38 .. +0x48, the third row; prompt 5972 "Set Cplane") */
static void pln_view_z(double V[3]) {
    double z[3] = { 0, 0, 0 }, v[3] = { 0, 0, 1 }, o[3], w0[3];
    int id = plane_active[0] > 0 ? plane_active[0] : 1;
    plane_v2w(id, z, v, o); plane_v2w(id, z, z, w0);
    for (int i = 0; i < 3; i++) V[i] = o[i] - w0[i];
    double l = sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]); if (l > 0) for (int i = 0; i < 3; i++) V[i] /= l;
}

/* one build: FUN_1802be7b0 LAB_1802bef07 with bVar5: msf_nurbs_splines = cfg_createNurbs; Chord height: surf_step_chord_dev = the panel value, surf_use_chord_dev = 1;
   Distance: surf_step_dist = the panel value (parting_line and its callees do not read it [code: the dumps]), surf_use_chord_dev = 0; then parting_line(Angle, list) */
static void pln_build(void) {
    int *ls = NULL, n = sel_list_surfs(&ls);
    for (int i = 0; i < n; i++) {   /* the refusals first: one surface not ported refuses the build (as Curve at Intersection) */
        const Surf *s = &F.surfs[F.ents[ls[i]].srf];
        if (s->kind != 0) { free(ls); snprintf(cam_msg, sizeof cam_msg, "Curve Parting Line: a trimmed or non-NURBS surface (kind %d): make_surf_polygons' trim / kind branch NOT PORTED", s->kind); overlay_dirty = 1; return; }
    }
    /* get_ent(.., 4 (selected), 0x100): the selected surfaces in database order; solid faces (read_next_solid_surf_from_temp_file): not offered here */
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && ls[j - 1] > ls[j]; j--) { int t = ls[j]; ls[j] = ls[j - 1]; ls[j - 1] = t; }
    int chord_mode = pl_method == 0;
    double chord = chord_mode ? pl_chord : mc_systol.surf_step_chord_dev, tol = mc_systol.tol, V[3];
    int metric = mcam_is_metric(&F), nurbs = sp_nurbs_cfg(), listed = 0, made = 0;
    pln_view_z(V);
    McsSimpView *views = calloc((size_t)F.nfviews + 1, sizeof *views);
    for (int i = 0; i < F.nfviews; i++) { views[i].n = F.fviews[i].n; memcpy(views[i].m, F.fviews[i].m, sizeof views[i].m); }
    mod_begin();
    const char *fail = NULL;
    for (int i = 0; i < n && !fail; i++) {
        const Surf *s = &F.surfs[F.ents[ls[i]].srf];
        PlMesh m; const char *why = NULL;
        int nt = plm_mesh(&F, s, chord * 0.5, &m, &why);   /* FUN_18054be80(1, ent, surf_step_chord_dev * 0.5, ..) */
        if (nt < 0) { fail = why ? why : "Curve Parting Line: make_surf_polygons NOT PORTED"; break; }
        if (nt > 0) {
            PlPts pp; plp_alloc(&pp);
            if (plp_points(&F, s, &m, V, pl_ang * 0.017453292519943295, chord, tol, metric, &pp) < 0) snprintf(cam_msg, sizeof cam_msg, "Too many points");   /* post_warning(0x14e4) */
            int st = 0;
            for (int c = 0; c < pp.nch; c++) {   /* FUN_180565bc0: every chain of 2 nodes or more through msf_create_spline */
                int cn = pp.cnt[c]; const double *Q = pp.P + 3 * st; st += cn;
                if (cn < 2) continue;
                PlCurve cv;
                if (!plc_curve(Q, cn, chord_mode, chord, tol, &cv)) continue;
                if (cv.listed) { listed++; if (chord_mode) plc_convert(&cv, chord, tol, nurbs, views, F.nfviews); }   /* FUN_180538d30 (surf_use_chord_dev) on the listed ones */
                int k = -1;
                if (cv.kind == 2) { V3 a = { cv.line[0], cv.line[1], cv.line[2] }, b = { cv.line[3], cv.line[4], cv.line[5] }, z = { 0, 0, 0 }; k = mcam_new_ent(&F, E_LINE, a, b, z, 0, 0, 0, geo_level, geo_color); }
                else if (cv.kind == 4) {
                    McsSimplify *o = &cv.simp;
                    if (o->view_is_new) { F.fviews = realloc(F.fviews, sizeof *F.fviews * (size_t)(F.nfviews + 1)); F.fviews[F.nfviews].n = o->view; memcpy(F.fviews[F.nfviews].m, o->view_m, sizeof o->view_m); F.nfviews++; views = realloc(views, sizeof *views * (size_t)(F.nfviews + 1)); views[F.nfviews - 1].n = o->view; memcpy(views[F.nfviews - 1].m, o->view_m, sizeof o->view_m); }
                    V3 cc = { o->c[0], o->c[1], o->c[2] }; k = mcam_new_arc_view(&F, o->view, o->view_m, cc, o->r, o->a0, o->sw, geo_level, geo_color);
                }
                else if ((cv.kind == 8 || cv.kind == 0x80) && cv.rec) k = mcam_new_curve(&F, cv.rec, cv.rn, geo_level, geo_color);
                if (k >= 0) { attr_new_ent(k); made++; }
                plc_free(&cv);
            }
            plp_free(&pp);
        }
        plm_free(&m);
    }
    mod_end(); free(views); free(ls);
    if (fail) { if (made) { do_undo(); if (nredo) urec_free(&redo_st[--nredo]); } snprintf(cam_msg, sizeof cam_msg, "%s", fail); overlay_dirty = 1; return; }
    pl_live = made > 0;
    if (made) doc_dirty = 1;
    pl_l_method = pl_method; pl_l_chord = pl_chord; pl_l_dist = pl_dist; pl_l_ang = pl_ang;
    if (!listed) snprintf(cam_msg, sizeof cam_msg, "No Parting Lines found in this view");   /* parting_line: post_warning(0x154b) (mccoreres 5451) when no curve was listed */
    else pln_msg();
    overlay_dirty = 1;
}
/* every build first undoes the last one (FUN_1802be7b0: IsUndoTransActive and bVar3 -> UndoTransTerminate) */
static void pln_rebuild(void) {
    if (pl_live) { do_undo(); if (nredo) urec_free(&redo_st[--nredo]); pl_live = 0; }
    pln_build();
}
/* get_event's nonzero return (the <Enter> key, "press <ENTER>"): LAB_1802bef07 rebuilds when bVar5 */
void pln_enter_key(void) { if (pl_built) pln_rebuild(); }
/* FUN_1802be7b0's panel codes: 0 OK / 2 Cancel leave (the live result is committed after the loop in both); 1 Apply: the first time (bVar5 false) bVar5 = 1 and the
   build, else commit (UndoTransCommit), bVar5 = bVar3 = 0, the selection cleared, prompt 5972 and the selection again; 4 .. 7 the panel values: rebuild when bVar5 */
int pln_button(int code) {
    if (code == 0 || code == 2) { pln_save(); return 0; }   /* the loop's end: FUN_1802be190 -> FUN_180247770 */
    if (code == 1) {
        if (!pl_built) { pl_built = 1; pln_rebuild(); return 1; }
        pl_built = 0; pl_live = 0; sel_clear(); pln_msg(); return 1;
    }
    return 1;
}
void pln_panel_sync(void) {
    if (!pl_built) return;
    if (pl_live && pl_l_method == pl_method && pl_l_chord == pl_chord && pl_l_dist == pl_dist && pl_l_ang == pl_ang) return;
    pln_rebuild();
}
void pln_script(const char *arg) {
    double a; int k;
    if (sscanf(arg, "chord=%lf", &a) == 1) pl_chord = a;
    else if (sscanf(arg, "dist=%lf", &a) == 1) pl_dist = a;
    else if (sscanf(arg, "ang=%lf", &a) == 1) pl_ang = a;
    else if (sscanf(arg, "method=%d", &k) == 1) pl_method = k ? 1 : 0;
    else if (!strcmp(arg, "msg")) { double V[3]; pln_view_z(V); printf("    rg plmsg '%s' chord %.6g dist %.6g ang %.6g method %d view %.4f %.4f %.4f\n", cam_msg, pl_chord, pl_dist, pl_ang, pl_method, V[0], V[1], V[2]); return; }   /* tests: the message line and the panel values */
    pln_panel_sync();
}
/* the BAML (wireframecurvepartinglinemanagerview): Curve Quality (Chord heigh_t / _Distance radios, each EvaluatorBox enabled by its radio: the manager's
   +0x48 SetEnabled calls in FUN_180246410), _Angle (-90 .. 90: FUN_1802456e0's limits 0xc056800000000000 / 0x4056800000000000); the values' minimum tol
   (FUN_1802457e0 / FUN_18016eb40 with tol_exref) */
void pln_panel_body(const CuiPanelOps *ops, void *u) {
    if (ops->exp(u, "Curve Quality")) {
        ops->radio(u, "Chord heigh_t:", &pl_method, 0, 1);
        ops->num(u, "", "plchord", &pl_chord, NULL, pl_method == 0, mc_systol.tol, 1e6);
        ops->radio(u, "_Distance:", &pl_method, 1, 1);
        ops->num(u, "", "pldist", &pl_dist, NULL, pl_method == 1, mc_systol.tol, 1e6);
        ops->end(u);
    }
    if (ops->exp(u, "_Angle")) { ops->num(u, "", "plang", &pl_ang, NULL, 1, -90.0, 90.0); ops->end(u); }
}
