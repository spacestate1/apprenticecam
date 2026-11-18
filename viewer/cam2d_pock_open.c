/* cam2d_pock_open.c - Pocket type 4 Open pocket (record 0x13fa = 4; POCKMAIN.cpp process_open_pocket, Pockopen.cpp; decomp/mcmill_all.c
 * l.334312-335760 and mccore helpers). Included by cam2d_pock.c after cam2d_pock_zz.c. Decode notes: formats/pocket_engine_b6.md, section "Open pocket".
 *
 * GeneratePocketToolpath (FUN_1801c5250 l.313685-313711) hands every pocket to process_open_pocket when a bound of it is open (FUN_1801e5840) or when
 * FUN_1801e57f0 holds (type 4, 0x13d8 on, 0x13f0 off), and to process_pocket otherwise. Panel 3001 (HSTPocketOpenPage FUN_180180e00 / FUN_180180e70
 * l.267827 / 267850, op +0x3468.. = record 0x13e0..): 0x13e0 overlap %, 0x13e8 overlap distance, 0x13f0 "Use open pocket cutting method", 0x13d8 = NOT
 * "Use Standard pocket for closed chains".
 *
 * Ported:
 *  - an open chain, 0x13f0 off: FUN_1801e5c70 close_pocket (l.334314): the chain extended to the line offset from its chord by the overlap
 *    (FUN_1801e6220, FUN_1801e7f30), closed with a line; then the ordinary pocket (process_pocket l.334972).
 *  - an open chain, 0x13f0 on: FUN_1801e78a0 + FUN_1801e6970 (ccomp_pock of the chain), machine_pocket with param_3 = 0 -> FUN_1801e7040 (l.334991):
 *    offset passes of the tool-centre path clipped by FUN_1801e7930, cut from the last to the first, links by FUN_1801e0f80.
 * Refused (op->error says NOT DECODED / NOT PORTED): closed chains with 0x13d8 on (MCCECalcClosedOpen, mcce PockClosedOpen.cpp FUN_1800e53d0); chains whose boxes overlap (sort_pock_bounds on an open bound); tapered walls
 * on the close / process_pocket paths; finish passes with 0x13f0 (FUN_1801cc170 on an open bound); keep tool down between depth cuts with the entry off (FUN_1801e7040 l.335153-335194); tool-centre or tapered offsets
 * of several bounds. Decode notes: formats/pocket_engine_b6.md, "Open pocket".
 */

typedef struct { PV a, b; } PkoLine;

/* An open chain as a list of entities in its own direction (chain_to_pock_bound; cf. pk_chain_gl, without the closure test) */
static int pko_chain_gl(const McamFile *f, const Chain *ch, Gl *out) {
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
    for (int i = 0; i + 1 < out->n; i++) {                   /* snap the lines' ends onto their neighbours, as pk_chain_gl */
        Gt *g = &out->e[i], *h = &out->e[i + 1];
        PV pe = gt_end(g), ps = gt_start(h);
        if (h->t == 'L') h->a = pe; else if (g->t == 'L') g->b = ps;
    }
    return out->n > 0;
}

/* FUN_1801e63e0 l.334564 find_open_pock_bound_direction: pock_ccw of the bound with its closing line */
static int pko_chord_ccw(const Gl *g) {
    Gl c = gl_copy(g);
    gl_push(&c, gt_line(gt_end(&g->e[g->n - 1]), gt_start(&g->e[0])));
    int ccw = gl_area(&c) > 0;
    gl_free(&c);
    return ccw;
}

/* FUN_1801e6220 l.334523 create_unbounded_trim_line: the chord offset by pct % x D away from n; side 'L' n = right normal, 'R' left normal (disassembly
   0x1801e6220: the chord's two points stay intact; decompiler shows them merged); l_offset moves the line towards the point p0 - n */
static int pko_trim_line(const Gl *g, double pct, char side, double D, PkoLine *L, PV *n) {
    PV p0 = gt_start(&g->e[0]), p1 = gt_end(&g->e[g->n - 1]);
    double dx = p1.x - p0.x, dy = p1.y - p0.y;
    if (side == 'L') { n->x = dy; n->y = -dx; } else { n->x = -dy; n->y = dx; }
    PV s = pv(p0.x - n->x, p0.y - n->y);
    double L2 = dx * dx + dy * dy;
    if (!(0 < L2)) return 0;
    double t = ((s.x - p0.x) * dx + (s.y - p0.y) * dy) / L2;          /* l_offset: proj_pt_ln */
    PV q = pv(p0.x + t * dx, p0.y + t * dy);
    double ux = s.x - q.x, uy = s.y - q.y, ul = sqrt(ux * ux + uy * uy);
    if (ul == 0.0) return 0;
    double d = pct * 0.01 * D;
    ux = ux / ul * d; uy = uy / ul * d;
    L->a = pv(p0.x + ux, p0.y + uy); L->b = pv(p1.x + ux, p1.y + uy);
    return 1;
}

/* ZB of a loop for zz_inters / zz_in_area / zz_fpai (cf. pk_zz_path) */
static void pko_zb(ZB *b, const Gl *g, int island) {
    memset(b, 0, sizeof *b);
    b->l = gl_copy(g); b->island = island; b->ccw = gl_area(&b->l) > 0; b->perim = gl_len(&b->l); gl_bbox(&b->l, b->bb);
    b->poly = gl_sample(&b->l, CAM_SYS_TOL * 0.5);
}

/* FUN_1801e5f00 l.334386: the trim line's extent over the chain: pock_bound_line_inters (nc_tol x 0.01, 20 tries) with t on the line, largest t below 0.5 (default
   -10) and smallest t from 0.5 (default 10) */
static int pko_trim_seg(const Gl *g, double pct, char side, double D, PkoLine *seg, PV *n) {
    PkoLine L;
    if (!pko_trim_line(g, pct, side, D, &L, n)) return 0;
    ZB zb; pko_zb(&zb, g, 0); ZBs B = { &zb, 1 };
    static ZI I[ZZ_MAXI + 2]; int c = 0;
    zz_inters(&B, L.a, L.b, g_nc_tol * 0.01, 20, I, &c);
    double lo = -10.0, hi = 10.0;
    for (int k = 1; k <= c; k++) { double t = I[k].t; if (0.5 <= t) { if (t <= hi && hi != t) hi = t; } else if (lo < t) lo = t; }
    PV d = pv(L.b.x - L.a.x, L.b.y - L.a.y);
    seg->a = pv(L.a.x + d.x * lo, L.a.y + d.y * lo); seg->b = pv(L.a.x + d.x * hi, L.a.y + d.y * hi);
    free(zb.poly.xy); gl_free(&zb.l);
    return 1;
}

#include "cam2d_pock_open_k.c"   /* the mccore / systypes geometry helpers (a file of their own for tools/pko_port.c, the DLL unit oracle) */
/* dist_pt_l (mccore 0x1804b32a0): distance to the unbounded line */
static double pko_dist_l(PV p, const PkoLine *L) {
    double dx = L->b.x - L->a.x, dy = L->b.y - L->a.y, l = sqrt(dx * dx + dy * dy);
    if (l <= CAM_SYS_TOL) return hypot(p.x - L->a.x, p.y - L->a.y);
    return fabs(((p.x - L->a.x) * dy - (p.y - L->a.y) * dx) / l);
}
/* proj_pt_ln: foot on the unbounded line, t along it; 0 = degenerate line */
static int pko_foot(PV p, const PkoLine *L, PV *q, double *t) {
    double dx = L->b.x - L->a.x, dy = L->b.y - L->a.y, d = dx * dx + dy * dy;
    if (d == 0.0) return 0;
    double u = ((p.x - L->a.x) * dx + (p.y - L->a.y) * dy) / d;
    if (t) *t = u;
    *q = pv(L->a.x + dx * u, L->a.y + dy * u);
    return 1;
}
/* FUN_1801e5900 l.334062 add_line_to_pock_bound: a line from q to the start (at_end 0) / from the end to q (at_end 1) */
static void pko_add_line(Gl *g, int at_end, PV q) {
    if (at_end) { gl_push(g, gt_line(gt_end(&g->e[g->n - 1]), q)); return; }
    Gt ln = gt_line(q, gt_start(&g->e[0]));
    gl_push(g, ln);
    memmove(g->e + 1, g->e, (size_t)(g->n - 1) * sizeof(Gt));
    g->e[0] = ln;
}
/* FUN_1801e7f30 l.335561: extend the bound's two ends to the line (end by end): the end's entity trimmed at the line's intersection on the entity (when that is also
   on the line, online != 0, and clear of gouge, gchk != 0), else a line added from / to the projection on the line (the same checks) */
static void pko_extend(Gl *g, const Gls *gouge, int online, int gchk, const PkoLine *L) {
    double nct = g_nc_tol;
    for (int end = 0; end < 2; end++) {
        Gt *e = end ? &g->e[g->n - 1] : &g->e[0];
        PV p = end ? gt_end(e) : gt_start(e);
        if (pko_dist_l(p, L) <= nct) continue;
        PV x; int ok = pko_ints(L, e, p, &x);
        if (ok && online) ok = pko_on_line(x, L);
        if (ok && pko_on_gt(x, e)) {
            Gt t = *e;
            int r = pko_trim(&t, x, end);
            if (gchk) { if (!r) goto proj; if (gouge) r = pko_no_gouge(&t, gouge); }
            if (r) { *e = t; continue; }
        }
    proj:;
        PV q; int okp = pko_foot(p, L, &q, NULL);
        if (okp && online) okp = pko_on_line(q, L);
        if (okp && gouge && gchk) { Gt t = gt_line(q, p); okp = pko_no_gouge(&t, gouge); }
        if (okp) pko_add_line(g, end, q);
    }
}

/* FUN_1801e5c70 l.334314 close_pocket: the chain extended to the overlap line (only when the overlap > 0) and closed with a line from its end to its start */
static int pko_close(const Gl *chain, double pct, double D, Gl *out) {
    char side = pko_chord_ccw(chain) ? 'L' : 'R';       /* FUN_1801e57a0 */
    *out = gl_copy(chain);
    PkoLine L; PV n;
    if (!pko_trim_line(chain, pct, side, D, &L, &n)) { gl_free(out); return 0; }
    if (0.0 < pct) pko_extend(out, NULL, 0, 1, &L);     /* FUN_1801e7f30(.., 0, 0, 1, line) */
    pko_add_line(out, 1, gt_start(&out->e[0]));
    return 1;
}

/* ccomp_pock (mccore 0x1805d8140; decomp/mccore_ccomp_pock2.c l.150-189) on the DLL-verified port of MCCECalcContourOffset: side 'R' = negative distance, angle by min_fil,
   p6 != 0 flags 0x40027 else 0x60027, the gap parameter 1e38 when p7 else chain_tol (p8 = 0 here). Results: *out (the entities in order); open bound = dir 0 */
static int pko_ccomp(const Gl *in, int dir, double dist, char side, double minfil, int p6, int p7, double chtol, Gls *out, char *err, size_t errn) {
    McceEnt *ents = calloc((size_t)in->n, sizeof(McceEnt));
    for (int i = 0; i < in->n; i++) {
        McceEnt *x = &ents[i]; const Gt *e = &in->e[i];
        if (e->t == 'L') { x->id = 'L'; x->a[0] = e->a.x; x->a[1] = e->a.y; x->a[2] = e->b.x; x->a[3] = e->b.y; }
        else { x->id = 'A'; x->a[0] = e->c.x; x->a[1] = e->c.y; x->a[2] = e->r; x->a[3] = e->sa; x->a[4] = e->sw; }
    }
    double d = side == 'R' ? -dist : dist;
    double ang = fabs(minfil - 0.01) < CAM_SYS_TOL ? 0.5729577951308232 : fabs(minfil - M_PI) < CAM_SYS_TOL ? 179.1 : 44.99999063801583;
    McceShape s = { ents, in->n, dir, 0 };
    if (getenv("PK_DEBUG")) { fprintf(stderr, "ccomp d %.6f side %c dir %d n %d flags %x p7 %d:", dist, side, dir, in->n, p6 ? 0x40027 : 0x60027, p7); for (int i = 0; i < in->n; i++) { PV a = gt_start(&in->e[i]), b = gt_end(&in->e[i]); fprintf(stderr, " %c(%.3f,%.3f)-(%.3f,%.3f)", in->e[i].t, a.x, a.y, b.x, b.y); } fprintf(stderr, "\n"); }
    McceResult r = mcce_contour_offset(&s, 1, d, p6 ? 0x40027 : 0x60027, ang, p7 ? 1e38 : chtol, g_nc_tol, g_nc_tol * 0.75);
    free(ents);
    memset(out, 0, sizeof *out);
    if (r.refused) { snprintf(err, errn, "open pocket: offset: %s (MCCECalcContourOffset path NOT PORTED)", r.refused); return 0; }
    for (int i = 0; i < r.n; i++) {
        Gl g = { 0 };
        for (int k = 0; k < r.s[i].n; k++) {
            McceEnt *x = &r.s[i].e[k];
            if (x->id == 'L') gl_push(&g, gt_line(pv(x->a[0], x->a[1]), pv(x->a[2], x->a[3])));
            else gl_push(&g, gt_arc(pv(x->a[0], x->a[1]), x->a[2], x->a[3], x->a[4]));
        }
        if (g.n) { g.island = r.s[i].dir; gls_push(out, g); }
    }
    mcce_shapes_free(&r);
    return 1;
}
/* double_up_pock_bound (mccore 0x1805dd710, disassembly): the bound followed by a copy of it reversed (reverse_pock_bound(.., 0) = plain reversal) */
static Gl pko_double_up(const Gl *in) {
    Gl d = gl_copy(in), r = gl_copy(in);
    gl_reverse(&r);
    for (int i = 0; i < r.n; i++) gl_push(&d, r.e[i]);
    gl_free(&r);
    return d;
}
/* FUN_1801e6720 l.334689: the chain doubled up, offset to its left by DAT_1804ff998 + d with ccomp_pock(.., 'L', 0.785398, 1, 1, 0): the capsule around the chain */
static int pko_capsule(const Gl *chain, int dir, double tc, double d, double chtol, Gls *out, char *err, size_t errn) {
    Gl dbl = pko_double_up(chain);
    /* dir: the bound's own direction byte; MCCECalcContourOffset gives a doubled-up shape its direction from the sign of d (FUN_1800ce9a0 l.111497 / FUN_1801230c0, mcce_ctour.c sh_doubled) */
    int ok = pko_ccomp(&dbl, dir, tc + d, 'L', 0.785398, 1, 1, chtol, out, err, errn);
    gl_free(&dbl);
    return ok;
}
/* FUN_1801e6600 l.334661: both ends of bound g at least mind from bound o (proj_pt_to_pock_bounds); the check of FUN_1801e6970 / FUN_1801e7040 */
static int pko_far(const Gl *g, const Gls *o, double mind) {
    PV q[2] = { gt_start(&g->e[0]), gt_end(&g->e[g->n - 1]) };
    for (int s = 0; s < 2; s++) {
        double dmin = o->n ? 1e38 : 0.0;                 /* no bound: rec.p stays the point, distance 0 */
        for (int i = 0; i < o->n; i++) { double d = gl_closest(&o->l[i], q[s], NULL, NULL, NULL); if (d < dmin) dmin = d; }
        if (dmin < mind) return 0;
    }
    return 1;
}

typedef struct { int ent; double t; PV p; } PkoCut;
static int pko_cut_cmp(const void *a, const void *b) {                 /* the qsort comparator LAB_1801e5da0 (entity, then t on it) */
    const PkoCut *x = a, *y = b;
    if (x->ent != y->ent) return x->ent < y->ent ? -1 : 1;
    return x->t - y->t < 0 ? -1 : 1;
}
static double pko_len(const Gl *g) { return gl_len(g); }
/* FUN_1801e6050 l.334449 create_pock_bound_segment: entities s.ent .. e.ent, the first cut at s.t, the last at e.t */
static Gl pko_piece(const Gl *g, const PkoCut *s, const PkoCut *e) {
    Gl p = { 0 };
    for (int i = s->ent; i <= e->ent; i++) {
        double t0 = i == s->ent && s->t > 0 ? s->t : 0, t1 = i == e->ent && e->t < 1 ? e->t : 1;
        gl_push(&p, gt_sub(&g->e[i], t0, t1));
    }
    return p;
}
/* FUN_1801e7930 l.335249: the bound cut at the segment L (pock_bound_line_inters nc_tol x 0.01, 20 tries; a point within the segment or nc_tol of its ends counts), pieces whose midpoint is behind
   n (and whose foot lies on the segment) dropped, the rest extended to L (gouge check against gouge, the on-line check on) */
static int pko_clip(const Gls *lst, const PkoLine *L, PV n, const Gls *gouge, double chtol, Gls *out, char *err, size_t errn) {
    memset(out, 0, sizeof *out);
    const Gl *g = &lst->l[0];
    ZB *zbs = calloc((size_t)lst->n, sizeof(ZB)); ZBs B = { zbs, lst->n };
    for (int i = 0; i < lst->n; i++) pko_zb(&zbs[i], &lst->l[i], 0);
    static ZI I[ZZ_MAXI + 2]; int c = 0;
    zz_inters(&B, L->a, L->b, g_nc_tol * 0.01, 20, I, &c);
    PkoCut *cut = malloc((size_t)(c + 2) * sizeof(PkoCut)); int nc = 0;
    for (int k = 1; k <= c; k++) {
        double t = I[k].t; int keep;
        if (t < 0.0 || 1.0 < t) { PV e = t < 0 ? L->a : L->b; keep = hypot(e.x - I[k].p.x, e.y - I[k].p.y) < g_nc_tol; }
        else keep = 1;
        if (keep && I[k].bnd > 0) {   /* FUN_1801e6050 (l.335450) is called on the list's head with each record's entity index, whatever bound the record is on: an index of a later bound is not meaningful */
            free(cut); for (int i = 0; i < lst->n; i++) { free(zbs[i].poly.xy); gl_free(&zbs[i].l); } free(zbs);
            snprintf(err, errn, "open pocket: a pass offset split into several bounds crosses the trim line on a later bound (FUN_1801e7930 l.335450 uses the head's entities): NOT PORTED"); return 0;
        }
        if (keep) { cut[nc].ent = I[k].ent; cut[nc].t = zz_t_gt(I[k].p, &g->e[I[k].ent]); cut[nc].p = I[k].p; nc++; }
    }
    if (nc > 0) {
        qsort(cut, (size_t)nc, sizeof(PkoCut), pko_cut_cmp);
        PkoCut st = { 0, 0, { 0, 0 } };
        for (int i = 0; i <= nc; i++) {
            PkoCut en = i == nc ? (PkoCut){ g->n - 1, 1.0, { 0, 0 } } : cut[i];
            gls_push(out, pko_piece(g, &st, &en));
            st = en;
        }
        for (int i = 1; i < lst->n; i++) gls_push(out, gl_copy(&lst->l[i]));     /* l.335488-335500: the bounds no record is on are copied whole */
    } else for (int i = 0; i < lst->n; i++) gls_push(out, gl_copy(&lst->l[i]));  /* l.335534 copy_pock_bounds */
    free(cut);
    /* midpoint of the first entity (l.335486): in front of n: kept (count); else dropped when there were cuts or its foot is within the segment */
    Gls keep = { 0 }; int nfront = 0;
    for (int i = 0; i < out->n; i++) {
        PV m = gt_pt(&out->l[i].e[0], 0.5);
        if (0.0 <= (m.y - L->a.y) * n.y + (m.x - L->a.x) * n.x) { nfront++; gls_push(&keep, out->l[i]); continue; }
        double t; PV q;
        if (nc > 0 || (pko_foot(m, L, &q, &t) && 0.0 <= t && t <= 1.0)) { gl_free(&out->l[i]); continue; }
        gls_push(&keep, out->l[i]);
    }
    free(out->l); *out = keep;
    if (1 < nfront) {                                   /* zero-length pieces (pock_perim < nc_tol) */
        Gls k2 = { 0 };
        for (int i = 0; i < out->n; i++) { if (pko_len(&out->l[i]) < g_nc_tol) gl_free(&out->l[i]); else gls_push(&k2, out->l[i]); }
        free(out->l); *out = k2;
    }
    for (int i = 0; i < out->n; i++) {                  /* the ends not closed brought to the line: FUN_1801e7f30(.., gouge, 1, 1, L) */
        PV a = gt_start(&out->l[i].e[0]), b = gt_end(&out->l[i].e[out->l[i].n - 1]);
        if (!(chtol > hypot(b.x - a.x, b.y - a.y))) pko_extend(&out->l[i], gouge, 1, 1, L);   /* IsBndClosed(chain_tol): closed when the ends are strictly within tol (mccore 0x1805ddcf0, disassembly) */
    }
    for (int i = 0; i < lst->n; i++) { free(zbs[i].poly.xy); gl_free(&zbs[i].l); }
    free(zbs);
    return 1;
}

/* proj_pt_to_pock_bounds (mccore 0x1805e6a10, disassembly): the nearest point on the bounds (each entity's start, the last end, the foot when t in [-1e-12, 1 + 1e-12]) strictly within maxd;
   none: r->p = p, bnd -1 */
static void pko_proj(const ZBs *B, PV p, double maxd, ZI *r) {
    r->p = p; r->bnd = -1; r->ent = 0; r->t = 0; r->cls = 0;
    double best = 1e38;
    for (int bi = 0; bi < B->n; bi++) {
        const Gl *l = &B->b[bi].l;
        for (int i = 0; i <= l->n; i++) {
            PV q; double t = 0;
            for (int s = 0; s < 2; s++) {
                if (s == 0) { if (i == l->n) { q = gt_end(&l->e[i - 1]); t = 1; } else { q = gt_start(&l->e[i]); t = 0; } }
                else {
                    if (i == l->n) break;
                    t = zz_t_gt(p, &l->e[i]);
                    if (!(-1e-12 <= t && t <= 1.000000000001)) break;
                    q = gt_pt(&l->e[i], t);
                }
                double d = hypot(q.x - p.x, q.y - p.y);
                if (d < maxd && d < best) { best = d; r->p = q; r->ent = i == l->n ? i - 1 : i; r->bnd = bi; r->t = t; }
            }
        }
    }
}
static void pko_flush(MB *b, const CamParams *p, ZP *P, double depth, double ctol, PV *cur) {
    for (int k = 0; k < P->n; k++) {
        const ZM *m = &P->m[k]; PV q = m->p;
        if (m->k == 1) {
            PV c = m->c; double r = hypot(cur->x - c.x, cur->y - c.y), a0 = atan2(cur->y - c.y, cur->x - c.x);
            double sw = zz_arc_sweep(*cur, q, c, m->cw); if (m->cw) sw = -sw;
            Gt g = gt_arc(c, r, a0, sw); pk_arc_moves(b, &g, depth, depth, p->feed, ctol);
            b->m[b->n - 1].p.x = q.x; b->m[b->n - 1].p.y = q.y;
        } else mv(b, 0, q.x, q.y, depth, p->feed);
        *cur = q;
    }
    P->n = 0;
}
/* FUN_1801dfb90 l.330667: two waypoints as a pair on one bound. On the same bound: themselves. Otherwise each projected on every bound (proj_pt_to_pock_bounds, one bound at a time, strictly within t); the pair on a same bound with the smallest sum of the two
   displacements, strictly under t x 100 (the first found on ties); 0 = none */
static int pko_dfb90(double t, const ZBs *B, const ZI *a, const ZI *z, ZI out[2]) {
    if (a->bnd == z->bnd) { out[0] = *a; out[1] = *z; return 1; }
    double best = t * 100.0; int found = 0;
    for (int bi = 0; bi < B->n; bi++) {
        ZBs one = { &B->b[bi], 1 }; ZI p, q;
        pko_proj(&one, a->p, t, &p); pko_proj(&one, z->p, t, &q);
        if (p.bnd < 0 || q.bnd < 0) continue;
        double s = hypot(p.p.x - a->p.x, p.p.y - a->p.y) + hypot(q.p.x - z->p.x, q.p.y - z->p.y);
        if (s < best) { best = s; p.bnd = q.bnd = bi; out[0] = p; out[1] = q; found = 1; }
    }
    return found;
}
/* FUN_1801e23c0 l.332355 (via FUN_1801e0f80: param_2 0, ignore_wire_islands) pock_move_avoid_islands: the move A -> Z at the depth, straight between waypoints, along the bound where a piece of it
   runs on a bound (find_path_around_island zz_fpai, FUN_1801e12b0 zz_walk). Waypoints: A (on the projection within nc_tol when on a bound; when A is off the bounds and not in the machinable area, its projection on the bounds too),
   every crossing of A-Z with t in (0, 1), the end points (same treatment). A piece with no bound on its end is straight; else straight when its midpoint is in the area and not on a bound; else
   along the bound when both ends are on the same bound or FUN_1801dfb90 finds such a pair; when none, nothing of the link is written (l.332695-332700) */
static int pko_link(MB *b, const CamParams *p, const ZBs *B, PV A, PV Z, double depth, double ctol, char *err, size_t errn) {
    if (zz_equal(A, Z)) return 1;
    static ZI I[ZZ_MAXI + 2]; static ZI W[1002]; int c = 0, n = 0;
    double tol = CAM_SYS_TOL, nct = g_nc_tol;
    zz_inters(B, A, Z, tol, 20, I, &c);
    double d1 = 1e38, d2 = 1e38; int i1 = 0, i2 = 0;
    for (int k = 1; k <= c; k++) {
        double da = hypot(A.x - I[k].p.x, A.y - I[k].p.y), dz = hypot(Z.x - I[k].p.x, Z.y - I[k].p.y);
        if (da < d1) { d1 = da; i1 = k; }
        if (dz < d2) { d2 = dz; i2 = k; }
    }
    for (int k = 1; k <= c; k++) I[k].cls = (0.0 < I[k].t && I[k].t < 1.0) || (k == i1 && d1 < nct * 2.0) || (k == i2 && d2 < nct * 2.0);
    if (d1 > nct) {
        n = 1; pko_proj(B, A, nct, &W[0]);
        if (W[0].bnd < 0 && !zz_in_area(B, A, NULL, nct, tol)) { n = 2; pko_proj(B, A, 1e38, &W[1]); }
    }
    for (int k = 1; k <= c; k++) if (I[k].cls) { if (n >= 1000) return -1; W[n++] = I[k]; }   /* over 999 waypoints: return -1 with no move (l.332561); FUN_1801e9a90 l.336692 ignores the value */
    if (d2 > nct) {
        if (n >= 1000) return -1;                                       /* l.332585 */
        int m = n + 1;
        pko_proj(B, Z, nct, &W[n]);
        if (W[n].bnd < 0 && !zz_in_area(B, Z, NULL, nct, tol)) {
            if (n + 1 >= 1000) return -1;                               /* l.332615 */
            W[n + 1] = W[n]; pko_proj(B, Z, 1e38, &W[n]); m = n + 2;
        }
        n = m;
    }
    int st[1002] = { 0 };
    for (int i = 1; i < n; i++) {
        if (W[i].bnd < 0) { st[i] = 1; continue; }
        int on = 0; PV m = pv((W[i].p.x + W[i - 1].p.x) * 0.5, (W[i].p.y + W[i - 1].p.y) * 0.5);
        int in = zz_in_area(B, m, &on, tol, tol);
        if (in && !on) { st[i] = 1; continue; }
        if (W[i].bnd == W[i - 1].bnd) { st[i] = 2; continue; }
        double dd = hypot(W[i].p.x - W[i - 1].p.x, W[i].p.y - W[i - 1].p.y);
        if (in && on && dd * dd < tol * 2.0 * tol * 2.0) { st[i] = 1; continue; }
        ZI ft[2];
        if (!pko_dfb90(tol, B, &W[i - 1], &W[i], ft)) {   /* l.332695-332700: state 0: return 0, no move of the link is written */
            if (getenv("PK_DEBUG")) { fprintf(stderr, "pko_link: no pair for piece %d of %d: %.5f %.5f (bnd %d) -> %.5f %.5f (bnd %d), in %d on %d\n", i, n, W[i - 1].p.x, W[i - 1].p.y, W[i - 1].bnd, W[i].p.x, W[i].p.y, W[i].bnd, in, on);
                for (int k = 0; k < n; k++) fprintf(stderr, "  W[%d] %.5f %.5f bnd %d ent %d t %.5f\n", k, W[k].p.x, W[k].p.y, W[k].bnd, W[k].ent, W[k].t); }
            return 1;
        }
        st[i] = 2;
    }
    if (!n) return 1;
    PV cur = A;
    for (int i = 1; i < n; i++) {
        if (st[i] == 0) return 1;               /* (l.332700) no move written */
        if (st[i] == 1) { mv(b, 0, W[i].p.x, W[i].p.y, depth, p->feed); cur = W[i].p; continue; }
        ZP P = { 0 }; P.cur = cur;
        double len; int dir; ZI ft[2];
        if (!pko_dfb90(tol + 1e-12, B, &W[i - 1], &W[i], ft)) { snprintf(err, errn, "open pocket: FUN_1801dfb90 failed on a link piece it accepted (MCAssert POCKNCI.cpp 0x415)"); return 0; }   /* l.332752-332757 */
        if (getenv("PK_DEBUG")) fprintf(stderr, "open link: along bound %d from %.4f %.4f (ent %d) to %.4f %.4f (ent %d)\n", ft[0].bnd, ft[0].p.x, ft[0].p.y, ft[0].ent, ft[1].p.x, ft[1].p.y, ft[1].ent);
        zz_fpai(B, ft[1].bnd, &ft[0], &ft[1], 0, &len, &dir);
        int ok = zz_walk(&P, B, &ft[0], &ft[1], dir);
        if (!ok) { snprintf(err, errn, "%.150s", P.err); free(P.m); return 0; }
        pko_flush(b, p, &P, depth, ctol, &cur);
        free(P.m);
    }
    return 1;
}

/* FUN_1801e67b0 l.334708 make_open_pock_safe_bound_rect: the bound's box grown by 10 times its size on every side, as a counter-clockwise rectangle */
static Gl pko_rect(const Gl *g) {
    double bb[4]; gl_bbox(g, bb);
    double w = bb[2] - bb[0], h = bb[3] - bb[1], x0 = bb[0] - w * 10.0, x1 = w * 10.0 + bb[2], y0 = bb[1] - h * 10.0, y1 = h * 10.0 + bb[3];
    Gl r = { 0 };
    gl_push(&r, gt_line(pv(x0, y0), pv(x1, y0))); gl_push(&r, gt_line(pv(x1, y0), pv(x1, y1)));
    gl_push(&r, gt_line(pv(x1, y1), pv(x0, y1))); gl_push(&r, gt_line(pv(x0, y1), pv(x0, y0)));
    return r;
}
static void pko_gls_free_all(Gls *a, int n) { for (int i = 0; i < n; i++) gls_free(&a[i]); free(a); }

/* FUN_180056a70 l.59008-59017 (op +0x1220.. = record 0x1288 / 0x1290 / 0x1298): tapered walls on when depth cuts are on (op +0x800), the flag is set and an angle is not 0 */
static int pko_taper_on(const CamParams *p) { return (p->mc_depth_on || p->stepdown > 1e-6) && pkb(p, 0x1288) && (1e-12 < fabs(pkd(p, 0x1290)) || 1e-12 < fabs(pkd(p, 0x1298))); }
/* FUN_1801e5de0 l.334347: the offset of a tapered wall at the depth, chain Z bz, wall angle ang; first (param_3): less R (the first of the two ccomp_pock runs) */
static double pko_taper_d(const CamParams *p, double bz, double depth, double ang, int first) {
    double D = p->tool_dia, cr = p->corner_r, h = bz - depth;
    if (pkb(p, 0x1231)) h -= cr;                          /* pock_prm +9 = tip comp */
    double c = cos(ang);
    if (fabs(c) <= 1e-12 && fabs(c) != 1e-12) c = 1.0;
    double r = tan(ang) * h + (p->stock_xy + cr) / c + D * 0.5 - cr;
    return first ? r - D * 0.5 : r;
}
/* one tool-centre plan of FUN_1801e7040 l.334991: the bounds and the passes */
typedef struct { Gls tcb, cap1, cap2; Gls *pass; int npass; ZB *zb; int nb; ZBs B; } PkoPlan;
static void pko_plan_free(PkoPlan *P) {
    pko_gls_free_all(P->pass, P->npass);
    for (int i = 0; i < P->nb; i++) { free(P->zb[i].poly.xy); gl_free(&P->zb[i].l); }
    free(P->zb);
    gls_free(&P->tcb); gls_free(&P->cap1); gls_free(&P->cap2);
    memset(P, 0, sizeof *P);
}
/* FUN_1801e6970 l.334785 (tc = R + wall stock, or R after the taper's first run) then FUN_1801e7040: start = the first pass offset, d1 / d2 the capsule offsets (l.335064-335071 / l.335117-335122). 1 with npass = 0 when there is nothing to cut (no error: FUN_1801e6970 l.334833-334835, machine_pocket l.322541), 0 refused */
static int pko_plan(const CamParams *p, const Gl *chain, int dir, double tc, double start, double d1, double d2, double step, double minfil, double chtol, PkoPlan *P, char *err, size_t en) {
    double D = p->tool_dia, nct = g_nc_tol;
    char side = pkb(p, 0x123a) ? 'L' : 'R';              /* cVar18 l.335048 */
    memset(P, 0, sizeof *P);
    if (!pko_ccomp(chain, dir, tc, side, minfil, 1, 0, chtol, &P->tcb, err, en)) return 0;       /* FUN_1801e6970: ccomp_pock(.., 1, 0, 0) */
    if (P->tcb.n == 0 || !pko_far(chain, &P->tcb, tc - 2.0 * nct)) return 1;                /* the result is not appended (l.334833): an empty pocket list, machine_pocket writes nothing */
    if (P->tcb.n != 1) { snprintf(err, en, "open pocket: the tool-centre offset is several bounds (FUN_1801e7040 l.335074 on a list): NOT PORTED"); return 0; }
    const Gl *base = &P->tcb.l[0];
    if (!pko_capsule(chain, dir, tc, d1, chtol, &P->cap1, err, en)) return 0;
    PkoLine seg; PV n;
    if (!pko_trim_seg(chain, pkd(p, 0x13e0) - 50.0, side, D, &seg, &n)) { snprintf(err, en, "open pocket: the chain has no chord (its start = its end)"); return 0; }
    double bb[4]; gl_bbox(chain, bb);
    double dlim = hypot(bb[2] - bb[0], bb[3] - bb[1]) + tc, d = start;
    int cpass = 0;
    for (;;) {
        /* ours (a guard, not Mastercam's): FUN_1801e7040's loop l.335076 has no limit: on a chain crossing its own chord the trim segment need not bound the offsets. An offset beyond the chain's diagonal cuts nothing of it: refused */
        if (d > dlim) { snprintf(err, en, "open pocket: the passes do not end at the trim line (a chain crossing its chord; FUN_1801e7040 l.335076 has no limit): NOT PORTED"); return 0; }
        Gls off = { 0 };
        if (!pko_ccomp(base, P->tcb.l[0].island, d, side, minfil, 1, 0, chtol, &off, err, en)) { gls_free(&off); return 0; }
        if (off.n == 0) { gls_free(&off); break; }
        if (!pko_far(base, &off, d - 2.0 * nct)) { gls_free(&off); break; }   /* FUN_1801e6600 l.334661: proj_pt_to_pock_bounds over the whole list (the loop at mccore 0x1805e6c47) */
        Gls out = { 0 };
        int nint = d > nct && pko_bounds_x(&off.l[0], base);    /* intersect_pock_bound(.., 0, 2, ..) l.335095 (the head): only the count > 0 is read */
        if (nint) for (int i = 0; i < off.n; i++) gls_push(&out, gl_copy(&off.l[i]));   /* copy_pock_bounds l.335097 */
        else {
            if (!pko_clip(&off, &seg, n, &P->cap1, chtol, &out, err, en)) { gls_free(&off); return 0; }
            if (out.n == 0) { gls_free(&out); gls_free(&off); break; }
        }
        gls_free(&off);
        if (P->npass == cpass) { cpass = cpass ? cpass * 2 : 16; P->pass = realloc(P->pass, (size_t)cpass * sizeof(Gls)); }
        P->pass[P->npass++] = out;
        d += step;
    }
    if (!P->npass) return 1;                               /* local_188 stays -1, l.335131 is not run: no error */
    if (!pko_capsule(chain, dir, tc, d2, chtol, &P->cap2, err, en)) return 0;     /* the bounds of the links (l.335115-335127) */
    P->nb = P->cap2.n + 1;
    P->zb = calloc((size_t)P->nb, sizeof(ZB));
    Gl rect = pko_rect(base);
    pko_zb(&P->zb[0], &rect, 0); gl_free(&rect);
    for (int i = 0; i < P->cap2.n; i++) pko_zb(&P->zb[i + 1], &P->cap2.l[i], 1);  /* local_160[2] = 1 (the head; a result of several rings is refused by the offset port) */
    P->B.b = P->zb; P->B.n = P->nb;
    return 1;
}
/* the start of a section, wr_pock_toolpath l.316154-316210 (non-Remachining branch; the Remachining branch l.315990-316049 is the same text): the tool down and more than nc_tol from the last point: retract_move there, or clear_move when the pocket changes unless clearance only at start / end
   (+0x111 and +0x2b1); then plunge_move at the section start s (first = the op's first section or the pocket changes unless clearance only at start / end). As pk_rem_cut. The section's first record is the entry FUN_1801c3b20 -> FUN_1801bf680 l.309886 (FUN_1801e9a90 l.336692: prev point set, tool up) */
static void pko_section(MB *b, const CamParams *p, PV s, double z, double ref, double nct, int k, int *any, int *lastk) {
    int first = !*any, newp = !first && *lastk != k, only_se = p->mc_clr_on && p->mc_clr_end, down = 0;
    if (!first) {
        V3 e = lastp(b);
        if (hypot(e.x - s.x, e.y - s.y) > nct) pk_up(b, p, pk_h(p, e.z, e.z), newp && !only_se);   /* set_heights(previous depth, previous depth), l.316156-316197 */
        else down = 1;
    }
    if (down) {
        V3 e = lastp(b);
        if (hypot(e.x - s.x, e.y - s.y) > CAM_SYS_TOL) mv(b, 0, s.x, s.y, e.z, p->feed);
        if (fabs(e.z - z) > 1e-12) mv(b, 0, s.x, s.y, z, p->plunge_feed);
    } else {
        int c14 = only_se ? first : (first || newp);
        PkH H = pk_h(p, z, ref);                                                  /* set_heights(depth, the marker's top) l.315582: top of stock, a later depth cut the previous cut's Z (wr_pock_depth_cuts l.315181) */
        if (p->mc_clr_on) { if (H.f + CAM_SYS_TOL < H.c && z + CAM_SYS_TOL < H.c && c14) pk_rapid(b, s.x, s.y, H.c); }
        else if (p->mc_ret_on && H.f + CAM_SYS_TOL < H.r && z + CAM_SYS_TOL < H.r && c14) pk_rapid(b, s.x, s.y, H.r);
        pk_rapid(b, s.x, s.y, z + CAM_SYS_TOL < H.f ? H.f : z);
        mv(b, 0, s.x, s.y, z, p->plunge_feed);
    }
    *any = 1; *lastk = k;
}
/* FUN_1801e7040 l.334991 through machine_pocket (FUN_1801d36c0 l.322563-322585, param_3 = 0), per depth, one pocket k. The chain is in its final direction (FUN_1801e78a0). 0 refused */
static int pko_open_cut(const McamFile *f, CamOp *op, MB *b, const Gl *chain, int dir, double chain_z, double minfil, double chtol, double ctol, int k, int *any, int *lastk) {
    CamParams *p = &op->p;
    char *err = op->error; size_t en = sizeof op->error;
    double D = p->tool_dia, R = D * 0.5, tc0 = R + p->stock_xy, step = D * p->stepover_pct / 100, nct = g_nc_tol;
    /* machine_pocket l.322563: the roughing branch needs 0x1238 and the bound; else straight to the finish test l.322565 */
    int rough = !p->pocket_no_rough, fin = pkb(p, 0x1239), nf = p->finish_n, taper = pko_taper_on(p);
    double fstep = pkd(p, 0x1260);
    if (fin && nf > 0) { snprintf(err, en, "open pocket cutting method with finish passes (FUN_1801cc170 l.318088-318130 on an open bound, FUN_1801cd200 / FUN_1801bdc00 with param_5 = 0): NOT PORTED"); return 0; }
    if (!rough) return 1;                                 /* finish off too: machine_pocket writes nothing, no error */
    if (p->mc_depth_on && p->mc_keep_down && !pkb(p, 0x12a0)) { snprintf(err, en, "open pocket cutting method: keep tool down between depth cuts with the entry off (FUN_1801e7040 l.335153-335194, wr_pock_toolpath's key l.316052): NOT PORTED"); return 0; }
    if (!(step > 1e-9)) { snprintf(err, en, "pocket: stepover must be > 0 (ours: FUN_1801e7040's loop l.335076 would not advance)"); return 0; }
    /* l.335064-335071, l.335117-335122 */
    double start = fin ? nf * fstep : 0.0, d1 = fin ? start - fstep : D * -0.01, d2 = fin ? fstep : D * 0.01;
    double zl[200]; int nrough = 1; double rstep = 0;
    int nz = hst_depths(p, zl, 200, &nrough, &rstep);
    PkoPlan P; memset(&P, 0, sizeof P);
    if (!taper && !pko_plan(p, chain, dir, tc0, start, d1, d2, step, minfil, chtol, &P, err, en)) { pko_plan_free(&P); return 0; }
    PkEntry ent; memset(&ent, 0, sizeof ent);
    Gls no = { 0 };
    int rc = 1;
    for (int z = 0; z < nz && rc; z++) {
        double depth = zl[z]; int first = 1;
        if (taper) {   /* process_open_pocket l.334935-334939: FUN_1801e6970 twice at this depth (chain offset by the wall's offset less R, then by R), FUN_1801e7040 on the result (local_60) */
            pko_plan_free(&P);
            Gls cd = { 0 };
            double dist = pko_taper_d(p, chain_z, depth, pkd(p, 0x1290), 1);    /* param_4 = pock_prm +0x68 for an outer bound */
            char side = pkb(p, 0x123a) ? 'L' : 'R';
            if (!pko_ccomp(chain, dir, dist, side, minfil, 1, 0, chtol, &cd, err, en)) { gls_free(&cd); rc = 0; break; }
            if (cd.n == 0 || !pko_far(chain, &cd, dist - 2.0 * nct)) { gls_free(&cd); continue; }   /* l.334833: not appended: nothing at this depth */
            /* FUN_1801e6970 l.334829-334834 appends the whole list; machine_pocket then runs FUN_1801e7040 on it (ccomp_pock on several shapes, not in the offset port) */
            if (cd.n != 1) { gls_free(&cd); snprintf(err, en, "open pocket: a tapered wall offset is several bounds (process_open_pocket l.334935-334939): NOT PORTED"); rc = 0; break; }
            if (!pko_plan(p, &cd.l[0], cd.l[0].island, R, start, d1, d2, step, minfil, chtol, &P, err, en)) { gls_free(&cd); pko_plan_free(&P); rc = 0; break; }
            gls_free(&cd);
        }
        for (int pk = P.npass - 1; pk >= 0 && rc; pk--) {
            for (int i = 0; i < P.pass[pk].n; i++) {
                const Gl *pc = &P.pass[pk].l[i];
                PV s0 = gt_start(&pc->e[0]);
                if (first) {
                    if (!pk_entry_plan(f, op, &no, s0, zl[0], &ent)) { rc = 0; break; }     /* the refusals of a chained entry point; with 0x13f0 the entry is the plain plunge (l.307927) */
                    pk_entry_free(&ent); memset(&ent, 0, sizeof ent);
                    pko_section(b, p, s0, depth, z > 0 ? zl[z - 1] : p->top_z, nct, k, any, lastk);
                    first = 0;
                } else {
                    V3 e = lastp(b);
                    if (!pko_link(b, p, &P.B, pv(e.x, e.y), s0, depth, ctol, err, en)) { rc = 0; break; }
                }
                pk_loop(b, pc, depth, p->feed, ctol);
            }
        }
    }
    pko_plan_free(&P);
    return rc;
}

/* a chain's bound (FUN_1801e5840 l.334033: open when the ends are more than chain_tol apart). One chain's own direction; 0 = not planar lines / arcs */
static int pko_gl_open(const Gl *g, double chtol) {
    PV a = gt_start(&g->e[0]), z = gt_end(&g->e[g->n - 1]);
    return chtol * chtol < (z.x - a.x) * (z.x - a.x) + (z.y - a.y) * (z.y - a.y);
}
/* an open bound among the op's chains (FUN_1801e5840 per chain) */
static int pko_has_open(const McamFile *f, const CamOp *op, double chtol) {
    for (int i = 0; i < op->nchains; i++) {
        if (op->chains[i].avoid) continue;
        Gl g; if (!pko_chain_gl(f, &op->chains[i], &g)) continue;
        int o = pko_gl_open(&g, chtol); gl_free(&g);
        if (o) return 1;
    }
    return 0;
}
/* disjoint_pock_bound_bbox (decomp/mccore_pock.c, 0x1805e3070): the boxes (pock_bound_bbox) apart by more than nc_tol. sort_pock_bounds l.247-251 tests nesting only when not disjoint */
static int pko_disjoint(const Gl *a, const Gl *b) {
    double p[4], q[4], t = g_nc_tol; gl_bbox(a, p); gl_bbox(b, q);
    return !(q[0] <= t + p[2] && q[1] <= t + p[3] && p[0] - t <= q[2] && p[1] - t <= q[3]);
}
/* process_open_pocket (l.334850) and its choice in GeneratePocketToolpath, for type 4 (per pocket of sort_pock_bounds). Returns 0 = all closed: the ordinary pocket; 1 = reg / mid / fe set from the chains (FUN_1801e5c70 on the open ones), the ordinary pocket continues;
   2 = done (0x13f0: pko_open_cut wrote the cut, possibly nothing); -1 refused (op->error) */
static int pko_open(const McamFile *f, CamOp *op, MB *b, double minfil, double chtol, double ctol, Gls *reg, PV *mid, PkFirst *fe) {
    CamParams *p = &op->p;
    int nl = 0, nopen = 0;
    Gl *g = calloc((size_t)(op->nchains ? op->nchains : 1), sizeof(Gl)); int *cid = calloc((size_t)(op->nchains ? op->nchains : 1), sizeof(int)), *isop = calloc((size_t)(op->nchains ? op->nchains : 1), sizeof(int));
    int rc = -1;
    for (int i = 0; i < op->nchains; i++) {
        const Chain *ch = &op->chains[i];
        if (ch->avoid) continue;
        if (!pko_chain_gl(f, ch, &g[nl])) { snprintf(op->error, sizeof op->error, "pocket needs planar line / arc chains"); goto done; }
        cid[nl] = i; isop[nl] = pko_gl_open(&g[nl], chtol); nopen += isop[nl]; nl++;
    }
    { double *zc = malloc(sizeof(double) * (size_t)(op->nchains + 1)); int nc = 0, fl = pk_isl_zs(f, op, zc, op->nchains, &nc, 1); free(zc);
      if (!fl) { snprintf(op->error, sizeof op->error, "Warning - the boundary does not lie in the construction plane"); goto done; } }   /* check_chains_3d l.313530-313541 (post_pock_warning 0x2309) */
    if (!nopen) {
        /* FUN_1801e57f0 l.334017: closed chains with 0x13d8 ("Use Standard pocket for closed chains" off) and 0x13f0 off: MCCECalcClosedOpen (mcce PockClosedOpen.cpp) */
        if (pkb(p, 0x13d8) && !pkb(p, 0x13f0)) { snprintf(op->error, sizeof op->error, "open pocket on closed chains with \"Use Standard pocket for closed chains\" off (MCCECalcClosedOpen, mcce PockClosedOpen.cpp): NOT PORTED"); goto done; }
        rc = 0; goto done;
    }
    /* sort_pock_bounds l.247-251: chains whose boxes do not overlap are separate pockets, numbered in the chains' order (l.303-315); overlapping boxes need IsContourNested on an open bound */
    for (int i = 0; i < nl; i++) for (int j = i + 1; j < nl; j++) if (!pko_disjoint(&g[i], &g[j])) {
        snprintf(op->error, sizeof op->error, "open pocket: chains with overlapping boxes (IsContourNested on an open bound, sort_pock_bounds; 0x13f0 with an island: warning 2405, no cut): NOT PORTED"); goto done; }
    if (nl > 1 && pkb(p, 0x13f8)) { snprintf(op->error, sizeof op->error, "open pocket with several chains and By depth (0x13f8; wr_pock_toolpath's section order): NOT PORTED"); goto done; }
    { const McSysTol *st = mc_systol_units(mcam_is_metric(f)); rm_tol = st->tol; rm_nc_tol = st->nc_tol; rm_chain_tol = st->chain_tol; }   /* pk_up (mccore tol of the part units) */
    CamParams lastp_ = *p;
    if (pkb(p, 0x13f0)) {
        for (int i = 0; i < nl; i++) if (!isop[i]) { snprintf(op->error, sizeof op->error, "open pocket cutting method with a closed chain among open ones (process_pocket and FUN_1801e7040 sections in one op): NOT PORTED"); goto done; }
        int any = 0, lastk = -1;
        for (int i = 0; i < nl; i++) {
            /* FUN_1801e78a0 l.335219: the chain reversed when (ccw with its chord) differs from the wanted direction: ccw when 0x123a (climb) */
            int ccw = pko_chord_ccw(&g[i]), rev = ccw ? !pkb(p, 0x123a) : pkb(p, 0x123a);
            if (rev) { gl_reverse(&g[i]); ccw = !ccw; }
            double cz = f->ents[op->chains[cid[i]].ent[0]].a.z;   /* the bound's Z +0x18 (chain_to_pock_bound l.1500-1533 local_res8) */
            /* set_depths per chain pocket, l.313612, process_open_pocket inside that loop, l.313694: the incremental depth / top and the heights are on this chain's Z */
            CamParams sv = op->p; pk_cp_for_z(&op->p, &sv, cz);
            int okc = pko_open_cut(f, op, b, &g[i], ccw ? 1 : -1, cz, minfil, chtol, ctol, i, &any, &lastk);
            if (i == nl - 1) lastp_ = op->p;
            op->p = sv;
            if (!okc) goto done;
        }
        if (any) { V3 e = lastp(b); PkH H = pk_h(&lastp_, e.z, lastp_.top_z);   /* start_top_of_stock: the last set_depths call (l.316545, mccore_depths.c l.461) */  if ((p->mc_clr_on || p->mc_ret_on) && (p->ht_inc & 4)) { H.f = e.z + p->feed_plane; if (H.f < e.z) H.f = e.z; } pk_up(b, p, H, 1); }   /* the end of the op: set_heights(depth, top) l.316546, the feed plane l.316548-316553, clear_move l.316554 */
        rc = 2; goto done;
    }
    memset(reg, 0, sizeof *reg);
    for (int i = 0; i < nl; i++) {
        Gl c2;
        if (!isop[i]) {
            /* per pocket (GeneratePocketToolpath l.313699-313700, FUN_1801e57f0): a closed chain with 0x13d8 on takes MCCECalcClosedOpen (l.334976) */
            if (pkb(p, 0x13d8)) { snprintf(op->error, sizeof op->error, "open pocket: a closed chain with \"Use Standard pocket for closed chains\" off (MCCECalcClosedOpen): NOT PORTED"); goto done; }
            if (!op->chains[cid[i]].closed) { snprintf(op->error, sizeof op->error, "open pocket: a chain flagged open whose ends are within chain_tol: NOT PORTED"); goto done; }
            c2 = g[i]; memset(&g[i], 0, sizeof g[i]);
        } else if (!pko_close(&g[i], pkd(p, 0x13e0), p->tool_dia, &c2)) { snprintf(op->error, sizeof op->error, "open pocket: the chain has no chord (its start = its end)"); goto done; }
        if (i == 0) {
            *mid = gt_pt(&c2.e[0], 0.5);
            fe->a = gt_start(&c2.e[0]); fe->b = gt_end(&c2.e[0]); fe->ccw = gl_area(&c2) > 0; fe->ok = 1;
        }
        if (gl_area(&c2) < 0) gl_reverse(&c2);            /* pk_region's convention for an outer loop */
        c2.island = 0;
        gls_push(reg, c2);
    }
    rc = 1;
done:
    for (int i = 0; i < nl; i++) gl_free(&g[i]);
    free(g); free(cid); free(isop);
    return rc;
}


