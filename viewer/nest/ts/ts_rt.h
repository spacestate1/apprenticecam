/* ts_rt.h - runtime under the transliterated TrueShape engine (viewer/nest/ts/ts_gen.c): the memory image of the
 * DLL's static data, the MSVC CRT functions it calls (by address), C++ throw / catch, streams. */
#ifndef TS_RT_H
#define TS_RT_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>

/* static data of n64dll.dll (.rdata 0x180261000 .. .data/.bss end 0x1804f5000) as one image, so that the decompile's
 * DAT_<addr> names (which alias: a struct base and its fields) address the same bytes as in the original. */
#define TS_IMG_LO 0x180261000ULL
#define TS_IMG_HI 0x180508000ULL
extern unsigned char ts_img[TS_IMG_HI - TS_IMG_LO];
#define TS_G(type, addr) (*(type *)(ts_img + ((uint64_t)(addr) - TS_IMG_LO)))
void ts_img_init(void);           /* ts_data.c (generated): initial .rdata / .data contents */

typedef struct TSFILE { FILE *f; int text, eof; char path[1024]; struct TSFILE *next; } TSFILE;
TSFILE *ts_fopen(const char *path, const char *mode);
int ts_fclose(TSFILE *f);
void ts_fclose_all(void);
void ts_fputs_text(TSFILE *f, const char *s, int n);
int ts_vsnprintf(char *out, size_t outsz, const char *fmt, va_list ap);
int ts_sprintf(char *out, const char *fmt, ...);
int ts_fprintf(TSFILE *f, const char *fmt, ...);

void ts_fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
/* ours: with ts_fatal_jb set (a caller inside a program, nest_ts_job.c), ts_fatal / NOT PORTED keep the message in
   ts_fatal_msg and longjmp there instead of exiting the process; the engine image must then be loaded again
   (ts_engine_reset, nest_ts.h) */
extern jmp_buf *ts_fatal_jb;
extern char ts_fatal_msg[512];
void ts_not_ported(uint64_t addr) __attribute__((noreturn));
void ts_bad(uint64_t func, const char *what) __attribute__((noreturn));   /* decompile not usable here */

/* C++ exceptions (gen/eh.py): every transliterated function with a try block pushes a ts_eh frame (its try blocks
 * from the DLL's FuncInfo, the DLL state of the call it is in: _eh->st) and setjmps; _CxxThrowException
 * (ts_cxx_throw) walks the frames from the innermost as msvcrt's FindHandler does - the first try block whose state
 * range holds the frame's state with a catch of one of the thrown object's classes - and longjmps there with the
 * object copied; the frame then runs that catch (handler = its index in the function).  None: ts_fatal. */
typedef struct ts_ehinfo { uint64_t func; int ntry; struct { int lo, hi, ncatch; const char *type[4]; } t[4]; } ts_ehinfo;
typedef struct ts_eh { jmp_buf jb; struct ts_eh *prev; const ts_ehinfo *info; int st, handler; unsigned char obj[0x400]; } ts_eh;
typedef struct ts_ehthrow { uint64_t ti; int size, n; const char *type[6]; } ts_ehthrow;
extern ts_eh *ts_eh_top;
extern const ts_ehthrow ts_eh_throws[];    /* ts_gen.c (generated) */
void ts_throw(void *obj, uint64_t throwinfo) __attribute__((noreturn));
/* debug: TS_TRACE=file logs every entry of a transliterated function (compare with the DLL's call sequence) */
extern FILE *ts_tracef;
void ts_trace_enter(uint64_t addr);
extern int ts_hook;          /* TS_TRACE or the TS_FATAL_AT test hook (ts_rt.c) */
#define TS_ENTER(a) do { if (ts_hook) ts_trace_enter(a); } while (0)
#endif
