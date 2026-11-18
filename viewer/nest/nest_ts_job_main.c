/* nest_ts_job_main.c - test driver: run the TrueShape back-end (nest_engine_ts) on NestJob text files and write NestOut
 * (nest_job_io.c), the counterpart of `tools/peloader/peloader n64dll.dll --job JOB --jobout OUT ...`
 * (tools/tests/nest_ts_job_oracle.sh).  Several JOB OUT pairs run one after the other in this process (the engine keeps
 * its state between runs as the DLL does in Mastercam's session).  NEST_TS_DIR (default ".") is the directory handed to
 * NOpenNestLib.  TS_FATAL_AT=<hex function>[:<n>] (test hook, ts_rt.c) stops the run at the n-th entry of that function
 * as a NOT PORTED function would. */
#include "nest_job_io.h"
#include <stdio.h>
#include <stdlib.h>

/* NEST_CANCEL_AT=n: Esc at the n-th progress call of each run (tools/peloader/job.c the same) */
static long cancel_at = -1, nprogress;
static int cancel_hook(void) { return ++nprogress == cancel_at; }

int main(int argc, char **argv) {
  if (argc < 3 || argc % 2 != 1) { fprintf(stderr, "usage: nest_ts_job_test JOB OUT [JOB OUT ...]\n"); return 2; }
  if (!getenv("NEST_TS_DIR")) setenv("NEST_TS_DIR", ".", 1);
  int ok = 0;
  for (int a = 1; a + 1 < argc; a += 2) {
    NestJob j; if (!nest_job_read(argv[a], &j)) { fprintf(stderr, "nest_ts_job_test: cannot read %s\n", argv[a]); return 2; }
    NestOut o = { 0 }; char err[512] = "";
    if (getenv("NEST_CANCEL_AT")) { cancel_at = atol(getenv("NEST_CANCEL_AT")); nprogress = 0; nest_ts_cancel = cancel_hook; }
    ok = nest_engine_ts(&j, &o, err, sizeof err);
    nest_out_write(argv[a + 1], ok, &o, err);
    if (!ok && err[0]) fprintf(stderr, "%s\n", err);
    nest_out_free(&o); nest_job_free(&j);
  }
  return ok ? 0 : 1;
}
