/* cui_draft_ord.c - the Drafting tab's Ordinate group: Horizontal / Vertical / Parallel ordinate dimensions as Mastercam 2017's
 * DraftApp::doOrdinate (mccad FUN_18003e640, decomp/draft/cad_rundraft.c:1842-2534; its disassembly decomp/draft/ord_cad_asm.txt)
 * runs them: the picks, the zero (base) ordinate and its children, each new ordinate stored and then dragged to its
 * text position (FUN_180038c50 -> RunDraftCommand(99) -> the ordinate drag state 3: FUN_1800466a0 / FUN_180046920 set-up,
 * FUN_180046430 mouse move, FUN_180046380 events, FUN_180046740 store - decomp/draft/ord_cad_drag.c / ord_cad_drag2.c). The record is the
 * port of mccore calc_ordinate_dimension (viewer/draft_ord.c). Spec: ../../formats/drafting/create.md 8.
 *
 * Runs inside the wireframe command frame (cui_geo.c: geo_cmd = G_ORDH / G_ORDV / G_ORDP) like cui_draft.c; the session's drafting
 * entities, their save and the project file are cui_draft.c's (McamFile.dnew; an ordinate child keeps its base as ent[1] = -2 - the
 * base's dnew index, mcam_saveops.c writes the base's eptr at +0x108). "ours" marks glue of this program. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "draft_create.h"
#include "draft_ord.h"
#include "mcam_saveops.h"
#include "draft.h"

DcSettings *dr_globals(void);                           /* cui_draft.c: the session's drafting globals, fonts, DfAssoc */
const DcFonts *dr_stroke_fonts(void);
int dr_assoc_on(void);
static void al_pick(const double w[3]); static int al_move(const double world[3], int ctrl); static void al_accept(void); static void al_end_drag(void);
static int al_drag;
static double D_(const uint8_t *b, size_t o) { double v; memcpy(&v, b + o, 8); return v; }
static void v2w(const double m[9], const double v[3], double w[3]) { for (int i = 0; i < 3; i++) w[i] = m[i] * v[0] + m[3 + i] * v[1] + m[6 + i] * v[2]; }
static void w2v(const double m[9], const double w[3], double v[3]) { for (int i = 0; i < 3; i++) v[i] = m[3 * i] * w[0] + m[3 * i + 1] * w[1] + m[3 * i + 2] * w[2]; }

/* ------------------------------------------------------------------------------------------------ the commands */
/* RunDraftCommand -> FUN_1800392d0 (cad_rundraft.c:323-327): 0x9479 _ORD_HORIZ, 0x947b _ORD_VERT, 0x947a _ORD_PARA (and 0x9478
   _ORD_EXIST) run FUN_18003e640; its orientation local_82f4 = 1 for 0x947b, 2 for 0x947a, else 0 (asm 0x18003e9aa-0x18003e9df).
   _ORD_EXIST is the same loop in horizontal mode (its point: picking an existing base ordinate, cui_draft_pick.c); 0x9477 _ORD_ALIGN
   runs the dimension loop with type 3 (the Align section below); _ORD_WINDOW (FUN_18003e2f0, dialog 2533) is NOT DECODED: greyed */
int do_cmd(int c) { return c == G_ORDH || c == G_ORDV || c == G_ORDP || c == G_ORDE || c == G_ORDA; }
static int do_orient0(int c) { return c == G_ORDV ? 1 : c == G_ORDP ? 2 : 0; }
const char *do_label(int c) { return c == G_ORDH ? "Horizontal" : c == G_ORDV ? "Vertical" : c == G_ORDP ? "Parallel" : c == G_ORDE ? "Add To Existing" : c == G_ORDA ? "Align" : ""; }   /* ui/layout.txt l.306-311 */
int do_find_ft(const char *ft) {
    if (!ft) return 0;
    if (!strcmp(ft, "doDraft_DIMENSION_ORD_HORIZ")) return G_ORDH;
    if (!strcmp(ft, "doDraft_DIMENSION_ORD_VERT")) return G_ORDV;
    if (!strcmp(ft, "doDraft_DIMENSION_ORD_PARA")) return G_ORDP;
    if (!strcmp(ft, "doDraft_DIMENSION_ORD_EXIST")) return G_ORDE;
    if (!strcmp(ft, "doDraft_DIMENSION_ORD_ALIGN")) return G_ORDA;   /* the dimension loop, type 3 (create.md 8.1, 9.3) */   /* the same loop, horizontal mode (create.md 8.1) */
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the state */
/* the ordinate globals beyond cui_draft.c's: h_txt_aln (config 13002 "Horizontal?"), ord_neg_sign (12008 "Display negative sign?"),
   ord_arrowhd (14010 "Apply to ordinate dimensions?") - the config writer FUN_1801d44c0.. (core_cfgdraft.c:811, 860, 908) */
static int do_h_aln, do_neg, do_arrowhd, do_units = -1;
static DcSettings do_saved; static int do_saved_h, do_saved_neg, do_saved_arr;   /* DraftGlobalsSave at the start (cad_rundraft.c:2048) */
static int do_orient;                                   /* local_82f4 */
static int do_zero;                                     /* SI: the next pick makes a zero (base) ordinate */
static int do_base = -1;                                /* BL + local_7e48: the base ordinate's dnew index, -1 none */
static int do_nopick;                                   /* ours: the ordpt step */
static int do_fb; static uint8_t do_fbase[0x810];        /* BL with a base picked from the part (local_7e48 = its record; ours: a copy) */
static int do_phase;                                    /* R12: 1 pick, 2 the parallel zero's orientation point, 4 the text drag */
static uint8_t do_ap[0x98]; static int do_apent = -1;   /* local_2bc8: the picked point's aPoint, the entity it references */
static double do_p4[3], do_p5[3], do_p6[3];             /* RBP+0xf8 / 0xb0 / 0x98: the calc's p4 / p5 / p6 */
static double do_cp[3];                                 /* local_8288: the event point on the construction plane (MCEvent +0x110) */
static double do_depth; static int do_view = 1; static double do_vm[9];
/* the drag (state 3 of the dimension loop for the stored ordinate) */
static int do_dragk = -1;                               /* dragedit_eptr: the stored ordinate's dnew index */
static uint8_t do_drag[0x810]; static int do_drag_ok;   /* drag_ent, GetDfDragEntValid */
static DoInput do_din; static DoSettings do_dO;         /* DAT_1806959c0 / 95990 / 959a8 / 95978 / 93014 / 95958 / 49698 / 49694 / 93013 /
                                                           93020 and the globals of the drag (DraftGlobalsFromDim of the record) */
static uint8_t do_dbase[0x810];

static double cfgd_(int key, double def) { char v[128]; mcam_config_value(mcam_is_metric(&F), key, v, sizeof v); return v[0] ? atof(v) : def; }
static int cfgi_(int key, int def) { char v[128]; mcam_config_value(mcam_is_metric(&F), key, v, sizeof v); if (!v[0]) return def; if (v[0] == 'y' || v[0] == 'Y') return 1; if (v[0] == 'n' || v[0] == 'N') return 0; return atoi(v); }
static void do_globals_load(void) {
    int metric = mcam_is_metric(&F);          /* the units cui_draft.c dr_metric takes */
    if (do_units == metric) return;
    do_units = metric;
    do_h_aln = cfgi_(13002, 0); do_neg = cfgi_(12008, 0); do_arrowhd = cfgi_(14010, 0);
    (void)cfgd_;
}
static void do_settings(DoSettings *O) {                /* the current globals as calc_ordinate_dimension reads them */
    memset(O, 0, sizeof *O); O->S = *dr_globals();
    O->S.sel_dist = SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1);   /* sel_dist in world units (cui_snap.c) */
    O->S.color = geo_color; O->S.level = geo_level;      /* main_color / main_level */
    O->h_aln = do_h_aln; O->neg_sign = do_neg; O->arrowhd = do_arrowhd;
}
static void do_from_dim(const uint8_t *E) {             /* DraftGlobalsFromDim into the session globals (cad_rundraft.c:2343-2346) */
    DoSettings O; memset(&O, 0, sizeof O); O.S = *dr_globals();
    do_globals_from_dim(E, &O);
    double sd = dr_globals()->sel_dist; *dr_globals() = O.S; dr_globals()->sel_dist = sd;
    do_h_aln = O.h_aln; do_neg = O.neg_sign; do_arrowhd = O.arrowhd;
}

/* the scene: the session's drafting entities, the dragged one as the drag entity (FUN_180046920 erases it and draws drag_ent) */
static void do_scene(void) {
    int n = 0;
    for (int i = 0; i < F.ndnew; i++) n += F.dnew[i].on && i != do_dragk;
    int drag = do_dragk >= 0 && do_drag_ok;
    uint8_t *b = n + drag ? malloc((size_t)(n + drag) * 0x810) : NULL; int k = 0;
    for (int i = 0; i < F.ndnew; i++) if (F.dnew[i].on && i != do_dragk) memcpy(b + (size_t)k++ * 0x810, F.dnew[i].E, 0x810);
    if (drag) memcpy(b + (size_t)k++ * 0x810, do_drag, 0x810);
    scene_draft_session(b, k, 0x810);
    free(b); overlay_dirty = 1;
}

/* the prompts: picks (cad_rundraft.c:2085-2170, asm 0x18003eafa-0x18003f182): mccoreres 27730 / 27731 / 27732 "Create dimension,
   ordinate horizontal: / vertical: / parallel: " (27736 otherwise) + "\n" + 27733 "Indicate initial ordinate location" (zero) /
   27734 "Indicate position for next ordinate element" + "\n" + 27450 "Select an ordinate dimension". The orientation point (asm 0x18003ee80): + 27039 "Specify orientation point". The drag (FUN_180046c50): mccadres
   27054 "Sketch the text position for the dimension" (27070's '~' line left out: the '~' formatting pick is not ported) */
static void do_prompt(void) {
    const char *t = do_orient == 0 ? "Create dimension, ordinate horizontal: " : do_orient == 1 ? "Create dimension, ordinate vertical: " :
                    do_orient == 2 ? "Create dimension, ordinate parallel: " : "Create dimension, ordinate:";
    if (do_phase == 4) snprintf(cam_msg, sizeof cam_msg, "Sketch the text position for the dimension");
    else if (geo_cmd == G_ORDA) snprintf(cam_msg, sizeof cam_msg, "Create dimension, ordinate:\nSelect an ordinate dimension");   /* 27736 / 27450 */
    else if (do_phase == 2) snprintf(cam_msg, sizeof cam_msg, "%s\nSpecify orientation point", t);
    else snprintf(cam_msg, sizeof cam_msg, "%s\n%s\nSelect an ordinate dimension", t, do_zero ? "Indicate initial ordinate location" : "Indicate position for next ordinate element");
}
const char *do_prompt_text(void) { do_prompt(); return cam_msg; }

/* LAB_18003e9f0: the (re)start - zero mode, no base, orientation from the command */
static void do_restart(void) {
    do_orient = do_orient0(geo_cmd); do_zero = 1; do_base = -1; do_fb = 0; do_phase = 1; do_dragk = -1; do_drag_ok = 0;
    do_prompt(); do_scene();
}
int do_begin(int c) {
    (void)c;
    if (!mcam_std_view(1, do_vm)) return 0;
    if (!dr_stroke_fonts()) {
        cui_message("Drafting", "The drafting commands need Mastercam's stroke fonts (font0.fnt ..) from the user's install: run tools/setup_from_install.py or set \"fonts\" in paths.conf.");
        return 0;
    }
    do_globals_load();
    do_saved = *dr_globals(); do_saved_h = do_h_aln; do_saved_neg = do_neg; do_saved_arr = do_arrowhd;   /* DraftGlobalsSave */
    do_restart();
    return 1;
}
static void do_drag_drop(void);
void do_end(void) {                                     /* LAB_18003f467: DraftGlobalsRestore; a drag in progress ends as failed */
    if (geo_cmd == G_ORDA && al_drag >= 0) al_end_drag();
    if (do_phase == 4) do_drag_drop();
    *dr_globals() = do_saved; do_h_aln = do_saved_h; do_neg = do_saved_neg; do_arrowhd = do_saved_arr;
    do_phase = 0; do_dragk = -1; do_drag_ok = 0;
    do_scene();
}

/* ------------------------------------------------------------------------------------------------ the picks */
/* the aPoint of a point event: cui_draft.c dr_ac_apoint's rule (FUN_1804e3b90 / FUN_1804e1bb0: 'P' t 0 on a point entity, '1' / '2'
   t 0 / 1 at a line's ends, every other snap a free 'K' point - create.md 7.2) */
static void do_ac_apoint(uint8_t ap[0x98], int *ent, V3 p) {
    double w[3] = { p.x, p.y, p.z };
    *ent = -1;
    int e = geo_snap_feat_ent, f = geo_snap_feat, as = dr_assoc_on();
    if (as && e >= 0 && e < F.nents && F.ents[e].type == E_POINT && geo_snap_kind == GK_POINT) {
        dc_apoint_world(ap, w, 'P'); dc_apoint_ent(ap, 0, (uint64_t)F.ents[e].eptr, 0.0); *ent = e; return; }
    if (as && e >= 0 && e < F.nents && F.ents[e].type == E_LINE && geo_snap_kind == GK_END && (f == 0 || f == 1)) {
        dc_apoint_world(ap, w, f ? '2' : '1'); dc_apoint_ent(ap, 0, (uint64_t)F.ents[e].eptr, f ? 1.0 : 0.0); *ent = e; return; }
    dc_apoint_world(ap, w, 'K');
}
/* a point event of get_event at the click: the AutoCursor point (no Ctrl / Alt), else map_cursor_to_3d's 'K' point; MCEvent::
   SetupPointEvent (mastercam.dll @1802efad0, decomp/draft/ord_mc_event.c) puts the aPoint at +0x40 and at +0x110 its world point projected on the
   construction plane (proj_pt_cp: here the Top plane at the construction depth) */
static int do_point_event(float mx, float my, const double w[3], int ctrl, uint8_t ap[0x98], int *ent, double cp[3]) {
    V3 p = { w[0], w[1], grid_z() };
    geo_snap_ptr(mx, my); int sn = !ctrl && geo_snap(&p, NULL, -1, 1); geo_snap_ptr_off();
    if (sn) do_ac_apoint(ap, ent, p);
    else { double q[3] = { w[0], w[1], grid_z() }; dc_apoint_world(ap, q, 'K'); *ent = -1; }
    cp[0] = D_(ap, 8); cp[1] = D_(ap, 0x10); cp[2] = grid_z();
    return sn;
}

/* ------------------------------------------------------------------------------------------------ the text drag */
/* FUN_180038c50 -> RunDraftCommand(99) -> FUN_1800490a0(ent, 0) -> FUN_1800466a0 (oc off) -> FUN_180046920: the record loaded as the
   drag entity, DfOrdType (its base), DraftGlobalsSave + DraftGlobalsFromDim(record), a zero ordinate: auto_alignment 0; the calc inputs
   from the record: the aPoint, view +0x328, depth +0x320, p4 / p5 / p6 = the leader's first two points and the placement (world),
   the orientation from the text angle; drag entity valid and drawn (FUN_1800490a0). The oc (dragging a base's children along,
   df_load_oc / df_adjust_oc, panel id 17) is off for a new ordinate (set_oc_on(0)) */
static void do_drag_setup(int k) {
    const uint8_t *E = F.dnew[k].E;
    do_dragk = k; memcpy(do_drag, E, 0x810);
    const uint8_t *par = NULL;
    int pb = F.dnew[k].ent[1] <= -2 ? -2 - F.dnew[k].ent[1] : -1;
    if (pb >= 0 && pb < F.ndnew && F.dnew[pb].on) { memcpy(do_dbase, F.dnew[pb].E, 0x810); par = do_dbase; }
    else if (do_fb && !memcmp(E + 0x108, do_fbase + 0x20, 8)) { memcpy(do_dbase, do_fbase, 0x810); par = do_dbase; }   /* a base from the part */
    memset(&do_din, 0, sizeof do_din);
    do_din.type = do_ord_type(do_drag, par);
    do_settings(&do_dO);
    do_globals_from_dim(do_drag, &do_dO);
    if (do_din.type == 1) do_dO.S.auto_align = 0;
    memcpy(do_din.ap, E + 0x68, 0x98);
    { int16_t vv; memcpy(&vv, E + 0x328, 2); do_din.view = vv; } if (!mcam_std_view(do_din.view ? do_din.view : 1, do_din.vm)) memcpy(do_din.vm, do_vm, sizeof do_vm);
    do_din.depth = D_(E, 0x320);
    double v[3];
    v[0] = D_(E, 0x4c8); v[1] = D_(E, 0x4d0); v[2] = D_(E, 0x4d8); v2w(do_din.vm, v, do_din.p4);
    v[0] = D_(E, 0x4e0); v[1] = D_(E, 0x4e8); v[2] = D_(E, 0x4f0); v2w(do_din.vm, v, do_din.p5);
    v[0] = D_(E, 0x310); v[1] = D_(E, 0x318); v[2] = D_(E, 0x320); v2w(do_din.vm, v, do_din.p6);
    do_din.orient = do_orient_of(E);
    do_din.has_base = do_din.type == 0; do_din.base = par; do_din.base_eptr = 0;   /* a session base: its eptr is written by the save */
    if (par == do_dbase && do_fb && !memcmp(E + 0x108, do_fbase + 0x20, 8)) memcpy(&do_din.base_eptr, do_fbase + 0x20, 8);   /* a part base: its eptr */
    do_drag_ok = 1; do_phase = 4;
    do_prompt(); do_scene();
}
/* the drag left without the click (Esc, a panel button, another command): FUN_180046740(0) - the stored record stays as it was
   (drag discarded), DraftGlobalsRestore; doOrdinate then UndoTransTerminate: the new ordinate is removed (cad_rundraft.c:2306-2333) */
static void do_drag_drop(void) {
    if (do_dragk < 0) return;
    int k = do_dragk; do_dragk = -1; do_drag_ok = 0;
    if (k == F.ndnew - 1) F.ndnew--; else F.dnew[k].on = 0;   /* the store's undo transaction terminated (ours: never pushed) */
    if (do_zero) { do_base = -1; do_fb = 0; }           /* a zero ordinate: no base, zero mode again (BL = 0) */
    do_phase = 1;
    do_prompt(); do_scene();
}
/* the click (event 4): FUN_180047080 -> FUN_180046740(1): the drag entity as the last mouse move left it written over the stored
   record (its eptr kept), DraftGlobalsRestore; doOrdinate: DraftGlobalsFromDim(the stored record), UndoTransCommit; the base is
   reloaded and the next picks make children (SI = 0) (cad_rundraft.c:2306-2340) */
static void do_drag_accept(void) {
    int k = do_dragk; if (k < 0) return;
    memcpy(F.dnew[k].E, do_drag, 0x810);
    do_dragk = -1; do_drag_ok = 0;
    URec r; memset(&r, 0, sizeof r); r.kind = U_DRAFT; r.idx = k; undo_push(r);
    doc_dirty = 1;
    do_from_dim(F.dnew[k].E);
    do_zero = 0; do_phase = 1;
    do_prompt(); do_scene();
}
/* FUN_180046430: one mouse move - the sketched point (df_calc_sketched_point), calc_ordinate_dimension into the drag entity with the
   inputs FUN_180046920 set (p4 / p5 / p6 rewritten by each call for a zero ordinate); valid: drawn; invalid: 27010 and the drag
   restarts from the stored record (FUN_180046320, the loop's FUN_180040ed0) */
int do_move(const double world[3], int ctrl) {
    if (geo_cmd == G_ORDA) return al_move(world, ctrl);
    if (do_phase != 4 || do_dragk < 0) return 0;
    do_din.text[0] = world[0]; do_din.text[1] = world[1]; do_din.text[2] = grid_z();
    do_din.ctrl = ctrl;
    do_dO.S.sel_dist = SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1);
    DoResult R = do_calc_ordinate(do_drag, &do_din, &do_dO, dr_stroke_fonts());
    if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); do_drag_ok = 0; do_scene(); return 1; }
    if (!R.valid) { int k = do_dragk; snprintf(cam_msg, sizeof cam_msg, "Warning - cannot calculate dimension"); do_drag_setup(k); return 1; }
    do_drag_ok = 1; do_scene();
    return 1;
}

/* state 3 (asm 0x18003ec9a-0x18003ee16): the record from InitDIM, the text point = the event point + 10 tol in x and y,
   calc_ordinate_dimension; invalid: the command ends; DfAssoc off: DfTurnOffOrdAssoc; store_ent (one undo transaction with the
   drag); a zero ordinate becomes the base; then the drag (state 4) */
static int do_create(void) {
    DoSettings O; do_settings(&O);
    uint8_t E[0x810]; dc_init_dim(E, &O.S, do_view);
    DoInput in; memset(&in, 0, sizeof in);
    memcpy(in.ap, do_ap, 0x98);
    in.text[0] = do_cp[0] + 5e-5 * 10.0; in.text[1] = do_cp[1] + 5e-5 * 10.0; in.text[2] = do_cp[2];   /* *tol_exref * 10.0 (XMM7 = 10.0) */
    memcpy(in.p4, do_p4, sizeof in.p4); memcpy(in.p5, do_p5, sizeof in.p5); memcpy(in.p6, do_p6, sizeof in.p6);
    in.view = do_view; memcpy(in.vm, do_vm, sizeof in.vm); in.depth = do_depth;
    in.type = do_zero ? 1 : 0; in.orient = do_orient;
    in.has_base = !do_zero && (do_fb || (do_base >= 0 && do_base < F.ndnew && F.dnew[do_base].on));
    in.base = !in.has_base ? NULL : do_fb ? do_fbase : F.dnew[do_base].E;
    if (do_fb) memcpy(&in.base_eptr, do_fbase + 0x20, 8);       /* the part record's eptr (+0x108; the save keeps the part's records) */
    DoResult R = do_calc_ordinate(E, &in, &O, dr_stroke_fonts());
    if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); return 0; }
    if (!R.valid) return 0;
    do_orient = in.orient;                               /* *param_10 (the base's orientation for a child) */
    int parent = in.has_base && !do_fb ? do_base : -1;
    if (!dr_assoc_on()) { memset(E + 0x108, 0, 8); E[0x123] = 0; parent = -1; }   /* DfTurnOffOrdAssoc (decomp/draft/ord_core_helpers.c 1443-1463) */
    F.dnew = realloc(F.dnew, sizeof *F.dnew * (size_t)(F.ndnew + 1));
    struct DraftNew *d = &F.dnew[F.ndnew];
    memcpy(d->E, E, 0x810); d->ent[0] = do_apent; d->ent[1] = parent >= 0 ? -2 - parent : -1; d->on = 1;
    int k = F.ndnew++;
    if (do_zero) do_base = k;                            /* get_ent_from_eptr(the new eptr, base): BL = 1 */
    do_drag_setup(k);
    return 1;
}

/* FUN_180039860 (decomp/draft/ord_cad_drag.c 276): the picked dimension must be an ordinate (else mccadres 27451 "You must select an
   ordinate dimension.") and a zero one, DfOrdType 1 (else mccoreres 27038 "Select a BASE(0) ordinate dimension"); DraftIsDirty (its
   aPoint off its entity, core_calcdim.c 9237) asks to regenerate it: the regeneration is NOT PORTED - an associative base whose entity
   was changed here is refused. Then (asm 0x18003f2c7-0x18003f2f6) BL = 1, SI = 0, view / depth = the base's +0x328 / +0x320 */
static void do_take_base(const CuiDraftHit *h) {
    const uint8_t *B = h->E;
    if (B[0x308] != 3) { cui_message("Drafting", "You must select an ordinate dimension."); return; }
    uint8_t Bc[0x810]; memcpy(Bc, B, 0x810);
    const uint8_t *par = NULL;
    if (h->dnew >= 0) { int pb = F.dnew[h->dnew].ent[1] <= -2 ? -2 - F.dnew[h->dnew].ent[1] : -1; if (pb >= 0 && pb < F.ndnew && F.dnew[pb].on) par = F.dnew[pb].E; }
    else { uint64_t pe; memcpy(&pe, B + 0x108, 8); const DraftDoc *dd = pe ? cui_draft_doc() : NULL; uint32_t n;
        for (int i = 0; dd && i < draft_count(dd) && !par; i++) { const uint8_t *P = draft_record(dd, i, &n); if (P && n >= 0x810 && !memcmp(P + 0x20, &pe, 8)) par = P; } }
    if (do_ord_type(Bc, par) != 1) { cui_message("Drafting", "Select a BASE(0) ordinate dimension"); return; }
    if (h->file >= 0 && B[0x68] != 'K' && B[0x68] != 'O') {   /* DraftIsDirty: the referenced entity changed in this session */
        uint64_t ae; memcpy(&ae, B + 0x68 + 0x28, 8);
        for (int e = 0; ae && e < F.nents; e++) if ((uint64_t)F.ents[e].eptr == ae && !F.ents[e].type) {
            cui_message("Drafting", "The base ordinate's geometry was changed: regenerating it is not ported."); return; }
    }
    if (h->dnew >= 0) { do_base = h->dnew; do_fb = 0; } else { do_fb = 1; memcpy(do_fbase, B, 0x810); do_base = -1; }
    do_zero = 0;
    int16_t vv; memcpy(&vv, B + 0x328, 2); do_view = vv ? vv : 1; if (!mcam_std_view(do_view, do_vm)) mcam_std_view(1, do_vm);
    memcpy(&do_depth, B + 0x320, 8);
    do_prompt(); do_scene();
}
/* a left click in the view (cui_geo.c geo_click) */
int do_click(float mx, float my, const double w[3], int ctrl) {
    if (geo_cmd == G_ORDA) { if (do_phase == 4) al_accept(); else al_pick(w); return 1; }
    if (do_phase == 4) {                                /* event 4: accept the drag entity as last computed */
        do_drag_accept();
        return 1;
    }
    uint8_t ap[0x98]; int ent = -1; double cp[3];
    int sn = do_point_event(mx, my, w, ctrl, ap, &ent, cp);
    if (do_phase == 1 && !sn && !do_nopick) {                         /* no AutoCursor point: get_event's entity event (mask 0x40, FUN_1804e3b90 / FUN_1804dff70:
                                                           select_ent_univ, cui_draft_pick) - a base ordinate (FUN_180039860) */
        CuiDraftHit h; double q[3] = { w[0], w[1], grid_z() };
        if (cui_draft_pick(q, 0x40, &h)) { do_take_base(&h); return 1; }
    }
    if (do_phase == 2) {                                /* the orientation point (get_event 9): local_8288 = its +0x110; p5 = p6 = it */
        memcpy(do_cp, cp, sizeof do_cp); memcpy(do_p5, cp, sizeof do_p5); memcpy(do_p6, cp, sizeof do_p6);
        if (!do_create()) { geo_end(); return 1; }
        return 1;
    }
    /* state 1 -> 2 (asm 0x18003f323-0x18003f433): a zero ordinate takes the active view (Top) and the construction depth (the view z of
       the sketched point: the cursor on the construction plane); the aPoint, local_8288 = +0x110, p4 = p5 = p6 = the aPoint's point */
    if (do_zero) { do_view = 1; mcam_std_view(1, do_vm); double q[3] = { w[0], w[1], grid_z() }, v[3]; w2v(do_vm, q, v); do_depth = v[2]; }
    memcpy(do_ap, ap, 0x98); do_apent = ent; memcpy(do_cp, cp, sizeof do_cp);
    for (int i = 0; i < 3; i++) do_p4[i] = do_p5[i] = do_p6[i] = D_(ap, 8 + 8 * i);
    if (do_zero && do_orient == 2) { do_phase = 2; do_prompt(); do_scene(); return 1; }   /* rubber band from p4 (init_rubber_band(0): not drawn here) */
    {   /* state 2, not the parallel zero (asm 0x18003f01c-0x18003f06d): local_8288 to the view, z = the depth, back; p5 = p6 = it */
        double v[3]; w2v(do_vm, do_cp, v); v[2] = do_depth; v2w(do_vm, v, do_cp);
        memcpy(do_p5, do_cp, sizeof do_p5); memcpy(do_p6, do_cp, sizeof do_p6);
    }
    if (!do_create()) { geo_end(); return 1; }          /* calc failed: the command ends (asm 0x18003ed62 -> 0x18003f44a) */
    return 1;
}
int do_need(void) { return do_phase == 4 ? 't' : geo_cmd == G_ORDA ? 'e' : 'p'; }   /* Align's pick: entities only (flags 10) */  /* picks: get_event 0x1000000b / 9 (points, AutoCursor); the drag: positions */

/* Esc: the drag -> the new ordinate removed, picking goes on (cad_rundraft.c:2318-2333); picking: get_event cancelled -> the command
   ends (LAB_18003f45c). 1 = handled */
int do_esc(void) {
    if (geo_cmd == G_ORDA) { if (al_drag >= 0) { al_end_drag(); return 1; } return 0; }
    if (do_phase == 4) { do_drag_drop(); return 1; }
    return 0;
}
/* the panel's buttons (FUN_180038ed0 while picking: 23 / 24 end the command, 25 (Enter / OK-New) restarts it; during the drag
   FUN_18012af20 -> codes 3 / 4 / 5: the drag fails - the new ordinate removed -, 5 restarts the command). Which FunctionPanel button
   posts 23 / 24 / 25 is NOT DECODED (create.md 2.5): as cui_draft.c, OK / Cancel = 23 / 24, OK-New (code 1) = 25 */
int do_button(int code) {
    if (geo_cmd == G_ORDA) { if (al_drag >= 0) al_end_drag(); return code == 1; }
    if (do_phase == 4) { do_drag_drop(); if (code == 1) do_restart(); return 1; }
    if (code == 1) { do_restart(); return 1; }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the function panel */
/* DraftingSmartDimensionsManager (the panel doOrdinate opens, cad_rundraft.c:1950-2030): Mode shows the orientation (FUN_180038b30(4 /
   5 / 6, local_82f4): FUN_180259080 - 0 Horizontal, 1 Vertical, other Auto), greyed (the event 0x10000000 a Mode change sends is NOT
   DECODED); live: _Centered text (id 3 auto_alignment) and De_cimal places (id 12 num_digits) (FUN_18012af20, create.md 2.5; the drag
   re-runs at the last cursor, FUN_180047170); the rest greyed as in cui_draft.c */
static int po_mode, po_center, po_digits, po_dummy;
void do_panel_body(const CuiPanelOps *ops, void *u) {
    DcSettings *S = do_phase == 4 ? &do_dO.S : dr_globals();
    po_mode = do_orient == 0 ? 1 : do_orient == 1 ? 2 : 0; po_center = S->auto_align; po_digits = S->digits;
    int t = -1;
    if (!ops->tab || ops->tab(u, 0, "Basic")) {
        if (ops->exp(u, "Entity")) {
            ops->label(u, "Mode:");
            ops->radio(u, "_Auto", &po_mode, 0, 0); ops->radio(u, "_Horizontal", &po_mode, 1, 0); ops->radio(u, "_Vertical", &po_mode, 2, 0);
            ops->end(u);
        }
        if (ops->exp(u, "Format")) { ops->toggle(u, "_Font", 1, -1); ops->toggle(u, "_Edit text", 1, -1); ops->toggle(u, "Heigh_t", 1, -1);
            ops->inum(u, "De_cimal places:", "digits", &po_digits, NULL, 1, 0, 8); ops->end(u); }
    }
    if (!ops->tab || ops->tab(u, 1, "Advanced")) {
        if (ops->exp(u, "Dimension Text")) { ops->check(u, "_Centered text", &po_center, 1); ops->label(u, "Edit Mode:");
            t = -1; ops->radio(u, "P_lace", &t, 0, 0); ops->radio(u, "_Edit", &t, 1, 0); ops->end(u); }
        if (ops->exp(u, "Dimension Box")) { ops->check(u, "_Show when dragging", &po_dummy, 0); ops->end(u); }
        if (ops->exp(u, "Settings")) { ops->toggle(u, "_Options", 1, -1); ops->toggle(u, "_Update", 1, -1); ops->end(u); }
    }
}
const char *const *do_panel_tabs(int *n) { static const char *const two[2] = { "Basic", "Advanced" }; *n = 2; return two; }
void do_panel_sync(void) {
    DcSettings *S = do_phase == 4 ? &do_dO.S : dr_globals();
    int ch = 0;
    if (po_center != S->auto_align) { S->auto_align = po_center; ch = 1; }
    if (po_digits != S->digits && po_digits >= 0 && po_digits <= 8) { S->digits = po_digits; ch = 1; }
    if (ch && do_phase == 4) { double c[3] = { do_din.text[0], do_din.text[1], do_din.text[2] }; do_move(c, 0); }   /* FUN_180047170 */
}

/* ------------------------------------------------------------------------------------------------ script steps (ours) */
static int I16_(const uint8_t *b, int o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static void do_dump_rec(const char *tag, const uint8_t *E) {
    char t[0x60]; memcpy(t, E + 0x650, 0x58); t[0x58] = 0;
    uint64_t pe, ae; memcpy(&pe, E + 0x108, 8); memcpy(&ae, E + 0x68 + 0x28, 8);
    printf("    %s: type %d aP '%c' (%.4f,%.4f,%.4f)%s offset %.6f value %.6f parent %s text '%s' at (%.4f,%.4f) angle %.4f h %.4f view %d depth %.4f h_aln %d neg %d arrowhd %d auto %d digits %d ls %d\n",
           tag, E[0x308], E[0x68] ? E[0x68] : '?', D_(E, 0x70), D_(E, 0x78), D_(E, 0x80), ae ? " eptr" : "", D_(E, 0x100), D_(E, 0x338), pe ? "eptr" : "-", t,
           D_(E, 0x638), D_(E, 0x640), D_(E, 0x5e8), D_(E, 0x600), I16_(E, 0x328), D_(E, 0x320), E[0x122], E[0x124], E[0x125], E[0x7c9], E[0x7cd], I16_(E, 0x7b4));
    const uint8_t *w = E + 0x4a8; int n = I16_(w, 0x1a); if (n > 4) n = 4;
    printf("      ldr1 on %d vis %d arrow %d type %d n %d:", w[0], w[3], w[4], w[0x18], I16_(w, 0x1a));
    for (int i = 0; i < n; i++) printf(" (%.4f,%.4f)", D_(w, 0x20 + 0x18 * i), D_(w, 0x28 + 0x18 * i));
    printf("\n      placement (%.4f,%.4f) offsets (%.4f,%.4f)\n", D_(E, 0x310), D_(E, 0x318), D_(E, 0x360), D_(E, 0x368));
}
static void do_dump(void) {
    printf("    ordinate cmd '%s' phase %d zero %d base %d orient %d drag %d dims %d\n", do_cmd(geo_cmd) ? do_label(geo_cmd) : "-", do_phase, do_zero, do_base, do_orient, do_dragk, F.ndnew);
    for (int i = 0; i < F.ndnew; i++) { if (F.dnew[i].E[0x308] != 3) continue; char tag[48]; snprintf(tag, sizeof tag, "ord %d%s base %d", i, F.dnew[i].on ? "" : " (undone)", F.dnew[i].ent[1] <= -2 ? -2 - F.dnew[i].ent[1] : -1);
        do_dump_rec(tag, F.dnew[i].E); printf("      refs %d\n", F.dnew[i].ent[0]); }
    if (do_phase == 4 && do_drag_ok) do_dump_rec("drag", do_drag);
}
/* orddrag:X,Y (a mouse move at world X,Y during the text drag), orddump; ordglobals:N - the session globals from the open part's
   ordinate N (DraftGlobalsFromDim, the '~' pick's ToDimGlobalsFromDraft - ours as a step: the corpus recreation) */
static const uint8_t *file_ord(DraftDoc *dd, int want) {
    int k = 0; uint32_t n;
    for (int i = 0; dd && i < draft_count(dd); i++) { const uint8_t *E = draft_record(dd, i, &n);
        if (E && n == 0x810 && E[0] == 0x40 && E[0x308] == 3) { if (k == want) return E; k++; } }
    return NULL;
}
int do_script(const char *st, const char *arg) {
    if (!strcmp(st, "orddrag") && arg) { double x = 0, y = 0; sscanf(arg, "%lf,%lf", &x, &y); double w[3] = { x, y, 0 }; if (!do_move(w, 0)) printf("    (no ordinate drag)\n"); return 1; }
    if (!strcmp(st, "orddump")) { do_dump(); return 1; }
    if (!strcmp(st, "draftpick") && arg) {             /* draftpick:X,Y[,MASK] - the drafting entity cui_draft_pick takes at world X,Y (default mask 0x70) */
        double x = 0, y = 0; int m = 0x70; sscanf(arg, "%lf,%lf,%i", &x, &y, &m); double q[3] = { x, y, grid_z() }; CuiDraftHit h;
        if (cui_draft_pick(q, m, &h)) printf("    draftpick: %s %d type %d dist %.6f text '%.20s'\n", h.file >= 0 ? "file" : "session", h.file >= 0 ? h.file : h.dnew, h.E[0] == 0x40 ? h.E[0x308] : -1, h.dist, h.E[0] == 0x40 ? (const char *)h.E + 0x650 : "");
        else printf("    draftpick: none\n");
        return 1; }
    if (!strcmp(st, "ordat") && arg) {                 /* ordat:X,Y - a Ctrl+click at world X,Y (no AutoCursor: FUN_1804e3b90's map_cursor_to_3d 'K' point) */
        if (!do_cmd(geo_cmd)) { printf("    (no ordinate command)\n"); return 1; }
        double x = 0, y = 0; sscanf(arg, "%lf,%lf", &x, &y); double w[3] = { x, y, grid_z() }; do_click(0, 0, w, 1); return 1; }
    if (!strcmp(st, "ordpt") && arg) {                 /* ordpt:X,Y - ours (corpus recreation): a Ctrl+click whose entity pick is skipped (the part's own
                                                           ordinates at the point would be picked as bases) - map_cursor_to_3d's 'K' point */
        if (!do_cmd(geo_cmd)) { printf("    (no ordinate command)\n"); return 1; }
        double x = 0, y = 0; sscanf(arg, "%lf,%lf", &x, &y); double w[3] = { x, y, grid_z() }; do_nopick = 1; do_click(0, 0, w, 1); do_nopick = 0; return 1; }
    if (!strcmp(st, "ordglobals") && arg) {
        DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *E = file_ord(dd, atoi(arg));
        if (E) { do_globals_load(); do_from_dim(E); } else printf("    (no ordinate dimension %s)\n", arg);
        draft_free(dd); return 1;
    }
    if (!strcmp(st, "ordredo") && arg) {               /* ordredo:N - a drag mouse move at the cursor the regeneration FUN_1803053a0 gives the open part's
                                                           ordinate N (core_dimlayout.c 3127-3150: the aPoint + the offsets +0x360 / +0x368, the text start
                                                           by FUN_180255830 unless h_txt_aln is 1; the corpus oracle's cursor) */
        DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *Es = file_ord(dd, atoi(arg));
        if (!Es || do_phase != 4) printf("    (ordredo: %s)\n", Es ? "no drag" : "no such ordinate");
        else {
            double vm[9]; int16_t vv; memcpy(&vv, Es + 0x328, 2); mcam_std_view(vv ? vv : 1, vm);
            double aw[3], av[3], T[3], b4[3], af[3], cw[3]; memcpy(aw, Es + 0x70, 24); w2v(vm, aw, av);
            T[0] = av[0] + D_(Es, 0x360); T[1] = av[1] + D_(Es, 0x368); T[2] = D_(Es, 0x370);
            if (Es[0x122] != 1) { do_text_ends(Es, dr_stroke_fonts(), T, b4, af); memcpy(T, Es[0x122] == 0 ? b4 : af, sizeof T); }
            v2w(vm, T, cw); do_move(cw, 0);
        }
        draft_free(dd); return 1;
    }
    if (!strcmp(st, "ordcmp") && arg) {                /* ordcmp:N - the last ordinate made here against the open part's ordinate N (the oracle's fields;
                                                           the aPoint reference and the base's eptr apart: EQUAL-LAYOUT) */
        DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *Es = file_ord(dd, atoi(arg)); const uint8_t *Eo = NULL; int io = -1;
        for (int i = F.ndnew - 1; i >= 0 && !Eo; i--) if (F.dnew[i].on && F.dnew[i].E[0x308] == 3) { Eo = F.dnew[i].E; io = i; }
        if (!Es || !Eo) { printf("    ordcmp: nothing to compare\n"); draft_free(dd); return 1; }
        char why[1500] = ""; size_t L = 0; int nd = 0, nref = 0;
#define DF(...) do { nd++; L += (size_t)snprintf(why + L, L < sizeof why ? sizeof why - L : 0, __VA_ARGS__); } while (0)
#define CL(a, b) (fabs((a) - (b)) <= 1e-6 * (1 + fabs(b)))
        if (!CL(D_(Eo, 0x70), D_(Es, 0x70)) || !CL(D_(Eo, 0x78), D_(Es, 0x78))) DF(" aP position;");
        else if (Eo[0x68] != Es[0x68] || memcmp(Eo + 0x90, Es + 0x90, 8) || !CL(D_(Eo, 0x80), D_(Es, 0x80))) nref++;
        if (!CL(D_(Eo, 0x338), D_(Es, 0x338))) DF(" value %.6f/%.6f;", D_(Eo, 0x338), D_(Es, 0x338));
        if (strncmp((const char *)Eo + 0x650, (const char *)Es + 0x650, 0x58)) DF(" text '%.20s'/'%.20s';", Eo + 0x650, Es + 0x650);
        for (int k = 0; k < 3; k++) if (!CL(D_(Eo, 0x638 + 8 * k), D_(Es, 0x638 + 8 * k))) { DF(" textpos (%.5f,%.5f)/(%.5f,%.5f);", D_(Eo, 0x638), D_(Eo, 0x640), D_(Es, 0x638), D_(Es, 0x640)); break; }
        if (!CL(D_(Eo, 0x5e8), D_(Es, 0x5e8))) DF(" textangle;");
        { const uint8_t *a = Eo + 0x4a8, *b = Es + 0x4a8; int bad = a[0] != b[0] || a[1] != b[1] || a[3] != b[3] || a[4] != b[4] || a[0x18] != b[0x18] || I16_(a, 0x1a) != I16_(b, 0x1a);
          int np = I16_(b, 0x1a); if (np > 4) np = 4;
          for (int i = 0; !bad && i < np * 3; i++) bad = !CL(D_(a, 0x20 + 8 * i), D_(b, 0x20 + 8 * i));
          if (bad) DF(" ldr1;"); }
        for (int k = 0; k < 3; k++) if (!CL(D_(Eo, 0x310 + 8 * k), D_(Es, 0x310 + 8 * k))) { DF(" placement;"); break; }
        for (int k = 0; k < 2; k++) if (!CL(D_(Eo, 0x360 + 8 * k), D_(Es, 0x360 + 8 * k))) { DF(" offsets;"); break; }
        if (Eo[0x122] != Es[0x122] || Eo[0x124] != Es[0x124] || Eo[0x125] != Es[0x125]) DF(" flags;");
        { uint64_t a, b; memcpy(&a, Eo + 0x108, 8); memcpy(&b, Es + 0x108, 8); int pa = a || F.dnew[io].ent[1] <= -2; if (!pa != !b) DF(" parent %s/%s;", pa ? "set" : "0", b ? "set" : "0"); }   /* (a session base: its eptr comes with the save) */
#undef DF
#undef CL
        printf("    ordcmp %s: %s%s%s\n", arg, nd ? "DIFFER" : nref ? "EQUAL-LAYOUT" : "EQUAL", why, nref ? " (aPoint reference)" : "");
        draft_free(dd); return 1;
    }
    if (!strcmp(st, "ordfile")) {
        DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *E; int k = 0;
        for (; (E = file_ord(dd, k)); k++) { char tag[32]; snprintf(tag, sizeof tag, "file ord %d", k); do_dump_rec(tag, E); }
        if (!k) printf("    (no ordinate dimensions in the part file)\n");
        draft_free(dd); return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ Align (0x9477) */
/* FUN_1800392d0 runs 0x9477 in the dimension loop FUN_18003f7f0 with FUN_180048b10(3, -1) (cad_dimcmd.c:474-478; app+0x5a = 0: no live
   entity). Picks (FUN_180049c60 case 3, cad_linear.c:2729-2735): get_event flags 10 (entities, no AutoCursor point), mask 0x40; prompt
   (FUN_180049560 case 3) 27736 "Create dimension, ordinate:" + 27450 "Select an ordinate dimension". FUN_18004a620 case 0x40
   (cad_linear.c:3167-3193): an ordinate -> FUN_1800490a0(ent, 1) -> FUN_1800466a0 with set_oc_on(1) -> the drag of 8.3 with the oc:
   FUN_180046920 (oc on: auto_alignment 0, h_txt_aln 1; a zero ordinate without children or an orphan: oc off), df_load_oc(type,
   orientation, base, 1) - a child: the oc = its base + the base's other children; a zero: its children (ord_core_helpers.c 499) -,
   FUN_1802423f0 their order (else oc off); each mouse move: calc_ordinate_dimension then df_adjust_oc (the others' texts staggered along
   the dragged text's row, move_ordinate_dimension); the click: FUN_180046740(1) - the dragged one written, df_manage_oc(3) - every oc
   entry written (one UndoTrans). Another dimension: 27451 "You must select an ordinate dimension.". The loop then picks again. */
typedef struct { CuiDraftHit h; uint8_t E[0x810]; } OrdRef;
static OrdRef *al_all; static int al_nall;               /* every ordinate (the part's - as rewritten here -, then the session's) */
static int al_drag = -1;                                  /* the dragged one (al_all index), -1 none (declared above) */
static DoOc *al_oc; static int *al_ocsrc; static int al_n, al_ocon; static DoOcGlob al_g;
static void al_collect(void) {
    al_nall = 0; free(al_all); al_all = NULL;
    const DraftDoc *dd = cui_draft_doc(); int nf = dd ? draft_count(dd) : 0;
    al_all = malloc(sizeof *al_all * (size_t)(nf + F.ndnew + 1));
    for (int i = 0; i < nf; i++) { uint32_t n; const uint8_t *E = draft_record(dd, i, &n); if (!E || n < 0x810 || E[0] != 0x40 || E[0x308] != 3) continue;
        const uint8_t *M = cui_draft_mod(E); OrdRef *r = &al_all[al_nall++]; memset(r, 0, sizeof *r);
        r->h.file = i; r->h.dnew = -1; memcpy(r->E, M ? M : E, 0x810); memcpy(r->E + 0x20, E + 0x20, 8); r->h.E = r->E; }
    for (int i = 0; i < F.ndnew; i++) { if (!F.dnew[i].on || cui_draft_is_repl(i) || F.dnew[i].E[0x308] != 3) continue;
        OrdRef *r = &al_all[al_nall++]; memset(r, 0, sizeof *r); r->h.file = -1; r->h.dnew = i; memcpy(r->E, F.dnew[i].E, 0x810); r->h.E = r->E; }
}
static int al_child_of(int c, int p) {                    /* +0x108 = the parent's eptr (a session base: McamFile.dnew ent[1]) */
    const OrdRef *C = &al_all[c], *P = &al_all[p];
    if (C->h.dnew >= 0 && F.dnew[C->h.dnew].ent[1] <= -2) return P->h.dnew == -2 - F.dnew[C->h.dnew].ent[1];
    uint64_t pe, ce; memcpy(&ce, C->E + 0x108, 8); memcpy(&pe, P->E + 0x20, 8);
    return P->h.file >= 0 && ce && ce == pe;
}
static int al_parent(int c) { for (int i = 0; i < al_nall; i++) if (i != c && al_child_of(c, i)) return i; return -1; }
static void al_scene(void) {                              /* the dragged one and the oc entries drawn from their copies */
    uint64_t *hide = malloc(sizeof(uint64_t) * (size_t)(al_n + 2)); int nh = 0;
    int nd = 0; uint8_t *b = malloc((size_t)(F.ndnew + al_n + 1) * 0x810);
    int skipd[1024]; int ns = 0;
    if (al_drag >= 0) { if (al_all[al_drag].h.dnew >= 0) skipd[ns++] = al_all[al_drag].h.dnew; else memcpy(&hide[nh++], al_all[al_drag].E + 0x20, 8); }
    for (int j = 0; j < al_n && ns < 1024; j++) { const OrdRef *r = &al_all[al_ocsrc[j]]; if (r->h.dnew >= 0) skipd[ns++] = r->h.dnew; else memcpy(&hide[nh++], r->E + 0x20, 8); }
    for (int i = 0; i < F.ndnew; i++) { if (!F.dnew[i].on) continue; int sk = 0; for (int q = 0; q < ns; q++) sk |= skipd[q] == i;
        uint64_t m; memcpy(&m, F.dnew[i].E + 0x20, 8); for (int q = 0; q < nh && m; q++) sk |= hide[q] == m;
        if (!sk) memcpy(b + (size_t)nd++ * 0x810, F.dnew[i].E, 0x810); }
    if (al_drag >= 0 && do_drag_ok) memcpy(b + (size_t)nd++ * 0x810, do_drag, 0x810);
    for (int j = 0; j < al_n; j++) memcpy(b + (size_t)nd++ * 0x810, al_oc[j].E, 0x810);
    if (cui_draft_hide((const unsigned long long *)hide, nh)) scene_draft_rebuild();
    scene_draft_session(b, nd, 0x810);
    free(b); free(hide); overlay_dirty = 1;
}
static void al_end_drag(void) {
    al_drag = -1; do_drag_ok = 0; al_n = 0; al_ocon = 0; free(al_oc); al_oc = NULL; free(al_ocsrc); al_ocsrc = NULL;
    *dr_globals() = do_saved; do_h_aln = do_saved_h; do_neg = do_saved_neg; do_arrowhd = do_saved_arr;   /* DraftGlobalsRestore */
    if (cui_draft_hide(NULL, 0)) scene_draft_rebuild(); do_phase = 1; do_prompt(); dr_scene();
}
static void al_pick(const double w[3]) {
    CuiDraftHit h; double q[3] = { w[0], w[1], grid_z() };
    if (!cui_draft_pick(q, 0x40, &h)) return;
    if (h.E[0x308] != 3) { cui_message("Drafting", "You must select an ordinate dimension."); return; }
    al_collect();
    int d = -1;
    for (int i = 0; i < al_nall && d < 0; i++) if ((h.dnew >= 0 && al_all[i].h.dnew == h.dnew) || (h.file >= 0 && al_all[i].h.file == h.file)) d = i;
    if (d < 0) return;
    al_drag = d;
    memcpy(do_drag, al_all[d].E, 0x810);
    int p = al_parent(d);
    memset(&do_din, 0, sizeof do_din);
    do_din.type = do_ord_type(do_drag, p >= 0 ? al_all[p].E : NULL);
    int nkids = 0; for (int i = 0; i < al_nall; i++) nkids += i != d && al_child_of(i, d);
    int ocon = !((do_din.type == 1 && nkids == 0) || do_din.type == 2);   /* FUN_180046920: DAT_180693012 = 1, set_oc_on(0) */
    do_settings(&do_dO); do_globals_from_dim(do_drag, &do_dO);
    if (ocon) { do_dO.S.auto_align = 0; do_dO.h_aln = 1; }
    if (do_din.type == 1) do_dO.S.auto_align = 0;
    memcpy(do_din.ap, do_drag + 0x68, 0x98);
    { int16_t vv; memcpy(&vv, do_drag + 0x328, 2); do_din.view = vv ? vv : 1; } if (!mcam_std_view(do_din.view, do_din.vm)) return;
    do_din.depth = D_(do_drag, 0x320);
    double v[3];
    v[0] = D_(do_drag, 0x4c8); v[1] = D_(do_drag, 0x4d0); v[2] = D_(do_drag, 0x4d8); v2w(do_din.vm, v, do_din.p4);
    v[0] = D_(do_drag, 0x4e0); v[1] = D_(do_drag, 0x4e8); v[2] = D_(do_drag, 0x4f0); v2w(do_din.vm, v, do_din.p5);
    v[0] = D_(do_drag, 0x310); v[1] = D_(do_drag, 0x318); v[2] = D_(do_drag, 0x320); v2w(do_din.vm, v, do_din.p6);
    do_din.orient = do_orient_of(do_drag);
    do_din.has_base = do_din.type == 0; do_din.base = p >= 0 ? al_all[p].E : NULL;
    if (p >= 0 && al_all[p].h.file >= 0) memcpy(&do_din.base_eptr, al_all[p].E + 0x20, 8);
    /* df_load_oc(type, orientation, base, 1): a child: its base (entry 0) + the base's other children; a zero: its children */
    al_n = 0; free(al_oc); free(al_ocsrc); al_oc = calloc((size_t)al_nall + 1, sizeof *al_oc); al_ocsrc = calloc((size_t)al_nall + 1, sizeof *al_ocsrc);
    al_ocon = 0;
    if (ocon && do_din.type != 2) {
        int base = do_din.type == 1 ? d : p;
        if (do_din.type == 0 && p >= 0) { memcpy(al_oc[al_n].E, al_all[p].E, 0x810); al_ocsrc[al_n++] = p; }
        for (int i = 0; base >= 0 && i < al_nall; i++) if (i != d && i != base && al_child_of(i, base)) { memcpy(al_oc[al_n].E, al_all[i].E, 0x810); al_ocsrc[al_n++] = i; }
        for (int j = 0; j < al_n; j++) { al_oc[j].E[0x7c9] = 1; memset(al_oc[j].E + 0x20, 0, 8); }
        al_ocon = al_n > 0 && do_oc_order(al_oc, al_n, do_drag, do_din.orient, dr_stroke_fonts(), &al_g);
        if (!al_ocon) al_n = 0;
    }
    do_drag_ok = 1; do_phase = 4;
    do_prompt(); al_scene();
}
static int al_move(const double world[3], int ctrl) {
    if (do_phase != 4 || al_drag < 0) return 0;
    do_din.text[0] = world[0]; do_din.text[1] = world[1]; do_din.text[2] = grid_z(); do_din.ctrl = ctrl;
    do_dO.S.sel_dist = SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1);
    DoResult R = do_calc_ordinate(do_drag, &do_din, &do_dO, dr_stroke_fonts());
    if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); return 1; }
    if (!R.valid) { snprintf(cam_msg, sizeof cam_msg, "Warning - cannot calculate dimension"); al_end_drag(); return 1; }
    if (al_ocon) do_oc_adjust(al_oc, al_n, do_drag, do_din.orient, &al_g, dr_stroke_fonts());   /* df_adjust_oc(DAT_180649694) */
    al_scene();
    return 1;
}
static void al_accept(void) {
    if (al_drag < 0) return;
    cui_draft_edit_begin();
    uint8_t E[0x810]; memcpy(E, do_drag, 0x810); memcpy(E + 0x20, al_all[al_drag].E + 0x20, 8);   /* +0x20 = dragedit_eptr (FUN_180046740) */
    cui_draft_edit_rec(&al_all[al_drag].h, E);
    for (int j = 0; j < al_n; j++) { const OrdRef *r = &al_all[al_ocsrc[j]];   /* df_manage_oc(3): each entry's own eptr back */
        memcpy(al_oc[j].E + 0x20, r->E + 0x20, 8); cui_draft_edit_rec(&r->h, al_oc[j].E); }
    cui_draft_edit_commit();
    al_end_drag();
}
