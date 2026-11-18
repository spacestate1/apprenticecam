/* nest_ts_job.c - the TrueShape back-end of Geometry Nesting (nest_job.h): the N* calls NESTING.DLL makes, in its order and
 * with its values, on the port of NestLib (nest_ts.c + ts/, the DLL's exports are the FUN_<addr> below), and NESTING.DLL's
 * engine callbacks answered from the NestJob. Citations: decomp/nest/nesting_all.c (nesting.dll) L<n> / FUN_<addr>,
 * "asm @" = objdump of nesting.dll; the n64dll export names from decomp/nest/n64dll_exports.txt.
 *
 * Sequence (FUN_18003fb40 L44096-44119): FUN_180043040 NOpenNestLib(dataDir, 1, FUN_180006cf0) L46482; FUN_1800411f0 the
 * callbacks L45081; FUN_180039c90 the parameters L39833; FUN_18003a120 NStartNewPartList / parts FUN_180037400 /
 * NEndNewPartList L40029; FUN_18003e870 NStartNestOrder / sheets FUN_18003a3f0 / NNestOrder L43175; FUN_180039050
 * NCloseNestLib. Optimized pairs: the NStartCluster / NAddClusterElement / NEndCluster after a pair part's NAddPartInfo
 * (FUN_180037400 L38278-38307). Groups and user clusters (FUN_180036950 / FUN_180035f90) are not part of NestJob. */
#define _GNU_SOURCE
#include "nest_job.h"
#include "nest_ts.h"
#include "ts/build/ts_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* the N* exports (n64dll_exports.txt) */
#define NOpenNestLib              FUN_18000ab40
#define NStartSettingCallBacks    FUN_18000b100
#define NEndSettingCallBacks      FUN_18000bbc0
#define NSetErrorHandlerCB        FUN_18000b260
#define NSetProgressFunctionCB    FUN_18000b3c0
#define NSetNewPartGeomHandleCB   FUN_18000b510
#define NSetNewSheetGeomHandleCB  FUN_18000b7d0
#define NSetNestedOrderResultCB   FUN_18000ba70
#define NSetNestedSheetResultCB   FUN_18000b920
#define NStartSetParams           FUN_18000bd20
#define NSetDLLEnableKey          FUN_1800158c0
#define NSetResolution            FUN_18000bea0
#define NSetPartDistType          FUN_18000cd50
#define NSetClearance             FUN_18000cbd0
#define NSetThicknessAndMaterialSelection FUN_18000df80
#define NSetNestFillersUptoNestedHeight   FUN_180011c90
#define NSetFitPartInPart         FUN_18000d640
#define NSetGrpMaxElmtSize        FUN_180013f00
#define NSetExactDistance         FUN_18000d930
#define NSetAccuNest              FUN_1800149a0
#define NSetGuillotineCut         FUN_180013120
#define NSetClusterCutoffUtil     FUN_18000dc40
#define NSetCutterDiameter        FUN_18000c190
#define NSetFitClusterFirst       FUN_18000d4b0
#define NSetFitMaxOnLast          FUN_18000d340
#define NEndSetParam              FUN_18000e330
#define NStartNewPartList         FUN_18000e470
#define NAddPartInfo              FUN_18000ebf0
#define NEndNewPartList           FUN_18000f860
#define NStartCluster             FUN_180121740
#define NAddClusterElement        FUN_180121d80
#define NEndCluster               FUN_180121f80
#define NStartNestOrder           FUN_180010070
#define NAddSheetInfo             FUN_180010ad0
#define NAddSheetPriority         FUN_180011890
#define NNestOrder                FUN_1800102d0
#define NCloseNestLib             FUN_180011a30
#define NDwgGeomStart             FUN_180015d40
#define NDwgProfsStart            FUN_1800160d0
#define NDwgClosedProfStart       FUN_180017410
#define NDwgAddLine               FUN_1800181b0
#define NDwgAddArc                FUN_1800192c0
#define NDwgAddCircle             FUN_180019da0
#define NDwgChangeCutterDia       FUN_180017fd0
#define NDwgClosedProfEnd         FUN_18001aa70
#define NDwgProfsEnd              FUN_18001a590
#define NDwgGeomEnd               FUN_18001b110
#define NInqNestedSheet           FUN_18002f0e0
#define NInqNestedParameters      FUN_18002fac0
#define NInqNestedPartType        FUN_18002d700
#define NInqNestedInstance        FUN_18002e270
#define NInqLastNestedPart        FUN_18002fee0
#define NInqLastNestedPartType    FUN_18002dda0
#define NInqLastNestedInstance    FUN_18002e970
#define NInqNextPartType          FUN_1800314b0

/* the key NESTING.DLL passes (nesting.dll .data 0x1801301f0, FUN_180039c90 L39915) = the one the DLL checks
   (FUN_180061000 L48981) and its own .nip runner passes (FUN_1800253e0) */
static char ts_key[] = "023FED93085B7F3B101E5AF5AD65066AD185C62AC803D633079B8C3C2F582037D41580E1076BC8B5F8F3";

static struct { const NestJob *job; NestOut *out; char *err; int errsz; int nresults; NestOutInst *pend; } U;   /* pend: ours, the instances of the result being read (freed if a ts_fatal stops the run) */

/* FUN_1800097c0: ns_parse_prtname + GetPartPtr(id) (FUN_180039750) - the job part with that id; -1 when either fails */
static int part_of_name(const char *n, long *elem) {
  long id = 0; int type = 0; *elem = -1;
  if (!nest_parse_prtname(n, &id, &type, elem)) return -1;
  for (int i = 0; i < U.job->nparts; i++) if (U.job->parts[i].id == id) return i;
  return -1;
}
static int sheet_index(const char *n) { for (int i = 0; i < U.job->nsheets; i++) if (!strcmp(U.job->sheets[i].name, n)) return i; return -1; }

/* the profile entities of the geometry callbacks (FUN_180006f90 L4996-5085 / FUN_1800074b0) */
static void dwg_profile(const NestProfile *q, int cutter, double cutterDia) {
  NDwgClosedProfStart((uint)(q->outer ? 1 : 0));
  for (int k = 0; k < q->n; k++) {
    const NestDwgEnt *e = &q->e[k];
    if (e->kind == 0) NDwgAddLine(e->x1, e->y1, e->x2, e->y2);
    else if (e->kind == 1) NDwgAddArc(e->cx, e->cy, e->x1, e->y1, e->sweep);
    else NDwgAddCircle(e->cx, e->cy, e->r);
  }
  if (cutter) NDwgChangeCutterDia(cutterDia);       /* parts only (L5096; FUN_1800073c0 asm @180007463) */
  NDwgClosedProfEnd();
}
/* FUN_180006f90 L4915: the new-part-geometry callback. The engine name parses back to the part (FUN_1800097c0) - else 0.
   A rectangular part goes through FUN_1800073c0 (one outer profile of 4 lines, cutter, asm @1800073c0-1800074a9); the
   job carries that profile, so both are the same call sequence here */
static uint64_t cb_part_geom(uint64_t name_, uint64_t a2, uint64_t a3, uint64_t a4) {
  (void)a2; (void)a3; (void)a4;
  long elem; int pi = part_of_name((const char *)name_, &elem);   /* FUN_1800097c0 (L4934); elements: user clusters only */
  if (pi < 0) return 0;
  const NestJobPart *p = &U.job->parts[pi];
  if (!NDwgGeomStart() || !NDwgProfsStart(0)) { NCloseNestLib(); return 0; }   /* L4981-4982 / asm @18000748e */
  for (int i = 0; i < p->nprof; i++) dwg_profile(&p->prof[i], 1, p->cutterDia);
  NDwgProfsEnd(); NDwgGeomEnd();
  return 1;
}
/* FUN_1800074b0 L5150: the new-sheet-geometry callback: NDwgProfsStart(1), outer contours then holes, no cutter */
static uint64_t cb_sheet_geom(uint64_t name_, uint64_t a2, uint64_t a3, uint64_t a4) {
  (void)a2; (void)a3; (void)a4;
  int si = sheet_index((const char *)name_);
  if (si < 0 || U.job->sheets[si].nprof == 0) return 0;       /* "(callback for sheet with no contours)" L5233 */
  const NestJobSheet *s = &U.job->sheets[si];
  if (!NDwgGeomStart()) { NCloseNestLib(); return 0; }
  if (!NDwgProfsStart(1)) { NDwgGeomEnd(); NCloseNestLib(); return 0; }
  for (int pass = 1; pass <= 2; pass++)                       /* local_294 = 1, 2 (L5240-5375): code 0 then code 1 */
    for (int i = 0; i < s->nprof; i++) if ((s->prof[i].outer != 0) == (pass == 1)) dwg_profile(&s->prof[i], 0, 0);
  NDwgProfsEnd(); NDwgGeomEnd();
  return 1;
}
/* FUN_180006cf0: the error handler shows the message (FUN_180038ea0) and returns 1; ours: keep the last message */
static uint64_t cb_error(uint64_t code, uint64_t sev, uint64_t kind, uint64_t msg) {
  (void)code; (void)sev; (void)kind;
  if (msg) snprintf(U.err, (size_t)U.errsz, "%s", (const char *)msg);
  return 1;
}
/* FUN_180006700: the progress callback (Esc -> 0: no keyboard here). Code 1: NInqLastNestedPart / PartType / Instance,
   0 past 50000 instances of the part type (asm @1800067c6-18000684b); code 0x13: NInqNextPartType; 0x14 / 0xe only with
   the local-area mode (DAT_180135e89, never on for geometry: nesting_addin.md 6.6); returns 1 */
int (*nest_ts_cancel)(void);
static uint64_t cb_progress(uint64_t code, uint64_t text) {
  (void)text;
  if (nest_ts_cancel && nest_ts_cancel()) return 0;                       /* KeyPress(0x1b) -> 0 (L4613-4616) */
  if ((int)code == 1) {
    uint part = 0, inst = 0; undefined4 cnt = 0; char nm[1024] = ""; undefined8 d[7] = { 0 }; undefined4 i1 = 0, i2 = 0; undefined8 x = 0;
    NInqLastNestedPart(&part, &inst);
    NInqLastNestedPartType(part, nm, &cnt);
    NInqLastNestedInstance(part, inst, &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &i1, &i2, &x);
    if ((int)cnt > 50000) return 0;                                       /* 0x2718 / 0x2719 */
  } else if ((int)code == 0x13) {
    char nm[1024] = ""; NInqNextPartType(nm);
  }
  return 1;
}
static uint64_t cb_order(uint64_t a, uint64_t b) { (void)a; (void)b; return 0; }   /* LAB_1800066f0: xor eax,eax; ret */
/* FUN_180005f90 L4299: the nested-sheet-result callback, one CNST_Result per copy (param_1): NInqNestedParameters,
   NInqNestedSheet (asm @1800060b5-18000612c), per part type NInqNestedPartType + per instance NInqNestedInstance (asm
   @180006427-18000647c: d3 XMATRIX -> x, d4 YMATRIX -> y, d7 ANGLE, i1 MIRROR). A result without nested parts is dropped
   (L4529-4533); returns whether results were added (L4559). NestOut keeps one sheet with its copy count (what NESTING.DLL
   repeats per copy is the same readback) */
static uint64_t cb_sheet_result(uint64_t copies_) {
  int copies = (int)copies_, before = U.nresults;
  for (int c = 0; c < copies; c++) {
    undefined8 prm[5] = { 0 }; char sn[1024] = ""; uint p2 = 0; undefined4 p3 = 0, ntypes = 0;
    double L = 0, W = 0, area = 0, partArea = 0, cutArea = 0, nl = 0, nw = 0; undefined8 p6 = 0;
    NInqNestedParameters(&prm[0], &prm[1], &prm[2], &prm[3], &prm[4]);
    NInqNestedSheet(sn, &p2, &p3, (undefined8 *)&L, (undefined8 *)&W, &p6, (undefined8 *)&area, &ntypes, (undefined8 *)&partArea,
                    (undefined8 *)&cutArea, (undefined8 *)&nl, (undefined8 *)&nw);
    NestOutSheet tmp; memset(&tmp, 0, sizeof tmp);
    tmp.sheet = sheet_index(sn); tmp.copies = copies;
    tmp.len = L; tmp.wid = W; tmp.area = area; tmp.partArea = partArea; tmp.cutArea = cutArea; tmp.nestLen = nl; tmp.nestWid = nw;
    tmp.util = partArea / area * 100.0;                                    /* the Results dialog's utilisation (FUN_180084d30, 7.2) */
    if (tmp.sheet < 0) return U.nresults > before;                         /* ns_parse_shtname failed (L4373) */
    int nparts = 0;
    for (int i = 1; i <= (int)ntypes; i++) {
      char pn[1024] = ""; undefined4 n = 0;
      NInqNestedPartType((uint)i, pn, &n);
      long elem = -1; int pi = part_of_name(pn, &elem);
      if (pi < 0) { free(tmp.inst); U.pend = NULL; return U.nresults > before; }   /* ns_parse_prtname failed (L4471-4476) */
      if ((int)n > 0) nparts++;
      for (int k = 1; k <= (int)n; k++) {
        undefined8 xll = 0, yll = 0, d5 = 0, d6 = 0; double x = 0, y = 0, ang = 0; undefined4 mir = 0, ident = 0;
        NInqNestedInstance((uint)i, (uint)k, &xll, &yll, (undefined8 *)&x, (undefined8 *)&y, &d5, &d6, (undefined8 *)&ang, &mir, &ident);
        tmp.inst = U.pend = realloc(tmp.inst, sizeof(NestOutInst) * (size_t)(tmp.ninst + 1));
        NestOutInst *o = &tmp.inst[tmp.ninst++]; o->part = pi; o->x = x; o->y = y; o->angle = ang; o->mirror = (int)mir;
        o->elem = elem < 0 ? 0 : (int)elem;                                /* CNST_Inst +0x28 (L4505-4509) */
        o->entry = i - 1;                                                  /* one CNST_Part per part type i (L4465-4509) */
      }
    }
    U.pend = NULL;
    if (nparts < 1 || c > 0) { free(tmp.inst); if (nparts >= 1) U.nresults++; continue; }
    U.out->s = realloc(U.out->s, sizeof(NestOutSheet) * (size_t)(U.out->ns + 1));
    U.out->s[U.out->ns++] = tmp; U.nresults++;
  }
  return U.nresults > before;
}

int nest_engine_ts(const NestJob *job, NestOut *out, char *err, int errsz) {
  memset(&U, 0, sizeof U); U.job = job; U.out = out; U.err = err; U.errsz = errsz;
  /* NOpenNestLib's directory: Mastercam's data dir <...>\Nesting\ (FUN_180043c20); ours: NEST_TS_DIR or a fresh temporary
     directory (the engine keeps its tmpnst<n> work files there) */
  char dir[1024]; const char *e = getenv("NEST_TS_DIR"); char tmpl[1024] = "";
  if (e) snprintf(dir, sizeof dir, "%s/", e);
  else {
    snprintf(tmpl, sizeof tmpl, "%s/nestlib.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(tmpl)) { snprintf(err, (size_t)errsz, "Warning - unable to open NESTLIB"); return 0; }
    snprintf(dir, sizeof dir, "%s/", tmpl);
  }
  int ok = 0;
  /* ours: a NOT PORTED function or another ts_fatal stops this run with its message instead of ending the program (the
     jump target is set before the engine is loaded: the load runs engine code too).  The run's work directory
     (DAT_1802b0ca0 = "<dir>/tmpnst<k>", NOpenNestLib L4611; NCloseNestLib would remove it, L8989) is emptied and removed
     as NOpenNestLib(dir, 1) does with a leftover one (L4602-4606: FUN_1800577a0 + _rmdir), what the engine holds is
     released and its image loaded again for the next run (ts_engine_reset) */
  static jmp_buf fatal_jb;
  if (setjmp(fatal_jb)) {
    snprintf(err, (size_t)errsz, "nest_ts: %s", ts_fatal_msg);
    free(U.pend); U.pend = NULL;
    char wd[1024], pre[1100];
    snprintf(wd, sizeof wd, "%s", (const char *)&TS_G(char, 0x1802b0ca0ULL));
    snprintf(pre, sizeof pre, "%stmpnst", dir);
    if (!strncmp(wd, pre, strlen(pre))) {
      ts_fatal_jb = &fatal_jb;
      if (!setjmp(fatal_jb)) { FUN_1800577a0((undefined8)(uintptr_t)wd); ts_rmdir(wd); }
    }
    ts_fatal_jb = NULL;
    ts_engine_reset();
    if (tmpl[0]) rmdir(tmpl);
    return 0;
  }
  ts_fatal_jb = &fatal_jb;
  ts_engine_load();
  if ((int)NOpenNestLib((undefined8)dir, 1, (code *)cb_error) != 1) { snprintf(err, (size_t)errsz, "Warning - unable to open NESTLIB"); goto done; }   /* 10013 */
  /* FUN_1800411f0 L45099-45110 */
  if (NStartSettingCallBacks() != 1) goto done;
  NSetErrorHandlerCB((code *)cb_error);
  NSetProgressFunctionCB((code *)cb_progress);
  NSetNewPartGeomHandleCB((code *)cb_part_geom);
  NSetNewSheetGeomHandleCB((code *)cb_sheet_geom);
  NSetNestedOrderResultCB((code *)cb_order);
  NSetNestedSheetResultCB((code *)cb_sheet_result);
  if (NEndSettingCallBacks() != 1) goto done;
  /* FUN_180039c90 L39906-39936: NSetExactDistance(0) - FUN_1800c4cf0 (toolpath common edge) is off for geometry;
     NSetThicknessAndMaterialSelection(0, multi-material FUN_18003e930: none); NSetGrpMaxElmtSize(max(20, largest group):
     no groups) */
  if (NStartSetParams() != 1 || NSetDLLEnableKey(ts_key) != 1) { NCloseNestLib(); goto done; }
  NSetResolution(job->resolution);
  NSetPartDistType(0);
  NSetClearance(job->clearance);
  NSetThicknessAndMaterialSelection(0, 0);
  NSetNestFillersUptoNestedHeight((uint)job->fillersUptoNestedHeight);
  NSetFitPartInPart((uint)job->fitPartInPart, (uint)job->preferHoleFilling);
  NSetGrpMaxElmtSize(20);
  NSetExactDistance(0);
  NSetAccuNest(0);
  NSetGuillotineCut((uint)(job->guillotine != 0));
  NSetClusterCutoffUtil(0);
  NSetCutterDiameter(job->cutter);
  NSetFitClusterFirst(0);
  NSetFitMaxOnLast(1);
  if ((int)NEndSetParam() != 1) { NCloseNestLib(); goto done; }
  /* FUN_18003a120 / FUN_180037400: NAddPartInfo(name, min, max, rect, L, W, step, grain, prio, 2, commonTool, mirror) */
  if (NStartNewPartList() != 1) goto done;
  for (int i = 0; i < job->nparts; i++) {
    const NestJobPart *p = &job->parts[i];
    if ((int)NAddPartInfo((char *)p->name, (uint)p->minq, p->maxq, p->isRect, p->len, p->wid, p->step, p->grain, p->prio, 2,
                          p->commonTool, p->mirror) != 1) { snprintf(err, (size_t)errsz, "(AddTheParts) (AddPartInfo has failed)"); NCloseNestLib(); goto done; }
    if (p->pair) {                                 /* L38278-38307: cluster "%dc%da" (FUN_180055ad0), one element, 360 (raw bits) */
      char cn[64]; snprintf(cn, sizeof cn, "%dc%da", p->id, p->id);
      if ((int)NStartCluster(cn, 7, 1, 0, p->commonTool) != 1) { snprintf(err, (size_t)errsz, "(AddTheParts) (NStartCluster has failed)"); NCloseNestLib(); goto done; }
      if ((int)NAddClusterElement((char *)p->name, 2, 0.0, 0.0, (undefined8)0x4076800000000000ULL) != 1) {
        snprintf(err, (size_t)errsz, "(AddTheParts) (NAddClusterElement has failed)"); NEndCluster(); NCloseNestLib(); goto done; }   /* L38318-38320 */
      if ((int)NEndCluster() != 1) { snprintf(err, (size_t)errsz, "(AddTheParts) (NEndCluster has failed)"); NCloseNestLib(); goto done; }
    }
  }
  if ((int)NEndNewPartList() != 1) { NCloseNestLib(); goto done; }
  /* FUN_18003e870 / FUN_18003a3f0: NAddSheetInfo(name, qty, 1, rect, L, W, 0, 0, 0, 0, 0, corner, dir, grain, 1, collar, 0, 0)
     (asm @18003a6e3-18003a730) + NAddSheetPriority(name, prio) */
  if ((int)NStartNestOrder() != 1) { NCloseNestLib(); goto done; }
  for (int i = 0; i < job->nsheets; i++) {
    const NestJobSheet *s = &job->sheets[i];
    if ((int)NAddSheetInfo((char *)s->name, (uint)s->qty, 1, s->isRect, s->len, s->wid, 0, 0.0, 0.0, 0.0, 0.0, s->corner, s->dir,
                           s->grain, 1, s->collar, 0, 0) != 1) { snprintf(err, (size_t)errsz, "(InputSheets) (NAddSheetInfo has failed)"); NCloseNestLib(); goto done; }
    if (!NAddSheetPriority((char *)s->name, (uint)s->prio)) snprintf(err, (size_t)errsz, "(InputSheets) (NAddSheetPriority has failed)");
  }
  if ((int)NNestOrder() != 1) { NCloseNestLib(); goto done; }
  NCloseNestLib();                                                          /* FUN_180039050 */
  ok = 1;
done:
  ts_fatal_jb = NULL;
  if (tmpl[0]) rmdir(tmpl);
  return ok;
}
