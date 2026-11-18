/* nest_job_io.c - NestJob / NestOut as text (ours: the test format of tools/tests/nest_ts_job_oracle.sh). Doubles are
 * written with %a (exact), so a job read back is bit-identical to the one written; NestOut likewise, for the bitwise
 * comparison of the original engine (tools/peloader --job) with the port (nest_ts_job_test).
 *
 *   NESTJOB 1
 *   params <clearance> <resolution> <cutter> <fillers> <fitPartInPart> <preferHoleFilling> <guillotine>
 *   part <name> <minq> <maxq> <isRect> <len> <wid> <step> <rotate> <grain> <prio> <commonTool> <mirror> <cutterDia> <nprof> [<id> <pair>]
 *     (id defaults to atol(name), the "%dp" of the part id; pair to 0)
 *   sheet <name> <qty> <isRect> <len> <wid> <collar> <corner> <dir> <grain> <prio> <nprof>
 *     prof <outer> <n>                       (nprof of these after their part / sheet)
 *       ent <kind> <x1> <y1> <x2> <y2> <cx> <cy> <sweep> <r>
 *   end
 * Characters (grain, dir) are written as their decimal code. */
#include "nest_job_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wr_prof(FILE *f, const NestProfile *p, int n) {
  for (int i = 0; i < n; i++) {
    fprintf(f, "prof %d %d\n", p[i].outer, p[i].n);
    for (int k = 0; k < p[i].n; k++) {
      const NestDwgEnt *e = &p[i].e[k];
      fprintf(f, "ent %d %a %a %a %a %a %a %a %a\n", e->kind, e->x1, e->y1, e->x2, e->y2, e->cx, e->cy, e->sweep, e->r);
    }
  }
}
int nest_job_write(const char *path, const NestJob *j) {
  FILE *f = fopen(path, "w"); if (!f) return 0;
  fprintf(f, "NESTJOB 1\nparams %a %a %a %d %d %d %d\n", j->clearance, j->resolution, j->cutter, j->fillersUptoNestedHeight,
          j->fitPartInPart, j->preferHoleFilling, j->guillotine);
  for (int i = 0; i < j->nparts; i++) {
    const NestJobPart *p = &j->parts[i];
    fprintf(f, "part %s %d %d %d %a %a %a %d %d %d %d %d %a %d %d %d\n", p->name, p->minq, p->maxq, p->isRect, p->len, p->wid, p->step,
            p->rotate, (unsigned char)p->grain, p->prio, p->commonTool, p->mirror, p->cutterDia, p->nprof, p->id, p->pair);
    wr_prof(f, p->prof, p->nprof);
  }
  for (int i = 0; i < j->nsheets; i++) {
    const NestJobSheet *s = &j->sheets[i];
    fprintf(f, "sheet %s %d %d %a %a %a %d %d %d %d %d\n", s->name, s->qty, s->isRect, s->len, s->wid, s->collar, s->corner,
            (unsigned char)s->dir, (unsigned char)s->grain, s->prio, s->nprof);
    wr_prof(f, s->prof, s->nprof);
  }
  fprintf(f, "end\n");
  return fclose(f) == 0;
}

static double D(char **s) { return strtod(*s, s); }
static int I(char **s) { return (int)strtol(*s, s, 10); }
static int rd_prof(FILE *f, NestProfile **pp, int n) {
  char line[1024];
  *pp = calloc((size_t)(n > 0 ? n : 1), sizeof(NestProfile));
  for (int i = 0; i < n; i++) {
    if (!fgets(line, sizeof line, f) || strncmp(line, "prof ", 5)) return 0;
    char *s = line + 5; NestProfile *p = &(*pp)[i];
    p->outer = I(&s); p->n = I(&s); p->e = calloc((size_t)(p->n > 0 ? p->n : 1), sizeof(NestDwgEnt));
    for (int k = 0; k < p->n; k++) {
      if (!fgets(line, sizeof line, f) || strncmp(line, "ent ", 4)) return 0;
      char *t = line + 4; NestDwgEnt *e = &p->e[k];
      e->kind = I(&t); e->x1 = D(&t); e->y1 = D(&t); e->x2 = D(&t); e->y2 = D(&t); e->cx = D(&t); e->cy = D(&t); e->sweep = D(&t); e->r = D(&t);
    }
  }
  return 1;
}
static void name_tok(char **s, char *out, int sz) {
  while (**s == ' ') (*s)++;
  int k = 0; while (**s && **s != ' ' && **s != '\n' && k + 1 < sz) out[k++] = *(*s)++;
  out[k] = 0;
}
int nest_job_read(const char *path, NestJob *j) {
  FILE *f = fopen(path, "r"); if (!f) return 0;
  char line[1024]; memset(j, 0, sizeof *j); int ok = 0;
  if (!fgets(line, sizeof line, f) || strncmp(line, "NESTJOB 1", 9)) goto done;
  while (fgets(line, sizeof line, f)) {
    char *s = line;
    if (!strncmp(s, "end", 3)) { ok = 1; break; }
    if (!strncmp(s, "params ", 7)) {
      s += 7; j->clearance = D(&s); j->resolution = D(&s); j->cutter = D(&s); j->fillersUptoNestedHeight = I(&s);
      j->fitPartInPart = I(&s); j->preferHoleFilling = I(&s); j->guillotine = I(&s);
    } else if (!strncmp(s, "part ", 5)) {
      s += 5; j->parts = realloc(j->parts, sizeof(NestJobPart) * (size_t)(j->nparts + 1));
      NestJobPart *p = &j->parts[j->nparts++]; memset(p, 0, sizeof *p);
      name_tok(&s, p->name, sizeof p->name);
      p->minq = I(&s); p->maxq = I(&s); p->isRect = I(&s); p->len = D(&s); p->wid = D(&s); p->step = D(&s); p->rotate = I(&s);
      p->grain = (char)I(&s); p->prio = I(&s); p->commonTool = I(&s); p->mirror = I(&s); p->cutterDia = D(&s); p->nprof = I(&s);
      { char *t = s; long v = strtol(t, &s, 10); p->id = s != t ? (int)v : atoi(p->name); p->pair = I(&s); }
      if (!rd_prof(f, &p->prof, p->nprof)) goto done;
    } else if (!strncmp(s, "sheet ", 6)) {
      s += 6; j->sheets = realloc(j->sheets, sizeof(NestJobSheet) * (size_t)(j->nsheets + 1));
      NestJobSheet *q = &j->sheets[j->nsheets++]; memset(q, 0, sizeof *q);
      name_tok(&s, q->name, sizeof q->name);
      q->qty = I(&s); q->isRect = I(&s); q->len = D(&s); q->wid = D(&s); q->collar = D(&s); q->corner = I(&s);
      q->dir = (char)I(&s); q->grain = (char)I(&s); q->prio = I(&s); q->nprof = I(&s);
      if (!rd_prof(f, &q->prof, q->nprof)) goto done;
    }
  }
done:
  fclose(f);
  return ok;
}
void nest_job_free(NestJob *j) {
  for (int i = 0; i < j->nparts; i++) { for (int k = 0; k < j->parts[i].nprof; k++) free(j->parts[i].prof[k].e); free(j->parts[i].prof); }
  for (int i = 0; i < j->nsheets; i++) { for (int k = 0; k < j->sheets[i].nprof; k++) free(j->sheets[i].prof[k].e); free(j->sheets[i].prof); }
  free(j->parts); free(j->sheets); memset(j, 0, sizeof *j);
}
int nest_out_write(const char *path, int ok, const NestOut *o, const char *err) {
  FILE *f = fopen(path, "w"); if (!f) return 0;
  fprintf(f, "ok %d\nerr %s\n", ok, err ? err : "");
  for (int i = 0; i < o->ns; i++) {
    const NestOutSheet *s = &o->s[i];
    fprintf(f, "sheet %d %d %a %d\n", s->sheet, s->copies, s->util, s->ninst);
    fprintf(f, "dims %a %a %a %a %a %a %a\n", s->len, s->wid, s->area, s->partArea, s->cutArea, s->nestLen, s->nestWid);
    for (int k = 0; k < s->ninst; k++)
      fprintf(f, "inst %d %a %a %a %d %d\n", s->inst[k].part, s->inst[k].x, s->inst[k].y, s->inst[k].angle, s->inst[k].mirror, s->inst[k].elem);
  }
  return fclose(f) == 0;
}
