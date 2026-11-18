/* scene.h - shared 3D view state and functions */
#ifndef SCENE_H
#define SCENE_H
#include "mcam_load.h"
extern McamFile F; extern int loaded;
extern float ctrx, ctry, ctrz, ext;
extern double vmat[9], vcx, vcy, vht;   /* Mastercam view: matrix rows (screen right, up, to viewer), screen centre (view), height */
typedef struct { double m[9], cx, cy, h; } ViewState;
extern int view_w, view_h, fit_anim, anim_delay; extern double cfg_zoom_fac;
void view_get(ViewState *v); void view_put(const ViewState *v);
void view_set_matrix(const double m[9]);                       /* SetGrView */
void view_std(int n);
void view_std_animated(int n);
void view_rotate_keys(int h, int v, double rot_deg);
double view_mean_depth(void);
void view_axis_rotate_begin(const double pivot[3]);
int view_axis_rotate(int mode, double mx, double my_up, const double pivot[3]);           /* Alt+arrows, FUN_1803278f0 */                                 /* FUN_1801ccd70 transition (config 33006) */
int view_number(const double m[9]); extern int view_anim;                                          /* reserved view 1 Top .. 7 Iso, 8 Axonometric */
void view_px_to_view(float px, float py, double *x, double *y);
void view_to_world3(double vx, double vy, double vz, double out[3]);
void world_to_view3(const double p[3], double out[3]);
void view_fit(int animate);
void view_zoom_push(void); void view_unzoom_previous(void); void view_unzoom(double f);
void view_zoom_steps(int n, double px, double py, int wheel);
void view_wheel_run_end(void);                                 /* a left click ends a run of wheel zooms (+0x38d8) */
void view_rotate_free(double dx, double dy_up, const double pivot[3]);
void view_rotate_begin(void); void view_rotate_turntable(double dx, double dy_up, const double pivot[3]);
void view_pan_px(double dx, double dy_up);
void view_pan_steps(int sx, int sy, double pan_fac);
extern int ortho, show_geom, show_tp, show_rapid, show_grid, show_tool, solid_mode;
extern int scene_force_mesh;   /* draw the solid mesh even in Wireframe display (extrude preview, formats/extrude.md sec. 3) */
extern int solid_ntri, solid_faces, solid_meshed, solids_ok; extern double solid_scale; extern char solid_msg[160];
extern int sel; extern long sim; extern int playing; extern float sim_speed;
extern int bp_tool_on, bp_tool_num; extern double bp_tool_pos[3];
extern Tool bp_tool_op;   /* the op's own tool data when the part has no such tool (dia 0 = none) */
/* Backplot tool look (formats/backplot.md 13.4): display flag 7 tool / 8 holder; shaded = lit, blended, two culling
   passes; otherwise the same polygons as lines (Plain / Fluted); RGBA 0..1 (alpha = opacity); spin angle in degrees */
extern int bp_draw_cutter, bp_draw_holder, bp_tool_shaded; extern float bp_tool_rgba[4], bp_holder_rgba[4]; extern double bp_spin_deg;
extern const float *bp_ep; extern int bp_nep;   /* Backplot endpoints (13.3): N points x y z (world), white GL points of size 2 */
void mc_col(int idx, float *c);                       /* Mastercam palette colour index -> RGB */
extern int bp_lines_on; extern const float *bp_lv[2]; extern int bp_ln[2];   /* Backplot: the motions drawn instead of the toolpaths (x y z r g b, world) */   /* Backplot: tool drawn here (display flag 7) */
extern char path_buf[1024]; extern char status[256];
extern int theme;
extern int show_axes;
extern float bg_top[3], bg_bot[3], origin_col[3];             /* viewport gradient, origin line colour (0..1) */
extern int show_info_axes;
extern const unsigned char MC_PALETTE[256][3];
extern int cfg_feed_color, cfg_rapid_color, cfg_solid_color, cfg_geom_color, cfg_outline_color;   /* config 5045 / 5046 / 5052 / 5027 */                                   /* F9: axis lines through the origin */                                        /* origin axes + corner view indicator (Alt+F9) */
void scene_set_center(double x, double y, double z);        /* rotation centre (Alt+F12) */
void scene_pixel_to_world(int w, int h, float mx, float my, double out[3]);   /* on the grid plane */
/* the grid, as Mastercam's sel_grid (mastercam.dll, see ../formats/ui.md): spacing, origin (in the construction plane,
   at the construction depth), total size, snap mode 1 Near / 2 Always, colour index. show = show_grid. */
typedef struct { double dx, dy, ox, oy, size; int snap, snap_mode, color, style, at_floor; } GridSet;   /* style 0 infinite lines, 1 Mastercam dots; at_floor: under the part instead of Z 0 */
double grid_z(void);                                         /* Z of the grid plane (cursor / snap plane too) */
extern GridSet grid;
void grid_defaults(int metric);
/* snap a construction-plane point to the grid (rounds to the nearest multiple of the spacing from the origin) */
void grid_round(double *x, double *y);
extern double stock_lo[3], stock_hi[3]; extern int have_stock;
void gl_setup(void);
extern char draft_msg[400];   /* what the drafting layer drew for the current part (scene.c draft_build) */
void scene_draft_session(const unsigned char *recs, int n, unsigned len);   /* the session's drafting records (core_ui cui_draft.c) */
void scene_draft_rebuild(void);
void scene_notes_rebuild(void);                              /* ours: the notes (McamFile.notes, E_NOTE) drawn again */                             /* ours: the part's drafting rebuilt (records the session replaced: draft_set_skip) */
int load_file(const char *p);                                 /* 1 = ok; on failure the current part stays, status = reason */
void unload_file(void);
extern int part_file_units;                                  /* the open part's units as read (load_file: F.metric; new_file: -1) */
void new_file(void);                                         /* empty part: File > New */                                      /* close the part: empty scene */
long tp_total(void);
void draw_scene(int x, int y, int w, int h);
void view_anim_step(double now_ms);
extern float view_anim_ms;                                   /* 0 = no animation (headless shots) */
float scene_view_height(void);                              /* model units across the view height (for panning) */
int op_index(int id);
int scene_pick(int x, int y, int w, int h, float mx, float my, double out[3]);   /* cursor -> world (on Z = part bottom/0) */
void scene_reupload(void);
void scene_remesh_solids(void);
extern int (*scene_ent_hl)(int e);               /* the shaded face highlight (core_ui): bit 0 auto-highlighted, bit 1 selected */
extern int scene_hl_colors[2], scene_hl_stipple; /* config 5000 / 5033 colours, config 4053 StippleHighlight */
int scene_srf_look(int i);                       /* surface i's face highlight: 0 none, 1 selected, 2 / 3 auto-highlighted (3: stipple passes) */
extern int scene_tp_hide_id;                     /* Q20 (core_ui cui_preview.c): this op's toolpath is left out; 0 = none */
void scene_set_user_edges(const float *seg, int nseg);   /* their edges: x y z x y z per segment (sld_draw_solid_wire) */
int scene_part_tris(const float **tri);
int scene_user_tris(const float **tri);   /* the solids made here (Solids > Extrude ...), same format */
   /* the part file's solids as triangles (x y z nx ny nz per vertex, model units) */
int scene_part_surf_tris(int i, const float **tri);    /* F.surfs[i]'s display triangles (count; 18 floats each; 0 = not drawn) */
void scene_surfs_update(int full);                     /* the session's surfaces changed (new / deleted / hidden; full = an edited record) - GL context */
int scene_surf_wire(int i, const float **seg);         /* F.surfs[i]'s drawn wireframe: segments (12 floats: x y z r g b x2), model units */
int scene_mesh_tris(int i, const float **tri);          /* F.meshes[i]'s drawn triangles (count; 18 floats each; 0 = not drawn) */
int scene_mesh_wire(int i, const float **seg);          /* F.meshes[i]'s triangle outlines (segments, 12 floats each) */
void scene_meshes_changed(void);                        /* a mesh was added / deleted / changed: rebuilt at the next use */
void scene_surf_wire_build(void);                      /* (re)build the surfaces' wireframe for the current view if needed */
int scene_surf_skipped(void);                          /* surfaces not drawn (kinds other than NURBS) */
int scene_part_solid_tris(int i, const float **tri);   /* F.solids[i]'s triangles (count; 18 floats each) */
void scene_take_stock_mesh(float *v, int n, const float rgba[4]);   /* the shaded stock model (x y z nx ny nz, n vertices; the buffer becomes the scene's) */
extern void (*scene_part_closing)(void);   /* before load_file / new_file / unload_file free the current part (cui_geo.c: the running
                                              command ends on the document it was started on) */
extern void (*scene_part_changed)(void);   /* after load_file / new_file / unload_file (the machine group's tree name, cui_grpprops.c) */
extern void (*scene_upload_hook)(void);   /* at the start of every upload (before a load's fit): the surfaces' display-list nodes as
                                             store_ent_dl / FUN_18032d870 make them at the current view (core_ui cui_srfdl.c) */
extern void (*scene_draw_hook)(void);     /* at the start of every draw_scene: the repaint's display-list check (FUN_1801dadd0, cui_srfdl.c) */
extern int scene_part_gen;                /* +1 at every load_file / new_file / unload_file */
extern void (*scene_fit_extra)(void (*acc)(void *u, double x, double y, double z), void *u);  /* extra fit points (the stock, cui_stock.c) */
void scene_set_user_solids(const float *tri, int ntri);   /* x y z nx ny nz per vertex, model units; then scene_reupload */                                   /* after ops were added; keeps the view */
void scene_set_overlay(const float *v, int n);                /* n vertices (x y z r g b) drawn as GL_LINES */
void scene_set_gnomon(const float *tri, int nt, const float *lin, int nl);   /* Transform > Dynamic's gnomon: nt vertices x y z nx ny nz r g b a (lit
                                                               triangles, blended), nl x y z r g b (lines); drawn last, over everything */
void scene_set_dash(const float *v, int n);
void scene_take_dash(float *v, int n);
extern int scene_gl_skipped;                                  /* a GL upload was skipped for lack of memory (ours) */                        /* the same, the buffer becomes the scene's (no copy) */                   /* dashed line-style geometry (x y z r g b, GL_LINES), depth-tested */
void scene_set_verify(const float *v, int n, const float rgb[3], double z0, double z1);   /* the verify stock mesh (x y z nx ny nz); n = 0 hides */
int scene_to_screen(int w, int h, double x, double y, double z, float *sx, float *sy);
int view_std_matrix(int n, double m[9]);
unsigned scene_view_serial(int w, int h);                  /* changes whenever the projection of a w x h view changes (ours) */                   /* standard view n (1 Top .. 8) rows: x, y, z axes */
#endif
