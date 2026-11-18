/* cam3d_cscallop_zbuf.c - Constant Scallop (op 43) stage D5 / Z1: the zone builder's zone buffer and its two zone files (formats/surf_classic3.md
 * 12.36, 12.38). The buffer is the builder FUN_180187850's local_9a18 (vftable zonbuf_ctrl, the builder's rbp+0x1e0; its fields set at asm
 * 180187d85..180187e8f): +0x18 the zone number, +0x1a the pass, +0x1c the zone's side, +0x1e "tmpprojz.tmp is open", +0x1f DAT_1804e8ffd,
 * +0x20 the draw mode (param_8), +0x22 the error, +0x28 param_10 (DAT_1804e9240), +0x30 mccore tol, +0x38 DAT_1804e9250, +0x40..+0x68
 * the box, +0x70 / +0x90 the files tmpprojz.tmp / tmpdztmp.tmp with the zone's start position +0x78 / +0x98 and the 12-byte zone header
 * +0x7c / +0x9c (int count, short pass, short side, short (never written: NOT DECODED, written as 0 here), byte 1, byte), +0x88 / +0xa8
 * the entries written, +0xac the steps of the zone, +0xb0 the capacity n (2 x records + 10), +0xb4 the step counter, +0xb8 / +0xbc the
 * range not yet written, +0xc0 the step count, +0xc4 / +0xc8 the last step of each direction, +0xd0 the steps (n x 0x30), +0xd8 their
 * flags (n bytes), +0xe0 a work copy (n x 0x18), +0xe8 / +0xf0 the zone's first record and end.
 * Ported from the decompile (decomp/mcmsurf_all.c l.N) and checked against the asm; checked as a whole against the DLL by the call log of
 * tools/peloader --lmm D15_ZB D15_ZBLOG (every call of these functions in the DLL's own builder replayed here, the buffer compared at each
 * call and the files by structure: tools/tests/zbuf_cmp.sh). Records are the port's CsSegDir records (direction, index). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cam3d_cscallop.h"

/* SysFile on memory: Seek (vtable +0x18, from the start), GetPosition, Write (the bytes at the position, the file grows) */
static int zf_write(CsZFile *f, const void *p, size_t n)
{
    if (f->pos + n > f->cap) {
        size_t c = f->cap ? f->cap : 4096;
        while (c < f->pos + n) c *= 2;
        unsigned char *q = realloc(f->p, c);
        if (!q) return 0;
        f->p = q; f->cap = c;
    }
    if (f->pos > f->n) memset(f->p + f->n, 0, f->pos - f->n);
    memcpy(f->p + f->pos, p, n); f->pos += n;
    if (f->pos > f->n) f->n = f->pos;
    return 1;
}
#define TRACE(Z, nm, ra, a, s) do { if ((Z)->trace) (Z)->trace((Z), (nm), (ra), (a), (s)); } while (0)

/* the step's record as the file holds it: Mastercam writes the record's heap address (+0x20, 8 bytes); the port writes its own reference
   (direction + 1) << 32 | index (0 = none), read back by cs_zf_rec */
static long long zrec(int dir, int r) { return r < 0 ? 0 : ((long long)(dir + 1) << 32) | (unsigned)r; }
void cs_zf_rec(long long v, int *dir, int *r) { if (!v) { *dir = -1; *r = -1; } else { *dir = (int)(v >> 32) - 1; *r = (int)(unsigned)v; } }

static void step_bytes(const CsZStep *s, unsigned char o[0x30])
{
    memset(o, 0, 0x30);                     /* +4 / +0x2e: the caller's bytes in Mastercam (stack, not initialised), 0 here */
    memcpy(o, &s->i0, 4); memcpy(o + 8, s->p, 24);
    long long r = zrec(s->rdir, s->rec); memcpy(o + 0x20, &r, 8);
    o[0x28] = s->b28; o[0x29] = s->b29; memcpy(o + 0x2a, &s->e2a, 2); memcpy(o + 0x2c, &s->s2c, 2);
}
static void hdr_bytes(int cnt, short pass, short side, unsigned char o[12])
{
    memset(o, 0, 12);                        /* +8 short and +0xb byte: never written in Mastercam (NOT DECODED), 0 here */
    memcpy(o, &cnt, 4); memcpy(o + 4, &pass, 2); memcpy(o + 6, &side, 2); o[10] = 1;
}

/* the builder's setup of the buffer (FUN_180187850 asm 180187d85..180187e8f; l.262944-262962): zone 1 (local_9a00 = FUN_180159fa0's
   count + 1, 0 + 1 when the third file is not open, as from FUN_180186ff0), the files at their start (tmpprojz.tmp at DAT_1804e930c = 0,
   tmpdztmp.tmp at 0), +0xb4 = 0, +0x100 = 0 */
int cs_zb_buf_init(CsZbc *Z, int cap, int fopen, unsigned char f9ffd, short draw, double d28, double tol, double t9250, double t9238)
{
    CsZFile f0 = Z->f[0], f1 = Z->f[1];
    memset(Z, 0, sizeof *Z);
    Z->f[0] = f0; Z->f[1] = f1; Z->f[0].pos = Z->f[0].n = 0; Z->f[1].pos = Z->f[1].n = 0;
    Z->zone = 1; Z->fopen = (unsigned char)fopen; Z->zsh = f9ffd; Z->draw = draw; Z->d28 = d28; Z->tol = tol; Z->t9250 = t9250;
    Z->t9238 = t9238; Z->cap = cap;
    Z->st = calloc((size_t)cap, sizeof *Z->st); Z->fl = calloc((size_t)cap, 1); Z->e0 = calloc((size_t)cap, sizeof *Z->e0);
    return Z->st && Z->fl && Z->e0;
}
void cs_zb_buf_free(CsZbc *Z)
{
    free(Z->st); free(Z->fl); free(Z->e0); free(Z->f[0].p); free(Z->f[1].p);
    memset(Z, 0, sizeof *Z);
}

/* FUN_1801aa460(zbc) (l.283614, asm 1801aa460): +0xb8 = 0, +0xbc = -1, +0xc0 = 0, +0xac = 0, +0xc8 = +0xc4 = -1, +0xe8 = 0, +0xf0 = -1 */
void cs_zb_reset(CsZbc *Z, unsigned long ra)
{
    TRACE(Z, "aa460", ra, 0, NULL);
    Z->b8 = 0; Z->bc = -1; Z->c0 = 0; Z->ac = 0; Z->last[1] = -1; Z->last[0] = -1; Z->frec = -1; Z->fdir = -1; Z->f0 = -1;
}
/* FUN_1801ab410(zone, pass, side, zbc) (l.284295, asm 1801ab410): the headers' counts 0 and pass -1 (+0x7c / +0x82 / +0x88, +0x9c / +0xa2 /
   +0xa8 = 0, +0x80 / +0xa0 = -1), FUN_1801aa460; the same zone number as +0x18: both files back to the zone's start (+0x78 / +0x98 when
   >= 0), else the start = the files' positions now; then +0x1a = pass, +0x1c = side, +0x18 = zone */
void cs_zb_start(CsZbc *Z, short zone, short pass, short side, unsigned long ra)
{
    if (Z->trace) { CsZStep a = { .i0 = zone, .rdir = pass, .rec = side }; Z->trace(Z, "ab410", ra, 0, &a); }   /* (zone, pass, side) */
    Z->h[0].cnt = 0; Z->h[0].side = 0; Z->n88[0] = 0; Z->h[1].cnt = 0; Z->h[1].side = 0; Z->n88[1] = 0;
    Z->h[0].pass = -1; Z->h[1].pass = -1;
    cs_zb_reset(Z, 0x1801ab477ul);   /* asm 1801ab472 */
    if (zone == Z->zone) {
        if (Z->spos[0] > -1) Z->f[0].pos = (size_t)Z->spos[0];
        if (Z->spos[1] > -1) Z->f[1].pos = (size_t)Z->spos[1];
    } else {
        Z->spos[0] = (int)Z->f[0].pos; Z->spos[1] = (int)Z->f[1].pos;
    }
    Z->pass = pass; Z->side = side; Z->zone = zone;
}

/* FUN_1801c6ae0(a, b, pts, tol) (l.?, asm 1801c6ae0..1801c6d98): the points a+1..b-1 of the 0x18 array within tol of the chord a-b: u =
   (pb - pa) / |pb - pa| (0 when the length is 0), per point w = p - pa: max(|w x u|, (|w| - |pb - pa|) x 2.0 when |w| is longer, else 0)
   above tol -> 0; b - a < 2 -> 1 */
static int zb_c6ae0(int a, int b, const double (*p)[3], double tol)
{
    if (b - a < 2) return 1;
    double dx = p[b][0] - p[a][0], dy = p[b][1] - p[a][1], dz = p[b][2] - p[a][2];
    double L = sqrt(dy * dy + dx * dx + dz * dz), ux, uy, uz;
    if (L == 0.0) { uz = 0.0; uy = 0.0; ux = 0.0; } else { ux = dx / L; uy = dy / L; uz = dz / L; }
    for (int i = a + 1; i < b; i++) {
        double wx = p[i][0] - p[a][0], wy = p[i][1] - p[a][1], wz = p[i][2] - p[a][2];
        double l = sqrt(wy * wy + wx * wx + wz * wz), e = l > L ? (l - L) * 2.0 : 0.0;   /* 2.0 = 0x1803eb830 */
        double cx = wy * uz - wz * uy, cy = wz * ux - uz * wx, cz = uy * wx - wy * ux;
        double c = sqrt(cy * cy + cx * cx + cz * cz);
        if (!(c > e)) c = e;
        if (c > tol) return 0;
    }
    return 1;
}
/* FUN_1801c71b0(n, pts, tol, flags) (asm 1801c71b0..1801c7291): flags[0..n) = 0; n >= 3 and not 0 > tol: in windows of up to 100 points
   (the next window starts at the last point of the one before) from a base point a: each point b - 1 is flagged (1) while a..b stays
   within tol (FUN_1801c6ae0), else the base moves to b - 1 */
static void zb_c71b0(int n, const double (*p)[3], double tol, unsigned char *fl)
{
    if (n > 0) memset(fl, 0, (size_t)n);
    if (n < 3 || 0.0 > tol) return;
    int a = 0, e = n < 100 ? n : 100;
    for (;;) {
        for (int b = a + 2; b < e; b++) {
            if (zb_c6ae0(a, b, p, tol)) fl[b - 1] = 1;
            else a = b - 1;
        }
        if (e == n) break;
        a = e - 1; e = a + 100;
        if (e > n) e = n;
    }
}

/* mccore UnitNormal (1804b7480), as cam3d_cscallop_9a20.c unit_n (mccore tol) */
static int zb_unit_n(double tl, const double *a, const double *b, double *o)
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
/* FUN_180236cb0(c, v, o) (asm 180236cb0): v normalised (FUN_18002fbd0, 0 stays 0) when every |component| <= c, else copied */
static void zb_6cb0(double c, const double *v, double *o)
{
    if (!(fabs(v[0]) <= c && fabs(v[1]) <= c && fabs(v[2]) <= c)) { o[0] = v[0]; o[1] = v[1]; o[2] = v[2]; return; }
    double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l == 0.0) { o[0] = o[1] = o[2] = 0.0; return; }
    o[0] = v[0] / l; o[1] = v[1] / l; o[2] = v[2] / l;
}

/* FUN_18015c3e0(rec, end, dir, ctx, d, out) (l.232? asm 18015c3e0..18015c58e): -1 without a record or when |+0x80 - +0x70| < d (not
   ordered: d above it). From the end's side (end 0: from +0x5a + 1 upwards, against +0x70; end 1: from +0x5c - 1 downwards, against
   +0x80; other ends -1) the first of the record's (+0x5c - +0x5a - 1) inner points (FUN_1801ac3c0, none: -1) whose s is farther than d
   -> out = that point, returns its index; none: end 0 out = (+0x80, +0x88), returns +0x5c - 1; end 1 out = (+0x70, +0x78), returns +0x5a */
static int zb_c3e0(const CsSegDir *D, int r, int end, double d, double out[2])
{
    if (r < 0) return -1;
    const CsSeg *R = &D->S[r];
    double ref = R->s1;
    if (d > fabs(R->s1 - R->s0)) return -1;
    short k, st;
    if (end == 0) { st = 1; ref = R->s0; k = (short)(R->s5a + 1); }
    else if (end == 1) { st = -1; k = (short)(R->s5c - 1); }
    else return -1;
    if (R->pt < 0) return -1;                                    /* FUN_1801ac3c0 NULL (FUN_1801a7fd0's windowed arm, N13-28) */
    const double (*p)[2] = (const double (*)[2])(D->P.pt + R->pt);
    short n = (short)((unsigned short)R->s5c - (unsigned short)R->s5a - 1);
    for (short c = 0; c < n; c++, k = (short)(k + st))
        if (fabs(ref - p[k][0]) > d) { out[0] = p[k][0]; out[1] = p[k][1]; return (unsigned short)k; }
    if (end == 0) { out[0] = R->s1; out[1] = R->z1; return (unsigned short)(R->s5c - 1); }
    out[0] = R->s0; out[1] = R->z0; return (unsigned short)R->s5a;
}

/* FUN_180166550(A, B, ctx, &sa, &sb, &ta, &tb) (l.?, asm 180166550..18016692f; the decompile garbles the 3-vector indexing): d = B - A
   (z dropped); per step (A then B) with a record and end <= 1: FUN_18015c3e0(rec, end, dir = step +0x28, ctx, tol x 4.0, out) (>= 0):
   P[dir] = the record's line position (header +0x18 of direction dir), P[1 - dir] = out[0], P[2] = out[1]; w = P - the step (z dropped);
   UnitNormal(FUN_180236cb0(0.05, d), FUN_180236cb0(0.05, w)): 0 -> s = +1 when its z > 0 else -1; 3 -> t = +1 when the angle
   (mutl_v_angle_3d) is below 1e-6, -1 when within 1e-6 of pi, else 0. Returns 1 when sa or sb is not 0 */
static int zb_6550(const CsZbc *Z, CsSegDir *const *DD, const CsZStep *A, const CsZStep *B, short *sa, short *sb, short *ta, short *tb)
{
    double d[3] = { B->p[0] - A->p[0], B->p[1] - A->p[1], B->p[2] - A->p[2] };
    for (int k = 0; k < 2; k++) {
        const CsZStep *S = k ? B : A;
        short s = 0, t = 0;
        if (S->rec >= 0 && S->e2a <= 1) {
            int dir = S->b28 != 0;
            const CsSegDir *D = DD[dir];
            double o[2];
            if (zb_c3e0(D, S->rec, S->e2a, Z->tol * 4.0, o) > -1) {    /* 4.0 = 0x1803eba70 */
                double P[3];
                P[dir] = D->H[D->S[S->rec].line].pos; P[1 - dir] = o[0]; P[2] = o[1];
                double w[3] = { P[0] - S->p[0], P[1] - S->p[1], 0.0 }, dd[3] = { d[0], d[1], 0.0 }, n1[3], n2[3], N[3];
                zb_6cb0(0.05, dd, n1); zb_6cb0(0.05, w, n2);         /* 0.05 = 0x1803f04d0 */
                int e = zb_unit_n(Z->tol, n1, n2, N);
                if (e == 0) s = N[2] > 0.0 ? 1 : -1;
                else if (e == 3) {
                    double a = cs_v_angle_3d(n1, n2);
                    if (1e-06 > fabs(a)) t = 1;                                  /* 1e-6 = 0x1803f3ed8 */
                    else if (1e-06 > fabs(a - 3.141592653589793)) t = -1;       /* 0x1803eb838 */
                }
            }
        }
        if (!k) { *sa = s; *ta = t; } else { *sb = s; *tb = t; }
    }
    return *sa != 0 || *sb != 0;
}
/* FUN_1801695c0(ctx, flag, S0, S1, &t) (asm 1801695c0..18016987d; Z3, surf_classic3.md 12.44): the side of one step against the
   chord S0 -> S1, the per-step body of FUN_180166550 above for a single step: d = S1 - S0 (z dropped); the step (flag != 0: S0, else S1)
   without a record or with end above 1 (unsigned, asm 18016967d) -> 0; FUN_18015c3e0(rec, end, dir = +0x28 != 0, ctx, tol x 4.0
   (0x1803eba70), out) <= -1 -> 0; P[dir] = header[+0x24].pos, P[1 - dir] = out[0], w = P - the step (x, y; z 0, asm 18016979a);
   UnitNormal(FUN_180236cb0(0.05, d), FUN_180236cb0(0.05, w)) (0.05 = 0x1803f04d0): 0 -> +1 when its z > 0 else -1; 3 -> 0 with
   *t = +1 below 1e-6 (0x1803f3ed8) of angle, -1 within 1e-6 of pi (0x1803eb838), else 0; other errors 0 (*t 0). ND: a record of the
   other direction's array (no input does that) */
int cs_zb_95c0(double tol, CsSegDir *const *DD, const CsZStep *S0, const CsZStep *S1, int flag, short *t)
{
    const CsZStep *S = flag ? S0 : S1;
    *t = 0;
    if (S->rec < 0 || S->e2a > 1) return 0;
    int dir = S->b28 != 0;
    if (S->rdir != dir) return -101;
    const CsSegDir *D = DD[dir];
    double o[2];
    if (zb_c3e0(D, S->rec, S->e2a, tol * 4.0, o) <= -1) return 0;
    double P[2];
    P[dir] = D->H[D->S[S->rec].line].pos; P[1 - dir] = o[0];
    double dd[3] = { S1->p[0] - S0->p[0], S1->p[1] - S0->p[1], 0.0 }, w[3] = { P[0] - S->p[0], P[1] - S->p[1], 0.0 }, n1[3], n2[3], N[3];
    zb_6cb0(0.05, dd, n1); zb_6cb0(0.05, w, n2);
    int e = zb_unit_n(tol, n1, n2, N);
    if (e == 0) return N[2] > 0.0 ? 1 : -1;
    if (e == 3) {
        double a = cs_v_angle_3d(n1, n2);
        if (1e-06 > fabs(a)) *t = 1;
        else if (1e-06 > fabs(a - 3.141592653589793)) *t = -1;
    }
    return 0;
}
/* FUN_1801ad550(n, steps, ctx) (l.286?, asm 1801ad550..): the zone's side. From the first step with a record and end <= 1, each later such
   step against the one before (FUN_180166550): both signs negative or both positive -> that sign; else the first sign, or the second
   when the first is 0, is kept (a later pair overrides). Returns the last kept, 0 without two such steps */
short cs_zb_side(const CsZbc *Z, CsSegDir *const *DD, int n, const CsZStep *st)
{
    short keep = 0;
    int i = 0;
    while (i < n && !(st[i].rec >= 0 && st[i].e2a <= 1)) i++;
    if (i >= n) return 0;
    for (int j = i + 1; j < n; j++) {
        if (!(st[j].rec >= 0 && st[j].e2a <= 1)) continue;
        short a, b, ta, tb;
        if (zb_6550(Z, DD, &st[i], &st[j], &a, &b, &ta, &tb)) {
            if (a < 0 && b < 0) return a;
            if (a > 0 && b > 0) return a;
            if (a != 0) keep = a; else if (b != 0) keep = b;
        }
        i = j;
    }
    return keep;
}

/* FUN_1801c7060(atstart, any, zbc) (l.?, asm 1801c7060): +0x22 = 0; at the zone's start position: the tmpdztmp.tmp header (+0x9c count
   += the range's length when any, pass, byte 1; a write that is not 12 bytes: FUN_180268e20(1) -> +0x22); with any, per step of the range
   with a record and end < 2 its record (8 bytes), +0xa8 + 1 */
static void zb_c7060(CsZbc *Z, int atstart, int any)
{
    Z->err = 0;
    if (atstart) {
        if (any && Z->b8 <= Z->bc) Z->h[1].cnt += Z->bc - Z->b8 + 1;
        Z->h[1].pass = Z->pass; Z->h[1].one = 1;
        unsigned char o[12]; hdr_bytes(Z->h[1].cnt, Z->h[1].pass, Z->h[1].side, o);
        if (!zf_write(&Z->f[1], o, 12)) { Z->err = cs_cfb_err(1); return; }
    }
    if (!any || Z->b8 > Z->bc) return;
    for (int i = Z->b8; i <= Z->bc; i++)
        if (Z->st[i].rec >= 0 && (short)Z->st[i].e2a < 2) {
            long long r = zrec(Z->st[i].rdir, Z->st[i].rec);
            if (!zf_write(&Z->f[1], &r, 8)) { Z->err = cs_cfb_err(1); return; }
            Z->n88[1]++;
        }
}
/* FUN_1801c9be0(atstart, any, zbc) (l.?, asm 1801c9be0): flags[max(+0xb8, 1) - 1] = 0, +0x22 = 0. With any and at least 3 steps in the
   range: no entry written yet -> the box (x, y) from step 0; then every step from max(+0xb8, 1) to +0xc0: the box grows, a step closer
   than +0x30 to the last kept one is flagged, unflagged steps counted (and kept). Header (when at the zone's start, and the count or
   +0x88 >= 1): +0x7c += the count, pass, side, byte 1; then (any, count >= 1) every unflagged step of the range (0x30 bytes), +0x88 + 1 */
static void zb_c9be0(CsZbc *Z, int atstart, int any)
{
    int b8 = Z->b8, cnt = Z->bc < b8 ? 0 : Z->bc - b8 + 1, i6 = b8 > 0 ? b8 : 1;
    Z->fl[i6 - 1] = 0; Z->err = 0;
    int hdr;
    if (!any || cnt < 3) hdr = cnt >= 1 || Z->n88[0] >= 1;
    else {
        if (Z->n88[0] == 0) { Z->box[0] = Z->box[1] = Z->st[0].p[0]; Z->box[2] = Z->box[3] = Z->st[0].p[1]; }
        cnt = 0;
        if (i6 < Z->c0) {
            int prev = i6 - 1;
            for (int k = i6; k < Z->c0; k++) {
                const double *p = Z->st[k].p, *q = Z->st[prev].p;
                if (Z->box[0] <= p[0]) { if (Z->box[1] <= p[0] && p[0] != Z->box[1]) Z->box[1] = p[0]; } else Z->box[0] = p[0];
                if (Z->box[2] <= p[1]) { if (Z->box[3] <= p[1] && p[1] != Z->box[3]) Z->box[3] = p[1]; } else Z->box[2] = p[1];
                double dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
                if (sqrt(dx * dx + dy * dy + dz * dz) < Z->tol) Z->fl[k] = 1;
                if (!Z->fl[k]) { cnt++; prev = k; }
            }
            hdr = cnt >= 1 || Z->n88[0] >= 1;
        } else hdr = Z->n88[0] >= 1;
    }
    if (hdr && atstart) {
        if (any && cnt > 0) Z->h[0].cnt += cnt;
        Z->h[0].pass = Z->pass; Z->h[0].side = Z->side; Z->h[0].one = 1;
        unsigned char o[12]; hdr_bytes(Z->h[0].cnt, Z->h[0].pass, Z->h[0].side, o);
        if (!zf_write(&Z->f[0], o, 12)) { Z->err = cs_cfb_err(1); return; }
    }
    if (cnt < 1 || !any || Z->bc < Z->b8) return;
    for (int i = Z->b8; i <= Z->bc; i++)
        if (!Z->fl[i]) {
            unsigned char o[0x30]; step_bytes(&Z->st[i], o);
            if (!zf_write(&Z->f[0], o, 0x30)) { Z->err = cs_cfb_err(1); return; }
            Z->n88[0]++;
        }
}
/* FUN_1801aa5e0(zbc) (l.283712, asm 1801aa5e0..1801aa868): the steps' points to the 0x18 work array (+0xe0), FUN_1801c71b0(+0xc0, it,
   +0x28, flags); the draw mode +0x20 > 0 draws the steps (FUN_180197e40 / FUN_1801d7ca0: NOT PORTED, the builder runs with 0 from
   FUN_180186ff0); with +0x1e: side 0 -> +0x1c = FUN_1801ad550(+0xc0, steps, ctx); FUN_1801c7060(tmpdztmp.tmp at +0x98, +0xb8 <= +0xbc,
   ..), then without an error FUN_1801c9be0(tmpprojz.tmp at +0x78, the same, ..). Returns -1 for the draw mode (refused), else 0 */
int cs_zb_flush(CsZbc *Z, CsSegDir *const *DD, unsigned long ra)
{
    TRACE(Z, "aa5e0", ra, 0, NULL);
    for (int i = 0; i < Z->c0; i++) memcpy(Z->e0[i], Z->st[i].p, sizeof Z->e0[i]);
    zb_c71b0(Z->c0, (const double (*)[3])Z->e0, Z->d28, Z->fl);
    if (Z->draw > 0) return -1;
    if (!Z->fopen) return 0;
    if (Z->side == 0) {
        TRACE(Z, "ad550", 0x1801aa809ul, Z->c0, NULL);
        Z->side = cs_zb_side(Z, DD, Z->c0, Z->st);
    }
    int any = Z->b8 <= Z->bc;
    zb_c7060(Z, (int)Z->f[1].pos == Z->spos[1], any);
    if (Z->err == 0) zb_c9be0(Z, (int)Z->f[0].pos == Z->spos[0], any);
    return 0;
}
/* FUN_1801aea00(keep, zbc) (l.?, asm 1801aea00): when keep < +0xbc, the last keep steps (from +0xbc - keep + 1) moved to the front,
   +0xc4 / +0xc8 = -1 then the last moved step of each direction (+0x28 < 2, end < 2); +0xc0 = +0xb8 = keep, +0xbc = keep - 1 */
static void zb_aea00(CsZbc *Z, short keep)
{
    if (!(keep < Z->bc)) return;
    Z->last[0] = Z->last[1] = -1;
    int src = Z->bc - keep + 1;
    for (int i = 0; i < keep; i++) {
        const CsZStep *s = &Z->st[i];
        if (s->b28 < 2 && s->e2a < 2) Z->last[s->b28] = i;
        Z->st[i] = Z->st[src + i];
    }
    Z->c0 = keep; Z->b8 = keep; Z->bc = keep - 1;
}
/* FUN_1801a7d60(step, zbc) (l.281697, asm 1801a7d60): the last step (+0xbc, when the range is not empty) with direction < 2 and end < 2
   becomes its direction's last (+0xc4 / +0xc8); the step is copied to +0xc0 with +0 = +0xb4; +0xac / +0xb4 + 1, +0xbc = +0xc0; the zone's
   first step sets +0xe8 / +0xf0 (record, end) and the box, later ones grow it; +0xc0 + 1, and at the capacity +0xb0 the buffer is flushed
   (FUN_1801aa5e0) and its last 10 steps kept (FUN_1801aea00(10, ..)) */
int cs_zb_add(CsZbc *Z, CsSegDir *const *DD, const CsZStep *s, unsigned long ra)
{
    TRACE(Z, "a7d60", ra, 0, s);
    if (Z->b8 <= Z->bc) {
        const CsZStep *l = &Z->st[Z->bc];
        if (l->b28 < 2 && l->e2a < 2) Z->last[l->b28] = Z->bc;
    }
    CsZStep *d = &Z->st[Z->c0];
    *d = *s; d->i0 = Z->b4;
    Z->ac++; Z->b4++; Z->bc = Z->c0;
    if (Z->ac == 1) {
        Z->frec = s->rec; Z->fdir = s->rdir; Z->f0 = (short)s->e2a;
        Z->box[0] = Z->box[1] = s->p[0]; Z->box[2] = Z->box[3] = s->p[1]; Z->box[4] = Z->box[5] = s->p[2];
    } else
        for (int k = 0; k < 3; k++) {
            double v = s->p[k];
            if (Z->box[2 * k] <= v) { if (Z->box[2 * k + 1] <= v && v != Z->box[2 * k + 1]) Z->box[2 * k + 1] = v; } else Z->box[2 * k] = v;
        }
    Z->c0++;
    if (Z->c0 == Z->cap) {
        if (cs_zb_flush(Z, DD, 0x1801a7ee6ul) < 0) return -1;
        zb_aea00(Z, 10);
    }
    return 0;
}
/* FUN_1801a95e0(zbc) (l.283002, asm 1801a95e0): per file, when its header (count, pass, side) differs from (+0x88 / +0xa8, +0x1a, +0x1c):
   the header = those values (byte 1), rewritten at the zone's start, the file put back where it was; a write that is not 12 bytes:
   FUN_180268e20(1) -> +0x22 */
void cs_zb_headers(CsZbc *Z, unsigned long ra)
{
    TRACE(Z, "a95e0", ra, 0, NULL);
    for (int f = 0; f < 2; f++) {
        CsZHdr *h = &Z->h[f];
        if (h->cnt == Z->n88[f] && h->pass == Z->pass && h->side == Z->side) continue;
        size_t at = Z->f[f].pos;
        Z->f[f].pos = (size_t)Z->spos[f];
        h->cnt = Z->n88[f]; h->pass = Z->pass; h->one = 1; h->side = Z->side;
        unsigned char o[12]; hdr_bytes(h->cnt, h->pass, h->side, o);
        if (!zf_write(&Z->f[f], o, 12)) { Z->err = cs_cfb_err(1); return; }
        Z->f[f].pos = at;
    }
}
/* FUN_1801a9460(zbc) (asm 1801a9460..1801a94aa): +0x88 entries, or -1 when fewer than 4 or the box |+0x58 - +0x50| x |+0x48 - +0x40| is
   below DAT_1804e9238 x 0.1 (0x1803f04d8) */
int cs_zb_check(CsZbc *Z, unsigned long ra)
{
    TRACE(Z, "a9460", ra, 0, NULL);
    int n = Z->n88[0];
    if (n < 4 || Z->t9238 * 0.1 > fabs(Z->box[3] - Z->box[2]) * fabs(Z->box[1] - Z->box[0])) return -1;
    return n;
}
/* FUN_1801adec0(zone, zbc) (l.286195, asm 1801adec0): the zone dropped: when zone is +0x18, both files back to the zone's start (>= 0) */
void cs_zb_drop(CsZbc *Z, short zone, unsigned long ra)
{
    TRACE(Z, "adec0", ra, zone, NULL);
    if (zone != Z->zone) return;
    if (Z->spos[0] > -1) Z->f[0].pos = (size_t)Z->spos[0];
    if (Z->spos[1] > -1) Z->f[1].pos = (size_t)Z->spos[1];
}
