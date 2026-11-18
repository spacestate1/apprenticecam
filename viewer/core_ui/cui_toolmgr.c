/* cui_toolmgr.c - the model of the Tool Manager (mccoreres dialog 1099, CToolListBoth: the part / library lists'
   selection, sort, filter, the dialog's actions and prompts, the library file rules, menu 1225, the OnSize layout), the
   Tool List Filter (CToolFilt, dialog 25519), the Tool Manager and Toolpaths manager tool reports (rpx templates),
   Renumber tools (CRenumbTl, opmanres dialog 805) and the op -> tool lookup they share. Moved from viewer/toolmgr_ui.inc
   and viewer/mcamui.c (formats/qt_port_plan.md phase 7; formats/toolmanager_ui.md 1.5 behaviour + 1.5.10 code map,
   formats/renumber_tools.md); names, comments and decomp citations kept. What only a front end does (drawing, the file
   dialogs, its menu / popup windows) stays there: viewer/toolmgr_ui.inc (nuklear), viewer-qt/src/toolmgr.cpp (Qt). */
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
#include <time.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#define access _access
#define W_OK 2
#endif
#include "paths.h"
#include "toollib.h"
#include "rpx.h"
#include "cam2d.h"
#include "cam_fileop.h"
#include "mcam_saveops.h"
#include "core_ui.h"

#define CP(dst, src) snprintf(dst, sizeof dst, "%s", src)
#define message cui_message

/* ------------------------------------------------------------------------------------------------ dialog state (was mcamui.c) */
int show_tm;                                          /* the Tool Manager is open (DoModal) */
int tm_part_sel = -1, tm_lib_sel = -1, tm_pick_for_params;
unsigned char tm_sel[2][4096]; int tm_anchor[2] = { -1, -1 };
int tm_bup_done;                                      /* <file>bup copy (FUN_18087ca80) made for the current library */
int tm_found_init;                                    /* the first open's library search ran (draw_tool_manager) */
int tm_confirm;                                       /* 1 part delete (used tools), 2 library delete */
int tm_focus = -1;                                    /* the list that has the focus (NM_SETFOCUS) */
int tm_lib_modified;                                  /* "library modified" dlg+0x47202 */
Tool *tm_clip; int tm_nclip;                          /* Mastercam clipboard of the dialog: dlg+0x47288 (cloned assemblies), count +0x472a8 */
char tm_imp_last[1100];                               /* DAT_1814316b0: the last "Import Tools" source file (session) */
/* config 31020 Reports Folder, 31011 / 31017 op manager tool lists, 31006 / 31007 TOOL LIST (MILL-SIMPLE / -DETAILED):
   Tool Manager reports (read by cui_config_item, was mcamui.c screen_colors_load) */
char cfg_rpt_dir[300], cfg_rpt_mill[80], cfg_rpt_router[80], cfg_rpt_mill_simple[80], cfg_rpt_mill_det[80];
void cui_rpt_config_item(int key, const char *q) {
    if (key == 31020 || key == 31011 || key == 31017 || key == 31006 || key == 31007) {   /* text values */
        char tv[300]; const char *b = q + strspn(q, " "); snprintf(tv, sizeof tv, "%s", b); tv[strcspn(tv, "\r\n")] = 0;
        if (key == 31020) CP(cfg_rpt_dir, tv); else if (key == 31011) CP(cfg_rpt_mill, tv); else if (key == 31006) CP(cfg_rpt_mill_simple, tv);
        else if (key == 31007) CP(cfg_rpt_mill_det, tv); else CP(cfg_rpt_router, tv);
    }
}

/* Tool List Filter (CToolFilt, dialog 25519, 1.5.6): one settings set (Filter... edits the part list's filter; the library list
   re-reads the same prm), the library ignores operation masking (list+0x1e374) */
TmFilt tmf, tmf_edit; int tmf_init, tmf_open;
void tmf_defaults(TmFilt *f) {                        /* InitSettings 0x1808640f0: all on, no masking, diameter Ignore */
    memset(f, 0, sizeof *f); memset(f->type, 1, 26); memset(f->mat, 1, 6); memset(f->rad, 1, 3); f->op = 2; f->unit = 2; f->dsel = 0;
}
/* CtlToolType 0x180863270: the 25 picture toggles of 25519 (template positions, 30 x 28 du); 1827 -> 0 (ProdEnableBits &
   0x40 = 0) */
const TmfType TMF_TYPES[TMF_NTYPES] = {
    {19,20,10,"Endmill1 Flat"},{49,20,11,"Endmill2 Sphere"},{79,20,19,"Endmill3 Bull"},{109,20,13,"Face mill"},{139,20,15,"Radius mill"},{169,20,12,"Chamfer mill"},{199,20,14,"Slot mill"},
    {19,49,17,"Taper mill"},{49,49,16,"Dove mill"},{79,49,18,"Lollipop mill"},{109,49,3,"Drill"},{139,49,6,"Reamer"},{169,49,7,"Bore"},{199,49,4,"Tap RH"},
    {19,78,5,"Tap LH"},{49,78,1,"Center Drill"},{79,78,2,"Spot Drill"},{109,78,8,"CBore"},{139,78,9,"CSink"},{169,78,0,"Undefined"},{199,78,21,"Engrave tool"},
    {19,107,22,"Bradpt Drill"},{49,107,23,"High Feed Mill"},{79,107,24,"Thread Mill"},{109,107,25,"Barrel Mill"} };
/* combo texts (CToolFilt::DoDataExchange 0x180863470): 2497 5356-5358, 2502 4958 / 4960 / 5355, 1772 2040 2043 2048 2052 2061;
   the material boxes relabelled at run time 3711-3716 */
const char *const TMF_OPM[3] = { "Used by operations", "Not used by operations", "No operation masking" };
const char *const TMF_UM[3] = { "English", "Metric", "No unit masking" };
const char *const TMF_DM[5] = { "Ignore", "Equal", "Less Than", "Greater Than", "Between" };
const char *const TMF_RAD[3] = { "None", "Corner", "Full" };
const char *const TMF_MAT[6] = { "HSS", "Carbide", "Ti Coated", "Ceramic", "User Def 1", "User Def 2" };
void tm_open_filter(void) {                           /* OnInitDialog -> AllTrueToFalse: an all-on group is shown with nothing ticked */
    tp_filt_open = 0;                                 /* batch 12: opened from the Tool Manager (the Tool page's Filter... sets it after) */
    if (!tmf_init) { tmf_defaults(&tmf); tmf_init = 1; }
    tmf_edit = tmf;
    int all = 1; for (int k = 0; k < 26; k++) all &= tmf_edit.type[k]; if (all) memset(tmf_edit.type, 0, 26);
    all = 1; for (int k = 0; k < 6; k++) all &= tmf_edit.mat[k]; if (all) memset(tmf_edit.mat, 0, 6);
    all = 1; for (int k = 0; k < 3; k++) all &= tmf_edit.rad[k]; if (all) memset(tmf_edit.rad, 0, 3);
    tmf_open = 1;
}
void tmf_ok(void) {                                   /* OnOK -> AllFalseToTrue: a group with nothing ticked means all */
    TmFilt *f = &tmf_edit;
    int any = 0; for (int k = 0; k < 26; k++) any |= f->type[k]; if (!any) memset(f->type, 1, 26);
    any = 0; for (int k = 0; k < 6; k++) any |= f->mat[k]; if (!any) memset(f->mat, 1, 6);
    any = 0; for (int k = 0; k < 3; k++) any |= f->rad[k]; if (!any) memset(f->rad, 1, 3);
    tmf = *f; tmf_open = 0;
    if (tp_filt_open) { tp_factive = 1; tp_filt_open = 0; }   /* batch 12: ToolPageBase::OnToolFilterPb - the filter's DoModal 1 -> Filter Active on */
}
void tmf_types_all(int on) { for (int k = 0; k < TMF_NTYPES; k++) tmf_edit.type[TMF_TYPES[k].type] = (unsigned char)(on != 0); }   /* 1815 / 1817 */
int tm_used(const Tool *t) {                          /* ToolsUsedInOperations */
    for (int k = 0; k < F.nops; k++) if (F.ops[k].tool == t->num) return 1;
    for (int k = 0; k < ncamops; k++) if (camops[k]->p.tool_num == t->num) return 1;
    return 0;
}
int tm_filter_ok(int L, const Tool *t) {              /* TestToolOk 0x180865900 */
    if (!tm_factive[L]) return 1;
    return tm_filter_test(L, t);
}
int tm_filter_test(int L, const Tool *t) {            /* TestToolOk's tests (batch 12: also the Tool page's list, L = 0) */
    const TmFilt *f = &tmf;
    if (!tmf_init) { tmf_defaults(&tmf); tmf_init = 1; }
    if (t->type < 0 || t->type >= 26 || !f->type[t->type]) return 0;
    if (L == 0 && f->op == 0 && !tm_used(t)) return 0;
    if (L == 0 && f->op == 1 && tm_used(t)) return 0;
    if (f->unit == 0 && t->is_metric) return 0;
    if (f->unit == 1 && !t->is_metric) return 0;
    double lo = -1e38, hi = 1e38, tol = cfg_sys_tol;   /* GetDlgFilterSettings */
    if (f->dsel == 1) lo = hi = f->d1; else if (f->dsel == 2) hi = f->d1 - tol; else if (f->dsel == 3) lo = f->d1 + tol;
    else if (f->dsel == 4) { lo = fmin(f->d1, f->d2); hi = fmax(f->d1, f->d2); }
    if (t->dia < lo || t->dia > hi) return 0;
    if (t->rad_type >= 0 && t->rad_type < 3 && !f->rad[t->rad_type]) return 0;
    if (t->tool_mat > 0 && t->tool_mat <= 6 && !f->mat[t->tool_mat - 1]) return 0;
    return 1;
}
static int tm_sort_L; static const TmColSet *tm_sort_set;   /* the list sorted and its active column set */
static int tm_cmp(const void *pa, const void *pb) {  /* SortColumn 0x18050b060: cmp x (+1 ascending / -1) */
    int ia = *(const int *)pa, ib = *(const int *)pb, L = tm_sort_L, c = tm_sort_set->sortcol, r = 0;
    const Tool *a = tm_tool(L, ia), *b = tm_tool(L, ib);
    double tol = cfg_sys_tol;
    switch (c) {
    case 2: r = (a->num > b->num) - (a->num < b->num); break;
    case 6: r = fabs(a->dia - b->dia) <= tol ? 0 : a->dia < b->dia ? -1 : 1; break;
    case 8: r = fabs(a->flute_len - b->flute_len) <= tol ? 0 : a->flute_len < b->flute_len ? -1 : 1; break;
    case 9: r = (a->flutes > b->flutes) - (a->flutes < b->flutes); break;
    case 3: case 4: case 5: case 7: case 10: { char x[128], y[128]; tm_cell(a, c, x, sizeof x); tm_cell(b, c, y, sizeof y); r = strcmp(x, y); break; }
    default: r = 0;                                    /* columns 0, 1 (bare tools only here), 11, 12: constant */
    }
    if (!r) r = (ia > ib) - (ia < ib);                 /* stable */
    return tm_sort_set->sortasc ? r : -r;
}
/* the rows IDX[0..N) of list L (0 = part tools) in the order of column set S (FillList's SortList() -> LVM_SORTITEMS with SortColumn) */
void tm_sort_rows(int L, const TmColSet *s, int *idx, int n) { tm_sort_L = L; tm_sort_set = s; qsort(idx, n, sizeof *idx, tm_cmp); }
int tm_rows(int L, int *idx) {
    int n = 0;
    for (int i = 0; i < tm_count(L) && n < 4096; i++) {
        if (L == 1 && tm_dispmode == 1) continue;       /* Assemblies only: no bare-tool rows (a library has no assemblies here) */
        if (tm_filter_ok(L, tm_tool(L, i))) idx[n++] = i;
    }
    tm_sort_rows(L, TM_SET(L), idx, n);
    return n;
}
void tm_sortlist(int L, int c) { tm_sortset(TM_SET(L), c); }
void tm_sortset(TmColSet *s, int c) {                 /* SortList 0x18050b3e0: same column toggles, a new one starts ascending */
    if (s->sortcol == c) s->sortasc = !s->sortasc; else { s->sortcol = c; s->sortasc = 1; }
}
int tm_nsel(int L) { int k = 0; for (int i = 0; i < tm_count(L) && i < 4096; i++) k += tm_sel[L][i]; return k; }
void tm_clear_sel(int L) { memset(tm_sel[L], 0, sizeof tm_sel[L]); }
int tm_colmenu_ok(int L, int c) { return !(L == 1 && tm_dispmode == 0 && (c == 3 || c == 5 || c == 12)); }   /* VisibleColumnPopupMenu: columns 2..12 */

/* ---- the library file: <file>bup backup (FUN_18087ca80), restore (FUN_1808753c0) / drop (FUN_180875370) ---- */
static int tm_copy_file(const char *a, const char *b) {
    FILE *in = fopen(a, "rb"), *out = in ? fopen(b, "wb") : NULL; char buf[65536]; size_t k; int ok = in && out;
    while (ok && (k = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, k, out) != k) ok = 0;
    if (in) fclose(in); if (out && fclose(out) != 0) ok = 0;
    return ok;
}
static void tm_lib_backup(void) {                     /* FUN_18087ca80: <library>bup (made before our first write, not at open) */
    if (tm_bup_done || !tlib_ok || !file_exists(tlib.path)) return;
    char b[1100]; snprintf(b, sizeof b, "%sbup", tlib.path);
    tm_copy_file(tlib.path, b);
    tm_bup_done = 1;
}
static void tm_lib_reload(void) {                     /* FUN_18087c600: GetTools again, then the machine-group combo */
    if (!tlib_ok || !tlib.path[0] || !file_exists(tlib.path)) return;
    ToolLib nl; char err[300];
    if (!toollib_load(tlib.path, &nl, err, sizeof err)) return;
    toollib_free(&tlib); tlib = nl;
}
static void tm_lib_finish(int restore) {              /* FUN_1808753c0 (restore = copy bup over the library) / FUN_180875370 (delete bup) */
    if (tm_bup_done && tlib_ok) {
        char b[1100]; snprintf(b, sizeof b, "%sbup", tlib.path);
        if (restore && tm_copy_file(b, tlib.path)) { remove(b); tm_lib_reload(); } else if (!restore) remove(b);
    }
    tm_bup_done = 0; tm_lib_modified = 0;
}
static int tm_lib_readonly(const char *path) {        /* FUN_1807fed50: a read-only file -> 857 «Warning» / 8987 */
    if (file_exists(path) && access(path, W_OK) != 0) { message("Warning", "Cannot write to tool library because the file is marked read-only."); return 1; }
    return 0;
}
int tm_lib_write(void) {                              /* the library is written at once (TlToolLibraryX), not on a Save */
    if (!tlib_ok || !tlib.path[0] || tlib.path[0] == '(') return 1;
    if (tm_lib_readonly(tlib.path)) return 0;
    tm_lib_backup();
    char err[400];
    if (ends_with_ci(tlib.path, ".tooldb")) { if (!toollib_save_tooldb(tlib.path, &tlib, err, sizeof err)) { message("Tool Libraries Error", "%s", err); return 0; } }
    else if (!toollib_save(tlib.path, &tlib)) { message("Tool Libraries Error", "Cannot write %s: %s", tlib.path, strerror(errno)); return 0; }
    tm_lib_modified = 1;                              /* every library handler sets dlg+0x47202 */
    return 1;
}

/* ---- prompts inside the dialog: Yes / No with an optional "Don't show this again" (message_box_yes_no_do_not_show_str) ---- */
TmPrompt tmq;
static int tm_sim_yes = 1, tm_sim_dont;               /* DAT_181326564 (initially 1 = Yes) / DAT_1814442cd ("don't show") */
static int tm_dup_yes = 1, tm_dup_dont;               /* DAT_181313644 (initially 1) / DAT_18143db69 */
static void tm_ask(int kind, const char *title, const char *text, int dont_box) {
    tmq.kind = kind; tmq.dont_box = dont_box; CP(tmq.title, title); CP(tmq.text, text);
}
/* TlToolMill::DoIsSimilar 0x18083f720 (Up / paste into the part, ToolSystem +0x1d8 lambda -> TlAssembly::IsSimilar): a = the
   tool being added, b = a part tool. The asymmetric type tests are as in the code. FL = TlToolMill+0x800. The stored-profile
   comparison (TlObject::operator== of GetStoredProfileConst) is NOT DECODED: tools here have none, so it passes. */
static int tm_is_similar(const Tool *a, const Tool *b) {
    double tol = cfg_sys_tol;
    if (fabs(a->flute_len - b->flute_len) >= tol) return 0;
    if ((a->is_metric != 0) != (b->is_metric != 0) || a->type != b->type || a->rad_type != b->rad_type) return 0;
    if (fabs(a->dia - b->dia) >= tol || fabs(a->corner_r - b->corner_r) >= tol || a->flutes != b->flutes) return 0;
    if ((a->type == 4 || b->type == 5 || a->type == 24) && fabs(tm_info_pitch(a) - tm_info_pitch(b)) >= tol) return 0;
    if ((a->type == 16 || b->type == 12 || a->type == 17 || b->type == 21) && fabs(a->tip_angle - b->tip_angle) >= tol) return 0;
    return 1;
}
/* TpToolList::ToolAttribsMatch 0x1808f5480 (Down / paste into the library, IndexByToolExists 0x1808f45f0) */
static int tm_attribs_match(const Tool *a, const Tool *b) {
    double tol = cfg_sys_tol;
    if ((a->is_metric != 0) != (b->is_metric != 0) || a->type != b->type || a->rad_type != b->rad_type) return 0;
    if (fabs(a->dia - b->dia) > tol || fabs(a->corner_r - b->corner_r) > tol || a->flutes != b->flutes) return 0;
    if ((a->type == 4 || a->type == 5) && fabs(tm_info_pitch(a) - tm_info_pitch(b)) > tol) return 0;
    if (a->type <= 21 && ((0x231000u >> a->type) & 1) && fabs(a->tip_angle - b->tip_angle) > tol) return 0;   /* 12, 16, 17, 21 */
    return 1;
}

/* ---- adding tools: one job at a time, paused while a prompt is open ----
   L 0 = into the part (Up FUN_18087bc00, paste FUN_18087a660): similar-tool check (ToolSystem slot 2, 5th argument 1);
   L 1 = into the library (Down FUN_180878d90, paste FUN_18087a2c0): IndexByToolExists(info, prompt 1, group 0). */
TmJob tmj;
static void tm_job_end(void) {
    if (tmj.L == 0) { if (tmj.last >= 0) { tm_clear_sel(0); tm_sel[0][tmj.last] = 1; tm_part_sel = tmj.last; } }   /* FillList(-1) */
    else if (tmj.added) tm_lib_write();               /* AddAssemblies, reload, FillList(-1), library modified */
    for (int i = 0; i < tmj.n; i++) free(tmj.t[i].holder_rz);
    free(tmj.t); memset(&tmj, 0, sizeof tmj);
}
static int tm_msg_open(void) { const char *a, *b; return cui_hooks.msg_open && cui_hooks.msg_open(&a, &b); }   /* mcamui show_msg */
static void tm_job_add(int yes) {                     /* the current tool: add it or (No) leave it out */
    const Tool *t = &tmj.t[tmj.pos++];
    if (!yes) return;
    if (tmj.L == 0) {
        int k = part_add_tool(t);                     /* the library number unless group +0x11b8, D / H by tp_tool_offset when renumbered (part_add_tool) */
        if (k < 0 && !tm_msg_open()) message("Tool Libraries Error", "Unable to add tool to tool list");   /* 6515 / 6510 (part_add_tool may have said why) */
        else tmj.last = k;
    } else { toollib_add(&tlib, t); tmj.added++; }    /* PrepareObjectForStorage: new IDs, group 0; the tool number is kept */
}
static void tm_job_run(void) {
    while (tmj.active && !tmq.kind && tmj.pos < tmj.n) {
        const Tool *t = &tmj.t[tmj.pos]; int cnt = 0, lastm = -1;
        if (tmj.L == 0) { for (int i = 0; i < F.ntools; i++) if (tm_is_similar(t, &F.tools[i])) cnt++; }
        else for (int i = 0; i < tmj.base_n && i < tlib.n; i++) if (tm_attribs_match(t, &tlib.t[i])) { cnt++; lastm = i; }   /* the list as loaded (dlg+0x471e0) */
        if (!cnt) { tm_job_add(1); continue; }
        int *dont = tmj.L == 0 ? &tm_sim_dont : &tm_dup_dont, ans = tmj.L == 0 ? tm_sim_yes : tm_dup_yes;
        if (*dont) { tm_job_add(ans); continue; }     /* "don't show again": the last answer */
        char d[300], s[400];
        if (cnt < 2) { cui_tm_describe(tmj.L == 0 ? t : &tlib.t[lastm], d, sizeof d, tmj.L == 0);
                       snprintf(s, sizeof s, "A similar tool (%s) already exists in tool list.  Add another?", d); }   /* 5254 */
        else snprintf(s, sizeof s, "%d similar tools already exists in tool list.  Add another?", cnt);              /* 5257 */
        tm_ask(tmj.L == 0 ? TQ_SIMILAR : TQ_DUPLICATE, "Similar Tool Warning", s, 1);                                   /* 5258 */
        return;
    }
    if (tmj.active && !tmq.kind) tm_job_end();
}
static void tm_job_start(int L, const Tool *src, const unsigned char *pick, int n) {
    if (tmj.active || tmq.kind) return;
    memset(&tmj, 0, sizeof tmj); tmj.L = L; tmj.last = -1; tmj.active = 1; tmj.base_n = tlib_ok ? tlib.n : 0;
    tmj.t = calloc(n ? n : 1, sizeof(Tool));
    for (int i = 0; i < n; i++) if (!pick || pick[i]) tool_copy(&tmj.t[tmj.n++], &src[i]);
    tm_job_run();
}
void tm_up(void) {                                    /* FUN_18087bc00: Add a Library Tool to Part (library double-click too) */
    if (!tlib_ok) return;
    int idx[4096], n = tm_rows(1, idx); unsigned char pick[4096] = { 0 }; Tool *v = calloc(n ? n : 1, sizeof(Tool)); int m = 0;
    for (int k = 0; k < n; k++) if (tm_sel[1][idx[k]]) { v[m] = tlib.t[idx[k]]; pick[m++] = 1; }   /* selection in list order */
    tm_job_start(0, v, pick, m); free(v);
}
void tm_down(void) {                                  /* FUN_180878d90: Add a Part Tool to Library */
    if (!tlib_ok) { message("Tool Libraries Error", "No tool library is open."); return; }
    if (tm_lib_readonly(tlib.path)) return;
    int idx[4096], n = tm_rows(0, idx); unsigned char pick[4096] = { 0 }; Tool *v = calloc(n ? n : 1, sizeof(Tool)); int m = 0;
    for (int k = 0; k < n; k++) if (tm_sel[0][idx[k]]) { v[m] = F.tools[idx[k]]; pick[m++] = 1; }
    tm_job_start(1, v, pick, m); free(v);
}
int tm_up_ok(void) { return tlib_ok && tm_nsel(1) > 0 && !tmj.active; }     /* the Up / Down buttons' enable state */
int tm_down_ok(void) { return tlib_ok && tm_nsel(0) > 0 && !tmj.active; }
static void tm_clip_free(void) { for (int i = 0; i < tm_nclip; i++) free(tm_clip[i].holder_rz); free(tm_clip); tm_clip = NULL; tm_nclip = 0; }
/* Copy tool(s) FUN_180878770 / FUN_180878760 -> FUN_180878780: the clipboard is emptied, then holds a clone of every selected
   item of that list (in item order). Paste FUN_18087a660 (part, as Up) / FUN_18087a2c0 (library, as Down). */
void tm_copy(int L) {
    tm_clip_free();
    int idx[4096], n = tm_rows(L, idx);
    tm_clip = calloc(n ? n : 1, sizeof(Tool));
    for (int k = 0; k < n; k++) if (tm_sel[L][idx[k]]) tool_copy(&tm_clip[tm_nclip++], tm_tool(L, idx[k]));
}
void tm_paste(int L) {
    if (tm_nclip < 1) return;
    if (L == 1) { if (!tlib_ok) return; if (tm_lib_readonly(tlib.path)) return; }
    tm_job_start(L, tm_clip, NULL, tm_nclip);
}
void tm_delete(int L) {
    if (L == 0) {                                     /* FUN_180872b80 */
        for (int i = F.ntools - 1; i >= 0; i--) if (i < 4096 && tm_sel[0][i]) {
            if (tm_used(&F.tools[i]) && tm_confirm != 11) continue;
            free(F.tools[i].holder_rz); memmove(F.tools + i, F.tools + i + 1, (F.ntools - i - 1) * sizeof(Tool)); F.ntools--; doc_dirty = 1;
        }
        tm_clear_sel(0); tm_part_sel = -1;
    } else {                                          /* FUN_1808727a0 (after the confirmation) */
        if (tm_lib_readonly(tlib.path)) return;
        for (int i = tlib.n - 1; i >= 0; i--) if (i < 4096 && tm_sel[1][i]) toollib_remove(&tlib, i);
        tm_clear_sel(1); tm_lib_sel = -1;
        tm_lib_write();
    }
}
void tm_delete_ask(int L) {                           /* Delete tool(s): the library always asks; the part only for used tools */
    if (L == 1) { tm_confirm = 2; return; }
    int used = 0; for (int i = 0; i < F.ntools && i < 4096; i++) if (tm_sel[0][i] && tm_used(&F.tools[i])) used = 1;
    if (used) tm_confirm = 1; else tm_delete(0);
}
void tm_confirm_answer(int yes) {                     /* 3840 «Deleting Tools»: Yes deletes; No (part) still deletes the unused selected tools */
    int k = tm_confirm;
    if (yes) { tm_confirm = k == 1 ? 11 : 0; tm_delete(k == 1 ? 0 : 1); tm_confirm = 0; }
    else { if (k == 1) { tm_confirm = 0; tm_delete(0); } tm_confirm = 0; }
}
void tm_delete_unused(void) {                         /* FUN_180872f40: part tools no operation uses, no prompt */
    for (int i = F.ntools - 1; i >= 0; i--) if (!tm_used(&F.tools[i])) {
        free(F.tools[i].holder_rz); memmove(F.tools + i, F.tools + i + 1, (F.ntools - i - 1) * sizeof(Tool)); F.ntools--; doc_dirty = 1; }
    tm_clear_sel(0); tm_part_sel = -1;
}
void tm_create(int L) {                               /* FUN_180871ad0 / FUN_180871670: template = the single selection, else the default */
    const Tool *tpl = NULL;
    if (tm_nsel(L) == 1) for (int i = 0; i < tm_count(L) && i < 4096; i++) if (tm_sel[L][i]) tpl = tm_tool(L, i);
    tedit_open(-1, NULL);
    if (tpl) { free(tedit.holder_rz); tool_copy(&tedit, tpl); tedit.num = next_tool_num(); tedit.has_guid = 0; tedit.has_extras = 1; }   /* has_extras: as the wizard frames set it (cui_toolwiz.c tedit_open) */
    tm_lib_tool_edit = L == 1 ? -2 : -1;              /* -2: a new library tool (the library path after +0x90 is NOT DECODED) */
}
void tm_edit(int L) {
    for (int i = 0; i < tm_count(L); i++) if (i < 4096 && tm_sel[L][i]) {
        if (L == 0) { tm_lib_tool_edit = -1; tedit_open(i, &F.tools[i]); }
        else { tedit_open(-1, &tlib.t[i]); tm_lib_tool_edit = i; }
        return;
    }
}

/* ---- library buttons and menu commands that pick a file (1.5.8) ---- */
static int tm_after_save;                             /* the action waiting for the "Save Library?" answer (FUN_18087c0e0) */
static char tm_after_path[1100], tm_after_path2[1100];
enum { TA_NONE, TA_OK, TA_BROWSE, TA_NEW, TA_IMPORT };
static void tm_switch_lib(const char *path, int act);
void tm_close_now(void) {                             /* WriteSettings, then the dialog ends */
    tm_settings_write(); tm_colinit = 0;               /* the next OnInitDialog reads them again */
    show_tm = 0; tm_pick_for_params = 0; tmf_open = 0;
    tm_clip_free();
    if (cui_hooks.tm_closed) cui_hooks.tm_closed();    /* the front end's menu / window state (mcamui tm_menu, tm_win_init) */
}
static void tm_after(int act) {                       /* continue after FUN_18087c0e0 */
    if (act == TA_OK) tm_close_now();
    else if (act) tm_switch_lib(tm_after_path, act);
}
static void tm_save_prompt(int act) {                 /* FUN_18087c0e0: library modified -> 3842 «Save Library?» / 3843; No restores the backup */
    if (!tm_lib_modified) { tm_lib_finish(0); tm_after(act); return; }
    tm_after_save = act; tm_ask(TQ_SAVELIB, "Save Library?", "Save changes to your current library?", 0);
    /* the 308 / 310 "previous version" question applies when get_file_version of the backup reports an older Mastercam
       version; for a .tooldb what get_file_version returns is NOT DECODED, so it is not asked */
}
void tm_ok(void) { if (!tmj.active && !tmq.kind) tm_save_prompt(TA_OK); }    /* OnOK FUN_18087a250 */
void tm_cancel(void) {                                /* OnCancel FUN_180876f30 (also Esc and the close box): modified -> restore */
    if (tmj.active) return;
    tm_lib_finish(tm_lib_modified); tm_close_now();
}
void tm_q_answer(int yes) {
    int k = tmq.kind; tmq.kind = 0;
    if (k == TQ_SIMILAR) { if (tmq.dont_box == 2) tm_sim_dont = 1; tm_sim_yes = yes; tm_job_add(yes); tm_job_run(); }
    else if (k == TQ_DUPLICATE) { if (tmq.dont_box == 2) tm_dup_dont = 1; tm_dup_yes = yes; tm_job_add(yes); tm_job_run(); }
    else if (k == TQ_SAVELIB) { int a = tm_after_save; tm_after_save = 0; tm_lib_finish(!yes); tm_after(a); }
}
static void tm_switch_lib(const char *path, int act) { /* Browse FUN_180876f70 / New FUN_1808789a0 / Import FUN_1808791c0, after FUN_18087c0e0 */
    if (act == TA_NEW || act == TA_IMPORT) {
        ToolLib nl; memset(&nl, 0, sizeof nl); nl.metric = mcam_is_metric(&F); char err[400];
        if (act == TA_IMPORT) {                       /* GetTools(part file) -> library vtable +0x88 for each assembly, +0x28 */
            McamFile *pf = calloc(1, sizeof *pf);
            if (pf && mcam_load(pf, tm_after_path2)) { for (int i = 0; i < pf->ntools; i++) toollib_add(&nl, &pf->tools[i]); }
            if (pf) { mcam_free(pf); free(pf); }
            if (file_exists(path)) { ToolLib ex; if (toollib_load(path, &ex, err, sizeof err)) { for (int i = 0; i < nl.n; i++) toollib_add(&ex, &nl.t[i]); toollib_free(&nl); nl = ex; } }
            CP(tm_imp_last, tm_after_path2);
        }
        int ok = ends_with_ci(path, ".tooldb") ? toollib_save_tooldb(path, &nl, err, sizeof err) : toollib_save(path, &nl);
        toollib_free(&nl);
        if (!ok) { message("Tool Libraries Error", "Tool Library could not be created"); return; }   /* 6517 / 6510 */
    }
    tm_open_lib(path);                                /* SetToolLibrary, reload, FillList, label, backup (FUN_18087ca80) */
    tm_bup_done = 0; tm_lib_modified = 0;
}
void tm_browse(const char *path) {                    /* BrowseForToolLibraryToOpen: a different file only */
    if (tlib_ok && !strcmp(path, tlib.path)) return;
    CP(tm_after_path, path); tm_save_prompt(TA_BROWSE);
}
void tm_new(const char *path) {                       /* BrowseForToolLibraryToSave: an existing file is removed first */
    if (tm_lib_readonly(path)) return;
    if (tlib_ok && !strcmp(path, tlib.path)) { message("Tool Libraries Error", "Tool Library could not be created"); return; }
    if (file_exists(path)) remove(path);
    CP(tm_after_path, path); tm_save_prompt(TA_NEW);
}
void tm_import(const char *part, const char *lib) {
    if (tm_lib_readonly(lib)) return;
    CP(tm_after_path, lib); if (part != tm_after_path2) CP(tm_after_path2, part); tm_save_prompt(TA_IMPORT);
}
void tm_save_to(const char *path) {                   /* Save tool(s) to library FUN_18087cb50: AddAssemblies(file, clones of the selection) */
    int L = tm_focus < 0 ? 0 : tm_focus;             /* dlg+0x4727a: the list the menu was opened on */
    if (tm_lib_readonly(path)) return;
    int same = tlib_ok && !strcmp(path, tlib.path);
    ToolLib dst; char err[400]; memset(&dst, 0, sizeof dst); dst.metric = mcam_is_metric(&F);
    if (!same && file_exists(path) && !toollib_load(path, &dst, err, sizeof err)) { message("Tool Libraries Error", "Unable to add tool to tool list"); return; }
    int idx[4096], n = tm_rows(L, idx); Tool *v = calloc(n ? n : 1, sizeof(Tool)); int m = 0;
    for (int k = 0; k < n; k++) if (tm_sel[L][idx[k]]) tool_copy(&v[m++], tm_tool(L, idx[k]));
    ToolLib *D = same ? &tlib : &dst;
    for (int k = 0; k < m; k++) { toollib_add(D, &v[k]); free(v[k].holder_rz); }
    free(v);
    int ok;
    if (same) ok = tm_lib_write();                    /* the open library: reload, FillList(-2), library modified */
    else { ok = ends_with_ci(path, ".tooldb") ? toollib_save_tooldb(path, &dst, err, sizeof err) : toollib_save(path, &dst); toollib_free(&dst);
           if (!ok) message("Tool Libraries Error", "Unable to add tool to tool list"); }   /* 6515 / 6510 */
    if (same && ok) tm_clear_sel(1);
}
void tm_compress(const char *path) {                  /* Compress FUN_180872660: OpenFile, then TlToolLibrarySQL slot 28 (COMMIT, VACUUM) */
    if (!file_exists(path)) { message("Tool Libraries Error", "Error - file not found"); return; }   /* 5593 / 6510 */
    if (tm_lib_readonly(path)) { message("Tool Libraries Error", "Tool Library compression failed"); return; }
    char err[300];
    if (!ends_with_ci(path, ".tooldb") || !toollib_vacuum(path, err, sizeof err)) message("Tool Libraries Error", "Tool Library compression failed");   /* 6513 */
}
/* the file dialogs' results (mcamui tm_fd_accept / the Qt dialog): Save tool(s) to / New / the import target get the
   default extension (lpstrDefExt) when the name has none */
void tm_def_ext(char *p, size_t n) { if (!strrchr(path_base(p), '.')) strncat(p, ".tooldb", n - strlen(p) - 1); }
/* Import Tools step 2: PromptForFileToSave(<part>.<library extension>, 992, 25531) in the part's folder */
void tm_import_target(const char *part, char *dir, size_t nd, char *name, size_t nn) {
    CP(tm_after_path2, part);
    char nm[1400]; CP(nm, part); char *d = strrchr((char *)path_base(nm), '.'); if (d) *d = 0;
    const char *ext = tlib_ok && strrchr(path_base(tlib.path), '.') ? strrchr(path_base(tlib.path), '.') : ".tooldb";
    strncat(nm, ext, sizeof nm - strlen(nm) - 1);
    snprintf(dir, nd, "%.*s", path_dirlen(nm), nm); snprintf(name, nn, "%s", path_base(nm));
}
void tm_import_to(const char *lib) { tm_import(tm_after_path2, lib); }
/* cui_hooks.lib_opened (tm_open_lib): the library list's selection and the backup flag reset (was mcamui hook_lib_opened) */
void tm_lib_opened(void) { tm_lib_sel = -1; tm_bup_done = 0; memset(tm_sel[1], 0, sizeof tm_sel[1]); }
void tm_lib_dir(char *dir, size_t n) {                /* ToolLibrary(group, dir, ...): the folder of the current library */
    if (tlib_ok && path_dirlen(tlib.path)) snprintf(dir, n, "%.*s", path_dirlen(tlib.path), tlib.path); else config_dir_make(dir, n);
}
/* Import Tools FUN_1808791c0: 25550 «Select file for tool import:», starting at the last import source, else the part's folder */
void tm_import_start(char *dir, size_t nd, char *name, size_t nn) {
    dir[0] = 0; name[0] = 0;
    if (tm_imp_last[0]) snprintf(dir, nd, "%.*s", path_dirlen(tm_imp_last), tm_imp_last);
    else if (F.path[0]) snprintf(dir, nd, "%.*s", path_dirlen(F.path), F.path);
    if (tm_imp_last[0]) snprintf(name, nn, "%s", path_base(tm_imp_last));
}

/* ---- the dialog opens (draw_tool_manager's first frame: OnInitDialog step 3 FUN_18087c200, the filter's defaults, the
   library of the part's operations (op +0x416) when it exists here) ---- */
void tm_open_init(void) {
    if (!tm_colinit) tm_settings_read();              /* OnInitDialog step 3 (FUN_18087c200) */
    if (!tmf_init) { tmf_defaults(&tmf); tmf_init = 1; }
    if (!tlib_ok && !tm_found_init) {                 /* the library of the part's operations (op +0x416), when it exists here */
        tm_found_init = 1;
        for (int k = 0; k < F.nops && !tlib_ok; k++) if (F.ops[k].raw && F.ops[k].raw_n >= 0x416 + 2) {
            char p[300]; snprintf(p, sizeof p, "%.*s", 255, (const char *)F.ops[k].raw + 0x416);
            char hp[1100]; host_path(p, hp, sizeof hp);
            if (hp[0] && file_exists(hp)) tm_open_lib(hp);
            break;
        }
        if (!tlib_ok && !F.nops) starter_lib_open();   /* a part without operations and no library: the shipped starter tools (ours) */
    }
}

/* ---- keys (LVN_KEYDOWN FUN_180879d10 part / FUN_180879c10 library) ---- */
void tm_key(int L, int k, int ctrl) {
    if (L < 0 || tmq.kind || tm_confirm || tmj.active) return;
    if (k == TMK_INSERT) tm_create(L);               /* editable dlg+0x47278 / +0x47279 = 1 */
    else if (k == TMK_DELETE) { if (tm_nsel(L)) tm_delete_ask(L); }
    else if (ctrl && k == TMK_C) { if (tm_nsel(L)) tm_copy(L); }
    else if (ctrl && k == TMK_V) { if (tm_nclip > 0) tm_paste(L); }
}
/* Esc inside the modal dialog after the front end closed its own menus (mcamui tm_keydown): a prompt says No, the delete
   confirmation and the filter close, else Cancel */
void tm_escape(void) {
    if (tmq.kind) tm_q_answer(0); else if (tm_confirm) tm_confirm = 0;
    else if (tmf_open) tmf_open = 0; else tm_cancel();
}
/* a row click (mcamui tm_click_row): k = the row's place in the shown order idx[0..n) */
void tm_click(int L, int i, int k, const int *idx, int n, int ctrl, int shift, int right) {
    tm_clear_sel(!L);                                 /* NM_SETFOCUS: focusing one list deselects the other */
    tm_focus = L;
    if (right) { if (!tm_sel[L][i]) { tm_clear_sel(L); tm_sel[L][i] = 1; } }
    else if (ctrl) tm_sel[L][i] = !tm_sel[L][i];
    else if (shift && tm_anchor[L] >= 0) {
        int a = -1, z = k; for (int q = 0; q < n; q++) if (idx[q] == tm_anchor[L]) a = q;
        tm_clear_sel(L); if (a < 0) a = k; if (a > z) { int tmp = a; a = z; z = tmp; }
        for (int q = a; q <= z; q++) tm_sel[L][idx[q]] = 1;
    } else { tm_clear_sel(L); tm_sel[L][i] = 1; }
    if (!shift) tm_anchor[L] = i;
    if (L == 0) tm_part_sel = i; else tm_lib_sel = i;
}
void tm_dblclick(int L, int i) {
    if (L == 1) tm_up();                              /* FUN_180878b90 = Up */
    else if (tm_tool(L, i)->type != 20) tm_edit(0);   /* FUN_180878bd0 (type 20: block drill dialog NOT DONE) */
}
void tm_item_text(const Tool *t, char *o, size_t n) {   /* item text "%3ld - %s, %s": number, Dia. string, Type string */
    char d[64], ty[64]; tm_cell(t, 6, d, sizeof d); tm_cell(t, 10, ty, sizeof ty);
    snprintf(o, n, "%3ld - %s, %s", (long)t->num, d, ty);
}
/* image lists (CMillToolListCtrl::InitBitmaps 0x1805091c0): 16 px 25046 25045 1339 1242 10325 10326 (Details), 32 px 5798
   10338 10323 10336 1241 (Small Icons), 96 px 10324 10337 10322 10335 1240 (Large Icons); 26 tool types per strip. Item image
   (col 0) = MC type + 0 (bare, unused) / 26 (bare, used) / 52 (assembly) / 78 (assembly, used). The pack name of the strip
   and the cell (tool type) of a list item: */
const char *tm_item_strip(int used, int large) {
    static const char *sm[2] = { "mccoreres_BMP_5798", "mccoreres_BMP_10338" }, *lg[2] = { "mccoreres_BMP_10324", "mccoreres_BMP_10337" };
    return large ? lg[used != 0] : sm[used != 0];
}
int tm_item_cell(const Tool *t) { return t->type >= 0 && t->type < 26 ? t->type : 0; }

/* ---- menu 1225 (FUN_1808777f0, toolmanager_ui.md 1.5.5): the items in order with their cascades (View > 32939-32941,
   Arrange tools > 32942-32945, Import/export tools > 32949 / 32950 / 33080 / 32951 / 32952 / 32953). Not ported (greyed):
   Edit holder, Edit projection (33229), Edit assembly names, Load tool(s) on machine (33003), Convert a library to / from
   text, Import from Machining Cloud. ---- */
int tm_menu_items(int L, TmMItem *it, int cap) {
    int sel = tm_nsel(L) > 0, ed = 1, n = 0;          /* both lists editable (dlg+0x47278 / 0x47279 = 1) */
#define I(d, t, a, e, c) do { if (n < cap) { it[n].depth = d; it[n].text = t; it[n].act = a; it[n].en = e; it[n].checked = c; it[n].sub = 0; } n++; } while (0)
#define SUB(t, s) do { if (n < cap) { it[n].depth = 0; it[n].text = t; it[n].act = TMA_NONE; it[n].en = 1; it[n].checked = 0; it[n].sub = s; } n++; } while (0)
    I(0, "Create new tool ...", TMA_CREATE, ed, 0);
    I(0, "Edit tool...", TMA_EDIT, ed && sel, 0);
    I(0, "Edit holder...", TMA_NONE, 0, 0);           /* holder wizard: NOT DONE */
    I(0, "Edit projection...", TMA_NONE, 0, 0);       /* 33229: NOT PORTED */
    I(0, "Edit assembly names...", TMA_NONE, 0, 0);   /* dialog 33053: no assemblies here */
    I(0, "Delete tool(s)", TMA_DELETE, ed && sel, 0);
    I(0, "Delete unused tool(s)", TMA_DELUNUSED, L == 0, 0);
    I(0, NULL, TMA_SEP, 0, 0);
    SUB("View", 1);
    { static const int vm[3] = { 0, 3, 1 }; static const char *vn[3] = { "Large Icons", "Small Icons", "Details" };   /* 32939-32941 */
      for (int k = 0; k < 3; k++) I(1, vn[k], TMA_VIEW0 + vm[k], 1, tm_listview[L] == vm[k]); }
    SUB("Arrange tools", 2);
    { static const int ac[4] = { 2, 10, 4, 6 }; static const char *an[4] = { "by Tool Number", "by Tool Type", "by Tool Name", "by Tool Diameter" };   /* 32942-32945 */
      for (int k = 0; k < 4; k++) I(1, an[k], TMA_SORT0 + ac[k], 1, TM_SET(L)->sortcol == ac[k]); }
    I(0, NULL, TMA_SEP, 0, 0);
    I(0, "Copy tool(s)", TMA_COPY, sel, 0);           /* 32947 */
    I(0, "Paste tool(s)", TMA_PASTE, tm_nclip >= 1, 0);   /* 32948 */
    I(0, NULL, TMA_SEP, 0, 0);
    I(0, "Load tool(s) on machine...", TMA_NONE, 0, 0);   /* 33003: NOT PORTED */
    I(0, "Save tool(s) to library...", TMA_SAVETO, sel, 0);   /* 32946: BrowseForToolLibraryToSaveOrAppend (the front end's file dialog) */
    SUB("Import/export tools", 3);
    I(1, "Convert a library to text...", TMA_NONE, 0, 0);     /* 32949: NOT PORTED */
    I(1, "Convert a library from text...", TMA_NONE, 0, 0);   /* 32950: NOT PORTED */
    I(1, "Import from Machining Cloud...", TMA_NONE, 0, 0);   /* 33080: NOT PORTED */
    I(1, "Compress", TMA_COMPRESS, L == 1, 0);         /* 32951: OpenFile on the current library (the front end's file dialog) */
    I(1, "Report...", TMA_REPORT, 1, 0);               /* 32952 */
    I(1, "Detailed report...", TMA_DREPORT, 1, 0);     /* 32953 */
#undef I
#undef SUB
    return n < cap ? n : cap;
}
/* an item's action (TMA_SAVETO / TMA_COMPRESS first show the front end's file dialog, which then calls tm_save_to /
   tm_compress; tm_menu_act sets the list the menu was opened on for Save tool(s) to library) */
void tm_menu_act(int L, int act) {
    if (act >= TMA_VIEW0 && act < TMA_VIEW0 + 4) { tm_listview[L] = act - TMA_VIEW0; return; }   /* SetViewMode, FillList(-1) */
    if (act >= TMA_SORT0 && act < TMA_SORT0 + TM_COLS) { tm_sortlist(L, act - TMA_SORT0); return; }
    switch (act) {
    case TMA_CREATE: tm_create(L); break;
    case TMA_EDIT: tm_edit(L); break;
    case TMA_DELETE: tm_delete_ask(L); break;
    case TMA_DELUNUSED: tm_delete_unused(); break;
    case TMA_COPY: tm_copy(L); break;
    case TMA_PASTE: tm_paste(L); break;
    case TMA_SAVETO: tm_focus = L; break;
    case TMA_REPORT: tm_report(L, 0); break;
    case TMA_DREPORT: tm_report(L, 1); break;
    }
}

/* ---- layout (OnSize FUN_18087a9c0, pixels; margin dlg+0x4721c = 10, dlg+0x47218 = 5; minimum size FUN_1808790f0 =
   MapDialogRect(600 x 350)). Controls keep their template size; x of the bitmap / label / library buttons is kept.
   dx / dy = dialog units -> px (the template font's base units / 4, / 8). ---- */
static TmRect tmr(float x, float y, float w, float h) { TmRect r = { x, y, w, h }; return r; }
void tm_layout(float cw, float ch, float dx, float dy, TmLayout *Lo) {
#define D(x, y, ww, hh) tmr((x) * dx, (y) * dy, (ww) * dx, (hh) * dy)   /* template rect (1099) */
    const float m = 10, m2 = 5, mid = ch / 2;
    TmRect combo = D(7, 7, 130, 13), bmp = D(145, 7, 15, 14), ulbl = D(167, 10, 96, 8), view = D(513, 7, 81, 13), pfa = D(377, 87, 57, 10),
           lfa = D(377, 261, 57, 10), filt = D(377, 241, 50, 14), grp = D(375, 276, 72, 44), path = D(7, 187, 130, 15), bn = D(147, 185, 25, 18),
           bb = D(183, 185, 25, 18), bi = D(219, 185, 25, 18), up = D(395, 161, 27, 24), dn = D(395, 193, 27, 24), plbl = D(345, 15, 18, 8),
           llbl = D(339, 195, 26, 8), ok = D(374, 325, 36, 18), help = D(415, 325, 36, 18);
    static const int ry[3] = { 287, 297, 308 }, rw[3] = { 33, 51, 31 };
    memset(Lo, 0, sizeof *Lo);
    float rwid = 2 * m + view.w, col = cw + m - rwid;
    Lo->view = tmr(cw - rwid, m, view.w, view.h);
    if (tm_view == 0 || tm_view == 1) {
        Lo->combo = tmr(m, m, combo.w, combo.h); Lo->bmp = tmr(bmp.x, m, bmp.w, bmp.h);
        Lo->used_lbl = tmr(ulbl.x, m2 + m, ulbl.w, ulbl.h);
        float ytop = 2 * m + bmp.h;
        Lo->pfa = tmr(col, 40 + ytop, pfa.w, pfa.h);
        float pfab = Lo->pfa.y + pfa.h;
        Lo->plbl = tmr(cw - 2 * m - rwid - plbl.w, m, plbl.w, plbl.h);
        if (tm_view == 1) {
            Lo->filt = tmr(col, pfab + m, filt.w, filt.h);
            Lo->plist = tmr(m, ytop, cw - 2 * m - rwid, ch - m - ytop);
        } else {
            Lo->lfa = tmr(col, mid + 100, lfa.w, lfa.h);
            Lo->filt = tmr(col, m + lfa.h + 100 + mid, filt.w, filt.h);
            float g = filt.h + 120 + mid;
            Lo->grp = tmr(col, m + g, grp.w, grp.h);
            for (int k = 0; k < 3; k++) Lo->rad[k] = tmr(col + 5, m + g + (ry[k] - 276) * dy, (rw[k] + 20) * dx, 10 * dy);
            Lo->path = tmr(m, mid - m, path.w, path.h);
            Lo->bnew = tmr(bn.x, mid - m, bn.w, bn.h); Lo->bbrw = tmr(bb.x, mid - m, bb.w, bb.h); Lo->bimp = tmr(bi.x, mid - m, bi.w, bi.h);
            float hh = fmaxf(fmaxf(path.h, bn.h), fmaxf(bb.h, bi.h)) / 2;
            Lo->up = tmr(col, fmaxf(pfab, mid - m - up.h), up.w, up.h);
            Lo->down = tmr(col, hh + mid + m, dn.w, dn.h);
            Lo->plist = tmr(m, ytop, cw - 2 * m - rwid, (mid - m) - ytop - hh);
            Lo->llist = tmr(m, m + hh + mid, cw - 2 * m - rwid, ch - 2 * m - hh - mid);
            Lo->llbl = tmr(cw - 2 * m - rwid - llbl.w, mid - m, llbl.w, llbl.h);
        }
    } else {
        Lo->path = tmr(m, m, path.w, path.h);
        float top2 = 2 * m + path.h;
        Lo->bnew = tmr(bn.x, m, bn.w, bn.h); Lo->bbrw = tmr(bb.x, m, bb.w, bb.h); Lo->bimp = tmr(bi.x, m, bi.w, bi.h);
        Lo->lfa = tmr(col, 40 + top2, lfa.w, lfa.h);
        Lo->filt = tmr(col, Lo->lfa.y + lfa.h + m, filt.w, filt.h);
        float g = 80 + filt.h + top2;
        Lo->grp = tmr(col, m + g, grp.w, grp.h);
        for (int k = 0; k < 3; k++) Lo->rad[k] = tmr(col + 5, m + g + (ry[k] - 276) * dy, (rw[k] + 20) * dx, 10 * dy);
        Lo->llbl = tmr(cw - 2 * m - rwid - llbl.w, m, llbl.w, llbl.h);
        Lo->llist = tmr(m, top2, cw - 2 * m - rwid, ch - m - top2);
    }
    Lo->help = tmr(cw - m - help.w, ch - m - help.h, help.w, help.h);
    Lo->ok = tmr(cw - 2 * m - help.w - ok.w, ch - m - ok.h, ok.w, ok.h);
#undef D
}
/* which controls show (FUN_18087a050): part list / library list / Up-Down / Display mode */
void tm_shown(int *part, int *lib, int *updown, int *dispmode) { *part = tm_view != 2; *lib = tm_view != 1; *updown = tm_view == 0; *dispmode = tm_view != 1; }

/* ------------------------------------------------------------------------------------------------ reports (was mcamui.c) */
/* ---- Tool list... (Toolpaths manager, mccore RunToolReportForSelectedOps): the tools of the selected operations,
   once each in operation order (tool slot op +0x544; numbered with the operation's tool number), written as report
   data (FUN_18061bdc0 -> TOOLS: LIBNAME = core 5028 "Operations Manager", FILTER / SORT = core 5360 upper-cased,
   AddNode_DATE; FUN_180649320 per tool: NUMBER "%ld", COMMENT = tool struct +0x56 trimmed and left out when empty,
   DIAMETER / RADIUS / CORNER-RADIUS by make_rstr with config 29003 places, DIAMETER- / LENGTH-OFFSET) and shown with
   the report template config 31017 (router) / 31011 (mill) from <reports folder>MSC\ (GetToolMatlRpx) - Mastercam
   opens it in its ActiveReports viewer (RunActiveReportsViewerUsingRegistry), here a window. NOT DECODED: ASSY-NAME
   and HOLDER-NAME (the tool assembly / holder objects of the part's TlMgr) stay empty; the viewer window itself
   (title, toolbar, zoom); report fonts are drawn with this program's one UI font. ---- */
RLayout tlr; int show_tlr, tlr_new; char tlr_title[160];

void make_rstr(double v, char *b, size_t n, int places) {   /* mccore make_rstr */
    snprintf(b, n, "%.*f", places, v);
    size_t l = strlen(b);
    if (l > 1) {
        if (places == 0) { if (b[l - 1] == '.') b[l - 1] = 0; }
        else while (l > 1 && b[l - 1] == '0' && b[l - 2] != '.') b[--l] = 0;
    }
}
/* a Windows path (Mastercam config) opened on this OS (paths.h host_path); 0 = not there */
int ci_path(const char *win, char *out, size_t n) {
    struct stat st; host_path(win, out, n);
    return stat(out, &st) == 0;
}
/* TOOLS root: LIBNAME, FILTER (AddNode_YN, upper-cased 5359 / 5360), AddNode_DATE, SORT (WriteXML_MillToolList FUN_18061b5c0) */
static void report_root_nodes(RNode *root, const char *libname, int filter) {
    rnode_new(root, "LIBNAME", libname);
    rnode_new(root, "FILTER", filter ? "YES" : "NO");
    time_t now = time(NULL); struct tm *tm = localtime(&now); char b[96];
    { char wd[32], mo[32]; strftime(wd, sizeof wd, "%A", tm); strftime(mo, sizeof mo, "%B", tm);
      snprintf(b, sizeof b, "%s, %s %d, %d", wd, mo, tm->tm_mday, tm->tm_year + 1900); } rnode_new(root, "DATE-LONG", b);          /* en-US sLongDate dddd, MMMM d, yyyy */
    snprintf(b, sizeof b, "%d/%d/%d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900); rnode_new(root, "DATE-SHORT", b);   /* sShortDate M/d/yyyy */
    snprintf(b, sizeof b, "%d:%02d %s", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, tm->tm_min, tm->tm_hour < 12 ? "AM" : "PM"); rnode_new(root, "TIME", b);   /* h:mm tt */
    strftime(b, sizeof b, "%Y", tm); rnode_new(root, "YEAR", b);
    rnode_new(root, "SORT", "NO");
}
/* one tool (FUN_180649320, reached from both report paths) */
static void report_tool_node(RNode *root, const Tool *t, long num, int multi, int router, int places) {
    /* AddNode_TlTool_START: TOOL / TOOL-R (router) or TOOL-M under it when the template is a multi-kind one
       (LoadRpxInfo -> FUN_180638750 = 6: FUN_180616d00 / FUN_18061bdc0), else the fields on TOOL itself
       (FUN_1806168b0 / WriteXML_MillToolList); multi-kind = subreports bound to TOOL-M / -L / -B / -R here
       (the tag -> kind code 10000 ... 40000 mapping of LoadRpxInfo is not traced) */
    char b[96];
    RNode *tn = rnode_new(root, "TOOL", NULL), *m = multi ? rnode_new(tn, router ? "TOOL-R" : "TOOL-M", NULL) : tn;
    snprintf(b, sizeof b, "%ld", num); rnode_new(m, "NUMBER", b);
    char c[80]; snprintf(c, sizeof c, "%s", t->name);
    char *cs = c + strspn(c, " \t"); size_t cl = strlen(cs); while (cl && (cs[cl - 1] == ' ' || cs[cl - 1] == '\t')) cs[--cl] = 0;
    if (*cs) rnode_new(m, "COMMENT", cs);
    make_rstr(t->dia, b, sizeof b, places); rnode_new(m, "DIAMETER", b);
    make_rstr(t->dia * 0.5, b, sizeof b, places); rnode_new(m, "RADIUS", b);
    make_rstr(t->corner_r, b, sizeof b, places); rnode_new(m, "CORNER-RADIUS", b);
    snprintf(b, sizeof b, "%ld", (long)t->dia_off); rnode_new(m, "DIAMETER-OFFSET", b);
    snprintf(b, sizeof b, "%ld", (long)t->len_off); rnode_new(m, "LENGTH-OFFSET", b);
}
/* the F.tools index of op i's tool: its tool slot (record +0x5ac / CamParams.tool_slot, 1-based TL order - the identity
   Mastercam uses, numbers can repeat), else the first tool with its number; -1 = none */
int op_tool_index(int i) {
    int k = -1; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) k = j;
    int slot = k >= 0 ? camops[k]->p.tool_slot : (F.ops[i].raw && F.ops[i].raw_n >= 0x5b0 ? (int)(F.ops[i].raw[0x5ac] | F.ops[i].raw[0x5ad] << 8 | F.ops[i].raw[0x5ae] << 16 | F.ops[i].raw[0x5af] << 24) : 0);
    int num = k >= 0 ? camops[k]->p.tool_num : F.ops[i].tool;
    if (slot > 0 && slot <= F.ntools && F.tools[slot - 1].num == num) return slot - 1;
    for (int t = 0; t < F.ntools; t++) if (F.tools[t].num == num) return t;
    return -1;
}
void tool_report(void) {
    int router = cm_router(), places = cfg_places >= 0 ? cfg_places : 4;
    const char *name = router ? cfg_rpt_router : cfg_rpt_mill;
    char rel[700], path[800];
    snprintf(rel, sizeof rel, "%sMSC\\%s.rpx", cfg_rpt_dir, name);
    if (!name[0] || !cfg_rpt_dir[0] || !ci_path(rel, path, sizeof path)) { message("Tool list", "Warning - report file does not exists!"); return; }   /* mccore string */
    RNode *root = rnode_new(NULL, "TOOLS", NULL);
    report_root_nodes(root, "Operations Manager", 0);   /* LIBNAME core 5028 */
    const Tool *seen[256]; int nseen = 0;
    int multi = rpx_binds(path, "TOOL-M") || rpx_binds(path, "TOOL-L") || rpx_binds(path, "TOOL-B") || rpx_binds(path, "TOOL-R");
    for (int i = 0; i < F.nops; i++) {
        if (!op_selected(i)) continue;
        int num = F.ops[i].tool; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) num = camops[j]->p.tool_num;
        int ti = op_tool_index(i); const Tool *t = ti >= 0 ? &F.tools[ti] : NULL;   /* one entry per tool slot (op +0x544) */
        if (!t) continue;                                    /* no tool: left out */
        int dup = 0; for (int k = 0; k < nseen; k++) if (seen[k] == t) dup = 1;
        if (dup || nseen >= 256) continue;
        seen[nseen++] = t;
        report_tool_node(root, t, num, multi, router, places);
    }
    rpx_layout_free(&tlr);
    if (!rpx_layout(path, &root, 1, &tlr)) message("Tool list", "%s", tlr.err);
    else { snprintf(tlr_title, sizeof tlr_title, "%s", name); show_tlr = 1; tlr_new = 1; }
    rnode_free(root);
}
/* ---- Report... / Detailed report... (FUN_180875590 -> FUN_18087c6b0 -> FUN_1806168b0 / WriteXML_MillToolList FUN_18061b5c0):
   template GetToolMatlRpx(true, 0, 1 | 2) = config 31006 TOOL LIST (MILL-SIMPLE) / 31007 TOOL LIST (MILL-DETAILED); every row of
   the list in list order; LIBNAME = the part file (part list) or the library path, FILTER = that list's Filter Active ---- */
void tm_report(int L, int detailed) {
    const char *name = detailed ? cfg_rpt_mill_det : cfg_rpt_mill_simple;
    char rel[700], path[800];
    snprintf(rel, sizeof rel, "%sMSC\\%s.rpx", cfg_rpt_dir, name);
    if (!name[0] || !cfg_rpt_dir[0] || !ci_path(rel, path, sizeof path)) {   /* FUN_1806405a0 fails: the text report of FUN_180875590 (NOT DONE) */
        snprintf(cam_msg, sizeof cam_msg, "Tool Manager report: no template %s", rel); return; }
    RNode *root = rnode_new(NULL, "TOOLS", NULL);
    report_root_nodes(root, L == 0 ? F.path : tlib.path, tm_factive[L]);
    int multi = rpx_binds(path, "TOOL-M") || rpx_binds(path, "TOOL-L") || rpx_binds(path, "TOOL-B") || rpx_binds(path, "TOOL-R");
    int idx[4096], n = tm_rows(L, idx);
    for (int k = 0; k < n; k++) { const Tool *t = tm_tool(L, idx[k]); report_tool_node(root, t, t->num, multi, cm_router(), cfg_places >= 0 ? cfg_places : 4); }
    rpx_layout_free(&tlr);
    if (!rpx_layout(path, &root, 1, &tlr)) message("Tool list", "%s", tlr.err);
    else { snprintf(tlr_title, sizeof tlr_title, "%s", name); show_tlr = 1; tlr_new = 1; }
    rnode_free(root);
}
/* tests: the report as laid out (pages, items; the TIME / DATE values change with the clock) */
void tlr_dump(void) {
    if (!show_tlr) { printf("    report: none\n"); return; }
    printf("    report \"%s\": %d page(s), %d item(s)\n", tlr_title, tlr.pages, tlr.n);
    for (int i = 0; i < tlr.n; i++) { const RItem *it = &tlr.it[i];
        if (it->line == 2) printf("      p%d image %g,%g %gx%g %d bytes mode %d\n", it->page, (double)it->x, (double)it->y, (double)it->w, (double)it->h, it->nimg, it->sizemode);
        else if (it->line) printf("      p%d line %g,%g %g,%g\n", it->page, (double)it->x, (double)it->y, (double)it->w, (double)it->h);
        else if (it->text && it->text[0]) printf("      p%d text %g,%g w%g a%d \"%s\"\n", it->page, (double)it->x, (double)it->y, (double)it->w, it->align, it->text); }
}

/* ------------------------------------------------------------------------------------------------ Renumber tools (was mcamui.c) */
/* CRenumbTl (IDD 805, formats/renumber_tools.md): Start / Increment / the two offset checks persist (DAT_1808a1908 / 190c /
   1a14 / 1a18), "unused tools" always opens checked (DAT_1808a1a1c is never written back), the amounts and the head check
   come from the machine (LOfsAmt / DOfsAmt / UseHeadForTool) */
int rn_start = 1, rn_inc = 1, rn_len_on = 1, rn_dia_on = 1;
int show_rn, rn_new, rn_unused, rn_head, rn_lofs, rn_dofs;
char rn_txt[4][16];                                   /* start, increment, length amount, diameter amount */
void rn_open(void) {
    rn_unused = 1;
    rn_lofs = F.ctl_found ? F.ctl_lofs : 0; rn_dofs = F.ctl_found ? F.ctl_dofs : 0; rn_head = F.ctl_found ? F.ctl_head : 0;
    snprintf(rn_txt[0], 16, "%d", rn_start); snprintf(rn_txt[1], 16, "%d", rn_inc);
    snprintf(rn_txt[2], 16, "%d", rn_lofs); snprintf(rn_txt[3], 16, "%d", rn_dofs);
    show_rn = 1; rn_new = 1;
}
void rn_apply(void) {
    int start = atoi(rn_txt[0]), inc = atoi(rn_txt[1]), lofs = atoi(rn_txt[2]), dofs = atoi(rn_txt[3]);
    rn_start = start; rn_inc = inc;                          /* UpdateData(TRUE) + persist */
    /* scope: the selected ops + every op using one of their tools (FUN_180459950) */
    char tin[256] = { 0 };
    for (int i = 0; i < F.nops; i++) if (op_selected(i)) { int t = op_tool_index(i); if (t >= 0 && t < 256) tin[t] = 1; }
    int ids[1024], nids = 0;
    for (int i = 0; i < F.nops && nids < 1024; i++) { int t = op_tool_index(i); if (op_selected(i) || (t >= 0 && t < 256 && tin[t])) ids[nids++] = F.ops[i].id; }
    /* stored ops in scope are taken over (their numbers live in their NCI / record in Mastercam) */
    for (int a = 0; a < nids; a++) {
        int i = -1; for (int j = 0; j < F.nops; j++) if (F.ops[j].id == ids[a]) i = j;
        if (i < 0) continue;
        int mine = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) mine = 1;
        if (mine) continue;
        CamOp tmp; char err[200];
        if (cam_fileop_type(&F.ops[i]) == CAM_LFACE) continue;   /* Lathe Face: its tool is its own ltool_rec (not a TlMgr tool) */
        if (!cam_op_from_file(&F, i, &tmp, err, sizeof err)) continue;
        CamOp *op = malloc(sizeof *op); *op = tmp;
        if (!cam_generate(&F, op)) { cam_op_free(op); continue; }
        { int at = op->p.file_op_id > 0 ? takeover_index(op->p.file_op_id) : -1;   /* taken over in place */
          remove_stored_op(op->p.file_op_id);
          add_op_at(op, at); }
        if (ncamops < MAX_CAMOPS) camops[ncamops++] = op; else cam_op_free(op);
    }
    /* the in-scope ops in tree order: each new tool (slot) takes the counter */
    int newnum[256], counter = start;
    for (int t = 0; t < 256; t++) newnum[t] = INT32_MIN;
    for (int i = 0; i < F.nops; i++) {
        int t = op_tool_index(i);
        if (t < 0 || t >= 256 || !tin[t]) continue;
        if (newnum[t] == INT32_MIN) { newnum[t] = counter; counter += inc; }
    }
    char used[256] = { 0 };
    for (int i = 0; i < F.nops; i++) { int t = op_tool_index(i); if (t >= 0 && t < 256) used[t] = 1; }   /* before anything is renumbered */
    /* write: the ops (tool #, head), then the tools (number, offsets) */
    for (int j = 0; j < ncamops; j++) {
        int t = op_tool_index(camops[j]->op_index);
        if (t < 0 || t >= 256 || newnum[t] == INT32_MIN) continue;
        CamOp *op = camops[j]; op->p.tool_num = newnum[t]; op->p.tool_slot = t + 1;
        if (rn_head) { int32_t h = newnum[t]; int k = 0; while (k < op->p.nrpatch && op->p.rpatch[k].off != 0x5a4) k++;
            if (k == op->p.nrpatch && op->p.nrpatch < CAM_NRPATCH) op->p.nrpatch++; else if (k == op->p.nrpatch) cam_rpatch_full(0x5a4);
            if (k < op->p.nrpatch) { op->p.rpatch[k].off = 0x5a4; op->p.rpatch[k].n = 4; op->p.rpatch[k].height = 0; memcpy(op->p.rpatch[k].v, &h, 4); } }
    }
    for (int t = 0; t < F.ntools && t < 256; t++) {
        if (newnum[t] != INT32_MIN) {
            F.tools[t].num = newnum[t];
            if (rn_len_on) F.tools[t].len_off = lofs + newnum[t];
            if (rn_dia_on) F.tools[t].dia_off = dofs + newnum[t];
        }
    }
    if (rn_unused)                                           /* tools no operation uses, continuing the counter */
        for (int t = 0; t < F.ntools && t < 256; t++) {
            if (used[t]) continue;
            F.tools[t].num = counter;
            if (rn_len_on) F.tools[t].len_off = lofs + counter;
            if (rn_dia_on) F.tools[t].dia_off = dofs + counter;
            counter += inc;
        }
    rn_start = counter;                                      /* the persisted Start = the running counter */
    camops_detach(); camops_attach();
    memset(op_msel, 0, sizeof op_msel); sel = -1;            /* the scope becomes the selection */
    for (int i = 0; i < F.nops && i < 1024; i++) { int t = op_tool_index(i); if (t >= 0 && t < 256 && tin[t]) { op_msel[i] = 1; if (sel < 0) sel = i; } }
    msel_owner = sel; sim = -1;
    doc_dirty = 1; scene_reupload();
}
void rn_ok(void) { rn_apply(); show_rn = 0; }               /* OnOK (FUN_1804574e0) */
void rn_cancel(void) { show_rn = 0; }                        /* Cancel persists nothing */

/* ------------------------------------------------------------------------------------------------ test steps */
/* tests: both lists as shown, the clipboard, the prompt, the flags (mcamui tm_script "dump") */
void tm_dump(void) {
    for (int q = 0; q < 2; q++) {
        int idx[4096], n = tm_rows(q, idx);
        printf("    tm list %d (%s, %d rows, view %d):\n", q, q ? (tlib_ok ? tlib.path : "-") : "part", n, tm_listview[q]);
        for (int k = 0; k < n; k++) { char s[160]; tm_item_text(tm_tool(q, idx[k]), s, sizeof s); printf("      %s%s | %s\n", tm_sel[q][idx[k]] ? "*" : " ", s, tm_tool(q, idx[k])->name); }
    }
    printf("    tm clip %d, prompt %d \"%s\", modified %d, bup %d, view %d, disp %d, confirm %d, wizard %d, show %d, lib %s\n", tm_nclip, tmq.kind, tmq.text,
           tm_lib_modified, tm_bup_done, tm_view, tm_dispmode, tm_confirm, tedit_index, show_tm, tlib_ok ? tlib.path : "-");
}
/* MCAMUI_SCRIPT "show:tm..." steps (after "show:tm"; was mcamui tm_script): returns 1 when the step is one of these. key: /
   size: are the front ends' (their key path / window) */
int cui_tm_script(const char *a) {
    int L = 0, v = 0;
    if (!tm_colinit) tm_settings_read();
    if (!strncmp(a, "copy:", 5)) tm_copy(atoi(a + 5));
    else if (!strncmp(a, "paste:", 6)) tm_paste(atoi(a + 6));
    else if (!strncmp(a, "focus:", 6)) tm_focus = atoi(a + 6);
    else if (!strncmp(a, "ans:", 4)) { if (a[4] == 'd' && tmq.dont_box) tmq.dont_box = 2; tm_q_answer(a[4] == 'Y' || a[5] == 'Y'); }
    else if (!strncmp(a, "view:", 5) && sscanf(a + 5, "%d,%d", &L, &v) == 2 && L >= 0 && L < 2) tm_listview[L] = v;
    else if (!strncmp(a, "mode:", 5)) tm_view = atoi(a + 5);
    else if (!strncmp(a, "disp:", 5)) tm_dispmode = atoi(a + 5);
    else if (!strncmp(a, "col:", 4) && sscanf(a + 4, "%d,%d", &L, &v) == 2 && L >= 0 && L < 2 && v >= 2 && v < TM_COLS) tm_col_toggle(L, v);
    else if (!strncmp(a, "sort:", 5) && sscanf(a + 5, "%d,%d", &L, &v) == 2 && L >= 0 && L < 2) tm_sortlist(L, v);
    else if (!strncmp(a, "report:", 7) && sscanf(a + 7, "%d,%d", &L, &v) == 2) tm_report(L, v);
    else if (!strncmp(a, "compress:", 9)) tm_compress(a + 9);
    else if (!strncmp(a, "saveto:", 7)) tm_save_to(a + 7);
    else if (!strncmp(a, "new:", 4)) tm_new(a + 4);
    else if (!strncmp(a, "browse:", 7)) tm_browse(a + 7);
    else if (!strncmp(a, "import:", 7)) { char p[1100]; CP(p, a + 7); char *b = strchr(p, '|'); if (b) { *b++ = 0; tm_import(p, b); } }
    else if (!strcmp(a, "ok")) tm_ok();
    else if (!strcmp(a, "cancel")) tm_cancel();
    /* ours (phase 7, both apps): fact:L,V Filter Active, conf:Y|N the delete confirmation, click:L,I[,c|s|r] a row click
       (row I of the shown order; Ctrl / Shift / right), dbl:L,I a double click, mitem:L,TEXT a menu 1225 item by its text, mdump:L,
       fset:KEY=V the open filter's controls, fok / fcancel, filters (the active filter), rdump (the report) */
    else if (!strncmp(a, "fact:", 5) && sscanf(a + 5, "%d,%d", &L, &v) == 2 && L >= 0 && L < 2) tm_factive[L] = v != 0;
    else if (!strncmp(a, "conf:", 5)) { if (tm_confirm) tm_confirm_answer(a[5] == 'Y'); }
    else if (!strncmp(a, "click:", 6) || !strncmp(a, "dbl:", 4)) {
        int dbl = a[0] == 'd', r = 0; const char *q = a + (dbl ? 4 : 6);
        if (sscanf(q, "%d,%d", &L, &r) == 2 && L >= 0 && L < 2) {
            int idx[4096], n = tm_rows(L, idx); const char *m = strchr(strchr(q, ',') + 1, ',');
            if (r >= 0 && r < n) {
                if (dbl) { tm_click(L, idx[r], r, idx, n, 0, 0, 0); tm_dblclick(L, idx[r]); }
                else tm_click(L, idx[r], r, idx, n, m && m[1] == 'c', m && m[1] == 's', m && m[1] == 'r');
            }
        }
    }
    else if (!strncmp(a, "mitem:", 6) && sscanf(a + 6, "%d", &L) == 1 && strchr(a + 6, ',') && L >= 0 && L < 2) {
        const char *t = strchr(a + 6, ',') + 1; TmMItem it[64]; int n = tm_menu_items(L, it, 64), done = 0;
        for (int k = 0; k < n && !done; k++) if (it[k].text && !strcmp(it[k].text, t)) {
            done = 1;
            if (!it[k].en || it[k].act == TMA_NONE || it[k].act == TMA_SAVETO || it[k].act == TMA_COMPRESS) printf("    (menu item %s %s)\n", t, it[k].en ? "needs a file" : "greyed");
            else tm_menu_act(L, it[k].act);
        }
        if (!done) printf("    (no menu item %s)\n", t);
    }
    else if (!strcmp(a, "mdump") || !strncmp(a, "mdump:", 6)) {
        L = a[5] == ':' ? atoi(a + 6) : 0; TmMItem it[64]; int n = tm_menu_items(L, it, 64);
        for (int k = 0; k < n; k++) printf("      menu %*s%s%s%s\n", it[k].depth * 2, "", it[k].text ? it[k].text : "----", it[k].en ? "" : " (greyed)", it[k].checked ? " *" : "");
    }
    else if (!strncmp(a, "fset:", 5) && tmf_open) {
        char k[32]; const char *eq = strchr(a + 5, '='); if (!eq || eq - (a + 5) > 30) return 1;
        memcpy(k, a + 5, eq - (a + 5)); k[eq - (a + 5)] = 0; const char *val = eq + 1; TmFilt *f = &tmf_edit;
        if (!strncmp(k, "type", 4)) { int ty = atoi(k + 4); if (ty >= 0 && ty < 26) f->type[ty] = atoi(val) != 0; }
        else if (!strncmp(k, "mat", 3)) { int m = atoi(k + 3); if (m >= 0 && m < 6) f->mat[m] = atoi(val) != 0; }
        else if (!strncmp(k, "rad", 3)) { int m = atoi(k + 3); if (m >= 0 && m < 3) f->rad[m] = atoi(val) != 0; }
        else if (!strcmp(k, "op")) f->op = atoi(val); else if (!strcmp(k, "unit")) f->unit = atoi(val); else if (!strcmp(k, "dsel")) f->dsel = atoi(val);
        else if (!strcmp(k, "d1")) f->d1 = atof(val); else if (!strcmp(k, "d2")) f->d2 = atof(val);
        else if (!strcmp(k, "all")) tmf_types_all(1); else if (!strcmp(k, "none")) tmf_types_all(0);
        else if (!strcmp(k, "matall")) memset(f->mat, 1, 6); else if (!strcmp(k, "matnone")) memset(f->mat, 0, 6);
        else if (!strcmp(k, "reset")) tmf_defaults(f);
    }
    else if (!strcmp(a, "fok")) { if (tmf_open) tmf_ok(); }
    else if (!strcmp(a, "fcancel")) tmf_open = 0;
    else if (!strcmp(a, "filters")) {
        const TmFilt *f = tmf_open ? &tmf_edit : &tmf;
        printf("    filter%s: types", tmf_open ? " (dialog)" : ""); for (int k = 0; k < 26; k++) printf("%d", f->type[k]);
        printf(" mat %d%d%d%d%d%d rad %d%d%d op %d unit %d dsel %d d1 %g d2 %g active %d %d\n", f->mat[0], f->mat[1], f->mat[2], f->mat[3], f->mat[4], f->mat[5],
               f->rad[0], f->rad[1], f->rad[2], f->op, f->unit, f->dsel, f->d1, f->d2, tm_factive[0], tm_factive[1]);
    }
    else if (!strcmp(a, "rdump")) tlr_dump();
    else if (!strcmp(a, "dump")) tm_dump();
    else return 0;
    return 1;
}
/* the tool steps of MCAMUI_SCRIPT "show:ARG" both apps share (was mcamui run_script): tm[:LIB], tmsel:L,I, tm* (cui_tm_script),
   tmup, tmdown, tmdel:L, tmfilter, tnew, twtype:T, twpage:P, coolant, twfinish, twset:F=V, tedit:N, holder:LIB|NAME|NUM;
   ours (phase 7): twprop:ID=V (a Finalize Properties field), twstd:I (a standard size row), twwarn:Y|N|dY, cool:I=0|1
   (On / Off), coolok, coolcancel, twcancel, twdump, tlrclose. 1 = handled. tmmenu:L is the front ends' (their menu) */
int cui_tool_show_step(const char *arg) {
    if (!strcmp(arg, "tm") || !strncmp(arg, "tm:", 3)) { show_tm = 1; if (arg[2] == ':') tm_open_lib(arg + 3); tm_open_init(); }
    else if (!strcmp(arg, "tnew")) tedit_open(-1, NULL);                       /* Define Tool wizard */
    else if (!strncmp(arg, "tmsel:", 6)) { int L = atoi(arg + 6), i = atoi(strchr(arg + 6, ',') ? strchr(arg + 6, ',') + 1 : "0"); if (L >= 0 && L < 2 && i >= 0 && i < 4096) { tm_sel[L][i] = 1; tm_focus = L; } }
    else if (!strcmp(arg, "tmup")) tm_up();
    else if (!strcmp(arg, "tmdown")) tm_down();
    else if (!strncmp(arg, "tmdel:", 6)) { int L = atoi(arg + 6); if (L == 1) tm_confirm = 0, tm_delete(1); else tm_delete(0); }
    else if (!strcmp(arg, "tmfilter")) tm_open_filter();
    else if (!strncmp(arg, "tm", 2) && strncmp(arg, "tmmenu:", 7) && strncmp(arg, "tmkey:", 6) && strncmp(arg, "tmsize:", 7) && cui_tm_script(arg + 2)) { }
    else if (cui_tw_show_step(arg)) { }                                         /* the wizard / coolant steps (cui_toolwiz.c) */
    else if (!strcmp(arg, "tlrclose")) show_tlr = 0;
    else if (!strncmp(arg, "holder:", 7)) {           /* holder:LIB|NAME|TOOLNUM - put a holder from LIB on tool TOOLNUM */
        char a[1100]; CP(a, arg + 7); char *b1 = strchr(a, '|'), *b2 = b1 ? strchr(b1 + 1, '|') : NULL;
        if (b2) { *b1++ = 0; *b2++ = 0; tm_open_lib(a); Tool *tl = NULL; for (int i = 0; i < F.ntools; i++) if (F.tools[i].num == atoi(b2)) tl = &F.tools[i];
            for (int i = 0; tl && tlib_ok && i < tlib.nh; i++) if (!strcmp(tlib.h[i].name, b1)) { tool_set_holder(tl, &tlib.h[i]); doc_dirty = 1; break; } }
    }
    else return 0;
    return 1;
}
/* the Renumber tools steps (was mcamui run_script): renum, rnset:K=V (0 start 1 inc 2 len 3 dia, head, unused, lon, don), rnok;
   ours: rncancel, rndump */
int cui_rn_script(const char *st, const char *arg) {
    if (!strcmp(st, "renum")) rn_open();
    else if (!strcmp(st, "rnset") && arg) { int k = atoi(arg); const char *v = strchr(arg, '='); if (v) { v++;
        if (!strncmp(arg, "head", 4)) rn_head = atoi(v); else if (!strncmp(arg, "unused", 6)) rn_unused = atoi(v);
        else if (!strncmp(arg, "lon", 3)) rn_len_on = atoi(v); else if (!strncmp(arg, "don", 3)) rn_dia_on = atoi(v);
        else if (k >= 0 && k < 4) snprintf(rn_txt[k], 16, "%s", v); } }
    else if (!strcmp(st, "rnok")) rn_ok();
    else if (!strcmp(st, "rncancel")) rn_cancel();
    else if (!strcmp(st, "rndump")) printf("    renumber %s: start \"%s\" inc \"%s\" len %d \"%s\" dia %d \"%s\" head %d unused %d (persisted start %d inc %d)\n", show_rn ? "open" : "closed",
                                         rn_txt[0], rn_txt[1], rn_len_on, rn_txt[2], rn_dia_on, rn_txt[3], rn_head, rn_unused, rn_start, rn_inc);
    else if (!strcmp(st, "tlrdump")) tlr_dump();
    else if (!strcmp(st, "toolrpt")) tool_report();   /* ours: the Toolpaths manager's Tool list... (CM_TOOLLIST) */
    else if (!strcmp(st, "tools")) return 0;
    else return 0;
    return 1;
}
