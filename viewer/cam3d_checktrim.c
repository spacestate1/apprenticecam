/* cam3d_checktrim.c - McHmm's Edit Passes to Surfaces plan 0x141f (CTrimPassesToSurfacePlan / EvaTrimPassesToSurfacePlan) as mckernel uses it
 * for the check surfaces of HST Waterline, Raster, Spiral, Radial and Project with "HST check trimming method" 1 (the registry default; user
 * decision N13-10). Included by cam3d_finish.c after the drop cutter (hd_tip). Decode: formats/surface_selection.md 8.3 (cited "ts l.N" =
 * decomp/hst3d_trimsurf.c, the DumpRange of mchmm.dll 18034e000-18035cd70; "tb2 l.N" = decomp/raster/tb2.c; "d3 l.N" = decomp/scallop/d3.c;
 * "mk l.N" = decomp/mckernel_all.c; "ms l.N" = decomp/mcmsurf_all.c).
 *
 * The plan (mckernel, after each style's passes plan, e.g. Waterline mk l.93590-93603): inputs "MachiningPassesIn" = the passes, "ProtectedSurfaces"
 * = ekPlanC2, the check model (Check Database Loader 0x1421 -> Detect Top Check Surfaces 0x1423 -> Check Surface Tessellation 0x1424, FUN_18007ca68
 * mk l.92160); parameters FUN_180070e8c (mk l.86832-86889): DefragmentThickness 0, OverThickness = tmpmmlu.tmp +0x150 (0 when +0x160 > 0),
 * TrimToSurfaceTolerance 1e-6, TrimToSurfExtension 0, TrimToSurfJoinGaps 0, TrimToSurfFragmentAllowance 0; OutputID = TrimmedOutPasses (0x1cb2):
 * the plan's trimmed-out output is what the next plan reads. The plan's own defaults for 0x141f (FUN_180303a20 d3 l.218-412):
 * EnableTrimToSurfaces = !IsNullKey(ProtectedSurfaces) (true here), TrimToSurfaceSimultanious false, TrimToSurfacePerZLevel =
 * IsZProfilePassesPlan(the input), SurfaceTrimKeepExterior false, TrimToSurfaceResolution (6999) 0.5.
 *
 * The evaluator (ts l.1093-1290 FUN_18034f5a0 -> FUN_1802ed680 -> FUN_1803581a0 ts l.6566, one task per pass FUN_1803585e0 -> FUN_180357ab0):
 *   the trimmer (Eva +2000, set up by FUN_1803503e0 ts l.1431 from FUN_1802eb170 tb2 l.370-534): tool 1 = the op's tool (ToolShaftRadius R,
 *   ToolCornerRadius Rc) offset by OverThickness t (FUN_18013e330: R + t, Rc + t, hst3d_grid.c), the drop start lowered by t (+0x48);
 *   the test FUN_180351fe0 / FUN_180351ee0 (ts l.1897-1985): a CToolDropPos of tool 1 on the check boxset from z - t; the position is "in"
 *   when the drop rises above its start (the tool, stock-inflated, touches a check surface).
 *   FUN_1803589c0 (ts l.6896): the pass's in-intervals by FUN_1803574d0 (ts l.6003) -> FUN_180353440 per segment: FUN_1803538c0 samples the
 *   segment at n = max(1, int(L / TrimToSurfaceResolution)) equal steps (L the 3D length) and records each change of the test; FUN_180353c70
 *   refines each change by FUN_180356f50 (bisection on the segment, Z linear, until the bracket is <= TrimToSurfaceTolerance; the crossing is
 *   the bracket's end that is not in) and pairs the crossings into in-intervals (a pass starting in opens one at its start; on a closed pass the
 *   first one is closed at the end instead, through the seam).
 *   FUN_180356240 (ts l.5263) joins touching intervals (JoinGaps 0: no gap joining), FUN_180355410 (Extension 0: nothing), FUN_1803541c0
 *   (DefragmentThickness 0: the whole pass as the defragment piece).
 *   FUN_180357ab0 (ts l.6340-6372): KeepExterior false -> the main output = the in-intervals, TrimmedOutPasses = their complement
 *   (FUN_1803569d0 ts l.5561: open pass [start, first), the gaps, [last, end); closed pass the gaps and the one across the seam) - what is cut.
 *
 * Ported as decoded. Ours (labelled): the check model is our tessellation of the check references (OCCT / mcs_mesh at the drive's deflection,
 * the Check Surface Tessellation's "Tolerance" = job +0xa0, mk l.92171); Detect Top Check Surfaces' selection is not applied (our drop takes
 * every triangle, as the drive's); the per-Z weave of TrimToSurfacePerZLevel (FUN_180352a90 / FUN_180352d10) only skips the drop far outside
 * the slice (FUN_1802887a0's signed distance > the tolerance, ts l.1960-1975) and is not built: every test is the drop. */

struct C3Chk { C3Mesh M; HstDrop d; double t, res, tol; int warned; };

/* the test of FUN_180351fe0 / FUN_180351ee0: tool 1 dropped from (x, y, z - t) rises (hd_tip = the highest tip of the offset tool in contact) */
static int c3chk_in(const C3Chk *c, const double *P) { return hd_tip(&c->d, P[0], P[1]) > P[2] - c->t; }

/* c3_chk_open: the check model of the op when its style trims to it (c3_check_rule C3_CHK_KERNEL) and it has check references; NULL with
   err[0] = 0 when there is nothing to trim, NULL with err set on a refusal. Call before the drive's c3_mesh_op (that call then leaves the
   checks out of the drive). R / Rc: the op's tool (ToolShaftRadius / ToolCornerRadius), rec: the op record. */
static C3Chk *c3_chk_open(const McamFile *f, CamOp *op, const uint8_t *rec, uint32_t rn, double defl, double R, double Rc, int metric, char *err, size_t errn) {
    err[0] = 0; g_c3_chk_kernel = 0;
    if (c3_check_rule(op->p.type) != C3_CHK_KERNEL) return NULL;
    C3Chk *c = calloc(1, sizeof *c);
    const char *cs = getenv("MCAM_CHK_STL");   /* testing: the check model from an ASCII STL in mm (the file the mchmm.dll harness loads, PREFIX.chk.stl) */
    if (cs) { if (!c3_mesh_stl(cs, metric ? 1.0 : 25.4, &c->M)) { snprintf(err, errn, "MCAM_CHK_STL: cannot read %s", cs); free(c); return NULL; } }
    else if (!c3_mesh_refs(f, op, defl, TP_REF_CHECK, &c->M, err, errn)) { free(c); return NULL; }
    if (c->M.n <= 0) { c3_mesh_free(&c->M); free(c); return NULL; }   /* no checks: the plan is not added (FUN_180070e8c fails without tmpmmlu.tmp) */
    /* OverThickness = the stock to leave on the checks: tmpmmlu.tmp +0x150 written by FUN_180220fe0 (ms l.359092-359109: op 0x84 with 0x16c4 != 0 ->
       op +0x1240 = record 0x12a8, the HST op's check stock, compared with the Z stock op +0x1360 by FUN_1800202d0 ms l.23081), 0 when +0x160 > 0
       (FUN_180070e8c mk l.86862-86866). +0x160 (the part of a negative check stock beyond the tool, ms l.359105-359115) is only partly decoded:
       a negative check stock is refused. */
    double t = c3d(rec, rn, 0x12a8);
    if (t < 0) { snprintf(err, errn, "check surfaces: a negative stock to leave on the checks (tmpmmlu.tmp +0x160, FUN_180220fe0) is NOT DECODED"); c3_mesh_free(&c->M); free(c); return NULL; }
    /* the tool holder joins tool 1 when mckernel sets ToolHolderDefinition (FUN_180079bfc under FUN_18006f480: K+0x472 = record 0x16d7 with a holder and
       K+0x49e = record 0x165c == 0, mk l.87618-87620; FUN_18013ece0 tb2 l.428-470): NOT PORTED */
    if (c3b(rec, rn, 0x16d7) && !c3b(rec, rn, 0x165c)) { snprintf(err, errn, "check surfaces with the tool holder check (ToolHolderDefinition in the check trimming, FUN_18013ece0) are NOT PORTED"); c3_mesh_free(&c->M); free(c); return NULL; }
    c->t = t;
    /* tool 1: FUN_18013e330(tool, ToolHolderThickness, t, 0) = R + t, Rc + t (ts tool at +0x268); in hd's terms flat radius R - Rc, corner Rc + t */
    hd_build(&c->d, &c->M, fmax(R - Rc, 0.0), Rc + t);
    /* TrimToSurfaceResolution 0.5 (FUN_180303a20 d3 l.375-379, not set by mckernel) and TrimToSurfaceTolerance 1e-6 (mk l.86870-86873, factor 1):
       McHmm's lengths are in mm (the inch part's values reach it x 25.4, as MachTrimZTolerance's 0.1 mm in cam3d_finish.c) */
    c->res = metric ? 0.5 : 0.5 / 25.4; c->tol = metric ? 1e-6 : 1e-6 / 25.4;
    g_c3_chk_kernel = 1;
    return c;
}
static const C3Mesh *c3_chk_mesh(const C3Chk *c) { return &c->M; }
static void c3_chk_close(C3Chk *c) { if (!c) return; hd_free(&c->d); c3_mesh_free(&c->M); free(c); }

/* a position on the pass: segment i (P[i] -> P[i + 1]) at u, the point p */
typedef struct { int i; double u, p[3]; } C3ChkPos;
static void c3chk_at(const double *P, int i, double u, double *q) { for (int k = 0; k < 3; k++) q[k] = P[3 * i + k] + (P[3 * i + 3 + k] - P[3 * i + k]) * u; }
static int c3chk_eq(const C3ChkPos *a, const C3ChkPos *b) { return a->i == b->i && a->u == b->u; }
static int c3chk_lt(const C3ChkPos *a, const C3ChkPos *b) { return a->i < b->i || (a->i == b->i && a->u < b->u); }

/* FUN_180356f50: the crossing between segment parameters ua (state sa) and ub, bisected to the tolerance; the end that is not in */
static C3ChkPos c3chk_bisect(const C3Chk *c, const double *P, int i, double ua, double ub) {
    C3ChkPos a = { i, ua, { 0 } }, b = { i, ub, { 0 } };
    c3chk_at(P, i, ua, a.p); c3chk_at(P, i, ub, b.p);
    int sa = c3chk_in(c, a.p); double t2 = c->tol * c->tol;
    for (int it = 0; it < 200; it++) {
        double dx = a.p[0] - b.p[0], dy = a.p[1] - b.p[1], dz = a.p[2] - b.p[2];
        if (!(dx * dx + dy * dy + dz * dz > t2)) break;
        C3ChkPos m = { i, a.u != b.u ? (a.u + b.u) * 0.5 : a.u, { 0 } };
        c3chk_at(P, i, m.u, m.p); m.p[2] = a.p[2] != b.p[2] ? (a.p[2] + b.p[2]) * 0.5 : a.p[2];
        if (c3chk_in(c, m.p) == sa) a = m; else b = m;
    }
    return sa ? b : a;
}

/* the piece of the pass from position a to position b (through the seam of a closed pass when b is before a) */
static int c3chk_piece(const double *P, int n, int closed, const C3ChkPos *a, const C3ChkPos *b, double *out) {
    int m = 0;
    #define PUT(q) do { if (!m || fabs(out[3 * m - 3] - (q)[0]) + fabs(out[3 * m - 2] - (q)[1]) + fabs(out[3 * m - 1] - (q)[2]) > 0) { memcpy(out + 3 * m, (q), sizeof(double) * 3); m++; } } while (0)
    PUT(a->p);
    if (c3chk_lt(b, a) && closed) {   /* across the seam: P[n - 1] = P[0] */
        for (int k = a->i + 1; k <= n - 1; k++) PUT(P + 3 * k);
        for (int k = 1; k <= b->i; k++) PUT(P + 3 * k);
    } else for (int k = a->i + 1; k <= b->i; k++) PUT(P + 3 * k);
    PUT(b->p);
    #undef PUT
    return m;
}

/* the pass P[0..n-1] (closed: P[n - 1] = P[0], a loop) cut where the tool touches the checks; each kept piece goes to emit(xyz, m, ctx).
   Returns the number of pieces. */
static int c3_chk_trim(const C3Chk *c, const double *P, int n, int closed, void (*emit)(const double *xyz, int m, void *ctx), void *ctx) {
    if (n <= 0) return 0;
    if (n == 1) { if (c3chk_in(c, P)) return 0; emit(P, 1, ctx); return 1; }   /* a one-position pass: in -> the interval [start, end] -> nothing kept */
    /* FUN_1803574d0 / FUN_180353440 / FUN_1803538c0 / FUN_180353c70: the in-intervals */
    int cap = 16, ni = 0; C3ChkPos *I = malloc(sizeof(C3ChkPos) * 2 * cap);
    C3ChkPos st = { 0, 0.0, { P[0], P[1], P[2] } }, en = { n - 2, 1.0, { P[3 * n - 3], P[3 * n - 2], P[3 * n - 1] } }, op = st;
    int s = c3chk_in(c, P), open = s, first = 1, wrapped = 0;   /* first: the first crossing of the pass (FUN_180353c70's param_11) */
    for (int i = 0; i + 1 < n; i++) {
        const double *A = P + 3 * i, *B = A + 3;
        double L = sqrt((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2]));
        double q = (double)(int)(L / c->res); if (q <= 1.0) q = 1.0;
        double step = 1.0 / q; int m = (int)(1.0 / step + 0.5), j = 1; double up = 0.0;
        for (; j <= m; j++) {
            double u = j == m ? 1.0 : j * step, Q[3]; c3chk_at(P, i, u, Q);
            int g = c3chk_in(c, Q);
            if (g != s) {
                C3ChkPos x = c3chk_bisect(c, P, i, up, u);
                if (!open) { op = x; open = 1; }
                else {
                    if (first && closed && c3chk_eq(&op, &st)) { en = x; wrapped = 1; }   /* a closed pass that starts in: its first interval ends the last one (ts l.3929-3946) */
                    else { if (ni == cap) { cap *= 2; I = realloc(I, sizeof(C3ChkPos) * 2 * cap); } I[2 * ni] = op; I[2 * ni + 1] = x; ni++; }
                    open = 0;
                }
                first = 0; s = g;
            }
            up = u;
        }
    }
    if (open && !(wrapped && c3chk_eq(&op, &en))) { if (ni == cap) { cap *= 2; I = realloc(I, sizeof(C3ChkPos) * 2 * cap); } I[2 * ni] = op; I[2 * ni + 1] = en; ni++; }   /* FUN_1803574d0 ts l.6230-6256 */
    (void)wrapped;
    /* FUN_180356240 (param_6 = 0): touching intervals are one */
    for (int k = 0; k + 1 < ni; ) {
        if (c3chk_eq(&I[2 * k + 1], &I[2 * k + 2])) { I[2 * k + 1] = I[2 * k + 3]; memmove(I + 2 * k + 2, I + 2 * k + 4, sizeof(C3ChkPos) * 2 * (ni - k - 2)); ni--; }
        else k++;
    }
    /* FUN_1803569d0: the complement = what TrimmedOutPasses holds */
    double *buf = malloc(sizeof(double) * 3 * (2 * n + 4)); int np = 0, mm;
    #define OUT(a, b) do { mm = c3chk_piece(P, n, closed, (a), (b), buf); if (mm >= 2) { emit(buf, mm, ctx); np++; } } while (0)
    C3ChkPos pst = { 0, 0.0, { P[0], P[1], P[2] } }, pen = { n - 2, 1.0, { P[3 * n - 3], P[3 * n - 2], P[3 * n - 1] } };
    if (!ni) { emit(P, n, ctx); np = 1; }
    else {
        if (!closed && !c3chk_eq(&I[0], &pst)) OUT(&pst, &I[0]);
        for (int k = 0; k + 1 < ni; k++) OUT(&I[2 * k + 1], &I[2 * k + 2]);
        if (!closed) { if (!c3chk_eq(&I[2 * ni - 1], &pen)) OUT(&I[2 * ni - 1], &pen); }
        else if (!(c3chk_eq(&I[2 * ni - 1], &pen) && c3chk_eq(&I[0], &pst))) OUT(&I[2 * ni - 1], &I[0]);
    }
    #undef OUT
    free(buf); free(I);
    return np;
}

/* ------------------------------------------------------------------------------------------------ self test (ours, testing) */
/* cam_cli chktrim-test: c3_chk_trim on a flat square check (z = 0, x / y in [10, 20]) against analytic crossings: a ball (R = Rc = 3) with its tip
   at z = -1 touches the square's edge at a horizontal distance sqrt(R'^2 - (zc)^2) with R' = 3 + t and the centre zc = -1 - t + R' above the plane. */
typedef struct { int n; double a[8][3], b[8][3]; int np[8]; } C3ChkTestOut;
static void c3chk_test_emit(const double *xyz, int m, void *ctx) {
    C3ChkTestOut *o = ctx; if (o->n >= 8) return;
    memcpy(o->a[o->n], xyz, sizeof(double) * 3); memcpy(o->b[o->n], xyz + 3 * (m - 1), sizeof(double) * 3); o->np[o->n] = m; o->n++;
}
int cam3d_chktrim_selftest(FILE *fo) {
    C3Chk *c = calloc(1, sizeof *c);
    double tri[18] = { 10, 10, 0, 20, 10, 0, 20, 20, 0,   10, 10, 0, 20, 20, 0, 10, 20, 0 };
    c->M.n = 2; c->M.t = malloc(sizeof tri); memcpy(c->M.t, tri, sizeof tri); c3_mesh_box(&c->M);
    c->res = 0.5; c->tol = 1e-6;
    int bad = 0;
    for (int ti = 0; ti < 2; ti++) {
        double t = ti ? 0.5 : 0.0, Rp = 3 + t, zc = -1 - t + Rp, h = sqrt(Rp * Rp - zc * zc);
        c->t = t; if (ti) hd_free(&c->d); hd_build(&c->d, &c->M, 0.0, 3.0 + t);
        /* 1. an open line through the square */
        double L1[6] = { 0, 15, -1, 30, 15, -1 }; C3ChkTestOut o = { 0 };
        int np = c3_chk_trim(c, L1, 2, 0, c3chk_test_emit, &o);
        int ok = np == 2 && fabs(o.b[0][0] - (10 - h)) < 2e-6 && o.b[0][0] <= 10 - h + 1e-12 && fabs(o.a[1][0] - (20 + h)) < 2e-6 && o.a[1][0] >= 20 + h - 1e-12;
        fprintf(fo, "t %.1f line: %d pieces, cut at %.7f / %.7f (expected %.7f / %.7f) %s\n", t, np, np > 0 ? o.b[0][0] : 0, np > 1 ? o.a[1][0] : 0, 10 - h, 20 + h, ok ? "ok" : "FAIL"); bad += !ok;
        /* 2. a closed rectangle starting inside: two pieces, the one across the seam's crossing last (FUN_1803569d0) */
        double L2[18] = { 15, 14, -1, 30, 14, -1, 30, 16, -1, 0, 16, -1, 0, 14, -1, 15, 14, -1 }; C3ChkTestOut o2 = { 0 };
        np = c3_chk_trim(c, L2, 6, 1, c3chk_test_emit, &o2);
        ok = np == 2 && fabs(o2.a[0][0] - (10 - h)) < 2e-6 && fabs(o2.a[0][1] - 16) < 1e-9 && fabs(o2.b[0][0] - (10 - h)) < 2e-6 && fabs(o2.b[0][1] - 14) < 1e-9
             && fabs(o2.a[1][0] - (20 + h)) < 2e-6 && fabs(o2.a[1][1] - 14) < 1e-9 && fabs(o2.b[1][0] - (20 + h)) < 2e-6 && fabs(o2.b[1][1] - 16) < 1e-9 && o2.np[0] == 4 && o2.np[1] == 4;
        fprintf(fo, "t %.1f closed loop: %d pieces %s\n", t, np, ok ? "ok" : "FAIL"); bad += !ok;
        /* 3. a pass wholly in, one position in / out */
        double L3[6] = { 12, 12, -1, 18, 18, -1 }, P4[3] = { 15, 15, -1 }, P5[3] = { 40, 40, -1 }; C3ChkTestOut o3 = { 0 };
        int n3 = c3_chk_trim(c, L3, 2, 0, c3chk_test_emit, &o3), n4 = c3_chk_trim(c, P4, 1, 0, c3chk_test_emit, &o3), n5 = c3_chk_trim(c, P5, 1, 0, c3chk_test_emit, &o3);
        ok = n3 == 0 && n4 == 0 && n5 == 1;
        fprintf(fo, "t %.1f inside line / points: %d %d %d %s\n", t, n3, n4, n5, ok ? "ok" : "FAIL"); bad += !ok;
        /* 4. above the square by more than t: untouched */
        double L6[6] = { 0, 15, 0.25 - (ti ? 0 : 0.5), 30, 15, 0.25 - (ti ? 0 : 0.5) }; C3ChkTestOut o6 = { 0 };
        L6[2] = L6[5] = t + 1e-3; np = c3_chk_trim(c, L6, 2, 0, c3chk_test_emit, &o6);
        ok = np == 1 && o6.np[0] == 2;
        fprintf(fo, "t %.1f above by t + 0.001: %d piece %s\n", t, np, ok ? "ok" : "FAIL"); bad += !ok;
    }
    c3_chk_close(c);
    fprintf(fo, "%s\n", bad ? "FAILED" : "all ok");
    return bad;
}
