/* cam3d_cscallop_zcore.c - Constant Scallop (op 43) stage D5 / Z2: the core functions the zone builder's three walks (FUN_1801bca00,
 * FUN_180195fc0, FUN_1801bff10) share (formats/surf_classic3.md 12.36 / 12.39). Ported from the asm of mcmsurf.dll (the decompile drops
 * arguments and garbles the 3-vector indexing here); each checked against the DLL's own function by the unit log of its builder
 * (tools/peloader --lmm D15_ZB D15_ZBUNIT: inputs at the entry, outputs at the return; viewer/cfb_port D15_ZUNIT replays them,
 * tools/tests/zcore_cmp.sh). Already ported elsewhere and reused: FUN_1801864b0 (cs_9a20_pt), FUN_18015f990 (cs_9a20_lines),
 * FUN_1801c65e0, FUN_18015c3e0 / FUN_180236cb0 / mutl_v_angle_3d (cam3d_cscallop_zbuf.c). */
#include <math.h>
#include <string.h>
#include "cam3d_cscallop.h"

/* FUN_1801c24f0(zbc, end, rec) (asm 1801c24f0..1801c2510): the zone has more than one step (+0xac > 1) and starts at this record end
   (+0xe8 / +0xf0) */
int cs_zc_first(const CsZbc *Z, int end, int dir, int rec)
{
    return Z->ac > 1 && Z->frec == rec && Z->fdir == dir && Z->f0 == end;
}

/* FUN_1801716d0(ctx, P, lines, cell) (asm 1801716d0..180171706): FUN_180176c90 = FUN_18015f990(P, ctx, &lines[0..3]) (asm 180176c90),
   then FUN_1801716b0 (asm 1801716b0): cell[0] = (ushort) min(lines[0], lines[1]), cell[1] = min(lines[2], lines[3]) (cmovl). Returns 1
   when P lies in the grid; lines are written as far as FUN_18015f990 got */
int cs_zc_cell(CsLoop *L, const double P[3], int ln[4], unsigned short cell[2])
{
    if (!cs_9a20_lines(L, P, &ln[0], &ln[1], &ln[2], &ln[3])) return 0;
    cell[0] = (unsigned short)(ln[0] < ln[1] ? ln[0] : ln[1]);
    cell[1] = (unsigned short)(ln[2] < ln[3] ? ln[2] : ln[3]);
    return 1;
}

/* mccore UnitNormal (1804b7480), as cam3d_cscallop_9a20.c unit_n (mccore tol) */
static int zc_unit_n(double tl, const double *a, const double *b, double *o)
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

/* FUN_18016e860(P, v, ctx, lines, cells) (asm 18016e860..18016e9f0): N = UnitNormal((0, 0, 1.0), v) (an error: 0); h = |the smaller of
   the steps ctx +0x348 / +0x408| x 0.5 (comisd / ja: stepA unless stepB is not above it); the points P + N h and P - N h, each in the grid
   (FUN_1801716d0) -> its lines / cell into the next slot. Returns the number found (0..2) */
int cs_zc_beside(CsLoop *L, const double P[3], const double v[3], int ln[2][4], unsigned short cell[2][2])
{
    static const double z1[3] = { 0.0, 0.0, 1.0 };   /* 1.0 = 0x1803eb820 */
    double N[3];
    if (zc_unit_n(L->tol, z1, v, N)) return 0;
    double a = L->X->stepA, b = L->X->stepB, h = fabs(b > a ? a : b) * 0.5;   /* 0.5 = 0x1803eb810 */
    int n = 0;
    for (int k = 0; k < 2; k++) {
        double s = k ? -h : h, Q[3] = { N[0] * s + P[0], N[1] * s + P[1], N[2] * s + P[2] };
        if (cs_zc_cell(L, Q, ln[n], cell[n])) n++;
    }
    return n;
}

/* the walk point's record line kind: header +0x14 of the record's line in the point's header array (+8) */
static int wp_kind(CsLoop *L, const CsWPt *W)
{
    return L->D[W->hd]->H[L->D[W->rdir]->S[W->rec].line].dir;
}
/* FUN_18016ca20(d, a, b) (asm 18016ca20..18016ca93): |b - a| in (x, y) (sqrt(dy^2 + dx^2)) below d x 1.5 + d x 1.5 (0x180405548) */
static int near_ca20(double d, const double *a, const double *b)
{
    double dx = b[0] - a[0], dy = b[1] - a[1], e = d * 1.5;
    return e + e > sqrt(dy * dy + dx * dx);
}
/* FUN_18016ce00(mode, lines, ctx, out) (l.?, asm 18016ce00): the corner of the cell: mode 0 (A line lines[0], B line lines[2]),
   1 (lines[1], lines[2]), 2 (lines[1], lines[3]), 3 (lines[0], lines[3]); out = their positions (header +0x18) */
static void corner_ce00(CsLoop *L, int mode, const int ln[4], double out[2])
{
    int a = (mode == 1 || mode == 2) ? ln[1] : ln[0], b = (mode == 2 || mode == 3) ? ln[3] : ln[2];
    out[0] = L->D[0]->H[a].pos; out[1] = L->D[1]->H[b].pos;
}
/* FUN_180179e00(P, k, lines, ctx) (asm 180179e00..180179e85): P strictly inside the cell (corners 0 and 2) widened by k */
static int inside_9e00(CsLoop *L, const double *P, double k, const int ln[4])
{
    double q0[2], q1[2];
    corner_ce00(L, 0, ln, q0); corner_ce00(L, 2, ln, q1);
    return P[0] > q0[0] - k && P[1] > q0[1] - k && q1[0] + k > P[0] && q1[1] + k > P[1];
}
/* FUN_180179f30(ctx, d, A, B, lines, cell) (asm 180179f30..18017a1d9): A and B in one cell. Near (FUN_18016ca20): the midpoint (z of A)
   must lie in the grid; else with e = d x 1.5 and u the unit (x, y) direction A -> B (0 when the length is 0) the points A + u e (z of A)
   and B - u e (z of B) must both lie in the grid, in the same cell. Then A and B inside that cell widened by 2e-12 (0x180413ab8). The
   lines are those of the last point looked up */
static int cell_9f30(CsLoop *L, double d, const double *A, const double *B, int ln[4], unsigned short cell[2])
{
    double dx = B[0] - A[0], dy = B[1] - A[1];
    if (near_ca20(d, B, A)) {
        double M[3] = { A[0] + dx * 0.5, A[1] + dy * 0.5, A[2] };
        if (!cs_zc_cell(L, M, ln, cell)) return 0;
    } else {
        double e = d * 1.5, l = sqrt(dy * dy + dx * dx), ux, uy;
        if (l == 0.0) { ux = 0.0; uy = 0.0; } else { ux = dx / l; uy = dy / l; }
        double P1[3] = { A[0] + ux * e, A[1] + uy * e, A[2] }, P2[3] = { B[0] + ux * -e, B[1] + uy * -e, B[2] };
        unsigned short c1[2];
        if (!cs_zc_cell(L, P1, ln, c1)) return 0;
        if (!cs_zc_cell(L, P2, ln, cell)) return 0;
        if (c1[0] != cell[0] || c1[1] != cell[1]) return 0;
    }
    return inside_9e00(L, A, 2e-12, ln) && inside_9e00(L, B, 2e-12, ln);
}
int cs_zc_cellab(CsLoop *L, double d, const double *A, const double *B, int ln[4], unsigned short cell[2]) { return cell_9f30(L, d, A, B, ln, cell); }
/* FUN_18017b790(W) (asm 18017b790..18017b85f): for an end < 2 (unsigned) the corner of the cell the record's line is: line kind != 0
   (B line) checks lines[2] (corner 0) then lines[3] (2), kind 0 checks lines[1] (1) then lines[0] (3) (the byte pairs 0x3020200 /
   0x30101 on the stack); found: +0x40 = the corner, +0x41 = the table 02 01 03 04 05 06 08 07 (0x4030102 / 0x7080605) at corner x 2 + end */
static int corner_b790(CsLoop *L, CsWPt *W)
{
    static const unsigned char T0[4] = { 0, 2, 2, 3 }, T1[4] = { 1, 1, 3, 0 }, T2[8] = { 2, 1, 3, 4, 5, 6, 8, 7 };
    if ((unsigned short)W->end > 1) return 0;
    const unsigned char *t = wp_kind(L, W) != 0 ? T0 : T1;
    int line = L->D[W->rdir]->S[W->rec].line;
    for (int k = 0; k < 2; k++)
        if (line == W->ln[t[2 * k + 1]]) { W->b40 = t[2 * k]; W->b41 = T2[(short)W->end + t[2 * k] * 2]; return 1; }
    return 0;
}
/* FUN_18017c490(a, b, out) (asm 18017c490..18017c4d4): *out = 100; a in 1..8: the position (0..2) of b in row a of DAT_1804d4728 (3 bytes
   a row: 8 6 4 / 3 5 7 / 2 8 6 / 5 7 1 / 4 2 8 / 7 1 3 / 6 4 2 / 1 3 5, read from the DLL's .data) -> *out, 1; else 0 */
static int pos_c490(unsigned char a, unsigned char b, unsigned char *out)
{
    static const unsigned char T[27] = { 0, 0, 0, 8, 6, 4, 3, 5, 7, 2, 8, 6, 5, 7, 1, 4, 2, 8, 7, 1, 3, 6, 4, 2, 1, 3, 5 };
    *out = 100;
    if ((unsigned char)(a - 1) > 7) return 0;
    for (int i = 0; i < 3; i++) if (T[a * 3 + i] == b) { *out = (unsigned char)i; return 1; }
    return 0;
}
/* FUN_18016eb80(ctx, d, W1, W2, out) (asm 18016eb80..18016ec55): two walk points in one cell. Both ends < 2 and on lines of different
   kinds within 2 x 1.5 d (FUN_18016cdb0 -> FUN_18016ca20): FUN_18017c1c0 (NOT PORTED: not reached by any input; -100); else
   FUN_180179f30(ctx, d, W1 point, W2 point, W1 lines, cell). Then W2's lines = W1's, both corners (FUN_18017b790) and
   FUN_18017c490(W1 +0x41, W2 +0x41, out). Returns 0 / 1 */
int cs_zc_pair(CsLoop *L, double d, CsWPt *W1, CsWPt *W2, unsigned char *out)
{
    unsigned short cell[2];
    if (W1->end < 2 && W2->end < 2 && wp_kind(L, W1) != wp_kind(L, W2) && near_ca20(d, W1->P, W2->P)) return -100;
    if (!cell_9f30(L, d, W1->P, W2->P, W1->ln, cell)) return 0;
    memcpy(W2->ln, W1->ln, sizeof W2->ln);
    if (!corner_b790(L, W1) || !corner_b790(L, W2)) return 0;
    return pos_c490(W1->b41, W2->b41, out);
}

/* ---- FUN_1801aaea0's leaves (its tree: surf_classic3.md 12.39) */
/* FUN_1801b6000(mode, A, B, widen, w, out) (asm 1801b6000..1801b60f7): the box of A and B (x, y; min = the other unless it is above,
   max likewise), widened when widen is set so that each side is at least w: mode 1 adds w - dx to each x side and (w - dy) x 0.5 to each
   y side, other modes (w - dx) x 0.5 and w - dy (0.5 = 0x1803eb810); out = xmin, ymin, xmax, ymax */
void cs_zc_box(int mode, const double *A, const double *B, int widen, double w, double out[4])
{
    double x0 = B[0] > A[0] ? A[0] : B[0], y0 = B[1] > A[1] ? A[1] : B[1], x1 = A[0] > B[0] ? A[0] : B[0], y1 = A[1] > B[1] ? A[1] : B[1];
    double ex = 0.0, ey = 0.0, dx = x1 - x0, dy = y1 - y0;
    if (widen) {
        if ((short)mode == 1) { ex = w > dx ? w - dx : 0.0; ey = w > dy ? (w - dy) * 0.5 : 0.0; }
        else { ex = w > dx ? (w - dx) * 0.5 : 0.0; ey = w > dy ? w - dy : 0.0; }
    }
    out[0] = x0 - ex; out[1] = y0 - ey; out[2] = ex + x1; out[3] = ey + y1;
}
/* FUN_1801aed20(up, hdrs, rec, box) (asm 1801aed20..1801aedf2): 0 = the record's line lies strictly inside the box (its kind 1: box y,
   else x) and its (+0x70, +0x80) range reaches the box's other side (up: box lo <= +0x80 and +0x70 <= box hi, else +0x80 <= box hi and
   box lo <= +0x70); else 1 */
int cs_zc_skip(CsLoop *L, int up, int hd, int dir, int rec, const double box[4])
{
    const CsSeg *R = &L->D[dir]->S[rec];
    const CsSegLine *H = &L->D[hd]->H[R->line];
    int k = H->dir;
    double p = H->pos;
    if (!(p > (k == 1 ? box[1] : box[0]))) return 1;
    if (!((k == 1 ? box[3] : box[2]) > p)) return 1;
    int e = k == 0;
    double lo = e ? box[1] : box[0], hi = e ? box[3] : box[2];
    if (up) { if (lo > R->s1) return 1; if (R->s0 > hi) return 1; }
    else { if (R->s1 > hi) return 1; if (lo > R->s0) return 1; }
    return 0;
}
/* FUN_1802537e0(box, P) (l.?, asm 1802537e0): the outcode of P against the box with 1e-12: 1 left / 2 right, | 4 below / | 8 above */
int cs_zc_outcode(const double box[4], const double P[2])
{
    int c = 0;
    if (box[0] - P[0] <= 1e-12) { if (1e-12 < P[0] - box[2]) c = 2; } else c = 1;
    if (1e-12 < box[1] - P[1]) return c | 4;
    if (1e-12 < P[1] - box[3]) c |= 8;
    return c;
}

/* ---- the rest of Z2: FUN_1801aaea0's tree (surf_classic3.md 12.41), from the asm. Records are (direction, index). A record that
   FUN_1801a9c80 freed keeps its bytes but +0x10 (the free-list link, asm 1801a9d38): its +0 is the next it had (sn); when FUN_1801b8870
   hands its memory to a new record (rf), a pointer to it reaches that record (cs_zc_res). */
#define ZND (-101)
void cs_zc_res(const CsLoop *L, int *dir, int *rec)
{
    for (int k = 0; k < 1 << 20 && *rec >= 0; k++) {
        const CsSeg *s = &L->D[*dir]->S[*rec];
        if (!(s->fr && s->rf)) return;
        int d = s->rud, r = s->rui;
        *dir = d; *rec = r;
    }
}
/* the record now in the memory "F d i" (kind 0: slot i of direction d's array of FUN_1801b1ad0) or "P k" (kind 1: the k-th record of the
   FUN_1801b86c0 pool); 0 when the port has no record there */
int cs_zc_mem(const CsLoop *L, int kind, int a, int b, int *dir, int *rec)
{
    const CsFree *F = L->D[0]->fl;
    if (!F) return 0;
    if (kind == 0) { if (a < 0 || a > 1 || b < 0 || b >= F->n0[a]) return 0; *dir = a; *rec = b; }
    else { if (a < 0 || a >= F->np) return 0; *dir = F->pool[a][0]; *rec = F->pool[a][1]; }
    cs_zc_res(L, dir, rec);
    return 1;
}
/* a record's +0 / +8: its next / prev (a freed one: those it had, sn / sp), followed to the record in that memory now */
int cs_zc_next(const CsLoop *L, int d, int r, int prev, int *nd)
{
    const CsSeg *s = &L->D[d]->S[r];
    int q = s->fr ? (prev ? s->sp : s->sn) : (prev ? s->prev : s->next);
    *nd = d;
    if (q >= 0) cs_zc_res(L, nd, &q);
    return q;
}
static const CsSeg *zs(const CsLoop *L, int d, int r) { return &L->D[d]->S[r]; }

/* FUN_1801bfbd0(a, up, R, Q) (asm 1801bfbd0..1801bfc79): the s ranges of R and Q overlap - a: (+0x70, +0x80), else (+0xa0, +0xa8); up:
   0 when Q's first is above R's last or R's first above Q's last, !up the other way round; else 1 */
int cs_zc_ovl(const CsLoop *L, int a, int up, int rd, int r, int qd, int q)
{
    const CsSeg *R = zs(L, rd, r), *Q = zs(L, qd, q);
    double r0 = a ? R->s0 : R->sa0, r1 = a ? R->s1 : R->sa8, q0 = a ? Q->s0 : Q->sa0, q1 = a ? Q->s1 : Q->sa8;
    if (up) { if (q0 > r1) return 0; return !(r0 > q1); }
    if (r1 > q0) return 0;
    return !(q1 > r0);
}
/* FUN_1801c2a10(up, R, line, n, hdrs) (asm 1801c2a10..1801c2b02): line outside [0, n) -> NULL; else along the line's list (up: from its
   first by +0, else from its last by +8) the first record Q whose range overlaps R's (FUN_1801bfbd0(1, up, R, Q)); a Q with R's start
   above Q's end is passed, a Q whose start is above R's end stops the search (NULL). The headers are hd's (records of hd) */
int cs_zc_adj(const CsLoop *L, int up, int rd, int r, int line, int n, int hd, int *q)
{
    *q = -1;
    if (line <= -1 || line >= n) return 0;
    if (line >= L->D[hd]->nl) return ZND;
    const CsSeg *R = zs(L, rd, r);
    double lo = up ? R->s0 : R->s1, hi = up ? R->s1 : R->s0;
    int d = hd, x = up ? L->D[hd]->H[line].first : L->D[hd]->H[line].last;
    for (long k = 0; x >= 0; k++) {
        if (k > 1 << 24) return ZND;
        const CsSeg *Q = zs(L, d, x);
        double q0 = up ? Q->s0 : Q->s1, q1 = up ? Q->s1 : Q->s0;
        if (!(lo > q1)) {
            if (q0 > hi) return 0;
            if (cs_zc_ovl(L, 1, up, rd, r, d, x)) { if (d != hd) return ZND; *q = x; return 1; }
        }
        int nd; x = cs_zc_next(L, d, x, !up, &nd); d = nd;
    }
    return 0;
}
/* the header of a line in hd's headers (+0x14 kind, +0x18 position); NULL past them (header n = record 0's bytes: not ported here) */
static const CsSegLine *zc_hdr(const CsLoop *L, int hd, int line)
{
    return line >= 0 && line < L->D[hd]->nl ? &L->D[hd]->H[line] : NULL;
}
/* FUN_1801aeb30(w, R, Q, hdrs, box) (asm 1801aeb30..1801aecb9): the box between R's line and Q's (the neighbouring line) over the common
   part of their s ranges, both sides moved in by w; 0 when a side is not longer than 2w. k = the kind (+0x14) of R's line: the positions
   go to box y (k == 1) else x, the s range to box x when k == 0 else y; lower position = R's unless Q's is not above it, upper = R's
   unless not above Q's; s from R's lower end (R's +0x70 when +0x80 is above it, else +0x80): the larger of R's and Q's (Q's when not
   below), to R's other end: the smaller (Q's when R's is not below) */
int cs_zc_ibox(const CsLoop *L, double w, int rd, int r, int q, int hd, double box[4])
{
    const CsSeg *R = zs(L, rd, r), *Q = zs(L, hd, q);
    const CsSegLine *HR = zc_hdr(L, hd, R->line), *HQ = zc_hdr(L, hd, Q->line);
    if (!HR || !HQ) return ZND;
    int k = HR->dir, bl = k == 0;
    double x2, x3, x4, x6;
    if (R->s1 > R->s0) { x3 = Q->s0; x6 = Q->s1; x4 = R->s0; x2 = R->s1; }
    else { x3 = Q->s1; x6 = Q->s0; x4 = R->s1; x2 = R->s0; }
    double x0 = HR->pos, x5 = HQ->pos;
    if (!(x5 > x0)) x0 = x5;
    box[k == 1 ? 1 : 0] = x0;
    if (!(x4 > x3)) x4 = x3;
    box[bl == 1 ? 1 : 0] = x4;
    x0 = HR->pos; x3 = HQ->pos;
    if (!(x0 > x3)) x0 = x3;
    box[k == 1 ? 3 : 2] = x0;
    if (!(x6 > x2)) x2 = x6;
    box[bl == 1 ? 3 : 2] = x2;
    double w2 = w + w;
    for (int c = 0; c < 2; c++) {
        if (!(box[2 + c] - box[c] > w2)) return 0;
        box[c] = w + box[c];
        box[2 + c] = box[2 + c] - w;
    }
    return 1;
}
/* FUN_1801aba60(code, box, seg, I, &u, &t) (asm 1801aba60..1801abc34): the segment seg (x0, y0, x1, y1) against the box side of the outcode
   bit (1 x = box[0], 2 x = box[2], 4 y = box[1], 8 y = box[3]); 0 when the segment's extent across that side is below 1e-12 (0x1803eb800);
   else t = the parameter along seg, I = the point, u = the parameter along the side (from its lower corner); 1 */
int cs_zc_edge(int code, const double box[4], const double seg[4], double I[2], double *u, double *t)
{
    int k = (short)code <= 2 ? 0 : 1, j = 1 - k;
    if (1e-12 > fabs(seg[k] - seg[k + 2])) return 0;
    double E0[2], E1[2];
    if (code == 1) { E0[0] = box[0]; E0[1] = box[1]; E1[0] = box[0]; E1[1] = box[3]; }
    else if (code == 2) { E0[0] = box[2]; E0[1] = box[1]; E1[0] = box[2]; E1[1] = box[3]; }
    else if (code == 4) { E0[0] = box[0]; E0[1] = box[1]; E1[0] = box[2]; E1[1] = box[1]; }
    else if (code == 8) { E0[0] = box[0]; E0[1] = box[3]; E1[0] = box[2]; E1[1] = box[3]; }
    else return ZND;   /* other codes leave the side unset (asm 1801abb2e); never called so */
    double tt = (E0[k] - seg[k]) / (seg[k + 2] - seg[k]);
    *t = tt;
    I[0] = (seg[2] - seg[0]) * tt + seg[0];
    I[1] = (seg[3] - seg[1]) * tt + seg[1];
    *u = (I[j] - E0[j]) / (E1[j] - E0[j]);
    return 1;
}
/* FUN_1801ab730(seg, w2, R, inc, n, hdrs) (asm 1801ab730..1801aba54): Q = the record of R's line + inc whose range overlaps R's
   (FUN_1801c2a10) and the box between them (FUN_1801aeb30(w2)); none: -2. Then seg clipped against the box (FUN_1802537e0 outcodes, at
   most 4 rounds): both ends beyond one side -1; both inside: 1 when an earlier round moved an end, else 0; the first side (1, 2, 4, 8) an
   end lies beyond that FUN_1801aba60 cuts: the cut inside the side (0 < u < 1.0, 0x1803eb820) -> 1, else that end moved to the cut;
   4 rounds: -2 */
int cs_zc_clip(const CsLoop *L, const double seg[4], double w2, int rd, int r, int inc, int n, int hd)
{
    const CsSeg *R = zs(L, rd, r);
    int up = R->s1 > R->s0, q;
    int f = cs_zc_adj(L, up, rd, r, (short)inc + R->line, n, hd, &q);
    if (f == ZND) return ZND;
    if (!f) return -2;
    double box[4];
    int b = cs_zc_ibox(L, w2, rd, r, q, hd, box);
    if (b == ZND) return ZND;
    if (!b) return -2;
    double P[4] = { seg[0], seg[1], seg[2], seg[3] };
    int di = 0;
    for (int it = 0; it < 4; it++) {
        int c0 = cs_zc_outcode(box, P), c1 = cs_zc_outcode(box, P + 2);
        if ((short)(c0 & c1) > 0) return -1;
        if (!(c0 | c1)) return it != 0;
        static const int code[4] = { 1, 2, 4, 8 };
        double I[2], u = 0.0, t;
        int ok = 0;
        for (int e = 0; e < 4 && !ok; e++)
            if ((c0 & code[e]) || (c1 & code[e])) {
                ok = cs_zc_edge(code[e], box, P, I, &u, &t);
                if (ok == ZND) return ZND;
                if (ok) di = (c0 & code[e]) ? 1 : 2;
            }
        if (!ok) continue;
        if (u > 0.0 && 1.0 > u) return 1;
        if (di == 1) { P[0] = I[0]; P[1] = I[1]; }
        else if (di == 2) { P[2] = I[0]; P[3] = I[1]; }
    }
    return -2;
}
/* FUN_1801aaea0(mode, w, dir, A, B, ctx) (asm 1801aaea0..1801ab3fe): does the segment A -> B (x, y) reach across a strip between two
   neighbouring lines of direction dir (0: A, else B; n = ctx +0x338 / +0x3f8, headers +0x388 / +0x448, step +0x348 / +0x408)?
   box = FUN_1801b6000(dir, A, B, 0, 0.0); the lines of its corners (FUN_18015fba0 = jmp FUN_18015f990, else FUN_1801c65e0, which lets an
   index reach n), dir 0 their A lines, else their B lines; from the lowest to the highest index, or back (the highest first unless B - A
   (x for dir 0, y for 1, z for 2) and the step are both > 0 or both < 0), every record of each line's list: FUN_1801ab730(A..B,
   2 x DAT_1804e9238, R, +-1, n, headers) >= 0 -> 1. None: 0. The block per record at asm 1801ab1ca..1801ab38f (FUN_1801aed20, l_ints, two
   counters in the argument homes, xmm6) writes only locals never read again: no effect (mode and w feed only it). Header n (index n) is
   record 0's memory of dir's array (surf_classic3.md 12.34): its +0 = that memory's next. ZND: NOT PORTED */
int cs_zc_seg(CsLoop *L, int mode, double w, int dir, const double A[3], const double B[3])
{
    (void)mode; (void)w;
    const CsEmCtx *X = L->X;
    int n = (short)dir ? X->nB : X->nA, hd = (short)dir ? 1 : 0;
    double step = (short)dir ? X->stepB : X->stepA, box[4];
    cs_zc_box(dir, A, B, 0, 0.0, box);
    int a0, b0, c0, d0, a1, b1, c1, d1;
    double P[3] = { box[0], box[1], A[2] };
    if (!cs_9a20_lines(L, P, &a0, &b0, &c0, &d0)) cs_d15_clamp(X, P, &a0, &b0, &c0, &d0);
    P[0] = box[2]; P[1] = box[3]; P[2] = B[2];
    if (!cs_9a20_lines(L, P, &a1, &b1, &c1, &d1)) cs_d15_clamp(X, P, &a1, &b1, &c1, &d1);
    int v[4];
    if ((short)dir == 0) { v[0] = a0; v[1] = b0; v[2] = a1; v[3] = b1; } else { v[0] = c0; v[1] = d0; v[2] = c1; v[3] = d1; }
    int mn = v[0] < v[1] ? v[0] : v[1], mx = v[0] > v[1] ? v[0] : v[1];
    for (int i = 2; i < 4; i++) { if (v[i] < mn) mn = v[i]; if (v[i] > mx) mx = v[i]; }
    int k = (short)dir == 2 ? 2 : (short)dir == 1 ? 1 : 0;
    double dd = B[k] - A[k];
    int lofirst = dd > 0.0 ? step > 0.0 : 0.0 > step;
    int start = lofirst ? mn : mx, end = lofirst ? mx : mn, inc = end > start ? 1 : -1;
    double seg[4] = { A[0], A[1], B[0], B[1] }, w2 = L->g + L->g;   /* DAT_1804e9238 = ctx +0x2b8 */
    const CsFree *F = L->D[hd]->fl;
    for (int i = start; inc > 0 ? i <= end : i >= end; i += inc) {
        int d = hd, r;
        if (i >= 0 && i < L->D[hd]->nl) r = L->D[hd]->H[i].first;
        else if (i == n && F && F->n0[hd] > 0) {
            int od = hd, oi = 0;
            cs_zc_res(L, &od, &oi);
            r = cs_zc_next(L, od, oi, 0, &d);
        } else return ZND;
        for (long c = 0; r >= 0; c++) {
            if (c > 1 << 24) return ZND;
            int s = cs_zc_clip(L, seg, w2, d, r, inc, n, hd);
            if (s == ZND) return ZND;
            if (s >= 0) return 1;
            int nd; r = cs_zc_next(L, d, r, 0, &nd); d = nd;
        }
    }
    return 0;
}
