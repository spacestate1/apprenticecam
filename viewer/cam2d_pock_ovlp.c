/* cam2d_pock_ovlp.c - Pocket roughing, cutting method 1 "Constant Overlap Spiral" (POCKOVLP.cpp), included by cam2d_pock.c before
 * gen_pocket_v2. Line numbers: decomp/mcmill_all.c; the mccore pbmap functions in viewer/cam2d_remach.c (verified against the real
 * mccore.dll by tools/tests/remach_oracle.sh: pbmap_init_tool / pbmap_bnd_to_tp / filter_pbmap_tp / reverse_pbmap_bnds).
 *
 * FUN_1801ede50 l.339478 case 1 calls FUN_1801e8650 (l.335846) per pocket (one outer rough boundary with its islands, the rough
 * boundary = the tool-centre bounds offset by finish passes x spacing, as for the other spiral methods):
 *   - refused by Mastercam when stepover (pock_prm +0x18) <= overlap (+0x50 = D x 0x1270 %, recompute_tool_dia_dependent_fields l.314927):
 *     warning 0x964 (mcmillres 2404), the pocket gets no roughing;
 *   - every bound is turned the same way: ccw when climb (0x123a = +0x12) xor spiral inside to outside (0x1258 = +0x30) is 0, reverse
 *     otherwise (bVar15, l.335952..335965);
 *   - build_pock_bounds_info; the bounds' done flags (+5) cleared;
 *   - the first pass: spiral outside to inside: the first non-island bound; inside to outside: the first island bound (none: no pass),
 *     by FUN_1801e9a90 (no burial) / FUN_1801e9ee0 (Minimize tool burial): the entity start nearest the position (entity 0 when there is
 *     none yet), the entry at the first; marked done.
 *   - the pbmap of the pocket walls: init_pbmap(ymin - R, ymax + R, cell = overlap), verify_pock_bounds(bounds, R - overlap, or 0)
 *     (l.336015-336030) copied to the working map cur;
 *   - dV3 (the trace distance / the cut swept radius) = R; R2 (the tool disc in bnd_to_tp) = stepover - R, less the overlap when
 *     100 - overlap% < stepover% (l.335995-336005); negative: R2 = 0, dV3 = stepover;
 *   - loop (l.336050-336145), until the map area stops changing (equal to the last within 1e-12 twice): pbmap_to_pbmap_bnds(cur, dV3, 1),
 *     FUN_1801e85c0 keeps the rings whose first point is in the machinable area (pt_in_machinable_area), none left: end; climb off:
 *     reverse_pbmap_bnds; the ring: with a position FUN_1801e8470 (ring and point nearest it; the ring is restarted there), else
 *     FUN_1801e8540 (the longest ring); pbmap_init_tool(R2, cell); pbmap_bnd_to_tp(cur, base, tool, ring, 1, &tp, R2); none: end;
 *     filter_pbmap_tp(tp, overlap * 0.5);
 *     outside to inside: FUN_1801e9020 cuts the list now (first record: the entry at the very first, else the link FUN_1801dae10 / FUN_1801e0f80,
 *     each next record FUN_1801e0f80); inside to outside: FUN_1801e9180 writes the records (flag = last of its list) to a temp file;
 *     FUN_1801e8f90: the lines through the records (from the previous list's end, the first record at the very first) are swept into cur
 *     with radius dV3 (pbmap_verify_line).
 *   - inside to outside: FUN_1801e9250 reads the records back last to first: the first read enters (when no first pass was cut, else link), each
 *     record flagged last-of-list links with FUN_1801dae10 / FUN_1801e0f80 when Minimize tool burial is on (the others FUN_1801e0f80).
 *   - the remaining bounds, nearest first (FUN_1801cbd20 from the current position; entity start or midpoint when op +0x888 = lead in/out on (record 0x8f0)),
 *     each rotated (reorder_pock_bound) and cut by FUN_1801e9a90 / FUN_1801e9ee0.
 * Output: pieces in cut order (GlList). tag 0 = a whole bound pass (cut with pk_loop), tag 2 = a record list (cut record to record with
 * FUN_1801e0f80 = pk_e0f80); gen_pocket_v2's pk_rough_depth writes them, its generic link between pieces is FUN_1801e0f80 / FUN_1801dae10.
 * Not decoded here: user-defined entry point (pock_start_exref, the ones above that use *user_defined_entry_exref): the caller refuses it for the whole engine. */

#include "cam2d_remach.h"

static RmB *pk_ov_rmb(const Gl *l) {
    RmB *b = rm_b_new(l->n); int m = 0;
    for (int i = 0; i < l->n; i++) {
        const Gt *g = &l->e[i]; RmEnt *x = &b->e[m];
        if (g->t == 'L') { x->id = 'L'; x->a[0] = g->a.x; x->a[1] = g->a.y; x->a[2] = g->b.x; x->a[3] = g->b.y; m++; }
        else if (g->t == 'A') { x->id = 'A'; x->a[0] = g->c.x; x->a[1] = g->c.y; x->a[2] = g->r; x->a[3] = g->sa; x->a[4] = g->sw; m++; }
    }
    b->n = m; b->valid = 1; b->isl = (unsigned char)l->island;
    rm_b_info(b); rm_b_bbox(b);
    return b;
}
static int pk_ov_is_point(const Gl *l) { return l->n < 2 && (l->n == 0 || l->e[0].t == 'P'); }   /* pock_bound_is_point */

/* the bound pass of FUN_1801e9a90 / FUN_1801e9ee0: from the entity start nearest pos when there is a position, entity 0 otherwise (strict <, first wins) */
static Gl pk_ov_pass(const Gl *l, int have, PV pos) {
    Gl c = gl_copy(l);
    int best = 0;
    if (have) { double bd = 1e38; for (int k = 0; k < c.n; k++) { PV s = gt_start(&c.e[k]); double d = hypot(pos.x - s.x, pos.y - s.y); if (d < bd) { bd = d; best = k; } } }
    gl_rotate(&c, best);
    c.tag = 0;
    return c;
}
static void pk_ov_push_list(GlList *out, const RmTp *t, int reversed) {
    int n = 0; for (const RmTp *q = t; q; q = q->next) n++;
    PV *p = malloc(sizeof(PV) * (n ? n : 1)); int i = 0;
    for (const RmTp *q = t; q; q = q->next) p[i++] = pv(q->x, q->y);
    Gl l = { 0 };
    if (n == 1) gl_push(&l, gt_line(p[0], p[0]));                   /* a single record: only its link moves */
    for (int k = 0; k + 1 < n; k++) {
        if (reversed) gl_push(&l, gt_line(p[n - 1 - k], p[n - 2 - k]));
        else gl_push(&l, gt_line(p[k], p[k + 1]));
    }
    l.tag = 2; gll_push(out, l); free(p);
}
static void pk_ov_append_rev(GlList *out, const GlList *tmp) {
    for (int i = tmp->n - 1; i >= 0; i--) {
        Gl l = { 0 }; const Gl *t = &tmp->l[i];
        for (int k = t->n - 1; k >= 0; k--) gl_push(&l, gt_line(t->e[k].b, t->e[k].a));
        l.tag = 2; gll_push(out, l);
    }
}

/* FUN_1801e8650 for one pocket; rb is modified (turned). 0 + err when Mastercam refuses (warning, no roughing for the pocket). */
static int pk_ov_loops(Gls *rb, double D, double stepov, double step_pct, double ov_pct, int climb, int in2out, int lead_on, double nctol, GlList *out, char *warn, size_t warnn) {
    memset(out, 0, sizeof *out);
    double ovl = 0.01 * D * ov_pct;                                                  /* +0x50 = D x 0x1270 %, recompute_tool_dia_dependent_fields l.314927 */
    if (stepov <= ovl) { snprintf(warn, warnn, "The constant overlap spiral tolerance must be less than the stepover. The toolpath will not be created."); return 0; }   /* post_warning 0x964 (mcmillres 2404), l.335938 */
    int ccw_want = (climb == 0); if (in2out) ccw_want = !ccw_want;                   /* bVar15 */
    for (int i = 0; i < rb->n; i++) if (rb->l[i].n && !pk_ov_is_point(&rb->l[i]) && (gl_area(&rb->l[i]) > 0) != ccw_want) gl_reverse(&rb->l[i]);
    RmB *blist = NULL;
    for (int i = 0; i < rb->n; i++) if (!pk_ov_is_point(&rb->l[i])) rm_b_append(&blist, pk_ov_rmb(&rb->l[i]));
    int *done = calloc((size_t)(rb->n ? rb->n : 1), sizeof(int));
    int have = 0, first_cut = 1; PV pos = pv(0, 0);
    /* the first pass (l.335968-335990) */
    for (int i = 0; i < rb->n; i++) {
        Gl *l = &rb->l[i];
        if (pk_ov_is_point(l) || !l->n) continue;
        if (!in2out ? !l->island : l->island) {
            Gl p = pk_ov_pass(l, 0, pos);
            gll_push(out, p); done[i] = 1; have = 1; first_cut = 0;
            pos = gt_start(&p.e[0]);                                                 /* a closed pass ends at its start */
            break;
        }
    }
    double R = D * 0.5, R2 = stepov - R, dV3 = R;
    if (100.0 - ov_pct < step_pct) R2 = R2 - ovl;                                     /* l.336001 */
    if (R2 < 0.0) { R2 = 0.0; dV3 = stepov; }
    double ymin = 1e38, ymax = -1e38;
    for (RmB *b = blist; b; b = b->next) { if (b->bbox[1] < ymin) ymin = b->bbox[1]; if (ymax < b->bbox[3]) ymax = b->bbox[3]; }   /* +0x28 / +0x38 */
    RmMap base, cur; memset(&base, 0, sizeof base); memset(&cur, 0, sizeof cur);
    rm_map_init(&base, ymin - R, ymax + R, ovl);
    double rv = dV3 - ovl; if (rv < 0.0) rv = 0.0;
    rm_verify_bounds(blist, rv, &base);
    rm_map_copy(&cur, &base);
    GlList tmp = { 0 };
    int sv = 0, fp = 0; double last_a = 0.0; PV lastp_v = pv(0, 0);
    for (int it = 0; it < 100000; it++) {
        double a = rm_map_area(&cur);
        if (fabs(a - last_a) < 1e-12) { sv++; if (1 < sv) break; }
        RmBnd *bnds = NULL;
        rm_map_trace(&cur, dV3, 1, &bnds);
        int nvalid = 0;
        for (RmBnd *n = bnds; n; n = n->next) { n->on = pk_pt_ok(rb, pv(n->first->x, n->first->y), nctol); if (n->on) nvalid++; }
        if (!nvalid) { rm_bnds_free(bnds); break; }
        if (!climb) rm_reverse_bnds(bnds);
        RmBnd *pick = NULL;
        if (have) {                                                                  /* FUN_1801e8470 l.335749 */
            double bd = 1e38; RmPt *bp = NULL;
            rm_map_set_used(&cur, 0);
            for (RmBnd *n = bnds; n; n = n->next) {
                if (!n->on) continue;
                for (RmPt *q = n->first; q && !q->used; q = q->next) {
                    double d = sqrt((q->x - pos.x) * (q->x - pos.x) + (q->y - pos.y) * (q->y - pos.y));
                    if (d < bd) { bd = d; bp = q; pick = n; }
                    q->used = 1;
                }
            }
            if (pick) pick->first = bp;
        } else {                                                                     /* FUN_1801e8540 l.335795 */
            int bc = 0;
            rm_map_set_used(&cur, 0);
            for (RmBnd *n = bnds; n; n = n->next) {
                if (!n->on) continue;
                int c = 0;
                for (RmPt *q = n->first; q && !q->used; q = q->next) { q->used = 1; c++; }
                if (bc < c) { pick = n; bc = c; }
            }
        }
        if (!pick) { rm_bnds_free(bnds); break; }
        RmTool tl; rm_tool_init(R2, base.cell, &tl);
        RmTp *tp = NULL;
        rm_bnd_to_tp(&cur, &base, &tl, pick, 1, &tp, R2);
        rm_tool_free(&tl);
        if (!tp) { rm_bnds_free(bnds); break; }
        rm_bnds_free(bnds);
        rm_filter_tp(&tp, ovl * 0.5);
        /* the ring-closing record of pbmap_bnd_to_tp (pb l.705, checked against the real mccore.dll, tools/tests/remach_oracle.sh) is kept, as the decompile and the DLL have it, although the
           stored NCI of BLOCK (Handbook Vol. 1, X9 / 2017, op 3) does not contain it: user decision N9-5 (formats/user_decisions.md, 2026-11-09) */
        if (!in2out) {
            pk_ov_push_list(out, tp, 0);                                             /* FUN_1801e9020 */
            for (RmTp *q = tp; q; q = q->next) { pos = pv(q->x, q->y); }
            have = 1;
        } else {
            pk_ov_push_list(&tmp, tp, 0);                                             /* FUN_1801e9180 */
            for (RmTp *q = tp; q; q = q->next) { pos = pv(q->x, q->y); }
            have = 1;
        }
        for (RmTp *q = tp; q; q = q->next) {                                         /* FUN_1801e8f90 l.336151 */
            double l4[4];
            if (!fp) { l4[0] = q->x; l4[1] = q->y; } else { l4[0] = lastp_v.x; l4[1] = lastp_v.y; }
            l4[2] = q->x; l4[3] = q->y;
            rm_verify_line(l4, dV3, &cur);
            lastp_v = pv(q->x, q->y); fp = 1;
        }
        rm_tp_free(tp);
        last_a = a;
    }
    if (in2out) {
        pk_ov_append_rev(out, &tmp);                                                 /* FUN_1801e9250 l.336298 */
        if (tmp.n) { Gl *l = &tmp.l[0]; pos = l->e[0].a; have = 1; }                 /* the last move ends at the first list's first record */
    }
    gll_free(&tmp);
    /* the remaining bounds (FUN_1801cbd20 + reorder_pock_bound + FUN_1801e9a90 / FUN_1801e9ee0, l.336135-336150) */
    while (1) {
        int bi = -1, be = 0; double bd = 1e38, tv = lead_on ? 0.5 : 0.0;
        for (int i = 0; i < rb->n; i++) {
            Gl *l = &rb->l[i];
            if (done[i] || pk_ov_is_point(l) || !l->n) continue;
            for (int k = 0; k < l->n; k++) {
                if (l->e[k].t == 'P') continue;
                PV q = gt_pt(&l->e[k], tv);
                double d = sqrt((q.x - pos.x) * (q.x - pos.x) + (q.y - pos.y) * (q.y - pos.y));
                if (d < bd || bi < 0) { bi = i; be = k; bd = d; }
            }
        }
        if (bi < 0) break;
        gl_rotate(&rb->l[bi], be);
        Gl p = pk_ov_pass(&rb->l[bi], have, pos);
        pos = gt_start(&p.e[0]); gll_push(out, p); have = 1; done[bi] = 1;
    }
    (void)first_cut;
    free(done); rm_b_free_list(&blist); rm_map_free(&base); rm_map_free(&cur);
    return 1;
}
