/* cam2d_pock_fin.c - the Pocket finish passes (page 3004) of gen_pocket_v2, ported from mcmill Pockfin.cpp / Pockentr.cpp
 * (batch 6, 2026-10-23). Included by cam2d_pock.c (one translation unit with cam2d.c). Every cited line is
 * decomp/mcmill_all.c unless named otherwise; mccore pieces: decomp/mccore_pock.c, decomp/mccore_pock2.c and
 * ~/mastercam-re/work/tree_b6_finish/decomp_finish{,2}.c (Ghidra DumpDecomp of mccore reverse_pock_bound @1805dda60 and
 * bnci_wr_pock_marker @1800958f0).
 *
 * Structure (GeneratePocketToolpath FUN_1801c5250 l.313461 -> process_pocket l.323151 -> machine_pocket FUN_1801d36c0
 * l.322459 -> finish FUN_1801cc170 l.317877 per pocket and depth unit; per pass and bound the leads FUN_1801bdc00
 * l.308436 and the writer FUN_1801cd200 l.318365; the sections are ordered by wr_pock_toolpath l.315266 with the qsort
 * comparator FUN_1801c6520 l.~314580):
 *   pk_fin_init     the page's fields, refusals (NOT DECODED / ours)
 *   pk_fin_bounds   the bounds of finish pass j (FUN_1801cc170 l.318050-318120)
 *   pk_fin_pick     bound order / start entity (0x1269, FUN_1801cbd20 l.317673 + reorder_pock_bound) and direction
 *                   (0x143a / 0x123a, reverse_pock_bound)
 *   pk_fin_leads    the lead entities (FUN_1801bdc00, FUN_1801bf950, FUN_1801c2b30) and their checks (FUN_1801c4860 ->
 *                   FUN_1801c3d70 l.~311760, FUN_1801c1b60, FUN_1801bbc70)
 *   pk_sec_cmp      the section order (0x126b, 0x13f8, thin wall 0x1436 / 0x1438)
 */

typedef struct {
    int nf; double fs;               /* +0x32 passes, +0x38 spacing */
    int comp, ctl, ctl_only, opt;    /* 0x13a8 (+0x180): cc_in_control (1..3), cc_in_control_only (1); optimize op +0x56c */
    int climb;                       /* FUN_1801cc170 local_19e: 0x1436 ? 0x143a : 0x123a; != 0 -> side 'L' */
    int closest, after, bydepth;     /* 0x1269 (+0x41), 0x126b (+0x43) honoured value, 0x13f8 (+0x1d0) */
    int thin, nzt;                   /* 0x1436 (+0x20e), 0x1438 (+0x210) */
    int smooth;                      /* FUN_1801d2b80 on the finish bounds (l.318060) */
    double slen;
    int ein_line, ein_arc, eout_arc, eout_line;      /* FUN_1801bdc00 local_303 / 306 / 305 / 304 */
    double ll, lr, lsw, xl, xr, xsw, lr_raw, xr_raw;
    int lperp, xperp, lead_master, chk_in, chk_out;
    double ovl;                      /* FUN_1801bdc00 cVar27: closed, exit on, 0x988 > nc_tol (op +0x920) */
    int ktd;                         /* 0x126a (+0x42) keep tool down: FUN_1801cd200 cVar19 (param_9 = 0) */
    int fwr, finall;                 /* finish_with_rough (GeneratePocketToolpath l.313559-313567; 1 from msurf_map l.339989 for Surface rough), pock_finish_all (l.313514-313521) */
} PkFin;


/* the page's fields. rough / method: the roughing on and its cutting method; returns 0 with op->error set (refusal) */
static int pk_fin_lead_init(const CamParams *p, PkFin *F, char *err, size_t errn);
static int pk_fin_init(const CamParams *p, int rough, int method, PkFin *F, char *err, size_t errn) {
    memset(F, 0, sizeof *F);
    double nct = g_nc_tol, tol = CAM_SYS_TOL;
    F->nf = p->finish_n > 0 ? p->finish_n : 0; F->fs = p->finish_step;
    if (!F->nf) return 1;
    F->comp = p->pk_ok ? pks(p, 0x13a8) : 0;
    if (F->comp < 0 || F->comp > 3) { snprintf(err, errn, "pocket finish compensation type %d (0x13a8): NOT DECODED", F->comp); return 0; }
    F->ctl = F->comp >= 1;                         /* mccore cc_in_control: Control / Wear / Reverse wear (decomp/mccore_cc.c) */
    F->ctl_only = F->comp == 1;                    /* cc_in_control_only */
    F->opt = F->ctl_only && p->ct_ccopt && p->tool_dia > tol;   /* l.318070: op +0x56c, D > tol, control only */
    F->thin = p->pk_ok ? pkb(p, 0x1436) : 0;
    F->nzt = p->pk_ok ? pks(p, 0x1438) : 2;
    F->climb = p->pk_ok ? (F->thin ? pkb(p, 0x143a) : pkb(p, 0x123a)) : 1;   /* l.317946-317951 */
    F->closest = p->pk_ok ? pkb(p, 0x1269) : 0;
    /* 0x126b "Machine finish passes after roughing all pockets" (inverted box: byte 0 = checked): finish_with_rough
       (l.313559) -> the section order. The Mill_MM defaults op holds 1, Mill_Inch 0. Batch 6 honoured it for stored ops and after
       a dialog edit only; decisions batch dec1 (formats/user_decisions.md B6-4: follow Mastercam's default) honours it always,
       so new metric pockets from Mill_MM take Mastercam's order */
    F->after = p->pk_ok ? pkb(p, 0x126b) : 0;
    F->bydepth = p->pk_ok ? pkb(p, 0x13f8) : 0;
    F->smooth = 0; F->slen = p->pk_ok ? pkd(p, 0x1400) : 0;
    /* l.318060: roughing on, cutting method 8, (pocket type != 4 or +0x1c8 = 0), 0x1400 > nc_tol and != 0 */
    /* batch 6 gated this (stored ops / after an edit of 0x1400); decisions batch dec1 (formats/user_decisions.md B6-3: on, as
       Mastercam does): new ops from the defaults (Mill_MM 2 mm, Mill_Inch 0.1) get the rounded finish corners too */
    if (rough && method == 8 && (!p->pk_ok || pks(p, 0x13fa) != 4 || pkb(p, 0x13f0) == 0) && F->slen > nct && F->slen != 0) F->smooth = 1;
    /* 0x126a Keep tool down (pock_prm +0x42): the link into each finish bound (FUN_1801cd200 l.318461-318515; cam2d_pock.c pk_ktd_*) */
    F->ktd = p->pk_ok ? pkb(p, 0x126a) : 0;
    if (p->pk_ok && pks(p, 0x1420) > 0 && !pko_taper_on(p)) {   /* tapered walls: no spring passes, warning 0x96b (FUN_1801c5250 l.313764) */
        snprintf(err, errn, "pocket finish spring passes (0x1420): the link between the spring sections NOT DECODED"); return 0; }
    if (F->thin && F->nzt < 1) { snprintf(err, errn, "pocket thin wall: Z finish passes (0x1438) must be >= 1"); return 0; }
    return pk_fin_lead_init(p, F, err, errn);
}

/* the lead fields of FUN_1801bdc00 (l.308620-308720; op +X = record +0x68+X), split from pk_fin_init for Pocket Remachining's rough passes (cam2d_pock_rem.c 0x13d0) */
static int pk_fin_lead_init(const CamParams *p, PkFin *F, char *err, size_t errn) {
    double nct = g_nc_tol, tol = CAM_SYS_TOL;
    F->lead_master = p->lead_on;
    int ion = p->lead_on && p->lead_in_on, oon = p->lead_on && p->lead_out_on;
    double lsw = (p->lead_sw_rec & 1) ? p->lead_sw : (p->lead_sw > 1e-6 ? p->lead_sw : M_PI / 2);   /* no record: a quarter (ours, as before) */
    double xsw = (p->lead_sw_rec & 2) ? p->lead_xsw : (p->lead_xsw > 1e-6 ? p->lead_xsw : M_PI / 2);
    F->ll = p->lead_len; F->lr = p->lead_r; F->lsw = lsw; F->xl = p->lead_xlen; F->xr = p->lead_xr; F->xsw = xsw;
    F->lr_raw = p->lead_r; F->xr_raw = p->lead_xr;
    F->lperp = p->lead_in_perp; F->xperp = p->lead_out_perp;
    F->ein_arc = ion && lsw > 1e-6 && F->lr > nct;
    F->ein_line = ion && F->ll > nct;
    F->eout_arc = oon && xsw > 1e-6 && F->xr > nct;
    F->eout_line = oon && F->xl > nct;
    F->chk_in = (p->lead_xflags & 16) ? 1 : 0; F->chk_out = (p->lead_xflags & 32) ? 1 : 0;   /* op +0x92a / +0x92b (l.309325) */
    if ((ion && (p->lead_xflags & 2)) || (oon && (p->lead_xflags & 8))) {
        snprintf(err, errn, "pocket finish lead Use entry / exit point (0x929 / 0x971): NOT DECODED"); return 0; }
    F->ovl = oon && p->lead_ovl > nct ? p->lead_ovl : 0;
    if ((ion && (fabs(p->mc_lead_dz[0]) > tol || fabs(p->mc_lead_dz[1]) > tol)) || (oon && (fabs(p->mc_lead_dz[2]) > tol || fabs(p->mc_lead_dz[3]) > tol))) {
        snprintf(err, errn, "pocket finish lead ramp / helix heights (0x908 / 0x920 / 0x950 / 0x968): NOT DECODED here"); return 0; }
    int depth_on = p->mc_depth_on || p->stepdown > 1e-6;
    if (depth_on && ((ion && (p->lead_xflags & 1)) || (oon && (p->lead_xflags & 4)))) {
        snprintf(err, errn, "pocket finish leads on first / last depth cut only (0x928 / 0x970): per-depth processing (FUN_1801bfd20) not ported"); return 0; }
    return 1;
}

/* ------------------------------------------------------------------------------------------------ pass bounds */
/* FUN_1801cc170 for one pass (l.318050-318120): the base (computer: the tool-centre bounds; control only: the chain bounds,
   local_168 = param_1 with the wall stock added to the distance, l.317960-317968) offset by (n - j - 1) x spacing (+ stock),
   offset_pock_bounds with the minimum fillet angle; High Speed corner smoothing FUN_1801d2b80; Optimize (control only,
   l.318068-318086): out by D x 0.505, merged, back with angle pi; merge_pock_bounds (closed pockets, l.318117). Here
   gls_offset does offset + merge in one (pgeo.h: the ccomp_pock substitute). */
static Gls pk_fin_bounds(const PkFin *F, const CamParams *p, const Gls *base, int j, double minfil, double minfil_hs, double tol) {
    double d = (F->nf - j - 1) * F->fs + (F->ctl_only ? p->stock_xy : 0);
    Gls fl = gls_offset(base, -d, minfil, tol);
    if (F->smooth && fl.n) pk_smooth(&fl, F->slen, minfil_hs, tol);
    if (F->opt && fl.n) {
        double o = p->tool_dia * 0.005 + p->tool_dia * 0.5;
        Gls a = gls_offset(&fl, -o, minfil, tol);
        if (!a.n) { gls_free(&a); gls_free(&fl); return a; }      /* l.318078: none -> the pass is skipped */
        Gls bk = gls_offset(&a, o, M_PI, tol);
        gls_free(&a); gls_free(&fl); fl = bk;
    }
    return fl;
}

/* pt_in_ctour (tri-state, nc_tol): 1 inside, 0 on, -1 outside */
static int pk_in_ctour(const Gl *l, PV q, double nct, double tol) {
    if (gl_closest(l, q, NULL, NULL, NULL) <= nct) return 0;
    Gls one = { (Gl *)l, 1, 1 };
    return gls_inside(&one, q, tol) ? 1 : -1;
}
/* mccore pt_in_machinable_area (decomp/mccore_pock.c l.1334): in order over the bounds, an outer bound containing the
   point allows it; an outer bound outside of which it lies, before any containing one, rejects it; an island strictly
   containing it rejects it; on a boundary is neither */
static int pk_machinable(const Gls *s, PV q, double nct, double tol) {
    int in = 0;
    for (int i = 0; i < s->n; i++) {
        int c = pk_in_ctour(&s->l[i], q, nct, tol);
        if (!s->l[i].island) { if (c == 1) in = 1; else if (!in && c == -1) return 0; }
        else if (c == 1) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------------------------------ intersections */
/* points where entity a meets entity b (within tol on both; touching included), with a's parameter. Returns 0..2 */
static int pk_xpts(const Gt *a, const Gt *b, double tol, PV *o, double *ta) {
    PV c[2]; int n = 0;
    if (a->t == 'P' || b->t == 'P') return 0;
    if (a->t == 'L' && b->t == 'L') {
        PV da = pv(a->b.x - a->a.x, a->b.y - a->a.y), db = pv(b->b.x - b->a.x, b->b.y - b->a.y);
        double den = da.x * db.y - da.y * db.x; if (fabs(den) < 1e-15) return 0;
        double s = ((b->a.x - a->a.x) * db.y - (b->a.y - a->a.y) * db.x) / den;
        c[n++] = pv(a->a.x + da.x * s, a->a.y + da.y * s);
    } else if (a->t == 'A' && b->t == 'A') {
        double dx = b->c.x - a->c.x, dy = b->c.y - a->c.y, d = hypot(dx, dy); if (d < 1e-15) return 0;
        double aa = (a->r * a->r - b->r * b->r + d * d) / (2 * d), h2 = a->r * a->r - aa * aa;
        if (h2 < -tol * fmax(a->r, b->r)) return 0;
        double h = h2 > 0 ? sqrt(h2) : 0; PV u = pv(dx / d, dy / d), m = pv(a->c.x + u.x * aa, a->c.y + u.y * aa);
        c[n++] = pv(m.x - u.y * h, m.y + u.x * h); if (h > 1e-12) c[n++] = pv(m.x + u.y * h, m.y - u.x * h);
    } else {
        const Gt *l = a->t == 'L' ? a : b, *k = a->t == 'L' ? b : a;
        PV d = pv(l->b.x - l->a.x, l->b.y - l->a.y); double L = hypot(d.x, d.y); if (L < 1e-15) return 0;
        PV u = pv(d.x / L, d.y / L), w = pv(l->a.x - k->c.x, l->a.y - k->c.y);
        double bb = w.x * u.x + w.y * u.y, disc = bb * bb - (w.x * w.x + w.y * w.y - k->r * k->r);
        if (disc < -2 * tol * k->r) return 0;
        double s = disc > 0 ? sqrt(disc) : 0;
        c[n++] = pv(l->a.x + u.x * (-bb - s), l->a.y + u.y * (-bb - s)); if (s > 1e-12) c[n++] = pv(l->a.x + u.x * (-bb + s), l->a.y + u.y * (-bb + s));
    }
    int m = 0;
    for (int i = 0; i < n; i++) {
        double t1 = gt_t_near(a, c[i]), t2 = gt_t_near(b, c[i]); PV p1 = gt_pt(a, t1), p2 = gt_pt(b, t2);
        if (hypot(p1.x - c[i].x, p1.y - c[i].y) <= tol && hypot(p2.x - c[i].x, p2.y - c[i].y) <= tol) { o[m] = p1; ta[m] = t1; m++; }
    }
    return m;
}

/* ------------------------------------------------------------------------------------------------ leads */
/* FUN_1801c2b30: a lead arc (line = 0) or line (line = 1) at the start (exit = 0) or end (exit = 1) of entity e, on side
   'L' / 'R' */
static Gt pk_lead_ent(const Gt *e, int exit, int line, int left, double len, double r, double sw, int perp) {
    PV p = exit ? gt_end(e) : gt_start(e), t;
    if (e->t == 'L') { double dx = e->b.x - e->a.x, dy = e->b.y - e->a.y, L = hypot(dx, dy); t = L == 0 ? pv(0, 0) : pv(dx / L, dy / L); }
    else { double a = exit ? e->sa + e->sw : e->sa; t = pv(-sin(a), cos(a)); if (e->sw < 0) t = pv(-t.x, -t.y); }
    PV n = left ? pv(-t.y, t.x) : pv(t.y, -t.x);           /* local_a8 / uStack_a0 */
    if (!line) {
        PV c = pv(p.x + n.x * r, p.y + n.y * r);
        double s = left ? sw : -sw, a = 0;
        if (n.x * n.x + n.y * n.y >= 1e-12) { a = atan2(-n.y, -n.x); if (a < 0) a += 2 * M_PI; }
        if (!exit) a -= s;
        return gt_arc(c, r, a, s);
    }
    PV dir = perp ? n : (exit ? t : pv(-t.x, -t.y));        /* cVar3 = op +0x891 / +0x8d9 (perpendicular) */
    PV q = pv(p.x + dir.x * len, p.y + dir.y * len);
    return exit ? gt_line(p, q) : gt_line(q, p);
}

typedef struct { PV p; double key, t; int ent, same; } PkIx;

/* the lead section midpoint FUN_1801bbc70 (CalcSectMidPtViaM): along the lead from intersection i to the next one (or to
   the lead's start for the entry / its end for the exit), the point at half the length */
static PV pk_sect_mid(const Gt *le, int nle, const PkIx *ix, int cnt, int i, int entry) {
    int e0 = ix[i].ent, e1; double t0 = ix[i].t, t1;
    if (i < cnt - 1) { e1 = ix[i + 1].ent; t1 = ix[i + 1].t; }
    else if (!entry) { e1 = nle - 1; t1 = 1; }
    else { e1 = 0; t1 = 0; }
    if (e1 < e0 || (e1 == e0 && t1 < t0)) { int ti = e0; e0 = e1; e1 = ti; double tt = t0; t0 = t1; t1 = tt; }
    double L = 0;
    for (int k = e0; k <= e1; k++) { double a = k == e0 ? t0 : 0, b = k == e1 ? t1 : 1; L += gt_len(&le[k]) * (b - a); }
    double h = L * 0.5;
    for (int k = e0; k <= e1; k++) {
        double a = k == e0 ? t0 : 0, b = k == e1 ? t1 : 1, l = gt_len(&le[k]) * (b - a);
        if (h <= l || k == e1) { double f = l > 0 ? h / l : 0; if (f > 1) f = 1; return gt_pt(&le[k], a + (b - a) * f); }
        h -= l;
    }
    return gt_pt(&le[e0], t0);
}
/* FUN_1801c37b0: insert sorted by key (ascending), at most 8 kept */
static void pk_ix_ins(PkIx *a, int *n, PkIx x) {
    int pos = *n;
    for (int k = *n - 1; k >= 0; k--) if (x.key < a[k].key) pos = k;
    if (pos >= 8) return;
    for (int k = (*n < 8 ? *n : 7); k > pos; k--) a[k] = a[k - 1];
    a[pos] = x; if (*n < 8) (*n)++;
}
/* FUN_1801c1b60: the first intersection (from the pass outwards) where the lead leaves: another bound's offset, or the
   section beyond it outside the offset bounds (pt_in_machinable_area of param_5), or - for the first one - the piece
   between it and the pass outside the pass bounds */
static int pk_ix_trim(const Gt *le, int nle, const PkIx *ix, int cnt, const Gls *pass, const Gls *inner, int entry, double nct, double tol) {
    for (int i = 0; i < cnt; i++) {
        if (!ix[i].same) return i;
        PV m = pk_sect_mid(le, nle, ix, cnt, i, entry);
        int ok = pk_machinable(inner, m, nct, tol);
        if (ok && i == 0) { PV m2 = gt_pt(&le[ix[i].ent], (ix[i].t + (entry ? 1.0 : 0.0)) * 0.5); ok = pk_machinable(pass, m2, nct, tol); }
        if (!ok) return i;
    }
    return -1;
}

/* the lead check FUN_1801c4860 -> FUN_1801c3d70 (param_1 0, param_8 1, param_9 0x20, param_10 1): le = [entry nin][exit
   nout]; keep[] in / out. pass = the pass bounds (local_1a8), inner = each pass bound offset by the finish spacing
   (offset_pock_bounds bound by bound, +0x50 = the bound it came from: parent[]), me = the index of the bound led */
static void pk_lead_check(Gt *le, int nle, int nin, int nout, char *keep, const Gls *pass, const Gls *inner, const int *parent, int me, double nct, double tol) {
    PkIx a[8], x[8]; int na = 0, nx = 0;
    for (int b = 0; b < inner->n; b++)
        for (int k = 0; k < nle; k++)
            for (int m = 0; m < inner->l[b].n; m++) {
                PV o[2]; double ta[2]; int c = pk_xpts(&le[k], &inner->l[b].e[m], nct, o, ta);
                for (int q = 0; q < c; q++) {
                    PkIx v = { o[q], 0, ta[q], k, parent[b] == me };
                    if (k < nin) { v.key = 2.0 - (k > 0 ? ta[q] + 1.0 : ta[q]); pk_ix_ins(a, &na, v); }
                    else if (k >= nle - nout) { v.key = k > nin ? ta[q] + 1.0 : ta[q]; pk_ix_ins(x, &nx, v); }
                }
            }
    int ia = pk_ix_trim(le, nle, a, na, pass, inner, 1, nct, tol), ix_ = pk_ix_trim(le, nle, x, nx, pass, inner, 0, nct, tol);
    if (ia >= 0) {                                            /* l.~311870: the entry from the intersection on */
        int e = a[ia].ent; if (e > 0) keep[0] = 0;
        le[e] = gt_sub(&le[e], a[ia].t, 1);
    }
    if (ix_ >= 0) {
        int e = x[ix_].ent; if (e < nle - 1) keep[nle - 1] = 0;
        le[e] = gt_sub(&le[e], 0, x[ix_].t);
    }
    int degenerate = pass->n ? fabs(gl_area(&pass->l[0])) < 1e-6 : 0;   /* bVar5 */
    for (int k = 0; k < nle; k++) {
        if (gt_len(&le[k]) < nct) keep[k] = 0;
        if (!keep[k] || (k >= nin && k < nle - nout)) continue;   /* the overlap: the length test only */
        int ok = !degenerate && pk_machinable(pass, gt_pt(&le[k], 0.5), nct, tol);
        if (ok) ok = !degenerate && pk_machinable(pass, gt_pt(&le[k], k < nin ? 0.001 : 0.999), nct, tol);
        if (!ok) keep[k] = 0;
    }
    /* a kept lead crossing the (first) pass bound away from the lead's ends drops its whole side (l.~312000) */
    int first = -1; for (int k = 0; k < nle; k++) if (keep[k]) { first = k; break; }
    if (first >= 0 && pass->n) {
        const Gl *pb = &pass->l[0];
        PV ins = gt_start(&le[0]), ine = nin ? gt_end(&le[nin - 1]) : ins;
        PV outs = nout ? gt_start(&le[nle - nout]) : ins, oute = gt_end(&le[nle - 1]);
        int din = 0, dout = 0;
        for (int k = 0; k < nle; k++) {
            if (!keep[k]) continue;
            for (int m = 0; m < pb->n; m++) {
                PV o[2]; double ta[2]; int c = pk_xpts(&le[k], &pb->e[m], nct, o, ta);
                for (int q = 0; q < c; q++) {
                    if (k < nin) { if (hypot(ins.x - o[q].x, ins.y - o[q].y) > nct && hypot(ine.x - o[q].x, ine.y - o[q].y) > nct) din = 1; }
                    else if (k >= nle - nout) { if (hypot(outs.x - o[q].x, outs.y - o[q].y) > nct && hypot(oute.x - o[q].x, oute.y - o[q].y) > nct) dout = 1; }
                }
            }
        }
        if (din) for (int k = 0; k < nin; k++) keep[k] = 0;
        if (dout) for (int k = nle - nout; k < nle; k++) keep[k] = 0;
    }
    /* a dropped entry entity drops the ones before it, a dropped exit entity the ones after it */
    for (int k = 1; k < nle - 1; k++) if (!keep[k]) {
        if (k < nin) for (int q = 0; q < k; q++) keep[q] = 0;
        else if (k >= nle - nout) for (int q = k + 1; q < nle; q++) keep[q] = 0;
    }
}

/* FUN_1801bdc00 for a closed bound l (already started where the pass starts, see pk_fin_pick: split = 1 when the start
   entity still has to be split at its midpoint, 0 when l already starts at the entry point - this program's stand-in for
   the midpoint, the start nearest the first chain entity's midpoint). Builds out = [entry leads] + l + [exit leads];
   *nin_k / *nout_k = the lead entities kept. pass / inner / parent / me: for the checks. */
static void pk_fin_leads(const PkFin *F, Gl *l, int split, int left, const Gls *pass, const Gls *inner, const int *parent, int me,
                         Gl *out, int *nin_k, int *nout_k, double nct, double tol) {
    memset(out, 0, sizeof *out); *nin_k = *nout_k = 0;
    int any = F->ein_line || F->ein_arc || F->eout_arc || F->eout_line || F->ovl > 0;
    if (any && (F->ein_arc || F->eout_arc)) {
        /* l.308810-308835: start at the first entity that is not an arc of radius <= a lead radius turning towards
           the lead side (pock_ccw ^ island ^ clockwise arc); none -> no leads */
        double area = gl_area(l); int ccw = area > 0, found = -1;
        for (int i = 0; i < l->n; i++) {
            const Gt *e = &l->e[i];
            if (e->t != 'A' || (F->lr_raw < e->r && F->xr_raw < e->r)) { found = i; break; }
            int c = ccw; if (l->island) c = !c; if (e->sw < 0) c = !c;
            if (!c) { found = i; break; }
        }
        if (found < 0) any = 0;
        else if (found > 0) { gl_split_start(l, found, 0.5); split = 0; }
    }
    if (!any) { for (int i = 0; i < l->n; i++) gl_push(out, l->e[i]); return; }
    if (split) gl_split_start(l, 0, 0.5);                    /* FUN_1801bf950: the first entity in two halves */
    Gt *le = malloc((l->n + 6) * sizeof(Gt)); char *keep = malloc(l->n + 6);
    int nle = 0, nin = 0, nout = 0, novl = 0;
    const Gt *e0 = &l->e[0], *en = &l->e[l->n - 1];
    Gt ea = { 0 }, xa = { 0 };
    if (F->ein_arc) ea = pk_lead_ent(e0, 0, 0, left, 0, F->lr, F->lsw, 0);
    if (F->ein_line) le[nle++] = pk_lead_ent(F->ein_arc ? &ea : e0, 0, 1, left, F->ll, 0, 0, F->lperp);
    if (F->ein_arc) le[nle++] = ea;
    nin = nle;
    if (F->ovl > 0) {
        /* FUN_1801bd7c0: the overlap goes on along the pass from its start (the second half of the split entity, the
           entities after it, the first half) for 0x988: whole entities while their length <= the rest + tol, the next one
           cut at the rest (creep_along_ent), at most once round */
        double rest = F->ovl;
        for (int i = 0; i < l->n; i++) {
            Gt e = l->e[i]; double L = gt_len(&e);
            if (L <= rest + CAM_SYS_TOL) { le[nle++] = e; novl++; rest -= L; if (rest <= CAM_SYS_TOL) break; }
            else { le[nle++] = gt_sub(&e, 0, rest / L); novl++; break; }
        }
        if (novl) en = &le[nle - 1];
    }
    if (F->eout_arc) { xa = pk_lead_ent(en, 1, 0, left, 0, F->xr, F->xsw, 0); le[nle++] = xa; }
    if (F->eout_line) le[nle++] = pk_lead_ent(F->eout_arc ? &xa : en, 1, 1, left, F->xl, 0, 0, F->xperp);
    nout = nle - nin - novl;
    for (int k = 0; k < nle; k++) keep[k] = 1;
    if (nin > F->chk_in && nout > F->chk_out) pk_lead_check(le, nle, nin, nout, keep, pass, inner, parent, me, nct, tol);
    for (int k = 0; k < nin; k++) if (keep[k]) { gl_push(out, le[k]); (*nin_k)++; }
    for (int i = 0; i < l->n; i++) gl_push(out, l->e[i]);
    for (int k = nin; k < nle - nout; k++) if (keep[k]) gl_push(out, le[k]);
    for (int k = nle - nout; k < nle; k++) if (keep[k]) { gl_push(out, le[k]); (*nout_k)++; }
    free(le); free(keep);
}

/* ------------------------------------------------------------------------------------------------ sections */
/* a toolpath section as wr_pock_toolpath sorts them (FUN_1801c74a0 l.~315170 reads the -99 markers of
   bnci_wr_pock_marker: pocket, finish flag, pass, depth index, depth, sub-pocket; thin wall fields +0x3e / +0x40) */
typedef struct { int fin, k, zi, j, thin, ti; double z; long pos; int sub, p9; int sk, face; double depth; } PkSec;   /* sub / p9: bnci_wr_pock_marker param_7 / param_9 (Remachining: sub-pocket local_232, group local_236 of FUN_1801ec3c0; section +0x10 / +0x1a); sk: the marker's pocket number (= k except Island facing); face: a top section (key 8); depth: the cut's Z (z = the sort depth) */
static const PkFin *g_pk_sec_fin;
/* FUN_1801c6520: 12 keys, the depth (one of them) compared as a double with tol, higher first; then the write position */
static int pk_sec_cmp(const void *pa, const void *pb) {
    const PkSec *a = pa, *b = pb; const PkFin *F = g_pk_sec_fin;
    long ka[12], kb[12]; int dk;
    const PkSec *s[2] = { a, b }; long *K[2] = { ka, kb };
    for (int w = 0; w < 2; w++) {
        const PkSec *x = s[w]; long *k = K[w];
        long fin = x->fin, pass = x->fin ? x->j : 0, th = x->thin ? x->ti : 0, pk = x->sk, sub = x->sub, p9 = x->p9;
        for (int i = 0; i < 12; i++) k[i] = 0;
        /* [0] +0x44 spring (0), [8] facing (0), [9] depth index, [10] thin flag, [11] spring index (0) */
        k[8] = x->face; k[9] = x->zi; k[10] = x->thin; k[11] = 0;
        if (!F->after) {
            if (!F->bydepth) { k[1] = fin; k[2] = th; k[3] = pk; k[4] = sub; k[5] = p9; k[7] = pass; dk = 6; }
            else { k[1] = fin; k[3] = th; k[4] = pk; k[5] = sub; k[6] = p9; k[7] = pass; dk = 2; }
        } else if (!F->bydepth) {
            if (!F->thin) { k[1] = th; k[2] = pk; k[3] = sub; k[4] = p9; k[6] = fin; k[7] = pass; dk = 5; }
            else { k[1] = th; k[2] = pass; k[3] = pk; k[4] = sub; k[5] = p9; k[7] = fin; dk = 6; }
        } else {
            if (F->thin) { k[2] = th; k[3] = pass; k[4] = pk; k[5] = sub; k[6] = p9; k[7] = fin; dk = 1; }
            else { k[2] = th; k[3] = pk; k[4] = sub; k[5] = p9; k[6] = fin; k[7] = pass; dk = 1; }
        }
    }
    for (int i = 0; i < 12; i++) {
        if (i == dk) {
            if (a->z > b->z + CAM_SYS_TOL) return -1;
            if (b->z > a->z + CAM_SYS_TOL) return 1;
            continue;
        }
        if (ka[i] < kb[i]) return -1;
        if (ka[i] > kb[i]) return 1;
    }
    return a->pos < b->pos ? -1 : a->pos > b->pos;
}
