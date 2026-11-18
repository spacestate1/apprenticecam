/* nest_ts.h - port of NestLib's TrueShape nesting engine (n64dll.dll, Mastercam 2017 Geometry Nesting, TrueShape mode).
 *
 * How it is made (formats/nesting_engine.md; user decisions N-1 / N-2): the engine is ~150k decompiled lines reachable
 * from Mastercam's API calls, so it is transliterated function by function from Ghidra's decompile of n64dll.dll
 * (viewer/nest/ts/gen/gen.py -> ts/build/ts_gen.c; every function carries its address and its line in
 * decomp/nest/n64dll_all.c), with hand fixes where the decompile is not C (viewer/nest/ts/fix/FUN_<addr>.c), and runs
 * on a byte image of the DLL's static data (ts/ts_rt.h) so the decompile's global names keep their aliasing.  The CRT
 * functions it calls are mapped in ts/ts_crt.h (sin / cos transliterated from the DLL's libm, ts/ts_libm.c; printf
 * number formatting and qsort from nest_rect.c).  Functions not reached by the supported cases stop with
 * "NOT PORTED: FUN_<addr>" instead of guessing.
 *
 * Status (2026-11-05, milestone 2): byte-identical .nop vs the original on every generated oracle case
 * (tools/tests/nest_ts_oracle.sh: 67/67 default seed, 127/127 -n 100 -s 2, 177/177 -n 150 -s 3; unseen seed 227/227 -n 200 -s 7): rectangles,
 * DXF lines / arcs / circles, holes, rings, rotation steps 90 / 45 / 10 / 180 / 360, priorities, min / max quantities,
 * several sheets, sheet collar, resolution 1 / 0.25, clearance and cutter (part offset with exact arcs), and
 * Mastercam's "Optimized pairs" clusters (.nip PART/CLUSTER TYPE 7).  Functions the oracle cases never reach stay
 * "NOT PORTED" stubs; decompile hazards still open are listed at the end of ts/build/ts_gen.h.
 * The DLL's C++ try / catch blocks are generated from its exception data (ts/gen/eh.py), the error messages' arguments
 * from the call sites' machine code (ts/gen/vaargs.py); errors, ts_fatal recovery and resources: formats/nesting_engine.md §9.
 * The engine is process-global (one image, one thread), as the DLL in Mastercam's session.
 *
 * Entry: the .nip runner of the DLL (runNestCommon @180001220 L162 -> FUN_180001a00 L404: NipReader, NNestOrder,
 * the .nop writer FUN_1800022f0 L698), i.e. the counterpart of `tools/peloader/peloader n64dll.dll --nip X.nip`. */
#ifndef NEST_TS_H
#define NEST_TS_H

/* run the .nip like runNestCommon(path, &status, NULL); returns runNestCommon's value (1 = ok) and the status */
int ts_run_nip(const char *nipPath, int *status);
/* the DLL load (static data, CRT and C++ static initialisers, DllMain): once per process */
void ts_engine_load(void);
/* ours: the next ts_engine_load loads the image again (after a run stopped by ts_fatal: the engine's state is undefined) */
void ts_engine_reset(void);

#endif
