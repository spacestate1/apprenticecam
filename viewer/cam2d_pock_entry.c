/* cam2d_pock_entry.c - the pocket roughing entry, shared by every roughing engine: Pockentr.cpp _enter_pocket
 * (FUN_1801bc830, decomp/mcmill_all.c l.307784) with its helix (FUN_1801c1d80 l.311345 / find_helix_center
 * FUN_1801c04d0 l.310454 / FUN_1801c4a60 l.313246 / FUN_1801c5020 l.313408), "follow boundary" ramp (FUN_1801c14b0
 * l.311010) and ramp (FUN_1801c30d0 l.312161, its line FUN_1801c0ad0 l.310645 / FUN_1801c0e80 l.310797). Included by
 * cam2d_pock.c after pk_helix_center / pk_line_inside / pk_arc_moves. Decode notes: DECODE_entry.md (work tree).
 *
 * Interface (for any roughing engine): the entry of a cut that starts at S, in the rough boundary rb (the loops the cut
 * may not leave, tool-centre), planned once at the first depth z_first (FUN_1801c1f80: the entry height = top of stock -
 * depth + Z clearance; wr_pock_depth_cuts l.315072 copies the first depth's cut, entry included, shifted to the other
 * depths):
 *   PkEntry e; if (!pk_entry_plan(f, op, &rb, S, z_first, &e)) -> op->error set (NOT DECODED), refuse the op
 *   e.skip      -> the pocket is skipped (0x12d1 helix / 0x134a ramp "Skip" when the entry fails, l.307969-307983)
 *   per depth z: pk_entry_emit(b, p, &e, z, z_approach, ctol): rapid to the entry start at z_approach, to the feed
 *                plane, plunge feed to the entry top, the entry moves; then, if e.kind != PKE_PLUNGE, the caller feeds
 *                from the entry end to S (the link FUN_1801e23c0, l.307983; around islands NOT DECODED, see below)
 *   pk_entry_free(&e)
 *
 * Field map (pock_prm + X = record 0x1228 + X): +0x78 0x12a0 entry on, +0x7a 0x12a2 type (0 helix, 1 ramp), helix:
 * +0x80 / +0x88 min / max radius (= D % 0x12f8 / 0x1300, recompute_tool_dia_dependent_fields l.314889), +0x90 0x12b8
 * Z clearance, +0x98 0x12c0 plunge angle, +0xa0 0x12c8 XY clearance, +0xa8 0x12d0 CCW, +0xa9 0x12d1 skip, +0xaa 0x12d2
 * follow boundary, +0xab 0x12d3 on failure only, +0xb0 0x12d8 "if length exceeds", +0xb8 0x12e0 save skipped boundary,
 * +0xb9 0x12e1 output 3D arcs, +0xc0 0x12e8 tolerance, +0xc8 0x12f0 center on entry point, +0xe0 0x1308 entry feed;
 * ramp: +0xe8 / +0xf0 min / max length (= D % 0x1360 / 0x1368, l.314905), +0xf8 0x1320 Z clearance, +0x100 / +0x108
 * 0x1328 / 0x1330 zig / zag angle, +0x110 0x1338 XY clearance, +0x118 0x1340 XY angle (radians: used as such by
 * FUN_1801c0e80), +0x120 0x1348 auto angle, +0x121 0x1349 CCW, +0x122 0x134a skip, +0x128 0x1350 additional slot width,
 * +0x130 0x1358 align with entry point, +0x131 0x1359 ramp from entry point, +0x148 0x1370 entry feed.
 * Feeds: cur_op +0x310 = feed rate, +0x318 = plunge rate (cam_fileop.c Edit Common map).
 */

enum { PKE_PLUNGE = 0, PKE_HELIX = 1, PKE_PATH = 2, PKE_OUTSIDE = 3 };   /* OUTSIDE: Facing pocket: plunge outside the outer bound (FUN_1801bfd60), the caller feeds to the cut start */
typedef struct { Gt g; double z0, z1; } PkeMv;          /* a move of a PATH entry, z relative to the first depth */
typedef struct {
    int kind, skip;
    PV s;                    /* where the tool goes down */
    double ztop;             /* PATH: the plunge target above the depth */
    PV hc; double hr, a0, sw, h, htol; int harcs;        /* HELIX */
    PkeMv *m; int nm, cap;   /* PATH */
    PV end;                  /* the entry's end: the link to the cut start goes from here */
    double feed;             /* the entry moves' feed (the plunge to the top is always at the plunge rate, FUN_1801c4e10) */
} PkEntry;

static void pk_entry_free(PkEntry *e) { free(e->m); e->m = NULL; e->nm = e->cap = 0; }
static void pke_push(PkEntry *e, Gt g, double z0, double z1) {
    if (gt_len(&g) < 1e-12) return;                      /* ours: a zero-length move is no motion (FUN_1801c5020's first
                                                            point / a start at the end of an entity) */
    if (e->nm == e->cap) { e->cap = e->cap ? 2 * e->cap : 64; e->m = realloc(e->m, e->cap * sizeof *e->m); }
    e->m[e->nm].g = g; e->m[e->nm].z0 = z0; e->m[e->nm].z1 = z1; e->nm++;
}
/* an arc of a PATH entry from z0 to z1: as the arc (Output 3D arcs 0x12e1 on: gr_circle_mill) or linearized
   (FUN_1801c5020: n = |sweep| / sqrt(8 tol / r) + 1 points, at least 2, z linear in the angle) */
static void pke_arc(PkEntry *e, const Gt *a, double z0, double z1, int arcs, double tol) {
    if (arcs) { pke_push(e, *a, z0, z1); return; }
    double da = sqrt(tol * 8.0 / a->r); int n = (int)(fabs(a->sw) / da) + 1; if (n < 2) n = 2;
    PV q = gt_start(a);
    for (int i = 1; i < n; i++) {
        double f = (double)i / (n - 1); PV r = pv(a->c.x + a->r * cos(f * a->sw + a->sa), a->c.y + a->r * sin(f * a->sw + a->sa));
        pke_push(e, gt_line(q, r), 0, (z1 - z0) * f + z0); q = r;
    }
}

/* the op's chains hold a point (FUN_1801c38a0 l.312509: pock_bound_is_point -> user_defined_entry / pock_start) */
static int pk_has_point(const McamFile *f, const CamOp *op) {
    for (int i = 0; i < op->nchains; i++) for (int k = 0; k < op->chains[i].n; k++) if (f->ents[op->chains[i].ent[k]].type == E_POINT) return 1;
    return 0;
}

/* ------------------------------------------------------------------------------------------------ ramp line */
static Gt pke_rot(const Gt *e, double a) {
    double c = cos(a), s = sin(a); Gt r = *e;
#define R(P) pv(c * (P).x - s * (P).y, s * (P).x + c * (P).y)
    if (e->t == 'L') { r.a = R(e->a); r.b = R(e->b); } else { r.c = R(e->c); r.sa = e->sa + a; }
#undef R
    return r;
}
static PV pke_rotp(PV q, double a) { return pv(cos(a) * q.x - sin(a) * q.y, sin(a) * q.x + cos(a) * q.y); }
static int pke_dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
/* FUN_1801c0e80 (l.310797): the ramp line at XY angle a. The (offset) bounds rotated by -a; rows y = ymin + nc_tol ..
   ymax - nc_tol (bbox +0x28 / +0x38), n = trunc(2 span / stepover) + 2 rows (at least 10), evenly spaced; per row the
   intersections in pairs (1,2), (3,4).. whose midpoint is inside (pt_in_any_machinable_area); the pieces back at angle a;
   the one closest to S (dist_to_finite_l) among those at least min length (0xe8) long. (Rough angle frame rotations of
   the zigzag engine cancel: everything here is in world coordinates.) Row / entity intersections: ours (the kernel of
   pock_bound_line_inters is mccore; here lines half-open [start, end), arcs by asin, tangency left out). */
static int pke_scan(const Gls *ob, PV S, double a, double step, double minlen, double nctol, double tol, PV *L0, PV *L1, double *len) {
    Gls r = { 0 };
    for (int i = 0; i < ob->n; i++) { Gl l = { 0 }; for (int k = 0; k < ob->l[i].n; k++) gl_push(&l, pke_rot(&ob->l[i].e[k], -a)); l.island = ob->l[i].island; gls_push(&r, l); }
    double ymin = 1e300, ymax = -1e300;
    for (int i = 0; i < r.n; i++) { double bb[4]; gl_bbox(&r.l[i], bb); ymin = fmin(ymin, bb[1]); ymax = fmax(ymax, bb[3]); }
    double y0 = ymin + nctol, span = (ymax - nctol) - y0, q = span * 2.0 / step;
    q = q >= 0 ? floor(q) : ceil(q);
    int n = (int)q + 2; if (n < 10) n = 10;
    double dy = span / (n - 1), best = 0; int found = 0;
    double *xs = NULL; int cap = 0;
    for (int row = 0; row < n; row++) {
        double y = row * dy + y0; int m = 0;
        for (int i = 0; i < r.n; i++) for (int k = 0; k < r.l[i].n; k++) {
            const Gt *e = &r.l[i].e[k]; double xv[2]; int nx = 0;
            if (e->t == 'L') {
                if ((e->a.y <= y && y < e->b.y) || (e->b.y <= y && y < e->a.y)) xv[nx++] = e->a.x + (y - e->a.y) * (e->b.x - e->a.x) / (e->b.y - e->a.y);
            } else if (e->t == 'A') {
                double sv = (y - e->c.y) / e->r;
                if (fabs(sv) < 1 - 1e-12) {
                    double th[2] = { asin(sv), M_PI - asin(sv) };
                    for (int j = 0; j < 2; j++) {
                        double rel = e->sw > 0 ? th[j] - e->sa : e->sa - th[j];
                        rel = fmod(rel, 2 * M_PI); if (rel < 0) rel += 2 * M_PI;
                        if (rel < fabs(e->sw)) xv[nx++] = e->c.x + e->r * cos(th[j]);
                    }
                }
            }
            for (int j = 0; j < nx; j++) { if (m == cap) { cap = cap ? 2 * cap : 64; xs = realloc(xs, cap * sizeof *xs); } xs[m++] = xv[j]; }
        }
        qsort(xs, m, sizeof *xs, pke_dcmp);
        for (int k = 0; k + 1 < m; k += 2) {
            if (!gls_inside(&r, pv((xs[k] + xs[k + 1]) * 0.5, y), tol)) continue;
            PV p0 = pv(xs[k], y), p1 = pv(xs[k + 1], y);
            p0 = pke_rotp(p0, a); p1 = pke_rotp(p1, a);
            double l = hypot(p1.x - p0.x, p1.y - p0.y);
            double ux = p1.x - p0.x, uy = p1.y - p0.y, t = l > 0 ? ((S.x - p0.x) * ux + (S.y - p0.y) * uy) / (l * l) : 0;
            t = fmin(1, fmax(0, t));
            double d = hypot(S.x - (p0.x + ux * t), S.y - (p0.y + uy * t));
            if (minlen <= l && (!found || d < best)) { *L0 = p0; *L1 = p1; *len = l; found = 1; best = d; }
        }
    }
    free(xs); gls_free(&r);
    return found;
}
/* FUN_1801c0ad0 (l.310645): the ramp line in the rough boundary offset inward by the XY clearance (FUN_1801bfce0: slot
   width / 2 + 0x1338; FUN_1801c2440 offset_pock_bounds + delete_nested + merge -> gls_offset, our substitute); at the XY
   angle 0x1340, or with Auto angle 0x1348 the longest of 0, 10 .. 170 degrees refined by 10 halvings of the 10 degree
   step (best +- step, strictly longer wins); longer than max length: cut to it from the end nearest S */
static int pke_ramp_line(const CamParams *p, const Gls *rb, PV S, double minfil, double tol, double nctol, PV *P0, PV *P1) {
    double D = p->tool_dia, xyclr = pkd(p, 0x1350) * 0.5 + pkd(p, 0x1338);
    double minlen = D * pkd(p, 0x1360) / 100, maxlen = D * pkd(p, 0x1368) / 100;
    double step = D * p->stepover_pct / 100;            /* pock_prm +0x18 = D % 0x1248 (l.314899) */
    if (step <= 1e-12) return 0;
    Gls ob = gls_offset(rb, -xyclr, minfil, tol);
    if (!ob.n) { gls_free(&ob); return 0; }
    int found = 0; double len = 0;
    if (!pkb(p, 0x1348)) found = pke_scan(&ob, S, pkd(p, 0x1340), step, minlen, nctol, tol, P0, P1, &len);
    else {
        double best = 0, ba = 0, a = 0, st = 0.1745329251994329; PV q0, q1; double l;
        for (int k = 0; k < 18; k++, a += 0.1745329251994329)
            if (pke_scan(&ob, S, a, step, minlen, nctol, tol, &q0, &q1, &l) && best < l) { *P0 = q0; *P1 = q1; found = 1; best = l; ba = a; }
        for (int r = 0; r < 10; r++) {
            st *= 0.5; double nb = ba;
            for (int s = 0; s < 2; s++) {
                double aa = s == 0 ? ba - st : ba + st;
                if (pke_scan(&ob, S, aa, step, minlen, nctol, tol, &q0, &q1, &l) && best < l) { *P0 = q0; *P1 = q1; found = 1; best = l; nb = aa; }
            }
            ba = nb;
        }
        len = best;
    }
    gls_free(&ob);
    if (found && maxlen < len) {
        double dx = P1->x - P0->x, dy = P1->y - P0->y, l = hypot(dx, dy), ux = l == 0 ? 0 : dx / l, uy = l == 0 ? 0 : dy / l;
        if (hypot(P1->x - S.x, P1->y - S.y) <= hypot(P0->x - S.x, P0->y - S.y)) *P0 = pv(P1->x - ux * maxlen, P1->y - uy * maxlen);
        else *P1 = pv(P0->x + ux * maxlen, P0->y + uy * maxlen);
    }
    return found;
}

/* ------------------------------------------------------------------------------------------------ the three entries */
/* FUN_1801c30d0 (l.312161): ramp. H = top - depth + Z clearance 0x1320 (< nc_tol: fails). Line P0 P1 (above, length L
   >= nc_tol); zig / zag drops tz[0] = tan(0x1328) L, tz[1] = tan(0x1330) L. Walking back from the end nearest S (the
   last pass uses tz[1], alternating) until the height is used up gives the start parameter u and the first pass's
   angle; then the passes from u, each to the other end, dropping |u_end - u| tz; z never below the depth. With an
   additional slot width (half > nc_tol) each point is offset by +-half along the line's left normal (side per pass
   direction, 0x1349 = 0 CW flips it, the first point flipped too), and from the third point a half circle about the
   previous centre point at the previous Z (gr_arc_mill, CW when 0x1349 = 0) joins the two sides. End = the last centre
   point. Feed 0x1370 ? feed : plunge (FUN_1801c4e10). */
static int pke_ramp(const CamParams *p, const Gls *rb, PV S, double z_first, double minfil, double tol, double nctol, PkEntry *e) {
    double H = (p->top_z - z_first) + pkd(p, 0x1320);
    if (H < nctol) return 0;
    PV P0, P1;
    if (!pke_ramp_line(p, rb, S, minfil, tol, nctol, &P0, &P1)) return 0;
    double dx = P1.x - P0.x, dy = P1.y - P0.y, L = hypot(dx, dy);
    if (L < nctol) return 0;
    double tz[2] = { tan(pkd(p, 0x1328)) * L, tan(pkd(p, 0x1330)) * L };
    if (tz[0] <= 1e-12 && tz[1] <= 1e-12) return 0;      /* ours: the decomp's loop would not end (dialog minimum 0.1 deg) */
    double dir, ucur;
    if (hypot(P1.x - S.x, P1.y - S.y) <= hypot(P0.x - S.x, P0.y - S.y)) { dir = -1; ucur = 1; } else { dir = 1; ucur = 0; }
    double Hr = H, u = 0; int k = 1, kf = 0, npass = 1;
    while (u = 0, kf = 0, 0.0 < Hr) {
        npass++;
        if (npass > 100000) return 0;                      /* ours: a guard (thousands of passes = a near-zero angle) */
        double t = tz[k];
        if (1e-12 < t && Hr <= t) {
            u = fabs(t) <= 1e-12 ? 0 : Hr * dir / t + ucur; kf = k;
            if (u < 0 || 1 < u) u = 0;
            break;
        }
        Hr -= t; k = !k;
        if (ucur <= 0.5) { dir = -1; ucur = 1; } else { dir = 1; ucur = 0; }
    }
    double half = pkd(p, 0x1350) * 0.5; int slot = !(half < nctol || half == nctol), cw = 0; PV nrm = pv(0, 0);
    if (slot) { nrm = pv(-dy / L * half, dx / L * half); cw = pkb(p, 0x1349) == 0; }
    e->feed = pkb(p, 0x1370) ? p->feed : p->plunge_feed;
    double zabs = H + z_first, zprev = 0; PV prevc = pv(0, 0), cur = pv(0, 0), c = pv(0, 0);
    for (int s = 0; ; s++) {
        double ucur0 = ucur, zc = fmax(zabs, z_first);
        c = pv((1 - u) * P0.x + P1.x * u, (1 - u) * P0.y + P1.y * u);
        PV pt = c;
        if (slot) {
            double sg = ucur <= u ? -1 : 1; if (cw) sg = -sg; if (s == 0) sg = -sg;
            pt = pv(c.x + nrm.x * sg, c.y + nrm.y * sg);
            if (s > 1) {
                /* to prevc + nrm sg (the other side): a half circle */
                Gt a = gt_arc(prevc, half, atan2(cur.y - prevc.y, cur.x - prevc.x), cw ? -M_PI : M_PI);
                pke_push(e, a, zprev - z_first, zprev - z_first);
                cur = gt_end(&a);
            }
        }
        if (s == 0) { e->s = pt; e->ztop = zc - z_first; }
        else pke_push(e, gt_line(cur, pt), zprev - z_first, zc - z_first);
        cur = pt;
        zabs = zc - fabs(ucur - u) * tz[kf]; kf = !kf; ucur = ucur > 0.5 ? 0 : 1;
        if (s + 1 >= npass) break;
        u = ucur0; prevc = c; zprev = zc;
    }
    e->end = c; e->kind = PKE_PATH;
    return 1;
}

/* FUN_1801c14b0 (l.311010): helix "follow boundary". The rough boundary offset inward by the helix XY clearance
   (FUN_1801c2440), its first outer loop, turned to 0x12d0's direction (pock_ccw / reverse_pock_bound); shorter than
   0x12d8 "if length exceeds": fails. Started at the entity whose start is nearest S (reorder_pock_bound); slope
   tan(0x12c0) (< 1e-12 fails); horizontal length H / tan = laps x perimeter + rest: the ramp starts `rest` before the
   loop's start (walking back from the last entity) and runs `laps` more full turns, ending at the loop start at the
   depth; z drops by the entity length x tan per entity (never below the depth). Lines FUN_1801c4e10, arcs as helical
   arcs (0x12e1 on, gr_circle_mill) or linearized at 0x12e8 (FUN_1801c5020); feed 0x1308 (gated). End = the loop start. */
static int pke_bound(const CamParams *p, const Gls *rb, PV S, double z_first, double hzclr, double hang, double xyclr, int hccw,
                     double minfil, double tol, PkEntry *e) {
    Gls ob = gls_offset(rb, -xyclr, minfil, tol);
    int li = -1; for (int i = 0; i < ob.n && li < 0; i++) if (!ob.l[i].island && ob.l[i].n) li = i;
    if (li < 0) { gls_free(&ob); return 0; }
    Gl L = gl_copy(&ob.l[li]); gls_free(&ob);
    if ((gl_area(&L) > 0) != (hccw != 0)) gl_reverse(&L);
    double per = gl_len(&L);
    if (per < pkd(p, 0x12d8)) { gl_free(&L); return 0; }
    int kpre = 0; double bd = 1e300;
    for (int i = 0; i < L.n; i++) { PV q = gt_start(&L.e[i]); double d = hypot(S.x - q.x, S.y - q.y); if (d < bd) { bd = d; kpre = i; } }
    if (kpre) gl_rotate(&L, kpre);
    double ta = tan(hang);
    if (ta < 1e-12) { gl_free(&L); return 0; }
    double H = (p->top_z - z_first) + hzclr, Lh = H / ta;
    int laps = (int)floor(Lh / per); double rem = Lh - laps * per, t = 0; int i = L.n;
    for (;;) {
        i--;
        if (i < 0) { i = kpre; t = 0; break; }             /* (as decoded: the pre-reorder index) */
        double len = gt_len(&L.e[i]);
        if (rem <= len + 1e-12) { t = 1 - rem / len; break; }
        rem -= len;
    }
    int arcs = p->pk_ok ? pkb(p, 0x12e1) : 1; double atol = p->pk_ok ? pkd(p, 0x12e8) : 0;
    double zabs = H + z_first, zc = fmax(zabs, z_first), zprev;
    e->s = gt_pt(&L.e[i], t); e->ztop = zc - z_first;
    zabs = zc - gt_len(&L.e[i]) * (1 - t) * ta; zprev = zc;
    double tcur = t; int lap = 0;
    while (lap <= laps) {
        zc = fmax(zabs, z_first);
        Gt g = tcur > 0 ? gt_sub(&L.e[i], tcur, 1) : L.e[i];
        if (g.t == 'L') pke_push(e, g, zprev - z_first, zc - z_first);
        else if (g.t == 'A') pke_arc(e, &g, zprev - z_first, zc - z_first, arcs, atol);
        if (++i >= L.n) { lap++; i = 0; }
        zabs = zc - gt_len(&L.e[i]) * ta; zprev = zc; tcur = 0;
    }
    e->end = gt_start(&L.e[0]); e->kind = PKE_PATH;
    gl_free(&L);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ plan / emit */
/* _enter_pocket (FUN_1801bc830 l.307923-307990) for a cut starting at S, planned at the first depth z_first */
static int pk_entry_plan(const McamFile *f, CamOp *op, const Gls *rb, PV S, double z_first, PkEntry *e) {
    const CamParams *p = g_pk_ep ? g_pk_ep : &op->p;
    memset(e, 0, sizeof *e); e->kind = PKE_PLUNGE; e->s = S; e->end = S;
    double tol = cam_chain_tol(mcam_is_metric(f)) * 0.5, nctol = g_nc_tol;
    int roll = p->pk_ok ? p->pk_roll : 1; double minfil = roll == 0 ? M_PI : roll == 1 ? 0.785398 : 0.01;
    if (p->pk_ok && pks(p, 0x13fa) == 1) {   /* _enter_pocket l.307880: Facing: FUN_1801bfd60 (approach distance pock_prm +0x168 = 0x1390), before the entry-on test (LAB_1801bcbc8 only when it fails: sVar5 == -1) */
        PV Q;
        int spiral = pk_spiral_cutting(p);
        for (int i = 0; i < rb->n; i++) {
            if (rb->l[i].island) continue;
            if (pk_face_out(g_pk_face_chain, &rb->l[i], S, pkd(p, 0x1390), spiral, p->tool_dia * 0.5, minfil, tol, nctol, &Q)) { e->kind = PKE_OUTSIDE; e->s = Q; e->end = Q; return 1; }
            break;
        }
    }
    if (g_pk_isl_chk) {   /* _enter_pocket l.307880-307916: Island facing, facing an island: FUN_1801bfd60 -> FUN_1801c1fc0 (cam2d_pock_isl.c); -1 (no point found): the normal entry below (LAB_1801bcbc8) */
        for (int i = 0; i < rb->n; i++) {
            if (rb->l[i].island) continue;
            PV Q;
            if (pk_isl_out(p, &rb->l[i], g_pk_isl_chk, S, pkd(p, 0x1390), minfil, tol, &Q)) {
                if (!pk_line_inside(g_pk_isl_chk, Q, S, nctol)) { snprintf(op->error, sizeof op->error, "pocket type Island facing: the entry move leaves the check bounds (FUN_1801e0f80 find_path_around_island NOT DECODED)"); return 0; }
                e->kind = PKE_OUTSIDE; e->s = Q; e->end = Q; return 1;   /* FUN_1801c4e10: rapid above Q, plunge; the link to the cut start (pk_rough_depth / pk_zz_depth) */
            }
            break;
        }
    }
    int entry_on = p->pk_ok ? pkb(p, 0x12a0) : 1, type = p->pk_ok ? pks(p, 0x12a2) : 0;
    /* a point chained with the pocket = the user's entry point (FUN_1801c38a0 l.312509: looked for when the entry is off,
       or helix + 0x12f0 Center on entry point, or ramp + 0x1358 Align / 0x1359 Ramp from entry point) */
    if (p->pk_ok && pk_has_point(f, op) && (!entry_on || (type == 0 && pkb(p, 0x12f0)) || (type == 1 && (pkb(p, 0x1358) || pkb(p, 0x1359))))) {
        snprintf(op->error, sizeof op->error, "pocket entry from a chained entry point: NOT DECODED"); return 0; }
    /* off, open pocket with 0x13f0, or ramp + Ramp from entry point (no point: the plain plunge, l.307927) */
    if (!entry_on || (p->pk_ok && pks(p, 0x13fa) == 4 && pkb(p, 0x13f0)) || (type == 1 && pkb(p, 0x1359))) return 1;
    int ok = 0;
    if (type == 0) {
        double hmin = p->tool_dia * (p->pk_ok ? pkd(p, 0x12f8) : 50) / 100, hmax = p->tool_dia * (p->pk_ok ? pkd(p, 0x1300) : 100) / 100;
        double hzclr = p->pk_ok ? pkd(p, 0x12b8) : p->helix_zclr, hang = p->pk_ok ? pkd(p, 0x12c0) : p->ramp_deg * M_PI / 180;
        double xyclr = p->pk_ok ? pkd(p, 0x12c8) : 0; int hccw = p->pk_ok ? pkb(p, 0x12d0) : 1;
        int fb = p->pk_ok && pkb(p, 0x12d2), fonly = p->pk_ok && pkb(p, 0x12d3);
        /* entry feed 0x1308 (FUN_1801c4a60 / FUN_1801c4e10: feed rate when set, else the plunge rate). Batch 6 honoured it after
           an edit only; decisions batch dec1 (formats/user_decisions.md B6-5: follow the defaults' "use feed rate") always -
           Mill_Inch's default op holds 1, Mill_MM 0, the user's stored pocket (A001 op 13) 0 */
        e->feed = p->pk_ok && pkb(p, 0x1308) ? p->feed : p->plunge_feed;
        if (!(fb && !fonly)) {
            /* FUN_1801c1d80: H < nc_tol fails; find_helix_center; the sweep H 2 pi / (tan(angle) r 2 pi), sign by 0x12d0,
               ending at the angle towards S. The link from its end to S must stay inside (existing rule of this port:
               pock_move_avoid_islands' path around islands is NOT DECODED, so a link that leaves = the entry fails) */
            PV hc; double hr;
            double H = (p->top_z - z_first) + hzclr;
            if (H >= nctol && hang > 1e-9 && pk_helix_center(rb, S, hmax, hmin, xyclr, minfil, tol, nctol, &hc, &hr)) {
                double ae = atan2(S.y - hc.y, S.x - hc.x); PV he = pv(hc.x + hr * cos(ae), hc.y + hr * sin(ae));
                if (pk_line_inside_ent(rb, he, S, nctol)) {
                    double h = (p->top_z - z_first) + hzclr, dzt = tan(hang) * hr * 2 * M_PI, sw = h * 2 * M_PI / dzt;
                    if (!hccw) sw = -sw;
                    double an = ae; if (an < 0) an += 2 * M_PI;
                    e->kind = PKE_HELIX; e->hc = hc; e->hr = hr; e->sw = sw; e->a0 = an - sw; e->h = h;
                    e->harcs = p->pk_ok ? pkb(p, 0x12e1) : 1; e->htol = p->pk_ok ? pkd(p, 0x12e8) : 0;
                    if (!e->harcs && !(e->htol > 0)) { snprintf(op->error, sizeof op->error, "pocket helix: linearized with tolerance <= 0"); return 0; }
                    e->s = pv(hc.x + hr * cos(e->a0), hc.y + hr * sin(e->a0)); e->end = he;
                    ok = 1;
                }
            }
        }
        if (!ok && fb) {
            if (p->pk_ok && !pkb(p, 0x12e1) && !(pkd(p, 0x12e8) > 0)) { snprintf(op->error, sizeof op->error, "pocket helix: linearized with tolerance <= 0"); return 0; }
            ok = pke_bound(p, rb, S, z_first, hzclr, hang, xyclr, hccw, minfil, tol, e);
            if (ok && !pk_line_inside_ent(rb, e->end, S, nctol)) ok = 0;      /* the link rule above */
            if (!ok) { pk_entry_free(e); e->kind = PKE_PLUNGE; }
        }
    } else if (type == 1) {
        ok = pke_ramp(p, rb, S, z_first, minfil, tol, nctol, e);
        if (getenv("PK_DEBUG")) fprintf(stderr, "ramp: ok %d end %.4f %.4f S %.4f %.4f link %d\n", ok, e->end.x, e->end.y, S.x, S.y, ok ? pk_line_inside_ent(rb, e->end, S, nctol) : -1);
        if (ok && !pk_line_inside_ent(rb, e->end, S, nctol)) ok = 0;          /* the link rule above */
        if (!ok) { pk_entry_free(e); e->kind = PKE_PLUNGE; }
    }
    if (!ok) {                                           /* l.307969: skip (0x134a ramp / 0x12d1 otherwise) or plunge at S;
                                                            save skipped boundary 0x12e0 (save_bound) and warning 0x955 are not
                                                            done here */
        e->kind = PKE_PLUNGE; e->s = S; e->end = S;
        if (p->pk_ok && (type == 1 ? pkb(p, 0x134a) : pkb(p, 0x12d1))) e->skip = 1;
    }
    return 1;
}

/* the Z of the entry's first record: plunge_move's height at the section start (wr_pock_toolpath l.316198; FUN_1801c4a60 l.313310-313316; ramp FUN_1801c30d0) */
static double pk_entry_z(const PkEntry *e, double z) { return e->kind == PKE_HELIX ? z + e->h : e->kind == PKE_PATH ? z + e->ztop : z; }
/* the entry at depth z: rapid to its start at z_app, to the feed plane, plunge feed to its top (FUN_1801c4e10 first
   point: plunge_move / gr_lin_mill at the plunge rate), then its moves (the first depth's, shifted). PLUNGE: down at S. */
static void pk_entry_emit(MB *b, const CamParams *p, const PkEntry *e, double z, double z_app, double ctol) {
    double zf = p->top_z + p->feed_plane;
    if (g_pk_sec_on) { if (!g_pk_down) { if (!isnan(z_app)) pk_rapid(b, e->s.x, e->s.y, z_app); double zr = pk_entry_z(e, z); pk_rapid(b, e->s.x, e->s.y, zr + CAM_SYS_TOL < g_pk_hf ? g_pk_hf : zr); }   /* plunge_move, mccore_moves.c l.148-153: a rapid to max(feed plane, the first record's Z) */ }   /* pk_sec_start; g_pk_down: the tool stays down (plunge_move l.157-165: the moves below feed there) */
    else {
    mv(b, 1, e->s.x, e->s.y, z_app, 0);
    mv(b, 1, e->s.x, e->s.y, zf, 0);
    }
    if (e->kind == PKE_PLUNGE || e->kind == PKE_OUTSIDE) { mv(b, 0, e->s.x, e->s.y, z, p->plunge_feed); return; }   /* FUN_1801c4e10: rapid above the point, plunge at the plunge rate */
    if (e->kind == PKE_HELIX) {
        double h = e->h, sw = e->sw, a0 = e->a0;
        mv(b, 0, e->s.x, e->s.y, z + h, p->plunge_feed);
        if (e->harcs) {                                   /* FUN_1801c4a60: full turns, then the rest */
            double s = sw > 0 ? 1 : -1, done = 0, z0 = z + h;
            while (fabs(sw) - done > 1e-9) {
                double piece = fmin(2 * M_PI, fabs(sw) - done);
                Gt t = gt_arc(e->hc, e->hr, a0 + s * done, s * piece);
                double z1 = z + h - h * (done + piece) / fabs(sw);
                pk_arc_moves(b, &t, z0, z1, e->feed, ctol); z0 = z1; done += piece;
            }
        } else {                                          /* FUN_1801c5020 */
            double da = sqrt(e->htol * 8.0 / e->hr); int n = (int)(fabs(sw) / da) + 1; if (n < 2) n = 2;
            for (int i = 1; i < n; i++) {
                double f = (double)i / (n - 1), an = f * sw + a0;
                mv(b, 0, e->hc.x + e->hr * cos(an), e->hc.y + e->hr * sin(an), (z - (z + h)) * f + (z + h), e->feed);
            }
        }
        return;
    }
    mv(b, 0, e->s.x, e->s.y, z + e->ztop, p->plunge_feed);
    for (int i = 0; i < e->nm; i++) {
        const PkeMv *m = &e->m[i];
        if (m->g.t == 'L') mv(b, 0, m->g.b.x, m->g.b.y, z + m->z1, e->feed);
        else pk_arc_moves(b, &m->g, z + m->z0, z + m->z1, e->feed, ctol);
    }
}
