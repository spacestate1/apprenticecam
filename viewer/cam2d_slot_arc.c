/* cam2d_slot_arc.c - Slot Mill's ramp on the exact line / arc loop (decisions batch dec1, formats/user_decisions.md B7-3).
 * Included by cam2d.c after pgeo.c (it needs the exact loops); called from slot_chain (cam2d_ops.c).
 *
 * Mastercam (decomp/mcmill_all.c): FUN_180218e00 offsets the slot ctour inward (ccomp_pock_from_ctour, min fillet 0.785398)
 * and hands it to FUN_1802178c0 ("SLOTMILL.cpp" create_ramped_ctour, l.366957):
 *   - ctour_to_pock_bound + reverse_pock_bound; laps = floor((top - z) / (perimeter tan(angle))) + 3, records = laps x n;
 *   - walk the reversed bound cyclically from z upward; each record climbs length_gt x tan(angle) (its +0x78 = its start z);
 *     two consecutive lines whose joint lies on the line from the first's start to the second's end (dist_pt_l <
 *     length x 2e-7) are one record (the second is skipped);
 *   - the record crossing top is cut at top when it climbs >= tol: a line at the interpolated point, an arc's sweep x f;
 *     kept only when the cut record is longer than nc_tol; the walk ends;
 *   - reverse_pock_bound again (the tool descends from top, each record ending at its lower z), then one loop of the
 *     un-ramped bound at z (+0x78 = z); FUN_180218130 with a start record at top.
 * FUN_180218410 writes each record: a line G1 at the feed rate (0x378); an arc through FUN_1802189e0 (l.367698):
 *   |z1 - z0| <= tol (flat) or 0x1280 "Output helixes as arcs" (P +0x58) set -> gr_arc_mill (an arc, helical when z changes);
 *   else constr_ns_arc and n = floor(|sweep| / sqrt(8 x 0x1288 / r)) + 1 lines of equal angle, z linear in the angle
 *   (z = |remaining| (z0 - z1) / |sweep| + z1), each at the feed rate (cur_op +0x310).
 * Ours: the offset - slot_offset_convex (exact, for convex boundaries: the usual slot) or else pgeo.c gls_offset (the
 * existing substitute for ccomp_pock, as the pocket engines use); the loop starts at the boundary start moved d along the
 * inward normal (its nearest point with gls_offset, as slot_loop does for the polygon);
 * a Tolerance 0x1288 <= 0 with 0x1280 off is refused (Mastercam would divide by it).
 */

static Gl slot_gl(const BEnt *e, int n) {
    Gl l = { 0 };
    for (int i = 0; i < n; i++)
        gl_push(&l, e[i].arc ? gt_arc(pv(e[i].cx, e[i].cy), e[i].r, e[i].a0, e[i].sw) : gt_line(pv(e[i].ax, e[i].ay), pv(e[i].bx, e[i].by)));
    return l;
}

/* ours: the exact inward offset of a convex slot boundary (lines moved along the inward normal, arcs concentric r -+ d,
   tangent joins kept, sharp line / line corners re-intersected) - ccomp_pock_from_ctour's internals are not decoded, and
   gls_offset's Clipper topology puts tangent points up to ~0.02 off. Returns 0 (use gls_offset) for anything else: a
   reflex corner, a non-tangent corner with an arc, an element that vanishes, or an area that disagrees with gls_offset's. */
static int slot_offset_convex(const BEnt *e, int n, int left, double d, double gls_area, Gl *out) {
    double sgn = left ? 1 : -1;                                  /* the region is on the left of a counter-clockwise loop */
    Gl o = { 0 };
    for (int i = 0; i < n; i++) {
        if (e[i].arc) {
            double r = e[i].r - d * ((e[i].sw > 0) == left ? 1 : -1);
            if (!(r > 1e-6)) { gl_free(&o); return 0; }
            gl_push(&o, gt_arc(pv(e[i].cx, e[i].cy), r, e[i].a0, e[i].sw));
        } else {
            double tx = e[i].bx - e[i].ax, ty = e[i].by - e[i].ay, l = hypot(tx, ty);
            if (!(l > 0)) { gl_free(&o); return 0; }
            tx /= l; ty /= l; double nx = -ty * sgn, ny = tx * sgn;
            gl_push(&o, gt_line(pv(e[i].ax + d * nx, e[i].ay + d * ny), pv(e[i].bx + d * nx, e[i].by + d * ny)));
        }
    }
    for (int i = 0; i < n; i++) {
        Gt *a = &o.e[i], *b = &o.e[(i + 1) % n];
        PV E = gt_end(a), S = gt_start(b);
        PV ta = gt_tan(a, 1), tb = gt_tan(b, 0);
        double turn = ta.x * tb.y - ta.y * tb.x;
        if (hypot(E.x - S.x, E.y - S.y) < 1e-9) continue;                    /* tangent / continuous join */
        if (a->t != 'L' || b->t != 'L' || turn * sgn <= 0) { gl_free(&o); return 0; }   /* only convex line / line corners */
        double den = ta.x * tb.y - ta.y * tb.x, u = ((S.x - a->a.x) * tb.y - (S.y - a->a.y) * tb.x) / den;
        PV X = pv(a->a.x + ta.x * u, a->a.y + ta.y * u);
        a->b = X; b->a = X;
    }
    for (int i = 0; i < n; i++) {                                 /* a trimmed line must keep its direction */
        if (o.e[i].t != 'L') continue;
        double ox = e[i].bx - e[i].ax, oy = e[i].by - e[i].ay, nx2 = o.e[i].b.x - o.e[i].a.x, ny2 = o.e[i].b.y - o.e[i].a.y;
        if (ox * nx2 + oy * ny2 <= 0) { gl_free(&o); return 0; }
    }
    double A = fabs(gl_area(&o));
    if (!(A > 0) || fabs(A - gls_area) > 1e-3 * A + 1e-6) { gl_free(&o); return 0; }
    *out = o; return 1;
}

/* dist_pt_l: distance of q from the infinite line a -> b */
static double slot_dist_pt_l(PV q, PV a, PV b) {
    double dx = b.x - a.x, dy = b.y - a.y, l = hypot(dx, dy);
    return l > 0 ? fabs((q.x - a.x) * dy - (q.y - a.y) * dx) / l : hypot(q.x - a.x, q.y - a.y);
}

/* one record of the descending ramp / the loop: a line, an arc (gr_arc_mill) or the linearised helix (FUN_1802189e0) */
static int slot_arc_rec(MList *ml, const Gt *g, double z0, double z1, const CamParams *p, char *err, size_t errn) {
    PV q = gt_end(g);
    if (g->t != 'A') { ml_line(ml, q.x, q.y, z1, p->feed); return 1; }
    if (fabs(z1 - z0) <= MC_TOL || mcb(p, 0x1280)) { ml_add(ml, 'A', q.x, q.y, g->c.x, g->c.y, g->sw < 0, 0, z1, p->feed); return 1; }
    double tol = mcd(p, 0x1288);
    if (!(tol > 0) || !(g->r > 0)) { snprintf(err, errn, "Slot Mill: helix linearization Tolerance (0x1288) must be > 0 when Output helixes as arcs is off"); return 0; }
    double sw = fabs(g->sw), step = sqrt(tol * 8.0 / g->r);
    int k = (int)floor(sw / step) + 1; if (k > 1000000) k = 1000000;
    double da = sw / k, a = g->sa, s = g->sw < 0 ? -1 : 1, rem = sw;
    for (int j = 0; j < k; j++) {
        a += s * da; rem -= da;
        ml_line(ml, g->c.x + g->r * cos(a), g->c.y + g->r * sin(a), fabs(rem) * (z0 - z1) / sw + z1, p->feed);
    }
    return 1;
}

/* the ramp of the innermost loop (offset d) from zt down to z, then one loop at z. Returns 1 output, 0 nothing (zt - z <=
   1e-12), -1 error (err set). *ex / *ey = the end of the last record. */
static int slot_ramp_exact(const BEnt *e, int n, int left, double d, double nx, double ny, double ang_deg, double z, double zt,
                           const CamParams *p, double gtol, MList *ml, double *ex, double *ey, char *err, size_t errn) {
    Gl B = slot_gl(e, n);
    if (!left) gl_reverse(&B);                                   /* gls_offset: the region on the left (counter-clockwise outer) */
    Gls in = { &B, 1, 1 };
    Gls o = gls_offset(&in, -d, 0.785398, gtol);
    gl_free(&B);
    int best = -1; double ba = 0;
    for (int i = 0; i < o.n; i++) { double a = fabs(gl_area(&o.l[i])); if (!o.l[i].island && o.l[i].n && a > ba) { ba = a; best = i; } }
    if (best < 0) { gls_free(&o); snprintf(err, errn, "Slot Mill: offset failed (Mastercam warning 0x954)"); return -1; }
    Gl L;
    if (slot_offset_convex(e, n, left, d, ba, &L)) gls_free(&o);  /* exact; starts at e[0].a + d N */
    else {
        L = gl_copy(&o.l[best]); gls_free(&o);
        if (!left) gl_reverse(&L);
        PV q = pv(e[0].ax + d * nx, e[0].ay + d * ny), pt; int ei = 0; double t = 0; gl_closest(&L, q, &ei, &t, &pt); gl_split_start(&L, ei, t);
    }
    if (!(zt - z > 1e-12)) { gl_free(&L); return 0; }
    Gl R = gl_copy(&L); gl_reverse(&R);
    double tn = tan(ang_deg * 0.01745329251994329), per = gl_len(&R);
    long cap = (long)(floor((zt - z) / (per * tn)) + 3) * R.n;
    if (cap > 2000000) cap = 2000000;
    if (cap < 1) cap = 1;
    Gt *rec = malloc(sizeof(Gt) * cap); double *zs = malloc(sizeof(double) * cap); long nr = 0;
    double zc = z; int i = 0;
    for (long k = 0; k < cap; k++) {
        Gt g = R.e[i]; int nx1 = (i + 1) % R.n;
        if (g.t == 'L' && R.e[nx1].t == 'L') {                   /* the collinear-lines test of the walk */
            PV a = g.a, b2 = R.e[nx1].b;
            if (slot_dist_pt_l(g.b, a, b2) < hypot(b2.x - a.x, b2.y - a.y) * 2e-7) { g.b = b2; nx1 = (nx1 + 1) % R.n; }
        }
        double z1 = zc + gt_len(&g) * tn;
        if (zt < z1) {
            if (z1 - zc >= MC_TOL) {
                double f = (zt - zc) / (z1 - zc);
                if (g.t == 'L') g.b = pv(g.a.x * (1 - f) + g.b.x * f, g.a.y * (1 - f) + g.b.y * f);
                else if (g.t == 'A') g.sw *= f;
                if (gt_len(&g) > MC_NC_TOL) { rec[nr] = g; zs[nr] = zc; nr++; }
            }
            break;
        }
        rec[nr] = g; zs[nr] = zc; nr++;
        zc = z1; i = nx1;
    }
    gl_free(&R);
    int ok = 1;
    if (nr > 0) {
        /* reversed: from the top down; the start record at zt (FUN_180218130), each record ending at its lower z */
        PV top = gt_end(&rec[nr - 1]);
        ml_line(ml, top.x, top.y, zt, p->plunge_feed);
        double zprev = zt;
        for (long k = nr - 1; k >= 0 && ok; k--) {
            Gt g = gt_rev(&rec[k]);
            ok = slot_arc_rec(ml, &g, zprev, zs[k], p, err, errn);
            zprev = zs[k];
        }
        for (int k = 0; k < L.n && ok; k++) ok = slot_arc_rec(ml, &L.e[k], z, z, p, err, errn);   /* one loop at z */
        if (ok) { *ex = ml->r[ml->n - 1].x; *ey = ml->r[ml->n - 1].y; }
    }
    free(rec); free(zs); gl_free(&L);
    if (!ok) return -1;
    return nr > 0;
}
