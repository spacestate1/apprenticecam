/* bnci_update - port of Mastercam 2017's BnciUpdate (mccore.dll): an operation whose dialog changed only "patchable" values (feed /
 * plunge / retract rates, spindle speed, coolant, D / H offsets, program and sequence numbers) keeps its toolpath; the records of
 * its NCI section are patched in place (user decision D2, formats/opdlg_port_audit.md 19, formats/bnci_update.md).
 *
 * Sources (Mastercam 2017 x64 VAs): decomp/dirty/core_bnciupd.c (Ghidra 2026-10-24) BnciUpdate::Update @180a5f4b0 / UpdatePossible /
 * DontAllow @180a5c530 / SetChangeFlags @180a5f210 / SetGcodeMask / ProcessNci @180a5e540 / Get1016Limit / GetFeedData @180a5ce80 /
 * ChangeFeed @180a59310 / ChangeFeedTraverse / ChangeFeedPlunge / ChangeFeedRetract / CheckMinFeedRate / GetRpmData @180a5e290 /
 * ChangeRpmOpToolInfo / ChangeDiaOffset / ChangeLenOffset / ChangeCoolant / ChangeToolNo / ChangeProgNo / ChangeSeqNo / ChangeSeqInc /
 * ChangeStationNo; decomp/ncidrill/core_brw.c, core_brw2.c (Ghidra 2026-10-26) CBnciReadWrite::FindGcode @1800f6190, IsZmove
 * @1800f7b80, GetValidPreviousMove @1800f7370 (FUN_18013a8a0 / FUN_18013a8d0), bnci_get_pt @1800865c0, RewriteLine @1800f9620.
 * Line numbers "B l." are core_bnciupd.c's. CalcMode is 1 in every BnciUpdate construction (bnci_update, the tool edit, Edit
 * Common): "replace a value equal to the old one" - the ratio paths (mode 0) are not ported.
 * "ours" marks glue; NOT PORTED cases are refused (the caller marks the op dirty, as before). */
#include "bnci_update.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TOL 5e-5                                                  /* tol_exref (config 1000 default) */
#define OPN 0x28d0                                                /* sizeof(operation) (UpdatePossible's copies) */

static int16_t I16(const uint8_t *b, size_t o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static int32_t I32(const uint8_t *b, size_t o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static double D(const uint8_t *b, size_t o) { double v; memcpy(&v, b + o, 8); return v; }
static void W16(uint8_t *b, size_t o, int16_t v) { memcpy(b + o, &v, 2); }
static void W32(uint8_t *b, size_t o, int32_t v) { memcpy(b + o, &v, 4); }
static void WD(uint8_t *b, size_t o, double v) { memcpy(b + o, &v, 8); }

/* ------------------------------------------------------------------------------------------------ the filtered record list */
typedef struct { uint8_t *r; size_t n; } Rec;                    /* a record image in the section (n = the bytes stored) */
typedef struct {
    Rec *rec; long n;
    const uint8_t *O, *N; size_t on, nn;                          /* old / new operation images (this+0x5518 / this+0x2c48) */
    int opcode;
    /* SetChangeFlags (B l.6651-7322): exact compares */
    int c_ret, c_plg, c_feed, c_tool, c_tid, c_dia, c_len, c_cool, c_prog, c_seq, c_inc, c_rpm, c_stat;
    double minarc;                                                /* this+0x308: MinArcFeedRate when AdjustArcFeedrates, else -1 */
    BnciuStats *st;
} U;
static double od(const uint8_t *op, size_t n, size_t off) { return off + 8 <= n ? D(op, off) : 0; }
static int32_t oi(const uint8_t *op, size_t n, size_t off) { return off + 4 <= n ? I32(op, off) : 0; }
static int16_t oh(const uint8_t *op, size_t n, size_t off) { return off + 2 <= n ? I16(op, off) : 0; }
#define OD(o) od(u->O, u->on, o)
#define ND_(o) od(u->N, u->nn, o)
#define OI(o) oi(u->O, u->on, o)
#define NI(o) oi(u->N, u->nn, o)

static int16_t g_of(const U *u, long k) { return k >= 0 && k < u->n && u->rec[k].n >= 2 ? I16(u->rec[k].r, 0) : 0; }
static int has(const U *u, long k, size_t off, size_t w) { return k >= 0 && k < u->n && off + w <= u->rec[k].n; }
/* FindGcode(G, start, forward) (RW @1800f6190): from start + 1; a record with op id (+4) 0 ends the search (-1) */
static long find_g(const U *u, int g, long start) {
    for (long k = start + 1; k < u->n; k++) {
        if (u->rec[k].n < 0x18) continue;                         /* size 0 records are skipped */
        if (I32(u->rec[k].r, 4) == 0) return -1;
        if (I16(u->rec[k].r, 0) == g) return k;
    }
    return -1;
}
/* bnci_get_pt @1800865c0 (the cases a 2D mill section has) */
static int get_pt(const U *u, long k, double p[3]) {
    int g = g_of(u, k); size_t o;
    switch (g) {
    case 0: case 1: case 2: case 3: case 0x14: case 0x15: case 0x1e: case 0x1f: case 0x51: case 100: o = 0x20; break;
    case 0xb: case 0x53: case 1003: o = 0x18; break;
    case 1002: o = 0x50; break;
    case 1016: o = 0x28; break;
    default: return 0;
    }
    if (!has(u, k, o, 24)) return 0;
    for (int i = 0; i < 3; i++) p[i] = D(u->rec[k].r, o + 8 * (size_t)i);
    return 1;
}
/* GetValidPreviousMove(k, lo, motion) @1800f7370: back from k - 1, skipping FUN_18013a8a0's gcodes (1005-1008, 1022, 1055: bits
   0x400000020000f of gcode - 0x3ed); motion: only FUN_18013a8d0's gcodes */
static int skip_g(int g) { unsigned d = (unsigned)(g - 0x3ed); return d < 0x33 && ((0x400000020000fULL >> d) & 1); }
static long prev_move(const U *u, long k, long lo) {
    if (k <= 0) return -1;
    if (k > u->n - 1) k = u->n - 1;
    for (long j = k - 1; j >= lo; j--) {
        if (!skip_g(g_of(u, j))) return j;
        if (j == 0) return -1;
    }
    return -1;
}
/* IsZmove(k, &up) @1800f7b80: gcode 0 / 1: the XY distance to the previous record's point < tol (up = previous z < this z); 100: true */
static int is_zmove(const U *u, long k, int *up) {
    *up = 0;
    if (k <= 0 || k >= u->n) return 0;
    int g = g_of(u, k);
    if (g == 100) return 1;
    if (g != 0 && g != 1) return 0;
    double p[3], q[3]; long j;
    if (!get_pt(u, k, p) || (j = prev_move(u, k, 0)) < 0 || !get_pt(u, j, q)) return 0;
    if (sqrt((q[1] - p[1]) * (q[1] - p[1]) + (q[0] - p[0]) * (q[0] - p[0]) + 0.0) < TOL) { *up = q[2] < p[2]; return 1; }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ feeds (B l.5079-5951, 2312-2837) */
typedef struct { size_t off; double v; int type, prev; } FD;
static int near(double a, double b) { return fabs(a - b) < TOL; }
static int feed_data(const U *u, long k, FD *e) {                 /* GetFeedData: one element (2D mill gcodes) */
    int g = g_of(u, k); size_t off; int type;
    switch (g) {
    case 0: off = 0x38; type = 1; break;
    case 1: off = 0x38; type = 2; break;
    case 2: case 3: off = 0x50; type = 2; break;
    case 0x51: off = 0x40; type = 2; break;
    case 100: off = 0x50; type = 2; break;
    case 1000: case 1001: case 1002: if (!has(u, k, 0xc, 1) || u->rec[k].r[0xc] != 0) return 0; off = 0x40; type = 2; break;   /* mill */
    default: return 0;
    }
    if (!has(u, k, off, 8) || !(D(u->rec[k].r, off) > 0.0)) return 0;
    e->off = off; e->v = D(u->rec[k].r, off); e->type = type; e->prev = 0;
    int up;                                                       /* IsZmove: plunge / retract by value (l.5865-5891) */
    if (is_zmove(u, k, &up) && g != 0 && g != 100) {
        if (!up && u->c_plg && near(e->v, OD(0x318))) e->type = 4;
        else if (up && u->c_ret && near(e->v, OD(0x320))) e->type = 3;
    }
    /* CalcMode 1 and the pocket / surface opcodes (l.5892-5948; 0x84 / 0x86 refused before): the old op's values */
    if (u->opcode == 3 || u->opcode == 9 || u->opcode == 10 || u->opcode == 0xf || u->opcode == 0x2c || u->opcode == 0x6b) {
        int t = e->type;
        if (near(e->v, OD(0x310))) ;
        else if (near(e->v, OD(0x318))) t = 4;
        else if (near(e->v, OD(0x320))) t = 3;
        if (t != e->type) { e->prev = e->type; e->type = t; }
    }
    return 1;
}
static void touched(U *u, long k) { (void)k; u->st->records++; }
static void change_feed(U *u, long k) {                           /* ChangeFeed: on element[0].type */
    FD e; if (!feed_data(u, k, &e)) return;
    uint8_t *r = u->rec[k].r; int up, g = g_of(u, k);
    if (e.type == 0 || e.type == 2) {                             /* ChangeFeedTraverse (l.2739-2837) */
        if (!u->c_feed || e.v != OD(0x310)) return;
        WD(r, e.off, ND_(0x310));
        if (fabs(e.v - D(r, e.off)) >= TOL) {
            double f = D(r, e.off);                               /* CheckMinFeedRate (l.4125-4137) */
            if (TOL <= u->minarc && f < u->minarc) { WD(r, e.off, u->minarc); u->st->minarc++; }
            touched(u, k);
        }
    } else if (e.type == 3) {                                     /* ChangeFeedRetract (l.2635-2732): only up Z moves */
        if (!u->c_ret || g == 0 || !is_zmove(u, k, &up) || !up || e.v != OD(0x320)) return;
        WD(r, e.off, ND_(0x320));
        if (fabs(e.v - D(r, e.off)) >= TOL) touched(u, k);
    } else if (e.type == 4) {                                     /* ChangeFeedPlunge (l.2532-2628) */
        if (!u->c_plg || g == 0) return;
        if (!(is_zmove(u, k, &up) || e.prev != 0)) return;
        if (up || e.v != OD(0x318)) return;
        WD(r, e.off, ND_(0x318));
        if (fabs(e.v - D(r, e.off)) >= TOL) touched(u, k);
    }                                                             /* 1 (rapids): never */
}
static void change_rpm(U *u, long k) {                            /* GetRpmData + ChangeRpmOpToolInfo (l.3566-3648) */
    int g = g_of(u, k); uint8_t *r = u->rec[k].r;
    if ((g == 1000 || g == 1001 || g == 1002) && has(u, k, 0x38, 4) && r[0xc] == 0) {
        if (I32(r, 0x38) == OI(0x328)) { W32(r, 0x38, NI(0x328)); touched(u, k); }
    } else if (g == 4 && has(u, k, 0x20, 8)) {
        if (near(D(r, 0x20), (double)OI(0x328))) { WD(r, 0x20, (double)NI(0x328)); touched(u, k); }
    }
}
/* the header records after the 1016 at POS (l.6265-6280; mill: rec +0xc == 0) */
static void change_header(U *u, long pos) {
    long k = find_g(u, 1002, pos);
    if (k >= 0 && has(u, k, 0x4a, 0)) {
        uint8_t *r = u->rec[k].r;
        if (u->c_tool && I32(r, 0x24) == OI(0x2e0) && I32(r, 0x24) != NI(0x2e0)) { W32(r, 0x24, NI(0x2e0)); touched(u, k); }   /* ChangeToolNo */
        if (u->c_dia && I32(r, 0x28) != NI(0x308)) { W32(r, 0x28, NI(0x308)); touched(u, k); }   /* ChangeDiaOffset: mill, no old check */
        if (u->c_len && I32(r, 0x2c) != NI(0x30c)) { W32(r, 0x2c, NI(0x30c)); touched(u, k); }   /* ChangeLenOffset: mill, no old check */
        if (u->c_cool && I16(r, 0x48) == oh(u->O, u->on, 0x330) && I16(r, 0x48) != oh(u->N, u->nn, 0x330)) {   /* ChangeCoolant */
            W16(r, 0x48, oh(u->N, u->nn, 0x330)); touched(u, k); }
        if (u->c_prog && I32(r, 0x18) == OI(0xf8) && I32(r, 0x18) != NI(0xf8)) { W32(r, 0x18, NI(0xf8)); touched(u, k); }   /* ChangeProgNo */
        if (u->c_seq && I32(r, 0x1c) == OI(0xfc) && I32(r, 0x1c) != NI(0xfc)) { W32(r, 0x1c, NI(0xfc)); touched(u, k); }    /* ChangeSeqNo */
        if (u->c_inc && I32(r, 0x20) == OI(0x100) && I32(r, 0x20) != NI(0x100)) { W32(r, 0x20, NI(0x100)); touched(u, k); } /* ChangeSeqInc */
    }
    if (u->c_stat && has(u, pos, 0x5c, 0) && I32(u->rec[pos].r, 0x58) == OI(0x53c)) { W32(u->rec[pos].r, 0x58, NI(0x53c)); touched(u, pos); }   /* ChangeStationNo */
}

/* DontAllow's mask (B l.4500-4991, formats/opdlg_port_audit.md 19.1): the fields the patch may change; any other difference refuses */
static const struct { uint16_t off, n; } MASK[] = {
    { 6, 0x78 }, { 0x80, 0x84 }, { 0x170, 0x104 }, { 0x2e0, 4 }, { 0x308, 4 }, { 0x30c, 4 }, { 0x310, 8 }, { 0x318, 8 }, { 0x320, 8 },
    { 0x328, 4 }, { 0x330, 2 }, { 0x336, 0x78 }, { 0x53c, 4 }, { 0x540, 4 }, { 0x544, 8 }, { 0x780, 0x80 }, { 0x868, 0x20 },
    { 0x930, 0x28 }, { 0xae8, 0x2a },
};
static int op_is_lathe(int op) { return (uint16_t)(op - 0x3c) <= 12 || (uint16_t)(op - 0xc9) <= 0x23; }
static int op_is_wire(int op) { return (uint16_t)(op - 0x4a) <= 9; }
static int op_is_xform(int c) { return c == 4 || c == 0x50 || c == 0x6c || c == 0x9a; }

int bnciu_patch(uint8_t *sec, size_t n, const uint8_t *oldop, size_t oldn, const uint8_t *newop, size_t newn, double minarc,
                BnciuStats *st, char *err, size_t errn) {
    memset(st, 0, sizeof *st); err[0] = 0;
    U uu, *u = &uu; memset(u, 0, sizeof *u);
    u->O = oldop; u->N = newop; u->on = oldn < OPN ? oldn : OPN; u->nn = newn < OPN ? newn : OPN; u->st = st; u->minarc = minarc;
    u->opcode = (uint16_t)oh(oldop, oldn, 4);
    if (u->on < 0x1300 || u->nn < 0x1300) { snprintf(err, errn, "no operation record"); return 0; }
    if (oi(oldop, oldn, 0) != oi(newop, newn, 0)) { snprintf(err, errn, "another operation"); return 0; }   /* bnci_update: ids differ */
    if (op_is_lathe(u->opcode) || op_is_wire(u->opcode)) { snprintf(err, errn, "lathe / wire operations (P4)"); return 0; }
    if (u->opcode == 0x84 || u->opcode == 0x86) { snprintf(err, errn, "the High Speed feed records (ChangeFeedHmm / BackHmm2d) are NOT PORTED"); return 0; }
    if (op_is_xform(u->opcode)) { snprintf(err, errn, "transform operations (GetSourceOperation) are NOT PORTED"); return 0; }
    if (u->opcode == 2 && oldop[0x1298]) { snprintf(err, errn, "a drill subprogram (DontAllow)"); return 0; }
    /* DontAllow: the masked compare */
    size_t L = u->on < u->nn ? u->on : u->nn;
    if (u->on != u->nn) { snprintf(err, errn, "the operation records differ in size"); return 0; }
    uint8_t *a = malloc(L), *b = malloc(L); memcpy(a, oldop, L); memcpy(b, newop, L);
    for (size_t i = 0; i < sizeof MASK / sizeof MASK[0]; i++)
        if (MASK[i].off + (size_t)MASK[i].n <= L) { memset(a + MASK[i].off, 0, MASK[i].n); memset(b + MASK[i].off, 0, MASK[i].n); }
    int other = memcmp(a, b, L) != 0; free(a); free(b);
    if (other) { snprintf(err, errn, "a change outside the values Mastercam patches (DontAllow)"); return 0; }
    /* SetChangeFlags (exact compares) */
    u->c_ret = OD(0x320) != ND_(0x320); u->c_plg = OD(0x318) != ND_(0x318); u->c_feed = OD(0x310) != ND_(0x310);
    u->c_tool = OI(0x2e0) != NI(0x2e0); u->c_tid = OI(0x544) != NI(0x544); u->c_dia = OI(0x308) != NI(0x308);
    u->c_len = OI(0x30c) != NI(0x30c); u->c_cool = oh(oldop, oldn, 0x330) != oh(newop, newn, 0x330);
    u->c_prog = OI(0xf8) != NI(0xf8); u->c_seq = OI(0xfc) != NI(0xfc); u->c_inc = OI(0x100) != NI(0x100);
    u->c_rpm = OI(0x328) != NI(0x328); u->c_stat = OI(0x53c) != NI(0x53c);
    /* NOT PORTED here (refused): the tool number / slot (the tool-system update after the loop, this+0x344), the home position,
       misc values, comments, canned text (batch 11's pv_nci_rewrite covers misc values / canned text alone) */
    if (u->c_tool || u->c_tid) { snprintf(err, errn, "a tool number / tool change (the tool-system update is NOT PORTED)"); return 0; }
    if (memcmp(oldop + 0x870, newop + 0x870, 24) || memcmp(oldop + 0x780, newop + 0x780, 0x80) || memcmp(oldop + 6, newop + 6, 0x78)
        || memcmp(oldop + 0x336, newop + 0x336, 0x78) || memcmp(oldop + 0xae8, newop + 0xae8, 0x2a)) {
        snprintf(err, errn, "home position / misc values / comments / canned text changed (NOT PORTED here)"); return 0; }
    int feedchg = u->c_ret || u->c_plg || u->c_feed, rpmchg = u->c_rpm;
    /* SetGcodeMask (B l.7329-7380): this+0x302 = only header ints changed */
    static const int M0[] = { 0, 1, 2, 3, 4, 0xb, 0x14, 0x15, 0x16, 0x17, 0x1e, 0x1f, 0x20, 0x21, 0x51, 0x52, 0x53, 100, 1000, 1001, 1002,
                              1008, 1011, 1012, 1014, 1016, 1025, 20001, 20004 };
    static const int M1[] = { 1000, 1001, 1002, 1014, 1016, 20004 };
    const int *M = feedchg || rpmchg ? M0 : M1; size_t nm = feedchg || rpmchg ? sizeof M0 / sizeof *M0 : sizeof M1 / sizeof *M1;
    /* ReadSection(op id, mask): the section's records whose gcode is in the mask, in file order [NOT DECODED: ReadSection's own
       filtering beyond the mask - taken as file order] */
    long cap = 0;
    for (size_t p = 0; p + 2 <= n; ) {
        uint16_t sz; memcpy(&sz, sec + p, 2);
        if (p + 2 + sz > n) break;
        uint8_t *r = sec + p + 2;
        if (sz >= 2) {
            int g = I16(r, 0), in = 0;
            for (size_t i = 0; i < nm && !in; i++) in = M[i] == g;
            if (g == 0xb && in) { free(u->rec); snprintf(err, errn, "5-axis moves (IsAlongVectorMove) are NOT PORTED"); return 0; }
            if (in) { if (u->n == cap) { cap = cap ? 2 * cap : 256; u->rec = realloc(u->rec, sizeof *u->rec * (size_t)cap); } u->rec[u->n++] = (Rec){ r, sz }; }
        }
        p += 2 + (size_t)sz;
    }
    /* ProcessNci's loop (B l.6190-6366) */
    long pos = -1, i = 0;
    while (i < u->n) {
        pos = find_g(u, 1016, pos);
        if (pos < 0) break;
        change_header(u, pos);
        long lim = find_g(u, 1014, pos); if (lim < 0) lim = u->n;  /* Get1016Limit (l.4994-5014) */
        if (lim <= pos) break;
        if (feedchg || rpmchg) {
            for (long k = pos + 1; k < lim; k++) { if (feedchg) change_feed(u, k); if (rpmchg) change_rpm(u, k); }
            i = pos + 1 > lim ? pos + 1 : lim;
        } else pos++;
        i++;
    }
    free(u->rec);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the app (ours: glue) */
#ifndef BNCIU_NO_APP
int bnciu_apply(McamFile *f, int index, const uint8_t *newrec, uint32_t newn, double minarc, BnciuStats *st, char *msg, size_t msgn) {
    msg[0] = 0; memset(st, 0, sizeof *st);
    if (index < 0 || index >= f->nops) { snprintf(msg, msgn, "no such operation"); return 0; }
    Op *o = &f->ops[index];
    if (!o->raw || o->raw_n <= 0x68 || newn != o->raw_n) { snprintf(msg, msgn, "operation %d is not stored in the part", index + 1); return 0; }
    /* the op's section: as rewritten here, else the part file's (cam_postvals.c pv_nci_rewrite's reader) */
    uint8_t *d = NULL; size_t len = 0, off = 0;
    if (o->nci) { d = malloc(o->nci_n); memcpy(d, o->nci, o->nci_n); len = o->nci_n; }
    else {
        struct stat sb;
        if (!f->path[0] || !f->nci_off || stat(f->path, &sb) != 0 || (long long)sb.st_size != f->file_size || (long long)sb.st_mtime != f->file_mtime) {
            snprintf(msg, msgn, "the part file changed on disk since it was opened (or has no NCI) - open it again"); return 0; }
        FILE *in = fopen(f->path, "rb");
        if (in) { fseek(in, 0, SEEK_END); long Lf = ftell(in); fseek(in, 0, SEEK_SET);
            if (Lf > 0) { len = (size_t)Lf; d = malloc(len); if (fread(d, 1, len, in) != len) { free(d); d = NULL; } } fclose(in); }
        if (!d) { snprintf(msg, msgn, "cannot read %s", f->path); return 0; }
        off = f->nci_off;
    }
    size_t s0 = 0, s1 = 0; int in = 0, found = 0;
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        int16_t g = sz >= 2 ? I16(d, off + 2) : 0;
        if (g == -2 && sz >= 0x20) { in = I32(d, off + 2 + 0x1c) == o->id; if (in) s0 = off; }
        if (in && g == -3) { s1 = off + 2 + sz; found = 1; break; }
        off += 2 + sz;
    }
    if (!found) { free(d); snprintf(msg, msgn, "operation %d has no NCI in the part", index + 1); return 0; }
    uint8_t *sec = malloc(s1 - s0); memcpy(sec, d + s0, s1 - s0); free(d);
    char err[300];
    if (!bnciu_patch(sec, s1 - s0, o->raw + 0x68, o->raw_n - 0x68, newrec + 0x68, newn - 0x68, minarc, st, err, sizeof err)) {
        free(sec); snprintf(msg, msgn, "operation %d: %s", index + 1, err); return 0; }
    mcam_op_set_nci(f, index, sec, s1 - s0);
    memcpy(o->raw, newrec, newn);
    return 1;
}
#endif
