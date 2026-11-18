/* core_bridge - the C side of the Qt UI: everything that needs viewer/paths.h / platform.h (static inline C, not
   compiled as C++), the user's Mastercam configuration, the recent-files list and document actions (now in
   viewer/core_ui), saving as a Mastercam part, the Toolpaths manager texts (viewer/optree_text.inc). Behaviour and
   citations are moved from viewer/mcamui.c (formats/qt_port_plan.md phases 1-2). The shared UI behaviour itself is
   called directly through viewer/core_ui/core_ui.h. */
#ifndef CORE_BRIDGE_H
#define CORE_BRIDGE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* folders (platform.h / paths.h) */
const char *qc_config_dir(void);                       /* ~/.config/mcamui, %APPDATA%\apprenticeCAM */
const char *qc_exe_dir(void);
const char *qc_config_file(char *out, size_t n, const char *name);
const char *qc_site_file(char *out, size_t n, const char *key, const char *rel);   /* "" when paths.conf KEY unset */

/* the user's Mastercam configuration (CONFIG/mcamxm.config, read only): mcamui.c load_view_factors +
   screen_colors_load, the items the Qt UI uses so far */
void qc_load_config(void);
void qc_reload_config(void);                 /* the system units changed: the current configuration read again */
extern float qc_vk_zoom, qc_vk_unzoom, qc_vk_pan, qc_vk_rot;   /* config 4026-4029 */
extern int qc_dyn_free, qc_use_shf_to_spin, qc_reverse_wheel;  /* config 33002 / 33009 / 33010 */
extern int qc_mru_n;                                            /* config 4023 */

/* recent files (<config dir>/recent.txt, mcamui.c recent_load / recent_add) */
void qc_recent_load(void);
void qc_recent_add(const char *path);
int qc_recent_count(void);
const char *qc_recent(int i);

/* document (GL context current: scene.c uploads) */
int qc_open(const char *path);                         /* core_ui open_part: 1 = opened; 0 = the current document stays (message hook says why) */
int qc_part_newer(const char *path);                   /* 1 = a part file from a newer release than Mastercam 2017 (viewer/mcam_grp.h) */
const char *qc_newer_dialog_msg(void);                 /* the Open dialog's message for it: mccoreres 5280 + "\n" + 5301 (DoFileDialog) */
void qc_new(void);                                     /* File > New (init_db) */
void qc_close(void);
int qc_save_part(const char *dst, char *msg, size_t n);   /* Mastercam part via mcam_saveops.c */
const char *qc_save_ext(void);                         /* ".mcam" / ".mcx-9": the open part's version */
int qc_loaded(void);
const char *qc_part_path(void);
int qc_is_part_file(const char *p);

/* Toolpaths manager rows (optree_text.inc) */
int qc_nops(void);
void qc_op_row(int i, char *out, size_t n);
void qc_params_row(int i, char *out, size_t n);
void qc_tool_row(int i, char *out, size_t n);
void qc_geom_row(int i, char *out, size_t n);
void qc_path_row(int i, char *out, size_t n);
int qc_op_stale(int i);
long qc_op_npts(int i);
int qc_nsolids(void);
void qc_level_counts(int counts[256]);
const char *qc_level_name(int level);
int qc_level_visible(int level);                     /* LevelData::isVisible (the Visible column's mark, FUN_180249e00 col 1) */
void qc_level_vis_click(int level);                  /* a click on a row's Visible cell (FUN_180479960 column 1) */
const char *qc_sys_plane(int i);                     /* the seven SystemPlanes names (levels_ui.inc) */
int qc_planes_mgr_rows(void); void qc_planes_mgr_row(int i, char **cells, char *buf, int bufn); const char *qc_planes_mgr_g_header(void);   /* the Planes manager (cui_planes.c) */
const char *qc_plane_active_name(int type);          /* the name of the active plane of PlaneType type (MastercamPlanes::GetActiveName 0x1802c56d0): status bar */

#ifdef __cplusplus
}
#endif
#endif
