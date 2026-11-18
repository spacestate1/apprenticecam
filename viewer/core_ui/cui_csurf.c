/* cui_csurf.c - Wireframe > Curve Surface (mccad sketcher_curve_6 0x180007a50, decomp/mccad_sketch.c l.3-120). Spec: ../../formats/wireframe.md
 * "Curve Surface". No panel: select_ents(prompt mccadres 5967 "Select curves to convert to surface curves", mask 0x8e: lines, arcs,
 * parametric splines, NURBS curves); at End Selection set_clr_sel_bits(4, 0, 4, 1, 0x100) marks the displayed surfaces, UndoTransStart,
 * then per selected curve one chain (RecordBoundaries_Add_Chain + add_curve_to_chain(ent, 0, 1, 0, ..)) and
 * project_chains_onto_surfs(chain, 0, surf_step_dist, 1, surf_step_chord_dev, surf_step_chord_dev, 0, 0, 1, 0, &n, sp) (viewer/mccsurf.c
 * cs_project); n >= 1: the curve deleted (delete_ent(eptr, 1, 0, 0, .., 0)), else its selection bit cleared and redrawn;
 * UndoTransCommit; the surfaces' bit 4 cleared; the command returns (one select_ents, no loop).
 * Ours: the selection is the current one when Enter (End Selection) is given, as Convert to NURBS (cui_spline.c); entities outside the
 * mask are left alone. Refused with a message (the curve kept): surfaces of kinds other than NURBS / trimmed NURBS displayed (their
 * evaluation is not ported here), a curve used by an operation (delete_ent of a referenced entity NOT DECODED), and the cases
 * cs_project refuses (two pieces on one surface: the join FUN_180535ea0 / FUN_1805474e0 NOT PORTED; FUN_180531890 NOT DECODED). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "../mcsurf.h"
#include "../mccsurf.h"

int sp_used_by_ops_pub(int e);   /* cui_spline.c */

/* a line / arc made or moved here has no record: the record the save writes for it (mcam_saveops.c: start, end, the centre in the
   arc's view coordinates, radius, start angle, sweep, view) */
static uint8_t *csf_rec(const Ent *en, uint32_t *n, double vm[9], int *has_vm) {
    *has_vm = 0;
    if (en->type == E_SPLINE) {
        if (en->crv < 0 || en->crv >= F.ncrvs) return NULL;
        const CurveRec *cr = &F.crvs[en->crv]; uint8_t *r = malloc(cr->n); if (!r) return NULL; memcpy(r, cr->rec, cr->n); *n = cr->n; return r;
    }
    uint8_t *r = calloc(1, 0xd0); if (!r) return NULL;
    double d[13];
    if (en->type == E_LINE) {
        uint32_t c = 2; memcpy(r, &c, 4);
        d[0] = en->a.x; d[1] = en->a.y; d[2] = en->a.z; d[3] = en->b.x; d[4] = en->b.y; d[5] = en->b.z; memcpy(r + 0x68, d, 48); *n = 0x98; return r;
    }
    if (en->type == E_ARC) {
        uint32_t c = 4; memcpy(r, &c, 4);
        V3 s = mcam_arc_pt(en, en->a0), t = mcam_arc_pt(en, en->a0 + en->sw), cv = en->c;
        if (en->vw) { const double *m = en->vm; cv = (V3){ m[0] * en->c.x + m[1] * en->c.y + m[2] * en->c.z, m[3] * en->c.x + m[4] * en->c.y + m[5] * en->c.z, m[6] * en->c.x + m[7] * en->c.y + m[8] * en->c.z }; memcpy(vm, en->vm, 72); *has_vm = 1; }
        d[0] = s.x; d[1] = s.y; d[2] = s.z; d[3] = t.x; d[4] = t.y; d[5] = t.z; d[6] = cv.x; d[7] = cv.y; d[8] = cv.z; d[9] = en->r; d[10] = en->a0; d[11] = en->sw;
        memcpy(r + 0x68, d, 96); int16_t v = (int16_t)(en->vw ? en->vw : 1); memcpy(r + 0xc8, &v, 2); *n = 0xd0; return r;
    }
    free(r); return NULL;
}

/* the view table find_view_k searches (mastercam.dll mcViews 1 .. n_views): the 18 built in (n_views 18, decomp/mastercam_stdviews_init.c),
   a part's stored record of a number in its place (as viewer/mcsrfcrv.c view_mat), the part's other views and those added this session;
   a view find_view_k makes goes through AddMCView (mcam_add_view: kept in the part, written by the save as Mastercam's new view is) */
static int csf_vcount(void *ctx) { (void)ctx; int hi = 18; for (int i = 0; i < F.nfviews; i++) if (F.fviews[i].n > hi) hi = F.fviews[i].n; return hi; }
static int csf_vget(void *ctx, int v, double m[9]) {
    (void)ctx;
    for (int i = 0; i < F.nfviews; i++) if (F.fviews[i].n == v) { memcpy(m, F.fviews[i].m, sizeof(double) * 9); return 1; }
    return mcam_std_view(v, m);
}
static int csf_vadd(void *ctx, const double m[9]) { (void)ctx; return mcam_add_view(&F, m, 2e-7); }

void csf_run(void) {
    int *l; int n = sel_list(&l, 0);
    /* the candidate surfaces: displayed (FUN_180211740 walks the display list: not blanked, on a visible level), not deleted here */
    CsSurf *S = calloc((size_t)F.nents + 1, sizeof *S); int *Se = calloc((size_t)F.nents + 1, sizeof *Se), ns = 0, other = 0;
    for (int i = 0; i < F.nents; i++) {
        const Ent *en = &F.ents[i];
        if (en->type != E_SURF || en->srf < 0 || en->srf >= F.nsurfs || !ENT_LV_SHOWN(en) || F.surfs[en->srf].off) continue;
        const Surf *sf = &F.surfs[en->srf]; const Surf *b = sf->kind == 10 && sf->n >= 0x108 ? mcam_surf_ref(&F, *(const int64_t *)(sf->rec + 0x100)) : NULL;
        if (sf->kind == 0 || (sf->kind == 10 && b && b->kind == 0)) { S[ns].rec = sf->rec; S[ns].n = sf->n; S[ns].base = b ? b->rec : NULL; S[ns].nbase = b ? b->n : 0; Se[ns++] = i; }
        else other++;
    }
    static const CsViews V = { csf_vcount, csf_vget, csf_vadd, NULL };
    CsParams P = { mc_systol.tol, mc_systol.surf_step_chord_dev, mc_systol.curve_min_step, mcam_is_metric(&F) == 1, &V };
    char msg[300] = ""; int made = 0, refused = 0, curves = 0;
    mod_begin();   /* UndoTransStart */
    for (int i = 0; i < n; i++) {
        int e = l[i]; const Ent *en = &F.ents[e];
        if (en->type != E_LINE && en->type != E_ARC && en->type != E_SPLINE) continue;   /* select_ents mask 0x8e */
        curves++;
        if (other) { refused++; snprintf(msg, sizeof msg, "Curve Surface: %d displayed surface(s) of a kind other than NURBS / trimmed NURBS: NOT PORTED", other); continue; }
        if (sp_used_by_ops_pub(e)) { refused++; snprintf(msg, sizeof msg, "Curve Surface: the curve is used by an operation: delete_ent of a referenced entity NOT DECODED"); continue; }
        uint32_t rn = 0; double vm[9]; int hv; uint8_t *r = csf_rec(en, &rn, vm, &hv);
        if (!r) continue;
        CsCurve C = { r, rn, hv ? vm : NULL };
        CsResult *R = NULL; char m2[300];
        int k = cs_project(&C, S, ns, &P, &R, m2, sizeof m2);
        free(r);
        if (k < 0) { refused++; snprintf(msg, sizeof msg, "%s", m2); continue; }
        for (int q = 0; q < k; q++)   /* put_cons_in_db with main colour / level (FUN_18054d4f0: DAT_1812f3978 static 1) */
            if (mcam_new_cons(&F, R[q].body, (const double (*)[2])R[q].uv, R[q].npts, Se[R[q].srf], geo_level, geo_color) >= 0) made++;
        cs_free(R, k);
        if (k >= 1) mod_del(e);   /* delete_ent(eptr, 1, 0, 0, .., 0) */
    }
    mod_end();   /* UndoTransCommit */
    free(l); free(S); free(Se); sel_clear();
    geo_end();   /* sketcher_curve_6 returns: the command ends (its message below kept) */
    if (made) doc_dirty = 1;
    (void)curves;
    if (refused) snprintf(cam_msg, sizeof cam_msg, "%s", msg); else cam_msg[0] = 0;   /* clearprompt */
    overlay_dirty = 1;
}
