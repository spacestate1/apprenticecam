/* cui_attr.c - Home > Attributes / Organize: the selected entities' colour, line style, point style and line width, the main
 * attributes they fall back to, the default wireframe / surface / solid colours, Change Level and Set All (formats/home_tab_plan.md).
 * Sources: mastercam.dll (~/mastercam-re/ghidra mc2017; dumps decomp/home/attr.c, attr2.c, bmpcombo.c):
 *   ChangeSelectedEntityLineStyle @18046d210 -> FUN_1804670d0(1, 0x1f078e, v, main_attrib + 8)
 *   ChangeSelectedEntityLineWidth @18046d240 -> FUN_180467430(2, 0x1f1ffe, v, main_attrib + 0)
 *   ChangeSelectedEntityPointStyle @18046d270 -> FUN_1804670d0(3, 0x1f1fff, v, main_attrib + 0xc)
 *   ChangeSelected{Wireframe,Surfaces,Solids}Color @18046d3c0 / 18046d330 / 18046d2a0 -> FUN_180469e70(0 / 1 / 3, 0x1f1fff, colour):
 *     the colour table's default of that kind, then FUN_180466d70(0, 0x1f1fff, colour, ..) on the selection
 *   FUN_1804670d0 / FUN_180467430 / FUN_180466d70: every selected entity (rewind_dl_db(2, mask)) whose value (FUN_18046a280: kind 0
 *     colour ent +6, 1 line style +0x18, 2 width (int) of the double +0x10, 3 point style +0x1c) differs gets it (FUN_18046a420: kinds
 *     1 and 3 write BOTH +0x18 and +0x1c), one undo transaction; no entity selected -> the main attribute takes the value; then the
 *     selection is cleared (set_clr_sel_bits(0, 0xe, ..)).
 * The entity types of the masks: 0x1f078e for the line style (not points / text ...): here every curve and surface (type masks NOT
 * DECODED in detail: E_POINT excluded from the line style, ours). Solids: not part of this program's entity selection (their colour is
 * not changed here). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core_ui.h"
#include "scene.h"
extern int srf_front_color;                       /* cui_surf.c */

int cfg_ln_style = 1; double cfg_ln_width = 1;   /* config 10000 "Line style?" / 10001 "Line width?" = main_attrib +8 / +0 */
/* solidColor (mastercam.dll .data 0x18087b86d, 156 before the configuration) = config 5052: scene.c cfg_solid_color */

/* one undo step of attribute changes: per entity its values before / after (cui_undo.c U_ATTR) */
typedef struct { int i; int color, level, ln, pt; double w; } AttrVal;
typedef struct { int n; AttrVal *a, *b; } AttrUndo;

static void attr_get(int i, AttrVal *v) { const Ent *e = &F.ents[i]; v->i = i; v->color = e->color; v->level = e->level; v->ln = e->ln_style; v->pt = e->pt_style; v->w = e->width; }
static void attr_put(const AttrVal *v) {
    Ent *e = &F.ents[v->i]; if (!e->type) return;
    e->color = v->color; e->level = v->level; e->ln_style = v->ln; e->pt_style = v->pt; e->width = v->w;
    mcam_level_store(&F, v->level);                        /* write_ent: FUN_1802c4010 (mcam_load.h) */
    if (!e->created) e->attr_edited = 1;
    for (int k = e->first_poly; k < e->first_poly + e->npoly && k < F.npolys; k++) {
        F.polys[k].color = (uint8_t)v->color; F.polys[k].level = v->level; F.polys[k].ln_style = (uint8_t)v->ln; F.polys[k].width = (uint8_t)(v->w >= 1 ? v->w : 1); }
    if (e->type == E_SURF && e->srf >= 0 && e->srf < F.nsurfs) {   /* the SURF_ID record's header (rewritten by the save when edited) */
        Surf *s = &F.surfs[e->srf]; s->color = v->color; s->level = v->level;
        if (s->rec && s->n >= 0x20) { s->rec[6] = (uint8_t)v->color; memcpy(s->rec + 8, &v->level, 4); memcpy(s->rec + 0x10, &v->w, 8);
            uint16_t ln = (uint16_t)v->ln, pt = (uint16_t)v->pt; memcpy(s->rec + 0x18, &ln, 2); memcpy(s->rec + 0x1c, &pt, 2); }
        s->edited = 1;
    }
}
void attr_undo_apply(void *p, int undo) {
    AttrUndo *u = p; if (!u) return;
    for (int k = 0; k < u->n; k++) attr_put(undo ? &u->a[k] : &u->b[k]);
    scene_surfs_update(0);
}
void attr_undo_free(void *p) { AttrUndo *u = p; if (!u) return; free(u->a); free(u->b); free(u); }

/* the edit: SET changes one entity's value; returns 1 when the entity changed */
typedef int (*AttrSet)(Ent *e, AttrVal *v, int val, double dval);
static int set_color(Ent *e, AttrVal *v, int c, double d) { (void)e; (void)d; if (v->color == c) return 0; v->color = c; return 1; }
static int set_level(Ent *e, AttrVal *v, int l, double d) { (void)e; (void)d; if (v->level == l) return 0; v->level = l; return 1; }
static int set_lstyle(Ent *e, AttrVal *v, int s, double d) { (void)d; if (e->type == E_POINT || e->type == E_NOTE) return 0; if (v->ln == s) return 0; v->ln = v->pt = s; return 1; }   /* kind 1: compares +0x18, writes +0x18 and +0x1c; mask 0x1f078e has no points / notes (0x10 / 0x20) */
static int set_pstyle(Ent *e, AttrVal *v, int s, double d) { (void)e; (void)d; if (v->pt == s) return 0; v->pt = v->ln = s; return 1; }   /* kind 3: compares +0x1c, writes both (FUN_18046a420, as decoded) */
static int set_width(Ent *e, AttrVal *v, int w, double d) { (void)e; (void)d; if ((int)v->w == w) return 0; v->w = w; return 1; }   /* kind 2: (int) compare, (double) store */

/* apply to the selected entities (one undo step); returns the count changed, -1 when nothing is selected */
static int attr_apply_sel(AttrSet fn, int val, double dval) {
    int nsel = 0, n = 0; AttrUndo *u = calloc(1, sizeof *u);
    for (int i = 0; ent_sel && i < ent_sel_n && i < F.nents; i++) if (ent_sel[i] && F.ents[i].type) {
        nsel++;
        AttrVal a, b; attr_get(i, &a); b = a;
        if (!fn(&F.ents[i], &b, val, dval)) continue;
        u->a = realloc(u->a, sizeof(AttrVal) * (u->n + 1)); u->b = realloc(u->b, sizeof(AttrVal) * (u->n + 1));
        u->a[u->n] = a; u->b[u->n] = b; u->n++; attr_put(&b); n++;
    }
    if (n) { URec r; memset(&r, 0, sizeof r); r.kind = U_ATTR; r.ax = u; undo_push(r); doc_dirty = 1; scene_reupload(); scene_surfs_update(0); }
    else attr_undo_free(u);
    if (nsel) sel_clear();                                 /* set_clr_sel_bits(0, 0xe, ..) */
    overlay_dirty = 1;
    return nsel ? n : -1;
}
/* FUN_1804670d0 kind 1 / FUN_180467430 kind 2 / FUN_1804670d0 kind 3; v = -1: the main attribute's own value */
void attr_line_style(int v) { if (v == -1) v = cfg_ln_style; if (v < 1) return; if (attr_apply_sel(set_lstyle, v, 0) < 0) cfg_ln_style = v; }
void attr_line_width(int v) { if (v == -1) v = (int)cfg_ln_width; if (v < 1) return; if (attr_apply_sel(set_width, v, 0) < 0) cfg_ln_width = v; }
void attr_point_style(int v) { if (v == -1) v = cfg_pt_style; if (v < 1) return; if (attr_apply_sel(set_pstyle, v, 0) < 0) cfg_pt_style = v; }
/* FUN_180469e70(kind, mask, colour): the default of the kind (0 wireframe = main_color, 1 surfaces = surfaceFrontColor, 3 solids =
   solidColor), then the selection's colour (every selected entity, whatever its type - the mask is 0x1f1fff for all three) */
void attr_default_color(int kind, int c) {
    if (c < 0 || c > 255) return;
    if (kind == 0) geo_color = c; else if (kind == 1) srf_front_color = c; else if (kind == 3) cfg_solid_color = c;
    attr_apply_sel(set_color, c, 0);
}
int attr_selected(void) { int n = 0; for (int i = 0; ent_sel && i < ent_sel_n && i < F.nents; i++) if (ent_sel[i] && F.ents[i].type) n++; return n; }
/* Change Level / Set All's level (ChangeEntityLevel @18046c210 Move: the selected entities' ent +8; the Copy mode = translate_silent
   copies, NOT PORTED here) */
int attr_set_level(int level) { if (level < 1) return 0; int n = attr_apply_sel(set_level, level, 0); return n < 0 ? 0 : n; }
/* Set All (ChangeEntityAttribs @18046bc80 -> CAttribs::UpdateEnt): each field the dialog checks; the dialog's Force visibility, Surface
   Density, EA Mgr and Same as... are NOT PORTED */
int attr_set_all(int use_color, int color, int use_ln, int ln, int use_pt, int pt, int use_level, int level, int use_w, int w) {
    int nsel = 0, n = 0; AttrUndo *u = calloc(1, sizeof *u);
    for (int i = 0; ent_sel && i < ent_sel_n && i < F.nents; i++) if (ent_sel[i] && F.ents[i].type) {
        nsel++; Ent *e = &F.ents[i];
        AttrVal a, b; attr_get(i, &a); b = a; int ch = 0;
        /* CAttribs::UpdateEnt @1800e8670: colour +6; level +8; point style (points only, ent type & 1) -> +0x1c and +0x18; width (types
           0x1f1ffe, not points) -> +0x10; line style (types 0x1f078e, not points) -> +0x18 only */
        if (use_color) ch |= set_color(e, &b, color, 0);
        if (use_level) ch |= set_level(e, &b, level, 0);
        if (use_pt && e->type == E_POINT && (b.pt != pt || b.ln != pt)) { b.pt = b.ln = pt; ch = 1; }
        if (use_w && e->type != E_POINT && (int)b.w != w) { b.w = w; ch = 1; }
        if (use_ln && e->type != E_POINT && b.ln != ln) { b.ln = ln; ch = 1; }
        if (!ch) continue;
        u->a = realloc(u->a, sizeof(AttrVal) * (u->n + 1)); u->b = realloc(u->b, sizeof(AttrVal) * (u->n + 1));
        u->a[u->n] = a; u->b[u->n] = b; u->n++; attr_put(&b); n++;
    }
    if (n) { URec r; memset(&r, 0, sizeof r); r.kind = U_ATTR; r.ax = u; undo_push(r); doc_dirty = 1; scene_reupload(); scene_surfs_update(0); }
    else attr_undo_free(u);
    if (nsel) sel_clear();
    overlay_dirty = 1;
    return n;
}
/* a new entity made here takes the main attributes (main_attrib: style, width; the point style for points) */
void attr_new_ent(int i) {
    if (i < 0 || i >= F.nents) return; Ent *e = &F.ents[i];
    e->ln_style = e->type == E_POINT ? 1 : cfg_ln_style; e->width = cfg_ln_width >= 1 ? cfg_ln_width : 1;
    if (e->type == E_POINT) e->pt_style = cfg_pt_style;
    for (int k = e->first_poly; k < e->first_poly + e->npoly && k < F.npolys; k++) { F.polys[k].ln_style = (uint8_t)e->ln_style; F.polys[k].width = (uint8_t)e->width; }
}
/* the ribbon / mini toolbar colour buttons (ChangeSelected*ColorWithPicker -> FUN_180469f20): the Colors dialog on the kind's default;
   OK applies FUN_180469e70 (the default + the selection). Mastercam with a selection opens the picker in a live-preview mode
   (FUN_1804686f0 / FUN_180469a80, not read): here the same dialog, applied on OK (ours) */
static void cdone0(int c) { attr_default_color(0, c); }
static void cdone1(int c) { attr_default_color(1, c); }
static void cdone3(int c) { attr_default_color(3, c); }
void attr_color_cmd(int kind) {
    int cur = kind == 0 ? geo_color : kind == 1 ? srf_front_color : cfg_solid_color;
    color_dialog(cur, 1, kind == 0 ? cdone0 : kind == 1 ? cdone1 : cdone3);
}

/* ---------------------------------------------------------------------------------------------------------------- Organize: Z depth */
/* the construction depth (mccore ConstrDepth constr_depth): new geometry's Z in the C-plane - a free click lands on the C-plane at this
   depth; with 2D construction (constr_view_is_3d off, Home > 2D / 3D = OnScreen2D3DToggle @180470200 toggling it) every point is put at
   it, snapped ones too (ours as the 2D rule of point_proc: the projection code itself is NOT DECODED). OnScreenZDepth @180470cc0 ->
   FUN_1801adce0: point_proc_key with constr_view_is_3d forced on, prompt mccoreres 2388 "Select point for new construction depth";
   a typed value ('V') is the depth itself, a point its Z in the active C-plane (SubtractActiveOrigin + world_to_view); then the status
   bar's Z pane (33425, " %.4f") */
double constr_depth = 0; int zd_pick;
int zd_picking(void) { return zd_pick; }
void zd_start(void) { zd_pick = 1; snprintf(cam_msg, sizeof cam_msg, "Select point for new construction depth"); }
void zd_set(double z) { constr_depth = z; zd_pick = 0; cam_msg[0] = 0; overlay_dirty = 1; }
void zd_click(double x, double y, double z) {             /* the picked point (AutoCursor, 3D): its depth in the C-plane */
    double z0[3] = { 0, 0, 0 }, ez[3] = { 0, 0, 1 }, o[3], w1[3], cz[3];   /* the C-plane's origin and Z axis (view_to_world) */
    int id = plane_active[0] > 0 ? plane_active[0] : 1;
    plane_v2w(id, z0, z0, o); plane_v2w(id, z0, ez, w1);
    for (int k = 0; k < 3; k++) cz[k] = w1[k] - o[k];
    double l = sqrt(cz[0] * cz[0] + cz[1] * cz[1] + cz[2] * cz[2]); if (l > 0) for (int k = 0; k < 3; k++) cz[k] /= l;
    zd_set((x - o[0]) * cz[0] + (y - o[1]) * cz[1] + (z - o[2]) * cz[2]);
}
void constr_toggle_3d(void) { cfg_constr_3d = !cfg_constr_3d; }

/* ----------------------------------------------------------------------------------------- Change Level / Set All: selection first */
/* ChangeEntityLevel @18046c210 / ChangeEntityAttribs @18046bc80: select_ents(mask 0x1f1fff, prompt opmanres 179 "Select entities to
   change the level of" / 186 "Select entities to change the attributes of") - entities already selected are taken at once (the
   pre-selection), else the user selects and ends with Enter; then the dialog (FUN_18046b290 = opmanres 516 / sel_attrib_dialog =
   192). Esc / Cancel: the selection cleared (set_clr_sel_bits_all(0, 4, ..)) */
int selrun_kind;
void selrun_start(int kind) {
    if (attr_selected()) { selrun_kind = kind; selrun_end(1); return; }
    selrun_kind = kind;
    snprintf(cam_msg, sizeof cam_msg, "%s", kind == SR_LEVEL ? "Select entities to change the level of" : "Select entities to change the attributes of");
}
void selrun_end(int ok) {
    int k = selrun_kind; selrun_kind = SR_NONE; cam_msg[0] = 0;
    if (!ok || !attr_selected()) { sel_clear(); overlay_dirty = 1; return; }
    if (cui_hooks.selrun_dialog) cui_hooks.selrun_dialog(k);
}
/* a copy of entity i in place, as Paste makes it (cui_misc.c paste_clip): points / lines / arcs (view arcs keep their view) and
   surfaces with the record (a trimmed / offset surface's base is not duplicated: -1, ours); splines are not copied here (-1) */
int ent_duplicate(int i) {
    const Ent *e = &F.ents[i]; int k = -1;
    if (e->type == E_SURF) {
        const Surf *sf = &F.surfs[e->srf]; if (sf->kind == 10 || sf->kind == 8) return -1;
        uint8_t *r = malloc(sf->n); memcpy(r, sf->rec, sf->n); k = mcam_new_surf(&F, r, sf->n, e->level, e->color); free(r);
    } else if (e->type == E_ARC && e->vw) k = mcam_new_arc_view_w(&F, e->vw, e->vm, e->c, e->r, e->a0, e->sw, e->level, e->color);
    else if (e->type == E_POINT || e->type == E_LINE || e->type == E_ARC) k = mcam_new_ent(&F, e->type, e->a, e->b, e->c, e->r, e->a0, e->sw, e->level, e->color);
    if (k >= 0) { Ent *n = &F.ents[k], *o = &F.ents[i]; n->ln_style = o->ln_style; n->pt_style = o->pt_style; n->width = o->width >= 1 ? o->width : 1;
        for (int p = n->first_poly; p < n->first_poly + n->npoly && p < F.npolys; p++) { F.polys[p].ln_style = (uint8_t)n->ln_style; F.polys[p].width = (uint8_t)n->width; } }
    return k;
}
/* Change Levels' OK (ChangeEntityLevel l.4): Move = each selected entity's level (ent +8) rewritten (rewrite_ent); Copy
   (DAT_1809af29c) = translate_silent copies in place (the originals stay, deselected) and the copies get the level. Force visibility
   (the level's on / off, LevelData::setLevel) is NOT PORTED (levels have no visibility in this program yet) */
int attr_change_level(int copy, int level) {
    if (level < 1) return 0;
    if (!copy) return attr_set_level(level);
    int from = F.nents, n = 0, nsel = 0, skipped = 0;
    for (int i = 0; ent_sel && i < ent_sel_n && i < from; i++) if (ent_sel[i] && F.ents[i].type) {
        nsel++; int k = ent_duplicate(i);
        if (k < 0) { skipped++; continue; }
        F.ents[k].level = level; mcam_level_store(&F, level); for (int p = F.ents[k].first_poly; p < F.ents[k].first_poly + F.ents[k].npoly && p < F.npolys; p++) F.polys[p].level = level;
        if (F.ents[k].type == E_SURF) { Surf *s = &F.surfs[F.ents[k].srf]; s->level = level; memcpy(s->rec + 8, &level, 4); }
        n++;
    }
    if (n) { undo_ents_added(from); doc_dirty = 1; scene_surfs_update(0); scene_reupload(); }
    if (nsel) sel_clear();
    if (skipped) snprintf(cam_msg, sizeof cam_msg, "Change Level Copy: %d entit%s not copied (splines / trimmed or offset surfaces: NOT PORTED)", skipped, skipped == 1 ? "y" : "ies");
    overlay_dirty = 1;
    return n;
}

/* ----------------------------------------------------------------------------------------------------------- script steps (ours) */
/* lnstyle:N / lnwidth:N / ptstyle:N (the combos), defcolor:KIND,C (a colour button's dialog OK), chlevel / setall (the commands),
   selrunok / selrunesc (Enter / Esc), chlevelok:0|1,LEVEL (Change Levels' OK: Move / Copy), setallok:COLOR,LSTYLE,PSTYLE,LEVEL,WIDTH
   (Attributes' OK, -1 = that box unchecked), zdepth:Z (a typed depth), zdpick + zdpt:X,Y,Z (the depth pick), cplane3d (the 2D / 3D
   toggle), attrdump (the main attributes and every live entity's) */
int cui_attr_script(const char *st, const char *arg) {
    #define IS(S_) (!strcmp(st, S_))
    if (IS("lnstyle") && arg) { attr_line_style(atoi(arg)); return 1; }
    if (IS("lnwidth") && arg) { attr_line_width(atoi(arg)); return 1; }
    if (IS("ptstyle") && arg) { attr_point_style(atoi(arg)); return 1; }
    if (IS("defcolor") && arg) { int k = 0, c = 0; if (sscanf(arg, "%d,%d", &k, &c) == 2) attr_default_color(k, c); return 1; }
    if (IS("chlevel")) { selrun_start(SR_LEVEL); return 1; }
    if (IS("setall")) { selrun_start(SR_ATTRIBS); return 1; }
    if (IS("selrunok")) { selrun_end(1); return 1; }
    if (IS("selrunesc")) { selrun_end(0); return 1; }
    if (IS("chlevelok") && arg) { int cp = 0, lv = 0; if (sscanf(arg, "%d,%d", &cp, &lv) == 2) attr_change_level(cp, lv); return 1; }
    if (IS("setallok") && arg) { int v[5] = { -1, -1, -1, -1, -1 }; sscanf(arg, "%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
        attr_set_all(v[0] >= 0, v[0], v[1] >= 1, v[1], v[2] >= 1, v[2], v[3] >= 1, v[3], v[4] >= 1, v[4]); return 1; }
    if (IS("zdepth") && arg) { zd_set(atof(arg)); return 1; }
    if (IS("zdpick")) { zd_start(); return 1; }
    if (IS("zdpt") && arg) { double x = 0, y = 0, z = 0; sscanf(arg, "%lf,%lf,%lf", &x, &y, &z); if (zd_pick) zd_click(x, y, z); return 1; }
    if (IS("cplane3d")) { constr_toggle_3d(); return 1; }
    if (IS("attrdump")) {
        printf("    main: color %d level %d lstyle %d width %g pstyle %d | surf %d solid %d | depth %.4f %s\n", geo_color, geo_level, cfg_ln_style, cfg_ln_width,
               cfg_pt_style, srf_front_color, cfg_solid_color, constr_depth, cfg_constr_3d ? "3D" : "2D");
        for (int i = 0; i < F.nents; i++) { const Ent *e = &F.ents[i]; if (!e->type || e->gen || e->type == E_NOTE) continue;   /* notes: notedump */
            printf("      ent %d type %d color %d level %d lstyle %d pstyle %d width %g z %.3f%s\n", i, e->type, e->color, e->level, e->ln_style, e->pt_style, e->width, e->a.z, e->attr_edited ? " edited" : ""); }
        return 1; }
    return 0;
    #undef IS
}

/* ----------------------------------------------------------------------------------------- the dialogs' templates */
const BsCtl CLDLG_516[] = {   /* opmanres dialog 516 at its template positions (exports/opmanres_dialogs.tsv) */
    { 1358, 8, 4, 5, 121, 30, "Operation" }, { 1360, 5, 10, 17, 50, 12, "Move" }, { 1362, 5, 66, 17, 40, 12, "Copy" },
    { 1370, 8, 4, 39, 121, 110, "Level Number" }, { 1367, 4, 10, 52, 90, 12, "Use Active Level" }, { 1401, 0, 10, 70, 51, 10, "Number:" },
    { 1368, 2, 10, 82, 50, 14, "" }, { 1103, 1, 66, 82, 40, 14, "Select..." }, { 4150, 4, 10, 100, 80, 14, "Force visibility" },
    { 4151, 5, 21, 116, 30, 12, "On" }, { 4152, 5, 52, 116, 30, 12, "Off" }, { 1, 1, 9, 153, 36, 18, "OK" },
    { 2, 1, 46, 153, 36, 18, "Cancel" }, { 9, 1, 83, 153, 36, 18, "Help" }, { 444, 0, 10, 132, 110, 10, "(Active level is always visible)" }, { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl ADLG_192[] = {   /* opmanres dialog 192 at its template positions (exports/opmanres_dialogs.tsv) */
    { 1259, 8, 4, 5, 121, 30, "Color" }, { 1270, 4, 10, 17, 12, 12, "" }, { 1261, 2, 22, 17, 50, 12, "" }, { 1281, 1, 78, 16, 20, 14, "" },
    { 1263, 1, 102, 15, 16, 16, "" }, { 25065, 8, 4, 39, 121, 30, "Line Style" }, { 1275, 4, 10, 52, 12, 12, "" },
    { 25269, 3, 21, 51, 96, 81, "" }, { 25066, 8, 4, 73, 121, 30, "Point Style" }, { 1284, 4, 10, 86, 12, 12, "" },
    { 25347, 3, 22, 85, 96, 101, "" }, { 1268, 8, 4, 107, 121, 78, "Level" }, { 1271, 4, 10, 119, 12, 12, "" },
    { 1265, 2, 22, 119, 50, 12, "" }, { 1269, 1, 78, 118, 40, 14, "Select..." }, { 4150, 4, 22, 137, 80, 14, "Force visibility" },
    { 4151, 5, 32, 153, 30, 12, "On" }, { 4152, 5, 63, 153, 30, 12, "Off" }, { 25067, 8, 4, 189, 121, 30, "Line Width" },
    { 1272, 4, 10, 202, 12, 12, "" }, { 25273, 3, 22, 201, 96, 80, "" }, { 25064, 1, 36, 280, 70, 14, "EA Mgr" },
    { 27015, 1, 24, 280, 12, 14, "" }, { 1276, 1, 40, 260, 50, 14, "Same as..." }, { 1, 1, 9, 302, 36, 18, "OK" },
    { 2, 1, 46, 302, 36, 18, "Cancel" }, { 9, 1, 83, 302, 36, 18, "Help" }, { 1278, 8, 4, 223, 121, 30, "Surface Density" },
    { 1280, 4, 10, 236, 12, 12, "" }, { 1287, 2, 22, 236, 96, 12, "" }, { 444, 0, 9, 169, 110, 10, "(Active level is always visible)" }, { 0, 0, 0, 0, 0, 0, NULL } };
