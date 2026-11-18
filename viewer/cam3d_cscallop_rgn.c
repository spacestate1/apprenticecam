/* cam3d_cscallop_rgn.c - Constant Scallop (op 43) stage C: the frame of FUN_1801cfb60's recut file temp.rgn (formats/surf_classic3.md 12.21).
 * Sources: decomp/mcmsurf_all.c (l.N) and objdump of mcmsurf.dll (asm). The line blocks themselves are cs_rgn_block (FUN_1801a1a70,
 * cam3d_cscallop_fd80.c); this file holds what FUN_1801cfb60 writes around them:
 *  - the tool block DAT_1804e9288 .. 9299 and DAT_1804e9148 (l.310085-310115, asm 1801d0981..1801d0abb; 9148 asm 1801d33af..1801d33b9);
 *  - FUN_1801c9350 (l.304982, asm 1801c9350..1801c951f): the rufvcs wall records per line copied after the blocks, with FUN_1801b0c80
 *    (l.288238) and its reader FUN_1801a6f50 (asm 1801a6f50..1801a6fba: returns read == 0x18);
 *  - the trailer (l.310883-310955, asm 1801d33df..1801d3780).
 * The header (FUN_18025d6d0 / FUN_180229640 / FUN_180268b30) is cs_scan_header; kind 6's end FUN_18022c850 is cs_scan_appendix. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

static void put(CsBuf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 4096; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, p, n); b->n += n;
}

/* FUN_1801cfb60 l.310085-310115 (asm 1801d0981..1801d0abb; xmm10 = param_4 the stock, xmm9 = param_5 r, xmm13 = param_6 D, xmm14 = 2.0
   (0x1803eb830), xmm11 = 0.0, 0x1803eb810 = 0.5, xmm10 then = DAT_1804e9248):
     s = stock, or -r when -stock > r (comisd + jbe: a NaN keeps the stock)   -> local_3620, DAT_1804e92a0
     a = s + r; c1 = a > 9248, else a = 0                                       -> DAT_1804e9260 / 9278, 9143 / 9280
     h = (2 s + D) x 0.5; b = h - a; c2 = b > 9248, else b = 0 and a = h       -> DAT_1804e9268 / 9270, 92a8 / 9281
   DAT_1804e9288 = b, 9290 = a, 9298 = c1, 9299 = c2 (929a..929f: .bss, never stored to: 0); local_3608 = a (asm 1801d0bce / 1801d0c28) and
   at the end DAT_1804e9148 = local_3608 - xmm10 (asm 1801d33af..1801d33b9; xmm10 = local_3620 = s again from asm 1801d1abf on the scanner
   path, callee-saved through the loop). */
void cs_rgn_tool(double stock, double r, double D, double t9248, CsRgnTool *T)
{
    double s = stock;
    if (-stock > r) s = -r;
    double a = s + r; int c1 = 1;
    if (!(a > t9248)) { c1 = 0; a = 0.0; }
    double h = (s * 2.0 + D) * 0.5;
    double b = h - a; int c2 = 1;
    if (!(b > t9248)) { c2 = 0; b = 0.0; a = h; }
    T->s = s; T->b9288 = b; T->a9290 = a; T->c9298 = (unsigned char)c1; T->c9299 = (unsigned char)c2; T->d9148 = a - s;
}

/* FUN_1801c9350(&DAT_1804e9120 rufvcs, &DAT_1804e9060 temp.rgn) (l.304982), called after the per-line loop when DAT_1804e92a9 (the scanner
   path, local_3644 = 1) and local_3671 (1 for kinds 6 / 7) (l.310858, asm 1801d3386..1801d33ab). vcs / vlen = the rufvcs file (the scanner's
   plmvcs00.tmp renamed by FUN_1801868a0), t98 = DAT_1804e9798 (n 8-byte entries {int offset, short count, 2 bytes} as FUN_1801e7c20 read them
   from the file's head: int n, n x 8 bytes), *a4 = DAT_1804e97a4 (FUN_1801868a0: the largest count of the table). The recut file is seeked to
   its end (out->n) = DAT_1804e9790. Per line (DAT_1804e92d8 = 0 .. n - 1):
     FUN_1801b0c80(rufvcs, FUN_1801a6f50, &local_38): count = 0; if the entry's count > DAT_1804e97a0 (500, l.310045) nothing; else from the
       entry's offset while it is not -1: seek there, read the 0x18 trailer {+2 short k, +4 int previous, +8 double -> local_38} (a short read
       ends the walk: FUN_1801a6f50 returns 0), k < 1 or k > 500 ends it too (the "reading file fb_vertd" message), else read the k 0x68
       records before it into DAT_1804e97a8 (a short read returns the count so far), count += k, offset = previous;
     count < 1: entry = {-1, (short)0} (bytes 6, 7 kept);
     else the count records written, then the 0x18 trailer {+0 / +1 not written (the DLL's stack: 0 here), +2 short count, +4 int -1, +8
       local_38, +0x10 short 0 (si, asm 1801c946d), +0x12..+0x17 not written (0 here)}, entry = {DAT_1804e9790 + count x 0x68, count},
       DAT_1804e9790 += count x 0x68 + 0x18, DAT_1804e97a4 = max(DAT_1804e97a4, count).
   The records go to DAT_1804e97a8, a pool of DAT_1804e97a0 = 500 records (FUN_1800ce330(500, 0x68, 1), l.310045-310048); FUN_1801b0c80 checks
   each block's count against it, not the sum: a line whose blocks hold more than 500 in all would run past the pool - NOT PORTED (-2).
   Returns 0, -1 (bad table), -2 (beyond the pool). */
int cs_rgn_vcs_copy(CsBuf *out, const unsigned char *vcs, size_t vlen, int n, unsigned char *t98, short *a4)
{
    int pos = (int)out->n;
    double lp = 0.0;   /* local_38 of FUN_1801c9350: set by the reader before any use */
    unsigned char *pool = malloc(500 * 0x68 + 1);
    for (int line = 0; line < n; line++) {
        unsigned char *e = t98 + 8 * (size_t)line;
        int off; short ec; memcpy(&off, e, 4); memcpy(&ec, e + 4, 2);
        int cnt = 0;
        if (ec <= 500) {
            long long o = off;
            while (o != -1) {
                short k; int prev;
                if (o < 0 || (size_t)o + 0x18 > vlen) break;                       /* the 0x18 read fails: FUN_1801a6f50 returns 0 */
                memcpy(&k, vcs + o + 2, 2); memcpy(&prev, vcs + o + 4, 4); memcpy(&lp, vcs + o + 8, 8);
                if (k < 1 || k > 500) break;
                long long st = o - (long long)k * 0x68;
                if (st < 0 || (size_t)st + (size_t)k * 0x68 > vlen) break;         /* short read: bVar3 false, the count so far */
                if (cnt + k > 500) { free(pool); return -2; }
                memcpy(pool + (size_t)cnt * 0x68, vcs + st, (size_t)k * 0x68);
                cnt += k; o = prev;
            }
        }
        if (cnt < 1) { int m1 = -1; short z = 0; memcpy(e, &m1, 4); memcpy(e + 4, &z, 2); continue; }
        put(out, pool, (size_t)cnt * 0x68);
        unsigned char t[0x18]; memset(t, 0, sizeof t);
        short c = (short)cnt; int m1 = -1;
        memcpy(t + 2, &c, 2); memcpy(t + 4, &m1, 4); memcpy(t + 8, &lp, 8);
        put(out, t, 0x18);
        int at = pos + cnt * 0x68;
        memcpy(e, &at, 4); memcpy(e + 4, &c, 2);
        if (*a4 < c) *a4 = c;
        pos = at + 0x18;
    }
    free(pool);
    return 0;
}

/* The trailer (l.310883-310955, asm 1801d33df..1801d3780; one pass: local_3654 = 0 for kinds 6 / 7, so bVar45 = 1), at DAT_1804e92ec =
   the file's position:
     short local_360c = 0 (l.309923 / 310841); DAT_1804e9150 / 9168 / 9180 (the frame rows -param_9, param_7, param_8: l.309983-309991, the
     scanner's 500 / 518 / 530 of the same arguments, l.326195-326201); DAT_1804e9238; DAT_1804e9240 / 5.0 (0x1803eb840); DAT_1804e9248;
     DAT_1804e9250; the 0x18 bytes at DAT_1804e9288 (cs_rgn_tool); param_6 (D); DAT_1804e9148 (tool types 0x19 / 0x17 with FUN_18021b330 /
     FUN_18021b3b0 take cur_op +0x2f0 instead: refused by the caller); DAT_1804e9230 (-param_16), DAT_1804e9228 (-param_17); int (param_21 !=
     0); then, as local_35e8 = 0 (l.310840), int DAT_1804e9144 and the n-int line table DAT_1804e92f8; int -1 (no containment: DAT_1804e8ffc
     = 0; else param_13 and two n x 8 arrays, NOT PORTED); short DAT_1804e97a4 (0 unless DAT_1804e92a9 and the copy ran, local_3656) and, when
     > 0, DAT_1804e9798 (n x 8 bytes). Then SysFile::Close. */
void cs_rgn_trailer(CsBuf *out, const double F[9], const CsCfbConst *C, const CsRgnTool *T, double D, double t9230, double t9228, int p21,
                    int n, const int *table, short a4, const unsigned char *t98)
{
    short z = 0; put(out, &z, 2);
    put(out, F, 24); put(out, F + 3, 24); put(out, F + 6, 24);
    put(out, &C->t9238, 8);
    double h5 = C->t9240 / 5.0; put(out, &h5, 8);
    put(out, &C->t9248, 8); put(out, &C->t9250, 8);
    unsigned char tb[0x18]; memset(tb, 0, sizeof tb);
    memcpy(tb, &T->b9288, 8); memcpy(tb + 8, &T->a9290, 8); tb[0x10] = T->c9298; tb[0x11] = T->c9299;
    put(out, tb, 0x18);
    put(out, &D, 8); put(out, &T->d9148, 8);
    put(out, &t9230, 8); put(out, &t9228, 8);
    int f = p21 != 0; put(out, &f, 4);
    put(out, &n, 4); put(out, table, (size_t)n * 4);
    int m1 = -1; put(out, &m1, 4);
    put(out, &a4, 2);
    if (a4 > 0) put(out, t98, (size_t)n * 8);
}
