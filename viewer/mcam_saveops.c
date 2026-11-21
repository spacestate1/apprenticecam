/* mcam_saveops - write a Mastercam part (.mcam / .mcx-9) with this program's geometry and operations.
 * ../formats/mcam_write.md has the record layouts and rules; the summary:
 *  - the output is a base part plus inserted records: the open part itself, or for a new part a Mastercam part
 *    with its geometry, solids, operations and toolpaths removed (Mastercam's File > New starts from an empty
 *    database with the machine / tool / plane setup; see decomp/mastercam_filenew.c init_db);
 *  - new geometry (entities with no file id yet) becomes line / arc / point records;
 *  - each operation is a copy of a Mastercam operation of the same type and style (this part, the user's other parts
 *    of the same version, then Mastercam's operation defaults library - config 2303, the library Mastercam itself
 *    takes new operations from), with ids, links and our parameters patched; an op whose toolpath was computed here
 *    gets its NCI section (nci_write.c) and is written clean, any other is written dirty (red X): Mastercam regenerates
 *    the toolpath (../formats/nci_save.md);
 *  - chains are TP records (kind 6 header + kind 7 per entity), points kind 1, generated from the decoded layout;
 *  - the header record (system sub 0x0d) gets the counts, max id and shifted setup / NCI offsets.
 * Source files are only read. */
#include <pthread.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "paths.h"
#include "cam2d.h"
#include "mcam_write.h"
#include "mcam_saveops.h"
#include "mcam_grp.h"
#include "cam_postvals.h"
#include "part_tools.h"
#include "toollib.h"
#include "cam_fileop.h"
#include "mcsurf.h"
#include "sldtoolp.h"
#include "lathe_newop.h"
#include "sld_ops.h"
#include "occt_solid.h"
#include "nci_write.h"

static uint32_t rd32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t rd16(const uint8_t *b) { return b[0] | b[1] << 8; }
static int64_t rd64(const uint8_t *b) { return (int64_t)(rd32(b) | (uint64_t)rd32(b + 4) << 32); }
static void w32(uint8_t *b, uint32_t v) { b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }
static void w16(uint8_t *b, uint16_t v) { b[0] = v; b[1] = v >> 8; }
static void w64(uint8_t *b, uint64_t v) { w32(b, (uint32_t)v); w32(b + 4, (uint32_t)(v >> 32)); }
static void wd(uint8_t *b, double v) { memcpy(b, &v, 8); }
static double rdd(const uint8_t *b) { double v; memcpy(&v, b, 8); return v; }


/* Mastercam opcode (and 2D High Speed style at op +0x1228, -1 = any) for our op types; 0 = not exported */
static int id_in(const int32_t *a, int n, int32_t id) { for (int i = 0; i < n; i++) if (a[i] == id) return 1; return 0; }
int mcam_opcode_for(const CamParams *p, int *style) {
    *style = -1;
    switch (p->type) {
    case CAM_CONTOUR: return p->ct_cutoff_op ? 0x82 : 1;   /* 0x82 = Tab Cutoff (FUN_180044170: InitOperation(1) + opcode 0x82) */
    case CAM_DRILL: return 2;                 /* Canned Cycle */
    case CAM_POCKET: return 3;
    case CAM_CIRCLE: return 18;               /* Circle Mill */
    case CAM_THREAD: return 100;              /* Thread Mill */
    case CAM_FACE: return 102;                /* Facing */
    case CAM_SLOT: return 105;                /* Slot milling */
    case CAM_HELIX: return 106;               /* Helix Bore */
    case CAM_DYNAMIC: *style = 0; return 134; /* 2D High Speed: 0 Dynamic Mill, 1 Area, 2 Dynamic Contour, 3 Peel */
    case CAM_AREA: *style = 1; return 134;
    case CAM_DYNCONTOUR: *style = 2; return 134;
    case CAM_PEEL: *style = 3; return 134;
    case CAM_ENGRAVE: return 416;             /* Engrave (engrave.dll C-hook; the record copies the defaults library's Engrave op) */
    case CAM_OPTIROUGH: *style = 1; return 132;   /* 3drough: Surface High Speed, style byte 0x16bc = 1 (written with its references: save_opcode_for, S6) */
    default: return 0;
    }
}
static int uses_points(int type) { return type == CAM_DRILL || type == CAM_HELIX || type == CAM_CIRCLE || type == CAM_THREAD; }

const char *mcam_part_ext(int major) { return major >= 0x13 ? ".mcam" : ".mcx-9"; }

typedef struct { uint8_t *b; size_t n, cap; } Buf;
static void put(Buf *o, const uint8_t *p, size_t n) {
    if (o->n + n > o->cap) { o->cap = (o->n + n) * 2 + 4096; o->b = xrealloc(o->b, o->cap); }
    memcpy(o->b + o->n, p, n); o->n += n;
}

static double chain_area(const McamFile *f, const Chain *c) {   /* signed area in chain order, > 0 CCW */
    double *xy, z; int n = cam_chain_polyline(f, c, 0.01, &xy, &z), i; double a = 0;
    for (i = 0; i < n; i++) { int j = (i + 1) % n; a += xy[2 * i] * xy[2 * j + 1] - xy[2 * j] * xy[2 * i + 1]; }
    free(xy); return a / 2;
}

/* ------------------------------------------------------------------------------------------------ template parts */
/* a value of the user's Mastercam configuration (paths.conf user: CONFIG/mcamxm.config for a metric part, mcamx.config
   for an inch one - their 2303 entries name Mill_mm / Mill_Inch.mcam-defaults): the line "<key>. <label>? <value>" */
static const char *config_value(int metric, int key, char *out, size_t n) {
    char p[1100]; out[0] = 0;
    if (!site_file(p, sizeof p, "user", metric ? "CONFIG/mcamxm.config" : "CONFIG/mcamx.config")[0]) return out;
    FILE *f = fopen(p, "r"); if (!f) return out;
    char line[2048], want[16]; snprintf(want, sizeof want, "%d.", key);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, want, strlen(want))) continue;
        const char *q = strchr(line, '?'); if (!q) break;
        q++; while (*q == ' ') q++;
        snprintf(out, n, "%s", q); out[strcspn(out, "\r\n")] = 0;
        break;
    }
    fclose(f);
    return out;
}

const char *mcam_config_value(int metric, int key, char *out, size_t n) { return config_value(metric, key, out, n); }

/* ------------------------------------------------------------------------------------------------ system tolerances */
/* McSysTol mc_systol: defined in mcam_load.c (the test tools that link the loader without this file read it too) */
static int systol_units = -1;                       /* the units of the configuration last read (-1: none yet) */
/* mccore FUN_1803bc7d0 (decomp/mccore_systol.c l.1-29; DAT_1813eb30c = the units FUN_1803bd410 is given): tol 5e-5;
   metric chain_plane_tol 0.02, min_arc_length 0.02, nc_tol 0.002, chain_tol 0.002, curve_min_step 0.02, curve_max_step 100,
   curve_chord_dev 0.02, surf_step_chord_dev 0.02; inch nc_tol 0.0001, chain_tol 0.0001, min_arc_length 0.0002,
   chain_plane_tol 0.002, curve_min_step 0.001, curve_max_step 100, curve_chord_dev 0.001, surf_step_chord_dev 0.001
   (msf_join_tol 0.2 / 0.01 has no configuration item: not kept). gl_tolerance: mastercam.dll FUN_1801b4070
   (decomp/mastercam_gltol.c) IsMetric() ? 0.025 : 0.001. */
void mc_systol_defaults(int metric) {
    McSysTol *t = &mc_systol;
    t->tol = 5e-05;
    if (metric) { t->chain_plane_tol = 0.02; t->min_arc_length = 0.02; t->nc_tol = 0.002; t->chain_tol = 0.002; t->curve_min_step = 0.02;
                  t->curve_max_step = 100.0; t->curve_chord_dev = 0.02; t->surf_step_chord_dev = 0.02; t->gl_tolerance = 0.025; }
    else { t->nc_tol = 0.0001; t->chain_tol = 0.0001; t->min_arc_length = 0.0002; t->chain_plane_tol = 0.002; t->curve_min_step = 0.001;
           t->curve_max_step = 100.0; t->curve_chord_dev = 0.001; t->surf_step_chord_dev = 0.001; t->gl_tolerance = 0.001; }
}
/* ReadCfgFile's per-item setters for these keys: FUN_1801bab70 (mccore_systol.c l.109-141, section flag +0x1a) and
   FUN_1801ba530 (decomp/units/systol_readers.c, flag +0x17: 0x1b5c -> gl_tolerance_exref). The value is taken as read
   (no range check on this path; the Tolerances page FUN_1801aed10 bounds its edits to 1e-8 .. 10000) */
void mc_systol_item(int key, double v) {
    McSysTol *t = &mc_systol;
    switch (key) {
    case 1000: t->tol = v; break;
    case 1001: t->chain_tol = v; break;
    case 1002: t->chain_plane_tol = v; break;
    case 1003: t->min_arc_length = v; break;
    case 1004: t->curve_min_step = v; break;
    case 1005: t->curve_max_step = v; break;
    case 1006: t->curve_chord_dev = v; break;
    case 1007: t->surf_step_chord_dev = v; break;
    case 1008: t->nc_tol = v; break;
    case 7004: t->gl_tolerance = v; break;
    }
}
/* FUN_1801b6bd0 (decomp/units/core_cfgmisc.c l.185-209), run by ReadCfgFile after the file when the [TOLERANCES] section
   was read (FUN_1801bb2c0 l.275-277) */
void mc_systol_check(void) {
    McSysTol *t = &mc_systol;
    if (t->nc_tol < t->tol) t->nc_tol = t->tol;
    if (t->min_arc_length < t->tol) t->min_arc_length = t->tol;
    if (t->curve_min_step < t->tol) t->curve_min_step = t->tol;
    if (t->curve_chord_dev < t->tol) t->curve_chord_dev = t->tol;
    if (t->surf_step_chord_dev < t->tol) t->surf_step_chord_dev = t->tol;
    if (t->min_arc_length < t->nc_tol) t->min_arc_length = t->nc_tol;
}
/* Startup (mccore FUN_1803b9f90, core_cfgcallers.c l.427-560): FUN_1803bcb10(IsMetric()) = every section's defaults for the
   current units, then the current configuration is read. ChangeConfiguration (core_units.c l.3-155): SetUnits, then
   ReadCfgFile of the new current configuration - no defaults first (FUN_1803bd410's only callers are startup, the
   Configuration dialog's "Restore defaults" FUN_180174730 and FUN_18018d4d0), so an item the file lacks keeps the value
   it had. The file read is mcam_config_value's (CONFIG/mcamxm.config / mcamx.config in paths.conf "user"); none found:
   the values stay. */
static void systol_read(int metric) {
    static const int keys[] = { 1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 7004 };
    char v[64];
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        double d; if (sscanf(config_value(metric, keys[i], v, sizeof v), "%lf", &d) == 1) mc_systol_item(keys[i], d);
    }
    mc_systol_check();
}
const McSysTol *mc_systol_units(int metric) {
    metric = metric ? 1 : 0;
    if (systol_units != metric) {
        if (systol_units < 0) mc_systol_defaults(metric);
        systol_read(metric);
        systol_units = metric;
    }
    return &mc_systol;
}
int mc_systol_held(void) { return systol_units; }
double mc_systol_tol(void) { return mc_systol.tol; }                                   /* for mcce_dyn.c */
double mc_systol_chain_tol(int metric) { return mc_systol_units(metric)->chain_tol; }

/* Mastercam's operation defaults library (config 2303 "Mill Defaults Library": new operations take their settings from
   it, formats/ui.md) for the part's units; opened once per units. NULL if not configured / not readable. */
static McwFile dlib_[2]; static int dlib_tried[2], dlib_ok[2];
static pthread_mutex_t dlib_lock = PTHREAD_MUTEX_INITIALIZER;   /* ours: opened once under the lock (the Qt dialog's worker thread, cam_fileop.c) */
void mcam_defaults_library_reset(void) {   /* Q19 (ours): the defaults file was saved - read again */
    pthread_mutex_lock(&dlib_lock);
    for (int k = 0; k < 2; k++) { if (dlib_ok[k]) mcw_close(&dlib_[k]); dlib_tried[k] = dlib_ok[k] = 0; }
    pthread_mutex_unlock(&dlib_lock);
}
const McwFile *mcam_defaults_library(int metric) {
    McwFile *lib = dlib_; int *tried = dlib_tried, *ok = dlib_ok;
    int k = metric ? 1 : 0;
    pthread_mutex_lock(&dlib_lock);
    if (!tried[k]) {
        char v[1100], p[1100], rd[1100], err[200];   /* Q19: the program's saved copy of the file when there is one (paths.h) */
        if (config_value(k, 2303, v, sizeof v)[0] && mcw_open(&lib[k], defaults_read(host_path(v, p, sizeof p), rd, sizeof rd), err, sizeof err)) ok[k] = 1;
        tried[k] = 1;
    }
    const McwFile *r = ok[k] ? &lib[k] : NULL;
    pthread_mutex_unlock(&dlib_lock);
    return r;
}

/* kept for callers: the defaults library only (the user's other parts are no longer a source) */
int mcam_template_parts(McwFile *out, int max, const char *src) {
    (void)src;
    const McwFile *L = mcam_defaults_library(1);
    char err[200];
    return max > 0 && L && mcw_open(&out[0], L->path, err, sizeof err) ? 1 : 0;
}

/* a note's attributes changed here (core_ui cui_attr.c; notes take colour, level, width and point style - the line style's mask 0x1f078e
   has no N_ID / W_ID): only the fields the edit changed - the width (+0x10), and the two style fields (+0x18 / +0x1c) when a point style
   was set (kind 3 compares +0x1c and writes both, FUN_18046a420); stored values that were not edited stay as they are (a note's +0x18 and
   +0x1c differ in the corpus: CREATE_LINES.mcam 2 / 1) */
static void note_attr_patch(uint8_t *r, const Ent *en) {
    if (en->width >= 1 && en->width != rdd(r + 0x10)) wd(r + 0x10, en->width);
    if (en->ln_style != (int16_t)rd16(r + 0x18) || en->pt_style != (int16_t)rd16(r + 0x1c)) { w16(r + 0x18, (uint16_t)en->ln_style); w16(r + 0x1c, (uint16_t)en->pt_style); }
}
static int is_3d_opcode(int oc) { return oc == 5 || oc == 7 || oc == 10 || oc == 11 || oc == 12 || oc == 13 || oc == 15 || oc == 44 || oc == 109 || oc == 131 || oc == 132; }   /* save_opcode_for's 3D codes */
static int find_op(const McwFile *f, int opcode, int style) {
    for (int i = 0; i < f->nseg; i++) {
        const McwSeg *s = &f->seg[i];
        if (s->kind != SEG_ENT || s->cls != 0x4000 || s->sub != 1 || (is_3d_opcode(opcode) ? s->len < 4 + 0x2938 : s->len != 4 + 0x2938)) continue;   /* a 3D op: its record with its variable tail (opcodes 9, 10, 15, 0x83 pointer +0x12f8, 0x84 +0x1570: formats/mcam_write.md 3) */
        const uint8_t *e = f->d + s->off + 4;
        if (!(rd16(e + 4) & 1)) continue;                 /* deleted (lib_delete: selection bit 1 off; FUN_1802d3990 skips it) */
        /* the style: 2D High Speed (134) u16 +0x1228; Surface High Speed (132) the byte +0x16bc (op +0x1654, msurf_hmm_*_toolbar;
           cam_fileop.c default_op reads the same byte) */
        if (rd16(e + 0x6c) == opcode && (style < 0 || (opcode == 132 ? e[0x16bc] : rd16(e + 0x1228)) == style)) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------------------------------------ 3D toolpath references (S6) */
static void head(uint8_t *e, uint32_t cls, int assoc, int sub, int color, int level, int64_t eptr, int32_t now, int32_t idn, int major, int minor, int attrib);
/* Stage S6 of formats/surface_selection.md (section 6 "S6"): the .mcam records of a 3D op's drive / check references.
 * The op types: the Mastercam opcode (and Surface High Speed style, the byte +0x16bc) of each 3D port type - the codes
 * cam_fileop.c default_op takes the op of (formats/surface_selection.md 3); mcam_opcode_for keeps 0 for them (the dialogs
 * use it). */
static int is_3d_type(int t) {
    return t == CAM_OPTIROUGH || cam_hst_style(t) >= 0 || t == CAM_SRPOCKET || t == CAM_SRPARALLEL || t == CAM_SFPARALLEL || t == CAM_SFCONTOUR
        || t == CAM_SRPROJECT || t == CAM_BLEND || t == CAM_SRMPOCKET || t == CAM_SRPLUNGE || t == CAM_SFRADIAL || t == CAM_SFPROJECT || t == CAM_SFPENCIL;
}
static int save_opcode_for(const CamParams *p, int *style) {
    int oc = mcam_opcode_for(p, style); if (oc) return oc;
    int t = p->type; *style = -1;
    if (cam_hst_style(t) >= 0) { *style = cam_hst_style(t); return 132; }
    switch (t) {
    case CAM_SRPARALLEL: return 5; case CAM_SRPROJECT: return 7; case CAM_SRPOCKET: return 10; case CAM_SFPARALLEL: return 11;
    case CAM_SFRADIAL: return 12; case CAM_SFPROJECT: return 13; case CAM_SFCONTOUR: return 15; case CAM_SRPLUNGE: return 44;
    case CAM_BLEND: return 109; case CAM_SRMPOCKET: return 131; case CAM_SFPENCIL: return 39;
    default: return 0;
    }
}
int mcam_save_opcode_for(const CamParams *p, int *style) { return save_opcode_for(p, style); }
/* the entity a kind 4 record names: tp_surf_list (decomp/mcmsurf_all.c l.22463-22600) copies from the entity record +0x34 (idn) ->
   TP +0x6c, +0x30 (its creation stamp) -> +0x70, its class -> +0x78, +0x20 (eptr) -> +0x80. A surface of the part (made here: the
   ids the save just gave it), a solid by its SOLID_ID record in the base part (solids made / imported here are not written to the
   part: refused), anything else (mesh, CAD file) by its record in the base part. 0 + why when it cannot be named. */
typedef struct { int32_t idn, ctime; int64_t eptr; uint32_t cls; int seg; } RefEnt;
static int ref_entity(const McwFile *m, const McamFile *F, const CamRef *r, RefEnt *o, char *why, size_t wn) {
    memset(o, 0, sizeof *o); o->cls = r->cls; o->seg = -1;
    if (r->cls == TP_ECLS_SURF && r->index >= 0 && r->index < F->nsurfs) {
        const Surf *sf = &F->surfs[r->index]; const Ent *en = sf->ent >= 0 && sf->ent < F->nents ? &F->ents[sf->ent] : NULL;
        if (en && !en->type) { snprintf(why, wn, "drive / check surface %d was deleted", r->idn); return 0; }
        o->idn = sf->idn; o->eptr = sf->eptr;
        o->ctime = en && en->created ? en->ctime : sf->n >= 0x34 ? (int32_t)rd32(sf->rec + 0x30) : 0;
        return o->idn > 0;
    }
    int32_t idn = r->idn;
    if (r->cls == TP_ECLS_SOLID && r->index >= 0 && r->index < F->nsolids) idn = F->solids[r->index].idn;
    for (int i = 0; idn > 0 && i < m->nseg; i++)
        if (m->seg[i].kind == SEG_ENT && m->seg[i].cls == r->cls && m->seg[i].id == idn && m->seg[i].len >= 4 + 0x38) {
            const uint8_t *e = m->d + m->seg[i].off + 4;
            o->idn = idn; o->eptr = rd64(e + 0x20); o->ctime = (int32_t)rd32(e + 0x30); o->seg = i;
            for (int k = 0; k < F->nents; k++) if (F->ents[k].idn == idn && !F->ents[k].type && !F->ents[k].created) {   /* deleted here */
                snprintf(why, wn, "its %s %d was deleted", r->cls == TP_ECLS_SOLID ? "solid" : "entity", idn); return 0; }
            return 1;
        }
    snprintf(why, wn, r->cls == TP_ECLS_SOLID ? "solid %d is not in the Mastercam part (solids made or imported here are not written)"
                      : "entity %d (class 0x%x) is not in the Mastercam part", idn, r->cls);
    return 0;
}
/* a kind 4 record (formats/surface_selection.md 2.1): tp_surf_list's fields (above), +0x74 0x100 and +0x90 4 for every record
   (l.22520-22525: uStack_55e4 = 0x100, local_55c8 = 4), +0x92 0 (local_55c6), +0x98 u16 flags (0 drive, 4 check, 0x20 gouge; a
   mesh's 2: l.22545-22552), +0x9a i16 the solid's SldToolPOp link (sld_assign_toolp, l.22562), +0x68 op id / +0x88 op eptr
   (FUN_1802287f0's op record +0x68 / +0x20). Header: store_ent_assoc (decomp/mccore_tooladd.c l.7462-7483): class 0x4000, subtype 2,
   selection bits 1 | 0x100 (head's 0x0101), store_ent with the colour tp_linear_color (mccore 0x180034efd -> IAT 0x180ceb978
   ?tp_linear_color@@3EA, a configuration colour not read here: 0x9f as this writer's other TP records) and the level main_level
   (0x1812b5b60, ?main_level@@3HA; F->main_level), no attributes (store_ent copies them only for classes 0x19058f / 0x1870; 0 in all
   1409 kind 4 records of the 2017 corpus). A 0x398-byte (2018) record has every field from +0x98 on 0x10 later (2.1, measured). */
static void put_k4(Buf *tpb, int tpsz, const McamFile *F, const RefEnt *re, unsigned flags, int link, int32_t op_id, int64_t op_eptr,
                   int64_t eptr, int32_t now, int32_t idn, int major, int minor) {
    uint8_t tr[4 + 0x398]; memset(tr, 0, sizeof tr); w32(tr, (uint32_t)tpsz);
    uint8_t *t = tr + 4; int sh = tpsz == 0x398 ? 0x10 : 0;
    head(t, 0x4000, 1, 2, 0x9f, F->main_level > 0 ? F->main_level : 1, eptr, now, idn, major, minor, 0);
    w32(t + 0x68, (uint32_t)op_id); w32(t + 0x6c, (uint32_t)re->idn); w32(t + 0x70, (uint32_t)re->ctime); w32(t + 0x74, 0x100);
    w32(t + 0x78, re->cls); w64(t + 0x80, (uint64_t)re->eptr); w64(t + 0x88, (uint64_t)op_eptr); w16(t + 0x90, 4);
    w16(t + 0x98 + sh, (uint16_t)flags); w16(t + 0x9a + sh, (uint16_t)link);
    put(tpb, tr, 4 + (size_t)tpsz);
}
/* a whole-body SldToolPOp (ops-history type 0x18) appended to a solid's history for a solid picked here (link 0): formats/
   surface_selection.md 7.7. Made by sld_surfs_to_toolp_op -> FUN_1800b8880 (decomp/surfsel/mcgeomsld_surfsel_internals.c l.203-361):
   ctor FUN_18028c700 (on McSldEntOp FUN_1802739b0 on McOperation FUN_180261b90), +0x90 = 1 and +0x148 = the body's type for a whole
   body, +0xf0 / +0xf4 = 0, +0x18 = the solid's eptr, FUN_180265420(op, 0, 0), FUN_1802656b0(op, 0), FUN_180262b50 -> FUN_180267170
   (appended at the history position, opsfile +0x14, then +0x14 + 1); the link: sld_assign_toolp (A:3) -> FUN_18026a080 = the
   highest link of the solid's ops (slot 41), from McOpsFile +0xb0 (0 by the ctor FUN_180266d90; not read from the file), + 1.
   Written by slot 21 FUN_1802907e0 -> McSldEntOp FUN_180276400 -> McOperation FUN_180266560 (ops version >= 0x19, 64-bit):
     base       i32 +0x44 n1 = 0 (FUN_1802656b0(op, 0)), i32 +0x60 n2 = 0 (ctor), i32 +0x80 n3 = 0 (FUN_180265420(op, 0, 0)),
                u16 strlen + 1 = 1 and the name "" (ctor: calloc(1, 1)), u8 +9 / +0xa / +0xb / +0xc = 0, u32 +0x10 = 0 (ctor),
                u64 +0x18 solid eptr, u32 +0x34 op id (ctor: opsfile +0x14), u32 +0x30 type 0x18, u32 +0x3c version 0x1c,
                u8 +0x40 = 0, u8 +0x41 = 0x0c (ctor: u16 +0x40 = 0xc00)
     McSldEntOp u8 +0x90 = 1, i32 +0x94 = 0 faces (no face typed apart from the body: FUN_1800b8880 l.311-340), i32 +0xa8 = 0,
                u8 +0xc0 = 0, i32 +0xd0 = 0, i32 +0xe0 = 0 (ctor)
     SldToolPOp u64 +0xf0 = kind 0 | link << 32, i32 +0xf8 / +0x108 / +0x118 / +0x130 = 0 (ctor), u16 +0x148 type, i32 +0x14c = 0,
                i32 +0x158 = 0 (ops version > 0xb; ctor)
   = 97 bytes, the size of every whole-body SldToolPOp of the corpus (Benchmark 3_0 for 2017 ops 99-102: the same bytes but for the
   op id, the link, the type and the op version their Mastercam release wrote). */
#define SLDTP_WHOLE_N 97
static void sldtoolp_whole_bytes(uint8_t *b, int64_t solid_eptr, int op_id, int link, int type) {
    memset(b, 0, SLDTP_WHOLE_N);
    w16(b + 0x0c, 1);                                  /* name length (b + 0x0e: the name's NUL) */
    w64(b + 0x17, (uint64_t)solid_eptr); w32(b + 0x1f, (uint32_t)op_id); w32(b + 0x23, 0x18); w32(b + 0x27, 0x1c); b[0x2c] = 0x0c;
    b[0x2d] = 1;                                       /* +0x90 whole body; then +0x94, +0xa8, +0xc0, +0xd0, +0xe0 = 0 */
    w32(b + 0x3f, 0); w32(b + 0x43, (uint32_t)link);   /* +0xf0 kind 0, +0xf4 link */
    w16(b + 0x57, (uint16_t)type);                     /* +0x148 */
}
/* the solids that get a new SldToolPOp in this save */
typedef struct { int solid; int link; int type; int op_id; } NewSldTp;
/* the next link id of solid s (FUN_18026a080 over its ops and the ones this save adds) and the new op's id; 0 + why when the op cannot
   be added as decoded */
static int sld_new_link(const Solid *s, int si, const NewSldTp *nw, int nnw, int *link, int *op_id, char *why, size_t wn) {
    if (!s->ops_raw || !s->hist_ok) { snprintf(why, wn, "its history (ops block) is not readable here"); return 0; }
    if (s->pos != s->nops) { snprintf(why, wn, "its history is rolled back (position %d of %d ops): inserting the toolpath op there renumbers the later ops' records (FUN_180267170) - NOT PORTED", s->pos, s->nops); return 0; }
    int mx = 0, added = 0;                             /* McOpsFile +0xb0: 0 (ctor FUN_180266d90) */
    for (int k = 0; k < s->nops; k++) {
        if (s->ops[k].type == 0x1b || s->ops[k].type == 0x1c) {   /* FBM Drill / Lathe Toolpath: their own slot 41 (FUN_18027dc60 / FUN_18027f580) */
            snprintf(why, wn, "its history has an FBM / lathe toolpath op whose link (slot 41) is NOT DECODED"); return 0; }
        if (s->ops[k].type != 0x18) continue;
        SldToolP t; char e[128] = "";
        int r = sldtoolp_at(s, k, &t, e);
        if (r < 0) { snprintf(why, wn, "its SldToolPOp %d does not parse: %s", k + 1, e); return 0; }
        if (r > 0 && t.link > mx) mx = t.link;
        if (r > 0) sldtoolp_free(&t);
    }
    for (int k = 0; k < nnw; k++) if (nw[k].solid == si) { if (nw[k].link > mx) mx = nw[k].link; added++; }
    *link = mx + 1; *op_id = s->pos + added;
    return 1;
}

/* ------------------------------------------------------------------------------------------------ record builders */
/* the version stamp the base part's writer put on its records: the raw writer FUN_1802c72c0 (decomp/mccore_tooladd.c)
   sets ent +0x48 / +0x49 / +0x4b..+0x4e of every record from one set of globals (DAT_18143d54a..550); taken from the
   base's header record (sub 0x0d) */
static uint8_t g_vstamp[7];
/* the common 0x68-byte header */
static void head(uint8_t *e, uint32_t cls, int assoc, int sub, int color, int level, int64_t eptr, int32_t now, int32_t idn, int major, int minor, int attrib) {
    w32(e, cls);
    /* selection bits (u16 +4): ASSOC records need bit 1 to be processed (FUN_1802cf2a0 sel_bit_is_on(ent, 1)) and save_db
       sets 1 and 0x100 (formats/mcam_write.md 1); geometry: 0x81, the bits every point / line / arc / solid record of the
       16 user parts and new-plate.mcam has (the meaning of 0x80 is NOT DECODED) */
    e[4] = assoc ? 0x01 : 0x81; e[5] = assoc ? 0x01 : 0x00;
    e[6] = (uint8_t)color; w32(e + 8, (uint32_t)level);
    if (attrib) { wd(e + 0x10, 1.0); w16(e + 0x18, 1); w16(e + 0x1a, 1); w16(e + 0x1c, 1); }
    w64(e + 0x20, (uint64_t)eptr);
    if (assoc) w32(e + 0x2c, (uint32_t)sub);
    w32(e + 0x30, (uint32_t)now); w32(e + 0x34, (uint32_t)idn);
    memcpy(e + 0x48, g_vstamp, 7); e[0x48] = (uint8_t)major; e[0x49] = (uint8_t)minor;
    /* +0x4a is not stamped by the writer; mccore only tests it for 2 (wire: bytes_per_ent NCI_ID, the TP kind 7 / 0x2a
       reader, decomp/mccore_sections.c / mccore_dbread.c), so 0 */
    e[0x4a] = 0;
}

/* ------------------------------------------------------------------------------------------------ base for a new part */
/* File > New (init_db, decomp/mastercam_filenew.c) empties the database (dbinit): no geometry, operations, tools or
   machine group. A toolpath needs a machine group: the default one is config 25000 "Mill Machine Definition"
   (formats/ui.md). The base is the operation defaults library (config 2303), whose machine group is that machine (checked
   by name below), with its geometry, solids, operations (OP 1), toolpath references (TP 2), tools (TL 3), NCI_ID (5),
   the operations' holder records (ASSOC 31, op +0x1210 = their ent_idn) and NCI removed, its TlMgr archive emptied and
   the header's counts / offsets fixed. Kept: system records, planes, levels, the machine group / setup / machine /
   control records. */
/* a new surface's references to entities made here (they get their file eptr in the same pass, in entity order, so a referenced
   entity - made before the surface - already has it): the eptr fields of the kinds the Surfaces commands build, each matched
   against the session eptr (Ent.eptr before the save = mcam_new_surf / cui_surf.c's session value, kept in Ent.septr) */
/* surface curves (E_SCRV, formats/wireframe.md "Curve Surface") and their surfaces' reference counts: put_cons_in_db (mccore 0x1802da530,
   decomp/curvesurf/cs2.c 171) adds 1 to the surface's ent +0x28 (put_special_undo + rewrite_ent) and inc_refs_eptr 1 to the uv curve's;
   a surface's +0x28 written here = as read + the surface curves made here on it - the part's surface curves deleted here on it. Ours: the
   decrement on a deletion is inferred (the delete of a surface curve was not decoded). eptr = the surface's file eptr or session eptr. */
static int cons_ref_delta(const McamFile *F, int64_t eptr) {
    int d = 0;
    for (int k = 0; k < F->nents; k++) {
        const Ent *en = &F->ents[k];
        if (en->cls != 0x400 || en->crv < 0 || en->crv >= F->ncrvs || F->crvs[en->crv].n < 0xd8) continue;
        if ((int64_t)rd64(F->crvs[en->crv].rec + 0xd0) != eptr) continue;
        if (en->created && en->type == E_SCRV) d++;
        else if (!en->created && !en->type) d--;
    }
    return d;
}
static void surf_remap_refs(McamFile *F, uint8_t *e, uint32_t n, int self) {
    uint32_t offs[8]; int no = mcam_surf_ref_offsets(e, n, offs);
    for (int r = 0; r < no; r++) {
        int64_t v = (int64_t)rd64(e + offs[r]);
        for (int k = 0; k < F->nents; k++) if (k != self && F->ents[k].created && F->ents[k].septr && F->ents[k].septr == v) { w64(e + offs[r], (uint64_t)F->ents[k].eptr); break; }
    }
}

static int new_base(McwFile *m, int metric, char *msg, size_t msgn, char *note, size_t noten) {
    const McwFile *b = mcam_defaults_library(metric);
    if (!b) { snprintf(msg, msgn, "no operation defaults library (config 2303 in the user's %s) to start a new part from", metric ? "mcamxm.config" : "mcamx.config"); return 0; }
    {   /* the library's machine group = config 25000 ? (op_group +0x355 machine definition, compared by file name) */
        char want[1100]; config_value(metric, 25000, want, sizeof want);
        const char *wn = strrchr(want, '\\'); wn = wn ? wn + 1 : want;
        int found = 0;
        for (int i = 0; i + 1 < b->nseg && !found; i++)
            if (b->seg[i].kind == SEG_ENT && b->seg[i].cls == 0x200 && b->seg[i].sub == 0x0f && b->seg[i + 1].kind == SEG_OOB && b->seg[i + 1].len >= 4 + 0x1ae0) {
                const uint8_t *g = b->d + b->seg[i + 1].off + 4;   /* first group record: the machine group */
                char mm[0x106]; memcpy(mm, g + 0x355, 0x105); mm[0x105] = 0;
                const char *mn = strrchr(mm, '\\'); mn = mn ? mn + 1 : mm;
                found = 1;
                if (wn[0] && strcasecmp(mn, wn)) snprintf(note, noten, "the defaults library's machine (%s) is not config 25000's (%s)", mn, wn);
            }
    }
    char *keep = xcalloc(b->nseg, 1); int hdr = -1, tlm = -1;
    for (int i = 0; i < b->nseg; i++) {
        const McwSeg *s = &b->seg[i];
        int drop = s->kind == SEG_NCI || s->kind == SEG_SOLID_TAIL ||
                   (s->kind == SEG_ENT && s->cls != 0x200 && s->cls != 0x4000) ||                      /* geometry, solids */
                   (s->kind == SEG_ENT && s->cls == 0x4000 && (s->sub == 1 || s->sub == 2 || s->sub == 3 || s->sub == 5 || s->sub == 31));
        keep[i] = !drop;
        if (s->kind == SEG_ENT && s->cls == 0x200 && s->sub == 0x0d && s->len >= 4 + 0x80) hdr = i;   /* written up to +0x7f */
        if (s->kind == SEG_ENT && s->cls == 0x4000 && s->sub == 33 && s->len >= 4 + 0x378 + 11 && b->d[s->off + 4 + 0x56]
            && !memcmp(b->d + s->off + 4 + 0x378, "\xff\xff\x05\x00\x05\x00TlMgr", 11)) tlm = i;
    }
    /* the emptied TlMgr: the record's first 0x378 bytes, then the archive TlMgr::DeserializeV5 reads (part_tools.md 1):
       the class tag and three empty lists (items, slot table, lathe slot table) */
    McwInsert ins = { 0, NULL, 0 }; uint8_t *tlrec = NULL;
    if (tlm >= 0) {
        static const uint8_t ar[] = { 0xff, 0xff, 5, 0, 5, 0, 'T', 'l', 'M', 'g', 'r', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        size_t L = 4 + 0x378 + sizeof ar; tlrec = xmalloc(L);
        memcpy(tlrec, b->d + b->seg[tlm].off, 4 + 0x378); memcpy(tlrec + 4 + 0x378, ar, sizeof ar); w32(tlrec, (uint32_t)(L - 4));
        keep[tlm] = 0; ins.before = tlm; ins.bytes = tlrec; ins.len = L;
    }
    size_t n; uint8_t *d = mcw_build(b, keep, tlrec ? &ins : NULL, tlrec ? 1 : 0, &n);
    if (hdr >= 0) {                                                      /* header: no ops / refs / NCI; setup moved */
        size_t hoff = 0; for (int i = 0; i < hdr; i++) if (keep[i]) hoff += b->seg[i].len;
        uint8_t *H = d + hoff + 4;
        uint32_t setup = rd32(H + 0x74), shift = 0;
        for (int i = 0; i < b->nseg; i++) if (!keep[i] && b->seg[i].off < setup) shift += (uint32_t)b->seg[i].len;
        if (tlrec && b->seg[tlm].off < setup) shift -= (uint32_t)ins.len;
        w32(H + 0x6c, 0); w32(H + 0x70, 0); if (setup) w32(H + 0x74, setup - shift); w32(H + 0x78, 0xffffffffu);
    }
    free(keep); free(tlrec);
    char err[200];
    if (!mcw_open_mem(m, d, n, err, sizeof err)) { snprintf(msg, msgn, "%s", err); return 0; }
    snprintf(m->path, sizeof m->path, "%s", b->path);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ tools */
/* An operation names its tool by TlMgr slot (op +0x544 = record +0x5ac, part_tools.md 1.1) and carries the tool's
   op_tool_info block at +0x348 (0x288 bytes up to +0x5d0; in 189/189 user ops byte-identical to the tool's TL_ID body
   but for the op's feed / plunge / retract / rpm and bytes after string NULs). On load (decomp/mccore_tooladd.c):
   ProcessAssocEntity (FUN_1802cf2a0) case 3 collects every TL_ID body (tp_tool, 0x698 bytes) and the post-load
   FUN_1802ce370 calls TlMgr::Add(tp_tool) for each one whose slot the tool system does not have (vfunc +0x200, the
   check bnci_restore reports as "Mill tool does not exist"): TlToolMill::CreateNewFromOldTool + SetupNewAssembly
   build the tool and its assembly. So a tool the part does not have is written as a TL_ID record with a free slot. */
typedef struct { int slot, num; char name[0x78]; } PartSlot;
static void add_slot(PartSlot **ps, int *n, int slot, int num, const char *name) {
    *ps = xrealloc(*ps, sizeof **ps * (*n + 1)); (*ps)[*n].slot = slot; (*ps)[*n].num = num;
    snprintf((*ps)[*n].name, sizeof (*ps)[*n].name, "%s", name); (*n)++;
}
/* the base part's tools: TL_ID records (ASSOC 3: ent +0x2cc slot, +0x68 tool #, +0xbe name) and the TlMgr archive's
   slot table (ASSOC 33, part_tools_list) */
static int part_slots(const McwFile *m, PartSlot **ps) {
    int n = 0; *ps = NULL;
    for (int i = 0; i < m->nseg; i++) {
        const McwSeg *g = &m->seg[i]; if (g->kind != SEG_ENT || g->cls != 0x4000) continue;
        const uint8_t *e = m->d + g->off + 4; size_t len = g->len - 4;
        if (g->sub == 3 && len >= 0x2d0) { char nm[0x79]; memcpy(nm, e + 0xbe, 0x78); nm[0x78] = 0; add_slot(ps, &n, (int32_t)rd32(e + 0x2cc), (int32_t)rd32(e + 0x68), nm); }
        if (g->sub == 33 && len > 0x56 && e[0x56]) {
            Tool *t; int k = part_tools_list(e, len, &t);
            for (int j = 0; j < k; j++) { add_slot(ps, &n, t[j].slot, t[j].num, t[j].name); free(t[j].holder_rz); }
            if (k >= 0) free(t);
        }
    }
    return n;
}
/* the tool that owns a slot of the part: the first of F's tools with that slot, when the base part has the slot (its
   TL_ID / TlMgr entry). A tool edited here keeps its slot and stays the part's tool, whatever its number or name now;
   another tool with the same slot (a copy made here) is a new tool */
static int slot_owner(const McamFile *F, const PartSlot *ps, int n, const Tool *t) {
    if (!t || t->slot <= 0) return 0;
    int in = 0; for (int i = 0; i < n && !in; i++) in = ps[i].slot == t->slot;
    if (!in) return 0;
    for (int k = 0; k < F->ntools; k++) if (F->tools[k].slot == t->slot) return &F->tools[k] == t;
    return 0;
}

/* ------------------------------------------------------------------------------------------------ part tools edited
   A part tool changed here is written back where Mastercam keeps it (formats/part_tools.md 10, mcam_write.md 8):
   - the TlMgr archive (ASSOC 33): part_tools_write rebuilds it (TlMgr::Serialize's layout, every unedited byte kept)
     with the changed fields in the tool's objects, its op_tool_info block (TlTool level) and its assembly;
   - its TL_ID record (body = tp_tool, TlTool::SetToOldTool) and the op_tool_info of the operations written here:
     the original bytes, except those that toollib.c tool_tp_tool / tool_op_info give differently for the edited and
     the loaded tool (bytes those builders do not reproduce - legacy holder size +0x2b0, +0x3c8, +0x408, an op's own
     values - stay as stored).
   BASE = the part as loaded (mcam_load of the source), paired with F's tools by slot (slot_owner). */
typedef struct { const Tool *now; Tool base; Tool nb, bb; uint8_t ob_now[0x288], ob_base[0x288]; int res, tl_changed; } TEdit;
static void edit_prep(TEdit *x) {                  /* both tools built from the loaded block and slot */
    x->nb = *x->now; x->bb = x->base;
    memcpy(x->nb.op_info, x->base.op_info, 0x288); x->nb.has_op_info = x->bb.has_op_info = x->base.has_op_info;
    x->nb.slot = x->base.slot;
    tool_op_info(&x->nb, x->ob_now); tool_op_info(&x->bb, x->ob_base);
    uint8_t a[0x698], b[0x698]; tool_tp_tool(&x->nb, a); tool_tp_tool(&x->bb, b);
    x->tl_changed = memcmp(a, b, 0x698) != 0;
}
static void merge_edit(uint8_t *dst, size_t n, const uint8_t *now, const uint8_t *base) {   /* the bytes the edit changes */
    for (size_t k = 0; k < n; k++) if (now[k] != base[k]) dst[k] = now[k];
}
static void edit_tl_body(TEdit *x, uint8_t *body, size_t n) {       /* a TL_ID body (ent +0x68), tp_tool */
    uint8_t a[0x698], b[0x698]; tool_tp_tool(&x->nb, a); tool_tp_tool(&x->bb, b);
    merge_edit(body, n < 0x698 ? n : 0x698, a, b);
}

/* TpToolList::OpenSlot (mccore 0x1808f4df0): the smallest slot >= 1 not in the TlMgr slot map; GetOpenSlot
   (0x1808f1100) also skips slots a TL_ID record holds (VerifyOpenSlot 0x1808f1a60) */
static int open_slot(const PartSlot *ps, int n) {
    for (int s = 1;; s++) { int used = 0; for (int i = 0; i < n && !used; i++) used = ps[i].slot == s; if (!used) return s; }
}

/* ------------------------------------------------------------------------------------------------ export */
/* where byte `pos` of the source ends up: inserted blocks at or before it added, left-out segments before it removed */
static size_t new_pos(const McwFile *m, const char *keep, const McwInsert *ins, int nins, size_t pos) {
    size_t p = pos;
    for (int k = 0; k < nins; k++) { size_t at = ins[k].before < m->nseg ? m->seg[ins[k].before].off : m->n; if (at <= pos) p += ins[k].len; }
    for (int i = 0; i < m->nseg; i++) if (!keep[i] && m->seg[i].off < pos) p -= m->seg[i].len;
    return p;
}

/* the output offset of insertion K (mcw_build's order: before segment i its insertions in list order, then the segment if kept) */
static size_t ins_out_off(const McwFile *m, const char *keep, const McwInsert *ins, int nins, int K) {
    size_t q = 0;
    for (int i = 0; i <= m->nseg; i++) {
        for (int k = 0; k < nins; k++) if (ins[k].before == i) { if (k == K) return q; q += ins[k].len; }
        if (i < m->nseg && keep[i]) q += m->seg[i].len;
    }
    return q;
}

/* a stored op of F with id ID whose NCI was rewritten here and that is not replaced by an edited version */
static const Op *rev_op(const McamFile *F, int32_t id, const int32_t *dropped, int ndropped) {
    if (id_in(dropped, ndropped, id)) return NULL;
    for (int k = 0; k < F->nops; k++) if (F->ops[k].id == id && F->ops[k].nci) return &F->ops[k];
    return NULL;
}

/* an op's heights into its record E (ent bytes; formats/mcam_write.md): read back as cam_fileop.c params_from_record does
   (P10027 top +0x2e8 / inc +0x2f0 relative to the chain Z; P10029 depth +0x1a0 / inc +0x1a8 relative to the chain Z, plus
   P10068 stock +0x888; P10024 feed plane +0x190 / inc +0x198 relative to the top; clearance +0x170 / inc +0x178 / on +0x179
   and retract +0x180 / +0x188 / +0x189 relative to the top); a value whose stored form already gives ours is kept (abs / inc
   as stored), else it is written absolute (feed plane: incremental). Used by the save and by cui_grpprops.c's modal copy. */
static void heights_to_record(const McamFile *F, const CamOp *op, uint8_t *e) {
    const CamParams *p = &op->p; int pts = uses_points(p->type);
        {   double cz = 0;
        if (op->nchains && op->chains[0].n) { const Ent *e0 = &F->ents[op->chains[0].ent[0]]; cz = e0->type == E_ARC ? e0->c.z : e0->a.z; }
        else if (pts && op->nholes) for (int i = 0; i < F->nents; i++) { const Ent *en = &F->ents[i];
            if (en->type == E_ARC && fabs(en->c.x - op->holes[0].x) < 1e-6 && fabs(en->c.y - op->holes[0].y) < 1e-6) { cz = en->c.z; break; }
            if (en->type == E_POINT && fabs(en->a.x - op->holes[0].x) < 1e-6 && fabs(en->a.y - op->holes[0].y) < 1e-6) { cz = en->a.z; break; } }
        #define SAME(a_, b_) (fabs((a_) - (b_)) <= 1e-9 * (1 + fabs(b_)))
        int chamfer = p->type == CAM_CONTOUR && p->contour_type == 1;
        if (p->type == CAM_THREAD) {
            /* Thread Mill's own heights (cam_fileop.c, mcmill FUN_1802374d0): clearance 0x1230, feed plane 0x1238, top
               0x1240, bottom 0x1248, the last three from the point's Z when S16 0x1250 = 1; the common fields are kept */
            int16_t thinc = (int16_t)rd16(e + 0x1250);
            for (int k = 0; k < p->nrpatch; k++) if (p->rpatch[k].off == 0x1250 && p->rpatch[k].n == 2) memcpy(&thinc, p->rpatch[k].v, 2);   /* batch 8: the
                                                    Linking 2837 radio's patch (written after this) decides how the heights are kept */
            double zc = thinc == 1 ? cz : 0;
            if (!SAME(rdd(e + 0x1240) + zc, p->top_z)) wd(e + 0x1240, p->top_z - zc);
            if (!SAME(rdd(e + 0x1248) + zc, p->final_z)) wd(e + 0x1248, p->final_z - zc);
            if (!SAME(rdd(e + 0x1230), p->clearance)) wd(e + 0x1230, p->clearance);
            if (!SAME(rdd(e + 0x1238) - rdd(e + 0x1240), p->feed_plane)) wd(e + 0x1238, rdd(e + 0x1240) + p->feed_plane);
        } else {
        double zstock = rdd(e + 0x888);
        if (!isfinite(zstock)) zstock = 0;
        double top0 = rdd(e + 0x2e8) + (rd32(e + 0x2f0) ? cz : 0);
        double fin0 = (e[0x1a8] ? cz + rdd(e + 0x1a0) : rdd(e + 0x1a0)) + zstock;
        double fp0 = e[0x198] ? rdd(e + 0x190) : rdd(e + 0x190) - top0;
        /* 2D chamfer: the reader moves the top to the final Z and the feed plane up by the difference */
        int same_ch = chamfer && SAME(fin0, p->top_z) && SAME(fp0 + top0 - fin0, p->feed_plane);
        double top = chamfer && same_ch ? top0 : p->top_z;
        if (!chamfer ? !SAME(top0, p->top_z) : !same_ch) { wd(e + 0x2e8, p->top_z); w32(e + 0x2f0, 0); top0 = p->top_z; fp0 = e[0x198] ? rdd(e + 0x190) : rdd(e + 0x190) - top0; }
        double ret0 = rdd(e + 0x180) + (e[0x188] ? top0 : 0), clr0 = rdd(e + 0x170) + (e[0x178] ? top0 : 0);
        if (p->retract_z > -1e29) { if (!e[0x189] || !SAME(ret0, p->retract_z)) { wd(e + 0x180, p->retract_z); e[0x188] = 0; } e[0x189] = 1; }
        else e[0x189] = 0;                                          /* off: the stored value / abs-inc kept */
        /* clearance off reads as the retract (on) or top + feed plane: kept off when that is ours */
        /* regen-consistency 2026-11-19: a drill op's feed plane field 0x190 holds the R plane (written below), and the reader's clearance-off
           value is top + its feed plane = the R plane (cam_fileop.c params_from_record) - not top + CamParams.feed_plane */
        double clr_off = e[0x189] ? (p->retract_z > -1e29 ? p->retract_z : ret0) : p->type == CAM_DRILL ? p->r_plane : top + p->feed_plane;
        if (e[0x179] ? !SAME(clr0, p->clearance) : !SAME(clr_off, p->clearance)) { wd(e + 0x170, p->clearance); e[0x178] = 0; e[0x179] = 1; }
        if (p->type == CAM_DRILL) {                                 /* the R plane (p->r_plane absolute) */
            double r0 = rdd(e + 0x190) + (rd32(e + 0x198) ? top0 : 0);
            if (!SAME(r0, p->r_plane)) { wd(e + 0x190, p->r_plane); e[0x198] = 0; }
        } else if (!chamfer) {
            if (!SAME(fp0, p->feed_plane)) { wd(e + 0x190, p->feed_plane); e[0x198] = 1; }
        } else if (!same_ch) { wd(e + 0x190, p->feed_plane); e[0x198] = 1; }
        /* depth: final Z = depth (+ chain Z when incremental) + stock; the stock is kept, the depth written absolute */
        if (chamfer) { if (!same_ch) { wd(e + 0x888, 0); wd(e + 0x1a0, p->top_z); e[0x1a8] = 0; } }
        else if (!SAME(fin0, p->final_z)) { wd(e + 0x1a0, p->final_z - zstock); e[0x1a8] = 0; }
        }
        #undef SAME
    }
}
/* the height bytes a CamOp is saved with: BASE (its own stored record / the defaults op it is made from) through
   heights_to_record, then its record patches (written last, as the save does) - into OUT (N bytes) */
void mcam_op_record_heights(const McamFile *F, const CamOp *op, const uint8_t *base, uint32_t n, uint8_t *out) {
    memcpy(out, base, n);
    if (n >= 0x1250 + 2) heights_to_record(F, op, out);
    for (int k = 0; k < op->p.nrpatch; k++)
        if ((uint32_t)op->p.rpatch[k].off + op->p.rpatch[k].n <= n) memcpy(out + op->p.rpatch[k].off, op->p.rpatch[k].v, op->p.rpatch[k].n);
}

/* the Stock Setup edited here (formats/mcam_write.md "Stock Setup"): WriteFileGroups writes each op_group as its 0x1ae0
   struct, so the machine group's group_pg3 fields are patched in place in the active machine group's record (the one
   the loader read them from, mcam_load.c): +0x1228 Fit screen, +0x1230.. size, +0x1248.. origin, +0x1260 Display,
   +0x1261 shaded, +0x1264 shape, +0x126c axis, +0x1270 centre on axis, +0x1690 corner, the cylinder's end circles
   (+0x15c0 / +0x1628), and the 12 lines of stock_calc_geom at +0x1380 (what the dialog's OK
   leaves there: stock_ok(1) recomputes them, mccore 0x1800d5d80), the solid id (+0x1268), the STL file (+0x1271, when it
   changed) and the stock plane (+0x8a8, when chosen here). Only when changed here: an untouched part keeps its bytes. */
/* a stored Mastercam op marked by the Stock Setup's regen rule (cui_stock.c stock_regen_mark) */
/* dirty ops (formats/opdlg_port_audit.md "Dirty operations"): also a stored op whose tool was changed here (Op.stale; FUN_180a45440 /
   EditSelectedMillTool write record +0xfc = 1 for it, decomp/opman_tree_all.c l.16422-16425) */
static int regen_op(const McamFile *F, int32_t id) { for (int i = 0; i < F->nops; i++) if (F->ops[i].id == id && (F->ops[i].regen || F->ops[i].stale)) return 1; return 0; }
static void stock_patch(McwFile *m, const McamFile *F) {
    for (int i = 0; i + 1 < m->nseg; i++)
        if (m->seg[i].kind == SEG_ENT && m->seg[i].cls == 0x200 && m->seg[i].sub == 0x0f && m->seg[i + 1].kind == SEG_OOB && m->seg[i + 1].len >= 4 + 0x1ae0) {
            uint8_t *g = m->d + m->seg[i + 1].off + 4;             /* the machine group the loader read (F->stock_grp, the active one) */
            const uint8_t *gr[256]; int ng = 0; mcam_grp_block(m->d + m->seg[i + 1].off, m->seg[i + 1].len, gr, 256, &ng);   /* + member entries (mcam_grp.h) */
            for (int k = 0; k < ng && k < 256; k++)
                if ((int32_t)rd32(gr[k]) == F->stock_grp) g = (uint8_t *)gr[k];
            g[0x1228] = (uint8_t)(F->stock_fit != 0);
            for (int k = 0; k < 3; k++) { wd(g + 0x1230 + 8 * k, F->stock[k]); wd(g + 0x1248 + 8 * k, F->stock_org[k]); }
            g[0x1260] = (uint8_t)(F->stock_disp != 0); g[0x1261] = (uint8_t)(F->stock_shaded != 0);
            w32(g + 0x1264, (uint32_t)F->stock_shape); w16(g + 0x1690, (uint16_t)F->stock_sel);
            w32(g + 0x126c, (uint32_t)F->stock_axis); g[0x1270] = (uint8_t)(F->stock_center != 0);
            w32(g + 0x1268, (uint32_t)F->stock_solid);           /* the solid's id (FUN_1801ed0b0 from +0x10338) */
            if (strncmp((const char *)g + 0x1271, F->stock_file, 0x104)) {   /* the STL file (+0x1271, 0x104 chars): only when changed */
                memset(g + 0x1271, 0, 0x104); memcpy(g + 0x1271, F->stock_file, strnlen(F->stock_file, 0x103)); }
            if (F->stock_plane_dirty) {   /* the stock plane chosen here: Plane::Convert(group_view) - zeroed 0x68 bytes, i16 plane id, i16 view
                                             number, +4 i16 the plane's +0x348 (0 here, cui_stock.c st_plane_selected), +8 matrix rows, +0x50 origin */
                memset(g + 0x8a8, 0, 0x68); w16(g + 0x8a8, (uint16_t)F->stock_vid); w16(g + 0x8aa, (uint16_t)F->stock_vno);
                for (int k = 0; k < 9; k++) wd(g + 0x8b0 + 8 * k, F->stock_vm[k]);
                for (int k = 0; k < 3; k++) wd(g + 0x8f8 + 8 * k, F->stock_vo[k]);
            }
            int rect = F->stock_shape == 0 && F->stock[0] > 0 && F->stock[1] > 0 && F->stock[2] > 0;
            int cyl = F->stock_shape == 1 && F->stock[0] > 0 && F->stock[1] > 0 && F->stock_arc_view > 0;
            if (rect || cyl)
                for (int l = 0; l < 12; l++) for (int k = 0; k < 6; k++) wd(g + 0x1380 + 0x30 * l + 8 * k, F->stock_lines[l][k]);
            if (cyl)   /* the end circles, a_3d (0x68 bytes) as stock_calc_geom fills them: first 0x30 bytes 0, centre (axis view
                          coordinates) +0x30, radius +0x48, start 0 +0x50, sweep 2 pi +0x58, view number (i16) +0x60 */
                for (int a = 0; a < 2; a++) {
                    uint8_t *c = g + 0x15c0 + 0x68 * a;
                    memset(c, 0, 0x68);
                    for (int k = 0; k < 3; k++) wd(c + 0x30 + 8 * k, F->stock_arc[a][k]);
                    wd(c + 0x48, F->stock_arc_r); wd(c + 0x58, 2 * M_PI); w16(c + 0x60, (uint16_t)F->stock_arc_view);
                }
            return;
        }
}

/* the part solids' description / level / colour edited here (Analyze's solid dialog, core_ui az_start_solid): patched in place -
   the name after the solid id in the SOLIDXMT trailer (0x40 bytes, 0x100 from solid version 0x1c: sld_save_solids / FUN_1800c6d20,
   formats/solid_history.md 1.2; sld_set_description keeps at most 0x100 bytes, the shorter field takes 0x3f + the terminator) and
   the SOLID_ID record's colour (+0x06) and level (+0x08) */
static void solid_patch(McwFile *m, const McamFile *F) {
    for (int k = 0; k < F->nsolids; k++) {
        const Solid *so = &F->solids[k]; if (!so->edited) continue;
        for (int i = 0; i + 1 < m->nseg; i++) {
            const McwSeg *x = &m->seg[i], *t = &m->seg[i + 1];
            if (x->kind != SEG_ENT || x->cls != 0x20000 || x->len < 4 + 0x16c || t->kind != SEG_SOLID_TAIL) continue;
            const uint8_t *e = m->d + x->off + 4;
            if (strncmp((const char *)e + 0x68, so->name, 0x100)) continue;
            size_t nl = e[0x49] > 0x1b ? 0x100 : 0x40, at = rd32(e + 0x168) + 4;
            if (at + nl > t->len) break;
            uint8_t *nm = m->d + t->off + at;
            memset(nm, 0, nl); memcpy(nm, so->desc, strnlen(so->desc, nl - 1));
            break;
        }
        for (int i = 0; i < m->nseg; i++) {
            const McwSeg *x = &m->seg[i];
            if (x->kind != SEG_ENT || x->cls != 0x10000 || x->len < 4 + 0xac) continue;
            uint8_t *e = m->d + x->off + 4;
            if ((int32_t)rd32(e + 0x34) != so->idn) continue;
            e[6] = (uint8_t)so->color; w32(e + 8, (uint32_t)so->level);
            break;
        }
    }
}

/* Machine Group Properties > Files / Tool Settings edited here (2026-10-23, formats/grpprops_tabs.md 2.3 / 3.1; core_ui
   cui_grpprops.c): CGroupPg0::OnOK (FUN_1801e0a40) writes the dialog's copy of the group back, WriteFileGroups writes the
   0x1ae0 struct, so the fields are patched in place in the active machine group's record (as stock_patch; the group whose
   +0 is F->grp_id, else the last machine group = COpMgrTree::LoadTree's active one): +4 name (0x105, _mbsnbcpy_s 0x104),
   +0x980 comment (2000, 1999), +0x1178 program #, +0x119c Feed Calculation, +0x11a0 feed, +0x11b0 spindle speed, +0x1188
   retract, +0x1180 plunge (FUN_1801eb2b0), +0x115c..+0x1168 the override switches, +0x11b8..+0x11c4 the toolpath
   configuration switches. A string is written only when it differs from the stored one (its bytes after the NUL stay);
   numbers are written as loaded unless changed, so the bytes are those of the file. The machine / control records (sequence
   numbers, arc feed, comment switches, library paths) are not written: SaveControlToDb and the page 2 layout are not ported. */
static int seq_ok(double v) { return v == v && v > -2147483648.0 && v < 2147483648.0; }   /* (int) of a stored value: defined only in range (ours) */
static void grp_patch(McwFile *m, const McamFile *F) {
    for (int i = 0; i + 1 < m->nseg; i++)
        if (m->seg[i].kind == SEG_ENT && m->seg[i].cls == 0x200 && m->seg[i].sub == 0x0f && m->seg[i + 1].kind == SEG_OOB && m->seg[i + 1].len >= 4 + 0x1ae0) {
            uint8_t *t = m->d + m->seg[i + 1].off, *g = NULL, *last = NULL;
            const uint8_t *gr[256]; int ng = 0; mcam_grp_block(t, m->seg[i + 1].len, gr, 256, &ng);   /* + member entries (mcam_grp.h) */
            for (int k = 0; k < ng && k < 256; k++) {
                uint8_t *r = (uint8_t *)gr[k];
                if (F->grp_rec && (int32_t)rd32(r) == F->grp_id) g = r;
                if (rd32(r + 0x10c) == 0) last = r;
            }
            if (!g) g = last;
            if (!g) return;
            if (strncmp((const char *)g + 4, F->grp_name, 0x104)) { memset(g + 4, 0, 0x105); memcpy(g + 4, F->grp_name, strnlen(F->grp_name, 0x104)); }
            if (strncmp((const char *)g + 0x980, F->grp_comment, 1999)) { memset(g + 0x980, 0, 2000); memcpy(g + 0x980, F->grp_comment, strnlen(F->grp_comment, 1999)); }
            w32(g + 0x1178, (uint32_t)F->grp_prog); w32(g + 0x119c, (uint32_t)F->grp_fs_mode);
            wd(g + 0x11a0, F->grp_feed); wd(g + 0x11b0, F->grp_rpm); wd(g + 0x1188, F->grp_retract); wd(g + 0x1180, F->grp_plunge);
            for (int k = 0; k < 4; k++) { w32(g + 0x115c + 4 * k, (uint32_t)F->grp_ovr[k]); w32(g + 0x11b8 + 4 * k, (uint32_t)F->grp_tcfg[k]); }
            return;
        }
}

/* the operation's own fields in its record E (ent bytes; RECLEN = the record's length with its u32): the heights, the XY stock,
   the type pages' fields and last its record patches - what the save writes for OP besides the part-level fields (ids, tree
   sequence, chains, group, tool slot / block). Shared by mcam_save_part and the defaults-file writer (mcam_op_dialog_record,
   Q19). KEEP_SEQ = an edited Mastercam op on its own record; NTABREC / KEEP_TABS its copied tab records (contour). */
/* a tab centre -> the chain entity it lies on (the nearest over the op's chains) and t from that entity's own start (TP kind 30) */
static int tab_assign(const McamFile *F, const CamOp *op, double qx, double qy, int *chain, int *entj, double *t) {
    double bd = 1e300; *chain = -1; *entj = -1; *t = 0;
    for (int c2 = 0; c2 < op->nchains; c2++) {
        const Chain *cc = &op->chains[c2]; if (cc->solid_idn > 0 || !cc->closed) continue;
        for (int j = 0; j < cc->n; j++) {
            const Ent *en = &F->ents[cc->ent[j]]; double u, d;
            if (en->type == E_LINE) { double ex = en->b.x - en->a.x, ey = en->b.y - en->a.y, L2 = ex * ex + ey * ey;
                u = L2 > 0 ? ((qx - en->a.x) * ex + (qy - en->a.y) * ey) / L2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
                d = hypot(en->a.x + ex * u - qx, en->a.y + ey * u - qy); }
            else if (en->type == E_ARC && fabs(en->sw) > 1e-12) { double a = atan2(qy - en->c.y, qx - en->c.x) - en->a0, tw = 2 * M_PI;
                a = fmod(a, tw); if (en->sw > 0 && a < 0) a += tw; if (en->sw < 0 && a > 0) a -= tw;
                u = a / en->sw; if (u > 1) u = (fabs(a) - fabs(en->sw)) < (tw - fabs(a)) ? 1 : 0;
                double aa = en->a0 + en->sw * u; d = hypot(en->c.x + en->r * cos(aa) - qx, en->c.y + en->r * sin(aa) - qy); }
            else continue;
            if (d < bd) { bd = d; *chain = c2; *entj = j; *t = u; }
        }
    }
    return *chain >= 0;
}
static int new_tab_recs(const McamFile *F, const CamOp *op, const CamParams *p, int ntabrec) {   /* how many TP kind 30 records are written */
    if (ntabrec || p->type != CAM_CONTOUR || p->tabs <= 0) return 0;
    int n = 0; for (int t = 0; t < p->ntab_xy; t++) { int c, j; double u; if (tab_assign(F, op, p->tab_xy[2 * t], p->tab_xy[2 * t + 1], &c, &j, &u)) n++; }
    return n;
}
static void op_fields_to_record(const McamFile *F, const CamOp *op, uint8_t *e, size_t reclen, int keep_seq, int ntabrec, int keep_tabs) {
    const CamParams *p = &op->p; int style, opcode = mcam_opcode_for(p, &style);
    heights_to_record(F, op, e);
    if (p->type != CAM_DRILL && p->type != CAM_THREAD) wd(e + 0x2e0, p->stock_xy);
    if (p->type == CAM_CONTOUR || p->type == CAM_POCKET || p->type == CAM_SLOT) {
        double depth = p->top_z - p->final_z;                       /* depth cuts / multi passes / lead in-out */
        int ncuts = p->stepdown > 1e-6 ? (int)ceil(depth / p->stepdown - 1e-9) : 1;
        double st0 = rdd(e + 0x870);                                /* the stored depth cuts give the same levels: kept */
        int ncuts0 = e[0x868] && st0 > 1e-6 ? (int)ceil(depth / st0 - 1e-9) : 1, keepdc = ncuts0 == ncuts && (ncuts == 1 || fabs(st0 - p->stepdown) < 1e-9);
        /* depth cuts on +0x868, P10065 max rough step +0x870; P10066 "Number of finish cuts" +0x878 / P10067 +0x880:
           contour and pocket levels here (cam2d.c zlevels) are mccore calc_depth_cuts with no finish cuts -> 0; the
           slot engine's (cam2d_ops.c mc_depths) are the op's own */
        if (!keepdc) { e[0x868] = ncuts > 1; wd(e + 0x870, p->stepdown > 1e-6 ? p->stepdown : depth); }
        if (p->type == CAM_SLOT) { w16(e + 0x878, (uint16_t)(p->mc_depth_nf > 0 ? p->mc_depth_nf : 0)); wd(e + 0x880, p->mc_depth_fs); }
        else if (e[0x868]) {   /* contour / pocket: the finish cuts the engines take (cam2d.c gen_contour / the pocket engines'
                                  hst_depths; batch 3): the record's own, kept - 0 in every stored and default op */
            int nf = p->mc_depth_nf > 0 ? p->mc_depth_nf : 0;
            if ((int16_t)rd16(e + 0x878) != nf) w16(e + 0x878, (uint16_t)nf);
            if (nf && fabs(rdd(e + 0x880) - p->mc_depth_fs) > 1e-9) wd(e + 0x880, p->mc_depth_fs);
        }
        if (p->type == CAM_CONTOUR) {
            /* multi passes (+0x8a8): per depth P15560 "Number of roughing cuts" (+0x8aa) passes, P10066-style finish
               count +0x8b8 at the last depth (mcmill contour, decomp/mcmill_ctour.c l.1506-1580: op +0x842 / +0x850);
               cam2d.c gen_contour cuts one rough pass per depth and finish_n at the final depth -> 1 */
            int mprec = p->ct_mp_rec && p->mc_mp_on;               /* batch 5: the dialog's Multi Passes group (rough count / spacing from the record) */
            e[0x8a8] = p->finish_n > 0 || mprec;                    /* off: the stored counts / spacings kept */
            if (p->finish_n > 0) { w16(e + 0x8aa, (uint16_t)(mprec ? p->mc_mp_nr : 1)); w16(e + 0x8b8, (uint16_t)p->finish_n); wd(e + 0x8c0, p->finish_step); }
            /* P15559 entry arc sweep +0x960: lead_sw 0 = a quarter in the engine (cam2d.h) */
            e[0x8f0] = p->lead_on && (p->lead_r > 0 || p->lead_len > 0 || p->lead_xr > 0 || p->lead_xlen > 0);   /* off: the stored lead values kept */
            if (e[0x8f0]) {   /* the entry and the exit side, each from its own values (HSTLeadIOPageBase store FUN_1801735c0:
                                 entry 0x900 / 0x910 / 0x918 / % 0x930 / 0x938, exit 0x948 / 0x958 / 0x960 / % 0x978 / 0x980;
                                 cam_fileop.c LEADS), each only when it differs; the dialog's other lead fields go as record
                                 patches (cui_opdlg.c) */
                double L[2][3] = { { p->lead_len, p->lead_r, (p->lead_sw_rec & 1) || p->lead_sw > 0 ? p->lead_sw : M_PI / 2 },
                                   { p->lead_xlen, p->lead_xr, (p->lead_sw_rec & 2) || p->lead_xsw > 0 ? p->lead_xsw : M_PI / 2 } };
                /* a sweep from the record / a patch as is (0 = no arc, cam2d.h lead_sw_rec), else 0 = a quarter (ours) */
                static const int LO[2][5] = { { 0x900, 0x910, 0x918, 0x930, 0x938 }, { 0x948, 0x958, 0x960, 0x978, 0x980 } };
                for (int sd = 0; sd < 2; sd++) {
                    if (fabs(rdd(e + LO[sd][0]) - L[sd][0]) > 1e-9) wd(e + LO[sd][0], L[sd][0]);
                    if (fabs(rdd(e + LO[sd][1]) - L[sd][1]) > 1e-9) wd(e + LO[sd][1], L[sd][1]);
                    if (fabs(rdd(e + LO[sd][2]) - L[sd][2]) > 1e-9) wd(e + LO[sd][2], L[sd][2]);
                    if (p->tool_dia > 0) {                          /* % of D (bnci_ctour recalculates the lengths from them) */
                        if (fabs(rdd(e + LO[sd][3]) * 0.01 * p->tool_dia - L[sd][0]) > 1e-9) wd(e + LO[sd][3], 100 * L[sd][0] / p->tool_dia);
                        if (fabs(rdd(e + LO[sd][4]) * 0.01 * p->tool_dia - L[sd][1]) > 1e-9) wd(e + LO[sd][4], 100 * L[sd][1] / p->tool_dia);
                    }
                }
            }
            if (p->side_rel) {                                     /* side relative to each chain's direction (as read) */
                int c0 = (int16_t)rd16(e + 0x5d0);
                w16(e + 0x5d0, (uint16_t)(p->side == SIDE_ON ? 4 : (keep_seq && c0 != 4 ? c0 : p->ct_comp)));   /* ct_comp: batch 5 */
                if (p->side != SIDE_ON) e[0x5d2] = p->side == 2;   /* 0 left, 1 right; comp off: kept */
            } else { w16(e + 0x5d0, (uint16_t)(p->side == SIDE_ON ? 4 : p->ct_comp)); e[0x5d2] = 0; }   /* computer (or ct_comp), left */
            /* roll cutter around corners: 0 none, 1 sharp, 2 all - u8 0x5d3 (page 2938 control 1335, UpdateLocalOp
               FUN_180157e30; formats/opdlg_fields_notes.md F1 conflict 1); 0x5d4 "Optimize cutter comp in control" (2144)
               and 0x5d5 are not touched */
            e[0x5d3] = (uint8_t)p->roll;
            w16(e + 0x1308, (uint16_t)(p->contour_type == 1 ? 2 : p->contour_type == 0 && p->ct_rtype > 0 && p->ct_rtype != 2 ? p->ct_rtype : 0));   /* contour type: 0 2D, 2 2D chamfer; a stored 3D / Ramp / ... kept (ct_rtype) */ wd(e + 0x1288, p->chamfer_w); wd(e + 0x1290, p->chamfer_tip);
            /* tabs P16002 on, P16003 "Automatically calculate tab positions" (+0x1381): auto unless the op's own tab
               records (TP kind 30) are copied, P16004 count, P16005 width, P16006 thickness */
            if (p->tabs > 0) { e[0x1380] = 1; if (!ntabrec) { e[0x1381] = (uint8_t)(p->tab_rec ? p->tab_auto != 0 : 1); w16(e + 0x1382, (uint16_t)p->tabs); } wd(e + 0x1388, p->tab_w); wd(e + 0x1390, p->tab_h); }
            else if (!keep_tabs) e[0x1380] = 0;                     /* off; the stored count / sizes kept */
        }
    }
    if (p->type == CAM_POCKET) {
        wd(e + 0x1240, p->tool_dia * p->stepover_pct / 100); wd(e + 0x1248, p->stepover_pct);
        e[0x1239] = p->finish_n > 0; if (p->finish_n > 0) { w16(e + 0x125a, (uint16_t)p->finish_n); wd(e + 0x1260, p->finish_step); }
        /* regen-consistency 2026-11-19: zigzag (pocket_style 1) is cutting method S16 0x123c = 0 - the engines take method 0 for it
           (cam2d_pock.c / cam2d_pock_zz.c: pks(0x123c), pocket_style 1 -> 0) and params_from_record reads style 1 back from 0 */
        if (p->pocket_style == 1 && (int16_t)rd16(e + 0x123c) != 0) w16(e + 0x123c, 0);
    }
    if (p->type == CAM_HELIX && fabs(rdd(e + 0x1250) - p->helix_pitch) > 1e-12)
        wd(e + 0x1250, p->helix_pitch);   /* regen-consistency 2026-11-19: Helix Bore rough pitch 0x1250 (dialog 2823; params_from_record helix_pitch) */
    if (p->type == CAM_THREAD) {
        /* regen-consistency 2026-11-19: the Thread Mill fields the engine cuts with and cam_fileop.c params_from_record reads back -
           pitch 0x1258 (op +0x11f0: the thread mill engine's pitch, FUN_1802350f0 / FUN_180235a80 / FUN_180236170, decomp/mcmill_all.c l.385268 / l.385626 / l.385807; a new op takes
           the tool's pitch, cam_fileop.c cam_defaults_from_file - unsaved, the saved op regenerated with the default op's), ID / OD
           S16 0x1290 (0 ID, 1 OD), hand u8 0x12a0 (0 right), direction u8 0x12a2 (1 bottom up); each only when it differs */
        if (fabs(rdd(e + 0x1258) - p->thread_pitch) > 1e-12) wd(e + 0x1258, p->thread_pitch);
        if (((int16_t)rd16(e + 0x1290) == 0) != (p->thread_internal != 0)) w16(e + 0x1290, (uint16_t)(p->thread_internal ? 0 : 1));
        if ((e[0x12a0] == 0) != (p->thread_rh != 0)) e[0x12a0] = p->thread_rh ? 0 : 1;
        if ((e[0x12a2] == 1) != (p->thread_up != 0)) e[0x12a2] = p->thread_up ? 1 : 0;
    }
    if (p->type == CAM_DRILL) {
        /* P10100 cycle, P10108 first peck +0x1230, P10115 tip comp +0x1268; P10109 subsequent peck +0x1238 / P10110
           peck clearance +0x1240: not in CamParams, the copied op's kept */
        w16(e + 0x1228, (uint16_t)p->drill_cycle); if (p->drill_cycle == 1 || p->drill_cycle == 2) wd(e + 0x1230, p->peck);   /* cam_fileop: pecks for cycles 1 / 2 */
        e[0x1268] = (uint8_t)(p->tip_comp ? 1 : 0);                /* u8 (HSTDrillTipCompPage::MapToOpEnt 0x180165720) */
    }
    if (opcode == 134) {                                            /* 2D High Speed: style, stepover %, region */
        w16(e + 0x1228, (uint16_t)style); wd(e + 0x1580, p->stepover_pct);
        /* depth cuts as the engine read them (cam2d.c hst_depths: on = +0x868 or a max step +0x870, finish count
           +0x878, finish step +0x880) */
        e[0x868] = p->mc_depth_on || p->stepdown > 1e-6; if (p->stepdown > 1e-6) wd(e + 0x870, p->stepdown);
        w16(e + 0x878, (uint16_t)(p->mc_depth_nf > 0 ? p->mc_depth_nf : 0)); wd(e + 0x880, p->mc_depth_fs);
        if (style == 0) e[0x1567] = (uint8_t)(p->dyn_outside ? 1 : 0);   /* +0x1567: 1 = from outside (formats/dynamic.md) */
    }
    for (int k = 0; k < p->nrpatch; k++)                           /* Edit Common Parameters: its record bytes last */
        if (p->rpatch[k].off + p->rpatch[k].n + 4 <= reclen) memcpy(e + p->rpatch[k].off, p->rpatch[k].v, p->rpatch[k].n);
    if (opcode == 134) {
        /* regen-consistency 2026-11-19: the fields cam2d_dyn.c gen_dynamic_mcce cuts with from CamParams and cam_fileop.c
           params_from_record reads back (LoadDynPockParm, decomp/mcmill_hst2d_dyn.c): stepover % 0x1730 when 0x1700 and 0x1701 are
           set (the reader's choice, l.1612-1624; 0x1580 above otherwise), min toolpath radius % 0x1658, micro lift 0x16d0, plunge
           angle 0x1678, back feed 0x1248, gap size 0x1600 (or % of D 0x1608 when 0x15cd); each only when it reads back otherwise; after the patches (the dialog's 0x15cd / 0x1700 / 0x1701 decide the field) */
#define WDIF(off, v) do { if (fabs(rdd(e + (off)) - (v)) > 1e-9) wd(e + (off), (v)); } while (0)
        if (e[0x1700] && e[0x1701]) WDIF(0x1730, p->stepover_pct);
        WDIF(0x1658, p->min_rad_pct); WDIF(0x16d0, p->micro_lift); WDIF(0x1678, p->ramp_deg);
        if (!(rdd(e + 0x1248) > 1e-5 ? fabs(rdd(e + 0x1248) - p->back_feed) <= 1e-9 : fabs(p->feed - p->back_feed) <= 1e-9)) wd(e + 0x1248, p->back_feed);
        if (e[0x15cd]) { if (p->tool_dia > 0) WDIF(0x1608, p->link_retract * 100 / p->tool_dia); } else WDIF(0x1600, p->link_retract);
#undef WDIF
    }
}

/* nci_write (Q23): the OP_ID record (and a new TL_ID record) each op would be saved with, captured instead of written */
typedef struct { uint8_t **rec; uint32_t *recn; uint8_t **tl; uint32_t *tln; uint8_t **tp; uint32_t *tpn; } SaveCapture;
static int save_impl(const char *src, const char *dst, McamFile *F, CamOp **ops, int nops, char *msg, size_t msgn, SaveCapture *cap);
int mcam_save_part(const char *src, const char *dst, McamFile *F, CamOp **ops, int nops, char *msg, size_t msgn) {
    return save_impl(src, dst, F, ops, nops, msg, msgn, NULL);
}
int mcam_op_records(const char *src, McamFile *F, CamOp **ops, int nops, uint8_t **rec, uint32_t *recn, uint8_t **tl, uint32_t *tln, char *msg, size_t msgn) {
    return mcam_op_records_tp(src, F, ops, nops, rec, recn, tl, tln, NULL, NULL, msg, msgn);
}
int mcam_op_records_tp(const char *src, McamFile *F, CamOp **ops, int nops, uint8_t **rec, uint32_t *recn, uint8_t **tl, uint32_t *tln,
                       uint8_t **tp, uint32_t *tpn, char *msg, size_t msgn) {
    for (int k = 0; k < nops; k++) { rec[k] = tl[k] = NULL; recn[k] = tln[k] = 0; if (tp) { tp[k] = NULL; tpn[k] = 0; } }
    /* the save gives new entities ids (en->idn / eptr / ctime): kept as they were */
    struct { int32_t idn; int64_t eptr; int32_t ctime; } *keep = malloc(sizeof *keep * (size_t)(F->nents + 1));
    for (int i = 0; i < F->nents; i++) { keep[i].idn = F->ents[i].idn; keep[i].eptr = F->ents[i].eptr; keep[i].ctime = F->ents[i].ctime; }
    SaveCapture cap = { rec, recn, tl, tln, tp, tpn };
    int ok = save_impl(src, "", F, ops, nops, msg, msgn, &cap);
    for (int i = 0; i < F->nents; i++) { F->ents[i].idn = keep[i].idn; F->ents[i].eptr = keep[i].eptr; F->ents[i].ctime = keep[i].ctime; }
    free(keep);
    return ok;
}
static int save_impl(const char *src, const char *dst, McamFile *F, CamOp **ops, int nops, char *msg, size_t msgn, SaveCapture *cap) {
    McwFile m; char err[300];
    msg[0] = 0;
    if (!cap && src && (!strcmp(src, dst) || same_file(src, dst))) { snprintf(msg, msgn, "refusing to write over the source part"); return 0; }
    int fresh = !src || !src[0];
    char basenote[400] = "";
    if (fresh) { if (!new_base(&m, mcam_is_metric(F), msg, msgn, basenote, sizeof basenote)) return 0; }
    else if (!mcw_open(&m, src, err, sizeof err)) { snprintf(msg, msgn, "%s", err); return 0; }
    /* positions and ids */
    int last_op = -1, nci_id = -1, hdr = -1, maxseq = 0, first_geo_slot = -1, first_assoc = -1;
    int64_t max_eptr = 0;
    for (int i = 0; i < m.nseg; i++) {
        McwSeg *s = &m.seg[i]; const uint8_t *e = m.d + s->off + 4;
        if (s->kind != SEG_ENT) continue;
        if (s->len >= 4 + 0x28) { int64_t ep = rd64(e + 0x20); if (ep > max_eptr) max_eptr = ep; }
        if (s->cls == 0x200 && s->sub == 0x0d && s->len >= 4 + 0x80) hdr = i;   /* written up to +0x7f */
        if (s->cls == 0x4000 && first_assoc < 0) first_assoc = i;
        if ((s->cls == 0x20000 || s->cls == 0x10000) && first_geo_slot < 0) first_geo_slot = i;
        if (s->cls == 0x4000 && s->sub == 1) { last_op = i; if (s->len >= 4 + 0xe8) { int sq = (int16_t)rd16(e + 0xe6); if (sq > maxseq) maxseq = sq; } }
        if (s->cls == 0x4000 && s->sub == 5 && nci_id < 0) nci_id = i;
    }
    if (hdr < 0 || first_assoc < 0) { mcw_close(&m); snprintf(msg, msgn, "the base part has no header / setup records"); return 0; }
    memcpy(g_vstamp, m.d + m.seg[hdr].off + 4 + 0x48, 7);
    /* the units every record is saved with: IsMetric() (FUN_1808c4ce0 stamps DAT_18143d54f, FUN_1802c72c0 / FUN_1802c4010 write it
       at record +0x4d, FUN_1802c5a00 at the version entity's +0x6f; decomp/units/core_stamp.c) - a part whose units were switched
       (cui_units_set) is saved in the new ones */
    {   int um = mcam_is_metric(F); g_vstamp[5] = (uint8_t)um;
        if (m.n > 4 + 0x6f && rd32(m.d) == 0x98 && rd32(m.d + 4) == 0x200) m.d[4 + 0x6f] = (uint8_t)um; }
    if (first_geo_slot < 0) first_geo_slot = first_assoc;              /* geometry goes after the system records */
    int op_slot = last_op >= 0 ? last_op + 1 : first_assoc + 1;        /* new ops: after the existing ones (or the setup) */
    for (int i = 0; last_op < 0 && i < m.nseg; i++) if (m.seg[i].cls == 0x4000 && m.seg[i].sub == 7) op_slot = i + 1;
    int tp_slot = nci_id >= 0 ? nci_id : m.first_nci >= 0 ? m.first_nci : m.nseg;
    int32_t next_idn = m.max_ent_idn + 1, next_op = m.max_op_id + 1;
    int64_t next_eptr = (max_eptr + 0x1000) & ~(int64_t)0xf;
    int32_t now = (int32_t)time(NULL);
    int major = m.major, minor = m.minor;
    /* groups: from an existing op in the base, else the base's machine group / first toolpath group record */
    int32_t my_group = -1, my_mgroup = -1;
    for (int i = 0; i < m.nseg && my_group < 0; i++) if (m.seg[i].cls == 0x4000 && m.seg[i].sub == 1 && m.seg[i].len >= 4 + 0x5c0) {
        const uint8_t *e = m.d + m.seg[i].off + 4; my_group = (int32_t)rd32(e + 0x2f4); my_mgroup = (int32_t)rd32(e + 0x5bc); }
    /* no op: the base's first toolpath group (op_group +0x10c parent != 0) and its machine group (formats/mcam_write.md 2:
       an op in a machine group itself is moved to a new toolpath group by CheckOpsGroupAssignment) */
    for (int i = 0; i + 1 < m.nseg && my_group < 0; i++)
        if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x200 && m.seg[i].sub == 0x0f && m.seg[i + 1].kind == SEG_OOB) {
            const uint8_t *gr[256]; int cnt = 0; mcam_grp_block(m.d + m.seg[i + 1].off, m.seg[i + 1].len, gr, 256, &cnt);   /* + member entries (mcam_grp.h) */
            if (cnt > 256) cnt = 256;
            for (int k = 0; k < cnt && my_group < 0; k++) {
                const uint8_t *r = gr[k]; int32_t par = (int32_t)rd32(r + 0x10c);
                if (par != 0) { my_group = (int32_t)rd32(r); my_mgroup = par; }
            }
            /* a nested toolpath group's root: follow the parents */
            for (int hop = 0; my_group >= 0 && hop < 16; hop++) {
                int up = -1;
                for (int k = 0; k < cnt; k++) {
                    const uint8_t *r = gr[k]; if ((int32_t)rd32(r) == my_mgroup) up = (int32_t)rd32(r + 0x10c);
                }
                if (up <= 0) break; my_mgroup = up;
            }
        }
    Buf geob = { 0 }, opb = { 0 }, tpb = { 0 }, tlb = { 0 };
    /* the views made since the part was read (mcam_add_view): DB_ID 0x200 subtype 0, 0xb8 bytes: +0x48 the version stamp, +0x68 i16 subtype 0, +0x6a i16 number, +0x70 matrix rows (as read: mcam_load.c; every other byte of a stored view record of PARALLELSTEEP.mcx-9 is 0).
       They go first in the geometry block (in all 2809 corpus file / view pairs with an arc in the view, the view's record comes before its first arc); written at every save, as the new entities are.
       The byte +0x4a: 12797 stored view records have 0, 1, 3 or 5 there, not correlated with the file's entity records (6212 equal, 6129 not); what it means is NOT DECODED, 0 is what 2766 records have */
    for (int i = 0; i < F->nfviews; i++) if (F->fviews[i].isnew) {
        uint8_t vr[4 + 0xb8]; memset(vr, 0, sizeof vr); w32(vr, 0xb8); uint8_t *ve = vr + 4; w32(ve, 0x200);
        memcpy(ve + 0x48, g_vstamp, 7); ve[0x48] = (uint8_t)major; ve[0x49] = (uint8_t)minor; ve[0x4a] = 0;
        w16(ve + 0x6a, (uint16_t)F->fviews[i].n); for (int k = 0; k < 9; k++) wd(ve + 0x70 + 8 * k, F->fviews[i].m[k]);
        put(&geob, vr, sizeof vr);
    }
    PartSlot *pslots; int npslots = part_slots(&m, &pslots), ntlw = 0;
    /* ---- part tools edited here (see "part tools edited" above) ---- */
    TEdit *ted = NULL; int nted = 0, ntoolw = 0, tlm_seg = -1; uint8_t *tlm_new = NULL; size_t tlm_n = 0;
    /* the level table changed here (a level made / named: F->lvl_dirty): its record DB_ID 0x200 subtype 0x15 rewritten as save_db
       @1802bc3d0 writes it (decomp/levels/core_levels.c 1350-1400): +0x7c the visible count, +0x80 the named count, then the visible
       levels and the named ones (level + the name, at most 300 characters, in a 0x130 field), in the table's order. Ours: the rest
       of the record (main level / colour / attributes +0x08 .. +0x72) is the part's own, not the session's */
    int lvl_seg = -1; uint8_t *lvl_new = NULL; size_t lvl_n = 0;
    for (int i = 0; i < m.nseg && F->lvl_dirty; i++) {
        const McwSeg *g = &m.seg[i];
        if (g->kind != SEG_ENT || g->cls != 0x200 || g->sub != 0x15 || g->len < 4 + 0x98) continue;
        int nv = 0, nn = 0;
        for (int k = 0; k < F->nlvls; k++) { nv += F->lvls[k].vis != 0; nn += mcam_level_named(&F->lvls[k]); }
        /* the record length (FUN_1802c72c0 @1802c72c0, decomp/mccore_tooladd.c L86-90: bytes_per_ent(ent, 1) + bytes_per_ent(ent, 2)
           is written as the length, then the fixed 0x98 bytes, then the +0x88 buffer of bytes_per_ent(ent, 2) bytes, L162 / L198);
           bytes_per_ent @1802e5230 (decomp/mccore_entdebug.c L771-787) for DB_ID 0x200 subtype 0x15: fixed 0x30 + 0x68, variable
           (named * 0x4d + visible) * 4 = visible * 4 + named * 0x134, padded up to a multiple of 8; the padding is the zeroes of
           save_db's calloc (decomp/levels/core_levels.c L1371) */
        size_t var = (size_t)nv * 4 + (size_t)nn * 0x134; if (var % 8) var += 8 - var % 8;
        size_t L = 0x98 + var;
        lvl_new = xcalloc(4 + L, 1); w32(lvl_new, (uint32_t)L); memcpy(lvl_new + 4, m.d + g->off + 4, 0x98);
        uint8_t *e = lvl_new + 4; w32(e + 0x7c, (uint32_t)nv); w32(e + 0x80, (uint32_t)nn);
        uint8_t *t = e + 0x98;
        for (int k = 0; k < F->nlvls; k++) if (F->lvls[k].vis) { w32(t, (uint32_t)F->lvls[k].level); t += 4; }
        for (int k = 0; k < F->nlvls; k++) if (mcam_level_named(&F->lvls[k])) {
            w32(t, (uint32_t)F->lvls[k].level); snprintf((char *)t + 4, 0x12d, "%.300s", F->lvls[k].name); t += 0x134; }
        lvl_seg = i; lvl_n = 4 + L;
        break;
    }
    char toolnote[400] = "";
    if (!fresh && F->ntools) {
        McamFile *B = xcalloc(1, sizeof *B);
        if (mcam_load(B, src)) {
            ted = xcalloc(F->ntools, sizeof *ted);
            for (int k = 0; k < F->ntools; k++) {
                const Tool *t = &F->tools[k];
                if (!slot_owner(F, pslots, npslots, t)) continue;
                int b = -1; for (int j = 0; j < B->ntools && b < 0; j++) if (B->tools[j].slot == t->slot) b = j;
                if (b < 0) continue;
                TEdit *x = &ted[nted++]; x->now = t; x->base = B->tools[b]; x->base.holder_rz = NULL;
                if (B->tools[b].holder_n) { x->base.holder_rz = xmalloc(sizeof(double) * 2 * B->tools[b].holder_n); memcpy(x->base.holder_rz, B->tools[b].holder_rz, sizeof(double) * 2 * B->tools[b].holder_n); }
                edit_prep(x);
                x->res = x->tl_changed ? PT_NOSLOT : 0;                 /* until the TlMgr says otherwise */
            }
            for (int i = 0; i < m.nseg && nted; i++) {                  /* the TlMgr archive (formats/part_tools.md 1) */
                const McwSeg *g = &m.seg[i];
                if (g->kind != SEG_ENT || g->cls != 0x4000 || g->sub != 33 || g->len < 4 + 0x57 || !m.d[g->off + 4 + 0x56]) continue;
                PtEdit *pe = xcalloc(nted, sizeof *pe);
                for (int k = 0; k < nted; k++) { pe[k].now = &ted[k].nb; pe[k].base = &ted[k].bb; pe[k].block_now = ted[k].ob_now; pe[k].block_base = ted[k].ob_base; }
                uint8_t *o; size_t on;
                int r = part_tools_write(m.d + g->off + 4, g->len - 4, pe, nted, &o, &on);
                if (r < 0) {                                            /* lathe / older classes: the record is kept */
                    for (int k = 0; k < nted; k++) if (ted[k].tl_changed) ted[k].res = PT_CLASS;
                    snprintf(toolnote, sizeof toolnote, "the part's tool manager archive could not be read: tool edits not written");
                } else {
                    for (int k = 0; k < nted; k++) {
                        ted[k].res = pe[k].result;
                        if (pe[k].result == PT_CLASS) { size_t L_ = strlen(toolnote); snprintf(toolnote + L_, sizeof toolnote - L_, "%sT%d: a type of another tool class is not written (NOT DECODED)", L_ ? "; " : "", ted[k].now->num); }
                    }
                    if (r > 0) { tlm_seg = i; tlm_n = 4 + on; tlm_new = xmalloc(tlm_n); w32(tlm_new, (uint32_t)on); memcpy(tlm_new + 4, o, on); }
                }
                free(o); free(pe);
                break;
            }
            /* TL_ID records of the edited tools (same length: rewritten in place); a tool with no assembly in the archive
               (PT_NOSLOT) is the TL_ID's alone (Mastercam adds it to its TlMgr on load, mcam_write.md 8) */
            for (int k = 0; k < nted; k++) {
                if (ted[k].res > 0 || (ted[k].res == PT_NOSLOT && ted[k].tl_changed)) ntoolw++;
                if (ted[k].res == PT_CLASS || ted[k].res == 0 || !ted[k].tl_changed) continue;
                for (int i = 0; i < m.nseg; i++) {
                    const McwSeg *g = &m.seg[i];
                    if (g->kind != SEG_ENT || g->cls != 0x4000 || g->sub != 3 || g->len < 4 + 0x2d0) continue;
                    uint8_t *e = m.d + g->off + 4;
                    if ((int32_t)rd32(e + 0x2cc) == ted[k].base.slot) edit_tl_body(&ted[k], e + 0x68, g->len - 4 - 0x68);
                }
            }
        }
        mcam_free(B); free(B);
    }
    struct { int num, slot; const Tool *from; } *newtl = NULL; int nnewtl = 0;
    char notes[700] = ""; int added = 0, skipped = 0, newgeo = 0;
#define NOTE(...) do { size_t L_ = strlen(notes); snprintf(notes + L_, sizeof notes - L_, __VA_ARGS__); } while (0)
    if (basenote[0]) NOTE("%s", basenote);
    /* ---- new geometry: entities with no file id ---- */
    for (int i = 0; i < F->nents; i++) {
        Ent *en = &F->ents[i];
        if (!en->created || !en->type) continue;                         /* drawn here: written fresh every time */
        if (en->type == E_NOTE) continue;                                /* notes: after the dimensions (below) */
        uint8_t r[4 + 0xd0]; memset(r, 0, sizeof r);
        uint8_t *e = r + 4; size_t len;
        /* a Move's result (core_ui mod_pair): it keeps the id of the original it replaces (xform_swap_idn), which is dropped below;
           else a new id */
        int moved = 0;
        if (en->idn > 0) for (int k = 0; k < F->nents && !moved; k++) moved = k != i && !F->ents[k].type && !F->ents[k].created && F->ents[k].idn == en->idn;
        if (!moved) en->idn = next_idn++;
        int64_t old_eptr = en->eptr;
        en->eptr = next_eptr; next_eptr += 0x200; en->ctime = now;
        if (en->type == E_MESH) {   /* a mesh made here (File > Open of an STL file; formats/cad_import.md 7): FUN_1802c72c0 writes u32 = bytes_per_ent
                                       (0x130) + the entry's size (GetPolygonalMeshDataSize) - 8, the record without its +0x70 (FUN_1802c2f00), then
                                       WritePolygonalMeshDataToStream (UtMMFDataMgr::WriteEntryToStream: mcam_stl_entry_write) */
            McMesh *mh = en->msh >= 0 && en->msh < F->nmeshes ? &F->meshes[en->msh] : NULL;
            size_t dn = 0; uint8_t *dat = mh ? mcam_stl_entry_write(mh, &dn) : NULL;
            if (!dat) { NOTE("%sa mesh not written: %s", notes[0] ? "; " : "", mh && mh->mixed ? "its facets' colour bytes differ (WriteEnhancedSTL's sort order: NOT PORTED)" : "its data is not STL (NOT DECODED)"); continue; }
            uint8_t R[0x130]; memcpy(R, mh->rec, 0x130);
            head(R, 0x100000, 0, 0, en->color ? en->color : 1, en->level ? en->level : 1, en->eptr, now, en->idn, major, minor, 1);
            if (en->blanked) R[5] |= 1;
            uint8_t *mr = xmalloc(4 + 0x128 + dn);
            w32(mr, (uint32_t)(0x128 + dn)); memcpy(mr + 4, R, 0x70); memcpy(mr + 4 + 0x70, R + 0x78, 0x130 - 0x78); memcpy(mr + 4 + 0x128, dat, dn);
            put(&geob, mr, 4 + 0x128 + dn); free(mr); free(dat);
            mh->eptr = en->eptr; mh->idn = en->idn; newgeo++;
            continue;
        }
        if (en->type == E_SURF) {   /* a surface made here: its SURF_ID record as built (put_surf_in_db's fields), the writer's header */
            Surf *sf = en->srf >= 0 && en->srf < F->nsurfs ? &F->surfs[en->srf] : NULL;
            if (!sf || sf->n < 0x68 + 0x98) continue;
            uint8_t *sr = xmalloc(4 + sf->n); memcpy(sr + 4, sf->rec, sf->n);
            head(sr + 4, 0x100, 0, 0, en->color ? en->color : 1, en->level ? en->level : 1, en->eptr, now, en->idn, major, minor, 0);
            if (en->blanked) sr[4 + 5] |= 1;             /* sel bit 0x100: a trimmed surface's base stays blanked (formats/mcam.md "Blanked") */
            { int dr = cons_ref_delta(F, en->septr); if (dr) w16(sr + 4 + 0x28, (uint16_t)((int16_t)rd16(sf->rec + 0x28) + dr)); }   /* surface curves made here on it */
            /* the references to other entities made here (a trimmed surface's base, ...): their new eptr */
            surf_remap_refs(F, sr + 4, sf->n, i);
            sf->eptr = en->eptr; sf->idn = en->idn; (void)old_eptr;
            w32(sr, sf->n); put(&geob, sr, 4 + sf->n); free(sr); newgeo++;
            continue;
        }
        if (en->type == E_SCRV) {   /* a surface curve made here (Curve Surface, mcam_new_cons): its uv curve (put_nurbs_in_db first: the 2D order-2 NURBS,
                                       selection 0x01 - the corpus' uv curves are not drawn -, +0x28 refs 1: inc_refs_eptr), then the CONS record (put_cons_in_db:
                                       +0xd0 the surface's eptr - a surface made here: its new one -, +0xd8 the uv curve's) */
            CurveRec *cr = en->crv >= 0 && en->crv < F->ncrvs ? &F->crvs[en->crv] : NULL;
            const CurveRec *ur = cr && cr->n >= 0xe0 ? mcs_curve(F, (int64_t)rd64(cr->rec + 0xd8)) : NULL;
            if (!cr || !ur) { NOTE("%sa surface curve not written: its records are missing", notes[0] ? "; " : ""); continue; }
            int64_t uep = en->eptr; en->eptr = next_eptr; next_eptr += 0x200;   /* the eptrs in file order: the uv curve first, then the CONS */
            uint8_t *ub = xmalloc(4 + ur->n); memcpy(ub + 4, ur->rec, ur->n);
            head(ub + 4, 0x80, 0, 0, en->color ? en->color : 1, en->level ? en->level : 1, uep, now, next_idn++, major, minor, 1);
            ub[4 + 4] = 0x01; w16(ub + 4 + 0x28, 1);
            w32(ub, ur->n); put(&geob, ub, 4 + ur->n); free(ub);
            uint8_t cb[4 + 0xf0]; memset(cb, 0, sizeof cb); memcpy(cb + 4, cr->rec, cr->n < 0xf0 ? cr->n : 0xf0); uint8_t *ce = cb + 4;
            head(ce, 0x400, 0, 0, en->color ? en->color : 1, en->level ? en->level : 1, en->eptr, now, en->idn, major, minor, 1);
            wd(ce + 0x10, en->width >= 1 ? en->width : 1.0); w16(ce + 0x18, (uint16_t)(en->ln_style >= 1 ? en->ln_style : 1));
            { int64_t sv = (int64_t)rd64(ce + 0xd0); for (int k = 0; k < F->nents; k++) if (F->ents[k].created && F->ents[k].septr && F->ents[k].septr == sv) { w64(ce + 0xd0, (uint64_t)F->ents[k].eptr); break; } }
            w64(ce + 0xd8, (uint64_t)uep);
            w32(cb, 0xf0); put(&geob, cb, sizeof cb); newgeo += 2;
            continue;
        }
        if (en->type == E_SPLINE) {   /* a spline curve made here (mcam_new_curve: a Transform's result): its class 8 / 0x80 record as built, the
                                         writer's header (as the points / lines / arcs: selection 0x81, the attributes below) */
            CurveRec *cr = en->crv >= 0 && en->crv < F->ncrvs ? &F->crvs[en->crv] : NULL;
            if (!cr || cr->n < 0xa8) { NOTE("%sa spline curve not written: it has no curve record", notes[0] ? "; " : ""); continue; }   /* (ours: never
                                                                      another curve's record) */
            uint8_t *sr = xmalloc(4 + cr->n); memcpy(sr + 4, cr->rec, cr->n); uint8_t *ce = sr + 4;
            head(ce, cr->cls, 0, 0, en->color ? en->color : 1, en->level ? en->level : 1, en->eptr, now, en->idn, major, minor, 1);
            wd(ce + 0x10, en->width >= 1 ? en->width : 1.0); w16(ce + 0x18, (uint16_t)(en->ln_style >= 1 ? en->ln_style : 1));
            cr->eptr = en->eptr; memcpy(cr->rec, ce, 0x68);
            w32(sr, cr->n); put(&geob, sr, 4 + cr->n); free(sr); newgeo++;
            continue;
        }
        if (en->type == E_LINE) {
            len = 0x98; en->cls = 2; head(e, 2, 0, 0, en->color ? en->color : 1, en->level ? en->level : 1, en->eptr, now, en->idn, major, minor, 1);
            wd(e + 0x68, en->a.x); wd(e + 0x70, en->a.y); wd(e + 0x78, en->a.z); wd(e + 0x80, en->b.x); wd(e + 0x88, en->b.y); wd(e + 0x90, en->b.z);
        } else if (en->type == E_ARC) {
            len = 0xd0; en->cls = 4; head(e, 4, 0, 0, en->color ? en->color : 1, en->level ? en->level : 1, en->eptr, now, en->idn, major, minor, 1);
            V3 s = mcam_arc_pt(en, en->a0), t = mcam_arc_pt(en, en->a0 + en->sw), cv = en->c;
            /* a_3d (formats/mcam.md "Arc views"): start / end world, the centre in the arc's view coordinates
               (world_to_view), view number at +0x60 (A_ID +0xc8); XY arcs: view 1, centre = world */
            if (en->vw) { const double *m = en->vm;
                cv = (V3){ m[0] * en->c.x + m[1] * en->c.y + m[2] * en->c.z, m[3] * en->c.x + m[4] * en->c.y + m[5] * en->c.z, m[6] * en->c.x + m[7] * en->c.y + m[8] * en->c.z }; }
            wd(e + 0x68, s.x); wd(e + 0x70, s.y); wd(e + 0x78, s.z); wd(e + 0x80, t.x); wd(e + 0x88, t.y); wd(e + 0x90, t.z);
            wd(e + 0x98, cv.x); wd(e + 0xa0, cv.y); wd(e + 0xa8, cv.z);
            wd(e + 0xb0, en->r); wd(e + 0xb8, en->a0); wd(e + 0xc0, en->sw); w32(e + 0xc8, en->vw ? (uint32_t)en->vw : 1);
        } else {
            len = 0x80; en->cls = 1; head(e, 1, 0, 0, en->color ? en->color : 1, en->level ? en->level : 1, en->eptr, now, en->idn, major, minor, 1);
            wd(e + 0x68, en->a.x); wd(e + 0x70, en->a.y); wd(e + 0x78, en->a.z);
        }
        wd(e + 0x10, en->width >= 1 ? en->width : 1.0);   /* the entity's attributes (main_attrib at creation, Home > Attributes after) */
        { int st = en->type == E_POINT ? (en->pt_style >= 1 ? en->pt_style : 1) : (en->ln_style >= 1 ? en->ln_style : 1);
          w16(e + 0x18, (uint16_t)(en->ln_style >= 1 ? en->ln_style : st)); w16(e + 0x1c, (uint16_t)(en->type == E_POINT ? st : (en->pt_style >= 1 ? en->pt_style : st))); }
        if (en->blanked) e[5] |= 1;                       /* sel bit 0x100: blanked (XfBlankEntity; draw_ent skips it) - as the surfaces above */
        w32(r, (uint32_t)len);
        put(&geob, r, 4 + len); newgeo++;
    }
    /* ---- drafting entities made here (core_ui cui_draft.c): the D_ID record as calc_linear_dimension built it (formats/drafting/
       records.md 2), written after the new geometry. The header as the geometry's (head(): selection bits 0x81 - corpus: 0x81 on most
       2017 D_ID records - colour / level from the record, a new eptr / id / time, the writer's stamp); the attributes (+0x10..+0x1f)
       stay as InitDIM set them (main_attrib). An aPoint that references an entity gets that entity's eptr (new geometry got its eptr
       above); a referenced entity deleted here: the dimension is not written (what Mastercam does with the associative dimension of
       deleted geometry is NOT DECODED) ---- */
    int newdim = 0, skipdim = 0;
    int64_t *dimep = F->ndnew ? calloc((size_t)F->ndnew, sizeof *dimep) : NULL;   /* each written dimension's eptr (an ordinate's base) */
    for (int k = 0; k < F->ndnew; k++) {
        const struct DraftNew *dn = &F->dnew[k];
        if (!dn->on) continue;
        if (rd64(dn->E + 0x20)) { if (dimep) dimep[k] = (int64_t)rd64(dn->E + 0x20); continue; }   /* a part record rewritten here (core_ui
                                                                     cui_draft_pick.c): written over that record below, its eptr kept */
        int bad = 0;
        for (int j = 0; j < 2; j++) if (dn->ent[j] >= 0 && (dn->ent[j] >= F->nents || !F->ents[dn->ent[j]].type)) bad = 1;
        /* the aPoints of the record (records.md 2.1: linear +0x70 / +0x108, angular +0x68 / +0x100 / +0x198 / +0x230, circular +0x68,
           point +0x68, perpendicular / tangent +0x70 / +0x108 / +0x1a0 / +0x238): entity slots holding a session token (MCAM_EPTR_TOKEN) */
        static const int apo[7][4] = { { 0x70, 0x108 }, { 0x68, 0x100, 0x198, 0x230 }, { 0x68 }, { 0x68 }, { 0x68 }, { 0x70, 0x108, 0x1a0, 0x238 }, { 0x70, 0x108, 0x1a0 } };
        int ty = dn->E[0x308] < 7 ? dn->E[0x308] : 0;
        for (int j = 0; j < 4 && apo[ty][j]; j++) for (int sl = 0; sl < 2; sl++) { uint64_t t = rd64(dn->E + apo[ty][j] + 0x28 + 0x18 * sl);
            if ((t >> 62) == 1 && ((int64_t)(t & 0xffffffffull) >= F->nents || !F->ents[t & 0xffffffffull].type)) bad = 1; }
        if (bad) { skipdim++; continue; }
        uint8_t r[4 + 0x810]; memcpy(r + 4, dn->E, 0x810); uint8_t *e = r + 4;
        for (int j = 0; j < 4 && apo[ty][j]; j++) for (int sl = 0; sl < 2; sl++) { uint64_t t = rd64(e + apo[ty][j] + 0x28 + 0x18 * sl);
            if ((t >> 62) == 1) w64(e + apo[ty][j] + 0x28 + 0x18 * sl, (uint64_t)F->ents[t & 0xffffffffull].eptr); }
        int64_t ep = next_eptr; next_eptr += 0x200;
        head(e, 0x40, 0, 0, e[6] ? e[6] : 1, (int)rd32(e + 8) ? (int)rd32(e + 8) : 1, ep, now, next_idn++, major, minor, 0);
        if (e[0x308] == 3) {                                /* an ordinate (core_ui cui_draft_ord.c): its one aPoint at +0x68; +0x108 = the base
                                                               ordinate's eptr (calc_ordinate_dimension, DfOrdType), ent[1] = -2 - the base's index
                                                               (a base not written: 0, an orphan as DfTurnOffOrdAssoc leaves it) */
            if (dn->ent[0] >= 0) w64(e + 0x68 + 0x28, (uint64_t)F->ents[dn->ent[0]].eptr);
            int pb = dn->ent[1] <= -2 ? -2 - dn->ent[1] : -1;
            if (pb >= 0) w64(e + 0x108, (uint64_t)(pb < k && dimep ? dimep[pb] : 0));
        } else
        for (int j = 0; j < 2; j++) if (dn->ent[j] >= 0 && (e[0x308] == 0 || e[0x308] == 2)) w64(e + (e[0x308] == 2 ? 0x68 : 0x70) + 0x98 * j + 0x28, (uint64_t)F->ents[dn->ent[j]].eptr);   /* aPoint slot 0 eptr (circular: the
                                                                              centre aPoint at +0x68, records.md 2.1) */
        if (dimep) dimep[k] = ep;
        w32(r, 0x810); put(&geob, r, sizeof r); newdim++;
    }
    free(dimep);
    if (skipdim) NOTE("%s%d dimension(s) not written: their geometry was deleted", notes[0] ? "; " : "", skipdim);
    /* ---- notes made here (E_NOTE, draft_note.c; formats/drafting/notes.md 4): the N_ID / W_ID record as made (calc_note / CalcLabel),
       written after the dimensions with the geometry's header (head(): selection bits 0x81 - corpus N_ID: 870 x 0x81, 726 x 0x1 -, a new
       eptr / id / time, the writer's stamp), the Ent's colour / level; its memory-table entries go into the part's table (below) under
       new ids: above every id of the base part's table (mTableRead gives each stored id a new one on load, core_draft_rec2.c 2416-2497,
       so the ids only have to be unique in the file) ---- */
    int newnote = 0; int32_t mt_max = 0;
    int mt_seg = -1;                                                   /* the memory table: the out-of-band block after DB_ID sub 0xE */
    for (int i = 0; i + 1 < m.nseg; i++) if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x200 && m.seg[i].sub == 0xE && m.seg[i + 1].kind == SEG_OOB && m.seg[i + 1].sub == 0xE) { mt_seg = i + 1; break; }
    if (mt_seg >= 0) for (size_t o = 0; o + 0x28 <= m.seg[mt_seg].len; ) {
        const uint8_t *h = m.d + m.seg[mt_seg].off + o; int32_t id = (int32_t)rd32(h); uint64_t sz = (uint64_t)rd64(h + 0x10);
        if (!id) break;
        if (id > mt_max) mt_max = id;
        o += 0x28 + (size_t)sz;
    }
    Buf mtnew = { 0 }, noteb = { 0 };                                  /* the new notes' entries ([0x28 header][data] each) and records: the records go
                                                                          after the memory table (a note read before the table's entries would
                                                                          not find its text: the read-time fix-up looks it up, mTableFindData at
                                                                          decomp/mccore_dbread.c 3085; the corpus N_ID records all follow the table) */
    for (int k = 0; k < F->nnotes; k++) {
        const Note *nt = &F->notes[k]; int e = nt->ent;
        if (e < 0 || e >= F->nents || !F->ents[e].created || !F->ents[e].type) continue;
        if (mt_seg < 0) { NOTE("%sa note not written: the base part has no memory table", notes[0] ? "; " : ""); continue; }
        uint8_t r[4 + 0x1c8]; memcpy(r + 4, nt->E, 0x1c8); uint8_t *e8 = r + 4;
        int32_t *nid = xcalloc((size_t)(nt->nmt ? nt->nmt : 1), sizeof *nid);
        for (int q = 0; q < nt->nmt; q++) nid[q] = ++mt_max;
#define NEWID(old) ({ int32_t o_ = (old), v_ = 0; for (int q_ = 0; q_ < nt->nmt; q_++) if (nt->mt[q_].id == o_) v_ = nid[q_]; v_; })
        if (e8[0x68] > 1) w32(e8 + 0xf8, (uint32_t)NEWID((int32_t)rd32(e8 + 0xf8)));
        uint8_t **data = xcalloc((size_t)(nt->nmt ? nt->nmt : 1), sizeof *data);   /* the entries' bytes, ids inside patched */
        for (int q = 0; q < nt->nmt; q++) { data[q] = xmalloc(nt->mt[q].n ? nt->mt[q].n : 1); memcpy(data[q], nt->mt[q].p, nt->mt[q].n); }
        /* the leader chain (mTableDeleteNote's walk, core_draft_rec2.c 2310-2370): wl +0x20 (an aPoint array id when +0x19) and +0x84 (the
           next wl's id), from the record's wl at +0x100 through the chained entries */
        uint8_t *wl = e8 + 0x100; uint32_t avail = 0x98;
        for (int guard = 0; e8[0x68] < 3 && wl && avail >= 0x88 && wl[0] && guard < 1000; guard++) {
            int32_t nx = (int32_t)rd32(wl + 0x84);
            if (wl[0x19]) w32(wl + 0x20, (uint32_t)NEWID((int32_t)rd32(wl + 0x20)));
            w32(wl + 0x84, (uint32_t)NEWID(nx));
            uint8_t *nw = NULL; uint32_t na = 0;
            for (int q = 0; q < nt->nmt; q++) if (nx && nt->mt[q].id == nx) { nw = data[q]; na = nt->mt[q].n; }
            wl = nw; avail = na;
        }
#undef NEWID
        for (int q = 0; q < nt->nmt; q++) {
            uint8_t h[0x28]; memset(h, 0, sizeof h);                   /* MTABLE_REC: id @0, size @0x10 (the pointers Mastercam leaves at +8 /
                                                                          +0x18 / +0x20 are not read back, mTableRead) */
            w32(h, (uint32_t)nid[q]); w64(h + 0x10, nt->mt[q].n);
            put(&mtnew, h, sizeof h); put(&mtnew, data[q], nt->mt[q].n); free(data[q]);
        }
        free(data); free(nid);
        const Ent *en = &F->ents[e];
        int64_t ep = next_eptr; next_eptr += 0x200;
        head(e8, rd32(e8), 0, 0, en->color, en->level, ep, now, next_idn++, major, minor, 0);
        if (en->attr_edited) note_attr_patch(e8, en);   /* Home > Attributes on it (as a part note's, below) */
        w32(r, 0x1c8); put(&noteb, r, sizeof r); newnote++;
    }
    /* ---- operations ---- */
    /* a new operation is a copy of the defaults library's operation of its type (config 2303, formats/ui.md: the file
       Mastercam fills a new operation from); an edited Mastercam operation is a copy of its own record */
    const McwFile *dlib = mcam_defaults_library(mcam_is_metric(F));
    int nref = 0; int32_t *refs = NULL;                                 /* geometry referenced by the new ops */
    /* the TP_ID record size of the base part: 0x388 (X9 / 2017), 0x398 (2018: every field from +0x98 on 0x10 later, formats/
       surface_selection.md 2.1) - the kind 4 records take the base's layout */
    int tpsz = 0x388;
    for (int i = 0; i < m.nseg; i++) if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x4000 && m.seg[i].sub == 2) { if (m.seg[i].len == 4 + 0x398) tpsz = 0x398; break; }
    NewSldTp newsld[64]; int nnewsld = 0;                              /* whole-body SldToolPOps this save appends to solids' histories */
    int32_t uidmap[512][2]; int nuidmap = 0;                         /* contour ops with a separate Tab Cutoff: ct_uid -> the op id written */
    /* the ops whose NCI section this save writes (formats/nci_save.md 4): the op's record (its place in opb), its new TL_ID record (tlc,
       NULL: the part's record of its slot) and its tp records (drill points: bnci_drill's list) */
    typedef struct { int k; size_t rec_at; uint32_t recn; uint8_t *tp; uint32_t tpn; } NciPend;
    NciPend *npend = NULL; int nnpend = 0;
    uint8_t **tlc = cap ? NULL : xcalloc((size_t)nops + 1, sizeof *tlc);
    for (int k = 0; k < nops; k++) {
        const CamOp *op = ops[k]; const CamParams *p = &op->p;
        size_t tp0 = tpb.n;                                            /* this op's tp records start here (nci_write's capture) */
        if (p->type == CAM_LFACE && p->lf_new && p->file_op_id <= 0) {   /* a new Lathe Face (lathe_newop.c, formats/lathe.md 8.6) */
            uint8_t *lr = NULL; char we[300] = ""; double fp[2][3];
            if (!lfn_record(F, p, &lr, we, sizeof we) || !cam_lface_points(F, op, fp, we, sizeof we)) {
                free(lr); skipped++; NOTE("%s%s (%s)", notes[0] ? "; " : "", cam_op_label(op), we); continue; }
            uint8_t *r = xmalloc(4 + LFN_REC); w32(r, LFN_REC); memcpy(r + 4, lr, LFN_REC); free(lr);
            uint8_t *e = r + 4;
            int32_t op_id = next_op++, op_idn = next_idn++; int64_t op_eptr = next_eptr; next_eptr += 0x3000;
            w64(e + 0x20, (uint64_t)op_eptr); w32(e + 0x30, (uint32_t)now); w32(e + 0x34, (uint32_t)op_idn);
            e[0x48] = (uint8_t)major; e[0x49] = (uint8_t)minor; memcpy(e + 0x4b, g_vstamp + 3, 4);
            w32(e + 0x68, (uint32_t)op_id);
            memset(e + 0x6e, 0, 0x78); snprintf((char *)e + 0x6e, 0x78, "%s", op->name);
            w16(e + 0xe6, (uint16_t)(++maxseq));
            if (F->grp_rec) w32(e + 0x160, (uint32_t)F->grp_prog);   /* OMsetOperationToMachine (as the mill ops above) */
            if (F->ctl_ok && seq_ok(F->ctl_seq[0]) && seq_ok(F->ctl_seq[1])) { w32(e + 0x164, (uint32_t)(int32_t)F->ctl_seq[0]); w32(e + 0x168, (uint32_t)(int32_t)F->ctl_seq[1]); }
            w32(e + 0xe8, 0xffffffffu); w32(e + 0xec, 0xffffffffu);
            e[0xfc] = 1;                                                /* op +0x94 dirty (GetFaceBoundaryFromStock sets it for new points) */
            w64(e + 0x1210, 0);                                         /* op +0x11a8: the defaults file's holder idn names nothing here */
            /* GetFaceBoundaryFromStock: op +0x88 = op +0xd4 = 2 (two point associations), op +0xb68 = 1 */
            w32(e + 0x68 + 0x88, 2); w32(e + 0x68 + 0xd4, 2); e[0x68 + 0xb68] = 1;
            w32(e + 0xf0, 2); w32(e + 0xf4, 0); w32(e + 0xf8, 0);      /* tp count 2, no chains */
            /* the machine group: the base's (my_mgroup) and its toolpath group (as the ops above) */
            if (my_group >= 0) { w32(e + 0x2f4, (uint32_t)my_group); w32(e + 0x5bc, (uint32_t)my_mgroup); }
            /* NOT DECODED: the op's stock copies (op +0xb40.. = LBDRY records of the stock after the op, LATHEPROJECTCLASS::UpdateOpStock
               0x180112400 - the stock after the op is not computed here): written as none (0) */
            NOTE("%s%s: its stock-after-op copies (op +0xb40, UpdateOpStock) are NOT DECODED - not written", notes[0] ? "; " : "", cam_op_label(op));
            put(&opb, r, 4 + LFN_REC);
            if (cap) { cap->recn[k] = LFN_REC; cap->rec[k] = xmalloc(LFN_REC); memcpy(cap->rec[k], e, LFN_REC); }
            free(r);
            /* the two face points: point entities (create_point_ent, in_assoc_mode) + TP kind 1 records 'P' (init_tp_ent(.., 1, 1, 'P'),
               store_ent_assoc) - as millturn.mcam_8 op 1's (measured: P_ID 0x80 records, TP name "unnamed drill point") */
            for (int q = 0; q < 2; q++) {
                int32_t pidn = next_idn++; int64_t pep = next_eptr; next_eptr += 0x200;
                uint8_t pr[4 + 0x80]; memset(pr, 0, sizeof pr); w32(pr, 0x80); uint8_t *pe = pr + 4;
                head(pe, 1, 0, 0, 1, 1, pep, now, pidn, major, minor, 1);
                wd(pe + 0x68, fp[q][0]); wd(pe + 0x70, fp[q][1]); wd(pe + 0x78, fp[q][2]);
                put(&geob, pr, sizeof pr); newgeo++;
                uint8_t tr[4 + 0x388]; memset(tr, 0, sizeof tr); w32(tr, 0x388);
                uint8_t *t = tr + 4;
                head(t, 0x4000, 1, 2, 0x9f, 1, next_eptr, now, next_idn++, major, minor, 1); next_eptr += 0x400;
                w32(t + 0x68, (uint32_t)op_id); w32(t + 0x6c, (uint32_t)pidn); w32(t + 0xc8, (uint32_t)pidn);
                w32(t + 0x70, (uint32_t)now); w32(t + 0x74, 1); w32(t + 0x78, 1);
                w64(t + 0x80, (uint64_t)pep); w64(t + 0xd0, (uint64_t)pep); w64(t + 0x88, (uint64_t)op_eptr);
                w16(t + 0x90, 1);
                wd(t + 0x98, fp[q][0]); wd(t + 0xa0, fp[q][1]); wd(t + 0xa8, fp[q][2]);
                t[0xb0] = 'P';
                wd(t + 0xd8, -1e38); wd(t + 0xe0, 1e38); wd(t + 0x140, -1e38); wd(t + 0x148, -1e38); wd(t + 0x150, -1e38); wd(t + 0x158, -1e38);
                snprintf((char *)t + 0x116, 0x20, "unnamed drill point");
                put(&tpb, tr, sizeof tr);
                refs = xrealloc(refs, sizeof(int32_t) * (nref + 1)); refs[nref++] = pidn;
            }
            if (cap && cap->tp && tpb.n > tp0) { cap->tpn[k] = (uint32_t)(tpb.n - tp0); cap->tp[k] = xmalloc(cap->tpn[k]); memcpy(cap->tp[k], tpb.b + tp0, cap->tpn[k]); }
            added++;
            continue;
        }
        int style, opcode = save_opcode_for(p, &style), tmpl = -1, pts = uses_points(p->type), is3d = is_3d_type(p->type);
        const McwFile *tf = &m;
        if (opcode && p->file_op_id > 0)                               /* an edited Mastercam op: its own record */
            for (int i = 0; i < m.nseg && tmpl < 0; i++) if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x4000 && m.seg[i].sub == 1 && (is3d ? m.seg[i].len >= 4 + 0x2938 : m.seg[i].len == 4 + 0x2938) && (int32_t)rd32(m.d + m.seg[i].off + 4 + 0x68) == p->file_op_id) tmpl = i;   /* full OP_ID only: fields up to +0x1588 are written below; a 3D op with its variable tail (find_op) */
        if (opcode && tmpl < 0 && dlib) { tmpl = find_op(dlib, opcode, style); if (tmpl >= 0) tf = dlib; }
        if (opcode == 0x82 && tmpl < 0 && dlib) { tmpl = find_op(dlib, 1, -1); if (tmpl >= 0) tf = dlib; }   /* FUN_180044170: a contour op (InitOperation(1)) given opcode 0x82 */
        int nitems = pts ? op->nholes : op->nchains;
        /* chains picked on a solid (Chaining dialog Solids mode): Mastercam keeps them as an SldToolPOp in the solid's
           history (Parasolid tags of the picked edges / faces / loops, formats/mcam.md "Solid chains") - NOT DECODED for
           writing, so such an operation is not exported */
        int sldc = 0; for (int c = 0; c < op->nchains; c++) if (op->chains[c].sld) sldc = 1;
        if (sldc) { skipped++; NOTE("%s%s (chains picked on a solid are not exported)", notes[0] ? "; " : "", cam_op_label(op)); continue; }
        /* a 3D op's drive / check references (stage S6, formats/surface_selection.md 2.1 / 2.2): its CamRef list (picked here) as new kind 4
           records; a stored op edited here whose references were not changed (no list: they are its TP kind 4 records, cam_refs_from_tp)
           keeps its own records - Mastercam's parameter edit (edit_operation) leaves them alone, as the tab / point records below */
        int nk4 = 0, nk4copy = 0, anysolidref = 0, nnw3 = 0; RefEnt *re3 = NULL; int *lk3 = NULL; NewSldTp nw3[64]; char why3[300] = "";
        if (is3d && opcode) {
            if (tpsz == 0x398 && !cap) snprintf(why3, sizeof why3, "a Mastercam 2018 part: its operation records are not written here");
            if (!why3[0] && op->nref) {
                re3 = xcalloc((size_t)op->nref, sizeof *re3); lk3 = xcalloc((size_t)op->nref, sizeof *lk3);
                for (int q = 0; q < op->nref && !why3[0]; q++) {
                    const CamRef *cr = &op->ref[q];
                    if (!ref_entity(&m, F, cr, &re3[q], why3, sizeof why3)) break;
                    lk3[q] = cr->link;
                    if (cr->cls != TP_ECLS_SOLID) continue;
                    anysolidref = 1;
                    const Solid *so = cr->index >= 0 && cr->index < F->nsolids ? &F->solids[cr->index] : sldtoolp_solid_by_idn(F, re3[q].idn);
                    if (!so) { snprintf(why3, sizeof why3, "solid %d is not in the part", re3[q].idn); break; }
                    int si = (int)(so - F->solids);
                    if (cr->link > 0) {                                /* its SldToolPOp is in the solid's history (kept as stored) */
                        SldToolP t; char e_[128] = ""; int g = sldtoolp_get(so, cr->link, &t, e_);
                        if (g <= 0) { snprintf(why3, sizeof why3, "solid %d has no toolpath op with link %d%s%s", re3[q].idn, cr->link, e_[0] ? ": " : "", e_); break; }
                        sldtoolp_free(&t); continue;
                    }
                    /* picked here: a whole body (a CamRef names a solid, no faces; single face picks need each face's McEptr id - NOT
                       DECODED, formats/surface_selection.md 7.4 / 7.7, decision N13-6) - a new SldToolPOp in its history */
                    int lnk, oid; char w_[220];
                    NewSldTp all[128]; int na = 0;
                    for (int u = 0; u < nnewsld && na < 64; u++) all[na++] = newsld[u];
                    for (int u = 0; u < nnw3 && na < 128; u++) all[na++] = nw3[u];
                    if (nnw3 >= 64 || nnewsld + nnw3 >= 64) { snprintf(why3, sizeof why3, "more than 64 new solid references in one save (a limit here)"); break; }
                    if (!sld_new_link(so, si, all, na, &lnk, &oid, w_, sizeof w_)) { snprintf(why3, sizeof why3, "solid %d: %s", re3[q].idn, w_); break; }
                    nw3[nnw3].solid = si; nw3[nnw3].link = lnk; nw3[nnw3].type = (int)(cr->flags & 0x24); nw3[nnw3].op_id = oid; nnw3++;
                    lk3[q] = lnk;
                }
                nk4 = op->nref;
            } else if (!why3[0] && p->file_op_id > 0) {
                for (int i = 0; i < m.nseg; i++) {
                    const McwSeg *sg = &m.seg[i];
                    if (sg->kind != SEG_ENT || sg->cls != 0x4000 || sg->sub != 2 || sg->len < 4 + 0x100) continue;
                    const uint8_t *t = m.d + sg->off + 4;
                    if ((int32_t)rd32(t + 0x68) != p->file_op_id || (int16_t)rd16(t + 0x90) != 4) continue;
                    int32_t ri = (int32_t)rd32(t + 0x6c);
                    for (int q = 0; q < F->nents && !why3[0]; q++) if (F->ents[q].idn == ri && !F->ents[q].type && !F->ents[q].created)
                        snprintf(why3, sizeof why3, "its drive / check entity %d was deleted", ri);
                    if (rd32(t + 0x78) == TP_ECLS_SOLID) anysolidref = 1;
                    nk4copy++;
                }
                nk4 = nk4copy;
            }
            nitems += nk4;
        }
        if (!why3[0] && op->nrface > 0)   /* decision N13-6: a face picked here needs its McEptr id in the SldToolPOp (formats/surface_selection.md 7.7) - NOT DECODED */
            snprintf(why3, sizeof why3, "solid faces were picked (their McEptr ids are NOT DECODED, decision N13-6); pick whole solids to save this op");
        if (why3[0]) { skipped++; NOTE("%s%s (NOT PORTED: %s)", notes[0] ? "; " : "", cam_op_label(op), why3); free(re3); free(lk3); continue; }
        if (!opcode || tmpl < 0 || (!nitems && p->type != CAM_FACE && !is3d)) {
            skipped++;
            NOTE("%s%s (%s)", notes[0] ? "; " : "", cam_op_label(op), !opcode ? "type not exported yet" : tmpl < 0 ? (dlib ? "no operation of this type in the defaults library" : "no operation defaults library (config 2303)") : "no chains / points");
            free(re3); free(lk3); continue;
        }
        for (int u = 0; u < nnw3; u++) newsld[nnewsld++] = nw3[u];
        uint8_t *r = xmalloc(tf->seg[tmpl].len); memcpy(r, tf->d + tf->seg[tmpl].off, tf->seg[tmpl].len);
        uint8_t *e = r + 4;
        int32_t op_id = next_op++, op_idn = next_idn++; int64_t op_eptr = next_eptr; next_eptr += 0x3000;
        /* the separate Tab Cutoff (FUN_180044170, decomp/mcmill_ctour.c l.4000-4004): the 0x82 op's opcode (+4 = record 0x6c) and op +0xd0 = record
           0x138 = its parent's op id (CDependentOperations::DetectChildOperationIDs finds the children by it); the parent is written before it */
        if (p->ct_uid > 0 && nuidmap < (int)(sizeof uidmap / sizeof uidmap[0])) { uidmap[nuidmap][0] = p->ct_uid; uidmap[nuidmap][1] = op_id; nuidmap++; }
        if (opcode == 0x82) { w16(e + 0x6c, 0x82); int32_t par = 0; for (int u = 0; u < nuidmap; u++) if (uidmap[u][0] == p->ct_parent_uid) par = uidmap[u][1];
                            if (p->ct_parent_uid < 0) par = -p->ct_parent_uid;   /* a stored parent (its record copied with its id) */
                            w32(e + 0x138, (uint32_t)par); }
        w64(e + 0x20, (uint64_t)op_eptr); w32(e + 0x30, (uint32_t)now); w32(e + 0x34, (uint32_t)op_idn);
        e[0x48] = (uint8_t)major; e[0x49] = (uint8_t)minor; memcpy(e + 0x4b, g_vstamp + 3, 4);   /* the writer's stamp (head) */
        int keep_seq = p->file_op_id > 0 && tf == &m && (int32_t)rd32(e + 0x68) == p->file_op_id;
        /* B2: a new Multisurface Pocket (op 131) is computed from the default record with its pocket block un-shifted (user decision
           T-1, cam_fileop.c cam_default_op_record: 0x13c8.. moved up by 8 + the 1312 page's init forcing); the shipped template copied
           above still holds the shifted block, so the record the op was computed from is written (0x1300..0x165f) - else a reopened op
           reads other values at the code's offsets than it was made with */
        if (!keep_seq && p->type == CAM_SRMPOCKET && tf->seg[tmpl].len >= 4 + 0x1660) {
            uint32_t dn = 0; const uint8_t *dr = cam_default_op_record(CAM_SRMPOCKET, mcam_is_metric(F), &dn);
            if (dr && dn >= 0x1660) memcpy(e + 0x1300, dr + 0x1300, 0x1660 - 0x1300);
        }
        w32(e + 0x68, (uint32_t)op_id);
        memset(e + 0x6e, 0, 0x78);                                      /* comment: 0x6e .. 0xe5 (an edited op without one keeps none) */
        if (!(keep_seq && !strcmp(op->name, cam_type_label(p->type)))) snprintf((char *)e + 0x6e, 0x78, "%s", op->name);
        if (!keep_seq) w16(e + 0xe6, (uint16_t)(++maxseq));          /* an edited op keeps its place in the tree */
        /* OMsetOperationToMachine (mastercam.dll @1803259d0, decomp/mastercam_opsettomachine.c), run by InitOperation
           (mccore_ops.c l.2437) for a new op: op +0xf8 = the machine group's +0x1178 (program #), op +0xfc = (int)StartSeqNum,
           op +0x100 = (int)SeqNumInc (the control's nc_output_ctrl +0 / +8, mdef_grpprops_accessors.c) = record +0x160 /
           +0x164 / +0x168; an edited Mastercam op keeps its own. Ours: written with the new op's record (Mastercam: when the op
           is created; the same values unless Program # was changed in between). No group / control read (a new part): the
           defaults library op's values stay. The post does not read them (nc_post.c numbers N100 step 2). */
        if (!keep_seq && F->grp_rec) w32(e + 0x160, (uint32_t)F->grp_prog);
        if (!keep_seq && F->ctl_ok && seq_ok(F->ctl_seq[0]) && seq_ok(F->ctl_seq[1])) { w32(e + 0x164, (uint32_t)(int32_t)F->ctl_seq[0]); w32(e + 0x168, (uint32_t)(int32_t)F->ctl_seq[1]); }
        w32(e + 0xe8, 0xffffffffu); w32(e + 0xec, 0xffffffffu);
        e[0xfc] = 1;                                                    /* dirty: Mastercam regenerates the toolpath */
        /* the op's planes: op_view x3 at record 0x5d8 Tplane / 0x688 Cplane / 0x738 WCS (cam2d.c cam_planes_to_record,
           formats/mcam_write.md 11). A new op: InitOperation (mccore 0x180046660, decomp/mccore_ops.c l.1770) replaces the
           defaults library op's views with the active planes (MastercamPlanes::InitOperationViewsToActivePlanes ->
           Plane::Convert: the plane's work offset, -1) and then sets an Automatic Tplane offset from
           MastercamPlanes::GetMatchingWorkOffset (BuildWorkOffsetMap - NOT DECODED; every new op of the user parts,
           Dynamic ones included, stores -1 although the defaults library's Dynamic ops hold 0): the op's own planes are
           written. An edited Mastercam op (its own record): rewritten only when its planes changed (the same bytes
           otherwise). */
        {
            CamParams st; memset(&st, 0, sizeof st);
            int same = keep_seq && cam_planes_from_record(e, (uint32_t)(tf->seg[tmpl].len - 4), &st, NULL, 0)
                       && st.tplane == p->tplane && st.cplane == p->cplane && st.wcs == p->wcs
                       && !memcmp(st.torg, p->torg, sizeof st.torg) && !memcmp(st.corg, p->corg, sizeof st.corg) && !memcmp(st.worg, p->worg, sizeof st.worg)
                       && st.wofs == (p->wofs_manual && p->wofs >= 0 ? p->wofs : -1) && st.wwofs == p->wwofs && st.cwofs == p->cwofs;
            if (!same) cam_planes_to_record(e, p);
        }
        if (!keep_seq) w64(e + 0x1210, 0);   /* op +0x11a8: ent_idn of its holder record (ASSOC 31) in the template's file; none
                                                here (HolderGetHolder fails -> TlAssembly::CreateNewFromOp's default holder) */
        /* an edited Mastercam op: its tab records (TP kind 30) are copied from the original */
        /* an edited op whose stored tabs were not taken here (cam_fileop: tabs from its tab records only): kept as stored,
           with their records */
        int keep_tabs = keep_seq && p->type == CAM_CONTOUR && p->tabs == 0 && e[0x1380];
        if (keep_tabs) NOTE("%s%s: its tabs are kept as stored (not used for the toolpath here)", notes[0] ? "; " : "", cam_op_label(op));
        int ntabrec = 0;
        if (keep_seq && (p->tabs > 0 || keep_tabs))
            for (int i = 0; i < m.nseg; i++) if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x4000 && m.seg[i].sub == 2 && m.seg[i].len >= 4 + 0x100
                && (int32_t)rd32(m.d + m.seg[i].off + 4 + 0x68) == p->file_op_id && (int16_t)rd16(m.d + m.seg[i].off + 4 + 0x90) == 30) ntabrec++;
        int ntp = ntabrec + nk4; for (int c = 0; c < op->nchains; c++) ntp += 1 + (op->chains[c].solid_idn > 0 ? 1 : op->chains[c].n);
        int nptrec = 0, pl3 = p->type == CAM_SRPLUNGE;   /* Rough Plunge: its two grid corners are the op's point TP records (kind 1;
                                                                     formats/surf_classic2.md 2.9 step 2; user decision T-2), with its
                                                                     kind 4 references and chains */
        if (keep_seq && (pts || pl3))
            for (int i = 0; i < m.nseg; i++) if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x4000 && m.seg[i].sub == 2 && m.seg[i].len >= 4 + 0x100
                && (int32_t)rd32(m.d + m.seg[i].off + 4 + 0x68) == p->file_op_id) { int k_ = (int16_t)rd16(m.d + m.seg[i].off + 4 + 0x90); if (k_ == 1 || k_ == 27) nptrec++; }
        if (nptrec != op->nholes) nptrec = 0;                       /* holes changed here: written fresh */
        int nnewtab = pts ? 0 : new_tab_recs(F, op, p, ntabrec); ntp += nnewtab;
        if (pts) ntp = op->nholes; else if (pl3) ntp += op->nholes;
        w32(e + 0xf0, (uint32_t)ntp); w32(e + 0xf4, (uint32_t)(pts ? 0 : op->nchains));
        if (!keep_seq) w32(e + 0xf8, (uint32_t)(pts ? 0 : op->nchains));   /* second chain count (NOT DECODED): an edited op keeps its own */
        if (my_group >= 0) { w32(e + 0x2f4, (uint32_t)my_group); w32(e + 0x5bc, (uint32_t)my_mgroup); }
        else { my_group = (int32_t)rd32(e + 0x2f4); my_mgroup = (int32_t)rd32(e + 0x5bc); }
        /* heights. Read back as cam_fileop.c params_from_record does (P10027 top +0x2e8 / inc +0x2f0 relative to the chain
           Z; P10029 depth +0x1a0 / inc +0x1a8 relative to the chain Z, plus P10068 stock +0x888; P10024 feed plane +0x190 /
           inc +0x198 relative to the top; clearance +0x170 / inc +0x178 / on +0x179 and retract +0x180 / +0x188 / +0x189
           relative to the top); a value whose stored form already gives ours is kept (abs / inc as stored), else it is
           written absolute (feed plane: incremental) */
        /* tool: the op's op_tool_info block (+0x348 .. +0x5d0), see "tools" above */
        {   Tool tool; const Tool *tl = NULL;
            if (p->tool_slot > 0) for (int k = 0; k < F->ntools && !tl; k++) if (F->tools[k].slot == p->tool_slot && F->tools[k].num == p->tool_num) tl = &F->tools[k];
            if (!tl) tl = mcam_tool(F, p->tool_num);
            if (tl) { tool = *tl; tool.holder_rz = NULL; tool.holder_n = 0; }
            else {   /* no such tool here: Mastercam's default tool of the type (DatabaseDefaults.tooldb, tool_new) */
                tool_new(&tool, p->tool_type ? p->tool_type : 10, p->tool_dia, p->tool_num, mcam_is_metric(F));
                free(tool.holder_rz); tool.holder_rz = NULL; tool.holder_n = 0;
                tool.rpm = p->rpm; tool.feed = p->feed; tool.plunge = p->plunge_feed;
                NOTE("%sT%d: not in the part or a library, Mastercam's default %s written", notes[0] ? "; " : "", p->tool_num, tool_type_name(tool.type));
            }
            int32_t tslot = (int32_t)rd32(e + 0x5ac);
            if (!slot_owner(F, pslots, npslots, tl)) {                  /* a new tool: a TL_ID record with a free slot */
                int s_ = -1;
                for (int k = 0; k < nnewtl; k++) if (newtl[k].num == tool.num && newtl[k].from == tl && tl) s_ = newtl[k].slot;
                if (s_ < 0) {
                    s_ = open_slot(pslots, npslots);
                    add_slot(&pslots, &npslots, s_, tool.num, tool.name);
                    newtl = xrealloc(newtl, sizeof *newtl * (nnewtl + 1)); newtl[nnewtl].num = tool.num; newtl[nnewtl].from = tl; newtl[nnewtl++].slot = s_;
                    tool.slot = s_;
                    uint8_t tr[4 + 0x700]; memset(tr, 0, sizeof tr); w32(tr, 0x700);
                    uint8_t *t = tr + 4;   /* header as the user parts' 62 TL_ID records (sel 0x0101, colour 0x9f, level 1) */
                    head(t, 0x4000, 1, 3, 0x9f, 1, next_eptr, now, next_idn++, major, minor, 0); next_eptr += 0x800;
                    tool_tp_tool(&tool, t + 0x68);
                    w32(t + 0x68 + 0x274, rd32(e + 0x5bc));   /* op_tool_info +0x274 machine group (TlTool +0x138) */
                    put(&tlb, tr, sizeof tr); ntlw++;
                    if (cap) { cap->tl[k] = xmalloc(0x700); memcpy(cap->tl[k], tr + 4, 0x700); cap->tln[k] = 0x700; }
                    else { tlc[k] = xmalloc(0x700); memcpy(tlc[k], tr + 4, 0x700); }
                }
                tool.slot = s_;
            }
            uint8_t blk[0x288];
            if (keep_seq && tslot == tool.slot && tl) {                 /* an edited op, same tool: its own block */
                memcpy(blk, e + 0x348, 0x288);
                for (int k = 0; k < nted; k++) if (ted[k].now == tl && (ted[k].res > 0 || ted[k].res == PT_NOSLOT)) merge_edit(blk, 0x288, ted[k].ob_now, ted[k].ob_base);
            }
            else { tool_op_info(&tool, blk); memcpy(blk + 0x274, e + 0x5bc, 4); }   /* machine group: the op's (+0x5bc) */
            /* the op's own values (op = tool block, but for these: 189/189 user ops) */
            wd(blk + 0x30, p->feed); wd(blk + 0x38, p->plunge_feed); w32(blk + 0x48, (uint32_t)p->rpm);
            /* regen-consistency 2026-11-19: and the retract rate the engines feed retracts at (+0x40 = record 0x388, op +0x320;
               cam_fileop.c reads it into mc_rrate; cam2d.c / cam2d_ops.c / cam2d_face.c retract moves) - the tool's value went in
               when the op had another (a new op's from its default record) */
            if (p->mc_rrate > 0 && fabs(rdd(blk + 0x40) - p->mc_rrate) > 1e-9) wd(blk + 0x40, p->mc_rrate);
            if ((p->coolant != 0) != (rd16(blk + 0x50) == 16)) w16(blk + 0x50, (uint16_t)(p->coolant ? 16 : 8));   /* TlCoolant.OldStyle 16 Flood / 8 Off */
            memcpy(e + 0x348, blk, 0x288);
        }
        op_fields_to_record(F, op, e, tf->seg[tmpl].len, keep_seq, ntabrec, keep_tabs);
        uint8_t tab_1338[8]; memcpy(tab_1338, e + 0x13a0, 8);   /* op +0x1338 for the tab records (the record buffer is freed before the chains) */
        uint8_t tab_blk[0x30]; memcpy(tab_blk, e + 0x1388, 0x30);   /* record 0x1388..0x13b7 (op +0x1320..): edit_operation's tab rewrite */
        int tab_rw = p->type == CAM_CONTOUR && e[0x1380] && !e[0x1381] && cam_tabprm_edited(F, p), tab_ow = e[0x13f1];
        /* store_ent_assoc of a kind 4 record naming a solid (tp_is_solid) runs FUN_1800c1250(op, 1, 1) (decomp/mccore_tooladd.c l.7562-7565;
           mccore 0x1800c1250: op +0xe9 = record +0x151 = 1, UpdateListAndDB); [measured] +0x151 is 1 in the 20 X9 / 2017 corpus ops with a
           solid kind 4 record, 0 in the other 444. A new reference list: FUN_18001bcf0 stores the records and rewrites the op with +0x94 = 1,
           +0x97 = 0 (decomp/mcmsurf_all.c l.19917-19925, 20087-20089) - record +0xfc / +0xff */
        if (is3d && anysolidref && tf->seg[tmpl].len >= 4 + 0x152) e[0x151] = 1;
        if (is3d && op->nref && tf->seg[tmpl].len >= 4 + 0x100) e[0xff] = 0;
        size_t rec_at = opb.n;                                          /* the record's place in opb (its NCI section: below) */
        put(&opb, r, tf->seg[tmpl].len);
        if (cap) { cap->recn[k] = tf->seg[tmpl].len - 4; cap->rec[k] = xmalloc(cap->recn[k]); memcpy(cap->rec[k], r + 4, cap->recn[k]); }
        free(r);
        /* ---- 3D references (kind 4): before the op's chains, as msurf_assoc stores them (drive / check FUN_18001bcf0, then the
           containment chains FUN_18001cff0; formats/surface_selection.md 1.3 steps 4 / 5) ---- */
        for (int q = 0; is3d && op->nref && q < nk4; q++) {
            put_k4(&tpb, tpsz, F, &re3[q], op->ref[q].flags, lk3[q], op_id, op_eptr, next_eptr, now, next_idn++, major, minor); next_eptr += 0x400;
            refs = xrealloc(refs, sizeof(int32_t) * (nref + 1)); refs[nref++] = re3[q].idn;
        }
        for (int i = 0; is3d && !op->nref && nk4copy && i < m.nseg; i++) {   /* an edited stored op: its own kind 4 records, new ids (as the points) */
            const McwSeg *sg = &m.seg[i];
            if (sg->kind != SEG_ENT || sg->cls != 0x4000 || sg->sub != 2 || sg->len < 4 + 0x100) continue;
            if ((int32_t)rd32(m.d + sg->off + 4 + 0x68) != p->file_op_id || (int16_t)rd16(m.d + sg->off + 4 + 0x90) != 4) continue;
            uint8_t *tr = xmalloc(sg->len); memcpy(tr, m.d + sg->off, sg->len);
            uint8_t *t = tr + 4;
            w64(t + 0x20, (uint64_t)next_eptr); next_eptr += 0x400; w32(t + 0x34, (uint32_t)next_idn++);
            w32(t + 0x68, (uint32_t)op_id); w64(t + 0x88, (uint64_t)op_eptr);
            int32_t ri = (int32_t)rd32(t + 0x6c); if (ri) { refs = xrealloc(refs, sizeof(int32_t) * (nref + 1)); refs[nref++] = ri; }
            put(&tpb, tr, sg->len); free(tr);
        }
        free(re3); free(lk3);
        /* ---- points (kind 1) ---- */
        for (int i = 0; nptrec && i < m.nseg; i++)                     /* the original point records */
            if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x4000 && m.seg[i].sub == 2 && m.seg[i].len >= 4 + 0x100
                && (int32_t)rd32(m.d + m.seg[i].off + 4 + 0x68) == p->file_op_id) {
                int k_ = (int16_t)rd16(m.d + m.seg[i].off + 4 + 0x90); if (k_ != 1 && k_ != 27) continue;
                uint8_t *tr = xmalloc(m.seg[i].len); memcpy(tr, m.d + m.seg[i].off, m.seg[i].len);
                uint8_t *t = tr + 4;
                w64(t + 0x20, (uint64_t)next_eptr); next_eptr += 0x400; w32(t + 0x34, (uint32_t)next_idn++);
                w32(t + 0x68, (uint32_t)op_id); w64(t + 0x88, (uint64_t)op_eptr);
                int32_t ri = (int32_t)rd32(t + 0x6c); if (ri) { refs = xrealloc(refs, sizeof(int32_t) * (nref + 1)); refs[nref++] = ri; }
                put(&tpb, tr, m.seg[i].len); free(tr);
            }
        for (int h = 0; (pts || pl3) && !nptrec && h < op->nholes; h++) {
            const Ent *se = NULL;
            for (int i = 0; i < F->nents && !se; i++) {
                const Ent *en = &F->ents[i];
                if (en->type == E_ARC && fabs(en->c.x - op->holes[h].x) < 1e-6 && fabs(en->c.y - op->holes[h].y) < 1e-6) se = en;
                if (en->type == E_POINT && fabs(en->a.x - op->holes[h].x) < 1e-6 && fabs(en->a.y - op->holes[h].y) < 1e-6) se = en;
            }
            uint8_t tr[4 + 0x388]; memset(tr, 0, sizeof tr); w32(tr, 0x388);
            uint8_t *t = tr + 4;
            head(t, 0x4000, 1, 2, 0x9f, 1, next_eptr, now, next_idn++, major, minor, 1); next_eptr += 0x400;
            w32(t + 0x68, (uint32_t)op_id);
            w32(t + 0x6c, (uint32_t)(se ? se->idn : 0)); w32(t + 0xc8, (uint32_t)(se ? se->idn : 0));
            w32(t + 0x70, (uint32_t)(se ? se->ctime : 0)); w32(t + 0x74, 1); w32(t + 0x78, se ? se->cls : 1);
            w64(t + 0x80, (uint64_t)(se ? se->eptr : 0)); w64(t + 0xd0, (uint64_t)(se ? se->eptr : 0)); w64(t + 0x88, (uint64_t)op_eptr);
            w16(t + 0x90, 1);
            wd(t + 0x98, op->holes[h].x); wd(t + 0xa0, op->holes[h].y); wd(t + 0xa8, se ? (se->type == E_ARC ? se->c.z : se->a.z) : 0);
            /* init_tp_ent (mccore 0x18004d5a0, decomp/mccore_tptools.c) kind 1: +0x74 = 1, +0x78 the source's class, +0xb0 the
               point-type char (toupper; 'C' in all 14 user drill points), +0xb8 / +0xc0 two values (0), +0xc8 / +0xd0 source
               idn / eptr, +0xd8 -1e38, +0xe0 1e38, +0x140..+0x158 four -1e38, +0x160 i16 -1. The name "Point n" at +0x116
               is the user parts' (init_tp_ent leaves it empty; who names it is NOT DECODED) */
            t[0xb0] = 'C';
            wd(t + 0xd8, -1e38); wd(t + 0xe0, 1e38); wd(t + 0x140, -1e38); wd(t + 0x148, -1e38); wd(t + 0x150, -1e38); wd(t + 0x158, -1e38);
            w16(t + 0x160, 0xffff);
            snprintf((char *)t + 0x116, 0x20, "Point %d", h + 1);
            put(&tpb, tr, sizeof tr);
            if (se) { refs = xrealloc(refs, sizeof(int32_t) * (nref + 1)); refs[nref++] = se->idn; }
        }
        /* ---- chains: header (kind 6) + one record per entity (kind 7) ---- */
        for (int c = 0; !pts && c < op->nchains; c++) {
            const Chain *ch = &op->chains[c];
            /* chain direction: a chain is stored as chained (mccore add_chain, config 6001 / 6002: formats/chaining.md) and
               Mastercam's contour runs along it with the compensation left / right of it (+0x5d2; side_rel ops, checked
               against Mastercam's NCI: formats/mcam.md "TP_BND +0xa8", cam2d.c chain_offset). Other operation types keep
               the chained direction. A contour whose side is outside / inside (not side_rel) was cut by cam2d.c
               chain_offset CW outside / CCW inside; written with compensation left (+0x5d2 = 0, above), the chain is
               given that direction so Mastercam's contour runs the same way; an open chain cut on the right (side 2)
               is reversed for the same reason */
            int reverse = 0;
            if (p->type == CAM_CONTOUR && !p->side_rel && p->side != SIDE_ON) {
                if (ch->closed) { double a = chain_area(F, ch); reverse = p->side == SIDE_OUTSIDE ? a > 0 : a < 0; }
                else reverse = p->side == SIDE_INSIDE;
            }
            int64_t chn_eptr = next_eptr; next_eptr += 0x400;
            uint8_t hr[4 + 0x388]; memset(hr, 0, sizeof hr); w32(hr, 0x388);
            uint8_t *hh = hr + 4;
            head(hh, 0x4000, 1, 2, 0x9f, 1, chn_eptr, now, next_idn++, major, minor, 0);
            w32(hh + 0x68, (uint32_t)op_id); w64(hh + 0x80, (uint64_t)next_eptr); w64(hh + 0x88, (uint64_t)op_eptr);
            int solid = ch->solid_idn > 0;
            w16(hh + 0x90, 6); w32(hh + 0x98, (uint32_t)(c + 1)); w32(hh + 0x9c, (uint32_t)(solid ? 1 : ch->n)); hh[0xa1] = (uint8_t)ch->closed;
            hh[0xe0] = ch->avoid ? 4 : (unsigned char)ch->role;                               /* chain role: 0 machining region, 4 avoidance */
            snprintf((char *)hh + 0xa4, 0x20, solid ? "Solid Chain %d" : "Chain %d", c + 1);
            put(&tpb, hr, sizeof hr);
            /* the op's tabs on this chain as TP kind 30 records (mccore save_chain with the tab_list_rec list, formats/contour_100_plan.md 2;
               layout and order as Mastercam's: BRACKET4 op 15 - chain header, its tabs, then the chained entities): +0x6c / +0x80 the
               entity the tab is on, +0x98 chain number, +0x9c the entity's index in the chain, +0xa0 the chain header's eptr, then the
               0x98-byte tab_list_rec of FUN_18021ae10 from +0x98: +0xa8 point, +0xc8 t from the entity's own start, +0xd0 width (op
               +0x1320 = record 0x1388), +0xd8 thickness (0x1390), +0xe0 Full (0x1398), +0xe4 Ramp (0x139c), +0xe8 op +0x1338 (0x13a0),
               +0xf8 ramp angle (0x13b0). Not for an edited Mastercam op (its own records are copied below). */
            if (!solid && nnewtab && ch->closed) {
                for (int t = 0; t < p->ntab_xy; t++) {
                    int bc, bj; double bu;
                    if (!tab_assign(F, op, p->tab_xy[2 * t], p->tab_xy[2 * t + 1], &bc, &bj, &bu)) continue;
                    if (bc != c || bj < 0) continue;
                    int jw = reverse ? ch->n - 1 - bj : bj;              /* its index in the chain as written below */
                    const Ent *en = &F->ents[ch->ent[bj]];
                    uint8_t tr[4 + 0x388]; memset(tr, 0, sizeof tr); w32(tr, 0x388);
                    uint8_t *b = tr + 4;
                    head(b, 0x4000, 1, 2, 0x9f, 1, next_eptr, now, next_idn++, major, minor, 1); next_eptr += 0x400;
                    w32(b + 0x68, (uint32_t)op_id); w32(b + 0x6c, (uint32_t)en->idn); w64(b + 0x80, (uint64_t)en->eptr); w64(b + 0x88, (uint64_t)op_eptr);
                    w16(b + 0x90, 30); w32(b + 0x98, (uint32_t)(c + 1)); w32(b + 0x9c, (uint32_t)(jw + 1)); w64(b + 0xa0, (uint64_t)chn_eptr);
                    double px, py; if (en->type == E_LINE) { px = en->a.x + (en->b.x - en->a.x) * bu; py = en->a.y + (en->b.y - en->a.y) * bu; }
                    else { double aa = en->a0 + en->sw * bu; px = en->c.x + en->r * cos(aa); py = en->c.y + en->r * sin(aa); }
                    wd(b + 0xa8, px); wd(b + 0xb0, py); wd(b + 0xb8, en->a.z); wd(b + 0xc8, bu);
                    wd(b + 0xd0, p->tab_w); wd(b + 0xd8, p->tab_h); b[0xe0] = (uint8_t)(p->tab_full != 0); w16(b + 0xe4, (uint16_t)(p->tab_ramp != 0));
                    memcpy(b + 0xe8, tab_1338, 8); wd(b + 0xf8, p->tab_ang);
                    put(&tpb, tr, sizeof tr);
                }
            }
            if (solid) {                                                /* "Solid Chain": one record naming the solid */
                int si = -1; for (int i = 0; i < m.nseg; i++) if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x10000 && m.seg[i].id == ch->solid_idn) si = i;
                const uint8_t *se = si >= 0 ? m.d + m.seg[si].off + 4 : NULL;
                uint8_t br[4 + 0x388]; memset(br, 0, sizeof br); w32(br, 0x388);
                uint8_t *b = br + 4;
                head(b, 0x4000, 1, 2, 0x9f, 1, next_eptr, now, next_idn++, major, minor, 1); next_eptr += 0x400;
                w32(b + 0x68, (uint32_t)op_id); w32(b + 0x6c, (uint32_t)ch->solid_idn); w32(b + 0x70, se ? rd32(se + 0x30) : 0);
                w32(b + 0x74, 0x10000); w32(b + 0x78, 0x10000); w64(b + 0x80, se ? rd64(se + 0x20) : 0); w64(b + 0x88, (uint64_t)op_eptr);
                w16(b + 0x90, 7); w32(b + 0x98, (uint32_t)(c + 1)); w32(b + 0x9c, 2); w64(b + 0xa0, (uint64_t)chn_eptr);
                put(&tpb, br, sizeof br);
                refs = xrealloc(refs, sizeof(int32_t) * (nref + 1)); refs[nref++] = ch->solid_idn;
                continue;
            }
            for (int j = 0; j < ch->n; j++) {
                int jj = reverse ? ch->n - 1 - j : j;
                const Ent *en = &F->ents[ch->ent[jj]];
                uint8_t br[4 + 0x388]; memset(br, 0, sizeof br); w32(br, 0x388);
                uint8_t *b = br + 4;
                head(b, 0x4000, 1, 2, 0x9f, 1, next_eptr, now, next_idn++, major, minor, 1); next_eptr += 0x400;
                w32(b + 0x68, (uint32_t)op_id); w32(b + 0x6c, (uint32_t)en->idn); w32(b + 0x70, (uint32_t)en->ctime);
                w32(b + 0x74, en->cls); w32(b + 0x78, en->cls); w64(b + 0x80, (uint64_t)en->eptr); w64(b + 0x88, (uint64_t)op_eptr);
                w16(b + 0x90, 7); w32(b + 0x98, (uint32_t)(c + 1)); w32(b + 0x9c, (uint32_t)(j + 1)); w64(b + 0xa0, (uint64_t)chn_eptr);
                b[0xa8] = (uint8_t)(reverse ? !ch->rev[jj] : ch->rev[jj]);
                put(&tpb, br, sizeof br);
                refs = xrealloc(refs, sizeof(int32_t) * (nref + 1)); refs[nref++] = en->idn;
            }
        }
        for (int i = 0; ntabrec && i < m.nseg; i++)                    /* tabs of an edited Mastercam op */
            if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x4000 && m.seg[i].sub == 2 && m.seg[i].len >= 4 + 0x100
                && (int32_t)rd32(m.d + m.seg[i].off + 4 + 0x68) == p->file_op_id && (int16_t)rd16(m.d + m.seg[i].off + 4 + 0x90) == 30) {
                uint8_t *tr = xmalloc(m.seg[i].len); memcpy(tr, m.d + m.seg[i].off, m.seg[i].len);
                uint8_t *t = tr + 4;
                w64(t + 0x20, (uint64_t)next_eptr); next_eptr += 0x400; w32(t + 0x34, (uint32_t)next_idn++);
                w32(t + 0x68, (uint32_t)op_id); w64(t + 0x88, (uint64_t)op_eptr);
                /* mccore edit_operation (decomp/mccore_edit_operation.c l.3021-3046): the tab block changed, Manual, tabs on -> a tab not
                   edited (+0x110 = 0), or every tab with "Overwrite edited tabs" 0x13f1, takes the op's width op +0x1320, thickness
                   +0x1328, Full +0x1330 (4 bytes), Ramp +0x1334 (4), +0x1338 and ramp angle +0x1348 (rewrite_ent_assoc); the edited
                   flag stays */
                if (tab_rw && m.seg[i].len >= 4 + 0x111 && (t[0x110] == 0 || tab_ow)) {
                    memcpy(t + 0xd0, tab_blk + 0x00, 8); memcpy(t + 0xd8, tab_blk + 0x08, 8); memcpy(t + 0xe0, tab_blk + 0x10, 4);
                    memcpy(t + 0xe4, tab_blk + 0x14, 4); memcpy(t + 0xe8, tab_blk + 0x18, 8); memcpy(t + 0xf8, tab_blk + 0x28, 8);
                }
                put(&tpb, tr, m.seg[i].len); free(tr);
            }
        if (cap && cap->tp && tpb.n > tp0) { cap->tpn[k] = (uint32_t)(tpb.n - tp0); cap->tp[k] = xmalloc(cap->tpn[k]); memcpy(cap->tp[k], tpb.b + tp0, cap->tpn[k]); }
        /* its NCI: an op whose toolpath was computed here and not changed since (formats/nci_save.md 3: Mastercam clears op +0x94 only
           when the op's bnci_section run ended with its -3 record, mccore FUN_180103fb0; a dirty op (CamOp.dirty, an edit not computed)
           has no NCI saved, save_db l.1451) */
        if (!cap && op->nm > 0 && !op->dirty && (!op->gen_units || op->gen_units - 1 == (mcam_is_metric(F) != 0))) {
            npend = xrealloc(npend, sizeof *npend * (size_t)(nnpend + 1));
            NciPend *np = &npend[nnpend++]; np->k = k; np->rec_at = rec_at; np->recn = (uint32_t)(tf->seg[tmpl].len - 4); np->tp = NULL; np->tpn = 0;
            if (tpb.n > tp0) { np->tpn = (uint32_t)(tpb.n - tp0); np->tp = xmalloc(np->tpn); memcpy(np->tp, tpb.b + tp0, np->tpn); }
        }
        added++;
    }
    if (cap) {                                                         /* nci_write: the records only, nothing is written */
        free(pslots); free(newtl); for (int k = 0; k < nted; k++) free(ted[k].base.holder_rz);
        free(ted); mcw_close(&m); free(geob.b); free(opb.b); free(tpb.b); free(tlb.b); free(refs); free(tlm_new); free(lvl_new); free(mtnew.b); free(noteb.b);
        snprintf(msg, msgn, "%s", notes);
        return 1;
    }
    int anydel = 0; for (int k = 0; k < F->nents; k++) if (!F->ents[k].type && !F->ents[k].created && F->ents[k].idn) anydel = 1;
    /* operations of the base part no longer in the list (deleted / cut in the Toolpaths manager: COpMgrTree::OnDeleteKey's soft delete,
       made permanent by DeleteOperationsFromDb - the op's records go; formats/align_ops.md 1.4-1.5) */
    int32_t *gone = NULL; int ngone = 0;
    for (int i = 0; !fresh && i < m.nseg; i++) {
        const McwSeg *sg = &m.seg[i];
        if (sg->kind != SEG_ENT || sg->cls != 0x4000 || sg->sub != 1 || sg->len < 4 + 0xe8) continue;   /* the OP_ID records mcam_load reads */
        int32_t id = (int32_t)rd32(m.d + sg->off + 4 + 0x68); int in = 0;
        for (int k = 0; k < F->nops && !in; k++) in = F->ops[k].id == id && F->ops[k].raw;
        for (int k = 0; k < nops && !in; k++) in = ops[k]->p.file_op_id == id;   /* replaced by an edited version: dropped below anyway */
        if (!in) { gone = xrealloc(gone, sizeof(int32_t) * (size_t)(ngone + 1)); gone[ngone++] = id; anydel = 1; }
    }
    int nrev = 0; for (int k = 0; k < F->nops; k++) if (F->ops[k].nci) nrev++;   /* NCI rewritten here (Reverse toolpath) */
    free(pslots); free(newtl);
    for (int k = 0; k < nted; k++) free(ted[k].base.holder_rz);
    free(ted);
    if (toolnote[0]) NOTE("%s%s", notes[0] ? "; " : "", toolnote);
    if (tlb.n) { put(&tlb, tpb.b, tpb.n); free(tpb.b); tpb = tlb; tlb.b = NULL; }   /* TL_ID records first (their order is free) */
    int anydirty = 0; for (int i = 0; i < F->nops; i++) if (F->ops[i].regen || F->ops[i].stale) anydirty = 1;   /* a dirty stored op: its +0xfc byte */
    int anycmt = 0; for (int i = 0; i < F->nops; i++) if (F->ops[i].cmt_edited && F->ops[i].raw) anycmt = 1;   /* a stored op renamed in the tree (Op.cmt_edited) */
    /* ---- solids made here (user decision SOL-1, formats/solid_history.md 5; F->sldnew from core_ui cui_sldsave.c): per solid a SOLID_ID
       record with the geometry (section 12) and its SOLIDXMT segment in section 13 (sld_save_solids 0x1800c6b50 -> FUN_1800c6d20):
       u32 0x16c, the SOLIDXMT record, the XT (PK_PART_transmit; here viewer/occt/occt_xt.cpp), u32 N, the name, the McOpsFile
       (FUN_18026dc50). The file's version picks (major, solid ver, Parasolid): X9 0x12 -> (0x12, 0x1a, 27.0 = SCH_2700142_26105), 2017 0x13 ->
       (0x13, 0x1c, 28.0 = SCH_2800143_28002) - sld_save_solids' table, the corpus' 386 X9 / 394 2017 SOLIDXMT records. ---- */
    Buf sldb = { 0 }; int nsldw = 0, nsldref = 0; char sldwhy[400] = "";
    if (F->nsldnew > 0) {
        int sver = major >= 0x13 ? 0x1c : 0x1a, pk = major >= 0x13 ? 28 : 27;
        double scale = mcam_is_metric(F) ? 1000.0 : 1000.0 / 25.4;   /* part units per metre (the XT is in metres) */
        uint32_t nid = 0; for (int k = 0; k < F->nsolids; k++) if (!F->solids[k].brep && F->solids[k].sid >= nid) nid = F->solids[k].sid + 1;   /* sld_N: unused id
            (an imported solid not saved yet has no id: F.solids[].brep) -
            (DAT_1803e80cc counts up from the largest read; the reader only matches ids, solid_history.md 5.1 [inferred]) */
        for (int k = 0; k < F->nsldnew; k++) {
            SldNew *sn = &F->sldnew[k];
            if (sn->why[0] || !sn->body || !sn->nops) { if (!sldwhy[0]) snprintf(sldwhy, sizeof sldwhy, "solid %d: %s", k + 1, sn->why[0] ? sn->why : "no history"); nsldref++; continue; }
            uint32_t N = nid++;
            char key[32]; snprintf(key, sizeof key, "sld_%u", N);
            char xerr[300] = ""; size_t xn = 0; uint8_t *xt = NULL;
            if (!sn->imported) xt = ocs_xt(sn->body, scale, pk, key, &xn, xerr, sizeof xerr);
            else {
                /* an imported solid = a brick (formats/solid_import_save.md 4): the SOLIDXMT XT, and the SldBodyOp's own copy of the body
                   (FUN_18026fa70: PK_ENTITY_copy, CNC/mcEntinfoPtr kind 0 on every edge / 0x34 on every face, the key "sld_N" + "_bo";
                   FUN_18026fc80 -> ps_save_body in the op version's Parasolid, FUN_18026e380: 0x1a -> 27.0, 0x1c -> 28.0 = the SOLIDXMT's).
                   One header DATE for both (equal in 301 / 317 corpus pairs: one save) */
                time_t tn = time(NULL); char date[64]; strftime(date, sizeof date, "%a %b %d %H:%M:%S %Y", localtime(&tn));
                char bkey[40]; snprintf(bkey, sizeof bkey, "%s_bo", key);
                int ne = 0, nf = 0, ne2 = -1, nf2 = -1; size_t bn = 0;
                xt = ocs_xt_ex(sn->body, scale, pk, key, date, 0, &xn, &ne, &nf, xerr, sizeof xerr);
                uint8_t *bx = xt ? ocs_xt_ex(sn->body, scale, pk, bkey, date, 1, &bn, &ne2, &nf2, xerr, sizeof xerr) : NULL;
                SldOp *op = sn->nops == 1 && sn->ops[0].cls == SLD_CLS_BODY ? &sn->ops[0] : NULL;
                if (!bx || !op || ne != ne2 || nf != nf2) {
                    if (!sldwhy[0]) snprintf(sldwhy, sizeof sldwhy, "imported solid %d: %s", k + 1, !bx ? xerr : "the body op's XT differs from the solid's");
                    free(xt); free(bx); nsldref++; continue; }
                free(op->bxt); op->bxt = bx; op->nbxt = bn;
                /* the records (ps_import_solid 0x18004c3e0 -> FUN_1802656b0): one per edge (PK_BODY_ask_edges), then one per face, kind
                   0 / 0x34, op 0, no refs, x28 / b2c / b2d 0; ids 0x30 apart (one block) - ours: the base of cui_sldsave.c's rec_id(0, 0, i) */
                free(op->e1); op->n1 = ne + nf; op->e1 = calloc((size_t)op->n1 + 1, sizeof *op->e1);
                for (int i = 0; i < op->n1; i++) { op->e1[i].id = 0x10000000ull + 0x30ull * (uint64_t)i; op->e1[i].kind = i < ne ? 0 : 0x34; }
                op->n2 = 0; op->n3 = 0;
            }
            double lo[3], hi[3];
            if (!xt || !ocs_bbox(sn->body, lo, hi)) { if (!sldwhy[0]) snprintf(sldwhy, sizeof sldwhy, "solid %d: %s", k + 1, xt ? "no box" : xerr); free(xt); nsldref++; continue; }
            /* the ops: the file's layout and version, the solid's eptr, a Cut / Boss's target = the solid itself (187 / 187 user cuts),
               the chain entities' eptrs (MCAM_EPTR_TOKEN) */
            int64_t S = next_eptr; next_eptr += 0x400; int32_t idn = next_idn++;
            int bad = 0;
            for (int o = 0; o < sn->nops; o++) {
                SldOp *op = &sn->ops[o]; op->fver = sver; op->ver = (uint32_t)sver; op->solid_eptr = (uint64_t)S;
                if (op->cls == SLD_CLS_EXTRUDE) { SldExtP P; sld_extp_get(op->P, &P); P.target = op->type == SLD_OP_EXTRUDE ? 0 : (uint64_t)S; sld_extp_put(op->P, &P); }   /* a Cut / Boss target (a fillet has no P target) */
                for (int c = 0; c < op->nch; c++) for (int q = 0; q < op->cnt[c]; q++) {
                    uint64_t t = op->ent[c][q]; if (!(t >> 62)) continue;
                    int ei = (int)(t & 0xffffffff); if (ei < 0 || ei >= F->nents || !F->ents[ei].type || !F->ents[ei].eptr) { bad = 1; continue; }
                    op->ent[c][q] = (uint64_t)F->ents[ei].eptr;
                }
                for (int l = 0; l < 2; l++) { SldEptr *e = l ? op->e2 : op->e1; int n = l ? op->n2 : op->n1;
                    for (int i = 0; i < n; i++) for (int r = 0; r < 2; r++) { uint64_t *ref = r ? &e[i].ref2 : &e[i].ref1;
                        if (!(*ref >> 62)) continue;
                        int ei = (int)(*ref & 0xffffffff); if (ei < 0 || ei >= F->nents || !F->ents[ei].eptr) { bad = 1; continue; }
                        *ref = (uint64_t)F->ents[ei].eptr; } }
            }
            if (bad) { if (!sldwhy[0]) snprintf(sldwhy, sizeof sldwhy, "solid %d: a chain entity is gone", k + 1); free(xt); nsldref++; next_idn--; continue; }
            SldOpsHdr oh = { 0, sn->expanded, N, (uint32_t)sn->nops, (uint64_t)S };   /* +0x0c 0: Mastercam trusts the XT (807 / 807 corpus) */
            size_t obn = 0; uint8_t *ob = sld_opsfile_write(&oh, sn->ops, sn->nops, &obn);
            if (!ob) { if (!sldwhy[0]) snprintf(sldwhy, sizeof sldwhy, "solid %d: history not writable", k + 1); free(xt); nsldref++; next_idn--; continue; }
            /* SOLID_ID (class 0x10000, len 0x100; sld_init_solid_ent 0x1800bd5c0): the header as the geometry's (head(): sel 0x81, the
               attributes), +0x68 i32 body tag (runtime: 0), +0x70 u16 flags = 1 (init), +0x78 / +0x90 the box in part units = Parasolid's
               metre box x 1000 (test-cube: -5.000000000000001 = -0.005 x 1000; ours: OCCT's tight box), +0xa8 N; the rest 0
               (runtime fields, formats/solid_history.md 5.4) */
            uint8_t sr[4 + 0x100]; memset(sr, 0, sizeof sr); w32(sr, 0x100);
            uint8_t *e = sr + 4;
            head(e, 0x10000, 0, 0, sn->color ? sn->color : 1, sn->level ? sn->level : 1, S, now, idn, major, minor, 1);
            w16(e + 0x70, 1);
            for (int a = 0; a < 3; a++) { wd(e + 0x78 + 8 * a, (lo[a] / scale) * scale); wd(e + 0x90 + 8 * a, (hi[a] / scale) * scale); }
            w32(e + 0xa8, N);
            put(&geob, sr, sizeof sr); newgeo++;
            /* section 13: u32 0x16c, SOLIDXMT (+0x00 0x20000, +0x48 major, +0x49 solid ver, +0x68 "sld_N", +0x168 xt size), the XT, u32 N,
               the name (0x100 bytes from ver 0x1c, else 0x40), the ops */
            uint8_t xr[4 + 0x16c]; memset(xr, 0, sizeof xr); w32(xr, 0x16c);
            w32(xr + 4, 0x20000); xr[4 + 0x48] = (uint8_t)major; xr[4 + 0x49] = (uint8_t)sver;
            snprintf((char *)xr + 4 + 0x68, 0x100, "%s", key); w32(xr + 4 + 0x168, (uint32_t)xn);
            uint8_t tr[4 + 0x100]; memset(tr, 0, sizeof tr); w32(tr, N);
            size_t nl = sver > 0x1b ? 0x100 : 0x40; snprintf((char *)tr + 4, nl, "%s", sn->desc);
            /* reverse DB order (FUN_1800c6d20 walks DAT_1803e8130 backwards): a later solid's segment goes first */
            Buf one = { 0 }; put(&one, xr, sizeof xr); put(&one, xt, xn); put(&one, tr, 4 + nl); put(&one, ob, obn);
            put(&one, sldb.b, sldb.n); free(sldb.b); sldb = one;
            free(xt); free(ob); nsldw++;
        }
    }
    int anysolid = 0; for (int k = 0; k < F->nsolids; k++) if (F->solids[k].edited) anysolid = 1;
    int moddim = 0; for (int k = 0; k < F->ndnew; k++) if (F->dnew[k].on && rd64(F->dnew[k].E + 0x20)) moddim = 1;
    int anyattr = 0; for (int k = 0; k < F->nents; k++) if (F->ents[k].type && !F->ents[k].created && F->ents[k].attr_edited) anyattr = 1;   /* Home > Attributes (core_ui cui_attr.c) */
    for (int k = 0; k < F->nsurfs; k++) if (F->surfs[k].edited) anyattr = 1;
    if (!added && !newgeo && !newdim && !newnote && !moddim && !fresh && !anydel && !nrev && !ntoolw && !F->stock_dirty && !F->grp_dirty && !anydirty && !anycmt && !anysolid && !anyattr && !lvl_new && !nsldw) { mcw_close(&m); free(geob.b); free(sldb.b); free(opb.b); free(tpb.b); free(refs); free(tlm_new);
        free(gone); free(mtnew.b); free(noteb.b); free(npend); for (int k = 0; tlc && k < nops; k++) free(tlc[k]); free(tlc); snprintf(msg, msgn, "nothing to add%s%s%s%s", notes[0] ? ": " : "", notes, sldwhy[0] ? "; solids not written: " : "", sldwhy); return 0; }
    /* ---- records left out: part geometry deleted here; Mastercam operations replaced by an edited version (their OP,
       TP and NCI records - NCI positions are reset when Mastercam loads, formats/mcam_write.md) ---- */
    char *keep = xmalloc(m.nseg); int ndel = 0, ndropped = 0;
    int32_t *dropped_ids = xmalloc(sizeof(int32_t) * (size_t)(nops + ngone + 1)); int ndi = 0;   /* every replaced original, no limit */
    for (int k = 0; k < nops; k++) if (ops[k]->p.file_op_id > 0) dropped_ids[ndi++] = ops[k]->p.file_op_id;
    for (int k = 0; k < ngone; k++) dropped_ids[ndi++] = gone[k];
    free(gone); int nopgone = ngone;
#define DROPPED(id) id_in(dropped_ids, ndi, (id))
    int in_dropped = 0, in_rev = 0, ninsv = 3;
    /* operations whose NCI was rewritten here (nci_reverse.c): their section is replaced by the rewritten one and their
       OP_ID record gets +0x148 = 1 (15095 NCI edited) / +0xfc = 0 (15086 dirty), as bnci_reverse sets them */
    /* every insertion this save can make, sized once: geometry / ops / toolpaths (3), one per rewritten NCI section (nrev), the memory
       table, the new notes' records, the TlMgr archive (3), the rebuilt level-name record (1) */
    McwInsert *insv = xmalloc(sizeof(McwInsert) * (size_t)(3 + nrev + 3 + 1)); int nrevw = 0;
#define REVOP(id) rev_op(F, (id), dropped_ids, ndi)
    for (int i = 0; i < m.nseg; i++) {
        keep[i] = i != tlm_seg && i != lvl_seg;                         /* the TlMgr / level records: replaced (inserted below) */
        const McwSeg *sg = &m.seg[i];
        if (sg->kind == SEG_ENT && sg->cls == 0x40 && sg->len == 4 + 0x810)   /* a dimension rewritten here (Align, core_ui cui_draft_pick.c): its body */
            for (int k = F->ndnew - 1; k >= 0; k--) if (F->dnew[k].on && rd64(F->dnew[k].E + 0x20) && rd64(F->dnew[k].E + 0x20) == rd64(m.d + sg->off + 4 + 0x20)) {
                memcpy(m.d + sg->off + 4 + 0x68, F->dnew[k].E + 0x68, 0x810 - 0x68); break; }
        if (sg->kind == SEG_ENT && sg->cls == 0x4000 && sg->sub == 1 && sg->len >= 4 + 0x149 && REVOP((int32_t)rd32(m.d + sg->off + 4 + 0x68))) {
            const Op *ro = REVOP((int32_t)rd32(m.d + sg->off + 4 + 0x68));
            if (ro->nrev || !ro->postv) { m.d[sg->off + 4 + 0x148] = 1; m.d[sg->off + 4 + 0xfc] = 0; }
            /* batch 11: post values edited here (cam_postvals.c pv_nci_rewrite): the record's post fields as the op holds them -
               CoolantPageBase / CannedTextPageBase / MiscellaneousValuesPageBase MapToOpEnt write op +0x330, +0xae8..+0xb11, +0x780..+0x7ff */
            if (ro->postv && ro->raw) {
                static const int PVR[3][2] = { { PV_COOL_OLD, 2 }, { PV_MISC_SET, PV_MISC_REAL + 80 - PV_MISC_SET }, { PV_CAN_ON, PV_CAN + 40 - PV_CAN_ON } };
                for (int q = 0; q < 3; q++) if ((uint32_t)(PVR[q][0] + PVR[q][1]) <= ro->raw_n && (uint32_t)(4 + PVR[q][0] + PVR[q][1]) <= sg->len)
                    memcpy(m.d + sg->off + 4 + PVR[q][0], ro->raw + PVR[q][0], (size_t)PVR[q][1]);
            }
        }
        if (sg->kind == SEG_ENT && sg->cls == 0x4000 && sg->sub == 1 && sg->len >= 4 + 0x100 && regen_op(F, (int32_t)rd32(m.d + sg->off + 4 + 0x68))) {
            m.d[sg->off + 4 + 0xfc] = 1; m.d[sg->off + 4 + 0xff] = 0; }   /* Mark2dHSTOpsRefMachStockDef4Regen: op +0x94 = 1, +0x97 = 0 */
        /* a stored op renamed in the Toolpaths manager (core_ui cui_misc.c op_set_comment): OnEndlabeledit @180361440 writes the
           comment into the op's record (strcpy0(record + 0x6e, text, 0x78); UpdateListAndDB -> rewrite_ent): the kept record's 0x6e..0xe5 */
        if (sg->kind == SEG_ENT && sg->cls == 0x4000 && sg->sub == 1 && sg->len >= 4 + 0x6e + 0x78)
            for (int k = 0; k < F->nops; k++) if (F->ops[k].cmt_edited && F->ops[k].raw && F->ops[k].raw_n >= 0x6e + 0x78 && F->ops[k].id == (int32_t)rd32(m.d + sg->off + 4 + 0x68)) {
                memcpy(m.d + sg->off + 4 + 0x6e, F->ops[k].raw + 0x6e, 0x78); break; }
        if (sg->kind == SEG_NCI && sg->sub == -2 && sg->len >= 2 + 0x20) {
            const Op *ro = REVOP((int32_t)rd32(m.d + sg->off + 2 + 0x1c));
            if (ro && nrevw >= nrev) ro = NULL;                  /* (ours: a second section of the same op is kept as stored - insv has nrev slots) */
            in_rev = ro != NULL;
            if (ro) { insv[ninsv].before = -1; insv[ninsv].bytes = ro->nci; insv[ninsv].len = ro->nci_n; ninsv++; nrevw++; }
        }
        /* the old records go; the new section goes in after its end record (so the header's NCI start stays put) */
        if (in_rev && sg->kind == SEG_NCI) { keep[i] = 0; if (sg->sub == -3) { in_rev = 0; insv[ninsv - 1].before = i + 1; } continue; }
        if (sg->kind == SEG_ENT && sg->cls == 0x4000 && (sg->sub == 1 || sg->sub == 2) && sg->len >= 4 + 0x6c && DROPPED((int32_t)rd32(m.d + sg->off + 4 + 0x68))) { keep[i] = 0; if (sg->sub == 1) ndropped++; continue; }
        /* tp_change_eptr: a kept TP record of a moved entity (same id, new record) points at the new record (+0x80 / +0xd0 eptr) */
        if (sg->kind == SEG_ENT && sg->cls == 0x4000 && sg->sub == 2 && sg->len >= 4 + 0xd8) {
            int32_t rid = (int32_t)rd32(m.d + sg->off + 4 + 0x6c);
            for (int k = 0; rid > 0 && k < F->nents; k++) if (F->ents[k].created && F->ents[k].type && F->ents[k].idn == rid) {
                int del = -1; for (int q = 0; q < F->nents && del < 0; q++) if (q != k && !F->ents[q].type && !F->ents[q].created && F->ents[q].idn == rid) del = q;
                if (del >= 0) for (int fo = 0x80; fo <= 0xd0; fo += 0x50)   /* the eptr fields that held the original's record */
                    if ((int64_t)rd64(m.d + sg->off + 4 + fo) == F->ents[del].eptr) w64(m.d + sg->off + 4 + fo, (uint64_t)F->ents[k].eptr);
                break;
            }
        }
        /* tp_substitute_ent (mccore 0x180037f10; core_ui cui_srfedit.c): a kept TP record whose reference was re-pointed here (Untrim /
           Remove Boundary of a surface an op uses: TpRef.subst1 in force) names the new surface: +0x6c its id, +0x70 its +0x30, +0x80 its
           eptr; kinds 1 / 2 / 3 / 0x1b also +0xc8 / +0xd0 (0x10 later in a 0x398-byte record) */
        if (sg->kind == SEG_ENT && sg->cls == 0x4000 && sg->sub == 2 && sg->len >= 4 + 0xd8) {
            uint8_t *tr = m.d + sg->off + 4; int32_t top = (int32_t)rd32(tr + 0x68), tid = (int32_t)rd32(tr + 0x6c);
            for (int k = 0; tid && k < F->ntp; k++) {
                const TpRef *t = &F->tp[k]; if (t->op_id != top || t->ent != tid) continue;
                int ns = tp_ref_subst(F, t); if (ns < 0) break;
                int e = F->surfs[ns].ent; if (e < 0 || e >= F->nents || !F->ents[e].type) break;
                w32(tr + 0x6c, (uint32_t)F->ents[e].idn); w32(tr + 0x70, (uint32_t)F->ents[e].ctime); w64(tr + 0x80, (uint64_t)F->ents[e].eptr);
                int kd = (int16_t)rd16(tr + 0x90), sh = sg->len - 4 >= 0x398 ? 0x10 : 0;
                if ((kd == 1 || kd == 2 || kd == 3 || kd == 0x1b) && sg->len >= 4 + 0xd8 + sh) { w32(tr + 0xc8 + sh, (uint32_t)F->ents[e].idn); w64(tr + 0xd0 + sh, (uint64_t)F->ents[e].eptr); }
                break;
            }
        }
        if (sg->kind == SEG_NCI) {                       /* -2 starts an op's section (+0x1c op id), -3 ends it */
            const uint8_t *q = m.d + sg->off + 2;
            if (sg->sub == -2 && sg->len >= 2 + 0x20) in_dropped = DROPPED((int32_t)rd32(q + 0x1c));
            if (in_dropped) keep[i] = 0;
            if (sg->sub == -3) in_dropped = 0;
            continue;
        }
        if (sg->kind == SEG_ENT && sg->cls == 0x100) {   /* a part surface: dropped when deleted here, rewritten when its record was edited here */
            for (int k = 0; k < F->nsurfs; k++) {
                const Surf *sf = &F->surfs[k]; int e = sf->ent;
                if (sf->idn != sg->id || e < 0 || e >= F->nents || F->ents[e].created) continue;
                if (!F->ents[e].type) { keep[i] = 0; ndel++; }
                else if (sf->edited && sf->n == sg->len - 4) memcpy(m.d + sg->off + 4, sf->rec, sf->n);
                break;
            }
            if (keep[i] && sg->len >= 4 + 0x2a) {   /* the surface curves made / deleted here on it: its +0x28 (cons_ref_delta) */
                uint8_t *rr = m.d + sg->off + 4; int dr = cons_ref_delta(F, (int64_t)rd64(rr + 0x20));
                if (dr) w16(rr + 0x28, (uint16_t)((int16_t)rd16(rr + 0x28) + dr));
            }
            continue;
        }
        if (sg->kind == SEG_ENT && (sg->cls == 0x400 || sg->cls == 0x80) && sg->len >= 4 + 0x28) {   /* a part surface curve deleted here: its CONS
                                                                      record and its uv curve's (delete of a surface curve: NOT DECODED; ours: both go) */
            int64_t ep = (int64_t)rd64(m.d + sg->off + 4 + 0x20); int gone = 0;
            for (int k = 0; k < F->nents && !gone; k++) {
                const Ent *en = &F->ents[k];
                if (en->type || en->created || en->cls != 0x400 || en->crv < 0 || en->crv >= F->ncrvs) continue;
                if (sg->cls == 0x400 ? en->idn == sg->id : (F->crvs[en->crv].n >= 0xe0 && (int64_t)rd64(F->crvs[en->crv].rec + 0xd8) == ep)) gone = 1;
            }
            if (gone) { keep[i] = 0; if (sg->cls == 0x400) ndel++; continue; }
        }
        /* a part curve whose attributes were changed here (core_ui cui_attr.c): colour +6, level +8, width +0x10, styles +0x18 / +0x1c
           (mastercam.dll FUN_18046a420 writes both style fields) */
        if (sg->kind == SEG_ENT && (sg->cls == 1 || sg->cls == 2 || sg->cls == 4 || sg->cls == 8 || sg->cls == 0x80) && sg->len >= 4 + 0x20)
            for (int k = 0; k < F->nents; k++) { const Ent *en = &F->ents[k];
                if (!en->type || en->created || (!en->attr_edited && !en->blank_edited) || en->idn != sg->id) continue;
                uint8_t *rr = m.d + sg->off + 4;
                if (en->blank_edited) rr[5] = (uint8_t)((rr[5] & ~1) | (en->blanked ? 1 : 0));   /* sel bit 0x100 (BlankEptr) */
                if (!en->attr_edited) break;
                rr[6] = (uint8_t)en->color; w32(rr + 8, (uint32_t)en->level); wd(rr + 0x10, en->width >= 1 ? en->width : 1.0);
                w16(rr + 0x18, (uint16_t)en->ln_style); w16(rr + 0x1c, (uint16_t)(en->type == E_POINT ? en->pt_style : en->ln_style)); break; }
        if (sg->kind == SEG_ENT && (sg->cls == 0x10 || sg->cls == 0x20) && sg->len >= 4 + 0x20)   /* a part note whose colour / level / attributes
                                                                      changed here (E_NOTE: the header as the curves') */
            for (int k = 0; k < F->nents; k++) { const Ent *en = &F->ents[k];
                if (en->type != E_NOTE || en->created || !en->attr_edited || en->idn != sg->id) continue;
                uint8_t *rr = m.d + sg->off + 4; rr[6] = (uint8_t)en->color; w32(rr + 8, (uint32_t)en->level);
                note_attr_patch(rr, en); break; }
        if (sg->kind != SEG_ENT || (sg->cls != 1 && sg->cls != 2 && sg->cls != 4 && sg->cls != 0x10 && sg->cls != 0x20 && sg->cls != 0x100000)) continue;   /* a deleted
                                                                      part mesh (PMESH_ID, its data inside the record's length) goes too; a deleted
                                                                      part note (E_NOTE): its record goes, its memory-table entries below */
        for (int k = 0; k < F->nents; k++) if (!F->ents[k].type && !F->ents[k].created && F->ents[k].idn == sg->id) { keep[i] = 0; ndel++; break; }
    }
    /* ---- header (sub 0x0d): referenced-geometry count, op count, setup / NCI offsets, max id ---- */
    uint8_t *H = m.d + m.seg[hdr].off + 4;
    for (int i = 0; i < m.nseg; i++)                                    /* geometry referenced by the kept ops too */
        if (keep[i] && m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x4000 && m.seg[i].sub == 2) {
            const uint8_t *t = m.d + m.seg[i].off + 4; int kind = (int16_t)rd16(t + 0x90);
            /* kind 4 too: [measured] the header's +0x6c equals the number of entities with a reference count (+0x38) > 0 in the 2017
               corpus parts with 3D ops, and those include the surfaces / solids named by kind 4 records (e.g. WATERLINE.mcam: 182 = its
               182 drive surfaces, no chain; formats/surface_selection.md 6 "S6") */
            if (kind == 7 || kind == 1 || kind == 4) { refs = xrealloc(refs, sizeof(int32_t) * (nref + 1)); refs[nref++] = (int32_t)rd32(t + 0x6c); }
        }
    int ndist = 0; for (int i = 0; i < nref; i++) { int seen = 0; for (int j = 0; j < i && !seen; j++) seen = refs[j] == refs[i]; if (!seen && refs[i]) ndist++; }
    McwInsert ins3[3] = { { first_geo_slot, geob.b, geob.n }, { op_slot, opb.b, opb.n }, { tp_slot, tpb.b, tpb.n } };
    memcpy(insv, ins3, sizeof ins3);
    /* the memory table rewritten when notes were made or deleted here: the base part's entries without those of the deleted part notes
       (mTableDeleteNote frees them, core_draft_rec2.c 2310-2370), the new notes' entries, the base part's terminator (id 0) */
    Buf mtb = { 0 }; int ndelnote = 0;
    for (int k = 0; k < F->nnotes; k++) { int e = F->notes[k].ent; if (e >= 0 && e < F->nents && !F->ents[e].type && !F->ents[e].created) ndelnote++; }
    if (mt_seg >= 0 && (mtnew.n || ndelnote)) {
        const uint8_t *b = m.d + m.seg[mt_seg].off; size_t o = 0, L = m.seg[mt_seg].len;
        while (o + 0x28 <= L) {
            int32_t id = (int32_t)rd32(b + o); uint64_t sz = (uint64_t)rd64(b + o + 0x10);
            if (!id) break;
            int gone_mt = 0;
            for (int k = 0; k < F->nnotes && !gone_mt; k++) { int e = F->notes[k].ent;
                if (e < 0 || e >= F->nents || F->ents[e].type || F->ents[e].created) continue;
                for (int q = 0; q < F->notes[k].nmt && !gone_mt; q++) gone_mt = F->notes[k].mt[q].id == id; }
            if (!gone_mt) put(&mtb, b + o, 0x28 + (size_t)sz);
            o += 0x28 + (size_t)sz;
        }
        put(&mtb, mtnew.b, mtnew.n);
        put(&mtb, b + o, L - o);                                       /* the terminator as stored */
        keep[mt_seg] = 0;
        insv[ninsv].before = mt_seg; insv[ninsv].bytes = mtb.b; insv[ninsv].len = mtb.n; ninsv++;
    }
    free(mtnew.b);
    if (noteb.n) {                                                     /* the new notes' records: with the geometry, but not before the table */
        insv[ninsv].before = mt_seg + 1 > first_geo_slot ? mt_seg + 1 : first_geo_slot; insv[ninsv].bytes = noteb.b; insv[ninsv].len = noteb.n; ninsv++;
    }
    if (tlm_new) { insv[ninsv].before = tlm_seg; insv[ninsv].bytes = tlm_new; insv[ninsv].len = tlm_n; ninsv++; }
    if (lvl_new) { insv = xrealloc(insv, sizeof(McwInsert) * (size_t)(ninsv + 1)); insv[ninsv].before = lvl_seg; insv[ninsv].bytes = lvl_new; insv[ninsv].len = lvl_n; ninsv++; }
    if (sldb.n) {                                                      /* the new solids' section 13 segments: after the part's own solids (section
                                                                          13 is in reverse DB order and the new SOLID_IDs come first in the DB),
                                                                          else before the setup record (ASSOC 7, section 14) */
        int at = -1;
        for (int i = 0; i < m.nseg; i++) if (m.seg[i].kind == SEG_SOLID_TAIL) at = i + 1;
        for (int i = 0; at < 0 && i < m.nseg; i++) if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x4000 && m.seg[i].sub == 7) at = i;
        if (at < 0) at = first_assoc;
        insv = xrealloc(insv, sizeof(McwInsert) * (size_t)(ninsv + 1)); insv[ninsv].before = at; insv[ninsv].bytes = sldb.b; insv[ninsv].len = sldb.n; ninsv++;
    }
#undef REVOP
    /* ---- the solids that got a whole-body SldToolPOp (see sldtoolp_whole_bytes): their XT + trailer + history segment rewritten with the
       ops appended. McOpsFile write FUN_18026dc50 (decomp/solid_history/h1_read_save_regen.c l.774-890): u8 +0xc, u8 +0xe, u32 +0x10,
       u32 +0x14 (history position, FUN_180267170: + 1 per appended op), u64 +0x20, u32 count (+0x38), then per op u32 type, u32 size
       (slot 19), the bytes (slot 21). The XT is not touched (a toolpath op adds no geometry). The solid's name edits (solid_patch, in
       place) go first so the copy has them. ---- */
    if (anysolid) { solid_patch(&m, F); anysolid = 0; }
    uint8_t *sldtail[64]; int nsldtail = 0;
    for (int u = 0; u < nnewsld; u++) {
        int done = 0; for (int w = 0; w < u; w++) done |= newsld[w].solid == newsld[u].solid;
        if (done) continue;
        const Solid *so = &F->solids[newsld[u].solid];
        for (int i = 0; i + 1 < m.nseg; i++) {
            const McwSeg *x = &m.seg[i], *t = &m.seg[i + 1];
            if (x->kind != SEG_ENT || x->cls != 0x20000 || x->len < 4 + 0x16c || t->kind != SEG_SOLID_TAIL) continue;
            const uint8_t *se = m.d + x->off + 4;
            if (strncmp((const char *)se + 0x68, so->name, 0x100)) continue;
            size_t at = rd32(se + 0x168) + 4 + (se[0x49] > 0x1b ? 0x100 : 0x40);   /* the ops block (formats/solid_history.md 1.2) */
            if (at + 22 > t->len) break;
            int nadd = 0; for (int w = u; w < nnewsld; w++) if (newsld[w].solid == newsld[u].solid) nadd++;
            size_t nn = t->len + (size_t)nadd * (8 + SLDTP_WHOLE_N);
            uint8_t *b = xmalloc(nn); memcpy(b, m.d + t->off, t->len);
            uint8_t *h = b + at; int64_t septr = rd64(h + 10);
            w32(h + 6, rd32(h + 6) + (uint32_t)nadd); w32(h + 18, rd32(h + 18) + (uint32_t)nadd);
            size_t o = t->len;
            for (int w = u; w < nnewsld; w++) if (newsld[w].solid == newsld[u].solid) {
                w32(b + o, 0x18); w32(b + o + 4, SLDTP_WHOLE_N); sldtoolp_whole_bytes(b + o + 8, septr, newsld[w].op_id, newsld[w].link, newsld[w].type);
                o += 8 + SLDTP_WHOLE_N;
            }
            keep[i + 1] = 0;
            insv = xrealloc(insv, sizeof(McwInsert) * (size_t)(ninsv + 1)); insv[ninsv].before = i + 1; insv[ninsv].bytes = b; insv[ninsv].len = nn; ninsv++;
            if (nsldtail < 64) sldtail[nsldtail++] = b;
            break;
        }
    }
    /* ---- the NCI sections of the ops computed here (formats/nci_save.md 4). bnci_section (mccore @180088880) deletes the op's old section
       and writes the new one at the end of the session NCI file (bnci_wr_sos seeks to the end, decomp/nciw/core_wr.c l.2785); save_db copies
       every valid section in that file's order (decomp/mccore_fileio.c l.1400-1490), each record by bnci_write_to_disk (+0x10 = its offset
       in the part file). So: the replaced ops' sections are left out (above, in_dropped), the new ones follow the kept ones; an NCI_ID
       record (ASSOC 5, all zero but its header: save_db l.1381-1387, measured 16 / 16 user parts) precedes them when the base has none.
       The op's record: +0xe8 / +0xec = the -2 / -3 positions (bnci_wr_sos / bnci_wr_eos: op +0x80 / +0x84; the loader resets them,
       FUN_1802cfb60), +0xfc = 0 (FUN_180103fb0, decomp/mccore_filter.c l.3554-3556: op +0x94 cleared after the -3 record). An op the
       writer refuses stays as before: dirty, no section. ---- */
    int nnciw = 0, nci_ins = -1, add_nciid = 0; Buf ncib = { 0 }; size_t nci_X = 0;
    if (nnpend) {
        int at = m.nseg; for (int i = 0; i < m.nseg; i++) if (m.seg[i].kind == SEG_NCI && m.seg[i].sub == 0x7fff) { at = i; break; }
        insv = xrealloc(insv, sizeof(McwInsert) * (size_t)(ninsv + 1)); nci_ins = ninsv;
        insv[ninsv].before = at; insv[ninsv].bytes = NULL; insv[ninsv].len = 0; ninsv++;
        nci_X = ins_out_off(&m, keep, insv, ninsv, nci_ins);           /* the block's place does not depend on its length */
        add_nciid = nci_id < 0;
        if (add_nciid) { uint8_t r[4 + 0xb2]; memset(r, 0, sizeof r); w32(r, 0xb2); head(r + 4, 0x4000, 1, 5, 0, 0, 0, 0, 0, major, minor, 0); put(&ncib, r, sizeof r); }
        size_t sec0 = ncib.n;
        NciwPart *P = nciw_part_load(src && src[0] && !fresh ? src : NULL);
        int metric = mcam_is_metric(F);
        /* which ops: Mastercam's engines write a section-start block (bnci_wr_sof) + 1002 per pass - in the user parts 128 / 149 Contour,
           8 / 41 2D High Speed and 1 / 1 Pocket sections hold several (formats/nci_save.md 6) - and Reverse toolpath takes each 1000-1002 as
           one pass (formats/reverse_toolpath.md 3.3). Where an engine starts a new block is decoded for Drill only (bnci_drill: one
           bnci_wr_sof, at the first cycle, decomp/mcmill_all.c l.69999; nci_writer.md 10) and, ported with Move.sof (nci_save.md 7), for
           Pocket's section starts (wr_pock_toolpath l.316183) and the standard 2D Contour's passes (FUN_180045770 l.6732; NS-4) and the 2D High Speed Dynamic region path (nci_save.md 7.3) - CamOp.nci_blocks:
           the op went through them - the gate is lifted for those
           (user decision NS-1: per op type where the block placement matches every corpus section whose moves regenerate identically).
           MCAMUI_SAVE_NCI=all (ours, testing) writes every op the writer takes */
        const char *sw = getenv("MCAMUI_SAVE_NCI"); int all = sw && !strcmp(sw, "all");
        int nkept = 0;
        for (int u = 0; u < nnpend; u++) {
            const NciPend *np = &npend[u]; const CamOp *op = ops[np->k];
            uint8_t *e = opb.b + np->rec_at + 4;
            if (!all && rd16(e + 0x6c) != 2 && !((rd16(e + 0x6c) == 3 || rd16(e + 0x6c) == 1 || rd16(e + 0x6c) == 134) && op->nci_blocks)) { nkept++; continue; }
            const uint8_t *t = tlc[np->k]; uint32_t tn = t ? 0x700 : 0;   /* its new TL_ID, another op's new one of its slot, else the part's */
            for (int q = 0; !t && q < nops; q++) if (tlc[q] && rd32(tlc[q] + 0x2cc) == rd32(e + 0x5ac)) { t = tlc[q]; tn = 0x700; }
            Move *mv = xmalloc(sizeof(Move) * (size_t)op->nm); memcpy(mv, op->m, sizeof(Move) * (size_t)op->nm);
            if (!cam_op_top(&op->p)) for (int q = 0; q < op->nm; q++) {   /* world -> op coordinates (wpt_to_op, as nci_write.c nciw_view) */
                double w[3] = { mv[q].p.x, mv[q].p.y, mv[q].p.z }, c[3]; cam_plane_w2v(op->p.tplane, op->p.torg, w, c);
                mv[q].p.x = c[0]; mv[q].p.y = c[1]; mv[q].p.z = c[2]; }
            long base = (long)(nci_X + ncib.n);
            NciwOp no = { e, np->recn, t, tn, F, metric, base, np->tp, np->tpn };
            NciwBuf sec = { 0 }; NciwStats st; memset(&st, 0, sizeof st); char e2[300] = "";
            if (nciw_section(P, &no, mv, op->nm, &sec, &st, e2, sizeof e2)) {
                w32(e + 0xe8, (uint32_t)base); w32(e + 0xec, (uint32_t)(base + st.eos_off)); e[0xfc] = 0;
                put(&ncib, sec.b, sec.n); nnciw++;
            } else NOTE("%s%s: toolpath not saved (%s) - it opens with the red X", notes[0] ? "; " : "", cam_op_label(op), e2);
            free(sec.b); free(mv);
        }
        nciw_part_free(P);
        if (nkept) NOTE("%s%d operation(s) written without their toolpath (where Mastercam starts a new NCI pass is NOT DECODED for their type)", notes[0] ? "; " : "", nkept);
        if (nnciw) { insv[nci_ins].bytes = ncib.b; insv[nci_ins].len = ncib.n; }
        else { ncib.n = 0; add_nciid = 0; }
        nci_X += add_nciid ? sec0 : 0;                                  /* the first section's offset */
    }
    for (int u = 0; u < nnpend; u++) free(npend[u].tp);
    free(npend); for (int k = 0; tlc && k < nops; k++) free(tlc[k]); free(tlc);
    uint32_t setup_pos = rd32(H + 0x74), nci_pos = rd32(H + 0x78);
#define NEWPOS(pos) new_pos(&m, keep, insv, ninsv, pos)
    w32(H + 0x6c, (uint32_t)ndist);
    w32(H + 0x70, rd32(H + 0x70) + (uint32_t)added - (uint32_t)ndropped);
    if (setup_pos && setup_pos != 0xffffffffu) w32(H + 0x74, (uint32_t)NEWPOS(setup_pos));
    if (nci_pos && nci_pos != 0xffffffffu) w32(H + 0x78, (uint32_t)NEWPOS(nci_pos));
    if (nnciw) {   /* +0x78 = the first NCI record (bnci_restore seeks there): ours when no section of the base is left */
        int base_nci = nrevw > 0;
        for (int i = 0; i < m.nseg && !base_nci; i++) base_nci = keep[i] && m.seg[i].kind == SEG_NCI && m.seg[i].sub != 0x7fff;
        if (!base_nci || !nci_pos || nci_pos == 0xffffffffu) w32(H + 0x78, (uint32_t)nci_X);
    }
    w32(H + 0x7c, (uint32_t)(next_idn - 1));
#undef NEWPOS
#undef DROPPED
    if (F->stock_dirty) stock_patch(&m, F);
    if (F->grp_dirty) grp_patch(&m, F);
    if (anysolid) solid_patch(&m, F);
    size_t outn; uint8_t *out = mcw_build(&m, keep, insv, ninsv, &outn);
    {   /* every NCI record's +0x10 = its offset in the part: save_db writes each record with bnci_write_to_disk (decomp/mccore_fileio.c l.1488;
           nci_writer.md 2). The kept stored records move when records before them change (the reader overwrites +0x10 anyway,
           bnci_read_from_disk, formats/mcam_write.md 6) */
        uint32_t np = rd32(H + 0x78);
        if (np && np != 0xffffffffu) for (size_t o = np; o + 2 <= outn; ) {
            uint16_t sz = rd16(out + o); if (!sz || o + 2 + sz > outn) break;
            if (sz >= 0x14) w32(out + o + 2 + 0x10, (uint32_t)o);
            o += 2 + (size_t)sz;
        }
    }
    free(keep); free(dropped_ids); free(insv); for (int u = 0; u < nsldtail; u++) free(sldtail[u]); free(mtb.b); free(noteb.b); free(sldb.b); free(ncib.b);
    int ok = 0;
    {   char tmp[4200]; FILE *o = snprintf(tmp, sizeof tmp, "%s.tmp", dst) < (int)sizeof tmp ? fopen(tmp, "wb") : NULL;
        if (o) { ok = fwrite(out, 1, outn, o) == outn; if (fclose(o) != 0) ok = 0; if (ok && file_replace(tmp, dst) != 0) ok = 0; if (!ok) remove(tmp); }
        if (!ok) snprintf(err, sizeof err, "cannot write %s", dst); }
    free(out);
    char dimnote[96] = "";
    if (newdim) snprintf(dimnote, sizeof dimnote, "%d new dimension(s), ", newdim);
    if (newnote) snprintf(dimnote + strlen(dimnote), sizeof dimnote - strlen(dimnote), "%d new note(s), ", newnote);
    char rednote[160] = "";   /* the ops written with their toolpath (NCI) open as they are; the others with the red X */
    if (nnciw) snprintf(rednote, sizeof rednote, " (%d with their toolpath%s)", nnciw, added > nnciw ? ", the others open with the red X - regenerate them" : "");
    else if (added) snprintf(rednote, sizeof rednote, " (they open with the red X - regenerate them)");
    if (!ok) snprintf(msg, msgn, "%s", err);
    else snprintf(msg, msgn, "wrote %s: %d new entities, %s%d deleted, %d operation(s) added (%d replacing stored ones), %d operation(s) deleted, %d new tool(s), %d tool(s) rewritten, %d reversed%s%s%s%s%s", dst, newgeo, dimnote, ndel, added, ndropped - nopgone, nopgone, ntlw, ntoolw, nrevw, F->stock_dirty ? ", stock setup" : "", F->grp_dirty ? ", group settings" : "",
                  rednote, skipped || notes[0] ? ". " : "", notes);
    if (ok && (nsldw || nsldref)) { size_t k = strlen(msg);
        snprintf(msg + k, msgn - k, "; %d solid(s) made here written%s%d not written%s%s", nsldw, nsldw ? " (Parasolid XT + history) - " : ", ", nsldref, sldwhy[0] ? ": " : "", sldwhy); }
    if (ok && !cap && major >= 0x13 && mcam_ext_mismatch(dst, major)) { size_t k = strlen(msg);   /* user decision 2026-11-19 (formats/user_decisions.md) */
        snprintf(msg + k, msgn - k, "\n\n%s", mcam_ext_warning()); }
    mcw_close(&m); free(geob.b); free(opb.b); free(tpb.b); free(refs); free(tlm_new); free(lvl_new);
    return ok;
#undef NOTE
}

/* a Mastercam 2017 part (major >= 0x13: a new part, whose base is the 2017 operation defaults library, or a 2017 source) saved under a
   .mcx-9 name: Mastercam's own Save As offers only *.mcam for a 2017 part (33207 "Mastercam 2017 Files (*.mcam)", mainwindow.cpp file
   types), so the name says X9 while the content is 2017. User decision 2026-11-19 (formats/user_decisions.md): warn, suggest .mcam, still
   save. The check and the text are ours (no Mastercam source). */
int mcam_ext_mismatch(const char *dst, int major) {
    size_t n = dst ? strlen(dst) : 0;
    return major >= 0x13 && n >= 6 && !strcasecmp(dst + n - 6, ".mcx-9");
}
const char *mcam_ext_warning(void) {        /* ours (no Mastercam source): user decision 2026-11-19 */
    return "Warning: this is a Mastercam 2017 part (.mcam content) saved under a .mcx-9 (Mastercam X9) name. "
           "Mastercam X9 cannot open it as an X9 part; save it with the .mcam extension instead.";
}

int mcam_save_with_ops(const char *src, const char *dst, const McamFile *F, CamOp **ops, int nops, char *msg, size_t msgn) {
    return mcam_save_part(src, dst, (McamFile *)F, ops, nops, msg, msgn);
}

/* Q19 "Save parameters to defaults file" (formats/opdlg_port_audit.md 22): the operation as Mastercam's dialog op (ToolpathDlg
   +0x20f0 after OnUpdateLocalOp) is handed to WriteOperationToDefaultsFile (mccore 0x180896fc0, decomp/b12/tpdlg_all.c l.6834).
   Here that op is the record this program's .mcam save writes for OP (mcam_save_part above), without the part-level fields the
   save adds (record header, ids, chain counts, group, tool slot allocation, tab / point / chain records): TMPL = OP's own stored
   record (OWN: an edited Mastercam op, the save's keep_seq) or the defaults file's op of its type (an op made here: the save's
   template too, find_op on the defaults library). E (0x2938 ent bytes) gets: the comment, the program # / sequence start and
   step of a new op (OMsetOperationToMachine, as the save), its planes, the holder reference of a new op (0), the op_tool_info
   block (the part tool's, its slot kept; the op's feeds / rpm / coolant) and op_fields_to_record. The caller then puts back the
   fields WriteOperationToDefaultsFile takes from the defaults file's op. Returns 0 (MSG) for a type the save does not write. */
int mcam_op_dialog_record(const McamFile *F, const CamOp *op, const uint8_t *tmpl, int own, uint8_t *e, char *msg, size_t msgn) {
    const CamParams *p = &op->p; int style, opcode = mcam_opcode_for(p, &style);
    msg[0] = 0;
    if (!opcode || p->type == CAM_OPTIROUGH) { snprintf(msg, msgn, "%s: this operation type is not written to Mastercam files here", cam_op_label(op)); return 0; }
    memcpy(e, tmpl, 0x2938);
    /* comment (op +6): the save's rule, except (ours) the name this program gave a new op itself before L-7 (core_ui open_params:
       "<type> <n>", "Chamfer <n>" / "Engrave <n>"; still in projects saved then), which is no comment of the user's: the defaults
       op keeps its own. A new op made now has none (InitOperation clears op +6, decision L-7) */
    {   char base[sizeof op->name]; snprintf(base, sizeof base, "%s", op->name);
        char *sp = strrchr(base, ' '); int auto_name = 0;
        if (sp && sp[1]) { const char *q = sp + 1; while (*q >= '0' && *q <= '9') q++; if (!*q) { *sp = 0;
            auto_name = !strcmp(base, cam_type_label(p->type)) || (p->type == CAM_CONTOUR && (!strcmp(base, "Chamfer") || !strcmp(base, "Engrave"))); } }
        if (!own && auto_name) { /* the template's comment kept */ }
        else { memset(e + 0x6e, 0, 0x78); if (!(own && !strcmp(op->name, cam_type_label(p->type)))) snprintf((char *)e + 0x6e, 0x78, "%s", op->name); }
    }
    if (!own && F->grp_rec) w32(e + 0x160, (uint32_t)F->grp_prog);
    if (!own && F->ctl_ok && seq_ok(F->ctl_seq[0]) && seq_ok(F->ctl_seq[1])) { w32(e + 0x164, (uint32_t)(int32_t)F->ctl_seq[0]); w32(e + 0x168, (uint32_t)(int32_t)F->ctl_seq[1]); }
    {   CamParams st; memset(&st, 0, sizeof st);
        int same = own && cam_planes_from_record(e, 0x2938, &st, NULL, 0)
                   && st.tplane == p->tplane && st.cplane == p->cplane && st.wcs == p->wcs
                   && !memcmp(st.torg, p->torg, sizeof st.torg) && !memcmp(st.corg, p->corg, sizeof st.corg) && !memcmp(st.worg, p->worg, sizeof st.worg)
                   && st.wofs == (p->wofs_manual && p->wofs >= 0 ? p->wofs : -1) && st.wwofs == p->wwofs && st.cwofs == p->cwofs;
        if (!same) cam_planes_to_record(e, p);
    }
    if (!own) w64(e + 0x1210, 0);
    {   Tool tool; const Tool *tl = NULL;                           /* the save's tool block, without a new TL_ID record / slot */
        if (p->tool_slot > 0) for (int k = 0; k < F->ntools && !tl; k++) if (F->tools[k].slot == p->tool_slot && F->tools[k].num == p->tool_num) tl = &F->tools[k];
        if (!tl) tl = mcam_tool(F, p->tool_num);
        if (tl) { tool = *tl; tool.holder_rz = NULL; tool.holder_n = 0; }
        else {
            tool_new(&tool, p->tool_type ? p->tool_type : 10, p->tool_dia, p->tool_num, mcam_is_metric(F));
            free(tool.holder_rz); tool.holder_rz = NULL; tool.holder_n = 0;
            tool.rpm = p->rpm; tool.feed = p->feed; tool.plunge = p->plunge_feed;
            tool.slot = (int32_t)rd32(e + 0x5ac);
        }
        uint8_t blk[0x288];
        if (own && (int32_t)rd32(e + 0x5ac) == tool.slot && tl) memcpy(blk, e + 0x348, 0x288);
        else { tool_op_info(&tool, blk); memcpy(blk + 0x274, e + 0x5bc, 4); }
        wd(blk + 0x30, p->feed); wd(blk + 0x38, p->plunge_feed); w32(blk + 0x48, (uint32_t)p->rpm);
        if ((p->coolant != 0) != (rd16(blk + 0x50) == 16)) w16(blk + 0x50, (uint16_t)(p->coolant ? 16 : 8));
        memcpy(e + 0x348, blk, 0x288);
    }
    int keep_tabs = own && p->type == CAM_CONTOUR && p->tabs == 0 && e[0x1380];
    op_fields_to_record(F, op, e, 4 + 0x2938, own, 0, keep_tabs);
    (void)style;
    return 1;
}

/* a part drawn from File > New is written on the operation defaults library (new_base): its stored views (numbers 9.. ) are in the file, so they are in Mastercam's list (n_views) while the command runs. Loaded here once for such a part, so find_view_k sees them and a
   new view takes the next number */
void mcam_fresh_views(McamFile *f) {
    if (f->path[0] || f->nfviews) return;
    const McwFile *b = mcam_defaults_library(mcam_is_metric(f)); if (!b) return;
    for (int i = 0; i < b->nseg; i++) if (b->seg[i].kind == SEG_ENT && b->seg[i].cls == 0x200 && b->seg[i].sub == 0 && b->seg[i].len >= 4 + 0xb8) {
        const uint8_t *r = b->d + b->seg[i].off + 4;
        f->fviews = realloc(f->fviews, (f->nfviews + 1) * sizeof *f->fviews);
        f->fviews[f->nfviews].n = r[0x6a] | r[0x6b] << 8; memcpy(f->fviews[f->nfviews].m, r + 0x70, sizeof f->fviews[f->nfviews].m); f->fviews[f->nfviews].isnew = 0; f->nfviews++;
    }
}
