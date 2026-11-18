/* cam2d_pock_tspiral.c - Pocket roughing, cutting method 5 "True Spiral" (Pocktrue.cpp), ported from
 * decomp/mcmill_all.c. Included by cam2d_pock.c (one translation unit with pgeo.c / cam2d.c).
 *
 * FUN_1801ede50 (l.339337, spiral rough) case 5 calls FUN_1801ef3d0 (l.340034) per pocket (one outer rough boundary
 * plus its islands; the rough boundary = the tool-centre bounds offset by finish passes x spacing, as for High Speed):
 *   - centre = center_gravity_pock_bound (mccore 0x1805e28f0, decomp/pocket_b6/decomp_tspiral_core.c) of the (last) outer bound:
 *     the length-weighted centroid of the boundary curve (lines by their midpoints, arcs split into
 *     floor(|sweep| / 0.1745) + 1 pieces, each piece's mid-angle point weighted by its length);
 *   - build_arc_pock FUN_1801ee880 (l.339597): half circles i = 0, 1, ... about the centre shifted by -/+ stepover / 4
 *     in X (alternating), radius stepover (i / 2 + 0.375), clipped to the machinable area (segments list);
 *   - spiral inside to outside (pock_prm+0x30 = record 0x1258): the segments in order (FUN_1801ef550 l.340090), then
 *     one pass around the outer rough boundary from the point nearest the last position (FUN_1801ef030 l.339903);
 *     outside to inside: first that boundary pass (on the reversed bound, from the point nearest the end of the last
 *     segment), then the segments in reverse order, each reversed.
 *   - between pieces: FUN_1801e0f80 -> FUN_1801e23c0 (pock_move_avoid_islands, l.332360): a straight feed when the move
 *     stays in the machinable area; when a section of it is outside, the path around the island
 *     (find_path_around_island / FUN_1801e12b0) is NOT DECODED: refused.
 *   - the first move enters via FUN_1801bfcb0 -> FUN_1801bc830 (the common entry: the helix of cam2d_pock.c).
 * Climb (pock_prm+0x12 = record 0x123a, dialog 2940 control 25113) selects the arcs' direction: climb = the even half
 * circles from angle 0 counter-clockwise about centre.x - stepover / 4 (start angle 0x30 / sweep 0x38 of the build_arc_pock
 * arc entity, l.339650-339690), conventional the mirror (clockwise from pi about centre.x + stepover / 4).
 * Ours / substitutes: the intersection kernel (exact line / circle carriers, points kept within nc_tol of both
 * entities: the role of mccore ints_on_gt_new, not decoded) and pt_in_machinable_area as cam2d_pock.c pk_pt_ok.
 */

/* center_gravity_pock_bound (mccore 0x1805e28f0) */
static PV pk_ts_centroid(const Gl *l) {
    double sx = 0, sy = 0, sl = 0;
    for (int i = 0; i < l->n; i++) {
        const Gt *e = &l->e[i]; double len = gt_len(e);
        sl += len;
        if (e->t == 'L') { sx += len * (e->a.x + e->b.x) * 0.5; sy += len * (e->a.y + e->b.y) * 0.5; }
        else if (e->t == 'A') {
            double q = fabs(e->sw) / 0.1745; int n = (int)(q >= 0 ? floor(q) : ceil(q)) + 1;
            double da = e->sw / n, dl = len / n, a = da * 0.5 + e->sa;
            for (int k = 0; k < n; k++, a += da) { sx += dl * (cos(a) * e->r + e->c.x); sy += dl * (sin(a) * e->r + e->c.y); }
        }
    }
    return sl > 0 ? pv(sx / sl, sy / sl) : pv(0, 0);
}

/* intersections of the half circle a with one bound entity b (intersect_pock_bound l.mccore 0x1805e4570 -> ints_on_gt_new;
   our exact carriers, kept when within nc_tol of both entities) */
static int pk_ts_x(const Gt *a, const Gt *b, double nctol, PV *o) {
    PV q[2]; int n = 0, k = 0;
    if (b->t == 'L') n = x_line_circ(b->a, vsub(b->b, b->a), a->c, a->r, q);
    else if (b->t == 'A') n = x_circ_circ(a->c, a->r, b->c, b->r, q);
    for (int i = 0; i < n; i++) if (ent_on(a, q[i], nctol) && ent_on(b, q[i], nctol)) o[k++] = q[i];
    return k;
}

/* FUN_1801ef330: the point is strictly inside an outer bound (pt_in_ctour == 1: not within nc_tol of a bound) */
static int pk_ts_in_outer(const Gls *b, PV q, double tol, double nctol) {
    for (int i = 0; i < b->n; i++) {
        if (b->l[i].island) continue;
        if (gl_closest(&b->l[i], q, NULL, NULL, NULL) < nctol) continue;
        Gls one = { &b->l[i], 1, 1 };
        if (gls_inside(&one, q, tol)) return 1;
    }
    return 0;
}

typedef struct { PV c, p0, p1; double r, a0, a1; } PkTsSeg;
typedef struct { double x, y, a; } PkTsPt;
static int pk_ts_cmp_up(const void *x, const void *y) { double a = ((const PkTsPt *)x)->a, b = ((const PkTsPt *)y)->a; return b > a ? -1 : a > b; }   /* LAB_1801ef2e0 */
static int pk_ts_cmp_dn(const void *x, const void *y) { double a = ((const PkTsPt *)x)->a, b = ((const PkTsPt *)y)->a; return b > a ? 1 : (a <= b) - 1; }   /* LAB_1801ef300 */
static void pk_ts_restrict(double lo, double hi, double *a) { while (*a > hi) *a -= 2 * M_PI; while (*a < lo) *a += 2 * M_PI; }   /* restrict_angle */

/* build_arc_pock FUN_1801ee880 (l.339597): the clipped half circles of the spiral in build order */
static int pk_ts_build(const Gls *b, PV ctr, double step, int climb, double tol, double nctol, PkTsSeg **segs, int *nseg) {
    int cap = 0, empty = 0; *segs = NULL; *nseg = 0;
    PkTsPt *pt = NULL; int ptcap = 0;
    PV *raw = NULL; int rawcap = 0;
    for (int i = 0; i < 0x7fff; i++) {
        double d21 = (i + 0.25) * 0.5 * step, d3 = step * 0.5 + d21;
        double d22 = (d21 - d3) * 0.5, r = (d3 + d21) * 0.5;
        if (!climb) d22 = -d22;
        int odd = i & 1;
        PV c = pv(odd ? ctr.x - d22 : ctr.x + d22, ctr.y);
        double sa = odd ? (climb ? M_PI : 0) : (climb ? 0 : M_PI), sw = climb ? M_PI : -M_PI;
        Gt arc = gt_arc(c, r, sa, sw);
        int np = 0;
        for (int j = 0; j < b->n; j++) {          /* every bound of the pocket (outer and islands) */
            int nraw = 0;                                         /* intersect_pock_bound's own list (per bound) */
            for (int k = 0; k < b->l[j].n; k++) {
                PV o[2]; int nx = pk_ts_x(&arc, &b->l[j].e[k], nctol, o);
                for (int m = 0; m < nx; m++) {
                    int dup = 0;                                  /* not within tol of a point it already stored */
                    for (int s = 0; s < nraw && !dup; s++) dup = hypot(raw[s].x - o[m].x, raw[s].y - o[m].y) < tol;
                    if (dup) continue;
                    if (nraw == rawcap) { rawcap = rawcap ? rawcap * 2 : 64; raw = realloc(raw, rawcap * sizeof *raw); }
                    raw[nraw++] = o[m];
                    double dx = o[m].x - c.x, dy = o[m].y - c.y;
                    if (odd ? !(dy < 0) : !(dy > 0)) continue;   /* even: dy > 0, odd: dy < 0 (l.339728-339740) */
                    if (np + 2 >= ptcap) { ptcap = ptcap ? ptcap * 2 : 64; pt = realloc(pt, ptcap * sizeof *pt); }
                    double a = 0;
                    if (dx * dx + dy * dy >= 1e-12) { a = atan2(dy, dx); if (a < 0) a += 2 * M_PI; }
                    pt[np++] = (PkTsPt){ o[m].x, o[m].y, a };
                }
            }
        }
        if (np + 2 >= ptcap) { ptcap = ptcap ? ptcap * 2 : 64; pt = realloc(pt, ptcap * sizeof *pt); }
        PV s0 = gt_start(&arc), s1 = gt_end(&arc);
        pt[np++] = (PkTsPt){ s0.x, s0.y, sa };
        pt[np++] = (PkTsPt){ s1.x, s1.y, sa + sw };
        double lo = odd ? M_PI / 2 : -M_PI / 2;
        for (int k = 0; k < np; k++) pk_ts_restrict(lo, lo + 2 * M_PI, &pt[k].a);
        qsort(pt, np, sizeof *pt, climb ? pk_ts_cmp_up : pk_ts_cmp_dn);
        int got = 0;
        for (int k = 0; k + 1 < np; k++) {
            double am = (pt[k + 1].a + pt[k].a) * 0.5;
            PV m = pv(cos(am) * r + c.x, sin(am) * r + c.y);
            if (!pk_pt_ok(b, m, nctol)) continue;            /* pt_in_machinable_area */
            if (*nseg == cap) { cap = cap ? cap * 2 : 64; *segs = realloc(*segs, cap * sizeof **segs); }
            (*segs)[(*nseg)++] = (PkTsSeg){ c, pv(pt[k].x, pt[k].y), pv(pt[k + 1].x, pt[k + 1].y), r, pt[k].a, pt[k + 1].a };
            got++;
        }
        /* end: two half circles in a row without a segment whose last point is not inside an outer bound */
        if (*nseg < 1 || got) empty = 0;
        else if (pk_ts_in_outer(b, pv(pt[np - 1].x, pt[np - 1].y), tol, nctol)) empty = 0;
        else if (++empty > 1) break;
    }
    free(pt); free(raw);
    return 1;
}

/* one segment as an open chain (FUN_1801e1cb0 arc from start to end about the centre, direction flag) */
static Gl pk_ts_seg_gl(const PkTsSeg *s, int rev) {
    Gl l = { 0 };
    Gt a = gt_arc(s->c, s->r, s->a0, s->a1 - s->a0);
    gl_push(&l, rev ? gt_rev(&a) : a);
    return l;
}
/* FUN_1801ef030: once around the bound from the point nearest q (proj_pt_to_pock_bound) */
static Gl pk_ts_bound_pass(const Gl *bound, int reverse, PV q) {
    Gl l = gl_copy(bound);
    if (reverse) gl_reverse(&l);                         /* reverse_pock_bound (FUN_1801ef3d0 l.340063) */
    pk_start_near_pt(&l, q, CAM_SYS_TOL);
    return l;
}

/* FUN_1801ef3d0 for every outer loop of the rough boundary bounds (with the islands inside it): the pieces to cut in
   order (each an open chain; consecutive pieces are joined by a straight feed, checked here). 0 + err on refusal. */
static int pk_ts_loops(const Gls *bounds, double step, int climb, int in2out, double tol, double nctol,
                       GlList *out, char *err, size_t errn) {
    memset(out, 0, sizeof *out);
    for (int o = 0; o < bounds->n; o++) {
        if (bounds->l[o].island) continue;
        Gls pb = { 0 };
        gls_push(&pb, gl_copy(&bounds->l[o]));
        for (int j = 0; j < bounds->n; j++) {
            if (!bounds->l[j].island) continue;
            Gls one = { &bounds->l[o], 1, 1 };
            if (gls_inside(&one, gt_pt(&bounds->l[j].e[0], 0), tol)) gls_push(&pb, gl_copy(&bounds->l[j]));
        }
        PV ctr = pk_ts_centroid(&pb.l[0]);
        PkTsSeg *sg; int ns;
        pk_ts_build(&pb, ctr, step, climb, tol, nctol, &sg, &ns);
        if (getenv("PK_DEBUG")) fprintf(stderr, "true spiral: centre %.4f %.4f, %d segments\n", ctr.x, ctr.y, ns);
        int from = out->n;
        if (ns == 0 && in2out) {   /* FUN_1801ef030 would start from the caller's uninitialised position (l.339497) */
            free(sg); gls_free(&pb); gll_free(out);
            snprintf(err, errn, "pocket True Spiral: no spiral segment fits (boundary pass start NOT DECODED)"); return 0;
        }
        if (!in2out && ns > 0) gll_push(out, pk_ts_bound_pass(&pb.l[0], 1, sg[ns - 1].p1));
        for (int k = 0; k < ns; k++) gll_push(out, in2out ? pk_ts_seg_gl(&sg[k], 0) : pk_ts_seg_gl(&sg[ns - 1 - k], 1));
        if (in2out) { Gl *pl = &out->l[out->n - 1]; gll_push(out, pk_ts_bound_pass(&pb.l[0], 0, gt_end(&pl->e[pl->n - 1]))); }
        /* links (FUN_1801e0f80 -> pock_move_avoid_islands FUN_1801e23c0): straight when inside */
        for (int k = from + 1; k < out->n; k++) {
            Gl *pl = &out->l[k - 1]; PV a = gt_end(&pl->e[pl->n - 1]), z = gt_start(&out->l[k].e[0]);
            if (hypot(a.x - z.x, a.y - z.y) <= CAM_SYS_TOL) continue;
            if (!pk_line_inside(&pb, a, z, nctol)) {
                free(sg); gls_free(&pb); gll_free(out);
                snprintf(err, errn, "pocket True Spiral: link around an island (find_path_around_island) NOT DECODED"); return 0;
            }
        }
        free(sg); gls_free(&pb);
    }
    return 1;
}
