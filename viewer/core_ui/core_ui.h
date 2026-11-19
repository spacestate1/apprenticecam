/* core_ui - the UI behaviour of the front end (viewer-qt = Qt 6; until 2026-10-22 also viewer/mcamui.c = nuklear, removed
 * in formats/qt_port_plan.md phase 9 - comments naming mcamui.c refer to the copy in
 * ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz), plain C with no
 * toolkit: the document (part + .mcamui project), entity / operation selection, picking, AutoCursor snap, the
 * highlight / selection overlay (the decoded FUN_180135ad0 stripes), the Extrude feature list the project stores, the
 * keymap and right-click menu files, undo / redo (formats/qt_port_plan.md phase 2).
 *
 * The code was moved out of mcamui.c unchanged; the names it had there are kept (pick_entity, geo_snap, build_overlay,
 * undo_push ...) so the decomp citations next to it and the callers stay as they were. What the moved code needs from a
 * front end goes through `cui_hooks` (message boxes, the UI command state a document change resets, the extra overlay
 * lines of commands that have not moved yet).
 *
 * Files: cui_doc.c (document, recent files), cui_feat.c (Extrude features / bodies), cui_geo.c (the wireframe
 * commands and their function panels' model, phase 3), cui_sel.c (selection, picking,
 * window / polygon select, auto-highlight rule, operation multi-selection, pivot markers, keymap, right-click menu,
 * config items), cui_snap.c (AutoCursor), cui_overlay.c (stipple patterns, ov_entity, build_overlay, snap marker),
 * cui_undo.c (undo / redo), cui_chain.c (the chaining state machine, the Chaining / Chaining Options / Drill Point
 * Selection dialogs' model, chain arrows; phase 4), cui_extrude.c (the Extrude command, its panel model, the drag
 * arrows, the Solids manager model; phase 5), cui_ops.c (the operations: pending op, generate / edit / delete / clipboard,
 * the Toolpaths manager menu model; phase 6), cui_opdlg.c (the toolpath dialog's model: page tree, pages, Linking, frame),
 * cui_toolpage.c (its Tool page, menu 1247, the Tool List 6410, the tool list columns / library / part tools). */
#ifndef CORE_UI_H
#define CORE_UI_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {                                     /* the core headers have no C++ guards of their own */
#endif
#include "scene.h"
#include "cam2d.h"
#include "geo2d.h"
#include "solid25.h"
#include "occt_solid.h"
#include "xt_mesh.h"
#include "toollib.h"
#include "feedspeed.h"
#include "toolwiz.h"
#include "rpx.h"

/* ------------------------------------------------------------------------------------------------ front-end hooks */
typedef struct {
    void (*message)(const char *title, const char *text);   /* a message box (mcamui message(), the Qt QMessageBox) */
    void (*clear_ops)(void);         /* doc_clear_ops: the UI's command state (chaining, op dialog, Verify ...) */
    void (*solids_cleared)(void);    /* solids_clear: the Extrude command ends (mcamui ex_mode = 0) */
    void (*ops_changed)(void);       /* undo apply / remove_stored_op changed the operation list (tree refresh) */
    int (*message_check)(const char *title, const char *text, const char *check);   /* a message box with an OK button and a check box
                                                                (CHECK its text); 1 = checked at OK. NULL = cui_script_check */
    int (*ask_yes_no)(const char *caption, const char *text);   /* a Yes / No question (Yes the default button); 1 = Yes. NULL = Yes */
    int (*ask_choice)(const char *caption, const char *text, const char **buttons, int n);   /* a dialog of N push buttons: the index pressed
                                                                (the last = Cancel / closed); NULL = cui_script_choice */
    int (*ask_save_path)(char *path, size_t n, const char *suffix);   /* a Save As file dialog on PATH (the platform's overwrite question);
                                                                1 = OK (PATH = the chosen file), 0 = Cancel */
    /* build_overlay: entities drawn in the select colour besides the selection (chained, picked by a command) and the
       ones the hover highlight leaves alone (chained); extra overlay lines of the commands still in the front end
       (drill points, drawing preview, Extrude arrows), appended after the hover highlight as before */
    int (*ov_chained)(int e);
    int (*ov_picked)(int e);
    void (*ov_extra)(float **pv, int *pn, int *pcap);
    /* the wireframe commands (cui_geo.c): File > New when nothing is open (mcamui do_action(ACT_NEW)); a command
       starts - the UI ends its toolpath chaining / op dialog (cam_mode, show_params, cam_clear_selection) and brings
       the command's function panel tab to the front (mcamui dock_show_panel) */
    void (*new_part)(void);
    void (*geo_started)(void);
    /* chaining (cui_chain.c): set vp_* to the graphics view (mcamui vp_main); the Chaining dialog's OK with chains / the
       Drill Point Selection's OK (mcamui: the Extrude command's ex_chains_done, else open_params = the op dialog);
       Unselect All asks MCMessageBox 5007 "Are you sure?" / caption 10206 "Unselect All Chains?", Yes = chain_unselect_all */
    void (*view_port)(void);
    void (*chain_ok)(void);
    void (*chain_ask_unselect_all)(void);
    /* the message box shown since the last script step (mcamui show_msg / msg_title / msg_text, cleared by dump_state):
       the Extrude script steps print it (cui_extrude.c) and close it */
    int (*msg_open)(const char **title, const char **text);
    void (*msg_close)(void);
    /* the operations (cui_ops.c, phase 6): the Toolpaths manager menu items only a front end has (CM_EDIT Edit Common
       Parameters, CM_EXPAND / CM_COLLAPSE the tree's open flags, CM_TOOLLIST the tool report, CM_RENUMTOOLS Renumber
       tools, CM_GRP_RENAME the rename box); the Tool page's Define / Edit Tool wizard (index -1 = a new tool) and Tool
       Manager (NULL = not in this front end: the menu items are greyed); a library was opened (the Tool Manager's library
       list selection resets) */
    void (*op_menu)(int act);
    void (*tool_wizard)(int index);
    void (*tool_manager)(void);
    void (*lib_opened)(void);
    /* the Tool Manager closed (core_ui tm_close_now, phase 7): the front end's menu / window state */
    void (*tm_closed)(void);
    /* dialog 977 answered with OK (cui_dirty.c dop_answer): the front end runs the command it held (DOP_POST / DOP_BACKPLOT /
       DOP_VERIFY), with its graphics context for Backplot / Verify */
    void (*dop_resume)(int action);
    /* the system units changed (cui_units_set): ChangeConfiguration's ReadCfgFile of the new current configuration - the front
       end reads its items again from mcamxm.config / mcamx.config (cui_config_item, grid, view factors, colours) */
    void (*config_reload)(void);
    /* get_angle / get_real (mccore @1804e9620 -> mastercam.dll RequestRealInputModal @180327430): opmanres dialog 286 (a 154 x 16
       box, ComboBox 254) titled with PROMPT, *V the default; 1 = OK (*V = the value), 0 = Cancel. NULL = cui_script_real(_ok) */
    int (*ask_real)(const char *prompt, double *v);
    /* a Home command's selection ended (cui_attr.c selrun: Change Level, Set All): the front end opens the command's dialog
       (SR_LEVEL: opmanres 516 "Change Levels", SR_ATTRIBS: 192 "Attributes"); NULL = the script values (cui_attr.c) */
    void (*selrun_dialog)(int kind);
    /* Drafting > Note's dialog (mccoreres 452 "Note Dialog", CNoteDlg; core_ui cui_draft_note.c NOTE_452 / dn_dlg_*): TEXT (N bytes, 8-bit,
       "\r\n" line breaks) and *RADIO (0..7: the Create radios 3888..3895) in and out; 1 = OK. NULL = the script answer (notedlg step) */
    int (*note_dialog)(char *text, size_t n, int *radio);
    /* a file dialog on PATH (Geometry Nesting's sessions, FUN_1800d6590): SAVE = PromptForFileToSave (the platform's overwrite
       question) else PromptForFileToOpen; TITLE, FILTER ("Label (*.a *.b)"), the default SUFFIX; 1 = OK (PATH = the file). NULL /
       scripts = Cancel (a script answers with nestfile:PATH) */
    int (*ask_file)(char *path, size_t n, int save, const char *title, const char *filter, const char *suffix);
    /* KeyPress(VK) (mastercam.dll @1803221b0), asked by a computation that runs on the UI thread (Geometry Nesting's progress
       callback FUN_180006700 L4613-4616): one keyboard message taken from the queue (PeekMessage 0x100..0x109, PM_REMOVE); 1 when it
       is a key-down of VK; another key-down: the view keys (FUN_1803222f0); other input stays queued until the run ends. NULL = never */
    int (*key_press)(int vk);
    void (*key_press_end)(void);   /* the computation ended: the input still queued is handled (Windows dispatches it after the run) */
    /* a dialog of N radio buttons with OK / Cancel (S4: mcmsurfres 100 "Select Boss/Cavity", CBossCavity): *SEL in (the radio checked
       at the start) and out; 1 = OK, 0 = Cancel (*SEL unchanged). NULL = the script answer (step bosscav:N, -1 = Cancel) */
    int (*radio_dialog)(int dlg, const char *title, const char **labels, int n, int *sel);
    /* N13-9: the generation at the end of a new 3D op's flow (after the picks that follow the op dialog) is queued for the front end,
       which runs it as the op dialog's OK does (worker thread + progress window): ms_gen_take, gen_run, gen_end. NULL = computed at
       once on the calling thread */
    void (*gen_queued)(void);
} CuiHooks;
extern CuiHooks cui_hooks;
void cui_message(const char *title, const char *fmt, ...);
extern char cui_status[300]; extern int cui_status_gen;   /* UpdateStatusBarText's pane 0 text (cui_doc.c) */
void cui_status_text(const char *t);
extern char cam_msg[300];                        /* the prompt line (mcamui: the status message) */

/* ------------------------------------------------------------------------------------------------ config */
/* mcamxm.config items of the moved features: cui_config_item(key, text after '?') per line, cui_config_done() after
   the file (screen_colors_load in mcamui.c, qc_load_config in viewer-qt) */
void cui_config_item(int key, const char *val);
void cui_config_done(void);
extern int cfg_autohl_color, cfg_autohl, cfg_presel, cfg_select_color, cfg_back_color, cfg_dashed_wire, cfg_stipple_hl;
#define cfg_chain_tol (mc_systol.chain_tol)      /* 1001 Chaining tolerance (chain_tol: McSysTol, mcam_load.h; also the Chaining Options' FUN_1801f9680) */
extern int cfg_live_color, cfg_pt_style;          /* 5026 Live entity color, 10002 Point style */
int mc_rgb(int idx, float *rgb);                  /* Mastercam colour number -> RGB (MC_PALETTE, scene.c) */

/* ------------------------------------------------------------------------------------------------ document (cui_doc.c) */
#define MAX_CAMOPS 1024                                      /* = the Operations Manager selection size (op_msel) */
extern CamOp *camops[MAX_CAMOPS]; extern int ncamops;
extern char doc_path[1100];          /* project file, "" = not saved yet */
extern int doc_dirty;                /* operations / tools changed since the last save */
extern char machine[16];             /* machine type of the document (MACHINE line) */
extern char recent_files[12][1100]; extern int nrecent_files;
int ends_with_ci(const char *s, const char *e);
int is_project_file(const char *p);
int is_part_file(const char *p);
int file_exists(const char *p);
void config_dir_make(char *out, size_t n);   /* the config dir, created (platform.h) */
void recent_load(void);
void recent_add(const char *path);
void doc_clear_ops(void);
int doc_write(const char *path);
int doc_read(const char *proj, const char *part_override);
int open_part(const char *p);        /* 1 = opened (part, its sidecar, or a project); 0 = the previous document stays */
void doc_close(void);
int cui_doc_save_to(const char *path);   /* doc_save_to without the follow-up action: 1 = written */
/* doc_save's choice: 1 = save to `path` directly; 0 = ask (dialog at dir / name); -1 = nothing to save (message shown) */
int cui_doc_save_target(int as, char *path, size_t np, char *dir, size_t nd, char *name, size_t nn);
void remove_stored_op(int id);
void camops_detach(void);
void camops_attach(void);

/* ------------------------------------------------------------------------------------------------ features (cui_feat.c) */
#define EX_TOL (mc_systol.tol)   /* tol, the system tolerance (config 1000: McSysTol, mcam_load.h; formats/mcmill_2d_engines.md sec. 0: 5e-5) */
#define EX_NCTOL 1e-4    /* nc_tol (same source); sec. 1.2: chain entities in the chain plane "within nc_tol" */
#define EX_MINDIST (EX_TOL * 10)   /* app+0x8 = tol * 10 (SolidsAppBase ctor FUN_180164c60, decomp/extrude/b5.c:146):
                                      the "Below minimum distance." (sld 1101) check and the AutoCursor threshold */
/* one feature = one SldExtrudeOp (formats/extrude.md sec. 6): P, the chains, per chain its direction (SLD_EXT_CHAIN_DATA,
   here +1 / -1 along Z), the operation name and "Create a single operation" */
/* prim (ours storage of a SldPrimitiveOp, decomp/mcgeomsld_primop.c): 0 = an Extrude feature; 15 Block / 17 Cylinder (the op
   types sld_basic_primitive records, opmanres 27601 + type = "Block" / "Cylinder") made by the Bounding Box panel's Solids
   output: pv = box[6] (xmin xmax ymin ymax zmin zmax, world = the Top view here) / cylinder r, len, c[3], dir[3] */
/* level / color (SOL-5d): the new body's SOLID_ID level and colour, taken when the feature is made - dbput_solid (mccore 0x1802da1a0,
   decomp/solid_history/h19_dbput_solid.c) stores the solid with store_ent(.., *solidColor_exref, main_level, main_attrib): the solid colour
   (config 5052) and the main level at that moment; 0 = not recorded (an older project: the save's current values) */
/* fl (cui_fillet.c, formats/solid_fillet.md): a Constant Fillet op on body p.target (sld_fillet: one SldFilletOp per body; p.type = EX_CUT:
   it modifies its body); NULL = not a fillet. The picks are kept as Mastercam keeps them (McSldEntOp lists A / B): the face's / edge's
   record (its name, -1 none) + its bbox centre (ps_get_bbox_center, part units); whole = a body pick (op+0x90) */
typedef struct FilFeat { int whole; int nf, ne; int *frec, *erec; double (*fpt)[3], (*ept)[3]; double radius; int overflow, mitered, propagate; } FilFeat;
typedef struct { ExtrudeParams p; int nch; Chain *ch; int *cdir; char name[64]; int single; int prim; double pv[8]; int level, color; FilFeat *fl; } ExFeat;
FilFeat *filfeat_dup(const FilFeat *f);
void filfeat_free(FilFeat *f);
int fil_apply(Body **bs, int *nb, ExFeat *f, ExErr *e);              /* cui_fillet.c: the feature on its body (feat_apply calls it) */
int prim_body(int prim, const double *pv, const char *name, Body *out);   /* the body of a Block / Cylinder (cui_feat.c) */
/* SOL-3 (cui_feat.c): the features on the OpenCASCADE kernel (occt_solid.h) - the groups of a feature as the kernel's input */
typedef struct { OcsGroup *g; OcsChain *ch; OcsSeg *seg; int *first; int ng; } OcsIn;   /* first[k] = group k's first chain (ExFeat.ch index) */
int feat_ocs_groups(const ExFeat *f, OcsIn *in, ExErr *e);
void ocsin_free(OcsIn *in);
void ocs_params_of(const ExtrudeParams *ep, int single, OcsParams *p);
double feat_xt_scale(void);                                          /* part units per metre */
Body body_of_occ(OcsBody *ob, const char *name);                     /* takes the reference */
struct SldNew; int sld_new_build(struct SldNew **out);              /* cui_sldsave.c: the solids made here for the part save (sld_ops.h) */
/* undo record payload: the features one OK / Apply made (sec. 6: one per body / group) */
typedef struct { int n; ExFeat *f; } FeatPack;
typedef struct { int *ix, n; } ExGrp;
extern ExFeat *feats; extern int nfeats;
extern Body *sbodies; extern int nsbodies;
extern int feat_body[4096];
const char *ex_defname(int type);
int ex_is_defname(const char *n);
int ex_nvis(void);
void chain_copy_into(Chain *d, const Chain *s);
ExFeat feat_dup(const ExFeat *f);
void feat_free(ExFeat *f);
void feat_release(void *p);
int ex_chain_xy(const Chain *c, double *z);
int ex_chain_normal(const Chain *c);
int ex_wind(const double *p, int n, double x, double y);
double ex_pdist(const double *p, int n, double x, double y);
int ex_grouping(const ExFeat *f, ExGrp **out, int *ng, ExErr *e);
int feat_groups(const ExFeat *f, Profile **out, int *ng, ExErr *e);
void groups_free(Profile *g, int n);
int feat_apply(Body **bs, int *nb, ExFeat *f, ExErr *e);
void bodies_free(Body **bs, int *nb);
Body *bodies_dup(int *nt);
void bodies_show(const Body *bs, int nb);
void bodies_show_with(const Body *bs, int nb, const Body *extra);
void solids_rebuild(void);
void solids_clear(void);
void feat_push(const ExFeat *f);
void feat_pop(void);
void feats_write(FILE *f);
void feats_read(FILE *f);

/* ------------------------------------------------------------------------------------------------ undo (cui_undo.c) */
enum { U_ENTS_ADD, U_ENTS_DEL, U_OP_ADD, U_OP_DEL, U_OP_EDIT, U_ENTS_MOD, U_FEAT, U_DRAFT, U_DRAFTEDIT, U_SURFEDIT, U_ATTR };   /* U_ATTR: attributes changed in place (ax, cui_attr.c) */   /* U_SURFEDIT: surface records rewritten here (sx = SurfEdits, cui_surf.c) */   /* U_DRAFTEDIT: existing drafting rewritten (idx = cui_draft_pick.c transaction) */   /* U_DRAFT: a drafting entity made here (idx = McamFile.dnew index; cui_draft.c) */
/* U_ENTS_MOD: an edit (trim, fillet ...): entities added (ents) and switched off (dents); U_FEAT: a solid feature (feat) */
typedef struct { int kind; int *ents; int *types; int n; CamOp *op, *op2; int idx; int *dents, *dtypes, nd; void *feat;
                 int *pairs; void *sx; void *ax; int *bents, nb; } URec;   /* U_ENTS_MOD of a Move: pairs[i] = the dents index ents[i] replaces (keeps its id), else -1 */
extern URec *undo_st, *redo_st; extern int nundo, nredo, cap_undo, cap_redo, undo_limit, undo_lim_on, undo_lim_n;
extern int *poly_stash; extern int npoly_stash;
extern unsigned char *ent_hidden; extern int nent_hidden, any_hidden;
void urec_free(URec *r);
void ust_grow(URec **st, int *cap, int n);
void ent_off(int i);
void ent_on(int i, int type);
void undo_clear(void);
void undo_reset(void);
void undo_push(URec r);
extern int mod_from; extern URec mod_rec;   /* the edit being collected (Analyze restores it on Cancel) */
void mod_begin(void);
void mod_del(int i);
void mod_blank(int i);                  /* blank a kept entity inside the edit (sel bit 0x100; Undo unblanks: Break Many's Blank) */
void mod_end(void);
void undo_ents_added(int from);
void feat_undo(URec *r, int undo);
void undo_apply(URec *r, int undo);
void do_undo(void);
void do_redo(void);

/* ------------------------------------------------------------------------------------------------ selection / picking (cui_sel.c) */
/* selection aperture: sel_dist = (view height / 30) / (window height / 522) world units (set_parms), i.e. 17.4 px */
#define SEL_APERTURE_PX (522.0 / 30.0)
extern int vp_x, vp_y, vp_w, vp_h;               /* the graphics viewport in window coordinates (y down) */
extern unsigned char *ent_sel; extern int ent_sel_n, nselected;
extern int hover_ent;                            /* the entity under the cursor, drawn highlighted (build_overlay) */
extern int sld_hover_body;                       /* the body under the cursor during a solid body pick (sld_identify_from_cursor
                                                    hit type 1; formats/ui.md "Solid selection hover highlight"), -1 none */
extern unsigned char op_msel[1024]; extern int msel_owner;   /* Operations Manager multi-selection (op_click) */
double seg_dist(float px, float py, float ax, float ay, float bx, float by);
/* in Mastercam's display list: an entity on a visible level (draw_ent / LevelData::isVisible, mcam_load.h mcam_level_visible); the
   picks, window / polygon selection, AutoCursor and chaining picks walk only these (select_ent_univ / dbselwin / dl_search_fast) */
#define ENT_LV_SHOWN(en) (!(en)->blanked && mcam_level_visible(&F, (en)->level))   /* + not blanked (sel bit 0x100: draw_ent / select_ent_univ) */
int pick_entity(float mx, float my);
int pick_point_under(float mx, float my, double out[3]);
void sel_grow(void);
int is_sel(int i);
int sel_list(int **out, int all_if_none);
int sel_list_surfs(int **out);
#define DS_TEMP (-2)    /* dyno_surf / the curve commands: the entity index standing for ce_temp_surf (a solid face converted, cui_cedge.c) */
extern const Surf *ce_temp_surf; const Surf *ds_surf(int e); int ce_face_temp(int si, int fi, char *msg, size_t msgn);
/* cui_cedge.c: the solid side of Curve One Edge / All Edges */
int ce_pick_edge(float mx, float my, int *si, int *ei); int ce_sld_edge_one(int si, int ei, int level, int color, char *msg, size_t msgn); int ce_sld_edges(int si, int fi, int open_only, int level, int color, int *made, char *msg, size_t msgn); int ce_open_refused(const int *surf_ents, int ns, char *msg, size_t msgn); int ce_open_edges(const int *surf_ents, int ns, double tol, int level, int color, int *made, char *msg, size_t msgn); void ce_dump_edges(int si); void ce_scan(void);
void srf_drop_base(int e);                 /* a removed trimmed / offset surface's unreferenced blanked base, in the open edit (cui_sel.c) */
extern int srf_pick_active;               /* a Surfaces command's surface prompt is up: pick_entity takes surfaces (cui_sel.c) */
int pick_surfs_ok(void);
/* chaining's surfaces (formats/chaining.md "Surfaces in the Chaining dialog"): the Chaining dialog in Single mode with config 6026 on takes
   surfaces (cui_chain.c) */
int chain_srf_pick(void);
/* cui_srfdl.c: the surfaces' display-list nodes (mcsrfdl.c) and the shading-off pick on them (FUN_18013cbc0): 1 found (*d view units,
   uv), 0 built but nothing nearer than best, -1 no built node (dist_to_surf), -2 not evaluable here (*why) */
int srfdl_pick(int surf, const double cur[2], double best, double *d, double uv[2], const char **why);
int srfdl_info(int surf, int *n, int *nloop);
/* gl_Hit_Surfs (cui_srfdl.c, mcglhit.c): the shading-on surface pick at the cursor (view x / y) among the surfaces ok() takes; 1 = hit
   *srf (F.surfs index) with uv (*uv_set 0: no projection wrote one) and *hitz the hit's view z, 0 none, -2 not ported (*why); *noport = candidate surfaces whose
   shading mesh is not ported (no node here) */
int srfhit_pick(const double cur[2], int (*ok)(int), int *srf, double uv[2], int *uv_set, double *hitz, int *noport, const char **why);
int srfhit_point(int surf, const double uv[2], double P[3]);
int srfhit_ported(int surf);
/* cui_sel.c: the (u, v) of the last shaded surface pick (select_ent_univ's buffer, CS-21); CS-20: the Ent under the cursor whose shaded
   pick is not decoded (-1 none) and its click message (1 shown) */
extern double pick_srf_uv[2]; extern int pick_srf_uv_ok, pick_noport_under;
int pick_noport_msg(void);
/* cui_chain.c: dist_to_surf's untrimmed branch (mccore 0x1802ddba0): the cursor's view x / y, maxd in model units; *dout the distance */
int dist_to_surf_uv(const Surf *s, const double pk[2], double maxd, double *uv, double *dout);
int srfdl_dist_trimmed(int surf, const double cur[2], double best, double *d, double uv[2], const char **why);   /* dist_to_surf, kind 10: FUN_1802de180 */
/* the Surfaces tab (cui_surf.c, formats/surfaces_tab_plan.md) */
enum { SRF_CHNORM = 1, SRF_REVOLVE, SRF_BLOCK, SRF_CYL, SRF_SPH, SRF_CONE, SRF_TOR, SRF_FLAT, SRF_SOLID, SRF_LOFT, SRF_SNSET, SRF_UNTRIM, SRF_RMBND, SRF_EXT, SRF_TRIMS, SRF_OFFSET };
extern int srf_cmd;                             /* the running Surfaces command, 0 none */
int srf_is_ft(const char *ft);                  /* a Surfaces-tab FT function (ported or not) */
int srf_find_ft(const char *ft);                /* its command when ported, else 0 */
int srf_start(const char *ft);
int srf_crv_flow_exec(int e, int method, int number, double chord, double dist, int direction, double tol, int level, int color, char *msg, size_t msgn);   /* Curve Flowline */
int srf_crv_supported(int e, char *msg, size_t msgn);
void srf_test_pmdcase(int e, const char *path);
void srf_test_scan(void);
void srf_test_iso(int c, int e, int dir, double par);
void srf_test_pt(int e, double fu, double fv); void srf_test_dev(int c, int e);   /* tests */
int srf_crv_const_exec(int e, double u, double v, double tol, int direction, int level, int color, char *msg, size_t msgn);   /* Curve Constant Parameter (cui_surf.c, mcsrfcrv.c) */
void srf_end(void);                             /* Esc: the command ends (one undo step) */
int srf_click(float mx, float my);
int srf_pick_ent(int e);                       /* tests: a pick of entity e */              /* a graphics click; 1 = taken */
void srf_ov(float **pv, int *pn, int *pcap);    /* overlay: the normal arrow */
void srf_edits_free(void *p);
void srf_tx_save(int s); void srf_tx_commit(void); void srf_tx_cancel(void);   /* a surface edit transaction (cui_surf.c) */
void srf_edits_apply(void *p, int undo);
int srf_rev_axis_ok(void); void srf_rev_reset(void); int srf_rev_take(int e, V3 w);   /* Revolve's axis (G_SREV) */
int srf_rev_exec(const Chain *ch, int nch, double sa_deg, double ea_deg, int dir2, int level, int color, char *msg, size_t msgn);            /* the selected surfaces (E_SURF Ent indices; cui_sel.c) */
void srf_prim_frame(int kind, int axis, double A[3], double X[3], double Y[3]); void srf_blk_anchor_signs(int *an, double L, double W);
extern int srf_solid_sel[64], srf_solid_nsel;   /* From Solid: the selected part solids (F.solids indices) */
int srf_solid_selecting(void); void srf_solid_enter(void);   /* From Solid's body selection (cui_geo.c) */
/* Normals > Set / Untrim (cui_srfedit.c) */
int srf_sn_selecting(void); int srf_sn_active(void); const char *srf_sn_prompt(void); void srf_sn_begin(int show); int srf_sn_pick(int e); int srf_sn_enter(void);
int srf_sn_button(int code); void srf_sn_values(int show, int dir); void srf_sn_end(void); void srf_sn_ov(float **pv, int *pn, int *pcap); void srf_sn_dump(void);
int srf_ut_active(void); const char *srf_ut_prompt(void); void srf_ut_begin(int keep); int srf_ut_pick(int e); void srf_ut_values(int keep); void srf_ut_end(void);
/* Surfaces > Offset (OffsetSurfaces, cui_srfedit.c) */
int srf_of_active(void); int srf_of_selecting(void); int srf_of_flip_enabled(void); const char *srf_of_prompt(void); void srf_of_begin(void);
int srf_of_pick(int e); int srf_of_enter(int move, double dist); int srf_of_button(int code); void srf_of_values(int move, double dist); void srf_of_end(void);
void srf_of_ov(float **pv, int *pn, int *pcap); void srf_of_dump(void);
void *srf_tx_take(void);                        /* the open surface transaction handed over (a U_ENTS_MOD's sx; cui_surf.c) */

void srf_trim_dump(void); void srf_tp_dump(void); int srf_rb_active(void); void srf_rb_begin(void); void srf_rb_end(void); int srf_rb_begin_dyno(int e, V3 w, char *msg, size_t msgn); int srf_rb_exec(int e, double u, double v);   /* Remove Boundary (cui_srfedit.c) */
void srf_tp_subst_sync(void);                  /* after an undo / redo: the session ops' references re-pointed by tp_substitute_ent (cui_srfedit.c) */
int geo_has_panel(int c);                       /* 0: the command has no function panel (Remove Boundary; cui_geo.c) */
int srf_ned_clicks(void);                       /* Set Normals / Untrim run: graphics clicks go to geo_click (cui_geo.c) */
void srf_ned_enter(void);                       /* Enter: Set Normals' End Selection (cui_geo.c) */
int srf_solid_exec(int use_current, int level, char *msg, size_t msgn);   /* From Solid (cui_surf.c) */
int srf_loft_exec(Chain *ch, int nch, int ruled, int level, int color, char *msg, size_t msgn);   /* Loft (cui_surf.c) */
int srf_flat_exec(Chain *ch, int nch, int level, int color, char *msg, size_t msgn);   /* Flat Boundary (cui_surf.c) */
enum { SRF_DRAFT = 40, SRF_EXTRUDE = 41 };   /* Surfaces > Create > Draft / Extrude (cui_surf.c; values apart from the SRF_* list above) */
int srf_draft_exec(const Chain *ch, int nch, double len, double ang_deg, int lflip, int aflip, int split, int level, int color, char *msg, size_t msgn);   /* Draft (G_SDRAFT) */
void srf_draft_sync(double *len, double *run, double ang, double *plen, double *prun, double *pang);   /* the dialog's Running Length */
int srf_extr_exec(const Chain *ch, int nch, double prm[12], int level, int color, int **list, int *nlist, char *msg, size_t msgn);   /* Extrude (G_SEXTR) */
void srf_extr_axis_out(const double a[3]); void srf_extr_axis(double a[3]); void srf_extr_reset(void); int srf_extr_base(V3 w); int srf_extr_built(void);
int srf_prim_exec(int kind, V3 base, const double p[6], int axis, int level, int color, char *msg, size_t msgn);   /* Simple: Block (0) / Cylinder (1), cui_surf.c */
void sel_clear(void);
int sel_escape_idle(void);                       /* Esc with no function running: the selection cleared (PumpPreSelection 0x802a -> 0x8104) */
/* Home > Attributes / Organize (cui_attr.c) */
extern int cfg_ln_style; extern double cfg_ln_width;   /* config 10000 / 10001 main line style / width (solidColor: scene.c cfg_solid_color) */
void attr_line_style(int v); void attr_line_width(int v); void attr_point_style(int v);   /* v = -1: the main attribute's */
void attr_default_color(int kind, int c);          /* 0 wireframe, 1 surfaces, 3 solids */
int attr_selected(void); int attr_set_level(int level);
int attr_set_all(int use_color, int color, int use_ln, int ln, int use_pt, int pt, int use_level, int level, int use_w, int w);
void attr_new_ent(int i);
void attr_color_cmd(int kind);
extern double constr_depth; extern int zd_pick; int zd_picking(void); void zd_start(void); void zd_set(double z); void zd_click(double x, double y, double z);   /* Z depth */
void constr_toggle_3d(void);
enum { SR_NONE, SR_LEVEL, SR_ATTRIBS };
extern int selrun_kind;                           /* a Home command waits for its selection (Enter runs it, Esc cancels) */
void selrun_start(int kind); void selrun_end(int ok);
int attr_change_level(int copy, int level);       /* Change Levels' OK: Move (0) / Copy (1) the selection to LEVEL */
int ent_duplicate(int i);
int cui_attr_script(const char *st, const char *arg);   /* lnstyle / ptstyle / lnwidth / defcolor / chlevel / setall / zdepth ... */                         /* a copy in place (the clipboard's rules); -1 = a type not copied */                      /* Home > 2D / 3D (cfg_constr_3d)  */                     /* the Colors dialog on a default colour (0 wireframe, 1 surfaces, 3 solids) */
void attr_undo_apply(void *p, int undo); void attr_undo_free(void *p);
void sel_toggle(int e);                          /* a pick on an entity in general selection */
void sel_all(void);                              /* SelectAllMask */
void sel_set_list(const char *list);             /* "3,4,5" (test scripts: sel:) */
int ent_users(int i, char *out, size_t n);
void delete_selected_ents(void);
void hide_toggle(void);                          /* OnHide (Alt+E): show only the selected entities / all again */
int seg_hits_rect(float ax, float ay, float bx, float by, float x0, float y0, float x1, float y1);
char *window_select(float x0, float y0, float x1, float y1, int mode);
char *polygon_select(int mode, const float *px, const float *py, int n);
int op_selected(int i);
int op_nselected(void);
void op_click(int i, int ctrl, int shift);
/* the auto-highlight rule (autohl_active): the front end tells which prompt is up */
typedef struct {
    int blocked;          /* a modal dialog / the backstage / the Tool Manager is up */
    int point_pick;       /* a point pick is running (Analyze AutoCursor, Linking page, Alt+F12, Extrude point) */
    int entity_pick;      /* a dialog's entity pick (colour / level Select, Analyze) */
    int ex_mode, ex_phase;
    int cam_chaining;     /* toolpath chaining / drill points */
    int geo_cmd, geo_need;   /* a wireframe command and its next pick ('p' / 'e' / 'c') */
} CuiHlState;
int cui_autohl_active(const CuiHlState *s);
int cui_hover_update(float mx, float my, int active);   /* 1 when hover_ent changed */
/* the status-bar cursor: the world point under (mx, my) on the grid plane, grid-snapped when Snap to Grid is on
   (Near: within half the pick distance); raw = before the snap. Returns 1 when snapped. */
int cui_cursor_world(float mx, float my, double world[3], double raw[3]);
/* notes (E_NOTE) in the general selection (cui_note.c): the note under the cursor (select_ent_univ's N_ID / W_ID distance, *DPX in px), -1
   none; the strokes of note entity E as world segment pairs (count, *OUT malloc'd) for the selection / highlight draw */
void note_pick_config_changed(void);   /* the configuration was read again: note_pick reads config 15007 anew */
int note_pick(float mx, float my, double *dpx, double wpt[3]);   /* WPT: the note's anchor (+0x70, world) for the shaded-surface depth test */
int note_segs(int e, float **out);
/* middle-button rotation: pivot pick + markers */
extern int piv_on, piv_axis_on, piv_axm; extern float piv_sx, piv_sy;
extern const char *PIV_BM0[13]; extern const char *PIV_BM1[27];
void cui_rotate_begin(float mx, float my, double pivot[3], int dyn_free);
/* one drag step of the free rotation with axis mode axm (0 none, 1 Ctrl, 2 X, 3 Y, 4 Z); lx / ly_up = the cursor in
   view pixels from the bottom-left, dx / dy_up the motion */
void cui_rotate_free_step(int axm, int lx, int ly_up, int dx, int dy_up, const double pivot[3]);
/* keymap: CONFIG/mastercam.kmp (KEYMAP Mastercam <function> "<keys>"), else Mastercam 2017's shipped defaults */
typedef struct { char fn[48], keys[32]; } CuiKey;
extern CuiKey cui_kmap[128]; extern int cui_nkmap; extern char kmap_src[600];
void cui_kmap_load(void);
void cui_kmap_load_defaults(void);   /* Mastercam 2017's shipped mastercam.kmp */
/* graphics right-click menu: the "Right mouse button menu" dropdown of CONFIG/default.mtb */
typedef struct { int kind; char fn[64]; int sub; } RmbItem;   /* kind 0 function, 1 separator, 2 submenu (dropdown sub) */
typedef struct { char name[64]; int idx; RmbItem it[32]; int n; } RmbDrop;
extern RmbDrop rmb_drop[8]; extern int nrmb_drop, rmb_main;
void rmb_load(void);
int rmb_find(int idx);

/* ------------------------------------------------------------------------------------------------ AutoCursor (cui_snap.c) */
enum { ACS_ORIGIN, ACS_CENTER, ACS_END, ACS_MID, ACS_POINT, ACS_QUAD, ACS_INT, ACS_TAN, ACS_PERP, ACS_NEAR, ACS_HV, ACS_ANG, ACS_TMID, ACS_N };
extern int ac_snap[ACS_N];
extern double ac_ang_step;
extern int geo_snap_kind;
extern int geo_snap_feat_ent, geo_snap_feat;     /* the last geo_snap's feature-pass winner when it won: entity, candidate (line 0 start / 1 end), else -1 */
extern V3 ac_tmid[2]; extern int ac_tmid_ok[2];
enum { GK_TAN, GK_POINT, GK_END, GK_MID, GK_QUAD, GK_CENTER, GK_INT, GK_ORIGIN, GK_TCTR, GK_TMID, GK_PERP, GK_GRID, GK_HV, GK_NEAR, GK_NONE = 99 };
#define RB_TPN(rb) ((rb) == 0 || (rb) == 17 || (rb) == 35)          /* mask 0x800020001 (dlsearch_tpn:125-126, FUN_180131620:774-776) */
#define RB_HV(rb)  ((rb) == 0 || (rb) == 17 || (rb) == 32 || (rb) == 35)   /* mask 0x900020001 (mccore_autocursor2.c:561-562) */
P2 xy(V3 p);
GE ent_ge(int i);
double angfold_pi(double a);
int geo_snap(V3 *p, const V3 *anchor, int rb, int use_grid);
void geo_snap_ptr(float mx, float my);           /* geo_snap searches around this mouse position (window coords) until _off */
void geo_snap_ptr_off(void);
int geo_cursor_on_plane(const V3 *p);           /* the click's plane point is the cursor (not a view along the plane) */
extern float cui_ptr_x, cui_ptr_y; extern int cui_ptr_ok; extern int cui_view_drag;
int cui_point_pick_active(void);                 /* a point pick outside the wireframe commands waits (live AutoCursor) */
void pp_hover(const double world[3]);
void pp_ov(float **pv, int *pn, int *pcap);
void pp_dump(void);
void snap_bench(int n);                         /* ours (measurements) */
int geo_ang_snap(V3 *p, const V3 *anchor, int rb, int ctrl_down);
void ac_tmid_clear(void);
void ac_tmid_capture(const V3 *cursor);
/* AutoCursor on solids: the bodies shown (bodies_show) and their edges (solid25.c body_brep_edges) for the solid
   passes of geo_snap (formats/ui.md "AutoCursor on solids"); NULL / 0 clears */
void ac_solids_set(const Body *bs, int nb);
int ac_solid_edges(int body, const BEdge **e);

/* ------------------------------------------------------------------------------------------------ overlay (cui_overlay.c) */
extern int overlay_dirty;
void ov_pt(float **pv, int *pn, int *pcap, V3 p, float r, float g, float b);
unsigned short ln_stipple(int style);
int stipple_px(void);
void ov_entity_pat(float **pv, int *pn, int *pcap, const Ent *en, unsigned short pat, const float rgb[3]);
void ov_entity(float **pv, int *pn, int *pcap, int e, const float rgb[3]);
void ov_fast_marker(float **pv, int *pn, int *pcap, V3 c);   /* draw_fast_marker (0x180331460) */
void build_overlay(void);

/* ------------------------------------------------------------------------------------------------ wireframe commands (cui_geo.c) */
/* the Wireframe tab's commands (formats/wireframe.md; mcamui.c names): geo_cmd = the running one, its picks so far and
   the panel values (GeoParams gps[geo_cmd], derived fields from geo_panel_sync) */
enum { G_NONE, G_POINT, G_PT_DYN, G_PT_SEG, G_PT_END, G_PT_SMALL, G_BOLT,
       G_LINE, G_LPAR, G_LPERP, G_LCLOSE, G_LBIS, G_LTAN,
       G_CIRCLE, G_ARC3, G_ATAN, G_CEDGE, G_AEND, G_APOLAR, G_APOLEND,
       G_RECT, G_RSHAPE, G_POLY, G_ELLIPSE, G_HELIX, G_SPIRAL, G_BBOX,
       G_TRIM, G_BREAK2, G_JOIN, G_BREAKX, G_BREAKN, G_TRIMMANY, G_BREAKPTS,
       G_FILLET, G_FILLETC, G_CHAMF, G_CHAMFC, G_OFFSET, G_OFFSETC, G_PROJECT, G_CLOSEARC, G_BRKCIRC,
       G_XTRANS, G_XTRANS3D, G_XROT, G_XORIGIN, G_XMIRROR, G_XARRAY, G_XSTRETCH, G_XDIST, G_XDYN, G_XSCALE,   /* Transform tab (cui_xform.c; G_XDYN Dynamic: + cui_xdyn.c) */
       G_XROLL,   /* Transform > Roll: a chain command (cui_xroll.c) */
       G_DIMH, G_DIMV, G_DIMP,                                                                 /* Drafting tab (cui_draft.c) */
       G_ORDH, G_ORDV, G_ORDP, G_ORDE, G_ORDA,                                                               /* Drafting > Ordinate (cui_draft_ord.c) */
       G_DIMC, G_DIMA, G_DIMB, G_DIMCH, G_DIMPT, G_DIMT, G_DIMPP, G_NOTE,                                                                      /* Drafting tab: Circular (cui_draft_dim.c), Angular (cui_draft_ang.c) */
       G_SREV,                                                                                  /* Surfaces > Revolve (cui_surf.c) */
       G_SNSET, G_SUNTRIM, G_SRMBND,                                                                /* Surfaces > Normals > Set, Untrim (cui_srfedit.c) */
       G_SBLOCK, G_SCYL, G_SSPH, G_SCONE, G_STOR, G_SFLAT, G_SSOLID, G_SLOFT, G_CCONST, G_CFLOW, G_CDYN, G_CEDGE1, G_CEDGEA, G_CSLICE, G_LNORM, G_CINT, G_CSURF,                                                                     /* Surfaces > Simple > Block / Cylinder (cui_surf.c) */
       G_PTNODES, G_CVIEWS, G_RGROOVE, G_LETTERS,                                                                     /* N9-16 (cui_wfmisc.c): Point Nodes, Combine Views, Relief Groove */
       G_SPLMAN, G_SPLAUTO, G_SPLBLEND, G_SPLCURV, G_X2NURBS, G_UNTRIM, G_SIMPL, G_MODIFYSP, G_REFIT,   /* the spline commands (cui_spline.c, formats/wireframe.md "Wireframe spline commands") */
       G_CPART,   /* Wireframe > Curve Parting Line (cui_partline.c) */
       G_SDRAFT, G_SEXTR,   /* Surfaces > Create > Draft / Extrude (cui_surf.c, cui_geo.c) */
       G_SEXT,    /* Surfaces > Modify > Extend (cui_srftrim.c) */
       G_STRIMS,  /* Surfaces > Trim to Surfaces (cui_srftrim.c) */
       G_SOFFSET, /* Surfaces > Create > Offset (cui_srfedit.c) */
       /* (keep G_NCMD last) */                                                                     /* Surfaces > Simple > Block / Cylinder (cui_surf.c) */
       G_NCMD };
int wm_ptnodes(int e);                           /* cui_wfmisc.c */
void wm_ln_begin(void); void wm_ln_end(void); int wm_ln_active(void); void wm_ln_take(int e, V3 w); void wm_ln_click_pick(float mx, float my, V3 w); void wm_ln_hover(V3 w, int snapped);
void wm_ln_exec(void); void wm_ln_apply(void); void wm_ln_reselect(void); int wm_ln_enable_reselect(void); int wm_ln_enable_apply(void); void wm_ln_ov(float **pv, int *pn, int *pcap);   /* Line Normal (cui_wfmisc.c) */
void geo_add_like(int i, const GE *pieces, int n);   /* cui_geo.c: pieces with entity i's attributes */
void wm_combine_views(void);
int wm_script(const char *st, const char *arg);
void geo_let_chain_changed(void);
void wm_let_chain_exec(void); int wm_let_arcpick(int e, V3 w); int wm_let_wants_arc(void); int wm_let_chain(void); int wm_let_take_cancelled(void); const char *wm_let_prompt(void);
void wm_let_begin(void); void wm_let_exec(V3 w); int wm_let_ported(void); int wm_let_mode(void); void wm_let_gpset(const char *k, double x, const char *sv);
void wm_rg_gpset(const char *k, double x);
void wm_rg_begin(void); int wm_rg_exec(void); void wm_rg_ok(void); void wm_rg_pick(double wx, double wy, double wz); int wm_rg_prevsel(void);
extern int wm_rg_wait; void wm_rg_wait_start(void); void wm_rg_enter(void);   /* Preview: wait_for_space (Enter) */
extern int wm_rg_picking;                        /* Relief Groove's Select button: the next point is the groove position */
enum { TR_AUTO = 0, TR_1, TR_2, TR_3, TR_DIVIDE, TR_POINT, TR_LENGTH };   /* Mastercam's trim mode numbers (FUN_1800ce550) */
/* command options: one set per command (each Mastercam function panel has its own model and persisted keys -
   geopanel_ui.inc); `gp` is the running command's set. The fields the geometry reads (len, dist, r, n, ...) are derived
   from the panel values (v_*) and locks (lk_*) by geo_panel_sync each frame. */
typedef struct {
    double len, ang, dist, r, sa, ea, rot, ea_r, eb_r, pitch, revs, zpitch, z, maxr, d2, corner_r, hole_dia, bolt_r;
    double taper, height, pitch2, zpitch2, ell_tol;         /* Helix taper (HELIX_PARAMS [3]), Height (control 750), Spiral final pitches (SPIRAL_PARAMS [3], [5]), Ellipse tolerance (+0x20) */
    int anchor;                                             /* Rectangular Shapes base point 1..9 (0 = from anchor_ctr, the older scenes' field) */
    int ofc_ila;                                            /* Offset Chains: Infinite Look Ahead (check 1668, DAT_180242a08; default 1) */
    int ell_out;                                            /* Ellipse output type (+0x1c): 0 NURBS, 1 arcs, 2 lines */
    int n, copies, mode, trim, anchor_ctr, flat, horiz, vert, pts, circles, ctr, del, move, shape, round, cw, cont, partial;
    int lk_r, lk_sa, lk_ea;                                   /* Arc Polar locks (panel +0x10 / +0x30 / +0x40) */
    double chw;                                        /* chamfer entities Width (struct +0x20) */
    /* panel model (geopanel_ui.inc): EvaluatorBox values and hard locks, radio groups and check boxes */
    double v_len, v_ang, v_dist, v_r, v_w, v_h, v_tol, v_step;
    int v_n, lk_len, lk_ang, lk_dist, lk_n, lk_w, lk_h;
    int lk_ra, lk_rb;                                  /* Ellipse: the A / B radius locks (CEllipseDlg param +0x18 / +0x08, create_ellipse.c) */
    int edit_pt;                                       /* Line Endpoints CurrentlyEditing (PointToEdit: 0 One, 1 Two, 2 None; manager +0x80,
                                                          FUN_1800bfd20 starts it at 2) */
    int entmode, multi, dir, single, tangent, style, method, action, lines, trimmode, start_pt, inverted, full, refcirc, surf, cmode;
    /* Bounding Box (cui_bbox.c): the panel model = the settings struct (spec bboxpanel 1): sizes +8..+0x28, the read-only
       Volume; AllEntities +0, VM Shape (0 Rectangular / 1 Cylindrical = !+0x30), RectangularOrigin +0x34, CylindricalOrigin
       +0x38 (0 Top, 1 Bottom, 2 Center), the axis as the struct code +0x3c (0 Z, 1 Y, 2 X), CenterOnAxis +0x40, Incremental
       +0x41, BothDirections +0x42, the create flags +1..+4, CreateToolPlane +0x43, SetAsCurrentToolPlane +0x50, SetWCS +0x51 */
    double bb_sx, bb_sy, bb_sz, bb_r, bb_h, bb_vol;
    int bb_all, bb_shape, bb_rorg, bb_corg, bb_axis, bb_center, bb_incr, bb_both, bb_lines, bb_pts, bb_ctr, bb_solids, bb_ctp, bb_tpcur, bb_tpwcs;
    char bb_tpname[64];                          /* Create Tool Plane Name (settings +0x48 CString) */
} GeoParams;
extern GeoParams gps[G_NCMD];
extern int geo_cmd, geo_npick, geo_nent, geo_phase, geo_applied;
extern V3 geo_pick[4], geo_entpt[4]; extern int geo_ent[4];
/* the chain commands (Fillet Chains, Chamfer Chains, Offset Chains): their chain_it runs the Chaining dialog
   (geo_chaining, cui_chain.c chain_begin_geo); its OK hands the chains over (geo_chains_done, called by the UI's
   cui_hooks.chain_ok while geo_chaining); the command then works on every chain of geo_chains */
extern int geo_chaining, geo_nchains; extern Chain geo_chains[256];
int geo_chain_cmd(int c);
void geo_chains_done(void);
int geo_panel_enabled(void);                     /* the panel takes input (EnableWindow(0) while chain_it runs) */
int geo_panel_apply_enabled(void);               /* the Apply button (the managers' EnableApply) */
extern int geo_panel_req;                        /* a panel code posted by a body button (4 = Reselect), -1 = none */
extern int geo_live, *geo_live_l, geo_live_n;    /* the live result (sketcher live colour) and its entities */
extern double geo_dia, geo_rw, geo_rh, geo_poly_r; extern int geo_poly_n;
extern int geo_level, geo_color;                 /* the main attributes new entities get */
extern V3 geo_cursor; extern int geo_cursor_ok, geo_snapped, geo_snap_ent;   /* the drawing cursor (snapped) */
const char *geo_name(int c);
int geo_find(const char *name);                  /* the command of a ribbon label (0 = not ported: greyed) */
int geo_start(const char *name);
const char *geo_steps(void);                     /* the picks it needs: p point, e entity, c chain; "" = on the selection */
const char *geo_prompt(void);
int geo_need(void);                              /* the next pick ('p' / 'e' / 'c'), 0 = none */
int geo_rb_type(void);
void geo_take(int e, V3 w);                      /* a pick: entity e (or -1) at w */
void geo_point(V3 p);
void geo_reset_picks(void);
void geo_add(int type, V3 a, V3 b, V3 c, double r, double a0, double sw);
void geo_line(V3 a, V3 b);
void geo_run(void);
void geo_exec(void);
void geo_apply(void);                            /* the selection commands' Apply */
void geo_end(void);
void geo_cancel(void);                           /* Esc / Cancel */
extern int geo_panel_layout;                     /* bumped when a command's panel rows change (the front end rebuilds the panel) */
void geo_escape(void);                           /* the Esc key: geo_cancel, a Transform dialog xf_escape */
void geo_live_commit(void);
void geo_live_drop(void);
void geo_live_update(void);                      /* a panel value changed: rebuild the live result */
/* the lock fields lk_*: bit GEO_LK_HARD = the EvaluatorBox's lock button (IsLocked = the model's IsHardLocked), bit GEO_LK_SOFT =
   its soft lock (IsSoftLocked: cncsoftware.controls.dll ValueLockBehavior.OnGotKeyboardFocus sets it when a HasLock box gets
   the keyboard focus, evaluatorbox.baml Setter_20 / _21). The commands' panel readers take FunctionPanelEditableDouble::
   GetLocked(false) = hard || soft (mastercam.dll @180191fd0; e.g. sketcher_line_1.c FUN_1800c3e10 :4545), so a nonzero lk_*
   is "locked"; geo_soft_lock_cmd: the commands whose soft-lock clearing is ported (the others ignore the focus) */
enum { GEO_LK_HARD = 1, GEO_LK_SOFT = 2 };
int geo_soft_lock_cmd(int c);
int geo_line_rb_seg(V3 c, V3 *a, V3 *b);         /* the Line Endpoints rubber band at cursor c (the overlay, tests) */
void geo_lock_button(int *lk);                   /* a lock button clicked (lk's hard bit toggled): the dialog's own handling */
/* panel model: geo_panel_sync derives the geometry's values from the panel values / locks; pk_note stores the
   changed persisted keys (<config dir>/panel_keys.tsv); geo_panel_button: 0 OK, 1 OK-New (Apply), 2 Cancel */
void geo_panel_sync(void);
void pk_note(int c);
void geo_panel_button(int code);
/* the panel's contents (formats/ui_wireframe_panels.txt) walked through the UI's builder, in order; texts keep the
   WPF access-key underscores. exp: an AdvancedExpander, returns 1 when open (its controls follow, then end); radio /
   check / num / inum edit *var when the user changes them (en = enabled); lk = the EvaluatorBox hard lock (NULL: none);
   toggle = n Reselect / point ToggleButtons: code >= 0 = a live button that posts that panel code when clicked (the
   chain commands' Reselect, 4), -1 = not ported (greyed); combo: items, *ix, the item index that is not ported */
typedef struct {
    int (*exp)(void *u, const char *hdr);
    void (*end)(void *u);
    void (*label)(void *u, const char *t);
    int (*radio)(void *u, const char *t, int *var, int val, int en);
    void (*check)(void *u, const char *t, int *var, int en);
    void (*toggle)(void *u, const char *t, int n, int code);
    void (*num)(void *u, const char *label, const char *id, double *v, int *lk, int en, double lo, double hi);
    void (*inum)(void *u, const char *label, const char *id, int *v, int *lk, int en, int lo, int hi);
    void (*combo)(void *u, const char **items, int n, int *ix, int greyed);
    /* tab: the start of tab ix (geo_panel_tabs order) - 1 = walk its contents (mcamui: the shown tab only; Qt: every tab,
       each into its own page). NULL = no tabs (everything is walked) */
    int (*tab)(void *u, int ix, const char *name);
    /* text: a TextBox editing buf (n bytes incl. the NUL), en = enabled. NULL = not provided (left out) */
    void (*text)(void *u, const char *label, const char *id, char *buf, int n, int en);
    /* tbtn: n ToggleButtons in a row bound to *var (labels t separated by '|'): button i is checked when *var == i; a click sets
       *var = (*var == i ? none : i) (WireframeLineEndpointsManagerViewModel.OnSetCurrentlyEditingCommand, mccad.interop.dll);
       button i enabled when en bit i is set. NULL = not provided */
    void (*tbtn)(void *u, const char *t, int n, int *var, int none, int en);
} CuiPanelOps;
void geo_panel_body(const CuiPanelOps *ops, void *u);
void wm_rg_panel(const CuiPanelOps *ops, void *u);       /* cui_wfmisc.c */
void wm_let_panel(const CuiPanelOps *ops, void *u);
const char *const *geo_panel_tabs(int *n);       /* the running panel's tabs ("Basic"; Bounding Box: "Basic", "Advanced") */
/* ---- the Bounding Box function panel (cui_bbox.c; mccad create_bbox / create_boundingshape_either_method -> FUN_180343ae0) */
void bb_begin(int stock);                        /* geo_start of "Bounding Box" (stock = the Stock Setup's Bounding box button) */
void bb_stock_run(void);                         /* Stock Setup 25451: the sheet hidden (st_pick = ST_PICK_BBOX), the panel in stock mode */
void bb_abort(void);                             /* the command ends without OK (another command, geo_end): Cancel */
void bb_poll(void);                              /* a panel value changed (geo_live_update): the posted panel code's handling */
int bb_button(int code);                         /* panel codes 0 OK, 1 Apply, 2 Cancel, 7 Select; 1 = handled */
int bb_selecting(void);                          /* the Manual selection (select_ents) is running: Enter ends it (bb_enter) */
void bb_enter(void);
int bb_on(void);
int bb_apply_enabled(void);
void bb_panel_body(const CuiPanelOps *ops, void *u);
/* ---- the Transform tab (cui_xform.c; MCCADUTIL do_xform_* over mccore Xf*, formats/wireframe.md "Transform") */
/* ---- the Drafting tab: Horizontal / Vertical / Parallel linear dimensions (cui_draft.c; formats/drafting/create.md) */
int dr_cmd(int c);                                /* c is a drafting command (G_DIMH .. G_DIMP) */
const char *dr_label(int c);                      /* its ribbon label */
int dr_find_ft(const char *ft);                   /* the command of a ribbon FT function (doDraft_DIMENSION_*), 0 = not ported */
int dr_begin(int c);                              /* geo_start: 0 = refused (no stroke fonts) */
void dr_end(void);                                /* geo_end: the live entity committed */
int dr_need(void);                                /* 'e' first pick (point or line), 'p' second point, 't' text placement */
int dr_click(float mx, float my, const double w[3], int ctrl);
int dr_move(const double world[3], int ctrl);     /* a mouse move (placement): 1 = taken */
int dr_esc(void);                                 /* Esc: 1 = a dimension in progress was cancelled (the command goes on) */
int dr_button(int code);                          /* panel code: 1 = handled (OK-New), 0 = the command ends */
const char *dr_prompt_text(void);
int dr_live_on(void);
void dr_scene(void);                              /* the session's drafting entities to the scene (scene_draft_session) */
/* ---- the Drafting tab's Ordinate group: Horizontal / Vertical / Parallel ordinate dimensions (cui_draft_ord.c; formats/drafting/create.md 8);
   the same roles as the dr_* calls above */
void cui_spldump(void);                         /* tests: the spldump hook (cui_spline.c) */
int si_cmd(int c); int si_begin(int c); void si_end(void); void si_script(const char *arg); void si_take(int e, V3 p); int si_need(void); const char *si_prompt_text(void); int si_click(float mx, float my, const double w[3], int ctrl); int si_esc(void); void si_panel_sync(void); int si_button(int code); void si_panel_body(const CuiPanelOps *ops, void *u); void si_enter_key(void);   /* Curve at Intersection (cui_srfint.c) */
int pln_cmd(int c); int pln_begin(int c); void pln_end(void); void pln_script(const char *arg); void pln_take(int e, V3 p); int pln_need(void); const char *pln_prompt_text(void); int pln_click(float mx, float my, const double w[3], int ctrl); int pln_esc(void); void pln_panel_sync(void); int pln_button(int code); void pln_panel_body(const CuiPanelOps *ops, void *u); void pln_enter_key(void);   /* Curve Parting Line (cui_partline.c) */
int sl_cmd(int c); int sl_begin(int c); void sl_end(void); void sl_script(const char *arg); void sl_take(int e, V3 p); int sl_need(void); const char *sl_prompt_text(void); int sl_click(float mx, float my, const double w[3], int ctrl); int sl_esc(void); void sl_enter_key(void); int sl_button(int code); void sl_panel_sync(void); void sl_panel_body(const CuiPanelOps *ops, void *u);   /* Curve Slice (cui_slice.c) */
/* dyno_surf (cui_dsurf.c, mccore 0x1803210c0) and Curve Dynamic (sketcher_curve_4) */
int ds_running(void); int ds_autocursor(void); void ds_start(int ent, int mode, int p4, double u, double v); int ds_event(int code, const double uu[2], const double *snap); void ds_uu(const double raw[3], double uu[2]); void ds_overlay(float **pv, int *pn, int *pcap); int ds_uv(double *u, double *v); int ds_last_aborted(void); int ds_debug_poly(void); int ds_point_count(void);
int srf_crv_edge_supported(int e, char *msg, size_t msgn); int srf_crv_edge_one(int e, double u, double v, double ang, int level, int color, char *msg, size_t msgn); int srf_crv_edge_all(int e, double ang, int level, int color, char *msg, size_t msgn);
int srf_crv_edge_begin(int e, V3 w, char *msg, size_t msgn); int srf_crv_const_begin(int e, V3 w, char *msg, size_t msgn); int cdyn_take(int e, V3 w); void cdyn_collect(void); int cdyn_build(void); int cdyn_points(void);
int srf_ext_supported(int e, char *msg, size_t msgn); int srf_ext_begin(int e, V3 w, char *msg, size_t msgn); void srf_ext_edge(int e, double u, double v);   /* Surfaces > Extend (cui_srftrim.c) */
int srf_ext_exec(int e, double len, int to_plane, int linear, int keep, int level, int color, char *msg, size_t msgn);   /* -1: To plane, the Plane Selection dialog opened (the build follows from its callback) */
void srf_ext_reset(void); void srf_ext_plane_forget(void); void geo_sext_plane_done(int e);   /* Extend To plane (cui_srftrim.c / cui_geo.c) */
/* Surfaces > Trim to Surfaces (trim_surface_1, cui_srftrim.c G_STRIMS) */
void srf_trims_begin(void); int srf_trims_phase(void); int srf_trims_ready(int option); const char *srf_trims_prompt(int option);
int srf_trims_enter(char *msg, size_t msgn); int srf_trims_click(int e, V3 w, int option, char *msg, size_t msgn); void srf_trims_keep(int e, double u, double v);
void srf_trims_restart(void);
int srf_trims_exec(int option, int ext, int split, int keepm, int usecur, int keep, int level, int color, char *msg, size_t msgn);
int srf_trimp_exec(const int *ents, int n, const double org[3], const double nrm[3], int delun, int split, int keepm, int usecur, int keep, int level, int color, char *msg, size_t msgn);   /* Trim to Plane's build (the command: greyed, get_plane_info) */
extern int srf_front_color;                     /* cui_surf.c: surfaceFrontColor */
void geo_ce_enter(void);   /* Curve All Edges: the Enter of its selection / options */
void geo_ds_key(int code);   /* Enter (0xb) / the Esc path of a running dyno_surf: cui_geo.c */
int sp_cmd(int c);                                /* the spline family (cui_spline.c): G_SPLMAN .. G_REFIT, G_CSURF */
void csf_run(void);                               /* Curve Surface (cui_csurf.c): the selected curves made surface curves */
int sp_begin(int c); void sp_end(void); void sp_curv_reset(void); void sp_curv_run(void); void sp_curv_panel(const CuiPanelOps *ops, void *u); void sp_move(void); void sp_script(const char *arg); void sp_take(int e, V3 p); int sp_need(void); const char *sp_prompt_text(void); int sp_click(float mx, float my, const double w[3], int ctrl); int sp_esc(void); int sp_button(int code); void sp_enter_key(void); void sp_panel_body(const CuiPanelOps *ops, void *u); void sp_panel_sync(void); void sp_overlay(float **pv, int *pn, int *pcap);
int do_cmd(int c);
const char *do_label(int c);
int do_find_ft(const char *ft);
int do_begin(int c);
void do_end(void);
int do_need(void);
int do_click(float mx, float my, const double w[3], int ctrl);
int do_move(const double world[3], int ctrl);
int do_esc(void);
int do_button(int code);
const char *do_prompt_text(void);
void do_panel_body(const CuiPanelOps *ops, void *u);
const char *const *do_panel_tabs(int *n);
void do_panel_sync(void);
int do_script(const char *st, const char *arg);
/* ---- the pick of an existing drafting entity (cui_draft_pick.c over viewer/draft_pick.c: select_ent_univ's drafting cases,
   formats/drafting/create.md 9) - shared by the Drafting commands */
typedef struct { const unsigned char *E; int file; int dnew; double dist; } CuiDraftHit;   /* the record; the part's drafting index or -1;
                                                                                    the session's McamFile.dnew index or -1 */
int cui_draft_pick(const double w[3], int mask, CuiDraftHit *hit);   /* W: the mouse's world point on the construction plane; MASK 0x40
                                                                        dimensions / 0x10 notes / 0x20 witness-leaders; 1 = picked */
const unsigned char *cui_draft_mod(const unsigned char *fileE);
struct DraftDoc; const struct DraftDoc *cui_draft_doc(void);
void cui_draft_edit_begin(void);                  /* one undo step of rewritten drafting records (ours; cui_draft_pick.c) */
int cui_draft_edit_rec(const CuiDraftHit *src, const unsigned char newE[0x810]);   /* SRC rewritten as NEWE: its dnew index */
void cui_draft_edit_commit(void);
void dr_edit_undo(int tx, int undo);
int cui_draft_is_repl(int k);                     /* McamFile.dnew[k] replaces a part record (its eptr at +0x20) */
int cui_draft_hide(const unsigned long long *eptrs, int n);    /* part records not drawn (a drag draws their copies); 1 = changed */   /* the open part's drafting records (viewer/draft.h), read once per part */   /* a part record edited here: its edited bytes (NULL: not edited) */
void dr_panel_body(const CuiPanelOps *ops, void *u);
const char *const *dr_panel_tabs(int *n);
void dr_panel_sync(void);                         /* the panel's values -> the drafting globals (a change recomputes) */
void dr_undo(int idx, int undo);
void dr_write(FILE *f);                           /* the project file's DIM lines (ours) */
int dr_read_line(const char *ln);                 /* 1 = a DIM line */
/* ---- Drafting > Note (cui_draft_note.c; formats/drafting/notes.md 5): run through the dr_* entry points (geo_cmd G_NOTE) */
int dn_cmd(int c);
int dn_find_ft(const char *ft);
int dn_begin(void);
void dn_end(void);
int dn_move(const double world[3], int ctrl);
int dn_click(float mx, float my, const double w[3], int ctrl);
int dn_need(void);
int dn_esc(void);
int dn_button(int code);
void dn_panel_body(const CuiPanelOps *ops, void *u);
int dn_script(const char *st, const char *arg);
void dn_write_ent(FILE *f, int e);                /* the project file's NOTE line of note entity E made here (ours) */
int dn_read_line(const char *ln);                 /* 1 = a NOTE line */
int dn_dlg_radio_index(unsigned id);              /* the radio index 0..7 of a Create radio, -1 */
int dn_dlg_ported(unsigned id);                   /* the control works here (else greyed: NOT PORTED) */
int dn_dlg_enabled(unsigned id, int radio, int text_empty);   /* CNoteDlg's enable rule while creating */
extern char dn_last_text[10000]; extern int dn_last_radio, stay_in_create_note;
extern int cui_script_note_ok, cui_script_note_radio; extern char cui_script_note_text[4096];   /* tests: the scripted dialog answer */
void dr_dump(void);
int dr_script(const char *st, const char *arg);   /* dimdrag / dimdump / dimfile / dimpanel (tests) */
int xf_cmd(int c);                               /* c is a Transform command (G_XTRANS .. G_XSCALE, G_XDYN among them) */
int xf_on(void);
int xf_begin(int c);                             /* geo_start of a Transform command: select_ents (or the preselection) */
void xf_abort(void);                             /* the command left without OK: as Cancel */
int xf_selecting(void);                          /* select_ents runs: Enter (xf_enter) ends it */
void xf_enter(void);
int xf_enter_ok(void);                           /* Enter goes to xf_enter: select_ents, or Remove Item's copy pick (mccoreres 1534) */
void xf_escape(void);                            /* Esc in a Transform dialog: OK while dlg +0x5090 is set, else Cancel (cui_xform.c) */
int xf_need(void);                               /* a pick a dialog button asked for ('p' / 'e'), Stretch's window corners */
void xf_take(int e, V3 w);
const char *xf_prompt(void);
int xf_button(int code);                         /* panel codes 0 OK, 1 Apply, 2 Cancel, >= 10 the dialog's buttons */
void xf_poll(void);                              /* a panel value changed: the dialog's edit rules, then the preview (0x66) */
int xf_apply_enabled(void);
int xf_panel_enabled(void);
void xf_panel_body(const CuiPanelOps *ops, void *u);
/* ---- Transform > Dynamic (DynamicXform, formats/wireframe.md "Dynamic - port spec"): the round / data side in cui_xform.c, the
   gnomon widget in cui_xdyn.c. Front ends: route the view's left press / moves / release here first while xdyn_on() */
int xdyn_on(void);                               /* the gnomon is up (placement / idle / a drag) */
int xdyn_press(float mx, float my, int dbl);     /* a left press: 1 = taken (a handle drag, the placement, a Multiple drop) */
int xdyn_move(float mx, float my);               /* hover / drag: 1 = redraw */
int xdyn_release(float mx, float my);            /* 1 = taken (no click follows) */
int xdyn_rclick(void);                           /* a right click: Esc (get_event 1) - 1 = taken */
int xdyn_ctrl(void);                             /* Ctrl down (key event 8 / 0x11, FUN_1800eb0a0): gnomon <-> geometry - 1 = taken */
int xdyn_enter(void);                            /* Enter: panel code 1 (FUN_1800e58f0) - 1 = taken */
extern int xdy_ctrl_held;                        /* the UI: Ctrl is down at a panel Apply (GetAsyncKeyState(0x11), Ctrl+Apply) */
typedef struct { double m[9], t[3]; } GnMat;     /* a gnomon frame: p' = m p + t, columns = its axes (cui_xform.c XM layout) */
typedef struct { float x, y, px; unsigned char rgba[4]; int bold, box; char s[64]; } GnText;   /* a label / the read-out (window coords) */
extern GnMat gn_C; extern int gn_on, gn_hover, gn_drag, gn_snapped3d;
extern int gn_size, gn_ang_on, gn_ruler_on, gn_ruler_size, gn_selmask; extern double gn_ang_inc;
int gn_pick(float mx, float my);                 /* GrGnomon::Select: the SelItem under the cursor, 0 none */
void gn_set_hover(int id);
int gn_edit_mode(int id);                        /* the default edit map (ResetEditMap) */
int gn_edit_kind(int mode);                      /* 1 origin, 2 translate, 3 axis (align / face), 4 rotate */
void gn_begin(int id, float mx, float my);       /* a drag of item id starts (E := C) */
int gn_move(float mx, float my);
void gn_apply(void);                             /* ApplyDrag: C := the drag frame */
void gn_cancel(void);
void gn_set_value(int id, const double *v, int nv);   /* tests: a drag of id to a value (distance / angle / origin x y z / align line e [x y z]) */
GnMat gn_frame(void);                            /* the drag frame while dragging, else C */
void gn_show(int on); void gn_reset(void);
double gn_scale(void);                           /* mesh -> model units */
double gn_value(int *kind);
void gn_upload(void);                            /* the gnomon's triangles / lines to the scene (scene_set_gnomon) */
int gn_texts(GnText *t, int max);                /* the X / Y / Z labels and the drag read-out */
/* ---- Transform > Roll (cui_xroll.c; do_xform_roll, formats/wireframe.md "Roll - port spec"): a wireframe chain command (G_XROLL) */
typedef struct { int mode, roll, axisx, cw; double dia, btol, ang; int twopt; double p1[3], p2[3]; int type, useattr, level, color; } XRoll;
extern XRoll xrl;
extern int xrl_pick, xrl_pts_ok;                 /* Roll's 829 "Select two points" pick (1 / 2 = the point asked), dlg +0x10500 (both picked once) */
void xroll_reset(void);                           /* the per-run reset (MC l.642-653) */
int xroll_run(const Chain *ch, int nch, int main_lv, int main_co, char *msg, size_t msgn);   /* the results (inside mod_begin / mod_end) */
void xroll_panel(const CuiPanelOps *ops, void *u);
void xf_ov(float **pv, int *pn, int *pcap);      /* the group / result colours (config 5010 / 5031 while 5044 is on) */
int xf_ent_mark(int e);                          /* 2 group, 8 result */
/* the views (cui_xform.c): rows of view V (the reserved 1..8, the part's own), n_views, views_parallel (mastercam.dll 0x1803418c0) */
int mc_view_rows(int v, double R[9]); int mc_view_count(void); int mc_views_parallel(int a, int b, int *anti);
extern int pick_req_bits;                       /* get_event's sel-bit argument: picks / windows take only entities with these marks (cui_sel.c) */
extern int gs_containment; int get_window_select_mode(void);   /* GetWindowSelectMode (cui_sel.c) */
void xf_mark_ent(int e, int bit);                /* set the group (2) / result (8) mark, the other one off */
extern int cfg_group_color, cfg_result_color;    /* config 5010 / 5031 (cui_xform.c) */
void xf_clear_colors(void);                      /* ClearColors (mastercam.dll): the marks and the selection off */
void xf_reset_doc(void);                         /* a new / opened part: no marks */
void xf_config_item(int key, const char *q);
void xf_dump(void);
int xf_script(const char *st, const char *arg);
int xf_scale_all(double s, int *skipped);        /* fmu_scale_db: every point / line / arc scaled about the origin (units change); returns the count */
/* units (formats/units.md): the System Configuration's current configuration mcamx.config (English) / mcamxm.config (Metric) */
int cui_units_change(int metric);                 /* the Configuration's OK with the other units: asks to scale a part with entities, then cui_units_set */
int cui_units_set(int metric, int scale);         /* 1 = changed; scale: answer to "Scale current part to Metric / English?" */
const char *cui_units_config(int metric);        /* "mcamxm.config (Metric)" / "mcamx.config (English)" (the Current: combo's text) */
void units_dump(void);
void ex_units(void);                              /* the Extrude session values converted to the system units (RegisterVariable, cui_extrude.c) */                            /* tests: the units state (MCAMUI_UNITSDUMP / unitsdump step) */
void bb_ov(float **pv, int *pn, int *pcap);      /* the preview (ours: the Lines and arcs outline instead of the live solid) */
void bb_dump(void);
/* push pull (cui_bbox.c): the face pick through geo_click, the arrow drag from the views' mouse handlers (as ex_drag_*) */
int bb_face_pickable(void);
int bb_click(float mx, float my);
int bb_arrow_hit(float mx, float my);              /* 1 = the press is on the push-pull arrow (then bb_drag_start) */
int bb_dragging(void);
void bb_drag_start(void);
void bb_drag_move(float mx, float my);
void bb_drag_value(double value);
void bb_drag_end(int cancel);                      /* 0 = the release (commit), 1 = Esc (code 6) */
int bb_script(const char *st, const char *arg);
/* view glue */
int geo_ov_picked(int e);                        /* build_overlay: the command's picked entities */
void geo_ov_extra(float **pv, int *pn, int *pcap);   /* the drawing preview, rubber band, snap marker */
int geo_click(float mx, float my, const double w[3], int ctrl);   /* 1 = the click was the command's pick */
void geo_cursor_update(const double raw[3], int ctrl, int mouse_snapped);
int geo_tmid_key(void);                          /* the N key (Temporary Midpoints capture) */
void set_main_from_entity(void);                 /* mini toolbar Set From Entity */
int cui_geo_script(const char *st, const char *arg);   /* the MCAMUI_SCRIPT geo steps; 1 = handled */

/* ------------------------------------------------------------------------------------------------ chaining (cui_chain.c) */
extern int cam_mode;                             /* CAM_* while selecting chains / points (toolpath chaining), 0 otherwise */
extern Chain cchains[256]; extern int ncchains;  /* the chains picked so far */
extern Hole choles[2048]; extern int ncholes;    /* the drill points picked so far */
extern int show_params;                          /* the op dialog is up (phase 6) */
extern int cam_variant;                          /* contour type from the gallery: 1 chamfer, 2 engrave */
extern int ex_mode, ex_phase;                    /* the Extrude command / its panel (phase 5) */
extern int chain_single, chain_win_new, partial_e1; extern double partial_px, partial_py;
extern int win_sel_mode, cfg_chain_zigzag, cfg_chain_dir, cfg_chain_open, cfg_chain_closed, cfg_win_prompt, cfg_chain_infinite;
extern int win_pending; extern char *win_sel;
extern int cfg_chain_cursor, show_chain_opts; extern int cfg_chain_mode;
#define cfg_sys_tol (mc_systol.tol)               /* 1000 System tolerance (tol: McSysTol) */
extern int dyn_state, dyn_end, chain_wait, partial_waiting;
extern Chain last_chains[256]; extern int nlast_chains, last_used;
extern int chain_dlg_exp, chain_last_mode, cfg_chain_last_mode, cfg_chain_plane_def, cfg_chain_wait_def, win_group_n;
extern int cfg_ent_mask, cfg_break_thread, cfg_surf_single, cfg_dyn_break, cfg_sect_on, cfg_sync_mode, cfg_rev_inner, cfg_sort_mode;
extern double cfg_sect_ang;
extern char chain_prompt[160];
extern int poly_n; extern float poly_x[256], poly_y[256];   /* Polygon / Vector sketch (window coordinates) */
extern int last_clicks;                          /* the click count of the last left click (2 = double-click ends a polygon) */
void cam_clear_selection(void);
void chain_prompt_upd(void);
void chain_dlg_init(void);
void chain_dlg_done(void);
int gallery_pick(const char *name);              /* a toolpath gallery item starts its chaining (0 = not generated here) */
void chain_begin_extrude(void);                  /* the Extrude command's chaining (mcamui ex_chaining's chain part) */
void chain_begin_geo(const char *prompt);        /* a wireframe chain command's chain_it (geo_chaining) */
int chain_pick_at(int e, double wx, double wy);  /* run_chain_dlg's pick on entity e at world (wx, wy) in the current mode */
int cui_spline_chaining(void);                     /* chaining for an Engrave op: spline curves (E_SPLINE) pickable / chainable */
void chain_reverse(Chain *c);                    /* reverse_chain */
int chain_dlg_shown(void);                       /* the Chaining / Drill Point Selection dialog is up */
/* S4: a new 3D surface op is made as mccore / mcmsurf msurf_assoc makes it (cui_chain.c; formats/surface_selection.md 1.2-1.6):
   [Boss/Cavity dialog 100] -> drive / check selection (FUN_18001bcf0, configuration 17019) -> containment chaining (17018) ->
   [Toolpath/surface selection dialog: stage S5] -> the op dialog -> the picks that follow it (start / radial point, plunge corners,
   blend / project chains) -> generation, or message 241, or the 427 "keep this operation?" question */
extern int cfg_ms_all_geom, cfg_ms_toolcont, cfg_ms_mach_stl, cfg_ms_show_dlg;   /* config 17019 / 17018 / 17017 / 17023 */
enum { MS_OFF, MS_SEL, MS_CONT, MS_DLG, MS_POST, MS_POSTCH, MS_GEN };
extern int ms_phase;                             /* MS_*: where the flow of the new op is (MS_OFF = none) */
extern int drv_pick;                             /* the select_ents prompt runs: 1 drive (406), 2 check (407) */
extern int ms_sld_body, ms_sld_faces;            /* the Selection Bar's Select body / Select face buttons (OnSolidsSelectBody / Face: the
                                                    solid pick mask's bits 1 / 4); at the flow's start Body on, Face off (N13-8) */
int ms_selbar_enabled(void);                     /* the buttons are enabled (the drive / check prompt runs) */
void ms_selbar_toggle(int face);                 /* a click on Select body (0) / Select face (1) */
int ms_gen_take(CamOp *job);                     /* N13-9: the flow's generation waits for the front end (cui_hooks.gen_queued): 1 = JOB
                                                    taken - compute it (gen_run) and end it with gen_end */
int ms_gen_waiting(void);
extern int plg_pick; int plg_picking(void); void plg_click(double x, double y, double z);   /* the point picks after the op dialog */
int ms_live(void);                               /* the flow of a new 3D op runs (cam_mode is its op) */
void drv_toggle(int solid_index);                /* a body pick on part solid I (the solid selection; script drvsolid) */
void drv_toggle_face(int solid_index, int face); /* a face pick (script drvface) */
void drv_toggle_surf(int surf_index);            /* a surface's sel bit 4 (the general selection; script drvsurf) */
void drv_click(float mx, float my);              /* a click in the drive / check prompt: a surface, else a solid body / face (ours: N13-3) */
int drv_done(void);                              /* End Selection (Enter) in the drive / check prompt; 1 = handled */
int ms_dialog_ok(void);                          /* the op dialog's OK of the new op: 1 = taken by the flow (cui_ops.c pp_ok_quick) */
int ms_dialog_cancel(void);                      /* its Cancel: 1 = taken (the 427 question) */
int ms_chain_ok(void);                           /* the Chaining dialog's OK in the flow (containment / blend / curves): 1 = taken */
int ms_gen_end(int ok, int cancelled);           /* cui_ops.c gen_end: 1 = the flow took the result */
void ms_keep_op(int applied, const CamParams *seed, int seed_tool);   /* the new op kept without a toolpath (cui_ops.c) */
void ms_drop_op(void);                           /* the new op deleted (cui_ops.c) */
void ms_dump(void);                              /* tests: the flow's state, its lists, the references (step srfsel) */
int pick_entity_surf(float mx, float my);        /* pick_entity restricted to surfaces (cui_sel.c) */
double pf_field_get(int off, int t);             /* the op dialog's record field (cui_opdlg.c; t 1 f64, 2 u8, 3 s16, 4 s32) */
void pf_field_set(int off, int t, double v);
void chain_cancel(void);                         /* Esc / the dialog's close box (toolpath chaining) */
void chain_keep_last(void);                      /* the chains kept for Last (at the op dialog) */
void polygon_end(void);
void vector_end(void);
void window_chains(double sx, double sy);
int chain_window_drag(void);                     /* a left press in the view drags the selection window */
void chain_window_done(float x0, float y0, float x1, float y1);   /* its release (window coordinates) */
int chain_sketching(void);                       /* a polygon / vector is being sketched (Enter ends it) */
void chain_sketch_end(void);
int chain_press_pick(void);                      /* chaining picks take the press point */
int chain_click_pre(float mx, float my);         /* handle_click: Area / Polygon / Vector / window start / Point (1 = taken) */
int chain_has_view_arc(const Chain *c); int chain_polyline3(const Chain *c, double tol, double **out);   /* view-arc chains (ours) */
int chain_is_3d(const Chain *c);   /* a chain holding a curve outside the XY planes (CS-7): drawn / dumped in 3D */
int cui_all_chaining(void);        /* chaining for an operation: Mastercam's whole chain entity mask (CS-7) */
int chain_click_ent(int e, float mx, float my);  /* handle_click: drill points / chain picks on entity e (0 = not chaining) */
int ov_chained(int e);
void chain_ov_holes(float **pv, int *pn, int *pcap);   /* the picked drill points' crosses */
typedef struct { float x0, y0, x1, y1, w; float t[6]; float rgb[3]; } ChainArrow;
int chain_arrows(ChainArrow *out, int cap);      /* the chains' start / end arrows in window coordinates */
/* the dialog templates: id, x, y, w, h (dialog units), bmp (> 0 mccoreres bitmap, 0 text, -1 radio, -2 combo, -3 group,
   -4 check), text (caption / tooltip) */
typedef struct { int id, x, y, w, h, bmp; const char *text; } ChCtl;
extern const ChCtl CH_1058[], CH_6464[]; extern const int CH_1058_N, CH_6464_N;
extern const char *const CH_WIN_MODES[5];
const ChCtl *chain_dlg_template(int *n, int *h);
void chain_act(int id);                          /* a Chaining dialog control */
void chain_unselect_all(void);
int chain_ctl_live(int id);                      /* enabled (FUN_1801f00d0) */
int chain_ctl_on(int id);                        /* pressed */
int chain_radio_on(int id);                      /* a radio of the dialog (1482 / 1499, Solids mode 3041 / 1499) is the checked one */
int chain_dlg_icon(void);                        /* the dialog shows UIVertDialog's expand icon (not in Solids mode) */
int chain_escape(void);                          /* Esc during chaining: 1 = taken by the solid selection (else chain_cancel) */
/* solid chaining (cui_sldchain.c, formats/chaining.md "Solid chaining"): the Solids mode of the Chaining dialog
   (template 6518) and the Pick Reference Face dialog (mcgeomsldres 134) */
extern int sld_mode, sld_last_mode, sld_use_face, cfg_sld_edge_color, cfg_sld_face_color;
extern const ChCtl CH_6518[], SREF_134[]; extern const int CH_6518_N, SREF_134_N;
int sld_allowed(void);
void sld_enter(void); void sld_leave(void); void sld_commit(void); void sld_reset(void); void sld_cancel(void); void sld_clear(void);
int sld_act(int id); int sld_ctl_live(int id); int sld_ctl_on(int id); void sld_dlg_refresh(void);
int sld_pick_part_solid(float mx, float my);     /* From Solid: the part solid under the cursor (cui_sldchain.c) */
int sld_pick_part_face(float mx, float my, int *solid, int *face, double hp[3]);   /* the part solid and face under the cursor (Line Normal) */
int sld_part_face_pt(int si, int f, float c[3]);   /* tests: the centre of face F's largest triangle (cui_sldchain.c) */
int sld_part_nface(int si);                      /* the faces of part solid SI in this program's face list (cui_sldchain.c topo; XT FACE order) */
int sld_click(float mx, float my);               /* window coordinates; 1 = taken */
int sld_hover_update(float mx, float my);        /* 1 = the highlight changed */
int sld_ref_pending(void); void sld_ref_other(void); void sld_ref_ok(void);
int sld_escape(void); int sld_nitems(void); void sld_prompt(void); void sld_dump(void);
void sld_overlay(float **pv, int *pn, int *pcap);
int sld_chain_arrows(int ci);
int sld_script(const char *st, const char *arg);
/* cui_snap.c helpers for it */
int sld_edge_pick(float sx0, float sy0, int body, int back, int *bo, int *eo, double *depth, double *cp, int max);
int ac_solid_sets(void); const Body *ac_solid_body(int b); int ac_solid_nbodies(void); extern int ac_solids_gen;
int sld_pos_visible(double x, double y, double z);
/* Chaining Options (dialog 405) */
typedef struct { int ent, col, lev, closed, open, sect, ign, spt, brk, surf, dbrk, ccw, cur, zig, sort, inf, rinv, sync; double tol, ptol, ang; } ChainOpts;
void chain_opts_init(ChainOpts *o);
void chain_opts_exclusive(ChainOpts *o, int which);   /* 0 Start chain at point / 1 Break closest entity switched on */
void chain_opts_ok(const ChainOpts *o);
extern const char *const CHO_SORT[5]; extern const char *const CHO_SYNC[7];
/* Drill Point Selection (dialog 2300) */
void drill_unselect(void);
void drill_unselect_all(void);
void drill_ok(void);
int chain_points_dlg(void);                      /* the point ops: the Drill Point Selection dialog */
int chain_points_ok_only(void);                  /* hole-type ops: the Chaining window's OK only */
void cui_chain_config_item(int key, const char *val);
void chain_dump(void);
int cui_chain_script(const char *st, const char *arg);

/* ------------------------------------------------------------------------------------------------ Extrude (cui_extrude.c) */
/* the command (SolidsExtrudeApp, formats/extrude.md): session globals (sec. 2.4), the running command's P and state */
extern ExtrudeParams ex_sess, ex_p; extern int ex_sess_single, ex_auto_op, ex_min_dist, ex_autoprev;
extern char ex_name[64];
extern int ex_single, ex_prev, ex_cdir[256], ex_cid[256], ex_ninit, ex_thin_auto, ex_panel, ex_edit_idx, ex_tab;
extern unsigned char ex_chsel[256];
extern int ex_pick;                              /* 1 AutoCursor point, 2 target body, 3 vector (line / first point), 4 second point */
extern V3 ex_pick_p0; extern int ex_prev_req; extern unsigned long ex_prev_hash, ex_arw_hash;
extern char ex_vec_txt[64]; extern int ex_vec_bad;
extern int ex_drag, ex_drag_chain; extern double ex_drag_anchor[3], ex_drag_dir[3]; extern ExtrudeParams ex_drag_p;
extern int ex_exp[8];                            /* expanders: Operation, Chains, Distance, Trim to Faces, Draft, Thin Wall, Plane Orientation, Preview */
const char *ex_sld(int id);
void ex_log(const char *fmt, ...);
void ex_report(const ExErr *e);
void ex_err_text(const ExErr *e, char *out, size_t n);   /* the box's text in one line (the preview's warning) */
/* ---- Solids > Modify > Constant Fillet (cui_fillet.c; formats/solid_fillet.md) */
#define FL_MAXPICK 4096
#define FL_ERR_REFUSE (-1001)                    /* ours: SOL-F1, Propagate along tangencies off is not built */
typedef struct { double radius; int overflow, mitered, propagate; } FilSess;   /* SLD_FILLET_PARAMS fields (P+0x08 / +0x10 / +0x14 / +0x15) */
typedef struct { int body, idx, rec; double pt[3]; } FlPick;                   /* a pick: body, face / edge index (-1 body), its record, bbox centre */
enum { FLV_NAME, FLV_LIST, FLV_RADIUS, FLV_N };
extern FilSess fl_sess, fl_p; extern int fl_selbtn[3], fl_mode, fl_phase, fl_edit_idx, fl_prev, fl_prev_req, fl_tab, fl_adding, fl_sel_changed;
extern char fl_name[64], fl_warn[300]; extern FlPick fl_pk[3][FL_MAXPICK]; extern int fl_npk[3]; extern unsigned char fl_lsel[3][FL_MAXPICK];
void fl_units(void);
void fl_start(void);
void fl_edit(int fi);
void fl_click(float mx, float my, int shift);
void fl_unselect_all(void);
void fl_sel_done(void);
void fl_add_sel(int all);
void fl_escape(void);
int fl_list_n(void);
void fl_list_item(int i, char *out, size_t n, int *kind, int *k);
void fl_list_select(int i, int on);
int fl_list_selected(int i);
void fl_remove_sel(void);
void fl_validate(const char **e);
void fl_preview(void);
void fl_panel_frame(void);
void fl_end(int accepted, int restart);
void fl_cancel(void);
void fl_accept(int apply);
void fl_set_radius(double r);
void fl_set_overflow(int o);
void fl_set_check(int which, int on);           /* 0 Propagate along tangencies, 1 Mitered corners, 2 Automatically preview */
void fl_ov(float **pv, int *pn, int *pcap);
void fl_dump(void);
void fl_part_solid_msg(void);                      /* ours: a part solid picked (not ported) */
int cui_fillet_script(const char *st, const char *arg);   /* the fillet / fl* MCAMUI_SCRIPT steps (cui_extrude_script calls it) */
void ex_prompt_panel(void);
void ex_chaining(void);
void ex_start(void);
void ex_edit(int fi);
int ex_unchanged(void);
void ex_force(void);
void ex_setparams(void);
void ex_set_type(int t);
void ex_set_target(int b);
void ex_chains_done(void);                       /* the Chaining dialog's OK (cui_hooks.chain_ok while ex_mode) */
int ex_below_min(void);
void ex_escape(void);                            /* Esc / closing the Chaining dialog during Extrude */
void ex_end(int accepted, int restart);
void ex_cancel(void);
ExFeat ex_cur_feat(void);
void ex_preview(void);
void ex_accept(int apply);                       /* OK (0) / Apply = OK-New (1); edit mode: Regen */
void feat_delete_last(void);
int ex_any_sel(void);
void ex_remove_sel(void);
void ex_reverse_sel(void);
void ex_add_chains(int all);
int ex_set_vector(const double v[3]);
void ex_autocursor(V3 p);
int ex_pick_body(float mx, float my);
void ex_click(float mx, float my, const double w[3]);   /* a graphics click while the panel is up (sec. 1.3 events) */
typedef struct { double a[3], d[3], val; int kind, chain; } ExArrow;   /* kind 1 distance, 2 Direction 1, 4 Direction 2 */
int ex_mid_tan(const Chain *c, double p[3], double t[3]);
int ex_arrow_list(ExArrow *ar, int nmax);
double ex_seg_dist(double mx, double my, double x0, double y0, double x1, double y1);
int ex_arrow_hit(float mx, float my, int *kind, double a[3], double d[3]);
void ex_drag_start(int kind, const double a[3], const double d[3]);
void ex_drag_move(float mx, float my);
void ex_drag_end(int cancel);
enum { EXV_NAME, EXV_CHAINS, EXV_DIST, EXV_FACES, EXV_ANGLE, EXV_D1, EXV_D2, EXV_N };
void ex_validate(const char **e);                /* the validators' texts (NULL = valid) */
void ex_panel_frame(void);                       /* ex_force + ex_preview + the arrows' redraw check (each panel frame) */
void ex_ov_arrows(float **pv, int *pn, int *pcap);   /* the drag arrows (overlay lines) */
typedef struct {
    int edit, has_t, nvis;
    int create, cut, boss, target, sel_target, single, auto_op;
    int through_all, trim_hdr, trim_content, min_dist;
    int draft_ctl, split, draft_ends, thin_ctl, d1, d2;
    int preview_btn, buttons, tabs;
} ExEn;
void ex_enables(ExEn *e);                        /* the panel controls' IsEnabled */
void ex_btn_select_target(void);
void ex_btn_autocursor(void);
void ex_btn_select_vector(void);
void ex_set_normal(void);
void ex_set_cplane(void);
void ex_set_through_all(void);
void ex_set_check(int which, int on);            /* the checkbox headers: 3 Trim to Faces, 4 Draft, 5 Thin Wall */
void ex_vec_commit(void);                        /* the Vector box typed (ex_vec_txt) */
void ex_vec_show(void);                          /* ex_vec_txt = the first chain's direction */
void ex_chain_label(int i, char *out, size_t n);
void ex_chain_click(int i, int ctrl, int on);
/* the Solids manager (mastercam.dll HistDlg tree): rows, images, the right-click menu and its actions */
extern const char *const SLD_OPNAME[32]; extern const int SLD_OPBMP[32];
/* the part's solids and their history (Solids manager rows; cui_extrude.c) */
const char *sm_psolid_text(int i);
const char *sm_pop_text(int i, int k);
int sm_pop_editable(int i, int k);
void sm_pop_icon(int i, int k, int sel, char *key, size_t n);
extern const char *SM_STOP_TEXT, *SM_STOP_ICON;
extern unsigned char sm_psolid_open[512];
void sm_psolids_load(void);
extern unsigned char sm_body_open[512]; extern int sm_body_init, sm_sel_kind, sm_sel_idx;
int sld_optype(const ExFeat *f);
void sm_init(void);
const char *sm_feat_text(int i);
void sm_feat_icon(int i, int sel, char *key, size_t n);
enum { SM_SUB_PREP = -2, SM_SUB_SOLIDS = -1, SM_NONE = 0, SM_EXTRUDE, SM_DELETE, SM_REGEN_ALL, SM_EXPAND, SM_COLLAPSE, SM_EDITP };
typedef struct { const char *t; int act; int sep; } SmItem;   /* act < 0 a submenu, SM_NONE greyed; sep: a separator follows */
extern const SmItem SM_SOLIDS[], SM_PREP[]; extern const int SM_NSOLIDS, SM_NPREP;
int sm_menu(int row_kind, int row_feat, SmItem *it, int cap);   /* row_kind 0 background, 1 solid, 2 op */
void sm_menu_act(int act, int row_feat);
void ex_dump(void);                              /* MCAMUI_EXDUMP=1 */
int ex_show_step(const char *arg);               /* show:extrude / exedit:N / exchain:E / exchainok */
int cui_extrude_script(const char *st, const char *arg);

/* ------------------------------------------------------------------------------------------------ operations (cui_ops.c) */
extern CamOp pending;                            /* the op the toolpath dialog edits (show_params) */
extern int tool_choice;                          /* index into F.tools, or F.ntools = custom */
extern int edit_camop;                           /* camops index being edited in the params dialog, -1 = new op */
extern CamOp *op_clip, *op_deleted; extern int op_deleted_at, op_clip_cut;   /* Toolpaths manager: clipboard, last deleted */
extern char tp_grp_name[260], mach_grp_name[260];
extern int cm_group, cm_ctrl;                    /* the right-clicked group (0 none, 1 toolpath, 2 machine), Ctrl held */
const Op *default_op(int type, int metric);
void default_op_reset(void);                     /* Q19 (ours): read the defaults file again after a save / reset */
void heights_from_default_op(CamParams *p, int type, int metric, double cz);
void open_params(void);                          /* the chaining's OK: a new op (cui_hooks.chain_ok's op branch) */
void generate_pending(void);                     /* the dialog's OK */
int gen_begin(CamOp *job);                      /* OK in parts (ours, a worker thread): apply the dialog, a copy to compute; 0 = nothing to run */
int gen_run(CamOp *job);                        /* cam_generate on the copy (reads F only) */
void gen_end(CamOp *job, int ok, int cancelled); /* the copy back into pending + the commit; cancelled: pending as before OK */
int gen_can_cancel(void);
int gen_abortable(const CamOp *job);           /* the engine stops on mcce_abort_set (Dynamic Mill) */
void gen_commit(void);
void pp_ok(void);                               /* OK: an unchanged existing op is left as it was (Mastercam edit_operation), else generate_pending */
void pp_ok_snapshot(void);                      /* the dialog's inputs as opened (open_op_params) */
int pp_ok_unchanged(void);
void pp_ok_forget(void);                       /* OK would hand cam_generate what the dialog opened with */
void pending_free(void);
Chain chain_dup(const Chain *c);
void open_op_params(int op_index);               /* the tree's Parameters row / a double click */
void readd_camops(void);
int takeover_index(int id);
void add_op_at(CamOp *op, int at);
void regen_ops_for_tool(int num);
void post_ops(void);                             /* Post selected operations (the LinuxCNC post) */
int selected_camop(void);
void delete_selected_op(void);
CamOp *camop_clone(const CamOp *s);
void camop_insert(CamOp *op, int k);
int camop_regen(CamOp *op);
void op_seq_assign(CamOp *op);     /* cui_ops.c: an op made here gets the next CamParams.op_seq (OMgetNextOpId order) */
void cutoff_children(CamOp *par);   /* cui_ops.c: FUN_180044170, the separate Tab Cutoff op (0x82) of a contour op */
void op_copy(int cut);
void op_copy_ex(int cut, int append);            /* Cut / Copy; APPEND (Shift held) adds to the buffer (CutSelected / CopySelected param 1) */
void op_paste(void);
int op_paste_ready(void);                        /* a cut or copy buffer to paste */
void op_undelete(void);                          /* UndeleteOperations: the last delete back */
int op_delete_buffered(void);                    /* ops in the delete buffer */
void op_buffers_purge(int mask);                 /* DeleteBufferedOperations: 1 cut, 2 delete buffer (permanent) */
void op_buffers_reset(void);                     /* a new / opened part */
extern int op_del_prompt;
extern int cui_script_yes;
extern int cui_script_radio;                    /* tests: the radio_dialog answer (step bosscav:N; -1 = Cancel) */
extern double cui_script_real; extern int cui_script_real_ok;   /* tests: the value / OK scripted ask_real dialogs get (cui_draft_dim.c step dimreal:V | dimreal:cancel) */
extern int cui_script_choice;                   /* tests: the button scripted ask_choice dialogs get (step askchoice:N; default 0) */
void dirty_ent_users(int ent);
void mod_unpair(void);                   /* the edit dropped: mod_pair's chains go back */
void mod_pair(int orig, int moved);             /* a Move: MOVED replaces ORIG (xform_swap_idn: same id; the ops' chains follow it) */                   /* validate_toolpath: the ops using entity ENT (deleted / moved) get dirty */                      /* tests: the answer scripted questions get (step askyes:0|1; default Yes) */                        /* Display Options 4061 "Prompt for toolpath deletion" (1 at start) */
extern int32_t *op_gone_ids; extern int nop_gone;   /* ops of the part out of the list here (project file OPGONE lines) */
void op_gone_apply(int32_t id);                  /* the project file: stored op ID deleted (permanently) */
int edit_selected(int (*fn)(CamOp *op, const uint8_t *raw, uint32_t rawn, void *ctx), void *ctx, int *skipped);
FsGroup fs_group(void);
const Material *fs_material(char *why, size_t n);
void recalc_feeds(void);
void reverse_selected(void);
void fs_from_source(CamParams *p, const Tool *t);
/* the Toolpaths manager right-click menu (OPMANRES.DLL menu 780, formats/opmanager_menu.md) */
enum { CM_NA = 0, CM_OP, CM_EDIT, CM_DELETE, CM_UNDO, CM_EXPAND, CM_COLLAPSE, CM_TOOLLIST, CM_CUT, CM_COPY, CM_PASTE, CM_UNDELETE, CM_REVERSE, CM_RECALC, CM_SEP,
       CM_MILLPOP, CM_ALIAS, CM_HIDE, CM_CTRL, CM_NEEDOPS, CM_NEEDSEL, CM_DOCFILE, CM_RENUMTOOLS, CM_GRP_NEW, CM_GRP_RENAME, CM_GRP_DELETE, CM_GRP_DELEMPTY,
       CM_LATHEPOP, CM_LFACE };   /* the Lathe toolpaths popup (usable on a lathe machine group) and its Face (cui_lface.c) */
typedef struct { int depth; const char *text; int act; const char *arg; } CMItem;
extern const CMItem cm_items[]; extern const int cm_nitems;
#define CM_N cm_nitems
int cm_kids_of(int i);
int cm_shown(int i);
int cm_has_kids(int i);
int cm_router(void);
int cm_leaf_ok(int act);
int cm_usable(int i);
void cm_run(int i);
void op_doc_file(void);                          /* Doc file (cui_optree.c) */
/* the Toolpaths manager rows (cui_optree.c: viewer/optree_text.inc) */
void cui_otx_op_row(int i, char *out, size_t n);
void cui_otx_params_row(int i, char *out, size_t n);
void cui_otx_tool_row(int i, char *out, size_t n);
void cui_otx_geom_row(int i, char *out, size_t n);
void cui_otx_path_row(int i, char *out, size_t n);
int cui_otx_is_drill(int oc);
const char *cui_sys_plane(int i);
int cui_op_script(const char *st, const char *arg);   /* the MCAMUI_SCRIPT operation / op dialog steps; 1 = handled */
int pp_ok_quick(void);                           /* OK without computing: unchanged / post values only / marked dirty (1 = done) */
/* dirty operations (cui_dirty.c; formats/opdlg_port_audit.md "Dirty operations"): op +0x94 */
int op_dirty(int i);                             /* F.ops[i] is dirty (the tree's X image) */
CamOp *op_dirty_edit(int i);                     /* its changed parameters waiting for regeneration, NULL = none */
void op_mark_dirty_edit(int i, CamOp *edit);     /* edit_operation's changed case: op i dirty (EDIT taken; NULL = mark only) */
void dirty_clear_doc(void);
void dirty_forget_stored(int id);                /* a stored op left the list */
int op_regen_selected(void);                     /* toolbar 800 "Regenerate all selected operations" (OMregenSelectedOperations) */
void op_select_dirty(void);                      /* "Select all dirty operations" (OMselectDirtyOperations) */
int op_regen_dirty(void);                        /* "Regenerate all dirty operations" (OMregenDirtyOperations) */
enum { DOP_NONE, DOP_POST, DOP_BACKPLOT, DOP_VERIFY };
extern int dop_pending, dop_choice;              /* dialog 977 open (the held command), its radio 0 Regenerate / 1 Do not regenerate */
int dop_check(int action);                       /* DirtyOperationWarning: 1 = dialog 977 opened, the command waits */
int dop_answer(int ok);                          /* 977 OK / Cancel: the command to run now (DOP_*), 0 none */
const unsigned char *dop_post_only(unsigned char *only);   /* the post's op mask after "Do not regenerate" (NULL = all) */
void dirty_write_project(FILE *f);
void dirty_read_project(const char *proj, CamOp *(*camop_of_k)(int k, void *ctx), void *ctx);
int dirty_save_list(CamOp ***out);               /* the ops the .mcam save writes (edits in place of their ops) */
int cui_dirty_script(const char *st, const char *arg);
/* Q19 "Save parameters to defaults file" 9433 + the ours reset / restore commands (cui_defaults.c) */
int pp_save_defaults(void);                      /* OnWriteToDefaultsPb: 1 = written */
int pp_save_defaults_live(void);
enum { DFLQ_RESET = 1, DFLQ_RESTORE = 2 };
typedef struct { int pending; char text[1600]; char arg[200]; } DflQuestion;
extern DflQuestion dfl_q;                        /* a confirmation waiting for its answer (dfl_answer) */
int dfl_has_copy(void);                          /* the program's working copy of the group's defaults file exists */
int dfl_backup_names(char (*names)[160], int max);   /* its dated backups, oldest first */
const char *dfl_work_path(char *out, size_t n);
int dfl_reset_ask(void);                         /* "Reset defaults file to original (ours)" */
int dfl_restore_ask(int i);                      /* "Restore a dated backup (ours)": backup I (0 = the oldest) */
int dfl_answer(int yes);
int cui_defaults_script(const char *st, const char *arg);
/* Q20 "Preview toolpath" 10501 (cui_preview.c) */
extern int pv_on;
int pv_shown(void); int pv_live(void);
int pv_begin(CamOp *job); void pv_end(CamOp *job, int ok, int cancelled);   /* the button in parts (a worker thread runs gen_run) */
int pp_preview(void);                            /* the three in a row */
int pv_has_job(void); void pv_commit(void); void pv_commit_dirty(void); void pv_cancel(void); void pv_clear(void);
void pv_overlay(float **v, int *n, int *cap);
void pp_pv_snapshot(void); void pp_pv_forget(void); int pp_pv_unchanged(void);
int cui_preview_script(const char *st, const char *arg);
void op_dump(void);                              /* MCAMUI_OPDUMP=1 */
void op_ctl_dump(void);                          /* opctl: the shown page's controls */

/* ------------------------------------------------------------------------------------------------ toolpath dialog (cui_opdlg.c) */
enum { PG_TYPE = 1, PG_TOOL, PG_HOLDER, PG_STOCK, PG_CUT, PG_DEPTH, PG_FINPASS, PG_TROCH, PG_TRANS, PG_LEAD, PG_BREAK, PG_LINK,
       PG_HOME, PG_ARC, PG_PLANES, PG_COOL, PG_CANNED, PG_MISC, PG_AXIS, PG_AXCOMBO, PG_ROTARY, PG_MHEAD, PG_MULTI, PG_TIP,
       PG_TABS, PG_ROUGH, PG_FINISH, PG_ROUGHFIN, PG_ENTRY, PG_TABCUT, PG_WALL, PG_FINISH2, PG_HSTLEAD, PG_SETUP, PG_N };
typedef struct { int clr_on, clr_end, clr_inc, ret_on, ret_inc, fp_inc, top_inc, dep_inc; double cz, fstock, clr_abs; int thr_clr_on; double thr_clr; double bt; } OpHFlags;   /* bt: the break through set_depths takes off the depth (cam_bt_eff; hf_apply's depth 0x1a0 adds it back); clr_abs: the Clearance value, kept while it is off; thr_clr (thr_clr_on): Thread Mill's own clearance (record 0x1230) of a stored op / an edit of page 2837 */
enum { LKP_NONE, LKP_CLR, LKP_RET, LKP_FP, LKP_TOP, LKP_DEPTH };
extern const char *const PG_TITLE[PG_N];
extern OpHFlags pp_hf;                           /* the dialog's op */
extern int pp_page;                              /* the active page (ToolpathDlg::s_ActivePage) */
extern const uint8_t *pp_rec; extern uint32_t pp_rec_n;
extern int pp_tstate[PG_N], pp_tree_fresh, pp_mp_pic;
extern int lk_pick;                              /* a Linking page "..." button waits for a graphics point (LKP_*) */
int pg_has(const CamParams *p, int s);
int pg_done(const CamParams *p, int s);
int pg_parent_ok(const CamParams *p, int i);
void hf_from_record(OpHFlags *h, const uint8_t *r, uint32_t n, double cz);
void hf_from_op(OpHFlags *h, const CamOp *op);
void rp_put(CamParams *p, int off, const void *v, int n);
void hf_apply(CamParams *p, const OpHFlags *h);
void lk_pick_z(double z);
void lk_click(double wx, double wy, double gz);  /* a graphics click while lk_pick is armed */
/* Contour Tabs page Position... 1098 (FUN_180190070 / position_tab): 1 = pick a chain entity, 2 = place the tab (cui_opdlg.c) */
extern int tb_pick, tb_hok; extern double tb_hx, tb_hy;
void tb_start(void); void tb_click_ent(int e); void tb_move(double x, double y); void tb_click_pt(double x, double y);
void tb_key(int enter);                           /* 1 Enter, 0 Esc */
void tb_overlay(float **pv, int *pn, int *pcap);
int pp_rs16(uint32_t o);
int pp_page_pic(const CamParams *p, int pg, int *x, int *y, int *w, int *h, char *file, size_t fn);
int pp_stock_bmp(double w, double f);
int pp_disabled(const CamParams *p, int s);
void pp_hf_record(const uint8_t *r, uint32_t n, double cz);
void pp_hf_op(const CamOp *op);
void pp_hf_apply(void);
/* Linking rows */
enum { LK_CLR = 1, LK_RET, LK_FP, LK_TOP, LK_DEP };
enum { LK_ON, LK_VAL, LK_INC, LK_PICK };
typedef struct { int id; const char *btn; int has_on, on, live, inc, pick; double shown; } LkRow;
void lk_frame(void);                             /* the page's per-frame writes (draw only when not scripting) */
int lk_rows(LkRow *r);                           /* up to 5 rows */
void lk_set(int row, int what, double v);        /* LK_ON (the check), LK_VAL (the shown value), LK_INC (1 Incremental), LK_PICK (the "..." button) */
void lk_set_clr_end(int on);
/* the frame (6544, dialog units) */
typedef struct { int x, y, w, h; } PpRect;
enum { PPF_TREE, PPF_QVLABEL, PPF_QV, PPF_LEG1, PPF_LEG2, PPF_LEG1T, PPF_LEG2T, PPF_PAGE, PPF_STATUS, PPF_OK, PPF_CANCEL, PPF_APPLY, PPF_HELP, PPF_N };
extern const PpRect PP_FRAME[PPF_N];
#define PP_W 567
#define PP_H 395
typedef struct { int rect, id, bmp, live; const char *tip; } PpButton;
extern const PpButton PP_BUTTONS[4];
typedef struct { int bmp; const char *tip; int hst; } PpTbBtn;
#define PP_NTB 9
extern const PpTbBtn PP_TB[PP_NTB];
int pp_hst(const CamParams *p);
PpRect pp_wrap_rect(int x, int y, int w, int h);   /* the controls' group beside a page picture */
void pp_title(char *out, size_t n);
int pp_page_shown(void);                         /* the page drawn (Axis Control shows Axis Combination) */
void pp_frame_begin(void);
typedef struct { int slot, depth, done, state; const char *title; } PpTreeRow;
int pp_tree_rows(PpTreeRow *out, int cap);
void pp_tree_click(int slot);
void pp_note_change(const CamParams *before, const OpHFlags *hbefore);
int pp_quickview_rows(const char **nm, char (*v)[64]);   /* up to 10 rows */
void pp_cancel(void);
void pp_dialog_op(CamOp *out);                   /* Q19 / Q20: pending with the pages applied (shallow copy) */                            /* Cancel / Esc */
/* batch 12 (formats/opdlg_port_audit.md 18): Apply 12321, Hide dialog 10320, Reload parameters from defaults file 9431, the toolbar */
int pp_apply_enabled(void); void pp_apply(void);
extern int pp_hide_on; void pp_hide(void); void pp_hide_end(void);
int pp_reload_defaults(void);
int pp_tb_live(int i); void pp_tb_click(int i);
/* a page's controls (not Tool / Linking), walked in the nuklear layout order; each control op returns the value shown now */
typedef struct {
    void (*row)(void *u, float h, int du, int cols);                     /* a dynamic row h px high (du: h in dialog units) */
    void (*row_pic)(void *u, float h_du, float pad_px, float w_du, const char *file);   /* a picture row: pad, the picture w x h */
    void (*row_static_pic)(void *u, float h_du, float w_du, const char *file);          /* a one-cell row holding a picture */
    void (*pic)(void *u, float w_du, float h_du, const char *file);      /* a picture in the next cell */
    void (*spacing)(void *u);
    double (*prop)(void *u, const char *label, double v, double lo, double hi, double step);   /* label "#..." */
    int (*iprop)(void *u, const char *label, int v, int lo, int hi);
    int (*check)(void *u, const char *label, int v);
    int (*combo)(void *u, const char *const *items, int n, int cur, float pw, float ph);
    void (*label)(void *u, const char *t, int style);                    /* 0 normal, 1 error */
    void (*option)(void *u, const char *t, int on);                      /* a greyed radio button (not ported) */
    int (*button)(void *u, const char *t, int enabled);                  /* a push button: 1 = clicked (the Planes page) */
    int (*radio)(void *u, const char *t, int on);                        /* a live radio button: 1 = clicked */
} PpOps;
void pp_page_body(int pg, const PpOps *ops, void *u);

/* ------------------------------------------------------------------------------------------------ Tool page / Tool List (cui_toolpage.c) */
extern char cui_ui_dir[600];                     /* the front end's ui folder */
extern int cfg_places;                           /* config 29003 */
extern ToolLib tlib; extern int tlib_ok;         /* the library open in the Tool Manager */
enum { TM_COLS = 13 };
typedef struct { int vis[TM_COLS], w[TM_COLS], ord[TM_COLS], sortcol, sortasc; } TmColSet;
extern TmColSet tm_cs[2][3];
#define TM_SET(L) (&tm_cs[L][(L) == 0 ? 2 : tm_dispmode])
extern const char *tm_head[TM_COLS];
extern int tm_view, tm_dispmode, tm_factive[2], tm_listview[2], tm_colinit;
void tm_cs_reset_list(int L);
void tm_settings_read(void);
void tm_settings_write(void);
void tm_col_toggle(int L, int c);
void tm_real(char *o, size_t n, double v, int b);
double tm_info_pitch(const Tool *t);
void tm_cell(const Tool *t, int col, char *o, size_t n);
const Tool *tm_tool(int L, int i);
int tm_count(int L);
int tm_filter_test(int L, const Tool *t);         /* TestToolOk's tests without the Filter Active switch (cui_toolmgr.c) */
extern int tp_factive, tp_filt_open; int tp_rows(int *idx, int cap);   /* batch 12: the Tool page's filter (cui_toolpage.c); the rows sorted */
extern TmColSet tp_cs[3]; extern int tp_listview, tp_list_init;       /* the Tool page's own CMillToolListCtrl (cui_toolpage.c) */
void tp_list_open(void);                         /* the op dialog opens: the page's list is made again (InitPage reads its settings) */
void tp_list_ok(void);                           /* the dialog's OK: UpdateLocalOp -> WriteToolIconPrm */
int tp_list_cols(int *cols, int *w);             /* the page list's visible columns >= 2 in order and their widths */
void tp_sortlist(int c);                         /* LVN_COLUMNCLICK on 25463: OnListClickColumn -> SortList(c) */
int tm_list_cols(int L, int *cols, int *w);     /* the visible columns >= 2 in order and their widths */
void tm_open_lib(const char *path);
void starter_lib_open(void);
int next_tool_num(void);
int part_add_tool(const Tool *t);
extern int tl6410_open, tl6410_sel, otp_wait_new;
extern uint8_t tp_rapid, tp_ftc, tp_batch;
int tp_metric(void);
int tp_hem(const CamParams *p);
void tp_from_record(const uint8_t *r, uint32_t n);
void tp_from_rpatch(const CamParams *p);
void tp_rp(CamParams *p, int off, const void *v, int n);
void tp_apply(CamParams *p);
int tp_z(const Tool *t);
double tp_fpt(const CamParams *p, const Tool *t);
double tp_sfm(const CamParams *p);
const McamFile *tl6410_mach(void);
void tl6410_mach_reset(void);
void grp_defaults_file(char *out, size_t n);     /* Q19: the machine group's operation defaults file on this host ("" = none) */                    /* Q19 (ours) */
const char *tl6410_libfile(void);
void tl6410_machine_lib(char *out, size_t n);
void pp_tp_record(const uint8_t *r, uint32_t n);
void pp_tp_op(const CamOp *op);
void pp_tp_apply(void);
enum { TPK_LIST, TPK_LABEL, TPK_RO, TPK_EDIT, TPK_DIV, TPK_BTN, TPK_CHECK, TPK_COMBO, TPK_TEXTBOX, TPK_GROUP, TPK_BMP, TPK_RADIO };
typedef struct { int kind, id, x, y, w, h; const char *text; int ref; } TpCtl;   /* ref: 0 enabled, -1 greyed */
int tp_ctls(TpCtl *o, int cap);                  /* the Tool page's controls (template positions, draw order) */
const Tool *tp_tool(void);
void tp_ctl_text(int id, char *v, size_t n);
int tp_ctl_check(int id);
int tp_ctl_combo(void);
extern const char *const TP_DIR[2];
void tp_ctl_set(int id, double v);               /* an edit committed / a check or the combo changed / Select library tool... */
void tp_set_comment(const char *s);
void tp_pick(int i);                             /* a list click (OnItemSelect) */
void tp_new_tool_done(void);                     /* the Define Tool wizard closed (AddNewTool) */
enum { OTP_NONE, OTP_SELLIB, OTP_NEW, OTP_EDIT, OTP_TM, OTP_REINIT };
typedef struct { char text[200]; int act, en, sep; } OtpItem;
int otp_menu(OtpItem *it, int cap);              /* menu 1247 */
void otp_menu_act(int act);
#define TL6410_N 15
#define TL6410_W 440
#define TL6410_H 231
extern const TpCtl TL6410[TL6410_N];
void tl6410_prepare(void);
void tl6410_count_text(char *b, size_t n);
void tl6410_ok(void);
void tl6410_cancel(void);
void tl6410_show(void);                          /* the Tool List opens (a new CToolList: its list settings read again) */
int tl6410_dispmode(void);                       /* its own "Tool Manager Display Mode" (GUI\\Mill Tool Library Selection) */
const TmColSet *tl6410_set(void);                /* its list's active column set */
void tl6410_sortlist(int c);                     /* LVN_COLUMNCLICK on 1762: 0x18086bdb0 -> SortList(c) */
int tl6410_cols(int *cols, int *w);              /* the visible columns >= 2 in order and their widths */
int tl6410_rows(int *idx, int cap);              /* the library rows in the list's order */
void tm_cs_default(TmColSet s3[3], int list6a9); /* a CMillToolListCtrl's defaults (no settings read) */


/* ------------------------------------------------------------------------------------------------ Tool Manager (cui_toolmgr.c) */
extern int show_tm, tm_part_sel, tm_lib_sel, tm_pick_for_params, tm_bup_done, tm_found_init, tm_confirm, tm_focus, tm_lib_modified;
extern unsigned char tm_sel[2][4096]; extern int tm_anchor[2];
extern Tool *tm_clip; extern int tm_nclip; extern char tm_imp_last[1100];
extern char cfg_rpt_dir[300], cfg_rpt_mill[80], cfg_rpt_router[80], cfg_rpt_mill_simple[80], cfg_rpt_mill_det[80];
void cui_rpt_config_item(int key, const char *q);   /* config 31020 / 31011 / 31017 / 31006 / 31007 (cui_config_item) */
typedef struct { unsigned char type[26], mat[6], rad[3]; int op, unit, dsel; double d1, d2; } TmFilt;
extern TmFilt tmf, tmf_edit; extern int tmf_init, tmf_open;
typedef struct { int x, y, type; const char *t; } TmfType;
#define TMF_NTYPES 25
extern const TmfType TMF_TYPES[TMF_NTYPES];
extern const char *const TMF_OPM[3], *const TMF_UM[3], *const TMF_DM[5], *const TMF_RAD[3], *const TMF_MAT[6];
void tmf_defaults(TmFilt *f);
void tm_open_filter(void);
void tmf_ok(void);
void tmf_types_all(int on);
int tm_used(const Tool *t);
int tm_filter_ok(int L, const Tool *t);
int tm_rows(int L, int *idx);                    /* the shown rows (filter, display mode, sort) */
void tm_sortlist(int L, int c);
void tm_sortset(TmColSet *s, int c);            /* SortList 0x18050b3e0 on one column set */
void tm_sort_rows(int L, const TmColSet *s, int *idx, int n);   /* SortColumn 0x18050b060 order */
int tm_nsel(int L);
void tm_clear_sel(int L);
int tm_colmenu_ok(int L, int c);
int tm_lib_write(void);
enum { TQ_NONE, TQ_SIMILAR, TQ_DUPLICATE, TQ_SAVELIB };
typedef struct { int kind, dont_box; char title[64], text[400]; } TmPrompt;   /* dont_box 0 none, 1 shown, 2 ticked */
extern TmPrompt tmq;
typedef struct { int active, L, pos, added, last, base_n; Tool *t; int n; } TmJob;
extern TmJob tmj;
void tm_up(void);
void tm_down(void);
int tm_up_ok(void);
int tm_down_ok(void);
void tm_copy(int L);
void tm_paste(int L);
void tm_delete(int L);
void tm_delete_ask(int L);
void tm_confirm_answer(int yes);
void tm_delete_unused(void);
void tm_create(int L);
void tm_edit(int L);
void tm_close_now(void);
void tm_ok(void);
void tm_cancel(void);
void tm_q_answer(int yes);
void tm_browse(const char *path);
void tm_new(const char *path);
void tm_import(const char *part, const char *lib);
void tm_save_to(const char *path);
void tm_compress(const char *path);
void tm_def_ext(char *p, size_t n);
void tm_import_target(const char *part, char *dir, size_t nd, char *name, size_t nn);
void tm_import_to(const char *lib);
void tm_lib_dir(char *dir, size_t n);
void tm_lib_opened(void);                        /* cui_hooks.lib_opened of a front end with a Tool Manager */
void tm_import_start(char *dir, size_t nd, char *name, size_t nn);
void tm_open_init(void);
enum { TMK_NONE, TMK_INSERT, TMK_DELETE, TMK_C, TMK_V };
void tm_key(int L, int k, int ctrl);
void tm_escape(void);
void tm_click(int L, int i, int k, const int *idx, int n, int ctrl, int shift, int right);
void tm_dblclick(int L, int i);
void tm_item_text(const Tool *t, char *o, size_t n);
const char *tm_item_strip(int used, int large);
int tm_item_cell(const Tool *t);
enum { TMA_NONE, TMA_SEP, TMA_CREATE, TMA_EDIT, TMA_DELETE, TMA_DELUNUSED, TMA_COPY, TMA_PASTE, TMA_SAVETO, TMA_COMPRESS, TMA_REPORT, TMA_DREPORT,
       TMA_VIEW0 = 100, TMA_SORT0 = 200 };
typedef struct { int depth; const char *text; int act, en, checked, sub; } TmMItem;   /* sub: a cascade (1 View, 2 Arrange tools, 3 Import/export) */
int tm_menu_items(int L, TmMItem *it, int cap);
void tm_menu_act(int L, int act);
typedef struct { float x, y, w, h; } TmRect;
typedef struct { TmRect combo, bmp, used_lbl, view, pfa, lfa, filt, grp, rad[3], path, bnew, bbrw, bimp, up, down, plist, llist, plbl, llbl, ok, help; } TmLayout;
void tm_layout(float cw, float ch, float dx, float dy, TmLayout *Lo);
void tm_shown(int *part, int *lib, int *updown, int *dispmode);
void cui_tm_describe(const Tool *t, char *o, size_t n, int upper);   /* tl_make_description / CreateToolDescription (cui_optree.c) */
/* reports */
extern RLayout tlr; extern int show_tlr, tlr_new; extern char tlr_title[160];
void make_rstr(double v, char *b, size_t n, int places);
int ci_path(const char *win, char *out, size_t n);
int op_tool_index(int i);
void tool_report(void);                          /* the Toolpaths manager's Tool list... */
void tm_report(int L, int detailed);             /* the Tool Manager's Report... / Detailed report... */
void tlr_dump(void);
/* Renumber tools (opmanres 805) */
extern int rn_start, rn_inc, rn_len_on, rn_dia_on, show_rn, rn_new, rn_unused, rn_head, rn_lofs, rn_dofs;
extern char rn_txt[4][16];
void rn_open(void);
void rn_apply(void);
void rn_ok(void);
void rn_cancel(void);
void tm_dump(void);
int cui_tm_script(const char *a);
int cui_tool_show_step(const char *arg);         /* show:tm... / tnew / tw... / coolant / holder (both apps) */
int cui_rn_script(const char *st, const char *arg);   /* renum / rnset / rnok / rncancel / rndump / tlrdump / toolrpt */

/* ------------------------------------------------------------------------------------------------ Define Tool wizard (cui_toolwiz.c) */
extern Tool tedit; extern int tedit_index, tm_lib_tool_edit, tw_page;
extern TwState tws;
extern const char *const TW_TITLES[3];
void tedit_open(int index, const Tool *src);
void tedit_close(void);
void tw_open_part(int index);
int tw_first_page(void);
const char *tw_title(void);
void tw_caption(char *cap, size_t n);
int tw_can_back(void);
int tw_can_next(void);
int tw_can_finish(void);
void tw_back(void);
void tw_next(void);
void tw_goto(int p);
void tw_pick_type(int type);
extern StdSize *tw_sizes; extern int tw_nsizes, tw_size_pick, tw_warn_row, tw_warn_off;
int tw_std_refresh(const Tool *t);
const char *tw_std_prompt(const Tool *t);
void tw_std_row_text(const Tool *t, int i, char *row, size_t n);
void tw_std_choose(int i);
void tw_warn_answer(int yes, int dont);
void tw_finish(void);
enum { TWP_GROUP, TWP_INT, TWP_NUM, TWP_COMBO, TWP_TEXT, TWP_BUTTON, TWP_CHECK };
typedef struct { int kind, id; const char *label; int col; const char *tip; } TwPRow;   /* col 0 Operation, 1 General + type panel */
int tw_materials(const char *const **items);
int tw_prop_rows(const Tool *t, TwPRow *o, int cap);
double tw_prop_get(const Tool *t, int id);
void tw_prop_text(const Tool *t, int id, int is_int, char *b, size_t n);
int tw_parse_num(const char *s, double *out);
int tw_parse_int(const char *s, long *out);
void tw_prop_set(Tool *t, int id, double dv);
int tw_prop_combo(const Tool *t, int id, const char *const **items, int *cur);
void tw_prop_set_combo(Tool *t, int id, int ns);
int tw_prop_check(const Tool *t, int id);
void tw_prop_set_check(Tool *t, int id, int on);
char *tw_prop_str(Tool *t, int id, size_t *cap);
void tw_prop_set_str(Tool *t, int id, const char *s);
typedef struct { int open; Tool *t; int style; } TwCool;
extern TwCool cool;
extern const char *const COOL_ONOFF[2], *const COOL_NAMES[3], *const COOL_CNAMES[10];
int coolant_is_open(void);
void coolant_dlg_open(Tool *t);
int cool_idx(int i);
int cool_enabled(int i);
void cool_set(int i, int ns);
void cool_ok(void);
void cool_cancel(void);
int cui_tw_show_step(const char *arg);
void tw_dump(void);

/* ================================================================================================ phase 8 */
#include "backplot.h"
#include "verify.h"
double cui_clock(void);                          /* seconds, monotonic (only differences are used: play, spin) */
/* a dialog template control (ribbonuires / mccadutilres / opmanres templates): id, class (0 static, 1 button, 2 edit, 3 list
   box, 4 up-down, 5 radio, 6 combo, 7 list view, 8 group box), x y w h in dialog units, text */
typedef struct { unsigned id; int cls, x, y, w, h; const char *text; } BsCtl;
extern const BsCtl NOTE_452[];                    /* the Note dialog's template (mccoreres 452; cui_draft_note.c) */

/* ------------------------------------------------------------------------------------------------ Backplot (cui_backplot.c) */
extern int bp_on; extern Backplot bpl; extern BpOptions bp_opt, bpo;
extern int bpo_open, bpo_use_verify; extern int *bpo_pick;
extern int bp_exp, bp_tab, bp_show_cs, bp_new, bp_playing, bp_speed_pos; extern double bp_last_adv, bp_exp_h;
extern char bp_cs_txt[4][32];
extern int bp_tr_grp_open, bp_tr_sel; extern char bp_tr_op_open[1024];
void bp_tree_init(void);
void bp_stop(void);
void bp_start(void);
void bp_toggle(int byte);
void bp_keys(int key);
void bp_sync_tool(void);
void bp_build_lines(void);
void bp_set_lines(void);
void bp_advance(void);
void bp_frame(void);                             /* one frame of the VCR bar: play, the drawn portion, the tool */
void bp_frame_still(void);                       /* the same without play (script steps) */
typedef struct { int id; float x; const char *img, *tip; } BpVcrBtn;   /* x < 0: from the right edge */
#define BP_NVCR 10
extern const BpVcrBtn BP_VCR[BP_NVCR];
int bp_vcr_checked(int id);
int bp_vcr_enabled(int id);
void bp_vcr(int id);
void bp_slider_seek(double f);
double bp_slider_pos(void);
double bp_stop_tick(int k);
void bp_cs_open(void);
void bp_cs_at_ops(int on);
void bp_cs_ok(void);
typedef struct { int id, byte; float x; const char *img, *tip; } BpTgl;
extern const BpTgl BP_TOGGLES[7];
int bp_tgl_enabled(int id);
void bp_tgl(int id);
void bp_row2(int id);
enum { BPE_BYTE, BPE_INT, BPE_REAL, BPE_OPAC };
void bpo_update(void);
void bp_options_open(void);
void bpo_ok(void);
void bpo_cancel(void);
void bpo_color_done(int c);
void bpo_edit_text(int kind, const void *val, char *s, size_t n);
int bpo_edit_commit(int kind, void *val, const char *s, double lo, double hi, char *clue, size_t nclue);   /* 1 = the ClueWnd shows clue */
void bpo_spin(double *g, int up);
enum { BPK_NONE, BPK_STATIC, BPK_FAST_T, BPK_FAST, BPK_ENDPTS, BPK_INTERP, BPK_INC_T, BPK_INC, BPK_CL_OP, BPK_CL_TL, BPK_FIT, BPK_HOME_L,
       BPK_CAXIS, BPK_HOME_M, BPK_SIMCC, BPK_SIMAX, BPK_SIMROT, BPK_CC_CTL, BPK_CC_RAD_T, BPK_CC_RAD, BPK_PLAIN, BPK_FLUTED, BPK_SHADED,
       BPK_T_COLOR, BPK_T_MAT, BPK_T_COL, BPK_T_MATL, BPK_AUTO, BPK_ASDEF, BPK_SPIN, BPK_H_COLOR, BPK_H_MAT, BPK_H_COL, BPK_H_MATL,
       BPK_OP_A, BPK_OP_I, BPK_LOOP_OP, BPK_LOOP_TC, BPK_C_RAPID, BPK_C_LIN, BPK_C_CW, BPK_C_CCW, BPK_USEVF, BPK_C_VF, BPK_VEC, BPK_C_VEC,
       BPK_VEC_T, BPK_VEC1, BPK_VEC2, BPK_VEC3, BPK_VECLEN, BPK_VLEN, BPK_OK, BPK_CANCEL, BPK_HELP };
typedef struct { char t; int key; float x, y, w, h, x2, x3; const char *text; } BpoCtl;
extern const BpoCtl BPO_CTLS[];
int bpo_edit_id(int key);
int bpo_on(int key);
int bpo_en(int key);
void bpo_click(int key);
void *bpo_val(int key, int *kind, double *lo, double *hi);
void bpo_select(int key);
typedef struct { int lvl, id, op, m0, m1, expandable, open, selected; char text[220]; } BpTreeRow;
int bp_tree_rows(BpTreeRow *out, int cap);
void bp_tree_toggle(const BpTreeRow *r);
void bp_tree_click(const BpTreeRow *r);
void bp_tree_expand_all(int on);
typedef struct { char t; int id; short x, y, w, h; const char *s; } BpCtl;   /* t: s static, e edit, g group, l line, c check, b bitmap */
extern const BpCtl BP_PG4000[], BP_PG4001[], BP_PG4002[], BP_PG4006[];
void bp_num(char *o, size_t n, double v);
void bp_time(char *o, size_t n, double s);
const char *bp_det_val(int id, char *b);
const BpCtl *bp_det_page(void);
int bp_info_refresh(void);
const char *bp_info_val(int id, char *b);
int bp_page_hidden(int id, int hide_cycle);
void bp_status_text(char *o, size_t n);
void bp_dump(void);
int cui_bp_script(const char *st, const char *arg);

/* ------------------------------------------------------------------------------------------------ Verify (cui_verify.c) */
extern int vf_on, vf_newwin, vf_playing, vf_pos, vf_dirty;
/* the Mastercam Simulator's player (formats/simulator.md 4): job modes (MachineSimulationPlayer SimulationParameters.JobType) */
enum { VF_RUN_NONE = 0, VF_RUN_PLAY = 1, VF_RUN_SINGLE_OP = 2, VF_RUN_FASTFORWARD = 3, VF_RUN_STEP = 4 };
/* stop condition bits (SimulationStateChangedEventArgs.StopCondition, the ones our motions can raise) */
enum { VF_STOP_OPCHANGE = 1, VF_STOP_TOOLCHANGE = 2, VF_STOP_XCHANGE = 4, VF_STOP_YCHANGE = 8, VF_STOP_ZCHANGE = 16 };
enum { VF_SPEED_MIN = 0, VF_SPEED_MAX = 20 };            /* UBVS.Regulator: base Regulator(0, 20, 10) */
enum { VF_KEY_HOME = 1001, VF_KEY_SPACE = 1002 };
#define VF_SETTINGS_FILE "MastercamSimulatorDefaults.xml"   /* McamPathInfo.XmlSettingsFileName */
#define VF_SCHEME_MAX 32                                 /* ours: the most Scheme colours read */
#define VF_CMP_MAX 30                                    /* CompareWindowViewModel StepsMaximum */
typedef struct {                                         /* mastercamsimulatordefaults.x (the keys we use) */
    int stop_enabled, stop_opchange, stop_toolchange, stop_collision, stop_toolinsp, stop_dx, stop_dy, stop_dz;
    int speed;                                           /* PerformanceIndexVerification (0..20) */
    int tool_vis, stock_vis, initial_stock_vis, workpiece_vis, toolpath_vis, fixture_vis, wire_vis, gnomon_vis, axes_vis;
    unsigned char stock_rgb[3], flute_rgb[3], holder_rgb[3];
    int precision, op_mode, tp_segment, show_rapid, show_points, focus, viewports;
    int loop;                                            /* LoopSimulation (the player's, not in the file) */
    double stock_to_leave;                               /* CustomSettings StockToLeave (0) */
    int cmp_n; double cmp_end[VF_CMP_MAX]; unsigned char cmp_rgb[VF_CMP_MAX][3];   /* CustomSettings StockCompareColors (7) */
    int tol_metric;                                      /* CustomSettings ToleranceUnit: 0 Imperial (the default), 1 Metric */
    int color_loop;                                      /* CustomSettings IsColorLoop (false) */
    int loop_by_tool;                                    /* CustomSettings drawMode: 0 Operation (the default), 1 Toolnumber */
    int scheme_n; unsigned char scheme_rgb[VF_SCHEME_MAX][3];   /* CustomSettings Scheme (12 colours; [0] Yellow = the cut colour) */
    char source[1100];                                   /* the file read, or "built-in" */
} VfSimSettings;
extern VfSimSettings vf_set; extern int vf_run, vf_last_stop;
void vf_settings_load(void);
int vf_settings_read(const char *path);                  /* "Read from Defaults": 1 = read */
void vf_apply_visibility(void);
int vf_set_named(const char *name_eq_value);
void vf_job(int mode);
void vf_prev_op(void);
void vf_slider_set(int v);
double vf_moves_per_second(void); double vf_frames_per_second(void);
#define VF_MI_N 32
int vf_moveinfo(char out[][2][128]);                     /* the Move List rows (label, value) */
extern VF *vf; extern NcMotion *vfm; extern int vfnm;
void vf_seek(int target);
void vf_push_mesh(void);
void vf_stop(void);
void vf_start(void);
void vf_advance(void);
void vf_sync_tool(void);
void vf_frame(void);
void vf_frame_still(void);
typedef struct { float x; const char *img, *tip; int id; } VfBtn;
extern const VfBtn VF_BTNS[8];
void vf_btn(int id);
void vf_keys(int key);
void vf_slider_seek(double f);
double vf_slider_pos(void);
int vf_status(char *l1, size_t n1, char *l2, size_t n2);
void vf_dump(void);
int cui_vf_script(const char *st, const char *arg);
/* the Simulator's Compare tool window (twStockCompare; formats/simulator.md 8) */
enum { VF_CMP_BROWSE = 0, VF_CMP_WORKPIECE = 1, VF_CMP_STL = 2 };
typedef struct { int kind; char path[1100]; } VfCmpItem;
extern int vf_cmp_open, vf_cmp_nitems, vf_cmp_sel, vf_cmp_need_refresh, vf_cmp_applied;
extern VfCmpItem vf_cmp_items[16];
void vf_cmp_show(int on);                                /* ShowStockCompare */
int vf_cmp_select(int i);                                /* a combo item (not Browse); 1 = target set */
int vf_cmp_add_stl(const char *path);                    /* Browse... -> the chosen file; 0 = unreadable */
int vf_cmp_can_refresh(void);
void vf_cmp_refresh(void);                               /* RefreshCommand */
void vf_cmp_set_stl(double v);                           /* StockToLeave */
void vf_cmp_options(double tol);                         /* the Compare window's OK (Tolerance) */
void vf_cmp_set_value(int row, int start, double v);     /* the grid's Start / End of a row (row 0 = the top = the last item) */
void vf_cmp_set_color(int row, const unsigned char rgb[3]);
void vf_cmp_row(int row, char *start, size_t ns, char *end, size_t ne, unsigned char rgb[3]);
void vf_cmp_dump(void);

/* ------------------------------------------------------------------------------------------------ Analyze + pickers (cui_analyze.c) */
extern int cdlg_open, cdlg_new, cdlg_cur, cdlg_prev, cdlg_select_ok, cdlg_picking; extern char cdlg_txt[8];
void color_dialog(int cur, int allow_select, void (*done)(int));
void color_dialog_pick_ent(int e);
void cdlg_commit(void);
void cdlg_click(int i);
void cdlg_select(void);
void cdlg_close(int ok);
extern int ldlg_open, ldlg_new, ldlg_cur, ldlg_radio, ldlg_lo, ldlg_hi, ldlg_picking; extern char ldlg_txt[16], ldlg_lo_t[16], ldlg_hi_t[16];
void level_dialog(int cur, void (*done)(int));
void level_dialog_pick_ent(int e);
int ldlg_rows(int *rows, int cap);
void ldlg_click(int level);
void ldlg_commit(void);
void ldlg_range_commit(int hi);
void ldlg_select(void);
void ldlg_close(int ok);
extern const BsCtl LDLG_563[], CDLG_593[];
extern const BsCtl CLDLG_516[], ADLG_192[];       /* Change Levels 129 x 175, Attributes 129 x 324 (cui_attr.c) */
extern const BsCtl AZ_1073[], AZ_1080[], AZ_6470[], AZ_6494[];
extern int az_sol;                                 /* the part solid of Analyze's solid dialog (6494), -1 none */
void az_start_solid(int i);
void analyze_dblclick(int e, int sol);             /* a double-click in the graphics window, no command running */
extern int az_list[512], az_n, az_i, az_ent, az_pick, az_changed, az_open_new, analyze_pick;
#define AZ_NIDS 30
extern char az_txt[AZ_NIDS][260]; extern const int AZ_IDS[AZ_NIDS];
int az_slot(int id);
void az_fill(void);
void az_apply(void);
void az_close(int commit);
void az_step(int dir);
void az_start(int picked);
void analyze_start(void);
int analyze_pick_ent(int e);
void az_propagate(void);
void az_autocursor_pick(double x, double y, double z);
const BsCtl *az_template(int *n, int *hdlg, const char **title);
int az_single(void);
int az_hidden(int id);
int az_ctl_dy_px(const BsCtl *k);   /* slot 118: the control's move in px (one entity) */
int az_dlg_dh_px(void);                 /* slot 118: the window's height change in px */
int az_readonly(int id);
int az_button_bmp(int id, const char **mod);
int az_static_bmp(const char *text, const char **mod);
int az_live(int id);
void az_act(int act);
void az_dump(void);
int cui_az_script(const char *st, const char *arg);

/* ------------------------------------------------------------------------------------------------ Edit Common Parameters (cui_ecp.c) */
#include "cam_fileop.h"
enum { EF_TOOL, EF_HEAD, EF_FEED, EF_PLUNGE, EF_LOFS, EF_DOFS, EF_RRATE, EF_RPM, EF_CLR, EF_RET, EF_FP, EF_TOP, EF_DEP, EF_N };
enum { ECK_RAPID = 100, ECK_FTC, ECK_BATCH, ECK_DIR, ECK_TOOLSEL };
enum { ECC_CLR_USE = 1, ECC_CLR_WHEN, ECC_CLR_INC, ECC_RET_USE, ECC_RET_INC, ECC_FP_INC, ECC_TOP_INC, ECC_DEP_INC, ECC_RAPID, ECC_FTC, ECC_BATCH, ECC_DIR };
enum { ECR_STATIC, ECR_GROUP, ECR_CHECK, ECR_EDIT, ECR_COMBO, ECR_BTN, ECR_LIST };
typedef struct { unsigned id; int role, f, x, y, w, h; const char *text; } EcpCtl;   /* f: field / ECK_ / ECC_ (-1 = greyed, not done) */
extern const EcpCtl ECP_243[];
extern int show_ecp, ecp_new, ecp_list, ecp_tool, ecp_on_toolsel;
extern CamCommonEdit ecp; extern char ecp_txt[EF_N][32];
extern const char *const ECP_USE_S[3], *const ECP_WHEN_S[3], *const ECP_AI_S[3], *const ECP_RAPID_S[2], *const ECP_FTC_S[2], *const ECP_BATCH_S[2], *const ECP_DIR_S[2];
void ecp_real_str(char *b, size_t n, double v);
int ecp_is_long(int k);
void ecp_open(void);
void ecp_propagate(int i);
int *ecp_on_ptr(int k);
int *ecp_check_ptr(int f);
int ecp_valid(void);
void ecp_apply(void);
void ecp_ok(void);
void ecp_cancel(void);
void ecp_all(int on);
int ecp_combo(int c, const char *const **items, int **val, int **on);
void ecp_op_row(int i, char *t, size_t n);
extern TmColSet ecp_cs[3];                       /* N13-32: the tool list 395's CMillToolListCtrl column sets (cui_ecp.c) */
void ecp_sortlist(int c);                        /* LVN_COLUMNCLICK on 395: 0x180143b10 -> SortList(c) */
int ecp_tool_cols(int *cols, int *w);            /* its visible columns >= 2 in order and their widths */
int ecp_tool_rows(int *idx, int cap);            /* the part tools in its order */
void ecp_button(int id);
void ecp_dump(void);
int cui_ecp_script(const char *st, const char *arg);
/* the new Lathe Face command + its dialog (cui_lface.c; mclathe lathe_face 0x1801a3290, templates 3911 / 5361; formats/lathe.md 8) */
enum { LFR_STATIC = 1, LFR_GROUP, LFR_LIST, LFR_NA, LFR_NA_CHECK, LFR_NA_RADIO, LFR_NA_COMBO, LFR_I32, LFR_I16, LFR_F64, LFR_FEED, LFR_FEEDU,
       LFR_FFEEDU, LFR_RPM, LFR_CSS, LFR_RPMR, LFR_CHECK, LFR_COMMENT, LFR_SRCRADIO, LFR_BTN, LFR_FINZ, LFR_ROUGH, LFR_FINISH, LFR_AWAY,
       LFR_COMBO_DIR, LFR_COMBO_ROLL };
typedef struct { int page; unsigned id; int role; uint32_t off; int x, y, w, h; const char *text; } LfCtl;   /* page 0 tool (3911), 1 Face (5361) */
extern const LfCtl LF_CTLS[];
extern int show_lface, lface_page;
int lface_can_start(char *why, size_t n);
void lface_open(void); void lface_cancel(void); int lface_ok(void); void lface_set_page(int pg); void lface_pick_tool(int row);
int lface_tool_rows(void); int lface_tool_row(void); void lface_tool_cell(int row, int col, char *t, size_t n); const char *lface_tool_col_name(int col);
int lface_ctl_enabled(const LfCtl *c); int lface_ctl_checked(const LfCtl *c); void lface_ctl_text(const LfCtl *c, char *t, size_t n);
int lface_combo(const LfCtl *c, const char *const **items); int lface_combo_index(const LfCtl *c); int lface_ctl_set(const LfCtl *c, const char *val);
void lface_dump(void);
int lface_ctl_italic(const LfCtl *c);
extern int cui_script_check;            /* scripts: the check box state message_check answers with (step lfwarncheck:0|1) */   /* the field is shown in italics (a machine check changed it, L-12) */
int cui_lface_script(const char *st, const char *arg);

/* ------------------------------------------------------------------------------------------------ misc (cui_misc.c) */
extern int show_gridset; extern char gs_txt[5][32]; extern int gs_inited;
typedef struct { unsigned id; char t; int x, y, w, h, k; const char *text; } GsCtl;   /* t: g group, s static, e edit (k = field), r radio (k = snap mode), b button */
extern const GsCtl GS_1589[];
double *gs_field(int k);
void gs_fill(void);
void gs_edit(int k);
void gs_close(void);
typedef struct { int act; char title[64], l[2][64], v[2][270]; int nf; } AskBox;
extern AskBox ask;
void ask_open(int act, const char *title, const char *l1, const char *v1, const char *l2, const char *v2);
void ask_ok(void);
void ask_end(int commit);
void grp_rename_begin(void);
int grp_renaming(int group);
typedef struct { int type, level, color; V3 a, b, c; double r, a0, sw; int vw; double vm[9];
                 uint8_t *srec, *brec; uint32_t sn, bn; int blevel, bcolor; } Clip;   /* vw / vm: a view arc's view (Ent.vw); srec: a surface's record, brec: its base's (trimmed / offset) */
extern int nclip, paste_pending;
int copy_selected(void);
void paste_clip(void);
void edit_copy(void);
void edit_cut(void);
void edit_paste(void);
extern int zoom_window, zw_active, show_help, show_about, want_print, show_info;
void zoom_window_start(void);
void zoom_window_done(float x0, float y0, float x1, float y1, int vw, int vh);
extern const char *const ABOUT_TEXT;
void print_path(char *out, size_t n);
void print_done(const char *out, int ok);
void cui_cross_cursor(unsigned char data[128], unsigned char mask[128]);
const char *gal_tip(const char *key);
extern const BsCtl BS_105[], BS_107[], BS_111[], BS_115[], BS_129[], BS_130[], BS_140[];
enum { BS_CMD_NONE, BS_CMD_NEW, BS_CMD_SAVE, BS_CMD_CONFIG };
typedef struct { const char *text; int page; int icon; int cmd; } BsItem;
#define BS_NITEMS 15
extern const BsItem BS_ITEMS[BS_NITEMS];
int bs_count(const BsCtl *c);
const BsCtl *bs_page_ctls(int page, int *n);
int bs_live(int page, int id);
typedef struct { unsigned id; const char *t; } BsInfoVal;
int bs_info_values(BsInfoVal *v, int cap);
void misc_dump(void);
int cui_qat_load(char fn[16][64]);
int cui_misc_script(const char *st, const char *arg, int vw, int vh);   /* vw / vh = the graphics view (zoomwin) */

/* ------------------------------------------------------------------------------------------------ Stock Setup (cui_stock.c) */
/* Machine Group Properties > Stock Setup = mastercam.dll CGroupPg3, opmanres dialog 1590; the model is McamFile's stock_*
   fields (group_pg3 = op_group +0x1228). Roles: STR_GROUP group box, STR_GROUPCHK check box drawn as a group title, ... */
enum { STR_GROUP, STR_GROUPCHK, STR_RADIO, STR_CHECK, STR_BUTTON, STR_BMPBTN, STR_EDIT, STR_STATIC, STR_COMBO, STR_PICTURE, STR_CORNER, STR_HIDDEN,
       STR_EDITML, STR_LABEL, STR_ETCH };   /* + multi-line edit, a static showing a value (sh_text), an etched line (Files page) */
enum { ST_PICK_ORIGIN = 1, ST_PICK_C1, ST_PICK_C2, ST_PICK_BBOX, ST_PICK_SOLID, ST_PICK_PLANE };   /* the sheet is hidden meanwhile: BBOX the Bounding
                                                                      Box panel runs, SOLID the solid pick (528), PLANE the Plane Selection (960) */
typedef struct { unsigned id; int role, x, y, w, h; const char *text; } StCtl;
typedef struct { int shape, disp, fit, shaded, axis, center, mtree, corner, mask, solid;
                 double org[3], size[3], c1[3]; char org_t[3][32], size_t_[3][32], plane[64], file[0x104]; } StPage;
extern const StCtl ST_1590[]; extern const char *const ST_SHEET_TITLE; extern const char *const ST_TABS[3];
extern int show_stock_setup, st_page, st_pick, st_q2d, st_browse, cfg_stock_color, cfg_constr_3d;
#define cfg_curve_chord (mc_systol.curve_chord_dev)   /* 1006 Curve chordal deviation (McSysTol) */
extern StPage stp;
int stock_supported(void);
int stock_ok(void);
void stock_box(double lo[3], double hi[3]);
void stock_calc_lines(double L[12][6]);
int stock_display_lines(double L[12][6]);
int stock_top(double *z);
int stock_facing_box(double box[4], double *ztop);
int stock_facing_circle(double c[3], double *ztop);     /* cylinder along the Tplane axis: centre x, y and radius */
int stock_display_circles(double C[2][3], double *r, double M[9]);   /* cylinder end circles: centres (axis view), radius, view matrix */
int stock_axis_view(int axis, double M[9]);
void stock_calc_store(void);                             /* stock_calc_geom: F.stock_lines (+ the cylinder's end circles) */
void stock_w2v(const double w[3], double v[3]);          /* world -> the stock plane's view (world_to_view(p - origin)) */
void stock_v2w(const double v[3], double w[3]);          /* P3D::ViewToWorldAdd */
int stock_model(const float **v, const int **t);         /* CreateShadedStockModel: triangles (indices into v, 3 floats each) */
int stock_display_model(void);                           /* the model's triangles when the stock is displayed (else 0) */
int stock_display_shaded(void);                          /* the Solid display with a model (FUN_18033fe70) */
void stock_fit_iter(void (*acc)(void *u, double x, double y, double z), void *u);   /* FUN_180337650: the stock's points for Fit (Display + Fit screen) */
long stock_fit_points(void);                             /* their count */
void stock_free_all(void);                               /* the model and the shaded copy freed */
int stock_regen_mark(void);                              /* Mark2dHSTOpsRefMachStockDef4Regen */
void st_browse_done(const char *path);                   /* the STL browse dialog's result (NULL = cancelled) */
void st_plane_selected(int id);                          /* the stock plane's Plane Selection OK (FUN_1801f1d70) */
void st_plane_cancel(void);
int st_pick_solid_at(float mx, float my);                /* the part solid under the cursor (F.solids index, -1 none) */
void st_solid_select(int i);
void st_solid_click(float mx, float my);                 /* the solid pick's graphics click (window coordinates) */
void st_from_box_world(const double a[3], const double b[3]);
void st_open(int page);
void st_ok(void);
void st_cancel(void);
int st_page_live(void);
int st_shown(unsigned id);
int st_enabled(unsigned id);
int st_checked(unsigned id);
const StCtl *st_ctl(unsigned id);
char *st_text(unsigned id);
void st_edit(unsigned id, const char *t);
void st_click(unsigned id);
void st_corner(int k);
void st_q2d_answer(int yes);
void st_pick_click(double wx, double wy, double gz);
void st_pick_escape(void);
int st_point_pick(void);                                 /* a point pick is waiting (st_pick other than ST_PICK_BBOX) */
int stock_bbox_sel(int mask, const unsigned char *pick, int npick, double bb[6]);   /* make_bounding_box (pick NULL = all shown + solids) */
int stock_bcyl_sel(int mask, const unsigned char *pick, int npick, int axis, int center, double c[3], double dir[3], double *r, double *len);   /* FUN_18005bab0 */
void st_from_box(const double a[3], const double b[3]);  /* FUN_1801edd80 */
int st_bbox_panel_result(int rect, const double box[6], const double cyl[8], int axis_code, int center);
int st_picture_bmp(void);
int st_marker(int *button);
void st_dump(void);
int cui_stock_script(const char *st, const char *arg);
/* the sheet's pages 0 Files / 1 Tool Settings / 2 Stock Setup (cui_stock.c dispatches to cui_grpprops.c for 0 / 1) */
void st_set_page(int page);                              /* a tab clicked (the page becomes visited: its OnOK runs at OK) */
const StCtl *sh_table(int page);
const StCtl *sh_ctl(int page, unsigned id);
int sh_shown(int page, unsigned id);
int sh_enabled(int page, unsigned id);
int sh_checked(int page, unsigned id);
char *sh_text(int page, unsigned id);
void sh_edit(int page, unsigned id, const char *t);
void sh_click(int page, unsigned id);
int sh_bitmap(int page, unsigned id);                    /* the UIBitmapButton's opmanres bitmap (0 none) */
/* ---- Machine Group Properties > Files / Tool Settings (cui_grpprops.c) */
extern const StCtl GP_815[], GP_1589[];
void gp_open(void);
void gp_visit(int page);
int gp_visited(int page);
const StCtl *gp_table(int page);
const StCtl *gp_ctl(int page, unsigned id);
int gp_bitmap(int page, unsigned id);
int gp_shown(int page, unsigned id);
int gp_enabled(int page, unsigned id);
int gp_checked(int page, unsigned id);
char *gp_text(int page, unsigned id);
void gp_edit(int page, unsigned id, const char *t);
void gp_click(int page, unsigned id);
void gp_ok(void);
void gp_dump(void);
void grp_name_sync(void);                                /* the tree's machine group label = F.grp_name */
void grp_set_name(const char *nm);                       /* the tree's Rename of the machine group */
int grp_assign_seq(void);                                /* Assign tool numbers sequentially (group +0x11b8) */
const uint8_t *grp_modal_record(const uint8_t *base, uint32_t n, int new_opcode);   /* InitOperation's modal copy (NULL = none) */
void gp_write_project(FILE *f);
int gp_read_project_line(const char *line);

/* ------------------------------------------------------------------------------------------------ planes (cui_planes.c) */
const char *plane_name(int id);
/* planes made here (Bounding Box Create Tool Plane; cui_planes.c) and the active planes (index = PlaneType 0 Cplane, 1 Tplane,
   2 Gview, 3 WCS; plane ids, 1 = Top) */
#define UPLANE_MAX 256
typedef struct { int id; char name[64]; double m[9], org[3]; int wofs, part; int disp, coord; } UPlane;   /* part: from the part's MastercamPlanes archive (cui_planes.c planes_restore_part; not written as an UPLANE line); disp, coord: Plane +0x334 / +0x348 (Planes manager; coord -1 = NOT DECODED) */
extern UPlane uplanes[UPLANE_MAX]; extern int nuplanes; extern int plane_active[4]; extern int plane_active_org[4];
/* restored on open (cui_planes.c): the part's planes, active planes / origins, the construction depth, the Gview and the window; the active origin per type (GetActiveOrigin) */
void planes_restore_part(void); void planes_restore_view(void); int planes_project_active_differs(void); const double *plane_active_origin(int type); int plane_wofs(int id);
void plane_set_active(int type, int id); void plane_set_active_org(int type, int id);   /* SetActivePlane(type, plane): the plane and its origin (cui_planes.c) */
/* the Planes manager rows (PlanesMgrBar): count, cells of row i (Name / G / WCS / C / T / Display / Offset / Coordinate), the G column header */
int planes_mgr_rows(void); void planes_mgr_row(int i, char *cells[8], char *buf, int bufn); const char *planes_mgr_g_header(void);
int planes_add(const char *name, const double m[9], const double org[3]);   /* the new plane's id, 0 = not added */
void planes_clear_user(void);
const double *plane_origin(int id);
void planes_init_op(CamParams *p);                /* a new op's planes = the active planes (InitOperation) */
void planes_set_active(CamParams *p);              /* the active planes on a copied op (InitOperationViewsToActivePlanes) */
void plane_w2v(int id, const double org[3], const double w[3], double v[3]);
void plane_v2w(int id, const double org[3], const double v[3], double w[3]);
int op_tplane_is_wcs(const CamParams *p);
double op_chain_depth(const CamParams *p, const Chain *ch, int nch, const Hole *h, int nh);
int cui_active_tplane(void);                     /* the main window's active Tplane (cui_sldchain.c): Top, switching NOT PORTED */
int op_wofs(const CamParams *p);                 /* the stored work offset (Tplane op_view +0x92): Manual >= 0, else -1 */
void planes_map_to_op(CamParams *p);             /* MapToOp at OK: Manual without a value >= 0 becomes Automatic */
void planes_refresh(CamParams *p);               /* RefreshData / SetToolPlane: Manual = the Tplane's +0x92 >= 0 */
int upl_ok_enabled(void);                        /* Update Plane OK: only choice 2 is ported */
extern int pls_open, pls_which, pls_sel;         /* Plane Selection (842): open for WCS 0 / Tplane 1 / Cplane 2, the row */
extern int upl_open, upl_choice; extern char upl_text[240];   /* Update Plane (10269) */
#define PLS842_N 6
#define PLS842_W 242
#define PLS842_H 160
extern const TpCtl PLS842[PLS842_N];
#define UPL10269_N 8
#define UPL10269_W 230
#define UPL10269_H 137
extern const TpCtl UPL10269[UPL10269_N];
void upl_close(int ok);
void pls_begin(int which);
int pls_rows(void);
const char *pls_row(int i);
void pls_ok(void);
void pls_cancel(void);
void planes_copy(int id);
void planes_wofs(int manual, int v);
void planes_page_body(const PpOps *ops, void *u);
void planes_dump(void);
int cui_planes_script(const char *st, const char *arg);

/* ------------------------------------------------------------------------------------------------ Plane Selection (cui_plsel.c) */
/* mccore get_plane_info (0x180a2aea0) + its dialog CPlaneDlg (mccoreres 1085 "Plane Selection"): formats/plane_selection.md. The
   caller's plane (PLANE_sf, world): o = origin (+0), n = unit normal (+0x18), x = unit X direction in the plane (+0x30). psel_begin =
   get_plane_info(prompt, plane, &ok, NULL, res, 0, NULL) (nowait 1 = get_plane_info_without_waiting): PLANE is used as the plane on
   entry and written in place while the dialog runs; DONE(ok, plane, ctx) runs where get_plane_info returns (ok 0: Cancel / Esc / no
   valid plane - the plane is then the one on entry, or whatever a failed pick left). One at a time. */
typedef struct { double o[3], n[3], x[3]; } PlaneSf;
typedef void (*PselDone)(int ok, PlaneSf *plane, void *ctx);
void psel_begin(const char *prompt, PlaneSf *plane, PselDone done, void *ctx, int nowait);   /* prompt NULL = mccoreres 7436 "Select Plane" */
extern int psel_open;                                 /* the dialog runs (callers' panels / picks wait) */
extern int psel_cue, psel_named, psel_upd, psel_flip; /* the X / Y / Z point pick (dialog suspended), the plane list (842) over it, UpdateData(FALSE) count, the Flip flag */
extern double psel_val[3]; extern char psel_text[3][64];   /* the X / Y / Z edits: MCReal values, texts */
int psel_ctl(int id);                                 /* a control of 1085 by its id (OK 1, Cancel 2, 4466.., 1258..): 1 = taken */
void psel_set_text(int axis, const char *t);          /* typed into edit 964 + AXIS */
void psel_edit_done(int axis);                        /* its EN_KILLFOCUS / <Enter> */
void psel_esc(void);                                  /* get_event returns 1 */
int psel_click(float mx, float my, const double w[3], const double ws[3], int snapped);   /* a graphics click (W under the cursor, WS the AutoCursor point, SNAPPED: it snapped); 1 = taken */
void psel_spin(int axis, int delta);                  /* the edit's spin buddy (UDN_DELTAPOS iDelta) */
extern char psel_errtip[160]; extern int psel_errtip_axis;   /* UIEditReal::DisplayErrorTip on edit 964 + axis ("" none) */
void psel_named_answer(int plane_id);                 /* SelectPlaneFromList's answer (0 = Cancel; cui_planes.c pls_which 4) */
const char *psel_prompt(void);
void psel_overlay(float **pv, int *pn, int *pcap);    /* draw_plane's lines */
void psel_dump(void);
int cui_psel_script(const char *st, const char *arg);
enum { PSK_GROUP, PSK_EDIT, PSK_BMP, PSK_BTN };
typedef struct { int kind, id, x, y, w, h; const char *text; int bmp; int ref; } PselCtl;   /* bmp: mccoreres bitmap (> 0), -N: opmanres bitmap N (GetMastercamResourceHandle); ref -1 greyed */
#define PSEL1085_N 22
#define PSEL1085_W 143
#define PSEL1085_H 268
extern const PselCtl PSEL1085[PSEL1085_N];

/* ------------------------------------------------------------------------------------------------ Transform > Geometry Nesting (cui_nest.c) */
/* NESTING.DLL's geometry flow (formats/nesting_addin.md): the property sheet CNestPropSht (Sheets 101 / Parts 105 pages), the
   Configuration dialog CCfgDlg (201), the Nesting Results dialog CResultDlg (205); the templates are mcnestingres.dll's (control
   rectangles in dialog units, roles from the control styles). The front end draws them from these tables and the ns_* model. */
enum { NSR_STATIC, NSR_GROUP, NSR_LIST, NSR_EDIT, NSR_COMBO, NSR_DROP, NSR_CHECK, NSR_RADIO, NSR_BUTTON, NSR_BMPBTN, NSR_OWNER,
       NSR_SWATCH, NSR_VALUE };   /* combo = editable (CBS_DROPDOWN), drop = CBS_DROPDOWNLIST, value = a static showing a value */
typedef struct { unsigned id; int role, x, y, w, h; const char *text; } NsCtl;
#define NS_SHEET 0                                /* the property sheet's own controls (title, page tabs, bitmap buttons, OK / Cancel / Help) */
const NsCtl *ns_table(int tpl, int *n, int *w, int *h);   /* tpl 0 / 101 / 105 / 201 / 205 */
extern int ns_open, ns_page, ns_cfg_open, ns_res_open, ns_chaining;   /* the sheet is up (page 0 Sheets / 1 Parts); Configuration; Results; chain_it for 1 parts / 2 sheets */
extern int ns_deleting;                           /* the Results Delete pick loop runs (FUN_180087fb0): the Results dialog is suspended */
void ns_del_pick(int e);                          /* its get_event event 2: the picked entity (-1 = none) */
void ns_del_end(void);                            /* get_event returned 0 (Esc): the loop ends */
void ns_del_window(float x0, float y0, float x1, float y1);   /* its get_event event 0x20: the window (screen corners, vp_* set) */
int ns_shown(int tpl, unsigned id);
int ns_enabled(int tpl, unsigned id);
int ns_checked(int tpl, unsigned id);
const char *ns_text(int tpl, unsigned id);        /* an edit's / value static's text, a combo's edit text */
int ns_items(int tpl, unsigned id, const char ***items);   /* a list's / combo's items; returns the count */
int ns_cursel(int tpl, unsigned id);              /* the selected item (-1 none) */
const char *ns_tip(int tpl, unsigned id);         /* a bitmap button's tooltip */
void ns_edit(int tpl, unsigned id, const char *v);   /* an edit / combo text committed (kill focus / Enter) */
void ns_select(int tpl, unsigned id, int i);      /* a list / combo selection */
void ns_click(int tpl, unsigned id);              /* a button / check box / radio */
const char *const *ns_menu(int tpl, unsigned id, const int **cmds, int *n);   /* a list's right-click menu (NULL = none); "" = separator */
int ns_menu_enabled(int tpl, int cmd);
void ns_menu_cmd(int tpl, int cmd);               /* a menu command (40005 Add rectangular ...) */
void ns_start(void);                              /* Transform > Geometry Nesting (CO do_nest_g -> NE m_nest_g) */
void ns_chains_done(void);                        /* the Chaining dialog's OK while ns_chaining (from geo_chains_done) */
void ns_chain_end(void);                          /* chain_it cancelled while ns_chaining (from geo_end) */
void ns_dump(void);                               /* tests: the session (sheets, parts, results) */
int cui_ns_script(const char *st, const char *arg);   /* ours: the nest* script steps (cui_nest.c), 0 = not one */

#ifdef __cplusplus
}
#endif
#endif
