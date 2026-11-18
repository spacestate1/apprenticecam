/* cui_undo.c - undo / redo (Ctrl+Z / Ctrl+Y; keymap UndoEvent / RedoEvent): the URec records of geometry added /
   deleted / edited, operations added / deleted / edited (the hooks for the op records: camops in cui_doc.c) and solid
   features (feat_undo in cui_feat.c). Moved from viewer/mcamui.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "core_ui.h"

/* ------------------------------------------------------------------------------------------------ undo / edit */
/* Undo (Ctrl+Z) / Redo (Ctrl+Y): geometry drawn / deleted / pasted / cut, operations added / deleted / edited. */
void urec_free(URec *r) { free(r->ents); free(r->types); free(r->dents); free(r->dtypes); free(r->pairs); r->pairs = NULL; free(r->bents); r->bents = NULL; r->nb = 0; r->ents = r->types = r->dents = r->dtypes = NULL; feat_release(r->feat); r->feat = NULL; srf_edits_free(r->sx); r->sx = NULL; attr_undo_free(r->ax); r->ax = NULL; }
/* undo depth: config 16013 "Limit the number of Undo events?" (n = unlimited) / 16014 "Number of events?" */
URec *undo_st, *redo_st; int nundo, nredo, cap_undo, cap_redo, undo_limit = 0, undo_lim_on, undo_lim_n = 100;
void ust_grow(URec **st, int *cap, int n) { if (n < *cap) return; *cap = *cap ? *cap * 2 : 64; *st = realloc(*st, sizeof(URec) * *cap); }
int *poly_stash; int npoly_stash;                    /* poly counts of entities switched off */
unsigned char *ent_hidden; int nent_hidden, any_hidden;

static void stash_grow(void) { if (npoly_stash < F.npolys) { poly_stash = realloc(poly_stash, sizeof(int) * F.npolys); for (int i = npoly_stash; i < F.npolys; i++) poly_stash[i] = -1; npoly_stash = F.npolys; } }
void ent_off(int i) {                                         /* delete (type 0) keeping what is needed to restore */
    Ent *en = &F.ents[i]; stash_grow();
    for (int k = 0; k < en->npoly; k++) { int pi = en->first_poly + k; if (pi < F.npolys) { if (F.polys[pi].count) poly_stash[pi] = F.polys[pi].count; F.polys[pi].count = 0; } }
    if (en->type == E_SURF && en->srf >= 0 && en->srf < F.nsurfs) { F.surfs[en->srf].off = 1; scene_surfs_update(0); }   /* not drawn / picked */
    if (en->type == E_MESH && en->msh >= 0 && en->msh < F.nmeshes) { F.meshes[en->msh].off = 1; scene_meshes_changed(); }
    en->type = 0;
}
void ent_on(int i, int type) {
    Ent *en = &F.ents[i]; stash_grow();
    for (int k = 0; k < en->npoly; k++) { int pi = en->first_poly + k; if (pi < F.npolys && poly_stash[pi] > 0) F.polys[pi].count = poly_stash[pi]; }
    if (type == E_SURF && en->srf >= 0 && en->srf < F.nsurfs) { F.surfs[en->srf].off = 0; scene_surfs_update(0); }
    if (type == E_MESH && en->msh >= 0 && en->msh < F.nmeshes) { F.meshes[en->msh].off = 0; scene_meshes_changed(); }
    en->type = type;
}
void undo_clear(void) {
    for (int i = 0; i < nundo; i++) urec_free(&undo_st[i]);
    for (int i = 0; i < nredo; i++) urec_free(&redo_st[i]);
    nundo = nredo = 0;
}
void undo_reset(void) {                          /* a different document: history and stashes no longer apply */
    undo_clear(); free(poly_stash); poly_stash = NULL; npoly_stash = 0; any_hidden = 0; free(ent_hidden); ent_hidden = NULL; nent_hidden = 0;
    free(ent_sel); ent_sel = NULL; ent_sel_n = 0; nselected = 0; hover_ent = -1;
}
void undo_push(URec r) {
    if (undo_limit > 0 && nundo >= undo_limit) { urec_free(&undo_st[0]); memmove(undo_st, undo_st + 1, sizeof(URec) * (nundo - 1)); nundo--; }
    ust_grow(&undo_st, &cap_undo, nundo); undo_st[nundo++] = r;
    for (int i = 0; i < nredo; i++) urec_free(&redo_st[i]);
    nredo = 0;
}
/* an edit: mod_begin, mod_del (switch an entity off), new entities, mod_end = one undo step */
int mod_from = -1; URec mod_rec;                           /* the edit being collected (mod_begin .. mod_end) */
void mod_begin(void) { if (mod_from >= 0) return; mod_from = F.nents; memset(&mod_rec, 0, sizeof mod_rec); mod_rec.kind = U_ENTS_MOD; }
void mod_del(int i) {
    if (i < 0 || i >= F.nents || !F.ents[i].type) return;
    mod_rec.dents = realloc(mod_rec.dents, sizeof(int) * (mod_rec.nd + 1)); mod_rec.dtypes = realloc(mod_rec.dtypes, sizeof(int) * (mod_rec.nd + 1));
    mod_rec.dents[mod_rec.nd] = i; mod_rec.dtypes[mod_rec.nd++] = F.ents[i].type;
    ent_off(i); if (ent_sel && i < ent_sel_n && ent_sel[i]) { ent_sel[i] = 0; nselected--; }
}
/* Break Many's Blank (sketcher_trim_3 FUN_1800af8d0: BlankEptr + put_special_undo(1)): the original stays, blanked */
void mod_blank(int i) {
    if (i < 0 || i >= F.nents || !F.ents[i].type || F.ents[i].blanked) return;
    mod_rec.bents = realloc(mod_rec.bents, sizeof(int) * (mod_rec.nb + 1)); mod_rec.bents[mod_rec.nb++] = i;
    mcam_ent_blank(&F, i, 1); F.ents[i].blank_edited = 1;
    if (ent_sel && i < ent_sel_n && ent_sel[i]) { ent_sel[i] = 0; nselected--; }
}
/* a Move (formats/align_geo.md 2.2, mccore xform_store_ent with the move flag): the moved entity is a new record that takes the
   original's id (xform_swap_idn) and the references (tp_change_eptr re-points the ops' TP records; ours: the CamOp chains), and
   every op using it gets op +0x94 = 1 (no warning) */
static int *mpair_o, *mpair_m, nmpair;
static void chains_repoint(int from, int to) {
    for (int j = 0; j < ncamops; j++) {
        CamOp *ops[2] = { camops[j], camops[j]->edit };
        for (int w = 0; w < 2; w++) if (ops[w]) for (int c = 0; c < ops[w]->nchains; c++)
            for (int k = 0; k < ops[w]->chains[c].n; k++) if (ops[w]->chains[c].ent[k] == from) ops[w]->chains[c].ent[k] = to;
    }
}
void mod_pair(int orig, int moved) {
    if (orig < 0 || moved < 0 || orig >= F.nents || moved >= F.nents) return;
    mpair_o = realloc(mpair_o, sizeof(int) * (size_t)(nmpair + 1)); mpair_m = realloc(mpair_m, sizeof(int) * (size_t)(nmpair + 1));
    mpair_o[nmpair] = orig; mpair_m[nmpair] = moved; nmpair++;
    F.ents[moved].idn = F.ents[orig].idn;                    /* xform_swap_idn: the moved entity keeps the original's id */
    chains_repoint(orig, moved);
    dirty_ent_users(moved);                                  /* tp_change_eptr(.., 1, 1): op +0x94 = 1, +0xf2 = 1, +0x97 = 0 */
}
/* the edit being collected is dropped: the chains mod_pair moved go back */
void mod_unpair(void) { for (int q = nmpair - 1; q >= 0; q--) chains_repoint(mpair_m[q], mpair_o[q]); nmpair = 0; }
void mod_end(void) {
    if (mod_from < 0) return;
    int na = F.nents - mod_from;
    if (na > 0) { mod_rec.ents = malloc(sizeof(int) * na); mod_rec.types = malloc(sizeof(int) * na); mod_rec.n = na;
                  for (int i = 0; i < na; i++) { mod_rec.ents[i] = mod_from + i; mod_rec.types[i] = F.ents[mod_from + i].type; } }
    if (nmpair && na > 0) {                                   /* the Move pairs, for undo / redo */
        mod_rec.pairs = malloc(sizeof(int) * na);
        for (int i = 0; i < na; i++) { mod_rec.pairs[i] = -1;
            for (int q = 0; q < nmpair; q++) if (mpair_m[q] == mod_rec.ents[i])
                { for (int d = 0; d < mod_rec.nd; d++) if (mod_rec.dents[d] == mpair_o[q]) mod_rec.pairs[i] = d;
                  for (int b = 0; b < mod_rec.nb; b++) if (mod_rec.bents[b] == mpair_o[q]) mod_rec.pairs[i] = mod_rec.nd + b; } }   /* an original blanked, not deleted (Convert to NURBS: delete_ent(.., blank, ..)) */
    }
    nmpair = 0;
    if (na > 0 || mod_rec.nd || mod_rec.nb) { undo_push(mod_rec); doc_dirty = 1; } else urec_free(&mod_rec);
    mod_from = -1; memset(&mod_rec, 0, sizeof mod_rec);
}
void undo_ents_added(int from) {                             /* entities [from, nents) were just created */
    if (F.nents <= from) return;
    URec r = { U_ENTS_ADD, malloc(sizeof(int) * (F.nents - from)), malloc(sizeof(int) * (F.nents - from)), F.nents - from, NULL, NULL, 0 };
    for (int i = from; i < F.nents; i++) { r.ents[i - from] = i; r.types[i - from] = F.ents[i].type; }
    undo_push(r);
}

/* the operations made here, detached from / re-attached to the model in camops order */
void camops_detach(void) {
    int idx[256], n = 0;
    for (int j = 0; j < ncamops; j++) idx[n++] = camops[j]->op_index;
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (idx[b] > idx[a]) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
    for (int a = 0; a < n; a++) mcam_remove_op(&F, idx[a]);
}
void camops_attach(void) {          /* back where they were (op_index kept across detach), lowest first */
    int ord[MAX_CAMOPS], n = 0;
    for (int j = 0; j < ncamops; j++) if (camops[j]->op_index >= 0) ord[n++] = j;
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++)
        if (camops[ord[b]]->op_index < camops[ord[a]]->op_index) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
    char *placed = xcalloc(ncamops ? ncamops : 1, 1);
    for (int a = 0; a < n; a++) {
        CamOp *c = camops[ord[a]]; int at = c->op_index;
        int k = mcam_add_op(&F, cam_type_label(c->p.type), c->name, c->p.tool_num, c->m, c->nm);
        if (at > k) at = k;
        mcam_move_op(&F, k, at); c->op_index = at; placed[ord[a]] = 1;
    }
    for (int j = 0; j < ncamops; j++) if (!placed[j])        /* new ones: at the end */
        camops[j]->op_index = mcam_add_op(&F, cam_type_label(camops[j]->p.type), camops[j]->name, camops[j]->p.tool_num, camops[j]->m, camops[j]->nm);
    free(placed);
}

void undo_apply(URec *r, int undo) {
    switch (r->kind) {
    case U_ENTS_ADD: case U_ENTS_DEL: {
        int off = (r->kind == U_ENTS_ADD) == undo;
        for (int i = 0; i < r->n; i++) { if (off) ent_off(r->ents[i]); else ent_on(r->ents[i], r->types[i]); }
        break; }
    case U_ENTS_MOD:
        for (int i = 0; i < r->n; i++) { if (undo) ent_off(r->ents[i]); else if (r->types[i]) ent_on(r->ents[i], r->types[i]); }   /* type 0: made and deleted inside the edit (the nesting Results Delete) - stays off */
        for (int i = 0; i < r->nd; i++) { if (undo) ent_on(r->dents[i], r->dtypes[i]); else ent_off(r->dents[i]); }
        for (int i = 0; i < r->nb; i++) mcam_ent_blank(&F, r->bents[i], !undo);   /* mod_blank */
        if (r->sx) srf_edits_apply(r->sx, undo);          /* surface records rewritten in the same edit (Untrim's base: cui_srfedit.c) */
        for (int i = 0; r->pairs && i < r->n; i++) if (r->pairs[i] >= 0) {   /* a Move: the chains follow (validate_toolpath re-marks dirty) */
            int m = r->ents[i], o = r->pairs[i] < r->nd ? r->dents[r->pairs[i]] : r->bents[r->pairs[i] - r->nd];
            if (undo) chains_repoint(m, o); else chains_repoint(o, m);
            dirty_ent_users(undo ? o : m);
        }
        break;
    case U_FEAT: feat_undo(r, undo); break;
    case U_DRAFT: dr_undo(r->idx, undo); break;          /* a drafting entity made here (cui_draft.c) */
    case U_DRAFTEDIT: dr_edit_undo(r->idx, undo); break;   /* drafting rewritten here (cui_draft_pick.c) */
    case U_SURFEDIT: srf_edits_apply(r->sx, undo); break;  /* surface records rewritten here (cui_surf.c) */
    case U_ATTR: attr_undo_apply(r->ax, undo); break;      /* attributes changed in place (cui_attr.c) */
    case U_OP_ADD: case U_OP_DEL: {
        int remove = (r->kind == U_OP_ADD) == undo;
        camops_detach();
        if (remove) { for (int j = 0; j < ncamops; j++) if (camops[j] == r->op) { r->idx = j; memmove(camops + j, camops + j + 1, (ncamops - j - 1) * sizeof camops[0]); ncamops--; break; } }
        else if (ncamops < MAX_CAMOPS) { int j = r->idx > ncamops ? ncamops : r->idx; memmove(camops + j + 1, camops + j, (ncamops - j) * sizeof camops[0]); camops[j] = r->op; ncamops++; }
        camops_attach();
        break; }
    case U_OP_EDIT: {
        camops_detach();
        for (int j = 0; j < ncamops; j++) if (camops[j] == (undo ? r->op2 : r->op)) {
            CamOp *to = undo ? r->op : r->op2; to->op_index = camops[j]->op_index; camops[j] = to; }   /* same tree position */
        camops_attach();
        break; }
    }
    srf_tp_subst_sync();                                 /* tp_substitute_ent's re-pointed op references follow the entities (cui_srfedit.c) */
    sel = -1; msel_owner = -9; sim = -1; doc_dirty = 1; scene_reupload(); overlay_dirty = 1;
    if (cui_hooks.ops_changed) cui_hooks.ops_changed();
}
void do_undo(void) {
    if (!nundo) { cam_msg[0] = 0; return; }
    URec r = undo_st[--nundo]; undo_apply(&r, 1); ust_grow(&redo_st, &cap_redo, nredo); redo_st[nredo++] = r;
    cam_msg[0] = 0;
}
void do_redo(void) {
    if (!nredo) { cam_msg[0] = 0; return; }
    URec r = redo_st[--nredo]; undo_apply(&r, 0); ust_grow(&undo_st, &cap_undo, nundo); undo_st[nundo++] = r;
    cam_msg[0] = 0;
}
