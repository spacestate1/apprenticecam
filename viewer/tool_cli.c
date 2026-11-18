/* tool_cli - tool library harness: list a .tooldb/.tools library, convert it to .tools, list found libraries.
 * Usage: tool_cli find | tool_cli LIB [--save OUT.tools] [--holders] | tool_cli PART.mcx-9 --part */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "toollib.h"

static void show(const Tool *t) {
    printf("T%-4d %-15s D%-7.3f r%-6.3f tip%-5.0f fl%-6.1f oal%-6.1f z%d S%-6d F%-8.1f pl%-7.1f H%d%s%s%s  %s\n", t->num, tool_type_name(t->type),
           t->dia, t->corner_r, t->tip_angle, t->flute_len, t->oal, t->flutes, t->rpm, t->feed, t->plunge, t->len_off,
           t->coolant ? " cool" : "", t->holder[0] ? " holder=" : "", t->holder, t->name);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: see source\n"); return 1; }
    if (!strcmp(argv[1], "find")) { char **p; int n = toollib_find(&p); for (int i = 0; i < n; i++) printf("%s\n", p[i]); return 0; }
    if (argc > 2 && !strcmp(argv[2], "--part")) {
        McamFile f; if (!mcam_load(&f, argv[1])) return 1;
        for (int i = 0; i < f.ntools; i++) show(&f.tools[i]);
        return 0;
    }
    ToolLib L; char err[256];
    if (!toollib_load(argv[1], &L, err, sizeof err)) { printf("FAILED: %s\n", err); return 1; }
    printf("%s: %d tools, %d holders, %s\n", argv[1], L.n, L.nh, L.metric == 1 ? "mm" : L.metric == 0 ? "inch" : "units?");
    for (int i = 0; i < L.n; i++) show(&L.t[i]);
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--holders")) for (int k = 0; k < L.nh; k++) printf("holder %-24s %d pts, top %.1f\n", L.h[k].name, L.h[k].n, L.h[k].rz[2 * L.h[k].n - 1]);
        if (!strcmp(argv[i], "--save") && i + 1 < argc) { toollib_save(argv[++i], &L); printf("wrote %s\n", argv[i]); }
    }
    toollib_free(&L);
    return 0;
}
