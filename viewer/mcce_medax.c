/* mcce_medax.c - port of mcce::CalcMedAx (Mastercam 2017 mcce.dll @180005d3f), the exact medial axis (Voronoi diagram)
 * of a region bounded by closed loops of line segments (arcs chorded first) and reflex-vertex point sites, traced edge by
 * edge from the sharpest convex corner of the outer loop. Included by mcce_dyn.c (uses its P2 helpers).
 *
 * Spec: decomp/hst2/calcmedax_spec.txt (sections 2-3; "l.N" = decomp/mcce_all.c line N, "f.A-B" = function body) and its
 * addendum decomp/medax/medaxgaps/medaxgaps_spec.txt (parabola frame, sort comparators, clean-up).
 * formats/mcce_engines.md 6.15. Every constant / comparison strictness / loop order follows the decompile; pieces marked
 * NOT DECODED are listed in formats/mcce_engines.md 6.15 "Port". */

/* CSh entity (stride 0x88) as CalcMedAx uses it: +0x08 id 'L' / 'A' / 'P'; +0x12 bit 0x40 = has a source entity
   (l.104396); 'L' +0x18 e1 / +0x28 e2; 'A' +0x18 c, +0x28 r, +0x30 sa, +0x38 sw; 'P' +0x18 point; +0x60 source shape in R,
   +0x80 source entity in R (l.104398-104401) */
typedef struct { char id; P2 p1, p2, c; double r, sa, sw; int f40, src_sh, src_ent; } MxE;
/* CSh: entities, +0x50 dir (1 ccw, -1 cw), +0x54 bit0 hole */
typedef struct { MxE *e; int n, cap; int dir, hole; } MxS;
typedef struct { MxS *s; int n, cap; } MxSL;

/* MAE record (stride 0x118, spec section 2) */
typedef struct {
    int type;                       /* +0x00: 0 line-line, 1 line-point (parabola), 2 point-point */
    P2 O, d; double s1;             /* +0x08 / +0x18 / +0x28 (types 0 / 2: X(s) = O + s d, s in [0, s1]) */
    double h, u0, u1;               /* +0x30 / +0x38 / +0x40 (type 1) */
    double M[6], Mi[6];             /* +0x48 world -> local, +0x90 local -> world (2D affine: x' = a x + b y + c) */
    int sh1, en1; double t1s, t1e;  /* side 1 +0xd8 / +0xdc / +0xe0 (t at the edge start) / +0xe8 (t at the end) */
    int sh2, en2; double t2s, t2e;  /* side 2 +0xf0 / +0xf4 / +0xf8 / +0x100 */
    int parent, ch1, ch2;           /* +0x108 / +0x10c / +0x110 */
    unsigned flags;                 /* +0x114: 1 = child slot 1, 2 = removed */
} Mae;
typedef struct { Mae *r; int n, cap; } MaeL;
typedef struct { double s; int sh, ent; double t; } Epd;   /* EPD (stride 0x18): +0 s, +8 shape, +0xc entity, +0x10 t */
typedef struct { Epd *r; int n, cap; } EpdL;

static void mx_push(MxS *s, MxE e) { if (s->n == s->cap) { s->cap = s->cap ? 2 * s->cap : 16; s->e = realloc(s->e, s->cap * sizeof *s->e); } s->e[s->n++] = e; }
static void mx_free(MxS *s) { free(s->e); s->e = NULL; s->n = s->cap = 0; }
static void msl_push(MxSL *l, MxS s) { if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 8; l->s = realloc(l->s, l->cap * sizeof *l->s); } l->s[l->n++] = s; }
static void msl_free(MxSL *l) { for (int i = 0; i < l->n; i++) mx_free(&l->s[i]); free(l->s); l->s = NULL; l->n = l->cap = 0; }
static void mae_push(MaeL *l, const Mae *m) { if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 32; l->r = realloc(l->r, l->cap * sizeof *l->r); } l->r[l->n++] = *m; }
static void epd_push(EpdL *l, Epd e) { if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 8; l->r = realloc(l->r, l->cap * sizeof *l->r); } l->r[l->n++] = e; }

/* FUN_180021580 (f.23111-23154): point at t */
static P2 mx_at(const MxE *e, double t) {
    if (e->id == 'L') return p2((e->p2.x - e->p1.x) * t + e->p1.x, (e->p2.y - e->p1.y) * t + e->p1.y);
    if (e->id == 'A') { double a = t * e->sw + e->sa; return p2(cos(a) * e->r + e->c.x, sin(a) * e->r + e->c.y); }
    if (e->id == 'P') return e->p1;
    return p2(0, 0);
}
static const MxE *G_ent(const MxSL *G, int sh, int ent) { return &G->s[sh].e[ent]; }

/* CalcLnLnIntersectPt @180002216 (f.3611-3649): lines {p, d} and {q, e} (directions not normalised); t1 on the first,
   t2 on the second, X = p + t1 d; false when |den| < 1e-14, else |q + t2 e - X|^2 <= 1e-12 */
static int lnln(P2 p, P2 d, P2 q, P2 e, double *t1, double *t2, P2 *X) {
    double den = -(e.y * d.x) - -(e.x * d.y);
    if (fabs(den) < 1e-14) return 0;
    double a = (-((q.x - p.x) * e.y) - -((q.y - p.y) * e.x)) / den;
    P2 x = p2(a * d.x + p.x, a * d.y + p.y);
    double b = ((q.y - p.y) * d.x - (q.x - p.x) * d.y) / den;
    if (t1) *t1 = a;
    if (t2) *t2 = b;
    if (X) *X = x;
    double dy = (b * e.y + q.y) - x.y, dx = (b * e.x + q.x) - x.x;
    return dy * dy + dx * dx <= 1e-12;
}
/* FUN_1800ad500 (f.~94200): nearest point on segment a-b (t clamped to [0, 1]; degenerate -> midpoint, t 0.5) */
static P2 near_seg(P2 a, P2 b, P2 q, double *tt) {
    double dx = b.x - a.x, dy = b.y - a.y, l2 = dy * dy + dx * dx;
    if (l2 < 1e-14) { if (tt) *tt = 0.5; return p2((a.x + b.x) * 0.5, (a.y + b.y) * 0.5); }
    double t = -(((a.y - q.y) * dy + (a.x - q.x) * dx) / l2);
    if (t < 0.0 || 1.0 < t) t = t < 0.0 ? 0.0 : 1.0;
    if (tt) *tt = t;
    return p2(t * dx + a.x, t * dy + a.y);
}
/* GetNearestEntPt @180004d31 (f.8319-8366): 'L' FUN_1800ad500, 'P' the point ('A' does not occur after chording) */
static P2 mx_nearest(const MxE *e, P2 q) {
    if (e->id == 'L') return near_seg(e->p1, e->p2, q, NULL);
    if (e->id == 'P') return e->p1;
    return p2(0, 0);   /* ('A': GetNearestArcPt, unused here) */
}

/* 2D affine maps of the parabola frame (CTMatrix, see the addendum) */
static P2 mx_apply(const double M[6], P2 p) { return p2((p.y * M[1] + p.x * M[0]) + M[2], (p.x * M[3] + p.y * M[4]) + M[5]); }   /* operator*(CPt2) @180166990 order */

/* ---------------------------------------------------------------------------------------------- MAE evaluators */
/* FUN_1800c60d0 (f.107005-107016): start parameter (0; type 1 u0) */
static double mae_s0(const Mae *r) { return r->type == 1 ? r->u0 : 0.0; }
/* FUN_1800c60f0 (f.107017-107031): end parameter (s1; type 1 u1) */
static double mae_s1(const Mae *r) { return (r->type & ~2) == 0 ? r->s1 : r->type == 1 ? r->u1 : 0.0; }
/* FUN_1800c5230 (f.106550-106588): point at s; type 1 = (+0x90) * (h u^2, 2 h u) */
static P2 mae_pt(const Mae *r, double s) {
    if ((r->type & ~2) == 0) return p2(s * r->d.x + r->O.x, s * r->d.y + r->O.y);
    if (r->type == 1) return mx_apply(r->Mi, p2(s * r->h * s, s * r->h * 2.0));
    return p2(0, 0);
}
/* FUN_1800c5060 (f.106474-106513): end point */
static P2 mae_end(const Mae *r) { return mae_pt(r, mae_s1(r)); }
/* FUN_1800ab790 / FUN_1800ab890 (f.93185-93227): parabola arc length */
static double par_F(double h, double u) { double k = h * 2.0 * u, a = sqrt(h * h * 4.0 + k * k); return log((a + k) / (h * 2.0)) * h + sqrt((k * k) / (h * h) + 4.0) * k * 0.25; }
static double par_len(double h, double u0, double u1) {
    double a = par_F(h, u0), b = fabs(par_F(h, u1));
    if (0.0 <= u0 * u1) return (b <= fabs(a)) ? fabs(a) - b : b - fabs(a);
    return b + fabs(a);
}
/* FUN_1800c5170 (f.106514-106549): length */
static double mae_len(const Mae *r) {
    if ((r->type & ~2) == 0) { P2 e = mae_end(r); return sqrt((e.y - r->O.y) * (e.y - r->O.y) + (e.x - r->O.x) * (e.x - r->O.x)); }
    if (r->type == 1) return par_len(r->h, r->u0, r->u1);
    return 0;
}
/* FUN_1800c5330 (f.106589-106696): clearance (radius) at s, and the point */
static double mae_rad(const MxSL *G, const Mae *r, double s, P2 *pt) {
    double rho = 0; P2 X = p2(0, 0);
    if (r->type == 0) {
        X = p2(s * r->d.x + r->O.x, s * r->d.y + r->O.y);
        const MxE *e = G_ent(G, r->sh1, r->en1); P2 dd = psub(e->p2, e->p1), F;
        if (lnln(X, p2(-dd.y, dd.x), e->p1, dd, NULL, NULL, &F)) rho = sqrt((X.y - F.y) * (X.y - F.y) + (X.x - F.x) * (X.x - F.x));
    } else if (r->type == 1) {
        P2 L = p2(r->h * s * s, r->h * s * 2.0);
        rho = sqrt((L.y - 0.0) * (L.y - 0.0) + (L.x - r->h) * (L.x - r->h));
        X = mx_apply(r->Mi, L);
    } else if (r->type == 2) {
        X = p2(s * r->d.x + r->O.x, s * r->d.y + r->O.y);
        P2 q = G_ent(G, r->sh1, r->en1)->p1;
        rho = sqrt((X.x - q.x) * (X.x - q.x) + (X.y - q.y) * (X.y - q.y));
    }
    if (pt) *pt = X;
    return rho;
}
/* FUN_1800c6080 (f.106986-107004): t outside (0, 1) -> 0 below 0.5, else 1 */
static double snap01(double t) { if (t <= 0.0 || 1.0 <= t) return t < 0.5 ? 0.0 : 1.0; return t; }
/* FUN_1800c4a30 (f.106299-106433): the side parameters at s */
static void side_params(const MxSL *G, const Mae *r, double s, double *ta, double *tb) {
    if (ta) *ta = 0.0;
    if (tb) *tb = 0.0;
    const MxE *a = G_ent(G, r->sh1, r->en1), *b = G_ent(G, r->sh2, r->en2);
    if (r->type == 0) {
        P2 X = p2(s * r->d.x + r->O.x, s * r->d.y + r->O.y), d = psub(a->p2, a->p1); double t;
        if (lnln(a->p1, d, X, p2(d.y, -d.x), &t, NULL, NULL) && ta) *ta = snap01(t);
        d = psub(b->p2, b->p1);
        if (lnln(b->p1, d, X, p2(d.y, -d.x), &t, NULL, NULL) && tb) *tb = snap01(t);
    }
    if (r->type == 1) {
        P2 W = mx_apply(r->Mi, p2(-r->h, r->h * 2.0 * s));   /* the foot (-h, 2hu) mapped back (l.106380-106383) */
        const MxE *L = a->id == 'L' ? a : b->id == 'L' ? b : NULL;
        if (L) {
            P2 d = psub(L->p2, L->p1); double t = 0.0, ax = fabs(d.x);
            if (ax <= fabs(d.y)) { if (1e-12 <= fabs(d.y)) t = (W.y - L->p1.y) / d.y; }
            else if (1e-12 <= ax) t = (W.x - L->p1.x) / d.x;
            if (L == a) { if (ta) *ta = snap01(t); } else { if (tb) *tb = snap01(t); }
        }
    }
}
/* FUN_1800c6480 (f.107159-107175): set the end parameter and the end side parameters */
static void mae_set_end(const MxSL *G, Mae *r, double s) {
    if ((r->type & ~2) == 0) r->s1 = s; else if (r->type == 1) r->u1 = s;
    side_params(G, r, s, &r->t1e, &r->t2e);
}
/* FUN_1800c64d0 (f.107176-107209): trim the edge to [a, b] */
static void mae_trim(const MxSL *G, Mae *r, double a, double b) {
    double s0 = 0, s1 = 0;
    if ((r->type & ~2) == 0) { s1 = b - a; s0 = 0.0; r->O = p2(a * r->d.x + r->O.x, a * r->d.y + r->O.y); r->s1 = s1; }
    else if (r->type == 1) { r->u0 = a; r->u1 = b; s0 = a; s1 = b; }
    side_params(G, r, s0, &r->t1s, &r->t2s);
    side_params(G, r, s1, &r->t1e, &r->t2e);
}

/* ---------------------------------------------------------------------------------------------- roots */
/* sort comparator LAB_180002509 (l.95444) */
static int cmp_dbl(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return y > x ? -1 : (x > y ? 1 : 0); }   /* = 1800afd00: ascending (equal roots are merged after) */
/* FUN_1800b1840 (f.96247-96289): quadratic c0 + c1 x + c2 x^2 */
static int quad_roots(const double c[3], double *x) {
    int n = 0; double disc = c[1] * c[1] - c[2] * 4.0 * c[0];
    if (disc < 0.0) disc = 0.0;
    double q = (sqrt(disc) * (c[1] < 0.0 ? -1.0 : 1.0) + c[1]) * -0.5;
    if (1e-14 < fabs(q)) x[n++] = c[0] / q;
    if (!(fabs(c[2]) <= 1e-14)) x[n++] = q / c[2];
    return n;
}
/* FUN_1800afd30 (f.95292-95478) for degree <= 2 (the only use here): returns the roots ascending */
static int poly_roots2(const double c0[3], double *out) {
    double c[3] = { c0[0], c0[1], c0[2] }, x[4]; int deg = 2, n = 0;
    while (deg > 0 && !(1e-14 <= fabs(c[deg]))) deg--;              /* trailing |c| < 1e-14 dropped (l.95345) */
    if (deg == 0) {
        if (fabs(c[0]) < 1e-14) x[n++] = 0.0;   /* zero polynomial: root 0 (l.95349-95352); FUN_1800b06b0 at degree 0 (l.95691: its loop
                                                   does not run) adds no root; FUN_1800b12f0's Newton polish (l.96117-96185) is a no-op
                                                   here: p'(0) = c1 with |c1| < 1e-14 < 1e-12 stops it before the first step */
    } else {
        while (deg > 0 && fabs(c[0]) < 1e-15) {                     /* factor out x, root 0 once */
            for (int k = 0; k < deg; k++) c[k] = c[k + 1];
            if (n == 0) x[n++] = 0.0;
            deg--;
        }
        if (deg == 1) x[n++] = -(c[0] / c[1]);
        else if (deg == 2) n += quad_roots(c, x + n);
    }
    for (int i = 0; i < n; i++) {                                    /* |p(x)| > 1e-5 dropped, original coefficients */
        double v = c0[2];
        for (int k = 1; k >= 0; k--) v = x[i] * v + c0[k];
        if (1e-05 < fabs(v)) { memmove(x + i, x + i + 1, (n - i - 1) * sizeof *x); n--; i--; }
    }
    if (n > 1) {
        qsort(x, n, sizeof *x, cmp_dbl);
        for (int i = 1; i < n; i++) if (fabs(x[i] - x[i - 1]) < 1e-12) { memmove(x + i, x + i + 1, (n - i - 1) * sizeof *x); n--; i--; }
    }
    for (int i = 0; i < n; i++) out[i] = x[i];
    return n;
}

/* ---------------------------------------------------------------------------------------------- bisectors */
/* the parabola builder FUN_1800b3a10, its limit FUN_1800b2dc0 and the two parabola contacts are in mcce_medax_par.c */
static int bis_lp(const MxSL *G, const MaeL *out, Mae *r);
static int contact_lpar(const MxSL *G, const Mae *r, int k, int j, double *t, double *s);
static int contact_ppar(const MxSL *G, const Mae *r, int k, int j, double *s);

/* FUN_1800b3980 (f.97262-97601): build the bisector of the two sides */
static int bisector(const MxSL *G, const MaeL *out, Mae *r) {
    const MxE *a = G_ent(G, r->sh1, r->en1), *b = G_ent(G, r->sh2, r->en2);
    if (a == b) return 0;                                                          /* l.97338 */
    if (a->id == 'L' && b->id == 'L') {                                            /* type 0, l.97342 */
        double dx1 = a->p2.x - a->p1.x, dy1 = a->p2.y - a->p1.y, dx2 = b->p2.x - b->p1.x, dy2 = b->p2.y - b->p1.y;
        if (!((1e-12 < fabs(dx1)) || (1e-12 < fabs(dy1)))) return 0;
        double a1 = atan2(dy1, dx1);
        if (!((1e-12 < fabs(dx2)) || (1e-12 < fabs(dy2)))) return 0;
        double a2 = atan2(dy2, dx2), D = a1 - a2, k;
        if (-3.141592653589793 <= D) { if (3.141592653589793 < D) D -= 6.283185307179586; } else D += 6.283185307179586;
        k = (D <= 0.0) ? -3.141592653589793 - D : 3.141592653589793 - D;
        double ang = D + k * 0.5 + a2;                                             /* l.97396 */
        P2 d = p2(cos(ang), sin(ang)), O = r->parent == -1 ? a->p1 : mae_end(&out->r[r->parent]);   /* l.97401 / l.97408 */
        P2 X; double sa, sb;
        P2 q = p2(dx1 * r->t1s + a->p1.x, dy1 * r->t1s + a->p1.y), n1 = p2(dy1, -dx1);
        if (!lnln(O, d, q, n1, NULL, NULL, &X)) return 0;
        if (!((X.x - O.x) * (X.x - O.x) + (X.y - O.y) * (X.y - O.y) <= 1e-12)) return 0;
        if (!lnln(O, d, p2(dx1 + a->p1.x, dy1 + a->p1.y), n1, &sa, NULL, &X) || !(0.0 <= sa)) return 0;   /* l.97430 */
        q = p2(dx2 * r->t2s + b->p1.x, dy2 * r->t2s + b->p1.y); P2 n2 = p2(dy2, -dx2);
        if (!lnln(O, d, q, n2, NULL, NULL, &X)) return 0;
        if (!((X.y - O.y) * (X.y - O.y) + (X.x - O.x) * (X.x - O.x) <= 1e-12)) return 0;   /* l.97444 */
        if (!lnln(O, d, b->p1, n2, &sb, NULL, &X) || !(0.0 <= sb)) return 0;      /* l.97456-97461 */
        r->type = 0; r->O = O; r->d = d; r->s1 = (sb <= sa) ? sb : sa;             /* l.97462-97471 */
        return 1;
    }
    if (a->id != 'P' || b->id != 'P') return bis_lp(G, out, r);                    /* l.97484 */
    /* P-P, type 2 (l.97485-97600) */
    P2 p1 = a->p1, q2 = b->p1, dd = psub(q2, p1);
    double L = sqrt(dd.x * dd.x + -dd.y * -dd.y);
    if (L < 1e-12) return 0;                                                       /* l.97523 */
    P2 n = p2(-dd.y / L, dd.x / L);
    if (r->parent == -1) return 0;                                                 /* l.97530 */
    P2 O = mae_end(&out->r[r->parent]), M = p2((q2.x + p1.x) * 0.5, (q2.y + p1.y) * 0.5), F;
    /* FUN_1800ad400: the foot of O on {M, n} (l.97537) */
    if (!lnln(M, n, O, p2(n.y, -n.x), NULL, NULL, &F)) F = M;
    if (1e-12 < (F.x - O.x) * (F.x - O.x) + (F.y - O.y) * (F.y - O.y)) return 0;   /* l.97540 */
    const MxS *S1 = &G->s[r->sh1], *S2 = &G->s[r->sh2];
    const MxE *na = &S1->e[(r->en1 + 1) % S1->n];
    if (na->id != 'L') return 0;                                                   /* l.97550 */
    int jb = r->en2 - 1; if (jb < 0) jb = S2->n - 1;
    const MxE *nb = &S2->e[jb];
    if (nb->id != 'L') return 0;                                                   /* l.97558 */
    double sA, tA, sB, tB;
    /* the second line's direction is (dy, -dx) of the neighbour (l.97560-97563: auStack_90 = (y2 - y1, -(x2 - x1)), auStack_b0 the
       same for nb; its point is side 2's 'P' = nb's end). Fixed 2026-11-14: it was (-dy, dx), which flips tA / tB below, so on
       nearly straight walls both were rejected and s1 became 3.4e38 (edges leaving the region; OptiRough TUTORIAL 7 op 5's helix
       centre outside the part, "nothing to cut"). Checked against mcce.dll FUN_1800c5df0's MAE list (peloader MCCE_TRACE_MEDAX). */
    int oa = lnln(O, n, na->p1, p2(na->p2.y - na->p1.y, -(na->p2.x - na->p1.x)), &sA, &tA, NULL);   /* l.97568 */
    int ob = lnln(O, n, nb->p2, p2(nb->p2.y - nb->p1.y, -(nb->p2.x - nb->p1.x)), &sB, &tB, NULL);   /* l.97571 */
    if (oa && tA < 0.0) oa = 0;                                                    /* l.97575-97580 */
    if (ob && tB < 0.0) ob = 0;
    if ((oa && sA < 0.0) || (ob && sB < 0.0)) return 0;                            /* l.97581-97582 */
    double s1;
    if (!oa) s1 = ob ? sB : 3.4028234663852886e+38;
    else s1 = (!ob || sA < sB) ? sA : sB;
    r->type = 2; r->O = O; r->d = n; r->s1 = s1;                                   /* l.97591-97598 */
    return 1;
}

/* ---------------------------------------------------------------------------------------------- contacts */
/* FUN_1800bb2d0 (f.101470-101553): r(s) = r0 + r1 s, signed distances of O and O + d from the side-1 line */
static int rad_lin(const MxSL *G, const Mae *r, double *r0, double *r1) {
    const MxE *a = G_ent(G, r->sh1, r->en1); P2 dd = psub(a->p2, a->p1), n = p2(-dd.y, dd.x), X; double t;
    if (!lnln(r->O, n, a->p1, dd, &t, NULL, &X)) return 0;
    double d0 = sqrt((r->O.y - X.y) * (r->O.y - X.y) + (r->O.x - X.x) * (r->O.x - X.x)); if (t < 0.0) d0 = -d0;
    P2 O2 = padd(r->O, r->d);
    if (!lnln(O2, n, a->p1, dd, &t, NULL, &X)) return 0;
    double d1 = sqrt((O2.y - X.y) * (O2.y - X.y) + (O2.x - X.x) * (O2.x - X.x)); if (t < 0.0) d1 = -d1;
    *r0 = d0; *r1 = d1 - d0;
    return 1;
}
/* FUN_1800bb520 (f.101554-101588): t in (0, 1), or the point at t within 1e-5 of the nearer end */
static int t_ok(P2 p, P2 d, double t) {
    if (0.0 < t && t < 1.0) return 1;
    double x = t * d.x + p.x, y = t * d.y + p.y;
    if (0.5 <= t) { x -= p.x + d.x; y -= p.y + d.y; } else { x -= p.x; y -= p.y; }
    return y * y + x * x < 1.0000000000000002e-10;
}
/* FUN_1800bb6a0 (f.101589-101625): start < s < end, or point(s) == point(start) */
static int s_ok(const Mae *r, double s) {
    double a = mae_s0(r), b = mae_s1(r);
    if (!(s <= a) && !(b <= s)) return 1;
    P2 x = mae_pt(r, s), y = mae_pt(r, a);
    return (x.y - y.y) * (x.y - y.y) + (x.x - y.x) * (x.x - y.x) < 9.999999999999998e-15;
}
/* FUN_1800b9ca0 (f.100735-100809): line x straight bisector */
static int contact_lstr(const MxSL *G, const Mae *r, int k, int j, double *t, double *s) {
    const MxE *e = G_ent(G, k, j); P2 p = e->p1, d = psub(e->p2, e->p1);
    double r0, r1;
    if (!rad_lin(G, r, &r0, &r1)) return 0;
    P2 u = d; double l = sqrt(d.y * d.y + d.x * d.x);
    if (1e-12 <= l) u = p2(u.x / l, u.y / l);
    P2 A = p2(r0 * -u.y + r->O.x, r0 * u.x + r->O.y), B = p2(r1 * -u.y + r->d.x, r1 * u.x + r->d.y);
    if (!lnln(A, B, p, d, s, t, NULL)) return 0;
    if (*s * r1 + r0 < 0.0) return 0;
    if (!s_ok(r, *s)) return 0;
    return t_ok(p, d, *t);
}
/* FUN_1800b9f20 (f.100810-100899): point x straight bisector, |O + s d - P|^2 = (r0 + r1 s)^2 */
static int contact_pstr(const MxSL *G, const Mae *r, P2 P, double *s) {
    double r0, r1;
    if (!rad_lin(G, r, &r0, &r1)) return 0;
    double ox = r->O.x, oy = r->O.y, dx = r->d.x, dy = r->d.y, c[3], x[4];
    c[0] = ((((ox * ox - ox * 2.0 * P.x) + P.x * P.x + oy * oy) - oy * 2.0 * P.y) + P.y * P.y) - r0 * r0;   /* l.100869 */
    c[2] = (dy * dy + dx * dx) - r1 * r1;                                                                  /* l.100871 */
    c[1] = ((((dx * ox - dx * P.x) + dy * oy) - dy * P.y) - r1 * r0) * 2.0;                                /* l.100872 */
    int n = poly_roots2(c, x);
    for (int i = 0; i < n; i++) if (s_ok(r, x[i])) { *s = x[i]; return 1; }
    return 0;
}
/* FUN_1800baa80 (f.101164-101309): line x point-point bisector: r as type 0 with both sides = the line, then the
   point contact against r's side-1 point; t = foot of X(s) on the line */
static int contact_lpp(const MxSL *G, const Mae *r, int k, int j, double *t, double *s) {
    Mae q = *r; q.type = 0; q.sh1 = q.sh2 = k; q.en1 = q.en2 = j; q.t1s = q.t1e = q.t2s = q.t2e = -1.0;
    if (!contact_pstr(G, &q, G_ent(G, r->sh1, r->en1)->p1, s)) return 0;
    const MxE *e = G_ent(G, k, j); P2 d = psub(e->p2, e->p1), X = mae_pt(r, *s);
    if (!lnln(e->p1, d, X, p2(-d.y, d.x), t, NULL, NULL)) return 0;
    return t_ok(e->p1, d, *t);
}
/* FUN_1800bad40 (f.101310-101402): point x point-point bisector (linear) */
static int contact_ppp(const MxSL *G, const Mae *r, P2 P, double *s) {
    P2 A = G_ent(G, r->sh1, r->en1)->p1, B = G_ent(G, r->sh2, r->en2)->p1, Q;
    double ca = ((A.y - P.y) * r->d.y + (A.x - P.x) * r->d.x) * 2.0, cb = ((B.y - P.y) * r->d.y + (B.x - P.x) * r->d.x) * 2.0, c;
    if (fabs(ca) <= fabs(cb)) { Q = B; c = cb; } else { Q = A; c = ca; }
    if (!(1e-14 <= fabs(c))) return 0;
    double v = (((((P.y - Q.y) * r->O.y + (P.x - Q.x) * r->O.x) * 2.0 - P.x * P.x) - P.y * P.y) + Q.x * Q.x + Q.y * Q.y) / c;
    if (!s_ok(r, v)) return 0;
    *s = v; return 1;
}
/* FUN_1800ba220 (f.100900-100947): dispatch on (candidate entity type, edge type) */
static int contact(const MxSL *G, const Mae *r, int k, int j, double *t, double *s) {
    char id = G_ent(G, k, j)->id; *t = 0.0;
    if (id == 'P' && r->type == 0) return contact_pstr(G, r, G_ent(G, k, j)->p1, s);
    if (id == 'L' && r->type == 0) return contact_lstr(G, r, k, j, t, s);
    if (id == 'L' && r->type == 1) return contact_lpar(G, r, k, j, t, s);
    if (id == 'L' && r->type == 2) return contact_lpp(G, r, k, j, t, s);
    if (id == 'P' && r->type == 1) return contact_ppar(G, r, k, j, s);
    if (id == 'P' && r->type == 2) return contact_ppp(G, r, G_ent(G, k, j)->p1, s);
    return 0;
}
/* FUN_1800bafe0 (f.101403-101469): the contact candidates over every entity but the two sides */
static void candidates(const MxSL *G, const Mae *r, EpdL *E) {
    E->n = 0;
    for (int k = 0; k < G->n; k++) for (int j = 0; j < G->s[k].n; j++) {
        if ((k == r->sh1 && j == r->en1) || (k == r->sh2 && j == r->en2)) continue;
        double t, s;
        if (contact(G, r, k, j, &t, &s)) { Epd e = { s, k, j, t }; epd_push(E, e); }
    }
}
/* FUN_1800bc160 (f.101900-102107): classify a contact: 1 = on a side's nearest point, 4 = outside the sides' angle, 2 = keep */
static int classify(const MxSL *G, const Mae *r, const Epd *c) {
    double tc = c->t; if (0.0 <= tc) { if (!(tc <= 1.0)) tc = 1.0; } else tc = 0.0;
    P2 P = mx_at(G_ent(G, c->sh, c->ent), tc);
    P2 n1 = mx_nearest(G_ent(G, r->sh1, r->en1), P);
    if ((n1.y - P.y) * (n1.y - P.y) + (n1.x - P.x) * (n1.x - P.x) < 9.999999999999998e-15) return 1;
    P2 n2 = mx_nearest(G_ent(G, r->sh2, r->en2), P);
    if ((n2.y - P.y) * (n2.y - P.y) + (n2.x - P.x) * (n2.x - P.x) < 9.999999999999998e-15) return 1;
    Mae q = *r; mae_set_end(G, &q, c->s);
    P2 Q = mae_end(&q), A = mx_at(G_ent(G, q.sh1, q.en1), q.t1e), B = mx_at(G_ent(G, q.sh2, q.en2), q.t2e);
    P2 vA = psub(A, Q), vB = psub(B, Q), vP = psub(P, Q);
    if (!((1e-12 < fabs(vA.x)) || (1e-12 < fabs(vA.y)))) return 2;
    double aA = atan2(vA.y, vA.x);
    if (!((1e-12 < fabs(vB.x)) || (1e-12 < fabs(vB.y)))) return 2;
    double aB = atan2(vB.y, vB.x);
    if (!((1e-12 < fabs(vP.x)) || (1e-12 < fabs(vP.y)))) return 2;
    double aP = atan2(vP.y, vP.x), dB = aB - aA, dP = aP - aA;
    if (-3.141592653589793 <= dB) { if (3.141592653589793 < dB) dB -= 6.283185307179586; } else dB += 6.283185307179586;
    if (0.0 < dB) dB += -6.283185307179586;
    if (-3.141592653589793 <= dP) { if (3.141592653589793 < dP) dP -= 6.283185307179586; } else dP += 6.283185307179586;
    if (0.0 < dP) dP += -6.283185307179586;
    return dP < dB ? 4 : 2;
}
/* qsort comparator LAB_180006ee7 (l.102131) = 1800bc9a0 (medaxgaps C): ascending s, equal -> 0 */
static int cmp_epd(const void *a, const void *b) { double x = ((const Epd *)a)->s, y = ((const Epd *)b)->s; return y > x ? -1 : (x > y ? 1 : 0); }
/* MSVCR120 qsort (import 0x1801d3638) order of equal keys [inferred, medaxgaps C: from the MSVC CRT source, not mcce.dll]:
   up to 8 elements "shortsort" (the maximum, first of equals, moved to the top); larger arrays: our stable sort (ours) */
static void epd_sort(EpdL *E) {
    if (E->n <= 8) {
        for (int hi = E->n - 1; hi > 0; hi--) {
            int mx = 0;
            for (int p = 1; p <= hi; p++) if (cmp_epd(&E->r[p], &E->r[mx]) > 0) mx = p;
            Epd t = E->r[mx]; E->r[mx] = E->r[hi]; E->r[hi] = t;
        }
        return;
    }
    for (int i = 1; i < E->n; i++) { Epd t = E->r[i]; int j = i - 1; while (j >= 0 && cmp_epd(&E->r[j], &t) > 0) { E->r[j + 1] = E->r[j]; j--; } E->r[j + 1] = t; }
}
/* FUN_1800bc720 (f.102108-102174): sort, classify, keep at most one contact; the end parameter */
static int filter_end(const MxSL *G, const Mae *r, EpdL *E, double *end) {
    if (E->n > 1) epd_sort(E);
    for (int i = 0; i < E->n; i++) {
        int c = classify(G, r, &E->r[i]);
        if (c == 4 || c == 1) { memmove(E->r + i, E->r + i + 1, (E->n - i - 1) * sizeof *E->r); E->n--; i--; }
        else if (c == 2 && i + 1 < E->n) E->n = i + 1;
    }
    if (E->n != 1) { *end = r->type == 1 ? r->u1 : r->s1; return *end <= 3.3994206429189033e+38; }
    *end = E->r[0].s;
    return 1;
}
/* FUN_1800bc070 (f.101883-101899): find the end of the edge */
static int find_end(const MxSL *G, Mae *r, EpdL *E) {
    double end;
    candidates(G, r, E);
    if (!filter_end(G, r, E, &end)) return 0;
    mae_set_end(G, r, end);
    return 1;
}

/* ---------------------------------------------------------------------------------------------- tracing */
/* FUN_1800bf450 (f.103649-103810): side-advance variants v = 0..3 */
static int advance(int v, const MxSL *G, const MaeL *out, Mae *r, int *a, int *b) {
    *a = 0; *b = 0;
    if (v == 0) {
        if (r->parent != -1) {
            const Mae *p = &out->r[r->parent];
            if (p->en1 == r->en1 && p->sh1 == r->sh1 && p->en2 == r->en2 && p->sh2 == r->sh2) return 0;
        }
        return 1;
    }
    if (v == 1 || v == 3) {
        const MxS *S = &G->s[r->sh1]; const MxE *e = &S->e[r->en1];
        if (e->id == 'P') *a = 1;
        else {
            P2 E = e->id == 'L' ? e->p2 : e->id == 'A' ? p2(cos(e->sw + e->sa) * e->r + e->c.x, sin(e->sw + e->sa) * e->r + e->c.y) : p2(0, 0);
            P2 q = mx_at(e, r->t1s);
            if ((E.x - q.x) * (E.x - q.x) + (E.y - q.y) * (E.y - q.y) < 9.999999999999998e-15 || 1.0 < r->t1s) *a = 1;
        }
        if (*a) { r->t1s = 0.0; r->en1 = (r->en1 + 1) % S->n; }
    }
    if (v == 2 || v == 3) {
        const MxS *S = &G->s[r->sh2]; const MxE *e = &S->e[r->en2];
        if (e->id == 'P') *b = 1;
        else {
            P2 E = e->id == 'L' ? e->p1 : e->id == 'A' ? p2(cos(e->sa) * e->r + e->c.x, sin(e->sa) * e->r + e->c.y) : p2(0, 0);
            P2 q = mx_at(e, r->t2s);
            if ((E.x - q.x) * (E.x - q.x) + (E.y - q.y) * (E.y - q.y) < 9.999999999999998e-15 || (r->t2s <= 0.0 && r->t2s != 0.0)) *b = 1;
        }
        if (*b) { int k = r->en2; if (k == 0) k = S->n; r->en2 = k - 1; r->t2s = 1.0; }
    }
    if (v == 1) return *a;
    if (v == 2 || v == 3) return (v == 2 || *a) && *b;
    return 0;
}
/* FUN_1800c0170 (f.104036-104080): t in [lo, hi) on line e */
static int in_lo_hi(const MxE *e, double p, double q, double t) {
    double lo = p, hi = q; if (q < p) { hi = p; lo = q; }
    if (hi < t) return 0;
    P2 d = psub(e->p2, e->p1), T = p2(d.x * t + e->p1.x, d.y * t + e->p1.y);
    double hx = (d.x * hi + e->p1.x) - T.x, hy = (d.y * hi + e->p1.y) - T.y, lx = (d.x * lo + e->p1.x) - T.x, ly = (d.y * lo + e->p1.y) - T.y;
    if (hy * hy + hx * hx < 1.0000000000000002e-10 || (1.0000000000000002e-10 <= ly * ly + lx * lx && t < lo)) return 0;
    return 1;
}
/* FUN_1800bfab0 (f.103862-103906): t in (lo, hi] */
static int in_lo_hi2(const MxE *e, double p, double q, double t) {
    double lo = p, hi = q; if (q < p) { hi = p; lo = q; }
    if (t < lo) return 0;
    P2 d = psub(e->p2, e->p1), T = p2(d.x * t + e->p1.x, d.y * t + e->p1.y);
    double ly = (d.y * lo + e->p1.y) - T.y, lx = (d.x * lo + e->p1.x) - T.x, hy = (d.y * hi + e->p1.y) - T.y, hx = (d.x * hi + e->p1.x) - T.x;
    if (ly * ly + lx * lx < 1.0000000000000002e-10 || (1.0000000000000002e-10 <= hy * hy + hx * hx && hi < t)) return 0;
    return 1;
}
/* FUN_1800bfce0 (f.103907-104035): already traced? */
static int is_dup(const MxSL *G, const MaeL *out, const Mae *r) {
    int na = G_ent(G, r->sh1, r->en1)->id != 'P', nb = G_ent(G, r->sh2, r->en2)->id != 'P';
    if (na || nb) {
        for (int i = 0; i < out->n; i++) {
            const Mae *m = &out->r[i];
            if (na) {
                if (m->sh1 == r->sh1 && m->en1 == r->en1 && in_lo_hi(G_ent(G, m->sh1, m->en1), m->t1s, m->t1e, r->t1s)) return 1;
                if (m->sh2 == r->sh1 && m->en2 == r->en1 && in_lo_hi(G_ent(G, m->sh2, m->en2), m->t2s, m->t2e, r->t1s)) return 1;
            }
            if (nb) {
                if (m->sh1 == r->sh2 && m->en1 == r->en2 && in_lo_hi2(G_ent(G, m->sh1, m->en1), m->t1s, m->t1e, r->t2s)) return 1;
                if (m->sh2 == r->sh2 && m->en2 == r->en2 && in_lo_hi2(G_ent(G, m->sh2, m->en2), m->t2s, m->t2e, r->t2s)) return 1;
            }
        }
        return 0;
    }
    for (int i = 0; i < out->n; i++) {
        const Mae *m = &out->r[i];
        if ((m->sh1 == r->sh1 && m->en1 == r->en1 && m->sh2 == r->sh2 && m->en2 == r->en2) ||
            (m->sh1 == r->sh2 && m->en1 == r->en2 && m->sh2 == r->sh1 && m->en2 == r->en1)) return 1;
    }
    return 0;
}
/* FUN_1800be7c0 (f.102932-103427): trace one edge from the work record c */
static int trace_edge(const MxSL *G, const MaeL *out, const Mae *c, Mae *e, EpdL *E) {
    Mae work = *c, r, best; double bl = -1.0; int bhas = 0; Epd bep = { 0 }; int a = 1, b = 1;
    memset(&best, 0, sizeof best);
    for (int guard = 0; guard < 1000000; guard++) {
        if (!a && !b) {                                                         /* l.103118 */
            if (bl <= -1e-07) return 0;
            *e = best; E->n = 0;
            if (bhas) epd_push(E, bep);
            return 1;
        }
        for (int v = 0; v < 4; v++) {
            r = work;
            if (!advance(v, G, out, &r, &a, &b)) continue;
            if (is_dup(G, out, &r)) return 0;                                   /* l.103252 */
            if (!bisector(G, out, &r)) continue;
            if (!find_end(G, &r, E)) continue;
            double L = mae_len(&r);
            if (L <= 0.0001) {                                                  /* l.103260 */
                if (bl < L) { bl = L; best = r; bhas = E->n > 0; if (bhas) bep = E->r[0]; }
                continue;
            }
            *e = r; return 1;
        }
        work = r;                                                               /* l.103379-103423 */
    }
    return 0;
}
/* FUN_1800bd590 (f.102426-102556): children of the edge just added (index k) */
static void spawn(const MxSL *G, const MaeL *out, const EpdL *E, MaeL *W) {
    int k = out->n - 1; const Mae *e = &out->r[k];
    if (e->sh1 == e->sh2 && (e->en1 + 1) % G->s[e->sh1].n == e->en2) return;  /* l.102460-102465 */
    int sh = e->sh1, en = e->en1; double t = e->t1e;
    for (int j = 0; j <= E->n; j++) {
        Mae c; memset(&c, 0, sizeof c);
        c.parent = k; c.ch1 = c.ch2 = -1; c.flags = 0;
        c.sh1 = sh; c.en1 = en; c.t1s = t;
        if (j < E->n) {
            sh = E->r[j].sh; en = E->r[j].ent; t = E->r[j].t;
            c.sh2 = sh; c.en2 = en; c.t2s = t; c.flags |= 1;
        } else {
            c.sh2 = e->sh2; c.en2 = e->en2; c.t2s = e->t2e;
            if (E->n == 0 && e->parent != -1 && out->r[e->parent].ch1 == k) c.flags |= 1;
        }
        P2 A = mx_at(G_ent(G, c.sh1, c.en1), c.t1s), B = mx_at(G_ent(G, c.sh2, c.en2), c.t2s);
        if ((A.y - B.y) * (A.y - B.y) + (A.x - B.x) * (A.x - B.x) < 1.0000000000000002e-10) continue;   /* l.102538 */
        mae_push(W, &c);
    }
}
/* FUN_1800bcc80 (f.102259-102425): seed at the sharpest convex corner of the outer (the last shape of G) */
static P2 mx_tan_end(const MxE *e) {
    if (e->id == 'L') return psub(e->p2, e->p1);
    if (e->id == 'A') {
        double a = e->sw + e->sa, y = cos(a) * e->r * fabs(e->sw), x = sin(a) * e->r * fabs(e->sw);
        if (e->sw <= 0.0) { if (0.0 <= e->sw) return p2(0, 0); y = -y; } else x = -x;
        return p2(x, y);
    }
    return p2(0, 0);
}
static P2 mx_tan_start(const MxE *e) {
    if (e->id == 'L') return psub(e->p2, e->p1);
    if (e->id == 'A') {
        double a = e->sw * 0.0 + e->sa, y = cos(a) * e->r * fabs(e->sw), x = sin(a) * e->r * fabs(e->sw);
        if (e->sw <= 0.0) { if (0.0 <= e->sw) return p2(0, 0); y = -y; } else x = -x;
        return p2(x, y);
    }
    return p2(0, 0);
}
static int seed(const MxSL *G, MaeL *W) {
    int o = G->n - 1; const MxS *S = &G->s[o];
    int bp = -1, bc = -1, prev = S->n - 1; double best = 2.0;
    for (int i = 0; i < S->n; i++) {
        const MxE *p = &S->e[prev], *c = &S->e[i];
        if (p->id != 'P' && c->id != 'P') {
            P2 tp = mx_tan_end(p), tc = mx_tan_start(c);
            double lp = sqrt(tp.y * tp.y + tp.x * tp.x);
            if (1e-12 <= lp) {
                double lc = sqrt(tc.x * tc.x + tc.y * tc.y);
                if (1e-12 <= lc) {
                    double d = (tp.y / lp) * (tc.y / lc) + (tc.x / lc) * (tp.x / lp);
                    if (d <= best) { best = d; bp = prev; bc = i; }
                }
            }
        }
        prev = i;
    }
    if (bp == -1) return 0;                                                     /* Reg(0x6e, "MedAxInit.cpp", 199) */
    Mae c; memset(&c, 0, sizeof c);
    c.parent = c.ch1 = c.ch2 = -1; c.flags = 0;
    c.sh1 = o; c.en1 = bc; c.t1s = 0.0; c.sh2 = o; c.en2 = bp; c.t2s = 1.0;
    W->n = 0; mae_push(W, &c);
    return 1;
}

/* ---------------------------------------------------------------------------------------------- post-processing */
/* FUN_1800c30c0 (f.105632-105788): two neighbouring source entities joining smoother than angDeg */
static int smooth_joint(const MxSL *R, int sh, int e1, int sh2, int e2, double angDeg) {
    const MxS *S = &R->s[sh]; int last = S->n - 1;
    if (abs(e1 - e2) != 1 && (e1 != 0 || e2 != last) && (e1 != last || e2 != 0)) return 0;
    const MxE *A = &S->e[e1], *B = &R->s[sh2].e[e2];
    if (A->id == 'P' || B->id == 'P') return 0;
    P2 ta, tb;
    if (e2 - e1 == 1 || (e1 == last && e2 == 0)) { ta = mx_tan_end(A); tb = mx_tan_start(B); }
    else { tb = mx_tan_end(B); ta = mx_tan_start(A); }
    double la = sqrt(ta.y * ta.y + ta.x * ta.x);
    if (!(1e-12 <= la)) return 0;
    double lb = sqrt(tb.y * tb.y + tb.x * tb.x);
    if (!(1e-12 <= lb)) return 0;
    return cos(angDeg * 0.017453292519943295) <= (tb.y / lb) * (ta.y / la) + (tb.x / lb) * (ta.x / la);
}
/* FUN_1800c5df0 (f.106892-106985): the edge end (start or end) with the largest clearance; strict >, start first */
static int max_clear(const MxSL *G, const MaeL *M, double *s_out, double *rho_out) {
    double best = -1.0, bs = 0, a = 0, b = 0; int bi = -1;
    for (int i = 0; i < M->n; i++) {
        const Mae *r = &M->r[i];
        if ((r->type & ~2) == 0) { b = r->s1; a = 0; } else if (r->type == 1) { a = r->u0; b = r->u1; }
        double ra = mae_rad(G, r, a, NULL), rb = mae_rad(G, r, b, NULL);
        if (best < ra) { best = ra; bs = a; bi = i; }
        if (best < rb) { best = rb; bs = b; bi = i; }
    }
    if (s_out) *s_out = bs;
    if (rho_out) *rho_out = best;
    return bi;
}
/* FUN_1800c4100 (f.105978-105989) = FUN_1800c4530 + FUN_1800c3a90 + FUN_1800c4140 */
static void post_process(const MxSL *R, const MxSL *G, MaeL *out, double angDeg) {
    for (int i = out->n - 1; i >= 0; i--) {                                     /* FUN_1800c4530 (f.106122-106197) */
        Mae *m = &out->r[i];
        if (!(m->sh1 >= 0 && m->sh1 < G->n && m->en1 >= 0 && m->en1 < G->s[m->sh1].n)) continue;
        if (!(m->sh2 >= 0 && m->sh2 < G->n && m->en2 >= 0 && m->en2 < G->s[m->sh2].n)) continue;
        const MxE *a = G_ent(G, m->sh1, m->en1), *b = G_ent(G, m->sh2, m->en2);
        if (!a->f40 || !b->f40 || a->src_sh != b->src_sh) continue;
        if (a->src_ent == b->src_ent && R->s[a->src_sh].e[a->src_ent].id == 'A') m->flags |= 2;
        else if (smooth_joint(R, a->src_sh, a->src_ent, b->src_sh, b->src_ent, angDeg)) m->flags |= 2;
    }
    int k = 0;                                                                  /* FUN_1800c3a90 (f.105789-105832) */
    while (k < out->n && (out->r[k].flags & 2)) k++;
    if (out->n <= k && k != 0) {
        double s; int bi = max_clear(G, out, &s, NULL);
        if (bi >= 0 && bi < out->n) { mae_trim(G, &out->r[bi], s, s); out->r[bi].flags &= ~2u; }
    }
    for (int i = 0; i < out->n; i++) {                                          /* FUN_1800c43e0 (f.106076-106121) */
        Mae *m = &out->r[i]; if (m->flags & 2) continue;
        if (m->parent != -1 && (out->r[m->parent].flags & 2)) m->parent = -1;
        if (m->ch1 != -1 && (out->r[m->ch1].flags & 2)) m->ch1 = -1;
        if (m->ch2 != -1 && (out->r[m->ch2].flags & 2)) m->ch2 = -1;
    }
    int *map = malloc((size_t)(out->n > 0 ? out->n : 1) * sizeof *map), n = 0;              /* FUN_1800c3c10 (f.105833-105977) */
    for (int i = 0; i < out->n; i++) if (!(out->r[i].flags & 2)) { map[i] = n; out->r[n++] = out->r[i]; }
    out->n = n;
    for (int i = 0; i < n; i++) {
        Mae *m = &out->r[i];
        if (m->parent != -1) m->parent = map[m->parent];
        if (m->ch1 != -1) m->ch1 = map[m->ch1];
        if (m->ch2 != -1) m->ch2 = map[m->ch2];
    }
    free(map);
}
/* FUN_1800bef50 (f.103428-103648): one nest group (holes..., outer) */
static int build_group(const MxSL *R, const MxSL *G, double angDeg, MaeL *out) {
    MaeL W = { 0 }; EpdL E = { 0 }; out->n = 0;
    if (!seed(G, &W)) { free(W.r); return 0; }
    long guard = 0;
    while (W.n > 0) {
        if (mcce_abort || ++guard > 2000000) { free(W.r); free(E.r); return 0; }   /* ours: Cancel / runaway guard */
        Mae c = W.r[--W.n], e;                                                   /* LIFO */
        if (trace_edge(G, out, &c, &e, &E)) {
            mae_push(out, &e);
            int k = out->n - 1, p = out->r[k].parent;                            /* FUN_1800c03a0 */
            if (p >= 0) { if (out->r[k].flags & 1) out->r[p].ch1 = k; else out->r[p].ch2 = k; }
            spawn(G, out, &E, &W);
        }
    }
    free(W.r); free(E.r);
    post_process(R, G, out, angDeg);
    return 1;
}

/* ---------------------------------------------------------------------------------------------- preparation */
/* FUN_1800ab630 + ApxArcWithLnSegs @180001dd9 (f.93134-93184, f.3180-3248): chord points of an arc, start included */
static int arc_chords(double tol, const MxE *a, P2 **pts) {
    int n; double dth;
    if (1e-12 <= a->r) {
        if (tol <= a->r) { dth = acos(1.0 - tol / a->r) * 2.0; if (1e-12 <= dth) { if (1.5707963267948966 < dth) dth = 1.5707963267948966; } else dth = 1e-12; }
        else dth = 1.5707963267948966;
        double acc = 0.0;
        for (double x = fabs(a->sw); 0.0 < x; x += -3.141592653589793) acc += (3.141592653589793 < x ? 3.141592653589793 : x) / dth;
        n = (int)(acc + 1.0); if (n < 1) n = 1;
    } else n = 1;
    P2 *p = malloc((n + 1) * sizeof *p);
    p[0] = p2(cos(a->sa) * a->r + a->c.x, sin(a->sa) * a->r + a->c.y);
    double ang = a->sa;
    for (int k = 1; k <= n; k++) { ang = a->sw / (double)n + ang; p[k] = p2(cos(ang) * a->r + a->c.x, sin(ang) * a->r + a->c.y); }
    *pts = p; return n + 1;
}
static double mx_len(const MxE *e) { return e->id == 'L' ? sqrt((e->p2.y - e->p1.y) * (e->p2.y - e->p1.y) + (e->p2.x - e->p1.x) * (e->p2.x - e->p1.x)) : e->id == 'A' ? fabs(e->sw * e->r) : 0.0; }
static P2 mx_endpt(const MxE *e) { return e->id == 'L' ? e->p2 : e->id == 'A' ? p2(cos(e->sw + e->sa) * e->r + e->c.x, sin(e->sw + e->sa) * e->r + e->c.y) : e->p1; }
static P2 mx_startpt(const MxE *e) { return e->id == 'A' ? p2(cos(e->sa) * e->r + e->c.x, sin(e->sa) * e->r + e->c.y) : e->p1; }
/* FUN_18012c2b0 (f.161604-161721): drop an entity of length <= tol when the shape has > 2 entities and the previous end
   and the next start are within tol */
static void drop_tiny(MxS *s, double tol) {
    for (int i = 0; i < s->n; i++) {
        const MxE *e = &s->e[i];
        if (e->id == 'P') continue;
        if (!(mx_len(e) <= tol && 2 < s->n)) continue;
        P2 pe = mx_endpt(&s->e[i == 0 ? s->n - 1 : i - 1]);
        P2 ns = mx_startpt(&s->e[i < s->n - 1 ? i + 1 : 0]);
        if ((ns.y - pe.y) * (ns.y - pe.y) + (ns.x - pe.x) * (ns.x - pe.x) <= tol * tol) { memmove(s->e + i, s->e + i + 1, (s->n - i - 1) * sizeof *s->e); s->n--; i--; }
    }
}
/* FUN_1800c09d0 (f.104311-104425) for one shape */
static void prep_shape(const MxS *src, int idx, double arcTol, MxS *out) {
    MxS s = { 0 }; s.dir = src->dir; s.hole = src->hole;
    for (int j = 0; j < src->n; j++) { MxE e = src->e[j]; e.f40 = 1; e.src_sh = idx; e.src_ent = j; mx_push(&s, e); }
    drop_tiny(&s, 1e-7);
    /* FUN_18012c740(S, 1e-7, 1e-7) (f.161722-161768, medaxgaps E): on a chain whose entity ends meet within 1e-7 it only
       recomputes the bbox; its gap repair (trim / snap / bridge) is not ported - the shapes here come closed from the region
       code (gap 0) */
    MxS c = { 0 }; c.dir = s.dir; c.hole = s.hole;                              /* FUN_180126100 arcs -> chords */
    for (int j = 0; j < s.n; j++) {
        if (s.e[j].id != 'A') { mx_push(&c, s.e[j]); continue; }
        P2 *p; int np = arc_chords(arcTol, &s.e[j], &p);
        for (int k = 1; k < np; k++) { MxE l = s.e[j]; l.id = 'L'; l.p1 = p[k - 1]; l.p2 = p[k]; mx_push(&c, l); }
        free(p);
    }
    mx_free(&s);
    MxS w = { 0 }; w.dir = c.dir; w.hole = c.hole;                              /* FUN_1800c0d70 reflex points (f.104440-104579) */
    int prev;
    {
        MxS q = { 0 }; for (int i = 0; i < c.n; i++) if (c.e[i].id != 'P') mx_push(&q, c.e[i]);
        prev = q.n - 1;
        for (int i = 0; i < q.n; i++) {
            const MxE *p = &q.e[prev], *cu = &q.e[i]; P2 P2e = p->p2;
            double cr = (cu->p2.y - p->p1.y) * (P2e.x - p->p1.x) - (P2e.y - p->p1.y) * (cu->p2.x - p->p1.x);
            int turn = cr > 0.0 ? 1 : cr < 0.0 ? -1 : 0;
            if (turn != 0 && ((!c.hole && c.dir != turn) || (c.hole && c.dir == turn))) {
                MxE pt; memset(&pt, 0, sizeof pt); pt.id = 'P'; pt.p1 = P2e;   /* +0x12 / +0x60 / +0x80 zeroed (FUN_180027160 memset, medaxgaps D) */
                mx_push(&w, pt);
            }
            mx_push(&w, *cu);
            prev = i;
        }
        mx_free(&q);
    }
    mx_free(&c);
    if ((w.hole && w.dir == -1) || (!w.hole && w.dir == 1)) {                  /* FUN_1800c0d40: RevSh */
        for (int i = 0, j = w.n - 1; i <= j; i++, j--) {
            MxE a = w.e[i], b = w.e[j];
            if (a.id == 'L') { P2 t = a.p1; a.p1 = a.p2; a.p2 = t; }
            if (b.id == 'L') { P2 t = b.p1; b.p1 = b.p2; b.p2 = t; }
            w.e[i] = b; w.e[j] = a;
        }
        w.dir = -w.dir;
    }
    *out = w;
}

/* mcce::CalcMedAx (f.10590-10863): R = nest groups "holes..., outer"; S = the prepared copies; M = the medial axis.
   Returns 0 when the seed throws (no convex corner) or on Cancel. */
static int calc_med_ax(const MxSL *R, MxSL *S, MaeL *M, double arcTol, double angDeg) {
    msl_free(S); M->n = 0;
    for (int i = 0; i < R->n; i++) { MxS s; prep_shape(&R->s[i], i, arcTol, &s); msl_push(S, s); }
    int i = 0;
    while (i < S->n) {
        int base = i;
        while (i < S->n && S->s[i].hole) i++;
        if (i >= S->n) break;                                                    /* (no outer left) */
        MxSL G = { S->s + base, i - base + 1, 0 };
        MaeL MG = { 0 };
        if (!build_group(R, &G, angDeg, &MG)) { free(MG.r); return 0; }
        int n0 = M->n;
        for (int k = 0; k < MG.n; k++) {
            Mae e = MG.r[k];
            e.sh1 += base; e.sh2 += base;
            if (e.parent != -1) e.parent += n0;
            if (e.ch1 != -1) e.ch1 += n0;
            if (e.ch2 != -1) e.ch2 += n0;
            mae_push(M, &e);
        }
        free(MG.r);
        i++;
    }
    return 1;
}
