/* Filesystem portability: Linux and Windows, each native. Every path the program builds or splits goes through here.
   - Linux paths are first class: '/' is the only separator, "/x" is absolute, and any other character (including
     '\\' and ':') is part of a name. Windows-style text is translated only where it really is a Windows path
     (paths.h host_path: Mastercam's own files, or a "C:\\..." value the user typed).
   - Windows: '/' and '\\' both separate, "C:\\x" / "C:/x" / "\\\\server\\x" are absolute; new paths use PATH_SEP
   - folders: config (Windows %APPDATA%\\apprenticeCAM, Linux $XDG_CONFIG_HOME/mcamui or ~/.config/mcamui), home
     (HOME, else USERPROFILE), executable (GetModuleFileName / /proc/self/exe)
   - Windows paths written by Mastercam (config files, tool libraries: "C:\\Users\\Public\\...") are opened on Linux
     through host_path(): drive letters map to folders given in paths.conf ("drive_c = <windows>"), backslashes
     become '/', and each component is matched case-insensitively (NTFS names are case-insensitive). */
#ifndef PLATFORM_H
#define PLATFORM_H
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <direct.h>
#  include <io.h>
#  ifndef R_OK
#    define R_OK 4
#    define F_OK 0
#  endif
#  define PATH_SEP '\\'
#  define PATH_SEP_S "\\"
#else
#  include <unistd.h>
#  include <fcntl.h>
#  define PATH_SEP '/'
#  define PATH_SEP_S "/"
#endif

#ifdef _WIN32
static inline int is_sep(char c) { return c == '/' || c == '\\'; }
#else
static inline int is_sep(char c) { return c == '/'; }
#endif
/* last separator in P (either kind), or NULL */
static inline char *path_sep_r(const char *p) {
    const char *r = NULL;
    for (; p && *p; p++) if (is_sep(*p)) r = p;
    return (char *)r;
}
/* file name part of P */
static inline const char *path_base(const char *p) { const char *s = path_sep_r(p); return s ? s + 1 : p; }
/* length of the folder part of P (without the final separator; 0 = none) */
static inline int path_dirlen(const char *p) { const char *s = path_sep_r(p); return s ? (int)(s - p) : 0; }
static inline int path_is_abs(const char *p) {
#ifdef _WIN32
    return p && (is_sep(p[0]) || (isalpha((unsigned char)p[0]) && p[1] == ':' && (is_sep(p[2]) || !p[2])));
#else
    return p && p[0] == '/';
#endif
}
/* written Windows-style ("C:\\x", "C:/x", "\\\\server\\x", or backslashes and no '/'): Mastercam's own files */
static inline int path_is_windows(const char *p) {
    if (!p || !p[0]) return 0;
    if (isalpha((unsigned char)p[0]) && p[1] == ':' && (p[2] == '\\' || p[2] == '/' || !p[2])) return 1;
    if (p[0] == '\\' && p[1] == '\\') return 1;
    return strchr(p, '\\') && !strchr(p, '/');
}
/* is P a filesystem root ("/", "\\", "C:\\", "C:") */
static inline int path_is_root(const char *p) {
    if (is_sep(p[0]) && !p[1]) return 1;
#ifdef _WIN32
    return isalpha((unsigned char)p[0]) && p[1] == ':' && (!p[2] || (is_sep(p[2]) && !p[3]));
#else
    return 0;
#endif
}
/* OUT = A + separator + B (B absolute = B alone; A empty = B) */
static inline char *path_join(char *out, size_t n, const char *a, const char *b) {
    if (!a || !a[0] || path_is_abs(b)) { snprintf(out, n, "%s", b); return out; }
    size_t la = strlen(a);
    snprintf(out, n, "%s%s%s", a, is_sep(a[la - 1]) ? "" : PATH_SEP_S, b);
    return out;
}
/* parent folder of P in place ("/a/b" -> "/a", "/a" -> "/", "C:\\a" -> "C:\\"); returns 0 at a root */
static inline int path_up(char *p) {
    if (path_is_root(p)) return 0;
    char *s = path_sep_r(p); if (!s) return 0;
    if (s == p) { p[1] = 0; return 1; }                              /* "/a" -> "/" */
#ifdef _WIN32
    if (s == p + 2 && p[1] == ':') { p[3] = 0; return 1; }          /* "C:\\a" -> "C:\\" */
#endif
    *s = 0; return 1;
}

static inline const char *home_dir(void) {
    const char *h = getenv("HOME");
    if (!h || !h[0]) h = getenv("USERPROFILE");
    return h && h[0] ? h : ".";
}
/* the program's own settings folder (icons.pack, paths.conf, recent.txt) */
static inline const char *config_dir(void) {
    static char d[1024];
    if (!d[0]) {
        const char *e = getenv("MCAMUI_CONFIG_DIR");
#ifdef _WIN32
        const char *ad = getenv("APPDATA");
        if (e && e[0]) snprintf(d, sizeof d, "%s", e);
        else if (ad && ad[0]) snprintf(d, sizeof d, "%s\\apprenticeCAM", ad);
        else snprintf(d, sizeof d, "%s\\apprenticeCAM", home_dir());
#else
        const char *x = getenv("XDG_CONFIG_HOME");
        if (e && e[0]) snprintf(d, sizeof d, "%s", e);
        else if (x && x[0]) snprintf(d, sizeof d, "%s/mcamui", x);
        else snprintf(d, sizeof d, "%s/.config/mcamui", home_dir());
#endif
    }
    return d;
}
static inline char *config_file(char *out, size_t n, const char *name) { return path_join(out, n, config_dir(), name); }
/* folder holding the running executable ("." when unknown) */
static inline const char *exe_dir(void) {
    static char d[1024];
    if (!d[0]) {
#ifdef _WIN32
        DWORD k = GetModuleFileNameA(NULL, d, sizeof d - 1); d[k] = 0;
#else
        ssize_t k = readlink("/proc/self/exe", d, sizeof d - 1); d[k > 0 ? k : 0] = 0;
#endif
        char *s = path_sep_r(d); if (s) *s = 0; else snprintf(d, sizeof d, ".");
    }
    return d;
}
static inline int make_dir(const char *p) {
#ifdef _WIN32
    return _mkdir(p);
#else
    return mkdir(p, 0755);
#endif
}
/* make P and its parents */
static inline void make_dirs(const char *p) {
    char t[1024]; snprintf(t, sizeof t, "%s", p);
    for (char *q = t + 1; *q; q++) if (is_sep(*q) && q[-1] != ':') { char c = *q; *q = 0; make_dir(t); *q = c; }   /* not "C:" */
    make_dir(t);
}
/* absolute form of P (OUT = P when it cannot be resolved) */
static inline char *full_path(const char *p, char *out, size_t n) {
#ifdef _WIN32
    if (!_fullpath(out, p, n)) snprintf(out, n, "%s", p);
#else
    char r[4096]; if (realpath(p, r)) snprintf(out, n, "%s", r); else snprintf(out, n, "%s", p);
#endif
    return out;
}
/* creation time of a file (0 = the filesystem does not record one) */
static inline time_t file_btime(const char *p) {
#ifdef _WIN32
    struct _stat64 st; return _stat64(p, &st) == 0 ? (time_t)st.st_ctime : 0;   /* NTFS: st_ctime = creation */
#elif defined(STATX_BTIME)
    struct statx sx;
    if (statx(AT_FDCWD, p, 0, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME)) return (time_t)sx.stx_btime.tv_sec;
    return 0;
#else
    (void)p; return 0;
#endif
}
static inline struct tm *local_time(time_t t, struct tm *out) {
#ifdef _WIN32
    return localtime_s(out, &t) ? NULL : out;
#else
    return localtime_r(&t, out);
#endif
}
#ifdef _WIN32
#  ifndef strtok_r
#    define strtok_r strtok_s
#  endif
#  define strcasecmp _stricmp
#  define strncasecmp _strnicmp
#endif
/* A and B name the same existing file (links, "./", case on Windows) */
static inline int same_file(const char *a, const char *b) {
#ifdef _WIN32
    HANDLE ha = CreateFileA(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    HANDLE hb = CreateFileA(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    int same = 0;
    if (ha != INVALID_HANDLE_VALUE && hb != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION ia, ib;
        if (GetFileInformationByHandle(ha, &ia) && GetFileInformationByHandle(hb, &ib))
            same = ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber && ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
    }
    if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
    if (hb != INVALID_HANDLE_VALUE) CloseHandle(hb);
    return same;
#else
    struct stat sa, sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}
/* replace DST by SRC (atomic on Linux; Windows rename() cannot overwrite, MoveFileEx can); 0 = ok */
static inline int file_replace(const char *src, const char *dst) {
#ifdef _WIN32
    return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(src, dst);
#endif
}
/* allocation that cannot return NULL: out of memory (or an absurd size from a damaged file) ends the program with a
   message instead of a NULL dereference somewhere later */
static inline void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "out of memory (%zu bytes)\n", n); exit(3); }
    return q;
}
static inline void *xmalloc(size_t n) { return xrealloc(NULL, n); }
static inline void *xcalloc(size_t a, size_t b) {
    if (b && a > (size_t)-1 / b) { fprintf(stderr, "out of memory\n"); exit(3); }
    void *q = xmalloc(a * b); memset(q, 0, a * b); return q;
}
/* one text line of any length into *BUF (grown as needed), without the newline; 0 at end of file */
static inline int read_line(FILE *f, char **buf, size_t *cap) {
    size_t n = 0; int c;
    if (!*buf || *cap < 256) { *cap = 256; *buf = xrealloc(*buf, *cap); }
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') break;
        if (n + 2 > *cap) { *cap *= 2; *buf = xrealloc(*buf, *cap); }
        (*buf)[n++] = (char)c;
    }
    if (c == EOF && n == 0) return 0;
    if (n && (*buf)[n - 1] == '\r') n--;
    (*buf)[n] = 0; return 1;
}
/* flush a written file to disk */
static inline int file_sync(FILE *f) {
#ifdef _WIN32
    return _commit(_fileno(f));
#else
    return fsync(fileno(f));
#endif
}
/* memmem / memrchr are GNU-only */
static inline const void *mem_mem(const void *h, size_t hn, const void *nd, size_t nn) {
    if (!nn) return h;
    for (const unsigned char *p = (const unsigned char *)h; hn >= nn; p++, hn--)
        if (*p == *(const unsigned char *)nd && !memcmp(p, nd, nn)) return p;
    return NULL;
}
static inline const void *mem_rchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s + n;
    while (n--) if (*--p == (unsigned char)c) return p;
    return NULL;
}
#endif
