/* cam3d_cscallop_6e60.c - Constant Scallop (op 43) stage D2: FUN_180146e60, the last pass of FUN_18015d0f0, which cuts every record where
 * it crosses the lines of the other direction (formats/surf_classic3.md 12.34).
 * Sources: objdump of mcmsurf.dll (asm addresses below; the decompile l.214170-214620 drops FUN_1801470e0's arguments at the call and
 * the jump tables of FUN_1801af5f0 / FUN_1801af0f0, which were read from the DLL file: 0x1801afa50, 0x1801af170). Constants from the DLL:
 * 1e-12 (0x1803eb800), 0.5 (0x1803eb810), 1.0 / -1.0 (0x1803eb820 / 0x1803eb888), 1.5 (0x180405548), 1e38 (0x1803ea528). Every
 * floating-point expression keeps the DLL's operand order (build with -ffp-contract=off).
 * Records are (direction, index) as in cam3d_cscallop_9a20.c. ctx +0x30a (DAT_1804d569a) is set to 1 by FUN_1801b1ad0 (asm 1801b2a9f)
 * just before it calls FUN_18015d0f0 (asm 1801b2ab0) for header kind 0x11 (op 0x2b): CsLoop.f30a; FUN_1801470e0 is its only reader in
 * FUN_18015d0f0's tree (the zone builder writes it again, asm 180187b59). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

#define BIG 1e38                     /* 0x1803ea528 */
#define NP (-100)                    /* NOT PORTED: a record whose +0x40 is NULL (FUN_1801a7fd0's windowed arm, N13-28) */
#define NONE 0xffdf                  /* FUN_180166e50's "no case" (asm 180166e6a) */
#define ND (-101)                    /* NOT PORTED: line header n's chain through freed memory (see OV below) */

/* The other direction's line header i. FUN_1801c65e0 lets an index reach n (b == n passes its test, asm 1801c6658 / 1801c6704), so
   FUN_180149e90 / FUN_1801470e0 read the header one past the array. FUN_1801b1ad0 allocates the headers (descriptor 0: ctx +0x338 x 0x20,
   FUN_1801ae960 asm 1801ae9ce) and the records (descriptor 1: x 0xb0) in one FUN_1800cdfa0 call of mode 2 (r14d = 2, asm 1801b1de5 /
   1801b2097 / 1801b228a), i.e. pl2StackAlloc first (mccore 1805903c0 -> 1805797c0: a bump allocator, base + used, no header, no
   alignment); with the large buffer (N13-28) header n is therefore the first 0x20 bytes of record 0 of that direction's array: +0 its
   next (first), +8 its prev (last), +0x10 / +0x12 / +0x14 bytes of its +0x10, +0x18 its +0x18 as the position (FUN_18018a640 zeroes +0x10
   / +0x18 of every record it opens, asm 18018abb9 / 18018abbd; FUN_1801ab500 copies them from records that have them 0). That memory may
   since have been freed (FUN_1801a9c80 leaves +0 / +8) and reused for a new record of either direction (the LIFO free list, CsFree):
   v->cd / the occupant tell which port record is there now; header n's chain then runs through that record's next (its stale one when
   freed) in the occupant's direction, and FUN_1801470e0 looks each record's line up in the other direction's headers as the DLL does.
   A chain that reaches a freed record after the first, or a line index past the headers, is refused (ND); so is header n when the
   free list is not modelled (D->fl NULL) and record 0 is not live. */
typedef struct { const CsSegDir *D; int n; CsSegLine x; int xok, cd; } OV;
static void ov_init(OV *v, CsLoop *L, const CsSegDir *D, int n)
{
    v->D = D; v->n = n; v->xok = 0; v->cd = D->dir;
    if (D->n < 1) return;
    int od = D->dir, oi = 0;
    if (D->fl) { od = D->fl->occ[D->dir][0]; oi = D->fl->occ[D->dir][1]; }
    const CsSegDir *E = L->D[od];
    const CsSeg *S0 = &E->S[oi];
    if (!D->fl) {
        if (S0->line < 0 || S0->line >= D->nl) return;
        for (int r = D->H[S0->line].first; r != -1; r = D->S[r].next)
            if (r == 0) { v->xok = 1; break; }
        if (!v->xok) return;
    }
    v->xok = 1; v->cd = od;
    v->x.first = S0->fr ? S0->sn : S0->next; v->x.last = S0->fr ? S0->sp : S0->prev;
    v->x.n10 = (short)(S0->z10 & 0xffff); v->x.n12 = (short)((S0->z10 >> 16) & 0xffff); v->x.dir = (unsigned char)((S0->z10 >> 32) & 0xff);
    memcpy(&v->x.pos, &S0->z18, 8);
}
static const CsSegLine *ov(const OV *v, int i)
{
    if (i >= 0 && i < v->n) return &v->D->H[i];
    if (i == v->n && v->xok) return &v->x;
    return NULL;
}

/* the work array of FUN_1801b1ab0 (asm 1801b1ab0: +0 = 1, +4 capacity, +8 count (short), +0x10 the data) */
typedef struct { int cap; short n; double *v; } Cuts;

/* FUN_18014c710(eps, a, b) (asm 18014c710): 0 when |a - b| < eps, 2 when b > a, else 3 (a > b) or 4 */
static int cmp_c710(double eps, double a, double b)
{
    double d = fabs(a - b);
    if (eps > d) return 0;
    if (b > a) return 2;
    return !(a > b) ? 4 : 3;           /* setbe after comisd a, b */
}
/* FUN_180166e50(eps, pr, k, v) (asm 180166e50..18016702d): the interval pr against the cut pair v[k], v[k + 1]; 1..13 or NONE */
static int cls_6e50(double eps, const double pr[2], int k, const double *v)
{
    int c1 = cmp_c710(eps, pr[1], v[k]), c2 = cmp_c710(eps, pr[0], v[k + 1]);
    int c3 = cmp_c710(eps, pr[1], v[k + 1]), c4 = cmp_c710(eps, pr[0], v[k]);
    int r = NONE;
    if (c4 == 0) {
        if (c3 == 0) return 6;
        if (c3 == 2) return 4;
        if (c3 == 3) return 0xc;
    } else if (c4 == 2) {
        if (c1 == 3) { if (c3 == 2) return 3; }
        else if (c1 == 0) r = 2;
        else if (c1 == 2) r = 1;
        if (c3 == 0) return 0xb;
        if (c3 == 3) return 0xd;
        if (r != NONE) return r;
    } else if (c4 == 3) {
        if (c2 == 2 && c3 == 3) return 8;
        if (c3 == 0) return 7;
        if (c3 == 2) return 5;
    }
    if (c2 == 0) return c3 == 3 ? 9 : r;   /* asm 180166fae.. */
    if (c2 == 3) return 0xa;
    return r;
}
/* FUN_1801af0f0(pr, k, W) (asm 1801af0f0): FUN_180166e50 with DAT_1804e9250, mapped by the jump table at 0x1801af170 */
static int cls_f0f0(const CsLoop *L, const double pr[2], int k, const Cuts *W)
{
    static const int map[13] = { 3, 3, 5, 6, 6, 6, 6, 4, 2, 2, 5, 4, 1 };
    int c = cls_6e50(L->t9250, pr, k, W->v);
    return c >= 1 && c <= 13 ? map[c - 1] : 0;
}
/* FUN_1801ada20(k, W) (asm 1801ada20): the next pair: 0 after -1, else k + 2; -1 when no pair is left */
static short next_da20(short k, const Cuts *W)
{
    if (W->n > 1 && k + 2 < W->n) return k < 0 ? 0 : (short)(k + 2);
    return -1;
}
/* FUN_180166480(a, b) (asm 180166480..18016654b): on one line, both with |+0xa0 - +0xa8| > 1e-12 and the same sense: -1 when b lies before
   a, 1 after, else 0 */
static int ord_6480(const CsSeg *a, const CsSeg *b)
{
    if (a->line != b->line) return 0;
    if (!(fabs(a->sa0 - a->sa8) > 1e-12)) return 0;
    int ua = a->sa8 > a->sa0;
    if (!(fabs(b->sa0 - b->sa8) > 1e-12)) return 0;
    int ub = b->sa8 > b->sa0;
    if (ua != ub) return 0;
    if (ua) {
        if (a->s0 > b->s1) return -1;
        return b->s0 > a->s1 ? 1 : 0;
    }
    if (b->s1 > a->s0) return -1;
    return a->s1 > b->s0 ? 1 : 0;
}
/* FUN_1801a9980(mode, x, lim, dir, ctx, R) (asm 1801a9980..1801a9ab2): the record's start (mode 0) or end (1) moved along its points to x
   (FUN_1801a9770 from that end towards the other) when the path is shorter than lim; FUN_1801ac3c0 = the record's +0x40 */
static int ext_9980(CsLoop *L, int mode, double x, double lim, int dir, CsSeg *R)
{
    double P0[2], P1[2];
    short k0, k1;
    if (mode == 0) { P0[0] = R->s0; P0[1] = R->z0; k1 = (short)(R->s5c - 1); k0 = R->s5a; P1[0] = R->s1; P1[1] = R->z1; }
    else { P0[0] = R->s1; P0[1] = R->z1; k1 = (short)(R->s5a + 1); k0 = R->s5c; P1[0] = R->s0; P1[1] = R->z0; }
    if (R->pt < 0) return NP;
    double (*pts)[2] = L->D[dir]->P.pt + R->pt;
    short idx = 0; double O[2] = { 0, 0 }, d = 0;
    if (cs_9a20_walk(L, x, k0, P0, k1, P1, pts, &idx, O, &d) != 1) return 0;
    int c = lim > d;
    if (c) {
        if (mode == 0) { R->s5a = idx; R->s0 = O[0]; R->z0 = O[1]; }
        else { R->s5c = idx; R->s1 = O[0]; R->z1 = O[1]; }
    }
    return c;
}
/* FUN_1801af5f0(W, dir, up, ctx, R) (asm 1801af5f0..1801afa4d): the record cut to the pairs of W. pr = (s0, s1) of R as it came (up) or
   (s1, s0); per pair FUN_1801af0f0 (jump table 0x1801afa50): 0 / 2 next pair; 1 both ends moved to the pair (the first time R itself,
   later a copy of R as it came, inserted by FUN_1801ab500 beside the last record, side FUN_180166480); 4 one end moved (R itself); 5 the
   other end moved, last pair (R itself the first time, else a copy); 3 / 6 stop. Always returns 0 (here NP when R has no points) */
static int cut_f5f0(CsLoop *L, const Cuts *W, int dir, int up, int r0, long *cov)
{
    CsSegDir *D = L->D[dir];
    CsSeg R0 = D->S[r0], C;
    int F = 0, last = r0, i30 = 0, i31 = 0;
    double pr[2];
    if (up) { pr[0] = R0.s0; pr[1] = R0.s1; } else { pr[0] = R0.s1; pr[1] = R0.s0; }
    short k = next_da20(-1, W);
    if (k <= -1) return 0;
    for (;;) {
        short k13 = k;
        int copy = 0, f15 = 0, f14 = 0, c = cls_f0f0(L, pr, k, W);
        CsSeg *T = NULL;
        if (cov) cov[40 + c]++;
        switch (c) {
        case 0: case 2:
            k = next_da20(k, W);
            if (k <= -1) return 0;
            continue;
        case 1:   /* asm 1801af780: both ends (the first call taken without the r15b test, asm 1801af947) */
            if (F) { C = R0; copy = 1; T = &C; } else { F = 1; T = &D->S[r0]; }
            f14 = 1; f15 = 1;
            if (up) { i30 = 0; i31 = 1; } else { i30 = 1; i31 = 0; }
            k = next_da20(k, W);
            break;
        case 4:   /* asm 1801af85c */
            T = &D->S[r0];
            if (up) { f14 = 1; i31 = 1; } else { f15 = 1; i30 = 1; }
            k = next_da20(k, W);
            F = 1;
            break;
        case 5:   /* asm 1801af896 */
            if (F) { C = R0; copy = 1; T = &C; } else T = &D->S[r0];
            if (up) { f15 = 1; i30 = 0; } else { f14 = 1; i31 = 0; }
            k = -1;
            break;
        default:
            return 0;
        }
        if (f15 && ext_9980(L, 0, W->v[k13 + i30], BIG, dir, T) == NP) return NP;
        if (f14 && ext_9980(L, 1, W->v[k13 + i31], BIG, dir, T) == NP) return NP;
        if (copy) {   /* asm 1801af9cf..1801afa04 */
            int m = ord_6480(&D->S[last], &C);
            if (!m) return 0;
            int n = cs_d15_insert(D, m, &C, last);
            if (n < 0) return -16;   /* the port's own memory (the DLL's pool does not run out with the large buffer, N13-28) */
            last = n;
        }
        if (k <= -1) return 0;
    }
}
/* FUN_1801561e0(step, n, A, W, &err) (asm 1801561e0..180156473): the cut positions from the crossings A[0..n-1] (A[0] / A[n-1] the record's
   ends): each run of steps shorter than tol + step gets a start before its first point and an end after its last (half the neighbouring
   step less 3 tol, at most step - 3 tol; the first / last step special), sorted ascending (FUN_1801aeac0, an insertion sort). Returns the
   steps not shorter; *err = FUN_180268e20(2) (-4) when W is full */
static short gaps_61e0(const CsLoop *L, double step, int n, const double *A, Cuts *W, short *err)
{
    short bx = 0;
    *err = 0; W->n = 0;
    double t3 = L->tol;
    t3 += L->tol; t3 += L->tol;
    double sg = A[1] > A[0] ? 1.0 : -1.0;
    int in = 0;
    for (int i = 0; i < n - 1; i++) {
        double x5 = A[i], t = L->tol, d = fabs(x5 - A[i + 1]);
        if (t + step > d) {
            if (in) continue;
            in = 1;
            if (W->n == W->cap) { *err = -4; return bx; }
            double v = i == 0 ? t3 + t3 : fabs(A[i - 1] - x5) * 0.5;
            double v1 = v - t3, v0 = step - t3, w = v1 > v0 ? v0 : v1;
            if (i == 1) {
                double x0 = fabs(A[0] - A[1]);
                if (x0 > step) w = step - t * 1.5; else w = x0 - t;
            }
            W->v[W->n++] = x5 - w * sg;
        } else {
            bx++;
            if (!in) continue;
            in = 0;
            if (W->n == W->cap) { *err = -4; return bx; }
            double w2 = d * 0.5 - t3, v0 = step - t3, w = w2 > v0 ? v0 : w2;
            if (i == n - 2 && d > step) w = step - t * 1.5;
            W->v[W->n++] = w * sg + x5;
        }
    }
    if (in) {
        if (W->n == W->cap) { *err = -4; return bx; }
        W->v[W->n++] = sg * t3 + A[n - 1];
    }
    if (W->n > 1 && W->v[0] > W->v[1])   /* FUN_1801aeac0 (asm 1801aeac0..1801aeb27) */
        for (short i = 1; i < W->n; i++) {
            double key = W->v[i];
            short j = (short)(i - 1);
            while (j >= 0 && W->v[j] > key) { W->v[j + 1] = W->v[j]; j--; }
            W->v[j + 1] = key;
        }
    return bx;
}
/* FUN_1801c65e0(P, ctx, &a, &b, &c, &d) (asm 1801c65e0..1801c676f): FUN_18015f990's lines, clamped into the grid instead of failing */
static void clamp_65e0(const CsEmCtx *X, const double P[3], int *a, int *b, int *c, int *d)
{
    int v = (int)floor((P[0] - X->firstA) / X->stepA);
    *a = v;
    if (0.0 > X->stepA) { *b = v; (*a)++; } else *b = v + 1;
    if (*a < 0 || *b <= 0) { if (0.0 > X->stepA) { *a = 1; *b = 0; } else { *a = 0; *b = 1; } }
    else if (*a >= X->nA || *b > X->nA) {
        if (0.0 > X->stepA) { *a = X->nA - 1; *b = X->nA - 2; } else { *a = X->nA - 2; *b = X->nA - 1; }
    }
    v = (int)floor((P[1] - X->firstB) / X->stepB);
    *c = v;
    if (0.0 > X->stepB) { *d = v; (*c)++; } else *d = v + 1;
    if (*c < 0 || *d <= 0) { if (0.0 > X->stepB) { *c = 1; *d = 0; } else { *c = 0; *d = 1; } }
    else if (*c >= X->nB || *d > X->nB) {
        if (0.0 > X->stepB) { *c = X->nB - 1; *d = X->nB - 2; } else { *c = X->nB - 2; *d = X->nB - 1; }
    }
}
void cs_d15_clamp(const CsEmCtx *X, const double P[3], int *a, int *b, int *c, int *d) { clamp_65e0(X, P, a, b, c, d); }
/* FUN_1801aad80(box, ctx, out) -> FUN_1801c9520 (asm 1801c9520..1801c9677): the lines of both corners (FUN_18015fba0 = jmp FUN_18015f990,
   else FUN_1801c65e0); out[0] / out[2] the B lines, out[1] / out[3] the A lines, spanning both corners. Always 1 */
static void box_ad80(CsLoop *L, const double box[4], int out[4])
{
    int a0, b0, c0, d0, a1, b1, c1, d1;
    double P[3] = { box[0], box[1], 0.0 };
    if (!cs_9a20_lines(L, P, &a0, &b0, &c0, &d0)) clamp_65e0(L->X, P, &a0, &b0, &c0, &d0);
    P[0] = box[2]; P[1] = box[3];
    if (!cs_9a20_lines(L, P, &a1, &b1, &c1, &d1)) clamp_65e0(L->X, P, &a1, &b1, &c1, &d1);
    if (a0 < b0) { out[3] = a1 < a0 ? a1 : a0; out[1] = b0 > b1 ? b0 : b1; }   /* cmovl / cmovg as asm 1801c95fe.. */
    else { out[3] = a0 > a1 ? a0 : a1; out[1] = b0 < b1 ? b0 : b1; }
    if (c0 < d0) { out[0] = c0 < c1 ? c0 : c1; out[2] = d0 > d1 ? d0 : d1; }
    else { out[0] = c0 > c1 ? c0 : c1; out[2] = d0 < d1 ? d0 : d1; }
}
void cs_d15_boxlines(CsLoop *L, const double box[4], int out[4]) { box_ad80(L, box, out); }
/* FUN_180149e90(R, a, b, ohdrs, out) (asm 180149e90..18014a010): from out[a] towards out[b] the first line whose position lies within
   1e-12 of R's s range, and from out[b] back towards out[a]; out[] narrowed to them: 3 both, 1 only from a, 2 only from b, 0 none */
static int span_9e90(const CsSeg *R, int a, int b, const OV *oH, int out[4])
{
    double lo = R->s1 > R->s0 ? R->s0 : R->s1, hi = R->s0 > R->s1 ? R->s0 : R->s1;
    int fa = 0, fb = 0, ia = 0, ib = 0, dba = out[b] - out[a];
    int s = dba < 0 ? -1 : 1;
    for (int i = out[a]; s > 0 ? i <= out[b] : i >= out[b]; i += s) {
        const CsSegLine *h = ov(oH, i);
        if (!h) return ND;
        double p = h->pos;
        if (p + 1e-12 > lo && hi > p - 1e-12) { fa = 1; ia = i; break; }
    }
    if (dba != 0) {
        int s2 = out[a] - out[b] < 0 ? -1 : 1;
        for (int i = out[b]; s2 > 0 ? i <= out[a] : i >= out[a]; i += s2) {
            const CsSegLine *h = ov(oH, i);
            if (!h) return ND;
            double p = h->pos;
            if (p + 1e-12 > lo && hi > p - 1e-12) { fb = 1; ib = i; break; }
        }
    }
    if (fa) { out[a] = ia; if (fb) { out[b] = ib; return 3; } return 1; }
    if (fb) { out[b] = ib; return 2; }
    return 0;
}
/* FUN_1801498e0(R, hdrs, O, ohdrs) (asm 1801498e0..180149972): R and O on lines of different kinds cross: each one's line position lies in
   the other's s range */
static int cross_98e0(const CsSeg *R, const CsSegLine *RH, const CsSeg *O, const CsSegLine *OH)
{
    if (OH->dir == RH->dir) return 0;
    double lo = R->s1 > R->s0 ? R->s0 : R->s1, hi = R->s0 > R->s1 ? R->s0 : R->s1, p = OH->pos;
    if (!(p >= lo) || !(hi >= p)) return 0;   /* comisd + jb: fails when below or unordered */
    lo = O->s1 > O->s0 ? O->s0 : O->s1; hi = O->s0 > O->s1 ? O->s0 : O->s1; p = RH->pos;
    if (!(p >= lo) || !(hi >= p)) return 0;
    return 1;
}
/* FUN_180147980(ctx, step^2, hdrs, R, &err) (asm 180147980..180147c64; FUN_1801470e0 calls it when ctx +0x30a is set and the record's s
   span is shorter than the other direction's step): 0 when R is longer than the step (FUN_180239160 = dz^2 + ds^2 of its ends). Else the
   points R's s0 / s1 at the line position + half the step (then - half, asm 180147ae1..180147b89) must both lie in the grid
   (FUN_1801716d0 = FUN_180176c90 (FUN_18015f990) + FUN_1801716b0 (the cell: the lower line of each pair)); none: 0. The same cell
   (FUN_18017c1a0: (nA - 1) x cell[1] + cell[0]): 1. Else the other-direction line the two cells share; none, or a line without records:
   0 (asm 180147c54: FUN_180268e20(3) is called but its value is dropped, *err stays 0); 1 when no record of it crosses R (FUN_1801498e0),
   else 0. 1 = R is freed by the caller */
static int short_7980(CsLoop *L, double step2, int dir, int r, short *err)
{
    const CsEmCtx *X = L->X;
    CsSegDir *D = L->D[dir];
    const CsSeg *R = &D->S[r];
    const CsSegLine *RH = &D->H[R->line];
    *err = 0;
    double dz = R->z0 - R->z1, ds = R->s0 - R->s1, dd = dz * dz;
    dd += ds * ds;
    if (dd > step2) return 0;
    int kind = RH->dir, si = kind == 0 ? 1 : 0, oi = kind == 0 ? 0 : 1, i21 = kind == 0 ? 2 : 0, i20 = kind == 0 ? 3 : 1;
    double half = fabs(kind == 0 ? X->stepB : X->stepA) * 0.5, pos = RH->pos;
    const CsSegDir *O = L->D[kind == 0 ? 1 : 0];
    double P[3], Q[3];
    P[si] = R->s0; P[2] = R->z0; Q[si] = R->s1; Q[2] = R->z1;
    int pa[4], qa[4], e;
    for (e = 0; e < 2; e++) {
        P[oi] = (e == 0 ? 1.0 : -1.0) * half + pos;
        Q[oi] = P[oi];
        if (cs_9a20_lines(L, P, &pa[0], &pa[1], &pa[2], &pa[3]) && cs_9a20_lines(L, Q, &qa[0], &qa[1], &qa[2], &qa[3])) break;
    }
    if (e == 2) return 0;
    unsigned short cp0 = (unsigned short)(pa[0] < pa[1] ? pa[0] : pa[1]), cp1 = (unsigned short)(pa[2] < pa[3] ? pa[2] : pa[3]);
    unsigned short cq0 = (unsigned short)(qa[0] < qa[1] ? qa[0] : qa[1]), cq1 = (unsigned short)(qa[2] < qa[3] ? qa[2] : qa[3]);
    if ((X->nA - 1) * (int)cp1 + (int)cp0 == (X->nA - 1) * (int)cq1 + (int)cq0) return 1;
    int line;
    if (pa[i21] == qa[i20]) line = pa[i21];
    else if (pa[i20] == qa[i21]) line = pa[i20];
    else return 0;
    if (O->H[line].first == -1) return 0;
    for (int o = O->H[line].first; o != -1; o = O->S[o].next)
        if (cross_98e0(R, RH, &O->S[o], &O->H[O->S[o].line])) return 0;
    return 1;
}
/* FUN_18014a070(side, R, hdrs) (asm 18014a070): with the op's z limits (DAT_18050341a) R's start / end refused by FUN_18014a020 -> 0 */
static int zok_a070(CsLoop *L, int side, int dir, int r)
{
    if (!L->zl) return 1;
    double P[3];
    cs_9a20_pt(L, side, dir, r, P);
    return cs_9a20_zok(L, P);
}
/* FUN_1801470e0(ctx, hdrs, R, desc, &err) (asm 1801470e0..1801476bb): the other direction's lines in a box around R (s range x line position
   +- (tol x 0.5 + tol)) narrowed to those R's s range reaches (FUN_180149e90, both ends needed); A0 = R's s0, the positions of the lines
   R crosses (an end line counts outright when its end point is refused by the z limits; else a record of it must cross R, FUN_1801498e0),
   R's s1. None crossed: R freed (FUN_1801a9c80(1, ..)), -1. Else FUN_1801561e0 into A1 (descriptor 1) and FUN_1801af5f0 cuts R.
   Returns FUN_1801561e0's count (0 when it stopped earlier) */
static int rec_70e0(CsLoop *L, int dir, int r, double *A0, int cap0, Cuts *W, short *err, long *cov)
{
    const CsEmCtx *X = L->X;
    CsSegDir *D = L->D[dir];
    *err = 0;
    const CsSeg *R = &D->S[r];
    const CsSegLine *RH = &D->H[R->line];
    int kind = RH->dir;
    if (kind != dir) { *err = NP; return 0; }                       /* a line header whose +0x14 is not its direction: not met */
    int od = kind == 0 ? 1 : 0, a = kind == 0 ? 0 : 1, b = kind == 0 ? 2 : 3;
    double step = fabs(kind == 0 ? X->stepB : X->stepA);
    CsSegDir *O = L->D[od];
    if (L->f30a && step > fabs(R->s1 - R->s0)) {   /* asm 1801471a6..180147210 */
        int f = short_7980(L, step * step, dir, r, err);
        if (cov) cov[f ? 71 : 70]++;
        if (*err || !f) return 0;
        cs_d15_free(D, r);
        return -1;
    }
    int up = cs_9a20_up(L, dir);
    double pos = RH->pos, d2 = L->tol * 0.5 + L->tol;
    double P0[2], P1[2];
    if (kind == 0) { P0[0] = pos + d2; P0[1] = R->s0; P1[0] = pos - d2; P1[1] = R->s1; }
    else { P0[0] = R->s0; P0[1] = pos + d2; P1[0] = R->s1; P1[1] = pos - d2; }
    double box[4];
    for (int c = 0; c < 2; c++) {
        box[c] = P1[c] > P0[c] ? P0[c] : P1[c];
        box[2 + c] = P0[c] > P1[c] ? P0[c] : P1[c];
    }
    int out[4];
    box_ad80(L, box, out);
    OV oH;
    ov_init(&oH, L, O, od ? X->nB : X->nA);
    int sp = span_9e90(R, a, b, &oH, out);
    if (sp == ND) { *err = ND; return 0; }
    if (out[a] == oH.n || out[b] == oH.n) { if (cov) cov[69]++; }
    if (sp < 3) { if (cov) cov[60]++; return 0; }
    if (out[b] == out[a]) { if (cov) cov[61]++; return 0; }
    int sgn = out[b] - out[a] < 0 ? -1 : 1;
    W->n = 0;                                                        /* FUN_1801b1ab0 on descriptor 1 */
    short ws[4] = { 0, 0, 0, 0 };
    if ((ov(&oH, out[b])->pos > ov(&oH, out[a])->pos) == up) { ws[a] = 0; ws[b] = 1; } else { ws[a] = 1; ws[b] = 0; }
    int k = 0;
    A0[k++] = R->s0;
    for (int i = out[a]; sgn > 0 ? i <= out[b] : i >= out[b]; i += sgn) {
        if ((i == out[a] || i == out[b]) && !zok_a070(L, ws[i == out[a] ? a : b], dir, r)) {
            if (cov) cov[62]++;
            A0[k++] = ov(&oH, i)->pos;
            if (k == cap0) { *err = -4; return 0; }
            continue;
        }
        const CsSegDir *C = i == oH.n ? L->D[oH.cd] : O;   /* the records header i's chain runs through (header n: OV) */
        for (int o = ov(&oH, i)->first; o != -1; o = C->S[o].next) {
            const CsSeg *Os = &C->S[o];
            if (Os->fr || Os->line < 0 || Os->line >= O->nl) { *err = ND; return 0; }
            if (cross_98e0(R, RH, Os, &O->H[Os->line])) {
                A0[k++] = O->H[Os->line].pos;
                if (k == cap0) { *err = -4; return 0; }
                break;
            }
        }
    }
    A0[k++] = R->s1;
    if (k == 2) { if (cov) cov[63]++; cs_d15_free(D, r); return -1; }
    short res = gaps_61e0(L, step, k, A0, W, err);
    if (cov) cov[64 + (res > 4 ? 4 : res)]++;
    if (!res || *err) return res;
    *err = (short)cut_f5f0(L, W, dir, up, r, cov);
    return res;
}
/* FUN_180146e60(dir, ctx, abort) (asm 180146e60..1801470dc): FUN_180169990 = two descriptors of (other count + 0x32) doubles (N13-31: the
   port's own arrays); per line, per record (the next one taken before the call, asm 180147030) FUN_1801470e0, stopping at an error. The
   status bar (mfc #8010 0x15b, every n / 10 lines) and ms_break are UI. cov (test counters, may be NULL): [40 + FUN_1801af0f0 case],
   [60..68] FUN_1801470e0's exits */
int cs_d15_6e60(CsLoop *L, int dir, long *cov)
{
    const CsEmCtx *X = L->X;
    CsSegDir *D = L->D[dir];
    int n = dir ? X->nB : X->nA, m = dir ? X->nA : X->nB, cap = m + 0x32;
    double *A0 = calloc((size_t)cap, sizeof *A0);
    Cuts W = { cap, 0, calloc((size_t)cap, sizeof(double)) };
    short err = 0;
    if (!A0 || !W.v) { free(A0); free(W.v); return 0; }               /* FUN_180169990 failed: nothing done, err 0 (asm 180146f0d) */
    for (int l = 0; l < n && !err; l++)
        for (int r = D->H[l].first; r != -1 && !err; ) {
            int nx = D->S[r].next;
            rec_70e0(L, dir, r, A0, cap, &W, &err, cov);
            r = nx;
        }
    free(A0); free(W.v);
    return err;
}

/* FUN_18014b350(dir, ctx) (asm 18014b350..18014b3bd): the largest |+0x78 - +0x88| (|z0 - z1|) over the direction's records, from 0.0 */
static double dz_b350(const CsSegDir *D, int n)
{
    double m = 0.0;
    for (int l = 0; l < n; l++)
        for (int r = D->H[l].first; r != -1; r = D->S[r].next) {
            double d = fabs(D->S[r].z0 - D->S[r].z1);
            if (d > m) m = d;
        }
    return m;
}
/* FUN_18015c8f0(ctx) (asm 18015c8f0..18015c9c3), ctx +0x309 == 0 (op 0x2b, FUN_18024d390, surf_classic3.md 12.30): the larger of
   FUN_18014b350(0) and (1). FUN_1801b1ad0 keeps the larger of it and DAT_1804d56c0 (ctx +0x330) in DAT_1804d56c0 (asm 1801b2ada..1801b2ae4).
   The ctx +0x309 arm (ctx +0x320, 1e-6 / pi/2 / 1e38 / CRT 0x1803c8192, ctx +0x2b8) is not ported (ops 0x28..0x2a) */
double cs_d15_c8f0(const CsLoop *L)
{
    double a = dz_b350(L->D[0], L->X->nA), b = dz_b350(L->D[1], L->X->nB);
    return a > b ? a : b;
}
