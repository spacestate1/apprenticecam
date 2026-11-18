/* cam_smooth.c - Mastercam's toolpath Smoothing (the "Smoothing Settings" group of the Arc Filter / Tolerance page mccoreres 10318,
 * ArcFilterRefinePage, controls 10382 / 10386 / 10397-10401). Included by cam2d.c after cam_filter.c. Decode notes, citations and
 * the NOT DECODED list: formats/contour_smoothing.md.
 *
 * Where Mastercam runs it: BNCISection::PostProcess (decomp/mccore_filter.c l.3588-3601), after the Line/Arc filter: record 0xca8 on ->
 * bnci_point_distribution_op 0x180087040 (not op_is_drill) -> bnci_point_distribution 0x1809450f0 (~/mastercam-re/work/ctour100/
 * mc_ptdist.c l.98-276; mccore.dll): no "bfilter9" special app is shipped (load_special_app fails, the built-in path runs), srand(0x1786),
 * the op's NCI section re-read record by record through the collector FUN_180945d50 (mc_ptdist.c l.1025-1384), which keeps a node list
 * (the Line/Arc filter's: FUN_180944290 append, 0xe8-byte nodes from a calloc'ed pool FUN_18094b5f0 / FUN_180944210) and flushes it
 * with FUN_18094adc0 (mc_ptdist.c l.6123-6162): PointDistribution_3D FUN_18093bcf0 (CFilter.cpp; ~/mastercam-re/work/ctour100/
 * mc_ptdist2.c l.3-740, the stack arguments Ghidra drops read from the disassembly of mccore.dll 0x18093bcf0-0x18093d107) with
 * (record 0xc98 tolerance, 0xca9 fixed segment length, 0xcaa shift points randomly, 0xcab minimize number of points, 0xcac present
 * arcs as line segments, 0xca0 segment length), then every node but the list's first written by FUN_18094ce10 (lines ct_lin_mill,
 * arcs ct_arc_mill). Helpers decompiled 2026-11-08 into ~/mastercam-re/work/ctsmooth/pd_more.c / pd_more2.c (FUN_180947050,
 * FUN_18094a170, FUN_18093e0f0, FUN_180940b40, FUN_18094ce10, FUN_180944290, FUN_18094b530, FUN_180944210).
 *
 * Ours: the toolpath is this program's Move list. Lines are line records, the chords of one engine arc (Move.arc, XY) one arc record;
 * the records between moves that the collector reacts to are known here only for Contour's passes (gen_contour: the contour marker
 * -96 bnci_wr_ctour_marker written before every pass writer FUN_180056be0, decomp/mcmill_ctour.c l.1750 / 2343; cam2d.c ps_mark),
 * and the stop_cc record 1004 after a move with cc 140 (nci_write.c) - other op types / Ramp / Oscillate / Tab Cutoff and an op that
 * also went through the Line/Arc filter are refused (formats/contour_smoothing.md). */
#include <limits.h>

typedef struct SmNode {
    int type;                 /* node +0x00: 0 rapid / list start, 1 line, 2 CW arc, 3 CCW arc */
    int cc;                   /* +0x02 */
    double p[3];              /* +0x08: end point */
    double tg[3];             /* +0x20: the vertex tangent phase 1 leaves (with mk = -1) */
    double c[3];              /* +0x70: arc centre (x, y, depth); phase 1: the line end's original point */
    double feed;              /* +0x88 */
    unsigned char brk;        /* +0x94: set by phase 1 (0xff), not read by the writer */
    char keep;                /* +0xb5: NCI arc +0x5a (full circle) */
    int mk;                   /* +0xc0: -1 = tg holds the vertex tangent */
    int k0, k1;               /* ours: an arc node's chords in the input moves */
    const PsArc *sc;          /* ours: the arc recorded for them (a lead arc, cam2d.c ps_arc_rec), NULL for an engine arc */
    struct SmNode *prev, *next;   /* +0xd8 / +0xe0 */
} SmNode;

typedef struct {
    double T, seg; int fixed, shift, minim, arcl;   /* record 0xc98 / 0xca0 / 0xca9 / 0xcaa / 0xcab / 0xcac */
    SmNode *head, *tail;
    SmNode **pool; int npool, cpool;
    unsigned rs;              /* the CRT rand() state (srand(0x1786) per op) */
    const Move *in; MB *out;
    double pnx, pny, pdep;    /* ct_lin_mill / ct_arc_mill: _prev_nc_pt / cur_depth (init_gv on the op's 1002: "far away") */
} PSm;

static SmNode *ps_new(PSm *S) {   /* FUN_18094b5f0: zeroed memory (FUN_180944210 calloc) */
    if (S->npool == S->cpool) { S->cpool = S->cpool ? S->cpool * 2 : 1024; S->pool = realloc(S->pool, S->cpool * sizeof *S->pool); }
    SmNode *n = calloc(1, sizeof *n); S->pool[S->npool++] = n; return n;
}
static void ps_free(PSm *S) { for (int i = 0; i < S->npool; i++) free(S->pool[i]); free(S->pool); S->pool = NULL; S->npool = S->cpool = 0; }
static int ps_rand(PSm *S) { S->rs = S->rs * 214013u + 2531011u; return (int)((S->rs >> 16) & 0x7fff); }   /* MSVC CRT rand() */
static double ps_len(const double *a) { return sqrt(a[1] * a[1] + a[0] * a[0] + a[2] * a[2]); }
static double ps_dist(const double *a, const double *b) { double d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; return ps_len(d); }
static int ps_i32(double q) { return q >= -2147483648.0 && q < 2147483648.0 ? (int)q : INT_MIN; }   /* cvttsd2si r32 (out of range: 0x80000000) */
static double ps_acos(double c) { if (!(c < 1.0)) c = 1.0; else if (c <= -1.0) c = -1.0; return acos(c); }   /* the clamp of every acos here */
static void ps_append(PSm *S, SmNode *n) {   /* FUN_180944290 */
    n->next = NULL; n->prev = S->tail;
    if (!S->head || !S->tail) S->head = n; else S->tail->next = n;
    S->tail = n;
}
static void ps_insert_before(SmNode *at, SmNode *n) {   /* FUN_18094b530 insert_nci_pt_before */
    SmNode *pv = at->prev; at->prev = n; n->next = at; n->prev = pv; if (pv) pv->next = n;
}

/* FUN_1809415c0: the height of the apex over a base of length L with the base angles a, b */
static double ps_apex(double L, double a, double b) {
    if (fabs(a) >= 1e-06 && fabs(b) >= 1e-06) {
        double tb = tan(b), ta = tan(a), x = tb * L / (ta + tb);
        return x <= L - x ? tb * (L - x) : ta * x;
    }
    return 0.0;
}

/* FUN_18094a170 PointDistribution_3D::fit_line: how far the nodes strictly between a and b are from the line a -> b */
static double ps_fit_line(const SmNode *a, const SmNode *b) {
    if (!a || !b || !((b->type & ~3) != 0 || b->type == 1)) return 1000000.0;
    if (a->next == b) return 0.0;
    if (b->prev->feed != b->feed) return 1000000.0;
    double u[3] = { b->p[0] - a->p[0], b->p[1] - a->p[1], b->p[2] - a->p[2] }, L = ps_len(u), worst = 0.0;
    if (L == 0.0) u[0] = u[1] = u[2] = 0.0; else { u[0] /= L; u[1] /= L; u[2] /= L; }
    for (const SmNode *q = a->next; q && q != b; q = q->next) {   /* CFilter.cpp 0xf6b asserts q is a line */
        double v[3] = { q->p[0] - a->p[0], q->p[1] - a->p[1], q->p[2] - a->p[2] };
        double da = ps_len(v), db = ps_dist(q->p, b->p), m = da <= db ? db : da;
        double over = m <= L ? 0.0 : (m - L) * 2.0;
        double x[3] = { v[1] * u[2] - v[2] * u[1], v[2] * u[0] - u[2] * v[0], u[1] * v[0] - v[1] * u[0] };
        double d = ps_len(x); if (d <= over) d = over;
        if (worst < d) worst = d;
    }
    return worst;
}

/* the random step back of ExtractLinesUsingToleranceAndMaxCount (CFilter.cpp 0x1042-0x1052 / 0x1089-0x1099) */
static SmNode *ps_back(PSm *S, SmNode *nx, const SmNode *anchor, int *cnt) {
    (*cnt)--;
    double f = (double)ps_rand(S) / 32767.0;
    int k = (int)((double)*cnt * f); if (k < 0) k = 0; if (*cnt < k) k = *cnt;
    SmNode *p = nx, *q;
    do { q = p; if (k == 0) break; k--; p = q->prev; } while (q->prev != anchor);
    return q;
}
/* FUN_18093eda0 PointDistribution_3D::ExtractLinesUsingToleranceAndMaxCount: drop the line ends a straight run keeps within tol */
static void ps_extract(PSm *S, SmNode *head, SmNode *tail, double tol, int maxcnt, int shift) {
    if (head->next == tail) return;
    int cnt = 0; SmNode *cur = head, *anchor = head;
    while (cur) {
        SmNode *nx = cur->next;
        if (!nx) { if (cnt > 0) { anchor->next = cur; cur->prev = anchor; } break; }
        int k = cnt + 1;
        if (k == 1) { if (nx->type != 1) { anchor = nx; k = 0; } }
        else if (ps_fit_line(anchor, nx) <= tol) {
            if (0 < maxcnt && maxcnt < k) {
                if (shift) { cnt = k - 1; nx = ps_back(S, nx, anchor, &cnt); }
                anchor->next = nx; nx->prev = anchor; anchor = nx; k = 0;
            }
        } else {
            if (shift && 5 < k) { cnt = k - 1; nx = ps_back(S, nx, anchor, &cnt); }
            int t = nx->type;
            anchor->next = nx->prev; nx->prev->prev = anchor;
            if (t == 1) { nx = nx->prev; anchor = nx; } else anchor = nx;
            k = 0;
        }
        cnt = k; cur = nx;
        if (!anchor) break;
    }
}
/* FUN_180942380 PointDistribution_3D::RemoveClosePoints */
static void ps_remove_close(SmNode *head, double thr, double len) {
    for (SmNode *n = head->next; n; n = n->next) {
        SmNode *nx = n->next;
        if (n->type != 1 || !nx || nx->type != 1) continue;
        const SmNode *pv = n->prev;
        double d02[3] = { nx->p[0] - pv->p[0], nx->p[1] - pv->p[1], nx->p[2] - pv->p[2] };
        double d01[3] = { n->p[0] - pv->p[0], n->p[1] - pv->p[1], n->p[2] - pv->p[2] };
        double d12[3] = { nx->p[0] - n->p[0], nx->p[1] - n->p[1], nx->p[2] - n->p[2] };
        double L02 = ps_len(d02), L12 = ps_len(d12), L01 = ps_len(d01);
        double a0 = ps_acos((d02[1] * d01[1] + d02[0] * d01[0] + d02[2] * d01[2]) / (L01 * L02));
        double a1 = ps_acos((d02[1] * d12[1] + d02[0] * d12[0] + d02[2] * d12[2]) / (L12 * L02));
        double h = ps_apex(L02, a0, a1);
        if (h < thr && (len < L02 || len < L12)) { n->prev->next = nx; nx->prev = n->prev; n = nx; }
    }
}

/* FUN_180942110: the Bezier curve of the cnt control points cp at i / n, i = 1 .. n - 1 (de Casteljau), each point through the
   callback FUN_180947050: a line node before `at` (its cc, feed); the end (i = n) makes `at` a line (it is one here) */
static void ps_bezier(PSm *S, int n, int cnt, const double (*cp)[3], SmNode *at) {
    int nn = n < 1 ? 1 : n;
    for (int i = 1; i < nn; i++) {
        double t = (double)i * (1.0 / (double)nn), w[4][3];
        memcpy(w, cp, sizeof(double) * 3 * cnt);
        for (int m = cnt; m > 1; m--)
            for (int j = 0; j < m - 1; j++) for (int c = 0; c < 3; c++) w[j][c] = (w[j + 1][c] - w[j][c]) * t + w[j][c];
        SmNode *q = ps_new(S); q->type = 1; q->cc = at->cc; memcpy(q->p, w[0], sizeof q->p); q->feed = at->feed;
        ps_insert_before(at, q);
    }
    at->type = 1;
}

/* FUN_180947160 create_arc_3d_from_Gcode_center_start_end_plane (XY, view 1) + FUN_1809468c0 break_arc_3d_to_nsegs with the
   callback FUN_180947050: the arc node `n` (from its previous node's point) as lines; 0 = the arc is not made (CFilter.cpp 0x1408) */
static int ps_break_arc(PSm *S, SmNode *n, double T, int fixed, double maxlen) {
    const double *s = n->prev->p, *e = n->p; int cw = n->type == 2;
    double r, sa, sw;
    if (n->keep) {                                    /* constr_circle 0x1804b5ad0 */
        r = hypot(s[0] - n->c[0], s[1] - n->c[1]);
        if (!(g_nc_tol <= r)) return 0;
        sw = cw ? -2 * M_PI : 2 * M_PI; sa = ff_ang(s[0] - n->c[0], s[1] - n->c[1]);
    } else {                                          /* constr_ns_arc 0x1804b5c00 (constr_arc_new 0x1804b5830), nc_tol */
        FA2 a; if (!ff_arc_c(s, e, n->c, cw, g_nc_tol, &a)) return 0;
        r = a.r; sa = a.sa; sw = a.sw;
    }
    /* FUN_180940ca0 (the chord count for the tolerance, FUN_180940b40 = the chord's angle) / FUN_180940c50 (for the segment length) */
    int k1 = 1, k2;
    if (CAM_SYS_TOL <= r && T <= r) { double ang = asin(sqrt(r * r - (r - T) * (r - T)) / r) * 2.0, q = ceil(fabs(sw) / fabs(ang));
        if ((short)ps_i32(q) > 0) k1 = (short)ps_i32(q); }
    int ns = k1;
    if (fixed) { k2 = 1; if (CAM_SYS_TOL <= r) { double q = ceil(fabs(sw / (maxlen / r))); if ((short)ps_i32(q) > 0) k2 = (short)ps_i32(q); } if (ns < k2) ns = k2; }
    if (ns < 1) ns = 1;
    double z0 = s[2], dz = fabs(e[2] - s[2]) >= 1e-07 ? (e[2] - s[2]) / ns : 0.0, a = sa;
    for (int i = 1; i <= ns; i++) {
        a += sw / ns;
        if (i == ns) break;
        SmNode *q = ps_new(S); q->type = 1; q->cc = n->cc; q->feed = n->feed;
        q->p[0] = cos(a) * r + n->c[0]; q->p[1] = sin(a) * r + n->c[1]; q->p[2] = z0 + dz * i;
        ps_insert_before(n, q);
    }
    n->type = 1;
    return 1;
}

/* FUN_18093bcf0 PointDistribution_3D::PointDistribution_3D over the list head .. tail (head = the run's start point) */
static void ps_distribute(PSm *S, SmNode *head, SmNode *tail) {
    double T = S->T; int fixed = S->fixed, shift = S->shift, minim = S->minim;
    if (fixed) { minim = 0; shift = 0; }                         /* local_458 / param_6 */
    if (!head || head == tail) return;
    double t1 = T * 0.1, t2 = 0.0;
    if (!fixed) t2 = (T - t1) * (shift ? 0.1 : 0.075);
    double t3 = ((T - t2) - t1) * 0.7, t4 = (T - (t3 + t1 + t2)) * 0.125;
    double maxlen = fixed ? S->seg : T * 10.0;
    /* 1: eight passes over the line - line vertices: a vertex turning by at most 9 degrees moves towards its chord (at most t4 a pass);
       the eighth pass leaves the vertex tangent (+0x20, mark +0xc0 = -1) */
    for (int pass = 1; pass <= 8; pass++)
        for (SmNode *n = head->next; n; n = n->next) {
            SmNode *nx = n->next;
            if (n->type != 1 || !nx || nx->type != 1) continue;
            const double *P0 = n->prev->p; double P1[3] = { n->p[0], n->p[1], n->p[2] }; const double *P2 = nx->p;
            double d01[3] = { P1[0] - P0[0], P1[1] - P0[1], P1[2] - P0[2] }, d12[3] = { P2[0] - P1[0], P2[1] - P1[1], P2[2] - P1[2] };
            double d02[3] = { P2[0] - P0[0], P2[1] - P0[1], P2[2] - P0[2] }, L02 = ps_len(d02);
            if (!(L02 <= T * 50.0 && 1e-07 <= L02)) continue;
            double L01 = ps_len(d01), L12 = ps_len(d12);
            double ang = ps_acos((d12[1] * d01[1] + d12[0] * d01[0] + d12[2] * d01[2]) / (L12 * L01));
            if (!(fabs(ang) <= 0.15707963267948966)) continue;
            double f = 1.0 - 1.0 / (L01 / L12 + 1.0), Q[3] = { P0[0] + d02[0] * f, P0[1] + d02[1] * f, P0[2] + d02[2] * f };
            double dq[3] = { Q[0] - P1[0], Q[1] - P1[1], Q[2] - P1[2] }, m = ps_len(dq);
            if (t4 < m) { double k = t4 / m; for (int c = 0; c < 3; c++) Q[c] = dq[c] * k + P1[c]; }
            n->brk = 0xff;
            if (pass == 1) memcpy(n->c, P1, sizeof n->c);
            else if (pass == 8) {
                if (ps_dist(n->c, P1) <= 1e-07) n->brk = 0;
                double g = 1.0 - 1.0 / (L12 / L01 + 1.0);
                for (int c = 0; c < 3; c++) { double D = (P1[c] + d01[c]) - P2[c]; n->tg[c] = (D * g + P2[c]) - P1[c]; }
                n->mk = -1;
            }
            memcpy(n->p, Q, sizeof Q);
        }
    ps_extract(S, head, tail, t1, 0, 0);
    /* 3: each line as a Bezier curve (the vertex tangents at its ends), cut into ceil(L / maxlen) pieces (30 at most unless fixed) */
    for (SmNode *n = head->next; n; n = n->next) {
        if (n->type != 1) continue;
        const SmNode *pv = n->prev; const double *A = pv->p, *B = n->p;
        double d[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, L = ps_len(d);
        int ns = (short)ps_i32(ceil(L / maxlen));          /* 0x18093c4f4: cvttsd2si, then 16-bit compares */
        if (ns < 1) ns = 1;
        if (!fixed && 30 < ns) ns = 30;
        else if (ns < 2) continue;
        double v0[3], v1[3], a0 = 0.0, a1 = 0.0;
        if (pv->mk == -1) { memcpy(v0, pv->tg, sizeof v0); a0 = ps_acos((v0[1] * d[1] + v0[0] * d[0] + v0[2] * d[2]) / (ps_len(v0) * L)); }
        else memcpy(v0, d, sizeof v0);
        if (n->mk == -1) { memcpy(v1, n->tg, sizeof v1); a1 = ps_acos((v1[1] * d[1] + v1[0] * d[0] + v1[2] * d[2]) / (ps_len(v1) * L)); }
        else memcpy(v1, d, sizeof v1);
        if (!(L <= T * 50.0)) a0 = a1 = 0.0;
        int b0 = 1e-05 < a0, b1 = 1e-05 < a1;           /* (1e-05 <= a && a != 1e-05), NaN false */
        double h;
        if (b0 && b1) h = ps_apex(L, a0, a1);
        else h = b0 == b1 ? 0.0 : t3;
        double cp[4][3]; int ncp;
        if (h <= t3 * 0.1 + t3) {
            if (h <= 1e-07) { memcpy(cp[0], A, sizeof cp[0]); memcpy(cp[1], B, sizeof cp[1]); ncp = 2; }
            else if (a0 <= a1) {                           /* from B back along its tangent */
                double s = h / sin(a1);
                if (!b0) { double c = cos(a1); if (L * 0.95 < c * s) s = c * L * 0.95; }
                double k = s / ps_len(v1), u[3] = { v1[0] * k, v1[1] * k, v1[2] * k }, Q[3] = { B[0] - u[0], B[1] - u[1], B[2] - u[2] };
                if (!b0) {                                 /* proj_pt_ln_3d on A - B: t below 0.6 pulls Q towards B */
                    double ab[3] = { d[0], d[1], d[2] }, LL = ps_len(ab);
                    if (CAM_SYS_TOL <= LL) {
                        double t = ((Q[1] - A[1]) * ab[1] + (Q[0] - A[0]) * ab[0] + (Q[2] - A[2]) * ab[2]) / (LL * LL);
                        if (t < 0.6) { double r = t / 0.6; for (int c = 0; c < 3; c++) Q[c] = -u[c] * r + B[c]; }
                    }
                }
                memcpy(cp[0], A, sizeof cp[0]); memcpy(cp[1], Q, sizeof cp[1]); memcpy(cp[2], B, sizeof cp[2]); ncp = 3;
            } else {                                       /* from A along its tangent */
                double s = h / sin(a0);
                if (!b1) { double c = cos(a0); if (L * 0.95 < s * c) s = L * 0.95 * c; }
                double k = s / ps_len(v0), u[3] = { v0[0] * k, v0[1] * k, v0[2] * k }, Q[3] = { u[0] + A[0], u[1] + A[1], u[2] + A[2] };
                if (!b1) {                                 /* t above 0.4 pulls Q towards A */
                    double ab[3] = { d[0], d[1], d[2] }, LL = ps_len(ab);
                    if (CAM_SYS_TOL <= LL) {
                        double t = ((Q[1] - A[1]) * ab[1] + (Q[0] - A[0]) * ab[0] + (Q[2] - A[2]) * ab[2]) / (LL * LL);
                        if (!(t <= 0.4)) { double r = 0.4 / t; for (int c = 0; c < 3; c++) Q[c] = u[c] * r + A[c]; }
                    }
                }
                memcpy(cp[0], A, sizeof cp[0]); memcpy(cp[1], Q, sizeof cp[1]); memcpy(cp[2], B, sizeof cp[2]); ncp = 3;
            }
        } else {                                           /* cubic: both tangents, t3 / sin along each */
            double k0 = t3 / sin(a0) / ps_len(v0), k1 = t3 / sin(a1) / ps_len(v1);
            memcpy(cp[0], A, sizeof cp[0]); memcpy(cp[3], B, sizeof cp[3]);
            for (int c = 0; c < 3; c++) { cp[1][c] = v0[c] * k0 + A[c]; cp[2][c] = -(v1[c] * k1) + B[c]; }
            ncp = 4;
        }
        ps_bezier(S, ns, ncp, (const double (*)[3])cp, n);
    }
    /* 4: drop what the curves keep within the rest of the tolerance (with Shift points randomly: at most 100 in a run, random breaks;
       + Minimize number of points: half the tolerance, then RemoveClosePoints) */
    if (!fixed) {
        if (shift && minim) { ps_extract(S, head, tail, t2 * 0.5, 100, 1); ps_remove_close(head, t2 * 0.5, T); }
        else if (shift) ps_extract(S, head, tail, t2, 100, 1);
        else ps_extract(S, head, tail, t2, 0, 0);
    }
    /* 5: Present arcs as line segments */
    if (S->arcl)
        for (SmNode *n = head->next; n; n = n->next)
            if ((n->type == 2 || n->type == 3) && n->prev && !ps_break_arc(S, n, T, fixed, maxlen)) return;   /* CFilter.cpp 0x1408 */
}

/* the input moves k0 .. k1 as they are (FUN_18094cd80 writes the record unchanged; ct_arc_mill an arc node); a recorded lead arc is
   tagged as the arc it is (mb_tag_arc: the post writes G2 / G3 with its centre - Mastercam's NCI has the arc record) */
static void ps_copy(PSm *S, int k0, int k1, const PsArc *sc) {
    int from = S->out->n;
    for (int j = k0; j <= k1; j++) { if (S->out->n == S->out->cap) { S->out->cap = S->out->cap ? S->out->cap * 2 : 1024; S->out->m = realloc(S->out->m, S->out->cap * sizeof(Move)); }
        S->out->m[S->out->n++] = S->in[j]; }
    if (sc) mb_tag_arc(S->out, from, sc->cx, sc->cy, sc->cw);
}
/* FUN_18094adc0 + FUN_18094ce10: distribute the list and write every node after its first */
static void ps_flush(PSm *S) {
    if (!S->head) return;
    ps_distribute(S, S->head, S->tail);
    for (SmNode *n = S->head->next; n; n = n->next) {
        if (n->type == 0 || n->type == 1) {                /* ct_lin_mill: a point within tol of the last one (XY and depth) is dropped */
            int moved = fabs(n->p[0] - S->pnx) > CAM_SYS_TOL || fabs(n->p[1] - S->pny) > CAM_SYS_TOL || fabs(n->p[2] - S->pdep) > CAM_SYS_TOL;
            if (!moved) continue;
            mv(S->out, n->type == 0, n->p[0], n->p[1], n->p[2], n->feed); S->out->m[S->out->n - 1].cc = n->cc;
            S->pnx = n->p[0]; S->pny = n->p[1]; S->pdep = n->p[2];
        } else {                                           /* ct_arc_mill: the arc as it came (its chords) */
            ps_copy(S, n->k0, n->k1, n->sc);
            S->pnx = n->p[0]; S->pny = n->p[1]; S->pdep = n->p[2];
        }
    }
    S->head = S->tail = NULL;
}

/* the collector FUN_180945d50 over the moves (g_ps_mk: the moves a contour marker -96 precedes; g_ps_arcs: the lead arcs' chords) */
enum { PS_ACTIVE = -1000, PS_RESET = -2000 };
static void ps_run_moves(PSm *S, const Move *m, int nm, MB *out) {
    S->in = m; S->out = out; S->head = S->tail = NULL;
    S->pnx = S->pny = 1e38; S->pdep = 1e38;                /* init_gv (the op's 1002): _prev_nc_pt / cur_depth far away */
    int st = PS_RESET; double sp[3] = { 0, 0, 0 }, sf = 0; int im = 0, ia = 0;   /* the op's 1002 record: (uVar6 - 1000) < 3 -> -2000 */
    for (int k = 0; k < nm; k++) {
        while (im < g_ps_nmk && g_ps_mk[im] < k) im++;
        if (im < g_ps_nmk && g_ps_mk[im] == k) ps_flush(S);   /* -96: LAB_1809461c4 - flush, the state kept */
        while (ia < g_ps_narc && g_ps_arcs[ia].k0 < k) ia++;
        const PsArc *sc = ia < g_ps_narc && g_ps_arcs[ia].k0 == k && k > 0 && g_ps_arcs[ia].k1 < nm ? &g_ps_arcs[ia] : NULL;
        int e = k, type;
        if (m[k].rapid) type = 0;
        else if (sc) { e = sc->k1; type = sc->cw ? 2 : 3; }
        else if (k > 0 && m[k].arc > 0 && m[k].apl == 0) { while (e + 1 < nm && m[e + 1].arc == m[k].arc && !m[e + 1].rapid) e++; type = m[k].cw ? 2 : 3; }
        else type = 1;
        if (type == 0 || type == 1) sc = NULL;
        double feed = type == 0 ? -2.0 : m[k].feed;         /* bnci_wr_lin: G0 feed -2 */
        const double ep[3] = { m[e].p.x, m[e].p.y, m[e].p.z };
        int cc = 0; for (int j = k; j <= e; j++) if (m[j].cc) cc = m[j].cc;
        if (type == 0) { st = 0; ps_flush(S); ps_copy(S, k, e, NULL); }
        else if (type == 1 && st != PS_RESET && fabs(ep[0] - sp[0]) <= CAM_SYS_TOL && fabs(ep[1] - sp[1]) <= CAM_SYS_TOL
                 && fabs(ep[2] - sp[2]) <= CAM_SYS_TOL) {   /* a repeated point is not in Mastercam's NCI (ct_lin_mill drops it when the
                                                               engine writes it, mc_grlin.c l.836-850): not a record here - the move kept
                                                               as it is between lists (ours: a zero-length move), dropped inside one */
            if (!S->head) ps_copy(S, k, e, NULL);
            k = e; continue;
        }
        else if (st == PS_RESET || (st != PS_ACTIVE && sf != feed)) { st = 0; ps_flush(S); ps_copy(S, k, e, sc); }
        else {
            SmNode *n = ps_new(S); n->type = type; n->cc = cc; memcpy(n->p, ep, sizeof n->p); n->feed = feed; n->k0 = k; n->k1 = e; n->sc = sc;
            if (type != 1) { n->c[0] = sc ? sc->cx : m[k].cx; n->c[1] = sc ? sc->cy : m[k].cy; n->c[2] = ep[2];
                n->keep = fabs(ep[0] - m[k - 1].p.x) < 1e-6 && fabs(ep[1] - m[k - 1].p.y) < 1e-6; }   /* NCI arc +0x5a (nci_write.c) */
            if (st != PS_ACTIVE || !S->head) {               /* the list's start: the state's point */
                SmNode *h = ps_new(S); h->type = 0; memcpy(h->p, sp, sizeof h->p); h->feed = sf; ps_append(S, h); st = PS_ACTIVE; }
            ps_append(S, n);
        }
        memcpy(sp, ep, sizeof sp); sf = feed;
        if (cc == 140) ps_flush(S);                         /* stop_cc's 1004 record after it: flush */
        k = e;
    }
    ps_flush(S);                                            /* the section's end (-3) */
}

static int ps_patch_off(int o) { return o >= 0xc98 && o < 0xcad; }
/* the smoothing fields: the stored op's record (or the defaults op) with the dialog's patches; 1 = Smoothing on (record 0xca8) */
static int ps_params(const McamFile *f, const CamParams *p, PSm *S) {
    uint8_t rec[0xd00]; memset(rec, 0, sizeof rec);
    const Op *fo = NULL;
    if (p->file_op_id > 0) for (int i = 0; i < f->nops; i++) if (f->ops[i].id == p->file_op_id && f->ops[i].raw) { fo = &f->ops[i]; break; }
    if (fo) memcpy(rec, fo->raw, fo->raw_n < sizeof rec ? fo->raw_n : sizeof rec);
    else { uint32_t dn = 0; const uint8_t *dr = cam_default_op_record(p->type, mcam_is_metric(f), &dn); if (dr) memcpy(rec, dr, dn < sizeof rec ? dn : sizeof rec); }
    for (int k = 0; k < p->nrpatch; k++) {
        int o = p->rpatch[k].off, n = p->rpatch[k].n;
        if (!p->rpatch[k].height && ps_patch_off(o) && o + n <= (int)sizeof rec) memcpy(rec + o, p->rpatch[k].v, n);
    }
    if (!rec[0xca8]) return 0;
    memcpy(&S->T, rec + 0xc98, 8); memcpy(&S->seg, rec + 0xca0, 8);
    S->fixed = rec[0xca9] != 0; S->shift = rec[0xcaa] != 0; S->minim = rec[0xcab] != 0; S->arcl = rec[0xcac] != 0;
    return 1;
}
/* 1 = smoothed, 0 = off, -1 = refused (op->error) */
static int cam_smooth(const McamFile *f, CamOp *op, MB *b) {
    if (op->p.type == CAM_DRILL) return 0;                  /* bnci_point_distribution_op: not op_is_drill */
    PSm S; memset(&S, 0, sizeof S);
    if (!ps_params(f, &op->p, &S)) return 0;
    if (op->p.type != CAM_CONTOUR) { snprintf(op->error, sizeof op->error, "Smoothing: ported for Contour only (the records between this operation's moves, where the point list is flushed, are not mapped)"); return -1; }
    if (!g_ps_map) { snprintf(op->error, sizeof op->error, "Smoothing: the contour markers of Ramp / Oscillate / Tab Cutoff passes are not mapped onto the moves here (not generated)"); return -1; }
    if (g_ps_arc) { snprintf(op->error, sizeof op->error, "Smoothing: the toolpath has arcs (chain arcs, rolled corners, corner rounding) that are chords here, which the distribution would move (not generated)"); return -1; }
    if (op->filtered) { snprintf(op->error, sizeof op->error, "Smoothing after the Line/Arc filter: the filter's output records are not kept here (not generated)"); return -1; }
    /* a zero tolerance / segment length is no special case: the counts come out of cvttsd2si as 0x80000000 -> 1 (ps_i32), as there */
    S.rs = 0x1786;                                          /* srand(0x1786) */
    MB o = { 0 };
    ps_run_moves(&S, b->m, b->n, &o);
    ps_free(&S);
    free(b->m); *b = o;
    return 1;
}
