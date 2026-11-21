/* cui_ops.c - the operations this program makes (core_ui.h "operations"): the pending op of the toolpath dialog
   (open_params / open_op_params / generate_pending / pending_free), the defaults op's heights, regenerating, deleting,
   the Toolpaths manager's clipboard / undelete / reverse / feeds-speeds actions, the post, and the model of the Toolpaths
   manager's right-click menu (OPMANRES.DLL menu 780). Moved from viewer/mcamui.c unchanged (formats/qt_port_plan.md phase
   6); names, comments and decomp citations kept. What only a front end can do (its Tool Manager, the Edit Common
   Parameters / Renumber tools / report / rename dialogs, the tree's open flags) goes through cui_hooks.op_menu. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "paths.h"
#include "toollib.h"
#include "feedspeed.h"
#include "nc_post.h"
#include "mp/mp_post.h"
#include "nci_write.h"
#include "bnci_update.h"
#include "nci_reverse.h"
#include "cam_postvals.h"
#include "cam_fileop.h"
#include "mcam_saveops.h"
#include "toolwiz.h"
#include "core_ui.h"
#include "../lathe_newop.h"   /* lfn_lathe_group: the Lathe toolpaths popup */
#include "cui_opfields.h"

#define CP(dst, src) snprintf(dst, sizeof dst, "%s", src)
#define message cui_message

CamOp pending;
int tool_choice;                                /* index into F.tools, or F.ntools = custom */
int edit_camop = -1;                            /* camops index being edited in the params dialog, -1 = new op */
char tp_grp_name[260] = "Toolpath Group-1", mach_grp_name[260] = "Machine Group-1";
int cm_group, cm_ctrl;                          /* right-clicked group: 0 none, 1 toolpath group, 2 machine group; Ctrl held */

/* ------------------------------------------------------------------------------------------------ Toolpaths manager menu */
/* right-click menu of the Toolpaths manager, laid out as Mastercam's (Operations Manager popup, OPMANRES.DLL menu 780).
   Entries this program cannot do are shown greyed, as Mastercam greys unavailable commands. */
CamOp *op_clip, *op_deleted; int op_deleted_at;   /* (unused since 2026-10-26: the buffers below) */
int op_clip_cut;
int cui_script_yes = 1, cui_script_choice = 0;
int op_del_prompt = 1;                            /* Display Options (opmanres 821) 4061 "Prompt for toolpath deletion" = COpMgrTree +0x75a4:
                                                     1 at start (FUN_180343d50 writes 1 at its block +0x64 = this+0x75a4, decomp/dirty/
                                                     mc_dispopt.c; block +0x74 = +0x75b4); its persistence is NOT DECODED */
/* CM_MILLPOP: the Mill popup (usable on a mill machine); CM_ALIAS: a popup showing the items of item atoi(arg) (Router
   toolpaths = the mill list, usable on a router); CM_HIDE: removed from the menu, as Mastercam deletes Stock Model /
   Collision check when they cannot run; CM_CTRL: only while Ctrl is held (Solid Drill); CM_NEEDOPS / CM_NEEDSEL: not
   available here, greyed by Mastercam's rules anyway (ops in the tree / an op selected) */
#define S_(d) { d, "", CM_SEP, NULL }
const CMItem cm_items[] = {
    { 0, "Mill toolpaths", CM_MILLPOP, NULL },
      { 1, "New", CM_NA, NULL }, S_(1),
      { 1, "Contour", CM_OP, "Contour" }, { 1, "Drill", CM_OP, "Drill" }, { 1, "Pocket", CM_OP, "Pocket" }, { 1, "Face", CM_OP, "Face" },
      { 1, "2D High Speed", CM_NA, NULL },
        { 2, "Dynamic Mill", CM_OP, "Dynamic Mill" }, { 2, "Dynamic Contour", CM_OP, "Dynamic Contour" }, S_(2),
        { 2, "Area Mill", CM_OP, "Area Mill" }, { 2, "Peel Mill", CM_OP, "Peel Mill" }, { 2, "Blend Mill", CM_NA, NULL },
      { 1, "Engrave", CM_OP, "Engrave" }, S_(1),      /* Engrave 3628: the engrave.dll module (opcode 416), CAM_ENGRAVE (viewer/engrave.c) */
      { 1, "Surface rough", CM_NA, NULL },
        { 2, "Parallel", CM_OP, "Surface Rough Parallel" }, { 2, "Radial", 0, 0 }, { 2, "Project", CM_OP, "Surface Rough Project" }, { 2, "Flowline", 0, 0 }, { 2, "Contour", 0, 0 }, { 2, "Restmill", 0, 0 }, { 2, "Pocket", CM_OP, "Surface Rough Pocket" }, { 2, "Plunge", 0, 0 },
      { 1, "Surface finish", CM_NA, NULL },
        { 2, "Parallel", CM_OP, "Surface Finish Parallel" }, { 2, "Parallel steep", 0, 0 }, { 2, "Radial", CM_OP, "Surface Finish Radial" }, { 2, "Project", CM_OP, "Surface Finish Project" }, { 2, "Flowline", 0, 0 }, { 2, "Contour", CM_OP, "Surface Finish Contour" },
        /* Pencil = classic op 39 (CAM_SFPENCIL): ported (cam3d_pencil.c gen_sfpencil, cam_cli sfpencil) but kept greyed by the user's decision
           N13-19 until the Pencil Linker 0x1442 is decoded (its pass order / links are ours, formats/surf_classic3.md 10) */
        { 2, "Shallow", 0, 0 }, { 2, "Pencil", 0, 0 }, { 2, "Leftover", 0, 0 }, { 2, "Scallop", 0, 0 }, { 2, "Blend", CM_OP, "Surface Finish Blend" },
      { 1, "Surface high speed toolpath", CM_NA, NULL },
        { 2, "Dynamic OptiRough", CM_OP, "OptiRough" }, { 2, "Area Roughing", CM_OP, "Area Roughing" }, S_(2), { 2, "Waterline", CM_OP, "Waterline" }, { 2, "Scallop", CM_OP, "Scallop" }, { 2, "Horizontal Area", CM_OP, "Horizontal Area" },
        { 2, "Raster", CM_OP, "Raster" }, { 2, "Pencil", CM_OP, "Pencil" }, { 2, "Spiral", CM_OP, "Spiral" }, { 2, "Radial", CM_OP, "Radial" }, { 2, "Hybrid", 0, 0 }, { 2, "Project", CM_OP, "Project" },
      { 1, "Multiaxis", CM_NA, NULL },
        { 2, "Pattern", 0, 0 },
          { 3, "Curve", 0, 0 }, { 3, "Swarf milling", 0, 0 }, { 3, "Parallel", 0, 0 }, { 3, "Drill", 0, 0 }, { 3, "Along curve", 0, 0 }, { 3, "Morph", 0, 0 },
          { 3, "Flow", 0, 0 }, { 3, "Multi surface", 0, 0 }, { 3, "Port", 0, 0 }, { 3, "Triangular mesh", 0, 0 },
        { 2, "Application", 0, 0 },
          { 3, "Swarf", 0, 0 }, { 3, "Rotary", 0, 0 }, { 3, "Project curve", 0, 0 }, { 3, "Roughing", 0, 0 }, { 3, "Circle mill", 0, 0 }, { 3, "Port expert", 0, 0 },
          { 3, "Blade expert", 0, 0 }, { 3, "Rotary advanced", 0, 0 },
      { 1, "Multiaxis Link...", 0, 0 }, S_(1),
      { 1, "FBM Drill", 0, 0 }, { 1, "FBM Mill", 0, 0 }, S_(1),
      { 1, "Stock Model", CM_HIDE, 0 }, { 1, "View Stock Model", CM_HIDE, 0 }, { 1, "Stock Model Export to STL", CM_HIDE, 0 }, { 1, "Stock Model Convert to PMesh", CM_HIDE, 0 }, { 1, "", CM_HIDE, 0 },
      { 1, "Circle toolpaths", CM_NA, NULL },
        { 2, "Circle mill", CM_OP, "Circle Mill" }, { 2, "Thread mill", CM_OP, "Thread Mill" }, { 2, "Auto drill", 0, 0 }, { 2, "Drill start holes", 0, 0 },
        { 2, "Slot mill", CM_OP, "Slot Mill" }, { 2, "Helix bore", CM_OP, "Helix Bore" },
      { 1, "Transform", 0, 0 }, { 1, "Nest", 0, 0 }, { 1, "Manual entry", 0, 0 }, { 1, "Point", 0, 0 }, { 1, "Trim", 0, 0 },
      { 1, "Wire frame", CM_NA, NULL },
        { 2, "Ruled", 0, 0 }, { 2, "Revolved", 0, 0 }, { 2, "Swept 2D", 0, 0 }, { 2, "Swept 3D", 0, 0 }, { 2, "Coons patch", 0, 0 }, { 2, "Lofted", 0, 0 },
      S_(1), { 1, "Probe", 0, 0 },
    /* Lathe toolpaths (MENU 780 position 1, opmanres; greyed unless the arrow's machine group is a lathe, OnRclick): Face 3680 ->
       FT lathe_face (decomp/opman_create_map.tsv) = the new Lathe Face (cui_lface.c, formats/lathe.md 8); the other lathe ops are
       not made here (greyed) */
    { 0, "Lathe toolpaths", CM_NA, NULL },
      { 1, "Rough", 0, 0 }, { 1, "Finish", 0, 0 }, { 1, "Thread", 0, 0 }, { 1, "Groove", 0, 0 }, { 1, "Plunge turn", 0, 0 }, { 1, "Contour rough", 0, 0 },
      { 1, "Dynamic rough", 0, 0 }, { 1, "Face", CM_LFACE, NULL }, { 1, "Cutoff", 0, 0 }, { 1, "Drill", 0, 0 }, { 1, "Point", 0, 0 }, { 1, "Transform", 0, 0 },
      { 1, "Manual entry", 0, 0 },
      { 1, "Quick", CM_NA, NULL }, { 2, "Quick rough", 0, 0 }, { 2, "Quick finish", 0, 0 }, { 2, "Quick groove", 0, 0 },
      { 1, "Canned", CM_NA, NULL }, { 2, "Canned rough", 0, 0 }, { 2, "Canned finish", 0, 0 }, { 2, "Canned groove", 0, 0 }, { 2, "Canned pattern", 0, 0 },
      { 1, "Misc Ops", CM_NA, NULL }, { 2, "Pickoff/Pull/Cutoff", 0, 0 }, { 2, "Stock transfer", 0, 0 }, { 2, "Stock flip", 0, 0 }, { 2, "Stock advance", 0, 0 },
        { 2, "Chuck", 0, 0 }, { 2, "Tailstock", 0, 0 }, { 2, "Steady rest", 0, 0 },
      { 1, "C Axis", CM_NA, NULL }, { 2, "Face contour", 0, 0 }, { 2, "Cross contour", 0, 0 }, { 2, "C-axis contour", 0, 0 }, { 2, "Face drill", 0, 0 },
        { 2, "Cross drill", 0, 0 }, { 2, "C-axis drill", 0, 0 },
      S_(1), { 1, "Stock Model", CM_HIDE, 0 }, { 1, "View Stock Model", CM_HIDE, 0 }, { 1, "Stock Model Export to STL", CM_HIDE, 0 }, { 1, "Stock Model Convert to PMesh", CM_HIDE, 0 },
    { 0, "Wire toolpaths", CM_NA, NULL }, { 1, "(wire toolpaths are not made here)", CM_NA, NULL },
    { 0, "Router toolpaths", CM_ALIAS, "0" },
    { 0, "Edit selected operations", CM_NA, NULL },
      { 1, "Edit common parameters...", CM_EDIT, NULL }, { 1, "Change NC file name...", 0, 0 }, { 1, "Change Program #...", 0, 0 }, S_(1),
      { 1, "Renumber tools...", CM_RENUMTOOLS, 0 }, { 1, "Renumber work offsets...", 0, 0 }, { 1, "Reverse toolpath", CM_REVERSE, NULL }, { 1, "Recalculate feeds/speeds", CM_RECALC, 0 },   /* Reverse = ReversePath(1) -> bnci_reverse (nci_reverse.c, formats/reverse_toolpath.md); Recalculate = list_op_recalc_sf (formats/feeds_speeds.md) */
    S_(0),
    { 0, "Groups", CM_NA, NULL },
      { 1, "New Machine group", CM_NA, NULL }, { 2, "Mill", 0, 0 }, { 2, "Lathe", 0, 0 }, { 2, "Router", 0, 0 }, { 2, "Wire", 0, 0 },
      { 1, "New Toolpath group", CM_GRP_NEW, 0 }, S_(1), { 1, "Rename", CM_GRP_RENAME, 0 }, { 1, "Delete", CM_GRP_DELETE, 0 }, { 1, "Delete all empty", CM_GRP_DELEMPTY, 0 },
    S_(0),
    { 0, "Cut", CM_CUT, NULL }, { 0, "Copy", CM_COPY, NULL }, { 0, "Paste", CM_PASTE, NULL }, { 0, "Delete", CM_DELETE, NULL }, { 0, "Undelete", CM_UNDELETE, NULL }, { 0, "Undo", CM_UNDO, NULL },
    S_(0),
    { 0, "Expand", CM_EXPAND, NULL }, { 0, "Collapse", CM_COLLAPSE, NULL }, { 0, "Doc file...", CM_DOCFILE, 0 }, { 0, "Select...", CM_NEEDOPS, 0 },
    { 0, "Sort", CM_NA, NULL }, { 1, "Sort...", CM_NEEDOPS, 0 }, { 1, "Unsort", 0, 0 },
    S_(0),
    { 0, "Import...", 0, 0 }, { 0, "Export...", CM_NEEDOPS, 0 }, { 0, "Batch...", 0, 0 },
    S_(0),
    { 0, "Display options...", 0, 0 }, { 0, "Collision check...", CM_HIDE, 0 }, { 0, "Setup sheet...", CM_NEEDSEL, 0 }, { 0, "Tool list...", CM_TOOLLIST, NULL },
    S_(0), { 0, "Toolpath Editor", CM_NA, NULL }, { 0, "Solid Drill", CM_CTRL, NULL },
};
#undef S_
const int cm_nitems = (int)(sizeof cm_items / sizeof cm_items[0]);

int cm_kids_of(int i) { return cm_items[i].act == CM_ALIAS ? atoi(cm_items[i].arg) : i; }   /* whose children a popup shows */
int cm_shown(int i) { return cm_items[i].act != CM_HIDE && (cm_items[i].act != CM_CTRL || cm_ctrl); }
int cm_has_kids(int i) { int k = cm_kids_of(i); return k + 1 < CM_N && cm_items[k + 1].depth > cm_items[k].depth; }
int cm_router(void) { return !strcasecmp(machine, "Router"); }
/* a command's enable state now (as Mastercam's update handlers: selection / clipboard / history) */
/* Mastercam's rules (OnRclick, ../formats/opmanager_menu.md): ops < 1 greys Expand / Collapse / Doc file / Sort / Select /
   Export; no selection greys the Edit popup, Cut, Copy, Delete, Setup sheet, Tool list; Paste needs a cut or copy buffer;
   group commands need a right-clicked group */
int cm_leaf_ok(int act) {
    int ops = loaded ? F.nops : 0;
    switch (act) {
    case CM_NA: case CM_SEP: case CM_HIDE: case CM_NEEDOPS: case CM_NEEDSEL: case CM_CTRL: case CM_GRP_NEW: return 0;
    case CM_EDIT: return op_nselected() > 0;
    case CM_CUT: case CM_COPY: case CM_DELETE: return op_nselected() > 0;   /* OnRclick: greyed without a selection (formats/align_ops.md 1.1) */
    case CM_RENUMTOOLS: return op_nselected() > 0;
    case CM_RECALC: return op_nselected() > 0;
    case CM_REVERSE: return op_nselected() > 0;                  /* the Edit selected operations popup: sel >= 1 */
    case CM_TOOLLIST: return ops > 0 && op_nselected() > 0;
    case CM_PASTE: return op_paste_ready();                       /* greyed when both buffers are empty */
    case CM_UNDELETE: return 1;                                   /* 3567: never greyed (OnRclick has no EnableMenuItem for 0xdef) */
    case CM_UNDO: return nundo > 0;
    case CM_EXPAND: case CM_COLLAPSE: case CM_DOCFILE: return ops > 0;
    case CM_GRP_RENAME: case CM_GRP_DELETE: return cm_group != 0;
    case CM_GRP_DELEMPTY: return loaded;
    case CM_OP: return 1;
    case CM_LFACE: return loaded && lfn_lathe_group(&F, NULL, 0) != NULL;   /* OnRclick: the Lathe popup only on a lathe machine group (type 1) */
    default: return 1;
    }
}
/* an item is usable if it has an action, or is a submenu with a usable item somewhere below */
int cm_usable(int i) {
    if (cm_items[i].act == CM_SEP) return 0;
    if (cm_items[i].act == CM_MILLPOP && cm_router()) return 0;               /* machine type greys the other popups */
    if (cm_items[i].act == CM_ALIAS && !cm_router()) return 0;
    if (!cm_has_kids(i)) return cm_leaf_ok(cm_items[i].act);
    int t = cm_kids_of(i);
    for (int k = t + 1; k < CM_N && cm_items[k].depth > cm_items[t].depth; k++) if (cm_shown(k) && !cm_has_kids(k) && cm_leaf_ok(cm_items[k].act)) return 1;
    return 0;
}
void cm_run(int i) {
    const CMItem *m = &cm_items[i];
    switch (m->act) {
    case CM_OP: if (!loaded && cui_hooks.new_part) cui_hooks.new_part(); geo_end(); ex_mode = 0; gallery_pick(m->arg); break;
    case CM_LFACE: geo_end(); ex_mode = 0; lface_open(); break;                              /* lathe_face 0x1801a3290 (cui_lface.c) */
    case CM_EDIT: if (op_nselected() > 0 && cui_hooks.op_menu) cui_hooks.op_menu(CM_EDIT); break;          /* RunEditCommonParams: CEditCommonDlg on the selected ops */
    case CM_DELETE: delete_selected_op(); break;            /* 3566 -> COpMgrTree::OnDeleteKey */
    case CM_UNDO: do_undo(); break;
    case CM_EXPAND: case CM_COLLAPSE:                         /* the tree's open flags (mcamui mach_open / grp_open / op_open) */
    case CM_TOOLLIST:                                         /* tool_report: RunToolReportForSelectedOps */
    case CM_RENUMTOOLS:                                       /* rn_open: CRenumbTl (IDD 805) */
    case CM_GRP_RENAME:                                       /* ask_open(CM_GRP_RENAME, "Rename", "Name:", the group's name) */
        if (cui_hooks.op_menu) cui_hooks.op_menu(m->act); break;                      /* RunToolReportForSelectedOps */
    case CM_CUT: op_copy(1); break;
    case CM_COPY: op_copy(0); break;
    case CM_PASTE: op_paste(); break;
    case CM_UNDELETE: op_undelete(); break;
    case CM_DOCFILE: op_doc_file(); break;
    case CM_RECALC: recalc_feeds(); break;                       /* list_op_recalc_sf */
    case CM_REVERSE: reverse_selected(); break;                  /* ReversePath(1) */
    case CM_GRP_DELETE:                                       /* DeleteGroup: Mastercam's refusals (mccore/opmanres strings) */
        if (cm_group == 1 && F.nops > 0) message("Toolpath Manager", "You cannot delete groups that contain operations.\n\nMove the group's operations to another group and try again.");
        else message("Toolpath Manager", "The Toolpath Manager must contain at least one group.  Group not deleted.");
        break;
    case CM_GRP_DELEMPTY: snprintf(cam_msg, sizeof cam_msg, F.nops ? "no empty groups" : "the Toolpath Manager keeps at least one group"); break;
    }
}

/* A new operation's heights. Mastercam keeps the default operation's height fields (value, incremental flag, on flag:
   Mill_MM / Mill_Inch.mcam-defaults, the same file cam_defaults_from_file reads) unchanged when the operation is
   created; they are turned into Z values only when the toolpath is computed, per chain, by mccore set_depths
   (decomp/mccore_depths.c @18089f680, param_2 = the chain's Z): depth +0x138 (+0x140 inc -> chain Z + value) plus the
   floor stock +0x820, top of stock +0x280 (+0x288 inc -> chain Z + value; -1e38 = 0), feed plane +0x128 (+0x130 inc ->
   top + value), retract +0x118 (on +0x121, inc +0x120 -> top + value), clearance +0x108 (on +0x111, inc +0x110 -> top +
   value). Operation offset X = record offset 0x68 + X. cam_defaults evaluates them at Z 0; this re-evaluates them at
   the first chain's (or first point's) Z with the same rule, as params_from_record does for stored ops. The active
   origin offset of set_depths (MastercamPlanes::ActiveOriginIsOn) is 0 here (Top plane, world origin). */
static McamFile dfo[2]; static int dfo_tried[2], dfo_ok[2];
void default_op_reset(void) {   /* Q19 (ours): the defaults file was saved - read again (cui_defaults.c) */
    for (int k = 0; k < 2; k++) { if (dfo_ok[k]) mcam_free(&dfo[k]); dfo_tried[k] = dfo_ok[k] = 0; }
}
const Op *default_op(int type, int metric) {   /* the Mill_MM / Mill_Inch defaults file's op of this type */
    McamFile *df = dfo; int *tried = dfo_tried, *ok = dfo_ok;
    int k = metric == 0 ? 1 : 0;
    if (!tried[k]) { char path[1100], rd[1100]; site_file(path, sizeof path, "shared", k ? "mill/Ops/Mill_Inch.mcam-defaults" : "mill/Ops/Mill_MM.mcam-defaults");
        tried[k] = 1; ok[k] = mcam_load(&df[k], defaults_read(path, rd, sizeof rd)); }   /* Q19: the program's saved copy when there is one (paths.h) */
    if (!ok[k]) return NULL;
    if (type == CAM_OPTIROUGH) {   /* 3drough: the first op 132 with style 0x16bc = 1 (cam_fileop.c default_op, the same rule) */
        for (int i = 0; i < df[k].nops; i++) { const Op *o = &df[k].ops[i]; if (o->opcode == 132 && o->raw && o->raw_n > 0x1760 && o->raw[0x16bc] == 1) return o; }
        return NULL;
    }
    if (cam_hst_style(type) >= 0) {   /* the first op 132 with style 0x16bc = cam_hst_style (cam_fileop.c default_op) */
        int st = cam_hst_style(type);
        for (int i = 0; i < df[k].nops; i++) { const Op *o = &df[k].ops[i]; if (o->opcode == 132 && o->raw && o->raw_n > 0x1760 && o->raw[0x16bc] == st) return o; }
        return NULL;
    }
    if (type == CAM_SRPOCKET || type == CAM_SRPARALLEL || type == CAM_SFPARALLEL || type == CAM_SFCONTOUR || type == CAM_SRPROJECT || type == CAM_BLEND || type == CAM_SRMPOCKET || type == CAM_SRPLUNGE || type == CAM_SFRADIAL || type == CAM_SFPROJECT || type == CAM_SFPENCIL) {   /* srough: the first op 10 / 5 / 11 / 15 / 7 / 109 / 131 / 44 (cam_fileop.c default_op, the same rule; 131's pocket block is un-shifted there, cam_default_op_record) */
        int oc = type == CAM_SFPENCIL ? 39 : type == CAM_SFPROJECT ? 13 : type == CAM_SRPOCKET ? 10 : type == CAM_SRPLUNGE ? 44 : type == CAM_SFRADIAL ? 12 : type == CAM_SFPARALLEL ? 11 : type == CAM_SFCONTOUR ? 15 : type == CAM_SRPROJECT ? 7 : type == CAM_BLEND ? 109 : type == CAM_SRMPOCKET ? 131 : 5;
        for (int i = 0; i < df[k].nops; i++) { const Op *o = &df[k].ops[i]; if (o->opcode == oc && o->raw && o->raw_n >= 0x1700) return o; }
        return NULL;
    }
    for (int i = 0; i < df[k].nops; i++) { const Op *o = &df[k].ops[i]; if (cam_fileop_type(o) == type && o->raw && o->raw_n >= 0x900) return o; }
    return NULL;
}
void heights_from_default_op(CamParams *p, int type, int metric, double cz) {
    { static const uint8_t z[0x300]; pp_tp_record(z, sizeof z); }   /* Tool page switches: off unless the defaults op has them */
    const Op *o = default_op(type, metric);
    if (o) {
        const uint8_t *base = o->raw;
        if (type == CAM_SRMPOCKET) { uint32_t un = 0; const uint8_t *u = cam_default_op_record(type, metric, &un); if (u && un == o->raw_n) base = u; }   /* op 131: its pocket
                                                                                                   block un-shifted (cam_fileop.c, user decision T-1) */
        const uint8_t *r = base;
        { const uint8_t *mr = grp_modal_record(base, o->raw_n, o->opcode); if (mr) r = mr; }   /* InitOperation's "override with modal
                                                                                                   values" copy (cui_grpprops.c) */
        pp_hf_record(r, o->raw_n, cz);                   /* the Linking page's flags (opparams_ui.inc) */
        /* threadmill: a new Thread Mill op keeps the defaults op's clearance 0x1230 (pp_hf_record: thr_clr): FUN_180236810 rapids to
           it with no on / off switch (was ours: the Linking flags' clearance, i.e. the feed plane) */
        pp_tp_record(r, o->raw_n);                       /* ... and the Tool page's (record 0x1a9 / 0x2f1 / 0x2f9) */
        if (type == CAM_THREAD) return;                  /* Thread Mill: its own top / depth fields (+0x1240 / +0x1248), cam_defaults' values kept */
        #define RD(off) ({ double v_; memcpy(&v_, r + (off), 8); v_; })
        double top = RD(0x2e8); if (top == -1e38) top = 0;
        if (r[0x2f0]) top += cz;
        /* InitOperation (mccore 0x180046660, decomp/mccore_ops.c:2425): the first operation (the op list is empty) with an
           absolute top of stock (op +0x288 = record 0x2f0 off) takes calc_top_of_stock = the machine group stock's top
           when stock_ok (cui_stock.c stock_top) */
        else if (F.nops == 0) { double zs; if (stock_top(&zs)) top = zs; }
        double fin = (r[0x1a8] ? cz : 0) + RD(0x1a0);
        /* floor stock op +0x820 (record 0x888); Facing (opcode 0x66) takes the constant DAT_181378c50 (1e-308, i.e. 0)
           instead; below tol the opcode's own offset is subtracted when its flag is set (op offsets of the switch) */
        double fstock = o->opcode == 0x66 ? 0 : RD(0x888);
        {   int bo = cam_bt_offset(o->opcode);                   /* set_depths' break through (cam_fileop.c cam_bt_eff; tol = the system
                                                                    tolerance, was 1e-7 here) */
            p->bt_eff = bo && o->raw_n >= (uint32_t)bo + 16 ? cam_bt_eff(o->opcode, r[bo], RD(bo + 8), fstock) : 0;
            fin -= p->bt_eff; }
        fin += fstock;
        double fp = RD(0x190) + (r[0x198] ? top : 0);
        p->top_z = top; p->final_z = fin;
        p->dep_inc = r[0x1a8] != 0; p->top_inc = r[0x2f0] != 0; p->dep_cz = cz;
        if (type == CAM_DRILL) p->r_plane = fp; else p->feed_plane = fp - top;
        double ret = RD(0x180) + (r[0x188] ? top : 0), clr = RD(0x170) + (r[0x178] ? top : 0);
        p->retract_z = r[0x189] ? ret : -1e30;
        /* clearance off: this program's motion has no "no clearance" mode, so it rapids at the retract / feed plane
           (params_from_record's convention) */
        p->clearance = r[0x179] ? clr : (r[0x189] ? ret : fp);
        #undef RD
        return;
    }
}

/* a new op's parameters from the defaults (InitOperation): cam_defaults + the active planes, the Linking / Tool page flags and heights
   from the Mill_MM / Mill_Inch defaults op at the chain's Z, the contour variant, Facing's stock boundary. Reads the pending op's
   chains / holes (open_params: the deep copy of the selection; batch 12 Reload: the op's own). Was inline in open_params (same steps,
   same order of the writes to pending.p: cam_defaults / planes_init_op did not depend on the chains). */
static void pp_seed_params(void) {
    cam_defaults(&pending.p, cam_mode, &F);
    planes_init_op(&pending.p);                          /* InitOperation: the active planes (cui_planes.c; Top = no change) */
    {   /* chain Z as params_from_record takes it: the first chain's first entity (start / centre), else the first point */
        double cz = 0;
        double fbox[4], ftop;
        if (pending.nchains && pending.chains[0].n) { const Ent *e0 = &F.ents[pending.chains[0].ent[0]]; cz = e0->type == E_ARC ? e0->c.z : e0->a.z; }
        else if (cam_mode == CAM_FACE && stock_facing_box(fbox, &ftop)) cz = ftop;   /* make_facing_pock_bound: the boundary at the stock's top (cui_stock.c) */
        else if (pending.nholes) for (int e = 0; e < F.nents; e++) { const Ent *en = &F.ents[e];
            if (en->type == E_ARC && fabs(en->c.x - pending.holes[0].x) < 1e-6 && fabs(en->c.y - pending.holes[0].y) < 1e-6) { cz = en->c.z; break; }
            if (en->type == E_POINT && fabs(en->a.x - pending.holes[0].x) < 1e-6 && fabs(en->a.y - pending.holes[0].y) < 1e-6) { cz = en->a.z; break; } }
        pp_hf_op(&pending);                              /* this program's convention, unless the default op gives its flags */
        heights_from_default_op(&pending.p, cam_mode, mcam_is_metric(&F), cz);
    }
    if (cam_mode == CAM_CONTOUR) pending.p.contour_type = cam_variant;
    {   /* Facing without chains (op +0x8c = 0): facing_op_uses_stock (mcmill 0x180098e00) -> make_facing_pock_bound (mcmill
           0x180098f90): the machine group stock's job_setup_stock_corners rectangle (cui_stock.c stock_facing_box); no
           defined stock = no boundary (the bound returns -1). Was: the solids' bounding box (ours, removed). */
        double fbox[4], ftop, fc[3];
        if (cam_mode == CAM_FACE && !pending.nchains && stock_facing_box(fbox, &ftop)) {
            pending.p.use_box = 1; memcpy(pending.p.box, fbox, sizeof fbox);
        } else if (cam_mode == CAM_FACE && !pending.nchains && stock_facing_circle(fc, &ftop)) {   /* cylindrical stock: its circle */
            pending.p.use_circ = 1; memcpy(pending.p.circ, fc, sizeof fc);
        }
    }
}

static int script_tool = -1;                             /* ours (tests): the libtool / usetool step's tool for the next gen step */
static int script_newop_tool = -1;                       /* ours (tests): newoptool - the tool of the next new op, however it is started */
void open_params(void) {
    pv_clear();                                          /* Q20: a new dialog has no preview */
    chain_keep_last();                                   /* Last (FUN_1801ed6b0 / FUN_1801e7be0): core_ui cui_chain.c */
    edit_camop = -1;
    pending_free();                                      /* what an earlier dialog's Cancel left (pending owns its chains / holes until here) */
    pending.chains = calloc(ncchains ? ncchains : 1, sizeof(Chain));
    for (int i = 0; i < ncchains; i++) {                /* deep copy (selection keeps its own) */
        pending.chains[i].n = cchains[i].n; pending.chains[i].closed = cchains[i].closed; pending.chains[i].cut0 = cchains[i].cut0; pending.chains[i].cut1 = cchains[i].cut1;
        pending.chains[i].sld = cchains[i].sld ? 1 : 0;   /* a solid chain (its curves are Ent.gen entities) */
        pending.chains[i].ent = malloc(cchains[i].n * sizeof(int)); pending.chains[i].rev = malloc(cchains[i].n * sizeof(int));
        memcpy(pending.chains[i].ent, cchains[i].ent, cchains[i].n * sizeof(int)); memcpy(pending.chains[i].rev, cchains[i].rev, cchains[i].n * sizeof(int));
    }
    pending.nchains = ncchains;
    pending.holes = malloc((ncholes ? ncholes : 1) * sizeof(Hole)); memcpy(pending.holes, choles, ncholes * sizeof(Hole)); pending.nholes = ncholes;
    pp_seed_params();
    {   /* default tool = InitOperation: the first tool of the machine group's list that fits the op - op_is_mill(opcode)
           (mccore @1800c6150: opcode <= 0x39, 100..200, 0x1a0, multiaxis) needs Cnc::Tool::is_mill_tool, op_is_drill(opcode)
           needs is_drill_tool. Drill opcodes pass both tests, so no tool fits them (decoded as is): no default tool. The
           CToolFilt filter (common/PRM/toolfilt(m).prm per opcode) is absent here, so it lets every type through. */
        int st, oc = mcam_opcode_for(&pending.p, &st), pick = -1;
        int need_mill = oc <= 0x39 || (oc >= 100 && oc <= 200) || oc == 0x1a0, need_drill = cui_otx_is_drill(oc);
        for (int i = 0; i < F.ntools && pick < 0; i++) {
            int t = F.tools[i].type;
            if (need_mill && !tool_is_mill(t)) continue;
            if (need_drill && !tool_is_drill(t)) continue;
            pick = i;
        }
        tool_choice = pick;                              /* -1 = no tool: the Tool page shows none selected */
        if (pick >= 0) fs_from_source(&pending.p, &F.tools[pick]);
        if (cam_mode == CAM_CONTOUR) pending.p.contour_type = cam_variant;   /* the tool may have suggested chamfer */
    }
    pending.nref = 0; pending.nrface = 0;               /* 3D ops: their references come from the msurf flow (cui_chain.c ms_to_dialog) */
    if (script_newop_tool >= 0 && script_newop_tool < F.ntools) { tool_choice = script_newop_tool; fs_from_source(&pending.p, &F.tools[script_newop_tool]); script_newop_tool = -1; }   /* ours (tests) */
    pending.name[0] = 0;   /* the comment (op +6) of a new op is empty: InitOperation clears it (formats/contour_3d_tabcutoff.md), every
                              op of Mill_MM.mcam-defaults has none. Before L-7 (2026-11-16) ours named it "<type> <n>" */
    show_params = 1; pp_ok_forget(); tp_list_open();   /* a new op: OK always generates; the Tool page made again */
}


/* batch 12: "Reload parameters from defaults file" 9431 = ToolpathDlg::OnReadFromDefaultsPb 0x180894220 (decomp/b12/tpdlg_all.c; only
   with this[0x729c] = 0, the single-op dialog) -> ReplaceOperation 0x1808951f0 (vtable +0x598): returns without a change unless the
   machine group's defaults file (OMgetGrpDefaultsFile) exists and the op is in the op list; then a new op of the same opcode is made by
   InitOperation(opcode, read defaults = 1, ...) - the defaults file's op of that type (here default_op: the Mill_MM / Mill_Inch file
   new ops come from) with the active planes -, the old op's tool assembly is put on it (TlToolSystem slot 0x118; list_op_copy mode 3
   when the tool is not found) together with the old op's tool slot (op +0xb0 -> new op +0xb0) and tool info block (+0x2e0), its chains
   are copied (list_op_copy mode 4, unless op_is_no_ents), it takes the old op's place in the tree (COpMgrTree::MoveOperation) and the
   old op is deleted. reload_from_toolpg = the op id and the dialog closes (OnCancel); the op manager's edit loop (decomp/opman_tree_all.c
   l.16862, while reload_from_toolpg > 0) opens it again on the new op, on the same page (s_ActivePage).
   Here: the dialog's op is re-seeded as open_params seeds a new op from its chains (pp_seed_params), the tool kept (tool_choice;
   its feeds / speeds as for a new op, fs_from_source - the ToolSystem assignment's own feed rule is NOT DECODED), the stored op it
   replaces (file_op_id) / our op being edited (edit_camop) kept, the tree fresh, the page kept. Ours: the replacement takes effect
   at OK (Mastercam commits it at once: a Cancel after Reload leaves the reloaded op, dirty); the op comment is kept; a new op (not in
   the list yet in this program) reloads too. Returns 0 when there is no defaults op of this type (ReplaceOperation's false). */
int pp_reload_defaults(void) {
    if (!show_params || !default_op(pending.p.type, mcam_is_metric(&F))) return 0;
    CamParams old = pending.p;
    cam_mode = old.type;
    if (old.type == CAM_CONTOUR) cam_variant = old.contour_type;
    pp_seed_params();
    pending.p.file_op_id = old.file_op_id;
    if (tool_choice >= 0 && tool_choice < F.ntools) fs_from_source(&pending.p, &F.tools[tool_choice]);
    else { pending.p.tool_num = old.tool_num; pending.p.tool_dia = old.tool_dia; pending.p.tool_type = old.tool_type; }   /* a custom tool (gen_begin registers it) */
    if (old.type == CAM_CONTOUR) pending.p.contour_type = old.contour_type;
    pp_ok_forget();                                      /* the replaced op is regenerated at OK (Mastercam: a new, dirty op) */
    return 1;
}

/* ---- OK on an existing operation's dialog: Mastercam's edit_operation (mccore 0x1800b0760, decompiled in
   decomp/mccore_edit_operation.c; the tree's Parameters item: COpMgrTree::Selchanged 0x18039be00 ->
   edit_operation(op, 0, ...), decomp/opman_tree_all.c:42397) keeps a copy of the op before the dialog and after OK
   compares it with the dialog's result: memcmp_op(copy, cur_op, 0, 0) == 0 and the holder unchanged -> nothing is
   done (no dirty flag, no NCI change, the op record copied back as it was; decomp l.1338-1346 -> LAB_1800b2f16 l.2331); a change
   either patches the NCI in place (bnci_update) or marks the op dirty (op +0x94 = 1, decomp l.2572-2588). So an unchanged
   OK does not regenerate. The compare is chosen by op_is_oldnci(opcode) / op_is_msurf(opcode) (decomp l.474-475, gate
   l.1326; decomp/mccore_edit_operation_rule.c): for the modern / msurf opcodes - every opcode this program edits (1, 2, 3,
   0x12, 100, 0x66, 0x69, 0x6a, 0x86 are all in op_is_oldnci's 0 list) - the op compare memcmp_op(copy, cur_op, 0, 0)
   plus, for opcode 0x84 with the HST holder flag, the TlHolder compare; for old-NCI opcodes the else branch (l.1347-1358)
   compares only the NC file name (op +0x170, _stricmp), the comment (op +6) and byte op +0x289 - it does not apply here.
   The tool: op_tool_info_changed (l.1306) on the op's tool record.
   Stand-in: "unchanged" = the inputs generate_pending would hand to cam_generate (the params with the Linking / Tool page /
   Planes / template-page applies), the op comment, the tool choice and the whole tool record with its holder outline
   (tool_same) equal those of the dialog as opened. GAP: memcmp_op's own field list / exclusions are not decoded field by
   field - this compare is over the engine inputs, so a field Mastercam ignores but the engine reads counts as a change
   (we regenerate where Mastercam might not), and op fields this program does not model are not compared.
   NOT PORTED: the changed case marks the op dirty in Mastercam (regenerated later with "Regenerate all dirty"); this
   program regenerates at once (as before). New ops (open_params) always generate, and so (ours) does an op without a
   toolpath (a stored op with no NCI or a stale one, an op of ours with no moves) - Mastercam would leave it dirty. */
typedef struct { int ok, tool, has_tool; CamParams p; char name[sizeof pending.name]; Tool t; double *holder; int holder_n; } PpSnap;
static PpSnap pp_open;
/* Q20: the op as Preview toolpath computed it (ToolpathDlg +0x7360, GetPreviewOp): after a preview, RunChainBasedTpDialog hands it
   to edit_operation as the op before the dialog (vtable +0x478 GetTpPreviewed -> +0x470 GetPreviewOp, decomp/mcmill_all.c
   l.401947-401950), so OK compares the dialog with it (cui_preview.c) */
static PpSnap pp_pv;
/* the tool as generation uses it (gen_begin copies num / dia from it; Mastercam: op_tool_info_changed and the TlHolder
   compare, decomp/mccore_edit_operation.c l.1306 / l.1326-1336): the whole record and its holder outline */
static void tool_snap(PpSnap *s) {
    free(s->holder); s->holder = NULL; s->holder_n = 0; s->has_tool = 0;
    if (tool_choice < 0 || tool_choice >= F.ntools) return;
    const Tool *t = &F.tools[tool_choice];
    memcpy(&s->t, t, sizeof s->t); s->t.holder_rz = NULL; s->has_tool = 1;
    if (t->holder_rz && t->holder_n > 0) { s->holder_n = t->holder_n; s->holder = malloc(sizeof(double) * 2 * t->holder_n); memcpy(s->holder, t->holder_rz, sizeof(double) * 2 * t->holder_n); }
}
static int tool_same_s(const PpSnap *s) {
    if (tool_choice < 0 || tool_choice >= F.ntools) return !s->has_tool;
    if (!s->has_tool) return 0;
    const Tool *t = &F.tools[tool_choice]; Tool c; memcpy(&c, t, sizeof c); c.holder_rz = NULL;
    if (memcmp(&c, &s->t, sizeof c)) return 0;
    int hn = t->holder_rz ? t->holder_n : 0;
    if (hn != (s->holder ? s->holder_n : 0)) return 0;
    return !hn || !memcmp(t->holder_rz, s->holder, sizeof(double) * 2 * hn);
}
static int tool_same(void) { return tool_same_s(&pp_open); }
static void pp_applied(CamParams *out) {               /* pending.p as generate_pending applies it, pending.p itself kept */
    CamParams save; memcpy(&save, &pending.p, sizeof save); OpHFlags hb; memcpy(&hb, &pp_hf, sizeof hb);
    cam_common_drop_heights(&pending.p); pp_hf_apply(); pp_tp_apply(); planes_map_to_op(&pending.p); pf_apply(&pending.p);
    memcpy(out, &pending.p, sizeof *out);
    memcpy(&pending.p, &save, sizeof save); memcpy(&pp_hf, &hb, sizeof hb);
}
/* Q19 / Q20: the dialog's op as OnUpdateLocalOp / OnOK_Pages leaves it (the pages applied, the tool's number / diameter as gen_begin
   takes them) without changing pending: OUT = a shallow copy of pending (its chains / holes are pending's, read only) */
void pp_dialog_op(CamOp *out) {
    *out = pending; pp_applied(&out->p);
    if (tool_choice >= 0 && tool_choice < F.ntools) { out->p.tool_num = F.tools[tool_choice].num; out->p.tool_dia = F.tools[tool_choice].dia; }
    out->m = NULL; out->nm = 0;
}
static void snap_take(PpSnap *s) {
    { double *h = s->holder; memset(s, 0, sizeof *s); s->holder = h; }
    pp_applied(&s->p); s->tool = tool_choice; memcpy(s->name, pending.name, sizeof s->name); tool_snap(s); s->ok = 1;
}
static int snap_unchanged(const PpSnap *s) {
    if (!s->ok || tool_choice != s->tool || memcmp(s->name, pending.name, sizeof s->name) || !tool_same_s(s)) return 0;
    CamParams now; memset(&now, 0, sizeof now); pp_applied(&now);
    return !memcmp(&now, &s->p, sizeof now);
}
void pp_ok_snapshot(void) { snap_take(&pp_open); }
void pp_ok_forget(void) { pp_open.ok = 0; }
int pp_ok_unchanged(void) { return snap_unchanged(&pp_open); }
void pp_pv_snapshot(void) { snap_take(&pp_pv); }
void pp_pv_forget(void) { pp_pv.ok = 0; }
int pp_pv_unchanged(void) { return snap_unchanged(&pp_pv); }
/* batch 11 (ours; formats/opdlg_port_audit.md 17): a Mastercam op (stored in the part) whose dialog changed only post-side values -
   Coolant 6546 (record 0x398, 0xb50-0xb79), Canned Text 6548, Misc Values 6555 (0x7e8-0x867). Mastercam marks the op dirty and its
   regeneration writes the same motion with these values in the section-start records (bnci_wr_sof: 1011 / 1012 / 1025 / 1002); here
   the stored toolpath is kept and those records are rewritten (cam_postvals.c pv_nci_rewrite), so the LinuxCNC post still posts
   Mastercam's toolpath and the Mastercam post (.pst) path reads the new values. Returns the PVG_* groups changed, 0 = not this case. */
static void pv_strip(CamParams *p, int *groups) {
    int k = 0;
    for (int i = 0; i < p->nrpatch; i++) {
        if (pv_is_post_field(p->rpatch[i].off, p->rpatch[i].n)) {
            if (groups) *groups |= p->rpatch[i].off == PV_COOL_OLD ? PVG_COOL : p->rpatch[i].off >= PV_CAN_ON ? PVG_CAN | PVG_COOL : PVG_MISC;
            continue;
        }
        p->rpatch[k++] = p->rpatch[i];
    }
    for (int i = k; i < p->nrpatch; i++) memset(&p->rpatch[i], 0, sizeof p->rpatch[i]);
    p->nrpatch = k;
}
static int pp_ok_postonly(int *index) {
    if (!pp_open.ok || edit_camop >= 0 || pending.p.file_op_id <= 0 || tool_choice != pp_open.tool || memcmp(pp_open.name, pending.name, sizeof pp_open.name) || !tool_same()) return 0;
    int i = -1; for (int k = 0; k < F.nops; k++) if (F.ops[k].id == pending.p.file_op_id) i = k;
    if (i < 0 || !F.ops[i].raw || F.ops[i].stale) return 0;
    for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) return 0;
    CamParams now, was; memset(&now, 0, sizeof now); pp_applied(&now); memcpy(&was, &pp_open.p, sizeof was);
    int g = 0, g0 = 0; pv_strip(&now, &g); pv_strip(&was, &g0);
    now.coolant = was.coolant;                         /* CamParams.coolant mirrors the V9 coolant word 0x398 (cui_opdlg.c cool_put) */
    if (!g || memcmp(&now, &was, sizeof now)) return 0;
    *index = i; return g;
}
static int pp_ok_apply_postvals(int i, int groups) {
    CamParams now; memset(&now, 0, sizeof now); pp_applied(&now);
    Op *o = &F.ops[i]; uint8_t *save = malloc(o->raw_n); memcpy(save, o->raw, o->raw_n);   /* restored when the NCI cannot take them */
    for (int k = 0; k < now.nrpatch; k++)
        if (pv_is_post_field(now.rpatch[k].off, now.rpatch[k].n) && (uint32_t)now.rpatch[k].off + now.rpatch[k].n <= o->raw_n)
            memcpy(o->raw + now.rpatch[k].off, now.rpatch[k].v, now.rpatch[k].n);
    char msg[300];
    if (!pv_nci_rewrite(&F, i, groups, msg, sizeof msg)) { memcpy(o->raw, save, o->raw_n); free(save); snprintf(cam_msg, sizeof cam_msg, "%s", msg); return 0; }
    free(save);
    o->postv_groups |= groups;
    return 1;
}
/* Dirty operations (formats/opdlg_port_audit.md "Dirty operations"). edit_operation's changed case (decomp/mccore_edit_operation.c
   l.2572-2589): unless one of the masked compares says only names / file names changed (cStack_1b3e0 / 1b3da / 1b3de / 1b3d9 /
   1b3dc / bVar49 / bVar10, l.1464-2329) or bnci_update patched the NCI (l.2577; mccore 0x180139200 -> BnciUpdate::Update ->
   UpdatePossible / DontAllow 0x180a5c530, decomp/dirty/core_bnciupd.c), the op is marked dirty: op +0x94 = !op_is_oldnci(opcode)
   (every opcode made here is a new-NCI one: op_is_oldnci lists 1-4, 0x12, 0x64, 0x66, 0x69, 0x6a, 0x86 and returns 0 for
   op_is_msurf 0x84), +0x97 = 0 - and not regenerated (bnci_regenerate_op follows only for opcode 0x6c, l.3140-3145): the
   op keeps its toolpath until Regenerate (cui_dirty.c).
   DontAllow allows the NCI patch when the two ops differ only in these fields (it zeroes / copies them in both copies and
   memcmps 0x28d0 bytes; op offset + 0x68 = record offset): op +6 0x78 (comment), +0x80..+0x104 (+0xf8 program #, +0xfc / +0x100
   sequence), +0x170 0x104 (NC file name), +0x2e0 (tool number, mill), +0x308 / +0x30c (diameter / length offset), +0x310 /
   +0x318 / +0x320 (feed, plunge, retract rates), +0x328 (spindle speed), +0x330 (coolant word), +0x336 0x78 (tool comment),
   +0x53c, +0x540 (mill machine), +0x544 8 (tool slot), +0x780 0x80 (misc values), +0x868 0x20, +0x930 0x28, +0xae8 0x2a (canned
   text), and for 2D High Speed (0x86) +0x11e0. It refuses (dirty) when the op has dependents, is a subprogram op, a drill op
   with op +0x1298, its NCI cannot be restored (list_op_restore_bnci), or a 2D HST feed lies within tol of op +0x1600 (styles 0 /
   3 with +0x1654) - the last three are NOT PORTED (no dependents / subprograms here; an op without a toolpath already
   regenerates at OK, pp_ok_forget). Tool changes in the dialog: edit_operation's tool assembly compare (l.2545-2575, not fully
   decoded) - ours: another tool = dirty.
   The patch itself (BnciUpdate::ProcessNci): ported for stored ops (D2, bnci_update.c, pp_ok_bnciu below: feed / plunge / retract
   rates, spindle speed, coolant, D offset - the NCI patched, the toolpath kept); batch 11's post values (pv_nci_rewrite); an op of
   ours is regenerated at once (the same motion with the new values, ours). */
static const struct { uint16_t a, b; } BNCI_REC[] = {   /* record offsets [a, b) */
    { 0x6e, 0xe6 }, { 0xe8, 0x16c }, { 0x1d8, 0x2dc }, { 0x348, 0x34c }, { 0x370, 0x39c }, { 0x39e, 0x416 }, { 0x5a4, 0x5aa },
    { 0x5ac, 0x5b4 }, { 0x7e8, 0x868 }, { 0x8d0, 0x8f0 }, { 0x998, 0x9c0 }, { 0xb50, 0xb7a },
};
static void bnci_mask(CamParams *p) {
    p->feed = p->plunge_feed = p->mc_rrate = 0; p->rpm = 0; p->coolant = 0; p->ct_doff = 0;
    int hst = p->type == CAM_DYNAMIC || p->type == CAM_AREA || p->type == CAM_DYNCONTOUR || p->type == CAM_PEEL, k = 0;
    for (int i = 0; i < p->nrpatch; i++) {
        int off = p->rpatch[i].off, end = off + p->rpatch[i].n, in = hst && off >= 0x1248 && end <= 0x1250;
        for (size_t r = 0; r < sizeof BNCI_REC / sizeof BNCI_REC[0] && !in; r++) in = off >= BNCI_REC[r].a && end <= BNCI_REC[r].b;
        if (!in) p->rpatch[k++] = p->rpatch[i];
    }
    for (int i = k; i < p->nrpatch; i++) memset(&p->rpatch[i], 0, sizeof p->rpatch[i]);
    p->nrpatch = k;
}
static int pp_ok_dirties_s(const PpSnap *s) {           /* 1 = OK marks the (existing) op dirty (compared with S) */
    if (!s->ok || (edit_camop < 0 && pending.p.file_op_id <= 0)) return 0;   /* a new op / no snapshot (no toolpath): computed */
    if (tool_choice != s->tool || !tool_same_s(s)) return 1;
    CamParams now, was; memset(&now, 0, sizeof now); pp_applied(&now); memcpy(&was, &s->p, sizeof was);
    bnci_mask(&now); bnci_mask(&was);
    return memcmp(&now, &was, sizeof now) != 0;
}
static int pp_ok_mark_dirty(void) {
    int idx = -1;
    if (edit_camop >= 0 && edit_camop < ncamops) idx = camops[edit_camop]->op_index;
    else for (int i = 0; i < F.nops; i++) if (F.ops[i].id == pending.p.file_op_id) idx = i;
    if (idx < 0) return 0;
    CamOp job; if (!gen_begin(&job)) return 1;           /* a message (no tool), as generate_pending */
    CamOp *e = malloc(sizeof *e); *e = job; e->m = NULL; e->nm = 0; e->op_index = -1; e->dirty = 0; e->edit = NULL;
    op_mark_dirty_edit(idx, e);
    pending_free(); edit_camop = -1;                     /* gen_begin copied the chains / holes into the edit */
    doc_dirty = 1; sel = idx; msel_owner = -9; sim = -1; cam_msg[0] = 0;
    show_params = 0; cam_mode = 0; cam_clear_selection(); pls_open = 0; upl_open = 0;   /* the dialog closes as gen_commit closes it */
    scene_reupload();
    return 1;
}
/* D2 (user decision, formats/opdlg_port_audit.md 19.7): a stored Mastercam op whose dialog changed only values BnciUpdate patches
   (pp_ok_dirties 0: bnci_mask's fields) keeps its toolpath; its OP_ID record takes the dialog's values and its NCI section is
   patched as BnciUpdate::ProcessNci does (bnci_update.c). The values: the op_tool_info block's (mcam_saveops.c: record 0x378 feed,
   0x380 plunge, 0x390 rpm, 0x398 coolant 16 / 8), retract rate 0x388 and D offset 0x370 (cam_fileop.c), the record patches inside
   the masked ranges. 1 = patched, 0 = not this case, -1 = bnci_update refused (a case NOT PORTED or DontAllow): the op is marked
   dirty and keeps its toolpath, as edit_operation does when bnci_update fails (decomp/mccore_edit_operation.c l.2572-2589). */
static int pp_ok_bnciu(void) {
    if (!pp_open.ok || edit_camop >= 0 || pending.p.file_op_id <= 0 || tool_choice != pp_open.tool || !tool_same()) return 0;
    if (pp_ok_dirties_s(&pp_open)) return 0;
    int i = -1; for (int k = 0; k < F.nops; k++) if (F.ops[k].id == pending.p.file_op_id) i = k;
    if (i < 0 || !F.ops[i].raw || F.ops[i].stale || F.ops[i].regen) return 0;
    if (F.ops[i].raw_n < 0x1600) return -1;                     /* a record too short to patch: refused */
    for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) return 0;
    CamParams now; memset(&now, 0, sizeof now); pp_applied(&now);
    Op *o = &F.ops[i]; uint8_t *rec = malloc(o->raw_n); memcpy(rec, o->raw, o->raw_n);
    for (int k = 0; k < now.nrpatch; k++)
        if ((uint32_t)now.rpatch[k].off + now.rpatch[k].n <= o->raw_n) memcpy(rec + now.rpatch[k].off, now.rpatch[k].v, now.rpatch[k].n);
    double v; int32_t iv;
    v = now.feed; memcpy(rec + 0x378, &v, 8); v = now.plunge_feed; memcpy(rec + 0x380, &v, 8); v = now.mc_rrate; memcpy(rec + 0x388, &v, 8);
    iv = (int32_t)now.rpm; memcpy(rec + 0x390, &iv, 4); iv = now.ct_doff; memcpy(rec + 0x370, &iv, 4);
    int16_t cw; memcpy(&cw, rec + 0x398, 2);
    if ((now.coolant != 0) != (cw == 16)) { cw = now.coolant ? 16 : 8; memcpy(rec + 0x398, &cw, 2); }
    if (!memcmp(rec, o->raw, o->raw_n)) { free(rec); return 0; }
    double minarc = F.ctl_ok && F.ctl_arcfeed && F.mach_ok ? F.mach_minarc[tp_metric() ? 1 : 0] : -1;   /* BnciUpdate this+0x308 */
    BnciuStats st; char msg[400];
    int ok = bnciu_apply(&F, i, rec, o->raw_n, minarc, &st, msg, sizeof msg);
    free(rec);
    if (!ok) { fprintf(stderr, "D2: %s - marked dirty\n", msg); return -1; }   /* log only (ours): Mastercam shows no message */
    return 1;
}
/* OK without a computation (1 = done): unchanged existing op -> closed as edit_operation leaves it (as Cancel: the op stays as
   it was); batch 11's post values of a stored op; a change that marks the op dirty. 0 = compute (generate_pending / gen_*) */
static int pp_ok_dirties(void) { return pp_ok_dirties_s(&pp_open); }
int pp_ok_quick(void) {
    tp_list_ok();                                        /* the Tool page's UpdateLocalOp: WriteToolIconPrm (cui_toolpage.c) */
    if (ms_dialog_ok()) return 1;                        /* S4: a new 3D op - the picks after the dialog, then its generation (cui_chain.c) */
    if (pv_on) {   /* Q20 (cui_preview.c): edit_operation compares with the previewed op (GetPreviewOp) */
        pp_open.ok = 0;
        if (pv_has_job() && pp_pv_unchanged()) { pv_commit(); return 1; }                     /* no change since: as computed */
        if (pv_has_job() && pp_ok_dirties_s(&pp_pv)) { pv_commit_dirty(); return 1; }        /* changed: the preview kept, dirty */
        pv_clear(); return 0;                                                                /* else computed (as before) */
    }
    if (pp_ok_unchanged()) { pp_cancel(); pp_open.ok = 0; return 1; }
    {   int idx = -1, g = pp_ok_postonly(&idx);          /* batch 11: post values only - the stored toolpath stays */
        if (g && pp_ok_apply_postvals(idx, g)) { pp_cancel(); pp_open.ok = 0; doc_dirty = 1; scene_reupload(); return 1; }
    }
    {   int b = pp_ok_bnciu();                           /* D2: the NCI patch; refused -> dirty (edit_operation) */
        if (b > 0) { pp_cancel(); pp_open.ok = 0; doc_dirty = 1; scene_reupload(); return 1; }
        if (b < 0) { pp_open.ok = 0; if (pp_ok_mark_dirty()) return 1; }
    }
    if (pp_ok_dirties()) { pp_open.ok = 0; if (pp_ok_mark_dirty()) return 1; }
    pp_open.ok = 0;
    return 0;
}
/* the dialog's OK (script opok; the Qt OK button runs pp_ok_quick and then gen_* on a worker thread) */
void pp_ok(void) {
    if (pp_ok_quick()) return;
    generate_pending();
}

/* generate_pending in three parts so a front end can run the computation off the UI thread (ours: the Qt dialog's OK
   with a progress window and Cancel): gen_begin applies the dialog to the pending op and hands back a copy of it (its
   chains / holes deep-copied, so pending is untouched while it runs); gen_run = cam_generate on that copy (reads F
   only, writes the copy); gen_end takes the copy back into pending and continues as before - or, cancelled, drops it
   and restores pending's params as they were before OK (the dialog stays open). generate_pending runs the three in a
   row: the same steps on the same data as the single function it replaces. */
static CamParams gen_before; static int gen_cancellable;
static Chain *gen_dup_chains(const Chain *c, int n) {
    Chain *d = calloc(n ? n : 1, sizeof(Chain));
    for (int i = 0; i < n; i++) d[i] = chain_dup(&c[i]);
    return d;
}
static void gen_free_parts(CamOp *o) {
    for (int i = 0; i < o->nchains; i++) cam_chain_free(&o->chains[i]);
    free(o->chains); free(o->holes); free(o->m);
    o->chains = NULL; o->holes = NULL; o->m = NULL; o->nchains = o->nholes = o->nm = 0;
}
int gen_begin(CamOp *job) {
    memcpy(&gen_before, &pending.p, sizeof gen_before); gen_cancellable = 1;
    if (tool_choice < 0) { snprintf(cam_msg, sizeof cam_msg, "Unable to calculate toolpath.  Tool assigned to operation does not exist!"); return 0; }   /* mccoreres 3837 (caller not traced) */
    if (tool_choice < F.ntools) { pending.p.tool_num = F.tools[tool_choice].num; pending.p.tool_dia = F.tools[tool_choice].dia; }
    else {                                               /* custom tool: register it so the tree / post / project know it */
        Tool t; tool_new(&t, 10, pending.p.tool_dia, next_tool_num(), mcam_is_metric(&F));
        t.rpm = pending.p.rpm; t.feed = pending.p.feed; t.plunge = pending.p.plunge_feed;
        int idx = part_add_tool(&t);
        if (idx < 0) return 0;
        pending.p.tool_num = F.tools[idx].num; pending.p.tool_type = 10; tool_choice = idx;
        gen_cancellable = 0;                             /* the part's tool list changed: no Cancel past this point */
    }
    cam_common_drop_heights(&pending.p);                 /* the parameters dialog's heights replace Edit Common's record bytes */
    pp_hf_apply();                                       /* ... written with the Linking page's flags (opparams_ui.inc) */
    pp_tp_apply();                                       /* ... and the Tool page's switches / retract rate (ToolPageBase::MapToOp) */
    planes_map_to_op(&pending.p);                        /* ... and the Planes page's work offset (PlanesPageBase::MapToOp, cui_planes.c) */
    pf_apply(&pending.p);                                /* ... and the template pages' edited record fields (cui_opdlg.c) */
    *job = pending;
    job->chains = gen_dup_chains(pending.chains, pending.nchains);
    job->holes = malloc((pending.nholes ? pending.nholes : 1) * sizeof(Hole)); memcpy(job->holes, pending.holes, pending.nholes * sizeof(Hole));
    job->m = NULL; job->nm = 0;
    return 1;
}
int gen_can_cancel(void) { return gen_cancellable; }
/* the engines that stop on mcce_abort_set (mcce_dyn.c: Dynamic Mill and Dynamic Contour) - the
   others have no abort points (fast: contour / pocket / drill / ..., McHmm Area Clearance), so the front end shows Cancel
   disabled for them */
int gen_abortable(const CamOp *job) { int t = job->p.type; return t == CAM_DYNAMIC || t == CAM_DYNCONTOUR; }
/* Remachining sources 0 / 1 (FUN_180053a70): the operation list before OP - made here: their moves (Control per op: the contour's compensation), stored: their NCI */
static int cui_ops_cam_of(int i) { for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) return j; return -1; }
int cui_prev_ops(const McamFile *f, const CamOp *op, CamPrevOp **out, int *n) {
    CamPrevOp *o = calloc((size_t)(F.nops + 1), sizeof *o); int k = 0;
    for (int i = 0; i < F.nops; i++) {
        if (op->p.file_op_id > 0 && F.ops[i].id == op->p.file_op_id) break;
        if (op->p.file_op_id <= 0 && op->op_index == i && cui_ops_cam_of(i) >= 0 && camops[cui_ops_cam_of(i)]->m == op->m) break;
        int c = cui_ops_cam_of(i);
        if (c >= 0) {
            CamPrevOp e; memset(&e, 0, sizeof e);
            e.m = camops[c]->m; e.nm = camops[c]->nm; e.dia = camops[c]->p.tool_dia; e.opcode = camops[c]->p.type;
            e.control = camops[c]->p.type == CAM_CONTOUR && camops[c]->p.side != SIDE_ON && camops[c]->p.ct_comp == 1;
            if (camops[c]->p.type == CAM_POCKET && camops[c]->p.pk_ok) { int16_t cc1; memcpy(&cc1, camops[c]->p.pk_p + (0x13a8 - 0x1228), 2); e.pocket_1340 = cc1 == 1; }   /* FUN_1802cb260 l.487203 */
            if (camops[c]->p.type == CAM_POCKET && camops[c]->p.pk_ok && camops[c]->p.pk_p[0x1239 - 0x1228]) { int16_t cc; memcpy(&cc, camops[c]->p.pk_p + (0x13a8 - 0x1228), 2); e.pk_fin_ctl = cc == 1; }   /* FUN_1801ebc60 l.338082 */
            o[k++] = e;
        } else { CamPrevOp e; if (cam_prev_op_entry(f, i, &e)) o[k++] = e; }
    }
    *out = o; *n = k;
    return 1;
}
int gen_run(CamOp *job) { cam_set_prev_ops_fn(cui_prev_ops); return cam_generate(&F, job); }
void gen_end(CamOp *job, int ok, int cancelled) {
    if (cancelled && gen_cancellable) { gen_free_parts(job); memcpy(&pending.p, &gen_before, sizeof gen_before); ms_gen_end(0, 1); return; }   /* the op as before OK */
    pending.m = NULL; gen_free_parts(&pending); pending = *job;   /* the computed copy is the pending op (as cam_generate on it) */
    if (!ok) { snprintf(cam_msg, sizeof cam_msg, "%s failed: %s", cam_op_label(&pending), pending.error); ms_gen_end(0, 0); return; }
    ms_gen_end(1, 0);                                    /* S4: the flow of a new 3D op ends (committed below) */
    if (getenv("MCAMUI_GEN_MOVES")) { FILE *o = fopen(getenv("MCAMUI_GEN_MOVES"), "w");   /* tests (tools/tests/dlg_fileop_cmp.sh): the moves as cam_cli fileop --moves writes them */
        for (int i = 0; o && i < pending.nm; i++) fprintf(o, "%.6f %.6f %.6f %d\n", pending.m[i].p.x, pending.m[i].p.y, pending.m[i].p.z, pending.m[i].rapid); if (o) fclose(o); }
    char warn[sizeof pending.warning + 70]; snprintf(warn, sizeof warn, "%s", pending.warning[0] ? pending.warning : "");
    char nm[sizeof pending.name]; snprintf(nm, sizeof nm, "%s", cam_op_label(&pending));
    gen_commit();
    /* decisions batch dec1 (ours): an engine's stand-in warning (CamOp.warning, e.g. Pocket's Minimize tool burial fallback) is
       shown after the commit (gen_commit clears the message) */
    if (warn[0]) snprintf(cam_msg, sizeof cam_msg, "%s: warning: %s", nm, warn);
}
void generate_pending(void) {
    CamOp job; if (!gen_begin(&job)) return;
    pp_engrave_ask(&job);                                /* Engrave's depth warning 1808 (E-DW1) */
    int ok = gen_run(&job);
    gen_end(&job, ok, 0);
}
void gen_commit(void) {
    CamOp *op = malloc(sizeof *op); *op = pending;
    if (edit_camop >= 0 && edit_camop < ncamops) {       /* regenerate: the edited op replaces the old one in place */
        CamOp *old = camops[edit_camop];
        op->op_index = old->op_index; camops[edit_camop] = op;
        int keep = old->op_index; old->op_index = -1;
        camops[edit_camop]->op_index = keep;
        readd_camops();
        cam_op_free(old);                                 /* op edits are not on Undo's stack (formats/align_ops.md 4) */
        edit_camop = -1;
    } else {
        { int at = op->p.file_op_id > 0 ? takeover_index(op->p.file_op_id) : -1;   /* taken over in place */
          if (op->p.file_op_id > 0) remove_stored_op(op->p.file_op_id);   /* edited from Mastercam's: it replaces the stored op */
          else if (!op->p.op_seq) op_seq_assign(op);     /* a new op: OMgetNextOpId (InitOperation, decomp/mccore_ops.c l.272 / l.1698) */
          add_op_at(op, at); }
        if (ncamops < MAX_CAMOPS) { camops[ncamops++] = op; op_buffers_purge(3); }   /* CreateNewOperation purges the buffers; ops are not on Undo's stack */
    }
    doc_dirty = 1;
    memset(&pending, 0, sizeof pending);
    cutoff_children(op);                                 /* bnci_ctour's FUN_180044170: a separate Tab Cutoff op */
    scene_reupload();
    sel = op->op_index; msel_owner = -9; sim = -1;
    cam_msg[0] = 0;                                     /* generated: no message (Mastercam shows the toolpath only) */
    show_params = 0; cam_mode = 0; cam_clear_selection();
    pls_open = 0; upl_open = 0;
}

/* the machine group's "Post with Mastercam post (.pst)" (ours, user decision P2; F.grp_mppost, Files page): every operation
   through the group's post (CCNCMachine::PostProcFile, F.mach_post; %MCAMDIR% = paths.conf shared, P1) with the MP engine
   (viewer/mp/mp_post.c = mp_cli's chain: nci_ascii -> mp_run_nci). The .pst reads each op's NCI: Mastercam's stored one, a
   reversed one (nci_reverse.c), or - for an op made or edited here - the NCI viewer/nci_write.c writes from its moves as mccore's
   bnci_wr_* writers do (user decision Q23; nciw_view: the OP_ID record the save would write + the section). Refused: an op whose
   tool changed after Mastercam computed it (nc_post.c's rule), an op nci_write refuses (drill canned cycles: NOT DECODED).
   Q24: one program per NC file name (op +0x170, grouped as bnci_to_nci groups them), named as Mastercam's post names it -
   build_filename(the NCI name, NC folder, the control's NC suffix) (nci_write.c nciw_nc_name / nciw_check_suffix); the folder is
   the part's (ours: the control's NC path is a Windows path); no NC suffix read from the control: the LinuxCNC post's name
   <part>-mcamui.nc (ours). An existing file is replaced (Mastercam asks: ours has no prompt). Line ends "\n" unless the post /
   control says otherwise (P3); mill and router machines only (P4). ONLY = dialog 977's "Do not regenerate" mask (cui_dirty.c;
   NULL = every op). */
static int nc_target(char *out, size_t outn, const char *suffix);
static void post_ops_mp(const unsigned char *only) {
    #define MP_REFUSE(...) do { message("Post", __VA_ARGS__); return; } while (0)
    if (!F.mach_ok || !F.mach_post[0])
        MP_REFUSE("Not posted: the machine group has no post (.pst) - clear \"Post with Mastercam post (.pst)\" in Machine Group Properties > Files to post with the LinuxCNC post.");
    if (F.mach_type != 1 && F.mach_type != 3)             /* CCNCMachine::Type: mill 1, router 3 (mcam_load.h); lathe / wire: P4 */
        MP_REFUSE("Not posted: the Mastercam post is used for mill and router machines only (machine type %d).", F.mach_type);
    char pst[1100]; host_path(F.mach_post, pst, sizeof pst);
    if (!file_exists(pst)) MP_REFUSE("Not posted: the post %s was not found (%s; %%MCAMDIR%% = paths.conf shared).", F.mach_post, pst);
    for (int i = 0; i < F.nops; i++) {
        if (only && i < 1024 && !only[i]) continue;      /* left out by dialog 977's "Do not regenerate" */
        int mine = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) mine = 1;
        if (!mine && F.ops[i].stale) MP_REFUSE("Not posted: operation %d's toolpath was made for a tool that has since been changed - regenerate it first.", i + 1);
    }
    NciwView v; char verr[600] = "";
    if (!nciw_view(&F, camops, ncamops, tp_metric(), &v, verr, sizeof verr)) MP_REFUSE("Not posted: %s.", verr);
    #undef MP_REFUSE
    /* the NC files: op +0x170 groups in tree order (nci_ascii.c: _mbsicmp of the names); disabled ops (op +0xe8) are skipped */
    char sfx[0x110] = ""; nciw_check_suffix(F.ctl_ncsuffix, sfx, sizeof sfx);
    char dir[1100]; int dl = path_dirlen(F.path); snprintf(dir, sizeof dir, "%.*s", dl, F.path);   /* "" = the current folder */
    int *grp = malloc(sizeof(int) * (size_t)(F.nops + 1)), *ops = malloc(sizeof(int) * (size_t)(F.nops + 1)), ng = 0;
    for (int i = 0; i < F.nops; i++) grp[i] = -1;
    char names[600] = ""; int nposted = 0, nmsg_all = 0; char last_all[300] = "";
    for (int i = 0; i < F.nops; i++) {
        const Op *o = &v.G.ops[i];
        if (only && i < 1024 && !only[i]) continue;      /* 977 "Do not regenerate" */
        if (grp[i] >= 0 || !o->raw || o->raw_n <= 0x1d8 + 0x104 || o->raw[0x150]) continue;
        char ni[0x106]; memcpy(ni, o->raw + 0x1d8, 0x104); ni[0x104] = 0;
        int nop = 0;
        for (int k = i; k < F.nops; k++) {
            const Op *q = &v.G.ops[k]; char nk[0x106];
            if (!q->raw || q->raw_n <= 0x1d8 + 0x104 || q->raw[0x150] || grp[k] >= 0 || (only && k < 1024 && !only[k])) continue;
            memcpy(nk, q->raw + 0x1d8, 0x104); nk[0x104] = 0;
            if (strcasecmp(ni, nk)) continue;
            grp[k] = ng; ops[nop++] = k;
        }
        ng++;
        char out[1200], tmp[1300];
        if (F.ctl_ok && F.ctl_ncsuffix[0] && ni[0]) nciw_nc_name(o->raw, o->raw_n, dir, sfx, out, sizeof out);
        else { const char *b = path_base(F.path); char base[256]; snprintf(base, sizeof base, "%s", b); char *dot = strrchr(base, '.'); if (dot) *dot = 0;
               snprintf(out, sizeof out, "%s%s%s-mcamui.nc", dir, dir[0] ? "/" : "", base); }
        if (!nc_target(out, sizeof out, sfx[0] ? sfx : ".nc")) continue;   /* N1: Cancel skips this program, no message */
        snprintf(tmp, sizeof tmp, "%s.tmp", out);   /* the previous program is replaced only by a complete one */
        FILE *fo = fopen(tmp, "wb");                 /* binary: the post's line ends (MpEnv.eol) */
        if (!fo) { message("Post", "Not posted: cannot write %s", tmp); break; }
        MpPostReq q = { pst, out, "\n", tp_metric(), F.mach_type == 3 ? 1 : 0, ops, nop };   /* IsMetric() (tp_metric); product: router 1 */
        char perr[600] = "", last[300] = ""; int nmsg = 0;
        int okw = mp_post_part(&v.G, &q, fo, &nmsg, last, sizeof last, perr, sizeof perr);
        int wfail = fflush(fo) != 0 || file_sync(fo) != 0; if (fclose(fo) != 0) wfail = 1;
        if (!okw) { remove(tmp); message("Post", "Not posted (%s): %s.", path_base(out), perr); break; }
        if (wfail || file_replace(tmp, out) != 0) { remove(tmp); message("Post", "Writing %s failed: %s", out, strerror(errno)); break; }
        nposted += nop; nmsg_all += nmsg; if (last[0]) snprintf(last_all, sizeof last_all, "%s", last);
        size_t L = strlen(names); snprintf(names + L, sizeof names - L, "%s%s", L ? ", " : "", out);
    }
    free(grp); free(ops);
    const char *pn = path_sep_r(pst) ? path_sep_r(pst) + 1 : pst;
    if (names[0]) {
        if (nmsg_all) snprintf(cam_msg, sizeof cam_msg, "posted %d operation(s) to %s (Mastercam post %s; %d post message(s), last: %.120s)", nposted, names, pn, nmsg_all, last_all);
        else snprintf(cam_msg, sizeof cam_msg, "posted %d operation(s) to %s (Mastercam post %s)", nposted, names, pn);
    }
    nciw_view_free(&v);
}

/* N1 (user decision, formats/align_geo.md 4.2): the NC file of a program. The control's post_dlg_settings ([2] keep the NC file, [3] Overwrite;
   2017 parts: page 2 +0xdc9, mcam_load ctl_post; X9 parts NOT DECODED -> default_post_settings: NC kept, Ask): Overwrite and a writable
   folder -> OUT as it is; Ask (or a folder that cannot be written, FUN_1800201f0) -> PromptForFileToSave = a Save As dialog on OUT (filter
   "<SUFFIX> Files" + "All Files (*.*)", OFN_OVERWRITEPROMPT: the platform asks before replacing a file). 1 = post to OUT, 0 = Cancel: that
   program is skipped, no message (CBatch::PostSelOps FUN_180023500 l.25854-25880). The chosen folder becoming the group's NC folder
   (SetNCPrefix): NOT PORTED (ours: NC files go to the part's folder, N2). */
static int nc_target(char *out, size_t outn, const char *suffix) {
    int keep = F.ctl_post_ok ? F.ctl_post[2] != 0 : 1, ow = F.ctl_post_ok ? F.ctl_post[3] != 0 : 0;
    if (!keep) return 1;                                  /* send-only / NC not kept: not asked (the post's own path) */
    char dir[1100]; snprintf(dir, sizeof dir, "%s", out); { char *q = (char *)path_sep_r(dir); if (q) *q = 0; else snprintf(dir, sizeof dir, "."); }
    char probe[1200]; snprintf(probe, sizeof probe, "%s/.mcamui_w%d", dir, (int)getpid());
    FILE *t = ow ? fopen(probe, "w") : NULL; int writable = t != NULL; if (t) { fclose(t); remove(probe); }
    if (ow && writable) return 1;
    if (!cui_hooks.ask_save_path) return cui_script_yes;  /* tests: the default name accepted (askyes:0 = Cancel) */
    return cui_hooks.ask_save_path(out, outn, suffix);
}
void post_ops(void) {
    if (!F.nops) { snprintf(cam_msg, sizeof cam_msg, "nothing to post: the part has no operations"); return; }
    /* COpMgrTree::PostSelected -> DirtyOperationWarning (dialog 977, cui_dirty.c): a dirty op holds the post until it is answered */
    if (dop_check(DOP_POST)) return;
    unsigned char only_m[1024]; const unsigned char *only = dop_post_only(only_m);   /* 977 "Do not regenerate": ops without a toolpath left out */
    if (F.grp_mppost) { post_ops_mp(only); return; }    /* the Mastercam post (ours, P2): its own NC files (Q24) */
    char out[1100], base[256];
    extern char cad_doc_name[]; const char *fp = F.path[0] ? F.path : cad_doc_name;   /* an imported file: its name (geo_filename) */
    const char *b = path_sep_r(fp) ? path_sep_r(fp) + 1 : fp;
    snprintf(base, sizeof base, "%s", b); char *dot = strrchr(base, '.'); if (dot) *dot = 0;
    int dl = (int)(b - fp);
    snprintf(out, sizeof out, "%.*s%s-mcamui.nc", dl, fp, base);
    if (!nc_target(out, sizeof out, ".nc")) { cam_msg[0] = 0; return; }   /* N1: Cancel - not posted, no message */
    char tmp[1200]; snprintf(tmp, sizeof tmp, "%s.tmp", out);   /* the previous program is replaced only by a complete one */
    const char *mode = "w";
    FILE *fo = fopen(tmp, mode);
    if (!fo) { snprintf(out, sizeof out, "%s-mcamui.nc", base); snprintf(tmp, sizeof tmp, "%s.tmp", out); fo = fopen(tmp, mode); }
    if (!fo) { snprintf(cam_msg, sizeof cam_msg, "cannot write %s", tmp); return; }
    for (char *q = base; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
    /* the LinuxCNC post (nc_post.c): every operation in tree order - Mastercam's from its stored toolpath, ours from
       the generated moves; MCAM_POST_STYLE=ij-quad selects the older program style */
    pv_set_machine_fallback(tl6410_mach);                 /* batch 11: a new part's coolant style = the defaults library's machine (cam_postvals.c) */
    NcPostOpts po = { getenv("MCAM_POST_STYLE") ? getenv("MCAM_POST_STYLE") : "r180", base, (const char *)only };
    char perr[200]; int okw = nc_post_part(fo, &F, camops, ncamops, &po, perr, sizeof perr);
    int wfail = fflush(fo) != 0 || file_sync(fo) != 0; if (fclose(fo) != 0) wfail = 1;
    if (!okw) { remove(tmp); message("Post", "Not posted: %s.", perr); return; }
    if (wfail || file_replace(tmp, out) != 0) { remove(tmp); message("Post", "Writing %s failed: %s", out, strerror(errno)); return; }
    snprintf(cam_msg, sizeof cam_msg, "posted %d operation(s) to %s (LinuxCNC post)", okw, out);
}

/* an edited tool (the Tool Manager / Define Tool wizard: Cnc::Tool::AssemblyChangedProcessor FUN_180a449c0 -> FUN_180a45440,
   decomp/dirty/core_tooldirty.c): every op using it whose tool is IsDifferentEnoughToDirtyOp (l.351, 422) gets record +0xfc = 1
   (op +0x94, dirty) and keeps its toolpath; other changes go through BnciUpdate::Update (feeds / numbers patched in the NCI, dirty
   only when that fails, l.598-601, 709-715). Not regenerated (Mastercam regenerates on Regenerate). Ours: TlTool::DifferentEnoughToDirtyOp
   per tool type is NOT DECODED - the shape fields this program's engines read (diameter, type, corner radius, tip angle) stand for
   it for ops made here; a stored op with this tool is marked whatever changed (as before: its toolpath was made for the old tool). */
void regen_ops_for_tool(int num) {
    const Tool *t = mcam_tool(&F, num); if (!t) return;
    for (int k = 0; k < ncamops; k++) {
        CamOp *op = camops[k];
        if (op->p.tool_num != num) continue;
        if (op->p.tool_dia == t->dia && op->p.tool_type == t->type && op->p.corner_r == t->corner_r && op->p.tip_angle == t->tip_angle) continue;
        op->dirty |= CAM_DIRTY_TOOL;                              /* regenerated with the tool's shape (cui_dirty.c tool_refresh) */
    }
    for (int i = 0; i < F.nops; i++) {                            /* Mastercam's stored ops: their toolpath was made for the old tool */
        if (F.ops[i].tool != num) continue;
        int mine = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) mine = 1;
        if (mine) continue;
        F.ops[i].stale = 1;                                       /* dirty (op +0x94: tree image 0x13 / 0x18, InitTvItem -2); the post refuses it unregenerated (ours) */
    }
}

Chain chain_dup(const Chain *c) {
    Chain d = *c;
    d.ent = malloc((c->n ? c->n : 1) * sizeof(int)); d.rev = malloc((c->n ? c->n : 1) * sizeof(int));
    memcpy(d.ent, c->ent, c->n * sizeof(int)); memcpy(d.rev, c->rev, c->n * sizeof(int));
    return d;
}

/* S4 (cui_chain.c, msurf_assoc l.15965-16244): the new 3D op kept without a toolpath - the 427 question's Yes, message 241's case.
   APPLIED: the op dialog was OK'd (its pages applied as OK applies them, gen_begin); else (its Cancel) the op as the dialog opened -
   SEED / SEED_TOOL. Mastercam's op is dirty (rewrite_ent_assoc: op +0x94 = 1, l.19919-19925) and has no NCI */
void ms_keep_op(int applied, const CamParams *seed, int seed_tool) {
    if (applied == 1) {                                  /* 2 = pending is the applied op already (a failed generation, gen_end) */
        CamOp job; if (!gen_begin(&job)) { pending_free(); show_params = 0; cam_mode = 0; cam_clear_selection(); return; }
        pending.m = NULL; gen_free_parts(&pending); pending = job;
    } else if (!applied) {
        if (seed) pending.p = *seed;
        tool_choice = seed_tool;
        if (tool_choice >= 0 && tool_choice < F.ntools) { pending.p.tool_num = F.tools[tool_choice].num; pending.p.tool_dia = F.tools[tool_choice].dia; }
    }
    free(pending.m); pending.m = NULL; pending.nm = 0; pending.dirty = CAM_DIRTY_PARAMS; pending.edit = NULL;
    edit_camop = -1;
    gen_commit();
}
/* the new 3D op deleted (TpPartOpList::DeleteListAndDB, LAB_1800175a6): nothing of it stays */
void ms_drop_op(void) {
    pending_free(); edit_camop = -1;
    show_params = 0; cam_mode = 0; cam_clear_selection(); pls_open = 0; upl_open = 0;
}
void pending_free(void) {
    for (int i = 0; i < pending.nchains; i++) cam_chain_free(&pending.chains[i]);
    free(pending.chains); free(pending.holes); free(pending.m);
    memset(&pending, 0, sizeof pending);
}

/* Parameters of an operation: ops created here open the params dialog filled in (Regenerate replaces the op);
   toolpaths stored in the part show what is known about them, read-only */
void open_op_params(int op_index) {
    pv_clear();                                          /* Q20: a new dialog has no preview */
    int k = -1;
    for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == op_index) k = j;
    if ((k >= 0 && camops[k]->p.type == CAM_LFACE) || (k < 0 && op_index >= 0 && op_index < F.nops && cam_fileop_type(&F.ops[op_index]) == CAM_LFACE)) {
        /* Lathe Face (viewer/lathe_face.c): regenerated from its record only; its parameter pages (mclathe CLFaceProp) are not ported */
        snprintf(cam_msg, sizeof cam_msg, "operation %d: Lathe Face parameters are not editable here (the lathe pages are NOT PORTED); Regenerate only", op_index + 1);
        printf("    (%s)\n", cam_msg); return;
    }
    if (k < 0 && op_dirty_edit(op_index)) {             /* a stored op marked dirty with changed parameters: the dialog shows
                                                            them (Mastercam's op holds them), on its record with them applied */
        const CamOp *ed = op_dirty_edit(op_index); const Op *so = &F.ops[op_index];
        static uint8_t *prec; static uint32_t prec_n;      /* pp_rec points here while the dialog is open */
        free(prec); prec_n = so->raw_n; prec = malloc(prec_n ? prec_n : 1); if (so->raw) memcpy(prec, so->raw, prec_n);
        for (int r = 0; r < ed->p.nrpatch; r++) if ((uint32_t)ed->p.rpatch[r].off + ed->p.rpatch[r].n <= prec_n) memcpy(prec + ed->p.rpatch[r].off, ed->p.rpatch[r].v, ed->p.rpatch[r].n);
        pending_free(); cam_clear_selection();
        CamOp *c = camop_clone(ed); pending = *c; free(c); pending.edit = NULL; pending.dirty = 0;
        free(pending.m); pending.m = NULL; pending.nm = 0;
        {   double cz = 0; if (pending.nchains && pending.chains[0].n) { const Ent *e0 = &F.ents[pending.chains[0].ent[0]]; cz = e0->type == E_ARC ? e0->c.z : e0->a.z; }
            pp_hf_record(prec, prec_n, cz); pp_tp_record(prec, prec_n); }
        tool_choice = F.ntools;
        for (int i = 0; i < F.ntools; i++) if (F.tools[i].num == pending.p.tool_num) tool_choice = i;
        for (int i = 0; i < F.ntools; i++) if (F.tools[i].num == pending.p.tool_num && F.tools[i].dia == pending.p.tool_dia && F.tools[i].type == pending.p.tool_type) { tool_choice = i; break; }
        cam_mode = pending.p.type; cam_variant = pending.p.type == CAM_CONTOUR ? pending.p.contour_type : 0;
        edit_camop = -1; show_params = 1; cam_msg[0] = 0; tp_list_open();
        pp_ok_snapshot();
        return;
    }
    if (k < 0) {                                         /* an operation stored in the part: edit it as ours */
        CamOp tmp; char err[200];
        /* an operation this program cannot regenerate (formats/provenance_audit.md): Mastercam would open its
           parameters; nothing here (the earlier read-only info window was ours, removed) */
        if (!cam_op_from_file(&F, op_index, &tmp, err, sizeof err)) { printf("    (op %d not editable here: %s)\n", op_index + 1, err); return; }
        pending_free();                                  /* also what a Cancel / an unchanged OK left (pending owns it until the next open) */
        cam_clear_selection();
        pending = tmp;
        {   /* Linking page flags: the stored record's (cz = the first chain's Z, as params_from_record) */
            double cz = 0; if (tmp.nchains && tmp.chains[0].n) { const Ent *e0 = &F.ents[tmp.chains[0].ent[0]]; cz = e0->type == E_ARC ? e0->c.z : e0->a.z; }
            pp_hf_record(F.ops[op_index].raw, F.ops[op_index].raw_n, cz);
            pp_tp_record(F.ops[op_index].raw, F.ops[op_index].raw_n);   /* the Tool page's switches: the stored record's */
        }
        /* the op's own tool, as cam_op_from_file took it (cam_op_tool_index: op_tool - slot, then number); by number alone
           a part with two tools of one number gave the dialog (and generate_pending's tool_dia) the other one */
        { int ti = cam_op_tool_index(&F, op_index); tool_choice = ti >= 0 ? ti : F.ntools; }
        cam_mode = pending.p.type; cam_variant = pending.p.type == CAM_CONTOUR ? pending.p.contour_type : 0;
        edit_camop = -1; show_params = 1; tp_list_open();
        cam_msg[0] = 0;                                 /* Mastercam opens the parameters without a message */
        pp_ok_snapshot();
        if (F.ops[op_index].npts <= 0 || F.ops[op_index].stale) pp_ok_forget();   /* ours: no (valid) toolpath - OK generates it */
        return;
    }
    pending_free();                                      /* as above */
    cam_clear_selection();
    const CamOp *src = camops[k]->edit ? camops[k]->edit : camops[k];   /* dirty with changed parameters: those (cui_dirty.c) */
    memset(&pending, 0, sizeof pending);
    pending.p = src->p; snprintf(pending.name, sizeof pending.name, "%s", src->name);
    pending.chains = calloc(src->nchains ? src->nchains : 1, sizeof(Chain));
    for (int i = 0; i < src->nchains; i++) pending.chains[i] = chain_dup(&src->chains[i]);
    pending.nchains = src->nchains;
    pending.holes = malloc((src->nholes ? src->nholes : 1) * sizeof(Hole)); memcpy(pending.holes, src->holes, src->nholes * sizeof(Hole));
    pending.nholes = src->nholes;
    pp_hf_op(src);
    {   /* Tool page switches: the record it was saved from (the defaults op, mcam_saveops.c), then its own patches */
        static const uint8_t z[0x300]; const Op *d = default_op(src->p.type, mcam_is_metric(&F));
        if (d) pp_tp_record(d->raw, d->raw_n); else pp_tp_record(z, sizeof z);
        pp_tp_op(src); }
    tool_choice = F.ntools;
    for (int i = 0; i < F.ntools; i++) if (F.tools[i].num == src->p.tool_num) tool_choice = i;
    /* two tools with one number (NAKTO-BRAKE-BRACKET T2 6 mm / 3 mm): the one this op was generated with (its diameter /
       type), not the last of that number - generate_pending takes tool_dia from tool_choice */
    for (int i = 0; i < F.ntools; i++) if (F.tools[i].num == src->p.tool_num && F.tools[i].dia == src->p.tool_dia && F.tools[i].type == src->p.tool_type) { tool_choice = i; break; }
    cam_mode = src->p.type; cam_variant = src->p.type == CAM_CONTOUR ? src->p.contour_type : 0;
    edit_camop = k; show_params = 1; tp_list_open();
    cam_msg[0] = 0;
    pp_ok_snapshot();
    if (camops[k]->nm <= 0) pp_ok_forget();              /* ours: no toolpath - OK generates it */
}

/* re-add all ops created here to the model in camops order (keeps the tree order after a regenerate) */
void readd_camops(void) {             /* rebuild our ops' F.ops entries (new moves) in their tree positions */
    int ord[MAX_CAMOPS], n = 0;
    for (int j = 0; j < ncamops; j++) if (camops[j]->op_index >= 0) ord[n++] = j;
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++)
        if (camops[ord[b]]->op_index < camops[ord[a]]->op_index) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
    for (int a = n - 1; a >= 0; a--) mcam_remove_op(&F, camops[ord[a]]->op_index);   /* highest index first */
    for (int a = 0; a < n; a++) {                                    /* lowest first: each goes back where it was */
        CamOp *c = camops[ord[a]]; int at = c->op_index;
        int k = mcam_add_op(&F, cam_type_label(c->p.type), c->name, c->p.tool_num, c->m, c->nm);
        if (at > k) at = k;
        mcam_move_op(&F, k, at); c->op_index = at;
    }
    for (int j = 0; j < ncamops; j++) if (camops[j]->op_index < 0)  /* not placed yet: at the end */
        camops[j]->op_index = mcam_add_op(&F, cam_type_label(camops[j]->p.type), camops[j]->name, camops[j]->p.tool_num, camops[j]->m, camops[j]->nm);
}
/* the tree position of stored op ID that is about to be taken over (-1: none / already one of ours) */
int takeover_index(int id) {
    for (int i = 0; i < F.nops; i++) if (F.ops[i].id == id) {
        for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) return -1;
        return i;
    }
    return -1;
}
/* add OP's moves to F.ops at tree position AT (-1 = the end), keeping the program order = the tree order */
void add_op_at(CamOp *op, int at) {
    int k = mcam_add_op(&F, cam_type_label(op->p.type), op->name, op->p.tool_num, op->m, op->nm);
    if (at < 0 || at > k) at = k;
    if (at != k) { mcam_move_op(&F, k, at); for (int j = 0; j < ncamops; j++) if (camops[j] != op && camops[j]->op_index >= at) camops[j]->op_index++; }
    op->op_index = at;
}


/* apply FN to every selected operation (Edit Common Parameters, Recalculate feeds/speeds). FN returns 0 to leave an op as it is.
   Dirty operations (formats/opdlg_port_audit.md "Dirty operations"): an op FN did not change is left alone; a changed op is marked
   dirty with the change as its edit (cui_dirty.c op_mark_dirty_edit; its toolpath stays) when EDIT_SEL_DIRTY is set
   (list_op_recalc_sf: "changed ops marked dirty", formats/feeds_speeds.md) or the change is more than BnciUpdate patches
   (Edit Common Parameters FUN_180145b50: bnci_update, else op +0x94, formats/opmanager_menu.md; bnci_mask above); else (the NCI
   patch, NOT PORTED) it is regenerated at once as before - ops made here as copies (undo keeps the old), stored ones taken over
   as ops of this program. An op already dirty with an edit starts from that edit. Returns the count edited; *skipped = ops left
   alone. */
int edit_sel_dirty;                                  /* set by the caller for one edit_selected run */
static int edit_changes(const CamParams *was, const CamParams *now, int *dirties) {
    CamParams *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    memcpy(a, was, sizeof *a); memcpy(b, now, sizeof *b);
    int changed = memcmp(a, b, sizeof *a) != 0;
    bnci_mask(a); bnci_mask(b);
    *dirties = edit_sel_dirty ? changed : memcmp(a, b, sizeof *a) != 0;
    free(a); free(b);
    return changed;
}
int edit_selected(int (*fn)(CamOp *op, const uint8_t *raw, uint32_t rawn, void *ctx), void *ctx, int *skipped) {
    int nskip = 0, mode_dirty = edit_sel_dirty;
    /* the selected ops by identity: indices move while stored ops are taken over */
    CamOp *mine[MAX_CAMOPS]; int nmine = 0, ids[1024], nids = 0;
    for (int i = 0; i < F.nops; i++) if (op_selected(i)) {
        int k = -1; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) k = j;
        if (k >= 0) { if (nmine < MAX_CAMOPS) mine[nmine++] = camops[k]; } else if (nids < 1024) ids[nids++] = F.ops[i].id;
    }
    CamOp *done[MAX_CAMOPS + 1024]; int ndone = 0, dirt[1024], ndirt = 0, nunch = 0;   /* dirt: stored op ids marked dirty (their index stays) */
    for (int a = 0; a < nmine; a++) {                         /* ops made here: edited copies replace them (undo keeps the old) */
        int k = -1; for (int j = 0; j < ncamops; j++) if (camops[j] == mine[a]) k = j;
        if (k < 0) continue;
        const CamOp *base = camops[k]->edit ? camops[k]->edit : camops[k];
        if (base->p.type == CAM_LFACE) { nskip++; continue; }   /* Lathe Face: no parameter edits here (regenerate only) */
        CamOp *op = camop_clone(base); cam_op_free(op->edit); op->edit = NULL; op->dirty = 0;
        if (!fn(op, NULL, 0, ctx)) { cam_op_free(op); nskip++; continue; }
        int dirties = 0;
        if (!edit_changes(&base->p, &op->p, &dirties)) { cam_op_free(op); nskip++; nunch++; done[ndone++] = camops[k]; continue; }   /* unchanged: nothing (Mastercam: no dirty, no patch); stays selected */
        if (dirties) { op_mark_dirty_edit(camops[k]->op_index, op); done[ndone++] = camops[k]; continue; }
        free(op->m); op->m = NULL; op->nm = 0;
        cam_set_prev_ops_fn(cui_prev_ops); pp_engrave_ask(op); if (!cam_generate(&F, op)) { snprintf(cam_msg, sizeof cam_msg, "%s: %s", cam_op_label(op), op->error); cam_op_free(op); continue; }
        CamOp *old = camops[k]; op->op_index = old->op_index; camops[k] = op; old->op_index = -1;
        cam_op_free(old);                                 /* not on Undo's stack */
        done[ndone++] = op;
    }
    readd_camops();
    for (int a = 0; a < nids; a++) {                          /* stored in the part: taken over as ops of this program */
        int i = -1; for (int j = 0; j < F.nops; j++) if (F.ops[j].id == ids[a]) i = j;
        if (i < 0) continue;
        CamOp tmp; char err[200];
        const CamOp *ed = op_dirty_edit(i);
        if (ed) { CamOp *c = camop_clone(ed); tmp = *c; free(c); }
        else if (cam_fileop_type(&F.ops[i]) == CAM_LFACE) { nskip++; continue; }   /* Lathe Face: no parameter edits here (regenerate only) */
        else if (!cam_op_from_file(&F, i, &tmp, err, sizeof err)) { nskip++; continue; }   /* not regenerable here: left as stored */
        CamParams *was = malloc(sizeof *was); memcpy(was, &tmp.p, sizeof *was);
        if (!fn(&tmp, F.ops[i].raw, F.ops[i].raw_n, ctx)) {   /* a stack CamOp: free its arrays only (cam_op_free frees the struct too:
                                                                 crashed both apps when Recalculate skipped a stored op, phase 8 fix) */
            for (int ci = 0; ci < tmp.nchains; ci++) cam_chain_free(&tmp.chains[ci]);
            free(tmp.chains); free(tmp.holes); free(tmp.m); free(was); nskip++; continue; }
        int dirties = 0, changed = edit_changes(was, &tmp.p, &dirties); free(was);
        CamOp *op = malloc(sizeof *op); *op = tmp;
        if (!changed) { cam_op_free(op); nskip++; nunch++; if (ndirt < 1024) dirt[ndirt++] = ids[a]; continue; }   /* unchanged: stays selected */
        if (dirties) { op_mark_dirty_edit(i, op); if (ndirt < 1024) dirt[ndirt++] = ids[a]; continue; }
        cam_set_prev_ops_fn(cui_prev_ops); pp_engrave_ask(op); if (!cam_generate(&F, op)) { snprintf(cam_msg, sizeof cam_msg, "%s: %s", cam_op_label(op), op->error); cam_op_free(op); continue; }
        { int at = op->p.file_op_id > 0 ? takeover_index(op->p.file_op_id) : -1;   /* taken over in place */
          remove_stored_op(op->p.file_op_id);
          add_op_at(op, at); }
        if (ncamops < MAX_CAMOPS) { camops[ncamops++] = op; done[ndone++] = op; }   /* ops are not on Undo's stack */
    }
    /* the edited ops stay selected */
    memset(op_msel, 0, sizeof op_msel); sel = -1;
    for (int a = 0; a < ndone; a++) if (done[a]->op_index >= 0 && done[a]->op_index < 1024) { op_msel[done[a]->op_index] = 1; if (sel < 0) sel = done[a]->op_index; }
    for (int a = 0; a < ndirt; a++) for (int i = 0; i < F.nops && i < 1024; i++) if (F.ops[i].id == dirt[a]) { op_msel[i] = 1; if (sel < 0 || i < sel) sel = i; }
    msel_owner = sel; sim = -1;
    if (ndone + ndirt - nunch) { doc_dirty = 1; scene_reupload(); }
    if (skipped) *skipped = nskip;
    edit_sel_dirty = mode_dirty = 0; (void)mode_dirty;
    return ndone + ndirt - nunch;
}

/* ---- feeds / speeds (formats/feeds_speeds.md: mccore calc_op_fs, list_op_recalc_sf; mcmill CToolProp) */
static MatLib fs_lib; static int fs_lib_ok;
FsGroup fs_group(void) {
    FsGroup g = { F.have_grp ? F.grp_fs_mode : FS_TOOL, F.grp_rpm, F.grp_feed, F.grp_plunge, F.grp_retract };
    return g;
}
/* the machine group's material (GetMaterialFromName: the part's own list - none in the parts seen, NOT DONE - then the
   machine definition's material library); NULL + WHY when it cannot be found */
const Material *fs_material(char *why, size_t n) {
    if (!F.have_grp || !F.grp_material[0] || !strcasecmp(F.grp_material, "NONE")) { snprintf(why, n, "the machine group has no material"); return NULL; }
    if (!F.matlib[0]) { snprintf(why, n, "no material library"); return NULL; }   /* internal reason, not shown */
    char p[1024], err[300]; matlib_resolve(F.matlib, p, sizeof p);
    if (!fs_lib_ok || strcmp(fs_lib.path, p)) {
        if (fs_lib_ok) matlib_free(&fs_lib);
        fs_lib_ok = matlib_load(p, &fs_lib, err, sizeof err);
        if (!fs_lib_ok) { snprintf(why, n, "material library %s: %s", F.matlib, err); return NULL; }
    }
    const Material *m = matlib_find(&fs_lib, F.grp_material);
    if (!m) snprintf(why, n, "material '%s' is not in %s", F.grp_material, p);
    return m;
}
typedef struct { const Material *m; FsGroup g; int nfail; char first[200]; } RecalcCtx;
int recalc_edit_fn(CamOp *op, const uint8_t *raw, uint32_t rawn, void *ctx) {
    RecalcCtx *rc = ctx; int style, opcode = mcam_opcode_for(&op->p, &style);
    const Tool *t = mcam_tool(&F, op->p.tool_num);
    FsResult r = { 0, 0, 0, 0, 0 };
    if (opcode && t) r = fs_calc(opcode, op->p.rpm, op->p.feed, t, &rc->g, rc->m, 0, 0, op->p.rpm < 0, 0);
    if (!r.ok) {
        if (!rc->nfail++) snprintf(rc->first, sizeof rc->first, "%s: %s", cam_op_label(op), !t ? "its tool is not in the part" : "no valid feed / speed (e.g. tool flutes 0, material values 0)");
        return 0;
    }
    CamCommonEdit ce; memset(&ce, 0, sizeof ce);
    ce.on_rpm = 1; ce.rpm = r.rpm; ce.on_feed = 1; ce.feed = r.feed; ce.on_plunge = 1; ce.plunge = r.plunge; ce.on_rrate = 1; ce.rrate = r.retract;
    cam_common_apply(&F, op, &ce, raw, rawn);
    return 1;
}
/* Toolpaths > Recalculate feeds/speeds (list_op_recalc_sf): calc_op_fs for every selected op with the group's mode and
   material, clamp on, CW; changed ops marked dirty (cui_dirty.c; regenerated with Regenerate). Ops with no valid result are left
   as they are (feeds_speeds.md). */
void recalc_feeds(void) {
    RecalcCtx rc; memset(&rc, 0, sizeof rc); rc.g = fs_group();
    char why[400] = "";
    /* calc_op_fs mode 1 with a material that cannot be found: nothing changed (feeds_speeds.md); its "show" flag, which
       adds message mccoreres 4038 "Your default work piece material does not exist!" and the material picker, is not
       traced for this caller */
    if (rc.g.mode == FS_MATERIAL && !(rc.m = fs_material(why, sizeof why))) return;
    edit_sel_dirty = 1;                                  /* list_op_recalc_sf: changed ops are marked dirty */
    int skipped = 0, n = edit_selected(recalc_edit_fn, &rc, &skipped);
    (void)n;   /* RecalcSpeedsAndFeeds says nothing about ops left unchanged (the invented summary box is gone, item 17) */
}
/* Toolpaths > Edit selected operations > Reverse toolpath (COpMgrTree::ReversePath(1), formats/reverse_toolpath.md 1):
   bnci_reverse on each selected operation's stored NCI (nci_reverse.c), in tree order, then a refresh. The NCI is what
   is reversed - not the chains - so operations made or edited here, whose moves are regenerated from their
   parameters and which have no NCI, are left as they are. Multiaxis ops are skipped silently; a subprogram op gets
   Mastercam's warning 2911; an op the core cannot reverse stays unchanged. ReversePath pushes no undo record (the
   spec has none): reversing again is the way back (not exact: spec 3.5 raises rapids). */
void reverse_selected(void) {
    int nrev = 0, nleft = 0; char first[300] = "";
    for (int i = 0; i < F.nops; i++) if (op_selected(i)) {
        int mine = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) mine = 1;
        if (mine) { nleft++; if (!first[0]) snprintf(first, sizeof first, "operation %d is generated here from its parameters (no NCI to reverse)", i + 1); continue; }
        char msg[300]; int r = nci_reverse_op(&F, i, msg, sizeof msg);
        if (r > 0) nrev++;
        if (r == 0) nleft++;
        if (r == 0 && !strcmp(msg, NCI_REV_SUBPGM_MSG)) message("Mastercam", "%s", msg);   /* post_warning(0xb5f) */
        else if (r >= 0 && msg[0] && !first[0]) snprintf(first, sizeof first, "%s", msg);
    }
    if (nrev) { doc_dirty = 1; sim = -1; scene_reupload(); }
    (void)nleft;   /* ReversePath only refreshes the tree: no summary (item 17) */
}
/* toolpath dialog, tool chosen (CToolProp::SetFeedsAndSpeedsFromSource with the group's Feed Calculation):
   0 from the tool, 1 from material (calc_op_fs, then plunge = retract = feed x 0.5), 2 from defaults (the values the
   op got from the defaults file stay), 3 user defined: unchanged */
void fs_from_source(CamParams *p, const Tool *t) {
    FsGroup g = fs_group();
    /* GetFeedsAndSpeedsFromTool (mccore 0x180884bf0): feed / plunge / retract / rpm of the tool's op_tool_info (+0x30 /
       +0x38 / +0x40 / +0x48); the CCW sign from tp_tool +0x2c0 is not mapped to a Tool field here (NOT DECODED) */
    if (g.mode == FS_TOOL) { cam_params_from_tool(p, t); if (t->retract > 0) p->mc_rrate = t->retract; return; }
    int keep_rpm = p->rpm; double keep_feed = p->feed, keep_plunge = p->plunge_feed;
    cam_params_from_tool(p, t);                                 /* geometry from the tool */
    p->rpm = keep_rpm; p->feed = keep_feed; p->plunge_feed = keep_plunge;
    if (g.mode != FS_MATERIAL) return;
    char why[400]; const Material *m = fs_material(why, sizeof why);
    int style, opcode = mcam_opcode_for(p, &style);
    if (!m) return;                                             /* material not found: nothing changed (calc_op_fs) */
    FsResult r = fs_calc(opcode, p->rpm, p->feed, t, &g, m, 0, 0, p->rpm < 0, 0);
    if (!r.ok) return;                                          /* no valid result (rpm: D < tol fails): unchanged */
    p->rpm = r.rpm; p->feed = r.feed; p->plunge_feed = r.feed * 0.5; p->mc_rrate = r.feed * 0.5;   /* GetFeedsAndSpeedsFromMaterial: plunge = retract = feed x 0.5 */
}

int selected_camop(void) {                       /* camops index of the selected tree op, -1 if none / from the part */
    for (int k = 0; sel >= 0 && k < ncamops; k++) if (camops[k]->op_index == sel) return k;
    return -1;
}

CamOp *camop_clone(const CamOp *s);
CamOp *camops_undo_last_deleted(void) { return nundo && undo_st[nundo - 1].kind == U_OP_DEL ? undo_st[nundo - 1].op : NULL; }
/* ---- Toolpaths manager Delete / Undelete / Cut / Copy / Paste (Mastercam: formats/align_ops.md, opman_tree_all.c COpMgrTree::OnDeleteKey
   @180360de0, COperationsArray::DeleteOperation @180308840 / UnbufferOperations @18030d620 / Paste @18030b350). A delete is soft: the ops
   leave the list and the screen into the delete buffer (one delete; Undelete puts all of them back at their places); the buffer is purged
   (made permanent) by the next delete, a new toolpath, a new / opened part. Cut = a soft delete into the cut buffer, pasted once with the
   same ids; Copy keeps references, pasted as new ops any number of times. Ops made here and ops stored in the part alike. */
typedef struct { Op op; Poly *polys; int npolys; CamOp *cam; int at; } BufOp;   /* an op out of the list: the part's Op + its toolpath
                                                                                   polylines (their points stay in F.pts), or ours */
static BufOp *delbuf, *cutbuf; static int ndelbuf, ncutbuf;
static int32_t *copy_ids; static CamOp **copy_cam; static int ncopy;   /* the copy buffer: references (stored op id / our op's clone) */
int32_t *op_gone_ids; int nop_gone;               /* stored ops (part ids) out of the list here: deleted, cut, purged - saved without them */
static void gone_add(int32_t id) { op_gone_ids = realloc(op_gone_ids, sizeof *op_gone_ids * (size_t)(nop_gone + 1)); op_gone_ids[nop_gone++] = id; }
static void gone_del(int32_t id) { for (int i = 0; i < nop_gone; i++) if (op_gone_ids[i] == id) { op_gone_ids[i] = op_gone_ids[--nop_gone]; return; } }
static void fix_polys(void) {                     /* each op's polylines are contiguous (mcam_remove_op's re-find) */
    for (int o = 0; o < F.nops; o++) {
        Op *op = &F.ops[o]; op->first_poly = -1; op->npolys = 0;
        for (int i = 0; i < F.npolys; i++) if (F.polys[i].kind != K_GEOM && F.polys[i].op == op->id) { if (op->first_poly < 0) op->first_poly = i; op->npolys++; }
        if (op->first_poly < 0) op->first_poly = F.npolys;
    }
}
static void shift_camops(int from, int d) { for (int j = 0; j < ncamops; j++) if (camops[j]->op_index >= from) camops[j]->op_index += d; }
/* take tree op I out of the list into B (TpPartOpList::DeleteListAndDB(id, false, false): out of the list and the display) */
static void take_op(int i, BufOp *b) {
    memset(b, 0, sizeof *b); b->at = i;
    int k = -1; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) k = j;
    if (k >= 0) {                                 /* ours: its F entry goes, the CamOp is kept (attach rebuilds it) */
        b->cam = camops[k];
        mcam_remove_op(&F, i);
        memmove(camops + k, camops + k + 1, (size_t)(ncamops - k - 1) * sizeof camops[0]); ncamops--;
        shift_camops(i + 1, -1);
        return;
    }
    b->op = F.ops[i]; int32_t id = b->op.id, n = 0;
    for (int q = 0; q < F.npolys; q++) if (F.polys[q].kind != K_GEOM && F.polys[q].op == id) n++;
    b->polys = malloc(sizeof(Poly) * (size_t)(n ? n : 1));
    int m = 0, w = 0;
    for (int q = 0; q < F.npolys; q++) { if (F.polys[q].kind != K_GEOM && F.polys[q].op == id) b->polys[m++] = F.polys[q]; else F.polys[w++] = F.polys[q]; }
    b->npolys = m; F.npolys = w;
    memmove(F.ops + i, F.ops + i + 1, (size_t)(F.nops - i - 1) * sizeof(Op)); F.nops--;
    shift_camops(i + 1, -1); fix_polys();
    gone_add(id);
}
/* put B back at tree index AT (AddListAndInit) */
static void put_op(BufOp *b, int at) {
    if (at < 0 || at > F.nops) at = F.nops;
    if (b->cam) {
        if (ncamops >= MAX_CAMOPS) { cam_op_free(b->cam); b->cam = NULL; return; }
        int k = mcam_add_op(&F, cam_type_label(b->cam->p.type), b->cam->name, b->cam->p.tool_num, b->cam->m, b->cam->nm);
        shift_camops(at, +1); mcam_move_op(&F, k, at); b->cam->op_index = at;
        int pos = 0; while (pos < ncamops && camops[pos]->op_index < at) pos++;
        memmove(camops + pos + 1, camops + pos, (size_t)(ncamops - pos) * sizeof camops[0]); camops[pos] = b->cam; ncamops++;
        b->cam = NULL; return;
    }
    F.ops = realloc(F.ops, sizeof(Op) * (size_t)(F.nops + 1));
    memmove(F.ops + at + 1, F.ops + at, (size_t)(F.nops - at) * sizeof(Op)); F.ops[at] = b->op; F.nops++;
    shift_camops(at, +1);
    for (int q = 0; q < b->npolys; q++) {
        if (F.npolys == F.cappolys) { F.cappolys = F.cappolys ? F.cappolys * 2 : 1024; F.polys = realloc(F.polys, sizeof(Poly) * (size_t)F.cappolys); }
        F.polys[F.npolys++] = b->polys[q];
    }
    free(b->polys); b->polys = NULL; fix_polys();
    gone_del(b->op.id);
}
static void free_buf(BufOp *b, int n) {           /* the permanent delete (DeleteOperationsFromDb): the op records go at the save.
                                                     mark_op_dependents (dependent ops dirty) and the unused-tool removal: NOT PORTED */
    for (int i = 0; i < n; i++) { if (b[i].cam) cam_op_free(b[i].cam); else { free(b[i].op.raw); free(b[i].op.nci); } free(b[i].polys); }
}
void op_buffers_purge(int mask) {                 /* DeleteBufferedOperations(mask): 1 cut buffer, 2 delete buffer */
    if (mask & 2) { free_buf(delbuf, ndelbuf); free(delbuf); delbuf = NULL; ndelbuf = 0; }
    if (mask & 1) { free_buf(cutbuf, ncutbuf); free(cutbuf); cutbuf = NULL; ncutbuf = 0; }
}
void op_buffers_reset(void) {                     /* a new / opened part: the buffers and the gone list (the old part's) */
    op_buffers_purge(3); nop_gone = 0;
    for (int i = 0; i < ncopy; i++) cam_op_free(copy_cam[i]);
    free(copy_ids); free(copy_cam); copy_ids = NULL; copy_cam = NULL; ncopy = 0;
}
int op_delete_buffered(void) { return ndelbuf; }

static int op_sel_list(int *idx) {                   /* the selected tree ops, ascending */
    int n = 0; for (int i = 0; loaded && i < F.nops; i++) if (op_selected(i)) idx[n++] = i;
    return n;
}
static void after_list_change(void) {
    sel = -1; msel_owner = -9; memset(op_msel, 0, sizeof op_msel); sim = -1; doc_dirty = 1;
    scene_reupload(); overlay_dirty = 1;
    if (cui_hooks.ops_changed) cui_hooks.ops_changed();
}
void op_gone_apply(int32_t id) {                  /* reopened project: a stored op deleted earlier goes (permanently) */
    for (int i = 0; loaded && i < F.nops; i++) {
        if (F.ops[i].id != id) continue;
        int ours = 0; for (int j = 0; j < ncamops; j++) ours |= camops[j]->op_index == i;
        if (ours) continue;
        BufOp b; take_op(i, &b); free_buf(&b, 1); after_list_change(); return;
    }
}
/* OnDeleteKey: the confirmation (Yes default) when "Prompt for toolpath deletion" is on, the previous delete purged, the selected ops
   soft-deleted (highest first so the indexes hold) */
void delete_selected_op(void) {
    if (!loaded) return;
    int *idx = malloc(sizeof(int) * (size_t)(F.nops + 1)), n = op_sel_list(idx);
    if (!n) { free(idx); return; }
    if (op_del_prompt && cui_hooks.ask_yes_no &&
        !cui_hooks.ask_yes_no("Toolpath Manager",                 /* mccoreres 7616 / 8979 (message_box_yes_no(8979, 7616, 2)) */
            "You are about to delete the selected operations.\nUndelete is limited and may not be able to undo this action.\n\n"
            "Do you want to proceed?\n\nThis message can be disabled in the Display Options found\non the right-click menu inside of the Toolpath Manager.")) {
        free(idx); return; }
    op_buffers_purge(2);
    delbuf = calloc((size_t)n, sizeof *delbuf); ndelbuf = n;
    for (int q = n - 1; q >= 0; q--) take_op(idx[q], &delbuf[q]);
    free(idx);
    after_list_change();
    cam_msg[0] = 0;                                   /* no message after a delete */
}
/* UndeleteOperations: every op of the last delete back at its index (index order); empty buffer: nothing */
void op_undelete(void) {
    if (!ndelbuf) { cam_msg[0] = 0; return; }
    for (int q = 0; q < ndelbuf; q++) put_op(&delbuf[q], delbuf[q].at);
    free(delbuf); delbuf = NULL; ndelbuf = 0;
    after_list_change(); cam_msg[0] = 0;
}
/* CutOperations: unless Shift, the previous cut buffer is purged; the ops out of the list into the cut buffer.
   CopyOperations: unless Shift, the cut buffer is purged and the copy buffer cleared; references appended.
   Neither touches the delete buffer. */
void op_copy_ex(int cut, int append) {
    if (!loaded) return;
    int *idx = malloc(sizeof(int) * (size_t)(F.nops + 1)), n = op_sel_list(idx);
    if (!n) { free(idx); return; }
    if (cut) {
        if (!append) op_buffers_purge(1);
        cutbuf = realloc(cutbuf, sizeof *cutbuf * (size_t)(ncutbuf + n));
        for (int q = n - 1; q >= 0; q--) take_op(idx[q], &cutbuf[ncutbuf + q]);
        ncutbuf += n;
        free(idx); after_list_change(); cam_msg[0] = 0; return;
    }
    if (!append) { op_buffers_purge(1); for (int i = 0; i < ncopy; i++) cam_op_free(copy_cam[i]); ncopy = 0; }
    copy_ids = realloc(copy_ids, sizeof *copy_ids * (size_t)(ncopy + n)); copy_cam = realloc(copy_cam, sizeof *copy_cam * (size_t)(ncopy + n));
    for (int q = 0; q < n; q++) {
        int k = -1; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == idx[q]) k = j;
        copy_cam[ncopy] = k >= 0 ? camop_clone(camops[k]) : NULL; copy_ids[ncopy] = F.ops[idx[q]].id; ncopy++;
    }
    free(idx); cam_msg[0] = 0;
}
void op_copy(int cut) { op_copy_ex(cut, 0); }
int op_paste_ready(void) { return ncutbuf > 0 || ncopy > 0; }
/* PasteOperations: the cut buffer wins (same ids, pasted once), else the copy buffer (new ops, repeatable). Target: after the selected op
   [ours: Mastercam pastes at the insert arrow, which this tree does not have]. A copy of a stored op is a new op made here from its
   parameters (cam_op_from_file) and computed by this program's engines [list_op_copy's NCI handling / the copy's dirty state NOT DECODED] */
void op_paste(void) {
    if (!op_paste_ready() || !loaded) { cam_msg[0] = 0; return; }
    int at = sel >= 0 && sel < F.nops ? sel + 1 : F.nops;
    if (ncutbuf) {
        for (int q = 0; q < ncutbuf; q++) put_op(&cutbuf[q], at + q);
        free(cutbuf); cutbuf = NULL; ncutbuf = 0;
        after_list_change(); cam_msg[0] = 0; return;
    }
    for (int q = 0; q < ncopy; q++) {
        CamOp *c = NULL;
        if (copy_cam[q]) c = camop_clone(copy_cam[q]);
        else {
            int i = -1; for (int j = 0; j < F.nops; j++) if (F.ops[j].id == copy_ids[q]) i = j;
            char err[300]; c = calloc(1, sizeof *c);
            if (i < 0 || !cam_op_from_file(&F, i, c, err, sizeof err)) { snprintf(cam_msg, sizeof cam_msg, "operation %d: %s", q + 1, i < 0 ? "no longer in the part" : err); free(c); continue; }
            c->p.file_op_id = 0;
            cam_set_prev_ops_fn(cui_prev_ops); pp_engrave_ask(c); if (!cam_generate(&F, c)) { snprintf(cam_msg, sizeof cam_msg, "%s: %s", cam_op_label(c), c->error); cam_op_free(c); continue; }
        }
        /* list_op_copy (decomp/align/core_oplist.c l.612: a new id from OMgetNextOpId; no write to op +0xd0 in it): a copy of a contour op
           with a separate Tab Cutoff is not the parent of the original's 0x82 op (that op's +0xd0 holds the original's id) - its own
           link starts empty (ct_uid 0); a copy of a Tab Cutoff op keeps its parent (ct_parent_uid), whose next regeneration deletes it
           (FUN_180044170: DetectChildOperationIDs finds every op with +0xd0 = its id). No regeneration on Paste (COperationsArray::Paste
           / ValidatePastedOps call no bnci_*), so no Tab Cutoff op is made for a pasted copy until it is regenerated. */
        op_seq_assign(c); c->p.ct_uid = 0;
        BufOp b; memset(&b, 0, sizeof b); b.cam = c; put_op(&b, at + q);
    }
    after_list_change();
}
/* ---- operation editing from the Toolpaths manager menu: clipboard, undelete, reverse, feeds / speeds */
CamOp *camop_clone(const CamOp *s) {
    if (!s) return NULL;
    CamOp *d = malloc(sizeof *d); *d = *s;
    d->chains = malloc(sizeof(Chain) * (s->nchains ? s->nchains : 1));
    for (int i = 0; i < s->nchains; i++) d->chains[i] = chain_dup(&s->chains[i]);
    d->holes = malloc(sizeof(Hole) * (s->nholes ? s->nholes : 1)); memcpy(d->holes, s->holes, sizeof(Hole) * s->nholes);
    d->m = malloc(sizeof(Move) * (s->nm ? s->nm : 1)); memcpy(d->m, s->m, sizeof(Move) * s->nm);
    d->edit = camop_clone(s->edit);                      /* a dirty op's changed parameters go with it */
    return d;
}
/* put an operation into the list at camops index k (it gets a tree entry), one undo step */
void camop_insert(CamOp *op, int k) {
    if (ncamops >= MAX_CAMOPS) { cam_op_free(op); message("Paste", "Too many operations."); return; }
    camops_detach();
    if (k < 0 || k > ncamops) k = ncamops;
    memmove(camops + k + 1, camops + k, (ncamops - k) * sizeof camops[0]); camops[k] = op; ncamops++;
    camops_attach();
    sel = op->op_index;                                  /* ops are not on Undo's stack (formats/align_ops.md 4) */ msel_owner = -9; sim = -1; doc_dirty = 1; scene_reupload();
}
/* Tab Cutoff as a separate operation (formats/contour_3d_tabcutoff.md 2): bnci_ctour calls FUN_180044170 (decomp/mcmill_ctour.c
   l.3905-4307) at every generation of a contour op: CDependentOperations::DetectChildOperationIDs (mccore 0x1802e9a50: the ops whose op +0xd0 =
   record 0x138 is this op's id) - those of opcode 0x82 are deleted (DeleteListAndDB); then, with Tabs on (op +0x1318) and Tab Cutoff 0x138a =
   3, a new op: init_operation(op, 1, 0, ok) = InitOperation(opcode 1, .., read defaults 0) (decomp/mccore_ops.c), its parameters from
   cutoff_source below; then (FUN_180044170) op +0x170 (the NC name) from the parent, opcode 0x82, op +0xd0 = the parent's id, the parent's
   tool block (op +0x2e0, 0x288 bytes) when the tool numbers (op +0x2e0) differ, Tabs on (op +0x1318 = 1); stored (store_ent_assoc), the
   parent's chains saved for it with the parent's tabs (tp_get_chains + tabs_from_chains, list_op_get_chains, save_chain), and the
   caller generates it (ctour_toolpaths l.558-571 / COpMgrTree::RegenSelected: bnci_section -> bnci_cutoff). A new op each time: the
   old child is gone, the new one has a new id (OMgetNextOpId) and goes where cutoff_children's tail puts it. */
static int ct_uid_max(void) { int m = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->p.ct_uid > m) m = camops[j]->p.ct_uid; return m; }
static int op_seq_max(void) { int m = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->p.op_seq > m) m = camops[j]->p.op_seq; return m; }
/* an op made here (gen_commit, a pasted copy, a Tab Cutoff op): the next in the order ops are made (OMgetNextOpId) */
void op_seq_assign(CamOp *op) { if (op) op->p.op_seq = op_seq_max() + 1; }
static int stored_child_of(int i, int pid) {             /* a stored op of opcode 0x82 whose record 0x138 is pid */
    const Op *o = &F.ops[i]; int32_t v = 0;
    if (!o->raw || o->opcode != 0x82 || o->raw_n < 0x13c) return 0;
    memcpy(&v, o->raw + 0x138, 4); return pid > 0 && v == pid;
}
/* list_op_get_prev(op, 0, 1, ok, machine group, -1) (mccore 0x1800be800, ~/mastercam-re/work/c3d_a045/lp.c): of the op list, the op whose
   opcode (op +4) is 1 exactly, in the active machine's group (RetrieveRootMachineGroup(op +0x28c) = the machine's id; one machine group
   here), with the greatest op id (op +0: `*param_1 < *piVar11` keeps the higher). Op ids grow as ops are made (OMgetNextOpId): a stored
   op (or ours taking one over, file_op_id) has its stored id; an op made here is newer than every stored one, in the order made
   (CamParams.op_seq; 0 = reopened from a project: tree order, as the .mcx-9 save numbers them, mcam_saveops.c next_op++). A dirty op
   with changed parameters is that op as changed (Mastercam's op list holds them, edit_operation). Returns the camops index (>= 0),
   -2 - the F.ops index for a stored op, or -1 (none). */
static int newest_contour_op(void) {
    int best = -1, bcls = -1, bpos = -1; long bkey = -1;
    for (int i = 0; i < F.nops; i++) {
        int k = -1; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) k = j;
        int cls; long key;
        if (k >= 0) {
            const CamOp *o = camops[k]->edit ? camops[k]->edit : camops[k]; int st;
            if (mcam_opcode_for(&o->p, &st) != 1) continue;
            if (o->p.file_op_id > 0) { cls = 0; key = o->p.file_op_id; } else { cls = 1; key = o->p.op_seq; }
        } else {
            if (F.ops[i].opcode != 1) continue;
            cls = 0; key = F.ops[i].id;
        }
        if (cls > bcls || (cls == bcls && (key > bkey || (key == bkey && i > bpos)))) { bcls = cls; bkey = key; bpos = i; best = k >= 0 ? k : -2 - i; }
    }
    return best;
}
/* config item 17028 "Get defaults from previous operation?" (ReadCfgFile item 0x4284 -> set_init_op_use_prev_op_same_type, decomp/mccore_tooledit.c
   l.12110; the Configuration page's check l.12021) = the switch DAT_1812aceb0 InitOperation reads (l.334; refs_aceb0.txt). Not in the file:
   the image's static value 1 (mccore .data 0x1812aceb0 = 01). The user's (my mcam2017/CONFIG/mcamxm.config and mcamx.config l.610): y.
   MCAMUI_TEST_CFG17028 (ours, tests) stands in for the file's value. */
static int cfg_prev_op_same_type(void) {
    char v[32]; const char *t = getenv("MCAMUI_TEST_CFG17028");
    if (t) snprintf(v, sizeof v, "%s", t); else mcam_config_value(mcam_is_metric(&F), 17028, v, sizeof v);
    if (!v[0]) return 1;
    return v[0] == 'y' || v[0] == 'Y' || atoi(v) == 1;
}
/* InitOperation(new, 1, 0, 1, -1, 0, ok) as FUN_180044170 calls it (redo_op_idn = 0: InitOperation is its only writer, mccore refs, and
   only mccore / mcmill name it): with 17028 = y and an op list that is not empty, the copy is list_op_get_prev's (l.330-336; the milling
   branch l.553-1390 copies it whole and clears the comment op +6, 0x78 bytes); with 17028 = n (local_d7c7 = 0) LAB_1800479d4 (l.1397-1416)
   reads default_ops_get(1, ..): the machine group's defaults file's contour op (here: cam_defaults + heights_from_default_op, the op a new
   Contour starts from in this program, at the parent's chain Z). Then the tail common to both: the active planes
   (MastercamPlanes::InitOperationViewsToActivePlanes l.1773, local_d7c1 = 1 since redo_op_idn = 0) - and, NOT PORTED here (no field for
   them or their inputs): the NC name copy of l.1416-1430 (FUN_180044170 overwrites it from the parent), init_misc_values (l.1721), the tool
   change position, the machine group's "override with modal values" heights (l.2086-2122, cui_grpprops.c grp_modal_record). 0 = no
   source could be read (a stored op this program cannot read): no child, a message. */
static int cutoff_source(const CamOp *par, CamParams *out, char *why, size_t whyn) {
    if (cfg_prev_op_same_type()) {
        int s = newest_contour_op();
        if (s >= 0) { const CamOp *o = camops[s]->edit ? camops[s]->edit : camops[s]; *out = o->p; }
        else if (s <= -2) {
            int i = -2 - s; const CamOp *ed = op_dirty_edit(i);
            if (ed) *out = ed->p;
            else { CamOp tmp; memset(&tmp, 0, sizeof tmp);
                if (!cam_op_from_file(&F, i, &tmp, why, whyn)) return 0;
                *out = tmp.p; for (int c = 0; c < tmp.nchains; c++) cam_chain_free(&tmp.chains[c]); free(tmp.chains); free(tmp.holes); free(tmp.m); }
        } else { snprintf(why, whyn, "no contour operation to copy"); return 0; }   /* not reached: the parent is one */
    } else {
        double cz = 0;   /* the chain Z as pp_seed_params takes it */
        if (par->nchains && par->chains[0].n) { const Ent *e0 = &F.ents[par->chains[0].ent[0]]; cz = e0->type == E_ARC ? e0->c.z : e0->a.z; }
        OpHFlags hf = pp_hf; const uint8_t *rec = pp_rec; uint32_t recn = pp_rec_n; int fresh = pp_tree_fresh;   /* the dialog's state, kept */
        cam_defaults(out, CAM_CONTOUR, &F);
        heights_from_default_op(out, CAM_CONTOUR, mcam_is_metric(&F), cz);
        pp_hf = hf; pp_rec = rec; pp_rec_n = recn; pp_tree_fresh = fresh;
    }
    planes_set_active(out);                              /* InitOperationViewsToActivePlanes */
    return 1;
}
/* FUN_180044170: `if (local_a158[0] != local_4f48[0])` (op +0x2e0, the tool number, of the new op and the parent) the parent's op +0x2e0 ..
   +0x568 (0x288 bytes) over the new op's: here the CamParams fields from that block (record 0x348 .. 0x5d0: tool number / slot / shape,
   feed 0x378, plunge 0x380, retract 0x388, rpm 0x390, coolant 0x398, D offset 0x370) and the record patches inside it */
static void cutoff_tool_block(CamParams *d, const CamParams *par) {
    if (d->tool_num == par->tool_num) return;
    d->tool_num = par->tool_num; d->tool_slot = par->tool_slot; d->tool_dia = par->tool_dia; d->tool_type = par->tool_type;
    d->corner_r = par->corner_r; d->tip_angle = par->tip_angle;
    d->feed = par->feed; d->plunge_feed = par->plunge_feed; d->mc_rrate = par->mc_rrate; d->rpm = par->rpm; d->coolant = par->coolant; d->ct_doff = par->ct_doff;
    int k = 0;
    for (int i = 0; i < d->nrpatch; i++) if (!(d->rpatch[i].off >= 0x348 && d->rpatch[i].off < 0x5d0)) d->rpatch[k++] = d->rpatch[i];
    for (int i = 0; i < par->nrpatch && k < CAM_NRPATCH; i++) if (par->rpatch[i].off >= 0x348 && par->rpatch[i].off < 0x5d0) d->rpatch[k++] = par->rpatch[i];
    for (int i = k; i < d->nrpatch; i++) memset(&d->rpatch[i], 0, sizeof d->rpatch[i]);
    d->nrpatch = k;
}
/* Where the new op goes: store_ent_assoc -> TpPartOpList::AddListAndInit (decomp/align/core_oplist.c l.2414: op +0x7e = 0, which
   InitOperation sets (mccore_ops.c l.1700) and FUN_180044170 leaves, appends it to the op list), then OMrefreshOperation ->
   COpMgrTree::RefreshOperation -> AddOperation (decomp/opman_tree_all.c l.25883: with the insert arrow in the op's toolpath group,
   OMinsertInfo +0x37b4, it is put at the arrow - after the op above it, +0x37b8 - and SortOpListToMatchTree; else last in its group).
   This tree has no insert arrow: the arrow is after the last item (COpMgrTree::LoadTree, formats/qt_port_plan.md), where new ops go
   here too - the end of the list. Whether AddOperation's SelectOperation(id, 1, ..) of the new op happens during a regeneration
   (OMrefreshDisabled) is NOT DECODED: the selection is left as it is. */
void cutoff_children(CamOp *par) {
    if (!par || par->p.type != CAM_CONTOUR || par->p.ct_cutoff_op || par->op_index < 0) return;
    int want = par->p.tabs > 0 && par->p.tab_rec && par->p.tab_cut == 3;
    if (!want && !par->p.ct_uid && par->p.file_op_id <= 0) return;
    if (want && !par->p.ct_uid) par->p.ct_uid = ct_uid_max() + 1;
    int fid = par->p.file_op_id;
    for (int i = F.nops - 1; i >= 0; i--) if (fid > 0 && stored_child_of(i, fid)) { int id = F.ops[i].id; remove_stored_op(id); gone_add(id); }
    for (int j = 0; j < ncamops; j++) { CamOp *c = camops[j];   /* DeleteListAndDB of every 0x82 child */
        if (c == par || !c->p.ct_cutoff_op) continue;
        if ((par->p.ct_uid > 0 && c->p.ct_parent_uid == par->p.ct_uid) || (fid > 0 && c->p.ct_parent_uid == -fid)) {
            camops_detach(); camops[j] = camops[--ncamops]; camops_attach(); cam_op_free(c); j--; } }
    if (!want) { doc_dirty = 1; scene_reupload(); if (cui_hooks.ops_changed) cui_hooks.ops_changed(); return; }
    CamParams src; char why[300] = "";
    if (!cutoff_source(par, &src, why, sizeof why)) {
        snprintf(cam_msg, sizeof cam_msg, "Tab Cutoff: the contour operation its parameters come from cannot be read (%s)", why);
        doc_dirty = 1; scene_reupload(); if (cui_hooks.ops_changed) cui_hooks.ops_changed(); return; }
    CamOp *n = calloc(1, sizeof *n); if (!n) return;
    n->p = src;
    cutoff_tool_block(&n->p, &par->p);
    n->p.type = CAM_CONTOUR; n->p.ct_cutoff_op = 1; n->p.ct_parent_uid = par->p.ct_uid; n->p.ct_uid = 0; n->p.file_op_id = 0;
    n->p.tabs = par->p.tabs > 0 ? par->p.tabs : 1;       /* op +0x1318 = 1 */
    /* the parent's tab list, saved with the chains (tabs_from_chains -> save_chain): the tab centres and their own values */
    memcpy(n->p.tab_xy, par->p.tab_xy, sizeof n->p.tab_xy); n->p.ntab_xy = par->p.ntab_xy;
    memcpy(n->p.tab_ed, par->p.tab_ed, sizeof n->p.tab_ed); memcpy(n->p.tab_own, par->p.tab_own, sizeof n->p.tab_own);
    n->name[0] = 0;                                      /* InitOperation clears op +6.. (the comment) */
    n->chains = calloc(par->nchains ? par->nchains : 1, sizeof(Chain)); n->nchains = par->nchains;
    for (int c = 0; c < par->nchains; c++) n->chains[c] = chain_dup(&par->chains[c]);
    n->op_index = -1;
    op_seq_assign(n);                                    /* OMgetNextOpId */
    if (!cam_generate(&F, n)) snprintf(cam_msg, sizeof cam_msg, "Tab Cutoff: %s", n->error);   /* bnci_section of the new op */
    if (ncamops < MAX_CAMOPS) { add_op_at(n, -1); camops[ncamops++] = n; }   /* the end of the list (above) */
    else cam_op_free(n);
    doc_dirty = 1; scene_reupload();
    if (cui_hooks.ops_changed) cui_hooks.ops_changed();
}
/* regenerate one operation in place (its tree position is kept) */
int camop_regen(CamOp *op) {
    free(op->m); op->m = NULL; op->nm = 0;
    pp_engrave_ask(op);
    if (!cam_generate(&F, op)) { snprintf(cam_msg, sizeof cam_msg, "%s: %s", cam_op_label(op), op->error); return 0; }
    if (op->warning[0]) snprintf(cam_msg, sizeof cam_msg, "%s: warning: %s", cam_op_label(op), op->warning);   /* dec1 (ours) */
    camops_detach(); camops_attach(); doc_dirty = 1; scene_reupload();
    cutoff_children(op);
    return 1;
}
/* ---- tests: MCAMUI_OPDUMP=1 prints the operations / the op dialog after every step (both apps, op_compare.sh) */
void op_dump(void) {
    printf("    opstate ops=%d camops=%d tools=%d show_params=%d edit=%d tool_choice=%d page=%d lk_pick=%d tl6410=%d/%d clip=%d%s deleted=%d msg=\"%s\"\n",
           loaded ? F.nops : 0, ncamops, F.ntools, show_params, edit_camop, tool_choice, pp_page, lk_pick, tl6410_open, tl6410_sel,
           op_paste_ready(), "", op_delete_buffered(), cam_msg);   /* clip = a cut / copy buffer to paste, deleted = ops in the delete buffer */
    for (int i = 0; loaded && i < F.nops; i++) {
        int k = -1; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) k = j;
        printf("      op %d %s \"%s\" T%d %ld pts%s%s%s\n", i + 1, F.ops[i].type, F.ops[i].comment, k >= 0 ? camops[k]->p.tool_num : F.ops[i].tool, F.ops[i].npts,
               F.ops[i].stale ? " STALE" : op_dirty(i) ? (op_dirty_edit(i) ? " DIRTY edited" : " DIRTY") : "", k >= 0 ? " (ours)" : "", op_selected(i) ? " (selected)" : "");
    }
    if (!show_params) return;
    const CamParams *p = &pending.p; char t[160]; pp_title(t, sizeof t);
    printf("    opdlg \"%s\" name \"%s\" type %d T%d dia %g S%d F%g P%g R%g top %g fin %g clr %g ret %g fp %g r %g step %g so %g side %d ct %d cyc %d peck %g cool %d arcs %d tabs %d fn %d tip %d\n",
           t, pending.name, p->type, p->tool_num, p->tool_dia, p->rpm, p->feed, p->plunge_feed, p->mc_rrate, p->top_z, p->final_z, p->clearance, p->retract_z,
           p->feed_plane, p->r_plane, p->stepdown, p->stepover_pct, p->side, p->contour_type, p->drill_cycle, p->peck, p->coolant, p->arcs, p->tabs, p->finish_n, p->tip_comp);
    printf("    opdlg hf clr %d/%d/%d %.4f ret %d/%d fp %d top %d dep %d cz %.4f fstock %.4f | tp rapid %d ftc %d batch %d\n",
           pp_hf.clr_on, pp_hf.clr_end, pp_hf.clr_inc, pp_hf.clr_abs, pp_hf.ret_on, pp_hf.ret_inc, pp_hf.fp_inc, pp_hf.top_inc, pp_hf.dep_inc, pp_hf.cz, pp_hf.fstock,
           tp_rapid, tp_ftc, tp_batch);
    PpTreeRow tr[64]; int nt = pp_tree_rows(tr, 64);
    printf("    opdlg tree:"); for (int i = 0; i < nt; i++) printf(" %s%d:%s%s%s", tr[i].slot == pp_page ? "*" : "", tr[i].depth, tr[i].title, tr[i].done ? "" : "(grey)", tr[i].state == 1 ? "(edited)" : tr[i].state == 2 ? "(disabled)" : ""); printf("\n");
    const char *qn[10]; char qv[10][64]; int nq = pp_quickview_rows(qn, qv);
    printf("    opdlg qv:"); for (int i = 0; i < nq; i++) printf(" %s=%s;", qn[i], qv[i]); printf("\n");
}

/* ---- tests: the walker that sets one page control (opset:LABEL=V; combo#K / check / prop / iprop by label) */
typedef struct { const char *key; double v; int combo_k, combo_i, hit; } PpSetU;
static double ps_prop(void *u, const char *l, double v, double lo, double hi, double st) {
    PpSetU *s = u; (void)st; if (!s->hit && !strcmp(l[0] == '#' ? l + 1 : l, s->key)) { s->hit = 1; double n = s->v; return n < lo ? lo : n > hi ? hi : n; } return v; }
static int ps_iprop(void *u, const char *l, int v, int lo, int hi) {
    PpSetU *s = u; if (!s->hit && !strcmp(l[0] == '#' ? l + 1 : l, s->key)) { s->hit = 1; int n = (int)s->v; return n < lo ? lo : n > hi ? hi : n; } return v; }
static int ps_check(void *u, const char *l, int v) { PpSetU *s = u; if (!s->hit && !strcmp(l, s->key)) { s->hit = 1; return s->v != 0; } return v; }
static int ps_combo(void *u, const char *const *it, int n, int cur, float pw, float ph) {
    PpSetU *s = u; (void)it; (void)pw; (void)ph;
    if (!s->hit && s->combo_k == s->combo_i++) { s->hit = 1; int k = (int)s->v; return k >= 0 && k < n ? k : cur; } return cur; }
static void ps_row(void *u, float h, int du, int c) { (void)u; (void)h; (void)du; (void)c; }
static void ps_rowpic(void *u, float h, float p, float w, const char *f) { (void)u; (void)h; (void)p; (void)w; (void)f; }
static void ps_rowspic(void *u, float h, float w, const char *f) { (void)u; (void)h; (void)w; (void)f; }
static void ps_pic(void *u, float w, float h, const char *f) { (void)u; (void)w; (void)h; (void)f; }
static void ps_void(void *u) { (void)u; }
static void ps_label(void *u, const char *t, int st) { (void)u; (void)t; (void)st; }
static void ps_option(void *u, const char *t, int on) { (void)u; (void)t; (void)on; }
static int ps_button(void *u, const char *t, int en) { (void)u; (void)t; (void)en; return 0; }   /* buttons: their own steps (plcopy / plsel) */
static int ps_radio(void *u, const char *t, int on) { (void)u; (void)t; (void)on; return 0; }
static const PpOps PS_OPS = { ps_row, ps_rowpic, ps_rowspic, ps_pic, ps_void, ps_prop, ps_iprop, ps_check, ps_combo, ps_label, ps_option, ps_button, ps_radio };
/* the page's controls as text (opctl): label = value lines */
typedef struct { int combo_i; } PpListU;
static double pl_prop(void *u, const char *l, double v, double lo, double hi, double st) { (void)u; printf("      prop %s = %g [%g..%g / %g]\n", l[0] == '#' ? l + 1 : l, v, lo, hi, st); return v; }
static int pl_iprop(void *u, const char *l, int v, int lo, int hi) { (void)u; printf("      iprop %s = %d [%d..%d]\n", l[0] == '#' ? l + 1 : l, v, lo, hi); return v; }
static int pl_check(void *u, const char *l, int v) { (void)u; printf("      check %s = %d\n", l, v); return v; }
static int pl_combo(void *u, const char *const *it, int n, int cur, float pw, float ph) {
    PpListU *s = u; (void)pw; (void)ph; printf("      combo#%d = %d (%s) of", s->combo_i++, cur, cur >= 0 && cur < n ? it[cur] : "?"); for (int i = 0; i < n; i++) printf(" %s;", it[i]); printf("\n"); return cur; }
static void pl_label(void *u, const char *t, int st) { (void)u; printf("      label%s %s\n", st ? "(error)" : "", t); }
static void pl_option(void *u, const char *t, int on) { (void)u; printf("      option(grey) %s = %d\n", t, on); }
static void pl_pic(void *u, float w, float h, const char *f) { (void)u; printf("      pic %s %gx%g\n", f, w, h); }
static void pl_rowpic(void *u, float h, float p, float w, const char *f) { (void)u; (void)p; printf("      pic %s %gx%g\n", f, w, h); }
static void pl_rowspic(void *u, float h, float w, const char *f) { (void)u; printf("      pic %s %gx%g\n", f, w, h); }
static int pl_button(void *u, const char *t, int en) { (void)u; printf("      button%s %s\n", en ? "" : "(grey)", t); return 0; }
static int pl_radio(void *u, const char *t, int on) { (void)u; printf("      radio %s = %d\n", t, on); return 0; }
static const PpOps PL_OPS = { ps_row, pl_rowpic, pl_rowspic, pl_pic, ps_void, pl_prop, pl_iprop, pl_check, pl_combo, pl_label, pl_option, pl_button, pl_radio };
/* the dialog's controls of the shown page: the model (both apps print the same) */
void op_ctl_dump(void) {
    if (!show_params) { printf("    (no op dialog)\n"); return; }
    pp_frame_begin();
    int pg = pp_page_shown(), x, y, w, h; char pf[64];
    printf("    page %d %s%s\n", pp_page, PG_TITLE[pp_page], pp_page_pic(&pending.p, pg, &x, &y, &w, &h, pf, sizeof pf) ? "" : " (no picture)");
    if (pp_page_pic(&pending.p, pg, &x, &y, &w, &h, pf, sizeof pf)) printf("      picture %s at %d,%d %dx%d\n", pf, x, y, w, h);
    if (pg == PG_TOOL) {
        TpCtl c[64]; int n = tp_ctls(c, 64);
        for (int i = 0; i < n; i++) { char v[160] = ""; if (c[i].kind == TPK_RO || c[i].kind == TPK_EDIT || c[i].kind == TPK_TEXTBOX) tp_ctl_text(c[i].id, v, sizeof v);
            if (c[i].kind == TPK_CHECK) snprintf(v, sizeof v, "%d", tp_ctl_check(c[i].id)); if (c[i].kind == TPK_COMBO) snprintf(v, sizeof v, "%s", TP_DIR[tp_ctl_combo()]);
            printf("      ctl kind %d id %d at %d,%d %dx%d \"%s\"%s = %s\n", c[i].kind, c[i].id, c[i].x, c[i].y, c[i].w, c[i].h, c[i].text, c[i].ref < 0 ? " (grey)" : "", v); }
        int idx[4096], nr = tp_rows(idx, 4096);          /* the shown rows in their order (filter, sort): list = the part tool's index */
        for (int r = 0; r < nr; r++) { int i = idx[r], cols[TM_COLS], wd[TM_COLS], nc = tp_list_cols(cols, wd); printf("      list %d%s:", i, i == tool_choice ? "*" : "");
            for (int k = 0; k < nc; k++) { char cl[160]; tm_cell(&F.tools[i], cols[k], cl, sizeof cl); printf(" [%s]", cl); } printf("\n"); }
    } else if (pg == PG_LINK && !pf_active(pg)) {
        LkRow r[5]; int n = lk_rows(r);
        for (int i = 0; i < n; i++) printf("      link %s%s on %d live %d %s %.4f\n", r[i].btn, r[i].has_on ? "" : " (no switch)", r[i].on, r[i].live, r[i].inc ? "Incremental" : "Absolute", r[i].shown);
        printf("      link clr_end %d%s\n", pp_hf.clr_end, pending.p.type == CAM_DRILL ? " | Subprogram (grey)" : "");
    } else if (pf_active(pg)) {                          /* a template page (cui_opdlg.c): kind id at x,y wxh "caption" = value */
        static const char *KN[] = { "static", "pic", "edit", "combo", "check", "radio", "button", "group", "trackbar", "list", "cgroup" };
        PfShown s[160]; int n = pf_ctls(s, 160);
        printf("      template %d\n", pf_page_dialog(pg));
        for (int i = 0; i < n; i++) {
            printf("      tctl %s %d at %d,%d %dx%d \"%s\"", KN[s[i].kind], s[i].id, s[i].x, s[i].y, s[i].w, s[i].h, s[i].text);
            if (s[i].kind == PFK_CHECK || s[i].kind == PFK_RADIO || s[i].kind == PFK_CGROUP) printf(" = %d", s[i].on);
            if (s[i].kind == PFK_COMBO || s[i].kind == PFK_LIST) { printf(" = %d of", s[i].cur); for (int k = 0; k < s[i].nitems; k++) printf(" %s;", s[i].items[k]); }
            if (s[i].kind == PFK_PIC) printf(" %s", s[i].pic);
            printf("%s%s%s\n", s[i].kind != PFK_STATIC && s[i].kind != PFK_PIC && s[i].kind != PFK_GROUP && !s[i].live ? " (grey)" : "", s[i].nd == 1 ? " NOT DECODED" : "", s[i].ours ? " (ours)" : "");
        }
    } else { PpListU lu = { 0 }; pp_page_body(pg, &PL_OPS, &lu); }
}

/* the MCAMUI_SCRIPT steps of the operations and the op dialog (moved from mcamui.c run_script: gen, libtool, usetool,
   edittool, delop, post, camops, recalc, reverse, editop, params, setdepth, regen, lkpick, lkval, menu, group, tools,
   docfile, show:page:N; ours, both apps: opdump, opctl, oppage:SLOT (a tree click), opset:LABEL=V, pfset:ID=V (a template page control), lkset:ROW,WHAT=V,
   tpset:ID=V, tpcomment:TEXT, tppick:I, tpsort:C, tpmenu:ACT, tl6410:open|sel=I|ok|cancel|sort=C|rows, opok, opcancel, delete (the Delete key's
   rule), lkclick:X,Y,Z, snapw:X,Y,Z; dirty operations through cui_dirty_script: tpseldirty, tpregen, tpregendirty, dirtyopt:0|1,
   dirtyok, dirtycancel, dirtydump). 1 = handled. */
int cui_op_script(const char *st, const char *arg) {
    if (cui_planes_script(st, arg)) return 1;          /* plsel / plrow / plok / plcopy / plwofs / upl* / pldump (cui_planes.c) */
    if (cui_dirty_script(st, arg)) return 1;
    if (cui_defaults_script(st, arg)) return 1;          /* Q19: dflsave / dflreset / dflrestore:N / dflyes / dflno / dfldump (cui_defaults.c) */
    if (cui_preview_script(st, arg)) return 1;           /* Q20: oppreview / pvdump (cui_preview.c) */           /* tpseldirty / tpregen / tpregendirty / dirtyopt / dirtyok / dirtycancel / dirtydump (cui_dirty.c) */
    #define IS(S_) (!strcmp(st, S_))
    if (IS("gen") && arg && loaded) {        /* gen:LABEL:ENT,ENT  (LABEL = gallery name or c/p/d) */
        char lab[64]; const char *colon = strchr(arg, ':');
        snprintf(lab, sizeof lab, "%.*s", colon ? (int)(colon - arg) : (int)strlen(arg), arg);
        if (!strcmp(lab, "c")) CP(lab, "Contour"); else if (!strcmp(lab, "p")) CP(lab, "Pocket"); else if (!strcmp(lab, "d")) CP(lab, "Drill");
        if (!gallery_pick(lab)) { printf("    (unknown op %s)\n", lab); return 1; }
        int ty = cam_mode;
        for (const char *q = colon; q && *q; q = strchr(q + 1, ',')) {
            int en = atoi(q + 1);
            if (en < 0 || en >= F.nents) continue;
            if (cam_type_uses_holes(ty)) { choles[ncholes].x = F.ents[en].c.x; choles[ncholes].y = F.ents[en].c.y; choles[ncholes].d = F.ents[en].type == E_ARC ? 2 * F.ents[en].r : 0; ncholes++; }
            else if (cam_chain(&F, en, cfg_chain_tol, &cchains[ncchains])) ncchains++;   /* chain_tol (config 1001) */
        }
        open_params();
        if (script_tool >= 0 && script_tool < F.ntools) { tool_choice = script_tool; fs_from_source(&pending.p, &F.tools[script_tool]); script_tool = -1; }
        generate_pending();
        return 1;
    }
    if (IS("gen") && arg) return 1;
    if (IS("libtool") && arg) {           /* LIB|NAME: open a library, copy the first tool whose name contains NAME */
        char a[1200]; CP(a, arg); char *bar = strchr(a, '|'); if (bar) *bar++ = 0;
        tm_open_lib(a);
        for (int i = 0; tlib_ok && bar && i < tlib.n; i++) if (strstr(tlib.t[i].name, bar)) { int k = part_add_tool(&tlib.t[i]); if (k >= 0) script_tool = k; break; }
        return 1;
    }
    if (IS("newoptool") && arg) { for (int i = 0; i < F.ntools; i++) if (strstr(F.tools[i].name, arg)) { script_newop_tool = i; break; } return 1; }   /* NAME: the part's first tool whose name contains NAME, for the next new op (ours) */
    if (IS("usetool") && arg) { int n = atoi(arg); for (int i = 0; i < F.ntools; i++) if (F.tools[i].num == n) script_tool = i; return 1; }
    if (IS("edittool") && arg) {          /* N:DIA - the Edit Tool wizard (tedit_open = tw_init_edit on a copy) with a new diameter, Finish */
        int n = atoi(arg); double d = strchr(arg, ':') ? atof(strchr(arg, ':') + 1) : 0;
        for (int i = 0; i < F.ntools; i++) if (F.tools[i].num == n) {
            Tool te; TwState tws; memset(&te, 0, sizeof te); memset(&tws, 0, sizeof tws); tool_copy(&te, &F.tools[i]); tw_init_edit(&te, &tws, 0, 0);
            te.dia = d; free(F.tools[i].holder_rz); tool_copy(&F.tools[i], &te); free(te.holder_rz);
            doc_dirty = 1; regen_ops_for_tool(n); break; }
        return 1;
    }
    if (IS("delop")) { sel = ncamops ? camops[ncamops - 1]->op_index : -1; delete_selected_op(); return 1; }
    if (IS("delete")) {                   /* the Delete key. Mastercam goes by the focused window (the Qt app: MainWindow::tpmFocused); a script
                                             has no focus - ours: the Toolpaths manager when ops are selected and no geometry, else the graphics */
        if (nselected > 0) delete_selected_ents(); else if (!show_params && op_nselected() > 0) delete_selected_op();
        return 1;
    }
    if (IS("post")) { post_ops(); return 1; }
    if (IS("grpmppost") && arg) {         /* ours (tests, MCP): the machine group's "Post with Mastercam post (.pst)" switch, as the Files
                                             page's check box + OK */
        int v = atoi(arg) != 0; if (v != F.grp_mppost) { F.grp_mppost = v; doc_dirty = 1; } return 1; }
    if (IS("camops")) {                   /* print the ops made / taken over here */
        for (int j = 0; j < ncamops; j++) { const CamParams *p = &camops[j]->p;
            printf("    op%d [%d] %s T%d S%d F%g P%g top %g fin %g clr %g ret %g fp %g r %g file %d patches %d:", j, camops[j]->op_index + 1, camops[j]->name,
                   p->tool_num, p->rpm, p->feed, p->plunge_feed, p->top_z, p->final_z, p->clearance, p->retract_z, p->feed_plane, p->r_plane, p->file_op_id, p->nrpatch);
            for (int k = 0; k < p->nrpatch; k++) { printf(" %x%s=", p->rpatch[k].off, p->rpatch[k].height ? "h" : ""); for (int b = 0; b < p->rpatch[k].n; b++) printf("%02x", p->rpatch[k].v[b]); }
            printf("\n"); }
        return 1;
    }
    if (IS("recalc")) { recalc_feeds(); return 1; }
    if (IS("reverse") && arg) { sel = atoi(arg) - 1; msel_owner = -9; reverse_selected(); printf("    %s\n", cam_msg); return 1; }   /* reverse:N - Reverse toolpath on tree op N */
    if (IS("editop") && arg && loaded) {  /* editop:N[:key=v,key=v] - open op N's parameters, edit, regenerate */
        int n = atoi(arg) - 1; char kv[600]; const char *c = strchr(arg, ':'); CP(kv, c ? c + 1 : "");
        open_op_params(n);
        if (!show_params) { printf("    (op %d not editable: %s)\n", n + 1, cam_msg); return 1; }
        for (char *sv = NULL, *t = c ? strtok_r(kv, ",", &sv) : NULL; t; t = strtok_r(NULL, ",", &sv)) {
            char *eq = strchr(t, '='); if (!eq) continue; *eq = 0;
            if (!cam_set_param(&pending.p, t, atof(eq + 1))) printf("    (unknown parameter %s)\n", t);
        }
        generate_pending();
        printf("    %s\n", cam_msg);
        return 1;
    }
    if (IS("group") && arg) { cm_group = atoi(arg); return 1; }     /* group:1|2|0 - the right-click was on the toolpath / machine group / an op */
    if (IS("menu") && arg) {              /* menu:Copy - a Toolpaths manager right-click command */
        int hit = -1;                     /* the shallowest item of that name (top-level Delete before Groups > Delete) */
        for (int i = 0; i < CM_N; i++) if (!cm_has_kids(i) && cm_shown(i) && !strcasecmp(cm_items[i].text, arg) && (hit < 0 || cm_items[i].depth < cm_items[hit].depth)) hit = i;
        if (hit >= 0) { if (cm_leaf_ok(cm_items[hit].act)) cm_run(hit); else printf("    (menu %s is greyed)\n", arg); }
        for (const char *g = strstr(arg, "Lathe>"); g; g = NULL) {           /* menu:Lathe>Face - the item inside Lathe toolpaths */
            int li = -1; for (int i = 0; i < CM_N; i++) if (cm_items[i].depth == 0 && !strcmp(cm_items[i].text, "Lathe toolpaths")) li = i;
            for (int i = li + 1; li >= 0 && i < CM_N && cm_items[i].depth > 0; i++) if (cm_items[i].depth == 1 && !strcasecmp(cm_items[i].text, g + 6)) {
                hit = i; if (cm_leaf_ok(cm_items[i].act)) cm_run(i); else printf("    (menu %s is greyed)\n", arg); break; }
        }
        for (const char *g = strstr(arg, "Groups>"); g; g = NULL) {          /* menu:Groups>Delete - the item inside Groups */
            for (int i = 0; i < CM_N; i++) if (cm_items[i].depth == 1 && !strcasecmp(cm_items[i].text, g + 7)) {
                int gi = i; while (gi > 0 && cm_items[gi].depth > 0) gi--;
                if (!strcmp(cm_items[gi].text, "Groups")) { hit = i; if (cm_leaf_ok(cm_items[i].act)) cm_run(i); else printf("    (menu %s is greyed)\n", arg); break; } } }
        if (hit < 0) printf("    (no menu item %s)\n", arg);
        for (int i = 0; i < F.nops; i++) printf("      %d %s %s %ld pts\n", i + 1, F.ops[i].type, F.ops[i].comment, F.ops[i].npts);
        return 1;
    }
    if (IS("tools")) { for (int t = 0; t < F.ntools; t++) printf("    tool %d: T%d H%d D%d %s\n", t, F.tools[t].num, F.tools[t].len_off, F.tools[t].dia_off, F.tools[t].name);
        for (int i = 0; i < F.nops; i++) { int n = F.ops[i].tool; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) n = camops[j]->p.tool_num; printf("    op %d %s T%d%s\n", i + 1, F.ops[i].type, n, op_selected(i) ? " (selected)" : ""); }
        return 1; }
    if (IS("docfile")) { op_doc_file(); return 1; }   /* Doc file (path on stderr in scripted runs) */
    if (IS("lkpick") && arg) { lk_pick = atoi(arg); snprintf(cam_msg, sizeof cam_msg, "Select point"); return 1; }   /* lkpick:1..5 - arm a Linking page "..." button (LKP_*) */
    if (IS("lkval")) {                    /* the Linking page fields of the open params dialog */
        printf("    linking: clr %.4f ret %.4f fp %.4f top %.4f depth %.4f msg \"%s\"\n", pp_hf.clr_abs,
               pending.p.type == CAM_DRILL ? pending.p.r_plane : pending.p.retract_z, pending.p.feed_plane, pending.p.top_z, pending.p.final_z, cam_msg);
        return 1; }
    if (IS("snapw") && arg) {             /* snapw:X,Y[,Z] (ours, tests): the AutoCursor snap (geo_snap, no anchor, no grid) of
                                             the screen point of world X,Y,Z in the view as last drawn */
        double x = 0, y = 0, z = 0; sscanf(arg, "%lf,%lf,%lf", &x, &y, &z); V3 q = { x, y, z };
        int k = geo_snap(&q, NULL, -1, 0);
        printf("    snapw %.4f,%.4f,%.4f -> %s kind %d at %.4f,%.4f,%.4f\n", x, y, z, k ? "snapped" : "none", k ? geo_snap_kind : GK_NONE, q.x, q.y, q.z);
        return 1;
    }
    if (IS("lkclick") && arg) { double x = 0, y = 0, z = 0; sscanf(arg, "%lf,%lf,%lf", &x, &y, &z); if (lk_pick) lk_click(x, y, z); else printf("    (no Linking pick armed)\n"); return 1; }
    if (IS("params") && arg) { open_op_params(atoi(arg) - 1); return 1; }       /* tree row number */
    if (IS("setdepth") && arg) { pending.p.final_z = atof(arg); return 1; }
    if (IS("regen")) { generate_pending(); return 1; }
    if (IS("show") && arg && !strncmp(arg, "page:", 5)) { pp_page = atoi(arg + 5); return 1; }   /* op dialog page slot (PG_*) */
    /* ours (both apps) */
    if (IS("opdump")) { op_dump(); return 1; }
    if (IS("opctl")) { op_ctl_dump(); return 1; }
    if (IS("oppage") && arg) { if (show_params) { pp_frame_begin(); pp_tree_click(atoi(arg)); } return 1; }
    if (IS("opset") && arg && show_params) {   /* opset:LABEL=V (a prop / iprop / check by its label, combo#K by its order) */
        char key[200]; const char *eq = strrchr(arg, '='); if (!eq) return 1;
        snprintf(key, sizeof key, "%.*s", (int)(eq - arg), arg);
        pp_frame_begin();
        CamParams before = pending.p; OpHFlags hb = pp_hf;
        PpSetU u = { key, atof(eq + 1), -1, 0, 0 };
        if (!strncmp(key, "combo#", 6)) { u.combo_k = atoi(key + 6); u.key = ""; }
        pp_page_body(pp_page_shown(), &PS_OPS, &u);
        if (!u.hit) printf("    (no control %s on page %d)\n", key, pp_page);
        pp_note_change(&before, &hb);
        return 1;
    }
    if (IS("openg")) {                    /* openg (ours, tests): the engine inputs of the last op made here - the fields the template
                                             pages' live controls set (cui_opdlg.c pf_bset) and hashes of the record blocks */
        if (!ncamops) { printf("    openg none\n"); return 1; }
        const CamParams *p = &camops[ncamops - 1]->p; unsigned hp = 2166136261u, hm = 2166136261u;
        for (size_t k = 0; k < sizeof p->pk_p; k++) hp = (hp ^ p->pk_p[k]) * 16777619u;
        for (size_t k = 0; k < sizeof p->mc_p; k++) hm = (hm ^ p->mc_p[k]) * 16777619u;
        printf("    openg type %d top %.9g fin %.9g clr %.9g fp %.9g side %d/%d ct %d roll %d chw %.9g cht %.9g tipd %.9g stock %.9g so %.9g\n",
               p->type, p->top_z, p->final_z, p->clearance, p->feed_plane, p->side, p->side_rel, p->contour_type, p->roll, p->chamfer_w, p->chamfer_tip, p->tip_dia, p->stock_xy, p->stepover_pct);
        printf("    openg face %d %.9g %.9g %.9g | mc comp %d/%d tip %d kd %d lead %d clrend %d mp %d %d %.9g %d %.9g finall %d | dc %d %.9g step %.9g\n",
               p->face_style, p->face_overlap, p->face_along, p->face_angle, p->mc_comp, p->mc_comp_right, p->mc_tip, p->mc_keep_down, p->mc_lead_on, p->mc_clr_end,
               p->mc_mp_on, p->mc_mp_nr, p->mc_mp_rs, p->mc_mp_nf, p->mc_mp_fs, p->ct_fin_all, p->mc_depth_nf, p->mc_depth_fs, p->stepdown);
        printf("    openg drill %d %.9g %d | thread %.9g %.9g %d %d %d %.9g %d %d %d %.9g %.9g | hst %.9g %.9g %.9g %.9g %.9g %d %.9g %.9g\n",
               p->drill_cycle, p->peck, p->tip_comp, p->thread_pitch, p->thread_major, p->thread_internal, p->thread_rh, p->thread_up, p->thread_depth, p->thread_full,
               p->thread_passes, p->mc_passes0, p->thread_step, p->mc_step0, p->min_rad_pct, p->micro_lift, p->back_feed, p->slot_width, p->link_retract, p->dyn_outside, p->wall_stock, p->entry_r);
        printf("    openg lead %.9g %.9g fin %d %.9g tabs %d %.9g %.9g pk %d %d %d %.9g ramp %.9g pitch %.9g arcs %d blocks pk %08x mc %08x\n",
               p->lead_len, p->lead_r, p->finish_n, p->finish_step, p->tabs, p->tab_w, p->tab_h, p->pk_roll, p->pocket_no_rough, p->pocket_style, p->pocket_angle,
               p->ramp_deg, p->helix_pitch, p->arcs, hp, hm);
        /* batches 3 / 4: the two lead sides, overlap / gouge / midpoint, adjust start / end, depth cuts group / taper / order */
        printf("    openg leads %d %d %d %d %d %.9g %.9g %.9g %.9g %.9g | %.9g %.9g %.9g %.9g %.9g | dz %.9g %.9g %.9g %.9g ovl %.9g mid %d g %d"
               " | adj %d %d %.9g %.9g %d %d %.9g %.9g | dc %d taper %d %.9g order %d\n",
               p->lead_on, p->lead_in_on, p->lead_out_on, p->lead_in_perp, p->lead_out_perp, p->lead_len, p->lead_len_pct, p->lead_r, p->lead_r_pct, p->lead_sw,
               p->lead_xlen, p->lead_xlen_pct, p->lead_xr, p->lead_xr_pct, p->lead_xsw, p->mc_lead_dz[0], p->mc_lead_dz[1], p->mc_lead_dz[2], p->mc_lead_dz[3],
               p->lead_ovl, p->lead_mid, p->lead_gouge, p->adj_s, p->adj_s_ext, p->adj_s_len, p->adj_s_pct, p->adj_e, p->adj_e_ext, p->adj_e_len, p->adj_e_pct,
               p->mc_depth_on, p->dc_taper, p->dc_taper_a, p->dc_bydepth);
        /* batch 5: compensation type / optimize / tip comp / corner rounding / break, break through, tab profile */
        printf("    openg b5 comp %d opt %d tip %d crnd %.9g cbrk %.9g bt %.9g tabs %d %.9g mp %d/%d\n",
               p->ct_comp, p->ct_ccopt, p->ct_tipcomp, p->ct_crnd, p->ct_cbrk, p->bt_eff, p->tab_ramp, p->tab_ang, p->ct_mp_rec, p->mc_mp_on);
        return 1;
    }
    if (IS("egdwarn") && arg) { pp_dwarn_preset = atoi(arg); return 1; }   /* egdwarn:N - the answer to Engrave's depth warning 1808 at the next OK (1 use max, 2 return, 3 ignore) */
    if (IS("pfset") && arg && show_params) {   /* pfset:ID=V - a template page control (cui_opdlg.c pf_set: edit value, check 0/1, radio, combo index) */
        int id = atoi(arg); const char *eq = strchr(arg, '='); if (!eq) return 1;
        pp_frame_begin();
        CamParams before = pending.p; OpHFlags hb = pp_hf;
        if (!pf_active(pp_page_shown())) { printf("    (page %d is not a template page)\n", pp_page); return 1; }
        pf_set(id, atof(eq + 1));
        pp_note_change(&before, &hb);
        return 1;
    }
    if (IS("recpatch") && arg && show_params) {   /* recpatch:OFF=T:V (ours, testing; as cam_cli --rec): a record field of the pending op with no control
                                                     here, e.g. the HST stock to leave on the checks 0x12a8 (scene s4_check_trim_stock) - T b / s / i / d */
        unsigned off = 0; char t = 0; double v = 0;
        if (sscanf(arg, "%x=%c:%lf", &off, &t, &v) != 3 || pending.p.nrpatch >= CAM_NRPATCH) { printf("    (bad recpatch %s)\n", arg); return 1; }
        int k = pending.p.nrpatch++; pending.p.rpatch[k].off = (uint16_t)off; pending.p.rpatch[k].height = 0;
        if (t == 'b') { pending.p.rpatch[k].n = 1; pending.p.rpatch[k].v[0] = (uint8_t)v; }
        else if (t == 's') { int16_t x = (int16_t)v; pending.p.rpatch[k].n = 2; memcpy(pending.p.rpatch[k].v, &x, 2); }
        else if (t == 'i') { int32_t x = (int32_t)v; pending.p.rpatch[k].n = 4; memcpy(pending.p.rpatch[k].v, &x, 4); }
        else { pending.p.rpatch[k].n = 8; memcpy(pending.p.rpatch[k].v, &v, 8); }
        return 1;
    }
    if (IS("lkset") && arg && show_params) {   /* lkset:ROW,WHAT=V (ROW LK_CLR 1 .. LK_DEP 5; WHAT 0 on, 1 value, 2 inc, 3 pick; ROW 0 = clr_end) */
        int row = 0, what = 0; double v = 0; if (sscanf(arg, "%d,%d=%lf", &row, &what, &v) < 3) return 1;
        CamParams before = pending.p; OpHFlags hb = pp_hf;
        if (row == 0) lk_set_clr_end((int)v); else lk_set(row, what, v);
        pp_note_change(&before, &hb);
        return 1;
    }
    if (IS("tpset") && arg && show_params) {   /* tpset:ID=V - a Tool page control (tp_ctl_set) */
        int id = atoi(arg); const char *eq = strchr(arg, '='); if (!eq) return 1;
        CamParams before = pending.p; OpHFlags hb = pp_hf;
        tp_ctl_set(id, atof(eq + 1));
        pp_note_change(&before, &hb);                     /* the record switches are not compared (as the dialog's frame) */
        return 1;
    }
    if (IS("tpcomment") && show_params) { tp_set_comment(arg ? arg : ""); return 1; }
    if (IS("tppick") && arg && show_params) { tp_pick(atoi(arg)); return 1; }
    if (IS("tpsort") && arg && show_params) { tp_sortlist(atoi(arg)); return 1; }   /* tpsort:C - a click on the list's column C header */
    if (IS("tpmenu") && arg && show_params) {  /* tpmenu:TEXT - an item of menu 1247 by its text (prefix) */
        OtpItem it[24]; int n = otp_menu(it, 24), hit = 0;
        for (int i = 0; i < n && !hit; i++) if (!strncmp(it[i].text, arg, strlen(arg))) { hit = 1; if (it[i].en && it[i].act) otp_menu_act(it[i].act); else printf("    (menu 1247 %s is greyed)\n", it[i].text); }
        if (!hit) printf("    (no menu 1247 item %s)\n", arg);
        return 1;
    }
    if (IS("tl6410") && arg && show_params) {
        if (!strcmp(arg, "open")) { tl6410_show(); tl6410_prepare(); }
        else if (!strncmp(arg, "sel=", 4)) tl6410_sel = atoi(arg + 4);
        else if (!strcmp(arg, "ok")) tl6410_ok(); else if (!strcmp(arg, "cancel")) tl6410_cancel();
        else if (!strncmp(arg, "sort=", 5) && tl6410_open) tl6410_sortlist(atoi(arg + 5));   /* N13-32: a click on the list's column C header */
        char b[64]; tl6410_count_text(b, sizeof b);
        printf("    tool list 6410 open %d sel %d lib \"%s\" %s\n", tl6410_open, tl6410_sel, tl6410_libfile(), b);
        if (tl6410_open && (!strncmp(arg, "sort=", 5) || !strcmp(arg, "rows"))) {   /* the shown rows in their order: list = the library tool's index */
            const TmColSet *cs = tl6410_set(); int idx[4096], nr = tl6410_rows(idx, 4096), cols[TM_COLS], wd[TM_COLS], nc = tl6410_cols(cols, wd);
            const char *sh = "-"; for (int k = 0; k < nc; k++) if (cols[k] == cs->sortcol) sh = tm_head[cols[k]];   /* the header's sort mark */
            printf("      list sort %s %s\n", sh, cs->sortasc ? "ascending" : "descending");
            for (int r = 0; r < nr; r++) { printf("      list %d%s:", idx[r], idx[r] == tl6410_sel ? "*" : "");
                for (int k = 0; k < nc; k++) { char cl[160]; tm_cell(tm_tool(1, idx[r]), cols[k], cl, sizeof cl); printf(" [%s]", cl); } printf("\n"); }
        }
        return 1;
    }
    if (IS("opok") && show_params) { pp_ok(); return 1; }
    /* the Toolpaths manager's Delete / Undelete / Cut / Copy / Paste on the selected ops (selops:..), the answer to its question,
       Display Options' "Prompt for toolpath deletion" (formats/align_ops.md) */
    if (IS("askyes") && arg) { cui_script_yes = atoi(arg) != 0; return 1; }
    if (IS("askchoice") && arg) { cui_script_choice = atoi(arg); return 1; }
    if (IS("delprompt") && arg) { op_del_prompt = atoi(arg) != 0; return 1; }
    if (IS("tpdel")) { delete_selected_op(); return 1; }
    if (IS("tpundel")) { op_undelete(); return 1; }
    if (IS("tpcut")) { op_copy_ex(1, arg && atoi(arg)); return 1; }
    if (IS("tpcopy")) { op_copy_ex(0, arg && atoi(arg)); return 1; }
    if (IS("tppaste")) { op_paste(); return 1; }
    if (IS("opcancel") && show_params) { pp_cancel(); return 1; }
    /* batch 12 (ours, tests): the frame - opapply (Apply 12321), optb:ID (a toolbar button by its command id: 5699 / 9431 / 10320 ...),
       ophideend (the Hide dialog prompt's Esc), opframe (prints Apply's / the toolbar's state, Hide) */
    if (IS("opapply") && show_params) { pp_frame_begin(); if (pp_apply_enabled()) pp_apply(); else printf("    opdlg frame: Apply is greyed\n"); return 1; }
    if (IS("optb") && arg && show_params) { int id = atoi(arg), hit = 0; pp_frame_begin();
        for (int i = 0; i < PP_NTB; i++) if (PP_TB[i].bmp == id) { hit = 1; if (pp_tb_live(i)) pp_tb_click(i); else printf("    opdlg frame: toolbar %d is greyed\n", id); }
        if (!hit) printf("    opdlg frame: no toolbar button %d\n", id);
        return 1; }
    if (IS("ophideend")) { pp_hide_end(); return 1; }
    if (IS("opframe")) {
        if (!show_params) { printf("    (no op dialog)\n"); return 1; }
        pp_frame_begin();
        printf("    opdlg frame apply %s hide %d tb:", pp_apply_enabled() ? "on" : "grey", pp_hide_on);
        for (int i = 0; i < PP_NTB; i++) if (PP_TB[i].bmp && (!PP_TB[i].hst || pp_hst(&pending.p))) printf(" %d%s", PP_TB[i].bmp, pp_tb_live(i) ? "" : "(grey)");
        printf("\n"); return 1; }
    #undef IS
    return 0;
}
