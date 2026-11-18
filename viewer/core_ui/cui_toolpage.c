/* cui_toolpage.c - the model of the toolpath dialog's Tool page (mccore ToolPage 6556 / ToolPageHEM 25524, ToolPageBase),
   its right-click menu 1247, the Tool List dialog 6410 behind "Select library tool...", and the tool list basics they
   share with the Tool Manager (the part / library tool lists' column sets and cells, the current tool library, adding a
   tool to the part). Moved from viewer/opparams_ui.inc, viewer/toolmgr_ui.inc and viewer/mcamui.c (formats/qt_port_plan.md
   phase 6; formats/mcamui_dialogs.md 1 "Tool page" / "Tool List 6410"; formats/toolmanager_ui.md 1.5.2 / 1.5.3 for the
   column sets); names, comments and citations kept. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "paths.h"
#include "toollib.h"
#include "mcam_saveops.h"
#include "core_ui.h"

#define message cui_message
char cui_ui_dir[600] = "ui";                          /* the front end's ui folder (starter_tools_mm.tools) */
int cfg_places = -1;                                  /* config 29003 places after decimal for analyze (n_places_an) */
ToolLib tlib; int tlib_ok;                            /* the library open in the Tool Manager */

/* ---- the tool lists' column sets and cells (was viewer/toolmgr_ui.inc; formats/toolmanager_ui.md section 1.5) ---- */
static const int tm_defw[TM_COLS] = { 0, 25, 40, 80, 80, 80, 50, 50, 50, 50, 50, 50, 50 };   /* GetDefaultColumnWidth */
const char *tm_head[TM_COLS] = { "", "", "#", "Assembly Name", "Tool Name", "Holder Name", "Dia.", "Cor. rad.", "Length",
                                        "# Flutes", "Type", "Rad. Type", "Tool Projection" };   /* 3759 12128 3764 12127 3794 3791 3781 3792 3761 3793 3801 */
int tm_view;                                   /* view combo 25297: 0 Both, 1 Part Tools, 2 Library Tools ("Tool Manager Mode", default 0) */
int tm_dispmode = 2;                           /* Display mode 10408-10410 (library list only): 0 Tools, 1 Assemblies, 2 Both (default 2) */
int tm_factive[2];                             /* Filter Active 25325 / 25263: DAT_181431680/81, zero at start */
/* column sets (GetActiveColumnSet = list+0x1e3a8 + mode * 0xb0): one per display mode; the part list stays in mode 2 */
TmColSet tm_cs[2][3];
int tm_listview[2] = { 1, 1 };                 /* "List Style" (SetViewMode): 0 Large Icons, 1 Details, 3 Small Icons */
int tm_colinit;


/* ---- column sets and the persisted settings (1.5.2 / 1.5.3; stored in our config dir, never Mastercam's registry) ---- */
static void cs_reset(TmColSet s3[3], int *listview) {   /* ResetListCtrlColumns 0x18050a7f0 + ReadRegistrySettings defaults */
    for (int m = 0; m < 3; m++) {
        TmColSet *s = &s3[m];
        for (int c = 0; c < TM_COLS; c++) { s->w[c] = tm_defw[c]; s->vis[c] = tm_defw[c] != 0; s->ord[c] = c; }
        s->vis[12] = 0; if (m == 0) s->vis[3] = s->vis[5] = 0;   /* set 0 hides 3, 5, 12; sets 1, 2 hide 12 */
        s->sortcol = 2; s->sortasc = 1;                          /* sort column 2 ascending */
    }
    *listview = 1;                                               /* view mode 1 (Details) */
}
void tm_cs_reset_list(int L) { cs_reset(tm_cs[L], &tm_listview[L]); }
/* the registry keys under get_REG_KEY: FUN_18087c200 opens REG_KEY\\DAT_181431690\\DAT_181431698 ("Tool Manager Mode",
   "Tool Manager Display Mode") and appends \\DAT_1814316a0 / \\DAT_1814316a8 for the two lists' ReadRegistrySettings; mccore's
   static initialisers (0x180004114-0x1800041ab) set them to "GUI", "Mill Tool Manager", "Part Tool List", "Lib Tool List"
   (the same key names as the user's Mastercam X4 hive). Here they are name prefixes in toolmanager.conf. */
#define TM_KEY "GUI\\Mill Tool Manager"
static const char *tm_regkey[2] = { TM_KEY "\\Part Tool List", TM_KEY "\\Lib Tool List" };
static char (*tm_ck)[96]; static int *tm_cv, tm_cn;   /* toolmanager.conf as read */
static int tm_get(const char *name, int *out) { for (int i = 0; i < tm_cn; i++) if (!strcmp(tm_ck[i], name)) { *out = tm_cv[i]; return 1; } return 0; }
/* CMillToolListCtrl::ReadRegistrySettings 0x18050a3f0 for one list's key (tm_get: the file as read) */
static void cs_read(const char *key, TmColSet s3[3], int *listview) {
    int ok = 1, x; char nm[160];
    cs_reset(s3, listview);
    snprintf(nm, sizeof nm, "%s\\List Style", key);
    if (!tm_get(nm, &x) || !(x == 0 || x == 1 || x == 3)) ok = 0; else *listview = x;   /* valid 0, 1, 3 */
    for (int m = 0; ok && m < 3; m++) {
        TmColSet *s = &s3[m];
        snprintf(nm, sizeof nm, "%s\\DisplayMode%d  SortInfo", key, m);   /* "DisplayMode%d " + "%s SortInfo" */
        if (!tm_get(nm, &x) || abs(x) < 1 || abs(x) > 12) { ok = 0; break; }
        s->sortcol = abs(x); s->sortasc = x > 0;
        for (int c = 0; ok && c < TM_COLS; c++) {
            int o = 0, vi = 0, w = 0;
            snprintf(nm, sizeof nm, "%s\\DisplayMode%d  Column%dOrder", key, m, c);   if (!tm_get(nm, &o) || o < 0 || o > 12) ok = 0;
            snprintf(nm, sizeof nm, "%s\\DisplayMode%d  Column%dVisible", key, m, c); if (!tm_get(nm, &vi) || (vi != 0 && vi != 1)) ok = 0;
            snprintf(nm, sizeof nm, "%s\\DisplayMode%d  Column%dWidth", key, m, c);   if (!tm_get(nm, &w) || w < 0) ok = 0;
            if (ok) { s->ord[c] = o; s->vis[c] = vi; s->w[c] = w; }
        }
    }
    if (!ok) cs_reset(s3, listview);                    /* any missing or invalid value: the defaults */
}
/* CMillToolListCtrl::WriteRegistrySettings 0x18050b870 for one list's key */
static void cs_write(FILE *f, const char *key, const TmColSet s3[3], int listview) {
    fprintf(f, "%s\\List Style=%d\n", key, listview);
    for (int m = 0; m < 3; m++) {
        const TmColSet *s = &s3[m];
        fprintf(f, "%s\\DisplayMode%d  SortInfo=%d\n", key, m, s->sortasc ? s->sortcol : -s->sortcol);
        for (int c = 0; c < TM_COLS; c++)
            fprintf(f, "%s\\DisplayMode%d  Column%dOrder=%d\n%s\\DisplayMode%d  Column%dVisible=%d\n%s\\DisplayMode%d  Column%dWidth=%d\n",
                    key, m, c, s->ord[c], key, m, c, s->vis[c], key, m, c, s->w[c]);
    }
}
static void cs_load(const char *file) {       /* FILE in our config dir as name = value pairs (tm_get) */
    char p[1100]; config_file(p, sizeof p, file);
    int cap = 0; tm_cn = 0;
    FILE *f = fopen(p, "r"); char line[256];
    while (f && fgets(line, sizeof line, f)) {
        char *eq = strrchr(line, '='); if (!eq) continue; *eq = 0;
        if (tm_cn == cap) { cap = cap ? cap * 2 : 256; tm_ck = realloc(tm_ck, cap * sizeof *tm_ck); tm_cv = realloc(tm_cv, cap * sizeof *tm_cv); }
        static const char *const was[3][2] = { { "Part Tool List\\", "Part Tool List\\" }, { "Library Tool List\\", "Lib Tool List\\" },
                                               { "Tool Manager ", "" } };   /* files written before the key names were decoded */
        char nm[96]; snprintf(nm, sizeof nm, "%s", line);
        for (int w = 0; w < 3; w++) { size_t l = strlen(was[w][0]);
            if (!strncmp(line, was[w][0], l)) { snprintf(nm, sizeof nm, TM_KEY "\\%s%s", was[w][1], w == 2 ? line : line + l); break; } }
        snprintf(tm_ck[tm_cn], 96, "%s", nm); tm_cv[tm_cn] = atoi(eq + 1); tm_cn++;
    }
    if (f) fclose(f);
}
static void cs_unload(void) { free(tm_ck); free(tm_cv); tm_ck = NULL; tm_cv = NULL; tm_cn = 0; }
void tm_settings_read(void) {                  /* FUN_18087c200 + CMillToolListCtrl::ReadRegistrySettings 0x18050a3f0 */
    cs_load("toolmanager.conf");
#define TM_GET(name, out) tm_get(name, out)
    int x;
    tm_view = 0; if (TM_GET(TM_KEY "\\Tool Manager Mode", &x) && x >= 0 && x <= 2) tm_view = x;           /* accepted if <= 2, default 0 */
    tm_dispmode = 2; if (TM_GET(TM_KEY "\\Tool Manager Display Mode", &x) && x >= 0 && x < 3) tm_dispmode = x;   /* accepted if < 3, default 2 */
    for (int L = 0; L < 2; L++) cs_read(tm_regkey[L], tm_cs[L], &tm_listview[L]);
#undef TM_GET
    cs_unload();
    tm_colinit = 1;
}
void tm_settings_write(void) {                 /* FUN_18087d480 "CToolListBoth::WriteSettings" + WriteRegistrySettings 0x18050b870 */
    if (!tm_colinit) tm_settings_read();
    char p[1100]; config_dir_make(p, sizeof p); config_file(p, sizeof p, "toolmanager.conf");
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, TM_KEY "\\Tool Manager Mode=%d\n" TM_KEY "\\Tool Manager Display Mode=%d\n", tm_view, tm_dispmode);
    for (int L = 0; L < 2; L++) cs_write(f, tm_regkey[L], tm_cs[L], tm_listview[L]);
    fclose(f);
}
/* The op dialog Tool page's list 25463 (decomp/mccore_toolpage.c): ToolPageBase +0x15a8 is a CMillToolListCtrl of its own (ctor
   0x180881e30 -> the default ctor 0x180505e90: display mode +0x1e3a0 = 2, InitSorting(0..2, ascending, column 2), +0x6a9 = 1).
   The page never changes its display mode, so set 2 is the active one (GetActiveColumnSet), as for the Tool Manager's part list.
   InitPage 0x1808864a0 -> ReadToolIconPrm 0x18088ac60 (before FillList): ReadRegistrySettings(REG_KEY\\m_sMainGUIKey\\
   m_sMainDialogKey\\m_sToolListKey); mccore's static initialisers set those to "GUI" (0x180004244), "Mill Tool Display"
   (0x180004214), "Tool List Settings" (0x180004274) - not the Tool Manager's keys. InitPage sets page +0x1570 = 1 last;
   UpdateLocalOp 0x18088d2a0 (the dialog's OK) with +0x1570 set -> WriteToolIconPrm 0x18088ddc0 -> WriteRegistrySettings under the
   same key. ToolPageBase's message map (0x180df98c0): WM_NOTIFY LVN_COLUMNCLICK from 25463 -> OnListClickColumn 0x180889340 =
   SortList(NMLISTVIEW iSubItem) (the same toggle as the Tool Manager's); FillList ends in SortList() (SortColumn 0x18050b060).
   The list's own HDN_ITEMCLICKA handler OnHeadingItemclick 0x509f60 is the same open question as in the Tool Manager
   (formats/toolmanager_ui.md 1.5.3): not used here either. Stored in toolpage.conf (never Mastercam's registry). */
#define TP_KEY "GUI\\Mill Tool Display\\Tool List Settings"
TmColSet tp_cs[3]; int tp_listview = 1, tp_list_init;
void tp_list_open(void) { tp_list_init = 0; }
static void tp_list_read(void) { cs_load("toolpage.conf"); cs_read(TP_KEY, tp_cs, &tp_listview); cs_unload(); tp_list_init = 1; }
void tp_list_ok(void) {
    if (!tp_list_init) return;                        /* the page was not made (+0x1570 clear) */
    char p[1100]; config_dir_make(p, sizeof p); config_file(p, sizeof p, "toolpage.conf");
    FILE *f = fopen(p, "w"); if (!f) return;
    cs_write(f, TP_KEY, tp_cs, tp_listview);
    fclose(f);
}
void tp_sortlist(int c) { if (!tp_list_init) tp_list_read(); tm_sortset(&tp_cs[2], c); }
int tp_list_cols(int *cols, int *w) {
    if (!tp_list_init) tp_list_read();
    const TmColSet *cs = &tp_cs[2]; int nc = 0;
    for (int k = 0; k < TM_COLS; k++) { int c = cs->ord[k]; if (c >= 2 && cs->vis[c]) { cols[nc] = c; w[nc] = cs->w[c] > 4 ? cs->w[c] : 4; nc++; } }
    return nc;
}
/* the CMillToolListCtrl defaults (ctor 0x180505e90 + InitColumns 0x1805097e0 -> ResetListCtrlColumns 0x18050a7f0) for a list that
   reads no settings (ECP's tool list 395, cui_ecp.c); LIST6A9 = list +0x6a9: 0 hides column 11 in every set */
void tm_cs_default(TmColSet s3[3], int list6a9) {
    int lv; cs_reset(s3, &lv);
    if (!list6a9) for (int m = 0; m < 3; m++) s3[m].vis[11] = 0;
}

/* N13-32 (decoded, no deviation): the Tool List 6410's list 1762 is a CMillToolListCtrl of its own (CToolList +0x1c60, ctor
   0x180869260 -> the default ctor 0x180505e90: +0x6a8 = 0x100, i.e. +0x6a9 = 1 = Rad. Type shown). OnInitDialog 0x18086c4b0 ->
   FUN_18086d050 (before FillList): "Tool Manager Display Mode" from REG_KEY\GUI\Mill Tool Library Selection (accepted if < 3, else
   2 - this dialog's own key, not the Tool Manager's) and ReadRegistrySettings(...\Tool List Settings); mccore's static
   initialisers 0x180004080 / 0x180004050 / 0x1800040b0 set DAT_181431620 / 28 / 30 = "GUI", "Mill Tool Library Selection",
   "Tool List Settings". SetListDisplayMode(that mode), FillList(-1). CToolList's message map (0x180df6000): WM_NOTIFY
   LVN_COLUMNCLICK from 1762 -> 0x18086bdb0 = SortList(NMLISTVIEW iSubItem) on +0x1c60 (the Tool page's toggle). Its DestroyWindow
   (0x18086a0a0, so OK and Cancel alike) -> FUN_18086dd30 "CToolList::WriteParameters": the display mode and
   WriteRegistrySettings under the same keys. Ours: toollist.conf (never Mastercam's registry). */
#define TL_KEY "GUI\\Mill Tool Library Selection"
TmColSet tl_cs[3]; int tl_listview = 1, tl_dispmode = 2, tl_list_init;
static void tl_list_read(void) {
    cs_load("toollist.conf"); int x;
    tl_dispmode = 2; if (tm_get(TL_KEY "\\Tool Manager Display Mode", &x) && x >= 0 && x < 3) tl_dispmode = x;
    cs_read(TL_KEY "\\Tool List Settings", tl_cs, &tl_listview); cs_unload(); tl_list_init = 1;
}
static void tl_list_write(void) {
    if (!tl_list_init) return;
    char p[1100]; config_dir_make(p, sizeof p); config_file(p, sizeof p, "toollist.conf");
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, TL_KEY "\\Tool Manager Display Mode=%d\n", tl_dispmode);
    cs_write(f, TL_KEY "\\Tool List Settings", tl_cs, tl_listview);
    fclose(f);
}
void tl6410_show(void) { tl6410_open = 1; tl6410_sel = -1; tl_list_init = 0; }   /* a new CToolList: OnInitDialog reads the settings */
int tl6410_dispmode(void) { if (!tl_list_init) tl_list_read(); return tl_dispmode; }
const TmColSet *tl6410_set(void) { if (!tl_list_init) tl_list_read(); return &tl_cs[tl_dispmode]; }   /* GetActiveColumnSet */
void tl6410_sortlist(int c) { tl6410_set(); tm_sortset(&tl_cs[tl_dispmode], c); }
int tl6410_cols(int *cols, int *w) {
    const TmColSet *cs = tl6410_set(); int nc = 0;
    for (int k = 0; k < TM_COLS; k++) { int c = cs->ord[k]; if (c >= 2 && cs->vis[c]) { cols[nc] = c; w[nc] = cs->w[c] > 4 ? cs->w[c] : 4; nc++; } }
    return nc;
}
/* FillList's rows: the library's tools (no bare-tool rows in display mode 1, as tm_rows; the filter is not ported), in SortColumn order */
int tl6410_rows(int *idx, int cap) {
    int n = 0; const TmColSet *s = tl6410_set();
    for (int i = 0; i < tm_count(1) && n < cap; i++) if (tl_dispmode != 1) idx[n++] = i;
    tm_sort_rows(1, s, idx, n);
    return n;
}

/* SetColumnVisibilityOnData 0x18050aa70: hiding sets width 0 and moves the column to order slot 1; showing gives it the
   default width and moves it to the last order slot */
void tm_col_toggle(int L, int c) {
    TmColSet *s = TM_SET(L); int pos = 0;
    for (int k = 0; k < TM_COLS; k++) if (s->ord[k] == c) pos = k;
    memmove(s->ord + pos, s->ord + pos + 1, (TM_COLS - 1 - pos) * sizeof(int));
    if (s->vis[c]) { s->vis[c] = 0; s->w[c] = 0; memmove(s->ord + 2, s->ord + 1, (TM_COLS - 2) * sizeof(int)); s->ord[1] = c; }
    else { s->vis[c] = 1; s->w[c] = tm_defw[c]; s->ord[TM_COLS - 1] = c; }
}

/* FormatReal(v, 1, b, 0) -> fmt_real "%.*lf" with n_places_nci, trailing '0' stripped; a final '.' removed (b = 0) or followed
   by "0" (b = 1). n_places_nci's value here is NOT DECODED: the configured places (cfg_places, default 4) are used. */
void tm_real(char *o, size_t n, double v, int b) {
    if (fabs(v) >= 1e38) { snprintf(o, n, "$$$$$$$$$$$$$$$$$$$$"); return; }
    snprintf(o, n, "%.*f", cfg_places >= 0 ? cfg_places : 4, v);
    if (strchr(o, '.')) { size_t l = strlen(o); while (l && o[l - 1] == '0') o[--l] = 0; }
    size_t l = strlen(o); if (l && o[l - 1] == '.') { if (b) snprintf(o + l, n - l, "0"); else o[l - 1] = 0; }
}
double tm_info_pitch(const Tool *t) {        /* op_tool_info +0x18: metric ThreadPitch, inch threads per inch (toollib_net.md 3.5) */
    return t->has_extras && !t->is_metric && t->pitch > 0 ? 1.0 / t->pitch : t->pitch;
}
void tm_cell(const Tool *t, int col, char *o, size_t n) {   /* FillList 0x180506ca0 */
    char a[48], b[48];
    switch (col) {
    case 2: snprintf(o, n, "%3d", t->num); break;
    case 3: case 5: snprintf(o, n, "  --  "); break;   /* bare tool (no assembly objects in this program: formats/part_tools.md) */
    case 4: snprintf(o, n, "%.120s", t->name); break;
    case 6: tm_real(a, sizeof a, t->dia, 1);
        if (t->type == 4 || t->type == 5) {            /* info+0x18 = the record's pitch field (inch: threads per inch) */
            double p = tm_info_pitch(t);
            tm_real(b, sizeof b, floor((p + 0.005) * 100) / 100, 0); snprintf(o, n, "%s-%s", a, b);
        } else if (t->type == 12 || t->type == 16 || t->type == 17 || t->type == 21) { tm_real(b, sizeof b, t->tip_angle, 0); snprintf(o, n, "%s-%s", a, b); }
        else snprintf(o, n, "%s", a);
        break;
    case 7: tm_real(o, n, t->corner_r, 1); break;
    case 8: tm_real(o, n, t->flute_len, 1); break;     /* TlToolMill+0x800 = FL */
    case 9: snprintf(o, n, "%hi", (short)t->flutes); break;
    case 10: snprintf(o, n, "%s", tool_type_name(t->type)); break;   /* tl_string_chooser(type, 8): the mill map = toollib.c type_names (mccoreres 5014..5412); 0x40 (router) would give 3970.. */
    case 11: snprintf(o, n, "%s", t->rad_type == 1 ? "Corner" : t->rad_type == 2 ? "Full" : "None"); break;   /* 3798 + info+6 */
    case 12: tm_real(o, n, t->oal, 1); break;          /* TlToolMill+0x7f0 = OAL */
    default: o[0] = 0;
    }
}
const Tool *tm_tool(int L, int i) { return L == 0 ? &F.tools[i] : &tlib.t[i]; }
int tm_count(int L) { return L == 0 ? F.ntools : (tlib_ok ? tlib.n : 0); }

/* ---- the current tool library, adding a tool to the part (was viewer/mcamui.c) ---- */
void tm_open_lib(const char *path) {
    ToolLib nl; char err[300];
    if (!toollib_load(path, &nl, err, sizeof err)) { message("Tool library", "%s", err); return; }
    if (tlib_ok) toollib_free(&tlib);
    tlib = nl; tlib_ok = 1; if (cui_hooks.lib_opened) cui_hooks.lib_opened();   /* the Tool Manager's library list state (mcamui: tm_lib_sel = -1, tm_bup_done = 0, tm_sel[1] cleared) */
    /* SetToolLibrary says nothing (the dialog's library label changes); load errors above are our own text */
}
/* the starter library shipped with the program (<ui dir>/starter_tools_mm.tools: standard metric drills, end mills,
   taps - our own data), opened when no Mastercam tool library is reachable (user request 2026-10-21) */
void starter_lib_open(void) {
    char p[700]; snprintf(p, sizeof p, "%s" PATH_SEP_S "starter_tools_mm.tools", cui_ui_dir);
    if (file_exists(p)) tm_open_lib(p);
}

int next_tool_num(void) { int n = 1; for (int i = 0; i < F.ntools; i++) if (F.tools[i].num >= n) n = F.tools[i].num + 1; return n; }

int part_add_tool(const Tool *t) {              /* copy into the part's tool list; renumbers on a clash */
    if (F.ntools >= 256) { message("Tools", "The tool list is full (256 tools)."); return -1; }
    /* an inch tool in a metric part (or the reverse): Mastercam's conversion is NOT DECODED (formats/toollib_net.md), so
       it is refused rather than copied with its numbers read in the wrong units */
    if ((t->is_metric != 0) != mcam_is_metric(&F)) {   /* IsMetric(): the system units, known for every part (mcam_load.h) */
        message("Tools", "%s is %s and the part is %s. Use a tool library in the part's units.", t->name[0] ? t->name : "This tool",
                t->is_metric ? "metric" : "inch", mcam_is_metric(&F) ? "metric" : "inch");
        return -1;
    }
    Tool *d = &F.tools[F.ntools]; tool_copy(d, t);
    /* TpPartToolList::PrepareNewToolByMachine / PrepareNewAssemblyByMachine (mccore_tooladd.c l.6819 / 6942): the machine
       group's +0x11b8 (Tool Settings 1239 "Assign tool numbers sequentially", cui_grpprops.c) set -> number 1, then
       OpenToolNumber (here: the lowest number no part tool has; OpenToolNumber's search beyond its start NOT DECODED); else
       the library's number (+0x11bc's WarnOfDuplicateToolNumbers NOT DECODED: renumbered on a clash as before) */
    int lib_num = d->num;
    if (grp_assign_seq()) { int n = 1; while (mcam_tool(&F, n)) n++; d->num = n; }
    else if (d->num <= 0 || mcam_tool(&F, d->num)) d->num = next_tool_num();
    /* Cnc::Tool::tp_tool_offset (mccore 0x1800dc270, formats/toolmanager_ui.md "D/H offsets"), run after the numbering: when
       CCNCMachine::AddToToolRegister(1) (decomp/mdef_addtotoolregister.c: the control's mill tool registers +0) is 0, length
       offset = LOfsAmt(1) + number and diameter offset = DOfsAmt(1) + number; 1 or no machine: the tool's own. Here only for a
       tool whose number changed (the library's D / H otherwise stay, as before; the GetProdEnableBits() & 6 gate: NOT DECODED,
       taken clear). The machine: the part's, else the defaults library's (tl6410_mach). Posting is unaffected (H0). */
    if (d->num != lib_num) {
        const McamFile *m = F.ctl_found ? &F : tl6410_mach();
        if (m && m->ctl_found && m->ctl_addreg == 0) { d->len_off = m->ctl_lofs + d->num; d->dia_off = m->ctl_dofs + d->num; }
    }
    if (d->dia_off <= 0) d->dia_off = d->num;          /* D / H = tool number: all 114 tools in the user's parts (2026-10-15) */
    if (d->len_off <= 0) d->len_off = d->num;
    doc_dirty = 1;
    return F.ntools++;
}

/* ---- Tool page and the Tool List dialog 6410 behind "Select library tool..." ----
   Decompiles: decomp/mccore_toolpage.c (mccore ToolPage / ToolPageHEM / ToolPageBase), decomp/mccore_ctoollist.c +
   mccore_selmillasm.c (CToolList, dialog 6410), decomp/mdef_toollibfile.c / mdef_turret.c (mcmachinedef CCNCMachine).
   Which template: the mcmill page factories (decomp/mcmill_all.c FUN_18026c800 case 2, FUN_18026f760, FUN_18026e210)
   make ToolPageHEM (ctor 0x18087e850: mccoreres dialog 25524 = 0x63b4) for opcodes 3 / 0x66 / 0x69 (Pocket, Facing, Slot),
   0x12 (Circle Mill) and every 2D High Speed op (0x86), and ToolPage (ctor 0x18087e810: dialog 6556 = 0x199c) for the
   rest (Contour, Drill, Thread Mill, Helix Bore). Both share ToolPageBase's controls (DoDataExchange 0x180883180, op =
   dialog +0x20f0): list 25463, tool name 3565 (op +0x316, tool info +0x146 shown when LocalOp), number 3566 (tool info
   +0), head 3568 (op +0x53c), length / diameter offset 3567 / 3569 (+0x31c / +0x318), diameter 3561 / corner radius 3563
   (+0x2e8 / +0x2f0), feed 3575 / plunge 3579 / retract rate 3581 / spindle speed 3577 (+0x310 / +0x318 / +0x320 /
   +0x328 sign = direction), FPT 3747 / SFM 3743 (dialog values), Spindle direction 3207, Force tool change 25196 (op
   +0x289), Rapid Retract 3305 (op +0x141), To batch 25601 (op +0x291), Comment 3583 (op +6, EM_LIMITTEXT 0x77). */
int tl6410_open, tl6410_sel = -1;
/* batch 12: the Tool page's Filter Active 25263 / Filter... 3056 (formats/opdlg_port_audit.md 18). ToolPageBase ctor 0x180881e30: page
   +0x1584 (the check's DDX, DoDataExchange 0x180883180) = DAT_1814317d1 - a byte of mccore's zero-initialised data (MemDump 2026-10-24:
   0), so the box starts unchecked and keeps its state between dialogs (OnFilterActiveCb 0x180888370 writes it back, then
   CMillToolListCtrl::FillList(-1)). Filter... = OnToolFilterPb 0x18088a010: the list's CToolFilt (page +0x1c60, its DoModal = vtable
   +0x2f0) == 1 (OK) -> Filter Active = 1 and FillList(-1). That CToolFilt reads the same prm as the Tool Manager's (ReadPrm, e.g.
   after RunToolManager 0x18088c000): here the one settings set tmf of the Tool Manager's Tool List Filter (cui_toolmgr.c) and its
   TestToolOk rules for a part list (tm_filter_test L = 0). The rows in the order of the page's own column set (tp_cs above: FillList's
   SortList() with SortColumn 0x18050b060, the Tool Manager's tm_cmp). */
int tp_factive, tp_filt_open;
int tp_rows(int *idx, int cap) {
    int n = 0;
    for (int i = 0; i < F.ntools && n < cap; i++) if (!tp_factive || tm_filter_test(0, &F.tools[i])) idx[n++] = i;
    if (!tp_list_init) tp_list_read();
    tm_sort_rows(0, &tp_cs[2], idx, n);
    return n;
}
int otp_wait_new = -1;                         /* AddNewTool: F.ntools before the wizard; the new tool gets selected */
uint8_t tp_rapid, tp_ftc, tp_batch;            /* record 0x1a9 / 0x2f1 / 0x2f9 (op +0x141 / +0x289 / +0x291) */
int tp_metric(void) { return mcam_is_metric(&F); }   /* IsMetric() (mcam_load.h) */
int tp_hem(const CamParams *p) {               /* ToolPageHEM (25524) or ToolPage (6556), see above */
    int t = p->type; return t == CAM_POCKET || t == CAM_FACE || t == CAM_SLOT || t == CAM_CIRCLE || t == CAM_DYNAMIC || t == CAM_AREA || t == CAM_DYNCONTOUR || t == CAM_PEEL;
}
/* the record bytes of the page's switches: a stored op's record / the defaults op a new one is copied from
   (mcam_saveops.c), then this program's own record patches (an op made here, edited) */
void tp_from_record(const uint8_t *r, uint32_t n) {
    tp_rapid = n > 0x1a9 ? r[0x1a9] : 0; tp_ftc = n > 0x2f1 ? r[0x2f1] : 0; tp_batch = n > 0x2f9 ? r[0x2f9] : 0;
}
void tp_from_rpatch(const CamParams *p) {
    for (int k = 0; k < p->nrpatch; k++) if (p->rpatch[k].n == 1) {
        if (p->rpatch[k].off == 0x1a9) tp_rapid = p->rpatch[k].v[0];
        if (p->rpatch[k].off == 0x2f1) tp_ftc = p->rpatch[k].v[0];
        if (p->rpatch[k].off == 0x2f9) tp_batch = p->rpatch[k].v[0];
    }
}
void tp_rp(CamParams *p, int off, const void *v, int n) {     /* a record patch that is not a height (cam_fileop.c rpatch_set) */
    int k = 0; while (k < p->nrpatch && p->rpatch[k].off != off) k++;
    if (k == p->nrpatch) { if (p->nrpatch >= CAM_NRPATCH) { cam_rpatch_full(off); return; } p->nrpatch++; }
    p->rpatch[k].off = (uint16_t)off; p->rpatch[k].n = (uint8_t)n; p->rpatch[k].height = 0; memcpy(p->rpatch[k].v, v, n);
}
/* ToolPageBase::MapToOp (0x180886e20) at OK: rapid retract, force tool change, batch and the retract rate into the record
   (the feed / plunge / rpm go with the op's tool block, mcam_saveops.c); the motion takes rapid retract as params_from_record
   does (cam_fileop.c: feed_retract = !0x1a9, mc_rapid_ret = 0x1a9) */
void tp_apply(CamParams *p) {
    tp_rp(p, 0x1a9, &tp_rapid, 1); tp_rp(p, 0x2f1, &tp_ftc, 1); tp_rp(p, 0x2f9, &tp_batch, 1);
    if (p->mc_rrate > 0) tp_rp(p, 0x388, &p->mc_rrate, 8);
    p->mc_rapid_ret = tp_rapid != 0; p->feed_retract = !tp_rapid;
}
/* CalculateFPT 0x180882ee0: z = 1 for tool types 4 / 5 (taps), else the tool's flutes (tool info +0x52); FPT = feed /
   (z x |rpm|) when rpm > 0; a tap with pitch > tol: metric tool the pitch, inch tool 1 / (tool info +0x18 = threads per
   inch) = the pitch again (mcam_load.c keeps Tool.pitch as the pitch). CalculateSFM 0x180882fb0: |rpm| x D / (metric
   part 318.3, else 3.82). A zero flute count (Mastercam divides by it) gives 0 here. */
int tp_z(const Tool *t) { return t && (t->type == 4 || t->type == 5) ? 1 : t ? t->flutes : 0; }
double tp_fpt(const CamParams *p, const Tool *t) {
    double f = 0; int z = tp_z(t), r = abs(p->rpm);
    if (r > 0 && z > 0) f = p->feed / ((double)z * r);
    if (t && (t->type == 4 || t->type == 5) && t->pitch > cfg_sys_tol) f = t->pitch;
    return f;
}
double tp_sfm(const CamParams *p) { return (double)abs(p->rpm) * p->tool_dia / (tp_metric() ? 318.3 : 3.82); }

/* Tool List (mccoreres 6410, 440 x 231 du; CToolList, ctor 0x180869260 = MCDialog(0x190a), run by FUN_18086e220 with
   +0x230a = "library" flag). OnInitDialog 0x18086c4b0: label 10253 = CCNCMachine::ToolLibFile(machine, Units())
   (machine +0xb26 inch / +0xc2b metric: McamFile.toollib, relative to the shared folder as measured on the defaults
   files), list = ToolLibrary(NULL) -> TlToolLibraryX::GetTools (FUN_18086a480), i.e. the current tool library: the one
   open in the Tool Manager, else the machine's library is opened. Filter Active 25194 = 1 (dlg +0x20230, FUN_18086a480);
   the CToolFilt filter itself is NOT PORTED (all tools listed). Display mode 10407-10410 = registry "Tool Manager
   Display Mode" (FUN_18086d050, shared with the Tool Manager: tm_dispmode), greyed here. "%d of %hd tools displayed" =
   mccoreres 3805 (FUN_18086d2f0). OK: the tool goes to the part (part_add_tool numbering) and the op. */
/* the machine: the part's machine record, else (a new part: init_db leaves no machine group; CToolList's ctor takes
   OMgetActiveMachine = the default machine, config 25000) the machine record of the operation defaults library
   (config 2303) - the base a new part is saved from, whose machine group new_base (mcam_saveops.c) checks to be config
   25000's machine. (The shipped .mcam-mmd files hold X5-era "%MCAMDIR%...TOOLS-5" names whose 2017 conversion is NOT
   DECODED, so they are not read.) */
static McamFile tlm_[2]; static int tlm_tried[2], tlm_ok[2];
void tl6410_mach_reset(void) {   /* Q19 (ours): the defaults file was saved - read again */
    for (int k = 0; k < 2; k++) { if (tlm_tried[k]) mcam_free(&tlm_[k]); tlm_tried[k] = tlm_ok[k] = 0; }
}
const McamFile *tl6410_mach(void) {
    if (F.mach_ok) return &F;
    McamFile *m = tlm_; int *tried = tlm_tried, *ok = tlm_ok; int k = tp_metric();
    if (!tried[k]) { tried[k] = 1; char v[1100], hp[1100], rd[1100]; mcam_config_value(k, 2303, v, sizeof v);   /* Q19: the saved copy (paths.h) */
        if (v[0]) ok[k] = mcam_load(&m[k], defaults_read(host_path(v, hp, sizeof hp), rd, sizeof rd)) && m[k].mach_ok; }
    return ok[k] ? &m[k] : NULL;
}
const char *tl6410_libfile(void) { const McamFile *m = tl6410_mach(); return m ? m->toollib[tp_metric()] : ""; }
/* the file: "%MCAMDIR%" (the X-era shared-folder macro; its 2017 expansion is NOT DECODED) and a relative name are taken
   relative to the shared folder, where the named libraries are (measured); a drive path as written */
void tl6410_machine_lib(char *out, size_t n) {
    const char *m = tl6410_libfile(); out[0] = 0;
    if (!m[0]) return;
    if (path_is_windows(m) && isalpha((unsigned char)m[0]) && m[1] == ':') { host_path(m, out, n); return; }
    if (!strncasecmp(m, "%MCAMDIR%", 9)) m += 9;
    char t[1200]; snprintf(t, sizeof t, "<APP_DIR_SHARED>%s", m); host_path(t, out, n);
}

/* hooks for cui_ops.c (open_params / open_op_params / generate_pending): the Tool page's record switches at open (stored op / defaults op
   record, then an op's own patches) and at OK */
void pp_tp_record(const uint8_t *r, uint32_t n) { tp_from_record(r, n); }
void pp_tp_op(const CamOp *op) { tp_from_rpatch(&op->p); }
void pp_tp_apply(void) { tp_apply(&pending.p); }

/* ---- the Tool page's controls (was opparams_ui.inc page_tool): template positions in dialog units, in the order the
   page draws them; dialog 25524 (ToolPageHEM) or 6556 (ToolPage) by tp_hem. ref = the control whose enabled state a
   label follows (0 = always enabled, -1 = always greyed). */
int tp_ctls(TpCtl *o, int cap) {
    const CamParams *p = &pending.p; int hem = tp_hem(p), drill = p->type == CAM_DRILL, n = 0;   /* InitControls 0x180885cc0: op_is_drill (opcode 2) */
    #define C(K_, ID_, X_, Y_, W_, H_, T_, REF_) do { if (n < cap) { TpCtl *c_ = &o[n++]; c_->kind = K_; c_->id = ID_; c_->x = X_; c_->y = Y_; c_->w = W_; c_->h = H_; c_->text = T_; c_->ref = REF_; } } while (0)
    C(TPK_LIST, 25463, 7, 17, 209, hem ? 159 : 162, "", 0);   /* 25463: 25524 7,17 209x159; 6556 7,17 209x162 */
    /* tool fields: ToolPageBase::OnKillfocusToolDiameterEdit / ...CornerRadiusEdit / ...NameEdit / ...NumEdit write the part's tool
       (UpdateDbTool) - not ported: shown read-only; the tool is edited with "Edit tool..." in the list's menu. Batch 12 decoded the
       chain (decomp/b12/acp.c, acp2.c): UpdateDbTool 0x18088ce40 -> RunDBToolUpdate 0x18088b4d0 (a clone of the assembly with the
       dialog's number +0x130 / corner radius (slot 0x108) / diameter +0x808 / name) -> Cnc::Tool::AssemblyChangedProcessor
       (vtable 0x180e4cf20 slot 0 = FUN_180a449c0): ops using the tool (list_op_by_toolID) 0 or 1 -> the tool updated in place
       (FUN_180a44f90), more -> TlUpdateAssemblyDlg (mccoreres 10293 "Assembly Has Changed": 10305 Update assembly and all
       operations / 10306 Create a new assembly / 10307 Cancel; results 0 / 1 / 2), an op in the Multi-Threading Manager ->
       message 5327; a Cancel of the toolpath dialog then restores the tool's offsets / diameter / corner radius
       (ToolpathDlg::RestoreOperation -> RestoreLoffDoffDiaCornerRad 0x180895950). Still NOT DECODED: the tool system's slots
       (0x10 add, 0x58 update, 0x118 assign, 0x200, 600 next number), FUN_180a45440 (what the update does to the other ops), the
       number edit's library branch (GetToolFromLibraryByToolNo -> AdjustAssemblyTool) and 10293's radio -> result order; an update
       here would regenerate the user's stored ops (regen_ops_for_tool), which a Cancel could not take back - so read-only. The
       head number edit does nothing (OnKillfocusHeadNumEdit 0x180888bb0 is empty); length / diameter offset 3567 / 3569 go to the op's
       tool info at MapToOp only (H / D words: G43 H0 rule) - read-only. */
    C(TPK_LABEL, 0, 221, 9, 58, 10, "Tool diameter:", 0); C(TPK_RO, 3561, 282, 6, 40, 14, "", 0);
    C(TPK_LABEL, 0, 221, 27, 58, 10, "Corner radius:", drill ? -1 : 0); C(TPK_RO, 3563, 282, 24, 40, 14, "", 0);
    C(TPK_DIV, 3605, 229, 38, 197, 0, "", 0);                                  /* 3605 UIDivider */
    C(TPK_LABEL, 0, 221, 50, 58, 10, "Tool name:", 0); C(TPK_RO, 3565, 282, 47, 144, 14, "", 0);
    C(TPK_LABEL, 0, 221, 68, 58, 10, "Tool number:", 0); C(TPK_RO, 3566, 282, 65, 40, 14, "", 0);
    C(TPK_LABEL, 0, 221, 86, 58, 10, "Head number:", 0); C(TPK_RO, 3568, 282, 83, 40, 14, "", 0);   /* op +0x53c; its value for a new op NOT DECODED (0 shown) */
    C(TPK_LABEL, 0, 328, 68, 55, 10, "Length offset:", 0); C(TPK_RO, 3567, 386, 65, 40, 14, "", 0);
    C(TPK_LABEL, 0, 328, 86, 55, 10, "Diameter offset:", drill ? -1 : 0); C(TPK_RO, 3569, 386, 83, 40, 14, "", 0);
    C(TPK_DIV, 3607, 224, 98, 202, 0, "", 0);                                  /* 3607 UIDivider */
    /* 5782 Recalculate values: shown with its text when the global HSTRecalculateValues is 0, else hidden (InitControls);
       the click posts UWM_TOOL_CHANGED to the dialog (OnRecalcValuesPb 0x1808894d0); batch 12: ToolpathDlg::OnToolChanged 0x180894a20
       posts it on to every made page (page +0x1570 set); the pages' own UWM_TOOL_CHANGED handlers (mcmill, e.g. the stepover / %
       of tool fields) are NOT DECODED: greyed */
    C(TPK_BTN, 5782, 340, 13, 70, 14, "Recalculate values", -1);
    /* Turret Index Position group 25587 (+25585 / 25586 bitmaps 0x61db / 0x61dc, 25588 / 25589): ShowTurretIndexPosCtrls
       0x18088cce0 shows it only when CCNCMachine::HasAlternateIndexPositionTurret (mcmachinedef 0x180069da0: machine
       Type +0xd8 == 2); a mill machine record has 0 (record +0x8f) -> hidden */
    if (tl6410_mach() && tl6410_mach()->mach_type == 2) {
        if (hem) { C(TPK_CHECK, 25588, 41, 136, 56, 10, "Left spindle", -1); C(TPK_CHECK, 25589, 140, 135, 58, 10, "Right spindle", -1); }   /* axis-combo logic NOT PORTED */
        else { C(TPK_CHECK, 25588, 261, 119, 56, 10, "Left spindle", -1); C(TPK_CHECK, 25589, 364, 119, 58, 10, "Right spindle", -1); }
    }
    /* ToolPageHEM::ShowHideHemCtrls 0x180881690 with prm_hem (op +0x1418 pocket / +0x1260 facing / +0x1280 slot /
       +0x12d8 circle / +0x1698 HST) flags [0] = IsHemAvail (HST style 0 / 2 and an Iscar tool: TlAssemblyItem
       manufacturer, not read here -> no), [1] HEM on, [2] RCTF, [3] = IsRctAvail && ![1] (GetHemCtrls 0x18087f6a0):
       Iscar HEM 10156 hidden, HEM Factor 10159 / 10160 / trackbar 10165 / "* Machine limited" 10207 hidden ([1] = 0),
       RCTF 10158 shown; batch 12: IsRctAvail 0x180880320 = the selected part tool is a mill tool (CMillToolListCtrl::GetToolBySlot)
       and not a TlToolEndmill-family tool (FUN_180528fd0 = dynamic_cast<TlToolEndmill>) whose radius type +0x978 is 3 (full
       radius); but what RCTF does (GetHemCtrls 0x18087f6a0 -> CalcHemFeedSpeed: the feed / rpm) is NOT DECODED -> greyed */
    if (hem) C(TPK_CHECK, 10158, 243, 144, 57, 10, "RCTF", -1);
    /* feeds: values in the op (feed / plunge / retract rate / rpm with the direction's sign); RunMachineDefChecks
       (machine feed / rpm limits) NOT PORTED */
    C(TPK_LABEL, 0, 221, 159, 58, 10, "Feed rate:", 0); C(TPK_EDIT, 3575, 281, 156, 40, 14, "", 0);
    C(TPK_LABEL, 0, 221, 177, 58, 10, "FPT:", 0); C(TPK_EDIT, 3747, 281, 174, 40, 14, "", 0);
    C(TPK_LABEL, 0, 221, 195, 58, 10, "Plunge rate:", drill ? -1 : 0); C(drill ? TPK_RO : TPK_EDIT, 3579, 281, 192, 40, 14, "", 0);   /* drill: disabled (InitControls) */
    C(TPK_LABEL, 0, 328, 142, 55, 10, "Spindle direction:", 0); C(TPK_COMBO, 3207, 386, 140, 40, 14, "", 0);
    C(TPK_LABEL, 0, 328, 159, 55, 10, "Spindle speed:", 0); C(TPK_EDIT, 3577, 386, 156, 40, 14, "", 0);
    C(TPK_LABEL, 0, 328, 177, 55, 10, tp_metric() ? "CS" : "SFM", 0); C(TPK_EDIT, 3743, 386, 174, 40, 14, "", 0);   /* InitPage: label 3741 = mccoreres 7570 "CS" (metric) / 7569 "SFM" */
    int rr_on = !tp_rapid && !drill;                      /* MapToDlg / OnRapidRetractCb: retract rate enabled when Rapid Retract is off */
    C(TPK_LABEL, 0, 328, 195, 55, 10, "Retract rate:", rr_on ? 0 : -1); C(rr_on ? TPK_EDIT : TPK_RO, 3581, 386, 192, 40, 14, "", 0);
    C(TPK_CHECK, 25196, 243, 210, 93, 10, "Force tool change", 0);           /* OnForceToolChangeCb */
    C(TPK_CHECK, 3305, 347, 210, 80, 10, "Rapid Retract", drill ? -1 : 0);   /* drill: disabled (InitControls) */
    C(TPK_DIV, 3609, 224, 222, 202, 0, "", 0);                                 /* 3609 UIDivider */
    C(TPK_LABEL, 0, 226, 230, 40, 10, "Comment", 0);                           /* 3583: the op comment (op +6, written to record +0x6e = pending.name) */
    C(TPK_TEXTBOX, 3583, 226, 240, 200, 36, "", 0);
    /* 3306 "Safety zone enabled": hidden at the end of InitPage (0x1808864a0) */
    C(TPK_LABEL, 0, 127, 183, 89, 10, "Right-click for options", 0);
    C(TPK_BTN, 3293, 7, 199, 66, 18, "Select library tool...", 0);           /* OnSelectLibToolPb */
    /* Filter Active 25263 / Filter... 3056 (OnFilterActiveCb / OnToolFilterPb 0x18088a010; batch 12: tp_factive / tp_rows above,
       the Tool Manager's Tool List Filter dialog 25519) */
    C(TPK_CHECK, 25263, 109, 203, 57, 10, "Filter Active", 0); C(TPK_BTN, 3056, 166, 199, 50, 18, "Filter...", 0);
    /* Tool inspection / change 3055 (checkable UIGroupbox): ShowToolInspectionGroup(1) only for opcode 0x84, multiaxis or
       HST style 1 (Area Mill) (InitPage / OnSetActive_Page); its values (op +0x1652 / +0x15d8 / +0x15e0 for 0x86) are not
       ported -> drawn greyed there, hidden for every other op */
    if (p->type == CAM_AREA) {
        C(TPK_GROUP, 3055, 7, 225, 209, 51, "Tool inspection / change", -1);
        C(TPK_LABEL, 0, hem ? 14 : 26, 247, hem ? 78 : 90, 10, "Force retract every", -1);
        C(TPK_CHECK, 0, 104, 238, 10, 10, "", -1); C(TPK_CHECK, 0, 104, 256, 10, 10, "", -1);
        C(TPK_LABEL, 0, 168, 237, 38, 10, tp_metric() ? "Millimeters" : "Inches", -1); C(TPK_LABEL, 0, 168, 256, 38, 10, "Minutes", -1);   /* InitPage: 7515 / 7514 */
    }
    C(TPK_CHECK, 25601, 7, 277, 45, 10, "To batch", 0);                       /* 25601 (hidden in SolidsWorks only) */
    #undef C
    return n;
}
const Tool *tp_tool(void) { return tool_choice >= 0 && tool_choice < F.ntools ? &F.tools[tool_choice] : NULL; }
/* the text a box shows (RO / EDIT), the formats of page_tool */
void tp_ctl_text(int id, char *v, size_t n) {
    const CamParams *p = &pending.p; const Tool *t = tp_tool(); v[0] = 0;
    switch (id) {
    case 3561: if (t) snprintf(v, n, "%.4f", t->dia); break;
    case 3563: if (t) snprintf(v, n, "%.4f", t->corner_r); break;
    case 3565: if (t) snprintf(v, n, "%s", t->name); break;
    case 3566: if (t) snprintf(v, n, "%d", t->num); break;
    case 3568: if (t) snprintf(v, n, "0"); break;
    case 3567: if (t) snprintf(v, n, "%d", t->len_off); break;
    case 3569: if (t) snprintf(v, n, "%d", t->dia_off); break;
    case 3575: snprintf(v, n, "%.4g", p->feed); break;
    case 3747: snprintf(v, n, "%.5g", tp_fpt(p, t)); break;
    case 3579: snprintf(v, n, "%.4g", p->plunge_feed); break;
    case 3577: snprintf(v, n, "%.0f", (double)abs(p->rpm)); break;
    case 3743: snprintf(v, n, "%.5g", tp_sfm(p)); break;
    case 3581: snprintf(v, n, "%.4g", p->mc_rrate); break;
    case 3583: snprintf(v, n, "%s", pending.name); break;
    }
}
int tp_ctl_check(int id) { return id == 25196 ? tp_ftc != 0 : id == 3305 ? tp_rapid != 0 : id == 25601 ? tp_batch != 0 : id == 25263 ? tp_factive != 0 : 0; }
int tp_ctl_combo(void) { return pending.p.rpm < 0; }   /* 3207: 0 CW, 1 CCW */
const char *const TP_DIR[2] = { "CW", "CCW" };        /* InitCombos 0x180885c40: mccoreres 3993 / 3994; GetDialogSpindleSpeed: CCW = -rpm */
/* an edit box committed (Enter / leaving it) with a plain number >= 0 (tw_edit_txt), a check box / the combo changed */
void tp_ctl_set(int id, double d) {
    CamParams *p = &pending.p; const Tool *t = tp_tool();
    switch (id) {
    case 3575: if (fabs(d - p->feed) > cfg_sys_tol) p->feed = d; break;   /* OnKillfocusFeedrateEdit: FPT follows (a tap's calc_tap_rpm NOT DECODED: rpm kept) */
    case 3747: { double fpt = tp_fpt(p, t); if (fabs(d - fpt) > 1e-12) p->feed = (double)abs(p->rpm) * tp_z(t) * d; } break;   /* OnKillfocusFPTEdit 0x1808888a0: feed = |rpm| x z x FPT */
    case 3579: if (p->type != CAM_DRILL) p->plunge_feed = d; break;
    case 3577: if (fabs(d) < 1e9) { int r = (int)d; p->rpm = p->rpm < 0 ? -r : r; } break;   /* OnKillfocusSpindleSpeedEdit: SFM / FPT follow */
    case 3743: {   /* OnKillfocusSFMEdit 0x180888d60: rpm = (uint)(k x SFM / D), feed = |rpm| x z x FPT */
        double sfm = tp_sfm(p);
        if (fabs(d - sfm) > 1e-12 && p->tool_dia > 0) {
            double fpt = tp_fpt(p, t); int r = (int)(unsigned)((tp_metric() ? 318.3 : 3.82) * d / p->tool_dia);
            p->rpm = p->rpm < 0 ? -r : r; p->feed = (double)abs(r) * tp_z(t) * fpt; } } break;
    case 3581: if (!tp_rapid && p->type != CAM_DRILL) p->mc_rrate = d; break;
    case 3207: if ((d != 0) != (p->rpm < 0)) p->rpm = -p->rpm; break;
    case 25196: tp_ftc = (uint8_t)(d != 0); break;
    case 3305: if (p->type != CAM_DRILL) tp_rapid = (uint8_t)(d != 0); break;
    case 25601: tp_batch = (uint8_t)(d != 0); break;
    case 3293: tl6410_show(); break;   /* OnSelectLibToolPb */
    case 25263: tp_factive = d != 0; break;               /* batch 12: OnFilterActiveCb */
    case 3056: tm_open_filter(); tp_filt_open = 1; break; /* batch 12: OnToolFilterPb (the Tool List Filter; its OK: tmf_ok) */
    }
}
void tp_set_comment(const char *s) { snprintf(pending.name, sizeof pending.name < 0x78 ? sizeof pending.name : 0x78, "%s", s); }
/* the list 25463: its own column set (tp_list_cols above; the Tool Manager's cell texts tm_cell). Left click selects (OnItemSelect: feeds / speeds from the source); a right click
   selects the row under it first (the list control's own behaviour) and opens menu 1247 */
int tm_list_cols(int L, int *cols, int *w) {
    if (!TM_SET(0)->w[2] && !TM_SET(1)->w[2]) tm_settings_read();   /* the Tool Manager's column sets, read once */
    TmColSet *cs = TM_SET(L); int nc = 0;
    for (int k = 0; k < TM_COLS; k++) { int c = cs->ord[k]; if (c >= 2 && cs->vis[c]) { cols[nc] = c; w[nc] = cs->w[c] > 4 ? cs->w[c] : 4; nc++; } }
    return nc;
}
void tp_pick(int i) { if (i >= 0 && i < F.ntools && i != tool_choice) { tool_choice = i; fs_from_source(&pending.p, &F.tools[i]); } }
/* AddNewTool (0x180882990): after a successful add FillList + SetSelectedTool(new slot); the front end calls this when
   the Define Tool wizard has closed */
void tp_new_tool_done(void) { if (otp_wait_new >= 0) { if (F.ntools > otp_wait_new) { tool_choice = F.ntools - 1; fs_from_source(&pending.p, &F.tools[tool_choice]); } otp_wait_new = -1; } }

/* menu 1247 (mccoreres; ToolPageBase::OnContextMenu 0x180887930 / ProcessContextMenu 0x18088a4e0). Removed as decoded:
   Get block drill 32974 (needs product bit 0x40 and opcode 0x132), Load tool on machine 33001 (CCNCMachine::
   FromMCExtensions, machine +0x1c8 = record +0x17f = 0), Edit Tool In New Editor 33069 / Save Tool To New Library 33072
   (always), Get / Send Item ... New Tool Manager 33075 / 33079 (IsRegKeyPresent of a key NOT DECODED: taken as absent).
   32975 reads 7524 "Get angled head..." (bit 0x40 off). Tool manager... gets "  (" + the library name + ")". The Define /
   Edit Tool wizard and the Tool Manager are the front end's (cui_hooks.tool_wizard / tool_manager; absent = greyed). */
int otp_menu(OtpItem *it, int cap) {
    int sel = tool_choice >= 0 && tool_choice < F.ntools, n = 0;
    #define I(T_, A_, EN_, SEP_) do { if (n < cap) { snprintf(it[n].text, sizeof it[n].text, "%s", T_); it[n].act = A_; it[n].en = EN_; it[n].sep = SEP_; n++; } } while (0)
    I("Select library tool...", OTP_SELLIB, 1, 0);                              /* 33074 -> OnSelectLibToolPb */
    I("Create new tool...", OTP_NEW, cui_hooks.tool_wizard != NULL, 0);         /* 32972 AddNewTool(0) */
    I("Edit tool...", OTP_EDIT, sel && cui_hooks.tool_wizard != NULL, 0);       /* 32973 EditTool (needs a selection) */
    I("Edit projection...", 0, 0, 0);       /* 32987 EditAssemblyProjection: NOT PORTED */
    I("Edit assembly names...", 0, 0, 0);   /* 33215 EditAssemblyNames: NOT PORTED */
    I("Get angled head...", 0, 0, 0);       /* 32975 AggregateSelectPgDlg: NOT PORTED */
    I("Save tool to library...", 0, 0, 0);  /* 33002 SaveAssembyToLibrary: NOT PORTED */
    {   char lab[200], nm[160] = "";        /* 32976 RunToolManager; ToolLibrary(group, dir, name, ext) -> name */
        if (tlib_ok) { snprintf(nm, sizeof nm, "%s", path_base(tlib.path)); char *d = strrchr(nm, '.'); if (d) *d = 0; }
        snprintf(lab, sizeof lab, "Tool manager...  (%s)", nm);
        I(lab, OTP_TM, cui_hooks.tool_manager != NULL, 1); }
    I("View", 0, 0, 0);                     /* 32977-32979 SetViewMode of this list: NOT PORTED */
    I("Arrange tools", 0, 0, 1);            /* 32980 / 32981 SortList 2 / 4: NOT PORTED */
    I("Re-initialize feeds & speeds", OTP_REINIT, 1, 0);   /* 33067 SetFeedsAndSpeedsFromSource */
    I("Feed speed calculator...", 0, 0, 0); /* 32983 FeedCalculator (enabled for non-wire ops): NOT PORTED */
    I("Search for cut parameters...", 0, 0, 0);   /* 32982 (material + tool system +0xe0 lookup): NOT DECODED */
    #undef I
    return n;
}
void otp_menu_act(int act) {
    int sel = tool_choice >= 0 && tool_choice < F.ntools;
    switch (act) {
    case OTP_SELLIB: tl6410_show(); break;
    case OTP_NEW: if (cui_hooks.tool_wizard) { otp_wait_new = F.ntools; cui_hooks.tool_wizard(-1); } break;
    case OTP_EDIT: if (sel && cui_hooks.tool_wizard) cui_hooks.tool_wizard(tool_choice); break;
    case OTP_TM: if (cui_hooks.tool_manager) cui_hooks.tool_manager(); break;
    case OTP_REINIT: if (sel) fs_from_source(&pending.p, &F.tools[tool_choice]); break;
    }
}

/* ---- Tool List 6410 (see above): the controls at template positions, the library it lists, OK */
const TpCtl TL6410[TL6410_N] = {
    { TPK_LABEL, 10253, 7, 5, 128, 18, "", 0 },                      /* the machine's library file (UILabel::EnableDataTip: the full path) */
    { TPK_BTN, 25079, 143, 5, 25, 18, "", -1 },                      /* New Library: NOT PORTED */
    { TPK_BMP, 25046, 179, 7, 15, 14, "mccoreres_BMP_25046.png", 0 },
    { TPK_LABEL, 0, 197, 11, 96, 8, "= Tool used in an operation", 0 },
    { TPK_LIST, 25463, 7, 36, 310, 186, "", 0 },
    { TPK_BTN, 3056, 341, 74, 50, 14, "Filter...", -1 },
    { TPK_CHECK, 25194, 341, 91, 52, 10, "Filter Active", -1 },     /* checked (dlg +0x20230 = 1), filter not ported */
    { TPK_LABEL, 3805, 336, 104, 69, 8, "", 0 },                     /* "%d of %hd tools displayed" */
    { TPK_GROUP, 0, 333, 126, 72, 44, "Display mode", -1 },
    { TPK_RADIO, 10408, 345, 136, 53, 10, "Tools", -1 },             /* 10407-10410 = tm_dispmode (greyed) */
    { TPK_RADIO, 10409, 345, 146, 71, 10, "Assemblies", -1 },
    { TPK_RADIO, 10410, 345, 157, 51, 10, "Both", -1 },
    { TPK_BTN, 1, 320, 204, 36, 18, "OK", 0 },
    { TPK_BTN, 2, 359, 204, 36, 18, "Cancel", 0 },
    { TPK_BTN, 9, 398, 204, 36, 18, "Help", -1 },
};
/* while it is up: the current tool library, else the machine's (opened), else (no Mastercam library here) the shipped
   starter tools (ours) */
void tl6410_prepare(void) {
    if (!tlib_ok) { char lp[1200]; tl6410_machine_lib(lp, sizeof lp); if (lp[0] && file_exists(lp)) tm_open_lib(lp); }
    if (!tlib_ok) starter_lib_open();
}
void tl6410_count_text(char *b, size_t n) { int k = tlib_ok ? tlib.n : 0; snprintf(b, n, "%d of %d tools displayed", k, k); }
void tl6410_ok(void) {
    if (tlib_ok && tl6410_sel >= 0 && tl6410_sel < tlib.n) { int k = part_add_tool(&tlib.t[tl6410_sel]); if (k >= 0) { tool_choice = k; fs_from_source(&pending.p, &F.tools[k]); } }
    tl6410_open = 0; tl_list_write();                  /* DestroyWindow 0x18086a0a0 -> WriteParameters 0x18086dd30 */
}
void tl6410_cancel(void) { tl6410_open = 0; tl_list_write(); }
