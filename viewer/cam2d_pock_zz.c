/* cam2d_pock_zz.c - Pocket roughing with cutting method 6 One Way and 0 Zigzag: mcmill "Pockzigr.cpp" / "POCKZIG.cpp",
 * ported from decomp/mcmill_all.c (line numbers below) and the mccore / systypes helpers dumped for this port into
 * ~/mastercam-re/work/tree_b6_zigzag/decomp/pocket_b6/zigzag_decomp_zz_core.c (find_path_around_island, next_p_index), decomp/pocket_b6/zigzag_decomp_zz_core2.c
 * (FUN_1805e1cc0, l_ints, la_ints, rotate_pock_bounds), decomp/pocket_b6/zigzag_decomp_zz_core3.c (t_on_arc, t_on_line), decomp/pocket_b6/zigzag_decomp_zz_sys.c
 * (systypes pt_on_arc, l_hv_ints); mccore pock_bound_line_inters / bound_line_inters / FUN_1805e33c0 / pt_in_ctour /
 * pt_in_machinable_area are in decomp/mccore_pock.c. Lost Ghidra arguments were read with objdump from
 * ~/mastercam-re/files/mcmill.dll ("dis ADDRESS"). Included by cam2d_pock.c (before gen_pocket_v2).
 *
 * Flow (FUN_1801ef830 l.340234, per pocket of the rough boundary): FUN_1801f3700 (l.343263) rotates the bounds by
 * -roughing angle (pock_prm +0x28 = record 0x1250, used directly by cos / sin: RADIANS, process_pocket l.323238 copies
 * it to rough_angle unconverted), builds the scan lines (FUN_1801f23e0 l.342243), groups them into regions
 * (FUN_1801f1fe0 l.341981) and links them: pock_prm +0x14 (record 0x123c) == 0 -> zigzag FUN_1801f15a0 (l.341514),
 * else one way FUN_1801f0d60 (l.341115). The moves are written in the rotated frame and rotated back (rot_rpt with
 * cos_rough / sin_rough, FUN_1801e32a0 l.332872).
 *
 * pock_bound fields used (decomp/mccore_pock.c; pgeo.h): +2 island, +3 counter-clockwise (the loop direction flag:
 * dis 1801f1dd4 and find_path_around_island use it with the island flag to turn "side" into a loop direction),
 * +0x20 / +0x30 bounding box min / max (FUN_1801f23e0 reads +0x28 / +0x38 as y range, FUN_1801f3940 the corners),
 * +0x48 perimeter (find_path_around_island), +0x60 next bound. Records (0x48 bytes, the temp file "pock1.tmp" of
 * FUN_1801f3700): +0 used, +4 scan line, +8 link to the next line, +0xc link to the previous line, +0x10 / +0x20 the two
 * ends A / B (A first along +x), +0x30 / +0x38 their bounds, +0x40 / +0x44 their entities. Regions (0xa0 bytes):
 * +0 not yet cut, +1 both ends of the first record on the same bound, +2 reversed, +4 orientation (FUN_1801f3b10),
 * +8 / +0xc first / last record, +0x10 / +0x58 copies of them.
 *
 * NOT DECODED here (refused with an error): FUN_1801e23c0 (pock_move_avoid_islands: a walk between two different
 * bounds, reached from FUN_1801f29c0 l.342510 when the two ends of a link are on different bounds).
 * Minimize tool burial (record 0x1259 = pock_prm +0x31) for zigzag (medax_zz): region choice FUN_1801f0540 (l.340654)
 * in FUN_1801f15a0, the bound flags +0x44 / +0x45 of FUN_1801f3940 (l.343328) and the burial branch of
 * find_path_around_island (mccore 0x1805e39c0 -> FUN_1805e35d0, decomp/pocket_b6/zigzag_decomp_zz_core.c); the bound's
 * lowest / highest points +0x20 / +0x30 from build_pock_bound_info (decomp/mccore_pock.c l.2042) + FUN_1805e41e0
 * (mccore, objdump). Product bit 0x10 (FUN_1801f2cc0's medial-axis link FUN_1801dae10) is taken clear as everywhere
 * in this port: with it clear machine_pocket (l.322515-322530) builds no medial axis for method 0.
 * Spec: decomp/medax/zzburial/zz_burial_spec.txt.
 * Ours (substitutes): MCCEPtInBnd (mcce.dll, not decoded) = on the loop when within tol (gl_closest), else even-odd
 * inside on a polygon sampled within tol / 2; constr_arc_new = the arc about the given centre from the start (radius
 * of the start) in the given direction, valid when the end is within nc_tol of that circle; the entity bounding boxes
 * of build_pock_bound_info = the exact extents of the line / arc.
 */

#define ZZ_MAXI 1000                                   /* pock_bound_line_inters max (FUN_1801f23e0: 1000) */

typedef struct { PV p; double t; int ent, bnd; char cls; } ZI;   /* pock_inters_rec: +0 class, +8 point, +0x18 t on the
                                                                    line, +0x20 entity, +0x28 bound (dis / l.342243) */
typedef struct { Gl l; int island, ccw; double perim, bb[4]; CPath poly;
                 PV lo, hi; int f44, f45; } ZB;   /* +0x20 / +0x30 lowest / highest point (build_pock_bound_info), +0x44 / +0x45
                                                     their FUN_1801f3940 flags (burial) */
typedef struct { ZB *b; int n; } ZBs;
typedef struct { int used, line, la, lb; PV a, b; int ba, bb, ea, eb; } ZS;          /* 0x48 record */
typedef struct { int used, flag, rev, o4, first, last; ZS fr, lr; } ZR;              /* 0xa0 region */
typedef struct { int k; PV p, c; int cw; } ZM;          /* k 0 feed line, 1 feed arc (centre c), 2 FUN_1801f35c0 (retract + plunge) */
typedef struct { ZM *m; int n, cap; PV cur, start; int started; char err[200]; } ZP;

static void zp_push(ZP *P, int k, PV p, PV c, int cw) {
    if (P->n == P->cap) { P->cap = P->cap ? P->cap * 2 : 256; P->m = realloc(P->m, P->cap * sizeof(ZM)); }
    ZM *m = &P->m[P->n++]; m->k = k; m->p = p; m->c = c; m->cw = cw; P->cur = p;
}

/* ------------------------------------------------------------------------------------------------ mccore geometry */
/* Equal (mccore 0x1804b3230, decomp/pocket_b6/zigzag_decomp_zz_core.c): both differences within tol and the distance below tol */
static int zz_equal(PV a, PV b) {
    double dx = b.x - a.x, dy = b.y - a.y, t = CAM_SYS_TOL;
    return dx <= t && -t <= dx && dy <= t && -t <= dy && dx * dx + dy * dy < t * t;
}
/* t_on_line (mccore 0x1804b48a0) */
static double zz_t_line(PV p, PV a, PV b) {
    double dx = b.x - a.x, dy = b.y - a.y, d = dy * dy + dx * dx;
    return CAM_SYS_TOL * CAM_SYS_TOL < d ? (dy * (p.y - a.y) + dx * (p.x - a.x)) / d : 0.0;
}
/* t_on_arc (mccore 0x1804b45f0): the angle of p as a fraction of the sweep, wrapped by +-2 pi, else the nearest */
static double zz_t_arc(PV p, const Gt *e) {
    if (fabs(e->sw) < 1e-6) return 0;
    double dx = p.x - e->c.x, dy = p.y - e->c.y, a = 0;
    if (1e-12 <= dy * dy + dx * dx) { a = atan2(dy, dx); if (a < 0) a += 2 * M_PI; }
    double t = (a - e->sa) / e->sw;
    if (t >= 0 && t <= 1) return t;
    double t2 = (a + 2 * M_PI - e->sa) / e->sw;
    if (t2 >= 0 && t2 <= 1) return t2;
    double t3 = (a - 2 * M_PI - e->sa) / e->sw;
    if (t3 >= 0 && t3 <= 1) return t3;
    double r = t, d1 = fmin(fabs(t), fabs(1 - t)), d2 = fmin(fabs(t2), fabs(1 - t2)), d3 = fmin(fabs(t3), fabs(1 - t3));
    if (d2 < d1) { r = t2; d1 = d2; }
    if (d1 <= d3) return r;
    return t3;
}
static double zz_t_gt(PV p, const Gt *e) { return e->t == 'L' ? zz_t_line(p, e->a, e->b) : e->t == 'A' ? zz_t_arc(p, e) : 0.0; }

/* l_hv_ints (systypes 0x180002330, decomp/pocket_b6/zigzag_decomp_zz_sys.c): k 1 = h horizontal, 2 = vertical; th / to on h / o */
static int zz_l_hv(int k, PV h0, PV h1, PV o0, PV o1, PV *pt, double *th, double *to) {
    double d1 = k == 2 ? h1.y : h1.x, d2 = k == 2 ? h0.y : h0.x;
    if (!(1e-12 <= fabs(d1 - d2))) return 0;
    double d7 = k == 1 ? o1.y - o0.y : o1.x - o0.x;
    if (!(1e-12 <= fabs(d7))) return 0;
    double ot = k == 2 ? o1.y - o0.y : o1.x - o0.x, d6 = sqrt(ot * ot + d7 * d7);
    if (!(1e-12 <= d6 && 1.5e-10 <= fabs(d7 / d6))) return 0;
    double t = ((k == 1 ? h0.y : h0.x) - (k == 1 ? o0.y : o0.x)) / d7;
    *to = t; pt->x = (o1.x - o0.x) * t + o0.x; pt->y = (o1.y - o0.y) * t + o0.y;
    *th = ((k == 1 ? pt->x : pt->y) - (k == 1 ? h0.x : h0.y)) / (d1 - d2);
    return 1;
}
/* l_ints (mccore 0x1804b3480, decomp/pocket_b6/zigzag_decomp_zz_core2.c): intersection of the infinite lines a and b; ta / tb on each */
static int zz_l_ints(PV a0, PV a1, PV b0, PV b1, PV *pt, double *ta, double *tb) {
    double dy = a1.y - a0.y, dx = a1.x - a0.x, L = dy * dy + dx * dx, tol = CAM_SYS_TOL;
    if (!(1e-24 <= L)) return 0;
    if (!(1e-24 <= dx * dx / L)) return zz_l_hv(2, a0, a1, b0, b1, pt, ta, tb);
    if (!(1e-24 <= dy * dy / L)) return zz_l_hv(1, a0, a1, b0, b1, pt, ta, tb);
    double bx = b1.x - b0.x, by = b1.y - b0.y, LB = by * by + bx * bx;
    if (!(1e-24 <= LB)) return 0;
    if (!(1e-24 <= bx * bx / LB)) return zz_l_hv(2, b0, b1, a0, a1, pt, tb, ta);
    if (!(1e-24 <= by * by / LB)) return zz_l_hv(1, b0, b1, a0, a1, pt, tb, ta);
    double d5 = b0.x - a0.x, d21 = b0.x - b1.x, d1 = b0.y - b1.y;
    double den = d1 * dx - d21 * dy, d15 = d5 * d1 - (b0.y - a0.y) * d21, d16 = d1 * dy + d21 * dx;
    int ok = tol * tol <= fabs(d16) ? tol <= fabs(den / d16) : tol * tol <= fabs(den);
    if (ok) {
        double t = d15 / den; *ta = t; pt->x = a0.x + dx * t; pt->y = a0.y + dy * t;
        *tb = ((b0.y - a0.y) * dx - d5 * dy) / den;
        return 1;
    }
    double d = sqrt(d21 * d21 + d1 * d1);
    if (!(tol < fabs(d))) return 0;
    double dist = d15 / d;
    if (!(fabs(dist) <= tol && tol != fabs(dist))) return 0;
    PV A[2] = { a0, a1 }, B[2] = { b0, b1 }; int ia = 1, ib = 1; double best = 1e38;   /* collinear: nearest ends */
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) { double q = hypot(B[j].y - A[i].y, B[j].x - A[i].x); if (q < best) { best = q; ib = j; ia = i; } }
    pt->x = (B[ib].x + A[ia].x) * 0.5; pt->y = (B[ib].y + A[ia].y) * 0.5;
    *ta = zz_t_line(*pt, a0, a1); *tb = zz_t_line(*pt, b0, b1);
    return 1;
}
/* la_ints (mccore 0x1804b6d90): the line through a0 a1 and the circle: both points (tangent within 2 r tol) */
static int zz_la_ints(PV a0, PV a1, PV c, double r, PV *p1, PV *p2) {
    double dx = a1.x - a0.x, dy = a1.y - a0.y, L2 = dx * dx + dy * dy;
    if (L2 <= 0) return 0;                                   /* proj_pt_ln: degenerate line */
    double t = ((c.x - a0.x) * dx + (c.y - a0.y) * dy) / L2;
    PV f = pv(a0.x + dx * t, a0.y + dy * t);
    double h2 = r * r - ((f.y - c.y) * (f.y - c.y) + (f.x - c.x) * (f.x - c.x));
    if (!(-(r * 2.0 * CAM_SYS_TOL) <= h2)) return 0;
    double h = h2 >= 0 ? sqrt(h2) : 0, L = sqrt(L2), ux = dx / L, uy = dy / L;
    *p1 = pv(ux * h + f.x, uy * h + f.y); *p2 = pv(ux * -h + f.x, uy * -h + f.y);
    return 1;
}
/* pt_on_arc (systypes 0x1800014e0): within tol of the circle and of the sweep (tol / r in angle) */
static int zz_pt_on_arc(PV p, const Gt *e, double tol) {
    if (!(fabs(hypot(e->c.x - p.x, e->c.y - p.y) - e->r) <= tol)) return 0;
    double dx = p.x - e->c.x, dy = p.y - e->c.y, a = 0;
    if (1e-12 <= dy * dy + dx * dx) { a = atan2(dy, dx); if (a < 0) a += 2 * M_PI; }
    double lo, hi, da = 1e-12 < e->r ? tol / e->r : 0;
    if (e->sw <= 0) { hi = e->sa; lo = hi + e->sw; } else { lo = e->sa; hi = lo + e->sw; }
    while (hi + da < a) a -= 2 * M_PI;
    while (a < lo - da) a += 2 * M_PI;
    return !(a < lo - da || hi + da < a);
}
static void zz_gt_bbox(const Gt *e, double bb[4]) {      /* ours: build_pock_bound_info's entity box, exact */
    PV s = gt_start(e), t = gt_end(e);
    bb[0] = fmin(s.x, t.x); bb[1] = fmin(s.y, t.y); bb[2] = fmax(s.x, t.x); bb[3] = fmax(s.y, t.y);
    if (e->t != 'A') return;
    double lo = e->sw < 0 ? e->sa + e->sw : e->sa, hi = lo + fabs(e->sw);
    for (int k = (int)floor(lo / (M_PI / 2)) + 1; k * (M_PI / 2) < hi; k++) {
        double a = k * (M_PI / 2); PV q = pv(e->c.x + e->r * cos(a), e->c.y + e->r * sin(a));
        bb[0] = fmin(bb[0], q.x); bb[1] = fmin(bb[1], q.y); bb[2] = fmax(bb[2], q.x); bb[3] = fmax(bb[3], q.y);
    }
}

/* FUN_1805e1cc0 (decomp/pocket_b6/zigzag_decomp_zz_core2.c): insert the intersection in t order along the line; a point of the same bound
   within tol of a neighbour (the record before / at / after the slot) is not added again */
static void zz_add(PV l0, PV l1, PV p, int ent, int bnd, ZI *rec, int *n) {
    double t = zz_t_line(p, l0, l1); int s13 = 1, cnt = *n;
    for (int i = 1; i <= cnt; i++) if (0.0 < t - rec[i].t) s13 = i + 1;
    int s10 = 1 < s13 - 1 ? s13 - 1 : 1, s12 = s13 + 1 < cnt ? s13 + 1 : cnt;
    for (int i = s10; i <= s12; i++) if (rec[i].bnd == bnd && hypot(rec[i].p.x - p.x, rec[i].p.y - p.y) < CAM_SYS_TOL) return;
    for (int i = cnt; i >= s13; i--) rec[i + 1] = rec[i];
    rec[s13].t = t; rec[s13].ent = ent; rec[s13].bnd = bnd; rec[s13].p = p; rec[s13].cls = 0;
    (*n)++;
}
/* bound_line_inters (mccore 0x1805e2120, decomp/mccore_pock.c l.4094): a horizontal line only tests the entities
   whose y range +- nc_tol holds it; lines with t on the entity in (-2e-7, 1.0000002), arcs both points on the arc
   within nc_tol */
static void zz_bound_line(const ZBs *B, int bi, PV l0, PV l1, int max, ZI *rec, int *n) {
    const Gl *l = &B->b[bi].l; double nct = g_nc_tol, dy = fabs(l1.y - l0.y);
    int hz = dy <= 1e-12 && dy != 1e-12; double yh = l1.y;
    for (int i = 0; i < l->n; i++) {
        const Gt *e = &l->e[i];
        if (hz) { double bb[4]; zz_gt_bbox(e, bb); if (!(yh <= nct + bb[3] && bb[1] - nct <= yh)) continue; }
        if (e->t == 'L') {
            PV q; double ta, tb;
            if (zz_l_ints(l0, l1, e->a, e->b, &q, &ta, &tb) && *n < max - 1 && -2e-07 < tb && tb < 1.0000002) zz_add(l0, l1, q, i, bi, rec, n);
        } else if (e->t == 'A') {
            PV p1, p2;
            if (zz_la_ints(l0, l1, e->c, e->r, &p1, &p2) && *n < max - 1) {
                if (zz_pt_on_arc(p1, e, nct)) zz_add(l0, l1, p1, i, bi, rec, n);
                if (*n < max - 1 && zz_pt_on_arc(p2, e, nct)) zz_add(l0, l1, p2, i, bi, rec, n);
            }
        }
    }
}
/* pock_bound_line_inters (mccore 0x1805e57b0, l.1102): up to tries attempts, the line shifted sideways by
   ((k + 1) / 2) eps, odd k to the left, even to the right (FUN_1805e33c0 l.4229), until the count is even;
   0 = even, -1 = the last attempt was odd (its intersections stay) */
static int zz_inters(const ZBs *B, PV a0, PV a1, double eps, int tries, ZI *rec, int *n) {
    for (int k = 0; k < tries; k++) {
        PV l0 = a0, l1 = a1;
        if (k > 0) {
            double dx = a1.x - a0.x, dy = a1.y - a0.y, L = sqrt(dx * dx + dy * dy), nx = 0, ny = 0;
            if (L != 0) { nx = -dy / L; ny = dx / L; }
            if ((k & 1) == 0) { nx = -nx; ny = -ny; }
            double s = (double)((k + 1) / 2) * eps;
            l0 = pv(l0.x + nx * s, l0.y + ny * s); l1 = pv(l1.x + nx * s, l1.y + ny * s);
        }
        *n = 0;
        for (int bi = 0; bi < B->n; bi++) zz_bound_line(B, bi, l0, l1, ZZ_MAXI, rec, n);
        if (!(*n & 1)) return 0;
    }
    return -1;
}
/* pt_in_ctour (mccore 0x1805e6e30): 0 on (within t1 of an entity start, or MCCEPtInBnd "on"), 1 inside, -1 outside */
static int zz_in_ctour(const ZB *b, PV p, double t1, double t2) {
    for (int i = 0; i < b->l.n; i++) { PV s = gt_start(&b->l.e[i]); if (sqrt((p.x - s.x) * (p.x - s.x) + (p.y - s.y) * (p.y - s.y)) < t1) return 0; }
    if (gl_closest(&b->l, p, NULL, NULL, NULL) <= t2) return 0;   /* ours: MCCEPtInBnd substitute */
    return pip(&b->poly, p.x, p.y) ? 1 : -1;
}
/* pt_in_machinable_area (mccore 0x1805e6f20, l.1334): outside when outside the outer bound(s) (unless already inside
   one) or inside an island; *on when on any bound */
static int zz_in_area(const ZBs *B, PV p, int *on, double t1, double t2) {
    int inside = 0; if (on) *on = 0;
    for (int i = 0; i < B->n; i++) {
        int s = zz_in_ctour(&B->b[i], p, t1, t2);
        if (s == 0 && on) *on = 1;
        if (!B->b[i].island) { if (s == 1) inside = 1; else if (!inside && s == -1) return 0; }
        else if (s == 1) return 0;
    }
    return 1;
}
/* FUN_1801f0c80 (l.341079): the midpoint strictly inside the first bound and strictly outside the others */
static int zz_mid_ok(const ZBs *B, PV a, PV b) {
    PV m = pv((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
    for (int i = 0; i < B->n; i++) {
        int s = zz_in_ctour(&B->b[i], m, g_nc_tol, CAM_SYS_TOL);
        if (i == 0) { if (s < 1) return 0; } else if (-1 < s) return 0;
    }
    return 1;
}
/* FUN_1801e0fd0 (l.331634): a straight move stays inside: no intersection (tol shift) strictly between its ends that
   is farther than nc_tol from both ends, and the midpoint in the machinable area */
static int zz_line_inside(const ZBs *B, PV a, PV b) {
    if (zz_equal(a, b)) return 1;
    static ZI rec[ZZ_MAXI + 2]; int n = 0;
    zz_inters(B, a, b, CAM_SYS_TOL, 20, rec, &n);
    for (int k = 1; k <= n; k++) {
        if (rec[k].t <= -2e-07 || 1.0000002 <= rec[k].t) continue;
        if (g_nc_tol < hypot(a.y - rec[k].p.y, a.x - rec[k].p.x) && g_nc_tol < hypot(b.y - rec[k].p.y, b.x - rec[k].p.x)) return 0;
    }
    return zz_in_area(B, pv((a.x + b.x) * 0.5, (a.y + b.y) * 0.5), NULL, g_nc_tol, CAM_SYS_TOL);
}

/* ------------------------------------------------------------------------------------------------ moves */
/* FUN_1801e2270 (l.332291): a feed line along the boundary, skipped below tol */
static void zz_line_to(ZP *P, PV q) { if (CAM_SYS_TOL <= hypot(P->cur.x - q.x, P->cur.y - q.y)) zp_push(P, 0, q, pv(0, 0), 0); }
/* constr_arc_new substitute (ours, see the header): sweep from s to e about c in the direction */
static double zz_arc_sweep(PV s, PV e, PV c, int cw) {
    double a0 = atan2(s.y - c.y, s.x - c.x), a1 = atan2(e.y - c.y, e.x - c.x), d = cw ? a0 - a1 : a1 - a0;
    while (d < 0) d += 2 * M_PI;
    while (d >= 2 * M_PI) d -= 2 * M_PI;
    return d;
}
/* FUN_1801e1cb0 (l.332119): an arc move; a chord below min_arc_length or an arc shorter than it is a line */
static int zz_arc_to(ZP *P, PV q, PV c, int cw) {
    double ch = hypot(P->cur.x - q.x, P->cur.y - q.y);
    if (g_min_arc <= ch) {
        double r = hypot(P->cur.x - c.x, P->cur.y - c.y);
        if (fabs(hypot(q.x - c.x, q.y - c.y) - r) > g_nc_tol) { snprintf(P->err, sizeof P->err, "pocket: arc move construction failed (FUN_1801e1cb0 -1)"); return 0; }
        double sw = zz_arc_sweep(P->cur, q, c, cw);
        if (g_min_arc <= fabs(r * sw)) { zp_push(P, 1, q, c, cw); return 1; }
    }
    zz_line_to(P, q);
    return 1;
}
/* next_p_index (mccore 0x1805e5260) */
static int zz_next(int n, int i, int d) { int k = i + d; if (k < 0) k = n - 1; if (n <= k) k = 0; return k; }

/* FUN_1801f29c0 (l.342510): along bound from -> to. Same bound: entity by entity in direction dir (0: from the order
   of the two points on from's entity); one entity: straight to the point when it lies ahead. Different bounds:
   FUN_1801e0f80 -> FUN_1801e23c0 NOT DECODED (refused). */
static int zz_walk(ZP *P, const ZBs *B, const ZI *from, const ZI *to, int dir) {
    if (!(from->bnd == to->bnd && from->bnd >= 0)) {
        snprintf(P->err, sizeof P->err, "pocket %s: link between two bounds (FUN_1801e0f80 -> FUN_1801e23c0 pock_move_avoid_islands) NOT DECODED", "zigzag / one way");
        return 0;
    }
    const Gl *l = &B->b[from->bnd].l; int n = l->n, i = from->ent, ti = to->ent;
    if (i < 0) return 1;                                  /* uVar9 = 0: no move */
    if (!(i < n && ti >= 0 && ti < n)) return 1;
    int ok;
    if (i == ti || dir == 0) {
        double tf = zz_t_gt(from->p, &l->e[i]), tt = zz_t_gt(to->p, &l->e[i]);
        int s = tt <= tf ? -1 : 1;
        if (dir == 0) { ok = 1; dir = s; } else ok = s == dir;
    } else ok = 0;
    for (int guard = 0; guard <= n + 1; guard++) {
        int done = 0, next; PV target;
        if (i == ti && ok) { done = 1; target = to->p; next = ti; }
        else { next = zz_next(n, i, dir); target = dir < 0 ? gt_start(&l->e[i]) : gt_end(&l->e[i]); }
        const Gt *e = &l->e[i];
        if (e->t == 'L') zz_line_to(P, target);
        else if (e->t == 'A') { int cw = dir < 0 ? 0.0 <= e->sw : e->sw < 0.0; if (!zz_arc_to(P, target, e->c, cw)) return 0; }
        ok = 1; i = next;
        if (done) return 1;
    }
    snprintf(P->err, sizeof P->err, "pocket: boundary walk did not reach its end");
    return 0;
}
/* FUN_1805e5310 (decomp/pocket_b6/zigzag_decomp_zz_core.c): arc length from s to e about c (constr_arc_new; a sweep above 2 pi - 0.001 loses
   a turn) */
static double zz_arc_len(PV s, PV e, PV c, int cw) {
    double sw = zz_arc_sweep(s, e, c, cw), r = hypot(s.x - c.x, s.y - c.y);
    if (6.282185307179586 < sw) sw -= 6.283185307179586;
    return fabs(r * sw);
}
/* ---- Minimize tool burial (record 0x1259 = pock_prm +0x31), zigzag only (medax_zz) */
/* FUN_1805e2070 (mccore, objdump 1805e2070): angle a within the arc's sweep: lo = sa, hi = sa + sw (swapped when
   sw < 0); a -= 2 pi while a > lo + 2 pi, a += 2 pi while a < lo; lo <= a <= hi */
static int zz_ang_in(const Gt *e, double a) {
    double lo = e->sa, hi = e->sa + e->sw;
    if (0.0 > e->sw) { double t = lo; lo = hi; hi = t; }
    double l2 = lo + 6.283185307179586;
    while (a > l2) a += -6.283185307179586;
    while (lo > a) a += 6.283185307179586;
    return !(a < lo) && !(hi < a);
}
/* FUN_1805e41e0 (mccore, objdump 1805e41e0-1805e43b7): the entity's lowest / highest point among start (get_ep 1),
   end (get_ep 0) and, for an arc, the top (cx, cy + r) / bottom (cx, cy - r) when their angle (-2 pi + k 2 pi +
   pi/2 / 3 pi/2, k = 0..2, each added once) lies in the sweep; first one wins ties (strict compares) */
static void zz_extreme(const Gt *e, PV *lo, PV *hi) {
    PV q[4]; int n = 0;
    q[n++] = gt_start(e); q[n++] = gt_end(e);
    if (e->t == 'A') {
        int top = 0, bot = 0; double a = -6.283185307179586;
        for (int k = 0; k < 3; k++) {
            if (!top && zz_ang_in(e, a + 1.570796326794896)) { q[n++] = pv(e->c.x, e->c.y + e->r); top = 1; }
            if (!bot && zz_ang_in(e, a + 4.712388980384689)) { q[n++] = pv(e->c.x, e->c.y - e->r); bot = 1; }
            a += 6.283185307179586;
        }
    }
    *lo = q[0]; *hi = q[0];
    for (int i = 1; i < n; i++) { if (lo->y > q[i].y) *lo = q[i]; if (q[i].y > hi->y) *hi = q[i]; }
}
/* build_pock_bound_info (decomp/mccore_pock.c l.2079-2096): +0x20 = the lowest point (entity 0, then strictly
   lower), +0x30 = the highest (entity 0, then strictly higher) */
static void zz_bound_extremes(ZB *b) {
    for (int i = 0; i < b->l.n; i++) {
        PV lo, hi; zz_extreme(&b->l.e[i], &lo, &hi);
        if (lo.y < b->lo.y || i == 0) b->lo = lo;
        if (b->hi.y < hi.y || i == 0) b->hi = hi;
    }
}
/* FUN_1801f3940 (l.343328): for every bound, its lowest (+0x20 -> flag +0x44) / highest (+0x30 -> +0x45) point, flag
   not yet set: set when |point.y - A.y| < 1.5 step (pock_prm +0x18) and point.x strictly between A.x and B.x (the
   record's stored ends). Called by FUN_1801f15a0 before every move of a record (dis 1801f1c8f), burial or not. */
static void zz_f3940(ZBs *B, PV a, PV b, double step) {
    for (int i = 0; i < B->n; i++) {
        ZB *z = &B->b[i];
        for (int s = 0; s < 2; s++) {
            int *f = s ? &z->f45 : &z->f44; PV c = s ? z->hi : z->lo;
            if (*f) continue;
            if (fabs(c.y - a.y) < step * 1.5 && (c.x - a.x) * (c.x - b.x) < 0.0) *f = 1;
        }
    }
}
/* proj_pt_gt (mccore 0x1804b4280): line -> proj_pt_ln (decomp/mccore_filter.c l.4912: infinite line, ok unless
   degenerate); arc -> proj_pt_ar (objdump 1804b41b0: radial, ok when the distance to the centre >= tol) */
static int zz_proj(PV p, const Gt *e, PV *q) {
    if (e->t == 'L') {
        double dx = e->b.x - e->a.x, dy = e->b.y - e->a.y, d = dy * dy + dx * dx;
        if (d == 0.0) return 0;
        double t = ((p.y - e->a.y) * dy + (p.x - e->a.x) * dx) / d;
        *q = pv((1.0 - t) * e->a.x + t * e->b.x, (1.0 - t) * e->a.y + t * e->b.y);
        return 1;
    }
    if (e->t == 'A') {
        double dx = p.x - e->c.x, dy = p.y - e->c.y, d = sqrt(dx * dx + dy * dy);
        if (CAM_SYS_TOL > d) return 0;
        double k = e->r / d; *q = pv(e->c.x + dx * k, e->c.y + dy * k);
        return 1;
    }
    return 0;
}
/* FUN_1805e35d0 (decomp/pocket_b6/zigzag_decomp_zz_core.c l.292-518): positions along the bound (t x length_gt +
   the lengths before) of 0 the highest point, 1 the lowest (first entity whose projection is within nc_tol, t in
   (-1e-12, 1.000000000001)), 2 'from' / 3 'to' (on their own entity index). u = (pos0 <= pos1), negated for a
   clockwise bound; the four in increasing position (ties: lower index; a point not found is never taken - after the
   found ones index 0 repeats, out values are written by then): a corner toggles u, 'from' / 'to' store u in out[0] /
   out[1]. (param_5, the four partial lengths, is not read by find_path_around_island: not kept.) */
static void zz_e35d0(const ZB *b, const ZI *from, const ZI *to, int out[2]) {
    const Gl *l = &b->l; int fnd[4] = { 0, 0, 0, 0 }; double val[4] = { 0, 0, 0, 0 }, cum = 0;
    PV pt[4] = { b->hi, b->lo, from->p, to->p }; int ent[4] = { -1, -1, from->ent, to->ent };
    for (int i = 0; i < l->n; i++) {
        const Gt *e = &l->e[i]; double len = gt_len(e);
        for (int s = 0; s < 4; s++) {
            if (fnd[s]) continue;
            int hit = 0;
            if (s < 2) {
                PV q;
                if (zz_proj(pt[s], e, &q) && sqrt((q.y - pt[s].y) * (q.y - pt[s].y) + (q.x - pt[s].x) * (q.x - pt[s].x)) < g_nc_tol) {
                    double t = zz_t_gt(pt[s], e);
                    if (-1e-12 < t && t < 1.000000000001) hit = 1;
                }
            } else if (i == ent[s]) hit = 1;
            if (hit) { fnd[s] = 1; val[s] = zz_t_gt(pt[s], e) * len + cum; }
        }
        cum += len;
    }
    int u = val[0] <= val[1];
    if (!b->ccw) u = !(val[0] <= val[1]);
    out[0] = out[1] = -1;                                   /* ours: uninitialised in Mastercam (local_124); only read when
                                                               both were written (an index out of range: dir 1 below) */
    for (int it = 0; it < 4; it++) {
        int k = 0; double m = 1e38;
        for (int s = 0; s < 4; s++) if (fnd[s] && val[s] < m) { m = val[s]; k = s; }
        fnd[k] = 0;
        if (k < 2) u = 1 - u;
        else if (k == 2) out[0] = u;
        else out[1] = u;
    }
}

/* find_path_around_island (mccore 0x1805e39c0, decomp/pocket_b6/zigzag_decomp_zz_core.c l.3-187): burial (param_4,
   FUN_1801f2cc0: param_6 && pock_prm +0x31 && a bound) and exactly one of the bound's flags +0x44 / +0x45 set:
   FUN_1805e35d0; when 'from' and 'to' lie on different sides of the lowest / highest points: dir = +0x44 ? 1 : -1,
   negated when out[0] == 1 and for a clockwise bound; the length is then the walk in that direction. Otherwise the
   length along the bound from 'from' forward to 'to'; same entity -> dir 0; else forward when not longer than half
   the perimeter, else backward (perimeter - length). Entities out of range: dir 1, length not set (0 here). The walk
   steps with next_p_index(dir) and measures to each entity's END point (get_ep(.., 0)), also when going backward
   (as decoded). */
static void zz_fpai(const ZBs *B, int bnd, const ZI *from, const ZI *to, int burial, double *len, int *dir) {
    *dir = 1; *len = 0;
    if (bnd < 0) return;
    const ZB *b = &B->b[bnd]; const Gl *l = &b->l; int n = l->n, i = from->ent, ti = to->ent, bur = 0;
    if (burial && b->f45 != b->f44) {
        int o[2]; zz_e35d0(b, from, to, o);
        if (o[0] != o[1]) {
            *dir = b->f44 == 0 ? -1 : 1;
            if (o[0] == 1) *dir = -*dir;
            if (!b->ccw) *dir = -*dir;
            bur = 1;
            if (getenv("ZZ_DEBUG")) fprintf(stderr, "burial fpai: bound %d f44 %d f45 %d out %d %d -> dir %d\n", bnd, b->f44, b->f45, o[0], o[1], *dir);
        }
    }
    if (i < 0 || n <= i || ti < 0 || n <= ti) { *dir = 1; return; }
    double d = 0; PV cur = from->p;
    for (int guard = 0; guard <= n + 1; guard++) {
        int more = i != ti; PV prev = cur;
        cur = more ? gt_end(&l->e[i]) : to->p;
        const Gt *e = &l->e[i]; double s = 0;
        if (e->t == 'L') s = hypot(prev.y - cur.y, prev.x - cur.x);
        else if (e->t == 'A' && !zz_equal(cur, prev)) s = zz_arc_len(prev, cur, e->c, e->sw < 0.0);
        d += s; i = zz_next(n, i, *dir);
        if (!more) break;
    }
    if (bur) { *len = d; return; }
    if (from->ent == to->ent) { *dir = 0; *len = d; }
    else if (d <= b->perim * 0.5) { *dir = 1; *len = d; }
    else { *dir = -1; *len = b->perim - d; }
}
/* FUN_1801f2cc0 (l.342711): from -> to as straight pieces between the line's boundary crossings (strictly inside the
   move, the first / last dropped when within nc_tol of its end); a piece whose midpoint is not inside goes around the
   bound (find_path_around_island + FUN_1801f29c0). write = 0: the length only (pieces by parity when the count is
   even). *from becomes the end. burial = param_6 && pock_prm +0x31 (zigzag: param_6 = 1, dis 1801f19b8 / 1801f1e82;
   one way: 0, dis 1801f1429): find_path_around_island's param_4 when the piece's bound exists (l.342920-342928).
   Product bit 0x10 (the FUN_1801dae10 medial-axis link at the top) is taken clear. */
static int zz_link(ZP *P, const ZBs *B, ZI *from, const ZI *to, int write, int burial, double *len) {
    static ZI I[ZZ_MAXI + 2]; int c = 0;
    *len = 0;
    zz_inters(B, from->p, to->p, g_nc_tol * 0.1, 20, I, &c);
    int s8 = 0, s14 = 0;
    for (int k = 1; k <= c; k++) if (1e-12 < I[k].t && I[k].t < 0.999999999999) { s14 = k; if (s8 == 0) s8 = k; }
    int found = 0;
    if (c > 0) {
        if (0 < s8 && 0 < s14 && s8 < s14) {
            if (hypot(I[s8].p.y - from->p.y, I[s8].p.x - from->p.x) < g_nc_tol) s8++;
            if (s8 < s14 && hypot(I[s14].p.y - to->p.y, I[s14].p.x - to->p.x) < g_nc_tol) s14--;
        }
        if (s8 != 0 && s14 != 0) { s14++; found = 1; }
    }
    if (!found) { s8 = 2; s14 = 2; }
    if (getenv("ZZ_DEBUG") && write) { fprintf(stderr, "link %.4f %.4f (e%d b%d) -> %.4f %.4f (e%d b%d): %d inters, pieces %d..%d\n", from->p.x, from->p.y, from->ent, from->bnd, to->p.x, to->p.y, to->ent, to->bnd, c, s8, s14);
        for (int k = 1; k <= c; k++) fprintf(stderr, "   I%d %.4f %.4f t %.6f e%d b%d\n", k, I[k].p.x, I[k].p.y, I[k].t, I[k].ent, I[k].bnd); }
    ZI cur = *from;
    for (; s8 <= s14; s8++) {
        ZI prev = cur, tg = s8 == s14 ? *to : I[s8];
        int ok = (!write && !(c & 1)) ? !(s8 & 1) : zz_mid_ok(B, prev.p, tg.p);
        if (ok) {
            if (write) zp_push(P, 0, tg.p, pv(0, 0), 0);
            *len += hypot(tg.p.y - prev.p.y, tg.p.x - prev.p.x);
        } else {
            double l; int dir; zz_fpai(B, tg.bnd, &prev, &tg, burial && tg.bnd >= 0, &l, &dir);
            *len += l;
            if (write && !zz_walk(P, B, &prev, &tg, dir)) return 0;
        }
        cur = tg;
    }
    if (write) *from = cur;
    return 1;
}

/* ------------------------------------------------------------------------------------------------ scan lines */
/* FUN_1801f03c0 (l.340593): class of the piece a -> b: the first sample (1/2, then 1/4 3/4, ... sixteenths) not on a
   bound decides 'I' / 'O'; outside -> 'O'; all on a bound -> 'B' */
static char zz_class(const ZBs *B, PV a, PV b) {
    for (int m = 2; m <= 16; m *= 2)
        for (int i = 1; i < m; i += 2) {
            PV q = pv((b.x - a.x) * ((double)i / m) + a.x, (b.y - a.y) * ((double)i / m) + a.y); int on;
            if (!zz_in_area(B, q, &on, CAM_SYS_TOL, CAM_SYS_TOL)) return 'O';
            if (!on) return 'I';
        }
    return 'B';
}
/* FUN_1801f0200 (l.340493): classify the pieces between consecutive intersections ('B' counts as inside, the last
   is 'O'), merge equal neighbours (the later one is dropped), drop a leading 'O' */
static void zz_classify(const ZBs *B, ZI *r, int *n) {
    for (int k = 1; k < *n; k++) { r[k].cls = zz_class(B, r[k].p, r[k + 1].p); if (r[k].cls == 'B') r[k].cls = 'I'; }
    if (*n) r[*n].cls = 'O';
    for (int i = 1; i < *n; ) {
        if (r[i].cls == r[i + 1].cls) { for (int j = i + 1; j < *n; j++) r[j] = r[j + 1]; (*n)--; }
        else i++;
    }
    if (*n && r[1].cls == 'O') { for (int j = 1; j < *n; j++) r[j] = r[j + 1]; (*n)--; }
}

typedef struct { ZS *s; int n, cap; } ZSv;
static void zsv_push(ZSv *v, ZS s) { if (v->n + 1 >= v->cap) { v->cap = v->cap ? v->cap * 2 : 256; v->s = realloc(v->s, v->cap * sizeof(ZS)); } v->s[++v->n] = s; }

/* FUN_1801f23e0 (l.342243): lines at y = ymin + 2 tol + i (range / (n - 1)), n = floor(range / step) + 2 (>= 2), range
   = ymax - ymin - 4 tol of the first bound; each from x 0 to 100 through the bounds (eps 0.1 nc_tol, 20 tries; an odd
   count after them = no segment), the inside pieces are the records; then each record is linked to the one record of
   the previous line whose x range overlaps it (two overlaps on either side -> -1 on both) */
static void zz_scan(const ZBs *B, double step, ZSv *S) {
    static ZI rec[ZZ_MAXI + 2];
    const ZB *o = &B->b[0]; double tol = CAM_SYS_TOL;
    double y0 = tol * 2.0 + o->bb[1], range = (o->bb[3] - tol * 2.0) - y0;
    int nl = (int)floor(range / step) + 2; if (nl < 2) nl = 2;
    int ps = 0, pe = -1;
    for (int i = 1; i <= nl; i++) {
        double y = (double)(i - 1) * (range / (double)(nl - 1)) + y0; int c = 0;
        if (zz_inters(B, pv(0, y), pv(100, y), g_nc_tol * 0.1, 20, rec, &c) != 0) c = 0;
        zz_classify(B, rec, &c);
        int cs = S->n + 1;
        for (int k = 1; k <= c / 2; k++) {
            ZS s = { 0 }; s.line = i; s.a = rec[2 * k - 1].p; s.b = rec[2 * k].p;
            s.ba = rec[2 * k - 1].bnd; s.bb = rec[2 * k].bnd; s.ea = rec[2 * k - 1].ent; s.eb = rec[2 * k].ent;
            zsv_push(S, s);
        }
        int ce = S->n;
        if (i > 1)
            for (int pi = ps; pi <= pe; pi++)
                for (int ci = cs; ci <= ce; ci++) {
                    ZS *P = &S->s[pi], *C = &S->s[ci];
                    if (!((C->a.x - P->b.x) * (C->b.x - P->a.x) < 0.0)) continue;
                    if (C->lb == 0) { if (P->la == 0) C->lb = pi; }
                    else if (C->lb > 0) { S->s[C->lb].la = -1; C->lb = -1; }
                    if (P->la == 0) { if (-1 < C->lb) P->la = ci; }
                    else if (P->la > 0) { S->s[P->la].lb = -1; P->la = -1; }
                }
        ps = cs; pe = ce;
    }
}
/* FUN_1801f3150 (l.342964): the vertical line through q against one bound: intersections below / above (nc_tol) */
static void zz_count_vert(const ZBs *B, int bnd, PV q, int *below, int *above) {
    if (bnd < 0) return;
    const Gl *l = &B->b[bnd].l; PV a0 = pv(q.x, 0), a1 = pv(q.x, 100);
    for (int i = 0; i < l->n; i++) {
        const Gt *e = &l->e[i]; PV h[2]; int nh = 0;
        if (e->t == 'L') { PV p; double ta, tb; if (zz_l_ints(a0, a1, e->a, e->b, &p, &ta, &tb) && -2e-07 < tb && tb < 1.0000002) h[nh++] = p; }
        else if (e->t == 'A') { PV p1, p2; if (zz_la_ints(a0, a1, e->c, e->r, &p1, &p2)) { if (zz_pt_on_arc(p1, e, g_nc_tol)) h[nh++] = p1; if (zz_pt_on_arc(p2, e, g_nc_tol)) h[nh++] = p2; } }
        for (int k = 0; k < nh; k++) { double d = q.y - h[k].y; if (d > g_nc_tol) (*below)++; else if (d <= -g_nc_tol && -g_nc_tol != d) (*above)++; }
    }
}
/* FUN_1801f3450 (l.343121): chain: start from the last record, reverse the region (+2 toggled, first <-> last, the
   links swapped); ab: swap the ends A / B of every record. The copies of the first / last are refreshed, the last
   record's next link is cleared. */
static void zz_flip(ZSv *S, ZR *r, int chain, int ab) {
    int i;
    if (!chain) i = r->first;
    else { i = r->last; r->rev = !r->rev; int t = r->first; r->first = r->last; r->last = t; }
    while (0 < i) {
        ZS *s = &S->s[i];
        if (ab) { PV p = s->a; s->a = s->b; s->b = p; int t = s->ba; s->ba = s->bb; s->bb = t; t = s->ea; s->ea = s->eb; s->eb = t; }
        if (chain) { int t = s->la; s->la = s->lb; s->lb = t; }
        if (i == r->first) r->fr = *s;
        if (i == r->last) { r->lr = *s; s->la = 0; }
        i = s->la;
    }
}
/* FUN_1801f1fe0 (l.341981): regions = chains of records along the links whose two ends lie on the same pair of bounds
   as the chain's first record (a different pair ends the chain; the previous record's link is cleared); +1 = the first
   record's ends on one bound. Zigzag: FUN_1801f3b10 (l.343399) orientation from the vertical lines through the first
   record's ends and the last record's A end (intersections only below -> 1, reversed via FUN_1801f3450; only above
   -> -1) */
static int zz_regions(const ZBs *B, ZSv *S, int zigzag, ZR **out) {
    ZR *R = NULL; int nr = 0, cap = 0;
    for (;;) {
        int i = 0;
        for (int k = 1; k <= S->n; k++) if (!S->s[k].used) { i = k; break; }
        if (!i) break;
        if (nr + 2 > cap) { cap = cap ? cap * 2 : 64; R = realloc(R, cap * sizeof(ZR)); }
        ZR *r = &R[++nr]; memset(r, 0, sizeof *r);
        r->used = 1; r->flag = 1; r->first = r->last = i;
        ZS f = S->s[i]; r->fr = r->lr = f;
        int prev = 0;
        while (i > 0) {
            ZS *s = &S->s[i];
            if (s->used) break;
            if (s->ba == f.ba && s->bb == f.bb) {
                s->used = 1;
                if (s->ba != s->bb || s->ba < 0) r->flag = 0;
                r->last = i; r->lr = *s; prev = i; i = s->la;
            } else {                                       /* restore the previous record with its link cleared */
                S->s[prev].la = 0; r->last = prev; r->lr = S->s[prev];
                break;
            }
        }
    }
    if (zigzag)
        for (int k = 1; k <= nr; k++) {
            ZR *r = &R[k];
            if (!r->flag) continue;
            int below = 0, above = 0;
            PV q3[3] = { r->fr.a, r->fr.b, r->lr.a };
            for (int j = 0; j < 3; j++) zz_count_vert(B, r->fr.ba, q3[j], &below, &above);
            int o = (below > 0 && above == 0) ? 1 : (below == 0 && above > 0) ? -1 : 0;
            if (o == 1) zz_flip(S, r, 1, 0);
            r->o4 = o;
        }
    *out = R;
    return nr;
}

/* ------------------------------------------------------------------------------------------------ linking */
static ZI zz_end(const ZS *s, int side) { ZI c = { 0 }; c.p = side ? s->b : s->a; c.ent = side ? s->eb : s->ea; c.bnd = side ? s->bb : s->ba; return c; }

/* FUN_1801f15a0 (l.341514), Minimize tool burial off: the next region = the cheapest of the four ends of its first /
   last record (the first region: the lowest y), cost = FUN_1801f2cc0 length x (1, 2, 3)[class]: class 0 = same bound
   orientation and first bound as the previous region (both +1), 1 = it reaches below the last point - nc_tol, else 2;
   an end of the last record reverses the region (FUN_1801f3450, dis 1801f1bbc: ends not swapped). Inside: start on
   the chosen side, the side alternates (zigzag); to the next record along the bound when both ends are on one bound
   (FUN_1801f29c0, direction dis 1801f1c54-1801f1df9: side B -> -1, negated for a clockwise bound, for an outer bound
   and for a reversed region), else FUN_1801f2cc0; across a record straight when FUN_1801e0fd0 allows it.
   Minimize tool burial on (pock_prm +0x31, l.341682 / 341799; medax_zz): the class comes from FUN_1801f0540 instead
   (l.341705: region class 0 when either of its first / last records lies next to a cut region, else 2; a region whose
   class is above the best class so far is skipped, l.341710; per end: class 0 when its record (first: ends 0 1, last:
   2 3) lies next to one, else 2, skipped when above the best class, l.341757), and the link cost gets + 10 x that
   record's smallest uncovered length (l.341828; 1e9 when none). FUN_1801f3940 before every move (l.341870). */
/* FUN_1801f0540 (l.340654-340815): region ri's first (m 0) / last (m 1) record against the first / last records of
   every cut region (+0 == 0, not ri): |A.y - A'.y| < step (pock_prm +0x18) and x overlap min(B.x) - max(A.x) > tol
   -> fl[m] = 1, val[m] = min(val[m], (B.x - A.x) - overlap) (start 1e9) */
static void zz_f0540(const ZR *R, int nr, int ri, double step, int fl[2], double val[2]) {
    fl[0] = fl[1] = 0; val[0] = val[1] = 1000000000.0;
    const ZS *I[2] = { &R[ri].fr, &R[ri].lr };
    for (int j = 1; j <= nr; j++) {
        if (j == ri || R[j].used) continue;
        const ZS *J[2] = { &R[j].fr, &R[j].lr };
        for (int m = 0; m < 2; m++)
            for (int n = 0; n < 2; n++) {
                double dy = fabs(I[m]->a.y - J[n]->a.y);
                if (!(dy <= step && step != dy)) continue;
                double lo = J[n]->a.x; if (J[n]->a.x < I[m]->a.x) lo = I[m]->a.x;
                double hi = J[n]->b.x; if (I[m]->b.x < J[n]->b.x) hi = I[m]->b.x;
                double ov = hi - lo;
                if (CAM_SYS_TOL <= ov && ov != CAM_SYS_TOL) {
                    fl[m] = 1;
                    double v = (I[m]->b.x - I[m]->a.x) - ov;
                    if (v <= val[m]) val[m] = v;
                }
            }
    }
}
static int zz_zigzag(ZP *P, ZBs *B, ZSv *S, ZR *R, int nr, double step, int burial) {
    const double w[3] = { 1.0, 2.0, 3.0 };
    int have_pt = 0, have_reg = 0, prev_reg = -1, side_cur = 0;
    ZI prev_end = { 0 }, prev_st = { 0 }; PV prev_pt = pv(0, 0);
    for (int i = 0; i < B->n; i++) B->b[i].f44 = B->b[i].f45 = 0;   /* l.341658 */
    for (int iter = 0; iter <= nr; iter++) {
        double best = 1e38; int bi = 0, brev = 0, bside = 0, bcls = 2;
        for (int i = 1; i <= nr; i++) {
            ZR *r = &R[i];
            if (!r->used) continue;
            int cls = 2, fl[2] = { 0, 0 }; double uv[2] = { 0, 0 };
            if (!burial) {
                if (have_pt && (r->fr.a.y < prev_pt.y - g_nc_tol || r->lr.a.y < prev_pt.y - g_nc_tol)) cls = 1;
                if (0 < prev_reg && R[prev_reg].flag && r->flag && R[prev_reg].o4 == r->o4 && R[prev_reg].fr.ba == r->fr.ba) cls = 0;
            } else {
                zz_f0540(R, nr, i, step, fl, uv);
                cls = (fl[0] || fl[1]) ? 0 : 2;
                if (bcls < cls) continue;
            }
            ZI c[4] = { zz_end(&r->fr, 0), zz_end(&r->fr, 1), zz_end(&r->lr, 0), zz_end(&r->lr, 1) };
            for (int k = 0; k < 4; k++) {
                double cost;
                if (burial) { cls = fl[k < 2 ? 0 : 1] ? 0 : 2; if (bcls < cls) continue; }
                if (!have_reg) cost = c[k].p.y;
                else {
                    double d = hypot(c[k].p.y - prev_end.p.y, c[k].p.x - prev_end.p.x);
                    if (!(d * w[cls] < best)) continue;
                    ZI fr = prev_end; if (!zz_link(P, B, &fr, &c[k], 0, burial, &cost)) return 0;
                    if (burial) cost = cost + uv[k / 2] * 10.0;
                }
                double v = cost * w[cls];
                if (best <= v) continue;
                best = v; bi = i; brev = 1 < k; bside = k & 1; bcls = cls;
            }
        }
        if (bi <= 0) return 1;
        ZR *r = &R[bi];
        if (brev) zz_flip(S, r, 1, 0);
        side_cur = bside;
        int first_seg = 1;
        for (int i = r->first, guard = 0; 0 < i && guard <= S->n; guard++) {
            ZS *s = &S->s[i];
            for (int st = 1; st <= 2; st++) {
                int dir = side_cur ? -1 : 1;
                ZI cur = zz_end(s, side_cur);
                zz_f3940(B, s->a, s->b, step);                  /* l.341870 (the record's stored ends) */
                if (!P->started) { P->started = 1; P->start = cur.p; P->cur = cur.p; }   /* entry FUN_1801bfcb0 */
                else if (st == 1) {
                    if (first_seg || prev_st.bnd != cur.bnd) { double l; if (!zz_link(P, B, &prev_st, &cur, 1, burial, &l)) return 0; }
                    else {
                        const ZB *bd = &B->b[cur.bnd];
                        if (!bd->ccw) dir = -dir;
                        if (!bd->island) dir = -dir;
                        if (r->rev) dir = -dir;
                        if (!zz_walk(P, B, &prev_st, &cur, dir)) return 0;
                    }
                } else {
                    if (zz_line_inside(B, prev_pt, cur.p)) zp_push(P, 0, cur.p, pv(0, 0), 0);
                    else { double l; if (!zz_link(P, B, &prev_st, &cur, 1, burial, &l)) return 0; }
                }
                prev_pt = cur.p; have_pt = 1; prev_st = cur;
                if (st == 1) side_cur = !side_cur;
                first_seg = 0;
            }
            i = s->la;
        }
        r->used = 0; have_reg = 1; prev_reg = bi; prev_end = prev_st;
    }
    return 1;
}

/* FUN_1801f07b0 (l.340818): the point to come from into region ri (one way): among the cut regions' first / last
   records, the nearest end to the region's start (ends on the line next to the region's first line preferred), or the
   start projected vertically onto the nearest (in y) record spanning its x when that is below half the end distance */
static int zz_oneway_via(const ZR *R, int nr, int ri, ZI *via) {
    const ZR *r = &R[ri]; PV S0 = r->fr.a;
    int L = r->rev ? r->fr.line + 1 : r->fr.line - 1, have_end = 0, have_proj = 0, onl_best = 0;
    double bd = 1e38, bdy = 1e38; ZI e = { 0 }, pr = { 0 };
    for (int j = 1; j <= nr; j++) {
        if (j == ri || R[j].used) continue;
        for (int k = 0; k < 2; k++) {
            const ZS *s = k ? &R[j].lr : &R[j].fr; int onl = s->line == L;
            for (int side = 0; side < 2; side++) {
                ZI c = zz_end(s, side); double d = hypot(c.p.y - S0.y, c.p.x - S0.x);
                if (onl == onl_best) { if (d < bd) { e = c; have_end = 1; bd = d; onl_best = onl; } }
                else if (onl && !onl_best) { e = c; have_end = 1; bd = d; onl_best = onl; }
            }
            if ((s->a.x - S0.x) * (s->b.x - S0.x) < 0.0) {
                double dy = s->a.y - S0.y;
                if (fabs(dy) < bdy) { pr.p = pv(S0.x, s->a.y); pr.ent = -1; pr.bnd = -1; have_proj = 1; bdy = fabs(dy); }
            }
        }
    }
    if (!have_end && !have_proj) return 0;
    *via = bd * 0.5 <= bdy ? e : pr;
    via->t = 0;
    return 1;
}
/* FUN_1801f0d60 (l.341115): one way. Conventional (record 0x123a = 0, Climb / Conventional radio of the cut page)
   cuts A -> B; climb swaps every record's ends first (FUN_1801f3450(.., 0, 1), dis 1801f0e00). The next region: its
   first record's A or last record's B nearest the last point (the first region: the lowest y), preferring a region next
   (line +- 1) to a cut one; the last record's end reverses it with the ends swapped (dis 1801f10c5). Inside a region:
   after a record, back to its start by FUN_1801f35c0 (l.343236: marker 3 + a line at the plunge feed op +0x318, which
   wr_pock_toolpath turns into retract + plunge; FUN_1801e3150 retracts only for pock_op_code 0xe), then along the bound
   to the next record's start in one direction per region (find_path_around_island, dis 1801f11aa: a single-entity first
   link gives direction 0 = decided per link by the order of the points on the entity) and the cut. Into a later region:
   FUN_1801f35c0 to FUN_1801f07b0's point when farther than tol, then FUN_1801f2cc0 to the start; no such point: the
   cut starts where the tool is (as decoded). */
static int zz_oneway(ZP *P, const ZBs *B, ZSv *S, ZR *R, int nr, int climb) {
    if (climb) for (int k = 1; k <= nr; k++) zz_flip(S, &R[k], 0, 1);
    int have_prev = 0; PV last = pv(0, 0);
    for (int iter = 0; iter <= nr; iter++) {
        double best = 1e38; int bi = 0, brev = 0, badj = -1;
        for (int i = 1; i <= nr; i++) {
            ZR *r = &R[i];
            if (!r->used) continue;
            int adj[2] = { -1, -1 }, want[2] = { r->fr.line - 1, r->lr.line + 1 };
            for (int j = 1; j <= nr; j++) {
                if (j == i || R[j].used) continue;
                for (int k = 0; k < 2; k++) if (adj[k] < 0 && want[k] == (k == 0 ? R[j].lr.line : R[j].fr.line)) adj[k] = j;
            }
            PV c[2] = { r->fr.a, r->lr.b };
            for (int k = 0; k < 2; k++) {
                double cost; int sel = 0;
                if (!have_prev) { adj[0] = adj[1] = -1; cost = c[k].y; sel = cost < best; }
                else {
                    cost = hypot(c[k].y - last.y, c[k].x - last.x);
                    if ((-1 < adj[k]) == (-1 < badj)) sel = cost < best;
                    else if (-1 < adj[k] && badj <= -1) sel = 1;
                }
                if (sel) { badj = adj[k]; brev = 0 < k; best = cost; bi = i; }
            }
        }
        if (bi <= 0) return 1;
        ZR *r = &R[bi];
        if (brev) zz_flip(S, r, 1, 1);
        int dir = -10, first_seg = 1; ZS prevs = { 0 };
        for (int i = r->first, guard = 0; 0 < i && guard <= S->n; guard++) {
            ZS *s = &S->s[i]; ZI st = zz_end(s, 0);
            if (!first_seg) {
                zp_push(P, 2, prevs.a, pv(0, 0), 0);                        /* FUN_1801f35c0 */
                ZI from = zz_end(&prevs, 0);
                if (dir == -10) { double l; zz_fpai(B, prevs.ba, &from, &st, 0, &l, &dir);   /* param_4 = 0 (dis 1801f11c7) */
                    if (getenv("ZZ_DEBUG")) fprintf(stderr, "oneway fpai: from %.4f %.4f e%d b%d to %.4f %.4f e%d b%d -> dir %d len %.4f perim %.4f\n", from.p.x, from.p.y, from.ent, from.bnd, st.p.x, st.p.y, st.ent, st.bnd, dir, l, B->b[prevs.ba].perim); }
                if (!zz_walk(P, B, &from, &st, dir)) return 0;
            } else if (!P->started) { P->started = 1; P->start = st.p; P->cur = st.p; }   /* entry FUN_1801bfcb0 */
            else {
                ZI via;
                if (zz_oneway_via(R, nr, bi, &via)) {
                    if (CAM_SYS_TOL < hypot(via.p.y - last.y, via.p.x - last.x)) zp_push(P, 2, via.p, pv(0, 0), 0);
                    double l; if (!zz_link(P, B, &via, &st, 1, 0, &l)) return 0;
                }
            }
            zp_push(P, 0, s->b, pv(0, 0), 0);                               /* FUN_1801e32a0: the cut */
            last = s->b; have_prev = 1; prevs = *s; first_seg = 0;
            i = s->la;
        }
        r->used = 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------------------------------ driver */
static Gt zz_rot_gt(const Gt *e, double ca, double sa, double ang) {
    Gt g = *e;
#define ZR_(q) pv((q).x * ca - (q).y * sa, (q).x * sa + (q).y * ca)
    if (g.t == 'L' || g.t == 'P') { g.a = ZR_(e->a); g.b = ZR_(e->b); }
    else if (g.t == 'A') { g.c = ZR_(e->c); g.sa = fmod(e->sa + ang, 2 * M_PI); if (g.sa < 0) g.sa += 2 * M_PI; }
#undef ZR_
    return g;
}
static void zz_free_bounds(ZBs *B) { for (int i = 0; i < B->n; i++) { gl_free(&B->b[i].l); free(B->b[i].poly.xy); } free(B->b); B->b = NULL; B->n = 0; }

/* one pocket (an outer loop and its islands, world coordinates) -> the roughing path in the frame rotated by -ang
   (rotate_pock_bounds, mccore 0x1805e7b90: every entity rotated, build_pock_bound_info refreshes the boxes) */
static int pk_zz_path(const Gls *pocket, double ang, double step, int method, int climb, int burial, ZP *P) {
    ZBs B = { calloc(pocket->n, sizeof(ZB)), 0 };
    double ca = cos(-ang), sa = sin(-ang);
    for (int pass = 0; pass < 2; pass++)                    /* the outer bound first (FUN_1801f0c80, f23e0's box) */
        for (int i = 0; i < pocket->n; i++) {
            const Gl *l = &pocket->l[i];
            if (l->island != pass || !l->n) continue;
            ZB *b = &B.b[B.n++]; memset(b, 0, sizeof *b);
            for (int k = 0; k < l->n; k++) gl_push(&b->l, zz_rot_gt(&l->e[k], ca, sa, -ang));
            b->island = l->island; b->ccw = gl_area(&b->l) > 0; b->perim = gl_len(&b->l); gl_bbox(&b->l, b->bb);
            b->poly = gl_sample(&b->l, CAM_SYS_TOL * 0.5);
            zz_bound_extremes(b);
        }
    if (!B.n || B.b[0].island) { zz_free_bounds(&B); return 1; }
    if (getenv("ZZ_DEBUG")) for (int i = 0; i < B.n; i++) { fprintf(stderr, "bound %d: island %d ccw %d perim %.4f bb %.4f %.4f %.4f %.4f\n", i, B.b[i].island, B.b[i].ccw, B.b[i].perim, B.b[i].bb[0], B.b[i].bb[1], B.b[i].bb[2], B.b[i].bb[3]);
        for (int k = 0; k < B.b[i].l.n; k++) { Gt *e = &B.b[i].l.e[k]; PV a = gt_start(e), z = gt_end(e); fprintf(stderr, "  %d %c %.4f %.4f -> %.4f %.4f sa %.4f sw %.4f\n", k, e->t, a.x, a.y, z.x, z.y, e->sa, e->sw); } }
    ZSv S = { 0 }; ZR *R = NULL;
    zz_scan(&B, step, &S);
    int nr = zz_regions(&B, &S, method == 0, &R), ok;
    if (getenv("ZZ_DEBUG")) { fprintf(stderr, "%d records, %d regions\n", S.n, nr);
        for (int k = 1; k <= nr; k++) fprintf(stderr, " region %d: flag %d rev %d o4 %d first %d (line %d) last %d (line %d)\n", k, R[k].flag, R[k].rev, R[k].o4, R[k].first, R[k].fr.line, R[k].last, R[k].lr.line);
        if (getenv("ZZ_DEBUG")[0] == '2') for (int k = 1; k <= S.n; k++) fprintf(stderr, "  rec %d line %d la %d lb %d  %.4f %.4f -> %.4f %.4f b%d/%d e%d/%d\n", k, S.s[k].line, S.s[k].la, S.s[k].lb, S.s[k].a.x, S.s[k].a.y, S.s[k].b.x, S.s[k].b.y, S.s[k].ba, S.s[k].bb, S.s[k].ea, S.s[k].eb); }
    ok = method == 0 ? zz_zigzag(P, &B, &S, R, nr, step, burial) : zz_oneway(P, &B, &S, R, nr, climb);
    free(R); free(S.s); zz_free_bounds(&B);
    return ok;
}

/* the roughing plan of one pocket (batch 6 merge: split into plan + depth for gen_pocket_v2's sections): per outer loop of
   the rough boundary (extract_pocket) the path (pk_zz_path, rotated back by +ang at output: rot_rpt), its start and the
   shared roughing entry (cam2d_pock_entry.c) planned at the first depth. Returns 0 with op->error set on a refusal. */
static int pk_zz_plan(const McamFile *f, CamOp *op, const Gls *rb, double z_first, PkZz **out, int *nout) {
    const CamParams *p = g_pk_ep ? g_pk_ep : &op->p;
    double tol = cam_chain_tol(mcam_is_metric(f)) * 0.5;
    int method = p->pk_ok ? pks(p, 0x123c) : 0; if (p->pocket_style == 1) method = 0;
    double ang = p->pk_ok ? pkd(p, 0x1250) : 0;            /* rough_angle = pock_prm +0x28, radians (l.323238) */
    double step = p->tool_dia * p->stepover_pct / 100;     /* pock_prm +0x18 */
    int climb = p->pk_ok ? pkb(p, 0x123a) : 1;
    /* Minimize tool burial: record 0x1259 = pock_prm +0x31, read by FUN_1801f15a0 only (One Way FUN_1801f0d60 passes
       param_6 = 0 to FUN_1801f2cc0 and param_4 = 0 to find_path_around_island) */
    int burial = method == 0 && p->pk_ok && pkb(p, 0x1259);
    double ca = cos(ang), sa = sin(ang);
    *out = NULL; *nout = 0;
    if (!(0.0 < step)) return 1;                            /* FUN_1801ef830: roughing only when pock_prm +0x18 > 0 */
    for (int oi = 0; oi < rb->n; oi++) {
        if (rb->l[oi].island || !rb->l[oi].n) continue;
        Gls pk = { 0 }; gls_push(&pk, gl_copy(&rb->l[oi]));
        Gls one = { &rb->l[oi], 1, 1 };
        for (int j = 0; j < rb->n; j++) if (rb->l[j].island && rb->l[j].n && gls_inside(&one, gt_pt(&rb->l[j].e[0], 0), tol)) gls_push(&pk, gl_copy(&rb->l[j]));
        ZP P = { 0 };
        int ok = pk_zz_path(&pk, ang, step, method, climb, burial, &P);
        if (!ok) { snprintf(op->error, sizeof op->error, "%.150s", P.err[0] ? P.err : "pocket zigzag / one way failed"); free(P.m); gls_free(&pk); return 0; }
        if (!P.started) { free(P.m); gls_free(&pk); continue; }
        /* rotate the path back (rot_rpt) once: the moves in world XY */
        for (int k = 0; k < P.n; k++) { ZM *m = &P.m[k]; PV q = m->p, c = m->c;
            m->p = pv(q.x * ca - q.y * sa, q.x * sa + q.y * ca); m->c = pv(c.x * ca - c.y * sa, c.x * sa + c.y * ca); }
        PV st = pv(P.start.x * ca - P.start.y * sa, P.start.x * sa + P.start.y * ca);
        *out = realloc(*out, (*nout + 1) * sizeof(PkZz)); PkZz *z = &(*out)[(*nout)++]; memset(z, 0, sizeof *z);
        z->m = P.m; z->n = P.n; z->st = st;
        if (!pk_entry_plan(f, op, &pk, st, z_first, &z->ent)) { gls_free(&pk); return 0; }
        z->skip = z->ent.skip;                              /* entry failed: skipped (0x12d1 / 0x134a) */
        gls_free(&pk);
    }
    return 1;
}
/* one depth of a planned One Way / Zigzag piece: the entry, then the path */
static void pk_zz_depth(MB *b, const CamParams *p, const PkZz *z, double depth, double ctol, int *any) {
    double zf = p->top_z + p->feed_plane; PV st = z->st; const ZM *M = z->m;
    if (g_pk_sec_on) pk_entry_emit(b, p, &z->ent, depth, pk_sec_start(b, p, z->ent.s, depth, pk_entry_z(&z->ent, depth), *any), ctol);
    else { if (*any) pk_retract(b, p); pk_entry_emit(b, p, &z->ent, depth, *any ? fmax(zf, p->retract_z) : p->clearance, ctol); }
    if (z->ent.kind != PKE_PLUNGE) mv(b, 0, st.x, st.y, depth, p->feed);   /* the link to the cut start (FUN_1801e23c0) */
    *any = 1;
    PV cur = st;
    for (int k = 0; k < z->n; k++) {
        const ZM *m = &M[k]; PV q = m->p;
        if (m->k == 1) {
            PV c = m->c; double r = hypot(cur.x - c.x, cur.y - c.y), a0 = atan2(cur.y - c.y, cur.x - c.x);
            double sw = zz_arc_sweep(cur, q, c, m->cw); if (m->cw) sw = -sw;
            Gt g = gt_arc(c, r, a0, sw); pk_arc_moves(b, &g, depth, depth, p->feed, ctol);
            b->m[b->n - 1].p.x = q.x; b->m[b->n - 1].p.y = q.y;
        } else if (m->k == 2) {
            /* FUN_1801f35c0 writes bnci_wr_pock_marker(3) = NCI -95 (mccore 0x1800958f0, decomp/pocket_b6/zigzag_decomp_zz_core4.c) before its line;
               wr_pock_toolpath (l.315766 / l.316476) replaces that line by retract_move at the current point + plunge_move at
               its end: as gen_pocket_v2's re-entry (pk_retract, feed plane, plunge feed) */
            if (g_pk_sec_on) { PkH H = pk_h(p, depth, g_pk_ref); pk_up(b, p, H, 0); pk_rapid(b, q.x, q.y, depth + CAM_SYS_TOL < H.f ? H.f : depth); mv(b, 0, q.x, q.y, depth, p->plunge_feed); }   /* l.316483-316487: retract_move, plunge_move(.., first = 0) on the section's heights (N9-12, V-14) */
            else { pk_retract(b, p); mv(b, 1, q.x, q.y, fmax(zf, p->retract_z), 0); mv(b, 1, q.x, q.y, zf, 0); mv(b, 0, q.x, q.y, depth, p->plunge_feed); }
        } else mv(b, 0, q.x, q.y, depth, p->feed);
        cur = q;
    }
}
