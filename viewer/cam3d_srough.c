/* cam3d_srough.c - classic Surface Rough toolpaths (mcmsurf.dll): Pocket (op 10) and Parallel (op 5). Included by cam2d.c after
 * cam3d_rough.c (the drive mesh, the slice) and cam2d_pock.c (the 2D Pocket engine). The decode is formats/surf_rough.md (cited
 * "sr 2.x"; "l.N" = decomp/mcmsurf_all.c). Record = the op record (op struct + 0x68).
 * Ours, labelled where used: the drive mesh (OCCT, as OptiRough); the per-level free region from cam3d_rough.c's slice (stands in for
 * mccore run_mctutl_zcut, NOT DECODED); the containment boolean (Clipper); the links between levels / regions (the 2D Pocket port's own
 * retracts; FUN_180290fb0's gap projection is NOT DECODED); Parallel's per-line profile (a drop of the stock-inflated tool on the mesh,
 * standing in for make_surf_model + the plane sections, NOT DECODED). */

static double sr_d(const uint8_t *r, uint32_t n, uint32_t o) { double v = 0; if (o + 8 <= n) memcpy(&v, r + o, 8); return v; }
static int sr_b(const uint8_t *r, uint32_t n, uint32_t o) { return o < n ? r[o] : 0; }
static int sr_s(const uint8_t *r, uint32_t n, uint32_t o) { int16_t v = 0; if (o + 2 <= n) memcpy(&v, r + o, 2); return v; }

/* the op's record: hst_record (the stored op, else the type's defaults op, with the HST-range patches) and every other record patch of the
   op dialog (the Surface parameters page edits heights 0x170.. and the stock 0x2e0, outside hst_record's ranges) */
static uint8_t *sr_record(const McamFile *f, const CamParams *p, uint32_t *rn) {
    uint8_t *r = hst_record(f, p, NULL, rn);
    if (!r) return NULL;
    for (int k = 0; k < p->nrpatch; k++) { int o = p->rpatch[k].off, m = p->rpatch[k].n; if (o >= 0 && (uint32_t)(o + m) <= *rn) memcpy(r + o, p->rpatch[k].v, m); }
    return r;
}

/* the Z levels (sr 2.2: FUN_1802a0d50 l.438183 + FUN_18029cee0 l.435887): the top value first, equal steps of at most the maximum
   stepdown down to the bottom, the bottom itself last; a range within the chord gives the top only. User depth lists are NOT DECODED
   (not read). Returns the count (>= 1) */
static int sr_levels(double top, double bot, double zdist, double chord, double *z, int max) {
    int k = 0; z[k++] = top;
    double dz = top - bot;
    if (dz <= chord || !(zdist > 0)) return k;
    int n = (int)ceil((dz - 1e-12) / zdist); double st = dz / n;
    for (int i = 1; i < n && k < max; i++) z[k++] = top - i * st;
    if (k < max) z[k++] = bot;
    return k;
}
/* top / bottom of the cut depths (sr 2.2, l.438245-438417): incremental (0x12e8) from the drive's Z range (maxZ - top adjustment 0x1300, or
   maxZ - stepdown with 0x1440; minZ + bottom adjustment 0x1308 + stock; top = max(top + stock, bottom)); absolute: max / min of 0x1310 / 0x1318,
   + stock with 0x1432; + corner radius on both with 0x12e9. Op 15 (surf_classic2.md 3.1, l.438263-438268 / l.438380): the stock flag is
   0x144e and keep-top 0x1440 is op 10's only (adj = the stock flag's offset, keeptop = 0 for op 15) */
static void sr_depths_x(const uint8_t *r, uint32_t n, double zmin, double zmax, double stock, double rc, double zdist, double *top, double *bot, uint32_t adj, int keeptop) {
    if (sr_b(r, n, 0x12e8)) {
        double t = keeptop && sr_b(r, n, 0x1440) ? zmax - zdist : zmax - sr_d(r, n, 0x1300);
        double b = zmin + sr_d(r, n, 0x1308) + stock;
        *top = fmax(t + stock, b); *bot = b;
    } else {
        double a = sr_d(r, n, 0x1310), c = sr_d(r, n, 0x1318);
        *top = fmax(a, c); *bot = fmin(a, c);
        if (sr_b(r, n, adj)) { *top += stock; *bot += stock; }
    }
    if (sr_b(r, n, 0x12e9)) { *top += rc; *bot += rc; }
}
static void sr_depths(const uint8_t *r, uint32_t n, double zmin, double zmax, double stock, double rc, double zdist, double *top, double *bot) {
    sr_depths_x(r, n, zmin, zmax, stock, rc, zdist, top, bot, 0x1432, 1);
}
/* the containment as tool-centre boundary: the op's chains as tmptc.tmp holds them (c3_containment: every chain of the op, type 0xff,
   FUN_1800135a0 l.13325-13337; an open chain closed by its start, FUN_180128390 l.196389-196416), offset (FUN_180128390 l.196444-196457):
   off = 0x12c0 (+ D/2 when 0x12b8 == 0); mode 0x12c8 (double: 0 Inside, 1 Center, 2 Outside); mode != 1 and |off| > 1e-4: offset inward
   by off (Inside) / outward (Outside). Without chains: the drive's XY box (ours: what the classic engines use without tmptc.tmp, after
   FUN_18012b7c0 removed it, is NOT DECODED). Returns 0 + op->error when the chains cannot be read. */
static int sr_containment(const McamFile *f, CamOp *op, const uint8_t *r, uint32_t n, double D, const C3Mesh *M, double arc_tol, CPaths *out) {
    CPaths reg = { 0 }; char ce[150];
    if (!c3_containment(f, op, 0xff, &reg, ce, sizeof ce)) { snprintf(op->error, sizeof op->error, "%s", ce); *out = reg; return 0; }
    if (reg.n) {
        double off = sr_d(r, n, 0x12c0) + (sr_b(r, n, 0x12b8) == 0 ? D * 0.5 : 0.0);
        int mode = (int)sr_d(r, n, 0x12c8);
        if (mode != 1 && fabs(off) > 1e-4) { CPaths o = cl_offset_nz(&reg, mode == 2 ? off : -off, arc_tol); cl_free(&reg); reg = o; }
    } else { reg.p = calloc(1, sizeof(CPath)); reg.p[0] = c3_box_path(M->lo[0], M->lo[1], M->hi[0], M->hi[1]); reg.n = 1; }
    *out = reg; return 1;
}
/* classic ops whose use of tmptc.tmp is not ported / decoded: refuse when the op has a containment (mcmsurf writes every chain of the op,
   FUN_1800135a0 l.13325-13337; type = 2 here for the ops whose own chains are curves: Project 7 / 0xd, where tmptc.tmp also receives the
   curves, unclosed, FUN_18012f750 l.200203-200209). 1 = refused (op->error set). */
static int sr_refuse_containment(const McamFile *f, CamOp *op, int type, const char *nm, const char *why) {
    int k = c3_count_type_chains(f, op, type);
    if (k < 0) { snprintf(op->error, sizeof op->error, "%s: the operation's chains cannot be rebuilt (missing geometry)", nm); return 1; }
    if (k > 0) { snprintf(op->error, sizeof op->error, "%s: containment - %s", nm, why); return 1; }
    return 0;
}

/* ---------------------------------------------------------------------------------------------------------- Pocket (op 10) */
static int gen_srough_pocket(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "Surface Rough Pocket here needs the Top tool plane at the origin"); return 0; }
    uint32_t rn = 0; uint8_t *r = sr_record(f, p, &rn);
    if (!r || rn < 0x1700) { free(r); snprintf(op->error, sizeof op->error, "no Surface Rough Pocket record (Mastercam's default op 10 is missing)"); return 0; }
    /* refusals (sr 2.4 / 2.6: what is NOT DECODED is not generated) */
    int method = sr_s(r, rn, 0x145c), rough = sr_b(r, rn, 0x1458);
    const char *why = NULL;
    if (rough && method == 7 && (sr_b(r, rn, 0x1478) || sr_d(r, rn, 0x1628) < 1))
        why = "High Speed with Spiral inside to outside or no channel runs mcmsurf's own High Speed engine (FUN_180376fd0 / FUN_180374bb0): NOT DECODED";
    else if (rough && method == 0 && sr_b(r, rn, 0x1420)) why = "quick zigzag (FUN_180375780): NOT DECODED";
    else if (sr_b(r, rn, 0x161a)) why = "facing: NOT DECODED";
    else if (sr_b(r, rn, 0x1441)) why = "auto-detect flats (FUN_1802e2040): NOT PORTED";
    else if (sr_b(r, rn, 0x1656)) why = "thin wall (FUN_180372a00): NOT DECODED";
    else if (sr_b(r, rn, 0x1640)) why = "spring passes: NOT DECODED";
    else if (sr_b(r, rn, 0x1434)) why = "align plunge entries (the pre-pass 0x1434): NOT DECODED";
    /* the roughing's keep tool down between depth cuts with op code 0xe (FUN_1801ede50 l.339478-339506, FUN_1801ef830 l.340363-340395):
       cur_op +0x800 (record 0x868) and +0x829 (0x891) of this op, entry off (pock_prm +0x78 = record 0x14c0): FUN_1801e0f80 back towards
       pock_start / pock_rough_start_pt at once after each level's roughing - those points are NOT DECODED (no stored op 10 has it) */
    else if (rough && sr_b(r, rn, 0x868) && sr_b(r, rn, 0x891) && !sr_b(r, rn, 0x14c0)) why = "keep tool down between depth cuts (0x891) with the entry off: the link to pock_rough_start_pt (FUN_1801ede50 l.339478-339506) NOT DECODED";
    if (why) { snprintf(op->error, sizeof op->error, "Surface Rough Pocket: %s", why); free(r); return 0; }
    double D = p->tool_dia > 0 ? p->tool_dia : sr_d(r, rn, 0x350), R = D * 0.5;
    double rc = fmin(fmax(p->tool_dia > 0 ? p->corner_r : sr_d(r, rn, 0x358), 0.0), R);
    double stock = sr_d(r, rn, 0x2e0), chord = sr_d(r, rn, 0x1240), zdist = sr_d(r, rn, 0x1248);
    if (!(chord > 1e-10)) chord = g_nc_tol;                          /* msurf_map l.340492: < 1e-10 -> nc_tol */
    if (!(zdist > 1e-10)) zdist = 0.05;                              /* l.340498 */
    if (!(D > 0)) { free(r); snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); return 0; }

    /* the drive mesh: tessellation 0.5 x msurf_chord_dev (FUN_180229ba0 param_4 = 1, l.364012; msurf_chord_dev = 0x1240 for op 10, msurf_map
       l.340017) */
    C3Mesh M; char err[150];
    if (!c3_mesh_op(f, op, chord * 0.5, &M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); free(r); return 0; }
    /* the tool model (sr 2.3, l.493058-493094): stock' = max(stock, -r), one stock for walls and floors; in the slice's terms (cam3d_rough.c)
       Rb = r + stock' (the ball, 0 = flat tip when <= 0.009 chord), F = R - r (flat radius; R + stock' for a flat tip) */
    double sp = fmax(stock, -rc), Rb = rc + sp, Fo = R - rc;
    if (Rb <= 0.009 * chord) { Rb = 0; Fo = R + sp; }
    double top, bot; sr_depths(r, rn, M.lo[2], M.hi[2], stock, rc, zdist, &top, &bot);
    double zl[400]; int nz = sr_levels(top, bot, zdist, chord, zl, 400);
    int up = sr_b(r, rn, 0x140a);                                     /* bottom to top (FUN_180302350 l.491569-491578) */
    double arc_tol = fmax(chord * 0.5, 1e-4), spacing = c3_resolution(R, rc, sp, chord);
    if (!(spacing > 0)) spacing = R;
    CPaths cont; if (!sr_containment(f, op, r, rn, D, &M, arc_tol, &cont)) { c3_mesh_free(&M); free(r); return 0; }
    double bx[4] = { 1e300, 1e300, -1e300, -1e300 };
    for (int i = 0; i < cont.n; i++) for (int k = 0; k < cont.p[i].n; k++) { bx[0] = fmin(bx[0], cont.p[i].xy[2 * k]); bx[1] = fmin(bx[1], cont.p[i].xy[2 * k + 1]); bx[2] = fmax(bx[2], cont.p[i].xy[2 * k]); bx[3] = fmax(bx[3], cont.p[i].xy[2 * k + 1]); }

    /* the 2D Pocket's parameters from the op's pocket block (record 0x1448 = the 2D Pocket's 0x1228 + 0x220, sr intro): the record with the
       block moved down, through params_from_record; method 7 (High Speed) -> 8 for the 2D engine, keep tool down (sr 2.4, FUN_1803769e0
       l.554653-554779; msurf_map case 10 l.340115-340206) */
    uint8_t *sh = malloc(rn); memcpy(sh, r, rn); memcpy(sh + 0x1228, r + 0x1448, 0x220);
    if (sr_s(r, rn, 0x145c) == 7) { int16_t m8 = 8; memcpy(sh + 0x123c, &m8, 2); }
    CamOp po; memset(&po, 0, sizeof po); po.p = *p;
    cam_params_from_raw((McamFile *)f, sh, rn, CAM_POCKET, &po);
    free(sh);
    po.p.type = CAM_POCKET; po.p.mc_depth_on = 0; po.p.stepdown = 0; po.p.stock_xy = 0; po.p.ntab_xy = 0;
    if (sr_b(r, rn, 0x15ca) && D - 2 * rc > 1e-9) po.p.stepover_pct *= (D - 2 * rc) / D;   /* "of flat" (dialog 242 1173 / 1174 = 0x15ca) */
    if (p->tool_dia > 0) { po.p.tool_dia = p->tool_dia; po.p.corner_r = p->corner_r; }
    /* the op's heights, feeds and spindle (the dialog's bound values; seeded from the same record's common fields) */
    po.p.clearance = p->clearance; po.p.retract_z = p->retract_z; po.p.feed_plane = p->feed_plane; po.p.top_z = p->top_z;
    po.p.feed = p->feed; po.p.plunge_feed = p->plunge_feed; po.p.rpm = p->rpm; po.p.tool_num = p->tool_num;

    int okall = 1, nlev = 0;
    for (int kk = 0; kk < nz && okall; kk++) {
        int k = up ? nz - 1 - kk : kk; double z = zl[k];
        if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); okall = 0; break; }
        CPaths forb = c3_slice(&M, z, Rb, z + rc, Fo, spacing, bx, arc_tol);
        CPaths fre = forb.n ? cl_bool(&cont, &forb, 2) : cl_bool(&cont, NULL, 0);
        cl_free(&forb);
        if (!fre.n) { cl_free(&fre); continue; }
        Gls reg = gls_from_cpaths(&fre); cl_free(&fre);
        CamOp lo = po; lo.p.final_z = z; lo.p.top_z = k > 0 && !up ? zl[k - 1] : (k + 1 < nz && up ? zl[k + 1] : p->top_z);
        lo.p.top_z = fmax(lo.p.top_z, z);
        g_pk_tc_in = &reg;
        int ok = gen_pocket_v2(f, &lo, b);
        g_pk_tc_in = NULL; gls_free(&reg);
        if (!ok) { snprintf(op->error, sizeof op->error, "level %.4f: %s", z, lo.error); okall = 0; break; }
        nlev++;
    }
    cl_free(&cont); c3_mesh_free(&M); free(r);
    if (!okall) return 0;
    if (!b->n) { snprintf(op->error, sizeof op->error, "nothing to cut (%d levels)", nz); return 0; }
    (void)nlev;
    return 1;
}

/* -------------------------------------------------------------------------------------------------------- Parallel (op 5) */
/* Decode: formats/surf_rough.md 3 and 8 (the op 5 stage 2 spec, cited "sp N"; l.N = decomp/mcmsurf_all.c).
   Frame (FUN_180129b40, sp 2): A = (cos a, sin a) the cut direction, u = position across the lines along -B = (sin a, -cos a); a point
   (u, a, z) is u (-B) + a A + z Z. */

/* ours: the drive as a height field (the max triangle Z per cell, spacing h) and the tip Z of the stock-inflated tool over it - stands in
   for make_surf_model + the plane sections of FUN_1801fd1e0 (NOT DECODED). Tool as the slice's: flat radius Fo, ball radius Rb (sr 2.3) */
typedef struct { double x0, y0, h; int nx, ny; float *z; } SrHf;
static void sr_hf_build_fill(const C3Mesh *M, double h, double pad, float fill, SrHf *H) {
    H->h = h; H->x0 = M->lo[0] - pad; H->y0 = M->lo[1] - pad;
    H->nx = (int)ceil((M->hi[0] - M->lo[0] + 2 * pad) / h) + 1; H->ny = (int)ceil((M->hi[1] - M->lo[1] + 2 * pad) / h) + 1;
    H->z = malloc(sizeof(float) * (size_t)H->nx * H->ny);
    /* cells off the model: the model's lowest Z (ours - Mastercam's offset-surface model carries the profile down the walls to their
       bottom edge; the lines reach stock + D/2 past the model, sp 2) */
    for (size_t i = 0; i < (size_t)H->nx * H->ny; i++) H->z[i] = fill;
    for (int t = 0; t < M->n; t++) {
        const double *P = M->t + 9 * t;
        double xl = fmin(P[0], fmin(P[3], P[6])), xh = fmax(P[0], fmax(P[3], P[6])), yl = fmin(P[1], fmin(P[4], P[7])), yh = fmax(P[1], fmax(P[4], P[7]));
        int i0 = (int)floor((xl - H->x0) / h), i1 = (int)ceil((xh - H->x0) / h), j0 = (int)floor((yl - H->y0) / h), j1 = (int)ceil((yh - H->y0) / h);
        if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0; if (i1 >= H->nx) i1 = H->nx - 1; if (j1 >= H->ny) j1 = H->ny - 1;
        double ex = P[3] - P[0], ey = P[4] - P[1], fx = P[6] - P[0], fy = P[7] - P[1], den = ex * fy - ey * fx;
        for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
            double x = H->x0 + i * h, y = H->y0 + j * h, z;
            if (fabs(den) > 1e-18) {
                double qx = x - P[0], qy = y - P[1], b1 = (qx * fy - qy * fx) / den, b2 = (ex * qy - ey * qx) / den;
                if (b1 < -1e-9 || b2 < -1e-9 || b1 + b2 > 1 + 1e-9) continue;
                z = P[2] + b1 * (P[5] - P[2]) + b2 * (P[8] - P[2]);
            } else z = fmax(P[2], fmax(P[5], P[8]));   /* a vertical triangle: its top over the cells of its box */
            float *c = &H->z[(size_t)j * H->nx + i]; if (z > *c) *c = (float)z;
        }
    }
}
static void sr_hf_build(const C3Mesh *M, double h, double pad, SrHf *H) { sr_hf_build_fill(M, h, pad, (float)M->lo[2], H); }
/* the tool tip Z at (x, y): max over the cells within Fo + Rb of h(q) - prof(r) (+ the floor stock, see sr 2.3); -1e30 = nothing under it */
static double sr_hf_tip(const SrHf *H, double x, double y, double Fo, double Rb, double s) {
    double rr = Fo + Rb, best = -1e30; int ir = (int)ceil(rr / H->h);
    int ci = (int)floor((x - H->x0) / H->h + 0.5), cj = (int)floor((y - H->y0) / H->h + 0.5);
    for (int j = cj - ir; j <= cj + ir; j++) { if (j < 0 || j >= H->ny) continue;
        for (int i = ci - ir; i <= ci + ir; i++) { if (i < 0 || i >= H->nx) continue;
            float hz = H->z[(size_t)j * H->nx + i]; if (hz < -1e29f) continue;
            double dx = H->x0 + i * H->h - x, dy = H->y0 + j * H->h - y, r = sqrt(dx * dx + dy * dy); if (r > rr) continue;
            double pr = r <= Fo ? 0.0 : Rb - sqrt(fmax(Rb * Rb - (r - Fo) * (r - Fo), 0.0));
            if (hz - pr > best) best = hz - pr; } }
    return best > -1e29 ? best + s : best;
}

typedef struct { double a0, z0, a1, z1; int type; } SrNode;
typedef struct { SrNode *n; int cnt, cap; } SrList;
static void srl_push(SrList *l, SrNode v) { if (l->cnt == l->cap) { l->cap = l->cap ? l->cap * 2 : 64; l->n = realloc(l->n, sizeof(SrNode) * l->cap); } l->n[l->cnt++] = v; }
static void srl_insert(SrList *l, int at, SrNode v) { srl_push(l, v); memmove(l->n + at + 1, l->n + at, sizeof(SrNode) * (l->cnt - 1 - at)); l->n[at] = v; }

/* FUN_1801fc390 (sp 7, l.335240): the band split of a chain at L and P; BELOW = L - 100 - d, ABOVE = P + 100 + d */
static void sr_band_split(SrList *c, double L, double P, double e, double d) {
    double BELOW = L - 100 - d, ABOVE = P + 100 + d;
    for (int i = 0; i < c->cnt; i++) {
        SrNode *n = &c->n[i];
        if (fabs(n->z0 - n->z1) < 2 * e) { double m = (n->z0 + n->z1) * 0.5; n->z0 = n->z1 = m; }
        if (fabs(n->z0 - L) <= e) n->z0 = L; if (fabs(n->z1 - L) <= e) n->z1 = L;
        if (fabs(n->z0 - P) <= e) n->z0 = P; if (fabs(n->z1 - P) <= e) n->z1 = P;
        double cuts[2]; int nc = 0;   /* level crossings strictly inside the segment, in order along it */
        double lv[2] = { L, P };
        for (int k = 0; k < 2; k++) { double Z = lv[k];
            if ((n->z0 < Z && n->z1 > Z) || (n->z0 > Z && n->z1 < Z)) cuts[nc++] = n->a0 + (n->a1 - n->a0) * (Z - n->z0) / (n->z1 - n->z0); }
        if (nc == 2 && ((cuts[0] - n->a0) * (n->a1 - n->a0) > (cuts[1] - n->a0) * (n->a1 - n->a0))) { double t = cuts[0]; cuts[0] = cuts[1]; cuts[1] = t; }
        if (nc) {   /* split into nc + 1 nodes (new nodes copy the type) */
            SrNode o = *n; double pa = o.a0, pz = o.z0; int at = i;
            for (int k = 0; k <= nc; k++) {
                double na = k < nc ? cuts[k] : o.a1, nz = k < nc ? o.z0 + (o.z1 - o.z0) * (na - o.a0) / (o.a1 - o.a0) : o.z1;
                if (k < nc) { double zz = fabs(nz - L) < fabs(nz - P) ? L : P; nz = zz; }
                SrNode q = { pa, pz, na, nz, o.type };
                if (k == 0) c->n[at] = q; else srl_insert(c, at + k, q);
                pa = na; pz = nz;
            }
            n = &c->n[i];
        }
        /* below L - e -> BELOW, above P - e -> ABOVE (a flat piece at P lifted too, l.335400-335410) */
        double zm = (n->z0 + n->z1) * 0.5;
        if (zm < L - e) { n->z0 = n->z1 = BELOW; }
        else if (zm > P - e && !(fabs(n->z0 - P) <= e && fabs(n->z1 - P) <= e && 0)) { if (zm > P + e || (fabs(n->z0 - P) <= e && fabs(n->z1 - P) <= e)) { n->z0 = n->z1 = ABOVE; } }
    }
}
/* FUN_1801f8870 (sp 8, l.332981): the pos-Z / neg-Z / plunge-control state machine on one chain. pz / nz: 1 = not allowed, 2 = allowed */
static void sr_state(SrList *c, double L, double P, double e, double chord, int pz, int nz, int pc, int second) {
    double ABOVE = P + 100 + 1, cur = L; int S = 0, above = 0;
    for (int i = 0; i < c->cnt; i++) if (c->n[i].z0 > P + e) above = 1;
    #define LIFT(n) do { (n)->z0 = (n)->z1 = ABOVE + 100; } while (0)
    for (int i = 0; i < c->cnt; i++) {
        SrNode *n = &c->n[i];
        int out = n->z0 < L - e || n->z0 > P + e, cls = out ? (n->z0 < (L + P) * 0.5 ? -1 : 1) : 0;
        int sl = n->z1 > n->z0 + e ? 1 : n->z1 < n->z0 - e ? -1 : 0;
        if (S == 2) { LIFT(n); continue; }
        if (S == 0) {
            if (cls == -1) { n->z0 = n->z1 = L; cur = L; S = 1; continue; }
            if (cls == 1) { if (pc) S = 2; continue; }
            if (pc && fabs(n->z1 - n->z0) < 0.05 * chord) {   /* near-flat: flattened at the higher end */
                double m = fmax(n->z0, n->z1); n->z0 = n->z1 = m;
                if (pz != 2 && nz != 2 && n->z0 >= L + e) { LIFT(n); if (pc) S = 2; } else { cur = n->z1; S = 1; }
                continue;
            }
            if ((sl > 0 && pz == 2) || (sl < 0 && nz == 2)) { cur = n->z1; S = 1; continue; }
            if (sl == 0) { if (n->z0 >= L + e && pz != 2 && nz != 2) { LIFT(n); if (pc) S = 2; } else { cur = n->z1; S = 1; } continue; }
            LIFT(n); if (pc) S = 2; continue;
        }
        /* S == 1 */
        if (cls == 1) { S = pc ? 2 : 0; continue; }
        if (cls == -1) { double z = nz == 2 ? L : cur; n->z0 = n->z1 = z; continue; }
        if (sl == 0) {
            if (pz == 1 && nz == 1) { if (n->z0 >= L + e) LIFT(n); else n->z0 = n->z1 = L; }
            else if (pz == 1 && nz == 2) { if (n->z0 > cur + e) LIFT(n); else cur = n->z1; }
            else { if (nz == 1 && n->z0 < cur - e) n->z0 = n->z1 = cur; cur = n->z1; }
        } else if (sl > 0) {
            if (pz == 1 && nz == 1) { LIFT(n); S = pc ? 2 : 0; }
            else if (pz == 1 && nz == 2) {
                if (n->z1 < cur + e) { n->z0 = n->z1; cur = n->z1; }
                else if (n->z0 >= cur - e) LIFT(n);
                else { double a = n->a0 + (n->a1 - n->a0) * (cur - n->z0) / (n->z1 - n->z0); SrNode up = { a, cur, n->a1, n->z1, n->type };
                       n->a1 = a; n->z0 = n->z1 = cur; srl_insert(c, i + 1, up); }
            } else if (pz == 2 && nz == 1) {
                if (n->z1 < cur + e) n->z0 = n->z1 = cur;
                else if (n->z0 < cur - e) { double a = n->a0 + (n->a1 - n->a0) * (cur - n->z0) / (n->z1 - n->z0); SrNode up = { a, cur, n->a1, n->z1, n->type };
                       n->a1 = a; n->z0 = n->z1 = cur; srl_insert(c, i + 1, up); }
                else cur = n->z1;
            }   /* pz 2, nz 2: no change, cur as it was (taken literally, sp 8) */
        } else {
            if (pz == 1) { if (nz == 2) { if (n->z0 > cur + e) LIFT(n); else cur = n->z1; } else LIFT(n); }
            else if (nz == 2) cur = n->z1;
            else { if (pc == 2) { S = 2; LIFT(n); } else if (n->z1 <= cur + e) n->z0 = n->z1 = cur; else { n->z1 = n->z0; cur = n->z0; } }
        }
    }
    if (second && !above) {   /* post-pass: lift after the last in-band non-flat node (all if none) */
        int last = -1; for (int i = 0; i < c->cnt; i++) { SrNode *n = &c->n[i]; if (!(n->z0 < L - e || n->z0 > P + e) && fabs(n->z1 - n->z0) > e) last = i; }
        for (int i = last + 1; i < c->cnt; i++) LIFT(&c->n[i]);
    }
    #undef LIFT
}

/* finish = 1: Surface Finish Parallel (op 11): msurf_map case 0xb (l.340207-340212) reads the stepover / angle / cutting method / approximate
   start point from op +0x1298 / +0x12a0 / +0x12a8 / +0x12aa (record 0x1300 / 0x1308 / 0x1310 / 0x1312) and leaves levels off (DAT_1805034a2,
   the posz / negz / plunge block unset): FUN_180201660 runs the lines once (l.337851) and FUN_1801ee8c0 cuts every node but the gaps
   (l.328398 / 328456); with Z limits (msurf_use_zlimits = op +0x1280 = record 0x12e8, limits 0x12f0 / 0x12f8, l.340427-340429) the band split
   between them only (l.328399-328418) and the in-band nodes */
/* ---------------------------------------------------------------- the containment crossings of a Parallel line (S7) */
/* FUN_180229840 (decomp/mcmsurf_all.c): sorted insert (the first argument, a tolerance, is not used) */
static void sr_x_ins(double v, double **a, int *n, int *cap) {
    if (*n + 2 >= *cap) { *cap = *cap ? *cap * 2 : 16; *a = realloc(*a, sizeof(double) * *cap); }
    for (int i = 0; i < *n; i++) if (v < (*a)[i]) { double t = (*a)[i]; (*a)[i] = v; v = t; }
    (*a)[(*n)++] = v;
}
static void sr_x_del2(double *a, int *n, int at) { for (int i = at + 2; i < *n; i++) a[i - 2] = a[i]; *n -= 2; }   /* drop a[at], a[at + 1] */
/* FUN_180229890: a run of chain points lying on the line, [lo, hi], as an interval: nothing when a pair already covers it (within tol);
   pairs inside it removed; inserted before the first pair whose midpoint is not below its own; joined with the previous / next pair when
   the gap is under tol */
static void sr_x_run(double lo, double hi, double tol, double **a, int *n, int *cap) {
    for (int i = 0; i + 1 < *n; i += 2) if ((*a)[i] - tol < lo && hi < tol + (*a)[i + 1]) return;
    for (int i = 0; i + 1 < *n; i += 2) if (lo - tol < (*a)[i] && (*a)[i + 1] < hi + tol) { sr_x_del2(*a, n, i); i -= 2; }
    int pos = 0;
    while (pos < *n && !((lo + hi) * 0.5 <= ((*a)[pos + 1] + (*a)[pos]) * 0.5)) pos += 2;
    if (*n + 2 >= *cap) { *cap = *cap ? *cap * 2 : 16; *a = realloc(*a, sizeof(double) * *cap); }
    for (int i = *n - 1; i >= pos; i--) (*a)[i + 2] = (*a)[i];
    (*a)[pos] = lo; (*a)[pos + 1] = hi; *n += 2;
    if (pos < 1 || tol <= (*a)[pos] - (*a)[pos - 1]) pos++;
    else { sr_x_del2(*a, n, pos - 1); pos--; }
    if (pos < *n - 1 && (*a)[pos + 1] - (*a)[pos] < tol) sr_x_del2(*a, n, pos);
}
/* FUN_180267000 (l.403956) as FUN_180226810 calls it (mode 0) for each line of the Parallel sections (FUN_1801fd1e0 l.336331-336360): the line
   at cross coordinate c against every tmptc.tmp chain (points (cross, along) in the cut frame; here (u, a) of sr_parallel, the chain closed by its
   first point as FUN_180128390 writes it); tol = 0.1 x curve_chord_dev. Per chain: the first point off the line (|u - c| > tol) starts a walk
   around the chain; a point on the line keeps its a as the pending crossing; a change of side adds a crossing - the pending point's a, else
   the interpolated a (or the new point's a when the edge spans less than tol across). After each chain the pairs shorter than tol are removed,
   and every run of points on the line longer than tol is added as an interval (FUN_180229890). Returns the count (the array's), *nx = the
   crossing count made even; -1 = a chain lying on the line entirely (the decompile's branch LAB_1802673c6 reads an index past local_d8: NOT
   DECODED). */
static int sr_x_line(const CPaths *cont, double sa, double ca, double c, double tol, double **out, int *nx) {
    double *a = NULL; int n = 0, cap = 0; *nx = 0;
    for (int ci = 0; ci < cont->n; ci++) {
        int m = cont->p[ci].n; if (m < 1) continue;
        int last = m;   /* the points 0 .. m with point m = point 0 (the closing point) */
        #define PU(k) (cont->p[ci].xy[2 * ((k) % m)] * sa - cont->p[ci].xy[2 * ((k) % m) + 1] * ca)
        #define PA(k) (cont->p[ci].xy[2 * ((k) % m)] * ca + cont->p[ci].xy[2 * ((k) % m) + 1] * sa)
        int s0 = 0; while (s0 <= last && fabs(PU(s0) - c) <= tol) s0++;
        if (s0 > last) { free(a); *out = NULL; return -1; }
        int side = PU(s0) < c ? -1 : 1, pending = 0, online = 0, j = s0; double pu = PU(s0), pa = PA(s0), v = 0;
        do {
            if (++j > last) j = 0;
            double cu = PU(j), cav = PA(j); int ns;
            if (fabs(cu - c) <= tol) { pending = 1; online = 1; v = cav; ns = side; }
            else {
                ns = cu < c ? -1 : 1;
                if (ns != side) {
                    if (!pending) { double d = cu - pu; v = fabs(d) >= tol ? pa + (cav - pa) * ((c - pu) / d) : cav; }
                    (*nx)++; sr_x_ins(v, &a, &n, &cap);
                }
                pending = 0; pu = cu; pa = cav;
            }
            side = ns;
        } while (j != s0);
        if (*nx > 0 && (*nx & 1)) (*nx)--;
        for (int k = 0; k + 1 < n; k += 2) if (a[k + 1] - a[k] < tol) { sr_x_del2(a, &n, k); k -= 2; }
        if (online) {
            int run = 0; double lo = 0, hi = 0; j = s0;
            do {
                if (++j > last) j = 0;
                double cu = PU(j), cav = PA(j);
                if (tol < fabs(cu - c)) { if (run && tol < hi - lo) { *nx += 2; sr_x_run(lo, hi, tol, &a, &n, &cap); } run = 0; }
                else if (run) { if (cav < lo) lo = cav; else if (hi < cav) hi = cav; }
                else { lo = hi = cav; run = 1; }
            } while (j != s0);
        }
        #undef PU
        #undef PA
    }
    *out = a; return n;
}

/* ---------------------------------------------------------------------------------------------- Parallel (op 5 / op 11) */
static int sr_parallel(const McamFile *f, CamOp *op, MB *b, int finish) {
    CamParams *p = &op->p;
    int radial = finish == 2;                            /* Surface Finish Radial (op 12): the lines are rays from the picked point (below) */
    const char *nm = radial ? "Surface Finish Radial" : finish ? "Surface Finish Parallel" : "Surface Rough Parallel";
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "%s here needs the Top tool plane at the origin", nm); return 0; }
    uint32_t rn = 0; uint8_t *r = sr_record(f, p, &rn);
    if (!r || rn < 0x1700) { free(r); snprintf(op->error, sizeof op->error, "no %s record (Mastercam's default op %d is missing)", nm, radial ? 12 : finish ? 11 : 5); return 0; }
    const char *why = NULL;
    if (!radial && sr_b(r, rn, finish ? 0x1312 : 0x13ea)) why = "the approximate start point (FUN_1801ee220) is not ported";
    else if (radial && op->nholes < 1) why = "the radial point is needed (Select radial point)";
    else if (sr_b(r, rn, 0x12a5)) why = "plunge into previous cut (FUN_1801ee720): NOT DECODED";
    else if (sr_b(r, rn, 0x12a4)) why = "optimize cut order: NOT DECODED";
    /* comp_to_tip = 0x1239 (1 = Tip, 0 = Center: the HST pages' tip comp combo, index = (byte == 0)); with absolute depths and Tip the levels
       get + f00, a machine-model offset whose meaning is NOT DETERMINED (sp 3, l.337855-337858) */
    else if (!finish && !sr_b(r, rn, 0x12e8) && sr_b(r, rn, 0x1239)) why = "absolute cut depths with tip comp Tip (the model offset f00): NOT DECODED";
    else if (!sr_b(r, rn, 0x1239) && (p->tool_dia > 0 ? p->corner_r : sr_d(r, rn, 0x358)) > 1e-9) why = "tip comp Center with a corner radius: NOT PORTED";
    if (why) { snprintf(op->error, sizeof op->error, "%s: %s", nm, why); free(r); return 0; }
    /* containment (S7, formats/surface_selection.md 8.2): ported for Parallel (below); Radial's rays (FUN_18013ae60) read it in code not traced: refused */
    int hascont = c3_count_type_chains(f, op, 0xff);
    if (hascont < 0) { snprintf(op->error, sizeof op->error, "%s: the operation's chains cannot be rebuilt (missing geometry)", nm); free(r); return 0; }
    if (radial && sr_refuse_containment(f, op, 0xff, nm, "its use by the radial rays (FUN_18013ae60) is NOT DECODED")) { free(r); return 0; }
    /* msurf_follow_tc_bdy = op +0x123e = record 0x12a6 (msurf_map l.340035): with it FUN_1801fd1e0 also follows the containment (FUN_180226340,
       l.337761-337790): NOT DECODED */
    if (hascont && sr_b(r, rn, 0x12a6)) { snprintf(op->error, sizeof op->error, "%s: containment with \"follow the containment boundary\" (record 0x12a6, FUN_180226340): NOT DECODED", nm); free(r); return 0; }
    double D = p->tool_dia > 0 ? p->tool_dia : sr_d(r, rn, 0x350), R = D * 0.5;
    double rc = fmin(fmax(p->tool_dia > 0 ? p->corner_r : sr_d(r, rn, 0x358), 0.0), R);
    double stock = sr_d(r, rn, 0x2e0), chord = sr_d(r, rn, 0x1240), zdist = sr_d(r, rn, 0x1248), sd = fabs(sr_d(r, rn, finish ? 0x1300 : 0x13d8));
    if (!(chord > 1e-10)) chord = g_nc_tol; if (!(zdist > 1e-10)) zdist = 0.05; if (!(sd > 1e-10)) sd = 0.05;   /* msurf_map l.340492 / 340498 / 340495 */
    double ang = sr_d(r, rn, finish ? 0x1308 : 0x13e0); int method = sr_s(r, rn, finish ? 0x1310 : 0x13e8);
    if (radial) { ang = 0; method = sr_s(r, rn, 0x1308); }   /* op 12: msurf_map case 0xc (l.340213-340221): cutting method 0x1308 */
    int zlim = finish && sr_b(r, rn, 0x12e8); double zl1 = sr_d(r, rn, 0x12f0), zl2 = sr_d(r, rn, 0x12f8);
    int pz = sr_b(r, rn, 0x13d1) ? 2 : 1, nz = sr_b(r, rn, 0x13d0) ? 2 : 1, pc = sr_b(r, rn, 0x13d2);   /* FUN_1802022d0 l.337910 */
    if (finish) { pz = nz = 2; pc = 0; }   /* op 11: the posz / negz / plunge block is not read (msurf_map case 0xb breaks before it) */
    double gap = sr_b(r, rn, 0x1288) ? sd * sr_d(r, rn, 0x1298) * 0.01 : sr_d(r, rn, 0x1290);
    if (radial && sr_b(r, rn, 0x1288)) gap = D * sr_d(r, rn, 0x1298) * 0.01;   /* msurf_map l.340445-340447: 0xc / 0xd -> pct x op +0x2e8 (D) */   /* l.340450 (the % as percent: ours, NOT DETERMINED) */
    /* FUN_1801fd1e0 l.336147-336153 (decomp/mcmsurf_all.c, the band globals): f68 = chord x 0.9, f78 = f68 x 0.01, f80 = min(tol_exref, f68 x 0.0001) - tol_exref
       = the system tolerance (config 1000, mc_systol) */
    double tol = CAM_SYS_TOL, f68 = 0.9 * chord, f78 = 0.009 * chord, f80 = fmin(tol, 0.00009 * chord);
    if (!(D > 0)) { free(r); snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); return 0; }
    C3Mesh M; char err[150];
    if (!c3_mesh_op(f, op, chord * 0.5, &M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); free(r); return 0; }
    double sp = fmax(stock, -rc), Rb = rc + sp, Fo = R - rc;
    if (Rb <= 0.009 * chord) { Rb = 0; Fo = R + sp; }
    /* the containment as tmptc.tmp holds it, offset (sr_containment: FUN_18012f750 / FUN_180128390 with the classic 0x12b8 / 0x12c0 / 0x12c8) */
    CPaths cont = { 0 };
    if (hascont && !sr_containment(f, op, r, rn, D, &M, fmax(chord * 0.5, 1e-4), &cont)) { c3_mesh_free(&M); free(r); return 0; }
    double hcell = fmin(fmax(chord * 4, R / 16), R / 4);   /* ours: the height field spacing */
    SrHf H; sr_hf_build(&M, hcell, Fo + Rb + hcell, &H);
    double ca = cos(ang), sa = sin(ang);
    /* each line: origin (lox, loy) + a (ldx, ldy) - parallel: u (-B) + a A; radial: the centre + a (cos t, sin t) */
    double *lox = NULL, *loy = NULL, *ldx = NULL, *ldy = NULL;
    #define WX(li, a) (lox[li] + (a) * ldx[li])
    #define WY(li, a) (loy[li] + (a) * ldy[li])
    /* the model's u / a ranges (the mesh's corners in the frame) */
    double umin = 1e300, umax = -1e300, amin = 1e300, amax = -1e300;
    for (int i = 0; i < M.n * 3; i++) { double x = M.t[3 * i], y = M.t[3 * i + 1], u = x * sa - y * ca, a = x * ca + y * sa;
        umin = fmin(umin, u); umax = fmax(umax, u); amin = fmin(amin, a); amax = fmax(amax, a); }
    double wid = fabs(stock) + D * 0.5;
    double emin = umin - wid, emax = umax + wid;
    double s = sd, u0 = 0; int count, start_out = 0;
    double a0 = amin - wid, a1 = amax + wid;
    if (!radial) {
        /* line layout (FUN_180133860 l.202481-202507, sp 2); first / last from the boundary edges: the mesh's u range (ours, flags 8 / 9 set) */
        double lo = umin - sd / 2, hi = umax + sd / 2, uend; int n = (int)ceil((hi - lo) / s);
        if (n < 2) { count = 1; u0 = uend = (lo + hi) / 2; }
        else { s = (hi - lo) / n; count = n + 1; u0 = hi; uend = lo;
            while (emax > u0) { u0 += s; count++; } while (uend > emin) { uend -= s; count++; } }
        if (count > 20000) { snprintf(op->error, sizeof op->error, "too many lines"); free(H.z); cl_free(&cont); c3_mesh_free(&M); free(r); return 0; }
        lox = malloc(sizeof(double) * count); loy = malloc(sizeof(double) * count); ldx = malloc(sizeof(double) * count); ldy = malloc(sizeof(double) * count);
        for (int i = 0; i < count; i++) { double u = u0 - i * s; lox[i] = u * sa; loy[i] = -u * ca; ldx[i] = ca; ldy[i] = sa; }
        if (hascont) {
            /* FUN_180133860 l.202508-202612 (DAT_1805039d4 = a containment): FUN_18013ebb0 (l.208686) takes the extent of tmptc.tmp's points in the
               frame (DAT_1804e8eb8 / ec0 across the lines); every line outside [eb8, ec0] is dropped (the first ones move the start, l.202512-202590);
               none left -> one line at the middle of the extent (l.202591-202594). Here: the extent of the offset containment in the line coordinate u */
            double cmin = 1e300, cmax = -1e300;
            for (int i = 0; i < cont.n; i++) for (int k = 0; k < cont.p[i].n; k++) { double u = cont.p[i].xy[2 * k] * sa - cont.p[i].xy[2 * k + 1] * ca; cmin = fmin(cmin, u); cmax = fmax(cmax, u); }
            int nk = 0;
            for (int i = 0; i < count; i++) { double u = u0 - i * s; if (u < cmin || u > cmax) continue; lox[nk] = u * sa; loy[nk] = -u * ca; nk++; }
            if (!nk) { double u = (cmin + cmax) * 0.5; lox[0] = u * sa; loy[0] = -u * ca; nk = 1; }
            count = nk;
        }
    } else {
        /* the rays (FUN_18013ae60, decomp/mcmsurf_all.c l.206862-207046): n = ceil(|sweep| / step - 1e-6) (step = max angle increment 0x1300,
           sweep 0x1318), + 1 when |sweep| < 2 pi; the start angle 0x1310 into [0, 2 pi); ray i at start + i x sweep / ceil(..); centre = the
           picked radial point (world_to_view of the Top C-plane: as is). Each ray from the start offset distance 0x1320 (DAT_1805034b0) to the
           model's farthest point + D / 2 + stock (ours: Mastercam's sections FUN_1802aed20 are NOT DECODED); Start inside / outside =
           record 0x130a (DAT_1805034a4 = (0x130a == 0): start outside, ours as read from the radio order 812 / 814) */
        double step_a = fabs(sr_d(r, rn, 0x1300)), sweep = sr_d(r, rn, 0x1318), st = sr_d(r, rn, 0x1310), r0 = sr_d(r, rn, 0x1320);
        if (!(step_a > 1e-9)) { snprintf(op->error, sizeof op->error, "%s: max angle increment must be > 0", nm); free(H.z); c3_mesh_free(&M); free(r); return 0; }
        double nd = ceil(fabs(sweep / step_a) - 1e-6); int nl = (int)nd; if (fabs(sweep) < 6.283184307179586) nl++;
        if (nl < 1 || nl > 20000) { snprintf(op->error, sizeof op->error, "%s: too many lines", nm); free(H.z); c3_mesh_free(&M); free(r); return 0; }
        while (st >= 6.283185307179586) st -= 6.283185307179586; while (st < 0) st += 6.283185307179586;
        double cx = op->holes[0].x, cy = op->holes[0].y, rmax = 0;
        for (int i = 0; i < M.n * 3; i++) rmax = fmax(rmax, hypot(M.t[3 * i] - cx, M.t[3 * i + 1] - cy));
        count = nl; a0 = fmax(r0, 0); a1 = rmax + wid; start_out = sr_b(r, rn, 0x130a) == 0;
        if (a1 <= a0) { snprintf(op->error, sizeof op->error, "%s: the start offset is beyond the model", nm); free(H.z); c3_mesh_free(&M); free(r); return 0; }
        lox = malloc(sizeof(double) * count); loy = malloc(sizeof(double) * count); ldx = malloc(sizeof(double) * count); ldy = malloc(sizeof(double) * count);
        for (int i = 0; i < count; i++) { double t = st + i * (sweep / nd); lox[i] = cx; loy[i] = cy; ldx[i] = cos(t); ldy[i] = sin(t); }
    }
    /* the profiles (ours: the height field drop, sampled every hcell along the line over the model's a range widened by the tool) */
    int na = (int)ceil((a1 - a0) / hcell) + 1;
    double *prof = malloc(sizeof(double) * (size_t)count * na), zmax = -1e300, zminp = 1e300;
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < na; k++) { double a = a0 + k * hcell, z = sr_hf_tip(&H, WX(i, a), WY(i, a), Fo, Rb, sp);
            prof[(size_t)i * na + k] = z;   /* -1e30: off the model - no profile there (Mastercam's sections exist only on the model) */
            if (z > -1e29) { zmax = fmax(zmax, z); zminp = fmin(zminp, z); } } }
    if (!(zmax > -1e299)) { snprintf(op->error, sizeof op->error, "the drive gave no profile"); free(prof); cl_free(&cont); c3_mesh_free(&M); free(r); free(lox); free(loy); free(ldx); free(ldy); return 0; }
    free(H.z);
    /* each line's crossings with the containment: FUN_180267000 mode 0 (sr_x_line) per line (FUN_1801fd1e0 l.336331-336360, the pairs written
       with the line's sections); FUN_180205770 trims the profile to the pairs; a line with fewer than two keeps no profile (FUN_180205770 ->
       FUN_1801fb100, l.339852). No line with a crossing (FUN_180133860's pass l.202620-202650, max count 0): error -0x18. */
    double **ivp = NULL; int *niv = NULL;
    if (hascont) {
        double xtol = 0.1 * mc_systol_units(mcam_is_metric(f))->curve_chord_dev;   /* FUN_180267000: curve_chord_dev x 0.1 */
        ivp = calloc(count, sizeof(double *)); niv = calloc(count, sizeof(int)); int anyx = 0, bad = 0;
        for (int i = 0; i < count && !bad; i++) {
            double u = lox[i] * sa - loy[i] * ca; int nx; double *v = NULL;
            int n = sr_x_line(&cont, sa, ca, u, xtol, &v, &nx);
            if (n < 0) { bad = 1; break; }
            if (nx > 0) anyx = 1;
            if (n < 2) { for (int k = 0; k < na; k++) prof[(size_t)i * na + k] = -1e30; n = 0; }
            ivp[i] = v; niv[i] = n;
        }
        if (bad || !anyx) { snprintf(op->error, sizeof op->error, bad ? "%s: a containment chain lies on a cut line (FUN_180267000's case NOT DECODED)" : "%s: no line crosses the containment", nm);
            for (int i = 0; i < count; i++) free(ivp[i]); free(ivp); free(niv); free(prof); cl_free(&cont); c3_mesh_free(&M); free(r); free(lox); free(loy); free(ldx); free(ldy); return 0; }
    }
    /* levels (FUN_1802022d0 / FUN_180204d90, sp 3) */
    double top, bot, step = zdist;
    if (sr_b(r, rn, 0x12e8)) { top = zmax - sr_d(r, rn, 0x1300); bot = zminp + sr_d(r, rn, 0x1308); if (top < bot) top = bot; }
    else { double x = sr_d(r, rn, 0x1310), y = sr_d(r, rn, 0x1318); top = fmax(x, y); bot = fmin(x, y); }
    { double st = fmax(fmax(step, 2 * f68), fmax(tol, 1e-10)), Rr = top - bot;
      if (st < Rr) { int nn = (int)(Rr / st); if (nn < 1) nn = 1; if (Rr / nn > st) nn++; step = Rr / nn; } else step = fmax(fmax(Rr, 2 * f68), fmax(tol, 1e-10)); }
    double clr = p->clearance, ret = p->retract_z > clr ? clr : p->retract_z, fpd = p->feed_plane > 0 ? p->feed_plane : 0;
    double feed = p->feed > 0 ? p->feed : 100, plunge = p->plunge_feed > 0 ? p->plunge_feed : feed;
    int okall = 1, anycut = 0;
    SrList ch = { 0 };
    double L = top, Pz = 0; int first = 1, lvcount = 0;
    if (finish) { L = zlim ? fmin(zl1, zl2) : -1e300; Pz = zlim ? fmax(zl1, zl2) : 1e300; if (zlim && Pz - L < 0.01 * chord) Pz = L + 0.01 * chord; bot = L; step = 1; first = 0; }
    while ((finish && lvcount == 0) || (!finish && bot - 0.4 * step <= L && lvcount < 2000)) {
        if (!finish && L < bot) L = bot;
        if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); okall = 0; break; }
        for (int pass = 0; pass < (pc == 2 && method == 1 ? 2 : 1); pass++) {
            int dir = start_out, started = 0; double lx = 0, ly = 0, lz = 0;   /* the level's moves: each level starts in the same direction (sp 5) */
            for (int li = 0; li < count; li++) {
                for (int rep = 0; rep < (pc == 2 && method == 0 ? 2 : 1); rep++) {
                    int second = (pc == 2 && method == 1) ? pass : rep;
                    /* the chains of the line: runs of profile between gaps (FUN_1801ef590 ends a chain at an a-gap), in the line's direction */
                    int rev = dir ^ (rep == 1), k = 0;
                    while (k + 1 < na) {
                        ch.cnt = 0;
                        for (; k + 1 < na; k++) { int k0 = rev ? na - 1 - k : k, k1 = rev ? na - 2 - k : k + 1;
                            double za = prof[(size_t)li * na + k0], zb = prof[(size_t)li * na + k1];
                            if (za < -1e29 || zb < -1e29) { if (ch.cnt) { k++; break; } continue; }
                            if (hascont) {   /* FUN_180205770 (l.339833): the profile trimmed to the crossing pairs - segments outside removed, a segment across a crossing
                                                cut there with its Z interpolated (l.339871-339874 / 339887-339891); the removed parts leave a gap that ends the chain */
                                double xa = a0 + k0 * hcell, xb = a0 + k1 * hcell, bt0 = 2, bt1 = 0;
                                for (int q = 0; q + 1 < niv[li]; q += 2) {
                                    double lo = ivp[li][q], hi = ivp[li][q + 1], ta = (lo - xa) / (xb - xa), tb = (hi - xa) / (xb - xa);
                                    double t0 = fmax(fmin(ta, tb), 0.0), t1 = fmin(fmax(ta, tb), 1.0);
                                    if (t1 > t0 + 1e-12 && t0 < bt0) { bt0 = t0; bt1 = t1; }
                                }
                                if (bt0 > 1) { if (ch.cnt) { k++; break; } continue; }
                                int cs = bt0 > 1e-12, ce = bt1 < 1 - 1e-12;
                                if (cs && ch.cnt) break;          /* a new piece: the current chain ends, this segment starts the next */
                                SrNode nd = { xa + bt0 * (xb - xa), za + bt0 * (zb - za), xa + bt1 * (xb - xa), za + bt1 * (zb - za), 0 }; srl_push(&ch, nd);
                                if (ce) { k++; break; }
                                continue;
                            }
                            SrNode nd = { a0 + k0 * hcell, za, a0 + k1 * hcell, zb, 0 }; srl_push(&ch, nd); }
                        if (!ch.cnt) continue;
                        double P = Pz;
                        if (finish && !zlim) goto cut_all;   /* levels off: no band split / state machine (l.328398) */
                        if (first) { double mz = 1e300; for (int q = 0; q < ch.cnt; q++) mz = fmin(mz, fmin(ch.n[q].z0, ch.n[q].z1));
                            P = mz + 100; if (P < L + 100) P = L + 100 + f78; }   /* FUN_180204ce0 l.339449 */
                        sr_band_split(&ch, L, P, f80, f78);
                        if (!finish) sr_state(&ch, L, P, f80, chord, pz, nz, pc, second);
                        cut_all:;
                        /* moves (FUN_1801ee8c0 sp 6): in-band nodes cut, others restart the cut */
                        int restart = 1;
                        for (int q = 0; q < ch.cnt; q++) {
                            SrNode *nd = &ch.n[q];
                            if (!(finish && !zlim) && !(L - f80 <= nd->z0 && nd->z0 <= P + f80)) { restart = 1; continue; }
                            if (nd->type == 0x24) continue;
                            double x0 = WX(li, nd->a0), y0 = WY(li, nd->a0), x1 = WX(li, nd->a1), y1 = WY(li, nd->a1);
                            if (!started) {   /* approach (FUN_1801408c0): clearance (or retract), then down (ours: rapid to z + feed plane, feed down) */
                                mv(b, 1, x0, y0, clr, 0); mv(b, 1, x0, y0, nd->z0 + fpd, 0); mv(b, 0, x0, y0, nd->z0, plunge); started = 1;
                            } else if (restart) {   /* link (FUN_180140d80 sp 9): long gap -> retract and down; short gap -> a straight feed (ours) */
                                double g = hypot(x0 - lx, y0 - ly);
                                if (g > gap) { double up = fmax(ret, fmax(lz, nd->z0) + fpd); mv(b, 1, lx, ly, up, 0); mv(b, 1, x0, y0, up, 0); mv(b, 1, x0, y0, nd->z0 + fpd, 0); mv(b, 0, x0, y0, nd->z0, plunge); }
                                else { double up = fmax(lz, nd->z0); if (up > lz + 1e-9) mv(b, 0, lx, ly, up, feed); mv(b, 0, x0, y0, up, feed); if (up > nd->z0 + 1e-9) mv(b, 0, x0, y0, nd->z0, plunge); }
                            } else if (fabs(lz - nd->z0) > f78) mv(b, 0, x0, y0, nd->z0, feed);
                            restart = 0;
                            mv(b, 0, x1, y1, nd->z1, feed);   /* (FUN_1801ee8c0 skips a move under f78: the profile samples are farther apart) */
                            lx = x1; ly = y1; lz = nd->z1; anycut = 1;
                        }
                    }
                }
                if (method == 0) dir = !dir;   /* zigzag: the direction flips after each line, empty ones too for op 5 (l.335185-335187) */
            }
            if (started) { mv(b, 1, lx, ly, ret > lz ? ret : lz + fpd, 0); }   /* FUN_180140820: the level's retract */
        }
        if (finish) break;
        Pz = L; first = 0; L -= step; lvcount++;
        if (L < bot - 0.4 * step) break;
    }
    #undef WX
    #undef WY
    free(ch.n); free(prof); c3_mesh_free(&M); free(r); free(lox); free(loy); free(ldx); free(ldy);
    if (ivp) { for (int i = 0; i < count; i++) free(ivp[i]); free(ivp); free(niv); }
    cl_free(&cont);
    if (!okall) return 0;
    if (!anycut || !b->n) { snprintf(op->error, sizeof op->error, "nothing to cut"); return 0; }
    mv(b, 1, b->m[b->n - 1].p.x, b->m[b->n - 1].p.y, clr, 0);
    return 1;
}
static int gen_srough_parallel(const McamFile *f, CamOp *op, MB *b) { return sr_parallel(f, op, b, 0); }
static int gen_sfinish_parallel(const McamFile *f, CamOp *op, MB *b) { return sr_parallel(f, op, b, 1); }

/* ------------------------------------------------------------------------------------------------------- Finish Contour (op 15) */
/* Surface Finish Contour (op 15; the 3D gallery's "Contour", ribbon msurf_contour_toolbar -> msurf_assoc(0xf)). Decode: formats/surf_classic2.md 3
   (l.N = decomp/mcmsurf_all.c). The pipeline is op 10's (fzt mesh, FUN_1802a14e0(1, ..): the levels FUN_1802a0d50 / FUN_18029cee0 with the stock
   flag 0x144e and no keep-top, the tool-centre loops of run_mctutl_zcut), then per level FUN_18035b9d0 -> FUN_18035d930 cuts the loops. Record:
   tolerance 0x1240, maximum stepdown 0x1248, closed contours Climb 0x13d4 (page 180 858 / 863), open contours One way / Zigzag 0x13d2 (1 / 0),
   bottom to top 0x13e8, the short gap 0x1288 / 0x1290 / 0x1298 (pct x stepdown; with 0x14a0: 0x14a8 x D, l.340444-340466).
   Refused (NOT DECODED): shallow 0x1408 >= 10, flats 0x1492 > 1, helix 0x1450, entry arc / line 0x1430, spiral 0x14b1, transitions other
   than Broken ((0x12a0 % 100) % 10 != 1), the approximate start point 0x13d0, optimise cut order 0x12a4.
   Ours: the slice (c3_slice standing in for run_mctutl_zcut, as gen_srough_pocket); the passes = the boundary loops of the tool's forbidden
   region at the level, clipped to the containment (closed loops whole, the containment cuts make the open pieces); Climb = the material on
   the right of the motion (the forbidden region's loops reversed), Conventional = as Clipper gives them; the loop order and start points
   (FUN_1803694e0 / FUN_18035d150 NOT DECODED); Zigzag flipping each open piece; the links (a straight feed within the short gap, else retract
   and down, as gen_srough_parallel's; the Broken transition's geometry NOT DECODED). */
static int gen_sfinish_contour(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p; const char *nm = "Surface Finish Contour";
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "%s here needs the Top tool plane at the origin", nm); return 0; }
    uint32_t rn = 0; uint8_t *r = sr_record(f, p, &rn);
    if (!r || rn < 0x1700) { free(r); snprintf(op->error, sizeof op->error, "no %s record (Mastercam's default op 15 is missing)", nm); return 0; }
    const char *why = NULL; int tr = (sr_s(r, rn, 0x12a0) % 100) % 10;
    if (sr_s(r, rn, 0x1408) >= 10) why = "shallow (dialog 998, FUN_180316a00 / FUN_18036ba50): NOT DECODED";
    else if (sr_b(r, rn, 0x1492) > 1) why = "flats (dialog 493, FUN_18036ad90): NOT DECODED";
    else if (sr_b(r, rn, 0x1450)) why = "helix entry: NOT DECODED";
    else if (sr_b(r, rn, 0x1430)) why = "entry / exit arc / line: NOT DECODED";
    else if (sr_b(r, rn, 0x14b1)) why = "spiral: NOT DECODED";
    else if (tr != 1) why = "transitions other than Broken (High speed / Ramp / Follow surface): NOT DECODED";
    else if (sr_b(r, rn, 0x13d0)) why = "the approximate start point is not ported";
    else if (sr_b(r, rn, 0x12a4)) why = "optimize cut order (FUN_18035d150): NOT DECODED";
    if (why) { snprintf(op->error, sizeof op->error, "%s: %s", nm, why); free(r); return 0; }
    double D = p->tool_dia > 0 ? p->tool_dia : sr_d(r, rn, 0x350), R = D * 0.5;
    double rc = fmin(fmax(p->tool_dia > 0 ? p->corner_r : sr_d(r, rn, 0x358), 0.0), R);
    double stock = sr_d(r, rn, 0x2e0), chord = sr_d(r, rn, 0x1240), zdist = sr_d(r, rn, 0x1248);
    if (!(chord > 1e-10)) chord = g_nc_tol;
    if (!(zdist > 1e-10)) zdist = 0.05;
    int climb = sr_b(r, rn, 0x13d4) != 0, oneway = sr_b(r, rn, 0x13d2) != 0, up = sr_b(r, rn, 0x13e8) != 0;
    double gap = sr_b(r, rn, 0x1288) ? (sr_b(r, rn, 0x14a0) ? sr_d(r, rn, 0x14a8) * 0.01 * D : sr_d(r, rn, 0x1298) * 0.01 * zdist) : sr_d(r, rn, 0x1290);   /* the % as percent: ours, as op 5 */
    if (!(D > 0)) { free(r); snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); return 0; }
    C3Mesh M; char err[150];
    if (!c3_mesh_op(f, op, chord * 0.5, &M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); free(r); return 0; }
    double sp = fmax(stock, -rc), Rb = rc + sp, Fo = R - rc;
    if (Rb <= 0.009 * chord) { Rb = 0; Fo = R + sp; }
    double top, bot; sr_depths_x(r, rn, M.lo[2], M.hi[2], stock, rc, zdist, &top, &bot, 0x144e, 0);
    double zl[2000]; int nz = sr_levels(top, bot, zdist, chord, zl, 2000);
    double arc_tol = fmax(chord * 0.5, 1e-4), spacing = c3_resolution(R, rc, sp, chord);
    if (!(spacing > 0)) spacing = R;
    CPaths cont; if (!sr_containment(f, op, r, rn, D, &M, arc_tol, &cont)) { c3_mesh_free(&M); free(r); return 0; }
    free(r);
    double bx[4] = { 1e300, 1e300, -1e300, -1e300 };
    for (int i = 0; i < cont.n; i++) for (int k = 0; k < cont.p[i].n; k++) { bx[0] = fmin(bx[0], cont.p[i].xy[2 * k]); bx[1] = fmin(bx[1], cont.p[i].xy[2 * k + 1]); bx[2] = fmax(bx[2], cont.p[i].xy[2 * k]); bx[3] = fmax(bx[3], cont.p[i].xy[2 * k + 1]); }
    double clr = p->clearance, ret = p->retract_z > clr ? clr : p->retract_z, fpd = p->feed_plane > 0 ? p->feed_plane : 0;
    double feed = p->feed > 0 ? p->feed : 100, plunge = p->plunge_feed > 0 ? p->plunge_feed : feed;
    int started = 0, flip = 0; double lx = 0, ly = 0, lz = 0;
    for (int kk = 0; kk < nz; kk++) {
        int k = up ? nz - 1 - kk : kk; double z = zl[k] - rc;   /* the levels are of the corner centre: the tip is a corner radius lower (with 0x12e9
                                                                   the depths get + r, l.340387 / l.438405-438418; the stored op's tip levels [measured]) */
        if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); cl_free(&cont); c3_mesh_free(&M); return 0; }
        CPaths forb = c3_slice(&M, z, Rb, z + rc, Fo, spacing, bx, arc_tol);
        if (!forb.n) { cl_free(&forb); continue; }
        /* the loops as closed polylines, clipped to the containment */
        CPaths lines = { 0 }; lines.p = calloc(forb.n, sizeof(CPath)); lines.n = forb.n;
        for (int i = 0; i < forb.n; i++) { CPath *L = &lines.p[i]; int n = forb.p[i].n; L->n = n + 1; L->xy = malloc(sizeof(double) * 2 * (n + 1));
            for (int q = 0; q < n; q++) { int s_ = climb ? n - 1 - q : q; L->xy[2 * q] = forb.p[i].xy[2 * s_]; L->xy[2 * q + 1] = forb.p[i].xy[2 * s_ + 1]; }
            L->xy[2 * n] = L->xy[0]; L->xy[2 * n + 1] = L->xy[1]; }
        cl_free(&forb);
        CPaths cut = cl_clip_open(&lines, &cont, 1);
        cl_free(&lines);
        for (int i = 0; i < cut.n; i++) {
            CPath *C = &cut.p[i]; if (C->n < 2) continue;
            int closed = hypot(C->xy[0] - C->xy[2 * (C->n - 1)], C->xy[1] - C->xy[2 * (C->n - 1) + 1]) < 1e-6;
            int rev = !closed && !oneway && flip; if (!closed && !oneway) flip = !flip;
            double x0 = C->xy[rev ? 2 * (C->n - 1) : 0], y0 = C->xy[rev ? 2 * (C->n - 1) + 1 : 1];
            if (!started) { mv(b, 1, x0, y0, clr, 0); mv(b, 1, x0, y0, z + fpd, 0); mv(b, 0, x0, y0, z, plunge); started = 1; }
            else { double g = hypot(x0 - lx, y0 - ly);
                if (g > gap || (fabs(lz - z) > 1e-9 && g > 1e-9)) { double upz = fmax(ret, fmax(lz, z) + fpd); mv(b, 1, lx, ly, upz, 0); mv(b, 1, x0, y0, upz, 0); mv(b, 1, x0, y0, z + fpd, 0); mv(b, 0, x0, y0, z, plunge); }
                else { if (fabs(lz - z) > 1e-9) mv(b, 0, lx, ly, z, plunge); mv(b, 0, x0, y0, z, feed); } }
            for (int q = 1; q < C->n; q++) { int s_ = rev ? C->n - 1 - q : q; mv(b, 0, C->xy[2 * s_], C->xy[2 * s_ + 1], z, feed); }
            lx = C->xy[rev ? 0 : 2 * (C->n - 1)]; ly = C->xy[rev ? 1 : 2 * (C->n - 1) + 1]; lz = z;
        }
        cl_free(&cut);
    }
    cl_free(&cont); c3_mesh_free(&M);
    if (!started || !b->n) { snprintf(op->error, sizeof op->error, "nothing to cut (%d levels)", nz); return 0; }
    mv(b, 1, lx, ly, clr, 0);
    return 1;
}

/* --------------------------------------------------------------------------------------------------------- Rough Project (op 7) */
/* Surface Rough Project (op 7; the 3D gallery's Roughing "Project", msurf_rgh_project_toolbar l.212766). Decode: formats/surf_classic3.md 1
   (l.N = decomp/mcmsurf_all.c). Projection type 0x13d4 (0 NCI, 1 Curves, 2 Points), source op 0x14e8; NCI: the source path's XY, every rapid
   ending a chain (FUN_18012cf00 l.198628, Z zeroed for op 7 l.199104); Curves: the chains, closed ones closed (FUN_18012ca70 l.198454). Tool
   as op 10 (tube r + stock', flat D/2 - r, l.429071-429095 [inferred argument order]). Levels (FUN_1802906c0 l.428420): R = top - bottom,
   st = max(zdist, 1.8 chord, 1e-10); R > st -> n = max(1, int(R / st)), + 1 when R / n > st, step R / n; else step = max(R, st); absolute
   depths: max / min of 0x1310 / 0x1318, + r with tip comp Tip (l.430603). Level loop (FUN_180293f40 l.430614-430635): L = top, P = 0;
   while bottom - 0.4 step <= L: L = max(L, bottom); a pass over the chains in file order; P = L; L -= step. In a pass (FUN_180289ff0
   l.424618) a point above P + a78 is not cut on later levels (l.424792); a78 = 0.0001 x 0.9 chord, a70 = 0.009 chord (l.429030-429040).
   Links (FUN_18027b520 l.417483-417503): gaps under a70 none; else the long-gap rule (FUN_180140d80) with max short gap = % x D when the %
   flag 0x1288 is on (l.430545), else 0x1290; retract between cuts 0x1508 -> always retract.
   Refused: Points (no point pick here), plunge control other than 0 / negative or positive Z not allowed (the stop rules' restart NOT
   DECODED), incremental depths' top is ours (below). Ours: the drop (the op-5 height field of the stock-inflated tool, standing in for the
   per-surface solvers FUN_180295ce0 / FUN_180294690, NOT DECODED), the source = this program's toolpath points (the point reduction
   FUN_18012f670 NOT DECODED), Curves = the op's chains (no containment), incremental depths: bottom = min Z of the projected profile + bottom
   stock 0x1308 (decoded, FUN_180296cf0) and top = max Z - top clearance 0x1300 (the max Z is read, its use is lost in the decompile: op 10's
   rule), the short-gap motion (a straight feed at the higher end, as op 5), the % read as percent (as op 5). */
/* finish = 1: Surface Finish Project (op 13 = 0xd): the same stage 1 FUN_18013a370 and stage 2 FUN_180139c80 (l.197519-197524,
   l.199400-199420); msurf_map case 0xd (l.340223-340226): the projection type = record 0x1300, cut angle pi / 2, and it breaks before the
   level block (no levels). Page 1002 (CMsProjectFinishPg init FUN_1800f5530.. / OK l.157950-157975): type 0x1300, source op 0x1418, Add depths
   0x1402 (control 860 = DDX +0x7b70), Retract between cuts 0x1439 (1180), depth limits 0x12e8. Ours: the pass without levels = one pass at
   the projected Z (FUN_180293f40's no-level path NOT DECODED), the depth limits as op 11's band (points outside it not cut); Add depths
   refused (NOT DECODED) */
static int gen_project_x(const McamFile *f, CamOp *op, MB *b, int finish) {
    CamParams *p = &op->p; const char *nm = finish ? "Surface Finish Project" : "Surface Rough Project";
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "%s here needs the Top tool plane at the origin", nm); return 0; }
    uint32_t rn = 0; uint8_t *r = sr_record(f, p, &rn);
    if (!r || rn < 0x1600) { free(r); snprintf(op->error, sizeof op->error, "no %s record (Mastercam's default op %d is missing)", nm, finish ? 13 : 7); return 0; }
    int mode = sr_s(r, rn, finish ? 0x1300 : 0x13d4); int32_t src = 0; if (rn >= 0x14ec) memcpy(&src, r + (finish ? 0x1418 : 0x14e8), 4);
    const char *why = NULL;
    if (mode == 2) why = "Points (the point pick is not ported)";
    else if (mode != 0 && mode != 1) why = "unknown projection type";
    else if (!finish && sr_b(r, rn, 0x13d2) != 0) why = "plunge control other than multiple (FUN_180289ff0's second pass / stop rules): NOT PORTED";
    else if (!finish && (!sr_b(r, rn, 0x13d0) || !sr_b(r, rn, 0x13d1))) why = "negative / positive Z not allowed (the chain stop's restart): NOT DECODED";
    else if (finish && sr_b(r, rn, 0x1402)) why = "Add depths: NOT DECODED";
    if (why) { snprintf(op->error, sizeof op->error, "%s: %s", nm, why); free(r); return 0; }
    /* containment (role 2 chains, S7): tmptc.tmp then holds every chain of the op, the curves included, unclosed (FUN_18012f750 l.200203-200209:
       param_6 = 0 for 7 / 0xd); how the projection uses it is NOT DECODED */
    if (sr_refuse_containment(f, op, 2, nm, "how the projection uses it (tmptc.tmp with the curves, DAT_1805039d4) is NOT DECODED")) { free(r); return 0; }
    double D = p->tool_dia > 0 ? p->tool_dia : sr_d(r, rn, 0x350), R = D * 0.5;
    double rc = fmin(fmax(p->tool_dia > 0 ? p->corner_r : sr_d(r, rn, 0x358), 0.0), R);
    double stock = sr_d(r, rn, 0x2e0), chord = sr_d(r, rn, 0x1240), zdist = sr_d(r, rn, 0x1248);
    if (!(chord > 1e-10)) chord = g_nc_tol;
    if (!(zdist > 1e-10)) zdist = 0.05;
    double a60 = 0.9 * chord, a70 = 0.01 * a60, a78 = 0.0001 * a60;
    double gap = sr_b(r, rn, 0x1288) ? sr_d(r, rn, 0x1298) * 0.01 * D : sr_d(r, rn, 0x1290);
    int retract_all = sr_b(r, rn, finish ? 0x1439 : 0x1508) != 0, inc = sr_b(r, rn, 0x12e8) != 0, tipc = sr_b(r, rn, 0x1239) != 0;
    int zlim = finish && sr_b(r, rn, 0x12e8); double zl1 = sr_d(r, rn, 0x12f0), zl2 = sr_d(r, rn, 0x12f8);
    double dmin = sr_d(r, rn, 0x1310), dmax = sr_d(r, rn, 0x1318), topclr = sr_d(r, rn, 0x1300), botstk = sr_d(r, rn, 0x1308);
    free(r);
    if (!(D > 0)) { snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); return 0; }
    /* the chains (XY) */
    CPaths ch = { 0 };
    if (mode == 1) {
        for (int i = 0; i < op->nchains; i++) { double *xy, z; int n = cam_chain_polyline(f, &op->chains[i], chord, &xy, &z);
            if (n < 2) { free(xy); continue; }
            CPath P = { 0 }; P.n = n + (op->chains[i].closed ? 1 : 0); P.xy = malloc(sizeof(double) * 2 * P.n); memcpy(P.xy, xy, sizeof(double) * 2 * n);
            if (op->chains[i].closed) { P.xy[2 * n] = xy[0]; P.xy[2 * n + 1] = xy[1]; }
            free(xy); ch.p = realloc(ch.p, sizeof(CPath) * (ch.n + 1)); ch.p[ch.n++] = P; }
        if (!ch.n) { snprintf(op->error, sizeof op->error, "%s: Curves - select the curves to project", nm); return 0; }
    } else {
        const Op *so = NULL; for (int i = 0; src > 0 && i < f->nops; i++) if (f->ops[i].id == src) so = &f->ops[i];
        if (!so) { snprintf(op->error, sizeof op->error, "%s: NCI - choose the source operation (record 0x14e8 = %d is not an op of this part)", nm, src); return 0; }
        for (int j = 0; j < so->npolys; j++) { const Poly *q = &f->polys[so->first_poly + j]; if (q->kind != K_CUT || q->count < 2) continue;
            CPath P = { 0 }; P.n = q->count; P.xy = malloc(sizeof(double) * 2 * P.n);
            for (int k = 0; k < q->count; k++) { P.xy[2 * k] = f->pts[q->first + k].x; P.xy[2 * k + 1] = f->pts[q->first + k].y; }
            ch.p = realloc(ch.p, sizeof(CPath) * (ch.n + 1)); ch.p[ch.n++] = P; }
        if (!ch.n) { snprintf(op->error, sizeof op->error, "%s: the source operation has no toolpath", nm); return 0; }
    }
    C3Mesh M; char err[150];
    if (!c3_mesh_op(f, op, chord * 0.5, &M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); cl_free(&ch); return 0; }
    double sp = fmax(stock, -rc), Rb = rc + sp, Fo = R - rc;
    if (Rb <= 0.009 * chord) { Rb = 0; Fo = R + sp; }
    double hcell = fmin(fmax(chord * 4, R / 16), R / 4);   /* ours: the height field spacing (as op 5) */
    SrHf H; sr_hf_build(&M, hcell, Fo + Rb + hcell, &H);
    /* the profiles: each chain sampled every cell, the tool tip dropped */
    int tot = 0; for (int i = 0; i < ch.n; i++) for (int k = 0; k + 1 < ch.p[i].n; k++) tot += (int)ceil(hypot(ch.p[i].xy[2 * k + 2] - ch.p[i].xy[2 * k], ch.p[i].xy[2 * k + 3] - ch.p[i].xy[2 * k + 1]) / hcell) + 1;
    double *px = malloc(sizeof(double) * 3 * (tot + ch.n + 1)); int *st = malloc(sizeof(int) * (ch.n + 1)); int m = 0;
    double zlo = 1e300, zhi = -1e300;
    for (int i = 0; i < ch.n; i++) { st[i] = m;
        for (int k = 0; k + 1 < ch.p[i].n; k++) {
            double x0 = ch.p[i].xy[2 * k], y0 = ch.p[i].xy[2 * k + 1], x1 = ch.p[i].xy[2 * k + 2], y1 = ch.p[i].xy[2 * k + 3];
            int ns = (int)ceil(hypot(x1 - x0, y1 - y0) / hcell); if (ns < 1) ns = 1;
            for (int j = k ? 1 : 0; j <= ns; j++) { double t = (double)j / ns, x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t, z = sr_hf_tip(&H, x, y, Fo, Rb, sp);
                if (z < -1e29) z = M.lo[2];
                px[3 * m] = x; px[3 * m + 1] = y; px[3 * m + 2] = z; m++; zlo = fmin(zlo, z); zhi = fmax(zhi, z); }
        } }
    st[ch.n] = m; int nch = ch.n;
    free(H.z); c3_mesh_free(&M); cl_free(&ch);
    double top, bot;
    if (finish) { inc = 0; dmin = dmax = -1e300; tipc = 0; }   /* no levels: one pass below */
    if (inc) { bot = zlo + botstk; top = fmax(zhi - topclr, bot); }
    else { top = fmax(dmin, dmax); bot = fmin(dmin, dmax); if (tipc) { top += rc; bot += rc; } }
    double Rg = top - bot, stp = fmax(fmax(zdist, 2 * a60), 1e-10), step;
    if (Rg > stp) { int n = (int)(Rg / stp); if (n < 1) n = 1; if (Rg / n > stp) n++; step = Rg / n; } else step = fmax(Rg, stp);
    double clr = p->clearance, ret = p->retract_z > clr ? clr : p->retract_z, fpd = p->feed_plane > 0 ? p->feed_plane : 0;
    double feed = p->feed > 0 ? p->feed : 100, plunge = p->plunge_feed > 0 ? p->plunge_feed : feed;
    int started = 0, first = 1, lv = 0; double lx = 0, ly = 0, lz = 0, L = top, P = 0;
    if (finish) { L = -1e300; bot = -1e300; step = 1; }
    while ((finish && lv == 0) || (!finish && bot - 0.4 * step <= L && lv < 100000)) {
        if (mcce_abort_get()) { free(px); free(st); snprintf(op->error, sizeof op->error, "cancelled"); return 0; }
        L = fmax(L, bot);
        for (int c = 0; c < nch; c++) {
            int cutting = 0;
            for (int k = st[c]; k < st[c + 1]; k++) {
                double x = px[3 * k], y = px[3 * k + 1], pz = px[3 * k + 2], z = fmax(pz, L);
                if (!first && pz > P + a78) { cutting = 0; continue; }   /* cut by an earlier level */
                if (zlim && (pz < fmin(zl1, zl2) || pz > fmax(zl1, zl2))) { cutting = 0; continue; }   /* finish: outside the depth limits */
                if (!cutting) {
                    if (!started) { mv(b, 1, x, y, clr, 0); mv(b, 1, x, y, z + fpd, 0); mv(b, 0, x, y, z, plunge); started = 1; }
                    else { double g = hypot(x - lx, y - ly);
                        if (g <= a70 && fabs(z - lz) <= a70) { }
                        else if (retract_all || g > gap) { double upz = fmax(ret, fmax(lz, z) + fpd); mv(b, 1, lx, ly, upz, 0); mv(b, 1, x, y, upz, 0); mv(b, 1, x, y, z + fpd, 0); mv(b, 0, x, y, z, plunge); }
                        else { double upz = fmax(lz, z); if (upz > lz + 1e-9) mv(b, 0, lx, ly, upz, feed); mv(b, 0, x, y, upz, feed); if (upz > z + 1e-9) mv(b, 0, x, y, z, plunge); } }
                    cutting = 1;
                } else mv(b, 0, x, y, z, feed);
                lx = x; ly = y; lz = z;
            }
        }
        P = L; first = 0; L -= step; lv++;
        if (finish) break;
    }
    free(px); free(st);
    if (!started || !b->n) { snprintf(op->error, sizeof op->error, "nothing to cut"); return 0; }
    mv(b, 1, lx, ly, clr, 0);
    return 1;
}

/* ---------------------------------------------------------------------------------------------------------- Rough Plunge (op 44) */
/* Surface Rough Plunge (op 44 = 0x2c; the 3D gallery's Roughing "Plunge", msurf_assoc(0x2c)). Decode: formats/surf_classic2.md 2 (l.N =
   decomp/mcmsurf_all.c). The corners: the op's two points, op->holes[0 / 1] (select_surf_points l.20442-20470: mcmsurfres 415 / 416; picked
   after the drive here, user decision T-2). Grid FUN_18012c430 (2.2): the Top frame u = x, v = y; per axis n = ceil(|d| / s) equal steps
   (|d| < tol: one row / column); zigzag rows from P1. Levels FUN_1802906c0 (2.3): absolute top / bottom = max / min of 0x1310 / 0x1318;
   step = R / n, n = max(1, int(R / st)) (+1 when R / n > st), st = max(zdist, 2 x DAT_180513a60, tol, 1e-10) - DAT_180513a60 (the msproj
   header) is NOT DETERMINED: 2 x 0.9 chord as op 5 (ours). Loop FUN_180293f40: L = top .. bottom - 0.4 step. Per plunge FUN_180296360
   (2.4): skip when not first and z >= L + step; z < L -> L; H = first ? max(L, z) + step : P; with 0x141c raised to top of stock 0x2e8 +
   corner r; rapid retract off (0x1a9 == 0): back up to Z_ret (as H; an absolute feed plane above it instead; + an incremental feed plane)
   at 0x388 (the plunge rate when < tol). Ours (labelled): the drop = op 5's height field (FUN_180290fb0 + make_surf_model NOT DECODED),
   incremental depths from the grid's Z range as op 5 (FUN_180296cf0 NOT DECODED), the link geometry of gap type 4 (rapid to the retract
   height - clearance when retract is off -, over, rapid down to H; FUN_180140d80 / FUN_1801408c0 NOT DECODED). No stored op 44 exists:
   unverified. */
static int gen_srough_plunge(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p; const char *nm = "Surface Rough Plunge";
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "%s here needs the Top tool plane at the origin", nm); return 0; }
    uint32_t rn = 0; uint8_t *r = sr_record(f, p, &rn);
    if (!r || rn < 0x1700) { free(r); snprintf(op->error, sizeof op->error, "no %s record (Mastercam's default op 44 is missing)", nm); return 0; }
    double tol = CAM_SYS_TOL; const char *why = NULL;
    double D = p->tool_dia > 0 ? p->tool_dia : sr_d(r, rn, 0x350), R = D * 0.5;
    double rc = fmin(fmax(p->tool_dia > 0 ? p->corner_r : sr_d(r, rn, 0x358), 0.0), R);
    if (sr_s(r, rn, 0x141a) == 1) why = "plunge path NCI (FUN_18012cf00 / FUN_18012b940 / FUN_18012bf40): NOT DECODED";
    else if (sr_b(r, rn, 0x13ec) && sr_d(r, rn, 0x13f0) > tol) why = "helix plunge (FUN_18028b7f0): NOT DECODED";
    else if (op->nholes < 2) why = "two corner points are needed (Select plunge point at lower left / upper right)";
    else if (!sr_b(r, rn, 0x12e8) && sr_b(r, rn, 0x1239)) why = "absolute cut depths with tip comp Tip (the msproj tool value DAT_180513a08, FUN_180293f40 l.430600-430605): NOT DECODED";
    else if (!sr_b(r, rn, 0x1239) && rc > 1e-9) why = "tip comp Center with a corner radius: NOT PORTED";
    if (why) { snprintf(op->error, sizeof op->error, "%s: %s", nm, why); free(r); return 0; }
    /* containment (S7): mcmsurf writes it to tmptc.tmp (FUN_1800135a0, every chain); its use by the plunge grid is NOT DECODED (surf_classic2.md) */
    if (sr_refuse_containment(f, op, 0xff, nm, "its use by the plunge grid (FUN_180293f40) is NOT DECODED")) { free(r); return 0; }
    double stock = sr_d(r, rn, 0x2e0), chord = sr_d(r, rn, 0x1240), zdist = sr_d(r, rn, 0x1248), s = fabs(sr_d(r, rn, 0x13d0));
    if (!(chord > 1e-10)) chord = g_nc_tol; if (!(zdist > 1e-10)) zdist = 0.05; if (!(s > 1e-10)) s = 0.05;   /* msurf_map l.340492 / 340495 */
    if (!(D > 0)) { free(r); snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); return 0; }
    C3Mesh M; char err[150];
    if (!c3_mesh_op(f, op, chord * 0.5, &M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); free(r); return 0; }
    double sp = fmax(stock, -rc), Rb = rc + sp, Fo = R - rc;
    if (Rb <= 0.009 * chord) { Rb = 0; Fo = R + sp; }
    double hcell = fmin(fmax(chord * 4, R / 16), R / 4);   /* ours: the height field spacing (as op 5) */
    SrHf H; sr_hf_build(&M, hcell, Fo + Rb + hcell, &H); c3_mesh_free(&M);
    /* the grid (2.2) */
    double u1 = op->holes[0].x, v1 = op->holes[0].y, du = op->holes[1].x - u1, dv = op->holes[1].y - v1; int nu = 1, nv = 1;
    if (fabs(du) >= tol) { int n = (int)ceil(fabs(du) / s); du /= n; nu = n + 1; } else du = 0;
    if (fabs(dv) >= tol) { int n = (int)ceil(fabs(dv) / s); dv /= n; nv = n + 1; } else dv = 0;
    if ((double)nu * nv > 4e6) { free(H.z); free(r); snprintf(op->error, sizeof op->error, "%s: too many plunge points", nm); return 0; }
    double u2 = u1 + (nu - 1) * du;
    int np = nu * nv; double *gx = malloc(sizeof(double) * 3 * np); int ng = 0; double zmax = -1e300, zmin = 1e300;
    for (int j = 0, fromP1 = 1; j < nv; j++, fromP1 = !fromP1) for (int k = 0; k < nu; k++) {
        double x = fromP1 ? u1 + k * du : u2 - k * du, y = v1 + j * dv, z = sr_hf_tip(&H, x, y, Fo, Rb, sp);
        if (z < -1e29) continue;                           /* off the model: no projected point (ours) */
        gx[3 * ng] = x; gx[3 * ng + 1] = y; gx[3 * ng + 2] = z; ng++; zmax = fmax(zmax, z); zmin = fmin(zmin, z); }
    free(H.z);
    if (!ng) { free(gx); free(r); snprintf(op->error, sizeof op->error, "%s: no plunge point over the drive", nm); return 0; }
    /* levels (2.3) */
    double top, bot;
    if (sr_b(r, rn, 0x12e8)) { top = zmax - sr_d(r, rn, 0x1300); bot = zmin + sr_d(r, rn, 0x1308); if (top < bot) top = bot; }   /* ours, as op 5 */
    else { double x = sr_d(r, rn, 0x1310), y = sr_d(r, rn, 0x1318); top = fmax(x, y); bot = fmin(x, y); }
    double step, f68 = 0.9 * chord;
    { double st = fmax(fmax(zdist, 2 * f68), fmax(tol, 1e-10)), Rr = top - bot;
      if (Rr > st) { int nn = (int)(Rr / st); if (nn < 1) nn = 1; if (Rr / nn > st) nn++; step = Rr / nn; } else step = fmax(fmax(Rr, 2 * f68), fmax(tol, 1e-10)); }
    double clr = p->clearance, ret = p->retract_z > clr ? clr : p->retract_z, fpv = sr_d(r, rn, 0x190);
    int fp_inc = sr_b(r, rn, 0x198), rapid_ret = sr_b(r, rn, 0x1a9), tos = sr_b(r, rn, 0x141c); double tosz = sr_d(r, rn, 0x2e8) + rc;
    if (!sr_b(r, rn, 0x189)) ret = clr;                    /* retract off: the clearance (2.4 step 4, sr 8 "Moves") */
    double feed = p->feed > 0 ? p->feed : 100, plunge = p->plunge_feed > 0 ? p->plunge_feed : feed, rrate = sr_d(r, rn, 0x388);
    if (!(rrate > tol)) rrate = plunge;                    /* l.431752-431762 */
    double L = top, P = 0; int first = 1, lv = 0, started = 0, any = 0;
    while (L >= bot - 0.4 * step && lv < 2000) {
        if (L < bot) L = bot;
        if (mcce_abort_get()) { free(gx); free(r); snprintf(op->error, sizeof op->error, "cancelled"); return 0; }
        for (int i = 0; i < ng; i++) {
            double x = gx[3 * i], y = gx[3 * i + 1], z = gx[3 * i + 2];
            if (!first && z >= L + step) continue;         /* cut at the level above */
            double zc = z < L ? L : z;
            double Hh = first ? fmax(L, z) + step : P;
            if (tos && Hh < tosz) Hh = tosz;
            if (!fp_inc && fpv > Hh) Hh = fpv;             /* an absolute feed plane above H: the approach goes to it first (2.4 step 3) */
            if (!started) { mv(b, 1, x, y, clr, 0); started = 1; }
            else mv(b, 1, b->m[b->n - 1].p.x, b->m[b->n - 1].p.y, fmax(ret, b->m[b->n - 1].p.z), 0);
            mv(b, 1, x, y, fmax(ret, Hh), 0); mv(b, 1, x, y, Hh, 0);
            mv(b, 0, x, y, zc, plunge); any = 1;
            if (!rapid_ret) {                              /* 2.4 step 5 */
                double zr = first ? fmax(L, z) + step : P;
                if (!fp_inc && fpv > zr) zr = fpv;
                if (tos && zr < tosz) zr = tosz;
                if (fp_inc) zr += fpv;
                mv(b, 0, x, y, zr, rrate);
            }
        }
        P = L; first = 0; L -= step; lv++;
    }
    free(gx); free(r);
    if (!any || !b->n) { snprintf(op->error, sizeof op->error, "nothing to cut"); return 0; }
    mv(b, 1, b->m[b->n - 1].p.x, b->m[b->n - 1].p.y, clr, 0);
    return 1;
}
static int gen_srough_project(const McamFile *f, CamOp *op, MB *b) { return gen_project_x(f, op, b, 0); }
static int gen_sfinish_project(const McamFile *f, CamOp *op, MB *b) { return gen_project_x(f, op, b, 1); }
