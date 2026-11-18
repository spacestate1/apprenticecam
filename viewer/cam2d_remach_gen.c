/* cam2d_remach_gen.c - Contour type 5 Remachining: the driver FUN_1800550c0 (decomp/mcmill_all.c l.58166-58845; formats/contour_remachining.md section 2), on the engine
 * cam2d_remach.c. Included by cam2d.c: it uses its motion helpers. Quirks of the DLL are kept as read and noted where they appear. */
#include "cam2d_remach.h"
#include <inttypes.h>

static CamPrevOpsFn g_prev_ops_fn;
void cam_set_prev_ops_fn(CamPrevOpsFn fn) { g_prev_ops_fn = fn; }

/* op->p.file_op_id's predecessors in F.ops: their stored NCI, Control = record 0x5d0 == 1, the +0x5d8 translation as read by verify_2d_operation (more2 l.410-418) */
int cam_prev_op_entry(const McamFile *f, int i, CamPrevOp *e) {
    memset(e, 0, sizeof *e);
    if (!mcam_op_nci_section(f, i, &e->nci, &e->nci_n)) return 0;
    const uint8_t *r = f->ops[i].raw; uint32_t rn = f->ops[i].raw_n;
    e->opcode = f->ops[i].opcode;
    if (r && rn >= 0x5d2) e->control = (int16_t)(r[0x5d0] | r[0x5d1] << 8) == 1;
    if (r && rn > 0x13a9 && e->opcode == 3) e->pocket_1340 = (int16_t)(r[0x13a8] | r[0x13a9] << 8) == 1;   /* record 0x13a8 = op +0x1340: the Trim to rest stop of FUN_1802cb260 */
    if (r && rn > 0x13a9 && e->opcode == 3) e->pk_fin_ctl = r[0x1239] && (int16_t)(r[0x13a8] | r[0x13a9] << 8) == 1;   /* mcmill l.338082-338083: op +0x11d1, op +0x1340 */
    if (r && rn > 0x6c0 && r[0x640]) {                       /* op +0x5d8: the 3x3 at +0x5e0 times the vector at +0x628 (record +0x68) */
        double m[9], v[3]; memcpy(m, r + 0x648, sizeof m); memcpy(v, r + 0x690, sizeof v);
        e->xv[0] = v[0] * m[0] + v[1] * m[3] + v[2] * m[6];
        e->xv[1] = v[0] * m[1] + v[1] * m[4] + v[2] * m[7];
        e->xv[2] = v[0] * m[2] + v[1] * m[5] + v[2] * m[8];
        e->xlate = rm_tol < sqrt(e->xv[0] * e->xv[0] + e->xv[1] * e->xv[1] + e->xv[2] * e->xv[2]);
    }
    return 1;
}
int cam_prev_ops_stored(const McamFile *f, const CamOp *op, CamPrevOp **out, int *n) {
    CamPrevOp *o = calloc((size_t)(f->nops ? f->nops : 1), sizeof *o); int k = 0;
    for (int i = 0; i < f->nops; i++) {
        if (f->ops[i].id == op->p.file_op_id) break;
        CamPrevOp e;
        if (cam_prev_op_entry(f, i, &e)) o[k++] = e;
    }
    *out = o; *n = k;
    return 1;
}
/* set_min_fil_angle(op +0x56b): 0 None pi, 1 Sharp 0.785398, 2 All 0.01 (cam2d.c ct_offset's comment) */
static double rm_minfil(const CamParams *p) { return p->roll == 2 ? 0.01 : p->roll == 1 ? 0.785398 : 3.141592653589793; }

static char rm_side(const CamParams *p, const Chain *ch, const double *xy, int n) {
    if (p->side == SIDE_ON) return p->ct_off_right ? 'R' : 'L';   /* Off: DAT_1804f16f8 = op +0x56a ? 'R' : 'L' (l.58324-58327), relative to the chain's direction */
    CPath one = { (double *)xy, n }; double a = cl_area(&one);
    int left = p->side == 1;
    if (ch->closed && !p->side_rel) left = (p->side == SIDE_INSIDE) == (a > 0);   /* as ctoff_port */
    return left ? 'L' : 'R';
}

/* chain_to_pock_bound (hlp l.702-834) for a flat chain of lines and arcs */
static RmB *rm_chain_bound(const McamFile *f, const Chain *ch) {
    RmB *b = rm_b_new(ch->n);
    for (int i = 0; i < ch->n; i++) {
        const Ent *E = &f->ents[ch->ent[i]]; int r = ch->rev[i]; RmEnt *x = &b->e[i];
        if (E->type == E_LINE) { V3 u = r ? E->b : E->a, v = r ? E->a : E->b; x->id = 'L'; x->a[0] = u.x; x->a[1] = u.y; x->a[2] = v.x; x->a[3] = v.y; }
        else { x->id = 'A'; x->a[0] = E->c.x; x->a[1] = E->c.y; x->a[2] = E->r; x->a[3] = r ? E->a0 + E->sw : E->a0; x->a[4] = r ? -E->sw : E->sw; }
    }
    b->valid = 1;
    rm_b_info(b);
    return b;
}

/* FUN_1805e8120 / verify_2d_operation (decomp core l.1433 = remach_more2.c l.371-629): an NCI section swept into the map. Records: 0 / 1 (+0x20 xyz, 1 swept if both Z <= zlim and no
   marker), 2 / 3 (plane 0 only, centre +0x38), 0x51 / 100 discs, -0x60 marker (the next linear move is not swept), -3 end, 0x3f5 the tool (diameter +0x20: radius, + rad_adj) */
static void rm_verify_nci(const uint8_t *d, size_t len, double z, double rad_adj, RmMap *m, int xl, const double xv[3], double rad) {
    static uint8_t buf[0x370];
    double zlim = rm_nc_tol + z, last[3] = { 0, 0, 0 }; int mark = 0; size_t off = 0;
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        memset(buf, 0, sizeof buf); memcpy(buf, d + off + 2, sz < sizeof buf ? sz : sizeof buf); off += 2 + sz;
        int16_t g = (int16_t)(buf[0] | buf[1] << 8);
        if (g == -3) break;
        double v[3]; memcpy(v, buf + 0x20, 24);
        if (xl && (g == 0 || g == 1 || g == 2 || g == 3)) { v[0] += xv[0]; v[1] += xv[1]; v[2] += xv[2]; }   /* FUN_1805e8120: the plane-0 branch; a move on another plane is not swept below */
        if (g == -0x60) mark = 1;
        else if (g == 0) { memcpy(last, v, sizeof last); mark = 0; }
        else if (g == 1) {
            if (last[2] <= zlim && v[2] <= zlim && !mark) { double l[4] = { last[0], last[1], v[0], v[1] }; rm_verify_line(l, rad, m); }
            mark = 0; memcpy(last, v, sizeof last);
        } else if (g == 2 || g == 3) {
            int16_t plane = (int16_t)(buf[0x18] | buf[0x19] << 8);
            if (plane == 0) {
                if (last[2] <= zlim && v[2] <= zlim) {
                    double cen[2]; memcpy(cen, buf + 0x38, 16);
                    if (xl) { cen[0] += xv[0]; cen[1] += xv[1]; }
                    /* constr_arc_new (cal l.2354-2442), tolerance nc_tol */
                    const double *a0 = g == 2 ? v : last, *a1 = g == 2 ? last : v;
                    double d1 = a0[0] - cen[0], d2 = a0[1] - cen[1], X = a1[0] - cen[0], Y = a1[1] - cen[1], s0 = d1 * d1 + d2 * d2, r0 = sqrt(s0), s1 = X * X + Y * Y, r1 = sqrt(s1);
                    if (fabs(r0 - r1) < rm_nc_tol || fabs(r0 - r1) == rm_nc_tol) {
                        double dx = a1[0] - a0[0], dy = a1[1] - a0[1], chord = sqrt(dy * dy + dx * dx), sa, sw;
                        if (rm_tol <= chord) {
                            sa = 0.0; if (1e-12 <= s0) { sa = atan2(d2, d1); if (sa < 0.0) sa += 6.283185307179586; }
                            double ea = 0.0; if (1e-12 <= s1) { ea = atan2(Y, X); if (ea < 0.0) ea += 6.283185307179586; }
                            for (sw = ea - sa; sw <= 1e-08; sw += 6.283185307179586) { }
                        } else { sw = 6.283185307179586; sa = 0.0; }
                        double a[5] = { cen[0], cen[1], r0, sa, sw };
                        rm_verify_arc(a, rad, m);
                    }
                }
                memcpy(last, v, sizeof last);
            }
        } else if (g == 0x51 || g == 100) {
            if (v[2] <= zlim) { double l[4] = { v[0], v[1], v[0], v[1] }; rm_verify_line(l, rad, m); }
        } else if (g == 0x3f5) {
            double dia; memcpy(&dia, buf + 0x20, 8); rad = dia * 0.5;
            if (0.0 <= rad + rad_adj) rad = rad + rad_adj;
        }
    }
}
/* The same over a generated op's Move list (CamOp.m): rapids move only; a feed move's arc tag (mb_tag_arc) gives the arc; markers are not recorded in Move, see the notes */
static void rm_verify_moves(const Move *m, int n, double z, double rad, RmMap *map) {
    double zlim = rm_nc_tol + z;
    for (int i = 1; i < n; i++) {
        const V3 a = m[i - 1].p, c = m[i].p;
        if (m[i].rapid) continue;
        if (m[i].arc > 0) {
            int e = i;
            while (e + 1 < n && m[e + 1].arc == m[i].arc && !m[e + 1].rapid) e++;
            double cx = m[i].cx, cy = m[i].cy, r = hypot(a.x - cx, a.y - cy);
            V3 q = m[e].p;
            if (a.z <= zlim && q.z <= zlim) {
                double sa = atan2(a.y - cy, a.x - cx), ea = atan2(q.y - cy, q.x - cx), sw = ea - sa;
                if (m[i].cw) { while (sw >= -1e-9) sw -= 2 * M_PI; } else { while (sw <= 1e-9) sw += 2 * M_PI; }
                double arc[5] = { cx, cy, r, sa, sw };
                rm_verify_arc(arc, rad, map);
            }
            i = e;
        } else if (a.z <= zlim && c.z <= zlim) { double l[4] = { a.x, a.y, c.x, c.y }; rm_verify_line(l, rad, map); }
    }
}

typedef struct { RmB *b; int idx, pos, ne, nx, t16, t17; } RmOut;

static void rm_outs_free(RmOut *o, int n) { for (int i = 0; i < n; i++) rm_b_free(o[i].b); free(o); }

/* One chain at one processed depth: FUN_1800550c0 l.58340-58760 up to the write. Returns the writer's list in group order. 0 = refused (op->error) */
/* the earlier operations swept into the map (verify_2d_operation): contour FUN_180053a70 / pocket FUN_1801ebc60 (l.337954): src 0 every op before the current one, src 1 only the last. Refused (post_warning 0x519, -1): contour rule (pocket_rule 0): a prior
   op with Control compensation; pocket rule: a prior Pocket op with finish passes in control only (l.338082-338083 opcode 3, op +0x11d1 and op +0x1340 == 1). */
static int rm_prev_cut(const McamFile *f, const CamOp *op, int src, double depth, RmMap *cut, int pocket_rule, char *err, size_t errn) {
    CamPrevOp *pv = NULL; int npv = 0;
    if (!g_prev_ops_fn || !g_prev_ops_fn(f, op, &pv, &npv)) { snprintf(err, errn, "Remachining from previous operations: the operation list is not available here (use the roughing tool source)"); return 0; }
    int first = src == 1 && npv > 1 ? npv - 1 : 0, fail = 0;   /* source 1: the last op before the current one */
    if (npv > 0) for (int k = first; k < npv; k++) {
        if (pocket_rule ? pv[k].pk_fin_ctl : pv[k].control) {  /* post_warning 0x519 (mcmillres 1305), -1 */
            snprintf(err, errn, "Remachining: a previous operation uses cutter compensation in control (post warning 0x519): its stock cannot be computed"); fail = 1; break; }
        if (pv[k].nci) rm_verify_nci(pv[k].nci, pv[k].nci_n, depth, 0.0, cut, pv[k].xlate, pv[k].xv, 0.0);
        else rm_verify_moves(pv[k].m, pv[k].nm, depth, pv[k].dia * 0.5, cut);
    }
    for (int k = 0; k < npv; k++) free(pv[k].nci);
    free(pv);
    return !fail;
}

static int rm_compute(const McamFile *f, const CamOp *op, const CamParams *p, const RmB *chain_bound, double zc, double depth, char side, int bv7, int leads, RmOut **out, int *nout, int last_depth, int n_actual, char *err, size_t errn, RmB **keep) {
    (void)last_depth; (void)n_actual;
    RmB *bound = rm_b_copy(chain_bound), *tofree = bound;
    bound->next = bound->prev = NULL;
    double minfil = rm_minfil(p), D = p->tool_dia, R = D * 0.5, stock = p->stock_xy, cell = p->rm_cell, Rr = 0.0;
    const char *why = NULL;
    int taper = ct_tapered(p);
    if (taper) {                                                /* l.58410-58422: FUN_1800569a0 (ct_taper_stock) includes the wall stock */
        double ts = ct_taper_stock(p, zc, depth);
        RmB *lst = NULL;
        if (rm_ccomp_pock(bound, &lst, ts, side, minfil, 1, 0, 0, &why) || !lst) { snprintf(err, errn, "Remachining: MCCECalcContourOffset port refused a call: %s", why ? why : "no result"); rm_b_free(bound); return 0; }
        rm_b_free(bound); bound = lst; tofree = lst;
    }
    rm_b_infos(bound);
    double ymin = 1e38, ymax = -1e38;
    for (RmB *b = bound; b; b = b->next) { if (b->bbox[1] <= ymin && ymin != b->bbox[1]) ymin = b->bbox[1]; if (ymax <= b->bbox[3] && b->bbox[3] != ymax) ymax = b->bbox[3]; }
    double w = D * 2.142857142857143; ymin -= w; ymax += w;
    RmMap cut, reach, rest2; memset(&cut, 0, sizeof cut); memset(&reach, 0, sizeof reach); memset(&rest2, 0, sizeof rest2);
    int src = p->rm_src;
    if (src < 2) {                                               /* FUN_180053a70 l.57212-57488 */
        rm_map_init(&cut, ymin, ymax, cell);
        if (!rm_prev_cut(f, op, src, depth, &cut, 0, err, errn)) { rm_map_free(&cut); rm_b_free_list(&tofree); return 0; }
    } else {                                                     /* FUN_180054f70 mm l.58134-58160 */
        Rr = p->rm_dia * 0.5;
        rm_map_init(&cut, ymin, ymax, cell);
        double ang = bv7 ? 0.785398 : minfil;
        for (RmB *b = bound; b; b = b->next) {
            RmB *l = NULL;
            if (rm_ccomp_pock(b, &l, Rr + stock, side, ang, 1, 0, 0, &why)) { snprintf(err, errn, "Remachining: MCCECalcContourOffset port refused the roughing tool offset: %s", why ? why : ""); rm_map_free(&cut); rm_b_free_list(&tofree); return 0; }
            rm_verify_bounds(l, Rr, &cut);
            rm_b_free_list(&l);
        }
    }
    int count = 1; double step = 0.0;                            /* drv 0x1800557c3-0x18005583d */
    if (1e-12 < fabs(R)) {
        double e = Rr - (stock + R), v = e / R, n = 0.0 <= v ? floor(v) : ceil(v);
        count = (int)(short)((int)n + 2); step = e / (double)(count - 1);
    }
    rm_map_init(&reach, ymin, ymax, cell);                       /* FUN_180054db0 mm l.58094-58128 */
    double ang_r = bv7 ? 3.141592653589793 : minfil;
    for (RmB *b = bound; b; b = b->next) {
        double dv = R + stock;
        for (int k = 0; k < count; k++, dv += step) {
            RmB *l = NULL;
            if (rm_ccomp_pock(b, &l, dv, side, ang_r, 1, 0, 0, &why)) { snprintf(err, errn, "Remachining: MCCECalcContourOffset port refused the reach offset: %s", why ? why : ""); rm_map_free(&cut); rm_map_free(&reach); rm_b_free_list(&tofree); return 0; }
            rm_verify_bounds(l, R, &reach);
            rm_b_free_list(&l);
        }
    }
    rm_map_diff(&reach, &cut, cell);
    RmBnd *bnds = NULL, *bnds2 = NULL; RmB *restb = NULL;
    rm_map_copy(&rest2, &reach);
    rm_map_trace(&reach, R, 0, &bnds);
    rm_verify_bnds(bnds, R + p->rm_clr, &reach, &rest2);
    rm_map_trace(&rest2, R, 0, &bnds2);
    rm_filter_bnds(&rest2, bnds2, cell * 0.5);
    rm_bnds_to_bounds(&rest2, bnds2, &restb);
    { short ng; rm_sort(&restb, 1, p->ct_rec_tol, &ng); }
    rm_b_bboxs(restb);
    rm_bnds_free(bnds); rm_bnds_free(bnds2);
    /* passes (l.58540-58635) */
    int mpon = p->mc_mp_on, nr = mpon ? p->mc_mp_nr : 0, nf = mpon ? p->mc_mp_nf : 1, total = nr + nf;
    RmRem *rem = NULL;
    RmLeads L; memset(&L, 0, sizeof L);
    double sw_in = p->lead_sw > 0 ? p->lead_sw : ((p->lead_sw_rec & 1) ? 0.0 : M_PI / 2), sw_out = p->lead_xsw > 0 ? p->lead_xsw : ((p->lead_sw_rec & 2) ? 0.0 : M_PI / 2);   /* cam2d.h lead_sw: an op made here keeps 0 = a quarter */
    L.r_in = p->lead_r; L.sw_in = sw_in; L.len_in = p->lead_len; L.perp_in = p->lead_in_perp; L.dz_in_line = p->mc_lead_dz[0]; L.dz_in_arc = p->mc_lead_dz[1];
    L.r_out = p->lead_xr; L.sw_out = sw_out; L.len_out = p->lead_xlen; L.perp_out = p->lead_out_perp; L.dz_out_line = p->mc_lead_dz[2]; L.dz_out_arc = p->mc_lead_dz[3];
    int t16 = 2, t17 = 0, ne = 0, nx = 0;
    int u21 = nr, u22 = nf, ok = 1;
    if (0 < total) {
        short idx = (short)(total - 1);
        for (int sv = 1; ok && sv <= total; sv++, idx--) {
            int rough = u21 != 0;
            if ((short)u21 < 1) u22--; else u21--;
            double off = (double)(short)u21 * p->mc_mp_rs + D * 0.5 + (double)(short)u22 * p->mc_mp_fs;
            if (!taper) off += stock;
            RmB *pass = NULL;
            int onew = bv7 ? 1 : !p->ct_nolook;                  /* ctour_prm +0x10 */
            if (rm_ccomp_pock(bound, &pass, off, side, bv7 ? 3.141592653589793 : minfil, onew, 0, 0, &why)) { snprintf(err, errn, "Remachining: MCCECalcContourOffset port refused a pass offset: %s", why ? why : ""); ok = 0; break; }
            /* the gouge bound (l.58567-58572): ccomp_pock(pass, dVar1, side, minfil, 1, 0, 0), dVar1 = finish spacing (Multi Passes on and > nc_tol), else D x 0.01. Used only by the check, so made only with it on */
            RmB *ref = NULL; int gouge = leads && p->lead_gouge;
            if (gouge && pass) {
                double d1 = (mpon && p->mc_mp_fs > rm_nc_tol) ? p->mc_mp_fs : D * 0.01;
                if (rm_ccomp_pock(pass, &ref, d1, side, minfil, 1, 0, 0, &why)) { snprintf(err, errn, "Remachining: MCCECalcContourOffset port refused the gouge bound: %s", why ? why : ""); ok = 0; rm_b_free_list(&pass); break; }
                for (RmB *z = ref; z; z = z->next) { rm_b_info(z); rm_b_bbox(z); }
            }
            if (!p->rm_notrim || rough) {
                if (pass) {
                    RmB *pieces = NULL;
                    rm_trim(&pass, restb, &pieces);
                    rm_merge(&pieces);
                    for (RmB *q = pieces; q; q = q->next) {
                        RmB *wlead = NULL; int nn1 = 0, nn2 = 0, t1 = 0, t2 = 0;
                        if (leads && !bv7) { wlead = rm_add_leads(q, &L, side, bound, ref, gouge, &nn1, &nn2, &t1, &t2); ne = nn1; nx = nn2; t16 = t1; t17 = t2; }
                        rm_rem_add(wlead ? wlead : q, idx, &rem);
                        if (wlead) rm_b_free(wlead);
                    }
                    rm_b_free_list(&pieces);
                }
            } else {
                for (RmB *q = pass; q; q = q->next) {                /* untrimmed (ctour_prm +0x40): leads added whether or not Control (l.58600-58630: see report) */
                    RmB *wlead = NULL; int nn1 = 0, nn2 = 0, t1 = 0, t2 = 0;
                    if (leads) { wlead = rm_add_leads(q, &L, side, bound, ref, gouge, &nn1, &nn2, &t1, &t2); ne = nn1; nx = nn2; t16 = t1; t17 = t2; }
                    RmB *w2 = wlead ? wlead : q; w2->par = NULL;
                    rm_rem_add(w2, idx, &rem);
                    if (wlead) rm_b_free(wlead);
                }
            }
            rm_b_free_list(&pass); rm_b_free_list(&ref);
        }
    }
    rm_map_free(&cut); rm_map_free(&reach); rm_map_free(&rest2); rm_b_free_list(&restb);
    if (ok) {
        int cap = 0, n = 0; RmOut *O = NULL;
        for (RmRem *r = rem; r; r = r->next) r->active = 1;
        char cont = 0; double cur[2] = { 0, 0 };
        for (;;) {
            RmRem *g = NULL;
            rm_group(&cont, cur, rem, &g);
            if (!g) break;
            int pos = 0;
            for (RmRem *q = g; q; q = q->next) {
                pos++;
                if (n == cap) { cap = cap ? cap * 2 : 32; O = realloc(O, (size_t)cap * sizeof(RmOut)); }
                O[n].b = rm_b_copy(q->b); O[n].idx = q->idx; O[n].pos = pos; O[n].ne = ne; O[n].nx = nx; O[n].t16 = t16; O[n].t17 = t17; n++;
            }
            rm_rem_free(&g, 0);
        }
        *out = O; *nout = n;
    }
    rm_rem_free(&rem, 1);
    if (keep && taper && ok) *keep = tofree;                    /* local_260: this depth's offset chain, FUN_180050de0's chain at the Control write (l.58708) */
    else rm_b_free_list(&tofree);
    return ok;
}

/* The pass writer (the part of FUN_180056be0 / pass() Remachining uses; no tabs) for one piece, records from pock_bound_to_ctour (core l.370-484) */
static void rm_emit(MB *b, const CamParams *p, const RmB *pc, int ne, int nx, int t16, int t17, double z, int first, int fin, double hz_ref) {
    double xs[2], xe[2]; rm_gt_ends(&pc->e[0], xs, xe);
    double lz = ne > 0 ? pc->e[0].h : 0.0, zf = p->top_z + p->feed_plane, zr = fmax(zf, p->retract_z);
    if (p->hz_on && !isnan(hz_ref)) ht_rel(p, hz_ref, &zf, &zr, NULL);
    double lx = xs[0], ly = xs[1], feed = fin && p->mc_mp_on && p->mp_ff_on && p->mp_ff_v > 0 ? p->mp_ff_v : p->feed;
    int kd = 0;
    if (!first && p->kd_ok && g_pass_end >= 1 && g_pass_end <= b->n) {
        V3 e0 = b->m[g_pass_end - 1].p;
        if (hypot(lx - e0.x, ly - e0.y) > g_nc_tol && kd_link_ok(p, e0.x, e0.y, lx, ly)) {
            b->n = g_pass_end; kd = 1;                        /* the last pass's retract goes */
            mv(b, 0, lx, ly, e0.z, p->feed);
            if (e0.z > z + lz + 1e-9) mv(b, 0, lx, ly, z + lz, p->plunge_feed);
        }
    }
    if (!kd) {
        if (first) mv(b, 1, lx, ly, p->clearance, 0);
        else if (zr > zf) { if (p->hz_on && b->n) { V3 e = lastp(b); if (e.z < zr - CAM_SYS_TOL) mv(b, 1, e.x, e.y, zr, 0); } mv(b, 1, lx, ly, zr, 0); }
        mv(b, 1, lx, ly, zf, 0);
        mv(b, 0, lx, ly, z + lz, p->plunge_feed);
    }
    int c0 = b->n, N = pc->n;
    double lf = p->lead_fin_on && p->lead_fin_v > 0 && p->lead_on ? p->lead_fin_v : p->feed;
    double lo = p->lead_fout_on && p->lead_fout_v > 0 && p->lead_on ? p->lead_fout_v : p->feed;
    double cur_z = z + lz;
    for (int i = 0; i < N; i++) {
        const RmEnt *e = &pc->e[i];
        double s[2], en[2]; rm_gt_ends(e, s, en);
        int rec = i + 2;                                       /* pock_bound_to_ctour: entity i ends on record i + 2 */
        double f = rec < t16 ? lf : (rec > t17 ? lo : feed);   /* DAT_1804f16fc / 1700 */
        double ze = z;
        if (i < ne) ze = z + (i + 1 < ne ? pc->e[i + 1].h : 0.0);
        else if (i >= N - nx) ze = z + e->h;
        if (e->id == 'L') mv(b, 0, en[0], en[1], ze, f);
        else {
            double step = e->a[2] > p->tol ? 2 * acos(1 - p->tol / e->a[2]) : M_PI / 8; if (step > M_PI / 90) step = M_PI / 90;
            int k = (int)ceil(fabs(e->a[4]) / step); if (k < 2) k = 2;
            int k0 = b->n;
            for (int j = 1; j < k; j++) {
                double t = e->a[3] + e->a[4] * j / k, zz = cur_z + (ze - cur_z) * j / k;
                mv(b, 0, e->a[0] + e->a[2] * cos(t), e->a[1] + e->a[2] * sin(t), zz, f);
            }
            mv(b, 0, en[0], en[1], ze, f);
            if (fabs(ze - cur_z) < 1e-9) mb_tag_arc(b, k0, e->a[0], e->a[1], e->a[4] < 0);
        }
        cur_z = ze;
    }
    pass_cc_tag(b, p, c0, b->n - 1);
    g_pass_end = b->n; pass_retract(b, p, z, zr);
}

/* FUN_1800550c0 */
static int gen_remach(const McamFile *f, CamOp *op, MB *b, const CamParams *q, const double *zl, int nz) {
    CamParams pr = *q, *p = &pr;
    { const McSysTol *st = mc_systol_units(mcam_is_metric(f)); rm_tol = st->tol; rm_nc_tol = st->nc_tol; rm_chain_tol = st->chain_tol; }
    if (p->tabs > 0) { p->tabs = 0; p->ntab_xy = 0; }          /* the writer call of FUN_1800550c0 has no tabs (l.58763) */
    int leads = p->lead_on && (p->lead_in_on || p->lead_out_on);
    /* compensation Off (0x5d0 = 4): FUN_1800550c0 has no Off test - the passes are offset by D/2 to the side of 0x5d2 (rm_side) as with Computer, bVar7 is false
       (l.58328-58335: Control only), and the writer FUN_180056be0 gets no cutter comp codes (pass_cc below). The side is relative to the chain's direction, which is
       Mastercam's only when the chain records give it (cam_fileop.c side_rel). */
    if (p->side == SIDE_ON && !p->side_rel) { snprintf(op->error, sizeof op->error, "Remachining with compensation Off: the chain directions are not in the operation's chain records, so the side of record 0x5d2 cannot be placed (NOT PORTED)"); return 0; }
    if (p->rm_cell <= 0) { snprintf(op->error, sizeof op->error, "Remachining: the remachining tolerance (0x1278) must be > 0"); return 0; }
    int bv7 = p->side != SIDE_ON && p->ct_comp == 1 && p->tool_dia > rm_tol;   /* op +0x568 == 1 (l.58328): Off is 4 in the record (ct_comp keeps the last non-Off type here) */
    int ci_last = -1;
    for (int ci = 0; ci < op->nchains; ci++) if (op->chains[ci].n && !cam_chain_is_point(f, &op->chains[ci])) ci_last = ci;
    if (ci_last < 0) { snprintf(op->error, sizeof op->error, "no usable chains"); return 0; }
    for (int ci = 0; ci < op->nchains; ci++) {
        const Chain *ch = &op->chains[ci];
        if (cam_chain_is_point(f, ch)) { snprintf(op->error, sizeof op->error, "Remachining: a point in the chains (chains_to_pock_bounds rejects it here: NOT PORTED)"); return 0; }
        if (cam_chain_3d(f, ch)) { snprintf(op->error, sizeof op->error, "Remachining on a 3D chain: chain_to_pock_bound's projection is NOT PORTED"); return 0; }
        for (int i = 0; i < ch->n; i++) if (f->ents[ch->ent[i]].type != E_LINE && f->ents[ch->ent[i]].type != E_ARC) { snprintf(op->error, sizeof op->error, "Remachining: a spline in the chain (ccomp_pock handles only lines and arcs here)"); return 0; }
        if (ch->cut0 > 1e-12 || ch->cut1 > 1e-12) { snprintf(op->error, sizeof op->error, "Remachining: a chain with trimmed ends (NOT PORTED)"); return 0; }
    }
    int taper = ct_tapered(p), depth_on = p->mc_depth_on || p->stepdown > 1e-6;
    int bydepth = p->dc_bydepth && op->nchains > 1 && depth_on && nz > 1, nouter = bydepth ? nz : op->nchains, ninner = bydepth ? op->nchains : nz, any = 0;
    /* per chain: the polyline for keep-tool-down, and the pieces (non-tapered: computed once at the first depth, FUN_1800451f0 copies the NCI to the other depths; tapered: per depth) */
    typedef struct { RmB *bound; double *xy; int n; char side; RmOut **per; int *nper; RmB **tb; } C;   /* tb: tapered, each depth's offset chain (local_260) */
    C *cs = calloc((size_t)op->nchains, sizeof *cs);
    int ok = 1, n_actual = depth_on ? nz : 1;
    for (int ci = 0; ci < op->nchains && ok; ci++) {
        const Chain *ch = &op->chains[ci]; C *c = &cs[ci]; double zc;
        c->n = cam_chain_polyline(f, ch, p->tol, &c->xy, &zc);
        c->side = rm_side(p, ch, c->xy, c->n);
        c->bound = rm_chain_bound(f, ch);
        c->per = calloc((size_t)nz, sizeof *c->per); c->nper = calloc((size_t)nz, sizeof *c->nper); c->tb = calloc((size_t)nz, sizeof *c->tb);
        if (!taper) ok = rm_compute(f, op, p, c->bound, zc, zl[0], c->side, bv7, leads, &c->per[0], &c->nper[0], 1, n_actual, op->error, sizeof op->error, NULL);
        else for (int k = 0; k < nz && ok; k++) ok = rm_compute(f, op, p, c->bound, zc, zl[k], c->side, bv7, leads, &c->per[k], &c->nper[k], k == nz - 1, n_actual, op->error, sizeof op->error, bv7 ? &c->tb[k] : NULL);
    }
    int hz_ci = -1; double hz_z = 0;
    RmLeads Lb; memset(&Lb, 0, sizeof Lb);
    {   double sw_in = p->lead_sw > 0 ? p->lead_sw : ((p->lead_sw_rec & 1) ? 0.0 : M_PI / 2), sw_out = p->lead_xsw > 0 ? p->lead_xsw : ((p->lead_sw_rec & 2) ? 0.0 : M_PI / 2);
        Lb.r_in = p->lead_r; Lb.sw_in = sw_in; Lb.len_in = p->lead_len; Lb.perp_in = p->lead_in_perp; Lb.dz_in_line = p->mc_lead_dz[0]; Lb.dz_in_arc = p->mc_lead_dz[1];
        Lb.r_out = p->lead_xr; Lb.sw_out = sw_out; Lb.len_out = p->lead_xlen; Lb.perp_out = p->lead_out_perp; Lb.dz_out_line = p->mc_lead_dz[2]; Lb.dz_out_arc = p->mc_lead_dz[3]; }
    pass_cc = p->ct_comp >= 1 && p->ct_comp <= 3 && p->side != SIDE_ON;
    for (int o = 0; ok && o < nouter; o++) for (int in = 0; ok && in < ninner; in++) {
        int ci = bydepth ? in : o, kz = bydepth ? o : in;
        C *c = &cs[ci];
        const Chain *ch = &op->chains[ci];
        RmOut *O = c->per[taper ? kz : 0]; int nO = c->nper[taper ? kz : 0];
        int last_depth = taper ? kz == nz - 1 : 1;              /* sVar9 == local_2b4: without taper the chain's single processed depth is always "the last"; FUN_1800451f0 copies every written record */
        for (int j = 0; j < nO; j++) {
            /* l.58666-58675: the filter of finish passes at the non-last depths */
            if (!(last_depth || p->ct_fin_all || p->mc_mp_nf <= O[j].idx)) continue;
            int fin = p->mc_mp_on && O[j].pos > p->mc_mp_nr;
            pr.kd_ok = (O[j].pos >= 2 && p->mc_mp_on && p->mc_mp_kd) || (n_actual >= 2 && p->mc_depth_on && p->mc_keep_down);
            g_kd_xy = c->xy; g_kd_n = c->n; g_kd_closed = ch->closed;
            pass_chain = ch; pr.lead_right = c->side == 'R';      /* pass_cc_tag (G41 left of travel) */
            pr.pass_last_depth = kz == nz - 1;
            pr.hz_on = (p->ht_inc & 7) != 0; pr.hz_in = hz_ci == ci ? hz_z : NAN;
            pr.hz_last = bydepth ? 1 : (kz == nz - 1);
            pr.hz_opend = kz == nz - 1 && ci == ci_last;
            RmB *pc = O[j].b, *tmp = NULL, *tmp2 = NULL; int ne = O[j].ne, nx = O[j].nx, t16 = leads ? O[j].t16 : 2, t17 = leads ? O[j].t17 : pc->n + 1;
            if (bv7) {                                          /* Control (l.58691-58714): the tool centre path back on the programmed path, the leads rebuilt on it */
                const char *why = NULL; char opp = c->side == 'R' ? 'L' : 'R';
                if (rm_ccomp_pock(pc, &tmp, p->tool_dia * 0.5, opp, 3.141592653589793, 1, 0, 0, &why)) { snprintf(op->error, sizeof op->error, "Remachining: MCCECalcContourOffset port refused the Control back-offset: %s", why ? why : ""); ok = 0; break; }
                if (!tmp) break;                                /* the group ends (l.58717: a NULL piece) */
                if (leads) {
                    tmp2 = rm_add_leads(tmp, &Lb, c->side, taper ? c->tb[kz] : c->bound, NULL, p->lead_gouge, &ne, &nx, &t16, &t17);   /* FUN_180050de0 l.58708: the gouge bound was freed (NULL, l.58622-58623), the check's chain tests still run on local_260 (the depth's offset chain with taper, l.58431-58435) */
                    pc = tmp2;
                } else { pc = tmp; ne = nx = 0; t16 = 2; t17 = pc->n + 1; }
            } else if (!leads) { ne = nx = 0; t16 = 2; t17 = pc->n + 1; }
            ps_mark();
            rm_emit(b, p, pc, ne, nx, t16, t17, zl[kz], !any, fin, pr.hz_in);
            any = 1; hz_ci = ci; hz_z = zl[kz];
            if (tmp) rm_b_free_list(&tmp);
            if (tmp2) rm_b_free(tmp2);
        }
    }
    pass_chain = NULL; g_kd_xy = NULL; g_kd_n = 0; pass_cc = 0;
    for (int ci = 0; ci < op->nchains; ci++) {
        free(cs[ci].xy); rm_b_free(cs[ci].bound);
        if (cs[ci].per) { for (int k = 0; k < nz; k++) if (cs[ci].per[k]) rm_outs_free(cs[ci].per[k], cs[ci].nper[k]); free(cs[ci].per); free(cs[ci].nper); }
        if (cs[ci].tb) { for (int k = 0; k < nz; k++) rm_b_free_list(&cs[ci].tb[k]); free(cs[ci].tb); }
    }
    free(cs);
    if (!ok) return 0;
    if (!any) { snprintf(op->error, sizeof op->error, "Remachining: no rest material found to cut with this tool"); return 0; }
    V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance, 0);
    return 1;
}
