/* cui_sel.c - entity selection (ent_sel; a pick toggles, select_ent_univ's nearest-candidate rule), the Operations
   Manager multi-selection (Ctrl / Shift), picking (pick_entity, pick_point_under), window / polygon selection, the
   auto-highlight rule and hover entity, the status-bar cursor, the middle-button rotation pivot and its markers, the
   keymap and the graphics right-click menu files, the config items of these features. Moved from viewer/mcamui.c. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "paths.h"
#include "core_ui.h"
#include "mcam_saveops.h"
#include "mcsurf.h"

#define CP(dst, src) snprintf(dst, sizeof dst, "%s", src)

unsigned char *ent_sel; int ent_sel_n, nselected;
int hover_ent = -1;                                /* the entity under the cursor, drawn highlighted (build_overlay) */
unsigned char op_msel[1024]; int msel_owner = -9;      /* Operations Manager multi-selection (op_click) */

int vp_x, vp_y, vp_w, vp_h;                     /* viewport in window coordinates (y down) */

double seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, L = dx * dx + dy * dy;
    float t = L > 0 ? ((px - ax) * dx + (py - ay) * dy) / L : 0;
    if (t < 0) t = 0; if (t > 1) t = 1;
    return hypot(px - (ax + t * dx), py - (ay + t * dy));
}

/* spline curves (E_SPLINE, formats/spline_chain.md): the general selection takes them as any curve - mccore's idle pick / highlight
   (FUN_1804df930, decomp/xform/gaps/idle.c l.136-144) uses get_current_sel_mask, 0x1f1fff when none is set, & 0x8f in the standard mode
   (points, lines, arcs, parametric splines 8, NURBS 0x80), with select_ent_univ's pick rule (the drawn polyline here) - so do a
   Transform's select_ents (mask 0x1f1fff) and Engrave chaining; the other wireframe commands' picks here do not (their spline cases are
   not ported), nor their lists (sel_list) */
static int spl_skip(const Ent *en) { return en->type == E_SPLINE && geo_cmd && geo_cmd != G_PTNODES && !cui_spline_chaining() && !xf_on() && !sp_cmd(geo_cmd); }   /* the spline commands (cui_spline.c) select splines (select_ents masks 0x88 / 0x8e / 0x188) */
/* + a hidden level: not in the display list select_ent_univ / the window selection walk (mcam_load.h mcam_level_visible) */
/* get_event's sel-bit argument (its 5th, param_5): select_ent_univ takes only entities whose sel bits hold all of it (local_5884 =
   param_7, decomp/mastercam_seluniv.c 290 / 353) and get_event's MCEvent::FilterEptrArray(ev, mask, param_5) filters a window's list
   the same way (decomp/mccore_getevent.c 189-191); 0 = none. Here the bits 2 / 8 are the transform marks (cui_xform.c xf_ent_mark) */
int pick_req_bits;
static int dl_skip(const Ent *en) {
    if (spl_skip(en) || !ENT_LV_SHOWN(en)) return 1;
    return pick_req_bits && (xf_ent_mark((int)(en - F.ents)) & pick_req_bits) != pick_req_bits;
}
/* GetWindowSelectMode (mastercam.dll @180263c10, decomp/getevent/mastercam_winselmode.c 18-34): mccore's GraphicsOverlayManager
   "SelectionContainmentBy" (+0x90: vtable +0x158 = FUN_180b3be50, set by +0x160 = FUN_180b3e860; decomp/getevent/mccore_overlaymgr.c
   122-145) mapped 0 -> 1, 1 -> 5, 2 -> 2, 3 -> 4, 4 -> 3 (window_select's modes 1..5 = the Chaining dialog's combo 538 order:
   SetWindowSelectMode(index + 1), decomp/mccore_chain_range.c 15648). The ctor FUN_180b356d0 zeroes it (mccore_overlaymgr.c 376, the
   8-byte store over +0x8c / +0x90); its setters are SetWindowSelectMode - the Chaining dialog's, which puts the previous mode back
   when chaining ends (mccore_chain_range.c 16323 / 16773) - and the General Selection panel (not ported here). So outside chaining
   it is 1 (Inside) here. */
int gs_containment;
int get_window_select_mode(void) { static const int m[5] = { 1, 5, 2, 4, 3 }; return gs_containment >= 0 && gs_containment <= 4 ? m[gs_containment] : 1; }
/* window / polygon selection: surfaces have their own tests (surf_win, the polygon branch) */
static int win_skip(const Ent *en) { return dl_skip(en) || en->type == E_SURF || en->type == E_NOTE || en->type == E_MESH; }   /* meshes: ent_in_polygon_view /
                                                                      ent_intersect_polygon's PMESH_ID tests (mastercam.dll FUN_1802075b0 / FUN_180207860) NOT PORTED */   /* notes (E_NOTE): the window /
                                                                      polygon test of a drafting entity is NOT DECODED - not window-selected */
/* surfaces (E_SURF): picked unless blanked / deleted / hidden here (Surf.off) */
static int surf_pickable(const Ent *en) { return en->type == E_SURF && en->srf >= 0 && en->srf < F.nsurfs && !F.surfs[en->srf].blanked && !F.surfs[en->srf].off && ENT_LV_SHOWN(en); }
/* meshes (E_MESH): picked unless blanked / deleted here (McMesh.off) / on a hidden level */
static int mesh_pickable(const Ent *en) { return en->type == E_MESH && en->msh >= 0 && en->msh < F.nmeshes && !F.meshes[en->msh].off && !en->blanked && ENT_LV_SHOWN(en); }
/* gl_Hit_Surfs (mastercam.dll FUN_180224220, decomp/surfpick/mc_pick.c): with shading on, the surface whose shading triangles the
   cursor ray hits nearest the viewer (the ray = view_to_world of the cursor at view depths 1 and 0; each surface first tested
   against its bounding circle / screen box). Returns the Ent index, -1 none; *depth = the view z select_ent_univ compares */
static int pss_no_mesh;                           /* chaining's surface mask (chain_srf_pick): surfaces only, no PMESH_ID */
static int pss_ok(int i) { int e = F.surfs[i].ent; return e >= 0 && e < F.nents && surf_pickable(&F.ents[e]) && !dl_skip(&F.ents[e]); }
/* select_ent_univ's (u, v) buffer (param_6). The hover (DrawAutoHighlight 0x18031f290 -> FUN_18020d280 = find_an_entity ->
   select_ent_from_sel_pt -> select_ent_univ) passes the global DAT_1809a0c28 (2 doubles, .bss: 0 at start), and a click takes the
   hovered entity with that buffer (FUN_1804dff70, decomp/draft/ev_ac.c l.50: GetAutoHighlightEntity before find_an_entity). So when
   no projection of gl_Hit_Surfs writes a (u, v) (CS-21), the surface is evaluated at what the buffer held: the last (u, v) a shaded
   pick wrote, or - when a wireframe / other candidate was found earlier in the same select_ent_univ call (l.540-716) - that
   candidate's parameter, which this program's wireframe pick does not compute (ah_known = 0: NOT DECODED, the hit not taken). */
static double ah_t[2]; static int ah_known = 1;
double pick_srf_uv[2]; int pick_srf_uv_ok;        /* the (u, v) of the last shaded surface pick (chain_srf_edge) */
int pick_noport_under = -1;                       /* CS-20: an Ent the cursor is over whose shaded pick is not decoded (message only) */
/* the port (cui_srfdl.c srfhit_pick: Mastercam's shading triangles, oracle-checked) on the surfaces whose make_surf_polygons is ported;
   *depth = the view z of eval_surf at the hit's (u, v) (select_ent_univ l.818-826 compares that point with the wireframe hit); sel bit
   0x1000 (l.819): not taken. CS-20: surfaces whose mesh is not ported and polygonal meshes (PickShadedPolygonalMeshFromCursor
   FUN_180206b10 -> MCLoadSTLMesh / MCLoadIFFMesh / MCLoadMCOMesh::Intersect, mccore 0x180b7ba60 / 0x180b7b2d0 / 0x180b7b760: NOT
   DECODED) are not picked with shading on; this program's own triangles say whether the cursor is over one of them and how near
   (pick_noport_under, *zo its view z): one in front of the port's hit hides it (nothing taken: a click gets the message). */
static int noport_under(float mx, float my, double *zo) {
    int best = -1; double bz = -1e300;
    scene_surf_wire_build();
    for (int pass = 0; pass < 2; pass++) {
        int n = pass ? (pss_no_mesh ? 0 : F.nmeshes) : F.nsurfs;
        for (int i = 0; i < n; i++) {
            int e; const float *t; int nt;
            if (!pass) { e = F.surfs[i].ent; if (e < 0 || e >= F.nents || !surf_pickable(&F.ents[e]) || srfhit_ported(i)) continue; nt = scene_part_surf_tris(i, &t); }
            else { e = F.meshes[i].ent; if (e < 0 || e >= F.nents || !mesh_pickable(&F.ents[e]) || dl_skip(&F.ents[e])) continue; nt = scene_mesh_tris(i, &t); }
            for (int k = 0; k < nt; k++) {
                const float *v = t + 18 * k; float sx[3], sy[3]; int ok = 1;
                for (int q = 0; q < 3 && ok; q++) { ok = scene_to_screen(vp_w, vp_h, v[6 * q], v[6 * q + 1], v[6 * q + 2], &sx[q], &sy[q]); sx[q] += vp_x; sy[q] += vp_y; }
                if (!ok) continue;
                double d = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]); if (fabs(d) < 1e-12) continue;
                double b1 = ((mx - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (my - sy[0])) / d, b2 = ((sx[1] - sx[0]) * (my - sy[0]) - (mx - sx[0]) * (sy[1] - sy[0])) / d;
                if (!(b1 >= 0 && b2 >= 0 && b1 + b2 <= 1)) continue;
                double z[3]; for (int q = 0; q < 3; q++) z[q] = vmat[6] * v[6 * q] + vmat[7] * v[6 * q + 1] + vmat[8] * v[6 * q + 2];
                double zz = z[0] + b1 * (z[1] - z[0]) + b2 * (z[2] - z[0]);
                if (zz > bz) { bz = zz; best = e; }
            }
        }
    }
    *zo = bz; return best;
}
static int pick_surf_shaded(float mx, float my, int wire_found, double *depth, double *hitz) {
    int best = -1; double w[3], cur[2], uv[2], hz; int si, set, np = 0; const char *why = NULL;
    pick_srf_uv_ok = 0;
    if (wire_found) ah_known = 0;                 /* select_ent_univ wrote that candidate's parameter into the buffer */
    scene_pick(vp_x, 0, vp_w, vp_h, mx, my - vp_y, w);
    cur[0] = vmat[0] * w[0] + vmat[1] * w[1] + vmat[2] * w[2]; cur[1] = vmat[3] * w[0] + vmat[4] * w[1] + vmat[5] * w[2];
    if (srfhit_pick(cur, pss_ok, &si, uv, &set, &hz, &np, &why) == 1) {
        if (set) { ah_t[0] = uv[0]; ah_t[1] = uv[1]; ah_known = 1; }
        if (ah_known && !((F.surfs[si].rec[4] | F.surfs[si].rec[5] << 8) & 0x1000)) {
            double P[3]; if (!srfhit_point(si, ah_t, P)) { best = F.surfs[si].ent; *hitz = hz; *depth = vmat[6] * P[0] + vmat[7] * P[1] + vmat[8] * P[2]; pick_srf_uv[0] = ah_t[0]; pick_srf_uv[1] = ah_t[1]; pick_srf_uv_ok = 1; }
        }
    }
    (void)np; (void)why;
    return best;
}
/* CS-20: a click that took nothing over a surface / mesh whose shaded pick is not decoded says so */
int pick_noport_msg(void) {
    if (pick_noport_under < 0 || pick_noport_under >= F.nents || !solid_mode) return 0;
    const Ent *en = &F.ents[pick_noport_under]; char m[300];
    if (en->type == E_MESH) snprintf(m, sizeof m, "Picking a mesh in shaded display is not decoded yet (PickShadedPolygonalMeshFromCursor); switch to wireframe to pick it.");
    else snprintf(m, sizeof m, "Picking this surface type (kind %d) in shaded display is not decoded yet (its shading triangles, make_surf_polygons); switch to wireframe to pick it.", en->srf >= 0 && en->srf < F.nsurfs ? F.surfs[en->srf].kind : -1);
    if (getenv("MCAMUI_CHAINDUMP")) { printf("    cd noport ent %d type %d: %s\n", pick_noport_under, en->type, m); fflush(stdout); }   /* tests */
    cui_message("apprenticeCAM", m);
    return 1;
}
/* (SEL_APERTURE_PX: core_ui.h - sel_dist = view height / 30 at 522 px, set_parms)
   select_ent_univ / FUN_18020f4e0 (decomp/mastercam_seluniv.c): the nearest entity within the aperture; a surface (class 0x100) is a
   candidate by dist_to_surf (mccore 0x1802ddba0, decomp/surfpick/core_dist.c: the screen distance to its iso lines at surf_division's
   spacing = the wireframe it is drawn with) only with shading off (gl_shading_on: skipped, l.405). With shading on, after the
   candidates gl_Hit_Surfs' surface (pick_surf_shaded) replaces the hit unless the hit's view z is at least the surface point's
   (l.819-838). */
/* surfaces are candidates only where Mastercam's selection mask takes them (the mask of the prompt: ours, by state): the general
   selection with no command running, and a Surfaces command's surface prompt (srf_pick_active, cui_surf.c). The wireframe commands,
   chaining, the Extrude / solid selection, Analyze's entity pick, drive and stock picks keep their wireframe masks. */
int srf_pick_active;
/* S4: the 3D surface ops' drive / check prompts (cui_chain.c drv_pick: select_ents mask 0x110100 = SURF_ID 0x100, SOLID_ID 0x10000,
   PMESH_ID 0x100000 - formats/surface_selection.md 1.4) take surfaces and no wireframe: pick_entity_surf */
int pick_surfs_ok(void) { return srf_pick_active || xf_selecting() || geo_cmd == G_X2NURBS || drv_pick || (!cam_mode && !geo_cmd && !geo_chaining && !ex_mode && !sld_mode && !analyze_pick && !st_pick); }
static int pick_only_surf;
int pick_entity_surf(float mx, float my) { pick_only_surf = 1; int e = pick_entity(mx, my); pick_only_surf = 0; return e >= 0 && F.ents[e].type == E_SURF ? e : -1; }
int pick_entity(float mx, float my) {
    int best = -1; double bd = SEL_APERTURE_PX; V3 bp = { 0 };
    pick_noport_under = -1;
    int shaded = solid_mode != 0, surfs = (F.nsurfs || F.nmeshes) && pick_surfs_ok();
    /* the Chaining dialog in Single mode with config 6026 "Allow surface edges in single mode" (run_chain_dlg case 2: get_event's mask
       DAT_1813c4cbc | 0x100, decomp/mccore_chain_range.c l.17843-17850): SURF_ID joins the wireframe mask - surfaces only (no PMESH_ID / notes) */
    int csrf = !surfs && F.nsurfs && chain_srf_pick();
    double cur[2] = { 0, 0 }; int have_cur = 0;   /* the cursor in view x / y (proj_iso_pt of a point on its ray) */
    if (!shaded && (surfs || csrf)) scene_surf_wire_build();
    for (int e = 0; e < F.nents; e++) {
        const Ent *en = &F.ents[e]; if (dl_skip(en)) continue;
        if (pick_only_surf && en->type != E_SURF) continue;
        if (en->type == E_MESH) {                         /* FUN_180205380 (shading off): the box first, then each triangle's three edges
                                                             projected (FUN_180204f40: dist_to_finite_line), the nearest */
            if (shaded || !surfs || !mesh_pickable(en)) continue;
            const float *sg; int ns = scene_mesh_wire(en->msh, &sg);
            for (int k = 0; k < ns; k++) {
                const float *a = sg + 12 * k, *b = a + 6; float ax, ay, bx, by;
                if (!scene_to_screen(vp_w, vp_h, a[0], a[1], a[2], &ax, &ay) || !scene_to_screen(vp_w, vp_h, b[0], b[1], b[2], &bx, &by)) continue;
                double d = seg_dist(mx, my, ax + vp_x, ay + vp_y, bx + vp_x, by + vp_y);
                if (d < bd) { bd = d; best = e; bp = (V3){ a[0], a[1], a[2] }; }
            }
            continue;
        }
        if (en->type == E_SURF) {   /* select_ent_univ l.403-411 (shading off): FUN_18013cbc0 on the surface's display-list points when its
                                       node is built, else dist_to_surf; the distances in view units (here x pixels per unit, as bd) */
            if (shaded || !(surfs || csrf) || !surf_pickable(en)) continue;
            if (!have_cur) { double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, mx, my - vp_y, w);
                cur[0] = vmat[0] * w[0] + vmat[1] * w[1] + vmat[2] * w[2]; cur[1] = vmat[3] * w[0] + vmat[4] * w[1] + vmat[5] * w[2]; have_cur = 1; }
            double ppu = view_h > 0 && vht > 0 ? view_h / vht : 1.0, d = 0, uv[2]; const char *why = NULL;
            int r = srfdl_pick(en->srf, cur, bd / ppu, &d, uv, &why);
            if (r == -1) { if (F.surfs[en->srf].kind != 10 ? dist_to_surf_uv(&F.surfs[en->srf], cur, bd / ppu, uv, &d) : srfdl_dist_trimmed(en->srf, cur, bd / ppu, &d, uv, &why) == 1) r = 1; }   /* kind 10: FUN_1802de180 */
            if (r == 1 && d * ppu < bd) { bd = d * ppu; best = e; bp = (V3){ 0, 0, 0 }; }   /* bp: read only with shading on */
            continue;
        }
        for (int pi = en->first_poly; pi < en->first_poly + en->npoly; pi++) {
            const Poly *p = &F.polys[pi];
            float ax = 0, ay = 0;
            for (int k = 0; k < p->count; k++) {
                V3 q = F.pts[p->first + k]; float sx, sy;
                if (!scene_to_screen(vp_w, vp_h, q.x, q.y, q.z, &sx, &sy)) continue;
                sx += vp_x; sy += vp_y;
                if (k) { double d = seg_dist(mx, my, ax, ay, sx, sy); if (d < bd) { bd = d; best = e; bp = q; } }
                ax = sx; ay = sy;
            }
        }
    }
    /* notes (E_NOTE): candidates where surfaces are (the general selection; the commands keep their masks) - select_ent_univ's N_ID / W_ID
       distance (cui_note.c note_pick), the nearest wins (strictly nearer than the wireframe's) */
    if (pick_surfs_ok() && F.nnotes && !pick_only_surf) { double nd, nw[3]; int ne = note_pick(mx, my, &nd, nw); if (ne >= 0 && nd < bd) { bd = nd; best = ne; bp = (V3){ nw[0], nw[1], nw[2] }; } }
    if (shaded && (surfs || csrf)) {
        double sz, hz = -1e300, nz; pss_no_mesh = csrf; int s = pick_surf_shaded(mx, my, best >= 0, &sz, &hz);
        int np = noport_under(mx, my, &nz);
        if (np >= 0 && (s < 0 || nz > hz)) { s = -1; pick_srf_uv_ok = 0; }   /* CS-20: in front, not decoded */
        if (s >= 0 && (best < 0 || vmat[6] * bp.x + vmat[7] * bp.y + vmat[8] * bp.z < sz)) best = s;
        if (best < 0) pick_noport_under = np;
        pss_no_mesh = 0;
    }
    return best;
}

/* the point on the geometry under the cursor (within the selection aperture), as the 3D point Mastercam's dynamic
   rotation picks for its pivot (FUN_18020f4e0); 0 if nothing is there */
int pick_point_under(float mx, float my, double out[3]) {
    double bd = SEL_APERTURE_PX; int found = 0;
    for (int e = 0; e < F.nents; e++) {
        const Ent *en = &F.ents[e]; if (dl_skip(en)) continue;
        for (int pi = en->first_poly; pi < en->first_poly + en->npoly; pi++) {
            const Poly *p = &F.polys[pi];
            float ax = 0, ay = 0; V3 pa = { 0 };
            for (int k = 0; k < p->count; k++) {
                V3 q = F.pts[p->first + k]; float sx, sy;
                if (!scene_to_screen(vp_w, vp_h, q.x, q.y, q.z, &sx, &sy)) continue;
                sx += vp_x; sy += vp_y;
                if (k) {
                    double ux = sx - ax, uy = sy - ay, l2 = ux * ux + uy * uy, t = l2 > 0 ? ((mx - ax) * ux + (my - ay) * uy) / l2 : 0;
                    t = t < 0 ? 0 : t > 1 ? 1 : t;
                    double d = hypot(ax + ux * t - mx, ay + uy * t - my);
                    if (d < bd) { bd = d; found = 1; out[0] = pa.x + (q.x - pa.x) * t; out[1] = pa.y + (q.y - pa.y) * t; out[2] = pa.z + (q.z - pa.z) * t; }
                }
                ax = sx; ay = sy; pa = q;
            }
        }
    }
    return found;
}

void sel_grow(void) { if (ent_sel_n != F.nents) { ent_sel = realloc(ent_sel, F.nents ? F.nents : 1); for (int i = ent_sel_n; i < F.nents; i++) ent_sel[i] = 0; ent_sel_n = F.nents; } }
int is_sel(int i) { return ent_sel && i < ent_sel_n && ent_sel[i] && F.ents[i].type; }
/* the selected entities, or every visible one if none is selected (all_if_none) */
int sel_list(int **out, int all_if_none) {   /* the wireframe commands' list: surfaces (E_SURF) are never in it (sel_list_surfs) */
    int n = 0, *l = malloc(sizeof(int) * (F.nents ? F.nents : 1));
    for (int i = 0; i < F.nents; i++) if (is_sel(i) && F.ents[i].type != E_SURF && F.ents[i].type != E_NOTE && F.ents[i].type != E_MESH && (F.ents[i].type != E_SPLINE || xf_on() || geo_cmd == G_BREAKN || sp_cmd(geo_cmd) || sl_cmd(geo_cmd) || si_cmd(geo_cmd))) l[n++] = i;   /* splines: a Transform's and Break Many's (select_ents mask 0x8e) only; notes: never (the
                                                                      wireframe commands do not take drafting) */
    if (!n && all_if_none) for (int i = 0; i < F.nents; i++) if (F.ents[i].type && (F.ents[i].type != E_SPLINE || geo_cmd == G_BREAKN || geo_cmd == G_CSLICE) && F.ents[i].type != E_SURF && F.ents[i].type != E_NOTE && F.ents[i].type != E_MESH) l[n++] = i;
    *out = l; return n;
}
int sel_list_surfs(int **out) {               /* the selected surfaces (Ent indices) */
    int n = 0, *l = malloc(sizeof(int) * (F.nents ? F.nents : 1));
    for (int i = 0; i < F.nents; i++) if (is_sel(i) && F.ents[i].type == E_SURF) l[n++] = i;
    *out = l; return n;
}
void sel_clear(void) { if (ent_sel) memset(ent_sel, 0, ent_sel_n); nselected = 0; }
/* Esc with no function running (decoded 2026-11-14, formats/ui.md "Esc with no function running"). The main frame's
   PreTranslateMessage (mastercam.dll FUN_18026ad90) posts every WM_KEYDOWN as thread message 0x802a (Esc) / 0x802b (any other
   key); with the Toolpaths manager focused COpMgrTree::PreTranslateMessage (@180364f70) posts Esc the same way. A running
   function reads it through get_event (its own Esc: the prompt / command ends). Otherwise CMastercamApp::Run (@180294550) hands
   the messages 0x8028..0x8038 to mccore PumpPreSelection (@1804e6890) - only while allow_preselection (config 4001 "Allow
   pre-selection", mccore FUN_1801ba2f0 case 0xfa1; ours cfg_presel). There FUN_1804e50a0 -> FUN_1804e3560: a polygon selection
   being drawn (DAT_1813f5f40 +1) or a window being dragged (DAT_1813f5f38 > 0) is cancelled and that is all; else
   PostMessage(0x8028, 0x8104) -> FUN_1804e6250 case 0x8104: FUN_1804e0710 (the preselection event state reset) and
   FUN_1804e07a0 = set_clr_sel_bits_all(0, 4, 0, 4, 0x1f1fff, 0, 0) (selection bit 4 off on every entity), repaint_graphics,
   gl_check_flush. Here: no idle window / polygon selection exists (nothing to cancel first) and the preselection event state
   has no counterpart (nothing held); the entity selection is cleared and the overlay redrawn. Returns 1 when it acted. */
int sel_escape_idle(void) {
    if (!cfg_presel) return 0;                   /* allow_preselection off: PumpPreSelection is not called, Esc does nothing */
    sel_clear(); overlay_dirty = 1;
    return 1;
}
/* a pick on an entity in general selection (mcamui.c handle_click): the entity's selected state flips */
void sel_toggle(int e) {
    if (e < 0) return;
    if (ent_sel_n != F.nents) { free(ent_sel); ent_sel = calloc(F.nents ? F.nents : 1, 1); ent_sel_n = F.nents; nselected = 0; }
    ent_sel[e] = !ent_sel[e]; nselected += ent_sel[e] ? 1 : -1;
    overlay_dirty = 1;
}
/* SelectAllMask (Ctrl+A, mastercam.kmp): every visible entity (mcamui.c run_mc_function) */
void sel_all(void) { sel_grow(); nselected = 0; for (int i = 0; i < F.nents; i++) { ent_sel[i] = F.ents[i].type != 0 && (F.ents[i].type != E_SPLINE || !spl_skip(&F.ents[i])) && (F.ents[i].type != E_SURF || surf_pickable(&F.ents[i]))
        && (F.ents[i].type != E_MESH || mesh_pickable(&F.ents[i])) && !(F.ents[i].type == E_NOTE && F.ents[i].blanked); nselected += ent_sel[i]; } overlay_dirty = 1; }   /* a blanked note (sel bit 0x100) is not
                                                                  drawn, so not visible / selectable - as a blanked surface (surf_pickable) */
/* the test scripts' sel:3,4,5 (sel:none clears) */
void sel_set_list(const char *arg) {
    sel_grow(); memset(ent_sel, 0, ent_sel_n); nselected = 0;
    for (char *q = (char *)arg; *q; ) { int en = (int)strtol(q, &q, 10); if (en >= 0 && en < F.nents && F.ents[en].type && (F.ents[en].type != E_SPLINE || !spl_skip(&F.ents[en]))) { ent_sel[en] = 1; nselected++; } if (*q == ',') q++; else break; }
    overlay_dirty = 1;
}

/* operations whose geometry includes entity I (ours by chain, Mastercam's by their TP_ID references); writes their
   tree numbers into OUT, returns the count */
int ent_users(int i, char *out, size_t n) {
    int cnt = 0; if (n) out[0] = 0;
    for (int j = 0; j < ncamops; j++) {
        const CamOp *o = camops[j]; int hit = 0;
        for (int c = 0; c < o->nchains && !hit; c++) for (int k = 0; k < o->chains[c].n; k++) if (o->chains[c].ent[k] == i) { hit = 1; break; }
        if (hit) { size_t l = strlen(out); if (l + 8 < n) snprintf(out + l, n - l, "%s%d", cnt ? ", " : "", o->op_index + 1); cnt++; }
    }
    if (F.ents[i].idn > 0) for (int k = 0; k < F.ntp; k++) if (F.tp[k].ent == F.ents[i].idn) {
        int oi = -1; for (int q = 0; q < F.nops; q++) if (F.ops[q].id == F.tp[k].op_id) oi = q;
        int mine = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == oi) mine = 1;   /* taken over: counted above */
        if (oi >= 0 && !mine) { char t[16]; snprintf(t, sizeof t, "%d", oi + 1);
            if (!strstr(out, t)) { size_t l = strlen(out); if (l + 8 < n) snprintf(out + l, n - l, "%s%s", cnt ? ", " : "", t); cnt++; } }
    }
    return cnt;
}
/* Home > Delete entities (mccadutil delete_entities -> mccore delete_all_by_mask(4, 0x1f1fff, 0, 1), formats/align_geo.md 1): when a
   selected entity is used by an operation and "Suppress associativity warning messages" (config 17014, power_user) is off, dialog
   6561 asks; "Delete all selected entities" deletes everything and validate_toolpath marks every op that used a deleted entity dirty
   (op +0x94; the op is never deleted, it keeps its toolpath until regenerated); "Delete only non-associative entities" deletes the
   others; Cancel nothing. One undo step. [planes / solids / reliefs depending on geometry: not checked here] */
static int64_t surf_base_eptr(const Surf *sf) {     /* a trimmed (10: D+0) / offset (8: D+0x20) surface's base eptr, else 0 */
    int64_t v = 0;
    if (sf->kind == 10 && sf->n >= 0x108) memcpy(&v, sf->rec + 0x100, 8);
    else if (sf->kind == 8 && sf->n >= 0x128) memcpy(&v, sf->rec + 0x120, 8);
    return v;
}
/* delete_ent's base rule for one surface being removed inside an edit (mod_del): its blanked, otherwise unreferenced base goes too */
void srf_drop_base(int e) {
    if (e < 0 || e >= F.nents || F.ents[e].srf < 0 || F.ents[e].srf >= F.nsurfs) return;
    int64_t be = surf_base_eptr(&F.surfs[F.ents[e].srf]); if (!be) return;
    int b = -1; for (int q = 0; q < F.nents && b < 0; q++) if (F.ents[q].type == E_SURF && (F.ents[q].created ? F.ents[q].septr : F.ents[q].eptr) == be && F.ents[q].blanked) b = q;
    if (b < 0 || ent_users(b, (char[8]){ 0 }, 8) > 0) return;
    for (int q = 0; q < F.nsurfs; q++) { int qe = F.surfs[q].ent; if (qe >= 0 && qe != e && F.ents[qe].type == E_SURF && surf_base_eptr(&F.surfs[q]) == be) return; }
    mod_del(b);
}
void delete_selected_ents(void) {
    int nassoc = 0;
    for (int i = 0; ent_sel && i < ent_sel_n && i < F.nents; i++) if (ent_sel[i] && F.ents[i].type) { char who[8]; nassoc += ent_users(i, who, sizeof who) > 0; }
    int skip_assoc = 0;
    if (nassoc) {
        char cv[16]; mcam_config_value(mcam_is_metric(&F), 17014, cv, sizeof cv);
        int suppress = cv[0] == 'y' || cv[0] == 'Y';
        if (!suppress) {
            static const char *B[3] = { "Delete &all selected entities", "Delete &only non-associative entities", "&Cancel" };   /* 3690 / 3688 / 2 */
            int c = cui_hooks.ask_choice ? cui_hooks.ask_choice("Deleting a referenced entity!",
                "Some of the geometry selected is associated to a Toolpath, Solid, or a Plane.  Deleting this geometry will permanently alter those operations.\n\nWould you like to",
                B, 3) : cui_script_choice;
            if (c == 2 || c < 0) { cam_msg[0] = 0; return; }
            skip_assoc = c == 1;
        }
    }
    int n = 0; URec r = { U_ENTS_DEL, NULL, NULL, 0, NULL, NULL, 0 };
    for (int i = 0; ent_sel && i < ent_sel_n && i < F.nents; i++) if (ent_sel[i] && F.ents[i].type) {
        char who[8]; int used = ent_users(i, who, sizeof who) > 0;
        if (used && skip_assoc) continue;
        if (used) dirty_ent_users(i);                     /* validate_toolpath: the users get op +0x94 = 1 */
        r.ents = realloc(r.ents, sizeof(int) * (r.n + 1)); r.types = realloc(r.types, sizeof(int) * (r.n + 1));
        r.ents[r.n] = i; r.types[r.n] = F.ents[i].type; r.n++;
        ent_off(i); ent_sel[i] = 0; n++;
    }
    /* delete_ent (mccore, decomp/align/geo_core_a.c l.211-250): a deleted offset (kind 8, base eptr at D+0x20) or trimmed (kind 10, D+0)
       surface takes its base surface along when the base is blanked (sel bit 0x100) and nothing else references it */
    for (int k = 0; k < r.n; k++) {
        const Ent *en = &F.ents[r.ents[k]]; if (r.types[k] != E_SURF || en->srf < 0 || en->srf >= F.nsurfs) continue;
        int64_t be = surf_base_eptr(&F.surfs[en->srf]); if (!be) continue;
        int b = -1; for (int q = 0; q < F.nents && b < 0; q++) if (F.ents[q].type == E_SURF && (F.ents[q].created ? F.ents[q].septr : F.ents[q].eptr) == be && F.ents[q].blanked) b = q;
        if (b < 0) continue;
        int used = ent_users(b, (char[8]){ 0 }, 8) > 0;
        for (int q = 0; q < F.nsurfs && !used; q++) { int qe = F.surfs[q].ent; used = qe >= 0 && F.ents[qe].type == E_SURF && surf_base_eptr(&F.surfs[q]) == be; }
        if (used) continue;
        r.ents = realloc(r.ents, sizeof(int) * (r.n + 1)); r.types = realloc(r.types, sizeof(int) * (r.n + 1));
        r.ents[r.n] = b; r.types[r.n] = E_SURF; r.n++; ent_off(b);
    }
    nselected = 0; for (int i = 0; ent_sel && i < ent_sel_n && i < F.nents; i++) nselected += ent_sel[i] && F.ents[i].type;
    if (n) { undo_push(r); doc_dirty = 1; scene_reupload(); overlay_dirty = 1; cam_msg[0] = 0; if (cui_hooks.ops_changed) cui_hooks.ops_changed(); }
    else { free(r.ents); free(r.types); cam_msg[0] = 0; }
}

/* window selection of entities for Window chaining, in screen space (the window is drawn on the screen); modes
   1 Inside, 2 In + Intrs, 3 Intersect, 4 Out + Intrs, 5 Outside (mccoreres 4224-4228, SetWindowSelectMode) */
int seg_hits_rect(float ax, float ay, float bx, float by, float x0, float y0, float x1, float y1) {
    float t0 = 0, t1 = 1, dx = bx - ax, dy = by - ay, p[4] = { -dx, dx, -dy, dy }, q[4] = { ax - x0, x1 - ax, ay - y0, y1 - ay };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) { if (q[i] < 0) return 0; continue; }
        float r = q[i] / p[i];
        if (p[i] < 0) { if (r > t1) return 0; if (r > t0) t0 = r; } else { if (r < t0) return 0; if (r < t1) t1 = r; }
    }
    return 1;
}
/* ---- surfaces in a window / polygon (mastercam.dll ent_in_polygon_view -> FUN_180211b00, ent_intersect_polygon -> FUN_180211cd0;
   decomp/surfpick/winsel.c, winsel2.c). surf_screen_box (0x180340810): the stored box (+0x28 / +0x40) about its stored centre (+0x58),
   half widths = 0.5 x the largest |view row . d| of d = (dx, dy, dz), (-dx, dy, dz), (-dx, -dy, dz) (dx.. the box size); here in screen
   px through the four view-plane corners. */
static int poly_has(float x, float y);
static double sdd(const uint8_t *b) { double v; memcpy(&v, b, 8); return v; }
static int surf_sbox(int s, float *bx0, float *by0, float *bx1, float *by1, float *cx, float *cy, float *rpx) {
    const uint8_t *S = F.surfs[s].rec + 0x68; if (F.surfs[s].n < 0x68 + 0x78) return 0;
    double d[3], C[3], hw[2];
    for (int k = 0; k < 3; k++) { d[k] = sdd(S + 0x40 + 8 * k) - sdd(S + 0x28 + 8 * k); C[k] = sdd(S + 0x58 + 8 * k); }
    for (int r = 0; r < 2; r++) {
        const double *R = vmat + 3 * r; double a = R[0] * d[0] + R[1] * d[1] + R[2] * d[2], b = a - 2 * d[0] * R[0], c = b - 2 * d[1] * R[1];
        double m = fabs(a) >= fabs(b) ? fabs(a) : fabs(b); if (fabs(c) >= m) m = fabs(c); hw[r] = m * 0.5;
    }
    float xs[4], ys[4];
    for (int q = 0; q < 4; q++) {
        double sx = q & 1 ? hw[0] : -hw[0], sy = q & 2 ? hw[1] : -hw[1];
        double w[3] = { C[0] + sx * vmat[0] + sy * vmat[3], C[1] + sx * vmat[1] + sy * vmat[4], C[2] + sx * vmat[2] + sy * vmat[5] };
        if (!scene_to_screen(vp_w, vp_h, w[0], w[1], w[2], &xs[q], &ys[q])) return 0;
        xs[q] += vp_x; ys[q] += vp_y;
    }
    *bx0 = *bx1 = xs[0]; *by0 = *by1 = ys[0];
    for (int q = 1; q < 4; q++) { if (xs[q] < *bx0) *bx0 = xs[q]; if (xs[q] > *bx1) *bx1 = xs[q]; if (ys[q] < *by0) *by0 = ys[q]; if (ys[q] > *by1) *by1 = ys[q]; }
    float ex, ey; double r = sdd(S + 0x70);
    if (!scene_to_screen(vp_w, vp_h, C[0], C[1], C[2], cx, cy) || !scene_to_screen(vp_w, vp_h, C[0] + r * vmat[0], C[1] + r * vmat[1], C[2] + r * vmat[2], &ex, &ey)) return 0;
    *cx += vp_x; *cy += vp_y; ex += vp_x; ey += vp_y; *rpx = hypotf(ex - *cx, ey - *cy);
    return 1;
}
/* FUN_180211cd0's grid: iso lines u = const then v = const at (range / count) steps (the last at the end); counts: NURBS sqrt((ncu + 4) 8)
   / sqrt((ncv + 4) 8), parametric sqrt((nu + 5) 8) / sqrt((nv + 5) 8), revolved 10 / round(2 (v1 - v0)) 2 + 2, loft 10 / (n - 1) (x 2 in
   mode > 1), draft 10 / 1, else 5 / 5 (no minimum of 16, unlike surf_extent). hit(u0 screen segment) = it crosses an edge of the window
   (l_ints with t, s in (-1e-12, 1 + 1e-12)) */
static int surf_grid(const Surf *sf, int *nu, int *nv) {
    const uint8_t *r = sf->rec; int k = sf->kind;
    if (k == 0 && sf->n >= 0x118) { *nu = (int16_t)(int)sqrt((double)(*(const int32_t *)(r + 0x108) + 4) * 8.0); *nv = (int16_t)(int)sqrt((double)(*(const int32_t *)(r + 0x10c) + 4) * 8.0); }
    else if (k == 1 && sf->n >= 0x110) { *nu = (int16_t)(int)sqrt((double)(*(const int32_t *)(r + 0x100) + 5) * 8.0); *nv = (int16_t)(int)sqrt((double)(*(const int32_t *)(r + 0x104) + 5) * 8.0); }
    else if (k == 2) { double d = (sdd(r + 0x88) - sdd(r + 0x80)) * 2.0; d = d >= 0 ? floor(d + 0.5) : ceil(d - 0.5); *nu = 10; *nv = (int)d * 2 + 2; }
    else if (k == 4 && sf->n >= 0x110) { *nu = 10; *nv = *(const int32_t *)(r + 0x104) - 1; if (*(const int16_t *)(r + 0x100) > 1) *nv *= 2; }
    else if (k == 6) { *nu = 10; *nv = 1; }
    else { *nu = 5; *nv = 5; }
    return *nu > 0 && *nv > 0;
}
static int segs_cross(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
    double den = (double)(bx - ax) * (dy - cy) - (double)(by - ay) * (dx - cx); if (fabs(den) < 1e-300) return 0;
    double t = ((double)(cx - ax) * (dy - cy) - (double)(cy - ay) * (dx - cx)) / den, u = ((double)(cx - ax) * (by - ay) - (double)(cy - ay) * (bx - ax)) / den;
    return t > -1e-12 && t < 1.000000000001 && u > -1e-12 && u < 1.000000000001;
}
static int surf_crosses(int s, const float *ex, const float *ey, int ne) {   /* the window / polygon edges ex/ey (closed) */
    const Surf *sf = &F.surfs[s]; int nu, nv; if (!surf_grid(sf, &nu, &nv)) return 0;
    double d[4]; mcs_surf_domain(sf, d);
    for (int dir = 0; dir < 2; dir++) {
        double a0 = dir ? d[2] : d[0], a1 = dir ? d[3] : d[1], b0 = dir ? d[0] : d[2], b1 = dir ? d[1] : d[3];
        double sa = (a1 - a0) / (dir ? nv : nu), sb = (b1 - b0) / (dir ? nu : nv);
        int ea = 0;
        for (double a = a0; !ea; a += sa) { if (a >= a1) { ea = 1; a = a1; }
            int eb = 0, first = 1; float px = 0, py = 0;
            for (double b = b0; !eb; b += sb) { if (b >= b1) { eb = 1; b = b1; }
                double p[3]; float sx, sy;
                if (mcs_surf_eval(&F, sf, dir ? b : a, dir ? a : b, p) || !scene_to_screen(vp_w, vp_h, p[0], p[1], p[2], &sx, &sy)) { first = 1; continue; }
                sx += vp_x; sy += vp_y;
                if (!first) for (int i = 0; i < ne; i++) { int j = (i + 1) % ne; if (segs_cross(px, py, sx, sy, ex[i], ey[i], ex[j], ey[j])) return 1; }
                first = 0; px = sx; py = sy;
            }
            if (sa <= 0) break;
        }
    }
    return 0;
}
/* the window (rectangle; ent_in_polygon_view's param 4 set): inside = the bounding circle (the projected centre +- the radius +0x70) or the
   screen box strictly inside; crossing (FUN_180211cd0): not when the box is wholly inside or wholly outside, else the grid */
static void surf_win(int s, float x0, float y0, float x1, float y1, int *in, int *hit) {
    float bx0, by0, bx1, by1, cx, cy, rp; *in = *hit = 0;
    if (!surf_sbox(s, &bx0, &by0, &bx1, &by1, &cx, &cy, &rp)) return;
    *in = (x0 < cx - rp && cx + rp < x1 && y0 < cy - rp && cy + rp < y1) || (x0 < bx0 && y0 < by0 && bx1 < x1 && by1 < y1);
    int inside = x0 < bx0 && bx0 < x1 && x0 < bx1 && bx1 < x1 && y0 < by0 && by0 < y1 && y0 < by1 && by1 < y1;
    int outside = (x1 < bx0 && x1 < bx1) || (bx0 < x0 && bx1 < x0) || (y1 < by0 && y1 < by1) || (by0 < y0 && by1 < y0);
    if (inside || outside) return;
    float ex[4] = { x0, x1, x1, x0 }, ey[4] = { y0, y0, y1, y1 };
    *hit = surf_crosses(s, ex, ey, 4);
}
char *window_select(float x0, float y0, float x1, float y1, int mode) {
    /* (the caller sets vp_x / vp_y / vp_w / vp_h - mcamui: as the click path sets them) */
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; } if (y0 > y1) { float t = y0; y0 = y1; y1 = t; }
    char *sel = calloc(F.nents ? F.nents : 1, 1);
    int surfs = F.nsurfs && pick_surfs_ok();
    for (int e = 0; e < F.nents; e++) {
        const Ent *en = &F.ents[e];
        if (en->type == E_SURF) {                         /* ent_in_polygon_view / ent_intersect_polygon (above) */
            if (!surfs || !surf_pickable(en)) continue;
            int in, hit; surf_win(en->srf, x0, y0, x1, y1, &in, &hit);
            int any = in || hit; sel[e] = mode == 1 ? in : mode == 2 ? any : mode == 3 ? hit && !in : mode == 4 ? !in : !any;
            continue;
        }
        if (!en->type || win_skip(en)) continue;
        int all_in = 1, any_hit = 0, have = 0; float px = 0, py = 0;
        for (int pi = en->first_poly; pi < en->first_poly + en->npoly; pi++)
            for (int k = 0; k < F.polys[pi].count; k++) {
                V3 q = F.pts[F.polys[pi].first + k]; float sx, sy;
                if (!scene_to_screen(vp_w, vp_h, q.x, q.y, q.z, &sx, &sy)) continue;
                sx += vp_x; sy += vp_y;
                int in = sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
                if (!in) all_in = 0;
                if (in || (k > 0 && have && seg_hits_rect(px, py, sx, sy, x0, y0, x1, y1))) any_hit = 1;
                px = sx; py = sy; have = 1;
            }
        if (!have) continue;
        int cross = any_hit && !all_in;
        sel[e] = mode == 1 ? all_in : mode == 2 ? any_hit : mode == 3 ? cross : mode == 4 ? !all_in : !any_hit;
    }
    return sel;
}
/* Polygon (mode 6): the same selection modes against a screen polygon */
static const float *sp_x, *sp_y; static int sp_n;
static int poly_has(float x, float y) {
    int in = 0;
    for (int i = 0, j = sp_n - 1; i < sp_n; j = i++)
        if (((sp_y[i] > y) != (sp_y[j] > y)) && x < (sp_x[j] - sp_x[i]) * (y - sp_y[i]) / (sp_y[j] - sp_y[i]) + sp_x[i]) in = !in;
    return in;
}
static int poly_seg_cross(float ax, float ay, float bx, float by) {
    for (int i = 0, j = sp_n - 1; i < sp_n; j = i++) {
        float d1 = (sp_x[i] - sp_x[j]) * (ay - sp_y[j]) - (sp_y[i] - sp_y[j]) * (ax - sp_x[j]);
        float d2 = (sp_x[i] - sp_x[j]) * (by - sp_y[j]) - (sp_y[i] - sp_y[j]) * (bx - sp_x[j]);
        float d3 = (bx - ax) * (sp_y[j] - ay) - (by - ay) * (sp_x[j] - ax), d4 = (bx - ax) * (sp_y[i] - ay) - (by - ay) * (sp_x[i] - ax);
        if (((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0))) return 1;
    }
    return 0;
}
char *polygon_select(int mode, const float *px_, const float *py_, int n_) {
    sp_x = px_; sp_y = py_; sp_n = n_;               /* the sketched polygon (screen vertices; the caller sets vp_*) */
    char *sel = calloc(F.nents ? F.nents : 1, 1);
    int surfs = F.nsurfs && pick_surfs_ok();
    for (int e = 0; e < F.nents; e++) {
        const Ent *en = &F.ents[e];
        if (en->type == E_SURF) {                         /* FUN_180211b00 param 4 clear: the four screen-box corners in the polygon; the crossing grid */
            if (!surfs || !surf_pickable(en)) continue;
            float bx0, by0, bx1, by1, cx, cy, rp; if (!surf_sbox(en->srf, &bx0, &by0, &bx1, &by1, &cx, &cy, &rp)) continue;
            int in = poly_has(bx0, by0) && poly_has(bx1, by0) && poly_has(bx1, by1) && poly_has(bx0, by1);
            int hit = surf_crosses(en->srf, sp_x, sp_y, sp_n), any = in || hit;
            sel[e] = mode == 1 ? in : mode == 2 ? any : mode == 3 ? hit && !in : mode == 4 ? !in : !any;
            continue;
        }
        if (!en->type || win_skip(en)) continue;
        int all_in = 1, any_hit = 0, have = 0; float px = 0, py = 0;
        for (int pi = en->first_poly; pi < en->first_poly + en->npoly; pi++)
            for (int k = 0; k < F.polys[pi].count; k++) {
                V3 q = F.pts[F.polys[pi].first + k]; float sx, sy;
                if (!scene_to_screen(vp_w, vp_h, q.x, q.y, q.z, &sx, &sy)) continue;
                sx += vp_x; sy += vp_y;
                int in = poly_has(sx, sy);
                if (!in) all_in = 0;
                if (in || (k > 0 && have && poly_seg_cross(px, py, sx, sy))) any_hit = 1;
                px = sx; py = sy; have = 1;
            }
        if (!have) continue;
        int cross = any_hit && !all_in;
        sel[e] = mode == 1 ? all_in : mode == 2 ? any_hit : mode == 3 ? cross : mode == 4 ? !all_in : !any_hit;
    }
    return sel;
}

/* when the auto-highlight runs (mccore FUN_1804df930 / FUN_1804e11e0: an entity-selection prompt turns it on, point
   entry leaves it off): here = chaining, the wireframe commands' entity picks, the dialogs' entity picks, and idle
   general selection (config 4001, default on); off while drawing geometry points, as in the original */
int cui_autohl_active(const CuiHlState *s) {
    if (!loaded || !cfg_autohl || s->blocked) return 0;                          /* config 4036, default on */
    if (s->point_pick) return 0;                                                  /* point picks */
    if (s->entity_pick) return 1;                                                 /* entity picks */
    if (s->ex_mode) return !s->ex_phase;                                          /* the Extrude command's chaining */
    if (s->cam_chaining) return 1;                                                /* toolpath chaining / drill points */
    if (s->geo_cmd) return s->geo_need == 'e' || s->geo_need == 'c';
    return cfg_presel;
}
/* auto-highlight (DrawAutoHighlight mastercam.dll 0x18031f290, driven by mccore FUN_1804df930 on mouse moves): the
   entity under the cursor is redrawn in the auto-highlight colour while a selection is active (active = the rule above
   and no view drag / rubber band - gr_dynamic in the original) */
/* the solid hover highlight (formats/ui.md "Solid selection hover highlight"; mccore FUN_1804df930 ->
   mcgeomsld sld_identify_from_cursor(.., highlight) -> FUN_180099aa0): only while the solid pick mask
   (GetSolidSelectionPickMask, 0 at start-up) is set - get_event with mode bits 0x7000 (FUN_1804e1340) or
   sld_pick_one_ent. Here the one such pick is the Extrude panel's Select Target Body (sld_pick_one_ent with mask 1 =
   bodies, FUN_1800a44c0 checks the Bodies button): the body under the cursor (sld_select along the cursor ray, the
   front one) is the highlighted tag (hit type 1). Plain idle, point entry and chaining keep mask 0: no solid hover. */
int sld_hover_body = -1;
int cui_hover_update(float mx, float my, int active) {
    int sb = ex_mode && ex_pick == 2 ? ex_pick_body(mx, my) : -1, ch = 0;
    if (sb != sld_hover_body) {
        sld_hover_body = sb; overlay_dirty = 1; ch = 1;
        if (getenv("MCAMUI_EXDUMP")) { printf("    sldhover %d\n", sb); fflush(stdout); }   /* tests (solid_compare.sh) */
    }
    if (sld_hover_update(mx, my)) ch = 1;             /* solid chaining: the solid pick mask is set (cui_sldchain.c) */
    int he = active && !sld_mode ? pick_entity(mx, my) : -1;   /* no wireframe pick in the solid selection */
    if (he == hover_ent) return ch;
    hover_ent = he; overlay_dirty = 1; return 1;
}
/* the cursor in the status bar (mcamui.c SDL_MOUSEMOTION): the world point under it on the grid plane; sel_grid_search
   snaps it when Snap to Grid is on: Always, or Near = within half the pick distance (decomp/mastercam_grid.c: dist <
   sel_dist * 0.5, or mode 2) */
int cui_cursor_world(float mx, float my, double world[3], double raw[3]) {
    scene_pick(vp_x, 0, vp_w, vp_h, mx, my - vp_y, world);
    cui_ptr_x = mx; cui_ptr_y = my; cui_ptr_ok = 1;
    pp_hover(world);                                     /* a point pick waits: the live AutoCursor (cui_snap.c) */
    if (raw) { raw[0] = world[0]; raw[1] = world[1]; raw[2] = world[2]; }
    if (!grid.snap) return 0;
    double gx = world[0], gy = world[1]; grid_round(&gx, &gy);
    float sx, sy; int is_near = 1;
    if (grid.snap_mode == 1 && scene_to_screen(vp_w, vp_h, gx, gy, 0, &sx, &sy))
        is_near = hypotf(sx + vp_x - mx, sy + vp_y - my) < SEL_APERTURE_PX * 0.5;
    if (is_near) { world[0] = gx; world[1] = gy; return 1; }
    return 0;
}

/* rotation pivot markers (CGraphicsView middle-button rotate, decomp/mastercam_viewmouse.c 510-516, mastercam_axisrot.c 133,
   mastercam_viewrot2.c 413; drawn by FUN_18022a1b0 as glBitmaps in the auto-highlight colour): marker 0 (13 x 13 at
   0x18087d918, raster x-6 / y-6) at the pivot's screen point while rotating; marker 1 (29 x 27 at 0x18087d940,
   x-15 / y-13) only once a Ctrl / X / Y / Z axis rotation has moved, hidden again when the axis mode changes */
int piv_on, piv_axis_on, piv_axm; float piv_sx, piv_sy;
const char *PIV_BM0[13] = { "......#......", "......#......", "....#####....", "...#..#..#...", "..#...#...#..", "..#...#...#..",
    "#############", "..#...#...#..", "..#...#...#..", "...#..#..#...", "....#####....", "......#......", "......#......" };
const char *PIV_BM1[27] = { "............#######..........", ".........#############.......", "........###############......",
    ".......#####.......######....", ".....#####...........#####...", ".....####.............####...", "....####...............####..",
    "...####.................####.", "...###...................###.", "...###...................###.", "#########.................###",
    ".#######..................###", "..#####...................###", "...###....................###", "....#.....................###",
    "..........................###", "..........................###", ".........................###.", ".........................###.",
    "........................####.", ".....#######...........####..", ".....######...........####...", ".....#####...........####....",
    ".....#######.......######....", ".....##################......", ".....##...############.......", ".....#......#######.........." };
/* the middle-button rotation start (CGraphicsView, decomp/mastercam_viewmouse.c): the pivot = the picked geometry
   under the cursor, else the screen centre at the mean displayed depth (FUN_18013dbd0); turntable mode (config 33002
   off) starts its reference; marker 0 shows at the pivot's screen point */
void cui_rotate_begin(float mx, float my, double pivot[3], int dyn_free) {
    if (!pick_point_under(mx, my, pivot)) view_to_world3(vcx, vcy, view_mean_depth(), pivot);   /* centre at FUN_18013dbd0 depth */
    view_axis_rotate_begin(pivot);
    if (!dyn_free) view_rotate_begin();
    float sx, sy; if (scene_to_screen(vp_w, vp_h, pivot[0], pivot[1], pivot[2], &sx, &sy)) { piv_sx = sx + vp_x; piv_sy = sy + vp_y; piv_on = 1; piv_axis_on = 0; piv_axm = 0; }
}
/* RotateObject (free mode): Ctrl / X / Y / Z hold one axis (decomp/mastercam_axisrot.c FUN_1801cf1c0); marker 1 once an
   axis rotation has moved, hidden again when the axis mode changes */
void cui_rotate_free_step(int axm, int lx, int ly_up, int dx, int dy_up, const double pivot[3]) {
    if (axm != piv_axm) { piv_axis_on = 0; piv_axm = axm; }
    if (!view_axis_rotate(axm, lx, ly_up, pivot)) view_rotate_free(dx, dy_up, pivot);
    else piv_axis_on = 1;
}

/* ---- Operations Manager multi-selection (COpMgrTree::Selchanged, ../formats/opmanager_menu.md): a plain click =
   SelectOperation(id, toggle, clear the others); Ctrl+click the same without clearing (toggles the op); Shift+click =
   SelectOperationRange(anchor, id) - clears the others unless Ctrl is also down. op_msel holds the set while sel (the
   anchor = last clicked op) is unchanged; anything else that sets sel makes it a single selection again. ---- */
int op_selected(int i) {
    if (i < 0 || i >= F.nops) return 0;
    if (sel >= 0 && msel_owner == sel && i < 1024) return op_msel[i];
    return sel == i;
}
int op_nselected(void) { int n = 0; for (int i = 0; i < F.nops; i++) n += op_selected(i); return n; }
void op_click(int i, int ctrl, int shift) {    /* ctrl / shift = the keys held at the click */
    if (i >= 1024) { sel = i; return; }
    if (msel_owner != sel || sel < 0) { memset(op_msel, 0, sizeof op_msel); if (sel >= 0 && sel < 1024) op_msel[sel] = 1; }
    if (shift && sel >= 0 && sel < 1024) {
        int a = sel < i ? sel : i, b = sel < i ? i : sel;
        if (!ctrl) memset(op_msel, 0, sizeof op_msel);
        for (int k = a; k <= b; k++) op_msel[k] = 1;
        msel_owner = sel;                                     /* the anchor stays */
        return;
    }
    if (!ctrl) memset(op_msel, 0, sizeof op_msel);
    op_msel[i] = ctrl ? !op_msel[i] : 1;
    sel = i; msel_owner = i;
}

/* Hot keys are Mastercam's: read from the user's mastercam.kmp (KEYMAP Mastercam <function> "<keys>"), else these
   defaults (identical to Mastercam 2017's shipped mastercam.kmp). Each Mastercam function maps to an action here. */
static const char *KMP_DEFAULT[][2] = {
    { "SelectAllMask", "Ctrl+A" }, { "OnScreenFit", "Alt+F1" }, { "OnScreenZoom", "F1" }, { "OnViewIsometric", "Alt+7" },
    { "OnScreenRepaint", "F3" }, { "OnScreenRegenerate", "Shift+Ctrl+R" }, { "OnEditCut", "Ctrl+X" }, { "OnEditCopy", "Ctrl+C" },
    { "OnEditPaste", "Ctrl+V" }, { "ViewOpManager", "Alt+O" }, { "UndoEvent", "Ctrl+Z" }, { "RedoEvent", "Ctrl+Y" },
    { "OnShadeToggle", "Alt+S" }, { "set_main_from_entity", "Alt+X" }, { "OnHide", "Alt+E" }, { "OnViewPreviousView", "Alt+P" },
    { "AnalyzeStart", "F4" }, { "OnDisplayInfo", "F9" }, { "OnWCSAxesToggle", "Alt+F9" }, { "delete_entities", "F5" },
    { "doDraft_GLOBALS", "Alt+D" }, { "OnFileExit", "Alt+F4" }, { "run_user_app", "Alt+C" }, { "AutoSaveParams", "Alt+A" },
    { "OnHelp", "Alt+H" }, { "OnScreenGrid", "Alt+G" }, { "ConfigureX", "Alt+F8" }, { "OnScreenUnzoom", "Alt+F2" },
    { "OnScreenLevels", "Alt+Z" }, { "OnAppAbout", "Alt+V" }, { "OnHideToolpath", "Alt+T" }, { "OnViewFront", "Alt+2" },
    { "SbSelectRotationPosition", "Alt+F12" }, { "OnViewTop", "Alt+1" }, { "OnViewRight", "Alt+5" }, { "OnViewLeft", "Alt+6" },
    { "OnViewBack", "Alt+3" }, { "OnViewBottom", "Alt+4" }, { "OnScreenUnzoomPrevious", "F2" }, { "OnFileNew", "Ctrl+N" },
    { "OnFileOpen", "Ctrl+O" }, { "OnFilePrint", "Ctrl+P" }, { "OnFileSave", "Ctrl+S" }, { "OnFileSaveAs", "Shift+Ctrl+S" },
    { "OnShadeTranslucentToggle", "Ctrl+T" }, { "ViewSolidsManager", "Alt+I" }, { "ViewPlanesManager", "Alt+L" },
    { "ToggleMultiThreadDialog", "Alt+M" }, { "ViewArtManager", "Alt+B" } };
CuiKey cui_kmap[128]; int cui_nkmap; char kmap_src[600] = "built-in defaults";
static void kmap_add(const char *fn, const char *keys) {
    if (cui_nkmap >= 128) return;
    CuiKey *b = &cui_kmap[cui_nkmap++]; snprintf(b->fn, sizeof b->fn, "%s", fn); snprintf(b->keys, sizeof b->keys, "%s", keys);
}
/* mcamui.c kmap_load: the user's CONFIG/mastercam.kmp (paths.conf "user"), else the defaults above; the front end
   parses the key names (and drops the ones it cannot bind) */
void cui_kmap_load(void) {
    static const char *paths[] = { "CONFIG/mastercam.kmp", NULL };   /* in paths.conf "user" */
    cui_nkmap = 0;
    for (int i = 0; paths[i] && !cui_nkmap; i++) {
        char sp[1100]; FILE *f = fopen(site_file(sp, sizeof sp, "user", paths[i]), "r"); if (!f) continue;
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char fn[64], keys[64];
            if (sscanf(line, "KEYMAP Mastercam %63s \"%63[^\"]\"", fn, keys) == 2) kmap_add(fn, keys);
        }
        fclose(f);
        if (cui_nkmap) snprintf(kmap_src, sizeof kmap_src, "%s", sp);
    }
    if (!cui_nkmap) cui_kmap_load_defaults();
}
void cui_kmap_load_defaults(void) {
    cui_nkmap = 0; snprintf(kmap_src, sizeof kmap_src, "built-in defaults");
    for (size_t i = 0; i < sizeof KMP_DEFAULT / sizeof KMP_DEFAULT[0]; i++) kmap_add(KMP_DEFAULT[i][0], KMP_DEFAULT[i][1]);
}

/* graphics-window right-click menu: the "Right mouse button menu" dropdown of the user's CONFIG/default.mtb
   (FUNCTION Mastercam <entry> / SEPARATOR / DDCOMMAND n = the dropdown "DROPDOWN START n" as a submenu); item text =
   the command's menu string (<ui_dir>/cmd_labels.tsv, tools/gen_cmd_labels.py; missing = the FT function name). The 2017 mini toolbar shown with it is NOT DECODED and not drawn. */
RmbDrop rmb_drop[8]; int nrmb_drop, rmb_main = -1;
void rmb_load(void) {
    static const char *paths[] = { "CONFIG/default.mtb", NULL };   /* in paths.conf "user" */
    for (int p = 0; paths[p]; p++) {
        char sp[1100]; FILE *f = fopen(site_file(sp, sizeof sp, "user", paths[p]), "r"); if (!f) continue;
        char line[256]; RmbDrop *d = NULL;
        while (fgets(line, sizeof line, f)) {
            int idx; char name[64], fn[64];
            if (sscanf(line, "DROPDOWN START %d \"%63[^\"]\"", &idx, name) == 2 && nrmb_drop < 8) {
                d = &rmb_drop[nrmb_drop++]; memset(d, 0, sizeof *d); d->idx = idx; CP(d->name, name);
                if (!strcmp(name, "Right mouse button menu")) rmb_main = nrmb_drop - 1;
            } else if (!strncmp(line, "DROPDOWN END", 12)) d = NULL;
            else if (d && d->n < 32 && sscanf(line, "FUNCTION Mastercam %63s", fn) == 1) { d->it[d->n].kind = 0; CP(d->it[d->n].fn, fn); d->n++; }
            else if (d && d->n < 32 && !strncmp(line, "SEPARATOR", 9)) d->it[d->n++].kind = 1;
            else if (d && d->n < 32 && sscanf(line, "DDCOMMAND %d", &idx) == 1) { d->it[d->n].kind = 2; d->it[d->n].sub = idx; d->n++; }
        }
        fclose(f); return;
    }
}
int rmb_find(int idx) { for (int i = 0; i < nrmb_drop; i++) if (rmb_drop[i].idx == idx) return i; return -1; }

void hide_toggle(void) {                         /* Alt+E: show only the selected entities / show all again */
    if (any_hidden) {
        for (int i = 0; i < F.nents && i < nent_hidden; i++) if (ent_hidden[i] && F.ents[i].type) ent_on(i, F.ents[i].type);
        any_hidden = 0; scene_reupload(); overlay_dirty = 1; cam_msg[0] = 0; return;
    }
    if (!nselected) { snprintf(cam_msg, sizeof cam_msg, "Select entities to keep on screen"); return; }
    ent_hidden = realloc(ent_hidden, F.nents ? F.nents : 1); nent_hidden = F.nents;
    for (int i = 0; i < F.nents; i++) {
        ent_hidden[i] = F.ents[i].type && !(ent_sel && i < ent_sel_n && ent_sel[i]);
        if (ent_hidden[i]) { int t = F.ents[i].type; ent_off(i); F.ents[i].type = t; }   /* off screen, not deleted */
    }
    any_hidden = 1; scene_reupload(); overlay_dirty = 1; cam_msg[0] = 0;
}

/* ------------------------------------------------------------------------------------------------ config items */
/* mcamui.c screen_colors_load, the items of the moved features (key -> value text after '?') */
void cui_config_item(int key, const char *q) {
    char yn = 0; sscanf(q, " %c", &yn);
    cui_chain_config_item(key, q);                         /* the chaining items 6000-6026, 1000, 1002 (cui_chain.c) */
    xf_config_item(key, q);                                /* 5010 / 5031 / 5044 / 10026 (cui_xform.c) */
    cui_rpt_config_item(key, q);                           /* the report templates 31020 / 31011 / 31017 / 31006 / 31007 (cui_toolmgr.c) */
    if (key == 1000 || (key >= 19000 && key <= 19040)) bp_opt_cfg(&bp_opt, key, q);   /* [BACKPLOT] backplot.md 12.1 / 13.1 (FUN_1801b7980; cui_backplot.c, phase 8) */
    if (key == 16013) undo_lim_on = yn == 'y';
    else if (key == 4053) cfg_stipple_hl = yn == 'y';     /* "Use stipple on solids/surfaces?" (StippleHighlight) */
    else if (key == 4056) cfg_dashed_wire = yn == 'y';     /* "Use dashed on wireframe?" (GetDashedWireHightlight) */
    else if (key == 16009) cfg_constr_3d = yn == 'y';      /* constr_view_is_3d (mccore FUN_1801ba870 case 0x3e89; cui_stock.c) */
    int v; if (sscanf(q, "%d", &v) != 1) return;
    if (key == 16014) undo_lim_n = v;
    if (key == 29003) cfg_places = v;                      /* places after decimal for analyze (n_places_an; Tool Manager cells) */
    if (key == 5033) cfg_select_color = v; else if (key == 5007) cfg_back_color = v;
    if (key == 5034) cfg_sld_edge_color = v; else if (key == 5035) cfg_sld_face_color = v;   /* sldEdge / sldFace_select_color (mccore FUN_1801d3400: 0x13aa / 0x13ab) */
    if (key == 5029) cfg_stock_color = v;                  /* Mill / Router - Stock color = mill_stock_color (mccore FUN_1801d3400: 0x13a5) */
    if (key == 5000) cfg_autohl_color = v;                 /* Auto-highlight color (auto_highlight_color) */
    if (key == 4036) cfg_autohl = v;                       /* Auto-highlight on/off (Screen page 3622) */
    if (key == 4001) cfg_presel = v;                       /* Allow pre-selection (Screen page 2053) */
    if (key == 5026) cfg_live_color = v;                   /* Live entity color (live_entity_color, sktcher_live_color) */
    if (key == 10002 && v >= 1 && v <= 8) cfg_pt_style = v;   /* main attribute point style: "Point style?" */
    if (key == 10000 && v >= 1 && v <= 5) cfg_ln_style = v;   /* "Line style?" (main_attrib +8) */
    if (key == 10001 && v >= 1) cfg_ln_width = v;          /* "Line width?" (main_attrib +0, a double; the combo's widths are whole) */
}
/* undo depth: config 16013 "Limit the number of Undo events?" (n = unlimited) / 16014 "Number of events?". The system
   tolerances (config 1000-1008, 7004) are read by the core (mc_systol_units, mcam_saveops.c) from the current configuration:
   the first read = startup (defaults first), a read after a units change = ChangeConfiguration's (formats/units.md 8) */
void cui_config_done(void) { undo_limit = undo_lim_on ? undo_lim_n : 0; mc_systol_units(mc_units_metric); }
