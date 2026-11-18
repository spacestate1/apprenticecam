/* cui_lface.c - the NEW Lathe Face command and its two-page dialog (shared by the front ends; Qt: viewer-qt/src/lfacedlg.cpp).
 *
 * Mastercam: Toolpaths manager menu 780 Lathe toolpaths > Face (3680, FT "lathe_face") and the Turning ribbon's Face (cmd_labels
 * lathe_face) -> mclathe.dll lathe_face 0x1801a3290 -> init_operation(0x42) -> edit_lathe_operation 0x1801c7480: the CLTpSheet with
 * the tool page (template 3911, CLToolProp 0x1801a62d0, tab text string 2093 "Toolpath parameters") and the Face page (template
 * 5361 "Face Parameters", CLFaceProp 0x180041170); the sheet opens on the tool page (op +0xf4 != 1). OK -> UpdateListAndDB +
 * bnci_section (the toolpath); Cancel -> DeleteListAndDB. Spec: ../../formats/lathe.md 7.4 / 8, control -> op field map
 * ../../formats/face_dialog_fields.tsv. The record is built by viewer/lathe_newop.c; the controls edit its bytes here.
 * Not decoded here: the sub-dialogs (Lead In/Out 2425, Filter, Corner, Coolant, Misc values, Tool display, Stock update, Ref points,
 * Canned text, Library origin, Tool angle, Axis combination, Define home, Select library tool, Tool filter), the point picks
 * (Select Points / Finish Z...), the compensation type and home position combo texts: their controls are greyed / refuse with a
 * NOT DECODED message. */
#include "core_ui.h"
#include "../lathe_newop.h"
#include "../lathe_tool.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int show_lface, lface_page;                 /* the sheet is up; its page: 0 the tool page, 1 the Face page */
static uint8_t *lf_rec, *lf_base;            /* the record the dialog edits; the one the tool pick made (the patches = their differences) */
static int lf_ltl[256], lf_nltl, lf_row = -1;
static int lf_face_seen;                     /* the Face page was opened (CLFaceProp exists: its OnOK runs at the sheet's OK) */
static int lf_tplane; static LfnPlanes lf_act;
static double lf_zlo, lf_zhi; static int lf_stock;
static int lf_z_edited;                      /* page +0x847c */
static LfnSpeedSt lf_st;                     /* the tool page's feed / speed state (L-12: +0x34, the finish feed radio, the italic fields) */

static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static void wdd(uint8_t *p, double v) { memcpy(p, &v, 8); }
static int32_t r32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void w32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static int16_t r16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static void w16(uint8_t *p, int16_t v) { memcpy(p, &v, 2); }
#define OPB(o) (lf_rec + LFN_OP + (o))

/* ---- the controls: templates 3911 / 5361 (exports/mclatheres_dialogs.tsv) with their roles (DDX / handlers, lathe.md 7.4 / 8) */
const LfCtl LF_CTLS[] = {
    /* tool page 3911 (455 x 317 du) */
    { 0, 830, LFR_LIST, 0, 5, 3, 200, 235, "" },
    { 0, 357, LFR_STATIC, 0, 216, 6, 62, 8, "Tool number:" }, { 0, 182, LFR_I32, 0x2e0, 282, 3, 39, 14, "" },
    { 0, 361, LFR_STATIC, 0, 330, 6, 51, 8, "Offset number:" }, { 0, 188, LFR_I32, 0x30c, 385, 3, 39, 14, "" },
    { 0, 1753, LFR_STATIC, 0, 216, 22, 62, 8, "Station number:" }, { 0, 1417, LFR_I32, 0x53c, 282, 19, 39, 14, "" },
    { 0, 1786, LFR_NA, 0, 358, 20, 66, 14, "Tool An&gle..." },
    { 0, 25587, LFR_GROUP, 0, 211, 37, 233, 29, "Turret Index Position" },
    { 0, 25588, LFR_NA_RADIO, 0, 250, 50, 70, 10, "Left spindle" }, { 0, 25589, LFR_NA_RADIO, 0, 364, 50, 70, 10, "Right spindle" },
    { 0, 392, LFR_STATIC, 0, 219, 80, 67, 8, "Feed rate:" }, { 0, 224, LFR_FEED, 0x310, 289, 77, 40, 14, "" },
    { 0, 405, LFR_FEEDU, 0, 331, 80, 36, 10, "in/rev" }, { 0, 416, LFR_FEEDU, 1, 371, 80, 41, 10, "in/min" }, { 0, 419, LFR_FEEDU, 2, 414, 81, 40, 10, "micro-in" },
    { 0, 5190, LFR_CHECK, 0x139a, 219, 96, 67, 10, "Finish feed rate:" }, { 0, 5191, LFR_F64, 0x13a0, 289, 93, 40, 14, "" },
    { 0, 5192, LFR_FFEEDU, 0, 331, 96, 36, 10, "in/rev" }, { 0, 5193, LFR_FFEEDU, 1, 371, 96, 38, 10, "in/min" }, { 0, 5194, LFR_FFEEDU, 2, 414, 96, 40, 10, "micro-in" },
    { 0, 1597, LFR_STATIC, 0, 219, 112, 67, 8, "Spindle speed:" }, { 0, 231, LFR_RPM, 0x328, 289, 109, 40, 14, "" },
    { 0, 420, LFR_CSS, 0x32d, 331, 112, 36, 10, "CSS" }, { 0, 421, LFR_RPMR, 0x32d, 371, 112, 38, 10, "RPM" },
    { 0, 5195, LFR_CHECK, 0x13a9, 208, 129, 78, 10, "Finish spindle speed:" }, { 0, 232, LFR_I32, 0x13ac, 289, 126, 40, 14, "" },
    { 0, 5210, LFR_CSS, 0x13b0, 332, 129, 36, 10, "CSS" }, { 0, 423, LFR_RPMR, 0x13b0, 372, 129, 38, 10, "RPM" },
    { 0, 397, LFR_STATIC, 0, 219, 145, 67, 8, "Max. spindle speed:" }, { 0, 237, LFR_I32, 0x548, 289, 142, 40, 14, "" },
    { 0, 1735, LFR_NA, 0, 380, 142, 50, 14, "Coolant" },
    { 0, 1250, LFR_GROUP, 0, 211, 165, 213, 36, "Home Position" }, { 0, 1373, LFR_STATIC, 0, 217, 179, 78, 11, "X0.0 Z0.0" },
    { 0, 1372, LFR_NA_COMBO, 0x868, 299, 178, 67, 40, "" }, { 0, 432, LFR_NA, 0, 370, 178, 50, 14, "&Define" },
    { 0, 25196, LFR_CHECK, 0x289, 220, 206, 73, 10, "Force tool change" }, { 0, 25601, LFR_CHECK, 0x291, 332, 206, 45, 10, "To batch" },
    { 0, 425, LFR_STATIC, 0, 220, 226, 136, 8, "Comment:" }, { 0, 260, LFR_COMMENT, 6, 220, 237, 205, 30, "" },
    { 0, 2229, LFR_NA_CHECK, 0, 9, 242, 71, 10, "Show library tools" }, { 0, 4294901759u, LFR_STATIC, 0, 126, 243, 69, 8, "Right-click for options" },
    { 0, 1755, LFR_NA, 0, 9, 254, 66, 14, "Select library tool..." }, { 0, 276, LFR_NA_CHECK, 0, 118, 257, 10, 10, "" }, { 0, 454, LFR_NA, 0, 131, 254, 61, 14, "Tool &Filter..." },
    { 0, 5456, LFR_GROUP, 0, 6, 277, 192, 36, "Axis Combination / Spindle Origin" }, { 0, 1740, LFR_NA, 0, 10, 287, 22, 22, "" },
    { 0, 5455, LFR_STATIC, 0, 36, 289, 156, 8, "Axis combination name" }, { 0, 5459, LFR_STATIC, 0, 36, 300, 156, 8, "Spindle origin:" },
    { 0, 451, LFR_NA, 0, 217, 281, 61, 14, "Misc. &Values..." }, { 0, 1420, LFR_CHECK, 0xb3c, 290, 284, 10, 10, "" }, { 0, 1416, LFR_NA, 0, 302, 281, 61, 14, "&Stock Update..." },
    { 0, 268, LFR_NA_CHECK, 0, 373, 284, 10, 10, "" }, { 0, 448, LFR_NA, 0, 385, 281, 61, 14, "Ref. &Points..." },
    { 0, 274, LFR_CHECK, 0x930, 205, 302, 10, 10, "" }, { 0, 453, LFR_NA, 0, 217, 299, 61, 14, "&Tool Display..." },
    { 0, 25264, LFR_NA, 0, 302, 299, 61, 14, "&Library Origin..." }, { 0, 1211, LFR_NA, 0, 374, 299, 72, 14, "&Canned Text..." },
    /* Face page 5361 (450 x 274 du) */
    { 1, 1339, LFR_SRCRADIO, 0, 10, 62, 10, 10, "" }, { 1, 1340, LFR_SRCRADIO, 1, 10, 78, 79, 10, "Use stock" },
    { 1, 1338, LFR_BTN, 0, 22, 58, 62, 14, "&Select Points..." }, { 1, 1343, LFR_BTN, 0, 22, 90, 62, 14, "Finish &Z..." },
    { 1, 1342, LFR_FINZ, 0x13b8, 21, 107, 40, 14, "" },
    { 1, 798, LFR_STATIC, 0, 245, 69, 51, 8, "Entry amount:" }, { 1, 795, LFR_F64, 0x11f0, 245, 79, 40, 14, "" },
    { 1, 809, LFR_ROUGH, 0x11c0, 245, 101, 70, 10, "Rough stepover:" }, { 1, 787, LFR_F64, 0x11c8, 245, 113, 40, 14, "" },
    { 1, 811, LFR_FINISH, 0x11d0, 245, 134, 70, 10, "Finish stepover:" }, { 1, 796, LFR_F64, 0x11d8, 245, 146, 40, 14, "" },
    { 1, 792, LFR_STATIC, 0, 321, 135, 116, 8, "Maximum number of finish passes:" }, { 1, 793, LFR_I16, 0x11e0, 321, 146, 40, 14, "" },
    { 1, 807, LFR_STATIC, 0, 245, 165, 65, 8, "Overcut amount:" }, { 1, 800, LFR_F64, 0x1208, 245, 175, 40, 14, "" },
    { 1, 805, LFR_STATIC, 0, 245, 195, 65, 8, "Retract amount:" }, { 1, 802, LFR_F64, 0x11f8, 245, 205, 40, 14, "" },
    { 1, 929, LFR_CHECK, 0x1200, 291, 207, 75, 10, "Rapid retract" },
    { 1, 789, LFR_STATIC, 0, 245, 225, 62, 8, "Stock to leave:" }, { 1, 790, LFR_F64, 0x11e8, 245, 235, 40, 14, "" },
    { 1, 1018, LFR_AWAY, 0x1210, 245, 257, 95, 10, "Cut away from center line" },
    { 1, 206, LFR_GROUP, 0, 359, 3, 87, 121, "Tool Compensation" },
    { 1, 850, LFR_STATIC, 0, 366, 13, 50, 8, "Compensation" }, { 1, 886, LFR_STATIC, 0, 366, 21, 48, 8, "type:" },
    { 1, 852, LFR_NA_COMBO, 0x568, 366, 31, 75, 66, "" },
    { 1, 855, LFR_STATIC, 0, 366, 49, 51, 8, "Compensation" }, { 1, 799, LFR_STATIC, 0, 366, 57, 52, 8, "direction:" },
    { 1, 846, LFR_COMBO_DIR, 0x56a, 366, 68, 48, 42, "" },
    { 1, 4294901759u, LFR_STATIC, 0, 366, 86, 52, 8, "Roll cutter" }, { 1, 4294901759u, LFR_STATIC, 0, 366, 94, 52, 8, "around corners:" },
    { 1, 908, LFR_COMBO_ROLL, 0x56b, 366, 105, 48, 41, "" },
    { 1, 816, LFR_CHECK, 0x888, 359, 240, 10, 10, "" }, { 1, 820, LFR_NA, 0, 372, 238, 72, 14, "&Lead In/Out..." },
    { 1, 1413, LFR_CHECK, 0x2d2, 359, 257, 10, 10, "" }, { 1, 1414, LFR_NA, 0, 372, 255, 72, 14, "Filter..." },
    { 1, 5383, LFR_CHECK, 0x1358, 359, 224, 10, 10, "" }, { 1, 5368, LFR_NA, 0, 372, 221, 72, 14, "Corner..." },
    { -1, 0, 0, 0, 0, 0, 0, 0, NULL } };
static const LfCtl *ctl_by_id(int page, unsigned id) { for (const LfCtl *c = LF_CTLS; c->text; c++) if (c->page == page && c->id == id) return c; return NULL; }

/* ---- the tool list (CLtlPgCmn::InitToolList 0x1801ac400 / FillListCtrl 0x1801ab1a0; lathe.md 8.1): the part's lathe tools of group
   0 or the op's group, sorted by SortLItems 0x1801b0d20 case 0 (tool number * 10000 + offset, ascending); columns 721 Number
   "T%.2d%.2d", 722 Tool Name, 723 Insert Info (4701 "R%s"), 724 Turret (4704 Top / 4705 Bottom) */
static void fill_tools(void) {
    int idx[256], n = lfn_tools(&F, idx, 256);
    const struct McGrpLathe *g = lfn_lathe_group(&F, NULL, 0);
    lf_nltl = 0;
    for (int i = 0; i < n; i++) {
        LtInfo t; if (!lt_info(F.ltl[idx[i]].body, F.ltl[idx[i]].n, &t)) continue;
        if (t.group != 0 && g && t.group != g->id) continue;
        lf_ltl[lf_nltl++] = idx[i];
    }
    for (int a = 0; a < lf_nltl; a++) for (int b = a + 1; b < lf_nltl; b++) {
        LtInfo x, y; lt_info(F.ltl[lf_ltl[a]].body, F.ltl[lf_ltl[a]].n, &x); lt_info(F.ltl[lf_ltl[b]].body, F.ltl[lf_ltl[b]].n, &y);
        long kx = (long)x.tool_num * 10000 + x.offset, ky = (long)y.tool_num * 10000 + y.offset;
        if (ky < kx) { int t = lf_ltl[a]; lf_ltl[a] = lf_ltl[b]; lf_ltl[b] = t; }
    }
}
int lface_tool_rows(void) { return show_lface ? lf_nltl : 0; }
int lface_tool_row(void) { return lf_row; }
void lface_tool_cell(int row, int col, char *t, size_t n) {
    t[0] = 0; if (row < 0 || row >= lf_nltl) return;
    LtInfo x; if (!lt_info(F.ltl[lf_ltl[row]].body, F.ltl[lf_ltl[row]].n, &x)) return;
    switch (col) {
    case 0: snprintf(t, n, "T%.2d%.2d", x.tool_num, x.offset); break;
    case 1: { const char *s = x.name; while (*s == ' ') s++; snprintf(t, n, "%s", s); size_t L = strlen(t); while (L && t[L - 1] == ' ') t[--L] = 0; break; }
    case 2: { char v[40]; snprintf(v, sizeof v, "%g", mcam_is_metric(&F) ? x.nose_r * 25.4 : x.nose_r); if (x.type != 4) snprintf(t, n, "R%s", v); break; }   /* 4701; drill 4703 "%s Dia." NOT DECODED here */
    case 3: snprintf(t, n, "%s", x.turret == 1 || x.turret == 3 || x.turret == 4 ? "Top" : "Bottom"); break;   /* is_turret_top (mccore 0x1800344b0): 1, 3, 4 */
    }
}
const char *lface_tool_col_name(int col) { static const char *c[4] = { "Number", "Tool Name", "Insert Info", "Turret" }; return col >= 0 && col < 4 ? c[col] : ""; }

static void planes_now(LfnPlanes *p) {    /* InitOperationViewsToActivePlanes: the active Cplane / Tplane / WCS (cui_planes.c) */
    memset(p, 0, sizeof *p);
    p->cplane = plane_active[0] == 1 ? 0 : plane_active[0]; memcpy(p->corg, plane_active_origin(0), sizeof p->corg);
    p->tplane = plane_active[1] == 1 ? 0 : plane_active[1]; memcpy(p->torg, plane_active_origin(1), sizeof p->torg);
    p->wcs = plane_active[3] == 1 ? 0 : plane_active[3]; memcpy(p->worg, plane_active_origin(3), sizeof p->worg);
}

/* GetToolProperty for list row ROW: the record made again with that tool (lfn_build), the dialog's edits so far kept (as Mastercam's
   page members keep them: only the fields the pick writes change) */
static int pick_row(int row, int keep_edits) {
    if (row < 0 || row >= lf_nltl) return 0;
    char err[300]; uint8_t *r = NULL, *b = NULL; int tp = 0; LfnSpeedSt st, st2;
    if (!keep_edits || !lf_rec || !lf_base) {   /* the dialog opens with this row (lfn_build: MapToDlg's clamps + the pick's checks, L-12) */
        if (!lfn_build_st(&F, lf_ltl[row], &lf_act, NULL, &r, &tp, &st, 1, err, sizeof err)) { cui_message("Lathe Face", "%s", err); return 0; }
        lf_st = st;
        b = malloc(LFN_REC); memcpy(b, r, LFN_REC);
    } else {                                    /* another row picked (OnClickToolbmpsLb): the tool's raw values + the kept edits, then the checks */
        if (!lfn_build_st(&F, lf_ltl[row], &lf_act, NULL, &r, &tp, &st, 0, err, sizeof err)
            || !lfn_build_st(&F, lf_ltl[row], &lf_act, NULL, &b, NULL, &st2, 1, err, sizeof err)) { free(r); cui_message("Lathe Face", "%s", err); return 0; }
        /* the user's edits (bytes that differ from the previous pick's record) carried over */
        for (int k = 0; k < LFN_REC; k++) if (lf_rec[k] != lf_base[k] && !(k >= LFN_OP + 0x2e0 && k < LFN_OP + 0x2e0 + 0x288)) r[k] = lf_rec[k];
        LfnSpeedSt s = lf_st;
        if (!lfn_repick_checks(&F, lf_rec, r, &s, err, sizeof err)) { free(r); free(b); cui_message("Lathe Face", "%s", err); return 0; }
        lf_st = s;
    }
    free(lf_base); lf_base = b;
    free(lf_rec); lf_rec = r; lf_row = row; lf_tplane = tp;
    return 1;
}

static void finish_z_init(void) {   /* CLFaceProp::OnInitDialog 0x1800426d0 -> InitZFinEd(1e30); SetRestCtrls 0x180043710 */
    char err[200]; double z = 0;
    lf_stock = lfn_finish_z(&F, lf_rec, &z, &lf_zlo, &lf_zhi, err, sizeof err);
    if (lf_stock) { wdd(OPB(0x13b8), z); *OPB(0xb68) = 1; }
    else *OPB(0xb68) = 0;                     /* no stock polygon: op +0xb68 = 0 ("Use stock" greyed) */
}

int lface_can_start(char *why, size_t n) { return loaded && lfn_can_start(&F, (const CamOp *const *)camops, ncamops, why, n); }
/* the command (lathe_face 0x1801a3290): the op made (InitOperation), the sheet opened on the tool page with row 0 picked */
void lface_open(void) {
    char why[300];
    if (!lface_can_start(why, sizeof why)) { cui_message("Lathe Face", "%s", loaded ? why : "No part is open."); return; }
    planes_now(&lf_act);
    fill_tools();
    free(lf_rec); free(lf_base); lf_rec = lf_base = NULL; lf_row = -1; lf_face_seen = 0; lf_z_edited = 0;
    /* InitLTool 0x1801abdc0: no earlier Face (FUN_1801c9700) -> row 0; an empty list -> no tool (fLToolDefined 0): what OK does then is
       NOT DECODED, refused at OK */
    if (lf_nltl > 0 && !pick_row(0, 0)) return;
    if (lf_nltl == 0) {
        char err[300]; uint8_t *r = NULL;
        if (!lfn_build_st(&F, -1, &lf_act, NULL, &r, NULL, &lf_st, 1, err, sizeof err)) { cui_message("Lathe Face", "%s", err); return; }
        lf_rec = r; lf_base = malloc(LFN_REC); memcpy(lf_base, r, LFN_REC);
    }
    { double z; char e[200]; lf_stock = lfn_finish_z(&F, lf_rec, &z, &lf_zlo, &lf_zhi, e, sizeof e); }   /* GetOpStockBoundary (SetRestCtrls); the range InitZFinEd will give */
    lface_page = 0; show_lface = 1;
    cam_msg[0] = 0;
}
void lface_cancel(void) { show_lface = 0; free(lf_rec); free(lf_base); lf_rec = lf_base = NULL; }   /* DeleteListAndDB */
void lface_set_page(int pg) {
    if (!show_lface) return;
    lface_page = pg ? 1 : 0;
    if (lface_page == 1 && !lf_face_seen) { lf_face_seen = 1; finish_z_init(); }   /* the page made on first activation */
}
void lface_pick_tool(int row) { if (show_lface && row != lf_row) pick_row(row, 1); }

/* ---- control state */
int lface_ctl_enabled(const LfCtl *c) {
    if (!lf_rec) return 0;
    switch (c->role) {
    case LFR_NA: case LFR_NA_CHECK: case LFR_NA_RADIO: case LFR_NA_COMBO: return 0;
    case LFR_LIST: return 1;
    case LFR_SRCRADIO: return c->off == 0 || lf_stock;                     /* 1340: GetOpStockBoundary != 0xff (SetRestCtrls) */
    case LFR_FINZ: return *OPB(0xb68) != 0;                                 /* 1342 / 1343 with Use stock */
    case LFR_BTN: return c->id == 1338 ? *OPB(0xb68) == 0 : *OPB(0xb68) != 0;   /* 1338 Select Points with "Select points" (OnSelptRb) */
    case LFR_F64: case LFR_I16:
        if (c->id == 787) return *OPB(0x11c0) != 0;                         /* EnableRoughStep 0x180041b50 */
        if (c->id == 796 || c->id == 793) return *OPB(0x11d0) != 0;         /* EnableFinishStep 0x180041ae0 */
        if (c->id == 5191) return *OPB(0x139a) != 0;
        return 1;
    case LFR_I32: if (c->id == 232) return *OPB(0x13a9) != 0; return 1;
    case LFR_FFEEDU: return *OPB(0x139a) != 0;
    case LFR_CSS: case LFR_RPMR: if (c->off == 0x13b0) return *OPB(0x13a9) != 0; return 1;
    default: return 1;
    }
}
int lface_ctl_checked(const LfCtl *c) {
    if (!lf_rec) return 0;
    switch (c->role) {
    case LFR_CHECK: case LFR_ROUGH: case LFR_FINISH: case LFR_AWAY: return *OPB(c->off) != 0;
    case LFR_SRCRADIO: return (*OPB(0xb68) != 0) == (c->off == 1);
    case LFR_FEEDU: { int i = *OPB(0x32e) ? 2 : rdd(OPB(0x310)) < 0 ? 0 : 1; return i == (int)c->off; }   /* radio 0x195 -> page +0x22360 */
    case LFR_FFEEDU: return lf_st.ffeed_mode == (int)c->off;                /* page +0x26e30 (MapToDlg: op +0x13a8 != 'R') */
    case LFR_CSS: return *OPB(c->off) != 0;
    case LFR_RPMR: return *OPB(c->off) == 0;
    default: return 0;
    }
}
static void fmt_d(char *t, size_t n, double v) { snprintf(t, n, "%.4f", v); char *e = t + strlen(t); while (e > t + 1 && e[-1] == '0' && strchr(t, '.') && e[-2] != '.') *--e = 0; }
void lface_ctl_text(const LfCtl *c, char *t, size_t n) {
    t[0] = 0; if (!lf_rec) return;
    switch (c->role) {
    case LFR_I32: snprintf(t, n, "%d", r32(OPB(c->off))); break;
    case LFR_I16: snprintf(t, n, "%d", r16(OPB(c->off))); break;
    case LFR_F64: fmt_d(t, n, rdd(OPB(c->off))); break;
    case LFR_FINZ: fmt_d(t, n, rdd(OPB(c->off))); break;
    case LFR_FEED: fmt_d(t, n, fabs(rdd(OPB(c->off)))); break;               /* MCReal page +0x1c468 = |feed| */
    case LFR_RPM: snprintf(t, n, "%d", abs(r32(OPB(c->off)))); break;        /* |rpm| */
    case LFR_COMMENT: snprintf(t, n, "%.*s", 0x78, (const char *)OPB(6)); break;
    case LFR_NA_COMBO: snprintf(t, n, "%d", c->off == 0x568 ? r16(OPB(c->off)) : *OPB(c->off)); break;   /* item texts NOT DECODED: the value */
    case LFR_STATIC: snprintf(t, n, "%s", c->text); break;
    default: break;
    }
}
int lface_combo(const LfCtl *c, const char *const **items) {
    static const char *DIR[] = { "Left", "Right" }, *ROLL[] = { "None", "Sharp", "All" };   /* mclatheres 788 / 791; 813 / 814 / 815 */
    if (c->role == LFR_COMBO_DIR) { *items = DIR; return 2; }
    if (c->role == LFR_COMBO_ROLL) { *items = ROLL; return 3; }
    return 0;
}
int lface_combo_index(const LfCtl *c) {
    if (!lf_rec) return 0;
    if (c->role == LFR_COMBO_DIR) return *OPB(0x56a) != 0;                  /* init = (op +0x56a != 0) */
    if (c->role == LFR_COMBO_ROLL) { int v = *OPB(0x56b); return v < 2 ? v : 2; }   /* init min(v, 2) */
    return 0;
}

static void set_default_cc(void) {   /* OnFromCenterCb 0x180042670 -> FacePgPrm vfunc +0x10 = SetDefaultCC 0x1801b5460 */
    if (lf_row < 0) return;
    const uint8_t *b = F.ltl[lf_ltl[lf_row]].body; uint32_t bn = F.ltl[lf_ltl[lf_row]].n;
    LtCtx cx; memset(&cx, 0, sizeof cx); cx.metric = mcam_is_metric(&F); cx.op_flip = *OPB(0x2b3); cx.op_axis_combo = r32(OPB(0x11bc));
    cx.tol = mc_systol_units(cx.metric)->tol;
    int idx = *OPB(0x56a), v = idx;
    if (lt_face_default_cc(b, bn, &cx, *OPB(0x1210) != 0, &idx, &v)) *OPB(0x56a) = (uint8_t)v;
}
/* a control changed (Qt / script): VAL is the text for edits, the index for combos / radios, 0/1 for checks; 0 = refused (MSG) */
static int ctl_set(const LfCtl *c, const char *val) {
    if (!lf_rec) return 0;
    if (!lface_ctl_enabled(c)) { snprintf(cam_msg, sizeof cam_msg, "the control is greyed%s", c->role == LFR_NA || c->role == LFR_NA_CHECK || c->role == LFR_NA_RADIO || c->role == LFR_NA_COMBO ? " (NOT DECODED here)" : ""); return 0; }
    double d = atof(val); int i = atoi(val);
    switch (c->role) {
    case LFR_I32: w32(OPB(c->off), i); return 1;
    case LFR_I16:                                                         /* 793: MCInt 1..32000 */
        if (i < 1 || i > 32000) { snprintf(cam_msg, sizeof cam_msg, "Maximum number of finish passes: 1 .. 32000"); return 0; }
        w16(OPB(c->off), (int16_t)i); return 1;
    case LFR_F64:
        /* the MCReal minimums (OnInitDialog): 0 for the stepovers, retract and entry; -1e30 for stock to leave and overcut */
        if (d < 0 && c->id != 790 && c->id != 800 && c->id != 5191) { snprintf(cam_msg, sizeof cam_msg, "the value must be >= 0"); return 0; }
        wdd(OPB(c->off), d); return 1;
    case LFR_FINZ:
        if (d < lf_zlo - mc_systol_units(mcam_is_metric(&F))->tol || d > lf_zhi + mc_systol_units(mcam_is_metric(&F))->tol) {
            snprintf(cam_msg, sizeof cam_msg, "Finish Z: %g .. %g (the stock's X limits, InitZFinEd)", lf_zlo, lf_zhi); return 0; }
        wdd(OPB(c->off), d); lf_z_edited = 1; return 1;                      /* page +0x847c */
    case LFR_FEED: { double s = rdd(OPB(0x310)) < 0 ? -1 : 1; wdd(OPB(0x310), s * fabs(d)); return 1; }
    case LFR_RPM: { int s = r32(OPB(0x328)) < 0 ? -1 : 1; w32(OPB(0x328), s * abs(i)); return 1; }   /* the sign rules of MapToOp: kept as Make_op_tool gave them */
    case LFR_COMMENT: memset(OPB(6), 0, 0x78); snprintf((char *)OPB(6), 0x78, "%s", val); return 1;
    case LFR_CHECK: *OPB(c->off) = (uint8_t)(i != 0); return 1;
    case LFR_ROUGH: case LFR_FINISH: {                                     /* OnRoughStepCb 0x180042d40 / OnFinishStepCb 0x1800425e0 */
        *OPB(c->off) = (uint8_t)(i != 0);
        int other = c->role == LFR_ROUGH ? 0x11d0 : 0x11c0;
        if (!i && !*OPB(other)) *OPB(other) = 1;
        return 1; }
    case LFR_AWAY: *OPB(0x1210) = (uint8_t)(i != 0); set_default_cc(); return 1;
    case LFR_SRCRADIO: *OPB(0xb68) = (uint8_t)(c->off == 1);
        if (!*OPB(0xb68)) { *OPB(0xb68) = lf_stock ? 1 : 0;                  /* SetRestCtrls: no points + a stock -> forced 1 */
                            snprintf(cam_msg, sizeof cam_msg, "Select points: picking the face points (CFaceTpGeometry::GetFaceBoundary) is NOT DECODED here"); }
        return 1;
    case LFR_FEEDU: {                                                       /* MapToOp: in/rev = feed < 0; micro-in = op +0x32e */
        double f = fabs(rdd(OPB(0x310)));
        if (c->off == 2) { *OPB(0x32e) = 1; wdd(OPB(0x310), f); }
        else { *OPB(0x32e) = 0; wdd(OPB(0x310), c->off == 0 ? -f : f); }
        return 1; }
    case LFR_FFEEDU: lf_st.ffeed_mode = (int)c->off; *OPB(0x13a8) = c->off == 0 ? 'R' : 'M'; return 1;   /* MapToOp: index 0 -> 'R', otherwise 'M' */
    case LFR_CSS: *OPB(c->off) = 1; return 1;
    case LFR_RPMR: *OPB(c->off) = 0; return 1;
    case LFR_COMBO_DIR: *OPB(0x56a) = (uint8_t)(i != 0); return 1;
    case LFR_COMBO_ROLL: *OPB(0x56b) = (uint8_t)(i < 0 ? 0 : i > 2 ? 2 : i); return 1;
    case LFR_BTN:
        snprintf(cam_msg, sizeof cam_msg, c->id == 1338 ? "Select Points: the point pick (CLFaceProp::OnSelptPb -> GetFaceBoundary) is NOT DECODED here"
                                                        : "Finish Z: the point pick (CLFaceProp::OnSelectZPb: point_proc_2d + XY_to_lathe) is NOT DECODED here; type the value");
        return 0;
    default: return 0;
    }
}

/* the tool page's feed / speed controls: their handlers end with CLToolPropBase::RunMachineDefChecks(true) 0x180265a60 (L-12) -
   OnKillfocusFeedrateEd 0x1802641c0 (224), OnInRevRb / OnInMinRb / OnSurfaceFinishRb (405 / 416 / 419), OnFinishFeedCb 0x180263740
   (5190, when checked), OnFinishFeedEd 0x180263790 (5191), OnFinishFeedPerRevRb / PerMinRb / SurfaceFinishRb (5192..5194),
   OnKillfocusSpindleSpeedEd 0x180264260 (231), OnCSSRb / OnRPMRb (420 / 421), OnFinishSSCb 0x180263880 (5195, when checked),
   OnFinishSSEd 0x1802638d0 (232), OnFinishCssRb / OnFinishRpmRb (5210 / 423), OnKillfocusMaxSSEd 0x180264200 (237) */
static int runs_checks(const LfCtl *c) {
    switch (c->id) {
    case 224: case 405: case 416: case 419: case 5191: case 5192: case 5193: case 5194: case 231: case 420: case 421: case 232: case 5210: case 423: case 237: return 1;
    case 5190: return *OPB(0x139a) != 0;
    case 5195: return *OPB(0x13a9) != 0;
    }
    return 0;
}
static int lf_warn_off;                       /* DAT_1804b641d: "Turn off warning for session..." (3275 check 500) was checked - for the rest of
                                                 the session (a static of mclathe.dll, never reset) */
int cui_script_check;
int lface_ctl_set(const LfCtl *c, const char *val) {
    if (!ctl_set(c, val)) return 0;
    /* OnKillfocusFeedrateEd / OnFinishFeedEd / OnKillfocusSpindleSpeedEd / OnFinishSSEd: ToolPgPrm +0x2929 = 1 (L-14) */
    if (c->page == 0 && (c->id == 224 || c->id == 5191 || c->id == 231 || c->id == 232)) lf_st.edited = 1;
    if (c->page == 0 && runs_checks(c)) {
        char err[300]; int chg = 0;
        if (!lfn_machine_checks(&F, lf_rec, &lf_st, &chg, err, sizeof err)) { snprintf(cam_msg, sizeof cam_msg, "%s", err); return 0; }
        /* a change: UpdateData(FALSE) + CToolModWarning (dialog 3275 "Tool Settings Modified", FUN_1801af210) */
        /* FUN_1801af210: DoModal of CToolModWarning (MCDialog 0xccb, caption "Tool Settings Modified"; OnInitDialog FUN_1801ad7a0: static
           503 = string 2424; DoDataExchange FUN_1801aa3c0: DDX_Check 500 -> dialog +0x1550; OK FUN_1801adf00 = UpdateData(TRUE) + OnOK),
           then DAT_1804b641d = dialog +0x1550 */
        if (chg && !lf_warn_off) {
            if (cui_hooks.message_check) lf_warn_off = cui_hooks.message_check("Tool Settings Modified", LFN_TOOLMOD_TEXT, "Turn off warning for session...") != 0;
            else { printf("    MESSAGE: %s [Turn off warning for session...: %d]\n", LFN_TOOLMOD_TEXT, cui_script_check); lf_warn_off = cui_script_check != 0; }
        }
    }
    return 1;
}
/* the field is shown in italics (a machine check changed it: the italic font ToolPrmBase +0x48 / CLToolPropBase +0x2b780) */
int lface_ctl_italic(const LfCtl *c) {
    if (!lf_rec) return 0;
    switch (c->id) {
    case 224: return (lf_st.italic & LFS_FEED) != 0;
    case 5191: return (lf_st.italic & LFS_FFEED) != 0;
    case 231: return (lf_st.italic & LFS_RPM) != 0;
    case 232: return (lf_st.italic & LFS_FRPM) != 0;
    case 237: return (lf_st.italic & LFS_MAX) != 0;
    }
    return 0;
}

/* OK: the pages' MapToOp (the record holds them), the op made and its toolpath generated (UpdateListAndDB + bnci_section) */
int lface_ok(void) {
    if (!show_lface || !lf_rec) return 0;
    if (lf_row < 0) { cui_message("Lathe Face", "Lathe Face: no lathe tool is selected (fLToolDefined 0): what OK does then is NOT DECODED"); return 0; }
    CamOp *op = calloc(1, sizeof *op);
    op->p.type = CAM_LFACE; op->p.file_op_id = 0;
    op->p.lf_new = lf_face_seen ? 2 : 1;
    op->p.lf_slot = r32(F.ltl[lf_ltl[lf_row]].body);
    op->p.tool_num = r32(OPB(0x2e0));
    /* the op's planes: the record's (the active planes; the Tplane SetOpDefaultTplane set) */
    op->p.tplane = lf_tplane > 0 ? lf_tplane : lf_act.tplane; memcpy(op->p.torg, lf_act.torg, sizeof op->p.torg);
    op->p.cplane = lf_act.cplane; memcpy(op->p.corg, lf_act.corg, sizeof op->p.corg);
    op->p.wcs = lf_act.wcs; memcpy(op->p.worg, lf_act.worg, sizeof op->p.worg);
    op->p.wofs = op->p.cwofs = op->p.wwofs = -1;
    op->p.tplane_rec_id = op->p.tplane;
    /* the dialog values: the record bytes that differ from the tool pick's record, as record patches (8 bytes at most each) */
    for (int k = LFN_OP; k < LFN_REC; ) {
        if (lf_rec[k] == lf_base[k] || (k >= LFN_OP + 6 && k < LFN_OP + 6 + 0x78)) { k++; continue; }
        int m = 0; while (k + m < LFN_REC && m < 8 && lf_rec[k + m] != lf_base[k + m]) m++;
        if (op->p.nrpatch >= CAM_NRPATCH) { cam_rpatch_full(k); free(op); return 0; }
        op->p.rpatch[op->p.nrpatch].off = (uint16_t)k; op->p.rpatch[op->p.nrpatch].n = (uint8_t)m; op->p.rpatch[op->p.nrpatch].height = 0;
        memcpy(op->p.rpatch[op->p.nrpatch].v, lf_rec + k, (size_t)m); op->p.nrpatch++;
        k += m;
    }
    snprintf(op->name, sizeof op->name, "%.*s", (int)sizeof op->name - 1, (const char *)OPB(6));   /* the comment (op +6) */
    op->op_index = -1;
    if (!cam_generate(&F, op)) {
        cui_message("Lathe Face", "%s", op->error[0] ? op->error : "the toolpath could not be made");
        cam_op_free(op); return 0;
    }
    edit_camop = -1;
    pending = *op; free(op);
    gen_commit();
    lface_cancel();
    return 1;
}

void lface_dump(void) {
    if (!show_lface) { printf("    opdlg lface closed\n"); return; }
    printf("    opdlg lface page %d row %d/%d tplane %d stock %d z [%g %g]\n", lface_page, lf_row, lf_nltl, lf_tplane, lf_stock, lf_zlo, lf_zhi);
    for (int r = 0; r < lf_nltl; r++) { char a[40], b[100], c[40], d[20]; lface_tool_cell(r, 0, a, sizeof a); lface_tool_cell(r, 1, b, sizeof b); lface_tool_cell(r, 2, c, sizeof c); lface_tool_cell(r, 3, d, sizeof d);
        printf("      list %s %s | %s | %s | %s\n", r == lf_row ? ">" : " ", a, b, c, d); }
    for (const LfCtl *c = LF_CTLS; c->text; c++) {
        if (c->role == LFR_STATIC || c->role == LFR_GROUP || c->role == LFR_LIST) continue;
        char t[200]; lface_ctl_text(c, t, sizeof t);
        const char *const *it; int ni = lface_combo(c, &it);
        printf("      ctl p%d %u %s%s", c->page, c->id, lface_ctl_enabled(c) ? "" : "(grey) ", c->text[0] ? c->text : "");
        if (t[0]) printf(" = %s", t);
        if (lface_ctl_italic(c)) printf(" (italic)");
        if (ni) printf(" = %s", it[lface_combo_index(c)]);
        if (c->role == LFR_CHECK || c->role == LFR_ROUGH || c->role == LFR_FINISH || c->role == LFR_AWAY || c->role == LFR_SRCRADIO || c->role == LFR_FEEDU
            || c->role == LFR_FFEEDU || c->role == LFR_CSS || c->role == LFR_RPMR) printf(" [%d]", lface_ctl_checked(c));
        printf("\n");
    }
}

/* script steps (tests): lface (the menu command), lfpage:0|1, lftool:ROW, lfset:ID=V (page from the id), lfok, lfcancel, lfdump */
int cui_lface_script(const char *st, const char *arg) {
    if (!strcmp(st, "lface")) { lface_open(); if (cam_msg[0]) printf("    %s\n", cam_msg); return 1; }
    if (!strcmp(st, "lfpage") && arg) { lface_set_page(atoi(arg)); return 1; }
    if (!strcmp(st, "lftool") && arg) { lface_pick_tool(atoi(arg)); return 1; }
    if (!strcmp(st, "lfset") && arg) {
        const char *eq = strchr(arg, '='); if (!eq) return 1;
        unsigned id = (unsigned)strtoul(arg, NULL, 10); const LfCtl *c = ctl_by_id(1, id); if (!c) c = ctl_by_id(0, id);
        if (!c) { printf("    (no control %u)\n", id); return 1; }
        if (c->page == 1) lface_set_page(1);
        if (!lface_ctl_set(c, eq + 1)) printf("    (op control %u refused: %s)\n", id, cam_msg);
        return 1;
    }
    if (!strcmp(st, "lfwarncheck") && arg) { cui_script_check = atoi(arg); return 1; }   /* the 3275 check box state a script answers with */
    if (!strcmp(st, "lfok")) { int ok = lface_ok(); printf("    opdlg lface ok %d %s\n", ok, cam_msg); return 1; }
    if (!strcmp(st, "lfcancel")) { lface_cancel(); return 1; }
    if (!strcmp(st, "lfdump")) { lface_dump(); return 1; }
    if (!strcmp(st, "lfmoves") && arg) {        /* lfmoves:N - tree op N's moves (ours): lathe X / Z (world (Z, X, 0), lathe_face.c) */
        int n = atoi(arg) - 1; const CamOp *o = NULL;
        for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == n) o = camops[j];
        if (!o) { printf("    (op %d: not computed here)\n", n + 1); return 1; }
        printf("    opdlg lface moves op %d: %d\n", n + 1, o->nm);
        for (int i = 0; i < o->nm; i++) printf("      list G%d X%.5f Z%.5f\n", o->m[i].rapid ? 0 : 1, o->m[i].p.y, o->m[i].p.x);
        return 1;
    }
    return 0;
}
