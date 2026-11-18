/* cam_filter.c - Mastercam's Line/Arc filter: the op dialog page mccoreres 10318 "Arc Filter / Tolerance" (ArcFilterRefinePage),
 * op dialog batch 10 (formats/opdlg_port_audit.md 14). Included by cam2d.c (after cam2d_pock.c: uses zz_l_ints).
 *
 * Where Mastercam runs it: BNCISection::PostProcess (mccore FUN_180103fb0) after an operation's NCI is written: record 0x33a
 * (filter on) and record 0x11e8 == 0 -> bnci_filter_op 0x180085b90 (skips op_is_drill) -> FUN_18004dd30 (the record 0x320 block
 * -> the filter globals) -> bnci_filter 0x1809443b0, which re-reads the op's NCI section and writes every record through
 * FUN_180944830 (the node list), then FUN_180948750 (the fit) and FUN_18094acf0 / FUN_18094ce10 (write the list). Decompiles:
 * ~/mastercam-re/ghidra_core (mccore.dll), copied to decomp/mccore_filter.c (this batch).
 *
 * Record block (FUN_18004dd30 / FUN_18004b6c0, op + X = record X + 0x68... here record offsets): 0x320 f64 filter_max_error
 * (the tolerance), 0x328 f64 filter_min_radius, 0x330 f64 filter_max_radius, 0x338 s16 filter_look_ahead, 0x33a u8 on,
 * 0x33b / 0x33c / 0x33d u8 filter_create_arcs_xy / _xz / _yz, 0x33e u8 filter_one_way, 0x33f u8 filter_reduce_tol_type,
 * 0x340 f64 filter_reduced_tol_value. Line tolerance FUN_180940b90 = reduce type 1 ? reduced value : max error; arc tolerance
 * FUN_18093fd40 = type 2 ? reduced : max error (the page's "Tighten Line / Arc filtering tolerance" radios).
 * The registry value "Use new filter X3MU1 and beyond" (FUN_18093ead0) defaults to 1: the new arc fit (FitArc, FUN_18093ab00)
 * is ported, the old one (FUN_1809495e0) is not used.
 *
 * Ours: the toolpath is this program's Move list, not NCI records: lines / rapids are nodes, the chords of one engine arc
 * (Move.arc, mb_tag_arc) are one arc node; NCI flags (node +0x90, the break rule of FUN_18094b6c0) are 0 - the moves carry
 * none; output arcs are drawn as chords (step sqrt(8 tol / r), as mw_arc) and tagged for the post. XZ / YZ arcs are created
 * only with MCAM_ARC_FILTER_PLANES=1 (posted with G18 / G19, nc_post.c); stored ops with the filter on are filtered as stored
 * (decisions batch dec2 Q6; before: only after an edit of the page or with MCAM_ARC_FILTER=1 - formats/post.md "Arc filter"). */

typedef struct FNode {
    int type;                 /* node +0x00: 0 rapid, 1 line, 2 CW arc, 3 CCW arc (in its view's coordinates) */
    int cc;                   /* +0x02: cutter compensation code (NCI +0x18 lines / +0x1a arcs) */
    double p[3];              /* +0x08: end point (world) */
    int pl;                   /* +0x68: arc plane 0 XY (view 1), 1 YZ (view 5), 2 XZ (view 2) */
    double c[3];              /* +0x70: arc centre in its view (x, y) and the depth */
    double feed;              /* +0x88 */
    int flags;                /* +0x90: NCI flags (0 here, ours) */
    char brk;                 /* +0x94: break / done */
    char keep;                /* +0xb5: NCI arc +0x5a */
    struct FNode *prev, *next;   /* +0xd8 / +0xe0 */
} FNode;

typedef struct {              /* one-way segment (FUN_180946b60 / FUN_18094c020 / FUN_18094b8d0), list +0x28 + k * 0x70 */
    char inpl, fwd; double n[3], d; int cnt; double sd[3], ed[3]; FNode *first, *last;
} FSeg;

typedef struct {
    double max_err, rmin, rmax, rval, tol, nctol;
    int look, xy, xz, yz, oneway, rtype;
    FNode *head, *tail;
    FSeg seg[10]; int nseg; char closed;
    FNode **pool; int npool, cpool;
    /* output */
    MB *out; double stol;
} FFilt;

typedef struct { double cx, cy, r, sa, sw; } FA2;   /* a_2d */
typedef struct { double cx, cy, cd, r, sa, sw; int view; } FArc;

static double ff_ltol(const FFilt *F) { return F->rtype == 1 ? F->rval : F->max_err; }   /* FUN_180940b90 */
static double ff_atol(const FFilt *F) { return F->rtype == 2 ? F->rval : F->max_err; }   /* FUN_18093fd40 */
static double ff_d3(const double *a, const double *b) { double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return sqrt(x * x + y * y + z * z); }

/* world_to_2d with the system views 1 Top / 2 Front / 5 Side (cam2d.c CAM_PLANES rows) */
static void ff_w2d(int view, const double *p, double *o) {
    const double *m = cam_plane_matrix(view);
    o[0] = m[0] * p[0] + m[1] * p[1] + m[2] * p[2]; o[1] = m[3] * p[0] + m[4] * p[1] + m[5] * p[2];
}
/* FUN_18094bb60: the depth of p for plane k (0 XY: z, 1 YZ: x, 2 XZ: -y) */
static double ff_depth(const double *p, int k) { return k == 0 ? p[2] : k == 1 ? p[0] : k == 2 ? -p[1] : 0; }
/* FUN_18094cd00: drop the planes (flags 0 XY, 1 YZ, 2 XZ) p is not in; the one left, -2 none, -1 more than one */
static int ff_plane_check(const double *p, double tol, const double *dep, char *fl) {
    int cnt = 0, idx = -1;
    for (int k = 0; k < 3; k++) if (fl[k]) { if (tol <= fabs(ff_depth(p, k) - dep[k])) fl[k] = 0; else { cnt++; idx = k; } }
    return cnt == 1 ? idx : cnt == 0 ? -2 : -1;
}

/* systypes / mccore 2D primitives */
static void ff_restrict(double lo, double hi, double *a) { while (*a > hi) *a -= 2 * M_PI; while (*a < lo) *a += 2 * M_PI; }   /* restrict_angle 0x1804b4440 */
static double ff_ang(double dx, double dy) { if (1e-12 <= dx * dx + dy * dy) { double a = atan2(dy, dx); return a < 0 ? a + 2 * M_PI : a; } return 0; }
static double ff_sweep3(double a1, double a2, double a3) {   /* find_sweep_angle 0x1804b6880 */
    ff_restrict(0, 2 * M_PI, &a1); ff_restrict(0, 2 * M_PI, &a2); ff_restrict(0, 2 * M_PI, &a3);
    a2 -= a1; ff_restrict(0, 2 * M_PI, &a2); a3 -= a1; ff_restrict(0, 2 * M_PI, &a3);
    if (a3 <= a2) a3 -= 2 * M_PI;
    return a3;
}
/* center_3pa 0x1804b5200: the perpendicular bisectors of p1 p2 and p2 p3 (l_ints), each pair at least tol apart */
static int ff_center3(const double *p1, const double *p2, const double *p3, double *c) {
    const double tol = CAM_SYS_TOL;
    if (hypot(p2[0] - p1[0], p2[1] - p1[1]) < tol || hypot(p3[0] - p2[0], p3[1] - p2[1]) < tol || hypot(p1[0] - p3[0], p1[1] - p3[1]) < tol) return 0;
    PV m1 = { (p1[0] + p2[0]) * 0.5, (p1[1] + p2[1]) * 0.5 }, m2 = { (p3[0] + p2[0]) * 0.5, (p3[1] + p2[1]) * 0.5 };
    PV s1 = { m1.x - (p2[1] - p1[1]), m1.y + (p2[0] - p1[0]) }, s2 = { m2.x - (p3[1] - p2[1]), m2.y + (p3[0] - p2[0]) }, q; double ta, tb;
    if (!zz_l_ints(m1, s1, m2, s2, &q, &ta, &tb)) return 0;
    c[0] = q.x; c[1] = q.y; return 1;
}
/* cr_a_3p 0x1804b5c90: the arc p1 -> p3 through p2 (sweep < 0 = CW) */
static int ff_arc3(const double *p1, const double *p2, const double *p3, FA2 *a) {
    double c[2]; if (!ff_center3(p1, p2, p3, c)) return 0;
    a->cx = c[0]; a->cy = c[1]; a->r = hypot(p1[0] - c[0], p1[1] - c[1]);
    double a1 = ff_ang(p1[0] - c[0], p1[1] - c[1]), a2 = ff_ang(p2[0] - c[0], p2[1] - c[1]), a3 = ff_ang(p3[0] - c[0], p3[1] - c[1]);
    ff_restrict(0, 2 * M_PI, &a1); a->sa = a1; a->sw = ff_sweep3(a1, a2, a3);
    return 1;
}
/* constr_arc_new 0x1804b5830 + constr_ns_arc 0x1804b5c00: the arc s -> e about c (radii within rtol), CW: start + sweep, sweep < 0 */
static int ff_arc_c(const double *s0, const double *e0, const double *c, int cw, double rtol, FA2 *a) {
    const double *s = cw ? e0 : s0, *e = cw ? s0 : e0;
    double sx = s[0] - c[0], sy = s[1] - c[1], ex = e[0] - c[0], ey = e[1] - c[1], r1 = sqrt(sx * sx + sy * sy), r2 = sqrt(ex * ex + ey * ey);
    if (!(fabs(r1 - r2) <= rtol)) return 0;
    double sa, sw;
    if (CAM_SYS_TOL <= hypot(e[0] - s[0], e[1] - s[1])) {
        sa = 1e-12 <= sx * sx + sy * sy ? ff_ang(sx, sy) : 0;
        double ea = 1e-12 <= ex * ex + ey * ey ? ff_ang(ex, ey) : 0;
        for (sw = ea - sa; sw <= 1e-8; sw += 2 * M_PI) {}
    } else { sw = 2 * M_PI; sa = 0; }
    a->cx = c[0]; a->cy = c[1]; a->r = r1; a->sa = sa; a->sw = sw;
    if (a->sw > 2 * M_PI) return 0;
    if (cw) { a->sa += a->sw; a->sw = -a->sw; }
    return 1;
}
static double ff_vang(const double *a, const double *b) {   /* FUN_18023a2c0: the angle between two vectors */
    double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), lb = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    double c = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (lb * la);
    if (c >= 1) c = 1; else if (c <= -1) c = -1;
    return acos(c);
}
static void ff_unit_normal(const double *p1, const double *p2, double *o) {   /* UnitNormal 0x1804b7480: p1 x p2, normalised */
    o[0] = p1[1] * p2[2] - p1[2] * p2[1]; o[1] = p1[2] * p2[0] - p1[0] * p2[2]; o[2] = p1[0] * p2[1] - p1[1] * p2[0];
    double l = sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]); if (l < 1e-12) l = 1e-12;
    o[0] /= l; o[1] /= l; o[2] /= l;
}
static void ff_rot(const double *v, const double *k, double t, double *o) {   /* FUN_180114560: v about the unit axis k by t */
    double d = k[0] * v[0] + k[1] * v[1] + k[2] * v[2], px = v[0] - k[0] * d, py = v[1] - k[1] * d, pz = v[2] - k[2] * d, c = cos(t), s = sin(t);
    o[0] = (k[1] * pz - k[2] * py) * s + px * c + k[0] * d;
    o[1] = (k[2] * px - k[0] * pz) * s + py * c + k[1] * d;
    o[2] = (k[0] * py - k[1] * px) * s + pz * c + k[2] * d;
}

/* ------------------------------------------------------------------------------------------------ FitArc (CFilter.cpp) */
typedef struct { double *p; int n, cap; } FPts;
static void fp_add(FPts *a, double x, double y, double z) {
    if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 300; a->p = realloc(a->p, a->cap * 3 * sizeof(double)); }
    a->p[3 * a->n] = x; a->p[3 * a->n + 1] = y; a->p[3 * a->n + 2] = z; a->n++;
}
/* min / max distance of the points and the chord midpoints from c (FUN_18093ded0 / FUN_18093f3a0 loops); stop early above lim */
static int ff_spread(const double *P, int n, const double *c, double lim, double *mn, double *mx) {
    for (int i = 0; i < n; i++) {
        double d = hypot(P[2 * i + 1] - c[1], P[2 * i] - c[0]);
        if (d < *mn) *mn = d; if (d > *mx) *mx = d;
        if (lim < *mx - *mn) return 0;
    }
    for (int i = 1; i < n; i++) {
        double d = hypot((P[2 * i - 1] * 0.5 + P[2 * i + 1] * 0.5) - c[1], (P[2 * i - 2] * 0.5 + P[2 * i] * 0.5) - c[0]);
        if (d < *mn) *mn = d; if (d > *mx) *mx = d;
        if (lim < *mx - *mn) return 0;
    }
    return 1;
}
/* FitArc::GetTheorCenterPt (FUN_180940ec0) */
static int ff_theor_center(const FFilt *F, const double *P, int n, double *c) {
    int m = (n - 1) / 2;
    double dx = P[2 * m] - P[0], dy = P[2 * m + 1] - P[1], L = sqrt(dy * dy + dx * dx);
    if (L == 0) dx = dy = 0; else { dx /= L; dy /= L; }
    double best = 1e38; int bi = m, bj = n - 1;
    for (int i = m, j = n - 1;; i--, j--) {
        double ex = P[2 * i] - P[2 * j], ey = P[2 * i + 1] - P[2 * j + 1], l = sqrt(ex * ex + ey * ey);
        if (l == 0) ex = ey = 0; else { ex /= l; ey /= l; }
        double cs = fabs(ey * dy + ex * dx);
        if (cs < best) { best = cs; bi = i; bj = j; }
        if (i - 1 < 1) break;
    }
    if (!(fabs(best - 1.0) >= 1e-9)) return 0;
    PV m1 = { (P[2 * m] + P[0]) * 0.5, (P[2 * m + 1] + P[1]) * 0.5 }, m2 = { (P[2 * bi] + P[2 * bj]) * 0.5, (P[2 * bi + 1] + P[2 * bj + 1]) * 0.5 };
    PV s1 = { m1.x - (P[2 * m + 1] - P[1]), m1.y + (P[2 * m] - P[0]) }, s2 = { m2.x - (P[2 * bi + 1] - P[2 * bj + 1]), m2.y + (P[2 * bi] - P[2 * bj]) }, q;
    double ta, tb;
    if (!zz_l_ints(m1, s1, m2, s2, &q, &ta, &tb)) return 0;
    c[0] = q.x; c[1] = q.y;
    return !(F->rmax * 5.0 < hypot(P[1] - q.y, P[0] - q.x));
}
/* FUN_18093f3a0: 3 x 3 grid search of the centre (step g / 2) for the least spread of the distances, halving down to stop; then
   the best centre projected on the perpendicular bisector of the first and last point */
static void ff_grid(const double *P, int n, const double *c, double g, double stop, double *oc, double *omn, double *omx) {
    if (!(stop < g)) return;
    double h = fabs(g) * 0.5, best = 1e38, cand[9][2]; int bi = 0;
    for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) { cand[3 * r + k][0] = c[0] + (k - 1) * h; cand[3 * r + k][1] = c[1] + (1 - r) * h; }
    for (int k = 0; k < 9; k++) {
        double mn = 1e38, mx = 0; ff_spread(P, n, cand[k], 1e300, &mn, &mx);
        double dd = fabs(mx - mn);
        if (dd < best) { *omn = mn; *omx = mx; best = dd; bi = k; }
    }
    oc[0] = cand[bi][0]; oc[1] = cand[bi][1];
    double as = fabs(stop);
    if (h > as && best >= as && h * 2.0 > best) { double cc[2] = { oc[0], oc[1] }; ff_grid(P, n, cc, h, as, oc, omn, omx); return; }
    if (as * 10.0 < best) { *omn = 0; *omx = 1e38; return; }
    const double *P0 = P, *PL = P + 2 * (n - 1);
    double mx_ = PL[0] * 0.5 + P0[0] * 0.5, my_ = PL[1] * 0.5 + P0[1] * 0.5;
    double ax = (PL[1] - P0[1]) + mx_, ay = -(PL[0] - P0[0]) + my_, bx = mx_ - (PL[1] - P0[1]), by = my_ + (PL[0] - P0[0]);
    double lx = bx - ax, ly = by - ay, L2 = ly * ly + lx * lx;
    if (L2 != 0.0) { double t = ((oc[1] - ay) * ly + (oc[0] - ax) * lx) / L2; oc[0] = (1 - t) * ax + t * bx; oc[1] = (1 - t) * ay + t * by; }   /* proj_pt_ln (systypes 0x180002780) */
    double mn = 1e38, mx = 0; ff_spread(P, n, oc, 1e300, &mn, &mx);
    *omn = mn; *omx = mx;
}
/* FitArc::FitArc (FUN_18093ab00): one arc from node a to node b through the nodes between them; *err = the spread of the
   distances (max - min) or the arc tolerance + 1 */
static int ff_fit_arc(const FFilt *F, FNode *a, FNode *b, int fwd, FArc *o, double *err) {
    const double tol = F->tol, atol = ff_atol(F), nct = F->nctol, LIM = 3.1447342462433827;
    *err = atol + 1.0;
    if (!a || !b || a->next == b || b->keep == 1) return 0;
    if (!F->xy && !F->xz && !F->yz) return 0;
    if (a->type == 0xb) return 0;
    double dep[3] = { ff_depth(a->p, 0), ff_depth(a->p, 1), ff_depth(a->p, 2) }; char fl[3] = { (char)F->xy, (char)F->yz, (char)F->xz };   /* FUN_18094b4c0 */
    FNode *q = fwd ? a->next : a->prev; if (!q) return 0;
    int idx = -1;
    for (; q; q = fwd ? q->next : q->prev) { idx = ff_plane_check(q->p, tol, dep, fl); if (idx == -2) return 0; if (q == b) break; }
    int view = idx == 0 ? 1 : idx == 1 ? 5 : idx == 2 ? 2 : 0;
    if (!view) return 0;
    /* only arcs in the range: one arc when they share plane, direction, centre and radius (within tol) */
    int seen_cw = 0, seen_ccw = 0, first = 1, pl0 = 0, ty0 = 0, pointfit = 0; double c0[3] = { 0, 0, 0 }, r0 = 0, swsum = 0;
    for (q = fwd ? a->next : a->prev; q; q = fwd ? q->next : q->prev) {
        int line = 0;
        if (q->type == 1) line = 1; else if (q->type == 2) seen_cw = 1; else if (q->type == 3) seen_ccw = 1; else return 0;
        if (seen_cw && seen_ccw) return 0;
        if (line) { pointfit = 1; break; }
        if (!((F->xy && q->pl == 0) || (F->xz && q->pl == 2))) { if (!F->yz || q->pl != 1) return 0; }
        if (!q->prev) { pointfit = 1; break; }
        double P2[2], Q2[2], C2[2] = { q->c[0], q->c[1] }; FA2 ar;
        ff_w2d(view, q->prev->p, P2); ff_w2d(view, q->p, Q2);      /* the previous node in list order (node +0xd8), as the decompile */
        int ok = ff_arc_c(P2, Q2, C2, q->type == 2, atol, &ar);
        swsum += ok ? ar.sw : 0;
        if (!ok || LIM < fabs(swsum)) { pointfit = 1; break; }
        if (!first) {
            double dc = hypot(C2[0] - c0[0], C2[1] - c0[1]), dr = hypot(Q2[0] - C2[0], Q2[1] - C2[1]);
            if (q->pl != pl0 || tol <= dc || q->type != ty0 || tol <= fabs(r0 - dr)) { pointfit = 1; break; }
        } else { pl0 = q->pl; ty0 = q->type; memcpy(c0, q->c, sizeof c0); r0 = hypot(Q2[0] - c0[0], Q2[1] - c0[1]); first = 0; }
        if (q == b) break;
    }
    if (!pointfit) {
        double S2[2], E2[2]; FA2 ar; ff_w2d(view, a->p, S2); ff_w2d(view, b->p, E2);
        if (ff_arc_c(S2, E2, c0, ty0 == 2, atol, &ar) && fabs(ar.sw) <= LIM) {
            o->cx = c0[0]; o->cy = c0[1]; o->cd = dep[pl0]; o->r = r0; o->sa = ar.sa; o->sw = ar.sw; o->view = view; *err = 0; return 1;
        }
        *err = atol + 1.0; return 0;
    }
    /* the points: the nodes' ends, arcs as chords (sagitta nc_tol) */
    FPts pts = { 0 }; double prev[3]; memcpy(prev, a->p, sizeof prev); fp_add(&pts, prev[0], prev[1], prev[2]);
    double swtot = 0; int res = -1;
    for (q = fwd ? a->next : a->prev; q && res < 0; q = fwd ? q->next : q->prev) {
        if (q->type == 2 || q->type == 3) {
            double d25 = ff_d3(q->c, prev), d26 = ff_d3(q->c, q->p);
            if (tol < fabs(d25 - d26)) { res = 0; break; }
            double r = (d26 + d25) * 0.5, step = 2.0 * asin(sqrt(r * r - (r - nct) * (r - nct)) / r);   /* FUN_180940b40(nc_tol, r) */
            double vE[3] = { q->p[0] - q->c[0], q->p[1] - q->c[1], q->p[2] - q->c[2] }, vS[3] = { prev[0] - q->c[0], prev[1] - q->c[1], prev[2] - q->c[2] }, nrm[3];
            double ang = ff_vang(vE, vS); ff_unit_normal(vS, vE, nrm);
            double ax[3] = { 0, 0, 1 }; if (q->pl == 1) { ax[0] = 1; ax[2] = 0; } else if (q->pl == 2) { ax[1] = -1; ax[2] = 0; }
            double a28 = ff_vang(ax, nrm), dsw;
            if (q->type == 2) { double s = fabs(ang); if (a28 < 1.570796326794896) s = 2 * M_PI - fabs(ang); dsw = -s; }
            else { dsw = fabs(ang); if (1.570796326794896 <= a28) dsw = 2 * M_PI - fabs(ang); }
            int cnt = (int)(short)((short)(int)(fabs(dsw) / step) + 1); double inc = dsw / cnt;
            for (int i = 1; i < cnt; i++) { double v[3]; ff_rot(vS, ax, inc * i, v); fp_add(&pts, q->c[0] + v[0], q->c[1] + v[1], q->c[2] + v[2]); }
            fp_add(&pts, q->p[0], q->p[1], q->p[2]); memcpy(prev, q->p, sizeof prev);
            swtot += dsw;
            if (2 * M_PI - tol <= swtot) { res = 0; break; }
        } else { fp_add(&pts, q->p[0], q->p[1], q->p[2]); memcpy(prev, q->p, sizeof prev); }
        if (q == b) break;
    }
    if (res == 0 || pts.n < 2) { free(pts.p); return 0; }
    int n = pts.n; double *P = malloc(2 * n * sizeof(double));
    for (int i = 0; i < n; i++) ff_w2d(view, pts.p + 3 * i, P + 2 * i);
    free(pts.p);
    double c[2], cb[2], mn = 0, mx = 1e38;
    if (!ff_theor_center(F, P, n, c)) { free(P); return 0; }
    { double m1 = 1e38, m2 = 0; if (!ff_spread(P, n, c, atol * 10.0, &m1, &m2)) { free(P); return 0; } }   /* FUN_18093ded0 */
    cb[0] = c[0]; cb[1] = c[1];
    ff_grid(P, n, c, atol * 20.0, atol * 0.5, cb, &mn, &mx);
    *err = fabs(mx - mn);
    if (!(*err <= atol)) { *err = atol + 1.0; free(P); return 0; }
    double best = 1e38; int si = 1;
    for (int i = 1; i < n - 1; i++) {
        double d = fabs(hypot(P[0] - P[2 * i], P[1] - P[2 * i + 1]) - hypot(P[2 * (n - 1)] - P[2 * i], P[2 * (n - 1) + 1] - P[2 * i + 1]));
        if (d < best) { best = d; si = i; }
    }
    FA2 a3, ar; int ok = 0;
    if (ff_arc3(P, P + 2 * si, P + 2 * (n - 1), &a3) && ff_arc_c(P, P + 2 * (n - 1), cb, a3.sw < 0.0, atol, &ar)
        && F->rmin <= ar.r && ar.r <= F->rmax && fabs(ar.sw) <= LIM) {
        o->cx = ar.cx; o->cy = ar.cy; o->cd = dep[idx]; o->r = ar.r; o->sa = ar.sa; ff_restrict(0, 2 * M_PI, &o->sa); o->sw = ar.sw; o->view = view; ok = 1;
    }
    free(P);
    return ok;
}

/* ------------------------------------------------------------------------------------------------ the node list */
/* FUN_18094b6c0 (Mill): a node the fit may not pass: a compensation code starting on it or ending after it, NCI flags (none
   here), a feed change to the next node */
static int ff_brk(const FNode *pv, const FNode *n, const FNode *nx) {
    int b2 = (n->cc != 0 && n->cc != pv->cc) || nx->cc == 140;
    int b6 = (pv->flags & 1) || (n->flags & 2) || (n->flags & 0x7c);
    if (b2 || b6) return 1;
    return !(fabs(n->feed - nx->feed) <= 1e-12);
}
/* FUN_180949c10 (new filter): the largest distance of the nodes between a and b from the line a b (beyond its ends: twice the
   excess); an arc in the range: line tolerance + 1 */
static double ff_line_dev(const FFilt *F, FNode *a, FNode *b, int fwd) {
    double dev = 0, lt = ff_ltol(F);
    if (!a || !b || a->next == b) return 0;
    for (FNode *q = fwd ? a->next : a->prev; q; q = fwd ? q->next : q->prev) { if (q->type == 2 || q->type == 3) return lt + 1.0; if (q == b) break; }
    double ux = b->p[0] - a->p[0], uy = b->p[1] - a->p[1], uz = b->p[2] - a->p[2], L = sqrt(ux * ux + uy * uy + uz * uz);
    if (L == 0) ux = uy = uz = 0; else { ux /= L; uy /= L; uz /= L; }
    for (FNode *q = fwd ? a->next : a->prev; q && q != b; q = fwd ? q->next : q->prev) {
        double vx = q->p[0] - a->p[0], vy = q->p[1] - a->p[1], vz = q->p[2] - a->p[2];
        double d1 = sqrt(vx * vx + vy * vy + vz * vz), d2 = ff_d3(q->p, b->p), m = d1 > d2 ? d1 : d2, ext = m <= L ? 0 : (m - L) * 2.0;
        double cx = uy * vx - vy * ux, cy = vy * uz - vz * uy, cz = vz * ux - uz * vx, cr = sqrt(cz * cz + cy * cy + cx * cx);
        double e = cr <= ext ? ext : cr;
        if (dev < e) dev = e;
        if (lt < dev) return lt + 1.0;
    }
    return dev;
}
static void ff_unlink(FFilt *F, FNode *n) {   /* FUN_18094ccc0 (the node keeps its own links) */
    if (!n->prev) F->head = n->next; else n->prev->next = n->next;
    if (!n->next) F->tail = n->prev; else n->next->prev = n->prev;
}
/* FUN_180948750: the fit over the nodes from A to B (fwd), or from B back to A */
static void ff_fit(FFilt *F, FNode *A, FNode *B, int fwd) {
    if (!A || !B) return;
    FNode *n14, *anchor, *endmark;
    if (!fwd) {
        n14 = B->prev;
        for (FNode *q = A; q; q = q->next) {
            if (!q->brk && (!q->next || !q->prev || ff_brk(q->prev, q, q->next))) q->brk = 1;
            if (q == B) break;
        }
        anchor = B; endmark = A;
    } else { n14 = A->next; anchor = A; endmark = B; }
    double lt = ff_ltol(F), at = ff_atol(F);
    double best_l = 0, best_a = 0, cur_l = 0, cur_a = 0; int best_ok = 0, cur_ok = 0; FArc best_arc = { 0 }, arc = { 0 };
    int cnt = 1, finish = 0;
    do {
        if (!n14) return;
        FNode *cand = fwd ? n14->next : n14->prev;
        cnt++;
        int stop;
        if (n14 == endmark || !cand) { stop = 1; finish = 1; }
        else if (cand->type < 1) stop = 1;
        else if (anchor->type > 0 || anchor->type == -6) stop = F->look <= cnt;
        else stop = 1;
        int c63 = 0, dd7 = 0;
        if (fwd && !n14->brk) {
            n14->brk = (!n14->prev || !n14->next) ? 1 : (char)ff_brk(n14->prev, n14, n14->next);
            if (n14->brk) c63 = 1;
        }
        if (!c63) {
            if (stop) c63 = 1;
            else {
                if (!n14->brk) {
                    cur_l = ff_line_dev(F, anchor, cand, fwd); cur_ok = 0;
                    if (F->max_err / 100.0 < cur_l) cur_ok = ff_fit_arc(F, anchor, cand, fwd, &arc, &cur_a);
                    if (cur_ok) { if (lt < cur_l && at < cur_a) c63 = 1; }
                    else if (lt < cur_l) c63 = 1;
                }
                if (!c63) {
                    if (n14->brk) dd7 = 1;
                    else { best_l = cur_l; best_a = cur_a; best_ok = cur_ok; if (cur_ok) best_arc = arc; }
                }
            }
        }
        if (c63) {
            if (n14->type >= 0 && (best_l <= lt || best_a <= at)) {
                FNode *lo = fwd ? anchor : n14, *hi = fwd ? n14 : anchor, *endn = hi;
                int fl = 0, any = 0;
                for (FNode *q = lo->next; q && q != hi;) { FNode *nx = q->next; if (q->type != 0xb) fl |= q->flags; ff_unlink(F, q); any = 1; q = nx; }
                if (!best_ok || best_l / lt <= best_a / at) { if (any && (endn->type == 2 || endn->type == 3)) endn->type = 1; }
                else {
                    endn->type = best_arc.sw <= 0.0 ? 2 : 3;
                    if (!fwd) endn->type = 5 - endn->type;
                    if (best_arc.view == 1) endn->pl = 0; else if (best_arc.view == 2) endn->pl = 2; else if (best_arc.view == 5) endn->pl = 1;
                    endn->c[0] = best_arc.cx; endn->c[1] = best_arc.cy; endn->c[2] = best_arc.cd; endn->keep = 0;
                }
                anchor->brk = 1; n14->brk = 1;
                if (endn->type != 0xb) endn->flags |= fl;
            }
            dd7 = 1;
        }
        if (dd7) { cnt = 0; FNode *nx = fwd ? n14->next : n14->prev; best_l = best_a = 0; best_ok = 0; anchor = n14; n14 = nx; }
        else n14 = cand;
    } while (!finish);
}

/* one way filtering: the planar runs of lines (FUN_180946b60, FUN_18094c020) and their direction (FUN_18094b8d0) */
static void ff_seg_dirs(const FFilt *F, FSeg *s) {
    int okS = 0, okE = 0;
    if (s->first) for (FNode *q = s->first->next; q; q = q->next)
        if (F->nctol < ff_d3(q->p, s->first->p)) { for (int k = 0; k < 3; k++) s->sd[k] = q->p[k] - s->first->p[k]; okS = 1; break; }
    if (s->last) for (FNode *q = s->last->prev; q; q = q->prev)
        if (F->nctol < ff_d3(s->last->p, q->p)) { for (int k = 0; k < 3; k++) s->ed[k] = s->last->p[k] - q->p[k]; okE = 1; break; }
    if (!okS) s->sd[0] = s->sd[1] = s->sd[2] = 0;
    if (!okE) s->ed[0] = s->ed[1] = s->ed[2] = 0;
}
static void ff_seg_track(FFilt *F, FNode *n) {
    F->closed = 0;
    if (n->type != 1) return;
    FSeg *s = &F->seg[0];
    if (F->nseg == 0) { s->inpl = 0; s->cnt = 0; s->first = n; F->seg[1].last = NULL; F->nseg++; }
    if (!s->inpl) {
        s->cnt++; s->last = n;
        FNode *p1 = n->prev, *p2 = p1 ? p1->prev : NULL;
        if (s->cnt > 3 && p1 && p2) {
            double a[3] = { p1->p[0] - n->p[0], p1->p[1] - n->p[1], p1->p[2] - n->p[2] }, b[3] = { p2->p[0] - p1->p[0], p2->p[1] - p1->p[1], p2->p[2] - p1->p[2] };
            if (F->nctol < sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]) && F->nctol < sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])) {
                double nx = a[2] * b[1] - a[1] * b[2], ny = b[2] * a[0] - a[2] * b[0], nz = a[1] * b[0] - b[1] * a[0], l = sqrt(ny * ny + nx * nx + nz * nz);
                if (2e-07 < l) {
                    s->inpl = 1; s->n[0] = nx / l; s->n[1] = ny / l; s->n[2] = nz / l;
                    s->d = n->p[1] * s->n[1] + n->p[0] * s->n[0] + n->p[2] * s->n[2];
                    for (FNode *q = p2->prev; q && fabs((q->p[1] * s->n[1] + s->n[0] * q->p[0] + q->p[2] * s->n[2]) - s->d) < F->nctol; q = q->prev) s->first = q;
                    ff_seg_dirs(F, s);
                }
            }
        }
    } else if (F->nctol <= fabs((s->n[1] * n->p[1] + s->n[0] * n->p[0] + s->n[2] * n->p[2]) - s->d)) {
        ff_seg_dirs(F, s); s->fwd = 1;
        int k = F->nseg - 1; if (k > 8) k = 8;
        for (; k >= 0; k--) F->seg[k + 1] = F->seg[k];
        if (F->nseg < 10) F->nseg++;
        s->inpl = 0; s->cnt = 0; F->closed = 1;
    } else { s->cnt++; s->last = n; }
}
static int ff_seg_dir(FFilt *F, int a, int b0) {
    for (int b = b0; b < F->nseg; b++) {
        FSeg *A = &F->seg[a], *B = &F->seg[b];
        if (0.9999998 < fabs(A->n[1] * B->n[1] + A->n[0] * B->n[0] + A->n[2] * B->n[2])) {
            if (0.0 < A->sd[1] * B->ed[1] + B->ed[0] * A->sd[0] + A->sd[2] * B->ed[2]) return 0;
            if (!A->first || !A->last || !B->first || !B->last) return 0;
            if (ff_d3(B->last->p, A->first->p) <= ff_d3(B->first->p, A->first->p) && ff_d3(B->first->p, A->last->p) <= ff_d3(B->last->p, A->last->p)) {
                A->fwd = !B->fwd; return 1;
            }
            return 0;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ in / out */
static void ff_emit(FFilt *F) {   /* FUN_18094acf0 / FUN_18094ce10: the list as moves */
    MB *b = F->out;
    for (FNode *n = F->head; n; n = n->next) {
        if (n->type == 0 || n->type == 1 || !b->n) { mv(b, n->type == 0, n->p[0], n->p[1], n->p[2], n->feed); b->m[b->n - 1].cc = n->cc; continue; }
        /* an arc from the last point: in its plane (u, v; the third coordinate linear), its view's direction */
        int u = n->pl == 1 ? 1 : 0, v = n->pl == 0 ? 1 : 2, w = 3 - u - v;
        V3 s = lastp(b); double sp[3] = { s.x, s.y, s.z }, c[3];
        c[u] = n->c[0]; c[v] = n->c[1]; c[w] = sp[w];                       /* view 5 = (y, z), view 2 = (x, z) */
        int cw = n->type == 2;
        double rad = hypot(n->p[u] - c[u], n->p[v] - c[v]), rs = hypot(sp[u] - c[u], sp[v] - c[v]);
        double a0 = atan2(sp[v] - c[v], sp[u] - c[u]), a1 = atan2(n->p[v] - c[v], n->p[u] - c[u]), sw = a1 - a0;
        if (hypot(n->p[u] - sp[u], n->p[v] - sp[v]) < 1e-9) sw = cw ? -2 * M_PI : 2 * M_PI;
        else if (!cw) { while (sw <= 0) sw += 2 * M_PI; while (sw > 2 * M_PI) sw -= 2 * M_PI; }
        else { while (sw >= 0) sw -= 2 * M_PI; while (sw < -2 * M_PI) sw += 2 * M_PI; }
        double st = rad > 1e-9 ? sqrt(8 * F->stol / rad) : M_PI / 18; if (st > M_PI / 18) st = M_PI / 18;   /* ours: the display chords (mw_arc) */
        int k = (int)ceil(fabs(sw) / st); if (k < 2) k = 2; if (k > 100000) k = 100000;
        int from = b->n;
        for (int i = 1; i <= k; i++) {
            double t = (double)i / k, a = a0 + sw * t, rr = rs + (rad - rs) * t, q[3];
            if (i == k) memcpy(q, n->p, sizeof q);
            else { q[u] = c[u] + rr * cos(a); q[v] = c[v] + rr * sin(a); q[w] = sp[w] + (n->p[w] - sp[w]) * t; }
            mv(b, 0, q[0], q[1], q[2], n->feed); b->m[b->n - 1].cc = n->cc;
        }
        mb_tag_arc(b, from, c[0], c[1], cw);
        for (int i = from; i < b->n; i++) { b->m[i].apl = n->pl; b->m[i].cz = c[2]; }
    }
    F->head = F->tail = NULL; F->nseg = 0;
}
static void ff_flush(FFilt *F) {   /* FUN_180944830 LAB_180944fb4 */
    if (F->oneway && ff_seg_dir(F, 0, 1)) ff_fit(F, F->seg[0].first, F->seg[0].last, F->seg[0].fwd);
    ff_fit(F, F->head, F->tail, 1);
    ff_emit(F);
}
static FNode *ff_new(FFilt *F) {
    if (F->npool == F->cpool) { F->cpool = F->cpool ? F->cpool * 2 : 1024; F->pool = realloc(F->pool, F->cpool * sizeof *F->pool); }
    FNode *n = calloc(1, sizeof *n); F->pool[F->npool++] = n; return n;
}
static void ff_push(FFilt *F, FNode *n) {   /* FUN_180944830 for NCI 0 / 1 / 2 / 3, FUN_180944290 (append) */
    n->next = NULL; n->prev = F->tail;
    if (!F->head || !F->tail) F->head = n; else F->tail->next = n;
    F->tail = n;
    if (F->oneway && n->type >= 0) ff_seg_track(F, n);
    if (n->type == 1) {
        if (F->oneway && F->closed && F->nseg > 1 && ff_seg_dir(F, 1, 2)) ff_fit(F, F->seg[1].first, F->seg[1].last, F->seg[1].fwd);
    } else if (n->type == 0) ff_flush(F);
}
static void ff_free(FFilt *F) { for (int i = 0; i < F->npool; i++) free(F->pool[i]); free(F->pool); F->pool = NULL; F->npool = F->cpool = 0; }

/* the filter over a move list (op coordinates); 1 = filtered */
static int ff_run_moves(FFilt *F, const Move *m, int nm, MB *out) {
    F->out = out; F->head = F->tail = NULL; F->nseg = 0; F->closed = 0;
    for (int k = 0; k < nm; k++) {
        FNode *n = ff_new(F);
        n->p[0] = m[k].p.x; n->p[1] = m[k].p.y; n->p[2] = m[k].p.z; n->feed = m[k].rapid ? -1.0 : m[k].feed; n->cc = m[k].cc;
        if (k > 0 && !m[k].rapid && m[k].arc > 0 && m[k].apl == 0) {   /* the chords of one engine arc record: one arc node */
            int e = k; while (e + 1 < nm && m[e + 1].arc == m[k].arc && !m[e + 1].rapid) e++;
            n->type = m[k].cw ? 2 : 3; n->pl = 0; n->c[0] = m[k].cx; n->c[1] = m[k].cy; n->c[2] = m[e].p.z;   /* NCI arc +0x48 = the depth
                                                                                         (z of the stored XY arcs: BRACKET4) */
            n->p[0] = m[e].p.x; n->p[1] = m[e].p.y; n->p[2] = m[e].p.z;
            for (int j = k; j <= e; j++) if (m[j].cc) n->cc = m[j].cc;
            k = e;
        } else { n->type = m[k].rapid ? 0 : 1; n->brk = n->type == 0; }
        ff_push(F, n);
    }
    ff_flush(F);
    ff_free(F);
    return 1;
}

static int ff_env(const char *k) { const char *e = getenv(k); return e && !strcmp(e, "1"); }
int cam_arc_filter_planes_on(void) { return ff_env("MCAM_ARC_FILTER_PLANES"); }
static int ff_patch_off(int o) { return o >= 0x320 && o < 0x348; }
/* the filter's record block: the stored op's record (or the Mill_MM / Mill_Inch defaults op) with the dialog's patches */
static int ff_params(const McamFile *f, const CamParams *p, FFilt *F) {
    /* decisions batch dec2 (Q6): the stored flag is honoured by default, as BNCISection::PostProcess does (0x33a on and 0x11e8 == 0,
       below); before, a stored op was filtered only after an edit of the page or with MCAM_ARC_FILTER=1 (that switch is now
       unnecessary and ignored). No user op has 0x33a on (205 / 205, formats/opdlg_port_audit.md 14), so their output is unchanged. */
    uint8_t rec[0x1200]; memset(rec, 0, sizeof rec);
    const Op *fo = NULL;
    if (p->file_op_id > 0) for (int i = 0; i < f->nops; i++) if (f->ops[i].id == p->file_op_id && f->ops[i].raw) { fo = &f->ops[i]; break; }
    if (fo) memcpy(rec, fo->raw, fo->raw_n < sizeof rec ? fo->raw_n : sizeof rec);
    else { uint32_t dn = 0; const uint8_t *dr = cam_default_op_record(p->type, mcam_is_metric(f), &dn); if (dr) memcpy(rec, dr, dn < sizeof rec ? dn : sizeof rec); }
    for (int k = 0; k < p->nrpatch; k++) {
        int o = p->rpatch[k].off, n = p->rpatch[k].n;
        if (!p->rpatch[k].height && ff_patch_off(o) && o + n <= (int)sizeof rec) memcpy(rec + o, p->rpatch[k].v, n);
    }
    if (!rec[0x33a] || rec[0x11e8]) return 0;               /* BNCISection::PostProcess: filter on and record 0x11e8 == 0 */
    int16_t la; memcpy(&F->max_err, rec + 0x320, 8); memcpy(&F->rmin, rec + 0x328, 8); memcpy(&F->rmax, rec + 0x330, 8);
    memcpy(&la, rec + 0x338, 2); memcpy(&F->rval, rec + 0x340, 8);
    F->look = la; F->xy = rec[0x33b] != 0; F->xz = rec[0x33c] != 0; F->yz = rec[0x33d] != 0; F->oneway = rec[0x33e] != 0; F->rtype = rec[0x33f];
    if (!cam_arc_filter_planes_on()) F->xz = F->yz = 0;       /* G18 / G19 arcs: opt-in (LinuxCNC; formats/post.md) */
    F->tol = CAM_SYS_TOL; F->nctol = g_nc_tol;
    F->stol = p->tol > 0 ? p->tol : g_nc_tol;
    return 1;
}
static int cam_arc_filter(const McamFile *f, CamOp *op, MB *b) {
    if (op->p.type == CAM_DRILL) return 0;                  /* bnci_filter_op: not for op_is_drill */
    FFilt F; memset(&F, 0, sizeof F);
    if (!ff_params(f, &op->p, &F)) return 0;
    MB o = { 0 };
    ff_run_moves(&F, b->m, b->n, &o);
    free(b->m); *b = o;
    return 1;
}

/* cam_cli nci-filter (testing / the oracle, ours): nodes "type x y z cx cy cz pl feed cc" one per line -> the filtered nodes */
int cam_filter_nodes(FILE *in, FILE *out, double max_err, double rmin, double rmax, int look, int xy, int xz, int yz, int oneway,
                     int rtype, double rval, int metric) {
    FFilt F; memset(&F, 0, sizeof F);
    F.max_err = max_err; F.rmin = rmin; F.rmax = rmax; F.look = look; F.xy = xy; F.xz = xz; F.yz = yz; F.oneway = oneway; F.rtype = rtype; F.rval = rval;
    F.nctol = mc_systol_units(metric)->nc_tol; F.tol = CAM_SYS_TOL; F.stol = F.nctol;   /* nc_tol (config 1008), tol (1000) */
    char line[512]; FNode *all = NULL;
    MB dummy = { 0 }; F.out = &dummy;
    /* no emit to moves: the list is printed at every flush */
    int nin = 0;
    while (fgets(line, sizeof line, in)) {
        FNode *n = ff_new(&F); int t, pl, cc;
        if (sscanf(line, "%d %lf %lf %lf %lf %lf %lf %d %lf %d", &t, &n->p[0], &n->p[1], &n->p[2], &n->c[0], &n->c[1], &n->c[2], &pl, &n->feed, &cc) != 10) continue;
        n->type = t; n->pl = pl; n->cc = cc; n->brk = t == 0; nin++;
        n->next = NULL; n->prev = F.tail;
        if (!F.head || !F.tail) F.head = n; else F.tail->next = n;
        F.tail = n;
        if (F.oneway && n->type >= 0) ff_seg_track(&F, n);
        if (n->type == 1) { if (F.oneway && F.closed && F.nseg > 1 && ff_seg_dir(&F, 1, 2)) ff_fit(&F, F.seg[1].first, F.seg[1].last, F.seg[1].fwd); }
        else if (n->type == 0) {
            if (F.oneway && ff_seg_dir(&F, 0, 1)) ff_fit(&F, F.seg[0].first, F.seg[0].last, F.seg[0].fwd);
            ff_fit(&F, F.head, F.tail, 1);
            for (FNode *q = F.head; q; q = q->next) fprintf(out, "%d %.9g %.9g %.9g %.9g %.9g %.9g %d %.9g %d\n", q->type, q->p[0], q->p[1], q->p[2], q->c[0], q->c[1], q->c[2], q->pl, q->feed, q->cc);
            F.head = F.tail = NULL; F.nseg = 0;
        }
    }
    if (F.oneway && ff_seg_dir(&F, 0, 1)) ff_fit(&F, F.seg[0].first, F.seg[0].last, F.seg[0].fwd);
    ff_fit(&F, F.head, F.tail, 1);
    for (FNode *q = F.head; q; q = q->next) fprintf(out, "%d %.9g %.9g %.9g %.9g %.9g %.9g %d %.9g %d\n", q->type, q->p[0], q->p[1], q->p[2], q->c[0], q->c[1], q->c[2], q->pl, q->feed, q->cc);
    ff_free(&F); (void)all; (void)nin;
    return 1;
}

/* cam_cli nci-filterpost (testing, ours): the same nodes through the real path (ff_push / ff_flush / ff_emit) into moves */
int cam_filter_nodes_moves(FILE *in, double max_err, double rmin, double rmax, int look, int xy, int xz, int yz, int oneway, int rtype,
                           double rval, int metric, Move **m, int *nm) {
    FFilt F; memset(&F, 0, sizeof F);
    F.max_err = max_err; F.rmin = rmin; F.rmax = rmax; F.look = look; F.xy = xy; F.xz = xz; F.yz = yz; F.oneway = oneway; F.rtype = rtype; F.rval = rval;
    F.nctol = mc_systol_units(metric)->nc_tol; F.tol = CAM_SYS_TOL; F.stol = F.nctol;   /* nc_tol (config 1008), tol (1000) */
    MB o = { 0 }; F.out = &o;
    char line[512];
    while (fgets(line, sizeof line, in)) {
        FNode *n = ff_new(&F); int t, pl, cc;
        if (sscanf(line, "%d %lf %lf %lf %lf %lf %lf %d %lf %d", &t, &n->p[0], &n->p[1], &n->p[2], &n->c[0], &n->c[1], &n->c[2], &pl, &n->feed, &cc) != 10) continue;
        n->type = t; n->pl = pl; n->cc = cc; n->brk = t == 0;
        ff_push(&F, n);
    }
    ff_flush(&F); ff_free(&F);
    *m = o.m; *nm = o.n;
    return 1;
}
