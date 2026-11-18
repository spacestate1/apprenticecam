/* cam_postvals.c - op dialog batch 11: the post-side values of an operation (cam_postvals.h). Sources (Ghidra DumpDecomp on
 * ~/mastercam-re/ghidra_core / ghidra_mdef, 2026-10-24; copies in ../decomp/b11/): mccore CoolantPageBase::SetDialogData
 * @180234fd0 / UpdateCannedText @180236f10 / FUN_180237920 / FUN_1802379c0 / FUN_180237c20, mcmill MapCoolantToCannedText
 * @180048090 (decomp/mcmill_all.c l.50209), CannedTextPageBase::SplitCannedSelect @180167800, bnci_wr_sof @180096f30 ->
 * bnci_wr_1011 @18008e240 / bnci_wr_1012 @18008e3d0 / bnci_wr_1025 @18008f9a0 / bnci_wr_1002 @18008d7c0, mcmachinedef
 * CCNCMachine::IsCoolantOptionEnabled @1800761e0 / CoolantOnCmd @180072960 / CoolantOffCmd @1800726c0 / UseCoolantFromPost
 * @18007bba0, mcpost CPostText::GetMiscIntValueFromIndex @18001c700 (decomp/mcpost_parse.c l.2754). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cam_postvals.h"

static int16_t g16(const uint8_t *r, int o) { int16_t v; memcpy(&v, r + o, 2); return v; }
static int32_t g32(const uint8_t *r, int o) { int32_t v; memcpy(&v, r + o, 4); return v; }

/* ------------------------------------------------------------------------------------------------ machine coolant */
/* DAT_1801fb500 = `""` (mcmachinedef .rdata, MemDump 2026-10-24): a text equal to it counts as none */
static const McamFile *(*pv_fb)(void);
void pv_set_machine_fallback(const McamFile *(*fn)(void)) { pv_fb = fn; }
const McamFile *pv_machine(const McamFile *f) {
    if (f && f->mach_cool_ok) return f;
    const McamFile *m = pv_fb ? pv_fb() : NULL;
    return m && m->mach_cool_ok ? m : f;
}
static int cool_set(const char *s) { return strcmp(s, "\"\"") != 0; }
int pv_cool_frompost(const McamFile *f) { return f && f->mach_cool_ok && f->mach_cool_frompost; }
/* CoolantOnCmd(k): k < 10, base text (k x 0x50) != `""` and on text ((k + 0x14) x 0x28) != `""` -> "%s %s" of CoolantBaseTxt and
   CoolantOnTxt (an empty on text -> DfltCoolantOnCmdID's resource string: NOT DECODED, taken as none) */
int pv_cool_on_cmd(const McamFile *f, int k, char *out, size_t n) {
    if (!f || !f->mach_cool_ok || k < 0 || k > 9 || !cool_set(f->mach_cool_base[k]) || !cool_set(f->mach_cool_on[k]) || !f->mach_cool_on[k][0]) return 0;
    if (out) snprintf(out, n, "%s %s", f->mach_cool_base[k], f->mach_cool_on[k]);
    return 1;
}
int pv_cool_off_cmd(const McamFile *f, int k, char *out, size_t n) {   /* off text at (k + 0x1e) x 0x28 */
    if (!f || !f->mach_cool_ok || k < 0 || k > 9 || !cool_set(f->mach_cool_base[k]) || !cool_set(f->mach_cool_off[k]) || !f->mach_cool_off[k][0]) return 0;
    if (out) snprintf(out, n, "%s %s", f->mach_cool_base[k], f->mach_cool_off[k]);
    return 1;
}
/* IsCoolantOptionEnabled(k): (!UseCoolantFromPost || k < 3) && (CoolantOnCmd || CoolantOffCmd), and the button text not empty */
int pv_cool_enabled(const McamFile *f, int k) {
    if (!f || !f->mach_cool_ok || !f->mach_cool_btn[0]) return 0;
    if (f->mach_cool_frompost && k >= 3) return 0;
    return pv_cool_on_cmd(f, k, NULL, 0) || pv_cool_off_cmd(f, k, NULL, 0);
}

/* ------------------------------------------------------------------------------------------------ coolant state */
void pv_cool_decode(const McamFile *f, const int16_t can[20], int16_t oldw, PvCool *c) {
    int fp = pv_cool_frompost(f);
    for (int k = 0; k < 10; k++) { c->state[k] = 2; c->pos[k] = 0; }          /* FUN_180237920 */
    if (fp) {                                                                  /* FUN_180237c20: the first word only */
        if (oldw == 0x10) c->state[0] = 0;
        if (oldw == 0x20) c->state[1] = 0;
        if (oldw == 0x40) c->state[2] = 0;
    } else
        for (int i = 0; i < 20 && can[i]; i++) {                               /* FUN_1802379c0: up to the first 0 entry */
            int v = can[i], r = v % 1000, a = v < 0 ? -v : v;                  /* C remainder: -3101 % 1000 = -101 */
            if (r > -101 || r < -120) continue;
            int k = (-r - 101) / 2;                                            /* -101 / -102 Flood, -103 / -104 Mist ... -119 / -120 option 9 */
            c->state[k] = (-r - 101) % 2;                                      /* odd code (-101) = On (0), even (-102) = Off (1) */
            c->pos[k] = (int16_t)(a / 1000 - 3);                               /* 3xxx before, 4xxx with, 5xxx after */
        }
    for (int k = 0; k < 10; k++) {                                             /* SetDialogData, per option */
        if (!fp && !pv_cool_enabled(f, k)) c->state[k] = 2;
        if (fp && c->state[k] == 2) c->state[k] = 1;
        if (c->pos[k] < 0 || c->pos[k] > 2) c->pos[k] = 0;
    }
}
/* MapCoolantToCannedText (mcmill @180048090): the coolant entries (v % 1000 in -120..-101) of OP go, the others keep their order,
   then the new coolant entries of NEWC are appended while there is room */
static void map_coolant(int16_t op[20], const int16_t newc[20]) {
    int16_t old[20]; memcpy(old, op, sizeof old); memset(op, 0, sizeof old);
    int n = 0;
    for (int i = 0; i < 20; i++) {
        int r = old[i] % 1000;
        if ((uint16_t)(r + 0x78) < 0x14) continue;
        if (old[i]) op[n++] = old[i];
    }
    for (int i = 0; i < 20 && n < 20; i++) { int r = newc[i] % 1000; if ((uint16_t)(r + 0x78) < 0x14) op[n++] = newc[i]; }
}
void pv_cool_encode(const McamFile *f, const PvCool *c, int16_t can[20], int16_t *oldw, uint8_t *canon) {
    int fp = pv_cool_frompost(f); int16_t loc[20]; int n = 0; memset(loc, 0, sizeof loc);
    for (int k = 0; k < 10; k++) {                                             /* UpdateCannedText: (pos + 3) x -1000 - |code| */
        if (!pv_cool_enabled(f, k) || f == NULL) continue;
        int code; if (c->state[k] == 0) code = 101 + 2 * k; else if (c->state[k] == 1) code = 102 + 2 * k; else continue;
        loc[n++] = (int16_t)((c->pos[k] + 3) * -1000 - code);
    }
    if (fp) *oldw = c->state[0] == 0 ? 0x10 : c->state[1] == 0 ? 0x20 : c->state[2] == 0 ? 0x40 : 8;
    else map_coolant(can, loc);
    int any = 0; for (int i = 0; i < 20; i++) if (can[i]) any = 1;
    *canon = (uint8_t)any;                                                     /* op +0x2bd8 (dialog) = record 0xb50: memcmp(zeros) != 0 */
}

/* ------------------------------------------------------------------------------------------------ canned text */
int pv_canned_split(const int16_t can[20], int which, int16_t *out) {
    int n = 0;
    for (int i = 0; i < 20; i++) {
        int v = can[i], a = v < 0 ? -v : v, w;
        if (a <= 0) continue;
        if (a < 1000) w = 0; else if (a < 2000) w = 1; else if (a < 3000) w = 2; else if (a < 4000) w = 0; else if (a < 5000) w = 1; else w = 2;
        if (w == which) out[n++] = (int16_t)v;
    }
    return n;
}
void pv_canned_label(const McamFile *f, int16_t v, char *out, size_t n) {
    int a = v < 0 ? -v : v;                                                    /* LoadListBefore: the table index */
    int idx = a < 1000 ? a : a < 2000 ? a - 1000 : a < 3000 ? a - 2000 : a < 4000 ? a - 2000 : a < 5000 ? a - 3000 : a - 4000;
    if (idx >= 1101 && idx <= 1120) {                                          /* X-style coolant [inferred label] */
        int k = (idx - 1101) / 2; char b[160] = "";
        if ((idx - 1101) % 2 == 0 ? pv_cool_on_cmd(f, k, b, sizeof b) : pv_cool_off_cmd(f, k, b, sizeof b)) { snprintf(out, n, "%s", b); return; }
        snprintf(out, n, "coolant %d", idx); return;
    }
    const char *t = pv_canned_text(idx);
    if (t) snprintf(out, n, "%s", t); else snprintf(out, n, "%d", idx);
}

/* ------------------------------------------------------------------------------------------------ post text stand-in */
/* Generic Fanuc 3X Mill.pst l.1789-1864 / MPFAN.pst l.2585-2660 [CTRL_MILL|DEFAULT]: [misc integers] 1-3, [canned text] 1-10 */
static const char *const MISC_TXT[4] = { NULL, "Work Coordinates [0-1=G92, 2=G54's]", "Absolute/Incremental, top level [0=ABS, 1=INC]",
                                         "Reference Return [0=G28, 1=G30]" };
const char *pv_misc_text(int idx, int *has) {
    if (has) *has = 1;                       /* no "" entry on the page: SetDlgControlTextFromIndex returns 1 (enabled) for every index */
    return idx >= 1 && idx <= 3 ? MISC_TXT[idx] : NULL;   /* other entries: the default text (GetTextStructure's resource id) NOT DECODED */
}
double pv_misc_value(int idx) { return idx == 1 ? 2 : 0; }   /* 1. "..."//2; the others have no value -> 0 */
const char *pv_canned_text(int n) {
    static const char *const T[11] = { NULL, "Stop", "Ostop", "Bld on", "bLd off", "M5", "M6", "M7", "M8", "M9", "M10" };
    return n >= 1 && n <= 10 ? T[n] : NULL;
}

int pv_is_post_field(int off, int n) {
    int e = off + n;
    return (off >= PV_COOL_OLD && e <= PV_COOL_OLD + 2) || (off >= PV_CAN_ON && e <= PV_CAN + 40)
        || (off >= PV_MISC_SET && e <= PV_MISC_REAL + 80);
}

/* ------------------------------------------------------------------------------------------------ the stored NCI */
/* bnci_wr_sof writes, per section start, 1011 (+0x18 10 reals), 1012 (+0x18 10 longs), ..., 1017, 1025 (+0x18 20 shorts, zeros
   when op +0xae8 = 0), ..., 1002 (+0x48 = op +0x330). Here: every 1011 / 1012 / 1000-1002 of the op's section and each 1025 that
   directly follows a 1017 (the sof one; 1025s inside the motion are canned text on points, FUN_180058690 in mcmill_ctour.c) */
int pv_nci_rewrite(McamFile *f, int index, int groups, char *msg, size_t msgn) {
    msg[0] = 0;
    if (index < 0 || index >= f->nops) { snprintf(msg, msgn, "no such operation"); return 0; }
    Op *o = &f->ops[index];
    if (!o->raw || o->raw_n < PV_MISC_REAL + 80 || o->raw_n < PV_CAN + 40) { snprintf(msg, msgn, "operation %d is not stored in the part", index + 1); return 0; }
    uint8_t *d = NULL; size_t len = 0, off = 0;
    if (o->nci) { d = malloc(o->nci_n); memcpy(d, o->nci, o->nci_n); len = o->nci_n; }
    else {
        struct stat st;
        if (!f->path[0] || !f->nci_off || stat(f->path, &st) != 0 || (long long)st.st_size != f->file_size || (long long)st.st_mtime != f->file_mtime) {
            snprintf(msg, msgn, "the part file changed on disk since it was opened (or has no NCI) - open it again"); return 0; }
        FILE *in = fopen(f->path, "rb");
        if (in) { fseek(in, 0, SEEK_END); long L = ftell(in); fseek(in, 0, SEEK_SET);
            if (L > 0) { len = (size_t)L; d = malloc(len); if (fread(d, 1, len, in) != len) { free(d); d = NULL; } } fclose(in); }
        if (!d) { snprintf(msg, msgn, "cannot read %s", f->path); return 0; }
        off = f->nci_off;
    }
    size_t s0 = 0, s1 = 0; int in = 0, found = 0;               /* the -2 .. -3 records of the op (nci_reverse.c find_section) */
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        int16_t g = sz >= 2 ? g16(d + off + 2, 0) : 0;
        if (g == -2 && sz >= 0x20) { in = g32(d + off + 2, 0x1c) == o->id; if (in) s0 = off; }
        if (in && g == -3) { s1 = off + 2 + sz; found = 1; break; }
        off += 2 + sz;
    }
    if (!found) { free(d); snprintf(msg, msgn, "operation %d has no NCI in the part", index + 1); return 0; }
    uint8_t *sec = malloc(s1 - s0); memcpy(sec, d + s0, s1 - s0); free(d);
    const uint8_t *R = o->raw; int prev = 0, nrw = 0;
    for (size_t p = 0; p + 2 <= s1 - s0; ) {
        uint16_t sz = (uint16_t)(sec[p] | sec[p + 1] << 8); uint8_t *r = sec + p + 2; int g = sz >= 2 ? g16(r, 0) : 0;
        int need = 0; const uint8_t *src = NULL; int doff = 0;
        if (g == 1011 && (groups & PVG_MISC)) { need = 0x18 + 80; src = R + PV_MISC_REAL; doff = 0x18; }
        if (g == 1012 && (groups & PVG_MISC)) { need = 0x18 + 40; src = R + PV_MISC_INT; doff = 0x18; }
        if (g == 1025 && prev == 1017 && (groups & (PVG_CAN | PVG_COOL))) { need = 0x18 + 40; src = R + PV_CAN; doff = 0x18; }
        if (g >= 1000 && g <= 1002 && (groups & PVG_COOL)) { need = 0x48 + 2; src = R + PV_COOL_OLD; doff = 0x48; }
        if (need) {
            if (sz < need) { free(sec); snprintf(msg, msgn, "operation %d: its stored NCI record %d is too short (%d bytes) for the post values", index + 1, g, sz); return 0; }
            if (g == 1025 && !R[PV_CAN_ON]) memset(r + doff, 0, 40);   /* bnci_wr_1025(op, op[0xae8] = 0, ...): the shorts stay 0 */
            else memcpy(r + doff, src, (size_t)(need - doff));
            nrw++;
        }
        if (g != 0 || sz) prev = g;
        p += 2 + (size_t)sz;
    }
    if (!nrw) { free(sec); snprintf(msg, msgn, "operation %d: no NCI record holds these values", index + 1); return 0; }
    mcam_op_set_nci(f, index, sec, s1 - s0);
    o->postv = 1;
    return 1;
}
