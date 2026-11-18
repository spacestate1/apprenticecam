/* mcpartline_trim.inc - make_surf_polygons on a trimmed surface (kind 10 over a kind 0 base): the trim-loop and cell-clipping engine of
 * FUN_18057a980 (mccore 0x18057a980). Included at the end of mcpartline_mesh.c (it uses that file's static grid / evaluation code).
 * Spec: ../formats/interfacial.md 12.4 / 12.8; decompiles decomp/partline/core_tess.c and decomp/interfacial/msp_trim.c; the arguments Ghidra
 * loses and the floating-point order are the disassembly's (objdump of mccore.dll, cited by address).
 * Oracle: tools/peloader --stl (MESH on kind 10 records, tools/stl_case.py --kinds 10); the loop stages can be dumped from the DLL with
 * STL_HOOKS (tools/peloader/mccore_stl.c). */

typedef struct {
    double *p; int np, cap;          /* uv points (u, v pairs) */
    int *tb; int nl;                 /* loop table: start, count (points start .. start + count, closed back to start) */
    double *ub, *vb;                 /* per-loop u box (lo, hi) and v box (lo, hi) */
    unsigned char *ccw;              /* per loop: is_bdy_ccw (inverted for the islands) */
    int outer;                       /* the outer loop (FUN_180586880: the one holding the smallest u) */
    int nl1;                         /* FUN_180591650 param_14: loops - 1, 0 when the islands were dropped */
} PltTrim;

/* development aid: PLT_DUMP=1 writes the loops at a stage to stderr in the format of tools/peloader --stl's STL_HOOKS 'L' dumps
   (4 = after FUN_180591650's FUN_180598070, 0 = the final loops the cells are cut by) */
static void plt_dump(const char *id, const PltTrim *T)
{
    if (!getenv("PLT_DUMP")) return;
    fprintf(stderr, "LOOPS %s %d\n", id, T->nl);
    for (int l = 0; l < T->nl; l++) {
        fprintf(stderr, "L %d %d\n", T->tb[2 * l], T->tb[2 * l + 1]);
        for (int k = T->tb[2 * l]; k <= T->tb[2 * l] + T->tb[2 * l + 1]; k++) fprintf(stderr, "%a %a\n", T->p[2 * (size_t)k], T->p[2 * (size_t)k + 1]);
    }
}

static void plt_free(PltTrim *T) { free(T->p); free(T->tb); free(T->ub); free(T->vb); free(T->ccw); memset(T, 0, sizeof *T); }

#define PX(P, i) ((P)[2 * (size_t)(i)])
#define PY(P, i) ((P)[2 * (size_t)(i) + 1])

/* eval_surf(trimmed or base, 0, u, v, out, 0, 1) of a kind 10 surface: the base's (eval_surf case 10, msp_trim.c l.2041) -> FUN_1805b08d0 */
static int plt_pt(Ctx *c, double u, double v, double P[3])
{
    double D[10][3];
    if (c->err) return -1;
    if (plm_eval(c->f, c->hdr ? c->hdr : c->s, 0, u, v, D)) { c->err = "trimmed surface mesh: eval_surf of the base failed"; return -1; }   /* make_surf_model: the kind 10 entity (its base may be an offset surface) */
    P[0] = D[0][0]; P[1] = D[0][1]; P[2] = D[0][2];
    return 0;
}

/* in_polygon (0x18046c960, core_tess.c l.4616): 1 inside, -1 outside, 0 on a loop (1e-10) */
static int in_polygon(int nloops, const double *p, const int *tb, const double q[2])
{
    int in = 0;
    double y3 = q[1] - 1e-10, x4 = q[0] - 1e-10, x5 = q[0] + 1e-10;
    for (int l = 0; l < nloops; l++) {
        long a = tb[2 * l], e = (long)tb[2 * l + 1] + tb[2 * l];
        double px = PX(p, e), py = PY(p, e);
        for (long k = a; k <= e; k++) {
            double y1 = PY(p, k), dy = y1 - py;
            if (1e-10 <= dy || 1e-10 <= -dy) {
                if ((py < y3 && y3 <= y1) || (y1 <= y3 && y3 < py)) {
                    double x = ((q[1] - py) * (PX(p, k) - px)) / dy + px;
                    double d = q[0] - x;
                    if (d < 1e-10 && -d < 1e-10) return 0;
                    if (q[0] < x) in ^= 1;
                }
            } else {
                double d = py - q[1];
                if (d < 1e-10 && -d < 1e-10 && ((x4 <= px && PX(p, k) <= x5) || (px <= x5 && x4 <= PX(p, k)))) return 0;
            }
            px = PX(p, k); py = PY(p, k);
        }
    }
    return in ? 1 : -1;
}

/* is_bdy_ccw (0x18058b3b0, core_tess.c l.8030) */
static int is_bdy_ccw(const int *tb, const double *p, int l)
{
    int s = tb[2 * l], e = s + tb[2 * l + 1];
    int m = s;
    for (int k = s; k <= e; k++) if (PY(p, k) < PY(p, m)) m = k;          /* the lowest point (first of equals) */
    int pv = m - 1; if (pv < s) pv = e;
    if (pv == m) return 1;
    double x0 = PX(p, m), y0 = PY(p, m);
    while (fabs(PX(p, pv) - x0) <= 1e-10 && PY(p, pv) - y0 <= 1e-10) {    /* the previous distinct point */
        pv--; if (pv < s) pv = e;
        if (pv == m) return 1;
    }
    if (pv < 0) return 1;
    int nx = pv;
    if (m != pv) {                                                          /* the next distinct point */
        int k = m;
        for (;;) {
            if (1e-10 < fabs(PX(p, k) - x0) || 1e-10 < PY(p, k) - y0) { nx = k; break; }
            k++; if (k > e) k = s;
            if (k == pv) { nx = pv; break; }
        }
    }
    if (pv == nx) return 1;
    double a = PX(p, pv) - x0, b = PY(p, pv) - y0, l1 = sqrt(a * a + b * b), ca = l1 == 0.0 ? 0.0 : a / l1;
    double c2 = PX(p, nx) - x0, d2 = PY(p, nx) - y0, l2 = sqrt(c2 * c2 + d2 * d2), cb = l2 != 0.0 ? c2 / l2 : 0.0;
    return fabs(ca - cb) <= 1e-10 || ca < cb;
}

/* FUN_180592ce0 (msp_trim.c l.2058): point i on the segment j-k (1e-10, along the longer axis) */
static int pt_on_seg(const double *p, int i, int j, int k)
{
    double x1 = PX(p, j), y1 = PY(p, j), x3 = PX(p, k), y4 = PY(p, k), dx = x3 - x1, dy = y4 - y1;
    if (fabs(dx) < fabs(dy) || fabs(dx) == fabs(dy)) {
        double y = PY(p, i);
        if ((y <= y1 && y4 <= y) || (y1 <= y && y <= y4)) {
            if (fabs(dy) <= 1e-10 && fabs(dy) != 1e-10) return 0;
            return fabs(PX(p, i) - (((y - y1) / dy) * dx + x1)) < 1e-10;
        }
    } else {
        double x = PX(p, i);
        if ((x <= x1 && x3 <= x) || (x1 <= x && x <= x3)) {
            if (fabs(dx) <= 1e-10 && fabs(dx) != 1e-10) return 0;
            return fabs(PY(p, i) - (((x - x1) / dx) * dy + y1)) < 1e-10;
        }
    }
    return 0;
}

/* FUN_18058a7c0 (disassembly 18058a7c0..18058a94b): segment i-j against k-l; t along k -> l of the crossing, 0 < t < 1 */
static int seg_cross(const double *p, int i, int j, int k, int l, double *t)
{
    double ax = PX(p, i), ay = PY(p, i), kx = PX(p, k), ky = PY(p, k);
    double dklx = kx - PX(p, l), ex = PX(p, j) - ax, fx = kx - ax;
    double ey = PY(p, j) - ay, dkly = ky - PY(p, l), fy = ky - ay;
    double D = dkly * ex - dklx * ey;
    double dot = dkly * ey + dklx * ex;
    double s5 = fx * dkly - fy * dklx, r;
    *t = -1.0;
    if (1e-10 > fabs(dot) || (4e-14 > fabs(dot) && 4e-14 > fabs(D)) || 2e-07 > fabs(D / dot) || 1e-10 > fabs(D)) r = -1.0;   /* DAT_1812f4868 = 4e-14 */
    else {
        double s = s5 / D;
        if (0.0 > s || s > 1.0) r = -1.0;
        else r = (fy * ex - fx * ey) / D;
    }
    if (r > 0.0 && 1.0 > r) { *t = r; return 1; }
    return 0;
}

/* FUN_180579660 (msp_trim.c l.767): a within tol of c, or a on the segment b-c (strictly between, perpendicular distance < tol) */
static int on_seg_tol(const double *a, const double *b, const double *c, double tol)
{
    if (!(tol < fabs(a[0] - c[0])) && !(tol < fabs(a[1] - c[1]))) return 1;
    double dx = c[0] - b[0], dy = c[1] - b[1], l = sqrt(dx * dx + dy * dy);
    double ax = a[0] - b[0], ay = a[1] - b[1];
    double t = ax * (dx / l) + ay * (dy / l);
    if (t < l && 0.0 < t) { double d = ax * -(dy / l) + ay * (dx / l); return fabs(d) < tol; }
    return 0;
}

/* FUN_180577e10 (disassembly 180577e10..1805782e5): per point a of loop l and the next b, c, d: d == a, d on a-b, a on c-d or a-b crossing
   c-d, and the 3D points b, c and that point M within tol of each other (one distance below tol): b and c replaced by M, *ok = 0 */
static int fix_spikes(Ctx *c, double *p, const int *tb, int l, double tol, int *ok)
{
    *ok = 1;
    int cnt = tb[2 * l + 1];
    if (cnt <= 2) return 0;
    int st = tb[2 * l], a = st;
    for (int it = 0; it <= tb[2 * l + 1]; it++, a++) {
        int end = tb[2 * l + 1] + st;
        int b = a == end ? st : a + 1, cc = b == end ? st : b + 1, d = cc == end ? st : cc + 1;
        double M[2];
        double dx = PX(p, d), ax = PX(p, a);
        if (!(fabs(dx - ax) > 1e-10) && !(fabs(PY(p, d) - PY(p, a)) > 1e-10)) { M[0] = (ax + dx) * 0.5; M[1] = (PY(p, d) + PY(p, a)) * 0.5; }   /* 180577f71 */
        else if (pt_on_seg(p, d, a, b)) { M[0] = dx; M[1] = PY(p, d); }
        else if (pt_on_seg(p, a, cc, d)) { M[0] = ax; M[1] = PY(p, a); }
        else {
            double t;
            if (!seg_cross(p, a, b, cc, d, &t)) continue;   /* 180578056: edx = a (left by FUN_180592ce0) */
            M[0] = (PX(p, d) - PX(p, cc)) * t + PX(p, cc); M[1] = (PY(p, d) - PY(p, cc)) * t + PY(p, cc);
        }
        double B[3], C[3], Q[3];
        if (plt_pt(c, PX(p, b), PY(p, b), B) || plt_pt(c, PX(p, cc), PY(p, cc), C) || plt_pt(c, M[0], M[1], Q)) return -1;
        double d1 = sqrt(((C[1] - B[1]) * (C[1] - B[1]) + (C[0] - B[0]) * (C[0] - B[0])) + (C[2] - B[2]) * (C[2] - B[2]));   /* 18057816f */
        int rep = tol > d1;
        if (!rep) {
            double d2 = sqrt(((Q[0] - B[0]) * (Q[0] - B[0]) + (Q[1] - B[1]) * (Q[1] - B[1])) + (Q[2] - B[2]) * (Q[2] - B[2]));
            rep = tol > d2;
            if (!rep) {
                double d3 = sqrt(((Q[0] - C[0]) * (Q[0] - C[0]) + (Q[1] - C[1]) * (Q[1] - C[1])) + (Q[2] - C[2]) * (Q[2] - C[2]));
                rep = tol > d3;
            }
        }
        if (rep) { PX(p, b) = M[0]; PY(p, b) = M[1]; PX(p, cc) = M[0]; PY(p, cc) = M[1]; *ok = 0; return 0; }
    }
    return 0;
}

/* FUN_1805776f0 (msp_trim.c l.28): loop l crossing itself - the segment k, k+1 (k from start + 2) against the earlier ones not next to it.
   The DLL skips runs of segments outside the segment's box (1e-10); the tests on the segments it does compare are the same. 1 = found */
static int self_cross(const double *p, const int *tb, int l)
{
    int st = tb[2 * l], cnt = tb[2 * l + 1];
    if (cnt <= 2) return 0;
    for (int ld = 2, i14 = st + 2; ld <= cnt; ld++, i14++) {
        int i15, first;
        if (ld == cnt) { i15 = st; first = st + 1; } else { i15 = i14 + 1; first = st; }
        for (int i11 = first; i11 + 1 < i14; i11++) {
            int i9 = i11 + 1;
            if (!(fabs(PX(p, i15) - PX(p, i11)) > 1e-10) && !(fabs(PY(p, i15) - PY(p, i11)) > 1e-10)) return 1;
            if (pt_on_seg(p, i15, i11, i9)) return 1;
            if (pt_on_seg(p, i9, i14, i15)) return 1;
            double t; if (seg_cross(p, i14, i15, i11, i9, &t)) return 1;
        }
    }
    return 0;
}

/* FUN_180578340 (msp_trim.c l.485): loop l against the loops before it. 1 = found */
static int cross_loops(const double *p, const int *tb, int l)
{
    int cnt = tb[2 * l + 1];
    if (cnt < 0) return 0;
    int i8 = tb[2 * l], i14 = i8 + 1;
    for (int d4 = 0; d4 <= cnt; d4++) {
        if (d4 > 0) { i8 = i14; i14 = (d4 == cnt) ? tb[2 * l] : i14 + 1; }
        for (int o = 0; o < l; o++) {
            int os = tb[2 * o], oc = tb[2 * o + 1];
            for (int k = 0, i12 = os; k <= oc; k++) {
                int i7 = (k == oc) ? os : i12 + 1;
                if (!(fabs(PX(p, i8) - PX(p, i12)) > 1e-10) && !(fabs(PY(p, i8) - PY(p, i12)) > 1e-10)) return 1;
                if (pt_on_seg(p, i8, i12, i7)) return 1;
                double t; if (seg_cross(p, i8, i14, i12, i7, &t) && !pt_on_seg(p, i14, i12, i7)) return 1;
                i12 = i7;
            }
        }
    }
    return 0;
}

/* FUN_180598070 (core_tess.c l.6211) with param_6 = 0 (no drawing): each loop's closing duplicate, repeated points and spikes removed (a linked
   list over the loop, FUN_180579660 at 1e-10), the loops compacted, those of fewer than 3 points dropped; FUN_180577e10 on every loop (a
   change restarts it all); then per loop FUN_1805776f0 (itself) and FUN_180578340 (the loops before). Returns 1 ok, 0 a crossing found,
   -1 not ported. close (param_17) is 0 on both calls here. */
static int clean_loops(Ctx *c, double *p, int *tb, int *nl, double tol)
{
    int *nx = NULL; int nxcap = 0;
    for (;;) {
        for (int l = 0; l < *nl; l++) {
            int *e = tb + 2 * l, st = e[0];
            if (0 < e[1] && fabs(PX(p, st) - PX(p, st + e[1])) <= 1e-10 && fabs(PY(p, st) - PY(p, st + e[1])) <= 1e-10) e[1]--;
            int head;
            if (e[1] < 2) head = -1;
            else {
                if (e[1] + 1 > nxcap) { nxcap = e[1] + 1; nx = realloc(nx, sizeof(int) * nxcap); }
                for (int k = 0; k < e[1]; k++) nx[k] = k + 1;
                nx[e[1]] = 0;
                int cur = 0; head = 0;
                do {
                    for (;;) {
                        int b = nx[cur], cc = nx[b];
                        const double *Pb = p + 2 * (size_t)(b + st), *Pc = p + 2 * (size_t)(cc + st);
                        int rm = 0;
                        if (!(1e-10 < fabs(Pc[0] - Pb[0])) && !(1e-10 < fabs(Pc[1] - Pb[1]))) rm = 1;
                        else {
                            const double *Pd = p + 2 * (size_t)(nx[cc] + st);
                            if (fabs(Pc[0] - Pd[0]) <= 1e-10 && fabs(Pc[1] - Pd[1]) <= 1e-10) rm = 1;
                            else if (on_seg_tol(Pb, Pc, Pd, 1e-10) || on_seg_tol(Pd, Pc, Pb, 1e-10)) rm = 1;
                        }
                        if (!rm) break;
                        nx[b] = nx[cc]; nx[cc] = -1;                               /* LAB_18059830a */
                        head = cur;
                        if (cur == nx[nx[cur]]) { head = -1; goto done; }
                    }
                    cur = nx[cur];
                } while (cur != head);
            }
        done:;
            int old = e[0];
            e[0] = l == 0 ? 0 : e[-2] + 1 + e[-1];
            e[1] = 0;
            if (head != -1) {
                int mn = head, k = head;
                do { if (k < mn) mn = k; k = nx[k]; } while (k != head);
                int w = mn, last = 0;
                do {
                    PX(p, e[1] + e[0]) = PX(p, w + old); PY(p, e[1] + e[0]) = PY(p, w + old);
                    last = e[1]; e[1]++;
                    w = nx[w];
                } while (w != mn);
                e[1] = last;
            }
        }
        int n = 0;
        for (int l = 0; l < *nl; l++) if (1 < tb[2 * l + 1]) { tb[2 * n] = tb[2 * l]; tb[2 * n + 1] = tb[2 * l + 1]; n++; }
        *nl = n;
        int again = 0;
        for (int l = 0; l < *nl; l++) {
            int ok; if (fix_spikes(c, p, tb, l, tol, &ok)) { free(nx); return -1; }
            if (!ok) { again = 1; break; }
        }
        if (!again) break;
    }
    free(nx);
    for (int l = 0; l < *nl; l++) {
        if (self_cross(p, tb, l)) return 0;
        if (l > 0 && cross_loops(p, tb, l)) return 0;
    }
    return 1;
}

/* FUN_180586880 (core_tess.c l.7949): the loop holding the smallest u (first of equals) */
static int outer_loop(const int *tb, const double *p, int nl)
{
    int best = -1; double mn = 0.0;
    for (int l = 0; l < nl; l++)
        for (int k = tb[2 * l]; k <= tb[2 * l] + tb[2 * l + 1]; k++)
            if (best == -1 || PX(p, k) < mn) { mn = PX(p, k); best = l; }
    return best;
}

/* FUN_180591650 (core_tess.c l.2105) on a kind 10 surface, param_1 = tol (make_surf_polygons' step x 0.1): the loops copied (record +0x138: m
   (u, v), then per loop start, count) packed, each point clamped to the base's +0x70 range; FUN_180598070; on failure the repair FUN_180586a50
   (up to 30 tries) is NOT PORTED. Then the outer loop, the per-loop boxes, the islands outside the outer loop (any point: in_polygon -1) ->
   only the outer loop kept, the is_bdy_ccw flags. Returns 1 ok, 0 no loop left (make_surf_polygons: no triangles), -1 not ported. */
static int plt_read(Ctx *c, const Surf *s10, double tol, PltTrim *T, const char **why)
{
    memset(T, 0, sizeof *T);
    const uint8_t *r = s10->rec, *br = c->s->rec;
    if (s10->n < 0x138) { *why = "trimmed surface mesh: short kind 10 record"; return -1; }
    int m = rd32(r + 0x10c), nl = rd32(r + 0x128);
    if (m < 0 || nl < 0 || 0x138 + 16 * (size_t)m + 8 * (size_t)nl > s10->n) { *why = "trimmed surface mesh: bad trim data"; return -1; }
    if (nl == 0) { *why = "trimmed surface mesh: no trim loop (FUN_180591650 edge_alloc(0) path) NOT PORTED"; return -1; }
    const uint8_t *pt = r + 0x138, *lt = pt + 16 * (size_t)m;
    int tot = 0;
    T->tb = malloc(sizeof(int) * 2 * (size_t)nl);
    for (int l = 0; l < nl; l++) {
        int ost = rd32(lt + 8 * l), cnt = rd32(lt + 8 * l + 4);
        if (cnt < -1 || ost < 0 || (size_t)ost + cnt + 1 > (size_t)m) { *why = "trimmed surface mesh: loop outside the points"; return -1; }
        T->tb[2 * l] = tot; T->tb[2 * l + 1] = cnt; tot += 1 + cnt;
    }
    T->cap = tot + 0x3c; T->p = malloc(sizeof(double) * 2 * (size_t)T->cap);
    double U0 = rdd(br + 0x70), U1 = rdd(br + 0x78), V0 = rdd(br + 0x80), V1 = rdd(br + 0x88);
    for (int l = 0; l < nl; l++) {
        int ost = rd32(lt + 8 * l), cnt = rd32(lt + 8 * l + 4);
        for (int k = 0; k <= cnt; k++) {
            double u = rdd(pt + 16 * (size_t)(ost + k)), v = rdd(pt + 16 * (size_t)(ost + k) + 8);
            if (!(u <= U1)) u = U1; else if (u < U0) u = U0;                  /* 180591853: the base's range (the overrun only reported) */
            if (!(v <= V1)) v = V1; else if (!(V0 <= v)) v = V0;
            PX(T->p, T->tb[2 * l] + k) = u; PY(T->p, T->tb[2 * l] + k) = v;
        }
    }
    T->np = tot; T->nl = nl;
    int ok = clean_loops(c, T->p, T->tb, &T->nl, tol);
    if (ok < 0) { *why = c->err; return -1; }
    if (!ok) { *why = "trimmed surface mesh: trim loops cross (FUN_180591650 -> repair FUN_180586a50) NOT PORTED"; return -1; }
    if (T->nl < 1) return 0;                                                  /* 180591f24 with local_88 = 1 */
    nl = T->nl;
    int ol = outer_loop(T->tb, T->p, nl);
    T->ub = malloc(sizeof(double) * 2 * nl); T->vb = malloc(sizeof(double) * 2 * nl); T->ccw = malloc((size_t)nl);
    int isl_out = 0;
    for (int l = 0; l < nl; l++) {
        double *ub = T->ub + 2 * l, *vb = T->vb + 2 * l;
        ub[0] = 1e38; ub[1] = -1e38; vb[0] = 1e38; vb[1] = -1e38;
        for (int k = T->tb[2 * l]; k <= T->tb[2 * l] + T->tb[2 * l + 1]; k++) {
            double u = PX(T->p, k), v = PY(T->p, k);
            if (u < ub[0]) ub[0] = u;
            if (u > ub[1]) ub[1] = u;
            if (v < vb[0]) vb[0] = v;
            if (v > vb[1]) vb[1] = v;
            if (l != ol && in_polygon(1, T->p, T->tb + 2 * ol, T->p + 2 * (size_t)k) == -1) isl_out = 1;
        }
        int w = is_bdy_ccw(T->tb, T->p, l);
        T->ccw[l] = (unsigned char)(l != ol ? !w : w);
    }
    T->outer = ol; T->nl1 = nl - 1;
    if (isl_out) {                                                            /* "ISLAND SLIPS OUTSIDE OUTER BOUNDARY": the outer loop only */
        T->ub[0] = T->ub[2 * ol]; T->ub[1] = T->ub[2 * ol + 1]; T->vb[0] = T->vb[2 * ol]; T->vb[1] = T->vb[2 * ol + 1];
        T->tb[0] = T->tb[2 * ol]; T->tb[1] = T->tb[2 * ol + 1]; T->ccw[0] = T->ccw[ol];
        T->nl = 1; T->nl1 = 0; T->outer = 0;
    }
    return 1;
}
