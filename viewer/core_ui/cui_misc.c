/* cui_misc.c - the remaining small UI models of viewer/mcamui.c, moved out in phase 8 of formats/qt_port_plan.md (names and
 * citations as they were there): the Configuration - Screen Grid page (mccoreres 1589), the Toolpaths manager's group
 * rename (TVM_EDITLABEL / OnEndlabeledit), the entity clipboard + Paste, the zoom window, Print / Help / About flags, the
 * backstage pages' templates and the Info page's values, the graphics cross cursor, the gallery tooltips' texts. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "paths.h"
#include "core_ui.h"
#include "mcsurf.h"
#include "mcam_saveops.h"

/* ------------------------------------------------------------------------------------------------ Screen Grid (1589) */
/* mccoreres dialog 1589 "Configuration - Screen Grid" (the Configuration page holding mcamxm.config [GRID SETTINGS]
   23002-23007): Spacing X / Y (25213 / 25217), Origin X / Y (25211 / 25215) + Select (25203: picking NOT PORTED, greyed),
   Snap Near / Always (25208 / 25206), Size (25204). Only the page's controls are drawn (x - 140 du, y - 15 du); the
   Configuration dialog around it is not ported. The invented style / height / colour / snap-switch / preset controls are
   gone. A typed number applies while it is typed (spacing / size > 0; the origin any number). */
int show_gridset;
char gs_txt[5][32]; int gs_inited;
double *gs_field(int k) { double *f[5] = { &grid.dx, &grid.dy, &grid.ox, &grid.oy, &grid.size }; return k >= 0 && k < 5 ? f[k] : NULL; }
void gs_fill(void) { for (int i = 0; i < 5; i++) snprintf(gs_txt[i], sizeof gs_txt[i], "%.5f", *gs_field(i)); gs_inited = 1; }
void gs_edit(int k) {
    char *e; double v = strtod(gs_txt[k], &e);
    if (e != gs_txt[k] && (k == 2 || k == 3 || v > 0)) { *gs_field(k) = v; overlay_dirty = 1; }
}
void gs_close(void) { show_gridset = 0; gs_inited = 0; }
/* the page's controls (exports/mccoreres_dialogs.tsv 1589; positions as drawn: x - 140, y - 15 du); k = the edit's field */
const GsCtl GS_1589[] = {
    { 25209, 'g', 150, 25, 115, 52, -1, "Spacing" }, { 25214, 's', 198, 40, 8, 8, -1, "X" }, { 25213, 'e', 214, 37, 40, 14, 0, "" },
    { 25218, 's', 198, 60, 8, 8, -1, "Y" }, { 25217, 'e', 214, 57, 40, 14, 1, "" },
    { 25202, 'g', 150, 79, 115, 72, -1, "Origin" }, { 25212, 's', 198, 91, 8, 14, -1, "X" }, { 25211, 'e', 214, 91, 40, 14, 2, "" },
    { 25216, 's', 198, 111, 8, 14, -1, "Y" }, { 25215, 'e', 214, 111, 40, 14, 3, "" }, { 25203, 'b', 238, 131, 16, 14, -1, "" },
    { 25207, 'g', 150, 155, 115, 42, -1, "Snap" }, { 25208, 'r', 159, 167, 53, 10, 1, "Near" }, { 25206, 'r', 159, 181, 53, 10, 2, "Always" },
    { 25205, 's', 188, 201, 15, 14, -1, "Size" }, { 25204, 'e', 214, 201, 40, 14, 4, "" }, { 0, 0, 0, 0, 0, 0, 0, NULL } };

/* ------------------------------------------------------------------------------------------------ group rename */
/* Toolpaths manager 3585 Rename (formats/opmanager_menu.md): TVM_EDITLABEL on the clicked group - an in-place edit of the
   tree item; OnEndlabeledit@180361440 / TpGrpList::CheckGroupName: the text is cut at the first ':', <= 259 chars; empty ->
   the old name stays; "Undefined" refused (4159), a name the other group has refused (4161). (mcamui showed a modal box
   "Rename" / "Name:" - its texts were not Mastercam's; both front ends now edit the tree item, phase 8.) */
AskBox ask;
void ask_open(int act, const char *title, const char *l1, const char *v1, const char *l2, const char *v2) {
    memset(&ask, 0, sizeof ask); ask.act = act; snprintf(ask.title, sizeof ask.title, "%s", title);
    snprintf(ask.l[0], sizeof ask.l[0], "%s", l1); snprintf(ask.v[0], sizeof ask.v[0], "%s", v1); ask.nf = 1;
    if (l2) { snprintf(ask.l[1], sizeof ask.l[1], "%s", l2); snprintf(ask.v[1], sizeof ask.v[1], "%s", v2); ask.nf = 2; }
}
void ask_ok(void) {
    if (ask.act == CM_GRP_RENAME) {                       /* OnEndlabeledit / CheckGroupName */
        char t[270]; snprintf(t, sizeof t, "%s", ask.v[0]); char *c = strchr(t, ':'); if (c) *c = 0;
        t[259] = 0;
        if (!t[0]) return;                                     /* empty: keeps the old name */
        if (!strcasecmp(t, "Undefined")) { cui_message("Toolpath Manager", "Naming a group 'UNDEFINED' is not allowed."); return; }
        if (!strcmp(t, cm_group == 2 ? tp_grp_name : mach_grp_name)) { cui_message("Toolpath Manager", "Group already exists with the same name."); return; }
        if (cm_group == 2) grp_set_name(t);                    /* the machine group's record +4 (cui_grpprops.c: one source) */
        else snprintf(tp_grp_name, sizeof tp_grp_name, "%s", t);
        doc_dirty = 1;
    }
}
/* the edit ends: Enter / focus lost = OnEndlabeledit with the text, Esc = no text (the label stays) */
void ask_end(int commit) { if (commit) { int a = ask.act; ask_ok(); if (ask.act == a) ask.act = 0; } else ask.act = 0; }
void grp_rename_begin(void) { ask_open(CM_GRP_RENAME, "Rename", "", cm_group == 2 ? mach_grp_name : tp_grp_name, NULL, NULL); }
int grp_renaming(int group) { return ask.act == CM_GRP_RENAME && cm_group == group; }   /* group 2 = the machine group, 1 = the toolpath group */

/* ------------------------------------------------------------------------------------------------ clipboard */
/* clipboard (Ctrl+X / Ctrl+C / Ctrl+V): entity definitions; Paste puts copies in the same place, selected */
static Clip *clip; int nclip;
static void clip_free(void) { for (int i = 0; i < nclip; i++) { free(clip[i].srec); free(clip[i].brec); } nclip = 0; }
int copy_selected(void) {
    clip_free();
    for (int i = 0; ent_sel && i < ent_sel_n && i < F.nents; i++) if (ent_sel[i] && F.ents[i].type && F.ents[i].type != E_NOTE && F.ents[i].type != E_MESH) {   /* notes: copy / paste
                                                             of drafting NOT PORTED; meshes: NOT PORTED */
        const Ent *e = &F.ents[i]; Clip *nc = realloc(clip, sizeof(Clip) * (nclip + 1));
        if (!nc) { cui_message("apprenticeCAM", "Not enough memory for the clipboard."); clip_free(); return 0; }   /* ours */
        clip = nc; Clip c = { e->type, e->level, e->color, e->a, e->b, e->c, e->r, e->a0, e->sw, e->type == E_ARC ? e->vw : 0, { 0 }, NULL, NULL, 0, 0, 0, 0 };
        if (e->type == E_SURF) {                          /* the record as stored, and a trimmed (D+0) / offset (D+0x20) surface's base: merge_db2
                                                             brings the referenced records along (ours: the record copy is the clipboard part) */
            const Surf *sf = &F.surfs[e->srf]; c.srec = malloc(sf->n); memcpy(c.srec, sf->rec, sf->n); c.sn = sf->n;
            const Surf *b = sf->kind == 10 ? mcam_surf_base(&F, sf) : NULL;
            if (sf->kind == 8 && sf->n >= 0x128) { int64_t be; memcpy(&be, sf->rec + 0x120, 8); b = mcs_surf(&F, be); }
            if (b) { c.brec = malloc(b->n); memcpy(c.brec, b->rec, b->n); c.bn = b->n; c.blevel = b->level; c.bcolor = b->color; }
            else if (sf->kind == 10 || sf->kind == 8) { free(c.srec); c.srec = NULL; continue; }   /* a base that cannot be found: left out */
        }
        if (e->type == E_SPLINE) {                        /* a spline curve: its class 8 / 0x80 record (merge_db2 copies the record as the other
                                                             entities'); one without its record is left out */
            if (e->crv < 0 || e->crv >= F.ncrvs) continue;
            const CurveRec *cr = &F.crvs[e->crv]; c.srec = malloc(cr->n); if (!c.srec) continue; memcpy(c.srec, cr->rec, cr->n); c.sn = cr->n;
        }
        if (c.vw) memcpy(c.vm, e->vm, sizeof c.vm);       /* a view arc keeps its view (formats/mcam.md "Arc views") */
        clip[nclip++] = c;
    }
    return nclip;
}
/* Paste (mccadutil runPaste -> FUN_180105610): a Paste panel with prompt 5457, Enter or Esc continues, then merge_db2
   adds the clipboard part in place; attribute overrides default off (the panel's checkbox names are NOT DECODED, so
   not offered). */
int paste_pending;
void paste_clip(void) {
    if (!nclip) { cam_msg[0] = 0; return; }
    paste_pending = 0;
    int from = F.nents;
    for (int i = 0; i < nclip; i++) {
        if (clip[i].type == E_SURF) {
            uint8_t *r = malloc(clip[i].sn); memcpy(r, clip[i].srec, clip[i].sn);
            if (clip[i].brec) {                           /* the base first (blanked, as stored), the surface pointing at it */
                int bk = mcam_new_surf(&F, clip[i].brec, clip[i].bn, clip[i].blevel, clip[i].bcolor);
                Surf *bs = &F.surfs[F.ents[bk].srf]; bs->blanked = (bs->rec[5] & 1) != 0; F.ents[bk].blanked = bs->blanked;
                int64_t be = F.ents[bk].septr; memcpy(r + (r[0x68] == 8 ? 0x120 : 0x100), &be, 8);
            }
            mcam_new_surf(&F, r, clip[i].sn, clip[i].level, clip[i].color); free(r);
            continue;
        }
        if (clip[i].type == E_SPLINE) {                   /* the curve record again (mcam_new_curve: the class 8 / 0x80 record, its polyline) */
            if (clip[i].srec) mcam_new_curve(&F, clip[i].srec, clip[i].sn, clip[i].level, clip[i].color);
            continue;
        }
        if (clip[i].type == E_ARC && clip[i].vw) mcam_new_arc_view_w(&F, clip[i].vw, clip[i].vm, clip[i].c, clip[i].r, clip[i].a0, clip[i].sw, clip[i].level, clip[i].color);
        else mcam_new_ent(&F, clip[i].type, clip[i].a, clip[i].b, clip[i].c, clip[i].r, clip[i].a0, clip[i].sw, clip[i].level, clip[i].color);
    }
    undo_ents_added(from); doc_dirty = 1; scene_surfs_update(0); scene_reupload(); overlay_dirty = 1;
    cam_msg[0] = 0;
}
void edit_copy(void) { copy_selected(); cam_msg[0] = 0; }                                   /* OnEditCopy */
void edit_cut(void) { if (copy_selected()) delete_selected_ents(); else cam_msg[0] = 0; }   /* OnEditCut */
void edit_paste(void) { if (nclip) { paste_pending = 1; snprintf(cam_msg, sizeof cam_msg, "Choose any attributes to set to the current user settings - Press Enter or Esc to continue"); } }   /* OnEditPaste, 5457 */

/* ------------------------------------------------------------------------------------------------ zoom window, flags */
int zoom_window, zw_active, show_help, show_about, want_print, show_info;
void zoom_window_start(void) { zoom_window = 1; snprintf(cam_msg, sizeof cam_msg, "Specify zoom window"); }   /* OnScreenZoom */
/* Zoom window: the rectangle (view pixels) fills the view of vw x vh */
void zoom_window_done(float x0, float y0, float x1, float y1, int vw, int vh) {
    zw_active = 0; zoom_window = 0; cam_msg[0] = 0;
    float rw = x1 > x0 ? x1 - x0 : x0 - x1, rh = y1 > y0 ? y1 - y0 : y0 - y1;
    if (rw > 4 && rh > 4) {
        view_zoom_push();
        double cx, cy; view_px_to_view((x0 + x1) / 2.0f, (y0 + y1) / 2.0f, &cx, &cy);
        float k = rw / vw > rh / vh ? rw / vw : rh / vh;
        vcx = cx; vcy = cy; vht *= k;                      /* the rectangle fills the view */
    }
}
/* About apprenticeCAM (ours, formats/provenance_audit.md "Intentional deviations": Mastercam's dialog 103 is licence / version
   information) */
const char *const ABOUT_TEXT = "apprenticeCAM - an experimental CAM workbench (MIT License). Reads and writes .mcx-9 / .mcam part files, 2D toolpaths, posting. "
                               "Use at your own risk.";
/* Print (OnFilePrint, INTENTIONAL: a BMP of the window): the file next to the part / project */
void print_path(char *out, size_t n) {
    char dir[1100] = "."; const char *src = F.path[0] ? F.path : doc_path;
    const char *sl = path_sep_r(src); if (sl) snprintf(dir, sizeof dir, "%.*s", (int)(sl - src), src);
    snprintf(out, n, "%s" PATH_SEP_S "print-%ld.bmp", dir, (long)time(NULL));
}
void print_done(const char *out, int ok) { if (ok) snprintf(cam_msg, sizeof cam_msg, "Print: saved %s", out); else snprintf(cam_msg, sizeof cam_msg, "Print: cannot write %s", out); }

/* the graphics-window cursor: Mastercam's (gafres.dll CURSOR 4: a 17 px inverting cross, hotspot in the middle) over the
   view, the normal arrow over panels and dialogs; the cross is generated here, not copied. 32 x 32, 1 bpp rows of 4 bytes,
   data 1 + mask 0 = the inverted screen pixel (hotspot 15, 15) */
void cui_cross_cursor(unsigned char data[128], unsigned char mask[128]) {
    memset(data, 0, 128); memset(mask, 0, 128);
    for (int k = -8; k <= 8; k++) { int x = 15 + k, y = 15;
        data[y * 4 + x / 8] |= (unsigned char)(0x80 >> (x % 8)); data[(15 + k) * 4 + 15 / 8] |= (unsigned char)(0x80 >> (15 % 8)); }
}

/* the palette tooltips' description texts are Mastercam's, so they are not shipped: setup_from_install.py writes
   <config dir>/gallery_tips.tsv from the user's own install (tools/gen_gallery_tips.py; MCAMUI_GALLERY_TIPS = another
   file). Without it the tooltips are the item names only. */
typedef struct { char key[128], tip[400]; } GalTip;
static GalTip *gtips; static int ngtips = -1;
const char *gal_tip(const char *key) {
    if (ngtips < 0) {
        ngtips = 0; char p[1100]; const char *e = getenv("MCAMUI_GALLERY_TIPS");
        if (e) snprintf(p, sizeof p, "%s", e); else config_file(p, sizeof p, "gallery_tips.tsv");
        FILE *f = fopen(p, "r"); char line[600]; int cap = 0;
        while (f && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0; char *t = strchr(line, '\t'); if (!t) continue; *t++ = 0;
            if (ngtips == cap) { cap = cap ? cap * 2 : 512; gtips = realloc(gtips, cap * sizeof *gtips); }
            snprintf(gtips[ngtips].key, sizeof gtips[ngtips].key, "%s", line); snprintf(gtips[ngtips].tip, sizeof gtips[ngtips].tip, "%s", t); ngtips++;
        }
        if (f) fclose(f);
    }
    for (int i = 0; i < ngtips; i++) if (!strcmp(gtips[i].key, key)) return gtips[i].tip;
    return NULL;
}

/* ------------------------------------------------------------------------------------------------ backstage */
/* backstage pages: ribbonuires.dll dialog templates (exports/ribbonuires_dialogs.tsv, generated) - id, class (0 static,
   1 button, 2 edit, 3 list box, 4 up-down), x y w h in dialog units, text (\n = a line break in the caption) */
const BsCtl BS_105[] = {
    { 1057, 0, 24, 0, 139, 34, "Convert" }, { 1054, 1, 24, 34, 56, 44, "&Migration Wizard" }, { 1061, 0, 94, 34, 193, 18, "Migration Wizard" },
    { 1058, 0, 94, 52, 193, 26, "" }, { 1055, 1, 24, 88, 56, 44, "&Import\nFolder" }, { 1062, 0, 94, 88, 193, 16, "Import Folder" },
    { 1059, 0, 94, 104, 193, 27, "" }, { 1056, 1, 24, 142, 56, 44, "&Export\nFolder" }, { 1063, 0, 94, 142, 193, 16, "Export Folder" },
    { 1060, 0, 94, 158, 193, 27, "" }, { 106, 0, 293, 8, 1, 187, "" }, { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl BS_107[] = {
    { 1057, 0, 24, 0, 318, 34, "Info" }, { 1094, 1, 24, 34, 56, 44, "&Project Manager" }, { 1095, 0, 94, 34, 248, 18, "Project Manager" },
    { 1096, 0, 94, 52, 248, 26, "Use the Project Manager to specify the types of files to save in your project folder. Specifying these file types lets you keep all project files in a single place." },
    { 1097, 1, 24, 87, 56, 44, "&Change Recognition" }, { 1098, 0, 94, 88, 248, 16, "Change Recognition" },
    { 1100, 0, 94, 104, 248, 27, "Compare the geometry of two versions of a part file, identify changed geometry, view the status of affected operations, and make decisions on updating the original file." },
    { 1102, 1, 24, 142, 56, 44, "&Track\nChanges" }, { 1104, 0, 94, 142, 248, 16, "Track Changes" }, { 1106, 0, 94, 158, 248, 27, "" },
    { 1107, 1, 24, 196, 56, 44, "&AutoSave / Backup" }, { 1108, 0, 94, 196, 248, 16, "AutoSave / Backup" }, { 1109, 0, 94, 212, 248, 27, "" },
    { 1110, 1, 24, 249, 56, 44, "&Repair\nFile" }, { 1111, 0, 94, 249, 248, 16, "Repair File" },
    { 1112, 0, 94, 265, 248, 27, "Perform routine maintenance on the current file to improve performance and ensure file integrity." },
    { 106, 0, 342, 9, 1, 281, "" }, { 1116, 0, 354, 50, 66, 10, "Name" }, { 1171, 0, 429, 50, 176, 10, "" }, { 1117, 0, 354, 62, 66, 10, "Type" },
    { 1172, 0, 429, 62, 176, 10, "" }, { 1122, 0, 354, 74, 66, 10, "Folder Path" }, { 1177, 0, 429, 74, 176, 10, "" }, { 1115, 0, 354, 86, 66, 10, "Size" },
    { 1170, 0, 429, 86, 176, 10, "" }, { 1119, 0, 354, 98, 66, 10, "Units" }, { 1174, 0, 429, 98, 176, 10, "" }, { 1118, 0, 354, 110, 66, 10, "Descriptor" },
    { 1173, 2, 425, 109, 180, 62, "" }, { 1113, 0, 354, 183, 251, 12, "Related Dates" }, { 1120, 0, 354, 198, 66, 10, "Last Modified" },
    { 1175, 0, 429, 198, 176, 10, "" }, { 1121, 0, 354, 210, 66, 10, "Created" }, { 1176, 0, 429, 210, 176, 10, "" },
    { 1093, 0, 354, 34, 251, 12, "Properties" }, { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl BS_111[] = {
    { 1057, 0, 24, 0, 139, 34, "Community" }, { 1065, 1, 24, 34, 56, 44, "&Link\nAccount" }, { 1066, 0, 93, 34, 193, 18, "" }, { 1067, 0, 93, 52, 193, 26, "" },
    { 1068, 1, 24, 88, 56, 44, "&Feedback Program" }, { 1069, 0, 93, 88, 193, 16, "Customer Feedback Program" },
    { 1073, 0, 93, 104, 193, 27, "Manage your Customer Feedback Program settings. This voluntary program guides our quality and product improvement efforts, and future development plans." },
    { 1070, 1, 24, 142, 56, 44, "&Satisfaction Survey" }, { 1071, 0, 93, 142, 193, 16, "Customer Satisfaction Survey" }, { 1072, 0, 93, 158, 193, 27, "" },
    { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl BS_115[] = {
    { 1057, 0, 24, 0, 139, 34, "Help" }, { 1089, 1, 24, 34, 56, 44, "&What's\nNew" }, { 1090, 0, 94, 34, 194, 16, "What's New" }, { 1091, 0, 94, 50, 194, 27, "" },
    { 1083, 1, 24, 88, 56, 44, "&Contents" }, { 1084, 0, 94, 88, 194, 18, "Contents" }, { 1085, 0, 94, 106, 194, 26, "" },
    { 1086, 1, 24, 142, 56, 44, "&Tutorials" }, { 1087, 0, 94, 142, 194, 16, "Tutorials" }, { 1088, 0, 94, 158, 194, 27, "" },
    { 1093, 1, 24, 196, 56, 44, "&ReadMe" }, { 1094, 0, 94, 196, 194, 16, "ReadMe" },
    { 1095, 0, 94, 212, 194, 27, "Learn about last-minute fixes and enhancements. Requires Adobe Acrobat(R) Reader(R). " },
    { 1076, 1, 24, 250, 56, 44, "Check for &Updates" }, { 1079, 0, 94, 250, 194, 16, "Check for Updates" }, { 1078, 0, 94, 266, 194, 27, "" },
    { 1077, 1, 24, 304, 56, 44, "&Network\nLicensing" }, { 1080, 0, 94, 304, 194, 16, "Network Licensing" }, { 1081, 0, 94, 320, 194, 27, "" },
    { 106, 0, 287, 9, 1, 330, "" }, { 1058, 0, 305, 34, 265, 12, "Product Information" }, { 1092, 0, 305, 52, 15, 11, "" }, { 1105, 0, 305, 91, 265, 12, "" },
    { 1099, 1, 305, 106, 56, 44, "&About apprenticeCAM" }, { 1103, 0, 377, 106, 193, 16, "About apprenticeCAM" }, { 1101, 0, 377, 122, 193, 27, "" },
    { 1211, 0, 306, 159, 264, 12, "Licensing" }, { 1212, 0, 306, 174, 94, 10, "License Type" }, { 1214, 0, 408, 174, 162, 10, "" },
    { 1213, 0, 306, 186, 94, 10, "License Number" }, { 1215, 0, 408, 186, 162, 10, "" }, { 1216, 0, 306, 198, 94, 10, "Version Type" },
    { 1217, 0, 408, 198, 162, 10, "" }, { 1218, 0, 306, 210, 94, 10, "Maintenance Expires" }, { 1220, 0, 408, 210, 162, 10, "" }, { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl BS_129[] = { { 1058, 0, 7, 7, 301, 16, "Recent Documents" }, { 1125, 3, 7, 23, 301, 151, "" }, { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl BS_130[] = {
    { 1058, 0, 7, 7, 295, 16, "Computer" }, { 1059, 0, 7, 23, 295, 15, "Recent Folders" }, { 1061, 0, 7, 38, 295, 126, "" }, { 1126, 3, 7, 38, 183, 19, "" },
    { 1127, 1, 7, 173, 56, 52, "&Browse" }, { 0, 0, 0, 0, 0, 0, NULL } };
const BsCtl BS_140[] = {
    { 1057, 0, 20, 0, 180, 34, "Print" }, { 1131, 1, 20, 34, 56, 44, "&Print" }, { 1132, 0, 89, 36, 70, 8, "Copies:" }, { 1133, 2, 165, 34, 38, 14, "" },
    { 1134, 4, 191, 34, 11, 14, "" }, { 1135, 0, 20, 86, 180, 19, "Printer" }, { 1136, 1, 20, 105, 182, 28, "" }, { 1138, 1, 143, 136, 59, 14, "Printer Properties" },
    { 1137, 0, 20, 151, 180, 19, "Settings" }, { 1139, 1, 20, 170, 182, 28, "" }, { 1140, 1, 20, 201, 182, 28, "" }, { 1143, 1, 143, 234, 59, 14, "Page Setup" },
    { 1197, 0, 210, 34, 223, 217, "" }, { 1147, 1, 261, 114, 154, 76, "" }, { 0, 0, 0, 0, 0, 0, NULL } };
int bs_count(const BsCtl *c) { int n = 0; while (c[n].text) n++; return n; }
/* File tab = BCGP backstage view, IDR_BCGP_RIBBON_XML CATEGORY_BACKSTAGE: its 15 entries in order. Pages (forms,
   ribbonui BackstagePage*View / property sheets) show the ribbonuires dialog templates above; commands run and close
   the backstage. icon = INDEX_SMALL in IDR_BCGP_IMAGE_CATEGORY_BACKSTAGE_SMALL (pages have none). Here: New, Save,
   Open (recent documents / Browse), Save As (Browse) and Help > About work; the rest is greyed (NOT DONE).
   The page buttons' images (set in ribbonui's form code) and the Info page's property values are NOT DECODED. */
const BsItem BS_ITEMS[BS_NITEMS] = {
    { "Info", 107, -1, 0 }, { "New", 0, 0, BS_CMD_NEW }, { "Open", -1, -1, 0 }, { "Open in Editor", 0, 1, 0 }, { "Merge", 0, 2, 0 },
    { "Save", 0, 3, BS_CMD_SAVE }, { "Save As", -2, -1, 0 }, { "Save Some", 0, 4, 0 }, { "Zip2Go", 0, 5, 0 }, { "Convert", 105, -1, 0 },
    { "Print", 140, -1, 0 }, { "Help", 115, -1, 0 }, { "Community", 111, -1, 0 }, { "Configuration", 0, 6, BS_CMD_CONFIG }, { "Options", 0, 7, 0 } };
const BsCtl *bs_page_ctls(int page, int *n) {
    const BsCtl *c = page == 107 ? BS_107 : page == 105 ? BS_105 : page == 140 ? BS_140 : page == 115 ? BS_115 : page == 111 ? BS_111 : page == 129 ? BS_129 : page == 130 ? BS_130 : NULL;
    if (n) *n = c ? bs_count(c) : 0;
    return c;
}
/* the live buttons of a page: Help 115 > About (1099; mcamui called run_local("About"), which did nothing - both front ends
   now open the About window as OnAppAbout does, phase 8), Computer 130 > Browse (1127) */
int bs_live(int page, int id) { return (page == 115 && id == 1099) || (page == 130 && id == 1127); }
/* Info values (formats/ui.md "File tab Info page"):
   host callback = mastercam.dll BackstagePageInfoManager slot 6 FUN_1800fa5b0: CFileFind on the geometry file name.
   Found: name = GetFileName, type = FUN_1800fa440 (shell type name, SHGetFileInfo SHGFI_TYPENAME - no Windows
   shell here: blank), folder = GetRoot (with its trailing separator), size = GetLength, units = IsMetric,
   descriptor = get_descr_info (NOT DECODED: blank), modified = GetLastWriteTime, created = GetCreationTime.
   Not found: name = the file name unless it is default_geo_filename ("" then), folder "", size 0, created =
   app +0x8d8 (NOT DECODED: blank). ribbonui then shows 193 / 201 / size 198-200 / dates 197 (formats/ui.md). */
int bs_info_values(BsInfoVal *v, int cap) {
    static char fold[700], sz[32], mod[128], cre[128];
    struct stat st; int saved = loaded && F.path[0] && stat(F.path, &st) == 0;
    const char *base = F.path[0] ? (path_sep_r(F.path) ? path_sep_r(F.path) + 1 : F.path) : "";
    fold[0] = sz[0] = mod[0] = cre[0] = 0;
    if (saved) {
        snprintf(fold, sizeof fold, "%.*s", (int)(base - F.path), F.path);
        long long b = (long long)st.st_size;                   /* ribbonui FUN_18006d480 */
        if (b < 0x400) snprintf(sz, sizeof sz, "%d KB", 1);
        else if (b < 0x100000) snprintf(sz, sizeof sz, "%lld KB", b >> 10);
        else if (b < 0x40000000) snprintf(sz, sizeof sz, "%lld MB", b >> 20);
        else snprintf(sz, sizeof sz, "%lld GB", b >> 30);
        struct tm tmm; if (local_time(st.st_mtime, &tmm)) strftime(mod, sizeof mod, "%a %b %d %H:%M:%S %Y", &tmm);   /* 197 */
        time_t bt = file_btime(F.path); struct tm tb;  /* platform.h */
        if (bt && local_time(bt, &tb)) strftime(cre, sizeof cre, "%a %b %d %H:%M:%S %Y", &tb);
    }
    const char *unsaved = loaded && base[0] ? "Not saved in current version" : "Not saved yet";   /* 201 / 193 */
    BsInfoVal V[] = { { 1171, saved ? base : unsaved }, { 1177, saved ? fold : unsaved },
        { 1170, saved ? sz : unsaved }, { 1174, loaded ? (mcam_is_metric(&F) ? "Metric" : "Inch") : NULL },
        { 1175, saved ? mod : NULL }, { 1176, saved ? cre : NULL },
        { 1173, saved ? NULL : "Not saved yet" } };            /* the descriptor's cue banner: 193 when not saved */
    int n = 0;
    for (size_t k = 0; k < sizeof V / sizeof V[0] && n < cap; k++) if (V[k].t && V[k].t[0]) v[n++] = V[k];
    return n;
}

/* tests: the state of the dialogs of this file (MCAMUI_MISCDUMP=1, both apps) */
void misc_dump(void) {
    if (show_gridset) printf("    grid %.5f %.5f %.5f %.5f %.5f snap %d mode %d '%s' '%s' '%s' '%s' '%s'\n", grid.dx, grid.dy, grid.ox, grid.oy, grid.size, grid.snap, grid.snap_mode,
                             gs_txt[0], gs_txt[1], gs_txt[2], gs_txt[3], gs_txt[4]);
    if (ask.act) printf("    (rename %s: '%s')\n", cm_group == 2 ? "machine group" : "toolpath group", ask.v[0]);
    if (nclip || paste_pending) printf("    clip %d%s\n", nclip, paste_pending ? " (paste prompt)" : "");
    if (zoom_window || zw_active) printf("    (zoom window %d)\n", zw_active);
    if (show_help || show_about || show_info) printf("    help %d about %d info %d\n", show_help, show_about, show_info);
    printf("    groups '%s' / '%s' grid %.5f %.5f %.5f %.5f %.5f mode %d\n", mach_grp_name, tp_grp_name, grid.dx, grid.dy, grid.ox, grid.oy, grid.size, grid.snap_mode);
    printf("    view %.4f %.4f %.4f\n", vcx, vcy, vht);           /* depends on the view's size: not compared between the front ends */
}
/* script steps (as mcamui run_script): ask:V1|V2 (fill the open input / rename edit and press OK / Enter); ours: gridset:K=V
   (a grid edit typed), gridsnap:1|2, gridclose, askcancel, rename (Groups > Rename on cm_group), zoomwin:X0,Y0,X1,Y1 (view
   pixels), paste (Enter on the Paste prompt) */
int cui_misc_script(const char *st, const char *arg, int vw, int vh) {
#define IS(x) (!strcmp(st, x))
    if (IS("ask") && arg) {                 /* ask:V1|V2 - fill the open input box and press OK */
        if (!ask.act) { printf("    (no input box)\n"); return 1; }
        char tmp[600]; snprintf(tmp, sizeof tmp, "%s", arg); char *bar = strchr(tmp, '|'); if (bar) { *bar = 0; snprintf(ask.v[1], sizeof ask.v[1], "%s", bar + 1); }
        snprintf(ask.v[0], sizeof ask.v[0], "%s", tmp); int a = ask.act; ask_ok(); if (ask.act == a) ask.act = 0;
        const char *mt = NULL, *mx = NULL; int shown = cui_hooks.msg_open && cui_hooks.msg_open(&mt, &mx);
        printf("    groups: '%s' / '%s'%s%s\n", mach_grp_name, tp_grp_name, shown ? " | message: " : "", shown ? mx : "");
        if (shown && cui_hooks.msg_close) cui_hooks.msg_close();
        return 1;
    }
    if (IS("askcancel")) { ask_end(0); return 1; }
    if (IS("rename")) { grp_rename_begin(); return 1; }
    if (IS("gridset") && arg && show_gridset) { int k = atoi(arg); const char *eq = strchr(arg, '='); if (eq && k >= 0 && k < 5) { if (!gs_inited) gs_fill(); snprintf(gs_txt[k], sizeof gs_txt[k], "%s", eq + 1); gs_edit(k); } return 1; }
    if (IS("gridsnap") && arg && show_gridset) { grid.snap_mode = atoi(arg); return 1; }
    if (IS("gridclose")) { gs_close(); return 1; }
    if (IS("zoomwin") && arg && zoom_window) { float a[4] = { 0 }; sscanf(arg, "%f,%f,%f,%f", &a[0], &a[1], &a[2], &a[3]); zoom_window_done(a[0], a[1], a[2], a[3], vw, vh); return 1; }
    if (IS("paste") && paste_pending) { paste_clip(); return 1; }
    if (IS("units") && arg) { cui_units_change(atoi(arg)); units_dump(); return 1; }   /* ours: the Configuration's Current: units (askyes answers the scale question) */
    if (IS("unitsdump")) { units_dump(); return 1; }
    return 0;
#undef IS
}

/* the Quick Access Toolbar of the user's CONFIG/Mastercam.Workspace: QuickAccessToolbarCommands (u16 count, then u32 command
   ids, 0 = separator) resolved by QATFunctionTable ("id:App.Function,..."); "" = a separator (mcamui.c draw_qat's parse; the
   parity check lists them) */
int cui_qat_load(char fn[16][64]) {
    int nq = 0;
    char wsp[1100]; FILE *f = fopen(site_file(wsp, sizeof wsp, "user", "CONFIG/Mastercam.Workspace"), "r");
    static char line[8192]; char table[4096] = "", cmds[512] = "";
    while (f && fgets(line, sizeof line, f)) {
        char *p;
        if ((p = strstr(line, "<QATFunctionTable>\"")) != NULL) { p += 19; char *e = strchr(p, '"'); if (e) { *e = 0; snprintf(table, sizeof table, "%s", p); } }
        if ((p = strstr(line, "<QuickAccessToolbarCommands>\"")) != NULL) { p += 29; char *e = strchr(p, '"'); if (e) { *e = 0; snprintf(cmds, sizeof cmds, "%s", p); } }
    }
    if (f) fclose(f);
    size_t L = strlen(cmds);
    for (size_t o = 4; o + 8 <= L && nq < 16; o += 8) {      /* little-endian u32 per 8 hex digits after the u16 count */
        unsigned b[4]; if (sscanf(cmds + o, "%2x%2x%2x%2x", &b[0], &b[1], &b[2], &b[3]) != 4) break;
        unsigned id = b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24;
        if (!id) { fn[nq++][0] = 0; continue; }
        char key[16]; snprintf(key, sizeof key, "%u:", id);
        const char *q = table; const char *hit = NULL;
        while ((q = strstr(q, key)) != NULL) { if (q == table || q[-1] == ',') { hit = q + strlen(key); break; } q++; }
        if (!hit) continue;
        const char *dot = strchr(hit, '.'), *end = strchr(hit, ','); if (!end) end = hit + strlen(hit);
        if (!dot || dot > end) continue;
        if (strncmp(hit, "Mastercam.", 10) == 0) snprintf(fn[nq], 64, "%.*s", (int)(end - dot - 1), dot + 1);
        else snprintf(fn[nq], 64, "%.*s", (int)(end - hit), hit);    /* add-in: App.Function */
        nq++;
    }
    return nq;
}

/* ------------------------------------------------------------------------------------------------ units (formats/units.md) */
/* The System Configuration dialog (mccoreres 282, CConfigDlg) "Current:" combo (2056) lists the configuration files with their
   units, "mcamx.config (English)" / "mcamxm.config (Metric)" (mccoreres 4962 / 4963). Selecting one (FUN_180175420 ->
   FUN_1801756a0) reads it (ReadCfgFile), makes it the current configuration (SetCurrentCfgFilename) and sets the system units
   from its item 1 "File is metric?" (SetUnits); OK (FUN_180170520) then, when the units differ from the ones the dialog opened
   with: an empty part -> init_db; else fmu_scale_yn asks "Scale current part to Metric?" / "...English?" (5304 / 5303): Yes ->
   fmu_scale_db (every geometry entity x 25.4 / 1 / 25.4 about the origin; here points, lines, arcs, splines and surfaces of kinds
   0 / 1 / 10, cui_xform.c xf_scale_all), No -> the numbers stay (sld_scale_sldBodies keeps the
   solids' size in the new units: NOT PORTED, the solids here are not rescaled either way); then RefreshStatusBar.
   SetUnits itself converts every RegisterVariable'd session value (FUN_1805f9da0) and stamps the units every record is saved with
   (FUN_1808c4ce0: version entity +0x6f, record +0x4d). decomp/units/core_units.c, core_cfgcallers.c, core_scale.c. */
const char *cui_units_config(int metric) { return metric ? "mcamxm.config (Metric)" : "mcamx.config (English)"; }
int cui_units_set(int metric, int scale) {
    metric = metric ? 1 : 0;
    int was = mcam_is_metric(&F);
    if (was == metric) return 0;                                /* ChangeConfiguration: same units - nothing */
    int n = 0, skipped = 0, nent = 0;
    for (int i = 0; loaded && i < F.nents; i++) nent += F.ents[i].type != 0;
    mc_units_metric = metric;                                   /* SetUnits (the combo's selection, before the dialog's OK scales) */
    mc_systol_units(metric);                                    /* ReadCfgFile's [TOLERANCES] (FUN_1801bab70 + FUN_1801b6bd0) and 7004 of
                                                                   the new current configuration over the values held (formats/units.md 8) */
    if (loaded) F.metric = metric;                              /* the part is saved in the system units (FUN_1808c4ce0) */
    if (loaded && scale && (nent || F.nsurfs || F.nsolids)) n = xf_scale_all(metric ? 25.4 : 1 / 25.4, &skipped);   /* fmu_scale_db (OK, FUN_180170520:
                                                                   after SetUnits - the scaled entities are built in the new units: a spline's
                                                                   polyline at their curve_chord_dev, mcam_new_curve) */
    if (loaded && (nent || F.nops || F.nsurfs || F.nsolids)) doc_dirty = 1;   /* an empty part: init_db - nothing to save */
    ex_units(); fl_units();                                               /* RegisterVariable'd session values (FUN_1805f9da0) */
    if (cui_hooks.config_reload) cui_hooks.config_reload();     /* ReadCfgFile of the new current configuration */
    snprintf(cam_msg, sizeof cam_msg, "%s", metric ? "System units switched from English to Metric." : "System units switched from Metric to English.");   /* 5479 / 5478 */
    if (scale && loaded) {
        size_t l = strlen(cam_msg);
        snprintf(cam_msg + l, sizeof cam_msg - l, " %d entit%s scaled by %s.", n, n == 1 ? "y" : "ies", metric ? "25.4" : "1/25.4");
        l = strlen(cam_msg);
        if (skipped) snprintf(cam_msg + l, sizeof cam_msg - l, " %d solid(s) / unsupported entities not scaled (NOT PORTED).", skipped);   /* cui_xform.c xf_scale_all */
    }
    overlay_dirty = 1;
    return 1;
}
/* the Configuration dialog's OK with another current configuration (FUN_180170520): a part with entities asks fmu_scale_yn
   (dbmmess_setup(5303 + to-metric) + dbmmess_mbyn; its caption is NOT DECODED: none here), else the units only */
int cui_units_change(int metric) {
    metric = metric ? 1 : 0;
    if (mcam_is_metric(&F) == metric) return 0;
    int nent = 0; for (int i = 0; loaded && i < F.nents; i++) nent += F.ents[i].type != 0;
    int scale = 0;
    if (loaded && (nent || F.nsurfs || F.nsolids))
        scale = cui_hooks.ask_yes_no ? cui_hooks.ask_yes_no("", metric ? "Scale current part to Metric?" : "Scale current part to English?") : 1;
    return cui_units_set(metric, scale);
}
/* tests: the units state and what depends on it (unitsdump step / MCAMUI_UNITSDUMP) */
void units_dump(void) {
    int m = mcam_is_metric(&F); char v[6][64];
    static const int keys[6] = { 1000, 1001, 1008, 7004, 19024, 23003 };
    for (int i = 0; i < 6; i++) mcam_config_value(m, keys[i], v[i], sizeof v[i]);
    printf("    units sys=%d part=%d is_metric=%d tp_metric=%d config='%s' info='%s'\n", mc_units_metric, loaded ? F.metric : -9, m, tp_metric(), cui_units_config(m), m ? "Metric" : "Inch");
    printf("    config 1000=%s 1001=%s 1008=%s 7004=%s 19024=%s 23003=%s\n", v[0], v[1], v[2], v[3], v[4], v[5]);
    const McSysTol *t = &mc_systol;                             /* the system tolerances held (formats/units.md 8) */
    printf("    systol held=%d tol=%g chain=%g plane=%g minarc=%g cmin=%g cmax=%g chord=%g surf=%g nc=%g gl=%g\n", mc_systol_held(),
           t->tol, t->chain_tol, t->chain_plane_tol, t->min_arc_length, t->curve_min_step, t->curve_max_step, t->curve_chord_dev,
           t->surf_step_chord_dev, t->nc_tol, t->gl_tolerance);
    printf("    grid %.5f %.5f size %.5f extrude dist %.5f thick %.5f\n", grid.dx, grid.dy, grid.size, ex_sess.dist, ex_sess.thick);
    const Op *d = default_op(CAM_CONTOUR, m);
    CamParams p; memset(&p, 0, sizeof p); cam_defaults(&p, CAM_CONTOUR, &F);
    printf("    defaults op contour %s: clearance %.4f retract %.4f feed plane %.4f depth %.4f feed %.4f stepdown %.4f\n", d ? "found" : "MISSING",
           p.clearance, p.retract_z, p.feed_plane, p.final_z, p.feed, p.stepdown);
}
