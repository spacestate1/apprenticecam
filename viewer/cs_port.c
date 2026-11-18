/* cs_port CASE OUTDIR > RESULTS: the Constant Scallop scanner port (cam3d_cscallop.c, formats/surf_classic3.md 12.12) on a
 * tools/msm_case.py case plus one line
 *   "CS tol p3 p4 p5 corner_dev systol has_n side nlines"   (%a or decimal; the scanner's arguments, a unit-test input)
 * Per MSM line: plm_model -> mm_fill (the frame of the MSM line; one fill block, N13-28) -> cs_phase_a (mode = mms +0x64). Prints
 *   "PA k mode np nt groups_before groups_after", per group "G xmin xmax off n kind" (%a), "TF nbytes fnv1a64".
 * Then the groups sorted (cs_sort_groups) and nlines lines spread over the groups' x range (a test input, not Mastercam's line placement:
 * pos_0 = xmin + step / 2, step = (xmax - xmin) / nlines, pos / lo / hi accumulated by += step as the scanner does, l.326590):
 *   "LINE i pos lo hi" (%a) and per piece in call order "PC" + the 0x58 bytes (hex; +2..+7 zero).
 * Writes OUTDIR/ID2371.tmp (the triangle file) and OUTDIR/scn.case (the DLL unit case of tools/peloader/mcmsurf_lmm.c SCN mode) so the
 * DLL's FUN_1801daa60 / FUN_1801dba80 run on the same input: tools/tests/scn_cmp.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include "mcam_load.h"
#include "mcpartline.h"
#include "cam3d_mmodel.h"
#include "cam3d_cscallop.h"

const McSysTol *mc_systol_units(int metric) { (void)metric; return &mc_systol; }
int occt_mesh_surf() { return -1; }
int part_tools_apply() { return -1; }
int part_tools_list() { return -1; }

#define MAXR 20000
static Surf g_s[MAXR];

/* stage C part 1 (CS_S1DIR, surf_classic3.md 12.16): the machine-model files of the case's surfaces as FUN_180229ba0 leaves them (records,
   tmpedg0 / tmpcrn0 in file order), for cs_s1_run */
static unsigned char *s1_rec, *s1_edg, *s1_crn; static size_t s1_nrec, s1_nedg, s1_ncrn, s1_mm1; static int64_t s1_ep[MAXR];
static void s1_add(unsigned char **b, size_t *n, const void *p, size_t k) { *b = realloc(*b, *n + k + 1); memcpy(*b + *n, p, k); *n += k; }
static uint64_t fnv(const void *p, size_t n) { const unsigned char *b = p; uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }

/* ---- stage C part 1: "OP hex" (the op record, tools/lmm_case.py) + "S1 nc_tol" in the case (surf_classic3.md 12.16) ----
   Pass 1 builds every MSM surface's model and its FUN_180229ba0 files (s1_rec / s1_edg / s1_crn); s1_stage1 then runs FUN_180133f10's
   frame / step / FUN_18025dd50 / FUN_18012f9c0 / line count for modes 6 and 5 on them (CS_S1DIR: the input files in DIR/in, the port's
   files after each mode in DIR/port6, DIR/port5); pass 2 runs the scanner (kind 7 = mode 6) with those real arguments: tol = msurf_chord_dev,
   p3 / p4 / p5 = stock / r / D, the corner deviation, the frame rows -y / x / z (DAT_1804ea500 / 518 / 530, l.326195-326203) with the
   rotation flag DAT_1804ea4f2 (l.326204), the edges / corners as FUN_180261f40 left them, the lines of FUN_180133f10. */
static unsigned char *g_op, *g_tl; static size_t g_opn, g_tln;   /* the OP_ID record, the TL_ID record (tools/lmm_case.py) */
static void wfile(const char *dir, const char *sub, const char *name, const void *p, size_t n)
{
    char d[4200], fn[4400]; snprintf(d, sizeof d, "%s/%s", dir, sub); mkdir(dir, 0755); mkdir(d, 0755);
    snprintf(fn, sizeof fn, "%s/%s", d, name); FILE *w = fopen(fn, "wb"); if (w) { if (n) fwrite(p, 1, n, w); fclose(w); }
}
static void s1_dump(const char *dir, const char *sub, const CsMM *mm, int nsurf)
{
    wfile(dir, sub, "tmpmm.tmp", mm->rec, (size_t)mm->nrec * 0x78);
    wfile(dir, sub, "tmpmm3.tmp", mm->mm3, mm->n3);
    wfile(dir, sub, "tmpedg0.tmp", mm->edg, mm->nedg);
    wfile(dir, sub, "tmpcrn0.tmp", mm->crn, mm->ncrn);
    /* tmpmm2.tmp (l.364508-364529, 364783, 364916): the name line, the record count, the list head (-1: the sorted-by-xmin list of
       l.364800-364895 NOT PORTED, not read by stage 1), the 0x20 entries */
    size_t n2 = 5 + 8 + (size_t)nsurf * 0x20; unsigned char *b = calloc(1, n2);
    memcpy(b, "part\n", 5); memcpy(b + 5, &mm->cnt2, 4); int m1 = -1; memcpy(b + 9, &m1, 4);
    size_t k = 6; while (mm->mm3[k] != '\n') k++;
    memcpy(b + 13, mm->mm3 + k + 2 + 0x96 + 0x48, (size_t)nsurf * 0x20);
    wfile(dir, sub, "tmpmm2.tmp", b, n2); free(b);
}
static void prow(const char *t, const double *v) { printf(" %s %a %a %a", t, v[0], v[1], v[2]); }
/* 0 and the scanner's real arguments for mode 6 (kind 7) */
static int s1_stage1(CsMM *mm, int nsurf, double nctol, CsOp43 *P, CsFrame *F6, int *n6, double *first6, double *step6, unsigned char **edg6, unsigned char **crn6)
{
    const char *dir = getenv("CS_S1DIR");
    static const double V[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };   /* the Top view (MCView +8..+0x48), as the DLL harness's Mastercam.dll glue */
    if (!g_op || g_opn < 0x68 + 0x28d0) { printf("S1 refused: no OP line\n"); return -1; }
    cs_op43_params(g_op + 0x68, nctol, P);
    printf("S1PARAM chord %a cdev %a slice %a angle %a pct %a stock %a D %a r %a\n", P->chord, P->cdev, P->slice, P->cut_angle, P->pct, P->stock, P->D, P->r);
    printf("S1GAP %a\n", P->gap);   /* msurf_max_short_gap (cs_op43_params) = the S1 oracle's CFB "gap" (FUN_1801cfb60 param_22) */
    /* tmpmm3 (FUN_180229ba0 l.364107-364178): +0 0x1c, +2 the entry count, the name line, the byte 0 (l.364133), FUN_180224290() = 0x96
       bytes (chord .. the max-stock / 0x84 flags: not read by FUN_18025dd50 / FUN_180261f40, written as 0 here - NOT PORTED), the rows
       (cs_mm_frame), the 0x20 entries (+0 the surface's eptr, +0x14 1 = drive surface; the rest is not read by stage 1: 0) */
    double a[3], b[3], c[3], z17[3] = { V[6], V[7], V[8] };
    if (cs_mm_frame(V, z17, a, b, c)) { printf("S1 refused cs_mm_frame\n"); return -1; }
    mm->n3 = 2 + 4 + 5 + 1 + 0x96 + 0x48 + (size_t)nsurf * 0x20; mm->mm3 = calloc(1, mm->n3);
    short h = 0x1c; memcpy(mm->mm3, &h, 2); memcpy(mm->mm3 + 2, &nsurf, 4); memcpy(mm->mm3 + 6, "part\n", 5);
    unsigned char *r = mm->mm3 + 6 + 5 + 1 + 0x96;
    memcpy(r, a, 24); memcpy(r + 24, b, 24); memcpy(r + 48, c, 24);
    for (int i = 0; i < nsurf; i++) { unsigned char *e = r + 0x48 + 0x20 * (size_t)i; memcpy(e, &s1_ep[i], 8); short one = 1; memcpy(e + 0x14, &one, 2); }
    mm->cnt2 = mm->nrec;
    if (dir) s1_dump(dir, "in", mm, nsurf);
    for (int mode = 6; mode >= 5; mode--) {
        CsFrame Fr; const char *why = NULL; int cnt = 0;
        cs_s1_frame(mode, P->cut_angle, V, &Fr);
        double slice = P->slice, pct = P->pct, cd = 0;
        double step = cs_s1_step(2, 0x2b, g_op + 0x68, P->D, P->chord, &slice, &pct, &cd);
        double p1[3] = { -Fr.y[0], -Fr.y[1], -Fr.y[2] };
        if (cs_mm_reframe(mm, p1, Fr.x, Fr.z, V, &cnt, &why)) { printf("S1 %d refused %s\n", mode, why); return -1; }
        if (cnt == 0) { printf("S1 %d error -0xb (no model count)\n", mode); return -1; }
        CsExt E;
        if (cs_extents(mm, P->D, P->stock, cnt, P->chord, &E)) { printf("S1 %d error -0xb (FUN_18012f9c0 read)\n", mode); return -1; }
        int n; double first, st;
        cs_s1_lines(step, &E, &n, &first, &st);
        printf("S1 %d kind %d pct %a turned %d ang %a count %d", mode, mode == 6 ? 7 : 6, pct, mm->turned, mm->ang, cnt);
        prow("x", Fr.x); prow("z", Fr.z); prow("y", Fr.y);
        printf(" ext %a %a %a %a %d %d n %d first %a step %a\n", E.lo4, E.hi5, E.lo6, E.hi7, E.f8, E.f9, n, first, st);
        if (dir) s1_dump(dir, mode == 6 ? "port6" : "port5", mm, nsurf);
        if (mode == 6 && getenv("CS_S1MODE") && atoi(getenv("CS_S1MODE")) == 5) {
            /* the kind 7 scanner between the two calls sorts each surface's tmpedg0 / tmpcrn0 records in place (FUN_1801e7e20 l.324362-324395,
               SysFile::Write back): the same sort here (cs_edge_groups on a scratch scanner), so mode 5 and the kind 6 scanner see the DLL's
               file order. (The S1 DLL harness runs no scanner between the calls: with CS_S1MODE=5 s1_cmp's mode 5 file compare does not apply.) */
            for (int i = 0; i < mm->nrec; i++) {
                const unsigned char *rc = mm->rec + 0x78 * (size_t)i; int eo, co, ne;
                memcpy(&eo, rc + 0x3c, 4); memcpy(&co, rc + 0x38, 4); memcpy(&ne, rc + 0x54, 4);
                if (ne <= 0) continue;
                CsScan T; memset(&T, 0, sizeof T);
                cs_edge_groups(&T, 1, ne, mm->edg + eo, mm->crn + co);
                memcpy(mm->edg + eo, T.edg, (size_t)ne * 0xb0); memcpy(mm->crn + co, T.crn, (size_t)ne * 0x70);
                cs_free(&T);
            }
        }
        if (mode == (getenv("CS_S1MODE") ? atoi(getenv("CS_S1MODE")) : 6)) {   /* CS_S1MODE=5: the scanner of the second call (kind 6) */
            *F6 = Fr; *n6 = n; *first6 = first; *step6 = st;
            *edg6 = malloc(mm->nedg + 1); memcpy(*edg6, mm->edg, mm->nedg); *crn6 = malloc(mm->ncrn + 1); memcpy(*crn6, mm->crn, mm->ncrn);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: cs_port CASE OUTDIR\n"); return 2; }
    FILE *f = fopen(argv[1], "r"); if (!f) { perror(argv[1]); return 2; }
    mkdir(argv[2], 0755);
    McamFile F; memset(&F, 0, sizeof F);
    int ns = 0, have = 0, hasn = 1, side = 1, nlines = 50, npa = 0;
    double tol = 0, p3 = 0, p4 = 0, p5 = 0, cdev = 0, systol = 0;
    CsScan S; memset(&S, 0, sizeof S);
    static char line[1 << 24];
    int s1 = 0, s1j = 0, n6 = 0, F6rot = 0; double s1_nctol = 0, first6 = 0, step6 = 0, F6r[9]; CsMM s1mm; memset(&s1mm, 0, sizeof s1mm);
    CsOp43 P1; CsFrame F6; unsigned char *edg6 = NULL, *crn6 = NULL;
    while (fgets(line, sizeof line, f)) if (!strncmp(line, "S1 ", 3)) s1 = 1;
    rewind(f);
    for (int pass = 0; pass < 1 + s1; pass++) {
    if (pass == 1) {   /* stage 1 (modes 6 then 5), then the scanner of mode 6 with its real arguments */
        if (!have) { fprintf(stderr, "cs_port: CS line first\n"); return 2; }
        s1mm.rec = s1_rec; s1mm.nrec = (int)(s1_nrec / 0x78); s1mm.edg = s1_edg; s1mm.nedg = s1_nedg; s1mm.crn = s1_crn; s1mm.ncrn = s1_ncrn;
        if (s1_stage1(&s1mm, s1mm.nrec, s1_nctol, &P1, &F6, &n6, &first6, &step6, &edg6, &crn6)) return 0;
        /* the scanner's frame rows (l.326195-326203) and DAT_1804ea4f2 (l.326204) */
        for (int i = 0; i < 3; i++) { F6r[i] = -F6.y[i]; F6r[3 + i] = F6.x[i]; F6r[6 + i] = F6.z[i]; }
        F6rot = F6r[4] < 0.999999999999 || F6r[0] < 0.999999999999;
        cs_free(&S);
        int r = cs_setup(&S, P1.chord, P1.stock, P1.r, P1.D, P1.cdev, systol);   /* FUN_1801cfb60 -> FUN_1801ea7e0 (12.15) */
        printf("CSSETUP %d off %a hoff %a e %a (real: mode %s, rot %d)\n", r, S.off, S.hoff, S.e, getenv("CS_S1MODE") ? getenv("CS_S1MODE") : "6", F6rot);
        if (r) return 0;
        nlines = n6;
        rewind(f);
    }
    while (fgets(line, sizeof line, f)) {
        if (pass == 1 && strncmp(line, "MSM ", 4)) continue;
        if (!strncmp(line, "TL ", 3)) {
            const char *h = line + 3; g_tln = strcspn(h, "\r\n") / 2; g_tl = malloc(g_tln + 8);
            for (size_t i = 0; i < g_tln; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); g_tl[i] = (uint8_t)v; }
        } else if (!strncmp(line, "OP ", 3)) {
            const char *h = line + 3; g_opn = strcspn(h, "\r\n") / 2; g_op = malloc(g_opn + 8);
            for (size_t i = 0; i < g_opn; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); g_op[i] = (uint8_t)v; }
        } else if (!strncmp(line, "S1 ", 3)) {
            if (sscanf(line + 3, "%la", &s1_nctol) != 1) return 2;
        } else if (!strncmp(line, "ST ", 3)) {
            int metric; double t, s;
            if (sscanf(line + 3, "%d %la %la", &metric, &t, &s) != 3) return 2;
            F.metric = metric; mc_systol.tol = t;
        } else if (!strncmp(line, "REC ", 4)) {
            const char *h = line + 4; size_t L = strcspn(h, "\r\n") / 2; uint8_t *r = malloc(L + 8);
            for (size_t i = 0; i < L; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); r[i] = (uint8_t)v; }
            if (ns >= MAXR) return 2;
            Surf *s = &g_s[ns++]; memset(s, 0, sizeof *s); s->kind = L > 0x68 ? r[0x68] : -1; s->rec = r; s->n = (uint32_t)L; s->ent = -1; memcpy(&s->eptr, r + 0x20, 8);
            F.surfs = g_s; F.nsurfs = ns;
        } else if (!strncmp(line, "CS ", 3)) {
            if (sscanf(line + 3, "%la %la %la %la %la %la %d %d %d", &tol, &p3, &p4, &p5, &cdev, &systol, &hasn, &side, &nlines) != 9) return 2;
            int r = cs_setup(&S, tol, p3, p4, p5, cdev, systol);
            printf("CSSETUP %d off %a hoff %a e %a\n", r, S.off, S.hoff, S.e);
            if (r) return 0;
            have = 1;
        } else if (!strncmp(line, "MSM ", 4)) {
            if (!have) { fprintf(stderr, "cs_port: CS line first\n"); return 2; }
            int k; PlModelIn in; memset(&in, 0, sizeof in); double *Fr = in.frame;
            if (sscanf(line + 4, "%d %la %la %la %d %la %la %d %la %la %la %la %la %la %la %la %la", &k, &in.tol, &in.d, &in.h, &in.c, &in.d2, &in.h2, &in.r,
                       Fr, Fr + 1, Fr + 2, Fr + 3, Fr + 4, Fr + 5, Fr + 6, Fr + 7, Fr + 8) != 17 || k < 0 || k >= ns) return 2;
            in.edges = 1;
            PlModel m; const char *why = NULL;
            if (plm_model(&F, &g_s[k], &in, &m, &why)) { printf("PA %d refused %s\n", k, why ? why : "?"); if (s1) { printf("S1 refused: a surface's model\n"); return 0; } continue; }
            if (s1 && pass == 0) {   /* FUN_180229ba0's files of the surface: tmpedg0 / tmpcrn0 in file order (FUN_180225000, 12.11), the 0x78 record */
                MmCorners mc; memset(&mc, 0, sizeof mc);
                if (m.nedge > 0 && mm_corners(m.edge, m.nedge, m.npts, in.tol * 0.01, &mc)) { printf("S1 refused: corners NOT DECODED\n"); return 0; }
                int ne = m.nedge > 0 ? mc.nedge : 0;
                unsigned char *eb = malloc((size_t)ne * 0xb0 + 1), *cb = malloc((size_t)ne * 0x70 + 1);
                for (int e = 0; e < ne; e++) plm_edge_bytes(&mc.edge[e], eb + (size_t)e * 0xb0);
                for (int e = 0; e < ne; e++) { if (e < mc.nc) mm_corner_bytes(&mc.c[e], cb + (size_t)e * 0x70); else memset(cb + (size_t)e * 0x70, 0, 0x70); }
                unsigned char rec[0x78]; unsigned char *b1 = NULL; size_t n1 = mm_mm1_bytes(&m, &b1); free(b1);
                mm_record(&m, (long long)s1_mm1, (int)s1_nedg, 0, rec);
                int co = (int)s1_ncrn; memcpy(rec + 0x38, &co, 4);           /* +0x38 the tmpcrn0 position (l.364600 local_7fd0) */
                memcpy(rec + 0x54, &ne, 4);
                memcpy(rec, &g_s[k].eptr, 8);                               /* +0 the surface's eptr (l.364560 local_8008 = pDVar11) */
                int one = 1, m1 = -1; memcpy(rec + 0x68, &one, 4);         /* +0x68 = 1 plain surface (local_7fa0) */
                memcpy(rec + 0x6c, &m1, 4);  /* +0x6c: -1, the sorted-by-xmin next index of l.364800-364895 NOT PORTED (not read by stage 1) */
                s1_ep[s1_nrec / 0x78] = g_s[k].eptr;
                s1_add(&s1_rec, &s1_nrec, rec, 0x78); s1_mm1 += n1;
                s1_add(&s1_edg, &s1_nedg, eb, (size_t)ne * 0xb0); s1_add(&s1_crn, &s1_ncrn, cb, (size_t)ne * 0x70);
                free(eb); free(cb); if (m.nedge > 0) mm_corners_free(&mc);
                plm_model_free(&m);
                continue;
            }
            MmFill mf;
            if (mm_fill(&F, &g_s[k], &m, s1 ? (F6rot ? F6r : NULL) : in.frame, &mf, &why)) { printf("PA %d refused %s\n", k, why ? why : "?"); plm_model_free(&m); continue; }
            int g0 = S.ng; size_t t0 = S.ntf; int off0 = S.toff;
            if (s1) {   /* the edges / corners as FUN_18025dd50 / FUN_180261f40 left them for mode 6 (record s1j: +0x3c, +0x38, +0x54) */
                const unsigned char *rc = s1_rec + 0x78 * (size_t)s1j++;
                int eo, co, ne; memcpy(&eo, rc + 0x3c, 4); memcpy(&co, rc + 0x38, 4); memcpy(&ne, rc + 0x54, 4);
                if (ne > 0 && !getenv("CS_PADIR")) cs_edge_groups(&S, side, ne, edg6 + eo, crn6 + co);   /* (the phase A oracle: FUN_1801e8360's groups only) */
            }
            /* tmpedg0 / tmpcrn0 of the surface (stage A, cam3d_mmodel.c) -> FUN_1801e7e20; not with CS_PADIR (the phase A oracle compares
               FUN_1801e8360's own groups only) */
            if (m.nedge > 0 && !s1 && !getenv("CS_NOEDGES") && !getenv("CS_PADIR")) {
                MmCorners mc;
                if (mm_corners(m.edge, m.nedge, m.npts, in.tol * 0.01, &mc)) { printf("PA %d refused corners NOT DECODED\n", k); mm_fill_free(&mf); plm_model_free(&m); continue; }
                unsigned char *eb = malloc((size_t)mc.nedge * 0xb0 + 1), *cb = malloc((size_t)mc.nedge * 0x70 + 1);
                for (int e = 0; e < mc.nedge; e++) plm_edge_bytes(&mc.edge[e], eb + (size_t)e * 0xb0);
                for (int e = 0; e < mc.nedge; e++) { if (e < mc.nc) mm_corner_bytes(&mc.c[e], cb + (size_t)e * 0x70); else memset(cb + (size_t)e * 0x70, 0, 0x70); }
                if (mc.nc != mc.nedge) printf("PA %d corners %d edges %d\n", k, mc.nc, mc.nedge);
                cs_edge_groups(&S, side, mc.nedge, eb, cb);
                free(eb); free(cb); mm_corners_free(&mc);
            }
            cs_phase_a(&S, hasn, side, m.mode, mf.np, mf.pts, mf.nrm, mf.nt, mf.tri);
            if (getenv("CS_PADIR")) {   /* the phase A oracle's input (tools/peloader/mcmsurf_lmm.c PHA) and the port's own files */
                char d[4096], fn[4200]; snprintf(d, sizeof d, "%s/%d", getenv("CS_PADIR"), npa); mkdir(getenv("CS_PADIR"), 0755); mkdir(d, 0755);
                unsigned char rec[0x78], *b1 = NULL; mm_record(&m, 0, 0, 0, rec); size_t n1 = mm_mm1_bytes(&m, &b1);
                snprintf(fn, sizeof fn, "%s/tmpmm.tmp", d); FILE *w = fopen(fn, "wb"); if (w) { fwrite(rec, 1, 0x78, w); fclose(w); }
                snprintf(fn, sizeof fn, "%s/tmpmm1.tmp", d); w = fopen(fn, "wb"); if (w) { fwrite(b1, 1, n1, w); fclose(w); }
                free(b1);
                snprintf(fn, sizeof fn, "%s/port_ERRCHK.tmp", d); w = fopen(fn, "wb");
                for (int g = g0; w && g < S.ng; g++) {
                    unsigned char r[0x20]; memset(r, 0, sizeof r);
                    memcpy(r, &S.g[g].xmin, 8); memcpy(r + 8, &S.g[g].xmax, 8); memcpy(r + 0x10, &S.g[g].off, 4); memcpy(r + 0x14, &S.g[g].n, 4); r[0x18] = S.g[g].kind;
                    fwrite(r, 1, 0x20, w);
                }
                if (w) fclose(w);
                snprintf(fn, sizeof fn, "%s/port_ID2371.tmp", d); w = fopen(fn, "wb"); if (w) { fwrite(S.tf + t0, 8, S.ntf - t0, w); fclose(w); }
                snprintf(fn, sizeof fn, "%s/pha.lines", getenv("CS_PADIR")); w = fopen(fn, npa ? "a" : "w");
                if (w) {
                    if (!npa) { fprintf(w, "PHG %a %a %d %d %a", S.off_drive, S.hoff_drive, S.off_on, S.hoff_on, S.e); if (s1) fprintf(w, " %a", (double)F6rot); fprintf(w, "\n"); }
                    fprintf(w, "PHA %s %d %d %d %d", d, k, hasn, side, off0);
                    for (int i = 0; i < 9; i++) fprintf(w, " %a", (s1 ? F6r : in.frame)[i]);   /* S1: the scanner's rows */
                    fprintf(w, "\n"); fclose(w);
                }
                printf("PAFILES %d %d %d %d %d %d\n", npa, k, S.ng - g0, S.toff, mf.np, mf.nt);
                npa++;
            }
            printf("PA %d %d %d %d %d %d\n", k, m.mode, mf.np, mf.nt, g0, S.ng);
            for (int g = g0; g < S.ng; g++) printf("G %a %a %d %d %d\n", S.g[g].xmin, S.g[g].xmax, S.g[g].off, S.g[g].n, S.g[g].kind);
            printf("TF %zu %016llx\n", (S.ntf - t0) * 8, (unsigned long long)fnv(S.tf + t0, (S.ntf - t0) * 8));
            mm_fill_free(&mf); plm_model_free(&m);
        }
    }
    }   /* pass */
    fclose(f);
    if (!have || S.ng == 0) { printf("NOGROUPS\n"); return 0; }
    char fn[4200];
    snprintf(fn, sizeof fn, "%s/ID2371.tmp", argv[2]);
    FILE *w = fopen(fn, "wb"); if (!w) { perror(fn); return 2; }
    fwrite(S.tf, 8, S.ntf, w); fclose(w);
    snprintf(fn, sizeof fn, "%s/tmpedg0.tmp", argv[2]); w = fopen(fn, "wb"); if (!w) { perror(fn); return 2; }
    fwrite(S.edg ? (void *)S.edg : (void *)"", 1, S.nedg, w); fclose(w);
    snprintf(fn, sizeof fn, "%s/tmpcrn0.tmp", argv[2]); w = fopen(fn, "wb"); if (!w) { perror(fn); return 2; }
    fwrite(S.crn ? (void *)S.crn : (void *)"", 1, S.ncrn, w); fclose(w);
    if (cs_arc_setup(&S)) { printf("ARC refused (error 0x1518)\n"); return 0; }
    cs_sort_groups(&S);
    double xmin = S.g[0].xmin, xmax = S.g[0].xmax;
    for (int g = 0; g < S.ng; g++) { if (S.g[g].xmin < xmin) xmin = S.g[g].xmin; if (xmax < S.g[g].xmax) xmax = S.g[g].xmax; }
    double step = (xmax - xmin) / nlines, pos = xmin + step / 2, lo = pos - S.e * 2.0, hi = S.e * 2.0 + pos;
    if (s1) {   /* FUN_1801ea7e0 l.326209 / l.326526-326527: the first line processed (index n - 1) at -param_15 - (n - 1) param_16, += param_16 */
        step = step6; pos = -first6 - (double)(n6 + -1) * step6; lo = pos - S.e * 2.0; hi = S.e * 2.0 + pos;
    }
    snprintf(fn, sizeof fn, "%s/scn.case", argv[2]);
    FILE *c = fopen(fn, "w"); if (!c) { perror(fn); return 2; }
    fprintf(c, "SCN %a %a %a %a %a %a %a %d %d %a %a\n", S.e, S.off_drive, S.hoff_drive, S.off_chk, S.hoff_chk, S.t90, S.cdev, S.off_on, S.hoff_on, S.ctol, S.half);
    fprintf(c, "TF %s/ID2371.tmp %zu\n", argv[2], S.ntf / 18);
    fprintf(c, "EDG %s/tmpedg0.tmp %zu\n", argv[2], S.nedg / 0xb0);
    fprintf(c, "CRN0 %s/tmpcrn0.tmp %zu\n", argv[2], S.ncrn / 0x70);
    for (int g = 0; g < S.ng; g++) fprintf(c, "G %a %a %d %d %d\n", S.g[g].xmin, S.g[g].xmax, S.g[g].off, S.g[g].n, S.g[g].kind);
    /* CS_VTXLINES=k: instead, lines at the x of every k-th triangle vertex of ID2371 and at x +- e / 2 (the vertex-on-line branches of
       FUN_1801dbc50; a unit-test input), sorted, each with lo / hi = pos -+ 2 e */
    double *xs = NULL; int nx = 0;
    /* CS_MERGE=1: the port's batch / envelope merge after each line (cam3d_cscallop_merge.c), printed as the DLL harness prints its own
       (LMM_MERGE=1): "EN" + bytes 0..0x61 of each list node (with LMM_ESTFULL), "EST n fnv1a64", "WALL n fnv1a64"; the estore room 0x7fff
       (N13-28), the hint on (FUN_1801e7cd0(1)); after the line the walls are dropped (FUN_1801ed9b0) and the nodes returned (LAB_1801e4460) */
    int merge = getenv("CS_MERGE") ? atoi(getenv("CS_MERGE")) : 0;
    if (merge) { if (cs_est_init(&S, 0x7fff)) return 2; cs_est_hint(&S, 1); }
    /* CS_MERGE=2: then per line FUN_1801e02b0 ("ESTC", "ENC"), FUN_1801e0680 into OUTDIR/port_scn.bin (0x38 zero bytes first, as the
       harness's LMM_MERGE=4) and the walls by FUN_1801ed9b0 into OUTDIR/port_vcs.bin (nl, the nl x 8 table, rewritten at the end as
       FUN_1801ed8d0 does); "SCNTAB" + the scan file's line table. The scanner's line index counts down (l.326524): line i is nl - 1 - i */
    int nl_all = getenv("CS_VTXLINES") ? -1 : nlines;
    CsBuf scn = { 0 }, vcs = { 0 }; int soff = 0x38, voff = 0; int *stab = NULL; unsigned char *vtab = NULL;
    if (getenv("CS_VTXLINES")) {
        int kk = atoi(getenv("CS_VTXLINES")); if (kk < 1) kk = 1;
        size_t ntri = S.ntf / 18; xs = malloc(sizeof(double) * 3 * (ntri / kk + 2) * 3);
        for (size_t t = 0, q = 0; t < ntri; t++) for (int j = 0; j < 3; j++, q++) if (q % (size_t)kk == 0) {
            double x = S.tf[18 * t + 3 * j]; xs[nx++] = x; xs[nx++] = x + S.e * 0.5; xs[nx++] = x - S.e * 0.5; }
        int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
        qsort(xs, (size_t)nx, sizeof(double), cmpd);
        nlines = nx;
    }
    if (nl_all < 0) nl_all = nlines;
    if (merge == 2) {
        static unsigned char z38[0x38];
        CsBuf *b = &scn; b->cap = 1 << 20; b->p = malloc(b->cap); memcpy(b->p, z38, 0x38); b->n = 0x38;
        stab = malloc(sizeof(int) * (size_t)(nl_all + 1)); for (int k = 0; k < nl_all; k++) stab[k] = -1;
        vtab = calloc((size_t)nl_all * 8 + 8, 1); for (int k = 0; k < nl_all; k++) { int m1 = -1; memcpy(vtab + 8 * k, &m1, 4); }
        vcs.cap = 1 << 20; vcs.p = malloc(vcs.cap); memcpy(vcs.p, &nl_all, 4); memcpy(vcs.p + 4, vtab, (size_t)nl_all * 8); vcs.n = 4 + (size_t)nl_all * 8;
        voff = (int)vcs.n;
    }
    for (int i = 0; i < nlines; i++) {
        if (xs) { pos = xs[i]; lo = pos - S.e * 2.0; hi = S.e * 2.0 + pos; }
        fprintf(c, "L %a %a %a\n", pos, lo, hi);
        const char *why = NULL; S.npc = 0;
        printf("LINE %d %a %a %a\n", i, pos, lo, hi);
        if (cs_line_act(&S, pos, lo, hi, &why)) { printf("LINE refused %s\n", why); break; }
        {   /* the activated edge / corner nodes (+8.. of each, list order): counts and an FNV-1a hash ("NODES ne nc hash") */
            uint64_t h = 1469598103934665603ull; int ne = 0, nc = 0;
            for (CsEdge *e = S.eact; e; e = e->next, ne++) { unsigned char b[0xb0]; cs_edge_bytes(e, b); for (int j = 8; j < 0xb0; j++) { h ^= b[j]; h *= 1099511628211ull; } }
            for (CsCorner *e = S.cact; e; e = e->next, nc++) { unsigned char b[0x58]; cs_corner_bytes(e, b); for (int j = 8; j < 0x58; j++) { h ^= b[j]; h *= 1099511628211ull; } }
            printf("NODES %d %d %016llx\n", ne, nc, (unsigned long long)h);
        }
        if (cs_line_sect(&S, &why)) { printf("LINE refused %s\n", why); break; }
        if (merge) {
            if (cs_merge_line(&S)) printf("MERGE estore error %d\n", S.eerr);
            uint64_t h = 1469598103934665603ull; int n = 0; int full = getenv("LMM_ESTFULL") != NULL;
            for (short k = S.ehead; k != -1 && n < 0x8000; k = S.est[k].next, n++) {
                unsigned char b[0x68]; cs_node_bytes(&S.est[k], b);
                if (full) { printf("EN"); for (int j = 0; j < 0x62; j++) printf(" %02x", b[j]); printf("\n"); }
                for (int j = 0; j < 0x68; j++) { h ^= b[j]; h *= 1099511628211ull; }
            }
            printf("EST %d %016llx\n", n, (unsigned long long)h);
            uint64_t hw = 1469598103934665603ull;
            for (int k = 0; k < S.nwall; k++) { unsigned char b[0x68]; cs_node_bytes(&S.wall[k], b); for (int j = 0; j < 0x68; j++) { hw ^= b[j]; hw *= 1099511628211ull; } }
            printf("WALL %d %016llx\n", S.nwall, (unsigned long long)hw);
            if (merge == 2) {
                cs_clean_line(&S);
                uint64_t hc = 1469598103934665603ull; int nc = 0;
                for (short k = S.ehead; k != -1 && nc < 0x8000; k = S.est[k].next, nc++) {
                    unsigned char b[0x68]; cs_node_bytes(&S.est[k], b);
                    if (full) { printf("ENC"); for (int j = 0; j < 0x62; j++) printf(" %02x", b[j]); printf("\n"); }
                    for (int j = 0; j < 0x68; j++) { hc ^= b[j]; hc *= 1099511628211ull; }
                }
                printf("ESTC %d %016llx\n", nc, (unsigned long long)hc);
                cs_line_write(&S, 1, &scn, &soff, stab, nl_all - 1 - i, pos);
                if (S.nwall > 0) cs_wall_write(&S, &vcs, &voff, vtab, nl_all - 1 - i, pos);
            }
            S.nwall = 0;
            cs_est_release(&S);
        }
        if (getenv("SCN_PCHASH")) {   /* "PH n fnv1a64 code:count.." (tools/peloader/mcmsurf_lmm.c prints the same) */
            uint64_t h = 1469598103934665603ull; int kc[64] = { 0 };
            for (int p = 0; p < S.npc; p++) { unsigned char b[0x58]; cs_piece_bytes(&S.pc[p], b); for (int j = 0; j < 0x58; j++) { h ^= b[j]; h *= 1099511628211ull; } kc[b[0] & 63]++; }
            printf("PH %d %016llx", S.npc, (unsigned long long)h);
            for (int k = 0; k < 64; k++) if (kc[k]) printf(" %x:%d", k, kc[k]);
            printf("\n");
            pos = pos + step; lo = lo + step; hi = hi + step;
            continue;
        }
        for (int p = 0; p < S.npc; p++) { unsigned char b[0x58]; cs_piece_bytes(&S.pc[p], b); printf("PC"); for (int j = 0; j < 0x58; j++) printf(" %02x", b[j]); printf("\n"); }
        pos = pos + step; lo = lo + step; hi = hi + step;
    }
    fclose(c);
    { extern long cs_cov[8]; fprintf(stderr, "cs_port coverage: span %ld, two crossings %ld, no piece %ld, vertices on the line %ld (extended %ld, i2 below %ld)\n",
                                     cs_cov[0], cs_cov[1], cs_cov[2], cs_cov[3], cs_cov[4], cs_cov[5]); }
    { extern long cs_cov2[32]; extern const char *cs_cov2_name[32];
      fprintf(stderr, "cs_port coverage (edges / corners):"); for (int i = 0; cs_cov2_name[i]; i++) fprintf(stderr, " %s %ld,", cs_cov2_name[i], cs_cov2[i]); fprintf(stderr, "\n"); }
    if (merge) { extern long cs_cov3[24]; extern const char *cs_cov3_name[24];
      fprintf(stderr, "cs_port coverage (merge):"); for (int i = 0; cs_cov3_name[i]; i++) fprintf(stderr, " %s %ld,", cs_cov3_name[i], cs_cov3[i]); fprintf(stderr, "\n"); }
    if (merge == 2) {
        memcpy(vcs.p + 4, vtab, (size_t)nl_all * 8);   /* FUN_1801ed8d0: the table rewritten after nl */
        char fn2[4200]; FILE *w2;
        snprintf(fn2, sizeof fn2, "%s/port_scn.bin", argv[2]); w2 = fopen(fn2, "wb"); if (w2) { fwrite(scn.p, 1, scn.n, w2); fclose(w2); }
        if (s1) {   /* the whole scan file before FUN_18022c850 (12.17): the header FUN_180229640(-0x38, 0x11, 0x20) with +0xc = the trailer's
                       offset (uStack_3090 = local_3208[0], l.326767), the line blocks, the trailer (p18 = FUN_1801cfb60's param_21 = 0) */
            CsBuf full = { 0 }; unsigned char h[0x38]; memset(h, 0, sizeof h);
            cs_scan_header(h, -0x38, 0x11, 0x20); memcpy(h + 0xc, &soff, 4);
            full.cap = scn.n + 4096 + (size_t)nl_all * 4; full.p = malloc(full.cap); memcpy(full.p, h, 0x38); memcpy(full.p + 0x38, scn.p + 0x38, scn.n - 0x38); full.n = scn.n;
            cs_scan_trailer(&full, &S, F6r, P1.D, first6, step6, 0, nl_all, stab);
            snprintf(fn2, sizeof fn2, "%s/port_scn_s1.bin", argv[2]); w2 = fopen(fn2, "wb"); if (w2) { fwrite(full.p, 1, full.n, w2); fclose(w2); }
            printf("SCNFILE %zu trailer %d\n", full.n, soff);
            if (g_tl && g_tln >= 0x68 + 0x698) {   /* FUN_18022c850's appendix (12.17): port_scn_apx.bin; APX line for the DLL harness */
                short tt; memcpy(&tt, g_tl + 0x68 + 4, 2);
                if (tt == 0x19 || tt == 0xe || tt == 0x17) printf("APX refused: interfacial tool conversion NOT PORTED (tool type %d)\n", tt);
                else {
                    double tols[9] = { mc_systol.tol, mc_systol.chain_tol, mc_systol.chain_plane_tol, mc_systol.min_arc_length, mc_systol.curve_min_step,
                                       mc_systol.curve_max_step, mc_systol.curve_chord_dev, mc_systol.surf_step_chord_dev, mc_systol.nc_tol }, it[13] = { 0 };
                    size_t k = 6; while (s1mm.mm3[k] != '\n') k++;
                    const unsigned char *ent = s1mm.mm3 + k + 2 + 0x96 + 0x48;   /* tmpmm2's entries = tmpmm3's (s1_dump) */
                    int opid, e30; memcpy(&opid, g_op + 0x68, 4); memcpy(&e30, g_op + 0x30, 4);
                    cs_scan_appendix(&full, "part", s1mm.cnt2, -1, s1mm.cnt2, ent, tols, opid, e30, g_op + 0x68, g_tl + 0x68, it);
                    snprintf(fn2, sizeof fn2, "%s/port_scn_apx.bin", argv[2]); w2 = fopen(fn2, "wb"); if (w2) { fwrite(full.p, 1, full.n, w2); fclose(w2); }
                    printf("APX %zu TOLS", full.n); for (int i = 0; i < 9; i++) printf(" %a", tols[i]); printf("\n");
                }
            }
            free(full.p);
        }
        snprintf(fn2, sizeof fn2, "%s/port_vcs.bin", argv[2]); w2 = fopen(fn2, "wb"); if (w2) { fwrite(vcs.p, 1, vcs.n, w2); fclose(w2); }
        printf("SCNTAB 0"); for (int k = 0; k < nl_all; k++) printf(" %d", stab[k]); printf("\n");
        free(scn.p); free(vcs.p); free(stab); free(vtab);
    }
    printf("END\n");
    cs_free(&S);
    return 0;
}
