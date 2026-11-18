/* cui_ecp.c - Edit Common Parameters (opmanres dialog 243), moved out of viewer/mcamui.c in phase 8 of
 * formats/qt_port_plan.md (names and citations as they were there).
 *
 * opmanres dialog 243 "Edit Common Parameters (Mill/Router)"; RunEditCommonParams -> CEditCommonDlg, mastercam.dll:
 * OnInitDialog FUN_180143a10, OnOK FUN_180143d70, Apply FUN_180143180, per-op apply FUN_180145b50 = cam_common_apply. The
 * dialog starts with every setting off and the values of a zeroed operation (MCReal / MCLong 0 -> "0.0" / "0":
 * make_mcreal_str, 6 places, trailing zeros cut); each check box enables its controls (FUN_1801417c0 ...); OK / Apply first
 * check that no enabled field is empty (3786 / 3787) and no enabled choice is blank, then apply to every selected op. Not
 * here (shown greyed): the Clearance... / Retract... pick buttons, Coolant, Canned text, Misc. values, Home pos, Planes, NC
 * Name, Tool display, Ref point, Select library tool, Tool filter, Help; the machine definition rpm / feed clamp
 * (FUN_1801451d0, 3788 / 3789). The op list's columns (COpMgrList, flags 0x101 / 9) are NOT DECODED: the rows read as the
 * Toolpaths tree's. Mastercam marks edited ops dirty (or patches their NCI, bnci_update); here they are regenerated at once. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "core_ui.h"
#include "cam_fileop.h"

int show_ecp, ecp_new;
CamCommonEdit ecp;
char ecp_txt[EF_N][32];
int ecp_list = -1, ecp_tool = -1, ecp_on_toolsel;
const char *const ECP_USE_S[3] = { "No change", "Enable", "Disable" };                                    /* opmanres 3775, 3799, 3800 */
const char *const ECP_WHEN_S[3] = { "No change", "Always use Clearance", "Clearance at Start and End" };  /* 3775, 3778, 3779 */
const char *const ECP_AI_S[3] = { "No change", "Absolute", "Incremental" };                              /* 3775 - 3777 */
const char *const ECP_RAPID_S[2] = { "Rapid retract", "Retract rate" };                                  /* 3780, 3781 */
const char *const ECP_FTC_S[2] = { "Force tool change = On", "Force tool change = Off" };                /* 3782, 3783 */
const char *const ECP_BATCH_S[2] = { "To batch", "No batch" };                                           /* 3784, 3785 */
const char *const ECP_DIR_S[2] = { "CW", "CCW" };                                                        /* 3804, 3806 */

void ecp_real_str(char *b, size_t n, double v) {      /* make_mcreal_str: "%.6f", trailing zeros cut to one */
    snprintf(b, n, "%.6f", v);
    size_t l = strlen(b);
    while (l > 1 && b[l - 1] == '0' && b[l - 2] != '.') b[--l] = 0;
}
int ecp_is_long(int k) { return k == EF_TOOL || k == EF_HEAD || k == EF_LOFS || k == EF_DOFS || k == EF_RPM; }
void ecp_open(void) {
    memset(&ecp, 0, sizeof ecp);
    ecp.on_dir = 0; ecp.dir = -1; ecp.rapid = ecp.ftc = ecp.batch = -1;   /* combos filled without a selection */
    for (int k = 0; k < EF_N; k++) {
        if (ecp_is_long(k)) snprintf(ecp_txt[k], sizeof ecp_txt[k], "0"); else ecp_real_str(ecp_txt[k], sizeof ecp_txt[k], 0);
    }
    ecp_list = -1; ecp_tool = -1; ecp_on_toolsel = 0; show_ecp = 1; ecp_new = 1;
    tm_cs_default(ecp_cs, 0);                                  /* a new CEditCommonDlg: its list's defaults (below) */
}
/* N13-32 (decoded, no deviation): the tool list 395 is a CMillToolListCtrl (CEditCommonDlg +0x34d8, mastercam.dll). Set up by
   FUN_180142840 (from OnInitDialog FUN_180143a10): SetListObject(the part's tools: GetInternalToolSystem vtable +0x1c8), word
   +0x3b81 = 0 (list +0x6a9 = 0: column 11 Rad. Type hidden in every set; +0x6aa = 0 = a part list), InitMachGroup, InitColumns
   (-> ResetListCtrlColumns 0x18050a7f0), InitBitmaps, SetViewMode(1) Details, LVM_SETCOLUMNWIDTH LVSCW_AUTOSIZE_USEHEADER on columns
   1..4 (NOT PORTED: the default widths), FillList(-1); display mode = the ctor's 2, sorted by column 2 ascending. mastercam.dll
   imports neither ReadRegistrySettings nor WriteRegistrySettings: nothing is read or kept. CEditCommonDlg's message map
   (0x1805cfa10): WM_NOTIFY LVN_COLUMNCLICK from 395 -> 0x180143b10 = SortList(NMLISTVIEW iSubItem) 0x18050b3e0 on +0x34d8. */
TmColSet ecp_cs[3];
void ecp_sortlist(int c) { tm_sortset(&ecp_cs[2], c); }
int ecp_tool_cols(int *cols, int *w) {
    const TmColSet *cs = &ecp_cs[2]; int nc = 0;
    for (int k = 0; k < TM_COLS; k++) { int c = cs->ord[k]; if (c >= 2 && cs->vis[c]) { cols[nc] = c; w[nc] = cs->w[c] > 4 ? cs->w[c] : 4; nc++; } }
    return nc;
}
int ecp_tool_rows(int *idx, int cap) {                        /* FillList: the part tools (the list's CToolFilt: not ported), SortColumn order */
    int n = 0;
    for (int i = 0; i < F.ntools && n < cap; i++) idx[n++] = i;
    tm_sort_rows(0, &ecp_cs[2], idx, n);
    return n;
}
/* Propagate (FUN_180143eb0): the values of the op picked in the list into the fields (check boxes unchanged) */
void ecp_propagate(int i) {
    if (i < 0 || i >= F.nops) return;
    int k = -1; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) k = j;
    const Op *o = &F.ops[i]; const uint8_t *r = o->raw; uint32_t rn = o->raw_n;
#define RD(T, off) ({ T v_ = 0; if (r && (off) + sizeof(T) <= rn) memcpy(&v_, r + (off), sizeof(T)); v_; })
    if (k < 0 && !r) return;
    if (k < 0) {                                              /* stored in the part: its record */
        snprintf(ecp_txt[EF_TOOL], 32, "%d", RD(int32_t, 0x348)); snprintf(ecp_txt[EF_HEAD], 32, "%d", RD(int32_t, 0x5a4));
        ecp_real_str(ecp_txt[EF_FEED], 32, RD(double, 0x378)); ecp_real_str(ecp_txt[EF_PLUNGE], 32, RD(double, 0x380));
        snprintf(ecp_txt[EF_LOFS], 32, "%d", RD(int32_t, 0x374)); snprintf(ecp_txt[EF_DOFS], 32, "%d", RD(int32_t, 0x370));
        ecp_real_str(ecp_txt[EF_RRATE], 32, RD(double, 0x388));
        int rpm = RD(int32_t, 0x390); snprintf(ecp_txt[EF_RPM], 32, "%d", abs(rpm)); ecp.dir = rpm < 0;
        ecp_real_str(ecp_txt[EF_CLR], 32, RD(double, 0x170)); ecp.clr_use = RD(uint8_t, 0x179) ? 1 : 2; ecp.clr_when = RD(uint8_t, 0x319) ? 2 : 1; ecp.clr_inc = RD(uint8_t, 0x178) ? 2 : 1;
        ecp_real_str(ecp_txt[EF_RET], 32, RD(double, 0x180)); ecp.ret_use = RD(uint8_t, 0x189) ? 1 : 2; ecp.ret_inc = RD(uint8_t, 0x188) ? 2 : 1;
        ecp_real_str(ecp_txt[EF_FP], 32, RD(double, 0x190)); ecp.fp_inc = RD(uint8_t, 0x198) ? 2 : 1;
        ecp_real_str(ecp_txt[EF_TOP], 32, RD(double, 0x2e8)); ecp.top_inc = RD(uint8_t, 0x2f0) ? 2 : 1;
        ecp_real_str(ecp_txt[EF_DEP], 32, RD(double, 0x1a0)); ecp.dep_inc = RD(uint8_t, 0x1a8) ? 2 : 1;
        ecp.rapid = RD(uint8_t, 0x1a9) == 0; ecp.ftc = RD(uint8_t, 0x2f1) == 0; ecp.batch = RD(uint8_t, 0x2f9) == 0;
    } else {                                                  /* made here: the parameters as the save writes them */
        const CamParams *p = &camops[k]->p;
        snprintf(ecp_txt[EF_TOOL], 32, "%d", p->tool_num);
        ecp_real_str(ecp_txt[EF_FEED], 32, p->feed); ecp_real_str(ecp_txt[EF_PLUNGE], 32, p->plunge_feed);
        snprintf(ecp_txt[EF_RPM], 32, "%d", abs(p->rpm)); ecp.dir = p->rpm < 0;
        ecp_real_str(ecp_txt[EF_CLR], 32, p->clearance); ecp.clr_use = 1; ecp.clr_inc = 1;
        if (p->retract_z > -1e29) { ecp_real_str(ecp_txt[EF_RET], 32, p->retract_z); ecp.ret_use = 1; } else ecp.ret_use = 2;
        ecp.ret_inc = 1;
        if (p->type == CAM_DRILL) { ecp_real_str(ecp_txt[EF_FP], 32, p->r_plane); ecp.fp_inc = 1; }
        else { ecp_real_str(ecp_txt[EF_FP], 32, p->feed_plane); ecp.fp_inc = 2; }
        ecp_real_str(ecp_txt[EF_TOP], 32, p->top_z); ecp.top_inc = 1;
        ecp_real_str(ecp_txt[EF_DEP], 32, p->final_z); ecp.dep_inc = 1;
        /* head #, offsets, retract rate, when, rapid retract, force tool change, batch: from the template record the
           save copies - not known before saving (NOT DONE) */
    }
#undef RD
}
/* the enabled fields / choices are all filled in (FUN_180140c00) */
int *ecp_on_ptr(int k) {
    int *on[EF_N] = { &ecp.on_tool, &ecp.on_head, &ecp.on_feed, &ecp.on_plunge, &ecp.on_lofs, &ecp.on_dofs, &ecp.on_rrate, &ecp.on_rpm,
                      &ecp.on_clr, &ecp.on_ret, &ecp.on_fp, &ecp.on_top, &ecp.on_dep };
    return k >= 0 && k < EF_N ? on[k] : NULL;
}
int ecp_valid(void) {
    for (int k = 0; k < EF_N; k++) if (*ecp_on_ptr(k) && !ecp_txt[k][0]) return 0;
    if ((ecp.on_dir && ecp.dir < 0) || (ecp.on_rapid && ecp.rapid < 0) || (ecp.on_ftc && ecp.ftc < 0) || (ecp.on_batch && ecp.batch < 0)) return 0;
    return 1;
}
static int ecp_edit_fn(CamOp *op, const uint8_t *raw, uint32_t rawn, void *ctx) {
    cam_common_apply(&F, op, (const CamCommonEdit *)ctx, raw, rawn); return 1;
}
void ecp_apply(void) {
    if (!ecp_valid()) { cui_message("Missing Parameter", "A value must be specified for all parameters being edited.\n\n(The missing parameter field will be highlighted)"); return; }   /* opmanres 3787 / 3786 */
    ecp.tool = atoi(ecp_txt[EF_TOOL]); ecp.head = atoi(ecp_txt[EF_HEAD]); ecp.lofs = atoi(ecp_txt[EF_LOFS]); ecp.dofs = atoi(ecp_txt[EF_DOFS]);
    ecp.rpm = atoi(ecp_txt[EF_RPM]); ecp.feed = atof(ecp_txt[EF_FEED]); ecp.plunge = atof(ecp_txt[EF_PLUNGE]); ecp.rrate = atof(ecp_txt[EF_RRATE]);
    ecp.clr = atof(ecp_txt[EF_CLR]); ecp.ret = atof(ecp_txt[EF_RET]); ecp.fp = atof(ecp_txt[EF_FP]); ecp.top = atof(ecp_txt[EF_TOP]); ecp.dep = atof(ecp_txt[EF_DEP]);
    ecp.toolsel = ecp_on_toolsel && ecp_tool >= 0 && ecp_tool < F.ntools ? &F.tools[ecp_tool] : NULL;
    CamCommonEdit ce = ecp; ce.on_dir = ecp.on_dir && ecp.dir >= 0;
    edit_selected(ecp_edit_fn, &ce, NULL);
}
void ecp_ok(void) { if (ecp_valid()) { ecp_apply(); show_ecp = 0; } else ecp_apply(); }   /* OnOK: the check, then apply + close */
void ecp_cancel(void) { show_ecp = 0; }                      /* Cancel / the title bar X */
void ecp_all(int on) {                                        /* Enable All FUN_180143060 / Disable All FUN_180142f40 */
    int *p[] = { &ecp.on_tool, &ecp.on_head, &ecp.on_feed, &ecp.on_plunge, &ecp.on_lofs, &ecp.on_dofs, &ecp.on_rrate, &ecp.on_rpm, &ecp.on_dir,
                 &ecp.on_clr, &ecp.on_ret, &ecp.on_fp, &ecp.on_top, &ecp.on_dep, &ecp.on_rapid, &ecp.on_ftc, &ecp.on_batch, &ecp_on_toolsel };
    for (size_t k = 0; k < sizeof p / sizeof p[0]; k++) *p[k] = on;
}
/* the template (opmanres 243, 531 x 383 du; exports/opmanres_dialogs.tsv) with each control's binding: role E_CHECK + field
   = the setting's check box, E_EDIT = its edit, E_COMBO = a choice (c = ECC_*), E_BTN = a push button (OK / Cancel / Apply /
   Enable / Disable / Propagate live; the rest greyed), E_LIST = the op list / tool list, E_GROUP / E_STATIC = text */
const EcpCtl ECP_243[] = {
    { 659, ECR_LIST, 0, 7, 25, 168, 211, "" }, { 348, ECR_BTN, 0, 7, 7, 66, 14, "Propagate" },
    { 4294901759u, ECR_GROUP, 0, 183, 4, 172, 233, "" },
    { 1226, ECR_CHECK, EF_CLR, 224, 15, 10, 14, "" }, { 1217, ECR_BTN, -1, 238, 15, 58, 14, "Clearance..." }, { 356, ECR_EDIT, EF_CLR, 302, 15, 40, 14, "" },
    { 4294901759u, ECR_STATIC, 0, 193, 32, 40, 12, "Use" }, { 358, ECR_COMBO, ECC_CLR_USE, 238, 32, 105, 46, "" },
    { 4294901759u, ECR_STATIC, 0, 193, 47, 40, 12, "When" }, { 654, ECR_COMBO, ECC_CLR_WHEN, 238, 47, 105, 48, "" },
    { 4294901759u, ECR_STATIC, 0, 193, 62, 40, 12, "Abs/Inc" }, { 360, ECR_COMBO, ECC_CLR_INC, 238, 62, 105, 46, "" },
    { 25376, ECR_CHECK, EF_RET, 222, 80, 10, 14, "" }, { 954, ECR_BTN, -1, 238, 80, 58, 14, "Retract..." }, { 361, ECR_EDIT, EF_RET, 302, 80, 40, 14, "" },
    { 4294901759u, ECR_STATIC, 0, 193, 100, 40, 12, "Use" }, { 655, ECR_COMBO, ECC_RET_USE, 238, 97, 105, 46, "" },
    { 4294901759u, ECR_STATIC, 0, 193, 112, 40, 12, "Abs/Inc" }, { 365, ECR_COMBO, ECC_RET_INC, 238, 112, 105, 48, "" },
    { 967, ECR_CHECK, EF_FP, 222, 130, 10, 14, "" }, { 1373, ECR_BTN, -1, 238, 130, 58, 14, "Feed plane..." }, { 363, ECR_EDIT, EF_FP, 302, 130, 40, 14, "" },
    { 4294901759u, ECR_STATIC, 0, 193, 147, 40, 12, "Abs/Inc" }, { 367, ECR_COMBO, ECC_FP_INC, 238, 147, 105, 46, "" },
    { 387, ECR_CHECK, EF_TOP, 222, 165, 10, 14, "" }, { 1374, ECR_BTN, -1, 238, 165, 58, 14, "Top of stock..." }, { 369, ECR_EDIT, EF_TOP, 302, 165, 40, 14, "" },
    { 4294901759u, ECR_STATIC, 0, 193, 182, 40, 12, "Abs/Inc" }, { 371, ECR_COMBO, ECC_TOP_INC, 238, 182, 105, 47, "" },
    { 1242, ECR_CHECK, EF_DEP, 222, 200, 10, 14, "" }, { 973, ECR_BTN, -1, 238, 200, 58, 14, "Depth..." }, { 373, ECR_EDIT, EF_DEP, 302, 200, 40, 14, "" },
    { 4294901759u, ECR_STATIC, 0, 193, 217, 40, 12, "Abs/Inc" }, { 375, ECR_COMBO, ECC_DEP_INC, 238, 217, 105, 47, "" },
    { 4294901759u, ECR_GROUP, 0, 363, 18, 161, 28, "Rapid Retract" }, { 240, ECR_CHECK, ECK_RAPID, 371, 27, 16, 12, "" }, { 377, ECR_COMBO, ECC_RAPID, 393, 27, 120, 47, "" },
    { 4294901759u, ECR_GROUP, 0, 363, 52, 161, 28, "Force Tool Change" }, { 241, ECR_CHECK, ECK_FTC, 371, 62, 16, 12, "" }, { 384, ECR_COMBO, ECC_FTC, 393, 62, 120, 46, "" },
    { 4115, ECR_GROUP, 0, 363, 87, 161, 28, "Batch" }, { 25601, ECR_CHECK, ECK_BATCH, 371, 96, 16, 12, "" }, { 382, ECR_COMBO, ECC_BATCH, 393, 96, 120, 47, "" },
    { 239, ECR_CHECK, -1, 362, 121, 16, 14, "" }, { 269, ECR_BTN, -1, 378, 121, 60, 14, "Coolant..." },
    { 391, ECR_CHECK, -1, 448, 121, 16, 14, "" }, { 389, ECR_BTN, -1, 464, 121, 60, 14, "Canned text..." },
    { 1072, ECR_CHECK, -1, 362, 142, 16, 14, "" }, { 1180, ECR_BTN, -1, 378, 142, 60, 14, "Misc. values..." },
    { 1066, ECR_CHECK, -1, 448, 142, 16, 14, "" }, { 1174, ECR_BTN, -1, 464, 142, 60, 14, "Home pos..." },
    { 1080, ECR_CHECK, -1, 362, 163, 16, 14, "" }, { 1189, ECR_BTN, -1, 378, 163, 60, 14, "Planes..." },
    { 393, ECR_CHECK, -1, 448, 163, 16, 14, "" }, { 1255, ECR_BTN, -1, 464, 163, 60, 14, "NC Name..." },
    { 1078, ECR_CHECK, -1, 362, 200, 16, 14, "" }, { 1186, ECR_BTN, -1, 378, 200, 60, 14, "Tool display..." }, { 656, ECR_COMBO, -1, 444, 200, 80, 48, "" },
    { 1069, ECR_CHECK, -1, 362, 221, 16, 14, "" }, { 1177, ECR_BTN, -1, 378, 221, 60, 14, "Ref point..." }, { 658, ECR_COMBO, -1, 444, 221, 80, 48, "" },
    { 342, ECR_GROUP, 0, 7, 243, 261, 113, "Tool Properties" },
    { 415, ECR_CHECK, EF_TOOL, 13, 254, 75, 14, "Tool number:" }, { 413, ECR_EDIT, EF_TOOL, 92, 254, 40, 14, "" },
    { 434, ECR_CHECK, EF_LOFS, 143, 254, 75, 14, "Len. offset:" }, { 399, ECR_EDIT, EF_LOFS, 221, 254, 40, 14, "" },
    { 419, ECR_CHECK, EF_HEAD, 13, 275, 75, 14, "Head number:" }, { 417, ECR_EDIT, EF_HEAD, 92, 275, 40, 14, "" },
    { 402, ECR_CHECK, EF_DOFS, 143, 275, 75, 14, "Dia. offset:" }, { 400, ECR_EDIT, EF_DOFS, 221, 275, 40, 14, "" },
    { 423, ECR_CHECK, EF_FEED, 13, 296, 75, 14, "Feed rate:" }, { 421, ECR_EDIT, EF_FEED, 92, 296, 40, 14, "" },
    { 430, ECR_CHECK, EF_RRATE, 143, 296, 75, 14, "Retract rate:" }, { 407, ECR_EDIT, EF_RRATE, 221, 296, 40, 14, "" },
    { 426, ECR_CHECK, EF_PLUNGE, 13, 317, 75, 14, "Plunge rate:" }, { 424, ECR_EDIT, EF_PLUNGE, 92, 317, 40, 14, "" },
    { 411, ECR_CHECK, EF_RPM, 143, 317, 75, 14, "Spindle speed:" }, { 404, ECR_EDIT, EF_RPM, 221, 317, 40, 14, "" },
    { 409, ECR_CHECK, ECK_DIR, 143, 338, 75, 14, "Spindle direction:" }, { 742, ECR_COMBO, ECC_DIR, 221, 338, 40, 30, "" },
    { 4294901759u, ECR_GROUP, 0, 274, 243, 250, 113, "" }, { 379, ECR_CHECK, ECK_TOOLSEL, 279, 252, 75, 10, "Tool Selection" },
    { 397, ECR_BTN, -1, 367, 250, 66, 14, "Select library tool..." }, { 274, ECR_CHECK, -1, 447, 252, 10, 10, "" }, { 272, ECR_BTN, -1, 459, 250, 60, 14, "Tool filter" },
    { 395, ECR_LIST, 1, 279, 268, 238, 82, "" },
    { 25055, ECR_BTN, 0, 7, 360, 30, 16, "Enable All Settings" }, { 25054, ECR_BTN, 0, 47, 360, 30, 16, "Disable All Settings" },
    { 1, ECR_BTN, 0, 368, 358, 36, 18, "OK" }, { 2, ECR_BTN, 0, 408, 358, 36, 18, "Cancel" }, { 12321, ECR_BTN, 0, 448, 358, 36, 18, "Apply" },
    { 9, ECR_BTN, -1, 488, 358, 36, 18, "Help" },
    { 0, 0, 0, 0, 0, 0, 0, NULL } };
int *ecp_check_ptr(int f) {
    if (f >= 0 && f < EF_N) return ecp_on_ptr(f);
    switch (f) { case ECK_RAPID: return &ecp.on_rapid; case ECK_FTC: return &ecp.on_ftc; case ECK_BATCH: return &ecp.on_batch;
                 case ECK_DIR: return &ecp.on_dir; case ECK_TOOLSEL: return &ecp_on_toolsel; }
    return NULL;
}
/* a choice's items, its value and its check box (enabled = the check box on) */
int ecp_combo(int c, const char *const **items, int **val, int **on) {
    switch (c) {
    case ECC_CLR_USE: *items = ECP_USE_S; *val = &ecp.clr_use; *on = &ecp.on_clr; return 3;
    case ECC_CLR_WHEN: *items = ECP_WHEN_S; *val = &ecp.clr_when; *on = &ecp.on_clr; return 3;
    case ECC_CLR_INC: *items = ECP_AI_S; *val = &ecp.clr_inc; *on = &ecp.on_clr; return 3;
    case ECC_RET_USE: *items = ECP_USE_S; *val = &ecp.ret_use; *on = &ecp.on_ret; return 3;
    case ECC_RET_INC: *items = ECP_AI_S; *val = &ecp.ret_inc; *on = &ecp.on_ret; return 3;
    case ECC_FP_INC: *items = ECP_AI_S; *val = &ecp.fp_inc; *on = &ecp.on_fp; return 3;
    case ECC_TOP_INC: *items = ECP_AI_S; *val = &ecp.top_inc; *on = &ecp.on_top; return 3;
    case ECC_DEP_INC: *items = ECP_AI_S; *val = &ecp.dep_inc; *on = &ecp.on_dep; return 3;
    case ECC_RAPID: *items = ECP_RAPID_S; *val = &ecp.rapid; *on = &ecp.on_rapid; return 2;
    case ECC_FTC: *items = ECP_FTC_S; *val = &ecp.ftc; *on = &ecp.on_ftc; return 2;
    case ECC_BATCH: *items = ECP_BATCH_S; *val = &ecp.batch; *on = &ecp.on_batch; return 2;
    case ECC_DIR: *items = ECP_DIR_S; *val = &ecp.dir; *on = &ecp.on_dir; return 2;
    }
    return 0;
}
void ecp_op_row(int i, char *t, size_t n) { const Op *o = &F.ops[i]; snprintf(t, n, "%d - %s%s%s", i + 1, o->type, o->comment[0] ? " - " : "", o->comment); }
void ecp_button(int id) {
    if (id == 348) { if (ecp_list >= 0) ecp_propagate(ecp_list); }   /* enabled with a list selection (FUN_180144520) */
    else if (id == 25055) ecp_all(1); else if (id == 25054) ecp_all(0);
    else if (id == 1) ecp_ok(); else if (id == 2) ecp_cancel(); else if (id == 12321) ecp_apply();
}
void ecp_dump(void) {
    if (!show_ecp) return;
    printf("    ecp list %d tool %d toolsel %d:", ecp_list, ecp_tool, ecp_on_toolsel);
    static const char *fn[EF_N] = { "tool", "head", "feed", "plunge", "lofs", "dofs", "rrate", "rpm", "clr", "ret", "fp", "top", "dep" };
    for (int k = 0; k < EF_N; k++) printf(" %s%s=%s", *ecp_on_ptr(k) ? "+" : "", fn[k], ecp_txt[k]);
    printf(" | use %d %d when %d inc %d %d %d %d %d | rapid %d%+d ftc %d%+d batch %d%+d dir %d%+d\n", ecp.clr_use, ecp.ret_use, ecp.clr_when,
           ecp.clr_inc, ecp.ret_inc, ecp.fp_inc, ecp.top_inc, ecp.dep_inc, ecp.on_rapid, ecp.rapid, ecp.on_ftc, ecp.ftc, ecp.on_batch, ecp.batch, ecp.on_dir, ecp.dir);
}
/* script steps (as mcamui run_script): ecp, ecpset:KEY=V (a field enables itself), ecpok; ours: ecpbtn:ID, ecpcancel, ecpsort:C, ecprows */
int cui_ecp_script(const char *st, const char *arg) {
    if (!strcmp(st, "ecp")) { ecp_open(); return 1; }                 /* Edit common parameters... */
    if (!strcmp(st, "ecpset") && arg) {               /* ecpset:feed=12.5 (enables it), clr_inc=2, toolsel=K, ... */
        char k[32]; const char *eq = strchr(arg, '='); if (eq && eq - arg < 31) {
            memcpy(k, arg, eq - arg); k[eq - arg] = 0; const char *v = eq + 1;
            static const char *fn[EF_N] = { "tool", "head", "feed", "plunge", "lofs", "dofs", "rrate", "rpm", "clr", "ret", "fp", "top", "dep" };
            struct { const char *n; int *p; } cb[] = { { "dir", &ecp.dir }, { "clr_use", &ecp.clr_use }, { "clr_when", &ecp.clr_when }, { "clr_inc", &ecp.clr_inc },
                { "ret_use", &ecp.ret_use }, { "ret_inc", &ecp.ret_inc }, { "fp_inc", &ecp.fp_inc }, { "top_inc", &ecp.top_inc }, { "dep_inc", &ecp.dep_inc },
                { "rapid", &ecp.rapid }, { "ftc", &ecp.ftc }, { "batch", &ecp.batch }, { "on_dir", &ecp.on_dir }, { "on_rapid", &ecp.on_rapid },
                { "on_ftc", &ecp.on_ftc }, { "on_batch", &ecp.on_batch }, { "on_clr", &ecp.on_clr }, { "on_ret", &ecp.on_ret }, { "toolsel", &ecp_tool }, { "list", &ecp_list } };
            for (int f = 0; f < EF_N; f++) if (!strcmp(k, fn[f])) { snprintf(ecp_txt[f], sizeof ecp_txt[f], "%s", v); *ecp_on_ptr(f) = 1; }
            for (size_t c = 0; c < sizeof cb / sizeof cb[0]; c++) if (!strcmp(k, cb[c].n)) { *cb[c].p = atoi(v); if (!strcmp(k, "toolsel")) ecp_on_toolsel = 1; }
            if (!strcmp(k, "propagate")) ecp_propagate(atoi(v) - 1);
        }
        return 1;
    }
    if (!strcmp(st, "ecpok")) { ecp_apply(); show_ecp = 0; return 1; }
    if (!strcmp(st, "ecpbtn") && arg) { if (show_ecp) ecp_button(atoi(arg)); return 1; }
    if (!strcmp(st, "ecpcancel")) { ecp_cancel(); return 1; }
    if (!strcmp(st, "ecpdump")) { ecp_dump(); return 1; }
    if (!strcmp(st, "ecpsort") && arg) { if (show_ecp && ecp_on_toolsel) ecp_sortlist(atoi(arg)); return 1; }   /* N13-32: a click on the column C header (the list is live with Tool # selection on, as the Qt widget) */
    if (!strcmp(st, "ecprows") && show_ecp) {          /* the tool list's rows in their order: row = the part tool's index (* = selected) */
        int idx[4096], nr = ecp_tool_rows(idx, 4096), cols[TM_COLS], wd[TM_COLS], nc = ecp_tool_cols(cols, wd);
        const char *sh = "-"; for (int k = 0; k < nc; k++) if (cols[k] == ecp_cs[2].sortcol) sh = tm_head[cols[k]];   /* the header's sort mark */
        printf("    ecp tools sort %s %s\n", sh, ecp_cs[2].sortasc ? "ascending" : "descending");
        for (int r = 0; r < nr; r++) { printf("    ecp tool %d%s:", idx[r], idx[r] == ecp_tool ? "*" : "");
            for (int k = 0; k < nc; k++) { char cl[160]; tm_cell(&F.tools[idx[r]], cols[k], cl, sizeof cl); printf(" [%s]", cl); } printf("\n"); }
        return 1;
    }
    return 0;
}
