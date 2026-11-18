/* cui_extrude.c - the Solids > Extrude command (ex_*: chain selection, panel model, preview, drag arrows, OK / Apply /
   Cancel, editing a feature) and the Solids manager's model (rows, right-click menu, its actions). Moved from
   viewer/mcamui.c (+ solidsmgr_ui.inc) in phase 5 of formats/qt_port_plan.md with its comments and decomp citations;
   the mcamui names are kept. formats/extrude.md is the spec ("sec. N" below). The front ends draw the panel, the tree
   and the menu (mcamui.c nuklear, viewer-qt src/expanel.cpp / managers.cpp) over this model. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"

/* ------------------------------------------------------------------------------------------------ solids: Extrude */
/* Solids > Extrude, implemented after formats/extrude.md (MCGEOMSLD.DLL SolidsExtrudeApp "sld_newExtrude" ->
   sld_extrude, SLD_EXTRUDE_PARAMS); "sec. N" below = that file's sections. Chains are extruded along Z (solid25: the
   kernel has no other direction) into new bodies, or cut from / added to a target body. Bodies are rebuilt from the
   feature list (stored in the project as EXTRUDE + XCHAIN lines), so undo and reopening replay the same steps. */
/* sec. 2.4: the session globals - P (DAT_1803e89b0..), Create a single operation (DAT_1803bab19 = 1), Automatically
   determine operation type (DAT_1803e89a0 = 0), Calculate minimum distance (DAT_1803bab70 = 1), Automatically preview
   (DAT_1803bac10 = 1). Metric values from RegisterVariable(&v, metric, inch): Distance 50 / 1, Direction 1 / 2
   thickness 1 / 0.1 (set to the inch values at the first start in an inch part); draft 5 deg, Reverse off; Direction 1 */
ExtrudeParams ex_sess = { .type = EX_CREATE, .target = -1, .method = EXM_DISTANCE, .dist = 50, .draft_ang = 5, .thin_dir = THIN_DIR1, .thick = 1, .thick2 = 1 };
int ex_sess_single = 1, ex_auto_op = 0, ex_min_dist = 1, ex_autoprev = 1;
/* the running command (SolidsExtrudeApp) */
ExtrudeParams ex_p;              /* P (app+0x2a8) */
char ex_name[64];                /* operation name (app+0x61) */
int ex_single;                   /* Create a single operation (app+0x340) */
int ex_prev;                     /* the panel's "Automatically preview results" */
static int ex_tgt_picked;                       /* app+0x478: the target was chosen with Select Target Body (FUN_180157410, decomp/extrude/b3.c:2038) */
int ex_cdir[256], ex_cid[256];   /* per chain (parallel to cchains): direction along Z (SLD_EXT_CHAIN_DATA app+0x230), chain identity */
int ex_ninit;                    /* chains whose direction is set up (sec. 1.2) */
int ex_thin_auto;                /* app+0x2e78: Thin Wall was switched on automatically */
int ex_panel;                    /* the panel exists (after the first chain selection) */
int ex_edit_idx = -1;            /* the feature being edited (app+0x20 ModifyParams), -1 = a new op (sec. 1.7) */
int ex_tab;                      /* 0 Basic, 1 Advanced */
unsigned char ex_chsel[256];     /* Chains list selection */
int ex_pick;                     /* graphics pick for the panel: 1 AutoCursor point, 2 target body, 3 vector (line / first point), 4 vector second point */
V3 ex_pick_p0;
int ex_prev_req;                 /* Preview button (0x2bf) */
unsigned long ex_prev_hash, ex_arw_hash;
char ex_vec_txt[64]; int ex_vec_bad;
/* drag arrows (sec. 3): app+0x3c8 = the dragged arrow (0 none, 1 distance, 2 Direction 1, 4 Direction 2),
   app+0x3c0 = its index. FUN_1800dead0 saves P (type, Distance, ThinWallDirection, thicknesses) and the per-chain
   directions before the drag; Esc (FUN_1800dd1d0 -> FUN_1800dc500(1)) restores them. */
int ex_drag, ex_drag_chain;
double ex_drag_anchor[3], ex_drag_dir[3];
ExtrudeParams ex_drag_p; static int ex_drag_cdir[256];
/* expanders: Operation, Chains, Distance open; Trim to Faces, Draft, Thin Wall closed (ViewModel s_*Expanded, kept for
   the session); Plane Orientation / Preview: their initial state is NOT DECODED (open here) */
int ex_exp[8] = { 1, 1, 1, 0, 0, 0, 1, 1 };

/* mcgeomsldres strings used by this command (~/mastercam-re/regress/str/sldres.tsv) */
const char *ex_sld(int id) {
    switch (id) {
    case 201: return "Error Processing Solid";
    case 315: return "Invalid extrude distance.";
    case 337: return "No valid closed chains selected for non-thinwall extrude.";
    case 553: return "Boolean remove operation failed.";
    case 555: return "Failure drafting faces.";
    case 556: return "Failure allocating memory.";
    case 557: return "Failure extruding sheet body.";
    case 559: return "Failure generating boolean operation.";
    case 657: return "Operation requires planar chain set.";
    case 667: return "Self-intersecting body produced by extruding sheet body.";
    case 680: return "Chains must be closed for extrusion into a solid body.";
    case 1101: return "Below minimum distance.";
    }
    return NULL;
}
/* message_box_ok(id, caption): caption -1 = none (formats/extrude.md sec. 5) */
/* diagnostic: the Extrude command's steps on stderr ("extrude: ..."), always on (user request 2026-10-21) */
void ex_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); fputs("extrude: ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap); fflush(stderr);
}
void ex_report(const ExErr *e) {
    const char *t = ex_sld(e->id);
    ex_log("error box sld %d (caption %d): %s", e->id, e->caption, t ? t : "(no text)");
    if (!t) return;
    cui_message(e->caption < 0 ? "" : ex_sld(e->caption), "%s", t);
}


/* sld 851 (FUN_1800dba00) for a new op; empty in edit mode (formats/extrude.md 1.3 "Prompt while waiting"). Phase 5
   (both apps, Mastercam rule): mcamui showed sld 851 in edit mode too. */
void ex_prompt_panel(void) { if (ex_edit_idx >= 0) cam_msg[0] = 0; else snprintf(cam_msg, sizeof cam_msg, "Use dialog to select chain(s) and to modify extrude settings"); }
void ex_chaining(void) { chain_begin_extrude(); }   /* sec. 1.2: chain_it(sld 470); the Chaining dialog shows it as "%s %d" (core_ui cui_chain.c) */
/* RegisterVariable(&v, metric, inch) (mccore @1805fa3a0): the registered variable is converted whenever the system units change -
   SetUnits (@1805fa8c0) -> FUN_1805f9da0 multiplies it by metric / inch (to metric) or divides (to inch), decomp/units/core_units.c.
   ex_sess starts with the metric values (Distance 50 / 1, Thickness 1 / 0.1: formats/extrude.md 2.4); ex_sess_metric = the units
   its values are in. Called by ex_start and cui_units_set. */
static int ex_sess_metric = 1;
void ex_units(void) {
    int m = mcam_is_metric(&F);
    if (m == ex_sess_metric) return;
    double kd = 50.0 / 1.0, kt = 1.0 / 0.1;
    if (m) { ex_sess.dist *= kd; ex_sess.thick *= kt; ex_sess.thick2 *= kt; }
    else { ex_sess.dist /= kd; ex_sess.thick /= kt; ex_sess.thick2 /= kt; }
    ex_sess_metric = m;
}
void ex_start(void) {
    if (!loaded && cui_hooks.new_part) cui_hooks.new_part();   /* nothing open: File > New first (mcamui do_action(ACT_NEW)) */
    if (!loaded) return;
    geo_end(); cam_mode = 0; show_params = 0; cam_clear_selection();
    ex_units();                                          /* RegisterVariable: the session values in the system units */
    /* sec. 1.1 step 1 (ctor FUN_1800d73b0): P = the session globals, type forced to Create when no solid is visible;
       single op = DAT_1803bab19; name sld 707 "Extrude", then the default name of the type (FUN_1800db220) */
    ex_p = ex_sess; ex_p.target = -1; ex_p.legacy = 0;
    ex_tgt_picked = 0;                                   /* ctor FUN_1800d73b0 / decomp/extrude/b3.c:1242 */
    if (ex_nvis() < 1) ex_p.type = EX_CREATE;
    ex_single = ex_sess_single; ex_prev = ex_autoprev;
    snprintf(ex_name, sizeof ex_name, "%s", ex_defname(ex_p.type));
    ex_thin_auto = 0; ex_panel = 0; ex_ninit = 0; ex_pick = 0; ex_prev_req = 0; ex_prev_hash = 0; ex_tab = 0; ex_vec_bad = 0;
    scene_force_mesh = 0; ex_drag = 0; ex_edit_idx = -1;
    memset(ex_chsel, 0, sizeof ex_chsel);
    ex_mode = 1; ex_chaining();
    ex_log("start: chaining dialog up, %d visible bodies", ex_nvis());
}
/* sec. 1.7: editing an existing extrude (ctor with an op, app+0x20 = 1; slot 1 FUN_1800dec40 loads it: the op's
   params, chains and chain data; for Cut / Boss the target comes from the op, Create clears it) */
void ex_edit(int fi) {
    if (fi < 0 || fi >= nfeats || ex_mode) return;
    if (!loaded) return;
    geo_end(); cam_mode = 0; show_params = 0; cam_clear_selection();
    const ExFeat *f = &feats[fi];
    ex_edit_idx = fi;
    ex_p = f->p; ex_p.legacy = 0; ex_single = f->single; ex_prev = ex_autoprev;
    snprintf(ex_name, sizeof ex_name, "%s", f->name);
    ncchains = 0;
    for (int i = 0; i < f->nch && i < 256; i++) { chain_copy_into(&cchains[ncchains], &f->ch[i]); ex_cdir[ncchains] = f->cdir[i]; ncchains++; }
    ex_ninit = ncchains;
    if (ex_p.type == EX_CREATE) ex_p.target = -1;
    ex_thin_auto = 0; ex_panel = 1; ex_phase = 1; ex_pick = 0; ex_prev_req = 0; ex_prev_hash = 0; ex_arw_hash = 0; ex_tab = 0; ex_vec_bad = 0;
    ex_drag = 0; scene_force_mesh = 1; memset(ex_chsel, 0, sizeof ex_chsel);
    ex_mode = 1; ex_prompt_panel(); overlay_dirty = 1;
}
/* the panel values unchanged since the op was loaded (FUN_180130490 compares P, the vectors and thin wall):
   OK then does not rebuild (0x1800d8866) */
int ex_unchanged(void) {
    if (ex_edit_idx < 0) return 0;
    const ExFeat *f = &feats[ex_edit_idx];
    const ExtrudeParams *a = &ex_p, *b = &f->p;
    if (a->type != b->type || a->target != b->target || a->method != b->method || a->dist != b->dist
        || a->trim != b->trim || a->reverse != b->reverse || a->both != b->both || a->split != b->split
        || a->draft != b->draft || a->draft_rev != b->draft_rev || a->draft_ang != b->draft_ang
        || a->thin != b->thin || a->thin_dir != b->thin_dir || a->thick != b->thick || a->thick2 != b->thick2
        || a->draft_ends != b->draft_ends || ncchains != f->nch) return 0;
    for (int i = 0; i < ncchains; i++) {
        if (ex_cdir[i] != f->cdir[i] || cchains[i].n != f->ch[i].n || cchains[i].closed != f->ch[i].closed) return 0;
        for (int k = 0; k < cchains[i].n; k++) if (cchains[i].ent[k] != f->ch[i].ent[k] || cchains[i].rev[k] != f->ch[i].rev[k]) return 0;
    }
    return 1;
}
/* panel -> P tail (FUN_1801308a0 / FUN_180132310): with no visible solid or no target the type is Create */
void ex_force(void) {
    if (ex_p.target >= ex_nvis()) ex_p.target = -1;
    if (ex_nvis() < 1 || ex_p.target < 0) ex_p.type = EX_CREATE;
}
/* SetParams (FUN_180132310): DistanceMethod = 0 if > 1, Trim to faces on, or Through all with type != Cut;
   no visible solid: DistanceMethod 0, type 0 (FUN_180131600) */
void ex_setparams(void) {
    ex_force();
    if (ex_p.method > 1 || ex_p.trim || (ex_p.method == EXM_THROUGH_ALL && ex_p.type != EX_CUT)) ex_p.method = EXM_DISTANCE;
    if (ex_nvis() < 1) { ex_p.method = EXM_DISTANCE; ex_p.type = EX_CREATE; }
}
/* OperationType changed (observer FUN_180131750): the default name follows when the name is one of the three
   defaults (FUN_180132190); new op: AutoOpTypeChecked = 0 */
void ex_set_type(int t) {
    if (t == ex_p.type) return;
    if (ex_is_defname(ex_name)) snprintf(ex_name, sizeof ex_name, "%s", ex_defname(t));
    ex_p.type = t; ex_auto_op = 0;
}
/* a target chosen (FUN_1800ddf50): while the type is Create it becomes Cut and the name "Extrude Cut" (sec. 4.7) */
void ex_set_target(int b) {
    if (b < 0 || b >= ex_nvis()) return;
    ex_p.target = b; ex_tgt_picked = 1;
    if (ex_p.type == EX_CREATE) { ex_p.type = EX_CUT; ex_auto_op = 0; snprintf(ex_name, sizeof ex_name, "%s", ex_defname(EX_CUT)); }
}
/* FUN_1800d8d70 (decomp/extrude/b2.c:3291; called after every chain selection, b1.c:2418): new op, no target chosen with Select
   Target Body: the body the chains' extrusions hit becomes the target and the groups that only hit it going the other way are reversed
   (solid_target_search). Errors stay silent (FUN_1800d26d0 turns the message log off). "Automatically determine operation type"
   (FUN_1800da050, run after this) is not ported. */
static void ex_target_search(void) {
    if (ex_edit_idx >= 0 || ex_tgt_picked || !ncchains || ex_nvis() < 1) return;
    ExFeat f = ex_cur_feat(); Profile *g; int ng; ExErr e;
    if (!feat_groups(&f, &g, &ng, &e)) return;
    int *flip = calloc(ng ? ng : 1, sizeof(int));
    int t = solid_target_search(sbodies, nsbodies, g, ng, &f.p, EX_TOL, mcam_is_metric(&F), flip);
    if (t >= 0) {
        int nf = 0;
        for (int k = 0; k < ng; k++) if (flip[k]) { ex_cdir[g[k].first] = -ex_cdir[g[k].first]; nf++; }
        ex_p.target = t;
        ex_log("target search: body %d, %d chain group(s) of %d reversed", t, nf, ng);
        ex_setparams();
    }
    free(flip); groups_free(g, ng);
}
/* the Chaining dialog's OK (sec. 1.2, then 1.1 steps 4-5 for the first selection, 1.5 for Add / Rechain all) */
void ex_end(int accepted, int restart);
void ex_chains_done(void) {
    /* every chain must be planar (FUN_18000b140), else message_box_ok(657, 201) and the selection fails with its chains
       unselected. This kernel extrudes along Z only, so a chain must lie in a plane parallel to XY: a planar chain in
       another plane (Mastercam accepts it) gets the same message here. */
    int from = ex_panel ? ex_ninit : 0;
    if (from > ncchains) from = ncchains;
    ex_log("chaining OK: %d chain(s), panel already up %d", ncchains, ex_panel);
    for (int i = 0; i < ncchains; i++) { double z = 0; int xy = ex_chain_xy(&cchains[i], &z);
        ex_log("  chain %d: %d entities, %s, first ent %d, in an XY plane %d (z %.4f)", i + 1, cchains[i].n, cchains[i].closed ? "closed" : "open", cchains[i].n ? cchains[i].ent[0] : -1, xy, z); }
    for (int i = from; i < ncchains; i++) if (!ex_chain_xy(&cchains[i], NULL)) {
        ex_log("  chain %d is not in a plane parallel to XY -> refused (sld 657)", i + 1);
        ExErr e = { 657, 201 }; ex_report(&e);
        for (int k = from; k < ncchains; k++) cam_chain_free(&cchains[k]);
        ncchains = from; overlay_dirty = 1;
        if (!ex_panel) { ex_end(0, 0); return; }        /* no panel yet: the command ends (1.1 step 2) */
        ex_phase = 1; ex_prompt_panel(); return;
    }
    /* direction (1.2): chn_get_normal oriented to the C-plane +Z (this program's C-plane is always Top, reference +Z);
       a chain whose normal points away is reversed (reverse_chain) and its normal recomputed; no normal -> reference */
    int any_open = 0;
    for (int i = 0; i < ncchains; i++) {
        if (i < ex_ninit && ex_cid[i] == cchains[i].ent[0]) continue;
        if (ex_chain_normal(&cchains[i]) < 0) cam_chain_set_dir(&F, &cchains[i], 1);
        ex_cdir[i] = 1; ex_cid[i] = cchains[i].ent[0]; ex_chsel[i] = 0;
        if (!cchains[i].closed) any_open = 1;
    }
    ex_ninit = ncchains;
    if (!ex_panel) {        /* 1.1 step 5 (FUN_180157140): not every chain closed -> Thin Wall on (app+0x2e78 = 1 when it was off);
           FUN_180157090: exactly one visible solid -> it is the target, else none. Step 6, the target search
           FUN_1800d8d70: ex_target_search below. */
        if (any_open) { if (!ex_p.thin) ex_thin_auto = 1; ex_p.thin = 1; ex_exp[5] = 1; }
        ex_p.target = ex_nvis() == 1 ? 0 : -1;
        ex_panel = 1;
        ex_setparams();
    } else if (any_open && !ex_p.thin) { ex_p.thin = 1; ex_thin_auto = 0; ex_exp[5] = 1; }   /* 1.5: FUN_180132910(dlg, 1); the ThinWall change posts 0x329 */
    /* 1.5: Rechain all to nothing in edit mode -> MessageBox(sld 644, caption sld 745, MB_ICONINFORMATION) (FUN_180157710) */
    if (ex_edit_idx >= 0 && !ncchains) cui_message("Delete Chain", "Base chain cannot be deleted");
    ex_target_search();
    ex_phase = 1; ex_prompt_panel();
    ex_log("panel up: type %d thin %d (auto %d) dist %g target %d", ex_p.type, ex_p.thin, ex_thin_auto, ex_p.dist, ex_p.target);
    scene_force_mesh = 1;                          /* the preview is drawn shaded even in Wireframe display (sec. 3, ours) */
}

/* sec. 1.3: the "Below minimum distance." check (0x1800d892f / 0x1800d8a42): on a final accept (OK / Apply) or a
   preview request, a new op with >= 1 chain and abs(Distance) < app+0x8 (= tol * 10, EX_MINDIST) gets
   message_box_ok(sld 1101, caption 201) instead of the rebuild */
int ex_below_min(void) {
    if (ex_edit_idx >= 0 || !ncchains || fabs(ex_p.dist) >= EX_MINDIST) return 0;
    ExErr e = { 1101, 201 }; ex_report(&e);
    return 1;
}
/* Esc (get_event returns 1) / closing the Chaining dialog */
void ex_escape(void) {
    ex_log("escape (Esc, right click or the Chaining window closed): drag %d pick %d phase %d panel %d", ex_drag, ex_pick, ex_phase, ex_panel);
    if (ex_drag) { ex_drag_end(1); return; }             /* during an arrow drag: cancel it, restore the values (FUN_1800dd1d0) */
    if (ex_pick) { ex_pick = 0; ex_prompt_panel(); return; }
    if (!ex_phase && ex_panel) {                         /* chain_it cancelled from the panel (1.5): back to the panel */
        for (int k = ex_ninit; k < ncchains; k++) cam_chain_free(&cchains[k]);
        if (ncchains > ex_ninit) ncchains = ex_ninit;
        ex_phase = 1; ex_prompt_panel(); overlay_dirty = 1; return;
    }
    ex_end(0, 0);                                        /* not accepted (= Cancel) */
}
/* sec. 1.6 (slot 5 FUN_1800d83b0) */
void ex_end(int accepted, int restart) {
    ex_log("end: accepted %d restart %d, %d chain(s), %d feature(s), %d bod(ies)", accepted, restart, ncchains, nfeats, nsbodies);
    if (accepted) {
        /* P -> session globals, except: Thin Wall switched on automatically and never touched is stored off; the
           vector = the first chain's direction; the target is cleared. Single op -> DAT_1803bab19. */
        ExtrudeParams s = ex_p;
        if (ex_thin_auto) s.thin = 0;
        s.vec[0] = s.vec[1] = 0; s.vec[2] = ncchains ? ex_cdir[0] : 1;
        s.target = -1; s.legacy = 0;
        ex_sess = s; ex_sess_single = ex_single;
    }
    ex_autoprev = ex_prev;                               /* the panel's Preview flag -> DAT_1803bac10 (also on Cancel) */
    ex_mode = 0; ex_phase = 0; ex_panel = 0; ex_pick = 0; scene_force_mesh = 0; ex_drag = 0; ex_edit_idx = -1;
    cam_clear_selection(); bodies_show(sbodies, nsbodies); cam_msg[0] = 0; overlay_dirty = 1;   /* chains freed, ClosePrompt */
    if (accepted && restart) ex_start();                 /* Apply: post_FT_command("sld_newExtrude") */
}
void ex_cancel(void) { ex_end(0, 0); }            /* 0x2be */
/* the pending operation (all chains) */
ExFeat ex_cur_feat(void) {
    ExFeat f; memset(&f, 0, sizeof f);
    f.p = ex_p; f.nch = ncchains; f.ch = cchains; f.cdir = ex_cdir; f.single = ex_single;
    f.p.vec[0] = f.p.vec[1] = 0; f.p.vec[2] = ncchains ? ex_cdir[0] : 1;
    snprintf(f.name, sizeof f.name, "%s", ex_name);
    return f;
}
/* sec. 3: the preview is the real sld_extrude result (preview = 1), rebuilt on every change while "Automatically
   preview results" is on (2.3: posts 0x2bf) and by the Preview button. A failure pops its message box and shows the
   bodies without the extrusion (PK_MARK_goto). The preview's colour and how a previous preview goes away are NOT
   DECODED: the result is shown like the bodies. */
void ex_preview(void) {
    unsigned long h = 1469598103u;
    ExFeat f = ex_cur_feat();
    const unsigned char *q = (const unsigned char *)&f.p; for (size_t i = 0; i < sizeof f.p; i++) h = (h ^ q[i]) * 16777619u;
    h = (h ^ (unsigned)ncchains) * 16777619u;
    for (int i = 0; i < ncchains; i++) h = (h ^ (unsigned)cchains[i].ent[0] ^ (unsigned)cchains[i].n << 16 ^ (unsigned)(ex_cdir[i] + 2) << 28) * 16777619u;
    h = (h ^ (unsigned)ex_prev) * 16777619u; h = (h ^ (unsigned)(ex_edit_idx + 2)) * 16777619u;
    int changed = h != ex_prev_hash; ex_prev_hash = h;
    if (!ex_prev_req && !(ex_prev && changed)) return;
    ex_prev_req = 0;
    if (ex_below_min()) return;                      /* sec. 3: not rebuilt when the min-distance check fails */
    int nt; Body *tmp; ExErr e;
    if (ex_edit_idx >= 0) {
        /* edit: the preview is the history replayed with the op replaced by the panel's values (the original's
           edit-mode regeneration pipeline is NOT DECODED, sec. 1.7 - ours) */
        tmp = NULL; nt = 0;
        for (int i = 0; i < nfeats; i++) feat_apply(&tmp, &nt, i == ex_edit_idx ? &f : &feats[i], &e);
    } else {
        tmp = bodies_dup(&nt);
        if (ncchains && !feat_apply(&tmp, &nt, &f, &e)) ex_report(&e);
    }
    bodies_show(tmp, nt); bodies_free(&tmp, &nt);
}
/* OK (0x2bd) / Apply = OK-New (0x2c0): the loop ends accepted; new op with >= 1 chain: sld_extrude (final). The
   "Below minimum distance." check (sld 1101) compares with app+0x8 = tol * 10 (EX_MINDIST): done below (phase 5).
   Sec. 4.7 / 6: Create, or "Create a single operation" off, or Trim to faces -> one operation per group (per created
   body); otherwise (Cut / Boss with a single operation) one operation for all chains. The command ends either way
   (1.6); Apply starts it again. */
void ex_accept(int apply) {
    ex_log("%s pressed: %d chain(s), type %d dist %g reverse %d both %d thin %d", apply ? "Apply" : "OK", ncchains, ex_p.type, ex_p.dist, ex_p.reverse, ex_p.both, ex_p.thin);
    ex_force();
    if (ex_edit_idx >= 0) {
        /* edit (sec. 1.7): OK with unchanged parameters does not rebuild; otherwise the op is replaced with the
           panel's values and the history replayed. The original's regen messages are NOT DECODED: a failing replay
           reports its error and the edit is kept open. */
        if (ex_unchanged()) { ex_end(1, 0); return; }
        if (ncchains) {
            ExFeat cur = ex_cur_feat();
            ExFeat newf = feat_dup(&cur), old = feats[ex_edit_idx];
            feats[ex_edit_idx] = newf;
            Body *tmp = NULL; int nt = 0; ExErr e; int ok = 1;
            for (int i = 0; i < nfeats && ok; i++) if (!feat_apply(&tmp, &nt, &feats[i], &e)) ok = 0;
            bodies_free(&tmp, &nt);
            if (!ok) { feats[ex_edit_idx] = old; feat_free(&newf); ex_report(&e); return; }   /* the edit stays open */
            FeatPack *k = malloc(sizeof *k); k->n = 1; k->f = malloc(sizeof(ExFeat)); k->f[0] = old;
            URec r; memset(&r, 0, sizeof r); r.kind = U_FEAT; r.feat = k; r.idx = 2; r.nd = ex_edit_idx; undo_push(r);
            doc_dirty = 1; solids_rebuild();
        }
        ex_end(1, apply);
        return;
    }
    ex_force();
    /* sec. 1.3 (0x1800d892f): a final accept of a new op with >= 1 chain and abs(Distance) < app+0x8 (tol * 10) gets
       message_box_ok(sld 1101, caption 201) instead of sld_extrude; the loop ends accepted as for any OK / Apply (1.6).
       Phase 5 (both apps, Mastercam rule): mcamui made the feature anyway. */
    if (ex_below_min()) { ex_end(1, apply); return; }
    if (ncchains) {
        ExFeat all = ex_cur_feat(), *fs = calloc(ncchains, sizeof(ExFeat)); int nf = 0;
        if (ex_p.type == EX_CREATE || !ex_single || ex_p.trim) {
            ExGrp *gr = NULL; int ngr = 0; ExErr ge;
            if (ex_grouping(&all, &gr, &ngr, &ge)) {            /* sec. 6: one operation per group */
                for (int i = 0; i < ngr; i++) {
                    ExFeat g = all; g.ch = malloc(sizeof(Chain) * gr[i].n); g.cdir = malloc(sizeof(int) * gr[i].n); g.nch = 0;
                    for (int m = 0; m < gr[i].n; m++) { g.ch[g.nch] = cchains[gr[i].ix[m]]; g.cdir[g.nch++] = ex_cdir[gr[i].ix[m]]; }
                    g.p.vec[2] = g.cdir[0];
                    fs[nf] = feat_dup(&g); nf++;
                    free(g.ch); free(g.cdir);
                }
                for (int i = 0; i < ngr; i++) free(gr[i].ix);
                free(gr);
            } else fs[nf++] = feat_dup(&all);                  /* grouping failed: one feature, feat_apply reports it (ours) */
        } else fs[nf++] = feat_dup(&all);
        int nt; Body *tmp = bodies_dup(&nt); ExErr e; int ok = 1;
        for (int i = 0; i < nf && ok; i++) if (!feat_apply(&tmp, &nt, &fs[i], &e)) ok = 0;
        bodies_free(&tmp, &nt);
        if (!ok) { ex_report(&e); for (int i = 0; i < nf; i++) feat_free(&fs[i]); free(fs); }
        else {
            for (int i = 0; i < nf; i++) feat_push(&fs[i]);
            FeatPack *k = malloc(sizeof *k); k->n = nf; k->f = fs;
            URec r; memset(&r, 0, sizeof r); r.kind = U_FEAT; r.feat = k; r.idx = 0; undo_push(r);
            doc_dirty = 1; solids_rebuild();
        }
    }
    ex_end(1, apply);
}
void feat_delete_last(void) {
    if (!nfeats) return;
    FeatPack *k = malloc(sizeof *k); k->n = 1; k->f = malloc(sizeof(ExFeat)); k->f[0] = feat_dup(&feats[nfeats - 1]);
    URec r; memset(&r, 0, sizeof r); r.kind = U_FEAT; r.feat = k; r.idx = 1; undo_push(r);
    feat_pop(); doc_dirty = 1; solids_rebuild();
    cam_msg[0] = 0;
}
/* Chains list (sec. 2.1, 1.4): Remove (0x325) unlinks and frees a chain and drops its chain data; Reverse (0x326)
   negates its direction; Reverse all toggles P+0x39 */
int ex_any_sel(void) { for (int i = 0; i < ncchains; i++) if (ex_chsel[i]) return 1; return 0; }
void ex_remove_sel(void) {
    /* edit mode: the base chain (the first) cannot be deleted - MessageBox(sld 644, caption 745) (model 0x290 FUN_18012d280) */
    if (ex_edit_idx >= 0 && ncchains && ex_chsel[0]) { cui_message("Delete Chain", "Base chain cannot be deleted"); ex_chsel[0] = 0; if (!ex_any_sel()) return; }
    int k = 0;
    for (int i = 0; i < ncchains; i++) {
        if (ex_chsel[i]) { cam_chain_free(&cchains[i]); continue; }
        cchains[k] = cchains[i]; ex_cdir[k] = ex_cdir[i]; ex_cid[k] = ex_cid[i]; k++;
    }
    ncchains = ex_ninit = k; memset(ex_chsel, 0, sizeof ex_chsel); overlay_dirty = 1;
}
void ex_reverse_sel(void) { for (int i = 0; i < ncchains; i++) if (ex_chsel[i]) ex_cdir[i] = -ex_cdir[i]; }
/* Add chains (0x321, 1) / Rechain all (0x321, 0): chain selection again with the panel suspended; Rechain all
   clears the list first (FUN_1800de090) */
void ex_add_chains(int all) {
    if (all) { cam_clear_selection(); ex_ninit = 0; memset(ex_chsel, 0, sizeof ex_chsel); }
    ex_chaining();
}
/* Plane Orientation: every chain's direction = the vector (typed: FUN_180131fb0; Select vector:
   sld_extrude_prompt_for_dir). The kernel extrudes along Z only: a vector with an X or Y part is not taken (0). */
int ex_set_vector(const double v[3]) {
    double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l < EX_TOL || hypot(v[0], v[1]) > EX_TOL * l) return 0;
    for (int i = 0; i < ncchains; i++) ex_cdir[i] = v[2] > 0 ? 1 : -1;
    return 1;
}
/* AutoCursor (0x322, FUN_1800de680): Distance method = Distance; per chain the signed projection of (point - chain
   start) on its direction; if abs > app+0x8 (= tol * 10, EX_MINDIST) a negative one flips that chain; the first
   chain's value (made positive) becomes Distance */
void ex_autocursor(V3 p) {
    for (int i = 0; i < ncchains; i++) {
        double z = 0; ex_chain_xy(&cchains[i], &z);
        double v = (p.z - z) * ex_cdir[i];
        if (fabs(v) > EX_MINDIST && v < 0) ex_cdir[i] = -ex_cdir[i];   /* app+0x8 = tol * 10 (FUN_180164c60, decomp/extrude/b5.c:146); phase 5: was 0 */
        if (i == 0) ex_p.dist = fabs(v);
    }
    ex_pick = 0; ex_prompt_panel();
}
/* the body under the cursor (sld_pick_one_ent for the target, sec. 4.7) */
int ex_pick_body(float mx, float my) {
    double vx, vy, o[3], o2[3], d[3]; view_px_to_view(mx - vp_x, my - vp_y, &vx, &vy);
    view_to_world3(vx, vy, 1e6, o); view_to_world3(vx, vy, 0, o2);
    for (int k = 0; k < 3; k++) d[k] = o2[k] - o[k];
    int best = -1; double bt = 1e300;
    for (int i = 0; i < nsbodies; i++) { double t; if (body_hit(&sbodies[i], o, d, &t) && t < bt) { bt = t; best = i; } }
    return best;
}
void ex_click(float mx, float my, const double w[3]) {   /* w = the world point under the cursor (the UI's scene_pick) */
    V3 p = { w[0], w[1], w[2] };
    if (ex_pick == 1) { geo_snap(&p, NULL, -1, 0); ex_autocursor(p); return; }
    if (ex_pick == 2) { int b = ex_pick_body(mx, my); if (b >= 0) { ex_set_target(b); ex_pick = 0; ex_prompt_panel(); } return; }
    if (ex_pick == 3) {                                  /* sld 905: a line, or the first of two points (mask 0x4003) */
        int e = pick_entity(mx, my);
        if (e >= 0 && F.ents[e].type == E_LINE) { const Ent *en = &F.ents[e]; double v[3] = { en->b.x - en->a.x, en->b.y - en->a.y, en->b.z - en->a.z };
            ex_vec_bad = !ex_set_vector(v); ex_pick = 0; ex_prompt_panel(); return; }
        if (!geo_snap(&p, NULL, -1, 0) && !geo_cursor_on_plane(&p)) { snprintf(cam_msg, sizeof cam_msg, "No point: pick a snapped point in this view"); return; }   /* ours, as cui_stock.c st_pick_click */
        ex_pick_p0 = p; ex_pick = 4; snprintf(cam_msg, sizeof cam_msg, "Set extrusion direction."); return;   /* sld 485 */
    }
    if (ex_pick == 4) { if (!geo_snap(&p, &ex_pick_p0, 0, 0) && !geo_cursor_on_plane(&p)) { snprintf(cam_msg, sizeof cam_msg, "No point: pick a snapped point in this view"); return; } double v[3] = { p.x - ex_pick_p0.x, p.y - ex_pick_p0.y, p.z - ex_pick_p0.z };   /* the two-point direction pick gets the line-like anchor modes (its rubber-band type number is NOT DECODED - mapped to 0 here) */
        ex_vec_bad = !ex_set_vector(v); ex_pick = 0; ex_prompt_panel(); return; }
    /* event 2 (FUN_1800dde10): an entity of a selected chain -> that chain's direction is negated */
    int e = pick_entity(mx, my); if (e < 0) return;
    for (int i = 0; i < ncchains; i++) for (int k = 0; k < cchains[i].n; k++) if (cchains[i].ent[k] == e) { ex_cdir[i] = -ex_cdir[i]; return; }
}

/* ---- drag arrows (sec. 3, FUN_1800dfb70 / dfcb0 / d9330 / e0110, drag handlers FUN_1800ddc70 / dd7c0 / dd960 /
   ddab0 / dd1d0). One distance arrow per chain: anchored at the chain's start point, along the chain's direction
   (negated by Reverse all, app+0x2e1), offset = Distance. Thin wall: a Direction 1 (red) and Direction 2 (green)
   arrow on the last chain, anchored at the first entity's mid point, along -/+ the side vector d x t (FUN_180097650,
   negated for closed chains). Arrow length / head size are ours (the original's shaft is a screen-constant
   0.1 x viewport height); the distance arrow's colour is NOT DECODED (orange here), red / green are the decoded
   UtColor(255,0,0) / UtColor(0,255,0). */
/* ExArrow: core_ui.h */
/* FUN_180097650: the first chain curve at its mid parameter (eval_curve (u0+u1)/2), tangent in the traversal dir */
int ex_mid_tan(const Chain *c, double p[3], double t[3]) {
    if (!c->n || c->ent[0] < 0 || c->ent[0] >= F.nents) return 0;
    const Ent *en = &F.ents[c->ent[0]]; int rev = c->rev ? c->rev[0] : 0;
    if (en->type == E_LINE) {
        p[0] = (en->a.x + en->b.x) / 2; p[1] = (en->a.y + en->b.y) / 2; p[2] = (en->a.z + en->b.z) / 2;
        double dx = en->b.x - en->a.x, dy = en->b.y - en->a.y, dz = en->b.z - en->a.z, l = sqrt(dx * dx + dy * dy + dz * dz);
        if (l < 1e-12) return 0;
        double s = rev ? -1 : 1; t[0] = s * dx / l; t[1] = s * dy / l; t[2] = s * dz / l; return 1;
    }
    if (en->type == E_ARC) {
        double a = en->a0 + en->sw / 2;
        p[0] = en->c.x + en->r * cos(a); p[1] = en->c.y + en->r * sin(a); p[2] = en->c.z;
        double s = (en->sw >= 0 ? 1 : -1) * (rev ? -1 : 1);
        t[0] = -sin(a) * s; t[1] = cos(a) * s; t[2] = 0; return 1;
    }
    return 0;
}
int ex_arrow_list(ExArrow *ar, int nmax) {
    int n = 0;
    for (int i = 0; i < ncchains && n < nmax; i++) {     /* the distance arrow of every chain */
        double *xy = NULL, z; int np = cam_chain_polyline(&F, &cchains[i], 0.01, &xy, &z);
        if (np < 1) { free(xy); continue; }
        ExArrow *a = &ar[n++];
        a->a[0] = xy[0]; a->a[1] = xy[1]; a->a[2] = z;
        a->d[0] = a->d[1] = 0; a->d[2] = ex_cdir[i] * (ex_p.reverse ? -1 : 1);
        a->val = ex_p.dist; a->kind = 1; a->chain = i;
        free(xy);
    }
    if (ex_p.thin && ncchains && n + 2 <= nmax) {        /* the thickness arrows on the last chain (FUN_1800e0110) */
        double p[3], t[3];
        if (ex_mid_tan(&cchains[ncchains - 1], p, t)) {
            double d[3] = { 0, 0, (double)ex_cdir[ncchains - 1] };
            double s[3] = { d[1] * t[2] - d[2] * t[1], d[2] * t[0] - d[0] * t[2], d[0] * t[1] - d[1] * t[0] };   /* d x t */
            double l = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
            if (l > EX_TOL) {
                for (int k = 0; k < 3; k++) s[k] /= l;
                if (cchains[ncchains - 1].closed) for (int k = 0; k < 3; k++) s[k] = -s[k];   /* the per-chain side flag (FUN_180097650 param_3): closed -> outward */
                if (ex_p.thin_dir != THIN_DIR2) { ExArrow *a = &ar[n++]; a->a[0] = p[0]; a->a[1] = p[1]; a->a[2] = p[2];
                    a->d[0] = -s[0]; a->d[1] = -s[1]; a->d[2] = -s[2]; a->val = ex_p.thick; a->kind = 2; a->chain = ncchains - 1; }
                if (ex_p.thin_dir != THIN_DIR1) { ExArrow *a = &ar[n++]; a->a[0] = p[0]; a->a[1] = p[1]; a->a[2] = p[2];
                    a->d[0] = s[0]; a->d[1] = s[1]; a->d[2] = s[2]; a->val = ex_p.thick2; a->kind = 4; a->chain = ncchains - 1; }
            }
        }
    }
    return n;
}
double ex_seg_dist(double mx, double my, double x0, double y0, double x1, double y1) {
    double dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((mx - x0) * dx + (my - y0) * dy) / l2 : 0;
    if (t < 0) t = 0; if (t > 1) t = 1;
    return hypot(mx - (x0 + t * dx), my - (y0 + t * dy));
}
/* the arrow under the cursor (FUN_1800dee10: the arrow lists hit-tested in order distance, Direction 1, Direction 2) */
int ex_arrow_hit(float mx, float my, int *kind, double a[3], double d[3]) {
    ExArrow ar[258]; int n = ex_arrow_list(ar, 258);
    int best = -1; double bd = 8;                        /* px aperture, ours (the GrArrow pick size is NOT DECODED) */
    for (int i = 0; i < n; i++) {
        float x0, y0, x1, y1;
        if (!scene_to_screen(vp_w, vp_h, ar[i].a[0], ar[i].a[1], ar[i].a[2], &x0, &y0)) continue;
        if (!scene_to_screen(vp_w, vp_h, ar[i].a[0] + ar[i].d[0] * ar[i].val, ar[i].a[1] + ar[i].d[1] * ar[i].val,
                             ar[i].a[2] + ar[i].d[2] * ar[i].val, &x1, &y1)) continue;
        double dd = ex_seg_dist(mx - vp_x, my - vp_y, x0, y0, x1, y1);
        if (dd < bd) { bd = dd; best = i; }
    }
    if (best < 0) return 0;
    *kind = ar[best].kind; memcpy(a, ar[best].a, sizeof ar[best].a); memcpy(d, ar[best].d, sizeof ar[best].d);
    ex_drag_chain = ar[best].chain;
    return 1;
}
/* drag start (FUN_1800ddc70): the values and chain directions are saved (FUN_1800dead0), prompt sld 505 */
void ex_drag_start(int kind, const double a[3], const double d[3]) {
    ex_drag = kind; ex_drag_p = ex_p;
    for (int i = 0; i < ncchains; i++) ex_drag_cdir[i] = ex_cdir[i];
    memcpy(ex_drag_anchor, a, sizeof ex_drag_anchor); memcpy(ex_drag_dir, d, sizeof ex_drag_dir);
    snprintf(cam_msg, sizeof cam_msg, "Select new position or Esc to cancel");
    overlay_dirty = 1;
}
/* drag move (FUN_1800dd7c0): the live value is the cursor projected on the arrow axis. Distance: abs(offset), a
   negative one flips all chain directions (app+0x341); Direction 1 / 2: the thickness, direction -> Both. */
void ex_drag_move(float mx, float my) {
    double vx, vy, o[3], o2[3], w[3];
    view_px_to_view(mx - vp_x, my - vp_y, &vx, &vy);
    view_to_world3(vx, vy, 1e6, o); view_to_world3(vx, vy, 0, o2);
    for (int k = 0; k < 3; k++) w[k] = o2[k] - o[k];
    double lw = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (lw < 1e-12) return;
    for (int k = 0; k < 3; k++) w[k] /= lw;
    const double *A = ex_drag_anchor, *D = ex_drag_dir;
    double u[3] = { A[0] - o[0], A[1] - o[1], A[2] - o[2] };
    double c = D[0] * w[0] + D[1] * w[1] + D[2] * w[2];
    if (fabs(c) > 0.999) return;                         /* looking along the arrow: no projection */
    double ud = u[0] * D[0] + u[1] * D[1] + u[2] * D[2], uw = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
    double s = (c * uw - ud) / (1 - c * c);
    if (ex_drag == 1) {
        if (s < 0) { for (int i = 0; i < ncchains; i++) ex_cdir[i] = -ex_drag_cdir[i]; }
        else for (int i = 0; i < ncchains; i++) ex_cdir[i] = ex_drag_cdir[i];
        ex_p.dist = fabs(s);
    } else if (ex_drag == 2) { ex_p.thick = fabs(s); if (ex_p.thin_dir == THIN_DIR1) ex_p.thin_dir = THIN_BOTH; }
    else if (ex_drag == 4) { ex_p.thick2 = fabs(s); if (ex_p.thin_dir == THIN_DIR2) ex_p.thin_dir = THIN_BOTH; }
    overlay_dirty = 1;
}
/* drag end (FUN_1800dd960 -> ddab0): commit; cancel = Esc (FUN_1800dd1d0 -> FUN_1800dc500(1)) restores */
void ex_drag_end(int cancel) {
    if (cancel) { ex_p = ex_drag_p; for (int i = 0; i < ncchains; i++) ex_cdir[i] = ex_drag_cdir[i]; }
    ex_drag = 0; ex_prompt_panel(); overlay_dirty = 1;
}

/* ---- the panel: WPF SolidsExtrudeManagerView (formats/extrude.md sec. 2), title sld 846 "Solid Extrude". Labels are
   the BAML texts without their access-key underscores (access keys are not implemented); icon-only buttons are
   labelled with their tooltip titles (resx MCGeomSld.UI.Solids.Extrude.SolidsExtrudeManagerViewToolTips). */
/* EXV_*: core_ui.h */
/* validator FUN_18020fbd0 (sec. 2.1 / 2.2): the field shows an error state (HasError), its text as the tooltip.
   Direction 1 / 2 are checked while their box is enabled (when else the validator runs is NOT DECODED). */
void ex_validate(const char **e) {
    for (int i = 0; i < EXV_N; i++) e[i] = NULL;
    const char *s = ex_name; while (*s == ' ' || *s == '\t') s++;
    if (!*s) e[EXV_NAME] = "Name cannot be blank";                                                        /* sld 2001 */
    if (!ncchains) e[EXV_CHAINS] = "Extrude requires at least one chain";                                 /* sld 2015 */
    if (ex_p.dist < EX_TOL) e[EXV_DIST] = "Distance must be greater than 0";                              /* sld 2016, whatever the method */
    if (ex_p.type != EX_CREATE && ex_p.trim) e[EXV_FACES] = "Trim to faces requires at least one face";   /* sld 2017 (no face can be picked here) */
    if (ex_p.draft && !(ex_p.draft_ang >= 0 && ex_p.draft_ang <= 89)) e[EXV_ANGLE] = "Draft angle must be between 0 and 90 degrees";   /* sld 2018, code limit 89 */
    if (ex_p.thin && ex_p.thin_dir != THIN_DIR2 && ex_p.thick < EX_TOL) e[EXV_D1] = "Direction 1 distance must be greater than 0";   /* sld 2019 */
    if (ex_p.thin && ex_p.thin_dir != THIN_DIR1 && ex_p.thick2 < EX_TOL) e[EXV_D2] = "Direction 2 distance must be greater than 0";  /* sld 2020 */
}

/* ---- the panel's frame (mcamui.c draw_extrude_panel head): P forced (ex_force), the preview rebuilt when due
   (ex_preview), and sec. 2.3 flags 1/2/4/7: a change of Distance, Reverse all, Thin Wall (direction, thicknesses) or
   the vector redraws the arrows (FUN_1800dfb70). mcamui runs it every frame the panel is up; the Qt app after every
   change (both: never during a scripted step - mcamui runs the whole script before its first frame). */
void ex_panel_frame(void) {
    if (!ex_mode || !ex_phase) return;
    ex_force();
    ex_preview();
    unsigned long h = 1469598103u;
    const unsigned char *q = (const unsigned char *)&ex_p; for (size_t i = 0; i < sizeof ex_p; i++) h = (h ^ q[i]) * 16777619u;
    for (int i = 0; i < ncchains; i++) h = (h ^ (unsigned)(ex_cdir[i] + 2)) * 16777619u;
    h = (h ^ (unsigned)ncchains) * 16777619u;
    if (h != ex_arw_hash) { ex_arw_hash = h; overlay_dirty = 1; }
}

/* the extrude drag arrows as overlay lines (mcamui.c ov_extra, sec. 3, FUN_1800dfb70); hl = the arrowhead size, ours
   (the original shaft is screen-constant) */
void ex_ov_arrows(float **pv, int *pn, int *pcap) {
#define OV(P, R, G, B) ov_pt(pv, pn, pcap, P, R, G, B)
    if (!(ex_mode && ex_phase)) return;
    ExArrow ar[258]; int na = ex_arrow_list(ar, 258);
    double hl = ext * 0.02 + 1e-6;                /* arrowhead size, ours (the original shaft is screen-constant) */
    for (int i = 0; i < na; i++) {
        float r = 1, g = .8f, b = 0;              /* distance: orange (the default GrArrow colour is NOT DECODED) */
        if (ar[i].kind == 2) { r = 1; g = 0; b = 0; }         /* Direction 1: UtColor(255,0,0) */
        else if (ar[i].kind == 4) { r = 0; g = 1; b = 0; }    /* Direction 2: UtColor(0,255,0) */
        V3 a0 = { ar[i].a[0], ar[i].a[1], ar[i].a[2] };
        V3 tp = { a0.x + ar[i].d[0] * ar[i].val, a0.y + ar[i].d[1] * ar[i].val, a0.z + ar[i].d[2] * ar[i].val };
        OV(a0, r, g, b); OV(tp, r, g, b);
        double p1[3], up[3] = { 0, 0, 1 };
        if (fabs(ar[i].d[2]) > 0.9) { up[0] = 1; up[2] = 0; }
        p1[0] = ar[i].d[1] * up[2] - ar[i].d[2] * up[1]; p1[1] = ar[i].d[2] * up[0] - ar[i].d[0] * up[2]; p1[2] = ar[i].d[0] * up[1] - ar[i].d[1] * up[0];
        double l = hypot(p1[0], hypot(p1[1], p1[2])); if (l < 1e-12) continue;
        for (int k = 0; k < 3; k++) p1[k] /= l;
        double p2[3] = { ar[i].d[1] * p1[2] - ar[i].d[2] * p1[1], ar[i].d[2] * p1[0] - ar[i].d[0] * p1[2], ar[i].d[0] * p1[1] - ar[i].d[1] * p1[0] };
        for (int s = -1; s <= 1; s += 2) {
            V3 h1 = { tp.x - ar[i].d[0] * hl + s * p1[0] * hl * .4f, tp.y - ar[i].d[1] * hl + s * p1[1] * hl * .4f, tp.z - ar[i].d[2] * hl + s * p1[2] * hl * .4f };
            OV(tp, r, g, b); OV(h1, r, g, b);
            V3 h2 = { tp.x - ar[i].d[0] * hl + s * p2[0] * hl * .4f, tp.y - ar[i].d[1] * hl + s * p2[1] * hl * .4f, tp.z - ar[i].d[2] * hl + s * p2[2] * hl * .4f };
            OV(tp, r, g, b); OV(h2, r, g, b);
        }
    }
#undef OV
}

/* the panel controls' IsEnabled bindings (formats/extrude.md sec. 2.1 / 2.2; mcamui.c ex_page_basic / ex_page_advanced):
   ModifyParams (sec. 1.7): Create body and "Create a single operation" disabled; Cut / Boss enabled iff the op's type !=
   Create (EnableCutBoss), new op: a target is set; the target button (EnableSelectTargetBody = new op and > 1 visible
   solid) and AutoOpType are new-op only; Through all = Cut body checked; the Trim to Faces header when type != Create,
   its content when Distance, type != Create and checked; Calculate minimum distance: edit -> false (model 0x128);
   Draft's controls with Draft (Split also Both directions, Draft ends also Thin Wall); Thin Wall's with Thin Wall,
   Direction 1 / 2 boxes = EnableDirection1 / 2; the Preview button = !Preview; OK / Apply / Cancel and both tabs off
   during an arrow drag (FUN_180130720/760/780/740/700(0)) */
void ex_enables(ExEn *e) {
    memset(e, 0, sizeof *e);
    e->edit = ex_edit_idx >= 0; e->has_t = ex_p.target >= 0; e->nvis = ex_nvis();
    e->create = !e->edit;
    e->cut = e->boss = e->edit ? ex_p.type != EX_CREATE : e->has_t;
    e->target = e->has_t;
    e->sel_target = !e->edit && e->nvis > 1;
    e->single = !e->edit;
    e->auto_op = !e->edit && e->nvis >= 1 && e->has_t;   /* AutoOpTypeEnabled; the classification (4.8) is NOT DECODED and not done */
    e->through_all = ex_p.type == EX_CUT;
    e->trim_hdr = ex_p.type != EX_CREATE;
    e->trim_content = ex_p.method == EXM_DISTANCE && ex_p.type != EX_CREATE && ex_p.trim;
    e->min_dist = !e->edit && e->trim_content;
    e->draft_ctl = ex_p.draft; e->split = ex_p.draft && ex_p.both; e->draft_ends = ex_p.draft && ex_p.thin;
    e->thin_ctl = ex_p.thin; e->d1 = ex_p.thin && ex_p.thin_dir != THIN_DIR2; e->d2 = ex_p.thin && ex_p.thin_dir != THIN_DIR1;
    e->preview_btn = !ex_prev;
    e->buttons = e->tabs = !ex_drag;
}

/* panel actions shared by both front ends (mcamui.c ex_page_basic / ex_page_advanced inline code) */
void ex_btn_select_target(void) { ex_pick = 2; snprintf(cam_msg, sizeof cam_msg, "Select target body to cut or add boss to."); }   /* 0x324 -> prompt sld 857 */
void ex_btn_autocursor(void) {                       /* 0x322: also DistanceMethod = 0; prompt sld 459 */
    ex_p.method = EXM_DISTANCE; ex_pick = 1; snprintf(cam_msg, sizeof cam_msg, "Select point to extrude to (in construction view)."); }
void ex_btn_select_vector(void) { ex_pick = 3; snprintf(cam_msg, sizeof cam_msg, "Select line, edge or two points to specify extrusion direction."); }   /* sld 905 */
void ex_set_normal(void) {                           /* FUN_1801329f0: each direction = its chn_get_normal */
    for (int i = 0; i < ncchains; i++) { int n = ex_chain_normal(&cchains[i]); ex_cdir[i] = n ? n : 1; } ex_vec_bad = 0; }
void ex_set_cplane(void) { for (int i = 0; i < ncchains; i++) ex_cdir[i] = 1; ex_vec_bad = 0; }   /* FUN_180132930: the C-plane +Z (Top) */
void ex_set_through_all(void) { ex_p.method = EXM_THROUGH_ALL; ex_p.trim = 0; }   /* clears TrimToFaces (model 0xd0 FUN_18012dd30) */
/* a checkbox header (Trim to Faces 3 / Draft 4 / Thin Wall 5): checking it opens its expander (the setters); a Thin
   Wall change posts 0x329: app+0x2e78 = 0 */
void ex_set_check(int which, int on) {
    int *v = which == 3 ? &ex_p.trim : which == 4 ? &ex_p.draft : which == 5 ? &ex_p.thin : NULL;
    if (!v || (*v != 0) == (on != 0)) return;
    *v = on != 0;
    if (on) ex_exp[which] = 1;
    if (which == 5) ex_thin_auto = 0;
}
/* the Vector box typed (FUN_180131720 -> FUN_180131fb0(dlg, 0)): parse_pnt, SubtractActiveOrigin (0 here), every
   chain's direction = that vector, Distance unchanged; unparsable: every chain's direction = its chn_get_normal */
void ex_vec_commit(void) {
    double v[3]; char c1, c2;
    if (sscanf(ex_vec_txt, "%lf %c %lf %c %lf", &v[0], &c1, &v[1], &c2, &v[2]) == 5 && c1 == ',' && c2 == ',') ex_vec_bad = !ex_set_vector(v);
    else { for (int i = 0; i < ncchains; i++) { int n = ex_chain_normal(&cchains[i]); ex_cdir[i] = n ? n : 1; } ex_vec_bad = 0; }
}
/* the Vector box shown (FUN_180132ab0): the first chain's direction in the active view (Top here), each component %ld
   when within tol of an integer, else %.4lf, joined with "," */
void ex_vec_show(void) {
    double v[3] = { 0, 0, ncchains ? ex_cdir[0] : 1 }; int k = 0;
    for (int i = 0; i < 3; i++) {
        double ri = floor(v[i] + 0.5);
        k += fabs(v[i] - ri) <= EX_TOL ? snprintf(ex_vec_txt + k, sizeof ex_vec_txt - k, "%s%ld", i ? "," : "", (long)ri)
                                       : snprintf(ex_vec_txt + k, sizeof ex_vec_txt - k, "%s%.4lf", i ? "," : "", v[i]);
    }
}
/* Chains list items: sld 749 "Chain " + "%s %d" (FUN_1801311f0) - note the double space */
void ex_chain_label(int i, char *out, size_t n) { snprintf(out, n, "%s %d", "Chain ", i + 1); }
/* a click on Chains list item i: Ctrl toggles (extended selection), else only it */
void ex_chain_click(int i, int ctrl, int on) {
    if (i < 0 || i >= 256) return;
    if (!ctrl) memset(ex_chsel, 0, sizeof ex_chsel);
    ex_chsel[i] = (unsigned char)(ctrl ? on : 1);
}

/* ------------------------------------------------------------------------------------------------ Solids manager model */
/* Sources (mastercam.dll, ../decomp/mastercam_histdlg.c and ../decomp/mastercam_solidsmgr.c):
 *  - the manager is the history tree (HistDlg_* -> the UITreeCtrl at +0xaf78): HistDlg_addSolid -> FUN_180232b00 adds a
 *    solid at the root with the text sld_get_description(solid), or opmanres 27600 "Solid" when that is empty, image 0;
 *    HistDlg_addOp -> FUN_1802326e0 adds an operation under its solid: text = the op name (mc_op_getName), when empty
 *    the type name from GetOpInfoFromOpType (opmanres 27601 + type: 0 Extrude Cut, 1 Extrude Boss, 2 Extrude, ...),
 *    image = type * 4 + 8, selected image = type * 4 + 10; dirty toolpath ops 0x8d / 0x8f.
 *  - image list (FUN_18022dbd0): CImageList::Create(opmanres bitmap 749, 16 px, white mask), + bitmap 754, then one
 *    4-cell strip per op type from the table at 0x18087e520 (type 0 730, 1 736, 2 733, 3 745, 4 743, 5 740, ...).
 *  - right-click menu (FUN_180230a10): submenus opmanres 1211 "Solids" and 1190 "Model Prep" with the ribbon's solid
 *    commands (strings 1173 ... 1228, command ids 33495 ... 33569), then the item commands: Delete 27641 (33043),
 *    Suppress 27644 (33046), Highlight 27645 (33047), Rename 27646 (33044) / Delete, Rename, Duplicate Solid 27647
 *    (33053), Attributes 3810 (33057), Check solid... 27655 (33058), Remove solid history 27670 (33474); Regen Solid 3255
 *    (33045), Regen All Solids 3256 (33202), Expand all 27648 (33054), Collapse all 27649 (33055), Auto-Highlight 27650
 *    (33056), Edit Parameters 27643 (33048), Recreate trimmed bodies 27656 (33059). Which of the two item groups goes
 *    with a solid row and which with an op row is read from the item kinds (1 solid, 2 op) - inferred.
 * Here: the bodies made by Extrude features (each feature under the body it created or changed) and the part's
 * Parasolid solids (their history operations are not read from the file: NOT DECODED, so no children; their
 * description field is not read either, so they show "Solid"). Working menu items: Solids > Extrude..., Delete on the
 * last feature, Regen All Solids, Expand all / Collapse all, Edit Parameters on a feature row (loads the op back into
 * the Extrude command, formats/extrude.md 1.7); the others are greyed. (Moved from viewer/solidsmgr_ui.inc.) */
/* op type -> name: GetOpInfoFromOpType (mastercam.dll 0x18022d600) loads the string id of the short table 0x18087e4e0 (read from
   the image: 27601 + type for types 0 .. 29, 27657 "Face-Face Fillet" for 0x1e, 27669 "Pattern" for 0x1f); image = type * 4 + 8,
   selected type * 4 + 10, from the per-type bitmaps of the table 0x18087e520 (FUN_18022dbd0 adds them after bitmaps 749 and 754,
   4 cells each: cell 0 / 2) */
const char *const SLD_OPNAME[32] = { "Extrude Cut", "Extrude Boss", "Extrude", "Revolve Cut", "Revolve Boss", "Revolve", "Sweep Cut",
    "Sweep Boss", "Sweep", "Fillet", "Chamfer", "Boolean Remove", "Boolean Common", "Boolean Add", "Body", "Block", "Cone", "Cylinder",
    "Sphere", "Torus", "Shell", "Loft Cut", "Loft Boss", "Loft", "Toolpath", "Draft Face", "Trim to surface/sheet", "FBM Drill",
    "Lathe Toolpath", "Thicken", "Face-Face Fillet", "Pattern" };                 /* opmanres 27601 .. 27630, 27657, 27669 */
const int SLD_OPBMP[32] = { 730, 736, 733, 745, 743, 740, 730, 736, 733, 738, 719, 713, 713, 713, 711, 709, 722, 724, 750, 757, 747,
    730, 736, 733, 755, 727, 758, 755, 755, 753, 738, 4173 };                     /* 0x18087e520 types 0 .. 31 */
unsigned char sm_body_open[512] = { 0 }; int sm_body_init;
int sm_sel_kind, sm_sel_idx = -1;                 /* selected row: 1 solid (body), 2 op (feature), 3 a part solid */
int sld_optype(const ExFeat *f) { return f->p.type == EX_CUT ? 0 : f->p.type == EX_BOSS ? 1 : 2; }
void sm_init(void) { if (!sm_body_init) { memset(sm_body_open, 1, sizeof sm_body_open); sm_body_init = 1; } }
/* a Block / Cylinder (ExFeat.prim, the Bounding Box panel's Solids): op types 15 / 17 -> opmanres 27616 "Block" / 27618
   "Cylinder", bitmaps 709 / 724 (the table at 0x18087e520, decomp/mastercam_sldop_bmp_table.txt) */
const char *sm_feat_text(int i) { if (feats[i].prim) return feats[i].name[0] ? feats[i].name : feats[i].prim == 15 ? "Block" : "Cylinder";
                                  int t = sld_optype(&feats[i]); return feats[i].name[0] ? feats[i].name : SLD_OPNAME[t]; }
void sm_feat_icon(int i, int sel, char *key, size_t n) { snprintf(key, n, "#strip:opmanres_BMP_%d.png:%d", feats[i].prim ? (feats[i].prim == 15 ? 709 : 724) : SLD_OPBMP[sld_optype(&feats[i])], sel ? 2 : 0); }   /* type*4+8 / +10 */
/* the part's solids (mc_add_solid_to_dialog, mcgeomsld 0x18002af00, decomp/sldtree/sld_adddlg.c): the solid row (HistDlg_addSolid
   -> FUN_180232b00: text sld_get_description = SOLID_ID +0xb8, which sld_read_solids fills from the name after the XT; empty ->
   opmanres 27600 "Solid"), then each op (HistDlg_addOp -> FUN_1802326e0: text mc_op_getName = op +0x20, empty -> the type name;
   image type * 4 + 8; an op that is not editable shows its selected image type * 4 + 10 as both images), the tool solids of an op
   (vfunc 0x130, Boolean ops: none in the user parts, NOT PORTED), then "Stop Op" (HistDlg_addStop -> FUN_180232d00: opmanres 27640,
   image 0x8c = 140 = the single cell of opmanres bitmap 759 in FUN_18022dbd0's list: 749 / 754 / 32 x 4 / 113 / 25011 / 760 x 2 /
   759) after the history position (header +0x14) ops; the solid row expanded by header +0x0e (HistDlg_ExpandSolid).
   Toolpath ops (type 24): sld_get_toolp_name ("N - " + the toolpath op's description, via CAssocUtils::GetOperationsByGeometry and
   the op's vfunc 0x148) is NOT DECODED - the type name is shown (FUN_1802326e0's text when it finds no toolpath op). The dirty /
   invalid overlays (op +9 / +0xa -> overlay images 138 / 139) are NOT SHOWN: their image states (UITreeCtrl state bits) are not
   ported; Mastercam also sets op +9 on every op after a load (FUN_180268520). */
const char *sm_psolid_text(int i) { return i >= 0 && i < F.nsolids && F.solids[i].desc[0] ? F.solids[i].desc : "Solid"; }
static int pop_type_ok(int t) { return t >= 0 && t < 32; }
const char *sm_pop_text(int i, int k) {
    const SolidOp *o = &F.solids[i].ops[k];
    if (o->type != 24 && o->name[0]) return o->name;
    return pop_type_ok(o->type) ? SLD_OPNAME[o->type] : "";     /* FUN_180267440: another type -> sld 278 "Unknown operation." */
}
/* mc_op_isEditable -> FUN_180264080: op id < the history position and op +0x10 == 0, and the solid has a body (SOLID_ID +0x68:
   every loaded solid with an XT) or it is the first op (the rolled-back branch, SOLID_ID +0x70 bit 4, is not read here) */
int sm_pop_editable(int i, int k) {
    const Solid *s = &F.solids[i]; const SolidOp *o = &s->ops[k];
    return o->id < s->pos && o->x10 == 0 && (s->xt_size > 0 || o->id == 0);
}
void sm_pop_icon(int i, int k, int sel, char *key, size_t n) {
    int t = F.solids[i].ops[k].type;
    snprintf(key, n, "#strip:opmanres_BMP_%d.png:%d", pop_type_ok(t) ? SLD_OPBMP[t] : 749, sel || !sm_pop_editable(i, k) ? 2 : 0);
}
const char *SM_STOP_TEXT = "Stop Op", *SM_STOP_ICON = "#strip:opmanres_BMP_759.png:0";
unsigned char sm_psolid_open[512];
void sm_psolids_load(void) {   /* after a part is opened: each solid row's expanded state from its ops header (+0x0e) */
    for (int i = 0; i < F.nsolids && i < 512; i++) sm_psolid_open[i] = F.solids[i].hist_ok ? F.solids[i].expanded != 0 : 0;
}

/* the menu's submenus: 1211 Solids (FUN_180230a10 order) and 1190 Model Prep */
const SmItem SM_SOLIDS[] = {
    { "Cylinder...", 0, 0 }, { "Block...", 0, 0 }, { "Sphere...", 0, 0 }, { "Cone...", 0, 0 }, { "Torus...", 0, 1 },
    { "Extrude...", SM_EXTRUDE, 0 }, { "Revolve...", 0, 0 }, { "Loft...", 0, 0 }, { "Sweep...", 0, 1 },
    { "Boolean...", 0, 0 }, { "Impression...", 0, 0 }, { "Rectangular Pattern...", 0, 0 }, { "Circular Pattern...", 0, 0 },
    { "Manual Pattern...", 0, 0 }, { "From Surfaces...", 0, 1 }, { "Constant Fillet...", 0, 0 }, { "Face to Face Fillet...", 0, 0 },
    { "Variable Fillet...", 0, 0 }, { "One Distance Chamfer...", 0, 0 }, { "Two Distance Chamfer...", 0, 0 },
    { "Distance and Angle Chamfer...", 0, 1 }, { "Shell...", 0, 0 }, { "Thicken...", 0, 0 }, { "Draft Faces...", 0, 0 },
    { "Draft Edge...", 0, 0 }, { "Draft Extrude...", 0, 0 }, { "Draft Plane...", 0, 0 }, { "Trim by Plane...", 0, 0 },
    { "Trim to Surface/Sheet...", 0, 1 }, { "Layout...", 0, 0 } };
const SmItem SM_PREP[] = {
    { "Hole-Axis...", 0, 0 }, { "Push-Pull...", 0, 0 }, { "Move...", 0, 0 }, { "Split Solid Faces...", 0, 1 }, { "Modify Feature...", 0, 0 },
    { "Modify Fillet...", 0, 0 }, { "Remove Faces...", 0, 0 }, { "Remove Fillets...", 0, 0 }, { "Remove History...", 0, 1 },
    { "Simplify Solid...", 0, 0 }, { "Optimize...", 0, 0 }, { "Repair Small Faces...", 0, 0 }, { "Check Solid...", 0, 0 },
    { "Find Features...", 0, 1 }, { "Disassemble...", 0, 0 }, { "Position...", 0, 1 }, { "Clear All", 0, 0 }, { "Change Face...", 0, 0 },
    { "Clear Feature", 0, 0 }, { "Set Feature...", 0, 0 } };
const int SM_NSOLIDS = sizeof SM_SOLIDS / sizeof SM_SOLIDS[0], SM_NPREP = sizeof SM_PREP / sizeof SM_PREP[0];
/* the menu for a right click on a row of kind row_kind (0 background, 1 solid, 2 op = feature row_feat) */
int sm_menu(int row_kind, int row_feat, SmItem *it, int cap) {
    int n = 0;
#define ADD(s, a, sp) do { if (n < cap) { it[n].t = s; it[n].act = a; it[n].sep = sp; n++; } } while (0)
    ADD("Solids", SM_SUB_SOLIDS, 0); ADD("Model Prep", SM_SUB_PREP, 0);
    if (row_kind == 2) {
        ADD("Delete", row_feat == nfeats - 1 ? SM_DELETE : SM_NONE, 0); ADD("Suppress", SM_NONE, 0); ADD("Highlight", SM_NONE, 0); ADD("Rename", SM_NONE, 1);
    } else if (row_kind == 4) {                    /* an op of the part's solid: not ported */
        ADD("Delete", SM_NONE, 0); ADD("Suppress", SM_NONE, 0); ADD("Highlight", SM_NONE, 0); ADD("Rename", SM_NONE, 1);
    } else if (row_kind == 1 || row_kind == 3) {
        ADD("Delete", SM_NONE, 0); ADD("Rename", SM_NONE, 0); ADD("Duplicate Solid", SM_NONE, 0); ADD("Attributes", SM_NONE, 0);
        ADD("Check solid...", SM_NONE, 0); ADD("Remove solid history", SM_NONE, 1);
    }
    ADD("Regen Solid", SM_NONE, 0); ADD("Regen All Solids", nfeats ? SM_REGEN_ALL : SM_NONE, 0); ADD("Expand all", SM_EXPAND, 0);
    ADD("Collapse all", SM_COLLAPSE, 0); ADD("Auto-Highlight", SM_NONE, 0);
    if (row_kind == 4) ADD("Edit Parameters", SM_NONE, 0);
    if (row_kind == 2) ADD("Edit Parameters", row_feat >= 0 && row_feat < nfeats && feats[row_feat].prim ? SM_NONE : SM_EDITP, 0);   /* a Block / Cylinder: its function panel is NOT PORTED */
    ADD("Recreate trimmed bodies", SM_NONE, 0);
#undef ADD
    return n;
}
void feat_delete_last(void);
void sm_menu_act(int act, int row_feat) {
    sm_init();                                     /* the open flags exist before the first tree draw (scripted runs) */
    if (act == SM_EXTRUDE) ex_start();
    else if (act == SM_EDITP) { if (row_feat >= 0 && row_feat < nfeats && !feats[row_feat].prim) ex_edit(row_feat); }           /* Edit Parameters (33048): the op loaded into the command (sec. 1.7) */
    else if (act == SM_DELETE) feat_delete_last();
    else if (act == SM_REGEN_ALL) solids_rebuild();
    else if (act == SM_EXPAND) { memset(sm_body_open, 1, sizeof sm_body_open); memset(sm_psolid_open, 1, sizeof sm_psolid_open); }
    else if (act == SM_COLLAPSE) { memset(sm_body_open, 0, sizeof sm_body_open); memset(sm_psolid_open, 0, sizeof sm_psolid_open); }
}

/* ------------------------------------------------------------------------------------------------ tests */
/* MCAMUI_EXDUMP=1 (both apps, ours): the command's state, the features and the bodies after every step */
void ex_dump(void) {
    printf("    ex mode %d phase %d panel %d edit %d pick %d drag %d tab %d name '%s' type %d target %d method %d dist %.4f both %d rev %d trim %d draft %d/%.4f/%d split %d ends %d thin %d/%d %.4f %.4f auto %d single %d prev %d vec '%s' bad %d\n",
           ex_mode, ex_phase, ex_panel, ex_edit_idx, ex_pick, ex_drag, ex_tab, ex_name, ex_p.type, ex_p.target, ex_p.method, ex_p.dist, ex_p.both, ex_p.reverse, ex_p.trim,
           ex_p.draft, ex_p.draft_ang, ex_p.draft_rev, ex_p.split, ex_p.draft_ends, ex_p.thin, ex_p.thin_dir, ex_p.thick, ex_p.thick2, ex_thin_auto, ex_single, ex_prev, ex_vec_txt, ex_vec_bad);
    for (int i = 0; ex_mode && i < ncchains; i++) printf("    exchain %d: %d ents first %d %s cdir %d sel %d\n", i, cchains[i].n, cchains[i].n ? cchains[i].ent[0] : -1, cchains[i].closed ? "closed" : "open", ex_cdir[i], ex_chsel[i]);
    for (int i = 0; i < F.nsolids; i++) {            /* the part's solids as the Solids manager lists them */
        const Solid *so = &F.solids[i];
        printf("    psolid %d '%s' sid %u hist %d ops %d pos %d open %d regen %d raw %zu\n", i, sm_psolid_text(i), so->sid, so->hist_ok, so->nops, so->pos, i < 512 ? sm_psolid_open[i] : -1, so->regen, so->ops_n);
        for (int k = 0; so->hist_ok && k <= so->nops; k++) {
            if (k == so->pos) printf("      %s\n", SM_STOP_TEXT);
            if (k == so->nops) break;
            char key[80]; sm_pop_icon(i, k, 0, key, sizeof key);
            printf("      op %d '%s' type %d id %d flags %d %d %d %d x10 %u editable %d icon %s\n", k, sm_pop_text(i, k), so->ops[k].type, so->ops[k].id,
                   so->ops[k].f9, so->ops[k].f10, so->ops[k].f11, so->ops[k].f12, so->ops[k].x10, sm_pop_editable(i, k), key);
        }
    }
    for (int i = 0; i < nfeats; i++) {
        const ExFeat *f = &feats[i];
        if (f->prim) { printf("    feat %d '%s' body %d prim %d %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n", i, f->name, i < 4096 ? feat_body[i] : -1, f->prim,
                              f->pv[0], f->pv[1], f->pv[2], f->pv[3], f->pv[4], f->pv[5], f->pv[6], f->pv[7]); continue; }
        printf("    feat %d '%s' body %d type %d method %d dist %.4f both %d rev %d draft %d/%.4f thin %d/%d %.4f %.4f single %d chains", i, f->name, i < 4096 ? feat_body[i] : -1, f->p.type,
               f->p.method, f->p.dist, f->p.both, f->p.reverse, f->p.draft, f->p.draft_ang, f->p.thin, f->p.thin_dir, f->p.thick, f->p.thick2, f->single);
        for (int k = 0; k < f->nch; k++) printf(" %d:%d%s", f->ch[k].n ? f->ch[k].ent[0] : -1, f->cdir[k], f->ch[k].closed ? "c" : "o");
        printf("\n");
    }
    for (int i = 0; i < nsbodies; i++) printf("    xbody %d %s: %d slab(s), volume %.4f\n", i, sbodies[i].name, sbodies[i].n, body_volume(&sbodies[i]));
}
static void ex_print_bodies(const char *ind) {
    for (int i = 0; i < nsbodies; i++) printf("%sbody %d %s: %d slab(s), volume %.3f\n", ind, i, sbodies[i].name, sbodies[i].n, body_volume(&sbodies[i]));
}
/* the show: steps of mcamui.c run_script: show:extrude, show:exedit:N (Edit Parameters of feature N), show:exchain:E
   (a chain from entity E during the Extrude chaining), show:exchainok (the chaining's OK) */
int ex_show_step(const char *arg) {
    if (!arg) return 0;
    if (!strcmp(arg, "extrude")) ex_start();
    else if (!strncmp(arg, "exedit:", 7)) ex_edit(atoi(arg + 7));   /* exedit:N - Edit Parameters of feature N */
    else if (!strncmp(arg, "exchain:", 8)) { int en = atoi(arg + 8); if (ex_mode && !ex_phase && en >= 0 && en < F.nents && ncchains < 256 && cam_chain(&F, en, cfg_chain_tol, &cchains[ncchains])) { ncchains++; overlay_dirty = 1; } }
    else if (!strcmp(arg, "exchainok")) { if (ex_mode && !ex_phase && ncchains) ex_chains_done(); }
    else return 0;
    return 1;
}
/* the MCAMUI_SCRIPT Extrude / Solids steps (mcamui.c run_script; same output in both apps):
   extrude:type,dist,both,reverse,draft,thin,thick,target|seed,seed... (chains, panel, OK), exset:KEY=VALUE (a panel
   control), and ours: exset:dragtow=X,Y,Z (drag to a world point), smmenu:KIND,FEAT (the Solids manager menu),
   smact:LABEL[,FEAT] (one of its items by label; the Solids submenu's too), smrows (the tree), exdump */
int cui_extrude_script(const char *st, const char *arg) {
    const char *mt, *mx;
    if (!strcmp(st, "extrude") && arg) {             /* extrude:type,dist,both,reverse,draft,thin,thick,target|seed,seed... (chains, panel, OK) */
        char a3[512]; snprintf(a3, sizeof a3, "%s", arg); char *bar = strchr(a3, '|'); if (bar) *bar++ = 0;
        ex_start(); double v[8] = { 0, 10, 0, 0, 0, 0, 1, 0 }; int nv = 0;
        for (char *q = a3; *q && nv < 8; ) { v[nv++] = strtod(q, &q); if (*q == ',') q++; else break; }
        for (char *q = bar; q && *q; ) { int en = (int)strtol(q, &q, 10);
            if (en >= 0 && en < F.nents && ncchains < 256 && cam_chain(&F, en, cfg_chain_tol, &cchains[ncchains])) ncchains++;
            if (*q == ',') q++; else break; }
        if (ncchains) ex_chains_done();
        if (ex_mode && ex_phase) {
            ex_p.dist = v[1]; ex_p.both = v[2] != 0; if ((int)v[2] == 2) ex_p.dist /= 2;   /* 2: the old symmetric mode */
            ex_p.reverse = (int)v[3]; ex_p.draft = v[4] > 0; if (v[4] > 0) ex_p.draft_ang = v[4];
            if (ex_p.thin != (int)v[5]) ex_thin_auto = 0;
            ex_p.thin = (int)v[5]; ex_p.thick = ex_p.thick2 = v[6];
            if ((int)v[0] != EX_CREATE) ex_set_target((int)v[7]);
            ex_set_type((int)v[0]); ex_force();
            ex_accept(0);
        } else ex_end(0, 0);
        if (cui_hooks.msg_open && cui_hooks.msg_open(&mt, &mx)) { printf("    (extrude: %s%s%s)\n", mt, mt[0] ? ": " : "", mx); if (cui_hooks.msg_close) cui_hooks.msg_close(); }
        ex_print_bodies("    ");
        if (ex_mode) ex_end(0, 0);
        return 1;
    }
    if (!strcmp(st, "exset") && arg) {               /* exset:KEY=VALUE - an Extrude panel control (tests / screenshots) */
        char k[32] = ""; const char *eq = strchr(arg, '='); double x = eq ? atof(eq + 1) : 0;
        snprintf(k, sizeof k, "%.*s", eq ? (int)(eq - arg) : (int)strlen(arg), arg);
        if (!strcmp(k, "type")) ex_set_type((int)x); else if (!strcmp(k, "target")) ex_set_target((int)x);
        else if (!strcmp(k, "dist")) ex_p.dist = x; else if (!strcmp(k, "method")) ex_p.method = (int)x;
        else if (!strcmp(k, "both")) ex_p.both = (int)x; else if (!strcmp(k, "reverse")) ex_p.reverse = (int)x;
        else if (!strcmp(k, "draft")) { ex_p.draft = (int)x; if (x) ex_exp[4] = 1; } else if (!strcmp(k, "angle")) ex_p.draft_ang = x;
        else if (!strcmp(k, "draftrev")) ex_p.draft_rev = (int)x; else if (!strcmp(k, "split")) ex_p.split = (int)x;
        else if (!strcmp(k, "draftends")) ex_p.draft_ends = (int)x;
        else if (!strcmp(k, "thin")) { ex_p.thin = (int)x; ex_thin_auto = 0; if (x) ex_exp[5] = 1; }
        else if (!strcmp(k, "thindir")) ex_p.thin_dir = (int)x; else if (!strcmp(k, "t1")) ex_p.thick = x; else if (!strcmp(k, "t2")) ex_p.thick2 = x;
        else if (!strcmp(k, "single")) ex_single = (int)x; else if (!strcmp(k, "preview")) ex_prev = (int)x;
        else if (!strcmp(k, "name") && eq) snprintf(ex_name, sizeof ex_name, "%s", eq + 1);
        else if (!strcmp(k, "chsel") && x >= 0 && x < 256) ex_chsel[(int)x] = 1;
        else if (!strcmp(k, "cdir") && x >= 0 && x < ncchains) ex_cdir[(int)x] = -ex_cdir[(int)x];   /* reverse that chain */
        else if (!strcmp(k, "tab")) ex_tab = (int)x; else if (!strcmp(k, "expand") && x >= 0 && x < 8) ex_exp[(int)x] = 1;
        else if (!strcmp(k, "ok")) ex_accept(0); else if (!strcmp(k, "apply")) ex_accept(1); else if (!strcmp(k, "cancel")) ex_cancel();
        else if (!strcmp(k, "remove")) ex_remove_sel(); else if (!strcmp(k, "add")) ex_add_chains(0); else if (!strcmp(k, "rechain")) ex_add_chains(1);
        else if (!strcmp(k, "pick")) ex_pick = (int)x;    /* 1 AutoCursor, 2 target body, 3 vector (the next MCAMUI_EVENTS click) */
        else if (!strcmp(k, "drag")) {                /* drag:K - press the arrow of kind K (1 distance, 2 / 4 thickness) */
            ExArrow ar[258]; int na = ex_arrow_list(ar, 258);
            for (int i = 0; i < na; i++) if (ar[i].kind == (int)x) { ex_drag_start(ar[i].kind, ar[i].a, ar[i].d); break; }
        }
        else if (!strcmp(k, "dragto") && eq) {        /* dragto:X,Y - drag to the window point (still held) */
            float sx = 0, sy = 0; sscanf(eq + 1, "%f,%f", &sx, &sy);
            if (ex_drag) { if (cui_hooks.view_port) cui_hooks.view_port(); ex_drag_move(sx, sy); }
        }
        else if (!strcmp(k, "dragtow") && eq) {       /* ours: dragtow=X,Y,Z - drag to the screen point of a world point (both apps' views) */
            double wx = 0, wy = 0, wz = 0; sscanf(eq + 1, "%lf,%lf,%lf", &wx, &wy, &wz); float sx, sy;
            if (ex_drag) { if (cui_hooks.view_port) cui_hooks.view_port();
                if (scene_to_screen(vp_w, vp_h, wx, wy, wz, &sx, &sy)) ex_drag_move(sx + vp_x, sy + vp_y); }
        }
        else if (!strcmp(k, "dragup")) { if (ex_drag) ex_drag_end(0); }      /* release = commit */
        else if (!strcmp(k, "dragesc")) { if (ex_drag) ex_drag_end(1); }     /* Esc = cancel, restore */
        int mo = cui_hooks.msg_open && cui_hooks.msg_open(&mt, &mx);
        printf("    exset %s: mode %d phase %d chains %d type %d target %d name '%s' thin %d (auto %d) dir %d t1 %g t2 %g dist %g%s%s\n", k, ex_mode, ex_phase, ncchains, ex_p.type, ex_p.target, ex_name, ex_p.thin, ex_thin_auto, ex_p.thin_dir, ex_p.thick, ex_p.thick2, ex_p.dist,
               mo ? " | message: " : "", mo ? mx : "");
        ex_print_bodies("      ");
        for (int i = 0; i < nfeats; i++) printf("      op %d '%s' type %d chains %d\n", i, feats[i].name, feats[i].p.type, feats[i].nch);
        return 1;
    }
    if (!strcmp(st, "smmenu")) {                     /* ours: smmenu:KIND,FEAT - the Solids manager menu of a row */
        int kind = 0, fi = -1; if (arg) sscanf(arg, "%d,%d", &kind, &fi);
        SmItem it[24]; int n = sm_menu(kind, fi, it, 24);
        for (int i = 0; i < n; i++) printf("    sm %s%s%s\n", it[i].t, it[i].act < 0 ? " >" : "", it[i].act ? "" : " (greyed)");
        return 1;
    }
    if (!strcmp(st, "smact") && arg) {               /* ours: smact:LABEL[,FEAT] - a Solids manager menu item by label */
        char lab[64]; snprintf(lab, sizeof lab, "%s", arg); int fi = -1; char *c = strrchr(lab, ',');
        if (c) { fi = atoi(c + 1); *c = 0; }
        SmItem it[24]; int n = sm_menu(fi >= 0 ? 2 : 0, fi, it, 24), act = -99;
        for (int i = 0; i < n; i++) if (!strcmp(it[i].t, lab)) act = it[i].act;
        for (int i = 0; i < SM_NSOLIDS; i++) if (!strcmp(SM_SOLIDS[i].t, lab)) act = SM_SOLIDS[i].act;
        if (act > 0) sm_menu_act(act, fi); else printf("    (solids menu item %s %s)\n", lab, act == -99 ? "not found" : "greyed");
        return 1;
    }
    if (!strcmp(st, "smrows")) {                     /* ours: the Solids manager rows */
        sm_init();
        for (int i = 0; i < F.nsolids; i++) printf("    row solid (part) 'Solid'\n");
        for (int b = 0; b < nsbodies; b++) {
            printf("    row solid %d 'Solid' %s\n", b, b < 512 && !sm_body_open[b] ? "closed" : "open");
            for (int i = 0; i < nfeats && i < 4096; i++) if (feat_body[i] == b) { char key[64]; sm_feat_icon(i, sm_sel_kind == 2 && sm_sel_idx == i, key, sizeof key); printf("      row op %d '%s' %s\n", i, sm_feat_text(i), key); }
        }
        return 1;
    }
    if (!strcmp(st, "exdump")) { ex_dump(); return 1; }
    return 0;
}
