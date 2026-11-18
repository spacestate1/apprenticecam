/* cam3d_cscallop_zwalk.c - Constant Scallop (op 43) stage D5 / Z3: the walk FUN_1801bca00 of the zone builder (formats/surf_classic3.md
 * 12.42), bottom-up. Ported from the asm of mcmsurf.dll (the decompile drops arguments of these calls); each function checked against
 * the DLL's own by the unit log of its builder (tools/peloader --lmm D15_ZB D15_ZBUNIT, viewer/cfb_port D15_ZUNIT, tools/tests/zcore_cmp.sh).
 * Done: FUN_1801bed30, FUN_1801ab6d0, FUN_1801aef30, FUN_1801c5ef0, FUN_1801bf450 (the candidate gatherer), FUN_18016fb60 /
 * FUN_18017a2c0 / FUN_180147900 (their grid cells), FUN_18015bf40 (the chooser), FUN_18017c2e0; (12.44) FUN_18015c2e0 / FUN_1801bfdc0 /
 * FUN_18015a1c0 (the side test), FUN_18016a7b0 / FUN_180179090 (the cell runs) and FUN_180180090 (the score) with their trees. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cam3d_cscallop.h"

#define ZND (-101)

/* FUN_1801bed30(i, zbc) (asm 1801bed30..1801beebe): the step to take step i's direction from. dA / dB = the (x, y) distances
   (sqrt(dy^2 + dx^2), 0 without one) from step i to the last step of each direction (zbc +0xc4 / +0xc8); both above mccore tol: the
   last A step when dB > dA, else the last B step (cmova, asm 1801bee1d); one above: that one; none: step i - 1 when its x or y differs
   from step i's by more than tol, else -1 (also when i - 1 < 0) */
int cs_zw_prev(const CsZbc *Z, double tol, int i)
{
    int la = Z->last[0], lb = Z->last[1], r = -1;
    const CsZStep *S = Z->st;
    double dA = 0.0, dB = 0.0;
    if (la > -1) { double dy = S[la].p[1] - S[i].p[1], dx = S[la].p[0] - S[i].p[0]; dA = sqrt(dy * dy + dx * dx); }
    if (lb > -1) { double dy = S[lb].p[1] - S[i].p[1], dx = S[lb].p[0] - S[i].p[0]; dB = sqrt(dy * dy + dx * dx); }
    if (dA > tol) {
        if (dB > tol) r = dB > dA ? la : lb;
        else r = la;
    } else if (dB > tol) r = lb;
    else goto back;
    if (r >= 0) return r;
back:
    if (i - 1 < 0) return r;
    if (fabs(S[i].p[0] - S[i - 1].p[0]) > tol) return i - 1;
    if (fabs(S[i].p[1] - S[i - 1].p[1]) > tol) return i - 1;
    return r;
}

/* FUN_1801ab6d0(L, R, end, mode) (asm 1801ab6d0..1801ab72d): the code of R's end (+0x30 for end 0, else +0x32): > 0 -> 0, 0 -> 2,
   < 0 -> 3 when mode 2 or (mode 1 and the stall list L has it on, FUN_1801bd8a0), else 1 */
int cs_zw_endcode(const CsLoop *L, const short *SL, int rd, int r, int end, int mode)
{
    const CsSeg *R = &L->D[rd]->S[r];
    short s = (short)end == 0 ? R->s30 : R->s32;
    if (s > 0) return 0;
    if (s == 0) return 2;
    int c = (short)mode == 2;
    if (!c && (short)mode == 1) c = cs_zb_stall_on(SL, s);
    return c ? 3 : 1;
}

/* the 3-vector slot of a kind (FUN_1800062d0, asm 1800062d0: 2 -> +0x10, 1 -> +8, else +0) */
static int k3(int k) { return (short)k == 2 ? 2 : (short)k == 1 ? 1 : 0; }

/* FUN_1801aef30(hdrs, R, box, out) (asm 1801aef30..1801af0e7): R's two ends as points (its line's kind k = header +0x14: the line position
   to x (k != 1) or y, the end's s (+0x70 / +0x80) to the other (y when k == 0, else x); z = +0x78 / +0x88), each tested against the box
   (FUN_1802537e0 outcode 0 = inside): an inside end goes to out[0..2] (end 0) / out[3..5] (end 1) with its x / y at the slots of k / k == 0.
   Returns 0 only end 0 inside, 1 only end 1, 2 both, -1 none */
int cs_zw_ends(const CsLoop *L, int hd, int rd, int r, const double box[4], double out[6])
{
    const CsSeg *R = &L->D[rd]->S[r];
    if (R->line < 0 || R->line >= L->D[hd]->nl) return ZND;
    const CsSegLine *H = &L->D[hd]->H[R->line];
    int k = H->dir, si = k == 0, ret = -1;
    double P[2];
    P[k == 1 ? 1 : 0] = H->pos;
    P[si == 1 ? 1 : 0] = R->s0;
    if (!cs_zc_outcode(box, P)) {
        out[k3(k)] = P[k == 1 ? 1 : 0];
        out[k3(si)] = P[si == 1 ? 1 : 0];
        out[2] = R->z0;
        ret = 0;
    }
    P[si == 1 ? 1 : 0] = R->s1;
    if (cs_zc_outcode(box, P)) return ret;
    out[3 + k3(k)] = P[k == 1 ? 1 : 0];
    out[3 + k3(si)] = P[si == 1 ? 1 : 0];
    out[5] = R->z1;
    return ret + 2;
}

/* mccore UnitNormal (1804b7480; as cam3d_cscallop_zcore.c): a x b over its length, the error code */
static int unit_n(double tl, const double *a, const double *b, double *o)
{
    double ox = b[2] * a[1] - b[1] * a[2], oy = b[0] * a[2] - a[0] * b[2], oz = a[0] * b[1] - b[0] * a[1];
    double l = sqrt(ox * ox + oy * oy + oz * oz);
    int err = 0;
    if (l < tl) {
        double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        if (tl <= la) { double lb = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]); err = (tl <= lb) + 2; } else err = 1;
    }
    double d = l < 1e-12 ? 1e-12 : l;
    o[0] = ox / d; o[1] = oy / d; o[2] = oz / d;
    return err;
}
/* FUN_1801c5ef0(side, a, b) (asm 1801c5ef0..1801c5f90): the angle a -> b (FUN_18025ce50 = mutl_v_angle_3d) turned to 0..2 pi by the
   side: UnitNormal(a, b) fine -> its z > 0 gives +1 else -1; equal to side: the angle, else 2 pi - angle (6.283185307179586 at
   0x1803eb848); error 3 (parallel) with angle >= pi / 2 (0x1803eb828): the angle; any other error: 2 pi */
double cs_zw_turn(double tol, int side, const double a[3], const double b[3])
{
    double ang = cs_v_angle_3d(a, b), o[3];
    int e = unit_n(tol, a, b, o);
    if (!e) {
        short s = o[2] > 0.0 ? 1 : -1;
        return s == (short)side ? ang : 6.283185307179586 - ang;
    }
    if (e == 3 && !(1.570796326794896 > ang)) return ang;
    return 6.283185307179586;
}

/* (x, y, 0) over its length (0 when 0), as FUN_1801bf450 makes its unit vectors (asm 1801bf4fb..1801bf55b) */
static void unit_xy(double x, double y, double o[3])
{
    double l = sqrt(y * y + x * x + 0.0);
    if (l == 0.0) { o[0] = o[1] = o[2] = 0.0; return; }
    o[0] = x / l; o[1] = y / l; o[2] = 0.0 / l;
}
/* FUN_1801bf450(L, mode, side, W, v, box, zr, ctx, dir, hdrs, max, &cnt, arr) (asm 1801bf450..1801bfbcf): the record ends of direction dir
   inside the box nearest the walk point W, into the candidate array (at most max entries of 0xa8 bytes; cnt in / out). The lines whose
   position lies strictly inside the box's span (FUN_1801c9520's lines of its corners, lowest to highest index); per record its ends
   inside the box (FUN_1801aef30) with z strictly inside zr, other than W's own (W +0x90 / +0xa2): d = |end - W point|; below mccore tol
   both angles are 1 degree (0x1803f4070), else a1 = mutl_v_angle_3d(unit v, unit (end - W) in x, y) and a2 = FUN_1801c5ef0(side, ..).
   Kept while fewer than max (the farthest slot tracked), else it replaces the farthest when nearer (the farthest then looked up again
   among the old values, asm 1801bf98a..1801bf9d6). Entry: +0 = 0, +0x20 = 0, +0x30 point, +0x48 end - W, +0x60 FUN_1801ab6d0(L, R,
   end, mode), +0x68 d, +0x70 a1, +0x78 a2, +0x80 / +0x88 pi, +0x90 R, +0x98 hdrs[dir], +0xa0 dir, +0xa2 end. Returns the number added */
int cs_zw_cands(CsLoop *L, const short *SL, int mode, int side, const CsZCand *W, const double v[2], const double box[4],
                const double zr[2], int dir, int max, short *cnt, CsZCand *arr)
{
    short n = *cnt, n0 = n;
    int bl[4];
    cs_d15_boxlines(L, box, bl);
    int a = (short)dir == 0 ? bl[3] : bl[0], b = (short)dir == 0 ? bl[1] : bl[2], lo, hi;
    if (a < b) { lo = a; hi = b; } else { lo = b; hi = a; }
    double u[3]; unit_xy(v[0], v[1], u);
    int d1 = (short)dir == 1;
    double s0 = d1 ? box[1] : box[0], s1 = d1 ? box[3] : box[2];
    double slo = s1 > s0 ? s0 : s1, shi = s0 > s1 ? s0 : s1;
    short di = 0;
    for (int i = 1; i < n; i++) if (arr[i].d68 > arr[di].d68) di = (short)i;
    int hd = (short)dir ? 1 : 0;
    if ((short)dir != 0 && (short)dir != 1) return ZND;
    const CsSegDir *D = L->D[hd];
    const CsFree *F = D->fl;
    for (int line = lo; line <= hi; line++) {
        /* header n (FUN_1801c65e0 lets an index reach n): record 0's memory of dir's array (surf_classic3.md 12.34) - its +0x18 as the
           position, its +0 as the first record */
        int d = hd, r;
        double pos;
        if (line >= 0 && line < D->nl) { pos = D->H[line].pos; r = D->H[line].first; }
        else if (line == D->nl && F && F->n0[hd] > 0) {
            int od = hd, oi = 0;
            cs_zc_res(L, &od, &oi);
            memcpy(&pos, &L->D[od]->S[oi].z18, 8);
            r = cs_zc_next(L, od, oi, 0, &d);
        } else return ZND;
        if (!(pos > slo) || !(shi > pos)) continue;
        for (long c = 0; r != -1; c++) {
            if (c > 1 << 24) return ZND;
            double E[6];
            int rc = cs_zw_ends(L, hd, d, r, box, E);
            if (rc == ZND) return ZND;
            int fl[2] = { (rc & 0xfffd) == 0, (unsigned short)(rc - 1) <= 1 };
            for (int e = 0; e < 2; e++) {
                if (!fl[e]) continue;
                const double *P = E + 3 * e;
                double z = P[2];
                if (!(z > zr[0]) || !(zr[1] > z)) continue;
                if (W->rdir == d && W->rec == r && W->end == e) continue;
                double dy = P[1] - W->p[1], dz = z - W->p[2], dx = P[0] - W->p[0];
                double dd = sqrt(dy * dy + dx * dx + dz * dz), a1, a2;
                if (L->tol > dd) { a1 = 0.017453292519943295; a2 = 0.017453292519943295; }   /* 0x1803f4070 */
                else {
                    double w[3];
                    unit_xy(P[0] - W->p[0], P[1] - W->p[1], w);
                    a1 = cs_v_angle_3d(u, w);
                    a2 = cs_zw_turn(L->tol, side, u, w);
                }
                short slot;
                if (n < (short)max) {
                    slot = n; n++;
                    if (slot == 0 || dd > arr[di].d68) di = slot;
                } else {
                    if (!(arr[di].d68 > dd)) continue;
                    slot = di; di = 0;
                    for (int i = 1; i < (short)max; i++) if (arr[i].d68 > arr[di].d68) di = (short)i;
                }
                if (slot <= -1) continue;
                CsZCand *C = &arr[slot];
                C->q20 = 0; C->p[0] = P[0]; C->p[1] = P[1]; C->p[2] = z; C->d68 = dd; C->b0 = 0;
                C->p80 = C->p88 = 3.141592653589793;
                C->rdir = d; C->rec = r; C->a70 = a1; C->a78 = a2;
                C->dl[0] = P[0] - W->p[0]; C->dl[1] = P[1] - W->p[1]; C->dl[2] = z - W->p[2];
                C->end = (short)e; C->hd = hd; C->dir = (short)dir;
                C->b60 = (unsigned char)cs_zw_endcode(L, SL, d, r, e, mode);
            }
            int nd; r = cs_zc_next(L, d, r, 0, &nd); d = nd;
        }
    }
    *cnt = n;
    return (short)(n - n0);
}

/* FUN_18017c2e0(code) (asm 18017c2e0..18017c30d, jump table 0x18017c310 / index bytes 0x18017c318 read from the DLL): 1 for the codes 4..22
   but 8, 13, 18 */
int cs_zw_code4(unsigned char c)
{
    return c >= 4 && c <= 22 && c != 8 && c != 13 && c != 18;
}

/* FUN_18017a2c0(W, C, ctx, d, lines, cells) (asm 18017a2c0..18017a3fc): W's and C's points in one grid cell (FUN_180179f30(ctx, d, W +0x30,
   C +0x30, lines, cell)); none -> 0. Same direction (+0xa0) and the records on the same line (+0x24): FUN_18016e860(midpoint W + (C - W) x
   0.5 (0x1803eb810), C - W, ctx, lines, cells) -> the cells beside it (0..2); else 1 */
int cs_zw_cell2(CsLoop *L, double d, const CsZCand *W, const CsZCand *C, int ln[2][4], unsigned short cell[2][2])
{
    if (!cs_zc_cellab(L, d, W->p, C->p, ln[0], cell[0])) return 0;
    if (C->dir != W->dir) return 1;
    if (W->rdir < 0 || C->rdir < 0) return ZND;
    if (L->D[C->rdir]->S[C->rec].line != L->D[W->rdir]->S[W->rec].line) return 1;
    double v[3] = { C->p[0] - W->p[0], C->p[1] - W->p[1], C->p[2] - W->p[2] };
    double M[3] = { v[0] * 0.5 + W->p[0], v[1] * 0.5 + W->p[1], v[2] * 0.5 + W->p[2] };
    return cs_zc_beside(L, M, v, ln, cell);
}
/* FUN_180147900(id, lines, max, &n, ids, lns) (asm 180147900..180147975): id not yet among ids[0..n): appended with its lines when n < max,
   else FUN_180268e20(3) (the error, *err) */
static void uniq_7900(int id, const int ln[4], int max, short *n, int *ids, int (*lns)[4], int *err)
{
    for (short i = 0; i < *n; i++) if (ids[i] == id) return;
    if (*n < (short)max) { ids[*n] = id; memcpy(lns[*n], ln, sizeof lns[0]); (*n)++; }
    else *err = 3;
}
/* FUN_18016fb60(zbc, W, max, &cnt, arr, &nc) (asm 18016fb60..18016fee0): the grid cell of each candidate. id0 = (nB - 1) x (nA - 1) + 1
   (FUN_180176200 + 1); per entry of the count on entry (+8 and +0x28 bytes cleared) FUN_18017a2c0(W, entry, ctx, zbc +0x30): 0 -> +4 =
   ++id0, +0xc..+0x1b = -1; else +4 = the cell number (FUN_18017c1a0: (nA - 1) x cell[1] + cell[0]) and its four lines, the cell noted once
   (FUN_180147900, at most 10); 2 (a second cell beside) while the count < max - 1: a copy of the entry appended with the second cell. Then
   per noted cell a copy of W with that cell at count + k (max reached: FUN_180268e20(3)). *cnt = the entries without the W copies, *nc =
   the cells; returns how many entries got a cell */
int cs_zw_cells(CsLoop *L, double d, const CsZCand *W, int max, short *cnt, CsZCand *arr, short *nc, int *err)
{
    const CsEmCtx *X = L->X;
    int id0 = (X->nB - 1) * (X->nA - 1) + 1, ids[10], lns[10][4];
    short n = *cnt, got = 0, nu = 0;
    for (int i = 0; i < 10; i++) { ids[i] = -1; for (int j = 0; j < 4; j++) lns[i][j] = -1; }
    *nc = 0;
    for (short i = 0; i < *cnt; i++) {
        CsZCand *C = &arr[i];
        C->b8 = 0; C->b28 = 0;
        int ln[2][4]; unsigned short cl[2][2];
        int c = cs_zw_cell2(L, d, W, C, ln, cl);
        if (c == ZND) return ZND;
        if (!c) { C->i4 = ++id0; for (int j = 0; j < 4; j++) C->ln[j] = -1; continue; }
        int id = (X->nA - 1) * (int)cl[0][1] + (int)cl[0][0];
        memcpy(C->ln, ln[0], sizeof C->ln); C->i4 = id;
        uniq_7900(id, ln[0], 10, &nu, ids, lns, err);
        got++;
        if (c == 2 && n < (short)max - 1) {
            CsZCand *E = &arr[n];
            *E = *C;
            id = (X->nA - 1) * (int)cl[1][1] + (int)cl[1][0];
            memcpy(E->ln, ln[1], sizeof E->ln); E->i4 = id;
            uniq_7900(id, ln[1], 10, &nu, ids, lns, err);
            n++; got++;
        }
    }
    for (short k = 0; k < nu; k++) {
        short s = (short)(n + k);
        if (s >= (short)max) { *err = 3; continue; }
        arr[s] = *W; arr[s].i4 = ids[k]; memcpy(arr[s].ln, lns[k], sizeof arr[s].ln);
    }
    *cnt = n; *nc = nu;
    return got;
}

/* FUN_18015bf40(ctx, w, n, arr) (asm 18015bf40..18015c2da): the candidate to go on with, -1 none (n > 0x3c: FUN_180268e20(3), -1). t =
   max(ctx +0x2b8 x 0.1 (0x1803f04d8), mccore tol) (t when not above tol). List A: the entries with +0x2c > 20 whose +0x80 lies below ctx
   +0x320 + 1e-6 (0x1803f3ed8) or whose |+0x58| lies below ctx +0x330 (m = the least +0x80 of them, from 2 pi); of those, list B: +0x80 below
   9.999999999999999e-06 (0x180414408) or |+0x58| below t. No A: the entries with +0x2c > 20 and +0x88 below ctx +0x320 + 1e-6 (m = the least
   +0x88); none: -1. B not empty: the first B entry with the largest +0x2c. Else over the list: r = +0x2c / best when best > 0 and +0x2c >
   best, else 1.0; |+0x80 (A) or +0x88 (the other list) - m| / r below w and +0x2c above the best so far -> that entry */
int cs_zw_choose(double tol, double g, double c320, double c330, double w, int n, const CsZCand *arr)
{
    short res = -1, best = 0, la[64], lb[64], na = 0, nb = 0, no = 0;
    if ((short)n > 0x3c) return -1;
    double t = g * 0.1, m = 6.283185307179586, lim = c320 + 0x1.0c6f7a0b5ed8dp-20;
    if (!(t > tol)) t = tol;
    for (short i = 0; i < (short)n; i++) {
        const CsZCand *e = &arr[i];
        int fl = e->s2c > 0x14, al = lim > e->p80 || c330 > fabs(e->dl[2]);
        if (!fl || !al) continue;
        la[na++] = i;
        if (m > e->p80) m = e->p80;
        if (0x1.4f8b588e368f0p-17 > e->p80 || t > fabs(e->dl[2])) lb[nb++] = i;
    }
    if (!na) {
        for (short i = 0; i < (short)n; i++) {
            const CsZCand *e = &arr[i];
            if (e->s2c > 0x14 && lim > e->p88) { la[no++] = i; if (m > e->p88) m = e->p88; }
        }
        if (!no) return -1;
    }
    if (nb > 0) {
        for (short k = 0; k < nb; k++) if (arr[lb[k]].s2c > best) { res = lb[k]; best = arr[lb[k]].s2c; }
        return res;
    }
    short cnt = na > 0 ? na : no;
    for (short k = 0; k < cnt; k++) {
        const CsZCand *e = &arr[la[k]];
        double r = best > 0 && e->s2c > best ? (double)e->s2c / (double)best : 1.0;
        double x = fabs((na > 0 ? e->p80 : e->p88) - m) / r;
        if (w > x && e->s2c > best) { res = la[k]; best = e->s2c; }
    }
    return res;
}

/* FUN_18015a1c0(side, flag, S0, S1, ctx) (asm 18015a1c0..18015a2a1): 0 when side (zbc +0x1c) is 0, a step has no record or an end
   above 1 (signed, asm 18015a1ec / 18015a1f8); s = FUN_1801695c0(ctx, flag, S0, S1, &t): s != 0 -> 1 when s has side's sign, else 2;
   s == 0 -> 3 when t == 1, 4 when t == -1, else 0 */
static int side_a1c0(const CsLoop *L, short side, int flag, const CsZStep *S0, const CsZStep *S1)
{
    if (side == 0 || S0->rec < 0 || S1->rec < 0 || (short)S0->e2a > 1 || (short)S1->e2a > 1) return 0;
    short t;
    int s = cs_zb_95c0(L->tol, L->D, S0, S1, flag, &t);
    if (s == -101) return ZND;
    if (s == 0) return t == 1 ? 3 : t == -1 ? 4 : 0;
    if ((side < 0 && s < 0) || (side > 0 && s > 0)) return 1;
    return 2;
}
/* the 0x30 step FUN_1801bfdc0 makes of an entry (asm 1801bfdcf..1801bfe5b): +0 0, +8 the point (+0x30..+0x40), +0x20 the record (+0x90),
   +0x28 the direction byte (+0xa0), +0x2a the end (+0xa2), +0x2c 0 */
static void cand_step(const CsZCand *C, CsZStep *S)
{
    memset(S, 0, sizeof *S);
    S->p[0] = C->p[0]; S->p[1] = C->p[1]; S->p[2] = C->p[2];
    S->rdir = C->rdir; S->rec = C->rec; S->b28 = (unsigned char)C->dir; S->e2a = (unsigned short)C->end;
}
/* FUN_1801bfdc0(zbc, W, C) (asm 1801bfdc0..1801bff04): r = FUN_18015a1c0(zbc +0x1c, 0, W's step, C's step, ctx): 1 / 2 -> r; 0 / 3 / 4
   -> r = FUN_18015a1c0(.., 1, ..): 1 / 2 -> r, 3 / 4 -> 1; any other (negative, above 4, or 0 the second time): FUN_180268e20(3), 0 */
int cs_zw_side2(const CsLoop *L, short side, const CsZCand *W, const CsZCand *C, int *err)
{
    CsZStep A, B;
    cand_step(W, &A); cand_step(C, &B);
    int r = side_a1c0(L, side, 0, &A, &B);
    if (r == ZND) return ZND;
    if (r == 1 || r == 2) return r;
    if (r == 0 || r == 3 || r == 4) {
        r = side_a1c0(L, side, 1, &A, &B);
        if (r == ZND) return ZND;
        if (r == 1 || r == 2) return r;
        if (r == 3 || r == 4) return 1;
    }
    *err = 3;
    return 0;
}
/* FUN_18015c2e0(zbc, W, n, idx, arr) (asm 18015c2e0..18015c3d0; FUN_1801bca00 asm 1801bd5..: after FUN_18016a7b0): per listed entry
   arr[idx[k]] FUN_1801bfdc0(zbc, W, entry): 1 keeps +0x2c, 0 -> 5, 2 -> 10, else 5 and FUN_180268e20(3); then the entry with +0x2c ==
   300 (0x12c) and the largest +0x70 strictly above 0.0 (comisd / jbe: NaN never) -> its index; none -1 */
int cs_zw_pick(const CsLoop *L, short side, const CsZCand *W, int n, const short *idx, CsZCand *arr, int *err)
{
    int best = -1; double m = 0.0;
    for (int k = 0; k < (short)n; k++) {
        CsZCand *C = &arr[idx[k]];
        int r = cs_zw_side2(L, side, W, C, err);
        if (r == ZND) return ZND;
        if (r != 1) {
            if (r == 0) C->s2c = 5;
            else if (r == 2) C->s2c = 10;
            else { C->s2c = 5; *err = 3; }
        }
        if (C->s2c == 300 && C->a70 > m) { best = (unsigned short)idx[k]; m = C->a70; }
    }
    return (short)best;
}

/* ---- FUN_18016a7b0 (surf_classic3.md 12.44): the candidates that share a grid cell, by the corners of the cell they lie on ---- */
/* the 9 x 9 byte tables of the DLL's .data (no writer in the code: only the lea at asm 18016c17b / 18016d609 / 18016e5f9), row = an
   entry's +0x1d, column = its neighbour's */
static const unsigned char T4670[81] = { 0,0,0,0,0,0,0,0,0, 1,0,4,0,10,0,15,0,19, 2,2,0,1,0,1,0,1,0, 3,0,20,0,5,0,11,0,16, 4,1,0,2,0,1,0,1,0,
    5,0,17,0,21,0,6,0,12, 6,1,0,1,0,2,0,1,0, 7,0,9,0,14,0,22,0,7, 8,1,0,1,0,1,0,2,0 };   /* DAT_1804d4670 (FUN_18016c130) */
static const unsigned char T46d0[81] = { 0,0,0,0,0,0,0,0,0, 1,0,2,0,1,0,1,0,1, 2,4,0,20,0,17,0,9,0, 3,0,1,0,2,0,1,0,1, 4,10,0,5,0,21,0,14,0,
    5,0,1,0,1,0,2,0,1, 6,15,0,11,0,6,0,22,0, 7,0,1,0,1,0,1,0,2, 8,19,0,16,0,12,0,7,0 };   /* DAT_1804d46d0 (FUN_18016d5c0) */
/* FUN_180177910(up, i, m) (asm 180177910): i + 1 (up) or i - 1, m -> 0, below 0 -> m - 1 */
static short wrap_7910(int up, short i, short m)
{
    short j = (short)(up ? i + 1 : i - 1);
    if (j == m) return 0;
    if (j < 0) return (short)(m - 1);
    return j;
}
/* FUN_18016e540(c) (asm 18016e540, the jump table at 0x18016e574 read from the DLL): 4..7 -> 3, 9..12 -> 8, 14..17 -> 13, 19..22 -> 18 */
static unsigned char cls_e540(unsigned char c)
{
    if (c >= 4 && c <= 7) return 3;
    if (c >= 9 && c <= 12) return 8;
    if (c >= 14 && c <= 17) return 13;
    if (c >= 19 && c <= 22) return 18;
    return c;
}
/* FUN_18016c130(i, m, ls) (up 1, DAT_1804d4670, asm 18016c130..18016c194) / FUN_18016d5c0 (up 0, DAT_1804d46d0, asm 18016d5c0..18016d622):
   a = ls[i] +0x1d outside 1..8 -> 0; j = FUN_180177910(up, i, m) outside 0..m-1 -> 0; else table[a x 9 + ls[j] +0x1d] (ND past the table) */
static int nb_code(int up, short i, short m, CsZCand *const *ls)
{
    unsigned char a = ls[i]->b1d;
    if ((unsigned char)(a - 1) > 7) return 0;
    short j = wrap_7910(up, i, m);
    if (0 > j || j >= m) return 0;
    unsigned x = (unsigned)a * 9 + ls[j]->b1d;
    if (x >= 81) return ZND;
    return (up ? T4670 : T46d0)[x];
}
/* FUN_18016e620(up, i, m, ls) (asm 18016e620..18016e6d2): a = the code above; a == 1: b = the same code of the other neighbour's side
   (FUN_180177910(!up, i, m)); b when FUN_18016e540(b) is 3, 8, 13 or 18 (0x42108), else a */
static int code_e620(int up, short i, short m, CsZCand *const *ls)
{
    int a = nb_code(up, i, m, ls);
    if (a != 1) return a;
    int b = nb_code(up, wrap_7910(!up, i, m), m, ls);
    if (b == ZND) return ZND;
    unsigned char c = cls_e540((unsigned char)b);
    if (c <= 0x12 && (0x42108u >> c & 1)) return b;
    return a;
}
/* FUN_18017c7c0(E) (asm 18017c7c0..18017c89f): the corner of E's record's line in E's cell (end above 1 -> 0): its line's kind (+0x98
   headers, +0x14) != 0 -> lines[2] gives corner 0, lines[3] corner 2; kind 0 -> lines[1] corner 1, lines[0] corner 3; +0x1c = the
   corner, +0x1d = 02 01 03 04 05 06 08 07 [corner x 2 + end]; returns 1, none 0 */
static int corner_c7c0(const CsLoop *L, CsZCand *E)
{
    static const unsigned char pr[2][4] = { { 1, 1, 3, 0 }, { 0, 2, 2, 3 } }, t8[8] = { 2, 1, 3, 4, 5, 6, 8, 7 };
    if ((unsigned short)E->end > 1) return 0;
    if (E->rec < 0 || E->rdir < 0 || E->hd < 0 || E->hd > 1) return ZND;
    int line = L->D[E->rdir]->S[E->rec].line;
    if (line < 0 || line >= L->D[E->hd]->nl) return ZND;
    const unsigned char *q = pr[L->D[E->hd]->H[line].dir != 0];
    for (int k = 0; k < 2; k++)
        if (line == E->ln[q[2 * k + 1]]) { E->b1c = q[2 * k]; E->b1d = t8[(short)E->end + q[2 * k] * 2]; return 1; }
    return 0;
}
/* FUN_1801711f0(start, n, ls, &first) (asm 1801711f0..180171296): the next run of at least two entries with the same cell (+4) from
   start: its length, *first its first index; none 0 */
static short run_11f0(short st, short n, CsZCand *const *ls, short *first)
{
    short i = st;
    for (; i < n - 1; i++) if (ls[i]->i4 == ls[i + 1]->i4) break;
    if (!(i < n - 1)) return 0;
    short j = (short)(i + 2);
    while (j < n && ls[j - 1]->i4 == ls[j]->i4) j++;
    *first = i;
    return (short)(j - i);
}
/* FUN_180170a40(m, ls, &idx) (asm 180170a40..180170a7d): the entries with +0 set; idx the last (-1); 1 when exactly one */
static int one_0a40(short m, CsZCand *const *ls, short *idx)
{
    short c = 0; *idx = -1;
    for (short i = 0; i < m; i++) if (ls[i]->b0) { *idx = i; c++; }
    return c == 1;
}
/* FUN_180175dc0(ctx, m, ls) (asm 180175dc0..180175eee): +0x20 (double) = |the entry's x (corner 0 / 2) or y (1 / 3) - the position of the
   cell's line of that corner (corner 0 lines[0] A, 1 lines[2] B, 2 lines[1] A, 3 lines[3] B)| + {0, a, b + a, a + (b + a)}[corner]
   (a = |stepA|, b = |stepB|); other corners unchanged */
static int dist_5dc0(const CsLoop *L, short m, CsZCand *const *ls)
{
    double a = fabs(L->X->stepA), b = fabs(L->X->stepB), off[4];
    off[0] = 0.0; off[1] = a; off[2] = b + a; off[3] = a + off[2];
    static const int li[4] = { 0, 2, 1, 3 };
    for (short i = 0; i < m; i++) {
        CsZCand *E = ls[i];
        int c = E->b1c;
        if (c > 3) continue;
        int hd = c & 1, ln = E->ln[li[c]];
        if (ln < 0 || ln >= L->D[hd]->nl) return ZND;
        double d = fabs(E->p[hd] - L->D[hd]->H[ln].pos) + off[c];
        memcpy(&E->q20, &d, 8);
    }
    return 0;
}
static double d20(const CsZCand *E) { double d; memcpy(&d, &E->q20, 8); return d; }
/* FUN_18017b570 (asm 18017b570..18017b5ef) by +4, FUN_18017b4f0 (asm 18017b4f0..18017b56b) by the +0x20 double: insertion sorts, an entry
   moved down past those strictly greater (comisd / jbe: NaN stops) */
static void sort_id(CsZCand **ls, short n)
{
    for (short i = 1; i < n; i++) {
        CsZCand *c = ls[i]; short j = (short)(i - 1);
        for (; j >= 0 && c->i4 < ls[j]->i4; j--) ls[j + 1] = ls[j];
        ls[j + 1] = c;
    }
}
static void sort_d20(CsZCand **ls, short n)
{
    for (short i = 1; i < n; i++) {
        CsZCand *c = ls[i]; short j = (short)(i - 1); double v = d20(c);
        for (; j >= 0 && d20(ls[j]) > v; j--) ls[j + 1] = ls[j];
        ls[j + 1] = c;
    }
}
/* FUN_18016b4b0(ctx, c, i, m, ls) (asm 18016b4b0..18016b608): the entries on corner c: ls[i]'s x (c 0 / 2) or y strictly inside the s
   range of one of their records -> 1; some but none -> 0; none at all: FUN_18014a950(ctx, ls[i] end, record, headers) made the entry
   on ls[i]'s record -> 1 */
static int corner_b4b0(CsLoop *L, int c, short i, short m, CsZCand *const *ls)
{
    int sl = (c & 0xfd) != 0, any = 0;
    for (short j = 0; j < m; j++) {
        if (ls[j]->b1c != (unsigned char)c) continue;
        any = 1;
        if (ls[j]->rec < 0) return ZND;
        const CsSeg *R = &L->D[ls[j]->rdir]->S[ls[j]->rec];
        double lo = R->s1 > R->s0 ? R->s0 : R->s1, hi = R->s0 > R->s1 ? R->s0 : R->s1, v = ls[i]->p[sl];
        if (v > lo && hi > v) return 1;
    }
    if (any) return 0;
    const CsZCand *E = ls[i];
    if (E->rec < 0 || E->hd != E->rdir) return ZND;
    int r = cs_9a20_ent(L, (unsigned short)E->end, E->rdir, E->rec);
    return r == -100 ? ZND : r;
}
/* FUN_18016b610(ctx, m, ls) (asm 18016b610..18016b753): per corner c = 0..3 the first entry on it (code a, index f) and the last other
   (code b, index s; f / s carried over from earlier corners); a == 2c + 1 and FUN_18016b4b0(ctx, c - 1 (0 -> 3), f) -> +0x60 = 4; b == 2c
   + 2 and FUN_18016b4b0(ctx, c + 1 (3 -> 0), s) -> +0x60 = 4. Returns how many were marked */
static int mark_b610(CsLoop *L, short m, CsZCand *const *ls)
{
    short f = 0, s = 0, cnt = 0;
    for (int c = 0; c < 4; c++) {
        unsigned char a = 0, b = 0, w = (unsigned char)(2 * c + 1);
        for (short j = 0; j < m; j++) {
            if (ls[j]->b1c != (unsigned char)c) continue;
            if (!a) { a = ls[j]->b1d; f = j; } else { b = ls[j]->b1d; s = j; }
        }
        if (a == w) {
            int r = corner_b4b0(L, c ? c - 1 : 3, f, m, ls);
            if (r == ZND) return ZND;
            if (r) { cnt++; ls[f]->b60 = 4; }
        }
        if (b == w + 1) {
            int r = corner_b4b0(L, c == 3 ? 0 : c + 1, s, m, ls);
            if (r == ZND) return ZND;
            if (r) { cnt++; ls[s]->b60 = 4; }
        }
    }
    return cnt;
}
/* FUN_18016ac50(code, m, ls) (asm 18016ac50..18016ac9c): the entries whose +0x60 is not code kept in order; returns their count */
static short keep_ac50(unsigned char code, short m, CsZCand **ls)
{
    short k = 0;
    for (short i = 0; i < m; i++) if (ls[i]->b60 != code) { if (k < i) ls[k] = ls[i]; k++; }
    return k;
}
/* FUN_18016c2c0(i, n, ls, &out) (asm 18016c2c0..18016c3dc): *out = -1; n != 2 -> 0. a = ls[i] +0x29 in class 8 / 13 / 18 (FUN_18016e540,
   0x42100) and the next entry's +0x60 is 2 or 3 and its +0x2a == a, or b = ls[i] +0x2a in those classes and the previous entry's +0x60 is
   2 / 3 and its +0x29 == b: that entry +8 = 1, +0x2c = 300, *out = its +2; returns its +0x2c (300) */
static short pair_c2c0(short i, short n, CsZCand *const *ls, short *out)
{
    *out = -1;
    if (n != 2) return 0;
    CsZCand *E = ls[i], *F = NULL;
    unsigned char a = E->b29, c = cls_e540(a);
    if (c <= 0x12 && (0x42100u >> c & 1)) {
        CsZCand *G = ls[wrap_7910(1, i, 2)];
        if ((unsigned char)(G->b60 - 2) <= 1 && G->b2a == a) F = G;
    }
    if (!F) {
        unsigned char b = E->b2a; c = cls_e540(b);
        if (!(c <= 0x12 && (0x42100u >> c & 1))) return 0;
        CsZCand *G = ls[wrap_7910(0, i, 2)];
        if (!((unsigned char)(G->b60 - 2) <= 1 && G->b29 == b)) return 0;
        F = G;
    }
    F->b8 = 1; F->s2c = 300;
    short r = F->s2c > 0 ? F->s2c : 0;
    *out = F->s2;
    return r;
}
/* FUN_18016a7b0(zbc, W, n, ls, &cnt, out) (asm 18016a7b0..18016ab23; FUN_1801bca00 asm 1801bcfaf when its p2 is set and FUN_18016fb60
   gave 2 or more cells): the n entries (ls, the candidates in order) sorted by cell (+4); per run of two or more in one cell: each
   entry's corner (FUN_18017c7c0, one fails -> return); not exactly one with +0 set: FUN_180268e20(3) and W appended with the run's cell
   and lines (its corner must be found); +0x20 = the distance along the cell's sides (FUN_180175dc0), sorted by it; FUN_18016b610 marks
   (+0x60 = 4) and drops them (none left -> return); then exactly one with +0 set or FUN_180268e20(3) and return; per entry +0x29 / +0x2a
   / +0x28 the neighbours' codes (FUN_18016c130 / FUN_18016d5c0 / FUN_18016e620(1)); the distinct +0x28 of class 8 / 13 / 18 counted;
   a run of 2 with one such: FUN_18016c2c0 == 300 -> out[cnt++] = the entry it marked. Returns the count over all runs */
int cs_zw_group(CsLoop *L, CsZCand *W, int n, CsZCand *const *in, short *cnt, short *out, int *err)
{
    CsZCand *cp[0x3c], *ls[0x3d];
    short tot = 0, nn = (short)n, first = 0;
    *cnt = 0;
    if (nn > 0x3c) return ZND;
    for (short i = 0; i < nn; i++) cp[i] = in[i];
    sort_id(cp, nn);
    short len = run_11f0(0, nn, cp, &first);
    while (len > 0) {
        short st = first, m, idx;
        for (short b = 0; b < len; b++) {
            ls[b] = cp[b + st];
            int r = corner_c7c0(L, ls[b]);
            if (r == ZND) return ZND;
            if (!r) return tot;
        }
        if (one_0a40(len, ls, &idx)) m = len;
        else {
            *err = 3;
            ls[len] = W; m = (short)(len + 1);
            W->i4 = ls[0]->i4; memcpy(W->ln, ls[0]->ln, sizeof W->ln);
            int r = corner_c7c0(L, W);
            if (r == ZND) return ZND;
            if (!r) return tot;
        }
        if (dist_5dc0(L, m, ls) == ZND) return ZND;
        sort_d20(ls, m);
        int mk = mark_b610(L, m, ls);
        if (mk == ZND) return ZND;
        if (mk > 0) { m = keep_ac50(4, m, ls); if (m == 0) return tot; }
        if (!one_0a40(m, ls, &idx)) { *err = 3; return tot; }
        unsigned char fl[0x17]; memset(fl, 0, sizeof fl);
        short k = 0;
        for (short i = 0; i < m; i++) {
            CsZCand *E = ls[i];
            int a = nb_code(1, i, m, ls);
            if (a == ZND) return ZND;
            E->b29 = (unsigned char)a;
            int b = nb_code(0, i, m, ls);
            if (b == ZND) return ZND;
            E->b2a = (unsigned char)b;
            int c = code_e620(1, i, m, ls);
            if (c == ZND) return ZND;
            E->b28 = (unsigned char)c;
            if (c >= 0x17) return ZND;                               /* the 0x17 flags at rbp+0x320 (asm 18016aa45) */
            unsigned char q = cls_e540((unsigned char)c);
            if (q <= 0x12 && (0x42100u >> q & 1) && !fl[c]) k++;
            fl[c] = 1;
        }
        tot = (short)(tot + k);
        short o;
        if (m == 2 && k == 1 && pair_c2c0(idx, 2, ls, &o) == 300) out[(*cnt)++] = o;
        len = run_11f0((short)(st + len), nn, cp, &first);
    }
    return tot;
}

/* ---- FUN_180179090 (surf_classic3.md 12.44): the cells' runs again, after FUN_18016a7b0, for the side the walk keeps ---- */
static const unsigned char T4610[81] = { 0,0,0,0,0,0,0,0,0, 1,0,1,0,2,0,2,0,2, 2,2,0,1,0,1,0,1,0, 3,0,2,0,1,0,2,0,2, 4,1,0,2,0,1,0,1,0,
    5,0,2,0,2,0,1,0,2, 6,1,0,1,0,2,0,1,0, 7,0,2,0,2,0,2,0,1, 8,1,0,1,0,1,0,2,0 };   /* DAT_1804d4610 (FUN_18016e5c0) */
/* FUN_18016e5c0(i, j, ls) (asm 18016e5c0..18016e612): both +0x1d in 1..8 -> DAT_1804d4610[a x 9 + b], 2 read as -1; else 0 */
static short side_e5c0(short i, short j, CsZCand *const *ls)
{
    unsigned char a = ls[i]->b1d, b = ls[j]->b1d;
    if ((unsigned char)(a - 1) > 7 || (unsigned char)(b - 1) > 7) return 0;
    unsigned char v = T4610[a * 9 + b];
    return v == 2 ? -1 : v;
}
/* FUN_18016ab30(zbc, max, up, f2, i, m, ls) (asm 18016ab30..18016ac40): E = the neighbour FUN_180177910(up, i, m); E +0x2c >= max, the
   side of (i, E) (FUN_18016e5c0) = zbc +0x1c and E +0x28 of class 3 / 8 / 13 / 18 (0x42108): E +8 = 2, +0x2c = 120 when f2 and |z_i - z_E|
   > 2 x ctx +0x2f0, else 140; returns E +0x2c when > 0; otherwise 0 */
static short mark_ab30(short side, double c2f0, short max, int up, int f2, short i, short m, CsZCand *const *ls)
{
    short j = wrap_7910(up, i, m);
    CsZCand *E = ls[j];
    if (E->s2c < max) return 0;
    if (side_e5c0(i, j, ls) != side) return 0;
    unsigned char c = cls_e540(E->b28);
    if (!(c <= 0x12 && (0x42108u >> c & 1))) return 0;
    E->b8 = 2;
    if (f2 && fabs(ls[i]->p[2] - E->p[2]) > c2f0 + c2f0) E->s2c = 0x78; else E->s2c = 0x8c;
    return E->s2c > 0 ? E->s2c : 0;
}
/* FUN_18016c1a0(max, i, m, ls) (asm 18016c1a0..18016c2b2): a = ls[i] +0x29 of class 8 / 13 / 18 and the next entry's +0x2c >= max and
   +0x2a == a, or b = ls[i] +0x2a of those classes and the previous entry's +0x2c >= max and +0x29 == b: that entry +8 = 2, +0x2c = 150;
   returns 150, else 0 */
static short mark_c1a0(short max, short i, short m, CsZCand *const *ls)
{
    CsZCand *E = ls[i];
    unsigned char a = E->b29, c = cls_e540(a);
    if (c <= 0x12 && (0x42100u >> c & 1)) {
        CsZCand *F = ls[wrap_7910(1, i, m)];
        if (F->s2c >= max && F->b2a == a) { F->b8 = 2; F->s2c = 0x96; return F->s2c > 0 ? F->s2c : 0; }
    }
    unsigned char b = E->b2a; c = cls_e540(b);
    if (!(c <= 0x12 && (0x42100u >> c & 1))) return 0;
    CsZCand *F = ls[wrap_7910(0, i, m)];
    if (!(F->s2c >= max && F->b29 == b)) return 0;
    F->b8 = 2; F->s2c = 0x96;
    return F->s2c > 0 ? F->s2c : 0;
}
/* FUN_18016e170(ctx, d, m, ls) (asm 18016e170..18016e270): per entry whose FUN_18016c130 code is 1 or 2: with its next neighbour,
   |dz| > d or FUN_18015a830(ctx, P, Q) == 0 (never with ctx +0x309 = 0: it returns 1, asm 18015a834) counted; the second -> 2; else 1 */
static int steep_e170(int c309, double d, short m, CsZCand *const *ls)
{
    short k = 0;
    for (short i = 0; i < m; i++) {
        int a = nb_code(1, i, m, ls);
        if (a == ZND) return ZND;
        if ((unsigned char)(a - 1) > 1) continue;
        short j = wrap_7910(1, i, m);
        int big = fabs(ls[i]->p[2] - ls[j]->p[2]) > d;
        if (c309) return ZND;                                         /* FUN_18015a830's ctx +0x309 arm (FUN_180149630 / FUN_18015a860) */
        if (big) { k++; if (k == 2) return 2; }
    }
    return 1;
}
/* FUN_180179090(zbc, max, W, n, ls) (asm 180179090..180179405; FUN_1801bca00 asm 1801bd320 with max 0x1e, after FUN_18016a7b0 on the same
   list): side (zbc +0x1c) 0 -> 0. The entries sorted by cell (+4); per run of two or more: exactly one with +0 set, or FUN_180268e20(3)
   and W appended with the run's cell and lines (its corner: FUN_18017c7c0, none -> return; its +0x20 by FUN_180175dc0 alone); sorted by
   +0x20; again exactly one with +0 set or FUN_180268e20(3) and return; the distinct +0x28 of class 8 / 13 / 18 (b) and of class 3 (c);
   b + c > 1: f = FUN_18016e170(ctx, 2 x ctx +0x2f0) when b > 1 or b + c > 2 (2: up = side < 0, f2 = 1; else up = side >= 0), then
   FUN_18016ab30, and with c == 0, b > 1, f == 2 FUN_18016c1a0 (the largest result kept). Returns the largest */
int cs_zw_group2(CsLoop *L, short side, double c2f0, int c309, short max, CsZCand *W, int n, CsZCand *const *in, int *err)
{
    CsZCand *cp[0x3c], *ls[0x3d];
    short best = 0, nn = (short)n, first = 0;
    if (side == 0) return 0;
    if (nn > 0x3c) return ZND;
    for (short i = 0; i < nn; i++) cp[i] = in[i];
    sort_id(cp, nn);
    short len = run_11f0(0, nn, cp, &first);
    while (len > 0) {
        short st = first, m, idx;
        for (short b = 0; b < len; b++) ls[b] = cp[b + st];
        if (one_0a40(len, ls, &idx)) m = len;
        else {
            *err = 3;
            ls[len] = W;
            W->i4 = ls[0]->i4; memcpy(W->ln, ls[0]->ln, sizeof W->ln);
            int r = corner_c7c0(L, W);
            if (r == ZND) return ZND;
            if (!r) return best;
            if (dist_5dc0(L, 1, &ls[len]) == ZND) return ZND;
            m = (short)(len + 1);
        }
        sort_d20(ls, m);
        if (!one_0a40(m, ls, &idx)) { *err = 3; return best; }
        unsigned char fl[0x17]; memset(fl, 0, sizeof fl);
        short b8 = 0, c3 = 0;
        for (short i = 0; i < m; i++) {
            unsigned char c = ls[i]->b28;
            if (c >= 0x17) return ZND;                                /* the 0x17 flags at rbp+0x320 (asm 1801792a5) */
            unsigned char q = cls_e540(c);
            if (q <= 0x12 && (0x42100u >> q & 1)) { if (!fl[c]) b8++; }
            else if (q == 3 && !fl[c]) c3++;
            fl[c] = 1;
        }
        if (b8 + c3 > 1) {
            int f = 0, up = side >= 0, f2 = 0;
            if (b8 > 1 || b8 + c3 > 2) {
                f = steep_e170(c309, c2f0 + c2f0, m, ls);
                if (f == ZND) return ZND;
                if (f == 2) { up = side < 0; f2 = 1; }
            }
            short v = mark_ab30(side, c2f0, max, up, f2, idx, m, ls);
            if (v > best) best = v;
            if (c3 == 0 && b8 > 1 && f == 2) { v = mark_c1a0(max, idx, m, ls); if (v > best) best = v; }
        }
        len = run_11f0((short)(st + len), nn, cp, &first);
    }
    return best;
}

/* ---- FUN_180180090 (surf_classic3.md 12.44): the score of one candidate for the walk ---- */
/* the walk point FUN_1801be340 makes of an entry (asm 1801be35c..1801be3df): record +0x90, headers +0x98, end +0xa2, point +0x30..+0x40
   (its four lines / +0x40 / +0x41 are FUN_18016eb80's outputs) */
static void cand_wpt(const CsZCand *C, CsWPt *P)
{
    memset(P, 0, sizeof *P);
    P->rdir = C->rdir; P->rec = C->rec; P->hd = C->hd; P->end = C->end;
    P->P[0] = C->p[0]; P->P[1] = C->p[1]; P->P[2] = C->p[2];
}
/* FUN_1801bd790(rec, n, hdrs) (asm 1801bd790..1801bd890): up = +0x80 > +0x70, key = its s range in that order; a record of line + 1
   overlapping it (FUN_1801c1c40) and none of line - 1 holding it (FUN_1801bec50(1, ..)) -> 1; else one of line - 1 overlapping and none
   of line + 1 holding -> -1; else 0 */
static int side_d790(const CsSegDir *D, int r)
{
    const CsSeg *R = &D->S[r];
    int up = R->s1 > R->s0, line = R->line;
    double key[2] = { up ? R->s0 : R->s1, up ? R->s1 : R->s0 };
    int a = cs_zb_c1c40(D, up, key, line - 1) != -1;
    if (cs_zb_c1c40(D, up, key, line + 1) != -1 && cs_zb_bec50(D, 1, up, key, line - 1) == -1) return 1;
    if (a && cs_zb_bec50(D, 1, up, key, line + 1) == -1) return -1;
    return 0;
}
/* FUN_1801c2780(rec, end, n, hdrs, &o1, &o2) (asm 1801c2780..1801c2949): the record's neighbour on the end's side (end 0: +8, else +0;
   none -> 0); the end's point and the neighbour's facing end (FUN_1801864b0) give the gap along the line (x for a kind 0 line, else y),
   ordered; o1 = a record of line + 1 over the gap (FUN_1801c1c40 with up = +0x80 > +0x70), o2 = of line - 1; returns o1 or o2 */
static int gap_c2780(CsLoop *L, int dir, int r, short end)
{
    const CsSegDir *D = L->D[dir];
    const CsSeg *R = &D->S[r];
    int line = R->line;
    if (line < 0 || line >= D->nl) return ZND;
    int sl = D->H[line].dir == 0, up = R->s1 > R->s0;
    int q = end == 0 ? R->prev : R->next;
    if (q == -1) return 0;
    double P1[3], P2[3];
    cs_9a20_pt(L, (unsigned short)end, dir, r, P1);
    cs_9a20_pt(L, end == 0, dir, q, P2);
    double key[2];
    if (P2[sl] > P1[sl]) { key[0] = P1[sl]; key[1] = P2[sl]; } else { key[0] = P2[sl]; key[1] = P1[sl]; }
    int o1 = cs_zb_c1c40(D, up, key, line + 1) != -1, o2 = cs_zb_c1c40(D, up, key, line - 1) != -1;
    return o1 || o2;
}
/* FUN_18017b8e0(ctx, d, hdrs, rec) (asm 18017b8e0..18017ba3b): A = the record's start (line position at the line kind's slot, +0x70 at
   the other, z +0x78), B = its end (+0x80, z +0x88) on the neighbouring line (line - 1 when line > 0, else line + 1);
   FUN_180179f30(ctx, d, A, B) -> both in one cell */
static int cell_b8e0(CsLoop *L, double d, int dir, int r)
{
    const CsSegDir *D = L->D[dir];
    const CsSeg *R = &D->S[r];
    int line = R->line, nb = line > 0 ? line - 1 : line + 1;
    if (line < 0 || line >= D->nl || nb >= D->nl) return ZND;
    int k = D->H[line].dir, s = k == 0;
    if (k > 1) return ZND;
    double A[3], B[3];
    A[k] = D->H[line].pos; A[s] = R->s0; A[2] = R->z0;
    B[k] = D->H[nb].pos; B[s] = R->s1; B[2] = R->z1;
    int ln[4]; unsigned short cell[2];
    return cs_zc_cellab(L, d, A, B, ln, cell) != 0;
}
/* FUN_180180090(p1, zbc, p3, dir, i, W, rec, end, d, k, arr) (asm 180180090..1801803e9; FUN_1801bca00 asm 1801bd239 (own direction, p3 1)
   / 1801bd29f (the other, p3 0)): E = arr[k]. FUN_1801bfdc0(zbc, W, E): 0 -> 5, 2 -> 10, not 1 -> FUN_180268e20(3), 0. i < 10 and E's
   end starts the zone (FUN_1801c24f0) -> 20. E +0x70 below pi / 2 (0x1803eb828): p1 and FUN_1801be340 (FUN_18016eb80 on W and E, out
   o) -> E +8 = 1, {220, 210, 200}[o]; else 30. Otherwise: p1 and FUN_1801be340 -> E +8 = 1, {250, 240, 230}[o]; no rec -> 40; E's
   record not rec, on rec's line, end != p8: FUN_1801c2780(rec, p8) -> p3 ? 110 : 90; E's record = rec and p3: FUN_1801bd790 != 0 ->
   110, FUN_18017b8e0(zbc +0x30) -> 70; else end == p8 and (lines one apart or |W - E| (x, y) < d) and FUN_1801bfbd0(1, rec +0x80 -
   +0x70 > 0, E's record, rec) -> p3 ? 80 : 70; else 40. The fall-throughs: E +0x70 above pi / 2 -> 40, else 30 */
int cs_zw_score(CsLoop *L, const CsZbc *Z, int p1, int p3, int dir, int i, const CsZCand *W, int r7, short p8, double d, CsZCand *E, int *err)
{
    static const short t1[3] = { 220, 210, 200 }, t2[3] = { 250, 240, 230 };   /* the words at rsp+0x34 / +0x3c (asm 180180114..18018011c) */
    int r = cs_zw_side2(L, Z->side, W, E, err);
    if (r == ZND) return ZND;
    if (r != 1) { if (r == 0) return 5; if (r == 2) return 10; *err = 3; return 0; }
    if (i < 10 && E->rec >= 0 && cs_zc_first(Z, (unsigned short)E->end, E->rdir, E->rec)) return 20;
    int low = 1.570796326794896 > E->a70;
    if (p1) {
        CsWPt A, B; unsigned char o = 0;
        cand_wpt(W, &A); cand_wpt(E, &B);
        if (A.rec < 0 || B.rec < 0) return ZND;
        int q = cs_zc_pair(L, Z->tol, &A, &B, &o);
        if (q == -100 || q == ZND) return ZND;
        if (q) {
            if (o > 2) return ZND;                                   /* a stack word past the tables */
            E->b8 = 1;
            return low ? t1[o] : t2[o];
        }
    }
    if (low) return 30;
    if (r7 < 0) return 40;
    if (E->rec < 0 || E->rdir < 0) return ZND;
    const CsSegDir *D = L->D[dir];
    const CsSeg *R = &L->D[E->rdir]->S[E->rec], *Q = &D->S[r7];
    int same = E->rdir == dir && E->rec == r7;
    if (!same && R->line == Q->line && E->end != p8) {
        int g = gap_c2780(L, dir, r7, p8);
        if (g == ZND) return ZND;
        if (g) return p3 ? 110 : 90;
    } else if (p3 && same) {
        if (side_d790(D, r7)) return 110;
        int c = cell_b8e0(L, Z->tol, dir, r7);
        if (c == ZND) return ZND;
        if (c) return 70;
    } else {
        if (E->end != p8) return 40;
        int dl = R->line - Q->line;
        int near = abs(dl) == 1;
        if (!near) {
            double dy = W->p[1] - E->p[1], dx = W->p[0] - E->p[0];
            near = d > sqrt(dy * dy + dx * dx);
        }
        if (near && cs_zc_ovl(L, 1, Q->s1 - Q->s0 > 0.0, E->rdir, E->rec, dir, r7)) return p3 ? 80 : 70;
        return 40;
    }
    return E->a70 > 1.570796326794896 ? 40 : 30;
}

/* ---- the zonbuf pool and the cell triangulation FUN_18017daa0 (surf_classic3.md 12.46), from the asm. The pool is the builder's
   local_9908 (zbc +0x10): FUN_18015d200(1, 9, 0x32, 1000) at asm 180187a02 = kind 1, 1000 nodes per grow (+4), the used list of 0xa0 edge
   nodes (+8 head / +0x10 tail, sorted by +0x48), the free list (+0x18), at most 9 blocks (+0x28) of 0xfb8 bytes at +0x30 (+0 cell id, +4
   cell, +8 cap 0x32, +0xa points used, +0x10 0x32 points of 0x18, +0x4c0 triangles used, +0x4c8 triangles of 0x38: +8 three point slots,
   +0x20 three nodes), blocks in use +0x2a, the current block +0x2c, the vertex list +0x8db0 (0x28 each: +0 kind, +8 point slot, +0x10 entry,
   +0x18 corner, +0x20 z; cap +0x8da8 = 0x32, count +0x8daa). The nodes are rewritten whole when taken (FUN_18017e2d0), so only their
   number is kept across calls: the free list holds total - used */
void cs_zw_pool_init(CsZPool *P)
{
    memset(P, 0, sizeof *P);
    P->head = P->tail = -1; P->nmax = 9; P->vcap = 0x32;
    for (int k = 0; k < 9; k++) { P->blk[k].id = -1; P->blk[k].cap = 0x32; }
}
void cs_zw_pool_free(CsZPool *P) { free(P->nd); free(P->rk[0]); free(P->rk[1]); memset(P, 0, sizeof *P); }
/* FUN_18017d8d0(pool) (asm 18017d8d0..18017d8e8): FUN_18017d8a0 (asm 18017d8a0..18017d8c4: a used list goes in front of the free list,
   head / tail cleared), then +0x2a / +0x2c = 0 (one dword) */
void cs_zw_pool_reset(CsZPool *P)
{
    if (P->head >= 0) { P->used = 0; P->head = P->tail = -1; }
    P->nblk = 0; P->cur = 0;
}
/* FUN_18017da50(pool) (asm 18017da50): the free list's head; empty: FUN_1801460d0 (asm 1801460d0: a block of +4 = 1000 nodes from
   FUN_1800cde60, linked in address order in front of the free list) and again */
static int node_da50(CsZPool *P)
{
    if (P->used == P->total) {
        CsZNode *n = realloc(P->nd, sizeof *n * (size_t)(P->total + 1000));
        if (!n) return -1;
        P->nd = n; P->total += 1000;
    }
    return P->used++;
}
/* the order of record addresses (FUN_18017e180 compares them, signed): in Mastercam the records of A, then of B (FUN_1801b1ad0's
   FUN_1800cdfa0 mode 2 = pl2StackAlloc, asm 1801b1de5 / 1801b2097 / 1801b228a), then the FUN_1801b86c0 pool (edge_alloc) all come from one
   bump allocator growing upwards (mccore edge_alloc = 1805797c0, pl2StackAlloc 1805903c0 -> 1805797c0: base + used): rank = (A 0 / B 1 /
   pool 2, slot); a record made later sits in the memory it took (a freed record's, CsFree, or the pool's k-th); above every line index */
static int rank_build(const CsLoop *L, CsZPool *P)
{
    const CsFree *F = L->D[0]->fl;
    if (!F || L->D[1]->fl != F) return ZND;
    int n[2] = { L->D[0]->n, L->D[1]->n };
    if (P->rk[0] && P->rkn[0] == n[0] && P->rkn[1] == n[1] && P->rknp == F->np) return 0;
    for (int d = 0; d < 2; d++) {
        long long *t = realloc(P->rk[d], sizeof *t * (size_t)(n[d] + 1));
        if (!t) return ZND;
        P->rk[d] = t; P->rkn[d] = n[d];
        for (int i = 0; i < n[d]; i++) t[i] = i < F->n0[d] ? ((long long)d << 32 | i) : -1;
    }
    P->rknp = F->np;
    for (int k = 0; k < F->np; k++) {
        int d = F->pool[k][0], i = F->pool[k][1];
        if (d >= 0 && d < 2 && i >= 0 && i < n[d]) P->rk[d][i] = 2LL << 32 | k;
    }
    for (int ch = 1; ch; ) {   /* a freed record's memory taken again (rf): the new record has its rank (chains followed) */
        ch = 0;
        for (int d = 0; d < 2; d++)
            for (int i = 0; i < n[d]; i++) {
                const CsSeg *s = &L->D[d]->S[i];
                if (!(s->fr && s->rf) || P->rk[d][i] < 0 || s->rud < 0 || s->rud > 1 || s->rui < 0 || s->rui >= n[s->rud]) continue;
                if (P->rk[s->rud][s->rui] != P->rk[d][i]) { P->rk[s->rud][s->rui] = P->rk[d][i]; ch = 1; }
            }
    }
    return 0;
}
static long long rec_rank(const CsZPool *P, int d, int r)
{
    if (d < 0 || d > 1 || r < 0 || r >= P->rkn[d] || P->rk[d][r] < 0) return -1;
    return (1LL << 40) + P->rk[d][r];
}
/* pt_equal (mccore 180018c90, asm): |dx|, |dy|, |dz| < tol */
static int pt_eq3(const CsLoop *L, const double *a, const double *b)
{
    return L->tol > fabs(a[0] - b[0]) && L->tol > fabs(a[1] - b[1]) && L->tol > fabs(a[2] - b[2]);
}
/* the walk of FUN_180154fb0 / FUN_180154ea0 over a line's records from (R, e): e 0 -> (up: R, else +8) end 1; e 1 -> (up: +0) end 0;
   other -> up: +0 end 0, else +8 end 1 */
static void walk_4fb0(const CsLoop *L, int up, int *d, int *r, short *e)
{
    int nd = *d;
    if (*e == 0) { if (!up) *r = cs_zc_next(L, *d, *r, 1, &nd); *e = 1; }
    else if (*e == 1) { if (up) *r = cs_zc_next(L, *d, *r, 0, &nd); *e = 0; }
    else if (up) { *r = cs_zc_next(L, *d, *r, 0, &nd); *e = 0; }
    else { *r = cs_zc_next(L, *d, *r, 1, &nd); *e = 1; }
    *d = nd;
}
/* FUN_180154ea0(up, e, R, hdrs, P, &pos, &eo) (asm 180154ea0..180154fa3): along the walk the end nearest P (FUN_180239180 = (dy^2 + dx^2)
   + dz^2 below the best so far, from 1e38 at 0x1803ea528); pos = its place (from 1), eo its end; NULL (-1) without records */
static int near_4ea0(CsLoop *L, int up, short e, int d0, int r, const double P[3], short *pos, short *eo, int *rd)
{
    double best = 1e38; short k = 0; int res = -1, d = d0;
    while (r >= 0) {
        if (d != d0 || r >= L->D[d]->n || L->D[d]->S[r].line < 0) return ZND;   /* the walk reads every record with the line's headers */
        double Q[3]; k++;
        cs_9a20_pt(L, e, d, r, Q);
        double dx = P[0] - Q[0], dy = P[1] - Q[1], dz = P[2] - Q[2], s = dy * dy + dx * dx + dz * dz;
        if (best > s) { *pos = k; res = r; *rd = d; best = s; *eo = e; }
        walk_4fb0(L, up, &d, &r, &e);
    }
    return res;
}
/* FUN_180154fb0(up, e, R, hdrs, P, &pos, &eo) (asm 180154fb0..180155135): along the walk the ends equal to P (pt_equal), at most two:
   one -> it (pos its place from 1, eo its end); two -> FUN_180154ea0 from the first; none -> FUN_180154ea0 from (R, e) */
static int find_4fb0(CsLoop *L, int up, short e0, int d0, int r0, const double P[3], short *pos, short *eo, int *rd)
{
    int fd[2] = { 0, 0 }, fr[2] = { 0, 0 }, n = 0, d = d0, r = r0; short fe[2] = { 0, 0 }, fp[2] = { 0, 0 }, k = 0, e = e0;
    while (r >= 0) {
        if (d != d0 || r >= L->D[d]->n || L->D[d]->S[r].line < 0) return ZND;   /* the walk reads every record with the line's headers */
        double Q[3]; k++;
        cs_9a20_pt(L, e, d, r, Q);
        if (pt_eq3(L, P, Q)) { fd[n] = d; fr[n] = r; fe[n] = e; fp[n] = k; if (++n == 2) break; }
        walk_4fb0(L, up, &d, &r, &e);
    }
    if (n == 1) { *pos = fp[0]; *eo = fe[0]; *rd = fd[0]; return fr[0]; }
    if (n == 2) return near_4ea0(L, up, fe[0], fd[0], fr[0], P, pos, eo, rd);
    return near_4ea0(L, up, e0, d0, r0, P, pos, eo, rd);
}
/* FUN_180175f40(ctx, m, ls) (asm 180175f40..1801760aa): +0x20 (a double) = the place of the entry's point among the record ends of its
   corner's line (FUN_180154fb0) + {0, 10000, 20000, 30000}[corner] (0x180414480): corner 0 the B line lines[2] from its last record (+8)
   down, end 1; 1 the A line lines[1] from its first up, end 0; 2 the B line lines[3] from its first up, end 0; 3 the A line lines[0] from
   its last down, end 1. A corner above 3 or a line without the point's record stops (0; the later entries keep their +0x20) */
static int place_5f40(CsLoop *L, short m, CsZCand *const *ls)
{
    static const double off[4] = { 0.0, 10000.0, 20000.0, 30000.0 };
    for (short i = 0; i < m; i++) {
        CsZCand *E = ls[i];
        int c = E->b1c, hd, ln, up; short e0;
        switch (c) {
        case 0: hd = 1; ln = E->ln[2]; up = 0; e0 = 1; break;
        case 1: hd = 0; ln = E->ln[1]; up = 1; e0 = 0; break;
        case 2: hd = 1; ln = E->ln[3]; up = 1; e0 = 0; break;
        case 3: hd = 0; ln = E->ln[0]; up = 0; e0 = 1; break;
        default: return 0;
        }
        if (ln < 0 || ln >= L->D[hd]->nl) return ZND;
        int r0 = up ? L->D[hd]->H[ln].first : L->D[hd]->H[ln].last, rd = hd;
        short pos = 0, eo = 0;
        int r = r0 < 0 ? -1 : find_4fb0(L, up, e0, hd, r0, E->p, &pos, &eo, &rd);
        if (r == ZND) return ZND;
        if (r < 0) return 0;
        double v = (double)pos + off[c];
        memcpy(&E->q20, &v, 8);
    }
    return 1;
}
/* FUN_1801863e0(end, R, hdrs, out) (asm 1801863e0..1801864a0): the record's line position at the slot of the line's kind (header +0x14:
   1 -> y, 0 -> x; 2 would put it in z and leave y: refused), the other slot and z from +0x60 / +0x68 (end 0) or +0x90 / +0x98 */
static int pt_63e0(const CsLoop *L, int end, int hd, int d, int r, double P[3])
{
    const CsSeg *R = &L->D[d]->S[r];
    if (R->line < 0 || R->line >= L->D[hd]->nl) return ZND;
    const CsSegLine *H = &L->D[hd]->H[R->line];
    if (H->dir > 1) return ZND;
    P[H->dir == 1] = H->pos;
    P[H->dir == 0] = end ? R->v90 : R->v60;
    P[2] = end ? R->v98 : R->v68;
    return 0;
}
/* FUN_18017c8a0(ctx, E, max, cnt, arr) (asm 18017c8a0..18017cb54): the cells beside E's record (FUN_18016e860 at E's point, v = the record's
   end 1 - end 0 by FUN_1801863e0); per such cell (id FUN_18017c1a0) an entry of arr[0..cnt) with that id and E's record / direction / end
   takes its lines, else (while cnt < max) a copy of E with that cell is appended (FUN_18017c7c0 on it). Returns how many were appended */
static int beside_c8a0(CsLoop *L, const CsZCand *E0, short max, short cnt, CsZCand *arr)
{
    const CsZCand E = *E0;
    if (E.rec < 0 || E.rdir < 0 || E.hd < 0 || E.hd > 1) return ZND;
    double P0[3], P1[3];
    if (pt_63e0(L, 0, E.hd, E.rdir, E.rec, P0) == ZND || pt_63e0(L, 1, E.hd, E.rdir, E.rec, P1) == ZND) return ZND;
    double v[3] = { P1[0] - P0[0], P1[1] - P0[1], P1[2] - P0[2] };
    int ln[2][4]; unsigned short cl[2][2];
    int m = cs_zc_beside(L, E.p, v, ln, cl);
    if (!m) return 0;
    unsigned char fl[2] = { 0, 0 }; int id[2];
    for (int s = 0; s < m; s++) id[s] = (L->X->nA - 1) * (int)cl[s][1] + (int)cl[s][0];   /* FUN_18017c1a0 (asm 18017c1a0) */
    for (short j = 0; j < cnt; j++)
        for (int s = 0; s < m; s++)
            if (arr[j].i4 == id[s] && arr[j].rdir == E.rdir && arr[j].rec == E.rec && arr[j].dir == E.dir && arr[j].end == E.end) {
                fl[s] = 1; memcpy(arr[j].ln, ln[s], sizeof arr[j].ln);
            }
    short k = cnt;
    for (int s = 0; s < m; s++) {
        if (fl[s] || k >= max) continue;
        arr[k] = E; arr[k].i4 = id[s]; memcpy(arr[k].ln, ln[s], sizeof arr[k].ln);
        if (corner_c7c0(L, &arr[k]) == ZND) return ZND;
        k++;
    }
    return k - cnt;
}
/* FUN_18017cb60(E, pool) (asm 18017cb60..18017cc0a): the block of E's cell (+4) among those in use, else a new one (id, cell
   FUN_1801716b0 of E's lines, points and triangles 0) while fewer than +0x28; else 0xffff */
static short block_cb60(CsZPool *P, const CsZCand *E)
{
    for (short k = 0; k < P->nblk; k++) if (P->blk[k].id == E->i4) return k;
    if (P->nblk >= P->nmax) return -1;
    CsZBlk *B = &P->blk[P->nblk];
    B->id = E->i4;
    B->cell[0] = (unsigned short)(E->ln[0] < E->ln[1] ? E->ln[0] : E->ln[1]);
    B->cell[1] = (unsigned short)(E->ln[2] < E->ln[3] ? E->ln[2] : E->ln[3]);
    B->used = 0; B->ntri = 0;
    return P->nblk++;
}
/* FUN_18017eb50(side, code) (asm 18017eb50..18017eba2): bit code of 0x1a8610 (side 0, code <= 0x14), 0x314c20 (1, <= 0x15), 0x629840 (2,
   <= 0x16), 0x495280 (3, <= 0x16) */
static int side_eb50(unsigned char s, unsigned char c)
{
    static const unsigned m[4] = { 0x1a8610, 0x314c20, 0x629840, 0x495280 }, top[4] = { 0x14, 0x15, 0x16, 0x16 };
    return s < 4 && c <= top[s] && (m[s] >> c & 1);
}
/* FUN_18017cc10(n, codes) (asm 18017cc10..18017cdb8; jump table 0x18017cdc4 read from the DLL): per code its sides (4..7: 0..3; 9 / 19:
   0, 3; 10 / 20: 0, 1; 11 / 21: 1, 2; 12 / 22: 2, 3; 14 / 16: 1, 3; 15 / 17: 0, 2; others none); 1 when a side holds (FUN_18017eb50) the
   previous code (cyclic) or, n > 2, the next; n < 2 -> 0 */
static int touch_cc10(short n, const unsigned char *code)
{
    unsigned char sd[2] = { 0, 0 };
    short nx = 0;
    if (n < 2) return 0;
    for (short i = 0; i < n; i++) {
        unsigned char c = code[i], k;
        switch (c) {
        case 4: case 5: case 6: case 7: sd[0] = (unsigned char)(c - 4); k = 1; break;
        case 9: case 19: sd[0] = 0; sd[1] = 3; k = 2; break;
        case 10: case 20: sd[0] = 0; sd[1] = 1; k = 2; break;
        case 11: case 21: sd[0] = 1; sd[1] = 2; k = 2; break;
        case 12: case 22: sd[0] = 2; sd[1] = 3; k = 2; break;
        case 14: case 16: sd[0] = 1; sd[1] = 3; k = 2; break;
        case 15: case 17: sd[0] = 0; sd[1] = 2; k = 2; break;
        default: k = 0;
        }
        short pv = (short)(i ? i - 1 : n - 1);
        if (n > 2) nx = (short)(i == n - 1 ? 0 : i + 1);
        for (unsigned char b = 0; b < k; b++) {
            if (side_eb50(sd[b], code[pv])) return 1;
            if (n > 2 && side_eb50(sd[b], code[nx])) return 1;
        }
    }
    return 0;
}
/* FUN_1801727c0(code, n, ls) (asm 1801727c0): the first entry whose +0x1d is code, -1 */
static short find_27c0(unsigned char c, short n, CsZCand *const *ls)
{
    for (short i = 0; i < n; i++) if (ls[i]->b1d == c) return i;
    return -1;
}
/* FUN_18017eac0(slot, code, n, ls, V) (asm 18017eac0..18017eb4a): a record-end vertex (kind 1, corner -1, z 999.99 = 0x408f3feb851eb852)
   of the entry with that code; FUN_18017ea30(slot, code, n, ls, corner, z, V) (asm 18017ea30..18017eabb): a cell-corner vertex (kind 0).
   0 when no entry has the code */
static int vert_ac0(CsZPool *P, double *slot, int kind, unsigned char code, short n, CsZCand *const *ls, short corner, double z)
{
    CsZVert *V = &P->V[P->vcnt];
    V->slot = slot; V->t = (unsigned char)kind; V->s18 = kind ? -1 : corner; V->d20 = kind ? 999.99 : z;
    short i = find_27c0(code, n, ls);
    V->ent = i > -1 && i < n ? ls[i] : NULL;
    if (!V->ent) return 0;
    P->vcnt++;
    return 1;
}
/* the four vertex makers of FUN_18017d330 (asm 18017d330..18017d46e, jump table 0x18017d470 read from the DLL), by the run's code (+0x28
   of ls[i]); each first checks the vertex room (+0x8da8 > count + k) and the block's point room (cap > used + k), else FUN_180268e20(2) =
   -4, and takes k points. FUN_18017d4c0 (codes 4..7, k 2; asm 18017d4c0..18017d605): the record ends of codes 1 / 3 / 5 / 7 and the next.
   FUN_18017d090 (9..12, k 3; asm 18017d090..18017d320): codes a = 7 / 1 / 3 / 5 and b = 2 / 4 / 6 / 8 both present (FUN_1801727f0), a's
   end, the corner 0..3 with z = (z(a) + z(b)) x 0.5 (0x1803eb810) looked up with a, b's end. FUN_18017d610 (14..17, k 4; asm
   18017d610..18017d890) and FUN_18017ce10 (19..22, k 5; asm 18017ce10..18017d084): z = (z(ls[i + 1 cyclic]) + z(ls[i])) x 0.5; the first and
   last are record ends of two codes, the middle ones corners looked up with the first code. Other codes: -16 */
static int verts_d330(CsZPool *P, short i, short n, CsZCand *const *ls)
{
    static const unsigned char c4[4] = { 1, 3, 5, 7 };
    static const unsigned char a9[4] = { 7, 1, 3, 5 }, b9[4] = { 2, 4, 6, 8 };
    static const unsigned char e14[4][2] = { { 7, 4 }, { 1, 6 }, { 3, 8 }, { 5, 2 } };
    static const short k14[4][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };
    static const unsigned char e19[4][2] = { { 1, 8 }, { 3, 2 }, { 5, 4 }, { 7, 6 } };
    static const short k19[4][3] = { { 1, 2, 3 }, { 2, 3, 0 }, { 3, 0, 1 }, { 0, 1, 2 } };
    unsigned char c = ls[i]->b28;
    int k = c >= 4 && c <= 7 ? 2 : c >= 9 && c <= 12 ? 3 : c >= 14 && c <= 17 ? 4 : c >= 19 && c <= 22 ? 5 : 0;
    if (!k) return -16;
    CsZBlk *B = &P->blk[P->cur];
    if (P->vcnt + k >= P->vcap) return -4;
    if (B->used + k >= B->cap) return -4;
    short s1 = B->used;
    B->used = (short)(B->used + k);
    if (k == 2) {
        unsigned char a = c4[c - 4];
        for (int q = 0; q < 2; q++) if (!vert_ac0(P, B->pt[s1 + q], 1, (unsigned char)(a + q), n, ls, -1, 0.0)) return -16;
        return 0;
    }
    if (k == 3) {
        unsigned char a = a9[c - 9], b = b9[c - 9];
        short i1 = find_27c0(a, n, ls), i2 = find_27c0(b, n, ls);
        if (i1 < 0 || i2 < 0) return -16;
        double z = (ls[i1]->p[2] + ls[i2]->p[2]) * 0.5;
        if (!vert_ac0(P, B->pt[s1], 1, a, n, ls, -1, 0.0)) return -16;
        if (!vert_ac0(P, B->pt[s1 + 1], 0, a, n, ls, (short)(c - 9), z)) return -16;
        if (!vert_ac0(P, B->pt[s1 + 2], 1, b, n, ls, -1, 0.0)) return -16;
        return 0;
    }
    short j = wrap_7910(1, i, n);
    double z = (ls[j]->p[2] + ls[i]->p[2]) * 0.5;
    if (k == 4) {
        int t = c - 14;
        for (int q = 0; q < 4; q++) {
            int ok = q == 1 || q == 2 ? vert_ac0(P, B->pt[s1 + q], 0, e14[t][0], n, ls, k14[t][q - 1], z)
                                      : vert_ac0(P, B->pt[s1 + q], 1, e14[t][q == 3], n, ls, -1, 0.0);
            if (!ok) return -16;
        }
        return 0;
    }
    int t = c - 19;
    for (int q = 0; q < 5; q++) {
        int ok = q >= 1 && q <= 3 ? vert_ac0(P, B->pt[s1 + q], 0, e19[t][0], n, ls, k19[t][q - 1], z)
                                  : vert_ac0(P, B->pt[s1 + q], 1, e19[t][q != 0], n, ls, -1, 0.0);
        if (!ok) return -16;
    }
    return 0;
}
/* FUN_18017d950(V1, V2) (asm 18017d950..18017da4a): the edge's byte (node +8): two corners -> their corner numbers 2 apart; two record
   ends -> their entries' corners (+0x1c) differ; a corner c and a record end -> the entry's corner is neither of c's sides ({0, 3}, {0,
   1}, {1, 2}, {2, 3}); else 0xfa */
static unsigned char edge_d950(const CsZVert *a, const CsZVert *b)
{
    if (a->t == 0 && b->t == 0) return a->s18 > b->s18 ? (unsigned char)(a->s18 - b->s18) == 2 : (unsigned char)(b->s18 - a->s18) == 2;
    if (a->t == 1 && b->t == 1) return a->ent->b1c != b->ent->b1c;
    const CsZVert *cv, *rv;
    if (a->t == 0 && b->t == 1) { cv = a; rv = b; } else if (a->t == 1 && b->t == 0) { cv = b; rv = a; } else return 0xfa;
    static const unsigned char s0[4] = { 0, 0, 1, 2 }, s1[4] = { 3, 1, 2, 3 };
    if (cv->s18 < 0 || cv->s18 > 3) return 0xfa;
    unsigned char k = rv->ent->b1c;
    return k != s0[cv->s18] && k != s1[cv->s18];
}
/* FUN_18017e180(a, b, key) (asm 18017e180..18017e208): the two identities in order (kind, then x, then y: signed), 1 when a came first */
static int key_e180(const CsZId *a, const CsZId *b, CsZId k[2])
{
    int first = a->t < b->t || (a->t == b->t && (a->x < b->x || (a->x == b->x && a->y <= b->y)));
    if (first) { k[0] = *a; k[1] = *b; } else { k[0] = *b; k[1] = *a; }
    return first;
}
static int key_eq(const CsZId *a, const CsZId *b) { return a->t == b->t && a->x == b->x && a->y == b->y; }
/* FUN_18017e370(flags, ids, pairs, tri, pool) (asm 18017e370..18017e593): the triangle's edges (0, 1), (1, 2), (2, 0): the key
   (FUN_18017e180), the node with that key in the used list (FUN_18017d8f0, asm 18017d8f0: stops at the first +0x48 above the key's);
   found: its second triangle (+0x98) set (already set: -99 -> FUN_180268e20 = -16); else a node (FUN_18017da50; none: -4) written by
   FUN_18017e210 / FUN_18017e2d0 (points and pairs in the key's order, +8 the edge's byte, +0x90 the triangle) and put after the last with
   +0x48 not above its own (FUN_18017e0a0 / FUN_18017e070); tri +0x20 + 8k = the node */
static int edges_e370(CsZPool *P, const unsigned char fl[3], const CsZId id[3], const CsZPr pr[3], int blk, int ti)
{
    CsZTri *T = &P->blk[blk].tri[ti];
    for (int k = 0; k < 3; k++) {
        int a = k, b = k == 2 ? 0 : k + 1;
        CsZId key[2];
        int first = key_e180(&id[a], &id[b], key);
        int f = -1;
        for (int q = P->head; q >= 0; q = P->nd[q].next) {
            if (P->nd[q].key[0].x > key[0].x) break;
            if (key_eq(&P->nd[q].key[0], &key[0]) && key_eq(&P->nd[q].key[1], &key[1])) { f = q; break; }
        }
        if (f < 0) {
            f = node_da50(P);
            if (f < 0) return -4;
            CsZNode *N = &P->nd[f];
            int u = first ? a : b, w = first ? b : a;
            N->next = -1; N->b8 = fl[k];
            memcpy(N->pt[0], T->slot[u], sizeof N->pt[0]); memcpy(N->pt[1], T->slot[w], sizeof N->pt[1]);
            N->key[0] = key[0]; N->key[1] = key[1]; N->pr[0] = pr[u]; N->pr[1] = pr[w];
            N->t90 = blk * 0x40 + ti; N->t98 = -1;
            int pv = -1, cu = P->head;
            while (cu >= 0 && !(N->key[0].x < P->nd[cu].key[0].x)) { pv = cu; cu = P->nd[cu].next; }
            if (pv < 0) { N->next = P->head; P->head = f; } else { N->next = cu; P->nd[pv].next = f; }
            if (cu < 0) P->tail = f;
        } else {
            if (P->nd[f].t98 >= 0) return -16;
            P->nd[f].t98 = blk * 0x40 + ti;
        }
        T->node[k] = f;
    }
    return 0;
}
/* FUN_18017e750(ctx, pool) (asm 18017e750..18017e9e8): fewer than 3 vertices -> -16; the block's triangle room (cap > used + n - 2, else
   -4); the fan (V0, Vs, Vs+1), s = 1 .. n - 2: per vertex k of it (index vk) its kind, corner, z, entry and point slot, and the byte of the
   edge (vk, vk + 1; the last (vk, 0)) by FUN_18017d950 - note: vk + 1, so the first edge's byte is always (V0, V1)'s; then FUN_18017e5a0:
   per vertex a record end (FUN_18017e9f0, asm 18017e9f0: the entry's point into the slot, id (1, record, end), pair (record, +0xa0, +0xa2
   bytes)) or a corner (FUN_18017e0e0, asm 18017e0e0..18017e17a: FUN_18016ce00 of the entry's lines into the slot, z; id (0, the corner's
   A line, B line); pair 0); other kinds -16; then FUN_18017e370. Stops at the first non-zero */
static int fan_e750(CsLoop *L, CsZPool *P)
{
    short n = P->vcnt;
    if (n < 3) return -16;
    CsZBlk *B = &P->blk[P->cur];
    if ((short)(n - 2) + B->ntri >= B->cap) return -4;
    short t0 = B->ntri;
    B->ntri = (short)(B->ntri + n - 2);
    for (short s = 1; s < n - 1; s++) {
        short vi[3] = { 0, s, (short)(s + 1) };
        unsigned char fl[3]; CsZId id[3]; CsZPr pr[3];
        int ti = s - 1 + t0;
        CsZTri *T = &B->tri[ti];
        for (int k = 0; k < 3; k++) {
            const CsZVert *V = &P->V[vi[k]], *V2 = &P->V[k == 2 ? 0 : vi[k] + 1];
            fl[k] = edge_d950(V, V2);
            T->slot[k] = V->slot;
        }
        for (int k = 0; k < 3; k++) {
            const CsZVert *V = &P->V[vi[k]];
            const CsZCand *E = V->ent;
            memset(&pr[k], 0, sizeof pr[k]); pr[k].rdir = -1; pr[k].rec = -1;
            if (V->t == 1) {
                memcpy(V->slot, E->p, sizeof E->p);
                long long x = rec_rank(P, E->rdir, E->rec);
                if (x < 0) return ZND;
                id[k].t = 1; id[k].x = x; id[k].y = E->end;
                pr[k].rdir = E->rdir; pr[k].rec = E->rec; pr[k].dir = (unsigned char)E->dir; pr[k].end = (unsigned char)E->end;
            } else if (V->t == 0) {
                int c = V->s18;
                for (int q = 0; q < 4; q++) if (E->ln[q] < 0 || E->ln[q] >= L->D[q >> 1]->nl) return ZND;
                int a = (c == 1 || c == 2) ? E->ln[1] : E->ln[0], b = (c == 2 || c == 3) ? E->ln[3] : E->ln[2];   /* FUN_18016ce00 */
                V->slot[0] = L->D[0]->H[a].pos; V->slot[1] = L->D[1]->H[b].pos; V->slot[2] = V->d20;
                id[k].t = 0; id[k].x = a; id[k].y = b;
            } else return -16;
        }
        int r = edges_e370(P, fl, id, pr, P->cur, ti);
        if (r) return r;
    }
    return 0;
}
/* FUN_18017ebb0(W, zbc, mode, &o) (asm 18017ebb0..18017eccb): the first used node with one triangle (+0x98 0) between two record ends,
   one of them W's (record, +0xa0, +0xa2), the other's end code (FUN_1801ab6d0(zbc +0xf2, ..)) 2 or 3: *o = the other's index; none: 0xfa */
static int open_ebb0(const CsLoop *L, const CsZPool *P, const short *SL, int mode, const CsZCand *W, unsigned char *o)
{
    *o = 0xfa;
    for (int q = P->head; q >= 0; q = P->nd[q].next) {
        const CsZNode *N = &P->nd[q];
        if (N->t98 >= 0 || N->key[0].t != 1 || N->key[1].t != 1) continue;
        for (int b = 0; b < 2; b++) {
            if (!(N->pr[b].rdir == W->rdir && N->pr[b].rec == W->rec && N->pr[b].dir == (unsigned short)W->dir && N->pr[b].end == (unsigned short)W->end))
                continue;
            int x = b == 0;
            int c = cs_zw_endcode(L, SL, N->pr[x].rdir, N->pr[x].rec, N->pr[x].end, mode);
            if (c == ZND) return ZND;
            if ((unsigned char)(c - 2) <= 1) { *o = (unsigned char)x; return q; }
        }
    }
    return -1;
}
/* FUN_18017daa0(W, n, arr, zbc, mode, &out) (asm 18017daa0..18017e062; FUN_1801bca00 asm 1801bd027 after FUN_18017d8d0): *out = -1. W copied
   and put in its cell (FUN_180175ef0, asm 180175ef0: FUN_1801716d0, +4 = FUN_18017c1a0, FUN_18017c7c0; 0 -> -16); the n entries copied,
   those with +4 >= (nB - 1) x (nA - 1) (FUN_180176200; no cell yet) put in theirs the same way; then the cells beside each copy and W
   (FUN_18017c8a0, at most 0x3c); sorted by cell (FUN_18017b570); per run of two or more in one cell (FUN_1801711f0): the corners
   (FUN_18017c7c0; 0 -> -16), the places (FUN_180175f40), sorted by them (FUN_18017b4f0), the block (FUN_18017cb60 -> +0x2c; none: -16),
   FUN_18016b610 marking any -> return 0; +0x28 = FUN_18016e620(1) per entry, the first entry of each new code of class 8 / 13 / 18 (b) or 3
   (c) listed as (code, index) over W's copy; b == 1 and c == 0, or more than one listed and FUN_18017cc10 0: FUN_18017d330 (the vertices
   in list order, then FUN_18017e750); non-zero -> returned. Then FUN_18017ebb0 on the pool: the entry of arr[0..n) at the found node's
   other end gets +8 = 1 and *out = its index. Returns 0 (or the error) */
int cs_zw_tri(CsLoop *L, CsZPool *P, const short *SL, int mode, const CsZCand *W, int n, CsZCand *arr, short *out)
{
    CsZCand Wc = *W, C[0x3c + 1], *ls[0x3d], *ls2[0x3d];
    short cnt;
    *out = -1;
    if (n > 0x3c) return ZND;
    if (rank_build(L, P) == ZND) return ZND;
    unsigned short cl[2];
    int id0 = (L->X->nB - 1) * (L->X->nA - 1);
    /* FUN_180175ef0 */
    if (!cs_zc_cell(L, Wc.p, Wc.ln, cl)) return -16;
    Wc.i4 = (L->X->nA - 1) * (int)cl[1] + (int)cl[0];
    int r = corner_c7c0(L, &Wc);
    if (r == ZND) return ZND;
    if (!r) return -16;
    for (short i = 0; i < n; i++) {
        C[i] = arr[i];
        if (C[i].i4 < id0) continue;
        if (!cs_zc_cell(L, C[i].p, C[i].ln, cl)) return -16;
        C[i].i4 = (L->X->nA - 1) * (int)cl[1] + (int)cl[0];
        r = corner_c7c0(L, &C[i]);
        if (r == ZND) return ZND;
        if (!r) return -16;
    }
    cnt = (short)n;
    for (short i = 0; i < n; i++) {
        r = beside_c8a0(L, &C[i], 0x3c, cnt, C);
        if (r == ZND) return ZND;
        cnt = (short)(cnt + r);
    }
    r = beside_c8a0(L, &Wc, 0x3c, cnt, C);
    if (r == ZND) return ZND;
    cnt = (short)(cnt + r);
    for (short i = 0; i < cnt; i++) ls[i] = &C[i];
    sort_id(ls, cnt);
    short first = 0, ret = 0;
    short len = run_11f0(0, cnt, ls, &first);
    while (len > 0) {
        for (short b = 0; b < len; b++) {
            ls2[b] = ls[first + b];
            r = corner_c7c0(L, ls2[b]);
            if (r == ZND) return ZND;
            if (!r) return -16;
        }
        if (place_5f40(L, len, ls2) == ZND) return ZND;
        sort_d20(ls2, len);
        P->cur = block_cb60(P, ls2[0]);
        if (P->cur < 0 || P->cur > P->nmax) return -16;
        r = mark_b610(L, len, ls2);
        if (r == ZND) return ZND;
        if (r > 0) return ret;
        unsigned char fl[0x17], code[0x3c]; short idx[0x3c], nb = 0, nc = 0, k = 0;
        memset(fl, 0, sizeof fl);
        for (short i = 0; i < len; i++) {
            int c = code_e620(1, i, len, ls2);
            if (c == ZND) return ZND;
            ls2[i]->b28 = (unsigned char)c;
            if (c >= 0x17) return ZND;                              /* the 0x17 flags at rbp+0x2b80 (asm 18017de10) */
            unsigned char q = cls_e540((unsigned char)c);
            int add = 0;
            if (q <= 0x12 && (0x42100u >> q & 1)) { if (!fl[c]) { nb++; add = 1; } }
            else if (q == 3 && !fl[c]) { nc++; add = 1; }
            if (add) { if (k < 0x3c) { code[k] = (unsigned char)c; idx[k] = i; } k++; }
            fl[c] = 1;
        }
        if ((nb == 1 && nc == 0) || (k > 1 && !touch_cc10(k, code))) {
            P->vcnt = 0;
            for (short q = 0; q < k && q < 0x3c; q++) {
                r = verts_d330(P, idx[q], len, ls2);
                if (r) return (short)r;
            }
            r = fan_e750(L, P);
            if (r == ZND) return ZND;
            ret = (short)r;
            if (r) return ret;
        }
        len = run_11f0((short)(first + len), cnt, ls, &first);
    }
    unsigned char o;
    int q = open_ebb0(L, P, SL, mode, W, &o);
    if (q == ZND) return ZND;
    if (q < 0 || o == 0xfa) return ret;
    const CsZPr *R = &P->nd[q].pr[o];
    for (short i = 0; i < n; i++)
        if (R->rdir == arr[i].rdir && R->rec == arr[i].rec && R->dir == (unsigned short)arr[i].dir && R->end == (unsigned short)arr[i].end) {
            arr[i].b8 = 1; *out = i; return ret;
        }
    return ret;
}

/* ---- FUN_1801bca00, the walk's step (surf_classic3.md 12.47), from the asm (asm 1801bca00..1801bd789; the decompile garbles the stack
   arguments). Arguments: p1 (both directions), p2 (the cell runs), p3 (the triangulation), mode (FUN_1801ab6d0's), i (the zone buffer's
   step), zbc, p7 (z window), p8 (box factor), hdrs, then the outputs p10 (byte), p11 (angle), p12 (+0xa0), p13 (+0xa2), p14 (point);
   returns the chosen entry's record (+0x90), NULL none */
int cs_zw_walk(CsLoop *L, const CsZbc *Z, CsZPool *P, const short *SL, int p1, int p2, int p3, int mode, int i, int p7, double p8,
               double c2f0, int c309, CsZWOut *o, int *err)
{
    memset(o, 0, sizeof *o); o->rdir = o->rec = -1;
    if (c309) return ZND;                                            /* the ctx +0x309 arms (FUN_18015a720 / FUN_180168d20 / FUN_18015bf40) */
    if (i < 1) { *err = -16; return 0; }                             /* FUN_180268e20(-16) (asm 1801bcaaa) */
    const CsZStep *S = &Z->st[i];
    unsigned c = S->b28;
    if (c > 1) { o->e100 = 1; return 0; }                            /* zbc +0x100 = -16 (asm 1801bcb13..1801bcb23) */
    unsigned oth = c == 0;
    const CsEmCtx *X = L->X;
    double sc = fabs(c ? X->stepB : X->stepA), so = fabs(oth ? X->stepB : X->stepA);   /* xmm8 / xmm9 (asm 1801bcb55..1801bcbb4) */
    int rd[2], rr[2]; short re[2] = { 0, 0 };
    rd[c] = S->rdir; rr[c] = S->rec; re[c] = (short)S->e2a;
    int lst = Z->last[oth];
    if (lst > -1) { rd[oth] = Z->st[lst].rdir; rr[oth] = Z->st[lst].rec; re[oth] = (short)Z->st[lst].e2a; } else { rd[oth] = -1; rr[oth] = -1; }
    double W0[3] = { S->p[0], S->p[1], S->p[2] };
    int k = cs_zw_prev(Z, L->tol, i);                                /* FUN_1801bed30 */
    if (k < 0) return 0;
    CsZCand W; memset(&W, 0, sizeof W);
    double v[3] = { Z->st[k].p[0] - W0[0], Z->st[k].p[1] - W0[1], Z->st[k].p[2] - W0[2] };
    W.b0 = 1; W.s2 = -1; W.p80 = W.p88 = 3.141592653589793;           /* 0x180415170 */
    W.rdir = S->rdir; W.rec = S->rec; W.hd = (int)c; W.dir = (short)c; W.end = (short)S->e2a;
    memcpy(W.p, W0, sizeof W0);
    o->p10w = 1; o->p10 = 0;
    /* the box (asm 1801bcd1d..1801bcda2): d = |step| x p8 per direction, one below DAT_1804e9250 takes the other's; z window (p7) the wider
       of the two either side of the point, else -1e38 .. 1e38 (0x1803f4940) */
    double dA = fabs(X->stepA) * p8, dB = fabs(X->stepB) * p8;
    if (L->t9250 > dA) dA = dB;
    if (L->t9250 > dB) dB = dA;
    double m = dA > dB ? dA : dB;
    double box[4] = { W0[0] - dA, W0[1] - dB, W0[0] + dA, W0[1] + dB }, zr[2];
    if (p7) { zr[0] = W0[2] - m; zr[1] = W0[2] + m; } else { zr[0] = -1e38; zr[1] = 1e38; }
    CsZCand arr[0x3d], *ls[0x3d];
    memset(arr, 0, sizeof arr);
    short cnt = 0;
    int r = cs_zw_cands(L, SL, mode, Z->side, &W, v, box, zr, (int)c, 0x32, &cnt, arr);
    if (r == ZND) return ZND;
    if (p1) { r = cs_zw_cands(L, SL, mode, Z->side, &W, v, box, zr, (int)oth, 0x32, &cnt, arr); if (r == ZND) return ZND; }
    if (p3) cs_zw_pool_reset(P);                                     /* FUN_18017d8d0 */
    if (cnt <= 0) return 0;
    double w2 = L->g * 2.0;                                          /* DAT_1804e9238 x 2.0 (0x1803eb830) */
    int id0 = (X->nB - 1) * (X->nA - 1);                             /* FUN_180176200 */
    short nc = 0;
    int got = cs_zw_cells(L, Z->tol, &W, 0x3c, &cnt, arr, &nc, err);   /* FUN_18016fb60 */
    if (got == ZND) return ZND;
    short tot = (short)(cnt + nc);
    if (tot > 0x3c) tot = 0x3c;
    for (short q = 0; q < tot; q++) { arr[q].s2 = q; ls[q] = &arr[q]; }
    short pick = -1;
    if (p2 && (short)got > 1) {
        short c2 = 0, ol[64];
        r = cs_zw_group(L, &W, tot, ls, &c2, ol, err);               /* FUN_18016a7b0 */
        if (r == ZND) return ZND;
        if (c2 > 0) { r = cs_zw_pick(L, Z->side, &W, c2, ol, arr, err); if (r == ZND) return ZND; pick = (short)r; }   /* FUN_18015c2e0 */
    }
    short ch;
    if (pick > -1) ch = pick;
    else {
        short dout = -1; int dfl = 0;
        if (p3) {
            r = cs_zw_tri(L, P, SL, mode, &W, cnt, arr, &dout);       /* FUN_18017daa0 */
            if (r == ZND) return ZND;
            dfl = r == 0 && dout > -1 && dout < cnt;
        }
        /* the scores (asm 1801bd0a0..1801bd2da): +0x60 below 2 -> -1; an entry outside the cells (+4 above FUN_180176200) whose segment
           from the point crosses a strip (FUN_1801aaea0(0, 2 DAT_1804e9238, dir, ..), and the other direction when p1) -> -1; else
           FUN_180180090(p1' = !(+0x28 and FUN_18017c2e0(+0x28)), zbc, 1 / 0, dir, i, W, the step's record / end of that direction,
           |step|, k, arr) for an entry of the step's direction / the other; si = the largest */
        short si = 0, pos = 0;
        for (short q = 0; q < cnt; q++) {
            CsZCand *E = &arr[q]; short s = 0;
            if (E->b60 < 2) s = -1;
            else {
                if (E->i4 > id0) {
                    r = cs_zc_seg(L, 0, w2, (int)c, W0, E->p);
                    if (r == ZND || r == -101) return ZND;
                    if (r) s = -1;
                    else if (p1) { r = cs_zc_seg(L, 0, w2, (int)oth, W0, E->p); if (r == ZND || r == -101) return ZND; if (r) s = -1; }
                }
                if (s == 0) {
                    int q1 = !(E->b28 && cs_zw_code4(E->b28));
                    int dd = (unsigned short)E->dir == c ? (int)c : (unsigned short)E->dir == oth ? (int)oth : -1;
                    if (dd >= 0) {
                        if (rr[dd] >= 0 && rd[dd] != dd) return ZND;
                        r = cs_zw_score(L, Z, q1, dd == (int)c, dd, i, &W, rr[dd], re[dd], dd == (int)c ? sc : so, E, err);
                        if (r == ZND) return ZND;
                        s = (short)r;
                        if (si < s) si = s;
                    }
                }
            }
            E->s2c = s;
            if (s > 0) pos++;
        }
        short bx = -1, c150 = -1;
        if (pos > 0) {
            if (pos > 1) {
                if (p2 && (short)got > 1) {
                    r = cs_zw_group2(L, Z->side, c2f0, c309, 0x1e, &W, tot, ls, err);   /* FUN_180179090 */
                    if (r == ZND) return ZND;
                    if (si < (short)r) si = (short)r;
                }
                /* two neighbours of the best score on one record (asm 1801bd32c..1801bd3f1): the one with the larger +0x78 (|difference| at
                   least 1e-6, 0x1803f3ed8), else with the larger +0x68, loses (-1) */
                for (short q = 0; q < cnt - 1; q++) {
                    CsZCand *A = &arr[q], *B = &arr[q + 1];
                    if (A->s2c != si || B->s2c != si || A->rdir != B->rdir || A->rec != B->rec) continue;
                    if (1e-06 > fabs(A->a78 - B->a78)) { if (A->d68 > B->d68) A->s2c = -1; else B->s2c = -1; }
                    else if (A->a78 > B->a78) A->s2c = -1; else B->s2c = -1;
                }
            }
            /* asm 1801bd3f7..1801bd46b: a 150 is taken at once; else among the best the largest +0x70 */
            short bb = -1, ne = 0;
            for (short q = 0; q < cnt; q++) {
                if (arr[q].s2c == 150) { c150 = q; break; }
                if (arr[q].s2c == si) { ne++; if (bb < 0 || arr[q].a70 > arr[bb].a70) bb = q; }
            }
            if (c150 < 0) {
                if (ne > 0) bx = bb;
                /* asm 1801bd554..1801bd5e6: several and the best without +8: one in another cell with a smaller +0x68 than the current
                   choice and +0x70 less than 0.26179938779914935 (0x180413af8) below the best's */
                if (ne > 1 && !arr[bb].b8)
                    for (short q = 0; q < cnt; q++)
                        if (arr[q].s2c == si && arr[q].i4 != arr[bb].i4 && arr[bx].d68 > arr[q].d68 && 0.26179938779914935 > arr[bb].a70 - arr[q].a70) bx = q;
            }
        }
        ch = c150 >= 0 ? c150 : dfl ? dout : bx;
    }
    if (ch <= -1) return 0;
    const CsZCand *E = &arr[ch];
    o->found = 1; o->rdir = E->rdir; o->rec = E->rec; o->p12 = (unsigned short)E->dir; o->p13 = (unsigned short)E->end;
    memcpy(o->p14, E->p, sizeof o->p14);
    double v2[3] = { E->p[0] - W0[0], E->p[1] - W0[1], E->p[2] - W0[2] };
    o->p11 = cs_v_angle_3d(v, v2);                                   /* mutl_v_angle_3d (FUN_18025ce50) */
    o->p10 = E->b8;
    if (!E->b8 && cs_zw_code4(E->b28)) {                             /* FUN_1801be340(zbc, W, E) (asm 1801bd701) */
        CsWPt A, B; unsigned char ob = 0;
        cand_wpt(&W, &A); cand_wpt(E, &B);
        if (A.rec < 0 || B.rec < 0) return ZND;
        int q = cs_zc_pair(L, Z->tol, &A, &B, &ob);
        if (q == -100 || q == ZND) return ZND;
        if (q) o->p10 = 1;
    }
    return 1;
}

