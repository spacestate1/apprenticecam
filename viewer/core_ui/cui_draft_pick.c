/* cui_draft_pick.c - the drafting entity under the mouse for the Drafting commands (core_ui accessor of viewer/draft_pick.c, Mastercam's
 * select_ent_univ drafting cases - formats/drafting/create.md 9). The candidates in display-list order: the open part's drafting records
 * (file order; a record the session edited is taken as edited, cui_draft.c F.dmod), then the drafting entities made in this session
 * (McamFile.dnew). The cursor: the mouse's world point on the construction plane in the graphics view's 2-D (proj_iso_pt: vmat rows);
 * the aperture: sel_dist (SEL_APERTURE_PX of the view height, cui_snap.c); the graphics view (view_number) and config 15007
 * (iso_view_dimensions, as scene.c draft_build). "ours" marks glue. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "core_ui.h"
#include "draft_pick.h"
#include "scene.h"
#include "mcam_saveops.h"

static int32_t I32_(const uint8_t *b, size_t o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static DraftFonts *pk_fonts; static int pk_fonts_tried;
const DraftFonts *cui_draft_fonts(void) {                /* the stroke fonts as scene.c loads them (paths.conf "fonts", else <config>/fonts) */
    if (!pk_fonts_tried) { pk_fonts_tried = 1; char d[1100]; const char *k = site_path("fonts"); pk_fonts = draft_fonts_load(k[0] ? k : config_file(d, sizeof d, "fonts")); }
    return pk_fonts;
}
static DraftDoc *pk_doc; static char pk_path[1100];      /* ours: the open part's drafting, read once per part */
const DraftDoc *cui_draft_doc(void) {
    if (!loaded || !F.path[0]) { draft_free(pk_doc); pk_doc = NULL; pk_path[0] = 0; return NULL; }
    if (!pk_doc || strcmp(pk_path, F.path)) { draft_free(pk_doc); pk_doc = draft_load(F.path); snprintf(pk_path, sizeof pk_path, "%s", F.path); }
    return pk_doc;
}

int cui_draft_is_repl(int k);
int cui_draft_pick(const double w[3], int mask, CuiDraftHit *hit) {
    memset(hit, 0, sizeof *hit); hit->dnew = -1; hit->file = -1;
    const DraftDoc *dd = cui_draft_doc();
    int nf = dd ? draft_count(dd) : 0, n = 0;
    DpRec *R = malloc(sizeof *R * (size_t)(nf + F.ndnew + 1)); int *src = malloc(sizeof(int) * (size_t)(nf + F.ndnew + 1));
    for (int i = 0; i < nf; i++) {
        uint32_t len; const uint8_t *E = draft_record(dd, i, &len); if (!E) continue;
        const uint8_t *M = len >= 0x810 ? cui_draft_mod(E) : NULL;   /* edited here: the edited bytes */
        R[n] = (DpRec){ M ? M : E, len, dd }; src[n++] = i;
    }
    for (int i = 0; i < F.ndnew; i++) if (F.dnew[i].on && !cui_draft_is_repl(i)) { R[n] = (DpRec){ F.dnew[i].E, 0x810, NULL }; src[n++] = -2 - i; }
    DpView V; memcpy(V.gm, vmat, sizeof V.gm); V.gview = view_number(vmat);
    { char cv[64]; mcam_config_value(mcam_is_metric(&F), 15007, cv, sizeof cv); V.iso = !(cv[0] == 'n' || cv[0] == 'N'); }   /* startup value 1 (scene.c) */
    double C[2] = { vmat[0] * w[0] + vmat[1] * w[1] + vmat[2] * w[2], vmat[3] * w[0] + vmat[4] * w[1] + vmat[5] * w[2] };
    double lim = SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1), d = 0;
    int k = dp_pick(R, n, mask, cui_draft_fonts(), &V, C, lim, &d);
    if (k >= 0) { hit->E = R[k].E; hit->dist = d; if (src[k] <= -2) hit->dnew = -2 - src[k]; else hit->file = src[k]; }
    free(R); free(src);
    return k >= 0;
}

/* ------------------------------------------------------------------------------------------------ editing existing drafting (ours) */
/* Mastercam rewrites an existing drafting entity in place (write_ent, one UndoTrans: Align's oc, df_manage_oc(3)). Here: a session
   record (McamFile.dnew) is rewritten in place; a part record gets a session copy carrying the part record's eptr at +0x20 (the
   "replacement": drawn instead of the part's record - draft_set_skip -, picked instead of it, written over it by mcam_saveops.c);
   one transaction = one undo step (U_DRAFTEDIT: each record's other bytes swapped, a replacement made switched on / off) */
static int dnew_of_eptr(uint64_t ep) {                    /* the live replacement of the part record EP, -1 none */
    for (int i = F.ndnew - 1; ep && i >= 0; i--) if (F.dnew[i].on) { uint64_t m; memcpy(&m, F.dnew[i].E + 0x20, 8); if (m == ep) return i; }
    return -1;
}
const unsigned char *cui_draft_mod(const unsigned char *fileE) {
    uint64_t ep; memcpy(&ep, fileE + 0x20, 8); int k = dnew_of_eptr(ep);
    return k >= 0 ? F.dnew[k].E : NULL;
}
static uint64_t *hid; static int nhid;                     /* ours: part records drawn from a drag's copies (Align) */
int cui_draft_hide(const unsigned long long *ep, int n) {   /* 1 = the set changed (the part's drafting to be rebuilt) */
    if (n == nhid && (!n || !memcmp(hid, ep, sizeof *hid * (size_t)n))) return 0;
    free(hid); hid = NULL; nhid = 0; if (n > 0) { hid = malloc(sizeof *hid * (size_t)n); memcpy(hid, ep, sizeof *hid * (size_t)n); nhid = n; }
    return 1;
}
static int skip_rec(const uint8_t *E, uint32_t n) {
    if (n == 0x1c8 && (I32_(E, 0) == 0x10 || I32_(E, 0) == 0x20) && loaded && F.nnotes) return 1;   /* a note: drawn from F.notes (E_NOTE,
                                                                      mcam_load.c reads every N_ID / W_ID record of this size; scene.c draft_build) */
    if (n < 0x810 || I32_(E, 0) != 0x40) return 0;
    uint64_t ep; memcpy(&ep, E + 0x20, 8); for (int i = 0; i < nhid; i++) if (hid[i] == ep) return 1;
    return cui_draft_mod(E) != NULL;
}
__attribute__((constructor)) static void skip_install(void) { draft_set_skip(skip_rec); }   /* ours: from the start (a project's replacements) */
int cui_draft_is_repl(int k) { uint64_t m; memcpy(&m, F.dnew[k].E + 0x20, 8); return m != 0; }   /* a part record's replacement */
typedef struct { int idx, made; uint8_t E[0x810]; } EdRec;
typedef struct { EdRec *r; int n; } EdTx;
static EdTx *ed_tx; static int ned_tx;                    /* every transaction (U_DRAFTEDIT idx) */
static EdTx ed_cur; static int ed_open;
void cui_draft_edit_begin(void) { ed_cur.r = NULL; ed_cur.n = 0; ed_open = 1; draft_set_skip(skip_rec); }
int cui_draft_edit_rec(const CuiDraftHit *src, const unsigned char newE[0x810]) {   /* SRC: the record as picked (file / dnew) */
    if (!ed_open) return -1;
    int k = src->dnew;
    if (k < 0 && src->file >= 0) { uint64_t ep; memcpy(&ep, src->E + 0x20, 8); k = dnew_of_eptr(ep); }
    ed_cur.r = realloc(ed_cur.r, sizeof *ed_cur.r * (size_t)(ed_cur.n + 1)); EdRec *e = &ed_cur.r[ed_cur.n++];
    if (k >= 0) { e->idx = k; e->made = 0; memcpy(e->E, F.dnew[k].E, 0x810); uint64_t mk; memcpy(&mk, F.dnew[k].E + 0x20, 8);
                  memcpy(F.dnew[k].E, newE, 0x810); memcpy(F.dnew[k].E + 0x20, &mk, 8); }   /* (a replacement keeps its part record's eptr) */
    else {                                                   /* the first edit of a part record: its replacement */
        F.dnew = realloc(F.dnew, sizeof *F.dnew * (size_t)(F.ndnew + 1)); struct DraftNew *d = &F.dnew[F.ndnew];
        memcpy(d->E, newE, 0x810); memcpy(d->E + 0x20, src->E + 0x20, 8); d->ent[0] = d->ent[1] = -1; d->on = 1;
        k = F.ndnew++; e->idx = k; e->made = 1; memset(e->E, 0, 0x810);
    }
    return k;
}
void cui_draft_edit_commit(void) {
    if (!ed_open) return; ed_open = 0;
    if (!ed_cur.n) return;
    ed_tx = realloc(ed_tx, sizeof *ed_tx * (size_t)(ned_tx + 1)); ed_tx[ned_tx] = ed_cur;
    URec r; memset(&r, 0, sizeof r); r.kind = U_DRAFTEDIT; r.idx = ned_tx++; undo_push(r);
    doc_dirty = 1; scene_draft_rebuild();
}
void dr_edit_undo(int tx, int undo) {                     /* U_DRAFTEDIT (cui_undo.c): swap / switch, the last record first on undo */
    if (tx < 0 || tx >= ned_tx) return;
    EdTx *t = &ed_tx[tx];
    for (int j = 0; j < t->n; j++) {
        EdRec *e = &t->r[undo ? t->n - 1 - j : j];
        if (e->idx < 0 || e->idx >= F.ndnew) continue;
        if (e->made) F.dnew[e->idx].on = !undo;
        else { uint8_t tmp[0x810]; memcpy(tmp, F.dnew[e->idx].E, 0x810); memcpy(F.dnew[e->idx].E, e->E, 0x810); memcpy(e->E, tmp, 0x810); }
    }
    draft_set_skip(skip_rec); scene_draft_rebuild(); dr_scene();
}
