/* cui_geo_mc.inc - Mastercam's own entity kernels used by the Wireframe commands (included once by cui_geo.c).
 * These work on Mastercam's parameter of an entity (a line 0 .. 1 from a to b; an arc 0 .. 1 from its own start along its SIGNED
 * sweep), unlike geo2d's GE, which always runs counter-clockwise. Sources: decomp/wfacc/ (Ghidra dumps of mccadutil / mccore,
 * tools/ghidra_scripts/DumpDecomp.java), decomp/mccore_inters.c, decomp/mccad_cmds/sketcher_trim_2.c. */

/* pt_equal (xform/gaps/core_removed.c:251, mccore @180018c90): every coordinate closer than tol */
static int mc_pt_equal(V3 a, V3 b) { double t = mc_systol.tol; return fabs(a.x - b.x) < t && fabs(a.y - b.y) < t && fabs(a.z - b.z) < t; }
/* intersect_ents' hits of the two carriers: gp_ints (viewer/mcfillet.c mcf_gp_ints, the tol tests of l_ints / la_ints / aa_ints, not geo2d's 1e-9), first the hit nearer the seed. Returns 0 none, 1 two lines (one hit), 2 an arc in the pair (intersect_ents' second flag, mccore_inters.c:157: the two hits may coincide on a tangency) */
static int mc_inter(const GE *a, const GE *b, P2 seed, P2 x[2]) {
    if ((a->t != GE_LINE && a->t != GE_ARC) || (b->t != GE_LINE && b->t != GE_ARC)) return 0;
    McfG g[2]; const GE *e[2] = { a, b }; double o[4];
    for (int i = 0; i < 2; i++) { memset(&g[i], 0, sizeof g[i]); if (e[i]->t == GE_LINE) { g[i].line = 1; g[i].x1 = e[i]->a.x; g[i].y1 = e[i]->a.y; g[i].x2 = e[i]->b.x; g[i].y2 = e[i]->b.y; } else { g[i].cx = e[i]->c.x; g[i].cy = e[i]->c.y; g[i].r = e[i]->r; g[i].sa = e[i]->a0; g[i].sw = e[i]->sw; } }
    if (!mcf_gp_ints(&g[0], &g[1], seed.x, seed.y, o, mc_systol.tol)) return 0;
    x[0] = (P2){ o[0], o[1] }; x[1] = (P2){ o[2], o[3] };
    return a->t == GE_LINE && b->t == GE_LINE ? 1 : 2;
}
static int mc_inter_near(const GE *a, const GE *b, P2 seed, P2 *out) { P2 x[2]; if (!mc_inter(a, b, seed, x)) return 0; *out = x[0]; return 1; }
static double d3v(V3 a, V3 b) { return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }
/* dist_pt_l_3d (mccore_spline_brk.c @180013bd0): the distance to the unbounded line; a line under tol: the distance to its start */
static double mc_dist_pt_line(V3 p, V3 a, V3 b) {
    double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z, L = sqrt(dx * dx + dy * dy + dz * dz);
    if (fabs(L) <= mc_systol.tol) return d3v(p, a);
    double ex = p.x - a.x, ey = p.y - a.y, ez = p.z - a.z, cx = ey * dz - ez * dy, cy = ez * dx - dz * ex, cz = ex * dy - ey * dx;
    return fabs(sqrt(cx * cx + cy * cy + cz * cz) / L);
}
/* check_colinear (decomp/surfpick/core_dist.c @1806665c0): a piece <= tol -> yes; else dot of the unit directions >= 0.99985 and the perpendicular of the middle point to P1 P3 under 2 tol */
static int mc_colinear(V3 p1, V3 p2, V3 p3) {
    double ux = p2.x - p1.x, uy = p2.y - p1.y, uz = p2.z - p1.z, vx = p3.x - p2.x, vy = p3.y - p2.y, vz = p3.z - p2.z, lu = sqrt(ux * ux + uy * uy + uz * uz), lv = sqrt(vx * vx + vy * vy + vz * vz), tl = mc_systol.tol;
    if (lu <= tl || lv <= tl) return 1;
    if (!(0.99985 <= (ux * vx + uy * vy + uz * vz) / (lu * lv))) return 0;
    return mc_dist_pt_line(p2, p1, p3) < tl * 2;
}
/* a GE carrying an arc's signed sweep (only add_ge_lc reads it): the store keeps Mastercam's sign */
typedef struct { const CurveRec *c; int rev; double step, t, tp, acc, seg, P[3], t1, t2; } SplCreep;
static void spl_creep_init(SplCreep *k, const CurveRec *c, int rev); static void spl_creep(SplCreep *k, double d, double out[3]); static double spl_length(const CurveRec *c);
static int brk_spline_lines(int e, const CurveRec *c, double tol); static const CurveRec *ent_spline(int e); double sp_pick_t(int e, V3 w);   /* cui_spline.c */
static int brk_spline_arcs(int e, const CurveRec *c, double err, int filter);
static GE mc_ge(const Ent *M) {
    if (M->type == E_LINE) return ge_line(xy(M->a), xy(M->b), M->a.z);
    GE r; memset(&r, 0, sizeof r); r.t = GE_ARC; r.c = xy(M->c); r.r = M->r; r.a0 = M->a0; r.sw = M->sw; r.z = M->c.z; return r;
}
/* length_ent (mccore @180016e60): a line's length, |sweep| r */
static double mc_len(const Ent *M) { return M->type == E_LINE ? sqrt((M->b.x - M->a.x) * (M->b.x - M->a.x) + (M->b.y - M->a.y) * (M->b.y - M->a.y) + (M->b.z - M->a.z) * (M->b.z - M->a.z)) : fabs(M->sw) * M->r; }
/* t_on_gt (decomp/wfacc/core_kernels2.c:1984): t_on_line / t_on_arc (:3162, the angle in [0, 2pi), the three windings, the nearest to
   the range when none is inside) */
static double mc_t_on(const Ent *M, P2 p) {
    if (M->type == E_LINE) { double dx = M->b.x - M->a.x, dy = M->b.y - M->a.y, l2 = dx * dx + dy * dy; return l2 > 0 ? ((p.x - M->a.x) * dx + (p.y - M->a.y) * dy) / l2 : 0; }
    if (fabs(M->sw) < 1e-6) return 0;
    double dx = p.x - M->c.x, dy = p.y - M->c.y, a = 0;
    if (dx * dx + dy * dy >= 1e-12) { a = atan2(dy, dx); if (a < 0) a += 2 * M_PI; }
    double t = (a - M->a0) / M->sw;
    if (t >= 0 && t <= 1) return t;
    double t2 = (a + 2 * M_PI - M->a0) / M->sw;
    if (t2 >= 0 && t2 <= 1) return t2;
    double t3 = (a - 2 * M_PI - M->a0) / M->sw;
    if (t3 >= 0 && t3 <= 1) return t3;
    double r = t, d1 = fmin(fabs(t), fabs(1 - t)), d2 = fmin(fabs(t2), fabs(1 - t2));
    if (d2 < d1) { r = t2; d1 = d2; }
    double d3 = fmin(fabs(t3), fabs(1 - t3));
    if (d3 < d1) r = t3;
    return r;
}
/* restrict_angle(lo, hi, &a) (decomp/wfacc/core_kernels2.c:5316) */
static double mc_restrict(double lo, double hi, double a) { while (a < lo) a += 2 * M_PI; while (a > hi) a -= 2 * M_PI; return a; }
/* createArc3pt -> cr_a_3p / center_3pa (core_kernels2.c:2065-2068, :6587-6595) and find_sweep_angle (:6530-6545): any two points closer than tol fail; start p1; a's sweep to p2 in [0, 2pi] and to p3 in [0, 2pi]; clockwise (sweep to p3 - 2pi) when p3 is not past p2 */
static int mc_arc3(P2 p1, P2 p2, P2 p3, double z, GE *out) {
    double tl = mc_systol.tol;
    if (hypot(p1.x - p2.x, p1.y - p2.y) < tl || hypot(p1.x - p3.x, p1.y - p3.y) < tl || hypot(p2.x - p3.x, p2.y - p3.y) < tl) return 0;
    P2 o; double rr; if (!ge_circle3(p1, p2, p3, &o, &rr)) return 0;
    double a1 = mc_restrict(0, 2 * M_PI, atan2(p1.y - o.y, p1.x - o.x)), a2 = mc_restrict(0, 2 * M_PI, atan2(p2.y - o.y, p2.x - o.x)), a3 = mc_restrict(0, 2 * M_PI, atan2(p3.y - o.y, p3.x - o.x));
    double d2 = mc_restrict(0, 2 * M_PI, a2 - a1), d3 = mc_restrict(0, 2 * M_PI, a3 - a1), sw = d3 <= d2 ? d3 - 2 * M_PI : d3;
    memset(out, 0, sizeof *out); out->t = GE_ARC; out->c = o; out->r = rr; out->a0 = a1; out->sw = sw; out->z = z; return 1;
}
/* trim_ent_to_t(ent, n, pick, t0, t1, p6) (mccadutil @180061ee0, decomp/wfacc/mccadutil_kern3.c:1666). Lines: b = (p6 < pick && p6 < 1) || p6 < 0; per point
   b -> the start moved to it, else the end, b flips. Arcs: n 2 -> the points sorted, b from the pick against both; n 1 -> pick against
   p6 (0.5 when p6 is outside [-0.01, 1.01]); per point b -> the start, else the end; sweep restricted to [-2pi, 2pi]; the sign
   restored when the pick side demands it (:1834-1855). A full circle is no special case. */
static void mc_trim_to_t(Ent *M, int n, double pick, double t0, double t1, double p6) {
    double ts[2] = { t0, t1 };
    if (M->type == E_LINE) {
        int b = (p6 < pick && p6 < 1) || p6 < 0;
        V3 A = M->a, B = M->b;
        for (int i = 0; i < n; i++, b = !b) {
            V3 P = { A.x * (1 - ts[i]) + B.x * ts[i], A.y * (1 - ts[i]) + B.y * ts[i], A.z * (1 - ts[i]) + B.z * ts[i] };
            if (b) M->a = P; else M->b = P;
        }
        return;
    }
    if (M->type != E_ARC) return;
    int eq, gt;
    if (n == 2) { if (ts[1] < ts[0]) { double q = ts[0]; ts[0] = ts[1]; ts[1] = q; } double d = (pick - ts[0]) * (pick - ts[1]) * (ts[1] - ts[0]); eq = d == 0; gt = 0 < d; }
    else if (p6 > 1.01 || p6 < -0.01) { eq = pick == 0.5; gt = 0.5 < pick; }
    else { eq = pick == p6; gt = pick < p6; }
    double sa = M->a0, sw = M->sw, st = sa, en = sa + sw, nsw = sw;
    int b = !gt && !eq;
    for (int i = 0; i < n; i++, b = !b) {
        double A = sw * ts[i] + sa;
        if (b) st = A; else en = A;
        nsw = en - st;
        st = mc_restrict(0, 2 * M_PI, st);
        nsw = mc_restrict(-2 * M_PI, 2 * M_PI, nsw);
        M->a0 = st; M->sw = nsw;
    }
    int fix = 0;
    if (n == 2) { if (pick < ts[0] && pick < ts[1]) fix = 1; else if (pick > ts[0] && !(pick < ts[1] || pick == ts[1])) fix = 1; }
    else if (n == 1) { if (0 < sw && nsw <= 0 && nsw != 0) fix = 1; else if (!(0 <= sw) && !(nsw == 0 || nsw < 0)) fix = 1; }
    if (fix) M->sw = sw <= 0 ? fabs(nsw) - 2 * M_PI : 2 * M_PI - fabs(nsw);
}
/* trim_123's store after trim_ent_to_t (sketcher_trim_2.c:2872 / :2946): replace_ent only when the length is >= tol and < 1e12 - otherwise the
   record stays as it was */
static int mc_trim_store(int e, Ent *M) {
    double L = mc_len(M);
    if (!(mc_systol.tol <= L && L < 1e12)) return 0;
    GE r = mc_ge(M); replace_ent(e, &r, 1); return 1;
}
static int mc_trim1(int e, P2 k, P2 X) { Ent M = F.ents[e]; double t = mc_t_on(&M, X); mc_trim_to_t(&M, 1, mc_t_on(&M, k), t, 0, t); return mc_trim_store(e, &M); }
/* the seed point of intersect_ents (mccore_inters.c:141-153): eval_gt_pt at each entity's pick parameter, then the midpoint; gp_ints returns first the hit
   nearer it (closest_pt, :564) */
static P2 seed_pt(const GE *g, P2 k) { return ge_at(g, ge_param(g, k)); }
static P2 seed_mid(const GE *a, P2 ka, const GE *b, P2 kb) { P2 p = seed_pt(a, ka), q = seed_pt(b, kb); return (P2){ (p.x + q.x) / 2, (p.y + q.y) / 2 }; }
/* tan_on_ent_from_t (decomp/wfacc/core_kernels3.c:251): the unit tangent along the entity's own direction */
static P2 mc_tan(const Ent *M, double t) {
    if (M->type == E_LINE) { double dx = M->b.x - M->a.x, dy = M->b.y - M->a.y, l = hypot(dx, dy); return l > 0 ? (P2){ dx / l, dy / l } : (P2){ 1, 0 }; }
    double a = M->a0 + t * M->sw; return M->sw >= 0 ? (P2){ -sin(a), cos(a) } : (P2){ sin(a), -cos(a) };
}
/* t_from_pt_2d with param_6 0 (decomp/wfacc/core_kernels3.c:75): a Newton iteration from the seed - for a line t_on_line; for an arc its fixed point is the winding of
   the point's angle nearest the seed */
static double mc_t_seed(const Ent *M, double seed, P2 p) {
    if (M->type == E_LINE) return mc_t_on(M, p);
    double dx = p.x - M->c.x, dy = p.y - M->c.y, a = atan2(dy, dx), per = 2 * M_PI / fabs(M->sw);
    double t = (a - M->a0) / M->sw;
    return t + round((seed - t) / per) * per;
}
/* one selected curve of Trim Many's Trim (FUN_1800dc870, sketcher_trim_2.c:3060-3370): the trimmer's and the curve's carrier hits with the seeds (the trimmer's pick t, the curve's middle),
   the hits sorted by the seed midpoint; a second hit (only with an arc in the pair) counts only inside the trimmer and strictly inside the curve;
   a first hit on the curve's end is dropped for the second. Per hit: the side by the tangents' cross product (>= -tol against the flag) picks t +- 0.01 as
   the point to keep, trim_ent_to_t - one record rewritten per curve; hits past an end extend it. */
static int mc_trim_many_one(int e, int ei, double tpick, int flag) {
    const Ent *Tp = &F.ents[ei]; Ent T = *Tp, M = F.ents[e]; double tl = mc_systol.tol;
    GE gt = ent_ge(ei), gc = ent_ge(e); P2 x[2];
    P2 ksp = T.type == E_LINE ? (P2){ T.a.x + (T.b.x - T.a.x) * tpick, T.a.y + (T.b.y - T.a.y) * tpick }
                              : (P2){ T.c.x + T.r * cos(T.a0 + tpick * T.sw), T.c.y + T.r * sin(T.a0 + tpick * T.sw) };
    GE gs = M.type == E_LINE ? ge_line(xy(M.a), xy(M.b), 0) : gc;
    P2 mid = ge_at(&gs, 0.5), S = { (ksp.x + mid.x) / 2, (ksp.y + mid.y) / 2 };
    int n = mc_inter(&gt, &gc, S, x); if (!n) return 0;                 /* intersect_ents (the hits already sorted by the seed midpoint) */
    int second = n == 2 && (T.type == E_ARC || M.type == E_ARC);
    P2 X1 = x[0], X2 = n == 2 ? x[1] : x[0];
    double ttr1 = mc_t_on(&T, X1), tc1 = mc_t_on(&M, X1), ttr2 = mc_t_on(&T, X2), tc2 = mc_t_on(&M, X2);
    if (second && (1 + tl <= ttr2 || ttr2 <= 0 - tl || 1 - tl <= tc2 || tc2 <= 0 + tl)) second = 0;
    if (!((tc1 < 1 - tl || 1 + tl < tc1) && (0 + tl < tc1 || tc1 < 0 - tl))) {
        if (!second) return 0;
        X1 = X2; ttr1 = ttr2; tc1 = tc2; second = 0;
    }
    int done = 0, side;
    { P2 u = mc_tan(&T, ttr1), v = mc_tan(&M, tc1); int b4 = -tl <= v.x * u.y - u.x * v.y;
      double keep = b4 != flag ? tc1 + 0.01 : tc1 - 0.01;
      if (!((tc1 <= 0 && keep <= tc1) || (1 <= tc1 && tc1 <= keep))) { mc_trim_to_t(&M, 1, keep, tc1, 0, tc1); done = 1; }
      side = b4; }
    if (second) {
        P2 u = mc_tan(&T, ttr2); double d1 = tc2;
        if (done) {
            if (M.type == E_ARC) {
                if (tc2 < tc1 && flag != side) { d1 = tc2 / tc1; if (tc2 < d1) d1 = tc2 / (1 - tc1); d1 += 1.0; }
                if (tc1 < tc2 && flag == side) d1 = (tc2 / tc1 + 1.0) * -1.0;
            }
            tc2 = mc_t_seed(&M, d1, X2);
        }
        P2 v = mc_tan(&M, tc2); int b4 = -tl <= v.x * u.y - u.x * v.y;
        double keep = b4 != flag ? tc2 + 0.01 : tc2 - 0.01;
        if (!((tc2 <= 0 && keep <= tc2) || (1 <= tc2 && tc2 <= keep))) { mc_trim_to_t(&M, 1, keep, tc2, 0, tc2); done = 1; }
    }
    if (!done) return 0;
    GE r = mc_ge(&M); replace_ent(e, &r, 1); return 1;
}

/* ---- Fillet: find_fil_gt (viewer/mcfillet.c, oracle tools/tests/fillet_oracle.sh) and make_fillet / FUN_18005f380's store (decomp/wfacc/core_kernels.c:1211-1283, mccadutil_kern3.c:2143-2169) */
static void mc_fil_g(const Ent *M, McfG *g) { memset(g, 0, sizeof *g); if (M->type == E_LINE) { g->line = 1; g->x1 = M->a.x; g->y1 = M->a.y; g->x2 = M->b.x; g->y2 = M->b.y; } else { g->cx = M->c.x; g->cy = M->c.y; g->r = M->r; g->sa = M->a0; g->sw = M->sw; } }
static int mc_fillet_pair(Ent *A, Ent *B, double pk0, double pk1, int style, int use_dir, int fil_dir, int trim, GE *arc) {
    McfG G[2]; mc_fil_g(A, &G[0]); mc_fil_g(B, &G[1]); double pk[2] = { pk0, pk1 }; McfArc C[8]; int sel = -1;
    int n = mcf_find_fil(G, gp.r, pk, style, 1, mc_systol.tol, mc_systol.min_arc_length, C, 8, &sel, use_dir, fil_dir);
    if (n < 1 || sel < 0) return 0;                       /* several solutions: SelectArcToKeep (prompt 0x16ea) lets the user choose, not ported; Mastercam's proposal (the highlighted one) is kept */
    memset(arc, 0, sizeof *arc); arc->t = GE_ARC; arc->c = (P2){ C[sel].cx, C[sel].cy }; arc->r = C[sel].r; arc->a0 = C[sel].sa; arc->sw = C[sel].sw; arc->z = A->type == E_LINE ? A->a.z : A->c.z;
    if (trim) { mc_trim_to_t(A, 1, pk0, C[sel].t1[0], 0.0, C[sel].t2[0]); mc_trim_to_t(B, 1, pk1, C[sel].t1[1], 0.0, C[sel].t2[1]); }
    return 1;
}
/* make_fillet's store (decomp/wfacc/core_kernels.c, the trim branch, LAB_180259b50): an arc with |sweep| r < tol or a line shorter than tol is DELETED, else replace_ent */
static void mc_fillet_store(int e, Ent *M) {
    double L = M->type == E_LINE ? mc_len(M) : fabs(M->sw) * M->r;
    if (L < mc_systol.tol) { mod_del(e); return; }
    GE r = mc_ge(M); replace_ent(e, &r, 1);
}
/* ---- find_fil_ent (viewer/mcfillet_ent.c): the app's side: the entity's McfEnt and the pick's parameter (get_event: lines / arcs t_on_ent; on a spline the pick parameter is not decoded here: the nearest of 400 samples, inferred - the iteration converges on the same stretch from any seed) */
static double fe_seed(const McfEnt *e, const Ent *M, P2 k) {
    if (e->kind != 2) return mc_t_on(M, k);
    double bt = e->t1, bd = 1e38; for (int i = 0; i <= 400; i++) { double t = e->t1 + (e->t2 - e->t1) * i / 400.0, p[2], d[3]; mcf_ent_eval(e, t, p, d); double q = hypot(p[0] - k.x, p[1] - k.y); if (q < bd) { bd = q; bt = t; } }
    return bt;
}
static int fe_init(McfEnt *e, int id) {
    memset(e, 0, sizeof *e); const Ent *M = &F.ents[id]; double tl = mc_systol.tol;
    if (M->type == E_LINE) { e->kind = 0; e->ax = M->a.x; e->ay = M->a.y; e->az = M->a.z; e->bx = M->b.x; e->by = M->b.y; e->bz = M->b.z; e->t2 = 1; e->closed = hypot(M->a.x - M->b.x, M->a.y - M->b.y) < tl; return 1; }
    if (M->type == E_ARC) { e->kind = 1; e->cx = M->c.x; e->cy = M->c.y; e->r = M->r; e->a0 = M->a0; e->sw = M->sw; e->t2 = 1; e->closed = fabs(fabs(M->sw) - 2 * M_PI) < 1e-9; return 1; }
    const CurveRec *c = ent_spline(id); if (!c) return 0; e->kind = 2; e->c = c; mcs_spline_t1t2(c, &e->t1, &e->t2);
    double P0[2], P1[2], d[3]; mcf_ent_eval(e, e->t1, P0, d); mcf_ent_eval(e, e->t2, P1, d); e->closed = hypot(P0[0] - P1[0], P0[1] - P1[1]) < tl; return 1;
}
/* trim_ent_to_t's spline branch (mccadutil_kern3.c:1749-1775): the record's t1 (+0x98) and start point (+0x68), or t2 (+0xa0) and end point (+0x80); b = (p6 < pick && p6 < max t) || p6 < min t; a record that is an E_SPLINE gets a copy with new values (nothing else in the record changes) */
static int mc_trim_spline(int e, double pick, double t, double p6) {
    const CurveRec *c = ent_spline(e); if (!c) return 0; double t1, t2; mcs_spline_t1t2(c, &t1, &t2); int b = (p6 < pick && p6 < t2) || p6 < t1;
    double o[3][3]; if (mcs_spline_eval_d(c, t, 0, o)) return 0;
    uint32_t n = c->n; uint8_t *r = malloc(n); if (!r) return 0; memcpy(r, c->rec, n); memcpy(r + (b ? 0x98 : 0xa0), &t, 8); memcpy(r + (b ? 0x68 : 0x80), o[0], 24);
    Ent src = F.ents[e]; mod_del(e); int q = mcam_new_curve(&F, r, n, src.level, src.color); free(r);
    if (q >= 0) { Ent *m = &F.ents[q]; m->ln_style = src.ln_style >= 1 ? src.ln_style : 1; m->pt_style = src.pt_style; m->width = src.width >= 1 ? src.width : 1; for (int k = m->first_poly; k < m->first_poly + m->npoly && k < F.npolys; k++) { F.polys[k].ln_style = (uint8_t)m->ln_style; F.polys[k].width = (uint8_t)m->width; } doc_dirty = 1; }
    return q >= 0;
}
/* the intersection parameters for trim_ent_to_t's side test: intersect_ents_spl (seeds: the tangent points) by sampling the two curves (Mastercam's intersect_ents_spl is not ported: this only decides which end is trimmed; inferred) */
static int fe_inters(const McfEnt E[2], double tl[2], double out[2]) {
    enum { N = 400 }; double a0 = E[0].t1, a1 = E[0].t2, b0 = E[1].t1, b1 = E[1].t2, best = 1e38; int found = 0; static double P[N + 1][2], Q[N + 1][2]; double d[3];
    for (int i = 0; i <= N; i++) { mcf_ent_eval(&E[0], a0 + (a1 - a0) * i / N, P[i], d); mcf_ent_eval(&E[1], b0 + (b1 - b0) * i / N, Q[i], d); }
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
        double ax = P[i + 1][0] - P[i][0], ay = P[i + 1][1] - P[i][1], bx = Q[j + 1][0] - Q[j][0], by = Q[j + 1][1] - Q[j][1], det = ax * by - ay * bx; if (det == 0.0) continue;
        double u = ((Q[j][0] - P[i][0]) * by - (Q[j][1] - P[i][1]) * bx) / det, v = ((Q[j][0] - P[i][0]) * ay - (Q[j][1] - P[i][1]) * ax) / det; if (u < 0 || u > 1 || v < 0 || v > 1) continue;
        double ta = a0 + (a1 - a0) * (i + u) / N, tb = b0 + (b1 - b0) * (j + v) / N, q = fabs(ta - tl[0]) + fabs(tb - tl[1]); if (q < best) { best = q; out[0] = ta; out[1] = tb; found = 1; } }
    return found;
}
/* ---- Ellipse: one NURBS (CalculateEllipse mccore @18023bfe0, ellipse_to_nurbs @1805ad020, FUN_1805ad6a0 / FUN_1805adaf0: decomp/wfacc/core_kernels2.c:2248, core_spline.c:2126 / 2195). */
typedef struct { double w, x, y; } McE3;
static const double mc_ek[12] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4 };
/* the rational quadratic at parameter t (a knot span per quadrant); P and dP/dt (x, y) */
static void mc_ell_eval(const McE3 *cp, double t, double P[2], double dP[2]) {
    int q = (int)floor(t); if (q < 0) q = 0; if (q > 3) q = 3;
    double s = t - q, b[3] = { (1 - s) * (1 - s), 2 * s * (1 - s), s * s }, db[3] = { -2 * (1 - s), 2 - 4 * s, 2 * s };
    const McE3 *c = cp + 2 * q; double Nx = 0, Ny = 0, D = 0, dNx = 0, dNy = 0, dD = 0;
    for (int i = 0; i < 3; i++) { Nx += b[i] * c[i].w * c[i].x; Ny += b[i] * c[i].w * c[i].y; D += b[i] * c[i].w; dNx += db[i] * c[i].w * c[i].x; dNy += db[i] * c[i].w * c[i].y; dD += db[i] * c[i].w; }
    P[0] = Nx / D; P[1] = Ny / D; dP[0] = (dNx - P[0] * dD) / D; dP[1] = (dNy - P[1] * dD) / D;
}
/* FUN_1805adaf0: Newton on the parameter; the step clamped to (t2 - t1) / 50, the parameter to the end knots, 100 rounds */
static double mc_ell_t(const McE3 *cp, double qx, double qy, double seed, double t1, double t2) {
    double lim = (t2 - t1) / 50.0, t = seed, tl = mc_systol.tol;
    for (int it = 0; it < 100; it++) {
        double P[2], d[2]; mc_ell_eval(cp, t, P, d);
        double dx = qx - P[0], dy = qy - P[1]; if (hypot(dx, dy) < tl) break;
        double g = d[1] * dy + d[0] * dx; if (fabs(g) <= 2e-07) break;
        double l = hypot(d[0], d[1]), st = g / (l * l); if (st > lim) st = lim; else if (st < -lim) st = -lim;
        t += st; if (t < 0) t = 0; else if (t > 4) t = 4;
    }
    return t;
}
static double mc_ell_end(const McE3 *cp, double A, double B, double a, double t1, double t2) {
    if (fabs(a - 90) < 0.01) return 1; if (fabs(a - 180) < 0.01) return 2; if (fabs(a - 270) < 0.01) return 3;
    double s = sin(a * M_PI / 180), c = cos(a * M_PI / 180), r = sqrt(A * A * B * B / (c * c * B * B + s * s * A * A));
    return mc_ell_t(cp, r * c, r * s, a / 90.0, t1, t2);
}
/* one NURBS ellipse entity at centre C: 12 knots, 9 points (weights 1 / sqrt 0.5, stored homogeneous w x y z: nbrat_3d_to_4d), end > start or the reversed arrays (start > end), rotated by rot, moved to C */
static int mc_ellipse(V3 C, double A, double B, double sa, double ea, double rot) {
    double tl = mc_systol.tol; if (fabs(sa) <= tl) sa = 0; if (fabs(ea) <= tl) ea = 360; if (fabs(rot) <= tl) rot = 0;
    if (A < tl || B < tl || fabs(ea - sa) <= 1e-8) return -1;
    double sq = sqrt(0.5), k[12]; memcpy(k, mc_ek, sizeof k);
    McE3 cp[9] = { { 1, A, 0 }, { sq, A, B }, { 1, 0, B }, { sq, -A, B }, { 1, -A, 0 }, { sq, -A, -B }, { 1, 0, -B }, { sq, A, -B }, { 1, A, 0 } };
    double p5 = sa, p6 = ea; int rv = 0;
    if (p6 < p5) { double d = p5 - p6; while (360 <= d) d -= 360; rv = 1; while (360 <= p6) p6 -= 360; p5 = p6; while (p5 < 0) p5 += 360; p6 = d + p5; }
    double t1 = 0, t2 = 4;
    if (0 < p5) t1 = mc_ell_end(cp, A, B, p5, t1, t2);
    if (p6 < 360) t2 = mc_ell_end(cp, A, B, p6, t1, t2);
    if (rv) { for (int i = 0; i < 12; i++) k[i] = -mc_ek[11 - i]; McE3 q[9]; for (int i = 0; i < 9; i++) q[i] = cp[8 - i]; memcpy(cp, q, sizeof q); double s = t1; t1 = -t2; t2 = -s; }
    double cr = cos(rot * M_PI / 180), sr = sin(rot * M_PI / 180);
    uint32_t n = 0xd8 + 8 * (12 + 4 * 9); uint8_t *r = calloc(1, n); if (!r) return -1;
    uint32_t cls = 0x80; int nk = 12, ncp = 9; memcpy(r, &cls, 4); memcpy(r + 0xc8, &nk, 4); memcpy(r + 0xcc, &ncp, 4); r[0xd0] = 2; r[0xd1] = 4; r[0xd2] = 0x13;   /* ellipse_to_nurbs: &0xfff1 | 0x11 (core_kernels2.c), then FUN_1805ad6a0 |= 2 unconditionally (core_spline.c:2126): CalculateEllipse's closed test is overwritten */
    #define WD(o_, v_) do { double d_ = (v_); memcpy(r + (o_), &d_, 8); } while (0)
    for (int i = 0; i < 12; i++) WD(0xd8 + 8 * i, k[i]);
    for (int i = 0; i < 9; i++) { double x = cp[i].x * cr - cp[i].y * sr + C.x, y = cp[i].x * sr + cp[i].y * cr + C.y, w = cp[i].w; int b = 0xd8 + 8 * (12 + 4 * i);
        WD(b, w); WD(b + 8, w * x); WD(b + 16, w * y); WD(b + 24, w * C.z); }
    WD(0x98, t1); WD(0xa0, t2);
    #define ENDPT(t_, off_) do { double t__ = (t_); if (rv) t__ = -t__; double P_[2], d_[2]; mc_ell_eval(rv ? (McE3[9]){ {1,A,0},{sq,A,B},{1,0,B},{sq,-A,B},{1,-A,0},{sq,-A,-B},{1,0,-B},{sq,A,-B},{1,A,0} } : cp, t__, P_, d_); \
        WD((off_), P_[0] * cr - P_[1] * sr + C.x); WD((off_) + 8, P_[0] * sr + P_[1] * cr + C.y); WD((off_) + 16, C.z); } while (0)
    ENDPT(t1, 0x68); ENDPT(t2, 0x80);
    #undef ENDPT
    { CurveRec cv = { 0, 0x80, r, n }; double cmax = 0, dt = (t2 - t1) / (double)(ncp * 2 - 2), s = 1.0 / (t2 - t1);   /* the record's +0xa8, as mcsb_arc_nurbs_rec */
      for (double tt = t1; tt <= t2; tt += dt) { double d[3][3]; if (mcs_spline_eval_d(&cv, tt, 2, d)) continue;
          double a[3] = { d[1][0] * s, d[1][1] * s, d[1][2] * s }, b[3] = { d[2][0] * s * s, d[2][1] * s * s, d[2][2] * s * s }, c[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
          double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), kk = la > 1e-6 ? sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) / la : 0; if (cmax < kk && kk < 1e9) cmax = kk; }
      if (cmax < 1e-9) cmax = 1e-9; WD(0xa8, cmax); }
    #undef WD
    int q = mcam_new_curve(&F, r, n, geo_level, geo_color); free(r);
    if (q >= 0) { attr_new_ent(q); doc_dirty = 1; overlay_dirty = 1; }
    return q;
}
