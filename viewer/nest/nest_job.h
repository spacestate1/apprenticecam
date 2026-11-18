/* nest_job.h - the engine interface of Transform > Geometry Nesting (ours: the shape of the data, not Mastercam's).
 *
 * NESTING.DLL drives NestLib through two C APIs (formats/nesting_addin.md §6.3): the Rectangular engine (RN*, viewer/nest/
 * nest_rect.c) and TrueShape (N*, viewer/nest/nest_ts.c + ts/). The Mastercam side (viewer/core_ui/cui_nest.c)
 * builds one NestJob from its session - every value exactly as NESTING.DLL passes it to the engine (the citations are at
 * the builder) - and hands it to a back-end, which makes the API calls in NESTING.DLL's order and fills NestOut with what
 * NESTING.DLL reads back (CNST_Result / CNST_Inst, §7.1). A back-end is a function; cui_nest.c holds one per engine.
 *
 * Part geometry is in the job (profiles of lines / arcs / circles, the engine's NDwgAdd* calls of the new-part-geometry
 * callback FUN_180006f90, §6.6) so that a TrueShape back-end can answer its geometry callbacks from it; the Rectangular
 * back-end uses only the sizes. */
#ifndef NEST_JOB_H
#define NEST_JOB_H

typedef struct {                 /* one NDwgAdd* call (§6.6) */
  int kind;                      /* 0 line (x1,y1)-(x2,y2); 1 arc centre (cx,cy) from (x1,y1) by sweep (radians, CW < 0); 2 circle (cx,cy) r */
  double x1, y1, x2, y2, cx, cy, sweep, r;
} NestDwgEnt;
typedef struct { int outer; NestDwgEnt *e; int n; } NestProfile;   /* NDwgClosedProfStart(1) outer / (0) hole */

typedef struct {                 /* NAddPartInfo / RNAddPartInfo2 + RNSetPartPriority (FUN_180037400) */
  char name[64];                 /* "%dp" % part id */
  int minq, maxq;                /* FUN_18005be90 (min >= 1) */
  int isRect;                    /* NAddPartInfo arg 4: the part is sent as a rectangle (SHAPE R) */
  double len, wid;               /* part +0x238 / +0x240 */
  double step;                   /* TrueShape step angle (deg) */
  int rotate;                    /* Rectangular: FUN_1800c4be0's flag (90 / 180 / 270 allowed) */
  char grain;                    /* 'X' / 'Y' / 'N' */
  int prio;                      /* engine priority P - UI priority + 1 */
  int commonTool, mirror;        /* TrueShape only */
  double cutterDia;              /* NDwgChangeCutterDia(part +0x270) */
  NestProfile *prof; int nprof;  /* the geometry callback's profiles (TrueShape) */
  int id;                        /* the part id (part +0x230): the sheet results' part names parse back to it (FUN_1800097c0) */
  int pair;                      /* TrueShape Optimized pairs: after NAddPartInfo, NStartCluster("%dc%da", 7, 1, 0, commonTool) /
                                    NAddClusterElement(name, 2, 0, 0, 360) / NEndCluster (FUN_180037400 L38228-38307) */
} NestJobPart;

typedef struct {                 /* NAddSheetInfo / RNAddSheetInfo2 (FUN_18003a3f0) */
  char name[64];                 /* "%ds%dq" */
  int qty;
  int isRect;
  double len, wid;               /* enlarged by 2|c| when the collar c < 0 */
  double collar;
  int corner; char dir, grain;   /* TrueShape: STARTINGCORNER, fill direction; grain for both */
  int prio;                      /* NAddSheetPriority (TrueShape) */
  NestProfile *prof; int nprof;  /* chained sheets (TrueShape) */
} NestJobSheet;

typedef struct {
  double clearance, resolution, cutter;   /* RNSetClearance / RNSetResolution / RNSetCutterDiameter(0) */
  int fillersUptoNestedHeight;            /* RNSetNestFillersUptoNestedHeight(P+0x60) */
  int fitPartInPart, preferHoleFilling, guillotine;   /* TrueShape */
  NestJobPart *parts; int nparts;
  NestJobSheet *sheets; int nsheets;
} NestJob;

typedef struct { int part; double x, y, angle; int mirror; int elem; int entry; } NestOutInst;   /* part = index into job.parts; x / y = XMATRIX / YMATRIX
                                                                                (§7.1); elem = the cluster element of the part name (CNST_Inst +0x28,
                                                                                ns_parse_prtname FUN_180009850's 5th value, < 0 -> 0); entry = the
                                                                                CNST_Part (0x138) of the sheet result the instance belongs to, 0 ..
                                                                                (FUN_180005f90: TrueShape one per NInqNestedPartType i, L4465-4509;
                                                                                Rectangular one per part name in order of first appearance,
                                                                                FUN_1800056d0 L3884) - ns_output walks the entries in this order and
                                                                                numbers each one's instances 1.. (part +0x4c4, L5952 / L6121) */
typedef struct {
  int sheet;                     /* index into job.sheets */
  int copies;                    /* identical sheets (the engine's repetition) */
  double util;                   /* the engine's utilisation (%) */
  double len, wid, area, partArea, cutArea, nestLen, nestWid;   /* TrueShape: NInqNestedSheet (CNST_Result +0x140..+0x170, 7.1) */
  NestOutInst *inst; int ninst;
} NestOutSheet;
typedef struct { NestOutSheet *s; int ns; } NestOut;

/* ns_parse_prtname FUN_180009850 L6564 (via FUN_1800097c0 L6536, then GetPartPtr FUN_180039750(id, 1)): an engine part name
   back to the part id. Lower-cased; with a 'p': id = atol(before it), after it "<e>u" or "<e>c" -> element e, type 2, else
   type 0, element -1; without a 'p' a 'c' is needed: "c<id>u" -> type 1 element 0, "c<id>a" -> type 0 element -1 (the id is
   atol after the 'c'). Returns 0 when there is neither 'p' nor 'c' (or neither 'u' nor 'a' after a 'c'-only name). */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
static inline int nest_parse_prtname(const char *name, long *id, int *type, long *elem) {
  char b[1024]; int k = 0; for (; name[k] && k < 1023; k++) b[k] = (char)tolower((unsigned char)name[k]); b[k] = 0;   /* _mbslwr_s */
  char *p = strchr(b, 'p');
  if (!p) {
    char *c = strchr(b, 'c'); if (!c) return 0;
    char *u = strchr(b, 'u'); int a = 0;
    if (!u) { u = strchr(b, 'a'); if (!u) return 0; a = 1; }
    *u = 0; *id = atol(c + 1);
    if (a) { *type = 0; *elem = -1; } else { *type = 1; *elem = 0; }
    return 1;
  }
  *p = 0; *id = atol(b); *elem = -1; *p = 'p';
  char *u = strchr(p + 1, 'u'); if (!u) u = strchr(p + 1, 'c');
  if (!u) { *type = 0; return 1; }
  *u = 0; *elem = atol(p + 1); *type = 2;
  return 1;
}

/* a back-end: 1 = nested (out filled, possibly with no sheet), 0 = the engine failed (err) */
typedef int (*NestEngineFn)(const NestJob *job, NestOut *out, char *err, int errsz);
void nest_out_free(NestOut *o);
int nest_engine_rect(const NestJob *job, NestOut *out, char *err, int errsz);   /* nest_rect_job.c */
/* the TrueShape back-end: viewer/nest/nest_ts_job.c (on the TrueShape port nest_ts.c + ts/, linked into mcamcore) */
int nest_engine_ts(const NestJob *job, NestOut *out, char *err, int errsz);
/* NESTING.DLL's progress callback (FUN_180006700 L4613) first asks KeyPress(0x1b) and answers 0 - the engine stops - when
   Esc was pressed: the host's Esc check (NULL: never) */
extern int (*nest_ts_cancel)(void);

#endif
