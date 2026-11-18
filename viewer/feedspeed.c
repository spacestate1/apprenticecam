/* feedspeed - port of Mastercam 2017's feed / speed calculation. Sources (../formats/feeds_speeds.md):
 * mccore calc_op_fs @180336700 (CFeedCalc), FUN_180338050 rpm, FUN_180337a40 feed, FUN_180337df0 / FUN_180337f20
 * plunge / retract, FUN_180337bb0 CFeedCalc::CalcFpt, CalculateSFMFromMaterial @180334390, GetSFMIndex @180335580,
 * op_is_* classifiers; material records read like GetMaterialFromName / import_matl_ent. */
#include "feedspeed.h"
#include "paths.h"
#include <math.h>
#include <stdint.h>

#define TOL (mc_systol.tol)              /* mccore `tol`: config 1000 System tolerance (McSysTol, mcam_load.h; default 5e-5) */

/* ---------------------------------------------------------------- operation classes (mccore op_is_*) */
static int op_is_lathe(int c) { return (unsigned)(c - 0x3c) <= 0xc || (unsigned)(c - 0xc9) <= 0x23; }
static int op_is_msurf(int c) {
    switch (c) { case 5: case 6: case 7: case 8: case 9: case 10: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: case 0x1c: case 0x1d:
    case 0x27: case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x30: case 0x6b: case 0x6d:
    case 0x6e: case 0x70: case 0x71: case 0x83: case 0x84: return 1; }
    return 0;
}
static int op_is_drill(int c) { return ((unsigned)c <= 0x39 && ((0x380000010000004ULL >> (c & 0x3f)) & 1)) || c == 0x132; }
static int op_is_contour(int c) {
    switch (c) { case 1: case 0x33: case 0x34: case 0x35: case 0x4a: case 0x53: case 0x82: case 0x85: return 1; }
    return 0;
}
static int op_is_point(int c) {
    unsigned u = (unsigned)(c - 100);
    return op_is_drill(c) || c == 0x12 || (u <= 0xd && ((0x2041u >> u) & 1));
}
static int op_is_chook(int c) { return c == 0x10 || c == 0xd5 || (unsigned)(c - 0x191) <= 0x62; }
static int op_is_oldnci(int c) {
    if (op_is_msurf(c) || op_is_chook(c)) return 0;
    if (c > 0x132) return 1;
    if (c == 0x132) return 0;
    switch (c) {
    case 1: case 2: case 3: case 4: case 0x11: case 0x12: case 0x13: case 0x15: case 0x16: case 0x18: case 0x19: case 0x1a:
    case 0x1b: case 0x1c: case 0x1d: case 0x30: case 0x33: case 0x34: case 0x35: case 0x37: case 0x38: case 0x39: case 0x3c:
    case 0x3d: case 0x3e: case 0x3f: case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x47: case 0x48: case 0x4a:
    case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x50: case 0x51: case 0x53: case 100: case 0x65: case 0x66:
    case 0x67: case 0x68: case 0x69: case 0x6a: case 0x6c: case 0x6e: case 0x6f: case 0x70: case 0x82: case 0x85: case 0x86:
    case 0x87: case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x96: case 0x9a: case 0xc9: case 0xca: case 0xce: case 0xd0:
    case 0xd2: case 0xd3: case 0xd4: case 0xd6: case 0xd7: case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xe0:
    case 0xe1: case 0xe6: case 0xe7: case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: return 0;
    }
    return 1;
}
static int op_is_solid_ctrl(int c) { return c == 0x68 || (unsigned)(c - 0x88) <= 2; }
static int op_is_notool(int c) {                     /* op_is_notool(c, 1) */
    if (op_is_oldnci(c) || op_is_solid_ctrl(c)) return 1;
    switch (c) { case 0: case 4: case 0x11: case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x22: case 0x23: case 0x2f: case 0x4a:
    case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x50: case 0x51: case 0x52: case 0x53: case 0x67: case 0x6c:
    case 0x8b: case 0x9a: case 0xd6: case 0xd7: case 0xd9: case 0xda: case 0xdb: case 0xe6: case 0xe8: case 0xe9: case 0xea:
    case 0xeb: case 0xec: return 1; }
    return 0;
}
int fs_op_skipped(int c) { return !op_is_msurf(c) && (op_is_oldnci(c) || op_is_notool(c) || op_is_lathe(c)); }

int fs_sfm_index(int c) {                            /* GetSFMIndex */
    if (op_is_lathe(c)) return -1;
    if (op_is_drill(c)) return 0;
    if (op_is_contour(c) || op_is_point(c)) return 1;
    if (c == 3 || c == 0x66 || c == 0x69) return 2;
    if (!op_is_msurf(c)) {
        if (op_is_oldnci(c)) return -1;
        if (c == 0x13 || c == 0x65) return 1;
        if (c != 0x15 && c != 0x1b && c != 0x16 && c != 0x18 && c != 0x19 && c != 0x1a) {
            if (c == 0x68 || c == 0x88) return 0;
            if (c == 0x8a) return 1;
            if (c == 0x89 || c == 0x86) return 2;
            return -1;
        }
    }
    return 3;
}

/* ---------------------------------------------------------------- material libraries */
static uint32_t rd32(const uint8_t *b) { uint32_t v; memcpy(&v, b, 4); return v; }
static double rdd(const uint8_t *b) { double v; memcpy(&v, b, 8); return v; }

int matlib_load(const char *path, MatLib *L, char *err, size_t errn) {
    memset(L, 0, sizeof *L); snprintf(L->path, sizeof L->path, "%s", path);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errn, "cannot open %s", path); return 0; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > (1L << 30)) { fclose(f); snprintf(err, errn, "%s: not a material library", path); return 0; }
    uint8_t *d = malloc((size_t)len);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); snprintf(err, errn, "cannot read %s", path); return 0; }
    fclose(f);
    size_t off = 0; int cap = 0;
    while (off + 4 <= (size_t)len) {                 /* part-file record container (formats/mcam.md) */
        uint32_t n = rd32(d + off);
        if (n == 0 || off + 4 + n > (size_t)len) break;
        const uint8_t *r = d + off + 4;
        off += 4 + n;
        if (n >= 0x6a && rd32(r) == 0x200 && (r[0x68] | r[0x69] << 8) == 0xE) {   /* material table payload */
            while (off + 0x28 <= (size_t)len) { int32_t id = (int32_t)rd32(d + off); uint64_t s; memcpy(&s, d + off + 0x10, 8);
                off += 0x28; if (!id) break; if (s > (uint64_t)len - off) { off = (size_t)len; break; } off += (size_t)s; }
            continue;
        }
        if (n != 0x68 + 0x3a0 || rd32(r) != 0x4000) continue;
        const uint8_t *b = r + 0x68;
        if (L->n == cap) {
            Material *nm = realloc(L->m, (size_t)(cap ? cap * 2 : 64) * sizeof *L->m);
            if (!nm) { free(d); snprintf(err, errn, "out of memory"); matlib_free(L); return 0; }
            L->m = nm; cap = cap ? cap * 2 : 64;
        }
        Material *m = &L->m[L->n++]; memset(m, 0, sizeof *m);
        memcpy(m->name, b, sizeof m->name - 1);
        m->units = (int16_t)(b[0xc8] | b[0xc9] << 8);
        m->sfm = rdd(b + 0xd0); for (int i = 0; i < 4; i++) m->sfm_pct[i] = rdd(b + 0xd8 + 8 * i);
        m->fpt = rdd(b + 0xf8); for (int i = 0; i < 26; i++) m->fpt_pct[i] = rdd(b + 0x100 + 8 * i);
        for (int i = 0; i < 6; i++) { m->tm_on[i] = b[0x1d0 + i]; m->tm_sfm[i] = rdd(b + 0x1d8 + 8 * i); m->tm_fpt[i] = rdd(b + 0x208 + 8 * i); }
    }
    free(d);
    if (!L->n) { snprintf(err, errn, "%s: no materials", path); return 0; }
    return 1;
}
void matlib_free(MatLib *L) { free(L->m); memset(L, 0, sizeof *L); }
const Material *matlib_find(const MatLib *L, const char *name) {
    for (int i = 0; i < L->n; i++) if (!strcmp(L->m[i].name, name)) return &L->m[i];
    return NULL;
}
char *matlib_resolve(const char *mp, char *out, size_t n) {
    static const char *old[] = { ".materials-9", ".materials-8", ".materials-7", ".materials-6", ".materials-5", ".materials" };
    char p[1024]; snprintf(p, sizeof p, "%s", mp);
    char *dot = strrchr(p, '.');
    for (size_t i = 0; dot && i < sizeof old / sizeof *old; i++)
        if (!strcasecmp(dot, old[i])) { snprintf(dot, sizeof p - (size_t)(dot - p), ".mcam-materials"); break; }
    if (p[0] == '\\' || p[0] == '/') {               /* relative to the shared data folder */
        char t[1200]; snprintf(t, sizeof t, "<APP_DIR_SHARED>%s", p); return host_path(t, out, n);
    }
    return host_path(p, out, n);
}

/* ---------------------------------------------------------------- the calculation */
static int tm_index(const Tool *t) { return (t->tool_mat < 1 ? 1 : t->tool_mat) - 1; }   /* CalculateSFMFromMaterial: < 1 -> 1 */
static int pct_applies(double v) { return fabs(v - 0.0) >= TOL && fabs(v - 100.0) >= TOL; }

static int calc_sfm(int opcode, const Tool *t, const Material *m, int mode, double speed_pct, double *sfm) {
    int k = fs_sfm_index(opcode), tm = tm_index(t);
    *sfm = k >= 0 ? m->sfm_pct[k] / 100.0 * m->sfm : m->sfm;
    if (mode == FS_MATERIAL) {
        if (speed_pct > TOL) *sfm = speed_pct / 100.0 * *sfm;
        if (tm < 6 && m->tm_on[tm] && pct_applies(m->tm_sfm[tm])) *sfm = m->tm_sfm[tm] / 100.0 * *sfm;
    }
    int ok = *sfm > TOL;
    if (m->units == 1 || m->units == 2) *sfm /= 0.3048;   /* FUN_1803381f0 */
    return ok;
}
static int calc_fpt(const Tool *t, const Material *m, int mode, double feed_pct, double *fpt) {   /* CFeedCalc::CalcFpt */
    int ty = t->type, tm = tm_index(t);
    *fpt = (ty >= 0 && ty < 26 ? m->fpt_pct[ty] / 100.0 : 0) * m->fpt;
    if (m->units == 1 || m->units == 2) *fpt /= 25.4;
    if (mode == FS_MATERIAL) {
        if (feed_pct > TOL) *fpt = feed_pct / 100.0 * *fpt;
        if (tm < 6 && m->tm_on[tm] && pct_applies(m->tm_fpt[tm])) *fpt = m->tm_fpt[tm] / 100.0 * *fpt;
    }
    /* taps: CalcFpt reads op_tool_info +0x18 (pitch, inch: threads per inch) -> metric pitch / 25.4, inch 1 / TPI. Tool.pitch
       is the pitch in part units for both (mcam_load / toollib: TlToolThreading::SetOpToolInfo), so inch = pitch */
    if ((unsigned)(ty - 4) < 2 && t->pitch > TOL) *fpt = t->is_metric ? t->pitch / 25.4 : t->pitch;
    return *fpt > TOL;
}
/* FUN_180337a40: feed from |rpm| (also the base of plunge / retract) */
static int calc_feed(int rpm, const Tool *t, const FsGroup *g, const Material *m, double op_feed, double feed_pct, double *feed) {
    *feed = 0;
    if (g->mode == FS_TOOL) *feed = op_feed;
    else if (g->mode == FS_USER) *feed = g->user_feed;
    else {
        double fpt; if (!m || !calc_fpt(t, m, g->mode, feed_pct, &fpt)) return 0;
        int z = (unsigned)(t->type - 4) < 2 ? 1 : t->flutes;
        *feed = (double)abs(rpm) * (double)z * fpt;
    }
    if (*feed <= TOL) { *feed = 0; return 0; }
    return 1;
}

FsResult fs_calc(int opcode, int op_rpm, double op_feed, const Tool *t, const FsGroup *g, const Material *m,
                 double feed_pct, double speed_pct, int ccw, long max_rpm) {
    FsResult r = { 0, op_rpm, op_feed, t->plunge, t->retract };
    if (fs_op_skipped(opcode)) return r;
    if (g->mode == FS_MATERIAL && !m) return r;                     /* material not found: op unchanged */
    /* rpm (FUN_180338050) - written into the op even when a later step fails, as Mastercam does */
    int rpm = 0, ok = 0;
    if (g->mode == FS_TOOL) { rpm = op_rpm; ok = 1; }
    else if (g->mode == FS_USER) { if (isfinite(g->user_rpm) && fabs(g->user_rpm) < 1e6) { rpm = (int)g->user_rpm; ok = 1; } }
    else if (m) {
        double sfm;
        if (calc_sfm(opcode, t, m, g->mode, speed_pct, &sfm)) {
            double D = t->is_metric ? t->dia / 25.4 : t->dia;
            double v = D >= TOL ? sfm * 3.82 / D : NAN;      /* range-checked before the int conversion */
            if (isfinite(v) && v < 1e6) { rpm = (int)v; if (ccw) rpm = -rpm; ok = 1; }
        }
    }
    if (ok) {
        if (fabs((double)rpm) <= TOL) { rpm = 0; ok = 0; }
        else if (max_rpm > 0 && (double)max_rpm > TOL && (double)max_rpm < fabs((double)rpm)) rpm = rpm < 0 ? -(int)max_rpm : (int)max_rpm;
    }
    r.rpm = rpm;
    if (!ok) return r;
    /* feed */
    if (!calc_feed(rpm, t, g, m, op_feed, feed_pct, &r.feed)) return r;
    /* plunge / retract (FUN_180337df0 / FUN_180337f20); drill opcodes 2, 0x37-0x39 copy the feed */
    if (opcode == 2 || (unsigned)(opcode - 0x37) < 3) { r.plunge = r.feed; r.retract = r.feed; }
    else {
        double f;
        if (g->mode == FS_TOOL) r.plunge = t->plunge;
        else if (g->mode == FS_USER) r.plunge = g->user_plunge;
        else { calc_feed(rpm, t, g, m, op_feed, feed_pct, &f); r.plunge = f * 0.5; }
        if (r.plunge <= TOL) { r.plunge = 0; return r; }
        if (g->mode == FS_TOOL) r.retract = t->retract;
        else if (g->mode == FS_USER) r.retract = g->user_retract;
        else { calc_feed(rpm, t, g, m, op_feed, feed_pct, &f); r.retract = f * 0.5; }
        if (r.retract <= TOL) r.retract = 0;
    }
    /* calc_op_fs: material units == 1 -> x 25.4; the material struct is only loaded in From material (FUN_18033b570
       runs for group mode 1), so the other modes never convert */
    if (g->mode == FS_MATERIAL && m && m->units == 1) { r.feed *= 25.4; r.plunge *= 25.4; r.retract *= 25.4; }
    /* Mastercam writes the steps that succeeded into the op even when a later one fails (e.g. flutes 0 -> feed 0;
       CheckNumberOfFlutes, which may prevent that, is NOT DECODED). Deliberate deviation: the result is only valid -
       and only applied by callers - when every value is positive and finite. */
    if (!isfinite(r.feed) || !isfinite(r.plunge) || !isfinite(r.retract) || r.rpm == 0 || r.feed <= 0 || r.feed > 1e6) return r;
    r.ok = 1;
    return r;
}
