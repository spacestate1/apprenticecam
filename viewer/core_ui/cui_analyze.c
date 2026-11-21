/* cui_analyze.c - Analyze Entity Properties and the Colors / Levels pickers, moved out of viewer/mcamui.c in phase 8 of
 * formats/qt_port_plan.md (names and citations as they were there; formats/analyze.md, formats/level_picker.md). Both front
 * ends draw this model: the dialogs from the templates below (dialog units; mcamui BS_DX / BS_DY, Qt the template font's
 * base units). */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "core_ui.h"
#include "occt_mesh.h"
#include "occt_cad.h"

/* ------------------------------------------------------------------------------------------------ Colors picker */
/* Colors picker = sel_color_dialog (mastercam.dll 18033f4a0): CColorDlg (opmanres IDD 593 "Colors", OK / Help, no
   Cancel - the X cancels) holding a property sheet with CColorPage1 (IDD 106 "Colors": "Current color:" edit 226 +
   swatch 955 + "&Select" 213, enabled when the caller allows picking from an entity; FUN_18011a950 adds 16 x 16
   CColorBtn of 20 px on a 22 px pitch from 50 px down, colour i = palette i row by row) and CColorPage2 (IDD 108
   "Customize": palette RGB sliders - NOT DONE, greyed). A button click selects its colour, a double-click selects and
   closes with OK (CColorBtn: BN_CLICKED / WM_LBUTTONDBLCLK -> FUN_18011a8b0). Typing a number: > 255 restores the
   current one; the background colour (gr_back_color, config 5007) is refused with 4104 "Action not allowed" / 4105 and
   the previous colour (or 10) comes back (FUN_18011ae00). Select (213): prompt 2798 "Select an entity", its colour.
   The dialog returns the palette index (FUN_180119970 + the Wd_colors lookup). Its window position is NOT DECODED. */
int cdlg_open, cdlg_new, cdlg_cur, cdlg_prev, cdlg_select_ok, cdlg_picking;
char cdlg_txt[8];
static void (*cdlg_done)(int color);
void color_dialog(int cur, int allow_select, void (*done)(int)) {
    cdlg_cur = cdlg_prev = cur >= 0 && cur < 256 ? cur : 10; snprintf(cdlg_txt, sizeof cdlg_txt, "%d", cdlg_cur);
    cdlg_select_ok = allow_select; cdlg_done = done; cdlg_open = 1; cdlg_new = 1; cdlg_picking = 0;
}
void color_dialog_pick_ent(int e) {                 /* Select: the picked entity's colour */
    cdlg_picking = 0; cam_msg[0] = 0;
    if (e < 0 || e >= F.nents || !F.ents[e].type) return;
    cdlg_cur = F.ents[e].color; snprintf(cdlg_txt, sizeof cdlg_txt, "%d", cdlg_cur);
}
void cdlg_commit(void) {                            /* the edit lost focus / Enter (FUN_18011ae00) */
    int v = atoi(cdlg_txt);
    if (v > 255) v = cdlg_cur;
    if (v == cfg_back_color) { cui_message("Action not allowed", "The system entity color matches the current background color. Restoring previous system entity color.\n  You should select a different color, or, change the background color.");
                               v = cdlg_cur == cfg_back_color ? 10 : cdlg_cur; }
    cdlg_cur = v; snprintf(cdlg_txt, sizeof cdlg_txt, "%d", v);
}
void cdlg_click(int i) { if (i < 0 || i > 255) return; cdlg_cur = i; snprintf(cdlg_txt, sizeof cdlg_txt, "%d", i); }   /* a CColorBtn */
void cdlg_select(void) { if (!cdlg_select_ok) return; cdlg_picking = 1; snprintf(cam_msg, sizeof cam_msg, "Select an entity"); }   /* 213, prompt 2798 */
void cdlg_close(int ok) { cdlg_open = 0; if (ok && cdlg_done) cdlg_done(cdlg_cur); }   /* OK / a double-click (1), the X (0) */

/* ------------------------------------------------------------------------------------------------ Levels picker */
/* Levels picker = sel_level_dialog (mastercam.dll 18047d5b0, CSelLevel in picker mode, opmanres IDD 563 "Levels" -
   ../formats/level_picker.md): list (Number / Name, opmanres 1979 / 1948; the main level's row yellow, green when
   selected), "Selected Level" Number / Name, "List Levels" Used / Named / Used or named / Range with the range edits,
   the select-entity button (1327, prompt opmanres 214 "Select entity"), OK / Help; X = Cancel. Start: the caller's level
   (1 if 0). The radio and the range are the workspace's LevelListDisplay / Levellistlowrange / Levelhighlowrange
   (CONFIG/Mastercam.Workspace). Click = Number / Name + result; typing a number selects (creates) that level. Level
   names are not decoded in this program (Name column empty, Named = the main level only; name edit greyed). */
int ldlg_open, ldlg_new, ldlg_cur, ldlg_radio = -1, ldlg_lo = 1, ldlg_hi = 1000, ldlg_picking;
char ldlg_txt[16], ldlg_lo_t[16], ldlg_hi_t[16];
static void (*ldlg_done)(int level);
void level_dialog(int cur, void (*done)(int)) {
    if (ldlg_radio < 0) {                                     /* workspace state, once */
        char wsp[1100]; FILE *f = fopen(site_file(wsp, sizeof wsp, "user", "CONFIG/Mastercam.Workspace"), "r"); char line[512]; ldlg_radio = 2;
        while (f && fgets(line, sizeof line, f)) { int v;
            if (sscanf(line, "<LevelListDisplay>\"%d", &v) == 1) ldlg_radio = v;
            else if (sscanf(line, "<Levellistlowrange>\"%d", &v) == 1) ldlg_lo = v;
            else if (sscanf(line, "<Levelhighlowrange>\"%d", &v) == 1) ldlg_hi = v; }
        if (f) fclose(f);
        if (ldlg_radio < 0 || ldlg_radio > 3) ldlg_radio = 2;
    }
    ldlg_cur = cur > 0 ? cur : 1; snprintf(ldlg_txt, sizeof ldlg_txt, "%d", ldlg_cur);
    snprintf(ldlg_lo_t, sizeof ldlg_lo_t, "%d", ldlg_lo); snprintf(ldlg_hi_t, sizeof ldlg_hi_t, "%d", ldlg_hi);
    ldlg_done = done; ldlg_open = 1; ldlg_new = 1; ldlg_picking = 0;
}
void level_dialog_pick_ent(int e) {
    ldlg_picking = 0; cam_msg[0] = 0;
    if (e < 0 || e >= F.nents || !F.ents[e].type) return;
    ldlg_cur = F.ents[e].level; snprintf(ldlg_txt, sizeof ldlg_txt, "%d", ldlg_cur);
}
/* the list rows (FUN_18047c950 filters): Range = the range's empty levels first, then the used ones; else Number ascending */
int ldlg_rows(int *rows, int cap) {
    static int counts[4096];
    memset(counts, 0, sizeof counts);
    for (int i = 0; i < F.nents; i++) if (F.ents[i].type && F.ents[i].level >= 0 && F.ents[i].level < 4096) counts[F.ents[i].level]++;
    int nr = 0;
    if (ldlg_radio == 3) {
        for (int l = ldlg_lo; l <= ldlg_hi && l < 4096 && nr < cap; l++) if (!counts[l] && l != geo_level) rows[nr++] = l;   /* no level yet */
        for (int l = ldlg_lo; l <= ldlg_hi && l < 4096 && nr < cap; l++) if (counts[l] || l == geo_level) rows[nr++] = l;
    } else for (int l = 1; l < 4096 && nr < cap; l++) {
        int used = counts[l] || l == geo_level, named = l == geo_level;
        if ((ldlg_radio == 0 && used) || (ldlg_radio == 1 && named) || (ldlg_radio == 2 && (used || named)) || l == ldlg_cur) rows[nr++] = l;
    }
    if (ldlg_radio != 3) for (int a = 0; a < nr; a++) for (int b = a + 1; b < nr; b++) if (rows[b] < rows[a]) { int t = rows[a]; rows[a] = rows[b]; rows[b] = t; }   /* Number ascending */
    return nr;
}
void ldlg_click(int level) { ldlg_cur = level; snprintf(ldlg_txt, sizeof ldlg_txt, "%d", ldlg_cur); }
void ldlg_commit(void) { int v = atoi(ldlg_txt); if (v > 0) ldlg_cur = v; snprintf(ldlg_txt, sizeof ldlg_txt, "%d", ldlg_cur); }   /* typing a number selects it */
void ldlg_range_commit(int hi) {                     /* range 744 / 301: only with Range */
    int v = atoi(hi ? ldlg_hi_t : ldlg_lo_t);
    if (hi) { if (v >= 1 && v <= 2000000000) ldlg_hi = v; } else if (v >= 1) ldlg_lo = v;
    snprintf(ldlg_lo_t, 16, "%d", ldlg_lo); snprintf(ldlg_hi_t, 16, "%d", ldlg_hi);
}
void ldlg_select(void) { ldlg_picking = 1; snprintf(cam_msg, sizeof cam_msg, "Select entity"); }   /* 1327, opmanres 214 */
void ldlg_close(int ok) { ldlg_open = 0; if (ok && ldlg_done) ldlg_done(ldlg_cur); }
/* opmanres 563 / 593 / 106 templates (exports/opmanres_dialogs.tsv) */
const BsCtl LDLG_563[] = {
    { 1371, 7, 7, 7, 292, 117, "" }, { 1375, 8, 7, 135, 160, 101, "Selected Level" }, { 1378, 0, 12, 148, 42, 9, "Number:" },
    { 1379, 0, 64, 148, 60, 10, "Name:" }, { 1359, 2, 12, 159, 47, 14, "" }, { 1313, 2, 63, 159, 100, 14, "" }, { 1327, 1, 131, 217, 30, 16, "" },
    { 1376, 8, 172, 135, 128, 101, "List Levels" }, { 1363, 5, 175, 147, 115, 10, "Used" }, { 1364, 5, 175, 159, 115, 10, "Named" },
    { 1365, 5, 175, 171, 115, 10, "Used or named" }, { 1366, 5, 175, 184, 115, 10, "Range" }, { 744, 2, 186, 198, 54, 14, "" },
    { 301, 2, 185, 217, 55, 14, "" }, { 1, 1, 225, 240, 36, 18, "Ok" }, { 9, 1, 265, 240, 36, 18, "Help" }, { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl CDLG_593[] = {   /* 593 "Colors" 250 x 150 (static 230 = the sheet at 4,4) + page 106 "Colors" 241 x 98 */
    { 230, 0, 4, 4, 242, 120, "" }, { 1, 1, 171, 128, 36, 18, "OK" }, { 9, 1, 210, 128, 36, 18, "Help" },
    { 3001, 0, 8, 8, 47, 14, "Current color:" }, { 226, 2, 60, 8, 16, 14, "" }, { 955, 1, 80, 8, 16, 14, "" }, { 213, 1, 100, 8, 50, 14, "Select" },
    { 0, 0, 0, 0, 0, 0, NULL } };

/* ------------------------------------------------------------------------------------------------ Analyze */
/* Analyze dialogs: mccadutilres templates 1073 Point / 1080 Line / 6470 Arc Properties (exports/mccadutilres_dialogs.tsv) */
const BsCtl AZ_1073[] = {
    { 413, 2, 57, 30, 40, 14, "" }, { 419, 2, 57, 45, 40, 14, "" }, { 869, 2, 57, 60, 40, 14, "" }, { 867, 1, 61, 77, 20, 18, "|AutoCursor" },
    { 864, 6, 53, 124, 60, 63, "" }, { 891, 1, 53, 141, 36, 18, "|Propagate" }, { 1, 1, 20, 167, 36, 18, "|&OK" }, { 2, 1, 60, 167, 36, 18, "|&Cancel" },
    { 9, 1, 100, 167, 36, 18, "|&Help" }, { 877, 1, 34, 7, 36, 18, "|P&revious" }, { 875, 1, 72, 7, 36, 18, "|Nex&t" },
    { 4294901759, 0, 29, 123, 18, 12, "Style" }, { 2961, 0, 23, 27, 97, 1, "#1082" }, { 4294901759, 0, 23, 97, 97, 1, "#1082" },
    { 2962, 0, 23, 162, 97, 1, "#1082" }, { 25261, 1, 12, 101, 20, 18, "|Level" }, { 25114, 1, 73, 101, 20, 18, "|Color" },
    { 25117, 2, 95, 101, 36, 15, "" }, { 430, 2, 34, 101, 36, 15, "" }, { 2963, 0, 22, 29, 32, 10, "X" }, { 2965, 0, 22, 44, 32, 10, "Y" },
    { 2966, 0, 22, 59, 32, 10, "Z" }, { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl AZ_1080[] = {
    { 892, 2, 19, 47, 40, 14, "" }, { 893, 2, 19, 62, 40, 14, "" }, { 894, 2, 19, 77, 40, 14, "" }, { 901, 1, 29, 92, 20, 19, "|AutoCursor" },
    { 437, 2, 84, 47, 40, 14, "" }, { 441, 2, 84, 62, 40, 14, "" }, { 873, 2, 84, 77, 40, 14, "" }, { 870, 1, 93, 92, 20, 19, "|AutoCursor" },
    { 25056, 2, 68, 113, 40, 14, "" }, { 897, 2, 68, 134, 40, 14, "" }, { 899, 2, 68, 154, 40, 14, "" }, { 874, 2, 19, 189, 33, 14, "" },
    { 878, 2, 55, 189, 33, 14, "" }, { 880, 2, 91, 189, 33, 14, "" }, { 895, 6, 53, 231, 60, 66, "" }, { 896, 6, 53, 245, 60, 52, "" },
    { 891, 1, 53, 260, 36, 18, "|Propagate" }, { 1, 1, 20, 285, 36, 18, "|&OK" }, { 2, 1, 60, 285, 36, 18, "|&Cancel" }, { 9, 1, 100, 285, 36, 18, "|&Help" },
    { 10559, 0, 12, 231, 35, 12, "Style" }, { 10558, 0, 12, 245, 35, 12, "Width" }, { 908, 0, 28, 181, 7, 8, "#1081" }, { 909, 0, 65, 181, 7, 8, "#1081" },
    { 910, 0, 101, 181, 7, 8, "#1081" }, { 4294901759, 0, 22, 135, 22, 10, "3D" }, { 4294901759, 0, 22, 155, 22, 10, "2D" },
    { 907, 0, 47, 154, 16, 15, "#1178" }, { 906, 0, 47, 134, 16, 15, "#1178" }, { 25057, 0, 47, 113, 16, 15, "#1172" },
    { 877, 1, 34, 7, 36, 18, "|P&revious" }, { 875, 1, 72, 7, 36, 18, "|Nex&t" }, { 2961, 0, 23, 27, 97, 1, "#1082" },
    { 4294901759, 0, 23, 206, 97, 1, "#1082" }, { 2962, 0, 23, 281, 97, 1, "#1082" }, { 905, 0, 47, 31, 48, 15, "#1189" },
    { 25261, 1, 12, 210, 20, 17, "|Level" }, { 25114, 1, 73, 210, 20, 18, "|Color" }, { 25117, 2, 95, 211, 36, 15, "" }, { 430, 2, 34, 211, 36, 15, "" },
    { 2963, 0, 68, 47, 8, 15, "X" }, { 2965, 0, 68, 62, 8, 15, "Y" }, { 2966, 0, 68, 77, 8, 15, "Z" }, { 2967, 0, 34, 181, 8, 8, "X" },
    { 2969, 0, 71, 181, 8, 8, "Y" }, { 2971, 0, 107, 181, 8, 8, "Z" }, { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl AZ_6470[] = {
    { 843, 1, 81, 29, 36, 18, "|Flip view" }, { 867, 1, 103, 67, 20, 18, "AutoCursor|Use the AutoCursor to move arc" },
    { 437, 2, 57, 52, 40, 14, "" }, { 441, 2, 57, 68, 40, 14, "" }, { 873, 2, 57, 82, 40, 14, "" },
    { 870, 1, 99, 116, 20, 18, "AutoCursor|Use the AutoCursor to resize arc" }, { 25367, 2, 54, 105, 40, 14, "" }, { 415, 2, 54, 126, 40, 14, "" },
    { 25056, 2, 54, 150, 40, 14, "" }, { 897, 2, 54, 172, 40, 14, "" }, { 904, 2, 28, 209, 40, 14, "" }, { 420, 2, 75, 209, 40, 14, "" },
    { 25261, 1, 7, 229, 20, 18, "|Level" }, { 430, 2, 29, 230, 37, 15, "" }, { 25114, 1, 79, 229, 20, 18, "|Color" }, { 25117, 2, 101, 230, 35, 15, "" },
    { 895, 6, 53, 249, 60, 66, "" }, { 896, 6, 53, 263, 60, 52, "" }, { 891, 1, 53, 278, 36, 18, "|Propagate" }, { 1, 1, 20, 302, 36, 18, "Check|Ok" },
    { 2, 1, 60, 302, 36, 18, "|Cancel" }, { 9, 1, 100, 302, 36, 18, "?|Help" }, { 2963, 0, 45, 52, 8, 15, "X" }, { 2965, 0, 45, 68, 8, 15, "Y" },
    { 2966, 0, 45, 83, 8, 15, "Z" }, { 10559, 0, 30, 249, 18, 12, "Style" }, { 10558, 0, 30, 262, 18, 12, "Width" }, { 25057, 0, 34, 150, 16, 15, "#1172" },
    { 915, 0, 34, 126, 16, 15, "#1213" }, { 914, 0, 34, 105, 16, 15, "#1128" }, { 4294901759, 0, 13, 175, 20, 8, "3D" }, { 916, 0, 34, 172, 16, 15, "#1178" },
    { 902, 2, 45, 30, 20, 12, "" }, { 4294901759, 0, 7, 31, 34, 10, "View" }, { 911, 0, 23, 68, 16, 15, "#1119" }, { 917, 0, 41, 192, 16, 15, "#1129" },
    { 918, 0, 88, 192, 16, 15, "#1130" }, { 4294901759, 0, 23, 100, 97, 1, "#1082" }, { 4294901759, 0, 23, 145, 97, 1, "#1082" },
    { 4294901759, 0, 23, 226, 97, 1, "#1082" }, { 2962, 0, 23, 298, 97, 1, "#1082" }, { 877, 1, 34, 7, 36, 18, "|P&revious" },
    { 875, 1, 72, 7, 36, 18, "|Nex&t" }, { 2961, 0, 23, 27, 97, 1, "#1082" }, { 4294901759, 0, 23, 50, 97, 1, "#1082" }, { 0, 0, 0, 0, 0, 0, NULL } };
/* mccadutilres 6471 "NURBS Surface" (CAnalyzeSurfaceNurbs) / 6528 "Trimmed NURBS Surface" (CAnalyzeSurfaceTrimmedNurbs): the
   factory FUN_18008b600 for class 0x100 kind 0 / kind 10 on a NURBS base (decomp/analyze/surf.c) */
const BsCtl AZ_6471[] = {
    { 844, 1, 53, 68, 36, 18, "|&Flip Normals" }, { 895, 6, 53, 113, 60, 66, "" }, { 896, 6, 53, 127, 60, 52, "" }, { 891, 1, 53, 143, 36, 18, "|Propagate" }, 
    { 2962, 0, 23, 164, 97, 1, "#1082" }, { 1, 1, 20, 167, 36, 18, "Check|OK" }, { 2, 1, 60, 167, 36, 18, "|Cancel" }, { 9, 1, 100, 167, 36, 18, "?|Help" }, 
    { 877, 1, 34, 7, 36, 18, "|P&revious" }, { 875, 1, 72, 7, 36, 18, "|Nex&t" }, { 4294901759, 0, 29, 113, 18, 12, "Style" }, { 4294901759, 0, 29, 127, 18, 12, "Width" }, 
    { 2961, 0, 23, 27, 97, 1, "#1082" }, { 4294901759, 0, 23, 88, 97, 1, "#1082" }, { 926, 2, 75, 32, 45, 14, "" }, { 4294901759, 0, 7, 35, 60, 8, "Control Points" }, 
    { 920, 2, 75, 49, 45, 14, "" }, { 4294901759, 0, 7, 52, 60, 8, "Degree" }, { 25261, 1, 12, 92, 20, 18, "|Level" }, { 25114, 1, 73, 92, 20, 18, "|Color" }, 
    { 25117, 2, 95, 93, 36, 15, "" }, { 430, 2, 34, 93, 36, 15, "" }, { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl AZ_6528[] = {
    { 926, 2, 75, 32, 45, 14, "" }, { 920, 2, 75, 49, 45, 14, "" }, { 931, 2, 77, 76, 45, 14, "" }, { 922, 2, 77, 93, 45, 14, "" }, 
    { 844, 1, 53, 109, 36, 18, "|Flip Normals" }, { 895, 6, 53, 154, 60, 66, "" }, { 896, 6, 53, 168, 60, 52, "" }, { 891, 1, 54, 184, 36, 18, "*|Propagate" }, 
    { 2962, 0, 23, 205, 97, 1, "#1082" }, { 1, 1, 20, 208, 36, 18, "|OK" }, { 2, 1, 60, 208, 36, 18, "|Cancel" }, { 9, 1, 100, 208, 36, 18, "|Help" }, 
    { 877, 1, 34, 7, 36, 18, "|Previous" }, { 875, 1, 72, 7, 36, 18, "|Next" }, { 4294901759, 0, 29, 154, 18, 12, "Style" }, { 4294901759, 0, 29, 168, 18, 12, "Width" }, 
    { 2961, 0, 23, 27, 97, 1, "#1082" }, { 4294901759, 0, 23, 129, 97, 1, "#1082" }, { 4294901759, 0, 23, 35, 44, 8, "Control Points" }, { 4294901759, 0, 23, 52, 24, 8, "Degree" }, 
    { 25261, 1, 12, 133, 20, 18, "|Level" }, { 25114, 1, 73, 133, 20, 18, "|Color" }, { 25117, 2, 95, 134, 36, 15, "" }, { 430, 2, 34, 134, 36, 15, "" }, 
    { 4294901759, 0, 23, 66, 97, 1, "#1082" }, { 4294901759, 0, 15, 67, 43, 10, "Trim:" }, { 4294901759, 0, 11, 79, 60, 10, "Boundaries" }, { 4294901759, 0, 11, 95, 60, 10, "Boundary points" }, 
    { 0, 0, 0, 0, 0, 0, NULL } };
/* mccadutilres 6494 (CAnalyzeSolid, exports/mccadutilres_dialogs.tsv; the template caption is a placeholder, OnInitDialog sets
   the title) */
const BsCtl AZ_6494[] = {
    { 1807, 2, 19, 29, 105, 12, "" }, { 891, 1, 53, 105, 36, 18, "*|Propagate" }, { 1, 1, 20, 129, 36, 18, "Check|Ok" },
    { 2, 1, 60, 129, 36, 18, "|Cancel" }, { 9, 1, 100, 129, 36, 18, "?|Help" }, { 877, 1, 32, 7, 36, 18, "|P&revious" },
    { 875, 1, 75, 7, 36, 18, "|Nex&t" }, { 2961, 0, 23, 27, 97, 1, "#1082" }, { 4294901759, 0, 23, 81, 97, 1, "#1082" },
    { 2962, 0, 23, 125, 97, 1, "#1082" }, { 4294901759, 0, 8, 48, 70, 10, "# of Operations" }, { 416, 2, 85, 47, 30, 12, "" },
    { 1811, 2, 7, 65, 129, 12, "" }, { 25261, 1, 12, 85, 20, 18, "|Level" }, { 25114, 1, 73, 85, 20, 18, "|Color" },
    { 25117, 2, 95, 86, 36, 15, "" }, { 430, 2, 34, 86, 36, 15, "" }, { 0, 0, 0, 0, 0, 0, NULL } };

/* Analyze Entity Properties (mccadutil AnalyzeStart 18008d050, CAnalyzePoint / CAnalyzeLine / CAnalyzeArc -
   ../formats/analyze.md). The analyze list = the pre-selection, else the picked entity; one modeless dialog per entity
   type, values relative to the active plane (Top, origin 0 here) in "%.*f" with config 29003 places (n_places_an),
   trailing zeros cut. An edit applies when its field loses focus (EN_KILLFOCUS -> Apply): the first changed field
   wins (line: 3D length keeps the start, angle keeps the start and the 2D length; arc: diameter, radius, sweep,
   start angle keeping the end, end angle, arc length, centre), the entity is rewritten and every field recomputed.
   OK / Esc commit (one undo step), Cancel / X restore the original; Previous / Next commit and step through the list
   with wrap (hidden, with Propagate, for one entity - the dialog then moves up by 0x20 DLU); Propagate gives the
   other listed entities the dialog's level (and its colour when that changed). AutoCursor: pick a new point position
   (5342). Here the entity is rewritten as a new record (mod_del + new) and the view number is the Top view's.
   Color button: the Colors picker (sel_color_dialog, color_dialog). Level button: the Levels picker (sel_level_dialog).
   NOT DONE (greyed): Style / Width combos (line style and width are not modelled), arc resize AutoCursor (FUN_1800734d0),
   Flip view, Help, dialog position. */
int az_list[512], az_n, az_i = -1, az_ent = -1, az_pick, az_changed, az_open_new, analyze_pick;
static int az_col0;                                         /* colour at open / last propagate */
static Ent az_orig; static int az_orig_ent;
/* a part solid (the factory FUN_18008b600's class 0x10000 case: CAnalyzeSolid ctor FUN_180086d20, dialog 6494; its methods
   decomp/dblclk/cadu_solid_slots.c): az_sol = its F.solids index while the dialog is up (az_ent = -1), with the values at open */
int az_sol = -1;
static char az_sdesc0[256]; static int az_slv0, az_sco0, az_sedit0;
char az_txt[AZ_NIDS][260]; static char az_prev[AZ_NIDS][260];
const int AZ_IDS[AZ_NIDS] = { 413, 419, 869, 892, 893, 894, 437, 441, 873, 25056, 897, 899, 874, 878, 880, 902, 25367, 415, 904, 420, 430, 25117,
                         1807, 416, 1811, 926, 920, 931, 922, 0 };   /* 926 / 920 / 931 / 922: the surface dialogs (control points, degree, boundaries, boundary points) */   /* 1807 / 416 / 1811: the solid dialog 6494 (description, # of Operations, body type) */
/* surfaces with a ported dialog: NURBS (6471) and trimmed on a NURBS base (6528); the other kinds' dialogs (6472-6476, 6524-6536) are
   NOT PORTED yet - their entities are left out of the list as before */
static const Surf *az_surf(int e) { return e >= 0 && e < F.nents && F.ents[e].type == E_SURF && F.ents[e].srf >= 0 && F.ents[e].srf < F.nsurfs ? &F.surfs[F.ents[e].srf] : NULL; }
static int az_surf_ok(int e) {
    const Surf *s = az_surf(e); if (!s) return 0;
    if (s->kind == 0) return 1;
    const Surf *b = s->kind == 10 ? mcam_surf_base(&F, s) : NULL;
    return b && b->kind == 0;
}
static int az_ok(int e) { return e >= 0 && e < F.nents && (F.ents[e].type == E_POINT || F.ents[e].type == E_LINE || F.ents[e].type == E_ARC || az_surf_ok(e)); }
int az_slot(int id) { for (int k = 0; k < AZ_NIDS; k++) if (AZ_IDS[k] && AZ_IDS[k] == id) return k; return -1; }
static void az_num(int id, double v) { int k = az_slot(id); if (k >= 0) make_rstr(v, az_txt[k], sizeof az_txt[k], cfg_places >= 0 ? cfg_places : 4); }
static double az_val(int id) { int k = az_slot(id); return k >= 0 ? atof(az_txt[k]) : 0; }
static int az_chg(int id) { int k = az_slot(id); return k >= 0 && strcmp(az_txt[k], az_prev[k]); }
/* CAnalyzeSolid OnInitDialog (FUN_180086f30): 1807 = sld_get_description ("%s"), 416 = sld_get_num_operations ("%d"), 1811 =
   mccadutilres 6317 "Closed Solid Body" / 6318 "Open Sheet Body" by is_solid_a_sheet (PK_BODY_ask_type != solid: here the XT's
   BODY.body_type != 1, occt_xt_body_type), Level / Colour from the entity (the base dialog's fields) */
static void az_sfill(void) {
    const Solid *so = &F.solids[az_sol];
    snprintf(az_txt[az_slot(1807)], sizeof az_txt[0], "%s", so->desc);
    snprintf(az_txt[az_slot(416)], sizeof az_txt[0], "%d", so->hist_ok ? so->nops : 0);
    int bt = so->brep ? occt_brep_body_type(so->brep, so->brep_n) : occt_xt_body_type(so->xt, so->xt_size);
    snprintf(az_txt[az_slot(1811)], sizeof az_txt[0], "%s", bt > 0 && bt != 1 ? "Open Sheet Body" : "Closed Solid Body");
    snprintf(az_txt[az_slot(430)], sizeof az_txt[0], "%d", so->level); snprintf(az_txt[az_slot(25117)], sizeof az_txt[0], "%d", so->color);
}
void az_fill(void) {                                        /* entity -> dialog (slot 115) */
    memset(az_txt, 0, sizeof az_txt);
    if (az_sol >= 0) { az_sfill(); memcpy(az_prev, az_txt, sizeof az_txt); return; }
    if (az_ent < 0) return;
    const Ent *e = &F.ents[az_ent];
    if (e->type == E_SURF) {   /* slot 115: FUN_18008df40 (NURBS) / FUN_180090990 + FUN_1800907e0 (trimmed NURBS): "%u%s%u" with mccadutilres 6169 " by " */
        const Surf *s = az_surf(az_ent), *n = s->kind == 10 ? mcam_surf_base(&F, s) : s;
        if (n && n->n >= 0x118) { int32_t cu, cv; memcpy(&cu, n->rec + 0x108, 4); memcpy(&cv, n->rec + 0x10c, 4);
            snprintf(az_txt[az_slot(926)], sizeof az_txt[0], "%u by %u", (unsigned)cu, (unsigned)cv);
            snprintf(az_txt[az_slot(920)], sizeof az_txt[0], "%u by %u", n->rec[0x111], n->rec[0x112]); }
        if (s->kind == 10 && s->n >= 0x138) { int32_t nl, m; memcpy(&nl, s->rec + 0x128, 4); memcpy(&m, s->rec + 0x10c, 4);
            snprintf(az_txt[az_slot(931)], sizeof az_txt[0], "%d", nl); snprintf(az_txt[az_slot(922)], sizeof az_txt[0], "%d", m); }
    }
    else if (e->type == E_POINT) { az_num(413, e->a.x); az_num(419, e->a.y); az_num(869, e->a.z); }
    else if (e->type == E_LINE) {
        az_num(892, e->a.x); az_num(893, e->a.y); az_num(894, e->a.z); az_num(437, e->b.x); az_num(441, e->b.y); az_num(873, e->b.z);
        double dx = e->b.x - e->a.x, dy = e->b.y - e->a.y, dz = e->b.z - e->a.z, l2 = dx * dx + dy * dy;
        double ang = l2 < 1e-12 ? 0 : atan2(dy, dx) * 180 / M_PI; if (ang < 0) ang += 360;
        az_num(25056, ang); az_num(897, sqrt(l2 + dz * dz)); az_num(899, sqrt(l2));
        az_num(874, fabs(dx)); az_num(878, fabs(dy)); az_num(880, fabs(dz));
    } else if (e->type == E_ARC) {
        snprintf(az_txt[az_slot(902)], sizeof az_txt[0], "%d", e->vw ? e->vw : 1);   /* the arc's view (+0xad60 -> +0x1ed18, analyze/arc.c FUN_1800737b0);
                                                               centre = view_to_world (world), angles = the stored view angles */
        az_num(437, e->c.x); az_num(441, e->c.y); az_num(873, e->c.z);
        az_num(25367, e->r); az_num(415, 2 * e->r); az_num(25056, e->sw * 180 / M_PI); az_num(897, fabs(e->sw) * e->r);
        az_num(904, e->a0 * 180 / M_PI); az_num(420, (e->a0 + e->sw) * 180 / M_PI);
    }
    snprintf(az_txt[az_slot(430)], sizeof az_txt[0], "%d", e->level); snprintf(az_txt[az_slot(25117)], sizeof az_txt[0], "%d", e->color);
    memcpy(az_prev, az_txt, sizeof az_txt);
}
/* a copy of entity e (a view arc keeps its view: the world centre back to view coordinates, analyze/arc.c l.549 world_to_view) */
static int az_new(const Ent *e) {
    if (e->type == E_ARC && e->vw) { const double *m = e->vm;
        V3 cv = { m[0] * e->c.x + m[1] * e->c.y + m[2] * e->c.z, m[3] * e->c.x + m[4] * e->c.y + m[5] * e->c.z, m[6] * e->c.x + m[7] * e->c.y + m[8] * e->c.z };
        return mcam_new_arc_view(&F, e->vw, m, cv, e->r, e->a0, e->sw, e->level, e->color); }
    return mcam_new_ent(&F, e->type, e->a, e->b, e->c, e->r, e->a0, e->sw, e->level, e->color);
}
static int az_rewrite(Ent ne) {                             /* rewrite the current entity (mod transaction) */
    mod_del(az_ent);
    int k = az_new(&ne);
    if (k < 0) return 0;
    F.ents[k].pt_style = ne.pt_style;
    az_ent = az_list[az_i] = k; az_changed = 1; doc_dirty = 1; scene_reupload(); overlay_dirty = 1;
    return 1;
}
void az_apply(void) {                                       /* dialog -> entity (slot 116) + entity -> dialog */
    if (az_sol >= 0) {   /* CAnalyzeSolid slot 117 (FUN_180086eb0): sld_set_description with the edit's text (at most 0x100 bytes), the
                            Solids manager row re-added (HistDlg_removeSolid + mc_readd_solid_to_dialog: here the tree reads the
                            description); level / colour by the base dialog */
        Solid *so = &F.solids[az_sol]; int ch = 0;
        if (strcmp(so->desc, az_txt[az_slot(1807)])) { snprintf(so->desc, sizeof so->desc, "%s", az_txt[az_slot(1807)]); ch = 1; }
        int lv = atoi(az_txt[az_slot(430)]), co = atoi(az_txt[az_slot(25117)]);
        if (lv >= 1 && lv != so->level) { so->level = lv; ch = 1; }
        if (co >= 0 && co <= 255 && co != so->color) { so->color = co; ch = 1; }
        if (ch) { so->edited = 1; az_changed = 1; doc_dirty = 1; }
        az_fill(); return;
    }
    if (az_ent < 0) return;
    if (F.ents[az_ent].type == E_SURF) {   /* the second dialog's level / colour (FUN_180083000 / FUN_180086cb0): the record rewritten in place */
        Ent *e = &F.ents[az_ent]; Surf *s = &F.surfs[e->srf];
        int lv = atoi(az_txt[az_slot(430)]), co = atoi(az_txt[az_slot(25117)]);
        if ((lv >= 1 && lv != e->level) || (co >= 0 && co <= 255 && co != e->color)) {
            srf_tx_save(e->srf);
            if (lv >= 1) { e->level = s->level = lv; memcpy(s->rec + 8, &lv, 4); mcam_level_store(&F, lv); }
            if (co >= 0 && co <= 255) { e->color = s->color = co; s->rec[6] = (uint8_t)co; }
            s->edited = 1; az_changed = 1; doc_dirty = 1; scene_surfs_update(0); overlay_dirty = 1;
        }
        az_fill(); return;
    }
    Ent ne = F.ents[az_ent]; int geo = 0;
    double tol = CAM_SYS_TOL;                          /* tol_exref (decomp/analyze/ln.c l.462 / 570 / 675 / 734, arc.c l.121 / 487 / 635; was the chaining tolerance) */
    if (ne.type == E_POINT) {
        if (az_chg(413) || az_chg(419) || az_chg(869)) { ne.a.x = az_val(413); ne.a.y = az_val(419); ne.a.z = az_val(869); ne.b = ne.a; geo = 1; }
    } else if (ne.type == E_LINE) {
        double dx = ne.b.x - ne.a.x, dy = ne.b.y - ne.a.y, dz = ne.b.z - ne.a.z, L = sqrt(dx * dx + dy * dy + dz * dz), L2 = hypot(dx, dy);
        if (az_chg(897)) {
            double nl = az_val(897);
            if (nl > tol && L > 0) { ne.b.x = ne.a.x + dx * nl / L; ne.b.y = ne.a.y + dy * nl / L; ne.b.z = ne.a.z + dz * nl / L; geo = 1; }
            else cui_message("apprenticeCAM", "Warning - zero length line");             /* mccoreres 5011 */
        } else if (az_chg(25056)) {
            double a = az_val(25056) * M_PI / 180;
            ne.b.x = ne.a.x + L2 * cos(a); ne.b.y = ne.a.y + L2 * sin(a); ne.b.z = ne.a.z; geo = 1;
        } else if (az_chg(892) || az_chg(893) || az_chg(894) || az_chg(437) || az_chg(441) || az_chg(873)) {
            ne.a.x = az_val(892); ne.a.y = az_val(893); ne.a.z = az_val(894); ne.b.x = az_val(437); ne.b.y = az_val(441); ne.b.z = az_val(873); geo = 1;
        }
    } else if (ne.type == E_ARC) {
        if (az_chg(415)) { double d = az_val(415); if (d / 2 >= tol) { ne.r = d / 2; geo = 1; } }
        else if (az_chg(25367)) { double r = az_val(25367); if (r >= tol) { ne.r = r; geo = 1; } }
        else if (az_chg(25056)) { ne.sw = az_val(25056) * M_PI / 180; geo = 1; }
        else if (az_chg(904)) { double na = az_val(904) * M_PI / 180; ne.sw -= na - ne.a0; ne.a0 = na; geo = 1; }
        else if (az_chg(420)) { ne.sw = az_val(420) * M_PI / 180 - ne.a0; geo = 1; }
        else if (az_chg(897)) { double l = az_val(897); if (fabs(l) >= tol && ne.r > 0) { ne.sw = (ne.sw < 0 ? -1 : 1) * fabs(l) / ne.r; geo = 1; } }
        else if (az_chg(437) || az_chg(441) || az_chg(873)) { ne.c.x = az_val(437); ne.c.y = az_val(441); ne.c.z = az_val(873); geo = 1; }
        if (fabs(ne.sw) > 2 * M_PI) ne.sw = ne.sw < 0 ? -2 * M_PI : 2 * M_PI;
        ne.a = (V3){ ne.c.x + ne.r * cos(ne.a0), ne.c.y + ne.r * sin(ne.a0), ne.c.z };
        ne.b = (V3){ ne.c.x + ne.r * cos(ne.a0 + ne.sw), ne.c.y + ne.r * sin(ne.a0 + ne.sw), ne.c.z };
    }
    int lv = atoi(az_txt[az_slot(430)]), co = atoi(az_txt[az_slot(25117)]);
    if (lv >= 1 && lv != ne.level) { ne.level = lv; geo = 1; }
    if (co >= 0 && co <= 255 && co != ne.color) { ne.color = co; geo = 1; }
    if (geo) az_rewrite(ne);
    az_fill();
}
static void az_begin_ent(void) {                            /* open the dialog for az_list[az_i] */
    az_ent = az_list[az_i]; az_orig = F.ents[az_ent]; az_orig_ent = az_ent; az_changed = 0; az_col0 = az_orig.color;
    if (F.ents[az_ent].type != E_SURF) mod_begin();      /* a surface: its record edits go through the surface transaction (cui_surf.c) */
    az_fill(); az_open_new = 1;
}
void az_close(int commit) {                                 /* OK / Esc (1), Cancel / X (0) */
    if (az_i < 0) return;
    if (az_sol >= 0) {   /* Cancel: the values at open back (the solid dialog changes no geometry; no undo step here - NOT PORTED) */
        if (!commit) { Solid *so = &F.solids[az_sol]; snprintf(so->desc, sizeof so->desc, "%s", az_sdesc0); so->level = az_slv0; so->color = az_sco0; so->edited = az_sedit0; }
        az_sol = -1; az_i = -1; az_ent = -1; az_pick = 0; cam_msg[0] = 0; return;
    }
    if (az_ent >= 0 && F.ents[az_ent].type == E_SURF) {   /* OK: one undo step (rewrite_ent's put_special_undo); Cancel: the record back */
        if (commit) srf_tx_commit(); else srf_tx_cancel();
        scene_surfs_update(0); overlay_dirty = 1;
        if (mod_from >= 0 && commit) mod_end();
    }
    else if (commit) mod_end();
    else {                                                  /* restore: drop the rewrites, switch the originals back on */
        for (int i = mod_from; i >= 0 && i < F.nents; i++) ent_off(i);
        for (int k = 0; k < mod_rec.nd; k++) ent_on(mod_rec.dents[k], mod_rec.dtypes[k]);
        urec_free(&mod_rec); mod_from = -1; memset(&mod_rec, 0, sizeof mod_rec);
        scene_reupload(); overlay_dirty = 1;
    }
    az_i = -1; az_ent = -1; az_pick = 0; cam_msg[0] = 0;
}
void az_step(int dir) {                                     /* Previous / Next: commit, then the next list entry with wrap */
    if (az_ent >= 0 && F.ents[az_ent].type == E_SURF) srf_tx_commit(); else mod_end();
    az_i = (az_i + dir + az_n) % az_n;
    az_begin_ent();
}
void az_start(int picked) {                                 /* AnalyzeStart: the pre-selection, else the picked entity */
    az_n = 0;
    for (int i = 0; ent_sel && i < ent_sel_n && az_n < 512; i++) if (ent_sel[i] && F.ents[i].type && F.ents[i].type != E_NOTE && F.ents[i].type != E_MESH && (F.ents[i].type != E_SURF || az_surf_ok(i))) az_list[az_n++] = i;   /* notes: Analyze's
                                                                      drafting dialog NOT PORTED */   /* surfaces: the ported dialogs only (az_surf_ok) */
    if (!az_n && picked >= 0) az_list[az_n++] = picked;
    if (!az_n) return;
    az_i = 0; az_begin_ent();
}
/* AnalyzeStart (F4, mccadutil 18008d050): an open dialog commits first; with nothing selected the entity pick (6333) */
void analyze_start(void) {
    if (az_i >= 0) az_close(1);
    az_start(-1);
    if (az_i < 0 && sld_sel_count() == 1) {             /* the pre-selection is one part solid: its dialog (as its double-click). A solid
                                                          among other selected entities is not in the list (the mixed walk NOT DECODED) */
        for (int i = 0; i < F.nsolids; i++) if (sld_sel_is(i)) { az_start_solid(i); return; }
    }
    if (az_i < 0) { analyze_pick = 1; snprintf(cam_msg, sizeof cam_msg, "Select entities to analyze"); }   /* mccadutilres 6333 */
}
/* the analyze pick: a point / line / arc under the cursor starts the dialog (1 = taken) */
int analyze_pick_ent(int e) {
    if (az_ok(e)) { analyze_pick = 0; cam_msg[0] = 0; az_start(e); }
    return 1;
}
/* the dialog for part solid i (one entity: Previous / Next / Propagate hidden, slot 118) */
void az_start_solid(int i) {
    if (i < 0 || i >= F.nsolids) return;
    if (az_i >= 0) az_close(1);
    const Solid *so = &F.solids[i];
    az_sol = i; az_ent = -1; az_n = 1; az_i = 0; az_changed = 0; az_pick = 0;
    snprintf(az_sdesc0, sizeof az_sdesc0, "%s", so->desc); az_slv0 = so->level; az_sco0 = so->color; az_sedit0 = so->edited;
    az_fill(); az_open_new = 1;
}
/* a double-click in the graphics window with no command running (user report 2026-10-24: Mastercam opens the properties dialog;
   mastercam.dll UiView OnLButtonDblClk FUN_1801ca210 -> message 0x802f -> mccore FUN_1804e3a10 / FUN_1804e3af0, decomp/dblclk:
   how the idle state turns it into Analyze is NOT DECODED): the entity under the cursor (an open dialog commits first, as
   AnalyzeStart), else the part solid under it */
void analyze_dblclick(int e, int sol) {
    if (az_ok(e)) { if (az_i >= 0) az_close(1); az_start(e); }
    else if (sol >= 0) az_start_solid(sol);
}
void az_propagate(void) {                                   /* level (and a changed colour) to the other listed entities */
    if (az_ent < 0) return;
    int lv = F.ents[az_ent].level, co = F.ents[az_ent].color, colchg = co != az_col0;
    for (int k = 0; k < az_n; k++) {
        if (k == az_i) continue;
        int j = az_list[k]; if (j < 0 || j >= F.nents || !F.ents[j].type) continue;
        if (F.ents[j].type == E_SURF) {                 /* a listed surface: its record's level / colour */
            Surf *s = &F.surfs[F.ents[j].srf]; int ch = 0; srf_tx_save(F.ents[j].srf);
            if (F.ents[j].level != lv) { F.ents[j].level = s->level = lv; memcpy(s->rec + 8, &lv, 4); mcam_level_store(&F, lv); ch = 1; }
            if (colchg && F.ents[j].color != co) { F.ents[j].color = s->color = co; s->rec[6] = (uint8_t)co; ch = 1; }
            if (ch) { s->edited = 1; az_changed = 1; }
            continue;
        }
        Ent e = F.ents[j]; int ch = 0;
        if (e.level != lv) { e.level = lv; ch = 1; }
        if (colchg && e.color != co) { e.color = co; ch = 1; }
        if (!ch) continue;
        mod_del(j); int n = az_new(&e);
        if (n >= 0) { az_list[k] = n; F.ents[n].pt_style = e.pt_style; }
        az_changed = 1;
    }
    az_col0 = co; doc_dirty = 1; scene_reupload(); overlay_dirty = 1;
}
void az_autocursor_pick(double x, double y, double z) {     /* AutoCursor: the new point position (5342) */
    if (az_ent < 0 || !az_pick) return;
    Ent ne = F.ents[az_ent]; V3 p = { x, y, z };
    if (ne.type == E_POINT) { ne.a = ne.b = p; }
    else if (ne.type == E_LINE) {
        Ent t = ne; if (az_pick == 901) t.a = p; else t.b = p;
        if (hypot(hypot(t.b.x - t.a.x, t.b.y - t.a.y), t.b.z - t.a.z) <= CAM_SYS_TOL) {   /* ln.c l.570: L <= tol_exref -> 5011 */ cui_message("apprenticeCAM", "Warning - zero length line"); az_pick = 0; cam_msg[0] = 0; return; }
        ne = t;
    } else if (ne.type == E_ARC) {
        ne.c = p;
        ne.a = (V3){ p.x + ne.r * cos(ne.a0), p.y + ne.r * sin(ne.a0), p.z };
        ne.b = (V3){ p.x + ne.r * cos(ne.a0 + ne.sw), p.y + ne.r * sin(ne.a0 + ne.sw), p.z };
    }
    az_rewrite(ne); az_fill(); az_pick = 0; cam_msg[0] = 0;
}
static void az_level_done(int l) { if (az_ent < 0 && az_sol < 0) return; snprintf(az_txt[az_slot(430)], sizeof az_txt[0], "%d", l); az_apply(); }
static void az_color_done(int c) { if (az_ent < 0 && az_sol < 0) return; snprintf(az_txt[az_slot(25117)], sizeof az_txt[0], "%d", c); az_apply(); }
/* the dialog of the current entity: template, height (hdlg), title */
const BsCtl *az_template(int *n, int *hdlg, const char **title) {
    if (az_sol >= 0) {   /* the title: opmanres 27601 + mc_op_getType(solid, 0) (FUN_180086f30), e.g. "Extrude"; a solid without a readable
                            history: opmanres 27600 "Solid" (ours) */
        const Solid *so = &F.solids[az_sol];
        int k = 0; while (AZ_6494[k].text) k++;
        if (n) *n = k;
        if (hdlg) *hdlg = 154;
        if (title) *title = so->hist_ok && so->nops > 0 && so->ops[0].type >= 0 && so->ops[0].type < 32 ? SLD_OPNAME[so->ops[0].type] : "Solid";
        return AZ_6494;
    }
    const Ent *e = &F.ents[az_ent];
    if (e->type == E_SURF) {
        int tr = az_surf(az_ent)->kind == 10; const BsCtl *c = tr ? AZ_6528 : AZ_6471;
        int k = 0; while (c[k].text) k++;
        if (n) *n = k;
        if (hdlg) *hdlg = tr ? 233 : 193;
        if (title) *title = tr ? "Trimmed NURBS Surface" : "NURBS Surface";
        return c;
    }
    const BsCtl *c = e->type == E_POINT ? AZ_1073 : e->type == E_LINE ? AZ_1080 : AZ_6470;
    int k = 0; while (c[k].text) k++;
    if (n) *n = k;
    if (hdlg) *hdlg = e->type == E_POINT ? 193 : e->type == E_LINE ? 310 : 329;
    if (title) *title = e->type == E_POINT ? "Point Properties" : e->type == E_LINE ? "Line Properties" : "Arc Properties";
    return c;
}
/* slot 118 FUN_180082140 (decomp/analyze/first.c l.691-738): one entity -> Previous / Next / Propagate / the separators hidden;
   OK / Cancel / Help (1 / 2 / 9) moved up by 0x20 px (FUN_180082390 -> FUN_1800822d0, decomp/analyze/base.c: SetWindowPos at the
   client top - 0x20), then every child window (GW_CHILD / GW_HWNDNEXT, the three buttons again) by 0x20 px, and the window 0x44 px
   lower (textfit 2026-11-17: pixels, not dialog units - the 32 du of before put the first row 2 px above the window body) */
int az_single(void) { return az_n == 1; }
int az_hidden(int id) { return az_single() && (id == 875 || id == 877 || id == 891 || id == 2961 || id == 2962); }
int az_ctl_dy_px(const BsCtl *k) { return !az_single() ? 0 : (k->id == 1 || k->id == 2 || k->id == 9) ? -64 : -32; }
int az_dlg_dh_px(void) { return az_single() ? -0x44 : 0; }
int az_readonly(int id) { return id == 926 || id == 920 || id == 931 || id == 922 || id == 416 || id == 1811 || id == 899 || id == 874 || id == 878 || id == 880 || id == 902; }
/* a button's bitmap (mccoreres / mccadutilres BMP id; 0 = a text button) */
int az_button_bmp(int id, const char **mod) {
    const Ent *e = az_ent >= 0 ? &F.ents[az_ent] : NULL;
    int bid = id == 875 ? 1147 : id == 877 ? 1139 : id == 25261 ? 1153 : id == 25114 ? 1154 : id == 891 ? 1156
            : id == 901 ? 1186 : id == 870 && e && e->type == E_LINE ? 1187 : id == 867 || id == 870 ? 5537 : id == 843 ? 1123 : 0;
    if (mod) *mod = bid == 1156 ? "mccadutilres" : "mccoreres";
    if (id == 844 && e && e->type == E_SURF) {         /* FUN_18008e3c0: 0x45b with the normal reversed, else 0x1674 (the current resource
                                                          module = mccadutil's) */
        if (mod) *mod = "mccadutilres";
        return (az_surf(az_ent)->rec[0x6e] & 4) ? 0x45b : 0x1674;
    }
    return bid;
}
/* a bitmap static's resource ("#1172" ... with the runtime replacements, analyze.md) */
int az_static_bmp(const char *text, const char **mod) {
    int b = atoi(text + 1); *mod = "mccoreres";
    if (b == 1172) b = 25201; else if (b == 1178) b = 25202; else if (b == 1119) b = 25200;
    if (b == 1081 || b == 1189 || b == 1213) *mod = "mccadutilres";
    return b;
}
int az_live(int id) {
    if (az_sol >= 0) return id == 1 || id == 2 || id == 25114 || id == 25261;
    const Ent *e = &F.ents[az_ent];
    return id == 1 || id == 2 || id == 875 || id == 877 || id == 891 || id == 867 || id == 901 || (id == 870 && e->type == E_LINE) || id == 25114 || id == 25261 || (id == 844 && e->type == E_SURF);
}
void az_act(int act) {
    if (act == 1) az_close(1);
    else if (act == 2) az_close(0);
    else if (act == 875 || act == 877) { az_apply(); az_step(act == 875 ? 1 : -1); }
    else if (act == 891) { az_apply(); az_propagate(); }
    else if (act == 844 && az_ent >= 0 && F.ents[az_ent].type == E_SURF) {   /* Flip Normals (slot 119 FUN_18008e3c0): the surf header's +6 bit 4
                                                                                toggled in the record, rewrite_ent at once (no sel-bit swap, unlike
                                                                                change_normals); the button's bitmap follows (0x45b / 0x1674) */
        Surf *s = &F.surfs[F.ents[az_ent].srf]; srf_tx_save(F.ents[az_ent].srf); s->rec[0x6e] ^= 4; s->edited = 1;
        az_changed = 1; doc_dirty = 1; scene_surfs_update(0); overlay_dirty = 1; az_fill();
    }
    else if (act == 25261) level_dialog(atoi(az_txt[az_slot(430)]), az_level_done);   /* FUN_180082500: sel_level_dialog */
    else if (act == 25114) color_dialog(atoi(az_txt[az_slot(25117)]), 0, az_color_done);   /* FUN_180082430: sel_color_dialog */
    else if (act == 867 || act == 901 || act == 870) { az_pick = act; snprintf(cam_msg, sizeof cam_msg, "Select New Point Position"); }   /* mccadutilres 5342 */
}
void az_dump(void) {
    if (az_i >= 0 && az_sol >= 0) { printf("    analyze solid %d:", az_sol); for (int q = 0; q < AZ_NIDS; q++) if (az_txt[q][0]) printf(" %d=%s", AZ_IDS[q], az_txt[q]); printf("\n"); }
    if (az_i >= 0 && az_ent >= 0) { printf("    analyze %d/%d ent %d%s:", az_i + 1, az_n, az_ent, az_pick ? " (picking)" : "");
        for (int q = 0; q < AZ_NIDS; q++) if (az_txt[q][0]) printf(" %d=%s", AZ_IDS[q], az_txt[q]); printf("\n"); }
    if (analyze_pick) printf("    (analyze pick)\n");
    if (cdlg_open) printf("    colors %d '%s'%s%s\n", cdlg_cur, cdlg_txt, cdlg_select_ok ? " select" : "", cdlg_picking ? " (picking)" : "");
    if (ldlg_open) { int rows[64]; int nr = ldlg_rows(rows, 64); printf("    levels %d '%s' radio %d range %d..%d%s rows", ldlg_cur, ldlg_txt, ldlg_radio, ldlg_lo, ldlg_hi, ldlg_picking ? " (picking)" : "");
        for (int i = 0; i < nr; i++) printf(" %d", rows[i]); printf("\n"); }
}
/* script steps (as mcamui run_script): az:ID=VALUE, azlevel, azcolor, azok, azcancel; ours: azact:ID (a dialog button),
   azpickat:X,Y,Z (the AutoCursor pick's point), cdlg:I (a colour button), cdlgdbl:I, cdlgtxt:N (typed + commit), cdlgok,
   cdlgx (the X), ldlgclick:L, ldlgtxt:N, ldlgradio:K, ldlgrange:LO,HI, ldlgok, ldlgx */
int cui_az_script(const char *st, const char *arg) {
#define IS(x) (!strcmp(st, x))
    if (IS("az") && arg) {                                     /* az:ID=VALUE - type into an Analyze field and apply */
        int id = atoi(arg); const char *v = strchr(arg, '='); int k = az_slot(id);
        if (v && k >= 0) { snprintf(az_txt[k], sizeof az_txt[0], "%s", v + 1); az_apply(); printf(az_sol >= 0 ? "    analyze solid %d:" : "    analyze ent %d:", az_sol >= 0 ? az_sol : az_ent);
            for (int q = 0; q < AZ_NIDS; q++) if (az_txt[q][0]) printf(" %d=%s", AZ_IDS[q], az_txt[q]); printf("\n"); }
        return 1; }
    if (IS("azlevel")) { level_dialog(atoi(az_txt[az_slot(430)]), az_level_done); return 1; }
    if (IS("azcolor")) { color_dialog(atoi(az_txt[az_slot(25117)]), 0, az_color_done); return 1; }
    if (IS("azsolid") && arg) { az_start_solid(atoi(arg)); az_dump(); return 1; }   /* ours: the solid dialog for F.solids[N] (as a double-click on it) */
    if (IS("azok")) { az_close(1); return 1; }
    if (IS("azcancel")) { az_close(0); return 1; }
    if (IS("azact") && arg) { if (az_i >= 0 && az_live(atoi(arg))) az_act(atoi(arg)); return 1; }
    if (IS("azpickat") && arg) { double x = 0, y = 0, z = 0; sscanf(arg, "%lf,%lf,%lf", &x, &y, &z); az_autocursor_pick(x, y, z); return 1; }
    if (IS("cdlg") && arg) { if (cdlg_open) cdlg_click(atoi(arg)); return 1; }
    if (IS("cdlgdbl") && arg) { if (cdlg_open) { cdlg_click(atoi(arg)); cdlg_close(1); } return 1; }
    if (IS("cdlgtxt") && arg) { if (cdlg_open) { snprintf(cdlg_txt, sizeof cdlg_txt, "%s", arg); cdlg_commit(); } return 1; }
    if (IS("cdlgok")) { if (cdlg_open) cdlg_close(1); return 1; }
    if (IS("cdlgx")) { if (cdlg_open) cdlg_close(0); return 1; }
    if (IS("ldlgclick") && arg) { if (ldlg_open) ldlg_click(atoi(arg)); return 1; }
    if (IS("ldlgtxt") && arg) { if (ldlg_open) { snprintf(ldlg_txt, sizeof ldlg_txt, "%s", arg); ldlg_commit(); } return 1; }
    if (IS("ldlgradio") && arg) { if (ldlg_open) ldlg_radio = atoi(arg); return 1; }
    if (IS("ldlgrange") && arg) { if (ldlg_open && ldlg_radio == 3) { int lo = 0, hi = 0; sscanf(arg, "%d,%d", &lo, &hi);
        snprintf(ldlg_lo_t, 16, "%d", lo); ldlg_range_commit(0); snprintf(ldlg_hi_t, 16, "%d", hi); ldlg_range_commit(1); } return 1; }
    if (IS("ldlgok")) { if (ldlg_open) ldlg_close(1); return 1; }
    if (IS("ldlgx")) { if (ldlg_open) ldlg_close(0); return 1; }
    return 0;
#undef IS
}
