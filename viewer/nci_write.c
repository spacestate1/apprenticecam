/* nci_write - port of Mastercam 2017's binary NCI writers (mccore.dll) for operations made here (see nci_write.h,
 * ../formats/nci_writer.md). Every record is built as its writer builds it: memset(0x370), the gcode, the fields, then
 * bnci_write_to_disk (+0x10 file position, +0x14 bnci_bytes, the u16 size + the first bytes-2 bytes).
 *
 * Sources (Mastercam 2017 x64 VAs; decompiles in decomp/nciw/core_wr.c (Ghidra DumpDecomp 2026-10-24), decomp/b11/core_b11.c /
 * core_b11b.c, decomp/native_agent/core_wr81.c, decomp/mccore_bnci.c, decomp/mccore_cc.c):
 *   bnci_write_to_disk @18009d930, bnci_bytes @18003a800, FUN_18007bc90 (the +0xc flag), bnci_wr_sos @180097170,
 *   bnci_wr_sof @180096f30, bnci_wr_1011 @18008e240, _1012 @18008e3d0, _1013 @18008e540, _1014 @18008eb40, _1016 @18008ed40,
 *   _1017 @18008f1c0, _1025 @18008f9a0, _1027 @18008fad0, _1028 @18008fd30, _1029 @1800900f0, _1020 @18008f6a0,
 *   bnci_wr_tool @180098ca0, bnci_wr_views @180099bf0, _20800 @180090ba0, _1002 @18008d7c0, FUN_1800a0d70 (home),
 *   rotary_axis_op_param_to_bnci @1805ff0d0, bnci_wr_0 @18008d6b0, bnci_wr_lin @180094e50, bnci_wr_arc @1800917d0,
 *   bnci_wr_1004 @18008db90, bnci_wr_eos @180092820, cc_control_gcode_2 @18009dde0, cc_computer_gcode_2 @18009dce0,
 *   FUN_18011be20, FUN_1800a1280, op_is_* (core_wr.c), mcmill bnci_face @180097230 / FUN_1801e32a0 (gr_lin_mill: one line
 *   record per move).
 * "ours" marks glue that is not Mastercam's; [ND] marks a value written with a stand-in because its source is NOT DECODED
 * (counted in NciwStats.not_decoded, listed in nci_writer.md). */
#include "nci_write.h"
#include "mcam_grp.h"
#include "toollib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>

#define NB 0x370                                        /* sizeof(nci_bin) (the writers' memset) */

static int16_t I16(const uint8_t *b, int o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static int32_t I32(const uint8_t *b, int o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static double D(const uint8_t *b, int o) { double v; memcpy(&v, b + o, 8); return v; }
static void W16(uint8_t *b, int o, int16_t v) { memcpy(b + o, &v, 2); }
static void W32(uint8_t *b, int o, int32_t v) { memcpy(b + o, &v, 4); }
static void WD(uint8_t *b, int o, double v) { memcpy(b + o, &v, 8); }

/* ------------------------------------------------------------------------------------------------ op type tests (mccore) */
static int op_is_lathe(int op) { return (uint16_t)(op - 0x3c) <= 12 || (uint16_t)(op - 0xc9) <= 0x23; }     /* @1800c6060 (nci_ascii.c) */
static int op_is_wire(int op) { return (uint16_t)(op - 0x4a) <= 9; }                                          /* @1800c76c0 (nci_ascii.c) */
static int rec_flag(int op) { return op_is_lathe(op) ? 1 : op_is_wire(op) ? 2 : 0; }                         /* FUN_18007bc90 */
static int op_is_router(int op) { return op == 0x132; }                                                        /* @1800c6fe0 */
static int op_is_drill(int op) { return ((uint16_t)op <= 0x39 && ((0x380000010000004ULL >> (op & 0x3f)) & 1)) || op == 0x132; }   /* @1800c5e80 */
static int op_is_multiax(int c) {                                                                              /* @1800c6700 (nci_reverse.c) */
    switch (c) { case 0x1c: case 0x1d: case 0x2d: case 0x2e: case 0x30: case 0x31: case 0x6e: case 0x70: case 0x71: case 0x9a: return 1; }
    return 0;
}
static int op_is_msurf(int c) {                                                                                /* @1800c62b0 (nci_reverse.c) */
    switch (c) { case 5: case 6: case 7: case 8: case 9: case 10: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: case 0x1c:
    case 0x1d: case 0x27: case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x30: case 0x6b:
    case 0x6d: case 0x6e: case 0x70: case 0x71: case 0x83: case 0x84: return 1; }
    return 0;
}
static int op_is_xform(int c) { return c == 4 || c == 0x50 || c == 0x6c || c == 0x9a; }                       /* @1800c7700 */
static int op_is_no_refpt(int c) {                                                                             /* @1800c6970 (op_is_chook /
                                                                       op_is_solid_ctrl not ported: their opcodes are not made here) */
    switch (c) { case 4: case 0x11: case 0x14: case 0x2f: case 0x45: case 0x46: case 0x4d: case 0x50: case 0x51: case 0x52: case 0x67:
    case 0x6c: case 0x8b: case 0x9a: return 1; }
    return 0;
}
static int op_is_oldnci(int c) {                                                                               /* @1800c6dd0 (op_is_chook: not
                                                                       made here); 0 = the op's own views are written */
    if (op_is_msurf(c)) return 0;
    if (c >= 0x132) return c != 0x132;
    switch (c) {
    case 1: case 2: case 3: case 4: case 0x11: case 0x12: case 0x13: case 0x15: case 0x16: case 0x18: case 0x19: case 0x1a: case 0x1b:
    case 0x1c: case 0x1d: case 0x30: case 0x33: case 0x34: case 0x35: case 0x37: case 0x38: case 0x39: case 0x3c: case 0x3d: case 0x3e:
    case 0x3f: case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x47: case 0x48: case 0x4a: case 0x4b: case 0x4c: case 0x4d:
    case 0x4e: case 0x4f: case 0x50: case 0x51: case 0x53: case 100: case 0x65: case 0x66: case 0x67: case 0x68: case 0x69: case 0x6a:
    case 0x6c: case 0x6e: case 0x6f: case 0x70: case 0x82: case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: case 0x8a: case 0x8b:
    case 0x96: case 0x9a: case 0xc9: case 0xca: case 0xce: case 0xd0: case 0xd2: case 0xd3: case 0xd4: case 0xd6: case 0xd7: case 0xd8:
    case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xe0: case 0xe1: case 0xe6: case 0xe7: case 0xe8: case 0xe9: case 0xea: case 0xeb:
    case 0xec: return 0;
    }
    return 1;
}
static int op_is_contour(int c) { return c == 1 || c == 0x33 || c == 0x34 || c == 0x35 || c == 0x85; }        /* [ND] op_is_contour body not
                                                                       dumped: the contour family of nci_reverse.c op_is_subpgm case 1 */

/* ------------------------------------------------------------------------------------------------ bnci_bytes @18003a800 */
size_t nciw_bytes(int g) {
    switch ((int16_t)g) {
    case 0: case 1: case 0x3fb: case 20104: case 20109: case 20200: return 0x4a;
    case 2: case 3: case 0x3f6: case 0x3f9: case 0x404: case 20005: case 20006: case 20008: case 20502: return 0x62;
    case 4: case 0x388: case 0x3eb: return 0x32;
    case 0xb: case 0x53: case 900: case 0x38a: case 20106: return 0x72;
    case 0x50: case 0x3ec: return 0x1c;
    case 0x51: case 0xa06: case 20701: return 0xb2;
    case 0x52: case 0xa2a: case 0xaf5: case 0xaf6: case 0xaf7: case 0xaf8: return 0xba;
    case 100: case 0x9c4: return 0xd2;
    case -99: case -0x5f: case 200: case 0x385: case 20105: return 0x52;
    case -0x62: case -0x61: case -9: return 0x26;
    case -0x60: case -10: case -7: case -6: case -5: case -4: case 20100: return 0x3a;
    case -8: case 0xa8c: return 0x152;
    case -3: case 999: case 20009: return 0x22;
    case -2: case 0x389: return 0x2a;
    case 0xc9: case 0x386: case 0x403: case 20111: return 0x7a;
    case 2000: case 0x7d1: case 0x7d2: case 0x837: case 0x838: case 0x8ac: case 0xa8d: case 0xafc: case 0xb40: case 20108: return 0x8a;
    case 0x387: case 0x390: case 0x3f4: case 0x401: case 0x408: case 0x410: case 0x411: return 0x42;
    case 0x38f: case 0x3f3: case 0x405: case 0x407: case 20107: return 0x6a;
    case 0x393: case 0x394: return 0x94;
    case 0x399: case 0x39a: case 0x39d: return 0x1ca;
    case 0x39b: return 0x1d2;
    case 0x39c: return 0x1da;
    case 0x39e: return 0x272;
    case 0x3b6: return 0x5e;
    case 1000: case 0x3e9: case 0x3ea: case 0x8c0: case 20001: case 20002: case 20003: case 20020: case 20021: case 20103: case 20110:
    case 0x5015: return 0x92;
    case 0x3ed: case 0x3ee: case 0x3ef: case 0x402: case 0x412: case 0x413: case 0x41c: case 0x420: return 0x309;
    case 0x3f0: case 20010: case 20011: case 20012: case 20013: case 20014: case 20015: case 20016: case 20017: case 20018: case 20019: return 0x93;
    case 0x3f5: case 0xaf0: return 0x162;
    case 0x3f8: case 0x834: case 0x85c: case 0x884: case 0x9ce: case 0x9cf: case 0x9dd: case 0x9e2: case 0x9e3: case 0x9e4: case 0x9ec:
    case 0xa2b: case 0xa2c: case 0xa2d: case 0xa2e: case 0xa3c: case 20007: return 0x82;
    case 0x3fa: return 0xc2;
    case 0x3fc: return 0xea;
    case 0x41a: return 0x156;
    case 0x41b: case 0x41d: case 0x41f: case 0x4f4c: return 0x11f;
    case 0x833: case 0x899: case 0x89a: return 0x9e;
    case 0x836: case 0xa90: case 0xa91: case 0xb0e: case 0xb18: case 0xb19: case 0xb1a: case 0xb1b: case 0xb1c: case 0xb1d: case 0xb1e:
    case 0xb1f: case 0xb41: return 0x84;
    case 0x83e: case 20004: return 0xca;
    case 0x87a: case 0x89b: case 0x89d: case 0x89f: return 0x9c;
    case 0x898: return 0xaa;
    case 0x89c: return 0xae;
    case 0x89e: return 0xfa;
    case 0x8a0: case 0x8b2: return 0xa2;
    case 0x8ad: case 0x8ae: case 0x8af: case 0x8b3: return 0x86;
    case 0x8b0: return 0x96;
    case 0x9d1: case 0x9d2: case 0x9e5: case 0xa01: case 0xa28: return 0xda;
    case 0x9e0: case 0x9e1: case 0xa03: case 0xa0a: case 0xa0b: case 0xafb: return 0xe2;
    case 0x9ef: return 0xf2;
    case 0xa14: case 0xa19: case 0xa1a: return 0x10a;
    case 0xa34: case 0xa8b: case 0xaf1: return 0x7f;
    case 0xa8e: return 0x7e;
    case 0xa8f: return 0x372;
    case 0xafa: return 0x8e;
    case 0xb10: return 0x8c;
    case 0xb20: return 0xa6;
    case 0xb23: case 0xb24: return 0x85;
    case 0xb55: return 0x212;
    case 0xb56: return 0x21a;
    case 10000: return 0x44;
    case 20000: return 0x126;
    case 20101: case 20102: return 0x5a;
    case 20112: return 0x11d;
    case 20800: return 0x22;
    }
    if (g >= 0xb54 && g <= 0xb67) return 0x20e;                 /* 0xb54, 0xb57 .. 0xb67 (0xb55 / 0xb56 above) */
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the part index (ours) */
typedef struct { const uint8_t *r; uint32_t n; int sub; int32_t id; } PRec;
struct NciwPart {
    uint8_t *d; size_t len;
    PRec *pr; int npr;
    const uint8_t **grp; int ngrp;                               /* op_group records (0x1ae0 each) of the sub 0xF arrays */
    McamPlaneInfo *pl; int npl;                                   /* the planes archive */
    const uint8_t **ent; int nent;                                /* every entity record (ent +0x20 = its eptr: get_ent_from_eptr) */
};
NciwPart *nciw_part_load(const char *path) {
    NciwPart *P = calloc(1, sizeof *P);
    FILE *in = path && path[0] ? fopen(path, "rb") : NULL;
    if (!in) return P;
    fseek(in, 0, SEEK_END); long L = ftell(in); fseek(in, 0, SEEK_SET);
    if (L > 0) { P->d = malloc((size_t)L); P->len = fread(P->d, 1, (size_t)L, in); }
    fclose(in);
    if (mcam_part_newer(P->d, P->len)) { free(P->d); P->d = NULL; P->len = 0; return P; }   /* a newer release's part: not read (mcam_grp.h) */
    /* the record framing of nci_ascii.c part_index (mcam_load's): u32 length + record; 0x200 sub 0xF is followed by
       its op_group array, sub 0xE by the material table, 0x20000 (Parasolid) by its blob */
    const uint8_t *d = P->d; size_t len = P->len, off = 0; int cap = 0, gcap = 0;
    while (off + 4 <= len) {
        uint32_t n; memcpy(&n, d + off, 4);
        if (n == 0 || off + 4 + n > len) break;                   /* the binary NCI tail */
        const uint8_t *rec = d + off + 4;
        if (n >= 0x28) {
            if (P->nent % 256 == 0) P->ent = realloc(P->ent, (size_t)(P->nent + 256) * sizeof *P->ent);
            P->ent[P->nent++] = rec;
        }
        if (n >= 0x70 && I32(rec, 0) == 0x4000) {
            if (P->npr == cap) { cap = cap ? 2 * cap : 256; P->pr = realloc(P->pr, (size_t)cap * sizeof *P->pr); }
            P->pr[P->npr++] = (PRec){ rec, n, I32(rec, 0x2c), I32(rec, 0x34) };
            if (I32(rec, 0x2c) == 36 && rec[0x56] && !P->pl) {    /* MastercamPlanes (mcam_load.c) */
                P->pl = calloc(64, sizeof *P->pl);
                P->npl = mcam_planes_list(rec + 0x68, n - 0x68, P->pl, 64);
                if (P->npl < 0) P->npl = 0;
            }
        }
        off += 4 + n;
        if (n >= 0x6a && I32(rec, 0) == 0x200) {
            uint16_t sub; memcpy(&sub, rec + 0x68, 2);
            if (sub == 0xF && off + 4 <= len) {
                int ng = 0; size_t gl = mcam_grp_block(d + off, len - off, NULL, 0, &ng);   /* the records and their member entries (mcam_grp.h) */
                const uint8_t *gr[256];                                                    /* (the first 256 groups) */
                if (gl && ng > 0) { mcam_grp_block(d + off, len - off, gr, 256, &ng); if (ng > 256) ng = 256; } else ng = 0;
                for (int k = 0; k < ng; k++) {
                    if (P->ngrp == gcap) { gcap = gcap ? 2 * gcap : 16; P->grp = realloc(P->grp, (size_t)gcap * sizeof *P->grp); }
                    P->grp[P->ngrp++] = gr[k];
                }
                off = gl ? off + gl : len;
            } else if (sub == 0xE) {
                while (off + 0x28 <= len) {
                    int32_t id; uint64_t sz; memcpy(&id, d + off, 4); memcpy(&sz, d + off + 0x10, 8); off += 0x28;
                    if (id == 0) break;
                    if (sz > len - off) { off = len; break; }
                    off += sz;
                }
            }
        } else if (n >= 0x16c && I32(rec, 0) == 0x20000) {
            uint8_t ver = rec[0x49]; uint32_t xs; memcpy(&xs, rec + 0x168, 4);
            if (xs > len - off) break;
            off += xs + 4 + (ver > 0x1b ? 0x100 : 0x40) + 1 + 1 + 4 + 4 + 8;
            if (off + 4 > len) break;
            uint32_t cnt; memcpy(&cnt, d + off, 4); off += 4;
            for (uint32_t i = 0; i < cnt && off + 8 <= len; i++) { uint32_t z; memcpy(&z, d + off + 4, 4); if (8 + (size_t)z > len - off) { off = len; break; } off += 8 + z; }
        }
    }
    return P;
}
void nciw_part_free(NciwPart *P) { if (!P) return; free(P->d); free(P->pr); free(P->grp); free(P->pl); free(P->ent); free(P); }
static const PRec *prec(const NciwPart *P, int sub, int32_t id) {
    for (int i = 0; i < P->npr; i++) if (P->pr[i].sub == sub && P->pr[i].id == id) return &P->pr[i];
    return NULL;
}
const uint8_t *nciw_part_tl(const NciwPart *P, int32_t slot, uint32_t *n) {   /* TL_ID: ASSOC sub 3, slot +0x2cc (mcam_saveops.c) */
    for (int i = 0; i < P->npr; i++) if (P->pr[i].sub == 3 && P->pr[i].n >= 0x2d0 && I32(P->pr[i].r, 0x2cc) == slot) { *n = P->pr[i].n; return P->pr[i].r; }
    return NULL;
}
static const uint8_t *group_by_id(const NciwPart *P, int32_t id) {             /* TpGrpList::GroupByID */
    for (int i = 0; i < P->ngrp; i++) if (I32(P->grp[i], 0) == id) return P->grp[i];
    return NULL;
}
/* OMgetGrpMachineGroup: the group's machine group = the root of its +0x10c parent chain (mcam_saveops.c "a nested toolpath
   group's root: follow the parents"; formats/mcam_write.md 2) */
static const uint8_t *machine_group_of(const NciwPart *P, int32_t id) {
    const uint8_t *g = group_by_id(P, id);
    for (int hop = 0; g && I32(g, 0x10c) != 0 && hop < 16; hop++) { const uint8_t *u = group_by_id(P, I32(g, 0x10c)); if (!u) break; g = u; }
    return g;
}
static const McamPlaneInfo *plane_by_id(const NciwPart *P, int id) {           /* GetPlanePtrByID(PlaneID(id, 2)) */
    for (int i = 0; i < P->npl; i++) if (P->pl[i].cls == 2 && P->pl[i].id == id && P->pl[i].is_plane) return &P->pl[i];
    return NULL;
}

/* CCNCMachine::MaxToolSpindleRPM(op, NULL) @18006b850 - the port in nci_ascii.c max_tool_rpm (same rules, this index) */
static int max_tool_rpm(const NciwPart *P, const uint8_t *op, uint32_t opn, int32_t *out) {
    if (0x11bc + 4 > (int)opn) return 0;
    const PRec *a = prec(P, 26, I32(op, 0x11bc));                /* AxisCombinationFromID: the subtype-26 record of that id */
    if (!a || a->n < 0x1e50) return 0;
    const PRec *ch = NULL;
    for (int k = 0; k < 2 && !ch; k++) {
        const PRec *l = prec(P, 15, I32(a->r, 0x1e48 + 4 * k));
        if (!l) continue;
        int cnt = I32(l->r, 0x68);
        if (cnt < 1 || 0x70 + 4 * (size_t)cnt > l->n) continue;
        const PRec *c = prec(P, 12, I32(l->r, 0x70 + 4 * (cnt - 1)));
        int t = c && c->n >= 0xe8 ? I32(c->r, 0xe4) : -1;
        if ((t >= 200 && t <= 207) || (t >= 300 && t <= 307)) ch = l;
    }
    if (!ch) return 0;
    int32_t v = 0x7fffffff; int cnt = I32(ch->r, 0x68);
    for (int i = 0; i < cnt && v == 0x7fffffff; i++) {
        const PRec *p = prec(P, 12, I32(ch->r, 0x70 + 4 * i));
        int t = p && p->n >= 0xe8 ? I32(p->r, 0xe4) : -1;
        if (!p || p->n < 0x590) continue;
        if (t == 301 || t == 203 || t == 204) v = I32(p->r, 0x50c);
        else if (t == 303 || t == 307) v = I32(p->r, 0x58c);
        else if (t == 304 || t == 305) v = 100000;
    }
    if (v == -1) return 0;
    *out = v; return 1;
}

/* ------------------------------------------------------------------------------------------------ writer state */
typedef struct {
    const NciwPart *P; const NciwOp *o; NciwBuf *out; NciwStats *st;
    const uint8_t *op; uint32_t opn;        /* operation = record + 0x68 */
    const uint8_t *tl; uint32_t tln;        /* the TL_ID record image */
    int32_t opid; int opcode;
} W;
static void nd(W *w, const char *fmt, const char *what) {         /* ours: a stand-in value */
    w->st->not_decoded++;
    size_t L = strlen(w->st->note);
    if (L + strlen(what) + 4 < sizeof w->st->note && !strstr(w->st->note, what)) snprintf(w->st->note + L, sizeof w->st->note - L, fmt, L ? "; " : "", what);
}
#define ND(what) nd(w, "%s%s", what)
static uint8_t ob(const W *w, int off) { return off >= 0 && (uint32_t)off < w->opn ? w->op[off] : 0; }
static int16_t oh(const W *w, int off) { return off >= 0 && (uint32_t)off + 2 <= w->opn ? I16(w->op, off) : 0; }
static int32_t oi(const W *w, int off) { return off >= 0 && (uint32_t)off + 4 <= w->opn ? I32(w->op, off) : 0; }
static double od(const W *w, int off) { return off >= 0 && (uint32_t)off + 8 <= w->opn ? D(w->op, off) : 0; }
static void ocp(const W *w, uint8_t *r, int roff, int ooff, int n) {           /* memcpy(r + roff, op + ooff, n) */
    for (int i = 0; i < n; i++) r[roff + i] = ob(w, ooff + i);
}
static uint8_t tb(const W *w, int off) { return w->tl && off >= 0 && (uint32_t)off < w->tln ? w->tl[off] : 0; }
static void tcp(const W *w, uint8_t *r, int roff, int toff, int n) { for (int i = 0; i < n; i++) r[roff + i] = tb(w, toff + i); }
/* _mbsnbcpy_s(dst, size, src, count): at most count bytes of src (to its NUL), then a NUL (single-byte text) */
static void mbcpy(uint8_t *dst, const char *src, size_t count) { size_t n = 0; while (n < count && src[n]) { dst[n] = (uint8_t)src[n]; n++; } dst[n] = 0; }
static void mbcpy_op(const W *w, uint8_t *dst, int ooff, size_t count) {
    size_t n = 0; while (n < count && ob(w, ooff + (int)n)) { dst[n] = ob(w, ooff + (int)n); n++; } dst[n] = 0; }
static void mbcpy_tl(const W *w, uint8_t *dst, int toff, size_t count) {
    size_t n = 0; while (n < count && tb(w, toff + (int)n)) { dst[n] = tb(w, toff + (int)n); n++; } dst[n] = 0; }

static void rec0(W *w, uint8_t *r, int gcode) {                   /* memset(0x370), gcode, +4 op id (+0xc set by the caller) */
    memset(r, 0, NB); W16(r, 0, (int16_t)gcode); W32(r, 4, w->opid);
}
/* bnci_write_to_disk @18009d930: 1000 / 1001 -> 1002; +4 = cur_op when 0; +0x10 = file position, +0x14 = bnci_bytes; the u16
   (bytes - 2) and the first bytes - 2 bytes of the record */
static void put(W *w, uint8_t *r) {
    if ((uint16_t)(I16(r, 0) - 1000) < 2) W16(r, 0, 0x3ea);
    if (I32(r, 4) == 0) W32(r, 4, w->opid);
    size_t sz = nciw_bytes(I16(r, 0));
    if (!sz) { ND("a record of unknown size"); return; }
    NciwBuf *b = w->out;
    W32(r, 0x10, (int32_t)(w->o->base + (long)b->n)); W32(r, 0x14, (int32_t)sz);
    if (b->n + sz > b->cap) { b->cap = (b->n + sz) * 2 + 4096; b->b = realloc(b->b, b->cap); }
    uint16_t u = (uint16_t)(sz - 2); memcpy(b->b + b->n, &u, 2); memcpy(b->b + b->n + 2, r, sz - 2); b->n += sz;
    w->st->records++;
}

/* ------------------------------------------------------------------------------------------------ views (mastercam.dll) */
/* find_view_m(matrix, &vno, 1, &found): the view number of a matrix. Ours: the reserved views 1..8 (mcam_std_view) within
   1e-9; any other matrix: the op_view's stored number at VNO_OFF [ND: Mastercam's session view table / new view numbers] */
static int find_view(W *w, int moff, int vno_off) {
    double m[9]; for (int k = 0; k < 9; k++) m[k] = od(w, moff + 8 * k);
    for (int v = 1; v <= 8; v++) {
        double s[9]; if (!mcam_std_view(v, s)) continue;
        int eq = 1; for (int k = 0; k < 9 && eq; k++) eq = fabs(s[k] - m[k]) < 1e-9;
        if (eq) return v;
    }
    ND("find_view_m of a view other than the reserved 1..8 (the op's stored view number)");
    return oh(w, vno_off);
}
/* world_to_view(p, vno): the view's rows . p; views < 2 (and 1 Top) the identity (core_ui/cui_stock.c, mastercam.dll
   world_to_view 0x180341b30) */
static void world_to_view(W *w, int vno, const double p[3], double out[3], int moff) {
    double m[9];
    if (vno < 2) { memcpy(out, p, 3 * sizeof *p); return; }
    if (!mcam_std_view(vno, m)) for (int k = 0; k < 9; k++) m[k] = od(w, moff + 8 * k);   /* [ND] a session view: the op's matrix */
    for (int i = 0; i < 3; i++) out[i] = m[3 * i] * p[0] + m[3 * i + 1] * p[1] + m[3 * i + 2] * p[2];
}
static void make_reals_0(double *p) { for (int k = 0; k < 3; k++) if (fabs(p[k]) < 5e-5) p[k] = 0; }   /* @18013ac10 [ND: its
                                                                        tolerance; nci_ascii.c uses the same for the 1003 home] */

/* ------------------------------------------------------------------------------------------------ the section start */
static void wr_1011(W *w) {                                         /* @18008e240 */
    uint8_t r[NB]; rec0(w, r, 0x3f3); W32(r, 8, w->opid);
    if (!ob(w, 0x780)) {                                            /* CMiscInit::InitToPostText (reals = post text entries 12-21) */
        for (int i = 0; i < 10; i++) WD(r, 0x18 + 8 * i, 0);       /* cam_postvals.c pv_misc_value: 0 for 12-21 */
    } else ocp(w, r, 0x18, 0x7b0, 80);
    put(w, r);
}
static void wr_1012(W *w, double (*misc)(int)) {                   /* @18008e3d0 */
    uint8_t r[NB]; rec0(w, r, 0x3f4); W32(r, 8, w->opid);
    if (!ob(w, 0x780)) { for (int i = 0; i < 10; i++) W32(r, 0x18 + 4 * i, (int32_t)misc(i + 1)); }   /* InitToPostText ints 1-10 */
    else ocp(w, r, 0x18, 0x784, 40);
    put(w, r);
}
static int16_t cc_control_gcode(int16_t type, int dir) {           /* cc_control_gcode_2 @18009dde0 */
    if ((uint16_t)(type - 1) < 2) return dir == 0 ? 0x29 : 0x2a;
    if (type != 3) return 0;
    return dir != 0 ? 0x29 : 0x2a;
}
static int16_t cc_computer_gcode(int16_t type, int dir) {          /* cc_computer_gcode_2 @18009dce0 */
    if ((type & 0xfffc) == 0 && type != 1) return dir == 0 ? 0x29 : 0x2a;
    return 0;
}
static int op_is_point(int c) {                                     /* @1800c6f80 (decomp/mccore_toolfilt.c) */
    unsigned u = (unsigned)(c - 100);
    return op_is_drill(c) || c == 0x12 || (u <= 0xd && ((0x2041u >> (u & 0x1f)) & 1));
}
/* op_is_tip_comp @1800c7380 (decomp/mccore_op_is_tip_comp.c): the op's tip-comp byte; op 0x13 / op_is_point ones not listed
   before them (and every 'old NCI' type) point at the static comp_to_tip = 1 */
static int tip_comp(W *w) {
    int c = w->opcode;
    if (op_is_msurf(c)) return ob(w, c == 0x84 ? 0x290 : 0x11d1);
    if (op_is_oldnci(c)) return 1;
    if (op_is_drill(c)) return ob(w, 0x1200);
    if (op_is_contour(c) || c == 0x82 || c == 3) return ob(w, 0x11c9);
    if ((uint16_t)(c - 0x66) < 0x21 && ((0x100000019ULL >> ((c - 0x66) & 0x3f)) & 1)) return ob(w, 0x290);
    if (c == 0x12) return ob(w, 0x11d0);
    if (c == 0x13 || op_is_point(c)) return 1;
    if (c == 0x15) return ob(w, 0x121a);
    if (c == 0x16) return ob(w, 0x1210);
    if (c == 0x1b || c == 0x19 || c == 0x1a) return ob(w, 0x11d8);
    if (c == 0x18) return ob(w, 0x11d0);
    if (c == 0x65 || c == 0x96) return ob(w, 0x290);
    if (c == 0x87) return ob(w, 0x11c9);
    return -1;                                                      /* NULL (op_is_chook ops: not made here) */
}
static void wr_1013(W *w, int16_t fn_a1ea0) {                       /* @18008e540 */
    uint8_t r[NB]; rec0(w, r, 0x3f5); W32(r, 8, w->opid);
    int tc = tip_comp(w); W16(r, 0x30, (int16_t)(tc == 1));
    if (op_is_oldnci(w->opcode)) ND("1013 of an 'old NCI' operation type (FUN_1800a5a90)");
    W16(r, 0x18, w->opcode == 3 ? 0 : cc_control_gcode(oh(w, 0x568), ob(w, 0x56a)));   /* pocket: pock_in_finish = 0 at the section start */
    ocp(w, r, 0x20, 0x2e8, 16);
    /* the tool library path op +0x3ae (FUN_1800db520 fills an empty one / a missing file - NOT DECODED: kept); "" -> "NONE" */
    char lib[0x106]; size_t n = 0; while (n < 0x105 && ob(w, 0x3ae + (int)n)) { lib[n] = (char)ob(w, 0x3ae + (int)n); n++; } lib[n] = 0;
    if (!lib[0]) { ND("1013 tool library path (FUN_1800db520)"); snprintf(lib, sizeof lib, "NONE"); }
    mbcpy(r + 0x52, lib, 0x101);
    W32(r, 0x154, oi(w, 0x550));
    uint8_t b = ob(w, 0x54c); r[0x158] = b > 3 ? 0 : b;
    int v = find_view(w, 0x578, 0x572);
    W16(r, 0x32, (int16_t)v);
    double org[3] = { od(w, 0x5c0), od(w, 0x5c8), od(w, 0x5d0) }, ov[3]; world_to_view(w, v, org, ov, 0x578);
    WD(r, 0x38, ov[0]); WD(r, 0x40, ov[1]); WD(r, 0x48, ov[2]);
    W16(r, 0x50, fn_a1ea0);
    put(w, r);
}
static void wr_m33(W *w, int gcode, int moff) {                    /* 1014 @18008eb40 (Tplane +0x578) / 1017 @18008f1c0 (Cplane +0x628) */
    uint8_t r[NB]; rec0(w, r, gcode); W32(r, 8, w->opid);
    if (op_is_oldnci(w->opcode)) ND("1014 / 1017 of an 'old NCI' operation type (the active view)");
    if (gcode == 0x3f9 && w->opcode == 3) moff = 0x578;             /* pocket: op +0x620 = op +0x570 (the Tplane view) first */
    ocp(w, r, 0x18, moff, 72);
    put(w, r);
}
static void wr_1016(W *w, int metric) {                             /* @18008ed40 */
    uint8_t r[NB]; rec0(w, r, 0x3f8); W32(r, 8, w->opid);
    W32(r, 0x18, w->opid); W16(r, 0x1c, oh(w, 0x2e4)); W16(r, 0x1e, (int16_t)w->opcode);
    W16(r, 0x40, w->opcode == 3 ? 0 : cc_computer_gcode(oh(w, 0x568), ob(w, 0x56a)));
    int32_t wo = oh(w, 0x602); if (wo < -1) wo = -1;                /* FUN_18011be20(&w, -1) */
    W16(r, 0x42, (int16_t)wo); W16(r, 0x44, (int16_t)(metric != 0)); W16(r, 0x46, oh(w, 0x332));
    W16(r, 0x48, ob(w, 0x541)); ocp(w, r, 0x50, 0x2f8, 8); W32(r, 0x58, oi(w, 0x53c)); W16(r, 0x5c, ob(w, 0x540));
    W32(r, 0x60, oi(w, 0x544)); W32(r, 0x64, 0); W16(r, 0x68, oh(w, 0x5d8)); W16(r, 0x6a, oh(w, 0xb90));
    /* TapPitchCalculate @18007e500: 0 unless a drill-type op (the tool's TlToolTap pitch from the tool system: [ND]) */
    if (op_is_drill(w->opcode) || w->opcode == 0x40 || w->opcode == 0x68 || w->opcode == 0x88) ND("1016 tap pitch (TapPitchCalculate)");
    WD(r, 0x70, 0);
    if (op_is_oldnci(w->opcode)) ND("1016 of an 'old NCI' operation type");
    int pock = w->opcode == 3;                                      /* op +0x620.. = op +0x570.. (the Tplane view) */
    int v = find_view(w, pock ? 0x578 : 0x628, pock ? 0x572 : 0x622);   /* ms_tp_type 0x2e (5-axis) uses op +0x6d8: not made here */
    W16(r, 0x20, (int16_t)v);
    double org[3] = { od(w, pock ? 0x5c0 : 0x670), od(w, pock ? 0x5c8 : 0x678), od(w, pock ? 0x5d0 : 0x680) }, ov[3];
    world_to_view(w, v, org, ov, pock ? 0x578 : 0x628);
    WD(r, 0x28, ov[0]); WD(r, 0x30, ov[1]); WD(r, 0x38, ov[2]);
    if (!op_is_xform(w->opcode)) { W32(r, 0x64, w->opid); W32(r, 0x78, w->opid); }
    put(w, r);
}
static void wr_1025(W *w) {                                         /* @18008f9a0 (bnci_wr_sof: op[0xae8], op + 0xaea) */
    uint8_t r[NB]; rec0(w, r, 0x401);
    if (ob(w, 0xae8)) ocp(w, r, 0x18, 0xaea, 40);
    put(w, r);
}
static void wr_1027(W *w) {                                         /* @18008fad0 */
    uint8_t r[NB]; rec0(w, r, 0x403); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, w->opid);
    if (op_is_oldnci(w->opcode)) ND("1027 of an 'old NCI' operation type");
    ocp(w, r, 0x18, 0x6d8, 72);
    int v = find_view(w, 0x6d8, 0x6d2);
    double org[3] = { od(w, 0x720), od(w, 0x728), od(w, 0x730) }, ov[3];
    world_to_view(w, v, org, ov, 0x6d8);
    WD(r, 0x60, ov[0]); WD(r, 0x68, ov[1]); WD(r, 0x70, ov[2]);
    put(w, r);
}
static void wr_1028(W *w) {                                         /* @18008fd30: the defaults (no head / axis combination ents) */
    uint8_t r[NB]; rec0(w, r, 0x404); W32(r, 8, w->opid);
    if (oi(w, 0xa0) || oi(w, 0xb0)) ND("1028 / 1029 head and axis-combination components (get_assoc_ent_ent_idn 0x12 / 0x10)");
    WD(r, 0x30, 1.0); WD(r, 0x48, 1.0);                             /* local_7f78 / local_7f60 = 1.0 */
    put(w, r);
}
static void wr_1029(W *w) {                                         /* @1800900f0: the defaults */
    uint8_t r[NB]; rec0(w, r, 0x405); W32(r, 8, w->opid);
    put(w, r);
}
static void wr_1020(W *w) {                                         /* @18008f6a0 */
    uint8_t r[NB]; rec0(w, r, 0x3fc); W32(r, 8, w->opid);
    const uint8_t *g = group_by_id(w->P, oi(w, 0x554));            /* OMgetOpMachineGroup(op id): op +0x554 (record +0x5bc) */
    if (!g) { ND("1020 machine group not found (stock fields 0)"); }
    else { memcpy(r + 0x18, g + 0x1230, 48); memcpy(r + 0x48, g + 0x1690, 2); }
    double v[3] = { od(w, 0x978) - od(w, 0x960), od(w, 0x980) - od(w, 0x968), od(w, 0x988) - od(w, 0x970) };
    if (ob(w, 0x958) && (uint16_t)(oh(w, 0x95a) - 2) < 2 && fabs(v[0]) < 1e-12 && fabs(v[1]) < 1e-12 && fabs(v[2]) < 1e-12) {
        int16_t a = oh(w, 0x95c); if (a == 1) v[0] = 1; else if (a == 2) v[1] = 1; else if (a == 3) v[2] = 1; }
    WD(r, 0x50, v[0]); WD(r, 0x58, v[1]); WD(r, 0x60, v[2]);
    W32(r, 0x70, (int32_t)(int8_t)ob(w, 0x289));
    int32_t rpm = 0; if (max_tool_rpm(w->P, w->op, w->opn, &rpm)) W32(r, 0x74, rpm); else ND("1020 MaxToolSpindleRPM");
    /* OMgetOpMaterialName: the machine group's material +0x11d0; TpMatList::MaterialByName [ND: the part's material table is
       not read - a non-empty name is taken as found]; none -> "NONE" */
    const uint8_t *mg = g;
    const char *mat = mg && mg[0x11d0] ? (const char *)mg + 0x11d0 : "NONE";
    char mb[0x52]; snprintf(mb, sizeof mb, "%.*s", 0x50, mat);
    mbcpy(r + 0x96, mb, 0x50);
    put(w, r);
}
static void wr_tool(W *w) {                                          /* bnci_wr_tool @180098ca0 (param_2 = op id) */
    /* T = the tool's ent image (the internal tool system's vtable +0x168 fill, ours: the part's TL_ID record of slot op +0x544);
       T +0x34 = its ent idn (+8 of the tool records) */
    uint8_t r[NB]; int32_t idn = w->tl && w->tln >= 0x38 ? I32(w->tl, 0x34) : 0;
    if (!w->tl) ND("the tool's TL_ID record (20001-20009 tool fields 0)");
    rec0(w, r, 20001); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, idn);   /* tool comment: op +0x336 */
    mbcpy_op(w, r + 0x18, 0x336, 0x77); put(w, r);
    rec0(w, r, 20002); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, idn);   /* mfg: T +0x340 */
    mbcpy_tl(w, r + 0x18, 0x340, 0x77); put(w, r);
    rec0(w, r, 20003); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, idn);   /* chuck: T +0x3b8 */
    mbcpy_tl(w, r + 0x18, 0x3b8, 0x77); put(w, r);
    rec0(w, r, 20004); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, idn);   /* tool info 1 */
    tcp(w, r, 0x18, 0x68, 4); tcp(w, r, 0x1c, 0x6c, 2); tcp(w, r, 0x1e, 0xbc, 2); tcp(w, r, 0x20, 0x6e, 2);
    tcp(w, r, 0x28, 0x70, 8); tcp(w, r, 0x30, 0x78, 8); tcp(w, r, 0x38, 0x80, 8); tcp(w, r, 0x40, 0x88, 8);
    tcp(w, r, 0x50, 0x98, 8); tcp(w, r, 0x58, 0xa0, 8); tcp(w, r, 0x60, 0xa8, 8); tcp(w, r, 0x68, 0xb0, 4); tcp(w, r, 0x6c, 0xb8, 2);
    tcp(w, r, 0x6e, 0xba, 2); tcp(w, r, 0x70, 0x4d8, 8); tcp(w, r, 0x78, 0x4e0, 8);
    ND("20004 / 20006 / 20007 tool-assembly fields (TlAssembly components, holders)");
    put(w, r);
    /* 20005 (a drill tool found in the tool system, Cnc::Tool::is_drill_tool(T +0x6c)) / 20006 (else) */
    int ttype = (int16_t)(tb(w, 0x6c) | tb(w, 0x6d) << 8);
    int drill = ttype >= 1 && ttype <= 9;                         /* [ND] is_drill_tool: the drill-family types 1..9 (tooltypes_mill.tsv) */
    rec0(w, r, drill ? 20005 : 20006); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, idn);
    tcp(w, r, 0x18, 0x438, 2); tcp(w, r, 0x20, 0x440, 8); tcp(w, r, 0x28, 0x448, 8); tcp(w, r, 0x30, 0x450, 8); tcp(w, r, 0x38, 0x458, 8);
    if (!drill) tcp(w, r, 0x40, 0x460, 8);
    tcp(w, r, 0x48, 0x468, 8); tcp(w, r, 0x50, 0x470, 8);
    if (drill) tcp(w, r, 0x58, 0x478, 8);
    put(w, r);
    rec0(w, r, 20007); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, idn);   /* tool info 3 */
    for (int k = 0; k < 7; k++) tcp(w, r, 0x18 + 8 * k, 0x2f0 + 8 * k, 8);
    tcp(w, r, 0x50, 0x328, 2); W16(r, 0x68, (int16_t)(int8_t)tb(w, 0x2c0));
    put(w, r);
    rec0(w, r, 20008); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, oi(w, 0xa0));   /* head info 4 */
    if (oi(w, 0xa0)) ND("20008 head component (get_assoc_ent_ent_idn 0x10)");
    WD(r, 0x28, 1.0);
    put(w, r);
    rec0(w, r, 20009); W32(r, 8, idn);                              /* tool info 5: flag 0 */
    W32(r, 0x18, oi(w, 0x550)); r[0x1c] = ob(w, 0x54c);
    put(w, r);
    rec0(w, r, 20020); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, idn);   /* the main holder's name [ND: tool system] */
    put(w, r);
    rec0(w, r, 20021); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, idn);   /* the assembly's name [ND: tool system] */
    ND("20020 / 20021 holder and assembly names (TlAssembly)");
    put(w, r);
}
static void wr_views(W *w) {                                         /* bnci_wr_views @180099bf0: one buffer, stale bytes kept */
    uint8_t r[NB]; memset(r, 0, NB);
    r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 4, w->opid); W32(r, 8, w->opid);
    static const int VOFF[3] = { 0x688, 0x5d8, 0x738 };             /* Cplane / Tplane / WCS plane ids */
    for (int k = 0; k < 3; k++) {
        const McamPlaneInfo *p = plane_by_id(w->P, oh(w, VOFF[k]));
        if (!p) p = plane_by_id(w->P, 1);
        if (!w->P->npl) ND("the part's planes archive (plane names)");
        if (!p || !p->b360) continue;
        W16(r, 0, (int16_t)(20010 + 2 * k)); mbcpy(r + 0x18, p->name, 0x78); put(w, r);
        W16(r, 0, (int16_t)(20011 + 2 * k)); mbcpy(r + 0x18, p->s358, 0x78); put(w, r);
    }
    const uint8_t *mg = machine_group_of(w->P, oi(w, 0x28c));
    if (mg) {
        const char *mat = (const char *)mg + 0x11d0;
        W16(r, 0, 20016); mbcpy(r + 0x18, mat, 0x78); put(w, r);
        /* the material's comment: TpMatList::MaterialByName(name) +0x50, else "" [ND: the material table is not read: ""] */
        if (mat[0]) ND("20017 material comment (the part's material table)");
        W16(r, 0, 20017); mbcpy(r + 0x18, "", 0x78); put(w, r);
        memset(r, 0, NB); W16(r, 0, 20018); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 4, w->opid);   /* +8 not set again: 0 */
        const uint8_t *g = group_by_id(w->P, oi(w, 0x28c));
        char t[0x400] = "";
        if (g) { snprintf(t, sizeof t, "%.*s", 0x100, (const char *)g + 4);
                 if (g[0x980]) { size_t L = strlen(t); snprintf(t + L, sizeof t - L, ": %.*s", 0x300, (const char *)g + 0x980); } }   /* ": " @0x180cff780 */
        mbcpy(r + 0x18, t, 0x78); put(w, r);
        W16(r, 0, 20019); mbcpy(r + 0x18, "", 0x27); put(w, r);     /* op_is_wire: DAT_1813106d0; else "" */
    }
}
static void wr_20800(W *w) {                                         /* @180090ba0 */
    uint8_t r[NB]; rec0(w, r, 20800); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 8, w->opid);
    double v = 0; int c = w->opcode;                                /* FUN_1800a1280 */
    if (c == 0x1d || c == 0x6f) v = od(w, 0x14a0); else if (c == 0x2d || c == 0x70) v = od(w, 0x1518); else if (c == 0x2e) v = od(w, 0x1570);
    else if (c == 0x30) v = od(w, 0x14f0); else if (c == 0x6e) v = od(w, 0x1638); else if (c == 0x71) v = od(w, 0x1440);
    WD(r, 0x18, v);
    put(w, r);
}
static void wr_1002(W *w, const double first[3]) {                  /* @18008d7c0 */
    uint8_t r[NB]; rec0(w, r, 0x3ea); W32(r, 8, w->opid);
    W32(r, 0x18, oi(w, 0xf8)); W32(r, 0x1c, oi(w, 0xfc)); W32(r, 0x20, oi(w, 0x100)); W32(r, 0x24, oi(w, 0x2e0));
    W32(r, 0x28, oi(w, 0x308)); W32(r, 0x2c, oi(w, 0x30c));
    /* +0x30: view_to_plane(Cplane view, Tplane view) when the views differ (or its result >= 0) [ND: view_to_plane; equal views -> 0] */
    int tv = find_view(w, 0x578, 0x572), cv = find_view(w, 0x628, 0x622);
    if (tv != cv) ND("1002 plane of a Cplane other than the Tplane (view_to_plane)");
    W16(r, 0x30, 0);
    if (w->opcode == 0x86 || w->opcode == 0x84) ND("1002 rpm of a Dynamic op with an entry spindle speed (dynEntry)");
    W32(r, 0x38, oi(w, 0x328)); ocp(w, r, 0x40, 0x310, 8); W16(r, 0x48, oh(w, 0x330));
    WD(r, 0x50, first[0]); WD(r, 0x58, first[1]); WD(r, 0x60, first[2]);
    /* FUN_1800a0d70 (mill, not a transform op): make_reals_0(world_to_view(home op +0x870 - Tplane origin op +0x5c0, view op +0x572)) */
    double h[3] = { od(w, 0x870) - od(w, 0x5c0), od(w, 0x878) - od(w, 0x5c8), od(w, 0x880) - od(w, 0x5d0) }, hv[3];
    world_to_view(w, oh(w, 0x572), h, hv, 0x578); make_reals_0(hv);
    WD(r, 0x68, hv[0]); WD(r, 0x70, hv[1]); WD(r, 0x78, hv[2]);
    int16_t ra = 0;                                                 /* rotary_axis_op_param_to_bnci(op + 0x958) */
    if (ob(w, 0x958)) {
        int16_t t = oh(w, 0x95a), ax = oh(w, 0x95c);
        if (t == 1) ra = oh(w, 0x99a) == 1 ? (oh(w, 0x998) == 1 ? 1 : -1) : (oh(w, 0x998) != 1 ? -2 : 2);
        else if (t == 2) ra = ax == 1 ? 0x15 : ax == 2 ? 0x16 : ax == 3 ? 0x17 : 0;
        else if (t == 3) ra = ax == 1 ? 0xb : ax == 2 ? 0xc : ax == 3 ? 0xd : 0;
    }
    W16(r, 0x80, ra);
    if (ob(w, 0x958) && oh(w, 0x95a) == 1) ocp(w, r, 0x88, 0x990, 8);
    r[0x35] = ob(w, 0xc1c);
    put(w, r);
}
/* bnci_wr_0 @18008d6b0 (ent NULL): the drill op's first rapid */
static void wr_0(W *w, const double p[3]) {
    uint8_t r[NB]; rec0(w, r, 0); WD(r, 0x20, p[0]); WD(r, 0x28, p[1]); WD(r, 0x30, p[2]); WD(r, 0x38, -2.0); put(w, r);
}

/* FUN_1800a1ea0(opcode) @1800a1ea0 (decomp/nciw/core_misc.c): the byte table @0x1800a20f4 (opcode - 1, 0x132 entries, read from
   mccore.dll) through the switch's values; the opcodes not listed give 0 */
static int16_t fn_a1ea0(int op) {
    static const int16_t T[][2] = { {1,2},{2,3},{3,4},{4,15},{5,14},{6,14},{7,14},{8,14},{9,14},{10,14},{11,13},{12,13},{13,13},{14,13},
        {15,13},{18,2},{19,15},{21,5},{22,8},{23,2},{24,6},{25,7},{26,10},{27,9},{28,16},{29,17},{30,2},{31,2},{32,2},{33,2},{34,2},{35,2},
        {36,2},{37,3},{38,4},{39,13},{40,13},{41,13},{42,13},{43,13},{44,14},{45,13},{46,13},{48,18},{50,2},{51,2},{52,2},{53,2},{54,3},
        {55,3},{56,3},{57,3},{60,101},{61,102},{62,103},{63,104},{64,105},{65,106},{66,102},{67,102},{68,103},{71,101},{72,101},{80,15},
        {100,2},{102,19},{105,2},{106,2},{107,14},{108,15},{109,13},{110,20},{111,21},{112,22},{113,23},{130,2},{131,14},{132,13},{133,2},
        {134,2},{135,2},{150,25},{201,102},{202,101},{206,102},{208,103},{209,102},{210,101},{211,102},{212,103},{214,107},{215,108},
        {216,109},{217,110},{218,111},{219,112},{220,101},{224,101},{225,101},{226,114},{306,3} };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) if (T[i][0] == op) return T[i][1];
    return 0;
}
static double misc_default(int idx) { return idx == 1 ? 2 : 0; }   /* cam_postvals.c pv_misc_value (batch 11: the post text's "//2") */
static void sof(W *w, const double first[3]) {                     /* bnci_wr_sof @180096f30 */
    wr_1011(w); wr_1012(w, misc_default);
    wr_1013(w, fn_a1ea0(w->opcode));
    wr_m33(w, 0x3f6, 0x578); wr_1016(w, w->o->metric); wr_m33(w, 0x3f9, 0x628);
    wr_1025(w); wr_1027(w);
    if (!op_is_lathe(w->opcode) && !op_is_wire(w->opcode)) { wr_1028(w); wr_1029(w); }
    wr_1020(w);
    if (!op_is_lathe(w->opcode) && !op_is_wire(w->opcode)) wr_tool(w);
    wr_views(w);
    if (op_is_router(w->opcode)) ND("20300 block drill tool info (router ops)");
    wr_20800(w);
    wr_1002(w, first);
    if (op_is_drill(w->opcode) && !op_is_multiax(w->opcode) && !ob(w, 0x150)) wr_0(w, first);
    /* FUN_180096820(op, 1): the approach reference point (op[0x150]) - [ND] wpt_to_op not ported */
    if (!op_is_multiax(w->opcode) && !op_is_no_refpt(w->opcode) && !op_is_lathe(w->opcode) && ob(w, 0x150)) ND("the approach reference point (FUN_180096820)");
}
static void w_init(W *w, const NciwPart *P, const NciwOp *o, NciwBuf *out, NciwStats *st) {
    memset(w, 0, sizeof *w); w->P = P; w->o = o; w->out = out; w->st = st;
    w->op = o->rec && o->recn > 0x68 ? o->rec + 0x68 : NULL; w->opn = w->op ? o->recn - 0x68 : 0;
    w->opid = w->op ? I32(w->op, 0) : 0; w->opcode = w->op ? (uint16_t)I16(w->op, 4) : 0;
    w->tl = o->tl; w->tln = o->tln;
    if (!w->tl && w->op) w->tl = nciw_part_tl(P, oi(w, 0x544), &w->tln);
}
static void sos(W *w) {                                             /* bnci_wr_sos @180097170 (+0x18 = 1: the section is valid) */
    uint8_t r[NB]; memset(r, 0, NB);
    W16(r, 0, -2); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 4, w->opid); r[0x18] = 1; W32(r, 0x1c, w->opid);
    W16(r, 0x20, (int16_t)w->opcode); W32(r, 0x24, I32(w->o->rec, 0x30));
    put(w, r);
}
void nciw_sof(const NciwPart *P, const NciwOp *o, const double first[3], NciwBuf *out, NciwStats *st) {
    W w; w_init(&w, P, o, out, st); sos(&w); sof(&w, first);
}

/* ------------------------------------------------------------------------------------------------ drill (mcmill bnci_drill) */
/* Sources: mcmill bnci_drill @180067cf0 / bnci_drill_depth @1800694d0 / get_drill_direction @18006f650 / drill_view_to_op_view
   @18006d620 / tp_ent_is_control @1800799c0 (decomp/mcmill_all.c l.69483-70440, 73106, 74498, 81976); mccore bnci_wr_80 @1800910d0,
   bnci_wr_81 @1800911d0, bnci_wr_82 @1800914d0, bnci_wr_100 @18008df60, bnci_wr_lin @180094e50 (decomp/nciw/core_wr.c, core_misc.c),
   op_is_opid @1800c6f30 (decomp/ncidrill/core_opid.c). bnci_drill's ent offsets are the op's + 0x68 (written here as op offsets).
   The path ported: opcode 2 (Drill), points (tp kind 1) - from geometry or a bolt circle / grid (view-dependent) - with their own
   depths / feeds / retract / clearance / initial heights (tp +0xd8), canned text (tp +0xed: 1025) and manual entries (tp +0x163:
   1005 / 1006 / 1007 / 1026). Refused (formats/nci_writer.md 7): a rotary axis (op +0x958), subprograms (+0x1298), points from another
   op (op_is_opid: +0x28b), the approach point (+0x150), entities other than points, a view-dependent point with tp +0xec. */
#define DTOL (mc_systol.tol)                                        /* tol_exref (config 1000: McSysTol, mcam_load.h) */
static int dne(double a, double b) { return fabs(a - b) >= DTOL; }  /* the decompile's ABS(tol) < ABS(a - b) || == */
static int d_unset(double v, double s) { return fabs(v - s) < DTOL; }   /* ABS(v - s) <= tol && != tol */
static double d_ovr(double v, double dflt) { return v == 0.0 || v >= 1e38 || v <= -1e38 ? dflt : v; }   /* a point's own value */
static int32_t rec_idn(const uint8_t *e) { return I32(e, 0x34); }  /* ent +0x34 (the record's ent idn) */

static void wr_80(W *w) {                                           /* bnci_wr_80: +8 the op's ent idn */
    uint8_t r[NB]; memset(r, 0, NB); W16(r, 0, 0x50); W32(r, 4, w->opid); W32(r, 8, rec_idn(w->o->rec)); put(w, r);
}
static void wr_82(W *w, const double *v) {                          /* bnci_wr_82: the 10 custom drill parameters */
    uint8_t r[NB]; memset(r, 0, NB); W16(r, 0, 0x52); W32(r, 4, w->opid); W32(r, 8, rec_idn(w->o->rec));
    for (int k = 0; k < 10; k++) WD(r, 0x18 + 8 * k, v ? v[k] : 0);
    put(w, r);
}
static void wr_rapid(W *w, const double p[3]) {                     /* bnci_wr_lin(0, ent, 0, -2.0, p, 0): +0xc FUN_18007bc90 */
    uint8_t r[NB]; memset(r, 0, NB); W32(r, 4, w->opid); r[0xc] = (uint8_t)rec_flag(w->opcode);
    WD(r, 0x20, p[0]); WD(r, 0x28, p[1]); WD(r, 0x30, p[2]); WD(r, 0x38, -2.0); put(w, r);
}
/* bnci_wr_1005 @18008dc90 (decomp/nciw/core_wr.c l.5250): one record of gcode G per line of TEXT (lines end at '\n' / '\r', a run
   of them is one break; the text's end gives the last, possibly empty, line): +4 / +8 op id, +0xc FUN_18007bc90(op +4) (objdump
   0x18008dd0b: ecx = op +4), the line at +0x18 (_mbsnbcpy_s, 0x2ee bytes at most). The record is cleared once before the loop:
   bytes of a longer earlier line stay after a shorter one's NUL (as the 20010-20019 buffer, section 3 of formats/nci_writer.md) */
static void wr_1005(W *w, int g, const uint8_t *text, size_t textn) {
    uint8_t r[NB]; memset(r, 0, NB); W16(r, 0, (int16_t)g); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 4, w->opid); W32(r, 8, w->opid);
    size_t k = 0; int end = 0;
    do {
        uint8_t line[0x2ef]; size_t n = 0;
        while (k < textn && text[k] != '\n' && text[k] != '\r' && text[k] && n < 0x2ee) line[n++] = text[k++];
        if (k >= textn || !text[k]) end = 1;
        else while (k < textn && (text[k] == '\n' || text[k] == '\r')) k++;
        memcpy(r + 0x18, line, n); r[0x18 + n] = 0;
        put(w, r);
    } while (!end);
}
/* FUN_18009d8a0 @18009d8a0 (decomp/nciw/core_misc.c l.196; its 5th argument is the stack byte the callers set: tp[0x2c6]): a
   point's manual entry tp +0x164 when tp[0x163]: position tp +0x2c4 0 / 2 -> 1005 (0x3ed; 1006 0x3ee with tp[0x2c6]), 1 -> 1007
   (0x3ef; 1026 0x402 with tp[0x2c6]), any other position -> 1005 */
static void pt_text(W *w, const uint8_t *tp) {
    if (!tp[0x163]) return;
    int16_t pos = I16(tp, 0x2c4); int g = 0x3ed;
    if (pos == 1) g = tp[0x2c6] ? 0x402 : 0x3ef;
    else if ((pos == 0 || pos == 2) && tp[0x2c6]) g = 0x3ee;
    wr_1005(w, g, tp + 0x164, 0x2c4 - 0x164);
}
/* bnci_wr_81 / bnci_wr_100 call FUN_18009d8a0 before their records when (u16)tp +0x2c4 < 2, after them when it is 2 */
static void pt_text_before(W *w, const uint8_t *tp) { if (tp[0x163] && (uint16_t)I16(tp, 0x2c4) < 2) pt_text(w, tp); }
static void pt_text_after(W *w, const uint8_t *tp) { if (tp[0x163] && I16(tp, 0x2c4) == 2) pt_text(w, tp); }
/* bnci_wr_1025(op, 1, tp + 0xee) @18008f9a0: a point's own canned text (bnci_drill: when tp[0xed], before its 81 / 100) */
static void wr_1025_pt(W *w, const uint8_t *tp) {
    uint8_t r[NB]; rec0(w, r, 0x401); memcpy(r + 0x18, tp + 0xee, 40); put(w, r);
}
/* bnci_wr_0(ent, 0, p, 0) @18008d6b0 with a tp entity: written only when ent +0x74 is 1, 2 or 4 ((v - 1) & ~3 == 0, v != 3);
   +4 cur_op, +8 ent +0x34, +0x20 p, +0x38 -2.0 (+0xc 0) */
static void wr_0_ent(W *w, const uint8_t *tp, const double p[3]) {
    int32_t v = I32(tp, 0x74);
    if (((uint32_t)(v - 1) & 0xfffffffcu) != 0 || v == 3) return;
    uint8_t r[NB]; rec0(w, r, 0); W32(r, 8, I32(tp, 0x34));
    WD(r, 0x20, p[0]); WD(r, 0x28, p[1]); WD(r, 0x30, p[2]); WD(r, 0x38, -2.0); put(w, r);
}
/* bnci_wr_81(ent, p, q, idn, B, R, top, pz, custom, tp +0x150, tp +0x158, tp +0x160, tp[0x162], tp[0x163], ..): 82 first, then 81 */
static void wr_81(W *w, const uint8_t *tp, const double p[3], const double q[3], double B, double R, double top, double pz) {
    double cust[10];
    if (ob(w, 0x1258)) for (int k = 0; k < 10; k++) cust[k] = od(w, 0x1208 + 8 * k);   /* ent[0x12c0] ? ent +0x1270 : zeros */
    pt_text_before(w, tp);                                          /* core_wr.c l.1680-1683 */
    wr_82(w, ob(w, 0x1258) ? cust : NULL);
    uint8_t r[NB]; memset(r, 0, NB); W16(r, 0, 0x51); W32(r, 4, w->opid); W32(r, 8, rec_idn(tp));
    W16(r, 0x18, oh(w, 0x11c0));                                    /* the cycle (ent +0x1228) */
    WD(r, 0x20, p[0]); WD(r, 0x28, p[1]); WD(r, 0x30, p[2]);
    double v158 = D(tp, 0x158), v150 = D(tp, 0x150);
    WD(r, 0x38, v158 <= -1e38 ? od(w, 0x11e8) : v158);             /* ent +0x1250 */
    WD(r, 0x40, v150 <= -1e38 || (v150 < DTOL && -DTOL < v150) ? od(w, 0x310) : v150);   /* the feed (ent +0x378) */
    ocp(w, r, 0x48, 0x11c8, 0x20);                                  /* ent +0x1230 .. +0x124f */
    WD(r, 0x68, B - pz); WD(r, 0x70, R - pz); WD(r, 0x78, top - pz);
    ocp(w, r, 0x80, 0x11f0, 8);                                     /* ent +0x1258 */
    WD(r, 0x88, q[0]); WD(r, 0x90, q[1]); WD(r, 0x98, q[2]);
    int16_t f = I16(tp, 0x160); W16(r, 0xa0, (int16_t)(f != -1 ? (f | 0x4001) : 0x4001));
    WD(r, 0xa8, 0.0);
    put(w, r);
    pt_text_after(w, tp);                                           /* core_wr.c l.1717-1723 */
}
/* bnci_wr_100(ent, p, q, p.z, B, R, pz, tp +0x150, tp +0x158, tp +0x160, tp[0x162], tp[0x163], .., idn, last, top, Tplane matrix, 0) */
static void wr_100(W *w, const uint8_t *tp, const double p[3], const double q[3], double B, double R, double pz, int16_t last, double top) {
    pt_text_before(w, tp);                                          /* core_wr.c l.807-810 */
    uint8_t r[NB]; memset(r, 0, NB); W16(r, 0, 100); W32(r, 4, w->opid); W32(r, 8, rec_idn(tp));
    WD(r, 0x20, p[0]); WD(r, 0x28, p[1]); WD(r, 0x30, p[2]);
    WD(r, 0x38, B - pz); WD(r, 0x40, R - pz);
    double v158 = D(tp, 0x158), v150 = D(tp, 0x150);
    WD(r, 0x48, v158 <= -1e38 ? od(w, 0x11e8) : v158);
    WD(r, 0x50, v150 <= -1e38 ? od(w, 0x310) : v150);
    WD(r, 0x58, q[0]); WD(r, 0x60, q[1]); WD(r, 0x68, q[2]);
    int16_t f = I16(tp, 0x160), fl = tp[0x162] ? (int16_t)(last | 1) : last;
    if (f != -1) fl = (int16_t)(f | last);
    W16(r, 0x70, fl);
    WD(r, 0x78, top - pz);
    double m[9]; for (int k = 0; k < 9; k++) m[k] = od(w, 0x578 + 8 * k);   /* ent +0x5e0: the Tplane matrix */
    for (int k = 0; k < 9; k++) { if (fabs(m[k]) < 5e-5) m[k] = 0; WD(r, 0x80 + 8 * k, m[k]); }   /* make_reals_0(matrix) [ND tol: as make_reals_0] */
    WD(r, 0xc8, 0.0);
    put(w, r);
    pt_text_after(w, tp);                                           /* core_wr.c l.851-854 */
}
/* view_to_world(p, vno): the transpose of world_to_view (vno < 2: the identity) */
static void view_to_world(W *w, int vno, const double p[3], double out[3], int moff) {
    double m[9];
    if (vno < 2) { memcpy(out, p, 3 * sizeof *p); return; }
    if (!mcam_std_view(vno, m)) for (int k = 0; k < 9; k++) m[k] = od(w, moff + 8 * k);   /* [ND] a session view: the op's matrix */
    for (int i = 0; i < 3; i++) out[i] = m[i] * p[0] + m[3 + i] * p[1] + m[6 + i] * p[2];
}
/* tp_pt_is_viewdep(tp_point = tp + 0x98, 0) @18007a5e0 (decomp/mcmill_all.c l.82558): the point type char tp +0xb0 is 'B' (bolt
   circle) or 'G' (grid) and the short tp +0xe8 > 0 */
static int viewdep0(const uint8_t *tp) { return (tp[0xb0] == 'B' || tp[0xb0] == 'G') && I16(tp, 0xe8) > 0; }
/* tp_pt_is_viewdep(.., 1): tp +0xd0 (the source's eptr) 0 -> 0; get_ent_from_eptr fails -> the char test above; the source an ASSOC
   tp record (0x4000, sub 2) that tp_ent_is_control -> tp +0xe8 > 0; any other entity -> 0. Ours: get_ent_from_eptr = the part
   file's record whose ent +0x20 is the eptr. A source not in the file (a geometry entity made here, not saved yet) takes the char
   test as a missing one does: the save writes 'C' for every point made here (mcam_saveops.c), so 0 as Mastercam's geometry
   source. *amb is set when such a source meets a 'B' / 'G' point (the caller refuses) */
static int tp_kind_control(int16_t k) { return k == 2 || k == 3 || k == 5 || k == 6 || k == 0x16 || k == 0x17 || k == 0x29; }   /* tp_ent_is_control @1800799c0 */
static int viewdep1(W *w, const uint8_t *tp, int *amb) {
    int64_t e; memcpy(&e, tp + 0xd0, 8);
    if (!e) return 0;
    for (int i = 0; i < w->P->nent; i++) {
        const uint8_t *q = w->P->ent[i]; int64_t qe;
        memcpy(&qe, q + 0x20, 8);
        if (qe != e) continue;
        if (I32(q, 0) != 0x4000 || I32(q, 0x2c) != 2 || !tp_kind_control(I16(q, 0x90))) return 0;
        return I16(tp, 0xe8) > 0;
    }
    if (!w->o->tp) return viewdep0(tp);                             /* the part's own records: a missing source */
    if (viewdep0(tp)) *amb = 1;
    return 0;
}
/* the point in the Tplane (bnci_drill l.69757-69766, l.69825-69831): world = tp +0x98, a view-dependent point's view_to_world(tp +0x98,
   op +0x572); p = world_to_view(world - op +0x5c0, op +0x572) */
static void drill_pt(W *w, const uint8_t *tp, int vd, double out[3]) {
    double wp[3] = { D(tp, 0x98), D(tp, 0xa0), D(tp, 0xa8) };
    if (vd) { double v[3] = { wp[0], wp[1], wp[2] }; view_to_world(w, oh(w, 0x572), v, wp, 0x578); }
    double p[3] = { wp[0] - od(w, 0x5c0), wp[1] - od(w, 0x5c8), wp[2] - od(w, 0x5d0) };
    world_to_view(w, oh(w, 0x572), p, out, 0x578);
}
/* mccore tool_tip_length @1800a5440 (objdump 0x1800a54bc..0x1800a5501): D > tol and angle > tol: tan((90 - angle x 0.5) x K) x
   (D x 0.5), K = the double @0x180d018a8 = 0.01745329251994329 (one ulp under pi / 180: cam2d.c's
   cam_tool_tip_length uses pi / 180 - the depths differ in the last bit, CONTROL HORN.mcx-9 op 4) */
static double tip_length(double dia, double ang) {
    if (!(dia > DTOL) || !(ang > DTOL)) return 0;
    return tan((90.0 - ang * 0.5) * 0.01745329251994329) * (dia * 0.5);
}
/* bnci_drill_depth @1800694d0 (l.70302-70440; its top-of-stock argument is not read). Incremental (+0x140): + the point's z in the
   Tplane: world_to_view(tp +0x98 - op +0x5c0, op +0x572), a view-dependent point's tp +0xa8 as it is (l.70407-70421) */
static double drill_depth(W *w, const uint8_t *tp, int vd) {
    if (!d_unset(D(tp, 0xe0), 1e38)) return D(tp, 0xe0);            /* the point's own depth (tp_entity +0x78) */
    double z = od(w, 0x138);
    if (ob(w, 0x140)) {                                             /* incremental: from the point */
        if (vd) z += D(tp, 0xa8);
        else {
            double p[3] = { D(tp, 0x98) - od(w, 0x5c0), D(tp, 0xa0) - od(w, 0x5c8), D(tp, 0xa8) - od(w, 0x5d0) }, v[3];
            world_to_view(w, oh(w, 0x572), p, v, 0x578); z += v[2];
        }
    }
    double c = 0;
    if (ob(w, 0x1200)) {                                            /* tip comp: breakthrough, + the tip for drill tools but types 6-8 */
        int16_t tt = oh(w, 0x2e4);
        c = !tool_is_drill(tt) || (uint16_t)(tt - 6) < 3 ? od(w, 0x11f8) : tip_length(od(w, 0x2e8), od(w, 0x300)) + od(w, 0x11f8);
    }
    return z - c;
}
/* world_to_op(op, tp_entity, out) @1800a5690 (decomp/align/core_oplist.c l.4119): kinds 1 / 0x1b: a view-dependent point
   (tp_pt_is_viewdep(.., 0)) tp +0x98 - world_to_view(op +0x5c0, find_view_m(op +0x578)); else and kinds 2 / 3 wpt_to_op
   @1800a5950 (decomp/mccore_opview.c l.96): world_to_view(p - op +0x5c0, find_view_m(op +0x578)), make_reals_0. Kind 0x1c (a solid
   arc) is not ported (*nd set); any other kind leaves OUT as it is ("unknown pt id"). find_view_m resets op +0x572 to the view it
   finds when that differs and is > 1: ours keeps the op's (find_view returns the stored number unless a reserved view matches) */
static void world_to_op(W *w, const uint8_t *tp, double out[3], int *nd) {
    int16_t k = I16(tp, 0x90);
    int v = find_view(w, 0x578, 0x572);
    if ((k == 1 || k == 0x1b) && viewdep0(tp)) {
        double o[3] = { od(w, 0x5c0), od(w, 0x5c8), od(w, 0x5d0) }, ov[3];
        world_to_view(w, v, o, ov, 0x578);
        for (int j = 0; j < 3; j++) out[j] = D(tp, 0x98 + 8 * j) - ov[j];
    } else if (k == 1 || k == 0x1b || k == 2 || k == 3) {
        double p[3] = { D(tp, 0x98) - od(w, 0x5c0), D(tp, 0xa0) - od(w, 0x5c8), D(tp, 0xa8) - od(w, 0x5d0) };
        world_to_view(w, v, p, out, 0x578); make_reals_0(out);
    } else if (k == 0x1c) *nd = 1;
}
static int drill_section(W *w, char *err, size_t errn) {
    if (w->opcode != 2) { snprintf(err, errn, "drill operation type %d: only Drill (2) is ported (bnci_drill)", w->opcode); return 0; }
    /* bnci_drill l.69657-69673: op[0x958] with the rotary type +0x95a 2 / 3 (or FromMCExtensions with +0xc1d / +0xb98) -> FUN_1800698a0;
       type 1: unroll_x / unroll_y of every point (l.69767-69820) */
    if (ob(w, 0x958)) { snprintf(err, errn, "drill with a rotary axis: NOT DECODED (bnci_drill unroll / FUN_1800698a0)"); return 0; }
    /* op[0x1298] (ent +0x1300): set_n1018 / bnci_wr_1018 / bnci_wr_1019 subprogram records (local_2c63 / 2c65 / 2c5b paths) */
    if (ob(w, 0x1298)) { snprintf(err, errn, "drill subprogram: NOT DECODED (bnci_drill 1018 / 1019, set_n1018, subs_get_status)"); return 0; }
    /* op_is_opid: tp_get_just_opid + bnci_drill_section of the referenced ops (l.69683-69726, the local_2c50 record list) */
    if (ob(w, 0x28b)) { snprintf(err, errn, "drill points from another operation (op_is_opid): NOT DECODED"); return 0; }
    /* op[0x150] (ent +0x1b8): bnci_wr_sof's FUN_180096820 reference point (wpt_to_op of op +0x158) and bnci_drill's rapid at the first
       point when the clearance op +0x108 != 0 and <= its start height (l.69936-69942); ours: FUN_180096820's position test against the
       section start (op +0x80, DAT_1813755a4) is not decoded */
    if (ob(w, 0x150)) { snprintf(err, errn, "drill with an approach point (op +0x150): NOT DECODED (FUN_180096820)"); return 0; }
    /* the op's tp entities in list order: the save's (o->tp) or the part's records */
    const uint8_t **tps = NULL; int ntp = 0;
    if (w->o->tp) {
        for (uint32_t off = 0; off + 4 <= w->o->tpn; ) {
            uint32_t n = (uint32_t)I32(w->o->tp, (int)off); if (!n || off + 4 + n > w->o->tpn) break;
            const uint8_t *e = w->o->tp + off + 4;
            if (n >= 0x388 && I32(e, 0) == 0x4000 && I32(e, 0x2c) == 2 && I32(e, 0x68) == w->opid) { tps = realloc(tps, sizeof *tps * (size_t)(ntp + 1)); tps[ntp++] = e; }
            off += 4 + n;
        }
    } else for (int i = 0; i < w->P->npr; i++) {
        const PRec *q = &w->P->pr[i];
        if (q->n >= 0x388 && q->sub == 2 && I32(q->r, 0x68) == w->opid) { tps = realloc(tps, sizeof *tps * (size_t)(ntp + 1)); tps[ntp++] = q->r; }
    }
    int16_t last = -1;                                              /* local_2c24: the points (tp +0x90 == 1) - 1 */
    for (int i = 0; i < ntp; i++) if (I16(tps[i], 0x90) == 1) last++;
    /* which entities bnci_drill takes (l.69738-69742): not tp_ent_is_control and (tp_is_solid or not tp_pt_is_viewdep(.., 1) or
       !tp[0xec]). Kinds other than 1 (solid points 0x1b / solid arcs 0x1c: get_solid_arc_point @180070a10, tp_is_solid - mccore,
       not decompiled; lines / surfaces of the 5-axis drill) are refused */
    int *vd = calloc((size_t)(ntp ? ntp : 1), sizeof *vd), *use = calloc((size_t)(ntp ? ntp : 1), sizeof *use), npt = 0;
    for (int i = 0; i < ntp; i++) {
        int16_t k = I16(tps[i], 0x90);
        if (tp_kind_control(k)) continue;
        if (k != 1) { snprintf(err, errn, "drill: a drill entity of kind %d (not a point) is NOT DECODED here", k); goto refuse; }
        int amb = 0;
        vd[i] = viewdep1(w, tps[i], &amb);
        if (amb) { snprintf(err, errn, "drill: a bolt circle / grid point (tp +0xb0) whose source entity is not in the part file: NOT DECODED"); goto refuse; }
        if (vd[i] && tps[i][0xec]) { snprintf(err, errn, "drill: a view-dependent point with tp +0xec set (tp_is_solid, mccore): NOT DECODED"); goto refuse; }
        use[i] = 1; npt++;
    }
    if (!npt) { snprintf(err, errn, "drill: no points"); goto refuse; }

    /* bnci_drill's loop (l.69734-70226). c28 local_2c28 (points since the last cycle restart), c54 local_2c54 (no point taken yet),
       c48 local_2c48 (sof written), c5a local_2c5a (the point's own initial height), up / down local_2c67 / local_2c64, b78
       local_2b78 (the tool's height before a change). A dVar35 (start height), B dVar3 (the tool's height), C dVar32 (clearance),
       R dVar8 (retract), top dVar9, pz dVar1; cur local_2c18, prev local_2b58, q local_2c40. drill_view_to_op_view (non-rotary: the
       same view) leaves the points as they are. Not ported paths (refused above): local_2c63 / 2c68 / 2c65 (subprogram, opid) */
    int c28 = 0, c54 = 1, c48 = 0, up = 0, down = 0, ndw = 0;
    double A = 0, B = 0, C = 0, b78 = 0;
    double cur[3] = { 0, 0, 0 }, prev[3], q[3] = { 0, 0, 0 };
    for (int i = 0; i < ntp; i++) {
        const uint8_t *tp = tps[i];
        if (!use[i]) continue;
        c28++;
        const int cann = tp[0xed] != 0;                             /* local_2ba8 = tp[0xed] ? tp + 0xee : NULL */
        memcpy(prev, cur, sizeof prev);
        drill_pt(w, tp, vd[i], cur);
        double pz = cur[2];
        double top = od(w, 0x280); if (d_unset(top, -1e38)) top = 0;   /* ent +0x2e8 top of stock (+0x2f0 incremental) */
        if (ob(w, 0x288)) top += pz;
        double R = d_ovr(D(tp, 0x148), od(w, 0x128)); if (ob(w, 0x130)) R += top;   /* retract ent +0x190 (+0x198) */
        C = R;
        if (ob(w, 0x111)) { C = d_ovr(D(tp, 0x140), od(w, 0x108)); if (ob(w, 0x110)) C += top; }   /* clearance ent +0x170 (+0x178, on +0x179) */
        cur[2] = drill_depth(w, tp, vd[i]);
        int newc = 0, c5a = 0;
        if (d_unset(D(tp, 0xd8), -1e38)) {                          /* l.69860-69873 */
            A = R;
            if (ob(w, 0x111) && (!ob(w, 0x2b1) || c54)) A = C;      /* ent +0x319: clearance at the start / end only */
        } else { newc = 1; c5a = 1; A = D(tp, 0xd8); }             /* the point's own initial height */
        if (c28 == 1) { newc = 1; memcpy(prev, cur, sizeof prev); B = A; }
        else {                                                      /* l.69880-69899 */
            int ne = dne(B, A);
            if (ne) b78 = B;
            down = ne && A < B; up = ne && B <= A;
            newc = (newc || !ne) ? 0 : 1;                           /* own height or the same height: local_2c63 && tp[0x92] (0 here) */
        }
        if (!newc) {                                                /* LAB_1800686e3: the next point of the cycle */
            if (cann) wr_1025_pt(w, tp);
            int16_t lastf = last <= (int16_t)i ? (int16_t)-0x7ffe : 0;
            q[0] = cur[0]; q[1] = cur[1];
            if (up || down) q[2] = B; else { q[2] = A; B = A; }
            wr_100(w, tp, cur, q, B, R, pz, lastf, top);
        } else {                                                    /* LAB_1800686a9: a new cycle */
            if (!c48) {
                c48 = 1; q[0] = cur[0]; q[1] = cur[1]; q[2] = A;
                sof(w, q);
                B = A;                                              /* l.69944-69960: B = C when C == A, a rapid to R when ... */
                if (!dne(C, A)) {
                    B = C;
                    if ((!ob(w, 0x111) || ob(w, 0x2b1)) && oi(w, 0x88) != 1 && dne(R, C)) { q[2] = R; wr_rapid(w, q); B = R; }
                }
            }
            if (!c54) {                                             /* l.69962-70119 */
                wr_80(w);
                if (!c5a) {
                    if (up) {
                        q[0] = prev[0]; q[1] = prev[1]; q[2] = A; wr_rapid(w, q); B = A;
                        q[0] = cur[0]; q[1] = cur[1]; wr_rapid(w, q);
                    } else {
                        q[0] = cur[0]; q[1] = cur[1];
                        if (!down) q[2] = A;
                        else { q[2] = B; wr_rapid(w, q); q[2] = A; wr_rapid(w, q); }
                    }
                    up = down = 0; B = A;
                } else { q[0] = cur[0]; q[1] = cur[1]; q[2] = A; B = A; }
            }
            if (cann) wr_1025_pt(w, tp);
            wr_81(w, tp, cur, q, B, R, top, pz);
        }
        if (up || down) {                                           /* l.70157-70193: back up at this point, over to the next */
            wr_80(w);
            q[2] = A; wr_0_ent(w, tp, q);                           /* q[2]: FUN_18000b7e0(&local_2c40, 2) (objdump 0x18006911e) */
            if (i < oi(w, 0x88) - 1 && i + 1 < ntp) {
                const uint8_t *nx = tps[i + 1];
                world_to_op(w, nx, q, &ndw);
                q[2] = A; wr_0_ent(w, nx, q);
                if (c5a && d_unset(D(nx, 0xd8), -1e38)) { q[2] = b78; wr_0_ent(w, nx, q); }
            }
            c28 = 0; up = down = 0;
        }
        c54 = 0;
    }
    if (ndw) ND("world_to_op of a solid arc point (kind 0x1c)");
    if (c28 > 0) {                                                  /* l.70253-70282 */
        wr_80(w);
        if ((ob(w, 0x111) && ob(w, 0x2b1)) || up) {
            q[0] = cur[0]; q[1] = cur[1]; q[2] = C;
            if (ob(w, 0x111) && ob(w, 0x2b1) && A < C) { wr_rapid(w, q); B = C; }
            if (up && dne(B, A)) { q[0] = cur[0]; q[1] = cur[1]; q[2] = A; wr_rapid(w, q); }
        }
    }
    free(tps); free(vd); free(use);
    return 1;
refuse:
    free(tps); free(vd); free(use);
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the section */
int nciw_section(const NciwPart *P, const NciwOp *o, const Move *m, int nm, NciwBuf *out, NciwStats *st, char *err, size_t errn) {
    W ww, *w = &ww; w_init(w, P, o, out, st);
    if (!w->op || w->opn < 0x1588) { snprintf(err, errn, "no operation record"); return 0; }
    if (nm < 1) { snprintf(err, errn, "no toolpath"); return 0; }
    if (op_is_lathe(w->opcode) || op_is_wire(w->opcode)) { snprintf(err, errn, "lathe / wire operations are not written (P4)"); return 0; }
    uint8_t r[NB];
    sos(w);
    if (op_is_drill(w->opcode)) {                                   /* mcmill bnci_drill: the canned-cycle records 80 / 81 / 82 / 100 */
        if (!drill_section(w, err, errn)) return 0;
        goto eos;
    }
    double first[3] = { m[0].p.x, m[0].p.y, m[0].p.z };
    sof(w, first);
    /* the moves: one record each (mcmill gr_lin_mill -> bnci_wr_lin; the engines' arc records -> bnci_wr_arc). The markers -4 .. -7
       and the contour flags +0x40 / +0x58 the Mastercam engines write are NOT DECODED here (engine-specific; bnci_to_nci skips the
       markers): flags 0, no markers */
    ND("contour flags / section markers -4..-7 of the engine");
    if (o->f && o->f->ctl_arcfeed) ND("arc feed adjustment (AdjustArcFeedrates, calc_arc_feed)");
    for (int k = 0; k < nm; k++) {
        const Move *q = &m[k];
        if (q->arc > 0 && !q->rapid && k > 0 && q->apl == 0) {      /* an engine arc record: its chords -> one bnci_wr_arc */
            int en = k; int16_t cc = q->cc, ctf = (int16_t)q->ctf;
            while (en + 1 < nm && m[en + 1].arc == q->arc && !m[en + 1].rapid) { en++; if (m[en].cc) cc = m[en].cc; ctf |= (int16_t)m[en].ctf; }
            memset(r, 0, NB); W16(r, 0, q->cw ? 2 : 3); W32(r, 4, w->opid);
            W16(r, 0x18, 0); W16(r, 0x1a, cc);
            WD(r, 0x20, m[en].p.x); WD(r, 0x28, m[en].p.y); WD(r, 0x30, m[en].p.z);
            WD(r, 0x38, q->cx); WD(r, 0x40, q->cy); WD(r, 0x48, m[en].p.z);
            WD(r, 0x50, q->feed); W16(r, 0x58, ctf);   /* the contour flags (Move.ctf: Thread Mill) */
            /* +0x5a (bnci_wr_arc param_9; gr_arc_mill -> ct_arc_mill pass 0): 1 for a full circle - the engines' callers that set it are
               NOT DECODED; [inferred from the stored NCI: in every user part (630 / 14483 XY arcs) it is 1 exactly when the arc ends where
               it starts] - without it the post reads a zero-length arc (thread mill helices posted as G1 Z) */
            r[0x5a] = fabs(m[en].p.x - m[k - 1].p.x) < 1e-6 && fabs(m[en].p.y - m[k - 1].p.y) < 1e-6;
            put(w, r);
            if (cc == 140) { uint8_t z[NB]; rec0(w, z, 0x3ec); W32(z, 8, w->opid); put(w, z); }   /* stop_cc: bnci_wr_1004 */
            k = en; continue;
        }
        if (q->arc > 0 && q->apl != 0) ND("an XZ / YZ arc (written as its chords)");
        memset(r, 0, NB); W16(r, 0, q->rapid ? 0 : 1); W32(r, 4, w->opid);   /* bnci_wr_lin */
        W16(r, 0x18, (int16_t)q->cc);
        WD(r, 0x20, q->p.x); WD(r, 0x28, q->p.y); WD(r, 0x30, q->p.z);
        WD(r, 0x38, q->rapid ? -2.0 : q->feed);                      /* G0: -2 unless -2 / -3 / -4 already */
        W16(r, 0x40, (int16_t)q->ctf);                               /* the contour flags (Move.ctf: Thread Mill) */
        put(w, r);
        if (q->cc == 140) { uint8_t z[NB]; rec0(w, z, 0x3ec); W32(z, 8, w->opid); put(w, z); }
    }
eos:                                                                /* bnci_wr_eos @180092820 */
    st->eos_off = (long)out->n;
    memset(r, 0, NB); W16(r, 0, -3); r[0xc] = (uint8_t)rec_flag(w->opcode); W32(r, 4, w->opid); W32(r, 0x18, w->opid);
    W32(r, 0x1c, (int32_t)time(NULL));                              /* _time32 */
    put(w, r);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ Q24 naming */
void nciw_check_suffix(const char *in, char *out, size_t outn) {  /* CPostFileUtil::CheckNcSuffix @180077610 */
    char t[0x110]; snprintf(t, sizeof t, "%s", in ? in : "");
    char *a = t; while (*a == '.' ) a++;                            /* Trim('.') (@0x18020fea8 ".") */
    size_t L = strlen(a); while (L && a[L - 1] == '.') a[--L] = 0;
    while (*a && isspace((unsigned char)*a)) a++;                    /* Trim() */
    L = strlen(a); while (L && isspace((unsigned char)a[L - 1])) a[--L] = 0;
    if (!*a) { if (outn) out[0] = 0; return; }
    snprintf(out, outn, ".%s", a);                                  /* Format(".%s" @0x180215538) */
}
void nciw_nc_name(const uint8_t *oprec, uint32_t n, const char *dir, const char *suffix, char *out, size_t outn) {
    /* reduce_filename: the base name of op +0x170 (record +0x1d8), no directory, no extension */
    char nci[0x106] = ""; size_t k = 0;
    while (k < 0x104 && 0x1d8 + k < n && oprec[0x1d8 + k]) { nci[k] = (char)oprec[0x1d8 + k]; k++; } nci[k] = 0;
    const char *b = nci; for (const char *p = nci; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    char base[0x106]; snprintf(base, sizeof base, "%s", b);
    char *dot = strrchr(base, '.'); if (dot) *dot = 0;
    size_t dl = strlen(dir);
    const char *sep = !dl || dir[dl - 1] == '/' || dir[dl - 1] == '\\' ? "" : "/";   /* ours: no folder = the current one */
    /* build_filename: suffix without a leading '.' gets one */
    snprintf(out, outn, "%s%s%s%s%s", dir, sep, base, suffix && suffix[0] && suffix[0] != '.' ? "." : "", suffix ? suffix : "");
}

/* ------------------------------------------------------------------------------------------------ the app's ops (ours: glue) */
#include "mcam_saveops.h"
#include <sys/stat.h>
static const char *ncname_of(const uint8_t *rec, uint32_t n, char *b) {   /* op +0x170 (record +0x1d8) */
    size_t k = 0; while (k < 0x104 && 0x1d8 + k < n && rec[0x1d8 + k]) { b[k] = (char)rec[0x1d8 + k]; k++; } b[k] = 0; return b;
}
void nciw_view_free(NciwView *v) {
    if (!v) return;
    for (int i = 0; i < v->nbufs; i++) free(v->bufs[i]);
    free(v->bufs); free(v->G.ops); memset(v, 0, sizeof *v);
}
static void keep_buf(NciwView *v, void *p) { v->bufs = realloc(v->bufs, sizeof *v->bufs * (size_t)(v->nbufs + 1)); v->bufs[v->nbufs++] = p; }
int nciw_view(McamFile *F, CamOp **camops, int ncamops, int metric, NciwView *v, char *err, size_t errn) {
    memset(v, 0, sizeof *v);
    v->G = *F;
    v->G.ops = malloc(sizeof(Op) * (size_t)(F->nops + 1)); memcpy(v->G.ops, F->ops, sizeof(Op) * (size_t)F->nops);
    int n = 0; CamOp **mine = malloc(sizeof *mine * (size_t)(ncamops + 1)); int *at = malloc(sizeof(int) * (size_t)(ncamops + 1));
    for (int i = 0; i < F->nops; i++) for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) { mine[n] = camops[j]; at[n++] = i; }
    if (!n) { free(mine); free(at); return 1; }
    uint8_t **rec = calloc((size_t)n, sizeof *rec), **tl = calloc((size_t)n, sizeof *tl); uint32_t *recn = calloc((size_t)n, 4), *tln = calloc((size_t)n, 4);
    uint8_t **tp = calloc((size_t)n, sizeof *tp); uint32_t *tpn = calloc((size_t)n, 4);   /* drill points (bnci_drill's tp list) */
    struct stat sb; const char *src = F->path[0] && stat(F->path, &sb) == 0 ? F->path : NULL;
    char msg[700] = "";
    int ok = mcam_op_records_tp(src, F, mine, n, rec, recn, tl, tln, tp, tpn, msg, sizeof msg);
    NciwPart *P = nciw_part_load(src);
    long base = 0x40000000L;                                         /* ours: positions past any stored record */
    for (int k = 0; ok && k < n; k++) {
        CamOp *op = mine[k]; int i = at[k];
        if (!rec[k]) { snprintf(err, errn, "operation %d (%s) is not written by the save (%s)", i + 1, cam_op_label(op), msg[0] ? msg : "no record"); ok = 0; break; }
        if (!op->nm) { snprintf(err, errn, "operation %d (%s) has no toolpath - regenerate it first", i + 1, cam_op_label(op)); ok = 0; break; }
        if (op->gen_units && op->gen_units - 1 != (metric != 0)) {   /* 1016 +0x44 = IsMetric(): moves computed in the other units (CamOp.gen_units) */
            snprintf(err, errn, "operation %d (%s)'s toolpath was computed in %s and the part is now in %s - regenerate it first", i + 1, cam_op_label(op),
                     op->gen_units == 2 ? "mm" : "inch", metric ? "mm" : "inch"); ok = 0; break; }
        /* the TL_ID record: the save's new one, another op's new one with this slot, or the part's */
        const uint8_t *t = tl[k]; uint32_t tn = tln[k];
        for (int q = 0; !t && q < n; q++) if (tl[q] && I32(tl[q], 0x2cc) == I32(rec[k], 0x5ac)) { t = tl[q]; tn = tln[q]; }
        /* op +0x170: InitOperation (mccore_ops.c l.1416-1430) copies the previous op's NC name (list_op_get_prev of the active
           machine) into a new op; ours: the op before it in the tree that has one; an edited Mastercam op keeps its own */
        if (op->p.file_op_id <= 0) {
            char b[0x106];
            for (int q = i - 1; q >= 0; q--) {
                const Op *po = &v->G.ops[q];
                if (po->raw && po->raw_n > 0x1d8 + 0x104 && ncname_of(po->raw, po->raw_n, b)[0]) { memcpy(rec[k] + 0x1d8, po->raw + 0x1d8, 0x104); break; }
            }
        }
        Move *m = malloc(sizeof(Move) * (size_t)op->nm); memcpy(m, op->m, sizeof(Move) * (size_t)op->nm);
        if (!cam_op_top(&op->p)) for (int q = 0; q < op->nm; q++) {   /* world -> op coordinates (nc_post.c camop_events) */
            double w[3] = { m[q].p.x, m[q].p.y, m[q].p.z }, c[3]; cam_plane_w2v(op->p.tplane, op->p.torg, w, c);
            m[q].p.x = c[0]; m[q].p.y = c[1]; m[q].p.z = c[2]; }
        NciwOp no = { rec[k], recn[k], t, tn, F, metric, base, tp[k], tpn[k] };
        NciwBuf sec = { 0 }; NciwStats st; memset(&st, 0, sizeof st); char e2[300] = "";
        int r = nciw_section(P, &no, m, op->nm, &sec, &st, e2, sizeof e2);
        free(m);
        if (!r) { free(sec.b); snprintf(err, errn, "operation %d (%s): %s", i + 1, cam_op_label(op), e2); ok = 0; break; }
        W32(rec[k], 0xe8, (int32_t)base); W32(rec[k], 0xec, (int32_t)(base + st.eos_off));   /* op +0x80 / +0x84 (bnci_wr_sos / _eos) */
        Op *o = &v->G.ops[i];
        o->raw = rec[k]; o->raw_n = recn[k]; o->id = I32(rec[k], 0x68); o->opcode = (uint16_t)I16(rec[k], 0x6c);
        o->nci = sec.b; o->nci_n = sec.n; o->stale = 0;
        keep_buf(v, sec.b); keep_buf(v, rec[k]); rec[k] = NULL;
        v->nstandin += st.not_decoded;
        if (st.note[0] && !v->note[0]) snprintf(v->note, sizeof v->note, "%s", st.note);
        base += (long)sec.n + 0x1000;
    }
    for (int k = 0; k < n; k++) { free(rec[k]); free(tp[k]); if (tl[k]) keep_buf(v, tl[k]); }
    free(tp); free(tpn); free(rec); free(tl); free(recn); free(tln); free(mine); free(at); nciw_part_free(P);
    if (!ok) { if (!err[0]) snprintf(err, errn, "%s", msg); nciw_view_free(v); return 0; }
    return 1;
}
