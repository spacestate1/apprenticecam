/* mcpartline_trim2.inc - make_surf_polygons on a trimmed surface, part 2: the loops after the grid (FUN_18057a980 18057bcea..18057c1a6).
 * Included from mcpartline_trim.inc's user, mcpartline_mesh.c (see mcpartline_trim.inc). */

/* FUN_1805937e0 (core_tess.c l.3352, disassembly 1805937e0): the grid parameters cut to the trim box (1e-12) */
static void cut_params(double u0, double u1, double v0, double v1, int *nu, int *nv, double *pu, double *pv)
{
    while (1 < *nu && pu[*nu - 1] > u1 + 1e-12) (*nu)--;
    while (1 < *nv && pv[*nv - 1] > v1 + 1e-12) (*nv)--;
    if (1 < *nu) {
        int k = 0; while (u0 - 1e-12 > pu[k + 1]) k++;
        if (0 < k) { *nu -= k; if (0 <= *nu) for (int i = 0; i <= *nu; i++) pu[i] = pu[i + k]; }
    }
    if (1 < *nv) {
        int k = 0; while (v0 - 1e-12 > pv[k + 1]) k++;
        if (0 < k) { *nv -= k; if (0 <= *nv) for (int i = 0; i <= *nv; i++) pv[i] = pv[i + k]; }
    }
}

/* FUN_180599630 (disassembly 180599630): x within 2e-10 of either end of the box of another loop */
static int on_box_end(double x, const double *box, int skip, int n)
{
    for (int k = 0; k < n; k++) if (k != skip && (2e-10 > fabs(x - box[2 * k]) || 2e-10 > fabs(x - box[2 * k + 1]))) return 1;
    return 0;
}
/* the insertion of FUN_180577130: x into the sorted p[0..*n] unless within 2e-10 of one */
static void ins_param(double x, double *p, int *n)
{
    if (-1 < *n) { double md = 1e38; for (int k = 0; k <= *n; k++) if (fabs(x - p[k]) < md) md = fabs(x - p[k]); if (md <= 2e-10) return; }
    int k = *n;
    while (p[k] > x) { p[k + 1] = p[k]; k--; }
    p[k + 1] = x;
    (*n)++;
}
/* FUN_180577130 (core_tess.c l.1931, disassembly 180577130..1805775e6): an island no grid line crosses gets one through its middle (along
   its longer box side), moved in 2 % steps off the box ends of the other loops */
static void island_lines(const PltTrim *T, int *nu, int *nv, double *pu, double *pv)
{
    for (int l = 0; l < T->nl; l++) {
        if (l == T->outer) continue;
        const double *ub = T->ub + 2 * l, *vb = T->vb + 2 * l;
        int hit = 0;
        for (int k = 0; k <= *nu && !hit; k++) if (ub[0] + 1e-10 < pu[k] && pu[k] < ub[1] - 1e-10) hit = 1;
        for (int k = 0; k <= *nv && !hit; k++) if (vb[0] + 1e-10 < pv[k] && pv[k] < vb[1] - 1e-10) hit = 1;
        if (hit) continue;
        double eu = ub[1] - ub[0], ev = vb[1] - vb[0];
        int alongu = eu > ev;                                                  /* 1805772c5 */
        const double *b = alongu ? ub : vb, *boxes = alongu ? T->ub : T->vb;
        double lo = b[0], hi = b[1], c = (lo + hi) * 0.5;
        if (on_box_end(c, boxes, l, T->nl)) {
            double st = (alongu ? eu : ev) * 0.02 + 1e-10, x = st + c;
            while (hi - 1e-10 > x && on_box_end(x, boxes, l, T->nl)) x = x + st;
            if (hi - 1e-10 > x) c = x;
            else {
                x = c - st;
                while (x > lo + 1e-10 && on_box_end(x, boxes, l, T->nl)) x = x - st;
                if (x > lo + 1e-10) c = x;
            }
        }
        if (alongu) ins_param(c, pu, nu); else ins_param(c, pv, nv);
    }
}

/* FUN_180598d40 (core_tess.c l.2773): a point onto the grid parameters within 1e-10 */
static void snap_pt(double *P, const double *pu, const double *pv, int nu, int nv)
{
    for (int k = 0; k <= nu; k++) if (fabs(pu[k] - P[0]) <= 1e-10) P[0] = pu[k];
    for (int k = 0; k <= nv; k++) if (fabs(pv[k] - P[1]) <= 1e-10) P[1] = pv[k];
}

/* FUN_18059bb90 (msp_trim.c l.1806): from P towards Q the first grid line crossed (strictly between); P moved there. 0 = none */
static int next_cross(double *P, const double *Q, const double *pu, const double *pv, int nu, int nv)
{
    double gu = 0.0, gv = 0.0, tu = 0.0, tv = 0.0; int bu = 0, bv = 0;
    double qu = Q[0], p0 = P[0];
    if (qu > p0) {
        gu = qu + 1.0; for (int k = nu; k >= 0; k--) if (p0 < pu[k]) gu = pu[k];
        if (qu > gu) { bu = 1; tu = (gu - p0) / (qu - p0); }
    } else if (qu < p0) {
        gu = qu - 1.0; for (int k = 0; k <= nu; k++) if (pu[k] < p0) gu = pu[k];
        if (gu > qu) { bu = 1; tu = (gu - p0) / (qu - p0); }
    }
    double p1 = P[1], qv = Q[1];
    if (qv > p1) {
        gv = qv + 1.0; for (int k = nv; k >= 0; k--) if (p1 < pv[k]) gv = pv[k];
        if (qv > gv) { bv = 1; tv = (gv - p1) / (qv - p1); }
    } else if (qv < p1) {
        gv = qv - 1.0; for (int k = 0; k <= nv; k++) if (pv[k] < p1) gv = pv[k];
        if (gv > qv) { bv = 1; tv = (gv - p1) / (qv - p1); }
    }
    if (!bu || (bv && tv <= tu)) {
        if (!bv) return 0;
        P[0] = (qu - p0) * tv + p0; P[1] = gv;
    } else {
        P[0] = gu; P[1] = (qv - p1) * tu + p1;
    }
    return 1;
}

/* FUN_180576e90 (core_tess.c l.5976): the loops walked counter-clockwise (an is_bdy_ccw 0 loop backwards), each point snapped to the grid and
   followed by the grid crossings on the way to the next one: the new loops (points op, table otb) */
static void add_crossings(const PltTrim *T, const double *pu, const double *pv, int nu, int nv, double **op, int *onp, int **otb)
{
    int cap = 64, n = 0;
    double *o = malloc(sizeof(double) * 2 * cap);
    int *tb = malloc(sizeof(int) * 2 * (T->nl > 0 ? T->nl : 1));
    for (int l = 0; l < T->nl; l++) {
        tb[2 * l] = n; tb[2 * l + 1] = -1;
        int st = T->tb[2 * l], cnt = T->tb[2 * l + 1], dir = T->ccw[l] ? 1 : -1, first = T->ccw[l] ? st : st + cnt, cur = first;
        #define PUSH(P) do { if (n == cap) { cap *= 2; o = realloc(o, sizeof(double) * 2 * cap); } o[2 * n] = (P)[0]; o[2 * n + 1] = (P)[1]; n++; tb[2 * l + 1]++; } while (0)
        for (int it = 0; it <= cnt; it++) {
            int nx = it == cnt ? first : cur + dir;
            double P[2] = { PX(T->p, cur), PY(T->p, cur) }, Q[2] = { PX(T->p, nx), PY(T->p, nx) };
            snap_pt(P, pu, pv, nu, nv);
            PUSH(P);
            snap_pt(Q, pu, pv, nu, nv);
            while (next_cross(P, Q, pu, pv, nu, nv)) PUSH(P);
            cur = nx;
        }
        #undef PUSH
    }
    *op = o; *onp = n; *otb = tb;
}

/* FUN_180598030 (disassembly 180598030): a and b in one parameter interval (closed) */
static int same_interval(int n, const double *p, double a, double b)
{
    for (int k = 0; k < n; k++) if (a >= p[k] && p[k + 1] >= a && b >= p[k] && p[k + 1] >= b) return 1;
    return 0;
}
/* FUN_1805812e0 (core_tess.c l.10452, disassembly 1805812e0..1805813ed): a corner b of a, b, c on one grid line whose a and c lie in one
   interval of it: b = a (in place, cyclic over start .. end) */
static void flatten_corners(int s, int e, double *p, int nu, int nv, const double *pu, const double *pv)
{
    int a = s, b = s + 1;
    for (;;) {
        int c = b + 1 > e ? s : b + 1;
        double au = PX(p, a), bu = PX(p, b);
        if (au == bu && PY(p, a) != PY(p, b)) {
            if (bu == PX(p, c) && PY(p, b) != PY(p, c) && same_interval(nv, pv, PY(p, a), PY(p, c))) { PX(p, b) = PX(p, a); PY(p, b) = PY(p, a); }
        } else if (PY(p, a) == PY(p, b) && au != bu && PY(p, b) == PY(p, c) && bu != PX(p, c)) {
            if (same_interval(nu, pu, au, PX(p, c))) { PX(p, b) = PX(p, a); PY(p, b) = PY(p, a); }
        }
        if (a == e) return;
        a = b; b = c;
    }
}

/* FUN_18058b700 (msp_trim.c l.1224): the grid cell of a point; 1 when it lies on a grid line of the intervals looked at */
static int cell_of(int nu, int nv, const double *pu, const double *pv, const double *P, int *ci, int *cj)
{
    int on = 0;
    if (0 < nu) { int i = 0; for (;;) { *ci = i; if (P[0] == pu[i] || P[0] == pu[i + 1]) on = 1; if (!(pu[i + 1] < P[0])) break; i++; if (!(i < nu)) break; } }
    if (0 < nv) { int i = 0; for (;;) { *cj = i; if (P[1] == pv[i] || P[1] == pv[i + 1]) on = 1; if (!(pv[i + 1] < P[1])) break; i++; if (!(i < nv)) break; } }
    return on;
}

/* FUN_180581710 (msp_trim.c l.805): may the points after i up to j go (the 3D chord i-j within tol of them and of the uv midpoint) */
static int chord_ok(Ctx *c, int i, int j, int s, int e, const double *p, double tol)
{
    double P[3], Q[3], X[3];
    if (plt_pt(c, PX(p, i), PY(p, i), P) || plt_pt(c, PX(p, j), PY(p, j), Q)) return -1;
    if (!(tol < fabs(P[0] - Q[0])) && !(tol < fabs(P[1] - Q[1])) && !(tol < fabs(P[2] - Q[2]))) return 1;
    for (int k = i + 1 > e ? s : i + 1; k != j; k = k + 1 > e ? s : k + 1) {
        if (plt_pt(c, PX(p, k), PY(p, k), X)) return -1;
        if (tol < dist_pt_line(c, X, P, Q)) return 0;
    }
    if (plt_pt(c, (PX(p, i) + PX(p, j)) * 0.5, (PY(p, j) + PY(p, i)) * 0.5, X)) return -1;
    return tol < dist_pt_line(c, X, P, Q) ? 0 : 1;
}

static int on_cell_edge(const double *P, const double *pu, const double *pv, int ci, int cj)
{
    return P[0] == pu[ci] || P[0] == pu[ci + 1] || P[1] == pv[cj] || P[1] == pv[cj + 1];
}

/* FUN_180581410 (core_tess.c l.10530): the run f .. t of a loop inside one cell: points dropped (made copies of the one before them) while
   the 3D chord stays within tol; with several loops (multi) a run left wholly on the cell's edges gets its first inner point back */
static int simplify_run(Ctx *c, int multi, int f, int t, int s, int e, double *p, double tol, const double *pu, const double *pv, int ci, int cj)
{
    double keep[2] = { 0, 0 }; int kidx = -1;
    if (multi) {
        for (int k = f;;) {
            if (!on_cell_edge(p + 2 * (size_t)k, pu, pv, ci, cj)) { keep[0] = PX(p, k); keep[1] = PY(p, k); kidx = k; break; }
            if (k == t) break;
            k++; if (k > e) k = s;
        }
    }
    int a = f, nx = a + 1 > e ? s : a + 1;
    while (nx != t) {
        int b = nx + 1 > e ? s : nx + 1, last;
        for (;;) {
            int r = chord_ok(c, a, b, s, e, p, tol);
            if (r < 0) return -1;
            if (!r) { last = b - 1 < s ? e : b - 1; break; }
            if (b == t) { last = b; break; }
            b++; if (b > e) b = s;
        }
        for (int k = a; k != last; k = k + 1 > e ? s : k + 1) { PX(p, k) = PX(p, a); PY(p, k) = PY(p, a); }
        if (last == t) break;
        a = last; nx = last + 1 > e ? s : last + 1;
    }
    if (multi) {
        for (int k = f;;) {
            if (!on_cell_edge(p + 2 * (size_t)k, pu, pv, ci, cj)) return 0;
            if (k == t) break;
            k++; if (k > e) k = s;
        }
        if (kidx < 0) { c->err = "trimmed surface mesh: FUN_180581410 reads point -1 (a run wholly on the cell edges) NOT DECODED"; return -1; }
        int k2 = kidx + 1 > e ? s : kidx + 1;
        while (PX(p, kidx) == PX(p, k2) && PY(p, kidx) == PY(p, k2)) {
            PX(p, k2) = keep[0]; PY(p, k2) = keep[1];
            if (k2 == t) break;
            k2++; if (k2 > e) k2 = s;
        }
        PX(p, kidx) = keep[0]; PY(p, kidx) = keep[1];
    }
    return 0;
}

static int in_cell(const double *P, const double *pu, const double *pv, int ci, int cj)
{
    return pu[ci] <= P[0] && P[0] <= pu[ci + 1] && pv[cj] <= P[1] && P[1] <= pv[cj + 1];
}
static void swap_pt(double *p, int a, int b) { double x = PX(p, a), y = PY(p, a); PX(p, a) = PX(p, b); PY(p, a) = PY(p, b); PX(p, b) = x; PY(p, b) = y; }

/* FUN_180580470 (core_tess.c l.10678, disassembly 180580470..180581291): a loop wholly inside one cell, simplified by the 3D chord (as
   FUN_180581410); when fewer than 3 distinct points are left, a triangle is made (the longest 3D step or the one other point, a third point
   off the chord by the largest deviation, or 1e-6, inside the cell; else a small triangle about the middle of the loop's box) */
static int whole_loop(Ctx *c, int s, int e, double *p, double tol, const double *pu, const double *pv, int ci, int cj)
{
    double lo[2] = { PX(p, s), PY(p, s) }, hi[2] = { PX(p, s), PY(p, s) };
    for (int k = s + 1; k <= e; k++) {
        if (lo[0] <= PX(p, k)) { if (hi[0] < PX(p, k)) hi[0] = PX(p, k); } else lo[0] = PX(p, k);
        if (lo[1] <= PY(p, k)) { if (hi[1] < PY(p, k)) hi[1] = PY(p, k); } else lo[1] = PY(p, k);
    }
    int k = s + 1, r, u24, other = -1;
    while ((r = chord_ok(c, s, k, s, e, p, tol)) > 0) {
        if (k == e) {
            double P[3], Q[3];
            if (plt_pt(c, PX(p, s), PY(p, s), P) || plt_pt(c, PX(p, e), PY(p, e), Q)) return -1;
            if (tol < fabs(Q[0] - P[0]) || tol < fabs(Q[1] - P[1]) || tol < fabs(Q[2] - P[2])) { u24 = e; goto pair; }
            goto longest;
        }
        k++;
    }
    if (r < 0) return -1;
    {
        int last = k - 1, nxt = k > e ? s : k, a = last;
        while (nxt != last) {
            int b = nxt + 1 > e ? s : nxt + 1, t;
            for (;;) {
                int q = chord_ok(c, a, b, s, e, p, tol);
                if (q < 0) return -1;
                if (!q) { t = b - 1 < s ? e : b - 1; break; }
                if (b == last) { t = b; break; }
                b++; if (b > e) b = s;
            }
            for (int m = a; m != t; m = m + 1 > e ? s : m + 1) { PX(p, m) = PX(p, a); PY(p, m) = PY(p, a); }
            if (t == last) break;
            nxt = t + 1 > e ? s : t + 1;
            a = t;
        }
    }
    /* 18058088d..: at least 3 distinct points left -> done (LAB_180581291) */
    for (int m = s + 1; m <= e; m++) {
        int ds = PX(p, m) != PX(p, s) || PY(p, m) != PY(p, s);
        if (other == -1) { if (ds) other = m; }
        else if (ds && (PX(p, m) != PX(p, other) || PY(p, m) != PY(p, other))) return 0;
    }
    if (other == -1) goto longest;
    u24 = other;
    goto pair;
longest: {                                                                     /* LAB_180580902 (disassembly 180580902..180580a54) */
        double P[3], Q[3], best = 0.0; int bi = -1;
        if (plt_pt(c, PX(p, s), PY(p, s), P)) return -1;
        for (int m = s; m <= e; m++) {
            int n = m == e ? s : m + 1;
            if (plt_pt(c, PX(p, n), PY(p, n), Q)) return -1;
            double dy = fabs(P[1] - Q[1]), dz = fabs(P[2] - P[2]), dx = fabs(P[0] - Q[0]);   /* 1805809b1: SUBSD XMM1,XMM1 (z not compared) */
            double m3 = dy > dz ? dy : dz, mx = dx > m3 ? dx : (dy > dz ? dy : dz);
            if (mx > tol && mx > best) { bi = m; best = mx; }
            P[0] = Q[0]; P[1] = Q[1]; P[2] = Q[2];
        }
        if (bi == -1) goto small;
        if (bi != s) swap_pt(p, s, bi);
        u24 = bi + 1;
        if (u24 > e) { c->err = "trimmed surface mesh: FUN_180580470 takes the point after the loop's last NOT DECODED"; return -1; }
    }
pair: {                                                                        /* 180580973.. (core_tess.c l.10976) */
        if (u24 != s + 1) swap_pt(p, s + 1, u24);
        double ax = PX(p, s), ay = PY(p, s), bx = PX(p, s + 1), by = PY(p, s + 1);
        double nx = ay - by, ny = bx - ax, l = sqrt(nx * nx + ny * ny);
        if (l == 0.0) { nx = 0.0; ny = 0.0; } else { nx = nx / l; ny = ny / l; }
        double dev = 0.0;
        for (int m = s + 1; m <= e; m++) {
            double d = (PY(p, m) - ay) * ny + (PX(p, m) - ax) * nx;
            if (fabs(dev) < fabs(d)) dev = d;
        }
        if (1e-06 < fabs(dev) || fabs(dev) == 1e-06) { if (dev < 0.0) goto flip; }
        else if (0.0 <= ((pv[cj + 1] + pv[cj]) * 0.5 - ay) * ny + ((pu[ci + 1] + pu[ci]) * 0.5 - ax) * nx) dev = 1e-06;
        else {
            dev = -1e-06;
        flip:
            PX(p, s + 2) = PX(p, s); PY(p, s + 2) = PY(p, s);
            PX(p, s) = PX(p, s + 1); PY(p, s) = PY(p, s + 1);
            PX(p, s + 1) = PX(p, s + 2); PY(p, s + 1) = PY(p, s + 2);
        }
        double mx = (PX(p, s) + PX(p, s + 1)) * 0.5, my = (PY(p, s + 1) + PY(p, s)) * 0.5;
        PX(p, s + 2) = dev * nx + mx; PY(p, s + 2) = dev * ny + my;
        if (in_cell(p + 2 * (size_t)(s + 2), pu, pv, ci, cj)) goto rest;
        PX(p, s + 2) = mx - dev * nx; PY(p, s + 2) = my - dev * ny;
        if (in_cell(p + 2 * (size_t)(s + 2), pu, pv, ci, cj)) goto rest;
    }
small: {                                                                       /* core_tess.c l.11086, disassembly 180580e00..18058125f */
        double mu = (lo[0] + hi[0]) * 0.5, mv = (lo[1] + hi[1]) * 0.5;
        for (int m = 0; m < 3; m++) { PX(p, s + m) = mu; PY(p, s + m) = mv; }
        double cm = (pu[ci] + pu[ci + 1]) * 0.5, du = cm - mu, dv = cm - mv;   /* 180580e2b: the cell's u middle for both */
        double hu = (hi[0] - lo[0]) * 0.5, hv = (hi[1] - lo[1]) * 0.5, e1, e2;
        #define EPS(h, gap) ((1e-06 > (h) && (gap) > 1e-06) ? 1e-06 : 0.0)
        if (fabs(du) <= fabs(dv)) {
            if (dv > 1e-06) {
                e1 = EPS(hv, lo[1] - pv[cj]); PY(p, s) = PY(p, s) - (e1 + hv);
                e2 = EPS(hv, pv[cj + 1] - hi[1]); PY(p, s + 1) = (e2 + hv) + PY(p, s + 1); PY(p, s + 2) = (e2 + hv) + PY(p, s + 2);
                e1 = EPS(hu, pu[ci + 1] - hi[0]); PX(p, s + 1) = (e1 + hu) + PX(p, s + 1);
                e2 = EPS(hu, lo[0] - pu[ci]); PX(p, s + 2) = PX(p, s + 2) - (e2 + hu);
            } else {
                e1 = EPS(hv, pv[cj + 1] - hi[1]); PY(p, s) = (e1 + hv) + PY(p, s);
                e2 = EPS(hv, lo[1] - pv[cj]); PY(p, s + 1) = PY(p, s + 1) - (e2 + hv); PY(p, s + 2) = PY(p, s + 2) - (e2 + hv);
                e1 = EPS(hu, lo[0] - pu[ci]); PX(p, s + 1) = PX(p, s + 1) - (e1 + hu);
                e2 = EPS(hu, pu[ci + 1] - hi[0]); PX(p, s + 2) = (e2 + hu) + PX(p, s + 2);
            }
        } else {
            if (du > 1e-06) {
                e1 = EPS(hu, lo[0] - pu[ci]); PX(p, s) = PX(p, s) - (e1 + hu);
                e2 = EPS(hu, pu[ci + 1] - hi[0]); PX(p, s + 1) = (e2 + hu) + PX(p, s + 1); PX(p, s + 2) = (e2 + hu) + PX(p, s + 2);
                e1 = EPS(hv, lo[1] - pv[cj]); PY(p, s + 1) = PY(p, s + 1) - (e1 + hv);
                e2 = EPS(hv, pv[cj + 1] - hi[1]); PY(p, s + 2) = (e2 + hv) + PY(p, s + 2);
            } else {
                e1 = EPS(hu, pu[ci + 1] - hi[0]); PX(p, s) = (e1 + hu) + PX(p, s);
                e2 = EPS(hu, lo[0] - pu[ci]); PX(p, s + 1) = PX(p, s + 1) - (e2 + hu); PX(p, s + 2) = PX(p, s + 2) - (e2 + hu);
                e1 = EPS(hv, pv[cj + 1] - hi[1]); PY(p, s + 1) = (e1 + hv) + PY(p, s + 1);
                e2 = EPS(hv, lo[1] - pv[cj]); PY(p, s + 2) = PY(p, s + 2) - (e2 + hv);
            }
        }
        #undef EPS
    }
rest:                                                                          /* LAB_18058125f: the rest of the loop = the third point */
    for (int m = s + 3; m <= e; m++) { PX(p, m) = PX(p, s + 2); PY(p, m) = PY(p, s + 2); }
    return 0;
}

/* FUN_18057f9a0 (core_tess.c l.5026, disassembly 18057f9a0..1805803ed): per loop of more than 3 points FUN_1805812e0, then per run of points
   inside one grid cell (FUN_18058a980: the first point off the grid lines) FUN_180581410, or FUN_180580470 when the whole loop is in one
   cell; then the loops packed (repeats dropped), a 2-segment loop given a third point off its chord (2.00002e-05 of the chord) or a tiny
   triangle (2e-10), the loop boxes grown by it */
static int refine_loops(Ctx *c, double tol, int nl, int nu, int nv, const double *pu, const double *pv, double *p, int *tb, double *ub, double *vb)
{
    int multi = 1 < nl;
    for (int l = 0; l < nl; l++) {
        if (!(2 < tb[2 * l + 1])) continue;
        int s = tb[2 * l], e = tb[2 * l + 1] + s;
        flatten_corners(s, e, p, nu, nv, pu, pv);
        int i17 = s, i19 = e;
        do {
            int ci = 0, cj = 0, f4 = -1;
            for (int k = i17; k <= i19; k++) if (!cell_of(nu, nv, pu, pv, p + 2 * (size_t)k, &ci, &cj)) { f4 = k; break; }   /* FUN_18058a980 */
            if (f4 == -1) break;
            i17 = f4;
            for (int k = f4 + 1; k <= i19; k++) {                             /* 18057fb20 */
                const double *P = p + 2 * (size_t)k;
                if (!in_cell(P, pu, pv, ci, cj)) break;
                i17 = k;
                if (on_cell_edge(P, pu, pv, ci, cj)) break;
            }
            for (int k = f4 - 1; k >= s; k--) {                               /* 18057fbc2 */
                const double *P = p + 2 * (size_t)k;
                if (!in_cell(P, pu, pv, ci, cj)) break;
                f4 = k;
                if (on_cell_edge(P, pu, pv, ci, cj)) break;
            }
            int whole = 0;
            if (f4 != s || i17 != e) {                                        /* 18057fc32 */
                for (int k = i17;;) {
                    k++; if (k > e) k = s;
                    if (k == f4) { whole = 1; break; }
                    if (!in_cell(p + 2 * (size_t)k, pu, pv, ci, cj)) break;
                }
            }
            if (!whole && f4 == s) {
                if (i17 == e) whole = 1;
                else if (i19 > i17 + 1) {                                     /* 18057fcb9 */
                    for (int k = i19; ; ) {
                        if (!in_cell(p + 2 * (size_t)k, pu, pv, ci, cj)) break;
                        f4 = k; i19 = k;
                        k--;
                        if (!(k > i17 + 1)) break;
                    }
                }
            }
            if (whole) { if (whole_loop(c, s, e, p, tol, pu, pv, ci, cj)) return -1; break; }
            if (simplify_run(c, multi, f4, i17, s, e, p, tol, pu, pv, ci, cj)) return -1;
            i17 = i17 + 1;
        } while (i17 <= i19);
    }
    /* 18057fe1f..: packing */
    for (int l = 0; l < nl; l++) {
        int *E = tb + 2 * l;
        if (2 < E[1]) {
            int ns = l == 0 ? 0 : E[-2] + 1 + E[-1], w = ns - 1;
            for (int k = 0; k <= E[1]; k++) {
                int src = E[0] + k;
                if (k == 0 || PX(p, w) != PX(p, src) || PY(p, w) != PY(p, src)) { w++; PX(p, w) = PX(p, src); PY(p, w) = PY(p, src); }
            }
            E[0] = ns; E[1] = w - ns;
            while (0 < E[1] && PX(p, ns) == PX(p, ns + E[1]) && PY(p, ns) == PY(p, ns + E[1])) E[1]--;
            if (E[1] == 0) { PX(p, ns + 1) = PX(p, ns); PY(p, ns + 1) = PY(p, ns); E[1]++; }
            if (E[1] == 1) { PX(p, E[0] + 2) = PX(p, E[0] + 1); PY(p, E[0] + 2) = PY(p, E[0] + 1); E[1]++; }
        }
        if (E[1] == 2) {
            int s = E[0], i10 = s + 2, i17 = s + 1, i19 = s;
            double d = sqrt((PY(p, s + 1) - PY(p, s)) * (PY(p, s + 1) - PY(p, s)) + (PX(p, s + 1) - PX(p, s)) * (PX(p, s + 1) - PX(p, s)));
            double d2 = sqrt((PY(p, s + 2) - PY(p, s + 1)) * (PY(p, s + 2) - PY(p, s + 1)) + (PX(p, s + 2) - PX(p, s + 1)) * (PX(p, s + 2) - PX(p, s + 1)));
            if (d < d2) { d = d2; i17 = s + 2; i19 = s + 1; i10 = s; }
            double d3 = sqrt((PY(p, s + 2) - PY(p, s)) * (PY(p, s + 2) - PY(p, s)) + (PX(p, s + 2) - PX(p, s)) * (PX(p, s + 2) - PX(p, s)));
            if (d < d3) { i10 = s + 1; i19 = s + 2; d = d3; i17 = s; }
            double *ubl = ub + 2 * l, *vbl = vb + 2 * l;
            if (1e-10 < d || d == 1e-10) {
                double nx = PY(p, i19) - PY(p, i17), ny = PX(p, i17) - PX(p, i19), L = sqrt(nx * nx + ny * ny);
                if (L == 0.0) { nx = 0.0; ny = 0.0; } else { nx = nx / L; ny = ny / L; }
                double x3 = PX(p, i19), y3 = PY(p, i19);
                if (1e-10 <= fabs((PY(p, i10) - y3) * ny + (PX(p, i10) - x3) * nx)) continue;
                PX(p, i10) = (x3 + PX(p, i17)) * 0.5 - (PY(p, i17) - y3) * 2.0000200000000002e-05;
                PY(p, i10) = (PY(p, i19) + PY(p, i17)) * 0.5 + (PX(p, i17) - PX(p, i19)) * 2.0000200000000002e-05;
                if (PX(p, i10) < ubl[0]) ubl[0] = PX(p, i10);
                if (ubl[1] < PX(p, i10)) ubl[1] = PX(p, i10);
                if (PY(p, i10) < vbl[0]) vbl[0] = PY(p, i10);
                if (vbl[1] < PY(p, i10)) vbl[1] = PY(p, i10);
            } else {
                PX(p, s + 1) = PX(p, s) + 2e-10; PY(p, s + 1) = PY(p, s);
                PX(p, s + 2) = PX(p, s); PY(p, s + 2) = PY(p, s) + 2e-10;
                if (PX(p, s + 1) < ubl[0]) ubl[0] = PX(p, s + 1);
                if (ubl[1] < PX(p, s + 1)) ubl[1] = PX(p, s + 1);
                if (PY(p, s + 1) < vbl[0]) vbl[0] = PY(p, s + 1);
                if (vbl[1] < PY(p, s + 1)) vbl[1] = PY(p, s + 1);
                if (PX(p, s + 2) < ubl[0]) ubl[0] = PX(p, s + 2);
                if (ubl[1] < PX(p, s + 2)) ubl[1] = PX(p, s + 2);
                if (PY(p, s + 2) < vbl[0]) vbl[0] = PY(p, s + 2);
                if (vbl[1] < PY(p, s + 2)) vbl[1] = PY(p, s + 2);
            }
        }
    }
    return 0;
}

/* FUN_180577620 (core_tess.c l.5498): the outer loop counter-clockwise, the islands clockwise (reversed in place) */
static void orient_loops(int outer, int nl, double *p, const int *tb)
{
    for (int l = 0; l < nl; l++) {
        int w = is_bdy_ccw(tb, p, l);
        if (l != outer) w = !w;
        if (!w) for (int a = tb[2 * l], b = tb[2 * l] + tb[2 * l + 1]; a < b; a++, b--) swap_pt(p, a, b);
    }
}

/* 18057bcea..18057c1a6: the loops after the grid. pu / pv have room for the island lines. On return T holds the final loops (points / table,
   T->nl the count FUN_180598070 left). Returns 1 ok, 0 FUN_180576e90 failed (not reached: it always returns 1), -1 not ported */
static int plt_loops(Ctx *c, PltTrim *T, double tol, double u0, double u1, double v0, double v1, int *nu, int *nv, double *pu, double *pv)
{
    cut_params(u0, u1, v0, v1, nu, nv, pu, pv);                                /* 18057bd23 */
    island_lines(T, nu, nv, pu, pv);                                           /* 18057bd5c */
    double *np; int nnp, *ntb;
    add_crossings(T, pu, pv, *nu, *nv, &np, &nnp, &ntb);                       /* 18057bddf */
    free(T->p); free(T->tb); T->p = np; T->np = nnp; T->tb = ntb;
    if (0.0 < tol && T->nl > 0) {                                              /* 18057be1b */
        int tot = T->tb[2 * (T->nl - 1)] + T->tb[2 * (T->nl - 1) + 1] + 1;
        int *ctb = malloc(sizeof(int) * 2 * T->nl); memcpy(ctb, T->tb, sizeof(int) * 2 * T->nl);
        double *cp = malloc(sizeof(double) * 2 * ((size_t)tot + 4)); memcpy(cp, T->p, sizeof(double) * 2 * (size_t)tot);
        if (refine_loops(c, tol, T->nl, *nu, *nv, pu, pv, cp, ctb, T->ub, T->vb)) { free(ctb); free(cp); return -1; }   /* 18057bfbf */
        int ok = clean_loops(c, cp, ctb, &T->nl, tol);                         /* 18057c05e: the loop count kept even when it fails */
        if (ok < 0) { free(ctb); free(cp); return -1; }
        if (ok) {
            for (int l = 0; l < T->nl; l++) for (int k = ctb[2 * l]; k <= ctb[2 * l] + ctb[2 * l + 1]; k++) snap_pt(cp + 2 * (size_t)k, pu, pv, *nu, *nv);
            int nest = 1;
            for (int l = 0; l < T->nl; l++)
                if (l != T->outer && in_polygon(1, cp, ctb + 2 * T->outer, cp + 2 * (size_t)ctb[2 * l]) == -1) { nest = 0; break; }
            if (nest) {
                orient_loops(T->outer, T->nl, cp, ctb);
                memcpy(T->tb, ctb, sizeof(int) * 2 * T->nl);
                memcpy(T->p, cp, sizeof(double) * 2 * (size_t)tot);
            }
        }
        free(ctb); free(cp);
    }
    return 1;
}
