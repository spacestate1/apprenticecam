/* nci_ascii - port of Mastercam 2017's binary NCI -> ASCII .nci conversion (see nci_ascii.h).
 *
 * Sources (all addresses are Mastercam 2017 x64 VAs):
 *   [N]  mccore bnci_to_nci @180088920            decomp/mccore_bnci_nci.c + objdump of the call at 1800891a2
 *   [C]  mccore FUN_180083560 (bnci_convert_section, the name its _MCAssert strings use) decomp/mccore_bnci_nci.c
 *   [S]  mccore bnci_to_string @180089b70          decomp/mccore_bnci_str.c; the argument lists of the mill
 *        formatter calls are truncated in that decompile, so they come from objdump of bnci_to_string (addresses
 *        of the call sites are cited per gcode)
 *   [F]  the formatter bodies FUN_1808cXXXX        decomp/mccore_bnci_str.c where dumped, else objdump
 *   [R]  mccore bnci_read_from_disk @180087720     decomp/mccore_bnci_io.c
 *   [M]  mcmill bnci_convert_op_comment @18019aae0 decomp/mcmill_all.c l.284608 (+ FUN_18019bdd0 l.285518)
 * objdump disassembly is ours to read but not ours to invent: every number below is in those listings.
 * "ours" marks glue that is not Mastercam's (data access, buffers, error reporting).
 * MFC ordinals (mfc120.dll) are identified from their use: 4130 Empty, 1502 operator=(const char*), 2374 operator+=,
 * 1634 GetString, 4846 GetBuffer, 12059 ReleaseBuffer(-1), 4458 Find(char), 12084 Remove(char), 13814 / 13820 the
 * Trim pair (whitespace), 5558 GetLength, 4604 Format. */
#define _GNU_SOURCE
#include "nci_ascii.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#define NB 0x370                       /* sizeof(nci_bin): [R] memset(local_3c8, 0, 0x370) */

/* ---------------------------------------------------------------- little-endian field access (ours) */
static int16_t I16(const uint8_t *b, int o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static uint16_t U16(const uint8_t *b, int o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static int32_t I32(const uint8_t *b, int o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static double D(const uint8_t *b, int o) { double v; memcpy(&v, b + o, 8); return v; }
static void W32(uint8_t *b, int o, int32_t v) { memcpy(b + o, &v, 4); }
static void W16(uint8_t *b, int o, int16_t v) { memcpy(b + o, &v, 2); }
static void WD(uint8_t *b, int o, double v) { memcpy(b + o, &v, 8); }

/* ---------------------------------------------------------------- conversion state */
typedef struct { FILE *fp; char *mem; size_t n, cap; int crlf; } Sink;   /* ours: where the lines go */
struct MpNciConv {
    const McamFile *f;
    MpNciEnv env;
    int p5;                 /* bnci_to_string param_5: 1 in every [C] / [N] call except 20701 and the prm / external paths */
    double last_feed;       /* mccore _DAT_1812b13f8 (static -1e38): the previous feed for the modal "-1" */
    int flag_55a1;          /* mccore DAT_1813755a1: set by gcode -8, cleared by 1011 / 1032; turns 1000 into 1002 */
    Sink out;
    MpNciStats st;
    FILE *warn;
    struct PRec *pr; int npr;   /* ours: the part's 0x4000 (ASSOC) records, for the machine definition lookups */
};

static void warnf(MpNciConv *c, const char *fmt, ...) {          /* ours */
    c->st.not_decoded++;
    if (!c->warn) return;
    va_list ap; va_start(ap, fmt); fputs("nci_ascii: NOT DECODED: ", c->warn); vfprintf(c->warn, fmt, ap); fputc('\n', c->warn); va_end(ap);
}
static void sink_put_raw(Sink *s, const char *t, size_t n);
static void sink_put(Sink *s, const char *t, size_t n) {          /* text mode ("wt"): LF -> CR LF when crlf (MpNciEnv.crlf) */
    if (!s->crlf) { sink_put_raw(s, t, n); return; }
    size_t a = 0;
    for (size_t i = 0; i < n; i++) if (t[i] == '\n') { sink_put_raw(s, t + a, i - a); sink_put_raw(s, "\r\n", 2); a = i + 1; }
    sink_put_raw(s, t + a, n - a);
}
static void sink_put_raw(Sink *s, const char *t, size_t n) {      /* ours */
    if (s->fp) { fwrite(t, 1, n, s->fp); return; }
    if (s->n + n + 1 > s->cap) { s->cap = (s->n + n + 1) * 2; s->mem = realloc(s->mem, s->cap); }
    memcpy(s->mem + s->n, t, n); s->n += n; s->mem[s->n] = 0;
}
static void sink_printf(Sink *s, const char *fmt, ...) {          /* ours (SysFile::Printf) */
    char b[4096]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof b) n = sizeof b - 1;
    sink_put(s, b, (size_t)n);
}

/* ---------------------------------------------------------------- the CString line and its number writers */
typedef struct { char s[8192]; size_t n; } Str;                   /* ours: the CString local_33a8 */
static void s_empty(Str *s) { s->n = 0; s->s[0] = 0; }            /* Ordinal_4130 Empty */
static void s_cat(Str *s, const char *t) {                        /* Ordinal_2374 operator+= */
    size_t L = strlen(t); if (s->n + L >= sizeof s->s) L = sizeof s->s - 1 - s->n;
    memcpy(s->s + s->n, t, L); s->n += L; s->s[s->n] = 0;
}
static void s_set(Str *s, const char *t) { s_empty(s); s_cat(s, t); }   /* Ordinal_1502 operator= */
/* append_long @1808c9860 / append_short @1808c9a60 / append_real @1808c99e0 / FUN_1808c97d0 (%d): format into an
   0x50 buffer (FUN_18002bb80 = vsprintf_s(buf, 0x50, ...)), then " " (0x180cf5440) only if the string is not empty,
   then the text */
static void s_item(Str *s, const char *t) { if (s->n) s_cat(s, " "); s_cat(s, t); }

/* fmt_real @1803698f0 (objdump): |v| < 1e38 (0x180cf3b38) -> sprintf("%.*lf" @0x180d1b980, n_places_nci, v), else
   "$$$$$$$$$$$$$$$$$$$$" (@0x180d42408, 20 '$'); then every trailing '0' is removed (the '.' stays: "2." / "0.").
   Before that, when GetProdEnableBits() & 1 and DAT_1813e91a0 == 0 the value is first rounded to a few significant
   digits (a demo / learning-edition mode): ours = that bit clear (a full Mastercam). */
static void fmt_real(double v, int places, char *b, size_t bn) {
    if (fabs(v) < 1e38) snprintf(b, bn, "%.*f", places, v);       /* NaN fails the compare (comisd unordered) */
    else snprintf(b, bn, "$$$$$$$$$$$$$$$$$$$$");
    size_t L = strlen(b);
    while (L > 1 && b[L - 1] == '0') b[--L] = 0;                  /* the loop at 180369a55 (L > 1: ours, keeps one char) */
}
static void a_real(MpNciConv *c, Str *s, double v) { char b[0x50 + 400]; fmt_real(v, c ? c->env.n_places_nci : 8, b, sizeof b); s_item(s, b); }
static void a_short(Str *s, int16_t v) { char b[0x50]; snprintf(b, sizeof b, "%hd", v); s_item(s, b); }   /* "%hd" @0x180cf5a68 */
static void a_long(Str *s, int32_t v) { char b[0x50]; snprintf(b, sizeof b, "%d", v); s_item(s, b); }     /* "%ld" @0x180cfc698, Win64 long = 32 bit */
static void a_int(Str *s, int32_t v) { char b[0x50]; snprintf(b, sizeof b, "%d", v); s_item(s, b); }      /* FUN_1808c97d0: "%d" @0x180cff77c */
static void a_bool(Str *s, uint8_t v) { a_short(s, v == 1); }    /* append_boolean @1808c97b0: append_short(b == 1) */
static void a_p3d(MpNciConv *c, Str *s, const uint8_t *r, int o) { for (int k = 0; k < 3; k++) a_real(c, s, D(r, o + 8 * k)); }  /* append_p_3d @1808c9980 */
static void a_m33(MpNciConv *c, Str *s, const uint8_t *r, int o) { for (int k = 0; k < 9; k++) a_real(c, s, D(r, o + 8 * k)); }  /* append_matrix_33 @1808c98f0: 3 rows of 3 */

/* pos_to_ascii @1800a2dd0 (objdump): contour-flag word -> decimal code. Mode 2 is the wire branch (not used here). */
static int32_t pos_to_ascii(int mode, uint16_t f) {
    int32_t v = 0;
    if (mode == 1) {                                               /* 1800a2f5b */
        if (f & 4) v = 1;
        if (f & 1) v += 1000;
        if (f & 2) v += 100;
        if (f & 0x4000) v += 2000;
        if (f & 0x8000) v += 200;
        if (f & 0x80) v += 70000;
        else if (f & 0x800) v += 10000;
        else if (f & 0x1000) v += 20000;
        else if (f & 0x200) v += 30000;
        else if (f & 0x400) v += 40000;
        else if (f & 0x100) v += 50000;
        else if (f & 0x2000) v += 60000;
        if (f & 8) v += 100000; else if (f & 0x10) v += 200000; else if (f & 0x20) v += 300000; else if (f & 0x40) v += 400000;
        return v;
    }
    if (mode == 2) {                                               /* 1800a2e65 (wire) */
        if (f & 4) v = 1;
        if (f & 0x1000) v += 10;
        if (f & 1) v += 1000;
        if (f & 2) v += 100;
        if (f & 0x40) v += 2000;
        if (f & 0x80) v += 200;
        if (f & 0x100) v += 10000; else if (f & 0x200) v += 20000;
        if (f & 8) v += 100000; else if (f & 0x10) v += 200000; else if (f & 0x20) v += 300000;
        if (f & 0x800) v += 1000000; else if (f & 0x400) v += 2000000;
        if ((f & 0x4000) && (f & 0x8000)) return 100000000;
        if (f & 0x4000) return v + 10000000;
        if (f & 0x8000) return v + 20000000;
        return v;
    }
    /* mode 0 (other modes assert, then run this): 1800a301f */
    if (f & 0x2000) return (f & 4) ? 2 : 20;
    if (f & 4) v = 1;
    if (f & 1) v += 1000;
    if (f & 2) v += 100;
    if (f & 0x4000) v += 2000;
    if (f & 0x8000) v += 200;
    if (f & 0x1000) v += 70000;
    if (f & 8) v += 100000; else if (f & 0x10) v += 200000; else if (f & 0x20) v += 300000; else if (f & 0x40) v += 400000;
    return v;
}
/* nest_to_ascii @1800a1de0 (objdump; used by 1018 only, kept for completeness) */
static __attribute__((unused)) int32_t nest_to_ascii(int f) {
    int32_t v = (f & 0x40) ? 10000 : 0;
    if (f & 0x20) v += 1000;
    if (f & 0x10) v += 100;
    if (f & 4) v += 10;
    if (f & 2) v += 2;
    if (f & 1) v += 1;
    return v;
}

/* cvttsd2si (dwell's (int) of a double): out of range / NaN -> 0x80000000 */
static int32_t cvtt(double v) { if (!(v > -2147483649.0 && v < 2147483648.0)) return INT32_MIN; return (int32_t)v; }

/* ---------------------------------------------------------------- op / group / machine lookups (ours: McamFile) */
static const Op *op_by_id(const McamFile *f, int id) { for (int i = 0; i < f->nops; i++) if (f->ops[i].id == id) return &f->ops[i]; return NULL; }
/* the operation struct is the OP_ID entity from +0x68 (op +0 id = ent +0x68, op +4 opcode = ent +0x6c, op +0x80 / +0x84
   NCI start / end = ent +0xe8 / +0xec: [N] local_2aa8 / local_29c0 / local_29bc and [C] poVar18 + 0x80) */
static const uint8_t *opf(const Op *o, int off, int len) { if (!o || !o->raw || 0x68 + off + len > (int)o->raw_n) return NULL; return o->raw + 0x68 + off; }
/* the op's machine group = the group McamFile decoded (OMgetOpMachineGroup: record +0x5bc == op_group +0, mcam_load.h) */
static int op_in_decoded_group(const McamFile *f, const Op *o) {
    return o && o->raw && o->raw_n >= 0x5c0 && f->grp_rec && I32(o->raw, 0x5bc) == f->grp_id;
}

/* op_is_* predicates (mccore, objdump) */
static int op_is_xform(int op) { return op == 4 || op == 0x50 || op == 0x6c || op == 0x9a; }               /* @1800c7700 */
static int op_c7000(int op) { return op == 0x68 || (op >= 0x88 && op <= 0x8a); }                             /* FUN_1800c7000 */
static int op_is_no_nci(int op) { return op_c7000(op) || op == 0x51 || op == 0x67 || op == 0x8b; }                  /* @1800c6930 */
static int op_is_mt_misc_no_tool(int op) { return op == 0xe6 || (op >= 0xe8 && op <= 0xec); }                /* @1800c6550 */
static int op_is_lathe(int op) { return (uint16_t)(op - 0x3c) <= 12 || (uint16_t)(op - 0xc9) <= 0x23; }     /* @1800c6060 */
static int op_is_wire(int op) { return (uint16_t)(op - 0x4a) <= 9; }   /* @1800c76c0; its op 0x6c case needs GetProdEnableBits & 0x10 (wire product): ours = not wire */
static int op_is_notool(int op) {                                                                             /* @1800c6c60, 2nd arg 0 */
    static const int t[] = { 0, 4, 17, 30, 31, 32, 33, 34, 35, 47, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 103, 108, 139, 154,
                             214, 215, 217, 218, 219, 230, 232, 233, 234, 235, 236 };   /* jump table 0x1800c6cd4 -> 0x1800c6cbb */
    if (op_c7000(op)) return 1;
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (t[i] == op) return 1;
    return 0;
}
static int rec_flag_for_op(int op) { return op_is_lathe(op) ? 1 : op_is_wire(op) ? 2 : 0; }                 /* FUN_18007bc90 */

/* ---------------------------------------------------------------- the part's machine definition records
   The part file is read again (ours) with mcam_load.c's record framing (u32 length + entity; the payloads after the
   toolpath-group (sub 0xF), material-table (0xE) and Parasolid records skipped as mcam_load does). Kept: class 0x4000
   records (ASSOC) with their subtype (+0x2c) and entity id (+0x34). Machine definition subtypes used here (measured on the
   user's parts and the shipped defaults, consistent in every one): 12 = a machine component (CMachineComponent: name at
   +0x68 = Params +0, so Params = record +0x68), 15 = an id list (+0x68 count, +0x70 i32 ids), 26 = an axis combination
   (CAxisCombination). */
struct PRec { const uint8_t *r; uint32_t n; int32_t sub, id; };
static void part_index(MpNciConv *c, const uint8_t *d, size_t len) {
    size_t off = 0; int cap = 0;
    while (off + 4 <= len) {
        uint32_t n; memcpy(&n, d + off, 4);
        if (n == 0 || off + 4 + n > len) break;                    /* the binary NCI tail */
        const uint8_t *rec = d + off + 4;
        if (n >= 0x70 && I32(rec, 0) == 0x4000) {
            if (c->npr == cap) { cap = cap ? 2 * cap : 256; c->pr = realloc(c->pr, (size_t)cap * sizeof *c->pr); }
            c->pr[c->npr++] = (struct PRec){ rec, n, I32(rec, 0x2c), I32(rec, 0x34) };
        }
        off += 4 + n;
        if (n >= 0x6a && I32(rec, 0) == 0x200) {
            uint16_t sub = U16(rec, 0x68);
            if (sub == 0xF && off + 4 <= len) { uint32_t ng; memcpy(&ng, d + off, 4); off += 4 + (size_t)ng * 0x1ae0; }
            else if (sub == 0xE) {
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
}
static const struct PRec *prec(const MpNciConv *c, int sub, int32_t id) {
    for (int i = 0; i < c->npr; i++) if (c->pr[i].sub == sub && c->pr[i].id == id) return &c->pr[i];
    return NULL;
}
/* CCNCMachine::AxisCombinationFromID(machine, op +0x11bc, 0) @180061250: the combination whose CAxisCombination::Id
   (@180014810, object +0x1e9c) equals op +0x11bc. Id = the record's entity id: op +0x11bc (OP_ID +0x1224) names a subtype 26
   record's +0x34 in every part (measured). Its index-0 fallback (no such id) is NOT DECODED (NULL here). */
static const uint8_t *axcomb_of(const MpNciConv *c, const Op *o) {
    const uint8_t *p = opf(o, 0x11bc, 4); if (!p) return NULL;
    const struct PRec *a = prec(c, 26, I32(p, 0));
    return a && a->n >= 0x1e50 ? a->r : NULL;
}
static int comp_type(const struct PRec *p) { return p && p->n >= 0xe8 ? I32(p->r, 0xe4) : -1; }   /* Params +0x7c (FUN_18008fec0) */
/* CCNCMachine::MaxToolSpindleRPM(op, NULL) @18006b850 (objdump): no axis combination -> 0x7fffffff; else the first of its two
   component chains (FUN_180018a40: FUN_1800127a0(0) then (1) = the chain's last component has a type in 200..207 or
   300..307) is scanned from its start by MaxToolSpindleRPM(CObArray) @18006b920 until a component gives a value: type
   301 (spindle) / 203 / 204 -> UParams +4, 303 / 307 -> UParams +0x84, 304 / 305 -> 100000, UParams = Params +0x4a0
   (@1800ca9e0) = record +0x508. The chains are the subtype 15 lists named at axis-combination record +0x1e48 / +0x1e4c
   (measured: CAxisCombination +0x3d30 + idx * 0x28 holds them, FUN_1800126a0). Returns 0 when not resolved here. */
static int max_tool_rpm(MpNciConv *c, const Op *o, int32_t *out) {
    if (!o) return 0;
    const uint8_t *ac = axcomb_of(c, o);
    if (!ac) return 0;
    const struct PRec *ch = NULL;
    for (int k = 0; k < 2 && !ch; k++) {
        const struct PRec *l = prec(c, 15, I32(ac, 0x1e48 + 4 * k));
        if (!l) continue;
        int cnt = I32(l->r, 0x68);
        if (cnt < 1 || 0x70 + 4 * (size_t)cnt > l->n) continue;
        int t = comp_type(prec(c, 12, I32(l->r, 0x70 + 4 * (cnt - 1))));
        if ((t >= 200 && t <= 207) || (t >= 300 && t <= 307)) ch = l;
    }
    if (!ch) return 0;                                            /* FUN_180018a90 / CComponentGroup branch: NOT DECODED */
    int32_t v = 0x7fffffff; int cnt = I32(ch->r, 0x68);
    for (int i = 0; i < cnt && v == 0x7fffffff; i++) {
        const struct PRec *p = prec(c, 12, I32(ch->r, 0x70 + 4 * i));
        int t = comp_type(p);
        if (!p || p->n < 0x590) continue;
        if (t == 301 || t == 203 || t == 204) v = I32(p->r, 0x50c);
        else if (t == 303 || t == 307) v = I32(p->r, 0x58c);
        else if (t == 304 || t == 305) v = 100000;
    }
    if (v == -1) return 0;                                        /* -1 -> the CComponentGroup branch: NOT DECODED */
    *out = v; return 1;
}

/* ---------------------------------------------------------------- line output: the tail of [S] (LAB_18008cadd) */
static void emit(MpNciConv *c, const uint8_t *r, Str *s, int p4) {
    for (size_t i = 0; i < s->n; i++) {                           /* FUN_1806f27d0: TAB / LF / CR -> ' ' */
        unsigned ch = (unsigned char)s->s[i];
        if (ch == 9 || ch == 10 || ch == 13) s->s[i] = ' ';
    }
    char g[32] = "";
    if (p4) {                                                     /* BnciLineToGcodeString @1808c8c20 */
        int gc = I16(r, 0);
        if (gc >= 0) {
            if (gc == 0x41f) snprintf(g, sizeof g, "29999");
            else if (gc == 10000) snprintf(g, sizeof g, "%d", I16(r, 0x18) + 10000);
            else snprintf(g, sizeof g, "%d", gc);
        }
        sink_printf(&c->out, "%s\n%s\n", g, s->s);               /* "%s\n%s" (FUN_180077850) then Printf("%s\n") */
    } else sink_printf(&c->out, "%s\n", s->s);
    c->st.lines++;
}
/* text fields: Ordinal_1502(str, rec + off) - a C string inside the 0x370 nci_bin (ours: bounded) */
static void s_text(Str *s, const uint8_t *r, int off) {
    char t[NB + 1]; size_t L = 0; while (off + (int)L < NB && r[off + L]) { t[L] = (char)r[off + L]; L++; } t[L] = 0; s_set(s, t);
}

/* bnci_convert_op_comment @18019aae0 [M]: prints "1008\n%s\n" itself, one line per comment line (CRs removed when an LF
   is present - FUN_18019bdd0 - then fgets(299) through a temporary text file), each Trim()med */
static void op_comment_out(MpNciConv *c, const uint8_t *r) {
    char t[NB + 1]; size_t L = 0; while (0x18 + L < NB && r[0x18 + L]) { t[L] = (char)r[0x18 + L]; L++; } t[L] = 0;
    if (!L) return;
    int has_cr = strchr(t, '\r') != NULL, has_lf = strchr(t, '\n') != NULL;
    char line[300];
    if (!has_cr && !has_lf) {
        char *a = t; while (*a && isspace((unsigned char)*a)) a++;
        size_t n = strlen(a); while (n && isspace((unsigned char)a[n - 1])) a[--n] = 0;
        sink_printf(&c->out, "%d\n%s\n", 0x3f0, a); c->st.lines++;
        return;
    }
    if (has_lf) { char *w = t; for (char *p = t; *p; p++) if (*p != '\r') *w++ = *p; *w = 0; }   /* Remove('\r') */
    const char *p = t;                                            /* fgets(local_158, 299, f) over the "wt" / "rt" file */
    while (*p) {
        size_t n = 0; while (p[n] && p[n] != '\n' && n < 298) n++;
        if (p[n] == '\n' && n < 298) n++;
        memcpy(line, p, n); line[n] = 0; p += n;
        char *a = line; while (*a && isspace((unsigned char)*a)) a++;
        size_t m = strlen(a); while (m && isspace((unsigned char)a[m - 1])) a[--m] = 0;
        sink_printf(&c->out, "%d\n%s\n", 0x3f0, a); c->st.lines++;
    }
}

/* modal feed (the same code for 1 @180089d6e, 2/3 @180089e37, 11 @180089fd3): with param_5, a feed within tol of the
   previous one (and not the rapid -2.0 @0x180cfa898) prints as -1.0 (@0x180cf3b50); the stored feed becomes the
   previous one either way */
static double modal_feed(MpNciConv *c, double fr) {
    double out = fr;
    if (c->p5 && c->env.tol > fabs(fr - c->last_feed) && (isnan(fr) || fr != -2.0)) out = -1.0;
    if (c->p5) c->last_feed = fr;
    return out;
}
static void feed_reset(MpNciConv *c) { if (c->p5) c->last_feed = -1e38; }   /* 0x180cf3b58 */

/* ---------------------------------------------------------------- bnci_to_string [S] for nci_bin flag 0 / 1
   returns 1 printed, 0 nothing printed, -1 NOT DECODED (nothing printed) */
static int b2s(MpNciConv *c, uint8_t *r, int opcode, int p4) {
    Str S, *s = &S; s_empty(s);
    int g = I16(r, 0), flag = r[0xc], L = flag == 1;              /* bVar32 = param_1[0xc] == 1 (dil) */
    const McamFile *f = c->f;
    (void)opcode;
    if (flag == 2) { warnf(c, "gcode %d: Wire / Agie record (nci_bin +0xc = 2, nci_str_w* branch)", g); return -1; }
    if (g > 900) {
        if (g > 10000) {
            if (g > 0x4f4c) {
                if (g == 0x5015) s_text(s, r, 0x18);
                else if (g == 0x5016) {                           /* FUN_1808cc8a0 (dumped) */
                    for (int k = 0; k < 3; k++) a_real(c, s, D(r, 0x18 + 8 * k));
                    a_int(s, r[0x30]); a_int(s, I32(r, 0x34)); a_int(s, I32(r, 0x38));
                } else if (g == 0x50dd) { warnf(c, "20701 (FUN_1808cb150 with its own \"20701\\n\" prefix; the caller's rsi at 18008b551 is not traced)"); return -1; }
                else if (g == 0x5140) a_real(c, s, D(r, 0x18));   /* 20800: FUN_1808cac20(+0x18) @18008b53d */
                /* else: Empty (18008b52e) */
            } else if (g == 0x4f4c) s_text(s, r, 0x18);
            else switch (g) {
            case 20000: case 20001: case 20002: case 20010: case 20011: case 20012: case 20013: case 20014: case 20015:
            case 20016: case 20017: case 20018: case 20019:
                s_text(s, r, 0x18); break;
            case 20003: case 20020: case 20021: if (!L) s_text(s, r, 0x18); break;
            case 20004:                                           /* FUN_1808cd170, call 18008b038 */
                if (L) break;
                a_long(s, I32(r, 0x18)); a_short(s, I16(r, 0x1c)); a_short(s, I16(r, 0x1e)); a_short(s, I16(r, 0x20));
                for (int k = 0; k < 4; k++) a_real(c, s, D(r, 0x28 + 8 * k));
                a_long(s, I32(r, 0x48)); a_long(s, I32(r, 0x4c));
                for (int k = 0; k < 3; k++) a_real(c, s, D(r, 0x50 + 8 * k));
                a_long(s, I32(r, 0x68)); a_short(s, I16(r, 0x6c)); a_short(s, I16(r, 0x6e));
                for (int k = 0; k < 8; k++) a_real(c, s, D(r, 0x70 + 8 * k));
                a_int(s, I32(r, 0xb0)); a_real(c, s, D(r, 0xb8)); a_int(s, I32(r, 0xc0));
                break;
            case 20005: case 20006:                               /* FUN_1808cced0 / FUN_1808cd350, calls 18008b07e / 18008b0c4 */
                if (L) break;
                a_short(s, I16(r, 0x18));
                for (int k = 0; k < 8; k++) a_real(c, s, D(r, 0x20 + 8 * k));
                break;
            case 20007:                                           /* FUN_1808cd060, call 18008b129 */
                if (L) break;
                for (int k = 0; k < 7; k++) a_real(c, s, D(r, 0x18 + 8 * k));
                a_short(s, I16(r, 0x50)); a_real(c, s, D(r, 0x58)); a_real(c, s, D(r, 0x60));
                a_short(s, I16(r, 0x68)); a_real(c, s, D(r, 0x70)); a_real(c, s, D(r, 0x78));
                break;
            case 20008:                                           /* FUN_1808ccf90, call 18008b17a */
                if (L) break;
                a_real(c, s, D(r, 0x18)); a_real(c, s, D(r, 0x20)); a_real(c, s, D(r, 0x28)); a_short(s, I16(r, 0x30));
                a_real(c, s, D(r, 0x38)); a_real(c, s, D(r, 0x40)); a_short(s, I16(r, 0x48));
                a_real(c, s, D(r, 0x50)); a_real(c, s, D(r, 0x58));
                break;
            case 20009:                                           /* FUN_1808cac80, call 18008b19a: "%d" +0x18, short(byte +0x1c) */
                if (L) break;
                a_int(s, I32(r, 0x18)); a_short(s, r[0x1c]);
                break;
            case 0x4e87: case 0x4e8e: case 0x4e90: if (L) s_text(s, r, 0x18); break;
            case 0x4e84: case 0x4e85: case 0x4e86: case 0x4e88: case 0x4e89: case 0x4e8a: case 0x4e8b: case 0x4e8c:
            case 0x4e8d: case 0x4e8f:
                if (L) { warnf(c, "lathe tool record %d (call-site arguments not traced)", g); return -1; }
                break;
            default: break;                                       /* Empty */
            }
        } else if (g == 10000) {                                  /* LAB_18008aed3: text +0x1a; strip_chars("[]") when +0x18 == 0 */
            s_text(s, r, 0x1a);
            if (I16(r, 0x18) == 0) { size_t w = 0; for (size_t i = 0; i < s->n; i++) if (s->s[i] != '[' && s->s[i] != ']') s->s[w++] = s->s[i]; s->n = w; s->s[w] = 0; }
        } else switch (g) {                                       /* jump table 0x18008d04c / 0x18008cfa8 */
        case 901: case 902: case 903: case 904: case 905:
            if (L) { warnf(c, "lathe stock record %d", g); return -1; }
            break;
        case 911: for (int k = 0; k < 10; k++) a_real(c, s, D(r, 0x18 + 8 * k)); break;           /* FUN_1808cbb00 */
        case 912: for (int k = 0; k < 10; k++) a_long(s, I32(r, 0x18 + 4 * k)); break;            /* FUN_1808cbab0 */
        case 915: case 916: a_short(s, (int8_t)r[0x18]); a_short(s, I16(r, 0x1a)); break;        /* FUN_1808cc430 / cc3f0 */
        case 921: case 925:                                                                      /* FUN_1808cc380 / cc630 */
            a_short(s, I16(r, 0x18)); a_short(s, g == 921 ? I16(r, 0x1a) : (int8_t)r[0x1a]);
            a_real(c, s, D(r, 0x20)); a_real(c, s, D(r, 0x28)); a_real(c, s, D(r, 0x30)); a_short(s, I16(r, 0x38));
            break;
        case 922:                                                                                /* FUN_1808cc470 */
            a_short(s, I16(r, 0x18)); a_short(s, (int8_t)r[0x1a]); a_short(s, (int8_t)r[0x1b]); a_real(c, s, D(r, 0x20));
            a_short(s, (int8_t)r[0x28]); a_real(c, s, D(r, 0x30)); a_short(s, I16(r, 0x38));
            break;
        case 923:                                                 /* FUN_1808cc4f0(+0x18) (objdump) */
            a_short(s, I16(r, 0x18)); a_short(s, I16(r, 0x1a)); a_short(s, (int8_t)r[0x1c]); a_real(c, s, D(r, 0x20));
            a_real(c, s, D(r, 0x28)); a_short(s, I16(r, 0x30)); a_real(c, s, D(r, 0x38)); a_short(s, I16(r, 0x40));
            break;
        case 924:                                                 /* FUN_1808cc580(+0x18) (objdump) */
            a_short(s, I16(r, 0x18)); a_real(c, s, D(r, 0x20)); a_real(c, s, D(r, 0x28)); a_short(s, (int8_t)r[0x30]);
            a_real(c, s, D(r, 0x38)); a_long(s, I32(r, 0x40)); a_short(s, I16(r, 0x44)); a_short(s, I16(r, 0x46));
            a_short(s, (int8_t)r[0x48]); a_short(s, (int8_t)r[0x49]); a_short(s, I16(r, 0x4a));
            break;
        case 926:
            warnf(c, "misc-ops record 926 (FUN_1808cc6a0: MdComponentId::ToString)"); return -1;
        case 950:                                                 /* FUN_1808caa10(+0x24) @18008a9b5 */
            a_long(s, 0); a_short(s, 0); a_long(s, 0); a_long(s, I32(r, 0x24)); a_long(s, 0); a_short(s, 0);
            for (int k = 0; k < 20; k++) a_short(s, 0);
            break;
        case 999:                                                 /* @18008a9cb: op types 4 / 0x50 / 0x6c print nothing */
            if (I16(r, 0x1c) == 4 || I16(r, 0x1c) == 0x50 || I16(r, 0x1c) == 0x6c) return 0;
            a_short(s, I16(r, 0x1c)); a_short(s, (int16_t)U16(r, 0x1e)); a_long(s, I32(r, 0x18));   /* FUN_1808cc910 */
            break;
        case 1000: case 1001: case 1002: {                        /* @18008a550 */
            const Op *o = op_by_id(f, I32(r, 4));
            /* OMgetOpMachineGroup(op, 0) -> machine (+0x978) -> CtrlNCOutput: +0x1c / +0x20 = (int) its +0 / +8 doubles
               (StartSeqNum / SeqNumInc, the control page 8 nc_output_ctrl McamFile.ctl_seq) */
            if (o && op_in_decoded_group(f, o) && f->ctl_ok) { W32(r, 0x1c, cvtt(f->ctl_seq[0])); W32(r, 0x20, cvtt(f->ctl_seq[1])); }
            else warnf(c, "1000-1002 of op %d: its machine group's control (sequence start / increment) is not the decoded one; stored +0x1c / +0x20 kept", I32(r, 4));
            if (c->flag_55a1) { if (I16(r, 0) != 0x3e9) W16(r, 0, 0x3ea); c->flag_55a1 = 0; }
            if (!L) {
                c->last_feed = -1e38;                             /* 18008a5d5 (unconditional) */
                int16_t hd = I16(r, 0x48), m = 0;                 /* FUN_1808cd410's map of its 11th argument */
                if (hd == 0x10) m = 1; else if (hd == 0x20) m = 2; else if (hd == 0x40) m = 3; else if ((uint16_t)(hd - 1) <= 2) m = hd;
                for (int k = 0; k < 6; k++) a_long(s, I32(r, 0x18 + 4 * k));
                a_short(s, I16(r, 0x30)); a_long(s, I32(r, 0x38)); a_real(c, s, D(r, 0x40)); a_short(s, m);
                a_p3d(c, s, r, 0x50); a_p3d(c, s, r, 0x68); a_short(s, I16(r, 0x80)); a_real(c, s, D(r, 0x88));
            } else { warnf(c, "lathe tool change (flag 1, @18008a669)"); return -1; }
            break;
        }
        case 1003:                                                /* @18008a6ea: FUN_1808cb210 */
            a_real(c, s, D(r, 0x18)); a_real(c, s, D(r, 0x20)); a_real(c, s, D(r, 0x28));
            if (!L) feed_reset(c);
            break;
        case 1004: case 1040: case 1041: feed_reset(c); break;    /* 18008a51c: Empty, then the -1e38 reset */
        case 1005: case 1006: case 1007: case 1026: case 1042: case 1043: case 1051: case 1052: case 1053: case 1055:
            s_text(s, r, 0x18); break;
        case 1008: op_comment_out(c, r); return 1;                /* 18008a4dc: printed by bnci_convert_op_comment only */
        case 1011: case 1031:                                     /* FUN_1808cc330: 10 reals */
            if (!L) feed_reset(c);
            for (int k = 0; k < 10; k++) a_real(c, s, D(r, 0x18 + 8 * k));
            if (g == 1011) c->flag_55a1 = 0;
            break;
        case 1012: case 1032:                                     /* FUN_1808cc220: 10 longs */
            if (!L) feed_reset(c);
            for (int k = 0; k < 10; k++) a_long(s, I32(r, 0x18 + 4 * k));
            if (g == 1032) c->flag_55a1 = 0;
            break;
        case 1013:                                                /* FUN_1808cc270, call 18008a7ed */
            if (!L) feed_reset(c);
            a_short(s, I16(r, 0x18)); a_real(c, s, D(r, 0x20)); a_real(c, s, D(r, 0x28)); a_short(s, I16(r, 0x30));
            a_short(s, I16(r, 0x32)); a_p3d(c, s, r, 0x38); a_short(s, I16(r, 0x50));
            s_cat(s, " "); { char t[NB]; size_t n = 0; while (0x52 + n < NB && r[0x52 + n]) { t[n] = (char)r[0x52 + n]; n++; } t[n] = 0; s_cat(s, t); }
            break;
        case 1014: case 1017: feed_reset(c); a_m33(c, s, r, 0x18); break;    /* FUN_1808cd550 / FUN_1808cac50 */
        case 1016: {                                              /* FUN_1808cbfd0 (dumped; param_2 is short *) */
            feed_reset(c);
            if (I32(r, 0x78) < 0) W32(r, 0x78, 0);
            if (I32(r, 0x78) > 0 && !op_by_id(f, I32(r, 0x78))) W32(r, 0x78, 0);
            if (I32(r, 0x64) < 1 || I32(r, 0x78) < 1) {
                const Op *o = op_by_id(f, I32(r, 4));
                if (!o) warnf(c, "1016: op %d not in the part (Mastercam substitutes _cur_op)", I32(r, 4));
                else if (!op_is_xform(o->opcode)) {
                    if (I32(r, 0x64) < 1) W32(r, 0x64, I32(r, 4));
                    if (I32(r, 0x78) < 1) W32(r, 0x78, I32(r, 4));
                }
            }
            a_long(s, I32(r, 0x64)); a_short(s, I16(r, 0x1c)); a_short(s, I16(r, 0x1e)); a_short(s, I16(r, 0x20));
            a_p3d(c, s, r, 0x28);
            for (int k = 0; k < 5; k++) a_short(s, I16(r, 0x40 + 2 * k));
            a_real(c, s, D(r, 0x50)); a_long(s, I32(r, 0x58)); a_short(s, I16(r, 0x5c)); a_long(s, I32(r, 0x60));
            a_long(s, I32(r, 0x18)); a_real(c, s, D(r, 0x70)); a_long(s, I32(r, 0x78));
            break;
        }
        case 1018: case 1019: warnf(c, "subprogram record %d", g); return -1;
        case 1020: {                                              /* @18008ac5b */
            feed_reset(c);
            if (L) {                                              /* @18008ad6e */
                a_p3d(c, s, r, 0x18); a_p3d(c, s, r, 0x30); a_short(s, I16(r, 0x48));
                for (int k = 0; k < 4; k++) a_real(c, s, 0.0);
                a_long(s, I32(r, 0x70)); a_long(s, I32(r, 0x74)); s_cat(s, " ");
                { char t[NB]; size_t n = 0; while (0x96 + n < NB && r[0x96 + n]) { t[n] = (char)r[0x96 + n]; n++; } t[n] = 0; s_cat(s, t); }
                break;
            }
            const Op *o = op_by_id(f, I32(r, 4));
            if (!(o && o->raw && o->raw_n >= 0x5c0 && f->stock_rec && I32(o->raw, 0x5bc) == f->stock_grp)) {
                warnf(c, "1020 of op %d: its machine group's Stock Setup is not the decoded one (line written empty)", I32(r, 4));
                break;
            }
            int32_t rpm = c->env.max_spindle_rpm;
            if (!c->env.have_max_rpm && !max_tool_rpm(c, o, &rpm)) warnf(c, "1020 of op %d: CCNCMachine::MaxToolSpindleRPM not resolved (placeholder %d)", I32(r, 4), rpm);
            /* FUN_1808cc9a0(group +0x1230 size, group +0x1248 origin, group +0x1690, +0x50, +0x58, +0x60, 0.0, +0x70,
               MaxToolSpindleRPM, group +0x11d0 material) */
            for (int k = 0; k < 3; k++) a_real(c, s, f->stock[k]);
            for (int k = 0; k < 3; k++) a_real(c, s, f->stock_org[k]);
            a_short(s, (int16_t)f->stock_sel);
            a_real(c, s, D(r, 0x50)); a_real(c, s, D(r, 0x58)); a_real(c, s, D(r, 0x60)); a_real(c, s, 0.0);
            a_long(s, I32(r, 0x70)); a_long(s, rpm);
            s_cat(s, " "); s_cat(s, f->grp_material);
            break;
        }
        case 1025: {                                              /* FUN_1808caa80 (objdump): 20 shorts, codes 3101-3120 / 4101-4120 /
                                                                     5101-5120 renumbered to x050-x069; others printed as stored */
            if (!L) feed_reset(c);
            for (int k = 0; k < 20; k++) {
                int16_t raw = I16(r, 0x18 + 2 * k); uint16_t v = (uint16_t)abs(raw); int16_t out = raw;
                if ((uint16_t)(v - 3101) <= 19 || (uint16_t)(v - 4101) <= 19 || (uint16_t)(v - 5101) <= 19) {
                    int base = 0;
                    if ((uint16_t)(v - 3001) <= 998) base = 3000;
                    if ((uint16_t)(v - 4001) <= 998) base = 4000;
                    if ((uint16_t)(v - 5001) <= 998) base = 5000;
                    while ((int16_t)v > 1000) v = (uint16_t)((int16_t)v % 1000);
                    out = (int16_t)(v + base - 51);
                }                                                 /* 3050-3069 / 4050-4069 / 5050-5069 assert, printed as stored */
                a_short(s, out);
            }
            break;
        }
        case 1027: feed_reset(c); a_m33(c, s, r, 0x18); a_p3d(c, s, r, 0x60); break;   /* FUN_1808cd580 */
        case 1028:                                                /* FUN_1808cb3f0, call 18008ae2c */
            if (L) break;
            a_short(s, I16(r, 0x18)); a_short(s, I16(r, 0x1c)); a_p3d(c, s, r, 0x20); a_p3d(c, s, r, 0x38);
            a_short(s, I16(r, 0x50)); a_short(s, I16(r, 0x54)); a_long(s, I32(r, 0x58)); a_short(s, I16(r, 0x5c));
            break;
        case 1029:                                                /* FUN_1808cb490, call 18008ae62 */
            if (L) break;
            a_p3d(c, s, r, 0x18); a_p3d(c, s, r, 0x30); a_p3d(c, s, r, 0x48); a_bool(s, r[0x60]); a_short(s, I16(r, 0x62));
            break;
        default: break;                                           /* Empty (18008b52e) */
        }
    } else if (g == 900) {
        if (L) { warnf(c, "lathe record 900"); return -1; }
    } else switch (g) {                                           /* jump table 0x18008ced4 / 0x18008ce98 */
    case -9: return 0;
    case -8: c->flag_55a1 = 1; return 0;
    case 0:                                                       /* @180089c97: FUN_1808cb960 */
        a_short(s, I16(r, 0x18)); a_real(c, s, D(r, 0x20)); a_real(c, s, D(r, 0x28)); a_real(c, s, D(r, 0x30));
        a_real(c, s, L ? D(r, 0x38) : -2.0); a_long(s, pos_to_ascii(L, U16(r, 0x40)));
        if (!L && c->p5) c->last_feed = 1e38;
        break;
    case 1: {                                                     /* @180089d6e */
        double fr = L ? D(r, 0x38) : modal_feed(c, D(r, 0x38));
        a_short(s, I16(r, 0x18)); a_real(c, s, D(r, 0x20)); a_real(c, s, D(r, 0x28)); a_real(c, s, D(r, 0x30));
        a_real(c, s, fr); a_long(s, pos_to_ascii(L, U16(r, 0x40)));
        break;
    }
    case 2: case 3: {                                             /* @180089e37: FUN_1808ca930 */
        double fr = L ? D(r, 0x50) : modal_feed(c, D(r, 0x50));
        a_short(s, I16(r, 0x18)); a_short(s, I16(r, 0x1a)); a_real(c, s, D(r, 0x20)); a_real(c, s, D(r, 0x28));
        a_real(c, s, D(r, 0x38)); a_real(c, s, D(r, 0x40)); a_real(c, s, D(r, 0x30)); a_real(c, s, fr);
        a_long(s, pos_to_ascii(L, U16(r, 0x58))); a_bool(s, L ? 0 : r[0x5a]);
        break;
    }
    case 4:                                                       /* @180089fa2: FUN_1808cb0f0 */
        a_real(c, s, D(r, 0x18)); a_long(s, cvtt(D(r, 0x20))); a_short(s, L ? I16(r, 0x28) : 0);
        break;
    case 11: {                                                    /* @180089fd3: FUN_1808ca8a0 */
        double fr = modal_feed(c, D(r, 0x48));
        a_p3d(c, s, r, 0x18); a_p3d(c, s, r, 0x30); a_real(c, s, fr); a_short(s, I16(r, 0x50));
        a_long(s, pos_to_ascii(0, U16(r, 0x52))); a_p3d(c, s, r, 0x58);
        break;
    }
    case 80: feed_reset(c); break;                                /* 18008a51c */
    case 81: {                                                    /* @18008a079: FUN_1808cacd0 */
        if (!L && c->p5) c->last_feed = D(r, 0x40);
        a_short(s, I16(r, 0x18));
        for (int k = 0; k < 16; k++) a_real(c, s, D(r, 0x20 + 8 * k));
        a_long(s, pos_to_ascii(L, U16(r, 0xa0))); a_short(s, I16(r, 0xa4)); a_real(c, s, L ? 0.0 : D(r, 0xa8));
        break;
    }
    case 82: for (int k = 0; k < 10; k++) a_real(c, s, D(r, 0x18 + 8 * k)); break;   /* FUN_1808caef0 */
    case 83:                                                      /* @18008a1c3 (flag 0 only): FUN_1808cae60 */
        if (L) break;
        a_p3d(c, s, r, 0x18); a_p3d(c, s, r, 0x48); a_short(s, I16(r, 0x64)); a_short(s, I16(r, 0x68)); a_p3d(c, s, r, 0x30);
        a_long(s, I32(r, 0x60));                                  /* FUN_1800a5540: the sum of the set bits = the value itself */
        break;
    case 100:                                                     /* @18008a202: FUN_1808caf40 */
        a_short(s, 0);
        for (int k = 0; k < 10; k++) a_real(c, s, D(r, 0x20 + 8 * k));
        a_long(s, pos_to_ascii(0, U16(r, 0x70))); a_short(s, I16(r, 0x74)); a_real(c, s, D(r, 0x78));
        for (int k = 0; k < 9; k++) a_real(c, s, D(r, 0x80 + 8 * k));
        a_real(c, s, D(r, 0xc8));
        break;
    case 200: case 201:
        if (L) { warnf(c, "lathe thread record %d", g); return -1; }
        break;
    default: break;                                               /* Empty (18008b52e) */
    }
    emit(c, r, s, p4);
    return 1;
}

/* ---------------------------------------------------------------- single-record API */
static MpNciConv *conv_init(MpNciConv *c, const McamFile *f, const MpNciEnv *env) {
    memset(c, 0, sizeof *c);
    c->f = f;
    if (env) c->env = *env; else mp_nci_env_default(&c->env, f);
    c->p5 = 1; c->last_feed = -1e38;
    return c;
}
MpNciConv *mp_nci_conv_new(const McamFile *f, const MpNciEnv *env) { return conv_init(malloc(sizeof(MpNciConv)), f, env); }
void mp_nci_conv_free(MpNciConv *cv) { if (cv) { free(cv->out.mem); free(cv); } }

int mp_nci_record_ascii(MpNciConv *cv, const uint8_t *nci_bin, size_t n, int opcode, char *line1, size_t n1, char *line2, size_t n2) {
    static const McamFile empty;
    MpNciConv tmp, *c = cv ? cv : conv_init(&tmp, &empty, NULL);
    if (!cv) c->p5 = 0;
    uint8_t r[NB + 1]; memset(r, 0, sizeof r); memcpy(r, nci_bin, n < NB ? n : NB);   /* [R]: zero-filled nci_bin */
    Sink save = c->out; c->out = (Sink){ 0 };
    long before = c->st.lines;
    int rc = b2s(c, r, opcode, 1);
    int pairs = (int)(c->st.lines - before);
    if (line1 && n1) line1[0] = 0;
    if (line2 && n2) line2[0] = 0;
    if (rc > 0 && c->out.mem) {
        char *a = c->out.mem, *e1 = strchr(a, '\n'), *b = e1 ? e1 + 1 : a + strlen(a), *e2 = strchr(b, '\n');
        if (line1 && n1) snprintf(line1, n1, "%.*s", (int)(e1 ? e1 - a : (long)strlen(a)), a);
        if (line2 && n2) snprintf(line2, n2, "%.*s", (int)(e2 ? e2 - b : (long)strlen(b)), b);
    }
    free(c->out.mem); c->out = save;
    return rc < 0 ? -1 : pairs;
}

/* ---------------------------------------------------------------- environment defaults */
void mp_nci_env_default(MpNciEnv *e, const McamFile *f) {
    memset(e, 0, sizeof *e);
    e->n_places_nci = 8;        /* mastercam.dll .data 0x18087b884 = 08 00; written only by scoped save/restore guards
                                   (0x18044191c, toolmgr FormatReal) */
    e->tol = 5e-05;             /* mccore ?tol .data 0x1812b5b58 */
    e->vers_no = 22; e->m_vers_no = 1;   /* mccore .data 0x18143d54a / 0x18143d54b = 0x16 0x01 */
    if (f && f->path[0]) {
        snprintf(e->geo_filename, sizeof e->geo_filename, "%s", f->path);
        /* FUN_18034b270(geo_filename, out, 0): FUN_18034b160 = the file's last-write time (static MFC file-find helpers
           0x180b9219c / 0x180b92370), _localtime64_s, "%d/%d/%d %d:%d:%d" (@0x180d47050) = mday mon+1 year+1900 hour min sec */
        struct stat st;
        if (stat(f->path, &st) == 0) {
            struct tm tm; time_t t = st.st_mtime;
            if (localtime_r(&t, &tm)) { e->have_date = 1; e->day = tm.tm_mday; e->mon = tm.tm_mon + 1; e->year = tm.tm_year + 1900;
                                        e->hour = tm.tm_hour; e->min = tm.tm_min; e->sec = tm.tm_sec; }
        }
    }
}

/* ---------------------------------------------------------------- the file writer [N] + convert_section [C] */
typedef struct {                /* the five per-file values bnci_to_nci passes to FUN_180083560 (call 1800891a2) */
    int p3;                     /* [rsp+0x51]: a tool change was written */
    int32_t p4, p5, p6, p7;     /* [rsp+0x60] tool +0x24, [rsp+0x74] +0x2c (lathe +0x28), [rsp+0x78] 1016 +0x60, [rsp+0x7c] 1016 +0x58 */
} FileState;

/* bnci_read_from_disk [R] into the persistent read buffer R (FUN_180083560's local_6888): the stored bytes go to a
   zeroed 0x370 buffer, then the stored +0x14 byte count of it is copied over R - bytes of R past that count keep the
   previous record's values; +0x10 = the record's file position. Returns 0 at a size 0 / > 0x6e0 / truncated record. */
static int read_rec(const uint8_t *d, size_t len, size_t *off, uint8_t *R) {
    if (*off + 2 > len) return 0;
    uint16_t sz = (uint16_t)(d[*off] | d[*off + 1] << 8);
    if (sz == 0 || sz > 0x6e0 || *off + 2 + sz > len) return 0;
    uint8_t tmp[NB]; memset(tmp, 0, sizeof tmp);
    size_t k = sz < NB ? sz : NB; memcpy(tmp, d + *off + 2, k);
    uint32_t cnt = (uint32_t)I32(tmp, 0x14); if (cnt > NB) cnt = NB;   /* ours: clamp (Mastercam copies it unchecked) */
    memcpy(R, tmp, cnt);
    W32(R, 0x10, (int32_t)*off);
    /* +0x14 = bnci_bytes(gcode) (not printed by any formatter here); the version fix-ups of [R] for files of version
       0x12 / sub < 0x40 (20004 / 20006 / 20007, 1016 +0x78) depend on the temporary NCI file's MCFile version:
       NOT DECODED (not applied) */
    *off += 2 + sz;
    return 1;
}

typedef struct {
    MpNciConv *c;
    const uint8_t *d; size_t len;
    FileState fs;
    struct { const uint8_t *rec; int16_t g; } *tc; int ntc;   /* tool-change gcodes set by assign_tool_changes */
    struct { const uint8_t *rec; int16_t w; } *wo; int nwo;   /* 1016 +0x42 work offsets set by renumber_work_offsets */
} Ctx;

static void wr(Ctx *x, uint8_t *r, int opcode, int p5) {          /* bnci_to_string(rec, opcode, file, 1, p5, NULL) */
    int keep = x->c->p5; x->c->p5 = p5;
    int rc = b2s(x->c, r, opcode, 1);
    x->c->p5 = keep;
    (void)rc;
}
static void wr_text(Ctx *x, int gcode, const char *t, size_t maxn, int opcode) {   /* memset 0x370, gcode, _mbsnbcpy_s(+0x18) */
    uint8_t r[NB + 1]; memset(r, 0, sizeof r); W16(r, 0, (int16_t)gcode);
    size_t L = strlen(t); if (L > maxn) L = maxn; memcpy(r + 0x18, t, L);
    wr(x, r, opcode, 1);
}

/* the comments a tool change / 1011 writes once per op ([C] case 1000-1002 and 0x3f3) */
typedef struct { int m43, m46, m45, m47; } Once;
static void comments(Ctx *x, const Op *o, Once *on, int opcode) {
    const McamFile *f = x->c->f;
    int grp = op_in_decoded_group(f, o);
    if (!on->m43) {                                               /* 1051: CCNCMachine::Comment (_mbsnbcpy_s 0x105 / 0x104) */
        if (!grp) warnf(x->c, "1051 machine comment of op %d: machine of an undecoded group", o->id);
        else if (f->mach_comment[0]) wr_text(x, 0x41b, f->mach_comment, 0x104, opcode);
        on->m43 = 1;
    }
    if (!on->m46) {                                               /* 1053: op_group +4, "Undefined" (mccoreres 5014 = 0x1396) when empty */
        if (!grp) warnf(x->c, "1053 group name of op %d: undecoded group", o->id);
        else wr_text(x, 0x41d, f->grp_name[0] ? f->grp_name : "Undefined", 0x104, opcode);
        on->m46 = 1;
    }
    if (!on->m45) {                                               /* 1052: op_group +0x980 (0x2ee) */
        if (!grp) warnf(x->c, "1052 group comment of op %d: undecoded group", o->id);
        else if (f->grp_comment[0]) wr_text(x, 0x41c, f->grp_comment, 0x2ee, opcode);
        on->m45 = 1;
    }
    if (!on->m47) {                                               /* 1008: op +6 (0x78) */
        const uint8_t *p = opf(o, 6, 0x78); char t[0x79] = "";
        if (p) { size_t n = 0; while (n < 0x78 && p[n]) { t[n] = (char)p[n]; n++; } t[n] = 0; }
        if (t[0]) wr_text(x, 0x3f0, t, 0x78, opcode);
        on->m47 = 1;
    }
}

static const char *op_ncname_b(const Op *o, char *b) {         /* op +0x170: the NC file name ([N] _mbsicmp(op +0x170, ...)) */
    const uint8_t *p = opf(o, 0x170, 1); b[0] = 0;
    if (p) { size_t n = 0; while (n < 0x104 && 0x68 + 0x170 + n < o->raw_n && p[n]) { b[n] = (char)p[n]; n++; } b[n] = 0; }
    return b;
}
#define op_ncname(o) op_ncname_b((o), (char[0x105]){0})
/* the op's NCI section: the bytes after its -2 record (bnci_read_sos, see convert_section); 0 = none */
static int sec_find(const Ctx *x, const Op *o, const uint8_t **dp, size_t *lenp, size_t *offp) {
    const uint8_t *d = x->d; size_t len = x->len, off = 0;
    if (o->nci) { d = o->nci; len = o->nci_n; } else off = x->c->f->nci_off;
    if (!d) return 0;
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        int hit = sz >= 0x22 && I16(d, off + 2) == -2 && I32(d, off + 2 + 0x1c) == o->id && d[off + 2 + 0x18];
        off += 2 + sz;
        if (hit) { *dp = d; *lenp = len; *offp = off; return 1; }
    }
    return 0;
}
static const Tool *tool_by_slot(const McamFile *f, int slot) { for (int i = 0; i < f->ntools; i++) if (f->tools[i].slot == slot && slot >= 0) return &f->tools[i]; return NULL; }

/* mcmill CBnciPost::AssignToolChanges (FUN_180037ca0, decomp/mcmill_all.c l.39435): before posting, the selected ops'
   sections (op +0xe8 = 0; grouped by NC file name in first-appearance order, the while loop after the section reads) get
   their tool-change gcodes recomputed and written back to the binary NCI (FUN_18003b440 / CBnciPost::WriteToBnci
   FUN_18003b930); bnci_to_nci then converts them unchanged. Read records: 1000-1002, 1016, 1018, 1019. */
static void assign_tool_changes(Ctx *x, const char *sel) {
    MpNciConv *c = x->c; const McamFile *f = c->f;
    int *order = malloc(sizeof(int) * (size_t)(f->nops + 1)), n = 0;
    char *done = calloc((size_t)f->nops + 1, 1);
    for (int i = 0; i < f->nops; i++) {                           /* sections, then grouped by name (Ordinal_2863 compare) */
        const Op *o = &f->ops[i]; const uint8_t *e = opf(o, 0xe8, 1);
        if (!sel[i] || done[i] || !o->raw || !e || *e) continue;
        char name[0x105]; snprintf(name, sizeof name, "%s", op_ncname(o));
        for (int j = i; j < f->nops; j++) {
            const Op *q = &f->ops[j]; const uint8_t *qe = opf(q, 0xe8, 1);
            if (!sel[j] || done[j] || !q->raw || !qe || *qe || strcmp(op_ncname(q), name) != 0) continue;
            order[n++] = j; done[j] = 1;
        }
    }
    uint8_t s1016[NB]; memset(s1016, 0, sizeof s1016);            /* local_478 */
    int32_t prev_tool = 0, prev_op = 0, prev60 = 0;               /* uVar33, local_668, local_638 */
    int answer = (int16_t)0xffff;                                 /* uVar34 */
    const Op *prev_sec = NULL; int first = 1;                     /* local_5c0, bVar37 */
    for (int k = 0; k < n; k++) {
        const Op *op = &f->ops[order[k]];
        const uint8_t *d; size_t len, off;
        if (!sec_find(x, op, &d, &len, &off)) continue;           /* ReadSection < 1: the section is dropped */
        int xform = op_is_xform(op->opcode);                      /* local_683 */
        if (k == 0) first = 1;
        else {
            const Op *pv = &f->ops[order[k - 1]]; const uint8_t *a = opf(pv, 0x11b8, 1), *b = opf(op, 0x11b8, 1);
            if (strcmp(op_ncname(pv), op_ncname(op)) != 0 || (a && b && *a != *b)) first = 1;   /* name / section +0x2a */
        }
        const Op *src = op; int insub = 0, cnt = 0;               /* local_678, local_680, local_660 */
        while (off + 2 <= len) {
            uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
            if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
            const uint8_t *r = d + off + 2; off += 2 + sz;
            int g = I16(r, 0);
            if (g == -3) break;
            if (g == 0x3f8) { uint32_t m = (uint32_t)I32(r, 0x14); if (m > sz) m = sz; if (m > NB) m = NB; memcpy(s1016, r, m); continue; }
            if (g == 0x3fa) { insub = xform; continue; }
            if (g == 0x3fb) { insub = 0; continue; }
            if (g < 1000 || g > 1002) continue;                   /* CBnciReadWrite::IsToolChangeGcode */
            if (src->id != I32(s1016, 0x64) && op_is_xform(op->opcode)) {
                src = op_by_id(f, I32(s1016, 0x64));
                if (!src) { warnf(c, "AssignToolChanges: op %d's 1016 +0x64 op not found", op->id); break; }
            }
            int wire = op_is_wire(src->opcode); int32_t tool = I32(r, 0x24), cur = src->id, id60 = I32(s1016, 0x60);
            if (prev_op != cur) cnt = 0;
            const uint8_t *f289 = opf(src, 0x289, 1);
            int force = !(op_is_notool(src->opcode) || wire || !f289 || *f289 == 0);   /* bVar6 */
            if (prev_sec && op_is_lathe(prev_sec->opcode) != op_is_lathe(op->opcode)) prev60 = id60 + 1;
            prev_sec = op;
            int ng;
            if (first) ng = 1001;
            else if (wire || prev60 == id60) {
                int b37 = 1;
                if (prev_op == cur) {
                    if (id60 == prev60) {
                        cnt++;
                        if (!insub || !force) { if (cnt >= 1) { ng = 1000; b37 = 0; } }
                        else cnt = 0;
                    } else cnt = 0;
                } else if (prev_tool != tool) { ng = 1002; b37 = 0; }
                if (b37) ng = force ? 1002 : 1000;
            } else if (prev_tool == tool) {                       /* same tool number, another tool id */
                int t20 = 0;
                if (!op_is_lathe(src->opcode)) {                  /* Cnc::Tool lookup by id: MCToolType 0x14 */
                    const Tool *pt = tool_by_slot(f, prev60);
                    if (pt && pt->type == 0x14) t20 = 1;
                    else { const Tool *ct = tool_by_slot(f, id60); if (ct) t20 = ct->type == 0x14; }
                }
                if (answer < 0 && !t20) {
                    const Op *po = op_by_id(f, prev_op), *co = op_by_id(f, cur);
                    const uint8_t *pa = opf(po, 0x11bc, 4), *ca = opf(co, 0x11bc, 4);
                    if (force || (pa && ca && I32(pa, 0) != I32(ca, 0))) answer = 1002;
                    else { answer = c->env.same_tool_answer ? c->env.same_tool_answer : 1002;
                           warnf(c, "AssignToolChanges: tool %d reused with another tool id - Mastercam asks (message 0x13a4 / 0xefe; batch mode = 1002); %d used", tool, answer); }
                }
                ng = answer;
            } else ng = 1002;
            x->tc = realloc(x->tc, sizeof *x->tc * (size_t)(x->ntc + 1));
            x->tc[x->ntc].rec = r; x->tc[x->ntc].g = (int16_t)ng; x->ntc++;
            prev_op = cur; prev60 = id60; prev_tool = tool; first = 0;
            if (insub && ng >= 1000 && ng <= 1002) insub = 0;
        }
    }
    free(order); free(done);
}

/* ---------------------------------------------------------------- CBnciPost::RenumberWorkOffsets [W]
   mcmill FUN_180039940 (decomp/mcmill_all.c l.40659; its DebugStr names it), run by the Post command's pre-pass
   FUN_1800394e0 after AssignToolChanges: every 1016 whose work offset (+0x42) is negative ("Automatic") gets the number
   mastercam.dll's WorkOffsetUtil assigns (class layout and bodies: decomp/opman_dialogs.c l.4007 MapAddNci @180447a10,
   l.4880 BuildWorkOffsetMap @1804446a0, l.3719 MapAddOperations @1804486a0, l.5112 GetMatchingWorkOffset @1804471c0;
   the rest dumped to decomp/mastercam_workoffsetutil.c: MapAdd @180447810, AssignWorkOffsetNumber @180444360,
   MapAddPlanes @180448af0, NumberUndefinedWorkOffsetsInMap @180448ed0, SkipOperation @1804491f0, CheckForConflict
   @180445cc0, GetMapNodesByWorkOffset @180446f70, GetNextWorkOffset @1804475c0, SetBuildParameters @1800a98c0 /
   FUN_1800a5c00, WorkOffsetNode ctor @1800a2f20, ValidMachineGroupID @180449820, RequiresMachineGroupID @180449150,
   P3D::IsEqual @180430170, Matrix33 / P3D ctors and operator= @180427890 / 1804277f0 / 180427be0 / 180427c60 /
   18042f7d0 / 18042f910 / 18042f940, Matrix33::IsLeftHand @180429890, CheckDotProduct @180428b30 / 180428a90,
   Matrix33::Serialize @18042a880, MastercamPlanes::GetPlanes @1802b7530).

   WorkOffsetUtil (this): +8 std::map<MapKey, WorkOffsetNode> keyed (work offset, machine group id) (lexicographic, the
   compares in GetNextWorkOffset), +0x10 its size, +0x18 std::vector<WorkOffsetNode> of every node MapAdd saw, +0x98
   BuildParameters: +0x98 flags (1 planes, 2 operations, 4 NCI), +0x9c include "Automatic" (negative) offsets, +0x9d
   number them, +0xa0 matching mode, +0xa4 allow conflicts, +0xa8 first number, +0xac increment, +0xb0 include-op list,
   +0x118 exclude-op list, +0x180 op list of GetMatchingWorkOffset.
   WorkOffsetNode (0x5f0 bytes): +8 work offset, +0xc machine group id, +0x10 kind (2 operation, 4 plane, 5 NCI), +0x14
   how the number was got, +0x18 conflict, +0x20 Matrix33, +0xf0 origin P3D (world), +0x5b8 op id.
   Matrix33 = 3 row P3Ds + byte +0xc8 (= !IsValid); P3D = x y z + tol (+0x20; 2e-7 = 0x3e8ad7f29abcaf48 from P3D() /
   P3D(p_3d); operator=(p_3d) keeps the target's tol, operator=(P3D) copies it). */
typedef struct { double m[9], mtol[3]; int mbad; double o[3], otol; int w, mg, kind, how, conflict, opid; } WoNode;
typedef struct {
    WoNode *map; int nmap;          /* +8 / +0x10, kept sorted by (w, mg) */
    WoNode *vec; int nvec;          /* +0x18 */
    int flags, inc_auto, number, mode, allow_conflicts, start, step;   /* +0x98 .. +0xac */
    const char *incl;               /* +0xb0 (selected ops, by F.ops index; NULL = empty) */
    MpNciConv *c;
} WoUtil;
#define P3D_TOL 2e-7                /* 0x3e8ad7f29abcaf48 */

/* P3D::IsEqual(other, 0.) @180430170: every |difference| < this's tol */
static int wo_p_eq(const double *a, double tol, const double *b) { for (int k = 0; k < 3; k++) if (!(fabs(a[k] - b[k]) < tol)) return 0; return 1; }
/* Matrix33::IsEqual(other, 0.) @1804296d0 (operator!= @180428020): tol = GetTol(this) @1804295a0 = the smallest row
   tol; the three rows by P3D::IsEqual, then the +0xc8 bytes */
static int wo_m_eq(const WoNode *a, const double *bm, int bbad) {
    double t = a->mtol[0]; if (a->mtol[1] < t) t = a->mtol[1]; if (a->mtol[2] < t) t = a->mtol[2];
    for (int r = 0; r < 3; r++) if (!wo_p_eq(a->m + 3 * r, t, bm + 3 * r)) return 0;
    return a->mbad == bbad;
}
/* Matrix33::IsValid(.., force) @180429a70 checks: IsLeftHand @180429890 (v_cross_3D(row 0) -> row 1 x? .. dot row 2 <= 0;
   the cross product's second operand is not in the decompile: taken as row 1), _fpclass SNaN / QNaN of rows' elements,
   and with all three rows non-zero (an element |v| > 2e-7) CheckDotProduct @180428b30: |row_i . row_j| > 2e-7 -> error.
   Returns the +0xc8 byte (1 = invalid). */
static int wo_m_bad(const double *m) {
    double cx = m[1] * m[5] - m[2] * m[4], cy = m[2] * m[3] - m[0] * m[5], cz = m[0] * m[4] - m[1] * m[3];
    if (cx * m[6] + cy * m[7] + cz * m[8] <= 0.) return 1;
    int rows = 0, nan = 0;
    for (int r = 0; r < 3; r++) {
        int nz = 0;
        for (int k = 0; k < 3; k++) { if (fabs(m[3 * r + k]) > 2e-7) nz = 1; if (isnan(m[3 * r + k])) nan = 1; }
        rows += nz;
    }
    if (nan) return 1;
    if (rows == 3) {
        double d01 = m[0] * m[3] + m[1] * m[4] + m[2] * m[5], d02 = m[0] * m[6] + m[1] * m[7] + m[2] * m[8],
               d12 = m[3] * m[6] + m[4] * m[7] + m[5] * m[8];
        if (fabs(d01) > 2e-7 || fabs(d02) > 2e-7 || fabs(d12) > 2e-7) return 1;
    }
    return 0;
}
static int wo_key_cmp(int w1, int g1, int w2, int g2) { return w1 < w2 ? -1 : w1 > w2 ? 1 : g1 < g2 ? -1 : g1 > g2; }
static WoNode *wo_map_find(WoUtil *u, int w, int mg) { for (int i = 0; i < u->nmap; i++) if (u->map[i].w == w && u->map[i].mg == mg) return &u->map[i]; return NULL; }
static int wo_op_group(const Op *o) { return o && o->raw && o->raw_n >= 0x5c0 ? I32(o->raw, 0x5bc) : 0; }   /* see op_in_decoded_group */
/* WorkOffsetNode::RequiresMachineGroupID @180449150: (kind & ~4) != 0; ValidMachineGroupID @180449820: or mg > 0 */
static int wo_valid_mg(const WoNode *n) { return !((n->kind & ~4) != 0) || n->mg > 0; }

/* GetNextWorkOffset(mg, start, step) @1804475c0 (step != 0): from max(start, 0) by max(step, 1), the first number whose
   key (n, mg) is not in the map */
static int wo_next(WoUtil *u, int mg, int start, int step) {
    int n = start > -1 ? start : 0, inc = step > 0 ? step : 1;
    if (step == 0) { warnf(u->c, "GetNextWorkOffset with increment 0"); return n; }
    while (wo_map_find(u, n, mg)) n += inc;
    return n;
}
/* GetMatchingWorkOffset(m, o, exact, conflicts_ok, mg) const @1804471c0: map size 0 -> -1; pass 0 = the map in key
   order, pass 1 = the vector in add order; the first node with w >= 0, (exact: how 2 / 3), equal matrix and origin (node
   tols), kind 4 or mg < 1 or the same mg, no conflict (its second sub-pass differs only with conflicts_ok, false here),
   and op id < 1 or +0x180 empty (it is: SetBuildParameters gave an empty list) -> its w */
static int wo_matching(WoUtil *u, const WoNode *q, int exact, int mg) {
    if (u->nmap == 0) return -1;
    for (int pass = 0; pass < 2; pass++) {
        int n = pass ? u->nvec : u->nmap; const WoNode *v = pass ? u->vec : u->map;
        for (int i = 0; i < n; i++) {
            const WoNode *p = &v[i];
            if (p->w < 0 || (exact && !(p->how - 2u < 2))) continue;
            if (!wo_m_eq(p, q->m, q->mbad) || !wo_p_eq(p->o, p->otol, q->o)) continue;
            if (!(p->kind == 4 || mg < 1 || mg == p->mg)) continue;
            if (p->conflict) continue;
            return p->w;
        }
    }
    return -1;
}
/* AssignWorkOffsetNumber @180444360 */
static void wo_assign(WoUtil *u, WoNode *n) {
    int how = n->w >= 0;
    if (!u->number || n->w >= 0) { n->how = how; return; }
    if (u->mode != 0) {
        n->w = wo_matching(u, n, u->mode == 2, n->mg);
        if (n->w > -1) { n->how = 3; return; }
    } else if (n->w >= 0) { n->how = how; return; }
    n->w = wo_next(u, n->mg, u->start, u->step);
    n->how = n->w > -1 ? 2 : how;
}
/* CheckForConflict @180445cc0: w >= 0 and a valid mg; the map nodes with that w (and mg, when mg > 0;
   GetMapNodesByWorkOffset @180446f70) whose matrix or origin differs (this node's tols) */
static int wo_conflict(WoUtil *u, const WoNode *n) {
    if (n->w < 0 || !wo_valid_mg(n)) return 0;
    for (int i = 0; i < u->nmap; i++) {
        const WoNode *p = &u->map[i];
        if (!(n->mg < 1 || n->mg == p->mg) || p->w != n->w) continue;
        if (!wo_m_eq(n, p->m, p->mbad) || !wo_p_eq(n->o, n->otol, p->o)) return 1;
    }
    return 0;
}
/* MapAdd @180447810: a missing mg is the op's machine group (OMgetOpMachineGroup(node +0x5b8)); number, conflict
   flag, vector push, then the map insert (the std::map keeps the first node of a key). The node is updated in place. */
static int wo_map_add(WoUtil *u, WoNode *n) {
    if (!wo_valid_mg(n)) {
        n->mg = wo_op_group(op_by_id(u->c->f, n->opid));
        if (n->mg < 1) { warnf(u->c, "WorkOffsetUtil::MapAdd(): op %d has no machine group (debug string \"invalid machine group ID\")", n->opid); return 0; }
    }
    wo_assign(u, n);
    n->conflict = wo_conflict(u, n);
    u->vec = realloc(u->vec, sizeof *u->vec * (size_t)(u->nvec + 1)); u->vec[u->nvec++] = *n;
    if (!u->allow_conflicts && n->conflict) return 0;
    WoNode *e = wo_map_find(u, n->w, n->mg);
    if (e) return 0;                                              /* same key: the loop's match or the failed insert */
    int at = 0; while (at < u->nmap && wo_key_cmp(u->map[at].w, u->map[at].mg, n->w, n->mg) < 0) at++;
    u->map = realloc(u->map, sizeof *u->map * (size_t)(u->nmap + 1));
    memmove(u->map + at + 1, u->map + at, sizeof *u->map * (size_t)(u->nmap - at));
    u->map[at] = *n; u->nmap++;
    return 1;
}
/* SkipOperation @1804491f0: id < 1, in the exclude list (empty here), or not in a non-empty include list */
static int wo_skip(const WoUtil *u, const McamFile *f, int id) {
    if (id < 1) return 1;
    if (!u->incl) return 0;
    for (int i = 0; i < f->nops; i++) if (f->ops[i].id == id) return !u->incl[i];
    return 1;
}
/* op_has_no_view @1800c58e0 (mccore) */
static int op_has_no_view(int op) {
    if (op == 0 || op == 0x11 || op == 0x2f || op == 0x65) return 1;
    if (op == 0x89) return 0;
    return op_is_xform(op) || op_c7000(op);
}

/* the view P3D::ViewToWorld uses: Matrix33::GetViewNo(true) @1804295d0 -> FindView @180428d00 (formats/bbox_toolplane.md):
   invalid -> 0 (no node); view 1 = identity, then the session's view table mcViews by Matrix33 == with tol GetTol =
   2e-7; none -> AddMCView adds the matrix as a new view. The reserved views 1..8 (mcam_std_view) are matched here; the
   session's other views (Mastercam's mcViews beyond the reserved ones) are NOT DECODED: the matrix itself is used (any
   such view equals it within 2e-7 per element). Returns 0 for an invalid matrix. */
static int wo_view_matrix(const WoNode *n, double vm[9]) {
    if (n->mbad) return 0;
    for (int v = 1; v <= 8; v++) {
        double sm[9]; if (!mcam_std_view(v, sm)) continue;
        if (wo_m_eq(n, sm, 0)) { memcpy(vm, sm, sizeof sm); return v; }
    }
    memcpy(vm, n->m, sizeof n->m);
    return 9;
}

/* MapAddNci(bnci, op, vec, mg) @180447a10 over one record sequence (GetGcodeLinePtrs with the util's gcode mask; only
   1013 / 1014 / 1016 / 1027 act): 1013 = A (clears B C D), 1014 = B (clears C D), 1016 = C (clears D; its +0x64, or the
   op id when < 1, = node +0x5d0), 1027 = D, then with +0x9c set or C's +0x42 >= 0 and all four present: node kind 5,
   w = C +0x42, mg = MG, matrix = B +0x18 (Matrix33(matrix_33)), origin = ViewToWorld(A +0x38, GetViewNo(matrix, true))
   -> MapAdd, and pushed to OUT (out may be NULL). Returns the nodes made. */
typedef struct { uint8_t a[NB], b[NB], c[NB], d[NB]; } WoQuad;
static int wo_map_add_nci(WoUtil *u, const Op *op, const uint8_t *const *recs, const size_t *szs, int nrec, int mg, WoNode *out) {
    if (!(u->flags & 4) || nrec < 1) return 0;
    static WoQuad q; memset(&q, 0, sizeof q); int made = 0;
    for (int i = 0; i < nrec; i++) {
        const uint8_t *r = recs[i]; int g = I16(r, 0);
        uint32_t m = (uint32_t)I32(r, 0x14); if (m > szs[i]) m = (uint32_t)szs[i]; if (m > NB) m = NB;   /* memcpy(.., +0x14) */
        if (g == 0x3f5) { memset(&q, 0, sizeof q); memcpy(q.a, r, m); continue; }
        if (g == 0x3f6) { memset(q.b, 0, NB); memset(q.c, 0, NB); memset(q.d, 0, NB); memcpy(q.b, r, m); continue; }
        if (g == 0x3f8) { memset(q.c, 0, NB); memset(q.d, 0, NB); memcpy(q.c, r, m); continue; }
        if (g != 0x403) continue;
        memset(q.d, 0, NB); memcpy(q.d, r, m);
        if (!(u->inc_auto || I16(q.c, 0x42) > -1)) continue;
        if (I16(q.a, 0) != 0x3f5 || I16(q.b, 0) != 0x3f6 || I16(q.c, 0) != 0x3f8) continue;
        WoNode n; memset(&n, 0, sizeof n);
        for (int k = 0; k < 9; k++) n.m[k] = D(q.b, 0x18 + 8 * k);
        n.mtol[0] = n.mtol[1] = n.mtol[2] = P3D_TOL; n.mbad = wo_m_bad(n.m);
        double vm[9], p[3] = { D(q.a, 0x38), D(q.a, 0x40), D(q.a, 0x48) };
        if (wo_view_matrix(&n, vm) <= 0) continue;                /* GetViewNo <= 0: no node */
        for (int k = 0; k < 3; k++) n.o[k] = vm[k] * p[0] + vm[3 + k] * p[1] + vm[6 + k] * p[2];   /* P3D::ViewToWorld @180430500 */
        n.otol = P3D_TOL; n.kind = 5; n.w = I16(q.c, 0x42); n.mg = mg; n.opid = op->id; n.w = (int16_t)n.w;
        wo_map_add(u, &n);
        if (out && made == 0) out[0] = n;                          /* the caller reads element 0 */
        made++;
    }
    return made;
}

/* the part's MastercamPlanes archive (formats/planes_archive.md; the grammar of mcam_load.c planes_archive, here also
   keeping each Plane's Matrix33 (+0x40) rows and tols, origin PlanePoint's P3D (+0x140, Plane::GetOriginInWorldCoordinates
   @1803daa40) and u32 +0x330 (the work offset: MapAddPlanes reads Plane +0x330)). Ours: reader glue. */
typedef struct { int cls, id, w; double m[9], mtol[3], o[3], otol; } WoPlane;
typedef struct { const uint8_t *b; size_t n, p; int bad, ncls, nobj; char cls[1024][24]; int schema[1024]; } WoAr;
static unsigned war_u8(WoAr *a) { if (a->p + 1 > a->n) { a->bad = 1; return 0; } return a->b[a->p++]; }
static unsigned war_u16(WoAr *a) { if (a->p + 2 > a->n) { a->bad = 1; return 0; } unsigned v = a->b[a->p] | a->b[a->p + 1] << 8; a->p += 2; return v; }
static uint32_t war_u32(WoAr *a) { if (a->p + 4 > a->n) { a->bad = 1; return 0; } uint32_t v; memcpy(&v, a->b + a->p, 4); a->p += 4; return v; }
static double war_f64(WoAr *a) { if (a->p + 8 > a->n) { a->bad = 1; return 0; } double v; memcpy(&v, a->b + a->p, 8); a->p += 8; return v; }
static void war_skip(WoAr *a, size_t k) { if (a->p + k > a->n) { a->bad = 1; a->p = a->n; } else a->p += k; }
static void war_str(WoAr *a) {
    int wide = 0; uint32_t k;
    for (;;) { k = war_u8(a); if (k == 0xff) { k = war_u16(a); if (k == 0xfffe) { wide = 1; continue; } if (k == 0xffff) k = war_u32(a); } break; }
    if (!a->bad) war_skip(a, (size_t)k * (wide ? 2 : 1));
}
static int war_tag(WoAr *a, const char **name) {
    unsigned w = war_u16(a); uint32_t idx;
    if (a->bad) return -2;
    if (w == 0xffff) {
        unsigned sch = war_u16(a), ln = war_u16(a); int k = a->ncls + a->nobj + 1;
        if (a->bad || ln >= 24 || a->p + ln > a->n || k >= 1024) { a->bad = 1; return -2; }
        memcpy(a->cls[k], a->b + a->p, ln); a->cls[k][ln] = 0; a->schema[k] = (int)sch; a->p += ln; a->ncls++;
        *name = a->cls[k]; return (int)sch;
    }
    if (w == 0x7fff) { uint32_t d = war_u32(a); if (!(d & 0x80000000u)) return d ? -1 : -2; idx = d & 0x7fffffffu; }
    else if (w & 0x8000) idx = w & 0x7fff;
    else return w ? -1 : -2;
    if (idx < 1 || idx >= 1024 || !a->cls[idx][0]) { a->bad = 1; return -2; }
    *name = a->cls[idx]; return a->schema[idx];
}
static int war_level(WoAr *a, const char *want) { const char *nm = ""; int v = war_tag(a, &nm); if (v < 0 || strcmp(nm, want)) { a->bad = 1; return -1; } return v; }
static void war_p3d(WoAr *a, double *xyz, double *tol) {
    int v = war_level(a, "P3D"); if (v != 1 && v != 2) { a->bad = 1; return; }
    double t[4]; for (int k = 0; k < 4; k++) t[k] = war_f64(a);
    if (xyz) memcpy(xyz, t, 3 * sizeof *t);
    if (tol) *tol = t[3];
}
static void war_m33(WoAr *a, double *m, double *tol) {
    int v = war_level(a, "Matrix33"); if (v != 1 && v != 2) { a->bad = 1; return; }
    for (int i = 0; i < 3 && !a->bad; i++) war_p3d(a, m ? m + 3 * i : NULL, tol ? tol + i : NULL);
}
static void war_ppt(WoAr *a, double *xyz, double *tol) { int v = war_level(a, "PlanePoint"); if (v != 1 && v != 2) { a->bad = 1; return; } war_p3d(a, xyz, tol); war_skip(a, 50); }
static void war_base(WoAr *a, WoPlane *p) {
    if (war_level(a, "PlaneBase") != 4 || war_level(a, "PlaneID") != 1) { a->bad = 1; return; }
    war_skip(a, 16); p->cls = (int32_t)war_u32(a); p->id = (int32_t)war_u32(a);
    war_m33(a, p->m, p->mtol); war_str(a); war_skip(a, 2);
}
static int wo_planes(const MpNciConv *c, WoPlane **out) {
    const struct PRec *pr = NULL;
    for (int i = 0; i < c->npr; i++) if (c->pr[i].sub == 36 && c->pr[i].n > 0x68 && c->pr[i].r[0x56]) { pr = &c->pr[i]; break; }
    *out = NULL; if (!pr) return -1;
    WoAr *a = calloc(1, sizeof *a); a->b = pr->r + 0x68; a->n = pr->n - 0x68;
    int np = 0; WoPlane *pl = NULL;
    int v = war_level(a, "MastercamPlanes"), sv = war_level(a, "SystemPlanes");
    if (v != 4 || sv < 1 || sv > 4) a->bad = 1;
    if (!a->bad) {
        if (sv < 4) war_skip(a, 6);
        war_m33(a, NULL, NULL); war_p3d(a, NULL, NULL); if (sv < 3) war_skip(a, 4);
        war_skip(a, 32); for (int i = 0; i < 4 && !a->bad; i++) war_m33(a, NULL, NULL);
        for (int i = 0; i < 4 && !a->bad; i++) war_p3d(a, NULL, NULL);
        war_skip(a, 16);
    }
    unsigned cnt = a->bad ? 0 : war_u16(a); if (cnt == 0xffff) cnt = war_u32(a);
    for (unsigned i = 0; i < cnt && !a->bad; i++) {
        war_str(a);
        const char *nm = ""; int sch = war_tag(a, &nm);
        if (sch == -2) { if (a->bad) break; continue; }
        if (sch < 0) { a->bad = 1; break; }
        a->nobj++;
        char cn[24]; snprintf(cn, sizeof cn, "%s", nm);
        WoPlane p; memset(&p, 0, sizeof p);
        if (!strcmp(cn, "Plane")) {
            if (war_level(a, "Plane") != 4) { a->bad = 1; break; }
            war_base(a, &p); war_skip(a, 4);
            war_ppt(a, p.o, &p.otol);
            for (int k = 0; k < 3 && !a->bad; k++) war_ppt(a, NULL, NULL);
            p.w = (int32_t)war_u32(a);                            /* +0x330 */
            static const char T[] = "IBIIIIBIBSBIIBII";
            for (int k = 0; T[k] && !a->bad; k++) { if (T[k] == 'I') war_skip(a, 4); else if (T[k] == 'B') war_skip(a, 1); else war_str(a); }
            if (a->bad) break;
            pl = realloc(pl, sizeof *pl * (size_t)(np + 1)); pl[np++] = p;
        } else if (!strcmp(cn, "PlaneBase")) war_base(a, &p);     /* not a named plane: GetPlanes skips it */
        else a->bad = 1;
    }
    int ok = !a->bad && a->p == a->n; free(a);
    if (!ok) { free(pl); return -1; }
    for (int i = 1; i < np; i++) for (int j = i; j > 0 && (pl[j - 1].cls > pl[j].cls || (pl[j - 1].cls == pl[j].cls && pl[j - 1].id > pl[j].id)); j--) {
        WoPlane t = pl[j]; pl[j] = pl[j - 1]; pl[j - 1] = t; }      /* the std::map order: PlaneID::operator< (class, id, GUID) */
    *out = pl; return np;
}

/* the op's section records with the gcode mask of FUN_1800395c0's ReadSection (1000-1002, 1013, 1014, 1016, 1017, 1027) */
static int wo_section(Ctx *x, const Op *op, const uint8_t ***recs, size_t **szs) {
    const uint8_t *d; size_t len, off; int n = 0;
    *recs = NULL; *szs = NULL;
    if (!sec_find(x, op, &d, &len, &off)) return -1;
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        const uint8_t *r = d + off + 2; off += 2 + sz;
        int g = I16(r, 0);
        if (g == -3) break;
        if (!((g >= 1000 && g <= 1002) || g == 0x3f5 || g == 0x3f6 || g == 0x3f8 || g == 0x3f9 || g == 0x403)) continue;
        *recs = realloc(*recs, sizeof **recs * (size_t)(n + 1)); *szs = realloc(*szs, sizeof **szs * (size_t)(n + 1));
        (*recs)[n] = r; (*szs)[n] = sz; n++;
    }
    return n;
}

/* FUN_180039940 */
static void renumber_work_offsets(Ctx *x, const char *sel) {
    MpNciConv *c = x->c; const McamFile *f = c->f;
    /* BuildParameters {7, 0, 0, mode 1, allow 1, start 0, step 1, include = the selected ops} + BuildWorkOffsetMap on a
       fresh CInterrogateMastercamFile (LoadForWorkOffsetInfo of the open part: ours = the part file) */
    int any = 0;                                                  /* ours: nothing to renumber without a selected op's section */
    for (int i = 0; i < f->nops && !any; i++) {
        if (!sel[i] || !f->ops[i].raw) continue;
        const uint8_t **rr; size_t *ss; if (wo_section(x, &f->ops[i], &rr, &ss) > 0) any = 1; free(rr); free(ss);
    }
    if (!any) return;
    WoUtil u; memset(&u, 0, sizeof u); u.c = c;
    u.flags = 7; u.inc_auto = 0; u.number = 0; u.mode = 1; u.allow_conflicts = 1; u.start = 0; u.step = 1; u.incl = sel;
    int *ord = malloc(sizeof(int) * (size_t)(f->nops + 1)), no = 0;    /* the file's op map: by op id (CLibraryOperation map) */
    for (int i = 0; i < f->nops; i++) if (f->ops[i].raw) ord[no++] = i;
    for (int i = 1; i < no; i++) for (int j = i; j > 0 && f->ops[ord[j - 1]].id > f->ops[ord[j]].id; j--) { int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
    if (u.flags & 1) {                                            /* MapAddPlanes @180448af0 (CInterrogateMastercamFile +0x98) */
        WoPlane *pl; int np = wo_planes(c, &pl);
        if (np < 0) warnf(c, "RenumberWorkOffsets: the part's planes archive (ASSOC 36) was not read; planes left out of the work offset map");
        for (int i = 0; i < np; i++) {
            /* GetPlanes @1802b7530: named planes (class Plane) with Plane +0x120 == 0 (not in the archive: NOT DECODED, taken 0) */
            if (!(u.inc_auto || pl[i].w > -1)) continue;
            WoNode n; memset(&n, 0, sizeof n); n.kind = 4; n.w = pl[i].w;
            memcpy(n.m, pl[i].m, sizeof n.m); memcpy(n.mtol, pl[i].mtol, sizeof n.mtol); n.mbad = wo_m_bad(n.m);   /* Serialize: !IsValid */
            memcpy(n.o, pl[i].o, sizeof n.o); n.otol = pl[i].otol;
            wo_map_add(&u, &n);
        }
        free(pl);
    }
    if (u.flags & 2) {                                            /* MapAddOperations @1804486a0 over the non-skipped ops with a view */
        for (int k = 0; k < no; k++) {
            const Op *o = &f->ops[ord[k]];
            if (wo_skip(&u, f, o->id) || op_has_no_view(o->opcode)) continue;
            const uint8_t *w = opf(o, 0x602, 2), *m = opf(o, 0x578, 72), *p = opf(o, 0x5c0, 24);
            if (!w || !m || !p) { warnf(c, "RenumberWorkOffsets: op %d's views outside the stored record", o->id); continue; }
            if (!(u.inc_auto || I16(w, 0) > -1)) continue;        /* op +0x602 (the Tplane view's work offset) */
            WoNode n; memset(&n, 0, sizeof n); n.kind = 2; n.w = I16(w, 0); n.opid = o->id;
            for (int j = 0; j < 9; j++) n.m[j] = D(m, 8 * j);       /* op +0x578 Tplane matrix (Matrix33::operator=(matrix_33)) */
            n.mtol[0] = n.mtol[1] = n.mtol[2] = P3D_TOL; n.mbad = wo_m_bad(n.m);
            for (int j = 0; j < 3; j++) n.o[j] = D(p, 8 * j);       /* op +0x5c0 Tplane origin */
            n.otol = P3D_TOL;
            wo_map_add(&u, &n);
        }
    }
    if (u.flags & 4) {                                            /* the non-skipped ops' NCI: MapAddNci(.., NULL, 0) */
        for (int k = 0; k < no; k++) {
            const Op *o = &f->ops[ord[k]];
            if (wo_skip(&u, f, o->id)) continue;
            const uint8_t **recs; size_t *szs; int nr = wo_section(x, o, &recs, &szs);
            if (nr > 0) wo_map_add_nci(&u, o, recs, szs, nr, 0, NULL);
            free(recs); free(szs);
        }
    }
    /* +0x9c && +0x9d: NumberUndefinedWorkOffsetsInMap - both 0 in this build */
    /* SetBuildParameters {4, 1, 1, mode 1, allow 1, start 0, step 1, empty lists} */
    u.flags = 4; u.inc_auto = 1; u.number = 1; u.incl = NULL;
    for (int i = 0; i < f->nops; i++) {                           /* the CBnciPost sections (FUN_1800395c0: the selected ops) */
        const Op *op = &f->ops[i];
        if (!sel[i] || !op->raw) continue;
        const uint8_t **recs; size_t *szs; int nr = wo_section(x, op, &recs, &szs);
        if (nr < 1) { free(recs); free(szs); continue; }
        int mg = wo_op_group(op);                                 /* OMgetOpMachineGroup(op id, 1) */
        if (mg < 1) { warnf(c, "RenumberWorkOffsets: op %d: no machine group", op->id); free(recs); free(szs); continue; }
        int ia = -1, ib = -1, ic = -1, id = -1, i1016 = -1;       /* local_3838 / 3ba8 / 4288 / 3f18, lVar14 */
        for (int r = 0; r < nr; r++) {
            int g = I16(recs[r], 0);
            if (g == 0x3f5) { ia = r; ib = ic = id = -1; continue; }
            if (g == 0x3f6) { ib = r; ic = id = -1; continue; }
            if (g == 0x3f8) {
                ic = r; id = -1; i1016 = r;
                if (I16(recs[r], 0x42) >= 0) { i1016 = -1; ia = ib = ic = id = -1; }   /* LAB_18003a7bd */
                continue;
            }
            if (g == 0x403) id = r;
            if (i1016 < 0 || ia < 0 || ib < 0 || ic < 0 || id < 0) continue;
            if (I16(recs[ic], 0x42) >= 0) { i1016 = -1; ia = ib = ic = id = -1; continue; }
            const uint8_t *q[4] = { recs[ia], recs[ib], recs[ic], recs[id] }; size_t qs[4] = { szs[ia], szs[ib], szs[ic], szs[id] };
            WoNode n; int made = wo_map_add_nci(&u, op, q, qs, 4, mg, &n);
            if (made < 1 || n.w < 0) warnf(c, "RenumberWorkOffsets: op %d: unable to update 1016 line (its DebugStr)", op->id);
            else {
                x->wo = realloc(x->wo, sizeof *x->wo * (size_t)(x->nwo + 1));
                x->wo[x->nwo].rec = recs[i1016]; x->wo[x->nwo].w = (int16_t)n.w; x->nwo++;   /* (ushort) node +8 */
                if (getenv("NCI_WO_DEBUG")) fprintf(stderr, "nci_ascii: RenumberWorkOffsets: op %d 1016 @%ld -> work offset %d (how %d, mg %d)\n",
                                                   op->id, (long)I32(recs[i1016], 0x10), n.w, n.how, mg);
            }
            i1016 = -1; ia = ib = ic = id = -1;
        }
        free(recs); free(szs);
    }
    if (getenv("NCI_WO_DEBUG")) for (int i = 0; i < u.nmap; i++)   /* ours (MapDump @180448ce0 analogue) */
        fprintf(stderr, "nci_ascii: work offset map: w %d mg %d kind %d op %d conflict %d origin %g %g %g\n", u.map[i].w, u.map[i].mg,
                u.map[i].kind, u.map[i].opid, u.map[i].conflict, u.map[i].o[0], u.map[i].o[1], u.map[i].o[2]);
    free(ord); free(u.map); free(u.vec);
}

/* FUN_180083560 for one op. Returns 1 when its section was converted to its -3 record. */
static int convert_section(Ctx *x, const Op *o) {
    MpNciConv *c = x->c;
    const uint8_t *d = x->d; size_t len = x->len, off = 0;
    if (o->nci) { d = o->nci; len = o->nci_n; }                   /* ours: an op rewritten here (Reverse toolpath) */
    else off = c->f->nci_off;
    if (!d) { warnf(c, "op %d: no stored NCI data", o->id); return 0; }
    /* bnci_read_sos @180087a10 (objdump): the -2 record at the op's NCI start with +0x1c == op id and +0x18 != 0
       (otherwise FUN_180085cc0 regenerates the op first: NOT DECODED) */
    uint8_t sos[NB + 1]; int found = 0, stale = 0;
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        if (sz >= 0x22 && I16(d, off + 2) == -2 && I32(d, off + 2 + 0x1c) == o->id) {
            if (d[off + 2 + 0x18]) { memset(sos, 0, sizeof sos); memcpy(sos, d + off + 2, sz < NB ? sz : NB); off += 2 + sz; found = 1; break; }
            stale = 1;
        }
        off += 2 + sz;
    }
    if (!found) { warnf(c, "op %d: %s", o->id, stale ? "its -2 record has +0x18 = 0 (Mastercam regenerates it first)" : "no NCI section"); return 0; }
    int opcode = U16(sos, 0x20);                                  /* local_5738 = the -2 record's +0x20 (TP_OPCODE) */
    int optype = o->opcode;                                       /* op +4 */
    if (optype == 0x96) { warnf(c, "op %d: probe op (GetProbe() writers)", o->id); return 0; }
    if (op_is_wire(opcode)) warnf(c, "op %d: wire op (its tool change gcodes 1001 / 1002 and Wire records are not ported)", o->id);
    /* machine +0x10: CCNCMachine::WriteComponentUIDsToNCI / WriteAxisCombinationsToNCI: NOT DECODED (ours: off) */

    if (!op_is_xform(optype)) {                                   /* 999: op id, op type (0x96 -> 0x97), op +0x11b8 byte */
        uint8_t r[NB + 1]; memset(r, 0, sizeof r); W16(r, 0, 999);
        W32(r, 0x18, o->id); W16(r, 0x1c, (int16_t)(optype == 0x96 ? 0x97 : optype));
        const uint8_t *b = opf(o, 0x11b8, 1);
        if (!b) warnf(c, "999 of op %d: op +0x11b8 outside the stored record", o->id);
        W16(r, 0x1e, b ? *b : 0);
        wr(x, r, opcode, 1);
    }
    Once on = { 0 };
    if (op_is_mt_misc_no_tool(opcode)) {
        const uint8_t *p = opf(o, 6, 0x78); char t[0x79] = "";
        if (p) { size_t n = 0; while (n < 0x78 && p[n]) { t[n] = (char)p[n]; n++; } t[n] = 0; }
        if (t[0]) wr_text(x, 0x3f0, t, 0x78, opcode);
        on.m47 = 1;
    }
    if (optype == 0x11 || optype == 0x4d) {                       /* FUN_180083130: an external NCI file (op +0x9b9) */
        const uint8_t *a = opf(o, 0x14b0, 4);
        if (a && I16(a, 0) && I16(a, 2)) { warnf(c, "op %d: its NCI comes from the file at op +0x9b9 (FUN_180083130)", o->id); return 0; }
    }

    uint8_t R[NB + 1]; memset(R, 0, sizeof R);                    /* local_6888 (uninitialised stack in Mastercam; ours: 0) */
    uint8_t slots[24][NB]; memset(slots, 0, sizeof slots);        /* local_53c8: memset 0x5280 */
    uint8_t s1016[NB]; memset(s1016, 0, sizeof s1016);            /* local_6518 (from the zeroed local_6bf8) */
    uint8_t s1014[NB]; memset(s1014, 0, sizeof s1014);            /* local_6bf8 */
    uint8_t s20800[NB]; memset(s20800, 0, sizeof s20800);         /* local_4b8 */
    int in1040 = 0, m42 = 0, lathe = op_is_lathe(opcode);
    for (;;) {
        const uint8_t *recp = d + off + 2;
        if (!read_rec(d, len, &off, R)) { warnf(c, "op %d: NCI data ended before its -3 record", o->id); return 0; }
        for (int k = 0; k < x->ntc; k++) if (x->tc[k].rec == recp) { W16(R, 0, x->tc[k].g); break; }   /* CBnciPost::WriteToBnci */
        for (int k = 0; k < x->nwo; k++) if (x->wo[k].rec == recp) { W16(R, 0x42, x->wo[k].w); break; }   /* RenumberWorkOffsets' SetAt */
        c->st.records++;
        int g = I16(R, 0);
        if (g == 0x410) in1040 = 1; else if (g == 0x411) in1040 = 0;
        /* GetProdEnableBits() & 1 demo limit of 10 / 50 motion records: ours = a full product (bit clear) */
        if (g == -3) return 1;
        /* lathe products (bits 0x120) with 1016 +0x1e in {203, 205, 207} skip 1040 blocks (bnci_read_ignore_end):
           ours = a mill / router product */
        if (g < 200) {
            switch (g) {
            case 0: case 1: case 2: case 3: case 4: case 11: case 20: case 21: case 22: case 23: case 30: case 31:
            case 32: case 33: case 80: case 81: case 82: case 83:
                wr(x, R, opcode, 1); continue;
            case 100: memcpy(R + 0x80, s1014 + 0x18, 0x48); wr(x, R, opcode, 1); continue;   /* local_6808.. = local_6bf8 +0x18.. */
            case -96: case -9: case -8: case -7: case -6: case -5: case -4: case -2: continue;
            default: goto unknown;
            }
        }
        if (g < 202) { wr(x, R, opcode, 1); continue; }
        if (g > 10000) {
            if (g > 0x55f1) { warnf(c, "Wire record %d", g); continue; }
            if (g == 0x55f1) { wr(x, R, opcode, 1); continue; }
            if (g < 0x4ee9) {
                if (g == 0x4ee8) { wr(x, R, opcode, 1); continue; }
                if ((g >= 20000 && g <= 20009) || g == 20020 || g == 20021) { memcpy(slots[g - 20000], R, NB); continue; }
                if (g >= 20010 && g <= 20019) { wr(x, R, opcode, 1); continue; }
                if (g >= 0x4e84 && g <= 0x4e90) { memcpy(slots[g - 0x4e7a], R, NB); continue; }
                goto unknown;
            }
            if (g == 0x4f4c) { wr(x, R, opcode, 1); continue; }
            if (g > 0x5014) {
                if (g < 0x5017) { wr(x, R, opcode, 1); continue; }
                if (g == 0x50dd) { int k = c->p5; c->p5 = 0; b2s(c, R, opcode, 1); c->p5 = k; continue; }
                if (g == 0x5140) { memcpy(s20800, R, NB); continue; }
            }
            goto unknown;
        }
        if (g == 10000) { wr(x, R, opcode, 1); continue; }
        switch (g) {
        case 900: case 901: case 902: case 903: case 904: case 905: case 906: case 911: case 912: case 915: case 916:
        case 921: case 922: case 923: case 924: case 925: case 926: case 950: case 999: case 1003: case 1004: case 1005:
        case 1006: case 1007: case 1008: case 1010: case 1013: case 1015: case 1017: case 1026: case 1027: case 1028:
        case 1029: case 1031: case 1032: case 1040: case 1041: case 1042: case 1043: case 1051: case 1052: case 1053:
        case 1055: case 1056:
            wr(x, R, opcode, 1); continue;
        case 1000: case 1001: case 1002: {
            int16_t g0 = (int16_t)g;
            if (in1040) { W16(R, 0, 1000); wr(x, R, opcode, 1); continue; }
            if (g != 1000 || x->fs.p6 != I32(s1016, 0x60)) {      /* flush the buffered 20000-series records */
                if (!lathe) { for (int k = 0; k < 22; k++) if (I16(slots[k], 0) > 19999) wr(x, slots[k], opcode, 1); }
                else {
                    for (int k = 0; k < 3; k++) if (I16(slots[k], 0) > 19999) wr(x, slots[k], opcode, 1);
                    for (int k = 10; k < 23; k++) if (I16(slots[k], 0) > 0x4e83) wr(x, slots[k], opcode, 1);
                }
            }
            if (I16(s20800, 0) == 0x5140) wr(x, s20800, opcode, 1);   /* not wire */
            memset(slots, 0, sizeof slots);
            comments(x, o, &on, opcode);
            /* product_number() 0 / 5 / 6 restores the original gcode: it is unchanged here (not in a 1040 block, not wire) */
            W16(R, 0, g0);
            wr(x, R, opcode, 1);
            x->fs.p3 = 1; x->fs.p4 = I32(R, 0x24); x->fs.p5 = I32(R, lathe ? 0x28 : 0x2c);
            x->fs.p6 = I32(s1016, 0x60); x->fs.p7 = I32(s1016, 0x58);
            continue;
        }
        case 1011:
            m42 = 0;
            comments(x, o, &on, opcode);
            /* CMiscInit::InitToPostText (post text values, no NCI output) */
            wr(x, R, opcode, 1); continue;
        case 1012: wr(x, R, opcode, 1); continue;
        case 1014: memcpy(s1014, R, NB); wr(x, R, opcode, 1); continue;
        case 1016: memcpy(s1016, R, NB); wr(x, s1016, opcode, 1); continue;   /* written from the saved copy local_6518 (its 1016 fix-ups stay there) */
        case 1018: case 1019: warnf(c, "op %d: subprogram record %d (FUN_180085a80 decides whether it is written)", o->id, g); continue;
        case 1020:
            wr(x, R, opcode, 1);
            /* DAT_1812ab00b ("write the op's parameters", FUN_180083300 / bnci_op_prm): ours = off */
            continue;
        case 1025:
            if (!m42) {
                int sel = opcode == 4 ? I32(s1016, 0x64) : I32(R, 4);   /* local_5738 != 4 ? rec +4 : 1016 +0x64 */
                const Op *o2 = op_by_id(c->f, sel);
                if (o2) {
                    int32_t acid = 0; int ok = c->env.have_axis_comb;
                    if (ok) acid = c->env.axis_comb_id;
                    else { const uint8_t *ac = axcomb_of(c, o2); if (ac) { ok = 1; acid = I32(ac, 0x34); } }
                    if (ok) {
                        uint8_t r9[NB + 1]; memset(r9, 0, sizeof r9); W16(r9, 0, 0x3b6); W32(r9, 4, I32(R, 4)); W32(r9, 0x24, acid);
                        wr(x, r9, opcode, 1);
                    } else warnf(c, "950 of op %d: axis combination %d not found (AxisCombinationFromID's index-0 fallback); record not written", o->id,
                                 opf(o2, 0x11bc, 4) ? I32(opf(o2, 0x11bc, 4), 0) : -1);
                    m42 = 1;
                }
            }
            /* op_is_lathe_misc_no_tool(op): op comment again - lathe ops only (not decoded) */
            wr(x, R, opcode, 1); continue;
        default: goto unknown;
        }
    unknown:
        x->c->st.unknown++;
        if (c->warn) fprintf(c->warn, "nci_ascii: bnci_convert_section(): unknown gcode (%d) in op %d - conversion of the op stops\n", g, o->id);
        return 0;
    }
}

/* FUN_1800a0d70 for a mill op (objdump; not lathe, not a mill transform): world_to_view(op +0x870 - op +0x5c0, view op
   +0x572), then make_reals_0 (@18013ac00: components with 0 < |v| < 1e-9 or |v| > 1e64 become 0) */
static int home_pos(MpNciConv *c, const Op *o, double h[3]) {
    if (c->env.home_set) { memcpy(h, c->env.home, sizeof c->env.home); return 1; }
    const uint8_t *a = opf(o, 0x870, 24), *b = opf(o, 0x5c0, 24), *v = opf(o, 0x572, 2);
    if (!a || !b || !v) { warnf(c, "1003 home of op %d: op fields outside the stored record (0 0 0 written)", o->id); h[0] = h[1] = h[2] = 0; return 0; }
    if (op_is_xform(o->opcode)) { warnf(c, "1003 home of transform op %d (FUN_1800a0d70's transform branch; 0 0 0 written)", o->id); h[0] = h[1] = h[2] = 0; return 0; }
    if (op_is_lathe(o->opcode)) { warnf(c, "1003 home of lathe op %d (CLNciFmt::get_lhome_pos_nci; 0 0 0 written)", o->id); h[0] = h[1] = h[2] = 0; return 0; }
    double p[3] = { D(a, 0) - D(b, 0), D(a, 8) - D(b, 8), D(a, 16) - D(b, 16) }, m[9];
    int view = I16(v, 0);
    if (view < 2) memcpy(h, p, sizeof p);                         /* world_to_view: views < 2 = identity (formats/mcam.md) */
    else if (mcam_std_view(view, m)) for (int k = 0; k < 3; k++) h[k] = m[3 * k] * p[0] + m[3 * k + 1] * p[1] + m[3 * k + 2] * p[2];
    else { warnf(c, "1003 home of op %d: view %d is not a reserved view (0 0 0 written)", o->id, view); h[0] = h[1] = h[2] = 0; return 0; }
    for (int k = 0; k < 3; k++) { double q = fabs(h[k]); if ((q < 1e-9 && q > 0) || q > 1e64) h[k] = 0; }
    return 1;
}


static void header_1050(Ctx *x) {                                 /* FUN_1800a1c00 (objdump), run when the NC file opens with "w" */
    MpNciConv *c = x->c; char date[96], up[1024];
    if (c->env.have_date) snprintf(date, sizeof date, "%d %d %d %d %d %d", c->env.day, c->env.mon, c->env.year, c->env.hour, c->env.min, c->env.sec);
    else snprintf(date, sizeof date, "0 0 0 0 0 0");              /* @0x180cffcf8 */
    memcpy(up, c->env.geo_filename, sizeof up); up[sizeof up - 1] = 0;
    for (char *p = up; *p; p++) *p = (char)toupper((unsigned char)*p);   /* _mbsupr_s */
    sink_printf(&c->out, "%d\n", 0x41a);
    sink_printf(&c->out, "%d ", c->env.vers_no);
    sink_printf(&c->out, "%d ", c->env.m_vers_no);
    sink_printf(&c->out, "%s ", date);
    sink_printf(&c->out, "%s\n", up);
    c->st.lines++;
    /* DAT_1812ab00d -> FUN_18013ad30(file): NOT DECODED (ours: off) */
}

int mp_nci_write_part_env(const McamFile *f, const MpNciEnv *env, const int *op_index, int nops, FILE *out,
                          MpNciStats *stats, FILE *warn, char *err, size_t errn) {
    MpNciConv C; conv_init(&C, f, env); C.out.fp = out; C.warn = warn;
    C.out.crlf = C.env.crlf;
    Ctx X = { .c = &C };
    uint8_t *data = NULL;
    if (f->path[0] && f->nci_off) {                               /* ours: the stored NCI is read again from the part file */
        struct stat st;
        if (stat(f->path, &st) != 0 || (long long)st.st_size != f->file_size || (long long)st.st_mtime != f->file_mtime) {
            snprintf(err, errn, "the part file changed on disk since it was loaded"); return 0; }
        FILE *in = fopen(f->path, "rb");
        if (!in) { snprintf(err, errn, "cannot open %s", f->path); return 0; }
        data = malloc((size_t)st.st_size ? (size_t)st.st_size : 1);
        X.len = fread(data, 1, (size_t)st.st_size, in); fclose(in); X.d = data;
    }
    if (data) part_index(&C, data, X.len);
    char *sel = calloc((size_t)f->nops + 1, 1);
    for (int i = 0; i < nops; i++) if (op_index[i] >= 0 && op_index[i] < f->nops) sel[op_index[i]] = 1;
    assign_tool_changes(&X, sel);
    renumber_work_offsets(&X, sel);
    const Op *prev = NULL; int open = 0;
    C.st.skipped_op = -1;
#define SKIP_OP(i) do { C.st.skipped++; if (C.st.skipped_op < 0) C.st.skipped_op = (i); } while (0)   /* ours (MpNciStats.skipped) */
    for (int i = 0; i < f->nops; i++) {                           /* Operations Manager order (SortOperations(6)) */
        const Op *o = &f->ops[i];
        if (!sel[i]) continue;
        if (!o->raw) { warnf(&C, "op index %d: an op made here (no OP_ID record)", i); SKIP_OP(i); continue; }
        const uint8_t *dis = opf(o, 0xe8, 1);                     /* op +0xe8 (plVar27[0x3a], long = 4 bytes) must be 0 */
        if (dis && *dis) continue;
        char name[0x105]; snprintf(name, sizeof name, "%s", op_ncname(o));
        if (!open || (prev && strcasecmp(name, op_ncname(prev)) != 0)) {
            /* (re)open the NC file. FUN_1800a1800 (objdump) picks the mode: "wt" (@0x180cf822c) when this is the first
               selected op of that file name (counting ops that are not op_is_no_nci and have op +0xe8 = 0), else "at"
               (@0x180cffe48); its overwrite prompt is ours = yes. "w" mode writes the 1050 header (FUN_1800a1c00). */
            int first = 1;
            for (int j = 0; j < i; j++) {
                const Op *q = &f->ops[j]; const uint8_t *qe = opf(q, 0xe8, 1);
                if (sel[j] && q->raw && !op_is_no_nci(q->opcode) && qe && !*qe && strcasecmp(op_ncname(q), name) == 0) { first = 0; break; }
            }
            if (open) warnf(&C, "op %d switches to NC file \"%s\" (mode %s): written to the same stream", o->id, name, first ? "wt" : "at");
            memset(&X.fs, 0, sizeof X.fs); X.fs.p4 = X.fs.p5 = X.fs.p6 = X.fs.p7 = INT32_MIN;   /* 0x80000000 */
            if (first) { header_1050(&X); C.st.files++; }
            open = 1;
        }
        const uint8_t *se = opf(o, 0x80, 8);                      /* -1 < op +0x80 < op +0x84 */
        if (!se || !(I32(se, 0) > -1 && I32(se, 0) < I32(se, 4))) { warnf(&C, "op %d: no NCI positions", o->id); SKIP_OP(i); continue; }
        prev = o;                                                 /* plVar29 = plVar27 only for converted ops */
        /* FUN_1800a0c80(op) resets p4 (the previous tool number) for non-lathe products: it only matters for wire */
        if (convert_section(&X, o)) C.st.ops++; else SKIP_OP(i);
        int later = 0;                                            /* a later selected op writing to the same file? */
        for (int j = i + 1; j < f->nops && !later; j++) if (sel[j] && f->ops[j].raw && strcasecmp(op_ncname(&f->ops[j]), name) == 0) later = 1;
        if (later) continue;
        for (int j = f->nops - 1; j >= 0; j--) {                  /* 1003: the last selected op of the file with a tool */
            const Op *q = &f->ops[j];
            if (!sel[j] || !q->raw || strcasecmp(op_ncname(q), name) != 0) continue;
            if (!(q->opcode == 4 || !op_is_notool(q->opcode))) continue;
            uint8_t r[NB + 1]; memset(r, 0, sizeof r); W16(r, 0, 0x3eb);
            W32(r, 4, q->id); W32(r, 8, I32(q->raw, 0x34)); r[0xc] = (uint8_t)rec_flag_for_op(q->opcode);
            double h[3]; home_pos(&C, q, h); WD(r, 0x18, h[0]); WD(r, 0x20, h[1]); WD(r, 0x28, h[2]);
            if (op_is_wire(q->opcode)) warnf(&C, "1003 of wire op %d (the extra record before it)", q->id);
            int keep = C.p5; C.p5 = 1; b2s(&C, r, q->opcode, 1); C.p5 = keep;
            break;
        }
    }
#undef SKIP_OP
    free(sel); free(data); free(C.pr); free(X.tc); free(X.wo);
    if (stats) *stats = C.st;
    return 1;
}

int mp_nci_write_part(const McamFile *f, const int *op_index, int nops, FILE *out, char *err, size_t errn) {
    return mp_nci_write_part_env(f, NULL, op_index, nops, out, NULL, stderr, err, errn);
}
