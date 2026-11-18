/* nest_rect_main.c - test driver: run the ported Rectangular engine on a .nip and write the .nop (the counterpart
 * of `tools/peloader/peloader n64dll.dll --nip X.nip --out X.nop`). */
#include "nest_rect.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  const char *nip = NULL, *out = NULL; char err[512] = ""; int fillers = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "--fillers")) fillers = 1;
    else nip = argv[i];
  }
  if (!nip || !out) { fprintf(stderr, "usage: nest_rect_test X.nip --out X.nop [--fillers]\n"); return 2; }
  if (!rn_run_nip(nip, out, fillers, err, sizeof err)) { fprintf(stderr, "nest_rect_test: %s\n", err); return 1; }
  return 0;
}
