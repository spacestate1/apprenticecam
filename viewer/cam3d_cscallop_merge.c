/* cam3d_cscallop_merge.c - Constant Scallop (op 43) stage B: the line list ("estore") and the envelope merge of the scanner mcmsurf
 * FUN_1801ea7e0 (formats/surf_classic3.md 12.14). Sources: objdump of mcmsurf.dll (asm addresses below; FUN_1801e5200 is translated from
 * its disassembly 1801e5200..1801e66b4 - the decompile, decomp/mcmsurf_all.c l.322475, garbles its register pairs and the stack arguments
 * of FUN_1801e2a90) and decomp/mcmsurf_all.c (l.N) for the small helpers. Build with -ffp-contract=off as the rest of the engine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

void msvc_qsort(void *base, unsigned long long n, unsigned long long w, int (*cmp)(const void *, const void *));   /* nest/nest_rect.c */
void cs_nrm_interp(double t, const double *a, const double *b, double *o);                                       /* FUN_1802535d0 */
long cs_cov3[24];   /* merge branch counts (cs_port prints them) */
const char *cs_cov3_name[24] = { "pieces", "inserted before", "appended", "node kept", "piece kept", "crossing linear", "crossing l_ints",
                                 "crossing bisection", "node removed", "ties by code", "walls", "short groups", "short from prev", "short from next",
                                 "straight joins", "joints merged", "joints split", 0 };

/* ------------------------------------------------------------------ the store */
/* LAB_1801e5190 (asm 1801e5190..1801e51f5): every node chained free (i.next = i + 1, the last -1), free head 0, free count = capacity,
   list head / tail -1, error 0 (FUN_1801db730(0)) */
int cs_est_init(CsScan *S, int cap)
{
    free(S->est);
    S->est = calloc((size_t)cap + 1, sizeof(CsNode));
    if (!S->est) return -1;
    S->ecap = (short)cap;
    for (int i = 0; i < cap; i++) S->est[i].next = (short)(i + 1);
    S->est[cap - 1].next = -1;
    S->efree = 0; S->nfree = (short)cap; S->ehead = -1; S->etail = -1; S->eerr = 0;
    return 0;
}
/* FUN_1801e7cd0(1) (l.324167): hint on, its node the head, batch empty */
void cs_est_hint(CsScan *S, int on)
{
    S->hprev = -1; S->hnode = S->ehead; S->nbatch = 0; S->hint_on = (char)on;
}
/* LAB_1801e4460 (asm 1801e4460..1801e44d4): the line's nodes back to the free list (DAT_180501da0, after each line) */
void cs_est_release(CsScan *S)
{
    if (S->ehead != -1) {
        S->est[S->etail].next = S->efree;
        S->efree = S->ehead;
        S->etail = -1; S->ehead = -1; S->hnode = -1;
        S->nfree = S->ecap;
        S->hprev = -1;
    } else { S->hnode = -1; S->hprev = -1; }
}
/* FUN_1801da360 (l.315458): the node before n (-1 when n is the head or not in the list) */
static short est_before(const CsScan *S, short n)
{
    short p = -1, k = S->ehead;
    for (;;) {
        if (k == -1) return -1;
        if (k == n) break;
        p = k; k = S->est[k].next;
    }
    return p;
}
/* FUN_1801da5d0 (asm 1801da5d0..1801da6a7): where the search starts (the hint) and the node before it */
static short est_start(CsScan *S, double s, short *prev)
{
    if (!S->hint_on) { *prev = -1; return S->ehead; }
    short hn = S->hnode, hd = S->ehead;
    if (hn < 0 && hd != -1) { *prev = -1; return hd; }
    if (hn != -1 && s < S->est[hn].s0) { S->hnode = hd; *prev = -1; S->hprev = -1; return hd; }
    *prev = S->hprev;
    if (S->hprev != -1 && hn != -1 && S->est[S->hprev].next != hn) *prev = est_before(S, hn);
    return hn;
}
/* FUN_1801e2a90 (l.320886): a node from the free list between prev and next (prev -1: the new head; next -1: the new tail); no free
   node: error 2 and "ERROR: Ran Out of ROOM in ESTORE" */
static void est_insert(CsScan *S, short code, double s0, double z0, const double *n0, double s1, double z1, const double *n1, short prev, short next)
{
    if (S->efree == -1) { S->eerr = 2; return; }
    short k = S->efree; CsNode *N = &S->est[k];
    short nf = N->next;
    N->f0 = 0; N->s0 = s0; N->z0 = z0; N->n0[0] = n0[0]; N->n0[1] = n0[1]; N->n0[2] = n0[2];
    N->f1 = 0; N->s1 = s1; N->z1 = z1; N->n1[0] = n1[0]; N->n1[1] = n1[1]; N->n1[2] = n1[2];
    N->code = code;
    if (prev == -1) {
        if (S->hint_on) { S->hprev = -1; S->hnode = k; }
        S->ehead = k;
    } else S->est[prev].next = k;
    if (next == -1) { N->next = -1; S->etail = k; }
    else N->next = next;
    S->nfree--;
    S->efree = nf;
}
/* FUN_1801e2230 (l.320495): node n out of the list (after prev) onto the free list */
static void est_unlink(CsScan *S, short prev, short n)
{
    if (n == -1) return;
    if (prev != -1) S->est[prev].next = S->est[n].next;
    if (S->ehead == n) S->ehead = S->est[n].next;
    if (S->hint_on && (S->hnode == n || S->hprev == n)) { S->hprev = -1; S->hnode = -1; }
    S->nfree++;
    S->est[n].next = S->efree;
    S->efree = n;
    if (S->etail == n) S->etail = prev;
}

/* ------------------------------------------------------------------ walls */
/* FUN_1801688d0 (l.239852): the codes FUN_1801e01a0 always sends to the wall store */
static int code_wall(short c)
{
    switch (c - 2) { case 0: case 1: case 7: case 8: case 0xe: case 0xf: case 0x12: case 0x13: case 0x19: case 0x1a: case 0x20: case 0x21:
                     case 0x26: case 0x27: return 1; }
    return 0;
}
/* FUN_1801e01a0 (l.319282) with FUN_1801edb00 (l.327715): a vertical piece (|s1 - s0| < tol, |z1 - z0| > system tol, both normals
   horizontal: |n.z| < 8.7266462e-5) */
static int piece_is_wall(const CsScan *S, const CsPiece *p)
{
    if (code_wall(p->code)) return 1;     /* FUN_1801688d0's 1 is returned as is (asm 1801e01be -> 1801e0229) */
    if (fabs(p->s[1] - p->s[0]) < S->systol && S->ctol <= fabs(p->z[1] - p->z[0]) && fabs(p->z[1] - p->z[0]) != S->ctol)
        return fabs(p->n[0][2]) < 8.7266462e-05 && fabs(p->n[1][2]) < 8.7266462e-05;
    return 0;
}
/* FUN_1801edb20 (l.327726): the wall record (0x68 bytes as a node: +8 s0, +0x10 z0, +0x18 n0, +0x38 s1, +0x40 z1, +0x48 n1, +0x60 code;
   the flags +0 / +0x30 are not written). A full store (DAT_180501d40 == DAT_180501d44 = 2000) is flushed by FUN_1801ed9b0 first */
static int wall_add(CsScan *S, const CsPiece *p)
{
    if (S->nwall == S->cwall) {
        if (!S->wall_flush) return -1;
        int r = S->wall_flush(S);
        if (r) return r;
    }
    CsNode *W = &S->wall[S->nwall++];
    memset(W, 0, sizeof *W);
    W->s0 = p->s[0]; W->z0 = p->z[0]; memcpy(W->n0, p->n[0], 24);
    W->s1 = p->s[1]; W->z1 = p->z[1]; memcpy(W->n1, p->n[1], 24);
    W->code = p->code;
    return 0;
}

/* ------------------------------------------------------------------ l_ints (mccore 0x1804b3480; the port of viewer/cam2d_pock_zz.c
   zz_l_ints / zz_l_hv / zz_t_line with the system tolerance as an argument), called through mcmsurf FUN_1802531e0 (asm 1802531e0..1802532a0:
   line (a0, a1) and line (b0, b1); the point, then the parameters on each; returns l_ints' flag) */
static int l_hv(double tol, int k, const double *h0, const double *h1, const double *o0, const double *o1, double *pt, double *th, double *to)
{
    (void)tol;
    double d1 = k == 2 ? h1[1] : h1[0], d2 = k == 2 ? h0[1] : h0[0];
    if (!(1e-12 <= fabs(d1 - d2))) return 0;
    double d7 = k == 1 ? o1[1] - o0[1] : o1[0] - o0[0];
    if (!(1e-12 <= fabs(d7))) return 0;
    double ot = k == 2 ? o1[1] - o0[1] : o1[0] - o0[0], d6 = sqrt(ot * ot + d7 * d7);
    if (!(1e-12 <= d6 && 1.5e-10 <= fabs(d7 / d6))) return 0;
    double t = ((k == 1 ? h0[1] : h0[0]) - (k == 1 ? o0[1] : o0[0])) / d7;
    *to = t; pt[0] = (o1[0] - o0[0]) * t + o0[0]; pt[1] = (o1[1] - o0[1]) * t + o0[1];
    *th = ((k == 1 ? pt[0] : pt[1]) - (k == 1 ? h0[0] : h0[1])) / (d1 - d2);
    return 1;
}
static double t_line(double tol, const double *p, const double *a, const double *b)
{
    double dx = b[0] - a[0], dy = b[1] - a[1], d = dy * dy + dx * dx;
    return tol * tol < d ? (dy * (p[1] - a[1]) + dx * (p[0] - a[0])) / d : 0.0;
}
static int l_ints(double tol, const double *a0, const double *a1, const double *b0, const double *b1, double *pt, double *ta, double *tb)
{
    double dy = a1[1] - a0[1], dx = a1[0] - a0[0], L = dy * dy + dx * dx;
    if (!(1e-24 <= L)) return 0;
    if (!(1e-24 <= dx * dx / L)) return l_hv(tol, 2, a0, a1, b0, b1, pt, ta, tb);
    if (!(1e-24 <= dy * dy / L)) return l_hv(tol, 1, a0, a1, b0, b1, pt, ta, tb);
    double bx = b1[0] - b0[0], by = b1[1] - b0[1], LB = by * by + bx * bx;
    if (!(1e-24 <= LB)) return 0;
    if (!(1e-24 <= bx * bx / LB)) return l_hv(tol, 2, b0, b1, a0, a1, pt, tb, ta);
    if (!(1e-24 <= by * by / LB)) return l_hv(tol, 1, b0, b1, a0, a1, pt, tb, ta);
    double d5 = b0[0] - a0[0], d21 = b0[0] - b1[0], d1 = b0[1] - b1[1];
    double den = d1 * dx - d21 * dy, d15 = d5 * d1 - (b0[1] - a0[1]) * d21, d16 = d1 * dy + d21 * dx;
    int ok = tol * tol <= fabs(d16) ? tol <= fabs(den / d16) : tol * tol <= fabs(den);
    if (ok) {
        double t = d15 / den; *ta = t; pt[0] = a0[0] + dx * t; pt[1] = a0[1] + dy * t;
        *tb = ((b0[1] - a0[1]) * dx - d5 * dy) / den;
        return 1;
    }
    double d = sqrt(d21 * d21 + d1 * d1);
    if (!(tol < fabs(d))) return 0;
    double dist = d15 / d;
    if (!(fabs(dist) <= tol && tol != fabs(dist))) return 0;
    const double *A[2] = { a0, a1 }, *B[2] = { b0, b1 }; int ia = 1, ib = 1; double best = 1e38;   /* collinear: nearest ends */
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) { double q = hypot(B[j][1] - A[i][1], B[j][0] - A[i][0]); if (q < best) { best = q; ib = j; ia = i; } }
    pt[0] = (B[ib][0] + A[ia][0]) * 0.5; pt[1] = (B[ib][1] + A[ia][1]) * 0.5;
    *ta = t_line(tol, pt, a0, a1); *tb = t_line(tol, pt, b0, b1);
    return 1;
}

/* ------------------------------------------------------------------ FUN_1801e5200: one piece into the line's upper envelope */
#define CP3(d, s) ((d)[0] = (s)[0], (d)[1] = (s)[1], (d)[2] = (s)[2])
static void merge_piece(CsScan *S, const CsPiece *P)
{
    CsNode *E = S->est;
    const double tol = S->systol;                        /* DAT_1804ea578 */
    double sA, sB, zA, zB, sA0;                          /* xmm7, xmm8 / [rbp-8], xmm9, [rbp+0x158] (xmm6), [rbp+0x30] (xmm10) */
    double nA[3], nB[3];                                 /* [rsp+0x60], [rbp-0x80] */
    double nlo[3], nhi[3], mlo[3], mhi[3], q18[3], q68[3];   /* [rbp-0x38], [rbp-0x50], [rbp-0x20], [rbp+0], [rbp+0x18], [rbp-0x68] */
    double lo, hi, plz, phz, nlz, nhz, len, d = 0, xs = 0, xz = 0;   /* xmm10, xmm11, xmm12, xmm14, xmm13, [rsp+0x78], xmm6, [rbp+0x38],
                                                                    [rbp+0x150], [rbp+0x148] */
    short cur, prev, nx;                                 /* r14, r15, bx / [rbp+0x140] */
    cs_cov3[0]++;
    /* 1801e5214..1801e52d1: the ends ordered by s (A the lower; equal: the second end first) */
    if (P->s[0] < P->s[1]) {
        CP3(nA, P->n[0]); zA = P->z[0]; zB = P->z[1]; CP3(nB, P->n[1]); sA = P->s[0]; sB = P->s[1];
    } else {
        CP3(nA, P->n[1]); zA = P->z[1]; zB = P->z[0]; CP3(nB, P->n[0]); sA = P->s[1]; sB = P->s[0];
    }
    sA0 = sA;
    /* 1801e52b7..1801e531e: containment (DAT_1804ea4f0, FUN_1801cfb60's param_11): off on the op 43 path (12.2), not ported */
    /* 1801e531e..1801e5343: DAT_1804ea5e6 (the scanner's param_2 = 1) and FUN_1801e01a0 -> FUN_1801edb20 */
    if (piece_is_wall(S, P)) { cs_cov3[10]++; if (wall_add(S, P)) S->eerr = 2; }
    /* 1801e5343..1801e535a: |sB - sA| > 1e-12 */
    if (!(1e-12 < fabs(sB - sA))) return;
    cur = est_start(S, sA0, &prev);                      /* 1801e5383 FUN_1801da5d0 */
    if (S->nfree < 4 && S->eerr == 0) S->eerr = 2;       /* FUN_1801db730(2) */
    if (S->etail != -1 && !(sA < E[S->etail].s1)) {     /* 1801e53fe..1801e5419: beyond the tail: appended */
        prev = S->etail; cur = -1; goto L5523;
    }
    if (cur == -1) goto L5523;
L5440:
    if (S->nfree < 4) goto L5523;
    nx = E[cur].next;
    if (!(E[cur].s0 < sB)) {                             /* 1801e5473: the node starts at / after the piece's end: the piece before it */
        cs_cov3[1]++;
        est_insert(S, P->code, sA, zA, nA, sB, zB, nB, prev, cur);
        CP3(nA, nB); sA = sB; zA = zB;
        goto L54da;
    }
    /* L5600 */
    if (S->hint_on && E[cur].s1 + S->ctol < sA0) { S->hprev = prev; S->hnode = cur; }   /* *tol_exref (1801e5614) */
    if (!(sA < E[cur].s1)) goto L54de;                   /* the node ends before the piece starts */
    lo = sA; if (sA < E[cur].s0) lo = E[cur].s0;        /* 1801e564c..1801e5669 */
    hi = sB; if (E[cur].s1 < sB) hi = E[cur].s1;
    len = sB - sA;
    if (tol < len) {                                     /* 1801e5684: the piece at lo / hi */
        double dz = zB - zA, t = (lo - sA) / len;
        plz = dz * t + zA;
        cs_nrm_interp(t, nA, nB, nlo);
        t = (hi - sA) / len;
        phz = dz * t + zA;
        cs_nrm_interp(t, nA, nB, nhi);
    } else { CP3(nlo, nA); plz = zA; CP3(nhi, nB); phz = zB; }
    {
        CsNode *N = &E[cur];
        double nl = N->s1 - N->s0;
        if (tol < nl) {                                  /* 1801e572e: the node at lo / hi */
            double t = (lo - N->s0) / nl;
            nlz = (N->z1 - N->z0) * t + N->z0;
            cs_nrm_interp(t, N->n0, N->n1, mlo);
            double t3 = (hi - N->s0) / (N->s1 - N->s0);
            nhz = (N->z1 - N->z0) * t3 + N->z0;
            cs_nrm_interp(t3, N->n0, N->n1, mhi);
        } else { CP3(mlo, N->n0); nlz = N->z0; nhz = N->z1; CP3(mhi, N->n1); }
    }
    {   /* L580c: which is above over the overlap */
        double dlo = nlz - plz;
        if (fabs(dlo) <= tol) {
            if (fabs(nhz - phz) <= tol) {                /* equal at both ends: by code (0 / 0x12 = triangle pieces first), then the longer */
                short nc = E[cur].code, pc = P->code;
                int nt = nc == 0 || nc == 0x12, pt = pc == 0 || pc == 0x12;
                cs_cov3[9]++;
                if (nt && !pt) goto L58f5;
                if (!nt && pt) goto L5a48;
                if (E[cur].s1 - E[cur].s0 > len) goto L58f5;
                goto L5a48;
            }
            if (nhz > phz) goto L58f5;                  /* L58a0 */
            goto L5a48;
        }
        if (fabs(nhz - phz) <= tol) {                    /* L58ba */
            if (nlz > plz) goto L58f5;
            goto L5a48;
        }
        if (nlz > plz && nhz > phz) goto L58f5;          /* L58df */
        if (plz > nlz && phz > nhz) goto L5a48;          /* L5a32 */
        goto L5c1a;
    }
L58f5:  /* the node stays above over the overlap */
    cs_cov3[3]++;
    if (lo > sA) {
        if (sB > hi) {                                   /* 1801e5907: the piece's part before the node, the rest goes on past it */
            est_insert(S, P->code, sA, zA, nA, lo, plz, nlo, prev, cur);
            sA = hi; zA = phz; CP3(nlo, nhi);            /* 1801e594c..1801e5969: [rbp-0x38] = [rbp-0x50], nA is not changed */
            goto L54de;
        }
        /* L599a */
        est_insert(S, P->code, sA, zA, nA, lo, plz, nlo, prev, cur);
        CP3(nB, nA); zA = zB; sA = sB;                   /* 1801e59e6..1801e5a03: [rbp-0x80] = [rsp+0x60] */
        goto L54da;
    }
    if (sB > hi) goto L5979;                             /* L5972 */
    CP3(nA, nB); zA = zB; sA = sB;                       /* 1801e5a0c: hidden */
    goto L54da;
L5979:
    CP3(nA, nhi); sA = hi; zA = phz;
    goto L54de;
L5a48:  /* the piece is above over the overlap */
    cs_cov3[4]++;
    if (lo > E[cur].s0) {
        if (E[cur].s1 > hi) {                            /* 1801e5a6a: the piece inside the node: node front, piece, node back */
            short fb = S->efree;
            est_insert(S, E[cur].code, E[cur].s0, E[cur].z0, E[cur].n0, lo, nlz, mlo, prev, cur);
            if (fb != S->efree) prev = fb;
            est_insert(S, P->code, sA, zA, nA, sB, zB, nB, prev, cur);   /* L5aef */
            E[cur].s0 = hi; E[cur].z0 = nhz; CP3(E[cur].n0, mhi);
            zA = zB; sA = sB;
            goto L54da;
        }
        E[cur].s1 = lo; E[cur].z1 = nlz; CP3(E[cur].n1, mlo);   /* L5b54: the node's end cut back */
        goto L54de;
    }
    if (E[cur].s1 > hi) {                                /* L5b82: the piece ends in the node: the piece before it, the node from hi */
        est_insert(S, P->code, sA, zA, nA, sB, zB, nB, prev, cur);
        zA = zB; sA = sB;
        E[cur].s0 = hi; E[cur].z0 = nhz; CP3(E[cur].n0, mhi);
        goto L54da;
    }
    goto L6287;
L5c1a:  /* the two cross over the overlap */
    d = hi - lo;
    if (tol < d) {                                       /* 1801e5c32: the linear crossing */
        cs_cov3[5]++;
        double dp = phz - plz;
        double x = d * (nlz - plz) / ((dp - nhz) + nlz) + lo;
        xs = x;
        double t = (x - lo) / d;
        xz = t * dp + plz;
        cs_nrm_interp(t, nlo, nhi, q18);
        cs_nrm_interp(t, mlo, mhi, q68);
    } else {
        double a0[2] = { sA, zA }, a1[2] = { sB, zB }, b0[2] = { E[cur].s0, E[cur].z0 }, b1[2] = { E[cur].s1, E[cur].z1 }, pt[2], ta, tb;
        if (l_ints(S->ctol, a0, a1, b0, b1, pt, &ta, &tb)) {   /* 1801e5cfd FUN_1802531e0 */
            cs_cov3[6]++;
            xs = pt[0]; xz = pt[1];
            cs_nrm_interp(ta, nlo, nhi, q18);
            cs_nrm_interp(tb, mlo, mhi, q68);
        } else {                                         /* 1801e5d37: bisection, 1000 steps */
            cs_cov3[7]++;
            double dn = nhz - nlz, dp = phz - plz, t = 0.5, st = 0.5;
            for (short k = 0; k < 1000; k++) {
                double pz = dp * t + plz, nz = dn * t + nlz;
                xz = pz;
                if (fabs(pz - nz) < tol) break;
                st = st * 0.5;
                if (plz > nlz) { if (pz > nz) t = t + st; else t = t - st; }
                else { if (pz > nz) t = t - st; else t = t + st; }
            }
            xs = d * t + lo;
            cs_nrm_interp(t, nlo, nhi, q18);
            cs_nrm_interp(t, mlo, mhi, q68);
        }
    }
    /* L5e21: the crossing kept inside (lo, hi) */
    if (!(2e-12 < d)) xs = (hi + lo) * 0.5;
    else if (lo + 1e-12 > xs) xs = lo + 1e-12;
    else if (xs > hi - 1e-12) xs = hi - 1e-12;
    if (plz > nlz) {                                     /* 1801e5e89: the piece above at lo, the node at hi */
        double es0 = E[cur].s0;
        if (lo > es0) {
            if (xs > sA && E[cur].s1 > xs && sB > hi) {  /* node front, piece to the crossing, the node from it, the piece goes on */
                short fb = S->efree;
                est_insert(S, E[cur].code, E[cur].s0, E[cur].z0, E[cur].n0, lo, nlz, mlo, prev, cur);
                if (fb != S->efree) prev = fb;
                est_insert(S, P->code, sA, zA, nA, xs, xz, q18, prev, cur);
                sA = hi; zA = phz;
                E[cur].s0 = xs; E[cur].z0 = xz; CP3(E[cur].n0, q68);
                CP3(nlo, nhi);                           /* 1801e5fb1: [rbp-0x38] = [rbp-0x50] */
                goto L54de;
            }
            if (!(xs > sA)) goto L6229;                  /* L5fc8 */
            if (!(E[cur].s1 > xs)) goto L60e4;
            {
                short fb = S->efree;
                est_insert(S, E[cur].code, E[cur].s0, E[cur].z0, E[cur].n0, lo, nlz, mlo, prev, cur);
                if (fb != S->efree) prev = fb;
                est_insert(S, P->code, sA, zA, nA, xs, xz, q18, prev, cur);
                sA = sB; zA = zB;
                E[cur].s0 = xs; E[cur].z0 = xz; CP3(E[cur].n0, q68);
                goto L54da;
            }
        }
L60e4:
        if (xs > sA && E[cur].s1 > xs) {
            if (sB > hi) {                               /* 1801e6109 */
                est_insert(S, P->code, sA, zA, nA, xs, xz, q18, prev, cur);
                E[cur].s0 = xs; E[cur].z0 = xz; CP3(E[cur].n0, q68);
                goto L5979;
            }
            est_insert(S, P->code, sA, zA, nA, xs, xz, q18, prev, cur);   /* L6190 */
            sA = sB; zA = zB;
            E[cur].s0 = xs; E[cur].z0 = xz; CP3(E[cur].n0, q68);
            goto L54da;
        }
L6229:
        if (lo > es0) {
            if (!(xs > sA)) goto L5979;
            E[cur].s1 = lo; E[cur].z1 = nlz; CP3(E[cur].n1, mlo);   /* 1801e623a */
            sA = lo; zA = plz; CP3(nA, nlo);
            goto L54de;
        }
        if (xs > sA) goto L6287;                         /* L627d */
        goto L5979;
    }
    /* L6299: the node above at lo, the piece at hi */
    if (lo > sA && xs > E[cur].s0) {
        if (sB > xs && E[cur].s1 > hi) {                 /* 1801e62d0: piece front, node to the crossing, piece, node from hi */
            short fb = S->efree;
            est_insert(S, P->code, sA, zA, nA, lo, plz, nlo, prev, cur);
            short fd = S->efree;
            if (fb != fd) prev = fb;
            est_insert(S, E[cur].code, E[cur].s0, E[cur].z0, E[cur].n0, xs, xz, q68, prev, cur);
            if (fd != S->efree) prev = fd;
            est_insert(S, P->code, xs, xz, q18, sB, zB, nB, prev, cur);   /* 1801e63c1 -> L5aef */
            E[cur].s0 = hi; E[cur].z0 = nhz; CP3(E[cur].n0, mhi);
            zA = zB; sA = sB;
            goto L54da;
        }
        if (sB > xs) {                                   /* L63c6 (xs > E.s0 holds) */
            est_insert(S, P->code, sA, zA, nA, lo, plz, nlo, prev, cur);
            E[cur].s1 = xs; E[cur].z1 = xz; CP3(E[cur].n1, q68);   /* 1801e6427: nA is not changed */
            sA = xs; zA = xz;
            goto L54de;
        }
    }
    /* L6465 */
    if (xs > E[cur].s0) {
        if (sB > xs && E[cur].s1 > hi) {                 /* 1801e6491: node to the crossing, the piece, the node from hi */
            short fb = S->efree;
            est_insert(S, E[cur].code, E[cur].s0, E[cur].z0, E[cur].n0, xs, xz, q68, prev, cur);
            if (fb != S->efree) prev = fb;
            est_insert(S, P->code, xs, xz, q18, sB, zB, nB, prev, cur);
            E[cur].s0 = hi; E[cur].z0 = nhz; CP3(E[cur].n0, q68);   /* 1801e6543..1801e6564 -> 1801e5b3a: n0 = [rbp-0x68] */
            zA = zB; sA = sB;
            goto L54da;
        }
        if (sB > xs) {                                   /* L6569 */
            E[cur].s1 = xs; E[cur].z1 = xz; CP3(E[cur].n1, q68);
            CP3(nA, q18); sA = xs; zA = xz;
            goto L54de;
        }
        goto L6650;
    }
    /* L65cc */
    if (sB > xs) {
        if (!(E[cur].s1 > hi)) goto L6287;
        est_insert(S, P->code, sA, zA, nA, hi, phz, nhi, prev, cur);   /* 1801e65e4 */
        zA = zB; sA = sB;
        E[cur].s0 = hi; E[cur].z0 = nhz;
        goto L54da;
    }
L6650:
    if (lo > sA) {
        est_insert(S, P->code, sA, zA, nA, lo, plz, nlo, prev, cur);
        zA = zB; sA = sB;
        goto L54da;
    }
    sA = hi; zA = phz;                                   /* L66a9 */
    goto L54de;
L6287:  /* the node lies under the piece: removed */
    cs_cov3[8]++;
    est_unlink(S, prev, cur);
    goto L54e2;
L54da:  /* done with the piece */
    cur = -1; prev = -1;
    goto L54e2;
L54de:
    prev = cur;
L54e2:
    if (cur != -1) cur = nx;
    if (S->nfree < 4 && S->eerr == 0) S->eerr = 2;
    if (cur != -1) goto L5440;
L5523:  /* what is left of the piece goes before cur (or at the end) */
    if (sB > sA && S->nfree >= 1) {
        cs_cov3[2]++;
        est_insert(S, P->code, sA, zA, nA, sB, zB, nB, prev, cur);
    }
}

/* ------------------------------------------------------------------ the batch */
static int cmp_lo(const void *a, const void *b)         /* LAB_1801da1a0 (asm): by the lower end (+8) */
{
    double x = (*(const CsPiece *const *)a)->s[0], y = (*(const CsPiece *const *)b)->s[0];
    if (x < y) return -1;
    return y < x;
}
/* FUN_1801e7350 (l.323767): the batch qsorted by its lower ends (MSVC qsort), the hint reset, FUN_1801e5200 per record */
static void batch_flush(CsScan *S)
{
    msvc_qsort(S->bptr, (unsigned long long)S->nbatch, sizeof(CsPiece *), cmp_lo);
    if (S->hint_on) { S->hprev = -1; S->hnode = S->ehead; }
    for (short k = 0; k < S->nbatch; k++) merge_piece(S, S->bptr[k]);
    S->nbatch = 0;
}
/* FUN_1801e40c0 (l.321821): one piece into the batch (DAT_1804ea640, 0x58-byte records; DAT_1804ffe00 the pointers), its ends ordered by
   s (s1 <= s0: the second end first), flushed at DAT_1804d5fb8 - 1 = 999 records */
static void batch_add(CsScan *S, const CsPiece *p)
{
    if (S->nbatch < 999) {
        int lo = p->s[1] < p->s[0] || p->s[1] == p->s[0], hi = !lo;
        CsPiece *r = &S->batch[S->nbatch];
        r->code = p->code;
        r->s[0] = p->s[lo]; r->s[1] = p->s[hi]; r->z[0] = p->z[lo]; r->z[1] = p->z[hi];
        CP3(r->n[0], p->n[lo]); CP3(r->n[1], p->n[hi]);
        S->bptr[S->nbatch] = r;
        S->nbatch++;
    }
    if (S->nbatch == 999) batch_flush(S);
}
/* the line's pieces (S->pc, in the order FUN_1801db8c0 produced them) through FUN_1801e40c0 and the final FUN_1801e7350 of FUN_1801db8c0;
   the result is the line list from S->ehead and the walls S->wall[0..nwall) */
int cs_merge_line(CsScan *S)
{
    if (!S->batch) { S->batch = malloc(sizeof(CsPiece) * 1000); S->bptr = malloc(sizeof(CsPiece *) * 1000); if (!S->batch || !S->bptr) return -1; }
    if (!S->wall) { S->cwall = 2000; S->wall = calloc((size_t)S->cwall, sizeof(CsNode)); if (!S->wall) return -1; }   /* FUN_1801ed720 */
    for (int i = 0; i < S->npc; i++) batch_add(S, &S->pc[i]);
    batch_flush(S);
    return S->eerr ? -1 : 0;
}

/* a node as the DLL's 0x68 bytes (padding and +0x62.. zero) */
void cs_node_bytes(const CsNode *n, unsigned char out[0x68])
{
    memset(out, 0, 0x68);
    out[0] = n->f0; memcpy(out + 8, &n->s0, 8); memcpy(out + 0x10, &n->z0, 8); memcpy(out + 0x18, n->n0, 24);
    out[0x30] = n->f1; memcpy(out + 0x38, &n->s1, 8); memcpy(out + 0x40, &n->z1, 8); memcpy(out + 0x48, n->n1, 24);
    memcpy(out + 0x60, &n->code, 2);
}

/* ------------------------------------------------------------------ FUN_1801e02b0: the line list cleaned (DAT_180501d60, after FUN_1801db8c0) */
/* FUN_18015bc40 (l.231117) */
static int code_bc40(short c)
{
    switch (c - 1) { case 0: case 2: case 7: case 9: case 0xe: case 0x10: case 0x12: case 0x14: case 0x19: case 0x1b: case 0x20: case 0x22:
                     return 1; }
    return 0;
}
/* FUN_1801e7d60 (l.324208): a node's length in s (wall codes: DAT_1804ea570) */
static double node_len(const CsScan *S, short k)
{
    const CsNode *N = &S->est[k];
    double a = N->s1, b = N->s0;
    if (code_wall(N->code)) return S->e;
    return a - b;
}
/* FUN_1801e9ec0 (asm 1801e9ec0..1801ea2d5): runs of nodes shorter than DAT_1804ea578 merged into one flat node */
static void clean_short(CsScan *S)
{
    CsNode *E = S->est;
    const double tol = S->systol;
    short prev = -1, k = S->ehead;
    while (k != -1) {
        short di = E[k].next, si;
        double L = node_len(S, k);
        if (tol > L) {
            double zm, n[3];
            short lg = k;
            double z0 = E[k].z0, z1 = E[k].z1;
            zm = z0 > z1 ? z0 : z1;
            if (z0 > z1) CP3(n, E[k].n0); else CP3(n, E[k].n1);
            si = k;
            for (;;) {                                   /* 1801e9f81 */
                di = E[si].next;
                if (di == -1) break;
                if (node_len(S, di) > tol) break;
                if (E[di].s0 - E[si].s1 > tol) break;
                if (E[di].s1 - E[k].s0 > S->e) break;
                double l1 = E[di].s1 - E[di].s0;
                if (l1 > L) { lg = di; L = l1; }
                if (E[di].z0 > zm) zm = E[di].z0;        /* 1801ea00e: the normal is not taken (comisd xmm0, xmm0) */
                if (E[di].z1 > zm) zm = E[di].z1;
                si = di;
            }
            int fromnext = 0;
            cs_cov3[11]++;
            E[k].code = E[lg].code;                      /* 1801ea04c */
            if (prev != -1) {
                double g1 = E[k].s0 - E[prev].s1;
                if (di != -1 && tol > g1) {
                    double g2 = E[di].s0 - E[si].s1;
                    if (tol > g2) {
                        if (g2 > g1) goto take_prev;
                        goto take_next;
                    }
                }
                if (tol > g1) {                          /* 1801ea0f3 */
take_prev:          cs_cov3[12]++; E[k].code = E[prev].code; zm = E[prev].z1; CP3(n, E[prev].n1);
                    E[k].s0 = E[prev].s1;
                    goto set;
                }
            }
            if (di != -1 && tol > E[di].s0 - E[si].s1) { /* 1801ea139 */
take_next:      cs_cov3[13]++; E[k].code = E[di].code; zm = E[di].z0; CP3(n, E[di].n0); fromnext = 1;
            }
set:        CP3(E[k].n0, n); CP3(E[k].n1, n); E[k].z0 = zm; E[k].z1 = zm;   /* 1801ea184 */
            E[k].s1 = fromnext ? E[di].s0 : E[si].s1;
            for (short j = E[k].next; j != di; j = E[j].next) {
                E[j].s0 = E[k].s1; E[j].s1 = E[k].s1; E[j].z0 = E[k].z1; E[j].z1 = E[k].z1;
                CP3(E[j].n0, E[k].n1); CP3(E[j].n1, E[k].n1); E[j].code = E[k].code;
            }
        } else si = k;
        if (prev != -1 && tol > E[k].s0 - E[prev].s1) E[prev].s1 = E[k].s0;   /* 1801ea25e */
        prev = si; k = di;
    }
}
/* FUN_1801e3190 (asm 1801e3190..1801e3349): from k, the run of joined nodes of one kind (start, end) */
static void clean_run(const CsScan *S, short k, short *rs, short *re)
{
    const CsNode *E = S->est;
    short p = -1;
    *rs = -1; *re = -1;
    for (; k != -1; p = k, k = E[k].next) {
        if (*rs != -1) {
            if (fabs(E[k].s0 - E[p].s1) > S->half) return;
            if ((code_wall(E[p].code) || code_wall(E[k].code)) && fabs(E[k].z0 - E[p].z1) > S->half) return;
            short pc = E[p].code, kc = E[k].code;
            if (pc == 0) { if (kc != 0) return; } else if (kc == 0) return;
            if ((pc >= 0x12) != (kc >= 0x12)) return;
            if (code_bc40(pc) != code_bc40(kc)) return;
        }
        if (*rs == -1) *rs = k;
        *re = k;
    }
}
static double nlen(const CsNode *N)
{
    double ds = N->s1 - N->s0, dz = N->z1 - N->z0, q = ds * ds + dz * dz;
    if (0.0 < q) q = sqrt(q);
    return q;
}
/* dist_pt_l (mccore 0x1804b32a0, asm): the distance of p to the line (l[0], l[1]) - (l[2], l[3]) */
static double dist_pt_l(double tol, double px, double py, const double *l)
{
    double dx = px - l[0], dy = py - l[1], ly = l[3] - l[1], lx = l[2] - l[0];
    double L = sqrt(ly * ly + lx * lx);
    if (L <= tol) return sqrt(dy * dy + dx * dx);
    double d = (dx * ly - dy * lx) / L;
    if (d < 0.0) d = -d;
    return d;
}
/* FUN_1801e6fe0 (l.323651): the normal of node k at its start (end = 1) or end against the normal interpolated along the run, and the
   angles of the run's chord and of k to it */
static double norm_dev(const CsScan *S, double tol, const double *line, short k, double lk, double acc, double total, const double *nab, int end)
{
    const CsNode *N = &S->est[k];
    const double *nk = end ? N->n0 : N->n1;
    if (!end) acc = lk + acc;
    double t = acc / total;
    double x = (nab[3] - nab[0]) * t + nab[0], y = (nab[4] - nab[1]) * t + nab[1], z = (nab[5] - nab[2]) * t + nab[2];
    /* comisd / jbe (asm 1801a5cb6..1801a5cd6, = the scanner twin): an unordered (NaN: total 0 for a run of zero-length nodes) value
       takes the normalising branch, as do the ordered half <= |v| */
    if (!(S->half > fabs(x)) || !(S->half > fabs(y)) || !(S->half > fabs(z))) {
        double r = sqrt(x * x + y * y + z * z);
        if (r == 0.0) { z = 0.0; y = 0.0; x = 0.0; } else { x = x / r; y = y / r; z = z / r; }
    } else { z = 1.0; y = 0.0; x = 0.0; }
    double m = nk[0] - x, d1 = nk[1] - y;
    if (fabs(m) < fabs(d1)) m = d1;
    d1 = nk[2] - z;
    if (fabs(m) < fabs(d1)) m = d1;
    if (!(tol > fabs(m))) return fabs(m);                        /* asm 1801a5d8a: jbe, NaN included */
    double ly = line[3] - line[1], lx = line[2] - line[0], l0;
    double L = sqrt(lx * lx + 0.0 + ly * ly);
    if (L == 0.0) { ly = 0.0; lx = 0.0; l0 = 0.0; } else { lx = lx / L; ly = ly / L; l0 = 0.0 / L; }
    double a1 = acos(fabs(l0 * x + lx * y + ly * z));
    double ds = N->s1 - N->s0, dz = N->z1 - N->z0, q2 = 0.0, q0;
    double L2 = sqrt(ds * ds + 0.0 + dz * dz);
    if (L2 == 0.0) { ds = 0.0; q0 = 0.0; } else { ds = ds / L2; q2 = dz / L2; q0 = 0.0 / L2; }
    double a2 = acos(fabs(nk[0] * q0 + nk[1] * ds + nk[2] * q2));
    if (!(fabs(a1 - a2) > 0.2617993877991494)) return fabs(m);   /* asm 1801a5ecb: jbe, NaN included */
    return tol + 1.0;
}
/* FUN_1801e6a70 (l.323432): can the run a .. b be joined with c into one straight node (the chord a -> c's start, else a -> c's end; *flag
   = 1 when c's end) */
static int run_straight(const CsScan *S, short a, short b, short c, double L, char *flag)
{
    const CsNode *E = S->est;
    double tol = S->half;
    *flag = 0;
    double lc = nlen(&E[c]);
    for (int pass = 0; pass < 2; pass++) {
        double line[4], nab[6], total;
        if (pass == 0) {
            /* asm 1801a5727..1801a5748: comisd / jb - an unordered value also takes pass 0 */
            if (!(!(tol >= fabs(E[c].s0 - E[b].s1) * 1000.0) || !(tol >= fabs(E[c].z0 - E[b].z1) * 1000.0))) { *flag = 1; continue; }
            line[0] = E[a].s0; line[1] = E[a].z0; line[2] = E[c].s0; line[3] = E[c].z0;
            CP3(nab, E[a].n0); CP3(nab + 3, E[c].n0); total = L;
        } else {
            *flag = 1;
            line[0] = E[a].s0; line[1] = E[a].z0; line[2] = E[c].s1; line[3] = E[c].z1;
            CP3(nab, E[a].n0); CP3(nab + 3, E[c].n1); total = lc + L;
        }
        double acc = 0.0, d9 = 0.0;
        short p = -1;
        for (short k = a; k != -1; k = E[k].next) {
            double lk = nlen(&E[k]);
            if (k != a && (pass != 0 || k != c) &&
                (tol < fabs(E[k].s0 - E[p].s1) * 1000.0 || tol < fabs(E[k].z0 - E[p].z1) * 1000.0)) {
                if (!(tol > dist_pt_l(S->ctol, E[k].s0, E[k].z0, line))) return 0;   /* asm 1801a59d7: jbe, NaN included */
                d9 = norm_dev(S, tol, line, k, lk, acc, total, nab, 1);
            }
            if (!(tol > d9)) return 0;                                /* asm 1801a5a3b: jbe, NaN included */
            if (k != c) {
                if (!(tol > dist_pt_l(S->ctol, E[k].s1, E[k].z1, line))) return 0;
                d9 = norm_dev(S, tol, line, k, lk, acc, total, nab, 0);
            }
            if (!(tol > d9)) return 0;                                /* asm 1801a5b02 */
            if (k == c) break;
            acc = acc + lk; p = k;
        }
    }
    return 1;
}
/* FUN_1801e2680 (l.320692): a .. b become one node b from a's start (the nodes before b degenerate, code 0x24); flag: b ends at c's start */
static void run_join(CsScan *S, short a, short b, int flag, short c)
{
    CsNode *E = S->est;
    cs_cov3[14]++;
    E[b].s0 = E[a].s0; E[b].z0 = E[a].z0; CP3(E[b].n0, E[a].n0);
    for (short k = a; k != b; k = E[k].next) {
        E[k].s0 = E[a].s0; E[k].z0 = E[a].z0; E[k].s1 = E[a].s0; E[k].code = 0x24; E[k].z1 = E[a].z0;
    }
    if (flag) { E[b].s1 = E[c].s0; E[b].z1 = E[c].z0; }
}
/* FUN_1801e23f0 (l.320578): the straight stretches of the run *rs .. re joined */
static void clean_straight(CsScan *S, short *rs, short re)
{
    CsNode *E = S->est;
    short k = *rs, st = 0, pv = 0;
    int cnt = 0; double L = 0.0;
    if (k == -1) return;
    for (;;) {
        short cur = k;
        if (cnt == 0) { L = nlen(&E[cur]); cnt = 1; st = cur; }
        else {
            char flag;
            if (!run_straight(S, st, pv, cur, L, &flag)) {
                if (st == *rs) *rs = pv;
                run_join(S, st, pv, flag, cur);
                L = nlen(&E[cur]); cnt = 1; st = cur;
            } else { cnt++; L = L + nlen(&E[cur]); }
        }
        if (cur == re) break;
        k = E[cur].next; pv = cur;
        if (k == -1) break;
    }
    if (cnt > 1) {
        if (st == *rs) *rs = re;
        run_join(S, st, re, 0, re);
    }
}
void cs_clean_line(CsScan *S)
{
    clean_short(S);
    cs_clean_runs(S);
}
/* FUN_1801e02b0's loop after FUN_1801e9ec0: per run FUN_1801e3190 + FUN_1801e23f0. FUN_1801cfb60's FUN_1801974e0 runs the same code on
   its own store (asm identical but for the globals: FUN_1801a2cc0 / FUN_1801a1d80 / FUN_1801a55f0 / FUN_1801a5bc0 / FUN_1801a2650 =
   FUN_1801e3190 / FUN_1801e23f0 / FUN_1801e6a70 / FUN_1801e6fe0 / FUN_1801e2680, DAT_1804e9328 / 9330 / 9240 for DAT_1804ea4e0 / ea5f0 /
   ea568; surf_classic3.md 12.19), so it calls this with a CsScan view of that store (est, ehead, half, ctol) */
void cs_clean_runs(CsScan *S)
{
    short k = S->ehead;
    while (k != -1) {
        short rs, re;
        clean_run(S, k, &rs, &re);
        k = re;
        if (re != -1) k = S->est[re].next;
        short r0 = rs;
        clean_straight(S, &rs, re);
        if (S->trace) printf("RUN %d %d %d %d\n", S->trace_line, r0, re, rs);
    }
}

/* ------------------------------------------------------------------ FUN_1801e0680: the line's block of the scan file */
static void buf_put(CsBuf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 4096; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, p, n); b->n += n;
}
static void rec30(CsBuf *b, double s, double z, const double *n, short code, unsigned char f)
{
    unsigned char r[0x30]; memset(r, 0, sizeof r);   /* +0x2b..+0x2f: the DLL's stack bytes (not set) */
    memcpy(r, &s, 8); memcpy(r + 8, &z, 8); memcpy(r + 0x10, n, 24); memcpy(r + 0x28, &code, 2); r[0x2a] = f;
    buf_put(b, r, 0x30);
}
/* the list from S->ehead (codes 0x24 skipped) as points: each node's start, joints within DAT_1804ea578 (s, z and, with flag 1 (the
   scanner's local_31c8[0] = param_2), the normal) one midpoint with code + 0x27, else the end of the one and the start of the next; the
   last end; then the 0x18 trailer (+0 1, +2 n, +4 the line's previous table entry, +8 the line position); table[line] = the trailer's
   offset. Nothing for an empty line (asm 1801e0680..1801e0a9c) */
int cs_line_write(CsScan *S, int flag, CsBuf *out, int *off, int *table, int line, double pos)
{
    const CsNode *E = S->est;
    const double tol = S->systol;
    short last = -1, n = 0;
    for (short k = S->ehead; k != -1; k = E[k].next) {
        if (E[k].code == 0x24) continue;
        if (last == -1) { rec30(out, E[k].s0, E[k].z0, E[k].n0, E[k].code, E[k].f0); n++; last = k; continue; }
        const CsNode *P = &E[last], *K = &E[k];
        int j = fabs(P->s1 - K->s0) <= tol && fabs(P->z1 - K->z0) <= tol;
        if (j && flag) j = fabs(P->n1[0] - K->n0[0]) <= tol && fabs(P->n1[1] - K->n0[1]) <= tol && fabs(P->n1[2] - K->n0[2]) <= tol;
        if (j) {
            rec30(out, (P->s1 + K->s0) * 0.5, (P->z1 + K->z0) * 0.5, K->n0, (short)(K->code + 0x27), K->f0);
            n++; cs_cov3[15]++;
        } else {
            rec30(out, P->s1, P->z1, P->n1, P->code, P->f1);
            rec30(out, K->s0, K->z0, K->n0, K->code, K->f0);
            n += 2; cs_cov3[16]++;
        }
        last = k;
    }
    if (last == -1) return 0;
    rec30(out, E[last].s1, E[last].z1, E[last].n1, E[last].code, E[last].f1);
    n++;
    unsigned char t[0x18]; memset(t, 0, sizeof t);   /* +1, +0x10..+0x17: stack bytes in the DLL */
    t[0] = 1; memcpy(t + 2, &n, 2); memcpy(t + 4, &table[line], 4); memcpy(t + 8, &pos, 8);
    buf_put(out, t, 0x18);
    *off += n * 0x30;
    table[line] = *off;
    *off += 0x18;
    return 0;
}
/* FUN_1801ed9b0 (l.327643): the line's walls into plmvcs00.tmp (0x68-byte records, then 0x18: +2 n, +4 the line's previous block, +8 the
   line position, +0x10 0), the line's table entry (+0 offset, +4 count) updated */
int cs_wall_write(CsScan *S, CsBuf *out, int *off, unsigned char *table, int line, double pos)
{
    for (int k = 0; k < S->nwall; k++) { unsigned char b[0x68]; cs_node_bytes(&S->wall[k], b); buf_put(out, b, 0x68); }
    short c; memcpy(&c, table + 8 * line + 4, 2); c = (short)(c + S->nwall); memcpy(table + 8 * line + 4, &c, 2);
    if (S->wmax < c) S->wmax = c;
    unsigned char t[0x18]; memset(t, 0, sizeof t);   /* +0 / +1, +0x12..+0x17: stack bytes in the DLL */
    short n = (short)S->nwall; memcpy(t + 2, &n, 2); memcpy(t + 4, table + 8 * line, 4); memcpy(t + 8, &pos, 8);
    buf_put(out, t, 0x18);
    *off += S->nwall * 0x68;
    memcpy(table + 8 * line, off, 4);
    *off += 0x18;
    S->nwall = 0;
    return 0;
}

/* mccore l_ints(l1, l2, pt, &t1, &t2, &flag) for the stage C ports (cam3d_cscallop_fd80.c FUN_1801cc0f0 calls it directly through its
   import 0x1803e7340): the same function as above, tol = mccore tol */
int cs_l_ints(double tol, const double *a0, const double *a1, const double *b0, const double *b1, double *pt, double *ta, double *tb)
{
    return l_ints(tol, a0, a1, b0, b1, pt, ta, tb);
}
