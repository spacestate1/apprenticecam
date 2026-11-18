/* msm_port CASE > RESULTS: the port of make_surf_model (mcpartline_mesh.c plm_model, formats/surf_classic3.md 12.9) on a
 * tools/peloader --msm case (tools/msm_case.py; the oracle tools/peloader/mccore_stl.c MSM). Prints, per MSM line,
 *   "MSM k", "NU nu NVV nv NT ntri MODE m", "BOX" + 6 %a (mms +8..+0x30), "U %a" x (nu + 1), "V %a" x (nv + 1), the loop, "NE n" (mms +0x54)
 *   and the n tmpedg0 records as "W 176" + bytes, the vertices "P" / "N" (%a), "MSM end"
 * or "MSM k refused WHY". tools/tests/msm_cmp.py compares this with the oracle's output. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include "mcam_load.h"
#include "mcpartline.h"
#include "cam3d_mmodel.h"

/* the loader's links this tool does not use (as ifk/ifk_stl_port.c) */
const McSysTol *mc_systol_units(int metric) { (void)metric; return &mc_systol; }
int occt_mesh_surf() { return -1; }
int part_tools_apply() { return -1; }
int part_tools_list() { return -1; }

#define MAXR 20000
static Surf g_s[MAXR];

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: msm_port CASE\n"); return 2; }
    FILE *f = fopen(argv[1], "r"); if (!f) { perror(argv[1]); return 2; }
    McamFile F; memset(&F, 0, sizeof F);
    int ns = 0, nmsm = -1;
    static char line[1 << 24];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "ST ", 3)) {
            int metric; double t, s;
            if (sscanf(line + 3, "%d %la %la", &metric, &t, &s) != 3) return 2;
            F.metric = metric; mc_systol.tol = t;
        } else if (!strncmp(line, "REC ", 4)) {
            const char *h = line + 4; size_t L = strcspn(h, "\r\n") / 2; uint8_t *r = malloc(L + 8);
            for (size_t i = 0; i < L; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); r[i] = (uint8_t)v; }
            if (ns >= MAXR) return 2;
            Surf *s = &g_s[ns++]; memset(s, 0, sizeof *s); s->kind = L > 0x68 ? r[0x68] : -1; s->rec = r; s->n = (uint32_t)L; s->ent = -1; memcpy(&s->eptr, r + 0x20, 8);
            F.surfs = g_s; F.nsurfs = ns;
        } else if (!strncmp(line, "RIDGE ", 6)) {
            int k = atoi(line + 6); if (k < 0 || k >= ns) return 2;
            printf("RIDGE %d %d\n", k, plm_ridge(&F, &g_s[k], 5.0));
        } else if (!strncmp(line, "MSM ", 4)) {
            int k; PlModelIn in; memset(&in, 0, sizeof in); double *Fr = in.frame;
            if (sscanf(line + 4, "%d %la %la %la %d %la %la %d %la %la %la %la %la %la %la %la %la", &k, &in.tol, &in.d, &in.h, &in.c, &in.d2, &in.h2, &in.r,
                       Fr, Fr + 1, Fr + 2, Fr + 3, Fr + 4, Fr + 5, Fr + 6, Fr + 7, Fr + 8) != 17 || k < 0 || k >= ns) return 2;
            nmsm++;                                                           /* the MSM line's number (MSM_MMDIR/<n>: a surface may have several lines) */
            in.edges = 1;                                                     /* in_stack_d0 = 3: FUN_180229ba0's call (l.364700) */
            PlModel m; const char *why = NULL;
            if (plm_model(&F, &g_s[k], &in, &m, &why)) { printf("MSM %d refused %s\n", k, why ? why : "?"); continue; }
            printf("MSM %d\nNU %d NVV %d NT %d MODE %d\nBOX", k, m.nu, m.nv, m.ntri, m.mode);
            for (int i = 0; i < 6; i++) printf(" %a", m.box[i]);
            printf("\n");
            for (int i = 0; i <= m.nu; i++) printf("U %a\n", m.u[i]);
            for (int i = 0; i <= m.nv; i++) printf("V %a\n", m.v[i]);
            printf("NL %d\n", m.nl);
            if (m.nl && m.tb) for (int l = 0; l < m.nl; l++) {
                printf("L %d %d\n", m.tb[2 * l], m.tb[2 * l + 1]);
                for (int i = m.tb[2 * l]; i <= m.tb[2 * l] + m.tb[2 * l + 1]; i++) printf("LP %a %a\n", m.lp[2 * i], m.lp[2 * i + 1]);
            }
            else if (m.nl) { printf("L 0 %d\n", m.nlp - 1); for (int i = 0; i < m.nlp; i++) printf("LP %a %a\n", m.lp[2 * i], m.lp[2 * i + 1]); }
            printf("NE %d\n", m.nedge);
            for (int e = 0; e < m.nedge; e++) {                              /* the tmpedg0 records, as the oracle's SysFile::Write lines */
                unsigned char b[0xb0]; plm_edge_bytes(&m.edge[e], b);
                printf("W 176"); for (int i = 0; i < 0xb0; i++) printf(" %02x", b[i]); printf("\n");
            }
            if (m.nedge > 0) {                                                /* FUN_180225000 (cam3d_mmodel.c): tol = chord x 0.9 x 0.01 */
                MmCorners mc;
                if (mm_corners(m.edge, m.nedge, m.npts, in.tol * 0.01, &mc)) printf("CRN refused\n");
                else {
                    printf("CRN 1 %d\n", mc.chained);
                    for (int e = 0; e < mc.nedge; e++) { unsigned char b[0xb0]; plm_edge_bytes(&mc.edge[e], b); printf("E"); for (int i = 0; i < 0xb0; i++) printf(" %02x", b[i]); printf("\n"); }
                    for (int e = 0; e < mc.nc; e++) { unsigned char b[0x70]; mm_corner_bytes(&mc.c[e], b); printf("C"); for (int i = 0; i < 0x70; i++) printf(" %02x", b[i]); printf("\n"); }
                    mm_corners_free(&mc);
                }
            }
            if (getenv("MSM_MMDIR")) {                                       /* tmpmm / tmpmm1 for the fill oracle (mccore_stl.c MMB, tools/tests/mmb_cmp.py) */
                char d[4096], fn[4200]; snprintf(d, sizeof d, "%s/%d", getenv("MSM_MMDIR"), nmsm); mkdir(d, 0755);
                unsigned char rec[0x78], *b1 = NULL; mm_record(&m, 0, 0, 0, rec); size_t n1 = mm_mm1_bytes(&m, &b1);
                snprintf(fn, sizeof fn, "%s/tmpmm.tmp", d); FILE *w = fopen(fn, "wb"); if (w) { fwrite(rec, 1, 0x78, w); fclose(w); }
                snprintf(fn, sizeof fn, "%s/tmpmm1.tmp", d); w = fopen(fn, "wb"); if (w) { fwrite(b1, 1, n1, w); fclose(w); }
                free(b1);
                MmFill mf; const char *fw = NULL;
                if (mm_fill(&F, &g_s[k], &m, in.frame, &mf, &fw)) printf("MMB refused %s\n", fw);
                else {
                    printf("MMB %d %d\n", mf.np, mf.nt);
                    for (int i = 0; i < mf.nt; i++) printf("T %d %d %d\n", mf.tri[3 * i], mf.tri[3 * i + 1], mf.tri[3 * i + 2]);
                    for (int i = 0; i < mf.np; i++) printf("Q %a %a %a %a %a %a\n", mf.pts[3 * i], mf.pts[3 * i + 1], mf.pts[3 * i + 2], mf.nrm[3 * i], mf.nrm[3 * i + 1], mf.nrm[3 * i + 2]);
                    mm_fill_free(&mf);
                }
            }
            for (int i = 0; i < m.npts; i++) printf("P %a %a %a\n", m.pts[3 * i], m.pts[3 * i + 1], m.pts[3 * i + 2]);
            for (int i = 0; i < m.npts; i++) printf("N %a %a %a\n", m.nrm[3 * i], m.nrm[3 * i + 1], m.nrm[3 * i + 2]);
            printf("MSM end\n");
            plm_model_free(&m);
        }
    }
    return 0;
}
