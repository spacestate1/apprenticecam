/* mp_cli - ours: command line driver for the MP post engine.
 *   mp_cli PART.mcx-9|PART.mcam|FILE.nci POST.pst OUT.nc [--ops N,..] [--nci-out F] [--metric 0|1] [--product N]
 *          [--date YYYY-MM-DD] [--time HH:MM[:SS]] [--part PART] [--trace] [--call BLOCK[,BLOCK..]] [--set var=value ...]
 *          [--eol crlf|lf]   (the standard end of block: crlf = mcpost's "\r\n" (default), lf = "\n" as the app posts, MpEnv.eol)
 * For a part file the ASCII NCI is produced first with nci_ascii (mp_nci_write_part) when that module is linked
 * (build flag -DMP_HAVE_NCI_ASCII); otherwise only .nci input is accepted. --call runs the named postblocks with
 * mp_block_call instead of an NCI file (engine test). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <strings.h>
#include "mp_int.h"
#ifdef MP_HAVE_NCI_ASCII
#include "nci_ascii.h"
#include "mcam_load.h"
#endif

static int ends_with(const char *s, const char *e) { size_t a = strlen(s), b = strlen(e); return a >= b && !strcasecmp(s + a - b, e); }

int main(int argc, char **argv)
{
    const char *in = NULL, *pst = NULL, *out = NULL, *ops = NULL, *nci_out = NULL, *calls = NULL;
    const char *sets[64]; int nsets = 0, trace = 0; const char *partopt = NULL;
    MpEnv env; memset(&env, 0, sizeof env);
    time_t now = time(NULL); struct tm *lt = localtime(&now);
    env.day = lt->tm_mday; env.mon = lt->tm_mon + 1; env.year = lt->tm_year + 1900; env.hour = lt->tm_hour; env.min = lt->tm_min; env.sec = lt->tm_sec;
    env.metric = -1; env.product = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--ops") && i + 1 < argc) ops = argv[++i];
        else if (!strcmp(a, "--nci-out") && i + 1 < argc) nci_out = argv[++i];
        else if (!strcmp(a, "--metric") && i + 1 < argc) env.metric = atoi(argv[++i]);
        else if (!strcmp(a, "--product") && i + 1 < argc) env.product = atoi(argv[++i]);
        else if (!strcmp(a, "--date") && i + 1 < argc) sscanf(argv[++i], "%d-%d-%d", &env.year, &env.mon, &env.day);
        else if (!strcmp(a, "--time") && i + 1 < argc) { env.sec = 0; sscanf(argv[++i], "%d:%d:%d", &env.hour, &env.min, &env.sec); }
        else if (!strcmp(a, "--trace")) trace = 1;
        else if (!strcmp(a, "--eol") && i + 1 < argc) {
            const char *e = argv[++i];
            if (!strcmp(e, "lf")) env.eol = "\n"; else if (!strcmp(e, "crlf")) env.eol = NULL;
            else { fprintf(stderr, "mp_cli: --eol crlf|lf\n"); return 2; }
        }
        else if (!strcmp(a, "--part") && i + 1 < argc) partopt = argv[++i];
        else if (!strcmp(a, "--call") && i + 1 < argc) calls = argv[++i];
        else if (!strcmp(a, "--set") && i + 1 < argc && nsets < 64) sets[nsets++] = argv[++i];
        else if (!in) in = a; else if (!pst) pst = a; else if (!out) out = a;
        else { fprintf(stderr, "mp_cli: unexpected argument %s\n", a); return 2; }
    }
    if (calls && in && !out) { out = pst; pst = in; in = NULL; }
    if (!pst || !out || (!in && !calls)) {
        fprintf(stderr, "usage: mp_cli PART.mcx-9|PART.mcam|FILE.nci POST.pst OUT.nc [--ops N,..] [--nci-out F] [--metric 0|1]\n"
                        "              [--product 0..3] [--date Y-M-D] [--time H:M[:S]] [--trace] [--set var=val] [--eol crlf|lf]\n"
                        "       mp_cli POST.pst OUT.nc --call BLOCK[,BLOCK...]   (engine test)\n");
        return 2;
    }
    char err[512] = "";
    MpPost *p = mp_post_load(pst, err, sizeof err);
    if (!p) { fprintf(stderr, "mp_cli: %s\n", err); return 1; }
    for (int i = 0; i < mp_post_nwarnings(p); i++) fprintf(stderr, "pst: %s\n", mp_post_warning(p, i));
    /* product from the post folder / name when not given (ours) */
    if (env.product < 0) {
        env.product = 0;
        if (strcasestr(pst, "router")) env.product = 1;
        else if (strcasestr(pst, "lathe") || strcasestr(pst, "MPLFAN")) env.product = 2;
        else if (strcasestr(pst, "wire") || strcasestr(pst, "MPWFANUC")) env.product = 3;
    }
    if (env.metric < 0) env.metric = 0;

    FILE *nci = NULL; char tmpnci[600] = "";
    if (in && (ends_with(in, ".mcx-9") || ends_with(in, ".mcam") || ends_with(in, ".mcx"))) {
#ifdef MP_HAVE_NCI_ASCII
        static McamFile mf;
        if (!mcam_load(&mf, in)) { fprintf(stderr, "mp_cli: cannot load %s\n", in); return 1; }
        /* --ops: 1-based Operations Manager positions (as nci_dump); default every operation */
        int *opl = malloc(sizeof(int) * (size_t)(mf.nops + 1)), nop = 0;
        if (ops) { for (const char *c = ops; *c;) { int k = atoi(c); if (k >= 1 && k <= mf.nops) opl[nop++] = k - 1; c = strchr(c, ','); if (!c) break; c++; } }
        else for (int i = 0; i < mf.nops; i++) opl[nop++] = i;
        if (!nci_out) { snprintf(tmpnci, sizeof tmpnci, "%s.nci", out); nci_out = tmpnci; }
        FILE *f = fopen(nci_out, "w");
        if (!f) { perror(nci_out); return 1; }
        char e2[512] = "";
        if (!mp_nci_write_part(&mf, opl, nop, f, e2, sizeof e2)) { fprintf(stderr, "mp_cli: NCI: %s\n", e2); return 1; }
        fclose(f);
        env.part_path = in;
        env.prm_ctx = &mf;   /* mp_prm (MD/CD/op parameters) */
        nci = fopen(nci_out, "r");
#else
        fprintf(stderr, "mp_cli: part input needs nci_ascii (not linked); give an .nci file\n"); return 1;
#endif
    } else if (in) {
#ifdef MP_HAVE_NCI_ASCII
        if (partopt) {   /* --part: MD / CD / op parameters from a part while the NCI comes from a file (ours) */
            static McamFile pf;
            if (!mcam_load(&pf, partopt)) { fprintf(stderr, "mp_cli: cannot load %s\n", partopt); return 1; }
            env.prm_ctx = &pf; env.part_path = partopt;
        }
#endif
        nci = fopen(in, "r");
        if (!nci) { perror(in); return 1; }
    }
#ifdef MP_HAVE_NCI_ASCII
    else if (partopt) {   /* --call mode with --part: rd_cd$ / rd_md$ / rd_params$ read the part (ours) */
        static McamFile pf2;
        if (!mcam_load(&pf2, partopt)) { fprintf(stderr, "mp_cli: cannot load %s\n", partopt); return 1; }
        env.prm_ctx = &pf2; env.part_path = partopt;
    }
#endif
    FILE *o = fopen(out, "wb");
    if (!o) { perror(out); return 1; }
    env.nc_path = out; env.pst_path = pst; env.nci_path = in;
    MpRun *r = mp_run_new(p, &env, o, err, sizeof err);
    if (!r) { fprintf(stderr, "mp_cli: %s\n", err); return 1; }
    r->opt_trace = trace;
    for (int i = 0; i < nsets; i++) {
        char nm[256]; const char *eq = strchr(sets[i], '=');
        if (!eq) continue;
        snprintf(nm, sizeof nm, "%.*s", (int)(eq - sets[i]), sets[i]);
        int vi = mp_var_find(r, nm);
        if (vi >= 0) { r->vars[vi].v[0] = atof(eq + 1); continue; }
        int si = mp_str_find(r, nm);
        if (si >= 0) mp_str_set(r, si, eq + 1); else fprintf(stderr, "mp_cli: --set: unknown %s\n", nm);
    }
    int ok = 1;
    if (calls) {
        char buf[1024]; snprintf(buf, sizeof buf, "%s", calls);
        for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
            int b = mp_block_find(r, t);
            if (b < 0) { fprintf(stderr, "mp_cli: no postblock %s\n", t); ok = 0; continue; }
            if (!mp_block_call(r, b)) { ok = 0; break; }
        }
    } else {
#ifdef MP_HAVE_NCILOOP
        ok = mp_run_nci(r, nci);
#else
        fprintf(stderr, "mp_cli: NCI loop not linked\n"); ok = 0;
#endif
    }
    if (r->linelen) mp_out_eob(r, 0);   /* ours: flush a pending line */
    fclose(o);
    if (nci) fclose(nci);
    if (mp_run_error(r)) fprintf(stderr, "mp_cli: %d run message(s); last: %s\n", r->nerrors, mp_run_error(r));
    mp_run_free(r);
    return ok ? 0 : 1;
}
