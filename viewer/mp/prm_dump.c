/* prm_dump - print what mp_prm delivers for a part: the rd_cd$ / rd_md$ (0) / rd_tlpathgrp$ / op parameter calls
 * (prmcode$ + sparameter$, one line per pmachineinfo$ / pparameter$ call) and the CD / MD re-initialisations.
 * usage: prm_dump PART [cd|md [ENT [NAME]]|mdd ENT|mdinfo|grp|op N|ovr [AXCOMBO]|all]   (ours: test CLI)
 *   md ENT: rd_md$ with rd_mch_ent_no$ = ENT (-2: NAME = sparameter$); mdd ENT: the same as a dump read (pdump$);
 *   mdinfo: the decoded machine tree / axis combinations; ovr AXCOMBO: + InitialAssignFromMD's rotary part */
#include "mp_prm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int ncalls;
static int cb(void *ctx, int code, const char *s) { (void)ctx; printf("  %5d  %s\n", code, s); ncalls++; return 0; }
static void run(const char *what, int r) { printf("== %s: %d calls, result %d\n\n", what, ncalls, r); ncalls = 0; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s PART [cd|md [ENT [NAME]]|mdd ENT|mdinfo|grp|op N|ovr [AXCOMBO]|all]\n", argv[0]); return 2; }
    McamFile *f = calloc(1, sizeof *f);
    if (!mcam_load(f, argv[1])) { fprintf(stderr, "%s\n", mcam_error()); return 1; }
    const char *w = argc > 2 ? argv[2] : "all";
    MpPrmFound fd = mp_prm_found(f);
    printf("# %s: machine %d control %d pages %#x group %d ops %d\n", argv[1], fd.mach, fd.ctl, fd.pages, fd.grp, f->nops);
    int all = !strcmp(w, "all");
    if (all || !strcmp(w, "cd")) { printf("== rd_cd$\n"); run("rd_cd$", mp_prm_cd(f, cb, NULL)); }
    if (all || !strcmp(w, "md") || !strcmp(w, "mdd")) {
        int ent = argc > 3 ? atoi(argv[3]) : 0, dump = !strcmp(w, "mdd");
        printf("== rd_md$ (rd_mch_ent_no$ = %d%s)\n", ent, dump ? ", dump" : "");
        run("rd_md$", mp_prm_md2(f, ent, dump, argc > 4 ? argv[4] : "", cb, NULL));
    }
    if (!strcmp(w, "mdinfo")) { static char b[1 << 16]; mp_md_info(f, b, sizeof b); fputs(b, stdout); }
    if (all || !strcmp(w, "grp")) { printf("== rd_tlpathgrp$\n"); run("rd_tlpathgrp$", mp_prm_group(f, cb, NULL)); }
    if (all || !strcmp(w, "op")) {
        int k0 = argc > 3 ? atoi(argv[3]) : 0, k1 = argc > 3 ? k0 + 1 : (f->nops ? 1 : 0);
        for (int k = k0; k < k1; k++) { printf("== op %d (id %d opcode %d %s)\n", k, f->ops[k].id, f->ops[k].opcode, f->ops[k].type); run("op", mp_prm_op(f, k, cb, NULL)); }
    }
    if (all || !strcmp(w, "ovr")) {
        int ac = !all && argc > 3 ? atoi(argv[3]) : 0;
        MpOverride o[512]; int n = mp_cd_overrides_ac(f, ac, o, 512);
        printf("== rotary part (axis combination %d): %d\n", ac, mp_md_rotary(f, ac, o, 0));
        printf("== CD / MD re-initialisation (quiet %d): %d\n", mp_cd_quiet(f), n);
        for (int i = 0; i < n && i < 512; i++)
            printf("  %-18s %-14.15g flags %#04x msg %#x bit %#x%s  [%s]\n", o[i].var, o[i].value, o[i].flags, o[i].msg, o[i].bit,
                   o[i].arg ? " (comm_filter arg)" : "", o[i].source);
        double s, i2, m; if (mp_cd_seq(f, &s, &i2, &m)) printf("  seq start %g inc %g max %g\n", s, i2, m);
    }
    return 0;
}
