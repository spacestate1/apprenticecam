/* ifk_hbechain_cli - run the port of mcmsurf FUN_180232540 (ifk_hbechain.c) on an HBE file and print the entities and
 * chains in the text format the DLL oracle prints (tools/peloader/mcmsurf_hbe.c, formats/interfacial.md 14.2):
 *   ifk_hbechain_cli HBE flat depth maxang tol nc_tol chord_dev [maxpts [ignore_depths]]
 * Numbers may be C99 hex floats. Doubles are printed as %a. Top view only (view 1). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ifk_hbechain.h"
#include "ifk_offchk.h"
#include "ifk_tpconv.h"

static void pr3(const double *v) { printf(" %a %a %a", v[0], v[1], v[2]); }

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "--filter")) {   /* --filter CASES: the F lines of tools/peloader/mcmsurf_hbe.c through ifk_hbefilter.c */
        int hbe_filter(const IfkHbe68 *in, int n_in, IfkHbe68 *out, int cap, int plane_z, short max_win, double tol, double systol);
        FILE *f = fopen(argv[2], "r"); if (!f) return 1;
        char line[8192]; int k = 0;
        while (fgets(line, sizeof line, f)) {
            if (line[0] != 'F') continue;
            int pz, mw, n, cap; double systol, ftol;
            if (sscanf(line + 2, "%d %d %la %la %d %d", &pz, &mw, &systol, &ftol, &n, &cap) != 6) return 2;
            IfkHbe68 *in = calloc((size_t)n, sizeof *in), *out = calloc((size_t)cap, sizeof *out);
            for (int i = 0; i < n; i++) {
                double *r = in[i].p0;
                if (!fgets(line, sizeof line, f) || sscanf(line, "%la %la %la %la %la %la", r, r + 1, r + 2, in[i].p1, in[i].p1 + 1, in[i].p1 + 2) != 6) return 2;
                in[i].type = 1; in[i].first = i == 0;
            }
            int m = hbe_filter(in, n, out, cap, pz, (short)mw, ftol, systol);
            printf("FILT %d %d\n", k++, m);
            for (int i = 0; i < m; i++) {
                printf("R %d %d", out[i].type, out[i].first); pr3(out[i].p0); pr3(out[i].p1);
                if (out[i].type == 2) { pr3(out[i].c); pr3(out[i].n); }
                printf("\n");
            }
            free(in); free(out);
        }
        fclose(f);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--tp")) {   /* --tp CASES: the T / U lines of tools/peloader/mcmsurf_hbe.c through ifk_tpconv.c */
        FILE *f = fopen(argv[2], "r"); if (!f) return 1;
        char line[8192]; int k = 0;
        while (fgets(line, sizeof line, f)) {
            if (line[0] != 'T' && line[0] != 'U') continue;
            char cmd = line[0];
            int colour = 0, profile, n; double tol = 0;
            uint8_t *tp = NULL; short cnt = 0; int old;
            if (cmd == 'T') {
                if (sscanf(line + 2, "%la %d %d %d", &tol, &colour, &profile, &n) != 4) return 2;
                IfkHbe68 *in = calloc((size_t)n, sizeof *in);
                for (int i = 0; i < n; i++) {
                    int ty; double *a = in[i].p0, *b = in[i].p1;
                    if (!fgets(line, sizeof line, f) || sscanf(line, "%d %la %la %la %la %la %la", &ty, a, a + 1, a + 2, b, b + 1, b + 2) != 7) return 2;
                    in[i].type = (uint8_t)ty; in[i].first = i == 0;
                }
                tp = ifk_hbe_to_tpents(in, n, tol, (uint8_t)colour, &cnt);
                printf("TP %d %d\n", k++, cnt);
                for (int i = 0; tp && i < cnt; i++) { printf("E"); for (int j = 0; j < IFK_TPENT; j++) printf("%02x", tp[IFK_TPENT * i + j]); printf("\n"); }
                free(in);
                old = n;
                if (!tp || cnt < 1) { free(tp); continue; }
            } else {
                if (sscanf(line + 2, "%d %d", &profile, &n) != 2) return 2;
                tp = calloc((size_t)n, IFK_TPENT);
                for (int i = 0; i < n; i++) {
                    if (!fgets(line, sizeof line, f)) return 2;
                    for (int j = 0; j < IFK_TPENT; j++) { unsigned v; if (sscanf(line + 2 * j, "%2x", &v) != 1) return 2; tp[IFK_TPENT * i + j] = (uint8_t)v; }
                }
                cnt = (short)n; old = n;
            }
            IfkHbe68 *out = calloc(2 * (size_t)cnt + 2, sizeof *out); int m = old;
            int r = ifk_tpents_to_hbe(tp, cnt, 1, profile, old, out, &m);
            if (r < 0) r = 0;
            if (cmd == 'U') printf("BACKU %d %d %d\n", k++, r, m);
            else printf("BACK %d %d\n", r, m);
            for (int i = 0; r && i < m; i++) {
                printf("R %d %d", out[i].type, out[i].first); pr3(out[i].p0); pr3(out[i].p1);
                if (out[i].type == 2) { pr3(out[i].c); pr3(out[i].n); }
                printf("\n");
            }
            free(out); free(tp);
        }
        fclose(f);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "--bound")) {   /* --bound HBE parent conv_a0: the pock_bound of FUN_18021ff80 (ifk_offchk.c) */
        IfkHbe68 *rec = NULL; int n = ifk_hbe_read(argv[2], &rec);
        if (n < 0) return 1;
        IfkPockBound b;
        if (ifk_offchk_bound(rec, n, atoi(argv[3]), strtod(argv[4], NULL), &b)) return 2;
        printf("BOUND %d len %.17g box %.17g %.17g %.17g %.17g flags %d %d %d %d %d offset %.17g\n", b.n, b.len, b.box[0], b.box[1], b.box[2], b.box[3],
               b.b0, b.b1, b.b2, b.b3, b.b4, b.offset);
        printf("CHECK %d\n", ifk_offchk(rec, n, atoi(argv[3]), strtod(argv[4], NULL), 0, NULL, NULL));
        ifk_offchk_free(&b); free(rec);
        return 0;
    }
    if (argc < 8) { fprintf(stderr, "usage: ifk_hbechain_cli HBE flat depth maxang tol nc_tol chord_dev [maxpts [ignore_depths]]\n"); return 1; }
    IfkHbe68 *rec = NULL;
    int n = ifk_hbe_read(argv[1], &rec);
    if (n < 0) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    IfkHbeChainParams p = { 0 };
    p.flat = atoi(argv[2]); p.depth = strtod(argv[3], NULL); p.maxang = strtod(argv[4], NULL);
    p.tol = strtod(argv[5], NULL); p.nc_tol = strtod(argv[6], NULL); p.chord_dev = strtod(argv[7], NULL);
    p.max_spline_pts = argc > 8 ? atoi(argv[8]) : 4000;
    p.chain_ignore_depths = argc > 9 ? atoi(argv[9]) : 0;
    p.view = 1;
    IfkHbeChains o;
    if (ifk_hbe_to_chains(rec, n, &p, &o)) { fprintf(stderr, "out of memory\n"); return 2; }
    for (int i = 0; i < o.nents; i++) {
        const IfkCurve *c = &o.ents[i];
        printf("ENT %d %d", i, c->type); pr3(c->p0); pr3(c->p1); printf("\n");
        if (c->type == IFK_ENT_ARC) { printf(" ARC"); pr3(c->c); printf(" %a %a %a %d\n", c->r, c->sa, c->sw, c->view); }
        if (c->type == IFK_ENT_SPLINE) {
            printf(" SPL %d %d\n", c->nnode - 1, c->closed);
            for (int k = 0; k < 12 * (c->nnode - 1); k++) printf(" %a%s", c->coef[k], k % 12 == 11 ? "\n" : "");
        }
    }
    printf("MADE %d\n", o.made);
    if (o.made) for (int k = 0; k < o.nchains; k++) {
        printf("CHAIN %d %d %d:", k, o.chains[k].closed, o.chains[k].n);
        for (int j = 0; j < o.chains[k].n; j++) printf(" %d/%d", o.chains[k].it[j].ent, o.chains[k].it[j].ep);
        printf("\n");
    }
    if (o.undefined_hit) printf("UNDEFINED\n");
    ifk_hbe_chains_free(&o); free(rec);
    return 0;
}
