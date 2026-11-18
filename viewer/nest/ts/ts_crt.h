/* ts_crt.h - the MSVC 2010 CRT functions of n64dll.dll called by the transliterated engine, by address
 * (FID names from Ghidra, or identified from the decompile: line numbers = decomp/nest/n64dll_all.c). */
#ifndef TS_CRT_H
#define TS_CRT_H
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "ts_rt.h"

double ts_sin(double), ts_cos(double), ts_pow(double, double), ts_atan2(double, double), ts_acos(double);
char *ts_itoa(int v, char *buf, int radix);
void *ts_new(size_t n);
int ts_fgetc(TSFILE *f);
char *ts_fgets(char *buf, int n, TSFILE *f);
size_t ts_fread(void *p, size_t sz, size_t n, TSFILE *f);
size_t ts_fwrite(const void *p, size_t sz, size_t n, TSFILE *f);
long ts_ftell(TSFILE *f);
int ts_feof(TSFILE *f);
int ts_vsprintf(char *out, const char *fmt, va_list ap);
double ts_atof(const char *s);
void ts_cxx_throw(void *obj, void *throwinfo) __attribute__((noreturn));
void ts_msvc_qsort(void *base, uint64_t n, uint64_t w, void *cmp);

#define FUN_180224ab0 strcmp            /* L244455 */
#define FUN_180224d54 toupper           /* L244627 */
#define FUN_180224d90 strlen            /* L244650 */
#define FUN_180224e70 strrchr           /* L244725 */
#define FUN_180224f14 ts_fclose         /* fclose L244797 */
#define FUN_1802251e0 strncpy           /* L244978 */
#define FUN_1802253c0 strcat            /* L245186 (unnamed: strcat) */
#define FUN_180225460 strcpy            /* L245258 (unnamed: strcpy) */
#define FUN_1802259c0 ts_sprintf        /* sprintf L245555 */
#define FUN_180225ad8 ts_itoa           /* _itoa L245673 */
#define FUN_180225b04 ts_fprintf        /* fprintf L245689 */
#define FUN_180225c3c ts_new            /* operator new L245754: malloc + new handler */
#define FUN_180225ce0 floor             /* L245787 ("floor") */
#define FUN_18022ac4c ceilf             /* L250536 (ceilf, "ceilf" error name) */
#define FUN_180225de4 ceil              /* L245824 ("ceil") */
#define FUN_180225ef8 fabs              /* L245861 (sign bit cleared) */
#define FUN_180226700 ts_feof           /* L246404 (flag 0x10 = _IOEOF) */
#define FUN_1802269c0 ts_fopen          /* L246572 _fsopen(path, mode, 0x40) */
#define FUN_180226ae0 strcasecmp        /* _stricmp L246665 */
#define FUN_180226b60 strncmp           /* L246712 */
#define FUN_180227594 sqrt              /* L247480 ("sqrt") */
#define FUN_180227744 getenv            /* L247548 */
#define FUN_180227900 tolower           /* L247649 */
#define FUN_180227ba4 strstr            /* L247842 */
#define FUN_180228fbc ts_fwrite         /* fwrite L248912 */
#define FUN_18022904c ts_clock          /* clock L248940: only timing statistics (nesting_engine.md section 6) */
#define FUN_180229208 strncasecmp       /* _strnicmp L249067 */
#define FUN_180229260 fmod              /* L249095 ("fmod") */
#define FUN_1802293e4 ts_vsprintf       /* L249176 _vsprintf_l */
#define FUN_180229bfc ts_fread          /* fread L249647 */
#define FUN_180229ce0 ts_msvc_qsort     /* qsort L249703 */
#define FUN_18022a1c8 ts_malloc         /* malloc L250055 */
#define FUN_18022a280 ts_free           /* free L250104 */
#define FUN_18022a2c0 abs               /* L250129 */
#define FUN_18022abf4 ts_ftell          /* ftell L250508 */
#define FUN_18022ae88 ts_atan2          /* L250666 */
#define FUN_18022b93c ts_acos           /* acos (FUN_1801d9d80 L205401 returns it in degrees) */
#define FUN_18022bc7c atoi              /* L251072 strtol(s, 0, 10) */
#define FUN_18022bf4c ts_atof           /* L251292 _atof_l */
#define FUN_18022c018 ts_fgetc          /* fgetc L251358 */
#define FUN_180225024 sscanf            /* FID_conflict:sscanf L244859 */
#define FUN_1802268b0 ts_fscanf         /* FID_conflict:fscanf L246505 */
#define FUN_18022505c ts_fgets          /* fgets L244895 */
#define FUN_18022bd90 memset            /* L251164 (unnamed: memset) */
#define FUN_180227070 memcpy            /* L247110 */
#define FUN_180226b39 ts_strchr         /* L246693 (unnamed: strchr) */
#define FUN_180227048 ts_access         /* FID_conflict:_waccess L247092 = _access */
#define FUN_180228dfc ts_mkdir          /* FID_conflict:_wmkdir L248781 = _mkdir */
#define FUN_180227434 ts_rmdir          /* L247338 RemoveDirectoryA (_rmdir) */
#define FUN_18022755c ts_remove         /* L247444 DeleteFileA (remove) */
#define FUN_18022758c ts_remove         /* jmp FUN_18022755c */
#define FUN_1802294a8 ts_findfirst      /* _findfirst L249243 (FindFirstFileExA) */
#define FUN_1802295f0 ts_findnext       /* _findnext L249314 (FindNextFileA) */
#define FUN_1802293f0 ts_findclose      /* _findclose L249186 (FindClose) */
#define FUN_18022bc50 msvc_rand         /* rand L251051 */
#define FUN_18022bc38 msvc_srand        /* L251038 */
#define FUN_18022ae0c ts_fseek          /* fseek */
#define FUN_180226728 ts_ehvec_ctor     /* `eh vector constructor iterator' L246425 */
#define FUN_1802266a0 ts_ehvec_dtor     /* `eh vector destructor iterator' L246374 */
#define FUN_180502000 ts_sin            /* L291385 */
#define FUN_180502490 ts_cos            /* L291484 */
#define FUN_180502920 ts_pow            /* L291579 */
int32_t ts_clock(void);
void ts_ehvec_ctor(void *p, uint64_t size, int count, void *ctor, void *dtor);
void ts_ehvec_dtor(void *p, uint64_t size, int count, void *dtor);
int ts_fscanf(TSFILE *f, const char *fmt, ...);
char *ts_strchr(const char *s, int c);
int ts_access(const char *path, int mode);
int ts_mkdir(const char *path);
int ts_rmdir(const char *path);
int ts_remove(const char *path);
int64_t ts_findfirst(const char *spec, int *fd);
int ts_findnext(int64_t h, int *fd);
int ts_findclose(int64_t h);
void ts_crt_release(void);   /* ours: free the engine's blocks, close its streams / listings (ts_engine_reset) */
int ts_fseek(TSFILE *f, int off, int whence);
int msvc_rand(void); void msvc_srand(unsigned seed);
void *ts_malloc(size_t n);
void ts_free(void *p);
/* USER32 imports reached from the .nip runner (tools/peloader/win32.c behaviour): the progress callback FUN_180009f10
 * pumps messages - PeekMessageA reports "no message"; the runNestWin dialog functions are not used. */
int PeekMessageA(void *msg, void *hwnd, unsigned lo, unsigned hi, unsigned rm);
int TranslateMessage(void *msg); int64_t DispatchMessageA(void *msg); int IsDialogMessageA(void *h, void *msg);
int ShowWindow(void *h, int c); int SetWindowTextA(void *h, const char *s);
#define lstrcpyA strcpy
#endif
