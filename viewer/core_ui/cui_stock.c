/* cui_stock.c - the machine group's Stock Setup (Machine Group Properties > Stock Setup), shared by both front ends.
 *
 * Decoded 2026-10-22 (formats/mcam.md "Stock Setup", formats/mcamui_dialogs.md 6): the property page is mastercam.dll
 * CGroupPg3 (opmanres dialog 1590 "Group PG3 Dlg"; decomp/mastercam_stocksetup.c): vtable @1805ef4c0, message map
 * FUN_1801ee0f0, DoDataExchange FUN_1801ed460, OnInitDialog FUN_1801ef9d0, group -> page FUN_1801f2000, page -> group
 * (OnOK / apply) FUN_1801ed0b0, enable rules FUN_1801ed700, picture FUN_1801f2980 / WM_DRAWITEM FUN_1801ef4e0, the
 * corner hit test FUN_1801f0070 (+ FUN_1801ee060: corner k -> button 1294 + k), Select origin FUN_1801f16f0, Select
 * corners FUN_1801f1840, All Surfaces / Solids / Entities / Unselect All FUN_1801f1ca0 / 1c30 / 1bc0 / 1d10 -> FUN_1801f26d0
 * -> mccad create_bbox_stock_no_dlg (decomp/mccad_bbox_stock.c), box -> size + origin FUN_1801edd80. The sheet:
 * EditGroupPropertiesDlg (decomp/mastercam_grpprops.c), opened by COpMgrTree::Selchanged on the Properties children
 * (-0x10 .. -0x13 -> page 0 .. 3, decomp/opman_tree_all.c:41730). The model: struct group_pg3 = op_group +0x1228, read /
 * written by WriteFileGroups as part of the 0x1ae0 group record; mccore stock_ok / stock_calc_geom / FUN_1800d5a20
 * (stock_calc_center) / job_setup_stock_corners / calc_top_of_stock (decomp/mccore_stock.c, mccore_tpsd_helpers.c);
 * the display mastercam.dll draw_stock (12 lines, line style 4, mill_stock_color = config 5029, mccore FUN_1801d3400).
 *
 * Cylindrical stock (2026-10-22): stock_calc_geom's shape 1 (axis view through Plane::RotateRelative, the two end circles,
 * 12 lines), its display, stock_ok, Facing's circle boundary (make_facing_pock_bound) and the bounding cylinder of All
 * Surfaces / Solids / Entities (create_bcyl_stock_no_dlg); NCI extents (25077, nc_post.c nc_extents).
 * Bounding box (25451, 2026-10-22): the Bounding Box function panel in stock mode (cui_bbox.c, create_boundingshape_either_method).
 * The rest (2026-10-22, "Stock Setup rest", formats/mcam.md "Stock Setup"): the Solid shape (522 / 528: select_ent mask 0x10000,
 * FUN_1801f0fc0; stock_ok solid id > 0), the File shape (523 / 1253 / 364: FUN_1801ef850 PromptForFileToOpen + VerifyFileSuffix
 * + file_exists, FUN_1801f2000 / FUN_1801ed0b0 checks, stock_ok's missing-file message), the stock model CreateShadedStockModel
 * (mccore 0x180158770: box / cylinder / solid / STL with the decoded STL reader), its display (draw_stock: triangle outlines for
 * shapes 2 / 3, FUN_18033fe70 shaded with the Solid display), Fit screen (GetFitExtents -> FUN_180337650), the stock plane
 * (960: FUN_1801f1d70 SelectPlaneFromList -> Plane::Convert into the group at once; every conversion through the stock view),
 * the cylinder's axis view R . plane (RotatePlane + SYSTYPES operator*), the 2D High Speed regen marking after the sheet
 * (FUN_1801e1a20 + Mark2dHSTOpsRefMachStockDef4Regen).
 * NOT DECODED / NOT PORTED here (greyed or left out): Use Machine Tree (734 + tree 720: the machine definition's
 * CMachineTreeCtrl / CCNCMachine::UpdateStockFromMGData), planes other than the seven system planes, a cylinder whose axis view is
 * no standard view (Matrix33::GetViewNo's new view), the STL default folder (stl_prefix, set at run time) and the combo's list
 * (CFileComboBox), Parasolid's facetting of a solid stock (sld_to_stl: this program's mesh instead), select_ent's pick (a ray /
 * triangle test here) and solids made here (no SOLID_ID id), the shaded model's lights (gl_Init_Lights_E), the meaning of
 * the regen rule's op byte +0x17c8 (MP 41144), the Bounding box panel with a non-Top stock plane (greyed, ours), the rubber
 * band of Select corners, the Select origin prompt (select_point's "*" text), the sheet's other pages / Apply / Help, dialog
 * 977 (posting dirty ops: the stored NCI is posted, see stock_regen_mark).
 * Review fixes (2026-10-22, "Stock Setup review fixes" in formats/qt_port_plan.md): the model is counted, allocated once and
 * checked (as CreateShadedStockModel); the STL file read twice with a status (open failure / incomplete last record /
 * non-finite triangles, which are left out); messages when no model can be made or drawn are ours (Mastercam shows none);
 * the Fit points and the overlay read the model in place; scene.c owns the shaded / dash buffers and gives GL large arrays in
 * chunks after a headroom probe (ours: a GL driver crashed instead of failing). */
#include "../stl_mesh.h"
#include "core_ui.h"
#include "../nc_post.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

/* ---- opmanres dialog 1590 (exports/opmanres_dialogs.tsv), in template order; role from the handlers / DDX above */
const StCtl ST_1590[] = {
    { 725,   STR_GROUP,  14, 61, 294, 84, "Shape" },
    { 518,   STR_RADIO,  26, 79, 64, 10, "Rectangular" },
    { 520,   STR_RADIO,  26, 99, 62, 10, "Cylindrical" },
    { 522,   STR_RADIO,  96, 79, 34, 10, "Solid" },
    { 523,   STR_RADIO,  96, 99, 32, 10, "File" },
    { 728,   STR_STATIC, 36, 113, 36, 8, "Axis" },
    { 25070, STR_RADIO,  36, 125, 24, 10, "X" },
    { 25071, STR_RADIO,  66, 125, 21, 10, "Y" },
    { 25072, STR_RADIO,  94, 125, 21, 10, "Z" },
    { 526,   STR_CHECK,  122, 125, 82, 10, "Center on axis" },
    { 528,   STR_BMPBTN, 134, 75, 18, 16, "open" },          /* UIBitmapButton +0x43f8, bitmap 0x33f (831) */
    { 364,   STR_COMBO,  134, 97, 140, 266, "" },             /* CFileComboBox +0x53b8 (STL) */
    { 1253,  STR_BMPBTN, 280, 95, 16, 14, "" },              /* +0x3438, bitmap 0x3a1 (929) */
    { 1221,  STR_EDIT,   110, 171, 40, 12, "" },              /* size Y (+0x8fb8 -> group +0x1238) */
    { 1219,  STR_EDIT,   213, 167, 40, 12, "" },              /* size X (+0x81c0 -> +0x1230) */
    { 1223,  STR_EDIT,   295, 216, 40, 12, "" },              /* size Z (+0x9db0 -> +0x1240) */
    { 1227,  STR_PICTURE, 84, 153, 224, 116, "" },            /* UIBitmap +0x10340: opmanres bitmap 832 + FUN_1801f2980 index */
    { 25143, STR_GROUPCHK, 14, 155, 60, 10, "Display" },      /* +0x56f8 (check, group +0x1260) */
    { 1252,  STR_CHECK,  24, 171, 47, 10, "Fit screen" },     /* +0x5620 (group +0x1228) */
    { 530,   STR_RADIO,  24, 187, 50, 10, "Wire frame" },     /* radio +0xff7c (group +0x1261) */
    { 532,   STR_RADIO,  24, 200, 50, 10, "Solid" },
    { 1238,  STR_GROUP,  14, 223, 69, 102, "Stock Origin" },
    { 1232,  STR_STATIC, 18, 257, 11, 8, "X" },
    { 1215,  STR_EDIT,   33, 253, 40, 14, "" },               /* origin X (+0x57d8 -> group +0x1248) */
    { 1233,  STR_STATIC, 18, 269, 11, 8, "Y" },
    { 1230,  STR_EDIT,   33, 266, 40, 14, "" },
    { 1234,  STR_STATIC, 18, 282, 11, 8, "Z" },
    { 1231,  STR_EDIT,   33, 279, 40, 14, "" },
    { 1241,  STR_BMPBTN, 42, 300, 22, 17, "&Select origin..." },   /* +0x14b8, bitmap 0x33f (831) */
    { 1236,  STR_STATIC, 119, 161, 15, 8, "Y" },
    { 1298,  STR_CORNER, 129, 209, 9, 9, "" },                /* corner 4 (FUN_1801ee060: k -> 1294 + k) */
    { 1302,  STR_CORNER, 129, 226, 9, 9, "" },                /* 8 */
    { 1295,  STR_CORNER, 190, 177, 9, 9, "" },                /* 1 */
    { 1299,  STR_CORNER, 190, 196, 9, 9, "" },                /* 5 */
    { 1294,  STR_CORNER, 188, 213, 13, 13, "" },              /* 0: centre of the top face */
    { 1297,  STR_CORNER, 190, 239, 9, 9, "" },                /* 3 */
    { 1301,  STR_CORNER, 190, 259, 9, 9, "" },                /* 7 */
    { 1235,  STR_STATIC, 225, 159, 15, 8, "X" },
    { 1296,  STR_CORNER, 249, 209, 9, 9, "" },                /* 2 */
    { 1300,  STR_CORNER, 249, 226, 9, 9, "" },                /* 6 */
    { 1237,  STR_STATIC, 309, 208, 12, 8, "Z" },
    { 25452, STR_BUTTON, 153, 316, 57, 14, "S&elect corners..." },
    { 25451, STR_BUTTON, 215, 316, 57, 14, "&Bounding box" },
    { 25077, STR_BUTTON, 278, 316, 57, 14, "&NCI extents" },
    { 1243,  STR_BUTTON, 91, 334, 57, 14, "All Surfaces" },
    { 1244,  STR_BUTTON, 153, 334, 57, 14, "All Solids" },
    { 1245,  STR_BUTTON, 215, 334, 57, 14, "All Entities" },
    { 1246,  STR_BUTTON, 278, 334, 57, 14, "Unselect All" },
    { 294,   STR_HIDDEN, 6, 333, 40, 12, "" },                /* hidden on the mill page (FUN_1801ed700) */
    { 298,   STR_HIDDEN, 46, 336, 40, 12, "" },
    { 960,   STR_BMPBTN, 61, 29, 22, 18, "Select..." },       /* +0x4f8, bitmap 0x3aa (938) */
    { 307,   STR_EDIT,   87, 31, 178, 12, "" },               /* the stock plane's name (CString +0xff88; 4016 "Undefined") */
    { 4294901759u, STR_GROUP, 53, 15, 220, 40, "Stock Plane" },
    { 731,   STR_STATIC, 20, 233, 56, 16, "In view coordinates" },
    { 720,   STR_HIDDEN, 54, 58, 187, 201, "" },              /* machine tree: hidden unless Use Machine Tree */
    { 734,   STR_CHECK,  9, 353, 95, 10, "Use Machine Tree" },
    { 0, 0, 0, 0, 0, 0, NULL }
};
/* the sheet: EditGroupPropertiesDlg, title opmanres 3851; a mill group's pages (CGroupPg0 ctor FUN_1801df8d0, AddPage order;
   decomp/mastercam_grpsheet.c, formats/grpprops_tabs.md 0): 3852 Files (0x32f = 815), 3853 Tool Settings (0x635 = 1589),
   3855 Stock Setup (0x636 = 1590). No 4th page: Safety Zone (3856, 0x637) is added only for an Agie wire machine and
   RunningSafetyZone() @180442a40 returns false (decomp/mastercam_safetyzone_editgroup.c). The group's +0x19a0..+0x19e8 stay
   as stored (the save patches single fields of the group record). */
const char *const ST_SHEET_TITLE = "Machine Group Properties";
const char *const ST_TABS[3] = { "Files", "Tool Settings", "Stock Setup" };

int show_stock_setup, st_page = 2, st_pick;            /* the sheet is open; its page; a graphics pick (ST_PICK_*) */
int st_q2d;                                            /* "Override 2D construction mode?" waiting (the bbox mask) */
int st_browse;                                         /* the STL file dialog (1253) is wanted (the front end opens it, st_browse_done) */
int cfg_stock_color = 12;                              /* mill_stock_color: mastercam.dll .data initial value 12 */
int cfg_constr_3d = 1;                                 /* constr_view_is_3d: mccore .data initial value 1, config 16009 */
StPage stp;                                            /* the page's copy (CGroupPg3 members) */

/* ---- the model (group_pg3 in McamFile) */
/* job_setup_stock_corners (mccore 0x1805e4dc0): box min = origin + OFF(corner), max = min + size (Top stock plane) */
static void corner_off(int corner, const double s[3], double off[3]) {
    double dx = 0, dy = 0, dz = 0;
    switch (corner) {
    case 0: dx = -s[0] / 2; dy = -s[1] / 2; dz = -s[2]; break;
    case 1: dy = -s[1]; dz = -s[2]; break;
    case 2: dx = -s[0]; dy = -s[1]; dz = -s[2]; break;
    case 3: dx = -s[0]; dz = -s[2]; break;
    case 4: dz = -s[2]; break;
    case 5: dy = -s[1]; break;
    case 6: dx = -s[0]; dy = -s[1]; break;
    case 7: dx = -s[0]; break;
    default: break;
    }
    off[0] = dx; off[1] = dy; off[2] = dz;
}
/* ---- the stock plane (group_view at op_group +0x8a8: i16 plane id, +2 i16 view number, +8 matrix_33, +0x50 origin).
   world_to_view(p, vno) = the view's matrix rows . p, view_to_world = the transpose; view numbers < 2 are the identity
   (mastercam.dll world_to_view 0x180341b30 / view_to_world 0x180341520). The page uses the group's view directly (OnInitDialog
   FUN_1801ef9d0: page +0x105f0 = &group +0x8a8); every conversion is world_to_view(world - plane origin) / view_to_world +
   origin (FUN_1801f2000, FUN_1801ed0b0, FUN_1801edd80, FUN_1801f16f0; stock_calc_geom's P3D::ViewToWorldAdd). Ported: the
   seven system planes (ids 1..7 = view numbers 1..7, origin 0: cui_planes.c); other planes are NOT DECODED (the part's
   planes archive) - stock_supported() is 0 for them and nothing is drawn / computed from such a stock. */
int stock_supported(void) {
    if (!F.stock_rec) return 1;
    double S[9]; int v = F.stock_vno;
    if (v < 1 || v > 7 || !view_std_matrix(v, S)) return 0;
    for (int k = 0; k < 9; k++) if (fabs(F.stock_vm[k] - S[k]) > 1e-9) return 0;
    return fabs(F.stock_vo[0]) < 1e-9 && fabs(F.stock_vo[1]) < 1e-9 && fabs(F.stock_vo[2]) < 1e-9;
}
static void sp_matrix(double M[9]) {                       /* the stock view's rows (identity for view numbers < 2 / no record) */
    if (F.stock_rec && F.stock_vno >= 2 && view_std_matrix(F.stock_vno, M)) return;
    for (int k = 0; k < 9; k++) M[k] = k % 4 == 0;
}
static void m_w2v(const double M[9], const double w[3], double v[3]) { for (int r = 0; r < 3; r++) v[r] = M[3 * r] * w[0] + M[3 * r + 1] * w[1] + M[3 * r + 2] * w[2]; }
static void m_v2w(const double M[9], const double v[3], double w[3]) { for (int c = 0; c < 3; c++) w[c] = M[c] * v[0] + M[3 + c] * v[1] + M[6 + c] * v[2]; }
/* world -> stock view: world_to_view(p - plane origin) */
void stock_w2v(const double w[3], double v[3]) { double M[9], d[3]; sp_matrix(M); for (int k = 0; k < 3; k++) d[k] = w[k] - (F.stock_rec ? F.stock_vo[k] : 0); m_w2v(M, d, v); }
/* stock view -> world: P3D::ViewToWorldAdd(p, view, plane origin) */
void stock_v2w(const double v[3], double w[3]) { double M[9]; sp_matrix(M); m_v2w(M, v, w); for (int k = 0; k < 3; k++) w[k] += F.stock_rec ? F.stock_vo[k] : 0; }
/* the stock plane's name (MastercamPlanes::GetPlanePtrByID(+0x8a8) + 0x110, else opmanres 4016 "Undefined") */
static void plane_text(char *b, size_t n) {
    int vid = F.stock_rec ? F.stock_vid : 1; const char *pn = vid >= 1 && vid <= 7 ? cui_sys_plane(vid - 1) : "";
    snprintf(b, n, "%s", pn && pn[0] ? pn : "Undefined");
}
/* stock_ok (mccore 0x1800d6740): not for lathe / mill-turn groups (+0x19e8 = 1 / 6; 0 in every part here); rectangular X, Y and Z >= tol, cylindrical X and Y >= tol
   (system tol 5e-5, FUN_1803bc7d0), solid: the solid's id > 0, file: file_exists(+0x1271) - a missing file shows message_box_ok
   (mccoreres 7594 "File does not exist." + "\n\n" + the file, caption 7581 "Stock setup") and resets Display and the shape to
   0 (in the group). */
int stock_ok(void) {
    const double tol = 5e-05;
    if (F.stock_rec && (F.stock_gtype == 1 || F.stock_gtype == 6)) return 0;   /* group +0x19e8: lathe 1 / mill-turn 6 */
    switch (F.stock_shape) {
    case 0: return F.stock[0] >= tol && F.stock[1] >= tol && F.stock[2] >= tol;
    case 1: return F.stock[0] >= tol && F.stock[1] >= tol;
    case 2: return F.stock_solid > 0;
    case 3:
        if (file_exists(F.stock_file)) return 1;
        cui_message("Stock setup", "File does not exist.\n\n%s", F.stock_file);
        F.stock_disp = 0; F.stock_shape = 0; F.stock_dirty = 1; overlay_dirty = 1;
        return 0;
    }
    return 0;
}
/* stock_calc_geom (mccore 0x1800d5d80), shape 1: the axis (+0x126c) picks a RelativeType from the int table {X 5, Y 2, Z 1};
   Plane::RotateRelative (mastercam.dll 0x1803dd440 -> RotatePlane 0x1803dd050) builds the rotation R from the type alone and
   sets the plane's matrix to Plane::MultiplyMatrix33(current, R) = SYSTYPES operator*(current, R) = R . current
   (decomp/systypes_matmul.c: result[r][c] = sum_k second[r][k] first[k][c]; decomp/mastercam_multmat33*.{c,txt}); R for a
   type = the rotated Top plane: 1 Top, 2 Front, 3 Back, 5 Right. The new view number = Matrix33::GetViewNo (FindViewNo);
   a matrix that is no standard view gets a new view there - NOT DECODED: 0 is returned and such a cylinder is not
   supported (not drawn / stored). M = the axis view's rows. */
static int rot_view(int type, const double S[9], double M[9]) {
    double R[9]; view_std_matrix(type, R);
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) M[3 * r + c] = R[3 * r] * S[c] + R[3 * r + 1] * S[3 + c] + R[3 * r + 2] * S[6 + c];
    return view_number(M) <= 7 ? view_number(M) : 0;
}
int stock_axis_view(int axis, double M[9]) {
    double S[9]; sp_matrix(S);
    return rot_view(axis == 0 ? 5 : axis == 1 ? 2 : 1, S, M);
}
/* the two end circles (a_3d at +0x15c0 / +0x1628): centre = view_to_view(origin, stock view, axis view), radius = size
   X / 2, start 0, sweep 2 pi; the second centre's depth = the first's - size Y for type 2 (Front), + size Y otherwise
   (so the cylinder runs from the origin along the axis for a length of size Y) */
static int cyl_geom(double cv[2][3], double *r, double M[9]) {
    int v = stock_axis_view(F.stock_axis, M);
    double w[3]; stock_v2w(F.stock_org, w);
    m_w2v(M, w, cv[0]);
    cv[1][0] = cv[0][0]; cv[1][1] = cv[0][1];
    cv[1][2] = cv[0][2] + (F.stock_axis == 1 ? -F.stock[1] : F.stock[1]);
    *r = F.stock[0] * 0.5;
    return v;
}
/* a point of an end circle at angle t (view coordinates) -> world (view_to_world = M^T v) */
static void cyl_pt(const double M[9], const double c[3], double r, double t, double w[3]) {
    double v[3] = { c[0] + r * cos(t), c[1] + r * sin(t), c[2] };
    m_v2w(M, v, w);
}
/* the box in stock-view coordinates: min = origin + OFF(corner), max = min + size (job_setup_stock_corners 0x1805e4dc0) */
void stock_box(double lo[3], double hi[3]) {
    double off[3]; corner_off(F.stock_sel, F.stock, off);
    for (int k = 0; k < 3; k++) { lo[k] = F.stock_org[k] + off[k]; hi[k] = lo[k] + F.stock[k]; }
}
/* stock_calc_geom (mccore 0x1800d5d80), shape 0: the centre (FUN_1800d5a20 = min + size / 2 for the corner, the inverse
   of job_setup_stock_corners), then the 12 lines in its order (stock-view coordinates): top x0 edge, top y1 edge, top x1
   edge, top y0 edge, the same four at the bottom, the four verticals (top -> bottom) at (x0,y0) (x0,y1) (x1,y1) (x1,y0);
   every end point through P3D::ViewToWorldAdd(stock view, plane origin) */
void stock_calc_lines(double L[12][6]) {
    if (F.stock_shape == 1) {   /* the 12 lines join creep_along_ent(circle, i * length / 12) on both circles: angles i * 30 deg */
        double cv[2][3], r, M[9]; cyl_geom(cv, &r, M);
        for (int i = 0; i < 12; i++) { cyl_pt(M, cv[0], r, i * M_PI / 6, L[i]); cyl_pt(M, cv[1], r, i * M_PI / 6, L[i] + 3); }
        return;
    }
    double lo[3], hi[3]; stock_box(lo, hi);
    double x0 = lo[0], x1 = hi[0], y0 = lo[1], y1 = hi[1], z0 = lo[2], z1 = hi[2];
    double P[12][6] = { { x0, y0, z1, x0, y1, z1 }, { x0, y1, z1, x1, y1, z1 }, { x1, y0, z1, x1, y1, z1 }, { x0, y0, z1, x1, y0, z1 },
                        { x0, y0, z0, x0, y1, z0 }, { x0, y1, z0, x1, y1, z0 }, { x1, y0, z0, x1, y1, z0 }, { x0, y0, z0, x1, y0, z0 },
                        { x0, y0, z1, x0, y0, z0 }, { x0, y1, z1, x0, y1, z0 }, { x1, y1, z1, x1, y1, z0 }, { x1, y0, z1, x1, y0, z0 } };
    for (int l = 0; l < 12; l++) { stock_v2w(P[l], L[l]); stock_v2w(P[l] + 3, L[l] + 3); }
}
/* a cylinder whose axis view is a standard view (stock_axis_view) - else NOT DECODED (see above) */
static int cyl_ok(void) { double M[9]; return F.stock_shape != 1 || stock_axis_view(F.stock_axis, M) > 0; }

/* ---- the stock model (CreateShadedStockModel mccore 0x180158770, called at the end of stock_calc_geom; decomp/mccore_stock.c
   l.1581): stock_model +0x80 triangles, +0x84 vertices, +0x88 float vertices (world), +0x98 int indices. Shape 0: the 8 box
   corners from the lines (+0x1380 x0y0z1, +0x1398 x0y1z1, +0x13e0 x1y0z1, +0x13f8 x1y1z1, +0x1440 x0y0z0, +0x1458 x0y1z0,
   +0x14a0 x1y0z0, +0x14b8 x1y1z0) and 12 triangles (the index table below). Shape 1: n = (short)(2 pi / (2 acos(1 -
   gl_tolerance / r)) + 1), at least 32; n points on each end circle (eval_curve at i / n), the two centres; 2n side, n + n
   cap triangles. gl_tolerance = 0.001 inch / 0.025 mm (mastercam.dll FUN_1801b4070; its config binding NOT DECODED).
   Shape 2: the solid (fill_in_ent_idn(+0x1268) = a SOLID_ID entity) through sld_to_stl(gl_tolerance) into a temporary STL
   that is read back - ours: the solid's triangles as this program meshes it (xt_mesh.c; Parasolid's facetting NOT DECODED).
   Shape 3: the STL file (+0x1271). Both read through get_first_stl_triangle / get_next_stl_triangle and each vertex through
   P3D::ViewToWorldAdd(stock view, plane origin). */
static float *sm_v; static int *sm_t, sm_nv, sm_nt;
typedef struct { int shape, sel, axis, solid, vno, solid_tris; double s[3], o[3]; char file[0x104]; long long fsz, fmt; } SmKey;
static SmKey sm_key;
static int sm_built;
static int sm_cv, sm_ct;                                   /* the capacities allocated (one calloc each, as CreateShadedStockModel) */
static void sm_free(void) { free(sm_v); free(sm_t); sm_v = NULL; sm_t = NULL; sm_nv = sm_nt = sm_cv = sm_ct = 0; }
/* CreateShadedStockModel counts first, then callocs the vertex and index arrays once and gives up (no model) when either
   fails (mccore_stock.c l.1720 / 1760 / 2060-2080) */
static int sm_alloc(size_t nv, size_t nt) {
    sm_free();
    if (!nv || !nt || nv > (size_t)0x7fffffff || nt > (size_t)0x7fffffff) return 0;   /* int indices / counts */
    if (nv > SIZE_MAX / (3 * sizeof(float)) || nt > SIZE_MAX / (3 * sizeof(int))) return 0;
    sm_v = calloc(nv * 3, sizeof(float)); sm_t = calloc(nt * 3, sizeof(int));
    if (!sm_v || !sm_t) { sm_free(); return 0; }
    sm_cv = (int)nv; sm_ct = (int)nt; return 1;
}
static void sm_vert(double x, double y, double z) { if (sm_nv >= sm_cv) return; sm_v[3 * sm_nv] = (float)x; sm_v[3 * sm_nv + 1] = (float)y; sm_v[3 * sm_nv + 2] = (float)z; sm_nv++; }
static void sm_tri(int a, int b, int c) { if (sm_nt >= sm_ct) return; sm_t[3 * sm_nt] = a; sm_t[3 * sm_nt + 1] = b; sm_t[3 * sm_nt + 2] = c; sm_nt++; }
/* STL reading: mccore get_first_stl_triangle / get_next_stl_triangle, viewer/stl_mesh.c (shared with File > Open of an STL file) */
/* the file read once to count (CreateShadedStockModel's first loop) and once to fill. Status (ours, for the message): st[0] = 1
   the file could not be opened, st[1] = bytes left after the last whole binary record (a truncated record), st[2] =
   triangles with a NaN / infinite coordinate. Those triangles are left out (ours): Mastercam keeps them (atof / the float
   record) and its extents loop (DAT_18137ce08 .., `<` / `>` tests) skips NaN but takes +-Inf, which would make Fit infinite. */
static int tri_finite(double t[3][3]) { for (int v = 0; v < 3; v++) for (int k = 0; k < 3; k++) if (!isfinite(t[v][k])) return 0; return 1; }
static long stl_read(const char *path, void (*tri)(double t[3][3]), long st[3]) {
    st[0] = st[1] = st[2] = 0;
    StlTriReader R; long n = 0; double t[3][3];
    int ok = stl_tri_first(&R, path, t);
    if (!R.f) { st[0] = 1; return 0; }
    while (ok) {
        if (!tri_finite(t)) st[2]++;
        else { if (tri) tri(t); n++; }
        ok = stl_tri_next(&R, t);
    }
    if (!R.ascii) { fseek(R.f, 0, SEEK_END); long end = ftell(R.f); if (end > 0x54) st[1] = (end - 0x54) % 50; }   /* bytes of an incomplete last record */
    stl_tri_close(&R);
    return n;
}
static void sm_add_view_tri(double t[3][3]) {               /* ViewToWorldAdd on each vertex, three new vertices */
    int b = sm_nv; for (int v = 0; v < 3; v++) { double w[3]; stock_v2w(t[v], w); sm_vert(w[0], w[1], w[2]); }
    sm_tri(b, b + 1, b + 2);
}
static int solid_index(int idn) { for (int i = 0; i < F.nsolids; i++) if (F.solids[i].idn == idn && idn > 0) return i; return -1; }
static void sm_build(void) {
    sm_free();
    if (!stock_ok() || !stock_supported() || !cyl_ok()) return;
    if (F.stock_shape == 0) {
        if (!sm_alloc(8, 12)) return;                          /* calloc(0x18, 4) / calloc(0x24, 4) */
        double L[12][6]; stock_calc_lines(L);
        const double *P[8] = { L[0], L[0] + 3, L[2], L[2] + 3, L[4], L[4] + 3, L[6], L[6] + 3 };
        for (int k = 0; k < 8; k++) sm_vert(P[k][0], P[k][1], P[k][2]);
        static const int T[36] = { 0, 1, 5, 0, 5, 4, 1, 3, 7, 1, 7, 5, 3, 2, 6, 3, 6, 7, 2, 0, 4, 2, 4, 6, 0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6 };
        for (int k = 0; k < 12; k++) sm_tri(T[3 * k], T[3 * k + 1], T[3 * k + 2]);
    } else if (F.stock_shape == 1) {
        double cv[2][3], r, M[9]; cyl_geom(cv, &r, M);
        double gtol = mc_systol_units(mcam_is_metric(&F))->gl_tolerance;   /* gl_tolerance: config 7004 (FUN_1801ba530; default FUN_1801b4070 0.025 / 0.001) */
        int n = (short)(int)(2 * M_PI / (acos(1.0 - gtol / r) * 2.0) + 1.0); if (n < 32) n = 32;
        if (!sm_alloc(2 * (size_t)n + 2, 4 * (size_t)n)) return;   /* calloc((2n + 2) * 3, 4) / calloc(12 n, 4): n <= 0x7fff (short) */
        for (int a = 0; a < 2; a++) for (int i = 0; i < n; i++) { double w[3]; cyl_pt(M, cv[a], r, 2 * M_PI * i / n, w); sm_vert(w[0], w[1], w[2]); }
        for (int a = 0; a < 2; a++) { double w[3]; m_v2w(M, cv[a], w); sm_vert(w[0], w[1], w[2]); }
        for (int i = 0; i + 1 < n; i++) { sm_tri(i, i + n + 1, i + 1); sm_tri(i, i + n, i + n + 1); }
        sm_tri(n - 1, n, 0); sm_tri(n - 1, 2 * n - 1, n);
        for (int i = 0; i + 1 < n; i++) sm_tri(2 * n, i, i + 1);
        sm_tri(2 * n, n - 1, 0);
        for (int j = n; j + 1 < 2 * n; j++) sm_tri(2 * n + 1, j, j + 1);
        sm_tri(2 * n + 1, 2 * n - 1, n);
    } else if (F.stock_shape == 2) {
        int si = solid_index(F.stock_solid); const float *tr; int nt = si >= 0 ? scene_part_solid_tris(si, &tr) : 0;
        if (nt > 0 && !sm_alloc(3 * (size_t)nt, (size_t)nt)) { cui_message("Stock Setup", "Not enough memory for the solid's stock model (%d triangles): no stock model.", nt); return; }   /* ours, as above */
        for (int k = 0; k < nt; k++) { double t[3][3]; for (int v = 0; v < 3; v++) for (int c = 0; c < 3; c++) t[v][c] = tr[18 * k + 6 * v + c]; sm_add_view_tri(t); }
    } else if (F.stock_shape == 3) {                           /* the triangles counted first (the first read loop), then read again */
        long st[3], st2[3]; long nt = stl_read(F.stock_file, NULL, st);
        const char *fb = strrchr(F.stock_file, '/'); fb = fb ? fb + 1 : F.stock_file;
        /* Mastercam shows nothing when the model cannot be made (CreateShadedStockModel only _MCAsserts and returns; its caller
           stock_calc_geom ignores the result): these messages are ours (caption opmanres 3855 "Stock Setup") */
        if (st[0]) { cui_message("Stock Setup", "The stock file %s could not be read: no stock model.", fb); return; }
        if (nt <= 0) { cui_message("Stock Setup", "The stock file %s holds no triangles%s: no stock model.", fb, st[2] ? " with valid coordinates" : ""); return; }
        if (!sm_alloc(3 * (size_t)nt, (size_t)nt)) { cui_message("Stock Setup", "Not enough memory for the stock model of %s (%ld triangles): no stock model.", fb, nt); return; }
        stl_read(F.stock_file, sm_add_view_tri, st2);
        if (st[1] || st[2]) cui_message("Stock Setup", "The stock file %s: %s%s%s.", fb, st[1] ? "the last triangle record is incomplete and was left out" : "",
                                        st[1] && st[2] ? "; " : "", st[2] ? "triangles with invalid (NaN / infinite) coordinates were left out" : "");
    }
}
/* the model is rebuilt whenever its inputs change (Mastercam: at every stock_ok(1) -> stock_calc_geom; ours: a key) */
static void sm_sync(void) {
    if (!loaded || !F.stock_rec) { if (sm_built || sm_v) { sm_free(); sm_built = 0; } return; }   /* no part / no stock: nothing held */
    SmKey k; memset(&k, 0, sizeof k);
    k.shape = F.stock_shape; k.sel = F.stock_sel; k.axis = F.stock_axis; k.solid = F.stock_solid; k.vno = F.stock_vno;
    for (int c = 0; c < 3; c++) { k.s[c] = F.stock[c]; k.o[c] = F.stock_org[c]; }
    if (F.stock_shape == 2) { const float *t; int si = solid_index(F.stock_solid); k.solid_tris = si >= 0 ? scene_part_solid_tris(si, &t) : -1; }
    if (F.stock_shape == 3) { snprintf(k.file, sizeof k.file, "%s", F.stock_file); struct stat st; if (stat(F.stock_file, &st) == 0) { k.fsz = st.st_size; k.fmt = (long long)st.st_mtime; } }
    if (sm_built && !memcmp(&k, &sm_key, sizeof k)) return;
    sm_key = k; sm_built = 1; sm_build();
}
int stock_model(const float **v, const int **t) { sm_sync(); *v = sm_v; *t = sm_t; return sm_nt; }

/* draw_stock (mastercam.dll 0x1803360a0): the active machine group's stock when +0x1260 Display is on and stock_ok, shape
   < 2 as the 12 lines (draw_line_style(line, 4, mill_stock_color)); the lines are the group's (+0x1380, recomputed by
   stock_ok(1) -> stock_calc_geom). With the Solid display (+0x1261) and a model, the model shaded instead (FUN_18033fe70) */
int stock_display_lines(double L[12][6]) {
    if (!F.stock_disp || F.stock_shape > 1 || !stock_ok() || !stock_supported() || !cyl_ok()) return 0;
    if (F.stock_shaded && stock_display_shaded()) return 0;
    stock_calc_lines(L);
    return 12;
}
/* shapes 2 / 3 (and the shaded display): the model; draw_stock's OpenGL branch draws each triangle as a GL_LINE_STRIP
   p0 p1 p2 p0 in line style 4 (one stipple run per triangle), or shaded with +0x1261 */
int stock_display_model(void) {
    if (!F.stock_disp || F.stock_shape > 3 || !stock_ok() || !stock_supported() || !cyl_ok()) return 0;
    const float *v; const int *t; return stock_model(&v, &t);
}
int stock_display_shaded(void) { return F.stock_shaded && stock_display_model() > 0; }
/* GetFitExtents' stock (FUN_180337650): Display and Fit screen (+0x1228) on and stock_ok: shape < 2 the 24 end points of the
   12 lines, else the model's vertices (triangle by triangle) */
__attribute__((constructor)) static void st_fit_hook(void) { scene_fit_extra = stock_fit_iter; atexit(stock_free_all); }   /* scene.c view_fit (ours: the hook) */
void stock_fit_iter(void (*acc)(void *u, double x, double y, double z), void *u) {   /* no copy: the lines / the model walked in place */
    if (!loaded || !F.stock_rec || !F.stock_disp || !F.stock_fit || !stock_ok() || !stock_supported() || !cyl_ok()) return;
    if (F.stock_shape < 2) {
        double L[12][6]; stock_calc_lines(L);
        for (int l = 0; l < 12; l++) for (int e = 0; e < 2; e++) acc(u, L[l][3 * e], L[l][3 * e + 1], L[l][3 * e + 2]);
    } else {
        const float *v; const int *t; int nt = stock_model(&v, &t);
        for (long i = 0; i < 3L * nt; i++) acc(u, v[3 * t[i]], v[3 * t[i] + 1], v[3 * t[i] + 2]);
    }
}
static void fit_count(void *u, double x, double y, double z) { (void)x; (void)y; (void)z; (*(long *)u)++; }
long stock_fit_points(void) { long n = 0; stock_fit_iter(fit_count, &n); return n; }
/* everything the stock model holds (shape change / another file / a new part rebuild it through sm_sync; the app's exit) */
void stock_free_all(void) { sm_free(); sm_built = 0; scene_take_stock_mesh(NULL, 0, NULL); }
/* calc_top_of_stock (mccore 0x1800d2a50): the highest Z of the 12 stock lines in the op's coordinates (Top here) when
   stock_ok; InitOperation (mccore 0x180046660, decomp/mccore_ops.c:2425) gives a new operation that top of stock (op
   +0x280) only when it is the first operation (the op list is empty), not lathe, with an absolute top (op +0x288 = 0) */
int stock_top(double *z) {
    if (!stock_ok() || !stock_supported() || !cyl_ok() || F.stock_shape > 1) return 0;   /* shapes 2 / 3: the lines are not computed by stock_calc_geom */
    double L[12][6]; stock_calc_lines(L); *z = -1e38;
    for (int l = 0; l < 12; l++) { if (L[l][2] > *z) *z = L[l][2]; if (L[l][5] > *z) *z = L[l][5]; }
    return 1;
}
/* stock_ok(1) -> stock_calc_geom: the lines (+0x1380) and, for a cylinder, the end circles (+0x15c0 / +0x1628) kept in
   the model for the part save (mcam_saveops.c stock_patch); shapes 2 / 3 leave the lines as they are */
void stock_calc_store(void) {
    sm_sync();                                             /* CreateShadedStockModel at the end of stock_calc_geom: the old model goes now */
    if (!stock_ok() || F.stock_shape > 1 || !stock_supported() || !cyl_ok()) return;
    stock_calc_lines(F.stock_lines);
    if (F.stock_shape == 1) { double M[9]; F.stock_arc_view = cyl_geom(F.stock_arc, &F.stock_arc_r, M); }
}
/* draw_stock: shape 1 also draws the two end circles (FUN_18021e1b0(arc, colour, style 4)) */
int stock_display_circles(double C[2][3], double *r, double M[9]) {
    if (!F.stock_disp || F.stock_shape != 1 || !stock_ok() || !stock_supported() || !cyl_ok()) return 0;
    if (F.stock_shaded && stock_display_shaded()) return 0;
    cyl_geom(C, r, M);
    return 1;
}
/* make_facing_pock_bound, cylinder: the axis as a vector (X (1,0,0), Y (0,1,0), Z (0,0,1)) through the active Tplane
   (view_to_world, Top here) must be parallel to the Tplane's Z row (|dot| > 1 - tol), else no boundary (-1); then one full
   circle ('A', start 0, sweep 2 pi) centred at the origin's +0x1248 / +0x1250, radius size X / 2, at Z = +0x1258 + size Y */
int stock_facing_circle(double c[3], double *ztop) {
    const double tol = 5e-05;
    if (F.stock_shape != 1 || !(F.stock[0] > tol && F.stock[1] > tol) || !stock_supported()) return 0;   /* facing_op_uses_stock */
    double a[3] = { F.stock_axis == 0, F.stock_axis == 1, F.stock_axis == 2 };
    if (!(fabs(a[2]) > 1 - tol)) return 0;
    c[0] = F.stock_org[0]; c[1] = F.stock_org[1]; c[2] = F.stock[0] * 0.5; *ztop = F.stock_org[2] + F.stock[1];
    return 1;
}
/* make_facing_pock_bound (mcmill 0x180098f90): a Facing operation without chains (op +0x8c = 0) faces the stock
   (facing_op_uses_stock 0x180098e00: X and Y > tol): the 4 lines of the job_setup_stock_corners rectangle in the Tplane,
   (min x, min y) -> (max x, min y) -> (max x, max y) -> (min x, max y), at the box's top Z. job_setup_stock_corners
   (0x1805e4dc0): with stock_ok(1) the corners are world_to_view(Tplane) of the group's lines' +0x1440 (x0 y0 z0) and +0x13f8
   (x1 y1 z1), the Z the larger of the two (the Tplane is Top here) */
int stock_facing_box(double box[4], double *ztop) {
    const double tol = 5e-05;
    if (!(F.stock[0] > tol && F.stock[1] > tol) || F.stock_shape != 0 || !stock_supported()) return 0;
    double L[12][6]; stock_calc_lines(L);
    const double *p = L[4], *q = L[2] + 3;
    box[0] = fmin(p[0], q[0]); box[1] = fmin(p[1], q[1]); box[2] = fmax(p[0], q[0]); box[3] = fmax(p[1], q[1]); *ztop = fmax(p[2], q[2]);
    return 1;
}

/* ---- the bounding cylinder (Cylindrical shape: All Surfaces / Solids / Entities) - mccad create_bcyl_stock_no_dlg
   @18005e8b0 -> FUN_180346dc0 -> FUN_180345da0 -> FUN_18005bab0, add_to_minmax @180057c70 in cylinder mode
   (DAT_1806a16c4 = 1) and the finishing step FUN_180057e50 (decomp/mccad_bcyl.c, mccad_bcyl_finish.c, mccore_inters.c).
   The fit is order dependent (an incremental circle, then a refinement towards the minimum circle), like the original;
   the points come in this program's entity order (Mastercam's display-list order is NOT DECODED). NOT DECODED: l_hv_ints
   (the general line intersection is used for axis-parallel lines), the operand order of MultiplyMatrix33 (the views
   below are the pre-multiplied ones, consistent with stock_calc_geom), the temporary plane's origin for Center on axis
   (world 0 here: the Top stock plane), two stack points the original leaves uninitialised in rare cases ({0, 0}). */
#define BC_TOL   (mc_systol.tol)       /* tol_exref = config 1000 (McSysTol; default 5e-5, decomp/mccore_systol.c) */
#define BC_2PI   6.283185307179586

typedef struct { double x, y; } bc_p2;
typedef struct { double x, y, a; } bc_d2;    /* CArray<D2> element: point + angle (0x18 bytes) */

typedef struct {
    int    state;          /* DAT_1806a1730 */
    int    center_on_axis; /* DAT_1806a16c1 */
    double cx, cy;         /* DAT_1806a16c8 / DAT_1806a16d0 */
    double zmin, zmax;     /* DAT_1806a16d8 / DAT_1806a16e0 */
    double r;              /* DAT_1806a16e8 */
    double fx, fy;         /* DAT_1806a16f0/16f8: first stored point */
    bc_p2 *pts; long n, cap;   /* DAT_1806a1750 CArray (p_3d with z=0): data 1758, size 1760 */
} bc_state;

static void bc_push(bc_state *s, double x, double y)   /* FUN_180056440 append */
{
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 64; s->pts = realloc(s->pts, s->cap * sizeof *s->pts); }
    s->pts[s->n].x = x; s->pts[s->n].y = y; s->n++;
}

/* add_to_minmax @180057c70, branch DAT_1806a16c4 == 1. v = point already in the cylinder view. */
static void add_point(bc_state *s, const double v[3])
{
    double x = v[0], y = v[1], z = v[2];
    if (!s->center_on_axis) {
        if (s->state == 0) {                       /* first point */
            s->zmin = s->zmax = z;
            s->cx = x; s->cy = y; s->r = 0.0;
            s->fx = x; s->fy = y;
            bc_push(s, x, y);
            s->state = 1;
            return;                                /* no z update path needed: already set */
        }
        if (s->state == 1) {                       /* waiting for a second distinct point */
            double dx = x - s->fx, dy = y - s->fy;
            s->r = sqrt(dx * dx + dy * dy) * 0.5;  /* assigned even when not accepted */
            if (fabs(s->r) > BC_TOL) {             /* tol <= |r| && |r| != tol */
                s->cy = (y + s->fy) * 0.5;
                s->cx = (x + s->fx) * 0.5;
                bc_push(s, x, y);                  /* (second point also kept in DAT_1806a1700) */
                s->state = 2;
            }
        } else {                                   /* state >= 2: grow circle (no tolerance) */
            bc_push(s, x, y);
            double d = sqrt((y - s->cy) * (y - s->cy) + (x - s->cx) * (x - s->cx));
            if (s->r < d) {
                double ux = s->cx - x, uy = s->cy - y;
                double l = sqrt(ux * ux + uy * uy);
                if (l == 0.0) { ux = 0.0; uy = 0.0; } else { ux /= l; uy /= l; }
                double qx = ux * s->r + s->cx, qy = uy * s->r + s->cy;   /* far side of old circle */
                double nr = sqrt((y - qy) * (y - qy) + (x - qx) * (x - qx));
                s->cx = (qx + x) * 0.5;
                s->r  = nr * 0.5;
                s->cy = (qy + y) * 0.5;
            }
        }
        if (z < s->zmin) s->zmin = z;              /* depth range, both state 1 and >= 2 */
        if (s->zmax < z) s->zmax = z;
    } else {                                       /* centre on axis */
        if (s->state == 0) {
            /* centre = active origin in view coords if ActiveOriginIsOn(0), else (0,0).
             * Draft assumes origin at world 0 -> (0,0). (temporary plane's origin: NOT DECODED) */
            double ox = 0.0, oy = 0.0;
            s->state = 1;
            s->r = sqrt((x - ox) * (x - ox) + (y - oy) * (y - oy));
            s->cx = ox; s->cy = oy;
            s->zmin = s->zmax = z;
        } else {
            s->state = 2;
            double d = sqrt((x - s->cx) * (x - s->cx) + (y - s->cy) * (y - s->cy));
            if (s->r < d) s->r = d;
            if (z < s->zmin) s->zmin = z;
            if (s->zmax < z) s->zmax = z;
        }
    }
}

/* FUN_18005a270: copy pts[i] to *p, return distance to current centre */
static double dist_i(const bc_state *s, long i, bc_p2 *p)
{
    if (p) *p = s->pts[i];
    double dy = s->cy - s->pts[i].y, dx = s->cx - s->pts[i].x;
    return sqrt(dx * dx + dy * dy);
}

/* FUN_1800599a0: farthest point; replaces only when > current + tol. *pf <- pts[0] first
 * (rdx passed through at 0x1800599cb). Returns its index. */
static long farthest(const bc_state *s, double *dmax, bc_p2 *pf)
{
    long idx = 0;
    *dmax = dist_i(s, 0, pf);
    for (long i = 1; i < s->n; i++) {
        bc_p2 p; double d = dist_i(s, i, &p);
        if (BC_TOL + *dmax < d) { *pf = p; *dmax = d; idx = i; }
    }
    return idx;
}

/* FUN_180059a40 + restrict_angle(0, 2pi) @1804b4440 */
static double angle_of(const bc_state *s, double px, double py)
{
    double X = px - s->cx, Y = py - s->cy, a;
    if (1e-12 <= Y * Y + X * X) { a = atan2(Y, X); if (a < 0.0) a += BC_2PI; }
    else a = 0.0;
    while (BC_2PI < a) a -= BC_2PI;
    while (a < 0.0) a += BC_2PI;
    if (fabs(a - BC_2PI) < 1e-5) a = 0.0;
    return a;
}

/* FUN_180059b20: collect support points (|r - d| < tol), dedupe by angle (FUN_180057db0: |da| < 1e-5) */
static long collect(const bc_state *s, bc_d2 *A, long *nA)
{
    *nA = 0;
    for (long i = 0; i < s->n; i++) {
        bc_p2 p; double d = dist_i(s, i, &p);
        if (fabs(s->r - d) < BC_TOL) {
            double a = angle_of(s, p.x, p.y);
            int dup = 0;
            for (long k = 0; k < *nA; k++) if (fabs(A[k].a - a) < 1e-5) { dup = 1; break; }
            if (!dup) { A[*nA].x = p.x; A[*nA].y = p.y; A[*nA].a = a; (*nA)++; }
        }
    }
    return *nA;
}

/* l_ints @1804b3480 (mccore): intersection of two infinite lines L1=(a,b), L2=(p,q).
 * The axis-parallel cases go to l_hv_ints (NOT DECODED); this draft uses the general formula for them. */
static int l_ints(const bc_p2 L1[2], const bc_p2 L2[2], bc_p2 *out)
{
    double dy1 = L1[1].y - L1[0].y, dx1 = L1[1].x - L1[0].x;
    if (dy1 * dy1 + dx1 * dx1 < 1e-24) return 0;
    double dx2 = L2[1].x - L2[0].x, dy2 = L2[1].y - L2[0].y;
    if (dx2 * dx2 + dy2 * dy2 < 1e-24) return 0;
    double u1 = L2[0].x, v1 = L2[0].y;
    double e5  = u1 - L1[0].x;
    double e21 = u1 - L2[1].x, e1 = v1 - L2[1].y;
    double den = e1 * dx1 - e21 * dy1;
    double num = e5 * e1 - (v1 - L1[0].y) * e21;
    double dot = e1 * dy1 + e21 * dx1;
    int nonpar = (BC_TOL * BC_TOL <= fabs(dot)) ? (BC_TOL <= fabs(den / dot)) : (BC_TOL * BC_TOL <= fabs(den));
    if (nonpar) {
        double t = num / den;
        out->x = L1[0].x + dx1 * t; out->y = L1[0].y + dy1 * t;
        return 1;
    }
    double l2 = sqrt(e21 * e21 + e1 * e1);
    if (!(BC_TOL < fabs(l2))) return 0;
    double dist = num / l2;
    if (!(fabs(dist) < BC_TOL)) return 0;
    /* coincident lines: midpoint of the closest endpoint pair (L1 outer, L2 inner, strict <, init 1e38) */
    double best = 1e38; int i2 = 1, i1 = 1;
    for (int o = 0; o < 2; o++) for (int k = 0; k < 2; k++) {
        double d = sqrt((L2[k].y - L1[o].y) * (L2[k].y - L1[o].y) + (L2[k].x - L1[o].x) * (L2[k].x - L1[o].x));
        if (d < best) { best = d; i2 = k; i1 = o; }
    }
    out->x = (L2[i2].x + L1[i1].x) * 0.5; out->y = (L2[i2].y + L1[i1].y) * 0.5;
    return 1;
}

/* FUN_18005c4a0: bisector of (pf,q)  x  line (centre -> pf) */
static int move_c4a0(const bc_state *s, bc_p2 pf, bc_p2 q, bc_p2 *nc)
{
    bc_p2 L1[2], L2[2];
    L1[0].x = (q.x + pf.x) * 0.5; L1[0].y = (q.y + pf.y) * 0.5;
    L1[1].x = -(pf.y - q.y) + L1[0].x; L1[1].y = (pf.x - q.x) + L1[0].y;
    L2[0].x = s->cx; L2[0].y = s->cy; L2[1] = pf;
    return l_ints(L1, L2, nc);
}

/* FUN_18005c5e0: bisector of (P1,q)  x  line (centre -> midpoint(P1,P2)) */
static int move_c5e0(const bc_state *s, bc_p2 P1, bc_p2 P2, bc_p2 q, bc_p2 *nc)
{
    bc_p2 L1[2], L2[2];
    L1[0].x = (q.x + P1.x) * 0.5; L1[0].y = (q.y + P1.y) * 0.5;
    L1[1].x = -(P1.y - q.y) + L1[0].x; L1[1].y = (P1.x - q.x) + L1[0].y;
    L2[0].x = s->cx; L2[0].y = s->cy;
    L2[1].x = (P1.x - s->cx) * 0.5 + s->cx + (P2.x - s->cx) * 0.5;
    L2[1].y = (P1.y - s->cy) * 0.5 + s->cy + (P2.y - s->cy) * 0.5;
    return l_ints(L1, L2, nc);
}

/* FUN_18005c2c0: try the circle with diameter A[0]A[1]; keep it iff every point d < r + tol. */
static int try_diameter(bc_state *s, const bc_d2 *A)
{
    double ocx = s->cx, ocy = s->cy, orr = s->r;
    double dx = A[1].x - A[0].x, dy = A[1].y - A[0].y;
    s->r = sqrt(dy * dy + dx * dx) * 0.5;
    s->cx = (A[1].x + A[0].x) * 0.5;
    s->cy = (A[1].y + A[0].y) * 0.5;
    int ok = 1;
    for (long i = 0; i < s->n && ok; i++) ok = dist_i(s, i, NULL) < s->r + BC_TOL;
    if (s->n > 0 && !ok) { s->cx = ocx; s->cy = ocy; s->r = orr; }
    return ok;
}

/* FUN_180059550: sort A by angle, find the largest angular gap; true iff gap > 3.141492653589793 */
static int largest_gap(bc_d2 *A, long n, bc_p2 *P1, bc_p2 *P2, double *a1, double *a2)
{
    if (n < 2) return 0;
    for (long i = 0; i + 1 < n; i++)                      /* exchange sort, ascending angle */
        for (long j = i + 1; j < n; j++)
            if (A[j].a < A[i].a) { bc_d2 t = A[i]; A[i] = A[j]; A[j] = t; }
    double best = -BC_2PI;
    for (long i = 0; i < n; i++) {
        bc_d2 nx = (i == n - 1) ? A[0] : A[i + 1];
        if (i == n - 1) nx.a += BC_2PI;
        double g = nx.a - A[i].a;
        if (best < g) {
            P1->x = A[i].x; P1->y = A[i].y; *a1 = A[i].a;
            P2->x = nx.x;   P2->y = nx.y;   *a2 = nx.a;
            best = g;
        }
    }
    return 3.141492653589793 < best;
}

/* FUN_180057e50: refine towards the minimum enclosing circle of the stored points */
static void finish(bc_state *s)
{
    long n = s->n;
    if (n <= 2) return;
    bc_d2 *A = malloc(n * sizeof *A); long nA;
    /* loc128 / loc138 are the two stack p_2d's the original reuses between both phases.
     * They are uninitialised in the original when no candidate is found: draft uses {0,0}. NOT DECODED. */
    bc_p2 pf, loc128 = {0, 0}, loc138 = {0, 0}, P1 = {0, 0}, P2 = {0, 0};   /* P1 / P2: set by largest_gap before use */
    double dfar, dmax, a1, a2;

    long idx = farthest(s, &dfar, &pf);
    s->r = sqrt((pf.y - s->cy) * (pf.y - s->cy) + (pf.x - s->cx) * (pf.x - s->cx));
    if (collect(s, A, &nA) == 1) {                 /* phase 1: one support point */
        double best = -1.0;
        for (long i = 0; i < n; i++) {
            bc_p2 p; double d;
            if (i != idx && (d = dist_i(s, i, &p)) < dfar - BC_TOL && best < d) { best = d; loc128 = p; }
        }
        do {
            if (!move_c4a0(s, pf, loc128, &loc138)) break;
            s->cx = loc138.x; s->cy = loc138.y;
            s->r = sqrt((pf.y - s->cy) * (pf.y - s->cy) + (pf.x - s->cx) * (pf.x - s->cx));
            farthest(s, &dmax, &loc128);
        } while (s->r + BC_TOL < dmax);
    }
    long k = collect(s, A, &nA);
    int cont = 1;
    do {                                           /* phase 2: two supports on one side */
        if (k != 2 || try_diameter(s, A) || !largest_gap(A, nA, &P1, &P2, &a1, &a2)) break;
        idx = farthest(s, &dfar, &pf);
        double best = -1.0;
        for (long i = 0; i < n; i++) {
            bc_p2 p; double d;
            if (i != idx && (d = dist_i(s, i, &p)) < dfar - BC_TOL) {
                double a = angle_of(s, p.x, p.y);
                if (BC_2PI < a2 && a < a2 - BC_2PI) a += BC_2PI;
                if (a1 <= a && a <= a2 && best < d) { loc138 = p; best = d; }
            }
        }
        do {
            if (!move_c5e0(s, P1, P2, loc138, &loc128)) { cont = 0; break; }
            double dx = P2.x - loc128.x, dy = P2.y - loc128.y;
            double d = sqrt(dy * dy + dx * dx);
            if (fabs(s->r - d) < BC_TOL) { cont = 0; break; }
            s->cx = loc128.x; s->cy = loc128.y; s->r = d;
            farthest(s, &dmax, &loc138);
        } while (s->r + BC_TOL < dmax);
        k = collect(s, A, &nA);
    } while (cont);
    free(A);
}

/* View rows (world_to_view v = M w, view_to_world w = M^T v; viewz = row 2 @mastercam 180341aa0).
 * create_bcyl_stock_no_dlg @18005e8b0: page axis 1 -> 1, 2 -> 0, else 2.
 * FUN_18005bab0: 0 -> the active view, 1 -> RotateRelative(3), else RotateRelative(5): R . active (rot_view above:
 * MultiplyMatrix33 = SYSTYPES operator*, decoded 2026-10-22). The stock page runs it with the stock plane active
 * (FUN_1801f26d0: ActivePlaneSaveRestore(Planes, 0, stock group_view)); the Bounding Box panel with Top (cui_bbox.c). */
static int bcyl_fit(const double S[9], const double (*pts)[3], int n, int axis /*page 0 X,1 Y,2 Z*/, int center_on_axis,
             double out_c[3], double out_dir[3], double *out_r, double *out_len)
{
    double Mv[9]; if (axis == 2) memcpy(Mv, S, sizeof Mv); else rot_view(axis == 1 ? 3 : 5, S, Mv);
    const double (*M)[3] = (const double (*)[3])Mv;
    bc_state s; memset(&s, 0, sizeof s);
    s.center_on_axis = center_on_axis != 0;
    for (int i = 0; i < n; i++) {
        double v[3];
        for (int r = 0; r < 3; r++) v[r] = M[r][0] * pts[i][0] + M[r][1] * pts[i][1] + M[r][2] * pts[i][2];
        add_point(&s, v);
    }
    *out_r = 0.0; *out_len = 0.0;                  /* FUN_18005bab0: *param_3 = 0, *param_4 = 0 */
    if (n > 0) {                                   /* DAT_1806a16c0 set by any entity */
        finish(&s);
        *out_r = s.r;
        *out_len = s.zmax - s.zmin;
        double cv[3] = { s.cx, s.cy, s.zmin };     /* p_3d at DAT_1806a16c8 = (cx, cy, zmin) */
        for (int c = 0; c < 3; c++) out_c[c] = M[0][c] * cv[0] + M[1][c] * cv[1] + M[2][c] * cv[2];
        for (int c = 0; c < 3; c++) out_dir[c] = M[2][c];
    }
    free(s.pts);
    return fabs(*out_r) > BC_TOL;                  /* create_bcyl_stock_no_dlg: tol <= |r| && |r| != tol */
}

/* ---- the page */
static void real_str(char *b, size_t n, double v) { ecp_real_str(b, n, v); }   /* make_mcreal_str */
static void fill_texts(void) {
    for (int k = 0; k < 3; k++) { real_str(stp.org_t[k], sizeof stp.org_t[k], stp.org[k]); real_str(stp.size_t_[k], sizeof stp.size_t_[k], stp.size[k]); }
}
/* FUN_1801f2000: group -> page. The plane's name (GetPlanePtrByID(+0x8a8) + 0x110, else 4016 "Undefined"); Display / Fit
   screen / Solid display; origin = world_to_view(view_to_world(+0x1248, vno) - plane origin, vno) (= +0x1248 for the system
   planes); sizes; shape, axis, centre; the STL file (st_file_rebuild below) into the combo (CFileComboBox::InitComboBox);
   corner > 8 -> 0; shape 2: the solid id (+0x10338 = +0x1268), the shape back to 0 when fill_in_ent_idn finds no SOLID_ID
   entity of that id; shape 3: the rebuilt file missing -> message_box_ok(opmanres 3163 "Cannot open file: %s\nFile not found"
   with that file, caption 3855 "Stock Setup") and the shape back to 0, else VerifyFileSuffix(".stl") (always true after the
   rebuild); Use Machine Tree (+0x16a8). The page keeps a copy of the group's stock for FUN_1801e1a20 (after the sheet). */
/* FUN_1801f2000 rebuilds the group's file (+0x1271, 0x104) on every open: prefix = GetFilePrefix (the part before the last
   '\\'; mccore 0x18034b8d0), replaced by stl_prefix when there is none or dir_exists fails; suffix = GetFileSuffix (the
   extension WITHOUT its dot, mccore 0x18034b9c0) compared by _stricmp with stl_suffix ".stl" - never equal, so the suffix is
   always ".stl"; name = GetFileName(+0x1271, 1) (the file name, extension cut at the last '.'; 0x18034b630), empty -> "T"
   (written to +0x1271); then FUN_1801df580 = build_filename_s(+0x1271, 0x104, prefix, suffix) (mccore 0x1806f1f00: prefix +
   "\\" unless it ends with one + reduce_filename's name (no extension) + "." unless the suffix starts with one + suffix;
   decomp/mastercam_stlname*.{c,txt}, decomp/mccore_filename_parts.c). So "dir\\box.txt" becomes "dir\\box.stl".
   Here the separator is the host's ('/' or '\\', ours). NOT DECODED: stl_prefix (set at run time) - with no directory part
   or a directory that does not exist the file is left as it is (ours), so an empty name stays empty. */
static int dir_exists_(const char *p) { struct stat st; return p[0] && stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static void st_file_rebuild(char *f, size_t n) {
    const char *s1 = strrchr(f, '/'), *s2 = strrchr(f, '\\'), *sep = !s1 ? s2 : !s2 ? s1 : s1 > s2 ? s1 : s2;
    if (!sep) return;
    char pre[0x104], name[0x104];
    snprintf(pre, sizeof pre, "%.*s", (int)(sep - f), f);
    if (!dir_exists_(pre)) return;
    snprintf(name, sizeof name, "%s", sep + 1);
    char *dot = strrchr(name, '.'); if (dot) *dot = 0;
    if (!name[0]) snprintf(name, sizeof name, "T");
    char out[0x104]; snprintf(out, sizeof out, "%s%c%s.stl", pre, *sep, name);
    snprintf(f, n, "%s", out);
}
static struct { int shape, vno, axis, solid; double org[3], size[3]; char file[0x104]; } st_snap;   /* EditGroupPropertiesDlg's copies (local_c20 / local_15a0) */
static int stl_suffix_ok(const char *p) {                  /* VerifyFileSuffix(p, stl_suffix ".stl"): "." + GetFileSuffix == ".stl" (Ordinal_2863: case rule NOT DECODED; ours: case-insensitive, as FUN_1801f2000's _stricmp) */
    const char *d = strrchr(p, '.'), *sl = strrchr(p, '/'), *bs = strrchr(p, '\\');
    if (!d || (sl && sl > d) || (bs && bs > d) || !d[1]) return 0;
    return !strcasecmp(d, ".stl");
}
void st_open(int page) {
    memset(&stp, 0, sizeof stp);
    stp.disp = F.stock_rec ? F.stock_disp : 1; stp.fit = F.stock_rec ? F.stock_fit : 1;
    stp.shaded = F.stock_shaded != 0; stp.shape = F.stock_shape; stp.axis = F.stock_axis; stp.center = F.stock_center;
    stp.mtree = F.stock_mtree; stp.solid = F.stock_solid;
    if (F.stock_rec) st_file_rebuild(F.stock_file, sizeof F.stock_file);   /* the group's +0x1271 (not a change of the part: no dirty flag) */
    snprintf(stp.file, sizeof stp.file, "%s", F.stock_file);
    for (int k = 0; k < 3; k++) { stp.org[k] = F.stock_org[k]; stp.size[k] = F.stock[k]; }
    stp.corner = F.stock_sel >= 0 && F.stock_sel <= 8 ? F.stock_sel : 0;    /* > 8 -> 0 */
    plane_text(stp.plane, sizeof stp.plane);
    if (stp.shape == 2 && solid_index(stp.solid) < 0) stp.shape = 0;
    else if (stp.shape == 3) {
        if (!file_exists(stp.file)) { cui_message("Stock Setup", "Cannot open file: %s\nFile not found", stp.file); stp.shape = 0; }
        else if (!stl_suffix_ok(stp.file)) stp.shape = 0;
    }
    fill_texts();
    st_snap.shape = F.stock_shape; st_snap.vno = F.stock_rec ? F.stock_vno : 1; st_snap.axis = F.stock_axis; st_snap.solid = F.stock_solid;
    for (int k = 0; k < 3; k++) { st_snap.org[k] = F.stock_org[k]; st_snap.size[k] = F.stock[k]; }
    snprintf(st_snap.file, sizeof st_snap.file, "%s", F.stock_file);
    show_stock_setup = 1; st_page = page >= 0 && page < 3 ? page : 2; st_pick = 0; st_q2d = 0; st_browse = 0;
    gp_open(); gp_visit(st_page);                          /* Files / Tool Settings (cui_grpprops.c); EditGroup's page (FUN_1801e1950) */
}
void st_set_page(int page) { if (!show_stock_setup || page < 0 || page > 2) return; st_page = page; gp_visit(page); }
/* FUN_1801e1a20 (decomp/mastercam_stockchanged.c): changed = the shape (or < 0), the stock plane's view number, the origin
   (1e-10), then by shape: 0 / 1 sizes X, Y (+ Z for 0, the axis for 1), 2 the solid id, 3 the file name; corner, display
   flags and the rest are not compared */
static int st_changed(void) {
    int sh = F.stock_shape;
    if (sh != st_snap.shape || (F.stock_rec ? F.stock_vno : 1) != st_snap.vno || sh < 0) return 1;
    for (int k = 0; k < 3; k++) if (fabs(F.stock_org[k] - st_snap.org[k]) > 1e-10) return 1;
    if (sh < 2) {
        if (fabs(F.stock[0] - st_snap.size[0]) > 1e-10 || fabs(F.stock[1] - st_snap.size[1]) > 1e-10) return 1;
        return sh == 0 ? fabs(F.stock[2] - st_snap.size[2]) > 1e-10 : F.stock_axis != st_snap.axis;
    }
    if (sh == 2) return F.stock_solid != st_snap.solid;
    if (sh == 3) return strcmp(F.stock_file, st_snap.file) != 0;
    return 1;
}
/* Mark2dHSTOpsRefMachStockDef4Regen (mccore 0x1800ac9d0, decomp/mccore_stockregen.c): every op of the main op list with
   opcode 0x86 (2D High Speed), its sub-style (op +0x11c0 = record +0x1228, MP 12713) < 2 (Dynamic Mill / Area Mill), the byte
   at op +0x1760 (record +0x17c8, MP 41144 - its meaning NOT DECODED) set, in this machine group (OMgetOpMachineGroup; here
   record +0x5bc, as nc_post.c) -> DatabaseRetrieve, op +0x94 = 1 (dirty, record +0xfc) and op +0x97 = 0 (record +0xff),
   UpdateListAndDB. (Ghidra's long is 4 bytes: plVar1[0x470] / [0x5d8] are byte offsets 0x11c0 / 0x1760.) Here the stored
   Mastercam ops get Op.regen: the dirty tree image (op +0x94: 0x13 / 0x18) and the record bytes written by the part save
   (mcam_saveops.c). Posting a dirty op (COpMgrTree::PostSelected -> DirtyOperationWarning, decomp/mastercam_post_dirty.c)
   opens opmanres dialog 977 "Warning: Dirty Operations Selected": 25350 "Regenerate unlocked operations and continue" (the
   default, RegenSelected) / 25351 "Do not regenerate and continue" (posts the stored NCI; ops whose NCI section is invalid,
   bnci_op_section_valid, are unselected) / Cancel = no post. Dialog 977 is NOT PORTED and a stored Mastercam op cannot be
   regenerated here: the post takes the 25351 branch for these ops without asking (ours) - the stored toolpath is posted
   unchanged (nc_post.c refuses only the tool-changed `stale` ops). Ops made here keep no record (their +0x17c8 comes from the defaults library: 0 in all four). Data: +0x17c8
   is 0 in every 2D HS op of the user's parts and the defaults libraries - nothing is marked there. */
int stock_regen_mark(void) {
    int n = 0;
    for (int i = 0; i < F.nops; i++) {
        Op *o = &F.ops[i];
        int mine = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) mine = 1;
        if (mine || o->opcode != 0x86 || !o->raw || o->raw_n < 0x17c9 || o->raw[0x1228] >= 2 || !o->raw[0x17c8]) continue;
        int32_t g; memcpy(&g, o->raw + 0x5bc, 4); if (F.stock_rec && g != F.stock_grp) continue;
        o->regen = 1; n++;
    }
    if (n) doc_dirty = 1;
    return n;
}
/* after the sheet (EditGroupPropertiesDlg, OK or Cancel): mill groups run stock_ok(1) (stock_calc_geom: the lines and the
   model) and, when FUN_1801e1a20 sees a change against the copies taken before the sheet, Mark2dHSTOpsRefMachStockDef4Regen */
static void st_after_sheet(void) {
    stock_calc_store();
    if (st_changed()) stock_regen_mark();
    overlay_dirty = 1;
}
/* FUN_1801ed0b0 (OnOK, apply): page -> group: shape, axis, the combo's file (CFileComboBox::GetSelectedFile -> +0x1271), the
   solid id (+0x10338 -> +0x1268), centre, Display, Fit screen, Solid display, the origin (view_to_world + plane origin ->
   world_to_view: the same values for the system planes), sizes, corner; shape 1 -> size Z = 0; shape 3 with a missing file or
   another suffix -> shape 0; Use Machine Tree (+0x16a8) and CCNCMachine::UpdateStockFromMGData (machine definition: NOT
   DECODED, nothing here) */
void st_ok(void) {
    gp_ok();                                               /* the Files / Tool Settings pages' OnOK (FUN_1801e5590 / FUN_1801ec1b0) */
    if (st_page_live()) {
        F.stock_shape = stp.shape; F.stock_axis = stp.axis; F.stock_center = stp.center;
        snprintf(F.stock_file, sizeof F.stock_file, "%s", stp.file); F.stock_solid = stp.solid;
        F.stock_disp = stp.disp; F.stock_fit = stp.fit; F.stock_shaded = stp.shaded;
        for (int k = 0; k < 3; k++) { F.stock_org[k] = stp.org[k]; F.stock[k] = stp.size[k]; }
        F.stock_sel = (int16_t)stp.corner;
        if (F.stock_shape == 1) F.stock[2] = 0;               /* cylindrical: +0x1240 = 0 */
        else if (F.stock_shape == 3 && (!file_exists(F.stock_file) || !stl_suffix_ok(F.stock_file))) F.stock_shape = 0;
        F.stock_mtree = stp.mtree;
        if (!F.stock_rec) { F.stock_rec = 1; F.stock_vid = 1; F.stock_vno = 1; for (int k = 0; k < 9; k++) F.stock_vm[k] = k % 4 == 0; }
        F.stock_dirty = 1; doc_dirty = 1;
    }
    show_stock_setup = 0; st_pick = 0; st_q2d = 0; st_browse = 0;
    st_after_sheet();
}
void st_cancel(void) { show_stock_setup = 0; st_pick = 0; st_q2d = 0; st_browse = 0; cam_msg[0] = 0; st_after_sheet(); }
int st_page_live(void) { return gp_visited(2); }       /* the Stock Setup page was shown: its OnOK runs (MFC PSN_APPLY to the
                                                          pages that were created) */
/* the sheet's pages for the front ends: 0 / 1 cui_grpprops.c, 2 this page */
const StCtl *sh_table(int page) { return page == 2 ? ST_1590 : gp_table(page); }
const StCtl *sh_ctl(int page, unsigned id) { return page == 2 ? st_ctl(id) : gp_ctl(page, id); }
int sh_shown(int page, unsigned id) { return page == 2 ? st_shown(id) : gp_shown(page, id); }
int sh_enabled(int page, unsigned id) { return page == 2 ? st_enabled(id) : gp_enabled(page, id); }
int sh_checked(int page, unsigned id) { return page == 2 ? st_checked(id) : gp_checked(page, id); }
char *sh_text(int page, unsigned id) { return page == 2 ? st_text(id) : gp_text(page, id); }
void sh_edit(int page, unsigned id, const char *t) { if (page == 2) st_edit(id, t); else gp_edit(page, id, t); }
void sh_click(int page, unsigned id) { if (page == 2) st_click(id); else gp_click(page, id); }
int sh_bitmap(int page, unsigned id) { return page == 2 ? (id == 960 ? 938 : id == 1253 ? 929 : 831) : gp_bitmap(page, id); }   /* FUN_1801ef9d0 / FUN_1801e4410 */

/* FUN_1801ed700 (mill template, Use Machine Tree off) + what is not ported here */
int st_shown(unsigned id) {
    const StCtl *c = st_ctl(id);
    if (!c || c->role == STR_HIDDEN) return 0;
    if (stp.shape != 0 && (id == 1223 || id == 1235 || id == 1236 || id == 1237)) return 0;   /* only Z / the labels hide */
    return 1;
}
int st_enabled(unsigned id) {
    if (!st_shown(id)) return 0;
    switch (id) {
    case 734: return 0;                                    /* Use Machine Tree: the machine definition's component tree (CMachineTreeCtrl,
                                                              CCNCMachine::UpdateStockFromMGData) is NOT DECODED */
    case 528: return stp.shape == 2;                       /* FUN_1801ed700: 0x210 only for the solid shape */
    case 364: case 1253: return stp.shape == 3;            /* 0x16c / 0x4e5 only for the file shape */
    case 960: return 1;                                    /* 0x3c0: in no list (FUN_1801f1d70 needs the group) */
    case 25077: return stp.shape == 0 && F.nops > 0;      /* FUN_1801ed700: rectangular and the op list not empty */
    case 25451: if (F.stock_rec && F.stock_vno > 1) return 0; break;   /* ours: the Bounding Box panel (cui_bbox.c) works in Top
                                                              only (mc:FUN_1801f1150 runs it with the stock plane active) */
    }
    if (id == 1252 || id == 530 || id == 532) return stp.disp;
    if (id == 25070 || id == 25071 || id == 25072) return stp.shape == 1;
    if (stp.shape != 0 && id == 25452) return 0;
    if (stp.shape > 1) switch (id) {
        case 1215: case 1230: case 1231: case 1219: case 1221: case 1223: case 25452: case 25451: case 1243: case 1244: case 1245: case 1246: case 25077: return 0; }
    if (id == 307) return 0;                               /* the plane name: shown, changed through Select... */
    return 1;
}
int st_checked(unsigned id) {
    switch (id) {
    case 518: return stp.shape == 0; case 520: return stp.shape == 1; case 522: return stp.shape == 2; case 523: return stp.shape == 3;
    case 25070: return stp.axis == 0; case 25071: return stp.axis == 1; case 25072: return stp.axis == 2;
    case 526: return stp.center; case 25143: return stp.disp; case 1252: return stp.fit;
    case 530: return !stp.shaded; case 532: return stp.shaded; case 734: return stp.mtree;
    }
    return 0;
}
const StCtl *st_ctl(unsigned id) { for (const StCtl *c = ST_1590; c->text; c++) if (c->id == id) return c; return NULL; }
char *st_text(unsigned id) {
    switch (id) {
    case 1215: return stp.org_t[0]; case 1230: return stp.org_t[1]; case 1231: return stp.org_t[2];
    case 1219: return stp.size_t_[0]; case 1221: return stp.size_t_[1]; case 1223: return stp.size_t_[2];
    case 307: return stp.plane;
    case 364: return stp.file;                             /* the CFileComboBox's file (its list: NOT DECODED, the one file) */
    }
    return NULL;
}
/* an edit's text changed (DDX_ProcessEditReal on kill focus): the value is taken when it parses */
void st_edit(unsigned id, const char *t) {
    char *dst = st_text(id); if (!dst || id == 307 || id == 364) return;
    snprintf(dst, 32, "%s", t);
    char *e; double v = strtod(t, &e); if (e == t) return;
    switch (id) {
    case 1215: stp.org[0] = v; break; case 1230: stp.org[1] = v; break; case 1231: stp.org[2] = v; break;
    case 1219: stp.size[0] = v; break; case 1221: stp.size[1] = v; break; case 1223: stp.size[2] = v; break;
    }
}
/* FUN_1801edd80: a box -> size = |max - min| and the origin = the box point the corner names, all in stock-view coordinates
   (its two corners through world_to_view(stock view); the origin back through view_to_world, - plane origin,
   world_to_view). st_from_box takes the box in view coordinates, st_from_box_world two world corners. */
void st_from_box(const double a[3], const double b[3]) {
    double lo[3], hi[3], off[3];
    for (int k = 0; k < 3; k++) { lo[k] = fmin(a[k], b[k]); hi[k] = fmax(a[k], b[k]); stp.size[k] = fabs(b[k] - a[k]); }
    corner_off(stp.corner, stp.size, off);
    for (int k = 0; k < 3; k++) stp.org[k] = lo[k] - off[k];
    (void)hi; fill_texts();
}
void st_from_box_world(const double a[3], const double b[3]) {
    double M[9], va[3], vb[3]; sp_matrix(M); m_w2v(M, a, va); m_w2v(M, b, vb);
    st_from_box(va, vb);
}
/* entity extents (mccad FUN_180057940 / add_to_minmax): points, both line ends, arcs at CalculatePointsOnCurve's points
   (decomp/mccad_bbox_stock.c: n = |sweep| / 2 acos(1 - chord / r) + 1 steps, 4..100, plus each 90 degrees from the
   start; chord = curve_chord_dev, config 1006, defaults 0.02 mm / 0.001 in - mccore FUN_1803bc7d0), solids at their
   extents (sld_solid_extents_3d_view: the Extrude bodies' / the part solids' mesh vertices here) */
static double (*bc_in)[3]; static int bc_inn, bc_incap, bc_collect;   /* the points in visiting order (cylinder fit) */
static const double *bc_view;   /* the box's view (the active plane's rows; NULL = world): the stock page runs make_bounding_box
                                   with the stock plane active (FUN_1801f26d0) and FUN_1801f2e60 turns the box corners back with
                                   view_to_world(active view) - the box is in that view's coordinates */
static void mm_add(double bb[6], double x, double y, double z) {
    const double tol = 5e-05; double p[3] = { x, y, z };
    if (bc_view) { double w[3] = { x, y, z }; m_w2v(bc_view, w, p); }
    if (bc_collect) {
        if (bc_inn == bc_incap) { bc_incap = bc_incap ? bc_incap * 2 : 256; bc_in = realloc(bc_in, sizeof *bc_in * bc_incap); }
        bc_in[bc_inn][0] = x; bc_in[bc_inn][1] = y; bc_in[bc_inn][2] = z; bc_inn++;
    }
    for (int k = 0; k < 3; k++) { if (p[k] < bb[2 * k] - tol) bb[2 * k] = p[k]; if (p[k] > bb[2 * k + 1] + tol) bb[2 * k + 1] = p[k]; }
}
static void mm_arc(double bb[6], const Ent *e) {
    double cd = cfg_curve_chord;                                    /* curve_chord_dev (config 1006, McSysTol) */
    double sw = e->sw, half = acos(1.0 - cd / e->r);
    int n = 4;
    if (half * 2 > 1e-12) { n = (int)(fabs(sw) / (half * 2)) + 1; if (n < 4) n = 4; else if (n > 100) n = 100; }
    double dt = 1.0 / n, t = 0, q = M_PI / 2; int done = 0;
    while (!done) {                                        /* CalculatePointsOnCurve's loop (t over [0, 1]) */
        if (1.0 - 1e-08 <= t) { done = 1; t = 1.0; }
        if (fabs(sw) > q) { double tq = (q / fabs(sw)); if (tq - 1e-08 <= t && !done) { q += M_PI / 2; t = tq; } }
        double a = e->a0 + sw * t; V3 q = mcam_arc_pt(e, a);   /* eval_curve: a view arc (Ent.vw) in its view */
        mm_add(bb, q.x, q.y, q.z);
        t += dt;
    }
}
/* make_bounding_box over the entities carrying the selection bit: pick == NULL = every shown entity of the mask and the
   solids (set_clr_sel_bits(4, 0, 4, 1, mask): the stock buttons, the Bounding Box panel's All shown); pick != NULL = the
   entities with pick[i] set (the panel's Manual selection; ours: solid bodies are not in this program's entity selection,
   so a Manual selection holds wireframe only) */
int stock_bbox_sel(int mask, const unsigned char *pick, int npick, double bb[6]) {
    bb[0] = bb[2] = bb[4] = 1e38; bb[1] = bb[3] = bb[5] = -1e38;
    if (mask & 0x5ff) for (int i = 0; loaded && i < F.nents; i++) {      /* set_clr_sel_bits(mask) on the display list */
        const Ent *e = &F.ents[i];
        if (!e->type || e->gen || (ent_hidden && i < nent_hidden && ent_hidden[i])) continue;
        if (pick && (i >= npick || !pick[i])) continue;
        if (e->type == E_POINT && (mask & 1)) mm_add(bb, e->a.x, e->a.y, e->a.z);
        else if (e->type == E_LINE && (mask & 2)) { mm_add(bb, e->a.x, e->a.y, e->a.z); mm_add(bb, e->b.x, e->b.y, e->b.z); }
        else if (e->type == E_ARC && (mask & 4)) mm_arc(bb, e);
    }
    if ((mask & 0x10000) && !pick) {
        for (int b = 0; b < nsbodies; b++) { float *t; int nt = body_mesh(&sbodies[b], &t);   /* body_mesh counts triangles (3 vertices each;
                                                                                                    2026-11-18: the loop took only the first third) */
            for (int v = 0; v < 3 * nt; v++) mm_add(bb, t[6 * v], t[6 * v + 1], t[6 * v + 2]); free(t); }
        const float *pt; int nv = scene_part_tris(&pt);
        for (int v = 0; v < nv; v++) mm_add(bb, pt[6 * v], pt[6 * v + 1], pt[6 * v + 2]);
    }
    for (int k = 0; k < 3; k++) if (fabs(bb[2 * k + 1] - bb[2 * k]) >= 1e38) bb[2 * k] = bb[2 * k + 1] = 0;   /* create_bbox_stock_no_dlg */
    const double tol = 5e-05;
    return !(fabs(bb[1] - bb[0]) < tol && fabs(bb[3] - bb[2]) < tol && fabs(bb[5] - bb[4]) < tol);
}
static int bbox_of(int mask, double bb[6]) { return stock_bbox_sel(mask, NULL, 0, bb); }
/* FUN_18005bab0 over the same entities: the points in visiting order (bbox_of's add_to_minmax walk) fitted by bcyl_fit.
   axis = the page's axis (0 X, 1 Y, 2 Z). Returns 1 when any point was found (DAT_1806a16c0), r / len / c / dir set then */
int stock_bcyl_sel(int mask, const unsigned char *pick, int npick, int axis, int center, double c[3], double dir[3], double *r, double *len) {
    double bb[6];
    bc_collect = 1; bc_inn = 0; stock_bbox_sel(mask, pick, npick, bb); bc_collect = 0;
    *r = 0; *len = 0;
    if (bc_inn <= 0) return 0;
    static const double I[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    bcyl_fit(I, (const double (*)[3])bc_in, bc_inn, axis, center, c, dir, r, len);
    return 1;
}
/* FUN_1801f26d0 -> create_bbox_stock_no_dlg(mask, box, 1): in 2D construction mode it first asks mccadres 17 "Override 2D
   construction mode?" (caption 30 "Bounding Box"); No = the box's Z at the construction depth (FUN_180347840, depth 0
   here, expand 0) */
static void st_bbox_run(int mask, int flat) {
    double bb[6];
    stp.mask = mask;
    if (stp.shape == 1) {   /* FUN_1801f26d0 -> create_bcyl_stock_no_dlg(mask, axis, centre on axis) -> FUN_1801f2f60 */
        bc_collect = 1; bc_inn = 0; bbox_of(mask, bb); bc_collect = 0;
        double c[3], d[3], r, len;
        double S[9]; sp_matrix(S);
        if (bc_inn > 0 && bcyl_fit(S, (const double (*)[3])bc_in, bc_inn, stp.axis, stp.center, c, d, &r, &len)) {
            stp.disp = 1; stp.fit = 1;                     /* FUN_1801f2f60: both check boxes set */
            stock_w2v(c, stp.org);                         /* world_to_view(c - plane origin, the active = stock view) */
            stp.size[0] = 2 * r; stp.size[1] = len; stp.size[2] = 0;
            fill_texts();
        }
        return;
    }
    if (stp.shape != 0) return;                            /* solid / file shapes: no bounding shape (FUN_1801f26d0) */
    double S[9]; sp_matrix(S); bc_view = S;                /* the stock plane active (ActivePlaneSaveRestore): the box in its view */
    int got = bbox_of(mask, bb); bc_view = NULL;
    if (!got) return;
    if (flat) bb[4] = bb[5] = grid_z();                    /* FUN_180347840: the box's Z at the construction depth, in the active (= stock) view.
                                                              The depth in a non-Top stock plane's view is NOT DECODED - ours: the Top
                                                              construction depth (grid_z) for every plane */
    double a[3] = { bb[0], bb[2], bb[4] }, b[3] = { bb[1], bb[3], bb[5] };
    stp.disp = 1; stp.fit = 1;                             /* FUN_1801f2e60: Display + Fit screen checked */
    st_from_box(a, b);
}
static void st_bbox(int mask) {
    /* FUN_1801f1bc0 / 1c30 / 1ca0: the fields are zeroed (UpdateData(FALSE)) before the box is made */
    for (int k = 0; k < 3; k++) stp.org[k] = stp.size[k] = 0;
    fill_texts();
    if (!cfg_constr_3d && stp.shape == 0) { st_q2d = mask; return; }   /* create_bbox_stock_no_dlg's Yes / No question (st_q2d_answer);
                                                                           create_bcyl_stock_no_dlg has none */
    st_bbox_run(mask, 0);
}
void st_q2d_answer(int yes) { int m = st_q2d; st_q2d = 0; if (m) st_bbox_run(m, !yes); }
/* NCI extents (25077) = FUN_1801ef250: get_bnci_extents over the active machine group's ops (nc_post.c nc_extents); found
   -> corner 0, FUN_1801edd80 (size = |max - min|, origin = the top face's centre), UpdateData(FALSE), Display and Fit screen
   checked; nothing found -> no change, no message */
static void st_nci_extents(void) {
    int inc[MAX_CAMOPS], und = 0; double box[6];
    for (int j = 0; j < ncamops; j++) { OpHFlags h; hf_from_op(&h, camops[j]); inc[j] = h.top_inc; }
    double cd = cfg_curve_chord;                                    /* curve_chord_dev (config 1006, McSysTol) */
    if (!nc_extents(&F, camops, ncamops, inc, CAM_SYS_TOL, cd, box, &und)) return;   /* tol (config 1000) */
    if (box[0] > box[3]) return;                           /* no point added (the caller does not check: NOT DECODED what it shows) */
    stp.corner = 0;
    double a[3] = { box[0], box[1], box[2] }, b[3] = { box[3], box[4], box[5] };
    st_from_box_world(a, b);                               /* FUN_1801edd80 (world corners) */
    stp.disp = 1; stp.fit = 1;
}
/* a button / radio / check of the page (the message map FUN_1801ee0f0) */
void st_click(unsigned id) {
    if (!st_enabled(id)) return;
    switch (id) {
    case 518: case 520: case 522: case 523: { int s = id == 518 ? 0 : id == 520 ? 1 : id == 522 ? 2 : 3;
        if (s != stp.shape) { stp.shape = s; if (stp.mask) st_bbox_run(stp.mask, 0); } break; }   /* FUN_1801f0e80: FUN_1801f26d0 on a change */
    case 25070: case 25071: case 25072: {               /* FUN_1801ef060 / 0a0 / 0e0: FUN_1801f26d0 again with the last mask */
        stp.axis = id - 25070; if (stp.mask) st_bbox_run(stp.mask, 0); break; }
    case 526: stp.center = !stp.center; break;        /* control +0x5548 bound to +0x105f8 (no handler in FUN_1801ee0f0: no refit) */
    case 25143: stp.disp = !stp.disp; break;
    case 1252: stp.fit = !stp.fit; break;
    case 530: stp.shaded = 0; break; case 532: stp.shaded = 1; break;
    case 1241: st_pick = ST_PICK_ORIGIN; cam_msg[0] = 0; break;   /* select_point (prompt "*": NOT DECODED) */
    case 25452: st_pick = ST_PICK_C1; snprintf(cam_msg, sizeof cam_msg, "Select first stock corner point"); break;   /* opmanres 1447 */
    case 1243: st_bbox(0x100); break;                      /* All Surfaces */
    case 1244: st_bbox(0x10000); break;                    /* All Solids */
    case 1245: st_bbox(0x1905ff); break;                   /* All Entities */
    case 1246: for (int k = 0; k < 3; k++) stp.org[k] = stp.size[k] = 0; stp.mask = 0; fill_texts(); break;   /* Unselect All */
    case 25077: st_nci_extents(); break;                   /* NCI extents */
    case 25451: bb_stock_run(); break;                     /* Bounding box: mc:FUN_1801f1150 -> mccad create_boundingshape_either_method
                                                              (the function panel in stock mode, cui_bbox.c; the sheet is hidden meanwhile) */
    case 528:                                              /* FUN_1801f0fc0: AfxSuspendWindow, select_ent(opmanres 2798 "Select an entity",
                                                              mask 0x10000 = solids, one entity); a solid -> +0x10338 = its id */
        st_pick = ST_PICK_SOLID; snprintf(cam_msg, sizeof cam_msg, "Select an entity"); break;
    case 1253: st_browse = 1; break;                       /* FUN_1801ef850: PromptForFileToOpen(the path, "rb", ".stl") - the front end's
                                                              file dialog (STL Files (*.stl) / All Files (*.*)); st_browse_done */
    case 960:                                              /* FUN_1801f1d70: AfxSuspendWindow, SelectPlaneFromList(the stock plane's id) */
        st_pick = ST_PICK_PLANE; pls_begin(3); break;
    }
    if (id >= 1294 && id <= 1302) st_corner(id - 1294);
}
void st_corner(int k) { if (k >= 0 && k <= 8 && stp.shape == 0) stp.corner = k; }   /* FUN_1801f0070 (the size / origin stay) */
/* the STL browse result (FUN_1801ef850 after PromptForFileToOpen): VerifyFileSuffix(".stl") and file_exists -> the combo
   gets the file (InitComboBox); else no change. path NULL = the dialog was cancelled. */
void st_browse_done(const char *path) {
    if (!st_browse) return;                                /* no browse open (a late dialog result) */
    st_browse = 0;
    if (path && stl_suffix_ok(path) && file_exists(path)) snprintf(stp.file, sizeof stp.file, "%s", path);
}
/* FUN_1801f1d70 after SelectPlaneFromList: GetPlanePtrByID(the chosen id); a plane -> Plane::Convert into the group's
   group_view (+0x105f0 points at the group's +0x8a8: the group changes at once, OK or Cancel): zeroed 0x68 bytes, +0 the
   plane id, +2 its view number, +4 the plane's +0x348, +8 its matrix, +0x50 its origin; CheckGroupView(gv, 1, 1); the name;
   UpdateData(FALSE) (the page's numbers stay, now in the new plane's view). The seven system planes: id = view number,
   origin 0; the plane's +0x348: NOT DECODED for planes other than Top (0 in every part) - 0 written (ours). Nothing else of
   the group changes. */
void st_plane_selected(int id) {
    st_pick = 0;
    double M[9]; if (id < 1 || id > 7 || !view_std_matrix(id, M)) return;
    if (!F.stock_rec) F.stock_rec = 1;                    /* ours: a part without a machine group record (File > New here) gets one;
                                                              FUN_1801f1d70 needs the group (+0x4d8), which Mastercam always has */
    F.stock_vid = id; F.stock_vno = id; memcpy(F.stock_vm, M, sizeof M); F.stock_vo[0] = F.stock_vo[1] = F.stock_vo[2] = 0;
    F.stock_plane_dirty = 1; F.stock_dirty = 1; doc_dirty = 1;
    plane_text(stp.plane, sizeof stp.plane);
    overlay_dirty = 1;
}
void st_plane_cancel(void) { st_pick = 0; }               /* SelectPlaneFromList returned no plane: no change */
/* the solid pick (select_ent with mask 0x10000): the part's solids under the cursor ray, the front one (ours: a ray /
   triangle test on this program's mesh of each SOLID_ID solid; select_ent's Parasolid pick is NOT DECODED). The solids
   made here (Extrude) carry no SOLID_ID entity id (writing Parasolid solids is NOT DECODED) and cannot be picked as stock.
   A miss keeps the selection waiting (select_ent loops until a pick or Esc). mx / my: window coordinates (as ex_pick_body). */
static int ray_tri(const double o[3], const double d[3], const float *a, const float *b, const float *c, double *t) {
    double e1[3], e2[3], p[3], q[3], s[3];
    for (int k = 0; k < 3; k++) { e1[k] = b[k] - a[k]; e2[k] = c[k] - a[k]; s[k] = o[k] - a[k]; }
    p[0] = d[1] * e2[2] - d[2] * e2[1]; p[1] = d[2] * e2[0] - d[0] * e2[2]; p[2] = d[0] * e2[1] - d[1] * e2[0];
    double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2]; if (fabs(det) < 1e-18) return 0;
    double u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det; if (u < 0 || u > 1) return 0;
    q[0] = s[1] * e1[2] - s[2] * e1[1]; q[1] = s[2] * e1[0] - s[0] * e1[2]; q[2] = s[0] * e1[1] - s[1] * e1[0];
    double v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det; if (v < 0 || u + v > 1) return 0;
    *t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det; return *t >= 0;
}
int st_pick_solid_at(float mx, float my) {
    double vx, vy, o[3], o2[3], d[3]; view_px_to_view(mx - vp_x, my - vp_y, &vx, &vy);
    view_to_world3(vx, vy, 1e6, o); view_to_world3(vx, vy, 0, o2);
    for (int k = 0; k < 3; k++) d[k] = o2[k] - o[k];
    int best = -1; double bt = 1e300;
    for (int i = 0; i < F.nsolids; i++) {
        if (F.solids[i].idn <= 0) continue;
        const float *t; int nt = scene_part_solid_tris(i, &t);
        for (int k = 0; k < nt; k++) { double h; if (ray_tri(o, d, t + 18 * k, t + 18 * k + 6, t + 18 * k + 12, &h) && h < bt) { bt = h; best = i; } }
    }
    return best;
}
void st_solid_select(int i) {                              /* the pick's result: a part solid (F.solids index) */
    if (i < 0 || i >= F.nsolids || F.solids[i].idn <= 0) return;
    stp.solid = F.solids[i].idn; st_pick = 0; cam_msg[0] = 0; overlay_dirty = 1;
}
void st_solid_click(float mx, float my) { st_solid_select(st_pick_solid_at(mx, my)); }
/* the graphics click of a pick (AfxSuspendWindow around it: the sheet is hidden meanwhile) */
void st_pick_click(double wx, double wy, double gz) {
    V3 p = { wx, wy, gz };
    if (!geo_snap(&p, NULL, -1, 0) && !geo_cursor_on_plane(&p)) {   /* a view along the construction plane and nothing snapped:
        point_proc's map_cursor_to_3d result there is NOT DECODED - the click is refused (ours) */
        snprintf(cam_msg, sizeof cam_msg, "No point: pick a snapped point in this view"); return; }
    if (st_pick == ST_PICK_ORIGIN) { double w[3] = { p.x, p.y, p.z }; stock_w2v(w, stp.org); fill_texts(); st_pick = 0; cam_msg[0] = 0; }   /* FUN_1801f16f0: world_to_view(p - plane origin) */
    else if (st_pick == ST_PICK_C1) { stp.c1[0] = p.x; stp.c1[1] = p.y; stp.c1[2] = p.z; st_pick = ST_PICK_C2;
        snprintf(cam_msg, sizeof cam_msg, "Select second stock corner point"); }   /* opmanres 1448 */
    else if (st_pick == ST_PICK_C2) { double b[3] = { p.x, p.y, p.z }; st_from_box_world(stp.c1, b); st_pick = 0; cam_msg[0] = 0; }
    overlay_dirty = 1;
}
/* Esc during a pick: Select corners with only the first point q (world) = the origin from it (FUN_1801f1840, the code's own
   rule as decoded): corner 0: X / Y from world_to_view((q.x + size X / 2, q.y + size Y / 2) - plane origin), Z = q.z - size
   Z / 2 (not converted); other corners: X / Y from world_to_view(q - plane origin), Z = q.z - plane origin z (not converted).
   In the corner-0 branch the point handed to world_to_view has an uninitialised Z (stack local_298): NOT DECODED for a stock
   plane whose X / Y rows use Z - ours: q.z. The solid pick / the Plane Selection: no change. */
void st_pick_escape(void) {
    if (st_pick == ST_PICK_BBOX) { geo_cancel(); return; }   /* the Bounding Box panel runs: Esc = its Cancel (cui_bbox.c) */
    if (st_pick == ST_PICK_PLANE) { pls_cancel(); return; }
    if (st_pick == ST_PICK_C2) {
        double *q = stp.c1, w[3], v[3], o[3] = { 0, 0, 0 };
        if (F.stock_rec) for (int k = 0; k < 3; k++) o[k] = F.stock_vo[k];
        if (stp.corner == 0) { w[0] = q[0] + stp.size[0] * 0.5; w[1] = q[1] + stp.size[1] * 0.5; w[2] = q[2]; stock_w2v(w, v); stp.org[0] = v[0]; stp.org[1] = v[1]; stp.org[2] = q[2] - stp.size[2] * 0.5; }
        else { stock_w2v(q, v); stp.org[0] = v[0]; stp.org[1] = v[1]; stp.org[2] = q[2] - o[2]; }
        fill_texts();
    }
    st_pick = 0; cam_msg[0] = 0; overlay_dirty = 1;
}
/* a graphics point pick of the page is waiting (the Bounding Box panel's run hides the sheet too, but its clicks are the
   panel's entity selection, not a point pick; the solid pick / Plane Selection are not point picks either) */
int st_point_pick(void) { return st_pick == ST_PICK_ORIGIN || st_pick == ST_PICK_C1 || st_pick == ST_PICK_C2; }
/* create_boundingshape_either_method's success and its caller mc:FUN_1801f1150 (spec bboxpanel 5): rect -> the spans with
   |span| >= 1e38 zeroed, at least one span >= tol; the box (view coordinates of the stock view = world, Top) -> FUN_1801edd80
   (st_from_box: size + the origin by the page's corner) + FUN_1801f2e60 (Display + Fit screen); cylinder (r > tol) -> axis
   (struct code 0 -> Z, 1 -> Y, else X), centre on axis, origin = world_to_view(c - plane origin) (Top: c), diameter 2 r,
   length len, FUN_1801f2f60 (Display + Fit screen). shape = rect ? 0 : 1. Returns 1 when the page changed. */
int st_bbox_panel_result(int rect, const double box[6], const double cyl[8], int axis_code, int center) {
    const double tol = 5e-05;
    if (rect) {
        double b[6]; for (int k = 0; k < 6; k++) b[k] = box[k];
        int any = 0;
        for (int k = 0; k < 3; k++) { if (fabs(b[2 * k + 1] - b[2 * k]) >= 1e38) b[2 * k] = b[2 * k + 1] = 0; if (fabs(b[2 * k + 1] - b[2 * k]) >= tol) any = 1; }
        if (!any) return 0;
        stp.shape = 0;
        double a[3] = { b[0], b[2], b[4] }, c[3] = { b[1], b[3], b[5] };
        st_from_box(a, c);
    } else {
        if (!(cyl[0] > tol)) return 0;
        stp.shape = 1; stp.axis = axis_code == 0 ? 2 : axis_code == 1 ? 1 : 0; stp.center = center != 0;
        for (int k = 0; k < 3; k++) stp.org[k] = cyl[2 + k];
        stp.size[0] = 2 * cyl[0]; stp.size[1] = cyl[1]; stp.size[2] = 0;
        fill_texts();
    }
    stp.disp = 1; stp.fit = 1;
    return 1;
}
/* the picture: FUN_1801f2980 index (rect: 0 wire / 1 solid; cylinder X 2/3, Z 4/5, Y 6/7; else 0) -> opmanres bitmap 832 + i */
int st_picture_bmp(void) {
    int i = 0;
    if (stp.shape == 0) i = stp.shaded != 0;
    else if (stp.shape == 1) i = (stp.axis == 0 ? 2 : stp.axis == 1 ? 6 : 4) + (stp.shaded != 0);
    return 832 + i;
}
/* FUN_1801ef4e0: the selected corner's marker (rectangular shape only), in the picture's pixels: a line from (0, 3/4 h)
   to (cx - 14, cy), the arrow triangle (cx - 6, cy) (cx - 14, cy + 4) (cx - 14, cy - 4) and the cross (cx +- 4, cy),
   (cx, cy +- 4) at the corner button's centre; drawn black (default CPen / CBrush) */
int st_marker(int *button) { if (stp.shape != 0) return 0; *button = 1294 + stp.corner; return 1; }

static void st_dump_more(void);
void st_dump(void) {
    if (!show_stock_setup) { printf("    stocksetup closed  stock %g %g %g org %g %g %g corner %d disp %d shaded %d shape %d fit %d dirty %d\n",
                                    F.stock[0], F.stock[1], F.stock[2], F.stock_org[0], F.stock_org[1], F.stock_org[2], F.stock_sel, F.stock_disp, F.stock_shaded, F.stock_shape, F.stock_fit, F.stock_dirty); st_dump_more(); return; }
    printf("    stocksetup page %d shape %d disp %d fit %d shaded %d corner %d org '%s' '%s' '%s' size '%s' '%s' '%s' plane '%s' pick %d q2d %d bmp %d\n",
           st_page, stp.shape, stp.disp, stp.fit, stp.shaded, stp.corner, stp.org_t[0], stp.org_t[1], stp.org_t[2], stp.size_t_[0], stp.size_t_[1], stp.size_t_[2], stp.plane, st_pick, st_q2d, st_picture_bmp());
    printf("    stocksetup enabled:");
    for (const StCtl *c = ST_1590; c->text; c++) if (st_enabled(c->id)) printf(" %u", c->id);
    printf("\n");
    if (st_page < 2) gp_dump();                            /* Files / Tool Settings */
    { const char *fb = strrchr(stp.file, '/'); printf("    stocksetup page+ solid %d file '%s' browse %d\n", stp.solid, fb ? fb + 1 : stp.file, st_browse); }
}
/* the rest of the model (both apps): the plane, solid id, file, the model's triangle count, what is drawn, the regen marks */
static void st_dump_more(void) {
    double L[12][6]; const float *v; const int *t; int nr = 0;
    for (int i = 0; i < F.nops; i++) nr += F.ops[i].regen;
    int nl = stock_display_lines(L), sh = stock_display_shaded(); long nf = stock_fit_points(); int nt = stock_model(&v, &t);
    const char *fb = strrchr(F.stock_file, '/'); fb = fb ? fb + 1 : F.stock_file;   /* the file's name (the work folders differ) */
    printf("    stocksetup model plane %d vno %d solid %d file '%s' tris %d lines %d shaded %d fitpts %ld regen %d\n",
           F.stock_vid, F.stock_vno, F.stock_solid, fb, nt, nl, sh, nf, nr);
    if (nt > 0) {                                          /* the model's extents (world) */
        double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
        for (int i = 0; i < 3 * nt; i++) for (int k = 0; k < 3; k++) { double q = v[3 * t[i] + k]; if (q < lo[k]) lo[k] = q; if (q > hi[k]) hi[k] = q; }
        printf("    stocksetup model box %.4f %.4f %.4f  %.4f %.4f %.4f\n", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
    }
}
/* script steps (both apps): stocksetup[:PAGE] (open), stclick:ID, stedit:ID=V, stcorner:K, stpick:X,Y,Z, stesc,
   st2d:yes|no, stok, stcancel, stdump, stpage:N (the sheet's tab 0 Files / 1 Tool Settings / 2 Stock Setup; stclick / stedit
   go to the shown page), gpdump (cui_grpprops.c); ours for the dialogs the front ends open: stfile:PATH (the STL browse dialog returns
   PATH; stfile: alone = cancelled), stsolid:I (the solid pick hits part solid I), stsolidat:MX,MY (a click at window
   coordinates), stsolidw:X,Y,Z (a click on the world point's screen position); the stock plane's Plane Selection uses
   cui_planes.c's plrow:N / plok / plcancel */
int cui_stock_script(const char *st, const char *arg) {
#define IS(x) (!strcmp(st, x))
    if (IS("stocksetup")) { st_open(arg ? atoi(arg) : 2); return 1; }
    if (IS("stclick") && arg) { sh_click(st_page, (unsigned)atoi(arg)); return 1; }   /* the shown page's control */
    if (IS("stedit") && arg) { const char *eq = strchr(arg, '='); if (eq) sh_edit(st_page, (unsigned)atoi(arg), eq + 1); return 1; }
    if (IS("stpage") && arg) { st_set_page(atoi(arg)); return 1; }   /* a tab of the sheet */
    if (IS("gpdump")) { gp_dump(); return 1; }             /* the active group's Files / Tool Settings fields (+ the page when open) */
    if (IS("stcorner") && arg) { st_corner(atoi(arg)); return 1; }
    if (IS("stpick") && arg) { double x = 0, y = 0, z = 0; sscanf(arg, "%lf,%lf,%lf", &x, &y, &z); if (st_pick) st_pick_click(x, y, z); else printf("    (no stock pick armed)\n"); return 1; }
    if (IS("stesc")) { st_pick_escape(); return 1; }
    if (IS("st2d") && arg) { st_q2d_answer(!strcmp(arg, "yes")); return 1; }
    if (IS("stok")) { st_ok(); return 1; }
    if (IS("stcancel")) { st_cancel(); return 1; }
    if (IS("stdump")) { st_dump(); return 1; }
    if (IS("stfile")) { if (st_browse) st_browse_done(arg && arg[0] ? arg : NULL); else printf("    (no stock browse open)\n"); return 1; }
    if (IS("stsolid") && arg) { if (st_pick == ST_PICK_SOLID) st_solid_select(atoi(arg)); else printf("    (no solid pick armed)\n"); return 1; }
    if (IS("stsolidw") && arg) {                          /* a click where the world point X,Y,Z is on screen (both apps: their windows differ) */
        double x = 0, y = 0, z = 0; float sx, sy; sscanf(arg, "%lf,%lf,%lf", &x, &y, &z);
        if (st_pick != ST_PICK_SOLID) printf("    (no solid pick armed)\n");
        else if (scene_to_screen(view_w, view_h, x, y, z, &sx, &sy)) st_solid_click(sx + vp_x, sy + vp_y);   /* the view's pixels (view_px_to_view's frame) */
        return 1; }
    if (IS("stsolidat") && arg) { float x = 0, y = 0; sscanf(arg, "%f,%f", &x, &y); if (st_pick == ST_PICK_SOLID) st_solid_click(x, y); else printf("    (no solid pick armed)\n"); return 1; }
    return 0;
#undef IS
}
