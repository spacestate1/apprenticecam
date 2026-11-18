/* nest_rect.c - NestLib Rectangular engine (optiRectOrder), bit-exact port of n64dll.dll.  See nest_rect.h.
 * Citations: FUN_<addr> / L<line> = decomp/nest/n64dll_all.c; "asm <addr>" = objdump of n64dll.dll where Ghidra's
 * output was not enough.  Struct offsets are the original's (this = optiRectOrder, part record 0x1a0, sheet record
 * 0x1c0, placed-rect record 0x138, segment record 0x28).  The original's paged arrays (FUN_1801e39e0: page 100, max
 * 10000, error fn FUN_1801e3b40 = no-op) are plain arrays here; exceeding 10000 entries is reported and stops (the
 * original then silently drops writes - not reproduced).  Code under DAT_1802b32c8 == 1 (bss, never written: only
 * cmpl at asm 0x1801b665f..0x1801b9131) and DAT_1804f0f40 == 1 (bss, only cmpl at asm 0x1800e73cf / 0x1801b7d52) is
 * dead and not ported.  Compile with -ffp-contract=off: the original is MSVC x64 SSE2 scalar double arithmetic, every
 * expression below keeps the decompile's operation order. */
#include "nest_rect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RN_MAXPART 5000   /* _eh_vector_constructor_iterator_(.., 0x1a0, 5000, ..) FUN_1801b6890 L192924 */
#define RN_MAXSHEET 1000  /* (.., 0x1c0, 1000, ..) L192925 */
#define RN_MAXLIST 10000  /* FUN_1801e39e0(.., 100, 10000, ..) L192946-192952 */

typedef struct {          /* part record 0x1a0: ctor FUN_1801e42c0 L209853, setters FUN_1801e4340 L209869 .. */
  char name[256];         /* +0x20 */
  int min, max;           /* +0x120, +0x124 */
  double res;             /* +0x128 (FUN_1801b72b0 = this+0x20; ctor 1.0) */
  int rotate; char grain; /* +0x130, +0x134 */
  int rotFlag;            /* +0x138 set by FUN_1801e4ac0 (grain pre-rotation) */
  double f140, f148;      /* +0x140 / +0x148 (RNAddPartInfo2 passes 0, L19051) */
  int prio;               /* +0x150 (ctor 1) */
  double Lr, Wr;          /* +0x158 / +0x160 = L/res, W/res */
  int cat;                /* +0x168 category FUN_1801e45e0 */
  int done, target, placed, remain; /* +0x16c +0x170 +0x174 +0x178 (FUN_1801e4700 L209963) */
  int max0;               /* +0x17c */
  int fill, fillTot;      /* +0x180 +0x184 */
  int fits;               /* +0x188 written only by FUN_1801c5b10 (asm 0x1801c5c9f..0x1801c5da1) */
  double L, W;            /* +0x190 / +0x198 inflated by 2*offset */
} rn_part;

typedef struct {          /* sheet record 0x1c0: ctor FUN_1801e3b60 L209709, set FUN_1801e3ba0 L209721 */
  char name[256];         /* +0x20 */
  int qty;                /* +0x120 */
  double len, wid;        /* +0x128 / +0x130 */
  int cflag;              /* +0x138 (RNAddSheetInfo2 path: 0) */
  double collar[4];       /* +0x140.. */
  double res;             /* +0x160 (FUN_1801b74f0 = this+0x20) */
  int f168;               /* +0x168 */
  double collarR[4];      /* +0x170.. collar/res */
  double Lr, Wr;          /* +0x190 / +0x198 */
  double cost;            /* +0x1a0 */
  char grain;             /* +0x1a8 */
  int f1ac;               /* +0x1ac (RNAddSheetInfo2: 1, L19284) */
  char f1b0;              /* +0x1b0 (RNAddSheetInfo2: 'Y', L19283) */
  int left;               /* +0x1b4 */
  int used;               /* +0x1b8 */
} rn_sheet;

typedef struct { char name[256]; double x, y, L, W; int rot; double f128, f130; } rn_rec; /* 0x138 */
typedef struct { double x1, y1, x2, y2, len; } rn_seg;                                    /* 0x28 */
typedef struct { rn_rec *v; int n; } rn_reclist;
typedef struct { rn_seg *v; int n; } rn_seglist;

struct rn_ctx {
  rn_callbacks cb;
  double cutter, clr;     /* this+0x08 / +0x10 */
  double res;             /* this+0x20 = 1.0 */
  double offR, off;       /* this+0x28 / +0x30 */
  int np;                 /* this+0x38 */
  rn_part *A, *B, *C, *D; /* this+0x40 / +0x269360 / +0x465060 / +0x660d60 */
  int ns, cur;            /* this+0x1fbd40 / +0x1fbd44 */
  rn_sheet *S;            /* this+0x1fbd48 */
  rn_reclist rec, best;   /* this+0x269348 / +0x269358 */
  rn_seglist seg;         /* this+0x269350 */
  int nTypes, nSheets;    /* this+0x85ca60 / +0x85ca64 */
  double maxY, maxX;      /* this+0x85ca70 / +0x85ca78 */
  /* process globals of the original */
  int state;              /* DAT_1802ac098 */
  int method, fillers;    /* DAT_1802f2c78, DAT_1802f2c98 */
  double t, t2;           /* DAT_1802f5210 / DAT_1802f5218 wall thickness */
  int dir;                /* DAT_1802f5220 fill direction 1/2/3 */
  int dummy;              /* DAT_1802f5224 */
  int prio;               /* DAT_1802f5238 current priority */
  int pass;               /* DAT_1802f523c */
  int itSheet, itPart;    /* DAT_1802ac0a0 / DAT_1802ac09c (.data initial -1, asm dump 0x1802ac09c) */
  double bestUtil;        /* FUN_1801b7c30's local_1e90 (rsp+0x118): never initialised (absent from the init list
                           * L193680-193700), so a sheet instance where no trial improves reports the value left in
                           * that stack slot by the previous call from FUN_1801b7960's loop (same caller, same
                           * 0x1fa8 frame; oracle case r353 of seed 7).  The value before the first call is
                           * unknowable (stack garbage); 0 here. */
  int fail;
};

/* ---------------- MSVC CRT ---------------- */
static unsigned msvc_holdrand;   /* _getptd()->_holdrand */
void msvc_srand(unsigned seed) { msvc_holdrand = seed; }                       /* FUN_18022bc38 L251038 */
int msvc_rand(void) { msvc_holdrand = msvc_holdrand * 0x343fd + 0x269ec3; return (int)(msvc_holdrand >> 0x10 & 0x7fff); } /* L251051 */

static void ms_swap(char *a, char *b, unsigned long long w) { if (a != b) while (w--) { char t = *a; *a++ = *b; *b++ = t; } }
/* shortsort @180229c20 (L249655): repeated max selection to the high end */
static void ms_shortsort(char *lo, char *hi, unsigned long long w, int (*cmp)(const void *, const void *)) {
  while (hi > lo) { char *max = lo, *p; for (p = lo + w; p <= hi; p += w) if (cmp(p, max) > 0) max = p; ms_swap(max, hi, w); hi -= w; }
}
/* qsort @180229ce0 L249703 (MSVC 2010 qsort.c: median of 3, cutoff 8, explicit stack of 62) */
void msvc_qsort(void *base, unsigned long long n, unsigned long long w, int (*cmp)(const void *, const void *)) {
  char *lo, *hi, *mid, *lg, *hg, *lst[62], *hst[62]; unsigned long long size; int sp = 0;
  if (n < 2) return;
  lo = base; hi = (char *)base + (n - 1) * w;
recurse:
  size = (unsigned long long)(hi - lo) / w + 1;
  if (size <= 8) ms_shortsort(lo, hi, w, cmp);
  else {
    mid = lo + (size >> 1) * w;
    if (cmp(lo, mid) > 0) ms_swap(lo, mid, w);
    if (cmp(lo, hi) > 0) ms_swap(lo, hi, w);
    if (cmp(mid, hi) > 0) ms_swap(mid, hi, w);
    lg = lo; hg = hi;
    for (;;) {                                                                  /* LAB_180229e80 */
      if (mid > lg) { do lg += w; while (lg < mid && cmp(lg, mid) <= 0); }
      if (mid <= lg) { do lg += w; while (lg <= hi && cmp(lg, mid) <= 0); }    /* LAB_180229ea0 */
      do hg -= w; while (hg > mid && cmp(hg, mid) > 0);
      if (hg < lg) break;
      ms_swap(lg, hg, w);
      if (mid == hg) mid = lg;
    }
    hg += w;
    if (mid < hg) { do hg -= w; while (hg > mid && cmp(hg, mid) == 0); }
    if (mid >= hg) { do hg -= w; while (hg > lo && cmp(hg, mid) == 0); }       /* LAB_180229f30 */
    if (hg - lo >= hi - lg) {
      if (lo < hg) { lst[sp] = lo; hst[sp] = hg; ++sp; }
      if (lg < hi) { lo = lg; goto recurse; }
    } else {                                                                    /* LAB_180229f7c */
      if (lg < hi) { lst[sp] = lg; hst[sp] = hi; ++sp; }
      if (lo < hg) { hi = hg; goto recurse; }
    }
  }
  if (--sp >= 0) { lo = lst[sp]; hi = hst[sp]; goto recurse; }                 /* LAB_180229d96 */
}

/* printf %w.pf: _cftof_l L257931: FUN_18023dbd8 (= _fltout2: __dtold + $I10_OUTPUT FUN_180240ee8(ld, 0x11) -> 17
 * significant digits) then _fptostr L266895 (round half-up on the next digit) then _cftof2_l.  $I10_OUTPUT's 80-bit
 * digit generation is approximated by glibc's correctly rounded 17 digits (differs only when the exact value lies
 * within ~1e-19 relative of a 17-digit rounding boundary). */
int msvc_fmt_f(char *out, int outsz, double v, int width, int prec) {
  char e[40], man[24], buf[400], res[440]; int decpt, neg = signbit(v) != 0, i, k = 0, nd;
  if (v == 0.0) { strcpy(man, "0"); decpt = 0; }
  else { snprintf(e, sizeof e, "%.16e", fabs(v)); for (i = 0; e[i] && e[i] != 'e'; i++) if (e[i] != '.') man[k++] = e[i]; man[k] = 0; decpt = atoi(e + i + 1) + 1; }
  if (decpt + prec > 380) return snprintf(out, outsz, "%*.*f", width, prec, v);
  { const char *m = man; char *p = buf; nd = decpt + prec;                      /* _fptostr */
    *p = '0';
    while (p++, nd >= 1) { *p = *m ? *m++ : '0'; nd--; }
    *p = 0;
    if (nd > -1 && *m > '4') { while (*--p == '9') *p = '0'; *p += 1; }
    if (buf[0] == '1') decpt++; else memmove(buf, buf + 1, strlen(buf + 1) + 1); }
  k = 0; if (neg) res[k++] = '-';                                               /* _cftof2_l */
  if (decpt <= 0) res[k++] = '0'; else for (i = 0; i < decpt; i++) res[k++] = buf[i];
  if (prec > 0) {
    res[k++] = '.';
    for (i = 0; i < prec; i++) {
      int src = i + decpt;                                                      /* digit index into buf */
      res[k++] = src < 0 ? '0' : (src < (int)strlen(buf) ? buf[src] : '0');
    }
  }
  res[k] = 0;
  return snprintf(out, outsz, "%*s", width, res);
}

/* ---------------- helpers (0x1801e3b40-0x1801e5800) ---------------- */
static void rn_die(rn_ctx *c, const char *m) { fprintf(stderr, "nest_rect: %s\n", m); c->fail = 1; exit(4); }
static rn_rec *rec_add(rn_ctx *c, rn_reclist *l) { if (l->n >= RN_MAXLIST) rn_die(c, "more than 10000 placed rects (paged array max)"); return &l->v[l->n++]; }
static rn_seg *seg_add(rn_ctx *c) { if (c->seg.n >= RN_MAXLIST) rn_die(c, "more than 10000 segments (paged array max)"); return &c->seg.v[c->seg.n++]; }

/* FUN_1801e4340 L209869 (+ FUN_1801e4700 L209963) / FUN_1801e4430 L209889 (same without the counter reset) */
static void part_set(rn_part *p, const char *name, int min, double L, double W, int rot, char grain, int reset) {
  if (p->name != name) snprintf(p->name, sizeof p->name, "%s", name);
  p->min = min; p->L = L; p->W = W; p->rotate = rot;
  p->Lr = p->L / p->res; p->Wr = p->W / p->res; p->grain = grain;
  if (reset) { p->target = p->min; p->done = 0; p->placed = 0; p->remain = p->target; }
}
static void part_cat(rn_part *p, const double *r) {                           /* FUN_1801e45e0 L209928 */
  double v = ((p->Lr * p->Wr) / (r[2] * r[3])) * 100.0;
  if (v < 3.000001 && 1e-06 < fabs(v - 3.0)) { p->cat = 2; return; }
  p->cat = 1;
}
static void part_rot90(rn_part *p) {                                            /* FUN_1801e4ac0 L210122 */
  double t; p->rotFlag = 1; t = p->L; p->L = p->W; p->W = t; t = p->Lr; p->Lr = p->Wr; p->Wr = t;
}
static void part_grain(rn_part *p, char g) {                                    /* FUN_1801e4b60 L210140 */
  if (g != 'N' && p->grain != 'N') {
    if (g == 'X') { if (p->grain == 'Y' && p->rotate != 0) part_rot90(p); }
    else if (g == 'Y' && p->grain == 'X' && p->rotate != 0) part_rot90(p);
    p->rotate = 0;
  }
}
static int part_dec(rn_part *p) { p->placed++; p->remain--; return -1 < p->remain; }  /* FUN_1801e4790 L209987 */
static void part_settarget(rn_part *p, int v) { if (-1 < v) { p->target = v; p->done = p->min - p->target; } } /* FUN_1801e47e0 */
static void part_filler(rn_part *p) {                                          /* FUN_1801e49b0 L210079 */
  int d = p->max - p->min; p->done = p->fill; p->target = d; p->placed = 0; p->remain = p->remain == 0 ? d : 0;
}
/* counter copy FUN_1801e48d0 L210049 (get) + FUN_1801e4940 L210064 (set) */
static void cnt_copy(rn_part *d, const rn_part *s) { d->done = s->done; d->target = s->target; d->placed = s->placed; d->remain = s->remain; }

static double ovl(const double *p1, const double *p2) {                         /* FUN_1801e5000 L210252: overlap area */
  double w = 0.0, h = 0.0, d;
  if (p2[0] + 1e-06 < p1[0] + p1[2] && (d = (p2[0] + p2[2]) - 1e-06, p1[0] < d)) {
    if (p2[0] + 1e-06 <= p1[0]) w = (p1[0] + p1[2] + 1e-06 <= p2[0] + p2[2]) ? p1[2] : (p2[0] + p2[2]) - p1[0];
    else w = (p2[0] + p2[2] + 1e-06 <= p1[0] + p1[2]) ? p2[2] : (p1[0] + p1[2]) - p2[0];
  }
  if (p2[1] + 1e-06 < p1[1] + p1[3] && p1[1] < (p2[1] + p2[3]) - 1e-06) {
    if (p2[1] + 1e-06 <= p1[1]) h = (p1[1] + p1[3] + 1e-06 <= p2[1] + p2[3]) ? p1[3] : (p2[1] + p2[3]) - p1[1];
    else h = (p2[1] + p2[3] + 1e-06 <= p1[1] + p1[3]) ? p2[3] : (p1[1] + p1[3]) - p2[1];
  }
  return w * h;
}
static int isect(const double *a, const double *b) {                            /* FUN_1801e4d80 L210205 */
  double ax = a[0] + a[2] / 2.0, ay = a[1] + a[3] / 2.0, bx = b[0] + b[2] / 2.0, by = b[1] + b[3] / 2.0;
  if (fabs(ax - bx) < fabs((a[2] + b[2]) / 2.0) - 1e-06 && fabs(ay - by) < fabs((a[3] + b[3]) / 2.0) - 1e-06) return 1;
  if (fabs(ay - by) < fabs((a[3] + b[3]) / 2.0) - 1e-06 && fabs(ax - bx) < fabs((a[2] + b[2]) / 2.0) - 1e-06) return 1;
  return 0.0 < ovl(a, b);
}
/* FUN_1801e5320 L210311: cut segment s by the placed bottom edge b (same y only); out[0..k-1] = remaining pieces */
static int seg_cut(const rn_seg *b, const rn_seg *s, rn_seg *o, int *k) {
  *k = 0;
  if (b->y1 != s->y1) return 0;
  o[0].y1 = o[0].y2 = o[1].y1 = o[1].y2 = b->y1;
  if (b->x1 < s->x1) {
    if (b->x2 <= s->x1) return 0;
    if (s->x2 < b->x2) *k = 0;
    else { *k = 1; o[0].x1 = b->x2; o[0].x2 = s->x2; if (fabs(o[0].x1 - o[0].x2) == 0.0) (*k)--; }  /* FUN_1801e52f0 */
  } else {
    if (s->x2 <= b->x1) return 0;
    if (b->x2 < s->x2) {
      *k = 2; o[0].x1 = s->x1; o[0].x2 = b->x1;
      if (fabs(o[0].x1 - o[0].x2) == 0.0) {
        (*k)--; o[0].x1 = b->x2; o[0].x2 = s->x2;
        if (fabs(o[0].x1 - o[0].x2) == 0.0) (*k)--;
      } else { o[1].x1 = b->x2; o[1].x2 = s->x2; if (fabs(o[0].x1 - o[0].x2) == 0.0) (*k)--; } /* checks o[0] again, as L210366 */
    } else { *k = 1; o[0].x1 = s->x1; o[0].x2 = b->x1; if (fabs(o[0].x1 - o[0].x2) == 0.0) (*k)--; }
  }
  for (int i = 0; i < *k; i++) o[i].len = o[i].x2 - o[i].x1;
  return 1;
}
/* FUN_1801e5660 L210393: merge top edge t into segment s when same y and gap <= 1.0 (literal 1.0, raster units) */
static int seg_merge(const rn_seg *s, const rn_seg *t, rn_seg *o) {
  double v[4], x; int i, j;
  if (s->y1 != t->y1) return 0;
  v[0] = s->x1; v[1] = s->x2; v[2] = t->x1; v[3] = t->x2;
  for (i = 0; i < 4; i++) for (j = 0; j < 3 - i; j++) if (v[j + 1] < v[j]) { x = v[j]; v[j] = v[j + 1]; v[j + 1] = x; }
  if (1.0 < v[2] - v[1]) return 0;
  o->y2 = s->y1; o->y1 = o->y2; o->x1 = v[0]; o->x2 = v[3]; o->len = fabs(v[3] - v[0]);
  return 1;
}

/* ---------------- optiRectOrder core (0x1801b65e0-0x1801c6060) ---------------- */
rn_ctx *rn_new(double cutter, double clearance, double resolution, const rn_callbacks *cb) {
  rn_ctx *c = calloc(1, sizeof *c); int i;
  (void)resolution;                                                             /* not stored: asm 0x1801b6a0a stores 1.0 */
  if (cb) c->cb = *cb;
  c->A = calloc(RN_MAXPART, sizeof(rn_part)); c->B = calloc(RN_MAXPART, sizeof(rn_part));
  c->C = calloc(RN_MAXPART, sizeof(rn_part)); c->D = calloc(RN_MAXPART, sizeof(rn_part));
  c->S = calloc(RN_MAXSHEET, sizeof(rn_sheet));
  c->rec.v = calloc(RN_MAXLIST, sizeof(rn_rec)); c->best.v = calloc(RN_MAXLIST, sizeof(rn_rec));
  c->seg.v = calloc(RN_MAXLIST, sizeof(rn_seg));
  for (i = 0; i < RN_MAXPART; i++) {                                             /* FUN_1801e42c0 L209853 */
    rn_part *p[4] = { &c->A[i], &c->B[i], &c->C[i], &c->D[i] };
    for (int k = 0; k < 4; k++) { p[k]->res = 1.0; p[k]->prio = 1; }
  }
  /* FUN_1801b6890 L192918 */
  c->cutter = cutter; c->clr = clearance; c->res = 1.0; c->state = 1;
  c->offR = (cutter + c->clr / 2.0) / c->res; c->off = cutter + c->clr / 2.0;
  c->itSheet = -1; c->itPart = -1;
  return c;
}
void rn_free(rn_ctx *c) {
  if (!c) return;
  free(c->A); free(c->B); free(c->C); free(c->D); free(c->S); free(c->rec.v); free(c->best.v); free(c->seg.v); free(c);
}
void rn_set_fillers_upto_nested_height(rn_ctx *c, int on) { if (on == 1) c->fillers = 1; }
void rn_set_method_type(rn_ctx *c, int m) { c->method = m; }

/* RNAddPartInfo2 L19005 -> FUN_1800e7090 L105556 (grain not X/Y -> 'N') -> FUN_1801b6f60 L193023 (param_5 = 1:
 * no geometry callback) */
int rn_add_part(rn_ctx *c, const char *name, int minQty, int maxQty, double len, double wid, int rotate, char grain) {
  rn_part *p; int r;
  if (c->np >= RN_MAXPART) rn_die(c, "more than 5000 part types");
  if (grain != 'X' && grain != 'Y') grain = 'N';
  p = &c->A[c->np];
  p->res = c->res;                                                              /* FUN_1801b72b0 */
  part_set(p, name, minQty, len + c->off * 2.0, wid + c->off * 2.0, rotate, grain, 1);
  p->max = (minQty == 0 || maxQty < minQty) ? minQty : maxQty;                  /* FUN_1801b72f0 L193062 */
  p->max0 = p->max;                                                             /* FUN_1801b7310 */
  p->f140 = 0; p->f148 = 0;                                                     /* FUN_1801b7330/7350 */
  part_set(p, p->name, p->min, p->L, p->W, p->rotate, p->grain, 1);            /* FUN_1801e4700 again L193070 */
  r = 1; c->np++;
  return r;
}
int rn_set_part_priority(rn_ctx *c, int prio) { if (c->np > 0) c->A[c->np - 1].prio = prio; return 1; }

/* RNAddSheetInfo2 L19247 -> FUN_1800e71a0 L105588 (4 equal collars) -> FUN_1801b73d0 L193164 -> FUN_1801e3ba0 L209721 */
int rn_add_sheet(rn_ctx *c, const char *name, int qty, double len, double wid, double collar, char grain) {
  rn_sheet *s; int i;
  if (c->ns >= RN_MAXSHEET) rn_die(c, "more than 1000 sheet types");
  if (grain != 'X' && grain != 'Y') grain = 'N';
  s = &c->S[c->ns];
  s->res = c->res;                                                              /* FUN_1801b74f0 */
  snprintf(s->name, sizeof s->name, "%s", name);
  s->qty = qty; s->left = qty; s->len = len; s->wid = wid; s->cflag = 0;
  for (i = 0; i < 4; i++) s->collar[i] = collar;
  s->f168 = 0;
  for (i = 0; i < 4; i++) s->collarR[i] = s->collar[i] / s->res;
  s->Lr = s->len / s->res; s->Wr = s->wid / s->res;
  s->grain = grain; s->f1ac = 1; s->f1b0 = 'Y';
  c->ns++;
  return 1;
}

/* FUN_1801e3fd0 L209804: negative collar larger than the offset is clamped */
static void sheet_collar_adjust(rn_sheet *s, double cutter, double clr) {
  if (s->collar[0] <= -1e-06 && (cutter + clr) - 1e-06 < fabs(s->collar[0] + clr / 2.0)) {
    s->collar[0] = (0.0 - (cutter + clr)) - clr / 2.0;
    if (s->cflag == 0) {
      s->collar[1] = s->collar[0]; s->collar[2] = s->collar[0]; s->collar[3] = s->collar[0];
      for (int i = 0; i < 4; i++) s->collarR[i] = s->collar[i] / s->res;
    }
  }
}

static void reset_parts(rn_ctx *c) {                                            /* FUN_1801b78e0 L193336 */
  for (int i = 0; i < c->np; i++) { c->A[i].placed = 0; c->A[i].remain = c->A[i].target; } /* FUN_1801e4760 */
  c->rec.n = 0; c->seg.n = 0;
}

/* FUN_1801bf1c0 L196024: segment from p to q */
static void seg_push(rn_ctx *c, double px, double py, double qx, double qy) {
  rn_seg *s = seg_add(c); s->x1 = px; s->y1 = py; s->x2 = qx; s->y2 = qy; s->len = fabs(qx - px);
}

/* FUN_1801bf6c0 L196179: skyline update after placing rect r */
static void seg_update(rn_ctx *c, const double *r) {
  rn_seg b = { r[0], r[1], r[0] + r[2], r[1], r[2] }, t = { r[0], r[1] + r[3], r[0] + r[2], r[1] + r[3], r[2] }, o[2], m;
  int n = c->seg.n, i, j, k;
  for (i = 0; i < n; i++) {
    rn_seg s = c->seg.v[i];
    if (seg_cut(&b, &s, o, &k) == 1) {
      if (k == 0) { for (j = i; j < n - 1; j++) c->seg.v[j] = c->seg.v[j + 1]; n--; c->seg.n--; i--; }
      else if (k == 1) c->seg.v[i] = o[0];
      else if (k == 2) { c->seg.v[i] = o[0]; *seg_add(c) = o[1]; }
    }
  }
  for (j = 0; j < c->seg.n; j++) if (seg_merge(&c->seg.v[j], &t, &m) == 1) break;
  if (j < c->seg.n) c->seg.v[j] = m; else *seg_add(c) = t;
}

static void extents(const double *r, double *top, double *right) {             /* FUN_1801c5950 L198413 */
  if (*top - 1e-06 < r[1] + r[3] && 1e-06 < fabs((r[1] + r[3]) - *top)) *top = r[1] + r[3];
  if (*right - 1e-06 < r[0] + r[2] && 1e-06 < fabs((r[0] + r[2]) - *right)) *right = r[0] + r[2];
}

/* grain compatibility test shared by FUN_1801bd690 L195440 and FUN_1801c3a40 L197917 */
static int grain_ok(char sg, const rn_part *p) {
  return (sg != 'N' || (p->grain != 'X' && p->grain != 'Y')) && (sg != 'X' || p->grain != 'Y' || p->rotFlag != 0) &&
         (sg != 'Y' || p->grain != 'X' || p->rotFlag != 0);
}

/* FUN_1801be7d0 L195769: place cand (L = cand[2], W = cand[3]) on segment a, sliding right past overlaps.
 * out = (x, y, L, W); *score = contact percent. */
static int try_seg(rn_ctx *c, const double *rect, const double *cand, double *out, int a, double *score) {
  rn_seg s = c->seg.v[a]; double lim, start, slide = 0.0, h[4], v[4], area, sum; int ov = 0, found = 0, i;
  out[0] = (s.x1 - cand[2]) + 1.0;
  lim = (s.len + cand[2]) - 1.0;
  if (out[0] < rect[0]) { lim = lim + out[0]; out[0] = rect[0]; }
  start = out[0]; out[1] = s.y1; out[2] = cand[2]; out[3] = cand[3];
  for (;;) {
    if (lim <= slide) break;
    if (rect[1] + rect[3] < out[1] + out[3] || rect[0] + rect[2] < out[0] + out[2]) return 0;
    ov = 0;
    for (i = 0; i < c->rec.n; i++) {
      rn_rec *r = &c->rec.v[i];
      ov = isect(out, &r->x);
      if (ov == 1) {
        if (s.x1 + s.len <= r->x + r->L) return 0;
        out[0] = r->x + r->L; slide = out[0] - start;
        break;
      }
    }
    if (ov == 0) { found = 1; break; }
    slide = slide + 1.0;
  }
  if (found != 1) return 0;
  h[0] = out[0] - c->t; h[1] = out[1]; h[2] = out[2] + c->t * 2.0; h[3] = out[3];
  v[0] = out[0]; v[1] = out[1] - c->t2; v[2] = out[2]; v[3] = out[3] + c->t2 * 2.0;
  area = out[3] * 2.0 * c->t + out[2] * 2.0 * c->t2;
  sum = 0.0;
  for (i = 0; i < c->rec.n; i++) { sum = sum + ovl(v, &c->rec.v[i].x); sum = sum + ovl(h, &c->rec.v[i].x); }
  *score = (sum / area) * 100.0;
  return 1;
}

/* FUN_1801be190 L195601: best segment / orientation for one part */
static int best_pos(rn_ctx *c, const double *rect, const double *cand, int rotate, double *best, int *rot, int *segi, double *score) {
  double bc = 1000000.0, bs = 0.0, o[4], sc = 0.0, ce, cr[4] = { cand[0], cand[1], cand[3], cand[2] }; int any = 0, a, k;
  for (a = 0; a < c->seg.n; a++) {
    for (k = 0; k < 2; k++) {
      if (k == 1 && rotate == 0) break;
      if (try_seg(c, rect, k ? cr : cand, o, a, &sc) != 1) continue;
      any = 1;
      ce = c->dir == 2 ? (o[0] + o[2] / 2.0) / rect[2] : (o[1] + o[3] / 2.0) / rect[3];
      if (1e-06 < fabs(sc - bs)) {
        if (bs < sc) { *segi = a; memcpy(best, o, sizeof o); *score = sc; bs = sc; *rot = k; bc = ce; }
      } else if (ce < bc - 1e-06) { *segi = a; memcpy(best, o, sizeof o); *score = sc; bs = sc; *rot = k; bc = ce; }
    }
  }
  return any;
}

/* FUN_1801c3a40 L197851: randomised first row (fill direction 3, empty sheet only) */
static void random_row(rn_ctx *c, const double *rect, int cat, char sg) {
  enum { NA = 0x7d1 };
  static int bestSel[NA], bestCnt[NA], sel[NA], cnt[NA];
  int n = c->np, it, j, i, k, idx, v; double bestLeft, sum, left, x;
  if (!(c->rec.n < 5)) return;
  if (2 * n > NA) rn_die(c, "random row: more than 1000 part types (array 0x7d1)");
  bestLeft = rect[2];
  for (i = 0; i < NA; i++) { bestCnt[i] = 0; cnt[i] = 0; bestSel[i] = -1; sel[i] = -1; }
  msvc_srand(0);
  for (it = 0; it < 3000; it++) {
    for (i = 0; i < NA; i++) sel[i] = -1;
    for (i = 0; i < NA; i++) cnt[i] = 0;
    k = msvc_rand() % (n << 1);
    sum = 0.0;
    for (j = 0; j < k; j++) {
      idx = msvc_rand() % (n << 1);
      if (n - 1 < idx) {                                                        /* rotated variant of part idx-n */
        rn_part *p = &c->A[idx - n];
        if (p->cat == cat && p->prio == c->prio && 0 < p->remain) {
          if (p->rotate != 0 && grain_ok(sg, p) && p->Lr <= rect[3] + 1e-06) {
            sum = sum + p->Wr;
            if (rect[2] + 1e-06 < sum) break;
            v = (p->remain - cnt[idx]) - cnt[idx - n];
            if (0 < v) cnt[idx]++;
            if (0 < v) sel[idx] = 1;                                           /* LAB_1801c4427 */
          }
        }
      } else {
        rn_part *p = &c->A[idx];
        if (p->cat == cat && p->prio == c->prio && 0 < p->remain) {
          if (grain_ok(sg, p) && p->Wr <= rect[3] + 1e-06) {
            sum = sum + p->Lr;
            if (!(sum <= rect[2] + 1e-06)) break;
            v = (p->remain - cnt[idx]) - cnt[idx + n];
            if (0 < v) cnt[idx]++;
            if (0 < v) sel[idx] = 1;
          }
        }
      }
    }
    sum = 0.0;
    for (i = 0; i < n * 2; i++) {
      if (sel[i] != 1) continue;
      rn_part *p = &c->A[n - 1 < i ? i - n : i];
      if (p->cat == cat && p->prio == c->prio && 0 < p->remain) {
        if (n - 1 < i) { if (p->rotate != 0) sum = sum + (double)cnt[i] * p->Wr; }
        else sum = sum + (double)cnt[i] * p->Lr;
      }
    }
    if (sum < rect[2] - 1e-06 && (left = rect[2] - sum, left < bestLeft - 1e-06)) {
      for (i = 0; i < NA; i++) bestSel[i] = sel[i];
      for (i = 0; i < NA; i++) bestCnt[i] = cnt[i];
      bestLeft = left;
    }
  }
  x = rect[0];
  for (i = 0; i < n * 2; i++) {
    if (bestSel[i] != 1) continue;
    while (0 < bestCnt[i]) {                                                     /* LAB_1801c4ac0 */
      int r = n - 1 < i; rn_part *p = &c->A[r ? i - n : i]; rn_rec *q; double pr[4];
      if (r && p->rotate == 0) rn_die(c, "random row: rotated slot of a non-rotatable part (original loops forever)");
      pr[0] = x; pr[1] = rect[1]; pr[2] = r ? p->Wr : p->Lr; pr[3] = r ? p->Lr : p->Wr;
      q = rec_add(c, &c->rec);
      memset(q, 0, sizeof *q); snprintf(q->name, sizeof q->name, "%s", p->name);
      q->x = pr[0]; q->y = pr[1]; q->L = pr[2]; q->W = pr[3];
      q->rot = r ? (p->rotFlag != 1) : (p->rotFlag == 1);
      q->f128 = p->f140; q->f130 = p->f148;
      seg_update(c, pr);                                                          /* (segment index arg unused) */
      part_dec(p);
      x = x + pr[2];
      bestCnt[i] = bestCnt[i] + -1;                                              /* LAB_1801c5906 */
    }
  }
}

/* FUN_1801bd690 L195334: greedy placement of all parts of category cat at the current priority */
static void greedy(rn_ctx *c, const double *rect, int cat, char sg) {
  /* function-level locals of the original (local_1fc, local_1cc, local_b8, local_4a8, local_a8, local_78, local_1b8):
   * initialised once, kept across iterations of the while loop */
  int any = 1, i, bi = 0, brot = 0, bseg = 0; double b140 = 0, b148 = 0, bpos[4] = { 0 }; char bname[256] = "";
  while (any != 0) {
    double bestSc = 0.0, bestArea = 0.0;                                         /* local_208 / local_1d8 */
    any = 0;
    if (c->dir == 3 && cat == 1) random_row(c, rect, 1, sg);
    for (i = 0; i < c->np; i++) {
      rn_part *p = &c->A[i]; double cand[4], pos[4], sc = 0.0; int rot = 0, segi = 0, ok = 0;
      if (!(p->cat == cat && c->rec.n < 10000 && p->prio == c->prio && 0 < p->remain)) continue;
      if (!grain_ok(sg, p)) continue;
      cand[0] = 0.0; cand[1] = 0.0; cand[2] = p->Lr; cand[3] = p->Wr;
      if (p->fits == 1) ok = best_pos(c, rect, cand, p->rotate, pos, &rot, &segi, &sc);
      if (ok != 1) continue;
      any = 1;
      if (sc <= bestSc + 1e-06) {
        if (!(fabs(sc - bestSc) <= 1e-06 && bestArea + 1e-06 < cand[2] * cand[3])) continue;
      }
      snprintf(bname, sizeof bname, "%s", p->name); bestArea = cand[2] * cand[3];
      b140 = p->f140; b148 = p->f148; bestSc = sc; bi = i; memcpy(bpos, pos, sizeof pos);
      brot = rot; if (p->rotFlag == 1) brot = brot == 0;
      bseg = segi;
    }
    if (any != 0) {
      rn_rec *q = rec_add(c, &c->rec);
      memset(q, 0, sizeof *q); snprintf(q->name, sizeof q->name, "%s", bname);
      q->x = bpos[0]; q->y = bpos[1]; q->L = bpos[2]; q->W = bpos[3]; q->rot = brot; q->f128 = b140; q->f130 = b148;
      (void)bseg;
      seg_update(c, bpos);
      extents(bpos, &c->maxY, &c->maxX);
      part_dec(&c->A[bi]);
    }
  }
}

static int cmp_prio_desc(const void *a, const void *b) {                        /* FUN_1801c5ea0 L198550 */
  int x = *(const int *)a, y = *(const int *)b;
  return y < x ? -1 : (x < y ? 1 : 0);
}

static void wall(rn_ctx *c, double x, double y, double L, double W) {
  rn_rec *q = rec_add(c, &c->rec); memset(q, 0, sizeof *q); strcpy(q->name, "dummyPart");
  q->x = x; q->y = y; q->L = L; q->W = W; q->rot = 0;
}

/* FUN_1801bbde0 L194794: one trial.  Returns the utilisation after the minimum-quantity phase (xmm0 = local_3c8,
 * asm 0x1801bd62c); *util = utilisation including the filler phase. */
static double trial(rn_ctx *c, const double *rect, double *util, char sg, double P5, double P6) {
  double r2[4] = { rect[0], rect[1], rect[2], rect[3] }, u1, a; int i, j, np = c->np, nd = 0, *pr;
  int *sv0, *sv1, *sv2, *sv3;
  c->t = rect[2] * 0.03; if (c->t < 1e-06) c->t = 1.0;
  c->t2 = rect[3] * 0.03; if (c->t2 < 1e-06) c->t2 = 1.0;
  seg_push(c, rect[0], rect[1], rect[0] + rect[2], rect[1]);
  wall(c, rect[0] - c->t, rect[1], c->t, rect[3]);
  wall(c, rect[0] + rect[2], rect[1], c->t, rect[3]);
  wall(c, rect[0], rect[1] - c->t2, rect[2], c->t2);
  wall(c, rect[0], rect[1] + rect[3], rect[2], c->t2);
  for (i = 0; i < np; i++) part_cat(&c->A[i], rect);
  /* distinct priorities; the array is malloc'd (thunk_FUN_180225c3c) and the search runs over all np slots, so
   * unwritten slots take part: the oracle's HeapAlloc hands out zeroed memory (tools/peloader README), calloc here */
  pr = calloc(np > 0 ? np : 1, sizeof(int));
  pr[0] = c->A[0].prio; nd = 1;
  for (i = 1; i < np; i++) {
    int p = c->A[i].prio;
    for (j = 0; j < np && p != pr[j]; j++) ;
    if (np <= j) pr[nd++] = p;
  }
  msvc_qsort(pr, (unsigned long long)nd, 4, cmp_prio_desc);
  for (j = 0; j < nd; j++) { c->prio = pr[j]; greedy(c, rect, 1, sg); greedy(c, rect, 2, sg); }
  a = 0.0;
  for (i = 4; i < c->rec.n; i++) a = a + (c->rec.v[i].L - c->offR * 2.0) * (c->rec.v[i].W - c->offR * 2.0);
  u1 = (a / (P5 * P6)) * 100.0;
  if (c->fillers == 1) { r2[3] = c->maxY; r2[2] = c->maxX; }
  /* filler phase: remaining := max - min where the minimum was met (FUN_1801e49b0) */
  sv0 = calloc(np, sizeof(int)); sv1 = calloc(np, sizeof(int)); sv2 = calloc(np, sizeof(int)); sv3 = calloc(np, sizeof(int));
  for (i = 0; i < np; i++) { rn_part *p = &c->A[i]; sv0[i] = p->target; sv1[i] = p->done; sv2[i] = p->placed; sv3[i] = p->remain; part_filler(p); }
  for (j = 0; j < nd; j++) { c->prio = pr[j]; greedy(c, r2, 1, sg); greedy(c, r2, 2, sg); }
  free(pr);
  for (i = 0; i < np; i++) {
    rn_part *p = &c->A[i]; int f = p->placed;
    p->fill += f; p->fillTot += f;                                               /* FUN_1801e4890 */
    p->max = p->max - p->placed;                                                 /* FUN_1801b72f0(.., max - FUN_1801bd650) */
    p->target = sv0[i]; p->done = sv1[i]; p->placed = sv2[i]; p->remain = sv3[i];
  }
  free(sv0); free(sv1); free(sv2); free(sv3);
  a = 0.0;
  for (i = 4; i < c->rec.n; i++) a = a + (c->rec.v[i].L - c->offR * 2.0) * (c->rec.v[i].W - c->offR * 2.0);
  *util = (a / (P5 * P6)) * 100.0;
  return u1;
}

/* FUN_1801c5b10 L198447: which parts can still go on the sheet; 0 = none */
static int fits_check(rn_ctx *c, const double *rect) {
  int any = 0, u6 = 0;
  for (int i = 0; i < c->np; i++) {
    rn_part *p = &c->A[i]; int m = 0;
    if (p->done < p->min) {
      int u44 = p->Lr < rect[2] + 1e-06;
      if (p->Lr < rect[3] + 1e-06) u44 += 2;
      if (u44 == 0) p->fits = 0;
      else {
        u6 = 0;
        if (p->Wr < rect[2] + 1e-06) u6++;
        if (p->Wr < rect[3] + 1e-06) u6 += 2;
        if (u6 == 0 || (u6 != 3 && u6 == u44)) p->fits = 0; else { p->fits = 1; m = 1; }
      }
    }
    if (m == 1) any = 1;
  }
  return any;
}

/* snapshots: FUN_1801c1ed0 L197279 (A->B), FUN_1801c2cf0 L197639 (B->A), FUN_1801c2fb0 L197678 (A->C),
 * FUN_1801c33b0 L197745 (C->A), FUN_1801c3640 L197783 (A->D geometry), FUN_1801c3840 L197817 (D->A) */
static void snap_to(rn_ctx *c, rn_part *dst, const double *rect) {
  for (int i = 0; i < c->np; i++) {
    rn_part *a = &c->A[i], *d = &dst[i];
    if (rect) d->res = c->res;
    part_set(d, a->name, a->min, a->L, a->W, a->rotate, a->grain, 1);
    if (rect) part_cat(d, rect);
    cnt_copy(d, a); d->max = a->max; d->fill = a->fill; d->fillTot = a->fillTot;
    d->f140 = a->f140; d->f148 = a->f148; d->prio = a->prio;
  }
}
static void snap_from(rn_ctx *c, const rn_part *src) {
  for (int i = 0; i < c->np; i++) {
    rn_part *a = &c->A[i]; const rn_part *s = &src[i];
    cnt_copy(a, s); a->max = s->max; a->fill = s->fill; a->fillTot = s->fillTot;
    a->f140 = s->f140; a->f148 = s->f148; a->prio = s->prio;
  }
}
static void geom_save(rn_ctx *c) {
  for (int i = 0; i < c->np; i++) {
    rn_part *a = &c->A[i], *d = &c->D[i];
    d->L = a->L; d->W = a->W; d->Lr = a->Lr; d->Wr = a->Wr; d->rotate = a->rotate; d->rotFlag = a->rotFlag;
    d->f140 = a->f140; d->f148 = a->f148; d->prio = a->prio;
  }
}
static void geom_restore(rn_ctx *c) {
  for (int i = 0; i < c->np; i++) {
    rn_part *a = &c->A[i], *d = &c->D[i];
    a->L = d->L; a->W = d->W; a->Lr = d->Lr; a->Wr = d->Wr; a->rotate = d->rotate; a->rotFlag = d->rotFlag;
    a->f140 = d->f140; a->f148 = d->f148; a->prio = d->prio;
  }
}
/* the 90-degree turn of every part for pass 2 (L193955-193995 / L194118-194155): swap L/W, Lr/Wr, grain X<->Y */
static char flip(char g) { return g == 'X' ? 'Y' : g == 'Y' ? 'X' : g; }
static void parts_turn(rn_ctx *c, int keepRotFlag) {
  for (int i = 0; i < c->np; i++) {
    rn_part *p = &c->A[i]; double L = p->L, W = p->W;
    if (!keepRotFlag) p->rotFlag = 0;                                            /* FUN_1801e4c20 with local_1cc4 = 0 */
    part_set(p, p->name, p->min, W, L, p->rotate, flip(p->grain), 0);          /* FUN_1801e4430 */
  }
}

/* FUN_1801bf4c0 L196132: identical sheets - repeat the layout k more times */
static int repeats(rn_ctx *c, int s) {
  int k = 0x7fffffff, i, l;
  for (i = 0; i < c->np; i++) { rn_part *p = &c->A[i]; if (p->placed != 0 && p->remain / p->placed < k) k = p->remain / p->placed; }
  l = c->S[s].left;
  if (k != 0x7fffffff && l + -1 < k) k = l + -1;
  if (0 < c->S[s].left - k) c->S[s].left = c->S[s].left - k;                    /* FUN_1801bbdb0 */
  for (i = 0; i < c->np; i++) { rn_part *p = &c->A[i]; if (p->placed != 0) part_settarget(p, p->remain - k * p->placed); }
  if (k == 0x7fffffff) k = 0;
  return k;
}

static int any_left(rn_ctx *c) { for (int i = 0; i < c->np; i++) if (0 < c->A[i].remain) return 1; return 0; } /* FUN_1801bf160 */

/* FUN_1801b7c30 L193410: nest all instances of sheet type s.  1 = sheet type exhausted, 0 = nothing fits / failed,
 * 2 = everything nested.  rect = usable area (x0, y0, L, W), the 0x20 buffer of FUN_1801b7960. */
static int nest_sheet(rn_ctx *c, int s, double *rect) {
  rn_sheet *sh = &c->S[s]; double Lr, Wr, best = 0.0, util = 0.0, u, tmp; char sg, f1b0;
  int eb0 = 0, t, i;
  sheet_collar_adjust(sh, c->cutter, c->clr);
  Lr = sh->Lr; Wr = sh->Wr; sg = sh->grain; f1b0 = sh->f1b0;                    /* FUN_1801e3e00 L193699 */
  for (;;) {                                                                    /* LAB_1801b844f */
    if (sh->qty <= sh->used) return 1;
    if (sh->cflag == 1) {                                                       /* DAT_1802ac0a4 = 1 (.data) -> else-branch */
      rect[0] = sh->collarR[0] + c->clr / (c->res * 2.0); rect[1] = sh->collarR[3] + c->clr / (c->res * 2.0);
      rect[2] = (Lr - (sh->collarR[0] + sh->collarR[2])) - c->clr / c->res;
      rect[3] = (Wr - (sh->collarR[1] + sh->collarR[3])) - c->clr / c->res;
    } else {
      rect[0] = sh->collarR[0] + c->clr / (c->res * 2.0); rect[1] = sh->collarR[0] + c->clr / (c->res * 2.0);
      rect[2] = (Lr - sh->collarR[0] * 2.0) - c->clr / c->res;
      rect[3] = (Wr - sh->collarR[0] * 2.0) - c->clr / c->res;
    }
    geom_save(c);
    for (i = 0; i < c->np; i++) {
      c->A[i].fillTot = c->A[i].fillTot + c->A[i].fill; c->A[i].fill = 0;      /* FUN_1801bbd70 */
      part_grain(&c->A[i], sg);
    }
    c->pass = 0;
    do {
      c->pass = c->pass + 1;
      if (c->pass == 2) {                                                       /* turn the sheet 90 degrees */
        tmp = rect[2]; rect[2] = rect[3]; rect[3] = tmp;
        tmp = Wr; Wr = Lr; Lr = tmp; sg = flip(sg);
        parts_turn(c, 1);
      }
      for (t = 0; t < 3; t++) {
        if (t == 0) {
          if (c->pass == 1) { best = 0.0; snap_to(c, c->B, rect); } else snap_from(c, c->B);
          c->dir = (Wr <= Lr - 1e-06) ? 2 : 1;
        } else if (t == 1) { snap_from(c, c->B); c->dir = c->dir == 1 ? 2 : 1; }
        else { snap_from(c, c->B); c->dir = 3; }
        if (fits_check(c, rect) == 0) return 0;
        reset_parts(c);
        u = trial(c, rect, &util, sg, Lr, Wr);
        if (best + 1e-06 < u) {
          c->best.n = c->rec.n; memcpy(c->best.v, c->rec.v, sizeof(rn_rec) * c->rec.n); /* FUN_1801c2390 */
          best = u; c->bestUtil = util;
          snap_to(c, c->C, NULL);
          if (c->pass == 2) eb0 = 1;
#ifdef RN_TRACE
          fprintf(stderr, "RN_TRACE best sheet=%d pass=%d trial=%d dir=%d u=%.6f util=%.6f\n", s, c->pass, t, c->dir, u, util);
#endif
        }
      }
      if (c->pass == 2) {                                                       /* turn back */
        tmp = rect[2]; rect[2] = rect[3]; rect[3] = tmp;
        tmp = Wr; Wr = Lr; Lr = tmp; sg = flip(sg);
        parts_turn(c, 0);
      }
    } while (c->pass < 2);
    geom_restore(c);
    if (c->rec.n == 4) return 0;                                                /* the working list of the last trial */
    snap_from(c, c->C);
    c->rec.n = c->best.n; memcpy(c->rec.v, c->best.v, sizeof(rn_rec) * c->best.n); /* FUN_1801c2840 */
    if (c->cb.new_sheet && !c->cb.new_sheet(c->cb.user, sh->name, sh->len, sh->wid)) return 0; /* FUN_1801c0b30 */
    if (eb0 == 1) {                                                             /* best layout came from the turned sheet */
      for (i = 4; i < c->rec.n; i++) {
        rn_rec *r = &c->rec.v[i];
        if (f1b0 == 'X') f1b0 = 1; else if (f1b0 == 'Y') f1b0 = 2;
        if (f1b0 == 2) { tmp = r->x; r->x = r->y; r->y = tmp; tmp = r->L; r->L = r->W; r->W = tmp; }
        if (f1b0 == 1) r->rot = r->rot == 0;
      }
    }
    i = repeats(c, s);
    for (int q = 4; q < c->rec.n; q++) {                                       /* part-nested callbacks (local copy) */
      rn_rec r = c->rec.v[q];
      c->state = 4;
      if (eb0 == 1 && f1b0 == 1) { tmp = r.x; r.x = r.y; r.y = tmp; r.rot = r.rot == 0; }
      if (c->cb.part_nested) c->cb.part_nested(c->cb.user, r.name, (r.x + c->offR) * c->res, (r.y + c->offR) * c->res, r.rot);
      c->state = 1;
    }
    c->nSheets = c->nSheets + 1 + i;
    sh->used = i + 1 + sh->used;
    c->state = 3;                                                               /* FUN_1801c0c60 L196718 */
    t = c->cb.sheet_result ? c->cb.sheet_result(c, c->cb.user, i + 1, c->bestUtil) : 1;
    c->state = 1;
    if (t == 0) return 0;
    if (any_left(c) == 0) return 2;
    eb0 = 0;
    if (0 < sh->qty - c->nSheets) sh->left = sh->qty - c->nSheets;             /* FUN_1801bbdb0 */
  }
}

int rn_nest_order(rn_ctx *c) {                                                  /* FUN_1801b7960 L193352 */
  double rect[4] = { 0 }; int s, r, ret;
  if (c->method != 0) return 0;
  c->nTypes = 0; c->nSheets = 0; c->state = 1;                                  /* FUN_1801b6f20 */
  c->state = 2;
  c->dummy = c->np == 1;
  if (c->dummy) {
    rn_part *p = &c->A[c->np++];
    p->done = 0;                                                                /* FUN_1801b7c10 */
    part_set(p, "DummyPart", 0, 0, 0, 1, 'N', 1);
    p->max = 0; p->max0 = 0;
  }
  for (s = 0; s < c->ns; s++) {
    reset_parts(c);
    c->cur = s;
    r = nest_sheet(c, s, rect);
    if ((r == 0 && s == c->ns + -1) || r == 2) break;
    c->nTypes = c->nTypes + 1;
  }
  ret = c->cb.order_result ? c->cb.order_result(c, c->cb.user, c->nTypes, c->nSheets) : 1;
  c->state = 1;
  return ret;
}

/* ---------------- queries ---------------- */
static int inq_err(rn_ctx *c) { if (c->cb.error) c->cb.error(c->cb.user, 0x10, 3, "Cannot Inquire in this state."); return 0; }
int rn_inq_nested_sheet(rn_ctx *c, char *name, double *len, double *wid, int *zero, int *nInst) { /* FUN_1801c0d30 */
  rn_sheet *s;
  if (c->state != 3) return inq_err(c);
  s = &c->S[c->cur]; strcpy(name, s->name); *len = s->len; *wid = s->wid; *zero = 0; *nInst = c->rec.n + -4;
  return 1;
}
int rn_inq_sheet_total_nested(rn_ctx *c, char *name, int *copies) {             /* FUN_1801c0ef0 L196832 */
  if (c->state != 1) return inq_err(c);
  c->itSheet = c->itSheet + 1;
  while (c->itSheet < c->cur + 1) {
    if (0 < c->S[c->itSheet].used) { strcpy(name, c->S[c->itSheet].name); *copies = c->S[c->itSheet].used; return 1; }
    c->itSheet = c->itSheet + 1; *copies = 0;
  }
  c->itSheet = -1;
  return 0;
}
int rn_inq_nested_part(rn_ctx *c, int i, char *name, double *x, double *y, int *rot) { /* FUN_1801c10f0 L196898 */
  rn_rec *r;
  if (c->state != 3) return inq_err(c);
  if (i + 4 >= c->rec.n) rn_die(c, "RNInqNestedPart index beyond the list");
  r = &c->rec.v[i + 4]; strcpy(name, r->name);
  *x = (r->x + c->offR) * c->res; *y = (r->y + c->offR) * c->res; *rot = r->rot;
  return 1;
}
int rn_inq_nested_instance(rn_ctx *c, const char *name, int k, double *x, double *y, int *rot) { /* FUN_1801c1370 */
  int i, n = 0;
  *rot = 0; *y = 0.0; *x = 0.0;
  if (c->state != 3) return inq_err(c);
  for (i = 4; i < c->rec.n; i++) {
    rn_rec *r = &c->rec.v[i];
    if (strcmp(name, r->name) == 0) {
      if (k == n) { *x = (r->x + c->offR) * c->res; *y = (r->y + c->offR) * c->res; *rot = r->rot; return 1; }
      n = n + 1;
    }
  }
  return 0;
}
int rn_inq_nested_part_type(rn_ctx *c, int i, char *name, double *len, double *wid, int *n) { /* FUN_1801c1950 */
  rn_part *p;
  if (c->state != 3) return inq_err(c);
  p = &c->A[i]; strcpy(name, p->name);
  *len = p->L - c->off * 2.0; *wid = p->W - c->off * 2.0; *n = p->placed + p->fill;
  return 1;
}
int rn_inq_total_input_part_types(rn_ctx *c) { return c->dummy == 1 ? c->np + -1 : c->np; } /* FUN_1801c1af0 */
int rn_inq_nested_part_instances(rn_ctx *c, char *name, int *n) {               /* FUN_1801c1d60 L197247 */
  do {
    c->itPart = c->itPart + 1;
    if (c->np <= c->itPart) { c->itPart = -1; return 0; }
  } while (c->A[c->itPart].done < 1);
  strcpy(name, c->A[c->itPart].name);
  *n = c->A[c->itPart].done + c->A[c->itPart].fillTot;
  return 1;
}
void rn_inq_params(rn_ctx *c, double *cutter, double *clearance, double *resolution) { /* FUN_1801c1b20/1b50/1b80 */
  *cutter = c->cutter; *clearance = c->clr; *resolution = c->res;
}
