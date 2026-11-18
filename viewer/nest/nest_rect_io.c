/* nest_rect_io.c - .nip reader (rectangular subset) and .nop writer, reproducing runNestCommon(path.nip) of
 * n64dll.dll for NEST_TYPE RECTANGULAR (FUN_180001a00 L404 -> NipReader FUN_180024a40 / FUN_180024b20 L19926, then
 * RNNestOrder + RNEndRectangleNest L451).  Citations as in nest_rect.c.
 *
 * Reader scope: the line grammar of tools/peloader/README.md ("(KEY value)" leaf, "(NAME" opens, ")" closes, lines
 * containing '#' skipped - FUN_180203630 L223167 / FUN_180223850), keys matched exactly (the original matches with
 * strstr in a fixed order, FUN_180206130 L224448 / part dispatcher FUN_180209520 L226314, which agrees for the exact keys below).
 * Keys: SETTINGS/NEST_TYPE, SETTINGS/FILES/OUTPUT/FORMAT; PARAMETERS RESOLUTION, DEFAULT_CUTTER_DIAMETER, CLEARANCE;
 * PART NAME MINIMUM MAXIMUM PRIORITY SHAPE LENGTH WIDTH IF_ROTATE_PART GRAIN; SHEET NAME QUANTITY SHAPE LENGTH WIDTH
 * COLLAR GRAIN.  Numbers: atof / atoi (FUN_18022bf4c / FUN_18022bc7c; MSVC's $I10_INPUT is taken as strtod - equal
 * for the short decimals used by the tests).  SHAPE X parts / sheets (geometry files) are not supported. */
#include "nest_rect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { char name[1024], shape[64], rot[64], grain[64]; int min, max, prio; double len, wid; } nip_part;
typedef struct { char name[1024], shape[64], grain[64]; int qty; double len, wid, collar; } nip_sheet;

typedef struct { FILE *f; int first; int number; double sheetOffset; } nop_w;

static void lower(char *s) { for (; *s; s++) *s = (char)tolower((unsigned char)*s); }   /* FUN_18002b450 L23809 (via FUN_18002b350 L23777) */
static void upper(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }   /* FUN_180026bb0 L21231 */
static int yesno(char *s) {                                                     /* FUN_180026c10 L21249 */
  size_t n = strlen(s); upper(s);
  if (strncmp(s, "YES", n) == 0) return 1;
  if (strncmp(s, "NO", n) == 0) return 0;
  return -1;
}
static void slash(char *s) {                                                    /* FUN_18002b350: '\\' -> '/', lower */
  size_t n = strlen(s), i;
  if (s[0] == '\\') s[0] = '/';
  for (i = s[1] == '\\' ? 1 : 2; i < n; i++) if (s[i] == '\\') s[i] = '/';
  lower(s);
}

/* fprintf with the DLL CRT's %lf (msvc_fmt_f) - lines are written with CRLF as the text-mode stream does */
static void nl(FILE *f, const char *s) { for (; *s; s++) { if (*s == '\n') fputc('\r', f); fputc(*s, f); } }
static void wf(FILE *f, const char *pre, double v, const char *post) {
  char b[512]; msvc_fmt_f(b, sizeof b, v, 0, 6); nl(f, pre); nl(f, b); nl(f, post);
}

/* sheet result callback: FUN_180005250 L1801 -> FUN_1800052c0 L1823 (FORMAT UNIFIED) */
static int nop_sheet(rn_ctx *c, void *user, int copies, double util) {
  nop_w *w = user; char sname[1024], pname[1024], b[1200]; double len, wid, pl, pw, cut, clr, res, x, y;
  int zero, ninst, ntypes, k, i, n, nnot = 0, rot;
  if (!w->f) return 0;
  if (w->first == 0) {                                                          /* DAT_1802b2090 */
    w->first = 1;
    rn_inq_params(c, &cut, &clr, &res);
    nl(w->f, "(PARAMETERS\n");
    nl(w->f, "   (NEST_TYPE\t\t\t\t\tRECTANGULAR)\n");
    wf(w->f, "\t(RESOLUTION\t\t\t\t\t", res, ")\n");
    wf(w->f, "\t(CLEARANCE\t\t\t\t\t", clr, ")\n");
    wf(w->f, "\t(DEFAULT_CUTTER_DIAMETER\t\t", cut, ")\n");
    nl(w->f, ")\n");
    nl(w->f, "\n(LAYOUTLIST\n");
  }
  rn_inq_nested_sheet(c, sname, &len, &wid, &zero, &ninst);
  ntypes = rn_inq_total_input_part_types(c);
  for (i = 1; i <= ntypes; i++) { rn_inq_nested_part_type(c, i - 1, pname, &pl, &pw, &n); if (n == 0) nnot++; }
  nl(w->f, "\n\t(LAYOUT\n");
  snprintf(b, sizeof b, "\t\t(NUMBER\t\t\t\t\t%d)\n", w->number); nl(w->f, b); w->number++;   /* DAT_180293008 = 1 */
  snprintf(b, sizeof b, "\t\t(COPIES\t\t\t\t\t%d)\n", copies); nl(w->f, b);
  snprintf(b, sizeof b, "\t\t(PART_TYPES_NESTED\t\t%d)\n", ntypes - nnot); nl(w->f, b);
  wf(w->f, "\t\t(SHEET_AREA\t\t\t\t", len * wid, ")\n");
  wf(w->f, "\t\t(SHEET_OFFSET\t\t\t", w->sheetOffset, ")\n");
  nl(w->f, "\t\t(SHEET\n");
  snprintf(b, sizeof b, "\t\t\t(NAME\t\t\t\t%s)\n", sname); nl(w->f, b);
  nl(w->f, "\t\t\t(SHAPE\t\t\t\tR)\n");
  wf(w->f, "\t\t\t(LENGTH\t\t\t\t", len, ")\n");
  wf(w->f, "\t\t\t(WIDTH\t\t\t\t", wid, ")\n");
  { char u[64]; msvc_fmt_f(u, sizeof u, util, 5, 2); snprintf(b, sizeof b, "\t\t\t(UTILIZATION\t\t%s %%)\n", u); nl(w->f, b); }
  nl(w->f, "\t\t)\n");
  nl(w->f, "\t\t(SHEAR_NESTED_PARTS\n");
  for (i = 1; i <= ntypes; i++) {
    rn_inq_nested_part_type(c, i - 1, pname, &pl, &pw, &n);
    if (n == 0) continue;
    nl(w->f, "\t\t\t(PART\n");
    snprintf(b, sizeof b, "\t\t\t\t(NAME\t\t\t%s)\n", pname); nl(w->f, b);
    wf(w->f, "\t\t\t\t(LENGTH\t\t\t", pl, ")\n");
    wf(w->f, "\t\t\t\t(WIDTH\t\t\t", pw, ")\n");
    snprintf(b, sizeof b, "\t\t\t\t(INSTANCES\t\t%d)\n", n); nl(w->f, b);
    for (k = 1; k <= n; k++) {
      rn_inq_nested_instance(c, pname, k - 1, &x, &y, &rot);
      nl(w->f, "\t\t\t\t(INSTANCE\n");
      wf(w->f, "\t\t\t\t\t(XLL\t\t\t", x, ")\n");
      wf(w->f, "\t\t\t\t\t(YLL\t\t\t", y, ")\n");
      nl(w->f, rot == 0 ? "\t\t\t\t\t(IF_ROTATED\t\tNO)\n" : "\t\t\t\t\t(IF_ROTATED\t\tYES)\n");
      nl(w->f, "\t\t\t\t)\n");
    }
    nl(w->f, "\t\t\t)\n");
  }
  nl(w->f, "\t\t)\n");
  nl(w->f, "\t)\n");
  fflush(w->f);
  return 1;
}

/* order result callback: FUN_180005970 L1947 -> FUN_180005a00 L1965 */
static int nop_order(rn_ctx *c, void *user, int sheetTypes, int sheets) {
  nop_w *w = user; char name[1024], b[1200]; int n;
  (void)sheetTypes; (void)sheets;
  if (!w->f) return 0;
  nl(w->f, ")\n\n");
  nl(w->f, "(SUMMARY\n");
  nl(w->f, "\t(SHEETLIST\n");
  while (rn_inq_sheet_total_nested(c, name, &n) == 1) {
    nl(w->f, "\t\t(SHEET\n");
    snprintf(b, sizeof b, "\t\t\t(NAME\t\t%s)\n", name); nl(w->f, b);
    snprintf(b, sizeof b, "\t\t\t(INSTANCES\t\t%d)\n", n); nl(w->f, b);
    nl(w->f, "\t\t)\n");
  }
  nl(w->f, "\t)\n");
  nl(w->f, "\t(PARTLIST\n");
  while (rn_inq_nested_part_instances(c, name, &n) == 1) {
    nl(w->f, "\t\t(PART\n");
    snprintf(b, sizeof b, "\t\t\t(NAME\t\t\t%s)\n", name); nl(w->f, b);
    snprintf(b, sizeof b, "\t\t\t(INSTANCES\t\t%d)\n", n); nl(w->f, b);
    nl(w->f, "\t\t)\n");
  }
  nl(w->f, "\t)\n");
  nl(w->f, ")\n");
  w->first = 0;
  return 0;                                                                     /* FUN_180005a00 returns 0 */
}

/* error callback = the reader's FUN_180009d50 L4016 (runNestCommon without a client handler): severity 1 / 2 =
 * warning, continue; otherwise "( FATAL ERROR ) msg" to nestlib.err, NCloseNestLib, exit(-1) (FUN_18000a840) - the
 * .nop stays as written so far (the CRT flushes it at exit). */
static FILE *g_nop;
static int nip_error(void *user, int code, int sev, const char *msg) {
  FILE *e; (void)user; (void)code;
  e = fopen("nestlib.err", "ab");
  if (e) { fprintf(e, sev == 1 || sev == 2 ? "( WARNING ) %s\r\n" : "( FATAL ERROR ) %s\r\n", msg); fclose(e); }
  if (sev == 2 || sev == 1) return 1;
  if (g_nop) fflush(g_nop);
  exit(255);
}

static char *trim(char *s) { char *e; while (*s == ' ' || *s == '\t') s++; e = s + strlen(s); while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0; return s; }

int rn_run_nip(const char *nipPath, const char *nopPath, int fillers, char *err, int errsz) {
  FILE *f = fopen(nipPath, "rb"); char line[4096], stack[16][64], fmt[64] = "", ntype[64] = "";
  int depth = 0, np = 0, ns = 0, capP = 64, capS = 16, i;
  nip_part *P = calloc(capP, sizeof *P), cp; nip_sheet *S = calloc(capS, sizeof *S), cs;
  double res = -9999.0, cut = -9999.0, clr = -9999.0; rn_ctx *c; rn_callbacks cb; nop_w w;
  if (!f) { snprintf(err, errsz, "cannot open %s", nipPath); return 0; }
  memset(&cp, 0, sizeof cp); memset(&cs, 0, sizeof cs);
  while (fgets(line, sizeof line, f)) {
    char *s = trim(line), *key, *val, *e;
    if (strchr(s, '#') || !*s) continue;
    if (*s == ')') {
      if (depth > 0) {
        depth--;
        if (!strcmp(stack[depth], "PART") && depth >= 1 && !strcmp(stack[depth - 1], "PARTLIST")) {
          if (np == capP) P = realloc(P, sizeof *P * (capP *= 2));
          P[np++] = cp;
        }
        if (!strcmp(stack[depth], "SHEET") && depth >= 1 && !strcmp(stack[depth - 1], "SHEETLIST")) {
          if (ns == capS) S = realloc(S, sizeof *S * (capS *= 2));
          S[ns++] = cs;
        }
      }
      continue;
    }
    if (*s != '(') continue;
    key = s + 1; for (e = key; *e && *e != ' ' && *e != '\t' && *e != ')'; e++) ;
    val = e; if (*val) { *val = 0; val++; } else val = e;
    val = trim(val);
    if (strlen(val) && val[strlen(val) - 1] == ')') {                          /* leaf "(KEY value)" */
      val[strlen(val) - 1] = 0; val = trim(val);
      const char *sec = depth ? stack[depth - 1] : "";
      if (!strcmp(key, "NEST_TYPE")) snprintf(ntype, sizeof ntype, "%s", val);
      else if (!strcmp(key, "FORMAT")) snprintf(fmt, sizeof fmt, "%s", val);
      else if (!strcmp(sec, "PARAMETERS")) {
        if (!strcmp(key, "RESOLUTION")) res = atof(val);
        else if (!strcmp(key, "DEFAULT_CUTTER_DIAMETER")) cut = atof(val);
        else if (!strcmp(key, "CLEARANCE")) clr = atof(val);
      } else if (!strcmp(sec, "PART")) {
        if (!strcmp(key, "NAME")) snprintf(cp.name, sizeof cp.name, "%s", val);
        else if (!strcmp(key, "MINIMUM")) cp.min = atoi(val);
        else if (!strcmp(key, "MAXIMUM")) cp.max = atoi(val);
        else if (!strcmp(key, "PRIORITY")) cp.prio = atoi(val);
        else if (!strcmp(key, "SHAPE")) snprintf(cp.shape, sizeof cp.shape, "%s", val);
        else if (!strcmp(key, "LENGTH")) cp.len = atof(val);
        else if (!strcmp(key, "WIDTH")) cp.wid = atof(val);
        else if (!strcmp(key, "IF_ROTATE_PART")) snprintf(cp.rot, sizeof cp.rot, "%s", val);
        else if (!strcmp(key, "GRAIN")) snprintf(cp.grain, sizeof cp.grain, "%s", val);
      } else if (!strcmp(sec, "SHEET")) {
        if (!strcmp(key, "NAME")) snprintf(cs.name, sizeof cs.name, "%s", val);
        else if (!strcmp(key, "QUANTITY")) cs.qty = atoi(val);
        else if (!strcmp(key, "SHAPE")) snprintf(cs.shape, sizeof cs.shape, "%s", val);
        else if (!strcmp(key, "LENGTH")) cs.len = atof(val);
        else if (!strcmp(key, "WIDTH")) cs.wid = atof(val);
        else if (!strcmp(key, "COLLAR")) cs.collar = atof(val);
        else if (!strcmp(key, "GRAIN")) snprintf(cs.grain, sizeof cs.grain, "%s", val);
      }
    } else {                                                                    /* section open */
      if (depth < 16) snprintf(stack[depth], 64, "%s", key);
      depth++;
      if (!strcmp(key, "PART")) {                                               /* part section defaults, ctor FUN_180208fb0 L226183 */
        memset(&cp, 0, sizeof cp); cp.min = -9999; cp.max = -9999; cp.prio = -9999; cp.len = -9999.0; cp.wid = -9999.0;
        strcpy(cp.rot, "$"); strcpy(cp.grain, "$"); strcpy(cp.shape, "$");
      }
      if (!strcmp(key, "SHEET")) {                                              /* sheet section defaults FUN_180205b70 L224302 */
        memset(&cs, 0, sizeof cs); cs.qty = -9999; cs.len = -9999.0; cs.wid = -9999.0; cs.collar = 0.0;
        strcpy(cs.grain, "$"); strcpy(cs.shape, "$");
      }
    }
  }
  fclose(f);
  upper(ntype);
  if (strcmp(ntype, "RECTANGULAR")) { snprintf(err, errsz, "NEST_TYPE is not RECTANGULAR"); free(P); free(S); return 0; }

  memset(&w, 0, sizeof w); w.number = 1;
  w.f = strcmp(fmt, "UNIFIED") == 0 ? fopen(nopPath, "wb") : NULL;            /* FUN_180005250: only for UNIFIED */
  g_nop = w.f;
  memset(&cb, 0, sizeof cb); cb.user = &w; cb.sheet_result = nop_sheet; cb.order_result = nop_order; cb.error = nip_error;
  /* PARAMETERS L20335-20729: resolution < 0 -> 1.0; cutter / clearance only when > 0 (default 0, FUN_1800e6f20) */
  if (res < 0.0) res = 1.0;
  c = rn_new(0.0 < cut ? cut : 0.0, 0.0 < clr ? clr : 0.0, res, &cb);
  if (fillers) rn_set_fillers_upto_nested_height(c, 1);
  /* PARTLIST L22102-22678 */
  for (i = 0; i < np; i++) {
    nip_part *p = &P[i]; char g = 'N'; int rot, mn, mx; double L = p->len, W = p->wid;
    slash(p->name);
    if (toupper((unsigned char)p->shape[0]) != 'R') { snprintf(err, errsz, "part %s: only SHAPE R supported", p->name); rn_free(c); free(P); free(S); if (w.f) fclose(w.f); return 0; }
    if (L < 0.0) L = 0.0;
    if (W < 0.0) W = 0.0;
    mn = p->min < 0 ? 1 : p->min; mx = p->max < 0 ? 1 : p->max;
    if (strncmp(p->grain, "NONE", 4) == 0) g = 'N'; else if (p->grain[0] == 'X') g = 'X'; else if (p->grain[0] == 'Y') g = 'Y';
    rot = yesno(p->rot); if (rot == -1) rot = 1;
    rn_add_part(c, p->name, mn, mx, L, W, rot, g);                              /* RNAddPartInfo2 L22357 */
    rn_set_part_priority(c, p->prio);                                           /* raw value, default -9999 (L22360) */
  }
  /* SHEETLIST L23158-23468 */
  for (i = 0; i < ns; i++) {
    nip_sheet *s = &S[i]; char g = 'N'; int q = s->qty < 0 ? 0 : s->qty; double L = s->len < 0.0 ? 0.0 : s->len, W = s->wid < 0.0 ? 0.0 : s->wid;
    char sh = (char)toupper((unsigned char)s->shape[0]);
    if (sh == 'X' || sh == '$') { snprintf(err, errsz, "sheet %s: SHAPE X not supported", s->name); rn_free(c); free(P); free(S); if (w.f) fclose(w.f); return 0; }
    if (toupper((unsigned char)s->grain[0]) == 'X') g = 'X'; else if (toupper((unsigned char)s->grain[0]) == 'Y') g = 'Y';
    slash(s->name);
    w.sheetOffset = s->collar;                                                  /* DAT_1802b2008 = last sheet's collar */
    rn_add_sheet(c, s->name, q, L, W, s->collar, g);
  }
  rn_nest_order(c);
  rn_free(c);
  if (w.f) fclose(w.f);
  free(P); free(S);
  return 1;
}
