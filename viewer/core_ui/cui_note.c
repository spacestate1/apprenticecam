/* cui_note.c - notes (E_NOTE, formats/drafting/notes.md) in the general selection: the pick of a note under the cursor (mastercam.dll
 * select_ent_univ's N_ID / W_ID case = viewer/draft_pick.c dp_distance), its strokes for the selection / auto-highlight draw. The
 * Drafting > Note command is cui_draft_note.c. "ours" marks glue. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "draft.h"
#include "draft_pick.h"
#include "draft_note.h"
#include "mcam_saveops.h"

const DraftFonts *cui_draft_fonts(void);                     /* cui_draft_pick.c */

/* select_ent_univ / FUN_18020f4e0 (decomp/draft/ord_mc_seldim.c 576-660): an N_ID / W_ID entity in the graphics view (or every view with
   iso_view_dimensions, config 15007) gets the distance of its text strokes and leader chain from the cursor in proj_iso_pt's 2-D
   (draft_pick.c dp_distance); here the nearest live note within the aperture, its distance in screen px (*DPX; pick_entity compares it
   with the wireframe's). -1 = none */
static int note_iso_cfg[2] = { -1, -1 };                     /* config 15007 iso_view_dimensions of mcamx.config / mcamxm.config (-1: not read) */
void note_pick_config_changed(void) { note_iso_cfg[0] = note_iso_cfg[1] = -1; }
int note_pick(float mx, float my, double *dpx, double wpt[3]) {
    if (!loaded || !F.nnotes) return -1;
    double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, mx, my - vp_y, w);   /* cui_cursor_world's point (any point under the cursor: proj_iso_pt
                                                                       keeps only the view's x / y) */
    DraftDoc *d = mcam_notes_doc(&F, -1);
    DpView V; memcpy(V.gm, vmat, sizeof V.gm); V.gview = view_number(vmat);
    {   /* startup value 1 (scene.c); read once per units (ours: the pick runs at every hover - the configuration file is not re-read each time;
           note_pick_config_changed drops the cached values when the configuration is read again) */
        int m = mcam_is_metric(&F) ? 1 : 0;
        if (note_iso_cfg[m] < 0) { char cv[64]; mcam_config_value(m, 15007, cv, sizeof cv); note_iso_cfg[m] = !(cv[0] == 'n' || cv[0] == 'N'); }
        V.iso = note_iso_cfg[m];
    }
    double C[2] = { vmat[0] * w[0] + vmat[1] * w[1] + vmat[2] * w[2], vmat[3] * w[0] + vmat[4] * w[1] + vmat[5] * w[2] };
    double vh = scene_view_height(), lim = SEL_APERTURE_PX * vh / (vp_h > 0 ? vp_h : 1);
    int best = -1; double bd = 1e38;
    for (int k = 0; k < F.nnotes; k++) {
        int e = F.notes[k].ent; if (e < 0 || e >= F.nents || F.ents[e].type != E_NOTE || F.ents[e].blanked || !ENT_LV_SHOWN(&F.ents[e])) continue;   /* + level (ENT_LV_SHOWN) */
        const uint8_t *E = F.notes[k].E; int view = (int16_t)(E[0x88] | E[0x89] << 8);
        if (!V.iso && (view ? view : 1) != V.gview) continue;
        DpRec r = { E, 0x1c8, d }; double dd;
        if (dp_distance(&r, cui_draft_fonts(), &V, C, lim, &dd) && dd < bd && dd < lim) { bd = dd; best = e; }
    }
    draft_free(d);
    if (best >= 0 && dpx) *dpx = bd * (vp_h > 0 ? vp_h : 1) / (vh > 0 ? vh : 1);
    if (best >= 0 && wpt) {                                   /* view_to_world of the anchor (the depth pick_entity compares with a shaded surface) */
        const uint8_t *E = F.notes[F.ents[best].nt].E; int view = (int16_t)(E[0x88] | E[0x89] << 8); double m[9], v[3];
        memcpy(v, E + 0x70, 24);
        if (!mcam_std_view(view ? view : 1, m)) { wpt[0] = v[0]; wpt[1] = v[1]; wpt[2] = v[2]; }
        else for (int i = 0; i < 3; i++) wpt[i] = m[i] * v[0] + m[3 + i] * v[1] + m[6 + i] * v[2];
    }
    return best;
}

/* the strokes of note entity E as drawn (draft_render of the note alone: text, leaders, arrowheads) as world segments in scene_surf_wire's
   layout (12 floats each: x y z, 3 unused, x2 y2 z2, 3 unused);
   the count of segments, *OUT malloc'd (ours: the selection / auto-highlight draw of a drafting entity, cui_overlay.c) */
typedef struct { float *v; int n, cap; } SegAcc;
static void seg_poly(void *ctx, const double *p, int n, int closed, int fill, int color, int view) {
    SegAcc *A = ctx; (void)fill; (void)color; (void)view;
    for (int i = 0; i + 1 < n + (closed && n > 2); i++) {
        const double *a = p + 3 * i, *b = p + 3 * ((i + 1) % n);
        if (A->n == A->cap) { A->cap = A->cap ? 2 * A->cap : 256; A->v = realloc(A->v, sizeof(float) * 12 * (size_t)A->cap); }
        float *q = A->v + 12 * (size_t)A->n++; memset(q, 0, sizeof(float) * 12); for (int k = 0; k < 3; k++) { q[k] = (float)a[k]; q[6 + k] = (float)b[k]; }
    }
}
int note_segs(int e, float **out) {
    *out = NULL;
    if (e < 0 || e >= F.nents || F.ents[e].type != E_NOTE) return 0;
    DraftDoc *d = mcam_notes_doc(&F, F.ents[e].nt);
    SegAcc A = { 0 }; DraftOut o = { &A, seg_poly }; DraftStats st;
    draft_render(d, cui_draft_fonts(), &o, &st);
    draft_free(d);
    *out = A.v; return A.n;
}
