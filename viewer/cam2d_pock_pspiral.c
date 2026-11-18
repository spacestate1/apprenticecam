/* cam2d_pock_pspiral.c - Pocket roughing cutting methods 2 Parallel Spiral and 3 Parallel Spiral Clean Corners
 * (mcmill Pockparl.cpp), included by cam2d_pock.c before gen_pocket_v2. Line numbers: decomp/mcmill_all.c.
 *
 * FUN_1801ede50 l.339319 (spiral rough), case 2 / 3 (l.339480): pock_clean_corners = (method == 3), the pocket's rough
 * boundary list is copied (copy_pock_bounds, the copy = the boundary used for links / corner checks) and
 * FUN_1801ea750 (l.337176) cuts it; its point / "a loop was cut" arguments are ede50 locals reset to 0 before every
 * pocket (disassembly 1801ee1a0 MOV byte [RSP+0x60],0 / call 1801ee21c, Ghidra lost the register arguments).
 *
 * FUN_1801ea750, per pocket (pk_ps_loops):
 *   - every loop's +0x45 flag (offset-produced, used by clean corners) is cleared (l.337214);
 *   - repeat: each non-island loop of the current list is cut (in2out 0x1258 = 0: FUN_1801e9a90 at once) or written to
 *     the temp file (0x1258 = 1: FUN_1801eb050, a backward-linked record list); the start of its first entity is kept
 *     (get_ep(.., 1) = the start, mccore get_ep);
 *   - FUN_1801ea5d0 (l.337118): every non-island loop is offset inward by the stepover 0x1248 (pock_prm +0x18) with
 *     ccomp_pock (min fillet angle min(min_fil, 0.785398), l.337262), halved once when the offset fails / is empty; the
 *     new loops get +0x45 = 1; loops whose |area| exceeds their parent's are dropped (FUN_1801ea3b0);
 *   - the old non-island loops are deleted; of the new loops the first goes into the current list, every other one is
 *     linked at the head of the waiting list (link_pock_bound(list, b, head) inserts before the head, mccore_pock2.c
 *     l.137); then merge_pock_bounds (the islands stay in the list and are merged with the new loop);
 *   - no non-island loop left: no waiting loop -> done; else FUN_1801ea450 (l.337034) takes the waiting loop with the
 *     entity start nearest the last kept start (strict <, first wins), reordered to start at that entity
 *     (reorder_pock_bound), and merges it with the islands (link_pock_bound + merge_pock_bounds);
 *   - 0x1258 = 1: the records are read back from the last written (FUN_1801eaca0, +0x58 = the previous record), so the
 *     cut order is the reverse of the generation order (inside to outside), each cut by FUN_1801e9a90 / e9ee0.
 *   merge_pock_bounds keeps +0x45 of the outer loop on the merged loop (mccore FUN_1805de250 / FUN_1805dfc50, dumped to
 *   decomp/pocket_b6/pspiral_decomp_merge.c: ppVar12[0x45] = local_a8[0x45], local_180[0x45] = param_2[0x45]); so +0x45 = 0 for the pocket's
 *   own rough boundary loops and 1 for every loop made by the stepover offset.
 *
 * FUN_1801e9a90 (l.336573) / FUN_1801e9ee0 (l.336785, when 0x1259 Minimize tool burial is on) cut one loop:
 *   - start entity: the first loop of a pocket ("a loop was cut" = 0, no user-defined entry point) entity 0; later
 *     loops the entity whose start is nearest the current position (strict <, first wins; no split);
 *   - first loop: FUN_1801c3b20 -> FUN_1801bfcb0 -> FUN_1801bc830 entry at the start (pock_prev_pt_ok = 0 at a new
 *     pocket, machine_pocket l.322563) = the existing helix / plunge of gen_pocket_v2;
 *   - later loops: the link from the current position to the start: e9a90 FUN_1801e0f80 (pock_move_avoid_islands, as
 *     the High Speed port: a straight feed when FUN_1801e0fd0 holds, its path around islands NOT DECODED -> the High
 *     Speed port's retract / re-plunge); e9ee0 first tries FUN_1801dae10 = the walk along the medial axis
 *     (POCKMDL.cpp FUN_1801d9f60 / FUN_1801daeb0, ported in cam2d_pock_medax.c; the axis is built per pocket by
 *     gen_pocket_v2 as machine_pocket does) and falls back to FUN_1801e0f80 when it fails 8 times;
 *   - each entity as a line / arc; after it, when clean corners is on and the loop's +0x45 is set,
 *     FUN_1801e9420 (pk_ps_corner) for the corner to the next entity (not after the last entity).
 */

/* FUN_1801eada0 (l.337401): Q moved to the intersection of the line P->Q (pock_bound_line_inters: the infinite line
   through P and Q with every bound entity; lines with entity parameter in (-2e-7, 1.0000002), arcs pt_on_arc nc_tol,
   bound_line_inters mccore_pock.c l.4094) nearest P among those closer to P than the current Q and closer to Q than
   |PQ|; ok when Q and the midpoint of PQ are in the machinable area (pt_in_machinable_area, nc_tol: on a bound is
   fine = pk_pt_ok) */
static int pk_ps_clip(const Gls *bounds, PV P, PV *Q, double nctol) {
    PV q0 = *Q; double L = hypot(q0.x - P.x, q0.y - P.y), best = L;
    PV d = pv(q0.x - P.x, q0.y - P.y);
    for (int i = 0; i < bounds->n; i++) for (int k = 0; k < bounds->l[i].n; k++) {
        const Gt *e = &bounds->l[i].e[k]; PV x[2]; int nx = 0;
        if (e->t == 'L') {
            PV u = pv(e->b.x - e->a.x, e->b.y - e->a.y); double den = d.x * u.y - d.y * u.x;
            if (fabs(den) < 1e-15) continue;
            double w = ((e->a.x - P.x) * d.y - (e->a.y - P.y) * d.x) / den;   /* parameter on the entity */
            if (w > -2e-7 && w < 1.0000002) x[nx++] = pv(e->a.x + u.x * w, e->a.y + u.y * w);
        } else if (e->t == 'A') {
            double dl = hypot(d.x, d.y); if (dl < 1e-15) continue;
            PV un = pv(d.x / dl, d.y / dl), w = pv(P.x - e->c.x, P.y - e->c.y);
            double bb = w.x * un.x + w.y * un.y, disc = bb * bb - (w.x * w.x + w.y * w.y - e->r * e->r);
            if (disc < 0) continue;
            double sq = sqrt(disc);
            for (int s = -1; s <= 1; s += 2) {
                double t = -bb + s * sq; PV y = pv(P.x + un.x * t, P.y + un.y * t);
                double a = atan2(y.y - e->c.y, y.x - e->c.x);
                double rel = e->sw > 0 ? fmod(fmod(a - e->sa, 2 * M_PI) + 2 * M_PI, 2 * M_PI) : fmod(fmod(e->sa - a, 2 * M_PI) + 2 * M_PI, 2 * M_PI);
                double over = rel - fabs(e->sw);
                if (rel <= fabs(e->sw) || over * e->r <= nctol || (2 * M_PI - rel) * e->r <= nctol) x[nx++] = y;
                if (disc == 0) break;
            }
        }
        for (int j = 0; j < nx; j++) {
            double dp = hypot(x[j].x - P.x, x[j].y - P.y), dq = hypot(x[j].x - q0.x, x[j].y - q0.y);
            if (dp < best && dq < L) { *Q = x[j]; best = dp; }
        }
    }
    PV m = pv((P.x + Q->x) * 0.5, (P.y + Q->y) * 0.5);
    return pk_pt_ok(bounds, *Q, nctol) && pk_pt_ok(bounds, m, nctol);
}

/* FUN_1801e9420 (l.336373) clean_out_corner at P = the end of e / start of f: when the turn is sharper than 30 deg
   (t_in . t_out <= 0.866): d = unit(t_in - t_out), s = cross(t_in, d), the corner point Q = P + d (1 / |s| - 1) D / 2
   (cur_op +0x2e8 = tool diameter), clipped by FUN_1801eada0; when ok, not the loop's last entity and 0.0001 < |s|: up to
   10 tries (dist x 0.8 after each try whose loop crosses the bounds): h = dist |s| / (|s| + 1), c = dist - h,
   k = sqrt(c^2 - h^2); A = P + t_in k, B = P - t_out k, centre C = P + d c; the loop line P->A, arc A->B about C
   (clockwise when cross(t_in, t_out) > 0, constr_ns_arc), line B->P (FUN_1801e2270 / e1cb0 / e2270). Crossing test
   pock_bounds_intersect = transversal crossings (gt_cross, as the High Speed port's FUN_1801d2c60). */
static void pk_ps_corner(MB *b, const Gls *bounds, PV P, const Gt *e, const Gt *f, int last, double D, double z,
                         double feed, double ctol, double nctol) {
    PV ti = gt_tan(e, 1), to = gt_tan(f, 0);
    if (to.x * ti.x + to.y * ti.y > 0.866) return;
    PV d = pv(ti.x - to.x, ti.y - to.y); double dl = hypot(d.x, d.y);
    if (dl == 0) d = pv(0, 0); else d = pv(d.x / dl, d.y / dl);
    double s = d.y * ti.x - d.x * ti.y, as = fabs(s);
    double dist0 = (1.0 / as - 1.0) * D * 0.5;
    PV Q = pv(P.x + d.x * dist0, P.y + d.y * dist0);
    if (!pk_ps_clip(bounds, P, &Q, nctol)) return;
    double dist = hypot(Q.x - P.x, Q.y - P.y);
    if (last || !(as > 0.0001)) return;              /* (-0.9999 <= |s| as decompiled: always true) */
    int cw = (-(ti.x * to.y - ti.y * to.x)) < 0;     /* dVar4 = cross(-t_out, t_in) < 0 */
    for (int tr = 0; tr < 10; tr++) {
        double h = dist * as / (as + 1.0), c = dist - h, k2 = c * c - h * h, k = sqrt(k2 < 0 ? 0 : k2);
        PV A = pv(P.x + ti.x * k, P.y + ti.y * k), B = pv(P.x - to.x * k, P.y - to.y * k), C = pv(P.x + d.x * c, P.y + d.y * c);
        double ra = hypot(A.x - C.x, A.y - C.y);
        if (ra < 1e-12) continue;                    /* constr_ns_arc failed: the same values again (no shrink) */
        double a0 = atan2(A.y - C.y, A.x - C.x), a1 = atan2(B.y - C.y, B.x - C.x);
        double sw = cw ? -fmod(fmod(a0 - a1, 2 * M_PI) + 2 * M_PI, 2 * M_PI) : fmod(fmod(a1 - a0, 2 * M_PI) + 2 * M_PI, 2 * M_PI);
        Gl lp = { 0 };
        gl_push(&lp, gt_line(P, A)); gl_push(&lp, gt_arc(C, ra, a0, sw)); gl_push(&lp, gt_line(B, P));
        int hit = 0;
        for (int i = 0; i < bounds->n && !hit; i++) for (int j = 0; j < lp.n && !hit; j++)
            for (int m = 0; m < bounds->l[i].n && !hit; m++) hit = gt_cross(&lp.e[j], &bounds->l[i].e[m], nctol);
        if (!hit) { pk_loop(b, &lp, z, feed, ctol); gl_free(&lp); return; }
        gl_free(&lp);
        dist *= 0.8;
    }
}


/* "ours" (measured, ccomp_pock's internals are not decoded): an offset loop starts with the offset of its source
   loop's entity 0 (CHGREC_EX1_REV-A op 2, 2017 / X9: the innermost loop, cut from its entity 0, starts with the offset
   of chain entity 0, the bottom line from its left end). gls_offset starts anywhere: rotate the result to the entity
   start nearest T = the source's start moved by |d| to the left of its first entity (inward of an outer loop). */
static void pk_ps_restart(Gl *o, const Gl *src, double d) {
    if (!o->n || !src->n) return;
    PV s = gt_start(&src->e[0]), t = gt_tan(&src->e[0], 0), T = pv(s.x - t.y * d, s.y + t.x * d);
    int best = 0; double bd = 1e300;
    for (int k = 0; k < o->n; k++) { PV q = gt_start(&o->e[k]); double dd = hypot(q.x - T.x, q.y - T.y); if (dd < bd) { bd = dd; best = k; } }
    gl_rotate(o, best);
}

/* the rough boundary's outer loops (chains offset by R + wall stock + finish passes x spacing) started as above from
   the chain loop (the outer region loop) they come from: the region loop whose shifted start T is nearest the loop */
static void pk_ps_rb_start(Gls *rb, const Gls *reg, double dtot) {
    for (int i = 0; i < rb->n; i++) {
        if (rb->l[i].island) continue;
        int bj = -1; double bd = 1e300;
        for (int j = 0; j < reg->n; j++) {
            const Gl *g = &reg->l[j]; if (g->island || !g->n) continue;
            PV s = gt_start(&g->e[0]), t = gt_tan(&g->e[0], 0), T = pv(s.x - t.y * dtot, s.y + t.x * dtot);
            double dd = gl_closest(&rb->l[i], T, NULL, NULL, NULL); if (dd < bd) { bd = dd; bj = j; }
        }
        if (bj >= 0) pk_ps_restart(&rb->l[i], &reg->l[bj], dtot);
    }
}

static int pk_ps_npts(const Gls *s) { int n = 0; for (int i = 0; i < s->n; i++) if (!s->l[i].island && s->l[i].n && fabs(gl_area(&s->l[i])) > 0) n++; return n; }

/* FUN_1801ea750: the loops of one pocket in cut order (tag = the +0x45 flag), each rotated to its cut start */
static int pk_ps_loops(const Gls *bounds, double step, double minfil, int in2out, double tol, GlList *out, char *err, size_t errn) {
    Gls cur = { 0 }, isl = { 0 }, pend = { 0 };                 /* pend.l[0] = the head of the waiting list */
    for (int i = 0; i < bounds->n; i++) { Gl c = gl_copy(&bounds->l[i]); c.tag = 0; gls_push(bounds->l[i].island ? &isl : &cur, c); }
    GlList wr = { 0 }; PV last = pv(0, 0); int cut_any = 0, guard = 0;
    while (guard++ < 100000) {
        if (pk_ps_npts(&cur) == 0) {
            if (!pend.n) break;
            int bi = 0, be = 0;                                  /* FUN_1801ea450 */
            if (cut_any) {
                double bd = 1e38;
                for (int i = 0; i < pend.n; i++) for (int k = 0; k < pend.l[i].n; k++) {
                    PV s = gt_start(&pend.l[i].e[k]); double dd = hypot(last.x - s.x, last.y - s.y);
                    if (dd < bd) { bd = dd; bi = i; be = k; }
                }
            }
            Gl pick = pend.l[bi];
            memmove(&pend.l[bi], &pend.l[bi + 1], (pend.n - bi - 1) * sizeof(Gl)); pend.n--;
            gl_rotate(&pick, be);
            gls_free(&cur); gls_push(&cur, pick);
            if (isl.n) {
                int tg = pick.tag; PV keep = gt_start(&cur.l[0].e[0]);
                Gls m = pk_merge(&cur, &isl, tol); gls_free(&cur); cur = m;
                for (int i = 0; i < cur.n; i++) if (!cur.l[i].island) { cur.l[i].tag = tg; pk_start_near_ent(&cur.l[i], keep); }
            }
        }
        /* cut / write every non-island loop of the current list */
        for (int i = 0; i < cur.n; i++) {
            Gl *l = &cur.l[i];
            if (l->island || !l->n || fabs(gl_area(l)) <= 0) continue;
            last = gt_start(&l->e[0]); cut_any = 1;
            gll_push(&wr, gl_copy(l));
        }
        /* FUN_1801ea5d0: offset by the stepover (halved once), larger-than-parent loops dropped (FUN_1801ea3b0) */
        Gls nw = { 0 };
        for (int i = 0; i < cur.n; i++) {
            if (cur.l[i].island) continue;
            Gls one = { 0 }; gls_push(&one, gl_copy(&cur.l[i]));
            double dd = step; Gls o = { 0 };
            for (int k = 0; k < 2 && !o.n; k++, dd *= 0.5) { gls_free(&o); o = gls_offset(&one, -dd, minfil, tol); }
            double pa = fabs(gl_area(&cur.l[i]));
            for (int j = 0; j < o.n; j++) {
                if (fabs(gl_area(&o.l[j])) > pa) { gl_free(&o.l[j]); continue; }
                pk_ps_restart(&o.l[j], &cur.l[i], dd * 2);           /* dd was halved by the loop's step */
                o.l[j].tag = 1; gls_push(&nw, o.l[j]);
            }
            free(o.l); gls_free(&one);
        }
        gls_free(&cur);
        /* the first new loop is the current one, the others are linked at the head of the waiting list */
        for (int j = 0; j < nw.n; j++) {
            if (j == 0) { gls_push(&cur, nw.l[j]); continue; }
            gls_push(&pend, nw.l[j]);
            memmove(&pend.l[1], &pend.l[0], (pend.n - 1) * sizeof(Gl)); pend.l[0] = nw.l[j];
        }
        free(nw.l);
        if (pk_ps_npts(&cur) && isl.n) {                          /* merge_pock_bounds with the islands */
            int tg = cur.l[0].tag; PV keep = gt_start(&cur.l[0].e[0]);
            Gls m = pk_merge(&cur, &isl, tol); gls_free(&cur); cur = m;
            for (int i = 0; i < cur.n; i++) if (!cur.l[i].island) { cur.l[i].tag = tg; pk_start_near_ent(&cur.l[i], keep); }
        }
    }
    gls_free(&cur); gls_free(&isl); gls_free(&pend);
    if (guard >= 100000) { snprintf(err, errn, "pocket: Parallel Spiral loop limit"); gll_free(&wr); return 0; }
    if (in2out) for (int i = 0, j = wr.n - 1; i < j; i++, j--) { Gl t = wr.l[i]; wr.l[i] = wr.l[j]; wr.l[j] = t; }
    /* FUN_1801e9a90: loop 0 from entity 0, later loops from the entity start nearest the position (a loop ends where
       it starts; the clean-corner loops return to their corner) */
    for (int i = 1; i < wr.n; i++) {
        PV pe = gt_start(&wr.l[i - 1].e[0]); int best = 0; double bd = 1e38;
        for (int k = 0; k < wr.l[i].n; k++) { PV s = gt_start(&wr.l[i].e[k]); double dd = hypot(pe.x - s.x, pe.y - s.y); if (dd < bd) { bd = dd; best = k; } }
        gl_rotate(&wr.l[i], best);
    }
    if (getenv("PK_DEBUG")) for (int i = 0; i < wr.n; i++) fprintf(stderr, "ps loop %d: %d ents area %.5f tag %d\n", i, wr.l[i].n, gl_area(&wr.l[i]), wr.l[i].tag);
    *out = wr;
    return 1;
}

/* one loop of FUN_1801e9a90: the entities, with clean corners (FUN_1801e9420) after each but the last when on */
static void pk_ps_cut(MB *b, const Gls *bounds, const Gl *l, int clean, double D, double z, double feed, double ctol, double nctol) {
    for (int k = 0; k < l->n; k++) {
        pk_gt(b, &l->e[k], z, feed, ctol);
        if (clean && l->tag) pk_ps_corner(b, bounds, gt_end(&l->e[k]), &l->e[k], &l->e[(k + 1) % l->n], k == l->n - 1, D, z, feed, ctol, nctol);
    }
}

static int pk_ps_loops_reg(const Gls *rb, const Gls *reg, double dtot, double step, double minfil, int in2out, double tol, GlList *out, char *err, size_t errn) {
    Gls r = gls_copy(rb); pk_ps_rb_start(&r, reg, dtot);
    int ok = pk_ps_loops(&r, step, minfil, in2out, tol, out, err, errn);
    gls_free(&r); return ok;
}
