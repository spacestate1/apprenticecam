/* cam3d_hstlink.c - the general HST linker's (EvaGeneralLinkerPlan, mchmm.dll) lead attempts of a retract: what FUN_1801d76a0 does when the
 * vertical lead of a retract link rises (formats/hst_linker.md 24). Included by cam3d_finish.c after the lead check (c3_lc_lead / c3_lc_arc);
 * shared linker connect code (the plan for the wander FUN_1801dde40 too, which Waterline's linker also reaches) goes here.
 * x1 / x7 / x8 / x10 / x11 / x15 / x16 = decomp/hmm_linker_xN.c (Ghidra DecompAt dumps of mchmm.dll, local), hk = decomp/hmm_linker.c,
 * hc = decomp/hst3d_chop.c, dis = objdump of mchmm.dll.
 *
 * The lead connect of one side (FUN_1801dab60 exit / FUN_1801db250 approach, x2 l.1041-1330): FUN_1801dbf80 (x11 l.2482-3027) first puts the
 * pass's points into the connect - for the approach the next pass from its start S forwards, for the exit the previous pass from its end E
 * backwards: each segment's start point while the XY length walked is below (float connect +0x248 = 2.5, FUN_1801d1e70 x1 l.929) x the lead
 * radius or fewer than three points are in, then the last segment's end (hl_pnodes). The lead nodes (top T, arc centre C) go before S / after E
 * (FUN_1801db820 / FUN_1801da4a0), then FUN_1801d7450 (x8 l.587-694) drops T. When T rises (on a check strip: the traced case) it removes C,
 * the node walk finds no arc after T (FUN_1801d9330 returns 1), FUN_1801d87b0 removes what lies between T and S / E and FUN_1801d76a0
 * (x10 l.526-760) runs with param_7 = the "attempts" flag of FUN_1801d69f0:
 *   - attempts on: FUN_1801d8240 "FindBetterArc" (x11 l.1-175): a bisection on the overlap d (from |S - T|xy / 2, the step halved each round,
 *     at most 10000 rounds, stopping when d < connect +0x200 = (float) LinkTrimMinOverlapDiff, FUN_1801d1e70 arg 10 = plan +0x110c): per round
 *     FUN_180171b50 fits the arc of radius R = |C - T| that leaves the pass at the walked length v (hl_fit), FUN_1801d8ce0 puts its three nodes in
 *     and FUN_1801d8b30 (x11 l.400-481) checks them: the new top dropped (FUN_1801cf7b0) must not rise and the arc's refinement (FUN_1801cece0,
 *     c3_lc_arc) must leave it as it is (FUN_1801e0ee0 / FUN_1801d91e0); a good arc raises d, a bad one lowers it; success stores the trim v
 *     (approach +0x1e8 = v, exit +0x1e0 = -v). Failing that FUN_1801d7b00 (x8 l.219-586) fits with overlap 0 and stores its v the same way
 *     (succeeds when v != 0);
 *   - else (or both failing) without the frag's +0x78 (connect +0x1ca / +0x1c9: never on a Raster pass) and without the connect mode's neighbour
 *     frag (mode +0x50 / +0x48; empty on every traced Raster retract: c45 100 / 100 FUN_1801d76a0 calls) FUN_1801d8630 (x11 l.2395-2480): the
 *     vertical lead - one node at S / E raised by R, trim 0.
 * Only line segments are ported (Raster's passes have no arc points; an arc node in the walk is NOT DECODED here: the caller keeps SAFE). */

/* the pass points FUN_1801dbf80 puts into a lead connect, in the walk's order (q[0] = S or E); the count */
static int hl_pnodes(const double *xyz, int m, int ex, double L, double *q) {
    int n = 0; double len = 0.0;
    const double *last = ex ? xyz + 3 * (m - 1) : xyz;                         /* frag +0x88 / +0xb0: its start / end */
    for (int k = 0; ; k++) {
        if ((L <= len && 2 < n) || k >= m - 1) { memcpy(q + 3 * n, last, sizeof(double) * 3); return n + 1; }
        const double *a = ex ? xyz + 3 * (m - 1 - k) : xyz + 3 * k, *b = ex ? a - 3 : a + 3;
        memcpy(q + 3 * n, a, sizeof(double) * 3); n++; last = b;
        len += sqrt((b[1] - a[1]) * (b[1] - a[1]) + (b[0] - a[0]) * (b[0] - a[0]));
    }
}
/* FUN_180171b50's outputs (x11 / hc l.2431-2560): the trim v (param_1[0]), param_1[1] (c0), the arc's point on the pass side (p6 for the exit,
   p8 for the approach), centre p7 and top; e8 = param_11 */
typedef struct { double v, c0, p6[3], p7[3], p8[3], e8; int cnt; } HlFit;
/* the walker over q[0..n-1] (FUN_1801775c0 / 180177860 / 180177540 / 180177520 for a node list without arc nodes: segment i = q[i] -> q[i+1],
   the walk ends at q[n-2], x15 l.38-135) */
typedef struct { const double *q; int n, i; } HlW;
/* FUN_180172020 (x11 l.486-740, the closed-pass test dis 1801720a0-180172240): walks the segments accumulating the XY length into *acc until the
   segment where acc + its XY length reaches lim = R + (the segment's rise along the walk) x R / its 3D length; *v = the 3D length of the segments
   before it; s100 / sd8 = its points (rev 0: far / near end, rev 1: near / far, as the DLL's local_100 / local_d8); 0 = the walk ran out */
static int hl_walk(HlW *w, double *v, double *s100, double *sd8, int *cnt, double R, double *acc, double *lim, double *d2, int rev) {
    double dir = rev ? 1.0 : -1.0;
    *v = 0.0;
    int fresh = 1;
    for (;;) {
        if (!fresh) {                                                         /* the list's last node in the walk's direction = its first? */
            const double *A = w->q + 3 * (w->n - 1), *B = w->q;
            if (A[0] != B[0] || A[1] != B[1] || A[2] != B[2]) { *v = 0.0; return 0; }
            (*cnt)++; w->i = 0;                                               /* FUN_180177860 */
        }
        const double *s0 = w->q + 3 * w->i, *s1 = s0 + 3;
        const double *p3 = rev ? s0 : s1, *p4 = rev ? s1 : s0;
        memcpy(s100, p3, sizeof(double) * 3); memcpy(sd8, p4, sizeof(double) * 3);
        double dz = p4[2] - p3[2], L;
        *d2 = (p4[1] - p3[1]) * (p4[1] - p3[1]) + (p4[0] - p3[0]) * (p4[0] - p3[0]);
        L = sqrt(dz * dz + *d2);
        int go = 0;
        if (dz == 0.0) { *lim = R; go = 1; }
        else if (0.0 < L) { *lim = dir * dz * (R / L) + R; go = 1; }
        if (go && 0.0 < L) {
            double x = sqrt(*d2) + *acc;
            if (*lim <= x) return 0.0 < *d2;
            *acc = x; *v = L + *v;
        }
        if (w->i == w->n - 2) fresh = 0; else { w->i++; fresh = 1; }
    }
}
/* FUN_1801725b0 (x11 l.741-911), a line segment: the point P at the XY distance lim - acc along it from the walk side; *v += its 3D distance
   from the segment's walk-side end; 1 unless acc was already past lim */
static int hl_point(const double *s100, const double *sd8, double *P, double *v, double lim, double acc, double d2, int rev) {
    int r = 1; double f = 1.0;
    if (acc <= lim) { f = lim - acc; f = rev ? 1.0 - f / sqrt(d2) : f / sqrt(d2); }
    else { if (!rev) f = 0.0; r = 0; }
    if (!(sd8[0] == s100[0] && sd8[1] == s100[1] && sd8[2] == s100[2])) {   /* FUN_18002d030 */
        double g = 1.0 - f;
        P[0] = g * sd8[0] + f * s100[0]; P[1] = g * sd8[1] + f * s100[1]; P[2] = g * sd8[2] + f * s100[2];
    } else memcpy(P, sd8, sizeof(double) * 3);
    const double *b = rev ? s100 : sd8;
    double x = P[0] - b[0], y = P[1] - b[1], z = P[2] - b[2];
    *v = sqrt(y * y + x * x + z * z) + *v;
    if (*v == 0.0) { x = s100[0] - sd8[0]; y = s100[1] - sd8[1]; z = s100[2] - sd8[2]; *v = sqrt(y * y + x * x + z * z); }
    return r;
}
/* FUN_180172950 (x11 l.916-1425) for a line walk: the arc's point at the walk start (out[0..2]) and the walk's unit XY direction (out[3..4]).
   f = FUN_1801725b0's result, a0 = the overlap, acc = the XY length before P's segment, hc / cz = the arc centre's horizontal offset / Z;
   p8 = the pass-side point, *e8 / *c0 as the DLL's param_7 / param_14. 0 = an arc segment (NOT DECODED here) */
static int hl_tangent(double *out, HlW *w, int f, const double *s100, const double *sd8, const double *P, double *e8, double *p8, double a0,
                      double acc, double R, double hc, double cz, double *c0, int rev) {
    w->i = 0;                                                                 /* FUN_180177860 */
    const double *s0 = w->q, *s1 = w->q + 3;
    double dx = s1[0] - s0[0], dy = s1[1] - s0[1], n = sqrt(dy * dy + dx * dx);
    if (n == 0.0) n = 1.0;
    double ux = 1.0 / n * dx, uy = 1.0 / n * dy, d38 = 0.0, rz;
    out[3] = ux; out[4] = uy;
    if (uy * uy + ux * ux == 0.0) { memcpy(out, P, sizeof(double) * 3); return 1; }
    double ex, ey;
    if (!rev) { ex = sd8[0] - P[0]; ey = sd8[1] - P[1]; } else { ex = P[0] - s100[0]; ey = P[1] - s100[1]; }
    double tot = sqrt(ey * ey + ex * ex) + acc;
    int sg = sd8[2] < s100[2] ? 1 : -1;
    if (!rev) sg = -sg;
    *e8 = ((double)sg * hc + tot) - R;
    const double *st = w->q;                                                  /* FUN_180177900: the walk's start node */
    if (f != 0) {
        *e8 = 0.0;
        double t = R * R - (R - a0) * (R - a0);
        if (0.0 < t) d38 = sqrt(t);
        rz = cz - d38;
        out[0] = st[0]; out[1] = st[1]; out[2] = rz;
        memcpy(p8, st, sizeof(double) * 3);
        return 1;
    }
    /* f == 0 (x11 l.1124-1330) */
    s0 = w->q + 3 * w->i; s1 = s0 + 3;
    double sl = sqrt((s1[1] - s0[1]) * (s1[1] - s0[1]) + (s1[0] - s0[0]) * (s1[0] - s0[0]));
    while (sl == 0.0 && w->i != w->n - 2) {
        w->i++; s0 = w->q + 3 * w->i; s1 = s0 + 3;
        sl = sqrt((s1[1] - s0[1]) * (s1[1] - s0[1]) + (s1[0] - s0[0]) * (s1[0] - s0[0]));
    }
    double d32 = a0;
    if (d32 != 0.0 || *e8 <= 0.0) {
        double q = *e8 / sl;
        if (d32 - *e8 < 0.0) { memcpy(out, P, sizeof(double) * 3); out[3] = ux; out[4] = uy; return 1; }
        if (q < 0.0 || 1.0 < q) {
            double r = R, t = (d32 - *e8) - r; t = r * r - t * t; int neg = 0;
            if (t < 0.0) { double t2 = ((double)-sg * hc + tot) - r; *e8 = t2; double u = (d32 - t2) - r; t = r * r - u * u; neg = t < 0.0; }
            if (!neg && t != 0.0) d38 = sqrt(t);
            out[0] = st[0]; out[1] = st[1]; out[2] = cz - d38;
            memcpy(p8, st, sizeof(double) * 3);
            return 1;                                                         /* (LAB_18017357a: *e8 kept) */
        }
        double g = 1.0 - q;
        if (s1[0] == s0[0] && s1[1] == s0[1] && s1[2] == s0[2]) memcpy(p8, s0, sizeof(double) * 3);
        else { p8[0] = s0[0] * g + s1[0] * q; p8[1] = s0[1] * g + s1[1] * q; p8[2] = s0[2] * g + s1[2] * q; }
        double x = p8[0] - s0[0], y = p8[1] - s0[1], z = p8[2] - s0[2];
        *c0 = sqrt(y * y + x * x + z * z) + *c0;
        double t = R * R - (R - d32) * (R - d32);
        if (0.0 < t) d38 = sqrt(t);
        rz = cz - d38;
    } else {
        double d = fabs(*e8), q = d / sl;
        if (1.0 < q) {
            do {
                if (w->i == w->n - 2) break;                                  /* FUN_180177520 */
                s0 = w->q + 3 * w->i; s1 = s0 + 3;
                d = d - sl;
                double x = s1[0] - s0[0], y = s1[1] - s0[1], z = s1[2] - s0[2];
                *c0 = sqrt(y * y + x * x + z * z) + *c0;
                w->i++; s0 = w->q + 3 * w->i; s1 = s0 + 3;                    /* FUN_180177540 */
                sl = sqrt((s1[1] - s0[1]) * (s1[1] - s0[1]) + (s1[0] - s0[0]) * (s1[0] - s0[0]));
                if (0.0 < sl) {
                    double gx = s1[0] - s0[0], gy = s1[1] - s0[1], gn = sqrt(gy * gy + gx * gx);
                    if (gn == 0.0) gn = 1.0;
                    double r = 1.0 / gn; q = d / sl; ux = r * gx; uy = r * gy;
                }
            } while (1.0 < q);
        }
        double g = 1.0 - q;
        if (s1[0] == s0[0] && s1[1] == s0[1] && s1[2] == s0[2]) memcpy(p8, s0, sizeof(double) * 3);
        else { p8[0] = g * s0[0] + s1[0] * q; p8[1] = g * s0[1] + s1[1] * q; p8[2] = g * s0[2] + s1[2] * q; }
        double x = s0[0] - p8[0], y = s0[1] - p8[1], z = s0[2] - p8[2];
        *c0 = sqrt(y * y + x * x + z * z) + *c0;
        rz = cz;
    }
    out[0] = p8[0]; out[1] = p8[1]; out[2] = rz; out[3] = ux; out[4] = uy;
    *e8 = 0.0;
    return 1;
}
/* FUN_1801747f0 (x11 l.1430-1505): the fallback - the walk start, no arc (v = 0) */
static void hl_fit0(const HlW *w, HlFit *o, double R, int rev) {
    const double *st = w->q; double up[3] = { st[0], st[1], st[2] + R };
    memcpy(rev ? o->p6 : o->p8, up, sizeof up); memcpy(rev ? o->p8 : o->p6, st, sizeof up);   /* param_2 = rev ? up : P, param_3 = rev ? P : up */
    o->v = 0.0; o->c0 = 0.0;
}
/* FUN_180171b50 with the overlap ov on the pass points q[0..n-1] (rev = the exit); 0 = NOT DECODED */
static int hl_fit(const double *q, int n, double R, int rev, double ov, HlFit *o) {
    HlW w = { q, n, 0 };
    memset(o, 0, sizeof *o);
    if (n < 2) return 0;
    double a0 = ov, acc = ov, lim = -1.0, d2 = 0.0, v = 0.0, c0 = 0.0, s100[3] = { 0 }, sd8[3] = { 0 }, P[3];
    if (R == 0.0) { hl_fit0(&w, o, R, rev); return 1; }
    if (!hl_walk(&w, &v, s100, sd8, &o->cnt, R, &acc, &lim, &d2, rev)) { hl_fit0(&w, o, R, rev); return 1; }
    int f = hl_point(s100, sd8, P, &v, lim, acc, d2, rev);
    double dz = sd8[2] - s100[2], hc, cz;
    if (dz == 0.0) { hc = 0.0; cz = R; }
    else { hc = R / sqrt(d2 / (dz * dz) + 1.0); cz = sqrt(d2) * hc / fabs(dz); }
    cz = P[2] + cz;
    double r[5], e8 = 0.0, p5[3];
    if (!hl_tangent(r, &w, f, s100, sd8, P, &e8, p5, a0, acc, R, hc, cz, &c0, rev)) return 0;
    if (r[0] != P[0] || r[1] != P[1] || r[2] != P[2]) {
        double top[3] = { r[0], r[1], r[2] }, pt[3] = { r[0], r[1], r[2] };
        if (a0 != 0.0) {
            double t = (a0 - e8) - R, y = r[1] - r[4] * t;
            o->p7[0] = r[0] - r[3] * t; o->p7[1] = y; o->p7[2] = cz;
            top[0] = o->p7[0] - r[3] * R; top[1] = o->p7[1] - r[4] * R; top[2] = o->p7[2];
        }
        memcpy(o->p6, rev ? pt : top, sizeof top); memcpy(o->p8, rev ? top : pt, sizeof top);
        o->e8 = e8; o->v = v; o->c0 = c0;
    } else hl_fit0(&w, o, R, rev);
    return 1;
}
/* FUN_1801d8b30 (x11 l.396-481) on the fitted arc: 1 = kept, 0 = the new top rises or the arc's refinement rises (FUN_1801cc840's rise branch
   replaces the arc centre through the connect's virtual +0x18, c3_lc_lead_r, so FUN_1801d91e0 finds no arc node between the three nodes: false) */
static int hl_check(C3Link *lk, const HlFit *o, int rev) {
    const double *top = rev ? o->p8 : o->p6, *pt = rev ? o->p6 : o->p8;
    double t[3] = { top[0], top[1], top[2] };
    if (c3_lc_drop(lk, t)) return 0;
    int r = rev ? c3_lc_arc(lk, pt, o->p7, top, 0) : c3_lc_arc(lk, top, o->p7, pt, 0);
    return r == 0;
}
/* one side's rising lead after FUN_1801d7450 (T rose or the arc's refinement rose): FUN_1801d76a0. q = the pass points (hl_pnodes), R = |C - T|,
   d = |q[0] - T|xy (both before T's drop, x8 l.598-616), att = param_7. 1 = a trim (*t = v: FUN_1801d8240 or FUN_1801d7b00 succeeded), 2 = the
   vertical lead FUN_1801d8630, 0 = NOT DECODED. With a trim, *s gets what the connect keeps for the pass trimming FUN_1801a47b0 (x11 l.80-110,
   x8 l.193-206): the fit (its arc nodes: FindBetterArc leaves [T', C', Q] in the connect, FUN_1801d8ce0; FUN_1801d7b00 the one node Q = its
   pass-side point, x8 l.226-290), c0 (approach +0x220 / exit +0x228, FUN_180171b50's param_1[1]), a0 (approach +0x1f8 / exit +0x1f0: the
   bisection's overlap - FUN_180171b50's e8 (param_11); 0 after FUN_1801d7b00) and the closed-pass count (+0x234 / +0x230, FUN_1801d7b00 only) */
typedef struct { HlFit o; double a0; int one; } HlSide;
static int hl_rise(C3Link *lk, const double *q, int n, double R, double d, int rev, int att, double *t, HlSide *s) {
    int dbg = getenv("MCAM_LK_DEBUG") != NULL;
    *t = 0.0;
    if (att) {
        /* FUN_1801d8240 */
        double dv2 = d * 0.5, dv3 = dv2; int it = 0;
        do {
            if (dv3 < 0.0 || (0.0 < dv3 && dv3 < lk->r200)) break;
            double dv1;
            if (d <= dv3) dv1 = dv3 - dv2 * 0.5;
            else {
                HlFit o; if (!hl_fit(q, n, R, rev, dv3, &o)) return 0;
                lk->nfit++;
                if (dbg) fprintf(stderr, "la fit ov %.17g v %.17g c0 %.17g p6 (%.17g %.17g %.17g) p7 (%.17g %.17g %.17g) p8 (%.17g %.17g %.17g)\n", dv3, o.v, o.c0,
                                 o.p6[0], o.p6[1], o.p6[2], o.p7[0], o.p7[1], o.p7[2], o.p8[0], o.p8[1], o.p8[2]);
                if (o.v == 0.0) break;
                int ok = 1;
                if (dv3 != 0.0) ok = hl_check(lk, &o, rev);
                if (dbg) fprintf(stderr, "la check %d\n", ok);
                if (!ok) dv1 = dv3 - dv2 * 0.5;
                else {
                    dv1 = dv3 + dv2 * 0.5;
                    if (fabs(dv3 - dv1) < lk->r200) {
                        if (it < 10000) { *t = o.v; if (s) { s->o = o; s->a0 = dv3 - o.e8; s->one = 0; } return 1; }
                        break;
                    }
                }
            }
            dv2 = dv2 * 0.5; it++; dv3 = dv1;
        } while (it < 10000);
        /* FUN_1801d7b00 (the frag's +0x78 is 0: FUN_180171b50 with overlap 0) */
        if (R != 0.0) {
            HlFit o; if (!hl_fit(q, n, R, rev, 0.0, &o)) return 0;
            lk->nfit++;
            if (dbg) fprintf(stderr, "la trim v %.17g c0 %.17g cnt %d\n", o.v, o.c0, o.cnt);
            if (o.v != 0.0) {
                if (o.cnt != 0) return 0;                                     /* a closed pass walked round (+0x230 / +0x234): NOT DECODED */
                *t = o.v; if (s) { s->o = o; s->a0 = 0.0; s->one = 1; } return 1;
            }
        }
    }
    return 2;                                                                 /* FUN_1801d8630 */
}
/* The pass trimming of a lead whose trim fits (formats/hst_linker.md 30). FUN_1801a44a0 (hk l.13030-13222): the pass from the position moved
   by the trim (FUN_180279f90 -> FUN_18027ca10 on line segments: the 3D length, x12 l.84ff) - from the start forwards for the approach (param_5 =
   1), from the end backwards for the exit - to its other end (FUN_1802829c0). The point at the 3D length s along the pass (clamped): */
static void hl_at3(const double *p, int m, double s, double *o, int *seg) {
    for (int k = 0; k + 1 < m; k++) {
        const double *a = p + 3 * k, *b = a + 3;
        double L = sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
        if (s <= L || k + 2 == m) {
            double f = L > 0.0 ? s / L : 0.0; if (f > 1.0) f = 1.0; if (f < 0.0) f = 0.0;
            for (int i = 0; i < 3; i++) o[i] = a[i] + f * (b[i] - a[i]);
            *seg = k; return;
        }
        s -= L;
    }
    memcpy(o, p, sizeof(double) * 3); *seg = 0;
}
static double hl_len3(const double *p, int m) {
    double L = 0.0;
    for (int k = 0; k + 1 < m; k++) L += sqrt((p[3*k+3] - p[3*k]) * (p[3*k+3] - p[3*k]) + (p[3*k+4] - p[3*k+1]) * (p[3*k+4] - p[3*k+1]) + (p[3*k+5] - p[3*k+2]) * (p[3*k+5] - p[3*k+2]));
    return L;
}
/* FUN_18011a840 (an arc move from the writer's last point to B about C, radius R; written as lines by mccore, c3_lk_arc's rule) */
static void hl_arc_to(const C3Link *lk, MB *b, const double *C, const double *B, double R, double feed) {
    V3 l = lastp(b); double A[3] = { l.x, l.y, l.z };
    double u[3] = { A[0] - C[0], A[1] - C[1], A[2] - C[2] }, nu = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    double w[3] = { B[0] - C[0], B[1] - C[1], B[2] - C[2] };
    if (!(nu > 0.0)) { mv(b, 0, B[0], B[1], B[2], feed); return; }
    for (int i = 0; i < 3; i++) u[i] /= nu;
    double cu = w[0] * u[0] + w[1] * u[1] + w[2] * u[2];
    for (int i = 0; i < 3; i++) w[i] -= cu * u[i];
    double nw = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (!(nw > 1e-12 * R)) { mv(b, 0, B[0], B[1], B[2], feed); return; }
    double sw = atan2(nw, cu);
    for (int i = 0; i < 3; i++) w[i] /= nw;
    c3_lk_arc(lk, b, C, u, w, R, sw, B, feed);
}
/* FUN_1801a47b0 (hk l.13223-13508) -> FUN_180170f00 (hc l.2021-2169) -> FUN_1801713f0 (hc l.2170-2390) for line segments: the lead's arc
   continued along the trimmed-off part of the pass, between the positions a and b (approach: a = the start moved by c0, b = the start moved by v;
   exit: a = the end moved back by v, b = the end moved back by c0; FUN_180279f90). Approach: from the point (a.xy, the connect's last node z = Q.z)
   with acc = a0; exit: from a with acc = the XY length a -> b (FUN_180279ea0 = FUN_18027c660(.., 1)) + a0. Per segment e0 -> e1 (FUN_18027f780's
   iterator from a to b; a zero XY length is skipped): t = R - acc, the centre C = p + dir t (dir = the segment's unit XY direction, negated for
   the exit) at p.z + sqrt(R^2 - t^2); acc += |e1 - e0|xy (exit: -=); the end q = (e1.xy, C.z - sqrt(R^2 - (R - acc)^2)); an arc p -> q about C
   (FUN_18011a840) when FUN_180280c80 (dis 180280c80-180280cd6) is 1: on the iterator's last segment (+0x198 = +0x1c8, +0x190 = 1 or +0x178 = 0)
   always, before it only when the iterator's +0x28 is set and FUN_1802810a0 finds its +0x30 / +0x38 both 0 (dis 1802810a0-1802810e9) - which
   array those are is NOT DECODED; on every traced Raster trimming (e1 4, e2 50 FUN_1801713f0 calls, up to 3 segments) it was 0 before the
   last segment: one arc, written there (ret 18017199e), with the last segment's centre. Before it an arc to C - R z when q and the writer's last
   point lie opposite on the circle (dot / R^2 <= -0.998); p = q. An end left unwritten (a last segment of XY length 0) is written at the end. The angle bookkeeping (FUN_180060f10 / FUN_180060570, param_8) writes nothing.
   The count (+0x230 / +0x234) of closed passes walked round is 0 here (hl_rise refuses otherwise). 0 = NOT DECODED (an arc segment). */
static int hl_trim_arcs(const C3Link *lk, MB *b, const double *xyz, int m, int ex, double R, double v, double c0, double a0, double qz, double feed) {
    double L3 = hl_len3(xyz, m), A[3], B[3]; int ka, kb;
    v = fabs(v);
    if (ex) { hl_at3(xyz, m, L3 - v, A, &ka); hl_at3(xyz, m, L3 - c0, B, &kb); }
    else { hl_at3(xyz, m, c0, A, &ka); hl_at3(xyz, m, v, B, &kb); }
    if (kb < ka) return 0;
    double p[3] = { A[0], A[1], ex ? A[2] : qz }, acc = a0;
    if (ex) {                                                                 /* FUN_180279ea0: the XY length a -> b */
        double s = 0.0, *pt = A;
        for (int k = ka; k <= kb; k++) { const double *e1 = k == kb ? B : xyz + 3 * (k + 1); s += hypot(e1[0] - pt[0], e1[1] - pt[1]); pt = (double *)e1; }
        acc = s + a0;
    }
    { V3 l = lastp(b); if (l.x != p[0] || l.y != p[1] || l.z != p[2]) mv(b, 0, p[0], p[1], p[2], feed); }   /* FUN_180118ee0 code 0x19 */
    const double *e0 = A; double C[3] = { p[0], p[1], p[2] }, wr[3] = { p[0], p[1], p[2] };
    for (int k = ka; k <= kb; k++) {
        const double *e1 = k == kb ? B : xyz + 3 * (k + 1);
        double dx = e1[0] - e0[0], dy = e1[1] - e0[1], L = sqrt(dx * dx + dy * dy);
        if (L == 0.0) { e0 = e1; continue; }
        double t = R - acc, hh = R * R - t * t; hh = hh <= 0.0 ? 0.0 : sqrt(hh);
        double n = sqrt(dx * dx + dy * dy); if (n == 0.0) n = 1.0;
        double ux = 1.0 / n * dx, uy = 1.0 / n * dy; if (ex) { ux = -ux; uy = -uy; }
        C[0] = ux * t + p[0]; C[1] = uy * t + p[1]; C[2] = hh + p[2];
        double acc1 = ex ? acc - L : L + acc, w = R * R - (R - acc1) * (R - acc1); w = w <= 0.0 ? 0.0 : sqrt(w);
        double q[3] = { e1[0], e1[1], C[2] - w };
        if (k == kb) {                                                        /* FUN_180280c80: the iterator's last segment */
            V3 l = lastp(b);
            if (((q[1] - C[1]) * (l.y - C[1]) + (q[0] - C[0]) * (l.x - C[0]) + (q[2] - C[2]) * (l.z - C[2])) / (R * R) <= -0.998) {
                double bot[3] = { C[0], C[1], C[2] - R }; hl_arc_to(lk, b, C, bot, R, feed);
            }
            if (getenv("MCAM_LK_DEBUG")) fprintf(stderr, "la arc end (%.17g %.17g %.17g) centre (%.17g %.17g %.17g)\n", q[0], q[1], q[2], C[0], C[1], C[2]);
            hl_arc_to(lk, b, C, q, R, feed); memcpy(wr, q, sizeof wr);
        }
        acc = acc1; memcpy(p, q, sizeof p); e0 = e1;
    }
    if (wr[0] != p[0] || wr[1] != p[1] || wr[2] != p[2]) {                    /* hc l.2368-2384: the end not written yet (a last segment of XY length 0) */
        if (getenv("MCAM_LK_DEBUG")) fprintf(stderr, "la arc end (%.17g %.17g %.17g) centre (%.17g %.17g %.17g)\n", p[0], p[1], p[2], C[0], C[1], C[2]);
        hl_arc_to(lk, b, C, p, R, feed);
    }
    return 1;
}

/* The wander of a mode-1 connect (the connect's +0xa0 set: the stay-down connects, FUN_1801d1e70 arg 13 = 1, formats/hst_linker.md 10.1):
 * FUN_1801d2bd0 -> FUN_1801deaf0 (x3 l.1065-1205: 1 = nothing to do for a two-node path; for a line path of three or more nodes its
 * FUN_1801cfd50 arc tests fail and it returns 0) -> FUN_1801dde40 (x11 l.3028-3201) from the first node to the list's end: it walks the node
 * triples (a, b, c); FUN_1801e07f0 (x16 l.223-411, line nodes) is 1 when b lies above the line a-c at b (Z interpolated by the XY distances;
 * 0 when a and c coincide in XY): then the triple moves on, skipping removed nodes (0x80). Else FUN_1801de920 (x16 l.412-492): b without
 * flag 0x40 is marked removed (the connect's node virtual +0x20 with code 9 sets 0x80, traced) and the triple steps back (b = a, a = the
 * previous node not removed) - or, when a is the first node, forward (b = c, c = the next). FUN_1801de1f0 (x16 l.591-680) then drops the
 * nodes marked 0x80 without 0x40. So the link keeps the nodes that are not below the line of their neighbours. The arc branches
 * (FUN_1801de3a0) need arc nodes (not ported). One implementation for both linkers: Waterline's closed-pass ramp (all nodes 0x40, wl_wander).
 * f: the nodes' flags (0x40 on the connect's two end nodes, 0 on the nodes the reduce put in, traced on the ridge cases). Returns the new
 * node count (p compacted). Shared: Waterline's linker reaches the same FUN_1801dde40. */
static int hl_e07f0(const double *a, const double *b, const double *c) {
    double gy = c[1] - a[1], gx = c[0] - a[0];
    if (sqrt(gy * gy + gx * gx) == 0.0) return 0;
    double ab = sqrt((b[1] - a[1]) * (b[1] - a[1]) + (b[0] - a[0]) * (b[0] - a[0]));
    double cy = c[1] - b[1], cx = c[0] - b[0], bc = sqrt(cy * cy + cx * cx), z = c[2];
    if (bc != 0.0) {
        z = a[2];
        if (ab != 0.0) { double w = ab / (bc + ab); if (a[2] != c[2]) z = (1.0 - w) * a[2] + c[2] * w; }
    }
    return 0.0 < b[2] - z;
}
static int hl_wander(double *p, int n, const unsigned char *f0) {
    if (n <= 2) return n;                                                     /* FUN_1801deaf0: a two-node path is left alone */
    unsigned char *f = malloc((size_t)n); memcpy(f, f0, (size_t)n);
    #define HL_NX(i) ({ int j_ = (i) + 1; while (j_ < n && (f[j_] & 0x80)) j_++; j_; })
    int a = 0, b = 1, c = 2;
    while (c != n) {
        int up = hl_e07f0(p + 3 * a, p + 3 * b, p + 3 * c);
        /* FUN_1801de920 with b flagged 0x40: z_i <= b.z -> FUN_1801ce930, read by the Waterline port as the next triple (formats/hst_linker.md,
           Waterline closed ramp); a mode-1 path has no 0x40 node inside */
        if (!up && (f[b] & 0x40)) { double ab = sqrt((p[3*b+1] - p[3*a+1]) * (p[3*b+1] - p[3*a+1]) + (p[3*b] - p[3*a]) * (p[3*b] - p[3*a]));
            double cy = p[3*c+1] - p[3*b+1], cx = p[3*c] - p[3*b], bc = sqrt(cy * cy + cx * cx), z = p[3*c+2];
            if (bc != 0.0) { z = p[3*a+2]; if (ab != 0.0) { double w = ab / (bc + ab); if (p[3*a+2] != p[3*c+2]) z = (1.0 - w) * p[3*a+2] + p[3*c+2] * w; } }
            up = z <= p[3*b+2]; }
        if (up) { a = HL_NX(a); b = a < n ? HL_NX(a) : n; c = b < n ? HL_NX(b) : n; continue; }
        f[b] |= 0x80;                                                         /* the node virtual +0x20, code 9 */
        if (a == 0) { b = c; c = c + 1 < n ? c + 1 : n; }                    /* (the next node: arcs skipped, removed ones not) */
        else { b = a; a--; while ((f[a] & 0x80) && a != 0) a--; }
    }
    #undef HL_NX
    /* FUN_1801de1f0: a node flagged 0x80 without 0x40 is dropped; a run flagged 0x80 with 0x40 goes back onto the line between its kept neighbours
       (FUN_1801e0d70, x13 l.1-88: the Z at the run node's XY length fraction, raised only - the Waterline port's reading) */
    for (int i = 1; i < n; i++) {
        if (!(f[i] & 0x80) || !(f[i] & 0x40)) continue;
        int A = i - 1, B = i; while (B < n && (f[B] & 0x80)) B++;
        if (B >= n) break;
        double L = 0, acc = 0;
        for (int k = A; k < B; k++) L += sqrt((p[3 * k + 3] - p[3 * k]) * (p[3 * k + 3] - p[3 * k]) + (p[3 * k + 4] - p[3 * k + 1]) * (p[3 * k + 4] - p[3 * k + 1]));
        for (int k = i; k < B; k++) {
            acc += sqrt((p[3 * k] - p[3 * k - 3]) * (p[3 * k] - p[3 * k - 3]) + (p[3 * k + 1] - p[3 * k - 2]) * (p[3 * k + 1] - p[3 * k - 2]));
            double w = (1.0 / L) * acc, z = p[3 * A + 2];
            if (z != p[3 * B + 2]) z = (1.0 - w) * z + p[3 * B + 2] * w;
            if (z > p[3 * k + 2]) p[3 * k + 2] = z;
        }
        i = B;
    }
    int m = 0;
    for (int i = 0; i < n; i++) if (!((f[i] & 0x80) && !(f[i] & 0x40))) { if (m != i) memcpy(p + 3 * m, p + 3 * i, sizeof(double) * 3); m++; }
    free(f);
    return m;
}
