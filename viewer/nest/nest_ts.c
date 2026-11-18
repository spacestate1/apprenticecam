/* nest_ts.c - TrueShape engine port: entry point (see nest_ts.h). */
#include "nest_ts.h"
#include "ts/build/ts_gen.h"
#include <stdio.h>

#define IMG(a) ((void *)(ts_img + ((a) - TS_IMG_LO)))

/* The DLL's C++ static initialisers, run by the CRT's _cinit (L246900-246917: the table 0x180261398..0x1802614b8,
 * in table order) when the DLL is loaded.  Their atexit registrations (destructors at process exit) are not
 * reproduced.  Skipped: the STL's own statics in the same table (std::_Init_locks FUN_180260a18 / FUN_180260a74 and
 * the unnamed STL stubs 0x180260a94, 0x180260a5c, 0x180260a68, 0x180260a38, 0x180260a44, 0x180260a50). */
static void ts_static_init(void) {
  FUN_180001ea0(IMG(0x1802afc60ULL));                 /* FUN_1802605a0 L290817 .. FUN_180260a00 L291094 */
  FUN_180001f60(IMG(0x1802afda0ULL));                 /* FUN_1802605d0 */
  FUN_1800105c0((longlong)IMG(0x1802b3420ULL));       /* FUN_180260600 */
  FUN_180033470(IMG(0x1802b3e00ULL));                 /* FUN_180260630 */
  FUN_180051ee0(IMG(0x1802b3410ULL));                 /* FUN_180260660 */
  FUN_180060ae0(IMG(0x1802b340cULL));                 /* FUN_180260690 */
  FUN_1800105c0((longlong)IMG(0x1802b4340ULL));       /* FUN_1802606b0 */
  FUN_180061d40(IMG(0x1802b4d20ULL));                 /* FUN_1802606e0 */
  FUN_18007b750(IMG(0x1802b4ec0ULL));                 /* FUN_180260710 partToBeNested::partToBeNested */
  FUN_1800105c0((longlong)IMG(0x1802b5420ULL));       /* FUN_180260730 */
  FUN_1800105c0((longlong)IMG(0x1802b6370ULL));       /* FUN_180260760 */
  FUN_18007b750(IMG(0x1802b5e10ULL));                 /* FUN_180260790 */
  FUN_180020190((longlong)IMG(0x1802b6d70ULL));       /* FUN_1802607b0 */
  FUN_18001fe00(IMG(0x1802b7700ULL));                 /* FUN_1802607d0 */
  FUN_18007b750(IMG(0x1802f26e0ULL));                 /* FUN_1802607f0 */
  FUN_18007b750(IMG(0x1802f2cf0ULL));                 /* FUN_180260810 */
  FUN_1800105c0((longlong)IMG(0x1802f3250ULL));       /* FUN_180260830 */
  FUN_1800105c0((longlong)IMG(0x1802f4450ULL));       /* FUN_180260860 */
  for (int i = 0; i < 5000; i++)                      /* FUN_180260890: eh vector constructor iterator, 0x1a0 x 5000 */
    FUN_1801e42c0((longlong)IMG(0x1802f5240ULL) + (longlong)i * 0x1a0);
  FUN_1801f6940(IMG(0x1804f1080ULL));                 /* FUN_1802608d0 */
  FUN_1801fd280(IMG(0x1804f1120ULL));                 /* FUN_180260900 */
  FUN_1801f54c0(IMG(0x1804f11b0ULL));                 /* FUN_180260930 */
  FUN_1801ffae0(IMG(0x1804f1180ULL));                 /* FUN_180260960 */
  FUN_1801fdb10(IMG(0x1804f1b40ULL), 0);              /* FUN_180260990 */
  FUN_180212cf0(IMG(0x1804f1b60ULL));                 /* FUN_1802609b0 */
  FUN_1801fdb10(IMG(0x1804f1bd8ULL), 0);              /* FUN_1802609e0 */
  FUN_1801fdb10(IMG(0x1804f1bd0ULL), 0);              /* FUN_180260a00 */
}

/* loading the DLL: its static data, the CRT, the C++ static initialisers, DllMain - once per process, as
   NESTING.DLL keeps n64dll.dll loaded for the whole Mastercam session */
static int loaded;
/* ours: the engine's state is abandoned - what it holds is released (ts_crt_release) and the next ts_engine_load
   loads the image again */
void ts_engine_reset(void) { loaded = 0; ts_eh_top = NULL; ts_crt_release(); }
void ts_engine_load(void) {
  if (loaded) return;
  ts_crt_release();                      /* a load that stopped half way (ts_fatal) */
  ts_img_init();
  ts_crt_init();
  ts_static_init();
  FUN_180001150(0x180000000ULL, 1, 0);   /* DllMain(hinst, DLL_PROCESS_ATTACH, 0) (called by __DllMainCRTStartup L245528) stores the module handle = image base */
  loaded = 1;                            /* only once complete */
}

int ts_run_nip(const char *nipPath, int *status) {
  ts_engine_load();
  *status = 0;
  return (int)FUN_180001220((char *)nipPath, (void *)status, 0);   /* runNestCommon L162 */
}
