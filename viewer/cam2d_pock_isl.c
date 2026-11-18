/* cam2d_pock_isl.c - Pocket type 2 "Island facing" (record 0x13fa = pock_prm +0x1d2 = 2), included by cam2d_pock.c. Line numbers: decomp/mcmill_all.c;
 * mccore functions: decomp/mccore_pock*.c, decomp/pocket_b6/pspiral_decomp_merge.c. The decode (with what stays refused) is in
 * formats/pocket_engine_b6.md, "Island facing". No stored op of this type exists in the corpus: unverified against Mastercam.
 *
 * GeneratePocketToolpath FUN_1801c5250 for pocket type 2 (l.313559-313736):
 *   - depth cuts on: each depth is generated on its own (pock_proc_by_depth = 1, l.313630-313642, not the Standard copies of wr_pock_depth_cuts);
 *     before each depth FUN_1801c7970 (l.314958) unlinks the islands whose top (island chain Z + 0x1398 stock above islands) is below the depth + tol;
 *   - the pocket is cut at the depth as a Standard pocket (process_pocket l.313707), then FUN_1801c68e0 (l.314310) faces the tops:
 *     every island whose top lies strictly between this depth + tol and the previous depth (the first: top of stock) gets its own pocket at the top Z:
 *     comp_pock_bnd(island, R - 0x1388 overlap, flag 1: inward) = local_res8, the tool-centre bound of the top;
 *     FUN_1801c7350: the check bounds = the other bounds (the outer bound and the islands strictly higher in Z) offset by R + wall stock + finish passes x spacing;
 *     FUN_1801c6870: is the top bound nested in / overlapping a check outer bound, else the island is not faced;
 *     merge_pock_bounds_facing_islands (FUN_1805e02e0 param 1, FUN_1805dec50 param 1: pieces kept only when inside every outer bound they share a box with and in no island),
 *     then the islands and the loops that do not meet the top bound are unlinked (l.314375-314380);
 *     process_pocket(.., 0, 0, ..) on that list with roughing only (param_11 = 0): the normal roughing engines (no finish allowance: FUN_1801ede50 l.339387, FUN_1801ef830 l.340290);
 *     the entry FUN_1801bfd60 -> FUN_1801c1fc0 (l.311434) on the approach distance 0x1390, the exit FUN_1801c0190 l.310361 -> FUN_1801c1fc0 on the exit distance 0x13a0.
 *   - sections: the top sections are written with the facing flag (bnci_wr_pock_marker: decomp/nciw/core_wr.c l.2489-2497) and sort after the depth they belong to (pk_sec_cmp key 8, the sort depth pock_facing_sort_depth = the depth, l.314335);
 *     set_heights(top Z, last_top_of_stock) (l.314395) for the top sections, set_heights(depth, top_of_stock of the depth) for the others (by depth: the previous depth, adjust_depths mccore_depths.c l.~230).
 *
 * merge_pock_bounds_facing_islands is the port rm_merge_pock (cam2d_remach.c, FUN_1805e02e0, checked against the DLL by tools/tests/isl_face_oracle.sh). Ours, a substitute: the check bound offsets through gls_offset (MCCECalcOffset, the documented substitute for every Pocket bound). */

static double pk_isl_stk(const CamParams *p) { return pkd(p, 0x1398); }   /* FUN_1801c7970 l.314966 / FUN_1801c68e0 l.314346: pock_prm +0x170 */

/* check_chains_3d_view mccore_chain_range.c l.6120-6232 (refused 0x2309): each line's ends within chain_plane_tol (config 1002) of the first entity's Z (branch CHAIN +0xc = 0; the port has no other branch, a flat chain has this flag clear);
   arcs compare the centre Z (the second point local_29a8 of an arc record: unsettled, formats/pocket_engine_b6.md) and need a view parallel to the op (e->planar, views_parallel l.6230). The bound's Z +0x18 = the chain's Z (chains_to_pock_bounds): per region loop, parallel to pk_region's loops. 0 = a chain not flat */
static double pk_isl_vz(const CamParams *p, V3 q) {                                  /* world_to_view(param_4 = the op's view) */
    if (cam_op_top(p)) return q.z;
    double w[3] = { q.x, q.y, q.z }, v[3]; cam_plane_w2v(p->tplane, p->torg, w, v); return v[2];
}
static int pk_isl_zs(const McamFile *f, const CamOp *op, double *zs, int cap, int *nzs, int all) {   /* all: the open chains too (check_chains_3d l.313530 runs for types 3 / 4 on every chain, l.313523-313527); a closed chain pk_region drops is not counted, so that zs follow its list */
    int m = 0, flat = 1; double t = cam_chain_plane_tol; const CamParams *p = &op->p;
    for (int i = 0; i < op->nchains; i++) {
        const Chain *ch = &op->chains[i];
        if ((!ch->closed && !all) || ch->avoid) continue;
        if (ch->closed) { Gl t; if (!pk_chain_gl(f, ch, &t)) continue; gl_free(&t); }
        int got = 0; double z0 = 0;
        for (int j = 0; j < ch->n; j++) {
            const Ent *e = &f->ents[ch->ent[j]];
            if (e->type == E_LINE) { double za = pk_isl_vz(p, e->a), zb = pk_isl_vz(p, e->b); if (!got) { z0 = za; got = 1; } if (fabs(za - z0) > t || fabs(zb - z0) > t) flat = 0; }
            else if (e->type == E_ARC) { double zc = pk_isl_vz(p, e->c); if (!got) { z0 = zc; got = 1; } if (fabs(zc - z0) > t || (cam_op_top(p) && !e->planar)) flat = 0; }
        }
        if (m < cap) zs[m] = z0;
        m++;
    }
    *nzs = m;
    return flat;
}

/* the depth's region: FUN_1801c7970 (l.314975, only when depth cuts are on, l.313670) unlinks the islands whose top is below the depth + tol */
static Gls pk_isl_region(const CamParams *p, const Gls *reg, const double *zs, double depth, int depth_on, double **zs_out) {
    Gls r = { 0 }; double stk = pk_isl_stk(p);
    double *z2 = malloc(sizeof(double) * (size_t)(reg->n ? reg->n : 1)); int m = 0;
    for (int i = 0; i < reg->n; i++) {
        if (depth_on && reg->l[i].island && stk + zs[i] < depth + CAM_SYS_TOL) continue;
        z2[m++] = zs[i];
        gls_push(&r, gl_copy(&reg->l[i]));
    }
    *zs_out = z2;
    return r;
}

/* pt_in_any_machinable_area (mccore 1805e6d50): in one of the non-island bounds (on counts, pt_in_ctour != -1) and not strictly inside an island; pt_in_ctour = rm_pt_in_ctour */
static int pk_isl_in_any(const Gls *B, PV q, double nct, double tol) {
    int in = 0;
    for (int i = 0; i < B->n; i++) {
        RmB *b = pk_gl2rb(&B->l[i]); int s = rm_pt_in_ctour(b, q.x, q.y, nct, tol); rm_b_free(b);
        if (!B->l[i].island) { if (s != -1) in = 1; }
        else if (s == 1) return 0;
    }
    return in;
}
/* pock_bound_overlap (mccore 1805e58f0): 100 horizontal scan lines across the common Y range, inset by t; on each, the chords of a and b in order along the line: true when one pair of chords overlaps by more than t */
static int pk_isl_overlap(const Gl *a, const Gl *b, double t) {
    double ba[4], bb[4]; gl_bbox(a, ba); gl_bbox(b, bb);
    double y0 = (ba[1] < bb[1] ? bb[1] : ba[1]) + t, y1 = (bb[3] <= ba[3] ? bb[3] : ba[3]) - t;
    if (!(y0 < y1)) return 0;
    double dy = y1 - y0; ZB zb[2]; memset(zb, 0, sizeof zb); zb[0].l = *a; zb[1].l = *b;
    ZBs B = { zb, 2 };
    ZI *r = malloc(sizeof(ZI) * 1002); int ok = 0;
    for (int k = 0; k < 0x65 && !ok; k++, y0 += dy / 99.0) {
        PV l0 = pv(0, y0), l1 = pv(100, y0); int n = 0;
        zz_bound_line(&B, 0, l0, l1, 500, r, &n);
        int n0 = n;
        if (n == 0 || (n & 1)) continue;
        zz_bound_line(&B, 1, l0, l1, 500, r, &n);
        if (n == n0 || (n & 1)) continue;
        int i = 0, j = 0;
        for (;;) {
            int advA = i == 0 || i < j, advB = j == 0 || j < i;
            if (!advA) break;
            int a1 = i;
            do { if (n < ++a1) goto nextline; } while (r[a1].bnd != 0);
            i = a1;
            do { if (n < ++i) goto nextline; } while (r[i].bnd != 0);
            int b1 = j;
            if (!advB) goto out;
            do { if (n < ++b1) goto out; } while (r[b1].bnd != 1);
            j = b1;
            do { if (n < ++j) goto out; } while (r[j].bnd != 1);
            double d18 = r[b1].p.x - r[i].p.x, d0 = r[j].p.x - r[a1].p.x;   /* the records' x (l.5e70: +8 = the point) */
            if (d18 * d0 < 0.0) { double m = fabs(d0); if (fabs(d18) <= m) m = fabs(d18); if (t < m) { ok = 1; goto out; } }
        }
        nextline:;
    }
    out:
    free(r);
    return ok;
}
/* is_ctour_nested_or_intersecting (mccore 1805e4b40, a = the top bound, c = a check outer bound): crossing: pock_bound_overlap with tol x 2; else every entity start of the top bound in or on c */
static int pk_isl_nested(const Gl *a, const Gl *c, double chtol, double nct) {
    RmB *ra = pk_gl2rb(a), *rc = pk_gl2rb(c); int res = 1;
    if (rm_bound_intersect(ra, rc)) res = pk_isl_overlap(a, c, chtol * 2.0);
    else for (int i = 0; i < a->n && res; i++) { PV s = gt_start(&a->e[i]); if (rm_pt_in_ctour(rc, s.x, s.y, nct, rm_tol) == -1) res = 0; }
    rm_b_free(ra); rm_b_free(rc);
    return res;
}
/* FUN_1801d5870 l.323721 on the list before the merge: the direction by climb (pock_prm +0x12) and island, reversed for methods 2 / 3 / 8 when not inside to outside (+0x30); reverse_pock_bound keeps the first entity unless method 4 or 0x1269 */
static void pk_isl_orient(const CamParams *p, RmB *b) {
    int method = pks(p, 0x123c), want = b->isl == 0 ? pkb(p, 0x123a) != 0 : pkb(p, 0x123a) == 0;
    if ((method == 2 || method == 3 || method == 8) && pkb(p, 0x1258) == 0) want = !want;
    if (want != (int)b->ccw) rm_b_reverse(b, !(method == 4 || pkb(p, 0x1269)));
}

/* FUN_1801c68e0 (l.314310) for one chain pocket (outer loop oi of the depth's region and its islands): the islands' top pockets, appended to *pk. 0 + op->error when refused */
static int pk_isl_faces(CamOp *op, const Gls *reg, const double *zs, int oi, int sk, int gi, double zd, double zprev, double top_s, double minfil, double tol, PkPocket **pk, int *np, int *capp) {
    const CamParams *p = &op->p; double R = p->tool_dia * 0.5, nct = g_nc_tol, stk = pk_isl_stk(p);
    double fa = p->finish_n > 0 ? p->finish_n * p->finish_step : 0;                 /* l.314338: finish on (+0x11): + passes (+0x32) x spacing (+0x38) */
    double offs = R + p->stock_xy + fa, ov = pkd(p, 0x1388), chtol = pkd(p, 0x1228);   /* FUN_1801c68e0 param_4 = R + wall stock (dVar3 l.313551), pock_prm +0x160 overlap, +0 tolerance */
    Gls one = { &reg->l[oi], 1, 1 };
    for (int ib = 0; ib < reg->n; ib++) {
        if (!reg->l[ib].island || ib == oi || !gls_inside(&one, gt_pt(&reg->l[ib].e[0], 0), tol)) continue;
        double zt = stk + zs[ib];                                                     /* dVar8 l.314346 */
        if (!(zd + CAM_SYS_TOL < zt && zt < zprev)) continue;                         /* l.314347: param_2 = depth + tol, param_3 = previous depth */
        RmB *irb = pk_gl2rb(&reg->l[ib]), *ro = NULL; const char *why = NULL;       /* comp_pock_bnd(island, R - overlap, 1) (mccore_pock2.c l.29): ccomp_pock, side 'L' when ccw else 'R' (inward) */
        char side = irb->ccw ? 'L' : 'R';
        if (rm_ccomp_pock(irb, &ro, R - ov, side, minfil, 1, 1, 0, &why)) { rm_b_free(irb); snprintf(op->error, sizeof op->error, "pocket type Island facing: the top bound offset: %s", why ? why : "failed"); return 0; }
        rm_b_free(irb);
        if (!ro) continue;                                                                /* l.314351: nothing left */
        if (ro->next) { rm_b_free_list(&ro->next); ro->next = NULL; }                     /* l.314352-314353: the first of ccomp_pock's list (unsettled item 1: rm_ccomp_pock keeps MCCE's order, checked against the DLL) */
        ro->isl = 0; ro->prev = NULL; rm_b_info(ro); rm_b_bbox(ro);
        Gl res8 = pk_rb2gl(ro); res8.island = 0;
        /* FUN_1801c7350 (make_face_isl_check_bounds, l.314652): the other bounds: the outer, and the islands with a greater Z (strictly) */
        Gls src = { 0 };
        for (int j = 0; j < reg->n; j++) {
            if (j == ib) continue;
            if (reg->l[j].island && !(zs[ib] <= zs[j] && zs[j] != zs[ib])) continue;
            if (j != oi && !reg->l[j].island) continue;                                /* one chain pocket: the outer of this pocket only */
            if (reg->l[j].island && !gls_inside(&one, gt_pt(&reg->l[j].e[0], 0), tol)) continue;
            gls_push(&src, gl_copy(&reg->l[j]));
        }
        Gls chk = gls_offset(&src, -offs, minfil, tol); gls_free(&src);
        int nest = 0;
        for (int c = 0; c < chk.n && !nest; c++) if (!chk.l[c].island) nest = pk_isl_nested(&res8, &chk.l[c], chtol, nct);   /* FUN_1801c6870 l.314356 */
        if (nest) {
            RmB *res10 = rm_b_copy(ro), *list = ro;                                     /* l.314358: local_res10 before the check bounds are linked */
            for (int c = 0; c < chk.n; c++) { RmB *cb = pk_gl2rb(&chk.l[c]); cb->next = list; list->prev = cb; list = cb; }   /* l.314370: link_pock_bound(&local_res8, copy, local_res8): each before the head */
            for (RmB *x = list; x; x = x->next) pk_isl_orient(p, x);                    /* FUN_1801d5900 l.314372 */
            rm_merge_pock(&list, 1);                                                    /* merge_pock_bounds_facing_islands l.314373 */
            Gls keep = { 0 };
            for (RmB *x = list; x; x = x->next) if (!x->isl && rm_bound_intersect(x, res10)) { Gl g = pk_rb2gl(x); if (gl_area(&g) < 0) gl_reverse(&g); g.island = 0; gls_push(&keep, g); }   /* l.314375-314380; the roughing engines of this port take counter-clockwise loops */
            rm_b_free_list(&list); rm_b_free(res10); ro = NULL;
            for (int k = 0; k < keep.n; k++) {
                if (*np == *capp) { *capp = *capp ? *capp * 2 : 8; *pk = realloc(*pk, (size_t)*capp * sizeof(PkPocket)); }
                PkPocket *q = &(*pk)[(*np)++]; memset(q, 0, sizeof *q);
                gls_push(&q->tc, gl_copy(&keep.l[k]));
                q->rb = gls_copy(&q->tc);
                q->chk = gls_copy(&chk);
                q->face = 1; q->zi = gi; q->sk = sk; q->sub = k; q->ztop = zt; q->top_s = top_s;   /* marker l.339443: sVar16 + param_7 (the machine_pocket pocket index, D:1801d3963), from 0 in this process_pocket */
            }
            if (getenv("PK_DEBUG")) fprintf(stderr, "island facing: bound %d top %.4f (depth %.4f): %d loop(s)\n", ib, zt, zd, keep.n);
            gls_free(&keep);
        }
        else if (getenv("PK_DEBUG")) fprintf(stderr, "island facing: bound %d top %.4f (depth %.4f): not nested in a check bound (FUN_1801c6870), not faced\n", ib, zt, zd);
        if (ro) rm_b_free(ro);
        gls_free(&chk); gl_free(&res8);
    }
    return 1;
}

/* FUN_1801c1fc0 (l.311434): the point outside the top pocket's bound: the first non-island bound offset outward by dist (ccomp_pock, 'R' when ccw); spiral methods the closest point of it to S
   (proj_pt_to_pock_bound), others the nearest intersection of the +X line through S (in the frame of the roughing angle, l.311498-311583); with check bounds it must be in them (pt_in_any_machinable_area),
   else the nearest entity start of the offset bound that is (l.311593-311622). 1 = found (world coordinates) */
static int pk_isl_out(const CamParams *p, const Gl *outer, const Gls *chk, PV S, double dist, double minfil, double tol, PV *Q) {
    (void)tol;
    RmB *orb = pk_gl2rb(outer), *oro = NULL; const char *why = NULL;                     /* ccomp_pock(bound, dist, ccw ? 'R' : 'L', min_fil, 1, 1, 0) l.311493: outward */
    int rc = rm_ccomp_pock(orb, &oro, dist, orb->ccw ? 'R' : 'L', minfil, 1, 1, 0, &why); rm_b_free(orb);
    if (rc || !oro) { if (oro) rm_b_free_list(&oro); return 0; }
    Gls o = { 0 }; gls_push(&o, pk_rb2gl(oro)); o.l[0].island = 0; rm_b_free_list(&oro);
    Gl *ol = &o.l[0]; int found = 0; PV best = pv(0, 0); double nct = g_nc_tol;
    if (pk_spiral_cutting(p)) { PV q; gl_closest(ol, S, NULL, NULL, &q); best = q; found = 1; }   /* proj_pt_to_pock_bound */
    else {
        double ang = pkd(p, 0x1250), ca = cos(-ang), sa = sin(-ang);                    /* rough_angle (radians), the frame pock_bound_line_inters works in (rotate_pock_bounds l.314400) */
        ZB zb; memset(&zb, 0, sizeof zb);
        for (int k = 0; k < ol->n; k++) gl_push(&zb.l, zz_rot_gt(&ol->e[k], ca, sa, -ang));
        ZBs B = { &zb, 1 }; ZI *r = malloc(sizeof(ZI) * 1002); int n = 0;
        PV Sr = pv(S.x * ca - S.y * sa, S.x * sa + S.y * ca);
        zz_inters(&B, Sr, pv(Sr.x + 100.0, Sr.y), CAM_SYS_TOL, 20, r, &n);
        double bd = 1e38;
        for (int i = 1; i <= n; i++) {
            double d = sqrt((r[i].p.x - Sr.x) * (r[i].p.x - Sr.x) + (r[i].p.y - Sr.y) * (r[i].p.y - Sr.y));
            if (d < bd || !found) { bd = d; found = 1; best = r[i].p; }
        }
        free(r); gl_free(&zb.l);
        if (found) best = pv(best.x * ca + best.y * sa, -best.x * sa + best.y * ca);      /* back to world */
    }
    if (chk && chk->n) {
        if (found && pk_isl_in_any(chk, best, nct, CAM_SYS_TOL)) { *Q = best; gls_free(&o); return 1; }
        found = 0; double bd = 0;
        for (int i = 0; i < ol->n; i++) {
            PV s = gt_start(&ol->e[i]);
            if (pk_isl_in_any(chk, s, nct, CAM_SYS_TOL)) {
                double d = sqrt((S.y - s.y) * (S.y - s.y) + (S.x - s.x) * (S.x - s.x));
                if (!found || d < bd) { found = 1; bd = d; best = s; }
            }
        }
    }
    if (found) *Q = best;
    gls_free(&o);
    return found;
}

/* FUN_1801c0190 l.310361 (type 2 + facing): exit distance 0x13a0 > 1e-12: FUN_1801c1fc0 from the current position, the move FUN_1801e0f80 with the check bounds. 0 + err when the straight move leaves them */
static int pk_isl_exit(MB *b, const CamParams *p, PkPocket *q, double depth, double zf, double minfil, double tol, char *err, size_t errn) {
    double d = pkd(p, 0x13a0); if (!(1e-12 < d)) return 1;
    V3 e = lastp(b); PV s = pv(e.x, e.y), Q; int i = 0;
    while (i < q->rb.n && q->rb.l[i].island) i++;
    if (i >= q->rb.n) return 1;
    if (!pk_isl_out(p, &q->rb.l[i], &q->chk, s, d, minfil, tol, &Q)) return 1;
    if (!pk_line_inside(&q->chk, s, Q, g_nc_tol)) { snprintf(err, errn, "pocket type Island facing: the exit move leaves the check bounds (FUN_1801e0f80 find_path_around_island NOT DECODED)"); return 0; }
    pk_e0f80(b, p, &q->chk, s, Q, depth, zf, g_nc_tol);
    return 1;
}

/* the heights of a section (set_heights mccore 18089fa80, param_1 = the depth, param_2 = the section's top): feed plane incremental on the top, not below the depth; retract / clearance incremental on the top (as cam2d.c ht3).
   by depth (Island facing only): each depth's top of stock is the previous depth (adjust_depths), the top sections take last_top_of_stock (l.314394) */
static const CamParams *pk_isl_sec_params(const CamParams *p, CamParams *sp, double top_s, double depth) {
    (void)depth;                                                                      /* the floor of the feed plane: pk_h (set_heights), not stored here */
    *sp = *p;
    sp->top_z = top_s;
    sp->feed_plane = (p->ht_inc & 4) ? p->feed_plane : (p->top_z + p->feed_plane) - top_s;
    if ((p->ht_inc & 2) && p->retract_z > -1e29) sp->retract_z = top_s + (p->retract_z - p->top_z);
    if (p->mc_clr_on && (p->ht_inc & 1)) sp->clearance = top_s + (p->clearance - p->top_z);   /* set_heights tests record 0x179 first (I-C3) */
    return sp;
}
