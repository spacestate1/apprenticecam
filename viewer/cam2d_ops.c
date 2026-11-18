/* cam2d_ops.c - Face, Slot Mill, Helix Bore / Circle Mill (mcmill.dll engines), Thread Mill. Included by cam2d.c (uses its helpers). */

/* ------------------------------------------------------------------------------------------------ face */
/* the decoded engine (mcmill bnci_face, cam2d_face.c, batch 7) for every Face op with its record (new ops and stored ops as
   read): decisions batch dec1 (formats/user_decisions.md B7-1) removed batch 7's stored-op gate - the user's three stored Face
   ops (BRACKET4 / bracket4-dashed op 8, buckle-v8 op 41) then regenerate Mastercam's NCI (formats/opdlg_port_audit.md 11.1 /
   "Decisions batch dec1"). FACE_DECODED=1 (the former testing switch) is now the default and harmless; FACE_DECODED=0 (ours,
   testing / comparison only) keeps the former measured engine below. An op without the record block (mc_ok 0) keeps it too. */
static int gen_face_mc(const McamFile *f, CamOp *op, MB *b);   /* cam2d_face.c */
static int face_decoded(const CamParams *p) {
    if (!p->mc_ok) return 0;
    const char *e = getenv("FACE_DECODED"); if (e && !strcmp(e, "0")) return 0;
    return 1;
}
static int gen_face(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (face_decoded(p)) return gen_face_mc(f, op, b);
    /* the measured engine (stored ops as read): this program's style codes 0 zigzag, 1 one way, 2 one pass, 4 = its
       "dynamic" (measured on buckle-v8 op 41, Mastercam's One way + reverse last pass) */
    double R = p->tool_dia / 2, D = p->tool_dia, zl[200], ztop;
    CPaths raw = chain_paths(f, op, 1, &ztop);
    if (!raw.n && p->use_circ && p->circ[2] > 0) {   /* the cylindrical stock's circle boundary (make_facing_pock_bound), faced as a
                                                       closed chain would be; chords within the op tolerance */
        double r = p->circ[2], h = acos(fmax(-1.0, 1 - fmax(p->tol, 1e-6) / r));
        int n = h > 0 ? (int)ceil(M_PI / h) : 360; if (n < 8) n = 8; if (n > 3600) n = 3600;
        raw.p = xmalloc(sizeof(CPath)); raw.n = 1; raw.p[0].n = n; raw.p[0].xy = xmalloc(sizeof(double) * 2 * n);
        for (int k = 0; k < n; k++) { raw.p[0].xy[2 * k] = p->circ[0] + r * cos(2 * M_PI * k / n); raw.p[0].xy[2 * k + 1] = p->circ[1] + r * sin(2 * M_PI * k / n); }
    }
    /* styles (op +0x1228): 0 zigzag, 1 one way, 2 one pass (down the middle), 4 dynamic.  Mastercam: each pass spans the
     * chains grown by along - R; dynamic grows them by the full along overlap (buckle-v8 op 41: 110% of D 3 = 3.3) */
    int dyn = p->face_style == 4;
    CPaths reg = { 0 };
    if (raw.n && p->face_along > 0) { CPaths u = cl_union_evenodd(&raw); reg = cl_offset(&u, D * p->face_along / 100 - (dyn ? 0 : R), p->tol); cl_free(&u); }
    int tot = 4; for (int i = 0; i < raw.n; i++) tot += raw.p[i].n;   /* every point: the box must span the whole region */
    double *xs = xmalloc(sizeof(double) * tot), *ys = xmalloc(sizeof(double) * tot); int np = 0;
    if (raw.n) { for (int i = 0; i < raw.n; i++) for (int k = 0; k < raw.p[i].n; k++) { xs[np] = raw.p[i].xy[2 * k]; ys[np] = raw.p[i].xy[2 * k + 1]; np++; } }
    else if (p->use_box) { double bx[4] = { p->box[0], p->box[2], p->box[2], p->box[0] }, by[4] = { p->box[1], p->box[1], p->box[3], p->box[3] };
        for (int k = 0; k < 4; k++) { xs[np] = bx[k]; ys[np] = by[k]; np++; } }
    cl_free(&raw);
    if (!np) { free(xs); free(ys); snprintf(op->error, sizeof op->error, "face needs a closed chain (or the stock box)"); return 0; }
    /* work in a frame rotated so the cut runs along +X */
    double a = p->face_angle * M_PI / 180, ca = cos(a), sa = sin(a);
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    for (int k = 0; k < np; k++) {
        double u = xs[k] * ca + ys[k] * sa, v = -xs[k] * sa + ys[k] * ca;
        if (u < lo[0]) lo[0] = u; if (u > hi[0]) hi[0] = u; if (v < lo[1]) lo[1] = v; if (v > hi[1]) hi[1] = v;
    }
    free(xs); free(ys);
    double ov = D * p->face_overlap / 100;                        /* how far the tool edge goes past the side edges */
    double step = D * p->stepover_pct / 100; if (step < D * 0.05) step = D * 0.05;
    double v0 = lo[1] + R - ov, v1 = hi[1] - R + ov;
    /* Mastercam spaces passes evenly with floor(range / step) + 1 intervals (27 mm at 1.5 -> 19, 3 mm at 2.25 -> 2) */
    int n = v1 > v0 + 1e-9 ? (reg.n ? (int)floor((v1 - v0) / step + 1e-9) + 2 : (int)ceil((v1 - v0) / step - 1e-9) + 1) : 1;
    if (p->face_style == 2) n = 1;
    double dv = n > 1 ? (v1 - v0) / (n - 1) : 0;
    if (n == 1) v0 = v1 = (lo[1] + hi[1]) / 2;
    double u0 = lo[0] - R - D * 0.5, u1 = hi[0] + R + D * 0.5;      /* approach / exit clear of the stock */
#define W(U, V, Z, RAPID, F) mv(b, RAPID, (U) * ca - (V) * sa, (U) * sa + (V) * ca, Z, F)
    int nz = zlevels(p, zl, 200);
    double fp = p->top_z + p->feed_plane;
    for (int k = 0; k < nz; k++) {
        double z = zl[k];
        int fwd = 1, started = 0;
        if (!reg.n) { W(u0, v0, k ? fp : p->clearance, 1, 0); W(u0, v0, fp, 1, 0); W(u0, v0, z, 0, p->plunge_feed); started = 1; }
        for (int i = 0; i < n; i++) {
            /* Mastercam starts at +v; dynamic starts at -v, cuts its first pass one way, then zigzags (buckle-v8 op 41) */
            double v = reg.n && !dyn ? v1 - dv * i : v0 + dv * i, ua = fwd ? u0 : u1, ub = fwd ? u1 : u0;
            if (reg.n) {                                          /* this pass: where it crosses the grown chains */
                double ulo = 1e300, uhi = -1e300;
                for (int L = 0; L < reg.n; L++) for (int j = 0; j < reg.p[L].n; j++) {
                    const double *P = &reg.p[L].xy[2 * j], *Q = &reg.p[L].xy[2 * ((j + 1) % reg.p[L].n)];
                    double pv = -P[0] * sa + P[1] * ca, qv = -Q[0] * sa + Q[1] * ca;
                    if ((pv - v) * (qv - v) > 0 || pv == qv) continue;
                    double t = (v - pv) / (qv - pv), pu = P[0] * ca + P[1] * sa, qu = Q[0] * ca + Q[1] * sa, uu = pu + (qu - pu) * t;
                    if (uu < ulo) ulo = uu; if (uu > uhi) uhi = uu;
                }
                if (ulo > uhi) continue;
                ua = fwd ? ulo : uhi; ub = fwd ? uhi : ulo;
            }
            if (!started) { W(ua, v, k ? fp : p->clearance, 1, 0); W(ua, v, fp, 1, 0); W(ua, v, z, 0, p->plunge_feed); started = 1; }
            else {
                if (p->face_style == 0 || (dyn && i > 1)) W(ua, v, z, 0, p->feed);   /* zigzag: step over outside the stock */
                else { V3 e = lastp(b); mv(b, 1, e.x, e.y, fp, 0); W(ua, v, fp, 1, 0); W(ua, v, z, 0, p->plunge_feed); }
            }
            W(ub, v, z, 0, p->feed);
            if (p->face_style == 0 || (dyn && i > 0)) fwd = !fwd;
            else { /* one way: always u0 -> u1 */ }
        }
        V3 e = lastp(b); mv(b, 1, e.x, e.y, fp, 0);
    }
#undef W
    cl_free(&reg);
    if (!b->n) { snprintf(op->error, sizeof op->error, "face: no passes"); return 0; }
    V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance, 0);
    return 1;
}

/* ================================================================================================ mcmill.dll engines
 * Slot Mill, Helix Bore and Circle Mill as decoded in ../formats/mcmill_2d_engines.md ("spec §n"), from mcmill.dll
 * (decomp/mcmill_all.c, functions cited by name / address). Record offsets are part-file OP record offsets (spec
 * §0.1); CamParams.mc_p holds the engines' parameter block P = record 0x1228.. (set_slotmill_prm / set_circmill_prm).
 * Where Ghidra lost a call argument it was read from the disassembly of ~/mastercam-re/files/mcmill.dll (objdump),
 * cited as "dis <address>". A branch the spec marks NOT DECODED fails the operation with op->error naming it.
 *
 * mccore's plunge_move / retract_move / clear_move are ported below (mw_*, decomp/mccore_moves.c); set_depths is our
 * final_z / top_z; the NCI writers' subprogram (1018 / 1019) and colour flags have no counterpart here; arcs are
 * written as chords (the G-code output fits them back, cam_write_gcode). */

/* the system tolerances the engines compare against (mccore globals tol_exref / nc_tol_exref / min_arc_length_exref =
   config 1000 / 1008 / 1003): defaults of mccore FUN_1803bc7d0 (decomp/mccore_systol.c) - tol 5e-5 in both units,
   nc_tol and min_arc_length per the part's units (cam2d.c g_nc_tol / g_min_arc, set by cam_generate) */
#define MC_TOL CAM_SYS_TOL
#define MC_NC_TOL g_nc_tol
#define MC_MIN_ARC g_min_arc

static double mcd(const CamParams *p, int rec) { double v; memcpy(&v, p->mc_p + (rec - 0x1228), 8); return v; }
static int mcs(const CamParams *p, int rec) { int16_t v; memcpy(&v, p->mc_p + (rec - 0x1228), 2); return v; }
static int mcb(const CamParams *p, int rec) { return p->mc_p[rec - 0x1228]; }
/* mccore.dll compensation-type tests (dis mccore 0x18009de10 / 0x18009de30 / 0x18009de40) */
static int cc_in_computer(int t) { return (t & 0xfffc) == 0 && t != 1; }   /* 0 computer, 2 / 3 wear */
static int cc_in_control(int t) { return (uint16_t)(t - 1) <= 2; }        /* 1 control, 2 / 3 wear */
static int cc_in_control_only(int t) { return t == 1; }

int nc_cutter_comp_on(void);   /* nc_post.c: MCAM_CUTTER_COMP=1 (formats/post.md) */
static int mc_common_ok(CamOp *op, const char *what) {
    const CamParams *p = &op->p;
    if (!p->mc_ok) { snprintf(op->error, sizeof op->error, "%s: no Mastercam operation record to take its parameters from (stored op or the Mill_*.mcam-defaults default op)", what); return 0; }
    if (cc_in_control_only(p->mc_comp) && !(p->type == CAM_SLOT && nc_cutter_comp_on())) {   /* batch 7: Slot Mill with the switch (slot_chain) */   /* the engines then leave the offset to G41 / G42 (cc_control_gcode), which this program's post does not write */
        snprintf(op->error, sizeof op->error, "%s: compensation 'control' (G41/G42 in the control) is not output by this program; use computer, wear or off", what); return 0; }
    return 1;
}

/* pass table of FUN_180218340 (slot) / FUN_1800412d0 (circle mill), spec §0.4 step 6: offset of pass i (0 = first) */
typedef struct { int nr, nf; double rough, fs, base; } MPass;
static double mpass_off(const MPass *t, int i) {
    int k = t->nr + t->nf - 1 - i, a = k - t->nf > 0 ? k - t->nf : 0;
    return (k - a) * t->fs + a * t->rough + t->base;
}
/* depth passes, spec §0.4 (FUN_180218e00 / bnci_circmill): T = top_z, zf = final_z (set_depths) */
static MPass mc_depths(const CamParams *p, int on, double maxstep, int nf, double fs) {
    MPass t = { 0 };
    if (on && maxstep > 0) {
        double dz = (p->top_z - p->final_z) - (nf > 0 ? nf : 0) * fs;
        if (dz > 0) { int nr = (int)floor(dz / maxstep + 0.999999); t.nr = nr > 0 ? nr : 1; t.rough = dz / t.nr; }
        t.nf = nf; t.fs = fs;
        if (t.nf != 0 || t.nr != 0) return t;
    }
    t.nr = 0; t.rough = 0; t.nf = 1; t.fs = 0;
    return t;
}

/* motion records as the engines build them (ctour_rec / the 0x40-byte helix and circle records): 'L' line or 'A' arc
   to (x, y) at z (arcs: centre, cw, full = a whole turn back to the same point), each with its feed */
typedef struct { char k; int cw, full; double x, y, cx, cy, z, feed; } MRec;
typedef struct { MRec *r; int n, cap; } MList;
static void ml_add(MList *l, char k, double x, double y, double cx, double cy, int cw, int full, double z, double feed) {
    if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 64; l->r = xrealloc(l->r, l->cap * sizeof(MRec)); }
    MRec *r = &l->r[l->n++]; r->k = k; r->x = x; r->y = y; r->cx = cx; r->cy = cy; r->cw = cw; r->full = full; r->z = z; r->feed = feed;
}
static void ml_line(MList *l, double x, double y, double z, double feed) { ml_add(l, 'L', x, y, 0, 0, 0, 0, z, feed); }
/* FUN_180142b00: an arc record; a part arc ending where the last record ends (within nc_tol, same z) is not added */
static void ml_arc(MList *l, double x, double y, double cx, double cy, int cw, int full, double z, double feed) {
    if (l->n && !full) { const MRec *q = &l->r[l->n - 1];
        if (hypot(q->x - x, q->y - y) < MC_NC_TOL && fabs(q->z - z) < MC_NC_TOL) return; }
    ml_add(l, 'A', x, y, cx, cy, cw, full, z, feed);
}

/* writer state: tool down in the cut or up (w->any: the op has plunged before) */
typedef struct { int down, any; } MW;
/* mccore set_depths 0x18089f680 (decomp/mccore_depths.c): init_depth = the feed plane, at least the final depth; equal
   to it -> the retract height when on, else the clearance when on */
static double mc_init(const CamParams *p) {
    double fp = p->top_z + p->feed_plane;
    if (fp < p->final_z) fp = p->final_z;
    if (fabs(fp - p->final_z) < MC_TOL) { if (p->retract_z > -1e29) return p->retract_z; if (p->mc_clr_on) return p->clearance; }
    return fp;
}
/* the retract-rate feed up to init_depth (op +0x320 = record 0x388) unless rapid retract (op +0x141 = record 0x1a9); a
   rate of 0 has no feed to write: rapid */
static void mc_feed_up(MB *b, const CamParams *p, double x, double y, double z) {
    if (p->mc_rrate > 0) mv(b, 0, x, y, z, p->mc_rrate); else mv(b, 1, x, y, z, 0);
}
/* mccore retract_move 0x18089f390 (decomp/mccore_moves.c): feed to init_depth unless rapid retract, then rapid to the
   retract height when on, else the clearance when on, else init_depth */
static void mw_retract(MB *b, const CamParams *p, MW *w) {
    if (w->down && b->n) {
        V3 e = lastp(b); double z = e.z, init = mc_init(p);
        if (!p->mc_rapid_ret && z + MC_TOL < init) { mc_feed_up(b, p, e.x, e.y, init); z = init; }
        if (p->retract_z > -1e29) { if (z + MC_TOL < p->retract_z) mv(b, 1, e.x, e.y, p->retract_z, 0); }
        else if (p->mc_clr_on) { if (z + MC_TOL < p->clearance) mv(b, 1, e.x, e.y, p->clearance, 0); }
        else if (z + MC_TOL < init) mv(b, 1, e.x, e.y, init, 0);
    }
    w->down = 0;
}
/* mccore clear_move 0x18089e6a0: feed to init_depth unless rapid retract, then rapid to the clearance when on, else the
   retract height when on, else (rapid retract) init_depth */
static void mw_clear(MB *b, const CamParams *p, MW *w) {
    if (b->n) {
        V3 e = lastp(b); double z = e.z, init = mc_init(p);
        if (!p->mc_rapid_ret && z + MC_TOL < init) { mc_feed_up(b, p, e.x, e.y, init); z = init; }
        if (p->mc_clr_on) { if (z + MC_TOL < p->clearance) mv(b, 1, e.x, e.y, p->clearance, 0); }
        else if (p->retract_z > -1e29) { if (z + MC_TOL < p->retract_z) mv(b, 1, e.x, e.y, p->retract_z, 0); }
        else if (p->mc_rapid_ret && z + MC_TOL < init) mv(b, 1, e.x, e.y, init, 0);
    }
    w->down = 0;
}
/* mccore plunge_move 0x18089ed00: tool up - with param_5 a rapid at the clearance (when on) or else the retract height
   (when on), each only if above init_depth and the target; then a rapid to init_depth (or the target if higher). Tool
   down - a feed across at the current Z. Then the plunge feed (op +0x318) to the target. param_5 (the callers
   FUN_180218410 / FUN_180145ba0 l.228807 / FUN_180042070 l.46510): 0 only when the clearance is on, "only at start and
   end" (record 0x319) and this is not the first approach */
static void mw_plunge(MB *b, const CamParams *p, MW *w, double x, double y, double z) {
    double init = mc_init(p);
    int go = !(p->mc_clr_on && p->mc_clr_end && w->any);
    if (!w->down) {
        if (p->mc_clr_on) { if (init + MC_TOL < p->clearance && z + MC_TOL < p->clearance && go) mv(b, 1, x, y, p->clearance, 0); }
        else if (p->retract_z > -1e29 && init + MC_TOL < p->retract_z && z + MC_TOL < p->retract_z && go) mv(b, 1, x, y, p->retract_z, 0);
        mv(b, 1, x, y, z + MC_TOL < init ? init : z, 0);
    } else if (b->n) {
        V3 e = lastp(b);
        if (hypot(e.x - x, e.y - y) > MC_TOL) mv(b, 0, x, y, e.z, p->feed);
    }
    mv(b, 0, x, y, z, p->plunge_feed);
    w->down = w->any = 1;
}
static void mw_line(MB *b, double x, double y, double z, double feed) {
    V3 e = lastp(b);
    if (hypot(e.x - x, e.y - y) > 1e-9 || fabs(e.z - z) > 1e-9) mv(b, 0, x, y, z, feed);
}
/* an arc record from the current point as chords (chord step sqrt(8 tol / r), the step FUN_1802189e0 and the helix
   engines use for their own linearisation), z linear along it */
static void mw_arc(MB *b, const CamParams *p, const MRec *r) {
    V3 s = lastp(b);
    double rad = hypot(r->x - r->cx, r->y - r->cy), rs = hypot(s.x - r->cx, s.y - r->cy);
    double a0 = atan2(s.y - r->cy, s.x - r->cx), a1 = atan2(r->y - r->cy, r->x - r->cx), sw;
    if (!r->full && hypot(s.x - r->x, s.y - r->y) < 1e-9) { mw_line(b, r->x, r->y, r->z, r->feed); return; }
    if (r->full) sw = r->cw ? -2 * M_PI : 2 * M_PI;
    else if (!r->cw) { sw = a1 - a0; while (sw <= 0) sw += 2 * M_PI; while (sw > 2 * M_PI) sw -= 2 * M_PI; }
    else { sw = a1 - a0; while (sw >= 0) sw -= 2 * M_PI; while (sw < -2 * M_PI) sw += 2 * M_PI; }
    double st = rad > 1e-9 ? sqrt(8 * p->tol / rad) : M_PI / 18; if (st > M_PI / 18) st = M_PI / 18;
    int k = (int)ceil(fabs(sw) / st); if (k < 2) k = 2; if (k > 100000) k = 100000;
    int from = b->n;
    for (int i = 1; i < k; i++) {
        double t = (double)i / k, a = a0 + sw * t, rr = rs + (rad - rs) * t;
        mv(b, 0, r->cx + rr * cos(a), r->cy + rr * sin(a), s.z + (r->z - s.z) * t, r->feed);
    }
    mv(b, 0, r->x, r->y, r->z, r->feed);
    if (fabs(rs - rad) <= MC_NC_TOL) mb_tag_arc(b, from, r->cx, r->cy, sw < 0);   /* the record's true arc for the post */
}
/* one record list out: first record by a plunge when the tool is up (force_plunge: retract first if it is down),
   else fed to; (sx, sy) = where an arc that comes first starts */
static _Thread_local int g_mw_rec1;   /* mw_out: the index of the first move written for record 1 (the second record) */
static void mw_out(MB *b, const CamParams *p, MW *w, const MList *l, int force_plunge, double sx, double sy) {
    g_mw_rec1 = -1;
    if (!l->n) return;
    if (force_plunge) mw_retract(b, p, w);
    const MRec *r0 = &l->r[0];
    int i = 0;
    if (!w->down) {
        if (r0->k == 'A') mw_plunge(b, p, w, sx, sy, r0->z);
        else { mw_plunge(b, p, w, r0->x, r0->y, r0->z); i = 1; }
    }
    for (; i < l->n; i++) {
        const MRec *r = &l->r[i];
        if (i == 1) g_mw_rec1 = b->n;
        if (r->k == 'A') mw_arc(b, p, r); else mw_line(b, r->x, r->y, r->z, r->feed);
    }
}

/* ------------------------------------------------------------------------------------------------ slot mill */
/* spec §1. bnci_slotmill 0x180216f80: if <apps_prefix>slot9.dll loads (load_special_app) Mastercam hands the op to it
   and none of this runs (slot9.dll NOT DECODED); this is the bnci_slotmill path. */
typedef struct { int arc; double ax, ay, bx, by, cx, cy, r, a0, sw; } BEnt;   /* pock_bound record: line a->b, arc */
static void bent_rev(BEnt *e) {
    double t;
    t = e->ax; e->ax = e->bx; e->bx = t; t = e->ay; e->ay = e->by; e->by = t;
    if (e->arc) { e->a0 += e->sw; e->sw = -e->sw; }
}
/* FUN_180216d60: both lines, parallel (||cos| - 1| <= 2e-7) and j's end within tol of line i */
static int bent_merge_ok(const BEnt *i, const BEnt *j) {
    if (i->arc || j->arc) return 0;
    double ux = i->bx - i->ax, uy = i->by - i->ay, lu = hypot(ux, uy), vx = j->bx - j->ax, vy = j->by - j->ay, lv = hypot(vx, vy);
    if (lu == 0) ux = uy = 0; else { ux /= lu; uy /= lu; }
    if (lv == 0) vx = vy = 0; else { vx /= lv; vy /= lv; }
    if (fabs(fabs(ux * vx + uy * vy) - 1) > 2e-7) return 0;
    return fabs((j->bx - i->ax) * -uy + (j->by - i->ay) * ux) <= MC_TOL;
}
static void bent_poly(const BEnt *e, int n, double tol, CPath *out) {   /* closed loop, starting at e[0].a */
    int cap = 64; out->n = 0; out->xy = xmalloc(cap * 2 * sizeof(double));
#define BP(X, Y) do { if (out->n == cap) { cap *= 2; out->xy = xrealloc(out->xy, cap * 2 * sizeof(double)); } \
                      out->xy[2 * out->n] = (X); out->xy[2 * out->n + 1] = (Y); out->n++; } while (0)
    for (int i = 0; i < n; i++) {
        BP(e[i].ax, e[i].ay);
        if (e[i].arc) {
            double st = e[i].r > 1e-9 ? sqrt(8 * tol / e[i].r) : M_PI / 18; if (st > M_PI / 18) st = M_PI / 18;
            int k = (int)ceil(fabs(e[i].sw) / st); if (k < 2) k = 2;
            for (int j = 1; j < k; j++) { double a = e[i].a0 + e[i].sw * j / k; BP(e[i].cx + e[i].r * cos(a), e[i].cy + e[i].r * sin(a)); }
        }
    }
#undef BP
}
static double cpath_area(const CPath *c) {
    double A = 0;
    for (int i = 0; i < c->n; i++) { int j = (i + 1) % c->n; A += c->xy[2 * i] * c->xy[2 * j + 1] - c->xy[2 * j] * c->xy[2 * i + 1]; }
    return A / 2;
}
/* ccomp_pock_from_ctour (mccore, NOT DECODED internals): the boundary moved d inwards. Our offset (Clipper, sharp
   joins: the slot boundary is convex at its corners), oriented like the boundary, started nearest to the boundary
   start moved d along its inward normal (nx, ny). d <= 0: the boundary itself. */
static int slot_loop(const CPath *bnd, double d, int ccw, double nx, double ny, double tol, CPath *out) {
    out->n = 0; out->xy = NULL;
    if (d <= 0) { out->n = bnd->n; out->xy = xmalloc(bnd->n * 2 * sizeof(double)); memcpy(out->xy, bnd->xy, bnd->n * 2 * sizeof(double)); return 1; }
    CPaths in = { (CPath *)bnd, 1 };
    CPaths o = cl_offset_join(&in, -d, 1, tol);
    int best = -1; double ba = 0;
    for (int i = 0; i < o.n; i++) { double a = fabs(cpath_area(&o.p[i])); if (o.p[i].n >= 3 && a > ba) { ba = a; best = i; } }
    if (best < 0) { cl_free(&o); return 0; }
    out->n = o.p[best].n; out->xy = o.p[best].xy; o.p[best].xy = NULL;
    cl_free(&o);
    if ((cpath_area(out) > 0) != ccw) reverse_path(out);
    rotate_to(out, bnd->xy[0] + d * nx, bnd->xy[1] + d * ny);
    return 1;
}
static void loop_to_ml(const CPath *L, double z, const CamParams *p, MList *ml) {   /* ctour: start record, then the loop */
    ml_line(ml, L->xy[0], L->xy[1], z, p->plunge_feed);   /* FUN_180218410: record 1 at the plunge feed (0x380) */
    for (int i = 1; i <= L->n; i++) ml_line(ml, L->xy[2 * (i % L->n)], L->xy[2 * (i % L->n) + 1], z, p->feed);
}
/* FUN_180215d90: from the last loop's end E to the new loop start S: a line to E, a half circle E -> Q = E + u st
   about (E + Q) / 2, a half circle Q -> S about (Q + S) / 2, u = unit(E - S). The distance is the actual stepover st
   (dis 0x1802199c4..0x1802199ec: param_5 = xmm9 = Wr / (2n - 1), the value tested `0 < st` there); the arc direction
   byte is (param_6 == 0) with param_6 = [rsp+0x72] = (0x5d2 == 0) (dis 0x180218fff): CCW for left, CW for right. */
static void slot_connect(MList *ml, double ex, double ey, double sx, double sy, double st, int left, double z, const CamParams *p) {
    double ux = ex - sx, uy = ey - sy, l = hypot(ux, uy);
    if (l == 0) ux = uy = 0; else { ux /= l; uy /= l; }
    double qx = ex + ux * st, qy = ey + uy * st;
    ml_line(ml, ex, ey, z, p->plunge_feed);
    ml_add(ml, 'A', qx, qy, ex + ux * st * 0.5, ey + uy * st * 0.5, !left, 0, z, p->feed);
    ml_add(ml, 'A', sx, sy, (qx + sx) * 0.5, (qy + sy) * 0.5, !left, 0, z, p->feed);
}
/* FUN_1802178c0 ("SLOTMILL.cpp" create_ramped_ctour): ramp along the loop from zt down to z, then one loop at z.
   Returns 0 (nothing) when zt - z <= 1e-12. */
static int slot_ramp(const CPath *L, double ang_deg, double z, double zt, const CamParams *p, MList *ml) {
    if (!(zt - z > 1e-12)) return 0;
    int n = L->n; double tn = tan(ang_deg * 0.01745329251994329), per = 0;
    for (int i = 0; i < n; i++) per += hypot(L->xy[2 * ((i + 1) % n)] - L->xy[2 * i], L->xy[2 * ((i + 1) % n) + 1] - L->xy[2 * i + 1]);
    long cap = ((long)floor((zt - z) / (per * tn)) + 3) * n;   /* (laps + 3) x records */
    if (cap > 2000000) cap = 2000000;
    /* walk the loop backwards from its start, climbing length x tan(angle) per record; the record crossing zt is cut
       there (kept if longer than nc_tol) */
    double *w = xmalloc(sizeof(double) * 3 * (cap + 2)); long nw = 0;
    w[0] = L->xy[0]; w[1] = L->xy[1]; w[2] = z; nw = 1;
    double zc = z; int i = 0;
    for (long rec = 0; rec < cap; rec++) {
        int j = (i - 1 + n) % n;
        double px = L->xy[2 * i], py = L->xy[2 * i + 1], qx = L->xy[2 * j], qy = L->xy[2 * j + 1], len = hypot(qx - px, qy - py);
        double z1 = zc + len * tn;
        if (zt < z1) {
            if (z1 - zc >= MC_TOL) { double f = (zt - zc) / (z1 - zc);
                if (f * len > MC_NC_TOL) { w[3 * nw] = px + (qx - px) * f; w[3 * nw + 1] = py + (qy - py) * f; w[3 * nw + 2] = zt; nw++; } }
            break;
        }
        w[3 * nw] = qx; w[3 * nw + 1] = qy; w[3 * nw + 2] = z1; nw++;
        zc = z1; i = j;
    }
    /* reversed: from the top down to the loop start (each record ends at its lower z), start record at zt */
    ml_line(ml, w[3 * (nw - 1)], w[3 * (nw - 1) + 1], zt, p->plunge_feed);
    for (long k = nw - 2; k >= 0; k--) ml_line(ml, w[3 * k], w[3 * k + 1], w[3 * k + 2], p->feed);
    free(w);
    for (int k = 1; k <= n; k++) ml_line(ml, L->xy[2 * (k % n)], L->xy[2 * (k % n) + 1], z, p->feed);   /* one loop at z */
    return 1;
}

/* FUN_180216080 ("SLOTMILL.cpp" add_lead_in_out) with the lead fields FUN_180218e00 writes into the operation (SLead).
   Lead pieces from FUN_1801c24a0(gt, exit, line, side, out) (decomp/mcmill_all.c 0x1801c24a0):
     P / T = the gt's start (entry) or end (exit) point and tangent; for an entry T is reversed and the side flipped;
     N = left normal of T, negated for side 'R'.
     arc: centre P + r N, radius r, start angle = angle of P from the centre, sweep sw (negated for 'R'); an entry arc
          is turned round so that it ends at P (start angle + sweep, sweep negated).
     line: direction N when its perpendicular flag is set, else T; an entry line runs from P + len dir to P, an exit
          line from P to P + len dir.
   Entry: the arc on the first ctour gt, then the line on that arc; exit: the arc on the last gt, then the line on it.
   Each piece carries a ramp height (lead record +0x78 = op +0x8a0 / +0x8b8 / +0x8e8 / +0x900, record 0x908 / 0x920 /
   0x950 / 0x968, not written by the slot code): entry pieces end that much higher than the next one (going back from
   the loop start's z), exit pieces each that much higher than the previous (FUN_180216080 z loops). The gouge check
   FUN_1801c46f0 runs only with op +0x929 (record 0x991) set, which FUN_180218e00 clears before both calls. */
typedef struct { int en; double in_line, in_r, in_sw, out_line, out_r, out_sw; int in_perp, out_perp; } SLead;
/* point and travel direction at the start (end = 0) or end (end = 1) of record i of ml (from record i - 1) */
static void ml_pt_tan(const MList *ml, int i, int end, double *px, double *py, double *tx, double *ty) {
    const MRec *r = &ml->r[i], *q = &ml->r[i - 1];
    double x = end ? r->x : q->x, y = end ? r->y : q->y;
    *px = x; *py = y;
    if (r->k == 'A') { double a = atan2(y - r->cy, x - r->cx); *tx = -sin(a); *ty = cos(a); if (r->cw) { *tx = -*tx; *ty = -*ty; } }
    else { double dx = r->x - q->x, dy = r->y - q->y, l = hypot(dx, dy); *tx = l > 0 ? dx / l : 0; *ty = l > 0 ? dy / l : 0; }
}
static void slot_lead(MList *ml, const CamParams *p, int left, const SLead *L) {
    /* the last record, when a line, lengthened by P+0x28 (record 0x1250) */
    double ext = mcd(p, 0x1250);
    if (ext > 1e-12 && ml->n > 1 && ml->r[ml->n - 1].k == 'L') {
        MRec *r = &ml->r[ml->n - 1], *q = &ml->r[ml->n - 2]; double dx = r->x - q->x, dy = r->y - q->y, l = hypot(dx, dy);
        if (l > 0) { r->x += dx / l * ext; r->y += dy / l * ext; } }
    int in_line = L->in_line > 1e-12 && L->en, out_line = L->out_line > 1e-12 && L->en;
    int in_arc = L->in_r > 1e-12 && L->in_sw > 1e-12 && L->en, out_arc = L->out_r > 1e-12 && L->out_sw > 1e-12 && L->en;
    if (!(in_line || in_arc || out_line || out_arc) || ml->n < 2) return;
    MList o = { 0 };
    /* entry (side flipped: 'R' for a left slot) */
    double Px, Py, Tx, Ty, ax = 0, ay = 0, acx = 0, acy = 0; int acw = 0;
    ml_pt_tan(ml, 1, 0, &Px, &Py, &Tx, &Ty);
    double P0x = Px, P0y = Py, sx = Px, sy = Py, lx = 0, ly = 0;
    if (in_arc) {
        double nx = Ty, ny = -Tx;   /* left normal of -T ... */
        if (left) { nx = -nx; ny = -ny; }   /* ... negated for the flipped side 'R' */
        acx = Px + nx * L->in_r; acy = Py + ny * L->in_r;
        double sw = left ? -L->in_sw : L->in_sw, a = atan2(Py - acy, Px - acx); if (a < 0) a += 2 * M_PI;
        a += sw; sw = -sw; acw = sw < 0;
        ax = acx + L->in_r * cos(a); ay = acy + L->in_r * sin(a);
        Px = ax; Py = ay; Tx = -sin(a); Ty = cos(a); if (acw) { Tx = -Tx; Ty = -Ty; }   /* tangent at the arc start */
        sx = ax; sy = ay;
    }
    if (in_line) {
        double tx = -Tx, ty = -Ty, nx = -ty, ny = tx;
        if (left) { nx = -nx; ny = -ny; }
        double dx = L->in_perp ? nx : tx, dy = L->in_perp ? ny : ty;
        lx = Px; ly = Py; sx = Px + dx * L->in_line; sy = Py + dy * L->in_line;
    }
    double z = ml->r[0].z + (in_line ? p->mc_lead_dz[0] : 0) + (in_arc ? p->mc_lead_dz[1] : 0);
    ml_line(&o, sx, sy, z, p->plunge_feed);
    if (in_line) { z -= p->mc_lead_dz[0]; ml_add(&o, 'L', lx, ly, 0, 0, 0, 0, z, p->feed); }
    if (in_arc) { z -= p->mc_lead_dz[1]; ml_add(&o, 'A', P0x, P0y, acx, acy, acw, 0, z, p->feed); }
    if (!in_line && !in_arc) o.r[0] = ml->r[0];
    for (int i = 1; i < ml->n; i++) { MRec r = ml->r[i]; ml_add(&o, r.k, r.x, r.y, r.cx, r.cy, r.cw, r.full, r.z, r.feed); }
    /* exit (side as is) */
    ml_pt_tan(ml, ml->n - 1, 1, &Px, &Py, &Tx, &Ty);
    z = ml->r[ml->n - 1].z;
    if (out_arc) {
        double nx = -Ty, ny = Tx;
        if (!left) { nx = -nx; ny = -ny; }
        double cx = Px + nx * L->out_r, cy = Py + ny * L->out_r, sw = left ? L->out_sw : -L->out_sw;
        double a = atan2(Py - cy, Px - cx); if (a < 0) a += 2 * M_PI;
        double e = a + sw;
        Px = cx + L->out_r * cos(e); Py = cy + L->out_r * sin(e);
        z += p->mc_lead_dz[3]; ml_add(&o, 'A', Px, Py, cx, cy, sw < 0, 0, z, p->feed);
        Tx = -sin(e); Ty = cos(e); if (sw < 0) { Tx = -Tx; Ty = -Ty; }
    }
    if (out_line) {
        double nx = -Ty, ny = Tx;
        if (!left) { nx = -nx; ny = -ny; }
        double dx = L->out_perp ? nx : Tx, dy = L->out_perp ? ny : Ty;
        z += p->mc_lead_dz[2]; ml_add(&o, 'L', Px + dx * L->out_line, Py + dy * L->out_line, 0, 0, 0, 0, z, p->feed);
    }
    free(ml->r); *ml = o;
}

static int slot_ramp_exact(const BEnt *e, int n, int left, double d, double nx, double ny, double ang_deg, double z, double zt,
                           const CamParams *p, double gtol, MList *ml, double *ex, double *ey, char *err, size_t errn);   /* cam2d_slot_arc.c */
/* FUN_180218e00: one chain. Returns 1 output, 0 not a slot (warning 0xa5f), -1 error (op->error set). */
static int slot_chain(const McamFile *f, CamOp *op, const Chain *ch, MB *b, MW *w, int last) {
    CamParams *p = &op->p;
    double D = p->tool_dia;
    int ramp_on = mcb(p, 0x1262);
    /* 1. plunge-angle check (P+0x3a = 0x1262, P[10] = 0x1278): warning 0xa60, chain not cut */
    if (ramp_on && p->ramp_deg < 2e-7) { snprintf(op->error, sizeof op->error, "Slot Mill: ramp entry needs a plunge angle (Mastercam warning 0xa60)"); return -1; }
    /* 2. boundary (chain_to_pock_bound, closed chains) and FUN_180217ff0: consecutive collinear lines merged */
    if (!ch->closed || ch->n < 2) return 0;
    BEnt *e = xmalloc(sizeof(BEnt) * (ch->n + 2)); int n = 0;
    for (int k = 0; k < ch->n; k++) {
        const Ent *en = &f->ents[ch->ent[k]]; BEnt t; memset(&t, 0, sizeof t);
        if (en->type == E_LINE) { t.ax = en->a.x; t.ay = en->a.y; t.bx = en->b.x; t.by = en->b.y; }
        else if (en->type == E_ARC) { t.arc = 1; t.cx = en->c.x; t.cy = en->c.y; t.r = en->r; t.a0 = en->a0; t.sw = en->sw;
            t.ax = en->c.x + en->r * cos(en->a0); t.ay = en->c.y + en->r * sin(en->a0);
            t.bx = en->c.x + en->r * cos(en->a0 + en->sw); t.by = en->c.y + en->r * sin(en->a0 + en->sw); }
        else { free(e); return 0; }
        if (ch->rev[k]) bent_rev(&t);
        if (n && bent_merge_ok(&e[n - 1], &t)) { e[n - 1].bx = t.bx; e[n - 1].by = t.by; }
        else e[n++] = t;
    }
    if (n > 1 && bent_merge_ok(&e[0], &e[n - 1])) { e[0].ax = e[n - 1].ax; e[0].ay = e[n - 1].ay; n--; }
    /* 3. slot test FUN_180216990: the two longest lines (in boundary order; a longer third line drops the shorter of
       the two kept), parallel; W = distance of the first one's start from the second (dist_pt_l) */
    int L2[2], nl = 0;
#define LEN(i) hypot(e[i].bx - e[i].ax, e[i].by - e[i].ay)
    for (int i = 0; i < n; i++) {
        if (e[i].arc) continue;
        if (nl < 2) { L2[nl++] = i; continue; }
        double li = LEN(i);
        if (li > LEN(L2[0]) || li > LEN(L2[1])) { if (LEN(L2[0]) < LEN(L2[1])) L2[0] = L2[1]; L2[1] = i; }
    }
    if (nl < 2) { free(e); return 0; }
    double ux = e[L2[0]].bx - e[L2[0]].ax, uy = e[L2[0]].by - e[L2[0]].ay, vx = e[L2[1]].bx - e[L2[1]].ax, vy = e[L2[1]].by - e[L2[1]].ay;
    double lu = hypot(ux, uy), lv = hypot(vx, vy);
    if (lu == 0 || lv == 0 || fabs(fabs((ux * vx + uy * vy) / (lu * lv)) - 1) > 2e-7) { free(e); return 0; }
    double W = fabs((e[L2[0]].ax - e[L2[1]].ax) * vy - (e[L2[0]].ay - e[L2[1]].ay) * vx) / lv;
#undef LEN
    /* 4. FUN_180217520 break_slot_boundary: start and end at the midpoint of the first long line */
    {   int k = L2[0]; BEnt *r = xmalloc(sizeof(BEnt) * (n + 1)); int m = 0;
        double mx = (e[k].ax + e[k].bx) / 2, my = (e[k].ay + e[k].by) / 2;
        r[m] = e[k]; r[m].ax = mx; r[m].ay = my; m++;
        for (int i = 1; i < n; i++) r[m++] = e[(k + i) % n];
        r[m] = e[k]; r[m].bx = mx; r[m].by = my; m++;
        free(e); e = r; n = m; }
    /* 5. direction: side 'L' when 0x5d2 = 0 (pock_ccw must agree, else reverse_pock_bound) */
    int left = p->mc_comp_right == 0;
    CPath bnd; bent_poly(e, n, p->tol, &bnd);
    if ((cpath_area(&bnd) > 0) != left) {
        for (int i = 0, j = n - 1; i < j; i++, j--) { BEnt t = e[i]; e[i] = e[j]; e[j] = t; }
        for (int i = 0; i < n; i++) bent_rev(&e[i]);
        free(bnd.xy); bent_poly(e, n, p->tol, &bnd);
    }
    /* e is kept for the ramp on the exact loop (cam2d_slot_arc.c, decisions batch dec1 B7-3); freed at the end */
    double tx = bnd.xy[2] - bnd.xy[0], ty = bnd.xy[3] - bnd.xy[1], tl = hypot(tx, ty); tx /= tl; ty /= tl;
    double nx = left ? -ty : ty, ny = left ? tx : -tx;   /* inward normal at the start */
    /* 6. depth passes (spec §0.4; depth cuts on = 0x868 with max rough step 0x870 = stepdown) */
    MPass dp = mc_depths(p, p->stepdown > 0, p->stepdown, p->mc_depth_nf, p->mc_depth_fs);
    int nd = dp.nr + dp.nf;
    /* 7. XY: c = D/2 with computer compensation, s = XY stock; finish n_f = S16 0x1290, f_s = 0x1298 */
    double c = cc_in_computer(p->mc_comp) ? D * 0.5 : 0, s = p->stock_xy;
    int nf = mcs(p, 0x1290); double fs = mcd(p, 0x1298), sweep = mcd(p, 0x1248);
    double b0 = D * 0.5 + s + nf * fs, Wr = W - 2 * b0;
    if (Wr < 0) { free(bnd.xy); free(e); snprintf(op->error, sizeof op->error, "Slot Mill: slot width %.4g is less than tool + 2 x (stock + finish allowance) = %.4g", W, 2 * b0); return -1; }
    double rstep = p->stepover_pct * 0.01 * D;   /* 0x1270 = 0x1268 [%] x 0.01 x D (bnci_slotmill) */
    if (!(rstep > 0)) { free(bnd.xy); free(e); snprintf(op->error, sizeof op->error, "Slot Mill: stepover must be > 0"); return -1; }
    int nrl = (int)floor((Wr / rstep + 1) * 0.5 + 0.999999);
    double st = Wr / (nrl * 2.0 - 1);
    double zt = p->top_z;   /* the ramp's top: top of stock, then each pass's z (FUN_180218e00 dVar28 = dVar27 at the end of a pass) */
    int ok = 1;
    for (int i = 0; i < nd && ok; i++) {
        double z = p->final_z + mpass_off(&dp, i);
        int first = i == 0 || !p->mc_keep_down;   /* local_1f7: rapid + plunge_move on the pass's first ctour */
        int out_any = 0; double ex = 0, ey = 0;   /* bVar4, local_140 */
        int j0 = nrl;
        /* 8.1 ramp (0x1262): the innermost loop d = (n - 1) st + b, ramped from zt; the innermost loop is then not repeated.
           Decisions batch dec1 (formats/user_decisions.md B7-3): on the exact line / arc loop (cam2d_slot_arc.c, create_ramped_ctour
           FUN_1802178c0 + FUN_1802189e0: helical arc records as arcs with 0x1280 "Output helixes as arcs", else as lines at the
           angle step sqrt(8 x 0x1288 / r)); before: the polygon of slot_loop (slot_ramp below, kept for SLOT_RAMP_POLY=1, ours,
           comparison only) */
        if (ramp_on) {
            double d = (nrl - 1) * st + b0;
            if (nrl > 0 && d < W * 0.5 - MC_NC_TOL) {
                MList ml = { 0 };
                const char *rp = getenv("SLOT_RAMP_POLY");
                if (rp && !strcmp(rp, "1")) {
                    CPath L;
                    if (!slot_loop(&bnd, d, left, nx, ny, p->tol, &L)) { snprintf(op->error, sizeof op->error, "Slot Mill: offset failed (Mastercam warning 0x954)"); ok = 0; break; }
                    if (slot_ramp(&L, p->ramp_deg, z, zt, p, &ml)) {
                        mw_out(b, p, w, &ml, first, 0, 0); first = 0; out_any = 1;
                        ex = ml.r[ml.n - 1].x; ey = ml.r[ml.n - 1].y; }
                    free(L.xy);
                } else {
                    int r = slot_ramp_exact(e, n, left, d, nx, ny, p->ramp_deg, z, zt, p, cam_chain_tol(mcam_is_metric(f)) * 0.5, &ml, &ex, &ey, op->error, sizeof op->error);
                    if (r < 0) { free(ml.r); ok = 0; break; }
                    if (r > 0) { mw_out(b, p, w, &ml, first, 0, 0); first = 0; out_any = 1; }
                }
                free(ml.r);
                j0 = nrl - 1;
            }
        }
        /* 8.2 rough loops j = n - 1 .. 0, innermost first, offset j st + b */
        for (int j = j0 - 1; j >= 0 && ok; j--) {
            double d = j * st + b0;
            if (!(d < W * 0.5 - MC_NC_TOL)) continue;
            CPath L; MList ml = { 0 };
            if (!slot_loop(&bnd, d, left, nx, ny, p->tol, &L)) { snprintf(op->error, sizeof op->error, "Slot Mill: offset failed (Mastercam warning 0x954)"); ok = 0; break; }
            if (out_any) slot_connect(&ml, ex, ey, L.xy[0], L.xy[1], st, left, z, p);
            loop_to_ml(&L, z, p, &ml);
            if (j == 0 && nf == 0) {
                /* last rough loop, no finish passes: exit arc r_l / a, exit line r_l (perpendicular) with 0x1261, no entry;
                   r_l = Wr x (0.5 if a <= 1.5807963 rad else 0.25); leads only when the sweep is > 1e-12 (FUN_180216080
                   param_6 = param_7 = [rsp+0x78] = (1e-12 < sweep), dis 0x18021920f / 0x18021976b) */
                double a = sweep * 0.01745329251994329, rl = Wr * (a <= 1.5807963267948966 ? 0.5 : 0.25);
                SLead ld = { sweep > 1e-12, 0, 0, 0, mcb(p, 0x1261) ? rl : 0, rl, a, 0, 1 };
                slot_lead(&ml, p, left, &ld);
            }
            mw_out(b, p, w, &ml, first, 0, 0); first = 0; out_any = 1;
            ex = ml.r[ml.n - 1].x; ey = ml.r[ml.n - 1].y;   /* local_140 = the last record */
            free(ml.r); free(L.xy);
        }
        /* 8.3 finish passes k = 1 .. n_f at (n_f - k) f_s + c + s (the boundary itself when not > 1e-12 / not < W/2 - nc_tol) */
        for (int k = 1; k <= nf && ok; k++) {
            double d = (nf - k) * fs + c + s;
            CPath L; MList ml = { 0 };
            double dr = d;   /* r_l below uses the computed offset even when the boundary itself is cut */
            if (!(d > 1e-12 && d < W * 0.5 - MC_NC_TOL)) d = 0;
            if (!slot_loop(&bnd, d, left, nx, ny, p->tol, &L)) { snprintf(op->error, sizeof op->error, "Slot Mill: offset failed (Mastercam warning 0x954)"); ok = 0; break; }
            if (sweep <= 1e-12) {
                if (out_any && st > 0) slot_connect(&ml, ex, ey, L.xy[0], L.xy[1], st, left, z, p);
                loop_to_ml(&L, z, p, &ml);
            } else {
                /* entry and exit arcs r_l / a, with 0x1261 entry and exit lines r_l perpendicular (op +0x891 / +0x8d9 = 1);
                   r_l = (W - 2 offset) x (0.5 / 0.25); no link from the previous loop (the pass starts with a G1 to
                   the lead start at the plunge feed, FUN_180218410) */
                loop_to_ml(&L, z, p, &ml);
                double a = sweep * 0.01745329251994329, rl = (W - dr * 2) * (a <= 1.5807963267948966 ? 0.5 : 0.25);
                int pl = mcb(p, 0x1261);
                SLead ld = { 1, pl ? rl : 0, rl, a, pl ? rl : 0, rl, a, 1, 1 };
                slot_lead(&ml, p, left, &ld);
            }
            int from = b->n;
            mw_out(b, p, w, &ml, first, 0, 0); first = 0;
            /* FUN_180218410 on the finish passes with param_11 = param_12 = cc_in_control(0x5d0) (dis 0x180218fc4..0x180218fe8,
               0x180219b76..0x180219b82; the ramp and rough loop calls pass 0): record 2 carries cc_control_gcode (mccore
               cc_control_gcode_2: Control / Wear left 41 right 42, Reverse wear swapped) and the last record 140 (G40); with
               MCAM_CUTTER_COMP=1 only (ours, formats/post.md: without it today's computer-only output) */
            if (cc_in_control(p->mc_comp) && nc_cutter_comp_on() && g_mw_rec1 >= from && g_mw_rec1 < b->n && b->n - 1 > g_mw_rec1) {
                int g = (p->mc_comp == 3) ? (p->mc_comp_right ? 41 : 42) : (p->mc_comp_right ? 42 : 41);
                b->m[g_mw_rec1].cc = g; b->m[b->n - 1].cc = 140;
            }
            ex = ml.r[ml.n - 1].x; ey = ml.r[ml.n - 1].y;   /* local_140; bVar4 is not set by a finish pass */
            free(ml.r); free(L.xy);
        }
        zt = z;
    }
    free(bnd.xy); free(e);
    if (!ok) return -1;
    /* 9. clear_move when 0x179 and (0x319 = 0 or the last chain) */
    if (p->mc_clr_on && (!p->mc_clr_end || last)) mw_clear(b, p, w);
    return 1;
}

static int gen_slot(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (!mc_common_ok(op, "Slot Mill")) return 0;
    if (!op->nchains) { snprintf(op->error, sizeof op->error, "Slot Mill needs closed slot chains"); return 0; }
    MW w = { 0 }; int done = 0, notslot = 0;
    for (int ci = 0; ci < op->nchains; ci++) {
        int r = slot_chain(f, op, &op->chains[ci], b, &w, ci == op->nchains - 1);
        if (r < 0) return 0;
        if (r == 0) notslot++; else done++;
    }
    if (!done) { snprintf(op->error, sizeof op->error, "Slot Mill: no chain is a slot (%d checked: closed, two longest lines parallel; Mastercam warning 0xa5f)", notslot); return 0; }
    mw_clear(b, p, &w);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ helix bore / circle mill */
/* full or partial circle(s) around (cx, cy) radius r from angle a0, sweep sw (sign = direction), z linear z0 -> z1 */
static void arc_moves(MB *b, double cx, double cy, double r, double a0, double sw, double z0, double z1, double feed) {
    /* 5 degree steps, finer when needed to keep the chords within 0.004 of the circle */
    double amax = r > 0.004 ? fmin(M_PI / 36, 2 * acos(1 - 0.004 / r)) : M_PI / 36;
    int k = (int)ceil(fabs(sw) / amax); if (k < 2) k = 2; if (k > 100000) k = 100000;
    int from = b->n;
    for (int i = 1; i <= k; i++) { double a = a0 + sw * i / k; mv(b, 0, cx + r * cos(a), cy + r * sin(a), z0 + (z1 - z0) * i / k, feed); }
    if (fabs(sw) <= 2 * M_PI + 1e-9) mb_tag_arc(b, from, cx, cy, sw < 0);   /* the engine's arc (at most a full turn) */
}


/* ---- Helix Bore, spec §2 (bnci_helix_bore 0x180142c80) */
#define PT(cx, cy, r, a, X, Y) do { (X) = (cx) + (r) * cos(a); (Y) = (cy) + (r) * sin(a); } while (0)   /* FUN_180145b40 */
/* lead radius (FUN_180144ff0 / FUN_180143fb0): r / 2, or 0.55 D when control-only compensation and D > r - tol */
static double hb_lead_r(const CamParams *p, double r) {
    return cc_in_control_only(p->mc_comp) && p->tool_dia > r - MC_TOL ? p->tool_dia * 0.55 : r * 0.5;
}
/* spec §2.2 rough helix FUN_180144ff0(c, r, T, z) */
static void hb_rough(const CamParams *p, double cx, double cy, double r, double T, double z, MList *ml, double *sx, double *sy) {
    int cw = p->mc_comp_right == 1; double dir = cw ? -1 : 1;
    double F = p->feed, Fb = mcd(p, 0x1270), pitch = p->helix_pitch;    /* 0x378, 0x1270 bottom feed, 0x1250 rough pitch */
    double sweep = mcd(p, 0x1230), ov = mcd(p, 0x1238);                  /* entry / exit arc sweep (deg), overlap */
    int arcs = mcb(p, 0x12b0), ctr = mcb(p, 0x1248), perp = mcb(p, 0x1249);
    double Th = pitch > 1e-12 ? (T - z) * 2 * M_PI / pitch : 0;
    int m;
    if (!arcs) { double stp = sqrt(mcd(p, 0x12b8) * 8 / r); m = 2; if (stp >= 1e-12) { m = (int)(fabs(Th) / stp) + 1; if (m < 2) m = 2; } }
    else { m = (int)floor(fabs(Th) / (2 * M_PI) + 0.5) + 1; if (m < 2) m = 2; }
    double a0 = mcd(p, 0x1228) * 0.01745329251994329 - Th;               /* restrict_angle(0, 2 pi) */
    a0 = fmod(a0, 2 * M_PI); if (a0 < 0) a0 += 2 * M_PI;
    double H = T, X, Y;
    *sx = cx; *sy = cy;
    /* entry: a point at the centre (start at centre, sweep not 180); lead arc into (r, a0) about Lc = c + (r - rl) at a0
       (dis 0x180145329..0x180145330), from Lc + rl at a0 - dir sweep; with 0x1249 a line to Lc first */
    if (ctr && fabs(sweep - 180) > MC_TOL) ml_line(ml, cx, cy, H, F);
    if (ctr || cc_in_control(p->mc_comp) || perp) {
        double rl = hb_lead_r(p, r), lx, ly, qx, qy;
        PT(cx, cy, r - rl, a0, lx, ly);
        PT(lx, ly, rl, a0 - dir * 0.01745329251994329 * sweep, qx, qy);
        if (perp) ml_line(ml, lx, ly, H, F);
        ml_line(ml, qx, qy, H, F);
        PT(cx, cy, r, a0, X, Y); ml_arc(ml, X, Y, lx, ly, cw, 0, H, F);
    }
    double ang = a0;
    if (!arcs) {                                                          /* m points down the helix, all lines */
        for (int i = 0; i < m; i++) {
            double t = (double)i / (m - 1), zz = i == m - 1 ? z : (1 - t) * H + t * z;
            ang = t * dir * Th + a0; PT(cx, cy, r, ang, X, Y); ml_line(ml, X, Y, zz, F);
        }
    } else {                                                              /* whole turns each a pitch lower, then the rest */
        double rem = Th, piece = 0; int sg = Th >= -1e-6 ? 1 : -1;
        while (sg > 0 ? rem > 0 : fabs(rem) > 0) {
            int full = 0;
            if (ml->n > 0) {
                if (fabs(rem) <= 6.283184307179586) { ang += rem; piece = rem; rem = 0; H = z; }
                else { piece = sg * 2 * M_PI; rem -= piece; ang += piece; full = 1; H -= sg * pitch;
                       if (fabs(rem) < 1e-6) { rem = 0; H = z; } }
                while (ang > 2 * M_PI) ang -= 2 * M_PI; while (ang < -2 * M_PI) ang += 2 * M_PI;
            }
            PT(cx, cy, r, ang, X, Y);
            double len = fabs(piece * r);
            if (ml->n == 0 || len < MC_NC_TOL || len < MC_MIN_ARC) ml_line(ml, X, Y, H, F);
            else ml_arc(ml, X, Y, cx, cy, sg > 0 ? cw : !cw, full, H, F);
        }
    }
    /* bottom: a whole circle at z at the bottom feed, then the overlap arc dir x overlap / r */
    PT(cx, cy, r, ang, X, Y); ml_arc(ml, X, Y, cx, cy, cw, 1, z, Fb);
    if (ov > MC_MIN_ARC) { ang += dir * ov / r; PT(cx, cy, r, ang, X, Y); ml_arc(ml, X, Y, cx, cy, cw, 0, z, Fb); }
    /* exit: arc about M = (end + c) / 2, radius r / 2, to the angle (end angle) + dir sweep (dis 0x1801458d7..0x1801459a1);
       a line to the centre when farther than nc_tol */
    double te = atan2(Y - cy, X - cx); if (te < 0) te += 2 * M_PI;
    double mx = (X + cx) * 0.5, my = (Y + cy) * 0.5, qx, qy;
    PT(mx, my, r * 0.5, te + dir * 0.01745329251994329 * sweep, qx, qy);
    ml_arc(ml, qx, qy, mx, my, cw, 0, z, Fb);
    if (hypot(cx - qx, cy - qy) > MC_NC_TOL) ml_line(ml, cx, cy, z, Fb);
}
/* spec §2.3 finish helix FUN_180143fb0(c, r, T, z): lead in at z, circle, overlap, helix up to T (Z step 0x1280 per
   turn), finish feed 0x12a8; exit lead when control compensation or start at centre */
static void hb_finish(const CamParams *p, double cx, double cy, double r, double T, double z, MList *ml, double *sx, double *sy) {
    int cw = p->mc_comp_right == 1; double dir = cw ? -1 : 1;
    double Ff = mcd(p, 0x12a8), zs = mcd(p, 0x1280), sweep = mcd(p, 0x1230), ov = mcd(p, 0x1238);
    int arcs = mcb(p, 0x12b0), ctr = mcb(p, 0x1248), perp = mcb(p, 0x1249);
    double Th = zs > 1e-12 ? (T - z) * 2 * M_PI / zs : 0;
    int m;
    if (!arcs) { double stp = sqrt(mcd(p, 0x12b8) * 8 / r); m = 2; if (stp >= 1e-12) { m = (int)(fabs(Th) / stp) + 1; if (m < 2) m = 2; } }
    else { m = (int)floor(fabs(Th) / (2 * M_PI) + 0.5) + 1; if (m < 2) m = 2; }
    double a = fmod(mcd(p, 0x1228) * 0.01745329251994329, 2 * M_PI);   /* restrict_angle(-2 pi, 2 pi) */
    double rl = hb_lead_r(p, r), lx, ly, qx, qy, X, Y, H = z;
    /* lead: Lc = c + (r - rl) at a, start Lc + rl at a - dir sweep (dis 0x1801441c7..0x18014423f) */
    PT(cx, cy, r - rl, a, lx, ly);
    PT(lx, ly, rl, a - dir * 0.01745329251994329 * sweep, qx, qy);
    *sx = qx; *sy = qy;
    if (perp) { ml_line(ml, lx, ly, z, Ff); ml_line(ml, qx, qy, z, Ff); }
    else if (hypot(cx - qx, cy - qy) > MC_NC_TOL) ml_line(ml, qx, qy, z, Ff);
    PT(cx, cy, r, a, X, Y);
    ml_arc(ml, X, Y, lx, ly, cw, 0, z, Ff);
    ml_arc(ml, X, Y, cx, cy, cw, 1, z, Ff);
    if (ov > MC_MIN_ARC) { a += dir * ov / r; PT(cx, cy, r, a, X, Y); ml_arc(ml, X, Y, cx, cy, cw, 0, z, Ff); }
    double a1 = a;
    if (!arcs) {
        for (int i = 0; i < m; i++) {
            double t = (double)i / (m - 1); H = i == m - 1 ? T : (1 - t) * z + t * T;
            a = t * dir * Th + a1; PT(cx, cy, r, a, X, Y); ml_line(ml, X, Y, H, Ff);
        }
    } else if (Th > 0) {
        double rem = Th, piece = 0;
        for (;;) {
            int full = 0;
            if (rem <= 6.283184307179586) { a += rem; piece = rem; rem = 0; H = T; }
            else { rem -= 2 * M_PI; piece = 2 * M_PI; a += 2 * M_PI; full = 1; H += zs; if (fabs(rem) < 1e-6) { rem = 0; H = T; } }
            while (a > 2 * M_PI) a -= 2 * M_PI; while (a < -2 * M_PI) a += 2 * M_PI;
            PT(cx, cy, r, a, X, Y);
            double len = fabs(piece * r);
            if (len < MC_NC_TOL || len < MC_MIN_ARC) ml_line(ml, X, Y, H, Ff); else ml_arc(ml, X, Y, cx, cy, cw, full, H, Ff);
            if (rem <= 0) break;
        }
    }
    if (cc_in_control(p->mc_comp) || ctr) {   /* exit: Lc' = c + (r - rl) at the end angle, to Lc' + rl at it + dir sweep (dis 0x18014469c..0x180144719) */
        double te = atan2(Y - cy, X - cx); if (te < 0) te += 2 * M_PI;
        PT(cx, cy, r - rl, te, lx, ly);
        PT(lx, ly, rl, te + dir * 0.01745329251994329 * sweep, qx, qy);
        ml_arc(ml, qx, qy, lx, ly, cw, 0, H, Ff);
        if (perp) ml_line(ml, lx, ly, H, Ff);
        ml_line(ml, cx, cy, H, Ff);
    }
}

static int gen_helixbore(CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (!mc_common_ok(op, "Helix Bore")) return 0;
    if (!op->nholes) { snprintf(op->error, sizeof op->error, "Helix Bore: select circles or points"); return 0; }
    double D = p->tool_dia, cdia = mcd(p, 0x1240), T = p->top_z;
    int fin = p->finish_n > 0;                                        /* 0x1278 finish pass on */
    if (fin && (mcb(p, 0x12c1) || mcb(p, 0x12c0))) {
        snprintf(op->error, sizeof op->error, "Helix Bore: finish mode rec 0x%x (%s) is NOT DECODED", mcb(p, 0x12c1) ? 0x12c1 : 0x12c0, mcb(p, 0x12c1) ? "FUN_1801448f0" : "FUN_1801435a0"); return 0; }
    MW w = { 0 }; int done = 0, skipped = 0;
    for (int h = 0; h < op->nholes; h++) {
        double cx = op->holes[h].x, cy = op->holes[h].y;
        /* §2.1.1 FUN_180143390: an arc gives its radius, a point 0x1240 / 2; it writes 0x1240 = 2 rh back */
        double rh = op->holes[h].d > 0 ? op->holes[h].d / 2 : cdia / 2;
        if (!(rh > MC_TOL)) { skipped++; continue; }
        cdia = 2 * rh;
        if (rh <= D * 0.5) { skipped++; continue; }                   /* §2.1.2 warning 0xa56, point skipped */
        if (cc_in_control_only(p->mc_comp) && D > rh - MC_TOL) {
            snprintf(op->error, sizeof op->error, "Helix Bore: control-only compensation with the tool filling the hole: exit lead NOT DECODED"); return 0; }
        double zf = p->final_z, zb = zf + (p->mc_tip ? 0 : p->corner_r);   /* §2.1.3 corner radius unless to the tip */
        double rr = rh - p->stock_xy - (cc_in_computer(p->mc_comp) ? D * 0.5 : 0), rr0 = rr;   /* §2.1.4 */
        if (fin) rr -= p->finish_step;                                /* 0x1288 finish stepover */
        int n = mcs(p, 0x1258); double rs = mcd(p, 0x1260);          /* §2.1.5 rough passes, rough stepover */
        if (n < 1) n = 1;
        else if (n > 1 && rs > MC_TOL) { int k = (int)floor(rr / rs) + 1; if (k < n) n = k; }
        for (int k = 1; k <= n; k++) {
            double r = rr - (n - k) * rs;
            if (!(r > MC_TOL)) continue;
            MList ml = { 0 }; double sx, sy;
            hb_rough(p, cx, cy, r, T, zb, &ml, &sx, &sy);
            mw_out(b, p, &w, &ml, 1, sx, sy); mw_retract(b, p, &w);   /* FUN_180145ba0 (flags NOT DECODED): one cut, retract */
            free(ml.r);
        }
        if (fin) {                                                    /* §2.1.6 default finish mode */
            MList ml = { 0 }; double sx, sy;
            hb_finish(p, cx, cy, rr0, T, zf, &ml, &sx, &sy);
            mw_out(b, p, &w, &ml, 1, sx, sy); mw_retract(b, p, &w);
            free(ml.r);
        }
        done++;
        /* §2.1.7 clear_move with 0x179 and 0x189 (retract on) and (0x319 = 0 or the last point) */
        if (p->mc_clr_on && p->retract_z > -1e29 && (!p->mc_clr_end || h == op->nholes - 1)) mw_clear(b, p, &w);
    }
    if (!done) { snprintf(op->error, sizeof op->error, "Helix Bore: no hole larger than the tool (%d skipped, Mastercam warning 0xa56)", skipped); return 0; }
    mw_clear(b, p, &w);
    return 1;
}

/* ---- Circle Mill, spec §3 (bnci_circmill 0x18003f980) */
static double ang_pm_pi(double a) { while (a > M_PI) a -= 2 * M_PI; while (a < -M_PI) a += 2 * M_PI; return a; }
/* mcce.dll MCCECalcArcLnArcBetCir 0x180002d29 (decomp/mcce_all.c): from point E on a circle about c to a circle of
   radius R5 about c: arc 1 (centre c + (|E - c| + r) at E's angle, radius r, from E, sweep sw negative when ccw),
   a line along the tangent at its end A, arc 2 (radius r, centre C2 on the line offset 2r ... = A + 2(A - C1) + t dir,
   at R5 - r from c, the larger intersection parameter of FUN_1800a9000) ending on the R5 circle. Pieces shorter than
   1e-6 are dropped (FUN_1800ac800; all of them: the line stays). Returns the piece count, 0 = no transition. */
static int mcce_arc_ln_arc(double cx, double cy, double ex, double ey, int ccw, double sw, double R5, double r, MRec *pc, double z, double feed) {
    double dx = ex - cx, dy = ey - cy;
    if (fabs(dx) <= 1e-12 && fabs(dy) <= 1e-12) return 0;
    double an = atan2(dy, dx), rho = hypot(dx, dy);
    double c1x = cx + cos(an) * (rho + r), c1y = cy + sin(an) * (rho + r);
    double a1 = ang_pm_pi(an + M_PI), s1 = ccw ? -fabs(sw) : fabs(sw);
    double ax = c1x + r * cos(a1 + s1), ay = c1y + r * sin(a1 + s1);
    double vx = ax - c1x, vy = ay - c1y;
    if (fabs(vx) <= 1e-12 && fabs(vy) <= 1e-12) return 0;
    double tv = atan2(vy, vx), ux = ccw ? vy : -vy, uy = ccw ? -vx : vx;   /* line direction (dStack_e8 / e0) */
    double l0x = c1x + vx * 2, l0y = c1y + vy * 2;
    if (R5 - r < 0) return 0;
    /* FUN_1800a9000: the line l0 + t u against the circle (c, R5 - r); t0 < t1 */
    double uu = ux * ux + uy * uy, tf = ((cx - l0x) * ux + (cy - l0y) * uy) / uu;
    double fx = l0x + ux * tf - cx, fy = l0y + uy * tf - cy, d2 = fx * fx + fy * fy, rr = R5 - r, t;
    if (d2 > rr * rr) { if (d2 >= (rr + 1e-6) * (rr + 1e-6)) return 0; t = tf; }
    else { double h = sqrt(rr * rr - d2) / sqrt(uu); t = tf + h; }   /* two: the larger parameter */
    double c2x = l0x + ux * t, c2y = l0y + uy * t;
    if (t < 0 && hypot(c2x - l0x, c2y - l0y) >= 1e-6) return 0;
    if (fabs(c2x - cx) <= 1e-12 && fabs(c2y - cy) <= 1e-12) return 0;
    double t3 = atan2(c2y - cy, c2x - cx), t2 = ang_pm_pi(tv + M_PI), s2 = ang_pm_pi(t3 - t2);
    if (ccw ? s2 < 0 : s2 > 0) { if (fabs(s2 * r) >= 1e-6) s2 += s2 >= 0 ? -2 * M_PI : 2 * M_PI; }
    double bx = c2x + r * cos(t2), by = c2y + r * sin(t2);
    MRec q[3] = { { 'A', s1 < 0, 0, ax, ay, c1x, c1y, z, feed }, { 'L', 0, 0, bx, by, 0, 0, z, feed },
                  { 'A', s2 < 0, 0, c2x + r * cos(t2 + s2), c2y + r * sin(t2 + s2), c2x, c2y, z, feed } };
    double len[3] = { fabs(s1 * r), hypot(bx - ax, by - ay), fabs(s2 * r) };
    int n = 0;
    for (int i = 0; i < 3; i++) if (len[i] >= 1e-6) pc[n++] = q[i];
    if (!n) pc[n++] = q[1];
    return n;
}
typedef struct { int trans; double ang; } CmState;   /* DAT_1804f125c (a transition led into this circle), DAT_1804f1278 */
/* §3.3 FUN_1800409f0(c, R, z, cw, list, .., turn limit param_7 = 50 (dis 0x18004028a), .., out point param_9,
   .., next spacing param_12, first param_13, last param_14) */
static int cm_circle(CamOp *op, double cx, double cy, double R, double z, int first, int last, double next, CmState *st,
                     MList *ml, double *ox, double *oy) {
    CamParams *p = &op->p;
    double D = p->tool_dia;
    if (R < 1e-12 || R <= D * 0.5) { snprintf(op->error, sizeof op->error, "Circle Mill: tool D%.4g does not fit circle radius %.4g (Mastercam warning 0xa56)", D, R); return 0; }
    int lead = p->mc_lead_on, cw = p->mc_comp_right == 1; double s = cw ? -1 : 1;   /* leads only with 0x8f0 */
    double sweep = lead ? mcd(p, 0x1248) : 0, ovl = lead ? mcd(p, 0x1250) : 0;
    int ctr = lead && mcb(p, 0x1260), perp = lead && mcb(p, 0x1261);
    double a = st->trans ? st->ang : mcd(p, 0x1240) * 0.01745329251994329, ovA = ovl / R, ae = a + s * ovA;
    int turns = (int)floor(fabs(ovA) / (2 * M_PI)); if (turns > 50) turns = 50;
    double Rp, rl;
    if (cc_in_computer(p->mc_comp)) { Rp = R - D * 0.5; rl = Rp * 0.5; } else { Rp = R; rl = D * 0.25 + R * 0.5; }
    double q1x, q1y, q2x, q2y, sx, sy, ex, ey, pix, piy, pox, poy, F = p->feed, sw = s * sweep * 0.01745329251994329;
    PT(cx, cy, Rp - rl, a, q1x, q1y); PT(cx, cy, Rp - rl, ae, q2x, q2y);
    PT(cx, cy, Rp, a, sx, sy); PT(cx, cy, Rp, ae, ex, ey);
    PT(q1x, q1y, rl, a - sw, pix, piy); PT(q2x, q2y, rl, ae + sw, pox, poy);
    if (first || !st->trans) {   /* entry */
        if (ctr) ml_line(ml, cx, cy, z, F);
        if (perp) ml_line(ml, q1x, q1y, z, F);
        if (!ml->n || hypot(pix - ml->r[ml->n - 1].x, piy - ml->r[ml->n - 1].y) > MC_NC_TOL) ml_line(ml, pix, piy, z, F);
        if (hypot(sx - pix, sy - piy) > MC_NC_TOL) ml_add(ml, 'A', sx, sy, q1x, q1y, cw, 0, z, F);
    }
    for (int k = 0; k <= turns; k++) ml_add(ml, 'A', sx, sy, cx, cy, cw, 1, z, F);   /* circle + extra turns */
    *ox = sx; *oy = sy;
    if (hypot(sx - ex, sy - ey) > MC_NC_TOL) { ml_add(ml, 'A', ex, ey, cx, cy, cw, 0, z, F); *ox = ex; *oy = ey; }   /* overlap */
    int tr = st->trans;
    if (!last && mcb(p, 0x1338)) {   /* transition to the next circle (P+0x110), mcce MCCECalcArcLnArcBetCir */
        MRec pc[3]; int n = mcce_arc_ln_arc(cx, cy, *ox, *oy, !cw, mcd(p, 0x1330), Rp + next, next * 0.25, pc, z, F);
        for (int i = 0; i < n; i++) ml_add(ml, pc[i].k, pc[i].x, pc[i].y, pc[i].cx, pc[i].cy, pc[i].cw, 0, z, F);
        if (n) { st->trans = tr = 1; double an = atan2(ml->r[ml->n - 1].y - cy, ml->r[ml->n - 1].x - cx); st->ang = an < 0 ? an + 2 * M_PI : an; }
    }
    if (lead && (last || !tr)) {   /* exit */
        if (hypot(pox - ex, poy - ey) > MC_NC_TOL) ml_add(ml, 'A', pox, poy, q2x, q2y, cw, 0, z, F);
        if (perp) ml_line(ml, q2x, q2y, z, F);
        if (ctr && hypot(cx - ml->r[ml->n - 1].x, cy - ml->r[ml->n - 1].y) > MC_NC_TOL) ml_line(ml, cx, cy, z, F);
    }
    return 1;
}

/* §3.4 FUN_180041530(c, Rr, T, z, plunge param_5 = 1, target param_6 = circle 0's out point, .., next radius
   param_11 = (R - off(0)) - D/2) (bnci_circmill call dis 0x18004031a..0x18004036d; params at rbp+0x290.. of
   FUN_180041530, dis 0x180041596..0x1800415ea). Arc z / direction / full arguments: dis 0x18004190d, 0x180041bcc,
   0x180041c55, 0x180041e36 (z, cw flag 0x5d2). Returns 1, 0 = the entry failed and the point is skipped (0x12a9). */
static int cm_rough(const CamParams *p, double cx, double cy, double Rr, double T, double z, double tgx, double tgy, double nextR, MList *ml) {
    double D = p->tool_dia, st = mcd(p, 0x1268) * 0.01 * D;   /* P+0x48 = P+0x40 % x D (bnci_circmill) */
    double minR = mcd(p, 0x12d0) * 0.01 * D, maxR = mcd(p, 0x12d8) * 0.01 * D;   /* P+0x58 / P+0x60 */
    int cw = p->mc_comp_right == 1; double dir = cw ? -1 : 1, F = p->feed, X, Y;
    double rh = 0, ang = 0, H = 0, tn = 0, Th = 0; int helical = 0, m = 0;
    if (mcb(p, 0x1278)) {
        double d2 = Rr - mcd(p, 0x12a0);
        if (maxR <= d2 || minR <= d2) {
            rh = maxR <= d2 ? maxR : d2; helical = 1;
            H = (T + mcd(p, 0x1290)) - z; tn = tan(mcd(p, 0x1298) * 0.01745329251994329);
            Th = tn > 0 && rh > 0 ? H / (rh * tn) : 0;
            if (!mcb(p, 0x12b9)) { double stp = sqrt(mcd(p, 0x12c0) * 8 / rh); m = 2; if (stp >= 1e-12) { m = (int)(fabs(Th) / stp) + 1; if (m < 2) m = 2; } }
            else m = (int)floor(fabs(Th) / (2 * M_PI) + 0.5) + 1;
        } else if (mcb(p, 0x12a9)) return 0;   /* entry failed: skip the point */
    }
    if (helical) {
        double zs = z + H, zz = zs;
        if (!mcb(p, 0x12b9)) {
            for (int i = 0; i < m; i++) { double t = (double)i / (m - 1); zz = i == m - 1 ? z : (1 - t) * zs + t * z;
                ang = t * Th * dir; PT(cx, cy, rh, ang, X, Y); ml_line(ml, X, Y, zz, F); }
        } else {
            double rem = Th;
            while (rem > 0) {
                int full = 0;
                if (ml->n > 0) {
                    if (rem <= 2 * M_PI) {
                        if (rem <= M_PI) { ang += rem * dir; rem = 0; zz = z; }
                        else { rem -= M_PI; ang += dir * M_PI; zz -= rh * tn * M_PI; }
                    } else { rem -= 2 * M_PI; zz -= rh * tn * 2 * M_PI; full = 1; }
                    while (ang > 2 * M_PI) ang -= 2 * M_PI; while (ang < -2 * M_PI) ang += 2 * M_PI;
                }
                PT(cx, cy, rh, ang, X, Y);
                if (!ml->n) ml_line(ml, X, Y, zz, F); else ml_add(ml, 'A', X, Y, cx, cy, cw, full, zz, F);
            }
        }
        PT(cx, cy, rh, ang, X, Y); ml_add(ml, 'A', X, Y, cx, cy, cw, 1, z, F);   /* a whole circle at z */
    } else { rh = 0; ang = 0; ml_line(ml, cx, cy, z, F); X = cx; Y = cy; }      /* to the centre at z */
    double ex = X, ey = Y;
    if (rh + MC_NC_TOL < Rr) {   /* spiral out: half circles growing st/4, then st/2, until one meets the Rr circle */
        int cap = ((int)floor(fabs((Rr - rh) / st) + 0.5) + 5) * 2 + m;
        double ccx = cx, ccy = cy, r0 = rh, g = st * 0.25;
        for (;;) {
            double r1 = r0 + g, ncx = ccx - g * cos(ang), ncy = ccy - g * sin(ang);
            /* ints_on_gt(bound circle (c, Rr), this half circle): the first crossing along it, else its end */
            int hit = 0; double hx = 0, hy = 0;
            {   double vx = ncx - cx, vy = ncy - cy, dd = hypot(vx, vy);
                if (dd > 1e-12 && dd <= Rr + r1 + MC_NC_TOL && dd >= fabs(Rr - r1) - MC_NC_TOL) {   /* within nc_tol: touching counts */
                    double aa = (Rr * Rr - r1 * r1 + dd * dd) / (2 * dd), hh = sqrt(fmax(0, Rr * Rr - aa * aa));
                    double mx = cx + vx / dd * aa, my = cy + vy / dd * aa, best = 1e9;
                    for (int sgn = -1; sgn <= 1; sgn += 2) {
                        double px = mx + sgn * hh * -vy / dd, py = my + sgn * hh * vx / dd;
                        double t = (atan2(py - ncy, px - ncx) - ang) * dir; while (t < 0) t += 2 * M_PI; while (t >= 2 * M_PI) t -= 2 * M_PI;
                        if (t <= M_PI + 1e-7 && t > 1e-12 && t < best) { best = t; hx = px; hy = py; hit = 1; }
                    } } }
            double qx = hit ? hx : ncx + r1 * cos(ang + dir * M_PI), qy = hit ? hy : ncy + r1 * sin(ang + dir * M_PI);
            ex = qx; ey = qy;
            if (hypot(qx - cx, qy - cy) > Rr + MC_NC_TOL || ml->n >= cap) break;
            ml_add(ml, 'A', qx, qy, ncx, ncy, cw, 0, z, F);
            ccx = ncx; ccy = ncy; g = st * 0.5; ang += M_PI; while (ang > 2 * M_PI) ang -= 2 * M_PI;
            if (hit) break;
            r0 = r1;
        }
        ml_add(ml, 'A', cx - (ex - cx), cy - (ey - cy), cx, cy, cw, 0, z, F);   /* closing pair about c */
        ml_add(ml, 'A', ex, ey, cx, cy, cw, 0, z, F);
    }
    if (!p->mc_lead_on) {
        if (mcb(p, 0x1338)) {
            MRec pc[3]; int n = mcce_arc_ln_arc(cx, cy, ml->r[ml->n - 1].x, ml->r[ml->n - 1].y, !cw, mcd(p, 0x1330), nextR, (nextR - Rr) * 0.25, pc, z, F);
            for (int i = 0; i < n; i++) ml_add(ml, pc[i].k, pc[i].x, pc[i].y, pc[i].cx, pc[i].cy, pc[i].cw, 0, z, F);
            ex = ml->r[ml->n - 1].x; ey = ml->r[ml->n - 1].y;
        }
    } else {   /* exit: about M = (end + c) / 2, radius Rr / 2, to (end angle + dir sweep) (dis 0x180041d9d..0x180041e56) */
        double lx = ml->r[ml->n - 1].x, ly = ml->r[ml->n - 1].y, te = atan2(ly - cy, lx - cx); if (te < 0) te += 2 * M_PI;
        double mx = (ex + cx) * 0.5, my = (ey + cy) * 0.5, qx, qy;
        PT(mx, my, Rr * 0.5, te + dir * 0.01745329251994329 * mcd(p, 0x1248), qx, qy);
        ml_add(ml, 'A', qx, qy, mx, my, cw, 0, z, F);
    }
    /* FUN_180041340: rotate everything about c so that the end point's direction becomes the target's */
    double ax = ex - cx, ay = ey - cy, bx = tgx - cx, by = tgy - cy, la = hypot(ax, ay), lb = hypot(bx, by);
    if (la == 0) ax = ay = 0; else { ax /= la; ay /= la; }
    if (lb == 0) bx = by = 0; else { bx /= lb; by /= lb; }
    double sn = ax * by - ay * bx, cs = ax * bx + ay * by;
    for (int i = 0; i < ml->n; i++) {
        MRec *r = &ml->r[i]; double x = r->x - cx, y = r->y - cy;
        r->x = cx + x * cs - y * sn; r->y = cy + x * sn + y * cs;
        if (r->k == 'A') { x = r->cx - cx; y = r->cy - cy; r->cx = cx + x * cs - y * sn; r->cy = cy + x * sn + y * cs; }
    }
    return 1;
}

static int gen_circlemill(CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (!mc_common_ok(op, "Circle Mill")) return 0;
    if (!op->nholes) { snprintf(op->error, sizeof op->error, "Circle Mill: select circles or points"); return 0; }
    double D = p->tool_dia, zc = mcb(p, 0x1238) ? 0 : p->corner_r;   /* 0x1238 = 0: z raised by the corner radius */
    /* §3.2.3 XY passes: single pass unless multipasses (0x8a8) with rough (0x12e9) / finish (0x12ea) counts;
       spacing to the next circle (param_12): finish spacing when that pass is a finish pass, else rough (dis
       0x18004021b..0x18004022c, xmm8 = op +0x848, xmm9 = op +0x858) */
    MPass xy = { 1, 0, 0, 0, p->stock_xy }; double rsp = 0;
    if (p->mc_mp_on && ((mcb(p, 0x12e9) && p->mc_mp_nr >= 1) || (mcb(p, 0x12ea) && p->mc_mp_nf >= 1))) {
        xy.nr = mcb(p, 0x12e9) ? p->mc_mp_nr : 0; xy.rough = mcb(p, 0x12e9) ? p->mc_mp_rs : 0;
        xy.nf = mcb(p, 0x12ea) ? p->mc_mp_nf : 0; xy.fs = mcb(p, 0x12ea) ? p->mc_mp_fs : 0;
        rsp = xy.nr > 0 ? xy.rough : xy.fs;
    }
    MPass dp = mc_depths(p, p->stepdown > 0, p->stepdown, p->mc_depth_nf, p->mc_depth_fs);
    int nd = dp.nr + dp.nf;
    /* §3.2.4 warning 0x93b: several depth passes, finish passes only, no finish at all depths (0x12e8), no roughing */
    if (nd > 1 && !mcb(p, 0x12e8) && !mcb(p, 0x1262) && xy.nr == 0 && xy.nf > 0) { xy.nr = 1; xy.rough = 0; xy.nf -= 1; }
#define NEXTSP(j) ((xy.nr + xy.nf - 1 - (j) - xy.nf) < 0 ? xy.fs : xy.rough)   /* FUN_1800412d0's flag for pass j */
    MW w = { 0 }; int done = 0;
    for (int h = 0; h < op->nholes; h++) {
        int n0 = b->n;
        double cx = op->holes[h].x, cy = op->holes[h].y;
        double R = op->holes[h].d > MC_NC_TOL ? op->holes[h].d / 2 : mcd(p, 0x1258) / 2;   /* FUN_180040830 */
        double T = p->top_z;   /* the roughing helix's top: top of stock, then the previous pass's z (bnci_circmill dVar2) */
        for (int i = 0; i < nd; i++) {
            double z = p->final_z + mpass_off(&dp, i) + zc;
            int nxy = !p->mc_mp_on || mcb(p, 0x12e8) || i == nd - 1 ? xy.nr + xy.nf : xy.nr;
            CmState cs = { 0, 0 };
            MList c0 = { 0 }; double ox, oy, R0 = R - mpass_off(&xy, 0);
            /* a failing circle 0 (0xa56) ends this point (*param_2 = 0, break); one of the later circles ends the op */
            if (!cm_circle(op, cx, cy, R0, z, 1, nxy < 2, NEXTSP(1), &cs, &c0, &ox, &oy)) { free(c0.r); break; }
            int skip = 0;
            if (mcb(p, 0x1262)) {   /* §3.4 roughing spiral */
                double r = R0 - D * 0.5 - rsp;
                if (r > 1e-12) {
                    MList rl = { 0 };
                    if (!cm_rough(p, cx, cy, r, T, z, ox, oy, R0 - D * 0.5, &rl)) skip = 1;
                    else mw_out(b, p, &w, &rl, 0, 0, 0);
                    free(rl.r);
                }
            }
            if (skip) { free(c0.r); break; }
            for (int j = 0; j < nxy; j++) {
                if (j == 0) { mw_out(b, p, &w, &c0, 0, 0, 0); continue; }
                MList cj = { 0 }; double jx, jy;
                if (!cm_circle(op, cx, cy, R - mpass_off(&xy, j), z, 0, j == nxy - 1, NEXTSP(j + 1), &cs, &cj, &jx, &jy)) { free(cj.r); free(c0.r); return 0; }
                mw_out(b, p, &w, &cj, 0, 0, 0); free(cj.r);
            }
            free(c0.r);
            if (mcb(p, 0x1262) || nxy >= 1) T = z;
            if (!p->mc_keep_down) mw_retract(b, p, &w);   /* retract_move unless keep tool down (0x891) */
        }
        mw_retract(b, p, &w);                              /* the move to the next point: from the feed plane (see header) */
        if (b->n > n0) done++;
        if (p->mc_clr_on && (!p->mc_clr_end || h == op->nholes - 1)) mw_clear(b, p, &w);   /* §3.2.6 */
    }
#undef NEXTSP
    if (!done || !b->n) { if (!op->error[0]) snprintf(op->error, sizeof op->error, "Circle Mill: nothing to cut"); return 0; }
    mw_clear(b, p, &w);
    return 1;
}
#undef PT

static int gen_helix(CamOp *op, MB *b) { return op->p.type == CAM_CIRCLE ? gen_circlemill(op, b) : gen_helixbore(op, b); }

/* ------------------------------------------------------------------------------------------------ thread mill */
/* Thread Mill (opcode 100): mcmill.dll's thread-mill generator ported function by function (formats/mcmill_2d_engines.md §4,
   decompiles decomp/mcmill_all.c; the arguments the decompile lost are read from the disassembly, addresses below):
     bnci_threadmill 0x180234c80 (l.385061)  -> gen_thread: the points in order, each point's diameter, first / last point
     FUN_1802374d0   0x1802374d0 (l.386306)  -> tm_point: radial passes (rough / finish / spring), the finish feed override,
                                                the heights, the active-teeth Z passes, the cw / side bytes
     FUN_180236810   0x180236810 (l.385946)  -> tm_pass: approach, entry lead points, entry arc, helix, exit arc, exit lead
                                                points, retract; the cutter compensation codes
     FUN_180235a80   0x180235a80 (l.385542)  -> tm_arc: the entry / exit arc (quarter turn, inset 0x1270, quarter pitch rise)
     FUN_180235e90   0x180235e90 (l.385662)  -> tm_lead: a lead point
     FUN_1802360c0   0x1802360c0 (l.385749)  -> tm_rt: the radius at a Z (taper angle 0x12a8)
     FUN_180236170   0x180236170 (l.385772)  -> tm_helix: helix pieces (arcs of at most a turn / the tapered step, or lines)
     FUN_1802355d0   0x1802355d0 (l.385403)  -> tm_step: the tapered helix's arc step (chord error < tolerance 0x1288)
     FUN_180235120   0x180235120 (l.385274)  -> tm_arc3: a tapered helix piece = the arc through three points of it
     FUN_1802350f0   0x1802350f0 (l.385262)  -> tm_q: a quarter pitch when 0x1281
     FUN_180234b80   0x180234b80 (l.385021)  -> tm_vang: the angle between two vectors
     mccore center_3pa 0x1804b5200 (decomp/mccore_filter.c l.4717) -> tm_center3; mccore proj_pt_ar 0x1804b41b0 (dis only:
     the point moved onto the circle along the ray from its centre, nothing when nearer than 5e-5 to it) -> tm_step
   Record offsets (op + 0x68): 0x1228 S16 active teeth, 0x1230 clearance, 0x1238 feed plane, 0x1240 / 0x1248 top / bottom
   (S16 0x1250 = 1: from the point's Z), 0x1258 pitch, 0x1260 start angle (deg), 0x1268 allowance on the diameter, 0x1270
   entry / exit arc inset, 0x1278 lead length, 0x1280 / 0x1281 helical top / bottom arcs, 0x1282 linearise (tolerance 0x1288,
   also the tapered arcs' tolerance), S16 0x1290 0 ID / 1 OD, 0x1298 diameter for points, 0x12a0 left hand, 0x12a1 ID lead
   not from the centre, 0x12a2 bottom up, 0x12a3 perpendicular lead, 0x12a8 taper angle (deg), multipasses 0x8a8 / 0x8aa /
   0x8b0 / 0x8b8 / 0x8c0, S16 0x12b0 spring passes, 0x12c4 / 0x12b8 finish feed, 0x12c5 / S32 0x12c0 finish spindle speed;
   compensation 0x5d0, clearance only at start / end 0x319, rapid retract 0x1a9, feeds 0x378 / 0x380 / 0x388. Heights,
   pitch, hand, direction and ID / OD are CamParams (cam_fileop.c params_from_record maps them from these fields).
   Refused (with a message): the finish spindle override 0x12c5 (Mastercam writes an NCI 4 spindle record, bnci_wr_4, which
   this program's moves cannot carry); compensation Control without MCAM_CUTTER_COMP=1 (as every engine here). The NCI
   start / end flags (gr_lin_mill / gr_arc_mill param flags 1 / 2) and wr_sof are not moves and not written. */
typedef struct { double cx, cy, z, px, py, r, a, sw; } TmArc;   /* FUN_180235a80 output +0x00..+0x38 */
typedef struct {            /* FUN_1802374d0's block local_158 (passed as FUN_180236810 param_1) */
    double ox, oy, r;       /* +0x00 / +0x08 the point, +0x18 the helix radius */
    double zs, ze;          /* +0x20 / +0x28 helix bottom / top */
    double ztop, zbot;      /* +0x30 max(top, bottom) / +0x38 bottom, both + the base Z: the taper's references */
    int cw, hi;             /* +0x40 direction, +0x41 the side byte (cutter compensation) */
    int first, last;        /* +0x42 / +0x43 the point's first / last pass */
    int fpt, lpt;           /* +0x44 / +0x45 the op's first / last point */
} TmS;
static double tm_ang(double y, double x) { double a = (x * x + y * y >= 1e-12) ? atan2(y, x) : 0; return a < 0 ? a + 2 * M_PI : a; }
/* FUN_180234b80: the angle between two vectors (0 for a vector shorter than 1e-12 or |cos| > 1) */
static double tm_vang(double ax, double ay, double bx, double by) {
    double la = sqrt(ax * ax + ay * ay); if (la < 1e-12) return 0;
    double lb = sqrt(bx * bx + by * by); if (lb < 1e-12) return 0;
    double c = (bx * ax + by * ay) / (lb * la); return fabs(c) > 1.0 ? 0 : acos(c);
}
/* mccore center_3pa: the circle through three points - the bisectors of P1 P2 and P2 P3 intersected (l_ints); 0 when two
   points are closer than the system tolerance or the bisectors are parallel */
static int tm_center3(double x1, double y1, double x2, double y2, double x3, double y3, double *cx, double *cy) {
    if (hypot(x2 - x1, y2 - y1) < MC_TOL || hypot(x3 - x2, y3 - y2) < MC_TOL || hypot(x1 - x3, y1 - y3) < MC_TOL) return 0;
    double ax = (x1 + x2) * 0.5, ay = (y1 + y2) * 0.5, adx = -(y2 - y1), ady = x2 - x1;   /* local_e8 -> local_d8 */
    double bx = (x2 + x3) * 0.5, by = (y2 + y3) * 0.5, bdx = -(y3 - y2), bdy = x3 - x2;   /* local_108 -> local_f8 */
    double det = adx * bdy - ady * bdx; if (fabs(det) < 1e-15) return 0;
    double s = ((bx - ax) * bdy - (by - ay) * bdx) / det;
    *cx = ax + s * adx; *cy = ay + s * ady; return 1;
}
/* FUN_1802360c0: the radius r at Z z of a tapered thread (|0x12a8| >= 1e-6): ID r - tan(t) x (ztop - z), OD r - tan(t) x
   (z - zbot); the arguments (r, z, ztop = S +0x30, zbot = S +0x38) from dis 0x18023697b..0x18023698b */
static double tm_rt(const CamParams *p, double r, double z, double ztop, double zbot) {
    double t = mcd(p, 0x12a8);
    if (fabs(t) >= 1e-6) r -= tan(t * 0.01745329251994329) * (p->thread_internal ? ztop - z : z - zbot);
    return r;
}
static double tm_q(const CamParams *p) { return mcb(p, 0x1281) ? p->thread_pitch * 1.570796326794896 / 6.283185307179586 : 0; }   /* FUN_1802350f0 */
/* FUN_180235a80 (args dis 0x1802369b0..0x180236a0b / 0x180236fd3..0x180237016): the entry (entry = 1) or exit arc that meets the
   helix circle (radius r about o, at Z z) tangentially at angle a, a quarter turn (0x18030db98 = pi / 2) before / after it; both
   radii through tm_rt at z */
static void tm_arc(const CamParams *p, int entry, double ox, double oy, double z, double r, double a, int cw, int up, double zt, double zb, TmArc *o) {
    double d = M_PI / 2; if (cw) d = -d; if (entry) d = -d;
    double ang = a + d, re = p->thread_internal ? r - mcd(p, 0x1270) : r + mcd(p, 0x1270);
    if (p->thread_internal && re <= 0) re = 0;
    re = tm_rt(p, re, z, zt, zb); double rr = tm_rt(p, r, z, zt, zb);
    double p1x = rr * cos(a), p1y = rr * sin(a), p0x = re * cos(ang), p0y = re * sin(ang);
    /* l_ints: the perpendicular bisector of P0 P1 and the line from the centre through P1 */
    double mx = (p0x + p1x) / 2, my = (p0y + p1y) / 2, nx = -(p1y - p0y), ny = p1x - p0x;
    double det = p1x * ny - p1y * nx, cx = 0, cy = 0;
    if (fabs(det) > 1e-15) { double s = (mx * ny - my * nx) / det; cx = s * p1x; cy = s * p1y; }
    int flag = entry ? (up ? mcb(p, 0x1281) : mcb(p, 0x1280)) : (up ? mcb(p, 0x1280) : mcb(p, 0x1281));
    double dz = flag ? fabs(d * p->thread_pitch / (2 * M_PI)) : 0;
    if (!entry) dz = -dz;
    if (up) dz = -dz;
    o->z = z + dz;
    o->a = tm_ang(p0y - cy, p0x - cx); o->r = hypot(p0x - cx, p0y - cy);
    double c = o->r > 0 ? ((p1x - cx) * (p0x - cx) + (p1y - cy) * (p0y - cy)) / (o->r * o->r) : 1;
    o->sw = fabs(c) >= 0.999999999999 ? M_PI : acos(c);
    o->cx = cx + ox; o->cy = cy + oy; o->px = p0x + ox; o->py = p0y + oy;
}
/* FUN_180235e90: the lead point before the entry arc / after the exit arc */
static void tm_lead(const CamParams *p, int entry, int cw, double ox, double oy, const TmArc *A, double *x, double *y) {
    double L0 = mcd(p, 0x1278);
    if (p->thread_internal) {
        if (!mcb(p, 0x12a1)) { *x = A->cx; *y = A->cy; return; }
        double bx = mcb(p, 0x12a3) ? A->cx : ox, by = mcb(p, 0x12a3) ? A->cy : oy;
        double vx = bx - A->px, vy = by - A->py, L = hypot(vx, vy), ux = L > 0 ? vx / L : 0, uy = L > 0 ? vy / L : 0, d = fmin(L, L0);
        *x = A->px + ux * d; *y = A->py + uy * d;
    } else {
        double vx = A->px - A->cx, vy = A->py - A->cy, L = hypot(vx, vy), ux = L > 0 ? vx / L : 0, uy = L > 0 ? vy / L : 0, d = L0, dx = ux, dy = uy;
        if (!mcb(p, 0x12a3)) { dx = -uy; dy = ux; if (cw) d = -d; if (entry) d = -d; }
        *x = A->px + dx * d; *y = A->py + dy * d;
    }
}
/* FUN_1802355d0 (args dis 0x1802364c6..0x1802364ee: r, taper (rad), pitch, tolerance 0x1288): the angular step of a tapered
   helix's arcs - from pi, halved (at most 8 times) until the arc through the start, middle and end of a step deviates from
   the helix by less than the tolerance at 13 points (3D, the arc's Z linear in its angle) */
static double tm_step(double r, double t, double pitch, double tol) {
    double step = M_PI, k = tan(t);
    for (int n = 0;;) {
        double f = step / 6.283185307179586, re = r - k * f * pitch, rm = r - f * 0.5 * pitch * k, cx = 0, cy = 0;
        double ex = cos(step) * re, ey = sin(step) * re;
        tm_center3(r, 0, cos(step * 0.5) * rm, sin(step * 0.5) * rm, ex, ey, &cx, &cy);
        double v0x = r - cx, v0y = -cy, A = tm_vang(v0x, v0y, ex - cx, ey - cy), R = hypot(ex - cx, ey - cy), mx = 0, qx = 0, qy = 0;
        for (int i = 0; i < 13; i++) {
            double ai = i / 12.0 * step, zi = i / 12.0 * pitch * f, ri = r - k * zi, x = cos(ai) * ri, y = sin(ai) * ri;
            double d = hypot(x - cx, y - cy);
            if (d >= CAM_SYS_TOL) { qx = cx + (x - cx) * R / d; qy = cy + (y - cy) * R / d; }   /* proj_pt_ar (keeps the last point when it fails; its limit = [0x1812b5b58] = tol, config 1000) */
            double ap = tm_vang(qx - cx, qy - cy, v0x, v0y), dz = A > 0 ? ap * f * pitch / A - zi : -zi;
            double e = sqrt((qy - y) * (qy - y) + (qx - x) * (qx - x) + dz * dz);
            if (mx <= e) mx = e;
        }
        if (mx < tol) break;
        n++; step *= 0.5;
        if (n > 7) break;
    }
    return step;
}
/* FUN_180235120 (args dis 0x1802365f5..0x180236635: centre, previous / new angle, previous / new Z, r, ztop, zbot): the arc
   through the helix points at the previous angle, the middle angle and the new angle (radii tm_rt at their Z); its centre and
   end */
static void tm_arc3(const CamParams *p, double ox, double oy, double a0, double a1, double z0, double z1, double r, double zt, double zb,
                    double *cx, double *cy, double *ex, double *ey) {
    double r0 = tm_rt(p, r, z0, zt, zb), rm = tm_rt(p, r, (z0 + z1) * 0.5, zt, zb), r1 = tm_rt(p, r, z1, zt, zb), am = (a0 + a1) * 0.5;
    *ex = cos(a1) * r1 + ox; *ey = sin(a1) * r1 + oy;
    tm_center3(cos(a0) * r0 + ox, sin(a0) * r0 + oy, cos(am) * rm + ox, sin(am) * rm + oy, *ex, *ey, cx, cy);
}
/* an arc from the current point to (ex, ey) about (cx, cy) in direction cw, Z to z1 (gr_circle_mill / gr_arc_mill with an end
   and a centre); the cutter compensation code and the contour flags fl on its record */
static void tm_arc_to(MB *b, double cx, double cy, double ex, double ey, int cw, double z1, double feed, int cc, int fl) {
    V3 s = lastp(b); double as = tm_ang(s.y - cy, s.x - cx), ae = tm_ang(ey - cy, ex - cx), sw = cw ? as - ae : ae - as;
    while (sw <= 1e-12) sw += 2 * M_PI;
    while (sw > 2 * M_PI) sw -= 2 * M_PI;
    int from = b->n; arc_moves(b, cx, cy, hypot(ex - cx, ey - cy), as, cw ? -sw : sw, s.z, z1, feed);
    if (cc && from < b->n) b->m[from].cc = cc;
    if (from < b->n) b->m[from].ctf = fl;
}
/* FUN_180236170 (args per call: entry dis 0x180236ec2..0x180236f28 - taper 0, the arc's radius / start angle / sweep; helix
   0x180236f37..0x180236fcc - taper = |0x12a8| > 1e-6, S +0x18 radius, start angle 0x1260, sweep |zA - zB| / pitch x 2 pi;
   exit 0x1802371b7..0x18023720f - taper 0, the arc's radius and sweep, the helix's end angle): around (cx, cy) from *ang, total
   sweep sw in direction cw, Z from z0 to z1 linear in the angle. Arcs (0x1282 off): pieces of a turn (tapered: tm_step; the
   last two halves when between 1 and 1.01 steps), a tapered piece the tm_arc3 arc; every piece carries the code cc (stop_cc
   after a 140). Lines (0x1282 on): (short) floor(|sw| / sqrt(8 tol / r)) + 1 equal steps on the radius tm_rt (tapered), the
   code 41 / 42 on the first, 140 on the last. Contour flags fl (param_14): 1 = the first piece's record gets 1, 2 = the last's 2 */
static void tm_helix(MB *b, const CamParams *p, double cx, double cy, double z0, double z1, int taper, double zt, double zb, double r,
                     double *ang, double sw, int cw, double feed, int cc, int fl) {
    if (!mcb(p, 0x1282)) {
        double step = taper ? tm_step(r, mcd(p, 0x12a8) * 0.01745329251994329, p->thread_pitch, mcd(p, 0x1288)) : 2 * M_PI;
        if (!(sw >= 1e-12)) return;
        double lim = step * 1.01, rem = sw, zp = fabs(sw) * (z0 - z1) / sw + z1; int firstp = 1;
        do {
            double d = step;
            if (rem <= lim) { d = rem; if (step < rem) d = rem * 0.5; }
            double a0 = *ang;
            *ang = cw ? *ang - d : *ang + d;
            rem -= d;
            double zn = fabs(rem) * (z0 - z1) / sw + z1;
            int f = fl == 1 ? (firstp ? 1 : 0) : fl == 2 && !(rem >= 1e-12) ? 2 : 0;
            if (!taper) {
                int from = b->n; arc_moves(b, cx, cy, r, a0, cw ? -d : d, zp, zn, feed);
                if (cc && from < b->n) b->m[from].cc = cc;
                if (from < b->n) b->m[from].ctf = f;
            } else {
                double ccx = cx, ccy = cy, ex, ey; tm_arc3(p, cx, cy, a0, *ang, zp, zn, r, zt, zb, &ccx, &ccy, &ex, &ey);
                tm_arc_to(b, ccx, ccy, ex, ey, cw, zn, feed, cc, f);
            }
            zp = zn; firstp = 0;
        } while (rem >= 1e-12);
    } else {
        /* ours: a sweep of 0 (Mastercam: Z = 0 / 0, NaN) writes nothing */
        if (!(fabs(sw) >= 1e-12) || !(r > 0)) return;
        double x = floor(fabs(sw / sqrt(mcd(p, 0x1288) * 8.0 / r)));   /* cvttsd2si: out of range -> 0x80000000; the count is a short */
        int16_t n = (int16_t)(x < 2147483648.0 ? (int)x : INT32_MIN); n = (int16_t)(n + 1);
        double d = sw / n, rem = sw;
        for (int i = 0; i < n; i++) {
            *ang = cw ? *ang - d : *ang + d; rem -= d;
            double z = fabs(rem) * (z0 - z1) / sw + z1, rr = taper ? tm_rt(p, r, z, zt, zb) : r;
            mv(b, 0, cx + rr * cos(*ang), cy + rr * sin(*ang), z, feed);
            if (cc == 140 ? i == n - 1 : (cc == 41 || cc == 42) && i == 0) b->m[b->n - 1].cc = cc;
            b->m[b->n - 1].ctf = fl == 1 ? (i == 0) : fl == 2 && i == n - 1 ? 2 : 0;
        }
    }
}
/* one pass, FUN_180236810 */
static void tm_pass(MB *b, const CamParams *p, const TmS *S, double feed) {
    int bv2 = S->first || !p->thread_internal, bv3 = S->last || !p->thread_internal, up = p->thread_up != 0, cw = S->cw;
    int taper = fabs(mcd(p, 0x12a8)) > 1e-6;
    double zA = up ? S->zs : S->ze, zB = up ? S->ze : S->zs, a0 = mcd(p, 0x1260) * 0.01745329251994329, ox = S->ox, oy = S->oy;
    double clr = p->clearance, fpz = p->top_z + p->feed_plane;   /* dVar1 = 0x1230 (+ the origin Z), dVar17 = 0x1238 + the base Z */
    double r1 = tm_rt(p, S->r, zA, S->ztop, S->zbot), p1x = ox + r1 * cos(a0), p1y = oy + r1 * sin(a0);   /* local_158 */
    TmArc E; tm_arc(p, 1, ox, oy, zA, S->r, a0, cw, up, S->ztop, S->zbot, &E);
    double px[4], py[4]; int n = 0;
    if (p->thread_internal) {
        if (!mcb(p, 0x12a1) && hypot(E.px - ox, E.py - oy) > MC_TOL) { px[n] = ox; py[n++] = oy; }
        if (mcb(p, 0x12a3) || (mcb(p, 0x12a1) && mcd(p, 0x1278) > MC_NC_TOL)) { tm_lead(p, 1, cw, ox, oy, &E, &px[n], &py[n]); n++; }
    } else if (mcd(p, 0x1278) > MC_TOL) { tm_lead(p, 1, cw, ox, oy, &E, &px[n], &py[n]); n++; }
    px[n] = E.px; py[n++] = E.py;
    /* cutter compensation in control (cc_in_control 0x5d0, dis 0x180236b1e..0x180236b5c): 0x2a 'R' (G42) when the side byte is 0,
       0x29 'L' (G41) when 1, Reverse wear (3) the other way; 140 (0x8c) ends it. With MCAM_CUTTER_COMP=1 only (as slot_chain) */
    int cc = 0, stop = 0;
    if (cc_in_control(p->mc_comp) && nc_cutter_comp_on()) { cc = p->mc_comp == 3 ? (S->hi ? 42 : 41) : (S->hi ? 41 : 42); stop = 140; }
    if (bv2) {
        if (!p->mc_clr_end || S->fpt) mv(b, 1, px[0], py[0], clr, 0);
        if (E.z < fpz && fpz < clr) mv(b, 1, px[0], py[0], fpz, 0);
    }
    /* to the first point at the arc's Z: feed (plunge feed 0x380) when not above the feed plane, else rapid */
    { int f = E.z <= fpz; mv(b, !f, px[0], py[0], E.z, f ? p->plunge_feed : 0); }
    /* contour flags (sVar10 / uVar13): 1 on the first entry line when bVar2, else on the entry arc when there is no line */
    for (int k = 1; k < n; k++) { mv(b, 0, px[k], py[k], E.z, feed); if (k == n - 1) b->m[b->n - 1].cc = cc; b->m[b->n - 1].ctf = k == 1 && bv2; }
    int ecc = n < 2 ? cc : 0, efl = n < 2 ? bv2 : 0;   /* no lead lines: the code (and the flag) go on the entry arc */
    /* entry arc to the helix start: helical when its Z differs from the helix start's (tol), else flat */
    if (fabs(zA - E.z) >= MC_TOL) { double a = E.a; tm_helix(b, p, E.cx, E.cy, E.z, zA, 0, S->ztop, S->zbot, E.r, &a, E.sw, cw, feed, ecc, efl); }
    else tm_arc_to(b, E.cx, E.cy, p1x, p1y, cw, zA, feed, ecc, efl);
    /* the helix (sweep |zA - zB| / pitch x 2 pi, 0x18030a828) */
    double ang = a0;
    tm_helix(b, p, ox, oy, zA, zB, taper, S->ztop, S->zbot, S->r, &ang, fabs(zA - zB) / p->thread_pitch * 2 * M_PI, cw, feed, 0, 0);
    TmArc X; tm_arc(p, 0, ox, oy, zB, S->r, ang, cw, up, S->ztop, S->zbot, &X);
    double qx[4], qy[4]; int m = 0;
    if (p->thread_internal) {
        if (mcb(p, 0x12a3) || (mcb(p, 0x12a1) && mcd(p, 0x1278) > MC_NC_TOL)) { tm_lead(p, 0, cw, ox, oy, &X, &qx[m], &qy[m]); m++; }
        if (!mcb(p, 0x12a1) && hypot(X.px - ox, X.py - oy) > MC_NC_TOL) { qx[m] = ox; qy[m++] = oy; }
    } else if (mcd(p, 0x1278) > MC_TOL) { tm_lead(p, 0, cw, ox, oy, &X, &qx[m], &qy[m]); m++; }
    /* no lead lines: the stop code goes on the exit arc, with the flag 2 when bVar3 (dis LAB_180237114: -(bVar3) & 2); else the last
       exit line gets 2 when the pass is the point's last (+0x43, not bVar3) */
    int xcc = m ? 0 : stop, xfl = m ? 0 : bv3 ? 2 : 0;
    if (fabs(zB - X.z) >= MC_TOL) { double a = ang; tm_helix(b, p, X.cx, X.cy, zB, X.z, 0, S->ztop, S->zbot, X.r, &a, X.sw, cw, feed, xcc, xfl); }
    else tm_arc_to(b, X.cx, X.cy, X.px, X.py, cw, zB, feed, xcc, xfl);
    for (int k = 0; k < m; k++) { mv(b, 0, qx[k], qy[k], X.z, feed); if (k == 0) b->m[b->n - 1].cc = stop; b->m[b->n - 1].ctf = k == m - 1 && S->last ? 2 : 0; }
    if (bv3) {
        V3 e = lastp(b);
        if (e.z < fpz && fpz < clr) { if (p->mc_rapid_ret) mv(b, 1, e.x, e.y, fpz, 0); else mv(b, 0, e.x, e.y, fpz, p->mc_rrate); }
        e = lastp(b);
        if ((!p->mc_clr_end || S->lpt) && e.z < clr) mv(b, 1, e.x, e.y, clr, 0);
    }
}
/* FUN_1802374d0: one point (centre ox / oy, diameter dia) */
static void tm_point(MB *b, const CamParams *p, double ox, double oy, double dia, int fpt, int lpt) {
    int up = p->thread_up != 0, lh = p->thread_rh == 0;
    /* local_118: byte +0x40 cw = (0x12a2 ? 0x12a0 == 1 : 0x12a0 != 1), byte +0x41 its inverse; OD (0x1290 = 1) inverts +0x41 */
    int cw = up ? lh : !lh, hi = !cw; if (!p->thread_internal) hi = !hi;
    double comp = cc_in_computer(p->mc_comp) ? p->tool_dia : 0, pitch = p->thread_pitch;   /* local_168 = op +0x2e8 (rec 0x350) */
    int mp = p->mc_mp_on, rr = 1, fr = 0, total = 1;
    if (mp) { rr = p->mc_mp_nr; fr = p->mc_mp_nf; total = mcs(p, 0x12b0) + fr + rr; }
    for (int k = 1; k <= total; k++) {
        int s16 = rr;
        if (s16 < 1) { if (fr > 0) fr--; } else rr = s16 - 1;
        double feed = p->feed;
        if (s16 == 0 && mcb(p, 0x12c4)) feed = mcd(p, 0x12b8);   /* finish / spring passes: op +0x310 = +0x1250 */
        double T = p->top_z, B = p->final_z;                       /* + the base Z (point Z when 0x1250 = 1: cam_fileop.c) */
        TmS S; S.ztop = fmax(T, B); S.zbot = B;
        int teeth = mcs(p, 0x1228);
        if (teeth > 1) T -= (teeth - 1) * pitch;
        double q = tm_q(p);
        if (T <= pitch + q + B) T = pitch + q + B;
        double off = mp ? rr * p->mc_mp_rs + comp + fr * p->mc_mp_fs : comp;
        S.r = (p->thread_internal ? dia + mcd(p, 0x1268) - off : dia - mcd(p, 0x1268) + off) * 0.5;
        S.ox = ox; S.oy = oy; S.zs = q + B; S.ze = T; S.cw = cw; S.hi = hi; S.fpt = fpt; S.lpt = lpt;
        int nsub = 1; double z6 = S.ze, z7 = S.zs, Lt = 0;
        if (teeth >= 2) { Lt = teeth * pitch; double x = ((S.ze - S.zs) + Lt - 2 * pitch) / Lt; nsub = (int16_t)((int)(x >= 0 ? floor(x) : ceil(x)) + 1); }
        for (int j = 0; j < nsub; j++) {
            if (nsub >= 2) {
                if (up) { S.zs = j * Lt + z7; S.ze = pitch + S.zs; if (j == nsub - 1 && S.ze < z6) S.ze += pitch; }
                else { S.zs = (nsub - j - 1) * Lt + z7; S.ze = pitch + S.zs; if (j == 0 && S.ze < z6) S.ze += pitch; }
            }
            S.first = j == 0 && k == 1; S.last = j == nsub - 1 && k == total;
            tm_pass(b, p, &S, feed);
        }
    }
}
/* bnci_threadmill: the points in the op's order; a point's diameter is its arc's (or solid point data's) when picked at the
   centre (mode 'C', cam_fileop.c: Hole.d), else 0x1298; a point whose diameter is not above tol is skipped. first = the first
   point generated, last = the op's last point (even when skipped) */
static int gen_thread(CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (!p->mc_ok) { snprintf(op->error, sizeof op->error, "Thread Mill: no Mastercam operation record to take its parameters from (stored op or the Mill_*.mcam-defaults default op)"); return 0; }
    if (!(p->thread_pitch > 0)) { snprintf(op->error, sizeof op->error, "thread pitch is 0 (set it, or use a thread mill with a pitch)"); return 0; }
    if (cc_in_control_only(p->mc_comp) && !nc_cutter_comp_on()) { snprintf(op->error, sizeof op->error, "thread mill: compensation 'control' (G41/G42) is posted only with MCAM_CUTTER_COMP=1 (formats/post.md); use computer, wear or off"); return 0; }
    if (p->mc_mp_on) {   /* FUN_1802374d0: no passes -> post_warning 0x966 */
        int nr = p->mc_mp_nr, nf = p->mc_mp_nf, total = mcs(p, 0x12b0) + nf + nr;
        if ((int16_t)total == 0) { snprintf(op->error, sizeof op->error, "thread mill: no passes (multipasses with 0 rough, finish and spring passes)"); return 0; }
        if (mcb(p, 0x12c5) && total > (nr > 0 ? nr : 0)) { snprintf(op->error, sizeof op->error, "thread mill: Multi Passes Spindle Speed override (NCI 4 spindle change inside the op) is not written here; clear it"); return 0; }
    }
    int done = 0;
    for (int h = 0; h < op->nholes; h++) {
        double dia = op->holes[h].d > 0 ? op->holes[h].d : mcd(p, 0x1298);
        if (!(dia > MC_TOL)) continue;
        tm_point(b, p, op->holes[h].x, op->holes[h].y, dia, done == 0, h == op->nholes - 1);
        done++;
    }
    if (!done) { snprintf(op->error, sizeof op->error, "select circles or points (the hole diameter comes from the circle)"); return 0; }
    return 1;
}
