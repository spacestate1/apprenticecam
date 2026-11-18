/* Site folders - kept out of the source. Read from <config dir>/paths.conf (platform.h: Linux ~/.config/mcamui,
   Windows %APPDATA%\apprenticeCAM; "key = value" lines, '#' comments) or the environment (MCAMUI_<KEY>, which wins).
   Values may be written Linux or Windows style. Unset = "" and the feature that needs it finds nothing.
     shared   the shared data folder (<APP_DIR_SHARED>: mill/Ops (Mill_MM / Mill_Inch .mcam-defaults), common/ToolData,
              mill|router/Tools, mill/Posts); Mastercam's "%MCAMDIR%" token resolves here too (host_path)
     user     the user's documents folder (CONFIG/: *.Workspace, *.kmp, *.mtb, *.config)
     schema   list of folders holding the Parasolid schema files (.sch_txt)
     cad      list of folders with parts (new-part templates, file dialog shortcut)
     posts    folder with posted programs (file dialog shortcut)
     drive_c, drive_d ...  (Linux) where a Windows drive is mounted, so "C:\Users\..." paths inside Mastercam's own
              files open (host_path)
   Lists are separated by ';' (or ':' on Linux, never splitting a "C:" drive). Linux paths are native here. */
#ifndef PATHS_H
#define PATHS_H
#include "platform.h"

static inline const char *site_path(const char *key) {
    static struct { char k[16], v[1024]; } t[32]; static int n = -1;
    char env[32]; snprintf(env, sizeof env, "MCAMUI_%s", key);
    for (char *q = env; *q; q++) *q = (char)toupper((unsigned char)*q);
    if (getenv(env)) return getenv(env);
    if (n < 0) {
        n = 0; char p[1100], line[1100];
        FILE *f = fopen(config_file(p, sizeof p, "paths.conf"), "r");
        while (f && n < 32 && fgets(line, sizeof line, f)) {
            char *h = strchr(line, '#'); if (h) *h = 0;
            char *eq = strchr(line, '='); if (!eq) continue; *eq = 0;
            char *k = line, *v = eq + 1;
            while (isspace((unsigned char)*k)) k++;
            for (char *e = k + strlen(k); e > k && isspace((unsigned char)e[-1]); ) *--e = 0;
            while (isspace((unsigned char)*v)) v++;
            for (char *e = v + strlen(v); e > v && isspace((unsigned char)e[-1]); ) *--e = 0;
            if (*v == '~' && (is_sep(v[1]) || !v[1])) snprintf(t[n].v, sizeof t[n].v, "%s%s", home_dir(), v + 1);
            else snprintf(t[n].v, sizeof t[n].v, "%s", v);
            snprintf(t[n].k, sizeof t[n].k, "%.15s", k); n++;
        }
        if (f) fclose(f);
    }
    for (int i = 0; i < n; i++) if (!strcmp(t[i].k, key)) return t[i].v;
    return "";
}
/* next entry of a folder list (';' anywhere, ':' too on Linux except a drive "X:"); returns the rest or NULL */
static inline const char *path_list_next(const char *s, char *out, size_t n) {
    if (!s || !*s) return NULL;
    const char *e = s;
    for (; *e; e++) {
        if (*e == ';') break;
#ifndef _WIN32
        if (*e == ':' && !(e == s + 1 && isalpha((unsigned char)s[0]))) break;
#endif
    }
    snprintf(out, n, "%.*s", (int)(e - s), s);
    return *e ? e + 1 : e;
}

/* a path as written by Windows / Mastercam or by the user -> one this OS opens. "<APP_DIR_SHARED>" = paths.conf
   shared. Linux: a Linux path is used as written (only when it does not exist is each component also tried without
   regard to case); a Windows-style path (path_is_windows) gets "X:" -> paths.conf drive_x, '\\' -> '/', then the
   case-insensitive match (NTFS names). Windows: unchanged apart from the macro. */
static inline char *host_path_(const char *in, char *out, size_t n, const char *shared);
/* "<APP_DIR_SHARED>" resolution: paths.conf shared first; when that is unset or the file is not there, the copy
   tools/import_libraries.py made in <config dir>/shared (same layout: mill/Tools, mill/Ops, router/..., MATERIALS,
   common/ToolData) */
/* "%MCAMDIR%" (any case) = Mastercam's shared-folder token in its own files (machine / control definitions: the post path
   CCNCMachine::PostProcFile, the libraries). Mastercam expands it with mastercam.dll TokenToPath (NOT DECODED); here it is
   "<APP_DIR_SHARED>", i.e. paths.conf shared = the shared Mcam2017 folder (ours: user decision P1, formats/user_decisions.md
   "Post engine"; the rule cui_toolpage.c tl6410_machine_lib / cui_grpprops.c lib_host already applied by hand) */
static inline int path_is_mcamdir(const char *in) {
    static const char t[] = "%MCAMDIR%";
    for (int i = 0; i < 9; i++) if (toupper((unsigned char)in[i]) != t[i]) return 0;
    return 1;
}
static inline char *host_path(const char *in, char *out, size_t n) {
    if (path_is_mcamdir(in)) { char t[1100]; snprintf(t, sizeof t, "<APP_DIR_SHARED>%s", in + 9); return host_path(t, out, n); }
    if (strncmp(in, "<APP_DIR_SHARED>", 16)) return host_path_(in, out, n, NULL);
    const char *sh = site_path("shared"); struct stat st;
    if (sh[0] && stat(host_path_(in, out, n, sh), &st) == 0) return out;
    char imp[1100]; config_file(imp, sizeof imp, "shared");
    if (stat(imp, &st) == 0 && stat(host_path_(in, out, n, imp), &st) == 0) return out;
    return host_path_(in, out, n, sh);
}
static inline char *host_path_(const char *in, char *out, size_t n, const char *shared) {
    char p[2048];
    if (shared) {
        const char *r = in + 16; while (*r == '/' || *r == '\\') r++;
        char rel[1024]; snprintf(rel, sizeof rel, "%s", r);   /* the rest is Mastercam's Windows text */
#ifndef _WIN32
        for (char *q = rel; *q; q++) if (*q == '\\') *q = '/';
#endif
        path_join(p, sizeof p, shared, rel);
    } else snprintf(p, sizeof p, "%s", in);
#ifdef _WIN32
    snprintf(out, n, "%s", p); return out;
#else
    if (!path_is_windows(p)) {                                   /* a Linux path: used exactly as written */
        struct stat st0;
        if (stat(p, &st0) == 0 || !strchr(p, '/')) { snprintf(out, n, "%s", p); return out; }
    } else for (char *q = p; *q; q++) if (*q == '\\') *q = '/';
    char cur[2048]; const char *rest = p;
    int win = path_is_windows(in);
    if (win && isalpha((unsigned char)p[0]) && p[1] == ':') {   /* drive letter */
        char key[16]; snprintf(key, sizeof key, "drive_%c", tolower((unsigned char)p[0]));
        const char *m = site_path(key);
        if (!m[0]) { snprintf(out, n, "%s", p); return out; }   /* not mapped: cannot open */
        snprintf(cur, sizeof cur, "%s", m); rest = p + 2;
    } else if (p[0] == '/') { cur[0] = 0; }
    else { snprintf(cur, sizeof cur, "."); }
    struct stat st;
    if (stat(p, &st) == 0 && rest == p) { snprintf(out, n, "%s", p); return out; }   /* exists as written */
    while (*rest) {                                              /* component by component, case-insensitive */
        while (*rest == '/') rest++;
        if (!*rest) break;
        const char *e = strchr(rest, '/'); size_t l = e ? (size_t)(e - rest) : strlen(rest);
        char name[512]; snprintf(name, sizeof name, "%.*s", (int)l, rest);
        char tryp[3072]; snprintf(tryp, sizeof tryp, "%s/%s", cur, name);
        if (stat(tryp, &st) != 0) {
            DIR *d = opendir(cur[0] ? cur : "/"); struct dirent *de;
            while (d && (de = readdir(d))) if (!strcasecmp(de->d_name, name)) { snprintf(tryp, sizeof tryp, "%s/%s", cur, de->d_name); break; }
            if (d) closedir(d);
        }
        snprintf(cur, sizeof cur, "%.2047s", tryp);
        rest += l;
    }
    if (!win && stat(cur, &st) != 0) { snprintf(out, n, "%s", p); return out; }   /* Linux path not found: as written */
    snprintf(out, n, "%s", cur[0] ? cur : "/");
    return out;
#endif
}
/* folder KEY + REL, or "" when KEY is not set */
static inline const char *site_file(char *out, size_t n, const char *key, const char *rel) {
    if (!strcmp(key, "shared")) { char t[1100]; snprintf(t, sizeof t, "<APP_DIR_SHARED>%s", rel); return host_path(t, out, n); }   /* + the imported copy */
    const char *b = site_path(key);
    if (!b[0]) { out[0] = 0; return out; }
    char t[2048]; path_join(t, sizeof t, b, rel);
    return host_path(t, out, n);
}
/* ours (Q19 "Save parameters to defaults file", user decision "make sure default can easily be reset", formats/user_decisions.md):
   Mastercam writes the machine group's operation defaults file in its shared folder (WriteOperationToDefaultsFile). This program
   never writes Mastercam's own files: a save goes to the program's own copy of the file, under the folder "defaults" (paths.conf
   key `defaults`, environment MCAMUI_DEFAULTS; unset = <config dir>/defaults), at the file's path relative to the shared folder in
   lower case ("mill/ops/mill_mm.mcam-defaults"; a file outside the shared folder: "other/<name>"). Every reader of a defaults file
   takes that copy when it exists (defaults_read), so the new operations here see what was saved. */
static inline const char *defaults_dir(char *out, size_t n) {
    const char *d = site_path("defaults");
    if (d[0]) snprintf(out, n, "%s", d); else config_file(out, n, "defaults");
    return out;
}
/* the key of the Mastercam file REAL (a resolved path): its path below the shared folder (paths.conf shared, else the imported
   copy <config dir>/shared), lower case, '/' separators; "other/<file name>" otherwise. 0 = no file name. */
static inline int defaults_key(const char *real, char *rel, size_t n) {
    char roots[2][1100]; snprintf(roots[0], sizeof roots[0], "%s", site_path("shared")); config_file(roots[1], sizeof roots[1], "shared");
    const char *r = NULL;
    for (int k = 0; k < 2 && !r; k++) {
        size_t L = strlen(roots[k]);
        while (L && is_sep(roots[k][L - 1])) roots[k][--L] = 0;
        if (L && !strncasecmp(real, roots[k], L) && is_sep(real[L])) r = real + L + 1;
    }
    if (r) snprintf(rel, n, "%s", r);
    else { const char *b = real + strlen(real); while (b > real && !is_sep(b[-1])) b--; if (!*b) return 0; snprintf(rel, n, "other/%s", b); }
    for (char *q = rel; *q; q++) { if (*q == '\\') *q = '/'; *q = (char)tolower((unsigned char)*q); }
    return rel[0] != 0;
}
/* the program's working copy of REAL (it may not exist) */
static inline char *defaults_work(const char *real, char *out, size_t n) {
    char d[1100], rel[600]; out[0] = 0;
    if (!defaults_key(real, rel, sizeof rel)) return out;
    defaults_dir(d, sizeof d); path_join(out, n, d, rel);
    return out;
}
/* the file a reader opens for the Mastercam defaults file REAL: the working copy when it exists, else REAL itself */
static inline const char *defaults_read(const char *real, char *out, size_t n) {
    char w[1700]; struct stat st;
    if (real && real[0] && defaults_work(real, w, sizeof w)[0] && stat(w, &st) == 0 && S_ISREG(st.st_mode)) snprintf(out, n, "%s", w);
    else snprintf(out, n, "%s", real ? real : "");
    return out;
}
#endif
