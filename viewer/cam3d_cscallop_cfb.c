/* cam3d_cscallop_cfb.c - Constant Scallop (op 43) stage C item 3: FUN_1801cfb60's per-line stages for kinds 7 / 6 (formats/surf_classic3.md
 * 12.2, 12.18, 12.19), on the scan file of the scanner port (cam3d_cscallop_lines.c, 12.17).
 * Sources: decomp/mcmsurf_all.c (l.N) and objdump of mcmsurf.dll (asm addresses). Every floating-point expression keeps the DLL's operand
 * order (build with -ffp-contract=off).
 * Ported so far: FUN_1801cfb60's constants (l.309965-309980), the block walk FUN_180183310 with its readers FUN_1801a6ee0 / FUN_1801ae2b0 /
 * FUN_1801aaa50 / FUN_1801ad6c0 / FUN_1801abc40 -> FUN_1801abe30 / FUN_1801aab50, the wall-segment predicate FUN_180182070 (FUN_1801688d0,
 * FUN_1801d8ed0). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

static double rd(const unsigned char *p) { double v; memcpy(&v, p, 8); return v; }
static int ri(const unsigned char *p) { int v; memcpy(&v, p, 4); return v; }
static short rs(const unsigned char *p) { short v; memcpy(&v, p, 2); return v; }

/* FUN_180268e20 (l.405217): the error code of a failure class: 0 -> -11, 1 -> -12, 2 -> -4, else -16 */
int cs_cfb_err(int c) { return c == 0 ? -11 : c == 1 ? -12 : c == 2 ? -4 : -16; }

/* FUN_1801cfb60 l.309965-309980 (kinds other than 10 / 11: l.309973 (ushort)(kind - 10) > 1). chord = param_10 (msurf_chord_dev),
   systol = mccore tol, cdev = msurf_corner_dev */
void cs_cfb_consts(int kind, double chord, double systol, double cdev, CsCfbConst *C)
{
    C->t9238 = chord * 0.9;
    C->t9240 = (chord - C->t9238) * 0.5;
    C->t9248 = C->t9238 * 0.01;
    C->t9250 = systol;
    if (C->t9238 * 0.0001 < C->t9250) C->t9250 = C->t9238 * 0.0001;
    C->t9258 = C->t9238;
    if ((unsigned short)(kind - 10) > 1) {
        if (cdev <= chord) C->t9258 = cdev * 0.9;
        else C->t9258 = cdev - C->t9240 * 2.0;
    }
    C->systol = systol;
}

/* FUN_1801688d0 (l.239850; jump table at 180168900 / 180168908): the codes 2, 3, 9, 10, 16, 17, 20, 21, 27, 28, 34, 35, 40, 41 */
int cs_code_vert(int code)
{
    switch (code) {
    case 2: case 3: case 9: case 10: case 16: case 17: case 20: case 21: case 27: case 28: case 34: case 35: case 40: case 41: return 1;
    default: return 0;
    }
}
/* FUN_1801d8ed0 (l.314292): |n.z| < 8.7266462e-05 (0x1804150a0 = 0x3f16e05a66bc490f) */
static int horiz_n(const double *n) { return 8.7266462e-05 > fabs(n[2]); }

/* FUN_180182070 (l.259253, asm 180182070..18018211e): a vertical segment - the code's own family (FUN_1801688d0), or |ds| < tol (s1 - s0),
   |dz| > mccore tol (z1 - z0, comisd: strictly) and both normals horizontal */
int cs_seg_vertical(double tol, double systol, int code, const double s[2], const double z[2], const double n[6])
{
    if (cs_code_vert(code)) return 1;
    if (!(tol > fabs(s[1] - s[0]))) return 0;
    if (!(fabs(z[1] - z[0]) > systol)) return 0;
    return horiz_n(n) && horiz_n(n + 3);
}

/* FUN_180183310(file, pos, FUN_1801a6ee0, seg, 1, buf, p7, &p8) (l.259964, asm 180183310..18018360b) as FUN_1801cfb60 calls it for kinds
   6 / 7 (asm 1801d2881..1801d28b7: param_5 = 1, the 0x30-byte points; param_7 = local_3478, zeroed by FUN_1801ada90 at l.310759 and set
   only with containment, so its flag byte is 0 and the codes >= 0x25 are dropped). The chain of the line's blocks from pos: FUN_1801a6ee0
   reads the 0x18-byte block trailer (+2 short n, +4 int previous block, +8 the line position -> *p8; returns read == 0x18, asm 1801a6f15),
   n < 1 or a failed read -> FUN_180268e20(0) = -11; the point buffer grows to n + 100 records when n > its room (FUN_1801aaa50 /
   FUN_1801ad6c0: free + calloc, *cap); FUN_1801abc40(.., 1, ..) -> FUN_1801abe30 reads the n 0x30-byte points before the trailer (pos - n
   x FUN_1801ae2b0(1) = 0x30); FUN_1801aab50(1, ..): s +0, z +8, normal +0x10..+0x27, code +0x28. Consecutive points pair into segments
   (the flag bl, cleared per block at asm 18018344d): an open point takes s / z / normal / code; the next one closes it and, when the open
   code < 0x25, seg(code, DAT_1804e8ff0, {s0, s1}, {z0, z1}, {n0, n1}) is called; then a closing code >= 0x27 opens the next segment at the
   same point with code - 0x27, else the next point opens one. DAT_1804e8ff0 (two flag bytes) is never written (asm: only lea references;
   .data raw data ends at 1804e3800, so it is zero). Returns 0 at the end of the chain (previous block -1). seg returning non-zero (the DLL:
   DAT_1804e9344 != 0 after the call, asm 180183531) -> FUN_180268e20(2) = -4. */
int cs_cfb_walk(const unsigned char *f, size_t len, int pos, CsSegFn seg, void *ctx, double *p8, int *cap)
{
    static const unsigned char fl[2] = { 0, 0 };
    long long p = pos;
    for (;;) {
        if (p == -1) return 0;
        if (p < 0 || (size_t)p + 0x18 > len) return cs_cfb_err(0);
        short n = rs(f + p + 2);
        long long prev = ri(f + p + 4);
        *p8 = rd(f + p + 8);
        if (n < 1) return cs_cfb_err(0);
        if (*cap < n) *cap = (unsigned short)(n + 100);          /* FUN_1801ad6c0(1, n + 100, buf): calloc never fails here */
        int at = (int)(p - (long long)n * 0x30);                   /* asm 18018340c..180183436: (int)(pos - n * 0x30) */
        if (at < 0 || (size_t)at + (size_t)n * 0x30 > len) return cs_cfb_err(0);   /* FUN_1801abe30's short read */
        const unsigned char *b = f + at;
        int open = 0, code = 0; double s0 = 0, z0 = 0, n0[3] = { 0, 0, 0 };
        for (int i = 0; i < n; i++) {
            const unsigned char *q = b + (size_t)i * 0x30;
            double s = rd(q), z = rd(q + 8), nn[3] = { rd(q + 0x10), rd(q + 0x18), rd(q + 0x20) };
            short c = rs(q + 0x28);
            if (!open) { open = 1; s0 = s; z0 = z; memcpy(n0, nn, sizeof n0); code = c; continue; }
            if (code < 0x25) {
                double S[2] = { s0, s }, Z[2] = { z0, z }, N[6] = { n0[0], n0[1], n0[2], nn[0], nn[1], nn[2] };
                if (seg(ctx, code, fl, S, Z, N)) return cs_cfb_err(2);
            }
            if (c >= 0x27) { s0 = s; z0 = z; memcpy(n0, nn, sizeof n0); code = c - 0x27; open = 1; }
            else open = 0;
        }
        p = prev;
    }
}

/* the line table of a scan file (FUN_1801e7ad0 l.324073 -> FUN_1802410d0 l.379542: the trailer at header +0xc; its rows (9 doubles), 8
   doubles, int flag, int n, then the n ints). Returns n, or -1 */
int cs_cfb_table(const unsigned char *f, size_t len, int **table)
{
    if (len < 0x38) return -1;
    long long t = ri(f + 0xc);
    if (t < 0 || (size_t)t + 0x90 > len) return -1;
    int n = ri(f + t + 0x8c);
    if (n < 0 || (size_t)t + 0x90 + (size_t)n * 4 > len) return -1;
    *table = malloc(sizeof(int) * (size_t)(n + 1));
    for (int i = 0; i < n; i++) (*table)[i] = ri(f + t + 0x90 + 4 * (size_t)i);
    return n;
}

/* ---- the code families (unit-checked over all codes: CFBA mode) ---- */
/* FUN_18015afd0 (l.230426): code in 0..3 or 0x28..0x2a (unsigned compares of the sign-extended code) */
int cs_code_afd0(int code) { unsigned u = (unsigned)code; return u <= 3 || u - 0x28 <= 2; }
/* FUN_18015a310 (l.229818): code in 0x12..0x23 */
int cs_code_a310(int code) { return (unsigned)code - 0x12u < 0x12; }
/* FUN_180163f30 (l.236813): (ushort)code < 4 */
int cs_code_f3f30(int code) { return (unsigned short)code < 4; }
/* FUN_18015bc40 (l.231115) */
int cs_code_bc40(int code)
{
    switch (code) { case 1: case 3: case 8: case 10: case 15: case 17: case 19: case 21: case 26: case 28: case 33: case 35: return 1; default: return 0; }
}
/* FUN_18018af60 (l.264519) */
int cs_code_af60(int code)
{
    switch (code) {
    case 1: case 3: return 3;
    case 8: case 10: return 10;
    case 0xf: case 0x11: return 0x11;
    case 0x13: case 0x15: return 0x15;
    case 0x1a: case 0x1c: return 0x1c;
    case 0x21: case 0x23: return 0x23;
    default: return code;
    }
}
/* FUN_18018b0e0 (l.264600) */
int cs_code_b0e0(int code)
{
    if (code >= 0 && code <= 3) return 2;
    if (code >= 0x28 && code <= 0x2a) return 2;
    if (code >= 4 && code <= 10) return 9;
    if (code >= 0xb && code <= 0x11) return 0x10;
    if (code >= 0x12 && code <= 0x15) return 0x14;
    if (code >= 0x16 && code <= 0x1c) return 0x1b;
    if (code >= 0x1d && code <= 0x23) return 0x22;
    return code;
}
/* FUN_18015f6c0 (l.233638) */
int cs_code_f6c0(int code)
{
    switch (code) {
    case 1: case 8: case 0xf: case 0x13: case 0x1a: case 0x21: return 1;
    case 2: case 9: case 0x10: case 0x14: case 0x1b: case 0x22: case 0x28: return 2;
    case 3: case 10: case 0x11: case 0x15: case 0x1c: case 0x23: return 3;
    default: return 0;
    }
}

/* ---- the line list (the "estore": FUN_1801cfb60's node store DAT_1804e9328 of DAT_1804e9320 0x68-byte nodes, 0x7fff with the large
   buffer, N13-28; head DAT_1804e9330, tail 9334, free head 9338, free count 933c, last stored 9340, error 9344). The nodes are CsNode,
   the DLL's layout (cs_node_bytes): +0 flag, +8 s0, +0x10 z0, +0x18 n0[3], +0x30 flag, +0x38 s1, +0x40 z1, +0x48 n1[3], +0x60 code,
   +0x62 next. NO DLL ORACLE for the functions on the store (12.19): it is a global taken from pl2StackAlloc (l.310682-310694), not an
   argument of any of them (orchestrator ruling). ---- */
#define CP3(d, s) ((d)[0] = (s)[0], (d)[1] = (s)[1], (d)[2] = (s)[2])

/* FUN_1801a5570 (l.280054): every node free, linked by next in index order, the last -1; the list empty, the error 0 */
void cs_est_reset(CsEst *L)
{
    L->nfree = (short)L->cap;
    for (int i = 0; i < L->cap; i++) L->nd[i].next = (short)(i + 1);
    L->freeh = 0; L->err = 0; L->head = -1; L->tail = -1;
    L->nd[L->nfree - 1].next = -1;
}
int cs_est_init_cfb(CsEst *L, int cap)
{
    L->cap = cap; L->nd = calloc((size_t)cap + 1, sizeof(CsNode)); L->last = -1;
    if (!L->nd) return -1;
    cs_est_reset(L);
    return 0;
}
/* FUN_1801a5510 (l.280035): the line's nodes back on the free list (the tail linked to the old free head), the list empty; the free
   count is set back to the store size */
void cs_est_free_line(CsEst *L)
{
    short f = L->freeh;
    if (L->head != -1) {
        L->freeh = L->head;
        L->nd[L->tail].next = f;
        L->tail = -1; L->head = -1;
        L->nfree = (short)L->cap;
    }
}
/* FUN_180182120 (l.259278): FUN_180182070 on a stored node */
static int node_vertical(const CsEst *L, const CsCfbConst *C, short i)
{
    const CsNode *p = &L->nd[i];
    double s[2] = { p->s0, p->s1 }, z[2] = { p->z0, p->z1 };
    double n[6] = { p->n0[0], p->n0[1], p->n0[2], p->n1[0], p->n1[1], p->n1[2] };
    return cs_seg_vertical(C->t9250, C->systol, p->code, s, z, n);
}
/* FUN_1801a2760 (l.278175): a node from the free head, linked after prev / before next (-1: the new head / tail); no free node ->
   error 2 ("Ran Out of ROOM in ESTORE") */
static void est_store(CsEst *L, short code, unsigned char f0, double s0, double z0, const double *n0, unsigned char f1, double s1, double z1,
                      const double *n1, short prev, short next)
{
    if (L->freeh == -1) { L->err = 2; return; }
    short k = L->freeh; CsNode *p = &L->nd[k];
    short nf = p->next;
    p->f0 = f0; p->s0 = s0; p->z0 = z0; CP3(p->n0, n0);
    p->f1 = f1; p->s1 = s1; p->z1 = z1; CP3(p->n1, n1);
    p->code = code;
    if (prev == -1) L->head = k; else L->nd[prev].next = k;
    if (next == -1) L->tail = k;
    L->nfree--;
    p->next = next;
    L->last = k;
    L->freeh = nf;
}
/* FUN_180183080 (l.259840, asm 180183080..180183294): for a vertical segment, the place among the vertical nodes at the same s and the z
   snap; s0 = the segment's s0, z = its z pair (changed in place), *prev / *next the place found by FUN_180182e70 */
static void vert_place(const CsEst *L, const CsCfbConst *C, double s0, double *z, short *prev, short *next)
{
    const CsNode *E = L->nd;
    short si = *prev, bx, di;
    if (si == -1 && *next == -1) return;
    int up = z[1] > z[0];                                         /* seta, asm 1801830cf..1801830e0 */
    if (si == -1) {
        if (!node_vertical(L, C, *next)) return;
        bx = *next;
    } else {
        bx = node_vertical(L, C, si) ? L->head : si;              /* asm 180183190..1801831a0 */
        for (;;) {
            if (bx == -1) return;
            di = E[bx].next;
            if (s0 - E[bx].s1 > 1e-12) { si = bx; bx = di; continue; }   /* asm 1801831c2..1801831cf, 1801831f9 */
            if (E[bx].s0 - s0 > 1e-12) return;
            if (node_vertical(L, C, bx)) break;
            si = bx; bx = di;
        }
    }
    for (;;) {                                                    /* asm 18018310e..180183235 */
        if (bx == -1) return;
        double nz0 = E[bx].z0;
        di = E[bx].next;
        if (1e-12 > fabs(z[1] - nz0)) { z[1] = nz0; *prev = si; *next = bx; return; }
        if (1e-12 > fabs(z[0] - E[bx].z1)) { z[0] = E[bx].z1; *prev = bx; *next = di; return; }
        if (up ? nz0 > z[1] : z[1] > nz0) { *prev = si; *next = bx; return; }
        if (di == -1 || !node_vertical(L, C, di)) { *prev = bx; *next = di; return; }
        si = bx; bx = di;
    }
}
/* FUN_180182e70 (l.259776, asm 180182e70..180183079): the segment stored in the line list, ordered by s. DAT_1804e8fff = 0 on the scanner
   path (l.310261 / l.310681), so FUN_1801b4b60 is not called; no free node -> error 2. After the tail when s0 >= the tail's s1; else
   before the first node whose s0 >= s1 (s1 snapped to it within 1e-12). A vertical segment (FUN_180182070) is placed by FUN_180183080. */
int cs_est_insert(CsEst *L, const CsCfbConst *C, int code, const unsigned char fl[2], double s[2], double z[2], const double n[6])
{
    short prev = -1, next = -1, h = L->head;
    if (L->nfree < 1 && L->err == 0) L->err = 2;
    if (L->tail != -1 && !(s[0] < L->nd[L->tail].s1)) prev = L->tail;   /* comisd / jb, asm 180182f3d..180182f57 */
    else if (h != -1) {
        for (;;) {
            short nx = L->nd[h].next; double x = L->nd[h].s0;
            next = nx;
            if (x >= s[1]) { next = h; break; }
            if (1e-12 > s[1] - x) { s[1] = x; next = h; break; }
            prev = h; h = nx;
            if (nx == -1) break;
        }
    }
    if (cs_seg_vertical(C->t9250, C->systol, code, s, z, n)) vert_place(L, C, s[0], z, &prev, &next);
    est_store(L, (short)code, fl[0], s[0], z[0], n, fl[1], s[1], z[1], n + 3, prev, next);
    return L->err;
}
/* the list as an array of nodes in list order (a unit-check input; the DLL's FUN_180184cc0 builds its array itself) */
int cs_est_array(const CsEst *L, CsNode **out)
{
    int n = 0;
    for (short i = L->head; i != -1; i = L->nd[i].next) n++;
    *out = calloc((size_t)n + 1, sizeof(CsNode));
    int k = 0;
    for (short i = L->head; i != -1; i = L->nd[i].next) (*out)[k++] = L->nd[i];
    return n;
}

/* FUN_1801a7c70 (l.281626): a node's length along the line, DAT_1804e9248 for the vertical codes (FUN_1801688d0) */
static double node_len(const CsEst *L, const CsCfbConst *C, short k)
{
    const CsNode *N = &L->nd[k];
    double a = N->s1, b = N->s0;
    if (cs_code_vert(N->code)) return C->t9248;
    return a - b;
}
/* FUN_1801d7da0 (l.313585, asm 1801d7da0..1801d8238): runs of nodes shorter than DAT_1804e9250 merged into one flat node. The scanner's
   FUN_1801e9ec0 (cam3d_cscallop_merge.c clean_short) with one more condition: the node's FUN_18015bc40 family is the previous node's
   (FUN_18015b290, asm 1801d7e1c..1801d7e32); DAT_1804e9250 / 9248 for DAT_1804ea578 / ea570 */
void cs_cfb_clean_short(CsEst *L, const CsCfbConst *C)
{
    CsNode *E = L->nd;
    const double tol = C->t9250;
    short prev = -1, k = L->head;
    while (k != -1) {
        short di = E[k].next, si;
        double Ln = node_len(L, C, k);
        if (tol > Ln && (prev == -1 || cs_code_bc40(E[prev].code) == cs_code_bc40(E[k].code))) {
            double zm, n[3];
            short lg = k;
            double z0 = E[k].z0, z1 = E[k].z1;
            zm = z0 > z1 ? z0 : z1;                          /* asm 1801d7e3f..1801d7e72 */
            if (z0 > z1) CP3(n, E[k].n0); else CP3(n, E[k].n1);
            si = k;
            for (;;) {                                       /* 1801d7e80 */
                di = E[si].next;
                if (di == -1) break;
                if (node_len(L, C, di) > tol) break;
                if (E[di].s0 - E[si].s1 > tol) break;
                if (E[di].s1 - E[k].s0 > C->t9248) break;
                double l1 = E[di].s1 - E[di].s0;
                if (l1 > Ln) { lg = di; Ln = l1; }
                if (E[di].z0 > zm) zm = E[di].z0;            /* 1801d7f0d: the normal is not taken (comisd xmm0, xmm0) */
                if (E[di].z1 > zm) zm = E[di].z1;
                si = di;
            }
            int fromnext = 0;
            E[k].code = E[lg].code;                          /* 1801d7f5b */
            if (prev != -1) {
                double g1 = E[k].s0 - E[prev].s1;
                if (di != -1 && tol > g1) {
                    double g2 = E[di].s0 - E[si].s1;
                    if (tol > g2) {
                        if (g2 > g1) goto take_prev;
                        goto take_next;
                    }
                }
                if (tol > g1) {                              /* 1801d7fe3 */
take_prev:          E[k].code = E[prev].code; zm = E[prev].z1; CP3(n, E[prev].n1);
                    E[k].s0 = E[prev].s1;
                    goto set;
                }
            }
            if (di != -1 && tol > E[di].s0 - E[si].s1) {     /* 1801d8030 */
take_next:      E[k].code = E[di].code; zm = E[di].z0; CP3(n, E[di].n0); fromnext = 1;
            }
set:        CP3(E[k].n0, n); CP3(E[k].n1, n); E[k].z0 = zm; E[k].z1 = zm;   /* 1801d8082 */
            E[k].s1 = fromnext ? E[di].s0 : E[si].s1;
            for (short j = E[k].next; j != di; j = E[j].next) {
                E[j].s0 = E[k].s1; E[j].s1 = E[k].s1; E[j].z0 = E[k].z1; E[j].z1 = E[k].z1;
                CP3(E[j].n0, E[k].n1); CP3(E[j].n1, E[k].n1); E[j].code = E[k].code;
            }
        } else si = k;
        if (prev != -1 && tol > E[k].s0 - E[prev].s1) E[prev].s1 = E[k].s0;   /* 1801d81b2 */
        prev = si; k = di;
    }
}
/* FUN_1801974e0 (l.272574, asm 1801974e0..180197549) = the scanner's FUN_1801e02b0 on FUN_1801cfb60's store: FUN_1801d7da0, then per run
   FUN_1801a2cc0 + FUN_1801a1d80 (cs_clean_runs: the same asm, DAT_1804e9240 the run tolerance, mccore tol for dist_pt_l) */
void cs_cfb_clean(CsEst *L, const CsCfbConst *C)
{
    cs_cfb_clean_short(L, C);
    CsScan V; memset(&V, 0, sizeof V);
    V.est = L->nd; V.ehead = L->head; V.half = C->t9240; V.ctol = C->systol;
    cs_clean_runs(&V);
}

/* ---- the array stages (FUN_180184cc0's output local_3638: the node array is a direct argument of each) ---- */
/* FUN_180182690 (l.259480): the code of the 0x28 connector b from its neighbours a / c (-1: none) */
static void conn_code(int a, int b, int c, CsNode *arr)
{
    if (a == -1) { arr[b].code = c == -1 ? 2 : (short)cs_code_b0e0(arr[c].code); return; }
    if (c == -1) { arr[b].code = (short)cs_code_b0e0(arr[a].code); return; }
    short ca = arr[a].code, cc = arr[c].code;
    if (cs_code_bc40(ca) && cs_code_bc40(cc)) { arr[b].code = (short)cs_code_af60(cc); return; }
    if (!cs_code_a310(ca) && !cs_code_a310(cc)) {
        arr[b].code = (!cs_code_afd0(ca) && !cs_code_afd0(cc)) ? (short)cs_code_b0e0(cc) : 2;
        return;
    }
    arr[b].code = 0x14;
}
/* FUN_180182620(n, arr) (l.259450): every code 0x28 (a vertical connector) recoded from its neighbours, in array order */
void cs_conn_codes(int n, CsNode *arr)
{
    for (int i = 1; i <= n; i++)
        if (arr[i - 1].code == 0x28) conn_code(i - 2, i - 1, i == n ? -1 : i, arr);
}
/* FUN_180181a80(p1, tol, n, arr) (l.259010, asm 180181a80..180181c55): runs of nodes joined within tol (|s0 - previous s1| <= tol, the
   next not of FUN_18015a310's family); inside a run, from its first to its last node of FUN_18015afd0's family, the nodes of neither
   family and not code 7 are recoded: FUN_180163f30(p1) -> FUN_18015f6c0(code); p1 0x2a and FUN_1801688d0(code) -> 0x29; else p1.
   Returns the count. FUN_1801cfb60 calls it with p1 0 and tol = its param_22 (msurf_max_short_gap: xmm9 from [rbp+0x36a8] at asm
   1801d1181, passed at 1801d2cf7) */
int cs_join_runs(int p1, double tol, int n, CsNode *arr)
{
    int cnt = 0, si = 0;
    while (si < n) {
        int di = si, bp = si + 1;
        while (bp < n) {
            if (fabs(arr[bp].s0 - arr[di].s1) > tol) break;
            if (cs_code_a310(arr[bp].code)) break;
            di = bp; bp++;
        }
        while (si < bp && !cs_code_afd0(arr[si].code)) si++;
        if (si < di) {
            while (di > si && !cs_code_afd0(arr[di].code)) di--;
            if (si < di) {
                for (int k = si; k <= di; k++) {
                    short c = arr[k].code;
                    if (cs_code_a310(c) || cs_code_afd0(c) || c == 7) continue;
                    arr[k].code = cs_code_f3f30(p1) ? (short)cs_code_f6c0(c) : (p1 == 0x2a && cs_code_vert(c)) ? 0x29 : (short)p1;
                    cnt++;
                }
            }
        }
        si = bp;
    }
    return cnt;
}

/* FUN_1801b0c80(rufvcs, reader, ..) (l.288238): the line's wall records (FUN_1801cfb60's per-line index DAT_1804e9798[line] = {int offset,
   short count}, from FUN_1801e7c20) read into DAT_1804e97a8; returns their count. A line with offset -1 has none (0). Lines with walls:
   NOT PORTED (they also take FUN_180184cc0's vcs branch, FUN_18015bca0 / FUN_1801c8080, and its pl2 arrays) -> -1 */
int cs_cfb_walls(int off, int count)
{
    if (off == -1) return 0;
    (void)count;
    return -1;
}
/* FUN_18015f2f0(head, k, next, store, flags, &n) (l.233433, asm 18015f2f0..18015f5b5) with flags byte 0 = 0 (FUN_18015d700's first
   argument, uVar41 = 0 for kinds 6 / 7 when DAT_1804e8d80 = 0, l.310017-310029; asm 18015f314 -> 18015f4dd): the normal of a vertical
   connector from k's end normal a = n1 and next's start normal b = n0 (FUN_1801d8ed0: horizontal): both or neither horizontal -> a when k's
   z1 > next's z0 (ja) else b; only a -> a; only b -> b. Returns 1. (Flags byte 0 = 1, the z-limit mode: NOT PORTED.) */
static void conn_normal(const CsNode *k, const CsNode *nx, double out[3])
{
    int a = horiz_n(k->n1), b = horiz_n(nx->n0);
    if (a == b) { if (k->z1 > nx->z0) CP3(out, k->n1); else CP3(out, nx->n0); }
    else if (a) CP3(out, k->n1);
    else CP3(out, nx->n0);
}
/* FUN_180184cc0(0, flags, nvcs, vcs, cap, arr) (l.260939; FUN_1801cfb60's calls at asm 1801d2c96..1801d2cba: cap = local_362c = max(16000,
   the store size) = 0x7fff (N13-28), arr = local_3638) with nvcs = 0 (no pl2 work arrays: l.261001-261018 run only for nvcs > 0): the
   list into the array in order, nodes of code 0x24 left out; a node that is vertical by its geometry (FUN_180182120) but not by its
   code (FUN_1801688d0) gets code 0x28; with flags byte 1 (local_3157 = FUN_18015d700's second argument local_3672 = 1) a connector of
   code 0x28 is added after a node whose next node starts at the same s (|s1 - s0'| < DAT_1804e9250) at another z (|z1 - z0'| >
   DAT_1804e9250): from (s1, z1, f1) to the next's (s0, z0, f0), both normals FUN_18015f2f0's. Returns the count, or -4 (the array full:
   FUN_180268e20(2)). nvcs > 0: NOT PORTED (-1) */
int cs_cfb_to_array(const CsEst *L, const CsCfbConst *C, int flag1, int nvcs, int cap, CsNode *arr)
{
    if (nvcs > 0) return -1;
    return cs_cfb_to_array_v(L, C, flag1, 0, NULL, cap, arr);
}
/* The same with the line's walls (nvcs > 0: vcs = DAT_1804e97a8 as FUN_1801b0c80 filled it, cs_wall_read): FUN_180184cc0 takes three work
   arrays of nvcs + 5 records from pl2 (FUN_180158460(nvcs + 5, 2, 0 / 1 / 2), l.261001-261018, asm 180184d55..180184dd7; local_158 = their
   size, 0 when one failed - not with the large buffer, N13-28); at a vertical gap (as above) the points {k.s1, k.z1, nx.s0, nx.z0}
   (asm 180184fce..180185028) go to FUN_18015bca0(pts, 9238 x 8.0 + 9258, nvcs, vcs, A, B, C, &arr[n]) (cs_wall_split); 0 -> FUN_1801c8080
   (pts, nvcs, vcs, &arr[n]) (cs_wall_connector), > -1 -> 1 node, else the straight connector; the r nodes get f0 = k.f1 for the first,
   else 1, f1 = nx.f0 for the last, else 1 (asm 1801850b0..18018510e; n >= cap -> -4) */
long cs_wall_cov[4];   /* test coverage: wall gaps split by FUN_18015bca0, one FUN_1801c8080 connector, straight, nodes from the split */
int cs_cfb_to_array_v(const CsEst *L, const CsCfbConst *C, int flag1, int nvcs, CsNode *vcs, int cap, CsNode *arr)
{
    const CsNode *E = L->nd;
    CsNode *wa = NULL, *wb = NULL, *wc = NULL;
    if (nvcs > 0) { wa = calloc((size_t)nvcs + 5, sizeof *wa); wb = calloc((size_t)nvcs + 5, sizeof *wb); wc = calloc((size_t)nvcs + 5, sizeof *wc); }
    int n = 0;
    for (short k = L->head; k != -1; k = E[k].next) {
        if (E[k].code == 0x24) continue;
        if (cap <= n) { n = cs_cfb_err(2); break; }
        arr[n] = E[k];
        arr[n].next = 0;
        if (!cs_code_vert(E[k].code) && node_vertical(L, C, k)) arr[n].code = 0x28;
        n++;
        short nx = E[k].next;
        if (flag1 && nx != -1) {
            double d = E[k].s1 - E[nx].s0;
            if (fabs(d) < C->t9250 && fabs(E[k].z1 - E[nx].z0) > C->t9250) {
                if (nvcs > 0) {
                    double pts[4] = { E[k].s1, E[k].z1, E[nx].s0, E[nx].z0 };
                    CsNode *o = &arr[n];
                    short r = cs_wall_split(pts, C->t9238 * 8.0 + C->t9258, nvcs, vcs, C->systol, wa, wb, wc, o);
                    if (r > 0) { cs_wall_cov[0]++; cs_wall_cov[3] += r; }
                    else if ((r = cs_wall_connector(pts, nvcs, vcs, C, o) > -1 ? 1 : 0)) cs_wall_cov[1]++;
                    else cs_wall_cov[2]++;
                    if (r > 0) {
                        for (short j = 0; j < r; j++) {
                            if (cap <= n) { n = cs_cfb_err(2); goto done; }
                            arr[n].f0 = j == 0 ? E[k].f1 : 1;
                            arr[n].f1 = j == r - 1 ? E[nx].f0 : 1;
                            arr[n].next = 0;
                            n++;
                        }
                        continue;
                    }
                }
                double nr[3];
                conn_normal(&E[k], &E[nx], nr);
                if (cap <= n) { n = cs_cfb_err(2); break; }
                CsNode *c = &arr[n++];
                memset(c, 0, sizeof *c);
                c->f0 = E[k].f1; c->s0 = E[k].s1; c->z0 = E[k].z1;
                c->f1 = E[nx].f0; c->s1 = E[nx].s0; c->z1 = E[nx].z0;
                c->code = 0x28; CP3(c->n0, nr); CP3(c->n1, nr);
            }
        }
    }
done:
    free(wa); free(wb); free(wc);
    return n;
}
/* FUN_180181680(n, arr) (l.258829): the array's nodes but code 0x24 back into the (emptied) list by FUN_180182e70 */
int cs_cfb_from_array(CsEst *L, const CsCfbConst *C, int n, const CsNode *arr)
{
    for (int i = 0; i < n; i++) {
        if (arr[i].code == 0x24) continue;
        unsigned char fl[2] = { arr[i].f0, arr[i].f1 };
        double s[2] = { arr[i].s0, arr[i].s1 }, z[2] = { arr[i].z0, arr[i].z1 };
        double nn[6] = { arr[i].n0[0], arr[i].n0[1], arr[i].n0[2], arr[i].n1[0], arr[i].n1[1], arr[i].n1[2] };
        cs_est_insert(L, C, arr[i].code, fl, s, z, nn);
    }
    return L->err;
}
