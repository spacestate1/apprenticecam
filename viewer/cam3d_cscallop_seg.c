/* cam3d_cscallop_seg.c - Constant Scallop (op 43) stage D2 part: FUN_18018a640, the 0xb0 segment records of one direction of tmpemesh.tmp
 * (formats/surf_classic3.md 12.27). Source: objdump of mcmsurf.dll, asm 18018a640..18018af55 (the decompile, l.264180-264516, mixes the
 * point's s and z and drops the stack arguments); callees FUN_1801a6f50 (trailer), FUN_1801abc40 / FUN_1801abf40 (kind 2: the block's
 * points read raw, 0x18 each), FUN_1801aac80 (kind 2: +0 s, +8 z, +0x10 code, +0x12 flag), FUN_180195e70, FUN_1801a9c80.
 * The point buffer (p3, FUN_1801ad6c0 / FUN_1801aaa50 regrown to count + 100 when short) only holds the block's raw points: the port reads
 * them from the file directly (a failed calloc there is not modelled). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

static int r32(const unsigned char *p) { int v; memcpy(&v, p, 4); return v; }
static short r16(const unsigned char *p) { short v; memcpy(&v, p, 2); return v; }
static double r64(const unsigned char *p) { double v; memcpy(&v, p, 8); return v; }

/* FUN_180195e70(m, code) (asm 180195e70..180195ea4): m 1: code 0..3 or 0x28..0x2a; m 2: 0..0x11 or 0x28..0x2a (unsigned compares) */
int cs_code_5e70(int m, int code)
{
    unsigned c = (unsigned)(short)code;
    if ((short)m == 1) return c <= 3 || c - 0x28 <= 2;
    if ((short)m == 2) return c <= 0x11 || c - 0x28 <= 2;
    return 0;
}

/* FUN_1801a9c80(0, hdr, R) (asm 1801a9c80..1801a9d41): R unlinked from its line's list (the line = R +0x24); the line's two counts - 1
   (+0x12 only when > 0); with p1 != 0 it would go on the free list DAT_1804e97b8 (not here) */
static void seg_unlink(CsSegLine *H, CsSeg *S, int r)
{
    CsSeg *R = &S[r];
    CsSegLine *L = &H[R->line];
    if (R->prev == -1) { L->first = R->next; if (R->next != -1) S[R->next].prev = -1; }
    else S[R->prev].next = R->next;
    if (R->next == -1) { L->last = R->prev; if (R->prev != -1) S[R->prev].next = -1; }
    else S[R->next].prev = R->prev;
    L->n10--;
    if (L->n12 > 0) L->n12--;
}

/* FUN_18018a640(dir, FUN_1801a6f50, buf, p4, p5, p6, p7, p8, p9, arr, &n25, ctx, &err) for header kind 0x11 (stride 0x18). Returns the
   number of segments asked for (local_14c + 1; arr holds min(that, p9)); *err = FUN_180268e20(0) = -11 on a read failure. */
int cs_seg_dir(const CsEmCtx *X, double tol, int dir, int p4, int p5, double p6, double p7, int p9, CsSeg *S, CsSegLine *H, int *n25,
               short *err)
{
    const unsigned char *f = X->f;
    int n = dir ? X->nB : X->nA;
    const int *tab = dir ? X->tabB : X->tabA;
    double pos = dir ? X->firstB : X->firstA, step = dir ? X->stepB : X->stepA;   /* DAT_1804e92b8 / 9228 (asm 18018a68e..18018a717) */
    const long stride = 0x18;                                                         /* FUN_1801ae2b0(2) */
    int cur = -1, over = -1;                                                          /* r13 / [rsp+0x60], [rsp+0x4c] */
    *err = 0; *n25 = 0;
    for (int line = 0; line < n; line++) {
        long lo = 1, hi = 0;                                                          /* [rbp-0x68] / [rsp+0x78] */
        CsSegLine *L = &H[line];
        L->first = -1; L->last = -1; L->n10 = 0; L->n12 = 0; L->dir = (unsigned char)dir; L->pos = pos;
        long off = tab[line];
        int ok = 1;
        while (off != -1 && ok) {
            if (off < 0 || off + 0x18 > (long)X->len) { *err = -11; return over + 1; }
            short cnt = r16(f + off + 2); long nx = r32(f + off + 4); double tpos = r64(f + off + 8);
            if (cnt < 1) { *err = -11; return over + 1; }
            L->pos = tpos;
            long last = off - stride;                                                 /* [rbp-0x48] */
            long first = off - cnt * stride;                                          /* DAT_1804e92ec, [rbp-0x38] */
            if (first < 0) { *err = -11; return over + 1; }
            long po = first - stride;                                                 /* r12 */
            int in = 0;                                                               /* sil */
            short eff = 0;                                                            /* r14w: the previous point's code (- 0x27) */
            for (short k = 0; k < cnt; k++) {
                po += stride;
                const unsigned char *q = f + po;
                double s = r64(q), z = r64(q + 8); short code = r16(q + 0x10);
                if (code == 0x25) (*n25)++;
                short pe = eff;
                eff = code < 0x27 ? code : (short)(code - 0x27);
                if (!in) {
                    if (!cs_code_5e70(p5, eff)) continue;
                    double d;
                    if (L->n10 < 1 || lo > po || po > hi) d = 1e38;              /* 0x1803ea528 */
                    else {
                        const CsSeg *P = &S[cur];
                        double ps = P->s1, pz = P->z1;
                        d = p4 ? fabs(ps - s) : tol + tol;
                        if (d > tol) { double dz = z - pz, ds = s - ps; d = sqrt(dz * dz + ds * ds); }
                    }
                    if (d > p7) {                                                     /* asm 18018aaf9..18018abfc */
                        over++;
                        if (cur < p9 - 1) cur++;
                        CsSeg *R = &S[cur];
                        if (L->first == -1) L->first = cur;
                        L->last = cur; L->n10++; L->n12++;
                        R->p28 = (int)po; R->p2c = (int)po;
                        R->pt = R->cd = R->fl = -1; R->i34 = 0; R->i38 = 0; R->z3c = 0; R->s5a = 0; R->s5c = 0;
                        if (L->n10 > 1) { S[cur - 1].next = cur; R->prev = cur - 1; } else R->prev = -1;
                        R->next = -1; R->z10 = 0; R->z18 = 0; R->b20 = 0;
                        R->s0 = s; R->z0 = z; R->sa0 = s; R->s1 = s; R->z1 = z; R->sa8 = s;
                        lo = first; hi = last;
                    } else {                                                          /* asm 18018ac01 */
                        CsSeg *R = &S[cur];
                        R->p2c = (int)po; R->s1 = s; R->sa8 = s; R->z1 = z;
                    }
                    in = 1;
                } else {                                                              /* asm 18018ac27.. */
                    CsSeg *R = &S[cur];
                    if (cs_code_5e70(p5, pe) || R->p2c == R->p28) { R->p2c = (int)po; R->s1 = s; R->sa8 = s; R->z1 = z; }
                    if (code >= 0x27 && cs_code_5e70(p5, eff)) { in = 1; continue; }
                    in = 0;
                    if (fabs(R->s0 - R->s1) > 0.0 || fabs(R->z0 - R->z1) > 0.0) {      /* finished (asm 18018ad3a) */
                        R->line = line; R->s58 = R->s5a; R->s5e = R->s5c;
                        R->v60 = R->s0; R->v68 = R->z0; R->v90 = R->s1; R->v98 = R->z1;
                    } else {                                                          /* a point: dropped (asm 18018acc1) */
                        over--; cur--;
                        L->n10--; L->n12--;
                        if (L->n10 == 0) { L->first = -1; L->last = -1; }
                        else { L->last = cur; S[cur].next = -1; }
                        lo = cur >= 0 ? S[cur].p28 : 0; hi = cur >= 0 ? S[cur].p2c : 0;
                    }
                }
            }
            off = nx;
        }
        pos = pos + step;
    }
    if (over <= p9 - 1)                                                               /* asm 18018ae2b..18018af04 */
        for (int line = 0; line < n; line++)
            for (int r = H[line].first; r != -1; ) {
                CsSeg *R = &S[r];
                double ds = fabs(R->s0 - R->s1), dz = fabs(R->z0 - R->z1);
                if (p6 > ds && p6 > dz && p6 > sqrt(dz * dz + ds * ds)) seg_unlink(H, S, r);
                r = R->next;
            }
    return over + 1;
}

/* a record as the DLL's 0xb0 bytes with the pointers as indices (+0 next, +8 prev: index + 1, 0 = none) and +0x30..+0x33 (never written)
   zero: the unit check's form */
void cs_seg_bytes(const CsSeg *R, unsigned char o[0xb0])
{
    memset(o, 0, 0xb0);
    long long a = R->next + 1, b = R->prev + 1;
    memcpy(o, &a, 8); memcpy(o + 8, &b, 8); memcpy(o + 0x10, &R->z10, 8); memcpy(o + 0x18, &R->z18, 8); o[0x20] = R->b20;
    memcpy(o + 0x24, &R->line, 4); memcpy(o + 0x28, &R->p28, 4); memcpy(o + 0x2c, &R->p2c, 4);
    memcpy(o + 0x30, &R->s30, 2); memcpy(o + 0x32, &R->s32, 2); memcpy(o + 0x34, &R->i34, 4); memcpy(o + 0x38, &R->i38, 4); memcpy(o + 0x3c, &R->z3c, 2);
    long long q[3] = { R->pt + 1, R->cd + 1, R->fl + 1 };   /* the arrays' entries as index + 1, 0 = NULL */
    memcpy(o + 0x40, q, 0x18);
    memcpy(o + 0x58, &R->s58, 2); memcpy(o + 0x5a, &R->s5a, 2); memcpy(o + 0x5c, &R->s5c, 2); memcpy(o + 0x5e, &R->s5e, 2);
    memcpy(o + 0x60, &R->v60, 8); memcpy(o + 0x68, &R->v68, 8); memcpy(o + 0x70, &R->s0, 8); memcpy(o + 0x78, &R->z0, 8);
    memcpy(o + 0x80, &R->s1, 8); memcpy(o + 0x88, &R->z1, 8); memcpy(o + 0x90, &R->v90, 8); memcpy(o + 0x98, &R->v98, 8);
    memcpy(o + 0xa0, &R->sa0, 8); memcpy(o + 0xa8, &R->sa8, 8);
}

/* ---- stage D2 (formats/surf_classic3.md 12.29) ---------------------------------------------------------------------------------- */

/* FUN_1801b4e80(n, table, file, FUN_1801a6e70, &mx, &tot) (asm 1801b4e80..1801b4f7d; FUN_1801b1ad0 calls it for A at asm 1801b1f84 and for
   B at asm 1801b223a): per line the counts of its blocks along the chain (trailer +2 count, +4 previous block; a read failure or a count
   < 1 ends the line), mx = the largest line sum (short) + 10, tot = the sum over all lines + 10 */
void cs_seg_sizes(const CsEmCtx *X, int dir, short *mx, int *tot)
{
    int n = dir ? X->nB : X->nA;
    const int *tab = dir ? X->tabB : X->tabA;
    *mx = 0; *tot = 0;
    for (int i = 0; i < n; i++) {
        short sum = 0; long o = tab[i];
        while (o != -1) {
            if (o < 0 || (size_t)o + 0x18 > X->len) break;
            short c = r16(X->f + o + 2);
            if (c < 1) break;
            sum = (short)(sum + c); *tot += c;
            o = r32(X->f + o + 4);
        }
        if (*mx < sum) *mx = sum;
    }
    *mx = (short)(*mx + 10); *tot += 10;
}

/* FUN_1801a7fd0(DAT_1804d5729 = 2, mx, tot, DAT_1804d53b8, descriptors, ..) (asm 1801a7fd0..; FUN_1801b1ad0 asm 1801b2552 / 1801b2670):
   when tot < FUN_1801aaaa0(DAT_1804d53b8) (the number of 0x13-byte points - 0x10 point + 2 code + 1 flag - left in the licence-sized
   buffer: pl2MemBufRemainFit(0x13) / edge_fit(0x13)) only FUN_1801a7f00 allocates: descriptor 4 = tot x 0x10 (the (s, z) points),
   5 = tot x 2 (the codes), 6 = tot x 1 (the flags) (asm 1801a7f1a..1801a7f9c), counts p14 / p16 / p18 = tot; descriptors 0..3 stay NULL.
   With the ~18 MB buffer (N13-28) this in-memory arm is the one ported; the windowed arm (descriptors 0..3 of mx entries and 4..6 of
   the buffer's fit, which FUN_1801b88e0 then fills only partly) depends on the buffer's state: NOT PORTED. */
int cs_seg_pts_alloc(CsSegPts *P, int tot)
{
    memset(P, 0, sizeof *P);
    if (tot < 1) return -1;
    P->pt = calloc((size_t)tot, sizeof *P->pt); P->cd = calloc((size_t)tot, 2); P->fl = calloc((size_t)tot, 1);
    if (!P->pt || !P->cd || !P->fl) { cs_seg_pts_free(P); return -1; }
    P->n4 = P->n5 = P->n6 = tot;
    return 0;
}
void cs_seg_pts_free(CsSegPts *P) { free(P->pt); free(P->cd); free(P->fl); memset(P, 0, sizeof *P); }

/* FUN_1801b88e0(file, FUN_1801a7880, nsegs, n4, n5, n6, records, flags, codes, points) (asm 1801b88e0..1801b8b0f; the decompile l.293214
   is right but for the argument order): for each of the first nsegs records of the array (unlinked ones included) the points from its
   first point's offset (+0x28) while the position after the read is <= its last point's offset (+0x2c), FUN_1801a7880 (asm 1801a7880:
   0x18 bytes: +0 s, +8 z, +0x10 code, +0x12 flag); the record's first point sets +0x40 / +0x48 / +0x50 to the arrays' next entries;
   an array that becomes full sets that field of the record that filled it to NULL and is not written again. Then +0x3c = the count k,
   +0x58 = 0 (int: +0x58 / +0x5a), +0x5c = +0x5e = k - 1. Returns 0 for nsegs < 1 or a read failure ("ERROR: trouble reading file: surf"
   into tmpdiag), else 1. */
int cs_seg_points(const CsEmCtx *X, int nsegs, CsSeg *S, CsSegPts *P)
{
    int full4 = P->n4 < 1, full5 = P->n5 < 1, full6 = P->n6 < 1, i4 = 0, i5 = 0, i6 = 0;
    if (nsegs < 1) return 0;
    for (int i = 0; i < nsegs; i++) {
        CsSeg *R = &S[i];
        long pos = R->p28;
        short k = 0;
        do {
            if (pos < 0 || (size_t)pos + 0x18 > X->len) return 0;
            const unsigned char *q = X->f + pos;
            pos += 0x18;
            if (k == 0) {
                if (!full4) R->pt = i4;
                if (!full5) R->cd = i5;
                if (!full6) R->fl = i6;
            }
            k++;
            if (!full4) { P->pt[i4][0] = r64(q); P->pt[i4][1] = r64(q + 8); if (++i4 == P->n4) { R->pt = -1; full4 = 1; } }
            if (!full5) { P->cd[i5] = r16(q + 0x10); if (++i5 == P->n5) { R->cd = -1; full5 = 1; } }
            if (!full6) { P->fl[i6] = q[0x12]; if (++i6 == P->n6) { R->fl = -1; full6 = 1; } }
        } while ((int)pos <= R->p2c);                                                 /* asm 1801b8a7c..1801b8a85: eax vs [rbx-0x1c] */
        R->z3c = k; R->s58 = 0; R->s5a = 0; R->s5c = (short)(k - 1); R->s5e = (short)(k - 1);
    }
    return 1;
}

/* FUN_1801b84f0(&ctx +0x418, nB, ctx +0x448) (FUN_1801b1ad0 asm 1801b26ec..1801b2700, direction B only) -> FUN_1801b8410 for every record
   on a line's list (asm 1801b8410..1801b84ec): the line position *= sc[1] (+0x420 = 1.0), +0x70, +0x80, +0x60, +0x90 *= sc[0] (+0x418 =
   -1.0), +0xa0 = +0x70, +0xa8 = +0x80, and when +0x40 is set the s of its points +0x58 .. +0x5e *= sc[0] */
void cs_seg_flip(const double sc[2], CsSegLine *H, int n, CsSeg *S, CsSegPts *P)
{
    for (int l = 0; l < n; l++)
        for (int r = H[l].first; r != -1; r = S[r].next) {
            CsSeg *R = &S[r];
            H[R->line].pos = sc[1] * H[R->line].pos;
            R->s0 = sc[0] * R->s0; R->s1 = R->s1 * sc[0]; R->sa0 = R->s0; R->sa8 = R->s1;
            R->v60 = R->v60 * sc[0]; R->v90 = sc[0] * R->v90;
            if (R->pt != -1)
                for (short k = R->s58; k <= R->s5e; k++) P->pt[R->pt + k][0] = sc[0] * P->pt[R->pt + k][0];
        }
}
