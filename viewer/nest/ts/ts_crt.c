/* ts_crt.c - MSVC CRT behaviour needed by the transliterated engine (see ts_crt.h for the address map). */
#include "ts_crt.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../nest_rect.h"

/* heap: the oracle (tools/peloader) serves HeapAlloc zeroed, so the port's allocations are zeroed too */
/* Blocks carry a 16-byte header as the oracle's HeapAlloc (tools/peloader/win32.c): the engine writes one byte before a
 * block it allocated (FUN_18006a930 L52836 "*(lVar3 + -1) = 1"), which lands in that header.
 * Ours: the engine's blocks come from its own arena (mmap'd chunks, the free lists and the set of live blocks kept
 * outside the blocks), not from the host heap: the DLL writes into freed blocks / past blocks when the progress
 * callback stops a run at some points (Esc; seed-17 x003 at the 40th progress call: the DLL under tools/peloader's
 * coverage build aborts in glibc's free, "corrupted unsorted chunks") - here that can only change engine data, never
 * the host heap.  ts_crt_release (ts_engine_reset, after a run stopped by ts_fatal) drops the whole arena; a free of a
 * block that is not live is ignored (stderr). */
#include <sys/mman.h>
#define TS_CHUNK ((size_t)64 << 20)
#define TS_NB 4096                                                  /* exact size classes of 16 bytes up to 64 KiB */
static unsigned char *ts_chunk; static size_t ts_chunk_used;
static void **ts_chunks; static size_t ts_nchunks, ts_capchunks;     /* every chunk / big block (mmap) */
static size_t *ts_chunksz;
static void **ts_fl[TS_NB]; static size_t ts_fln[TS_NB], ts_flcap[TS_NB];   /* free lists, out of band */
typedef struct { void *p; size_t n; } ts_live;                       /* live blocks: open addressing, p NULL = empty */
static ts_live *ts_set; static size_t ts_setcap, ts_setn;
static ts_live *ts_big; static size_t ts_nbig, ts_capbig;            /* free big blocks (size = without the header) */
static size_t ts_hash(void *p) { uint64_t x = (uint64_t)(uintptr_t)p; x ^= x >> 29; x *= 0xbf58476d1ce4e5b9ULL; x ^= x >> 32; return (size_t)x; }
static void ts_set_put(void *p, size_t n);
static void ts_set_grow(void) {
  ts_live *old = ts_set; size_t oc = ts_setcap;
  ts_setcap = oc ? oc * 2 : 4096; ts_set = calloc(ts_setcap, sizeof *ts_set); ts_setn = 0;
  if (!ts_set) ts_fatal("out of memory (block set)");
  for (size_t i = 0; i < oc; i++) if (old[i].p) ts_set_put(old[i].p, old[i].n);
  free(old);
}
static void ts_set_put(void *p, size_t n) {
  if ((ts_setn + 1) * 2 > ts_setcap) ts_set_grow();
  size_t i = ts_hash(p) & (ts_setcap - 1);
  while (ts_set[i].p) i = (i + 1) & (ts_setcap - 1);
  ts_set[i].p = p; ts_set[i].n = n; ts_setn++;
}
static long ts_set_del(void *p) {                                  /* the block's size, -1 when not live */
  if (!ts_setcap) return -1;
  size_t m = ts_setcap - 1, i = ts_hash(p) & m;
  while (ts_set[i].p && ts_set[i].p != p) i = (i + 1) & m;
  if (!ts_set[i].p) return -1;
  long n = (long)ts_set[i].n;
  ts_set[i].p = NULL; ts_setn--;
  for (size_t j = (i + 1) & m; ts_set[j].p; j = (j + 1) & m) {     /* backward-shift deletion */
    size_t h = ts_hash(ts_set[j].p) & m;
    if ((j > i && (h <= i || h > j)) || (j < i && (h <= i && h > j))) { ts_set[i] = ts_set[j]; ts_set[j].p = NULL; i = j; }
  }
  return n;
}
static void *ts_map(size_t n) {
  void *q = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (q == MAP_FAILED) ts_fatal("out of memory (%zu bytes)", n);
  if (ts_nchunks == ts_capchunks) {
    ts_capchunks = ts_capchunks ? ts_capchunks * 2 : 64;
    ts_chunks = realloc(ts_chunks, ts_capchunks * sizeof *ts_chunks); ts_chunksz = realloc(ts_chunksz, ts_capchunks * sizeof *ts_chunksz);
  }
  ts_chunks[ts_nchunks] = q; ts_chunksz[ts_nchunks++] = n;
  return q;
}
void *ts_malloc(size_t n) {
  size_t r = (n + 15) & ~(size_t)15, k = r / 16; unsigned char *b;
  if (k < TS_NB && ts_fln[k]) { b = ts_fl[k][--ts_fln[k]]; memset(b, 0, r + 16); }
  else if (k < TS_NB) {
    if (!ts_chunk || ts_chunk_used + r + 16 > TS_CHUNK) { ts_chunk = ts_map(TS_CHUNK); ts_chunk_used = 0; }
    b = ts_chunk + ts_chunk_used; ts_chunk_used += r + 16;          /* fresh mmap memory is zero */
  } else {                                                          /* big block: its own mapping, reused at the same size */
    b = NULL;
    for (size_t i = 0; i < ts_nbig; i++) if (ts_big[i].n == r) { b = ts_big[i].p; ts_big[i] = ts_big[--ts_nbig]; memset(b, 0, r + 16); break; }
    if (!b) b = ts_map(r + 16);
  }
  *(size_t *)b = n;
  ts_set_put(b + 16, r);
  return b + 16;
}
static long ts_badfree;
void ts_free(void *p) {
  if (!p) return;
  long r = ts_set_del(p);
  if (r < 0) { if (ts_badfree++ < 5) fprintf(stderr, "nest_ts: free of a block that is not allocated (ignored)\n"); return; }
  size_t k = (size_t)r / 16;
  if (k >= TS_NB) {                                                 /* big block: kept mapped for a block of its size */
    if (ts_nbig == ts_capbig) { ts_capbig = ts_capbig ? ts_capbig * 2 : 64; ts_big = realloc(ts_big, ts_capbig * sizeof *ts_big); }
    ts_big[ts_nbig].p = (unsigned char *)p - 16; ts_big[ts_nbig++].n = (size_t)r; return;
  }
  if (ts_fln[k] == ts_flcap[k]) { ts_flcap[k] = ts_flcap[k] ? ts_flcap[k] * 2 : 64; ts_fl[k] = realloc(ts_fl[k], ts_flcap[k] * sizeof(void *)); }
  ts_fl[k][ts_fln[k]++] = (unsigned char *)p - 16;
}
void *ts_new(size_t n) { return ts_malloc(n); }
int32_t ts_clock(void) { return 0; }

char *ts_itoa(int v, char *buf, int radix) {          /* _itoa L245673 -> xtoa L245621: radix 10 signed */
  if (radix == 10) { sprintf(buf, "%d", v); return buf; }
  unsigned u = (unsigned)v; char t[40]; int n = 0;
  do { int d = (int)(u % (unsigned)radix); t[n++] = (char)(d > 9 ? d - 10 + 'a' : d + '0'); u /= (unsigned)radix; } while (u);
  for (int i = 0; i < n; i++) buf[i] = t[n - 1 - i];
  buf[n] = 0; return buf;
}
long ts_ftell(TSFILE *f) { return ftell(f->f); }
int ts_feof(TSFILE *f) { return f->eof || feof(f->f) ? 0x10 : 0; }
int ts_vsprintf(char *out, const char *fmt, va_list ap) { return ts_vsnprintf(out, (size_t)1 << 30, fmt, ap); }
/* atof: the DLL's _atof_l -> $I10_INPUT; strtod is correctly rounded - equal for the short decimals of .nip / .dxf input */
double ts_atof(const char *s) { return strtod(s, NULL); }
/* _CxxThrowException(obj, ThrowInfo): the ThrowInfo is a DAT_ in the image - its DLL address names it */
void ts_cxx_throw(void *obj, void *throwinfo) { ts_throw(obj, (uint64_t)((unsigned char *)throwinfo - ts_img) + TS_IMG_LO); }
void ts_msvc_qsort(void *base, uint64_t n, uint64_t w, void *cmp) { msvc_qsort(base, n, w, (int (*)(const void *, const void *))cmp); }
char *ts_strchr(const char *s, int c) { return (char *)strchr(s, (char)c); }   /* FUN_180226b39 compares (char) */
int ts_access(const char *path, int mode) { TSFILE *f = ts_fopen(path, "rb"); if (f) { ts_fclose(f); return 0; } (void)mode; return -1; }
/* directories / files: tools/peloader/win32.c semantics (drive letter dropped, '\\' -> '/') */
static void hpath(const char *in, char *out) {
  size_t k = 0; if (isalpha((unsigned char)in[0]) && in[1] == ':') in += 2;
  for (; *in && k < 1023; in++) out[k++] = *in == '\\' ? '/' : *in;
  out[k] = 0;
}
/* _findfirst / _findnext / _findclose (FUN_1802294a8 L249243 / FUN_1802295f0 L249314 / FUN_1802293f0 L249186:
 * FindFirstFileExA / FindNextFileA / FindClose): the one pattern NestLib uses, "*.*" in a directory (FUN_1800577a0 L44000: sprintf "%s/" "*.*") =
 * every entry of the directory, "." and ".." included; _finddata64i32_t: attrib +0 (0x10 directory, else 0 - the CRT
 * maps FILE_ATTRIBUTE_NORMAL 0x80 to 0), size +0x20, name +0x24 (the times +8..+0x18 are left 0).  The handle is an
 * index of the open listings. */
#include <dirent.h>
static DIR *ts_finds[16]; static char ts_finddir[16][1024];
static int find_fill(int h, int *fd) {
  struct dirent *e = readdir(ts_finds[h]);
  if (!e) return -1;
  memset(fd, 0, 0x24 + 260);
  char full[2100]; struct stat st; snprintf(full, sizeof full, "%s/%s", ts_finddir[h], e->d_name);
  if (stat(full, &st) == 0) { fd[0] = S_ISDIR(st.st_mode) ? 0x10 : 0; fd[8] = (int)st.st_size; }
  snprintf((char *)fd + 0x24, 260, "%s", e->d_name);
  return 0;
}
int64_t ts_findfirst(const char *spec, int *fd) {
  char h[1024]; hpath(spec, h);
  size_t L = strlen(h);
  if (L < 4 || strcmp(h + L - 4, "/*.*")) ts_fatal("_findfirst \"%s\": only <dir>/*.* is ported", spec);
  h[L - 4] = 0;
  int k = 0; while (k < 16 && ts_finds[k]) k++;
  if (k == 16) ts_fatal("_findfirst: too many open listings");
  if (!(ts_finds[k] = opendir(h))) return -1;
  snprintf(ts_finddir[k], sizeof ts_finddir[k], "%s", h);
  if (find_fill(k, fd)) { closedir(ts_finds[k]); ts_finds[k] = NULL; return -1; }
  return k;
}
int ts_findnext(int64_t h, int *fd) { return h >= 0 && h < 16 && ts_finds[h] ? find_fill((int)h, fd) : -1; }
int ts_findclose(int64_t h) {
  if (h < 0 || h >= 16 || !ts_finds[h]) return -1;
  closedir(ts_finds[h]); ts_finds[h] = NULL; return 0;
}
/* ours: everything the engine holds (blocks, open streams, listings) - its state is abandoned (ts_engine_reset) */
void ts_crt_release(void) {
  for (size_t i = 0; i < ts_nchunks; i++) munmap(ts_chunks[i], ts_chunksz[i]);
  ts_nchunks = 0; ts_chunk = NULL; ts_chunk_used = 0;
  for (int k = 0; k < TS_NB; k++) ts_fln[k] = 0;
  ts_nbig = 0;
  if (ts_set) memset(ts_set, 0, ts_setcap * sizeof *ts_set);
  ts_setn = 0; ts_badfree = 0;
  for (int k = 0; k < 16; k++) if (ts_finds[k]) { closedir(ts_finds[k]); ts_finds[k] = NULL; }
  ts_fclose_all();
}
int ts_mkdir(const char *path) { char h[1024]; hpath(path, h); return mkdir(h, 0777) ? -1 : 0; }
int ts_rmdir(const char *path) { char h[1024]; hpath(path, h); if (getenv("TS_KEEPTMP")) return 0; return rmdir(h) ? -1 : 0; }
int ts_remove(const char *path) {
  char h[1024]; hpath(path, h);
  if (getenv("TS_KEEPTMP")) {   /* debug: keep the engine's temporary files (as kept_<n>_<name>) to compare with the DLL's */
    static int kn; char q[1200]; const char *b = strrchr(h, '/'); snprintf(q, sizeof q, "kept_%03d_%s", kn++, b ? b + 1 : h);
    return rename(h, q) ? -1 : 0;
  }
  return unlink(h) ? -1 : 0;
}
int ts_fseek(TSFILE *f, int off, int whence) { if (f->text) ts_fatal("fseek on a text stream not ported"); return fseek(f->f, off, whence); }
int PeekMessageA(void *msg, void *hwnd, unsigned lo, unsigned hi, unsigned rm) { (void)msg; (void)hwnd; (void)lo; (void)hi; (void)rm; return 0; }
int TranslateMessage(void *msg) { (void)msg; ts_fatal("TranslateMessage not ported"); }
int64_t DispatchMessageA(void *msg) { (void)msg; ts_fatal("DispatchMessageA not ported"); }
int IsDialogMessageA(void *h, void *msg) { (void)h; (void)msg; ts_fatal("IsDialogMessageA not ported"); }
int ShowWindow(void *h, int c) { (void)h; (void)c; ts_fatal("ShowWindow not ported"); }
int SetWindowTextA(void *h, const char *s) { (void)h; (void)s; ts_fatal("SetWindowTextA not ported"); }
/* `eh vector constructor / destructor iterator' (L246425 / L246374): construct count objects forwards, destroy
 * backwards (the unwinding on a throwing constructor is not reproduced: the engine's constructors do not throw) */
void ts_ehvec_ctor(void *p, uint64_t size, int count, void *ctor, void *dtor) {
  (void)dtor; for (int i = 0; i < count; i++) ((void (*)(void *))ctor)((char *)p + (uint64_t)i * size);
}
void ts_ehvec_dtor(void *p, uint64_t size, int count, void *dtor) {
  for (int i = count - 1; i >= 0; i--) ((void (*)(void *))dtor)((char *)p + (uint64_t)i * size);
}
