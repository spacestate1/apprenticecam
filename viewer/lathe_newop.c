/* lathe_newop.c - a NEW Lathe Face operation (opcode 0x42) as Mastercam 2017 makes it from the Toolpaths manager menu
 * (opmanres MENU 780, Lathe toolpaths > Face = 3680 -> FT "lathe_face", decomp/opman_create_map.tsv) or the Turning ribbon's
 * Face (cmd_labels "lathe_face"): mclathe.dll lathe_face 0x1801a3290 -> init_operation(op, 0x42) (mccore InitOperation,
 * decomp/mccore_ops.c) -> lathe_toolpaths_start 0x18019fe10 -> CLFaceTp::lface 0x180046000 -> edit_lathe_operation 0x1801c7480
 * (the two-page sheet: tool page 3911 CLToolProp, Face page 5361 CLFaceProp). Spec: ../formats/lathe.md section 8.
 * Every step cites its function; what is not decoded is refused with its name, never replaced. */
#define LATHE_STOCK_MCAM
#include "lathe_newop.h"
#include "lathe_face.h"
#include "lathe_stock.h"
#include "lathe_tool.h"
#include "lathe_axis.h"
#include "cam_fileop.h"
#include "mcam_saveops.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t r32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static void wdd(uint8_t *p, double v) { memcpy(p, &v, 8); }
#define OP(r, o) ((r) + LFN_OP + (o))

const struct McGrpLathe *lfn_lathe_group(const McamFile *f, char *why, size_t whyn) {
    /* GetActiveLatheGroup (reset_lathe_op_prm_defaults 0x1801d3170): the active machine group = the last machine group
       (COpMgrTree::LoadTree; mcam_load.c stock_grp) when its type op_group +0x19e8 is 1 (lathe) */
    for (int k = 0; k < f->nlgrp; k++) if (f->lgrp[k].id == f->stock_grp) {
        const struct McGrpLathe *g = &f->lgrp[k];
        if (g->gtype == 1) {
            /* the group's Feed Calculation (+0x119c) and its user values (+0x11a0 feed / +0x11b0 rpm) as they are now: the
               Group Properties' Tool Settings page edits them in F's grp_* fields (core_ui/cui_grpprops.c) for the group grp_id */
            if (!f->have_grp || f->grp_id != g->id) return g;
            static _Thread_local struct McGrpLathe live;
            live = *g; live.g119c = f->grp_fs_mode; live.g11a0 = f->grp_feed; live.g11b0 = f->grp_rpm;
            return &live;
        }
        if (why) snprintf(why, whyn, g->gtype == 6 ? "the active machine group is a Mill-Turn group (its menu 1119 and machine definition are NOT DECODED)"
                                                : "the active machine group is not a lathe group (op_group +0x19e8 = %d)", g->gtype);
        return NULL;
    }
    if (why) snprintf(why, whyn, "the part has no active machine group");
    return NULL;
}

int lfn_tools(const McamFile *f, int *idx, int max) {
    /* CLtlPgCmn::InitToolList 0x1801ac400 / FillListCtrl: TlMgr::GetLatheTools - the part's LTL_ID records (lathe.md 8.1); the
       "Show library tools" check (2229) is off for a new op (page +0x22370 = 0) so the library is not read */
    int n = 0;
    for (int i = 0; i < f->nltl && n < max; i++) if (f->ltl[i].n >= LT_BODY_SIZE) idx[n++] = i;
    return n;
}
int lfn_ltl_of_slot(const McamFile *f, int32_t slot) {
    for (int i = 0; i < f->nltl; i++) if (f->ltl[i].n >= LT_BODY_SIZE && r32(f->ltl[i].body) == slot) return i;
    return -1;
}

int lfn_can_start(const McamFile *f, const CamOp *const *camops, int ncamops, char *why, size_t whyn) {
    const struct McGrpLathe *g = lfn_lathe_group(f, why, whyn);
    if (!g) return 0;
    /* InitOperation: with an op of the same opcode before it and config 17028 "Get defaults from previous operation?" = y the new op
       copies that op (list_op_get_prev; its lathe branch NOT DECODED): refused, as stage 1 (cam2d.c gen_lathe_face) */
    for (int i = 0; i < f->nops; i++) if (f->ops[i].opcode == 0x42) {
        snprintf(why, whyn, "Lathe Face: a new Face after an earlier Face (op %d) takes that op's parameters (InitOperation list_op_get_prev, config 17028; lathe branch NOT DECODED)", f->ops[i].id); return 0; }
    for (int i = 0; i < ncamops; i++) if (camops[i] && camops[i]->p.type == CAM_LFACE) {
        snprintf(why, whyn, "Lathe Face: a new Face after an earlier Face takes that op's parameters (InitOperation list_op_get_prev, config 17028; lathe branch NOT DECODED)"); return 0; }
    /* ToolPrmBase::CutParamToolType 0x1801b3450: with machine group +0x11c0 != 0 FacePgPrm::UpdateCutParams 0x1801b6750 takes the
       insert's GTBINSERTCLASS DOC values (this +0x998 block: NOT DECODED) */
    if (g->g11c0 != 0) { snprintf(why, whyn, "Lathe Face: the machine group takes the cut parameters from the tool's insert (group +0x11c0 = %d: GTBINSERTCLASS RghDOC / FinDOC / XOvercut / Retr are NOT DECODED)", g->g11c0); return 0; }
    /* ToolPgPrm::UpdateToolFeedInfo -> ToolPrmBase::GetToolFeedInfo 0x1801b37b0: the tool page's feeds / speeds come from group
       +0x119c's source - 0 the tool, 2 the defaults file op, 3 the group's +0x11a0 / +0x11b0 (ported, L-13), 1 From material
       (LATHETOOLCLASS::CalcFeedSpeed: NOT DECODED) */
    if (g->g119c != 0 && g->g119c != 2 && g->g119c != 3) { snprintf(why, whyn, "Lathe Face: the machine group takes the tool page's feeds and speeds from %s (group +0x119c = %d, ToolPrmBase::GetToolFeedInfo 0x1801b37b0)",
                                  g->g119c == 1 ? "the material (LATHETOOLCLASS::CalcFeedSpeed: NOT DECODED)" : "an unknown source", g->g119c); return 0; }
    uint32_t n = 0;
    if (!cam_lathe_default_record(0x42, mcam_is_metric(f), &n) || n != LFN_REC) {
        snprintf(why, whyn, "Lathe Face: no lathe operation defaults file with a Face op (lathe/Ops/LATHE_%s.mcam-defaults; InitOperation default_ops_get)", mcam_is_metric(f) ? "MM" : "INCH"); return 0; }
    return 1;
}

/* reset_lathe_op_prm_defaults 0x1801d3170, case 0x42: op +0x1218 .. +0x1357 := the block init_lathe_op_prm_defaults 0x1801d2630
   fills at 0x1804b3550 (.bss; disassembly 0x1801d2ab5 .. 0x1801d2b64): zeroed (memset 0x140), +0x00 0, +0x08 -0.1 in / -2.0 mm,
   +0x38 = 1, +0x58 'S', +0x60 0.02 in / 0.5 mm, +0xa0 0.1 in / 2.0 mm, +0xa8 0, +0xd8 = 1, +0xf8 'S', +0x100 0.02 in / 0.5 mm
   (constants 0x18031b720, 0x1802e9f30, 0x18030ae70, 0x1802e2bb0, 0x1802e4b98, 0x1802e2bb8) */
static void face_ee_defaults(uint8_t *op, int metric) {
    uint8_t *b = op + 0x1218;
    memset(b, 0, 0x140);
    wdd(b + 0x08, metric ? -2.0 : -0.1);
    b[0x38] = 1; b[0x58] = 'S'; wdd(b + 0x60, metric ? 0.5 : 0.02);
    wdd(b + 0xa0, metric ? 2.0 : 0.1);
    b[0xd8] = 1; b[0xf8] = 'S'; wdd(b + 0x100, metric ? 0.5 : 0.02);
}

/* CLtlPgCmn::GetToolProperty 0x1801ab7f0 for the picked tool + the sheet's UpdateCtrlData -> FacePgPrm::UpdateCtrlData 0x1801b5d10 */
typedef struct { int tplane; double torg[3]; int spindle_changed; } LfnToolStep;
static int lfn_tool_step(const McamFile *f, const struct McGrpLathe *g, const uint8_t *body, uint32_t bn, const uint8_t *prev, uint8_t *op,
                         LfnToolStep *ts, char *err, size_t errn) {
    LtCtx cx; memset(&cx, 0, sizeof cx);
    cx.metric = mcam_is_metric(f); cx.machine_group = g->id;
    cx.b_axis = 0;                              /* SetLToolAngles = SetOpAngles 0x180159950: TurningOpUsesBAxis 0x1801780f0 (a rotary component 3 /
                                                   9 in the op's combination) - the op's combination is the defaults file's here; a new op is
                                                   taken as not B axis (every stored lathe op has op +0x520..+0x531 = 0) */
    cx.op_flip = op[0x2b3]; cx.op_axis_combo = r32(op + 0x11bc); cx.keep_268 = (uint32_t)r32(prev + 0x268);
    cx.tol = mc_systol_units(cx.metric)->tol;
    if (!lt_make_op_tool(body, bn, &cx, op + 0x2e0, err, errn)) return 0;          /* Make_op_tool(op +0x2e0) */
    /* op +0x520 / +0x528 / +0x530 / +0x531 = pclsLTool +0xbc8 / +0xbd0 / +0xbd8 / +0xbd9: SetOpAngles, not B axis -> 0 */
    memset(op + 0x520, 0, 0x10); op[0x530] = 0; op[0x531] = 0;
    if (r32(body) != r32(prev + 0x264)) {      /* Slot(pclsLTool) != PrvTool +0x264: the axis combination of the tool */
        /* FromMCExtensions of the group's machine: 0 for a lathe group here (set only by InitFromMCExtensionsFile 0x180058790, the
           Mill-Turn import; mill-turn groups are refused, lfn_lathe_group) -> AxisComboFromLatheToolInfo */
        LaCombo c;
        if (!la_axis_combo_for_tool(f, g->id, body, bn, &c, err, errn)) return 0;
        if (r32(op + 0x11bc) != c.id) {
            /* the page's combination before the pick (this +0x2b768 = AxisCombinationFromID(op +0x11bc, 0) at CLToolProp::CLToolProp
               0x1801a62d0 / the previous pick; an id not in the machine: its first combination): ActiveSpindle(new) != ActiveSpindle(old)
               -> CLToolPropBase::ValidateSpindleSpeeds() 0x180266810 (run by the callers: lfn_build_st, lfn_repick_checks) */
            int olds = -1;
            if (!la_combo_spindle(f, g->id, r32(op + 0x11bc), &olds, err, errn)) return 0;
            ts->spindle_changed = c.spindle != olds;
            memcpy(op + 0x11bc, &c.id, 4);
            double cm[9], co[3];
            for (int k = 0; k < 9; k++) cm[k] = rdd(op + 0x6d8 + 8 * k);
            for (int k = 0; k < 3; k++) co[k] = rdd(op + 0x720 + 8 * k);
            int pid = 0; double m[9], org[3];
            int rr = la_default_tplane(f, g->id, f->nops, &c, cm, co, &pid, m, org, err, errn);   /* SetOpDefaultTplane 0x1801af890 */
            if (!rr) return 0;
            if (rr == 1) { ts->tplane = pid; memcpy(ts->torg, org, sizeof org); }
        }
        /* SetOpActiveSpindleFromAxisCombo / SetOpActiveTurretFromAxisCombo / SetOpStreamFromAxisCombo (mcmachinedef) */
        if (c.spindle >= 0) op[0x541] = (uint8_t)c.spindle;
        if (c.turret >= 0) op[0x540] = (uint8_t)c.turret;
        { int32_t st = c.stream; memcpy(op + 0x11b8, &st, 4); }
        if (!c.rot39) { memset(op + 0x520, 0, 0x10); op[0x530] = 0; }   /* no rotary component 3 / 9: op +0x520 / +0x528 / +0x530 = 0 */
    }
    /* FacePgPrm::UpdateCtrlData 0x1801b5d10: OKToUpdate (op +0x531 == 0); ToolOrientChanged 0x1801b5960 (its first call: 1) ->
       SetDefaultCC 0x1801b5460 (op +0x56a) + SetEEVectors 0x1801b56e0 (IntelliSetEEVector on op +0x1218 / +0x1288);
       UpdateCutParams: group +0x11c0 == 0 (lfn_can_start) -> CutParamToolType -1, nothing */
    if (op[0x531] == 0) {
        int ch = lt_orient_changed(prev, body, bn, &cx, 1, err, errn);
        if (ch < 0) return 0;
        if (ch) {
            int idx = op[0x56a], v = idx;
            if (lt_face_default_cc(body, bn, &cx, op[0x1210] != 0, &idx, &v)) op[0x56a] = (uint8_t)v;
            for (int w = 0; w < 2; w++) {
                double vv[2] = { rdd(op + (w ? 0x1288 : 0x1218)), rdd(op + (w ? 0x1288 : 0x1218) + 8) };
                if (!lt_intelli_ee_vector(body, bn, &cx, 0x42, w, vv)) { snprintf(err, errn, "Lathe Face: IntelliSetEEVector refused this tool (custom tool: NOT DECODED)"); return 0; }
                wdd(op + (w ? 0x1288 : 0x1218), vv[0]); wdd(op + (w ? 0x1288 : 0x1218) + 8, vv[1]);
            }
        }
    }
    return 1;
}

/* ---- the tool page's machine checks (L-12, formats/lathe.md 9; mclathe.dll ToolPgBase.cpp / ToolPrmClass.cpp) */
const char LFN_TOOLMOD_TEXT[] = "Current Tool Settings Modified\n\nIn order to conform to your current Machine Def / Control Def parameters, "
    "some of this tool's settings have been changed. Any values that have been changed to meet these settings will be shown in italics.\n";
/* the fields (lathe_newop.h): the page members CLToolPropBase +0x1ec50 rpm (MCLong +0x1e058), +0x1f9b8 max (MCLong +0x1edc0),
   +0x1d068 feed (MCReal +0x1c468), +0x23ea8 finish feed (MCReal +0x232a8), +0x28960 finish rpm (MCLong +0x27d68); their modes
   +0x22364 = (op +0x32d == 0), +0x22360 (MapToDlg: op +0x32e -> 2, else feed >= 0), +0x26e30, +0x28ad0 = (op +0x13b0 != 1) */
static int rpm_get(const uint8_t *op) { return abs(r32(op + 0x328)); }
static void rpm_set(uint8_t *op, int v) { int32_t s = r32(op + 0x328) < 0 ? -v : v; memcpy(op + 0x328, &s, 4); }   /* MapToOp's sign: as Make_op_tool gave it */
static int rpm_mode(const uint8_t *op) { return op[0x32d] == 0; }
static int feed_mode(const uint8_t *op) { double v = rdd(op + 0x310); return op[0x32e] ? 2 : (v > 0 || v == 0); }
static void feed_set(uint8_t *op, double v) { wdd(op + 0x310, feed_mode(op) == 0 ? -fabs(v) : fabs(v)); }   /* MapToOp: mode 0 -> negative */
static int frpm_mode(const uint8_t *op) { return op[0x13b0] != 1; }
typedef struct { const McamFile *f; const struct McGrpLathe *g; uint8_t *op; LfnSpeedSt *st; int changed; char *err; size_t errn; } Chk;
/* ToolPrmBase::GetSpeedMax(true, 1) 0x1801b36f0 / GetSpeedMin(mode) 0x1801b3760: the machine's limits for the op's current
   combination (cur_op +0x11bc); GetSpeedMin(0) = 0 */
static int speed_lim(Chk *k, long *mn, long *mx) { return la_lathe_rpm_limits(k->f, k->g->id, r32(k->op + 0x11bc), mn, mx, k->err, k->errn); }
static void ital(Chk *k, unsigned bit, int on) { if (on) k->st->italic |= bit; else k->st->italic &= ~bit; }
/* ToolPrmBase::ValidateSpindleSpeed 0x1801b7a10: +0x34 = MAX; value < GetSpeedMin(mode) -> the min; else value > MAX in mode 1 -> MAX;
   a change -> the italic font (+0x50) and *changed; none with RESET -> the plain font (+0x40) when it was italic */
static int vsp(Chk *k, int *v, int mode, long max, int reset, unsigned bit) {
    long mn = 0, mx = 0;
    if (!speed_lim(k, &mn, &mx)) return 0;
    k->st->prm34 = max;
    if (mode == 0) mn = 0;
    if (*v < mn) *v = (int)mn;
    else if (*v <= max || mode != 1) { if (reset) ital(k, bit, 0); return 1; }
    else *v = (int)max;
    ital(k, bit, 1); k->changed = 1;
    return 1;
}
/* ToolPrmBase::ValidateFeedrate 0x1801b77a0 (GetFeedMin / GetFeedMax(mode)): below the min -> the min, above the max -> the max
   (italic, *changed); inside -> the plain font */
static int vfr(Chk *k, double *v, int mode, unsigned bit) {
    double lim[3][2];
    if (!la_machine_feed_limits(k->f, k->g->id, mcam_is_metric(k->f), lim, k->err, k->errn)) return 0;
    if (mode < 0 || mode > 2) mode = 2;
    if (lim[mode][0] < *v || lim[mode][0] == *v) {
        if (*v <= lim[mode][1]) { ital(k, bit, 0); return 1; }
        *v = lim[mode][1];
    } else *v = lim[mode][0];
    ital(k, bit, 1); k->changed = 1;
    return 1;
}
/* CLToolPropBase::ValidateSpindleSpeeds(force, reset, &changed) 0x180266830: FORCE or GetSpeedMax < max -> max = GetSpeedMax (italic);
   else with RESET the plain font. Then (op 0x42: not 0xe0 / 0xe1) ValidateSpindleSpeed of the rpm (mode +0x22364) and, case 0x42,
   of the finish rpm (mode +0x28ad0), both against the max. MAIN_RPM 0: the rpm field is about to be replaced (the tool pick's
   UpdateToolFeedInfo) - only its +0x34 effect is kept */
static int vss(Chk *k, int force, int reset, int main_rpm) {
    long mn = 0, mx = 0;
    if (!speed_lim(k, &mn, &mx)) return 0;
    int32_t max = r32(k->op + 0x548);
    if (force || mx < max) { max = (int32_t)mx; memcpy(k->op + 0x548, &max, 4); ital(k, LFS_MAX, 1); k->changed = 1; }
    else if (reset) ital(k, LFS_MAX, 0);
    if (main_rpm) { int v = rpm_get(k->op); if (!vsp(k, &v, rpm_mode(k->op), max, reset, LFS_RPM)) return 0; rpm_set(k->op, v); }
    else k->st->prm34 = max;
    int fv = r32(k->op + 0x13ac);
    if (!vsp(k, &fv, frpm_mode(k->op), max, reset, LFS_FRPM)) return 0;
    memcpy(k->op + 0x13ac, &fv, 4);
    return 1;
}
/* CLToolPropBase::ValidateFeedrates 0x1802665e0: the feed (mode +0x22360) and, case 0x42, the finish feed (mode +0x26e30) */
static int vfeeds(Chk *k) {
    double v = fabs(rdd(k->op + 0x310));
    if (!vfr(k, &v, feed_mode(k->op), LFS_FEED)) return 0;
    feed_set(k->op, v);
    double w = rdd(k->op + 0x13a0);
    if (!vfr(k, &w, k->st->ffeed_mode, LFS_FFEED)) return 0;
    wdd(k->op + 0x13a0, w);
    return 1;
}
/* ToolPgPrm::UpdateToolFeedInfo 0x1801b76b0 (vfunc +0x10, run by ToolPgPrm::UpdateCtrlData 0x1801b6320 from the sheet's UpdateCtrlData
   after every tool pick; page +0x2929 "edited" is 0 here): OKToUpdate (op +0x531 == 0), ToolPrmBase::GetToolFeedInfo 0x1801b37b0
   by the machine group's feed calculation (+0x119c, formats/grpprops_tabs.md; L-13):
     0 From tool: rpm = SpdlSpeed(IsMetric), mode = (CSS == 0), feed = GetFastFR, mode = (FR != 'R') - the values Make_op_tool put
       in the op;
     2 From defaults: set_op_prm_defaults + default_ops_get(opcode, op, group) = the defaults file's op (InitOperation's source,
       DFLT_OP): rpm = its +0x328 as stored, mode = (its +0x32d == 0), feed = |its +0x310|, mode = (its +0x310 >= 0);
     3 User defined: feed = group +0x11a0, rpm = (long) group +0x11b0, mode RPM; the feed's mode is not written (the field keeps
       KEEP_FMODE, page +0x22360);
     1 From material (LATHETOOLCLASS::CalcFeedSpeed): see lfn_can_start.
   then ValidateSpindleSpeed(rpm, mode, +0x34, reset) and ValidateFeedrate(feed) */
static int tool_feed_info(Chk *k, const uint8_t *dflt_op, int keep_fmode) {
    if (k->op[0x531] != 0) return 1;
    int v; double w;
    if (k->g->g119c == 2) {
        v = r32(dflt_op + 0x328); k->op[0x32d] = dflt_op[0x32d];
        double d = rdd(dflt_op + 0x310); w = fabs(d);
        k->op[0x32e] = 0; wdd(k->op + 0x310, (d > 0 || d == 0) ? w : -w);
    } else if (k->g->g119c == 3) {
        v = (int)k->g->g11b0; k->op[0x32d] = 0;
        w = k->g->g11a0; k->op[0x32e] = keep_fmode == 2; wdd(k->op + 0x310, keep_fmode == 0 ? -fabs(w) : fabs(w));
    } else if (k->g->g119c == 0) {
        v = rpm_get(k->op); w = fabs(rdd(k->op + 0x310));
    } else { snprintf(k->err, k->errn, "Lathe Face: feed calculation %d (group +0x119c) is NOT DECODED", k->g->g119c); return 0; }
    if (!vsp(k, &v, rpm_mode(k->op), k->st->prm34, 1, LFS_RPM)) return 0;
    rpm_set(k->op, v);
    if (!vfr(k, &w, feed_mode(k->op), LFS_FEED)) return 0;
    feed_set(k->op, w);
    return 1;
}
int lfn_machine_checks(const McamFile *f, uint8_t *rec, LfnSpeedSt *st, int *changed, char *err, size_t errn) {
    const struct McGrpLathe *g = lfn_lathe_group(f, err, errn);
    if (changed) *changed = 0;
    if (!g) return 0;
    /* RunMachineDefChecks(true) 0x180265a60: (page +0x2b790 0: not inside OnInitDialog) MapToOp (the record is the page here),
       ValidateSpindleSpeeds(false, true), ValidateFeedrates; a change -> UpdateData(FALSE) + CToolModWarning (unless "Turn off
       warning for session" DAT_1804b641d) */
    Chk k = { f, g, rec + LFN_OP, st, 0, err, errn };
    if (!vss(&k, 0, 1, 1) || !vfeeds(&k)) return 0;
    if (changed) *changed = k.changed;
    return 1;
}
int lfn_repick_checks(const McamFile *f, const uint8_t *old, uint8_t *rec, LfnSpeedSt *st, char *err, size_t errn) {
    const struct McGrpLathe *g = lfn_lathe_group(f, err, errn);
    if (!g) return 0;
    uint8_t *op = rec + LFN_OP; const uint8_t *oop = old + LFN_OP;
    /* the page members the pick does not write keep the dialog's values: max, finish feed, finish rpm (and their radios) */
    memcpy(op + 0x548, oop + 0x548, 4); memcpy(op + 0x13a0, oop + 0x13a0, 8); memcpy(op + 0x13ac, oop + 0x13ac, 4);
    op[0x13a8] = oop[0x13a8]; op[0x13b0] = oop[0x13b0];
    /* ToolPgPrm::UpdateCtrlData 0x1801b6320: UpdateToolFeedInfo runs unless the lock (+0x2928) is on and the speeds were edited
       (+0x2929): then the rpm / feed fields keep the dialog's values (MapToOp: the rpm with the new tool's sign rules) */
    int keep = st->lock && st->edited;
    if (keep) {
        int32_t v = abs(r32(oop + 0x328)); if (r32(op + 0x328) < 0) v = -v; memcpy(op + 0x328, &v, 4);
        op[0x32d] = oop[0x32d]; op[0x32e] = oop[0x32e]; memcpy(op + 0x310, oop + 0x310, 8);
    }
    Chk k = { f, g, op, st, 0, err, errn };
    /* CLtlPgCmn::GetToolProperty 0x1801ab7f0: a new combination id whose spindle differs from the page's combination ->
       ValidateSpindleSpeeds() = (true, true) on the fields (the rpm field: the previous tool's, replaced below unless kept) */
    if (r32(op + 0x11bc) != r32(oop + 0x11bc)) {
        int olds = -1, news = -1;
        if (!la_combo_spindle(f, g->id, r32(oop + 0x11bc), &olds, err, errn) || !la_combo_spindle(f, g->id, r32(op + 0x11bc), &news, err, errn)) return 0;
        if (news != olds && !vss(&k, 1, 1, keep)) return 0;
    }
    /* the sheet's UpdateCtrlData -> UpdateToolFeedInfo (the new tool's rpm / feed), then OnClickToolbmpsLb 0x180263460:
       UpdateData(FALSE), SetFeedrateFromSS (thread ops only), ValidateFeedrates (no warning) */
    uint32_t dn = 0; const uint8_t *dflt = cam_lathe_default_record(0x42, mcam_is_metric(f), &dn);
    if (!dflt || dn != LFN_REC) { snprintf(err, errn, "Lathe Face: no lathe operation defaults file with a Face op (default_ops_get)"); return 0; }
    if ((!keep && !tool_feed_info(&k, dflt + LFN_OP, feed_mode(oop))) || !vfeeds(&k)) return 0;
    return 1;
}

int lfn_build(const McamFile *f, int ltl, const LfnPlanes *active, const CamParams *p, uint8_t **out, int *tplane, char *err, size_t errn) {
    LfnSpeedSt st;
    return lfn_build_st(f, ltl, active, p, out, tplane, &st, 1, err, errn);
}
int lfn_build_st(const McamFile *f, int ltl, const LfnPlanes *active, const CamParams *p, uint8_t **out, int *tplane, LfnSpeedSt *st,
                 int checks, char *err, size_t errn) {
    *out = NULL; if (tplane) *tplane = 0;
    memset(st, 0, sizeof *st);
    {   /* ToolPgPrm::ToolPgPrm 0x1801b2320: +0x2928 = EnableFeedRateLock, +0x2929 = (newOp == 0) = 0 */
        char v[32]; const char *t = getenv("MCAMUI_TEST_CFG17027");
        if (!t) t = mcam_config_value(mcam_is_metric(f), 17027, v, sizeof v);
        st->lock = (t && t[0]) ? (t[0] == 'y' || t[0] == 'Y' || atoi(t) != 0) : 1;
    }
    const struct McGrpLathe *g = lfn_lathe_group(f, err, errn);
    if (!g) return 0;
    if (g->g119c != 0 && g->g119c != 2 && g->g119c != 3) { snprintf(err, errn, "Lathe Face: the machine group takes the tool page's feeds and speeds from %s (group +0x119c = %d, ToolPrmBase::GetToolFeedInfo 0x1801b37b0)", g->g119c == 1 ? "the material (LATHETOOLCLASS::CalcFeedSpeed: NOT DECODED)" : "an unknown source", g->g119c); return 0; }
    int metric = mcam_is_metric(f);
    uint32_t n = 0; const uint8_t *src = cam_lathe_default_record(0x42, metric, &n);
    if (!src || n != LFN_REC) { snprintf(err, errn, "Lathe Face: no lathe operation defaults file with a Face op (InitOperation default_ops_get)"); return 0; }
    uint8_t *r = malloc(LFN_REC); memcpy(r, src, LFN_REC);
    uint8_t *op = r + LFN_OP;
    /* ---- InitOperation (decomp/mccore_ops.c): the defaults op (default_ops_get(0x42, op, group)); op +6 the comment cleared (L-7) */
    memset(op + 6, 0, 0x78);
    /* the defaults file's own lathe stock copies (op +0xb40..+0xb50, +0xb84: LBDRY ids of the defaults file) name nothing in this
       part: a new op has none until its stock is updated (LATHEPROJECTCLASS::UpdateOpStock 0x180112400, NOT DECODED here) */
    memset(op + 0xb40, 0, 0x14); memset(op + 0xb84, 0, 4);
    /* lathe: reset_lathe_op_prm_defaults 0x1801d3170 */
    memcpy(op + 0xb70, g->g1698, 8); memcpy(op + 0xb78, g->g16a0, 8);
    op[0xb3c] = 1; op[0xb3a] = 1;
    face_ee_defaults(op, metric);
    /* op_is_no_refpt(0x42) (mccore 0x1800c6970) is false: the reference points stay. InitOperationViewsToActivePlanes
       (mastercam.dll 0x1802b8830): Cplane +0x6d0, Tplane +0x570, WCS +0x620 := the active planes (Plane::Convert, work offset -1) */
    CamParams pl; memset(&pl, 0, sizeof pl);
    if (active) { pl.tplane = active->tplane; pl.cplane = active->cplane; pl.wcs = active->wcs;
                  memcpy(pl.torg, active->torg, sizeof pl.torg); memcpy(pl.corg, active->corg, sizeof pl.corg); memcpy(pl.worg, active->worg, sizeof pl.worg); }
    pl.wofs = pl.cwofs = pl.wwofs = -1;
    cam_planes_to_record(r, &pl);
    /* AddWCSOriginToPt3D(op +0x870): the home point + the WCS origin */
    for (int k = 0; k < 3; k++) wdd(op + 0x870 + 8 * k, rdd(op + 0x870 + 8 * k) + pl.worg[k]);

    /* ---- the tool page opens: CLToolProp::CLToolProp 0x1801a62d0 -> ToolPgPrm::ToolPgPrm (ToolPrmBase ctor: +0x34 =
       MaxLatheSpindleRPM(cur_op, NULL), the op's combination op +0x11bc = the defaults file's); CLToolPropBase::OnInitDialog
       0x180263a50 -> MapToDlg (CLToolProp::MapToDlg 0x1801a6dd0 = CLToolPropBase::MapToDlg 0x180261ba0): the fields from the op,
       the feed clamped to [GetFeedMin, GetFeedMax] of its mode, the Face finish feed likewise (mode +0x26e30 = (op +0x13a8 != 'R')),
       max = op +0x548, 0 or above GetSpeedMax -> GetSpeedMax. Its closing RunMachineDefChecks is skipped (page +0x2b790 is still 1
       inside OnInitDialog). */
    st->ffeed_mode = op[0x13a8] != 'R';
    Chk ck = { f, g, op, st, 0, err, errn };
    if (checks) {
        long mn = 0, mx = 0; double lim[3][2];
        if (!speed_lim(&ck, &mn, &mx) || !la_machine_feed_limits(f, g->id, metric, lim, err, errn)) { free(r); return 0; }
        st->prm34 = mx;
        int fm = feed_mode(op); double v = fabs(rdd(op + 0x310));
        if (v < lim[fm][0]) v = lim[fm][0];
        if (lim[fm][1] < v) v = lim[fm][1];
        feed_set(op, v);
        double w = rdd(op + 0x13a0);
        if (w < lim[st->ffeed_mode][0]) w = lim[st->ffeed_mode][0];
        if (lim[st->ffeed_mode][1] < w) w = lim[st->ffeed_mode][1];
        wdd(op + 0x13a0, w);
        int32_t max = r32(op + 0x548);
        if (max == 0 || mx < max) { max = (int32_t)mx; memcpy(op + 0x548, &max, 4); }
    }

    /* ---- the tool page (CLToolPropBase::OnInitDialog 0x180263a50 -> CLtlPgCmn::InitLTool 0x1801abdc0 -> GetToolProperty
       0x1801ab7f0 for the picked row) */
    if (ltl >= 0) {
        if (ltl >= f->nltl || f->ltl[ltl].n < LT_BODY_SIZE) { free(r); snprintf(err, errn, "Lathe Face: lathe tool %d is not in the part", ltl); return 0; }
        const uint8_t *body = f->ltl[ltl].body; uint32_t bn = f->ltl[ltl].n;
        uint8_t prev[LT_OPTOOL_SIZE]; memcpy(prev, op + 0x2e0, sizeof prev);        /* ToolPrmBase::SetPrvTool 0x1801b5780 */
        int keep_fmode = feed_mode(op);                                              /* the feed field's mode from MapToDlg (+0x22360) */
        LfnToolStep ts; memset(&ts, 0, sizeof ts);
        if (!lfn_tool_step(f, g, body, bn, prev, op, &ts, err, errn)) { free(r); return 0; }
        if (tplane) *tplane = ts.tplane;
        if (ts.tplane > 0) {                                                         /* SetOpDefaultTplane -> LatheTplane::GetOpTplane */
            pl.tplane = ts.tplane; memcpy(pl.torg, ts.torg, sizeof pl.torg);
            cam_planes_to_record(r, &pl);
        }
        st->raw_rpm = rpm_get(op); st->raw_css = op[0x32d]; st->raw_feed = rdd(op + 0x310);
        /* the spindle check of the pick (ValidateSpindleSpeeds(true, true), the rpm field still the defaults file's: replaced next),
           then the sheet's UpdateCtrlData -> ToolPgPrm::UpdateToolFeedInfo (CLTpSheet::InitTabPages 0x1801e4170 set +0xb1fc, so
           GetToolProperty's CLTpSheet::UpdateCtrlData 0x1801e61b0 runs the pages' UpdateCtrlData) */
        if (checks && ((ts.spindle_changed && !vss(&ck, 1, 1, 0)) || !tool_feed_info(&ck, src + LFN_OP, keep_fmode))) { free(r); return 0; }
    }
    /* ---- the op as made here: its planes (as they were when it was made, p) and its dialog values (record patches) */
    if (p) {
        CamParams q = pl;
        q.tplane = p->tplane; q.cplane = p->cplane; q.wcs = p->wcs;
        memcpy(q.torg, p->torg, sizeof q.torg); memcpy(q.corg, p->corg, sizeof q.corg); memcpy(q.worg, p->worg, sizeof q.worg);
        q.wofs_manual = p->wofs_manual; q.wofs = p->wofs; q.cwofs = p->cwofs; q.wwofs = p->wwofs;
        cam_planes_to_record(r, &q);
        for (int k = 0; k < p->nrpatch; k++) {
            int o = p->rpatch[k].off, m = p->rpatch[k].n;
            if (!p->rpatch[k].height && o + m <= LFN_REC) memcpy(r + o, p->rpatch[k].v, (size_t)m);
        }
    }
    *out = r;
    return 1;
}

int lfn_record(const McamFile *f, const CamParams *p, uint8_t **rec, char *err, size_t errn) {
    int ltl = lfn_ltl_of_slot(f, p->lf_slot);
    if (ltl < 0) { *rec = NULL; snprintf(err, errn, "Lathe Face: its lathe tool (slot %d) is not in the part", p->lf_slot); return 0; }
    return lfn_build(f, ltl, NULL, p, rec, NULL, err, errn);
}

int lfn_finish_z(const McamFile *f, const uint8_t *rec, double *z, double *lo, double *hi, char *err, size_t errn) {
    double tol = mc_systol_units(mcam_is_metric(f))->tol;
    LsBdry st; int src = 0;
    if (!lathe_op_stock(f, 0, tol, &st, &src, err, errn)) return 0;   /* lathe_toolpaths_start: GetPrvOpBdrys (a new op: appended) */
    LfIn in; memset(&in, 0, sizeof in);
    in.rec = rec; in.rn = LFN_REC; in.tol = tol; in.stock = &st;
    int ok = lathe_face_z_init(&in, z, lo, hi);
    ls_free(&st);
    if (!ok) snprintf(err, errn, "Lathe Face: no stock polygon (GetOpStockBoundary 0xff): \"Use stock\" is greyed");
    return ok;
}
