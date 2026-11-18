/* ifk_arcfit_test.c - driver of the CSpinBdryArcFitter port (ifk_arcfit.c) on the CASES format of the DLL oracle
 * (tools/peloader/mdef_arcfit.c, `peloader mcmachinedef.dll --arcfit CASES`); prints the same RES / T / END lines, so the two
 * outputs compare with cmp. Log lines of the oracle (SETACTIVE / SEARCH / ADD / ASSERT) are printed as the port knows them:
 * SETACTIVE when DefineNamedView would call SetActivePlane, ASSERT n (the count) instead of the DLL's per-call lines;
 * NOTDECODED / LIMIT when the port refuses a case.
 *   ifk_arcfit_test CASES [--out RESULTS] [--asserts]   (--asserts: print the ASSERT count line, for debugging) */
#include "ifk_arcfit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXPL 64

int main(int argc, char **argv)
{
    const char *cases = NULL, *out = NULL;
    int show_asserts = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--asserts")) show_asserts = 1;
        else if (!cases) cases = argv[i];
        else { fprintf(stderr, "usage: ifk_arcfit_test CASES [--out RESULTS] [--asserts]\n"); return 1; }
    }
    if (!cases) { fprintf(stderr, "usage: ifk_arcfit_test CASES [--out RESULTS] [--asserts]\n"); return 1; }
    FILE *f = fopen(cases, "r"), *o = out ? fopen(out, "w") : stdout;
    if (!f || !o) { perror("ifk_arcfit_test"); return 2; }
    char name[256] = "case", line[4096];
    double axis[6] = { 0, 0, 0, 1, 0, 0 }, fittol = 0.001;
    int flag = 0, col = 0, nl = 0, cap = 0, npl = 0, wcs = 0, cpl = 0;
    double *lines = NULL;
    IfkAfPlane pl[MAXPL];
    IfkAfEnv env;
    memset(&env, 0, sizeof env);
    env.systol = 5e-5; env.disttol = 5e-5; env.min_arc_length = 0.0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "CASE ", 5)) { sscanf(line + 5, "%255s", name); nl = 0; npl = 0; wcs = cpl = 0; }
        else if (!strncmp(line, "TOL ", 4)) { if (sscanf(line + 4, "%la %la", &env.systol, &env.disttol) != 2) return 2; }
        else if (!strncmp(line, "MINARC ", 7)) { if (sscanf(line + 7, "%la", &env.min_arc_length) != 1) return 2; }
        else if (!strncmp(line, "FIT ", 4)) { if (sscanf(line + 4, "%la %d", &fittol, &flag) != 2) return 2; }
        else if (!strncmp(line, "AXIS ", 5)) {
            if (sscanf(line + 5, "%la %la %la %la %la %la", axis, axis + 1, axis + 2, axis + 3, axis + 4, axis + 5) != 6) return 2;
        } else if (!strncmp(line, "COLOR ", 6)) col = atoi(line + 6);
        else if (!strncmp(line, "PL ", 3)) {
            int id, view;
            if (npl >= MAXPL) return 2;
            IfkAfPlane *p = &pl[npl];
            if (sscanf(line + 3, "%d %d %la %la %la %la %la %la", &id, &view, p->x, p->x + 1, p->x + 2, p->origin, p->origin + 1, p->origin + 2) != 8) return 2;
            p->id = (short)id; p->view = (short)view; npl++;
        } else if (!strncmp(line, "AC ", 3)) { if (sscanf(line + 3, "%d %d", &wcs, &cpl) != 2) return 2; }
        else if (!strncmp(line, "L ", 2)) {
            if (nl >= cap) { cap = cap ? 2 * cap : 64; lines = realloc(lines, (size_t)cap * 6 * sizeof *lines); }
            double *q = lines + 6 * (size_t)nl;
            if (sscanf(line + 2, "%la %la %la %la %la %la", q, q + 1, q + 2, q + 3, q + 4, q + 5) != 6) return 2;
            nl++;
        } else if (!strncmp(line, "RUN", 3)) {
            memset(&env.wcs, 0, sizeof env.wcs); memset(&env.cplane, 0, sizeof env.cplane);
            for (int i = 0; i < npl; i++) { if (pl[i].id == wcs) env.wcs = pl[i]; if (pl[i].id == cpl) env.cplane = pl[i]; }
            env.planes = pl; env.nplanes = npl;
            /* the tp_ents as mcmsurf FUN_18020c830 builds them: n + 1 entries, zeroed; +0 = 2, +4 colour, +6 = 3, start, end */
            int alloc_n = nl + 1;
            uint8_t *tp = calloc((size_t)alloc_n, IFK_TPENT_SIZE);
            for (int i = 0; i < nl; i++) {
                uint8_t *e = tp + IFK_TPENT_SIZE * (size_t)i;
                int32_t two = 2;
                memcpy(e, &two, 4); e[4] = (uint8_t)col; e[6] = 3;
                memcpy(e + 0x10, lines + 6 * (size_t)i, 48);
            }
            IfkAfResult r;
            int rc = ifk_arcfit(&tp, &alloc_n, (short)nl, axis, fittol, flag, &env, &r);
            if (rc != IFK_AF_OK) {
                fprintf(o, "RES %s %s\nEND\n", name, rc == IFK_AF_NOT_DECODED ? "NOTDECODED" : rc == IFK_AF_LIMIT ? "LIMIT" : "ERROR");
                free(tp); continue;
            }
            if (r.new_plane) fprintf(o, "RES %s %d %d NEW %d\n", name, r.ok, r.count, alloc_n);
            else fprintf(o, "RES %s %d %d %d %d\n", name, r.ok, r.count, r.prof_id, alloc_n);
            for (int i = 0; i < alloc_n; i++) {
                fprintf(o, "T %d ", i);
                for (int k = 0; k < IFK_TPENT_SIZE; k++) fprintf(o, "%02x", tp[IFK_TPENT_SIZE * (size_t)i + k]);
                fputc('\n', o);
            }
            if (r.new_plane) {
                const double *m = r.new_plane_m, *po = r.new_plane_o;
                fprintf(o, "SEARCH %a %a %a  %a %a %a  %a %a %a  %a %a %a\n", m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], po[0], po[1], po[2]);
                fprintf(o, "SETACTIVE 0 NEW\n");
            } else if (r.set_active_cplane) fprintf(o, "SETACTIVE 0 %d\n", r.prof_id);
            if (show_asserts && r.nassert) fprintf(o, "ASSERTS %d\n", r.nassert);
            fprintf(o, "END\n");
            free(tp);
        }
    }
    if (out) fclose(o);
    free(lines);
    return 0;
}
