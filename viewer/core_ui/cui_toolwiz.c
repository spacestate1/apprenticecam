/* cui_toolwiz.c - the model of the Define Tool / Edit Tool wizard window (formats/toolmanager_ui.md section 2; the per-type
   geometry rules are viewer/toolwiz.c): the tool being edited and its TwState, the page, opening / closing / Finish, the
   «Standard sizes» catalog rows and the StdSizeOverwritingToolGeometry warning (2.6.y / 2.10), the Finalize Properties rows
   (2.9, toolpropertiesview.baml) with their commit rules, and the Coolant dialog (2.12a). Moved from viewer/mcamui.c
   (formats/qt_port_plan.md phase 7); names, comments and citations kept. The drawing (nuklear: mcamui.c draw_tool_editor /
   tw_page_* / draw_coolant_dialog; Qt: viewer-qt/src/toolwiz.cpp) stays in the front ends. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "toollib.h"
#include "toolwiz.h"
#include "core_ui.h"

#define message cui_message

/* ---- Define Tool / Edit Tool wizard (formats/toolmanager_ui.md section 2; logic in toolwiz.c) ----
   Window: 200 px sidebar «Current Step:» + an 800 x 520 page, buttons below (2.2). Pages (2.3): Select Tool Type,
   Define Tool Geometry, Finalize Properties; in edit mode the type page is removed. */
Tool tedit; int tedit_index = -2;                     /* -2 closed, -1 new tool, else index into F.tools */
int tm_lib_tool_edit = -1;                            /* wizard on a library tool: its index, -2 a new library tool, -1 part */
TwState tws; int tw_page;                             /* 0 type, 1 define, 2 properties */
const char *const TW_TITLES[3] = { "Select Tool Type", "Define Tool Geometry", "Finalize Properties" };
void tedit_open(int index, const Tool *src) {
    tedit_index = index;
    memset(&tws, 0, sizeof tws);
    /* add-to-offset values: Mastercam's native caller is NOT DECODED; 0 = D / H follow the tool number (2.1) */
    if (src) { memset(&tedit, 0, sizeof tedit); tool_copy(&tedit, src); tw_init_edit(&tedit, &tws, 0, 0); tw_page = 1; }   /* edit: InitialPage DefineGeometryPage */
    else {   /* create: DefaultToolType 10 (or 3 when drilling, NOT DECODED which Mastercam passes), ToolNumberDefault */
        tw_new_type(&tedit, &tws, 10, next_tool_num(), mcam_is_metric(&F));
        tw_page = 0;
    }
    tedit.has_extras = 1;   /* the wizard's fields are set (mcamui set this in every frame of draw_tool_editor; here once the tool
                               is (re)made, so a scripted run without frames gets the same tool) */
}
void tedit_close(void) { tm_lib_tool_edit = -1; free(tedit.holder_rz); memset(&tedit, 0, sizeof tedit); tedit_index = -2; }
/* the Tool page's hook (cui_hooks.tool_wizard, mcamui hook_tool_wizard): a part tool, -1 = a new one */
void tw_open_part(int index) { tm_lib_tool_edit = -1; if (index < 0) tedit_open(-1, NULL); else tedit_open(index, &F.tools[index]); }
int tw_first_page(void) { return tws.creating ? 0 : 1; }   /* edit mode: the type page is removed */
const char *tw_title(void) { return tws.creating ? "Define Tool" : "Edit Tool"; }
void tw_caption(char *cap, size_t n) {
    if (tw_page == 0) snprintf(cap, n, "What type of tool would you like to create?");
    else if (tw_page == 1) snprintf(cap, n, "Define %s", tw_type_label(tedit.type));
    else snprintf(cap, n, "Finalize miscellaneous properties.");
}
/* Back / Next / Finish (2.3): Finish: CanFinish = !IsReadOnly; Finish is disabled on the type page; an invalid tool does not
   disable it. Help: HelpBrowser topic 75005 is not available here (greyed) */
int tw_can_back(void) { return tw_page > tw_first_page(); }
int tw_can_next(void) { return tw_page < 2; }
int tw_can_finish(void) { return tw_page > 0; }
void tw_back(void) { if (tw_can_back()) tw_page--; }
void tw_next(void) { if (tw_can_next()) tw_page++; }
void tw_goto(int p) { if (p >= tw_first_page() && p <= 2) tw_page = p; }   /* the sidebar's step list */
/* page 1: a type picked (PreviewSelectedPageChanging: a new type = that type's default tool; number kept, offsets re-derived) */
void tw_pick_type(int type) { if (tedit.type != type) { tw_new_type(&tedit, &tws, type, tedit.num, mcam_is_metric(&F)); tedit.has_extras = 1; } }

/* «Standard sizes» (DropdownGridView, 2.6.y): catalog rows; a pick runs the page's BulkUpdatePropertiesInternal
   (GeometryPageViewModel.BulkUpdateProperties: when editing, the StdSizeOverwritingToolGeometry warning first) */
StdSize *tw_sizes; int tw_nsizes = -1, tw_size_pick = -1;
static int tw_sizes_type = -1, tw_sizes_metric = -1;
int tw_warn_row = -1, tw_warn_off;                    /* pending warning (2.10); «Do not show ... for this session» */
/* a catalog row as the grid's XmlElement (DropdownGridResources templates: CenterDrill Size / ShankDiameter / DrillDiameter /
   DrillLength / OverallLength, SpotDrill Size / DrillDiameter / DrillLength / OverallLength, Drill Name / Diameter /
   FluteLength / OverallLength, Tap the same + ThreadInfo) -> the page's BulkUpdatePropertiesInternal (toolwiz.c) */
static void tw_apply_std(Tool *t, const StdSize *r) {
    TwStdRow w; memset(&w, 0, sizeof w);
    if (t->type == 1) { w.has = TWS_SHANK_DIA | TWS_DRILL_DIA | TWS_DRILL_LEN | TWS_OAL; w.shank_dia = r->shank; w.drill_dia = r->drill_dia; w.drill_len = r->drill_len; w.oal = r->oal; w.size = r->name; }
    else if (t->type == 2) { w.has = TWS_DRILL_DIA | TWS_DRILL_LEN | TWS_OAL; w.drill_dia = r->dia; w.drill_len = r->flute; w.oal = r->oal; w.size = r->name; }
    else { w.has = TWS_DIA | TWS_FLUTE_LEN | TWS_OAL; w.dia = r->dia; w.flute_len = r->flute; w.oal = r->oal; w.name = r->name;
           if (t->type == 4 || t->type == 5) { w.has |= TWS_THREAD_INFO; w.thread_info = r->thread_info; } }
    tw_std_size(t, &tws, &w);
}
int tw_std_refresh(const Tool *t) {                   /* the catalog rows of the tool's type / units (re-read when they change) */
    if (tw_sizes_type != t->type || tw_sizes_metric != (t->is_metric != 0)) {
        free(tw_sizes); tw_sizes = NULL;
        tw_nsizes = toollib_catalog_sizes(t->type, t->is_metric != 0, &tw_sizes);
        tw_sizes_type = t->type; tw_sizes_metric = t->is_metric != 0; tw_size_pick = -1;
    }
    return tw_nsizes;
}
const char *tw_std_prompt(const Tool *t) {            /* the closed grid's text: the pick, else the type's prompt */
    if (tw_size_pick >= 0 && tw_size_pick < tw_nsizes) return tw_sizes[tw_size_pick].name;
    return t->type == 1 ? "Select a standard size center drill." : t->type == 2 ? "Select a standard size spot drill." :
           t->type == 22 ? "Select a standard size bradpoint drill." : (t->type == 4 || t->type == 5) ? "Select a standard size tap." :
           "Select a standard size drill.";
}
void tw_std_row_text(const Tool *t, int i, char *row, size_t n) {
    const StdSize *r = &tw_sizes[i];
    if (t->type == 1) snprintf(row, n, "%-10s %8.4g %8.4g %8.4g %8.4g", r->name, r->shank, r->drill_dia, r->drill_len, r->oal);
    else if (t->type == 4 || t->type == 5) snprintf(row, n, "%-22s %8.4g %8.4g %8.4g %8.4g", r->name, r->dia, r->flute, r->oal, r->thread_info);
    else snprintf(row, n, "%-22s %8.4g %8.4g %8.4g", r->name, r->dia, r->flute, r->oal);
}
void tw_std_choose(int i) {                           /* a grid row picked */
    if (i < 0 || i >= tw_nsizes) return;
    if (tws.creating || tw_warn_off) { tw_size_pick = i; tw_apply_std(&tedit, &tw_sizes[i]); }
    else tw_warn_row = i;
}
void tw_warn_answer(int yes, int dont) {              /* ToolWizardWarningView (2.10): StdSizeOverwritingToolGeometry */
    if (yes) { if (tw_warn_row >= 0 && tw_warn_row < tw_nsizes) { tw_size_pick = tw_warn_row; tw_apply_std(&tedit, &tw_sizes[tw_warn_row]); }
               tw_warn_off = dont; }
    tw_warn_row = -1;
}

/* Finish: the tool goes to the library (a library tool) or the part */
void tw_finish(void) {
    Tool *t = &tedit;
    /* record +6 «Rad. Type» from TlRadiusType for the TlToolEndmill family (all 280 mill_mm.tooldb tools: corner radius 2 ->
       1, full radius 3 -> 2, else 0) */
    if ((t->type >= 10 && t->type <= 19) || t->type == 21) t->rad_type = t->radius_type == 2 ? 1 : t->radius_type == 3 ? 2 : 0;
    if (tm_lib_tool_edit != -1 && tlib_ok) {           /* a library tool: written to the library (TlToolLibraryX) */
        if (tm_lib_tool_edit >= 0 && tm_lib_tool_edit < tlib.n) { free(tlib.t[tm_lib_tool_edit].holder_rz); tool_copy(&tlib.t[tm_lib_tool_edit], t); }
        else toollib_add(&tlib, t);
        tm_lib_write(); tedit_close(); return;
    }
    int clash = -1; for (int i = 0; i < F.ntools; i++) if (i != tedit_index && F.tools[i].num == t->num) clash = i;
    if (clash >= 0) {   /* mccore TpToolList::WarnOfDuplicateToolNumbers @1808f6250: message_box_ok(text, 5583, 3) with text =
                           "%s%ld\n\n%s" of mccoreres 5583, the number and CreateToolDescription of the other tool (the
                           loaded 5573 is not used). Mastercam then keeps the tool; here ops find tools by number, so the
                           edit stays open (deviation) */
        char d[200]; cui_tm_describe(&F.tools[clash], d, sizeof d, 0);
        message("Duplicate tool numbers!", "Duplicate tool numbers!%d\n\n%s", t->num, d); return; }
    int idx = tedit_index;
    if (idx < 0) idx = part_add_tool(t);
    else { int oldnum = F.tools[idx].num; free(F.tools[idx].holder_rz); tool_copy(&F.tools[idx], t);
           for (int k = 0; k < ncamops; k++) if (camops[k]->p.tool_num == oldnum) camops[k]->p.tool_num = t->num; }
    doc_dirty = 1;
    if (idx >= 0) { tm_part_sel = idx; regen_ops_for_tool(F.tools[idx].num); }
    tedit_close();
}

/* ---- Finalize Properties (2.9, ToolPropertiesView: the Operation expander left, General + the type panel right
   (ToolPropertyDataTemplateSelector)), in mcamui's row order. Edit boxes commit as ToolEditBoxConverter (2.5: a plain
   number >= 0, else the old value stays); the ids are mcamui's edit-box ids. ---- */
static const char *const TW_DIRS[3] = { "Clockwise", "Counterclockwise", "Static" };   /* SpindleDirectionList */
static const char *const TW_HELIX[4] = { "None", "Up", "Down", "Compression" };        /* HelixTypeList */
static const char *const TW_CYCLES[20] = { "Drill/Counterbore", "Peck Drill", "Chip Break", "Tap", "Bore #1", "Bore #2", "Fine Bore (Shift)",
    "Rigid Tapping Cycle", "Custom Cycle 9", "Custom Cycle 10", "Custom Cycle 11", "Custom Cycle 12", "Custom Cycle 13",
    "Custom Cycle 14", "Custom Cycle 15", "Custom Cycle 16", "Custom Cycle 17", "Custom Cycle 18", "Custom Cycle 19", "Custom Cycle 20" };   /* CannedCycleTypeList */
static char tw_mats[32][32]; static const char *tw_mat_items[33]; static int tw_nmats = -1;
int tw_materials(const char *const **items) {         /* DatabaseCatalog.tooldb tool materials by name (read once) */
    if (tw_nmats < 0) { tw_nmats = toollib_catalog_materials(tw_mats, 32); for (int i = 0; i < tw_nmats; i++) tw_mat_items[i] = tw_mats[i]; }
    *items = tw_mat_items; return tw_nmats;
}
int tw_prop_rows(const Tool *t, TwPRow *o, int cap) {
    int n = 0, metric = t->is_metric != 0;
#define P(k, i, l, c) do { if (n < cap) { o[n].kind = k; o[n].id = i; o[n].label = l; o[n].col = c; o[n].tip = NULL; } n++; } while (0)
    P(TWP_GROUP, 0, "Operation", 0);
    P(TWP_INT, 100, "Tool number:", 0); P(TWP_INT, 101, "Length offset:", 0); P(TWP_INT, 102, "Diameter offset:", 0); P(TWP_INT, 103, "Head number:", 0);
    P(TWP_NUM, 104, metric ? "CS:" : "SFM:", 0); P(TWP_NUM, 105, "FPT:", 0); P(TWP_INT, 106, "Number of flutes:", 0);
    P(TWP_NUM, 107, "Feed rate:", 0); P(TWP_NUM, 108, "Plunge rate:", 0); P(TWP_NUM, 109, "Retract rate:", 0); P(TWP_INT, 110, "Spindle speed:", 0);
    P(TWP_COMBO, 130, "Spindle direction:", 0); P(TWP_COMBO, 131, "Material:", 0);
    P(TWP_NUM, 111, "Cutting speed adjustment:", 0); P(TWP_NUM, 112, "FPT adjustment:", 0);
    /* toolpropertiesview.baml (Operation expander, advanced content): row 4 Button "Coolant" (75 x 23, tooltip
       "Canned text coolant settings", EditCoolantCommand -> EditCoolantService.EditCoolant), row 6 CheckBox
       "Metric" (ToolMetric, IsChecked = EntitySelected.IsMetric, tooltip GeneralMetricTextBox_ToolTip) */
    P(TWP_BUTTON, 132, "Coolant", 0); if (n <= cap) o[n - 1].tip = "Canned text coolant settings";
    P(TWP_CHECK, 133, "Metric", 0); if (n <= cap) o[n - 1].tip = "Select the checkbox if the assembly is metric. Leave the checkbox unchecked if the assembly is inches.";
    P(TWP_GROUP, 0, "General", 1);
    P(TWP_TEXT, 140, "Name:", 1); P(TWP_TEXT, 141, "Description:", 1); P(TWP_TEXT, 142, "Manufacturer's tool code:", 1);
    P(TWP_NUM, 113, "Required pilot diameter:", 1); P(TWP_NUM, 114, "Helix Angle:", 1); P(TWP_COMBO, 134, "Helix Type:", 1);
    P(TWP_TEXT, 143, "Chuck:", 1);
    int ty = t->type;
    int milling = (ty >= 10 && ty <= 19) || ty == 21 || ty == 23 || ty == 25 || ty == 24 || ty == 0;
    int hole = tool_is_drill(ty) || ty == 6 || ty == 7;
    if (ty == 24) { P(TWP_GROUP, 0, "Thread milling", 1); P(TWP_NUM, 115, "Thread root diameter:", 1); }
    if (ty == 4 || ty == 5) { P(TWP_GROUP, 0, "Tapping", 1); P(TWP_NUM, 115, "Thread root diameter:", 1); }
    if (ty == 7) { P(TWP_GROUP, 0, "Boring", 1); P(TWP_NUM, 116, "Shift value (% dia):", 1); }
    if (ty == 6) P(TWP_GROUP, 0, "Reaming", 1);
    if (ty != 4 && ty != 5 && ty != 6 && ty != 7 && hole) P(TWP_GROUP, 0, "Drilling", 1);
    if (milling) {
        P(TWP_GROUP, 0, "Milling", 1);
        P(TWP_CHECK, 135, "Rough Tool", 1); P(TWP_CHECK, 136, "Finish Tool", 1);
        P(TWP_NUM, 117, "Rough XY step (%):", 1); P(TWP_NUM, 118, "Rough Z step (%):", 1); P(TWP_NUM, 119, "Finish XY step (%):", 1); P(TWP_NUM, 120, "Finish Z step (%):", 1);
    }
    if (hole) {
        P(TWP_GROUP, 0, "Holemaking", 1);
        P(TWP_NUM, 121, "Peck 1 (% dia):", 1); P(TWP_NUM, 122, "Peck 2 (% dia):", 1); P(TWP_NUM, 123, "Peck clearance (%):", 1);
        P(TWP_NUM, 124, "Dwell:", 1); P(TWP_NUM, 125, "Chip break (% dia):", 1); P(TWP_COMBO, 137, "Cycle:", 1);
    }
#undef P
    return n < cap ? n : cap;
}
double tw_prop_get(const Tool *t, int id) {           /* the value an edit box shows */
    switch (id) {
    case 100: return t->num; case 101: return t->len_off; case 102: return t->dia_off; case 103: return t->head_num;
    case 104: return tw_sfm(t); case 105: return tw_fpt(t); case 106: return t->flutes; case 107: return t->feed;
    case 108: return t->plunge; case 109: return t->retract; case 110: return t->rpm; case 111: return t->sfm_adj; case 112: return t->fpt_adj;
    case 113: return t->req_pilot_dia; case 114: return t->helix_angle; case 115: return t->root_dia; case 116: return t->shift;
    case 117: return t->step_rxy; case 118: return t->step_rz; case 119: return t->step_fxy; case 120: return t->step_fz;
    case 121: return t->peck1; case 122: return t->peck2; case 123: return t->peck_clear; case 124: return t->dwell; case 125: return t->chipbreak;
    }
    return 0;
}
/* the shown text: ints "%d"; numbers Math.Round(v, metric 5 / inch 6), invariant culture (mcamui tw_edit_num) */
void tw_prop_text(const Tool *t, int id, int is_int, char *b, size_t n) {
    double v = tw_prop_get(t, id);
    if (is_int) snprintf(b, n, "%d", (int)v);
    else { double r = t->is_metric ? 1e5 : 1e6; snprintf(b, n, "%.15g", nearbyint(v * r) / r); }
}
/* the edit boxes' commit rules (mcamui tw_edit_txt / tw_edit_int): a number box takes a plain number >= 0 (expressions,
   GAFControlHwndHost.EvaluateExpression, are NOT DECODED); an integer box a whole number within +-1e9 */
int tw_parse_num(const char *s, double *out) {
    char *e; double d = strtod(s, &e); while (*e == ' ') e++;
    if (e != s && !*e && isfinite(d) && d >= 0) { *out = d; return 1; }
    return 0;
}
int tw_parse_int(const char *s, long *out) {
    char *e; long d = strtol(s, &e, 10);
    if (e != s && !*e && d > -1000000000L && d < 1000000000L) { *out = d; return 1; }
    return 0;
}
void tw_prop_set(Tool *t, int id, double dv) {        /* a committed edit (the value already passed the box's own rule) */
    int iv = (int)dv;
    switch (id) {
    case 100: tw_set_tool_number(t, &tws, iv); break;
    case 101: t->len_off = iv; break; case 102: t->dia_off = iv; break; case 103: t->head_num = iv; break;
    case 104: tw_set_sfm(t, dv); break; case 105: tw_set_fpt(t, dv); break;
    case 106: if (iv >= 0) tw_set_flutes(t, iv); break;
    case 107: tw_set_feed(t, dv); break; case 108: t->plunge = dv; break; case 109: t->retract = dv; break;
    case 110: if (iv >= 0) tw_set_rpm(t, iv); break;
    case 111: t->sfm_adj = dv; break; case 112: t->fpt_adj = dv; break;
    case 113: if (dv <= t->dia) t->req_pilot_dia = dv; break;   /* ToolEditBoxConverterWithMax: max = OverallDiameter */
    case 114: t->helix_angle = dv; break; case 115: t->root_dia = dv; break; case 116: t->shift = dv; break;
    case 117: t->step_rxy = dv; break; case 118: t->step_rz = dv; break; case 119: t->step_fxy = dv; break; case 120: t->step_fz = dv; break;
    case 121: t->peck1 = dv; break; case 122: t->peck2 = dv; break; case 123: t->peck_clear = dv; break; case 124: t->dwell = dv; break; case 125: t->chipbreak = dv; break;
    }
}
int tw_prop_combo(const Tool *t, int id, const char *const **items, int *cur) {
    if (id == 130) { *items = TW_DIRS; *cur = t->spindle_dir >= 0 && t->spindle_dir < 3 ? t->spindle_dir : 0; return 3; }
    if (id == 134) { *items = TW_HELIX; *cur = t->helix_type >= 0 && t->helix_type < 4 ? t->helix_type : 0; return 4; }
    if (id == 137) { *items = TW_CYCLES; *cur = t->cycle >= 0 && t->cycle < 20 ? t->cycle : 0; return 20; }
    if (id == 131) { int nm = tw_materials(items); *cur = -1; for (int i = 0; i < nm; i++) if (!strcmp(tw_mats[i], t->mat_name)) *cur = i; return nm; }
    *items = NULL; *cur = 0; return 0;
}
void tw_prop_set_combo(Tool *t, int id, int ns) {      /* the combos' setters (the material: only a change; a blank one stays blank on item 0) */
    const char *const *items; int cur, n = tw_prop_combo(t, id, &items, &cur);
    if (ns < 0 || ns >= n) return;
    if (id == 130) { if (ns != cur) t->spindle_dir = ns; }
    else if (id == 134) { if (ns != cur) t->helix_type = ns; }
    else if (id == 137) { if (ns != cur) t->cycle = ns; }
    else if (id == 131) { if (ns != cur && (cur >= 0 || ns != 0)) snprintf(t->mat_name, sizeof t->mat_name, "%s", tw_mats[ns]); }
}
int tw_prop_check(const Tool *t, int id) { return id == 133 ? t->is_metric != 0 : id == 135 ? t->is_rough != 0 : id == 136 ? t->is_finish != 0 : 0; }
void tw_prop_set_check(Tool *t, int id, int on) { if (id == 133) t->is_metric = on; else if (id == 135) t->is_rough = on; else if (id == 136) t->is_finish = on; }
char *tw_prop_str(Tool *t, int id, size_t *cap) {     /* the text fields' buffers */
    switch (id) {
    case 140: *cap = sizeof t->name; return t->name; case 141: *cap = sizeof t->desc; return t->desc;
    case 142: *cap = sizeof t->mfg_code; return t->mfg_code; case 143: *cap = sizeof t->chuck; return t->chuck;
    }
    *cap = 0; return NULL;
}
void tw_prop_set_str(Tool *t, int id, const char *s) {   /* the Name goes through its setter (name_manual, 2.8) */
    size_t cap; char *b = tw_prop_str(t, id, &cap); if (!b) return;
    if (id == 140) { if (strcmp(s, t->name)) tw_set_name(t, &tws, s); }
    else snprintf(b, cap, "%s", s);
}

/* Coolant dialog (toolmanager.database: CoolantView.baml, CoolantViewModel, TlCoolantViewModel, EditCoolantService,
   CoolantParameters, CoolantStrings.resx). Title "Coolant"; expander "Standard Coolant" with Flood / Mist / Thru-tool
   combos (items On / Off = CoolantParameters.<X>On / Off, the parameterless constructor's labels: this program has no
   host that passes the control definition's coolant labels); expander "Custom Coolant" with Flood, Mist, Thru-tool,
   Custom 1..7 (state On / Off / Ignore, time Before / With / After); Help, OK, Cancel (72 wide).
   Standard Coolant = TlCoolant.OldStyle (flags 8 off, 16 flood, 32 mist, 64 thru-tool): a combo shows On when OldStyle
   equals its flag, else Off; setting On writes the flag, Off writes 8 (only when the value changes); Flood is enabled
   only while Mist and Thru-tool are Off (OldStyleFloodEnabled), and so on. EditCoolant edits a copy, OK keeps it.
   NOT DONE (greyed): the Custom Coolant rows (TlCoolant Flood / FloodPos ... Custom7Pos are not kept by this program's
   tool record), Help (help topic 75007). */
TwCool cool;
const char *const COOL_ONOFF[2] = { "On", "Off" };
const char *const COOL_NAMES[3] = { "Flood", "Mist", "Thru-tool" };
const char *const COOL_CNAMES[10] = { "Flood", "Mist", "Thru-tool", "Custom 1", "Custom 2", "Custom 3", "Custom 4", "Custom 5", "Custom 6", "Custom 7" };
static const int COOL_FLAG[3] = { 16, 32, 64 };
int coolant_is_open(void) { return cool.open; }
void coolant_dlg_open(Tool *t) {
    cool.open = 1; cool.t = t;
    cool.style = t->coolant_style ? t->coolant_style : (t->coolant ? 16 : 8);
}
int cool_idx(int i) { return cool.style == COOL_FLAG[i] ? 0 : 1; }   /* the combo's item: 0 On, 1 Off */
int cool_enabled(int i) { return cool_idx((i + 1) % 3) != 0 && cool_idx((i + 2) % 3) != 0; }
void cool_set(int i, int ns) { if (cool_enabled(i) && ns != cool_idx(i)) cool.style = ns == 0 ? COOL_FLAG[i] : 8; }
void cool_ok(void) { cool.t->coolant_style = cool.style; cool.t->coolant = cool.style == 16; cool.open = 0; }
void cool_cancel(void) { cool.open = 0; }

/* the wizard's MCAMUI_SCRIPT "show:" steps (was mcamui run_script) + ours (phase 7): twprop:ID=V, twstd:I, twwarn:Y|N|dY,
   twback, twnext, twgo:P, twpick:T (a page-1 type through tw_pick_type), cool:I=K, coolok, coolcancel, twcancel, twdump */
int cui_tw_show_step(const char *arg) {
    if (!strncmp(arg, "twtype:", 7) && tedit_index > -2) { tw_new_type(&tedit, &tws, atoi(arg + 7), tedit.num, mcam_is_metric(&F)); tedit.has_extras = 1; }
    else if (!strncmp(arg, "twpage:", 7) && tedit_index > -2) tw_page = atoi(arg + 7);
    else if (!strcmp(arg, "coolant") && tedit_index > -2) coolant_dlg_open(&tedit);
    else if (!strcmp(arg, "twfinish") && tedit_index > -2) { tw_finish(); const Tool *x = F.ntools ? &F.tools[F.ntools - 1] : NULL;
        if (x) printf("    last tool T%d type %d D%g OAL %g FL %g name \"%s\" H%d D%d S%d F%g\n", x->num, x->type, x->dia, x->oal, x->flute_len, x->name, x->len_off, x->dia_off, x->rpm, x->feed); }
    else if (!strncmp(arg, "twset:", 6) && tedit_index > -2) { int f = atoi(arg + 6); const char *c = strchr(arg, '='); if (c) tw_set(&tedit, &tws, f, atof(c + 1)); }
    else if (!strncmp(arg, "tedit:", 6)) { int n = atoi(arg + 6); for (int i = 0; i < F.ntools; i++) if (F.tools[i].num == n) { tedit_open(i, &F.tools[i]); break; } }
    else if (!strncmp(arg, "twprop:", 7) && tedit_index > -2) {
        int id = atoi(arg + 7); const char *c = strchr(arg, '='); if (!c) return 1; c++;
        TwPRow r[64]; int n = tw_prop_rows(&tedit, r, 64), kind = -1;
        for (int k = 0; k < n; k++) if (r[k].id == id) kind = r[k].kind;
        if (kind == TWP_INT) { long d; if (tw_parse_int(c, &d)) tw_prop_set(&tedit, id, (double)d); }
        else if (kind == TWP_NUM) { double d; if (tw_parse_num(c, &d)) tw_prop_set(&tedit, id, d); }
        else if (kind == TWP_COMBO) tw_prop_set_combo(&tedit, id, atoi(c));
        else if (kind == TWP_CHECK) tw_prop_set_check(&tedit, id, atoi(c) != 0);
        else if (kind == TWP_TEXT) tw_prop_set_str(&tedit, id, c);
        else printf("    (no property %d)\n", id);
    }
    else if (!strncmp(arg, "twstd:", 6) && tedit_index > -2) { tw_std_refresh(&tedit); tw_std_choose(atoi(arg + 6)); }
    else if (!strncmp(arg, "twwarn:", 7)) { const char *a = arg + 7; tw_warn_answer(a[0] == 'Y' || a[1] == 'Y', a[0] == 'd'); }
    else if (!strcmp(arg, "twback")) tw_back();
    else if (!strcmp(arg, "twnext")) tw_next();
    else if (!strncmp(arg, "twgo:", 5)) tw_goto(atoi(arg + 5));
    else if (!strncmp(arg, "twpick:", 7) && tedit_index > -2) tw_pick_type(atoi(arg + 7));
    else if (!strncmp(arg, "cool:", 5) && cool.open) { int i = atoi(arg + 5); const char *c = strchr(arg, '='); if (c && i >= 0 && i < 3) cool_set(i, atoi(c + 1)); }
    else if (!strcmp(arg, "coolok") && cool.open) cool_ok();
    else if (!strcmp(arg, "coolcancel")) cool_cancel();
    else if (!strcmp(arg, "twcancel")) tedit_close();
    else if (!strcmp(arg, "twdump")) tw_dump();
    else return 0;
    return 1;
}
/* tests: the wizard's state and the tool's fields (Define page boxes, Finalize Properties) */
void tw_dump(void) {
    if (tedit_index < -1) { printf("    wizard: closed\n"); return; }
    const Tool *t = &tedit; char cap[96]; tw_caption(cap, sizeof cap);
    const char *err = tw_validate(t);
    printf("    wizard \"%s\" page %d \"%s\" index %d lib %d type %d valid %s back %d next %d finish %d warn %d cool %d style %d\n", tw_title(), tw_page, cap,
           tedit_index, tm_lib_tool_edit, t->type, err ? err : "yes", tw_can_back(), tw_can_next(), tw_can_finish(), tw_warn_row, cool.open, cool.open ? cool.style : t->coolant_style);
    const TwRow *rows; int nr = tw_rows(t->type, &rows);
    printf("      define:");
    for (int i = 0; i < nr; i++) if (rows[i].kind == TWR_BOX) { char s[48]; tw_format(t, rows[i].field, s, sizeof s); printf(" %s=%s%s", tw_field_name(rows[i].field), s, tw_box_enabled(t, rows[i].field) ? "" : "(off)"); }
    printf(" corner %d upper %d shank %d\n", tw_corner_index(t), tw_slot_upper_index(t), t->shank_type);
    TwPRow r[64]; int n = tw_prop_rows(t, r, 64);
    printf("      props:");
    for (int k = 0; k < n; k++) {
        char s[160]; const char *const *items; int cur; size_t cp;
        switch (r[k].kind) {
        case TWP_INT: case TWP_NUM: tw_prop_text(t, r[k].id, r[k].kind == TWP_INT, s, sizeof s); printf(" %d=%s", r[k].id, s); break;
        case TWP_COMBO: { int m = tw_prop_combo(t, r[k].id, &items, &cur); printf(" %d=%s", r[k].id, m > 0 && cur >= 0 ? items[cur] : "-"); break; }
        case TWP_CHECK: printf(" %d=%d", r[k].id, tw_prop_check(t, r[k].id)); break;
        case TWP_TEXT: printf(" %d=\"%s\"", r[k].id, tw_prop_str((Tool *)t, r[k].id, &cp)); break;
        case TWP_GROUP: printf(" [%s]", r[k].label); break;
        }
    }
    printf("\n");
}
