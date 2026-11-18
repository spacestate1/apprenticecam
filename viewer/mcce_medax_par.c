/* mcce_medax_par.c - CalcMedAx line-point (parabola) bisector pieces, included after mcce_medax.c.
 * Source: the medaxgaps addendum (decomp/medax/medaxgaps/medaxgaps_spec.txt sections A / B; objdump
 * disassembly of mcce.dll, Ghidra lost the CTMatrix arguments) to calcmedax_spec.txt 3.6 / 3.7.
 *
 * CTMatrix (A): 3x3 row-major, points are row vectors p' = [x y 1] M: x' = (y M10 + x M00) + M20, y' = (x M01 + y M11) + M21;
 * A *= B sets A = A B (so the first factor is applied first). Stored here as M[9]. */

typedef struct { double m[9]; } Mx3;
static Mx3 mx3_id(void) { Mx3 r = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 } }; return r; }
static Mx3 mx3_mul(const Mx3 *a, const Mx3 *b) {   /* f.3414-3460: A[i][j] = A[i][0] B[0][j] + A[i][1] B[1][j] + A[i][2] B[2][j] */
    Mx3 r;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
        r.m[i * 3 + j] = a->m[i * 3 + 0] * b->m[0 * 3 + j] + a->m[i * 3 + 1] * b->m[1 * 3 + j] + a->m[i * 3 + 2] * b->m[2 * 3 + j];
    return r;
}
static Mx3 mx3_trans(double tx, double ty) { Mx3 r = mx3_id(); r.m[6] = tx; r.m[7] = ty; return r; }   /* TranslationMx @180167560 */
static Mx3 mx3_rot(double a) { Mx3 r = mx3_id(); r.m[0] = cos(a); r.m[1] = sin(a); r.m[3] = -sin(a); r.m[4] = cos(a); return r; }   /* RotationMx @180167390 */
static Mx3 mx3_refl0(void) { Mx3 r = mx3_id(); r.m[4] = -1.0; return r; }   /* ReflectionMx(0) @180167270: y -> -y */
/* InvertMx @180167030 (f.5745-5787): out[j][i] = Cofactor(i, j) / det; |det| < 1e-7 -> InvertByExchange (never here: det = +-1) */
static int mx3_inv(const Mx3 *a, Mx3 *out) {
    const double *m = a->m;
    double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (fabs(det) < 1e-7) return 0;   /* (InvertByExchange path not decoded / not reached) */
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        int r0 = (i + 1) % 3, r1 = (i + 2) % 3, c0 = (j + 1) % 3, c1 = (j + 2) % 3;
        double cof = m[r0 * 3 + c0] * m[r1 * 3 + c1] - m[r0 * 3 + c1] * m[r1 * 3 + c0];   /* cyclic minors carry the sign */
        out->m[j * 3 + i] = cof / det;
    }
    return 1;
}
static P2 mx3_pt(const Mx3 *M, P2 p) { return p2((p.y * M->m[3] + p.x * M->m[0]) + M->m[6], (p.x * M->m[1] + p.y * M->m[4]) + M->m[7]); }   /* @180166990 */
static void mx3_store(const Mx3 *M, double o[6]) { o[0] = M->m[0]; o[1] = M->m[3]; o[2] = M->m[6]; o[3] = M->m[1]; o[4] = M->m[4]; o[5] = M->m[7]; }
static Mx3 mx3_load(const double o[6]) { Mx3 r = mx3_id(); r.m[0] = o[0]; r.m[3] = o[1]; r.m[6] = o[2]; r.m[1] = o[3]; r.m[4] = o[4]; r.m[7] = o[5]; return r; }

/* FUN_1800ab960 (f.93228-93319, addendum B.8): parabola y^2 = 4 h x (local) x the line {Q, V}; returns n, u[i], t[i] */
static int par_x_line(double h, const double Mw[6], P2 Q, P2 V, double *u, double *t) {
    Mx3 M = mx3_load(Mw);
    P2 a = mx3_pt(&M, Q), b = mx3_pt(&M, padd(Q, V)), v = psub(b, a);
    double c[3], x[4];
    c[0] = a.y * a.y - (h * 4.0) * a.x;
    c[1] = (v.y * 2.0) * a.y - (h * 4.0) * v.x;
    c[2] = v.y * v.y;
    int n = poly_roots2(c, x);
    for (int i = 0; i < n; i++) { P2 L = p2(v.x * x[i] + a.x, v.y * x[i] + a.y); u[i] = L.y / (h * 2.0); t[i] = x[i]; }
    return n;
}
/* FUN_1800b2dc0 ParabolaLimit (f.96916-97102, addendum B.7) */
static int par_limit(const MxSL *G, const Mae *r, double *ulim) {
    P2 T, B;
    if (G_ent(G, r->sh2, r->en2)->id == 'P') {
        const MxS *S = &G->s[r->sh2]; int j = r->en2 == 0 ? S->n - 1 : r->en2 - 1;
        T = mx_tan_end(&S->e[j]); B = G_ent(G, r->sh2, r->en2)->p1;
    } else {
        const MxS *S = &G->s[r->sh1]; int j = (r->en1 + 1) % S->n;
        T = mx_tan_start(&S->e[j]); B = G_ent(G, r->sh1, r->en1)->p1;
    }
    P2 Nn = p2(T.y, -T.x); double len = sqrt(T.y * T.y + -T.x * -T.x);
    if (len >= 1e-12) Nn = p2(T.y / len, -T.x / len);
    double u[4], t[4];
    int n = par_x_line(r->h, r->M, B, Nn, u, t);
    if (n == 0) return 0;
    int i = 0; if (n == 2 && t[1] > t[0]) i = 1;
    if (t[i] < 0.0) return 0;
    *ulim = u[i];
    return 1;
}
/* FUN_1800b3a10 (f.97602-97800, addendum B.1-B.5): the line-point bisector (type 1) */
static int bis_lp(const MxSL *G, const MaeL *out, Mae *r) {
    int k = G_ent(G, r->sh2, r->en2)->id == 'L';                     /* B.1: line = side 2 if side 2 is 'L' */
    const MxE *L = k ? G_ent(G, r->sh2, r->en2) : G_ent(G, r->sh1, r->en1), *Pt = k ? G_ent(G, r->sh1, r->en1) : G_ent(G, r->sh2, r->en2);
    P2 E1 = L->p1, D = psub(L->p2, L->p1), P = Pt->p1, N = p2(-D.y, D.x), F; double t1;
    if (!lnln(P, N, E1, D, &t1, NULL, &F) || t1 < 0.0) return 0;     /* B.2 */
    r->h = sqrt((P.x - F.x) * (P.x - F.x) + (P.y - F.y) * (P.y - F.y)) * 0.5;
    if (r->h < 1e-12) return 0;
    Mx3 M = mx3_trans(-P.x, -P.y);                                   /* B.3 */
    if (!((fabs(N.x) > 1e-12) || (fabs(N.y) > 1e-12))) return 0;
    double a = atan2(N.y, N.x), th = 3.141592653589793 - a;
    Mx3 R = mx3_rot(th); M = mx3_mul(&M, &R);
    if (k) { Mx3 Rf = mx3_refl0(); M = mx3_mul(&M, &Rf); }
    Mx3 T2 = mx3_trans(r->h, 0.0); M = mx3_mul(&M, &T2);
    Mx3 Mi; if (!mx3_inv(&M, &Mi)) return 0;
    mx3_store(&M, r->M); mx3_store(&Mi, r->Mi);
    double ts = k ? r->t2s : r->t1s;                                 /* B.5 */
    P2 Q0 = p2(D.x * ts + E1.x, D.y * ts + E1.y);
    double y = mx3_pt(&M, Q0).y, hh = r->h * 2.0;
    r->u0 = y / hh;
    P2 X0 = mx3_pt(&Mi, p2((r->u0 * r->h) * r->u0, (r->h * 2.0) * r->u0));
    if (r->parent == -1) return 0;
    P2 E = mae_end(&out->r[r->parent]);
    if ((X0.x - E.x) * (X0.x - E.x) + (X0.y - E.y) * (X0.y - E.y) > 1e-12) return 0;
    double te = k ? 0.0 : 1.0;
    P2 Q1 = p2(D.x * te + E1.x, D.y * te + E1.y);
    r->u1 = mx3_pt(&M, Q1).y / (r->h * 2.0);
    double ul; if (par_limit(G, r, &ul) && r->u1 > ul) r->u1 = ul;
    if (!(r->u0 <= r->u1)) return 0;
    r->type = 1;
    return 1;
}
/* FUN_1800ba330 line x parabola (f.100948-101097, addendum B.9) */
static int contact_lpar(const MxSL *G, const Mae *r, int k, int j, double *t, double *s) {
    const MxE *e = G_ent(G, k, j); P2 A = e->p1, B = e->p2, C = p2(A.x - (B.y - A.y), A.y + (B.x - A.x));
    Mx3 M = mx3_load(r->M);
    P2 a = mx3_pt(&M, A), b = mx3_pt(&M, B), c = mx3_pt(&M, C), d = psub(b, a), nn = psub(c, a);
    double len = sqrt(nn.x * nn.x + nn.y * nn.y), h = r->h;
    if (len < 1e-12) return 0;
    nn = p2(nn.x / len, nn.y / len);
    double cf[3], x[4]; int big = fabs(d.x) > fabs(d.y);
    if (big) { double q = d.y / d.x; cf[0] = ((h * nn.y - a.y) - (nn.x * q) * h) + a.x * q; cf[1] = h * 2.0; cf[2] = ((nn.y - q) - nn.x * q) * h; }
    else { double q = d.x / d.y; cf[0] = ((nn.x - nn.y * q) * h - a.x) + a.y * q; cf[1] = (h * -2.0) * q; cf[2] = ((nn.x + 1.0) - nn.y * q) * h; }
    int n = poly_roots2(cf, x);
    for (int i = 0; i < n; i++) {
        double u = x[i], tt;
        int ok = s_ok(r, u);
        if (ok) {
            double u0 = x[0];   /* the loop reads slot 0 for t (@1800ba665) */
            tt = big ? (((u0 * u0) * (nn.x + 1.0) + nn.x) * h - a.x) / d.x : (((u0 * nn.y + 2.0) * u0 + nn.y) * h - a.y) / d.y;
            *t = tt;
            ok = t_ok(a, d, tt);
        }
        if (!ok) { if (i == 0) x[0] = x[1]; n--; i--; }
    }
    if (n > 0) { *s = x[0]; return 1; }
    return 0;
}
/* FUN_1800ba8a0 point x parabola (f.101098-101163, addendum B.9) */
static int contact_ppar(const MxSL *G, const Mae *r, int k, int j, double *s) {
    Mx3 M = mx3_load(r->M); P2 q = mx3_pt(&M, G_ent(G, k, j)->p1); double h = r->h, cf[3], x[4];
    cf[0] = (q.y * q.y + q.x * q.x) - h * h; cf[1] = (h * -4.0) * q.y; cf[2] = (h - q.x) * (h * 2.0);
    int n = poly_roots2(cf, x);
    for (int i = 0; i < n; i++) if (!s_ok(r, x[i])) { if (i == 0) x[0] = x[1]; n--; i--; }
    if (n > 0) { *s = x[0]; return 1; }
    return 0;
}
