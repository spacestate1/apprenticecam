/* cam2d_pock.c - Pocket (opcode 3) engine ported from mcmill's pocket code (POCKMAIN.cpp, Pockhs.cpp, Pockentr.cpp,
 * Pockfin.cpp; decomp/mcmill_all.c, mccore pieces in decomp/mccore_pock*.c) on exact line / arc loops (pgeo.c).
 * Included by cam2d.c. What is ported, and what is refused (NOT DECODED), is listed at gen_pocket_v2.
 *
 * Parameters: pock_prm = record 0x1228 (set_pock_prm, l.48597 for ctour_prm; pocket the same block, opparams case 2):
 *   +0x10 0x1238 roughing on, +0x11 0x1239 finishing on, +0x14 0x123c cutting method, +0x18 stepover = D % x 0x1248
 *   (recompute_tool_dia_dependent_fields l.314885), +0x30 0x1258 spiral inside to outside, +0x32 0x125a finish passes,
 *   +0x38 0x1260 finish spacing, +0x78 0x12a0 entry on, +0x7a 0x12a2 entry type (0 helix, 1 ramp), +0x80 / +0x88 helix
 *   min / max radius = D % x 0x12f8 / 0x1300, +0x90 0x12b8 helix Z clearance, +0x98 0x12c0 plunge angle (rad), +0xa0
 *   0x12c8 XY clearance, +0xa8 0x12d0 helix direction (1 = CCW), +0xa9 0x12d1 skip pocket if the entry fails, +0x1d8
 *   0x1400 sharp corner smoothing length, +0x1e0 0x1408 channel mode, +0x8 0x1230 finish at every depth (pock_finish_all
 *   with +0x20e, GeneratePocketToolpath l.313520).
 */

#include "cam2d_remach.h"
static double pkd(const CamParams *p, int rec) { double v; memcpy(&v, p->pk_p + (rec - 0x1228), 8); return v; }
static int pks(const CamParams *p, int rec) { int16_t v; memcpy(&v, p->pk_p + (rec - 0x1228), 2); return v; }
static int pkb(const CamParams *p, int rec) { return p->pk_p[rec - 0x1228]; }

/* ------------------------------------------------------------------------------------------------ emission */
static void pk_arc_moves(MB *b, const Gt *e, double z0, double z1, double feed, double tol) {
    int from = b->n;
    int m = (int)ceil(fabs(e->sw) / (2 * acos(1 - fmin(tol / fmax(e->r, 1e-9), 1.0))));
    if (m < 2) m = 2;
    if (m > 2000) m = 2000;
    for (int k = 1; k <= m; k++) { PV q = gt_pt(e, (double)k / m); mv(b, 0, q.x, q.y, z0 + (z1 - z0) * k / m, feed); }
    mb_tag_arc(b, from, e->c.x, e->c.y, e->sw < 0);
}
/* one entity at depth z (lines as one move, arcs as a true arc record split at full turns) */
static void pk_gt(MB *b, const Gt *e, double z, double feed, double tol) {
    if (e->t == 'L') { mv(b, 0, e->b.x, e->b.y, z, feed); return; }
    if (e->t != 'A') return;
    double sw = e->sw; int turns = (int)floor(fabs(sw) / (2 * M_PI) + 1e-9);
    if (fabs(sw) < 2 * M_PI + 1e-9) { pk_arc_moves(b, e, z, z, feed, tol); return; }
    Gt t = *e; double s = sw > 0 ? 1 : -1, done = 0;
    for (int i = 0; i < turns; i++) { t.sa = e->sa + s * 2 * M_PI * i; t.sw = s * 2 * M_PI; pk_arc_moves(b, &t, z, z, feed, tol); done += 2 * M_PI; }
    if (fabs(sw) - done > 1e-9) { t.sa = e->sa + s * done; t.sw = sw - s * done; pk_arc_moves(b, &t, z, z, feed, tol); }
}
static void pk_loop(MB *b, const Gl *l, double z, double feed, double tol) { for (int i = 0; i < l->n; i++) pk_gt(b, &l->e[i], z, feed, tol); }

/* ------------------------------------------------------------------------------------------------ region */
/* a closed chain as an exact loop in its own direction */
static int pk_chain_gl(const McamFile *f, const Chain *ch, Gl *out) {
    memset(out, 0, sizeof *out);
    for (int i = 0; i < ch->n; i++) {
        const Ent *e = &f->ents[ch->ent[i]]; Gt g;
        if (e->type == E_LINE) g = gt_line(pv(e->a.x, e->a.y), pv(e->b.x, e->b.y));
        else if (e->type == E_ARC) { if (!e->planar) { gl_free(out); return 0; } g = gt_arc(pv(e->c.x, e->c.y), e->r, e->a0, e->sw); }
        else continue;
        if (ch->rev && ch->rev[i]) g = gt_rev(&g);
        if (gt_len(&g) < 1e-12) continue;
        gl_push(out, g);
    }
    if (out->n == 0) return 0;
    for (int i = 0; i < out->n; i++) {                       /* snap the lines' ends onto their neighbours */
        Gt *g = &out->e[i], *h = &out->e[(i + 1) % out->n];
        PV pe = gt_end(g), ps = gt_start(h);
        if (hypot(pe.x - ps.x, pe.y - ps.y) > 1e-3 * (1 + gl_len(out))) { gl_free(out); return 0; }   /* not closed */
        if (h->t == 'L') h->a = pe; else if (g->t == 'L') g->b = ps;
    }
    return 1;
}
/* closed chains -> region loops (outer counter-clockwise, islands clockwise; nesting by containment, as
   sort_pock_bounds' nested chains: odd depth = island). first_mid = the first chain's first entity midpoint. */
static int pk_region(const McamFile *f, const CamOp *op, Gls *reg, PV *first_mid, double tol) {
    memset(reg, 0, sizeof *reg); int got = 0;
    for (int i = 0; i < op->nchains; i++) {
        const Chain *ch = &op->chains[i];
        if (!ch->closed || ch->avoid) continue;
        Gl l; if (!pk_chain_gl(f, ch, &l)) continue;
        if (!got) { *first_mid = gt_pt(&l.e[0], 0.5); got = 1; }
        gls_push(reg, l);
    }
    for (int i = 0; i < reg->n; i++) {
        PV q = gt_pt(&reg->l[i].e[0], 0.5); int depth = 0;
        for (int j = 0; j < reg->n; j++) if (j != i) { Gls one = { &reg->l[j], 1, 1 }; if (gls_inside(&one, q, tol)) depth++; }
        reg->l[i].island = depth & 1;
        double a = gl_area(&reg->l[i]);
        if ((a > 0) == reg->l[i].island) gl_reverse(&reg->l[i]);
    }
    return reg->n;
}
static int pk_count(const Gls *s) { int n = 0; for (int i = 0; i < s->n; i++) if (!s->l[i].island && s->l[i].n) n++; return n; }

/* the region minus its islands, rebuilt exactly (merge_pock_bounds role: loops of one list unioned, islands cut out) */
/* (was: Clipper union at distance 0, in which an island reaching outside the outer loop is a filled -1 winding region: the phantom loop that hung
   Standard pockets, N9-12) merge_pock_bounds = mccore FUN_1805e02e0 (cam2d_remach.c rm_merge_pock, checked against the DLL) */
static Gl pk_rb2gl(const RmB *b) {
    Gl l = { 0 };
    for (int i = 0; i < b->n; i++) { const RmEnt *e = &b->e[i]; gl_push(&l, e->id == 'A' ? gt_arc(pv(e->a[0], e->a[1]), e->a[2], e->a[3], e->a[4]) : gt_line(pv(e->a[0], e->a[1]), pv(e->a[2], e->a[3]))); }
    l.island = b->isl;
    return l;
}
static RmB *pk_gl2rb(const Gl *l) {
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
static Gls pk_merge(const Gls *outer, const Gls *isl, double tol) {
    (void)tol;
    RmB *list = NULL;
    for (int i = 0; i < outer->n; i++) rm_b_append(&list, pk_gl2rb(&outer->l[i]));
    for (int i = 0; i < isl->n; i++) rm_b_append(&list, pk_gl2rb(&isl->l[i]));
    rm_merge_pock(&list, 0);
    Gls r = { 0 };
    for (RmB *b = list; b; b = b->next) gls_push(&r, pk_rb2gl(b));
    rm_b_free_list(&list);
    return r;
}

/* FUN_1801d2b80 / FUN_1801d29b0: sharp corner smoothing of each non-island loop: ccomp_pock inward by s, then back
   out by s (an opening, convex corners rounded to radius s); when the inward offset is empty s is halved (two tries) */
static void pk_smooth(Gls *s, double len, double minfil, double tol) {
    if (len <= 0) return;
    Gls out = { 0 };
    for (int i = 0; i < s->n; i++) {
        if (s->l[i].island) { gls_push(&out, gl_copy(&s->l[i])); continue; }
        Gls one = { 0 }; gls_push(&one, gl_copy(&s->l[i]));
        double d = len; Gls res = { 0 };
        for (int k = 0; k < 2 && !res.n; k++, d *= 0.5) {
            Gls in = gls_offset(&one, -d, minfil, tol);
            if (in.n) res = gls_offset(&in, d, minfil, tol);
            gls_free(&in);
        }
        if (res.n) for (int j = 0; j < res.n; j++) gls_push(&out, res.l[j]);
        else gls_push(&out, gl_copy(&s->l[i]));
        free(res.l); gls_free(&one);
    }
    gls_free(s); *s = out;
}

/* rotate a loop to start at the entity start nearest q (FUN_1801ce870 with a position: get_ep / reorder_pock_bound) */
static double pk_start_near_ent(Gl *l, PV q) {
    int best = 0; double bd = 1e300;
    for (int i = 0; i < l->n; i++) { PV s = gt_start(&l->e[i]); double d = hypot(s.x - q.x, s.y - q.y); if (d < bd) { bd = d; best = i; } }
    gl_rotate(l, best);
    return bd;
}
/* FUN_1801d19d0 propagate_pock_bound_start_points: start at the point nearest q (the entity split there) */
static void pk_start_near_pt(Gl *l, PV q, double tol) {
    int best = -1; double bt = 0, bd = 1e300;
    for (int i = 0; i < l->n; i++) {
        if (l->e[i].t == 'P') continue;
        double t = gt_t_near(&l->e[i], q); PV x = gt_pt(&l->e[i], t); double d = hypot(x.x - q.x, x.y - q.y);
        if (d < bd - tol) { bd = d; best = i; bt = t; }
    }
    if (best < 0) return;
    double len = gt_len(&l->e[best]);
    if (len * bt < tol) { gl_rotate(l, best); return; }
    if ((1 - bt) * len < tol) { gl_rotate(l, (best + 1) % l->n); return; }
    gl_split_start(l, best, bt);
}

/* ------------------------------------------------------------------------------------------------ High Speed */
typedef struct { Gl *l; int n, cap; } GlList;
static void gll_push(GlList *g, Gl l) { if (g->n == g->cap) { g->cap = g->cap ? g->cap * 2 : 16; g->l = realloc(g->l, g->cap * sizeof(Gl)); } g->l[g->n++] = l; }
static void gll_free(GlList *g) { for (int i = 0; i < g->n; i++) gl_free(&g->l[i]); free(g->l); g->l = NULL; g->n = g->cap = 0; }
/* High Speed extra loops (cam2d_pock_hsx.c): loops (island flag = "small", FUN_1801d00d0 +2) and their owner (par:
   while computing the tag of the owner loop, in the result the index of the owner in the cut order) */
typedef struct { Gls l; int *par; int cap; } PkXs;
static void pkx_push(PkXs *x, Gl l, int par) {
    if (x->l.n == x->cap) { x->cap = x->cap ? x->cap * 2 : 16; x->par = realloc(x->par, x->cap * sizeof(int)); }
    x->par[x->l.n] = par; gls_push(&x->l, l);
}
static void pkx_free(PkXs *x) { gls_free(&x->l); free(x->par); memset(x, 0, sizeof *x); }
static void pk_hs_extras(const Gls *res, const Gls *next, double dia, double minfil, double smooth, double ftol, double tol, Gls *out);
static double pk_cea40(const Gl *a, const Gl *b);

/* hs_parallel_spiral_pocket FUN_1801d00d0 (l.320359), channel mode 0: the loops of one pocket in cut order.
   bounds: the rough boundary (outer loop(s) + islands). Steps (all cited lines are decomp/mcmill_all.c):
   - smoothing of the start loops (FUN_1801d2b80) when 0x1400 > nc_tol;
   - several outer loops: the first is cut, the others wait (FUN_1801ce870 with param_4 = 0, l.320505);
   - repeat: write the current loop (FUN_1801eb050), offset it inward by the stepover (FUN_1801d1850 l.321175: ccomp_pock,
     halved once when empty; loops larger than their parent dropped, FUN_1801ce7d0), smooth, take the new loop whose
     entity start is nearest the written loop's start (FUN_1801ce870 + reorder_pock_bound), merge it with the islands
     (merge_pock_bounds), smooth; several pieces -> nearest first, the others wait; no loop -> the waiting loop nearest
     the last start (l.320553-320575);
   - read back: the temp file is a backward-linked list (FUN_1801eaca0), so the cut order is the reverse of the write
     order (inside to outside) unless 0x1258 = 0, when FUN_1801d2310 reverses it again (l.320561);
   - FUN_1801d19d0 (l.321231): each loop then starts at the point nearest the previous loop's end.
   - stepover used (after the halving) strictly between D / 2 and D: the extra loops of FUN_1801d1740 (l.320598-320690,
     cam2d_pock_hsx.c pk_hs_extras), owned by the current loop (inside to outside) or the nearest next loop (outside to
     inside: carried with it - local_1c0 / local_198 - and re-owned after its merge, FUN_1801d28b0), returned in xo. */
static int pk_hs_loops(const Gls *bounds, double step, double smooth, double minfil, int in2out, double tol, double ftol,
                       double dia, PkXs *xo, GlList *out, char *err, size_t errn) {
    Gls res = { 0 }, isl = { 0 }, pend = { 0 };
    for (int i = 0; i < bounds->n; i++) gls_push(bounds->l[i].island ? &isl : &res, gl_copy(&bounds->l[i]));
    if (smooth > ftol) pk_smooth(&res, smooth, minfil, tol);
    while (res.n > 1) { gls_push(&pend, res.l[res.n - 1]); res.n--; }   /* the first loop is cut first */
    GlList wr = { 0 }; PV last = pv(0, 0); int guard = 0;
    PkXs xr = { 0 }, carry = { 0 }, wait = { 0 }; int tagn = 0;   /* extras: result, local_1c0, local_198 */
    while (guard++ < 100000) {
        if (pk_count(&res) == 0) {
            if (!pend.n) break;
            int bi = 0; double bd = 1e300;
            for (int i = 0; i < pend.n; i++) { Gl t = gl_copy(&pend.l[i]); double d = pk_start_near_ent(&t, last); if (d < bd) { bd = d; bi = i; } gl_free(&t); }
            Gl pick = pend.l[bi]; pend.l[bi] = pend.l[--pend.n];
            pk_start_near_ent(&pick, last);
            if (wait.l.n) {               /* l.320670-320681: the waiting extras of this loop are carried with it */
                PkXs w2 = { 0 };
                for (int i = 0; i < wait.l.n; i++) pkx_push(wait.par[i] == pick.tag && pick.tag ? &carry : &w2, wait.l.l[i], wait.par[i]);
                free(wait.l.l); free(wait.par); wait = w2;
            }
            gls_free(&res); gls_push(&res, pick);
            continue;
        }
        /* offset by the stepover (halved once when nothing is left) */
        Gls next = { 0 }; double used = step;   /* local_190: the stepover FUN_1801d1850 used */
        for (int i = 0; i < res.n; i++) {
            if (res.l[i].island) continue;
            Gls one = { 0 }; gls_push(&one, gl_copy(&res.l[i]));
            double d = step; Gls o = { 0 };
            for (int k = 0; k < 2 && !o.n; k++, d *= 0.5) { gls_free(&o); o = gls_offset(&one, -d, minfil, tol); if (o.n) used = d; }
            double pa = fabs(gl_area(&res.l[i]));
            for (int j = 0; j < o.n; j++) {
                if (fabs(gl_area(&o.l[j])) > pa) { gl_free(&o.l[j]); continue; }   /* FUN_1801ce7d0 */
                gls_push(&next, o.l[j]);
            }
            free(o.l); gls_free(&one);
        }
        if (smooth > ftol) pk_smooth(&next, smooth, minfil, tol);
        if (getenv("PK_DEBUG")) { fprintf(stderr, "iter: res %d (outer %d) -> next %d pend %d isl %d\n", res.n, pk_count(&res), next.n, pend.n, isl.n); }
        /* the extra loops (l.320598-320690) */
        Gls ex = { 0 };
        if (xo && next.n && used > dia * 0.5 && used < dia) pk_hs_extras(&res, &next, dia, minfil, smooth, ftol, tol, &ex);
        /* write this level's loops (with their extras: l.320718-320732) */
        for (int i = 0; i < res.n; i++) if (!res.l[i].island && res.l[i].n) {
            gll_push(&wr, gl_copy(&res.l[i])); last = gt_start(&res.l[i].e[0]);
            if (in2out) for (int k = 0; k < ex.n; k++) pkx_push(&xr, gl_copy(&ex.l[k]), wr.n - 1);
            else for (int k = 0; k < carry.l.n; k++) pkx_push(&xr, gl_copy(&carry.l.l[k]), wr.n - 1);
        }
        gls_free(&res);
        if (!in2out) { pkx_free(&carry); }
        /* the next loop: entity start nearest the last start */
        Gls outer = { 0 };
        for (int i = 0; i < next.n; i++) if (!next.l[i].island) gls_push(&outer, gl_copy(&next.l[i]));
        gls_free(&next);
        if (!in2out && ex.n) {            /* FUN_1801d28b0 (outside to inside): the owner is the nearest next loop */
            for (int i = 0; i < outer.n; i++) outer.l[i].tag = ++tagn;
            for (int k = 0; k < ex.n; k++) {
                int own = outer.n == 1 ? outer.l[0].tag : 0; double bdd = 1e38;
                if (outer.n != 1) for (int i = 0; i < outer.n; i++) { double d = pk_cea40(&ex.l[k], &outer.l[i]); if (d < bdd + CAM_SYS_TOL) { bdd = d; own = outer.l[i].tag; } }
                pkx_push(&wait, ex.l[k], own);
            }
            free(ex.l); memset(&ex, 0, sizeof ex);
        }
        gls_free(&ex);
        if (!outer.n) { gls_free(&outer); continue; }
        int bi = 0; double bd = 1e300;
        for (int i = 0; i < outer.n; i++) { Gl t = gl_copy(&outer.l[i]); double d = pk_start_near_ent(&t, last); if (d < bd) { bd = d; bi = i; } gl_free(&t); }
        for (int i = 0; i < outer.n; i++) if (i != bi) gls_push(&pend, gl_copy(&outer.l[i]));
        if (wait.l.n) {                   /* l.320748-320761: the extras of the picked loop go with it (local_1c0) */
            PkXs w2 = { 0 }; int tg = outer.l[bi].tag;
            for (int i = 0; i < wait.l.n; i++) pkx_push(wait.par[i] == tg && tg ? &carry : &w2, wait.l.l[i], wait.par[i]);
            free(wait.l.l); free(wait.par); wait = w2;
        }
        Gls cur = { 0 }; gls_push(&cur, gl_copy(&outer.l[bi])); gls_free(&outer);
        pk_start_near_ent(&cur.l[0], last);
        if (isl.n) {
            PV keep = gt_start(&cur.l[0].e[0]);
            Gls m = pk_merge(&cur, &isl, tol); gls_free(&cur); cur = m;
            for (int i = 0; i < cur.n; i++) if (!cur.l[i].island) pk_start_near_ent(&cur.l[i], keep);
            if (smooth > ftol) pk_smooth(&cur, smooth, minfil, tol);
        }
        if (carry.l.n) {                  /* FUN_1801d28b0(local_1c0, 0, merged): re-owned by the nearest merged loop */
            for (int i = 0; i < cur.n; i++) cur.l[i].tag = ++tagn;
            int nn = pk_count(&cur), one = -1;
            for (int i = 0; i < cur.n; i++) if (!cur.l[i].island) { one = cur.l[i].tag; break; }
            for (int k = 0; k < carry.l.n; k++) {
                if (nn == 1) { carry.par[k] = one; continue; }
                double bdd = 1e38;
                for (int i = 0; i < cur.n; i++) { double d = pk_cea40(&carry.l.l[k], &cur.l[i]); if (d < bdd + CAM_SYS_TOL) { bdd = d; carry.par[k] = cur.l[i].tag; } }
            }
        }
        /* several pieces: the nearest is cut next, the others wait */
        Gls o2 = { 0 }; int found = 0; double bd2 = 1e300; int bj = -1;
        for (int i = 0; i < cur.n; i++) if (!cur.l[i].island) { Gl t = gl_copy(&cur.l[i]); double d = pk_start_near_ent(&t, last); if (d < bd2) { bd2 = d; bj = i; } gl_free(&t); }
        if (carry.l.n && pk_count(&cur) > 1) {   /* l.320797-320808 as decoded: every carried extra leaves local_1c0, those
                                                    of another loop wait (local_198), those of the picked loop are dropped */
            PkXs w2 = wait; int tg = bj >= 0 ? cur.l[bj].tag : 0;
            for (int k = 0; k < carry.l.n; k++) { if (carry.par[k] != tg) pkx_push(&w2, carry.l.l[k], carry.par[k]); else gl_free(&carry.l.l[k]); }
            wait = w2; free(carry.l.l); free(carry.par); memset(&carry, 0, sizeof carry);
        }
        for (int i = 0; i < cur.n; i++) {
            if (cur.l[i].island) { gl_free(&cur.l[i]); continue; }
            if (i == bj) { pk_start_near_ent(&cur.l[i], last); gls_push(&o2, cur.l[i]); found = 1; }
            else gls_push(&pend, cur.l[i]);
        }
        free(cur.l);
        res = o2; (void)found;
    }
    gls_free(&res); gls_free(&isl); gls_free(&pend); pkx_free(&carry); pkx_free(&wait);
    if (guard >= 100000) { snprintf(err, errn, "pocket: High Speed loop limit"); gll_free(&wr); pkx_free(&xr); return 0; }
    /* cut order */
    if (in2out) for (int i = 0, j = wr.n - 1; i < j; i++, j--) { Gl t = wr.l[i]; wr.l[i] = wr.l[j]; wr.l[j] = t; }
    if (in2out) for (int k = 0; k < xr.l.n; k++) xr.par[k] = wr.n - 1 - xr.par[k];
    if (xo) *xo = xr; else pkx_free(&xr);
    for (int i = 1; i < wr.n; i++) { Gl *pl = &wr.l[i - 1]; pk_start_near_pt(&wr.l[i], gt_end(&pl->e[pl->n - 1]), CAM_SYS_TOL); }
    if (getenv("PK_DEBUG")) for (int i = 0; i < wr.n; i++) { fprintf(stderr, "loop %d: %d ents area %.5f\n", i, wr.l[i].n, gl_area(&wr.l[i]));
        for (int k = 0; k < wr.l[i].n; k++) { Gt *e = &wr.l[i].e[k]; PV a = gt_start(e), z = gt_end(e); fprintf(stderr, "  %c %.4f %.4f -> %.4f %.4f %s\n", e->t, a.x, a.y, z.x, z.y, e->t == 'A' ? "arc" : ""); } }
    *out = wr;
    return 1;
}

/* FUN_1801d21a0: a point is bad when it is outside an outer bound or strictly inside an island (pt_in_ctour with
   nc_tol: on the boundary is fine) */
static int pk_pt_ok(const Gls *bounds, PV q, double tol) {
    for (int i = 0; i < bounds->n; i++) if (gl_closest(&bounds->l[i], q, NULL, NULL, NULL) <= tol) return 1;
    return gls_inside(bounds, q, tol);
}
/* FUN_1801d2c60: a transition loop is usable when it crosses no bound (pock_bound_intersect: transversal
   intersections, ints_on_gt_new) and its entities' start / middle / end points (and, for entities longer than
   0.3 stepover, len x 10 / stepover + 1 points) are inside */
static int pk_inside_all(const Gls *bounds, const Gl *t, double tol, double step) {
    for (int i = 0; i < bounds->n; i++)
        for (int j = 0; j < t->n; j++)
            for (int k = 0; k < bounds->l[i].n; k++) if (gt_cross(&t->e[j], &bounds->l[i].e[k], tol)) return 0;
    for (int i = 0; i < t->n; i++) for (int k = 0; k <= 2; k++) if (!pk_pt_ok(bounds, gt_pt(&t->e[i], k * 0.5), tol)) return 0;
    for (int i = 0; i < t->n; i++) {
        int m = (int)(gt_len(&t->e[i]) * 10.0 / step);
        if (m + 1 > 3) for (int k = 0; k <= m; k++) if (!pk_pt_ok(bounds, gt_pt(&t->e[i], (double)k / m), tol)) return 0;
    }
    return 1;
}
/* the entry link rule of this port (ours, unchanged, cam2d_pock_entry.c: a link that leaves = the entry fails, because find_path_around_island is NOT DECODED): no crossing away from the move ends, midpoint inside. Stricter FUN_1801e0fd0 (pk_line_inside below) would change the entries of existing Standard pockets, whose real link would go around the island */
static int pk_line_inside_ent(const Gls *bounds, PV a, PV b, double tol) {
    Gt seg = gt_line(a, b);
    for (int j = 0; j < bounds->n; j++) for (int k = 0; k < bounds->l[j].n; k++) if (gt_cross(&seg, &bounds->l[j].e[k], tol)) {
        /* a crossing at an end of the move (the start on a loop) does not count */
        const Gt *e = &bounds->l[j].e[k];
        if (gl_closest(&(Gl){ (Gt *)e, 1, 1, 0, 0 }, a, NULL, NULL, NULL) <= tol || gl_closest(&(Gl){ (Gt *)e, 1, 1, 0, 0 }, b, NULL, NULL, NULL) <= tol) continue;
        return 0;
    }
    return pk_pt_ok(bounds, pv((a.x + b.x) / 2, (a.y + b.y) / 2), tol);
}

static int pk_line_inside(const Gls *bounds, PV a, PV b, double tol);

/* FUN_1801d1010 make_channel_loop_boundary / FUN_1801d13a0 make_s_shaped_boundary: two half circles from P2 to P3 */
static Gl pk_channel(int dir, PV p2, PV p3) {
    Gl l = { 0 };
    PV d = pv((p2.x - p3.x) * 0.5, (p2.y - p3.y) * 0.5), E = pv(p2.x + d.x, p2.y + d.y);
    PV c1 = pv((p2.x + E.x) / 2, (p2.y + E.y) / 2), c2 = pv((p3.x + E.x) / 2, (p3.y + E.y) / 2);
    double r1 = hypot(p2.x - c1.x, p2.y - c1.y), r2 = hypot(E.x - c2.x, E.y - c2.y), sw = dir ? -M_PI : M_PI;
    gl_push(&l, gt_arc(c1, r1, atan2(p2.y - c1.y, p2.x - c1.x), sw));
    gl_push(&l, gt_arc(c2, r2, atan2(E.y - c2.y, E.x - c2.x), sw));
    return l;
}
static Gl pk_sshape(int dir, PV p2, PV p3) {
    Gl l = { 0 };
    PV M = pv((p2.x + p3.x) / 2, (p2.y + p3.y) / 2), c1 = pv((p2.x + M.x) / 2, (p2.y + M.y) / 2), c2 = pv((p3.x + M.x) / 2, (p3.y + M.y) / 2);
    double r1 = hypot(p2.x - c1.x, p2.y - c1.y), r2 = hypot(M.x - c2.x, M.y - c2.y);
    gl_push(&l, gt_arc(c1, r1, atan2(p2.y - c1.y, p2.x - c1.x), dir ? M_PI : -M_PI));
    gl_push(&l, gt_arc(c2, r2, atan2(M.y - c2.y, M.x - c2.x), dir ? -M_PI : M_PI));
    return l;
}

/* ------------------------------------------------------------------------------------------------ entry */
/* find_helix_center FUN_1801c04d0 (l.310452): radii from the maximum to the minimum in 11 steps
   (r = (1 - 0.1 s) max + 0.1 s min); the rough boundary offset inward by XY clearance + r; on each loop the point
   nearest the cut's start is a candidate when it is inside and the circle of radius max(0.95 XY clearance + r -
   2 nc_tol, nc_tol) about it crosses no boundary; the nearest candidate wins, the first radius with one ends the search */
static int pk_helix_center(const Gls *rb, PV start, double rmax, double rmin, double xyclr, double minfil, double tol, double nctol, PV *c, double *r) {
    for (int s = 0; s < 11; s++) {
        double rr = (1 - 0.1 * s) * rmax + 0.1 * s * rmin;
        Gls o = gls_offset(rb, -(xyclr + rr), minfil, tol);
        int ok = 0; double bd = 1e300;
        for (int i = 0; i < o.n; i++) {
            PV q; gl_closest(&o.l[i], start, NULL, NULL, &q);
            if (!gls_inside(rb, q, tol)) continue;
            Gl circ = { 0 }; double cr = fmax(xyclr * 0.95 + rr - 2 * nctol, nctol);
            gl_push(&circ, gt_arc(q, cr, 0, 2 * M_PI));
            int hit = 0; for (int k = 0; k < rb->n && !hit; k++) hit = gl_cross(&rb->l[k], &circ, tol);
            if (!hit && gls_inside(rb, gt_pt(&circ.e[0], 0), tol)) {
                double d = hypot(q.x - start.x, q.y - start.y); if (d < bd) { bd = d; *c = q; ok = 1; }
            }
            gl_free(&circ);
        }
        gls_free(&o);
        if (ok) { *r = rr; return 1; }
    }
    return 0;
}

#include "cam2d_pock_tspiral.c"   /* cutting method 5 True Spiral (FUN_1801ef3d0) */
#include "cam2d_pock_ovlp.c"      /* cutting method 1 Constant Overlap Spiral (FUN_1801e8650, the mccore pbmap engine of cam2d_remach.c) */
/* Island facing (cam2d_pock_isl.c): the entry search of a top pocket (FUN_1801c1fc0) */
static const Gls *g_pk_isl_chk;                                     /* the check bounds of the top pocket being planned (FUN_1801c68e0 check_bounds_face_isl), NULL otherwise */
static const CamParams *g_pk_ep;                                    /* the section's parameters for the entry / zigzag plans (Island facing, per depth), NULL = the op's */
static int pk_isl_out(const CamParams *p, const Gl *outer, const Gls *chk, PV S, double dist, double minfil, double tol, PV *Q);

/* Facing pocket type (pock_prm +0x1d2 = 1): FUN_1801bfd60 / FUN_1801bfde0 (l.310158 / 310185) entry and FUN_1801c0190 (l.310339) exit: a point outside the
   first outer pocket bound. The bound offset outward by dist (ccomp_pock: side 'R' when ccw, 'L' otherwise), then: spiral methods proj_pt_to_pock_bound (closest point) when
   the move stays clear of the chain islands (FUN_1801c03c0), else lines through S at angles (0, 90, 45, 135, 10..80, 100..170, l.310196-310218, both directions of each) through
   pock_bound_line_inters: the nearest intersection whose move is clear; stops at the first angle with a hit. Clear: the capsule of radius R around S->Q does not cross
   any chain island (make_entry_exit_envelope FUN_1801c2770, l.311782, intersect_pock_bound, l.310430: fewer than 2 points). */
static const Gls *g_pk_face_chain;
static int pk_spiral_cutting(const CamParams *p) { int m = p->pk_ok ? pks(p, 0x123c) : 8; return m != 0 && m != 6; }   /* pock_spiral_cutting: 0 Zigzag, 6 One Way are not spiral (machine_pocket l.322534) */
static int pk_face_clear(const Gls *chain, PV a, PV b, double R, double nctol) {
    if (!chain) return 1;
    PV d = pv(b.x - a.x, b.y - a.y); double L = hypot(d.x, d.y);
    Gl cap = { 0 };
    if (L < 1e-12) { gl_push(&cap, gt_arc(a, R, 0, 2 * M_PI)); }
    else {
        PV n = pv(-d.y / L * R, d.x / L * R);
        PV a1 = pv(a.x + n.x, a.y + n.y), b1 = pv(b.x + n.x, b.y + n.y), b2 = pv(b.x - n.x, b.y - n.y), a2 = pv(a.x - n.x, a.y - n.y);
        gl_push(&cap, gt_line(a1, b1));
        { double ang = atan2(n.y, n.x); gl_push(&cap, gt_arc(b, R, ang, -M_PI)); }
        gl_push(&cap, gt_line(b2, a2));
        { double ang = atan2(-n.y, -n.x); gl_push(&cap, gt_arc(a, R, ang, -M_PI)); }
    }
    int ok = 1;
    for (int i = 0; i < chain->n && ok; i++) if (chain->l[i].island && gl_cross(&chain->l[i], &cap, nctol)) ok = 0;
    gl_free(&cap);
    return ok;
}
static int pk_face_ray(const Gl *o, PV S, double ang, PV *x, int cap) {
    PV d = pv(cos(ang), sin(ang)); int n = 0;
    for (int k = 0; k < o->n; k++) {
        const Gt *e = &o->e[k];
        if (e->t == 'L') {
            PV u = pv(e->b.x - e->a.x, e->b.y - e->a.y); double den = d.x * u.y - d.y * u.x;
            if (fabs(den) < 1e-15) continue;
            double w = ((e->a.x - S.x) * d.y - (e->a.y - S.y) * d.x) / den;
            if (w > -2e-7 && w < 1.0000002 && n < cap) x[n++] = pv(e->a.x + u.x * w, e->a.y + u.y * w);
        } else if (e->t == 'A') {
            PV w = pv(S.x - e->c.x, S.y - e->c.y);
            double bb = w.x * d.x + w.y * d.y, disc = bb * bb - (w.x * w.x + w.y * w.y - e->r * e->r);
            if (disc < 0) continue;
            double sq = sqrt(disc);
            for (int s = -1; s <= 1; s += 2) {
                double t = -bb + s * sq; PV y = pv(S.x + d.x * t, S.y + d.y * t);
                double a = atan2(y.y - e->c.y, y.x - e->c.x);
                double rel = e->sw > 0 ? fmod(fmod(a - e->sa, 2 * M_PI) + 2 * M_PI, 2 * M_PI) : fmod(fmod(e->sa - a, 2 * M_PI) + 2 * M_PI, 2 * M_PI);
                if (rel <= fabs(e->sw) && n < cap) x[n++] = y;
                if (disc == 0) break;
            }
        }
    }
    return n;
}
static int pk_face_out(const Gls *chain, const Gl *outer, PV S, double dist, int spiral, double R, double minfil, double tol, double nctol, PV *Q) {
    Gls one = { 0 }; gls_push(&one, gl_copy(outer));
    Gls o = gls_offset(&one, dist, minfil, tol);                   /* outward: + grows a counter-clockwise outer loop */
    gls_free(&one);
    if (!o.n) { gls_free(&o); return 0; }
    int ok = 0;
    if (spiral) {
        PV best = pv(0, 0); double bd = 1e38;
        for (int i = 0; i < o.n; i++) { PV q; double d = gl_closest(&o.l[i], S, NULL, NULL, &q); if (d < bd) { bd = d; best = q; } }
        if (bd < 1e38 && pk_face_clear(chain, S, best, R, nctol)) { *Q = best; ok = 1; }
    }
    if (!ok) {
        static const double ang[20] = { 0, 90, 45, 135, 10, 20, 30, 40, 50, 60, 70, 80, 100, 110, 120, 130, 140, 150, 160, 170 };
        for (int a = 0; a < 20 && !ok; a++) {
            double best = 1e38; PV x[64]; int n = 0;
            for (int i = 0; i < o.n; i++) n += pk_face_ray(&o.l[i], S, ang[a] * M_PI / 180.0, x + n, 64 - n);
            for (int i = 0; i < n; i++) {
                double d = hypot(x[i].x - S.x, x[i].y - S.y);
                if ((d < best || !ok) && pk_face_clear(chain, S, x[i], R, nctol)) { *Q = x[i]; best = d; ok = 1; }
            }
        }
    }
    gls_free(&o);
    return ok;
}
/* FUN_1801d4850 l.323068-323100, Facing: each outer bound outward by pock_prm +0x160 (0x1388 overlap) - R (+ finish passes x spacing when finishing), each island by
   R + stock (comp_pock_bnd with flag 0 = away from the loop's inside); then merge_pock_bounds. */
static Gls pk_face_tc(const Gls *reg, double dout, double tc_off, double minfil, double tol) {
    Gls outer = { 0 }, isl = { 0 };
    for (int i = 0; i < reg->n; i++) gls_push(reg->l[i].island ? &isl : &outer, gl_copy(&reg->l[i]));
    Gls o = gls_offset(&outer, dout, minfil, tol), io = isl.n ? gls_offset(&isl, -tc_off, minfil, tol) : (Gls){ 0 };
    gls_free(&outer); gls_free(&isl);
    Gls r = pk_merge(&o, &io, tol);
    gls_free(&o); gls_free(&io);
    return r;
}
/* ------------------------------------------------------------------------------------------------ entry (shared) */
/* set_heights(floor, ref) (mccore 0x18089fa80, decomp/pocket_b6/decomp_comp_pock_bnds_more.c): init = incremental ? ref + value : value, not below floor; retract = on ? (incremental ? ref + value : value) : init; clear = on ? (incremental ? ref + value : value) : retract */
typedef struct { double f, r, c; } PkH;
static PkH pk_h(const CamParams *p, double floor_z, double ref) {
    PkH H; H.f = (p->ht_inc & 4) ? ref + p->feed_plane : p->top_z + p->feed_plane;
    if (H.f < floor_z) H.f = floor_z;
    H.r = p->mc_ret_on ? ((p->ht_inc & 2) ? ref + (p->retract_z - p->top_z) : p->retract_z) : H.f;
    H.c = p->mc_clr_on ? ((p->ht_inc & 1) ? ref + (p->clearance - p->top_z) : p->clearance) : H.r;
    return H;
}
/* retract_move / clear_move (decomp/mccore_moves.c l.206 / l.8) on the heights H: the feed up to H.f unless rapid retract (op +0x141), then a rapid to: retract_move: retract (op +0x121) else clearance (op +0x111) else the feed plane; clear_move: clearance else retract else the feed plane */
static void pk_up(MB *b, const CamParams *p, PkH H, int clear) {
    double zr = clear ? (p->mc_clr_on ? H.c : p->mc_ret_on ? H.r : H.f) : (p->mc_ret_on ? H.r : p->mc_clr_on ? H.c : H.f);
    V3 e = lastp(b);
    if ((p->feed_retract && e.z + CAM_SYS_TOL < H.f) || e.z + CAM_SYS_TOL < zr) { double sv = g_hzf; g_hzf = H.f; retract_up(b, p, zr); g_hzf = sv; }
}
/* the start of a section of wr_pock_toolpath (types other than 3, l.316156-316197; decision N9-12: checked against the stored NCI of the Pocket ops): from the section's first record r0 at the section depth: tool down and more than nc_tol from the last point: set_heights(previous depth, previous depth), retract_move, or
   clear_move when the pocket changes unless clearance only at start / end (+0x111 and +0x2b1); then plunge_move on set_heights(depth, top of stock) (l.315582): the height to go to first (clearance or, clearance off, retract, when first or the pocket changes), NAN when none. g_pk_down: the tool stayed down.
   g_pk_sec_on: Pocket types 0 / 1 / 2 with a record (not the Surface rough levels). */
/* a chain pocket's depths and heights (FUN_1801c5250 l.313602-313612: set_depths(op, Z of the pocket's first non-island bound)): incremental depth / top on that Z; set_heights (mccore 18089fa80): an incremental plane is on the pocket's top, an absolute one stays */
static void pk_cp_for_z(CamParams *c, const CamParams *p, double cz) {
    c->final_z = p->dep_inc ? cz + (p->final_z - p->dep_cz) : p->final_z;
    c->top_z = p->top_inc ? cz + (p->top_z - p->dep_cz) : p->top_z;
    c->dep_cz = cz;
    double dz = c->top_z - p->top_z;
    if (!(p->ht_inc & 4)) c->feed_plane -= dz;
    if ((p->ht_inc & 2) && p->retract_z > -1e29) c->retract_z += dz;
    if (p->mc_clr_on && (p->ht_inc & 1)) c->clearance += dz;
}
/* keep tool down in wr_pock_toolpath (types other than 3): a -97 / -98 marker pair (bnci_wr_pock_marker 1 / 2, decomp/nciw/core_wr.c l.2464-2468)
   with a copy of a bound list in pock_ktd_bnd; on reading -97 (l.315658-315760) the records up to -98 are skipped and
   - a finish section (in_finish) with 0x126a on, the same marker pocket (local_93e = local_942) and sub-pocket (local_91e = local_90c) as the
     section before: local_944, the link FUN_1801e0f80(that bound, prev_nc_pt, the first record) at the marker's depth when the first linear
     record is read (l.315875-315905), lVar19 = 0;
   - otherwise (LAB_1801c8b0b): lVar19 = 1, local_8c0 = the bound, dVar10 = the section's depth - pending (g_pk_kp): at the first record
     of a later section with 0x891 (op +0x829) on and the same pocket / sub-pocket as the section before it, the same link at dVar10
     (l.316099-316154), then lVar19 = 0. lVar19 is not cleared at a section start (l.315540-316560).
   Either way the section start after it (l.316101-316150) finds the tool within nc_tol and keeps it down (plunge_move with the tool down). */
static int g_pk_kp; static Gls g_pk_kb; static double g_pk_kz; static int g_pk_cursub, g_pk_lastsub;
static int g_pk_ktd_err; static double g_pk_ctol;                    /* g_pk_ktd_err: a link failed (op->error from g_pk_ktd_msg); g_pk_ctol: gen_pocket_v2's arc chord */
static char g_pk_ktd_msg[160];
static int pk_ktd_take(MB *b, const CamParams *p, PV r0, int first);
static int g_pk_fin_sec;                                            /* the section being written is a finish section */
static int g_pk_sec_on, g_pk_down, g_pk_lastk, g_pk_curk; static double g_pk_lastz, g_pk_hf, g_pk_ref;   /* g_pk_ref: the marker's top for this section: top of stock, or for a later depth cut the previous cut's Z (wr_pock_depth_cuts l.315181; set_heights l.315582) */
/* zrec: the Z of the section's first record (plunge_move's param_3, l.316198: for a helix / ramp entry the entry's top, FUN_1801c4a60 l.313310-313316) */
static double pk_sec_start(MB *b, const CamParams *p, PV r0, double depth, double zrec, int any) {
    int first = !any, newp = !first && g_pk_lastk != g_pk_curk, only_se = p->mc_clr_on && p->mc_clr_end;
    g_pk_down = 0;
    pk_ktd_take(b, p, r0, first);                                /* l.316099-316154: the pending keep-tool-down link (0x891) */
    if (!first) {
        V3 e = lastp(b);
        if (hypot(e.x - r0.x, e.y - r0.y) > g_nc_tol) pk_up(b, p, pk_h(p, g_pk_lastz, g_pk_lastz), newp && !only_se);
        else g_pk_down = 1;
    }
    /* the section's own section-start block (Move.sof on its next move): wr_pock_toolpath calls wr_sof at every section start after the
       retract (decomp/mcmill_all.c l.316183, types other than 3) and has no cancel_q_1000 (none in l.315266-317200): also with the tool
       down; the op's first section is the op's first block */
    /* Move.sof 2 = a finish section's block (pock_in_finish = the section marker's finish flag at its start, wr_pock_toolpath l.315581;
       the 1013 / 1016 compensation words depend on it, decomp/nciw/core_wr.c l.962 / l.6932); the op's first block too when it is one */
    if (!first || g_pk_fin_sec) { g_sof_mb = b; g_sof_v = g_pk_fin_sec ? 2 : 1; }
    g_pk_lastk = g_pk_curk; g_pk_lastsub = g_pk_cursub; g_pk_lastz = depth;
    PkH H = pk_h(p, depth, g_pk_ref); g_pk_hf = H.f;
    int c14 = only_se ? first : (first || newp);                 /* l.316186-316197 */
    if (g_pk_down || !c14) return NAN;
    if (p->mc_clr_on) return (H.f + CAM_SYS_TOL < H.c && zrec + CAM_SYS_TOL < H.c) ? H.c : NAN;
    if (p->mc_ret_on) return (H.f + CAM_SYS_TOL < H.r && zrec + CAM_SYS_TOL < H.r) ? H.r : NAN;
    return NAN;
}
/* a rapid to (x, y, z) as the stored NCI records it: never diagonal: up first when it rises, across first when it goes down (the NCI of the stored Pocket ops: CHGREC op 2, A001 op 13, mc-test01 op 2, engraved coaster op 1; plunge_move and retract_move write one record, the split is below gr_lin_mill) */
static void pk_rapid(MB *b, double x, double y, double z) {
    if (!b->n) { mv(b, 1, x, y, z, 0); return; }
    V3 e = lastp(b);
    if (z > e.z + CAM_SYS_TOL) { mv(b, 1, e.x, e.y, z, 0); if (hypot(e.x - x, e.y - y) > CAM_SYS_TOL) mv(b, 1, x, y, z, 0); return; }
    if (hypot(e.x - x, e.y - y) > CAM_SYS_TOL) mv(b, 1, x, y, e.z, 0);
    if (fabs(e.z - z) > CAM_SYS_TOL) mv(b, 1, x, y, z, 0);
}
/* plunge_move at a point (decomp/mccore_moves.c l.85): a section whose first record is the plunge */
static void pk_sec_plunge(MB *b, const CamParams *p, PV a, double depth, int any) {
    double za = pk_sec_start(b, p, a, depth, depth, any);
    if (g_pk_down) { V3 e = lastp(b); if (hypot(e.x - a.x, e.y - a.y) > CAM_SYS_TOL) mv(b, 0, a.x, a.y, e.z, p->feed); if (fabs(e.z - depth) > 1e-12) mv(b, 0, a.x, a.y, depth, p->plunge_feed); return; }
    if (!isnan(za)) pk_rapid(b, a.x, a.y, za);
    pk_rapid(b, a.x, a.y, depth + CAM_SYS_TOL < g_pk_hf ? g_pk_hf : depth);
    mv(b, 0, a.x, a.y, depth, p->plunge_feed);
}
#include "cam2d_pock_entry.c"
/* Parallel Spiral / Parallel Spiral Clean Corners (methods 2 / 3, Pockparl.cpp) */
#include "cam2d_pock_pspiral.c"

/* ------------------------------------------------------------------------------------------------ the operation */
static void pk_retract(MB *b, const CamParams *p);
#include "cam2d_pock_hsx.c"   /* High Speed: the extra loops of a stepover between R and D (FUN_1801d1740) */

/* zz: the One Way / Zigzag pieces of the pocket (cam2d_pock_zz.c, per outer loop of the rough boundary): path, start, entry */
typedef struct { void *m; int n; PV st; PkEntry ent; int skip; } PkZz;
typedef struct { GlList loops; PkXs xs; Gls tc, rb; PkEntry ent; int skip; Gls cb; Gls *fb, *fin; int **par; char *fb_ok, *fin_ok; PkZz *zz; int nzz;
                 void *medax;
                 int zi, face, sk, sub, cp; double top_s, ztop; Gls chk, tr; } PkPocket;   /* Island facing (cam2d_pock_isl.c): zi the depth index of a by-depth pocket (-1: all depths), face a top pocket, sk the marker's pocket number, top_s the section's top of stock, ztop the facing Z, chk the check bounds */   /* medax: the pocket's medial axis (cam2d_pock_medax.c MdEdge list), burial only */

static void pk_retract(MB *b, const CamParams *p) {
    double zr = fmax(p->top_z + p->feed_plane, p->retract_z);
    retract_up(b, p, p->mc_clr_on ? fmax(zr, p->clearance) : zr);
}

static int pko_taper_on(const CamParams *p);   /* cam2d_pock_open.c: FUN_180056a70 */
#include "cam2d_pock_fin.c"
/* One Way / Zigzag (methods 6 / 0, Pockzigr.cpp FUN_1801ef830): pk_zz_plan / pk_zz_depth */
#include "cam2d_pock_zz.c"
/* FUN_1801e0fd0 l.331634: a straight move stays inside when no intersection of the line (pock_bound_line_inters, 20 tries, parameter in (-2e-7, 1.0000002)) lies farther than nc_tol from both its ends, and its midpoint is in the machinable area (each point separately: one entity may be crossed twice, N9-12) */
static int pk_line_inside(const Gls *bounds, PV a, PV b, double tol) {
    if (zz_equal(a, b)) return 1;
    ZB *zb = calloc((size_t)(bounds->n ? bounds->n : 1), sizeof(ZB));
    for (int i = 0; i < bounds->n; i++) zb[i].l = bounds->l[i];
    ZBs B = { zb, bounds->n }; ZI rec[ZZ_MAXI + 2]; int n = 0;
    zz_inters(&B, a, b, CAM_SYS_TOL, 20, rec, &n);
    int hit = 0;
    for (int i = 1; i <= n && !hit; i++) {
        double t = rec[i].t; if (!(-2e-7 < t && t < 1.0000002)) continue;
        double d1 = hypot(a.x - rec[i].p.x, a.y - rec[i].p.y), d2 = hypot(b.x - rec[i].p.x, b.y - rec[i].p.y);
        if (getenv("PK_DEBUG")) fprintf(stderr, "line %.4f,%.4f -> %.4f,%.4f rec %d t %.7f p %.5f,%.5f bnd %d d1 %.5f d2 %.5f\n", a.x, a.y, b.x, b.y, i, t, rec[i].p.x, rec[i].p.y, rec[i].bnd, d1, d2);
        if (tol < d1 && tol < d2) hit = 1;
    }
    free(zb);
    if (hit) return 0;
    return pk_pt_ok(bounds, pv((a.x + b.x) / 2, (a.y + b.y) / 2), tol);
}
/* the keep-tool-down link FUN_1801e0f80 -> FUN_1801e23c0 (pock_move_avoid_islands, cam2d_pock_open.c pko_link) from the tool's position to
   'to' at the depth, inside the bound list 'bnd'. Returns pko_link's value: 1 done (also when Mastercam writes nothing, l.332695-332700),
   0 error (err), -1 over 999 waypoints (no move) */
static void pko_zb(ZB *b, const Gl *g, int island);
static int pko_link(MB *b, const CamParams *p, const ZBs *B, PV A, PV Z, double depth, double ctol, char *err, size_t errn);
static int pk_ktd_link(MB *b, const CamParams *p, const Gls *bnd, PV to, double depth, char *err, size_t errn) {
    if (!b->n || !bnd->n) return 1;
    V3 e = lastp(b);
    ZB *zb = calloc((size_t)bnd->n, sizeof(ZB));
    for (int i = 0; i < bnd->n; i++) pko_zb(&zb[i], &bnd->l[i], bnd->l[i].island);
    ZBs B = { zb, bnd->n };
    int r = pko_link(b, p, &B, pv(e.x, e.y), to, depth, g_pk_ctol, err, errn);
    if (getenv("PK_DEBUG")) fprintf(stderr, "ktd link %.4f %.4f -> %.4f %.4f at %.4f in %d bounds: %d\n", e.x, e.y, to.x, to.y, depth, bnd->n, r);
    if (getenv("PK_DEBUG_KTD")) { FILE *df = fopen(getenv("PK_DEBUG_KTD"), "a"); if (df) { fprintf(df, "L %.6f %.6f %.6f %.6f %d\n", e.x, e.y, to.x, to.y, r);
        for (int i = 0; i < bnd->n; i++) { fprintf(df, "B %d %d\n", i, bnd->l[i].island); for (int k = 0; k < bnd->l[i].n; k++) { PV q = gt_start(&bnd->l[i].e[k]); fprintf(df, "%c %.6f %.6f\n", bnd->l[i].e[k].t, q.x, q.y); } }
        fclose(df); } }
    for (int i = 0; i < bnd->n; i++) { free(zb[i].poly.xy); gl_free(&zb[i].l); }
    free(zb);
    return r;
}
/* the pending link at a section's first record r0 (wr_pock_toolpath l.316099-316154, types other than 3: 0x891 on, the same marker pocket and
   sub-pocket as the section before, lVar19 > 0): FUN_1801e0f80(local_8c0, prev_nc_pt, the record) with pock_depth = dVar10; lVar19 = 0 */
static int pk_ktd_take(MB *b, const CamParams *p, PV r0, int first) {
    if (!g_pk_kp || first || !p->mc_keep_down || g_pk_lastk != g_pk_curk || g_pk_lastsub != g_pk_cursub) return 0;
    char e2[160] = "";
    int r = pk_ktd_link(b, p, &g_pk_kb, r0, g_pk_kz, e2, sizeof e2);
    if (r == 0) { g_pk_ktd_err = 1; snprintf(g_pk_ktd_msg, sizeof g_pk_ktd_msg, "keep tool down link: %.140s", e2); }
    if (r < 0) { g_pk_ktd_err = 1; snprintf(g_pk_ktd_msg, sizeof g_pk_ktd_msg, "keep tool down link: over 999 waypoints (FUN_1801e23c0 l.332561: -1, no move; the writer goes on) NOT PORTED"); }
    g_pk_kp = 0;
    return 1;
}
static void pk_ktd_pend(const Gls *bnd, double depth) { gls_free(&g_pk_kb); g_pk_kb = gls_copy(bnd); g_pk_kz = depth; g_pk_kp = 1; }
/* Minimize tool burial (0x1259) with Parallel Spiral: the POCKMDL.cpp medial axis and its link walk (FUN_1801d9f60 /
   FUN_1801dae10) */
#include "cam2d_pock_medax.c"
#include "cam2d_pock_isl.c"   /* Pocket type 2 Island facing */

static void pk_free(PkPocket *q, int nf) {
    gls_free(&q->chk); gls_free(&q->tr);
    { MdEdge *m = q->medax; md_free_edges(&m); q->medax = NULL; }
    gll_free(&q->loops); pkx_free(&q->xs); gls_free(&q->tc); gls_free(&q->rb); gls_free(&q->cb); pk_entry_free(&q->ent);
    for (int i = 0; i < q->nzz; i++) { free(q->zz[i].m); pk_entry_free(&q->zz[i].ent); } free(q->zz);
    for (int j = 0; j < nf; j++) { if (q->fb) gls_free(&q->fb[j]); if (q->fin) gls_free(&q->fin[j]); if (q->par) free(q->par[j]); }
    free(q->fb); free(q->fin); free(q->par); free(q->fb_ok); free(q->fin_ok);
}

/* one depth of a pocket's loop roughing (High Speed, Parallel Spiral, True Spiral; moved out of gen_pocket_v2): the shared
   entry (cam2d_pock_entry.c, planned at the first depth), the loops and their links */
typedef struct { const CamParams *p; double step, ctol, nctol, zf; int method, in2out, burial, ptype; double minfil, tol; char *err; size_t errn; int failed; } PkRoughCtx;
/* FUN_1801e0f80 -> pock_move_avoid_islands (FUN_1801e23c0): no move when the points are Equal; a straight feed when it
   stays inside (FUN_1801e0fd0); the path around islands (find_path_around_island) is NOT DECODED: instead (ours) pk_retract, a rapid over the
   target and the plunge; it stands in for the walk at the depth, not for a section start of wr_pock_toolpath, so it is not pk_sec_start (V-14). Returns 0 (FUN_1801e23c0's code for a written link) */
static int pk_e0f80(MB *b, const CamParams *p, const Gls *rb, PV pe, PV s0, double depth, double zf, double nctol) {
    if (zz_equal(pe, s0)) return 0;
    int in = pk_line_inside(rb, pe, s0, nctol);
    if (getenv("PK_DEBUG")) fprintf(stderr, "link %.3f %.3f -> %.3f %.3f in %d (rb %d loops)\n", pe.x, pe.y, s0.x, s0.y, in, rb->n);
    if (in) mv(b, 0, s0.x, s0.y, depth, p->feed);
    else { pk_retract(b, p); mv(b, 1, s0.x, s0.y, fmax(zf, p->retract_z), 0); mv(b, 1, s0.x, s0.y, zf, 0); mv(b, 0, s0.x, s0.y, depth, p->plunge_feed); }
    return 0;
}
/* FUN_1801c0190 l.310339: Facing, exit distance pock_prm +0x178 (0x13a0) > 1e-12: out to a point outside the first outer bound, the same search as the entry from the current position; the move is FUN_1801e0f80 */
static void pk_face_exit(MB *b, const PkRoughCtx *c, PkPocket *q, double depth) {
    const CamParams *p = c->p;
    double d = pkd(p, 0x13a0); if (!(1e-12 < d)) return;
    int i = 0; while (i < q->rb.n && q->rb.l[i].island) i++;
    if (i >= q->rb.n) return;
    V3 e = lastp(b); PV Q;
    if (!pk_face_out(g_pk_face_chain, &q->rb.l[i], pv(e.x, e.y), d, pk_spiral_cutting(p), p->tool_dia * 0.5, c->minfil, c->tol, c->nctol, &Q)) return;
    pk_e0f80(b, p, &q->rb, pv(e.x, e.y), Q, depth, c->zf, c->nctol);
}
static void pk_rough_depth(MB *b, const PkRoughCtx *c, PkPocket *q, double depth, int *anyp) {
    const CamParams *p = c->p; double step = c->step, ctol = c->ctol, nctol = c->nctol, zf = c->zf; int any = *anyp;
    if (q->nzz) {   /* One Way / Zigzag */
        for (int i = 0; i < q->nzz; i++) if (!q->zz[i].skip) pk_zz_depth(b, p, &q->zz[i], depth, ctol, &any);
        if (c->ptype == 1 && any) pk_face_exit(b, c, q, depth);
        if (q->face && any && !pk_isl_exit(b, p, q, depth, zf, c->minfil, c->tol, c->err, c->errn)) ((PkRoughCtx *)c)->failed = 1;
        *anyp = any; return;
    }
    PV st = gt_start(&q->loops.l[0].e[0]);
            if (g_pk_sec_on) pk_entry_emit(b, p, &q->ent, depth, pk_sec_start(b, p, q->ent.s, depth, pk_entry_z(&q->ent, depth), any), ctol);
            else { if (any) pk_retract(b, p); pk_entry_emit(b, p, &q->ent, depth, any ? fmax(zf, p->retract_z) : p->clearance, ctol); }
            if (q->ent.kind != PKE_PLUNGE) mv(b, 0, st.x, st.y, depth, p->feed);   /* the link to the cut start (FUN_1801e23c0) */
            any = 1;
            for (int i = 0; i < q->loops.n; i++) {
                const Gl *l = &q->loops.l[i];
                PV s0 = gt_start(&l->e[0]);
                if (i > 0) {
                    V3 e = lastp(b); PV pe = pv(e.x, e.y);
                    double dd = hypot(s0.x - pe.x, s0.y - pe.y);
                    if (dd > CAM_SYS_TOL) {
                        int done = 0;
                        if (c->method == 8 && (fabs(dd - step) < nctol || fabs(dd - step * 0.5) < nctol)) {   /* FUN_1801ce310 (High Speed links) */
                            PV t = gt_tan(&l->e[0], 0);
                            int dir = 0.0 < (s0.y - pe.y) * t.x - t.y * (s0.x - pe.x);
                            Gl ch = pk_channel(dir, pe, s0);
                            if (!pk_inside_all(&q->rb, &ch, nctol, step)) { gl_free(&ch); ch = pk_sshape(dir, pe, s0); if (!pk_inside_all(&q->rb, &ch, nctol, step)) { gl_free(&ch); ch.n = 0; } }
                            if (ch.n) { pk_loop(b, &ch, depth, p->feed, ctol); done = 1; }
                            gl_free(&ch);
                        }
                        if (!done) {
                            /* Parallel Spiral with Minimize tool burial: FUN_1801e9ee0 (l.336785, D:1801ea082-1801ea0f6)
                               first walks the medial axis (FUN_1801dae10, tol nc_tol); when it fails, and otherwise,
                               FUN_1801e0f80 */
                            int r = 1;
                            if (c->burial && q->medax) {
                                MdEmit me = { b, p, &q->rb, depth, zf, nctol };
                                r = md_dae10(&me, q->medax, pe, s0, nctol);
                            }
                            if (r != 0) pk_e0f80(b, p, &q->rb, pe, s0, depth, zf, nctol);
                        }
                    }
                }
                if (c->method == 1) {   /* FUN_1801e8650: a whole bound pass (FUN_1801e9a90) as loops, a record list point to point by FUN_1801e0f80 (FUN_1801e9020 l.336190) */
                    if (l->tag == 2) for (int k = 0; k < l->n; k++) { V3 e = lastp(b); pk_e0f80(b, p, &q->rb, pv(e.x, e.y), gt_end(&l->e[k]), depth, zf, nctol); }
                    else pk_loop(b, l, depth, p->feed, ctol);
                    continue;
                }
                if (c->method == 2 || c->method == 3) { pk_ps_cut(b, &q->rb, l, c->method == 3, p->tool_dia, depth, p->feed, ctol, nctol); continue; }
                /* High Speed: the loop's extra loops (stepover between R and D): cut while cutting it (FUN_1801cf8b0) */
                const Gl *xl[64]; int xsm[64], nx = 0;
                for (int k = 0; k < q->xs.l.n && nx < 64; k++) if (q->xs.par[k] == i) { xl[nx] = &q->xs.l.l[k]; xsm[nx] = q->xs.l.l[k].island; nx++; }
                if (nx) pk_hs_cut(b, p, &q->rb, l, xl, xsm, nx, c->in2out, depth, step, nctol, ctol);
                else pk_loop(b, l, depth, p->feed, ctol);
            }
    if (c->ptype == 1) pk_face_exit(b, c, q, depth);
    if (q->face && !pk_isl_exit(b, p, q, depth, zf, c->minfil, c->tol, c->err, c->errn)) ((PkRoughCtx *)c)->failed = 1;
    *anyp = any;
}

/* the inner bounds of the lead check (FUN_1801c4860: offset_pock_bounds of the pass bounds by the finish spacing, bound by
   bound - no merge - each result's +0x50 = its bound: parent[]) */
static Gls pk_fin_inner(const Gls *fl, double fs, double minfil, double tol, int **parent) {
    Gls out = { 0 }; int cap = 0; *parent = NULL;
    for (int i = 0; i < fl->n; i++) {
        Gl c = gl_copy(&fl->l[i]); int isl = c.island;
        if (isl) { gl_reverse(&c); c.island = 0; }
        Gls one = { 0 }; gls_push(&one, c);
        Gls o = gls_offset(&one, isl ? fs : -fs, minfil, tol);
        for (int k = 0; k < o.n; k++) {
            Gl r = o.l[k];
            if (isl) { gl_reverse(&r); r.island = !r.island; }
            gls_push(&out, r);
            if (out.n > cap) { cap = out.n * 2 + 4; *parent = realloc(*parent, cap * sizeof(int)); }
            (*parent)[out.n - 1] = i;
        }
        free(o.l); gls_free(&one);
    }
    return out;
}

/* one finish section (FUN_1801cc170 pass j of pocket q at depth z): every bound of the pass with its leads */
typedef struct { PV a, b; int ccw, ok; } PkFirst;   /* the first chain's first entity: start / end, the chain's direction */
/* kdown: pock_tool_down when FUN_1801cd200 writes the pocket's first finish bound (generation order, machine_pocket: the roughing of this
   pocket, or an earlier sub-pocket of the same chain pocket, left the tool down; GeneratePocketToolpath l.313598 clears it per chain pocket) */
static int pk_fin_section(MB *b, const CamParams *p, const PkFin *F, PkPocket *q, int k, int j, double depth, const PkFirst *fe,
                          int *anyp, int kdown, double minfil, double minfil_hs, double tol, double ctol, char *err, size_t errn) {
    double nct = g_nc_tol, zf = p->top_z + p->feed_plane;
    if (!q->fb_ok[j]) {
        q->fb[j] = pk_fin_bounds(F, p, F->ctl_only ? &q->cb : &q->tc, j, minfil, minfil_hs, tol); q->fb_ok[j] = 1;
        /* FUN_1801cc070 (l.318054, distance > 1e-12): with several bounds, an island longer than the longest outer bound and
           is_ctour_nested_or_intersecting skip the pass - that test is NOT DECODED: refused */
        double d = (F->nf - j - 1) * F->fs + (F->ctl_only ? p->stock_xy : 0);
        if (fabs(d) > 1e-12 && q->fb[j].n > 1) {
            double po = 0, pi = 0; int ho = 0, hi = 0;
            for (int i = 0; i < q->fb[j].n; i++) { double L = gl_len(&q->fb[j].l[i]); if (q->fb[j].l[i].island) { if (L > pi) pi = L; hi = 1; } else { if (L > po) po = L; ho = 1; } }
            if (ho && hi && pi > po) { snprintf(err, errn, "pocket finish: island longer than the outer bound (FUN_1801cc070 nesting test) NOT DECODED"); return 0; }
        }
    }
    Gls *fl = &q->fb[j];
    int nb = fl->n; if (!nb) return 1;
    char *done = calloc(nb, 1);
    for (int it = 0; it < nb; it++) {
        int bi = it, split = 0; Gl l;
        if (F->closest) {
            /* FUN_1801cbd20: the bound / entity nearest the tool (prev_nc_pt), each entity at t = 0.5 with leads on (op
               +0x888) else 0; then reorder_pock_bound to that entity */
            if (!*anyp) { free(done); snprintf(err, errn, "pocket finish Start at closest entity (0x1269) as the first motion: start position NOT DECODED"); return 0; }
            V3 e = lastp(b); PV pos = pv(e.x, e.y); int be = 0; double bd = 0; bi = -1;
            for (int i = 0; i < nb; i++) {
                if (done[i]) continue;
                for (int m = 0; m < fl->l[i].n; m++) {
                    PV x = gt_pt(&fl->l[i].e[m], F->lead_master ? 0.5 : 0.0); double dd = hypot(x.x - pos.x, x.y - pos.y);
                    if (bi < 0 || dd < bd) { bd = dd; bi = i; be = m; }
                }
            }
            if (bi < 0) break;
            done[bi] = 1;
            l = gl_copy(&fl->l[bi]); gl_rotate(&l, be); split = 1;
        } else {
            l = gl_copy(&fl->l[bi]);
            /* the start. The first bound of pocket 0 (or the op's first finish): the bound starts where offset_pock_bounds
               carries the chain's start (ours: the loop point nearest it); the bound's direction is set twice by
               reverse_pock_bound keeping the first entity (FUN_1801d5870 l.323745, FUN_1801cc170 l.318202 with 0x1269 =
               0), each time swapping the ends of that entity: the start is the chain's first entity start when the final
               direction is the chain's, else its end; FUN_1801bf950 then splits that entity at its midpoint (split = 1).
               Other bounds: the point nearest the end of the cut before (ours, the measured model; split = 0). */
            if ((!*anyp || (bi == 0 && k == 0)) && fe->ok) {
                int want = F->climb ? !l.island : l.island;
                pk_start_near_pt(&l, want == fe->ccw ? fe->a : fe->b, CAM_SYS_TOL); split = 1;
            } else { V3 e = lastp(b); pk_start_near_pt(&l, pv(e.x, e.y), CAM_SYS_TOL); }
        }
        if (!l.n) { gl_free(&l); continue; }
        /* direction (l.318190-318203): climb -> outer bounds counter-clockwise, islands clockwise; else the reverse.
           reverse_pock_bound(b, !0x1269): keep the first entity (the start stays) / plain reversal (the start point stays,
           the first entity is the one before) - both keep the start point of l here */
        int want = F->climb ? !l.island : l.island, ccw = gl_area(&l) > 0;
        if (want != ccw) gl_reverse(&l);
        Gl out; int nin = 0, nout = 0;
        if ((F->ein_line || F->ein_arc || F->eout_arc || F->eout_line) && !q->fin_ok[j]) {
            q->fin[j] = pk_fin_inner(fl, F->fs, minfil, tol, &q->par[j]); q->fin_ok[j] = 1;
        }
        pk_fin_leads(F, &l, split, F->climb, fl, &q->fin[j], q->par[j], bi, &out, &nin, &nout, nct, tol);
        gl_free(&l);
        if (!out.n) { gl_free(&out); continue; }
        /* ours (safety, as the contour of batch 5): with Control the G40 (140) move must be an exit line away from the part */
        if (F->ctl_only && !(nout > 0 && F->eout_line && out.e[out.n - 1].t == 'L')) {
            gl_free(&out); free(done);
            snprintf(err, errn, "pocket finish compensation in control needs a kept exit lead line (the G40 move) - ours, formats/post.md");
            return 0;
        }
        PV a = gt_start(&out.e[0]);
        /* keep tool down 0x126a (FUN_1801cd200 l.318461-318515, cVar19 = 0x126a with param_9 = 0): pock_tool_down, 0x126a and (0x126b, or
           depth cuts off (op +0x800), or not pock_finish_all, or not param_3); param_3 = FUN_1801cc170's bVar18 = !finish_with_rough on its
           first bound only (l.317975, l.318279; a later pass sets it only with 0x126a off, l.318011-318015) */
        int gdown = (j > 0 || it > 0) ? 1 : (g_pk_sec_on ? kdown : *anyp);
        int p3 = j == 0 && it == 0 && !F->fwr;
        int mark = F->ktd && gdown && (F->after || !p->mc_depth_on || !F->finall || !p3);
        if (g_pk_sec_on) {
            /* op code 4: the marker pair with a copy of the pass bounds (local_1a8, param_2 for op code != 0xe l.318254-318256), linked by
               wr_pock_toolpath (pk_ktd_*): at once when the section before is of the same pocket / sub-pocket, else left pending */
            if (mark) {
                if (*anyp && g_pk_lastk == g_pk_curk && g_pk_lastsub == g_pk_cursub) {
                    int r = pk_ktd_link(b, p, fl, a, depth, err, errn);
                    if (r == 0) { gl_free(&out); free(done); return 0; }
                    if (r < 0) { gl_free(&out); free(done); snprintf(err, errn, "pocket finish keep tool down: link over 999 waypoints (FUN_1801e23c0 l.332561) NOT PORTED"); return 0; }
                    g_pk_kp = 0;                                         /* l.315890: lVar19 = 0 */
                } else pk_ktd_pend(fl, depth);                           /* LAB_1801c8b0b */
            }
            pk_sec_plunge(b, p, a, depth, *anyp);
        }
        else if (mark) {
            /* op code 0xe (Surface rough levels, process_pocket param_9 = 1, l.323217): no markers, FUN_1801e0f80(param_2 = the pocket's
               tool-centre bounds (op code 0xe, l.318256-318257), prev_nc_pt, the start) at once (l.318493-318497). Then FUN_1801c3b20
               (l.318536: 0x126a and pock_rapid_finish = 0, finish_with_rough set by msurf_map) links again from prev_gr_nci_pt through
               ppVar20 and FUN_1801bfcb0 -> FUN_1801bf680 (l.309886) once more, and plunge_move with the tool down (decomp/mccore_moves.c
               l.157-165): a feed at the current Z to the start when it is still away, then the plunge feed to the depth */
            int r = pk_ktd_link(b, p, &q->tc, a, depth, err, errn);
            if (r == 0) { gl_free(&out); free(done); return 0; }
            if (r < 0) { gl_free(&out); free(done); snprintf(err, errn, "pocket finish keep tool down: link over 999 waypoints (FUN_1801e23c0 l.332561) NOT PORTED"); return 0; }
            V3 e = lastp(b);
            if (hypot(e.x - a.x, e.y - a.y) > CAM_SYS_TOL) {
                /* the link wrote nothing (FUN_1801e23c0 state 0, l.332695-332700, returns 0): FUN_1801c3b20 (l.318536) and FUN_1801bf680
                   (l.309915-309928) run FUN_1801e0f80 again from prev_gr_nci_pt through ppVar20 (l.318417-318420): param_7 on the last pass
                   (FUN_1801cc170 local_198 = FUN_1801edcd0(local_168 = the tool-centre bounds, the spacing 0x1260) l.318222-318228: the bounds
                   offset inward by the spacing (MCCECalcOffset, ours: gls_offset as everywhere in this port) and sorted), else param_2; the
                   same deterministic walk the second time. Control compensation (local_168 = the chain bounds) does not reach here
                   (Surface rough: cutter_comp 'N') */
                if (F->ctl_only) { gl_free(&out); free(done); snprintf(err, errn, "pocket finish keep tool down with compensation in control on the Surface rough levels: NOT PORTED"); return 0; }
                Gls b20 = { 0 }; const Gls *pb = &q->tc;
                if (j == F->nf - 1) { b20 = gls_offset(&q->tc, -F->fs, minfil, tol); pb = &b20; }
                r = pk_ktd_link(b, p, pb, a, depth, err, errn);
                gls_free(&b20);
                if (r == 0) { gl_free(&out); free(done); return 0; }
                if (r < 0) { gl_free(&out); free(done); snprintf(err, errn, "pocket finish keep tool down: the retry over 999 waypoints (FUN_1801c3b20 -> FUN_1801bfcb0, FUN_1801e3150) NOT PORTED"); return 0; }
                e = lastp(b);
                /* still away: plunge_move with the tool down (decomp/mccore_moves.c l.157-165): a feed at the current Z to the start */
                if (hypot(e.x - a.x, e.y - a.y) > CAM_SYS_TOL) mv(b, 0, a.x, a.y, e.z, p->feed);
            }
            if (fabs(e.z - depth) > CAM_SYS_TOL) mv(b, 0, a.x, a.y, depth, p->plunge_feed);
        }
        else {
            if (*anyp) pk_retract(b, p);
            mv(b, 1, a.x, a.y, *anyp ? fmax(zf, p->retract_z) : p->clearance, 0);
            mv(b, 1, a.x, a.y, zf, 0);
            mv(b, 0, a.x, a.y, depth, p->plunge_feed);
        }
        int m0 = b->n;
        for (int i = 0; i < out.n; i++) pk_gt(b, &out.e[i], depth, p->feed, ctol);
        /* FUN_1801cd200 l.318636-318656: cutter compensation in control: the first entity cc_control_gcode (Control / Wear:
           left 41, right 42; Reverse wear swapped), the last 140 (posted only with MCAM_CUTTER_COMP=1, nc_post.c) */
        if (F->ctl && b->n > m0) {
            int g = F->climb ? 41 : 42; if (F->comp == 3) g = F->climb ? 42 : 41;
            b->m[m0].cc = g; b->m[b->n - 1].cc = 140;
            if (getenv("PK_DEBUG")) fprintf(stderr, "cc %d at %.4f %.4f (from %.4f %.4f), 140 at %.4f %.4f\n", g, b->m[m0].p.x, b->m[m0].p.y, a.x, a.y, b->m[b->n - 1].p.x, b->m[b->n - 1].p.y);
        }
        gl_free(&out);
        *anyp = 1;
    }
    free(done);
    return 1;
}

#include "cam2d_pock_open.c"   /* Pocket type 4 Open pocket (process_open_pocket, FUN_1801e7040, FUN_1801e5c70) */
#include "cam2d_pock_rem.c"     /* pocket type 3 Remachining (FUN_1801ec3c0 l.338353) */
#include "cam2d_pock_taper.c"   /* tapered walls: process_pocket's taper arguments (FUN_1801d4850) */

/* gen_pocket_v2: GeneratePocketToolpath FUN_1801c5250 (l.313461) for closed Standard pockets (record 0x13fa = 0; Facing /
   Island facing / Remachining / Open pocket have their own paths). Roughing by cutting method (FUN_1801ede50 l.339319 /
   pock_spiral_cutting): 8 High Speed FUN_1801d00d0 (pk_hs_loops, with the extra loops of a stepover between R and D:
   cam2d_pock_hsx.c; trochoidal modes 1 / 2 refused), 2 / 3 Parallel Spiral (+ Clean Corners) FUN_1801ea750
   (cam2d_pock_pspiral.c; with 0x1259 Minimize tool burial the links walk the medial axis: cam2d_pock_medax.c), 5 True Spiral FUN_1801ef3d0
   (cam2d_pock_tspiral.c), 6 One Way and 0 Zigzag FUN_1801ef830 (cam2d_pock_zz.c; zigzag with 0x1259 burial too); 1 Constant
   Overlap Spiral and 4 Morph Spiral refused (NOT DECODED). Per pocket (extract_pocket order = the chains' order here;
   sort_pock_bounds NOT DECODED): tool-centre bounds = the chains offset inward by R + wall stock (FUN_1801d4620); rough
   boundary = that offset by finish passes x spacing (FUN_1801ede50 dVar18); the loops are computed once and cut at every
   depth (calc_depth_cuts as cam2d.c hst_depths; wr_pock_depth_cuts l.315072 shift the cut by the depth difference, the
   entry included). Entry: the shared roughing entry (cam2d_pock_entry.c, _enter_pocket FUN_1801bc830: helix, follow
   boundary, ramp, plunge, or the pocket skipped), then a line to the start. Finishing (cam2d_pock_fin.c, FUN_1801cc170 /
   FUN_1801cd200 / FUN_1801bdc00): pass bounds, direction, closest-entity start, leads with their checks, compensation
   tags, thin wall copies; the roughing and finishing sections are emitted in wr_pock_toolpath's order (pk_sec_cmp). The
   batch 6 decode notes: formats/pocket_engine_b6.md. */
/* srough: Surface Rough Pocket (op 10) clears each Z level with this engine - mcmsurf FUN_1803769e0 hands the level's tool-centre
   region to mcmill process_pocket (decomp/mcmsurf_all.c l.554653-554779, formats/surf_rough.md 2.4). When g_pk_tc_in is set it is
   that region (outer loops counter-clockwise, islands clockwise, already tool-centre: cutter_comp 'N', msurf_map l.340443): it
   replaces the chains' region and its offset by R + stock (cam3d_srough.c sets it per level; NULL = the 2D Pocket as before) */
static const Gls *g_pk_tc_in;
static int gen_pocket_v2(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    /* tol: the geometry (offset topology) tolerance, finer than the output chords: mccore chain_tol (config 1001,
       metric 0.002 / inch 0.0001) halved; ctol: chords of the displayed arcs (the post writes the true arcs) */
    double R = p->tool_dia / 2, tol = cam_chain_tol(mcam_is_metric(f)) * 0.5, nctol = g_nc_tol;
    double ctol = p->tol > 0 ? p->tol : tol;
    double minfil = 0.785398;                                /* set_min_fil_angle(op +0x56b): 0 pi, 1 0.785398, else 0.01 */
    { int roll = p->pk_ok ? p->pk_roll : 1; minfil = roll == 0 ? M_PI : roll == 1 ? 0.785398 : 0.01; }
    double minfil_hs = fmin(minfil, 0.785398);               /* FUN_1801d00d0 local_150 */
    /* pocket type pock_prm+0x1d2 = record 0x13fa (HSTPocketCutPage 2101: 0 Standard, 1 Facing, 2 Island facing, 3 Remachining, 4 Open pocket).
       1 Facing: FUN_1801d4850 / FUN_1801bfd60 / FUN_1801c0190 ported (pk_face_tc / pk_face_out / the exit in pk_rough_depth; no stored op, checked against the decompile only).
       2 Island facing: cam2d_pock_isl.c (FUN_1801c7970 / FUN_1801c68e0 per depth, no stored op: the mccore pieces checked against the DLL, the op unverified against Mastercam). 3 Remachining: cam2d_pock_rem.c (FUN_1801ec3c0 l.338353). 4 Open pocket: pko_open in cam2d_pock_open.c. */
    int pk_type = p->pk_ok ? pks(p, 0x13fa) : 0;
    { const McSysTol *st = mc_systol_units(mcam_is_metric(f)); rm_tol = st->tol; rm_nc_tol = g_nc_tol; rm_chain_tol = st->chain_tol; }   /* mccore tol / nc_tol / chain_tol of pk_merge and the Island facing bounds */
    if (!g_pk_tc_in && pk_type != 3 && pk_type != 4)                       /* FUN_1801c5250 l.313523-313527: an open chain with a pocket type other than Remachining / Open pocket: warning 0x93f (mcmillres 2367), no toolpath (LAB_1801c5374 l.313789) */
        for (int i = 0; i < op->nchains; i++) if (!op->chains[i].closed && !op->chains[i].avoid && !cam_chain_is_point(f, &op->chains[i]))   /* a point chain: its two ends coincide, so set_chain_closed (mccore_chain_range.c l.12427, distance < tol) makes it closed */ { snprintf(op->error, sizeof op->error, "Warning - not all boundaries are closed.  Select Open pockets or close chains"); return 0; }
    /* 2 Island facing (cam2d_pock_isl.c, FUN_1801c7970 / FUN_1801c68e0 per depth): the pocket as Standard, by depth (pock_proc_by_depth), plus the island top pockets */
    int isl_f = pk_type == 2 && !g_pk_tc_in;
    if (isl_f) {
        if (p->pocket_no_rough) { snprintf(op->error, sizeof op->error, "Roughing must be enabled when Island facing is selected"); return 0; }   /* valid_pocket_options l.315055: post_warning 0x969 (mcmillres 2409) */
        if (pk_has_point(f, op)) { snprintf(op->error, sizeof op->error, "pocket type Island facing from a chained entry point: NOT DECODED"); return 0; }
        if (pkb(p, 0x1436)) { snprintf(op->error, sizeof op->error, "pocket type Island facing with thin wall finishing (FUN_1801cad80 by depth): NOT DECODED"); return 0; }
        pk_type = 0;
    }
    if (pk_type == 1 && p->pk_ok && (pks(p, 0x123c) == 0 || pks(p, 0x123c) == 6) && pkd(p, 0x1250) != 0) { snprintf(op->error, sizeof op->error, "pocket type Facing with a roughing angle: the entry search runs in the rotated frame (FUN_1801bfde0 l.310185), not ported"); return 0; }
    if (pk_type == 1 && p->pk_ok && pk_has_point(f, op)) { snprintf(op->error, sizeof op->error, "pocket type Facing from a chained entry point: NOT DECODED"); return 0; }
    if (pk_type == 3) return pk_rem_gen(f, op, b, minfil, tol, ctol);   /* process_pocket l.323274, cam2d_pock_rem.c */
    if (pk_type != 0 && pk_type != 1 && pk_type != 2 && pk_type != 4) {
        static const char *tn[] = { "Standard", "Facing", "Island facing", "Remachining", "Open pocket" };
        snprintf(op->error, sizeof op->error, "pocket type %s: NOT DECODED", pk_type > 0 && pk_type < 5 ? tn[pk_type] : "?"); return 0;
    }
    /* Open pocket with the cutting method on an open chain: machine_pocket with param_3 = 0 never reads the cutting method (FUN_1801d36c0 l.322583-322585 -> FUN_1801e7040 l.322571-322573) */
    if (pk_type == 4 && !g_pk_tc_in && pkb(p, 0x13f0) && pko_has_open(f, op, cam_chain_tol(mcam_is_metric(f)))) {
        Gls r0; PV m0; PkFirst f0 = { { 0, 0 }, { 0, 0 }, 0, 0 };
        return pko_open(f, op, b, minfil, cam_chain_tol(mcam_is_metric(f)), ctol, &r0, &m0, &f0) == 2;
    }
    int method = p->pk_ok ? pks(p, 0x123c) : 8;
    if (p->pocket_style == 1) method = 0;
    int rough = !p->pocket_no_rough;
    /* FUN_1801ede50 (l.339337) per pocket: 1 Constant Overlap Spiral FUN_1801e8650 (cam2d_pock_ovlp.c), 2 / 3 Parallel Spiral FUN_1801ea750 (cam2d_pock_pspiral.c),
       5 True Spiral FUN_1801ef3d0 (cam2d_pock_tspiral.c), 8 High Speed FUN_1801d00d0; not spiral (pock_spiral_cutting): 6 One Way / 0 Zigzag FUN_1801ef830 */
    if (rough && method == 4) {   /* FUN_1801de750 (l.329829) runs on the medial axis (FUN_1801d8aa0 / FUN_1801da3c0 / FUN_1801db4b0 ...) */
        snprintf(op->error, sizeof op->error, "pocket Morph Spiral: NOT DECODED (medial axis FUN_1801d9f60 family)"); return 0;
    }
    /* zz: methods 6 One Way and 0 Zigzag (cam2d_pock_zz.c); zigzag reaches here by default since decisions batch dec1 (B6-2;
       PK_ZIGZAG_DECODED=0 = the former correlated engine, cam2d.c gen_pocket). Zigzag + Minimize tool burial (0x1259) is not the
       medial axis (machine_pocket l.322515-322530 builds it for methods 1-3 only with product bit 0x10 clear) but
       FUN_1801f0540 (region choice) + FUN_1801f3940 flags + find_path_around_island's burial branch: ported (medax_zz) in
       cam2d_pock_zz.c */
    int zz = rough && (method == 6 || method == 0);
    if (rough && method != 8 && method != 1 && method != 2 && method != 3 && method != 5 && !zz) { snprintf(op->error, sizeof op->error, "pocket cutting method %d: NOT DECODED", method); return 0; }
    /* methods 2 / 3 with 0x1259 Minimize tool burial: machine_pocket (l.322510-322555) builds the pocket's medial axis
       (FUN_1801d9f60, POCKMDL.cpp: cam2d_pock_medax.c md_build) and FUN_1801e9ee0 (l.336785) links the loops along it
       (FUN_1801dae10), falling back to FUN_1801e0f80 when the walk fails 8 times. burial = pock_prm +0x31 (0x1259) and
       method 1..3 (product bits 0); method 1 stays refused above */
    int burial = rough && (method == 1 || method == 2 || method == 3) && p->pk_ok && pkb(p, 0x1259);
    /* channel mode (0x1408) and the extra loops for a stepover between R and D are High Speed only (FUN_1801ede50 l.339401
       tests method 8 for the channel; FUN_1801d1740 is called from FUN_1801d00d0 only) */
    if (rough && method == 8 && p->pk_ok && pks(p, 0x1408) != 0) { snprintf(op->error, sizeof op->error, "pocket High Speed channel mode %d: NOT DECODED", pks(p, 0x1408)); return 0; }
    double step = p->tool_dia * p->stepover_pct / 100;
    if (rough && !zz && step <= 1e-9) { snprintf(op->error, sizeof op->error, "pocket: stepover must be > 0"); return 0; }
    int nf = p->finish_n > 0 ? p->finish_n : 0; double fs = p->finish_step;
    int in2out = p->pk_ok ? pkb(p, 0x1258) : 1, fin_all = p->pk_ok ? (pkb(p, 0x1230) || pkb(p, 0x1436)) : 1;
    double smooth = p->pk_ok ? pkd(p, 0x1400) : 0;

    PkFin F;                                                 /* the finish page (cam2d_pock_fin.c) */
    PkFirst fe = { { 0, 0 }, { 0, 0 }, 0, 0 };
    if (!pk_fin_init(p, rough, method, &F, op->error, sizeof op->error)) return 0;

    Gls reg; PV mid; int pk_open = 0;
    if (pk_type == 4 && !g_pk_tc_in) {   /* process_open_pocket (cam2d_pock_open.c): 2 = the open-pocket cutting method wrote the cut, 1 = an open chain closed by FUN_1801e5c70 (reg, mid, fe set) */
        pk_open = pko_open(f, op, b, minfil, cam_chain_tol(mcam_is_metric(f)), ctol, &reg, &mid, &fe);
        if (pk_open < 0) return 0;
        if (pk_open == 2) return 1;
    }
    if (pk_open == 1) { /* reg / mid / fe from the open chain */ }
    else if (g_pk_tc_in) { reg = gls_copy(g_pk_tc_in); for (int i = 0; i < reg.n; i++) reg.l[i].island = gl_area(&reg.l[i]) < 0; mid = reg.n ? gt_pt(&reg.l[0].e[0], 0.5) : pv(0, 0); }
    else if (!pk_region(f, op, &reg, &mid, tol)) { snprintf(op->error, sizeof op->error, "pocket needs closed planar chains"); return 0; }
    for (int i = 0; i < op->nchains && !fe.ok; i++) {      /* the first closed chain, in its own direction (pk_region's first) */
        const Chain *ch = &op->chains[i]; if (!ch->closed || ch->avoid) continue;
        Gl l; if (!pk_chain_gl(f, ch, &l)) continue;
        fe.a = gt_start(&l.e[0]); fe.b = gt_end(&l.e[0]); fe.ccw = gl_area(&l) > 0; fe.ok = 1; gl_free(&l);
    }
    /* pockets: each outer loop with the islands inside it */
    double tc_off = g_pk_tc_in ? 0.0 : R + p->stock_xy;     /* srough: the level region is the tool-centre region (see g_pk_tc_in) */
    if (pk_type == 1 && !rough) { gls_free(&reg); snprintf(op->error, sizeof op->error, "pocket type Facing without roughing: NOT DECODED"); return 0; }
    if (pk_type == 1 && p->finish_n > 0 && p->finish_step > 0) { gls_free(&reg); snprintf(op->error, sizeof op->error, "pocket type Facing with finish passes: NOT DECODED"); return 0; }
    g_pk_face_chain = &reg;
    /* depths (calc_depth_cuts: the High Speed depth list is the same arithmetic, cam2d.c hst_depths) */
    double zl[200]; int nrough = 1; double rstep = 0;
    int nz = hst_depths(p, zl, 200, &nrough, &rstep);
    int depth_on = p->mc_depth_on || p->stepdown > 1e-6;
    double *isl_zs = malloc(sizeof(double) * (size_t)(reg.n ? reg.n : 1)); int isl_nzs = 0;
    int flat = g_pk_tc_in ? 1 : pk_isl_zs(f, op, isl_zs, reg.n, &isl_nzs, pk_open == 1);
    if (!flat || (isl_f && isl_nzs != reg.n)) { free(isl_zs); gls_free(&reg); snprintf(op->error, sizeof op->error, "Warning - the boundary does not lie in the construction plane"); return 0; }   /* check_chains_3d l.313530-313541: post_pock_warning 0x2309 (mccoreres 8969), no toolpath: V-5, types 0 / 1 / 2 */   /* FUN_1801c5250 l.313630-313642 for type 2 with depth cuts: each depth on its own */
    int taper = p->pk_ok && !g_pk_tc_in && pko_taper_on(p);                     /* FUN_1801c5250 cVar4: by depth, l.313636-313641; process_pocket(.., cVar4, 0x1290, 0x1298, ..) l.313707, process_open_pocket l.313694 -> l.334972; cam2d_pock_taper.c */
    /* finish_with_rough: GeneratePocketToolpath l.313559-313567 = 0x126b (pock_prm +0x43), Use island depths 0x892 (op +0x82a), tapered walls
       (cVar4, FUN_180056a70) or Island facing; the Surface rough levels: msurf_map l.339989 sets it to 1 (GeneratePocketToolpath is not run).
       pock_finish_all l.313514-313521: 0x1230 or 0x1436 */
    F.fwr = g_pk_tc_in ? 1 : (pkb(p, 0x126b) || p->mc_isl_depths || taper || pk_type == 2); F.finall = fin_all;
    if (taper && pkb(p, 0x1436)) { free(isl_zs); gls_free(&reg); snprintf(op->error, sizeof op->error, "pocket with tapered walls and thin wall finishing (FUN_1801cad80 per depth, FUN_1801c5250 l.313729-313733): NOT PORTED"); return 0; }
    if (taper && isl_nzs != reg.n) { free(isl_zs); gls_free(&reg); snprintf(op->error, sizeof op->error, "pocket with tapered walls: a bound without its chain Z (the wall offset of FUN_1801d34b0 starts at the bound's Z): NOT PORTED"); return 0; }
    if (taper && nf > 0 && pks(p, 0x1420) > 0) snprintf(op->warning, sizeof op->warning, "Spring passes not output on tapered wall pockets");   /* FUN_1801c5250 l.313752-313768: post_warning(0x96b) (mcmillres), no spring passes */
    /* the depth and the top of stock of a pocket (FUN_1801c5250 l.313602-313612: set_depths(op, Z of the pocket's first non-island bound, ..), per chain pocket): an incremental depth / top is on that Z, not on chain 0's (dep_cz). One list when no chain pocket differs */
    typedef struct { double zl[200], rstep; int nz, nrough; CamParams p; Gls reg; double *zs; } PkCp;
    int ncp = 1, zdiff = 0;
    if (!g_pk_tc_in && (p->dep_inc || p->top_inc) && isl_nzs == reg.n) {
        int n = 0;
        for (int i = 0; i < reg.n; i++) if (!reg.l[i].island) { n++; if (fabs(isl_zs[i] - p->dep_cz) > CAM_SYS_TOL) zdiff = 1; }
        if (zdiff && n) ncp = n;
    }
    /* Facing: process_pocket per chain pocket (l.313707): the outer bounds are grown one chain pocket at a time (pk_face_tc), not merged across chains */
    if (!g_pk_tc_in && !zdiff && pk_type == 1 && isl_nzs == reg.n) {
        int n = 0; for (int i = 0; i < reg.n; i++) if (!reg.l[i].island) n++;
        if (n > 1) ncp = n;
    }
    PkCp *cps = calloc((size_t)ncp, sizeof(PkCp));
    if (ncp > 1) {
        int c = 0;
        for (int i = 0; i < reg.n && c < ncp; i++) {
            if (reg.l[i].island) continue;
            PkCp *C = &cps[c]; C->p = *p;
            if (zdiff) pk_cp_for_z(&C->p, p, isl_zs[i]);
            C->zs = malloc(sizeof(double) * (size_t)reg.n); int m = 0;
            Gls one = { &reg.l[i], 1, 1 };
            for (int j = 0; j < reg.n; j++) if (j == i || (reg.l[j].island && gls_inside(&one, gt_pt(&reg.l[j].e[0], 0), tol))) { gls_push(&C->reg, gl_copy(&reg.l[j])); C->zs[m++] = isl_zs[j]; }
            c++;
        }
    } else { cps[0].p = *p; cps[0].reg = gls_copy(&reg); cps[0].zs = malloc(sizeof(double) * (size_t)(reg.n ? reg.n : 1)); memcpy(cps[0].zs, isl_zs, sizeof(double) * (size_t)reg.n); }
    int np = 0, capp = 0; PkPocket *pk = NULL;
    Gls reg_t = { 0 };                                                  /* tapered walls: the bounds of the depth (process_pocket local_98, l.323256) */
    Gls reg_g = { 0 }; double *zs_g = NULL; int reg_g_own = 0;           /* the loop's region, owned for Island facing: freed on the way out */
    for (int cpi = 0; cpi < ncp; cpi++) {
    PkCp *C = &cps[cpi];
    C->nz = hst_depths(&C->p, C->zl, 200, &C->nrough, &C->rstep);
    const CamParams *cp_p = zdiff ? &C->p : p;
    const double *zl = C->zl; int nz = C->nz;
    int ng = (isl_f || taper) ? nz : 1;
    for (int gi = 0; gi < ng; gi++) {
    zs_g = NULL;
    reg_g = isl_f ? pk_isl_region(cp_p, &C->reg, C->zs, zl[gi], depth_on, &zs_g) : (g_pk_tc_in ? reg : C->reg); reg_g_own = isl_f;   /* FUN_1801c7970 l.313670: the islands below the depth unlinked */
    int gpk0 = np;
    if (taper && !pk_taper_region(cp_p, &reg_g, isl_f ? zs_g : C->zs, zl[gi], pk_type == 1, minfil, &reg_t, op->error, sizeof op->error)) goto fail;   /* the first FUN_1801d4620, l.323256 */
    const Gls *reg_b = taper ? &reg_t : &reg_g;                                  /* machine_pocket's chains */
    double tc_off_g = taper ? R : tc_off;                                        /* the second FUN_1801d4620, l.323261: D/2 (the stock is in the taper offset) */
    Gls tc_all = g_pk_tc_in ? gls_copy(&reg) : !reg_b->n ? (Gls){ 0 } : pk_type == 1 ? pk_face_tc(reg_b, pkd(p, 0x1388) - R, tc_off_g, minfil, tol) : gls_offset(reg_b, -tc_off_g, minfil, tol);   /* per chain pocket: process_pocket(local_b0, ..) l.313707 on the bounds extract_pocket gave, l.313668 */
    if (getenv("PK_DEBUG")) {
        Gls *L[2] = { &reg, &tc_all }; const char *nm[2] = { "reg", "tc" };
        for (int w = 0; w < 2; w++) for (int a = 0; a < L[w]->n; a++) {
            Gl *l = &L[w]->l[a]; double gap = 0;
            for (int k = 0; k < l->n; k++) { PV e1 = gt_end(&l->e[k]), s1 = gt_start(&l->e[(k + 1) % l->n]); gap = fmax(gap, hypot(e1.x - s1.x, e1.y - s1.y)); }
            CPath cp = gl_sample(l, 0.01);
            fprintf(stderr, "%s %d: n %d island %d area %.3f sampled %.3f maxgap %.4g\n", nm[w], a, l->n, l->island, gl_area(l), cl_area(&cp), gap); free(cp.xy);
            if (getenv("PK_DEBUG")[0] == '2') for (int k = 0; k < l->n; k++) { Gt *e = &l->e[k]; PV s1 = gt_start(e), e1 = gt_end(e); fprintf(stderr, "   %c %.3f %.3f -> %.3f %.3f%s\n", e->t, s1.x, s1.y, e1.x, e1.y, e->t == 'A' ? " (arc)" : ""); }
        }
    }
    if (!tc_all.n && !isl_f && !taper) { gls_free(&tc_all); snprintf(op->error, sizeof op->error, "tool does not fit in the pocket"); goto fail; }
    for (int i = 0; i < tc_all.n; i++) {
        if (tc_all.l[i].island) continue;
        if (np == capp) { capp = capp ? capp * 2 : tc_all.n + 4; pk = realloc(pk, (size_t)capp * sizeof(PkPocket)); }
        PkPocket *q = &pk[np++]; memset(q, 0, sizeof *q); q->zi = -1; q->cp = cpi;
        {   /* the marker's pocket number (bnci_wr_pock_marker param_2 = process_pocket param_12 = sVar15, l.313710, FUN_1801d36c0, l.339443): the chain pocket the bound comes from (extract_pocket, FUN_1801c5250 l.313668) */
            int ord = 0, best = -1, best_in = -1; double bd = 1e300, inb = 1e300;
            for (int oi = 0; oi < reg_g.n; oi++) {
                if (reg_g.l[oi].island) continue;
                Gls one = { &reg_g.l[oi], 1, 1 }; PV t0 = gt_pt(&tc_all.l[i].e[0], 0);
                if (gls_inside(&one, t0, tol)) {                               /* the innermost: sort_pock_bounds l.308-318 starts a new group for a bound whose parent is an island, so a pocket nested in an island is its own chain pocket */
                    double a = fabs(gl_area(&reg_g.l[oi]));
                    if (a < inb) { inb = a; best_in = ord; }
                }
                double d = gl_closest(&reg_g.l[oi], t0, NULL, NULL, NULL); if (d < bd) { bd = d; best = ord; }   /* Facing: the tool-centre bound lies outside the chain */
                ord++;
            }
            if (best_in >= 0) best = best_in;
            q->sk = ncp > 1 ? cpi : (best < 0 ? 0 : best);
        }
        if (taper) q->tr = gls_copy(&reg_t);
        if (isl_f || taper) {
            q->zi = gi; q->top_s = gi == 0 ? cp_p->top_z : zl[gi - 1];                 /* adjust_depths (FUN_1801ede50 D:1801ee708, FUN_1801ef830 l.340447, FUN_1801cc170 l.317982): the depth's top of stock is the previous depth */
            for (int k2 = gpk0; k2 < np - 1; k2++) if (pk[k2].sk == q->sk) q->sub++;           /* marker l.339443: sVar16 + param_7 = the pocket's index in machine_pocket (D:1801d3963) */
        }
        gls_push(&q->tc, gl_copy(&tc_all.l[i]));
        PV probe = gt_pt(&tc_all.l[i].e[0], 0);
        for (int j = 0; j < tc_all.n; j++) {
            if (!tc_all.l[j].island) continue;
            Gls one = { &tc_all.l[i], 1, 1 };
            if (gls_inside(&one, gt_pt(&tc_all.l[j].e[0], 0), tol)) gls_push(&q->tc, gl_copy(&tc_all.l[j]));
        }
        (void)probe;
        q->rb = nf && fs > 0 ? gls_offset(&q->tc, -(nf * fs), minfil, tol) : gls_copy(&q->tc);
        if (getenv("PK_DEBUG")) { fprintf(stderr, "pocket %d: tc", np - 1); for (int a = 0; a < q->tc.n; a++) fprintf(stderr, " %.3f/%d", gl_area(&q->tc.l[a]), q->tc.l[a].n);
            fprintf(stderr, " rb"); for (int a = 0; a < q->rb.n; a++) fprintf(stderr, " %.3f/%d", gl_area(&q->rb.l[a]), q->rb.l[a].n); fprintf(stderr, " (nf %d fs %g stock %g)\n", nf, fs, p->stock_xy); }
        if (nf && fs > 0) {   /* ours (safety): an inward offset cannot add area; more means the offset substitute (gls_offset for MCCECalcOffset)
                                 rebuilt a wrong loop (seen on Surface rough levels of hundreds of short lines) - refused instead of cut */
            double at = 0, ar = 0; for (int a = 0; a < q->tc.n; a++) at += gl_area(&q->tc.l[a]); for (int a = 0; a < q->rb.n; a++) ar += gl_area(&q->rb.l[a]);
            if (ar > at + fmax(1e-9, fabs(at) * 1e-6) && getenv("PK_DEBUG")) for (int a = 0; a < q->rb.n; a++) for (int e = 0; e < q->rb.l[a].n; e++) { const Gt *g = &q->rb.l[a].e[e]; if (g->t == 'A' && (g->r > 0.05 || fabs(g->sw) > 3.5)) fprintf(stderr, "rb %d ent %d arc c %.5f %.5f r %.5f sa %.4f sw %.4f\n", a, e, g->c.x, g->c.y, g->r, g->sa, g->sw); }
            if (ar > at + fmax(1e-9, fabs(at) * 1e-6)) { snprintf(op->error, sizeof op->error, "pocket: the rough boundary (the tool-centre bounds offset by the finish passes) came out larger than the bounds (%.4g > %.4g): the offset substitute failed (ours, MCCECalcOffset NOT PORTED)", ar, at); gls_free(&tc_all); goto fail; }
        }
    }
    gls_free(&tc_all);
    /* control only: the finish base of each pocket is its chain bounds (FUN_1801cc170 local_168 = param_1, l.317964):
       the outer chain loop holding the pocket's tool-centre bound and the chain islands inside it; one pocket per chain
       outer loop here (a chain pocket the tool splits into sub-pockets would be finished once per sub-pocket by
       machine_pocket - not ported: refused) */
    if (nf > 0 && F.ctl_only) {
        int *used = calloc(reg_b->n + 1, sizeof(int));
        for (int k = gpk0; k < np; k++) {
            PV q0 = gt_pt(&pk[k].tc.l[0].e[0], 0); int m = -1;
            for (int i = 0; i < reg_b->n; i++) { if (reg_b->l[i].island) continue; Gls one = { &reg_b->l[i], 1, 1 }; if (gls_inside(&one, q0, tol)) { m = i; break; } }
            if (m < 0 || used[m]++) { free(used); snprintf(op->error, sizeof op->error, "pocket finish compensation in control on a pocket the tool splits: NOT DECODED here"); goto fail; }
            gls_push(&pk[k].cb, gl_copy(&reg_b->l[m]));
            for (int i = 0; i < reg_b->n; i++) {
                if (!reg_b->l[i].island) continue;
                Gls one = { &reg_b->l[m], 1, 1 };
                if (gls_inside(&one, gt_pt(&reg_b->l[i].e[0], 0), tol)) gls_push(&pk[k].cb, gl_copy(&reg_b->l[i]));
            }
        }
        free(used);
    }
    if (isl_f) {   /* FUN_1801c68e0 (l.313721), per chain pocket: the top pockets of the depth */
        int ord = 0; double zprev = gi == 0 ? cp_p->top_z : zl[gi - 1];
        for (int oi = 0; oi < reg_g.n; oi++) {
            if (reg_g.l[oi].island) continue;
            if (!pk_isl_faces(op, &reg_g, zs_g, oi, ncp > 1 ? cpi : ord, gi, zl[gi], zprev, zprev, minfil, tol, &pk, &np, &capp)) goto fail;
            for (int k2 = gpk0; k2 < np; k2++) if (pk[k2].face) pk[k2].cp = cpi;
            ord++;
        }
        gls_free(&reg_g); free(zs_g); reg_g_own = 0;
    }
    gls_free(&reg_t); memset(&reg_t, 0, sizeof reg_t);
    }
    }
    if (np == 0) { gls_free(&reg); free(isl_zs); for (int c = 0; c < ncp; c++) { gls_free(&cps[c].reg); free(cps[c].zs); } free(cps); free(pk); snprintf(op->error, sizeof op->error, "tool does not fit in the pocket"); return 0; }
    if (nf > 0) for (int k = 0; k < np; k++) {
        pk[k].fb = calloc(nf, sizeof(Gls)); pk[k].fin = calloc(nf, sizeof(Gls)); pk[k].par = calloc(nf, sizeof(int *));
        pk[k].fb_ok = calloc(nf, 1); pk[k].fin_ok = calloc(nf, 1);
    }
    double zf = p->top_z + p->feed_plane;
    int any = 0;
    /* roughing: loops once per pocket (cut at every depth below) */
    if (rough) for (int k = 0; k < np; k++) {
        PkPocket *q = &pk[k];
        if (!q->rb.n) { q->skip = 1; continue; }
        PkCp *C = &cps[q->cp];
        double zq = q->zi >= 0 ? (q->face ? q->ztop : C->zl[q->zi]) : C->zl[0];        /* the plan's depth: by depth (Island facing) its own, else the first (shifted to the others) */
        CamParams spv;
        g_pk_ep = (isl_f || taper) ? pk_isl_sec_params(&C->p, &spv, q->top_s, zq) : (zdiff ? &C->p : NULL);   /* set_heights / top of stock of the section (entry helix height), the chain pocket's depths */
        g_pk_isl_chk = q->face ? &q->chk : NULL;                                        /* _enter_pocket l.307880: FUN_1801bfd60 -> FUN_1801c1fc0 when facing */
        if (taper) g_pk_face_chain = &q->tr;                                            /* tapered: machine_pocket's param_1 = local_98, l.323288 */
        const Gls *regc = q->face ? &q->rb : taper ? &q->tr : &reg; double regoff = q->face ? 0 : (taper ? R : tc_off) + (nf && fs > 0 ? nf * fs : 0);
        if (zz) {   /* One Way / Zigzag: a path, its start and entry per outer loop of the rough boundary */
            if (!pk_zz_plan(f, op, &q->rb, zq, &q->zz, &q->nzz)) goto fail;
            int live = 0; for (int i = 0; i < q->nzz; i++) live += !q->zz[i].skip;
            if (!live) q->skip = 1;
            continue;
        }
        if (method == 1) {
            { const McSysTol *st = mc_systol_units(mcam_is_metric(f)); rm_tol = st->tol; rm_nc_tol = st->nc_tol; rm_chain_tol = st->chain_tol; }   /* mccore tol / nc_tol / chain_tol of the part units, as gen_remach */
            Gls orb = gls_copy(&q->rb); pk_ps_rb_start(&orb, regc, regoff);            /* the entity 0 of the first pass, as the other spiral methods */
            int rok = pk_ov_loops(&orb, p->tool_dia, step, p->stepover_pct, pkd(p, 0x1270), pkb(p, 0x123a), in2out, p->lead_on, nctol, &q->loops, op->warning, sizeof op->warning);
            gls_free(&orb);
            if (!rok) { q->skip = 1; continue; }
        }
        else if (method == 5) { if (!pk_ts_loops(&q->rb, step, p->pk_ok ? pkb(p, 0x123a) : 1, in2out, tol, nctol, &q->loops, op->error, sizeof op->error)) goto fail; }
        else if (method == 8 ? !pk_hs_loops(&q->rb, step, smooth, minfil_hs, in2out, tol, nctol, p->tool_dia, &q->xs, &q->loops, op->error, sizeof op->error)
                        : !pk_ps_loops_reg(&q->rb, regc, regoff, step, minfil_hs, in2out, tol, &q->loops, op->error, sizeof op->error)) goto fail;
        if (!q->loops.n) { q->skip = 1; continue; }
        if (burial) {   /* machine_pocket l.322547-322555 (D:1801d3849): medTol = max(nc_tol x 50, tool diameter x 0.1) */
            double medTol = nctol * 50.0;
            if (nctol * 50.0 <= p->tool_dia * 0.1) medTol = p->tool_dia * 0.1;
            q->medax = md_build(&q->tc, nctol, medTol);
        }
        PV st = gt_start(&q->loops.l[0].e[0]);
        /* the entry (cam2d_pock_entry.c, _enter_pocket FUN_1801bc830): planned at the first depth, shifted to the others */
        if (!pk_entry_plan(f, op, &q->rb, st, zq, &q->ent)) goto fail;
        if (q->ent.skip) q->skip = 1;                        /* entry failed: pocket skipped (0x12d1 / 0x134a) */
    }
    g_pk_ep = NULL; g_pk_isl_chk = NULL;
    /* the sections (bnci_wr_pock_marker, one per pocket and depth for the roughing and per pass for the finishing) in
       wr_pock_toolpath's order (pk_sec_cmp). Finishing at every depth with 0x1230 / thin wall (pock_finish_all), else at the
       last (wr_pock_toolpath l.~315470). Thin wall (FUN_1801cad80 l.316904, 0x1436 / 0x1438 = nZ): the finish sections of
       each rough depth cut again nZ - 1 times, copy t (1 .. nZ - 1) raised by (nZ - t) x step / nZ (step = the rough depth
       step pock_rgh_step; depth cuts off: the call at 0x1801c5ba4 with top - depth); the originals are t = nZ.
       wr_pock_depth_cuts (l.~315100) writes the copies at the rough depths only. */
    int nsec = 0, csec = 0; PkSec *sec = NULL;
#define PK_SEC_ADD(F_, K_, ZI_, J_, TH_, TI_, Z_, POS_) do { if (nsec == csec) { csec = csec ? 2 * csec : 64; sec = realloc(sec, csec * sizeof *sec); } \
        sec[nsec].fin = F_; sec[nsec].k = K_; sec[nsec].zi = ZI_; sec[nsec].j = J_; sec[nsec].thin = TH_; sec[nsec].ti = TI_; sec[nsec].z = Z_; sec[nsec].pos = POS_; sec[nsec].sk = K_; sec[nsec].face = 0; sec[nsec].depth = Z_; sec[nsec].sub = 0; sec[nsec].p9 = 0; nsec++; } while (0)
    if (rough) for (int k = 0; k < np; k++) if (!pk[k].skip) for (int z = 0; z < cps[pk[k].cp].nz; z++) {
        if (pk[k].zi >= 0 && pk[k].zi != z) continue;
        PK_SEC_ADD(0, k, z, 0, 0, 0, cps[pk[k].cp].zl[z], ((long)k * 1000 + z) * 100000L);
        if (isl_f || taper) { sec[nsec - 1].sk = pk[k].sk; sec[nsec - 1].sub = pk[k].sub; sec[nsec - 1].face = pk[k].face; if (pk[k].face) sec[nsec - 1].depth = pk[k].ztop; }   /* the sort depth stays the depth (pock_facing_sort_depth l.314335), the top cut at its Z */
    }
    if (nf > 0) {
        int depth_on2 = p->mc_depth_on || p->stepdown > 1e-6;
        for (int k = 0; k < np; k++) {
            PkCp *C = &cps[pk[k].cp]; int z0i = fin_all ? 0 : C->nz - 1;
            double tstep = (depth_on2 ? C->rstep : C->p.top_z - C->zl[0]) / (F.thin ? F.nzt : 1);
            for (int z = z0i; z < C->nz; z++) {
                if (pk[k].face || (pk[k].zi >= 0 && pk[k].zi != z)) continue;   /* top pockets: roughing only (FUN_1801c68e0 process_pocket param_11 = 0); by depth: its depth */
                for (int j = 0; j < nf; j++) { PK_SEC_ADD(1, k, z, j, F.thin, F.nzt, C->zl[z], ((long)k * 1000 + z) * 100000L + 1 + j); if (isl_f || taper) { sec[nsec - 1].sk = pk[k].sk; sec[nsec - 1].sub = pk[k].sub; } }
                if (F.thin && z < C->nrough)
                    for (int t = 1; t < F.nzt; t++)
                        for (int j = 0; j < nf; j++) PK_SEC_ADD(1, k, z, j, 1, t, C->zl[z] + (F.nzt - t) * tstep, ((long)k * 1000 + z) * 100000L + 1 + nf * t + j);
            }
        }
    }
#undef PK_SEC_ADD
    g_pk_sec_fin = &F;
    /* the Surface rough levels (op code 0xe, process_pocket param_9 = 1) are not written through GeneratePocketToolpath's temp file and
       wr_pock_toolpath: machine_pocket's order stands, per pocket its roughing then its finish passes (FUN_1801d36c0 l.322583-322595 ->
       LAB_1801d3b83 l.322565-322568) = the write position */
    if (nsec > 1 && !g_pk_tc_in) qsort(sec, nsec, sizeof *sec, pk_sec_cmp);
    PkRoughCtx rc = { p, step, ctol, nctol, zf, method, in2out, burial, pk_type, minfil, tol, op->error, sizeof op->error, 0 };
    g_pk_sec_on = p->pk_ok && pk_type != 3 && !g_pk_tc_in; g_pk_lastk = -1; g_pk_lastz = 0;     /* N9-12 */
    /* Move.sof at each section start (pk_sec_start); not with the finish compensation in control: FUN_1801cd200 then sets cur_op +0x568 /
       +0x56a (decomp/mcmill_all.c l.318449-318452), which the finish blocks' 1013 / 1016 words read - no corpus section shows the result */
    op->nci_blocks = g_pk_sec_on && !F.ctl; g_pk_fin_sec = 0;
    g_pk_kp = 0; gls_free(&g_pk_kb); g_pk_ktd_err = 0; g_pk_cursub = g_pk_lastsub = 0; g_pk_ctol = ctol;   /* pock_ktd_bnd emptied (wr_pock_toolpath l.316574-316587), lVar19 = 0 (l.315432) */
    for (int i = 0; i < nsec; i++) {
        PkPocket *q = &pk[sec[i].k];
        g_pk_curk = q->sk;                                                       /* local_93e: the marker's chain pocket, l.315556 */
        g_pk_cursub = sec[i].sub;                                                /* local_91e: the marker's sub-pocket (+0x28), l.315563 */
        g_pk_fin_sec = sec[i].fin;
        const CamParams *secp = p; CamParams spv;
        if (taper) g_pk_face_chain = &q->tr;
        if (isl_f || taper) {
            secp = pk_isl_sec_params(&cps[q->cp].p, &spv, q->top_s, sec[i].depth); rc.p = secp; rc.zf = secp->top_z + secp->feed_plane;   /* set_heights(depth, the section's top) (wr_pock_toolpath l.315582) */
        }
        else if (zdiff) { secp = &cps[q->cp].p; rc.p = secp; rc.zf = secp->top_z + secp->feed_plane; }
        g_pk_ref = (isl_f || taper) ? secp->top_z : sec[i].zi == 0 && sec[i].thin && sec[i].ti > 1 && sec[i].ti < F.nzt ? sec[i].z + (sec[i].z - cps[q->cp].zl[0]) / (F.nzt - sec[i].ti) : sec[i].zi > 0 ? sec[i].z + (cps[q->cp].zl[sec[i].zi - 1] - cps[q->cp].zl[sec[i].zi]) : cps[q->cp].p.top_z;   /* (thin wall copies at the first depth, FUN_1801cad80 l.317014-317016: from the second copy on, the top is the Z of the copy before it) the marker's top for set_heights l.315582: Island facing the section's top (each depth generated on its own, pk_isl_sec_params); others wr_pock_depth_cuts l.315181: the source marker's Z less the shift of the previous copy */
        if (!sec[i].fin) {
            int n0 = b->n;
            pk_rough_depth(b, &rc, q, sec[i].depth, &any); if (rc.failed) { free(sec); goto fail; }
            /* the roughing's keep tool down between depth cuts (FUN_1801ede50 l.339478-339504, FUN_1801ef830 l.340363-340394): pock_tool_down,
               depth cuts on (op +0x800), 0x891 (op +0x829), entry off (0x12a0, pock_prm +0x78), not facing islands: a marker pair with the
               roughing bounds (the pocket's rough boundary; Zigzag: rotated back, l.340386) at the end of the section -> pending */
            if (g_pk_sec_on && b->n > n0 && p->mc_depth_on && p->mc_keep_down && !pkb(p, 0x12a0) && !q->face) pk_ktd_pend(&q->rb, sec[i].depth);
        }
        else {
            int kdown = (rough && !q->skip) || (sec[i].k > 0 && pk[sec[i].k - 1].cp == q->cp);   /* generation order (see pk_fin_section) */
            if (!pk_fin_section(b, secp, &F, q, sec[i].k, sec[i].j, sec[i].z, &fe, &any, kdown, minfil, minfil_hs, tol, ctol, op->error, sizeof op->error)) { free(sec); goto fail; }
        }
        if (g_pk_ktd_err) { snprintf(op->error, sizeof op->error, "%s", g_pk_ktd_msg); free(sec); goto fail; }
    }
    free(sec); gls_free(&g_pk_kb); g_pk_kp = 0;
    if (!any) { snprintf(op->error, sizeof op->error, "pocket: nothing to cut"); goto fail; }
    if (g_pk_sec_on) { PkH H = pk_h(zdiff ? &cps[ncp - 1].p : p, g_pk_lastz, zdiff ? cps[ncp - 1].p.top_z : p->top_z); if ((p->mc_clr_on || p->mc_ret_on) && (p->ht_inc & 4)) { H.f = g_pk_lastz + p->feed_plane; if (H.f < g_pk_lastz) H.f = g_pk_lastz; } pk_up(b, p, H, 1); g_pk_sec_on = 0; }   /* wr_pock_toolpath l.316546-316554: set_heights(depth, top), the feed plane, clear_move */
    else {
    pk_retract(b, p);
    { V3 e = lastp(b); if (e.z < p->clearance - 1e-9) mv(b, 1, e.x, e.y, p->clearance, 0); }
    }
    for (int k = 0; k < np; k++) pk_free(&pk[k], nf);
    free(pk); gls_free(&reg); free(isl_zs); for (int c = 0; c < ncp; c++) { gls_free(&cps[c].reg); free(cps[c].zs); } free(cps);
    return 1;
fail:
    gls_free(&reg_t);
    if (reg_g_own) { gls_free(&reg_g); free(zs_g); }
    g_pk_sec_on = 0; gls_free(&g_pk_kb); g_pk_kp = 0;
    g_pk_ep = NULL; g_pk_isl_chk = NULL;
    for (int k = 0; k < np; k++) pk_free(&pk[k], nf);
    free(pk); gls_free(&reg); free(isl_zs); for (int c = 0; c < ncp; c++) { gls_free(&cps[c].reg); free(cps[c].zs); } free(cps);
    return 0;
}

/* the zigzag engine's entry (cam2d.c gen_pocket_zigzag, its first pass at (sx, sy), depth z): the shared entry
   (cam2d_pock_entry.c) in the rough boundary (the chains offset by R + wall stock + finish passes x spacing), planned at
   the first depth z_first. Returns 1 when an entry was written (the caller then feeds to the start), 0 for a plunge at
   the start (nothing written; op->error set when the op is refused, or when the entry failed and the op skips such
   pockets (0x12d1 / 0x134a), which this engine does not do: refused). */
static int pk_entry_helix(const McamFile *f, CamOp *op, MB *b, double sx, double sy, double z, double z_first, int first) {
    CamParams *p = &op->p;
    double R = p->tool_dia / 2, tol = cam_chain_tol(mcam_is_metric(f)) * 0.5;
    int roll = p->pk_ok ? p->pk_roll : 1; double minfil = roll == 0 ? M_PI : roll == 1 ? 0.785398 : 0.01;
    int nf = p->finish_n > 0 ? p->finish_n : 0;
    Gls reg; PV mid; if (!pk_region(f, op, &reg, &mid, tol)) return 0;
    Gls tc = gls_offset(&reg, -(R + p->stock_xy), minfil, tol);
    Gls rb = nf && p->finish_step > 0 ? gls_offset(&tc, -(nf * p->finish_step), minfil, tol) : gls_copy(&tc);
    PkEntry e; int ok = pk_entry_plan(f, op, &rb, pv(sx, sy), z_first, &e);
    gls_free(&reg); gls_free(&tc); gls_free(&rb);
    if (!ok) return 0;
    if (e.skip) { snprintf(op->error, sizeof op->error, "pocket: the entry fails and the op skips such pockets (0x12d1 / 0x134a): not supported by this engine"); return 0; }
    if (e.kind == PKE_PLUNGE) return 0;
    double zf = p->top_z + p->feed_plane, ctol = p->tol > 0 ? p->tol : tol;
    pk_entry_emit(b, p, &e, z, first ? p->clearance : fmax(zf, p->retract_z), ctol);
    pk_entry_free(&e);
    return 1;
}
