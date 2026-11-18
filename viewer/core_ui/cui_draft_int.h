/* cui_draft_int.h - the state of the dimension command loop (cui_draft.c) shared with the other Dimension-group commands
 * (cui_draft_dim.c: Circular). Not a public API. */
#ifndef CUI_DRAFT_INT_H
#define CUI_DRAFT_INT_H
#include <stdint.h>
#include "draft_create.h"
#include "core_ui.h"

extern DcSettings dr_S, dr_saved;                       /* the session's drafting globals and DraftGlobalsSave's copy */
extern int dr_assoc;                                    /* DfAssoc (config 15003) */
extern int dr_npick, dr_placing;                        /* DAT_1806960e8 picks; placement (a drag entity being computed) */
extern uint8_t dr_drag[0x810]; extern int dr_drag_ok;  /* drag_ent, GetDfDragEntValid */
extern int dr_Pent[2];                                  /* the entities the stored record's aPoints reference (F.ents index, -1) */
extern int dr_live;                                     /* F.dnew index of the live entity, -1 */
extern double dr_view[9];                               /* the active view's rows */
int dr_view_no(void);
void dr_globals_load(void);
const DcFonts *dr_fonts_get(void);
void dr_scene(void);
void dr_store(void);                                    /* FUN_180043280-style store of dr_drag (UndoTrans, live entity) */
void dr_commit_live(void);
void dr_sketched(float mx, float my, const double w[3], double out[3]);   /* df_calc_sketched_point */
void dr_ac_apoint(uint8_t ap[0x98], int *ent, V3 p);   /* the AutoCursor point's aPoint (FUN_1804e1bb0 kinds P / 1 / 2, else K) */
int dr_pick_line(float mx, float my);                  /* the line under the cursor (select_ent_univ's rule) */

/* cui_draft_dim.c */
int dd_cmd(int c);
const char *dd_label(int c);
int dd_find_ft(const char *ft);
void dd_prompt(void);
void dd_restart(void);
int dd_move(const double world[3], int ctrl);
int dd_click(float mx, float my, const double w[3], int ctrl);
int dd_need(void);
void dd_panel_body(const CuiPanelOps *ops, void *u);
void dd_panel_sync(void);
int dd_script(const char *st, const char *arg);
int dd_esc(void);                                       /* Esc: 1 handled, 0 the command ends, -1 cui_draft.c dr_esc decides */
int dd_button(int code);                                /* panel code: as dd_esc */
int dd_pick_arc_any(float mx, float my);                /* the XY-or-other arc under the cursor (pick_entity's rule over the arcs) */
int dr_l_ints(const double *L1, const double *L2, double *pt);   /* mccore l_ints (draft_create.c dcx_l_ints) */
/* cui_draft_pt.c */
void dp4_prompt(void);
void dp4_restart(void);
int dp4_need(void);
int dp4_move(const double world[3], int ctrl);
int dp4_click(float mx, float my, const double w[3], int ctrl);
void dp4_panel_body(const CuiPanelOps *ops, void *u);
void dp4_panel_sync(void);
int dp4_script(const char *st, const char *arg);
/* cui_draft_tan.c */
void dt_prompt(void);
void dt_restart(void);
int dt_need(void);
int dt_move(const double world[3], int ctrl);
int dt_click(float mx, float my, const double w[3], int ctrl);
void dt_panel_body(const CuiPanelOps *ops, void *u);
void dt_panel_sync(void);
int dt_script(const char *st, const char *arg);
/* cui_draft_base.c */
void db_prompt(void);
void db_restart(void);
int db_need(void);
int db_click(float mx, float my, const double w[3], int ctrl);
int db_script(const char *st, const char *arg);
int db_move(const double world[3], int ctrl);
int db_esc(void);
int db_button(int code);
/* cui_draft_ang.c */
void da_prompt(void);
void da_restart(void);
int da_need(void);
int da_move(const double world[3], int ctrl);
int da_click(float mx, float my, const double w[3], int ctrl);
void da_panel_body(const CuiPanelOps *ops, void *u);
void da_panel_sync(void);
int da_script(const char *st, const char *arg);
#endif
