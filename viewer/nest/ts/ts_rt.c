/* ts_rt.c - runtime of the transliterated TrueShape engine: memory image, streams (MSVC text mode), throw. */
#define _GNU_SOURCE
#include "ts_rt.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

unsigned char ts_img[TS_IMG_HI - TS_IMG_LO];
ts_eh *ts_eh_top;
FILE *ts_tracef;
static long ts_ntrace;
int ts_hook;
static uint64_t ts_fatal_at; static long ts_fatal_n, ts_fatal_seen;
void ts_trace_enter(uint64_t addr) {
  if (ts_tracef && ts_ntrace++ < 20000000) fprintf(ts_tracef, "%llx\n", (unsigned long long)addr);
  if (addr == ts_fatal_at && ++ts_fatal_seen == ts_fatal_n) ts_fatal("TS_FATAL_AT %llx (test)", (unsigned long long)addr);
}

jmp_buf *ts_fatal_jb;
char ts_fatal_msg[512];
void ts_fatal(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); vsnprintf(ts_fatal_msg, sizeof ts_fatal_msg, fmt, ap); va_end(ap);
  fprintf(stderr, "nest_ts: %s\n", ts_fatal_msg);
  if (ts_fatal_jb) { jmp_buf *j = ts_fatal_jb; ts_fatal_jb = NULL; ts_eh_top = NULL; longjmp(*j, 1); }
  exit(5);
}
void ts_bad(uint64_t func, const char *what) { ts_fatal("NOT PORTED: FUN_%llx: %s", (unsigned long long)func, what); }
void ts_not_ported(uint64_t addr) { ts_fatal("NOT PORTED: FUN_%llx called", (unsigned long long)addr); }

void ts_throw(void *obj, uint64_t ti) {
  const ts_ehthrow *w = ts_eh_throws;
  while (w->ti && w->ti != ti) w++;
  if (!w->ti) ts_fatal("throw with an unknown ThrowInfo %llx", (unsigned long long)ti);
  for (ts_eh *f = ts_eh_top; f; f = f->prev) {
    int h = 0;
    for (int t = 0; t < f->info->ntry; t++) {
      int in = f->st >= f->info->t[t].lo && f->st <= f->info->t[t].hi;
      for (int c = 0; c < f->info->t[t].ncatch; c++, h++) {
        if (!in) continue;
        const char *ct = f->info->t[t].type[c];
        int match = !strcmp(ct, "...");
        for (int k = 0; k < w->n && !match; k++) match = !strcmp(ct, w->type[k]);
        if (!match) continue;
        memcpy(f->obj, obj, (size_t)w->size < sizeof f->obj ? (size_t)w->size : sizeof f->obj);
        f->handler = h; ts_eh_top = f->prev;
        longjmp(f->jb, 1);
      }
    }
  }
  /* no handler (MSVC: terminate): the thrown nestError's own message (FUN_1800537a0: code, severity, kind, text +0xc) */
  if (!strncmp(w->type[0], "nestError", 9)) ts_fatal("%.400s", (const char *)obj + 12);
  ts_fatal("uncaught exception %s", w->type[0]);
}

/* Windows path -> host path as tools/peloader/win32.c does: drop "X:", '\' -> '/', and resolve a missing path
 * component by component case-insensitively. */
static void host_path(const char *in, char *out, size_t outsz) {
  char p[1024]; size_t k = 0;
  if (isalpha((unsigned char)in[0]) && in[1] == ':') in += 2;
  for (; *in && k + 1 < sizeof p; in++) p[k++] = *in == '\\' ? '/' : *in;
  p[k] = 0;
  struct stat st;
  if (stat(p, &st) == 0) { snprintf(out, outsz, "%s", p); return; }
  char res[1024] = ""; const char *s = p;
  if (*s == '/') { strcpy(res, "/"); s++; }
  while (*s) {
    const char *e = strchr(s, '/'); size_t n = e ? (size_t)(e - s) : strlen(s);
    char comp[256]; snprintf(comp, sizeof comp, "%.*s", (int)n, s);
    char dir[1024]; snprintf(dir, sizeof dir, "%s", res[0] ? res : ".");
    char found[256]; snprintf(found, sizeof found, "%s", comp);
    DIR *d = opendir(dir);
    if (d) { struct dirent *de; while ((de = readdir(d))) if (!strcasecmp(de->d_name, comp)) { snprintf(found, sizeof found, "%s", de->d_name); break; } closedir(d); }
    size_t L = strlen(res);
    snprintf(res + L, sizeof res - L, "%s%s", (L && res[L - 1] != '/') ? "/" : "", found);
    s += n; if (*s == '/') s++;
  }
  snprintf(out, outsz, "%s", res);
}

/* MSVC text mode through a glibc cookie stream: reading turns CR LF into LF and stops at ^Z, writing turns LF into
 * CR LF; every CRT call on the stream (fgetc, fgets, fread, fscanf, fprintf, fwrite) then goes through the same
 * buffer, as in the DLL's CRT.  Binary mode uses the file directly. */
typedef struct { FILE *raw; int eof; } ts_cookie;
static ssize_t ck_read(void *c_, char *buf, size_t n) {
  ts_cookie *c = c_; size_t k = 0;
  while (k < n && !c->eof) {
    int ch = fgetc(c->raw);
    if (ch == EOF) break;
    if (ch == 0x1a) { c->eof = 1; break; }
    if (ch == '\r') { int d = fgetc(c->raw); if (d == '\n') ch = '\n'; else if (d != EOF) ungetc(d, c->raw); }
    buf[k++] = (char)ch;
  }
  return (ssize_t)k;
}
static ssize_t ck_write(void *c_, const char *buf, size_t n) {
  ts_cookie *c = c_;
  for (size_t i = 0; i < n; i++) { if (buf[i] == '\n') fputc('\r', c->raw); fputc(buf[i], c->raw); }
  return (ssize_t)n;
}
static int ck_close(void *c_) { ts_cookie *c = c_; int r = fclose(c->raw); free(c); return r; }

/* ours: the open streams, and the closed ones (kept until ts_fclose_all): the DLL closes a stream twice when the
   progress callback stops a run at some points (seed-17 x003, Esc at the 40th call); MSVC's fclose of a closed stream
   returns EOF (its FILE is a slot of the CRT's table), here the second close finds it in no open list and does the same */
static TSFILE *ts_open_files, *ts_closed_files;
TSFILE *ts_fopen(const char *path, const char *mode) {
  char hp[1024], m[8]; int text = strchr(mode, 'b') == NULL; size_t k = 0;
  host_path(path, hp, sizeof hp);
  for (const char *c = mode; *c && k < 6; c++) if (*c != 't' && *c != 'b') m[k++] = *c;
  m[k] = 0;
  FILE *raw = fopen(hp, m);
  if (!raw) return NULL;
  TSFILE *t = calloc(1, sizeof *t); t->text = text; snprintf(t->path, sizeof t->path, "%s", hp);
  t->next = ts_open_files; ts_open_files = t;
  if (text) {
    ts_cookie *c = calloc(1, sizeof *c); c->raw = raw;
    cookie_io_functions_t io = { ck_read, ck_write, NULL, ck_close };
    t->f = fopencookie(c, m, io);
  } else t->f = raw;
  return t;
}
int ts_fclose(TSFILE *f) {
  TSFILE **pp = &ts_open_files;
  while (*pp && *pp != f) pp = &(*pp)->next;
  if (!f || !*pp) return -1;                                         /* not open: EOF */
  *pp = f->next;
  int r = fclose(f->f); f->f = NULL;
  f->next = ts_closed_files; ts_closed_files = f;
  return r;
}
/* ours: close the streams an abandoned engine state left open, free the closed ones (ts_crt_release) */
void ts_fclose_all(void) {
  while (ts_open_files) ts_fclose(ts_open_files);
  while (ts_closed_files) { TSFILE *n = ts_closed_files->next; free(ts_closed_files); ts_closed_files = n; }
}
void ts_fputs_text(TSFILE *f, const char *s, int n) { fwrite(s, 1, (size_t)n, f->f); }
int ts_fgetc(TSFILE *f) { return fgetc(f->f); }
char *ts_fgets(char *buf, int n, TSFILE *f) { return fgets(buf, n, f->f); }
size_t ts_fread(void *p, size_t sz, size_t n, TSFILE *f) { return fread(p, sz, n, f->f); }
size_t ts_fwrite(const void *p, size_t sz, size_t n, TSFILE *f) { return fwrite(p, sz, n, f->f); }
int ts_fscanf(TSFILE *f, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfscanf(f->f, fmt, ap); va_end(ap); return r; }

static void ts_trace_close(void) { if (ts_tracef) fclose(ts_tracef); ts_tracef = NULL; }
void ts_crt_init(void) {
  const char *t = getenv("TS_TRACE");
  if (t && !ts_tracef) { ts_tracef = fopen(t, "w"); setvbuf(ts_tracef, NULL, _IOLBF, 0); atexit(ts_trace_close); }
  /* test hook TS_FATAL_AT=<hex function>[:<n>]: ts_fatal at the n-th (default 1st) entry of that function in this
     process, as a NOT PORTED function stops a run (the recovery path of nest_ts_job.c) */
  const char *f = getenv("TS_FATAL_AT");
  if (f && !ts_fatal_at) { ts_fatal_at = strtoull(f, NULL, 16); const char *c = strchr(f, ':'); ts_fatal_n = c ? atol(c + 1) : 1; }
  ts_hook = ts_tracef || ts_fatal_at;
}
