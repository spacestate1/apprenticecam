/* nci_reverse.c - Toolpaths manager "Reverse toolpath" (command 3582): mccore bnci_reverse @1800d0120 and its core
 * rev_core @1800d0770, ported from the decompiled code described in ../formats/reverse_toolpath.md (cited [S n.n]; the
 * listing keys C / H / K / M / N / R are that spec's section 0). It works on the binary NCI of an operation stored in
 * the part (records of 0x370 bytes, ../formats/mcam.md "Binary NCI tail"). GetProdEnableBits() is taken as 0 (a
 * normal Mill licence): the bits 2 / 4 / 0x10 paths are NOT DETERMINED [S 6] and are not taken. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "platform.h"
#include "nci_reverse.h"

#define RS 0x370
#define REC(a, i) ((a) + (size_t)(i) * RS)
/* the global `tol` (system tolerance, config 1000; 5e-5 as cam2d_ops.c MC_TOL / mcamui) */
#define TOL (mc_systol.tol)              /* McSysTol (mcam_load.h): config 1000 */

static int16_t gh(const uint8_t *r, int o) { int16_t v; memcpy(&v, r + o, 2); return v; }
static void sh(uint8_t *r, int o, int16_t v) { memcpy(r + o, &v, 2); }
static int32_t gi(const uint8_t *r, int o) { int32_t v; memcpy(&v, r + o, 4); return v; }
static void si(uint8_t *r, int o, int32_t v) { memcpy(r + o, &v, 4); }
static double gd(const uint8_t *r, int o) { double v; memcpy(&v, r + o, 8); return v; }
static void sd(uint8_t *r, int o, double v) { memcpy(r + o, &v, 8); }
#define GC(r) gh(r, 0)

/* ---------------------------------------------------------------------------------------------- op type tests */
static int op_is_msurf(int c) {          /* mccore op_is_msurf @1800c62b0 */
    switch (c) { case 5: case 6: case 7: case 8: case 9: case 10: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: case 0x1c:
    case 0x1d: case 0x27: case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x30: case 0x6b:
    case 0x6d: case 0x6e: case 0x70: case 0x71: case 0x83: case 0x84: return 1; }
    return 0;
}
static int op_is_xform(int c) { return c == 4 || c == 0x50 || c == 0x6c || c == 0x9a; }   /* op_is_xform @1800c7700 */
static int op_is_multiax(int c) {        /* op_is_multiax @1800c6700 */
    switch (c) { case 0x1c: case 0x1d: case 0x2d: case 0x2e: case 0x30: case 0x31: case 0x6e: case 0x70: case 0x71: case 0x9a: return 1; }
    return 0;
}
/* operation offset -> OP_ID record offset (+0x68: record +0x6c = op +4 = opcode) */
static int ob(const Op *o, int off) { off += 0x68; return o->raw && (uint32_t)off < o->raw_n ? o->raw[off] : 0; }
static int oh(const Op *o, int off) { off += 0x68; return o->raw && (uint32_t)off + 2 <= o->raw_n ? gh(o->raw, off) : 0; }
static double od(const Op *o, int off) { off += 0x68; return o->raw && (uint32_t)off + 8 <= o->raw_n ? gd(o->raw, off) : 0; }
static int oi(const Op *o, int off) { off += 0x68; return o->raw && (uint32_t)off + 4 <= o->raw_n ? gi(o->raw, off) : 0; }
/* op_is_subpgm @1800c7020 (Ghidra DumpDecomp, see the spec section 1). Case 3 (Pocket) first tests a global,
   DAT_181378da2 = cur_op + 0x1392 of whatever operation was current before (NOT DETERMINED): taken as 0, so the
   operation's own fields decide. */
static int op_is_subpgm(const Op *o) {
    switch (o->opcode) {
    case 1: case 0x33: case 0x34: case 0x35: case 0x85:
        if ((uint16_t)oh(o, 0x12a0) > 3) return 0;
        return ob(o, 0x800) && ob(o, 0x828) && (!ob(o, 0x12a2) || od(o, 0x12a8) <= TOL);
    case 2: return ob(o, 0x28b) != 0;
    case 3: return !ob(o, 0x1220) && ob(o, 0x800) && !ob(o, 0x82a) && ob(o, 0x828);
    case 4: case 0x50: case 0x9a: return ob(o, 0x1229) && !ob(o, 0x1227) && (oh(o, 0x11c0) != 8 || ob(o, 0x12d1));
    case 0x12: case 0x66: case 0x69: case 0x86: return ob(o, 0x800) && ob(o, 0x828);
    case 0x4a: case 0x53: return ob(o, 0x1496) != 0;
    case 0x6c: return ob(o, 0x1375) && !ob(o, 0x1371);
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------------- record helpers [S 0, 2] */
static int is_transparent(int c) { unsigned u = (unsigned)(c - 1005); return u < 0x33 && (0x400000020000fULL >> u & 1); }   /* K:2074 */
static int is_motion(int c) {            /* K:2092 */
    switch (c) { case 0: case 1: case 2: case 3: case 11: case 20: case 21: case 22: case 23: case 30: case 31: case 32: case 33:
    case 81: case 82: case 83: case 100: return 1; }
    return 0;
}
static int is_marker(const uint8_t *r, int which) { return GC(r) == 0 && gd(r, 0x38) == (which ? -4.0 : -3.0); }   /* K:2124 */
/* bnci_get_pt [N:3-112] */
static int get_pt(const uint8_t *r, double p[3]) {
    int o;
    switch (GC(r)) {
    case 1002: o = 0x50; break;
    case 0: case 1: case 2: case 3: case 20: case 21: case 30: case 31: case 81: case 100: o = 0x20; break;
    case 11: case 83: case 1003: o = 0x18; break;
    case 22: case 23: case 32: case 33: p[0] = gd(r, 0x20); p[1] = gd(r, 0x28); p[2] = gd(r, 0x40); return 1;
    case 1013: o = 0x38; break;
    case 1016: o = 0x28; break;
    default: return 0;
    }
    p[0] = gd(r, o); p[1] = gd(r, o + 8); p[2] = gd(r, o + 16);
    return 1;
}
/* bnci_get_pt_ptr (FUN_180086810) [K:1917-1972]: offset of the writable point, -1 = NULL */
static int pt_off(const uint8_t *r) {
    switch (GC(r)) {
    case 0: case 1: case 2: case 3: case 81: case 100: return 0x20;
    case 11: case 1003: return 0x18;
    case 1002: return 0x50;
    case 1013: return 0x38;
    case 1016: return 0x28;
    }
    return -1;
}
/* bnci_find_line(nci, n, start, code, not_equal, backward) [N] */
static int find_line(const uint8_t *a, int n, int start, int code, int noteq, int back) {
    if (start < 0) return -1;
    if (!back) { for (int i = start; i < n; i++) if ((GC(REC(a, i)) == code) != noteq) return i; }
    else { for (int i = start < n ? start : n - 1; i >= 0; i--) if ((GC(REC(a, i)) == code) != noteq) return i; }
    return -1;
}
/* prev_rec (FUN_1801327c0) [K:2191-2284]: the nearest record before IDX (>= MIN) that is not transparent (and is
   motion when MOTION); -1 if none */
static int prev_rec(const uint8_t *a, int n, int idx, int min, int motion) {
    if (idx == 0) return -1;
    if (n < idx) idx = n;
    for (idx--; min <= idx; idx--) {
        int c = GC(REC(a, idx));
        if (!is_transparent(c) && (!motion || is_motion(c))) return idx;
        if (idx == 0) return -1;
    }
    return -1;
}
/* is_vert @18013a970 [H:495-583] */
static int is_vert(const uint8_t *a, int n, int idx, int *up, double *feed, int *valid) {
    *valid = 0; *up = 0; *feed = 0;
    if (n <= idx) return 0;
    const uint8_t *r = REC(a, idx);
    int pi = prev_rec(a, n, idx, 0, 0);
    if (pi < 0) return 0;
    *valid = 1;
    switch (GC(r)) {
    case 1: *feed = gd(r, 0x38); break;
    case 2: case 3: *feed = gd(r, 0x50); break;
    case 11: *feed = gd(r, 0x48); break;
    case 81: *feed = gd(r, 0x40); break;
    case 100: for (int i = idx; i >= 0; i--) if (GC(REC(a, i)) == 81) { *feed = gd(REC(a, i), 0x40); break; } break;
    default: *feed = -2.0;
    }
    int c = GC(r);
    if (idx != 0 && c >= 0) {
        if (c < 2) {
            double p[3], q[3];
            if (get_pt(r, p) && get_pt(REC(a, pi), q) && hypot(q[0] - p[0], q[1] - p[1]) < TOL) { *up = q[2] < p[2]; return 1; }
        } else if (c == 100) return 1;
    }
    return 0;
}
/* FUN_1800cf9c0: offset of the cc (cutter compensation) field, -1 = none [M:242-267] */
static int cc_off(const uint8_t *r) {
    int c = GC(r);
    if (c == 0 || c == 1) return 0x18;
    if (c == 2 || c == 3) return 0x1a;
    if (c == 1004) return 0;
    return -1;
}
/* is_pass_end (FUN_18013a800, cur_op) [M:367-411] */
static int is_pass_end(int c, const Op *cur) {
    if (c < 0x51) {
        if (c == 0x50) return 1;
        if (c < -7) return 0;
        if (c < -5) {                                   /* -7, -6 */
            if (op_is_msurf(cur->opcode)) return 0;
            if (op_is_xform(cur->opcode)) return !(oi(cur, 0x88) > 1);
            return 1;
        }
        return c > -4 ? 0 : 1;                          /* -5, -4 */
    }
    if (((unsigned)(c - 0x3eb) & 0xfffffff6u) != 0) return 0;   /* 1003, 1004, 1011, 1012 ... */
    return c != 0x3f4;                                  /* ... but not 1012 */
}
/* bnci_toggle_comp (FUN_18008d4b0) [K:2318-2363]: 41 <-> 42 */
static void bnci_toggle_comp(uint8_t *r) {
    int c = GC(r), o;
    if (c == 0 || c == 1 || c == 1013) o = 0x18;
    else if (c == 2 || c == 3) o = 0x1a;
    else if (c == 1016) o = 0x40;
    else return;
    int v = gh(r, o);
    if (v == 41) sh(r, o, 42); else if (v == 42) sh(r, o, 41);
}

/* ---------------------------------------------------------------------------------------------- passes [S 3.3] */
typedef struct { int s, e, f8, fc, opid; } Pass;
/* find_passes @1800d2130 [C:1189-1341]; the "same as previous pass" flag (FUN_1800cfe30, +0x14) is never read by
   rev_core and is not computed */
static Pass *find_passes(const uint8_t *a, int n, const Op *cur, int *np) {
    int cnt = 0;
    for (int i = 0; i < n; i++) if ((unsigned)(GC(REC(a, i)) - 1000) < 3) cnt++;   /* FUN_1800cfdd0 */
    *np = cnt;
    if (!cnt) return NULL;
    Pass *P = calloc((size_t)cnt, sizeof(Pass));
    int k = 0;
    for (int i = 0; i < n; i++) {
        int c = GC(REC(a, i));
        if (c > 999) {
            if (c < 1003) {
                int j = i;
                while (j < n && !(is_motion(GC(REC(a, j))) && !is_marker(REC(a, j), 0))) j++;
                i = j;
                if (j < n) { P[k++].s = j; if (cnt <= k) i = n + 1; }
            } else if (c == 1016 && k < cnt) {
                const uint8_t *r = REC(a, i);
                if (!P[k].f8) P[k].f8 = gi(r, 0x04);
                if (!P[k].fc) P[k].fc = gi(r, 0x60);
                if (!P[k].opid) P[k].opid = gi(r, 0x64);
            }
        }
    }
    for (int p = 0; p < cnt; p++) {
        int s = P[p].s, j = s;
        if (s == 0 || j >= n) continue;
        while (!is_pass_end(GC(REC(a, j)), cur)) {
            if (is_marker(REC(a, j), 1)) break;
            if (++j >= n) break;
        }
        do { j--; if (j >= 0 && is_motion(GC(REC(a, j)))) { P[p].e = j; break; } } while (s <= j);
    }
    return P;
}
/* pass_max_z @1800d2470 [H:416-494] */
static double *pass_max_z(const uint8_t *a, int n, const Pass *P, int np) {
    if (np <= 0) return NULL;
    double *z = calloc((size_t)np, sizeof(double));
    for (int p = 0; p < np; p++) {
        z[p] = -1e38;
        int end = p < np - 1 ? P[p + 1].s : n;
        for (int i = P[p].s; i <= end - 1; i++) {
            int up, valid; double f, q[3];
            if (is_vert(a, n, i, &up, &f, &valid) && up && GC(REC(a, i)) == 0 && get_pt(REC(a, i), q) && q[2] > z[p]) z[p] = q[2];
        }
    }
    return z;
}
/* comp_pairs @1800cfa20 with swap = 1, counting in FUN_1800cfc10 [H:584-663, M:143-240]; 0 = unbalanced (abort) */
static int comp_pairs_swap(uint8_t *a, int n) {
    int inside = 0, nend = 0, nstart = 0;
    for (int i = 0; i < n; i++) {
        int o = cc_off(REC(a, i));
        if (o < 0) continue;
        int v = gh(REC(a, i), o), isend = 0;
        if (v == 140) isend = 1;
        else if (i < 1 || v != 1004) {
            if (v == 41 || v == 42) { nstart++; inside = 1; }
            continue;
        } else { int o2 = cc_off(REC(a, i - 1)); if (o2 >= 0 && gh(REC(a, i - 1), o2) != 140) isend = 1; }
        if (isend) nend++;
        inside = 0;
    }
    (void)inside;
    if (nstart != nend) return 0;
    int cnt = nstart;
    if (cnt < 1) return 1;
    int (*pr)[2] = calloc((size_t)cnt, sizeof *pr), k = 0;
    for (int i = 0; i < n && k < cnt; i++) {
        int o = cc_off(REC(a, i));
        if (o < 0) continue;
        int v = gh(REC(a, i), o);
        if (v == 140) pr[k++][1] = i;
        else if (v == 1004) { int o2 = i > 0 ? cc_off(REC(a, i - 1)) : -1; if (o2 >= 0 && gh(REC(a, i - 1), o2) != 140) pr[k++][1] = i; }
        else if (v == 41 || v == 42) pr[k][0] = i;
    }
    for (k = 0; k < cnt; k++) {
        uint8_t *s = REC(a, pr[k][0]), *e = REC(a, pr[k][1]); int os = cc_off(s), oe = cc_off(e);
        if (os >= 0 && oe >= 0 && gh(s, os) != 1004 && gh(e, oe) != 1004) { int t = gh(s, os); sh(s, os, gh(e, oe)); sh(e, oe, (int16_t)t); }
    }
    free(pr);
    return 1;
}

/* ---------------------------------------------------------------------------------------------- blocks [S 3.4] */
/* tail_xy @1800cf1b0 [H:1-157]: the run of non-vertical rapids that ends the pass at the tool change's first XY.
   Returns a copy (*outn records, from index *outidx) or NULL. Mastercam loops forever when the record at the scan
   position is transparent (0x1800cf3ac..0x1800cf3b7 jump back without a decrement); here the scan stops there. */
static uint8_t *tail_xy(const uint8_t *a, int n, int idx, int *outn, int *outidx) {
    *outn = 0; *outidx = -1;
    int t = find_line(a, n, idx, 1002, 0, 1);
    double ref[3], p[3];
    if (t < 0 || !get_pt(REC(a, t), ref)) return NULL;
    int l = find_line(a, n, idx, 1011, 0, 0);
    if (idx < l) idx = l - 1;
    for (;;) {
        if (idx < 0) break;
        const uint8_t *r = REC(a, idx);
        if (GC(r) != 0) break;                          /* a transparent record here: Mastercam's endless loop */
        int up, valid; double f;
        if (is_vert(a, n, idx, &up, &f, &valid)) break;
        int ok = get_pt(r, p);
        ref[2] = 0; p[2] = 0;
        if (!ok || hypot(hypot(ref[0] - p[0], ref[1] - p[1]), ref[2] - p[2]) > TOL) break;
        int pi = prev_rec(a, n, idx, 0, 0);
        if (pi < 0 || !get_pt(REC(a, pi), ref)) break;
        (*outn)++; idx--;
    }
    if (!*outn) return NULL;
    uint8_t *o = malloc((size_t)*outn * RS);
    memcpy(o, REC(a, idx + 1), (size_t)*outn * RS);
    *outidx = idx + 1;
    return o;
}
/* get_block @1800cf500 [H:160-406]: the approach (forward from FROM) or retract (backward from FROM down to TO) stack
   of vertical G0 / G1 moves. *outn may be > 0 with NULL returned (is_vert valid = 0), as in Mastercam. */
static uint8_t *get_block(const uint8_t *a, int n, int from, int to, int backward, int *outn, int *outidx, double *feed) {
    *outn = 0; *outidx = -1; *feed = -2.0;
    uint8_t *mem = NULL;
    if (!backward) {
        int t = find_line(a, n, from, 1002, 0, 1);
        if (t < 0) return NULL;
        int idx = t + 1, marker = 0;
        while (idx < n && (is_marker(REC(a, idx), 0) || is_transparent(GC(REC(a, idx))))) { if (is_marker(REC(a, idx), 0)) marker = 1; idx++; }
        int first = idx;
        for (; idx <= to && idx < n; idx++) {
            const uint8_t *r = REC(a, idx); int c = GC(r);
            if (!is_transparent(c) && (uint16_t)c > 1) break;
            if (!is_transparent(c) && !marker) {
                int up, valid; double f;
                if (!is_vert(a, n, idx, &up, &f, &valid)) break;
                if (!valid) { free(mem); return NULL; }
            }
            (*outn)++; marker = 0;
            mem = realloc(mem, (size_t)*outn * RS); memcpy(REC(mem, *outn - 1), r, RS);
            if (c == 1) *feed = gd(r, 0x38);
        }
        if (*outn < 1) return NULL;
        *outidx = first;
        return mem;
    }
    int idx = find_line(a, n, from, 1011, 0, 0);
    if (from < idx || idx < 0) idx = from;
    for (; to <= idx && idx >= 0; idx--) {
        const uint8_t *r = REC(a, idx); int c = GC(r);
        if (!is_transparent(c) && (uint16_t)c > 1) break;
        if (!is_transparent(c) && !is_marker(r, 1)) {
            int up, valid; double f;
            if (!is_vert(a, n, idx, &up, &f, &valid)) break;
            if (!valid) return NULL;
        }
        (*outn)++;
    }
    if (!*outn) return NULL;
    mem = malloc((size_t)*outn * RS);
    memcpy(mem, REC(a, idx + 1), (size_t)*outn * RS);
    for (int k = 0; k < *outn; k++) if (GC(REC(mem, k)) == 1) *feed = gd(REC(mem, k), 0x38);
    *outidx = idx + 1;
    return mem;
}
/* clearance_z (FUN_1800d2650) [K:2143-2174]; LP = OP_ID record + 0x160 (op + 0xf8) */
static double clearance_z(const uint8_t *lp, double a, double b, double z) {
    if (lp[0x19] && !lp[0x18] && fabs(z - gd(lp, 0x10)) < TOL) return z <= a ? a : z;
    if ((!lp[0x29] || lp[0x28] || TOL <= fabs(z - gd(lp, 0x20))) && (lp[0x38] || TOL <= fabs(z - gd(lp, 0x30)))
        && (lp[0x190] || TOL <= fabs(z - gd(lp, 0x188)))) {
        double v = z - b + a;
        return a <= v ? v : a;
    }
    return z <= a ? a : z;
}
static void max_z(double a, double *z) { if (*z <= a && a != *z) *z = a; }   /* FUN_1800cf1a0 */

/* ---------------------------------------------------------------------------------------------- final rapid heights [S 3.5] */
/* adjust_z_in_bnci_rapid_line (ins_fn @18012f510) [C:1556-1931]: the index of an inserted record, else -1 */
static int ins_fn(uint8_t **pa, int *pn, int idx, double v) {
    uint8_t *a = *pa; int n = *pn;
    if (n >= 0x7fff || idx >= 0x7fff || idx > n - 1) return -1;
    uint8_t *r = REC(a, idx);
    if ((uint16_t)GC(r) >= 2) return -1;
    uint8_t *nx = NULL;
    if (idx < n - 1) { nx = REC(a, idx + 1); if ((uint16_t)GC(nx) > 1 && GC(nx) != 1011) return -1; }
    int po = pt_off(r);
    if (po < 0) return -1;
    double z = gd(r, po + 16);
    if (!(z < v)) return -1;
    int ret = -1;
    if (GC(r) == 1 && nx && (GC(nx) == 0 || GC(nx) == 1011)) {
        uint8_t ins[RS]; memcpy(ins, r, RS);
        si(ins, 0x10, -1); sh(ins, 0, 0); sd(ins, 0x38, -2.0); sd(ins, 0x30, v);
        int no = pt_off(nx); double d;
        if (no < 0) return -1;                          /* _MCAssert in Mastercam */
        if (GC(nx) == 1011) d = TOL * 2.0;
        else d = sqrt(pow(gd(ins, 0x28) - gd(nx, no + 8), 2) + pow(gd(ins, 0x20) - gd(nx, no), 2) + pow(v - gd(nx, no + 16), 2));
        if (TOL < d) {
            a = realloc(a, (size_t)(n + 1) * RS);
            memmove(REC(a, idx + 2), REC(a, idx + 1), (size_t)(n - idx - 1) * RS);
            memcpy(REC(a, idx + 1), ins, RS);
            *pa = a; *pn = n + 1; r = REC(a, idx);
            ret = idx + 1;
        }
    }
    if (GC(r) == 0) sd(r, po + 16, v);                  /* a rapid is raised; a G1 keeps its Z */
    return ret;
}
/* post_fix @1800cefa0 [C:1456-1555] */
static void post_fix(uint8_t **pa, int *pn, const double *mz, int n_pass, const Op *cur) {
    int np; Pass *P = find_passes(*pa, *pn, cur, &np);
    if (!P) return;
    if (np != n_pass) { free(P); return; }
    for (int p = 0; p < np; p++) {
        int e = P[p].e;
        for (int i = P[p].s; i <= e; i++) {
            int before = *pn, at = ins_fn(pa, pn, i, mz[p]), grew = *pn - before;
            if (at < 0 || grew <= 0) continue;
            for (int k = p; k < np; k++)
                if (P[k].s <= at && at <= P[k].e) {
                    i += grew; P[p].e += grew; e += grew;
                    for (int q = p + 1; q < np; q++) { P[q].s += grew; P[q].e += grew; }
                    break;
                }
        }
    }
    for (int p = 0; p < np; p++) {
        int end = p == np - 1 ? *pn - 1 : P[p + 1].s;
        for (int i = P[p].e + 1; i <= end; i++) ins_fn(pa, pn, i, mz[p]);
    }
    free(P);
}

/* ---------------------------------------------------------------------------------------------- rev_core [S 3] */
static const Op *op_by_id(const Op *ops, int nops, int id) {   /* DatabaseRetrieve */
    for (int i = 0; i < nops; i++) if (ops[i].id == id && ops[i].raw && ops[i].raw_n >= 0x160 + 0x198) return &ops[i];
    return NULL;
}
static double xyd(const double *p, const double *q) { return hypot(p[0] - q[0], p[1] - q[1]); }
#define FAIL(...) do { snprintf(why, whyn, __VA_ARGS__); goto done; } while (0)

int nci_rev_core(uint8_t **pnci, int *pcount, const Op *ops, int nops, const Op *cur, int metric, int toggle_comp,
                 int use_max_z, char *why, size_t whyn) {
    int ok = 0, n = *pcount;
    snprintf(why, whyn, "-");
    if (!*pnci || !n) { snprintf(why, whyn, "the operation has no NCI"); return 0; }
    uint8_t *dst = *pnci, *src = malloc((size_t)n * RS);
    memcpy(src, dst, (size_t)n * RS);                   /* [C:147-152] */
    Pass *P = NULL; double *mz = NULL; int np = 0;
    uint8_t *blkA = NULL, *blkR = NULL, *tail = NULL;
    /* [S 3.1] the tool view / origin (1014 / 1013) only matter for arcs off the XY plane (see the body loop) */
    double REF[3] = { 0, 0, 1e38 };                     /* [C:175-189] */
    int ti = find_line(src, n, 0, 1002, 0, 0);
    if (ti >= 0 && get_pt(REC(src, ti), REF))
        for (ti++; ti < n && GC(REC(src, ti)) == 0; ti++) if (!get_pt(REC(src, ti), REF)) break;
    /* op +0x318 / +0x320 (plunge / retract rate) are read but get_block overwrites both before use [S 3.1] */
    double feedA, feedR;
    if (toggle_comp) for (int i = 0; i < n; i++) bnci_toggle_comp(REC(src, i));   /* [S 3.2.1] */
    for (int i = 0; i < n; i++) {                       /* [S 3.2.2] 1040 <-> 1041 (meaning NOT DETERMINED) */
        int c = GC(REC(src, i)); if (c == 1040) sh(REC(src, i), 0, 1041); else if (c == 1041) sh(REC(src, i), 0, 1040);
    }
    P = find_passes(src, n, cur, &np);                  /* [S 3.3] */
    if (!P) FAIL("the NCI has no tool change record");
    mz = pass_max_z(src, n, P, np);                     /* [S 3.2.4] */
    if (!comp_pairs_swap(src, n)) FAIL("unbalanced cutter compensation start / end codes");   /* [S 3.2.3, 2] */
    int first_g0 = 1;                                   /* local_2f08: set once, not per pass [C:141, 698-707] */
    for (int p = 0; p < np; p++) {
        int S = P[p].s, E = P[p].e;
        const Op *lpop = op_by_id(ops, nops, P[p].opid);   /* [S 3.4.a] */
        const uint8_t *LP = lpop ? lpop->raw + 0x160 : NULL;
        int nT = 0, tIdx = -1;                          /* [S 3.4.b] */
        free(tail); tail = tail_xy(src, n, E, &nT, &tIdx);
        if (tail) E = tIdx - 1;
        double A0[3], B0[3];                            /* [S 3.4.c] */
        if (E <= S || !get_pt(REC(src, S), A0) || !get_pt(REC(src, E), B0)) FAIL("pass %d: no start / end point", p + 1);
        double sv100[3] = { 0, 0, 0 }; int have100 = 0;
        if (GC(REC(src, E)) == 100) { sv100[0] = gd(REC(dst, E), 0x58); sv100[1] = gd(REC(dst, E), 0x60); sv100[2] = gd(REC(dst, E), 0x68); have100 = 1; }
        int nA, outA, nR, outR;                         /* [S 3.4.d] */
        free(blkA); blkA = get_block(src, n, S, E, 0, &nA, &outA, &feedA);
        free(blkR); blkR = get_block(src, n, E, nA + S, 1, &nR, &outR, &feedR);
        int ia = S + (nA - 1 < 0 ? 0 : nA - 1), ir = E - nR, oa, orr;
        if (ia < 0 || ia >= n || ir < 0 || ir >= n || (oa = pt_off(REC(dst, ia))) < 0 || (orr = pt_off(REC(dst, ir))) < 0)
            FAIL("pass %d: approach / retract without a point", p + 1);   /* a NULL dereference in Mastercam */
        double zA = gd(REC(dst, ia), oa + 16), zB = gd(REC(dst, ir), orr + 16);
        int body0 = S, bodyE = E;
        if (blkA) {                                     /* [S 3.4.e] approach: stays first, moved to the new start XY */
            for (int k = 0; k < nA; k++) {
                uint8_t *r = REC(blkA, k); int po;
                if (!is_transparent(GC(r)) && !is_marker(r, 0) && (po = pt_off(r)) >= 0) {
                    double q[3] = { gd(r, po), gd(r, po + 8), gd(r, po + 16) };
                    if (xyd(B0, q) < TOL) { sd(r, po, A0[0]); sd(r, po + 8, A0[1]); }
                    else if (xyd(A0, q) < TOL) { sd(r, po, B0[0]); sd(r, po + 8, B0[1]); }
                    double z = q[2];
                    if (LP) z = clearance_z(LP, zB, zA, z); else if (use_max_z) max_z(zB, &z);
                    sd(r, po + 16, z);
                }
                memcpy(REC(dst, outA + k), r, RS);
                body0++;
            }
        }
        if (blkR) {                                     /* retract: stays last, at the new end XY */
            for (int k = 0; k < nR; k++) {
                uint8_t *r = REC(blkR, k); int po;
                if (!is_transparent(GC(r)) && !is_marker(r, 1) && (po = pt_off(r)) >= 0) {
                    double q[3] = { gd(r, po), gd(r, po + 8), gd(r, po + 16) };
                    if (xyd(B0, q) < TOL) { sd(r, po, A0[0]); sd(r, po + 8, A0[1]); }
                    else if (xyd(A0, q) < TOL) { sd(r, po, B0[0]); sd(r, po + 8, B0[1]); }
                    double z = q[2];
                    if (LP) z = clearance_z(LP, zA, zB, z); else if (use_max_z) max_z(zA, &z);
                    sd(r, po + 16, z);
                }
                memcpy(REC(dst, outR + k), r, RS);
                bodyE--;
            }
        }
        int T = find_line(dst, n, body0, 1002, 0, 1);   /* [S 3.4.f] */
        if (T < 0) FAIL("pass %d: no tool change before the cut", p + 1);
        sd(REC(dst, T), 0x50, B0[0]); sd(REC(dst, T), 0x58, B0[1]);
        int i81 = find_line(dst, n, T, 81, 0, 0);       /* [S 4] */
        int drill = !(i81 < 0 || bodyE < i81);
        if (drill) {
            free(tail); tail = NULL; nT = 0;
            if (i81 != bodyE) {
                uint8_t *s81 = REC(src, i81), *d81 = REC(dst, i81), *sE = REC(src, bodyE);
                double k88 = gd(d81, 0x88), k90 = gd(d81, 0x90), k98 = gd(d81, 0x98);
                sd(s81, 0x20, B0[0]); sd(s81, 0x28, B0[1]);
                /* only +0x88 / +0x90 are written to the 81 (movups, dis 0x1800d131a) - from the last 100. When the pass
                   end is not a 100 these are uninitialised stack in Mastercam (NOT DETERMINED): left as they are */
                if (have100) { sd(s81, 0x88, sv100[0]); sd(s81, 0x90, sv100[1]); }
                memcpy(d81, s81, RS);
                body0 = i81 + 1;
                sd(sE, 0x20, A0[0]); sd(sE, 0x28, A0[1]);
                sd(sE, 0x58, k88); sd(sE, 0x60, k90); sd(sE, 0x68, k98);
                memcpy(REC(dst, bodyE), sE, RS);
                bodyE--;
            }
        }
        /* [S 3.4.g] body: reverse index order; each move ends where it used to start */
        for (int i = bodyE, o = body0; body0 <= i; i--, o++) {
            uint8_t *r = REC(src, i); int c = GC(r);
            if (is_motion(c) && !drill && !is_transparent(c)) {
                if (c == 0) { if (first_g0) { first_g0 = 0; if (find_line(src, n, i, 0, 1, 1) < 0) FAIL("pass %d: a rapid with nothing before it", p + 1); } }
                else first_g0 = 1;
                if (c == 2 || c == 3) sh(r, 0, c == 2 ? 3 : 2);
                int pi = prev_rec(src, n, i, T, 1); double q[3]; int po;
                if (pi < 0 || !get_pt(REC(src, pi), q) || (po = pt_off(r)) < 0) FAIL("pass %d: a move without a previous point", p + 1);
                int pc = GC(REC(src, pi));
                if (((pc == 2 || pc == 3) && gh(REC(src, pi), 0x18) != 0) || ((GC(r) == 2 || GC(r) == 3) && gh(r, 0x18) != 0))
                    /* plane_to_view / view_to_world / world_to_view with the tool view and origin [C:722-769]: NOT DECODED
                       here; Mastercam's result is not known, so the operation is left unchanged */
                    FAIL("pass %d: an arc outside the XY plane (its view transform is not decoded)", p + 1);
                sd(r, po, q[0]); sd(r, po + 8, q[1]); sd(r, po + 16, q[2]);
            }
            memcpy(REC(dst, o), r, RS);
        }
        if (!drill && tail) {                           /* [S 3.4.h] tail: reverse order, the old first one at B0 */
            for (int k = nT, o = tIdx; k > 0; k--, o++) {
                uint8_t *r = REC(tail, k - 1); double q[3];
                if (k == 1) memcpy(q, B0, sizeof q);
                else {                                  /* prev_rec(tail, k): the record itself (index k - 1) */
                    int pi = prev_rec(tail, nT, k, 0, 0);
                    if (pi < 0 || !get_pt(REC(tail, pi), q)) FAIL("pass %d: a tail move without a point", p + 1);
                }
                int po = pt_off(r);
                if (po >= 0) { sd(r, po, q[0]); sd(r, po + 8, q[1]); sd(r, po + 16, q[2]); }
                memcpy(REC(dst, o), r, RS);
            }
        }
        int Ep = P[p].e;
        for (int i = S; i <= Ep; i++) {                 /* [S 3.4.i] entry / exit flags 1 <-> 2, -4 <-> -5, -6 <-> -7 */
            uint8_t *r = REC(dst, i); int c = GC(r), fo = c == 0 || c == 1 ? 0x40 : c == 2 || c == 3 ? 0x58 : -1;
            if (fo >= 0) { int v = gh(r, fo); if (v == 1 || v == 2) sh(r, fo, v == 2 ? 1 : 2); }
            else if (c == -7) sh(r, 0, -6); else if (c == -6) sh(r, 0, -7); else if (c == -5) sh(r, 0, -4); else if (c == -4) sh(r, 0, -5);
        }
        if (feedA <= TOL) feedA = metric == 1 ? 2.5 : 0.1;   /* [S 3.4.j] IsMetric() */
        for (int i = S; i <= Ep; i++) {
            uint8_t *r = REC(dst, i); int up, valid; double f;
            if (GC(r) == 0 && is_vert(dst, n, i, &up, &f, &valid) && valid && !up && f <= TOL && gd(r, 0x30) < REF[2]) { sh(r, 0, 1); sd(r, 0x38, feedA); }
        }
        if (feedR <= TOL)                               /* [S 3.4.k] */
            for (int i = S; i <= Ep; i++) {
                uint8_t *r = REC(dst, i); int up, valid; double f;
                if (GC(r) == 1 && is_vert(dst, n, i, &up, &f, &valid) && valid && up && 0.0 <= f) {
                    int oc = cur->opcode;
                    if ((oc == 1 || oc == 0x85 || oc == 3) && !ob(cur, 0x141)) sd(r, 0x38, feedR);
                    else { sh(r, 0, 0); sd(r, 0x38, -2.0); }
                }
            }
        {   int end = p == np - 1 ? n : P[p + 1].s;     /* [S 3.4.l] links after the pass */
            for (int i = Ep + 1; i < end; i++) {
                uint8_t *r = REC(dst, i); double q[3]; int po;
                if ((uint16_t)GC(r) >= 2) continue;
                r[0xaa] = 0;
                if (!get_pt(r, q) || (po = pt_off(r)) < 0) continue;
                if (TOL <= xyd(q, B0)) { if (TOL <= xyd(q, A0)) r[0xaa] = 1; else { sd(r, po, B0[0]); sd(r, po + 8, B0[1]); } }
                else { sd(r, po, A0[0]); sd(r, po + 8, A0[1]); }
            }
        }
        {   uint8_t *r = REC(dst, S); double q[3];      /* [S 3.4.m] */
            if (GC(r) == 1 && gh(r, 0x40) == 0 && REF[2] <= gd(r, 0x30)) {
                int pi = prev_rec(dst, n, S, S - 1, 0);
                if (pi >= 0 && GC(REC(dst, pi)) == 1002 && get_pt(REC(dst, pi), q)
                    && sqrt(pow(gd(r, 0x28) - q[1], 2) + pow(gd(r, 0x20) - q[0], 2) + pow(gd(r, 0x30) - q[2], 2)) < TOL) { sh(r, 0, 0); sd(r, 0x38, -2.0); }
            }
        }
        if (p > 0)                                      /* [S 3.4.n] flagged links before the pass */
            for (int i = S - 1; i > P[p - 1].e; i--) {
                uint8_t *r = REC(dst, i); double q[3]; int po;
                if ((uint16_t)GC(r) >= 2 || !r[0xaa]) continue;
                if (!get_pt(r, q) || (po = pt_off(r)) < 0) continue;
                if (TOL <= xyd(q, B0)) { if (TOL <= xyd(q, A0)) continue; r[0xaa] = 0; sd(r, po, B0[0]); sd(r, po + 8, B0[1]); }
                else { r[0xaa] = 0; sd(r, po, A0[0]); sd(r, po + 8, A0[1]); }
            }
    }
    if (mz) post_fix(pnci, pcount, mz, np, cur);       /* [S 3.5] */
    ok = 1;
done:
    free(src); free(P); free(mz); free(blkA); free(blkR); free(tail);
    return ok;
}

/* ---------------------------------------------------------------------------------------------- bnci_reverse [S 1] */
/* the op's section in the stored NCI (or its rewritten copy): byte range of the -2 record .. the -3 record */
static int find_section(const uint8_t *d, size_t len, size_t off, int id, size_t *s0, size_t *s1) {
    int in = 0;
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        int16_t g = sz >= 2 ? gh(d + off + 2, 0) : 0;
        if (g == -2 && sz >= 0x20) { in = gi(d + off + 2, 0x1c) == id; if (in) *s0 = off; }
        if (in && g == -3) { *s1 = off + 2 + sz; return 1; }
        off += 2 + sz;
    }
    return 0;
}

int nci_reverse_op(McamFile *f, int index, char *msg, size_t msgn) {
    msg[0] = 0;
    if (index < 0 || index >= f->nops) { snprintf(msg, msgn, "no such operation"); return 0; }
    Op *o = &f->ops[index];
    if (!o->raw) { snprintf(msg, msgn, "operation %d is not stored in the part", index + 1); return 0; }
    if (op_is_multiax(o->opcode)) return -1;            /* [R:198-201] silently */
    if (op_is_subpgm(o)) { snprintf(msg, msgn, "%s", NCI_REV_SUBPGM_MSG); return 0; }   /* [R:202-207] post_warning 0xb5f */
    /* bnci_read_section(id, ..., p5 = 1): the records between the section start (-2) and end (-3) */
    uint8_t *d = NULL; size_t len = 0, s0 = 0, s1 = 0;
    if (o->nci) { d = malloc(o->nci_n); memcpy(d, o->nci, o->nci_n); len = o->nci_n; }
    else {
        struct stat st;
        if (!f->path[0] || !f->nci_off || stat(f->path, &st) != 0 || (long long)st.st_size != f->file_size || (long long)st.st_mtime != f->file_mtime) {
            snprintf(msg, msgn, "the part file changed on disk since it was opened (or has no NCI) - open it again"); return 0; }
        FILE *in = fopen(f->path, "rb");
        if (in) { fseek(in, 0, SEEK_END); long L = ftell(in); fseek(in, 0, SEEK_SET);
            if (L > 0) { len = (size_t)L; d = malloc(len); if (fread(d, 1, len, in) != len) { free(d); d = NULL; } } fclose(in); }
        if (!d) { snprintf(msg, msgn, "cannot read %s", f->path); return 0; }
    }
    if (!find_section(d, len, o->nci ? 0 : f->nci_off, o->id, &s0, &s1)) { free(d); snprintf(msg, msgn, "operation %d has no NCI in the part", index + 1); return 0; }
    size_t p = s0 + 2 + (d[s0] | d[s0 + 1] << 8), span = 0;   /* after the -2 record */
    int n = 0, cap = 64; uint8_t *a = malloc((size_t)cap * RS);
    int16_t szof[64]; int nszof = 0; uint16_t szval[64];       /* stored size per gcode (fallback for the byte count) */
    size_t eos = p;
    while (p + 2 <= s1) {
        uint16_t sz = (uint16_t)(d[p] | d[p + 1] << 8);
        int16_t g = gh(d + p + 2, 0);
        if (g == -3) { eos = p; break; }
        if (g != 1018 && g != 1019) {                   /* p5 = 1: subprogram records are not loaded [N:276-279] */
            if (n == cap) { cap *= 2; a = realloc(a, (size_t)cap * RS); }
            memset(REC(a, n), 0, RS); memcpy(REC(a, n), d + p + 2, sz < RS ? sz : RS); n++;   /* zero-filled, as bnci_read_from_disk */
            int k; for (k = 0; k < nszof && szof[k] != g; k++) ;
            if (k == nszof && nszof < 64) { szof[nszof] = g; szval[nszof++] = sz; }
        }
        p += 2 + sz;
    }
    span = eos - (s0 + 2 + (d[s0] | d[s0 + 1] << 8));
    char why[200];
    if (!nci_rev_core(&a, &n, f->ops, f->nops, o, mcam_is_metric(f), 1, 0, why, sizeof why)) {   /* [R:282] useMaxZ = 0 */
        free(a); free(d); snprintf(msg, msgn, "operation %d not reversed: %s", index + 1, why); return 0; }   /* [S 5] unchanged */
    /* bnci_rewrite_section (no fragments, 1018/1019 count 0) [N]: written in place from the section start while each
       record (+0x14 byte count) still ends before the old end-of-section; records that do not fit are lost. Each
       record gets +0x04 = the op id. */
    size_t hdr = s0 + 2 + (d[s0] | d[s0 + 1] << 8) - s0, tl = s1 - eos;
    uint8_t *out = malloc(hdr + span + tl + 16); size_t on = 0, used = 0; int dropped = 0;
    memcpy(out, d + s0, hdr); on = hdr;
    for (int i = 0; i < n; i++) {
        uint8_t *r = REC(a, i); int g = GC(r), bc = gi(r, 0x14);
        if (bc <= 2 || bc > 0x6e2) { int k; for (k = 0; k < nszof && szof[k] != g; k++) ; bc = k < nszof ? szval[k] + 2 : 0; }
        if (bc <= 2) { free(a); free(d); free(out); snprintf(msg, msgn, "operation %d not reversed: record size of code %d unknown", index + 1, g); return 0; }
        if (span < used + (size_t)bc) { dropped = n - i; break; }
        si(r, 0x04, o->id);
        out[on] = (uint8_t)(bc - 2); out[on + 1] = (uint8_t)((bc - 2) >> 8); memcpy(out + on + 2, r, (size_t)(bc - 2) < RS ? (size_t)(bc - 2) : RS);
        if ((size_t)(bc - 2) > RS) memset(out + on + 2 + RS, 0, (size_t)(bc - 2) - RS);
        on += (size_t)bc; used += (size_t)bc;
    }
    memcpy(out + on, d + eos, tl); on += tl;
    free(a); free(d);
    mcam_op_set_nci(f, index, out, on);
    /* [R:294-306] markEdited: op +0xe0 = 1 (15095 "Binary NCI of operation has been edited"); p5: op +0x94 = 0 (15086
       "NCI marked for regeneration (dirty)"). mark_op_dependents (operations that use this one) is not modelled here. */
    if (o->raw_n > 0x148) o->raw[0x148] = 1;
    if (o->raw_n > 0xfc) o->raw[0xfc] = 0;
    o->nrev++;
    if (dropped) snprintf(msg, msgn, "operation %d: %d NCI record(s) did not fit the section and were dropped, as Mastercam's in-place rewrite does", index + 1, dropped);
    return 1;
}
