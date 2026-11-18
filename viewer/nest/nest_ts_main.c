/* nest_ts_main.c - test driver: run the TrueShape port on a .nip in the current directory; the engine writes the
 * .nop named by the .nip (SETTINGS/FILES/OUTPUT/RESULT), as tools/peloader does with the original DLL. */
#include "nest_ts.h"
#include <stdio.h>

int main(int argc, char **argv) {
  if (argc != 2) { fprintf(stderr, "usage: nest_ts_test X.nip\n"); return 2; }
  int status = 0, r = ts_run_nip(argv[1], &status);
  printf("runNestCommon(%s) = %d, status = %d\n", argv[1], r, status);
  return r ? 0 : 1;
}
