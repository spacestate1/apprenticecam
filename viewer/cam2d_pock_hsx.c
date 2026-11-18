/* cam2d_pock_hsx.c - High Speed pocket (method 8) "extra loops" for a stepover between R and D, included by
 * cam2d_pock.c (one translation unit with cam2d.c). Every cited line is decomp/mcmill_all.c (mcmill.dll, Pockhs.cpp).
 *
 * hs_parallel_spiral_pocket FUN_1801d00d0 l.320598-320690: when the stepover actually used for the next loops
 * (local_190, after FUN_1801d1850's halving) is strictly between D / 2 and D, the material the two passes leave (the
 * corner cusps) becomes extra loops:
 *   - FUN_1801d1740(res, D / 2, side of the stepover, island flag 0) l.321132: each non-island current loop offset
 *     inward by D / 2 (ccomp_pock), as outer loops;
 *   - FUN_1801d1740(next, D / 2, the other side, island flag 1): each next loop offset outward by D / 2, as islands;
 *   - FUN_1801d2280 l.321620: a loop that intersects no loop of the other kind (pock_bound_intersect) is deleted
 *     (in list order, deletions seen by the later checks);
 *   - merge_pock_bounds: the outer loops minus the islands (gls_merge, our substitute, pgeo.c);
 *   - every result loop is an outer loop again (+2 = 0); one whose box (bbox_gt per entity) has its larger side
 *     <= D / 2 is flagged (+2 = 1): a "small" loop, cut as a circle only (FUN_1801ce710, below);
 *   - sharp corner smoothing (FUN_1801d2b80) when 0x1400 > nc_tol;
 *   - FUN_1801d28b0 l.321845: the owner (+0x50). Inside to outside (pock_prm+0x30 = record 0x1258 on): the current
 *     loop; outside to inside: the nearest next loop (FUN_1801cea40 l.319397: least distance between entity start
 *     points; a single next loop owns all).
 * The extras are written after their owner (FUN_1801eb050 with +1 = 1) and read back with it (FUN_1801d19d0 keeps the
 * group; FUN_1801d00d0 l.320853 LAB_1801d0ce0), then cut by FUN_1801cf8b0 l.319995 while cutting the owner (pk_hs_cut).
 */

/* FUN_1801cea40: the least distance between the entity start points of two loops */
static double pk_cea40(const Gl *a, const Gl *b) {
    double best = 1e38;
    for (int i = 0; i < a->n; i++) for (int j = 0; j < b->n; j++) {
        PV p = gt_start(&a->e[i]), q = gt_start(&b->e[j]); double d = hypot(q.x - p.x, q.y - p.y);
        if (d < best) best = d;
    }
    return best;
}
/* pock_bound_intersect as the port reads it elsewhere (pk_inside_all): a transversal crossing of two entities */
static int pk_bnd_cross(const Gl *a, const Gl *b, double tol) {
    for (int i = 0; i < a->n; i++) for (int j = 0; j < b->n; j++) if (gt_cross(&a->e[i], &b->e[j], tol)) return 1;
    return 0;
}

/* the extra loops between the current loops res and the next loops next (see the file header) */
static void pk_hs_extras(const Gls *res, const Gls *next, double dia, double minfil, double smooth, double ftol, double tol, Gls *out) {
    Gls ex = { 0 };
    double h = dia * 0.5;
    for (int i = 0; i < res->n; i++) {                       /* FUN_1801d1740(local_res8, .., 1) */
        if (res->l[i].island) continue;
        Gls one = { 0 }; gls_push(&one, gl_copy(&res->l[i]));
        Gls o = gls_offset(&one, -h, minfil, tol);
        for (int j = 0; j < o.n; j++) { o.l[j].island = 0; gls_push(&ex, o.l[j]); }
        free(o.l); gls_free(&one);
    }
    for (int i = 0; i < next->n; i++) {                      /* FUN_1801d1740(local_1a0, .., 0) */
        if (next->l[i].island) continue;
        Gls one = { 0 }; gls_push(&one, gl_copy(&next->l[i]));
        Gls o = gls_offset(&one, h, minfil, tol);
        for (int j = 0; j < o.n; j++) { o.l[j].island = 1; gls_push(&ex, o.l[j]); }
        free(o.l); gls_free(&one);
    }
    /* FUN_1801d2280: delete the loops that cross no loop of the other kind */
    char *dead = calloc(ex.n ? ex.n : 1, 1);
    for (int i = 0; i < ex.n; i++) {
        int hit = 0;
        for (int j = 0; j < ex.n && !hit; j++) if (!dead[j] && ex.l[j].island != ex.l[i].island) hit = pk_bnd_cross(&ex.l[i], &ex.l[j], tol);
        if (!hit) dead[i] = 1;
    }
    Gls keep = { 0 };
    for (int i = 0; i < ex.n; i++) { if (dead[i]) gl_free(&ex.l[i]); else gls_push(&keep, ex.l[i]); }
    free(ex.l); free(dead);
    Gls m = { 0 };
    if (keep.n) {
        /* the island loops reversed to clockwise (sort_pock_bounds' convention, pk_region) before the merge */
        for (int i = 0; i < keep.n; i++) { double a = gl_area(&keep.l[i]); if ((a > 0) == keep.l[i].island) gl_reverse(&keep.l[i]); }
        m = gls_merge(&keep, minfil, tol);
    }
    gls_free(&keep);
    for (int i = 0; i < m.n; i++) {                          /* l.320627-320683: +2 = 0, small loops +2 = 1 */
        double bb[4]; gl_bbox(&m.l[i], bb);
        double w = fmax(bb[2] - bb[0], bb[3] - bb[1]);
        m.l[i].island = w <= h;
    }
    if (smooth > ftol) pk_smooth(&m, smooth, minfil, tol);   /* l.320687 */
    if (getenv("PK_DEBUG")) for (int i = 0; i < m.n; i++) { double bb[4]; gl_bbox(&m.l[i], bb); fprintf(stderr, "extra %d: %d ents small %d box %.4f %.4f %.4f %.4f\n", i, m.l[i].n, m.l[i].island, bb[0], bb[1], bb[2], bb[3]); }
    *out = m;
}

/* ------------------------------------------------------------------------------------------------ cutting */
/* FUN_1801e2270 (line to a point, skipped below tol_exref) / FUN_1801e1cb0 (arc from the current point to a point about
   a centre; below min_arc_length a line; full = a whole circle back to the start) at depth z */
static void pk_x_line(MB *b, PV *cur, PV to, double z, double feed) {
    if (hypot(to.x - cur->x, to.y - cur->y) < CAM_SYS_TOL) return;
    mv(b, 0, to.x, to.y, z, feed); *cur = to;
}
static void pk_x_arc(MB *b, PV *cur, PV to, PV c, int cw, double z, double feed, double ctol) {
    if (hypot(to.x - cur->x, to.y - cur->y) < g_min_arc) { pk_x_line(b, cur, to, z, feed); return; }
    double r = hypot(cur->x - c.x, cur->y - c.y), a0 = atan2(cur->y - c.y, cur->x - c.x), a1 = atan2(to.y - c.y, to.x - c.x);
    double sw = a1 - a0;
    if (cw) { while (sw >= 0) sw -= 2 * M_PI; } else { while (sw <= 0) sw += 2 * M_PI; }
    Gt g = gt_arc(c, r, a0, sw); pk_arc_moves(b, &g, z, z, feed, ctol); *cur = to;
}
/* one entity of a loop from parameter t0 to t1 (FUN_1801cf8b0 l.320214-320228) */
static void pk_x_ent(MB *b, PV *cur, const Gt *e, double t1, double z, double feed, double ctol) {
    if (e->t == 'L') { pk_x_line(b, cur, gt_pt(e, t1), z, feed); return; }
    if (e->t != 'A') return;
    if (fabs(e->sw) > 6.283185107179587) { Gt t = *e; t.sa = atan2(cur->y - e->c.y, cur->x - e->c.x); t.sw = e->sw > 0 ? 2 * M_PI : -2 * M_PI; pk_arc_moves(b, &t, z, z, feed, ctol); return; }
    pk_x_arc(b, cur, gt_pt(e, t1), e->c, e->sw < 0, z, feed, ctol);
}
/* FUN_1801e0f80 as gen_pocket_v2 ports it (a straight feed when it stays inside; the path around islands is NOT DECODED:
   instead, ours: pk_retract, a rapid over the target, the plunge; a stand-in for the walk at the depth, not a section start: not pk_sec_start, V-14) */
static void pk_x_straight(MB *b, const CamParams *p, const Gls *rb, PV *cur, PV to, double z, double nctol) {
    double zf = p->top_z + p->feed_plane;
    if (pk_line_inside(rb, *cur, to, nctol)) mv(b, 0, to.x, to.y, z, p->feed);
    else { pk_retract(b, p); mv(b, 1, to.x, to.y, fmax(zf, p->retract_z), 0); mv(b, 1, to.x, to.y, zf, 0); mv(b, 0, to.x, to.y, z, p->plunge_feed); }
    *cur = to;
}
/* FUN_1801d26e0 l.321778: the S-shaped link (FUN_1801d13a0) when it stays inside (FUN_1801d2c60), else FUN_1801e0f80 */
static void pk_x_link(MB *b, const CamParams *p, const Gls *rb, int dir, PV *cur, PV to, double z, double step, double nctol, double ctol) {
    Gl s = pk_sshape(dir, *cur, to);
    if (pk_inside_all(rb, &s, nctol, step)) {
        pk_x_arc(b, cur, gt_end(&s.e[0]), s.e[0].c, !dir, z, p->feed, ctol);   /* cw = (param_2 == 0) */
        pk_x_arc(b, cur, to, s.e[1].c, dir, z, p->feed, ctol);                  /* cw = param_2 */
    } else pk_x_straight(b, p, rb, cur, to, z, nctol);
    gl_free(&s);
}

typedef struct { int k, ei, xi; double t; PV pt; } PkAtt;
/* FUN_1801cf8b0 l.319995 for a loop with extra loops: the loop's entities from its start; on each entity the
   attachments (FUN_1801cede0 inside to outside / FUN_1801ceb50 outside to inside: per extra loop, the entity start
   point nearest the loop) in increasing parameter (ties: the first extra); at one: the loop is cut up to it, then
   - a small extra: FUN_1801ce710 l.319237, a circle through the point and the attachment point (two half arcs about
     their midpoint, clockwise when the cross product below is < 0);
   - else: FUN_1801d26e0 out (direction: inside to outside cross >= 0, outside to inside cross < 0, cross = (P - cur) x
     the loop tangent), the extra loop from that entity all round (the recursive FUN_1801cf8b0, whose link from the
     previous point is of zero length here), FUN_1801d26e0 back with the other direction. */
static void pk_hs_cut(MB *b, const CamParams *p, const Gls *rb, const Gl *l, const Gl *const *xl, const int *xsmall, int nx,
                      int in2out, double z, double step, double nctol, double ctol) {
    PkAtt *at = calloc(nx ? nx : 1, sizeof(PkAtt)); int na = 0;
    for (int k = 0; k < nx; k++) {
        const Gl *x = xl[k]; double best = 1e38, bt = 1.0; int bi = -1, bj = -1; PV bp = pv(0, 0);
        for (int j = 0; j < x->n; j++) {
            PV P = gt_start(&x->e[j]);
            for (int i = 0; i < l->n; i++) {
                if (l->e[i].t == 'P') continue;
                double t = gt_t_near(&l->e[i], P); PV q = gt_pt(&l->e[i], t); double d = hypot(P.x - q.x, P.y - q.y);
                int take = in2out ? (d < best - CAM_SYS_TOL || (d < best + CAM_SYS_TOL && bi == i && t < bt))   /* FUN_1801cede0 */
                                  : (d < best + CAM_SYS_TOL && (bi != i || t < bt));                            /* FUN_1801ceb50 */
                if (take) { best = d; bt = t; bi = i; bj = j; bp = P; }
            }
        }
        if (bi >= 0) { at[na].k = k; at[na].ei = bi; at[na].t = bt; at[na].xi = bj; at[na].pt = bp; na++; }
    }
    char *used = calloc(na ? na : 1, 1);
    PV cur = gt_start(&l->e[0]);
    for (int i = 0; i < l->n; i++) {
        const Gt *e = &l->e[i];
        if (e->t == 'P') continue;
        double tm;
        do {
            int a = -1; tm = 1e38;
            for (int q = 0; q < na; q++) if (!used[q] && at[q].ei == i && at[q].t < tm) { tm = at[q].t; a = q; }
            if (tm > 1.0 - CAM_SYS_TOL) tm = 1.0;
            pk_x_ent(b, &cur, e, tm, z, p->feed, ctol);
            if (a >= 0) {
                PV P = at[a].pt, back = cur, tg = gt_tan(e, tm);
                double cr = (P.y - cur.y) * tg.x - tg.y * (P.x - cur.x);
                if (xsmall[at[a].k]) {
                    PV m = pv((cur.x + P.x) * 0.5, (cur.y + P.y) * 0.5);
                    pk_x_arc(b, &cur, P, m, cr < 0, z, p->feed, ctol);
                    pk_x_arc(b, &cur, back, m, cr < 0, z, p->feed, ctol);
                } else {
                    int dir = in2out ? cr >= 0 : cr < 0;
                    const Gl *x = xl[at[a].k];
                    pk_x_link(b, p, rb, dir, &cur, P, z, step, nctol, ctol);
                    for (int j = 0; j < x->n; j++) pk_x_ent(b, &cur, &x->e[(at[a].xi + j) % x->n], 1.0, z, p->feed, ctol);
                    pk_x_link(b, p, rb, !dir, &cur, back, z, step, nctol, ctol);
                }
                used[a] = 1;
            }
        } while (tm != 1.0);
    }
    free(at); free(used);
}
