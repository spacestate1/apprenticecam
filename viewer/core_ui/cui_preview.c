/* cui_preview.c - Q20 "Preview toolpath" (op dialog toolbar 10501; formats/opdlg_port_audit.md 22). Decompiles: decomp/b12/tpdlg_all.c
   (ToolpathDlg::OnPreviewToolpathPb 0x180893ea0, OnCancel 0x180892c50, RestoreOperation 0x180895be0, GetPreviewOp 0x1808905a0,
   GetTpPreviewed 0x1808908d0, AddDefaultsButtons 0x18088faa0 (also decomp/defs/core_oplib.c)), decomp/mcmill_all.c
   RunChainBasedTpDialog 0x18024ceb0 (l.401903), decomp/mccore_edit_operation.c (edit_operation's compare, l.1338).

   Mastercam (decoded):
   - The button exists for opcodes 1, 2, 3, 0x12, 0x13, 100, 102, 105, 106, 130, 134, 135, 0x132 (AddDefaultsButtons).
   - OnPreviewToolpathPb (this[0x729c] == 0): repaint_toolpath = 1 (the toolpaths are drawn while the dialog is up); OnOK_Pages (the
     pages into the dialog's op), UpdateOperationEntity(1) (the dialog's op written over the op in the database), a copy at +0x7360,
     this[0x9c30] (previewed) = 1, in_tp_prvw_mode = 1, bnci_section(op, ...) = the op's toolpath computed with the dialog's values,
     GetOperation (the op as computed back into the dialog), the copy at +0x7360 again (= GetPreviewOp's op). 2D HST (0x86) runs
     with multithreading off.
   - OK: RunChainBasedTpDialog -> GetTpPreviewed (vtable +0x478) true -> GetPreviewOp (+0x470) replaces edit_operation's "op before
     the dialog": OK with no change since the preview leaves the op as computed (memcmp_op equal: nothing else done); a change since
     the preview takes edit_operation's changed path (bnci_update, else op +0x94 = 1: dirty, the previewed toolpath kept).
   - Cancel (OnCancel): previewed -> the saved op's +0x94 (this[0x4a54] = this+0x49c0 + 0x94) = 1 and previewed = 0; RestoreOperation
     writes the op saved at OnInitDialog back (UpdateListAndDB): the parameters as before, marked dirty - its NCI is the preview's.
   Here: the preview is computed on a copy (gen_begin's applies on the dialog's op, gen_run) and drawn in the graphics in place of the
   op's toolpath (scene.c scene_tp_hide_id + the overlay, toolpath colours) - the op itself is not touched until OK. OK with no change
   since the preview commits the previewed op as computed (an op of ours replaced, a stored op taken over - gen_commit); a change
   outside the BnciUpdate mask since the preview commits it and marks it dirty with the dialog's values as its edit (cui_dirty.c);
   any other change computes again (as OK always did). Cancel leaves the op and its toolpath / NCI exactly as they were - this undoes
   what Mastercam's UpdateOperationEntity / bnci_section did (user decision Q20: "including undoing the stored-op takeover on Cancel")
   - and marks the op dirty as OnCancel does (decision P-D1 in the audit: the mark follows Mastercam although the toolpath is the
   original here). Ours: drawn as an overlay; a new op (not in the list yet) is previewed the same way and Cancel leaves nothing; a
   tool not in the part's list ("custom") is refused (registering it cannot be undone). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "platform.h"
#include "scene.h"
#include "mcam_saveops.h"
#include "core_ui.h"

int pv_on;                                   /* ToolpathDlg this[0x9c30]: a preview since the dialog opened */
static CamOp *pv_job;                        /* the previewed op as computed (GetPreviewOp's op + its moves); NULL = failed */

static void job_free(CamOp *o) {
    for (int i = 0; i < o->nchains; i++) cam_chain_free(&o->chains[i]);
    free(o->chains); free(o->holes); free(o->m);
    o->chains = NULL; o->holes = NULL; o->m = NULL; o->nchains = o->nholes = o->nm = 0;
}
/* the existing op the dialog edits (its index), -1 = a new op */
static int pv_index(void) {
    if (edit_camop >= 0 && edit_camop < ncamops) return camops[edit_camop]->op_index;
    if (pending.p.file_op_id > 0) for (int i = 0; i < F.nops; i++) if (F.ops[i].id == pending.p.file_op_id) return i;
    return -1;
}
void pv_clear(void) {
    if (pv_job) { job_free(pv_job); free(pv_job); pv_job = NULL; }
    pv_on = 0; pp_pv_forget();
    if (scene_tp_hide_id) { scene_tp_hide_id = 0; }
    overlay_dirty = 1;
}
/* AddDefaultsButtons' opcode list (every 2D type made here; not 3D OptiRough 0x84) */
int pv_shown(void) {
    static const int OC[] = { 1, 2, 3, 0x12, 0x13, 100, 102, 105, 106, 130, 134, 135, 0x132 };
    int st, oc = mcam_opcode_for(&pending.p, &st);
    for (size_t i = 0; i < sizeof OC / sizeof OC[0]; i++) if (OC[i] == oc) return 1;
    return 0;
}
int pv_live(void) { return show_params && pv_shown(); }
/* OnOK_Pages + UpdateOperationEntity's op: the dialog applied on a copy (gen_begin), the dialog itself left as it is */
int pv_begin(CamOp *job) {
    if (!pv_live()) return 0;
    if (tool_choice >= F.ntools) {   /* ours: gen_begin would add the tool to the part, which a Cancel cannot take back */
        snprintf(cam_msg, sizeof cam_msg, "Preview toolpath: choose a tool of the part's tool list first (apprenticeCAM)"); return 0; }
    CamParams keep; memcpy(&keep, &pending.p, sizeof keep); OpHFlags hb; memcpy(&hb, &pp_hf, sizeof hb);
    int r = gen_begin(job);
    memcpy(&pending.p, &keep, sizeof keep); memcpy(&pp_hf, &hb, sizeof hb);
    if (!r) return 0;                                  /* a message (no tool), as generate_pending */
    pp_pv_snapshot();                                  /* the op OK compares with (GetPreviewOp) */
    return 1;
}
/* bnci_section's result: the preview shown (CANCELLED: the computation was stopped - nothing changes) */
void pv_end(CamOp *job, int ok, int cancelled) {
    if (cancelled) { job_free(job); pp_pv_forget(); return; }
    if (pv_job) { job_free(pv_job); free(pv_job); pv_job = NULL; }
    pv_on = 1;                                         /* this[0x9c30] = 1 (before bnci_section) */
    if (!ok) { snprintf(cam_msg, sizeof cam_msg, "%s failed: %s", cam_op_label(&pending), job->error); job_free(job); }
    else {
        pv_job = malloc(sizeof *pv_job); *pv_job = *job;
        if (job->warning[0]) snprintf(cam_msg, sizeof cam_msg, "%s: warning: %s", cam_op_label(&pending), job->warning);   /* else the prompt line stays */
    }
    int i = pv_index();
    scene_tp_hide_id = i >= 0 ? F.ops[i].id : 0;      /* the op's own toolpath gives way to the preview */
    overlay_dirty = 1;
}
/* the button (script optb:10501 / oppreview): computed at once */
int pp_preview(void) {
    CamOp job; memset(&job, 0, sizeof job);
    if (!pv_begin(&job)) return 0;
    int ok = gen_run(&job);
    pv_end(&job, ok, 0);
    return 1;
}
int pv_has_job(void) { return pv_on && pv_job != NULL; }
/* OK, no change since the preview: the previewed op committed as computed */
void pv_commit(void) {
    CamOp *j = pv_job; pv_job = NULL;
    if (!j) { pv_clear(); return; }
    gen_end(j, 1, 0); free(j);                          /* pending = the computed op -> gen_commit */
    pv_clear();
}
/* OK, a change since the preview outside the BnciUpdate mask: the previewed op committed, then dirty with the dialog's values */
void pv_commit_dirty(void) {
    CamOp job2; memset(&job2, 0, sizeof job2);
    if (!gen_begin(&job2)) { pv_commit(); return; }
    CamOp *e = malloc(sizeof *e); *e = job2; e->m = NULL; e->nm = 0; e->op_index = -1; e->dirty = 0; e->edit = NULL;
    pv_commit();
    int idx = sel;                                      /* gen_commit: sel = the committed op */
    if (idx >= 0) { op_mark_dirty_edit(idx, e); doc_dirty = 1; scene_reupload(); }
    else cam_op_free(e);
}
/* OnCancel: previewed -> the op restored and dirty (op +0x94 = 1); here the op was never changed, only marked */
void pv_cancel(void) {
    if (!pv_on) return;
    int i = pv_index();
    pv_clear();
    if (i >= 0) { op_mark_dirty_edit(i, NULL); doc_dirty = 1; }
}
/* the preview's moves in the overlay (feed / rapid colours of the toolpaths, config 5045 / 5046) while the dialog is up */
void pv_overlay(float **v, int *n, int *cap) {
    if (!show_params || !pv_on || !pv_job) return;
    float fc[3], rc[3]; mc_rgb(cfg_feed_color, fc); mc_rgb(cfg_rapid_color, rc);
    const Move *m = pv_job->m;
    for (int i = 1; i < pv_job->nm; i++) {
        if (m[i].rapid && !show_rapid) continue;
        const float *c = m[i].rapid ? rc : fc;
        ov_pt(v, n, cap, m[i - 1].p, c[0], c[1], c[2]); ov_pt(v, n, cap, m[i].p, c[0], c[1], c[2]);
    }
}
/* tests (ours): oppreview, pvdump (the preview's move count / extent), opsum (per op: id, dirty, the FNV-1a of its stored record and
   of its toolpath points - "byte-identical" checks before / after a preview) */
static uint64_t fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }
int cui_preview_script(const char *st, const char *arg) {
    (void)arg;
    if (!strcmp(st, "opsum")) {
        for (int i = 0; loaded && i < F.nops; i++) {
            const Op *o = &F.ops[i]; uint64_t hr = 1469598103934665603ull, hp = hr;
            if (o->raw) hr = fnv(hr, o->raw, o->raw_n);
            for (int k = o->first_poly; k < o->first_poly + o->npolys && k < F.npolys; k++) {
                const Poly *pl = &F.polys[k]; hp = fnv(hp, &pl->kind, sizeof pl->kind); hp = fnv(hp, &F.pts[pl->first], sizeof(V3) * (size_t)pl->count); }
            printf("    opsum %d id %d dirty %d rec %zu %016llx tp %ld %016llx\n", i + 1, o->id, op_dirty(i), (size_t)o->raw_n, (unsigned long long)hr, (long)o->npts, (unsigned long long)hp);
        }
        return 1;
    }
    if (!strcmp(st, "oppreview")) { if (show_params) pp_preview(); return 1; }
    if (!strcmp(st, "pvdump")) {
        printf("    preview: on %d job %d hide %d", pv_on, pv_job != NULL, scene_tp_hide_id);
        if (pv_job) {
            double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 }; int nr = 0;
            for (int i = 0; i < pv_job->nm; i++) { const double *q = &pv_job->m[i].p.x; for (int k = 0; k < 3; k++) { if (q[k] < lo[k]) lo[k] = q[k]; if (q[k] > hi[k]) hi[k] = q[k]; } nr += pv_job->m[i].rapid != 0; }
            printf(" moves %d rapids %d box %.4f %.4f %.4f .. %.4f %.4f %.4f", pv_job->nm, nr, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
        }
        printf("\n"); return 1;
    }
    return 0;
}
