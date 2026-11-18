/* cam2d_pock_medax.c - Pocket "Minimize tool burial" (record 0x1259): mcmill's own pocket medial axis (POCKMDL.cpp) and
 * its link walk, used by the Parallel Spiral (methods 2 / 3) loop links of FUN_1801e9ee0 (l.336785). Included by
 * cam2d_pock.c after cam2d_pock_zz.c (uses its mccore ports t_on_gt / t_on_line / t_on_arc, pt_on_arc, la_ints, Equal).
 *
 * Source: the decode decomp/medax/pockmdl/pockmdl_spec.txt (cited "spec N.N"), read from
 * pockmdl_redecomp_sig.c (the POCKMDL functions re-decompiled with their recovered parameter lists, cited "R:") and
 * decomp/mcmill_all.c (cited "l.N"); disassembly in pockmdl_disasm_excerpts.txt (cited "D:addr").
 * Every constant, comparison strictness, loop order and tie rule is the decompiled one. Tags:
 *   [inferred]  my reading, not literal in the code;
 *   ours (...)  a mccore / CRT helper known by name only, implemented with its standard meaning.
 * mccore helpers that are decoded elsewhere and reused: t_on_gt / t_on_line / t_on_arc (cam2d_pock_zz.c zz_t_gt),
 * restrict_angle (cam2d_pock_tspiral.c pk_ts_restrict), dist_to_finite_line (decomp/mccore_proj2d.c, ported here),
 * proj_pt_ln (decomp/mccore_filter.c l.4912, ported here), pt_in_machinable_area / pt_in_ctour (decomp/mccore_pock.c
 * l.1334 / l.2893, ported here with the zz port's MCCEPtInBnd substitute), proj_pt_gt (mccore_pock.c l.4320).
 * Not decoded (ours): eval_gt_pt / eval_gt_tan / length_gt (pgeo gt_pt / gt_tan / gt_len), proj_pt_ar, ints_on_gt,
 * CalcIntersectRect, det3, dist_pt_l, pock_ccw (signed area), build_pock_bound_info (exact entity boxes), the MSVC CRT
 * qsort / bsearch (ported from the CRT's published algorithm: [inferred] that Mastercam 2017's CRT is that one),
 * gr_lin_mill's handling of a zero-length move (skipped, as cam2d_pock_zz.c zz_line_to).
 * The link stand-in FUN_1801e0f80 (pock_move_avoid_islands; its path around islands is NOT DECODED) is the caller's
 * existing one (cam2d_pock.c pk_e0f80).
 * "side" is always 'L' here (machine_pocket passes dl = 'L', D:1801d3878).
 */

/* ------------------------------------------------------------------------------------------------ data (spec 2) */
typedef struct { int id; Gt g; double bb[4]; } MdEnt;               /* entity 0x88: +0 id, +8 gt, +0x40/+0x50 bbox */
typedef struct { MdEnt *e; int n; int island; double bb[4]; } MdBnd; /* pock_bound: +0xc n, +0x10 ents, +2 island */
typedef struct { MdBnd *b; int n; } MdBs;
typedef struct MdNode {                                             /* NODE 0x50 (spec 2.3) */
    int id[3]; double x, y, z; char done[3]; struct MdNode *link[3]; struct MdNode *next;
} MdNode;
typedef struct { double x, y, r; } MdPt;
typedef struct MdEdge {                                             /* EDGE 0x58 (spec 2.4) */
    short type;                                                     /* 0 straight, 1 polyline */
    double x0, y0, x1, y1, r0, r1;                                  /* type 0 */
    MdPt *pts; int n, cap;                                          /* type 1 */
    int ida, idb;                                                   /* +0x38 / +0x3c entity pair */
    int state;                                                      /* +0x44 walk state */
    char excl;                                                      /* +0x48 */
    struct MdEdge *next;                                            /* +0x50 */
} MdEdge;
typedef struct { short t; double p[3]; double n[3]; char flag; } MdSurf;   /* roof surface 0x38 (spec 2.5) */

static void md_free_edges(MdEdge **l) {                             /* FUN_1801db4b0 (R) */
    MdEdge *e = *l;
    while (e) { MdEdge *nx = e->next; if (e->type == 1) free(e->pts); free(e); e = nx; }
    *l = NULL;
}
static void md_free_nodes(MdNode **l) {                             /* FUN_1801db520 (R) */
    MdNode *n = *l;
    while (n) { MdNode *nx = n->next; free(n); n = nx; }
    *l = NULL;
}
static void md_free_bs(MdBs *s) { for (int i = 0; i < s->n; i++) free(s->b[i].e); free(s->b); s->b = NULL; s->n = 0; }

/* ------------------------------------------------------------------------------------------------ mccore helpers */
/* proj_pt_ln (systypes 0x180002780, decomp/mccore_filter.c l.4912): t unclamped, flag 0 only for a zero-length line */
static int md_proj_pt_ln(PV q, PV a, PV b, PV *o, double *t) {
    double dx = b.x - a.x, dy = b.y - a.y, d = dy * dy + dx * dx;
    if (d == 0.0) return 0;
    double s = ((q.y - a.y) * dy + (q.x - a.x) * dx) / d;
    *t = s;
    *o = pv((1.0 - s) * a.x + s * b.x, (1.0 - s) * a.y + s * b.y);
    return 1;
}
/* dist_to_finite_line (mccore 0x1804b6680, decomp/mccore_proj2d.c l.83; global tol = CAM_SYS_TOL) */
static double md_dist_finite(PV q, PV a, PV b, double lim) {
    double dx = b.x - a.x, dy = b.y - a.y, L = sqrt(dx * dx + dy * dy), d2;
    if (CAM_SYS_TOL <= L) {
        double h = fabs((q.x - a.x) * dy - (q.y - a.y) * dx) / L;
        if (lim <= h) return h;
        double s = ((q.x - a.x) * dx + (q.y - a.y) * dy) / L;
        PV e;
        if (0.0 <= s) { if (s <= L) return h; if (L + lim <= s) return h; e = b; }
        else { if (s <= -lim) return h; e = a; }
        d2 = (e.y - q.y) * (e.y - q.y) + (e.x - q.x) * (e.x - q.x);
    } else d2 = (a.y - q.y) * (a.y - q.y) + (a.x - q.x) * (a.x - q.x);
    return sqrt(d2);
}
/* proj_pt_gt (mccore 0x1804b4280, decomp/mccore_pock.c l.4320): 'L' proj_pt_ln, 'A' proj_pt_ar.
   proj_pt_ar: ours (mccore proj_pt_ar not decoded): the circle point towards q, on = q's angle within the sweep */
static int md_proj_pt_gt(PV q, const Gt *g, PV *o) {
    double t;
    if (g->t == 'L') return md_proj_pt_ln(q, g->a, g->b, o, &t);
    if (g->t == 'A') {
        double dx = q.x - g->c.x, dy = q.y - g->c.y, d = sqrt(dx * dx + dy * dy);
        if (d == 0.0) return 0;
        *o = pv(g->c.x + g->r * dx / d, g->c.y + g->r * dy / d);
        double s = zz_t_arc(q, g);
        return s >= 0.0 && s <= 1.0;
    }
    return 0;
}
/* pt_in_ctour (mccore 0x1805e6e30, mccore_pock.c l.2893): 0 within t1 of an entity start (get_ep(.., 1)) or on the
   bound (MCCEPtInBnd: ours, within t2, as cam2d_pock_zz.c zz_in_ctour), 1 inside, -1 outside */
static int md_pt_in_ctour(const Gl *l, PV p, double t1, double t2) {
    for (int i = 0; i < l->n; i++) {
        PV s = gt_start(&l->e[i]);
        if (sqrt((p.x - s.x) * (p.x - s.x) + (p.y - s.y) * (p.y - s.y)) < t1) return 0;
    }
    if (gl_closest(l, p, NULL, NULL, NULL) <= t2) return 0;
    Gls one = { (Gl *)l, 1, 1 };
    return gls_inside(&one, p, t2) ? 1 : -1;
}
/* pt_in_machinable_area (mccore 0x1805e6f20, mccore_pock.c l.1334) */
static int md_in_area(const Gls *s, PV p, char *on, double t1, double t2) {
    int inside = 0;
    if (on) *on = 0;
    for (int i = 0; i < s->n; i++) {
        int c = md_pt_in_ctour(&s->l[i], p, t1, t2);
        if (c == 0 && on) *on = 1;
        if (!s->l[i].island) { if (c == 1) inside = 1; else if (!inside && c == -1) return 0; }
        else if (c == 1) return 0;
    }
    return 1;
}
/* CalcIntersectRect: ours (not decoded): the two boxes overlap within tol */
static int md_rect(const double a[4], const double b[4], double tol) {
    return !(a[2] + tol < b[0] || b[2] + tol < a[0] || a[3] + tol < b[1] || b[3] + tol < a[1]);
}
/* ints_on_gt(line, entity, .., tol 1e-6, &flag): ours (mccore ints_on_gt not decoded, pocket_engine_b6.md l.158): the
   points on both the segment and the entity (parameters within tol / length), flag = some point found */
static int md_ints_seg(PV a, PV b, const Gt *e, double tol, PV o[2]) {
    double dx = b.x - a.x, dy = b.y - a.y, L = sqrt(dx * dx + dy * dy);
    if (L <= 0) return 0;
    double eps = tol / L; int n = 0;
    if (e->t == 'L') {
        double ex = e->b.x - e->a.x, ey = e->b.y - e->a.y, den = dx * ey - dy * ex, Le = sqrt(ex * ex + ey * ey);
        if (fabs(den) < 1e-15 || Le <= 0) return 0;
        double s = ((e->a.x - a.x) * ey - (e->a.y - a.y) * ex) / den, w = ((e->a.x - a.x) * dy - (e->a.y - a.y) * dx) / den;
        if (s >= -eps && s <= 1 + eps && w >= -tol / Le && w <= 1 + tol / Le) o[n++] = pv(a.x + dx * s, a.y + dy * s);
    } else if (e->t == 'A') {
        PV p1, p2;
        if (!zz_la_ints(a, b, e->c, e->r, &p1, &p2)) return 0;
        PV c[2] = { p1, p2 }; int nc = (p1.x == p2.x && p1.y == p2.y) ? 1 : 2;
        for (int k = 0; k < nc; k++) {
            double s = ((c[k].x - a.x) * dx + (c[k].y - a.y) * dy) / (L * L);
            if (s >= -eps && s <= 1 + eps && zz_pt_on_arc(c[k], e, tol)) o[n++] = c[k];
        }
    }
    return n;
}
/* det3: ours (mccore det3 not decoded): the determinant of the matrix with columns a, b, c */
static double md_det3(const double a[3], const double b[3], const double c[3]) {
    return a[0] * (b[1] * c[2] - b[2] * c[1]) - b[0] * (a[1] * c[2] - a[2] * c[1]) + c[0] * (a[1] * b[2] - a[2] * b[1]);
}
/* dist_pt_l: ours (mccore dist_pt_l not decoded): distance of p to the line through the chord */
static double md_dist_pt_l(PV p, PV a, PV b) {
    double dx = b.x - a.x, dy = b.y - a.y, L = sqrt(dx * dx + dy * dy);
    if (L == 0) return sqrt((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y));
    return fabs((p.x - a.x) * dy - (p.y - a.y) * dx) / L;
}

/* ------------------------------------------------------------------------------------------------ MSVC CRT qsort / bsearch */
/* ours [inferred]: the Microsoft CRT qsort (qsort.c: median of three, CUTOFF 8 shortsort = repeated max to the end, the
   partition loop with "mid" tracking, the smaller partition recursed last). The comparator LAB_1801d7d10 is not
   transitive (|d| < 1e-12 -> 0), so equal-key order is the algorithm's: spec 8. */
static void md_swap(char *a, char *b, size_t w) { if (a != b) while (w--) { char t = *a; *a++ = *b; *b++ = t; } }
static void md_shortsort(char *lo, char *hi, size_t w, int (*cmp)(const void *, const void *)) {
    while (hi > lo) {
        char *max = lo;
        for (char *p = lo + w; p <= hi; p += w) if (cmp(p, max) > 0) max = p;
        md_swap(max, hi, w);
        hi -= w;
    }
}
static void md_qsort(void *base, size_t num, size_t w, int (*cmp)(const void *, const void *)) {
    char *lostk[64], *histk[64]; int stkptr = 0;
    if (num < 2) return;
    char *lo = base, *hi = (char *)base + w * (num - 1);
recurse:;
    size_t size = (size_t)(hi - lo) / w + 1;
    if (size <= 8) md_shortsort(lo, hi, w, cmp);
    else {
        char *mid = lo + (size / 2) * w;
        if (cmp(lo, mid) > 0) md_swap(lo, mid, w);
        if (cmp(lo, hi) > 0) md_swap(lo, hi, w);
        if (cmp(mid, hi) > 0) md_swap(mid, hi, w);
        char *loguy = lo, *higuy = hi;
        for (;;) {
            if (mid > loguy) { do loguy += w; while (loguy < mid && cmp(loguy, mid) <= 0); }
            if (mid <= loguy) { do loguy += w; while (loguy <= hi && cmp(loguy, mid) <= 0); }
            do higuy -= w; while (higuy > mid && cmp(higuy, mid) > 0);
            if (higuy < loguy) break;
            md_swap(loguy, higuy, w);
            if (mid == higuy) mid = loguy;
        }
        higuy += w;
        if (mid < higuy) { do higuy -= w; while (higuy > mid && cmp(higuy, mid) == 0); }
        if (mid >= higuy) { do higuy -= w; while (higuy > lo && cmp(higuy, mid) == 0); }
        if (higuy - lo >= hi - loguy) {
            if (lo < higuy) { lostk[stkptr] = lo; histk[stkptr] = higuy; ++stkptr; }
            if (loguy < hi) { lo = loguy; goto recurse; }
        } else {
            if (loguy < hi) { lostk[stkptr] = loguy; histk[stkptr] = hi; ++stkptr; }
            if (lo < higuy) { hi = higuy; goto recurse; }
        }
    }
    --stkptr;
    if (stkptr >= 0) { lo = lostk[stkptr]; hi = histk[stkptr]; goto recurse; }
}
/* ours [inferred]: the Microsoft CRT bsearch (bsearch.c) */
static void *md_bsearch(const void *key, void *base, size_t num, size_t w, int (*cmp)(const void *, const void *)) {
    char *lo = base, *hi = (char *)base + (num ? num - 1 : 0) * w;
    if (!num) return NULL;
    while (lo <= hi) {
        size_t half = num / 2;
        if (half) {
            char *mid = lo + ((num & 1) ? half : half - 1) * w;
            int r = cmp(key, mid);
            if (!r) return mid;
            if (r < 0) { hi = mid - w; num = (num & 1) ? half : half - 1; }
            else { lo = mid + w; num = half; }
        } else if (num) return cmp(key, lo) ? NULL : lo;
        else break;
    }
    return NULL;
}

/* ------------------------------------------------------------------------------------------------ geometry (spec 2.5 / 2.6) */
/* FUN_1801d9930 (D:1801d9930): T = eval_gt_tan, N = left ('L') / right normal */
static void md_d9930(const Gt *g, double t, char side, PV *T, PV *N) {
    *T = gt_tan(g, t);
    if (side == 'L') *N = pv(-T->y, T->x); else *N = pv(T->y, -T->x);
}
/* FUN_1801dd1f0 (R): the 45-degree roof plane of a line */
static void md_dd1f0(const Gt *g, char side, MdSurf *s) {
    double h = sqrt(2.0), dx = g->b.x - g->a.x, dy = g->b.y - g->a.y;
    h = h * 0.5;
    double L = sqrt(dx * dx + dy * dy), ux, uy = 0.0;
    if (L == 0.0) ux = 0.0; else { ux = dx / L; uy = dy / L; }
    s->n[2] = h;
    double a = uy * h, b = ux * h;
    if (side == 'L') a = -a; else b = -b;
    s->n[1] = b; s->n[0] = a;
    s->p[0] = g->a.x; s->p[2] = 0; s->p[1] = g->a.y;
}
/* FUN_1801d7b90 (R): the cone of an arc: sweep > 0 (strict) apex (c, -r) upper nappe, else apex (c, +r) lower */
static void md_d7b90(const Gt *g, MdSurf *s) {
    s->p[0] = g->c.x; s->p[1] = g->c.y;
    if (0.0 < g->sw) { s->p[2] = -g->r; s->flag = 1; return; }
    s->p[2] = g->r; s->flag = 0;
}
/* FUN_1801dba30 (R) */
static void md_dba30(const Gt *g, char side, MdSurf *s) {
    memset(s, 0, sizeof *s);
    if (g->t == 'L') { s->t = 0; md_dd1f0(g, side, s); return; }
    if (g->t == 'A') { s->t = 1; md_d7b90(g, s); }
}
/* FUN_1801dd1d0 (R): zero-radius arc */
static int md_zero_arc(const Gt *g) { return g->t == 'A' && g->r < 1e-12; }
/* FUN_1801d7ef0 (R, D:1801d7ef0): the 45-degree ray from the entity point at t along the normal */
static void md_d7ef0(const Gt *g, double t, char side, double o[6]) {
    PV T, N; md_d9930(g, t, side, &T, &N);
    PV P = gt_pt(g, t);
    o[2] = 0; o[1] = P.y; o[0] = P.x;
    o[3] = P.x + N.x * 0.707106781186548; o[4] = P.y + N.y * 0.707106781186548;
    o[5] = -0.707106781186548;                                      /* 0xbfe6a09e667f3bd1 */
}
/* FUN_1801d7d50 (R): the exact line-line bisector ray: P + n1 x n2 (unit, pointing down) */
static void md_d7d50(const double P[3], const Gt *g1, const Gt *g2, char side, double o[6]) {
    MdSurf s1, s2; md_dd1f0(g1, side, &s1); md_dd1f0(g2, side, &s2);
    const double *a = s1.n, *b = s2.n;
    double dx = b[2] * a[1] - a[2] * b[1], dy = b[0] * a[2] - a[0] * b[2], dz = a[0] * b[1] - b[0] * a[1];
    double L = sqrt(dx * dx + dy * dy + dz * dz);
    if (L == 0.0) { dz = 0.0; dy = 0.0; dx = 0.0; }
    else {
        dx = dx / L; dz = dz / L; dy = dy / L;
        if (0.0 < dz) { dx = dx * -1.0; dy = dy * -1.0; dz = dz * -1.0; }
    }
    o[0] = P[0]; o[1] = P[1]; o[2] = P[2];
    o[3] = dx + o[0]; o[4] = dy + o[1]; o[5] = dz + o[2];
}
/* FUN_1801dcb10 (R): segment / plane */
static int md_dcb10(const double s[6], const MdSurf *pl, double X[3], double *u) {
    double dx = s[3] - s[0], dz = s[5] - s[2], dy = s[4] - s[1];
    double den = dy * pl->n[1] + dx * pl->n[0] + dz * pl->n[2];
    if (fabs(den) < 1e-12) return 0;
    double v = ((pl->p[1] - s[1]) * pl->n[1] + (pl->p[0] - s[0]) * pl->n[0] + (pl->p[2] - s[2]) * pl->n[2]) / den;
    *u = v;
    X[0] = v * dx + s[0]; X[1] = v * dy + s[1]; X[2] = dz * v + s[2];
    return 1;
}
/* FUN_1801dc6f0 (R, D:1801dc850-1801dc920): segment / cone roots on the cone's nappe */
static int md_dc6f0(const double s[6], const MdSurf *c, double X[2][3], double U[2]) {
    int n = 0;
    double z0 = s[2], x0 = s[0], y0 = s[1], az = c->p[2];
    double dx = s[3] - x0, dy = s[4] - y0, dz = s[5] - z0;
    double wy = y0 - c->p[1], wx = x0 - c->p[0], wz = z0 - az;
    double a = (dy * dy + dx * dx) - dz * dz;
    double b = ((wy * dy + wx * dx) - wz * dz) * 2.0;
    double cc = (wy * wy + wx * wx) - wz * wz;
    double disc = b * b - a * 4.0 * cc;
    if (!(0.0 < disc)) return 0;
    if (fabs(a) <= 1e-12) {
        if (1e-12 < fabs(b)) {
            double u = -(cc / b), x = x0 + dx * u, y = y0 + dy * u, z = z0 + dz * u;
            int bad = c->flag == 0 ? az < z : z < az;
            if (!bad && z != az) { U[n] = u; X[n][0] = x; X[n][1] = y; X[n][2] = z; n++; }
        }
        return n;
    }
    double sq = sqrt(disc);
    for (int k = 0; k < 2; k++) {
        double u = (sq - b) / (a * 2.0), z = dz * u + s[2];
        double x = dx * u + s[0], y = dy * u + s[1];
        int lt = c->flag == 0 ? az < z : z < az, eq = z == az;
        if (!lt && !eq) { U[n] = u; X[n][0] = x; X[n][1] = y; X[n][2] = z; n++; }
        sq = sq * -1.0;
    }
    return n;
}
/* FUN_1801dca20 (R): the first hit of a ray (u > 0) on a surface */
static int md_dca20(const double r[6], const MdSurf *s, double X[3]) {
    if (s->t == 0) { double u; if (!md_dcb10(r, s, X, &u)) return 0; return 0.0 < u; }
    if (s->t == 1) {
        double XX[2][3], U[2], best = 1e38; int n = md_dc6f0(r, s, XX, U), ok = 0;
        for (int k = 0; k < n; k++) if (0.0 < U[k] && U[k] < best) { X[0] = XX[k][0]; X[1] = XX[k][1]; X[2] = XX[k][2]; best = U[k]; ok = 1; }
        return ok;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ bounds lookups */
/* FUN_1801db630 (R): the gt of the entity with that global id (first bound with id <= last id and 0 <= id - first id < n) */
static const Gt *md_db630(const MdBs *s, int id) {
    for (int i = 0; i < s->n; i++) {
        const MdBnd *b = &s->b[i];
        if (b->n < 1) continue;
        int k = id - b->e[0].id;
        if (id <= b->e[b->n - 1].id && -1 < k && k < b->n) return &b->e[k].g;
    }
    return NULL;
}
static const Gt *md_gt_or_zero(const MdBs *s, int id) {   /* not found: Mastercam leaves its local gt untouched (ours: zero) */
    static const Gt z; const Gt *g = md_db630(s, id); return g ? g : &z;
}
/* FUN_1801db560 (R): bound and index of an id (first bound with id <= its last id) */
static const MdBnd *md_db560(const MdBs *s, int id, int *idx) {
    for (int i = 0; i < s->n; i++) {
        const MdBnd *b = &s->b[i];
        if (b->n > 0 && id <= b->e[b->n - 1].id) { *idx = id - b->e[0].id; return b; }
    }
    *idx = -1; return NULL;
}
static void md_db5b0(const MdNode *n, int k, int *a, int *b) {      /* FUN_1801db5b0 (R): pair k of a node */
    if (k == 0) { *a = n->id[0]; *b = n->id[1]; return; }
    if (k != 1) { *a = n->id[1]; *b = n->id[2]; return; }
    *a = n->id[0]; *b = n->id[2];
}
static void md_db5f0(int k, int *i, int *j) {                       /* FUN_1801db5f0 (R) */
    if (k == 0) { *i = 0; *j = 1; return; }
    if (k != 1) { *i = 1; *j = 2; return; }
    *i = 0; *j = 2;
}
static int md_dd640(const MdNode *n) { return !(-1 < n->id[0] && -1 < n->id[1] && -1 < n->id[2]); }   /* FUN_1801dd640 */

/* ------------------------------------------------------------------------------------------------ nodes (spec 2.3, 3.7) */
/* FUN_1801dce70 (R): 1 = no duplicate (no node with > 2 equal id pairs within 2 tol, 3D) */
static int md_dce70(const MdNode *l, const MdNode *r, double tol) {
    for (; l; l = l->next) {
        int c = 0;
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) if (l->id[i] == r->id[j]) c++;
        if (2 < c) {
            double dx = r->x - l->x, dy = r->y - l->y, dz = r->z - l->z;
            if (sqrt(dx * dx + dy * dy + dz * dz) < tol * 2.0) return 0;
        }
    }
    return 1;
}
/* FUN_1801d7ad0 (R): insert a copy at the head unless a duplicate: 0 inserted, 1 duplicate */
static int md_d7ad0(MdNode **l, const MdNode *r, double tol) {
    if (!md_dce70(*l, r, tol)) return 1;
    MdNode *n = calloc(1, sizeof *n);
    *n = *r; n->next = *l; *l = n;
    return 0;
}
/* FUN_1801d7b60 (R): append tmp after the last node (an empty list stays empty: tmp lost - freed here) */
static void md_d7b60(MdNode **l, MdNode *tmp) {
    if (*l) { MdNode *n = *l; while (n->next) n = n->next; n->next = tmp; return; }
    *l = NULL; md_free_nodes(&tmp);
}
/* FUN_1801db6a0 (l.327793, R): the point is on the material side of its nearest boundary entity and not above its roof */
static int md_db6a0(const MdBs *s, char side, const double X[3], double tol) {
    double best = 1e38, bt = 0; const Gt *E = NULL; PV C = pv(0, 0), Q = pv(X[0], X[1]);
    for (int i = 0; i < s->n; i++) for (int k = 0; k < s->b[i].n; k++) {
        const Gt *g = &s->b[i].e[k].g;
        double t = zz_t_gt(Q, g);
        if (t < -2e-07 || 1.0000002 < t) t = 0.0;
        PV P = gt_pt(g, t);
        double d = sqrt((P.y - Q.y) * (P.y - Q.y) + (P.x - Q.x) * (P.x - Q.x));
        if (d < best) { bt = t; E = g; C = P; best = d; }
    }
    if (!E) return 0;
    PV T, N; md_d9930(E, bt, side, &T, &N);
    double dx = Q.x - C.x, dy = Q.y - C.y;
    if (N.y * dy + N.x * dx < 0.0) return 0;
    MdSurf S;
    if (E->t == 'L') {
        md_dd1f0(E, side, &S);
        double v = S.n[1] * (X[1] - S.p[1]) + S.n[0] * (X[0] - S.p[0]) + S.n[2] * (X[2] - S.p[2]);
        if (0.0 <= v) return 1;
        v = v * 1.414213562373095;
        return -tol <= v;
    }
    if (E->t != 'A') return 1;
    md_d7b90(E, &S);
    double ey = X[1] - S.p[1], h = sqrt((X[0] - S.p[0]) * (X[0] - S.p[0]) + ey * ey);
    double zs = S.flag == 0 ? S.p[2] - h : S.p[2] + h;
    return -tol <= X[2] - zs;
}
/* FUN_1801dcf70 (l.328822, R): the node is equidistant (2 tol) from its entities, at the depth of that distance */
static int md_dcf70(const MdBs *s, const MdNode *n, double tol) {
    double dist[3]; PV P = pv(n->x, n->y);
    for (int k = 0; k < 3; k++) {
        int id = n->id[k];
        if (id < 0) { dist[k] = 0.0; continue; }
        const Gt *g = md_gt_or_zero(s, id); PV C;
        if (!md_proj_pt_gt(P, g, &C)) {
            PV S = gt_pt(g, 0.0);
            dist[k] = sqrt((S.y - P.y) * (S.y - P.y) + (S.x - P.x) * (S.x - P.x));
            continue;
        }
        dist[k] = sqrt((C.y - P.y) * (C.y - P.y) + (C.x - P.x) * (C.x - P.x));
        double t = zz_t_gt(P, g), e;
        if (0.0 <= t) { if (t <= 1.0) continue; e = t - 1.0; } else e = fabs(t);
        if (tol < gt_len(g) * e) return 0;
    }
    for (int k = 0; k < 3; k++) {
        int i, j; md_db5f0(k, &i, &j);
        if (-1 < n->id[i] && -1 < n->id[j]) {
            double lim = tol * 2.0;
            if (lim < fabs(dist[i] - dist[j]) || lim < fabs(fabs(n->z) - dist[i])) return 0;
        }
    }
    return 1;
}

/* FUN_1801dc290 (l.328335, R) "shoot": the first roof surface (of an entity not in ids) hit by the segment */
static int md_dc290(const MdBs *s, char side, double tol, const double seg[6], const int ids[3], int first,
                    double *u, double X[3], int *idh, double *th) {
    double thr = first ? 1e-12 : -1e-12; int found = 0;
    *u = 1e38;
    for (int i = 0; i < s->n; i++) for (int k = 0; k < s->b[i].n; k++) {
        const MdEnt *E = &s->b[i].e[k];
        if (E->id == ids[0] || E->id == ids[1] || E->id == ids[2]) continue;
        double XX[2][3], U[2]; int n = 0;
        if (E->g.t == 'L') { MdSurf S; md_dd1f0(&E->g, side, &S); if (md_dcb10(seg, &S, XX[0], &U[0])) n = 1; }
        else if (E->g.t == 'A') { MdSurf S; md_d7b90(&E->g, &S); n = md_dc6f0(seg, &S, XX, U); }
        for (int r = 0; r < n; r++) {
            double v = U[r];
            if (!found) { if (!(thr < v)) continue; }
            else if (!(thr < v && v < *u)) continue;
            double t = zz_t_gt(pv(XX[r][0], XX[r][1]), &E->g);
            if (!(-2e-07 <= t && t <= 1.0000002)) continue;
            if (first) {
                double dy = XX[r][1] - seg[1], dx = XX[r][0] - seg[0], dz = XX[r][2] - seg[2];
                if (!(tol <= sqrt(dy * dy + dx * dx + dz * dz))) continue;
            }
            *u = v; X[0] = XX[r][0]; X[1] = XX[r][1]; X[2] = XX[r][2]; *idh = E->id; *th = t; found = 1;
        }
    }
    if (found && -tol < X[2]) found = 0;                              /* the node radius must reach nc_tol */
    return found;
}

/* ------------------------------------------------------------------------------------------------ Newton (spec 3.6) */
static void md_ddb60(const MdSurf *p, const double X[3], double Q[3]) {   /* FUN_1801ddb60 (R): onto a plane */
    double z0 = p->p[2], dx = X[0] - p->p[0], dy = X[1] - p->p[1], dz = X[2] - z0;
    double d = dy * p->n[1] + dx * p->n[0] + dz * p->n[2];
    Q[1] = p->p[1] + (dy - d * p->n[1]); Q[0] = p->p[0] + (dx - d * p->n[0]); Q[2] = z0 + (dz - d * p->n[2]);
}
static void md_dd670(const MdSurf *c, const double X[3], double Q[3]) {   /* FUN_1801dd670 (R): onto the cone's generator */
    double gz = 0.0, ax = c->p[0], ay = c->p[1], az = c->p[2];
    double dx = X[0] - ax, dy = X[1] - ay, h = sqrt(dy * dy + dx * dx + 0.0);
    if (2e-07 <= h) {
        double gx, gy;
        if (h == 0.0) { gy = 0.0; gx = 0.0; } else { gx = dx / h; gy = dy / h; }
        double sz = c->flag == 0 ? -1.0 : 1.0, L = sqrt(gy * gy + gx * gx + sz * sz);
        if (L == 0.0) { gy = 0.0; gx = 0.0; } else { gx = gx / L; gy = gy / L; gz = sz / L; }
        double v = gy * dy + gx * dx + gz * (X[2] - az);
        Q[0] = ax + gx * v; Q[1] = ay + gy * v; Q[2] = az + gz * v;
    } else { Q[0] = ax; Q[1] = ay; Q[2] = az; }
}
static void md_ddb30(const MdSurf *s, const double X[3], double Q[3]) {   /* FUN_1801ddb30 (R) */
    if (s->t == 0) md_ddb60(s, X, Q); else if (s->t == 1) md_dd670(s, X, Q);
}
static void md_d9a80(const MdSurf *s, const double Q[3], double N[3]) {   /* FUN_1801d9a80 (R): surface normal at Q */
    if (s->t != 0) {
        double vy = Q[1] - s->p[1], vz = Q[2] - s->p[2], vx = Q[0] - s->p[0], L = sqrt(vy * vy + vx * vx + vz * vz);
        double nz = 0.0, ny, nx;
        if (L == 0.0) { ny = 0.0; nx = 0.0; } else { nx = vx / L; ny = vy / L; nz = vz / L; }
        if (s->flag == 0) nz = -nz; else { nx = -nx; ny = -ny; }
        N[0] = nx; N[2] = nz; N[1] = ny;
        return;
    }
    N[0] = s->n[0]; N[1] = s->n[1]; N[2] = s->n[2];
}
/* FUN_1801dc0a0 (R): X from N_k . X = N_k . Q_k (Cramer, |det| >= 1e-12) */
static int md_dc0a0(const double Q[3][3], const double N[3][3], double X[3]) {
    double c0[3], c1[3], c2[3], r[3];
    for (int k = 0; k < 3; k++) {
        c0[k] = N[k][0]; c1[k] = N[k][1]; c2[k] = N[k][2];
        r[k] = N[k][1] * Q[k][1] + N[k][0] * Q[k][0] + N[k][2] * Q[k][2];
    }
    double d = md_det3(c0, c1, c2);
    if (!(1e-12 <= fabs(d))) return 0;
    X[0] = md_det3(r, c1, c2) / d; X[1] = md_det3(c0, r, c2) / d; X[2] = md_det3(c0, c1, r) / d;
    return 1;
}
/* FUN_1801ddc20 (l.329406, R): Newton on the node's 3 roof surfaces (converged: the projections within nc_tol / 2) */
static int md_ddc20(const MdBs *s, char side, double tol, MdNode *n) {
    MdSurf S[3]; double h = tol * 0.5, X[3] = { n->x, n->y, n->z };
    for (int k = 0; k < 3; k++) md_dba30(md_gt_or_zero(s, n->id[k]), side, &S[k]);
    for (short it = 0;;) {
        double Q[3][3], N[3][3];
        for (int k = 0; k < 3; k++) md_ddb30(&S[k], X, Q[k]);
        double a = sqrt((Q[1][1] - Q[0][1]) * (Q[1][1] - Q[0][1]) + (Q[1][0] - Q[0][0]) * (Q[1][0] - Q[0][0]) + (Q[1][2] - Q[0][2]) * (Q[1][2] - Q[0][2]));
        if (a < h) {
            double dx = Q[2][0] - Q[1][0], dy = Q[2][1] - Q[1][1], dz = Q[2][2] - Q[1][2];
            if (sqrt(dy * dy + dx * dx + dz * dz) < h) {
                dx = Q[2][0] - Q[0][0]; dy = Q[2][1] - Q[0][1]; dz = Q[2][2] - Q[0][2];
                if (sqrt(dy * dy + dx * dx + dz * dz) < h) break;
            }
        }
        for (int k = 0; k < 3; k++) md_d9a80(&S[k], Q[k], N[k]);
        if (!md_dc0a0(Q, N, X)) return 0;
        it++;
        if (0x13 < it) return 0;
    }
    n->x = X[0]; n->y = X[1]; n->z = X[2];
    return 1;
}

/* ------------------------------------------------------------------------------------------------ build (spec 3) */
/* FUN_1801dba80 insert_zero_radius_arcs (l.327981, R): full circles halved, a zero-radius arc at every reflex vertex */
static MdBnd md_dba80(const MdBnd *in, char side, double nctol) {
    MdBnd o = { calloc(2 * (size_t)in->n + 1, sizeof(MdEnt)), 0, in->island, { 0, 0, 0, 0 } };
    int j = 0, n = in->n;
    for (int i = 0; i < n; i++) {
        const Gt *E = &in->e[i].g;
        if (E->t == 'A' && fabs(fabs(E->sw) - 6.283185307179586) < 1e-06) {    /* full circle: two halves */
            o.e[j].g = *E; o.e[j].g.sw = E->sw * 0.5;
            o.e[j + 1].g = o.e[j].g; o.e[j + 1].g.sa = o.e[j + 1].g.sw + o.e[j + 1].g.sa;
            j += 2; continue;
        }
        int ip = i - 1; if (ip < 0) ip = n - 1;
        PV T1, N1, T2, N2;
        md_d9930(&in->e[ip].g, 1.0, side, &T1, &N1);
        md_d9930(E, 0.0, side, &T2, &N2);
        double cr = T1.x * T2.y - T2.x * T1.y;
        if (side == 'R') cr = cr * -1.0;
        if (cr < 0.0) {
            double l1 = gt_len(&in->e[ip].g), l2 = gt_len(E), L = l1 <= l2 ? l2 : l1;
            double v = L * cr;
            if (nctol < fabs(v)) {
                double a1 = 0.0, a2 = 0.0;
                if (1e-12 <= N1.y * N1.y + N1.x * N1.x) { a1 = atan2(N1.y, N1.x); if (a1 < 0.0) a1 = a1 + 6.283185307179586; }
                if (1e-12 <= N2.y * N2.y + N2.x * N2.x) { a2 = atan2(N2.y, N2.x); if (a2 < 0.0) a2 = a2 + 6.283185307179586; }
                if (side == 'L') pk_ts_restrict(a2, a2 + 6.283185307179586, &a1);
                else pk_ts_restrict(a1, a1 + 6.283185307179586, &a2);
                Gt z; memset(&z, 0, sizeof z);
                z.t = 'A'; z.c = gt_pt(E, 0.0); z.r = 0; z.sa = a1; z.sw = a2 - a1;
                o.e[j].g = z; j++;
            }
        }
        o.e[j] = in->e[i]; j++;
    }
    o.n = j;
    return o;
}

/* FUN_1801d8ed0 (l.325987, R): a node next to the end of its third entity also touches the neighbour entity */
static int md_d8ed0(const MdBs *s, const MdNode *n, int e1, int e2, MdNode *o) {
    int e3 = -1;
    for (int k = 0; k < 3; k++) if (-1 < n->id[k] && n->id[k] != e1 && n->id[k] != e2) e3 = n->id[k];
    if (e3 < 0) return 0;
    int idx; const MdBnd *b = md_db560(s, e3, &idx);
    if (!b) return 0;
    const Gt *E = &b->e[idx].g;
    double t = zz_t_gt(pv(n->x, n->y), E), d = t, nct = g_nc_tol;
    if (0.5 <= t) d = 1.0 - t;
    if (!(gt_len(E) * fabs(d) <= nct)) return 0;
    if (E->t == 'A') {
        double dx = E->c.x - n->x, dy = E->c.y - n->y;
        if (nct < sqrt(dx * dx + dy * dy) * fabs(d)) return 0;
    }
    int i2;
    if (0.5 <= t) { i2 = idx + 1; if (b->n - 1 < i2) i2 = 0; }
    else { i2 = idx - 1; if (i2 < 0) i2 = b->n - 1; }
    int e4 = b->e[i2].id;
    if (e4 == e1 || e4 == e2) return 0;
    *o = *n;
    o->id[0] = e1; o->id[1] = e2; o->id[2] = e4;
    for (int k = 0; k < 3; k++) { o->link[k] = NULL; o->done[k] = 0; }
    o->next = NULL;
    return 1;
}

/* FUN_1801da640 (l.327105-327393, R, D:1801da8f0-1801dabf0): trace the bisector of (g1, g2) from node nd to the next node */
static int md_da640(const MdBs *s, char side, double tol, double medTol, MdNode *nd, double t0, const Gt *g1, const Gt *g2,
                    int id1, int id2, int fromV, MdNode *nw) {
    MdSurf S2; double ray[6], u, X[3], tt; int id3, ok = 0;
    md_dba30(g2, side, &S2);
    memset(nw, 0, sizeof *nw);
    if (fromV) {
        if (!md_zero_arc(g1) && !md_zero_arc(g2)) {
            PV T1 = gt_tan(g1, 1.0), T2 = gt_tan(g2, 0.0);
            double cr = T1.x * T2.y - T2.x * T1.y;
            if (2e-07 <= cr) {
                const Gt *gm = gt_len(g1) <= gt_len(g2) ? g2 : g1;
                if (tol <= fabs(cr) * gt_len(gm)) goto bisect;
            }
        }
        md_d7ef0(g1, 1.0, side, ray);                                /* G1 joint / reflex / zero-radius: the normal ray */
        goto shoot;
    }
bisect:
    if (g1->t == 'L' && g2->t == 'L') { double P[3] = { nd->x, nd->y, nd->z }; md_d7d50(P, g1, g2, side, ray); goto shoot; }
    {   /* march along g1: rays hit on the roof of g2, the polyline through the hits shot at the other roofs */
        int passes = fromV ? 1 : 2;
        for (short p = 0; p < passes; p++) {
            double dt = (p == 0 ? 0.0 : 1.0) - t0;
            if (!(2e-07 <= fabs(dt))) continue;
            double x = fabs(dt) / medTol, r = 0.0 <= x ? floor(x + 0.5) : ceil(x - 0.5);
            int n = (short)(int)r < 10 ? 10 : (short)(int)r;
            double prev[3] = { nd->x, nd->y, nd->z }, best = 1e38, t = t0;
            for (short k = 1; k < n; k++) {
                t = dt / (double)(n - 1) + t;
                double Q[3];
                md_d7ef0(g1, t, side, ray);
                if (!md_dca20(ray, &S2, Q)) continue;
                double seg[6] = { prev[0], prev[1], prev[2], Q[0], Q[1], Q[2] };
                prev[0] = Q[0]; prev[1] = Q[1]; prev[2] = Q[2];
                if (!md_dc290(s, side, tol, seg, nd->id, k == 1, &u, X, &id3, &tt)) continue;
                double err = 1.0 < u ? u - 1.0 : (u < 0.0 ? fabs(u) : 0.0);
                if (err < best) {
                    nw->id[0] = id1; nw->id[1] = id2; nw->id[2] = id3; nw->x = X[0]; nw->y = X[1]; nw->z = X[2];
                    nw->link[0] = fromV ? nd : NULL; nw->link[1] = nw->link[2] = NULL; nw->done[0] = nw->done[1] = nw->done[2] = 0;
                    ok = 1; best = err;
                } else if (!ok) continue;
                if (err < 1e-12) break;
            }
        }
        if (ok && md_ddc20(s, side, tol, nw)) {
            double P[3] = { nw->x, nw->y, nw->z };
            if (md_db6a0(s, side, P, tol) && md_dcf70(s, nw, tol)) return 1;
        }
        if (!fromV) return 0;
        double r1[6], r2[6];                                          /* the mean of the two normal rays */
        md_d7ef0(g1, 1.0, side, r1); md_d7ef0(g2, 0.0, side, r2);
        ray[0] = (r2[0] + r1[0]) * 0.5; ray[1] = (r2[1] + r1[1]) * 0.5; ray[2] = (r2[2] + r1[2]) * 0.5;
        ray[3] = (r2[3] + r1[3]) * 0.5; ray[4] = (r2[4] + r1[4]) * 0.5; ray[5] = (r2[5] + r1[5]) * 0.5;
        if (!md_dc290(s, side, tol, ray, nd->id, 1, &u, X, &id3, &tt)) return 0;
        memset(nw, 0, sizeof *nw);
        nw->id[0] = id1; nw->id[1] = id2; nw->id[2] = id3; nw->link[0] = nd;
        nw->x = X[0]; nw->y = X[1]; nw->z = X[2];
        double P[3] = { X[0], X[1], X[2] };
        if (!md_db6a0(s, side, P, tol)) return 0;
        return md_dcf70(s, nw, tol);
    }
shoot:
    if (!md_dc290(s, side, tol, ray, nd->id, 1, &u, X, &id3, &tt)) return 0;
    memset(nw, 0, sizeof *nw);
    nw->id[0] = id1; nw->id[1] = id2; nw->id[2] = id3; nw->link[0] = fromV ? nd : NULL;
    nw->x = X[0]; nw->y = X[1]; nw->z = X[2];
    { double P[3] = { X[0], X[1], X[2] }; if (!md_db6a0(s, side, P, tol)) return 0; }
    return md_dcf70(s, nw, tol);
}

/* FUN_1801d9360 (l.326186, R): a node at every boundary vertex (z = 0, ids (e_i, e_i+1, -1)) */
static void md_d9360(const MdBs *s, double tol, MdNode **nodes) {
    for (int i = 0; i < s->n; i++) {
        const MdBnd *b = &s->b[i];
        for (int k = 0; k < b->n; k++) {
            MdNode n; memset(&n, 0, sizeof n);
            n.id[0] = b->e[k].id; n.id[1] = b->e[k + 1 < b->n ? k + 1 : 0].id; n.id[2] = -1;
            PV P = gt_pt(&b->e[k].g, 1.0); n.x = P.x; n.y = P.y; n.z = 0;
            md_d7ad0(nodes, &n, tol);
        }
    }
}
/* FUN_1801d9110 (l.326093, R): from every boundary vertex node along its bisector to the first interior node */
static void md_d9110(const MdBs *s, char side, double tol, double medTol, MdNode **nodes) {
    for (MdNode *nd = *nodes; nd; nd = nd->next) {
        if (!md_dd640(nd)) continue;
        int e1, e2, slot;
        if (nd->id[0] < 0) { e1 = nd->id[1]; e2 = nd->id[2]; slot = 2; }
        else if (nd->id[1] < 0) { e1 = nd->id[0]; e2 = nd->id[2]; slot = 1; }
        else if (nd->id[2] < 0) { e1 = nd->id[0]; e2 = nd->id[1]; slot = 0; }
        else continue;
        const Gt *g1 = md_gt_or_zero(s, e1), *g2 = md_gt_or_zero(s, e2);
        MdNode nw;
        if (!md_da640(s, side, tol, medTol, nd, 1.0, g1, g2, e1, e2, 1, &nw)) continue;
        double P[3] = { nw.x, nw.y, nw.z };
        if (!md_db6a0(s, side, P, tol) || !md_dcf70(s, &nw, tol) || md_d7ad0(nodes, &nw, tol) != 0) continue;
        nd->link[slot] = *nodes;
        MdNode ex;
        if (md_d8ed0(s, &nw, e1, e2, &ex)) md_d7ad0(nodes, &ex, tol);
    }
}
/* FUN_1801d7830 (l.325130, R): trace every unknown, not done pair of every node; returns the number of new nodes */
static int md_d7830(MdNode **nodes, const MdBs *s, char side, double tol, double medTol) {
    MdNode *tmp = NULL; int added = 0;
    for (MdNode *nd = *nodes; nd; nd = nd->next)
        for (int k = 0; k < 3; k++) {
            int ia, ib;
            if (nd->link[k] || nd->done[k]) continue;
            md_db5b0(nd, k, &ia, &ib);
            if (ia < 0 || ib < 0) continue;
            const Gt *ga = md_gt_or_zero(s, ia), *gb = md_gt_or_zero(s, ib);
            double t0 = zz_t_gt(pv(nd->x, nd->y), ga);
            MdNode nw;
            if (md_da640(s, side, tol, medTol, nd, t0, ga, gb, ia, ib, 0, &nw) && md_dce70(*nodes, &nw, tol) && md_d7ad0(&tmp, &nw, tol) == 0) added++;
        }
    md_d7b60(nodes, tmp);
    return added;
}
/* FUN_1801d9a00 (R): 3D end (x, y, -r) */
static void md_d9a00(const MdEdge *e, int end, double o[3]) {
    if (e->type == 0) { if (end == 0) { o[0] = e->x0; o[1] = e->y0; o[2] = -e->r0; } else { o[0] = e->x1; o[1] = e->y1; o[2] = -e->r1; } return; }
    if (e->type != 1) return;
    const MdPt *p = &e->pts[end == 0 ? 0 : e->n - 1];
    o[0] = p->x; o[1] = p->y; o[2] = -p->r;
}
/* FUN_1801dccd0 (R): 0 when an edge of the same pair already joins both nodes (each end within 2 tol of A or B) */
static int md_dccd0(const MdEdge *e, const MdNode *A, const MdNode *B, int ia, int ib, double tol) {
    for (; e; e = e->next) {
        if (!((e->ida == ia && e->idb == ib) || (e->ida == ib && e->idb == ia))) continue;
        int c = 0;
        for (int k = 0; k < 2; k++) {
            double q[3] = { 0, 0, 0 }; md_d9a00(e, k, q);
            double dy = q[1] - A->y, dx = q[0] - A->x, dz = q[2] - A->z;
            if (sqrt(dy * dy + dx * dx + dz * dz) < tol * 2.0) { c++; continue; }
            dy = q[1] - B->y; dx = q[0] - B->x; dz = q[2] - B->z;
            if (sqrt(dy * dy + dx * dx + dz * dz) < tol * 2.0) c++;
        }
        if (c == 2) return 0;
    }
    return 1;
}
static double md_d9540(const Gt *g, double t) {                   /* FUN_1801d9540 (R) */
    if (t < -1e-12) return gt_len(g) * fabs(t);
    if (1.000000000001 < t) return gt_len(g) * (t - 1.0);
    return 0.0;
}
/* FUN_1801d7fc0 (l.325457-325816, R): the edge between nodes A and B on the bisector of (ida, idb) */
static int md_d7fc0(const MdNode *A, const MdNode *B, const MdBs *s, char side, double tol, double medTol, int ida, int idb, MdEdge *o) {
    const Gt *g1 = md_gt_or_zero(s, ida), *g2 = md_gt_or_zero(s, idb);
    PV A2 = pv(A->x, A->y), B2 = pv(B->x, B->y);
    int straight = g1->t == 'L' && g2->t == 'L';
    if (!straight && !(md_zero_arc(g1) && md_zero_arc(g2))) {
        PV M1 = pv(A2.x * 0.9 + B2.x * 0.1, A2.y * 0.9 + B2.y * 0.1), M2 = pv(A2.x * 0.1 + B2.x * 0.9, A2.y * 0.1 + B2.y * 0.9);
        double a1 = zz_t_gt(M1, g1), a2 = zz_t_gt(M2, g1), b1 = zz_t_gt(M1, g2), b2 = zz_t_gt(M2, g2);
        straight = gt_len(g1) * fabs(a1 - a2) < g_nc_tol && gt_len(g2) * fabs(b1 - b2) < g_nc_tol;
    }
    memset(o, 0, sizeof *o);
    if (straight) {
        double M[3] = { (A->x + B->x) * 0.5, (B->y + A->y) * 0.5, (B->z + A->z) * 0.5 };
        if (!md_db6a0(s, side, M, tol)) return 0;
        double seg[6] = { A->x, A->y, A->z, B->x, B->y, B->z }, u, X[3], th; int ids[3] = { ida, idb, -1 }, idh;
        int f = md_dc290(s, side, tol, seg, ids, 1, &u, X, &idh, &th);
        if (f && 0.0 < u && u < 1.0) {
            double d = 0.5 <= u ? 1.0 - u : u;
            double dy = B->y - A->y, dx = B->x - A->x, dz = B->z - A->z;
            if (tol * 1.414213562373095 < sqrt(dy * dy + dx * dx + dz * dz) * d) return 0;
        }
        o->type = 0; o->x0 = A->x; o->y0 = A->y; o->x1 = B->x; o->y1 = B->y; o->r0 = fabs(A->z); o->r1 = fabs(B->z);
        return 1;
    }
    double tA1 = zz_t_gt(A2, g1), tB1 = zz_t_gt(B2, g1), lo1 = tB1 <= tA1 ? tB1 : tA1, hi1 = tB1 <= tA1 ? tA1 : tB1;
    double tA2 = zz_t_gt(A2, g2), tB2 = zz_t_gt(B2, g2), lo2, hi2;
    if (tA2 < tB2) { hi2 = tB2; lo2 = tA2; } else { hi2 = tA2; lo2 = tB2; }
    double e = md_d9540(g1, tA1), v = md_d9540(g1, tB1);
    if (e <= v) e = v;
    v = md_d9540(g2, tA2); if (e <= v) e = v;
    v = md_d9540(g2, tB2); if (e <= v) e = v;
    if (!(e <= tol)) return 0;
    o->type = 1;
    MdSurf S2; md_dba30(g2, side, &S2);
    double ray[6], mid[3];
    md_d7ef0(g1, (tB1 + tA1) * 0.5, side, ray);
    if (!md_dca20(ray, &S2, mid)) return 0;
    double ay = mid[1] - A->y, ax = mid[0] - A->x, az = mid[2] - A->z, by = mid[1] - B->y, bx = mid[0] - B->x, bz = mid[2] - B->z;
    double da = sqrt(ay * ay + ax * ax + az * az), db = sqrt(by * by + bx * bx + bz * bz), x = (da + db) / medTol;
    double r = 0.0 <= x ? floor(x + 0.5) : ceil(x - 0.5);
    int n = 4;
    if (3 < (short)(int)r) n = (short)(int)r;
    int np = n + 1;
    o->n = 0; o->cap = np;
    o->pts = calloc((size_t)np, sizeof(MdPt));
    o->n = np;
    o->pts[0].x = A->x; o->pts[0].y = A->y; o->pts[0].r = fabs(A->z);
    int half = np / 2; if (half < 1) half = 1;
    double dt = tB1 - tA1, t = tA1;
    for (int k = 1; k < n; k++) {
        double P[3];
        t = t + dt / (double)n;
        md_d7ef0(g1, t, side, ray);
        int hit = md_dca20(ray, &S2, P), flag = k == half && hit;
        if (!hit) goto fail;
        double s1 = zz_t_gt(pv(P[0], P[1]), g1), s2 = zz_t_gt(pv(P[0], P[1]), g2);
        if (s1 < lo1) { flag = 1; if (tol < gt_len(g1) * fabs(s1 - lo1)) goto fail; }
        if (hi1 < s1) { flag = 1; if (tol < gt_len(g1) * fabs(s1 - hi1)) goto fail; }
        if (s2 < lo2) { flag = 1; if (tol < gt_len(g2) * fabs(s2 - lo2)) goto fail; }
        int ok = hit;
        if (hi2 < s2) { if (tol < gt_len(g2) * fabs(s2 - hi2)) goto fail; ok = md_db6a0(s, side, P, tol); }
        else if (flag) ok = md_db6a0(s, side, P, tol);
        if (!ok) goto fail;
        o->pts[k].x = P[0]; o->pts[k].y = P[1]; o->pts[k].r = fabs(P[2]);
    }
    o->pts[n].x = B->x; o->pts[n].y = B->y; o->pts[n].r = fabs(B->z);
    return 1;
fail:
    free(o->pts); o->pts = NULL;
    return 0;
}
static void md_push_edge(MdEdge **l, const MdEdge *e, int ia, int ib, int excl) {
    MdEdge *n = calloc(1, sizeof *n);
    *n = *e; n->ida = ia; n->idb = ib; n->state = 0; n->excl = (char)excl;
    n->next = *l; *l = n;
}
/* FUN_1801d8aa0 create_medial_axis_from_node_points (l.325817, R) */
static void md_d8aa0(MdNode *nodes, const MdBs *s, char side, double tol, double medTol, MdEdge **edges) {
    for (MdNode *A = nodes; A; A = A->next)
        for (int k = 0; k < 3; k++) {
            int ia, ib; md_db5b0(A, k, &ia, &ib);
            if (ia < 0 || ib < 0 || A->done[k]) continue;
            MdNode *C = A->link[k]; MdEdge e;
            if (!C) {
                for (MdNode *B = nodes; B; B = B->next) {
                    if (B == A || (md_dd640(A) && md_dd640(B))) continue;
                    double dx = B->x - A->x, dy = B->y - A->y, dz = B->z - A->z;
                    if (!(tol <= sqrt(dx * dx + dy * dy + dz * dz))) continue;
                    for (int j = 0; j < 3; j++) {
                        int ja, jb;
                        if (B->link[j]) continue;
                        md_db5b0(B, j, &ja, &jb);
                        if (!((ia == ja && ib == jb) || (ia == jb && ib == ja))) continue;
                        if (md_dccd0(*edges, A, B, ia, ib, tol) && md_d7fc0(A, B, s, side, tol, medTol, ia, ib, &e)) {
                            md_push_edge(edges, &e, ia, ib, md_dd640(A) || md_dd640(B));
                            A->done[k] = 1; B->done[j] = 1;
                        }
                    }
                }
            } else if (md_dccd0(*edges, A, C, ia, ib, tol)) {
                int j = C->link[0] == A ? 0 : (C->link[1] != A ? 2 : 1);
                if (md_d7fc0(A, C, s, side, tol, medTol, ia, ib, &e)) {
                    md_push_edge(edges, &e, ia, ib, md_dd640(A) || md_dd640(C));
                    A->done[k] = 1; C->done[j] = 1;
                }
            }
        }
}
/* FUN_1801d9b80 (l.326611, R, D:1801d9b80-1801d9d0c): simplify a polyline to tol (window <= 100 points) */
static void md_d9b80(MdEdge *e, double tol) {
    int a = 0, n = e->n;
    for (int j = 2; j < n; j++) {
        int bad = 0;
        PV pa = pv(e->pts[a].x, e->pts[a].y), pj = pv(e->pts[j].x, e->pts[j].y);
        for (int k = a + 1; k < j; k++) if (tol < md_dist_pt_l(pv(e->pts[k].x, e->pts[k].y), pa, pj)) { bad = 1; break; }
        if (bad || j == n - 1 || a + 100 <= j) {
            for (int k = a + 1; k < j - 2; k++) e->pts[k].r = -1.0;
            a = j - 1;
        }
    }
    int kept = 0;
    for (int k = 0; k < n; k++) if (0.0 <= e->pts[k].r) kept++;
    if (kept == n) return;
    MdPt *np = calloc((size_t)kept + 1, sizeof(MdPt)); int m = 0;
    for (int k = 0; k < n; k++) if (0.0 <= e->pts[k].r) np[m++] = e->pts[k];
    free(e->pts); e->pts = np; e->n = m; e->cap = kept;
}

/* find_medial_axis FUN_1801da3c0 (l.327010-327104, R) + FUN_1801d9f20 simplify (FUN_1801d9f60 thunk, spec 1.2):
   the medial axis of one pocket's bounds (machine_pocket l.322547-322555: the pocket = the tool-centre bounds) */
static MdEdge *md_build(const Gls *pocket, double nctol, double medTol) {
    const char side = 'L';
    MdBs pv_ = { calloc((size_t)pocket->n + 1, sizeof(MdBnd)), 0 };
    for (int i = 0; i < pocket->n; i++) {
        const Gl *l = &pocket->l[i];
        if (l->n < 2 && l->n && l->e[0].t == 'P') continue;          /* pock_bound_is_point */
        if (!l->n) continue;
        Gl c = gl_copy(l);
        int ccw = gl_area(l) > 0;                                     /* pock_ccw: ours (signed area) */
        if (ccw != (l->island == 0)) gl_reverse(&c);                  /* reverse_pock_bound(c, 0): outer CCW, islands CW */
        MdBnd in = { calloc((size_t)c.n, sizeof(MdEnt)), c.n, l->island, { 0, 0, 0, 0 } };
        for (int k = 0; k < c.n; k++) {
            in.e[k].g = c.e[k];
            if (in.e[k].g.t == 'A') {                                 /* ours: start angle in [0, 2 pi) for t_on_arc */
                double a = fmod(in.e[k].g.sa, 2 * M_PI); if (a < 0) a += 2 * M_PI; in.e[k].g.sa = a;
            }
        }
        pv_.b[pv_.n++] = md_dba80(&in, side, nctol);
        free(in.e); gl_free(&c);
    }
    int k = 0;                                                        /* global renumber (l.327068-327079) */
    for (int i = 0; i < pv_.n; i++) for (int j = 0; j < pv_.b[i].n; j++) pv_.b[i].e[j].id = k++;
    MdNode *nodes = NULL; MdEdge *out = NULL;
    md_d9360(&pv_, nctol, &nodes);
    md_d9110(&pv_, side, nctol, medTol, &nodes);
    for (int it = 0; it < 4; it++) {                                  /* D:1801da5a0-1801da5f0 */
        md_d8aa0(nodes, &pv_, side, nctol, medTol, &out);
        if (md_d7830(&nodes, &pv_, side, nctol, medTol) == 0) break;
    }
    md_d8aa0(nodes, &pv_, side, nctol, medTol, &out);
    md_free_nodes(&nodes); md_free_bs(&pv_);
    for (MdEdge *e = out; e; e = e->next) if (e->type == 1) md_d9b80(e, nctol);
    if (getenv("PK_DEBUG")) {
        int ne = 0, nx = 0; for (MdEdge *e = out; e; e = e->next) { ne++; nx += e->excl; }
        fprintf(stderr, "medax: %d edges (%d excluded), medTol %g\n", ne, nx, medTol);
        if (getenv("PK_DEBUG")[0] == '2') for (MdEdge *e = out; e; e = e->next) {
            double a[3] = { 0, 0, 0 }, b[3] = { 0, 0, 0 }; md_d9a00(e, 0, a); md_d9a00(e, 1, b);
            fprintf(stderr, "  edge %d-%d t%d n%d x%d: %.4f %.4f r%.4f -> %.4f %.4f r%.4f\n", e->ida, e->idb, e->type, e->type ? e->n : 2, e->excl, a[0], a[1], -a[2], b[0], b[1], -b[2]);
        }
    }
    return out;
}

/* ------------------------------------------------------------------------------------------------ use (spec 4) */
typedef struct { MB *b; const CamParams *p; const Gls *rb; double depth, zf, nctol; } MdEmit;
static int pk_e0f80(MB *b, const CamParams *p, const Gls *rb, PV a, PV c, double depth, double zf, double nctol);
/* FUN_1801e32a0(1, 0, pt, init_feedrate, pock_depth) = a feed move at the cut depth (zero-length: ours, skipped) */
static void md_move(const MdEmit *m, PV q) {
    V3 e = lastp(m->b);
    if (zz_equal(pv(e.x, e.y), q)) return;
    mv(m->b, 0, q.x, q.y, m->depth, m->p->feed);
}
static int md_e0f80(const MdEmit *m, PV a, PV c) { return pk_e0f80(m->b, m->p, m->rb, a, c, m->depth, m->zf, m->nctol); }

static PV md_end(const MdEdge *e, int end) {                       /* FUN_1801d99a0 (R): 2D end */
    if (e->type != 0) return end == 0 ? pv(e->pts[0].x, e->pts[0].y) : pv(e->pts[e->n - 1].x, e->pts[e->n - 1].y);
    return end == 0 ? pv(e->x0, e->y0) : pv(e->x1, e->y1);
}
/* FUN_1801d6b10 (R): deep copy in list order (polylines without points skipped) */
static MdEdge *md_d6b10(const MdEdge *src) {
    MdEdge *head = NULL, *last = NULL;
    for (; src; src = src->next) {
        if (src->type == 1 && (!src->pts || src->n <= 0)) continue;
        MdEdge *n = calloc(1, sizeof *n);
        *n = *src; n->next = NULL;
        if (n->type == 1) { n->pts = NULL; n->cap = src->n; n->pts = calloc((size_t)src->n, sizeof(MdPt)); memcpy(n->pts, src->pts, (size_t)src->n * sizeof(MdPt)); }
        if (!last) head = n; else last->next = n;
        last = n;
    }
    return head;
}
static void md_d6c70(MdEdge *e) {                                   /* FUN_1801d6c70 (R): straight edges as 2-point polylines */
    for (; e; e = e->next) {
        if (e->type != 0) continue;
        e->type = 1; e->n = 2; e->cap = 2;
        e->pts = calloc(2, sizeof(MdPt));
        e->pts[0].x = e->x0; e->pts[0].y = e->y0; e->pts[1].x = e->x1; e->pts[1].y = e->y1;
        e->pts[0].r = e->r0; e->pts[1].r = e->r1;
    }
}
/* FUN_1801d7420 SplMdlAtIsects (l.324951, R): split the edge at the crossing points q of its segment i */
static void md_d7420(MdEdge *e, int i, PV *q, int cnt) {
    double tk[2] = { 0.0, 0.0 };
    PV a = pv(e->pts[i - 1].x, e->pts[i - 1].y), b = pv(e->pts[i].x, e->pts[i].y);
    double ra = e->pts[i - 1].r, rb = e->pts[i].r;
    for (int k = 0; k < cnt; k++) tk[k] = zz_t_line(q[k], a, b);
    if (1 < cnt && tk[0] < tk[1]) { PV t = q[0]; q[0] = q[1]; q[1] = t; double s = tk[0]; tk[0] = tk[1]; tk[1] = s; }   /* far point first */
    e->excl = 1;
    for (int k = 0; k < cnt; k++) {
        double d0 = (q[k].y - a.y) * (q[k].y - a.y) + (q[k].x - a.x) * (q[k].x - a.x);
        double dx = q[k].x - b.x, dy = q[k].y - b.y, d1 = dy * dy + dx * dx;
        int near0 = d0 < 1e-08, near1 = d1 < 1e-08;
        if (!((1e-08 <= d0 || i != 1) && (1e-08 <= d1 || i != e->n - 1))) continue;
        MdEdge *N = calloc(1, sizeof *N);
        *N = *e; N->pts = NULL; N->next = e->next; e->next = N;
        N->n = e->n - i; if (!near1) N->n = N->n + 1;
        N->cap = N->n;
        N->pts = calloc((size_t)N->n + 1, sizeof(MdPt));
        if (near1) memcpy(N->pts, &e->pts[i], (size_t)N->n * sizeof(MdPt));
        else if (near0) memcpy(N->pts, &e->pts[i - 1], (size_t)N->n * sizeof(MdPt));
        else {
            N->pts[0].x = q[k].x; N->pts[0].y = q[k].y; N->pts[0].r = (rb - ra) * tk[k] + ra;
            memcpy(&N->pts[1], &e->pts[i], (size_t)(N->n - 1) * sizeof(MdPt));
        }
        e->n = i; if (!near0) e->n = i + 1;
        if (!near1 && !near0) { e->pts[e->n - 1].x = q[k].x; e->pts[e->n - 1].y = q[k].y; e->pts[e->n - 1].r = N->pts[0].r; }
    }
}
/* FUN_1801d6f40 FndIsectsAndSplMdl (l.324718, R): split the edges where they cross a bound entity */
static void md_d6f40(MdEdge *e, const MdBs *mb) {
    for (; e; e = e->next) {
        if (e->type != 1) continue;                                   /* _MCAssert 0x1c2 */
        for (int i = 1; i < e->n; i++) {
            double sb[4];
            #define MD_SEGBB() do { MdPt *p0 = &e->pts[i - 1], *p1 = &e->pts[i]; \
                sb[0] = p1->x <= p0->x ? p1->x : p0->x; sb[1] = p1->y <= p0->y ? p1->y : p0->y; \
                sb[2] = p0->x <= p1->x ? p1->x : p0->x; sb[3] = p0->y <= p1->y ? p1->y : p0->y; } while (0)
            MD_SEGBB();
            for (int bi = 0; bi < mb->n; bi++) {
                const MdBnd *b = &mb->b[bi];
                if (!md_rect(sb, b->bb, 1e-06)) continue;
                for (int k = 0; k < b->n; k++) {
                    if (!md_rect(sb, b->e[k].bb, 1e-06)) continue;
                    PV q[2];
                    int cnt = md_ints_seg(pv(e->pts[i - 1].x, e->pts[i - 1].y), pv(e->pts[i].x, e->pts[i].y), &b->e[k].g, 1e-06, q);
                    if (0 < cnt) {
                        md_d7420(e, i, q, cnt);
                        if (e->n <= i) i = e->n - 1;
                        MD_SEGBB();
                    }
                }
            }
            #undef MD_SEGBB
        }
    }
}
/* FUN_1801d6d50 (l.324630, R): delete the edges whose first sample off the bounds is outside the machinable area */
static void md_d6d50(MdEdge **l, const Gls *bounds, double nctol) {
    static const double f3[3] = { 0.25, 0.5, 0.75 };
    /* The decompile, on a deletion, sets the edge pointer back to its predecessor (or NULL), sets i = that edge's count to end
       the sample loop, and then steps to predecessor->next (or *l): i.e. to the deleted edge's successor, with the predecessor
       unchanged. Written here with an explicit `del` flag and the successor read before the free, the same walk without
       reading through a pointer gcc cannot tell from the freed one (it warned -Wuse-after-free on the old `prev = e` form). */
    MdEdge *prev = NULL, *e = *l;
    while (e) {
        int del = 0;
        for (int i = 1; !del && i < e->n; i++) {
            for (int k = 0; k < 3; k++) {
                double f = f3[k]; char on = 0;
                PV S = pv((1.0 - f) * e->pts[i - 1].x + f * e->pts[i].x, (1.0 - f) * e->pts[i - 1].y + f * e->pts[i].y);
                if (!md_in_area(bounds, S, &on, nctol, CAM_SYS_TOL)) { del = 1; break; }
                if (!on) { i = e->n; break; }
            }
        }
        MdEdge *next = e->next;
        if (del) {
            if (!prev) *l = next; else prev->next = next;
            free(e->pts); free(e);
        } else prev = e;
        e = next;
    }
}
/* FUN_1801d95b0 (R): distance of q to an edge */
static double md_d95b0(const MdEdge *e, PV q) {
    if (e->type == 0) return md_dist_finite(q, pv(e->x0, e->y0), pv(e->x1, e->y1), 1e38);
    if (e->type != 1) return 0.0;
    double best = 1e38; int in = 0;
    for (int i = 0; i < e->n - 1; i++) {
        PV a = pv(e->pts[i].x, e->pts[i].y), b = pv(e->pts[i + 1].x, e->pts[i + 1].y), o = pv(0, 0); double t = 0;
        md_proj_pt_ln(q, a, b, &o, &t);
        if (t < 0.0 || 1.0 < t) {
            if (!in) { double d = md_dist_finite(q, a, b, 1e38); if (d < best) best = d; }
        } else {
            double dy = q.y - o.y, d = sqrt((q.x - o.x) * (q.x - o.x) + dy * dy);
            if (d < best) best = d;
            in = 1;
        }
    }
    return best;
}
static MdEdge *md_da350(MdEdge *e, PV q) {                          /* FUN_1801da350 (R): nearest edge, strict <, first wins */
    double best = 1e38; MdEdge *r = NULL;
    for (; e; e = e->next) { double d = md_d95b0(e, q); if (d < best) { r = e; best = d; } }
    return r;
}
/* END-POINT TABLE FUN_1801d7be0 (R, spec 2.8) */
typedef struct { double key; short end; MdEdge *e; int idx; } MdTab;
static int md_cmp(const void *pa, const void *pb) {                /* LAB_1801d7d10 */
    double d = ((const MdTab *)pa)->key - ((const MdTab *)pb)->key;
    if (fabs(d) < 1e-12) return 0;
    return d < 0.0 ? -1 : 1;
}
static MdTab *md_d7be0(MdEdge *e, int *nt) {
    int n = 0; for (MdEdge *x = e; x; x = x->next) n++;
    *nt = 2 * n;
    if (!n) return NULL;
    MdTab *t = calloc((size_t)*nt, sizeof *t); int k = 0;
    for (; e; e = e->next) {
        PV p0 = md_end(e, 0), p1 = md_end(e, 1);
        t[k].e = e; t[k].key = p0.y + p0.x; t[k].end = 0;               /* FUN_1801dd310: d99a0 + dba70 (y + x) */
        t[k + 1].e = e; t[k + 1].key = p1.y + p1.x; t[k + 1].end = 1;
        k += 2;
    }
    md_qsort(t, (size_t)*nt, sizeof *t, md_cmp);
    for (int i = 0; i < *nt; i++) t[i].idx = i;
    return t;
}
/* FUN_1801d9f90 (l.326854, R): the next edge (state st, not excluded) with an end within tol of the edge's end */
static MdEdge *md_d9f90(MdTab *T, int nt, double tol, const MdEdge *edge, int end, int st, short *ne) {
    PV p = md_end(edge, end);
    MdTab key; memset(&key, 0, sizeof key); key.key = p.y + p.x;
    MdTab *h = md_bsearch(&key, T, (size_t)nt, sizeof *T, md_cmp);
    if (!h) return NULL;
    int hi = h->idx, lo = hi;
    for (int i = h->idx - 1; i > -1; i--) if (fabs(T[i].key - h->key) < tol * 3.0) lo = i;
    for (int i = h->idx + 1; i < nt; i++) if (fabs(T[i].key - h->key) < tol * 3.0) hi = i;
    for (int i = lo; i <= hi; i++) {
        MdEdge *e = T[i].e;
        if (e == edge || e->excl != 0 || e->state != st) continue;
        PV q = md_end(e, T[i].end);
        if (sqrt((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y)) < tol) { *ne = T[i].end; return e; }
    }
    return NULL;
}
/* FUN_1801dcc10 (R): an end of e2 within tol of end end1 of e1 */
static int md_dcc10(const MdEdge *e1, const MdEdge *e2, double tol, int end1, short *end2) {
    PV p = md_end(e1, end1);
    for (short k = 0; k < 2; k++) {
        PV q = md_end(e2, k);
        if (sqrt((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y)) < tol) { *end2 = k; return 1; }
    }
    return 0;
}
/* FUN_1801dd870 (l.329207, R): projection of q on the edge (point, parameter, segment) */
static PV md_dd870(const MdEdge *e, PV q, double *tp, int *seg) {
    PV pr = pv(0, 0); *seg = -1;
    if (e->type == 0) {
        double t = 0; PV a = pv(e->x0, e->y0), b = pv(e->x1, e->y1);
        int ok = md_proj_pt_ln(q, a, b, &pr, &t);
        if (!ok || t < 0.0) { *tp = 0; return a; }
        if (t <= 1.0) { *tp = t; return pr; }
        *tp = 1.0; return b;
    }
    if (e->type != 1) return pr;
    double best = 1e38; int in = 0;
    for (int i = 0; i < e->n - 1; i++) {
        PV a = pv(e->pts[i].x, e->pts[i].y), b = pv(e->pts[i + 1].x, e->pts[i + 1].y), o = pv(0, 0); double t = 0;
        md_proj_pt_ln(q, a, b, &o, &t);
        if (t < 0.0 || 1.0 < t) {
            if (in) continue;
            PV c; double ct;
            if (0.0 <= t) { c = b; ct = i + 1; } else { c = a; ct = i; }
            double d = sqrt((q.y - c.y) * (q.y - c.y) + (q.x - c.x) * (q.x - c.x));
            if (d < best) { pr = c; *tp = ct; *seg = i; best = d; }
        } else {
            double d = sqrt((q.x - o.x) * (q.x - o.x) + (q.y - o.y) * (q.y - o.y));
            if (d < best) { in = 1; *tp = (double)i + t; pr = o; *seg = i; best = d; }
        }
    }
    return pr;
}
/* FUN_1801dd4c0 (l.329045, R): feed along the edge from pos to its end (dir 0: towards the last point) */
static void md_dd4c0(const MdEmit *m, const MdEdge *e, int dir, PV *pos) {
    if (e->type == 0) { PV t = dir == 0 ? pv(e->x1, e->y1) : pv(e->x0, e->y0); md_move(m, t); *pos = t; return; }
    if (e->type != 1) return;
    double t; int s; md_dd870(e, *pos, &t, &s);
    int step, last;
    if (dir == 0) { s = s + 1; step = 1; last = e->n - 1; } else { step = -1; last = 0; }
    for (int v = s * step, k = s; v <= last * step; v += step * step, k += step) md_move(m, pv(e->pts[k].x, e->pts[k].y));
    *pos = pv(e->pts[last].x, e->pts[last].y);
}
/* FUN_1801dd330 (l.328990, R): feed along the edge from pos (at its end dir) to the target point */
static void md_dd330(const MdEmit *m, const MdEdge *e, int dir, PV *pos, PV target) {
    if (e->type == 1) {
        double t; int s1, s2;
        md_dd870(e, *pos, &t, &s1); md_dd870(e, target, &t, &s2);
        int step;
        if (dir == 0) { step = 1; s1 = s1 + 1; } else { step = -1; s2 = s2 + 1; }
        for (int v = s1 * step, k = s1; v <= s2 * step; v += step * step, k += step) { PV q = pv(e->pts[k].x, e->pts[k].y); md_move(m, q); *pos = q; }
    }
    md_move(m, target); *pos = target;
}
/* FUN_1801daeb0 (l.327394-327638, R, D:1801daeb0-1801db481): one try of the link from -> to along the medial axis.
   Returns 0 done, -1 (0xffff) failed, else FUN_1801e0f80's code */
static int md_daeb0(const MdEmit *m, const MdEdge *medax, PV from, PV to, double tol) {
    /* the model FUN_1801d69b0 (CMdlPocks, l.324441): copies of the edges and of the link bounds */
    MdEdge *E = md_d6b10(medax);
    const Gls *bounds = m->rb;
    int open = 0;
    for (int i = 0; i < bounds->n; i++) {                             /* FUN_1801d7370: an open bound */
        const Gl *l = &bounds->l[i]; if (!l->n) continue;
        PV a = gt_start(&l->e[0]), b = gt_end(&l->e[l->n - 1]);
        if (g_nc_tol * g_nc_tol < (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)) { open = 1; break; }
    }
    if (!open) {
        MdBs mb = { calloc((size_t)bounds->n + 1, sizeof(MdBnd)), 0 };   /* build_pock_bound_info + pock_bounds_bbox (ours: exact) */
        for (int i = 0; i < bounds->n; i++) {
            const Gl *l = &bounds->l[i]; MdBnd *b = &mb.b[mb.n++];
            b->e = calloc((size_t)l->n + 1, sizeof(MdEnt)); b->n = l->n; b->island = l->island;
            b->bb[0] = b->bb[1] = 1e300; b->bb[2] = b->bb[3] = -1e300;
            for (int k = 0; k < l->n; k++) {
                b->e[k].g = l->e[k]; zz_gt_bbox(&l->e[k], b->e[k].bb);
                b->bb[0] = fmin(b->bb[0], b->e[k].bb[0]); b->bb[1] = fmin(b->bb[1], b->e[k].bb[1]);
                b->bb[2] = fmax(b->bb[2], b->e[k].bb[2]); b->bb[3] = fmax(b->bb[3], b->e[k].bb[3]);
            }
        }
        md_d6c70(E); md_d6f40(E, &mb); md_d6d50(&E, bounds, g_nc_tol);
        md_free_bs(&mb);
    }
    int ret = -1, nt = 0; MdTab *T = NULL;
    MdEdge *Ef = md_da350(E, from), *Et = md_da350(E, to);
    if (!Ef || !Et) goto out;
    T = md_d7be0(E, &nt);
    if (!T) goto out;
    for (MdEdge *x = E; x; x = x->next) x->state = 0;
    Ef->state = 3; Et->state = 3;
    /* phase 1: depth-first search from Efrom's end d (D:1801dafce-1801db1c4) */
    int found = 0, dsel = 0; short endTo = 0;
    for (short d = 0; d < 2; d++) {
        if (found) break;
        int ce = d, c = md_dcc10(Ef, Et, tol, d, &endTo), fail = 0; MdEdge *cur = Ef;
        while (!c) {
            short ne = 0; int st = 1;
            MdEdge *nx = md_d9f90(T, nt, tol, cur, ce, 0, &ne);
            if (!nx) {
                if (cur == Ef) { fail = 1; break; }
                cur->state = 3;
                int ce2 = ce == 0;
                nx = md_d9f90(T, nt, tol, cur, ce2, 0, &ne);
                if (!nx) {
                    nx = md_d9f90(T, nt, tol, cur, ce2, 1, &ne);
                    if (!nx) { nx = md_d9f90(T, nt, tol, cur, ce, 1, &ne); if (!nx) { fail = 1; break; } }
                    st = 3;
                }
            }
            cur = nx; nx->state = st; ce = ne == 0;
            c = md_dcc10(cur, Et, tol, ce, &endTo);
        }
        if (!fail) { found = 1; dsel = d; }
    }
    Et->state = 1; Ef->state = 3;
    double tF, tT; int sg;
    PV pF = md_dd870(Ef, from, &tF, &sg), pT = md_dd870(Et, to, &tT, &sg);
    if (getenv("PK_DEBUG")) fprintf(stderr, "medial link try tol %g: %.4f %.4f -> %.4f %.4f found %d dsel %d same %d\n", tol, from.x, from.y, to.x, to.y, found, dsel, Ef == Et);
    if (Ef == Et) { ret = md_e0f80(m, from, to); goto out; }
    int dirF = dsel == 0;
    {   /* phase 2a: confirm the path (state 1 -> 2), D:1801db260-1801db2ce */
        MdEdge *cur = Ef; int ce = dsel;
        while (!md_dcc10(cur, Et, tol, ce, &endTo)) {
            short ne = 0; MdEdge *nx = md_d9f90(T, nt, tol, cur, ce, 1, &ne);
            if (!nx) { ret = -1; goto out; }
            nx->state = 2; ce = ne == 0; cur = nx;
        }
    }
    {   /* phase 2b: emit (D:1801db318-1801db42e) */
        ret = md_e0f80(m, from, pF);
        if (ret != 0) goto out;
        PV pos = pF;
        md_dd4c0(m, Ef, dirF, &pos);
        MdEdge *cur = Ef; int ce = dsel;
        while (!md_dcc10(cur, Et, tol, ce, &endTo)) {
            short ne = 0; MdEdge *nx = md_d9f90(T, nt, tol, cur, ce, 2, &ne);
            if (!nx) { ret = -1; goto out; }
            md_dd4c0(m, nx, ne, &pos); ce = ne == 0; nx->state = 3; cur = nx;
        }
        md_dd330(m, Et, endTo, &pos, pT);
        ret = md_e0f80(m, pT, to);
    }
out:
    free(T); md_free_edges(&E);
    return ret;
}
/* FUN_1801dae10 (R, D:1801dae10): up to 8 tries, tol doubled after each failed one; the last code is returned (the
   caller FUN_1801e9ee0 then links with FUN_1801e0f80). Moves written by a failed try stay (literal). */
static int md_dae10(const MdEmit *m, const MdEdge *medax, PV from, PV to, double tol) {
    int r = 0;
    for (short k = 0; k < 8; k++) {
        r = md_daeb0(m, medax, from, to, tol);
        if (r == 0) return 0;
        tol = tol * 2.0;
    }
    return r;
}
