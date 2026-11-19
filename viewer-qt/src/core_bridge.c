/* core_bridge.c - see core_bridge.h. Code moved from viewer/mcamui.c keeps its comments / citations. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "paths.h"
#include "scene.h"
#include "cam2d.h"
#include "toollib.h"
#include "mcam_saveops.h"
#include "core_ui/core_ui.h"
#include "core_bridge.h"
#include "sld_ops.h"
#include "mcam_grp.h"

#define CP(dst, src) snprintf(dst, sizeof dst, "%s", src)

const char *qc_config_dir(void) { return config_dir(); }
const char *qc_exe_dir(void) { return exe_dir(); }
const char *qc_config_file(char *out, size_t n, const char *name) { return config_file(out, n, name); }
const char *qc_site_file(char *out, size_t n, const char *key, const char *rel) { return site_file(out, n, key, rel); }

/* ------------------------------------------------------------------------------------------------ configuration */
/* keyboard view factors, as in Mastercam's configuration (mcamxm.config 4026-4029, defaults 0.0625 / 0.0625 / 0.0625 /
   5 deg): one key press zooms / unzooms / pans by that fraction of the screen, or rotates by that many degrees. */
float qc_vk_zoom = 0.0625f, qc_vk_unzoom = 0.0625f, qc_vk_pan = 0.0625f, qc_vk_rot = 5.0f;
int qc_dyn_free = 1, qc_use_shf_to_spin = 0, qc_reverse_wheel = 0;   /* config 33002 / 33009 / 33010 */
int qc_mru_n = 10;                                                    /* mcamxm.config 4023 "Number of functions in the MRU menu?" */


/* the factors from the user's Mastercam configuration, when it can be found (read only) - mcamui.c load_view_factors */
static void load_view_factors(void) {
    const char *paths[] = { mc_units_metric ? "CONFIG/mcamxm.config" : "CONFIG/mcamx.config", mc_units_metric ? "CONFIG/mcamx.config" : "CONFIG/mcamxm.config", NULL };   /* in paths.conf "user": the current configuration (IsMetric) first */
    for (int i = 0; paths[i]; i++) {
        char sp[1100]; FILE *f = fopen(site_file(sp, sizeof sp, "user", paths[i]), "r"); if (!f) continue;
        char line[512]; int n = 0;
        while (fgets(line, sizeof line, f)) {
            int id; char *q = strchr(line, '?');
            if (sscanf(line, "%d.", &id) != 1 || !q) continue;
            double v = atof(q + 1);
            /* grid (mcamxm.config [GRID SETTINGS] 23002-23007, colour 5032) */
            if (id == 23002 && (v == 1 || v == 2)) { grid.snap_mode = (int)v; continue; }
            if (id == 23003 && v > 0) { grid.dx = v; continue; }
            if (id == 23004 && v > 0) { grid.dy = v; continue; }
            if (id == 23005) { grid.ox = v; continue; }
            if (id == 23006) { grid.oy = v; continue; }
            if (id == 23007 && v > 0) { grid.size = v; continue; }
            if (id == 5032 && v >= 0 && v < 256) { grid.color = (int)v; continue; }   /* (256-colour palette) */
            if (!(v > 0 && v < 1000)) continue;
            if (id == 4026) { qc_vk_zoom = (float)fmin(v, 0.9); n++; } else if (id == 4027) { qc_vk_unzoom = (float)fmin(v, 0.9); n++; }
            else if (id == 4028) { qc_vk_pan = (float)v; n++; } else if (id == 4029) { qc_vk_rot = (float)v; n++; }
        }
        fclose(f);
        cfg_zoom_fac = qc_vk_zoom;                             /* mccore zoom_fac = config 4026 (wheel / zoom keys) */
        if (n) { fprintf(stderr, "view factors from %s: zoom %g unzoom %g pan %g rotate %g; grid %g x %g, origin %g,%g, size %g, snap mode %d, colour %d\n",
                         sp, qc_vk_zoom, qc_vk_unzoom, qc_vk_pan, qc_vk_rot, grid.dx, grid.dy, grid.ox, grid.oy, grid.size, grid.snap_mode, grid.color); return; }
    }
}

/* mcamui.c screen_colors_load: the items used by the view / ribbon so far, plus core_ui's (auto-highlight 5000 / 4036 /
   4001, select colour 5033, background 5007, dashed highlight 4056, undo depth 16013 / 16014: cui_config_item); the
   chaining, backplot, report items stay in mcamui.c until their phases move them */
static void screen_colors_load(void) {
    const char *paths[] = { mc_units_metric ? "CONFIG/mcamxm.config" : "CONFIG/mcamx.config", mc_units_metric ? "CONFIG/mcamx.config" : "CONFIG/mcamxm.config", NULL };   /* in paths.conf "user": the current configuration (IsMetric) first */
    for (int i = 0; paths[i]; i++) {
        char sp[1100]; FILE *f = fopen(site_file(sp, sizeof sp, "user", paths[i]), "r"); if (!f) continue;
        char line[512];
        while (fgets(line, sizeof line, f)) {
            int key, v; const char *q = strrchr(line, '?');
            if (sscanf(line, "%d.", &key) != 1 || !q) continue;
            char yn = 0; sscanf(q + 1, " %c", &yn);              /* view settings (mccore_spin.c: 0x80e9 = 33001 ...) */
            cui_config_item(key, q + 1);                         /* core_ui items */
            if (key == 33002) qc_dyn_free = yn == 'y';             /* dynamic_orbiting ('free' mode) */
            else if (key == 33007) fit_anim = yn == 'y';           /* GetAllowFitAnimation */
            else if (key == 33006) view_anim = yn == 'y';          /* GetAllowViewAnimation */
            else if (key == 33009) qc_use_shf_to_spin = yn == 'y'; /* useShfToSpin */
            else if (key == 33010) qc_reverse_wheel = yn == 'y';   /* ReverseMouseWheel */
            if (sscanf(q + 1, "%d", &v) != 1) continue;
            if (key == 33008) anim_delay = v;                      /* GetAnimationDelay */
            if (key == 5007) mc_rgb(v, bg_top); else if (key == 5009) mc_rgb(v, bg_bot); else if (key == 5001) mc_rgb(v, origin_col);
            else if (key == 5045) cfg_feed_color = v; else if (key == 5046) cfg_rapid_color = v; else if (key == 5052) cfg_solid_color = v; else if (key == 8027) cfg_outline_color = v; else if (key == 5054) cfg_srf_edge_color = v;
            else if (key == 5027) cfg_geom_color = v; else if (key == 5032) grid.color = v;
            if (key == 4023 && v >= 0) qc_mru_n = v;
        }
        fclose(f); cui_config_done(); return;
    }
}

void qc_load_config(void) {
    theme = 1; solid_mode = 1;                           /* mcamui main(): light theme (gradient), shaded */
    screen_colors_load();
    grid_defaults(1);
    load_view_factors();
}
/* the system units changed (core_ui cui_units_set -> cui_hooks.config_reload): ChangeConfiguration reads the new current configuration
   (ReadCfgFile) - its items again, the grid from its [GRID SETTINGS] over set_default_grid(IsMetric) */
void qc_reload_config(void) {
    note_pick_config_changed();
    screen_colors_load();
    grid_defaults(mc_units_metric);
    load_view_factors();
}

/* ------------------------------------------------------------------------------------------------ recent files */
/* core_ui recent_load / recent_add (<config dir>/recent.txt) */
void qc_recent_load(void) { recent_load(); }
void qc_recent_add(const char *path) { recent_add(path); }
int qc_recent_count(void) { return nrecent_files; }
const char *qc_recent(int i) { return i >= 0 && i < nrecent_files ? recent_files[i] : ""; }

/* ------------------------------------------------------------------------------------------------ document */
int qc_is_part_file(const char *p) { return is_part_file(p); }   /* .mcam, .mcx, .mcx-5 .. .mcx-9 (never written) */
int qc_loaded(void) { return loaded; }
extern char cad_doc_name[];
const char *qc_part_path(void) { return loaded ? (F.path[0] ? F.path : cad_doc_name) : ""; }

/* the document (core_ui cui_doc.c): a part opens with its PART.mcamui sidecar when there is one, a .mcamui opens its
   part; messages go through cui_hooks.message */
int qc_open(const char *path) { return open_part(path); }
int qc_part_newer(const char *path) {                    /* the header's file major (mcam_grp.h mcam_part_newer) */
    FILE *f = fopen(path, "rb"); unsigned char h[4 + 0x6c]; int r = 0;
    if (f) { if (fread(h, 1, sizeof h, f) == sizeof h) r = mcam_part_newer(h, sizeof h); fclose(f); }
    return r;
}
const char *qc_newer_dialog_msg(void) { return MCAM_NEWER_MSG "\n" MCAM_NEWER_SELECT; }
void qc_new(void) {                                      /* mcamui do_action ACT_NEW = Mastercam's init_db: empty part, Top view, level 1, depth 0 */
    doc_close(); new_file();
    view_std(1);                                         /* init_db: SetGrView(Top) */
    snprintf(machine, sizeof machine, "Mill");
    cam_msg[0] = 0;                                      /* File > New shows no message */
}
void qc_close(void) { doc_close(); }

static int part_major(const char *path) {                /* version byte of a part's header (0x12 X9, 0x13 2017) */
    FILE *f = fopen(path, "rb"); unsigned char h[0x70]; int v = 0x13;
    if (f) { if (fread(h, 1, sizeof h, f) == sizeof h) v = h[4 + 0x6a]; fclose(f); }
    return v;
}
const char *qc_save_ext(void) { return loaded && F.path[0] ? mcam_part_ext(part_major(F.path)) : ".mcam"; }

int qc_save_part(const char *dst, char *msg, size_t n) {
    if (!loaded) { snprintf(msg, n, "Open a part, or start one with File > New."); return 0; }   /* mcamui export_mcam_dialog */
    if (F.path[0] && same_file(F.path, dst)) { snprintf(msg, n, "Refusing to write %s: part files are never overwritten.", dst); return 0; }
    CamOp **sv; int nsv = dirty_save_list(&sv);           /* the ops made here (a dirty one's changed parameters: cui_dirty.c) + stored ops changed here */
    SldNew *sn = NULL; F.nsldnew = sld_new_build(&sn); F.sldnew = sn;   /* SOL-1: the solids made here (cui_sldsave.c), written by the save */
    int ok = mcam_save_part(F.path[0] ? F.path : NULL, dst, &F, sv, nsv, msg, n);   /* as mcamui export_mcam */
    sld_new_free(sn, F.nsldnew); F.sldnew = NULL; F.nsldnew = 0;
    free(sv);
    if (ok) { int nocc = 0; for (int i = 0; i < F.nsolids; i++) if (F.solids[i].brep) nocc++;
        if (nocc) { size_t k = strlen(msg); snprintf(msg + k, n - k, "\n\nThe %d solid%s imported from a STEP file %s not written to the Mastercam part (no Parasolid output): save the project (.mcamui) to keep %s.", nocc, nocc == 1 ? "" : "s", nocc == 1 ? "is" : "are", nocc == 1 ? "it" : "them"); } }
    if (ok) qc_recent_add(dst);
    return ok;
}

/* ------------------------------------------------------------------------------------------------ Toolpaths manager texts */
/* optree_text.inc reads the ops made here (core_ui camops: the project's ops) */
/* the item texts (optree_text.inc), tm_describe and the SystemPlanes names: core_ui cui_optree.c (phase 6) */
const char *qc_sys_plane(int i) { return cui_sys_plane(i); }
int qc_planes_mgr_rows(void) { return planes_mgr_rows(); }
void qc_planes_mgr_row(int i, char **cells, char *buf, int bufn) { planes_mgr_row(i, cells, buf, bufn); }
const char *qc_planes_mgr_g_header(void) { return planes_mgr_g_header(); }
const char *qc_plane_active_name(int type) { return plane_name(plane_active[type]); }

int qc_nops(void) { return loaded ? F.nops : 0; }
void qc_op_row(int i, char *out, size_t n) { cui_otx_op_row(i, out, n); }
void qc_params_row(int i, char *out, size_t n) { cui_otx_params_row(i, out, n); }
void qc_tool_row(int i, char *out, size_t n) { cui_otx_tool_row(i, out, n); }
void qc_geom_row(int i, char *out, size_t n) { cui_otx_geom_row(i, out, n); }
void qc_path_row(int i, char *out, size_t n) { cui_otx_path_row(i, out, n); }
int qc_op_stale(int i) { return op_dirty(i); }   /* the dirty image (op +0x94, cui_dirty.c): a changed op / tool, the Stock Setup regen mark */
long qc_op_npts(int i) { return i >= 0 && i < F.nops ? F.ops[i].npts : 0; }
int qc_nsolids(void) { return loaded ? F.nsolids : 0; }
const char *qc_level_name(int level) {                  /* the part's level table (mcam_load.h): the name, "" when none */
    struct McLevel *l = loaded ? mcam_level(&F, level) : NULL;
    return l && mcam_level_named(l) ? l->name : "";
}
int qc_level_visible(int level) { return loaded && mcam_level_visible(&F, level); }
/* the Level Manager (CSelLevel in manager mode; decomp/level_picker): NM_CLICK on an item without Ctrl / Shift (FUN_1804775c0, d2.txt
   164-192) -> FUN_180479960(row, column) (d3.txt 602-680); column 1 (Visible): the new state = not the row's mark; turning a level
   other than the main one visible clears GetOnlyMainVisible (never set here: its check box is not ported); then FUN_18047bba0
   (d3.txt 1630-1660): the main level stays visible while main_level_vis (the "main level visible" check box +0x2988) is on, else
   LevelData::setLevel(level, new state); FUN_18047bcc0 invalidates the graphics */
void qc_level_vis_click(int level) {
    if (!loaded) return;
    int nv = !mcam_level_visible(&F, level);
    if (level == geo_level && mc_main_level_vis) nv = 1;
    mcam_level_set(&F, level, nv); F.lvl_dirty = 1;
    scene_reupload(); overlay_dirty = 1;
}
void qc_level_counts(int counts[256]) {                  /* levels_ui.inc: entities, not polylines */
    memset(counts, 0, 256 * sizeof(int));
    for (int i = 0; loaded && i < F.nents; i++) if (F.ents[i].type && F.ents[i].level >= 0 && F.ents[i].level < 256) counts[F.ents[i].level]++;
}
