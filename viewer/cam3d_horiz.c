/* cam3d_horiz.c - HST Horizontal Area (op 132, UI style 4, kernel style 3). Spec: ../formats/horizontal_area.md ("ha N.N").
 * Compiled inside cam2d.c's unit after cam3d_finish.c (cam3d_horiz.h head). Sources: decomp/hmm_zplan.c (hz), decomp/hst3d_triplane.c (tp),
 * decomp/mckernel_all.c (mk), decomp/horizarea/d1.c (gA), decomp/area_mill / hmm-vorlib (the pass positions, formats/area_mill.md 11.3).
 *
 * What runs (Mastercam's order, ha 0):
 *   1. the record values (FUN_180071314 style 3, ha 1) and the McHmm plan defaults (FUN_180313650, ha 2.3);
 *   2. the flat levels: CZInflexionsHorizontal over the drive mesh (FUN_18003dd10 / FUN_18003db30, tp l.1179-1405, DECODED ha 2.2) and the
 *      merge / keep of FUN_180411bd0 (hz l.15345-15510, ha 2.1);
 *   3. per flat, highest first: the tool-centre region (ha 3) and its Area Clearance pass positions (kind 4 = kind 2, FUN_180406b00 hp l.1811,
 *      area_mill_passes = formats/area_mill.md 11.3);
 *   4. depth cuts (CAxiallyOffsetPassesPlan ByOffset, ha 3.3).
 * NOT DECODED and refused (never guessed): Corner rounding (the loop smoothing FUN_180557f60), negative stock below the corner radius, the
 * McHmm linker (entries / links / order: ours, as gen_arearough - the SAFE links, labelled). */
#include "cam3d_horiz.h"

/* ---------------------------------------------------------------------------------------------- flat levels (ha 2) */
typedef struct { HorizBand *b; int n, cap; } HzBands;
static int hz_insert(HzBands *v, int pos, double lo, double hi, double a) {
    if (v->n == v->cap) { int c = v->cap ? 2 * v->cap : 64; HorizBand *nb = realloc(v->b, sizeof(HorizBand) * c); if (!nb) return 0; v->b = nb; v->cap = c; }
    memmove(v->b + pos + 1, v->b + pos, sizeof(HorizBand) * (v->n - pos));
    v->b[pos].zlo = lo; v->b[pos].zhi = hi; v->b[pos].area = a; v->n++;
    return 1;
}
/* FUN_18003db30 (tp l.1179-1280): bands sorted by z_hi; the narrowest existing band from the z_hi position on that contains
   [lo, hi] takes the area, else a new band is inserted there */
static int hz_add(HzBands *v, double lo, double hi, double a) {
    int pos = 0, n = v->n;
    { int a0 = 0, b0 = n; while (a0 < b0) { int m = (a0 + b0) / 2; if (v->b[m].zhi < hi) a0 = m + 1; else b0 = m; } pos = a0; }   /* first z_hi >= hi */
    if (pos > 0 && v->b[pos - 1].zhi == hi) pos--;                                  /* FUN_1800082c0: exact equality */
    int best = -1;
    for (int j = pos; j < n; j++)
        if (v->b[j].zlo <= lo && (best < 0 || v->b[j].zhi - v->b[j].zlo < v->b[best].zhi - v->b[best].zlo)) best = j;
    if (best >= 0) { v->b[best].area += a; return 1; }
    return hz_insert(v, pos, lo, hi, a);
}
int horiz_flats(const double *tri, int ntri, double zmin, double zmax, double grad, double merge, double amin, HorizBand *out, int max) {
    HzBands v = { 0 };
    for (int i = 0; i < ntri; i++) {
        const double *A = tri + 9 * i, *B = A + 3, *C = A + 6;
        double tzl = fmin(A[2], fmin(B[2], C[2])), tzh = fmax(A[2], fmax(B[2], C[2]));
        if (tzh < zmin || tzl > zmax) continue;                                        /* the grid box test (FUN_18003dd10, tp l.1355-1362) */
        /* normal components as decompiled (tp l.1363-1368): nz, the two horizontal ones */
        double nz = (B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0]);
        double n1 = (B[0] - A[0]) * (C[2] - A[2]) - (B[2] - A[2]) * (C[0] - A[0]);
        double n2 = (B[1] - A[1]) * (C[2] - A[2]) - (B[2] - A[2]) * (C[1] - A[1]);
        double h2 = n1 * n1 + n2 * n2, s2 = nz * nz + h2;
        if (!(grad > 0.0) || !(h2 <= s2 * grad * grad)) continue;                      /* |n_h| <= grad |n| (tp l.1369) */
        double area = sqrt(s2) * 0.5;                                                  /* the 3D triangle area (tp l.1383-1386) */
        if (area != 0.0 && !hz_add(&v, tzl, tzh, area)) { free(v.b); return -1; }
    }
    /* merge (hz l.15345-15405): scanning i = last .. 1, an overlap (z_lo(i) <= z_hi(i-1)) merges at once, else the smallest gap below
       `merge` (a later, lower i only when strictly smaller); band i goes into band i-1 (z_hi, area); repeat until none */
    for (;;) {
        int cand = -1;
        for (int i = v.n - 1; i >= 1; i--) {
            double gap = v.b[i].zlo - v.b[i - 1].zhi;
            if (gap <= 0) { cand = i; break; }
            if (merge <= gap || (cand >= 0 && v.b[cand].zlo - v.b[cand - 1].zhi <= gap)) continue;
            cand = i;
        }
        if (cand < 0) break;
        v.b[cand - 1].zhi = v.b[cand].zhi; v.b[cand - 1].area += v.b[cand].area;
        memmove(v.b + cand, v.b + cand + 1, sizeof(HorizBand) * (v.n - cand - 1)); v.n--;
    }
    /* keep (hz l.15406-15510): area >= HorizontalFlatAreaSize^2, appended from the last band to the first = highest first */
    int k = 0;
    for (int i = v.n - 1; i >= 0 && k < max; i--) if (v.b[i].area >= amin) out[k++] = v.b[i];
    free(v.b);
    return k;
}

/* McHmm defaults (FUN_180313650, gA l.30-113; ha 2.3). McHmm works in mm: unit = part units per mm.
   WeaveFlatTip / VorProfileFlatRad / WeaveCutterRadius / MachWeaveResolution: FUN_180310790 (hst3d_port_plan.md 4.5, c3_resolution). */
void horiz_defaults(double R, double Rc, double stock_z, double tol, double unit, HorizDefaults *d) {
    d->grad = 0.0001;                                            /* 0x1c68 */
    d->flat_area = (1.0 * unit) * (1.0 * unit);                  /* 0x1c69 DDV_MinDistance 1.0 (mm), squared at hz l.15409 */
    double eps = 0.6 * tol;                                      /* 0x1df7 HorizontalZEpsilonTolerance = 0.6 x StepMachTolerance */
    d->above = eps; d->below = eps;                              /* 0x1c6f / 0x1c70 */
    d->merge = 0.75 * (d->above + d->below);                     /* 0x1c6e */
    d->wall_out = 1.0 * unit;                                    /* 0x1c71 */
    d->flat_tip = Rc + stock_z <= 0.0;
    double fr = d->flat_tip ? 0.0 : fmax(0.0, R - Rc), wcr = R + stock_z - fr;
    double mwr = c3_resolution(R, Rc, stock_z, tol);
    d->tip_in = d->flat_tip ? -mwr - fmin(0.5 * unit, wcr * 0.8) : fr + 1.0 * unit;   /* 0x1c72 */
}

/* ---------------------------------------------------------------------------------------------- the op */
static int gen_horizarea(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "Horizontal Area here needs the Top tool plane at the origin"); return 0; }
    uint32_t rn = 0; uint8_t *rec = hst_record(f, p, NULL, &rn);
    if (!rec || rn < 0x1760) { free(rec); snprintf(op->error, sizeof op->error, "no Horizontal Area record (Mastercam's default op 132 style 4 is missing)"); return 0; }
    #define RD(o) c3d(rec, rn, o)
    #define RB(o) c3b(rec, rn, o)
    if (RB(0x16c3)) { free(rec); snprintf(op->error, sizeof op->error, "Horizontal Area: Corner rounding (ProfileSmoothing): NOT PORTED"); return 0; }
    int metric = mcam_is_metric(f);
    double unit = metric ? 1.0 : 1.0 / 25.4;
    double D = p->tool_dia > 0 ? p->tool_dia : RD(0x350), R = D * 0.5;
    double Rc = fmin(fmax(p->tool_dia > 0 ? p->corner_r : RD(0x358), 0.0), R);
    double mn = fmin(RD(0x12b8), RD(0x12c0)), mx = fmax(RD(0x12b8), RD(0x12c0));     /* MinOffset / MaxOffset (mk l.87994-88021, ha 1) */
    /* stock: ToolThicknessXY = TP+0x318 (0x13c0), ToolThicknessZ = min(TP+0x318, TP+0x320) (mk l.87501-87535, ha 1) */
    double sxy = RD(0x13c0), sz = fmin(RD(0x13c0), RD(0x13c8));
    double ztol = RD(0x12a0);
    /* StepMachTolerance = job +0xa0 = 0.5 x TP+0x220 (FUN_18006dd68 mk l.84714-84718; the default 0.025 / 0.001 mk l.84702-84705 when TP+0x220 <= 1e-12) */
    double tol = 0.5 * (ztol > 1e-12 ? ztol : (metric ? 0.025 : 0.001));
    int ndc = 0; memcpy(&ndc, rec + 0x1558, 4);                                        /* NumberOfOffsets = TP+0x44c (ha 1, 3.3) */
    double dstep = RD(0x13b8);                                                         /* its Stepdown TP+0x310 */
    int conventional = RB(0x16e4);
    double ramp_deg = RD(0x1338) * 180.0 / M_PI; if (!(ramp_deg > 0.01)) ramp_deg = 3.0;
    int zdep = RB(0x16c9); double zd1 = RD(0x1238), zd2 = RD(0x1240);
    free(rec);
    #undef RD
    #undef RB
    if (!(D > 0)) { snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); return 0; }
    if (!(mn > 1e-9)) { snprintf(op->error, sizeof op->error, "Horizontal Area: the XY stepover Minimum / Maximum must be > 0"); return 0; }
    if (fmin(sxy, sz) + Rc < -ztol) { snprintf(op->error, sizeof op->error, "negative stock to leave below the corner radius: NOT DECODED"); return 0; }
    C3Mesh M; char err[150];
    if (!c3_mesh_op(f, op, ztol > 1e-9 ? ztol : tol * 0.1, &M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); return 0; }
    /* PosZHi / PosZLo (hst3d_port_plan.md 6.7; style 3 is not in the mask 0x5b6, no corner-radius lowering, ha 1) */
    double zhi = M.hi[2], zlo = M.lo[2];
    if (sz > 0) zhi += sz; else if (sz < 0) zlo += sz;
    zhi += ztol; zlo -= ztol;
    if (zdep) { zhi = fmax(zd1, zd2); zlo = fmin(zd1, zd2); }
    HorizDefaults hd; horiz_defaults(R, Rc, sz, tol, unit, &hd);
    HorizBand fl[512];
    /* c = Eva+0x140 = ToolThicknessZ (0x1d20, FUN_1802f4c80, stored at 1802f2cff in FUN_1802f2340): the flats are searched in model Z over [PosZLo - c, PosZHi - c]
       (hz l.15327) and kept as (z_lo + c, z_hi + c) (hz l.15470-15510), the tool tip heights from there on. The weaves are at zA - c / zB - c (hz l.14945, 14957), the shape
       of c3_slice (centre = tip + Rc, radius Rc + c) is the same thing. */
    double c = sz;
    int nfl = horiz_flats(M.t, M.n, zlo - c, zhi - c, hd.grad, hd.merge, hd.flat_area, fl, 512);
    if (getenv("MCAM_3D_DEBUG")) cam3d_debug = 1;
    if (cam3d_debug) { fprintf(stderr, "horizarea: %d flats in %.5f..%.5f (merge %.6f, area >= %.6f)\n", nfl, zlo, zhi, hd.merge, hd.flat_area);
        for (int i = 0; i < nfl; i++) fprintf(stderr, "  flat %.6f .. %.6f area %.6f\n", fl[i].zlo, fl[i].zhi, fl[i].area); }
    if (nfl <= 0) { c3_mesh_free(&M); snprintf(op->error, sizeof op->error, nfl < 0 ? "out of memory" : "Horizontal Area: no flat areas found"); return 0; }
    int flat_tip = Rc + sz <= 0.0;
    double Rb = flat_tip ? 0.0 : Rc + sz, Fo = flat_tip ? R + sxy : (R - Rc) + sxy - sz;
    double spacing = c3_resolution(R, Rc, sz, tol); if (!(spacing > 0)) spacing = R;
    double arc_tol = fmax(tol * 0.5, 1e-4);
    double bx[4] = { M.lo[0] - R - sxy - 2 * spacing, M.lo[1] - R - sxy - 2 * spacing, M.hi[0] + R + sxy + 2 * spacing, M.hi[1] + R + sxy + 2 * spacing };
    CPaths box = { calloc(1, sizeof(CPath)), 1 }; box.p[0] = c3_box_path(bx[0] - R, bx[1] - R, bx[2] + R, bx[3] + R);
    double fp = p->top_z + p->feed_plane; if (fp < zhi + p->feed_plane) fp = zhi + p->feed_plane;
    double feed = p->feed > 0 ? p->feed : 100, plunge = p->plunge_feed > 0 ? p->plunge_feed : feed;
    /* the containment: the op's chains as tmptc.tmp holds them (c3_containment, S7) with the tool containment offset of FUN_180128390 as every
       op 132 (c3_tcb_offset: page 25624, |d| > 1e-4, warning 295 when nothing is left), else the drive's XY bounding box (Bounding Box Plan) */
    CPaths cont = { 0 };
    { char ce[150]; if (!c3_containment(f, op, c3_hst_tcb_type(op), &cont, ce, sizeof ce)) { snprintf(op->error, sizeof op->error, "%s", ce); cl_free(&box); c3_mesh_free(&M); return 0; } }
    { uint32_t rn2 = 0; uint8_t *r2 = hst_record(f, p, NULL, &rn2);
      if (r2) { int ok = c3_tcb_offset(&cont, r2, rn2, D, arc_tol); free(r2); if (!ok) { snprintf(op->error, sizeof op->error, C3_TCB_FAIL); cl_free(&box); cl_free(&cont); c3_mesh_free(&M); return 0; } } }
    if (!cont.n) { cont.p = calloc(1, sizeof(CPath)); cont.p[0] = c3_box_path(M.lo[0], M.lo[1], M.hi[0], M.hi[1]); cont.n = 1; }
    const char *dump = getenv("MCAM_HZ_DUMP");                                         /* testing: region / passes per flat */
    int npass = 0, down = 0;
    for (int k = 0; k < nfl; k++) {
        if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); cl_free(&box); cl_free(&cont); c3_mesh_free(&M); return 0; }
        if (getenv("MCAM_HZ_ONLY") && fabs(atof(getenv("MCAM_HZ_ONLY")) - fl[k].zhi) > 1e-4) continue;   /* testing: one flat */
        double zA = fl[k].zhi + c + hd.above, zB = fl[k].zlo + c - hd.below;           /* the two weaves (hz l.14912-14934, ha 3.1.4), kept bands = model + c */
        /* Weave space (ha 3.1): McHmm's weave carries the ball part (WeaveCutterRadius) only; the flat radius VorProfileFlatRad is applied
           later by the Voronoi pass (FUN_180407c10 hzp l.593-603, Eva +0xff8 = 0x1bee). A flat-tipped tool has VorProfileFlatRad = 0 and the
           whole radius in the weave. Fw = the offset inside the weave, Fin = the later Voronoi inset (c3_slice's F = Fw + Fin = Fo). */
        double Fw = flat_tip ? Fo : 0.0, Fin = flat_tip ? 0.0 : Fo;
        CPaths FA = c3_slice(&M, zA - c, Rb, zA + Rc, Fo, spacing, bx, arc_tol);   /* the free region at zA (kind 1, ha 3.2.2) */
        CPaths BW = c3_slice(&M, zB - c, Rb, zB + Rc, Fw, spacing, bx, arc_tol);   /* weave 2: blocked at zB, the weave's Z is zB - c (hz l.14957, the h <= 0 skip of FUN_1801295b0 uses it) */
        CPaths WA = c3_slice(&M, zA - c, Rb, zA + Rc, Fw, spacing, bx, arc_tol);   /* weave 1: blocked at zA */
        /* the runs (FUN_1804031a0 / FUN_1804039a0 hz l.6340-6641, ha 3.1.6): the contact that ends a fibre interval on weave 2 is above the flat's
           band (h >= zA - zB) -> wall run, else tip-edge run. A contact above zA also blocks the tool at zA, so a boundary point of BW is a wall
           run exactly when it lies on WA: tested with WA grown by e = the boundary shift of the two levels (ours: the polygon form of the decoded
           contact-height test; the fibres of both slices coincide, only a slanted wall moves by zA - zB). */
        double e = (zA - zB) + arc_tol;
        CPaths WAe = WA.n ? cl_offset_nz(&WA, e, arc_tol) : (CPaths){ 0 };
        CPaths wall = { 0 }, tip = { 0 };
        for (int i = 0; i < BW.n; i++) {
            const CPath *L = &BW.p[i]; if (L->n < 2) continue;
            char *isw = malloc(L->n);
            for (int j = 0; j < L->n; j++) { int j1 = (j + 1) % L->n; double mx2 = 0.5 * (L->xy[2 * j] + L->xy[2 * j1]), my2 = 0.5 * (L->xy[2 * j + 1] + L->xy[2 * j1 + 1]); isw[j] = in_region(&WAe, mx2, my2); }
            /* consecutive edges of one kind -> open polylines (FUN_1801192a0 / FUN_1801194d0 hz l.6380-6520) */
            int start = 0; for (int j = 1; j < L->n; j++) if (isw[j] != isw[j - 1]) { start = j; break; }
            for (int c = 0; c < L->n; ) { int a0 = (start + c) % L->n, kind = isw[a0], m = 1; while (c + m < L->n && isw[(start + c + m) % L->n] == kind) m++;
                CPath P = { malloc(sizeof(double) * 2 * (m + 1)), m + 1 };
                for (int q = 0; q <= m; q++) { int j = (a0 + q) % L->n; P.xy[2 * q] = L->xy[2 * j]; P.xy[2 * q + 1] = L->xy[2 * j + 1]; }
                CPaths *to = kind ? &wall : &tip; to->p = realloc(to->p, sizeof(CPath) * (to->n + 1)); to->p[to->n++] = P;
                c += m; }
            free(isw);
        }
        /* the edge pushes (FUN_180412ea0 hz l.16050-16104, ha 3.3): HorizontalWallEdgeOffsetOut swept along the wall runs; HorizontalTipEdgeOffsetIn
           along the tip-edge runs. The fibre toggles (ha 9.3, FUN_180125390: complement) make the wall push take its band out of the pocket; the tip
           push adds its band for TipEdgeOffsetIn > 0 (bull / ball) and takes it out for TipEdgeOffsetIn < 0 (flat tip): hz l.15014, 15083-15089, 15191. */
        CPaths P0 = BW.n ? cl_bool(&BW, NULL, 0) : (CPaths){ 0 };
        if (P0.n && wall.n && hd.wall_out > 0) { CPaths W2 = cl_offset_lines(&wall, hd.wall_out, 0, arc_tol), p2 = cl_bool(&P0, &W2, 2); cl_free(&W2); cl_free(&P0); P0 = p2; }
        if (P0.n && tip.n && hd.tip_in != 0) { CPaths B2 = cl_offset_lines(&tip, fabs(hd.tip_in), 0, arc_tol), p2 = cl_bool(&P0, &B2, hd.tip_in > 0 ? 0 : 2); cl_free(&B2); cl_free(&P0); P0 = p2; }
        CPaths FB = P0; P0 = (CPaths){ 0 };
        if (FB.n && Fin != 0.0) { CPaths o = cl_offset_nz(&FB, -Fin, arc_tol); cl_free(&FB); FB = o; }  /* the Voronoi inset by the flat radius */
        /* kind 4 pocket = weave 2 with the edge pushes (ha 3.2.1), and the tool must be free at zA (kind 1, ha 3.2.2-3) */
        CPaths T = FB.n ? cl_bool(&FB, &FA, 2) : (CPaths){ 0 };
        /* EnableTrimToBoundaries (FUN_1802ef450, ha 3.1 step 1): the ConstraintBoundaries plan of FUN_18007d7c8 mk l.92874-92915 = Bounding Box
           Plan 0x1425 (no containment chains; style 3 sets no OffsetDistance, FUN_180071120 mk l.86896 is style 0 only) or the chains' Boundaries */
        if (T.n) { CPaths t2 = cl_bool(&T, &cont, 1); cl_free(&T); T = t2; }
        cl_free(&WA); cl_free(&WAe); cl_free(&BW); cl_free(&wall); cl_free(&tip);
        cl_free(&FA); cl_free(&FB);
        double zc = fl[k].zhi + c;   /* the pass Z: slice +0x2d8 (= the kept band's z_hi, hz l.15209) -> FUN_180406b00's param_7 -> the drop object's Z +0x5c0 (hz l.8985; FUN_180407c10 passes param_1[0x286][0x5b]); oracle at c = 0: ha 9.2 */
        if (cam3d_debug) fprintf(stderr, "  flat %.5f: region %d loops, area %.5f\n", zc, T.n, T.n ? cl_total_area(&T) : 0.0);
        if (!T.n || cl_total_area(&T) < 1e-12) { cl_free(&T); continue; }
        AreaMillPasses ps;
        if (!area_mill_passes(&T, mn, mn, mx - mn, 0.0, arc_tol, unit, &ps)) { cl_free(&T); continue; }
        if (dump) { char fn[600]; snprintf(fn, sizeof fn, "%s_%.5f.txt", dump, zc); FILE *o = fopen(fn, "w");
            for (int i = 0; o && i < T.n; i++) { fprintf(o, "T %d\n", T.p[i].n); for (int j = 0; j < T.p[i].n; j++) fprintf(o, "%.6f %.6f\n", T.p[i].xy[2 * j], T.p[i].xy[2 * j + 1]); }
            for (int i = 0; o && i < ps.n; i++) for (int q = 0; q < ps.p[i].loops.n; q++) { CPath *L = &ps.p[i].loops.p[q]; fprintf(o, "P %d %.6f\n", L->n, ps.p[i].t);
                for (int j = 0; j < L->n; j++) fprintf(o, "%.6f %.6f\n", L->xy[2 * j], L->xy[2 * j + 1]); }
            if (o) fclose(o); }
        CPaths Tl = cl_offset_nz(&T, 0.002 * (metric ? 1 : 1 / 25.4), arc_tol);
        cl_free(&T);
        /* depth cuts (ha 3.3): CAxiallyOffsetPassesPlan, mckernel sets NumberOfOffsets and Stepdown only (mk l.93456-93500), ByOffset is the plan's default
           `PlanTypeNumber(MachiningPassesIn) != 0x147a` = true for 0x143a (gA l.404-414): FUN_18031f660 cuts each pass N + 1 times, i = N .. 0, before the next pass
           (gA l.880-890), at z + i x Stepdown (FUN_18031f160 -> CTessToolpath vtable +0x58 -> FUN_1804a7c60 decomp/proj/h3.c l.584 / l.723: z' = z + k s, raised) */
        int nrep = ndc >= 1 ? ndc : 0;
        for (int i = 0; i < ps.n; i++) for (int q = 0; q < ps.p[i].loops.n; q++) {
            CPath *L = &ps.p[i].loops.p[q]; if (L->n < 3) continue;
            if (ps.p[i].t == 0.0) {   /* ours: several grid components of one region each return its boundary pass t = 0 (area_mill_passes) - cut it once */
                int dup = 0;
                for (int i2 = 0; i2 < i && !dup; i2++) if (ps.p[i2].t == 0.0) for (int q2 = 0; q2 < ps.p[i2].loops.n; q2++) {
                    const CPath *O = &ps.p[i2].loops.p[q2];
                    if (O->n == L->n && !memcmp(O->xy, L->xy, sizeof(double) * 2 * L->n)) { dup = 1; break; } }
                if (dup) continue; }
            for (int rep = nrep; rep >= 0; rep--) {
                double z = zc + rep * dstep, z0 = z + 0.1 * (metric ? 1 : 1 / 25.4);
                CPath W = { malloc(sizeof(double) * 2 * L->n), L->n }; memcpy(W.xy, L->xy, sizeof(double) * 2 * L->n);
                if (conventional) reverse_path(&W);
                /* ours (as gen_arearough, hst3d_port_plan.md 9): closed passes, am_ramp entries, SAFE links */
                if (down) { V3 e = lastp(b); rotate_to(&W, e.x, e.y); }
                if (down && fabs(lastp(b).z - z) < 1e-9 && seg_inside(&Tl, lastp(b).x, lastp(b).y, W.xy[0], W.xy[1])) mv(b, 0, W.xy[0], W.xy[1], z, feed);
                else { if (down) { V3 e = lastp(b); mv(b, 1, e.x, e.y, fp, 0); }
                    mv(b, 1, W.xy[0], W.xy[1], fp, 0); mv(b, 1, W.xy[0], W.xy[1], z0, 0);
                    am_ramp(b, &W, z0, z, R, ramp_deg, plunge); }
                cut_loop(b, &W, z, feed); down = 1; npass++; free(W.xy);
            }
        }
        area_mill_passes_free(&ps); cl_free(&Tl);
    }
    cl_free(&box); cl_free(&cont); c3_mesh_free(&M);
    if (!npass || !b->n) { snprintf(op->error, sizeof op->error, "nothing to cut (%d flats)", nfl); return 0; }
    { V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance > fp ? p->clearance : fp, 0); }
    return 1;
}
