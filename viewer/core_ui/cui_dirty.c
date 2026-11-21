/* cui_dirty.c - dirty operations (Mastercam's op +0x94, saved as record +0xfc; formats/opdlg_port_audit.md "Dirty operations",
   formats/mcam.md "Dirty operations"): which ops are dirty, the edit waiting for regeneration, the Toolpaths manager's
   Select all dirty / Regenerate all selected / Regenerate all dirty (toolbar 800), dialog 977 "Warning: Dirty Operations
   Selected" before Post / Backplot / Verify, and the project lines. Decompiles: decomp/opman_tree_all.c (COpMgrTree::
   RegenSelected, DirtyOperationWarning, InitTvItem), decomp/dirty/ (OMregenSelectedOperations, OMregenDirtyOperations,
   OMselectDirtyOperations, COpMgrToolBar::OnUpdateOMToolbarButtons, CDirtyOpsDlg, mccore bnci_regenerate / BnciUpdate),
   decomp/mccore_edit_operation.c (edit_operation's dirty mark).
   Model: an op made here keeps its dirty state in its CamOp (CamOp.dirty / CamOp.edit: camops_detach / attach rebuild the
   F.ops entries); a stored Mastercam op in Op.regen (op +0x94; Op.stale = the older "tool changed" mark, dirty too) and its
   changed parameters in the table below (by op id), while F.ops[i] keeps Mastercam's toolpath and NCI. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include "platform.h"
#include "cam_fileop.h"
#include "core_ui.h"
#include "mcce_dyn.h"   /* mcce_thread_release (the regeneration workers) */

int dop_pending, dop_choice;                    /* dialog 977 open for DOP_*; its radio: 0 Regenerate (25350), 1 Do not regenerate (25351) */
static int dop_bypass;                          /* the answered command runs once without the check */
static unsigned char dop_post_skip[1024]; static int dop_post_nskip;   /* "Do not regenerate": the ops left out of the post */

/* the changed parameters of stored ops (op id -> the op as changed, no moves): owned here */
#define SED_MAX 1024
static struct { int id; CamOp *op; } sed[SED_MAX]; static int nsed;

static int camop_of(int i) { for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) return j; return -1; }
static int sed_find(int id) { for (int k = 0; k < nsed; k++) if (sed[k].id == id) return k; return -1; }
static CamOp *sed_take(int id) {                 /* removes the entry; the caller owns the op */
    int k = sed_find(id); if (k < 0) return NULL;
    CamOp *o = sed[k].op; sed[k] = sed[--nsed]; return o;
}

int op_dirty(int i) {
    if (!loaded || i < 0 || i >= F.nops) return 0;
    int k = camop_of(i);
    if (k >= 0) return camops[k]->dirty != 0;
    return F.ops[i].regen || F.ops[i].stale;
}
CamOp *op_dirty_edit(int i) {
    if (!loaded || i < 0 || i >= F.nops) return NULL;
    int k = camop_of(i);
    if (k >= 0) return camops[k]->edit;
    int s = sed_find(F.ops[i].id); return s >= 0 ? sed[s].op : NULL;
}
/* edit_operation's changed case (decomp/mccore_edit_operation.c l.2582-2588): op +0x94 = 1 (not for opcode 0x11 with +0x14b2,
   none made here), +0x97 = 0; the op keeps its NCI. EDIT (taken) = the op with the dialog's parameters */
void op_mark_dirty_edit(int i, CamOp *edit) {
    if (!loaded || i < 0 || i >= F.nops) { cam_op_free(edit); return; }
    if (edit) { free(edit->m); edit->m = NULL; edit->nm = 0; edit->dirty = 0; cam_op_free(edit->edit); edit->edit = NULL; }
    int k = camop_of(i);
    if (k >= 0) {
        CamOp *o = camops[k];
        if (edit) { cam_op_free(o->edit); o->edit = edit; }
        o->dirty |= CAM_DIRTY_PARAMS;
        return;
    }
    if (edit) {
        int s = sed_find(F.ops[i].id);
        if (s >= 0) { cam_op_free(sed[s].op); sed[s].op = edit; }
        else if (nsed < SED_MAX) { sed[nsed].id = F.ops[i].id; sed[nsed].op = edit; nsed++; }
        else { cam_op_free(edit); snprintf(cam_msg, sizeof cam_msg, "too many changed operations (%d)", SED_MAX); return; }   /* ours */
    }
    F.ops[i].regen = 1;
}
/* validate_toolpath (mccore, formats/align_geo.md 1.3): every op whose chain / points used entity ENT gets op +0x94 = 1 (dirty) and
   keeps its toolpath; ours: the op's CamOp dirty flag, stored ops F.ops[].regen (written as record +0xfc = 1). Its dependents
   (CDependentOperations::Detect): none here. */
void dirty_ent_users(int ent) {
    for (int j = 0; j < ncamops; j++) {
        const CamOp *o = camops[j]; int hit = 0;
        for (int c = 0; c < o->nchains && !hit; c++) for (int k = 0; k < o->chains[c].n; k++) if (o->chains[c].ent[k] == ent) { hit = 1; break; }
        if (hit) camops[j]->dirty |= CAM_DIRTY_PARAMS;
    }
    if (ent >= 0 && ent < F.nents && F.ents[ent].idn > 0)
        for (int k = 0; k < F.ntp; k++) if (F.tp[k].ent == F.ents[ent].idn)
            for (int q = 0; q < F.nops; q++) if (F.ops[q].id == F.tp[k].op_id && camop_of(q) < 0) F.ops[q].regen = 1;
}
void dirty_clear_doc(void) {
    op_buffers_reset();                                  /* the Toolpaths manager delete / cut / copy buffers (cui_ops.c) */
    for (int k = 0; k < nsed; k++) cam_op_free(sed[k].op);
    nsed = 0; dop_pending = 0; dop_bypass = 0; dop_post_nskip = 0;
}
/* a stored op left the list (taken over / removed): its edit goes with it */
void dirty_forget_stored(int id) { cam_op_free(sed_take(id)); }

/* ---- regeneration of one op (bnci_regenerate's per-op step; here cam_generate + the commit the dialog's OK does). Ours: the
   op's edit (else the op itself) is computed and replaces it (gen_commit: undo keeps the old one); a tool change refreshes
   the tool's shape first (as regen_ops_for_tool did). Stored: its edit (else cam_op_from_file) is computed and takes the
   stored op's place (the takeover gen_commit does for an op edited from Mastercam's). 1 = done. */
static void tool_refresh(CamParams *p) {
    const Tool *t = mcam_tool(&F, p->tool_num); if (!t) return;
    p->tool_dia = t->dia; p->tool_type = t->type; p->corner_r = t->corner_r; p->tip_angle = t->tip_angle;
}
/* 1 = the op's chains / points use an entity deleted here (validate_toolpath killed those TP records) */
static int uses_deleted(int i) {
    int k = camop_of(i);
    if (k >= 0) {
        const CamOp *o = camops[k]->edit ? camops[k]->edit : camops[k];
        for (int c = 0; c < o->nchains; c++) for (int q = 0; q < o->chains[c].n; q++) { int e = o->chains[c].ent[q]; if (e >= 0 && e < F.nents && !F.ents[e].type) return 1; }
        return 0;
    }
    for (int t = 0; t < F.ntp; t++) if (F.tp[t].op_id == F.ops[i].id && F.tp[t].ent > 0) {
        if (tp_ref_subst(&F, &F.tp[t]) >= 0) continue;  /* re-pointed by tp_substitute_ent (Untrim / Remove Boundary, cui_srfedit.c): it names a live surface */
        int live = 0, gone = 0;                      /* a Move's result has the same id (mod_pair): live */
        for (int e = 0; e < F.nents; e++) if (F.ents[e].idn == F.tp[t].ent) { if (F.ents[e].type) live = 1; else if (!F.ents[e].created) gone = 1; }
        if (gone && !live) return 1;
    }
    return 0;
}
/* one op's regeneration in three steps (regen_mask): prepare (the work op, as before), generate (cam_generate only: it reads the part and
   its own op, so the Dynamic-engine ops run on worker threads - ours, pure speed-up: the results are the same as one by one, nothing is
   committed while they run), finish (commit / restore, in the original order, the op found again by its id) */
typedef struct { int id, inplace, from_edit, ok, failed, k; CamOp *o; CamOp work; char err[300]; } RgJob;
static int regen_prep(int i, RgJob *j) {
    memset(j, 0, sizeof *j); j->id = F.ops[i].id; j->k = -1;
    /* what Mastercam's regeneration does with a chain that lost entities (deleted geometry) is NOT DECODED (formats/align_geo.md 1.3):
       refused, the op stays dirty with its toolpath */
    if (uses_deleted(i)) { snprintf(j->err, sizeof j->err, "operation %d: its chain uses deleted geometry (regenerating it is NOT DECODED)", i + 1); j->failed = 1; return 0; }
    int k = camop_of(i);
    if (k >= 0 && !camops[k]->edit) {               /* ours, parameters unchanged: in place */
        CamOp *o = camops[k];
        if (o->dirty & CAM_DIRTY_TOOL) tool_refresh(&o->p);
        free(o->m); o->m = NULL; o->nm = 0;           /* camop_regen */
        j->inplace = 1; j->o = o; return 1;
    }
    if (k >= 0) { CamOp *e = camops[k]->edit; j->work = *e; free(e); camops[k]->edit = NULL; j->from_edit = 1;
                  if (camops[k]->dirty & CAM_DIRTY_TOOL) tool_refresh(&j->work.p); }
    else {
        CamOp *e = sed_take(j->id);
        if (e) { j->work = *e; free(e); j->from_edit = 1; }
        else if (!cam_op_from_file(&F, i, &j->work, j->err, sizeof j->err)) { j->failed = 1; return 0; }   /* not regenerable here: stays dirty, Mastercam's NCI kept */
    }
    free(j->work.m); j->work.m = NULL; j->work.nm = 0;
    return 1;
}
int cui_prev_ops(const McamFile *f, const CamOp *op, CamPrevOp **out, int *n);
static void regen_gen(RgJob *j) { cam_set_prev_ops_fn(cui_prev_ops);
    if (!j->failed) j->ok = cam_generate(&F, j->inplace ? j->o : &j->work); }
static int regen_par(const RgJob *j) {               /* the Dynamic engine's ops (mcce_dyn.c / cam2d_dyn.c: thread-local state) */
    const CamParams *p = j->inplace ? &j->o->p : &j->work.p;
    return !j->failed && (p->type == CAM_DYNAMIC || p->type == CAM_DYNCONTOUR || p->type == CAM_PEEL || (p->type == CAM_AREA && !(p->am_ok && p->am_mode == 1)));
}
static int regen_finish(RgJob *j, char *err, size_t errn) {
    if (j->failed) { snprintf(err, errn, "%s", j->err); return 0; }
    if (j->inplace) {                                 /* camop_regen's tail */
        CamOp *o = j->o; int live = 0; for (int q = 0; q < ncamops; q++) live |= camops[q] == o;
        if (!live) return 1;                          /* a Tab Cutoff op deleted by its parent's regeneration in this batch (cutoff_children) */
        if (!j->ok) { snprintf(cam_msg, sizeof cam_msg, "%s: %s", cam_op_label(o), o->error); snprintf(err, errn, "%s", cam_msg); return 0; }
        if (o->warning[0]) snprintf(cam_msg, sizeof cam_msg, "%s: warning: %s", cam_op_label(o), o->warning);   /* dec1 (ours) */
        camops_detach(); camops_attach(); doc_dirty = 1; scene_reupload();
        o->dirty = 0; cutoff_children(o); return 1;
    }
    int i = -1; for (int q = 0; q < F.nops; q++) if (F.ops[q].id == j->id) i = q;   /* by identity: the commits before it move indices */
    if (i < 0 && !j->inplace) {                          /* a stored Tab Cutoff op its parent's regeneration deleted (cutoff_children) */
        for (int c = 0; c < j->work.nchains; c++) cam_chain_free(&j->work.chains[c]); free(j->work.chains); free(j->work.holes); free(j->work.m);
        return 1; }
    int k = i >= 0 ? camop_of(i) : -1;
    if (!j->ok) {
        snprintf(err, errn, "%s: %s", j->work.name, j->work.error);
        free(j->work.m); j->work.m = NULL; j->work.nm = 0;
        if (j->from_edit) {                           /* back where it was: the op stays dirty with its edit */
            CamOp *e = malloc(sizeof *e); *e = j->work;
            if (k >= 0) camops[k]->edit = e;
            else if (nsed < SED_MAX) { sed[nsed].id = j->id; sed[nsed].op = e; nsed++; } else cam_op_free(e);
        } else { for (int c = 0; c < j->work.nchains; c++) cam_chain_free(&j->work.chains[c]); free(j->work.chains); free(j->work.holes); }
        return 0;
    }
    pending_free(); pending = j->work; edit_camop = k;
    char warn[sizeof pending.warning + 70] = ""; if (pending.warning[0]) snprintf(warn, sizeof warn, "%s: warning: %s", cam_op_label(&pending), pending.warning);
    gen_commit();                                   /* replaces camops[k] (undo) / takes the stored op over; clears pending */
    if (warn[0]) snprintf(cam_msg, sizeof cam_msg, "%s", warn);
    return 1;
}
static RgJob *rg_jobs; static int rg_n; static atomic_int rg_next;
static void *rg_worker(void *u) {
    (void)u;
    for (;;) { int a = atomic_fetch_add(&rg_next, 1); if (a >= rg_n) break; if (regen_par(&rg_jobs[a])) regen_gen(&rg_jobs[a]); }
    mcce_thread_release();                            /* the worker's engine caches (mcce_dyn.c thread-local) */
    return NULL;
}
/* the threads for a batch: MCAM_REGEN_THREADS (ours; 1 = one by one as before), default the processor count, at most 16 */
static int regen_threads(int njobs) {
    const char *e = getenv("MCAM_REGEN_THREADS"); long n = e && *e ? atol(e) : sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1; if (n > 16) n = 16; if (n > njobs) n = njobs; return (int)n;
}

/* COpMgrTree::RegenSelected (decomp/opman_tree_all.c l.22954): the selected ops (locked ones +0xe0 refused with opmanres 3814 -
   no lock here) -> bnci_regenerate(0, 1, this[0x75b4], ...): this+0x75b4 is 0 (FUN_180343d50 sets the Display options block's
   +0x74 = 0; no dialog control or registry value sets it), so bnci_regenerate marks every selected op dirty first (+0x94 = 1)
   and regenerates them all - dirty or not (decomp/dirty/core_regen.c bnci_regenerate l.704-724, the loop's test l.829-834). The
   selection stays. Returns the count regenerated; cam_msg = the first failure (ours: Mastercam's per-op error box is not traced). */
static int regen_mask(const unsigned char *mask) {
    /* by identity: an op taken over keeps its index (takeover_index), ours keep theirs (readd_camops) */
    CamOp *mine[MAX_CAMOPS]; int ids[1024], nm = 0, ni = 0;
    for (int i = 0; i < F.nops && i < 1024; i++) if (mask[i]) { int k = camop_of(i); if (k >= 0) mine[nm++] = camops[k]; else ids[ni++] = F.ops[i].id; }
    RgJob *jobs = calloc((size_t)(nm + ni) + 1, sizeof *jobs); int nj = 0;
    for (int a = 0; a < nm; a++) { int i = mine[a]->op_index; if (i < 0) continue; regen_prep(i, &jobs[nj++]); }
    for (int a = 0; a < ni; a++) { int i = -1; for (int j = 0; j < F.nops; j++) if (F.ops[j].id == ids[a]) i = j;
                                   if (i < 0) continue; regen_prep(i, &jobs[nj++]); }
    for (int a = 0; a < nj; a++) if (!jobs[a].failed) pp_engrave_ask(jobs[a].inplace ? jobs[a].o : &jobs[a].work);   /* Engrave 1808 before the
                                                     computation, on this thread (E-DW1) */
    int npar = 0; for (int a = 0; a < nj; a++) npar += regen_par(&jobs[a]);
    int nt = regen_threads(npar);
    if (nt > 1) {                                     /* the Dynamic ops on nt workers while this thread does the others */
        pthread_t th[16]; int started = 0;
        rg_jobs = jobs; rg_n = nj; atomic_store(&rg_next, 0);
        for (int t = 0; t < nt; t++) if (!pthread_create(&th[started], NULL, rg_worker, NULL)) started++;
        for (int a = 0; a < nj; a++) if (!regen_par(&jobs[a]) || !started) regen_gen(&jobs[a]);
        for (int t = 0; t < started; t++) pthread_join(th[t], NULL);
        rg_jobs = NULL; rg_n = 0;
    } else for (int a = 0; a < nj; a++) regen_gen(&jobs[a]);
    int n = 0; char first[300] = "", err[300];
    for (int a = 0; a < nj; a++) { err[0] = 0; if (regen_finish(&jobs[a], err, sizeof err)) n++; else if (!first[0]) snprintf(first, sizeof first, "%s", err); }
    free(jobs);
    if (n) { doc_dirty = 1; scene_reupload(); }
    if (first[0]) snprintf(cam_msg, sizeof cam_msg, "%s", first);
    return n;
}
static void sel_save(unsigned char *m, int *s, int *o) { memcpy(m, op_msel, sizeof op_msel); *s = sel; *o = msel_owner; }
static void sel_restore(const unsigned char *m, int s, int o) { memcpy(op_msel, m, sizeof op_msel); sel = s < F.nops ? s : -1; msel_owner = o; sim = -1; }

/* OMregenSelectedOperations (decomp/dirty/mc_dirty1.c @18034f800): no op list / nothing selected -> FUN_18034e400(0) posts
   mccoreres 3964; else RegenSelected, then the graphics rebuilt */
int op_regen_selected(void) {
    if (!loaded || !F.nops || show_params) return 0;   /* ours: the op dialog is up (Mastercam's is modal) */
    if (op_nselected() < 1) { cui_message("apprenticeCAM", "No operations were selected.  Please select at least one operation."); return 0; }
    unsigned char m[1024] = { 0 }, sm[sizeof op_msel]; int ss, so;
    for (int i = 0; i < F.nops && i < 1024; i++) m[i] = op_selected(i) != 0;
    sel_save(sm, &ss, &so);
    int n = regen_mask(m);
    sel_restore(sm, ss, so);
    return n;
}
/* OMselectDirtyOperations (@18034f9b0) -> COpMgrTree::SelectAllInvalid(1) -> COperationsArray::SelectAll(0) (none) then
   SelectAllInvalid: every op with +0x94 (decomp/opman_opsarray.c l.4163) */
void op_select_dirty(void) {
    if (!loaded || !F.nops) return;
    memset(op_msel, 0, sizeof op_msel); sel = -1;
    for (int i = 0; i < F.nops && i < 1024; i++) if (op_dirty(i)) { op_msel[i] = 1; if (sel < 0) sel = i; }
    msel_owner = sel; sim = -1;
}
/* OMregenDirtyOperations (@18034f760): op list not empty -> OMselectDirtyOperations, FUN_18034e400(0) (none selected ->
   mccoreres 3964 "No operations were selected...") -> OMregenSelectedOperations */
int op_regen_dirty(void) {
    if (!loaded || !F.nops || show_params) return 0;
    op_select_dirty();
    return op_regen_selected();
}

/* ---- COpMgrTree::DirtyOperationWarning (decomp/opman_tree_all.c l.14583), run by BackplotSelected, VerifySelected,
   PostSelected and EditSelectedNci: the selected ops with +0x94 set, not locked (+0xe0) and with NCI (!op_is_no_nci) ->
   CDirtyOpsDlg (opmanres dialog 977, FUN_18012f020; DDX_Radio 25350 -> +0x1550, OnInitDialog sets it 0 = Regenerate):
   OK + 25350 -> select the dirty ops, RegenSelected (TPProcessManager off), select the old selection again, go on (0);
   OK + 25351 -> unselect the selected ops whose NCI section is not valid (bnci_op_section_valid); none left -> post_warning
   mccoreres 4944 "INVALID NCI" + "\n\n" + op_make_description(the last op), stop (-1); else go on (1); Cancel -> stop.
   Here the post works on every op (post_ops: ours, as before), Backplot / Verify on the selected ones. */
static int dop_acts(int action, int i) { return action == DOP_POST ? 1 : op_selected(i); }
static int op_nci_valid(int i) { return F.ops[i].npts > 0; }   /* ours: bnci_op_section_valid = the op has a toolpath here */
int dop_check(int action) {
    if (dop_bypass) { dop_bypass = 0; return 0; }
    dop_post_nskip = 0;
    if (!loaded) return 0;
    int any = 0;
    for (int i = 0; i < F.nops && i < 1024; i++) if (dop_acts(action, i) && op_dirty(i)) any = 1;
    if (!any) return 0;
    dop_pending = action; dop_choice = 0;           /* OnInitDialog: +0x1550 = 0, UpdateData(FALSE) */
    return 1;
}
int dop_answer(int ok) {
    int a = dop_pending; dop_pending = 0;
    if (!a || !ok) return 0;                        /* Cancel (DoModal != IDOK): the command does not run */
    if (dop_choice == 1) {                          /* 25351 Do not regenerate and continue */
        int left = 0, last = -1;
        memset(dop_post_skip, 0, sizeof dop_post_skip); dop_post_nskip = 0;
        for (int i = 0; i < F.nops && i < 1024; i++) {
            if (!dop_acts(a, i)) continue;
            last = i;
            if (op_nci_valid(i)) { left++; continue; }
            if (a == DOP_POST) { dop_post_skip[i] = 1; dop_post_nskip++; }
            else { if (sel >= 0 && msel_owner == sel) op_msel[i] = 0; else if (sel == i) sel = -1; }   /* UnSelectOperations */
        }
        if (!left) {
            char d[200] = ""; if (last >= 0) cui_otx_op_row(last, d, sizeof d);   /* op_make_description: NOT DECODED (ours: the tree row text) */
            cui_message("Mastercam", "INVALID NCI\n\n%s", d);                    /* mccoreres 4944 + DAT_1805d89a8 "\n\n" */
            dop_post_nskip = 0; return 0;
        }
        if (a != DOP_POST && sel >= 0 && msel_owner == sel && !op_msel[sel]) { int f = -1; for (int i = 0; i < F.nops && i < 1024; i++) if (op_msel[i]) { f = i; break; } sel = f; msel_owner = f; }
    } else {                                        /* 25350 Regenerate unlocked operations and continue */
        unsigned char m[1024] = { 0 }, sm[sizeof op_msel]; int ss, so;
        for (int i = 0; i < F.nops && i < 1024; i++) m[i] = dop_acts(a, i) && op_dirty(i);
        sel_save(sm, &ss, &so);
        regen_mask(m);
        sel_restore(sm, ss, so);
    }
    dop_bypass = 1;
    return a;
}
/* the post's ops left out by "Do not regenerate" (post_ops): NULL = every op */
const unsigned char *dop_post_only(unsigned char *only) {
    if (!dop_post_nskip) return NULL;
    for (int i = 0; i < F.nops && i < 1024; i++) only[i] = !dop_post_skip[i];
    dop_post_nskip = 0;
    return only;
}

/* ---- the project file (ours): DIRTY C<k> / DIRTY S<id> = a dirty op (k = the k-th op of ours written, as TREE), then its
   edit as an operations block whose lines start with "D:" (cam_write_ops; read back through cam_load_ops_ord). The
   stored ops' own mark is the older REGEN / STALE line (cui_doc.c). */
static void write_edit_block(FILE *f, CamOp *e) {
    FILE *m = tmpfile(); if (!m) return;
    cam_write_ops(m, &e, 1, &F); rewind(m);
    char *ln = NULL; size_t cap = 0;
    while (read_line(m, &ln, &cap)) fprintf(f, "D:%s\n", ln);
    free(ln); fclose(m);
}
void dirty_write_project(FILE *f) {
    for (int k = 0; k < nop_gone; k++) fprintf(f, "OPGONE %d\n", op_gone_ids[k]);   /* ours: ops of the part deleted / cut here
                                                                                   (the part file is never rewritten by a project save) */
    for (int j = 0; j < ncamops; j++) {
        if (!camops[j]->dirty) continue;
        fprintf(f, "DIRTY C%d %d\n", j, camops[j]->dirty);
        if (camops[j]->edit) write_edit_block(f, camops[j]->edit);
    }
    for (int k = 0; k < nsed; k++) {
        fprintf(f, "DIRTY S%d 1\n", sed[k].id);
        write_edit_block(f, sed[k].op);
    }
}
/* after the ops are placed (doc_read): CAMOP_OF_K(k) = our op written as the k-th, or NULL */
static CamOp *read_edit_block(FILE *g, char **ln, size_t *lcap, char *tmpl) {
    /* the D: lines up to the next non-D: line into a temporary operations file, read with cam_load_ops_ord */
    char path[1100]; snprintf(path, sizeof path, "%s", tmpl);
    FILE *t = fopen(path, "w"); if (!t) return NULL;
    long pos = ftell(g);
    while (read_line(g, ln, lcap)) {
        if (strncmp(*ln, "D:", 2)) { fseek(g, pos, SEEK_SET); break; }
        fprintf(t, "%s\n", *ln + 2); pos = ftell(g);
    }
    fclose(t);
    CamOp **ops = NULL; int n = cam_load_ops_ord(path, &F, &ops, NULL);
    remove(path);
    CamOp *e = n > 0 ? ops[0] : NULL;
    for (int i = 1; i < n; i++) cam_op_free(ops[i]);
    free(ops);
    return e;
}
void dirty_read_project(const char *proj, CamOp *(*camop_of_k)(int k, void *ctx), void *ctx) {
    FILE *g = fopen(proj, "r"); if (!g) return;
    char *ln = NULL; size_t lcap = 0, n = 0; (void)n;
    char tmpl[1100]; snprintf(tmpl, sizeof tmpl, "%s.dirty.tmp", proj);
    while (read_line(g, &ln, &lcap)) {
        if (!strncmp(ln, "OPGONE ", 7)) { op_gone_apply(atoi(ln + 7)); continue; }
        if (strncmp(ln, "DIRTY ", 6)) continue;
        char kind = ln[6]; int a = atoi(ln + 7), fl = 1; const char *sp = strchr(ln + 7, ' '); if (sp) fl = atoi(sp + 1);
        long pos = ftell(g); int hasblock = 0;
        if (read_line(g, &ln, &lcap) && !strncmp(ln, "D:", 2)) hasblock = 1;
        fseek(g, pos, SEEK_SET);
        CamOp *e = hasblock ? read_edit_block(g, &ln, &lcap, tmpl) : NULL;
        if (kind == 'C') {
            CamOp *o = camop_of_k(a, ctx);
            if (!o) { cam_op_free(e); continue; }
            o->dirty = fl ? fl : CAM_DIRTY_PARAMS;
            if (e) { e->dirty = 0; cam_op_free(o->edit); o->edit = e; }
        } else if (kind == 'S') {
            int i = -1; for (int j = 0; j < F.nops; j++) if (F.ops[j].id == a && camop_of(j) < 0) i = j;
            if (i < 0) { cam_op_free(e); continue; }
            if (e) { e->p.file_op_id = a; op_mark_dirty_edit(i, e); } else F.ops[i].regen = 1;
        } else cam_op_free(e);
    }
    free(ln); fclose(g);
}

/* the .mcam save (mcam_save_part): the ops of ours with an edit are written as the edit (Mastercam saves the op's
   parameters; every op of ours is written dirty, +0xfc = 1), and the stored ops with an edit as an edited op (its own
   record, dirty). OUT = malloc'd list; returns its count. */
int dirty_save_list(CamOp ***out) {
    CamOp **l = malloc(sizeof(CamOp *) * (size_t)(ncamops + nsed + 1)); int n = 0;
    for (int j = 0; j < ncamops; j++) l[n++] = camops[j]->edit ? camops[j]->edit : camops[j];
    for (int k = 0; k < nsed; k++) l[n++] = sed[k].op;
    *out = l; return n;
}

/* ---- tests (ours): tpseldirty / tpregen / tpregendirty (the toolbar 800 buttons), dirtyopt:0|1 (977's radio), dirtyok /
   dirtycancel (977's OK / Cancel; the command then runs through cui_hooks.dop_resume, else here: the post), dirtydump */
int cui_dirty_script(const char *st, const char *arg) {
    #define IS(S_) (!strcmp(st, S_))
    if (IS("tpseldirty")) { op_select_dirty(); return 1; }
    if (IS("tpregen")) { op_regen_selected(); return 1; }
    if (IS("tpregendirty")) { op_regen_dirty(); return 1; }
    if (IS("movehash")) {                                 /* ours (tests): each op made / taken over here - its move count and a hash of the moves */
        for (int j = 0; j < ncamops; j++) { const CamOp *o = camops[j]; unsigned long long h = 1469598103934665603ull;
            for (int k = 0; k < o->nm; k++) { const Move *v = &o->m[k];   /* the fields, not the padding */
                double d[8] = { v->p.x, v->p.y, v->p.z, v->feed, v->cx, v->cy, v->cz, 0 }; int iv[6] = { v->rapid, v->arc != 0, v->cw, v->cc, v->apl, v->ctf };   /* arc: a tag only (a running id, mb_tag_arc) */
                const unsigned char *q = (const unsigned char *)d; for (size_t b = 0; b < sizeof d; b++) h = (h ^ q[b]) * 1099511628211ull;
                q = (const unsigned char *)iv; for (size_t b = 0; b < sizeof iv; b++) h = (h ^ q[b]) * 1099511628211ull; }
            printf("    movehash op [%d] %s moves %d hash %016llx\n", o->op_index + 1, o->name, o->nm, h); }
        return 1;
    }
    if (IS("movesout") && arg) {                          /* ours (tests, tools/tests/regen_consistency.sh): each op made / taken over here -
                                                             its moves to ARG<op number>.m, the format of cam_cli --movesf (x y z rapid feed) */
        for (int j = 0; j < ncamops; j++) { const CamOp *o = camops[j]; char fn[1200]; snprintf(fn, sizeof fn, "%s%d.m", arg, o->op_index + 1);
            FILE *fo = fopen(fn, "w"); if (!fo) continue;
            for (int k = 0; k < o->nm; k++) fprintf(fo, "%.6f %.6f %.6f %d %.4f\n", o->m[k].p.x, o->m[k].p.y, o->m[k].p.z, o->m[k].rapid, o->m[k].feed);
            fclose(fo); printf("    movesout op [%d] %d moves -> %s\n", o->op_index + 1, o->nm, fn); }
        return 1;
    }
    if (IS("dirtyopt") && arg) { if (dop_pending) dop_choice = atoi(arg) ? 1 : 0; return 1; }
    if (IS("dirtyok") || IS("dirtycancel")) {
        if (!dop_pending) { printf("    (no dialog 977)\n"); return 1; }
        int a = dop_answer(IS("dirtyok"));
        if (a) { if (cui_hooks.dop_resume) cui_hooks.dop_resume(a); else if (a == DOP_POST) post_ops(); }
        return 1;
    }
    if (IS("dirtydump")) {
        printf("    dirty dialog977 %d choice %d:", dop_pending, dop_choice);
        for (int i = 0; loaded && i < F.nops; i++) if (op_dirty(i)) printf(" %d%s", i + 1, op_dirty_edit(i) ? "e" : "");
        printf("\n"); return 1;
    }
    #undef IS
    return 0;
}
