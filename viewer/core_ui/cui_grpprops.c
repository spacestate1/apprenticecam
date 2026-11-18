/* cui_grpprops.c - Machine Group Properties > Files and Tool Settings (the sheet's first two pages), shared by the front ends.
 *
 * Decoded 2026-10-22 (formats/grpprops_tabs.md), ported 2026-10-23. The sheet is mastercam.dll EditGroupPropertiesDlg
 * (decomp/mastercam_grpprops.c; caption opmanres 3851) built by CGroupPg0's ctor FUN_1801df8d0 (decomp/mastercam_grpsheet.c):
 * for a mill group (op_group +0x19e8 = 0) the pages are Files (CGroupPg1, opmanres dialog 0x32f = 815), Tool Settings
 * (CGroupPg2, 0x635 = 1589; 0x669 = 1641 is the wire page) and Stock Setup (CGroupPg3, 1590, cui_stock.c). There is no 4th
 * page for a mill group: Safety Zone (0x637) is built only for an Agie wire machine and the tree's Safety zone item is gated by
 * RunningSafetyZone() @180442a40 = return false (decomp/mastercam_safetyzone_editgroup.c) - it is not shown here.
 *
 * Files page (CGroupPg1, decomp/mastercam_grppg1_files.c): DDX FUN_1801e3940, OnInitDialog FUN_1801e4410, load (PrmToDlg)
 * FUN_1801e61b0, dialog -> group FUN_1801e3850, the comment switches FUN_1801e3240 / FUN_1801e3790, the name's kill focus
 * FUN_1801e4910 (':' -> '-'), OnOK FUN_1801e5590 with the check FUN_1801e2f70 (empty name -> opmanres 3857 "Invalid group
 * name"). Tool Settings page (CGroupPg2, decomp/mastercam_grppg2_toolset.c): DDX FUN_1801eb5e0, group -> page FUN_1801ec2d0,
 * page -> group FUN_1801eb2b0, enable rules FUN_1801eb790, control values FUN_1801eb070 / FUN_1801eb210, From material
 * FUN_1801ebd40, OnOK FUN_1801ec1b0. The sheet's OK: CGroupPg0::OnOK FUN_1801e0a40 (decomp/mastercam_grpprops.c l.331: the
 * name through TpGrpList::CheckGroupName, decomp/opman_mccore_grpname.c; the copy written back over the group).
 *
 * Ported (editable; the group record only, saved by mcam_saveops.c grp_patch): group name (+4) and comment (+0x980),
 * Program # (+0x1178), Feed Calculation (+0x119c) and its user values (+0x11b0 / +0x11a0 / +0x1188 / +0x1180), the override
 * switches (+0x115c..+0x1168), the toolpath configuration switches (+0x11b8..+0x11c4). Their effects on new operations / tools:
 * the program # and sequence numbers of a new op (OMsetOperationToMachine, mcam_saveops.c), the "override with modal values"
 * copy (InitOperation, grp_modal_record below), sequential tool numbers (PrepareNewToolByMachine, grp_assign_seq).
 * Shown read-only (the machine / control records, which the save cannot patch yet: CCNCMachine::SaveControlToDb is not
 * ported): toolpath directory, description, from file, control, post, the library paths, the four comment switches, Adjust
 * feed on arc move, minimum arc feed, sequence start / increment, the material name (the part's material list - the ASSOC
 * 0xE table - is not loaded, so FUN_1801eb000's rule cannot run). NOT PORTED (greyed): the machine's Edit (467) / Replace
 * (463), the toolpath-directory browse (1255), the tool library browse / Tool Manager / load tools (1258 / 1266 / 751), the
 * operation library / defaults browse + edit (1260 / 1264 / 1257 / 1262), the lathe tool manager (635), Material Edit /
 * Select (481 / 479), Apply / Help.
 * NOT DECODED: GroupMachineIsHiddenDefault (the "System Default" texts, opmanres 3168), truncateFileNameString(40) (names are
 * shown whole), CSystemProductFile's %MCAMDIR% expansion / version suffix (paths shown as stored), FileNameIsNone's red text
 * (DAT_1805d5528 = "NONE": FileNameIsNone itself is not decoded, the stored post is shown), the %MCAMDIR% expansion
 * FUN_1801e2f70's file checks see (files_check: ours, the program's shared-folder rule), MultiTaskingIsValid (751 greyed), TpGrpList::IndexByName's comparison (exact here), the
 * consumers of +0x11c0 / +0x11c4 and WarnOfDuplicateToolNumbers (+0x11bc stored only), tp_tool_offset (the D / H rule of
 * part_add_tool is kept). */
#include "core_ui.h"
#include "../mcam_saveops.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "../paths.h"

#define NEG1 4294901759u                                  /* the templates' -1 (0xFFFEFFFF) statics / group boxes */
/* ours (user decision P2, formats/user_decisions.md "Post engine"): the machine group's "Post with Mastercam post (.pst)" switch
   (F.grp_mppost). Mastercam's Files page has no such control: it shows the post read-only (686 / 604: CColorStaticST, FUN_1801e6720
   = CCNCMachine::PostProcFile; decomp/mastercam_grppg1_files.c) and always posts with it. The check box sits in the same
   "Machine - Toolpath Copy" box, on the free right half of the Edit / Replace row (no template control there); its id is not an
   opmanres one. Default off = the LinuxCNC post (nc_post.c). */
#define GP_MPPOST 0x7a01u
/* ---- opmanres dialog 815 "Group Page 1 Dlg" (exports/opmanres_dialogs.tsv), in template order; roles from DDX FUN_1801e3940 */
const StCtl GP_815[] = {
    { 705,   STR_STATIC, 14, 6, 60, 8, "Group name" },
    { 292,   STR_EDIT,   82, 4, 220, 12, "" },               /* UIEditString +0xe00 -> group +4 */
    { 672,   STR_STATIC, 14, 22, 60, 8, "Toolpath directory" },
    { 296,   STR_EDIT,   82, 20, 220, 12, "" },              /* UIEdit +0x1b70: NCPrefix(1) (control page 2) */
    { 1255,  STR_BMPBTN, 307, 20, 16, 14, "" },              /* UIBitmapButton +0xac98, bitmap 0x3a1 (929) */
    { NEG1,  STR_STATIC, 14, 36, 60, 8, "Group comment" },
    { 25126, STR_EDITML, 82, 36, 220, 28, "" },              /* CString +0xdf8 -> group +0x980 (DDV max 1999) */
    { 673,   STR_GROUP,  14, 71, 310, 87, "Machine - Toolpath Copy" },
    { 684,   STR_STATIC, 19, 88, 28, 8, "Edit" },
    { 467,   STR_BMPBTN, 51, 85, 16, 14, "" },               /* +0x8b68, bitmap 0xd92 (3474) */
    { 685,   STR_STATIC, 72, 88, 49, 8, "Replace" },
    { 463,   STR_BMPBTN, 126, 85, 16, 14, "" },              /* +0xbc58, bitmap 0x370 (880) */
    { 675,   STR_STATIC, 17, 103, 50, 8, "Description" },
    { 601,   STR_LABEL,  72, 103, 245, 8, "" },              /* CString +0xde8: CCNCMachine::Comment */
    { 676,   STR_STATIC, 17, 114, 50, 8, "From file" },
    { 607,   STR_LABEL,  72, 114, 245, 8, "" },              /* CString +0xde0: the machine definition's file name */
    { NEG1,  STR_ETCH,   19, 127, 300, 1, "" },
    { 677,   STR_STATIC, 17, 133, 50, 8, "Control" },
    { 602,   STR_LABEL,  72, 133, 245, 8, "" },              /* CString +0xdf0: CtrlDefFile's file name */
    { 686,   STR_STATIC, 17, 144, 50, 8, "Post" },
    { 604,   STR_LABEL,  72, 144, 245, 8, "" },              /* UILabel (CColorStaticST) +0x28e0: PostProcFile */
    { GP_MPPOST, STR_CHECK, 150, 87, 170, 10, "Post with Mastercam post (.pst) (ours)" },   /* ours, see GP_MPPOST */
    { 678,   STR_GROUP,  14, 161, 310, 47, "Tool Library" },
    { 1258,  STR_BMPBTN, 256, 180, 16, 14, "" },             /* +0x3ca8, 0x370 */
    { 625,   STR_LABEL,  30, 177, 220, 22, "" },             /* UILabel +0x558: ToolLibFile(units) */
    { 1266,  STR_BMPBTN, 275, 180, 16, 14, "" },             /* +0x4c68, 0xd92 (mill) */
    { 751,   STR_BMPBTN, 294, 180, 16, 14, "" },             /* +0x6be8, 0xbe0 (3040) */
    { 680,   STR_GROUP,  14, 212, 310, 47, "Operation Library" },
    { 1260,  STR_BMPBTN, 256, 225, 16, 14, "" },             /* +0x9b28, 0x370 */
    { 627,   STR_LABEL,  30, 228, 220, 22, "" },             /* UILabel +0x830: OpLibFile(units) */
    { 1264,  STR_BMPBTN, 275, 225, 16, 14, "" },             /* +0x2c10, 0xd92 */
    { 687,   STR_GROUP,  14, 263, 310, 47, "Operation Defaults" },
    { 1257,  STR_BMPBTN, 256, 276, 16, 14, "" },             /* +0xccf0, 0x370 */
    { 623,   STR_LABEL,  30, 279, 220, 22, "" },             /* UILabel +0xb08: OpDfltsFile(units) */
    { 1262,  STR_BMPBTN, 275, 276, 16, 14, "" },             /* +0x7ba8, 0xd92 */
    { 683,   STR_GROUP,  14, 315, 310, 46, "Output comments to NC file" },
    { 466,   STR_CHECK,  38, 328, 124, 10, "Output operation comments to NC" },   /* nc_output_ctrl +0x129 */
    { 476,   STR_CHECK,  38, 344, 110, 10, "Output machine name to NC" },         /* +0x12d */
    { 468,   STR_CHECK,  172, 328, 110, 10, "Output group name to NC" },          /* +0x12b */
    { 465,   STR_CHECK,  172, 342, 124, 10, "Output group comments to NC" },      /* +0x12a */
    { 635,   STR_BMPBTN, 307, 52, 6, 6, "" },                /* +0x5c28, 0x37e (894): LatheToolManager */
    { 0, 0, 0, 0, 0, 0, NULL }
};
/* ---- opmanres dialog 1589 "Group PG2 Dlg"; roles from DDX FUN_1801eb5e0 */
const StCtl GP_1589[] = {
    { 688,   STR_STATIC, 24, 26, 56, 8, "Program #" },
    { 293,   STR_EDIT,   84, 24, 58, 12, "" },               /* MCLong +0xd70 -> group +0x1178 */
    { NEG1,  STR_GROUP,  24, 44, 144, 190, "Feed Calculation" },
    { 469,   STR_RADIO,  32, 60, 62, 10, "From tool" },      /* DDX_Radio +0xd68 -> group +0x119c */
    { 470,   STR_RADIO,  32, 74, 64, 10, "From material" },
    { 471,   STR_RADIO,  32, 90, 64, 10, "From defaults" },
    { 475,   STR_RADIO,  32, 106, 64, 10, "User defined" },
    { 695,   STR_STATIC, 48, 123, 64, 8, "Spindle speed" },
    { 312,   STR_EDIT,   114, 120, 40, 12, "" },             /* MCReal +0x52b8 -> group +0x11b0 */
    { 694,   STR_STATIC, 48, 141, 64, 8, "Feed rate" },
    { 311,   STR_EDIT,   114, 138, 40, 12, "" },             /* +0x44c0 -> +0x11a0 */
    { 700,   STR_STATIC, 48, 159, 64, 8, "Retract rate" },
    { 314,   STR_EDIT,   114, 156, 40, 12, "" },             /* +0x60b0 -> +0x1188 */
    { 702,   STR_STATIC, 48, 177, 64, 8, "Plunge rate" },
    { 316,   STR_EDIT,   114, 174, 40, 12, "" },             /* +0x6ea8 -> +0x1180 */
    { 473,   STR_CHECK,  30, 198, 114, 10, "Adjust feed on arc move" },   /* +0xc88: AdjustArcFeedrates(1) */
    { 692,   STR_STATIC, 46, 214, 64, 8, "Minimum arc feed" },
    { 310,   STR_EDIT,   112, 212, 40, 12, "" },             /* +0x36c8: MinArcFeedRate */
    { NEG1,  STR_GROUP,  176, 136, 142, 78, "Advanced options" },
    { 499,   STR_CHECK,  186, 152, 130, 10, "Override defaults with modal values" },   /* +0x778 -> +0x115c */
    { 501,   STR_CHECK,  196, 167, 84, 10, "Clearance height" },   /* +0x5c8 -> +0x1160 */
    { 502,   STR_CHECK,  196, 182, 84, 10, "Retract height" },     /* +0x4f0 -> +0x1164 */
    { 504,   STR_CHECK,  196, 198, 84, 10, "Feed plane" },         /* +0x6a0 -> +0x1168 */
    { 1213,  STR_GROUP,  176, 44, 142, 83, "Toolpath Configuration" },
    { 1239,  STR_CHECK,  186, 58, 122, 10, "Assign tool numbers sequentially" },   /* +0xbb0 -> +0x11b8 */
    { 1384,  STR_CHECK,  186, 74, 122, 10, "Warn of duplicate tool numbers" },     /* +0xad8 -> +0x11bc */
    { 1290,  STR_CHECK,  186, 90, 122, 10, "Use tool's step, peck, coolant" },     /* +0xa00 -> +0x11c0 */
    { 1291,  STR_CHECK,  186, 103, 122, 18, "Search tool library when entering a tool number" },   /* +0x850 -> +0x11c4 */
    { 689,   STR_GROUP,  176, 226, 142, 50, "Sequence #" },
    { 690,   STR_STATIC, 196, 241, 32, 8, "Start" },
    { 297,   STR_EDIT,   240, 239, 40, 12, "" },             /* MCReal +0x28d0: nc_output_ctrl +0 */
    { 691,   STR_STATIC, 196, 260, 40, 8, "Increment" },
    { 304,   STR_EDIT,   240, 257, 40, 12, "" },             /* MCReal +0x1ad8: nc_output_ctrl +8 */
    { NEG1,  STR_GROUP,  24, 284, 294, 33, "Material" },
    { 477,   STR_EDIT,   29, 297, 172, 12, "" },             /* CString +0xd60 -> group +0x11d0 */
    { 481,   STR_BUTTON, 205, 297, 50, 14, "Edit..." },
    { 479,   STR_BUTTON, 259, 297, 50, 14, "Select..." },
    { 580,   STR_HIDDEN, 327, 247, 6, 14, "" },              /* "Leave these hidden controls alone" */
    { 554,   STR_HIDDEN, 329, 270, 10, 10, "" },
    { 0, 0, 0, 0, 0, 0, NULL }
};

/* the pages' copies (CGroupPg1 / CGroupPg2 members, filled from the group / machine / control at open) */
static struct {
    char name[0x105], comment[2000], ncdir[0x105], desc[0xd0], from[0x105], ctrl[0x105], post[0x105];
    char toollib[0x105], oplib[0x105], opdef[0x105], material[0x52];
    int ncout[4], arcfeed, prog, fs, ovr[4], tcfg[4];
    double rpm, feed, ret, plg;
    char prog_t[32], rpm_t[32], feed_t[32], ret_t[32], plg_t[32], minarc_t[32], seq_t[2][32];
    char cmt_lf[2000];                                     /* the comment as the front end edits it (LF line ends) */
    int visited[3];
    int mppost;                                            /* ours: GP_MPPOST (F.grp_mppost) */
} gpp;
static const McamFile *gp_src;                                   /* where the values come from: the part, or (a new part with no
                                                              machine group) the operation defaults library the save starts
                                                              from (mcam_saveops.c new_base: config 2303, cui_toolpage.c tl6410_mach) */

static const char *base_name(const char *p) {             /* PathFindFileName ('\\' or '/') */
    const char *a = strrchr(p, '\\'), *b = strrchr(p, '/'); const char *s = a > b ? a : b; return s ? s + 1 : p;
}
static void real_t(char *b, size_t n, double v) { ecp_real_str(b, n, v); }   /* make_mcreal_str */
static void cmt_to_lf(void) {                              /* CRLF (the Windows edit control's line ends) -> LF */
    size_t k = 0; for (const char *s = gpp.comment; *s && k + 1 < sizeof gpp.cmt_lf; s++) if (*s != '\r') gpp.cmt_lf[k++] = *s;
    gpp.cmt_lf[k] = 0;
}

/* FUN_1801e61b0 (Files PrmToDlg) + FUN_1801ec2d0 (Tool Settings group -> page) on the sheet's copy of the group */
void gp_open(void) {
    memset(&gpp, 0, sizeof gpp);
    gp_src = F.grp_rec ? &F : tl6410_mach();
    const McamFile *s = gp_src && gp_src->grp_rec ? gp_src : NULL;
    int met = tp_metric();                                 /* page +0x550 = IsMetric() (CGroupPg1 ctor) */
    if (s) {
        snprintf(gpp.name, sizeof gpp.name, "%s", s->grp_name); snprintf(gpp.comment, sizeof gpp.comment, "%s", s->grp_comment);
        gpp.prog = s->grp_prog; for (int k = 0; k < 4; k++) { gpp.ovr[k] = s->grp_ovr[k]; gpp.tcfg[k] = s->grp_tcfg[k]; }
        snprintf(gpp.from, sizeof gpp.from, "%s", base_name(s->grp_mdef));   /* PathFindFileName(machDefFile = group +0x355) */
    }
    if (s && s->have_grp) {
        gpp.fs = s->grp_fs_mode; gpp.rpm = s->grp_rpm; gpp.feed = s->grp_feed; gpp.ret = s->grp_retract; gpp.plg = s->grp_plunge;
        snprintf(gpp.material, sizeof gpp.material, "%s", s->grp_material);
    }
    if (gp_src && gp_src->mach_ok) {
        snprintf(gpp.desc, sizeof gpp.desc, "%s", gp_src->mach_comment);
        snprintf(gpp.ctrl, sizeof gpp.ctrl, "%s", base_name(gp_src->mach_ctrl));
        snprintf(gpp.post, sizeof gpp.post, "%s", gp_src->mach_post);           /* FUN_1801e6720: the full path */
        snprintf(gpp.toollib, sizeof gpp.toollib, "%s", gp_src->toollib[met]);  /* FUN_1801e3f20 (Type 5 wire: PowerLibFile) */
        real_t(gpp.minarc_t, sizeof gpp.minarc_t, gp_src->mach_minarc[met]);    /* MinArcFeedRate: Dynamics +0x30 / +0x34 by IsMetric */
    }
    if (gp_src && gp_src->ctl_ok) {
        snprintf(gpp.ncdir, sizeof gpp.ncdir, "%s", gp_src->ctl_ncpath);         /* NCPrefix(1) */
        snprintf(gpp.oplib, sizeof gpp.oplib, "%s", gp_src->ctl_oplib[met]);    /* FUN_1801e3ea0 */
        snprintf(gpp.opdef, sizeof gpp.opdef, "%s", gp_src->ctl_opdef[met]);    /* FUN_1801e3db0 */
        for (int k = 0; k < 4; k++) gpp.ncout[k] = gp_src->ctl_ncout[k] != 0;   /* FUN_1801e3240 */
        gpp.arcfeed = gp_src->ctl_arcfeed != 0;                                 /* FUN_1801eb070: AdjustArcFeedrates(1) */
        real_t(gpp.seq_t[0], sizeof gpp.seq_t[0], gp_src->ctl_seq[0]); real_t(gpp.seq_t[1], sizeof gpp.seq_t[1], gp_src->ctl_seq[1]);
    }
    gpp.mppost = F.grp_mppost;                                                  /* ours (GP_MPPOST) */
    snprintf(gpp.prog_t, sizeof gpp.prog_t, "%d", gpp.prog);                    /* MCLong::Init(v, 0, 0x7fffffff) */
    real_t(gpp.rpm_t, sizeof gpp.rpm_t, gpp.rpm); real_t(gpp.feed_t, sizeof gpp.feed_t, gpp.feed);
    real_t(gpp.ret_t, sizeof gpp.ret_t, gpp.ret); real_t(gpp.plg_t, sizeof gpp.plg_t, gpp.plg);
    cmt_to_lf();
}
/* a stored library path on this host (ours: Mastercam's %MCAMDIR% expansion is NOT DECODED - the rule of cui_toolpage.c
   tl6410_machine_lib, the shared folder, measured; an old versioned suffix -> the 2017 one as GetCurVerExtFromPrvVerExt
   @1803486a0 does for materials, formats/feeds_speeds.md; for .defaults-N by filter mccoreres 2800's list) */
static void lib_host(const char *m, int dflt, char *out, size_t n) {
    char p[1100]; snprintf(p, sizeof p, "%s", m); out[0] = 0;
    if (!p[0]) return;
    if (dflt) {
        static const char *old[] = { ".defaults-9", ".defaults-8", ".defaults-7", ".defaults-6", ".defaults-5", ".defaults" };
        char *dot = strrchr(p, '.');
        for (size_t i = 0; dot && i < sizeof old / sizeof *old; i++) if (!strcasecmp(dot, old[i])) { snprintf(dot, sizeof p - (size_t)(dot - p), ".mcam-defaults"); break; }
    }
    if (path_is_windows(p) && isalpha((unsigned char)p[0]) && p[1] == ':') { host_path(p, out, n); return; }
    const char *q = p; if (!strncasecmp(q, "%MCAMDIR%", 9)) q += 9;
    char t[1200]; snprintf(t, sizeof t, "<APP_DIR_SHARED>%s", q); host_path(t, out, n);
    /* ours: a relative name that is not under the shared folder itself ("Ops\Mill_MM.mcam-defaults", the defaults library's
       control) is tried under its mill product folder - where Mastercam's own shared files are (how Mastercam resolves it:
       NOT DECODED, see grp_defaults_file) */
    if (!file_exists(out) && !(path_is_windows(p) && isalpha((unsigned char)p[0]) && p[1] == ':')) {
        char o2[1200]; snprintf(t, sizeof t, "<APP_DIR_SHARED>mill\\%s", q); host_path(t, o2, sizeof o2);
        if (file_exists(o2)) snprintf(out, n, "%s", o2);
    }
}
/* Q19: OMgetGrpDefaultsFile(0) (mastercam.dll 0x180322fe0, ~/mastercam-re/work/defs_decode/mc_grpdefaults.c): the active
   machine group's OpDfltsFile (0x18015cb70 -> FUN_180157980: the machine's control, by IsMetric) when it is not empty, else none.
   Here: the part's control (its machine group), else the defaults library's (a new part, as gp_open), on this host by lib_host
   (ours: the %MCAMDIR% / old-suffix rule above). CCNCMachine::OpDfltsFile (mcmachinedef 0x180077340,
   ~/mastercam-re/work/defs_decode/mdef_opdflts.c) returns the control's string as stored (file_ctrl +0x61e inch / +0x722 metric);
   how file_exists(path, 1) / Mastercam resolve a relative one ("Ops\Mill_MM.mcam-defaults", the defaults library's own control) is
   NOT DECODED: when the string does not name an existing file here, ours = the file this program takes new operations from
   (cui_ops.c default_op: shared mill/Ops/Mill_MM or Mill_Inch.mcam-defaults by the units). "" = none. */
void grp_defaults_file(char *out, size_t n) {
    out[0] = 0;
    const McamFile *s = F.ctl_ok ? &F : tl6410_mach();
    if (!s || !s->ctl_ok) return;
    const char *v = s->ctl_opdef[tp_metric()];
    if (!v[0]) return;                                  /* OMgetGrpDefaultsFile: an empty OpDfltsFile -> none */
    lib_host(v, 1, out, n);
    if (!file_exists(out)) site_file(out, n, "shared", tp_metric() ? "mill/Ops/Mill_MM.mcam-defaults" : "mill/Ops/Mill_Inch.mcam-defaults");
}
/* FUN_1801e2f70 (Files page, run by PrmToDlg FUN_1801e61b0 when the page is set up and by OnOK FUN_1801e5590), its message
   through post_warning: an empty group name -> opmanres 3857 "Invalid group name" + DAT_1805d89a8 ("\n\n", mastercam.dll
   .rdata, decomp/mastercam_grppg1_sep.txt) + the name; else 3858 "Invalid file name" + "\n\n" + OpDfltsFile(units) when
   file_exists fails on it, else (machine Type 5: PowerLibFile - wire, NOT PORTED) + ToolLibFile(units) (the stored string,
   CCNCMachine::ToolLibFile @18007bb00 = machine +0xb26 / +0xc2b) when file_exists fails on it; nothing when both exist.
   Which file is checked: lib_host above (ours). The control focused by the return value (0x124 / 0x12f / 0x134): not ported. */
static void files_check(void) {
    if (!gpp.name[0]) { cui_message("apprenticeCAM", "Invalid group name\n\n%s", gpp.name); return; }
    if (!gp_src) return;
    char h[1200];
    if (gpp.opdef[0]) { lib_host(gpp.opdef, 1, h, sizeof h); if (!file_exists(h)) { cui_message("apprenticeCAM", "Invalid file name\n\n%s", gpp.opdef); return; } }
    if (gp_src->mach_ok && gp_src->mach_type == 5) return;
    if (gpp.toollib[0]) { lib_host(gpp.toollib, 0, h, sizeof h); if (!file_exists(h)) cui_message("apprenticeCAM", "Invalid file name\n\n%s", gpp.toollib); }
}
void gp_visit(int page) {
    if (page < 0 || page > 2) return;
    if (page == 0 && !gpp.visited[0]) files_check();          /* the page is set up: OnInitDialog -> PrmToDlg */
    gpp.visited[page] = 1;
}
int gp_visited(int page) { return page >= 0 && page < 3 && gpp.visited[page]; }

const StCtl *gp_table(int page) { return page == 0 ? GP_815 : page == 1 ? GP_1589 : NULL; }
const StCtl *gp_ctl(int page, unsigned id) { const StCtl *t = gp_table(page); for (; t && t->text; t++) if (t->id == id) return t; return NULL; }
/* the bitmap of a UIBitmapButton (FUN_1801e4410 SetBitmap; opmanres bitmaps) */
int gp_bitmap(int page, unsigned id) {
    if (page != 0) return 0;
    switch (id) {
    case 1255: return 0x3a1;
    case 467: case 1264: case 1262: case 1266: return 0xd92;  /* 1266: 0x37f only for a lathe page 0x358 */
    case 463: case 1258: case 1260: case 1257: return 0x370;
    case 751: return 0xbe0;
    case 635: return 0x37e;
    }
    return 0;
}
int gp_shown(int page, unsigned id) {
    const StCtl *c = gp_ctl(page, id);
    if (!c || c->role == STR_HIDDEN) return 0;
    /* 1266 hidden when CCNCMachine::MachTypeToCtrlType == 2 (lathe; FUN_1801e4410): shown for a mill machine */
    return 1;
}
int gp_enabled(int page, unsigned id) {
    if (!gp_shown(page, id)) return 0;
    if (page == 0) switch (id) {
        case 292: case 25126: return 1;                        /* the group's name / comment */
        case 296: return 0;                                    /* the control's NC path: not saved here (SaveControlToDb NOT PORTED) */
        case 466: case 476: case 468: case 465: return 0;      /* the control's nc_output_ctrl: read-only here */
        case 1255: case 467: case 463: case 1258: case 1266: case 1260: case 1264: case 1257: case 1262: case 635: return 0;   /* NOT PORTED */
        case 751: return 0;                                    /* MultiTaskingIsValid / NumToolsUsedInOps: NOT DECODED */
        case GP_MPPOST: return 1;                              /* ours: always (Post refuses when the group has no usable post) */
        default: return 1;                                     /* statics, labels, group boxes */
    }
    /* FUN_1801eb790: 310 = 473 checked; 580 / 481 = a material name; 501 / 502 / 504 = 499 checked; Feed Calculation 3 (User
       defined): 694 / 311 / 695 / 312 / 314 / 316 */
    switch (id) {
    case 293: case 469: case 470: case 471: case 475: case 499: case 1239: case 1384: case 1290: case 1291: return 1;
    case 501: case 502: case 504: return gpp.ovr[0] != 0;
    case 694: case 311: case 695: case 312: case 314: case 316: return gpp.fs == 3;
    case 473: case 310: case 297: case 304: return 0;        /* the control / machine records: read-only here */
    case 477: return 0;                                        /* FUN_1801eb000 needs the part's material list (not loaded) */
    case 481: case 479: return 0;                              /* edit_material / list_matl_select: NOT PORTED */
    }
    return 1;
}
int gp_checked(int page, unsigned id) {
    if (page == 0) switch (id) {
        case 466: return gpp.ncout[0]; case 465: return gpp.ncout[1]; case 468: return gpp.ncout[2]; case 476: return gpp.ncout[3];
        case GP_MPPOST: return gpp.mppost;
    }
    if (page == 1) switch (id) {
        case 469: return gpp.fs == 0; case 470: return gpp.fs == 1; case 471: return gpp.fs == 2; case 475: return gpp.fs == 3;
        case 473: return gpp.arcfeed;
        case 499: return gpp.ovr[0] != 0; case 501: return gpp.ovr[1] != 0; case 502: return gpp.ovr[2] != 0; case 504: return gpp.ovr[3] != 0;
        case 1239: return gpp.tcfg[0] != 0; case 1384: return gpp.tcfg[1] != 0; case 1290: return gpp.tcfg[2] != 0; case 1291: return gpp.tcfg[3] != 0;
    }
    return 0;
}
char *gp_text(int page, unsigned id) {
    if (page == 0) switch (id) {
        case 292: return gpp.name; case 25126: return gpp.cmt_lf; case 296: return gpp.ncdir;
        case 601: return gpp.desc; case 607: return gpp.from; case 602: return gpp.ctrl; case 604: return gpp.post;
        case 625: return gpp.toollib; case 627: return gpp.oplib; case 623: return gpp.opdef;
    }
    if (page == 1) switch (id) {
        case 293: return gpp.prog_t; case 312: return gpp.rpm_t; case 311: return gpp.feed_t; case 314: return gpp.ret_t; case 316: return gpp.plg_t;
        case 310: return gpp.minarc_t; case 297: return gpp.seq_t[0]; case 304: return gpp.seq_t[1]; case 477: return gpp.material;
    }
    return NULL;
}
/* an edit's text on kill focus (DDX on UpdateData) */
void gp_edit(int page, unsigned id, const char *t) {
    if (!gp_enabled(page, id) || !t) return;
    if (page == 0 && id == 292) {                              /* FUN_1801e4910: CString::Replace(':', '-') */
        snprintf(gpp.name, sizeof gpp.name, "%s", t);          /* _mbsnbcpy_s(+4, 0x105, .., 0x104) at OK */
        for (char *p = gpp.name; *p; p++) if (*p == ':') *p = '-';
        return;
    }
    if (page == 0 && id == 25126) {                            /* DDV_MaxChars 1999 (EM_LIMITTEXT); LF -> CRLF (ours: the edit
                                                                  control's line ends) */
        size_t k = 0;
        for (const char *s = t; *s; s++) {
            if (*s == '\r') continue;
            if (*s == '\n') { if (k + 2 > 1999) break; gpp.comment[k++] = '\r'; gpp.comment[k++] = '\n'; }
            else { if (k + 1 > 1999) break; gpp.comment[k++] = *s; }
        }
        gpp.comment[k] = 0; cmt_to_lf();
        return;
    }
    if (page != 1) return;
    char *dst = gp_text(1, id); if (!dst) return;
    snprintf(dst, 32, "%s", t);
    char *e;
    if (id == 293) {                                           /* MCLong::Init(v, 0, 0x7fffffff): taken when it parses in range
                                                                  (its out-of-range handling NOT DECODED: the text stays) */
        long v = strtol(t, &e, 10); if (e != t && v >= 0 && v <= 0x7fffffff) gpp.prog = (int)v;
        return;
    }
    double v = strtod(t, &e); if (e == t || !isfinite(v)) return;
    switch (id) { case 312: gpp.rpm = v; break; case 311: gpp.feed = v; break; case 314: gpp.ret = v; break; case 316: gpp.plg = v; break; }
}
void gp_click(int page, unsigned id) {
    if (page == 0 && id == GP_MPPOST && gp_enabled(page, id)) { gpp.mppost = !gpp.mppost; return; }   /* ours */
    if (!gp_enabled(page, id) || page != 1) return;
    switch (id) {
    case 469: gpp.fs = 0; break;
    case 471: gpp.fs = 2; break;
    case 475: gpp.fs = 3; break;
    case 470:                                                  /* FUN_1801ebd40: UpdateData(TRUE) -> 1; a material "NONE" / empty
                                                                  opens Select (FUN_1801ebf50, list_matl_select: NOT PORTED - as
                                                                  cancelled, FUN_1801eb980 returns -1 and nothing changes); the
                                                                  member is then set to 0 but UpdateData(TRUE) reads the radio back,
                                                                  still From material (as decompiled) */
        gpp.fs = 1; break;
    case 499: gpp.ovr[0] = !gpp.ovr[0]; break;                 /* BN_CLICKED -> enable rules (FUN_1801eb790) */
    case 501: gpp.ovr[1] = !gpp.ovr[1]; break;
    case 502: gpp.ovr[2] = !gpp.ovr[2]; break;
    case 504: gpp.ovr[3] = !gpp.ovr[3]; break;
    case 1239: gpp.tcfg[0] = !gpp.tcfg[0]; break;
    case 1384: gpp.tcfg[1] = !gpp.tcfg[1]; break;
    case 1290: gpp.tcfg[2] = !gpp.tcfg[2]; break;
    case 1291: gpp.tcfg[3] = !gpp.tcfg[3]; break;
    }
}

/* a new part has no machine group record: the one the save starts from (mcam_saveops.c new_base = the operation defaults
   library, config 2303; cui_toolpage.c tl6410_mach) becomes the part's, with every value it holds. 0 = none (no library) */
static int grp_ensure(void) {
    if (F.grp_rec) return 1;
    const McamFile *s = tl6410_mach();
    if (!s || s == &F || !s->grp_rec) return 0;
    F.grp_rec = 1; F.grp_id = s->grp_id; F.grp_mach = s->grp_mach;
    snprintf(F.grp_name, sizeof F.grp_name, "%s", s->grp_name); snprintf(F.grp_comment, sizeof F.grp_comment, "%s", s->grp_comment);
    snprintf(F.grp_mdef, sizeof F.grp_mdef, "%s", s->grp_mdef);
    F.grp_prog = s->grp_prog; for (int k = 0; k < 4; k++) { F.grp_ovr[k] = s->grp_ovr[k]; F.grp_tcfg[k] = s->grp_tcfg[k]; }
    if (!F.have_grp && s->have_grp) {
        F.have_grp = 1; F.grp_fs_mode = s->grp_fs_mode; F.grp_rpm = s->grp_rpm; F.grp_feed = s->grp_feed; F.grp_retract = s->grp_retract; F.grp_plunge = s->grp_plunge;
        snprintf(F.grp_material, sizeof F.grp_material, "%s", s->grp_material);
    }
    free(F.grp_names); F.grp_names = NULL; F.ngrp_names = 0;
    if (s->ngrp_names > 0) { F.grp_names = calloc((size_t)s->ngrp_names, sizeof *F.grp_names);
        if (F.grp_names) { memcpy(F.grp_names, s->grp_names, (size_t)s->ngrp_names * sizeof *F.grp_names); F.ngrp_names = s->ngrp_names; } }
    return 1;
}
/* the Toolpaths manager's machine group label = the group's name (InitTvItem -1 shows op_group +4; COpMgrTree::LoadTree on
   open, ChangeGroupNameInTree after a rename): one source, F.grp_name. No part / a new part without a group record: the
   group the save starts from (the defaults library's, "Machine Group-1" in both shipped libraries), else "Machine Group-1"
   (ours: the label's earlier default) */
void grp_name_sync(void) {
    const McamFile *s = F.grp_rec ? &F : loaded ? tl6410_mach() : NULL;
    snprintf(mach_grp_name, sizeof mach_grp_name, "%s", s && s->grp_rec ? s->grp_name : "Machine Group-1");
}
__attribute__((constructor)) static void grp_hook(void) { scene_part_changed = grp_name_sync; }
/* the Toolpaths manager's Rename of the machine group (cui_misc.c ask_ok, after its OnEndlabeledit rules): the group's name */
void grp_set_name(const char *nm) {
    if (!grp_ensure()) { snprintf(mach_grp_name, sizeof mach_grp_name, "%s", nm); return; }   /* no group anywhere: the label only (ours) */
    if (strcmp(F.grp_name, nm)) {
        for (int k = 0; k < F.ngrp_names; k++) if (!strcmp(F.grp_names[k], F.grp_name)) { snprintf(F.grp_names[k], 0x105, "%s", nm); break; }
        snprintf(F.grp_name, sizeof F.grp_name, "%.*s", 0x104, nm); F.grp_dirty = 1;
    }
    grp_name_sync();
}
/* TpGrpList::CheckGroupName(name, true) (mccore @1808fc3d0, decomp/opman_mccore_grpname.c): an empty (or blank after Trim)
   name -> -1 (no message); mccoreres 5014 "Undefined" -> post_warning 4159, 2; a name IndexByName finds -> post_warning
   "%s\n\n%s" (4161, the name), 1; else 0. post_warning's caption: the program's name (formats/provenance_audit.md
   "Intentional deviations"). Ordinal_2863's case rule: case-insensitive, as the Toolpaths manager rename (cui_misc.c);
   IndexByName: an exact match among the part's groups (its comparison NOT DECODED). */
static int check_group_name(const char *nm) {
    const char *a = nm; while (*a == ' ' || *a == '\t') a++;
    size_t n = strlen(a); while (n && (a[n - 1] == ' ' || a[n - 1] == '\t')) n--;
    if (!n) return -1;
    if (!strcasecmp(nm, "Undefined")) { cui_message("apprenticeCAM", "Naming a group 'UNDEFINED' is not allowed."); return 2; }
    for (int k = 0; gp_src && k < gp_src->ngrp_names; k++)
        if (!strcmp(gp_src->grp_names[k], nm)) { cui_message("apprenticeCAM", "Group already exists with the same name.\n\n%s", nm); return 1; }
    return 0;
}
/* the sheet's OK for the visited pages (MFC: PSN_APPLY to every page that was created) */
void gp_ok(void) {
    if (!gpp.visited[0] && !gpp.visited[1]) return;
    if (gpp.visited[0] && gpp.mppost != F.grp_mppost) { F.grp_mppost = gpp.mppost; doc_dirty = 1; }   /* ours (GP_MPPOST): not a group
                                                                  record field - the part saves unchanged; kept in the project */
    const McamFile *s = gp_src && gp_src->grp_rec ? gp_src : NULL;
    if (!s) return;                                            /* no machine group anywhere (no defaults library): nothing to write */
    char name[0x105]; snprintf(name, sizeof name, "%s", s->grp_name);
    char comment[2000]; snprintf(comment, sizeof comment, "%s", s->grp_comment);
    int prog = s->grp_prog, fs = s->grp_fs_mode, ovr[4], tcfg[4]; double rpm = s->grp_rpm, feed = s->grp_feed, ret = s->grp_retract, plg = s->grp_plunge;
    for (int k = 0; k < 4; k++) { ovr[k] = s->grp_ovr[k]; tcfg[k] = s->grp_tcfg[k]; }
    if (gpp.visited[0]) {                                      /* FUN_1801e5590: FUN_1801e3850, then FUN_1801e2f70 */
        snprintf(name, sizeof name, "%.*s", 0x104, gpp.name); snprintf(comment, sizeof comment, "%.*s", 1999, gpp.comment);
        files_check();                                         /* FUN_1801e2f70's warning (the OK continues) */
    }
    if (gpp.visited[1]) {                                      /* FUN_1801eb2b0 */
        if (!gpp.ovr[1] && !gpp.ovr[2] && !gpp.ovr[3]) gpp.ovr[0] = 0;
        if (!gpp.ovr[0]) gpp.ovr[1] = gpp.ovr[2] = gpp.ovr[3] = 0;
        for (int k = 0; k < 4; k++) { ovr[k] = gpp.ovr[k]; tcfg[k] = gpp.tcfg[k]; }
        prog = gpp.prog; fs = gpp.fs; feed = gpp.feed; rpm = gpp.rpm; ret = gpp.ret; plg = gpp.plg;
    }
    /* CGroupPg0::OnOK: a changed name goes through CheckGroupName; refused -> the original */
    int renamed = 0;
    if (strcmp(name, s->grp_name)) {
        if (check_group_name(name)) snprintf(name, sizeof name, "%s", s->grp_name);
        else renamed = 1;
    }
    int ch = strcmp(name, s->grp_name) || strcmp(comment, s->grp_comment) || prog != s->grp_prog || fs != s->grp_fs_mode
             || rpm != s->grp_rpm || feed != s->grp_feed || ret != s->grp_retract || plg != s->grp_plunge;
    for (int k = 0; k < 4; k++) ch |= ovr[k] != s->grp_ovr[k] || tcfg[k] != s->grp_tcfg[k];
    if (!ch) return;                                           /* nothing changed: the part saves byte-identically */
    if (s != &F && !grp_ensure()) return;                    /* a new part: the group the save starts from (new_base) */
    for (int k = 0; renamed && k < F.ngrp_names; k++) if (!strcmp(F.grp_names[k], F.grp_name)) { snprintf(F.grp_names[k], 0x105, "%s", name); break; }
    snprintf(F.grp_name, sizeof F.grp_name, "%s", name); snprintf(F.grp_comment, sizeof F.grp_comment, "%s", comment);
    F.grp_prog = prog; F.grp_fs_mode = fs; F.grp_rpm = rpm; F.grp_feed = feed; F.grp_retract = ret; F.grp_plunge = plg;
    for (int k = 0; k < 4; k++) { F.grp_ovr[k] = ovr[k]; F.grp_tcfg[k] = tcfg[k]; }
    F.grp_dirty = 1; doc_dirty = 1;
    grp_name_sync();                                           /* COpMgrTree::ChangeGroupNameInTree */
}

/* ---- the settings' effects on new operations / tools */
/* +0x11b8 of the active machine group (TpPartToolList::PrepareNewToolByMachine, mccore_tooladd.c l.6819) */
int grp_assign_seq(void) { return F.grp_rec && F.grp_tcfg[0] != 0; }
/* op_is_milling (mccore @1800c6190, decomp/mccore_toolfilt.c) */
static int op_is_milling(int c) {
    switch (c) {
    case 1: case 3: case 5: case 6: case 7: case 8: case 9: case 10: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: case 0x12:
    case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1d: case 0x1e:
    case 0x1f: case 0x20: case 0x21: case 0x22: case 0x23: case 0x27: case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c:
    case 0x2d: case 0x2e: case 0x30: case 0x33: case 0x34: case 0x35: case 100: case 0x65: case 0x66: case 0x67: case 0x68:
    case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f: case 0x70: case 0x71: case 0x83: case 0x84:
    case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: case 0x8a: case 0x8b: return 1;
    }
    return 0;
}
/* InitOperation (mccore 0x180046660, decomp/mccore_ops.c l.2088-2122): a new milling op whose predecessor (the last op of the
   op list) is milling, with the active machine group's +0x115c set, copies the predecessor's clearance (op +0x108 value,
   +0x110 incremental, +0x111 on) when +0x1160, retract (+0x118 / +0x120 / +0x121) when +0x1164, feed plane (+0x128 value,
   +0x130 incremental) when +0x1168 = record +0x170 / +0x178 / +0x179, +0x180 / +0x188 / +0x189, +0x190 / +0x198 - value and
   flags as they are. Here: the defaults op's record the new op is made from (cui_ops.c heights_from_default_op) with those
   bytes replaced by the predecessor's record bytes. A stored op: its record. An op made / edited here: the bytes the save
   would write for it (mcam_saveops.c mcam_op_record_heights: its own stored record or the defaults op it is made from,
   through the save's height rule, then its record patches), so its kept flags (clearance off, incremental values, Drill's
   R plane flag) are copied as they would be stored. NULL = no copy. The record stays valid until the next call. */
static uint8_t *modal_rec, *modal_pred;
const uint8_t *grp_modal_record(const uint8_t *base, uint32_t n, int new_opcode) {
    free(modal_rec); modal_rec = NULL; free(modal_pred); modal_pred = NULL;
    if (!F.grp_rec || !F.grp_ovr[0] || F.nops <= 0 || !op_is_milling(new_opcode) || !base || n < 0x1a0) return NULL;
    int last = F.nops - 1; const CamOp *c = NULL;
    for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == last) c = camops[j];
    int st, pc = c ? mcam_opcode_for(&c->p, &st) : F.ops[last].opcode;
    if (!op_is_milling(pc)) return NULL;
    const uint8_t *pr = NULL; uint32_t prn = 0;
    if (!c) { pr = F.ops[last].raw; prn = F.ops[last].raw_n; }
    else {
        const uint8_t *pb = NULL; uint32_t pbn = 0;
        if (c->p.file_op_id > 0) for (int i = 0; i < F.nops; i++) if (F.ops[i].id == c->p.file_op_id && F.ops[i].raw_n == 0x2938) { pb = F.ops[i].raw; pbn = F.ops[i].raw_n; }   /* full OP_ID (the save's rule) */
        if (!pb) { const Op *d = default_op(c->p.type, mcam_is_metric(&F)); if (d) { pb = d->raw; pbn = d->raw_n; } }
        if (!pb || pbn < 0x1a0) return NULL;
        modal_pred = malloc(pbn); if (!modal_pred) return NULL;
        mcam_op_record_heights(&F, c, pb, pbn, modal_pred);
        pr = modal_pred; prn = pbn;
    }
    if (!pr || prn < 0x1a0) return NULL;
    modal_rec = malloc(n); if (!modal_rec) return NULL;
    memcpy(modal_rec, base, n);
    uint8_t *r = modal_rec;
    if (F.grp_ovr[1]) memcpy(r + 0x170, pr + 0x170, 10);          /* +0x170 value, +0x178 inc, +0x179 on */
    if (F.grp_ovr[2]) memcpy(r + 0x180, pr + 0x180, 10);
    if (F.grp_ovr[3]) { memcpy(r + 0x190, pr + 0x190, 8); r[0x198] = pr[0x198]; }
    return modal_rec;
}

/* ---- the project file (ours: the GRPSET / GRPNAME / GRPCMT lines, as the STOCK line of cui_doc.c) */
void gp_write_project(FILE *f) {
    if (F.grp_mppost) fprintf(f, "GRPMPPOST 1\n");           /* ours (GP_MPPOST): only when on, so older projects read the same */
    if (!F.grp_dirty) return;
    fprintf(f, "GRPSET %d %d %.17g %.17g %.17g %.17g %d %d %d %d %d %d %d %d\n", F.grp_prog, F.grp_fs_mode, F.grp_rpm, F.grp_feed, F.grp_retract, F.grp_plunge,
            F.grp_ovr[0], F.grp_ovr[1], F.grp_ovr[2], F.grp_ovr[3], F.grp_tcfg[0], F.grp_tcfg[1], F.grp_tcfg[2], F.grp_tcfg[3]);
    fprintf(f, "GRPNAME %s\n", F.grp_name);
    fputs("GRPCMT ", f);                                       /* \\ \r \n escaped */
    for (const char *p = F.grp_comment; *p; p++) { if (*p == '\\') fputs("\\\\", f); else if (*p == '\r') fputs("\\r", f); else if (*p == '\n') fputs("\\n", f); else fputc(*p, f); }
    fputc('\n', f);
}
int gp_read_project_line(const char *line) {
    if (!strncmp(line, "GRPMPPOST ", 10)) { F.grp_mppost = atoi(line + 10) != 0; return 1; }
    if (!strncmp(line, "GRPSET ", 7)) {
        int a[10]; double v[4];
        if (sscanf(line + 7, "%d %d %lf %lf %lf %lf %d %d %d %d %d %d %d %d", &a[0], &a[1], &v[0], &v[1], &v[2], &v[3], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9]) == 14) {
            if (!grp_ensure()) return 1;                       /* a new part: the defaults library's group (as gp_ok) */
            F.grp_prog = a[0]; F.grp_fs_mode = a[1]; F.grp_rpm = v[0]; F.grp_feed = v[1]; F.grp_retract = v[2]; F.grp_plunge = v[3];
            for (int k = 0; k < 4; k++) { F.grp_ovr[k] = a[2 + k]; F.grp_tcfg[k] = a[6 + k]; }
            F.grp_dirty = 1;
        }
        return 1;
    }
    if (!strncmp(line, "GRPNAME ", 8) && F.grp_rec) { snprintf(F.grp_name, sizeof F.grp_name, "%s", line + 8); F.grp_name[strcspn(F.grp_name, "\r\n")] = 0; grp_name_sync(); return 1; }
    if (!strncmp(line, "GRPCMT ", 7) && F.grp_rec) {
        size_t k = 0;
        for (const char *p = line + 7; *p && *p != '\n' && *p != '\r' && k + 1 < sizeof F.grp_comment; p++) {
            if (*p == '\\' && p[1]) { p++; F.grp_comment[k++] = *p == 'r' ? '\r' : *p == 'n' ? '\n' : *p; }
            else F.grp_comment[k++] = *p;
        }
        F.grp_comment[k] = 0; return 1;
    }
    return 0;
}

/* ---- dump (both pages) and the stored group */
static void dump_str(const char *s) {                     /* control characters and ANSI bytes >= 0x80 escaped (the dumps stay ASCII) */
    putchar('\''); for (; *s; s++) { unsigned char c = (unsigned char)*s; if (c == '\r') fputs("\\r", stdout); else if (c == '\n') fputs("\\n", stdout); else if (c >= 0x80) printf("\\x%02x", c); else putchar(c); } putchar('\''); }
void gp_dump(void) {
    printf("    grpprops group rec %d id %d mach %d name ", F.grp_rec, F.grp_id, F.grp_mach); dump_str(F.grp_name);
    printf(" comment "); dump_str(F.grp_comment);
    printf(" prog %d fs %d rpm %g feed %g ret %g plg %g ovr %d %d %d %d tcfg %d %d %d %d dirty %d tree ", F.grp_prog, F.grp_fs_mode, F.grp_rpm, F.grp_feed,
           F.grp_retract, F.grp_plunge, F.grp_ovr[0], F.grp_ovr[1], F.grp_ovr[2], F.grp_ovr[3], F.grp_tcfg[0], F.grp_tcfg[1], F.grp_tcfg[2], F.grp_tcfg[3], F.grp_dirty);
    dump_str(mach_grp_name);
    if (F.grp_mppost) printf(" mppost 1");                     /* ours (GP_MPPOST): printed only when on - the earlier dumps stay */
    putchar('\n');
    printf("    grpprops machine id %d '%s' ctrl '%s' post '%s' tools '%s' | '%s' minarc %g %g control id %d ok %d seq %g %g %g ncout %d %d %d %d arcfeed %d\n",
           F.mach_id, F.mach_comment, base_name(F.mach_ctrl), base_name(F.mach_post), base_name(F.toollib[0]), base_name(F.toollib[1]), F.mach_minarc[0], F.mach_minarc[1],
           F.ctl_id, F.ctl_ok, F.ctl_seq[0], F.ctl_seq[1], F.ctl_seq[2], F.ctl_ncout[0], F.ctl_ncout[1], F.ctl_ncout[2], F.ctl_ncout[3], F.ctl_arcfeed);
    if (F.ctl_pgfb) printf("    grpprops control pages by file order (no id match): %x\n", F.ctl_pgfb);   /* resolve_machine's fallback */
    if (!show_stock_setup || st_page > 1) return;
    int pg = st_page;
    printf("    grpprops page %d visited %d %d %d:", pg, gpp.visited[0], gpp.visited[1], gpp.visited[2]);
    for (const StCtl *c = gp_table(pg); c->text; c++) {
        if (c->id == NEG1 || !gp_shown(pg, c->id)) continue;
        if (c->id == GP_MPPOST && !gp_checked(pg, c->id)) continue;   /* ours: listed only when checked (the earlier dumps stay) */
        const char *t = gp_text(pg, c->id);
        int chk = c->role == STR_CHECK || c->role == STR_RADIO;
        if (!t && !chk && gp_enabled(pg, c->id)) continue;
        printf(" %u", c->id);
        if (chk && gp_checked(pg, c->id)) printf("[x]");
        if (t) { putchar('='); dump_str(t); }
        if (!gp_enabled(pg, c->id)) printf("(g)");
    }
    printf("\n");
}
