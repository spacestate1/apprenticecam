/* ts_printf.c - the DLL CRT's printf family as used by the TrueShape port: integer / string conversions as MSVC 2010
 * (_output_l FUN_180234068 L258827), floating "%f" through msvc_fmt_f (nest_rect.c: _cftof_l L257931 -> _fltout2
 * 17 digits -> _fptostr L266895).  %e / %g are not used on the ported paths and abort. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../nest_rect.h"
#include "ts_rt.h"

int ts_vsnprintf(char *out, size_t outsz, const char *fmt, va_list ap) {
  size_t n = 0; char tmp[512];
#define PUTC(ch) do { if (n + 1 < outsz) out[n] = (ch); n++; } while (0)
  for (const char *p = fmt; *p; p++) {
    if (*p != '%') { PUTC(*p); continue; }
    const char *start = p++; char flags[8]; int nf = 0, width = -1, prec = -1, lng = 0;
    while (*p && strchr("-+ #0", *p)) { if (nf < 7) flags[nf++] = *p; p++; }
    flags[nf] = 0;
    if (*p == '*') { width = va_arg(ap, int); p++; } else if (*p >= '0' && *p <= '9') { width = 0; while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0'); }
    if (*p == '.') { p++; prec = 0; if (*p == '*') { prec = va_arg(ap, int); p++; } else while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0'); }
    if (*p == 'l') { p++; if (*p == 'l') { lng = 2; p++; } else lng = 1; }
    else if (*p == 'h') { p++; lng = -1; }
    else if (p[0] == 'I' && p[1] == '6' && p[2] == '4') { p += 3; lng = 2; }
    char conv = *p, spec[64];
    switch (conv) {
    case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'c': {
      snprintf(spec, sizeof spec, "%%%s", flags);
      if (width >= 0) snprintf(spec + strlen(spec), sizeof spec - strlen(spec), "%d", width);
      if (prec >= 0) snprintf(spec + strlen(spec), sizeof spec - strlen(spec), ".%d", prec);
      if (lng == 2) { strcat(spec, "ll"); size_t l = strlen(spec); spec[l] = conv; spec[l + 1] = 0; snprintf(tmp, sizeof tmp, spec, va_arg(ap, long long)); }
      else { size_t l = strlen(spec); spec[l] = conv; spec[l + 1] = 0; snprintf(tmp, sizeof tmp, spec, va_arg(ap, int)); } /* "l" = 32-bit on Windows */
      break; }
    case 's': {
      const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
      snprintf(spec, sizeof spec, "%%%s", flags);
      if (width >= 0) snprintf(spec + strlen(spec), sizeof spec - strlen(spec), "%d", width);
      if (prec >= 0) snprintf(spec + strlen(spec), sizeof spec - strlen(spec), ".%d", prec);
      strcat(spec, "s");
      { size_t need = strlen(s) + (width > 0 ? (size_t)width : 0) + 1; char *b = malloc(need + 1); snprintf(b, need + 1, spec, s);
        for (char *q = b; *q; q++) PUTC(*q);
        free(b); }
      continue; }
    case 'f': {
      double v = va_arg(ap, double);
      if (nf && strcmp(flags, "-") != 0) ts_fatal("printf flags %s with %%f not ported", flags);
      msvc_fmt_f(tmp, sizeof tmp, v, 0, prec < 0 ? 6 : prec);
      if (width > 0 && (int)strlen(tmp) < width) {
        char b[600]; if (nf) snprintf(b, sizeof b, "%-*s", width, tmp); else snprintf(b, sizeof b, "%*s", width, tmp); strcpy(tmp, b);
      }
      break; }
    case '%': tmp[0] = '%'; tmp[1] = 0; break;
    default: ts_fatal("printf conversion %%%c (format \"%s\") not ported", conv, start);
    }
    for (char *q = tmp; *q; q++) PUTC(*q);
  }
  if (outsz) out[n < outsz ? n : outsz - 1] = 0;
  return (int)n;
#undef PUTC
}

int ts_sprintf(char *out, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = ts_vsnprintf(out, (size_t)1 << 30, fmt, ap); va_end(ap); return r; }

/* text-mode stream: '\n' -> "\r\n" as the DLL CRT's _write in text mode */
int ts_fprintf(TSFILE *f, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); char *b = malloc(1 << 16); int r = ts_vsnprintf(b, 1 << 16, fmt, ap); va_end(ap);
  ts_fputs_text(f, b, r); free(b); return r;
}
