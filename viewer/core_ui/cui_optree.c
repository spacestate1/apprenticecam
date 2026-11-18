/* cui_optree.c - the Toolpaths manager item texts (viewer/optree_text.inc: InitTvItem / InitTvOpText) for the core and the
   Qt front end, and Doc file (COpMgrTree::DocFile). optree_text.inc is plain C with static helpers; mcamui.c includes its
   own copy for its tree. tm_describe and the SystemPlanes names moved here from viewer-qt core_bridge.c (phase 6), Doc
   file from mcamui.c (op_doc_file). */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include "core_ui.h"
#include "opcodes.h"
#include "mcam_saveops.h"

#include "optree_text.inc"
/* tl_make_description(tp_tool) 0x1800db8a0 (upper-cased) / TpToolList::CreateToolDescription 0x1808f34e0 (not): by the
   part's units (IsMetric): inch "%.4lf", metric "M%.2lf"; taps add " X %.2lf" of info+0x18; type 20 "#%ld - %s - %s".
   The branch for tp_tool+0x2c0 == 1 (a word of string 5023) is NOT DECODED and not taken (+0x2c0 is not read here).
   (moved from viewer/toolmgr_ui.inc tm_describe / tm_info_pitch) */
static void tm_describe(const Tool *t, char *o, size_t n, int upper) {
    int met = mcam_is_metric(&F); const char *ty = tool_type_name(t->type);
    if (t->type == 20) snprintf(o, n, "#%ld - %s - %s", (long)t->num, ty, t->name);
    else if (t->type == 4 || t->type == 5) snprintf(o, n, met ? "#%ld - M%.2f X %.2f %s - %s%s" : "#%ld - %.4f X %.2f %s - %s%s", (long)t->num, t->dia, tm_info_pitch(t), ty, t->type == 5 ? " - " : "", t->name);
    else snprintf(o, n, met ? "#%ld - M%.2f %s - %s" : "#%ld - %.4f %s - %s", (long)t->num, t->dia, ty, t->name);
    if (upper) for (char *p = o; *p; p++) *p = (char)toupper((unsigned char)*p);
}
/* Planes manager rows: the part's MastercamPlanes "SystemPlanes" archive (seen in the user's parts: ids 1-7 = Top, Front,
   Back, Bottom, Right side, Left side, Iso; the archive is not parsed here, so these seven are listed) - levels_ui.inc */
static const char *SYS_PLANES[7] = { "Top", "Front", "Back", "Bottom", "Right side", "Left side", "Iso" };
void cui_tm_describe(const Tool *t, char *o, size_t n, int upper) { tm_describe(t, o, n, upper); }
const char *cui_sys_plane(int i) { return i >= 0 && i < 7 ? SYS_PLANES[i] : ""; }
void cui_otx_op_row(int i, char *out, size_t n) { otx_op_row(i, out, n); }
void cui_otx_params_row(int i, char *out, size_t n) { otx_params_row(i, out, n); }
void cui_otx_tool_row(int i, char *out, size_t n) { otx_tool_row(i, out, n); }
void cui_otx_geom_row(int i, char *out, size_t n) { otx_geom_row(i, out, n); }
void cui_otx_path_row(int i, char *out, size_t n) { otx_path_row(i, out, n); }
int cui_otx_is_drill(int oc) { return otx_is_drill(oc); }

/* Doc file: a text dump of the tree (file name + time stamp, then the items indented by depth), as Mastercam's DocFile */
void op_doc_file(void) {
    /* COpMgrTree::DocFile -> DumpItemArray -> CStringArrayToFile (optree_text.inc): "<part name> Operations Manager.txt"
       (opmanres 3367) in the temp directory, then opened in the editor; nothing is said in the prompt */
    char path[1400], nm[400] = "", t[512]; const char *tmp = getenv("TMPDIR");
    if (F.path[0]) { const char *b = strrchr(F.path, '/'); b = b ? b + 1 : F.path; snprintf(nm, sizeof nm, "%s", b); char *x = strrchr(nm, '.'); if (x) *x = 0; }
    snprintf(path, sizeof path, "%s/%s%sOperations Manager.txt", tmp && tmp[0] ? tmp : "/tmp", nm, nm[0] ? " " : "");
    FILE *o = fopen(path, "w");
    if (!o) return;                                      /* SysFile::Open failure: CStringArrayToFile just returns */
    time_t now = time(NULL); char ts[96]; strftime(ts, sizeof ts, "%A, %B %d, %Y   %H:%M:%S", localtime(&now));
    snprintf(t, sizeof t, "%s    %s", F.path, ts); otx_line(o, 0, t); otx_line(o, 0, " ");
    otx_line(o, 0, mach_grp_name); otx_line(o, 1, "Properties - Mill Default");
    /* the Properties children of a mill group: -0x10 / -0x11 / -0x12 (opmanres 3829 / 3826 / 3827); the 4th (-0x13 "Safety zone")
       only for a wire group with an Agie machine, its label gated by RunningSafetyZone() = false (opman_tree_all.c l.29239,
       decomp/mastercam_safetyzone_editgroup.c) */
    otx_line(o, 2, "Files"); otx_line(o, 2, "Tool settings"); otx_line(o, 2, "Stock setup");
    otx_line(o, 1, tp_grp_name);
    for (int i = 0; i < F.nops; i++) {
        otx_op_row(i, t, sizeof t); otx_line(o, 2, t);
        otx_params_row(i, t, sizeof t); otx_line(o, 3, t); otx_tool_row(i, t, sizeof t); otx_line(o, 3, t);
        otx_geom_row(i, t, sizeof t); otx_line(o, 3, t); otx_path_row(i, t, sizeof t); otx_line(o, 3, t);
    }
    fclose(o);
    if (!getenv("MCAMUI_SCRIPT")) {                      /* edit_filename: Mastercam's editor; here the desktop's default */
#ifndef _WIN32
        pid_t pid = fork(); if (pid == 0) { execlp("xdg-open", "xdg-open", path, (char *)NULL); _exit(127); }
#endif                                               /* Windows: the file is written, not opened (the editor launch is not ported there) */
    }
    if (getenv("MCAMUI_SCRIPT")) fprintf(stderr, "docfile: %s\n", path);
}
