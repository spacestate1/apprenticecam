/* cui_chain.c - the chaining state machine (mccore CChainDlg / run_chain_dlg, chain_it) shared by both front ends:
 * the chains being picked (cchains), the dialog's mode and buttons, Window / Polygon / Area / Vector / Point / Partial /
 * Dynamic picking, Last, the Chaining Options model (dialog 405), the Drill Point Selection actions (2300), the chain
 * direction arrows, the script steps. Moved out of viewer/mcamui.c and viewer/chaindlg_ui.inc (formats/qt_port_plan.md
 * phase 4) with their mcamui names and decomp citations; formats/chaining.md has the rules. The chaining core itself
 * (cam_chain*, Chain) is viewer/cam2d.c. What a front end does with the result (the op dialog, the Extrude panel, the
 * Unselect All question, the viewport) goes through cui_hooks. */
#include "core_ui.h"
#include "cui_opfields.h"
#include "mcpartline.h"
#include "mcsurf.h"
#include "mcsurf_nrm.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <strings.h>

/* ------------------------------------------------------------------------------------------------ state */
int cam_mode;                                   /* CAM_* while selecting chains / points, 0 otherwise */
Chain cchains[256]; int ncchains;
Hole choles[2048]; int ncholes;
int show_params;                                /* the op dialog is up (its model moves with phase 6) */
int cam_variant;                                /* contour type picked from the gallery: 1 chamfer, 2 engrave */
int ex_mode, ex_phase;                          /* the Extrude command (formats/extrude.md); its panel is up when both are set (the command moves with phase 5) */
int chain_single, chain_win_new;   /* chain_single: 0 Chain, 1 Single, 2 Partial, 3 Window, 4 Area, 5 Polygon, 6 Vector, 7 Point (dialog modes 1 / 2 / 7 / 5 / 4 / 6 / 8 / 9) */
int partial_e1 = -1; double partial_px, partial_py;   /* Partial: first entity and its pick point */
/* Window chaining: mode combo (DAT_1812d3084, default 1 = Inside; mccoreres 4224-4228), config 6000 Zigzag, 6001 closed
   chain direction (1 = CCW), 6012 / 6013 open / closed masks, 6024 prompt for the window start point */
int win_sel_mode = 1, cfg_chain_zigzag = 1, cfg_chain_dir = 1, cfg_chain_open = 1, cfg_chain_closed = 1, cfg_win_prompt = 1, cfg_chain_infinite = 0;
int win_pending; char *win_sel;                 /* window drawn, waiting for the start point (4235) */
int cfg_chain_cursor = 1, show_chain_opts;      /* config 6002 Use cursor position; Chaining Options dialog open */
int cfg_chain_mode = 1;                         /* config 6014 Default chaining mode (dialog mode number) */
int dyn_state, dyn_end;                         /* Dynamic: 1 = pick the arrow (4229), 2 = pick the new position */
int chain_wait, partial_waiting;                /* Wait (control 540, config 6023 / chain_wait_at_end); a partial chain kept open */
Chain last_chains[256]; int nlast_chains, last_used;   /* Last: the previous chaining's chains */
/* CChainDlg state (mccore, decomp/mccore_chain_range.c): chain_dlg_exp = +0x1f2b5 expanded (template 1058) / collapsed
   (6464), kept in DAT_1813c4bb0 between dialogs (bss: collapsed at the first chaining, FUN_1801ec0c0 saves it);
   chain_last_mode = DAT_1813c4bb4 (dialog mode at the end of run_chain_dlg; used when config 6020 "Default to last
   mode used" = y); config 6021 = DAT_1813c4ca9, the C-plane radio's default (y = C-plane), 6023 = DAT_1813c4caa, Wait's
   default - both reloaded at every dialog when 6020 = n (CChainDlg ctor FUN_1801ead40); win_group_n = +0x1f0aa chains
   added by the last Window / Polygon selection while +0x1f0ac is set (Unselect removes them all, FUN_1801ee030) */
int chain_dlg_exp, chain_last_mode, cfg_chain_last_mode, cfg_chain_plane_def, cfg_chain_wait_def, win_group_n;
/* Chaining Options (dialog 405, CChainOptions OK FUN_1801f9680) values this program keeps without a ported effect:
   chain_use_ent_mask 6025, DAT_1813c4ca6 "Break closest entity to thread point" 6010, chain_dynamic_break 6007, chain_section_stop_angle_on 6017 / chain_section_stop_angle 6011 (degrees; used on
   surface edges only, build_edgecrv_on_surf), g_chain_sync_mode 6008; and with an effect: DAT_1813c4ca8 Reverse inner
   chains 6005, nested_chain_sort_mode 6006, chain_surface_edges_in_single_mode 6026 (cfg_surf_single: surfaces in Single mode,
   chain_srf_pick below) */
int cfg_ent_mask, cfg_break_thread, cfg_surf_single, cfg_dyn_break, cfg_sect_on, cfg_sync_mode, cfg_rev_inner, cfg_sort_mode;
double cfg_sect_ang = 30;
char chain_prompt[160];                         /* the op's chaining prompt; shown as "%s %d" with the next chain number */
int poly_n; float poly_x[256], poly_y[256];     /* Polygon chaining: screen vertices */
int last_clicks;                                /* click count of the last left click (double-click ends a polygon) */

static const char *ms_pt_prompt(void);
static int ms_escape(void);
static void ms_off(void);
static void ms_begin(int t);
static void vp_main(void) { if (cui_hooks.view_port) cui_hooks.view_port(); }   /* mcamui vp_main / the Qt view's setViewport */

void cam_clear_selection(void) {
    for (int i = 0; i < ncchains; i++) cam_chain_free(&cchains[i]);
    ncchains = 0; ncholes = 0; overlay_dirty = 1;
}

void chain_prompt_upd(void) {
    /* run_chain_dlg: "%s %d" (prompt, chain number); per mode: Area 4234 "Select area"; Polygon appends 9904; Partial
       4270 "Select the first entity" / 4271 "Select the last entity" */
    if (sld_mode) { sld_prompt(); return; }          /* Solids mode: the solid selection's prompt (cui_sldchain.c) */
    if (plg_picking()) { snprintf(cam_msg, sizeof cam_msg, "%s", ms_pt_prompt()); return; }   /* the 3D ops' point picks after the op dialog (msurf flow below) */
    if (!chain_prompt[0]) { cam_msg[0] = 0; return; }
    if (chain_single == 4) { snprintf(cam_msg, sizeof cam_msg, "Select area"); return; }
    if (chain_single == 2 && partial_waiting) { snprintf(cam_msg, sizeof cam_msg, "Branch point reached.  Select next branch."); return; }   /* state 3: 4269 */
    if (chain_single == 2) { snprintf(cam_msg, sizeof cam_msg, partial_e1 < 0 ? "Select the first entity" : "Select the last entity"); return; }
    if (win_pending) { snprintf(cam_msg, sizeof cam_msg, "Sketch approximate start point"); return; }
    if (dyn_state) { snprintf(cam_msg, sizeof cam_msg, "Select a chain arrow to move"); return; }   /* FUN_1801ec820 point_proc 0x1085 */
    if (chain_single == 0 && ncchains && cchains[ncchains - 1].branch) { snprintf(cam_msg, sizeof cam_msg, "Branch point reached.  Select next branch."); return; }   /* 4269 */
    int nwf = 0; for (int i = 0; i < ncchains; i++) if (!cchains[i].sld) nwf++;   /* number_of_chains: the dialog's CHAINs (solid chains come after chain_it) */
    snprintf(cam_msg, sizeof cam_msg, "%s %d%s", chain_prompt, nwf + 1, chain_single == 5 ? "\nSketch polygon for selection\nDouble-click or Enter to end polygon" : "");
}
/* CChainDlg constructor (FUN_1801ead40, decomp/mccore_chain_range.c l.14466-14503): the mode = DAT_1813c4bb4 when config
   6020 is on and a mode was used, else config 6014 (chain_mode_default); with 6020 off the C-plane / 3D radio
   (chain_plane_filter = 6021) and Wait (chain_wait_at_end = 6023) start from the config each time */
void chain_dlg_init(void) {
    static const int m2o[10] = { 0, 0, 1, 0, 4, 3, 5, 2, 6, 7 };   /* dialog mode 1 Chain 2 Single 4 Area 5 Window 6 Polygon 7 Partial 8 Vector 9 Point */
    int m = chain_last_mode && cfg_chain_last_mode ? chain_last_mode : cfg_chain_mode;
    chain_single = m >= 0 && m < 10 ? m2o[m] : 0;
    if (!cfg_chain_last_mode) { cam_chain_plane_filter = cfg_chain_plane_def; chain_wait = cfg_chain_wait_def; }
    win_group_n = 0; poly_n = 0;
    sld_reset();                                     /* run_chain_dlg: +0x17f8 from DAT_1813c4bb2 when solids may be chained */
}
/* run_chain_dlg end (l.18160-18186): the mode kept in DAT_1813c4bb4 for the next dialog */
void chain_dlg_done(void) {
    static const int o2m[8] = { 1, 2, 7, 5, 4, 6, 8, 9 };
    chain_last_mode = chain_single >= 0 && chain_single < 8 ? o2m[chain_single] : 1;
}
/* ================================================================================================ 3D surface ops (S4)
   A new 3D surface op is made as mcmsurf's msurf_assoc (decomp/mcmsurf_all.c l.15838-16263) makes it: formats/surface_selection.md
   1.3 (the order), 1.4 (FUN_18001bcf0 "select_surf_assoc": drive / check), 1.5 (a click), 1.6 (containment); the solid selection is
   mcgeomsld's (decomp/surfsel/mcgeomsld_surfsel*.c: S = surfsel.c, I = internals, A = assign). l.N below = decomp/mcmsurf_all.c.
   Ours / not decoded, each marked where it is: the solid hit test (decision N13-3: a ray against this program's tessellation of
   each XT face - Mastercam's sld_array_pick is Parasolid), select_ents' own interface (End Selection = Enter, Esc; the order between
   a surface and a solid under the cursor), the Selection Bar's Body / Face starting state (N13-8: Body on, Face off), the
   "Toolpath/surface selection" dialog (stage S5: a hook below), op 0x84's FUN_180009270 box centre, the CAD file pick (FUN_18001b8a0),
   the project points pick (select_surf_points mask 1), flowline ops (no port type), the highlight of picked solids. */

/* Configuration - Toolpaths, group 3014 "Toolpath/surface selection" (mccoreres dialog 319, decomp/surfsel/mccore_cfg319_*.c): config
   items 17017 "CAD file?" (y / n) -> msurf_mach_stl, 17018 "Containment?" -> msurf_toolcont, 17019 "Toolpath/surface selection?" ->
   msurf_all_geom = check x 10 + drive, 17023 "Show toolpath/surface selection dialog?" -> msurf_show_selection_dlg (mccore's config
   setter, decomp/units/systol_readers.c l.1130-1153: keys 0x4279 / 0x427a / 0x427b / 0x427f). Defaults = mccore's reset
   (decomp/mccore_tooledit.c l.11977-11982): 1 (Drive Prompt, Check None), 0, 0, 1 (decision N13-5). */
int cfg_ms_all_geom = 1, cfg_ms_toolcont = 0, cfg_ms_mach_stl = 0, cfg_ms_show_dlg = 1;
int ms_phase, drv_pick, plg_pick;
/* the Selection Bar's solid buttons (mcgeomsld OnSolidsSelectBody @1800d1260 / OnSolidsSelectFace @1800d13a0: each toggles its own
   bit of the solid pick mask, SetSolidSelectionBodiesPickMask / SetSolidSelectionFacesPickMask(!checked) - two independent
   switches, no radio). Their state when the 3D toolpath selection starts is NOT DECODED: decision N13-8 (SS-6) = whole bodies, so
   ms_begin sets Body on, Face off */
int ms_sld_body = 1, ms_sld_faces;
int cui_script_radio = 2;                         /* tests: dialog 100's answer (bosscav:N); 2 = as opened (Undefined) */
static int ms_cam;                                /* the op type the flow makes */
static int ms_drive, ms_check, ms_boss;           /* msurf_all_geom % 10, its check mode (l.19850-19862), dialog 100's answer 1 / 2 / 3 */
static int ms_shown241;                           /* DAT_1804e3758: message 241 shown in this session (l.16232-16240) */
static int ms_post_step, ms_pt_kind;              /* the picks after the op dialog: the next step; the point pick's kind */
static CamRef ms_ref[CAM_REF_MAX]; static int ms_nref;          /* the op's kind 4 records as FUN_18001bcf0 stores them */
static CamRefFace ms_rface[CAM_REF_FACE_MAX]; static int ms_nrface;
static CamParams ms_seed; static int ms_seed_tool;              /* the op as its dialog opened (Cancel + Yes keeps it) */
/* mcgeomsld's solid selection (sld_init_solid_surfs_selection S:404 clears it): the body list DAT_1803e81d8 / types 0x1803e8318
   and the face list DAT_1803e8200 / types 0x1803e8340 - here one list, face -1 = a body; the type of new picks
   (sld_set_toolp_srf_type S:1010, DAT_1803e80e0: 0 drive, 4 check); the solids' selection bit 4 (rewrite_ent of SOLID_ID) */
typedef struct { int s, f, type; } MsPick;
static MsPick *ms_pk; static int ms_npk, ms_pkcap, ms_ptype;
static unsigned char *ms_bsel; static int ms_nbsel;

int ms_live(void) { return ms_phase != MS_OFF && cam_mode && cam_mode == ms_cam; }
static void ms_off(void) { ms_phase = MS_OFF; drv_pick = 0; plg_pick = 0; ms_npk = 0; ms_nref = 0; ms_nrface = 0; }
/* the Mastercam opcode of a port type (formats/surface_selection.md 3) */
static int ms_opcode(int t) {
    switch (t) {
    case CAM_SRPARALLEL: return 5; case CAM_SRPROJECT: return 7; case CAM_SRPOCKET: return 10; case CAM_SFPARALLEL: return 0xb;
    case CAM_SFRADIAL: return 0xc; case CAM_SFPROJECT: return 0xd; case CAM_SFCONTOUR: return 0xf; case CAM_SRPLUNGE: return 0x2c;
    case CAM_BLEND: return 0x6d; case CAM_SRMPOCKET: return 0x83; case CAM_SFPENCIL: return 0x27;
    }
    return t == CAM_OPTIROUGH || cam_hst_style(t) >= 0 ? 0x84 : 0;
}
/* msurf_op_has_tcbs (mccore @1800c4850, DumpDecomp of ghidra_core 2026-11-12): the ops that take a containment boundary */
static int ms_has_tcbs(int oc) {
    switch (oc) { case 5: case 6: case 9: case 10: case 0xb: case 0xc: case 0xf: case 0x27: case 0x28: case 0x29: case 0x2a: case 0x2b:
                  case 0x6b: case 0x83: case 0x84: return 1; }
    return 0;
}
static void ms_prompt(void) {
    if (drv_pick) snprintf(cam_msg, sizeof cam_msg, "%s", drv_pick == 1 ? "Select Drive Surfaces" : "Select Check Surfaces");   /* mcmsurfres 406 / 407 */
}
static void ms_bits(void) { if (ms_nbsel != F.nsolids || !ms_bsel) { free(ms_bsel); ms_bsel = calloc(F.nsolids ? F.nsolids : 1, 1); ms_nbsel = F.nsolids; } }
/* set_clr_sel_bits_all(0, 4, 0, 4, 0x1f1fff, ..): every selection bit 4 off - the surfaces' (the general selection, ent_sel) and the solids' */
static void ms_clear_sel(void) {
    if (ent_sel) memset(ent_sel, 0, ent_sel_n);
    nselected = 0;
    ms_bits(); memset(ms_bsel, 0, ms_nbsel);
    overlay_dirty = 1;
}
static int ms_pk_find(int s, int f) { for (int i = 0; i < ms_npk; i++) if (ms_pk[i].s == s && ms_pk[i].f == f) return i; return -1; }
static void ms_pk_add(int s, int f, int type) {
    if (ms_npk == ms_pkcap) { ms_pkcap = ms_pkcap ? 2 * ms_pkcap : 64; ms_pk = realloc(ms_pk, sizeof *ms_pk * ms_pkcap); }
    ms_pk[ms_npk].s = s; ms_pk[ms_npk].f = f; ms_pk[ms_npk].type = type; ms_npk++;
}
/* FUN_1800cd8a0 (I:108): an entry belongs to the current type's set - type 0: entries of type 0; else entries with that bit */
static int ms_of_type(int type, int t) { return t == 0 ? type == 0 : (type & t) != 0; }
/* FUN_1800af580 (I:1) merging one entry back: a known entry gets the type ORed in (set when the type is 0 or REPLACE), a new one is appended */
static void ms_merge(int s, int f, int t, int replace) {
    int k = ms_pk_find(s, f);
    if (k < 0) { ms_pk_add(s, f, t); return; }
    if (t == 0 || replace) ms_pk[k].type = t; else ms_pk[k].type |= t;
}
/* sld_visible, not blanked, a body (S:528 / 597 / 832): here a part solid with a body on a visible level (ours: sld_visible's own test
   NOT DECODED, this program's display rule) */
static int ms_solid_visible(int s) { return s >= 0 && s < F.nsolids && (F.solids[s].xt || F.solids[s].brep) && mcam_level_visible(&F, F.solids[s].level); }
/* one solid pick (sld_sel_solid_surfs S:914): the current type's entries are taken out (FUN_1800cd8a0), the pick toggles the entry in them
   (FUN_1800a92f0 I:1098: an entry already there is removed, else appended), the solids of the remaining bodies get selection bit 4,
   everything is merged back (FUN_1800af580). Shift + face (the tangent faces, sld_array_pick A:687) NOT PORTED */
static void ms_pick_solid(int s, int f) {
    ms_bits();
    int k = ms_pk_find(s, f);
    if (k >= 0 && ms_of_type(ms_pk[k].type, ms_ptype)) { memmove(ms_pk + k, ms_pk + k + 1, sizeof *ms_pk * (ms_npk - k - 1)); ms_npk--; }
    else ms_merge(s, f, ms_ptype, 0);
    memset(ms_bsel, 0, ms_nbsel);                  /* set_clr_sel_bits_all(.., 0x10000, ..), then bit 4 on the current type's bodies */
    for (int i = 0; i < ms_npk; i++) if (ms_pk[i].f < 0 && ms_of_type(ms_pk[i].type, ms_ptype) && ms_pk[i].s < ms_nbsel) ms_bsel[ms_pk[i].s] = 1;
    overlay_dirty = 1;
}
static void ms_prompt(void);
void drv_toggle(int si) { if (drv_pick && ms_live() && si >= 0 && si < F.nsolids) { ms_pick_solid(si, -1); ms_prompt(); } }
void drv_toggle_face(int si, int f) {
    if (!drv_pick || !ms_live() || si < 0 || si >= F.nsolids || f < 0) return;
    if (f >= sld_part_nface(si)) { snprintf(cam_msg, sizeof cam_msg, "solid %d has no face %d", si + 1, f); return; }
    ms_pick_solid(si, f); ms_prompt();
}
void drv_toggle_surf(int k) {                     /* select_ents: the surface's selection bit 4 (the general selection) */
    if (!drv_pick || !ms_live() || k < 0 || k >= F.nsurfs) return;
    int e = F.surfs[k].ent; if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) return;
    sel_toggle(e); ms_prompt();
}
void drv_click(float mx, float my) {
    if (!drv_pick || !ms_live()) return;
    /* ours: select_ents' order between the general selection and the solid selection is NOT DECODED - a surface within the
       aperture first (select_ent_univ's rule, cui_sel.c), else a part solid under the cursor */
    int e = pick_entity_surf(mx, my);
    if (e >= 0) { sel_toggle(e); ms_prompt(); return; }
    /* the pick mask (sld_pick_one_ent mcgeomsld @1800c2560: the allowed kinds less the Selection Bar buttons not checked, bit 1 body,
       4 face) as sld_identify_from_cursor (@1800bd010) applies it: mask 0 -> no solid hit; a face hit stays a face with bit 4, else
       it becomes its body (LAB_1800bd1c3) */
    if (!ms_sld_body && !ms_sld_faces) return;
    if (ms_sld_faces) {                           /* a face: OUR hit test (N13-3), the ray's front face of this program's XT face tessellation */
        int si, fi; double hp[3]; int k = sld_pick_part_face(mx, my, &si, &fi, hp);
        if (k == 2) { snprintf(cam_msg, sizeof cam_msg, "A solid made in this session: its faces are NOT PORTED (only the part's solids)"); return; }
        if (k == 1) drv_toggle_face(si, fi);
        return;
    }
    int si = sld_pick_part_solid(mx, my);         /* a body: the same ray (ours) */
    if (si >= 0) drv_toggle(si);
}
/* sld_sel_selected_solid_surfs(true, false) (S:832): the current type's bodies taken out and dropped, every visible solid with
   selection bit 4 added as a body, the faces kept, merged back */
static void ms_sel_selected_solids(void) {
    ms_bits();
    MsPick *keep = malloc(sizeof *keep * (ms_npk + 1)); int nk = 0, n = 0;
    for (int i = 0; i < ms_npk; i++) {
        if (!ms_of_type(ms_pk[i].type, ms_ptype)) ms_pk[n++] = ms_pk[i];
        else if (ms_pk[i].f >= 0) keep[nk++] = ms_pk[i];
    }
    ms_npk = n;
    for (int s = 0; s < ms_nbsel; s++) if (ms_bsel[s] && ms_solid_visible(s)) ms_merge(s, -1, ms_ptype, 0);
    for (int i = 0; i < nk; i++) ms_merge(keep[i].s, keep[i].f, ms_ptype, 0);
    free(keep);
}
/* FUN_18001b530 (l.19303): selection bit 4 on every SURF_ID / PMESH_ID without the blank bit 0x100 (get_raw_ent mask 0x100100; this
   program has no mesh entities). Ours: also not on a hidden level / hidden here (get_raw_ent's first argument NOT DECODED) */
static void ms_sel_all_surfs(void) {
    ms_clear_sel(); sel_grow();
    for (int k = 0; k < F.nsurfs; k++) { int e = F.surfs[k].ent; if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF || F.surfs[k].blanked || F.surfs[k].off || !ENT_LV_SHOWN(&F.ents[e])) continue;
        if (!ent_sel[e]) { ent_sel[e] = 1; nselected++; } }
}
/* sld_sel_all_solid_surfs (S:528): every visible solid as a body of the current type */
static void ms_sel_all_solids(void) { for (int s = 0; s < F.nsolids; s++) if (ms_solid_visible(s)) ms_merge(s, -1, ms_ptype, 0); }
/* sld_sel_all_unselected_solid_surfs (S:597): of the bodies with picked faces, the faces not picked; every visible solid that is in
   neither list; merged with the current type */
static void ms_sel_unselected_solids(void) {
    int n0 = ms_npk;
    for (int s = 0; s < F.nsolids; s++) {
        int hasf = 0, hasb = 0;
        for (int i = 0; i < n0; i++) if (ms_pk[i].s == s) { if (ms_pk[i].f >= 0) hasf = 1; else hasb = 1; }
        if (hasf) { int nf = sld_part_nface(s); for (int f = 0; f < nf; f++) if (ms_pk_find(s, f) < 0) ms_merge(s, f, ms_ptype, 0); }
        else if (!hasb && ms_solid_visible(s)) ms_merge(s, -1, ms_ptype, 0);
    }
}
/* tp_surf_list (l.22410): a selected surface's record - in database order (get_raw_ent: F.ents order here), its own bits 4 / 0x20
   (0 for a surface no op is being edited on); FLAG = the step's: drive bit 4 cleared (l.19903), check bit 4 set (l.20034) */
static void ms_surf_record(int e, unsigned flag, CamRef *r) {
    memset(r, 0, sizeof *r); r->cls = TP_ECLS_SURF; r->index = F.ents[e].srf; r->idn = F.surfs[r->index].idn; r->flags = flag; r->link = 0;
}
static int ms_surf_selected(int e) { return F.ents[e].type == E_SURF && ent_sel && e < ent_sel_n && ent_sel[e] && F.ents[e].srf >= 0 && F.ents[e].srf < F.nsurfs; }
static void ms_check_begin(void);
static void ms_check_end(void);
static void ms_containment(void);
static void ms_to_dialog(void);
/* the drive step's end (l.19889-19990): the records (the solids: tp_surf_list takes those with an assigned SldToolPOp - none yet),
   the selection cleared, sld_set_toolp_srf_type(4) */
static void ms_drive_end(void) {
    if (ms_drive == 1) ms_sel_selected_solids();
    ms_nref = 0;
    for (int e = 0; e < F.nents && ms_nref < CAM_REF_MAX; e++) if (ms_surf_selected(e)) ms_surf_record(e, 0, &ms_ref[ms_nref++]);
    ms_clear_sel(); ms_ptype = 4;
    ms_check_begin();
}
static void ms_check_begin(void) {
    drv_pick = 0;
    if (ms_check == 2) { if (ms_drive != 2) { ms_sel_all_surfs(); ms_sel_unselected_solids(); } ms_check_end(); return; }   /* Unselected (l.19993-19999) */
    if (ms_check == 1) { drv_pick = 2; ms_prompt(); overlay_dirty = 1; return; }                                            /* Prompt: 407 */
    ms_check_end();
}
/* the check step's end (l.20015-20082): sld_surfs_to_toolp_op (one SldToolPOp per solid with picks, S:1232 - not written here, stage
   S6), tp_surf_list again: new surface records flagged 4; one already in the op (picked as drive) is not added again and, with Check
   Prompt, its drive record becomes a check record (tp_update_ent, l.20027-20058); every solid record stored (l.20066-20080) */
static void ms_check_end(void) {
    if (ms_check == 1) ms_sel_selected_solids();
    int nd = ms_nref;
    for (int e = 0; e < F.nents; e++) {
        if (!ms_surf_selected(e)) continue;
        int dup = -1; for (int d = 0; d < nd; d++) if (ms_ref[d].cls == TP_ECLS_SURF && ms_ref[d].index == F.ents[e].srf) { dup = d; break; }
        if (dup >= 0) { if (ms_check != 2) ms_ref[dup].flags |= 4; continue; }
        if (ms_nref < CAM_REF_MAX) ms_surf_record(e, 4, &ms_ref[ms_nref++]);
    }
    /* the solids (tp_surf_list's sld_assign_toolp loop, l.22560-22600): sld_assign_toolp (A:3) walks the solid list from the last to
       the first. The record: class 0x10000, the solid's idn, link = the new SldToolPOp's id (FUN_18026a080: max + 1) - 0 here: the
       SldToolPOp is not written (stage S6, decision N13-6); flags = the solid's own bits 4 / 0x20: which code sets them is NOT
       DECODED - set to the body type / the faces' common type, as every corpus record has them (surface_selection.md 7.3; mixed: 0) */
    ms_nrface = 0;
    for (int s = F.nsolids - 1; s >= 0; s--) {
        int any = 0, whole = -1, ft = -1, mixed = 0;
        for (int i = 0; i < ms_npk; i++) if (ms_pk[i].s == s) { any = 1; if (ms_pk[i].f < 0) whole = ms_pk[i].type; else { if (ft >= 0 && ft != ms_pk[i].type) mixed = 1; ft = ms_pk[i].type; } }
        if (!any || ms_nref >= CAM_REF_MAX) continue;
        CamRef *r = &ms_ref[ms_nref]; memset(r, 0, sizeof *r);
        r->cls = TP_ECLS_SOLID; r->index = s; r->idn = F.solids[s].idn; r->link = 0;
        r->flags = (unsigned)(whole >= 0 ? whole : mixed ? 0 : ft) & 0x24;
        for (int i = 0; i < ms_npk; i++) if (ms_pk[i].s == s && ms_pk[i].f >= 0 && ms_nrface < CAM_REF_FACE_MAX) {
            CamRefFace *q = &ms_rface[ms_nrface++]; q->ref = ms_nref; q->face = ms_pk[i].f; q->type = ms_pk[i].type; }
        ms_nref++;
    }
    ms_clear_sel(); drv_pick = 0;
    ms_containment();
}
/* the faces the references name (msurf_ent_count with sld_num_of_surfaces_in_toolp, S:426): a whole body = all its faces */
static int ms_ref_count(const CamRef *R, int nr, const CamRefFace *Q, int nq) {
    int n = 0;
    for (int d = 0; d < nr; d++) {
        if (R[d].cls != TP_ECLS_SOLID) { n++; continue; }
        int whole = 1; for (int i = 0; i < nq; i++) if (Q[i].ref == d) { n++; whole = 0; }
        if (whole && R[d].index >= 0 && R[d].index < F.nsolids) { int nf = sld_part_nface(R[d].index); n += nf > 0 ? nf : 1; }
    }
    return n;
}
/* containment (l.16001-16025): msurf_toolcont 1, the op takes one (msurf_op_has_tcbs) and has none -> FUN_18001cff0 (l.20726):
   chain_it_point(434 "Chain 2D tool containment boundary # ") through the Chaining dialog (sld_init_solid_chains_selection: solid
   edges allowed), save_chain(.., role 2). msurf_toolcont 2 (msurf_chain): no writer found - left out (1.2) */
static void ms_containment(void) {
    if (cfg_ms_toolcont == 1 && ms_has_tcbs(ms_opcode(ms_cam))) {
        ms_phase = MS_CONT;
        cam_clear_selection();
        show_params = 0; chain_win_new = 1; partial_e1 = -1; win_pending = 0; last_used = 0; partial_waiting = 0; dyn_state = 0;
        chain_dlg_init();
        snprintf(chain_prompt, sizeof chain_prompt, "Chain 2D tool containment boundary # ");   /* mcmsurfres 434 */
        chain_prompt_upd();
        return;
    }
    ms_to_dialog();
}
/* dialog 100's answer into the op (FUN_180010560, l.11161; op +N = record +N + 0x68): op 5 Boss -> +0x1380 = 1, +0x1368 = 0,
   +0x1369 = 1, +0x136a = 2, +0x1220 = 1, +0x1230 = 0.01; Cavity -> +0x1380 = 0, +0x1368 = 1, +0x1369 = 1, +0x136a = 0, +0x1220 = 1,
   +0x1230 = 3.0; op 7 the same without +0x1380; Undefined: nothing */
static void ms_boss_apply(void) {
    int oc = ms_opcode(ms_cam); if ((oc != 5 && oc != 7) || (ms_boss != 1 && ms_boss != 2)) return;
    int boss = ms_boss == 1;
    if (oc == 5) pf_field_set(0x13e8, 3, boss ? 1 : 0);
    pf_field_set(0x13d0, 2, boss ? 0 : 1); pf_field_set(0x13d1, 2, 1); pf_field_set(0x13d2, 3, boss ? 2 : 0);
    pf_field_set(0x1288, 2, 1); pf_field_set(0x1298, 1, boss ? 0.01 : 3.0);
}
/* the references and the containment chains into the new op, then (msurf_show_selection_dlg) the Toolpath/surface selection dialog,
   then the op dialog (l.16031-16038) */
static void ms_to_dialog(void) {
    int nch = ms_phase == MS_CONT ? ncchains : 0;
    if (cfg_ms_show_dlg) {
        /* HOOK (stage S5): EditAllGeom() - the "Toolpath/surface selection" dialog (formats/surface_selection.md 1.7) opens here, on
           the references just stored, before the op dialog. Not ported yet: the flow goes straight on. */
    }
    /* op 0x84: FUN_180009270 (l.5745: the drive's XY box centre / half diagonal into op +0x1388 / +0x1390 / +0x13a0) NOT PORTED */
    open_params();                                 /* init_operation_surface + the op dialog's seed (cui_ops.c) */
    for (int i = 0; i < pending.nchains && i < nch; i++) pending.chains[i].role = 2;   /* FUN_18001cff0's save_chain role 2 */
    pending.nref = ms_nref; memcpy(pending.ref, ms_ref, sizeof(CamRef) * ms_nref);
    pending.nrface = ms_nrface; memcpy(pending.rface, ms_rface, sizeof(CamRefFace) * ms_nrface);
    ms_boss_apply();
    ms_seed = pending.p; ms_seed_tool = tool_choice;
    ms_phase = MS_DLG; drv_pick = 0; cam_msg[0] = 0;
}
/* ---- after the op dialog (l.16053-16220) */
static int ms_rb(int off) { return (int)pf_field_get(off, 2); }
static int ms_rs16(int off) { return (int)pf_field_get(off, 3); }
static int ms_rs32(int off) { return (int)pf_field_get(off, 4); }
/* msurf_prompt_st_pt (msurf_map l.339990-340357, formats/surface_selection.md 3) or op 0x84 with op +0x1657: op 5 +0x1382, 0xb +0x12aa,
   0xf +0x1368, 0xc always; record = op + 0x68. 1 = ask; *radial: prompt 309 (FUN_18001ccc0 l.20637: ops 6 / 0xc) */
static int ms_wants_point(int oc, int *radial) {
    *radial = oc == 0xc;
    switch (oc) { case 5: return ms_rb(0x13ea) != 0; case 0xb: return ms_rb(0x1312) != 0; case 0xf: return ms_rb(0x13d0) != 0; case 0xc: return 1;
                  case 0x84: return ms_rb(0x16bf) != 0; }
    return 0;
}
static int ms_role_chains(int role) { int n = 0; for (int i = 0; i < pending.nchains; i++) if (pending.chains[i].role == role) n++; return n; }
static void ms_keep_question(int applied);
static void ms_generate(void);
static void ms_post_chain(const char *prompt) {
    ms_phase = MS_POSTCH;
    cam_clear_selection();
    show_params = 0; chain_win_new = 1; partial_e1 = -1; win_pending = 0; last_used = 0; partial_waiting = 0; dyn_state = 0;
    chain_dlg_init();                              /* sld_init_solid_chains_selection: solid edges allowed */
    snprintf(chain_prompt, sizeof chain_prompt, "%s", prompt);
    chain_prompt_upd();
}
static void ms_post_next(void) {
    int oc = ms_opcode(ms_cam);
    ms_phase = MS_POST; plg_pick = 0;
    for (;;) {
        int step = ms_post_step++;
        if (step == 0) {                           /* the start / radial point (l.16080-16099) when the op has no point record yet */
            int radial; if (!ms_wants_point(oc, &radial) || pending.nholes) continue;
            ms_pt_kind = radial ? 2 : 1; plg_pick = 1; chain_prompt[0] = 0; chain_prompt_upd(); return;
        }
        if (step == 1) {                           /* ops 7 / 0xd / 0x2c by the projection source (l.16100-16135; msurf_map: op 7 +0x136c, op 0xd
                                                      +0x1298, op 0x2c 2 or - +0x13b2 == 1 - 0) */
            if (oc != 7 && oc != 0xd && oc != 0x2c) continue;
            int src = oc == 7 ? ms_rs16(0x13d4) : oc == 0xd ? ms_rs16(0x1300) : ms_rs16(0x141a) == 1 ? 0 : 2;
            if (src == 0) {                        /* source operations: the op must name one (op 7 +0x1480, 0xd +0x13b0, 0x2c +0x1380) */
                int n = oc == 7 ? ms_rs32(0x14e8) : oc == 0xd ? ms_rs32(0x1418) : ms_rs32(0x13e8);
                if (n > 0) continue;
                ms_keep_question(1); return;       /* LAB_180017523 */
            }
            if (src == 1) {                        /* curves: FUN_18001c4d0 (l.20168) when the op has no role-3 chain: chain_it_point(356), role 3 */
                if (ms_role_chains(3)) continue;
                ms_post_chain("Select curves to project");   /* mcmsurfres 356 */
                return;
            }
            if (oc == 0x2c) {                      /* select_surf_points (l.20442-20470): Zigzag's two corners, 415 / 416 */
                if (pending.nholes) continue;
                ms_pt_kind = 3; plg_pick = 1; chain_prompt[0] = 0; chain_prompt_upd(); return;
            }
            /* ops 7 / 0xd points: select_surf_points' select_ents(355 "Select points to project", mask 1) - NOT PORTED (the engines refuse
               Points): no pick, the op goes on */
            continue;
        }
        if (step == 2) {                           /* Blend (l.16140-16158): its two chains when it has none - FUN_18001c4d0 chain_it_sync(blend_tp_title):
                                                      mcmillres 2631 "Blend: define " + chain_sync_msg (NOT DECODED, left out) + 2446 "chain " */
            if (oc != 0x6d || ms_role_chains(3)) continue;
            ms_post_chain("Blend: define chain");
            return;
        }
        /* step 3: flowline ops (surf_sel_bits + set_flow_surf): no port type. Step 4: the CAD file (msurf_mach_stl, op 0x6b, HST rest
           material) -> FUN_18001b8a0: NOT DECODED, left out */
        ms_generate();
        return;
    }
}
/* the op dialog's OK on the new op (edit_operation returned ok, l.16038): the fix-ups of ops 7 / 0xd / 0x2c (l.16053-16075: source
   "operations" with none chosen becomes "curves"; op 0x2c's NCI plunge path with no source becomes Zigzag), then the picks */
int ms_dialog_ok(void) {
    if (!ms_live() || ms_phase != MS_DLG || !show_params || edit_camop >= 0) return 0;
    int oc = ms_opcode(ms_cam);
    if (oc == 7 && ms_rs16(0x13d4) == 0 && ms_rs32(0x14e8) < 1) pf_field_set(0x13d4, 3, 1);
    if (oc == 0xd && ms_rs16(0x1300) == 0 && ms_rs32(0x1418) < 1) pf_field_set(0x1300, 3, 1);
    if (oc == 0x2c && ms_rs16(0x141a) == 1 && ms_rs32(0x13e8) < 1) pf_field_set(0x141a, 3, 0);
    show_params = 0; ms_post_step = 0;
    ms_post_next();
    return 1;
}
/* the op dialog's Cancel on the new op: edit_operation not ok -> LAB_180017523 / 532 (l.15965-15985, disassembly 18001754b-1800175c5):
   unless its result code is 99 (which dialog answer that is: NOT DECODED), message_box_yes_no(427 "Do you wish to keep this
   operation?", caption 428 "Toolpath/surface", 2) - No deletes the op, Yes keeps it as before the dialog (edit_operation's Cancel
   leaves the op as it was) */
int ms_dialog_cancel(void) {
    if (!ms_live() || ms_phase != MS_DLG || !show_params || edit_camop >= 0) return 0;
    show_params = 0;
    ms_keep_question(0);
    return 1;
}
static void ms_keep_question(int applied) {
    char keep_msg[sizeof cam_msg]; snprintf(keep_msg, sizeof keep_msg, "%s", cam_msg);
    int yes = cui_hooks.ask_yes_no ? cui_hooks.ask_yes_no("Toolpath/surface", "Do you wish to keep this operation?") : cui_script_yes;   /* mcmsurfres 428 / 427 */
    ms_off();
    if (yes && !applied) { pf_reset(); ms_boss_apply(); pf_apply(&ms_seed); }   /* the op before the dialog: FUN_180010560's fields only */
    if (yes) { ms_keep_op(applied, &ms_seed, ms_seed_tool); snprintf(cam_msg, sizeof cam_msg, "%s", keep_msg); }
    else ms_drop_op();
}
/* the generation (l.16222-16244): references > 0 -> bnci_section; else message 241 once per session when Drive is not None, the op kept */
/* N13-9 (SS-7): a front end with cui_hooks.gen_queued computes the job as the op dialog's OK does (its worker thread and progress
   window): the job waits here (ms_gen_take) and the front end ends it with gen_end; without the hook (cam_cli, core tests) it runs
   at once */
static CamOp ms_gen_job; static int ms_gen_wait;
static void ms_generate(void) {
    if (ms_ref_count(pending.ref, pending.nref, pending.rface, pending.nrface) > 0) {
        ms_phase = MS_GEN;
        CamOp job;
        if (!gen_begin(&job)) { ms_keep_question(1); return; }
        if (cui_hooks.gen_queued) { ms_gen_job = job; ms_gen_wait = 1; cui_hooks.gen_queued(); return; }
        int ok = gen_run(&job);
        gen_end(&job, ok, 0);                      /* -> ms_gen_end */
        return;
    }
    if (ms_drive > 0 && !ms_shown241) {
        ms_shown241 = 1;
        cui_message("Operation Manager ", "Surface operation does not contain any drive surfaces");   /* mcmsurfres 429 / 241 (message_box_ok_str) */
    }
    ms_off();
    ms_keep_op(1, NULL, -1);
}
int ms_gen_take(CamOp *job) {
    if (!ms_gen_wait) return 0;
    ms_gen_wait = 0;
    if (!ms_live() || ms_phase != MS_GEN) {        /* the flow ended meanwhile (a new part): the copy is dropped */
        for (int i = 0; i < ms_gen_job.nchains; i++) cam_chain_free(&ms_gen_job.chains[i]);
        free(ms_gen_job.chains); free(ms_gen_job.holes); free(ms_gen_job.m); return 0;
    }
    *job = ms_gen_job; return 1;
}
int ms_gen_waiting(void) { return ms_gen_wait; }
/* the Selection Bar's solid buttons: enabled while the solid selection runs (OnUpdateSolidsSelectBody @1800d15c0 /
   OnUpdateSolidsSelectFace: GetSolidSelectionActive and the kind allowed - here the drive / check prompt, select_ents 0x110100);
   a click toggles its bit (OnSolidsSelectBody / Face) */
int ms_selbar_enabled(void) { return drv_pick && ms_live(); }
void ms_selbar_toggle(int face) {
    if (!ms_selbar_enabled()) return;
    if (face) ms_sld_faces = !ms_sld_faces; else ms_sld_body = !ms_sld_body;
}
/* gen_end's result (cui_ops.c) for the flow's op: computed -> committed as usual; failed / cancelled -> bnci_section's flag 0 ->
   the 427 question (LAB_180017523; [inferred] the flag says the toolpath was not made) */
int ms_gen_end(int ok, int cancelled) {
    if (!ms_live() || ms_phase != MS_GEN) return 0;
    if (ok && !cancelled) { ms_off(); return 0; }  /* gen_end commits */
    ms_keep_question(cancelled ? 1 : 2);           /* failed: gen_end has the applied op in pending already */
    return 1;
}
/* the Chaining dialog's OK (chain_act case 1) while the flow chains: containment -> on to the dialog; blend / curve chains -> into
   the op with role 3 (save_chain(.., 3, ..)), then the next pick */
int ms_chain_ok(void) {
    if (!ms_live()) return 0;
    if (ms_phase == MS_CONT) { ms_to_dialog(); return 1; }
    if (ms_phase != MS_POSTCH) return 0;
    pending.chains = realloc(pending.chains, sizeof(Chain) * (pending.nchains + ncchains + 1));
    for (int i = 0; i < ncchains; i++) { Chain c = chain_dup(&cchains[i]); c.role = 3; pending.chains[pending.nchains++] = c; }
    chain_keep_last(); cam_clear_selection();
    ms_post_next();
    return 1;
}
/* Esc (or the Chaining dialog's close box) in the flow: in the drive / check prompt select_ents returns 2 -> the selection is
   cancelled -> the new op is deleted at once (l.19886-19889, 20009-20011; LAB_180017532 with the cancel flag: no question); in the
   containment chaining chain_it_point fails -> no containment, on to the dialog (msurf_assoc does not test FUN_18001cff0's result);
   in the start / radial point pick FUN_18001ccc0 stores nothing, the op goes on; in the plunge corners / blend / curve chaining the
   pick fails -> LAB_180017523: the 427 question. 1 = taken */
static int ms_escape(void) {
    if (!ms_live()) return 0;
    switch (ms_phase) {
    case MS_SEL: ms_off(); ms_drop_op(); return 1;
    case MS_CONT: cam_clear_selection(); ms_to_dialog(); return 1;
    case MS_POST:
        if (ms_pt_kind == 3) { plg_pick = 0; ms_keep_question(1); return 1; }
        plg_pick = 0; cam_msg[0] = 0; ms_post_next(); return 1;
    case MS_POSTCH: cam_clear_selection(); ms_keep_question(1); return 1;
    }
    return 0;
}
/* the point picks after the dialog: point_proc_assoc - the click's AutoCursor point (the front end's geo_snap), a kind 1 record (here
   the op's points: holes 0 / 1, as the engines read them - cam3d_srough.c radial / plunge) */
int plg_picking(void) { return plg_pick > 0 && ms_live() && ms_phase == MS_POST && !show_params; }
void plg_click(double x, double y, double z) {
    (void)z;
    if (!plg_picking()) return;
    pending.holes = realloc(pending.holes, sizeof(Hole) * (pending.nholes + 1));
    pending.holes[pending.nholes].x = x; pending.holes[pending.nholes].y = y; pending.holes[pending.nholes].d = 0; pending.nholes++; overlay_dirty = 1;
    if (ms_pt_kind == 3 && plg_pick == 1) { plg_pick = 2; chain_prompt_upd(); return; }   /* the second corner (416) */
    plg_pick = 0; cam_msg[0] = 0;
    ms_post_next();
}
static const char *ms_pt_prompt(void) {
    return ms_pt_kind == 2 ? "Select radial point" : ms_pt_kind == 1 ? "Select an approximate starting point"   /* mcmsurfres 309 / 310 */
         : plg_pick == 1 ? "Select plunge point at lower left" : "Select plunge point at upper right";   /* 415 / 416 */
}
/* End Selection (Enter): select_ents returns (not 2) */
int drv_done(void) {
    if (!drv_pick || !ms_live()) return 0;
    if (drv_pick == 1) ms_drive_end(); else ms_check_end();
    return 1;
}
/* the start (gallery_pick): dialog 100 for ops 5..8 (FUN_1800250b0 l.26245: +0x1550 = 2 (Undefined), DoModal, returns +0x1550 + 1 -
   DDX_Radio writes +0x1550 at OK only and the code tests no DoModal result, so Cancel answers 3 = Undefined), then FUN_18001bcf0's
   head (l.19850-19899): the modes, the selection cleared, the solid selection reset to type 0, Drive All / Prompt / None */
static void ms_begin(int t) {
    ms_off(); ms_cam = t; cam_mode = t; show_params = 0; ms_phase = MS_SEL;
    int oc = ms_opcode(t); ms_boss = 0;
    if (oc >= 5 && oc <= 8) {
        static const char *L[3] = { "Boss", "Cavity", "Undefined" };   /* mcmsurfres 100: 512 / 514 / 516 */
        int sel = 2;
        if (cui_hooks.radio_dialog) cui_hooks.radio_dialog(100, "Select Boss/Cavity", L, 3, &sel);
        else { if (cui_script_radio >= 0 && cui_script_radio < 3) sel = cui_script_radio; printf("    (dialog \"Select Boss/Cavity\": %s)\n", cui_script_radio < 0 ? "Cancel" : L[sel]); }
        ms_boss = sel + 1;
    }
    ms_drive = cfg_ms_all_geom % 10; ms_check = cfg_ms_all_geom < 10 ? 0 : cfg_ms_all_geom < 20 ? 1 : 2;
    ms_clear_sel(); ms_npk = 0; ms_ptype = 0; ms_nref = 0; ms_nrface = 0;
    ms_sld_body = 1; ms_sld_faces = 0;             /* N13-8: whole bodies (the buttons' starting state NOT DECODED) */
    if (ms_drive == 2) { ms_sel_all_surfs(); ms_sel_all_solids(); ms_drive_end(); return; }   /* FUN_18001b530 + sld_sel_all_solid_surfs */
    if (ms_drive == 1) { drv_pick = 1; ms_prompt(); return; }                                    /* select_ents(406, 0x110100) */
    ms_drive_end();                                                                              /* None (and any other value) */
}
/* tests: the flow's state and the references (step srfsel) */
void ms_dump(void) {
    printf("    srfsel phase %d prompt %d faces %d cfg %d/%d/%d/%d boss %d npick %d nref %d nrface %d pt %d\n", ms_live() ? ms_phase : 0, drv_pick, ms_sld_faces,
           cfg_ms_all_geom, cfg_ms_toolcont, cfg_ms_mach_stl, cfg_ms_show_dlg, ms_boss, ms_npk, ms_nref, ms_nrface, plg_pick);
    for (int i = 0; i < ms_npk; i++) printf("    srfsel pick solid %d %s %d type %d\n", ms_pk[i].s + 1, ms_pk[i].f < 0 ? "body" : "face", ms_pk[i].f < 0 ? 0 : ms_pk[i].f, ms_pk[i].type);
    for (int e = 0; ent_sel && e < ent_sel_n && e < F.nents; e++) if (ent_sel[e] && F.ents[e].type == E_SURF) printf("    srfsel selected surface %d\n", F.ents[e].srf + 1);
    for (int s = 0; ms_bsel && s < ms_nbsel; s++) if (ms_bsel[s]) printf("    srfsel selected solid %d\n", s + 1);
    const CamOp *ops[MAX_CAMOPS + 1]; int no = 0;
    if (ms_live() && ms_phase >= MS_DLG) ops[no++] = &pending;
    for (int j = 0; j < ncamops; j++) if (cam_type_is_3d(camops[j]->p.type)) ops[no++] = camops[j];
    for (int j = 0; j < no; j++) {
        const CamOp *o = ops[j];
        int n2 = 0, n3 = 0; for (int i = 0; i < o->nchains; i++) { if (o->chains[i].role == 2) n2++; if (o->chains[i].role == 3) n3++; }
        printf("    srfsel %s %s: %d ref, %d face, chains %d (role 2: %d, role 3: %d), points %d, moves %d, dirty %d\n", o == &pending ? "pending" : "op", o->name, o->nref, o->nrface,
               o->nchains, n2, n3, o->nholes, o->nm, o->dirty);
        for (int d = 0; d < o->nref; d++) printf("    srfsel   ref %d: %s %d idn %d flags 0x%x link %d\n", d, o->ref[d].cls == TP_ECLS_SOLID ? "solid" : o->ref[d].cls == TP_ECLS_SURF ? "surface" : "other",
                                                 o->ref[d].index + 1, o->ref[d].idn, o->ref[d].flags, o->ref[d].link);
        for (int d = 0; d < o->nrface; d++) printf("    srfsel   face: ref %d face %d type 0x%x\n", o->rface[d].ref, o->rface[d].face, o->rface[d].type);
        for (int h = 0; h < o->nholes; h++) printf("    srfsel   point %.4f,%.4f\n", o->holes[h].x, o->holes[h].y);
    }
}
int gallery_pick(const char *name) {             /* gallery items this program can generate */
    int t = cam_type_from_label(name);
    if (!t || !loaded) return 0;
    if (geo_chaining) geo_end();                     /* a wireframe chain command's chaining ends (it shares the chain list) */
    cam_clear_selection();
    ms_off();
    if (cam_type_is_3d(t)) { ms_begin(t); return 1; }   /* msurf_assoc (above) */
    cam_variant = t == CAM_CONTOUR && !strcasecmp(name, "Chamfer") ? 1 : 0;   /* "Engrave" is CAM_ENGRAVE now (engrave.dll, not a contour variant) */
    cam_mode = t; show_params = 0; chain_win_new = 1; partial_e1 = -1; win_pending = 0; last_used = 0; partial_waiting = 0; dyn_state = 0;
    chain_dlg_init();
    /* chaining prompt: Mastercam's string where the op's start code was traced (mcmill.dll, decomp/mcmill_all.c):
       ctour_toolpaths chain_it_point(4267), slotmill chain_it(mcmillres 2808), 2D HST FUN_1802cddc0(10208), pock_assoc ->
       mccore prompt_pocket chain_it_point(mccoreres 4268), face_assoc chain_it(mcmillres 2647 when the active machine
       group has stock X and Y > 0, else 2648); drill / helix / circle / thread pick points with an empty prompt (Drill Point Manager loop) */
    /* exact resource texts (trailing spaces kept); run_chain_dlg shows Format("%s %d", prompt, chains + 1) */
    const char *pr = t == CAM_CONTOUR ? "Select Contour chain " : t == CAM_SLOT ? "Select SlotMill chain" :
                     (t == CAM_DYNAMIC || t == CAM_AREA || t == CAM_DYNCONTOUR || t == CAM_PEEL) ? "Select 2D HST machining chain" :
                     t == CAM_POCKET ? "Select Pocket chain " :
                     t == CAM_FACE ? (F.stock[0] > 0 && F.stock[1] > 0 ? "Select OK to use defined stock or select chain " : "Select Face chain") :
                     t == CAM_ENGRAVE ? "Engraving: Select chains" :   /* engraveres 3 (engrave.dll chain selection, formats/engrave.md 1.1) */
                     cam_type_uses_holes(t) || t == CAM_HELIX || t == CAM_CIRCLE || t == CAM_THREAD ? "" : NULL;   /* point ops: mcmill tp_pt_mgr -> FUN_18008b0d0 get_event("") */
    snprintf(chain_prompt, sizeof chain_prompt, "%s", pr ? pr : cam_variant == 1 ? "Chamfer" : cam_variant == 2 ? "Engrave" : cam_type_label(t));
    chain_prompt_upd();
    return 1;
}
/* the Extrude command's chaining (mcamui ex_chaining, formats/extrude.md sec. 1.2): chain_it(sld 470) */
void chain_begin_extrude(void) {
    ex_phase = 0; chain_win_new = 1; partial_e1 = -1; win_pending = 0; partial_waiting = 0; dyn_state = 0;
    chain_dlg_init();
    snprintf(chain_prompt, sizeof chain_prompt, "Select chain(s) to extrude");
    chain_prompt_upd();
}
/* a wireframe chain command's chain_it (cui_geo.c geo_chaining): Fillet Chains / Chamfer Chains chain_it(0x1089) =
   mccadres 4233 "Select chain" (sketcher_fillet_2.c:199 / :275, sketcher_chamfer_2.c:201 / :282), Offset Chains
   chain_it(0xf64) (do_xform_offset_ctour.c:93, prompt from the caller). chain_it(int) -> run_chain_dlg(prompt, chains,
   NULL, 0, 0, 0, 0, false) (decomp/mccore_chain_range.c l.5014): no mode argument - the dialog's mode comes from the
   config as every chaining (chain_dlg_init); init_chaining -> FUN_1801e6440 starts with an empty list (*chains = 0,
   l.10699), so a Reselect starts from no chains */
void chain_begin_geo(const char *prompt) {
    cam_clear_selection();
    chain_win_new = 1; partial_e1 = -1; win_pending = 0; partial_waiting = 0; dyn_state = 0; last_used = 0;
    chain_dlg_init();
    snprintf(chain_prompt, sizeof chain_prompt, "%s", prompt);
    chain_prompt_upd();
}
/* the full chaining (every dialog mode): toolpath chaining or a wireframe chain command's chain_it */
static int chain_full(void) { return (cam_mode && !show_params) || geo_chaining; }
/* the Chaining dialog is up: toolpath chaining (not the op dialog), the Extrude command's chain selection or a wireframe
   chain command's chain_it */
int chain_dlg_shown(void) {
    if (ms_live() && ms_phase != MS_CONT && ms_phase != MS_POSTCH) return 0;   /* a 3D op's flow: only its chainings show the dialog */
    return ((cam_mode && !drv_pick) || (ex_mode && !ex_phase) || geo_chaining) && !show_params;
}
/* Esc / the window's close box during toolpath chaining (mcamui: cam_mode = 0; cam_clear_selection) */
void chain_cancel(void) {
    if (ms_escape()) { sld_cancel(); return; }       /* a 3D op's flow (msurf_assoc's rules, above) */
    sld_cancel(); ms_off(); cam_mode = 0; cam_clear_selection();
}
/* Esc while the Chaining dialog is up: the solid selection takes it first (sld_escape) */
int chain_escape(void) { return sld_escape(); }
/* UIVertDialog::ShowExpandButton(+0x17f8 == 0) (FUN_1801f06d0): no expand icon in Solids mode */
int chain_dlg_icon(void) { return !sld_mode; }

/* Last (FUN_1801ed6b0 / FUN_1801e7be0): keep this chaining's chains for the next chaining (mcamui open_params) */
void chain_keep_last(void) {
    for (int i = 0; i < nlast_chains; i++) cam_chain_free(&last_chains[i]);
    nlast_chains = 0;
    for (int i = 0; i < ncchains; i++) { if (cchains[i].sld) continue;   /* the dialog's CHAINs only (solid chains come after chain_it) */
        Chain *d = &last_chains[nlast_chains++]; *d = cchains[i];
        d->ent = malloc(d->n * sizeof(int)); d->rev = malloc(d->n * sizeof(int)); memcpy(d->ent, cchains[i].ent, d->n * sizeof(int)); memcpy(d->rev, cchains[i].rev, d->n * sizeof(int)); }
}

/* ------------------------------------------------------------------------------------------------ Window / Polygon / Vector */
/* spline curves (E_SPLINE) are chainable while chaining for an Engrave operation only (cam2d.c cam_chain_splines; formats/
   spline_chain.md): the chaining entry points below set the flag for their run and clear it after, so every other chaining
   (other operations, Extrude, the wireframe chain functions, script steps) leaves splines out */
int cui_spline_chaining(void) { return cam_mode == CAM_ENGRAVE && !ex_mode && !geo_chaining && !sld_mode; }
/* CS-7 (formats/chaining.md "Curves outside the C-plane, splines and NURBS"): chaining for an operation takes Mastercam's whole chain
   entity mask (run_chain_dlg DAT_1813c4cbc = 0x8f: lines, arcs, splines, NURBS in any plane; cam2d.c cam_chain_all) - the operation's
   generator refuses what it does not take (cam_generate). Extrude and the wireframe chain functions keep the old rule (planar lines /
   arcs): their handling of such curves is not checked here (a pick on one says so, chain_off_consumer) */
int cui_all_chaining(void) { return cam_mode && !ex_mode && !geo_chaining && !sld_mode; }
static void vector_end_(void); static void window_chains_(double sx, double sy); static int chain_click_pre_(float mx, float my);
static int chain_pick_at_(int e, double wx, double wy); static void chain_act_(int id);
static void cfl_on(void) { cam_chain_splines = cui_spline_chaining(); cam_chain_all = cui_all_chaining(); }
static void cfl_off(void) { cam_chain_splines = 0; cam_chain_all = 0; cam_chain_pick_view = 0; }
void vector_end(void) { cfl_on(); vector_end_(); cfl_off(); }
void window_chains(double sx, double sy) { cfl_on(); window_chains_(sx, sy); cfl_off(); }
int chain_click_pre(float mx, float my) { cfl_on(); int r = chain_click_pre_(mx, my); cfl_off(); return r; }
int chain_pick_at(int e, double wx, double wy) { cfl_on(); int r = chain_pick_at_(e, wx, wy); cfl_off(); return r; }
void chain_act(int id) { cfl_on(); chain_act_(id); cfl_off(); }
void polygon_end(void) {                         /* double-click / Enter: the polygon's chains (FUN_1801dfae0) */
    if (poly_n < 3) { poly_n = 0; return; }
    vp_main(); free(win_sel); win_sel = polygon_select(win_sel_mode, poly_x, poly_y, poly_n);
    double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, poly_x[0], poly_y[0] - vp_y, w);
    poly_n = 0;
    if (cfg_win_prompt) { win_pending = 1; chain_prompt_upd(); }
    else { window_chains(w[0], w[1]); chain_prompt_upd(); }
}
/* Vector (mode 8, FUN_1801df560): the sketched line's crossings with each entity (the last vector segment that crosses
   wins, as the loop over the vector segments does); ended like Polygon (double-click / Enter - the vector prompt adds no
   text, same sketch input 0x288 vs 0x248) */
static void vector_end_(void) {
    if (poly_n < 2) { poly_n = 0; return; }
    vp_main();
    char *crossed = calloc(F.nents ? F.nents : 1, 1); double *cxy = calloc(2 * (F.nents ? F.nents : 1), sizeof(double));
    for (int e = 0; e < F.nents; e++) {
        const Ent *en = &F.ents[e]; if (!en->type || !ENT_LV_SHOWN(en)) continue;   /* the display list (ENT_LV_SHOWN) */
        for (int pi = en->first_poly; pi < en->first_poly + en->npoly; pi++)
            for (int k = 1; k < F.polys[pi].count; k++) {
                V3 a = F.pts[F.polys[pi].first + k - 1], b = F.pts[F.polys[pi].first + k]; float ax, ay, bx, by;
                if (!scene_to_screen(vp_w, vp_h, a.x, a.y, a.z, &ax, &ay) || !scene_to_screen(vp_w, vp_h, b.x, b.y, b.z, &bx, &by)) continue;
                ax += vp_x; ay += vp_y; bx += vp_x; by += vp_y;
                for (int v = 0; v + 1 < poly_n; v++) {
                    float px = poly_x[v], py = poly_y[v], qx = poly_x[v + 1], qy = poly_y[v + 1];
                    float den = (bx - ax) * (qy - py) - (by - ay) * (qx - px); if (fabsf(den) < 1e-9f) continue;
                    float u = ((px - ax) * (qy - py) - (py - ay) * (qx - px)) / den, w = ((px - ax) * (by - ay) - (py - ay) * (bx - ax)) / den;
                    if (u < 0 || u > 1 || w < 0 || w > 1) continue;
                    crossed[e] = 1; cxy[2 * e] = a.x + (b.x - a.x) * u; cxy[2 * e + 1] = a.y + (b.y - a.y) * u;
                }
            }
    }
    double w0[3]; scene_pick(vp_x, 0, vp_w, vp_h, poly_x[0], poly_y[0] - vp_y, w0);
    Chain tmp[256]; int room = 256 - ncchains;
    int n = cam_chain_vector(&F, crossed, cxy, w0[0], w0[1], cfg_chain_tol, cfg_chain_dir == 1, cfg_chain_zigzag, cfg_chain_open, cfg_chain_closed, tmp, room > 0 ? room : 0);
    for (int i = 0; i < n; i++) cchains[ncchains++] = tmp[i];
    free(crossed); free(cxy); poly_n = 0;   /* Vector (mode 8) leaves +0x1f0ac as it was */ overlay_dirty = 1; chain_prompt_upd();
}
/* the window's chains, ordered from the start point (cam_chain_window, mccore FUN_1801dfae0) */
static void window_chains_(double sx, double sy) {
    if (!win_sel) return;
    Chain tmp[256]; int room = 256 - ncchains;
    cam_chain_plane_z = grid_z();                          /* constr_depth for the plane filter (FUN_1801f9e90) */
    int n = cam_chain_window(&F, win_sel, sx, sy, cfg_chain_tol, cfg_chain_dir == 1, cfg_chain_zigzag, cfg_chain_open, cfg_chain_closed, tmp, room > 0 ? room : 0);
    for (int i = 0; i < n; i++) cchains[ncchains++] = tmp[i];
    win_group_n = n;                                       /* run_chain_dlg: +0x1f0aa = chains added, +0x1f0ac = 1 */
    free(win_sel); win_sel = NULL; win_pending = 0; overlay_dirty = 1;
}
/* Window mode: the left button goes down in the view and drags the selection window (mcamui main loop zw_active 2) */
int chain_window_drag(void) { return chain_full() && chain_single == 3 && !win_pending && !sld_mode; }
/* the window's release: entities by window_select, then the start point prompt (mccoreres 4235) or the chains at once */
void chain_window_done(float x0, float y0, float x1, float y1) {
    if (fabsf(x1 - x0) > 3 && fabsf(y1 - y0) > 3) {
        vp_main(); free(win_sel); win_sel = window_select(x0, y0, x1, y1, win_sel_mode);
        if (cfg_win_prompt) { win_pending = 1; chain_prompt_upd(); }   /* mccoreres 4235 */
        else { double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x0, y0 - vp_y, w); window_chains(w[0], w[1]); chain_prompt_upd(); }
    }
}
/* a polygon / vector is being sketched: Enter ends it (mcamui main loop) */
int chain_sketching(void) { return (cam_mode || geo_chaining) && (chain_single == 5 || chain_single == 6) && poly_n && !sld_mode; }
void chain_sketch_end(void) { if (chain_single == 5) polygon_end(); else vector_end(); }
/* the chaining picks take the press point (chain_it comes through the view message 0x8030 OnLButtonDown posts with the
   press point, decomp/mastercam_viewmouse.c; mcamui.c main loop geo_pick_now) */
int chain_press_pick(void) { return (cam_mode && !show_params) || (ex_mode && !ex_phase) || geo_chaining; }

/* ------------------------------------------------------------------------------------------------ graphics picks */
/* handle_click's chaining cases before the wireframe command's (Area, Polygon / Vector points, the window start point,
   Point mode): 1 = the click was taken. (mx, my) window coordinates, vp_* set by the caller. */
static int chain_click_pre_(float mx, float my) {
    if (sld_mode && chain_full()) return sld_click(mx, my);   /* Solids mode: the solid selection's pick (cui_sldchain.c) */
    if (chain_full() && chain_single == 4 && !cam_type_uses_holes(cam_mode)) {   /* Area (mode 4): chain_area */
        double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, mx, my - vp_y, w);
        Chain tmp[256]; int room = 256 - ncchains;
        cam_chain_plane_z = grid_z();
        /* CS-7: chain_area with curves outside the C-plane (its nesting / boundary tests of such chains) is NOT PORTED: Area keeps the old rule */
        int all = cam_chain_all; cam_chain_all = 0;
        int n = cam_chain_area(&F, w[0], w[1], cfg_chain_tol, cfg_chain_dir == 1, cfg_chain_open, cfg_chain_infinite, tmp, room > 0 ? room : 0);
        cam_chain_all = all;
        for (int i = 0; i < n; i++) cchains[ncchains++] = tmp[i];
        win_group_n = 0; overlay_dirty = 1; chain_prompt_upd(); return 1;
    }
    if (chain_full() && (chain_single == 5 || chain_single == 6) && !win_pending) {   /* Polygon / Vector points */
        if (poly_n < 256) { poly_x[poly_n] = mx; poly_y[poly_n] = my; poly_n++; }
        if (last_clicks >= 2) { if (chain_single == 5) polygon_end(); else vector_end(); }
        return 1;
    }
    if (win_pending && (cam_mode || geo_chaining)) {     /* "Sketch approximate start point" (FUN_1801dfae0 point_proc) */
        double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, mx, my - vp_y, w);
        window_chains(w[0], w[1]); chain_prompt_upd(); return 1;
    }
    if (chain_single == 7 && !win_pending && !dyn_state && ((chain_full() && !cam_type_uses_holes(cam_mode)) || (ex_mode && !ex_phase))) {
        /* Point mode (dialog mode 9, FUN_1801edbb0): run_chain_dlg get_event(prompt, 0x800020f, selection mask 1 = points)
           -> chain_append_point: a picked point entity is chained (not when it already is the last chain); any other
           position (AutoCursor) is stored as a new point entity with the main attributes (store_ent) and chained */
        int e = -1; double bd = SEL_APERTURE_PX;
        for (int i = 0; i < F.nents; i++) { const Ent *pe = &F.ents[i]; float sx, sy;
            if (pe->type != E_POINT || !ENT_LV_SHOWN(pe) || !scene_to_screen(vp_w, vp_h, pe->a.x, pe->a.y, pe->a.z, &sx, &sy)) continue;
            double d = hypot(sx + vp_x - mx, sy + vp_y - my); if (d < bd) { bd = d; e = i; } }
        if (e >= 0 && ncchains && cam_chain_is_point(&F, &cchains[ncchains - 1]) && cchains[ncchains - 1].ent[0] == e) return 1;
        if (e < 0) {
            double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, mx, my - vp_y, w);
            V3 p = { w[0], w[1], grid_z() }, z0 = { 0, 0, 0 }; geo_snap(&p, NULL, -1, 0);
            int from = F.nents; geo_add(E_POINT, p, p, z0, 0, 0, 0); undo_ents_added(from);
            if (F.nents > from) e = F.nents - 1;
        }
        if (e >= 0 && ncchains < 256 && cam_chain_point(&F, e, &cchains[ncchains])) ncchains++;
        win_group_n = 0; overlay_dirty = 1; chain_prompt_upd(); return 1;
    }
    return 0;
}
/* handle_click's pick on entity e (the nearest under the cursor) during point / chain selection: drill points, then
   run_chain_dlg's picks. 0 = not chaining (the pick is a general selection). */
/* a chain as 3D points (ours, for chains holding view arcs): lines by their ends, arcs by mcam_arc_pt at chords of
   cam_chain_polyline's step; *out malloc'd (x, y, z triples); returns the count, 0 on failure */
int chain_polyline3(const Chain *c, double tol, double **out) {
    size_t cap = 64, n = 0; double *p = malloc(cap * 3 * sizeof(double));
    if (!p) { *out = NULL; return 0; }
    for (int i = 0; i < c->n; i++) {
        if (c->ent[i] < 0 || c->ent[i] >= F.nents) continue;
        const Ent *e = &F.ents[c->ent[i]]; int r = c->rev[i], k = 1;
        double *ST = NULL, *SP = NULL; int sn = 0;
        if (e->type == E_ARC) { double step = e->r > tol ? 2 * acos(1 - tol / e->r) : M_PI / 8; if (step > M_PI / 18) step = M_PI / 18;
                                k = (int)ceil(fabs(e->sw) / step); if (k < 2) k = 2; if (k > 4096) k = 4096; }
        if (e->type == E_SPLINE) { sn = cam_spline_samples(&F, e, tol, &ST, &SP); if (sn >= 2) k = sn - 1; }   /* its break points (CS-7, ours for drawing) */
        for (int j = (n ? 1 : 0); j <= k; j++) {
            double t = r ? 1.0 - (double)j / k : (double)j / k; V3 q;
            if (e->type == E_SPLINE && sn >= 2) { int s = r ? k - j : j; q = (V3){ SP[3 * s], SP[3 * s + 1], SP[3 * s + 2] }; }
            else if (e->type == E_SPLINE) q = r ? (j ? e->a : e->b) : (j ? e->b : e->a);
            else if (e->type == E_ARC) q = mcam_arc_pt(e, e->a0 + e->sw * t);
            else if (e->type == E_LINE) q = (V3){ e->a.x + (e->b.x - e->a.x) * t, e->a.y + (e->b.y - e->a.y) * t, e->a.z + (e->b.z - e->a.z) * t };
            else q = e->a;
            if (n == cap) { double *np_ = realloc(p, cap * 2 * 3 * sizeof(double)); if (!np_) { free(p); *out = NULL; return 0; } p = np_; cap *= 2; }
            p[3 * n] = q.x; p[3 * n + 1] = q.y; p[3 * n + 2] = q.z; n++;
        }
        free(ST); free(SP);
    }
    *out = p; return (int)n;
}
/* a chain holding an arc in another plane (Ent.vw) */
int chain_has_view_arc(const Chain *c) {
    for (int k = 0; k < c->n; k++) if (c->ent[k] >= 0 && c->ent[k] < F.nents && F.ents[c->ent[k]].type == E_ARC && F.ents[c->ent[k]].vw) return 1;
    return 0;
}
/* a chain holding a curve outside the XY planes (a view arc, a line not parallel to XY, a spline not parallel to XY: CS-7): drawn and
   dumped in 3D (chain_polyline3) */
int chain_is_3d(const Chain *c) {
    for (int k = 0; k < c->n; k++) { if (c->ent[k] < 0 || c->ent[k] >= F.nents) continue; const Ent *e = &F.ents[c->ent[k]];
        if (!e->gen && cam_ent_tilted(e)) return 1; }
    return 0;
}
/* CS-7: a curve Mastercam's chaining takes (cam_ent_chainable_any) that this consumer's rule here does not (Extrude, the wireframe chain
   functions: their handling of 3D curves / splines is not checked): 1 = said so */
static int chain_off_consumer(const Ent *en) {
    int v = cam_chain_views; cam_chain_views = cam_mode && !ex_mode && !geo_chaining;   /* (the rule chain_pick_at_ applies) */
    int ok = cam_ent_chainable(en) || !cam_ent_chainable_any(en); cam_chain_views = v;
    if (ok) return 0;
    cui_message("apprenticeCAM", ex_mode ? "Extrude: chaining a curve outside the XY planes or a spline curve for it is NOT PORTED here (Mastercam's chaining takes it; Extrude's handling of it is not decoded)"
                                         : "This function: chaining a curve outside the XY planes or a spline curve for it is NOT PORTED here (Mastercam's chaining takes it; this function's handling of it is not decoded)");
    return 1;
}
/* ------------------------------------------------------------------------------------------------ surfaces in the Chaining dialog
   (formats/chaining.md "Surfaces in the Chaining dialog"; decomp/mccore_chain_range.c). run_chain_dlg (0x1801f0920) adds SURF_ID 0x100 to
   get_event's entity mask DAT_1813c4cbc only in dialog mode 2 = Single and only while config 6026 chain_surface_edges_in_single_mode is on
   (l.17843-17850; the mask's 0x100 is taken off again after the event); Chain (mode 1, l.17837-17841: LAB_1801f0eaf, the plain mask), Partial,
   Window, Area, Polygon, Vector keep the wireframe mask; the C-plane / 3D radio (chain_plane_filter) plays no part (chain_single does not test it).
   With the mask the auto-highlight (FUN_1804df930 -> DrawAutoHighlight with the current selection mask) and the pick (select_ent_univ: dist_to_surf
   with shading off, gl_Hit_Surfs with it on) take surfaces as in the general selection. RunLatheChainDlg (0x1801ee400, l.16393-16403) does it for
   modes 1 and 2 (the lathe dialog: not here). */
int chain_srf_pick(void) {
    int chaining = (cam_mode && !show_params && !cam_type_uses_holes(cam_mode)) || geo_chaining || (ex_mode && !ex_phase);
    return chaining && chain_single == 1 && cfg_surf_single && !sld_mode && !dyn_state && !win_pending && !plg_picking() && !drv_pick;
}
/* msf_nurbs_splines (mccore 0x1813fb8f2): 0 at start (the image: MemDump 2026-11-17); the mccad curve-from-surface commands set it to
   cfg_createNurbs (or restore a saved value) and leave it - those writers are not mirrored here yet (formats/TODO.md): it stays 0 */
static int msf_nurbs_splines = 0;
/* dist_to_finite_line (mccore 0x1804b6680): the distance of p to the segment a b in the view plane; hit = the perpendicular foot
   inside the segment and the distance < maxd, or, past an end (within maxd of it along the line), the end's distance < maxd; a segment
   shorter than tol: the distance to a. t_on_line (0x1804b48a0): t of p's foot (0 when the segment is shorter than tol) */
static int dfl_hit(const double p[2], const double a[2], const double b[2], double maxd, double tol, double *d) {
    double ex = b[0] - a[0], ey = b[1] - a[1], L = sqrt(ex * ex + ey * ey), q;
    if (tol <= L) {
        double pd = fabs((p[0] - a[0]) * ey - (p[1] - a[1]) * ex) / L; *d = pd;
        if (maxd <= pd) return 0;
        double s = ((p[0] - a[0]) * ex + (p[1] - a[1]) * ey) / L;
        if (0.0 <= s) { if (s <= L) return 1; if (L + maxd <= s) return 0; q = (b[1] - p[1]) * (b[1] - p[1]) + (b[0] - p[0]) * (b[0] - p[0]); }
        else { if (s <= -maxd) return 0; q = (a[1] - p[1]) * (a[1] - p[1]) + (a[0] - p[0]) * (a[0] - p[0]); }
    } else q = (a[1] - p[1]) * (a[1] - p[1]) + (a[0] - p[0]) * (a[0] - p[0]);
    *d = sqrt(q); return *d < maxd;
}
static double t_on_line2(const double p[2], const double a[2], const double b[2], double tol) {
    double ex = b[0] - a[0], ey = b[1] - a[1], l2 = ey * ey + ex * ex;
    return tol * tol < l2 ? (ey * (p[1] - a[1]) + ex * (p[0] - a[0])) / l2 : 0.0;
}
/* dist_to_surf (mccore 0x1802ddba0, decomp/surfpick/core_dist.c l.178-377), the untrimmed branch: select_ent_univ's surface parameters
   of a pick (mastercam.dll 0x18020f4a0 l.403-409, the output l.557-558: get_event's t, chain_single's param_2). Pass 1: the u = const
   lines at surf_division's spacing (density 0: half the range) from u0 (the last at u1), sampled along v at its step (the last at v1);
   pass 0: the v = const lines likewise (kind 7: the u step at most 2). Each sample through proj_iso_pt (the graphics view's x / y axes);
   a segment that dist_to_finite_line hits within the best distance so far becomes the pick: (u, v - (1 - t) * step) in pass 1,
   (u - (1 - t) * step, v) in pass 0 (the full step even for a shortened last one, as the DLL); the result clamped to the domain. The
   early outs on the surface's bounding circle / surf_screen_box are not ported (they only reject; the surface was picked here).
   wk / xs / tol: surf_division's view inputs as the drawing has them (scene.c build_surf_wire). 1 = found */
static int srfedge_dts;   /* tests: script step srfdts / MCAMUI_SRFEDGE_DTS (chain_srf_edge) */
int dist_to_surf_uv(const Surf *s, const double pk[2], double maxd, double *uv, double *dout) {
    if (s->n < 0x90 || !(s->rec[0x6e] & 0x40)) return 0;
    const uint8_t *h = s->rec + 0x68; double u0, u1, v0, v1; memcpy(&u0, h + 8, 8); memcpy(&u1, h + 0x10, 8); memcpy(&v0, h + 0x18, 8); memcpy(&v1, h + 0x20, 8);
    double wk = vht, xs = view_h > 0 && vht > 0 ? view_h / vht : 1.0, tol = mc_systol.tol, best = maxd, a, c; int found = 0;
    for (int pass = 1; pass >= 0; pass--) {
        int ok = mcs_surf_division(&F, s, pass, wk, xs, xs, 5e-5, &a, &c);
        if (pass == 1 && h[1] == 0) a = (u1 - u0) * 0.5;
        if (pass == 0 && h[1] == 0) c = (v1 - v0) * 0.5;
        if (pass == 0 && s->kind == 7 && 2.0 < a) a = 2.0;
        if (!ok) continue;
        double sp = pass ? a : c, st = pass ? c : a, x = pass ? u0 : v0, xe = pass ? u1 : v1, y0 = pass ? v0 : u0, ye = pass ? v1 : u1;
        if (!(sp > 0) || !(st > 0)) continue;
        for (int xl = 0, nx = 0; !xl && nx < 100000; x += sp, nx++) {
            if (xe <= x) { xl = 1; x = xe; }
            double y = y0, pp[2] = { 0, 0 };
            for (int yl = 0, first = 1, ny = 0; !yl && ny < 100000; y += st, ny++) {
                if (ye <= y) { yl = 1; y = ye; }
                double S[3], P[2]; if (mcs_surf_eval(&F, s, pass ? x : y, pass ? y : x, S)) { first = 1; continue; }
                P[0] = vmat[0] * S[0] + vmat[1] * S[1] + vmat[2] * S[2]; P[1] = vmat[3] * S[0] + vmat[4] * S[1] + vmat[5] * S[2];   /* proj_iso_pt */
                double d;
                if (!first && dfl_hit(pk, pp, P, best, tol, &d)) {
                    double t = t_on_line2(pk, pp, P, tol); best = d; found = 1;
                    if (pass) { uv[0] = x; uv[1] = y - (1.0 - t) * st; } else { uv[0] = y - (1.0 - t) * st; uv[1] = x; }
                }
                pp[0] = P[0]; pp[1] = P[1]; first = 0;
            }
        }
    }
    if (found) { if (uv[0] < u0) uv[0] = u0; else if (u1 < uv[0]) uv[0] = u1; if (uv[1] < v0) uv[1] = v0; else if (v1 < uv[1]) uv[1] = v1; }
    if (dout) *dout = best;
    return found;
}
/* a picked surface in Single mode: chain_single (FUN_1801df0b0, decomp/mccore_chain_range.c l.5385-5418) with GetProdEnableBits & 2 clear (taken
   clear), config 6026 on and the entity a SURF_ID: FUN_1801e1a50(ent, (u, v) of the pick, 0) -> build_edgecrv_on_surf(ent, (u, v),
   surf_step_chord_dev, chain_section_stop_angle in degrees, 0, list) makes the edge curve as new database entities (main colour / level), then
   get_ent(list[0], mask 0x8e): the first curve (a point - a degenerate edge - is not taken: nothing is chained); the surfaces' selection bit 4
   cleared. The chain: eval_surf(u, v) and the curve's ends projected (proj_iso_pt), the end at the smaller screen distance (the end when equal) is
   the one entered (add_curve_to_chain(curve, ep)): the chain runs from the end nearer the pick; FUN_1801e8660(chain, !6002) as for any single pick;
   EraseArrows / draw_chain_direction. What the chaining core here cannot chain (a curve not parallel to XY, a spline outside Engrave) is left made
   and refused with a message (ours). The pick's (u, v), shading off: FUN_18013cbc0 on the surface's display-list points when its node is
   built (+0x10 = the generation FUN_1801391f0 succeeded; cui_srfdl.c / mcsrfdl.c, oracle-checked), else dist_to_surf (dist_to_surf_uv,
   untrimmed); shading on gl_Hit_Surfs on the make_surf_polygons triangles (cui_srfdl.c srfhit_pick; kinds whose mesh is not ported refused). A
   trimmed surface's edge: the trim-loop walk of build_edgecrv_on_surf (mcpartline.c ec_build_edgecrv_list, oracle-checked). Undo: Mastercam stores the curve with store_ent outside any UndoTransStart (none in
   run_chain_dlg / chain_single / mcmill's op commands; store_ent -> FUN_1802c7ce0 records nothing): here it is no undo record either;
   Cancel (cancel_chaining 0x1801dc6e0) does not delete it. */
static int chain_srf_edge(int e, float mx, float my) {
    const Surf *s = F.ents[e].srf >= 0 && F.ents[e].srf < F.nsurfs ? &F.surfs[F.ents[e].srf] : NULL;
    double u = 0, v = 0;
    if (!s || !chain_srf_pick()) return 1;
    /* the pick's (u, v) (user decisions CS-2 / CS-4: Mastercam's own calculation, else refuse). select_ent_univ (mastercam.dll
       0x18020f4a0 l.403-411): shading on -> gl_Hit_Surfs (FUN_180224220: Mastercam's shading triangles + project_pt_onto_surf);
       shading off -> FUN_18013cbc0 on the surface's display-list points (cui_srfdl.c / mcsrfdl.c: DlPoint records with (u, v) floats,
       built by FUN_1801391f0 -> FUN_18013a130 / FUN_18013a5e0) when the node is built (+0x10), else dist_to_surf (here
       dist_to_surf_uv, untrimmed; srfdl_dist_trimmed = FUN_1802de180 for kind 10; also the test step srfdts / MCAMUI_SRFEDGE_DTS, which keeps
       that branch exercised). */
    if (solid_mode != 0) {   /* gl_Hit_Surfs (cui_sel.c pick_surf_shaded -> cui_srfdl.c srfhit_pick: mcglhit.c + mccsurf.c cs_glhit_uv, oracle-checked
                                against the DLL); the (u, v) = select_ent_univ's buffer as the click takes it (CS-21). The same pick again here
                                (idempotent: the click's own select gave e) */
        if (pick_entity(mx, my) != e || !pick_srf_uv_ok) {
            if (!srfhit_ported(F.ents[e].srf)) cui_message("apprenticeCAM", "Picking this surface type in shaded display is not decoded yet (its shading triangles, make_surf_polygons); switch to wireframe to pick it.");
            else cui_message("apprenticeCAM", "Surface edge chaining with shading on: the pick's (u, v) would be a wireframe candidate's parameter left in select_ent_univ's buffer - NOT DECODED");
            return 1;
        }
        u = pick_srf_uv[0]; v = pick_srf_uv[1];
    } else {   double w[3], pk[2], uv[2], d;
        scene_pick(vp_x, 0, vp_w, vp_h, mx, my - vp_y, w);   /* a point on the cursor ray: its view x / y = the cursor's (proj_iso_pt) */
        pk[0] = vmat[0] * w[0] + vmat[1] * w[1] + vmat[2] * w[2]; pk[1] = vmat[3] * w[0] + vmat[4] * w[1] + vmat[5] * w[2];
        double ap = SEL_APERTURE_PX * (view_h > 0 ? vht / view_h : 1.0);   /* sel_dist in model units */
        const char *why = NULL;
        int r = (srfedge_dts || getenv("MCAMUI_SRFEDGE_DTS")) ? -1 : srfdl_pick(F.ents[e].srf, pk, ap, &d, uv, &why);
        if (r == -2) { char m[256]; snprintf(m, sizeof m, "Surface edge chaining: %s", why ? why : "NOT PORTED"); cui_message("apprenticeCAM", m); return 1; }
        if (r == 0) return 1;
        if (r == -1) {
            if (s->kind == 10) {   /* dist_to_surf's trimmed branch FUN_1802de180 */
                int t = srfdl_dist_trimmed(F.ents[e].srf, pk, ap, &d, uv, &why);
                if (t < 0) { char m[256]; snprintf(m, sizeof m, "Surface edge chaining: %s", why ? why : "NOT PORTED"); cui_message("apprenticeCAM", m); return 1; }
                if (t == 0) return 1;
            } else if (!dist_to_surf_uv(s, pk, ap, uv, NULL)) return 1;
        }
        u = uv[0]; v = uv[1]; }
    if (getenv("MCAMUI_CHAINDUMP")) { printf("    cd srfpick surf %d kind %d uv %.6f %.6f\n", e, s->kind, u, v); fflush(stdout); }
    McsSimpView *views = calloc((size_t)F.nfviews + 1, sizeof *views);
    for (int i = 0; i < F.nfviews; i++) { views[i].n = F.fviews[i].n; memcpy(views[i].m, F.fviews[i].m, sizeof views[i].m); }
    /* build_edgecrv_on_surf(.., chain_section_stop_angle * 57.29577951308233, ..) (FUN_1801e1a50; chain_section_stop_angle = config 6011 x
       0.01745329251994329, mccore_chain_cfg.c l.44): every curve it makes is stored (list order), list[0] is chained */
    PlCurve cv[64]; const char *why = NULL;
    int r = ec_build_edgecrv_list(&F, s, u, v, mc_systol.surf_step_chord_dev, cfg_sect_ang * 0.01745329251994329 * 57.29577951308233, mc_systol.tol, msf_nurbs_splines, views, F.nfviews, cv, 64, &why);
    if (r < 0) { char m[256]; snprintf(m, sizeof m, "Surface edge chaining: %s", why ? why : "NOT PORTED"); cui_message("apprenticeCAM", m); }
    int k = -1;
    for (int q = 0; q < r; q++) {
        /* no undo transaction: store_ent outside UndoTransStart (FUN_1802c7ce0 records nothing; FUN_1802df620 leaves none open after a commit) */
        V3 z = { 0, 0, 0 }; int kq = -1;
        if (cv[q].kind == 1) { V3 a = { cv[q].line[0], cv[q].line[1], cv[q].line[2] }; kq = mcam_new_ent(&F, E_POINT, a, a, z, 0, 0, 0, geo_level, geo_color); }
        else if (cv[q].kind == 2) { V3 a = { cv[q].line[0], cv[q].line[1], cv[q].line[2] }, b = { cv[q].line[3], cv[q].line[4], cv[q].line[5] }; kq = mcam_new_ent(&F, E_LINE, a, b, z, 0, 0, 0, geo_level, geo_color); }
        else if (cv[q].kind == 4) {
            McsSimplify *o = &cv[q].simp;
            if (o->view_is_new) { F.fviews = realloc(F.fviews, sizeof *F.fviews * (size_t)(F.nfviews + 1)); F.fviews[F.nfviews].n = o->view; memcpy(F.fviews[F.nfviews].m, o->view_m, sizeof o->view_m); F.nfviews++; }
            V3 cc = { o->c[0], o->c[1], o->c[2] }; kq = mcam_new_arc_view(&F, o->view, o->view_m, cc, o->r, o->a0, o->sw, geo_level, geo_color);
        }
        else if ((cv[q].kind == 8 || cv[q].kind == 0x80) && cv[q].rec) kq = mcam_new_curve(&F, cv[q].rec, cv[q].rn, geo_level, geo_color);
        if (kq >= 0) { attr_new_ent(kq); doc_dirty = 1; if (k < 0 && q == 0) k = kq; }
        plc_free(&cv[q]);
    }
    if (r > 0) scene_reupload();
    free(views);
    if (getenv("MCAMUI_CHAINDUMP") && k >= 0) { const Ent *ne = &F.ents[k];   /* tests: the made curve */
        printf("    cd srfedge surf %d uv %.4f %.4f -> ent %d type %d (%.4f,%.4f,%.4f)-(%.4f,%.4f,%.4f)\n", e, u, v, k, ne->type, ne->a.x, ne->a.y, ne->a.z, ne->b.x, ne->b.y, ne->b.z); fflush(stdout); }
    overlay_dirty = 1;
    if (k < 0 || F.ents[k].type == E_POINT) return 1;   /* get_ent(.., 0x8e): no curve */
    double S[3]; { double D[10][3]; if (mcsn_eval(&F, s, 0, u, v, D)) return 1; memcpy(S, D[0], sizeof S); }
    const Ent *ce = &F.ents[k]; float px, py, ax, ay, bx, by;
    if (!scene_to_screen(vp_w, vp_h, S[0], S[1], S[2], &px, &py) || !scene_to_screen(vp_w, vp_h, ce->a.x, ce->a.y, ce->a.z, &ax, &ay) || !scene_to_screen(vp_w, vp_h, ce->b.x, ce->b.y, ce->b.z, &bx, &by)) return 1;
    int ep = hypot(px - bx, py - by) <= hypot(px - ax, py - ay);
    win_group_n = 0;                                 /* a pick event clears +0x1f0ac */
    /* chain_single takes the curve whatever its kind and plane (no plane filter in FUN_1801df0b0; CS-7: cam_chain_all for an operation) */
    cam_chain_splines = cui_spline_chaining(); cam_chain_views = cam_mode && !ex_mode && !geo_chaining; cam_chain_all = cui_all_chaining();
    if (chain_off_consumer(&F.ents[k])) { cam_chain_views = 0; cam_chain_splines = 0; cam_chain_all = 0; return 1; }
    int ok = ncchains < 256 && cam_chain_single(&F, k, cfg_chain_tol, &cchains[ncchains]);
    cam_chain_views = 0; cam_chain_splines = 0; cam_chain_all = 0;
    if (!ok) { cam_msg[0] = 0; return 1; }
    cchains[ncchains].rev[0] = ep;
    if (!cfg_chain_cursor) cam_chain_set_dir(&F, &cchains[ncchains], cfg_chain_dir == 1);   /* FUN_1801e8660(chain, !6002): every kind (CS-7) */
    ncchains++; chain_prompt_upd();
    return 1;
}
int chain_click_ent(int e, float mx, float my) {
    const Ent *en = &F.ents[e];
    if (cam_type_uses_holes(cam_mode)) {
        double x, y, d = 0;
        if (en->type == E_POINT && cam_mode == CAM_DRILL) { x = en->a.x; y = en->a.y; }
        else if (en->type == E_ARC && !en->vw) { x = en->c.x; y = en->c.y; d = 2 * en->r; }   /* a view arc: no XY hole (ours) */
        else { cam_msg[0] = 0; return 1; }
        /* Drill Point Selection, manual mode: each pick adds its point (removing is Unselect / Unselect all; the earlier
           click-again-to-remove was ours) */
        if (ncholes < 2048) { choles[ncholes].x = x; choles[ncholes].y = y; choles[ncholes].d = d; ncholes++; overlay_dirty = 1; }
    } else if (cam_mode || (ex_mode && !ex_phase) || geo_chaining) {
        if (en->type == E_SURF) return chain_srf_edge(e, mx, my);   /* Single mode, config 6026 (chain_srf_pick) */
        double wp[3]; scene_pick(vp_x, 0, vp_w, vp_h, mx, my - vp_y, wp);
        /* the cursor in the view for the pick's parameter on a curve outside the C-plane (select_ent_univ: proj_iso_pt; cam2d.c ent_pick_t) */
        memcpy(cam_chain_pick_vm, vmat, sizeof cam_chain_pick_vm);
        cam_chain_pick_pk[0] = vmat[0] * wp[0] + vmat[1] * wp[1] + vmat[2] * wp[2]; cam_chain_pick_pk[1] = vmat[3] * wp[0] + vmat[4] * wp[1] + vmat[5] * wp[2];
        cam_chain_pick_view = 1;
        return chain_pick_at(e, wp[0], wp[1]);
    } else return 0;
    overlay_dirty = 1;
    return 1;
}
/* run_chain_dlg: a pick adds a chain - Chain mode FUN_1801ddfb0 / Single mode FUN_1801df0b0; entities already in a
   chain can be picked again (the 0x40 bits are cleared per chain); removing is Unselect / Unselect All. (wx, wy) = the
   pick's world point (chain_click_ent: the view's; the scripts' draw:CMD|cN: the entity's midpoint) */
static int chain_pick_at_(int e, double wx, double wy) {
    if (e < 0 || e >= F.nents || !F.ents[e].type) return 0;
    const Ent *en = &F.ents[e];
    double wp[3] = { wx, wy, 0 };
    {
        if (chain_single == 3) return 1;                  /* Window: the window is dragged, not clicked */
        /* ours: Dynamic / Partial / the branch extension measure the pick on the entity in the XY plane (cam2d.c
           ent_pick_t); an arc in another plane (Ent.vw) is not taken by them (3D chain picks NOT DECODED) */
        if (en->type == E_ARC && en->vw && ((dyn_state && ncchains) || chain_single == 2 || (chain_single == 0 && ncchains && cchains[ncchains - 1].branch))) {
            cui_message("apprenticeCAM", "An arc in another plane cannot be used by this chaining mode here (NOT PORTED)"); return 1; }
        /* CS-7: the same for a line / spline not parallel to XY (Dynamic's cut, Partial's path picks and the branch extension measure in XY here) */
        if (cam_chain_all && (en->type == E_LINE || en->type == E_SPLINE) && cam_ent_tilted(en) && ((dyn_state && ncchains) || chain_single == 2 || (chain_single == 0 && ncchains && cchains[ncchains - 1].branch))) {
            cui_message("apprenticeCAM", "A curve outside the XY planes cannot be used by this chaining mode here (NOT PORTED)"); return 1; }
        if (en->type == E_SPLINE && dyn_state == 2 && ncchains) {   /* cam2d.c cam_chain_dynamic refuses a cut inside a spline */
            cui_message("apprenticeCAM", "A Dynamic start / end inside a spline curve is not supported here (NOT PORTED)"); return 1; }
        if (dyn_state && ncchains) {                      /* Dynamic (FUN_1801ec820) */
            Chain *c = &cchains[ncchains - 1];
            if (dyn_state == 1) {                         /* the arrow: the nearer chain end (help: click the start or end point) */
                double *xy, z; int n = cam_chain_polyline(&F, c, 0.01, &xy, &z);
                double ds = hypot(xy[0] - wp[0], xy[1] - wp[1]), de = hypot(xy[2 * n - 2] - wp[0], xy[2 * n - 1] - wp[1]); free(xy);
                dyn_end = de < ds; dyn_state = 2; return 1;
            }
            if (cam_chain_dynamic(&F, c, dyn_end, e, wp[0], wp[1])) { c->branch = 0; overlay_dirty = 1; }   /* then FUN_1801ecb70 */
            dyn_state = 0; chain_prompt_upd(); return 1;
        }
        if (chain_single == 2 && partial_waiting && ncchains) {   /* Partial + Wait: further picks extend the chain */
            if (cam_chain_partial_more(&F, &cchains[ncchains - 1], e, cfg_chain_tol)) overlay_dirty = 1;
            else cui_message("apprenticeCAM", "Partial chain: path between first and last entity not found");
            chain_prompt_upd(); return 1;
        }
        win_group_n = 0;                                 /* a pick event clears +0x1f0ac (LAB_1801f18b8) */
        cam_chain_plane_z = grid_z();
        /* C-plane radio on (chain_plane_filter): Chain mode FUN_1801ddfb0 / Partial FUN_1801db3f0 take only an entity
           parallel to the C-plane (FUN_1801fa6e0), else post_warning 4213 */
        double dep;
        if (cam_chain_plane_filter && (chain_single == 0 || chain_single == 2) && cam_ent_chainable(en) && !cam_ent_cplane(&F, en, cam_chain_plane_tol, &dep)) {
            cui_message("apprenticeCAM", "The selected entity is not parallel to the Cplane (plane mask is enabled)"); return 1; }
        if (chain_single == 2) {                         /* Partial (FUN_1801db3f0): first pick, then the last entity */
            if (partial_e1 < 0) { partial_e1 = e; partial_px = wp[0]; partial_py = wp[1]; chain_prompt_upd(); return 1; }   /* then 4271 */
            int ok2 = ncchains < 256 && cam_chain_partial(&F, partial_e1, partial_px, partial_py, e, cfg_chain_tol, &cchains[ncchains]);
            partial_e1 = -1;
            if (ok2) { ncchains++; overlay_dirty = 1; if (chain_wait) partial_waiting = 1; }
            else cui_message("apprenticeCAM", "Partial chain: path between first and last entity not found");   /* post_warning 4171 */
            chain_prompt_upd(); return 1;
        }
        if (chain_single == 0 && ncchains && cchains[ncchains - 1].branch) {   /* run_chain_dlg -> FUN_1801e3850 */
            if (cam_chain_extend(&F, &cchains[ncchains - 1], e, cfg_chain_tol)) overlay_dirty = 1;
            else cui_message("apprenticeCAM", "The selected sub-chain does not touch the branch point");   /* post_warning 4175 */
            chain_prompt_upd(); return 1;
        }
        /* ours: for an operation (not Extrude / the wireframe chain functions) arcs in another plane (Ent.vw) can be chained;
           chain_walk takes the seed as stored (rev 0: it starts at the seed's start point and runs in its stored sweep
           direction; cam2d.c ent_pick_t gives 0 for a view arc, so the pick side does not choose the end) and grows from
           its end; cam_chain_orient (the pick side, add_chain's ep_n_from_t_sel) / set_dir (CW / CCW in the C-plane) /
           start at point measure in the C-plane (XY) and are skipped for such a chain - the pick side's rule for an
           entity outside the C-plane is NOT DECODED */
        if (chain_off_consumer(en)) return 1;
        cam_chain_views = cam_mode && !ex_mode && !geo_chaining;
        int ok = ncchains < 256 && (chain_single ? cam_chain_single(&F, e, cfg_chain_tol, &cchains[ncchains])
                                                 : cam_chain(&F, e, cfg_chain_tol, &cchains[ncchains]));
        cam_chain_views = 0;
        if (ok && chain_has_view_arc(&cchains[ncchains])) {   /* the pick side not measured (above); FUN_1801e8660 is every kind's (CS-7) */
            if (!cfg_chain_cursor) cam_chain_set_dir(&F, &cchains[ncchains], cfg_chain_dir == 1);
            ncchains++; }
        else if (ok) {
            cam_chain_orient(&F, &cchains[ncchains], e, wp[0], wp[1]);
            if (!cfg_chain_cursor) cam_chain_set_dir(&F, &cchains[ncchains], cfg_chain_dir == 1);   /* add_chain: FUN_1801e8660(!cursor) */
            if (cam_chain_start_pt && !chain_single) cam_chain_start_at_point(&F, &cchains[ncchains], cfg_chain_tol);   /* add_chain l.2125: FUN_1801e8be0 (not chain_single) */
            ncchains++; }
        else cam_msg[0] = 0;
    }
    overlay_dirty = 1;
    return 1;
}
/* build_overlay: an entity in a chain of the chaining (draw_chain_sel) */
int ov_chained(int e) {
    for (int i = 0; i < ncchains; i++) if (!cchains[i].sld) for (int k = 0; k < cchains[i].n; k++) if (cchains[i].ent[k] == e) return 1;
    return 0;
}
/* the picked drill points: tp_pt_mgr draws them in colour 0xe (FUN_18006c980(op, 0xe)); the marker shape (a cross of
   1 % of the view here) is NOT DECODED (mcamui ov_extra) */
void chain_ov_holes(float **pv, int *pn, int *pcap) {
    float pc[3] = { 1, 1, 0 }; mc_rgb(14, pc);
    for (int i = 0; i < ncholes; i++) {
        double d = ext * 0.01; V3 a = { choles[i].x - d, choles[i].y, 0 }, b = { choles[i].x + d, choles[i].y, 0 }, c = { choles[i].x, choles[i].y - d, 0 }, e2 = { choles[i].x, choles[i].y + d, 0 };
        ov_pt(pv, pn, pcap, a, pc[0], pc[1], pc[2]); ov_pt(pv, pn, pcap, b, pc[0], pc[1], pc[2]);
        ov_pt(pv, pn, pcap, c, pc[0], pc[1], pc[2]); ov_pt(pv, pn, pcap, e2, pc[0], pc[1], pc[2]);
    }
}

/* chain direction arrows: draw_chain_direction -> DisplayChainArrow(start, direction) -> DisplayArrow -> FUN_1801ff3a0
   (mastercam.dll): CncGaf::GrArrow (gafgr) of length ChainArrowRatio[ChainArrowSize] x view height = 0.1 x view
   height (mccore table {0.05, 0.075, 0.1}, size 2), width 8 - ratio x -50 = 13 px; GrArrow::Regenerate: head cone
   13 x 1.07 px long, radius half of that, shaft cylinder radius head / 5; colour palette 4, or 86 when
   (gr_back_color - 2) & 0xf7 != 0 (config 5007); opaque (ChainArrowTranslucent 0). Drawn flat here (the shaded
   cone / cylinder are not ported); head at the far end - the orientation is NOT confirmed from the code.
   Start arrow (draw_chain_start_direction); open chains also get the end arrow (draw_chain_end_direction,
   FUN_1801e20d0: DisplayChainArrow at the chain end along its direction, colour 0xc = palette 12). Window coordinates
   (vp_* = the view); the shaft is a line of width w from (x0, y0) to (x1, y1), the head the triangle t. */
int chain_arrows(ChainArrow *out, int cap) {
    int na = 0;
    float sc[3], ec[3];
    mc_rgb(((cfg_back_color - 2) & 0xf7) != 0 ? 86 : 4, sc); mc_rgb(12, ec);
    for (int i = 0; i < ncchains; i++) {
        if (!sld_chain_arrows(i)) continue;            /* solid chains: the last solid pick's arrows only (cui_sldchain.c) */
        double *xy, z; int n = cam_chain_polyline(&F, &cchains[i], ext * 1e-4, &xy, &z);
        double *p3 = NULL; int n3 = chain_is_3d(&cchains[i]) ? chain_polyline3(&cchains[i], ext * 1e-4, &p3) : 0;   /* a view chain: in 3D */
        if (n3) { free(xy); xy = NULL; n = n3; }
        for (int end = 0; end < (cchains[i].closed ? 1 : 2) && n >= 2; end++) {
            int i0 = end ? n - 1 : 0, i1 = end ? n - 2 : 1;
            float ax, ay, bx, by;
            if (n3 ? scene_to_screen(vp_w, vp_h, p3[3 * i0], p3[3 * i0 + 1], p3[3 * i0 + 2], &ax, &ay) && scene_to_screen(vp_w, vp_h, p3[3 * i1], p3[3 * i1 + 1], p3[3 * i1 + 2], &bx, &by)
                   : scene_to_screen(vp_w, vp_h, xy[2 * i0], xy[2 * i0 + 1], z, &ax, &ay) && scene_to_screen(vp_w, vp_h, xy[2 * i1], xy[2 * i1 + 1], z, &bx, &by)) {
                if (end) { bx = 2 * ax - bx; by = 2 * ay - by; }       /* direction of travel at the end */
                float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
                if (l > 1e-6f && na < cap) {
                    dx /= l; dy /= l; ax += vp_x; ay += vp_y;
                    float L = 0.1f * vp_h, h = 13 * 1.07f, hr = h / 2, sr = h / 5, nx = -dy, ny = dx;
                    float ex_ = ax + dx * (L - h), ey = ay + dy * (L - h);
                    ChainArrow *a = &out[na++];
                    a->x0 = ax; a->y0 = ay; a->x1 = ex_; a->y1 = ey; a->w = 2 * sr;
                    a->t[0] = ex_ + nx * hr; a->t[1] = ey + ny * hr; a->t[2] = ex_ - nx * hr; a->t[3] = ey - ny * hr; a->t[4] = ax + dx * L; a->t[5] = ay + dy * L;
                    memcpy(a->rgb, end ? ec : sc, sizeof a->rgb);
                }
            }
        }
        free(xy); free(p3);
    }
    return na;
}

/* ------------------------------------------------------------------------------------------------ the Chaining dialog */
/* mccore CChainDlg (decomp/mccore_chain_range.c; message map @0x180d286d8). FUN_1801f06d0 creates it from template
 * mccoreres 6464 "Chaining" (143 x 321 dialog units, collapsed) when +0x1f2b5 = 0, 1058 (143 x 428, expanded: adds the
 * Start / End groups with Back / Forward and Dynamic) when set; 6518 / 6519 are the solids layouts. +0x1f2b5 starts from
 * DAT_1813c4bb0 (bss -> collapsed at the first chaining of a session; FUN_1801ec0c0 saves it on close). UIVertDialog
 * (uictrls.dll) shows the toggle as the title-bar icon: LoadIconA 0x1b6d (collapsed) / 0x1b6e (expanded), a click =
 * WM_SYSCOMMAND SC_MOUSEMENU -> OnSysCommand toggles the state and posts WM_COMMAND 358 (FUN_1801ece50: +0x1f2b5 flipped,
 * the dialog re-created); hover tooltip m_expand / m_collapse = opmanres 7020 "Expand dialog" / 7021 "Contract dialog"
 * (mastercam.dll 0x18028fe08 SetUICtrlStrings). Controls at their template positions, captions / tooltips from the
 * template ("|text" = a bitmap button and its tooltip). Button images: CChainDlg loads mccoreres bitmaps into CBitmap
 * members (mccore 0x1801eb240..0x1801eb940) and sets them on its CMFCButtons in OnInitDialog (0x1801ed2ef.., SetImage);
 * the members' control ids are DoDataExchange's (FUN_1801ec100: DDX_Control(id, member)):
 * Wireframe 537 -> 1133, Solids 351 -> 1142, Chain 527 -> 1134, Point 549 -> 1143, Window 25496 -> 1135, Area 531 -> 1144,
 * Single 529 -> 1136, Polygon 534 -> 1145, Vector 25481 -> 1137, Partial 535 -> 1146, Chain Feature 9990 -> 10095, Chain
 * Feature Options 9991 -> 10098, Unselect All 10204 -> 10205, End chain 12321 -> 7004, Start / End Back 542 / 543 ->
 * 1139, Forward 561 / 545 -> 1147, Dynamic 556 -> 1140, Unselect 541 -> 1216, Reverse 25381 -> 1123, Last 525 -> 1138.
 * Wait 540 is a check box (DDX_Check +0x17e8), C-plane 1482 / 3D 1499 a radio group (DDX_Radio +0x17e0: 0 = C-plane);
 * Options 550, OK, Help have no image member and show their text.
 * Handlers (message map): 527 Chain FUN_1801ec690 (mode 1), 529 Single FUN_1801edd90 (2), 531 Area FUN_1801ec590 (4),
 * 25496 Window FUN_1801ee260 (5), 534 Polygon FUN_1801edc00 (6), 535 Partial FUN_1801edb60 (7), 25481 Vector
 * FUN_1801ee1e0 (8), 549 Point FUN_1801edbb0 (9), 1482 C-plane FUN_1801ec7e0 / 1499 3D FUN_1801ec560 (chain_plane_filter
 * 1 / 0), 540 Wait FUN_1801ee230, 525 Last FUN_1801ed6b0, 12321 End chain FUN_1801ecb70, 541 Unselect FUN_1801ee030,
 * 10204 Unselect All FUN_1801ee0f0, 25381 Reverse FUN_1801edc50, 542 / 561 Start Back / Forward FUN_1801ede80 /
 * FUN_1801edf60, 543 / 545 End Back / Forward FUN_1801ecd00 / FUN_1801ecda0, 556 Dynamic FUN_1801ec820, 550 Options
 * FUN_1801eda80, 358 expand FUN_1801ece50, 538 combo FUN_1801edd10, OK FUN_1801ed940; enabling FUN_1801f00d0.
 * Solids 351 (FUN_1801edde0 -> template 6518, the solid selection of mcgeomsld OnSolidsSelect* / sld_array_pick_loops):
 * core_ui cui_sldchain.c (formats/chaining.md "Solid chaining"); live as FUN_1801f00d0 (solid chaining allowed and
 * sld_get_num_solids_visible() >= 1). Not implemented (greyed): Chain Feature 9990 (FUN_1801ec6d0 -> run_chain_dlg msg 10:
 * CFChain feature search FUN_1801f2860 / FUN_1801f54b0 / FUN_1801f4050, decomp/mccore_chain_feat_opts.c: NOT PORTED),
 * Chain Feature Options 9991 (FUN_1801ec710: ChainFeatureDlg, mccoreres dialog 9436, config 6800-6814: NOT PORTED),
 * Help 9 (Mastercam's help file).
 * bmp: > 0 the mccoreres bitmap, 0 a text button, -1 radio, -2 the Window / Polygon mode combo, -3 a group box, -4 a
 * check box. */
const ChCtl CH_1058[] = {
    { 537, 32, 13, 37, 19, 1133, "Wireframe" }, { 351, 73, 13, 37, 19, 1142, "Solids" },
    { 1482, 38, 44, 38, 10, -1, "C-plane" }, { 1499, 80, 44, 25, 10, -1, "3D" },
    { 527, 32, 63, 37, 19, 1134, "Chain" }, { 549, 73, 63, 37, 19, 1143, "Point" },
    { 25496, 32, 86, 37, 19, 1135, "Window" }, { 531, 73, 86, 37, 19, 1144, "Area" },
    { 529, 32, 109, 37, 19, 1136, "Single" }, { 534, 73, 109, 37, 19, 1145, "Polygon" },
    { 25481, 32, 132, 37, 19, 1137, "Vector" }, { 535, 73, 132, 37, 19, 1146, "Partial" },
    { 538, 27, 159, 48, 12, -2, "" }, { 540, 85, 160, 31, 10, -4, "Wait" },
    { 525, 32, 182, 36, 18, 1138, "Last" }, { 12321, 73, 182, 36, 18, 7004, "End chain" },
    { 541, 32, 203, 36, 18, 1216, "Unselect" }, { 10204, 73, 203, 36, 18, 10205, "Unselect All" },
    { 25381, 53, 224, 36, 18, 1123, "Reverse" },
    { 9991, 32, 246, 36, 18, 10098, "Chain Feature Options" }, { 9990, 73, 246, 36, 18, 10095, "Chain Feature" },
    { -10, 7, 268, 129, 39, -3, "Start" }, { 542, 32, 282, 36, 18, 1139, "Back" }, { 561, 73, 282, 36, 18, 1147, "Forward" },
    { 556, 53, 311, 36, 18, 1140, "Dynamic" },
    { -11, 7, 333, 129, 39, -3, "End" }, { 543, 32, 345, 36, 18, 1139, "Back" }, { 545, 73, 345, 36, 18, 1147, "Forward" },
    { 550, 53, 375, 36, 18, 0, "Options" }, { 1, 33, 400, 36, 18, 0, "OK" }, { 9, 74, 400, 36, 18, 0, "Help" },
};
const ChCtl CH_6464[] = {                          /* collapsed: mccoreres dialog 6464 (143 x 321) */
    { 537, 32, 13, 37, 19, 1133, "Wireframe" }, { 351, 73, 13, 37, 19, 1142, "Solids" },
    { 1482, 38, 44, 38, 10, -1, "C-plane" }, { 1499, 80, 44, 25, 10, -1, "3D" },
    { 527, 32, 63, 37, 19, 1134, "Chain" }, { 549, 73, 63, 37, 19, 1143, "Point" },
    { 25496, 32, 86, 37, 19, 1135, "Window" }, { 531, 73, 86, 37, 19, 1144, "Area" },
    { 529, 32, 109, 37, 19, 1136, "Single" }, { 534, 73, 109, 37, 19, 1145, "Polygon" },
    { 25481, 32, 132, 37, 19, 1137, "Vector" }, { 535, 73, 132, 37, 19, 1146, "Partial" },
    { 538, 27, 159, 48, 12, -2, "" }, { 540, 85, 160, 31, 10, -4, "Wait" },
    { 525, 32, 182, 36, 18, 1138, "Last" }, { 12321, 73, 182, 36, 18, 7004, "End chain" },
    { 541, 32, 203, 36, 18, 1216, "Unselect" }, { 10204, 73, 203, 36, 18, 10205, "Unselect All" },
    { 25381, 53, 224, 36, 18, 1123, "Reverse" },
    { 9991, 32, 246, 36, 18, 10098, "Chain Feature Options" }, { 9990, 73, 246, 36, 18, 10095, "Chain Feature" },
    { 550, 53, 268, 36, 18, 0, "Options" }, { 1, 33, 291, 36, 18, 0, "OK" }, { 9, 74, 291, 36, 18, 0, "Help" },
};
const int CH_1058_N = sizeof CH_1058 / sizeof CH_1058[0], CH_6464_N = sizeof CH_6464 / sizeof CH_6464[0];
/* the window / polygon selection modes of combo 538 (mccoreres 4224-4228; win_sel_mode 1..5) */
const char *const CH_WIN_MODES[5] = { "Inside", "In + Intrs", "Intersect", "Out + Intrs", "Outside" };
/* the template of the dialog as it is now (expanded 1058 / collapsed 6464) and its size in dialog units */
const ChCtl *chain_dlg_template(int *n, int *h) {
    if (sld_mode) { if (n) *n = CH_6518_N; if (h) *h = 286; return CH_6518; }   /* FUN_1801f06d0: 6518 with +0x17f8 */
    if (n) *n = chain_dlg_exp ? CH_1058_N : CH_6464_N;
    if (h) *h = chain_dlg_exp ? 428 : 321;
    return chain_dlg_exp ? CH_1058 : CH_6464;
}

/* world size of n screen pixels (chain_to_pock_bound's tolerance 2.0 / get_x_scr_scl() in the nesting functions) */
static double chain_px_world(double n) {
    double a[3], b[3]; scene_pick(vp_x, 0, vp_w, vp_h, 0, 0, a); scene_pick(vp_x, 0, vp_w, vp_h, (float)n, 0, b);
    double d = hypot(b[0] - a[0], b[1] - a[1]); return d > 0 ? d : 0.01;
}
/* the last of the dialog's chains (get_last_chain of the CHAIN list: solid chains are not in it) */
static Chain *last_wf_chain(void) { for (int i = ncchains - 1; i >= 0; i--) if (!cchains[i].sld) return &cchains[i]; return NULL; }
static int n_wf_chains(void) { int n = 0; for (int i = 0; i < ncchains; i++) if (!cchains[i].sld) n++; return n; }
/* run_chain_dlg's end sorts the dialog's CHAINs; the solid chains are inserted afterwards at the position they were
   made at (sld_unassigned_toolp_to_chains: insert_chain at chain index plVar9[0x2d], kept by sld_chain_pos_removed) */
static void sort_wf_then_solids(double ptol) {
    Chain wf[256], sd[256]; int pos[256], nw = 0, ns = 0;
    for (int i = 0; i < ncchains; i++) { if (cchains[i].sld) { pos[ns] = i - ns; sd[ns++] = cchains[i]; } else wf[nw++] = cchains[i]; }
    if (cfg_sort_mode && cam_chain_sort_nested(&F, wf, nw, cfg_sort_mode, ptol) < 0)
        cui_message("apprenticeCAM", "Only closed chains may be selected when Sort inside to outside/outside to inside is enabled");
    if (cfg_rev_inner) cam_chain_reverse_inner(&F, wf, nw, ptol);
    int w = 0, n = 0;
    for (int j = 0; j < ns; j++) { while (w < pos[j] && w < nw) cchains[n++] = wf[w++]; cchains[n++] = sd[j]; }
    while (w < nw) cchains[n++] = wf[w++];
    ncchains = n;
}
static void chain_act_(int id) {
    if (sld_mode) {                                      /* the 6518 controls (cui_sldchain.c) */
        if (id == 537) { sld_leave(); overlay_dirty = 1; chain_prompt_upd(); return; }   /* FUN_1801ee2b0 */
        if (id == 1) { sld_commit(); sld_mode = 0; }   /* FUN_1801ed940: FUN_1801ecb70, OnSolidsSelectEnd, then the dialog's OK */
        else { sld_act(id); return; }
    }
    if (id == 351) { if (sld_allowed()) { sld_enter(); win_group_n = 0; } return; }   /* FUN_1801edde0 */
    Chain *lc = last_wf_chain();
    double ctol = cfg_chain_tol;
    /* handlers that post WM 0x8028 come back to run_chain_dlg as a 0x200 event, which clears +0x1f0ac (the Window /
       Polygon group Unselect removes); Wait, Reverse (not Partial), Start / End Back / Forward and the radios post nothing */
    if (id != 540 && id != 25381 && id != 542 && id != 561 && id != 543 && id != 545 && id != 1482 && id != 1499 && id != 541 && id != 358) win_group_n = 0;
    switch (id) {
    /* mode buttons: Chain (FUN_1801ec690), Single (FUN_1801edd90), Partial (FUN_1801edb60), Window (FUN_1801ee260 mode 5),
       Area (FUN_1801ec590 mode 4), Polygon (FUN_1801edc00 mode 6), Vector (FUN_1801ee1e0 mode 8), Point (FUN_1801edbb0 mode 9) */
    case 527: case 529: case 535: case 25496: case 531: case 534: case 25481: case 549: {
        int m = id == 527 ? 0 : id == 529 ? 1 : id == 535 ? 2 : id == 25496 ? 3 : id == 531 ? 4 : id == 534 ? 5 : id == 25481 ? 6 : 7;
        if (chain_single != m) { chain_single = m; partial_e1 = -1; if (m >= 5) poly_n = 0; chain_prompt_upd(); }
        break; }
    case 1482: cam_chain_plane_filter = 1; break;        /* C-plane (FUN_1801ec7e0): +0x17e0 = 0, chain_plane_filter = 1 */
    case 1499: cam_chain_plane_filter = 0; break;        /* 3D (FUN_1801ec560, wireframe): +0x17e0 = 1, chain_plane_filter = 0 */
    case 358:                                            /* expand / contract (UIVertDialog::OnSysCommand -> FUN_1801ece50) */
        chain_dlg_exp = !chain_dlg_exp; break;           /* the window takes the template's height */
    case 540: chain_wait = !chain_wait; if (!chain_wait) partial_waiting = 0; break;   /* FUN_1801ee230 */
    case 525:                                            /* Last (FUN_1801ed6b0): warning 4211 when Last was used and chains are selected */
        if (last_used && ncchains) { cui_message("apprenticeCAM", "The last chain was already selected"); break; }
        for (int i = 0; i < nlast_chains && ncchains < 256; i++) { Chain *d = &cchains[ncchains++]; *d = last_chains[i];
            d->ent = malloc(d->n * sizeof(int)); d->rev = malloc(d->n * sizeof(int)); memcpy(d->ent, last_chains[i].ent, d->n * sizeof(int)); memcpy(d->rev, last_chains[i].rev, d->n * sizeof(int)); }
        last_used = 1; overlay_dirty = 1; break;
    case 12321:                                          /* End chain (FUN_1801ecb70) */
        if (partial_waiting) partial_waiting = 0;
        else if (ncchains && cchains[ncchains - 1].branch) {
            cchains[ncchains - 1].branch = 0;
            if (!cfg_chain_cursor) cam_chain_set_dir(&F, &cchains[ncchains - 1], cfg_chain_dir == 1);
        } else if (chain_single == 2 && partial_e1 >= 0 && ncchains < 256) {
            if (cam_chain_single(&F, partial_e1, cfg_chain_tol, &cchains[ncchains])) {
                cam_chain_orient(&F, &cchains[ncchains], partial_e1, partial_px, partial_py); cchains[ncchains].partial = 1; ncchains++; overlay_dirty = 1; }
            partial_e1 = -1;
        }
        chain_prompt_upd(); break;
    case 541:                                            /* Unselect (FUN_1801ee030): the Window / Polygon group (+0x1f0ac, count +0x1f0aa) or the last chain */
        for (int k = 0; k < (win_group_n > 0 ? win_group_n : 1); k++) {   /* unselect_last_chain: the dialog's CHAINs (solid chains stay) */
            int i = ncchains - 1; while (i >= 0 && cchains[i].sld) i--; if (i < 0) break;
            cam_chain_free(&cchains[i]); memmove(cchains + i, cchains + i + 1, sizeof(Chain) * (ncchains - i - 1)); ncchains--; }
        win_group_n = 0; overlay_dirty = 1; break;
    case 10204:                                          /* MCMessageBox 5007 / 10206: the front end asks, Yes = chain_unselect_all */
        if (n_wf_chains() && cui_hooks.chain_ask_unselect_all) cui_hooks.chain_ask_unselect_all(); break;
    case 25381: if (lc) { chain_reverse(lc); overlay_dirty = 1; } break;   /* reverse_chain of the last chain */
    /* Start Back / Forward (FUN_1801ede80 / FUN_1801edf60), End Back / Forward (FUN_1801ecd00 / FUN_1801ecda0); the
       buttons are enabled as FUN_1801f00d0 (chain_ctl_live) */
    case 542: if (lc) { if (lc->closed) cam_chain_move_start(lc, 0); else cam_chain_end_move(&F, lc, 0, 0, ctol); overlay_dirty = 1; } break;
    case 561: if (lc) { if (lc->closed) cam_chain_move_start(lc, 1); else cam_chain_end_move(&F, lc, 0, 1, ctol); overlay_dirty = 1; } break;
    case 543: if (lc) { cam_chain_end_move(&F, lc, 1, 0, ctol); overlay_dirty = 1; } break;
    case 545: if (lc) { cam_chain_end_move(&F, lc, 1, 1, ctol); overlay_dirty = 1; } break;
    case 556: if (lc) { if (lc->closed) { dyn_state = 2; dyn_end = 0; } else dyn_state = 1; chain_prompt_upd(); } break;   /* FUN_1801ec820 */
    case 550: show_chain_opts = 1; break;                /* FUN_1801eda80 -> set_chain_options (dialog 405) */
    case 1:
        if (ncchains || (cam_mode == CAM_FACE && F.stock[0] > 5e-05 && F.stock[1] > 5e-05)) {   /* Facing: OK with no chain = the defined stock (facing_op_uses_stock) */
            /* run_chain_dlg end (l.18214-18229): nested_chain_sort_mode 1 / 3 sort_inside_to_outside, 2 / 4
               sort_outside_to_inside (all chains closed, else 4221), then Reverse inner chains (DAT_1813c4ca8 ->
               FUN_1801e83c0); nesting polygons at 2 screen pixels (chain_to_pock_bound(2.0 / get_x_scr_scl())) */
            double ptol = chain_px_world(2);
            int anysld = 0; for (int i = 0; i < ncchains; i++) if (cchains[i].sld) anysld = 1;
            if (anysld) sort_wf_then_solids(ptol);
            else {
                if (cfg_sort_mode && cam_chain_sort_nested(&F, cchains, ncchains, cfg_sort_mode, ptol) < 0)
                    cui_message("apprenticeCAM", "Only closed chains may be selected when Sort inside to outside/outside to inside is enabled");
                if (cfg_rev_inner) cam_chain_reverse_inner(&F, cchains, ncchains, ptol);
            }
            chain_dlg_done();
            if (ms_chain_ok()) break;                    /* a 3D op's containment / blend / curve chaining (msurf flow) */
            if (cui_hooks.chain_ok) cui_hooks.chain_ok();   /* mcamui: the Extrude command's ex_chains_done, else open_params (the op dialog) */
        }
        break;
    }
}
/* reverse_chain: the entities in the opposite order, each traversed the other way, the Dynamic cuts swapped */
void chain_reverse(Chain *lc) {
    for (int a = 0, z = lc->n - 1; a < z; a++, z--) { int te = lc->ent[a]; lc->ent[a] = lc->ent[z]; lc->ent[z] = te; int tr = lc->rev[a]; lc->rev[a] = lc->rev[z]; lc->rev[z] = tr; }
    for (int a = 0; a < lc->n; a++) lc->rev[a] = !lc->rev[a];
    { double t = lc->cut0; lc->cut0 = lc->cut1; lc->cut1 = t; }
}
/* Unselect All's Yes (run_chain_dlg msg 0xb -> FUN_1801eff00: all chains unselected) */
void chain_unselect_all(void) {
    if (sld_mode) { sld_clear(); return; }           /* Solids mode: OnSolidsSelectClear */
    int n = 0;                                       /* FUN_1801eff00 unlinks the dialog's CHAINs; solid chains stay */
    for (int i = 0; i < ncchains; i++) { if (cchains[i].sld) cchains[n++] = cchains[i]; else cam_chain_free(&cchains[i]); }
    if (n) { ncchains = n; ncholes = 0; overlay_dirty = 1; } else cam_clear_selection();
}

/* FUN_1801f00d0: 537 Wireframe enabled by +0x1f0b8 (param_6 != 2, so on here); with +0x1f2b5 (the expanded template) and
   a last chain (the partial chain in mode 7): Dynamic 556 = !(GetProdEnableBits() & 0x10) (mill: on), Start Back 542 /
   Forward 561 = closed (CHAIN +8) or partial (+10) or branch (+9), End Back 543 = branch or partial, End Forward 545 =
   partial; no last chain: all five off; End chain 12321 always on */
int chain_ctl_live(int id) {
    if (sld_mode) return sld_ctl_live(id);
    Chain *lc = last_wf_chain();
    switch (id) {
    case 351: return sld_allowed();                  /* +0x1f0b9 && allow_solid_chains && sld_get_num_solids_visible() >= 1 */
    case 537: case 527: case 529: case 535: case 25496: case 531: case 534: case 25481: case 549: case 1482: case 1499:
    case 538: case 540: case 525: case 12321: case 541: case 10204: case 25381: case 550: case 1: return 1;
    case 556: return lc != NULL;
    case 542: case 561: return lc && (lc->closed || lc->partial || lc->branch);
    case 543: return lc && (lc->partial || lc->branch);
    case 545: return lc && lc->partial;
    }
    return 0;
}
int chain_ctl_on(int id) {                         /* pressed state of the mode / toggle buttons */
    if (sld_mode) return sld_ctl_on(id);             /* FUN_1801edde0: 351 checked; FUN_1801ee2b0: 537 */
    switch (id) {
    case 537: return 1;
    case 527: return chain_single == 0; case 529: return chain_single == 1; case 535: return chain_single == 2;
    case 25496: return chain_single == 3; case 531: return chain_single == 4; case 534: return chain_single == 5;
    case 25481: return chain_single == 6; case 549: return chain_single == 7; case 540: return chain_wait;
    }
    return 0;
}

int chain_radio_on(int id) {                       /* DDX_Radio 1482 (+0x17e0 = !chain_plane_filter); Solids mode 3041 (+0x17e4 = !GetSolidChainingUseFace()) */
    if (sld_mode) return sld_ctl_on(id);
    return id == 1482 ? cam_chain_plane_filter != 0 : !cam_chain_plane_filter;
}

/* ------------------------------------------------------------------------------------------------ Chaining Options (405) */
/* mccoreres dialog 405 (143 x 403 dialog units), mccore CChainOptions (decomp/mccore_chain_feat_opts.c;
   set_chain_options 0x1801fab10 -> FUN_1801fa9a0: ctor FUN_1801f8a60, init FUN_1801f9a20, DDX FUN_1801f8ef0, OK
   FUN_1801f9680, message map @0x180d2a110). Session only - the config file is not written. The front ends lay the
   controls out from the template (mcamui chaindlg_ui.inc chain_opts_draw, viewer-qt chaindlg.cpp) and edit a ChainOpts. */
void chain_opts_init(ChainOpts *o) {             /* FUN_1801f9a20 */
    o->ent = cfg_ent_mask; o->col = cam_chain_color_filter; o->lev = cam_chain_level_filter; o->closed = cfg_chain_closed; o->open = cfg_chain_open;
    o->sect = cfg_sect_on; o->ang = cfg_sect_ang; o->ign = cam_chain_ignore_depths; o->spt = cam_chain_start_pt; o->brk = cfg_break_thread;
    o->surf = cfg_surf_single; o->dbrk = cfg_dyn_break; o->ccw = cfg_chain_dir == 1; o->cur = cfg_chain_cursor; o->zig = cfg_chain_zigzag;
    o->sort = cfg_sort_mode >= 0 && cfg_sort_mode <= 4 ? cfg_sort_mode : 0; o->inf = cfg_chain_infinite; o->rinv = cfg_rev_inner;
    o->sync = cfg_sync_mode >= 0 && cfg_sync_mode <= 6 ? cfg_sync_mode : 0;
    o->tol = cfg_chain_tol; o->ptol = cam_chain_plane_tol;
}
/* 2468 Start chain at point and 2520 Break closest entity to thread point exclude each other (FUN_1801f98f0 /
   FUN_1801f9230): the one switched on (which = 0 / 1) clears the other */
void chain_opts_exclusive(ChainOpts *o, int which) { if (which == 0 && o->spt) o->brk = 0; if (which == 1 && o->brk) o->spt = 0; }
void chain_opts_ok(const ChainOpts *o) {         /* FUN_1801f9680 */
    cfg_ent_mask = o->ent; cam_chain_color_filter = o->col; cam_chain_level_filter = o->lev; cam_chain_ignore_depths = o->ign; cam_chain_start_pt = o->spt;
    cfg_break_thread = o->brk; cfg_dyn_break = o->dbrk; cfg_chain_closed = o->closed; cfg_chain_open = o->open; cfg_sect_ang = o->ang; cfg_sect_on = o->sect;
    cfg_chain_dir = o->ccw; cfg_chain_cursor = o->cur; cfg_chain_zigzag = o->zig; cfg_chain_infinite = o->inf; cfg_surf_single = o->surf; cfg_rev_inner = o->rinv;
    cfg_chain_tol = o->tol; cam_chain_plane_tol = o->ptol; cfg_sync_mode = o->sync; cfg_sort_mode = o->sort;
    show_chain_opts = 0; win_group_n = 0;          /* then FUN_1801eda80 posts 9 */
}
/* combo 2901 Sorting (mccoreres 5132-5136) and 2447 Sync mode (mccoreres 4200-4206) */
const char *const CHO_SORT[5] = { "next closest", "inside to outside", "outside to inside", "optimize inside to outside ", "optimize outside to inside" };
const char *const CHO_SYNC[7] = { "None", "by Entity", "by Branch", "by Node", "by Point", "Manual", "Manual/density" };

/* ------------------------------------------------------------------------------------------------ Drill Point Selection (2300) */
/* mcmillres dialog 2300, CDrillPtSelection (mcmill tp_pt_mgr, decomp/mcmill_all.c @18007a6c0): Unselect (3244, the last
   point), Unselect all (3245); OK (with points) = the op dialog through cui_hooks.chain_ok */
void drill_unselect(void) { if (ncholes > 0) { ncholes--; overlay_dirty = 1; } }
void drill_unselect_all(void) { if (ncholes > 0) { ncholes = 0; overlay_dirty = 1; } }
void drill_ok(void) { if (ncholes && cui_hooks.chain_ok) cui_hooks.chain_ok(); }
/* the point ops show the Drill Point Selection dialog instead of the Chaining dialog; the hole-type ops that use the
   Chaining window's OK only (helix / circle / thread: Drill Point Manager loop, mcamui chain_dlg_body) */
int chain_points_dlg(void) { return cam_type_uses_holes(cam_mode); }
int chain_points_ok_only(void) { return cam_type_uses_holes(cam_mode) || cam_mode == CAM_HELIX || cam_mode == CAM_CIRCLE || cam_mode == CAM_THREAD; }

/* ------------------------------------------------------------------------------------------------ config (mccore config setter FUN_1801b8270: key -> global) */
void cui_chain_config_item(int key, const char *val) {
    char yn = 0; sscanf(val, " %c", &yn);
    int v; double tv;
    if (key == 6000) cfg_chain_zigzag = yn == 'y'; else if (key == 6012) cfg_chain_open = yn == 'y';
    else if (key == 6013) cfg_chain_closed = yn == 'y'; else if (key == 6024) cfg_win_prompt = yn == 'y';
    else if (key == 6004) cfg_chain_infinite = yn == 'y'; else if (key == 6002) cfg_chain_cursor = yn == 'y';
    else if (key == 6018) cam_chain_color_filter = yn == 'y'; else if (key == 6019) cam_chain_level_filter = yn == 'y';
    else if (key == 6023) { chain_wait = yn == 'y'; cfg_chain_wait_def = chain_wait; }
    else if (key == 6020) cfg_chain_last_mode = yn == 'y';          /* chain_default_last_mode */
    else if (key == 6021) { cfg_chain_plane_def = yn == 'y'; cam_chain_plane_filter = cfg_chain_plane_def; }   /* DAT_1813c4ca9 -> chain_plane_filter */
    else if (key == 6003) cam_chain_ignore_depths = yn == 'y';      /* chain_ignore_depths */
    else if (key == 6005) cfg_rev_inner = yn == 'y';                /* DAT_1813c4ca8 */
    else if (key == 6007) cfg_dyn_break = yn == 'y';                /* chain_dynamic_break */
    else if (key == 6009) cam_chain_start_pt = yn == 'y';           /* chain_start_at_point */
    else if (key == 6010) cfg_break_thread = yn == 'y';             /* DAT_1813c4ca6 */
    else if (key == 6017) cfg_sect_on = yn == 'y';                  /* chain_section_stop_angle_on */
    else if (key == 6025) cfg_ent_mask = yn == 'y';                 /* chain_use_ent_mask */
    else if (key == 6026) cfg_surf_single = yn == 'y';              /* chain_surface_edges_in_single_mode */
    if (key == 17017) cfg_ms_mach_stl = yn == 'y';                 /* msurf_mach_stl ("CAD file?") */
    if (sscanf(val, "%d", &v) != 1) return;
    if (key == 17018) cfg_ms_toolcont = v;                          /* msurf_toolcont ("Containment?") */
    if (key == 17019) cfg_ms_all_geom = v;                          /* msurf_all_geom ("Toolpath/surface selection?") */
    if (key == 17023) cfg_ms_show_dlg = v;                          /* msurf_show_selection_dlg */
    if (key == 6001) cfg_chain_dir = v;
    if (key == 6014) cfg_chain_mode = v < 1 ? 1 : v;               /* chain_mode_default, < 1 -> 1 */
    if (key == 6006) cfg_sort_mode = v; if (key == 6008) cfg_sync_mode = v;   /* nested_chain_sort_mode, g_chain_sync_mode */
    if (key == 6011) { if (sscanf(val, "%lf", &tv) == 1) cfg_sect_ang = tv; }   /* chain_section_stop_angle (deg) */
}

/* ------------------------------------------------------------------------------------------------ script steps */
/* the chains as the tests compare them (both apps): per chain closed / open, the entities in order (r = traversed end
   -> start), start / end point, cuts, partial / branch; the drill points; the dialog mode / flags */
void chain_dump(void) {
    printf("    cd mode %d exp %d wait %d plane %d sel %d win %d dyn %d partial %d/%d last %d/%d holes %d cam %d ex %d/%d params %d\n",
           chain_single, chain_dlg_exp, chain_wait, cam_chain_plane_filter, win_sel_mode, win_pending, dyn_state, partial_e1, partial_waiting,
           nlast_chains, last_used, ncholes, cam_mode, ex_mode, ex_phase, show_params);
    for (int i = 0; i < ncchains; i++) {
        const Chain *c = &cchains[i];
        printf("    cd chain %d: %s%s%s,", i, c->closed ? "closed" : "open", c->partial ? " partial" : "", c->branch ? " branch" : "");
        for (int k = 0; k < c->n; k++) printf(" %d%s", c->ent[k], c->rev[k] ? "r" : "");
        double *xy = NULL, z = 0; int n = cam_chain_polyline(&F, c, 0.01, &xy, &z);
        if (chain_is_3d(c)) {                     /* a view chain: its 3D start / end */
            double *p3 = NULL; int m3 = chain_polyline3(c, 0.01, &p3);
            if (m3 > 0) printf(" | start3 %.3f,%.3f,%.3f end3 %.3f,%.3f,%.3f", p3[0], p3[1], p3[2], p3[3 * m3 - 3], p3[3 * m3 - 2], p3[3 * m3 - 1]);
            free(p3);
        } else if (n > 0) printf(" | start %.3f,%.3f end %.3f,%.3f", xy[0], xy[1], xy[2 * n - 2], xy[2 * n - 1]);
        printf(" | cut %.3f / %.3f", c->cut0, c->cut1);
        if (c->sld && c->n) { const Ent *a = &F.ents[c->ent[0]]; printf(" | sld%d z %.3f", c->sld, c->rev[0] ? a->b.z : a->a.z);
            for (int k = 0; k < c->n; k++) { const Ent *e = &F.ents[c->ent[k]]; V3 p = c->rev[k] ? e->b : e->a, q = c->rev[k] ? e->a : e->b;
                printf(" %s(%.3f,%.3f,%.3f>%.3f,%.3f,%.3f%s)", e->type == E_ARC ? "A" : "L", p.x, p.y, p.z, q.x, q.y, q.z, e->type == E_ARC ? (e->sw * (c->rev[k] ? -1 : 1) > 0 ? " ccw" : " cw") : ""); } }
        printf("\n");
        free(xy);
    }
    for (int i = 0; i < ncholes; i++) printf("    cd hole %d: %.4f,%.4f d %.4f\n", i, choles[i].x, choles[i].y, choles[i].d);
    if (chain_srf_pick()) printf("    cd srf mask on, hover %d%s\n", hover_ent, hover_ent >= 0 && hover_ent < F.nents && F.ents[hover_ent].type == E_SURF ? " (surface)" : "");   /* Single + config 6026 */
    sld_dump();
}
/* 1 = handled: chainmode:LABEL (the gallery), chainopt:N (the mode 0..7), chainopts (the Options dialog), chainact:ID (a
   dialog control), chaindyn (Dynamic), chains (the chain list); ours: chaindump (the full dump), chainyes (Yes to
   Unselect All), chainwinmode:1..5 (combo 538), drillun / drillunall / drillok (Drill Point Selection). chainat:X,Y
   needs the UI's click path and stays in each front end. */
int cui_chain_script(const char *st, const char *arg) {
    if (!strcmp(st, "chainmode") && arg) { if (!gallery_pick(arg)) printf("    (unknown op %s)\n", arg); }   /* chainmode:Contour - start chaining like the gallery */
    else if (!strcmp(st, "chainopt") && arg) { chain_single = atoi(arg); partial_e1 = -1; }   /* chainopt:0|1|2 = Chain / Single / Partial */
    else if (!strcmp(st, "chainopts")) show_chain_opts = 1;   /* open the Chaining Options dialog (screenshots) */
    else if (!strcmp(st, "chainact") && arg) chain_act(atoi(arg));   /* chainact:ID - a Chaining dialog control (chain_act: 549 Point, 1482 C-plane, 358 expand ...) */
    else if (!strcmp(st, "chaindyn")) { if (ncchains) { if (cchains[ncchains - 1].closed) { dyn_state = 2; dyn_end = 0; } else dyn_state = 1; chain_prompt_upd(); } }   /* the Dynamic button */
    else if (!strcmp(st, "chains")) { for (int i = 0; i < ncchains; i++) { printf("    chain %d: %s,", i, cchains[i].closed ? "closed" : "open"); for (int k = 0; k < cchains[i].n; k++) printf(" %d%s", cchains[i].ent[k], cchains[i].rev[k] ? "r" : ""); printf("\n"); } }
    else if (!strcmp(st, "chaindump")) chain_dump();
    else if (!strcmp(st, "chainyes")) { if (ncchains) chain_unselect_all(); }
    else if (!strcmp(st, "chainwinmode") && arg) { int m = atoi(arg); if (m >= 1 && m <= 5) win_sel_mode = m; }   /* chainwinmode:1..5 - combo 538 */
    else if (!strcmp(st, "drillun")) drill_unselect();
    else if (!strcmp(st, "drillunall")) drill_unselect_all();
    else if (!strcmp(st, "drillok")) drill_ok();
    /* a 3D op's drive / check prompt (msurf flow): drvsolid:N - a body pick on part solid N (1-based); drvface:N,F - a face pick (face F
       of this program's list, 0-based); drvsurf:N - surface N (1-based, F.surfs order) toggled; drvdone - End Selection (Enter); drvesc -
       Esc; drvfacemode:0|1 - the Face button's state set directly; selbar[:body|face] - a click on the Selection Bar's solid button; srfsel - the flow's dump; plgpt:X,Y - a point pick
       after the op dialog; bosscav:N - dialog 100's answer (0 Boss, 1 Cavity, 2 Undefined, -1 Cancel); cfgitem:KEY=VALUE - a config item */
    else if (!strcmp(st, "drvsolid") && arg) drv_toggle(atoi(arg) - 1);
    else if (!strcmp(st, "drvface") && arg) { int a = 0, b = 0; sscanf(arg, "%d,%d", &a, &b); drv_toggle_face(a - 1, b); }
    else if (!strcmp(st, "drvsurf") && arg) drv_toggle_surf(atoi(arg) - 1);
    else if (!strcmp(st, "drvdone")) drv_done();
    else if (!strcmp(st, "drvclick") && arg) {       /* drvclick:X,Y,Z - a click at the screen position of a world point (drv_click: the real pick path) */
        double x = 0, y = 0, z = 0; sscanf(arg, "%lf,%lf,%lf", &x, &y, &z); float sx, sy; vp_main();
        if (scene_to_screen(vp_w, vp_h, x, y, z, &sx, &sy)) drv_click(sx + vp_x, sy + vp_y);
    }
    else if (!strcmp(st, "drvclickface") && arg) {   /* drvclickface:N,F - a click on face F of part solid N (its largest triangle's centre) */
        int a = 0, b = 0; sscanf(arg, "%d,%d", &a, &b); float c[3], sx, sy; vp_main();
        if (sld_part_face_pt(a - 1, b, c) && scene_to_screen(vp_w, vp_h, c[0], c[1], c[2], &sx, &sy)) {
            int hs = -1, hf = -1; double hp[3]; int k = sld_pick_part_face(sx + vp_x, sy + vp_y, &hs, &hf, hp);
            printf("    srfsel click on solid %d face %d: the ray hits %s %d face %d\n", a, b, k == 1 ? "solid" : "nothing", hs + 1, hf);
            drv_click(sx + vp_x, sy + vp_y);
        } else printf("    srfsel click on solid %d face %d: not on the screen\n", a, b);
    }
    else if (!strcmp(st, "drvesc")) chain_cancel();
    else if (!strcmp(st, "drvfacemode") && arg) ms_sld_faces = atoi(arg) != 0;
    else if (!strcmp(st, "selbar")) {                /* selbar[:body|face] - a click on the Selection Bar's Select body / Select face button, then
                                                        the buttons' state (N13-8) */
        if (arg && !strcmp(arg, "body")) ms_selbar_toggle(0); else if (arg && !strcmp(arg, "face")) ms_selbar_toggle(1);
        printf("    srfsel selbar enabled %d body %d face %d\n", ms_selbar_enabled(), ms_sld_body, ms_sld_faces);
    }
    else if (!strcmp(st, "srfsel")) ms_dump();
    else if (!strcmp(st, "bosscav") && arg) cui_script_radio = atoi(arg);
    else if (!strcmp(st, "cfgitem") && arg) { int k = atoi(arg); const char *q = strchr(arg, '='); if (q) cui_config_item(k, q + 1); }
    else if (!strcmp(st, "srfdts")) srfedge_dts = 1;   /* tests: the dist_to_surf branch of the surface-edge pick (chain_srf_edge) */
    else if (!strcmp(st, "srfdl")) {                 /* tests: every surface's display-list node (cui_srfdl.c): built, records, loop records */
        for (int i = 0; i < F.nsurfs; i++) { int n = 0, nl = 0, b = srfdl_info(i, &n, &nl); printf("    cd srfdl surf %d kind %d built %d n %d loops %d\n", i, F.surfs[i].kind, b, n, nl); }
        fflush(stdout);
    }
    else if (!strcmp(st, "plgpt") && arg) { double x = 0, y = 0; sscanf(arg, "%lf,%lf", &x, &y); plg_click(x, y, 0); }
    else if (sld_script(st, arg)) ;                  /* sldclick / sldrefother / sldrefok / sldesc / slddump (cui_sldchain.c) */
    else return 0;
    return 1;
}
