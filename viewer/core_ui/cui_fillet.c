/* cui_fillet.c - Solids > Modify > Constant Fillet (FT sld_do_fillet 0x1800e3670, SolidsConstantFilletApp; formats/solid_fillet.md is the
   spec, "sec. N" below = its sections). The selection (Solid Selection dialog 1413: Body / Face / Edge, Shift = tangents), the panel model
   (SolidsFilletManager: Name, Propagate along tangencies, Mitered corners, Overflow, the selection list, Radius, Automatically preview),
   preview, OK / OK-New / Cancel / Esc, editing a fillet op, and the feature's application on the OpenCASCADE kernel (fil_apply ->
   occt_solid.cpp ocs_fillet). A fillet is one feature per body (ExFeat.fl), replayed like the Extrude features (cui_feat.c) and written as a
   SldFilletOp by the part save (cui_sldsave.c). The front ends draw the panel (viewer-qt src/filpanel.cpp) over this model.
   User decisions (formats/user_decisions.md SOL-F1..F5): Propagate off is stored but refused (not built); Overflow stored, OCCT's own
   overflow handling for all three; corner geometry / failures / record order are OCCT's; failure box = sld 538 + 665; smooth edges by
   OCCT's continuity check. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"

/* ---- session (sec. 0 / 1.4): SLD_FILLET_PARAMS DAT_1803e9660 (static default DAT_1803b9708: radius 0.1, overflow 0, mitered 0,
   propagate 1), radius RegisterVariable(&DAT_1803e9668, 1.0, 0.1) -> metric 1.0; written on OK / OK-New only */
FilSess fl_sess = { 1.0, 0, 0, 1 };
static int fl_sess_metric = 1;
int fl_selbtn[3] = { 1, 1, 1 };         /* DAT_1803bac30 / 31 / 32: the Solid Selection dialog's Body / Face / Edge (1 in the image) */
/* ---- the running command */
int fl_mode, fl_phase;                  /* fl_phase 0: the pick (FUN_1801bd610 / FUN_1801bd2d0), 1: the panel */
int fl_edit_idx = -1;                   /* app+0x20: the feature being edited */
FilSess fl_p;                           /* P (app+0x228): radius, overflow, mitered, propagate */
char fl_name[64];                       /* app+0x61 */
int fl_prev;                            /* the panel's "Automatically preview results" (DAT_1803bac10 = ex_autoprev, shared) */
int fl_prev_req;                        /* the Preview button (0x2bf) */
static unsigned long fl_prev_hash;
int fl_tab;                             /* 0 Basic, 1 Advanced */
int fl_adding;                          /* the pick runs from the panel: 1 Add Selection (0x322, 0), 2 Reselect All (0x322, 1) */
int fl_sel_changed;                     /* [RSP+0x41] of the panel loop: Add / Reselect / Remove happened (edit: OK rebuilds) */
char fl_warn[300];                      /* the last preview's message (FUN_180156bc0: DisplayWarning of the log), ours: shown by the panel */
FlPick fl_pk[3][FL_MAXPICK]; int fl_npk[3];     /* app+0x240 bodies / +0x268 faces / +0x290 edges, in pick order */
static FlPick fl_save[3][FL_MAXPICK]; static int fl_nsave[3];
unsigned char fl_lsel[3][FL_MAXPICK];    /* the selection list's selected entries (IsSelected) */

/* mcgeomsldres strings of this command (decomp/extrude/sldres_str.tsv) */
#define FL_S438 "Select entities to fillet"
#define FL_S504 "Use dialog to modify fillet settings"
#define FL_REFUSE "The fillet is not built: \"Propagate along tangencies\" is off, and the OpenCASCADE kernel always fillets along the tangent edges (user decision SOL-F1)."

void fl_units(void) {                    /* RegisterVariable(&radius, 1.0, 0.1): converted when the units change (cui_extrude.c ex_units) */
    int m = mcam_is_metric(&F);
    if (m == fl_sess_metric) return;
    if (m) fl_sess.radius *= 10.0; else fl_sess.radius /= 10.0;
    fl_sess_metric = m;
}
static void fl_prompt(void) {
    if (fl_phase == 0) snprintf(cam_msg, sizeof cam_msg, "%s", FL_S438);           /* sld 438 (0x1b6) */
    else if (fl_edit_idx >= 0) cam_msg[0] = 0;                                     /* FUN_1801bcb10: empty in edit mode */
    else snprintf(cam_msg, sizeof cam_msg, "%s", FL_S504);                         /* sld 504 (0x1f8) */
}

FilFeat *filfeat_dup(const FilFeat *f) {
    if (!f) return NULL;
    FilFeat *r = malloc(sizeof *r); *r = *f;
    r->frec = malloc(sizeof(int) * (f->nf + 1)); r->erec = malloc(sizeof(int) * (f->ne + 1));
    r->fpt = malloc(sizeof *r->fpt * (f->nf + 1)); r->ept = malloc(sizeof *r->ept * (f->ne + 1));
    if (f->nf) { memcpy(r->frec, f->frec, sizeof(int) * f->nf); memcpy(r->fpt, f->fpt, sizeof *r->fpt * f->nf); }
    if (f->ne) { memcpy(r->erec, f->erec, sizeof(int) * f->ne); memcpy(r->ept, f->ept, sizeof *r->ept * f->ne); }
    return r;
}
void filfeat_free(FilFeat *f) { if (!f) return; free(f->frec); free(f->erec); free(f->fpt); free(f->ept); free(f); }

/* ---- the feature on its body (sec. 3 / 5; replay = SldFilletOp slot 25 FUN_180281450) */
static int same_pt(const double *a, const double *b) { double d = 0; for (int k = 0; k < 3; k++) d += (a[k] - b[k]) * (a[k] - b[k]); return d <= EX_TOL * EX_TOL * 100; }
/* a pick's face / edge on the body now: by its record (FUN_180269310); a pick without a record by its bbox centre (ours: Mastercam writes id
   0 for such edges and would fail sld 291 on a replay) */
static int fil_resolve(const OcsBody *ob, int kind, int rec, const double *pt) {
    if (rec >= 0) return ocs_find_rec(ob, kind, rec);
    int n = kind ? ocs_nedges(ob) : ocs_nfaces(ob);
    for (int i = 0; i < n; i++) { if ((kind ? ocs_edge_rec(ob, i) : ocs_face_rec(ob, i)) >= 0) continue;
        double c[3]; if (ocs_sub_center(ob, kind, i, c) && same_pt(c, pt)) return i; }
    return -1;
}
int fil_apply(Body **bs, int *nb, ExFeat *f, ExErr *e) {
    e->id = 0; e->caption = -1;
    const FilFeat *q = f->fl; int bi = f->p.target;
    if (!q || bi < 0 || bi >= *nb || !(*bs)[bi].occ) { e->id = 291; e->caption = 201; return 0; }
    if (!q->propagate) { e->id = FL_ERR_REFUSE; e->caption = 201; return 0; }        /* SOL-F1 */
    OcsBody *ob = (*bs)[bi].occ;
    /* FUN_180096570: body -> all its edges; face -> its edges; edge -> itself; each once, in that order */
    int nE = ocs_nedges(ob), *E = malloc(sizeof(int) * (nE + 1)), n = 0;
    unsigned char *in = calloc(nE + 1, 1);
#define ADD(x) do { int x_ = (x); if (x_ >= 0 && x_ < nE && !in[x_]) { in[x_] = 1; E[n++] = x_; } } while (0)
    if (q->whole) for (int i = 0; i < nE; i++) ADD(i);
    int missing = 0;
    for (int k = 0; k < q->nf; k++) {
        int fi = fil_resolve(ob, 0, q->frec[k], q->fpt[k]); if (fi < 0) { missing = 1; continue; }
        if (q->whole) continue;
        int *fe = NULL, m = ocs_face_edges(ob, fi, &fe); for (int i = 0; i < m; i++) ADD(fe[i]); free(fe);
    }
    for (int k = 0; k < q->ne; k++) { int ei = fil_resolve(ob, 1, q->erec[k], q->ept[k]); if (ei < 0) { missing = 1; continue; } ADD(ei); }
#undef ADD
    free(in);
    if (missing) { free(E); e->id = 291; e->caption = 201; return 0; }   /* "Fillet input edge(s)/face(s) not found." (FUN_180281450) */
    /* FUN_180036310: smooth edges dropped (PK_EDGE_is_smooth; SOL-F5: BRepLib::ContinuityOfFaces at 1e-10 rad) */
    int k2 = 0; for (int i = 0; i < n; i++) if (!ocs_edge_smooth(ob, E[i])) E[k2++] = E[i];
    n = k2;
    OcsFilP p = { q->radius, q->overflow, q->mitered, q->propagate };
    OcsBody *out = NULL; char err[300] = "";
    int ok = ocs_fillet(ob, E, n, &p, &out, err, sizeof err);
    free(E);
    if (!ok) { ex_log("fillet kernel: %s", err); e->id = 665; e->caption = 538; return 0; }   /* SOL-F4: sld 538 + 665 */
    int tag = f >= feats && f < feats + nfeats ? (int)(f - feats) : -1;
    ocs_op_set_tag(out, ocs_nops(out) - 1, tag);
    Body *t = &(*bs)[bi]; char nm[64]; snprintf(nm, sizeof nm, "%s", t->name);
    body_free(t); *t = body_of_occ(out, nm);
    return 1;
}

/* ---- the command */
static void fl_clear_sel(void) { memset(fl_npk, 0, sizeof fl_npk); memset(fl_lsel, 0, sizeof fl_lsel); }
void fl_start(void) {
    if (!loaded && cui_hooks.new_part) cui_hooks.new_part();
    if (!loaded) return;
    geo_end(); cam_mode = 0; show_params = 0; cam_clear_selection();
    if (ex_mode) return;
    fl_units();
    /* ctor FUN_1801bc100: P = the session block, name sld 823; set_clr_sel_bits / ps_reset_all_hilights */
    fl_p = fl_sess; snprintf(fl_name, sizeof fl_name, "Constant Radius Fillet");
    fl_prev = ex_autoprev; fl_prev_req = 0; fl_prev_hash = 0; fl_tab = 0; fl_adding = 0; fl_sel_changed = 0; fl_warn[0] = 0;
    fl_edit_idx = -1; fl_clear_sel();
    fl_mode = 1; fl_phase = 0; fl_prompt(); overlay_dirty = 1;
    ex_log("fillet: start, %d bod(ies), selection Body %d Face %d Edge %d", nsbodies, fl_selbtn[0], fl_selbtn[1], fl_selbtn[2]);
}
/* editing (SldFilletOp slot 27 FUN_180280ab0 -> SolidConstantFilletWithUI): the op's P, name and picks; the picks resolve on the body as it is
   before the op (the replay of the features before it) */
void fl_edit(int fi) {
    if (fi < 0 || fi >= nfeats || !feats[fi].fl || fl_mode || ex_mode || !loaded) return;
    geo_end(); cam_mode = 0; show_params = 0; cam_clear_selection();
    const ExFeat *f = &feats[fi]; const FilFeat *q = f->fl;
    fl_edit_idx = fi;
    fl_p.radius = q->radius; fl_p.overflow = q->overflow; fl_p.mitered = q->mitered; fl_p.propagate = q->propagate;
    snprintf(fl_name, sizeof fl_name, "%s", f->name);
    fl_prev = ex_autoprev; fl_prev_req = 0; fl_prev_hash = 0; fl_tab = 0; fl_adding = 0; fl_sel_changed = 0; fl_warn[0] = 0;
    fl_clear_sel();
    Body *tmp = NULL; int nt = 0; ExErr e;
    for (int i = 0; i < fi; i++) feat_apply(&tmp, &nt, &feats[i], &e);
    int bi = f->p.target;
    if (q->whole && fl_npk[0] < FL_MAXPICK) { FlPick *p = &fl_pk[0][fl_npk[0]++]; memset(p, 0, sizeof *p); p->body = bi; p->idx = -1; p->rec = -1; }
    for (int k = 0; k < q->nf && fl_npk[1] < FL_MAXPICK; k++) {
        FlPick *p = &fl_pk[1][fl_npk[1]]; p->body = bi; p->rec = q->frec[k]; memcpy(p->pt, q->fpt[k], sizeof p->pt);
        p->idx = bi < nt && tmp[bi].occ ? fil_resolve(tmp[bi].occ, 0, p->rec, p->pt) : -1;
        if (p->idx >= 0) fl_npk[1]++;                       /* FUN_180276390: entries that do not resolve are dropped */
    }
    for (int k = 0; k < q->ne && fl_npk[2] < FL_MAXPICK; k++) {
        FlPick *p = &fl_pk[2][fl_npk[2]]; p->body = bi; p->rec = q->erec[k]; memcpy(p->pt, q->ept[k], sizeof p->pt);
        p->idx = bi < nt && tmp[bi].occ ? fil_resolve(tmp[bi].occ, 1, p->rec, p->pt) : -1;
        if (p->idx >= 0) fl_npk[2]++;
    }
    bodies_free(&tmp, &nt);
    fl_mode = 1; fl_phase = 1; fl_prompt(); overlay_dirty = 1;
}

/* the bodies the picks are made on: the bodies as they are (new op), or before the edited op (edit; sld_array_pick_in_body) */
static Body *pick_bodies(int *nt) {
    if (fl_edit_idx < 0) { *nt = nsbodies; return sbodies; }
    static Body *tmp; static int ntmp;
    bodies_free(&tmp, &ntmp); ExErr e;
    for (int i = 0; i < fl_edit_idx; i++) feat_apply(&tmp, &ntmp, &feats[i], &e);
    *nt = ntmp; return tmp;
}
static int find_pick(int kind, int body, int idx) { for (int i = 0; i < fl_npk[kind]; i++) if (fl_pk[kind][i].body == body && fl_pk[kind][i].idx == idx) return i; return -1; }
static void del_pick(int kind, int i) { memmove(&fl_pk[kind][i], &fl_pk[kind][i + 1], sizeof(FlPick) * (fl_npk[kind] - i - 1)); memmove(&fl_lsel[kind][i], &fl_lsel[kind][i + 1], fl_npk[kind] - i - 1); fl_npk[kind]--; }
static void add_pick(int kind, const Body *bs, int body, int idx) {
    if (fl_npk[kind] >= FL_MAXPICK) return;
    FlPick *p = &fl_pk[kind][fl_npk[kind]]; memset(p, 0, sizeof *p); p->body = body; p->idx = idx; p->rec = -1;
    if (kind == 1) { p->rec = ocs_face_rec(bs[body].occ, idx); ocs_sub_center(bs[body].occ, 0, idx, p->pt); }
    if (kind == 2) { p->rec = ocs_edge_rec(bs[body].occ, idx); ocs_sub_center(bs[body].occ, 1, idx, p->pt); }
    fl_lsel[kind][fl_npk[kind]] = 0; fl_npk[kind]++;
}
/* ours (not ported, question back to the user): a fillet on a solid read from the part. Its faces / edges carry no names after the load (the
   McEptr links are rebuilt only by Mastercam's history replay, formats/solid_history.md 3.3), so the op's lists A / B, the 0x2e refs and the
   consumed list (solid_fillet.md 6) could not be written as decoded */
void fl_part_solid_msg(void) {
    ex_log("fillet: a part solid was picked - not ported");
    cui_message("Constant Radius Fillet", "%s", "A solid read from the part cannot be filleted here yet: its faces and edges have no Mastercam names until its history is replayed, so the fillet's history record could not be written as Mastercam writes it (not ported).");
}
/* one click of the pick (sld_array_pick: sld_pick_one_ent mask 7 less the buttons that are off, FUN_1800a92f0 toggles; Shift + edge / face:
   the tangent edges / faces, sld_add_tangent_edge_sel / sld_add_tangent_face_sel). The hit rule (formats/ui.md, sld_identify_from_cursor):
   an edge needs the Edge button, else it becomes its body (Body on); a face needs Face, else its body */
void fl_click(float mx, float my, int shift) {
    if (!fl_mode || fl_phase != 0) return;
    double vx, vy, o[3], o2[3], d[3], vx2, vy2, q1[3];
    view_px_to_view(mx - vp_x, my - vp_y, &vx, &vy); view_px_to_view(mx - vp_x + 6, my - vp_y, &vx2, &vy2);
    view_to_world3(vx, vy, 1e6, o); view_to_world3(vx, vy, 0, o2); view_to_world3(vx2, vy2, 0, q1);
    for (int k = 0; k < 3; k++) d[k] = o2[k] - o[k];
    double ap = sqrt((q1[0] - o2[0]) * (q1[0] - o2[0]) + (q1[1] - o2[1]) * (q1[1] - o2[1]) + (q1[2] - o2[2]) * (q1[2] - o2[2]));   /* ours: 6 px */
    int nt; Body *bs = pick_bodies(&nt);
    int bb = -1, bf = -1, be = -1; double bt = 1e300;
    for (int i = 0; i < nt; i++) {
        if (!bs[i].occ) continue;
        if (fl_edit_idx >= 0 && i != feats[fl_edit_idx].p.target) continue;    /* sld_array_pick_in_body */
        int fi, ei; double t;
        if (ocs_pick(bs[i].occ, o, d, ap, &fi, &ei, &t) && t < bt) { bt = t; bb = i; bf = fi; be = ei; }
    }
    if (bb < 0) { if (st_pick_solid_at(mx, my) >= 0) fl_part_solid_msg(); return; }
    int kind = -1, idx = -1;
    if (be >= 0 && fl_selbtn[2]) { kind = 2; idx = be; }
    else if (bf >= 0 && fl_selbtn[1] && !(be >= 0 && !fl_selbtn[1])) { kind = 1; idx = bf; }
    else if (fl_selbtn[0]) { kind = 0; idx = -1; }
    if (kind < 0) return;
    if (shift && kind != 0) {
        int *v = NULL, m = kind == 2 ? ocs_tangent_edges(bs[bb].occ, idx, &v) : ocs_tangent_faces(bs[bb].occ, idx, &v);
        int on = find_pick(kind, bb, idx) >= 0;
        for (int i = 0; i < m; i++) { int j = find_pick(kind, bb, v[i]); if (on && j >= 0) del_pick(kind, j); else if (!on && j < 0) add_pick(kind, bs, bb, v[i]); }
        free(v);
    } else {
        int j = find_pick(kind, bb, idx);
        if (j >= 0) del_pick(kind, j); else add_pick(kind, bs, bb, idx);
    }
    ex_log("fillet: pick body %d %s %d -> bodies %d faces %d edges %d", bb, kind == 0 ? "body" : kind == 1 ? "face" : "edge", idx, fl_npk[0], fl_npk[1], fl_npk[2]);
    overlay_dirty = 1;
}
void fl_unselect_all(void) { if (fl_mode && fl_phase == 0) { fl_clear_sel(); overlay_dirty = 1; } }   /* "UnSelect All" (2961) */
void fl_end(int accepted, int restart);
/* End Selection / the Solid Selection dialog's OK (or Enter): FUN_1801bd610 - nothing picked ends the command; FUN_1801bd2d0 back to the
   panel */
void fl_sel_done(void) {
    if (!fl_mode || fl_phase != 0) return;
    if (fl_adding) { fl_adding = 0; fl_sel_changed = 1; fl_phase = 1; fl_prompt(); overlay_dirty = 1; return; }
    if (!fl_npk[0] && !fl_npk[1] && !fl_npk[2]) { fl_end(0, 0); return; }
    fl_phase = 1; fl_prompt(); overlay_dirty = 1;
    ex_log("fillet: panel up (bodies %d faces %d edges %d)", fl_npk[0], fl_npk[1], fl_npk[2]);
}
void fl_add_sel(int all) {                     /* 0x322: Add Selection (0) / Reselect All (1), FUN_1801bd2d0 */
    if (!fl_mode || fl_phase != 1) return;
    memcpy(fl_save, fl_pk, sizeof fl_save); memcpy(fl_nsave, fl_npk, sizeof fl_nsave);
    if (all) fl_clear_sel();
    fl_adding = all ? 2 : 1; fl_phase = 0; fl_prompt(); overlay_dirty = 1;
}
void fl_escape(void) {
    if (!fl_mode) return;
    if (fl_phase == 0 && fl_adding) {            /* Esc in the pick from the panel: the arrays as they were */
        memcpy(fl_pk, fl_save, sizeof fl_pk); memcpy(fl_npk, fl_nsave, sizeof fl_npk); memset(fl_lsel, 0, sizeof fl_lsel);
        fl_adding = 0; fl_phase = 1; fl_prompt(); overlay_dirty = 1; return;
    }
    fl_end(0, 0);                                 /* the first pick (FUN_1801bd610: Esc ends the command) or the panel (= Cancel) */
}
/* the selection list (FUN_180129280: "Body N", "Face N", "Edge N") */
int fl_list_n(void) { return fl_npk[0] + fl_npk[1] + fl_npk[2]; }
void fl_list_item(int i, char *out, size_t n, int *kind, int *k) {
    static const char *nm[3] = { "Body", "Face", "Edge" };      /* sld 686 / 687 / 688 */
    int t = 0; while (t < 2 && i >= fl_npk[t]) { i -= fl_npk[t]; t++; }
    snprintf(out, n, "%s %d", nm[t], i + 1); *kind = t; *k = i;
}
void fl_list_select(int i, int on) { int t, k; char b[32]; if (i < 0 || i >= fl_list_n()) return; fl_list_item(i, b, sizeof b, &t, &k); fl_lsel[t][k] = on ? 1 : 0; overlay_dirty = 1; }
int fl_list_selected(int i) { int t, k; char b[32]; if (i < 0 || i >= fl_list_n()) return 0; fl_list_item(i, b, sizeof b, &t, &k); return fl_lsel[t][k]; }
void fl_remove_sel(void) {                     /* 0x323 per selected entry (FUN_1801bd110) */
    for (int t = 0; t < 3; t++) for (int i = fl_npk[t] - 1; i >= 0; i--) if (fl_lsel[t][i]) { del_pick(t, i); fl_sel_changed = 1; }
    overlay_dirty = 1;
}
/* validator FUN_180216e00 */
void fl_validate(const char **e) {
    for (int i = 0; i < FLV_N; i++) e[i] = NULL;
    const char *s = fl_name; while (*s == ' ' || *s == '\t') s++;
    if (!*s) e[FLV_NAME] = "Name cannot be blank";                                                                   /* sld 2001 */
    if (!fl_list_n()) e[FLV_LIST] = "Constant radius fillet requires at least one solid edge, face, or body";        /* sld 2029 */
    if (fl_p.radius < EX_TOL) e[FLV_RADIUS] = "Radius must be greater than 0";                                      /* sld 2030 (FUN_180217130: < tol) */
}
static int fl_has_error(void) { const char *e[FLV_N]; fl_validate(e); for (int i = 0; i < FLV_N; i++) if (e[i]) return 1; return 0; }

/* the features of the selection: one per body (FUN_180096570's per-body structs, walked from the last; bodies in first-seen order: the
   picked bodies, then the faces', then the edges') */
static int fl_make_feats(ExFeat **out) {
    int order[1024], no = 0;
    for (int t = 0; t < 3; t++) for (int i = 0; i < fl_npk[t]; i++) { int b = fl_pk[t][i].body, s = 0; for (int j = 0; j < no; j++) if (order[j] == b) s = 1; if (!s && no < 1024) order[no++] = b; }
    ExFeat *fs = calloc(no + 1, sizeof(ExFeat)); int nf = 0;
    for (int j = no - 1; j >= 0; j--) {
        int b = order[j]; FilFeat q; memset(&q, 0, sizeof q);
        q.radius = fl_p.radius; q.overflow = fl_p.overflow; q.mitered = fl_p.mitered; q.propagate = fl_p.propagate;
        for (int i = 0; i < fl_npk[0]; i++) if (fl_pk[0][i].body == b) q.whole = 1;
        int cf = 0, ce = 0; for (int i = 0; i < fl_npk[1]; i++) cf += fl_pk[1][i].body == b; for (int i = 0; i < fl_npk[2]; i++) ce += fl_pk[2][i].body == b;
        q.frec = calloc(cf + 1, sizeof(int)); q.fpt = calloc(cf + 1, sizeof *q.fpt); q.erec = calloc(ce + 1, sizeof(int)); q.ept = calloc(ce + 1, sizeof *q.ept);
        for (int i = 0; i < fl_npk[1]; i++) if (fl_pk[1][i].body == b) { q.frec[q.nf] = fl_pk[1][i].rec; memcpy(q.fpt[q.nf], fl_pk[1][i].pt, sizeof q.fpt[0]); q.nf++; }
        for (int i = 0; i < fl_npk[2]; i++) if (fl_pk[2][i].body == b) { q.erec[q.ne] = fl_pk[2][i].rec; memcpy(q.ept[q.ne], fl_pk[2][i].pt, sizeof q.ept[0]); q.ne++; }
        ExFeat *f = &fs[nf++]; memset(f, 0, sizeof *f);
        f->p.type = EX_CUT; f->p.target = b; snprintf(f->name, sizeof f->name, "%s", fl_name);
        f->fl = filfeat_dup(&q); free(q.frec); free(q.fpt); free(q.erec); free(q.ept);
        if (fl_edit_idx >= 0 && fl_edit_idx < nfeats) { f->level = feats[fl_edit_idx].level; f->color = feats[fl_edit_idx].color; }
        else { f->level = geo_level; f->color = cfg_solid_color; }
    }
    *out = fs; return nf;
}

/* the preview (sec. 1.3 / 4): sld_fillet(preview = true) on every change while Automatically preview is on, and the Preview button; its
   errors go to the warning (FUN_180156d90 / FUN_180156bc0), not to a box */
void fl_preview(void) {
    if (!fl_mode || fl_phase != 1) return;
    unsigned long h = 1469598103u;
    const unsigned char *q = (const unsigned char *)&fl_p; for (size_t i = 0; i < sizeof fl_p; i++) h = (h ^ q[i]) * 16777619u;
    for (int t = 0; t < 3; t++) for (int i = 0; i < fl_npk[t]; i++) h = (h ^ (unsigned)(fl_pk[t][i].body * 7919 + fl_pk[t][i].idx * 31 + t)) * 16777619u;
    h = (h ^ (unsigned)fl_prev) * 16777619u; h = (h ^ (unsigned)(fl_edit_idx + 2)) * 16777619u;
    int changed = h != fl_prev_hash; fl_prev_hash = h;
    if (!fl_prev_req && !(fl_prev && changed)) return;
    fl_prev_req = 0; fl_warn[0] = 0;
    ExFeat *fs = NULL; int nf = fl_make_feats(&fs);
    Body *tmp = NULL; int nt = 0; ExErr e;
    if (fl_edit_idx >= 0) {
        for (int i = 0; i < nfeats; i++) {
            if (i == fl_edit_idx) { if (nf && !feat_apply(&tmp, &nt, &fs[0], &e)) ex_err_text(&e, fl_warn, sizeof fl_warn); }
            else feat_apply(&tmp, &nt, &feats[i], &e);
        }
    } else {
        tmp = bodies_dup(&nt);
        for (int i = 0; i < nf; i++) if (!feat_apply(&tmp, &nt, &fs[i], &e)) ex_err_text(&e, fl_warn, sizeof fl_warn);
    }
    for (int i = 0; i < nf; i++) feat_free(&fs[i]);
    free(fs);
    if (fl_warn[0]) ex_log("fillet: preview warning: %s", fl_warn);
    bodies_show(tmp, nt); bodies_free(&tmp, &nt);
}
void fl_panel_frame(void) { if (fl_mode && fl_phase == 1) fl_preview(); }

/* end (slot 5 FUN_1801bc4a0) */
void fl_end(int accepted, int restart) {
    ex_log("fillet: end, accepted %d restart %d", accepted, restart);
    if (accepted) fl_sess = fl_p;                                 /* P -> DAT_1803e9660 */
    ex_autoprev = fl_prev;                                        /* the Preview flag -> DAT_1803bac10 (also on Cancel) */
    fl_mode = 0; fl_phase = 0; fl_adding = 0; fl_edit_idx = -1; fl_clear_sel(); fl_warn[0] = 0;
    cam_clear_selection(); bodies_show(sbodies, nsbodies); cam_msg[0] = 0; overlay_dirty = 1;
    if (accepted && restart) fl_start();                          /* OK-New: post_FT_command("sld_do_fillet") */
}
void fl_cancel(void) { fl_end(0, 0); }
/* OK (0x2bd) / OK-New (0x2c0): new op -> sld_fillet(final) per body; each body its own op, a failing body does not stop the others; the loop
   ends accepted either way (1.3 / 7). Edit: the op replaced and the history replayed (OK with nothing changed: no rebuild). */
void fl_accept(int apply) {
    if (!fl_mode || fl_phase != 1) return;
    if (fl_has_error()) return;                                   /* the FunctionPanel's error state (the buttons are disabled) */
    if (!fl_p.propagate) { cui_message("Error Processing Solid", "%s", FL_REFUSE); return; }   /* SOL-F1: not built, the panel stays */
    ExFeat *fs = NULL; int nf = fl_make_feats(&fs);
    if (fl_edit_idx >= 0) {
        const FilFeat *o = feats[fl_edit_idx].fl;
        int same = o && fabs(o->radius - fl_p.radius) < EX_TOL && o->overflow == fl_p.overflow && o->mitered == fl_p.mitered && o->propagate == fl_p.propagate;   /* FUN_1801290f0 */
        if (same && !fl_sel_changed) { snprintf(feats[fl_edit_idx].name, sizeof feats[fl_edit_idx].name, "%s", fl_name); for (int i = 0; i < nf; i++) feat_free(&fs[i]); free(fs); fl_end(1, 0); return; }
        if (nf != 1) { for (int i = 0; i < nf; i++) feat_free(&fs[i]); free(fs); return; }   /* the pick is limited to the op's body */
        ExFeat old = feats[fl_edit_idx]; feats[fl_edit_idx] = fs[0];
        Body *tmp = NULL; int nt = 0; ExErr e; int ok = 1;
        for (int i = 0; i < nfeats && ok; i++) if (!feat_apply(&tmp, &nt, &feats[i], &e)) ok = 0;
        bodies_free(&tmp, &nt);
        if (!ok) { feats[fl_edit_idx] = old; feat_free(&fs[0]); free(fs); ex_report(&e); return; }   /* the edit stays open */
        FeatPack *k = malloc(sizeof *k); k->n = 1; k->f = malloc(sizeof(ExFeat)); k->f[0] = old;
        URec r; memset(&r, 0, sizeof r); r.kind = U_FEAT; r.feat = k; r.idx = 2; r.nd = fl_edit_idx; undo_push(r);
        free(fs); doc_dirty = 1; solids_rebuild(); fl_end(1, apply); return;
    }
    int nt; Body *tmp = bodies_dup(&nt); ExErr e;
    ExFeat *good = calloc(nf + 1, sizeof(ExFeat)); int ng = 0;
    for (int i = 0; i < nf; i++) {
        if (feat_apply(&tmp, &nt, &fs[i], &e)) good[ng++] = fs[i];
        else { ex_report(&e); feat_free(&fs[i]); }               /* the failed body's op is deleted, its mark restored (sec. 7) */
    }
    bodies_free(&tmp, &nt); free(fs);
    if (ng) {
        for (int i = 0; i < ng; i++) feat_push(&good[i]);
        FeatPack *k = malloc(sizeof *k); k->n = ng; k->f = good;
        URec r; memset(&r, 0, sizeof r); r.kind = U_FEAT; r.feat = k; r.idx = 0; undo_push(r);
        doc_dirty = 1; solids_rebuild();
    } else free(good);
    fl_end(1, apply);
}
void fl_set_radius(double r) { fl_p.radius = r; }
void fl_set_overflow(int o) { if (o >= 0 && o <= 2) fl_p.overflow = o; }
void fl_set_check(int which, int on) { if (which == 0) fl_p.propagate = on ? 1 : 0; else if (which == 1) fl_p.mitered = on ? 1 : 0; else if (which == 2) fl_prev = on ? 1 : 0; }

/* the selection highlight (ps_hilight_edges on the picked entities; the select colour, ours: yellow) */
void fl_ov(float **pv, int *pn, int *pcap) {
    if (!fl_mode) return;
    int nt; Body *bs = pick_bodies(&nt);
    for (int t = 0; t < 3; t++) for (int i = 0; i < fl_npk[t]; i++) {
        const FlPick *p = &fl_pk[t][i]; if (p->body < 0 || p->body >= nt || !bs[p->body].occ) continue;
        float *l = NULL; int n;
        if (t == 0) n = ocs_edges(bs[p->body].occ, 0, &l); else n = ocs_sub_lines(bs[p->body].occ, t == 1 ? 0 : 1, p->idx, 0, &l);
        float g = fl_lsel[t][i] ? 0.5f : 1.0f;
        for (int k = 0; k < n; k++) { V3 a = { l[6 * k], l[6 * k + 1], l[6 * k + 2] }, b = { l[6 * k + 3], l[6 * k + 4], l[6 * k + 5] };
            ov_pt(pv, pn, pcap, a, 1, g, 0); ov_pt(pv, pn, pcap, b, 1, g, 0); }
        free(l);
    }
}

/* MCAMUI_EXDUMP / the fl dump step (tests): the command's state */
void fl_dump(void) {
    printf("    fl mode %d phase %d edit %d adding %d name '%s' radius %.6g overflow %d mitered %d propagate %d prev %d selbtn %d%d%d picks %d/%d/%d warn '%s'\n",
           fl_mode, fl_phase, fl_edit_idx, fl_adding, fl_name, fl_p.radius, fl_p.overflow, fl_p.mitered, fl_p.propagate, fl_prev,
           fl_selbtn[0], fl_selbtn[1], fl_selbtn[2], fl_npk[0], fl_npk[1], fl_npk[2], fl_warn);
    for (int i = 0; i < fl_list_n(); i++) { char b[32]; int t, k; fl_list_item(i, b, sizeof b, &t, &k);
        const FlPick *p = &fl_pk[t][k]; printf("      %s body %d idx %d rec %d sel %d\n", b, p->body, p->idx, p->rec, fl_lsel[t][k]); }
    const char *e[FLV_N]; fl_validate(e); for (int i = 0; fl_mode && i < FLV_N; i++) if (e[i]) printf("      error: %s\n", e[i]);
    printf("    fl session radius %.6g overflow %d mitered %d propagate %d\n", fl_sess.radius, fl_sess.overflow, fl_sess.mitered, fl_sess.propagate);
}

/* ---- the MCAMUI_SCRIPT steps (both apps; ours, for tests): fillet (start), flselbtn:B,F,E (the Solid Selection buttons), flpickat:K,X,Y,Z[,S]
   (a pick of kind K - 0 body / 1 face / 2 edge - nearest by bbox centre to the world point; S = Shift), fldone (End Selection), fledit:N,
   flset:KEY=V (radius / overflow / mitered / propagate / preview / name / lsel=N / remove / add / reselect / unselectall / prevbtn / ok /
   apply / cancel / esc), fldump */
static void fl_pick_at(int kind, const double w[3], int shift) {
    int nt; Body *bs = pick_bodies(&nt);
    int bb = -1, bi = -1; double bd = 1e300;
    for (int i = 0; i < nt; i++) {
        if (!bs[i].occ) continue;
        if (fl_edit_idx >= 0 && i != feats[fl_edit_idx].p.target) continue;
        if (kind == 0) { double lo[3], hi[3]; if (!ocs_bbox(bs[i].occ, lo, hi)) continue; double d = 0;
            for (int k = 0; k < 3; k++) { double c = (lo[k] + hi[k]) / 2; d += (c - w[k]) * (c - w[k]); } if (d < bd) { bd = d; bb = i; } continue; }
        int n = kind == 1 ? ocs_nfaces(bs[i].occ) : ocs_nedges(bs[i].occ);
        for (int j = 0; j < n; j++) { double c[3]; if (!ocs_sub_center(bs[i].occ, kind == 1 ? 0 : 1, j, c)) continue;
            int ff[2]; if (kind == 2 && ocs_edge_faces(bs[i].occ, j, ff) < 2) continue;   /* OCCT's seams are no Parasolid edges */
            double d = (c[0] - w[0]) * (c[0] - w[0]) + (c[1] - w[1]) * (c[1] - w[1]) + (c[2] - w[2]) * (c[2] - w[2]);
            if (d < bd) { bd = d; bb = i; bi = j; } }
    }
    if (bb < 0) { if (F.nsolids > 0) fl_part_solid_msg(); return; }   /* only part solids there (the script's pick: by the part having them) */
    if (shift && kind != 0) {
        int *v = NULL, m = kind == 2 ? ocs_tangent_edges(bs[bb].occ, bi, &v) : ocs_tangent_faces(bs[bb].occ, bi, &v);
        int on = find_pick(kind, bb, bi) >= 0;
        for (int i = 0; i < m; i++) { int j = find_pick(kind, bb, v[i]); if (on && j >= 0) del_pick(kind, j); else if (!on && j < 0) add_pick(kind, bs, bb, v[i]); }
        free(v);
    } else { int j = find_pick(kind, bb, bi); if (j >= 0) del_pick(kind, j); else add_pick(kind, bs, bb, bi); }
    overlay_dirty = 1;
}
int cui_fillet_script(const char *st, const char *arg) {
    const char *mt, *mx;
    if (!strcmp(st, "fillet")) { fl_start(); fl_dump(); return 1; }
    if (!strcmp(st, "fldump")) { fl_dump(); return 1; }
    if (!strcmp(st, "fldone")) { fl_sel_done(); fl_dump(); return 1; }
    if (!strcmp(st, "fledit") && arg) { fl_edit(atoi(arg)); fl_dump(); return 1; }
    if (!strcmp(st, "flselbtn") && arg) { sscanf(arg, "%d,%d,%d", &fl_selbtn[0], &fl_selbtn[1], &fl_selbtn[2]); return 1; }
    if (!strcmp(st, "flpickat") && arg) {
        int k = 0, s = 0; double w[3] = { 0, 0, 0 };
        if (sscanf(arg, "%d,%lf,%lf,%lf,%d", &k, &w[0], &w[1], &w[2], &s) >= 4 && fl_mode && fl_phase == 0) fl_pick_at(k, w, s);
        int mo = cui_hooks.msg_open && cui_hooks.msg_open(&mt, &mx);
        printf("    flpickat %s: picks %d/%d/%d%s%s\n", arg, fl_npk[0], fl_npk[1], fl_npk[2], mo ? " message box: " : "", mo ? mx : "");
        if (mo && cui_hooks.msg_close) cui_hooks.msg_close();
        return 1;
    }
    if (!strcmp(st, "flset") && arg) {
        char k[32] = ""; const char *eq = strchr(arg, '='); double x = eq ? atof(eq + 1) : 0;
        snprintf(k, sizeof k, "%.*s", eq ? (int)(eq - arg) : (int)strlen(arg), arg);
        if (!strcmp(k, "radius")) fl_set_radius(x); else if (!strcmp(k, "overflow")) fl_set_overflow((int)x);
        else if (!strcmp(k, "propagate")) fl_set_check(0, (int)x); else if (!strcmp(k, "mitered")) fl_set_check(1, (int)x);
        else if (!strcmp(k, "preview")) fl_set_check(2, (int)x); else if (!strcmp(k, "name") && eq) snprintf(fl_name, sizeof fl_name, "%s", eq + 1);
        else if (!strcmp(k, "lsel")) fl_list_select((int)x, 1); else if (!strcmp(k, "remove")) fl_remove_sel();
        else if (!strcmp(k, "add")) fl_add_sel(0); else if (!strcmp(k, "reselect")) fl_add_sel(1);
        else if (!strcmp(k, "unselectall")) fl_unselect_all();
        else if (!strcmp(k, "prevbtn")) { fl_prev_req = 1; fl_preview(); }
        else if (!strcmp(k, "ok")) fl_accept(0); else if (!strcmp(k, "apply")) fl_accept(1); else if (!strcmp(k, "cancel")) fl_cancel();
        else if (!strcmp(k, "esc")) fl_escape();
        if (fl_mode && fl_phase == 1 && strcmp(k, "ok") && strcmp(k, "apply")) fl_preview();
        int mo = cui_hooks.msg_open && cui_hooks.msg_open(&mt, &mx);
        printf("    flset %s%s%s%s%s\n", arg, mo ? ": message box '" : "", mo ? mt : "", mo ? "': " : "", mo ? mx : "");
        if (mo && cui_hooks.msg_close) cui_hooks.msg_close();
        fl_dump();
        for (int i = 0; i < nsbodies; i++) printf("    body %d %s: %d face(s) %d edge(s), volume %.4f\n", i, sbodies[i].name, sbodies[i].occ ? ocs_nfaces(sbodies[i].occ) : 0,
                                                  sbodies[i].occ ? ocs_nedges(sbodies[i].occ) : 0, body_volume(&sbodies[i]));
        return 1;
    }
    return 0;
}
