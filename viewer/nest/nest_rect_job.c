/* nest_rect_job.c - the Rectangular back-end of Geometry Nesting (nest_job.h): the RN* calls NESTING.DLL makes, in its order,
 * on the port of NestLib's Rectangular engine (nest_rect.c). Citations: decomp/nest/nesting_all.c (nesting.dll) L<n> /
 * FUN_<addr>, "asm @" = objdump of nesting.dll. */
#include "nest_job.h"
#include "nest_rect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void nest_out_free(NestOut *o) { for (int i = 0; i < o->ns; i++) free(o->s[i].inst); free(o->s); o->s = NULL; o->ns = 0; }

/* the Rectangular back-end: FUN_180043040 RNInitRectangleNest, FUN_1800411f0 the callbacks, FUN_180039c90 the parameters,
   FUN_18003a120 / FUN_180037400 the parts, FUN_18003e870 / FUN_18003a3f0 the sheets + RNNestOrder, FUN_180039050
   RNEndRectangleNest. The sheet-result callback LAB_180009c80 (asm @180009c80: util -> DAT_180134638, then FUN_180005f90)
   reads the sheet back: RNInqNestedSheet + RNInqNestedPart(i) per instance (FUN_180005830 L3942), angle 90 when rotated
   (FUN_180005980 L3992), mirror 0 */
typedef struct { const NestJob *job; NestOut *out; char *err; int errsz; } RnUser;
static int rn_part_index(const NestJob *job, const char *name) { for (int i = 0; i < job->nparts; i++) if (!strcmp(job->parts[i].name, name)) return i; return -1; }
static int rn_sheet_cb(rn_ctx *c, void *user, int copies, double util) {
  RnUser *u = user; char sn[1024]; double len, wid; int zero, ninst;
  if (!rn_inq_nested_sheet(c, sn, &len, &wid, &zero, &ninst)) return 0;
  NestOut *o = u->out; o->s = realloc(o->s, sizeof(NestOutSheet) * (size_t)(o->ns + 1));
  NestOutSheet *s = &o->s[o->ns++]; memset(s, 0, sizeof *s);
  s->sheet = -1; for (int i = 0; i < u->job->nsheets; i++) if (!strcmp(u->job->sheets[i].name, sn)) s->sheet = i;
  s->copies = copies; s->util = util;
  s->inst = calloc((size_t)(ninst > 0 ? ninst : 1), sizeof(NestOutInst));
  char (*names)[1024] = calloc((size_t)(ninst > 0 ? ninst : 1), sizeof *names);
  for (int i = 0; i < ninst; i++) {
    char pn[1024]; double x, y; int rot;
    if (!rn_inq_nested_part(c, i, pn, &x, &y, &rot)) { free(names); return 0; }
    snprintf(names[i], sizeof names[i], "%s", pn);
    NestOutInst *in = &s->inst[s->ninst++];
    in->part = rn_part_index(u->job, pn); in->x = x; in->y = y; in->angle = rot ? 90.0 : 0.0; in->mirror = 0;
    /* FUN_1800056d0 L3884: the type ids by first appearance of the name (_mbsicmp) - one CNST_Part per type */
    in->entry = -1;
    for (int j = 0; j < i && in->entry < 0; j++) if (!strcasecmp(names[j], pn)) in->entry = s->inst[j].entry;
    if (in->entry < 0) { int m = -1; for (int j = 0; j < i; j++) if (s->inst[j].entry > m) m = s->inst[j].entry; in->entry = m + 1; }
  }
  free(names);
  return 1;
}
static int rn_error_cb(void *user, int code, int sev, const char *msg) { RnUser *u = user; snprintf(u->err, u->errsz, "%s", msg ? msg : ""); (void)code; (void)sev; return 1; }
int nest_engine_rect(const NestJob *job, NestOut *out, char *err, int errsz) {
  RnUser u = { job, out, err, errsz };
  rn_callbacks cb; memset(&cb, 0, sizeof cb); cb.user = &u; cb.sheet_result = rn_sheet_cb; cb.error = rn_error_cb;
  rn_ctx *c = rn_new(job->cutter, job->clearance, job->resolution, &cb);   /* RNStartSetParams .. RNEndSetParam (L39916-39932) */
  if (!c) { snprintf(err, errsz, "Warning - unable to open NESTLIB"); return 0; }   /* 10013 */
  rn_set_fillers_upto_nested_height(c, job->fillersUptoNestedHeight);
  int ok = 1;
  for (int i = 0; i < job->nparts && ok; i++) {                           /* RNAddPartInfo2 + RNSetPartPriority (L38188-38198) */
    const NestJobPart *p = &job->parts[i];
    ok = rn_add_part(c, p->name, p->minq, p->maxq, p->len, p->wid, p->rotate, p->grain) == 1 && rn_set_part_priority(c, p->prio) == 1;
  }
  for (int i = 0; i < job->nsheets && ok; i++) {                          /* RNAddSheetInfo2 (asm @18003a777-18003a782) */
    const NestJobSheet *s = &job->sheets[i];
    ok = rn_add_sheet(c, s->name, s->qty, s->len, s->wid, s->collar, s->grain) == 1;
  }
  if (ok) ok = rn_nest_order(c) == 1;
  rn_free(c);                                                             /* RNEndRectangleNest */
  return ok;
}

