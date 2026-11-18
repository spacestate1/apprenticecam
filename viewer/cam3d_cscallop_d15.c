/* cam3d_cscallop_d15.c - Constant Scallop (op 43) stage D2: FUN_18015d0f0's callees on the 0xb0 segment records (formats/surf_classic3.md
 * 12.30). Sources: objdump of mcmsurf.dll (asm addresses below); the decompile (l.222007-222200, l.219779) drops arguments.
 * Records live in a growable array (CsSegDir); the DLL's 0xb0 pool (FUN_1801b86c0 / FUN_1801b8870, edge_alloc) is modelled by appending:
 * a record taken from the pool is overwritten whole (FUN_1801ab500 copies 0xb0 bytes), a freed one (FUN_1801a9c80(1, ..)) is only unlinked,
 * so only the lists' structure and the records' contents are Mastercam's, not their addresses. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

/* FUN_18015a310 (asm 18015a310): code - 0x12 <= 0x11 unsigned */
static int c_a310(short c) { return (unsigned)((int)c - 0x12) <= 0x11; }

/* FUN_180157620(cls, code) (asm 180157620..1801576bd, jump tables 0x1801576c0 / 0x1801576dc read from the DLL's bytes): code >= 0x27 is
   taken as code - 0x27; cls 0: code in {0,2,4,5,6,7,9,11,12,13,14,16}; cls 1: {1,3,8,10,15,17} or FUN_18015a310 (18..35); cls 2: code 0
   or 2; cls 3: 1, 3..17 or FUN_18015a310; any other cls: 0 */
int cs_cls_7620(int cls, int code)
{
    short c = (short)code;
    if (c >= 0x27) c = (short)(c - 0x27);
    switch ((short)cls) {
    case 0: { static const unsigned char t[17] = { 1,0,1,0,1,1,1,1,0,1,0,1,1,1,1,0,1 }; return (unsigned)(int)c <= 0x10 && t[c]; }
    case 1: { static const unsigned char t[17] = { 1,0,1,0,0,0,0,1,0,1,0,0,0,0,1,0,1 };   /* index code - 1; 0 -> FUN_18015a310 */
              unsigned i = (unsigned)((int)c - 1); return i <= 0x10 && t[i] ? 1 : c_a310(c); }
    case 2: return ((int)c & ~2) == 0;
    case 3: if (c == 1 || (unsigned)((int)c - 3) <= 0xe) return 1; return c_a310(c);
    }
    return 0;
}

/* FUN_18014b550(cls, start, n, codes, R) (asm 18014b550..18014b639): from start the points of class cls up to and including the first
   point after start that is not; R = {start, that index (or n - 1)} and returns R[1]; no point of the class, start out of [0, n), or R[1]
   == start: R = {-1, -1}, returns start */
static short run_b550(int cls, short start, short n, const short *codes, short R[2])
{
    if (start < 0 || start > n) { R[0] = R[1] = -1; return start; }
    R[1] = start; R[0] = start;
    short si = 0;
    if (start < n) {
        for (short bx = start; bx < n; bx++) {
            R[1] = bx;
            if (cs_cls_7620(cls, codes[bx])) si++;
            else if (bx > start) break;
        }
        if (si != 0 && R[1] != start) return R[1];
    }
    R[0] = R[1] = -1;
    return start;
}

/* the pool: a new record at the end of the array (the DLL pops DAT_1804e97b8, FUN_1801b8870) */
static int seg_new(CsSegDir *D)
{
    int pop[2] = { -1, -1 };
    if (D->fl && D->fl->n > 0) { D->fl->n--; pop[0] = D->fl->t[D->fl->n][0]; pop[1] = D->fl->t[D->fl->n][1]; }
    if (D->n >= D->cap) {
        int c = D->cap ? D->cap * 2 : 256;
        CsSeg *s = realloc(D->S, sizeof *s * (size_t)c);
        if (!s) return -1;
        D->S = s; D->cap = c;
    }
    memset(&D->S[D->n], 0, sizeof D->S[0]);
    if (D->fl) {
        for (int x = 0; x < 2; x++)
            if (pop[0] == D->fl->occ[x][0] && pop[1] == D->fl->occ[x][1]) { D->fl->occ[x][0] = D->dir; D->fl->occ[x][1] = D->n; }
        /* which memory the new record takes (surf_classic3.md 12.41): the popped record's (it keeps rf / rud / rui: a pointer to it now
           reaches the new record), else the next of the FUN_1801b86c0 pool (the harness links it in address order, asm 1801b8785..) */
        CsFree *F = D->fl;
        if (pop[0] >= 0 && F->S[pop[0]]) { CsSeg *P = &(*F->S[pop[0]])[pop[1]]; P->rf = 1; P->rud = D->dir; P->rui = D->n; }
        if (pop[0] < 0 && F->np >= F->pcap) {
            int c = F->pcap ? F->pcap * 2 : 256;
            int (*t)[2] = realloc(F->pool, sizeof *t * (size_t)c);
            if (!t) { free(F->pool); F->pool = NULL; F->np = F->pcap = 0; D->fl = NULL; return D->n++; }
            F->pool = t; F->pcap = c;
        }
        if (pop[0] < 0) { F->pool[F->np][0] = D->dir; F->pool[F->np][1] = D->n; F->np++; }
    }
    return D->n++;
}

/* FUN_1801a9c80(1, hdrs, R) (asm 1801a9c80..1801a9d41): R unlinked from its line's list, the counts +0x10 - 1 and +0x12 - 1 when > 0, R onto
   the free list (asm 1801a9d2a..1801a9d38: R +0x10 = the old head DAT_1804e97b8, head = R; R's own +0 / +8 are left as they were: kept in
   sn / sp, the port's next / prev cleared). D->fl (when set) records the push for FUN_1801b8870's pops (seg_new) */
static void seg_free(CsSegDir *D, int r)
{
    CsSeg *S = D->S, *R = &S[r];
    CsSegLine *L = &D->H[R->line];
    if (R->prev == -1) { L->first = R->next; if (R->next != -1) S[R->next].prev = -1; }
    else S[R->prev].next = R->next;
    if (R->next == -1) { L->last = R->prev; if (R->prev != -1) S[R->prev].next = -1; }
    else S[R->next].prev = R->prev;
    L->n10--;
    if (L->n12 > 0) L->n12--;
    R->sn = R->next; R->sp = R->prev; R->fr = 1; R->rf = 0;
    R->next = R->prev = -1;
    if (D->fl) {
        CsFree *F = D->fl;
        if (F->n >= F->cap) {
            int c = F->cap ? F->cap * 2 : 256;
            int (*t)[2] = realloc(F->t, sizeof *t * (size_t)c);
            if (!t) { free(F->t); F->t = NULL; D->fl = NULL; return; }   /* the port's memory: the model stops (header n then refused) */
            F->t = t; F->cap = c;
        }
        F->t[F->n][0] = D->dir; F->t[F->n][1] = r; F->n++;
    }
}

/* FUN_18014e050(prevB, A, nextB, pts, L) (asm 18014e050..18014e188): the record L becomes the run A ({+0x5a, +0x5c}, +0x70.. its start,
   +0x80.. its end, +0xa0 / +0xa8 their s) widened by the runs of the other class on either side ({+0x58, +0x5e}, +0x60 / +0x90 their
   points); fails when the runs do not touch (prevB end != A start, A end != nextB start) or A is not set */
static int run_e050(const short pB[2], const short pA[2], const short nB[2], const double (*pt)[2], CsSeg *L)
{
    if (pB[1] > -1 && pB[1] != pA[0]) return 0;
    if (pA[0] < 0 || pA[1] < 0) return 0;
    if (nB[0] > -1 && pA[1] != nB[0]) return 0;
    short c = pA[0], e = pA[1];
    if (pB[0] > -1) c = pB[0];
    if (nB[1] > -1) e = nB[1];
    L->s5a = pA[0]; L->s5c = pA[1];
    L->s0 = pt[pA[0]][0]; L->z0 = pt[pA[0]][1]; L->s1 = pt[pA[1]][0]; L->z1 = pt[pA[1]][1];
    L->sa0 = pt[pA[0]][0]; L->sa8 = pt[pA[1]][0];
    L->s58 = c; L->s5e = e;
    L->v60 = pt[c][0]; L->v68 = pt[c][1]; L->v90 = pt[e][0]; L->v98 = pt[e][1];
    return 1;
}

/* FUN_1801ab500(p3, L, hdrs, at) (asm 1801ab500..1801ab623): a pool record = L (0xb0 bytes), inserted after at (p3 == 1) or before; the
   line's counts +0x10 / +0x12 + 1. Returns its index (-1: no memory) */
static int seg_insert(CsSegDir *D, int p3, const CsSeg *L, int at)
{
    int r = seg_new(D);
    if (r < 0) return -1;
    CsSeg *S = D->S, *N = &S[r], *A = &S[at];
    *N = *L;
    CsSegLine *H = &D->H[A->line];
    if (p3 == 1) {
        N->prev = at; N->next = A->next; A->next = r;
        if (N->next == -1) H->last = r; else S[N->next].prev = r;
    } else {
        N->next = at; N->prev = A->prev; A->prev = r;
        if (N->prev == -1) H->first = r; else S[N->prev].next = r;
    }
    H->n10++; H->n12++;
    return r;
}

/* FUN_1801504e0(dir, ctx, p3 = 1, hdrs, R, &err) (asm 1801504e0..18015083a): the record R split into the runs of class A (ctx +0x2b0 == 2:
   cls 0, else 2) each widened by the runs of class B (1, else 3) beside it (FUN_18014b550 / FUN_18014e050): the first run rewrites R
   (FUN_180154b40: +0x5a, +0x5c, +0x70..+0x88, +0xa0, +0xa8, +0x58, +0x5e, +0x60, +0x68, +0x90, +0x98), each further one a new record
   after the last (FUN_1801ab500). A record whose first point is of neither class, or with no run of A, is freed (FUN_1801a9c80(1, ..));
   one with neither a run of B before nor after its first A run is kept as it is. Returns the runs written; *err = FUN_180268e20(0) (-11)
   when a run does not fit, FUN_180268e20(2) when no record is left. FUN_1801acb60 / FUN_1801ac3c0 return the record's +0x48 / +0x40
   (the in-memory arm; NULL there = the windowed arm: NOT PORTED, -1). */
static int split_4e0(CsSegDir *D, int r, int b2b0, short *err)
{
    int esi = 0;
    CsSeg *R = &D->S[r];
    if (R->cd < 0 || R->pt < 0) return -1;
    const short *codes = &D->P.cd[R->cd];
    const double (*pt)[2] = (const double (*)[2])&D->P.pt[R->pt];
    int clA = b2b0 == 2 ? 0 : 2, clB = b2b0 == 2 ? 1 : 3;
    short n = R->z3c, ax, R34[2], R30[2], R38[2], r13;
    if (cs_cls_7620(clB, codes[0])) {
        ax = run_b550(clB, 0, n, codes, R34);
        ax = run_b550(clA, ax, n, codes, R30);
        r13 = R34[0];
    } else if (cs_cls_7620(clA, codes[0])) {
        R34[0] = R34[1] = -1; r13 = -1;
        ax = run_b550(clA, 0, n, codes, R30);
    } else { seg_free(D, r); return 0; }
    if (R30[0] == -1 && R30[1] == -1) { seg_free(D, r); return 0; }
    short ax3c = run_b550(clB, ax, n, codes, R38);
    if (r13 == -1 && R38[0] == -1) return 0;
    CsSeg L = *R;
    int at = r;
    if (R30[0] <= -1) return 0;
    for (;;) {
        if (!run_e050(R34, R30, R38, pt, &L)) { *err = -11; return esi; }
        esi++;
        if (esi == 1) {
            R = &D->S[r];
            R->s5a = L.s5a; R->s5c = L.s5c; R->s0 = L.s0; R->z0 = L.z0; R->s1 = L.s1; R->z1 = L.z1; R->sa0 = L.sa0; R->sa8 = L.sa8;
            R->s58 = L.s58; R->s5e = L.s5e; R->v60 = L.v60; R->v68 = L.v68; R->v90 = L.v90; R->v98 = L.v98;
        } else {
            at = seg_insert(D, 1, &L, at);
            if (at < 0) { *err = -4; return esi; }   /* FUN_180268e20(2) = -4 */
        }
        R34[0] = R38[0]; R34[1] = R38[1];
        ax = run_b550(clA, ax3c, n, codes, R30);
        ax3c = run_b550(clB, ax, n, codes, R38);
        if (R30[0] <= -1) return esi;
    }
}

/* FUN_180150840(dir, ctx, &err) (asm 180150840..180150921): FUN_1801504e0 on every record of every line, the next one taken before the
   call (records it inserts are not revisited); stops at an error. Returns the sum of its returns. */
int cs_d15_split(CsSegDir *D, int b2b0, short *err)
{
    int cnt = 0;
    *err = 0;
    for (int l = 0; l < D->nl; l++)
        for (int r = D->H[l].first; r != -1; ) {
            int nx = D->S[r].next;
            int k = split_4e0(D, r, b2b0, err);
            if (k < 0) { *err = -100; return cnt; }   /* NOT PORTED: the windowed arrays */
            cnt += k;
            if (*err) return cnt;
            r = nx;
        }
    return cnt;
}

/* FUN_18014b260(dir, ctx, p4, R, Q) (asm 18014b260..18014b333): p4 = 1: Q = {R +0x5c - 1 (0 when <= 0), min(next +0x5a + 1, R +0x3c)}; p4 =
   -1: {prev +0x5c - 1 (0), min(R +0x5a + 1, R +0x3c)}; NULL without that neighbour or when Q[1] <= Q[0], else R's points (FUN_1801ac3c0) */
static const double (*gap_b260(const CsSegDir *D, int p4, int r, short Q[2]))[2]
{
    const CsSeg *S = D->S, *R = &S[r];
    short di, bx;
    if (p4 == 1) {
        if (R->next == -1) return NULL;
        di = (short)(R->s5c - 1 > 0 ? R->s5c - 1 : 0); bx = S[R->next].s5a;
    } else if (p4 == -1) {
        if (R->prev == -1) return NULL;
        di = (short)(S[R->prev].s5c - 1 > 0 ? S[R->prev].s5c - 1 : 0); bx = R->s5a;
    } else return NULL;
    bx = bx + 1 < R->z3c ? (short)(bx + 1) : R->z3c;
    if (bx <= di) return NULL;
    if (R->pt < 0) return NULL;
    Q[0] = di; Q[1] = bx;
    return (const double (*)[2])&D->P.pt[R->pt];
}

/* FUN_180156cf0(gap, dir, ctx, p4, R) (asm 180156cf0..180156f3e): with p4 = 1 the end of R (+0x80, +0x88) to the start of the next
   (+0x70, +0x78) (p4 = -1: the previous one's end to R's start): 1 when their distance <= gap and, along the points between
   (FUN_18014b260), the path from the end through the points Q[0] + 1 .. Q[1] - 1 to the start is shorter than gap (one step: the
   distance itself) */
static int gap_6cf0(const CsSegDir *D, double gap, int p4, int r)
{
    const CsSeg *S = D->S, *R = &S[r], *A, *B;
    if (p4 == 1) { if (R->next == -1) return 0; A = R; B = &S[R->next]; }
    else if (p4 == -1) { if (R->prev == -1) return 0; A = &S[R->prev]; B = R; }
    else return 0;
    double as = A->s1, az = A->z1, bs = B->s0, bz = B->z0;
    double d = sqrt((bz - az) * (bz - az) + (bs - as) * (bs - as));
    if (d > gap) return 0;
    short Q[2];
    const double (*pt)[2] = gap_b260(D, p4, r, Q);
    if (!pt) return 0;
    int k = Q[1] - Q[0];
    if (k == 1) return gap > d;
    if (k < 1) return 0;
    short i = (short)(Q[0] + 1);
    double t = sqrt((pt[i][1] - az) * (pt[i][1] - az) + (pt[i][0] - as) * (pt[i][0] - as));
    if (t > gap) return 0;
    for (short b = (short)(Q[0] + 2); b < Q[1]; b++) {
        double dz = pt[b][1] - pt[b - 1][1], ds = pt[b][0] - pt[b - 1][0];
        t += sqrt(dz * dz + ds * ds);
        if (t > gap) return 0;
    }
    double dz = bz - pt[Q[1] - 1][1], ds = bs - pt[Q[1] - 1][0];
    return gap > sqrt(dz * dz + ds * ds) + t;
}

/* FUN_18014d5c0(dir, ctx) (asm 18014d5c0..18014d6c1): per line, a record and the next one cut from the same segment (FUN_180155810: same
   +0x28, +0x2c, +0x40) with a short gap (FUN_180156cf0(ctx +0x2d8, dir, ctx, 1, R)) and FUN_1801574a0 true (ctx +0x309 = 0 for op 0x2b:
   FUN_18024d390 answers 1 only for ops 0x28 / 0x29 / 0x2a, so it returns 1 at asm 1801574c8) are merged by FUN_18014d6d0(1, hdrs, R)
   (asm 18014d6d0..18014d764: R takes the next one's +0x5c, +0x80, +0x88, +0xa8, +0x32, +0x38, +0x5e, +0x90, +0x98; the next one is freed)
   and R is tried again. Returns the merges. */
int cs_d15_merge(CsSegDir *D, double gap)
{
    int cnt = 0;
    for (int l = 0; l < D->nl; l++) {
        int r = D->H[l].first;
        while (r != -1 && D->S[r].next != -1) {
            CsSeg *R = &D->S[r], *N = &D->S[R->next];
            if (R->p28 == N->p28 && R->p2c == N->p2c && R->pt == N->pt && gap_6cf0(D, gap, 1, r)) {
                int nx = R->next;
                R->s5c = N->s5c; R->s1 = N->s1; R->z1 = N->z1; R->sa8 = N->sa8; R->s32 = N->s32; R->i38 = N->i38; R->s5e = N->s5e;
                R->v90 = N->v90; R->v98 = N->v98;
                seg_free(D, nx);
                cnt++;
            } else r = R->next;
        }
    }
    return cnt;
}

/* for cam3d_cscallop_9a20.c (FUN_180149a20's callees use the same two) */
int cs_d15_gap(const CsSegDir *D, double gap, int p4, int r) { return gap_6cf0(D, gap, p4, r); }   /* FUN_180156cf0 */
void cs_d15_free(CsSegDir *D, int r) { seg_free(D, r); }                                          /* FUN_1801a9c80(1, ..) */
int cs_d15_insert(CsSegDir *D, int p3, const CsSeg *L, int at) { return seg_insert(D, p3, L, at); }   /* FUN_1801ab500 */
