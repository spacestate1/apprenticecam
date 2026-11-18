/* cam3d_cscallop_zone.c - Constant Scallop (op 43) stage D5: the zone builder mcmsurf FUN_180187850 (formats/surf_classic3.md 12.36).
 * Ported bottom-up from the objdump of mcmsurf.dll; each function checked against the DLL's own (tools/peloader --lmm D15 with D15_ZU,
 * tools/tests/d15_cmp.sh). Records and line headers are the port's CsSegDir (cam3d_cscallop.h): a record's +0x10 (z10), +0x20 (b20, "both
 * ends coded"), +0x24 (line), +0x30 / +0x32 (s30 / s32, the codes of its two ends) and a header's +0x10 / +0x12 (n10 records, n12 records
 * still open) are the fields the builder works on. */
#include <math.h>
#include <string.h>
#include "cam3d_cscallop.h"

/* FUN_1801bc5c0(n, hdrs) (asm 1801bc5c0..1801bc609): per line n10 = its record count, n12 = n10; every record's +0x20 byte, +0x30 dword
   (both end codes) and +0x10 qword zeroed. Returns the total record count. */
int cs_zb_count(CsSegDir *D)
{
    int tot = 0;
    for (int l = 0; l < D->nl; l++) {
        CsSegLine *H = &D->H[l];
        H->n10 = 0;
        for (int r = H->first; r != -1; r = D->S[r].next) {
            H->n10++; D->S[r].b20 = 0; D->S[r].s30 = 0; D->S[r].s32 = 0; D->S[r].z10 = 0; tot++;
        }
        H->n12 = H->n10;
    }
    return tot;
}

/* FUN_1801c34b0(rec, hdrs, &cnt) (asm 1801c34b0..1801c34d8), reached from FUN_1801c3490 (end +0x30 = code) / FUN_1801c3470 (end +0x32):
   a record with both ends coded and +0x20 still 0 gets +0x20 = 1, its line's n12 - 1 and cnt + 1 */
static void zb_both(CsSegDir *D, int r, int *cnt)
{
    CsSeg *R = &D->S[r];
    if (R->s30 != 0 && R->s32 != 0 && !R->b20) { D->H[R->line].n12--; R->b20 = 1; ++*cnt; }
}
void cs_zb_code0(CsSegDir *D, int r, short code, int *cnt) { D->S[r].s30 = code; zb_both(D, r, cnt); }   /* FUN_1801c3490 */
void cs_zb_code1(CsSegDir *D, int r, short code, int *cnt) { D->S[r].s32 = code; zb_both(D, r, cnt); }   /* FUN_1801c3470 */

/* FUN_1801bec50(cl, up, key, line, n, hdrs) (asm 1801bec50..1801bed27): line out of 0..n-1: none. Else along the line (up: from its first
   record by +0, else from its last by +8) the first record whose upper value (cl: +0x80 up / +0x70 down; else +0xa8 up / +0xa0 down) is
   not below key[0] (ordered compare: a NaN does not move on); returned when its lower value (cl: +0x70 / +0x80; else +0xa0 / +0xa8) is not
   above key[1], else none. Returns the record or -1. */
static int zb_bec50(const CsSegDir *D, int cl, int up, const double key[2], int line)
{
    if (line <= -1 || line >= D->nl) return -1;
    int r = up ? D->H[line].first : D->H[line].last;
    while (r != -1) {
        const CsSeg *R = &D->S[r];
        double lo, hi;
        if (up) { lo = cl ? R->s0 : R->sa0; hi = cl ? R->s1 : R->sa8; }
        else { lo = cl ? R->s1 : R->sa8; hi = cl ? R->s0 : R->sa0; }
        if (key[0] > hi) { r = up ? R->next : R->prev; continue; }   /* comisd / jbe: only an ordered "above" moves on */
        return lo > key[1] ? -1 : r;                                  /* cmova */
    }
    return -1;
}

/* FUN_1801c1c40(up, key, line, n, hdrs) (asm 1801c1c40..1801c1cd7): line out of range: none. Along the line (up: first by +0 with
   (lo, hi) = (+0x70, +0x80); else last by +8 with (+0x80, +0x70)): key[0] above hi: next; lo above key[1]: none; key[0] above lo and hi
   above key[1]: this record; else next. */
static int zb_c1c40(const CsSegDir *D, int up, const double key[2], int line)
{
    if (line <= -1 || line >= D->nl) return -1;
    int r = up ? D->H[line].first : D->H[line].last;
    while (r != -1) {
        const CsSeg *R = &D->S[r];
        double lo = up ? R->s0 : R->s1, hi = up ? R->s1 : R->s0;
        if (!(key[0] > hi)) {
            if (lo > key[1]) return -1;
            if (key[0] > lo && hi > key[1]) return r;
        }
        r = up ? R->next : R->prev;
    }
    return -1;
}

int cs_zb_c1c40(const CsSegDir *D, int up, const double key[2], int line) { return zb_c1c40(D, up, key, line); }
int cs_zb_bec50(const CsSegDir *D, int cl, int up, const double key[2], int line) { return zb_bec50(D, cl, up, key, line); }

/* FUN_1801c0320(n, hdrs, &cnt) (asm 1801c0320..1801c05c3): the ends at the domain's border. The sense "up" = +0xa8 > +0xa0 of the first
   record (lines 1..n-2, in list order) whose |+0xa0 - +0xa8| exceeds 1e-12 (0x1803eb800); none: nothing. Then per line L = 1..n-2, its
   records from the first (up) or the last: a record with no record of line L+1 nor of line L-1 over its (+0xa0, +0xa8) range (FUN_1801bec50)
   gets both ends -4; and where it and its neighbour (up: +8, else +0) leave a gap (up: |rec +0x70 - nb +0x80|, else |rec +0x80 - nb +0x70|)
   wider than DAT_1804e9250 (t9250) that both neighbouring lines cover (FUN_1801c1c40), the two facing ends get -5 (up: nb +0x32, rec +0x30;
   else nb +0x30, rec +0x32). */
void cs_zb_ends(CsSegDir *D, double t9250, int *cnt)
{
    int n = D->nl, up = -1;
    if (n - 1 <= 1) return;
    for (int l = 1; l < n - 1 && up < 0; l++)
        for (int r = D->H[l].first; r != -1; r = D->S[r].next) {
            double d = D->S[r].sa0 - D->S[r].sa8;
            if (fabs(d) > 1e-12) { up = D->S[r].sa8 > D->S[r].sa0; break; }
        }
    if (up < 0) return;
    for (int i = 0; i < n - 2; i++) {
        int L = i + 1;
        for (int r = up ? D->H[L].first : D->H[L].last; r != -1; ) {
            CsSeg *R = &D->S[r];
            double key[2] = { up ? R->sa0 : R->sa8, up ? R->sa8 : R->sa0 };
            if (zb_bec50(D, 0, up, key, i + 2) == -1 && zb_bec50(D, 0, up, key, i) == -1) {
                cs_zb_code0(D, r, -4, cnt); cs_zb_code1(D, r, -4, cnt);
            }
            int nb = up ? R->prev : R->next;
            if (nb != -1) {
                double k2[2] = { up ? D->S[nb].s1 : D->S[nb].s0, up ? R->s0 : R->s1 };
                if (fabs(k2[1] - k2[0]) > t9250 && zb_c1c40(D, up, k2, i + 2) != -1 && zb_c1c40(D, up, k2, i) != -1) {
                    if (up) { cs_zb_code1(D, nb, -5, cnt); cs_zb_code0(D, r, -5, cnt); }
                    else { cs_zb_code0(D, nb, -5, cnt); cs_zb_code1(D, r, -5, cnt); }
                }
            }
            r = up ? R->next : R->prev;
        }
    }
}

/* FUN_1801c32f0(code, n, hdrs, &cnt) (asm 1801c32f0..1801c3374): on lines 1..n-2 every end coded `code` back to 0; a record with +0x20 set
   and an end now 0 gets +0x20 = 0, its line's n12 + 1, cnt - 1. FUN_1801beff0(L, code, n, hdrs, &cnt) (asm 1801beff0) the same for `code`
   and every code c with FUN_1801bd8a0(c, L) (asm 1801bd8a0: c < 0, -L[0] < c and L[-c] == 1). */
static void zb_clear(CsSegDir *D, const short *L, short code, int *cnt)
{
    for (int l = 1; l < D->nl - 1; l++)
        for (int r = D->H[l].first; r != -1; r = D->S[r].next) {
            CsSeg *R = &D->S[r];
            if (R->s30 == code || (L && cs_zb_stall_on(L, R->s30))) R->s30 = 0;
            if (R->s32 == code || (L && cs_zb_stall_on(L, R->s32))) R->s32 = 0;
            if (R->b20 && (R->s30 == 0 || R->s32 == 0)) { D->H[R->line].n12++; R->b20 = 0; --*cnt; }
        }
}
void cs_zb_clear(CsSegDir *D, short code, int *cnt) { zb_clear(D, NULL, code, cnt); }
void cs_zb_clear_stall(CsSegDir *D, const short *L, short code, int *cnt) { zb_clear(D, L, code, cnt); }

/* FUN_1801bf0d0(c, v, L) (asm 1801bf0d0): c < 0 and -L[0] < c: L[-c] = v. FUN_1801bd8a0(c, L): that entry == 1. FUN_1801be460(m, L) (asm
   1801be460..1801be4b2): L[0] = m, L[1] = L[2] = L[3] = L[6] = 1, L[4] = L[5] = 0 (L[7] is not written). */
void cs_zb_stall_set(short *L, short c, short v) { if (c < 0 && -(int)L[0] < c) L[-c] = v; }
int cs_zb_stall_on(const short *L, short c) { return c < 0 && -(int)L[0] < c && L[-c] == 1; }
void cs_zb_stall_init(short *L, short m)
{
    L[0] = m;
    cs_zb_stall_set(L, -1, 1); cs_zb_stall_set(L, -2, 1); cs_zb_stall_set(L, -3, 1); cs_zb_stall_set(L, -6, 1);
    cs_zb_stall_set(L, -4, 0); cs_zb_stall_set(L, -5, 0);
}
