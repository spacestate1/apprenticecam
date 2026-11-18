/* cam2d_pock_rem.c - Pocket type 3 "Remachining" (POCKRMCH.cpp FUN_1801ec3c0, decomp/mcmill_all.c l.338353-338921, called from process_pocket l.323274),
 * included by cam2d_pock.c before gen_pocket_v2. Decode notes: formats/pocket_engine_b6.md "Remachining (2026-11-09)". Line numbers = decomp/mcmill_all.c; the pbmap engine = viewer/cam2d_remach.c
 * (checked against the real mccore.dll by tools/tests/remach_oracle.sh, pbmap_fill_pock_bounds added there for this op).
 *
 * One call per chain pocket (FUN_1801c5250 l.313600 extract_pocket of the chains; process_pocket l.323258 local_98 = the pocket's chains, local_90 = FUN_1801d4620 the tool-centre bounds):
 *   build_pock_bounds_info(chains); Y range of the chains +- (ymax - ymin) x 0.5714285714285714 (l.338524-338526);
 *   cut map: source 0 / 1 (pock_prm +0x18a = 0x13b2 < 2) FUN_1801ebc60 (l.337954: verify_2d_operation over the earlier ops of the main list, 0 all before the current op,
 *            else only the last one; a Pocket op with finish on and compensation in control refuses it: post_warning 0x519, -1)
 *            source 2: FUN_1801ec130(chains, 0x13b8 x 0.5, stock) (l.338251: comp_pock_bnds by r + stock, pbmap_fill_pock_bounds, verify_pock_bounds r);
 *   reach map: FUN_1801ec130(chains, R, stock); pbmap_diff(reach, cut, cell) = what this tool can still remove (l.338567);
 *   rest: pbmap_to_pbmap_bnds(reach, R, 0), copy, verify_pbmap_bounds(R + clearance 0x13c8), trace again, filter_pbmap_bnds(cell x 0.5), pbmap_bnds_to_pock_bounds, sort_pock_bounds(0, 0, 1, **pock_prm), pock_bounds_bbox (l.338586-338615);
 *   l.338594-338609: local_128 (the walls' band) is only ever differenced from the display copy (+0x58 Display stock): no effect on the cuts, not done;
 *   per tool-centre pocket (extract_pocket(list, -2) l.338624): the writer bounds local_1b8 / local_1c8 = FUN_1801edcd0 (all tool-centre bounds offset by 0 / finish passes x spacing, disassembly 0x1801eca5a-0x1801ecabb);
 *   passes (l.338644-338786): offset d_0 = 0; d_k += spacing +0x38 while k <= nf (k = nf too); then stepover +0x18 (roughing on, > 1e-12), stop else; offset_pock_bounds(pocket, d), direction by +0x12 climb,
 *   pock_trim_toolpath against the rest (finish pass with +0x1a9 "Machine complete finish passes": copied whole, refused here), remach_merge_adj_segs;
 *   pieces with leads FUN_1801bdc00 when finish pass or +0x1a8 "Apply entry/exit curves to rough passes" (open pieces: no split, no overlap; the check FUN_1801c4860 l.313160 on: disassembly 0x1801ecdf0 param_5 = the pocket's closed flag = 1);
 *   the first pass with no piece (or points only) ends the loop; add_remachine_bound(piece, k);
 *   find_remachine_group until none: each entry's bnci_wr_pock_marker (pocket, finish = idx < nf, pass nf - idx - 1, depth index, sub-pocket local_232, group local_236) and the writer FUN_1801cd200:
 *   the first piece of a group: FUN_1801bfcb0 -> _enter_pocket with param_3 = 0 (LAB_1801bce44 -> FUN_1801bf680 l.309910, no helix/ramp entry); a later piece: FUN_1801e0f80 from the previous written piece's end to its start. Their first
 *   record is the piece start (the walk around islands of FUN_1801e0f80 stays NOT DECODED: the first record would be its first waypoint); wr_pock_toolpath's section start rule (l.315990-316049, type 3 only) then decides the motion: pk_rem_cut.
 *
 * NOT DECODED (refused below): keep tool down 0x126a (FUN_1801cd200 l.318461 / wr_pock_toolpath l.315662 ktd sections), Machine complete finish passes 0x13d1 (the +0x50 parents of sort_pock_bounds' p1 / p2 branches), compensation in control on
 * finish pieces (FUN_1801cbef0, l.318721), tapered walls / island depths (the proc_by_depth branch, FUN_1801c7970), lead helix / ramp heights, chained entry points (user_defined_entry), thin wall 0x1436, finish feed / spindle override 0x1434 / 0x1435.
 * comp_pock_bnds (FUN_1801ec130 l.338299; mccore 0x1805d8bf0, decomp/pocket_b6/decomp_comp_pock_bnds.c) and the pass offsets offset_pock_bounds (decomp/mccore_pock.c l.666): per bound ccomp_pock (rm_ccomp_pock); merge_pock_bounds FUN_1805e02e0 param 0: rm_merge_bounds (cam2d_remach.c, against the DLL in remach_oracle.sh MERGEB).
 * INFERRED: an offset rm_ccomp_pock refuses (the MCCE port's limits) falls back to gls_offset, this engine's offset substitute; FUN_1801edcd0's two offsets (the writer bounds, MCCECalcOffset) are gls_offset, and so the list order extract_pocket walks.
 */
typedef struct {
    Gl g;                      /* the piece as written (leads included) */
    int idx, k, sub, group;    /* add_remachine_bound idx; marker pocket / sub-pocket / group */
    int fin, j;                /* finish, pass nf - idx - 1 */
    int first;                 /* first of its group (FUN_1801cd200 param_9 == 0) */
    int nlead_in, nlead_out;   /* lead entities kept at the start / end */
    int point; PV pt;          /* a point piece: no entity, the section still enters (FUN_1801cd200 l.318561 FUN_1801bfcb0 runs before the entity loop, the marker is written for it) */
    const ZBs *wb;             /* FUN_1801cd200 param_2: local_1b8 (finish) / local_1c8 */
    PV prev_end;               /* the end of the piece written before it */
    long pos;
} PkRemP;
typedef struct { PkRemP *a; int n, cap; ZBs *zbs[64]; ZB *zb[64]; int nz; } PkRemList;

static int pk_rem_is_point(const RmB *b) { return b->n < 2 && (b->n == 0 || b->e[0].id == 'P'); }   /* pock_bound_is_point */

static Gl pk_rem_gl(const RmB *b) {
    Gl g = { 0 };
    for (int i = 0; i < b->n; i++) {
        const RmEnt *x = &b->e[i];
        if (x->id == 'L') gl_push(&g, gt_line(pv(x->a[0], x->a[1]), pv(x->a[2], x->a[3])));
        else if (x->id == 'A') gl_push(&g, gt_arc(pv(x->a[0], x->a[1]), x->a[2], x->a[3], x->a[4]));
    }
    return g;
}

/* the loops as a bound list (island flag and direction carried: chains_to_pock_bounds / the tool-centre bounds) */
static RmB *pk_rem_list(const Gls *g) { RmB *l = NULL; for (int i = 0; i < g->n; i++) if (g->l[i].n) rm_b_append(&l, pk_ov_rmb(&g->l[i])); return l; }
/* move_split_off_pock_bounds (decomp/mccore_pock2.c l.73): the bounds ccomp_pock marked (+1) go to *split */
static void pk_rem_split_off(RmB **l, RmB **split) {
    for (RmB *b = *l, *nx; b; b = nx) { nx = b->next; if (b->broken) { if (b->prev) b->prev->next = b->next; else *l = b->next; if (b->next) b->next->prev = b->prev; b->next = b->prev = NULL; rm_b_append(split, b); } }
}
/* delete_intersecting_pock_bounds (decomp/mccore_pock2.c l.304) */
static void pk_rem_del_x(RmB **l, RmB *by) {
    rm_b_bboxs(*l); rm_b_bboxs(by);                            /* pock_bounds_bbox(*param_1), pock_bounds_bbox(param_2) */
    for (RmB *b = *l, *nx; b; b = nx) { nx = b->next; for (const RmB *c = by; c; c = c->next) if (!rm_disjoint(b, c) && rm_bound_intersect(b, c)) { rm_b_del(l, b); break; } }
}
/* delete_nested_pock_bounds (decomp/mccore_pock2.c l.364): nested (is_ctour_nested, decomp/mccore_pock.c l.2730) in an island of list by */
static void pk_rem_del_nested(RmB **l, RmB *by) {
    rm_b_bboxs(*l); rm_b_bboxs(by);                            /* pock_bounds_bbox(*param_1), pock_bounds_bbox(param_2) */
    for (RmB *b = *l, *nx; b; b = nx) { nx = b->next; for (const RmB *c = by; c; c = c->next) if (b != c && c->isl && !(c->n < 2 && c->e[0].id == 'P') && !rm_disjoint(b, c) && rm_is_ctour_nested(b, c)) { rm_b_del(l, b); break; } }
}
/* offset_pock_bounds(list, d, min_fil, 1, 1) (decomp/mccore_pock.c l.666) = per bound in list order ccomp_pock with side outer: ccw ? 'L' : 'R', island: ccw ? 'R' : 'L' (comp_pock_bnd decomp/mccore_pock2.c l.24 the same); +0x50 = the source bound; 0 when the MCCE port refuses a call */
static int pk_rem_offset(const RmB *src, double d, double minfil, RmB **out, const char **why) {
    *out = NULL;
    for (const RmB *c = src; c; c = c->next) {
        RmB *res = NULL;
        char side = c->ccw ? (c->isl ? 'R' : 'L') : (c->isl ? 'L' : 'R');
        if (rm_ccomp_pock(c, &res, d, side, minfil, 1, 1, 0, why)) { rm_b_free_list(out); return 0; }
        while (res) { RmB *n = res->next; res->next = res->prev = NULL; res->par = (RmB *)c; rm_b_append(out, res); res = n; }
    }
    return 1;
}
/* FUN_1801ec130 l.338299-338311 for closed chains: comp_pock_bnds (mccore 0x1805d8bf0, decomp/pocket_b6/decomp_comp_pock_bnds.c) = pk_rem_offset; the split-offs aside (l.338300); FUN_1801ec350 turns outers ccw and islands cw; merge_pock_bounds (rm_merge_bounds);
   delete_intersecting by the chains, delete_nested by the copy from before the merge (the split-offs are not in it); the split-offs: delete_intersecting only, appended at the end. INFERRED: gls_offset (the engine's substitute) when the MCCE port refuses a call. */
static RmB *pk_rem_comp(const Gls *cp, RmB *chains, double d, double minfil, double tol) {
    const char *why = NULL; RmB *all = NULL, *split = NULL;
    if (!pk_rem_offset(chains, d, minfil, &all, &why)) {
        if (getenv("PK_DEBUG")) fprintf(stderr, "rem comp: gls_offset fallback (%s)\n", why ? why : "");
        Gls off = gls_offset(cp, -d, minfil, tol); RmB *l = pk_rem_list(&off); gls_free(&off);
        return l;
    }
    pk_rem_split_off(&all, &split);
    RmB *copy = NULL; for (RmB *q = all; q; q = q->next) { RmB *c = rm_b_copy(q); c->next = c->prev = NULL; rm_b_append(&copy, c); }
    for (RmB *q = all; q; q = q->next) if ((q->isl ? 0 : 1) != q->ccw) rm_b_reverse(q, 0);
    if (all) rm_merge_bounds(&all);
    if (all) pk_rem_del_x(&all, chains);
    { int n0 = 0, n1 = 0; for (RmB *q = all; q; q = q->next) n0++; if (all) pk_rem_del_nested(&all, copy); for (RmB *q = all; q; q = q->next) n1++; if (getenv("PK_DEBUG")) fprintf(stderr, "rem comp: %d rings, %d after delete_nested\n", n0, n1); }
    rm_b_free_list(&copy);
    if (split) pk_rem_del_x(&split, chains);
    while (split) { RmB *n = split->next; split->next = split->prev = NULL; rm_b_append(&all, split); split = n; }
    return all;
}
/* FUN_1801ec130 l.338251-338327: pk_rem_comp, pbmap_fill_pock_bounds, verify_pock_bounds(r) */
static void pk_rem_ec130(const Gls *cp, RmB *chains, double r, double stock, double minfil, double tol, double ymin, double ymax, double cell, RmMap *m) {
    rm_map_init(m, ymin, ymax, cell);
    RmB *l = pk_rem_comp(cp, chains, r + stock, minfil, tol);
    if (l) { rm_fill_bounds(l, m); rm_verify_bounds(l, r, m); rm_b_free_list(&l); }
}

/* FUN_1801bdc00 l.308436 for an open piece (param_4 = 0: no split FUN_1801bf950, no overlap cVar27 = 0, l.308696): [entry] + piece + [exit]; the lead check runs (param_5 = 1) as for a finish pass, pass = param_1 = the pocket
   (FUN_1801c4860: inner = the pocket offset by +0x38 bound by bound), me = the pocket bound nearest the piece start (FUN_1801edc70 l.339251) */
static void pk_rem_leads(const PkFin *F, int left, const Gl *piece, const Gls *pocket, const Gls *inner, const int *par, double nct, double tol, Gl *out, int *nik, int *nok) {
    memset(out, 0, sizeof *out); *nik = *nok = 0;
    int any = F->ein_line || F->ein_arc || F->eout_arc || F->eout_line;
    if (!any) { for (int i = 0; i < piece->n; i++) gl_push(out, piece->e[i]); return; }
    const Gt *e0 = &piece->e[0], *en = &piece->e[piece->n - 1];
    Gt le[8]; char keep[8]; int nle = 0, nin, nout; Gt ea = { 0 }, xa = { 0 };
    if (F->ein_arc) ea = pk_lead_ent(e0, 0, 0, left, 0, F->lr, F->lsw, 0);
    if (F->ein_line) le[nle++] = pk_lead_ent(F->ein_arc ? &ea : e0, 0, 1, left, F->ll, 0, 0, F->lperp);
    if (F->ein_arc) le[nle++] = ea;
    nin = nle;
    if (F->eout_arc) { xa = pk_lead_ent(en, 1, 0, left, 0, F->xr, F->xsw, 0); le[nle++] = xa; }
    if (F->eout_line) le[nle++] = pk_lead_ent(F->eout_arc ? &xa : en, 1, 1, left, F->xl, 0, 0, F->xperp);
    nout = nle - nin;
    for (int k = 0; k < nle; k++) keep[k] = 1;
    if (nin > F->chk_in && nout > F->chk_out) {
        PV s = gt_start(e0); double bd = 1e38; int me = 0;
        for (int i = 0; i < pocket->n; i++) { double d = gl_closest(&pocket->l[i], s, NULL, NULL, NULL); if (d < bd) { bd = d; me = i; } }
        pk_lead_check(le, nle, nin, nout, keep, pocket, inner, par, me, nct, tol);
    }
    for (int k = 0; k < nin; k++) if (keep[k]) { gl_push(out, le[k]); (*nik)++; }
    for (int i = 0; i < piece->n; i++) gl_push(out, piece->e[i]);
    for (int k = nin; k < nle; k++) if (keep[k]) { gl_push(out, le[k]); (*nok)++; }
}

/* the bounds of the lead check (FUN_1801c4860 l.313160): offset_pock_bounds of the pocket by +0x38, bound by bound, each result's +0x50 = its bound (parent[]) */
static Gls pk_rem_inner(const Gls *pocket, const RmB *pl, double fs, double minfil, double tol, int **parent) {
    RmB *res = NULL; const char *why = NULL; Gls out = { 0 };
    if (!pk_rem_offset(pl, fs, minfil, &res, &why)) return pk_fin_inner(pocket, fs, minfil, tol, parent);   /* INFERRED: the MCCE port refused a call */
    int n = 0; for (RmB *q = res; q; q = q->next) n++;
    *parent = calloc((size_t)(n ? n : 1), sizeof(int)); int k = 0;
    for (RmB *q = res; q; q = q->next, k++) { Gl g = pk_rem_gl(q); g.island = q->isl; gls_push(&out, g); int pi = 0; for (const RmB *c = pl; c && c != q->par; c = c->next) pi++; (*parent)[k] = pi; }
    rm_b_free_list(&res);
    return out;
}
/* sort_pock_bounds(chains, tol < d, 0, tol, &list) (decomp/mccore_pock.c l.193): the nesting (p3 = 0, rm_sort), then with p2 set (l.409-450) a group whose outer holds the first point of a chain and whose islands do not (a ring around the chains) is deleted (FUN_1805d48e0: the groups after it renumbered, and the loop index goes on, as read) */
static void pk_rem_sort(RmB **l, const RmB *chains, int p2, double sort_tol) {
    short ng = 0; rm_sort(l, 0, sort_tol, &ng);
    if (!p2) return;
    for (int g = 0; g < ng; g++) {
        int b14 = 0, b13 = 0;
        for (RmB *b = *l; b; b = b->next) {
            if (b->gid != g || (b->n < 2 && b->e[0].id == 'P')) continue;
            for (const RmB *c = chains; c; c = c->next) {
                if (c->n < 2 && c->e[0].id == 'P') continue;
                double s[2], en[2]; rm_gt_ends(&c->e[0], s, en);
                int in = rm_pt_in_ctour(b, s[0], s[1], rm_nc_tol, rm_tol) == 1;
                if (b13 || !b->isl) { if (!b14 && !b->isl) b14 = in; } else b13 = in;
            }
        }
        if (b14 && !b13) { for (RmB *b = *l, *nx; b; b = nx) { nx = b->next; if (b->gid == g) rm_b_del(l, b); else if (g < b->gid) b->gid--; } ng--; }
    }
}
/* one tool-centre pocket (l.338627-338921) with the rest bounds restb: appends the pieces in write order */
static int pk_rem_pocket(const CamParams *p, const PkFin *F, const Gls *pocket, RmB *tc_list, RmB *chains, double sort_tol, const ZBs *wbf, const ZBs *wbr, RmB *restb, int nf, double fs, double step, int rough, int lead_rough, int climb,
                         double minfil, double tol, double nct, int k, int sub, PkRemList *L, char *err, size_t errn) {
    RmRem *rem = NULL;
    Gls inner = { 0 }; int *par = NULL;
    RmB *pl = pk_rem_list(pocket);
    int lead_any = F->ein_line || F->ein_arc || F->eout_arc || F->eout_line;
    if (lead_any && (nf > 0 || lead_rough)) inner = pk_rem_inner(pocket, pl, pkd(p, 0x1260), minfil, tol, &par);    /* FUN_1801c4860 l.313160: offset_pock_bounds by +0x38 = 0x1260, whether or not finish passes are on */
    double d = 0.0; int ok = 1;
    for (int pass = 0; ok; pass++) {
        if (pass) {                                            /* l.338651-338665 */
            if (pass < nf) d += fs;
            else {
                if (step < 1e-12 || !rough) break;
                d += pass == nf ? fs : step;
            }
        }
        /* l.338673-338698: offset_pock_bounds, move_split_off, merge_pock_bounds, delete_intersecting (by the tool-centre bounds when d > nc_tol, else the chains), the split-offs too, sort_pock_bounds(chains, tol < d, 0, tol) */
        RmB *plist = NULL, *split = NULL; const char *why = NULL;
        if (!pk_rem_offset(pl, d, minfil, &plist, &why)) {     /* INFERRED: the MCCE port refused a call: gls_offset, the engine's substitute */
            Gls ofs = d > 0 ? gls_offset(pocket, -d, minfil, tol) : gls_copy(pocket);
            plist = pk_rem_list(&ofs); gls_free(&ofs);
            if (!plist) break;
        } else {
            pk_rem_split_off(&plist, &split);
            if (!plist) { rm_b_free_list(&split); break; }     /* l.338677 local_228 = 0 */
            rm_merge_bounds(&plist);
            RmB *by = nct < d ? tc_list : chains;
            if (plist) pk_rem_del_x(&plist, by);
            if (split) pk_rem_del_x(&split, by);
            while (split) { RmB *n = split->next; split->next = split->prev = NULL; rm_b_append(&plist, split); split = n; }
            split = NULL;
            pk_rem_sort(&plist, chains, rm_tol < d, sort_tol);
        }
        for (RmB *b = plist; b; b = b->next) {                 /* direction: l.338700-338711, climb = +0x12 = 0x123a */
            int want = climb ? !b->isl : b->isl;
            if (want != b->ccw) rm_b_reverse(b, 0);
        }
        RmB *pieces = NULL;
        rm_trim(&plist, restb, &pieces);                       /* pock_trim_toolpath l.338714 */
        rm_merge(&pieces);                                     /* remach_merge_adj_segs l.338721 */
        rm_b_free_list(&plist);
        int any_real = 0;
        for (RmB *q = pieces; q; q = q->next) if (!pk_rem_is_point(q)) { any_real = 1; break; }
        if (!any_real) { rm_b_free_list(&pieces); break; }     /* l.338723-338729 */
        for (RmB *q = pieces; q; q = q->next) {
            Gl g = pk_rem_gl(q);
            RmB *w = NULL;
            if (g.n && (pass < nf || lead_rough)) {            /* l.338747 */
                Gl out; int nik, nok; pk_rem_leads(F, climb, &g, pocket, &inner, par, nct, tol, &out, &nik, &nok);
                w = pk_ov_rmb(&out); w->par = q->par; w->nlead_in = nik; w->nlead_out = nok; gl_free(&out);
            } else if (!g.n && lead_any && (pass < nf || lead_rough)) { snprintf(err, errn, "pocket Remachining: entry / exit curves on a point piece: NOT DECODED"); ok = 0; gl_free(&g); break; }   /* FUN_1801bdc00 on a 'P' entity is not read */
            if (!w) q->nlead_in = q->nlead_out = -1;           /* no FUN_1801bdc00 run: the piece's entities carry no bit 0 / 1 (l.338747-338753), so 0x992 / 0x993 lift them (l.338862-338870) */
            rm_rem_add(w ? w : q, (short)pass, &rem);          /* l.338775 */
            if (w) rm_b_free(w);
            gl_free(&g);
        }
        rm_b_free_list(&pieces);
    }
    gls_free(&inner); free(par); rm_b_free_list(&pl);
    if (!ok) { rm_rem_free(&rem, 1); return 0; }
    /* groups, l.338839-338905 */
    for (RmRem *r = rem; r; r = r->next) r->active = 1;
    char cont = 0; double cur[2] = { 0, 0 }; int group = 0; PV prev = pv(0, 0);
    for (;;) {
        RmRem *g = NULL;
        rm_group(&cont, cur, rem, &g);
        if (!g) break;
        int first = 1;
        for (RmRem *q = g; q; q = q->next) {
            if (L->n == L->cap) { L->cap = L->cap ? L->cap * 2 : 64; L->a = realloc(L->a, (size_t)L->cap * sizeof *L->a); }
            PkRemP *P = &L->a[L->n];
            memset(P, 0, sizeof *P);
            P->g = pk_rem_gl(q->b); P->idx = q->idx; P->k = k; P->sub = sub; P->group = group; P->first = first; P->nlead_in = q->b->nlead_in; P->nlead_out = q->b->nlead_out;
            if (!P->g.n && q->b->n && q->b->e[0].id == 'P') { P->point = 1; P->pt = pv(q->b->e[0].a[0], q->b->e[0].a[1]); }
            P->fin = q->idx < nf; P->j = P->fin ? nf - q->idx - 1 : 0;
            P->wb = P->fin ? wbf : wbr;                        /* FUN_1801cd200 param_2, disassembly 0x1801ed1c9-0x1801ed1d2 */
            P->prev_end = prev; P->pos = L->n;
            prev = P->point ? P->pt : P->g.n ? gt_end(&P->g.e[P->g.n - 1]) : prev;
            L->n++; first = 0;
        }
        rm_rem_free(&g, 0);
        group++;
    }
    rm_rem_free(&rem, 1);
    return 1;
}

#define PkRemH PkH
#define pk_rem_h pk_h

#define pk_rem_up pk_up
/* plunge_move with the tool up (decomp/mccore_moves.c l.85): first: a rapid to s at clearance (or retract, clearance off) (+0x111: l.140, +0x121: l.131; pock_wr_sof = 1 in wr_pock_toolpath, l.315476), a rapid to s at the feed plane, the plunge at op +0x318 */
static void pk_rem_plunge(MB *b, const CamParams *p, PV s, double z, PkRemH H, int c14) {
    if (p->mc_clr_on) { if (H.f + rm_tol < H.c && z + rm_tol < H.c && c14) pk_rapid(b, s.x, s.y, H.c); }
    else if (p->mc_ret_on && H.f + rm_tol < H.r && z + rm_tol < H.r && c14) pk_rapid(b, s.x, s.y, H.r);
    pk_rapid(b, s.x, s.y, z + rm_tol < H.f ? H.f : z);
    mv(b, 0, s.x, s.y, z, p->plunge_feed);
}
/* one written piece at depth z, as wr_pock_toolpath copies its section (type 3: l.315990-316049). l.315582: set_heights(depth, the marker's top) for the section: ref = top of stock, a later depth cut the previous cut's Z (wr_pock_depth_cuts l.315181). The section's first record is the first point the writer goes to (the piece start, or the first waypoint of the link FUN_1801e0f80 = pko_link):
   tool down and more than nc_tol from the last point: set_heights(previous depth, previous depth) (l.315998), retract_move at the last point, or clear_move when the pocket changes unless clearance only at start / end (+0x111 and +0x2b1); then set_heights again (l.316019) and plunge_move.
   0x992 Plunge after first move (op +0x92a, l.315505-315508): the first entity of the piece is a lead, or carries no bit 0 because FUN_1801bdc00 did not run (rough pieces without +0x1a8, l.338740-338753, l.338862-338870). On a finish section the section's first record is raised to clearance (first section of the op) or retract when the next record is that entity (l.315816-315831, l.315847-315850).
   The entity record itself ends at the retract height (bVar17 is already clear, l.316047), and after it the tool plunges to the depth at the entity's end at the plunge rate (plunge_move with the tool down, l.316463-316467).
   0x993 Retract before last move (op +0x92b, l.315509-315511): before the last entity (0x8000 without bit 1): set_heights(depth, depth), at the last point clear_move on the last depth (and local_920 suppresses the op's end clear_move, l.316422, l.316552) else retract_move (l.316403-316428), then the record ends at the clearance / retract height (l.315790-315804). */
static void pk_rem_cut(MB *b, const CamParams *p, const PkRemP *q, double z, double ref, int zi, int nz, double ctol, double nct, int *any, int *lastk, double *lastz, int *td, int *noend) {
    if (!q->point && !q->g.n) return;
    PV s = q->point ? q->pt : gt_start(&q->g.e[0]);
    int first = !*any, newp = !first && *lastk != q->k, only_se = p->mc_clr_on && p->mc_clr_end, down = 0;
    PkRemH H = pk_rem_h(p, z, ref);
    /* the link records of a later piece of a group (FUN_1801e0f80 -> FUN_1801e23c0, FUN_1801bc830 l.308162-308223; only the first of them matters for the section start) */
    MB lk = { 0 }; PV r0 = s; int use_link = 0;
    if (!first && !q->first && !zz_equal(q->prev_end, s)) {
        char e2[160]; e2[0] = 0;
        mv(&lk, 0, q->prev_end.x, q->prev_end.y, z, p->feed);
        if (pko_link(&lk, p, q->wb, q->prev_end, s, z, ctol, e2, sizeof e2) && lk.n > 1) { use_link = 1; r0 = pv(lk.m[1].p.x, lk.m[1].p.y); }
    }
    int fl_in = (p->lead_xflags & 16) && p->lead_on && p->lead_in_on, fl_out = (p->lead_xflags & 32) && p->lead_on && p->lead_out_on;   /* local_945 / local_93f, l.315505-315511 */
    int lift_first = fl_in && q->g.n && q->nlead_in != 0, lift_last = fl_out && q->g.n && q->nlead_out != 0;
    double zp = z;
    if (q->fin && lift_first && (!use_link || lk.n <= 2)) zp = first ? H.c : H.r;  /* l.315816-315831, l.315847-315850 */
    if (!first && *td) {
        V3 e = lastp(b);
        if (hypot(e.x - r0.x, e.y - r0.y) > nct) pk_rem_up(b, p, pk_rem_h(p, *lastz, *lastz), newp && !only_se);
        else down = 1;
    }
    if (down) {
        V3 e = lastp(b);
        if (hypot(e.x - r0.x, e.y - r0.y) > rm_tol) mv(b, 0, r0.x, r0.y, e.z, p->feed);
        if (fabs(e.z - zp) > 1e-12) mv(b, 0, r0.x, r0.y, zp, p->plunge_feed);     /* the record is always written; a zero-length one is no motion */
    } else {
        int c14 = only_se ? first : (first || newp);            /* l.316032-316043 */
        pk_rem_plunge(b, p, r0, zp, H, c14);
    }
    *td = 1;
    if (use_link) for (int i = 2; i < lk.n; i++) {               /* the rest of the link, at the depth */
        if (b->n == b->cap) { b->cap = b->cap ? 2 * b->cap : 256; b->m = realloc(b->m, (size_t)b->cap * sizeof *b->m); }
        b->m[b->n++] = lk.m[i];
    }
    free(lk.m);
    double zc = lastp(b).z;
    PkRemH Hl = pk_rem_h(p, z, z);                               /* set_heights(depth, depth), l.315790 / l.316405 */
    int last_depth = zi == nz - 1;
    for (int i = 0; i < q->g.n; i++) {
        int lf = i == 0 && lift_first, ll = i == q->g.n - 1 && lift_last;
        double ze = lf ? H.r : ll ? (last_depth ? Hl.c : Hl.r) : z;
        const Gt *e = &q->g.e[i];
        if (ll) {                                                /* l.316403-316428: before the record */
            pk_rem_up(b, p, Hl, last_depth);
            if (last_depth) *noend = 1;
            *td = 0; zc = lastp(b).z;
        }
        if (ze == z && zc == z) pk_gt(b, e, z, p->feed, ctol);
        else if (e->t == 'L') mv(b, 0, e->b.x, e->b.y, ze, p->feed);
        else if (e->t == 'A') pk_arc_moves(b, e, zc, ze, p->feed, ctol);
        zc = ze;
        if (lf) { PV en = gt_end(e); mv(b, 0, en.x, en.y, z, p->plunge_feed); zc = z; *td = 1; }   /* l.316463-316467 */
    }
    *any = 1; *lastk = q->k; *lastz = z;
}

static int pk_rem_gen(const McamFile *f, CamOp *op, MB *b, double minfil, double tol, double ctol) {
    CamParams *p = &op->p;
    char *err = op->error; size_t errn = sizeof op->error;
    if (pk_has_point(f, op)) { snprintf(err, errn, "pocket Remachining from a chained entry point: NOT DECODED"); return 0; }
    if (pkb(p, 0x126a)) { snprintf(err, errn, "pocket Remachining with Keep tool down (0x126a): the keep-tool-down link (FUN_1801cd200 l.318461, wr_pock_toolpath l.315662) NOT DECODED"); return 0; }
    if (pkb(p, 0x13d1)) { snprintf(err, errn, "pocket Remachining, Machine complete finish passes (0x13d1): the pass bounds' +0x50 parents (sort_pock_bounds p1 / p2 branches) NOT DECODED"); return 0; }
    int depth_on = p->mc_depth_on || p->stepdown > 1e-6;
    if (depth_on && p->mc_isl_depths) { snprintf(err, errn, "pocket Remachining with Use island depths (0x892): the per-depth branch (pock_proc_by_depth, FUN_1801c7970) NOT DECODED"); return 0; }
    if (depth_on && pkb(p, 0x1288) && (fabs(pkd(p, 0x1290)) > 1e-12 || fabs(pkd(p, 0x1298)) > 1e-12)) { snprintf(err, errn, "pocket Remachining with tapered walls (FUN_180056a70, FUN_1801d4620 taper branch l.323257): NOT DECODED"); return 0; }
    int nf = p->finish_n > 0 ? p->finish_n : 0, rough = !p->pocket_no_rough, lead_rough = pkb(p, 0x13d0), src = pks(p, 0x13b2);
    double fs = p->finish_step, D = p->tool_dia, R = D * 0.5, stock = p->stock_xy, step = D * p->stepover_pct / 100, clr = pkd(p, 0x13c8), rr = pkd(p, 0x13b8) * 0.5;
    double cell = D * 0.01 * pkd(p, 0x1270);                   /* +0x50 = D x 0.01 x +0x48: recompute_tool_dia_dependent_fields l.314920 */
    if (!(cell > 0)) { snprintf(err, errn, "pocket Remachining: the map cell (D x 0.01 x 0x1270) is 0: nothing to cut"); return 0; }   /* ours: init_pbmap divides by the cell (pb l.626) and fails: Mastercam's op then writes nothing (FUN_1801ec3c0 l.338562: uVar12 != 0) */
    PkFin F;
    if (!pk_fin_init(p, rough, 2, &F, err, errn)) return 0;    /* l.338502: method forced to 2 for the op (the finish page's checks); keep tool down / spring refused there */
    if (nf == 0 && lead_rough && !pk_fin_lead_init(p, &F, err, errn)) return 0;
    if (nf > 0 && F.ctl) { snprintf(err, errn, "pocket Remachining: compensation in control on the finish pieces (FUN_1801cd200 l.318649, FUN_1801cbef0 l.318723) NOT DECODED"); return 0; }
    if (nf > 0 && F.thin) { snprintf(err, errn, "pocket Remachining with thin wall finish (0x1436): NOT DECODED"); return 0; }
    if (nf > 0 && (pkb(p, 0x1434) || pkb(p, 0x1435))) { snprintf(err, errn, "pocket Remachining with a finish feed / spindle override (0x1434 / 0x1435): not ported"); return 0; }
    for (int i = 0; i < op->nchains; i++) if (!op->chains[i].closed && !op->chains[i].avoid) { snprintf(err, errn, "pocket Remachining on an open chain (FUN_1801ec130 l.338282 FUN_1801e5c70): NOT DECODED"); return 0; }
    Gls reg; PV mid;
    if (!pk_region(f, op, &reg, &mid, tol)) { snprintf(err, errn, "pocket needs closed planar chains"); return 0; }
    { const McSysTol *st = mc_systol_units(mcam_is_metric(f)); rm_tol = st->tol; rm_nc_tol = st->nc_tol; rm_chain_tol = st->chain_tol; }
    /* the depths of a chain pocket (FUN_1801c5250 l.313602-313612: set_depths(op, Z of the pocket's first non-island bound)): an incremental depth / top is on that Z, as gen_pocket_v2 */
    double *zs = malloc(sizeof(double) * (size_t)(reg.n ? reg.n : 1)); int nzs = 0;
    int flat = pk_isl_zs(f, op, zs, reg.n, &nzs, 0), by_z = flat && (p->dep_inc || p->top_inc) && nzs == reg.n;
    if (!flat) { free(zs); gls_free(&reg); snprintf(err, errn, "Warning - the boundary does not lie in the construction plane"); return 0; }   /* check_chains_3d l.313530-313541 (post_pock_warning 0x2309), run for type 3 on every chain (l.313523-313527) */
    int nouter = 0; for (int i = 0; i < reg.n; i++) if (!reg.l[i].island) nouter++;
    CamParams *cps = calloc((size_t)(nouter ? nouter : 1), sizeof(CamParams));
    double (*zk)[200] = calloc((size_t)(nouter ? nouter : 1), sizeof *zk); int *nzk = calloc((size_t)(nouter ? nouter : 1), sizeof(int));
    { int c = 0; for (int i = 0; i < reg.n; i++) { if (reg.l[i].island) continue; cps[c] = *p;
        if (by_z) pk_cp_for_z(&cps[c], p, zs[i]);
        int nr = 1; double rs = 0; nzk[c] = hst_depths(&cps[c], zk[c], 200, &nr, &rs); c++; } }
    free(zs);
    /* the first depth (FUN_1801c5250 l.313652-313654: pock_depth = the top one; wr_pock_depth_cuts copies to the others; ec3c0's 6th argument, disassembly 0x1801ec5ec) */
    double nct = g_nc_tol;
    double sort_tol = pkd(p, 0x1228);
    PkRemList L; memset(&L, 0, sizeof L);
    int fail = 0;
    /* each chain pocket: an outer loop with the islands inside it */
    int k = -1;
    for (int ci = 0; ci < reg.n && !fail; ci++) {
        if (reg.l[ci].island) continue;
        k++;
        double depth0 = zk[k][0];                               /* the first depth of this chain pocket */
        Gls cp = { 0 }; gls_push(&cp, gl_copy(&reg.l[ci]));
        for (int j = 0; j < reg.n; j++) { if (!reg.l[j].island) continue; Gls one = { &reg.l[ci], 1, 1 }; if (gls_inside(&one, gt_pt(&reg.l[j].e[0], 0), tol)) gls_push(&cp, gl_copy(&reg.l[j])); }
        double ymin = 1e38, ymax = -1e38;
        for (int i = 0; i < cp.n; i++) { RmB *c = pk_ov_rmb(&cp.l[i]); if (c->bbox[1] < ymin) ymin = c->bbox[1]; if (ymax < c->bbox[3]) ymax = c->bbox[3]; rm_b_free(c); }   /* l.338511-338523 */
        { double w = (ymax - ymin) * 0.5714285714285714; ymin -= w; ymax += w; }
        RmB *chains = pk_rem_list(&cp);
        RmMap cut, reach, rest2; memset(&cut, 0, sizeof cut); memset(&reach, 0, sizeof reach); memset(&rest2, 0, sizeof rest2);
        if (src < 2) {                                          /* FUN_1801ebc60 (source, ymin, ymax, cell, 1, -1, -1, depth, 0.0, map), disassembly 0x1801ec5dc-0x1801ec61c */
            rm_map_init(&cut, ymin, ymax, cell);
            if (!rm_prev_cut(f, op, src, depth0, &cut, 1, err, errn)) { rm_map_free(&cut); rm_b_free_list(&chains); gls_free(&cp); fail = 1; break; }
        } else pk_rem_ec130(&cp, chains, rr, stock, minfil, tol, ymin, ymax, cell, &cut);
        pk_rem_ec130(&cp, chains, R, stock, minfil, tol, ymin, ymax, cell, &reach);
        rm_map_diff(&reach, &cut, cell);
        if (getenv("PK_DEBUG")) fprintf(stderr, "rem: src %d rr %.4f R %.4f stock %.4f clr %.4f cell %.5f nf %d fs %.4f step %.4f depth0 %.4f | cut %.3f reach-cut %.3f\n", src, rr, R, stock, clr, cell, nf, fs, step, depth0, rm_map_area(&cut), rm_map_area(&reach));
        RmBnd *b1 = NULL, *b2 = NULL; RmB *restb = NULL;
        rm_map_trace(&reach, R, 0, &b1);
        rm_map_copy(&rest2, &reach);
        rm_verify_bnds(b1, R + clr, &reach, &rest2);
        rm_map_trace(&rest2, R, 0, &b2);
        rm_filter_bnds(&rest2, b2, cell * 0.5);
        rm_bnds_to_bounds(&rest2, b2, &restb);
        { short ng; rm_sort(&restb, 1, sort_tol, &ng); }
        rm_b_bboxs(restb);
        rm_bnds_free(b1); rm_bnds_free(b2);
        rm_map_free(&cut); rm_map_free(&reach); rm_map_free(&rest2);
        /* the tool-centre pockets of this chain pocket: extract_pocket(list, -2) (decomp/mccore_pock.c l.140-146): the first bound of the list not yet taken gives the group; its outer bound, then its islands in list order (l.152-182) */
        Gls tc_all = gls_offset(&cp, -(R + stock), minfil, tol);
        RmB *tc_list = pk_rem_list(&tc_all);
        /* FUN_1801cd200's bounds: FUN_1801edcd0 l.338630-338637 (the tool-centre bounds offset by 0 / nf x spacing: MCCECalcOffset, not ported: gls_offset) */
        Gls wf = gls_copy(&tc_all), wr = nf > 0 ? gls_offset(&tc_all, -(nf * fs), minfil, tol) : gls_copy(&tc_all);
        if (L.nz + 2 > 64) { snprintf(err, errn, "pocket Remachining: too many chain pockets"); fail = 1; }
        ZBs *zf_ = NULL, *zr_ = NULL;
        if (!fail) for (int w = 0; w < 2; w++) {
            const Gls *W = w ? &wr : &wf; ZB *zb = calloc((size_t)(W->n ? W->n : 1), sizeof(ZB));
            for (int a = 0; a < W->n; a++) pko_zb(&zb[a], &W->l[a], W->l[a].island);
            ZBs *z = malloc(sizeof *z); z->b = zb; z->n = W->n; L.zbs[L.nz] = z; L.zb[L.nz++] = zb; if (w) zr_ = z; else zf_ = z;
        }
        gls_free(&wf); gls_free(&wr);
        int *gid = malloc(sizeof(int) * (size_t)(tc_all.n + 1)); char *done = calloc((size_t)tc_all.n + 1, 1);
        for (int i = 0; i < tc_all.n; i++) {
            gid[i] = i;
            if (!tc_all.l[i].island) continue;
            for (int j = 0; j < tc_all.n; j++) { if (tc_all.l[j].island) continue; Gls one = { &tc_all.l[j], 1, 1 }; if (gls_inside(&one, gt_pt(&tc_all.l[i].e[0], 0), tol)) { gid[i] = j; break; } }
        }
        int sub = 0;
        for (int i = 0; i < tc_all.n && !fail; i++) {
            int g = gid[i]; if (done[g]) continue; done[g] = 1;
            if (tc_all.l[g].island) continue;
            Gls pocket = { 0 }; gls_push(&pocket, gl_copy(&tc_all.l[g]));
            for (int j = 0; j < tc_all.n; j++) if (j != g && gid[j] == g && tc_all.l[j].island) gls_push(&pocket, gl_copy(&tc_all.l[j]));
            if (!pk_rem_pocket(p, &F, &pocket, tc_list, chains, sort_tol, zf_, zr_, restb, nf, fs, step, rough, lead_rough, pkb(p, 0x123a), minfil, tol, nct, k, sub, &L, err, errn)) fail = 1;
            gls_free(&pocket); sub++;
        }
        free(gid); free(done);
        rm_b_free_list(&tc_list); rm_b_free_list(&chains);
        gls_free(&tc_all); gls_free(&cp);
        rm_b_free_list(&restb);
    }
    int any = 0;
    if (!fail) {
        /* sections, in wr_pock_toolpath's order (as gen_pocket_v2, pk_sec_cmp with the marker's sub-pocket and group fields): rough at every depth, finish at every depth with 0x1230 / 0x1436 else the last */
        int fin_all = pkb(p, 0x1230) || pkb(p, 0x1436);
        int nzmax = 1; for (int c = 0; c < nouter; c++) if (nzmax < nzk[c]) nzmax = nzk[c];
        int nsec = 0; PkSec *sec = malloc(sizeof *sec * (size_t)(L.n * nzmax + 1));
        for (int i = 0; i < L.n; i++) {
            const PkRemP *q = &L.a[i];
            int nz = nzk[q->k]; const double *zl = zk[q->k];
            for (int z = q->fin && !fin_all ? nz - 1 : 0; z < nz; z++) {
                PkSec *s = &sec[nsec++]; memset(s, 0, sizeof *s);
                s->fin = q->fin; s->k = q->k; s->sk = q->k; s->zi = z; s->j = i; s->depth = zl[z];     /* j: resolved below for the sort */
                s->z = zl[z]; s->sub = q->sub; s->p9 = q->group; s->pos = (long)z * 1000000L + i;
            }
        }
        /* the sort's pass key is the marker's pass; the section's piece index is kept in pos's low digits */
        for (int i = 0; i < nsec; i++) { int pi = sec[i].j; sec[i].j = L.a[pi].j; sec[i].thin = 0; sec[i].ti = 0; (void)pi; }
        g_pk_sec_fin = &F;
        if (nsec > 1) qsort(sec, nsec, sizeof *sec, pk_sec_cmp);
        int lastk = -1, td = 0, noend = 0; double lastz = zk[0][0];
        for (int i = 0; i < nsec; i++) {
            const PkRemP *q = &L.a[sec[i].pos % 1000000L]; const CamParams *cq = &cps[q->k];
            double ref = sec[i].zi > 0 ? zk[q->k][sec[i].zi - 1] : cq->top_z;   /* wr_pock_depth_cuts l.315181 */
            pk_rem_cut(b, cq, q, sec[i].z, ref, sec[i].zi, nzk[q->k], ctol, nct, &any, &lastk, &lastz, &td, &noend);
        }
        free(sec);
        if (!any) { snprintf(err, errn, "pocket Remachining: nothing to cut (no remaining stock this tool can reach)"); fail = 1; }
        else if (!noend) { const CamParams *cl = &cps[nouter ? nouter - 1 : 0]; PkRemH H = pk_rem_h(cl, lastz, cl->top_z); if ((p->mc_clr_on || p->mc_ret_on) && (p->ht_inc & 4)) { H.f = lastz + cl->feed_plane; if (H.f < lastz) H.f = lastz; } pk_rem_up(b, cl, H, 1); }   /* start_top_of_stock = the last set_depths call, the last chain pocket even if it wrote nothing (l.316545, mccore_depths.c l.461) */   /* the end of the op: set_heights(depth, top) l.316546, the feed plane l.316548-316553, clear_move l.316554 */
    }
    free(cps); free(zk); free(nzk);
    for (int i = 0; i < L.n; i++) gl_free(&L.a[i].g);
    for (int i = 0; i < L.nz; i++) { ZBs *z = L.zbs[i]; for (int a = 0; a < z->n; a++) { gl_free(&z->b[a].l); free(z->b[a].poly.xy); } free(z->b); free(z); }
    free(L.a);
    gls_free(&reg);
    return !fail;
}
