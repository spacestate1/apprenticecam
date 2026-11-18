/* cam3d_cscallop_emesh.c - Constant Scallop (op 43) stage D1: FUN_18018e630, the "emesh" rewrite of temp.rgn into tmpemesh.tmp
 * (formats/surf_classic3.md 12.23 / 12.25). Sources: decomp/mcmsurf_all.c (l.N) and objdump of mcmsurf.dll (asm ADDR); the decompile
 * loses most arguments of the callees and indexes 3-vectors by bools wrongly ("bVar == true -> +2"), so every callee is read from the asm.
 *
 * FUN_1801d3ca0 calls FUN_18018e630(temp.rgn, &hdr, cVar9, p4, p5, p6, &DAT_1804d5390, abort) twice (asm 1801d47c0..1801d482a): direction B
 * (p4 = 1: the kind-7 record, header +0x18) creating tmpemesh.tmp, then direction A (p4 = 0: kind 6, header +0xc) appended to it. Per
 * line of the direction: FUN_1801a1f60 reads the line's block chain into the node store (FUN_180182e70), FUN_180184cc0 copies it to an
 * array, and every end of the line and every gap along it (FUN_1801a8e50) is extended by FUN_1801a9150 across the lines of the OTHER
 * direction read back from temp.rgn (FUN_1801a83c0 / FUN_1801a8ab0 / FUN_180158fb0 / FUN_1801a9080), the extension segments stored with
 * code 1 (FUN_1801aa4a0); then FUN_180182800 (code 0x28 nodes recoded), FUN_180182de0 -> FUN_1801a1a70 (the block, cs_rgn_block), and
 * after the lines FUN_1801b4070 (the input trailer copied with the new line table).
 * NOT PORTED: DAT_1804d7038 == 1 (FUN_1801a9e60, op +0x1268 = 1), containment (DAT_1804e8ffc, FUN_1801b1ab0 / FUN_180192af0), header
 * kinds other than 0x11, a store overflow (DAT_1804e9344 = 2) - refused. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

static int r32(const unsigned char *p) { int v; memcpy(&v, p, 4); return v; }
static short r16(const unsigned char *p) { short v; memcpy(&v, p, 2); return v; }
static double r64(const unsigned char *p) { double v; memcpy(&v, p, 8); return v; }
static void put(CsBuf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 4096; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, p, n); b->n += n;
}
/* MSVC cvttsd2si: out of range -> 0x80000000 */
static int cvtt(double x) { return (x > -2147483649.0 && x < 2147483648.0) ? (int)x : (int)0x80000000u; }

/* a SysFile::Read of n bytes at o of the in-memory file: whole or failed */
static int rd_ok(const CsEmCtx *X, long o, long n) { return o >= 0 && o + n <= (long)X->len; }

/* FUN_18018f780(table6, 1, file, hdr +0x18, ctx, err) (l.267677-267869) and FUN_180186bb0 / FUN_18015de40's values (l.262163-262218):
   the two-direction context DAT_1804d5390 as FUN_18018e630 / FUN_1801a83c0 / FUN_18015f990 read it. Direction A from the kind-6 trailer
   (header +0xc: n +0xa6, first +0x92, step +0x9a, the table), B from the kind-7 trailer (header +0x18); last = first + step added n - 1
   times (l.267706-267714, l.267817-267824); +0x358 / +0x360 = 1.0, +0x418 = -1.0, +0x420 = 1.0 (l.267718-267719, 267825-267826).
   +0x399 / +0x459 = 2 (FUN_18018e630 l.267066-267069 for header kinds 0xf / 0x11). Also the record's DAT_1804e9238 / 9240 / 9248 / 9250
   (FUN_180186bb0) and the record's containment int (-1 here). Returns 0, or -1 (a containment record or a bad file: NOT PORTED). */
static int em_rec(const unsigned char *f, size_t len, int t, int *n, double *first, double *step, const int **tab, int *cont, double tl[4])
{
    if (t < 0 || (size_t)t + 0xaa > len) return -1;
    *n = r32(f + t + 0xa6); *first = r64(f + t + 0x92); *step = r64(f + t + 0x9a);
    if (*n < 0 || (size_t)t + 0xaa + 4 * (size_t)*n + 6 > len) return -1;
    *tab = (const int *)(f + t + 0xaa);
    *cont = r32(f + t + 0xaa + 4 * *n);
    for (int i = 0; i < 4; i++) tl[i] = r64(f + t + 0x4a + 8 * i);
    return *cont < 0 ? 0 : -1;
}
int cs_em_ctx(const unsigned char *f, size_t len, CsEmCtx *X, double tl6[4], const char **why)
{
    memset(X, 0, sizeof *X);
    X->f = f; X->len = len;
    if (len < 0x38 || r32(f + 8) != 0x11) { *why = "temp.rgn header kind != 0x11: NOT PORTED"; return -1; }
    int c6, c7; double tl7[4];
    if (em_rec(f, len, r32(f + 0xc), &X->nA, &X->firstA, &X->stepA, &X->tabA, &c6, tl6)
        || em_rec(f, len, r32(f + 0x18), &X->nB, &X->firstB, &X->stepB, &X->tabB, &c7, tl7)) {
        *why = "temp.rgn record with containment (or unreadable): NOT PORTED"; return -1;
    }
    /* the tables are 4-byte aligned in no particular way: copy them */
    int *a = malloc(sizeof(int) * ((size_t)X->nA + 1)), *b = malloc(sizeof(int) * ((size_t)X->nB + 1));
    memcpy(a, X->tabA, 4 * (size_t)X->nA); memcpy(b, X->tabB, 4 * (size_t)X->nB);
    X->tabA = a; X->tabB = b;
    X->lastA = X->firstA; for (int i = 1; i < X->nA; i++) X->lastA = X->lastA + X->stepA;
    X->lastB = X->firstB; for (int i = 1; i < X->nB; i++) X->lastB = X->lastB + X->stepB;
    X->sA[0] = 1.0; X->sA[1] = 1.0; X->sB[0] = -1.0; X->sB[1] = 1.0;
    X->kindA = 2; X->kindB = 2;
    return 0;
}

/* FUN_1801ad940 (l.285922): max(|+0x348|, |+0x408|) */
static double em_maxstep(const CsEmCtx *X)
{
    double a = fabs(X->stepA), b = fabs(X->stepB);
    return a <= b ? b : a;
}

/* FUN_1801a6f50 (asm 1801a6f50..1801a6fba): the 0x18 trailer at o: +2 count, +4 previous block (sign-extended), +8 the line position;
   returns read == 0x18 (the outputs only when read) */
static int rd_trailer(const CsEmCtx *X, long o, short *cnt, long *prev, double *pos)
{
    if (!rd_ok(X, o, 0x18)) return 0;
    *cnt = r16(X->f + o + 2); *prev = r32(X->f + o + 4); *pos = r64(X->f + o + 8);
    return 1;
}

/* FUN_1801a1f60(file, off, FUN_1801a6f50, FUN_1801a7a00, 0x18, ctx, &DAT_1804e92b8) (l.277861-277995; reader FUN_1801a7a00 asm
   1801a7a00..1801a7a8b: +0 s, +8 z, +0x10 code, +0x12 flag, normal (0, 0, 1), returns read == 0x18): the line's block chain from the last
   block back. A block's first point starts a segment (its code); each next point ends it (its flag the end flag) and the segment goes to
   FUN_180182e70 when the start code < 0x25 (0x25 / 0x26: the containment list, empty here); a point whose code > 0x26 starts the next
   segment with code - 0x27. The trailer reader writes the line position. Returns the store error (FUN_180268e20(2) = -4) or 0. */
static int em_read_line(const CsEmCtx *X, CsEst *L, const CsCfbConst *C, int off, double *pos)
{
    long o = off; int ok;
    long nx = 0;
    do {
        if (o == -1) return 0;
        short cnt = 0;
        ok = rd_trailer(X, o, &cnt, &nx, pos);
        if (!ok || cnt < 1) {
            /* fprintf(tmpdiag, "ERROR: trouble reading file: (pre)surf\n") */
        } else {
            long q = o - (long)cnt * 0x18;
            int have = 0; short sc = 0; unsigned char fl[2] = { 0, 0 };
            double s[2] = { 0, 0 }, z[2] = { 0, 0 }, n[6] = { 0, 0, 1, 0, 0, 1 };
            for (short j = 0; j < cnt; j++, q += 0x18) {
                ok = rd_ok(X, q, 0x18);
                if (!ok) continue;
                double ps = r64(X->f + q), pz = r64(X->f + q + 8); short code = r16(X->f + q + 0x10); unsigned char pf = X->f[q + 0x12];
                if (have) {
                    fl[1] = pf;
                    if (sc < 0x25) {
                        double ss[2] = { s[0], ps }, zz[2] = { z[0], pz };
                        cs_est_insert(L, C, sc, fl, ss, zz, n);
                        if (L->err) return -4;
                    }
                    have = code > 0x26;
                    if (have) { s[0] = ps; z[0] = pz; sc = (short)(code - 0x27); fl[0] = pf; }
                } else {
                    fl[0] = pf; s[0] = ps; z[0] = pz; have = 1; sc = code;
                }
            }
        }
        o = nx;
    } while (ok);
    return 0;
}

/* FUN_1801a8ab0(file, off, FUN_1801a6f50, reader, stride, 0, 1, d2, cap, &n, arr, &pos, &cnt) (asm 1801a8ab0..1801a8e4a; reader
   FUN_1801a7bf0 for kind 2 (asm 1801a7bf0: +0 s, +8 z, +0x10 code, normal (0, 0, 1)), stride FUN_1801ae2b0(2) = 0x18): a line of the
   other direction read straight into an array of 0x68 nodes (no store): from the last block back, a segment per pair of points as in
   FUN_1801a1f60 (start code < 0x25); with p7 = 1, before a segment whose start is d2[1] < |s0 - previous s1| < d2[0] away from the previous
   node's end, a bridging node {previous end -> this start, the start's code}. A full array (n >= cap) -> FUN_180268e20(2) = -4. The flag
   bytes +0 / +0x30 are not written. Returns 0 (also when a read fails: the last reader result ends the walk) or -4. */
static int em_cross(const CsEmCtx *X, int off, const double d2[2], int cap, CsNode *arr, short *nout, double *pos)
{
    short si = 0; long o = off, nx = 0; int ok;
    static const double N[3] = { 0, 0, 1 };
    *nout = 0;
    for (;;) {
        if (o == -1) { *nout = si; return 0; }
        short cnt = 0;
        ok = rd_trailer(X, o, &cnt, &nx, pos);
        if (!ok || cnt < 1) {
            /* "ERROR: trouble reading file: (pre)surf" */
        } else {
            long q = o - (long)cnt * 0x18;
            int have = 0; short sc = 0; double s0 = 0, z0 = 0;
            for (short j = 0; j < cnt; j++, q += 0x18) {
                ok = rd_ok(X, q, 0x18);
                if (!ok) continue;
                double ps = r64(X->f + q), pz = r64(X->f + q + 8); short code = r16(X->f + q + 0x10);
                if (!have) { s0 = ps; z0 = pz; sc = code; have = 1; continue; }
                if (sc < 0x25) {
                    if (si > 0) {
                        if (si >= cap) { *nout = si; return -4; }
                        CsNode *pv = &arr[si - 1];
                        double d = fabs(s0 - pv->s1);
                        if (d > d2[1] && d2[0] > d) {
                            CsNode *b = &arr[si];
                            b->s0 = pv->s1; b->z0 = pv->z1; memcpy(b->n0, pv->n1, sizeof b->n0);
                            b->s1 = s0; b->z1 = z0; memcpy(b->n1, N, sizeof b->n1); b->code = sc;
                            si++;
                        }
                    }
                    if (si >= cap) { *nout = si; return -4; }
                    CsNode *e = &arr[si++];
                    e->s0 = s0; e->z0 = z0; memcpy(e->n0, N, sizeof e->n0);
                    e->s1 = ps; e->z1 = pz; memcpy(e->n1, N, sizeof e->n1); e->code = sc;
                }
                /* (code >= 0x25: the containment list DAT_1804e92d0 when DAT_1804e8ffc - not on this path) */
                if (code >= 0x27) { s0 = ps; z0 = pz; sc = (short)(code - 0x27); have = 1; } else have = 0;
            }
        }
        if (!ok) { *nout = si; return 0; }
        o = nx;
    }
}

/* FUN_18015f990 (asm 18015f990..18015fb94): the A lines around P[0] (i0 / i1) and the B lines around P[1] (j0 / j1); 0 when P lies
   outside either direction's range (tol out) or an index is out of [0, n) */
static int em_lines(const CsEmCtx *X, double tol, const double P[3], int *i0, int *i1, int *j0, int *j1)
{
    double st = X->stepA;
    if (st > 0.0) {
        if (X->firstA - tol > P[0]) return 0;
        if (P[0] > tol + X->lastA) return 0;
    }
    if (0.0 > st) {
        if (P[0] > tol + X->firstA) return 0;
        if (X->lastA - tol > P[0]) return 0;
    }
    int k = cvtt(floor((P[0] - X->firstA) / st));
    *i0 = k;
    if (0.0 > X->stepA) { *i1 = k; *i0 = *i0 + 1; } else *i1 = k + 1;
    if (*i0 < 0 || *i0 >= X->nA || *i1 < 0 || *i1 >= X->nA) return 0;
    st = X->stepB;
    if (st > 0.0) {
        if (X->firstB - tol > P[1]) return 0;
        if (P[1] > tol + X->lastB) return 0;
    }
    if (0.0 > st) {
        if (P[1] > tol + X->firstB) return 0;
        if (X->lastB - tol > P[1]) return 0;
    }
    k = cvtt(floor((P[1] - X->firstB) / st));
    *j0 = k;
    if (0.0 > X->stepB) { *j1 = k; *j0 = *j0 + 1; } else *j1 = k + 1;
    if (*j0 < 0 || *j0 >= X->nB || *j1 < 0) return 0;
    return *j1 < X->nB;
}

/* FUN_180158fb0(n, arr, v, d) (asm 180158fb0..1801591c6): the node holding v in s (within tol), by bisection over the n nodes (at most
   n + 10 steps; the order from arr[n-1].s1 > arr[0].s0); a node shorter than d is replaced by the nearest longer one holding v, first
   below then above; -1 when none */
static short em_find(short n, const CsNode *arr, double v, double d, double tol)
{
    short hi = (short)(n - 1), lo = 0, it = 0, lim = (short)(n + 10);
    if (lo > hi) return -1;
    int asc = arr[hi].s1 > arr[0].s0;
    while (lo <= hi) {
        if (it >= lim) return -1;
        short mid = (short)(((int)lo + (int)hi) / 2);
        double a = arr[mid].s0, b = arr[mid].s1, mn = b > a ? a : b, mx = a > b ? a : b;
        if (mn - tol > v) { if (asc) hi = (short)(mid - 1); else lo = (short)(mid + 1); }
        else if (v > tol + mx) { if (asc) lo = (short)(mid + 1); else hi = (short)(mid - 1); }
        else {
            if (!(d > fabs(mn - mx))) return mid;
            for (short k = (short)(mid - 1); k >= lo; k--) {
                double a2 = arr[k].s0, b2 = arr[k].s1, mn2 = b2 > a2 ? a2 : b2, mx2 = a2 > b2 ? a2 : b2;
                if (fabs(mn2 - mx2) > d && v > mn2 - tol && tol + mx2 > v) return k;
            }
            for (short k = (short)(mid + 1); k <= hi; k++) {
                double a2 = arr[k].s0, b2 = arr[k].s1, mn2 = b2 > a2 ? a2 : b2, mx2 = a2 > b2 ? a2 : b2;
                if (fabs(mn2 - mx2) > d && v > mn2 - tol && tol + mx2 > v) return k;
            }
            return -1;
        }
        it++;
    }
    return -1;
}

/* FUN_1801a83c0(a, sgn, P, ctx, cap, arr, &t) (asm 1801a83c0..1801a86c9): the next line of the other direction from P: a = 0 (a B line
   is extended): the A line i0, or i1 when sgn == 1, its s compared with P[1]; a = 1: the B line j0 / j1 (its s times ctx+0x418 by
   FUN_1801adf60), compared with P[0]. d2 = {FUN_1801ad940, DAT_1804e9250}. Returns the node holding P's coordinate (FUN_180158fb0), its
   line's position in *t, or -1. */
static short em_step(const CsEmCtx *X, const CsCfbConst *C, int a, int sgn, const double P[3], int cap, CsNode *arr, double *t)
{
    double d2[2] = { em_maxstep(X), C->t9250 };
    int i0 = 0, i1 = 0, j0 = 0, j1 = 0;
    if (!em_lines(X, C->systol, P, &i0, &i1, &j0, &j1)) return -1;
    const int *tab; int idx, r12;
    if (a == 0) { tab = X->tabA; r12 = 1; idx = sgn == 1 ? i1 : i0; }
    else { tab = X->tabB; r12 = 0; idx = sgn == 1 ? j1 : j0; }
    short n = 0;
    if (em_cross(X, tab[idx], d2, cap, arr, &n, t) < 0) return -1;
    if (a == 1) for (short i = 0; i < n; i++) { arr[i].s0 = X->sB[0] * arr[i].s0; arr[i].s1 = X->sB[0] * arr[i].s1; }
    if (n <= 0) return -1;
    double x1 = arr[0].s0, x0 = arr[n - 1].s1;
    double lo = x0 > x1 ? x1 : x0, hi = x1 > x0 ? x1 : x0;
    double v = P[r12];
    if (!(v >= lo)) return -1;
    if (!(hi >= v)) return -1;
    short k = em_find(n, arr, v, C->t9250, C->systol);
    if (0 > k) return -1;
    return k < n ? k : -1;
}

/* FUN_1801a9080(a, t, P, k, arr, out) (asm 1801a9080..1801a9140): out[a] = t, out[e] = P[e] (e = (a == 0)), out[2] = node k's z at out[e] */
static void em_interp(int a, double t, const double P[3], short k, const CsNode *arr, double out[3])
{
    int e = a == 0;
    out[a] = t;
    out[e] = P[e];
    double x = out[e] - arr[k].s0;
    x = x / (arr[k].s1 - arr[k].s0);
    x = x * (arr[k].z1 - arr[k].z0);
    out[2] = x + arr[k].z0;
}

/* FUN_1801aa4a0(c, code, A, NA, B, NB, sc) (asm 1801aa4a0..1801aa5d5): the segment A -> B stored by FUN_180182e70 in the line's own s (index
   e = (c == 0); times sc[0] when c == 1), its ends ordered by s, flags 0 */
static int em_seg(CsEst *L, const CsCfbConst *C, int c, short code, const double A[3], const double NA[3], const double B[3],
                  const double NB[3], double sc0)
{
    int e = c == 0;
    double ua = A[e], ub = B[e];
    if (c == 1) { ua = ua * sc0; ub = ub * sc0; }
    double s[2], z[2], n[6];
    if (ub > ua) { s[0] = ua; s[1] = ub; z[0] = A[2]; z[1] = B[2]; memcpy(n, NA, 24); memcpy(n + 3, NB, 24); }
    else { s[0] = ub; s[1] = ua; z[0] = B[2]; z[1] = A[2]; memcpy(n, NB, 24); memcpy(n + 3, NA, 24); }
    unsigned char fl[2] = { 0, 0 };
    return cs_est_insert(L, C, code, fl, s, z, n);
}

/* FUN_1801a9150(a, sgn, P, lim, Q, ctx, cap, work, &cnt, out) (asm 1801a9150..1801a9451): from P, line by line of the other direction
   (FUN_1801a83c0), the point on it at P's position (FUN_1801a9080), moved by tol in sgn's direction along a; with lim, stopped at Q when it
   passes Q (out = Q, returns 2); each step stored as a code-1 segment (FUN_1801aa4a0, normals (0, 0, 1), c = (a == 0), scale 1 / +0x418
   or 1 / +0x358). Returns 2 (Q reached), 1 (lim = 0: at least one step), else 0. */
static short em_extend(const CsEmCtx *X, CsEst *L, const CsCfbConst *C, int a, int sgn, const double P[3], int lim, const double Q[3],
                       int cap, CsNode *work, short *cnt, double out[3])
{
    static const double N[3] = { 0, 0, 1 };
    short ret = 0;
    *cnt = 0;
    memcpy(out, P, 24);
    double sc0; int c;
    if (a == 0) { sc0 = 1.0 / X->sB[0]; c = 1; } else { sc0 = 1.0 / X->sA[0]; c = 0; }
    double cur[3]; memcpy(cur, P, 24);
    for (;;) {
        double t = 0;
        short k = em_step(X, C, a, sgn, cur, cap, work, &t);
        if (k <= -1) return ret;
        em_interp(a, t, cur, k, work, out);
        double v = sgn == 1 ? out[a] + C->systol : out[a] - C->systol;
        out[a] = v;
        if (lim) {
            int clamp = 0;
            if (sgn == 1) clamp = out[a] > Q[a];
            else if (sgn == -1) clamp = Q[a] > out[a];
            if (clamp) { memcpy(out, Q, 24); ret = 2; }
        } else ret = 1;
        em_seg(L, C, c, 1, cur, N, out, N, sc0);
        (*cnt)++;
        memcpy(cur, out, 24);
        if (ret == 2) return 2;
    }
}

/* FUN_1801a8e50(d, gap, n, arr, pos, &ctr, out) (asm 1801a8e50..1801a9073): the line's ends and gaps, one per call (e = (d == 0)):
   ctr 0: P = the line's start {P[d] = pos, P[e] = arr[0].s0, P[2] = z0}, returns 0; then the first node k >= ctr with |s0(k) - s1(k-1)| >
   gap: P = node k-1's end, Q = node k's start, returns 2; at ctr == n: Q = the last end, returns 1; then -1. ctr is incremented on
   every call (past the node found). */
static short em_gaps(int d, double gap, short n, const CsNode *arr, double pos, short *ctr, double P[3], double Q[3])
{
    int e = d == 0;
    short r = -1, c = *ctr;
    if (n > 0) {
        if (c == 0) {
            P[d] = pos; P[e] = arr[0].s0; P[2] = arr[0].z0; r = 0;
        } else if (c > 0 && c < n) {
            for (; c < n; c++)
                if (fabs(arr[c].s0 - arr[c - 1].s1) > gap) {
                    P[d] = pos; P[e] = arr[c - 1].s1; P[2] = arr[c - 1].z1;
                    Q[d] = pos; Q[e] = arr[c].s0; Q[2] = arr[c].z0;
                    *ctr = (short)(c + 1);
                    return 2;
                }
            Q[d] = pos; Q[e] = arr[n - 1].s1; Q[2] = arr[n - 1].z1; r = 1;
        } else if (c == n) {
            Q[d] = pos; Q[e] = arr[n - 1].s1; Q[2] = arr[n - 1].z1; r = 1;
        }
    }
    *ctr = (short)(c + 1);
    return r;
}

/* FUN_180182800 / FUN_180182870 (l.259536-259620): each code-0x28 node of the list recoded from its neighbours: none -> 2; one -> FUN_18018b0e0
   of its code; both of FUN_18015bc40's family -> FUN_18018af60 of the next's code; else 0x14 when either is of FUN_18015a310's family, 2 when
   either is of FUN_18015afd0's, else FUN_18018b0e0 of the next's code */
static void em_conn(CsEst *L)
{
    CsNode *E = L->nd;
    short prev = -1;
    for (short k = L->head; k != -1; ) {
        if (E[k].code == 0x28) {
            short nx = E[k].next, c;
            if (prev == -1) c = nx == -1 ? 2 : (short)cs_code_b0e0(E[nx].code);
            else if (nx == -1) c = (short)cs_code_b0e0(E[prev].code);
            else if (cs_code_bc40(E[prev].code) && cs_code_bc40(E[nx].code)) c = (short)cs_code_af60(E[nx].code);
            else if (cs_code_a310(E[prev].code) || cs_code_a310(E[nx].code)) c = 0x14;
            else if (cs_code_afd0(E[prev].code) || cs_code_afd0(E[nx].code)) c = 2;
            else c = (short)cs_code_b0e0(E[nx].code);
            E[k].code = c;
        }
        prev = k;
        k = E[k].next;
    }
}

/* FUN_1801b4070(in, toff, n, table, out, &newoff, &err) (l.289934-290045): the input record's trailer at toff copied to the end of out with
   the new n-int table (the input's skipped): short, 3 rows, 4 doubles, the 0x18 tool block (also into DAT_1804e9288), 4 doubles, int,
   [int n_in, n_in ints skipped] int n + table, int containment (>= 0: n_in x 16 bytes), short a4 (>= 1 and n_in >= 1: n_in x 8 bytes) */
static int em_trailer(const CsEmCtx *X, int toff, int n, const int *table, CsBuf *out, int *newoff)
{
    const unsigned char *f = X->f;
    long o = toff;
    if (!rd_ok(X, o, 0xa6 + 4)) return -1;
    *newoff = (int)out->n;
    put(out, f + o, 0xa6); o += 0xa6;
    int nin = r32(f + o); o += 4;
    o += 4L * nin;
    put(out, &n, 4); put(out, table, 4 * (size_t)n);
    if (!rd_ok(X, o, 6)) return -1;
    int c = r32(f + o); put(out, f + o, 4); o += 4;
    if (c >= 0 && nin > 0) { if (!rd_ok(X, o, 16L * nin)) return -1; put(out, f + o, 16 * (size_t)nin); o += 16L * nin; }
    short a4 = r16(f + o); put(out, f + o, 2); o += 2;
    if (a4 >= 1 && nin >= 1) { if (!rd_ok(X, o, 8L * nin)) return -1; put(out, f + o, 8 * (size_t)nin); }
    return 0;
}

/* FUN_18018e630 for one direction (asm 18018e630..18018f0a0, l.266927-267367). dirB = p4 (1: the kind-7 record, the first call; 0: kind 6).
   tei = DAT_1804d7038; pool = DAT_1804e9320 (the store's size); L = the store (cs_est_init_cfb(L, pool)); arr / work two arrays of
   min(pool + 1000, 0x7fff) nodes (asm 18018e99e..18018e9c2, pl2StackAlloc in the DLL); otab = the output line table (DAT_1804e9318 /
   9310); *pos = DAT_1804e92b8. out = tmpemesh.tmp so far: dirB starts it (header, asm 18018e93a..18018e955 / FUN_18025d6d0 mode 0), dir A
   appends. Writes the blocks and the trailer; *toff = the trailer's position (newhdr +0x18 / +0xc). 0, an error of the DLL (< 0) or 1
   (NOT PORTED, *why). */
int cs_em_dir(const CsEmCtx *X, const CsCfbConst *C, int dirB, int tei, int pool, CsEst *L, CsNode *arr, CsNode *work, int *otab,
              CsBuf *out, double *pos, int *toff, CsEmTrace *T, const char **why)
{
    if (tei == 1) { *why = "DAT_1804d7038 = 1 (op +0x1268 = 1): FUN_1801a9e60 NOT PORTED"; return 1; }
    int n = dirB ? X->nB : X->nA;
    const int *itab = dirB ? X->tabB : X->tabA;
    short cap = (short)(pool + 1000 < 0x7fff ? pool + 1000 : 0x7fff);
    int d = dirB ? 1 : 0, a = dirB ? 0 : 1;           /* local_318 / local_308 (asm 18018e841..18018e8b1) */
    double gap = em_maxstep(X);                        /* xmm7 = FUN_1801ad940 (asm 18018e73a..18018e73f) */
    int off = (int)out->n;
    cs_est_reset(L);                                   /* FUN_1801a5570 */
    short sgn = 0;                                     /* r15w (asm 18018e6ab) */
    for (int line = 0; line < n; line++) {
        otab[line] = -1;
        em_read_line(X, L, C, itab[line], pos);
        if (L->err) { *why = "the node store DAT_1804e9328 overflowed (DAT_1804e9344 = 2): NOT PORTED"; return 1; }
        if (T && T->list) T->list(T, "EL", line, L);
        short na = (short)cs_cfb_to_array(L, C, 1, 0, cap, arr);
        if (na < 0) return -16;                        /* FUN_180268e20(3) */
        if (T && T->arr) T->arr(T, "EA", line, arr, na);
        if (na != 0) {
            if (dirB) for (short i = 0; i < na; i++) { arr[i].s0 = X->sB[0] * arr[i].s0; arr[i].s1 = X->sB[0] * arr[i].s1; }
            if (sgn == 0)
                for (short i = 0; i < na; i++)
                    if (fabs(arr[i].s0 - arr[i].s1) > C->systol) { sgn = arr[i].s1 - arr[i].s0 > 0.0 ? 1 : -1; break; }
            short ctr = 0, cnt = 0;
            double P[3] = { 0, 0, 0 }, Q[3] = { 0, 0, 0 }, E[3], E2[3];
            short r = em_gaps(d, gap, na, arr, *pos, &ctr, P, Q);
            while (r > -1) {
                short k;
                if (r == 0) { k = em_extend(X, L, C, a, (short)-sgn, P, 0, Q, cap, work, &cnt, E); if (T && T->ext) T->ext(T, line, r, k, cnt, E); }
                else if (r == 1) { k = em_extend(X, L, C, a, sgn, Q, 0, P, cap, work, &cnt, E); if (T && T->ext) T->ext(T, line, r, k, cnt, E); }
                else if (r == 2) {
                    k = em_extend(X, L, C, a, sgn, P, 1, Q, cap, work, &cnt, E);
                    if (T && T->ext) T->ext(T, line, r, k, cnt, E);
                    if (k != 2) {                      /* asm 18018ee07..18018ee68: back from Q to where the first stopped */
                        memcpy(E2, E, 24);
                        k = em_extend(X, L, C, a, (short)-sgn, Q, 1, E2, cap, work, &cnt, E);
                        if (T && T->ext) T->ext(T, line, 3, k, cnt, E);
                    }
                }
                if (L->err) { *why = "the node store DAT_1804e9328 overflowed (DAT_1804e9344 = 2): NOT PORTED"; return 1; }
                r = em_gaps(d, gap, na, arr, *pos, &ctr, P, Q);
            }
        }
        em_conn(L);
        if (T && T->list) T->list(T, "EC", line, L);
        cs_rgn_block(L, C, line, *pos, &off, otab, out);
        cs_est_free_line(L);
    }
    if (em_trailer(X, r32(X->f + (dirB ? 0x18 : 0xc)), n, otab, out, toff)) { *why = "temp.rgn trailer unreadable"; return 1; }
    return 0;
}

/* FUN_1801d3ca0's pool size DAT_1804e9320 for header kind 0x11 with method 2 (l.311487-311546): FUN_1801b5fb0 of each record (b6, b7 = the
   longest block chain + 10) plus its wall count a4 (DAT_1804e97a4 / DAT_1804d5830 = ctx+0x4a0, +5 when > 0), the larger (kind 7's + 100
   when larger), + max(nA, nB) + 100; then min(edge_fit(0x68) / 4, 0x7fff): edge_fit's bound depends on the licence-sized pl2 buffer -
   N13-28: the large buffer, taken as not binding. */
int cs_em_pool(int b6, int a4_6, int b7, int a4_7, int nA, int nB)
{
    int v = b6 + (a4_6 > 0 ? a4_6 + 5 : a4_6);         /* DAT_1804e97a4 = ctx +0xe0: a4 + 5 when > 0 (FUN_18015da80) */
    int a47 = a4_7 > 0 ? a4_7 + 5 : a4_7;              /* ctx +0x4a0 (FUN_18018f780 l.267800) */
    if (v < b7 + a47) v = b7 + a47 + 100;
    v = v + (nA < nB ? nB : nA) + 100;
    return v > 0x7fff ? 0x7fff : v;
}

/* DAT_1804d7038: msurf_find_tei_ctrl = op +0x1268, 3 when outside 1..3 (msurf_map l.340042-340044); FUN_180207f00 (l.340743-340762):
   > 2 becomes 2 when DAT_1805039d4 (the swap) is set, or the op type (ms_tp_type) is <= 0x2e and in the mask 0x48000000a280, or is 0x6d /
   0x6b, or is 0x2a with msurf_cutting_method 2; else 1 */
int cs_em_tei(int optype, int v1268, int swap, int cutmeth)
{
    short v = (short)v1268;
    if ((unsigned short)(v - 1) > 2) v = 3;
    if (v > 2) {
        unsigned short t = (unsigned short)optype;
        int two = swap || (t <= 0x2e && ((0x48000000a280ull >> (t & 0x3f)) & 1)) || t == 0x6d || t == 0x6b || (t == 0x2a && cutmeth == 2);
        v = two ? 2 : 1;
    }
    return v;
}
