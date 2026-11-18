/* cam2d_area.c - 2D High Speed Area Mill, Area Clearance mode (record 0x1567 = 0 -> McHmm "Area Clearance Passes"
 * plan 0x1435). Included by cam2d.c. Spec: ../formats/area_mill.md 11.
 *
 * What is decoded (McHmm.dll, area_mill.c): the passes - positions between MinOffset and MaxOffset on the medial axis,
 * each pass the offset of the tool region (area_mill_passes); the profile-ramp entry parameters (n steps, drop per step,
 * scan forward, the fragment chosen by MinProfileRampDiameter).
 * What is MEASURED (buckle-v10 op 2 NCI, formats/area_mill.md 11.5) because McHmm's linker is NOT DECODED: inner
 * passes first; loops counter-clockwise for climb; the ramp starts 0.1 above the top and ends on the ramp loop at the
 * point nearest the first pass (further depth levels are decoded shifted copies, see gen_area_hmm). Links between passes: McHmm's linker is NOT DECODED; a straight feed at the
 * level only where it stays in the tool-centre region, else a retract to the feed plane (SAFE neutral link). Core Roughing (0x1567 = 1) is refused (cam2d_dyn.c gen_dynamic). */

static double am_loop_len(const CPath *c) {
    double L = 0;
    for (int i = 0; i < c->n; i++) { int j = (i + 1) % c->n; L += hypot(c->xy[2 * j] - c->xy[2 * i], c->xy[2 * j + 1] - c->xy[2 * i + 1]); }
    return L;
}

/* profile ramp onto loop c (already started at its end point E = c[0]): FUN_1801a9740 (decomp/hmm_linker.c
 * l.16624-16979): n = int((z0 - z) / ProfileRampDropPerStep + 1), at least 1; FUN_180174910 (decomp/hmm ramp1)
 * places the n positions ProfileRampScanForward apart along the fragment before its start. Defaults (FUN_180037920,
 * decomp/hmm_link: ProfileRampScanForward = 1.1 x ToolShaftRadius, ProfileRampDropPerStep = ScanForward x
 * tan(MaxRampAngle)); measured on buckle-v10 op 2: ScanForward 3.3 for D 6, drop 4.6 / 27 per 3.3 mm step.
 * The descent is linear along the path (the flat steps McHmm makes at the fragment seam are NOT DECODED). */
static void am_ramp(MB *b, const CPath *c, double z0, double z, double R, double ramp_deg, double feed) {
    double scan = 1.1 * R, drop = scan * tan(ramp_deg * M_PI / 180);
    int n = drop > 1e-9 ? (int)((z0 - z) / drop + 1.0) : 1; if (n < 1) n = 1;
    double L = n * scan, per = am_loop_len(c);
    if (per < 1e-9) { mv(b, 0, c->xy[0], c->xy[1], z, feed); return; }
    /* walk backwards from c[0] for L along the loop: the ramp starts there */
    int k = 0; double back = L, sx = c->xy[0], sy = c->xy[1];
    int idx = 0;                                       /* the ramp starts on edge (idx, idx+1) */
    for (int guard = 0; guard < 1000000; guard++) {
        int a = ((k - 1) % c->n + c->n) % c->n;        /* previous vertex */
        double ex = c->xy[2 * a], ey = c->xy[2 * a + 1], px = c->xy[2 * ((k % c->n + c->n) % c->n)], py = c->xy[2 * ((k % c->n + c->n) % c->n) + 1];
        double l = hypot(px - ex, py - ey);
        if (l >= back) { double t = l > 0 ? back / l : 0; sx = px + (ex - px) * t; sy = py + (ey - py) * t; idx = a; break; }
        back -= l; k--;
    }
    int steps = -k;                                    /* full edges between the start point and c[0] */
    mv(b, 0, sx, sy, z0, feed);
    double done = hypot(c->xy[2 * ((idx + 1) % c->n)] - sx, c->xy[2 * ((idx + 1) % c->n) + 1] - sy);
    int v = (idx + 1) % c->n;
    mv(b, 0, c->xy[2 * v], c->xy[2 * v + 1], z0 - (z0 - z) * fmin(1, done / L), feed);
    for (int s = 0; s < steps; s++) {
        int w = (v + 1) % c->n;
        done += hypot(c->xy[2 * w] - c->xy[2 * v], c->xy[2 * w + 1] - c->xy[2 * v + 1]);
        v = w;
        mv(b, 0, c->xy[2 * v], c->xy[2 * v + 1], z0 - (z0 - z) * fmin(1, done / L), feed);
    }
    if (v != 0) mv(b, 0, c->xy[0], c->xy[1], z, feed);
}

static int gen_area_hmm(const McamFile *f, CamOp *op, MB *b) {
    if (hst_refused(f, op)) return 0;
    CamParams *p = &op->p;
    double R = p->tool_dia / 2, tol = fmax(p->tol, p->tool_dia * 0.002), ztop;
    /* the machining region: closed chains minus the avoidance chains (as gen_dynamic builds it) */
    CPaths raw = chain_paths(f, op, 1, &ztop);
    if (!raw.n) { cl_free(&raw); snprintf(op->error, sizeof op->error, "needs closed chains"); return 0; }
    CPaths region = cl_union_evenodd(&raw); cl_free(&raw);
    double zav; CPaths av = chain_paths(f, op, 2, &zav);
    if (av.n) { CPaths au = cl_union_evenodd(&av); CPaths d = cl_bool(&region, &au, 2); cl_free(&region); region = d; cl_free(&au); }
    cl_free(&av);
    /* tool region: centre R + stock from the walls (the stock: FUN_1801ac7e0 region / islands, decomp/mcmill_hst2d.c l.17477-17522; R: ToolThicknessXY 0 +
       the kernel's tool, area_mill.c; confirmed by the measured outer pass clearance 4.000 for R 3 / stock 1) */
    CPaths T0 = cl_offset_nz(&region, -(R + p->stock_xy), tol * 0.25);
    cl_free(&region);
    if (!T0.n) { cl_free(&T0); snprintf(op->error, sizeof op->error, "the tool does not fit (D%.3f)", p->tool_dia); return 0; }
    double mn = p->am_min_off, mx = p->am_max_off;
    if (mx <= 0) { mx = p->tool_dia * 0.45; mn = mx * 0.55; }          /* mckernel FUN_18006b750 replacement rule */
    AreaMillPasses ps;
    if (!area_mill_passes(&T0, mn, mn, mx - mn, p->am_smoothing ? p->am_corner_off_tol : 0, tol * 0.25, !mcam_is_metric(f) ? 1 / 25.4 : 1.0, &ps) || !ps.n) {
        cl_free(&T0); snprintf(op->error, sizeof op->error, "no Area Clearance passes"); return 0; }
    /* where a link may stay down: the tool-centre region (walls / islands / stock kept), grown by 2 um for the passes on
       its boundary */
    CPaths Tlink = cl_offset_nz(&T0, 0.002, tol * 0.25);
    cl_free(&T0);
    if (!p->am_climb) for (int i = 0; i < ps.n; i++) for (int k = 0; k < ps.p[i].loops.n; k++) reverse_path(&ps.p[i].loops.p[k]);
    /* the ramp fragment: the first pass (in cutting order) whose MeasureCurveDiameter reaches MinProfileRampDiameter
       (FUN_1801af630 decomp/hmm ramp1 l.325-869 compares FUN_1802793e0 with param_5 = linker +0x11b4); none: the last */
    int rp = ps.n - 1;
    for (int i = 0; i < ps.n; i++) if (ps.p[i].loops.n && area_mill_curve_diameter(&ps.p[i].loops.p[0]) >= p->am_min_ramp_dia) { rp = i; break; }
    /* depth cuts: McHmm runs once per region at the first level, then the CHST2DWorkItemDCS copy shifts that region's
       motion down by d per further level (d += rough step for the first nr - 1 copies, += finish step 0x880 after them;
       FUN_1802b11f0 / FUN_1802bb260, see cam2d_dyn.c gen_dynamic_mcce); the approach / retract between levels are this
       generator's (as before) */
    double zl[200]; int nr_dc = 1; double st_dc = 0; int nz = hst_depths(p, zl, 200, &nr_dc, &st_dc);
    double fp = p->top_z + p->feed_plane;
    MB cut = { 0 };
    {
        double z = zl[0];
        /* first pass start: its first vertex; the ramp ends on the ramp loop at the point nearest it */
        CPath *first = &ps.p[0].loops.p[0];
        CPath rl = { malloc(sizeof(double) * 2 * ps.p[rp].loops.p[0].n), ps.p[rp].loops.p[0].n };
        memcpy(rl.xy, ps.p[rp].loops.p[0].xy, sizeof(double) * 2 * rl.n);
        rotate_to(&rl, first->xy[0], first->xy[1]);
        double z0 = p->top_z + 0.1;                                     /* measured: 0.1 above the previous level (the top) */
        am_ramp(&cut, &rl, z0, z, R, p->am_ramp_deg > 0 ? p->am_ramp_deg : 3.0, p->plunge_feed > 0 ? p->plunge_feed : p->feed);
        free(rl.xy);
        /* the passes, inner first; each loop from the point nearest the current position */
        for (int i = 0; i < ps.n; i++) for (int k = 0; k < ps.p[i].loops.n; k++) {
            CPath c = { malloc(sizeof(double) * 2 * ps.p[i].loops.p[k].n), ps.p[i].loops.p[k].n };
            memcpy(c.xy, ps.p[i].loops.p[k].xy, sizeof(double) * 2 * c.n);
            V3 e = lastp(&cut);
            if (!(i == 0 && k == 0)) rotate_to(&c, e.x, e.y);
            /* link. McHmm's general linker (EvaGeneralLinkerPlan 0x1803a0c50, CLinkMovementConnect 0x180774f60 with its
               model-checked "wandered" positions FUN_1801d4990, decomp/hmm_linkconnect.c; ProfileLinkRadius fillets) is
               NOT DECODED. SAFE neutral link: a straight feed at the level when it stays in the tool-centre region (no
               wall, island or stock-to-leave is touched), else up to the feed plane (z NAN = the feed plane, filled in
               per level below), across, and down at the plunge feed */
            if (seg_inside(&Tlink, e.x, e.y, c.xy[0], c.xy[1])) mv(&cut, 0, c.xy[0], c.xy[1], z, p->feed);
            else { mv(&cut, 1, e.x, e.y, NAN, 0); mv(&cut, 1, c.xy[0], c.xy[1], NAN, 0);
                   mv(&cut, 0, c.xy[0], c.xy[1], z, p->plunge_feed > 0 ? p->plunge_feed : p->feed); }
            cut_loop(&cut, &c, z, p->feed);
            free(c.xy);
        }
    }
    double d = 0;
    for (int lv = 0; lv < nz; lv++) {
        if (lv) d += lv - 1 < nr_dc - 1 ? st_dc : p->mc_depth_fs;
        V3 s0 = cut.m[0].p;
        /* approach: rapid above the ramp start, down to the feed plane, feed to the ramp top */
        mv(b, 1, s0.x, s0.y, p->clearance, 0);
        if (fp < p->clearance) mv(b, 1, s0.x, s0.y, fp, 0);
        for (int i = 0; i < cut.n; i++) mv(b, cut.m[i].rapid, cut.m[i].p.x, cut.m[i].p.y, isnan(cut.m[i].p.z) ? fp : cut.m[i].p.z - d, cut.m[i].feed);
        V3 e = lastp(b);
        mv(b, 1, e.x, e.y, lv + 1 < nz ? fp : p->clearance, 0);
    }
    free(cut.m); cl_free(&Tlink);
    area_mill_passes_free(&ps);
    return 1;
}
