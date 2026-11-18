#include <stdarg.h>
/* cfb_port SCANFILE KIND CHORD SYSTOL CDEV > OUT: the port of FUN_1801cfb60's per-line stages (cam3d_cscallop_cfb.c, formats/surf_classic3.md
 * 12.19) on a scan file of the scanner port (cs_port CS_MERGE=2: port_scn_apx.bin). Doubles are %a (CHORD = msurf_chord_dev, SYSTOL =
 * mccore tol, CDEV = msurf_corner_dev). Prints, per line i of the file's table in FUN_1801cfb60's order (0 .. n - 1, l.310762) whose chain
 * is not -1, the block walk FUN_180183310's segments
 *   "SG i code f0 f1 s0 s1 z0 z1 n0 .. n5 v"   (v = FUN_180182070(DAT_1804e9250, ..), the vertical-wall test of FUN_180182e70)
 * and "LN i ret p8". tools/peloader/mcmsurf_lmm.c CFBU mode prints the same from the DLL's own functions: tools/tests/cfbu_cmp.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "cam3d_cscallop.h"

typedef struct { int line; const CsCfbConst *C; CsEst *L; } Ctx;
static int seg(void *ctx, int code, const unsigned char fl[2], double s[2], double z[2], double n[6])
{
    Ctx *c = ctx;
    int v = cs_seg_vertical(c->C->t9250, c->C->systol, code, s, z, n);
    printf("SG %d %d %d %d %a %a %a %a %a %a %a %a %a %a %d\n", c->line, code, fl[0], fl[1], s[0], s[1], z[0], z[1], n[0], n[1], n[2], n[3], n[4], n[5], v);
    if (c->L) return cs_est_insert(c->L, c->C, code, fl, s, z, n) != 0;   /* FUN_180182e70 (the DLL: DAT_1804e9344 != 0 -> -4) */
    return 0;
}
static int seg_quiet(void *ctx, int code, const unsigned char fl[2], double s[2], double z[2], double n[6])
{
    Ctx *c = ctx;
    return cs_est_insert(c->L, c->C, code, fl, s, z, n) != 0;
}
static void codes(const char *tag, int line, int k, int n, const CsNode *a)
{
    printf("%s %d %d %d", tag, line, k, n);
    for (int i = 0; i < n; i++) printf(" %d", a[i].code);
    printf("\n");
}

/* "EST" / "RL line n hash freehead nfree err": the line list in order, per node its store index (2 bytes) and cs_node_bytes, FNV-1a 64
   (tools/peloader/mcmsurf_lmm.c CFBL mode prints the same from the DLL's store) */
static void est_print(const char *tag, int line, const CsEst *L)
{
    uint64_t h = 1469598103934665603ull; int n = 0;
    for (short k = L->head; k != -1; k = L->nd[k].next) {
        unsigned char nb[0x68], ib[2] = { (unsigned char)k, (unsigned char)((unsigned short)k >> 8) };
        cs_node_bytes(&L->nd[k], nb);
        for (int q = 0; q < 2; q++) { h ^= ib[q]; h *= 1099511628211ull; }
        for (int q = 0; q < 0x68; q++) { h ^= nb[q]; h *= 1099511628211ull; }
        n++;
    }
    printf("%s %d %d %016llx %d %d %d\n", tag, line, n, (unsigned long long)h, L->freeh, L->nfree, L->err);
    if (getenv("CFB_DUMP"))
        for (short k = L->head; k != -1; k = L->nd[k].next) {
            const CsNode *d = &L->nd[k];
            printf("N %s %d %d %d %a %a %a %a %a %d %a %a %a %a %a %d\n", tag, line, k, d->f0, d->s0, d->z0, d->n0[0], d->n0[1], d->n0[2], d->f1, d->s1, d->z1,
                   d->n1[0], d->n1[1], d->n1[2], d->code);
        }
}

static int g_line;
static int *rg_tab, rg_off; static CsBuf rg_out;
static CsFdNode *fd_arr; static short *fd_list; static unsigned char *fd_mark;
static void fd_hash_print(const char *tag, int line, const CsFdNode *arr, int n, const char *pre, int a, int b)
{
    uint64_t h = 1469598103934665603ull;
    for (int j = 0; j < n; j++) { unsigned char nb[0x58]; cs_fd_node_bytes(arr + j, nb); for (int q = 0; q < 0x58; q++) { h ^= nb[q]; h *= 1099511628211ull; } }
    printf("%s %d%s", tag, line, pre);
    if (a != -99999) printf(" %d", a);
    if (b != -99999) printf(" %d", b);
    printf(" %016llx", (unsigned long long)h);
}
static void fd_trace(int stage, const CsEst *L, const CsFdNode *arr, int n, const short *list, int k, int v)
{
    switch (stage) {
    case 0: printf("FP %d %d %d\n", g_line, k, v); est_print("FS", g_line, L); break;
    case 1: fd_hash_print("FA", g_line, arr, n, "", n, k); printf("\n"); break;
    case 2: fd_hash_print("FB", g_line, arr, n, "", -99999, -99999); printf("\n"); break;
    case 3: fd_hash_print("FC", g_line, arr, n, "", k, -99999); for (int j = 0; j < k; j++) printf(" %d", list[j]); printf("\n"); break;
    case 4: printf("FD %d %d %d\n", g_line, k, v); est_print("FL", g_line, L); break;
    }
}

static unsigned char *slurp(const char *fn, size_t *len)
{
    FILE *f = fopen(fn, "rb"); if (!f) { perror(fn); return NULL; }
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)l + 1);
    if (fread(b, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(b); return NULL; }
    fclose(f); *len = (size_t)l; return b;
}
static void spit(const char *fn, const char *suf, const CsBuf *b, size_t n)
{
    char p[4200]; snprintf(p, sizeof p, "%s%s", fn, suf);
    FILE *w = fopen(p, "wb"); if (w) { fwrite(b->p, 1, n, w); fclose(w); }
}
static int ri4(const unsigned char *p) { int v; memcpy(&v, p, 4); return v; }
static double rd8(const unsigned char *p) { double v; memcpy(&v, p, 8); return v; }

/* One FUN_1801cfb60 run of kind 7 (param_2 = 1, param_3 = 0) or 6 (0, 1) into the recut file `out` (formats/surf_classic3.md 12.21):
   FUN_18025d6d0(local_3646 == 0, ..) (l.310658: kind 7 creates the file with the header FUN_180229640(-0x38, -1, -1) and DAT_1804e92ec =
   0x38; kind 6 reads the header and appends: SetPosition(0, 2), DAT_1804e92ec = the end, l.310665-310672), the table DAT_1804e92f8 = -1
   (l.310650-310656), the per-line arm (as main below, l.311101-311254) with the line position DAT_1804e92b8 = DAT_1804e9230 (l.310762) +=
   DAT_1804e9228 after every line (LAB_1801d2ea1, l.311253), FUN_1801c9350 (cs_rgn_vcs_copy), the trailer at DAT_1804e92ec = the position
   (asm 1801d33f9..1801d340d), then kind 7: FUN_180229640(-0x38, 0x11, 0x20) (l.310977-310989, case 6 / 7), +0x18 = the trailer (mask 0x2b0,
   asm 1801d3813..1801d382b), +0 = 0 (l.311013), FUN_180268b30 (the header written at 0, l.405081); kind 6: the header as read, +0xc = the
   trailer (asm 1801d3833..1801d3839), +0 = -0x38 (asm 1801d387d), FUN_18022c850 (cs_scan_appendix; its inputs read back from the port's
   own appendix of the scan file, apx = port_scn_apx.bin of kind 6: the same part name, tmpmm2, tolerances, op and tool).
   The frame rows, D, DAT_1804e9230 / 9228 and n are taken from the scan file's trailer (the scanner writes the same values of the same
   arguments: rows l.326195-326201 / 9150 / 9168 / 9180 l.309983-309991, -first / -step l.309994-309995). */
static int rgn_kind(int kind, const char *scanf_, const char *vcsf, double chord, double systol, double cdev, double gap, double stock,
                    double r, CsBuf *out, const char *pre)
{
    size_t len, vlen; unsigned char *b = slurp(scanf_, &len), *v = slurp(vcsf, &vlen);
    if (!b || !v) return 2;
    CsCfbConst C; cs_cfb_consts(kind, chord, systol, cdev, &C);
    int *t = NULL, n = cs_cfb_table(b, len, &t);
    if (n < 0) { printf("RGN %d bad scan file\n", kind); return 2; }
    int to = ri4(b + 0xc);
    double F[9]; memcpy(F, b + to, 72);
    double D = rd8(b + to + 0x68), t9230 = rd8(b + to + 0x78), t9228 = rd8(b + to + 0x80);
    /* FUN_1801868a0(1, kind == 7) (l.262043): the wall file's table into DAT_1804e9798 by FUN_1801e7c20 (int n, n x 8 bytes; n must be
       DAT_1804e9144), DAT_1804e97a4 = the largest count */
    if (vlen < 4 || ri4(v) != n || vlen < 4 + 8 * (size_t)n) { printf("RGN %d bad wall file\n", kind); return 2; }
    unsigned char *t98 = malloc(8 * (size_t)n + 8); memcpy(t98, v + 4, 8 * (size_t)n);
    short a4 = 0;
    for (int i = 0; i < n; i++) { short c; memcpy(&c, t98 + 8 * i + 4, 2); if (a4 < c) a4 = c; }
    int off;
    if (kind == 7) {
        unsigned char h[0x38]; memset(h, 0, sizeof h); cs_scan_header(h, -0x38, -1, -1);
        out->n = 0; memcpy(out->p, h, 0x38); out->n = 0x38; off = 0x38;
    } else off = (int)out->n;
    int *tab = malloc(sizeof(int) * (size_t)(n + 1)); for (int q = 0; q < n; q++) tab[q] = -1;
    CsEst L; if (cs_est_init_cfb(&L, 0x7fff)) return 2;
    CsNode *arr = calloc(0x8000, sizeof(CsNode)), *pool = calloc(501, sizeof(CsNode));
    CsFdNode *fa = calloc(2 * (0x7fff + 10), sizeof(CsFdNode)); short *fl = calloc(0x7fff + 10, sizeof(short)); unsigned char *fm = calloc(2 * (0x7fff + 10), 1);
    int cap = 500; double pos = t9230; int nb = 0;
    for (int i = 0; i < n; i++, pos = pos + t9228) {
        if (t[i] == -1) continue;
        Ctx c = { i, &C, &L }; double p8 = 0;
        int rr = cs_cfb_walk(b, len, t[i], seg_quiet, &c, &p8, &cap);
        if (rr) { printf("RGN %d line %d walk %d\n", kind, i, rr); return 3; }
        if (L.head != -1) {
            int cc = 0;
            for (short k = L.head; k != -1 && cc <= 0x7fff; k = L.nd[k].next) cc++;
            if (cc > 0x7fff) { printf("RGN %d line %d CYCLE: NOT PORTED (the DLL's FUN_1801974e0 never returns)\n", kind, i); return 3; }
            cs_cfb_clean(&L, &C);
            /* FUN_1801b0c80 (the line's walls into DAT_1804e97a8, cs_wall_read) and FUN_180184cc0 (cs_cfb_to_array_v) */
            double lp = 0; int nv = cs_wall_read(v, vlen, t98, i, pool, &lp);
            if (nv < 0) { printf("NOT PORTED: kind %d line %d: more than 500 wall records\n", kind, i); return 3; }
            int na = cs_cfb_to_array_v(&L, &C, 1, nv, pool, 0x7fff, arr);
            if (na < 0) { printf("RGN %d line %d to_array %d\n", kind, i, na); return 3; }
            cs_est_free_line(&L);
            cs_conn_codes(na, arr);
            cs_join_runs(0, gap, na, arr);
            cs_cfb_from_array(&L, &C, na, arr);
            if (L.err) { printf("RGN %d line %d estore %d\n", kind, i, L.err); return 3; }
            /* the second wall pass (asm 1801d2da6..1801d2e32) needs local_3157 != 0 and the byte r15b read from it after the first pass
               (asm 1801d2ccb) == 0: never on this path */
            CsFdResult R; cs_fd_endflags(&L, &C, pos, 0x2b, 0.0, fa, fl, fm, &R);
            cs_rgn_block(&L, &C, i, pos, &off, tab, out);
            if (off != (int)out->n) { printf("RGN %d line %d offset %d != %zu\n", kind, i, off, out->n); return 3; }
            nb++;
        }
        cs_est_free_line(&L);
    }
    spit(pre, kind == 7 ? ".pre7v" : ".pre6v", out, out->n);
    size_t vs = out->n;
    int e = cs_rgn_vcs_copy(out, v, vlen, n, t98, &a4);
    if (e) { printf("RGN %d vcs copy %d: NOT PORTED\n", kind, e); return 3; }
    int tr = (int)out->n;
    CsRgnTool T; cs_rgn_tool(stock, r, D, C.t9248, &T);
    cs_rgn_trailer(out, F, &C, &T, D, t9230, t9228, 0, n, tab, a4, t98);
    printf("RGN %d blocks %d vcs %zu..%d trailer %d end %zu a4 %d 9148 %a 9288 %a %a %d %d\n", kind, nb, vs, tr, tr, out->n, a4, T.d9148, T.b9288,
           T.a9290, T.c9298, T.c9299);
    printf("RGNV %d ret 0 a4 %d 9790 %d\n", kind, a4, tr);
    {   /* "RGNX": the record as written, in tools/peloader/mcmsurf_lmm.c RGNR's form; the last field = FUN_1801b5fb0's value on it (the
           largest point count of a line's blocks + 10, FUN_1801b4e80) */
        int mx = 0; uint64_t hh = 1469598103934665603ull;
        for (int i = 0; i < n; i++) {
            if (tab[i] != -1) { short c; memcpy(&c, out->p + tab[i] + 2, 2); if (mx < c) mx = c; }
            for (int k = 0; k < 4; k++) { hh ^= (uint8_t)(tab[i] >> (8 * k)); hh *= 1099511628211ull; }
        }
        printf("RGNX %d 0", kind);
        for (int i = 0; i < 9; i++) printf(" %a", F[i]);
        printf(" %a %a %a %a %a %a %d %d 0 0 0 0 0 0 %a %a %a %a 0 %d %016llx -1 %d %d\n", C.t9238, C.t9240 / 5.0, C.t9248, C.t9250, T.b9288, T.a9290,
               T.c9298, T.c9299, D, T.d9148, t9230, t9228, n, (unsigned long long)hh, a4, mx + 10);
    }
    printf("RGNTAB %d", kind); for (int i = 0; i < n; i++) printf(" %d", tab[i]); printf("\n");
    printf("RGNV98 %d", kind); for (int i = 0; i < n; i++) printf(" %d:%d", ri4(t98 + 8 * i), (int)(short)(t98[8 * i + 4] | t98[8 * i + 5] << 8)); printf("\n");
    unsigned char h[0x38]; memcpy(h, out->p, 0x38);
    if (kind == 7) { cs_scan_header(h, -0x38, 0x11, 0x20); memcpy(h + 0x18, &tr, 4); memset(h, 0, 4); }
    else { memcpy(h + 0xc, &tr, 4); int m = -0x38; memcpy(h, &m, 4); }
    memcpy(out->p, h, 0x38);
    free(b); free(v); free(t); free(t98); free(tab); free(arr); free(pool); free(fa); free(fl); free(fm); free(L.nd);
    return 0;
}
/* FUN_18022c850's inputs read back from the port's own appendix of a scan file (cs_scan_appendix's layout, header +0x10 / +0x14 / +0x24):
   the same part name, tmpmm2 count / head, tolerances, entries, op and tool (FUN_18022c850 takes them from the same globals each time) */
typedef struct { unsigned char *a; char name[4096]; int cnt2, head2, ne, opid, e30; double tols[9], it[13]; const unsigned char *ent, *op, *tl; } Apx;
static int apx_load(const char *fn, Apx *A)
{
    size_t al; unsigned char *a = slurp(fn, &al); if (!a) return 2;
    A->a = a;
    int l3 = ri4(a + 0x10), l4 = ri4(a + 0x14), l5 = ri4(a + 0x24);
    const unsigned char *p = a + l3; const unsigned char *nl = memchr(p, '\n', (size_t)(a + al - p));
    if (!nl) return 2;
    size_t nn = (size_t)(nl - p); if (nn >= sizeof A->name) return 2; memcpy(A->name, p, nn); A->name[nn] = 0;
    p = nl + 1;
    A->cnt2 = ri4(p); A->head2 = ri4(p + 4); memcpy(A->tols, p + 8, 72); A->ent = p + 8 + 72;
    A->ne = (int)((a + l4 - A->ent) / 0x20);
    const unsigned char *q = a + l4;
    A->opid = ri4(q + 4); A->e30 = ri4(q + 8);
    A->op = q + 16 + 8; A->tl = A->op + 0x28d0 + 8;
    memcpy(A->it, a + l5, 13 * 8);
    return 0;
}
static void apx_append(CsBuf *out, const Apx *A)
{
    cs_scan_appendix(out, A->name, A->cnt2, A->head2, A->ne, A->ent, A->tols, A->opid, A->e30, A->op, A->tl, A->it);
}
/* cfb_port --rgn OUT SCAN7 VCS7 SCAN6 VCS6 CHORD SYSTOL CDEV GAP STOCK R APX6: the port's whole temp.rgn of op 43 (FUN_180133f10 mode 6 ->
   FUN_1801cfb60 kind 7, then mode 5 -> kind 6). Writes OUT (the whole file), OUT.k7 (as kind 7 leaves it), OUT.pre7v / OUT.pre6v (before
   each FUN_1801c9350), OUT.noapx (before FUN_18022c850). Doubles %a. */
static int rgn_main(int argc, char **argv)
{
    if (argc < 14) { fprintf(stderr, "usage: cfb_port --rgn OUT SCAN7 VCS7 SCAN6 VCS6 CHORD SYSTOL CDEV GAP STOCK R APX6\n"); return 1; }
    const char *o = argv[2];
    double ch = strtod(argv[7], NULL), st = strtod(argv[8], NULL), cd = strtod(argv[9], NULL), gp = strtod(argv[10], NULL);
    double stock = strtod(argv[11], NULL), r = strtod(argv[12], NULL);
    CsBuf out = { 0 }; out.cap = 1 << 20; out.p = malloc(out.cap);
    int e = rgn_kind(7, argv[3], argv[4], ch, st, cd, gp, stock, r, &out, o);
    if (e) return e;
    spit(o, ".k7", &out, out.n);
    e = rgn_kind(6, argv[5], argv[6], ch, st, cd, gp, stock, r, &out, o);
    if (e) return e;
    spit(o, ".noapx", &out, out.n);
    Apx A; if (apx_load(argv[13], &A)) return 2;
    apx_append(&out, &A);
    spit(o, "", &out, out.n);
    printf("RGNFILE %zu header", out.n); for (int i = 0; i < 0x38; i += 4) printf(" %d", ri4(out.p + i)); printf("\n");
    printf("APX %zu TOLS", out.n); for (int i = 0; i < 9; i++) printf(" %a", A.tols[i]); printf("\n");
    free(A.a); free(out.p);
    return 0;
}

/* ---- stage D1 (formats/surf_classic3.md 12.25): FUN_18018e630 twice, temp.rgn -> tmpemesh.tmp ---- */
static void em_list(CsEmTrace *T, const char *tag, int line, const CsEst *L) { (void)T; est_print(tag, line, L); }
static void em_arr(CsEmTrace *T, const char *tag, int line, const CsNode *a, int n)
{
    (void)T;
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; i++) { unsigned char nb[0x68]; cs_node_bytes(&a[i], nb); for (int q = 0; q < 0x68; q++) { h ^= nb[q]; h *= 1099511628211ull; } }
    printf("%s %d %d %016llx\n", tag, line, n, (unsigned long long)h);
}
static void em_ext(CsEmTrace *T, int line, int kind, int ret, int cnt, const double o[3])
{
    (void)T; printf("EX %d %d %d %d %a %a %a\n", line, kind, ret, cnt, o[0], o[1], o[2]);
}
/* FUN_1801b5fb0 (asm 1801b4e80..: FUN_1801b4e80 with the reader FUN_1801a6e70): per line the sum of its blocks' counts along the chain, the
   largest + 10 */
static int chain_max(const unsigned char *f, size_t len, const int *tab, int n)
{
    short mx = 0;
    for (int i = 0; i < n; i++) {
        short sum = 0; long o = tab[i];
        while (o != -1) {
            if (o < 0 || (size_t)o + 0x18 > len) break;
            short c; memcpy(&c, f + o + 2, 2);
            if (c < 1) break;
            sum = (short)(sum + c);
            o = ri4(f + o + 4);
        }
        if (mx < sum) mx = sum;
    }
    return mx + 10;
}
/* cfb_port --emesh OUT RGN APX6 CHORD SYSTOL CDEV: the port's tmpemesh.tmp from the port's temp.rgn (cfb_port --rgn) as FUN_1801d3ca0 makes
   it (l.311742-311762): DAT_1804e9238 / 9240 / 9248 / 9250 from the kind-6 record (FUN_180186bb0; 9258 as FUN_1801cfb60's kind-6 run left
   it), DAT_1804d7038 (cs_em_tei: the op from APX6's appendix, no swap: DAT_1805039d4 = 0 with the trailer's containment -1, l.262197),
   DAT_1804e9320 (cs_em_pool), then FUN_18018e630(.., 1, 1, 0) (create: FUN_18025d6d0(0): the header FUN_180229640(-0x38, -1, -1) over the
   caller's block (asm 18018e6e9..18018e715: +0xc..+0x2f -1, +0x28 0; +0x31..+0x37 the DLL's stack, zero here), FUN_180229640(-0x38, hdr +8,
   hdr +4) (asm 18018e93f..18018e950), the lines, FUN_1801b4070 -> +0x18, +0 = 0, FUN_180268b30) and FUN_18018e630(.., 0, 0, 1) (FUN_18025d6d0(1):
   the header read back, the end; the lines; +0xc; +0 = -0x38 (asm 18018efd8); FUN_18022c850). Writes OUT.b (after B), OUT.noapx, OUT. */
static int emesh_main(int argc, char **argv)
{
    if (argc < 8) { fprintf(stderr, "usage: cfb_port --emesh OUT RGN APX6 CHORD SYSTOL CDEV\n"); return 1; }
    const char *o = argv[2];
    size_t len; unsigned char *f = slurp(argv[3], &len); if (!f) return 2;
    Apx A; if (apx_load(argv[4], &A)) return 2;
    double ch = strtod(argv[5], NULL), st = strtod(argv[6], NULL), cd = strtod(argv[7], NULL);
    CsEmCtx X; double tl[4]; const char *why = "";
    if (cs_em_ctx(f, len, &X, tl, &why)) { printf("EMESH NOT PORTED: %s\n", why); return 3; }
    CsCfbConst C; cs_cfb_consts(6, ch, st, cd, &C);
    C.t9238 = tl[0]; C.t9240 = tl[1]; C.t9248 = tl[2]; C.t9250 = tl[3];
    short ty, v1268; memcpy(&ty, A.op + 4, 2); memcpy(&v1268, A.op + 0x1268, 2);
    int tei = cs_em_tei(ty, v1268, 0, 0);
    int t6 = ri4(f + 0xc), t7 = ri4(f + 0x18);
    short a46, a47; memcpy(&a46, f + t6 + 0xaa + 4 * X.nA + 4, 2); memcpy(&a47, f + t7 + 0xaa + 4 * X.nB + 4, 2);
    int b6 = chain_max(f, len, X.tabA, X.nA), b7 = chain_max(f, len, X.tabB, X.nB);
    int pool = cs_em_pool(b6, a46, b7, a47, X.nA, X.nB);
    printf("EMCTX %d %d %a %a %a %a %a %a tei %d pool %d b6 %d b7 %d a4 %d %d T %a %a %a %a\n", X.nA, X.nB, X.firstA, X.stepA, X.lastA, X.firstB,
           X.stepB, X.lastB, tei, pool, b6, b7, a46, a47, C.t9238, C.t9240, C.t9248, C.t9250);
    CsEst L; if (cs_est_init_cfb(&L, pool)) return 2;
    int cap = pool + 1000 < 0x7fff ? pool + 1000 : 0x7fff;
    CsNode *arr = calloc((size_t)cap + 1, sizeof(CsNode)), *work = calloc((size_t)cap + 1, sizeof(CsNode));
    int *tb = malloc(sizeof(int) * ((size_t)X.nB + 1)), *ta = malloc(sizeof(int) * ((size_t)X.nA + 1));
    CsEmTrace T = { em_list, em_arr, em_ext };
    CsBuf out = { 0 }; out.cap = 1 << 20; out.p = malloc(out.cap);
    unsigned char h[0x38]; memset(h, 0, sizeof h); memset(h + 0xc, 0xff, 0x1c);
    cs_scan_header(h, -0x38, -1, -1);
    memcpy(out.p, h, 0x38); out.n = 0x38;
    cs_scan_header(h, -0x38, ri4(f + 8), ri4(f + 4));
    double pos = 0; int toff = 0;
    printf("EMDIR 1\n");
    int e = cs_em_dir(&X, &C, 1, tei, pool, &L, arr, work, tb, &out, &pos, &toff, &T, &why);
    if (e) { printf("EMESH dir B %d %s\n", e, e == 1 ? why : ""); return 3; }
    memcpy(h + 0x18, &toff, 4); memset(h, 0, 4);
    memcpy(out.p, h, 0x38);
    spit(o, ".b", &out, out.n);
    printf("EMTAB 1"); for (int i = 0; i < X.nB; i++) printf(" %d", tb[i]); printf("\n");
    memcpy(h, out.p, 0x38);
    printf("EMDIR 0\n");
    e = cs_em_dir(&X, &C, 0, tei, pool, &L, arr, work, ta, &out, &pos, &toff, &T, &why);
    if (e) { printf("EMESH dir A %d %s\n", e, e == 1 ? why : ""); return 3; }
    spit(o, ".noapx", &out, out.n);                    /* the header as direction B left it (FUN_18022c850 writes the new one) */
    memcpy(h + 0xc, &toff, 4); { int m = -0x38; memcpy(h, &m, 4); }
    memcpy(out.p, h, 0x38);
    printf("EMTAB 0"); for (int i = 0; i < X.nA; i++) printf(" %d", ta[i]); printf("\n");
    apx_append(&out, &A);
    spit(o, "", &out, out.n);
    printf("EMFILE %zu header", out.n); for (int i = 0; i < 0x38; i += 4) printf(" %d", ri4(out.p + i)); printf("\n");
    printf("APX %zu TOLS", out.n); for (int i = 0; i < 9; i++) printf(" %a", A.tols[i]); printf("\n");
    free(f); free(A.a); free(out.p); free(arr); free(work); free(ta); free(tb); free(L.nd);
    return 0;
}
/* cfb_port --segs EM TOL P6 P7: FUN_18018a640 (cs_seg_dir) on a tmpemesh.tmp for direction A then B, as FUN_1801b1ad0 calls it (p4 = 1,
   p5 = 2, p9 = 2 n, once more with the returned count when larger, l.288948-288955); prints what tools/peloader --lmm SEGS prints.
   cfb_port --pts EM TOL P6 P7 goes on as FUN_1801b1ad0 does (asm 1801b1f84 / 1801b223a / 1801b2552..1801b2700): FUN_1801b4e80 sizes
   (cs_seg_sizes), FUN_1801a7fd0's in-memory arrays (cs_seg_pts_alloc), FUN_1801b88e0 for A then B (cs_seg_points), FUN_1801b84f0 on B
   (cs_seg_flip); prints what tools/peloader --lmm PTS prints. */
static uint64_t fnv(uint64_t h, const void *p, size_t n) { const unsigned char *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }
static void segs_print(int dir, int n, int m, const CsSeg *S, const CsSegLine *H, const char *th, const char *tr)
{
    for (int i = 0; i < n; i++)
        printf("%s %d %d %d %d %d %d %d %a\n", th, dir, i, H[i].first + 1, H[i].last + 1, H[i].n10, H[i].n12, H[i].dir, H[i].pos);
    for (int i = 0; i < m; i++) {
        unsigned char b[0xb0]; cs_seg_bytes(&S[i], b);
        printf("%s %d %d %016llx\n", tr, dir, i, (unsigned long long)fnv(1469598103934665603ull, b, 0xb0));
        if (getenv("SEGS_DUMP")) { printf("SD %d %d", dir, i); for (int q = 0; q < 0xb0; q++) printf("%02x", b[q]); printf("\n"); }
    }
}
/* --d15 (surf_classic3.md 12.30): FUN_18015d0f0's first part, as tools/peloader --lmm D15 runs the DLL's: for A then B (when it has
   lines) FUN_180150840 (cs_d15_split, ctx +0x2b0 = B2B0 = DAT_1804d5640, FUN_1801b1ad0 asm 1801b1deb..1801b1df9) and FUN_18014d5c0
   (cs_d15_merge, gap ctx +0x2d8 = DAT_1804d5668 = 3 x max(|stepA|, |stepB|), asm 1801b1f1d..1801b1f4f); prints "QS dir count err",
   "QM dir merges", per line "QH dir line n10 n12 dir pos k" and its records in list order "QR dir line j hash" (0xb0 bytes, +0 / +8 as
   the list position + 1 of the next / previous record, 0 = none) */
static void d15_print(int dir, const CsSegDir *D)
{
    for (int l = 0; l < D->nl; l++) {
        int k = 0;
        for (int r = D->H[l].first; r != -1; r = D->S[r].next) k++;
        const CsSegLine *H = &D->H[l];
        printf("QH %d %d %d %d %d %a %d\n", dir, l, H->n10, H->n12, H->dir, H->pos, k);
        int j = 0;
        for (int r = D->H[l].first; r != -1; r = D->S[r].next, j++) {
            unsigned char b[0xb0]; cs_seg_bytes(&D->S[r], b);
            long long a = D->S[r].next != -1 ? j + 2 : 0, c = D->S[r].prev != -1 ? j : 0;
            memcpy(b, &a, 8); memcpy(b + 8, &c, 8);
            printf("QR %d %d %d %016llx\n", dir, l, j, (unsigned long long)fnv(1469598103934665603ull, b, 0xb0));
            if (getenv("SEGS_DUMP")) { printf("QD %d %d %d", dir, l, j); for (int q = 0; q < 0xb0; q++) printf("%02x", b[q]); printf("\n"); }
        }
    }
}
/* D15_ZU=M (surf_classic3.md 12.36): the zone builder's first leaves as tools/peloader --lmm D15 runs the DLL's (zu_run there): "ZU C"
   cs_zb_count, "ZU L" cs_zb_stall_init(7), "ZU E" cs_zb_ends on A and B; M >= 2 "ZU F" cs_zb_clear_stall(L, -4) on A, "ZU G" cs_zb_clear(-5)
   on B; M >= 3 "ZU H / I" with L[4] = 1 */
static void zu_agg(const char *tag, const CsSegDir *D, int cnt)
{
    int b = 0, m4 = 0, m5 = 0, s12 = 0;
    for (int l = 0; l < D->nl; l++) {
        s12 += D->H[l].n12;
        for (int r = D->H[l].first; r != -1; r = D->S[r].next) {
            const CsSeg *R = &D->S[r];
            b += R->b20 != 0; m4 += (R->s30 == -4) + (R->s32 == -4); m5 += (R->s30 == -5) + (R->s32 == -5);
        }
    }
    printf("ZU %s %d %d %d %d %d %d %d\n", tag, D->dir, 0, cnt, b, m4, m5, s12);
}
static void zu_port(CsSegDir *D, double t9250, int m)
{
    int cnt[2] = { 0, 0 };
    short L[16]; memset(L, 0, sizeof L);
    for (int d = 0; d < 2; d++) printf("ZU C %d %d\n", d, cs_zb_count(&D[d]));
    cs_zb_stall_init(L, 7);
    printf("ZU L %d %d %d %d %d %d %d\n", L[0], L[1], L[2], L[3], L[4], L[5], L[6]);
    for (int d = 0; d < 2; d++) { cs_zb_ends(&D[d], t9250, &cnt[d]); zu_agg("E", &D[d], cnt[d]); }
    if (m >= 2) {
        cs_zb_clear_stall(&D[0], L, -4, &cnt[0]); zu_agg("F", &D[0], cnt[0]);
        cs_zb_clear(&D[1], -5, &cnt[1]); zu_agg("G", &D[1], cnt[1]);
    }
    if (m >= 3) {
        cs_zb_stall_set(L, -4, 1);
        cs_zb_clear_stall(&D[0], L, -5, &cnt[0]); zu_agg("H", &D[0], cnt[0]);
        cs_zb_clear_stall(&D[1], L, 7, &cnt[1]); zu_agg("I", &D[1], cnt[1]);
    }
}
/* D15_ZREP=FILE (with D15_FULL D15_6E60, surf_classic3.md 12.38): stage Z1 - the zone buffer and its files (cam3d_cscallop_zbuf.c) replayed
   on the call log of the DLL's own zone builder (tools/peloader --lmm D15_ZB D15_ZBLOG: its "ZC" lines). The builder's prologue first
   (FUN_1801bc5c0 A / B for n = 2 x records + 10, the buffer's setup asm 180187d85..180187e8f); then every call the builder (or its
   callees) made - FUN_1801ab410, FUN_1801a7d60, FUN_1801aa5e0, FUN_1801a95e0, FUN_1801a9460, FUN_1801adec0, and FUN_1801ad550 where its
   caller stores the result in +0x1c (asm 180188726 / 180180807) - is made on the port's buffer; calls made inside those functions
   (FUN_1801aa5e0 from FUN_1801a7d60 asm 1801a7ee6, FUN_1801ad550 from FUN_1801aa5e0 asm 1801aa809) are the port's own. Prints "ZC" /
   "ZZ" as the harness does (the buffer at each call), then the files by structure ("ZH" / "ZQ" / "ZD", +0x84 of the header printed as 0:
   never written by Mastercam, NOT DECODED). */
typedef struct { CsSegDir *D; int nzz, box; } ZRep;
static void zr_ident(const CsSegDir *D, int dir, int r, char *o, size_t n)
{
    if (dir < 0 || r < 0) { snprintf(o, n, "? 0"); return; }
    if (dir > 1 || r >= D[dir].n) { snprintf(o, n, "? bad %d %d", dir, r); return; }
    int l = D[dir].S[r].line, j = 0;
    for (int q = D[dir].H[l].first; q != -1; q = D[dir].S[q].next, j++) if (q == r) { snprintf(o, n, "%d %d %d", dir, l, j); return; }
    snprintf(o, n, "? lost");
}
static int zr_find(const CsSegDir *D, int dir, int l, int j)
{
    if (dir < 0 || dir > 1 || l < 0 || l >= D[dir].nl) return -1;
    int q = D[dir].H[l].first;
    while (q != -1 && j-- > 0) q = D[dir].S[q].next;
    return q;
}
static void zr_step(const CsSegDir *D, const CsZStep *s, char *o, size_t n)
{
    char id[64]; zr_ident(D, s->rdir, s->rec, id, sizeof id);
    snprintf(o, n, "%d %a %a %a %s %d %d %d %d", s->i0, s->p[0], s->p[1], s->p[2], id, s->b28, s->b29, s->e2a, s->s2c);
}
static void zr_dig(ZRep *R, const CsZbc *Z)
{
    char id[64]; zr_ident(R->D, Z->fdir, Z->frec, id, sizeof id);
    printf("ZZ %d %d %d %d %d %d %d %a %a %a | %d %d %d %d %d | %d %d %d %d %d | %d %d %d %d %d %d %d %d | %s %d", Z->zone, Z->pass,
           Z->side, Z->fopen, Z->zsh, Z->draw, Z->err, Z->d28, Z->tol, Z->t9250, Z->spos[0], Z->h[0].cnt, Z->h[0].pass, Z->h[0].side,
           Z->n88[0], Z->spos[1], Z->h[1].cnt, Z->h[1].pass, Z->h[1].side, Z->n88[1], Z->ac, Z->cap, Z->b4, Z->b8,
           Z->bc, Z->c0, Z->last[0], Z->last[1], id, Z->f0);
    if (R->box) for (int k = 0; k < 6; k++) printf(" %a", Z->box[k]);
    unsigned long long h = 1469598103934665603ull; char o[256];
    for (int i = 0; i < Z->c0 && i < Z->cap; i++) {
        zr_step(R->D, &Z->st[i], o, sizeof o);
        for (char *p = o; *p; p++) { h ^= (unsigned char)*p; h *= 1099511628211ull; }
        h ^= Z->fl[i]; h *= 1099511628211ull;
    }
    printf(" | %016llx\n", h);
}
static void zr_trace(CsZbc *Z, const char *nm, unsigned long ra, int arg, const CsZStep *s)
{
    ZRep *R = Z->user; char o[256];
    if (!strcmp(nm, "ab410")) printf("ZC ab410 %lx %d %d %d\n", ra, s->i0, s->rdir, s->rec);
    else if (!strcmp(nm, "a7d60")) { zr_step(R->D, s, o, sizeof o); printf("ZC a7d60 %lx %s\n", ra, o); }
    else if (!strcmp(nm, "adec0")) printf("ZC adec0 %lx %d\n", ra, arg);
    else if (!strcmp(nm, "ad550")) { printf("ZC ad550 %lx %d\n", ra, arg); return; }
    else printf("ZC %s %lx\n", nm, ra);
    if (!((!strcmp(nm, "ab410") || !strcmp(nm, "aa460")) && R->nzz < 2 && ++R->nzz)) zr_dig(R, Z);
    if (!strcmp(nm, "a7d60")) R->box = 1;
}
static void zr_files(ZRep *R, const CsZbc *Z)
{
    for (int f = 0; f < 2; f++) {
        const unsigned char *b = Z->f[f].p; long n = (long)Z->f[f].n, o = 0; int z = 0;
        while (o + 12 <= n) {
            int c; short pa, sd; memcpy(&c, b + o, 4); memcpy(&pa, b + o + 4, 2); memcpy(&sd, b + o + 6, 2);
            printf("ZH %d %d %d %d %d %d %d\n", f, z, c, pa, sd, 0, b[o + 10]);
            o += 12;
            for (int k = 0; k < c && o + (f ? 8 : 0x30) <= n; k++) {
                long long v; int dir, r; char id[64];
                memcpy(&v, b + o + (f ? 0 : 0x20), 8); cs_zf_rec(v, &dir, &r); zr_ident(R->D, dir, r, id, sizeof id);
                if (!f) {
                    int i0, i28; double p[3]; memcpy(&i0, b + o, 4); memcpy(p, b + o + 8, 24); memcpy(&i28, b + o + 0x28, 4);
                    printf("ZQ %d %d %d %a %a %a %s %d\n", z, k, i0, p[0], p[1], p[2], id, i28);
                    o += 0x30;
                } else { printf("ZD %d %d %s\n", z, k, id); o += 8; }
            }
            z++;
        }
    }
}
static void zrep_port(CsSegDir *D, double tol, const double tl[4], const char *log)
{
    FILE *f = fopen(log, "r");
    if (!f) { printf("ZREP cannot read %s\n", log); return; }
    CsSegDir *DD[2] = { &D[0], &D[1] };
    int nrec = cs_zb_count(&D[0]) + cs_zb_count(&D[1]), n = nrec * 2 + 10;   /* FUN_1801bc5c0 A / B, asm 180187be7..180187c2a (p6 = 1) */
    ZRep R = { D, 0, 0 };
    CsZbc Z; memset(&Z, 0, sizeof Z);
    cs_zb_buf_init(&Z, n, 1, 0, 0, tl[1], tol, tl[3], tl[0]);
    Z.trace = zr_trace; Z.user = &R;
    char line[1024]; int ok = 1;
    while (ok && fgets(line, sizeof line, f)) {
        if (strncmp(line, "ZC ", 3)) continue;
        char nm[16]; unsigned long ra; int k = 0;
        if (sscanf(line + 3, "%15s %lx %n", nm, &ra, &k) < 2) continue;
        const char *a = line + 3 + k;
        if (!strcmp(nm, "ab410")) { int z, p, s; sscanf(a, "%d %d %d", &z, &p, &s); cs_zb_start(&Z, (short)z, (short)p, (short)s, ra); }
        else if (!strcmp(nm, "a7d60")) {
            if (ra == 0x1801a7ee6ul) continue;
            CsZStep s; memset(&s, 0, sizeof s); char q[16]; int dir = -1, l = 0, j = 0, b28, b29, e2a, s2c, m = 0;
            if (sscanf(a, "%d %la %la %la %15s %n", &s.i0, &s.p[0], &s.p[1], &s.p[2], q, &m) < 5) { ok = 0; break; }
            if (!strcmp(q, "?")) { sscanf(a + m, "%*s %d %d %d %d", &b28, &b29, &e2a, &s2c); }
            else { dir = atoi(q); sscanf(a + m, "%d %d %d %d %d %d", &l, &j, &b28, &b29, &e2a, &s2c); }
            s.rdir = dir; s.rec = dir < 0 ? -1 : zr_find(D, dir, l, j);
            if (dir >= 0 && s.rec < 0) { printf("ZREP record %d %d %d not in the port's lists: NOT PORTED\n", dir, l, j); ok = 0; break; }
            if (!strcmp(q, "?") && strncmp(a + m, "0 ", 2)) { printf("ZREP a step's record is not in the lists: NOT PORTED\n"); ok = 0; break; }
            s.b28 = (unsigned char)b28; s.b29 = (unsigned char)b29; s.e2a = (unsigned short)e2a; s.s2c = (short)s2c;
            if (cs_zb_add(&Z, DD, &s, ra) < 0) { printf("ZREP draw mode: NOT PORTED\n"); ok = 0; }
        }
        else if (!strcmp(nm, "aa5e0")) { if (ra != 0x1801a7ee6ul && cs_zb_flush(&Z, DD, ra) < 0) { printf("ZREP draw mode: NOT PORTED\n"); ok = 0; } }
        else if (!strcmp(nm, "a95e0")) cs_zb_headers(&Z, ra);
        else if (!strcmp(nm, "aa460")) {
            if (ra == 0x1801ab477ul) continue;   /* FUN_1801ab410's own (asm 1801ab472) */
            /* FUN_1801806b0 asm 18018096f..180180974: +0x1c = r15w (0 from asm 1801807b6, not written in between), then FUN_1801aa460 */
            if (ra != 0x180180979ul) { printf("ZREP FUN_1801aa460 from %lx: NOT PORTED\n", ra); ok = 0; break; }
            Z.side = 0; cs_zb_reset(&Z, ra);
        }
        else if (!strcmp(nm, "a9460")) cs_zb_check(&Z, ra);
        else if (!strcmp(nm, "adec0")) cs_zb_drop(&Z, (short)atoi(a), ra);
        else if (!strcmp(nm, "ad550")) {
            if (ra == 0x1801aa809ul) continue;
            /* the builder (asm 180188726, local_99fc) and FUN_1801806b0 (asm 180180807) store FUN_1801ad550(+0xc0, +0xd0, ctx) in +0x1c */
            if (ra != 0x180188726ul && ra != 0x180180807ul) { printf("ZREP FUN_1801ad550 from %lx: NOT PORTED\n", ra); ok = 0; break; }
            printf("ZC ad550 %lx %d\n", ra, Z.c0);
            Z.side = cs_zb_side(&Z, DD, Z.c0, Z.st);
        }
    }
    fclose(f);
    printf("ZE %d\n", (int)Z.f[0].pos);   /* tmpprojz.tmp's position at the end (the builder's DAT_1804e930c, asm LAB_1801899a1) */
    zr_files(&R, &Z);
    cs_zb_buf_free(&Z);
}
/* D15_ZUNIT=FILE (with D15_FULL D15_6E60, surf_classic3.md 12.39): stage Z2 - every "ZI" call of the DLL's unit log (tools/peloader --lmm
   D15_ZB D15_ZBUNIT) made on the port's functions (cam3d_cscallop_zcore.c; FUN_1801864b0 = cs_9a20_pt) with the same inputs, "ZI" echoed
   and "ZO" printed as the harness does (after the builder's prologue FUN_1801bc5c0 / FUN_1801c0320, which the inputs do not read) */
static const CsLoop *g_zul;   /* zunit_port's loop: "? F d i" / "? P k" (records in no list, surf_classic3.md 12.41) */
static int zu_tok_rec(const CsSegDir *D, char **t, int *dir, int *rec)
{
    if (!strcmp(t[0], "?")) {
        *dir = -1; *rec = -1;
        if (!strcmp(t[1], "F")) return g_zul && cs_zc_mem(g_zul, 0, atoi(t[2]), atoi(t[3]), dir, rec) ? 4 : -1;
        if (!strcmp(t[1], "P")) return g_zul && cs_zc_mem(g_zul, 1, atoi(t[2]), 0, dir, rec) ? 3 : -1;
        return strcmp(t[1], "0") ? -1 : 2;
    }
    *dir = atoi(t[0]); *rec = zr_find(D, *dir, atoi(t[1]), atoi(t[2]));
    return *rec < 0 ? -1 : 3;
}
static int zu_wpt(const CsSegDir *D, char **t, int nt, CsWPt *W)
{
    int k = zu_tok_rec(D, t, &W->rdir, &W->rec);
    if (k < 0 || k + 11 > nt || W->rdir < 0) return -1;
    W->hd = atoi(t[k]); W->end = (short)atoi(t[k + 1]);
    for (int i = 0; i < 3; i++) W->P[i] = strtod(t[k + 2 + i], NULL);
    for (int i = 0; i < 4; i++) W->ln[i] = atoi(t[k + 5 + i]);
    W->b40 = (unsigned char)atoi(t[k + 9]); W->b41 = (unsigned char)atoi(t[k + 10]);
    return W->hd > 1 ? -1 : k + 11;
}
static void zp(const char *fmt, ...);
static void zu_wout(const CsWPt *W) { zp(" [ %d %d %d %d %d %d]", W->ln[0], W->ln[1], W->ln[2], W->ln[3], W->b40, W->b41); }
/* the replay's outputs wait for the log's own "ZO" line (calls nest: FUN_1801aaea0's callees log inside it, surf_classic3.md 12.41):
   each call's output is pushed, a "ZO" of the log prints the latest */
static char zp_cur[1 << 20], *zp_st[256]; static int zp_n, zp_on;
static void zp(const char *fmt, ...)
{
    size_t l = strlen(zp_cur);
    va_list ap; va_start(ap, fmt); vsnprintf(zp_cur + l, sizeof zp_cur - l, fmt, ap); va_end(ap);
}
static void zp_push(void)
{
    if (!zp_on) return;
    zp_on = 0;
    if (zp_n < 256) zp_st[zp_n++] = strdup(zp_cur);
}
/* Z3 (surf_classic3.md 12.42): a candidate entry as the harness prints it (zu_cand: +0 +0x60 +0x20, the doubles +0x30..+0x88, headers,
   +0xa0, +0xa2, record) and the stall list */
static int zu_cand_in(const CsSegDir *D, char **t, int nt, CsZCand *C)
{
    if (nt < 27) return -1;
    memset(C, 0, sizeof *C);
    C->b0 = (unsigned char)atoi(t[0]); C->s2 = (short)atoi(t[1]); C->i4 = atoi(t[2]); C->b8 = (unsigned char)atoi(t[3]);
    for (int i = 0; i < 4; i++) C->ln[i] = atoi(t[4 + i]);
    C->b28 = (unsigned char)atoi(t[8]); C->s2c = (short)atoi(t[9]); C->b60 = (unsigned char)atoi(t[10]); C->q20 = atoll(t[11]);
    double *f[11] = { &C->p[0], &C->p[1], &C->p[2], &C->dl[0], &C->dl[1], &C->dl[2], &C->d68, &C->a70, &C->a78, &C->p80, &C->p88 };
    for (int i = 0; i < 11; i++) *f[i] = strtod(t[12 + i], NULL);
    C->hd = atoi(t[23]); C->dir = (short)atoi(t[24]); C->end = (short)atoi(t[25]);
    int k = zu_tok_rec(D, t + 26, &C->rdir, &C->rec);
    return k < 0 ? -1 : 26 + k;
}
static void zu_cand_out(const CsSegDir *D, const CsZCand *C, int full)
{
    char id[64]; zr_ident(D, C->rdir, C->rec, id, sizeof id);
    if (!full) { zp(" | %d %d %lld %a %a %a %a %a %a %a %a %a %a %a %d %d %d %s", C->b0, C->b60, C->q20, C->p[0], C->p[1], C->p[2], C->dl[0],
                    C->dl[1], C->dl[2], C->d68, C->a70, C->a78, C->p80, C->p88, C->hd, C->dir, C->end, id); return; }
    zp(" | %d %d %d %d %d %d %d %d %d %d %d %lld %a %a %a %a %a %a %a %a %a %a %a %d %d %d %s", C->b0, C->s2, C->i4, C->b8, C->ln[0], C->ln[1], C->ln[2],
       C->ln[3], C->b28, C->s2c, C->b60, C->q20, C->p[0], C->p[1], C->p[2], C->dl[0], C->dl[1], C->dl[2], C->d68, C->a70, C->a78, C->p80, C->p88,
       C->hd, C->dir, C->end, id);
}
/* FUN_18016a7b0's entries (surf_classic3.md 12.44): the full entry then +0x1c / +0x1d / +0x29 / +0x2a */
static int zu_candx_in(const CsSegDir *D, char **t, int nt, CsZCand *C)
{
    int k = zu_cand_in(D, t, nt, C);
    if (k < 0 || k + 4 > nt) return -1;
    C->b1c = (unsigned char)atoi(t[k]); C->b1d = (unsigned char)atoi(t[k + 1]); C->b29 = (unsigned char)atoi(t[k + 2]); C->b2a = (unsigned char)atoi(t[k + 3]);
    return k + 4;
}
static void zu_candx_out(const CsSegDir *D, const CsZCand *C) { zu_cand_out(D, C, 1); zp(" %d %d %d %d", C->b1c, C->b1d, C->b29, C->b2a); }
/* the zonbuf pool (surf_classic3.md 12.46) as the harness prints it: used, free (total - used), +0x2a, +0x2c, +0x8daa */
static CsZPool g_zpl, g_zplb;   /* the pool of the replayed nested calls; FUN_1801bca00's own (surf_classic3.md 12.47) */
static void zu_pool_out(const CsZPool *P) { zp(" %d %d %d %d %d", P->used, P->total - P->used, P->nblk, P->cur, P->vcnt); }
static void zunit_port(CsLoop *Lp, CsSegDir *D, const char *log)
{
    FILE *f = fopen(log, "r");
    if (!f) { fprintf(stdout, "ZUNIT cannot read %s\n", log); return; }
    static char line[1 << 20];
    g_zul = Lp; zp_n = 0; zp_on = 0;
    cs_zw_pool_free(&g_zplb); cs_zw_pool_init(&g_zplb);
    cs_zw_pool_free(&g_zpl); cs_zw_pool_init(&g_zpl);   /* FUN_18015d200(1, 9, 0x32, 1000) in the builder's prologue (asm 180187a02) */
    while (fgets(line, sizeof line, f)) {
        zp_push();
        if (!strncmp(line, "ZO ", 3)) { if (zp_n > 0) { fputs(zp_st[zp_n - 1], stdout); free(zp_st[--zp_n]); } continue; }
        if (!strncmp(line, "ZK ", 3)) {   /* the builder's end codes at this point (Z3, surf_classic3.md 12.42): the port's records follow */
            char kb[256], *kt[16]; int kn = 0; snprintf(kb, sizeof kb, "%s", line);
            for (char *q = strtok(kb, " \n"); q && kn < 16; q = strtok(NULL, " \n")) kt[kn++] = q;
            int dir, rec, k = kn > 1 ? zu_tok_rec(D, kt + 1, &dir, &rec) : -1;
            if (k < 0 || dir < 0 || 1 + k + 2 > kn) { printf("ZKBAD %s", line); continue; }
            D[dir].S[rec].s30 = (short)atoi(kt[1 + k]); D[dir].S[rec].s32 = (short)atoi(kt[2 + k]);
            continue;
        }
        if (strncmp(line, "ZI ", 3)) continue;
        fputs(line, stdout);
        zp_cur[0] = 0; zp_on = 1;
        static char buf[1 << 20], *t[16384]; int nt = 0; strcpy(buf, line);
        for (char *p = strtok(buf, " []\n"); p && nt < 16384; p = strtok(NULL, " []\n")) t[nt++] = p;
        const char *n = t[1];
        if (!strcmp(n, "864b0")) {
            int dir, rec, k = zu_tok_rec(D, t + 4, &dir, &rec);
            if (k < 0 || dir < 0 || atoi(t[4 + k]) != dir) { zp("ZO 864b0 NOT PORTED (record / headers)\n"); continue; }
            double P[3]; cs_9a20_pt(Lp, atoi(t[3]), dir, rec, P);
            zp("ZO 864b0 %a %a %a\n", P[0], P[1], P[2]);
        } else if (!strcmp(n, "c24f0")) {
            CsZbc Z; memset(&Z, 0, sizeof Z);
            int fd, fr, d, r, k = zu_tok_rec(D, t + 4, &fd, &fr);
            if (k < 0) { zp("ZO c24f0 NOT PORTED\n"); continue; }
            Z.ac = atoi(t[3]); Z.fdir = fd; Z.frec = fr; Z.f0 = (short)atoi(t[4 + k]);
            int e = atoi(t[5 + k]), k2 = zu_tok_rec(D, t + 6 + k, &d, &r);
            if (k2 < 0) { zp("ZO c24f0 NOT PORTED\n"); continue; }
            zp("ZO c24f0 %d\n", cs_zc_first(&Z, e, d, r));
        } else if (!strcmp(n, "5a830")) {
            /* FUN_18015a830(ctx) (asm 18015a830): ctx +0x309 == 0 -> 1 (op 43: ctx +0x309 = 0, surf_classic3.md 12.30); its +0x309 arm
               (FUN_180149630 / FUN_18015a860) is not ported */
            if (atoi(t[3])) zp("ZO 5a830 NOT PORTED (ctx +0x309)\n"); else zp("ZO 5a830 1\n");
        } else if (!strcmp(n, "6e860")) {
            double P[3], v[3]; for (int i = 0; i < 3; i++) { P[i] = strtod(t[3 + i], NULL); v[i] = strtod(t[6 + i], NULL); }
            int ln[2][4]; unsigned short c[2][2];
            int m = cs_zc_beside(Lp, P, v, ln, c);
            zp("ZO 6e860 %d", m);
            for (int i = 0; i < m; i++) zp(" %d %d %d %d %d %d", ln[i][0], ln[i][1], ln[i][2], ln[i][3], c[i][0], c[i][1]);
            zp("\n");
        } else if (!strcmp(n, "6eb80")) {
            CsWPt A, B; double d = strtod(t[3], NULL);
            int k = zu_wpt(D, t + 4, nt - 4, &A), k2 = k < 0 ? -1 : zu_wpt(D, t + 4 + k, nt - 4 - k, &B);
            if (k2 < 0) { zp("ZO 6eb80 NOT PORTED (walk point)\n"); continue; }
            unsigned char o = 0; int r = cs_zc_pair(Lp, d, &A, &B, &o);
            if (r == -100) { zp("ZO 6eb80 NOT PORTED (FUN_18017c1c0)\n"); continue; }
            zp("ZO 6eb80 %d", r); zu_wout(&A); zu_wout(&B); if (r) zp(" %d", o); zp("\n");
        }
        else if (!strcmp(n, "b6000")) {
            double A[2] = { strtod(t[4], NULL), strtod(t[5], NULL) }, B[2] = { strtod(t[6], NULL), strtod(t[7], NULL) }, o[4];
            cs_zc_box(atoi(t[3]), A, B, atoi(t[8]), strtod(t[9], NULL), o);
            zp("ZO b6000 %a %a %a %a\n", o[0], o[1], o[2], o[3]);
        } else if (!strcmp(n, "aed20")) {
            int dir, rec, k = zu_tok_rec(D, t + 5, &dir, &rec), hd = atoi(t[4]);
            if (k < 0 || dir < 0 || hd > 1) { zp("ZO aed20 NOT PORTED (record / headers)\n"); continue; }
            double b[4]; for (int i = 0; i < 4; i++) b[i] = strtod(t[5 + k + i], NULL);
            zp("ZO aed20 %d\n", cs_zc_skip(Lp, atoi(t[3]), hd, dir, rec, b));
        } else if (!strcmp(n, "537e0")) {
            double b[4], P[2]; for (int i = 0; i < 4; i++) b[i] = strtod(t[3 + i], NULL);
            P[0] = strtod(t[7], NULL); P[1] = strtod(t[8], NULL);
            zp("ZO 537e0 %d\n", cs_zc_outcode(b, P));
        } else if (!strcmp(n, "aaea0")) {
            /* the rest of Z2 (surf_classic3.md 12.41): FUN_1801aaea0's tree */
            double A[3], B[3]; for (int i = 0; i < 3; i++) { A[i] = strtod(t[6 + i], NULL); B[i] = strtod(t[9 + i], NULL); }
            int r = cs_zc_seg(Lp, atoi(t[3]), strtod(t[4], NULL), atoi(t[5]), A, B);
            if (r == -101) zp("ZO aaea0 NOT PORTED (header / line past the headers)\n"); else zp("ZO aaea0 %d\n", r);
        } else if (!strcmp(n, "ab730")) {
            double q[4]; for (int i = 0; i < 4; i++) q[i] = strtod(t[3 + i], NULL);
            int dir, rec, k = zu_tok_rec(D, t + 8, &dir, &rec);
            if (k < 0 || dir < 0) { zp("ZO ab730 NOT PORTED (record)\n"); continue; }
            int r = cs_zc_clip(Lp, q, strtod(t[7], NULL), dir, rec, atoi(t[8 + k]), atoi(t[9 + k]), atoi(t[10 + k]));
            if (r == -101) zp("ZO ab730 NOT PORTED (header / line past the headers)\n"); else zp("ZO ab730 %d\n", r);
        } else if (!strcmp(n, "c2a10")) {
            int dir, rec, k = zu_tok_rec(D, t + 4, &dir, &rec), q;
            int hd = k < 0 ? 9 : atoi(t[6 + k]);
            if (k < 0 || dir < 0 || hd > 1) { zp("ZO c2a10 NOT PORTED (record)\n"); continue; }
            int r = cs_zc_adj(Lp, atoi(t[3]), dir, rec, atoi(t[4 + k]), atoi(t[5 + k]), hd, &q);
            if (r == -101) { zp("ZO c2a10 NOT PORTED (header)\n"); continue; }
            char id[64]; zr_ident(D, r ? hd : -1, q, id, sizeof id); zp("ZO c2a10 %s\n", id);
        } else if (!strcmp(n, "aeb30")) {
            int d1, r1, d2 = -1, r2, k = zu_tok_rec(D, t + 4, &d1, &r1), k2 = k < 0 ? -1 : zu_tok_rec(D, t + 4 + k, &d2, &r2);
            int hd = k2 < 0 ? 9 : atoi(t[4 + k + k2]);
            if (k2 < 0 || d1 < 0 || d2 != hd || hd > 1) { zp("ZO aeb30 NOT PORTED (record)\n"); continue; }
            double o[4] = { 0, 0, 0, 0 };
            int r = cs_zc_ibox(Lp, strtod(t[3], NULL), d1, r1, r2, hd, o);
            if (r == -101) zp("ZO aeb30 NOT PORTED (line past the headers)\n"); else zp("ZO aeb30 %d %a %a %a %a\n", r, o[0], o[1], o[2], o[3]);
        } else if (!strcmp(n, "aba60")) {
            double b[4], q[4], I[2], u, tt; for (int i = 0; i < 4; i++) { b[i] = strtod(t[4 + i], NULL); q[i] = strtod(t[8 + i], NULL); }
            int r = cs_zc_edge(atoi(t[3]), b, q, I, &u, &tt);
            if (r == -101) zp("ZO aba60 NOT PORTED (code)\n");
            else if (r) zp("ZO aba60 1 %a %a %a %a\n", I[0], I[1], u, tt); else zp("ZO aba60 0\n");
        } else if (!strcmp(n, "bfbd0")) {
            int d1, r1, d2 = -1, r2, k = zu_tok_rec(D, t + 5, &d1, &r1), k2 = k < 0 ? -1 : zu_tok_rec(D, t + 5 + k, &d2, &r2);
            if (k2 < 0 || d1 < 0 || d2 < 0) { zp("ZO bfbd0 NOT PORTED (record)\n"); continue; }
            zp("ZO bfbd0 %d\n", cs_zc_ovl(Lp, atoi(t[3]), atoi(t[4]), d1, r1, d2, r2));
        } else if (!strcmp(n, "bed30")) {
            /* Z3 (surf_classic3.md 12.42) */
            int idx[4] = { atoi(t[3]), atoi(t[4]), atoi(t[5]), atoi(t[3]) - 1 }, m = 0;
            for (int i = 0; i < 4; i++) if (idx[i] > m) m = idx[i];
            CsZbc Z; memset(&Z, 0, sizeof Z);
            Z.st = calloc((size_t)m + 1, sizeof *Z.st); Z.last[0] = idx[1]; Z.last[1] = idx[2];
            for (int i = 0; i < 4; i++) if (idx[i] >= 0) { Z.st[idx[i]].p[0] = strtod(t[6 + 2 * i], NULL); Z.st[idx[i]].p[1] = strtod(t[7 + 2 * i], NULL); }
            zp("ZO bed30 %d\n", cs_zw_prev(&Z, Lp->tol, idx[0]));
            free(Z.st);
        } else if (!strcmp(n, "ab6d0")) {
            short SL[8]; for (int i = 0; i < 8; i++) SL[i] = (short)atoi(t[3 + i]);
            int dir, rec, k = zu_tok_rec(D, t + 11, &dir, &rec);
            if (k < 0 || dir < 0) { zp("ZO ab6d0 NOT PORTED (record)\n"); continue; }
            zp("ZO ab6d0 %d\n", cs_zw_endcode(Lp, SL, dir, rec, atoi(t[11 + k]), atoi(t[12 + k])));
        } else if (!strcmp(n, "aef30")) {
            int hd = atoi(t[3]), dir, rec, k = zu_tok_rec(D, t + 4, &dir, &rec);
            if (k < 0 || dir < 0 || hd > 1) { zp("ZO aef30 NOT PORTED (record)\n"); continue; }
            double b[4], o[6]; for (int i = 0; i < 4; i++) b[i] = strtod(t[4 + k + i], NULL);
            int r = cs_zw_ends(Lp, hd, dir, rec, b, o);
            if (r == -101) { zp("ZO aef30 NOT PORTED (line past the headers)\n"); continue; }
            zp("ZO aef30 %d", r);
            if (r == 0 || r == 2) zp(" %a %a %a", o[0], o[1], o[2]);
            if (r == 1 || r == 2) zp(" %a %a %a", o[3], o[4], o[5]);
            zp("\n");
        } else if (!strcmp(n, "c5ef0")) {
            double a[3], b[3]; for (int i = 0; i < 3; i++) { a[i] = strtod(t[4 + i], NULL); b[i] = strtod(t[7 + i], NULL); }
            zp("ZO c5ef0 %a\n", cs_zw_turn(Lp->tol, atoi(t[3]), a, b));
        } else if (!strcmp(n, "bf40")) {
            double g = strtod(t[3], NULL), c320 = strtod(t[4], NULL), c330 = strtod(t[5], NULL), w = strtod(t[6], NULL);
            int m = atoi(t[7]), j = 8, bad = 0;
            CsZCand *arr = calloc(64, sizeof *arr);
            for (int i = 0; i < m && i <= 0x3c && !bad; i++) {
                if (j >= nt || strcmp(t[j], "|")) { bad = 1; break; }
                int m2 = zu_cand_in(D, t + j + 1, nt - j - 1, &arr[i]);
                if (m2 < 0) bad = 1; else j += 1 + m2;
            }
            if (bad) zp("ZO bf40 NOT PORTED (entry record)\n");
            else zp("ZO bf40 %d\n", cs_zw_choose(Lp->tol, g, c320, c330, w, m, arr));
            free(arr);
        } else if (!strcmp(n, "7a2c0")) {
            CsZCand A, B; double d = strtod(t[3], NULL);
            int k = strcmp(t[4], "|") ? -1 : zu_cand_in(D, t + 5, nt - 5, &A), k2 = k < 0 || strcmp(t[5 + k], "|") ? -1 : zu_cand_in(D, t + 6 + k, nt - 6 - k, &B);
            if (k2 < 0) { zp("ZO 7a2c0 NOT PORTED (entry record)\n"); continue; }
            int ln[2][4]; unsigned short c[2][2];
            int r = cs_zw_cell2(Lp, d, &A, &B, ln, c);
            if (r == -101) { zp("ZO 7a2c0 NOT PORTED (record)\n"); continue; }
            zp("ZO 7a2c0 %d", r);
            for (int q = 0; q < r && q < 2; q++) zp(" %d %d %d %d %d %d", ln[q][0], ln[q][1], ln[q][2], ln[q][3], c[q][0], c[q][1]);
            zp("\n");
        } else if (!strcmp(n, "fb60")) {
            double d = strtod(t[3], NULL); int max = atoi(t[4]); short cnt = (short)atoi(t[5]);
            CsZCand W; int j = 6, bad = 0;
            if (max < 0 || max > 4096 || cnt < 0 || cnt > max || j >= nt || strcmp(t[j], "|")) bad = 1;
            int m = bad ? -1 : zu_cand_in(D, t + j + 1, nt - j - 1, &W);
            if (m < 0) { zp("ZO fb60 NOT PORTED (walk entry)\n"); continue; }
            j += 1 + m;
            CsZCand *arr = calloc((size_t)max + 1, sizeof *arr);
            for (int i = 0; i < cnt && !bad; i++) {
                if (j >= nt || strcmp(t[j], "|")) { bad = 1; break; }
                int m2 = zu_cand_in(D, t + j + 1, nt - j - 1, &arr[i]);
                if (m2 < 0) bad = 1; else j += 1 + m2;
            }
            if (bad) { zp("ZO fb60 NOT PORTED (entry record)\n"); free(arr); continue; }
            short nc = 0; int err = 0;
            int r = cs_zw_cells(Lp, d, &W, max, &cnt, arr, &nc, &err);
            if (r == -101) zp("ZO fb60 NOT PORTED (record)\n");
            else { zp("ZO fb60 %d %d %d", r, cnt, nc); for (int i = 0; i < cnt + nc && i < max; i++) zu_cand_out(D, &arr[i], 1); zp("\n"); }
            free(arr);
        } else if (!strcmp(n, "bfdc0")) {
            /* the rest of Z3 (surf_classic3.md 12.44) */
            CsZCand W, C; short side = (short)atoi(t[3]);
            int k = strcmp(t[4], "|") ? -1 : zu_cand_in(D, t + 5, nt - 5, &W), k2 = k < 0 || strcmp(t[5 + k], "|") ? -1 : zu_cand_in(D, t + 6 + k, nt - 6 - k, &C);
            if (k2 < 0) { zp("ZO bfdc0 NOT PORTED (entry record)\n"); continue; }
            int err = 0, r = cs_zw_side2(Lp, side, &W, &C, &err);
            if (r == -101) zp("ZO bfdc0 NOT PORTED (record)\n"); else zp("ZO bfdc0 %d\n", r);
        } else if (!strcmp(n, "c2e0")) {
            short side = (short)atoi(t[3]); int m = atoi(t[4]), j = 5, bad = 0;
            if (m < 0 || m > 0x3c || 5 + m >= nt) { zp("ZO c2e0 NOT PORTED (sizes)\n"); continue; }
            short ix[0x3c]; int top = 0;
            for (int i = 0; i < m; i++) { ix[i] = (short)atoi(t[j++]); if (ix[i] < 0 || ix[i] >= 0x3c) bad = 1; else if (ix[i] > top) top = ix[i]; }
            CsZCand W, *arr = calloc(0x3c + 1, sizeof *arr);
            int k = bad || strcmp(t[j], "|") ? -1 : zu_cand_in(D, t + j + 1, nt - j - 1, &W);
            if (k < 0) bad = 1; else j += 1 + k;
            for (int i = 0; i < m && !bad; i++) {
                if (j >= nt || strcmp(t[j], "|")) { bad = 1; break; }
                int m2 = zu_cand_in(D, t + j + 1, nt - j - 1, &arr[ix[i]]);
                if (m2 < 0) bad = 1; else j += 1 + m2;
            }
            if (bad) { zp("ZO c2e0 NOT PORTED (entry record)\n"); free(arr); continue; }
            int err = 0, r = cs_zw_pick(Lp, side, &W, m, ix, arr, &err);
            if (r == -101) zp("ZO c2e0 NOT PORTED (record)\n");
            else { zp("ZO c2e0 %d", r); for (int i = 0; i < m; i++) zp(" %d", arr[ix[i]].s2c); zp("\n"); }
            free(arr); (void)top;
        } else if (!strcmp(n, "80090")) {
            CsZbc Z; memset(&Z, 0, sizeof Z);
            int p1 = atoi(t[3]), fd, fr, k = zu_tok_rec(D, t + 6, &fd, &fr), j;
            if (k < 0) { zp("ZO 80090 NOT PORTED (record)\n"); continue; }
            Z.side = (short)atoi(t[4]); Z.ac = atoi(t[5]); Z.fdir = fd; Z.frec = fr; j = 6 + k;
            Z.f0 = (short)atoi(t[j]); Z.tol = strtod(t[j + 1], NULL);
            int p3 = atoi(t[j + 2]), dir = atoi(t[j + 3]), i = atoi(t[j + 4]), d7, r7, k2 = zu_tok_rec(D, t + j + 5, &d7, &r7);
            if (k2 < 0 || (r7 >= 0 && d7 != dir) || dir < 0 || dir > 1) { zp("ZO 80090 NOT PORTED (record)\n"); continue; }
            j += 5 + k2;
            short p8 = (short)atoi(t[j]); double d = strtod(t[j + 1], NULL); j += 3;
            CsZCand W, E;
            int k3 = j < nt && !strcmp(t[j], "|") ? zu_cand_in(D, t + j + 1, nt - j - 1, &W) : -1;
            int k4 = k3 < 0 || strcmp(t[j + 1 + k3], "|") ? -1 : zu_cand_in(D, t + j + 2 + k3, nt - j - 2 - k3, &E);
            if (k4 < 0) { zp("ZO 80090 NOT PORTED (entry record)\n"); continue; }
            int err = 0, r = cs_zw_score(Lp, &Z, p1, p3, dir, i, &W, r7, p8, d, &E, &err);
            if (r == -101) zp("ZO 80090 NOT PORTED (record / line / FUN_18017c1c0)\n"); else zp("ZO 80090 %d %d\n", r, E.b8);
        } else if (!strcmp(n, "79090")) {
            short side = (short)atoi(t[3]), max = (short)atoi(t[6]); double c2f0 = strtod(t[4], NULL); int c309 = atoi(t[5]), m = atoi(t[7]), j = 8, bad = 0;
            CsZCand W, *arr = calloc(0x3d, sizeof *arr), *ls[0x3d];
            if (m < 0 || m > 0x3c || j >= nt || strcmp(t[j], "|")) bad = 1;
            int k = bad ? -1 : zu_candx_in(D, t + j + 1, nt - j - 1, &W);
            if (k < 0) bad = 1; else j += 1 + k;
            for (int i = 0; i < m && !bad; i++) {
                if (j >= nt || strcmp(t[j], "|")) { bad = 1; break; }
                int m2 = zu_candx_in(D, t + j + 1, nt - j - 1, &arr[i]);
                if (m2 < 0) bad = 1; else j += 1 + m2;
                ls[i] = &arr[i];
            }
            if (bad) { zp("ZO 79090 NOT PORTED (entry record)\n"); free(arr); continue; }
            int err = 0, r = cs_zw_group2(Lp, side, c2f0, c309, max, &W, m, ls, &err);
            if (r == -101) zp("ZO 79090 NOT PORTED (record / line / ctx +0x309)\n");
            else { zp("ZO 79090 %d", r); zu_candx_out(D, &W); for (int i = 0; i < m; i++) zu_candx_out(D, &arr[i]); zp("\n"); }
            free(arr);
        } else if (!strcmp(n, "a7b0")) {
            int m = atoi(t[3]), j = 4, bad = 0;
            CsZCand W, *arr = calloc(0x3d, sizeof *arr), *ls[0x3d];
            if (m < 0 || m > 0x3c || j >= nt || strcmp(t[j], "|")) bad = 1;
            int k = bad ? -1 : zu_candx_in(D, t + j + 1, nt - j - 1, &W);
            if (k < 0) bad = 1; else j += 1 + k;
            for (int i = 0; i < m && !bad; i++) {
                if (j >= nt || strcmp(t[j], "|")) { bad = 1; break; }
                int m2 = zu_candx_in(D, t + j + 1, nt - j - 1, &arr[i]);
                if (m2 < 0) bad = 1; else j += 1 + m2;
                ls[i] = &arr[i];
            }
            if (bad) { zp("ZO a7b0 NOT PORTED (entry record)\n"); free(arr); continue; }
            short cnt = 0, out[64]; int err = 0;
            int r = cs_zw_group(Lp, &W, m, ls, &cnt, out, &err);
            if (r == -101) zp("ZO a7b0 NOT PORTED (record / line / table)\n");
            else {
                zp("ZO a7b0 %d %d", r, cnt); for (int i = 0; i < cnt; i++) zp(" %d", out[i]);
                zu_candx_out(D, &W); for (int i = 0; i < m; i++) zu_candx_out(D, &arr[i]); zp("\n");
            }
            free(arr);
        } else if (!strcmp(n, "bca00")) {
            /* FUN_1801bca00 whole (surf_classic3.md 12.47), on its own pool (the builder's pool is used only below FUN_1801bca00, so it
               follows the DLL's; the nested calls of the log are replayed on g_zpl as before) */
            int p1 = atoi(t[3]), p2 = atoi(t[4]), p3 = atoi(t[5]), mode = atoi(t[6]), i = atoi(t[7]), p7 = atoi(t[8]);
            double p8 = strtod(t[9], NULL); int h0 = atoi(t[10]), h1 = atoi(t[11]);
            CsZbc Z; memset(&Z, 0, sizeof Z);
            Z.side = (short)atoi(t[12]); Z.ac = atoi(t[13]);
            int fd, fr, k = zu_tok_rec(D, t + 14, &fd, &fr), j;
            if (k < 0) { zp("ZO bca00 NOT PORTED (record)\n"); continue; }
            j = 14 + k; Z.fdir = fd; Z.frec = fr; Z.f0 = (short)atoi(t[j]); Z.tol = strtod(t[j + 1], NULL);
            double c2f0 = strtod(t[j + 2], NULL), g = strtod(t[j + 4], NULL), t9250 = strtod(t[j + 5], NULL);
            int c309 = atoi(t[j + 3]), e100 = atoi(t[j + 6]); Z.last[0] = atoi(t[j + 7]); Z.last[1] = atoi(t[j + 8]);
            j += 9;
            short SL[8]; int bad = j + 9 > nt || strcmp(t[j], "|");
            for (int q = 0; q < 8 && !bad; q++) SL[q] = (short)atoi(t[j + 1 + q]);
            j += 9;
            int ix[4] = { i, i - 1, Z.last[0], Z.last[1] }, top = 0;
            for (int q = 0; q < 4; q++) if (ix[q] > top) top = ix[q];
            Z.st = calloc((size_t)top + 1, sizeof *Z.st);
            for (int q = 0; q < 4 && !bad; q++) {
                if (j + 1 >= nt || strcmp(t[j], "|")) { bad = 1; break; }
                if (atoi(t[j + 1]) < 0) { j += 2; continue; }
                CsZStep *s = &Z.st[ix[q]];
                s->b28 = (unsigned char)atoi(t[j + 2]);
                int k2 = zu_tok_rec(D, t + j + 3, &s->rdir, &s->rec);
                if (k2 < 0 || j + 3 + k2 + 4 > nt) { bad = 1; break; }
                s->e2a = (unsigned short)atoi(t[j + 3 + k2]);
                for (int e = 0; e < 3; e++) s->p[e] = strtod(t[j + 4 + k2 + e], NULL);
                j += 7 + k2;
            }
            if (bad || h0 != 0 || h1 != 1 || g != Lp->g || t9250 != Lp->t9250) { zp("ZO bca00 NOT PORTED (inputs)\n"); free(Z.st); continue; }
            CsZWOut o; int err = 0;
            int r = cs_zw_walk(Lp, &Z, &g_zplb, SL, p1, p2, p3, mode, i, p7, p8, c2f0, c309, &o, &err);
            free(Z.st);
            if (r == -101) { zp("ZO bca00 NOT PORTED\n"); continue; }
            char id[64]; zr_ident(D, o.rdir, o.rec, id, sizeof id);
            zp("ZO bca00 %s", id);
            if (o.found) zp(" %d %a %d %d %a %a %a", o.p10, o.p11, o.p12, o.p13, o.p14[0], o.p14[1], o.p14[2]);
            zp(" %d", o.e100 ? -16 : e100); zu_pool_out(&g_zplb); zp("\n");
        } else if (!strcmp(n, "d8d0")) {
            /* the zonbuf pool (surf_classic3.md 12.46): one pool for the whole builder, as the builder's local_9908 */
            cs_zw_pool_reset(&g_zpl);
            zp("ZO d8d0"); zu_pool_out(&g_zpl); zp("\n");
        } else if (!strcmp(n, "daa0")) {
            int mode = atoi(t[3]), m = atoi(t[4]), j = 18, bad = 0;
            short SL[8]; for (int i = 0; i < 8; i++) SL[i] = (short)atoi(t[5 + i]);
            CsZCand W, *arr = calloc(0x3d, sizeof *arr);
            if (m < 0 || m > 0x3c || j >= nt || strcmp(t[j], "|")) bad = 1;
            int k = bad ? -1 : zu_candx_in(D, t + j + 1, nt - j - 1, &W);
            if (k < 0) bad = 1; else j += 1 + k;
            for (int i = 0; i < m && !bad; i++) {
                if (j >= nt || strcmp(t[j], "|")) { bad = 1; break; }
                int m2 = zu_candx_in(D, t + j + 1, nt - j - 1, &arr[i]);
                if (m2 < 0) bad = 1; else j += 1 + m2;
            }
            if (bad) { zp("ZO daa0 NOT PORTED (entry record)\n"); free(arr); continue; }
            short o = -1;
            int r = cs_zw_tri(Lp, &g_zpl, SL, mode, &W, m, arr, &o);
            if (r == -101) zp("ZO daa0 NOT PORTED (record / line / free list)\n");
            else {
                zp("ZO daa0 %d %d %d", r, o, o >= 0 && o < m ? arr[o].b8 : -1); zu_pool_out(&g_zpl);
                for (int q = g_zpl.head; q >= 0; q = g_zpl.nd[q].next) {
                    const CsZNode *N = &g_zpl.nd[q];
                    zp(" |");
                    for (int e = 0; e < 2; e++) {
                        if (N->key[e].t == 1) { char id[64]; zr_ident(D, N->pr[e].rdir, N->pr[e].rec, id, sizeof id); zp(" 1 %s %lld", id, N->key[e].y); }
                        else zp(" %d %lld %lld", N->key[e].t, N->key[e].x, N->key[e].y);
                    }
                    zp(" %d %d", N->b8, N->t98 >= 0);
                    for (int i = 0; i < 6; i++) zp(" %a", N->pt[i / 3][i % 3]);
                }
                zp("\n");
            }
            free(arr);
        } else if (!strcmp(n, "bf450")) {
            short SL[8]; for (int i = 0; i < 8; i++) SL[i] = (short)atoi(t[3 + i]);
            CsZCand W; memset(&W, 0, sizeof W);
            int mode = atoi(t[11]), side = atoi(t[12]);
            for (int i = 0; i < 3; i++) W.p[i] = strtod(t[13 + i], NULL);
            W.end = (short)atoi(t[16]);
            int k = zu_tok_rec(D, t + 17, &W.rdir, &W.rec), j = 17 + (k < 0 ? 0 : k);
            if (k < 0 || j + 13 > nt) { zp("ZO bf450 NOT PORTED (walk record)\n"); continue; }
            double v[2] = { strtod(t[j], NULL), strtod(t[j + 1], NULL) }, b[4], zr[2];
            for (int i = 0; i < 4; i++) b[i] = strtod(t[j + 2 + i], NULL);
            zr[0] = strtod(t[j + 6], NULL); zr[1] = strtod(t[j + 7], NULL);
            int dir = atoi(t[j + 8]), h0 = atoi(t[j + 9]), h1 = atoi(t[j + 10]), max = atoi(t[j + 11]);
            short cnt = (short)atoi(t[j + 12]);
            j += 13;
            if (h0 != 0 || h1 != 1 || max < 0 || max > 4096 || cnt < 0 || cnt > max) { zp("ZO bf450 NOT PORTED (headers / sizes)\n"); continue; }
            CsZCand *arr = calloc((size_t)max + 1, sizeof *arr);
            int bad = 0;
            for (int i = 0; i < cnt && !bad; i++) {
                if (j >= nt || strcmp(t[j], "|")) { bad = 1; break; }
                int m = zu_cand_in(D, t + j + 1, nt - j - 1, &arr[i]);
                if (m < 0) bad = 1; else j += 1 + m;
            }
            if (bad) { zp("ZO bf450 NOT PORTED (entry record)\n"); free(arr); continue; }
            int r = cs_zw_cands(Lp, SL, mode, side, &W, v, b, zr, dir, max, &cnt, arr);
            if (r == -101) zp("ZO bf450 NOT PORTED (line past the headers)\n");
            else { zp("ZO bf450 %d %d", r, cnt); for (int i = 0; i < cnt; i++) zu_cand_out(D, &arr[i], 0); zp("\n"); }
            free(arr);
        }
    }
    zp_push();
    for (int i = 0; i < zp_n; i++) { fputs(zp_st[i], stdout); free(zp_st[i]); }   /* a log cut short: outputs in call order */
    zp_n = 0; g_zul = NULL;
    fclose(f);
}

static void d15_run(const CsEmCtx *X, CsSeg **SS, CsSegLine **HH, const int *MM, CsSegPts *P, int b2b0, double tol, const double tl[4])
{
    CsSegDir D[2];
    double a = fabs(X->stepA), b = fabs(X->stepB), gap = (a > b ? a : b) * 3.0;
    static CsFree FL;   /* the DLL's free list: emptied by FUN_1801b88c0 (asm 1801b26cd) before FUN_18015d0f0, then the FUN_1801b86c0 pool */
    free(FL.t); free(FL.pool); memset(&FL, 0, sizeof FL);
    for (int dir = 0; dir < 2; dir++) {
        D[dir].S = SS[dir]; D[dir].n = MM[dir]; D[dir].cap = MM[dir]; D[dir].H = HH[dir]; D[dir].nl = dir ? X->nB : X->nA; D[dir].P = P[dir];
        D[dir].dir = dir; D[dir].fl = &FL; FL.occ[dir][0] = dir; FL.occ[dir][1] = 0; FL.n0[dir] = MM[dir]; FL.S[dir] = &D[dir].S;
    }
    for (int dir = 0; dir < 2; dir++) {
        if (D[dir].nl < 1) continue;
        short err = 0;
        int c = cs_d15_split(&D[dir], b2b0, &err);
        printf("QS %d %d %d\n", dir, c, err);
        if (err) break;
        printf("QM %d %d\n", dir, cs_d15_merge(&D[dir], gap));
    }
    /* D15_FULL=1 (surf_classic3.md 12.32): FUN_18015d0f0's loop after its first part (l.232062-232074, asm 18015d0f0): when both directions
       have lines, FUN_180149a20 (cs_d15_9a20) then FUN_18014d5c0(0) / (1) while either merges; "QL round ret abort mergesA mergesB" as
       tools/peloader --lmm D15 prints. ctx +0x2b8 = DAT_1804e9238 (FUN_18018f780, asm 18018f7c0), DAT_1804e9250, the z limits of
       FUN_18014a020 = CFB_ZL "flag z1 z2" (op +0x1280 / +0x1288 / +0x1290, asm 180206cb2..180206cd7) */
    if (getenv("D15_FULL") && X->nA > 0 && X->nB > 0) {
        CsLoop Lp; memset(&Lp, 0, sizeof Lp);
        Lp.D[0] = &D[0]; Lp.D[1] = &D[1]; Lp.X = X; Lp.tol = tol; Lp.g = tl[0]; Lp.t9250 = tl[3]; Lp.gap = gap;
        if (getenv("CFB_ZL")) sscanf(getenv("CFB_ZL"), "%d %la %la", &Lp.zl, &Lp.z1, &Lp.z2);
        Lp.f30a = 1;   /* FUN_1801b1ad0 asm 1801b2a9f: DAT_1804d569a = ctx +0x30a = 1 before FUN_18015d0f0 (kind 0x11) */
        printf("QZ %d %a %a %a %a\n", Lp.zl, Lp.z1, Lp.z2, Lp.g, Lp.t9250);
        for (int round = 1; ; round++) {
            int np = 0, r = cs_d15_9a20(&Lp, &np), m0 = 0, m1 = 0;
            if (!r) { m0 = cs_d15_merge(&D[0], gap); m1 = cs_d15_merge(&D[1], gap); }
            printf("QL %d %d %d %d %d\n", round, r, 0, m0, m1);
            if (r == -100) printf("D15 FUN_180149a20: a record without its in-memory points (FUN_1801a7fd0's windowed arm): NOT PORTED\n");
            if (r || (!m0 && !m1)) break;
        }
        if (getenv("D15_COV")) { printf("QC"); for (int i = 0; i < 30; i++) printf(" %ld", Lp.cov[i]); printf("\n"); }
        /* D15_6E60=1 (surf_classic3.md 12.34): FUN_18015d0f0's last calls FUN_180146e60(0, ctx, abort) and (1) (asm 18015d1bf / 18015d1db)
           after the loop; "QE dir err ctx+0x30a" as tools/peloader --lmm D15 prints (ctx +0x30a is 0 at this point) */
        if (getenv("D15_6E60")) {
            long c6[80]; memset(c6, 0, sizeof c6);
            for (int dir = 0; dir < 2; dir++) {
                int e = cs_d15_6e60(&Lp, dir, c6);
                printf("QE %d %d %d\n", dir, e, Lp.f30a);
                if (e == -100) printf("D15 FUN_180146e60: a record without its in-memory points (FUN_1801a7fd0's windowed arm): NOT PORTED\n");
                if (e == -101) printf("D15 FUN_180146e60: line header n's chain runs through freed memory: NOT PORTED\n");
                if (e) break;
                if (dir == 1) printf("QV %a\n", cs_d15_c8f0(&Lp));   /* FUN_18015c8f0 (FUN_1801b1ad0 asm 1801b2ad5) */
            }
            if (getenv("D15_COV")) { printf("QF"); for (int i = 40; i < 72; i++) printf(" %ld", c6[i]); printf("\n"); }
            if (getenv("D15_ZU")) zu_port(D, Lp.t9250, atoi(getenv("D15_ZU")));
            if (getenv("D15_ZREP")) zrep_port(D, tol, tl, getenv("D15_ZREP"));
            if (getenv("D15_ZUNIT")) zunit_port(&Lp, D, getenv("D15_ZUNIT"));
        }
    }
    for (int dir = 0; dir < 2; dir++) { d15_print(dir, &D[dir]); SS[dir] = D[dir].S; }
}
static int segs_main(int argc, char **argv)
{
    int d15 = !strcmp(argv[1], "--d15"), pts = d15 || !strcmp(argv[1], "--pts");
    if (argc < 6 + d15) { fprintf(stderr, "usage: cfb_port --segs|--pts EM TOL P6 P7 | --d15 EM TOL P6 P7 B2B0\n"); return 1; }
    size_t len; unsigned char *f = slurp(argv[2], &len); if (!f) return 2;
    double tol = strtod(argv[3], NULL), p6 = strtod(argv[4], NULL), p7 = strtod(argv[5], NULL);
    CsEmCtx X; double tl[4]; const char *why = "";
    if (cs_em_ctx(f, len, &X, tl, &why)) { printf("SEGS NOT PORTED: %s\n", why); return 3; }
    CsSeg *SS[2] = { 0 }; CsSegLine *HH[2] = { 0 }; int RR[2] = { 0 }, MM[2] = { 0 };
    for (int dir = 0; dir < 2; dir++) {
        int n = dir ? X.nB : X.nA, p9 = 2 * n, ret = 0, n25 = 0; short err = 0;
        CsSeg *S = NULL; CsSegLine *H = calloc((size_t)n + 1, sizeof *H);
        for (int pass = 0; pass < 2; pass++) {
            free(S); S = calloc((size_t)p9 + 1, sizeof *S);
            memset(H, 0, sizeof *H * ((size_t)n + 1));
            ret = cs_seg_dir(&X, tol, dir, 1, 2, p6, p7, p9, S, H, &n25, &err);
            if (ret <= p9 || err) break;
            p9 = ret;
        }
        printf("SG %d %d %d %d\n", dir, ret, n25, err);
        int m = ret < p9 ? ret : p9;
        segs_print(dir, n, m, S, H, "SH", "SR");
        SS[dir] = S; HH[dir] = H; RR[dir] = ret; MM[dir] = m;
    }
    if (pts) {   /* FUN_1801b1ad0 asm 1801b23fc..1801b241f: both directions need segments, else no zones (-0xd or the caller's 0) */
        CsSegPts P[2];
        memset(P, 0, sizeof P);
        if (RR[0] < 1 || RR[1] < 1) printf("PTS no segments %d %d\n", RR[0], RR[1]);
        else {
            int ok = 1;
            for (int dir = 0; dir < 2 && ok; dir++) {
                short mx; int tot;
                cs_seg_sizes(&X, dir, &mx, &tot);
                if (cs_seg_pts_alloc(&P[dir], tot)) { printf("PTS alloc failed\n"); ok = 0; break; }
                int r = cs_seg_points(&X, RR[dir], SS[dir], &P[dir]);
                printf("PZ %d %d %d %d\n", dir, mx, tot, r);
                if (!r) ok = 0;
            }
            if (ok) {
                cs_seg_flip(X.sB, HH[1], X.nB, SS[1], &P[1]);
                for (int dir = 0; dir < 2; dir++) {
                    segs_print(dir, dir ? X.nB : X.nA, MM[dir], SS[dir], HH[dir], "PH", "PR");
                    uint64_t h = 1469598103934665603ull;
                    h = fnv(h, P[dir].pt, 16 * (size_t)P[dir].n4); h = fnv(h, P[dir].cd, 2 * (size_t)P[dir].n5); h = fnv(h, P[dir].fl, (size_t)P[dir].n6);
                    printf("PA %d %d %016llx\n", dir, P[dir].n4, (unsigned long long)h);
                }
                if (d15) d15_run(&X, SS, HH, MM, P, atoi(argv[6]), tol, tl);
            }
            cs_seg_pts_free(&P[0]); cs_seg_pts_free(&P[1]);
        }
    }
    for (int d = 0; d < 2; d++) { free(SS[d]); free(HH[d]); }
    free(f);
    return 0;
}
/* cfb_port --vcs KIND PRE VCS OUT: FUN_1801c9350 alone (cs_rgn_vcs_copy) on the recut file PRE and the wall file VCS (the scanner's
   plmvcs00.tmp, as FUN_1801868a0 loads it): OUT = PRE + the copied records; prints "RGNV" / "RGNV98" as --rgn (tools/tests/rgn_vcs_sample.sh) */
static int vcs_main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage: cfb_port --vcs KIND PRE VCS OUT\n"); return 1; }
    int kind = atoi(argv[2]);
    size_t pl, vl; unsigned char *pre = slurp(argv[3], &pl), *v = slurp(argv[4], &vl);
    if (!pre || !v || vl < 4) return 2;
    int n = ri4(v); if (n < 0 || vl < 4 + 8 * (size_t)n) return 2;
    unsigned char *t98 = malloc(8 * (size_t)n + 8); memcpy(t98, v + 4, 8 * (size_t)n);
    short a4 = 0;
    for (int i = 0; i < n; i++) { short c; memcpy(&c, t98 + 8 * i + 4, 2); if (a4 < c) a4 = c; }
    CsBuf out = { 0 }; out.cap = pl + 4096; out.p = malloc(out.cap); memcpy(out.p, pre, pl); out.n = pl;
    int e = cs_rgn_vcs_copy(&out, v, vl, n, t98, &a4);
    if (e) { printf("RGNV %d NOT PORTED %d\n", kind, e); return 3; }
    printf("RGNV %d ret 0 a4 %d 9790 %zu\n", kind, a4, out.n);
    printf("RGNV98 %d", kind); for (int i = 0; i < n; i++) printf(" %d:%d", ri4(t98 + 8 * i), (int)(short)(t98[8 * i + 4] | t98[8 * i + 5] << 8)); printf("\n");
    spit(argv[5], "", &out, out.n);
    return 0;
}

/* cfb_port --wspl FILE: tools/tests/wall_synth.py cases through cs_wall_split (FUN_18015bca0) and cs_wall_connector (FUN_1801c8080):
   "WS i r" + r node lines, "WC i r" + one node line when r > -1, "WV i codes-hash" (the walls' codes after: restored);
   tools/peloader --lmm WSPL prints the same from the DLL's functions (tools/tests/wall_cmp.sh) */
static void wnode(const CsNode *o)
{
    printf("N %d %d %a %a %a %a %a %a %a %a %a %a %d\n", o->f0, o->f1, o->s0, o->z0, o->n0[0], o->n0[1], o->n0[2], o->s1, o->z1,
           o->n1[0], o->n1[1], o->n1[2], o->code);
}
static int wspl_main(const char *fn)
{
    FILE *f = fopen(fn, "r"); if (!f) { perror(fn); return 2; }
    char line[1024]; int ci = 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "W ", 2)) continue;
        CsCfbConst C; memset(&C, 0, sizeof C); double d, pts[4]; int n;
        if (sscanf(line + 2, "%la %la %la %la %la %la %la %la %la %d", &C.t9238, &C.t9250, &C.t9258, &C.systol, &d, pts, pts + 1, pts + 2, pts + 3, &n) != 10) break;
        CsNode *v = calloc((size_t)n + 1, sizeof *v), *A = calloc((size_t)n + 5, sizeof *A), *B = calloc((size_t)n + 5, sizeof *B), *W = calloc((size_t)n + 5, sizeof *W);
        CsNode out[64]; memset(out, 0, sizeof out);
        for (int i = 0; i < n; i++) {
            if (!fgets(line, sizeof line, f) || strncmp(line, "R ", 2)) return 2;
            unsigned char r[0x68]; for (int k = 0; k < 0x68; k++) { unsigned x; sscanf(line + 2 + 2 * k, "%2x", &x); r[k] = (unsigned char)x; }
            CsNode *q = &v[i]; q->f0 = r[0]; memcpy(&q->s0, r + 8, 8); memcpy(&q->z0, r + 0x10, 8); memcpy(q->n0, r + 0x18, 24);
            q->f1 = r[0x30]; memcpy(&q->s1, r + 0x38, 8); memcpy(&q->z1, r + 0x40, 8); memcpy(q->n1, r + 0x48, 24); memcpy(&q->code, r + 0x60, 2);
        }
        short rs = cs_wall_split(pts, d, n, v, C.systol, A, B, W, out);
        printf("WS %d %d\n", ci, rs);
        for (int i = 0; i < rs; i++) wnode(&out[i]);
        CsNode o; memset(&o, 0, sizeof o);
        short rc = cs_wall_connector(pts, n, v, &C, &o);
        printf("WC %d %d\n", ci, rc);
        if (rc > -1) wnode(&o);
        unsigned long long h = 1469598103934665603ull;
        for (int i = 0; i < n; i++) { unsigned short c = (unsigned short)v[i].code; h ^= c & 0xff; h *= 1099511628211ull; h ^= c >> 8; h *= 1099511628211ull; }
        printf("WV %d %016llx\n", ci, h);
        free(v); free(A); free(B); free(W); ci++;
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "--wspl")) return wspl_main(argv[2]);
    if (argc > 1 && !strcmp(argv[1], "--rgn")) return rgn_main(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--vcs")) return vcs_main(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--emesh")) return emesh_main(argc, argv);
    if (argc > 1 && (!strcmp(argv[1], "--segs") || !strcmp(argv[1], "--pts") || !strcmp(argv[1], "--d15"))) return segs_main(argc, argv);
    if (argc < 6) { fprintf(stderr, "usage: cfb_port SCANFILE KIND CHORD SYSTOL CDEV\n"); return 1; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)len + 1);
    if (fread(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); return 2; }
    fclose(f);
    int kind = atoi(argv[2]);
    CsCfbConst C; cs_cfb_consts(kind, strtod(argv[3], NULL), strtod(argv[4], NULL), strtod(argv[5], NULL), &C);
    printf("CONST %a %a %a %a %a\n", C.t9238, C.t9240, C.t9248, C.t9250, C.t9258);
    int *t = NULL, n = cs_cfb_table(b, (size_t)len, &t);
    printf("CFB %d\n", n);
    int cap = 500;   /* FUN_1801ad6c0(1, 500, local_3408) l.309956 */
    /* FUN_1801cfb60's per-line arm for kinds 6 / 7 (l.311127-311170; local_364a = 0, param_18 = 1, local_3671 = 1, local_3648 = 0):
       FUN_180183310 (the segments into the line list by FUN_180182e70; the store 0x7fff nodes, N13-28), then when the list is not empty
       FUN_1801974e0, FUN_1801b0c80 (CFB_VCS = the line's wall table: the scanner's plmvcs00.tmp, int n + n x {int offset, short count}),
       FUN_180184cc0(0, flags, nvcs, ..) into the node array (cap 0x7fff), FUN_1801a5510, FUN_180182620, FUN_180181a80(0, CFB_GAP = the op's
       msurf_max_short_gap, ..), FUN_180181680. Prints "EST line n fnv1a64 freehead nfree err" (the list after FUN_1801974e0, est_print), "AR line n
       fnv1a64" (the array), "A2 line 0 n codes" / "A3 line count n codes" (after FUN_180182620 / FUN_180181a80), "RL line n fnv1a64 freehead
       nfree err" (the list rebuilt by FUN_180181680). CFB_ARR = FILE: each array (int line, int n, n x 0x68 bytes) for the CFBA unit mode.
       NOT PORTED after that: FUN_18018fd80 (endpoint flags) and FUN_180182de0 -> FUN_1801a1a70 (the temp.rgn block). */
    const char *arrf = getenv("CFB_ARR"); FILE *af = arrf ? fopen(arrf, "wb") : NULL;
    double gap = getenv("CFB_GAP") ? strtod(getenv("CFB_GAP"), NULL) : 0.0;
    int nvt = 0; unsigned char *vb = NULL; size_t vbl = 0; CsNode *pool = calloc(501, sizeof(CsNode));
    if (getenv("CFB_VCS") && *getenv("CFB_VCS")) {
        vb = slurp(getenv("CFB_VCS"), &vbl);
        if (!vb || vbl < 4) { fprintf(stderr, "no CFB_VCS\n"); return 2; }
        nvt = ri4(vb); if (vbl < 4 + 8 * (size_t)nvt) return 2;
    }
    CsEst L; if (cs_est_init_cfb(&L, 0x7fff)) return 2;
    CsNode *arr = calloc(0x8000, sizeof(CsNode));
    fd_arr = calloc(2 * (0x7fff + 10), sizeof(CsFdNode)); fd_list = calloc(0x7fff + 10, sizeof(short)); fd_mark = calloc(2 * (0x7fff + 10), 1);
    cs_fd_trace = fd_trace;
    if (getenv("CFB_RGN")) { rg_tab = malloc(sizeof(int) * (size_t)(n + 1)); for (int q = 0; q <= n; q++) rg_tab[q] = -1; }
    for (int i = 0; i < n; i++) {
        if (t[i] == -1) continue;
        Ctx c = { i, &C, &L }; double p8 = 0;
        int r = cs_cfb_walk(b, (size_t)len, t[i], seg, &c, &p8, &cap);
        printf("LN %d %d %a\n", i, r, p8);
        if (r) break;
        if (L.head != -1) {
            if (getenv("CFB_DUMP")) est_print("PRE", i, &L);
            {   /* a list that loops (as the DLL's, on cfb_synth.py inputs): "CYCLE i", the store re-initialised (FUN_1801a5570), as CFBL does */
                int c = 0;
                for (short k = L.head; k != -1 && c <= 0x7fff; k = L.nd[k].next) c++;
                if (c > 0x7fff) { printf("CYCLE %d\n", i); cs_est_reset(&L); continue; }
            }
            if (getenv("CFB_DUMP") && atoi(getenv("CFB_DUMP")) == 2) {   /* FUN_1801d7da0 alone on a copy ("D7"), as CFBL */
                CsEst K = L; K.nd = malloc(sizeof(CsNode) * (size_t)(L.cap + 1)); memcpy(K.nd, L.nd, sizeof(CsNode) * (size_t)(L.cap + 1));
                cs_cfb_clean_short(&K, &C); est_print("D7", i, &K);
                CsScan V; memset(&V, 0, sizeof V); V.est = K.nd; V.ehead = K.head; V.half = C.t9240; V.ctol = C.systol; V.trace = 1; V.trace_line = i;
                cs_clean_runs(&V);
                est_print("TR", i, &K);
                free(K.nd);
            }
            cs_cfb_clean(&L, &C);
            est_print("EST", i, &L);
            /* FUN_1801b0c80 (CFB_VCS: the line's walls, cs_wall_read) and FUN_180184cc0 with them (cs_cfb_to_array_v); "WL i nv" */
            double lp = 0; int nv = vb && i < nvt ? cs_wall_read(vb, vbl, vb + 4, i, pool, &lp) : 0;
            if (nv < 0) { printf("NOT PORTED: line %d has more than 500 wall records\n", i); cs_est_free_line(&L); continue; }
            if (nv > 0) printf("WL %d %d\n", i, nv);
            int na = cs_cfb_to_array_v(&L, &C, 1, nv, pool, 0x7fff, arr);
            if (na < 0) { printf("ERR %d to_array %d\n", i, na); break; }
            uint64_t h = 1469598103934665603ull;
            if (af) { fwrite(&i, 4, 1, af); fwrite(&na, 4, 1, af); }
            for (int j = 0; j < na; j++) {
                unsigned char nb[0x68]; cs_node_bytes(arr + j, nb); if (af) fwrite(nb, 0x68, 1, af);
                for (int q = 0; q < 0x68; q++) { h ^= nb[q]; h *= 1099511628211ull; }
            }
            printf("AR %d %d %016llx\n", i, na, (unsigned long long)h);
            cs_est_free_line(&L);
            cs_conn_codes(na, arr); codes("A2", i, 0, na, arr);
            int k = cs_join_runs(0, gap, na, arr); codes("A3", i, k, na, arr);
            cs_cfb_from_array(&L, &C, na, arr);
            est_print("RL", i, &L);
            if (L.err) { printf("ERR %d estore %d\n", i, L.err); break; }
            /* FUN_18018fd80 (cam3d_cscallop_fd80.c; pos = the line position the walk returned, p8: a unit-check value, the DLL's is
               DAT_1804e92b8). Prints "FP i pits dir" + "FS" (the list), "FA i n nruns hash", "FB i hash", "FC i k hash list..", "FD i n3
               spread" + "FL" (the list with its flags) - tools/peloader/mcmsurf_lmm.c CFBL prints the same from the DLL's functions */
            g_line = i;
            CsFdResult R; cs_fd_endflags(&L, &C, p8, 0x2b, 0.0, fd_arr, fd_list, fd_mark, &R);
            /* FUN_180182de0(2, ..) -> FUN_1801a1a70 (CFB_RGN: the blocks from offset 0, the per-line table starting at -1 - unit-check
               values, as CFBL; FUN_1801cfb60's own start / table are not checked yet) */
            if (rg_tab) { int r = cs_rgn_block(&L, &C, i, p8, &rg_off, rg_tab, &rg_out); printf("RG %d %d %d\n", i, r, rg_off); }
        }
        cs_est_free_line(&L);
    }
    if (af) fclose(af);
    if (rg_tab) { FILE *w = fopen(getenv("CFB_RGN"), "wb"); if (w) { fwrite(rg_out.p, 1, rg_out.n, w); fclose(w); } }
    { extern long cs_wall_cov[4]; if (cs_wall_cov[0] + cs_wall_cov[1] + cs_wall_cov[2]) printf("WALLCOV split %ld (%ld nodes) connector %ld straight %ld\n", cs_wall_cov[0], cs_wall_cov[3], cs_wall_cov[1], cs_wall_cov[2]); }
    if (getenv("CFB_CODES"))   /* the code families over -3 .. 0x45 (CFBA mode prints the DLL's) */
        for (int c = -3; c <= 0x45; c++)
            printf("CODE %d %d %d %d %d %d %d %d %d\n", c, cs_code_vert(c), cs_code_afd0(c), cs_code_a310(c), cs_code_f3f30(c), cs_code_bc40(c),
                   cs_code_af60(c), cs_code_b0e0(c), cs_code_f6c0(c));
    return 0;
}
