/* nest_rect.h - port of NestLib's Rectangular nesting engine (n64dll.dll "optiRectOrder", Mastercam 2017 Geometry
 * Nesting, rectangular mode).  Bit-exact port of the original; every behaviour cites the decompile
 * decomp/nest/n64dll_all.c ("L<n>" = line, FUN_ = address in n64dll.dll).  Map: formats/nesting_engine.md section 5.
 *
 * API = the RN* subset Mastercam's NESTING.DLL imports (objdump -p nesting.dll) + the queries runNestCommon's .nop
 * writer uses.  Original call sequence: RNStartSetParams / RNSet* / RNEndSetParam -> rn_new; RNAddPartInfo2 ->
 * rn_add_part; RNSetPartPriority -> rn_set_part_priority; RNAddSheetInfo2 -> rn_add_sheet; RNNestOrder -> rn_nest_order
 * (callbacks fire during it); RNEndRectangleNest -> rn_free.  One rn_ctx = the original's globals + optiRectOrder object.
 */
#ifndef NEST_RECT_H
#define NEST_RECT_H

typedef struct rn_ctx rn_ctx;
/* linked into the program next to core_ui, whose Renumber dialog has a global int rn_new (cui_toolmgr.c): the engine's
   constructor is linked as nest_rn_new (2026-11-05, Geometry Nesting; the name in the source stays rn_new) */
#define rn_new nest_rn_new

/* callbacks (RNSet*CB, L105414-105476: stored in DAT_1802f2c40..2c70); NULL = not set.  Return 0 = failure as in the
 * original (the sheet-result / new-sheet return value aborts the sheet, FUN_1801b7c30 L194279 / L194652). */
typedef struct {
  void *user;
  int (*error)(void *user, int code, int sev, const char *msg);              /* FUN_1801b6640 L192818 */
  int (*new_sheet)(void *user, const char *sheet, double len, double wid);   /* FUN_1801b6690 L192834 via FUN_1801c0b30 */
  int (*part_nested)(void *user, const char *part, double x, double y, int rotated); /* FUN_1801b6730 L192858 */
  int (*sheet_result)(rn_ctx *c, void *user, int copies, double util);       /* FUN_1801b67d0 L192882 */
  int (*order_result)(rn_ctx *c, void *user, int sheetTypes, int sheets);   /* FUN_1801b6830 L192900 */
} rn_callbacks;

/* RNEndSetParam -> FUN_1800e7010 L105536 -> ctor FUN_1801b6890 L192918.  resolution is stored by RNSetResolution
 * (FUN_1800e6ff0) but the ctor stores the constant 1.0 (asm 0x1801b6a0a) - kept only for RNInqResolution's caller. */
rn_ctx *rn_new(double cutter, double clearance, double resolution, const rn_callbacks *cb);
void rn_free(rn_ctx *c);                                                        /* RNEndRectangleNest FUN_1800e7800 */
void rn_set_fillers_upto_nested_height(rn_ctx *c, int on);                     /* FUN_1800e7920 L105910 */
void rn_set_method_type(rn_ctx *c, int m);                                      /* FUN_1800e6fb0 (!= 0: RNNestOrder no-op) */
int rn_add_part(rn_ctx *c, const char *name, int minQty, int maxQty, double len, double wid, int rotate, char grain);
int rn_set_part_priority(rn_ctx *c, int prio);                                  /* FUN_1801b7370 L193144: last part */
int rn_add_sheet(rn_ctx *c, const char *name, int qty, double len, double wid, double collar, char grain);
int rn_nest_order(rn_ctx *c);                                                   /* FUN_1801b7960 L193352 */

/* queries (FUN_1801c0d30..FUN_1801c1d60 L196768-197278); return 0 + error callback when called in the wrong state */
int rn_inq_nested_sheet(rn_ctx *c, char *name, double *len, double *wid, int *zero, int *nInst);
int rn_inq_sheet_total_nested(rn_ctx *c, char *name, int *copies);             /* iterator, 0 at end */
int rn_inq_nested_part(rn_ctx *c, int i, char *name, double *x, double *y, int *rot);
int rn_inq_nested_instance(rn_ctx *c, const char *name, int k, double *x, double *y, int *rot);
int rn_inq_nested_part_type(rn_ctx *c, int i, char *name, double *len, double *wid, int *n);
int rn_inq_total_input_part_types(rn_ctx *c);
int rn_inq_nested_part_instances(rn_ctx *c, char *name, int *n);               /* iterator, 0 at end */
void rn_inq_params(rn_ctx *c, double *cutter, double *clearance, double *resolution);

/* MSVC 2010 CRT pieces the DLL links statically (bit-exactness, nesting_engine.md section 6) */
void msvc_srand(unsigned seed);                                                 /* FUN_18022bc38 L251038 */
int msvc_rand(void);                                                            /* rand @18022bc50 L251051 */
void msvc_qsort(void *base, unsigned long long n, unsigned long long w, int (*cmp)(const void *, const void *));
/* printf "%<w>.<p>lf" as the DLL's CRT: _cftof_l L257931 -> _fltout2 (17 digits, FUN_18023dbd8) -> _fptostr L266895 */
int msvc_fmt_f(char *out, int outsz, double v, int width, int prec);

/* .nip subset reader (rectangular runNestCommon path, FUN_180024b20 L19926) and .nop writer (FUN_1800052c0 L1823 /
 * FUN_180005a00 L1965), in nest_rect_io.c */
/* fillers = 1: as if the client called RNSetNestFillersUptoNestedHeight(1) (Mastercam does; the .nip reader never does) */
int rn_run_nip(const char *nipPath, const char *nopPath, int fillers, char *err, int errsz);

#endif
