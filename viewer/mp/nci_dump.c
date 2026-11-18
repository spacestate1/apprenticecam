/* nci_dump - write the ASCII .nci Mastercam's bnci_to_nci makes for a part's stored toolpaths (nci_ascii.c).
 * Usage: nci_dump PART.mcam|PART.mcx-9 [--ops 1,2,...] [--places N] [--rpm N] [--axis-comb ID] [--crlf] [--stats] > out.nci
 *   --ops: 1-based Operations Manager positions (default: every op).  NOT DECODED warnings go to stderr. (ours) */
#include "nci_ascii.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = NULL, *ops = NULL; int stats = 0, crlf = 0, places = -1, rpm = -1, axc = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ops") && i + 1 < argc) ops = argv[++i];
        else if (!strcmp(argv[i], "--places") && i + 1 < argc) places = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rpm") && i + 1 < argc) rpm = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--axis-comb") && i + 1 < argc) axc = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--stats")) stats = 1;
        else if (!strcmp(argv[i], "--crlf")) crlf = 1;
        else if (argv[i][0] != '-' && !path) path = argv[i];
        else { fprintf(stderr, "usage: %s PART [--ops 1,2] [--places N] [--rpm N] [--axis-comb ID] [--crlf] [--stats]\n", argv[0]); return 2; }
    }
    if (!path) { fprintf(stderr, "usage: %s PART [--ops 1,2] [--places N] [--rpm N] [--axis-comb ID] [--crlf] [--stats]\n", argv[0]); return 2; }
    static McamFile f;
    if (!mcam_load(&f, path)) { fprintf(stderr, "%s: %s\n", path, mcam_error()); return 1; }
    int *idx = malloc(sizeof(int) * (size_t)(f.nops + 1)), n = 0;
    if (ops) {
        for (const char *p = ops; *p; ) {
            int k = atoi(p); if (k >= 1 && k <= f.nops) idx[n++] = k - 1;
            const char *q = strchr(p, ','); if (!q) break; p = q + 1;
        }
    } else for (int i = 0; i < f.nops; i++) idx[n++] = i;
    MpNciEnv env; mp_nci_env_default(&env, &f);
    if (places >= 0) env.n_places_nci = places;
    env.crlf = crlf;
    if (rpm >= 0) { env.max_spindle_rpm = rpm; env.have_max_rpm = 1; }
    if (axc >= 0) { env.axis_comb_id = axc; env.have_axis_comb = 1; }
    MpNciStats st; char err[256] = "";
    int ok = mp_nci_write_part_env(&f, &env, idx, n, stdout, &st, stderr, err, sizeof err);
    if (!ok) fprintf(stderr, "%s: %s\n", path, err);
    if (stats) fprintf(stderr, "%s: ops %ld files %ld records %ld lines %ld unknown %ld not_decoded %ld\n", path, st.ops, st.files,
                       st.records, st.lines, st.unknown, st.not_decoded);
    free(idx); mcam_free(&f);
    return ok ? 0 : 1;
}
