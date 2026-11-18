/* cam2d_face.c - Facing (opcode 0x66), ported from mcmill.dll bnci_face 0x180097230 (decomp/mcmill_all.c l.101309-102230;
 * op dialog batch 7, formats/opdlg_port_audit.md 11). Included by cam2d.c after cam2d_pock.c: it uses pgeo (exact line / arc
 * loops), cam2d_pock.c pk_chain_gl, cam2d_ops.c mcd / mcs / mcb / mc_init / mw_arc and cam2d_dyn.c mcce_moves.
 *
 * Record offsets below are part-file OP record offsets (op offset + 0x68, formats/mcmill_2d_engines.md 0.1); bnci_face reads
 * cur_op +0x11c0.. = record 0x1228.. (CamParams.mc_p). The distances of the "% [distance]" pairs are recomputed from the % by
 * bnci_face itself (l.101600-101609: op +0x11d0 / +0x11f0 / +0x11e0 / +0x1200 / +0x1210 = % x D x 0.01), so the engine reads
 * the % side: stepover 0x1240, along 0x1250, across 0x1260, approach 0x1270, exit 0x1280 (the stored distances 0x1238 /
 * 0x1248 / 0x1258 / 0x1268 / 0x1278 are the dialog's). Style S16 0x1228: 0 Zigzag, 4 One way, 3 One pass, 5 Dynamic (the
 * page's combo, FUN_18016ba50 / FUN_18016caf0).
 *
 * mccore primitives (decomp/mccore_moves.c, decomp/mccore_depths.c): set_depths sets init / retract / clear depth once per
 * boundary (mc_init); adjust_depths (0x18089e310, called after every pass with no offset) moves top_of_stock to the pass
 * depth and re-bases the incremental feed plane / retract / clearance on it - buckle-v8 op 41's NCI retracts to 13.5 and
 * approaches at 8.5 (15 / 10 incremental over the -1.5 pass). plunge_move param_5 is bnci_face's local_4386 (1 for the very
 * first approach only), retract_move / clear_move as decompiled.
 * Ours (marked where used): a rapid that descends while it moves in XY is written as the XY move at its height, then the
 * descent (Mastercam's NCI writer ct_lin_mill does this, NOT DECODED; buckle-v8 op 41 NCI records 5-6); the boundary order of
 * several chains (sort_pock_bounds' order NOT DECODED: chain order); splines linearised by chords at 0x1230
 * (chains_to_pock_bounds' linearisation NOT DECODED). Not ported: subprograms (1018 / 1019), the NCI markers / colours, the
 * "Face CE option" registry value (0 assumed: Dynamic flag 0x40, micro lift 0.01 D), face9.dll. */

/* ours (posted-NC switch, formats/post.md): the zigzag's "Feed rate between cuts" (0x1298 / 0x12a0) changes the F word of the
   links; honoured only with MCAM_FACE_LINK_FEED=1, else the links take the feed rate (the record's 0x1298 = 0 behaviour) */
int cam_face_link_feed_on(void) { const char *e = getenv("MCAM_FACE_LINK_FEED"); return e && !strcmp(e, "1"); }

typedef struct {
    MB *b; const CamParams *p;
    int down;                      /* local_4388 (plunge_move param_4 / retract_move param_5): the tool is down in the cut */
    int first;                     /* local_4386: plunge_move param_5, 1 for the op's first approach only */
    double top, init, ret, clr;    /* mccore top_of_stock / init_depth / retract_depth / clear_depth */
    int ret_on, clr_on;
    double fp_raw, ret_raw, clr_raw;   /* the incremental heights over top of stock (CamParams: feed_plane, retract_z / clearance - top_z) */
    double ca, sa;                 /* cos_rough / sin_rough (rot_rpt: the rotated frame back to the world) */
} FW;

/* set_depths 0x18089f680 (per boundary): top, init / retract / clear depth (mc_init = its init_depth rule) */
static void fw_set_depths(FW *w) {
    const CamParams *p = w->p;
    w->top = p->top_z; w->ret_on = p->retract_z > -1e29; w->clr_on = p->mc_clr_on;
    w->ret = p->retract_z; w->clr = p->clearance;
    w->fp_raw = p->feed_plane; w->ret_raw = p->retract_z - p->top_z; w->clr_raw = p->clearance - p->top_z;
    w->init = mc_init(p);
}
/* adjust_depths(op, &z, 0, 0, 0, 0) 0x18089e310: top_of_stock = z; incremental feed plane (+0x130 = 0x198), retract (+0x121
   on, +0x120 inc) and clearance (+0x111 on, +0x110 inc) re-based on it */
static void fw_adjust(FW *w, double z) {
    const CamParams *p = w->p;
    w->top = z;
    if (p->ht_inc & 4) w->init = z + w->fp_raw;
    if (w->ret_on && (p->ht_inc & 2)) w->ret = z + w->ret_raw;
    if (w->clr_on && (p->ht_inc & 1)) w->clr = z + w->clr_raw;
}
static void fw_rapid(FW *w, double x, double y, double z) {
    if (w->b->n) { V3 e = lastp(w->b);   /* ours: XY first when the rapid descends (see the header) */
        if (z < e.z - MC_TOL && hypot(x - e.x, y - e.y) > MC_TOL) mv(w->b, 1, x, y, e.z, 0); }
    mv(w->b, 1, x, y, z, 0);
}
static void fw_world(const FW *w, double u, double v, double *x, double *y) { *x = u * w->ca - v * w->sa; *y = u * w->sa + v * w->ca; }
/* FUN_1801e32a0 (gr_lin_mill of rot_rpt(pt)): g 1 linear at feed, g 0 rapid (feed -2.0) */
static void fw_lin(FW *w, int g, double u, double v, double z, double feed) {
    double x, y; fw_world(w, u, v, &x, &y);
    if (!g) { fw_rapid(w, x, y, z); return; }
    V3 e = lastp(w->b);
    if (hypot(e.x - x, e.y - y) > 1e-9 || fabs(e.z - z) > 1e-9) mv(w->b, 0, x, y, z, feed);
}
/* plunge_move 0x18089ed00 (decomp/mccore_moves.c l.79-200) */
static void fw_plunge(FW *w, double x, double y, double z, int go) {
    const CamParams *p = w->p; MB *b = w->b;
    if (!w->down) {
        if (w->clr_on) { if (w->init + MC_TOL < w->clr && z + MC_TOL < w->clr && go) fw_rapid(w, x, y, w->clr); }
        else if (w->ret_on && w->init + MC_TOL < w->ret && z + MC_TOL < w->ret && go) fw_rapid(w, x, y, w->ret);
        fw_rapid(w, x, y, z + MC_TOL < w->init ? w->init : z);
    } else if (b->n) {
        V3 e = lastp(b);
        if (hypot(e.x - x, e.y - y) > MC_TOL) mv(b, 0, x, y, e.z, p->feed);   /* tool down: across at the feed rate (op +0x310) */
    }
    {   V3 e = b->n ? lastp(b) : (V3){ x, y, z + 1 };                      /* op +0x318; a zero-length one is not in the NCI
                                                                               (buckle-v8 op 41: the reversed last pass) */
        if (hypot(e.x - x, e.y - y) > 1e-9 || fabs(e.z - z) > 1e-9) mv(b, 0, x, y, z, p->plunge_feed); }
    w->down = 1;
}
/* retract_move 0x18089f390 (l.201-285): feed at the retract rate (op +800 = 0x388) to init_depth unless rapid retract
   (op +0x141 = 0x1a9), then rapid to the retract depth when on, else the clearance when on, else init_depth */
static void fw_retract(FW *w) {
    const CamParams *p = w->p; MB *b = w->b;
    if (b->n) {
        V3 e = lastp(b); double z = e.z;
        if (!p->mc_rapid_ret && !(w->init <= MC_TOL + z)) { mc_feed_up(b, p, e.x, e.y, w->init); z = w->init; }
        if (w->ret_on) { if (z + MC_TOL < w->ret) mv(b, 1, e.x, e.y, w->ret, 0); }
        else if (w->clr_on) { if (z + MC_TOL < w->clr) mv(b, 1, e.x, e.y, w->clr, 0); }
        else if (z + MC_TOL < w->init) mv(b, 1, e.x, e.y, w->init, 0);
    }
    w->down = 0;
}
/* clear_move 0x18089e6a0 (l.3-78; cam2d_ops.c mw_clear with this writer's depths) */
static void fw_clear(FW *w) {
    const CamParams *p = w->p; MB *b = w->b;
    if (b->n) {
        V3 e = lastp(b); double z = e.z;
        if (!p->mc_rapid_ret && z + MC_TOL < w->init) { mc_feed_up(b, p, e.x, e.y, w->init); z = w->init; }
        if (w->clr_on) { if (z + MC_TOL < w->clr) mv(b, 1, e.x, e.y, w->clr, 0); }
        else if (w->ret_on) { if (z + MC_TOL < w->ret) mv(b, 1, e.x, e.y, w->ret, 0); }
        else if (p->mc_rapid_ret && z + MC_TOL < w->init) mv(b, 1, e.x, e.y, w->init, 0);
    }
    w->down = 0;
}

/* FUN_180096d20 (l.101140-101306): the automatic roughing angle of a boundary (world frame) */
static double face_auto_angle(const Gl *l) {
    double per = gl_len(l), best = 0, ang = 0; int found = 0;
    for (int i = 0; i < l->n; i++) {                     /* the longest line longer than a quarter of the perimeter */
        const Gt *e = &l->e[i]; if (e->t != 'L') continue;
        double len = gt_len(e);
        if (per * 0.25 < len && best < len) {
            best = len; found = 1;
            double dx = e->b.x - e->a.x, dy = e->b.y - e->a.y;
            if (1e-12 <= dx * dx + dy * dy) { ang = atan2(dy, dx); if (ang < 0) ang += 2 * M_PI; } else ang = 0;
        }
    }
    if (!found) {   /* 16 directions -15 pi / 16 + k pi / 16: midpoint tangents x length within tan(pi / 16) of a direction add up */
        double bin[16][7], tn = tan(0.19634954084936207), a = -2.945243112740431;
        for (int k = 0; k < 16; k++, a += 0.19634954084936207) {
            bin[k][0] = cos(a); bin[k][1] = sin(a); bin[k][2] = -sin(a); bin[k][3] = cos(a); bin[k][4] = bin[k][5] = 0; bin[k][6] = a; }
        for (int i = 0; i < l->n; i++) {
            PV t = gt_tan(&l->e[i], 0.5); double len = gt_len(&l->e[i]), vx = t.x * len, vy = t.y * len;
            for (int k = 0; k < 16; k++) {
                double d12 = vx * bin[k][0] + vy * bin[k][1], d14 = vx * bin[k][2] + vy * bin[k][3];
                if (1e-12 < fabs(d12) && fabs(d14) < fabs(d12) && fabs(d14) / fabs(d12) < tn) {
                    if (d12 < 0) { d12 = -d12; d14 = -d14; }
                    bin[k][4] += d12; bin[k][5] += d14;
                }
            }
        }
        int kb = 0; double mx = 0;
        for (int k = 0; k < 16; k++) if (mx < bin[k][4]) { mx = bin[k][4]; kb = k; }
        double vx = bin[kb][4] * bin[kb][0] + bin[kb][5] * bin[kb][2], vy = bin[kb][4] * bin[kb][1] + bin[kb][5] * bin[kb][3];
        if (1e-12 <= vx * vx + vy * vy) { ang = atan2(vy, vx); if (ang < 0) ang += 2 * M_PI; } else ang = 0;
    }
    /* restrict_angle(-1.570796326794896, 4.71238898038469) (mccore 0x1804b4440, decomp/mccore_inters.c): subtract 2 pi while
       above the upper bound, then add 2 pi while below the lower one (the bounds themselves stay) */
    while (4.71238898038469 < ang) ang -= 2 * M_PI;
    while (ang < -1.570796326794896) ang += 2 * M_PI;
    if (1.5707973267948958 < ang) ang -= M_PI;
    if (fabs(ang) < 1e-6) ang = 0;
    return fabs(ang - M_PI) < 1e-6 ? M_PI : ang;
}

/* FUN_180098d60's ccomp_pock with min fillet pi: every convex corner sharp. ccomp_pock's corner construction is NOT DECODED;
   ours: the corner arcs gls_offset builds about an input vertex are replaced by the two offset elements' end tangents meeting
   at the apex - BRACKET4 op 8's NCI pass ends at y 9.631 / 5.369 (a line-arc corner) agree to 1e-4 this way and are 0.018 off
   when the arc is extended along its circle */
static void face_sharpen(Gl *o, const Gl *in, double d) {
    Gl r = { 0 };
    for (int i = 0; i < o->n; i++) {
        const Gt *e = &o->e[i]; int corner = 0;
        if (e->t == 'A' && fabs(e->r - fabs(d)) < 1e-7 && fabs(e->sw) < M_PI - 1e-6)
            for (int k = 0; k < in->n && !corner; k++) { PV v = gt_start(&in->e[k]); corner = hypot(v.x - e->c.x, v.y - e->c.y) < 1e-6; }
        if (!corner) { gl_push(&r, *e); continue; }
        double h = e->sw * 0.5, L = e->r / cos(h);
        PV a = gt_start(e), b = gt_end(e), x = pv(e->c.x + L * cos(e->sa + h), e->c.y + L * sin(e->sa + h));
        gl_push(&r, gt_line(a, x)); gl_push(&r, gt_line(x, b));
    }
    gl_free(o); *o = r;
}
static Gt gt_rot(const Gt *e, double c, double s, double a) {
    Gt r = *e;
    r.a = pv(e->a.x * c - e->a.y * s, e->a.x * s + e->a.y * c); r.b = pv(e->b.x * c - e->b.y * s, e->b.x * s + e->b.y * c);
    r.c = pv(e->c.x * c - e->c.y * s, e->c.x * s + e->c.y * c); if (e->t == 'A') r.sa = e->sa + a;
    return r;
}
static void gl_rot(Gl *l, double a) { double c = cos(a), s = sin(a); for (int i = 0; i < l->n; i++) l->e[i] = gt_rot(&l->e[i], c, s, a); }
/* pock_bound_line_inters with the line (-100, v) - (100, v) at tolerance nc_tol x 0.01 (l.101968-101993): the smallest and
   largest u where the loop meets the horizontal line v; 0 = none (warning 0xa5d) */
static int face_span(const Gl *l, double v, double tol, double *ulo, double *uhi) {
    int n = 0; *ulo = 1e38; *uhi = -1e38;
#define HIT(U) do { double u_ = (U); if (u_ < *ulo) *ulo = u_; if (u_ > *uhi) *uhi = u_; n++; } while (0)
    for (int i = 0; i < l->n; i++) {
        const Gt *e = &l->e[i];
        if (e->t == 'L') {
            double dy = e->b.y - e->a.y;
            if (fabs(dy) <= tol) { if (fabs(e->a.y - v) <= tol) { HIT(e->a.x); HIT(e->b.x); } continue; }
            double t = (v - e->a.y) / dy, L = gt_len(e), et = L > 0 ? tol / L : 0;
            if (t >= -et && t <= 1 + et) HIT(e->a.x + (e->b.x - e->a.x) * t);
        } else if (e->t == 'A') {
            double dy = v - e->c.y; if (fabs(dy) > e->r + tol) continue;
            double dx = sqrt(fmax(0, e->r * e->r - dy * dy));
            for (int k = 0; k < 2; k++) {
                double x = e->c.x + (k ? dx : -dx), a = atan2(dy, x - e->c.x), at = e->r > 0 ? tol / e->r : 0;
                double rel = e->sw > 0 ? ang_norm(a - e->sa) : ang_norm(e->sa - a);
                if (rel <= fabs(e->sw) + at || rel >= 2 * M_PI - at) HIT(x);
            }
        }
    }
#undef HIT
    return n;
}

/* chains_to_pock_bounds (closed chains; 3D chains: warning 0x2309 / 0x270b) or the machine group's stock (FUN_180098f90
   make_facing_pock_bound: rectangle from job_setup_stock_corners, or the cylinder's circle when it stands along the
   Tplane's Z); sort_pock_bounds: a boundary with islands is refused (warning 0xa5e) */
static int face_bounds(const McamFile *f, CamOp *op, Gls *out, double lintol) {
    const CamParams *p = &op->p; memset(out, 0, sizeof *out);
    for (int i = 0; i < op->nchains; i++) {
        const Chain *ch = &op->chains[i];
        if (ch->avoid) continue;
        if (!ch->closed) { snprintf(op->error, sizeof op->error, "Facing: an open chain - chains_to_pock_bounds' handling of open chains is NOT DECODED"); gls_free(out); return 0; }
        Gl l; int other = 0;
        for (int k = 0; k < ch->n; k++) { int ty = f->ents[ch->ent[k]].type; if (ty != E_LINE && ty != E_ARC) other = 1; }
        if (!other) { if (!pk_chain_gl(f, ch, &l)) { snprintf(op->error, sizeof op->error, "Facing: a chain is not planar / not closed (Mastercam warning 0x2309)"); gls_free(out); return 0; } }
        else {   /* ours: chords at the linearization tolerance 0x1230 */
            double *xy, z; int n = cam_chain_polyline(f, ch, lintol, &xy, &z);
            memset(&l, 0, sizeof l);
            for (int k = 0; k + 1 < n; k++) if (hypot(xy[2 * k + 2] - xy[2 * k], xy[2 * k + 3] - xy[2 * k + 1]) > 1e-12) gl_push(&l, gt_line(pv(xy[2 * k], xy[2 * k + 1]), pv(xy[2 * k + 2], xy[2 * k + 3])));
            if (n > 2 && hypot(xy[0] - xy[2 * n - 2], xy[1] - xy[2 * n - 1]) > 1e-12) gl_push(&l, gt_line(pv(xy[2 * n - 2], xy[2 * n - 1]), pv(xy[0], xy[1])));
            free(xy);
            if (l.n < 2) { gl_free(&l); continue; }
        }
        if (gl_area(&l) < 0) gl_reverse(&l);
        gls_push(out, l);
    }
    if (!op->nchains) {
        Gl l; memset(&l, 0, sizeof l);
        if (p->use_box && p->box[2] > p->box[0] && p->box[3] > p->box[1]) {
            PV c[4] = { pv(p->box[0], p->box[1]), pv(p->box[2], p->box[1]), pv(p->box[2], p->box[3]), pv(p->box[0], p->box[3]) };
            for (int k = 0; k < 4; k++) gl_push(&l, gt_line(c[k], c[(k + 1) % 4]));
        } else if (p->use_circ && p->circ[2] > 0) gl_push(&l, gt_arc(pv(p->circ[0], p->circ[1]), p->circ[2], 0, 2 * M_PI));
        else { snprintf(op->error, sizeof op->error, "Facing: Stock boundary is not defined (Mastercam warning 0xa59)"); return 0; }
        gls_push(out, l);
    }
    for (int i = 0; i < out->n; i++) for (int j = 0; j < out->n; j++) if (i != j) {
        Gls one = { &out->l[j], 1, 1 };
        if (gls_inside(&one, gt_pt(&out->l[i].e[0], 0.5), MC_TOL)) {
            snprintf(op->error, sizeof op->error, "Facing does not support islands.  Use Pocket facing. (Mastercam warning 0xa5e)"); gls_free(out); return 0; }
    }
    if (!out->n) { snprintf(op->error, sizeof op->error, "Facing needs closed chains (or the machine group's stock)"); return 0; }
    return 1;
}

/* FUN_180098e80 (l.102274-102313; mccore calc_depth_cuts): passes n, rough passes nr, rough step rs */
static void face_depths(const CamParams *p, double top, double zf, int *n, int *nr, double *rs) {
    *rs = 0;
    if (!p->mc_depth_on) { *n = 1; *nr = 0; return; }
    int nf = p->mc_depth_nf; double fs = p->mc_depth_fs, mx = p->stepdown;
    double dz = (top - zf) - nf * fs;
    if (top - zf < MC_TOL || dz < MC_TOL) *nr = 0;
    else if (!(mx > 0)) { *nr = 1; *rs = dz; }   /* dz / 0 = inf: (short)(int) = 0, |inf - 0| > tol -> 1 cut of dz (x64 cvttsd2si) */
    else {
        double q = dz / mx; int k = (int16_t)(int)q; *nr = k;
        if (fabs(q - k) <= MC_TOL) *rs = mx; else { *nr = k + 1; *rs = dz / (k + 1); }
    }
    *n = nf + *nr;
    if (*n == 0) { *nr = 1; *n = 1; }
}
static double face_z(int k, int n, int nr, double rs, double zf, double fs) {   /* l.101830-101833 */
    int a = 0, u = k; if (k <= nr) { a = nr - k; u = nr; }
    return a * rs + zf + (n - u) * fs;
}

/* style 5 Dynamic (l.101863-101957): MCCECalcDynPock per depth with bnci_face's CDynPockParm, written by mcce_moves (the
   Dynamic Mill writer of this program: FUN_1801b07d0's NCI writer is NOT DECODED, as for Dynamic Mill) */
static int face_dynamic(FW *w, CamOp *op, const Gl *bound, int last_bound, int metric) {
    const CamParams *p = w->p; double D = p->tool_dia;
    double so = mcd(p, 0x1240) * D * 0.01, appr = mcd(p, 0x1270) * D * 0.01;
    double zf = p->final_z; int n, nr; double rs; face_depths(p, w->top, zf, &n, &nr, &rs);
    McceParm P; memset(&P, 0, sizeof P);                               /* ClearCeParm */
    P.flags = 0x40 | 0x8000002 | (mcb(p, 0x12c0) ? 1 : 0) | (metric ? 8 : 0);   /* "Face CE option" 0: 0x40 */
    P.start_sel = mcb(p, 0x12c2); P.order_mode = 1;
    P.R = D * 0.5; P.r = p->corner_r;                                  /* FUN_1801afa30 (tool-manager refinement NOT DECODED) */
    { double x = 1 - so / P.R; P.eng = x < -1 ? M_PI : acos(x > 1 ? 1 : x); }   /* CalcLnEngAng(D / 2, stepover) */
    P.micro_lift = 0.01 * D; P.approach = appr;
    P.chain_tol = cam_chain_tol(metric); P.sliver = P.chain_tol * 10.0 * 2.0;
    P.troch_r = D * 0.5 + so;
    /* ours: the CDynPockParm feeds stay 0 in bnci_face (ClearCeParm) - FUN_1801b07d0 takes the op's; the feed rate here */
    P.f_cut = P.f_back = P.f_ramp = P.f_first = P.f_conv = p->feed; P.f_retract = p->mc_rrate;
    CPath poly = gl_sample(bound, fmax(p->tol, MC_TOL));
    McceBnd bd = { poly.xy, poly.n, 0, 0 };
    int ok = 1;
    for (int k = 1; k <= n && ok; k++) {
        double z = face_z(k, n, nr, rs, zf, p->mc_depth_fs);
        P.top = w->top; P.depth = z;
        McceRec *rec = NULL; int nrec = 0; char err[160] = "";
        if (!mcce_calc_dyn_pock(&P, &bd, 1, NULL, 0, NULL, 0, &rec, &nrec, err, sizeof err) || nrec < 1) {
            snprintf(op->error, sizeof op->error, "Facing: Error creating the facing toolpath (Mastercam warning 0xa5d)%s%s", err[0] ? ": " : "", err);
            free(rec); ok = 0; break; }
        for (int i = 0; i < nrec; i++) if (!(rec[i].feed > 0)) rec[i].feed = p->feed;
        const McceRec *r0 = &rec[0];   /* FUN_180096be0: the first entity's start */
        double sx = r0->id == 'A' ? r0->cx + r0->r * cos(r0->sa) : r0->x1, sy = r0->id == 'A' ? r0->cy + r0->r * sin(r0->sa) : r0->y1;
        fw_plunge(w, sx, sy, z, w->first); w->first = 0;
        int down = 1; mcce_moves(w->b, p, rec, nrec, w->init, &down); w->down = down;
        free(rec);
        fw_adjust(w, z);
        if ((k < n || !last_bound) && w->clr_on && p->mc_clr_end) fw_retract(w); else fw_clear(w);
    }
    free(poly.xy);
    return ok;
}

static int gen_face_mc(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p; double D = p->tool_dia, R = D * 0.5;
    if (!p->mc_ok) { snprintf(op->error, sizeof op->error, "Facing: no operation record to take its parameters from"); return 0; }
    int style = mcs(p, 0x1228), move = mcs(p, 0x122a), zz = style == 0;
    double lintol = mcd(p, 0x1230); if (lintol < 1e-12) lintol = MC_TOL;
    double so = mcd(p, 0x1240) * D * 0.01, along = mcd(p, 0x1250) * D * 0.01, across = mcd(p, 0x1260) * D * 0.01;
    double appr = mcd(p, 0x1270) * D * 0.01, exitd = mcd(p, 0x1280) * D * 0.01;
    int rev = mcb(p, 0x12c1), climb = mcb(p, 0x12c0);
    Gls bounds; if (!face_bounds(f, op, &bounds, lintol)) return 0;
    FW w = { 0 }; w.b = b; w.p = p; w.first = 1;
    int metric = mcam_is_metric(f), ok = 1;
    for (int bi = 0; bi < bounds.n && ok; bi++) {
        const Gl *L = &bounds.l[bi]; int lastb = bi == bounds.n - 1;
        fw_set_depths(&w);
        double ang = mcb(p, 0x1288) ? face_auto_angle(L) : mcd(p, 0x1290);   /* rough_angle (radians) */
        if (climb) ang += M_PI;
        w.ca = cos(ang); w.sa = sin(ang);
        if (style == 5) { ok = face_dynamic(&w, op, L, lastb, metric); continue; }
        /* FUN_180098d60: the boundary offset out by along - D/2 (ccomp_pock, min fillet pi = sharp corners) */
        Gls one = { (Gl *)L, 1, 1 }, O = gls_offset(&one, along - R, 0, fmax(p->tol, MC_TOL) * 0.1);
        if (O.n == 1) face_sharpen(&O.l[0], L, along - R);
        if (O.n != 1) { gls_free(&O); snprintf(op->error, sizeof op->error, "Facing: Error compensating the stock boundary.  Please set across and along overlaps to at least 50%% (Mastercam warning 0xa5c)"); ok = 0; break; }
        Gl B = gl_copy(L); gl_rot(&B, -ang); gl_rot(&O.l[0], -ang);
        double bb[4]; gl_bbox(&B, bb); gl_free(&B);
        double ylo = bb[1], yhi = bb[3], e = across - R, step = 0, y0; int n;
        ylo -= e; yhi += e;
        if (yhi < ylo) { gls_free(&O); snprintf(op->error, sizeof op->error, "Facing: Error compensating the stock boundary.  Please set across and along overlaps to at least 50%% (Mastercam warning 0xa5c)"); ok = 0; break; }
        if (style == 3) {
            if (D < (yhi - ylo) - MC_NC_TOL) { gls_free(&O); snprintf(op->error, sizeof op->error, "Facing: Tool diameter is not large enough to face the part in one pass (Mastercam warning 0xa5a)"); ok = 0; break; }
            y0 = (yhi + ylo) * 0.5; n = 1;
        } else {
            if (!(so > 0)) { gls_free(&O); snprintf(op->error, sizeof op->error, "Facing: max. stepover must be > 0"); ok = 0; break; }   /* ours: Mastercam divides by it */
            y0 = ylo + MC_NC_TOL;
            double rng = (yhi - MC_NC_TOL) - y0, q = rng / so + 0.999999999999;
            n = (int)(q >= 0 ? floor(q) : ceil(q)) + 1; if (n < 3) n = 2;
            if (zz && rev && (n & 1)) n++;                               /* Even number of passes */
            step = rng / (n - 1);
        }
        int dir0 = climb ? -1 : 1, linkg = 1; double linkf = p->feed;
        if (zz) {
            if (move == 2) {
                if (mcd(p, 0x1250) <= 100.0) { gls_free(&O); snprintf(op->error, sizeof op->error, "Facing: The along overlap must be greater than 100%% when Rapid is selected for Move Between Cuts (Mastercam warning 0xa5b)"); ok = 0; break; }
                linkg = 0;
            } else if (mcb(p, 0x1298) && cam_face_link_feed_on()) linkf = mcd(p, 0x12a0);
        }
        int ktd = p->mc_depth_on && (style == 0 || style == 3) && p->mc_keep_down;   /* local_4385 */
        int nd, nr; double rs; face_depths(p, w.top, p->final_z, &nd, &nr, &rs);
        double y = y0, pu = 0, pv_ = 0; int dir = dir0, state = 0;
        for (int k = 1; k <= nd && ok; k++) {
            state = k == 1 ? (bi == 0 ? 2 : 1) : (ktd ? 3 : 1);
            double z = face_z(k, nd, nr, rs, p->final_z, p->mc_depth_fs);
            if (ktd && k >= 2) { step = -step; dir = -dir; } else { dir = dir0; y = y0; }
            for (int i = 0; i < n && ok; i++) {
                double ulo, uhi;
                if (!face_span(&O.l[0], y, MC_NC_TOL * 0.01, &ulo, &uhi)) { snprintf(op->error, sizeof op->error, "Facing: Error creating the facing toolpath (Mastercam warning 0xa5d)"); ok = 0; break; }
                if (style == 4 && rev && i == n - 1) dir = -dir;      /* Reverse direction of last pass */
                if (i == 0 || !zz) { if (dir < 0) uhi += appr; else ulo -= appr; }
                if (i == n - 1 || !zz) { if (dir < 1) ulo -= exitd; else uhi += exitd; }
                double su = dir < 1 ? uhi : ulo, eu = dir < 1 ? ulo : uhi;
                if (state == 0) {
                    if (move == 0) {   /* High speed loops: both ends to the outer one, a half circle between (gr_arc_mill) */
                        int cw;
                        if (dir < 0) { su = fmax(su, pu); cw = 0; } else { su = fmin(su, pu); cw = 1; }
                        if (step < 0) cw = !cw;
                        pu = su;
                        fw_lin(&w, 1, pu, pv_, z, p->feed);
                        MRec r; memset(&r, 0, sizeof r); r.k = 'A'; r.cw = cw; r.z = z; r.feed = linkf;
                        fw_world(&w, su, y, &r.x, &r.y); fw_world(&w, su, y - step * 0.5, &r.cx, &r.cy);
                        mw_arc(b, p, &r);
                    } else fw_lin(&w, linkg, su, y, z, linkf);        /* Linear / Rapid (the G1 to the previous end is a no-op) */
                } else {
                    double sx, sy; fw_world(&w, su, y, &sx, &sy);
                    if (state <= 2) { fw_plunge(&w, sx, sy, z, w.first); w.first = 0; }
                    if (state == 3) fw_lin(&w, 1, pu, pv_, z, p->plunge_feed);   /* keep tool down: down at the previous end */
                    state = !zz;
                }
                pu = eu; pv_ = y;
                fw_lin(&w, 1, eu, y, z, p->feed);
                fw_adjust(&w, z);
                if (k == nd && i == n - 1 && w.clr_on && p->mc_clr_end && !lastb) fw_retract(&w);
                else if (k == nd && i == n - 1) fw_clear(&w);
                else if ((style == 4 && (i != n - 2 || !rev)) || (i == n - 1 && !ktd)) fw_retract(&w);
                if (i < n - 1) { y += step; if (zz) dir = -dir; }
            }
        }
        gls_free(&O);
    }
    gls_free(&bounds);
    if (!ok) return 0;
    if (!b->n) { snprintf(op->error, sizeof op->error, "Facing: no passes"); return 0; }
    return 1;
}
