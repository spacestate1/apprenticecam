/* cam3d_cscallop_9a20.c - Constant Scallop (op 43) stage D2: FUN_180149a20, the pass of FUN_18015d0f0 that meets every record end with the
 * record of the other direction it crosses and extends the record to it or merges it with its neighbour (formats/surf_classic3.md 12.32).
 * Sources: objdump of mcmsurf.dll (asm addresses below; the decompile l.215586-217400 drops or garbles arguments in most of these), and
 * mccore's UnitNormal (1804b7480, decomp/mccore_filter.c l.4247), lp_ints (1804b3ed0, decomp/mccore_inters.c l.2806), proj_pt_ln_3d
 * (1804b42c0, decomp/draft/core_calcdim.c l.5703), pt_equal (mccore 180018c90, asm). Every floating-point expression keeps the DLL's
 * operand order and association (build with -ffp-contract=off).
 * Records are indices into the two CsSegDir arrays (direction 0 = A, 1 = B; a record's line header +0x14 = its direction, so "kind" =
 * the direction); the DLL's pointers become (direction, index), NULL = -1. FUN_1801574a0 and FUN_1801575e0 return 1 at once when ctx +0x309
 * == 0 (asm 1801574c8 / 1801575f1), which holds for op 0x2b (FUN_18024d390, 12.30); the other arm is not ported. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

#define BIG 1e38                     /* 0x47d2ced32a16a1b1 (0x1803ea528) */
#define NP (-100)                    /* NOT PORTED: a record whose +0x40 is NULL (FUN_1801a7fd0's windowed arm, N13-28) */

typedef struct { short i0; double p1[3], t1; short i28; double p2[3], t2, dist; } Cross;      /* the 0x58-byte result (+0, +8, +0x20, +0x28, +0x30, +0x48, +0x50) */
typedef struct { int a, b; short step; int n; unsigned char byte; double pos; double (*pts)[2]; } Poly;   /* +4, +8, +0xc, +0x10, +0x14, +0x18, +0x28 */
typedef struct { int rec; short side; double P[3]; unsigned char kind, flag; int oth; } Ent;   /* the 0x38 entry: +0, +8, +0x10, +0x28, +0x29, +0x30 */
typedef struct { unsigned char cdir, sdir; int cline, sline, count, cap; Ent *e; } Work;     /* CBlockBsda: +8, +0xc, +0x10, +0x14, +0x18, +0x1c, +0x20 */

static CsSeg *SR(CsLoop *L, int dir, int r) { return &L->D[dir]->S[r]; }
static double (*PTS(CsLoop *L, int dir, int r))[2]
{
    const CsSeg *R = SR(L, dir, r);
    return R->pt < 0 ? NULL : L->D[dir]->P.pt + R->pt;            /* FUN_1801ac3c0 (asm 1801ac3cf: +0x40 when set) */
}
static int same_5810(const CsSeg *a, const CsSeg *b)             /* FUN_180155810 (asm 180155810): same +0x28, +0x2c, +0x40 */
{
    return a && b && a->p28 == b->p28 && a->p2c == b->p2c && a->pt == b->pt;
}
static int idx3(int k) { return k == 2 ? 2 : k == 1 ? 1 : 0; }

/* FUN_18014a020 (asm 18014a020): with DAT_18050341a, a point within tol of either z limit is refused */
static int zok_a020(const CsLoop *L, const double P[3])
{
    if (!L->zl) return 1;
    if (L->tol > fabs(P[2] - L->z1)) return 0;
    if (L->tol > fabs(P[2] - L->z2)) return 0;
    return 1;
}
/* FUN_18017b860(n, hdrs) (asm 18017b860): over the first n - 1 lines the first record with |+0xa0 - +0xa8| > 1e-12: +0xa8 > +0xa0 */
static int up_b860(CsLoop *L, int dir)
{
    CsSegDir *D = L->D[dir];
    int n = dir ? L->X->nB : L->X->nA;
    for (int l = 0; l < n - 1; l++)
        for (int r = D->H[l].first; r != -1; r = D->S[r].next)
            if (fabs(D->S[r].sa0 - D->S[r].sa8) > 1e-12) return D->S[r].sa8 > D->S[r].sa0;
    return 0;
}
/* FUN_1801864b0(side, rec, hdrs, P) (asm 1801864b0): P[line dir] = the line position; P[dir == 0] and P[2] = the record's start (side 0)
   or end */
static void pt_64b0(CsLoop *L, int side, int dir, int r, double P[3])
{
    const CsSeg *R = SR(L, dir, r);
    const CsSegLine *H = &L->D[dir]->H[R->line];
    P[idx3(H->dir)] = H->pos;
    int j = H->dir == 0;
    if (side == 0) { P[j] = R->s0; P[2] = R->z0; }
    else { P[j] = R->s1; P[2] = R->z1; }
}
/* FUN_18015f990(P, ctx, &a, &b, &c, &d) (asm 18015f990..18015fb94): the A lines either side of P[0] (a, b) and the B lines of P[1] */
static int lines_f990(CsLoop *L, const double P[3], int *a, int *b, int *c, int *d)
{
    const CsEmCtx *X = L->X;
    double t = L->tol, s = X->stepA;
    if (s > 0.0) { if (X->firstA - t > P[0]) return 0; if (P[0] > t + X->lastA) return 0; }
    if (0.0 > s) { if (P[0] > t + X->firstA) return 0; if (X->lastA - t > P[0]) return 0; }
    int v = (int)floor((P[0] - X->firstA) / s);
    *a = v;
    if (0.0 > X->stepA) { *b = v; (*a)++; } else *b = v + 1;
    if (*a < 0 || *a >= X->nA || *b < 0 || *b >= X->nA) return 0;
    s = X->stepB;
    if (s > 0.0) { if (X->firstB - t > P[1]) return 0; if (P[1] > t + X->lastB) return 0; }
    if (0.0 > s) { if (P[1] > t + X->firstB) return 0; if (X->lastB - t > P[1]) return 0; }
    v = (int)floor((P[1] - X->firstB) / s);
    *c = v;
    if (0.0 > X->stepB) { *d = v; (*c)++; } else *d = v + 1;
    if (*c < 0 || *c >= X->nB || *d < 0 || *d >= X->nB) return 0;
    return 1;
}
/* pt_equal (mccore 180018c90, asm): |dx|, |dy|, |dz| < tol */
static int pt_eq(const CsLoop *L, const double *a, const double *b)
{
    return L->tol > fabs(a[0] - b[0]) && L->tol > fabs(a[1] - b[1]) && L->tol > fabs(a[2] - b[2]);
}
/* FUN_1801565a0(cls, P, ctx, &flag, &err) (asm 1801565a0..18015673c): FUN_18016e800 (asm 18016e800: {c, b, d, a} of FUN_18015f990); cls
   1 / 3: the A line L[cls] searched at P[1], cls 0 / 2: the B line at P[0]; the first record whose s range holds it; flag 0 when P =
   its start (FUN_180156bf0(3, 0, ..) = pt_equal), 1 its end, else 2; 10 when none. *odir = the table searched */
static int find_65a0(CsLoop *L, int cls, const double P[3], unsigned char *flag, short *err, int *odir)
{
    *flag = 10; *err = 0;
    int Li[4];
    if (!lines_f990(L, P, &Li[3], &Li[1], &Li[0], &Li[2])) return -1;
    int td, k;
    if ((((unsigned)cls - 1) & 0xfffd) == 0) { td = 0; k = 1; }
    else if (((unsigned)cls & 0xfffd) == 0) { td = 1; k = 0; }
    else { *err = -16; return -1; }
    *odir = td;
    CsSegDir *D = L->D[td];
    for (int r = D->H[Li[cls]].first; r != -1; r = D->S[r].next) {
        const CsSeg *R = &D->S[r];
        double lo = R->s1 > R->s0 ? R->s0 : R->s1, hi = R->s0 > R->s1 ? R->s0 : R->s1, c = P[k];
        if (c >= lo && hi >= c) {
            double Q[3];
            pt_64b0(L, 0, td, r, Q);
            if (pt_eq(L, P, Q)) { *flag = 0; return r; }
            pt_64b0(L, 1, td, r, Q);
            *flag = pt_eq(L, P, Q) ? 1 : 2;
            return r;
        }
    }
    return -1;
}
static int cls_of(int hdir, int side) { return hdir == 1 ? (side == 0 ? 1 : 3) : (side != 0 ? 2 : 0); }   /* asm 18014a99e..18014a9c5 */
/* FUN_18014a950(ctx, side, rec, hdrs, E) (asm 18014a950..18014aa4e) */
static short ent_a950(CsLoop *L, int side, int dir, int r, Ent *E)
{
    E->rec = -1;
    double P[3];
    pt_64b0(L, side, dir, r, P);
    int kind = L->D[dir]->H[SR(L, dir, r)->line].dir;
    unsigned char flag; short err = 0; int od = -1;
    int o = find_65a0(L, cls_of(kind, side), P, &flag, &err, &od);
    if (!err && o != -1) {
        E->rec = r; E->side = (short)side; memcpy(E->P, P, sizeof P); E->kind = (unsigned char)kind; E->oth = o; E->flag = flag;
        if (kind != dir || od != 1 - dir) return NP;   /* a line header whose +0x14 is not its direction: not met */
    }
    return err;
}
/* FUN_18014a950 for the zone walk (FUN_18016b4b0 asm 18016b5bf, surf_classic3.md 12.44): returns err != 0 -> 0; else E.rec == rec */
int cs_9a20_ent(CsLoop *L, int side, int dir, int r)
{
    Ent E;
    short e = ent_a950(L, side, dir, r, &E);
    if (e == NP) return NP;
    return e == 0 && E.rec == r;
}
/* FUN_18014aa50(ctx, W, &done, abort, &err) (asm 18014aa50..18014ae12): the entries of both ends of every record from W's line on (B
   first), up to W's stop; a full array grows once on the call's first line (FUN_180154a70: + 2 x the line's +0x10 + 100) and ends the
   gather after that line, elsewhere it ends the call with done = 0 (the line is gathered again next time). ms_break / the status bar
   (Ordinal_8010 0x15b, UpdateStatusBarText) are UI. Returns count > 0 */
static int gather_aa50(CsLoop *L, Work *W, int *done, short *err)
{
    int grew = 0;
    *err = 0; *done = 0;
    int dir = W->cdir, line0 = W->cline, line = line0;
    W->count = 0;
    for (;;) {
        int n = dir ? L->X->nB : L->X->nA;
        CsSegDir *D = L->D[dir];
        W->cdir = (unsigned char)dir;
        while (line < n) {
            W->cline = line;
            for (int r = D->H[line].first; r != -1; r = D->S[r].next)
                for (int side = 0; side < 2; side++) {
                    Ent *E = &W->e[W->count];
                    *err = ent_a950(L, side, dir, r, E);
                    if (*err) { W->count = 0; *done = 0; return 0; }
                    if (E->rec != -1 && ++W->count == W->cap) {
                        if (line0 != line) { *done = 0; return W->count > 0; }
                        int add = D->H[line].n10 * 2 + 100;
                        Ent *e = realloc(W->e, sizeof *e * (size_t)(W->cap + add));
                        if (!e) { *err = -16; W->count = 0; return 0; }
                        W->e = e; W->cap += add;
                        grew = 1;
                    }
                }
            line++;
            if ((line == W->sline && dir == W->sdir) || grew) { *done = 1; return W->count > 0; }
        }
        line = 0;
        dir = !dir;
        if (W->sline == 0 && dir == W->sdir) { *done = 1; return W->count > 0; }
    }
}
/* FUN_18014c740(rec, side, x) (asm 18014c740): x strictly inside the record's start extension (+0x60 .. +0x70) or end extension (+0x80 ..
   +0x90), longer than tol */
static int inext_c740(const CsLoop *L, const CsSeg *R, int side, double x)
{
    double a, b;
    if (side == 0) { a = R->v60; b = R->s0; } else if (side == 1) { a = R->s1; b = R->v90; } else return 0;
    double lo = b > a ? a : b, hi = a > b ? a : b;
    return fabs(hi - lo) > L->tol && x > lo && hi > x;
}
/* FUN_180156890(rec, side) (asm 180156890): 4 when +0xa0 == +0xa8; 5 without the neighbour on that side; up (+0xa8 > +0xa0): side 1 -> 0,
   side 0 -> 3; down: side 1 -> 1, side 0 -> 2 */
static int nbk_6890(const CsSeg *R, int side)
{
    int up;
    if (R->sa8 > R->sa0) up = 1; else if (R->sa0 > R->sa8) up = 0; else return 4;
    int nb;
    if (side == 0) nb = R->prev; else if (side == 1) nb = R->next; else return 5;
    if (nb == -1) return 5;
    if (up) return side == 1 ? 0 : 3;
    return (side != 1) + 1;
}
/* FUN_180156740(rec, side, .., x) (asm 180156740): x strictly inside the gap to the neighbour cut from the same segment, longer than tol */
static int ingap_6740(CsLoop *L, int dir, int r, int side, double x)
{
    const CsSeg *R = SR(L, dir, r), *N;
    double hi, lo;
    switch (nbk_6890(R, side)) {
    case 0: N = SR(L, dir, R->next); lo = R->s1; hi = N->s0; break;
    case 1: N = SR(L, dir, R->next); hi = R->s1; lo = N->s0; break;
    case 2: N = SR(L, dir, R->prev); lo = R->s0; hi = N->s1; break;
    case 3: N = SR(L, dir, R->prev); hi = R->s0; lo = N->s1; break;
    default: return 0;
    }
    if (!same_5810(R, N)) return 0;
    return fabs(hi - lo) > L->tol && x > lo && hi > x;
}
/* FUN_1801a9770(x, k0, P0, k1, P1, pts, &idx, O, &d) (asm 1801a9770..1801a996d): from P0 along pts[k0 + s] .. to P1 (s = +1 when k0 < k1,
   else -1; P1 stands for pts[k1 + s]) the first step whose s range holds x: idx = the step's first index, O = (x, z interpolated), d =
   the path length P0 -> O. 0 when |P1.s - P0.s| < DAT_1804e9250 or none */
static short walk_9770(const CsLoop *L, double x, short k0, const double P0[2], short k1, const double P1[2], double (*pts)[2], short *idx,
                       double O[2], double *d)
{
    short si = k0 < k1 ? 1 : -1, di = (short)(k1 + si);
    *d = 0.0;
    double ps = P0[0], pz = P0[1], dd = P1[0] - P0[0];
    if (L->t9250 > fabs(dd)) return 0;
    int up = dd > 0.0;
    short bx = (short)(k0 + si);
    for (;;) {
        if (!((si > 0 && bx <= di) || (si < 0 && bx >= di))) return 0;
        double qs = ps, qz = pz;
        if (bx == di) { ps = P1[0]; pz = P1[1]; } else { ps = pts[bx][0]; pz = pts[bx][1]; }
        int f = up ? (x >= qs && ps >= x) : (x >= ps && qs >= x);
        if (f) {
            *idx = (short)(bx - si);
            double a = x - qs;
            O[0] = x;
            O[1] = a / (ps - qs) * (pz - qz) + qz;
            double b = O[1] - qz;
            *d = sqrt(b * b + a * a) + *d;
            return 1;
        }
        double e = ps - qs, f2 = pz - qz;
        *d = sqrt(e * e + f2 * f2) + *d;
        bx = (short)(bx + si);
    }
}
/* FUN_1801556c0(rec, side, pts, x) (asm 1801556c0..180155805): x in the gap to the neighbour of the same segment: both records' extensions
   end at the point of the gap's path at x (FUN_1801a9770 from the neighbour's end) */
static int gapext_56c0(CsLoop *L, int dir, int r, int side, double (*pts)[2], double x)
{
    CsSeg *R = SR(L, dir, r), *N;
    int k = nbk_6890(R, side), nb;
    short r9, r10; double A[2], B[2];
    if (k <= 1) {
        nb = R->next; N = SR(L, dir, nb);
        A[0] = R->s1; A[1] = R->z1; r10 = R->s5c; r9 = N->s5a; B[0] = N->s0; B[1] = N->z0;
    } else if (k <= 3) {
        nb = R->prev; N = SR(L, dir, nb);
        r9 = R->s5a; r10 = N->s5c; A[0] = N->s1; A[1] = N->z1; B[0] = R->s0; B[1] = R->z0;
    } else return 0;
    r10 = (short)(r10 - 1);
    if (!same_5810(R, N)) return 0;
    short kk; double Pt[2], d;
    if (walk_9770(L, x, r10, A, r9, B, pts, &kk, Pt, &d) != 1) return 0;
    if (k > 1) {
        R->s58 = kk; R->v60 = Pt[0]; R->v68 = Pt[1];
        N->s5e = (short)(kk + 1); N->v90 = Pt[0]; N->v98 = Pt[1];
    } else {
        R->v90 = Pt[0]; R->v98 = Pt[1]; R->s5e = (short)(kk + 1);
        N->s58 = kk; N->v60 = Pt[0]; N->v68 = Pt[1];
    }
    return 1;
}
/* FUN_180154ac0(rec, side, pts) (asm 180154ac0): without a same-segment neighbour on that side the extension is reset to the segment's
   first (side 0) or last point */
static void extreset_4ac0(CsLoop *L, int dir, int r, int side, double (*pts)[2])
{
    CsSeg *R = SR(L, dir, r);
    int nb = side == 1 ? R->next : R->prev;
    if (same_5810(R, nb == -1 ? NULL : SR(L, dir, nb))) return;
    if (side == 0) { R->s58 = 0; R->v60 = pts[0][0]; R->v68 = pts[0][1]; }
    else if (side == 1) { R->s5e = (short)(R->z3c - 1); R->v90 = pts[R->s5e][0]; R->v98 = pts[R->s5e][1]; }
}
/* FUN_180148140(side, rec) (asm 180148140..18014822f): the same-segment neighbour's extension pulled back to the record's new end */
static void nbext_8140(CsLoop *L, int dir, int side, int r)
{
    CsSeg *R = SR(L, dir, r), *N;
    int k = nbk_6890(R, side);
    if (k == 0 || k == 1) { if (R->next == -1) return; N = SR(L, dir, R->next); }
    else if (k == 2 || k == 3) { if (R->prev == -1) return; N = SR(L, dir, R->prev); }
    else return;
    if (!same_5810(R, N)) return;
    if ((k == 0 && R->s1 > N->v60) || (k == 1 && N->v60 > R->s1)) {
        N->s58 = (short)(R->s5c - 1); N->v60 = R->s1; N->v68 = R->z1;
    } else if ((k == 3 && N->v90 > R->s0) || (k == 2 && R->s0 > N->v90)) {
        N->s5e = (short)(R->s5a + 1); N->v90 = R->s0; N->v98 = R->z0;
    }
}
/* FUN_180251bf0(P, k, m, pts) (asm 180251bf0): the parameter of P's foot on pts[k] -> pts[k + m] (0 when shorter than tol) */
static double par_1bf0(const CsLoop *L, const double *P, short k, short m, double (*pts)[2])
{
    double ds = pts[k + m][0] - pts[k][0], dz = pts[k + m][1] - pts[k][1];
    double den = dz * dz + ds * ds;
    if (den > L->tol * L->tol) return ((P[1] - pts[k][1]) * dz + (P[0] - pts[k][0]) * ds) / den;
    return 0.0;
}
/* FUN_180150930(ctx, E, pts, R, &err) (asm 180150930..180150ac7): the crossing at an end of the other record (flag 0 / 1): R from both
   records' ends */
static int atend_0930(CsLoop *L, const Ent *E, double (*pts)[2], Cross *X, short *err)
{
    *err = 0;
    if (E->flag >= 2) return 0;
    int dir = E->kind, od = 1 - dir;
    const CsSeg *R = SR(L, dir, E->rec), *O = SR(L, od, E->oth);
    double A[2], B[2];
    if (E->side == 0) { X->i0 = R->s5a; A[0] = R->s0; A[1] = R->z0; } else { X->i0 = (short)(R->s5c - 1); A[0] = R->s1; A[1] = R->z1; }
    memcpy(X->p1, E->P, sizeof X->p1);
    X->t1 = par_1bf0(L, A, X->i0, 1, pts);
    if (E->flag == 0) { X->i28 = O->s5a; B[0] = O->s0; B[1] = O->z0; } else { X->i28 = (short)(O->s5c - 1); B[0] = O->s1; B[1] = O->z1; }
    pt_64b0(L, E->flag, od, E->oth, X->p2);
    double (*po)[2] = PTS(L, od, E->oth);
    if (!po) { *err = NP; return 0; }
    X->t2 = par_1bf0(L, B, X->i28, 1, po);
    double dx = X->p2[0] - X->p1[0], dy = X->p2[1] - X->p1[1], dz = X->p2[2] - X->p1[2];
    X->dist = sqrt(dx * dx + dy * dy + dz * dz);
    return 1;
}
/* FUN_180156b60(ctx, Q, side, rec, hdrs) (asm 180156b60): a record of the other direction holds Q (FUN_1801565a0) */
static int hit_6b60(CsLoop *L, const double Q[3], int side, int dir, int r)
{
    unsigned char fl; short err = 0; int od;
    int o = find_65a0(L, cls_of(L->D[dir]->H[SR(L, dir, r)->line].dir, side), Q, &fl, &err, &od);
    return !err && o != -1;
}
/* FUN_180235b00(S, x, &out) (asm 180235b00..180235c97): where x lies on S's points (by s): 1 beyond the end (out = b + step), -1 before
   the start (out = a - step), 0 found by bisection (out = the step's first index, + 1 when step = -1), 2 otherwise */
static short where_5b00(const Poly *S, double x, int *out)
{
    double pa = S->pts[S->a][0], pb = S->pts[S->b][0];
    int dil;
    if (pb > pa) {
        if (S->step == 1) dil = 0; else if (S->step == -1) dil = 1; else return 2;
        if (x > pb) { *out = S->step + S->b; return 1; }
        if (pa > x) { *out = S->a - S->step; return -1; }
    } else {
        if (S->step == 1) dil = 2; else if (S->step == -1) dil = 3; else return 2;
        if (x > pa) { *out = S->a - S->step; return -1; }
        if (pb > x) { *out = S->step + S->b; return 1; }
    }
    int hi = S->a > S->b ? S->a : S->b, lo = S->a < S->b ? S->a : S->b;
    int inc = S->pts[hi][0] > S->pts[lo][0];
    if (lo == hi) return 2;
    hi--;
    while (lo <= hi) {
        int m = (hi + lo) / 2;
        if (inc) {
            if (S->pts[m][0] > x) { hi = m - 1; continue; }
            if (x > S->pts[m + 1][0]) { lo = m + 1; continue; }
        } else {
            if (x > S->pts[m][0]) { hi = m - 1; continue; }
            if (S->pts[m + 1][0] > x) { lo = m + 1; continue; }
        }
        *out = ((dil - 1) & 0xfd) == 0 ? m + 1 : m;
        return 0;
    }
    return 2;
}
/* FUN_18023d740(k, S, C) (asm 18023d740): S's point a (k 0) or b as 3D: C[byte] = pos, the other = s, C[2] = z */
static void p3_d740(int k, const Poly *S, double C[3])
{
    int i = k ? S->b : S->a, r9, r11;
    if (S->byte == 0) { r9 = 1; r11 = 2; } else if (S->byte == 1) { r9 = 0; r11 = 2; } else { r9 = 0; r11 = 1; }
    C[idx3(S->byte)] = S->pos;
    C[idx3(r9)] = S->pts[i][0];
    C[idx3(r11)] = S->pts[i][1];
}
/* FUN_180237580(mode, tol, A, B) (asm 180237580) */
static int near_7580(const CsLoop *L, int mode, double t, const double *A, const double *B)
{
    switch (mode) {
    case 1: return t > fabs(A[0] - B[0]);
    case 2: return t > fabs(A[0] - B[0]) && t > fabs(A[1] - B[1]);
    case 3: return pt_eq(L, B, A);
    case 4: return t > fabs(A[1] - B[1]);
    case 5: return t > fabs(A[2] - B[2]);
    }
    return 0;
}
/* FUN_1802390a0(mode, A, B) (asm 1802390a0) */
static double dist_90a0(int mode, const double *A, const double *B)
{
    double x, y, z;
    switch (mode) {
    case 1: return fabs(A[0] - B[0]);
    case 2: x = B[0] - A[0]; y = B[1] - A[1]; return sqrt(x * x + y * y);
    case 3: x = B[0] - A[0]; y = B[1] - A[1]; z = B[2] - A[2]; return sqrt(x * x + y * y + z * z);
    case 4: return fabs(A[1] - B[1]);
    case 5: return fabs(A[2] - B[2]);
    }
    return BIG;
}
/* FUN_180249ef0(U, k, m, pts, O, &t) (asm 180249ef0..18024a08f): U's foot on pts[k] -> pts[k + m], clamped to it */
static int foot_9ef0(const double U[2], short k, short m, double (*pts)[2], double O[2], double *t)
{
    int km = (int)k + (int)m;
    double ds = pts[km][0] - pts[k][0], dz = pts[km][1] - pts[k][1], as = U[0] - pts[k][0], az = U[1] - pts[k][1];
    double den = dz * dz + ds * ds;
    if (den == 0.0) return 0;
    double tt = (az * dz + as * ds) / den;
    *t = tt;
    double u = 1.0 - tt;
    O[0] = u * pts[k][0] + tt * pts[km][0];
    O[1] = u * pts[k][1] + tt * pts[km][1];
    if (0.0 > *t) { O[0] = pts[k][0]; O[1] = pts[k][1]; *t = 0.0; }
    else if (*t > 1.0) { O[0] = pts[km][0]; O[1] = pts[km][1]; *t = 1.0; }
    return 1;
}
typedef struct { int i; double t, s, z, dist; } Near;   /* FUN_180249c50's W: +0, +8, +0x10, +0x18, +0x28 */
/* FUN_180249c50(U, big, g100, S, W) (asm 180249c50..180249eee): the nearest point of S's steps to U (2D), stopping below g100 or once a
   foot is more than big away in s; none: S's first point */
static int nearest_9c50(const double U[2], double big, double g100, const Poly *S, Near *W)
{
    if (!(S->n > 0)) return 0;
    int mx = S->b; if (S->a > S->b) mx = S->a;
    int r13 = mx + 10;
    W->i = r13; W->dist = BIG;
    short bp = (short)S->a;
    for (short k = 1; k <= S->n; k++) {
        double O[2], t;
        if (foot_9ef0(U, bp, S->step, S->pts, O, &t)) {
            double dz = O[1] - U[1], ds = O[0] - U[0], dd = sqrt(dz * dz + ds * ds);
            if (W->dist > dd) {
                W->dist = dd; W->s = O[0]; W->z = O[1];
                if (S->step == -1) { W->i = bp - 1; W->t = 1.0 - t; } else { W->i = bp; W->t = t; }
                if (g100 > W->dist) break;
            }
            if (fabs(O[0] - U[0]) > big) break;
        }
        bp = (short)(bp + S->step);
    }
    if (W->i == (short)r13) {
        double dz = S->pts[S->a][1] - U[1], ds = S->pts[S->a][0] - U[0];
        W->dist = sqrt(dz * dz + ds * ds);
        W->s = S->pts[S->a][0]; W->z = S->pts[S->a][1];
        if (S->step == -1) { W->t = 1.0; W->i = S->a - 1; } else { W->t = 0.0; W->i = S->a; }
    }
    return 1;
}
/* FUN_180148960(g, g/2, g/100, P, O, S1, S2, R) (asm 180148960..180148ca7): the crossing near an end (a or b) of S2: R = that end and the
   nearest point of S1 */
static int endnear_8960(CsLoop *L, double g, double g2, double g100, const double P[3], const double O[2], const Poly *S1, const Poly *S2,
                        Cross *R)
{
    int k1 = S1->byte, r12 = k1 == 0;
    double T[3];
    T[idx3(k1)] = S1->pos; T[idx3(r12)] = O[0]; T[2] = O[1];
    double best = BIG, C[2][3];
    int sel = 10, r13 = S2->byte == 0 ? 4 : 1;   /* asm 180148a18..180148a34: test (byte == 0); cmove r13d, 1 */
    for (int i = 0; i < 2; i++) {
        p3_d740(i, S2, C[i]);
        int b = near_7580(L, r13, g, P, C[i]), a = near_7580(L, 5, g2, P, C[i]), mode;
        if (b) { if (a) { sel = i; break; } mode = r13; }
        else { if (!a) continue; mode = 5; }
        double dd = dist_90a0(mode, P, C[i]);
        if (best > dd) { best = dd; sel = i; }
    }
    if (sel >= 2) return 0;
    double U[2] = { T[idx3(r12)], C[sel][2] };
    Near W;
    int ok = nearest_9c50(U, BIG, g100, S1, &W);
    if (ok) {
        R->p1[idx3(k1)] = T[idx3(k1)]; R->p1[idx3(r12)] = T[idx3(r12)]; R->p1[2] = C[sel][2];
        R->i0 = (short)W.i; R->t1 = W.t;
        memcpy(R->p2, C[sel], sizeof R->p2);
        if (sel == 0) { R->t2 = 0.0; R->i28 = (short)S2->a; } else { R->t2 = 1.0; R->i28 = (short)((short)S2->b - S2->step); }
        double dx = R->p2[0] - R->p1[0], dy = R->p2[1] - R->p1[1], dz = R->p2[2] - R->p1[2];
        R->dist = sqrt(dx * dx + dy * dy + dz * dz);
    }
    return ok;
}
/* ---- mccore's 3D helpers (as cam3d_cscallop_sect.c, with the outputs these callers read) */
static int unit_n(const CsLoop *L, const double *a, const double *b, double *o)    /* UnitNormal (mccore 1804b7480) */
{
    double ox = b[2] * a[1] - b[1] * a[2], oy = b[0] * a[2] - a[0] * b[2], oz = a[0] * b[1] - b[0] * a[1];
    double l = sqrt(ox * ox + oy * oy + oz * oz), tl = L->tol;
    int err = 0;
    if (l < tl) {
        double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        if (tl <= la) { double lb = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]); err = (tl <= lb) + 2; } else err = 1;
    }
    double d = l < 1e-12 ? 1e-12 : l;
    o[0] = ox / d; o[1] = oy / d; o[2] = oz / d;
    return err;
}
static int lp_ints(const CsLoop *L, const double *n, const double *p, const double *Ln, double *o, double *t)   /* lp_ints (mccore 1804b3ed0) */
{
    double tl = L->tol;
    double dx = Ln[3] - Ln[0], dy = Ln[4] - Ln[1], dz = Ln[5] - Ln[2];
    double d = n[1] * dy + n[0] * dx + n[2] * dz;
    if (fabs(d) <= tl * tl && tl * tl != fabs(d)) {
        double ln = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), ll = sqrt(dx * dx + dy * dy + dz * dz);
        if (ln == 0.0 || ll == 0.0 || fabs(d) / (ll * ln) < tl) return 0;
    }
    double u = (n[0] * (p[0] - Ln[0]) + n[1] * (p[1] - Ln[1]) + n[2] * (p[2] - Ln[2])) / d;
    *t = u;
    o[0] = dx * u + Ln[0]; o[1] = dy * u + Ln[1]; o[2] = dz * u + Ln[2];
    return 1;
}
static int proj_ln3(const CsLoop *L, const double *P, const double *Ln, double *o, double *t)   /* proj_pt_ln_3d (mccore 1804b42c0) */
{
    double dx = Ln[3] - Ln[0], dy = Ln[4] - Ln[1], dz = Ln[5] - Ln[2];
    double l = sqrt(dy * dy + dx * dx + dz * dz);
    if (!(L->tol <= l)) return 0;
    double u = ((P[1] - Ln[1]) * dy + (P[0] - Ln[0]) * dx + (P[2] - Ln[2]) * dz) / (l * l);
    *t = u;
    double w = 1.0 - u;
    o[0] = w * Ln[0] + u * Ln[3]; o[1] = w * Ln[1] + u * Ln[4]; o[2] = w * Ln[2] + u * Ln[5];
    return 1;
}
/* FUN_18002fbd0 (asm 18002fbd0): v normalised (0 stays 0); FUN_180236cb0(c, v, o) (asm 180236cb0): normalised when every |component| <= c */
static void small_6cb0(double c, const double *v, double *o)
{
    if (fabs(v[0]) > c || fabs(v[1]) > c || fabs(v[2]) > c) { o[0] = v[0]; o[1] = v[1]; o[2] = v[2]; return; }
    double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l == 0.0) { o[0] = o[1] = o[2] = 0.0; return; }
    o[0] = v[0] / l; o[1] = v[1] / l; o[2] = v[2] / l;
}
/* FUN_1802409e0(A, B, P1, &t1, P2, &t2) (asm 1802409e0..180240b9d): the closest points of the lines A and B (common normal N, the planes
   N x dB / N x dA met with A / B by lp_ints); UnitNormal's error, 4 when lp_ints fails */
static int lines_09e0(const CsLoop *L, const double *A, const double *B, double *P1, double *t1, double *P2, double *t2)
{
    double dA[3] = { A[3] - A[0], A[4] - A[1], A[5] - A[2] }, dB[3] = { B[3] - B[0], B[4] - B[1], B[5] - B[2] }, N[3], M[3], Q[3];
    const double *vb;
    int e;
    if (unit_n(L, dA, dB, N) == 0) {
        if ((e = unit_n(L, N, dA, M))) return e;
        vb = dB;
        if ((e = unit_n(L, N, vb, Q))) return e;
    } else {
        double nA[3], nB[3];
        small_6cb0(0.05, dA, nA); small_6cb0(0.05, dB, nB);   /* 0x1803f04d0 */
        if ((e = unit_n(L, nA, nB, N))) return e;
        if ((e = unit_n(L, N, nA, M))) return e;
        if ((e = unit_n(L, N, nB, Q))) return e;
    }
    if (!lp_ints(L, Q, B, A, P1, t1)) return 4;
    if (!lp_ints(L, M, A, B, P2, t2)) return 4;
    return 0;
}
/* FUN_18024aec0(P, Ln, Q, &t) (asm 18024aec0): P's foot on the segment, clamped */
static int foot_aec0(const CsLoop *L, const double *P, const double *Ln, double *Q, double *t)
{
    if (!proj_ln3(L, P, Ln, Q, t)) return 0;
    if (*t > 1.0) { *t = 1.0; memcpy(Q, Ln + 3, 3 * sizeof *Q); }
    else if (0.0 > *t) { *t = 0.0; memcpy(Q, Ln, 3 * sizeof *Q); }
    return 1;
}
/* FUN_1802532e0(A, B, P1, &t1, P2, &t2) (asm 1802532e0..1802535ce): the closest points of the segments A and B */
static int segs_32e0(const CsLoop *L, const double *A, const double *B, double *P1, double *t1, double *P2, double *t2)
{
    int e = lines_09e0(L, A, B, P1, t1, P2, t2);
    if (e) return e;
    int c1 = 0, c2 = 0;
    if (0.0 > *t1) { memcpy(P1, A, 3 * sizeof *P1); *t1 = 0.0; c1 = 1; }
    else if (*t1 > 1.0) { memcpy(P1, A + 3, 3 * sizeof *P1); *t1 = 1.0; c1 = 1; }
    if (0.0 > *t2) { memcpy(P2, B, 3 * sizeof *P2); *t2 = 0.0; c2 = 1; }
    else if (*t2 > 1.0) { memcpy(P2, B + 3, 3 * sizeof *P2); *t2 = 1.0; c2 = 1; }
    double Q[3], t;
    if (c1) {
        double dx = P2[0] - P1[0], dy = P2[1] - P1[1], dz = P2[2] - P1[2];
        if (foot_aec0(L, P1, B, Q, &t)) {
            double ey = Q[1] - P1[1], ex = Q[0] - P1[0], ez = Q[2] - P1[2];
            double d1 = sqrt(ey * ey + ex * ex + ez * ez), d0 = sqrt(dx * dx + dy * dy + dz * dz);
            if (d0 > d1) { memcpy(P2, Q, sizeof Q); *t2 = t; }
        }
    }
    if (c2) {
        double dx = P2[0] - P1[0], dy = P2[1] - P1[1], dz = P2[2] - P1[2];
        if (foot_aec0(L, P2, A, Q, &t)) {
            double ey = Q[1] - P2[1], ex = Q[0] - P2[0], ez = Q[2] - P2[2];
            double d1 = sqrt(ey * ey + ex * ex + ez * ez), d0 = sqrt(dy * dy + dx * dx + dz * dz);
            if (d0 > d1) { memcpy(P1, Q, sizeof Q); *t1 = t; }
        }
    }
    return 0;
}
/* FUN_18021e150(I, T1, J, T2) (asm 18021e150): the last four steps were a 2-cycle */
static int cyc_e150(const CsLoop *L, const int *I, const double *T1, const int *J, const double *T2)
{
    return I[0] == I[2] && I[1] == I[3] && J[0] == J[2] && J[1] == J[3] && L->tol > fabs(T1[0] - T1[2]) && L->tol > fabs(T1[1] - T1[3])
           && L->tol > fabs(T2[0] - T2[2]) && L->tol > fabs(T2[1] - T2[3]);
}
/* FUN_180248a70(i, S1, j, S2, g10, g01, R) (asm 180248a70..180249019): from steps i / j, the closest segment pair by stepping along both
   polylines (t > 0.999 / < 0.001 moves on), R the best; stops below g01, after two non-improving steps beyond g10, at a fixed point, a
   2-cycle (FUN_18021e150) or n1 x n2 steps */
static int walk_8a70(CsLoop *L, short i, const Poly *S1, short j, const Poly *S2, double g10, double g01, Cross *R)
{
    int k1 = S1->byte, k2 = S2->byte, r14 = k1 == 0, r15 = k2 == 0, found = 0, iter = 0, maxit = S1->n * S2->n;
    int lo1, hi1, lo2, hi2;
    if (S1->step == 1) { lo1 = S1->a; hi1 = S1->b; } else { lo1 = S1->b; hi1 = S1->a; }
    if (S2->step == 1) { lo2 = S2->a; hi2 = S2->b; } else { lo2 = S2->b; hi2 = S2->a; }
    double A[6], B[6];
    A[idx3(k1)] = S1->pos; A[3 + idx3(k1)] = S1->pos; B[idx3(k2)] = S2->pos; B[3 + idx3(k2)] = S2->pos;
    double prev = BIG;
    unsigned char nh = 0;
    int cnt = 0, I = i, J = j, HI[4], HJ[4];
    double HT1[4], HT2[4];
    if (maxit <= 0) return 0;
    for (;;) {
        A[idx3(r14)] = S1->pts[I][0]; A[2] = S1->pts[I][1]; A[3 + idx3(r14)] = S1->pts[I + 1][0]; A[5] = S1->pts[I + 1][1];
        B[idx3(r15)] = S2->pts[J][0]; B[2] = S2->pts[J][1]; B[3 + idx3(r15)] = S2->pts[J + 1][0]; B[5] = S2->pts[J + 1][1];
        double P1[3], t1, P2[3], t2;
        if (segs_32e0(L, A, B, P1, &t1, P2, &t2)) return 0;
        double dy = P2[1] - P1[1], dx = P2[0] - P1[0], dz = P2[2] - P1[2];
        double dd = sqrt(dy * dy + dx * dx + dz * dz), pv = prev;
        prev = dd;
        if (R->dist > dd) {
            R->dist = dd; found = 1; cnt = 0; R->i0 = (short)I; R->i28 = (short)J;
            memcpy(R->p2, P2, sizeof P2); memcpy(R->p1, P1, sizeof P1); R->t1 = t1; R->t2 = t2;
            if (g01 > dd) return found;
        }
        if (pv > dd) cnt = 0;
        else { cnt++; if (cnt > 1 && dd > g10) return found; }
        int I2 = I, J2 = J;
        if (t1 > 0.999) { I2 = I + 1; if (hi1 - 1 < I + 1) I2 = hi1 - 1; }        /* 0x1804181e8, 0x1803f3ee8 */
        else if (0.001 > t1) { I2 = I - 1; if (lo1 > I - 1) I2 = lo1; }
        if (t2 > 0.999) { J2 = J + 1; if (hi2 - 1 < J + 1) J2 = hi2 - 1; }
        else if (0.001 > t2) { J2 = J - 1; if (lo2 > J - 1) J2 = lo2; }
        if (I == I2 && J == J2) return found;
        if (nh < 4) { HI[nh] = I2; HJ[nh] = J2; HT1[nh] = t1; HT2[nh] = t2; nh++; }
        if (nh == 4) { if (cyc_e150(L, HI, HT1, HJ, HT2)) return found; nh = 0; }
        if (++iter >= maxit) return found;
        I = I2; J = J2;
    }
}
/* FUN_1802537e0(box, P) (asm 1802537e0): the outcode of P against box {x0, y0, x1, y1} */
static int code_37e0(const double *bx, const double *P)
{
    int c = 0;
    if (bx[0] - P[0] > 1e-12) c = 1; else if (P[0] - bx[2] > 1e-12) c = 2;
    if (bx[1] - P[1] > 1e-12) return c | 4;
    if (P[1] - bx[3] > 1e-12) c |= 8;
    return c;
}
/* FUN_180237220(box, C0, C1) (asm 180237220..180237470): -1 when both ends are outside on one side, 0 inside, else 1 with the ends outside
   in x (or in y when |dx| < 0.0005) moved onto the box edge (that coordinate and z) */
static int clip_7220(const double *bx, double *C0, double *C1)
{
    int v = fabs(C0[0] - C1[0]) < 0.0005;   /* 0x180418198 */
    int c0 = code_37e0(bx, C0), c1 = code_37e0(bx, C1);
    if (c0 & c1) return -1;
    if (!(c0 | c1)) return 0;
    int n = 0, W[2];
    double X[2];
    if (!v) {
        if (c0 & 1) { X[0] = bx[0]; n = 1; W[0] = 1; } else if (c0 & 2) { X[0] = bx[2]; n = 1; W[0] = 1; }
        if (c1 & 1) { X[n] = bx[0]; W[n] = 2; n++; } else if (c1 & 2) { X[n] = bx[2]; W[n] = 2; n++; }
    } else {
        if (c0 & 4) { X[0] = bx[1]; n = 1; W[0] = 1; } else if (c0 & 8) { X[0] = bx[3]; n = 1; W[0] = 1; }
        if (c1 & 4) { X[n] = bx[1]; W[n] = 2; n++; } else if (c1 & 8) { X[n] = bx[3]; W[n] = 2; n++; }
    }
    int c = v;
    for (int k = 0; k < n; k++) {
        double x = X[k], z = (x - C0[c]) / (C1[c] - C0[c]) * (C1[2] - C0[2]) + C0[2];
        if (W[k] == 1) { C0[c] = x; C0[2] = z; } else { C1[c] = x; C1[2] = z; }
    }
    return 1;
}
/* FUN_18023fbd0(A, k2, pos2, w, g01, S2, R) (asm 18023fbd0..180240160): the closest segment of S2 (within w of its first point) to the
   segment A; none: the nearer end of A to S2's first point */
static int segpoly_fbd0(const CsLoop *L, const double *A, int k2, double pos2, double w, double g01, const Poly *S2, Cross *R)
{
    if (!(S2->n > 0)) return 0;
    int mx = S2->b; if (S2->a > S2->b) mx = S2->a;
    short m10 = (short)(mx + 10);
    R->i0 = m10; R->i28 = m10;
    int cl = k2 == 0;
    double B[6];
    B[idx3(k2)] = pos2; B[3 + idx3(k2)] = pos2;
    R->dist = BIG;
    int I = S2->a;
    for (int cnt = 1; cnt <= S2->n; cnt++, I += S2->step) {
        B[idx3(cl)] = S2->pts[I][0]; B[2] = S2->pts[I][1]; B[3 + idx3(cl)] = S2->pts[I + S2->step][0]; B[5] = S2->pts[I + S2->step][1];
        double P1[3], t1, P2[3], t2;
        if (segs_32e0(L, A, B, P1, &t1, P2, &t2)) continue;
        if (fabs(S2->pts[S2->a][0] - P2[idx3(cl)]) > w) break;
        double dy = P2[1] - P1[1], dx = P2[0] - P1[0], dz = P2[2] - P1[2], dd = sqrt(dy * dy + dx * dx + dz * dz);
        if (R->dist > dd) {
            R->dist = dd; R->i0 = 0; memcpy(R->p1, P1, sizeof P1); memcpy(R->p2, P2, sizeof P2); R->t1 = t1;
            if (S2->step == -1) { R->t2 = 1.0 - t2; R->i28 = (short)((short)I + S2->step); } else { R->i28 = (short)I; R->t2 = t2; }
            if (g01 > dd) break;
        }
        if (fabs(S2->pts[S2->a][0] - S2->pts[I][0]) > w) break;
    }
    if (R->i28 == m10) {
        double Q[2], z = S2->pts[S2->a][1];
        Q[idx3(k2)] = pos2; Q[idx3(cl)] = S2->pts[S2->a][0];
        double ay = Q[1] - A[1], ax = Q[0] - A[0], az = z - A[2], by = Q[1] - A[4], bx = Q[0] - A[3], bz = z - A[5];
        double d0 = sqrt(ay * ay + ax * ax + az * az), d1 = sqrt(by * by + bx * bx + bz * bz);
        R->i0 = 0;
        if (d1 > d0) { R->dist = d0; memcpy(R->p1, A, 3 * sizeof *A); R->t1 = 0.0; }
        else { R->dist = d1; memcpy(R->p1, A + 3, 3 * sizeof *A); R->t1 = 1.0; }
        R->p2[idx3(k2)] = pos2; R->p2[idx3(cl)] = S2->pts[S2->a][0]; R->p2[2] = z;
        if (S2->step == -1) { R->t2 = 1.0; R->i28 = (short)(S2->a - 1); } else { R->t2 = 0.0; R->i28 = (short)S2->a; }
    }
    return 1;
}
/* FUN_1802485d0(box1, S1, box2, S2, g10, g01, R) (asm 1802485d0..180248a5c): each segment of S1 clipped to the box (S1's line +- 4 tol,
   box1 in s) against S2 (FUN_18023fbd0); stops below g01 or once the segment leaves box1 */
static int boxwalk_85d0(CsLoop *L, const double *box1, const Poly *S1, const double *box2, const Poly *S2, double g10, double g01, Cross *R)
{
    int k1 = S1->byte, bl = k1 == 0, found = 0;
    double w2 = box2[1] - box2[0], t4 = L->tol * 4.0, BX[4], C[6];   /* 0x1803eba70 */
    BX[k1 == 1 ? 1 : 0] = S1->pos - t4;
    BX[bl == 1 ? 1 : 0] = box1[0];
    BX[k1 == 1 ? 3 : 2] = S1->pos + t4;
    BX[bl == 1 ? 3 : 2] = box1[1];
    C[idx3(k1)] = S1->pos; C[3 + idx3(k1)] = S1->pos;
    int I = S1->a;
    if (S1->n < 1) return 0;
    for (int cnt = 1; ; ) {
        C[idx3(bl)] = S1->pts[I][0]; C[2] = S1->pts[I][1]; C[3 + idx3(bl)] = S1->pts[I + S1->step][0]; C[5] = S1->pts[I + S1->step][1];
        if (clip_7220(BX, C, C + 3) > -1) {
            Cross Lc;
            Lc.dist = R->dist;
            if (segpoly_fbd0(L, C, S2->byte, S2->pos, w2, g01, S2, &Lc) && R->dist > Lc.dist) {
                found = 1;
                *R = Lc;
                if (S1->step == -1) { R->t1 = 1.0 - Lc.t1; R->i0 = (short)(S1->step + (short)I); } else R->i0 = (short)I;
                if (g01 > R->dist) return found;
            }
        }
        double c0 = C[idx3(bl)], c1 = C[3 + idx3(bl)];
        if (fabs(c0 - c1) > L->tol) {
            if (c1 > c0) { if (box1[0] > c1) return found; if (c0 > box1[1]) return found; }
            else { if (box1[0] > c0) return found; if (c1 > box1[1]) return found; }
        }
        I += S1->step;
        if (++cnt > S1->n) return found;
    }
}
/* the unit oracle (D15_UDUMP=file, surf_classic3.md 12.32): each FUN_180148960 / FUN_180248a70 / FUN_1802485d0 call's inputs ("UI ..")
   and the port's result ("UO ..") written to the file; tools/peloader --lmm D15 with D15_UCALL=file runs the DLL's function on every "UI"
   line and prints its "UO" */
static FILE *g_ud; static int g_un;
static void ud_poly(const Poly *S, int npts)
{
    fprintf(g_ud, " | %d %d %d %d %d %a %d", S->a, S->b, S->step, S->n, S->byte, S->pos, npts);
    for (int i = 0; i < npts; i++) fprintf(g_ud, " %a %a", S->pts[i][0], S->pts[i][1]);
}
static void ud_out(int ret, const Cross *R)
{
    fprintf(g_ud, "UO %d %d", g_un++, ret);
    if (ret) fprintf(g_ud, " %d %a %a %a %a %d %a %a %a %a %a", R->i0, R->p1[0], R->p1[1], R->p1[2], R->t1, R->i28, R->p2[0], R->p2[1], R->p2[2],
                     R->t2, R->dist);
    fprintf(g_ud, "\n");
}
/* FUN_180148f90(ctx, x, E, O, pts, R, &err) (asm 180148f90..18014946b): the record's continuation past the crossing (S1: the extension's
   points up to x) against the other record (S2: its points within max(|stepA|, |stepB|) of the line, its ends as the record's end
   points): FUN_180148960, else FUN_180248a70, else (or farther than g + tol) FUN_1802485d0 */
static int cont_8f90(CsLoop *L, double x, const Ent *E, const double O[2], double (*pts)[2], Cross *R, short *err)
{
    *err = 0;
    int dir = E->kind, od = 1 - dir, ret = 0, t;
    CsSeg *Rr = SR(L, dir, E->rec), *Oo = SR(L, od, E->oth);
    double lpos = L->D[dir]->H[Rr->line].pos, opos = L->D[od]->H[Oo->line].pos;
    int up = Rr->sa8 > Rr->sa0, mode;
    if (up) mode = E->side == 0 ? 0 : E->side == 1 ? 1 : 4; else mode = E->side == 0 ? 2 : E->side == 1 ? 3 : 4;
    Poly S1 = { 0 }, S2 = { 0 };
    S1.byte = (unsigned char)(E->kind == 1); S1.pos = lpos; S1.pts = pts;
    if (mode == 4) { *err = -99; return 0; }      /* 0xff9d */
    if (mode == 1 || mode == 3) {
        S1.a = Rr->s5c - 1; S1.step = 1; S1.b = Rr->s5e; S1.n = S1.b - S1.a;
        if (where_5b00(&S1, x, &t) == 0) { S1.b = S1.step + t; S1.n = S1.b - S1.a; }
    } else {
        S1.a = Rr->s5a + 1; S1.b = Rr->s58; S1.step = -1; S1.n = S1.a - S1.b;
        if (where_5b00(&S1, x, &t) == 0) { S1.b = S1.step + t; S1.n = S1.a - S1.b; }
    }
    double (*po)[2] = PTS(L, od, E->oth);
    if (!po) { *err = NP; return 0; }
    S2.a = Oo->s5a; S2.b = Oo->s5c; S2.step = 1; S2.n = S2.b - S2.a; S2.byte = (unsigned char)(E->kind != 1); S2.pos = opos; S2.pts = po;
    int upo = Oo->sa8 > Oo->sa0;
    double sv0[2] = { po[Oo->s5a][0], po[Oo->s5a][1] }, sv1[2] = { po[Oo->s5c][0], po[Oo->s5c][1] };
    po[Oo->s5a][0] = Oo->s0; po[Oo->s5a][1] = Oo->z0;
    po[Oo->s5c][0] = Oo->s1; po[Oo->s5c][1] = Oo->z1;
    double m = fabs(L->X->stepA), mb = fabs(L->X->stepB);
    if (!(m > mb)) m = mb;
    double lo = lpos - m, hi = m + lpos, xa = upo ? lo : hi, xb = upo ? hi : lo;
    if (where_5b00(&S2, xa, &t) == 0) S2.a = t;
    if (where_5b00(&S2, xb, &t) == 0) S2.b = S2.step + t;
    S2.n = S2.b - S2.a;
    double g = L->g;
    int n1 = SR(L, dir, E->rec)->z3c, n2 = Oo->z3c;
    if (!g_ud && getenv("D15_UDUMP")) g_ud = fopen(getenv("D15_UDUMP"), "w");
    if (g_ud) { fprintf(g_ud, "UI E %a %a %a %a %a %a %a %a", g, g * 0.5, g * 0.01, E->P[0], E->P[1], E->P[2], O[0], O[1]); ud_poly(&S1, n1); ud_poly(&S2, n2); fprintf(g_ud, "\n"); }
    ret = endnear_8960(L, g, g * 0.5, g * 0.01, E->P, O, &S1, &S2, R);      /* 0x1803eb810, 0x1803f3ef0 */
    if (g_ud) ud_out(ret, R);
    if (ret) L->cov[20]++;
    if (!ret) {
        double g10 = g * 10.0, g01 = g * 0.1;                                   /* 0x1803ed280, 0x1803f04d8 */
        short i0 = (short)(S1.a < S1.b ? S1.a : S1.b), j0 = (short)(S2.a < S2.b ? S2.a : S2.b);
        R->dist = BIG;
        if (g_ud) { fprintf(g_ud, "UI W %d %d %a %a", i0, j0, g10, g01); ud_poly(&S1, n1); ud_poly(&S2, n2); fprintf(g_ud, "\n"); }
        ret = walk_8a70(L, i0, &S1, j0, &S2, g10, g01, R);
        if (g_ud) {
            ud_out(ret, R);
            /* unit oracle only: FUN_1802485d0 on the same runs with the boxes below, whether or not it is reached (its result unused) */
            double a = S1.pts[S1.a][0], b = S1.pts[S1.b][0], c = S2.pts[S2.a][0], d = S2.pts[S2.b][0];
            double b1[2] = { b > a ? a : b, a > b ? a : b }, b2[2] = { d > c ? c : d, c > d ? c : d };
            Cross T; memset(&T, 0, sizeof T); T.dist = BIG;
            fprintf(g_ud, "UI B %a %a %a %a %a %a", b1[0], b1[1], b2[0], b2[1], g10, g01); ud_poly(&S1, n1); ud_poly(&S2, n2); fprintf(g_ud, "\n");
            ud_out(boxwalk_85d0(L, b1, &S1, b2, &S2, g10, g01, &T), &T);
        }
        if (ret && !(R->dist > g + L->tol)) L->cov[21]++;
        if (!ret || R->dist > g + L->tol) {
            double a = S1.pts[S1.a][0], b = S1.pts[S1.b][0], c = S2.pts[S2.a][0], d = S2.pts[S2.b][0];
            double b1[2] = { b > a ? a : b, a > b ? a : b }, b2[2] = { d > c ? c : d, c > d ? c : d };
            R->dist = BIG;
            if (g_ud) { fprintf(g_ud, "UI B %a %a %a %a %a %a", b1[0], b1[1], b2[0], b2[1], g10, g01); ud_poly(&S1, n1); ud_poly(&S2, n2); fprintf(g_ud, "\n"); }
            ret = boxwalk_85d0(L, b1, &S1, b2, &S2, g10, g01, R);
            if (g_ud) { ud_out(ret, R); fflush(g_ud); }
            L->cov[ret ? 22 : 23]++;
        }
    }
    po[Oo->s5a][0] = sv0[0]; po[Oo->s5a][1] = sv0[1];
    po[Oo->s5c][0] = sv1[0]; po[Oo->s5c][1] = sv1[1];
    return ret;
}
/* FUN_180148d20(ctx, x, E, pts, &idx, Q, &d, R, &err) (asm 180148d20..180148f8c): 3 = the crossing is at an end of the other record
   (FUN_180150930); else the record's extension walked to x (FUN_1801a9770): 1 at a point the other direction holds (FUN_180156b60), 2
   when the continuation meets the other record (FUN_180148f90), else 1; 0 when the walk fails */
static int cross_8d20(CsLoop *L, double x, const Ent *E, double (*pts)[2], short *idx, double Q[3], double *d, Cross *R, short *err)
{
    *err = 0;
    if (atend_0930(L, E, pts, R, err)) return 3;
    int dir = E->kind, bpl = E->kind == 1, r15 = !bpl;
    const CsSeg *Rr = SR(L, dir, E->rec);
    double lpos = L->D[dir]->H[Rr->line].pos, P0[2], P1[2];
    short k0, k1;
    if (E->side == 0) { P0[0] = Rr->s0; P0[1] = Rr->z0; k0 = (short)(Rr->s5a + 1); k1 = (short)(Rr->s58 + 1); P1[0] = Rr->v60; P1[1] = Rr->v68; }
    else { P0[0] = Rr->s1; P0[1] = Rr->z1; k0 = (short)(Rr->s5c - 1); k1 = (short)(Rr->s5e - 1); P1[0] = Rr->v90; P1[1] = Rr->v98; }
    double O[2];
    short r = walk_9770(L, x, k0, P0, k1, P1, pts, idx, O, d);
    if (r != 1) return r;
    Q[bpl] = lpos; Q[r15] = O[0]; Q[2] = O[1];
    if (hit_6b60(L, Q, E->side, dir, E->rec)) return 1;
    if (cont_8f90(L, x, E, O, pts, R, err)) return 2;
    return 1;
}
/* FUN_18014a0a0(ctx, 3 tol, E, &err) (asm 18014a0a0..18014a652): one record end against the crossing record of the other direction:
   0 = the end moved to the crossing (and the neighbour's extension, FUN_180148140); 1 / 2 = it overlaps the next / previous record of the
   same segment within the gap (merge); 3 = no crossing on the extension; 4 / 5 = overlapping a record of another segment; 6 / 7 = the
   gap or the crossing too far; 8 = at a z limit; -6 otherwise */
static int end_a0a0(CsLoop *L, double tol3, Ent *E, short *err)
{
    *err = 0;
    if (!zok_a020(L, E->P)) return 8;
    int dir = E->kind, od = 1 - dir, r12 = dir == 1 ? 0 : 1, side = E->side;
    int r8 = up_b860(L, dir);
    double x6 = fabs(tol3), x1 = L->tol + L->tol;
    if (r8 ? side == 0 : side != 0) { x1 = -x1; x6 = -x6; }
    double v = x6 + L->D[od]->H[SR(L, od, E->oth)->line].pos, v2 = v + x1;
    CsSeg *R = SR(L, dir, E->rec);
    int nb = side == 0 ? R->prev : R->next;
    if (nb != -1) {
        CsSeg *N = SR(L, dir, nb);
        double xx = side == 0 ? N->s1 : N->s0;
        int dil = 0;
        if (r8) { if (side == 0 && xx > v) dil = 2; else if (side == 1 && v > xx) dil = 1; }
        else { if (side == 0 && v > xx) dil = 2; else if (side == 1 && xx > v) dil = 1; }
        if (dil) {
            int si = dil == 1 ? 1 : -1;
            if (!same_5810(R, N)) return dil + 3;
            if (cs_d15_gap(L->D[dir], L->gap, si, E->rec)) return dil;   /* FUN_1801574a0 = 1 (ctx +0x309 == 0), FUN_180156cf0 */
            return dil + 5;
        }
    }
    double (*pts)[2] = PTS(L, dir, E->rec);
    if (!pts) { *err = NP; return -6; }
    if (!inext_c740(L, R, side, v2)) {
        if (!ingap_6740(L, dir, E->rec, side, v2) || !gapext_56c0(L, dir, E->rec, side, pts, v2)) extreset_4ac0(L, dir, E->rec, side, pts);
    }
    short idx = 0; double Q[3] = { 0 }, d = 0; Cross X; memset(&X, 0, sizeof X);
    int r = cross_8d20(L, v, E, pts, &idx, Q, &d, &X, err);
    L->cov[24 + (r >= 0 && r <= 3 ? r : 4)]++;
    R = SR(L, dir, E->rec);
    if (r == 1) {
        double qk = Q[idx3(r12)];
        if (side != 0 && side != 1) return -6;
        if (d > L->gap) return (side == 0) + 6;
        /* FUN_1801575e0 = 1 (ctx +0x309 == 0, asm 1801575e4) */
        if (side == 0) { R->s5a = (short)(idx - 1); R->s0 = qk; R->z0 = Q[2]; }
        else { R->s5c = (short)(idx + 1); R->s1 = qk; R->z1 = Q[2]; }
        nbext_8140(L, dir, side, E->rec);
        return 0;
    }
    if (r == 2) {
        if (side != 0 && side != 1) return -6;   /* FUN_1801575e0 = 1 */
    } else if (r == 0) return 3;
    else if (r != 3) return -6;
    if (side == 0) { R->s5a = X.i0; R->s0 = v; R->z0 = X.p1[2]; }
    else if (side == 1) { R->s5c = (short)(X.i0 + 1); R->s1 = v; R->z1 = X.p1[2]; }
    else return -6;
    nbext_8140(L, dir, side, E->rec);
    return 0;
}
/* FUN_180149980(ctx, 3 tol, E, &err) (asm 180149980..180149a12): FUN_18014a0a0 again while it moved the end and the end still meets a
   record (FUN_180156800 = FUN_18014a950 on the entry) */
static int end_9980(CsLoop *L, double tol3, Ent *E, short *err)
{
    for (;;) {
        int r = end_a0a0(L, tol3, E, err);
        if (*err) return r;
        if (r != 0) { if ((unsigned char)(r - 1) > 7) *err = -16; return r; }
        int dir = E->kind;
        *err = ent_a950(L, E->side, dir, E->rec, E);   /* FUN_180156800 (asm 180156800) */
        if (*err || E->rec == -1) return 0;
    }
}
/* FUN_18014d6d0(d, hdrs, R) (asm 18014d6d0..18014d7d0): d = 1: R takes the next record's end (+0x5c, +0x80, +0x88, +0xa8, +0x32, +0x38,
   +0x5e, +0x90, +0x98), else the previous one's start (+0x5a, +0x70, +0x78, +0xa0, +0x30, +0x34, +0x58, +0x60, +0x68); that one is freed
   (FUN_1801a9c80(1, ..)) and returned */
static int join_d6d0(CsLoop *L, int dir, int d, int r)
{
    CsSeg *R = SR(L, dir, r);
    int o = d == 1 ? R->next : R->prev;
    if (o == -1) return -1;
    CsSeg *N = SR(L, dir, o);
    if (d == 1) {
        R->s5c = N->s5c; R->s1 = N->s1; R->z1 = N->z1; R->sa8 = N->sa8; R->s32 = N->s32; R->i38 = N->i38; R->s5e = N->s5e;
        R->v90 = N->v90; R->v98 = N->v98;
    } else {
        R->s5a = N->s5a; R->s0 = N->s0; R->z0 = N->z0; R->sa0 = N->sa0; R->s30 = N->s30; R->i34 = N->i34; R->s58 = N->s58;
        R->v60 = N->v60; R->v68 = N->v68;
    }
    cs_d15_free(L->D[dir], o);
    return o;
}
/* FUN_180150ff0(ctx, d, k, W) (asm 180150ff0..180151086): entry k's record merged with its neighbour; the freed record dropped from every
   entry (+0 and +0x30) */
static void merge_0ff0(CsLoop *L, int d, int k, Work *W)
{
    int dir = W->e[k].kind;
    int o = join_d6d0(L, dir, d, W->e[k].rec);
    if (o == -1) return;
    for (int i = 0; i < W->count; i++) {
        if (W->e[i].rec == o && W->e[i].kind == dir) W->e[i].rec = -1;
        if (W->e[i].oth == o && W->e[i].kind == 1 - dir) W->e[i].oth = -1;
    }
}
/* FUN_180149a20(ctx, abort) (asm 180149a20..180149c8c): passes over the gathered entries (from B's first line, FUN_1801459c0(W, 100));
   FUN_180149980's result 0..8 counted (jump table 0x180149c90: 1 / 2 merge by FUN_180150ff0(+1 / -1)); after a whole pass the counters
   are compared with the previous pass's (FUN_1801513e0) and the passes stop when they are equal, or when a gather finds nothing */
int cs_d15_9a20(CsLoop *L, int *passes)
{
    Work W = { 1, 1, 0, 0, 0, 100, NULL };
    W.e = malloc(sizeof *W.e * 100);
    int cur[10] = { 0 }, prev[10] = { 0 }, total = 0;
    short err = 0;
    double tol3 = L->tol + L->tol + L->tol;   /* asm 180149a6b..180149a7d */
    *passes = 0;
    for (;;) {
        int done;
        if (!gather_aa50(L, &W, &done, &err)) break;
        total += W.count; cur[0] = total;
        for (int k = 0; k < W.count; k++) {
            Ent *E = &W.e[k];
            if (E->rec == -1 || E->oth == -1) continue;
            int r = end_9980(L, tol3, E, &err);
            if (err) goto out;
            switch (r) {
            case 0: cur[1]++; break;
            case 1: merge_0ff0(L, 1, k, &W); cur[2]++; break;
            case 2: merge_0ff0(L, -1, k, &W); cur[3]++; break;
            case 3: cur[9]++; break;
            case 4: cur[4]++; break;
            case 5: cur[5]++; break;
            case 6: cur[6]++; break;
            case 7: cur[7]++; break;
            case 8: cur[8]++; break;
            default: err = -16; goto out;
            }
            L->cov[r]++;
        }
        if (!done) continue;
        (*passes)++;
        if (!memcmp(prev, cur, sizeof cur)) break;
        memcpy(prev, cur, sizeof cur); memset(cur, 0, sizeof cur); total = 0;
        W.cdir = W.sdir; W.cline = W.sline;
    }
out:
    free(W.e);
    return err;
}

/* shared with cam3d_cscallop_6e60.c (FUN_180146e60's tree calls the same DLL functions) */
int cs_9a20_up(CsLoop *L, int dir) { return up_b860(L, dir); }                                                  /* FUN_18017b860 */
void cs_9a20_pt(CsLoop *L, int side, int dir, int r, double P[3]) { pt_64b0(L, side, dir, r, P); }               /* FUN_1801864b0 */
int cs_9a20_zok(const CsLoop *L, const double P[3]) { return zok_a020(L, P); }                                  /* FUN_18014a020 */
int cs_9a20_lines(CsLoop *L, const double P[3], int *a, int *b, int *c, int *d) { return lines_f990(L, P, a, b, c, d); }   /* FUN_18015f990 */
short cs_9a20_walk(const CsLoop *L, double x, short k0, const double P0[2], short k1, const double P1[2], double (*pts)[2], short *idx,
                   double O[2], double *d) { return walk_9770(L, x, k0, P0, k1, P1, pts, idx, O, d); }       /* FUN_1801a9770 */
