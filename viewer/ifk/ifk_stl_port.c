/* ifk_stl_port [-t] CASE > RESULTS: the port (ifk_surfpoly.c + mcpartline_mesh.c) on a tools/peloader --stl case (tools/peloader/mccore_stl.c),
 * printing the same sections: "MESH idx ntri nv 0 0" + triangles + vertices (%a) and, for WR, the "P" lines and "WR end".
 * -t: WR writes the STL text itself (MSVCR120 %f) instead of the P lines.
 * -f VALUES: prints ifk_msvc_f(v, 6) for each hex double of VALUES as "hex text" (the format of tools/peloader --crtf). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../mcam_load.h"
#include "../mcpartline.h"
#include "ifk_surfpoly.h"

/* the loader's links this tool does not use (as tools/partline_port.c) */
const McSysTol *mc_systol_units(int metric) { (void)metric; return &mc_systol; }
int occt_mesh_surf() { return -1; }
int part_tools_apply() { return -1; }
int part_tools_list() { return -1; }

#define MAXR 20000
static Surf g_s[MAXR];
static int g_sel[MAXR];

int main(int argc, char **argv)
{
    int text = 0, ai = 1;
    if (argc > 2 && !strcmp(argv[1], "-f")) {
        FILE *f = fopen(argv[2], "r"); if (!f) return 2;
        char line[512], t[700];
        while (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; if (!line[0] || !strncmp(line, "FMT ", 4)) continue; ifk_msvc_f(t, sizeof t, strtod(line, NULL), 6); printf("%s %s\n", line, t); }
        fclose(f); return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "-t")) { text = 1; ai = 2; }
    if (argc <= ai) { fprintf(stderr, "usage: ifk_stl_port [-t] CASE | -f VALUES\n"); return 2; }
    FILE *f = fopen(argv[ai], "r"); if (!f) { perror(argv[ai]); return 2; }
    McamFile F; memset(&F, 0, sizeof F);
    double step = 0.001; int ns = 0;
    static char line[1 << 24];
    IfkStlOut o = { stdout, !text };
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "ST ", 3)) {
            int metric; double t, s;
            if (sscanf(line + 3, "%d %la %la", &metric, &t, &s) != 3) return 2;
            F.metric = metric; mc_systol.tol = t; step = s;
        } else if (!strncmp(line, "REC ", 4)) {
            const char *h = line + 4; size_t L = strcspn(h, "\r\n") / 2; uint8_t *r = malloc(L + 8);
            for (size_t i = 0; i < L; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); r[i] = (uint8_t)v; }
            if (ns >= MAXR) return 2;
            Surf *s = &g_s[ns++]; memset(s, 0, sizeof *s); s->kind = L > 0x68 ? r[0x68] : -1; s->rec = r; s->n = (uint32_t)L; s->ent = -1; memcpy(&s->eptr, r + 0x20, 8);
            F.surfs = g_s; F.nsurfs = ns;   /* a trimmed surface finds its base (mcam_surf_base) among the records */
        } else if (!strncmp(line, "SEL ", 4)) {
            int k = atoi(line + 4); if (k < 0 || k >= ns) return 2; g_sel[k] = 1;
        } else if (!strncmp(line, "MESH ", 5)) {
            int k = atoi(line + 5); if (k < 0 || k >= ns) return 2;
            PlMesh m; const char *why = NULL;
            int nt = plm_mesh(&F, &g_s[k], step, &m, &why);   /* surf_to_stl's make_surf_polygons(1, 0, ent, tol, ..) */
            if (nt < 0) { printf("MESH %d refused %s\n", k, why ? why : "?"); continue; }
            printf("MESH %d %d %d 0 0\n", k, m.ntri, m.nv);
            for (int i = 0; i < m.ntri; i++) printf("%d %d %d\n", m.tri[3 * i], m.tri[3 * i + 1], m.tri[3 * i + 2]);
            for (int i = 0; i < m.nv; i++) printf("%a %a %a\n", m.xyz[3 * i], m.xyz[3 * i + 1], m.xyz[3 * i + 2]);
            plm_free(&m);
        } else if (!strncmp(line, "WR", 2)) {
            /* wr_stl's walk: class 0x100 records in order, not blanked (sel 0x100), selected (bit 4); the level test is answered visible as in the oracle */
            const Surf **sel = malloc(sizeof(Surf *) * (ns + 1)); int n = 0;
            for (int i = 0; i < ns; i++) {
                uint16_t sb; memcpy(&sb, g_s[i].rec + 4, 2);
                if (g_sel[i]) sb |= 4;
                if (!(sb & 0x100) && (sb & 4)) sel[n++] = &g_s[i];
            }
            const char *why = NULL;
            int r = ifk_wr_stl(&F, sel, n, step, &o, &why);
            if (r < 0) printf("WR refused %s\n", why ? why : "?");
            if (!text) printf("WR end\n");
            free(sel);
        }
    }
    fclose(f);
    return 0;
}
