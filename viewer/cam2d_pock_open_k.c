/* cam2d_pock_open_k.c - the geometry helpers of cam2d_pock_open.c: the mccore / systypes functions it needs, each with its source; checked against the real DLLs by tools/tests/open_pocket_oracle.sh (tools/pko_port.c, peloader --remach). Needs PV / Gt / RmEnt, PkoLine, g_nc_tol, CAM_SYS_TOL. */
#ifndef TWO_PI
#define TWO_PI 6.283185307179586
#endif
/* copies of mccore helpers as ported in cam2d_remach.c (a separate translation unit): pt_on_arc, dist_to_finite_line, l_ints_tol, la_ints_new, pt_on_gt_new */
static int pko_pt_on_arc(const double p[2], const double a[5], double t) { /* pko_pt_on_arc sys l.91 */
    double d5 = p[1], d1 = p[0], r = a[2], d4 = a[0] - d1, d6 = a[1] - d5;
    d4 = sqrt(d4 * d4 + d6 * d6);
    if (!(fabs(d4 - r) <= t)) return 0;
    d1 = d1 - a[0]; d5 = d5 - a[1];
    if (1e-12 <= d5 * d5 + d1 * d1) { d5 = atan2(d5, d1); if (d5 < 0.0) d5 += TWO_PI; } else d5 = 0.0;
    double d4b = 0.0, d6b, d7;
    if (a[4] <= 0.0) { d7 = a[3]; d6b = d7 + a[4]; } else { d6b = a[3]; d7 = d6b + a[4]; }
    if (1e-12 < r) d4b = t / r;
    for (; d7 + d4b < d5; d5 += -TWO_PI) { }
    for (; d5 < d6b - d4b; d5 += TWO_PI) { }
    return !(d5 < d6b - d4b || d7 + d4b < d5);
}
static int pko_dist_to_finite_line(const double p[2], const double l[4], double t, double *dist) {   /* pko_dist_to_finite_line more l.1175 */
    double d6 = l[1], d7 = l[0], d3 = l[2] - d7, d4 = l[3] - d6, d5 = sqrt(d3 * d3 + d4 * d4), dd;
    if (CAM_SYS_TOL <= d5) {
        double d1 = p[0], d2 = p[1], d8 = fabs((d1 - d7) * d4 - (d2 - d6) * d3) / d5;
        *dist = d8;
        if (t <= d8) return 0;
        double u = ((d1 - d7) * d3 + (d2 - d6) * d4) / d5;
        if (0.0 <= u) {
            if (u <= d5) return 1;
            if (d5 + t <= u) return 0;
            dd = (l[3] - p[1]) * (l[3] - p[1]) + (l[2] - p[0]) * (l[2] - p[0]);
        } else {
            if (u <= -t) return 0;
            dd = (d6 - p[1]) * (d6 - p[1]) + (d7 - p[0]) * (d7 - p[0]);
        }
    } else dd = (d6 - p[1]) * (d6 - p[1]) + (d7 - p[0]) * (d7 - p[0]);
    dd = sqrt(dd); *dist = dd;
    return dd < t;
}
static int pko_l_hv_ints(int k, const double *l1, const double *l2, double p[2], double *s, double *u) {   /* pko_l_hv_ints sys l.271-345 */
    const double *pl3 = l1 + 2, *pl4 = l1;
    if (k == 2) { pl3 = l1 + 3; pl4 = l1 + 1; }
    double d1 = *pl3, d2 = *pl4;
    if (1e-12 <= fabs(d1 - d2)) {
        double d7, d6;
        if (k == 1) d7 = l2[3] - l2[1]; else d7 = l2[2] - l2[0];
        if (1e-12 <= fabs(d7)) {
            double a = k == 2 ? l2[3] - l2[1] : l2[2] - l2[0];
            d6 = sqrt(a * a + d7 * d7);
            if (1e-12 <= d6 && 1.5e-10 <= fabs(d7 / d6)) {
                const double *a3 = k == 1 ? l2 + 1 : l2, *a4 = k == 1 ? l1 + 1 : l1;
                double v = (*a4 - *a3) / d7;
                *u = v;
                p[0] = (l2[2] - l2[0]) * v + l2[0];
                p[1] = (l2[3] - l2[1]) * *u + l2[1];
                *s = ((k == 2 ? p[1] : p[0]) - (k == 2 ? l1[1] : l1[0])) / (d1 - d2);
                return 1;
            }
        }
    }
    return 0;
}
/* pko_l_ints_tol sys l.382-535; the collinear-overlap branch's t_on_line_tol is only reached with |det| below tol: those parameters are never read here (ints_on_gt_new
   uses the point, the two pko_pt_on_gt_new tests decide) */
static int pko_l_ints_tol(const double l1[4], const double l2[4], double t, double p[2]) {
    double d21 = l1[2] - l1[0], d20, d18, d6, d1, d19, d2, d3, d7, d17, s, u;
    if (!(1e-12 < fabs(d21) || fabs(d21) == 1e-12)) return pko_l_hv_ints(2, l1, l2, p, &s, &u);
    d1 = l1[1]; d20 = l1[3] - d1;
    if (!(1e-12 < fabs(d20) || fabs(d20) == 1e-12)) return pko_l_hv_ints(1, l1, l2, p, &s, &u);
    d19 = l2[0]; d2 = l2[2]; d18 = d2 - d19;
    if (!(1e-12 < fabs(d18) || fabs(d18) == 1e-12)) return pko_l_hv_ints(2, l2, l1, p, &u, &s);
    d18 = l2[1]; d3 = l2[3]; d6 = d3 - d18;
    if (!(1e-12 < fabs(d6) || fabs(d6) == 1e-12)) return pko_l_hv_ints(1, l2, l1, p, &u, &s);
    d6 = d19 - l1[0];
    d17 = t * t;
    d19 = d19 - d2;
    d3 = d18 - d3;
    d18 = d18 - d1;
    d2 = d3 * d21 - d19 * d20;
    d1 = d6 * d3 - d18 * d19;
    d7 = d19 * d21 + d3 * d20;
    int b15, b16;
    if (d17 < fabs(d7) || d17 == fabs(d7)) { b16 = t == fabs(d2 / d7); b15 = t < fabs(d2 / d7); }
    else { b16 = d17 == fabs(d2); b15 = d17 < fabs(d2); }
    if (b15 || b16) {
        d1 = d1 / d2;
        p[0] = l1[0] + d21 * d1;
        p[1] = d20 * d1 + l1[1];
        return 1;
    }
    d21 = sqrt(d19 * d19 + d3 * d3);
    if (t < fabs(d21)) {
        d1 = d1 / d21;
        if (fabs(d1) <= t && t != fabs(d1)) {
            /* parallel, within tolerance: the midpoint of the nearest end pair (sys l.487-507) */
            double e1[4] = { l1[0], l1[1], l1[2], l1[3] }, e2[4] = { l2[0], l2[1], l2[2], l2[3] }, best = 1e38;
            int s11 = 1, s10 = 1;
            for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
                double d = sqrt((e2[2 * b + 1] - e1[2 * a + 1]) * (e2[2 * b + 1] - e1[2 * a + 1]) + (e2[2 * b] - e1[2 * a]) * (e2[2 * b] - e1[2 * a]));
                if (d <= best && best != d) { best = d; s10 = b; s11 = a; }
            }
            p[0] = (e2[2 * s10] + e1[2 * s11]) * 0.5;
            p[1] = (e2[2 * s10 + 1] + e1[2 * s11 + 1]) * 0.5;
            return 1;
        }
    }
    return 0;
}
static int pko_la_ints_new(const double l[4], const double a[5], double t, double p1[2], double p2[2]) {  /* pko_la_ints_new sys l.631-700 */
    double d6 = l[1], d1 = l[0], d8 = l[3] - d6, d7 = l[2] - d1, d5 = d8 * d8 + d7 * d7, d4 = 0.0;
    if (d5 == 0.0) return 0;
    d5 = ((a[1] - d6) * d8 + (a[0] - d1) * d7) / d5;
    d7 = d1 * (1.0 - d5) + d5 * l[2];
    d1 = d6 * (1.0 - d5) + d5 * l[3];
    d6 = a[2];
    d5 = d6 * d6 - ((d1 - a[1]) * (d1 - a[1]) + (d7 - a[0]) * (d7 - a[0]));
    if (!(-(d6 * 2.0 * t) <= d5)) return 0;
    double h = 0.0;
    if (0.0 <= d5) h = sqrt(d5);
    double dx = l[2] - l[0], dy = l[3] - l[1], len = sqrt(dx * dx + dy * dy);
    if (len == 0.0) { dx = 0.0; d4 = 0.0; } else { dx = dx / len; d4 = dy / len; }
    p1[0] = d7 + dx * h; p1[1] = d4 * h + d1;
    p2[0] = d7 + dx * -h; p2[1] = d4 * -h + d1;
    return 1;
}
static int pko_pt_on_gt_new(const double p[2], const RmEnt *e, double t) {   /* pko_pt_on_gt_new more l.194 */
    double d;
    if (e->id == 'L') return pko_dist_to_finite_line(p, e->a, t, &d);
    return pko_pt_on_arc(p, e->a, t);
}
/* RmEnt view of an entity for the mccore helpers of cam2d_remach.c (la_ints_new, l_ints_tol, pt_on_gt_new) */
static void pko_rm(const Gt *e, RmEnt *r) {
    memset(r, 0, sizeof *r);
    if (e->t == 'L') { r->id = 'L'; r->a[0] = e->a.x; r->a[1] = e->a.y; r->a[2] = e->b.x; r->a[3] = e->b.y; }
    else { r->id = 'A'; r->a[0] = e->c.x; r->a[1] = e->c.y; r->a[2] = e->r; r->a[3] = e->sa; r->a[4] = e->sw; }
}
/* ints_gt of the unbounded line L and an entity (sys l.150-238): the intersection nearest the hint point q (the first output) */
static int pko_ints(const PkoLine *L, const Gt *e, PV q, PV *out) {
    double l[4] = { L->a.x, L->a.y, L->b.x, L->b.y }; RmEnt r; pko_rm(e, &r); double p[2], p2[2];
    if (e->t == 'L') { if (!pko_l_ints_tol(l, r.a, g_nc_tol, p)) return 0; *out = pv(p[0], p[1]); return 1; }
    if (!pko_la_ints_new(l, r.a, g_nc_tol, p, p2)) return 0;
    double d1 = (p[0] - q.x) * (p[0] - q.x) + (p[1] - q.y) * (p[1] - q.y), d2 = (p2[0] - q.x) * (p2[0] - q.x) + (p2[1] - q.y) * (p2[1] - q.y);
    if (d2 <= d1) *out = pv(p2[0], p2[1]); else *out = pv(p[0], p[1]);
    return 1;
}
static int pko_on_line(PV p, const PkoLine *L) { RmEnt r; Gt g = gt_line(L->a, L->b); pko_rm(&g, &r); double q[2] = { p.x, p.y }; return pko_pt_on_gt_new(q, &r, g_nc_tol); }
static int pko_on_gt(PV p, const Gt *e) { RmEnt r; pko_rm(e, &r); double q[2] = { p.x, p.y }; return pko_pt_on_gt_new(q, &r, g_nc_tol); }

/* trim_gt_to_pt (mccore 0x1805d6a90, disassembly). Line: the start (end = 0) or end moved to p, ok = the direction kept (dot > 0, strict). Arc: the angle of p (0 when within 1e-6 of the centre, else atan2 brought to [0, 2 pi)),
   the sweep recomputed and brought into [0, 2 pi] (sw >= 0) or [-2 pi, 0] by FUN_1804b4440; ok when both the new and the old sweep are below 6.195918844579867 (0x180d805a8), else when |r sw'| < |r sw| + nc_tol
   (the data word 0x1812b5b68 is mccore's exported ?nc_tol@@3NA). The entity is unchanged when not ok. */
static int pko_trim(Gt *e, PV p, int end) {
    if (e->t == 'L') {
        PV d0 = pv(e->b.x - e->a.x, e->b.y - e->a.y); Gt g = *e;
        if (end) g.b = p; else g.a = p;
        PV d1 = pv(g.b.x - g.a.x, g.b.y - g.a.y);
        if (!(d1.x * d0.x + d1.y * d0.y > 0.0)) return 0;
        *e = g; return 1;
    }
    double dx = p.x - e->c.x, dy = p.y - e->c.y, a = 0.0;
    if (!(1e-12 > dx * dx + dy * dy)) { a = atan2(dy, dx); if (0.0 > a) a += 6.283185307179586; }
    double sa = e->sa, sw = e->sw, ee = sw + sa, nsa = sa, nsw;
    if (!end) { nsa = a; nsw = ee - a; } else nsw = a - sa;
    double lo = 0.0 > sw ? -6.283185307179586 : 0.0, hi = 0.0 > sw ? 0.0 : 6.283185307179586;   /* FUN_1804b4440 (lo, hi, &v), disassembly 0x1804b4440 */
    while (nsw > hi) nsw += -6.283185307179586;
    while (lo > nsw) nsw += 6.283185307179586;
    double K = 6.195918844579867;
    if (!((K > fabs(nsw) && K > fabs(sw)) || fabs(e->r * sw) + g_nc_tol > fabs(e->r * nsw))) return 0;
    e->sa = nsa; e->sw = nsw;
    return 1;
}
/* aa_ints_new (systypes 0x180001fb0, disassembly): the circles of a and b, centre distance d. d below t: a pair of radii within t gives a's two end points (0x180001140), else none; beyond tangency by more than t: none. Otherwise the
   points c1 + x u +- h perp(u), x = (r1^2 - r2^2 + d^2) / 2d (h^2 < 0: h = 0 and x = sign(x) r1) */
static int pko_aa_ints(const Gt *a, const Gt *b, double t, PV *p1, PV *p2) {
    double r1 = a->r, r2 = b->r, ex = a->c.x - b->c.x, ey = a->c.y - b->c.y, d2 = ex * ex + ey * ey, d = sqrt(d2), dr = fabs(r1 - r2);
    if (t > d) {
        if (!(t > dr)) return 0;
        *p1 = gt_start(a); *p2 = gt_end(a);
        return 1;
    }
    if (dr - d > t || d - r1 - r2 > t) return 0;
    double x = (r1 * r1 - r2 * r2 + d2) / (d * 2.0), h2 = r1 * r1 - x * x, h = 0.0;
    if (0.0 > h2) x = (x == 0.0 ? 0.0 : x < 0.0 ? -1.0 : 1.0) * r1;
    else h = sqrt(h2);
    double ux = b->c.x - a->c.x, uy = b->c.y - a->c.y, ul = sqrt(ux * ux + uy * uy);
    if (ul == 0.0) { ux = 0.0; uy = 0.0; } else { ux /= ul; uy /= ul; }
    *p1 = pv(a->c.x + -uy * h + ux * x, a->c.y + uy * x + ux * h);
    *p2 = pv(a->c.x + -uy * -h + ux * x, a->c.y + uy * x + ux * -h);
    return 1;
}
/* ints_on_gt_new (mccore 0x1804b6c40, decomp/mccore_pock.c l.4040-4090) at tolerance nc_tol: do the two entities share a point? ints_gt (systypes 0x180001670: l_ints_tol / la_ints_new / aa_ints_new), one candidate for two lines, else two; each must lie on both */
static int pko_ints_on(const Gt *a, const Gt *b) {
    double t = g_nc_tol, pp[2][2]; int np = 2; RmEnt ra, rb; pko_rm(a, &ra); pko_rm(b, &rb);
    if (a->t == 'L' && b->t == 'L') { if (!pko_l_ints_tol(ra.a, rb.a, t, pp[0])) return 0; np = 1; }
    else if (a->t == 'A' && b->t == 'A') { PV p1, p2; if (!pko_aa_ints(a, b, t, &p1, &p2)) return 0; pp[0][0] = p1.x; pp[0][1] = p1.y; pp[1][0] = p2.x; pp[1][1] = p2.y; }
    else if (a->t == 'L') { if (!pko_la_ints_new(ra.a, rb.a, t, pp[0], pp[1])) return 0; }
    else { if (!pko_la_ints_new(rb.a, ra.a, t, pp[0], pp[1])) return 0; }
    for (int i = 0; i < np; i++) if (pko_pt_on_gt_new(pp[i], &ra, t) && pko_pt_on_gt_new(pp[i], &rb, t)) return 1;
    return 0;
}
/* bbox_gt (mccore 0x180012bc0, decomp/mccore_rectbnd.c l.575-650; build_pock_bound_info decomp/mccore_pock.c l.2100): the end points; for an arc also the quarter points k x 1.570796326794896 between its angles, skipped within 1e-6 of either end angle */
static void pko_bbox_gt(const Gt *e, double b[4]) {
    PV s = gt_start(e), z = gt_end(e);
    b[0] = z.x < s.x ? z.x : s.x; b[1] = z.y < s.y ? z.y : s.y; b[2] = s.x <= z.x ? z.x : s.x; b[3] = s.y <= z.y ? z.y : s.y;
    if (e->t != 'A') return;
    double lo = e->sa, hi = e->sa + e->sw;
    if (hi < lo) { double t = hi; hi = lo; lo = t; }
    double q = 1.570796326794896;
    for (int k = (int)ceil(lo / q); k <= (int)floor(hi / q); k++) {
        double a = k * q;
        if (1e-06 < fabs(a - lo) && 1e-06 < fabs(a - hi)) {
            double x = cos(a) * e->r + e->c.x, y = sin(a) * e->r + e->c.y;
            if (x <= b[0]) b[0] = x; if (y <= b[1]) b[1] = y; if (b[2] <= x) b[2] = x; if (b[3] <= y) b[3] = y;
        }
    }
}
/* pock_bound_intersect (mccore 0x1805e5550, decomp/mccore_pock.c l.2187-2260) and, where only its count > 0 is read, intersect_pock_bound (l.892-1075): any pair of entities whose boxes overlap within nc_tol (strict) sharing a point */
static int pko_bounds_x(const Gl *a, const Gl *b) {
    double t = g_nc_tol, ba[4], bb[4];
    for (int i = 0; i < a->n; i++) {
        pko_bbox_gt(&a->e[i], ba);
        for (int j = 0; j < b->n; j++) {
            pko_bbox_gt(&b->e[j], bb);
            if (bb[0] < t + ba[2] && bb[1] < t + ba[3] && ba[0] < t + bb[2] && ba[1] < t + bb[3] && pko_ints_on(&a->e[i], &b->e[j])) return 1;
        }
    }
    return 0;
}
/* FUN_1801e6530 l.334619 gouge_check_ent_pock_bound: the entity (as a bound of one entity) has no point in common with the bound (pock_bound_intersect reads the head of the list only) */
static int pko_no_gouge(const Gt *e, const Gls *b) {
    if (!b->n) return 1;
    Gl one = { (Gt *)e, 1, 1, 0, 0 };
    return !pko_bounds_x(&one, &b->l[0]);
}
