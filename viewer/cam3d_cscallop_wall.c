/* cam3d_cscallop_wall.c - Constant Scallop (op 43) stage C: the vertical-gap connector of a line against the scanner's wall records
 * (formats/surf_classic3.md 12.22). FUN_180184cc0's vcs branch (l.261060-261140, asm 180184f62..1801850ff): at a vertical gap between a
 * node k and its next nx, FUN_18015bca0 (the connector split along the walls) or, when that gives nothing, FUN_1801c8080 (one connector
 * with the walls' normals). Sources: decomp/mcmsurf_all.c (l.N) and objdump of mcmsurf.dll (asm): the decompile loses arguments
 * (FUN_18015aff0, FUN_18014d900, FUN_180162300 calls) and some stores, so every function below is read from its asm.
 * The 0x68-byte records (walls, connectors) are CsNode {f0 +0, s0 +8, z0 +0x10, n0 +0x18, f1 +0x30, s1 +0x38, z1 +0x40, n1 +0x48, code +0x60};
 * the DLL's stack padding bytes of the copies (FUN_18014aef0's +1..+7, +0x31..+0x37, +0x62..+0x67) are not represented. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

/* FUN_18015aff0 (asm 18015aff0: movsx, cmp 0x11, setbe): code in 0 .. 0x11; FUN_18015a310 (asm 18015a310): code in 0x12 .. 0x23 */
static int code_aff0(short c) { return (unsigned)(int)c <= 0x11u; }
static int code_a310(short c) { return (unsigned)((int)c - 0x12) <= 0x11u; }

static void swap_ends(CsNode *t, const CsNode *v)
{
    t->f0 = v->f1; t->s0 = v->s1; t->z0 = v->z1; memcpy(t->n0, v->n1, 24);
    t->f1 = v->f0; t->s1 = v->s0; t->z1 = v->z0; memcpy(t->n1, v->n0, 24);
}

/* FUN_18014aef0(win, n, v, &na, A, &nb, B) (asm 18014aef0..18014b12b): each record whose s range lies strictly inside the window
   (min > win[0] and win[1] > max; min = s0 unless s1 <= s0 - comisd s1, s0 / jbe -, max = s0 unless s0 <= s1), turned so that its start
   is the upper ... lower end (kept when z1 > z0, else the two ends swapped), goes to A when FUN_18015aff0(code), else to B when
   FUN_18015a310(code). */
static void wall_classify(const double win[2], int n, const CsNode *v, short *na, CsNode *A, short *nb, CsNode *B)
{
    *na = 0; *nb = 0;
    for (int i = 0; i < n; i++) {
        double s0 = v[i].s0, s1 = v[i].s1;
        double mn = s1 > s0 ? s0 : s1, mx = s0 > s1 ? s0 : s1;
        if (!(mn > win[0]) || !(win[1] > mx)) continue;
        CsNode t = v[i];
        if (!(v[i].z1 > v[i].z0)) swap_ends(&t, &v[i]);
        t.code = v[i].code; t.next = 0;
        if (code_aff0(t.code)) A[(*na)++] = t;
        else if (code_a310(t.code)) B[(*nb)++] = t;
    }
}

/* FUN_18014cb60(2, n, a) (asm 18014cb60..18014ccd7, mode 2): insertion sort by z0 ascending, an element moved down while the one
   before it has a greater z0 (comisd + jbe: equal or unordered stops) */
static void wall_sort_z(int n, CsNode *a)
{
    for (int i = 1; i < n; i++) {
        CsNode key = a[i]; int j = i - 1;
        while (j >= 0 && a[j].z0 > key.z0) { a[j + 1] = a[j]; j--; }
        a[j + 1] = key;
    }
}

/* FUN_180145d80(tol, n, a, &cnt, out) (asm 180145d80..180145e6e) with FUN_18014d900(tol, i, n - 1, a, &m) (asm 18014d900..18014d9e4):
   from i, m = a[i] extended by every following a[j] while a[j].z0 <= m.z1 + tol (ja breaks: unordered continues): when a[j].z1 > m.z1
   m's end (f1, s1, z1, n1) = a[j]'s; returns the last index taken; m appended to out when 0 <= that < n, the next i = it + 1 */
static void wall_merge(double tol, int n, const CsNode *a, short *cnt, CsNode *out)
{
    short i = 0;
    do {
        CsNode m = a[i]; short last = i;
        for (short j = (short)(i + 1); j <= n - 1; j++) {
            if (a[j].z0 > m.z1 + tol) break;
            if (a[j].z1 > m.z1) { m.f1 = a[j].f1; m.s1 = a[j].s1; m.z1 = a[j].z1; memcpy(m.n1, a[j].n1, 24); }
            last = j;
        }
        if (0 <= last && last < n) { out[(*cnt)++] = m; i = last; } else i = (short)n;
        i++;
    } while (i < n);
}

/* FUN_1801539d0(p, r, &q, &t) (asm 1801539d0..180153b07): the projection of p on r's (s, z) segment, t = ((p.z - z0) dz + (p.s - s0) ds) /
   (dz^2 + ds^2), q = (s0 (1 - t) + s1 t, z0 (1 - t) + z1 t); 0 when the length is 0. FUN_180153b60 (asm 180153b60..180153bc8): t < 0 ->
   q = (s0, z0), t = 0; t > 1 -> (s1, z1), t = 1. */
static int proj_clamped(const double p[2], const CsNode *r, double q[2], double *t)
{
    double ds = r->s1 - r->s0, dz = r->z1 - r->z0, L = dz * dz + ds * ds;
    if (L == 0.0) return 0;
    double u = ((p[1] - r->z0) * dz + (p[0] - r->s0) * ds) / L;
    *t = u;
    q[0] = r->s0 * (1.0 - u) + r->s1 * u; q[1] = r->z0 * (1.0 - u) + r->z1 * u;
    if (0.0 > u) { q[0] = r->s0; q[1] = r->z0; *t = 0.0; }
    else if (u > 1.0) { q[0] = r->s1; q[1] = r->z1; *t = 1.0; }
    return 1;
}
/* FUN_1802535d0(t, a, b, out) (asm 1802535d0..1802536b2): (b - a) t + a normalised; v_z_3d (0, 0, 1) when its length is 0 */
static void lerp_normal(double t, const double a[3], const double b[3], double o[3])
{
    double x = (b[0] - a[0]) * t + a[0], y = (b[1] - a[1]) * t + a[1], z = (b[2] - a[2]) * t + a[2];
    double l = sqrt(y * y + x * x + z * z);
    if (l == 0.0) { o[0] = 0.0; o[1] = 0.0; o[2] = 1.0; return; }
    o[0] = x / l; o[1] = y / l; o[2] = z / l;
}
typedef struct { short idx; double p[2], n[3], t, d; } WallHit;   /* FUN_180162300's output: +0 idx, +8 point, +0x18 normal, +0x30 t, +0x38 d */
/* FUN_180162300(mode, p, tol, start, end, a, R) (asm 180162300..1801624b5): the nearest point of a[start .. end - 1] to p (FUN_180153b60),
   R.d starting at 1e38 (0x47d2ced32a16a1b1); a closer one (R.d > d) is taken (index, point, the lerp of the record's normals at t, t, d)
   and ends the search when tol > d; after each record that projects, mode 0: s0 - R.p.s, else z0 - R.p.z, > R.d ends it. Returns
   0 <= R.idx < end; end <= 0: 0, R untouched. */
static int wall_nearest(int mode, const double p[2], double tol, short start, short end, const CsNode *a, WallHit *R)
{
    if (!(end > 0)) return 0;
    R->idx = -1; { unsigned long long b = 0x47d2ced32a16a1b1ull; memcpy(&R->d, &b, 8); }
    for (short i = start; i < end; i++) {
        double q[2], t;
        if (!proj_clamped(p, &a[i], q, &t)) continue;
        double d = sqrt((q[1] - p[1]) * (q[1] - p[1]) + (q[0] - p[0]) * (q[0] - p[0]));
        if (R->d > d) {
            R->idx = i; R->p[0] = q[0]; R->p[1] = q[1];
            lerp_normal(t, a[i].n0, a[i].n1, R->n);
            R->t = t; R->d = d;
            if (tol > d) break;
        }
        double v = mode == 0 ? a[i].s0 - R->p[0] : a[i].z0 - R->p[1];
        if (v > R->d) break;
    }
    return 0 <= R->idx && R->idx < end;
}

/* FUN_180151510(P, dir, n, C, out) (asm 180151510..1801518e3): the connector P (start = its lower end) split at the walls C[] it runs
   along. 0 when P.z0 > C[n-1].z1 or C[0].z0 > P.z1. Q = P (dir) or P turned (Q.s0 / z0 = P.s1 / z1, Q.s1 / z1 = P.s0 / z0); R1 = the
   nearest wall point to Q's start, R2 to Q's end (FUN_180162300 mode 1 over C[0 .. n-1], tol = mccore tol); Q.n0 = R1's normal, Q.n1 =
   R2's. From index R1.idx stepping +1 (dir) or -1, count = |R2.idx - R1.idx| + 1 nodes: code 2 when FUN_18015aff0(the wall's code) else
   0x14, flags 0; start = Q's start / n0 for the first, else the previous node's end / n1; end = Q's end and R2's normal for the last, else
   the projection on Q (FUN_180162300 over Q alone) of the wall's end in the direction (s1 / z1 when dir, else s0 / z0) with its normal -
   when that fails the count so far is returned. */
static short wall_split(const CsNode *P, int dir, short n, const CsNode *C, double tol, CsNode *out)
{
    if (P->z0 > C[n - 1].z1) return 0;
    if (C[0].z0 > P->z1) return 0;
    CsNode Q; memset(&Q, 0, sizeof Q); short step;
    if (dir) { Q = *P; step = 1; }
    else { step = -1; Q.s0 = P->s1; Q.z0 = P->z1; Q.s1 = P->s0; Q.z1 = P->z0; }
    WallHit R1, R2;
    double p[2] = { Q.s0, Q.z0 };
    if (!wall_nearest(1, p, tol, 0, n, C, &R1)) return 0;
    memcpy(Q.n0, R1.n, 24);
    p[0] = Q.s1; p[1] = Q.z1;
    if (!wall_nearest(1, p, tol, 0, n, C, &R2)) return 0;
    short hi = R1.idx > R2.idx ? R1.idx : R2.idx, lo = R1.idx < R2.idx ? R1.idx : R2.idx;
    double n1[3]; memcpy(n1, R2.n, 24); memcpy(Q.n1, R2.n, 24);
    short count = (short)(hi - lo + 1), k = 0, w = R1.idx;
    for (; k < count; k++, w = (short)(w + step)) {
        CsNode *o = &out[k];
        o->code = code_aff0(C[w].code) ? 2 : 0x14; o->f0 = 0; o->f1 = 0;
        double e[2] = { dir ? C[w].s1 : C[w].s0, dir ? C[w].z1 : C[w].z0 };
        if (k == 0) { o->s0 = Q.s0; o->z0 = Q.z0; memcpy(o->n0, Q.n0, 24); }
        else { o->s0 = out[k - 1].s1; o->z0 = out[k - 1].z1; memcpy(o->n0, out[k - 1].n1, 24); }
        if (k == count - 1) { memcpy(o->n1, n1, 24); o->s1 = Q.s1; o->z1 = Q.z1; }
        else {
            WallHit R3;
            if (!wall_nearest(1, e, tol, 0, 1, &Q, &R3)) return k;
            o->s1 = R3.p[0]; o->z1 = R3.p[1]; memcpy(o->n1, R3.n, 24);
            memcpy(n1, Q.n1, 24);
        }
        o->next = 0;
    }
    return k;
}

/* FUN_18015bca0(pts, d, n, v, A, B, C, out) (asm 18015bca0..18015be99): pts = {k.s1, k.z1, nx.s0, nx.z0}; P = the connector from the lower
   end (dir = pts[3] > pts[1]); the window [min(s) - d, max(s) + d] (min / max as the asm: pts[2] on ties); FUN_18014aef0; A sorted
   (n > 1) and merged into C, then B the same (appended); C sorted; FUN_180151510(P, dir, nC, C, out) when nC > 0, else 0. */
short cs_wall_split(const double pts[4], double d, int n, const CsNode *v, double tol, CsNode *A, CsNode *B, CsNode *C, CsNode *out)
{
    CsNode P; memset(&P, 0, sizeof P);
    int dir = pts[3] > pts[1];
    if (dir) { P.s0 = pts[0]; P.z0 = pts[1]; P.s1 = pts[2]; P.z1 = pts[3]; }
    else { P.s0 = pts[2]; P.z0 = pts[3]; P.s1 = pts[0]; P.z1 = pts[1]; }
    double win[2];
    win[0] = (pts[2] > pts[0] ? pts[0] : pts[2]) - d;
    win[1] = (pts[0] > pts[2] ? pts[0] : pts[2]) + d;
    short na, nb, nc = 0;
    wall_classify(win, n, v, &na, A, &nb, B);
    if (na > 1) wall_sort_z(na, A);
    if (na > 0) wall_merge(tol, na, A, &nc, C);
    if (nb > 1) wall_sort_z(nb, B);
    if (nb > 0) wall_merge(tol, nb, B, &nc, C);
    if (nc > 1) wall_sort_z(nc, C);
    if (nc < 1) return 0;
    return wall_split(&P, dir, nc, C, tol, out);
}

/* FUN_1801c74f0(start, n, v, &code, seg, nrm) (asm 1801c74f0..1801c7628): the first record from start whose code >= 0; seg = (s0, z0, s1,
   z1) and nrm = (n0, n1) when z1 > z0, else its ends swapped; returns its index or -1 (code / seg untouched) */
static short wall_next(short start, short n, const CsNode *v, short *code, double seg[4], double nrm[6])
{
    if (!(start < n)) return -1;
    short i = start;
    while (v[i].code < 0) { i++; if (!(i < n)) return -1; }
    *code = v[i].code;
    if (v[i].z1 > v[i].z0) { seg[0] = v[i].s0; seg[1] = v[i].z0; seg[2] = v[i].s1; seg[3] = v[i].z1; memcpy(nrm, v[i].n0, 24); memcpy(nrm + 3, v[i].n1, 24); }
    else { seg[0] = v[i].s1; seg[1] = v[i].z1; seg[2] = v[i].s0; seg[3] = v[i].z0; memcpy(nrm, v[i].n1, 24); memcpy(nrm + 3, v[i].n0, 24); }
    return i;
}
static void mark(CsNode *v, short i) { if (v[i].code >= 0) v[i].code = (short)(-1 - v[i].code); }
/* FUN_1801c7aa0(n, v, &code, seg, nrm) (asm 1801c7aa0..1801c7ce6): the next unused wall (FUN_1801c74f0 from 0), marked (code = -1 - code),
   then joined with every later unused wall j that continues it: j's start within DAT_1804e9250 of seg's end (seg.z1 = j.z1, nrm's end =
   j's) or j's end within it of seg's start (seg.z0 = j.z0, nrm's start = j's) (distances sqrt(dz^2 + ds^2), 9250 > d), then code = j's,
   seg.s0 = min(seg.s0, j.s0, j.s1), seg.s1 = max(seg.s1, j.s0, j.s1) and j marked; returns 1 when a wall was found */
static int wall_chain(short n, CsNode *v, double t9250, short *code, double seg[4], double nrm[6])
{
    short c0 = *code;
    short i = wall_next(0, n, v, &c0, seg, nrm);
    if (i >= 0) mark(v, i);
    *code = c0;
    if (i < 0) return 0;
    for (;;) {
        double s2[4], n2[6]; short cj = c0;
        short j = wall_next((short)(i + 1), n, v, &cj, s2, n2);
        if (j < 0) return 1;
        i = j; c0 = cj;
        double d = sqrt((s2[1] - seg[3]) * (s2[1] - seg[3]) + (s2[0] - seg[2]) * (s2[0] - seg[2]));
        if (t9250 > d) { seg[3] = s2[3]; memcpy(nrm + 3, n2 + 3, 24); }
        else {
            double d2 = sqrt((s2[3] - seg[1]) * (s2[3] - seg[1]) + (s2[2] - seg[0]) * (s2[2] - seg[0]));
            if (!(t9250 > d2)) continue;
            seg[1] = s2[1]; memcpy(nrm, n2, 24);
        }
        *code = cj;
        if (seg[0] > s2[0]) seg[0] = s2[0];
        if (seg[0] > s2[2]) seg[0] = s2[2];
        if (s2[0] > seg[2]) seg[2] = s2[0];
        if (s2[2] > seg[2]) seg[2] = s2[2];
        mark(v, j);
    }
}

/* systypes proj_pt_ln (decomp/mccore_filter.c l.4912): t on the infinite line; t is left as it was when the line has no length */
static void proj_ln(const double p[2], const double l[4], double q[2], double *t)
{
    double dx = l[2] - l[0], dy = l[3] - l[1], L = dy * dy + dx * dx;
    if (L == 0.0) return;
    double u = ((p[1] - l[1]) * dy + (p[0] - l[0]) * dx) / L;
    *t = u; q[0] = (1.0 - u) * l[0] + u * l[2]; q[1] = (1.0 - u) * l[1] + u * l[3];
}

/* FUN_1801c8080(pts, n, v, out) (asm 1801c8080..1801c8675): A = the connector's lower end, B = its upper end (A = (pts[0], pts[1]) when
   pts[3] > pts[1], up = 1); d4 = 9238 x 8.0 (0x1804140a0) + 9258. For each chain of walls (FUN_1801c7aa0) taller than 9238 (z1 - z0 > 9238,
   d3 = 9238 / that): A projected on the chain's line (proj_pt_ln): -d3 < t < d3 + 1 and |A.s - q.s| < |seg.s0 - seg.s1| + d4 and < the best
   so far (1e38): normal a = the chain's start normal, code a = its code; B likewise with the end normal (normal b, code b). Neither: -1.
   out = (pts[0], pts[1]) -> (pts[2], pts[3]), code 0x28, then: both: code 2 / 0x14 from code a, else from code b (FUN_18015aff0 /
   FUN_18015a310), normals (a, b) when up else (b, a), returns 2; a only: code from a, both normals a, 0; b only: from b, both b, 1. The
   walls' codes are restored (-1 - code for the marked ones) at the end. */
static short wall_connector(const double pts[4], short n, CsNode *v, const CsCfbConst *C, CsNode *out)
{
    double na[3] = { 0, 0, 0 }, nb[3] = { 0, 0, 0 };
    double d4 = C->t9238 * 8.0 + C->t9258;
    double A[2], B[2]; int up;
    if (pts[3] > pts[1]) { A[0] = pts[0]; A[1] = pts[1]; B[0] = pts[2]; B[1] = pts[3]; up = 1; }
    else { A[0] = pts[2]; A[1] = pts[3]; B[0] = pts[0]; B[1] = pts[1]; up = 0; }
    double besta = 1e38, bestb = 1e38;
    int fa = 0, fb = 0; short ca = 0, cb = 0, code = 0;
    double seg[4], nrm[6], t = 0.0, q[2] = { 0, 0 };
    short r = -1;
    if (wall_chain(n, v, C->t9250, &code, seg, nrm)) {
        do {
            double w = fabs(seg[0] - seg[2]) + d4;
            double h = seg[3] - seg[1];
            if (h > C->t9238) {
                double d3 = C->t9238 / h;
                proj_ln(A, seg, q, &t);
                if (t > -d3 && d3 + 1.0 > t) {
                    double ds = fabs(A[0] - q[0]);
                    if (w > ds && besta > ds) { besta = ds; memcpy(na, nrm, 24); fa = 1; ca = code; }
                }
                proj_ln(B, seg, q, &t);
                if (t > -d3 && d3 + 1.0 > t) {
                    double ds = fabs(B[0] - q[0]);
                    if (w > ds && bestb > ds) { bestb = ds; memcpy(nb, nrm + 3, 24); fb = 1; cb = code; }
                }
            }
        } while (wall_chain(n, v, C->t9250, &code, seg, nrm));
        if (fa || fb) {
            out->s0 = pts[0]; out->z0 = pts[1]; out->s1 = pts[2]; out->z1 = pts[3]; out->code = 0x28;
            if (fa && fb) {
                if (code_aff0(ca)) out->code = 2;
                else if (code_a310(ca)) out->code = 0x14;
                else if (code_aff0(cb)) out->code = 2;
                else if (code_a310(cb)) out->code = 0x14;
                if (up) { memcpy(out->n0, na, 24); memcpy(out->n1, nb, 24); } else { memcpy(out->n0, nb, 24); memcpy(out->n1, na, 24); }
                r = 2;
            } else if (fa) {
                if (code_aff0(ca)) out->code = 2; else if (code_a310(ca)) out->code = 0x14;
                memcpy(out->n0, na, 24); memcpy(out->n1, na, 24);
                r = 0;
            } else {
                if (code_aff0(cb)) out->code = 2; else if (code_a310(cb)) out->code = 0x14;
                memcpy(out->n0, nb, 24); memcpy(out->n1, nb, 24);
                r = 1;
            }
        }
    }
    for (short i = 0; i < n; i++) if (v[i].code < 0) v[i].code = (short)(-1 - v[i].code);
    return r;
}
short cs_wall_connector(const double pts[4], int n, CsNode *v, const CsCfbConst *C, CsNode *out) { return wall_connector(pts, (short)n, v, C, out); }

/* FUN_1801b0c80(rufvcs, reader, &pos) (l.288238) for line `line`: the line's wall records (table t98 = DAT_1804e9798 as FUN_1801e7c20 loaded it)
   along the chain of 0x18 trailers into pool (DAT_1804e97a8, 500 records): see cs_rgn_vcs_copy; returns the count, -2 beyond the pool */
int cs_wall_read(const unsigned char *vcs, size_t vlen, const unsigned char *t98, int line, CsNode *pool, double *lp)
{
    int off; short ec; memcpy(&off, t98 + 8 * (size_t)line, 4); memcpy(&ec, t98 + 8 * (size_t)line + 4, 2);
    int cnt = 0;
    if (ec > 500) return 0;
    long long o = off;
    while (o != -1) {
        short k; int prev;
        if (o < 0 || (size_t)o + 0x18 > vlen) break;
        memcpy(&k, vcs + o + 2, 2); memcpy(&prev, vcs + o + 4, 4); memcpy(lp, vcs + o + 8, 8);
        if (k < 1 || k > 500) break;
        long long st = o - (long long)k * 0x68;
        if (st < 0 || (size_t)st + (size_t)k * 0x68 > vlen) break;
        if (cnt + k > 500) return -2;
        for (int j = 0; j < k; j++) {
            const unsigned char *r = vcs + st + 0x68 * (size_t)j; CsNode *d = &pool[cnt + j];
            memset(d, 0, sizeof *d);
            d->f0 = r[0]; memcpy(&d->s0, r + 8, 8); memcpy(&d->z0, r + 0x10, 8); memcpy(d->n0, r + 0x18, 24);
            d->f1 = r[0x30]; memcpy(&d->s1, r + 0x38, 8); memcpy(&d->z1, r + 0x40, 8); memcpy(d->n1, r + 0x48, 24);
            memcpy(&d->code, r + 0x60, 2); memcpy(&d->next, r + 0x62, 2);
        }
        cnt += k; o = prev;
    }
    return cnt;
}
