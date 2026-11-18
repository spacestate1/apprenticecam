/* cui_planes.c - the operation's planes (WCS / Tplane / Cplane): the toolpath dialog's Planes page (mccoreres dialog 6547,
 * PlanesPageBase, decomp/mccore_planespage.c), the Plane Selection dialog (dialog 842, mastercam.dll SelectPlaneFromList /
 * PlaneSelectListDlg, decomp/mastercam_plane_convert.c + decomp/mastercam_planeselect_dlg.c), the Update Plane dialog
 * (mccoreres dialog 10269, CUpdateViewDialog::DoModalWorkOffset, decomp/mccore_updateview_wofs.c); toolpaths computed
 * in the Tplane and the post's frame (cam2d.c cam_plane_* / cam_generate, nc_post.c camop_events). formats/mcamui_dialogs.md
 * "Planes page" and formats/qt_port_plan.md "Planes" hold the full write-up.
 *
 * The op's planes: op_view x3 (0xb0 bytes each) at op +0x570 Tplane, +0x620 Cplane, +0x6d0 WCS (record 0x5d8 / 0x688 /
 * 0x738): +0 u8 1, +2 view number, +8 matrix_33 rows (the plane's x / y / z axes in world coordinates), +0x50 origin
 * (world), +0x68 plane id, +0x92 work offset (Plane::InitOperationView 0x1803db1c0). op coordinates = wpt_to_op (mccore
 * 0x1800a5950, decomp/mccore_opview.c): rows . (p - Tplane origin); op_to_wpt (0x1800a2230) the inverse. The engines and
 * the NCI work in op coordinates (get_chain_depth_op / tp_select_depth / calc_top_of_stock / bnci_create_zfeed_arcs).
 * Planes here = the seven system planes (ids 1..7 = views 1 Top .. 7 Isometric: ReservedPlanes::BuildStandardMatrices,
 * names of the SystemPlanes archive seen in the user's parts, cui_optree.c cui_sys_plane); their origin is the world
 * origin and their work offset -1 (Plane::Init 0x1803daf20: +0x330 = -1; MastercamPlanes::BuildReservedPlane).
 *
 * The page (InitPage 0x1805c5480, RefreshData 0x1805c69e0, handlers 0x1805c5dd0..0x1805c6700):
 *  - names: GetPlaneName of the three op_views' plane ids; origins: world_to_view(origin, view) (PlanesWorldCoords off);
 *  - Select Plane (3145 / 3149 / 3153 = OnWCSSelViewPb / OnTPlaneSelViewPb / OnCPlaneSelViewPb -> OnSelViewPb 0x1805c6010):
 *    SelectPlaneFromList (the Plane Selection dialog, the current plane preselected, FUN_18043c260); Cancel = no change;
 *    OK: Plane::Convert into that op_view (matrix, origin, id; ResyncOpViews: no-op for system planes); for the Tplane
 *    the work offset = the plane's (+0x330: -1 here -> Automatic) and EnableDisableWorkOffset;
 *  - copies (3289 OnXferWCSToTPlanePb, 3290 OnXferTPlaneToWCSPb, 3291 OnXferTPlaneToCPlanePb, 3292 OnXferCPlaneToTPlanePb):
 *    OnSelViewPb(destination, the source's plane) (+ ResyncOpViews: the op_views of the same plane id become copies);
 *    into the Tplane also SetToolPlane(the source op_view) = the source's +0x92 becomes the work offset; each op_view
 *    keeps its own +0x92 (CamParams wofs / wwofs / cwofs);
 *  - Work Offset: Manual 3155 / Automatic 3166 (OnWorkOffsetManual 0x1805c6480 -> EnableDisableWorkOffset 0x1805c47f0):
 *    Automatic = -1; Manual = the Tplane plane's work offset, -1 -> the defaults library op of this type's (default_ops_get:
 *    op +0x602 = record 0x66a) when that is not -1; the edit 3157 shows "%d" while Manual, else empty and disabled;
 *    OnKillfocusWorkOffsetEdit 0x1805c5f00: atoi, < -1 -> -1, CheckWorkOffset 0x1805c4240 (> 0x7fff -> -1; Manual =
 *    value >= 0; a value other than the plane's opens the Update Plane dialog; the op keeps the value in every branch);
 *  - OK (UpdateLocalOp 0x1805c7fd0 -> MapToOp 0x1805c5ab0): the Tplane +0x92 = the value when Manual and >= 0, else -1;
 *    the WCS / Cplane +0x92 take it when their plane id is the Tplane's.
 *  - Quick view row 7545 "Cplane / Tplane": GetCustomizableData 0x1805c4b90 puts the Tplane's name, RefreshData (run by
 *    InitPage) replaces it with the Cplane's (this +0xea20): cui_opdlg.c shows the Cplane's.
 *
 * Ours (listed in formats/qt_port_plan.md "Planes"): the page is row-laid like this program's other pages (the
 * template's three group columns become three blocks; the copy buttons sit between them as in the template); the bitmap
 * buttons show their template captions (bitmaps 0x4de / 0x15a1 / 0x16d / 0x16a not in the icon pack); the origins are
 * shown with 4 decimals; the work offset edit is shown only while Manual (nuklear / the page walker cannot grey a
 * property); the Update Plane OK is enabled only with choice 2 (choices 0 / 1 are not ported; 0 stays the decoded
 * default selection).
 *
 * NOT DECODED / NOT PORTED (greyed or left out): planes other than the seven system planes (the part's MastercamPlanes
 * archive is not parsed: no named / user planes, New plane / from geometry / solid face / Dynamic Plane), Select Point
 * (3146 / 3150 / 3154) and typed origins (UpdateOrigin -> GetPlanePtrFromOpWithNewOrigin creates planes), the origin
 * display relative to the WCS (PlanesFollowRules::GetDisplayStatusRelToWcs; the "*Displayed relative to WCS" static 3158 is
 * shown only then - left out), PlanesWorldCoords, the mill-turn axis-substitution greying, the Plane Selection list's
 * columns (PlanesListCtrl: one column of names here), its "Select plane icon" / "Set new origin" / Help, the Update Plane
 * dialog's choices 0 / 1 (they change the plane: the planes archive is not written) and its "Don't show this warning
 * again" (registry), Automatic work offsets other than G54 (BuildWorkOffsetMap), the Cplane's use for wireframe creation,
 * arcs not lying in the Tplane, posting an op whose Tplane differs from its WCS (rotary / multi-axis output: the LinuxCNC
 * post is 3-axis), Contour's set_ctour_type after a plane change (UpdateLocalOp). */
#include "core_ui.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ---- planes made here (the Bounding Box panel's Create Tool Plane, cui_bbox.c; spec bbox_toolplane_spec.md B): Plane(Matrix33,
   P3D, PlaneType 1) + MastercamPlanes::Add(plane, false, true) (mastercam.dll 0x1803d6590 / 0x1802ae620,
   decomp/mastercam_planeadd.c): id = GetNextID = GetHighID(2) + 1 (planes_add; the part's planes from its MastercamPlanes
   archive, formats/planes_archive.md), name = EnsureUniqueName (0x1802b4ea0): empty -> opmanres 174 "Plane", taken ->
   "%s-%ld" with n = 1, 2, ... (CorrectName / IsReservedName / IsStatusBarPlaneLabel NOT DECODED: the system plane names
   and the names here count as taken), work offset -1 (Plane::Init). Kept for the session and in the project file (UPLANE
   lines, ours); a part save writes them only through the op_views that use them (the planes archive writer is NOT DECODED;
   Mastercam's reader then finds / makes the plane by matrix + origin, FUN_1802cfb60). Active planes (SetActivePlane
   0x1802c9cc0: only the given type changes; PlaneType 0 Cplane, 1 Tplane, 2 Gview, 3 WCS): the Tplane / WCS of new
   operations (InitOperationViewsToActivePlanes, decomp/mastercam_opviews_init.c). The Cplane stays Top here. Undo: no
   undo record - MastercamPlanes::Add (formats/bbox_toolplane.md B step 8: db_has_changed, SetEntityRefs, the recent-id
   array) records none and the panel's UndoTrans covers db entities only, so a plane made by the panel stays after Undo
   (as decoded; whether Mastercam's undo restores the planes some other way is NOT DECODED). */
UPlane uplanes[UPLANE_MAX]; int nuplanes;
int plane_active[4] = { 1, 1, 1, 1 };
int plane_active_org[4] = { 1, 1, 1, 1 };                 /* SystemPlanes +0x120: the plane whose origin is the active origin of type t (MastercamPlanes::GetActiveOrigin 0x1802c5830) */
static int part_act0[4], part_org0[4] = { 1, 1, 1, 1 };   /* the part's own active plane / origin ids as restored, for the project's ACTPLANE line (ours) */
static int name_taken(const char *n) {
    for (int i = 0; i < 7; i++) if (!strcmp(cui_sys_plane(i), n)) return 1;
    for (int i = 0; i < nuplanes; i++) if (!strcmp(uplanes[i].name, n)) return 1;
    return 0;
}
static const UPlane *uplane(int id) { for (int i = 0; i < nuplanes; i++) if (uplanes[i].id == id) return &uplanes[i]; return NULL; }
int planes_add(const char *name, const double m[9], const double org[3]) {
    if (nuplanes >= UPLANE_MAX) return 0;
    /* GetNextID(2) = GetHighID(2) + 1 (formats/planes_archive.md: GetHighID looks at the LAST plane in (class, id, GUID)
       order - its id when it is class 2 and above 0x33, else 0x33): the part's planes (mcam_load.c planes_archive) and the
       planes made here (class 2 too). An archive that could not be read counts as none (ours) */
    int hi = 0x33, fcls = F.planes_ok ? F.planes_maxcls : 0;
    if (fcls <= 2) {
        if (fcls == 2 && F.planes_hi2 > hi) hi = F.planes_hi2;
        for (int i = 0; i < nuplanes; i++) if (uplanes[i].id > hi) hi = uplanes[i].id;
    }
    if (hi + 1 >= 0x8000) return 0;                      /* GetNextID's search from 0x8000 on: NOT PORTED */
    /* Add: an id already used -> "unable to add plane", id 0 (formats/bbox_toolplane.md B steps 2 / 7) - what a second
       plane gets when the part's last plane is of a class above 2 (GetHighID(2) is then 0x33 every time) */
    if (uplane(hi + 1)) return 0;
    UPlane *u = &uplanes[nuplanes]; memset(u, 0, sizeof *u);
    const char *base = name && name[0] ? name : "Plane";   /* opmanres 174 */
    snprintf(u->name, sizeof u->name, "%s", base);
    for (long n = 1; name_taken(u->name) && n < 100000; n++) snprintf(u->name, sizeof u->name, "%s-%ld", base, n);
    u->id = hi + 1; memcpy(u->m, m, sizeof u->m); memcpy(u->org, org, sizeof u->org); u->wofs = -1;
    if (!cam_plane_register(u->id, u->m)) return 0;
    nuplanes++;
    return u->id;
}
void planes_clear_user(void) { nuplanes = 0; cam_plane_clear_user(); for (int t = 0; t < 4; t++) { plane_active[t] = 1; plane_active_org[t] = 1; part_act0[t] = 0; part_org0[t] = 1; } }
int planes_project_active_differs(void) {               /* the project's ACTPLANE line (ours): only when different from what opening the part restores */
    for (int t = 0; t < 4; t++) if (plane_active[t] != (part_act0[t] ? part_act0[t] : 1) || (t != 2 && plane_active_org[t] != part_org0[t])) return 1;
    return 0;
}
/* MastercamPlanes::SetActivePlane(PlaneType, Plane&) 0x1802c9be0 -> (.., SET_ACTIVE_ORIGIN = type != Gview) 0x1802c9cc0 (decomp/mastercam_planeadd.c): SystemPlanes::SetPlaneID
   (+0x110), then with SET_ACTIVE_ORIGIN 1 and a valid plane id SetActiveOrigin(type, the plane's id) (+0x120): making a plane active makes its origin the active origin
   (the Bounding Box's Set as current / WCS: SET_ACTIVE_ORIGIN 1, mccad_bcyl.c:196 / :199); the Gview has no origin (GetOriginID 0) */
void plane_set_active(int type, int id) {
    if (type < 0 || type > 3) return;
    plane_active[type] = id;
    if (type != 2 && id >= 1) plane_active_org[type] = id;
}
void plane_set_active_org(int type, int id) { if (type >= 0 && type <= 3 && type != 2) plane_active_org[type] = id >= 1 ? id : 1; }   /* the project's ACTPLANE line (ours) */
int plane_wofs(int id) { const UPlane *u = uplane(id); return u ? u->wofs : -1; }   /* Plane +0x330 (Plane::Init 0x1803daf20: -1) */
/* MastercamPlanes::GetActiveOrigin 0x1802c5830: GetOriginID(type) >= 1: that plane's origin (GetPlanePtrByID, the world origin when the id is not
   in the table; PlanePoint::GetPointInWorldCoordinates 0x1800a9270); < 1: the unnamed origin, SystemPlanes +0xe0 */
const double *plane_active_origin(int type) {
    static const double z[3] = { 0, 0, 0 };
    if (type < 0 || type > 3 || type == 2) return z;      /* GetOriginID returns 0 for the Gview (0x1804338b0, param_1 != 2) */
    if (plane_active_org[type] >= 1) return plane_origin(plane_active_org[type]);
    return F.planes_ok ? F.planes_uo : z;
}
/* what File > Open restores (mccore get_db 0x1802b1710 -> FUN_1802d5c80 reading the records in file order, then FUN_1802d1b60):
   1. dbinit / init_db 0x18033ae30: the active planes back to Top (planes_clear_user, doc_clear_ops), the construction depth to 0 only by File > New;
   2. the view record 0x200 sub 1 (store_db_ent 0x1802c5dd0): ConstrDepth::operator= +0xb0, the Gview (+0x6c, SetGrView of that view's matrix), the window centre +0xa0 and
      height +0xb8 (<= 0: 8.0 English, 200.0 metric), set_parms (planes_restore_view, the application's fit after load is replaced by it);
   3. MastercamPlanes record ASSOC 36 (ProcessAssocPlanes FUN_1802d0590): `Planes = filePlanes` (MastercamPlanes::operator= 0x1802acce0 -> Copy 0x1802b35e0: the archive's planes
      are added with their own ids, the active plane / origin ids copied, CopyActivePlanes 0x1802c51c0 = SystemPlanes::operator=);
   4. DbProcess::UpdateConflictingPlanes FUN_1802d6350 at the end (the archive read: only SetCGraphicsView 0x1802cb3e0 of the Gview plane, which does not move the view:
      FUN_1801d0d70 / FUN_1801d0420 record the plane; a file without the archive (+0x3c21 clear) is the view_ent route: NOT DECODED here, as today).
   Not restored here: the Previous planes (SystemPlanes +0x130 / +0x470, GetPreviousMatrix / GetPreviousOrigin: nothing here reads them); viewports (+0xd0 count, 1 in every corpus part). */
void planes_restore_part(void) {
    if (!F.planes_ok) return;                             /* MastercamPlanes schema other than 4 (2018 and later) or an archive that did not parse to its end: as before, Top (NOT DECODED) */
    for (int i = 0; i < F.nplanes_list; i++) {
        const McamPlaneInfo *q = &F.planes_list[i];
        if (q->cls != 2 || !q->is_plane || q->id <= 7 || nuplanes >= UPLANE_MAX) continue;   /* ids 1..7: the system planes (ReservedPlanes, cui_sys_plane; names and matrices equal in all 959 corpus parts) */
        if (!cam_plane_register(q->id, q->m)) continue;
        UPlane *u = &uplanes[nuplanes++]; memset(u, 0, sizeof *u);
        u->id = q->id; snprintf(u->name, sizeof u->name, "%s", q->name); memcpy(u->m, q->m, sizeof u->m); memcpy(u->org, q->org, sizeof u->org);
        u->wofs = q->wofs; u->disp = q->disp; u->coord = q->coord; u->part = 1;
    }
    /* SystemPlanes::GetPlaneID / GetOriginID: ids < 1 only for the Gview (174 corpus parts: an id of 0 = no plane is the Gview, the unnamed matrix);
       an id not in the table (none in the corpus) keeps plane_name's "UNDEFINED" and the Top matrix (GetActivePlane's unnamed fallback NOT PORTED) */
    for (int t = 0; t < 4; t++) { plane_active[t] = F.planes_act[t]; plane_active_org[t] = F.planes_org[t]; part_act0[t] = F.planes_act[t]; part_org0[t] = F.planes_org[t]; }
}
/* the Gview and the window of step 2 above, after the document is loaded and uploaded (view_fit is what this program does on open; Mastercam does not fit) */
void planes_restore_view(void) {
    if (!F.view_rec) return;
    constr_depth = F.constr_depth; zd_pick = 0;           /* ConstrDepth::operator= (store_db_ent 0x1802c5dd0) */
    ViewState v; view_get(&v);
    if (F.view_mat_ok) memcpy(v.m, F.view_mat, sizeof v.m);   /* SetGrView(mcViews[gv]) */
    v.cx = F.view_center[0]; v.cy = F.view_center[1];    /* put_wk_center */
    v.h = F.view_height > 0 ? F.view_height : (mcam_is_metric(&F) ? 200.0 : 8.0);   /* put_wk_height: 0x4069.. / 0x4020.. when <= 0 */
    view_put(&v);
    overlay_dirty = 1;
}
const double *plane_origin(int id) { static const double z[3] = { 0, 0, 0 }; const UPlane *u = uplane(id); return u ? u->org : z; }
/* InitOperation's views from the active planes (decomp/mastercam_opviews_init.c: Plane::Convert of the active WCS / Tplane /
   Cplane into the new op; their work offsets -1): a no-op while all are Top */
void planes_init_op(CamParams *p) {
    int t = plane_active[1], w = plane_active[3], c = plane_active[0];
    if (t == 1 && w == 1 && c == 1) return;
    p->tplane = t == 1 ? 0 : t; memcpy(p->torg, plane_active_origin(1), sizeof p->torg);
    p->wcs = w == 1 ? 0 : w; memcpy(p->worg, plane_active_origin(3), sizeof p->worg);
    p->cplane = c == 1 ? 0 : c; memcpy(p->corg, plane_active_origin(0), sizeof p->corg);
}
/* the same on an op copied from another one (the Tab Cutoff op: InitOperation copies list_op_get_prev's op, then
   InitOperationViewsToActivePlanes, mccore_ops.c l.1773; cui_ops.c cutoff_source): the active planes whatever the copy held -
   planes_init_op's "no-op while all are Top" holds only for an op that starts at Top */
void planes_set_active(CamParams *p) {
    int t = plane_active[1], w = plane_active[3], c = plane_active[0];
    p->tplane = t == 1 ? 0 : t; memcpy(p->torg, plane_active_origin(1), sizeof p->torg);
    p->wcs = w == 1 ? 0 : w; memcpy(p->worg, plane_active_origin(3), sizeof p->worg);
    p->cplane = c == 1 ? 0 : c; memcpy(p->corg, plane_active_origin(0), sizeof p->corg);
}
/* the Planes manager's rows (PlanesMgrBar FUN_18040b9d0 / SetPlanesListIcons FUN_180412640): the planes of class 2 (MastercamPlanes::GetPlanes order: (class, id, GUID)), the +0x120
   hidden ones are not stored; columns opmanres 1948 Name, 59399 G, 3058 WCS, 126 C, 127 T, 1949 Display, 1947 Offset, 1998 Coordinate: the G / WCS / C / T cells show the column's
   header text in the row of the active plane of that type (MastercamPlanes::GetActivePlaneID), Display opmanres 120 "X" when +0x334 != 0 (Plane::Init 1), Offset "%d" of +0x330 when >= 0,
   Coordinate +0x348: 0 "Mill" 142, 1 "Lathe X" 153, 2 "Lathe D" 187 (m_ConstructorCoordinateDisplay of a plane made here: NOT DECODED, empty). Column G header + "*" when no row
   is the Gview (HDM_SETITEM). Not ported: the icon column (SetPlanesListIcons' image, Displayed / Locked), the +0xbbc8 / +0xbbcb filters, the second list */
static int pmgr_row_ids(int *out, int max) {
    int n = 0; for (int id = 1; id <= 7; id++) if (n < max) out[n++] = id;
    int vals[UPLANE_MAX], m = 0; for (int i = 0; i < nuplanes && m < UPLANE_MAX; i++) vals[m++] = uplanes[i].id;
    for (int i = 1; i < m; i++) { int k = vals[i], j = i - 1; while (j >= 0 && vals[j] > k) { vals[j + 1] = vals[j]; j--; } vals[j + 1] = k; }
    for (int i = 0; i < m && n < max; i++) out[n++] = vals[i];
    return n;
}
int planes_mgr_rows(void) { int a[UPLANE_MAX + 7]; return pmgr_row_ids(a, UPLANE_MAX + 7); }
const char *planes_mgr_g_header(void) {
    int a[UPLANE_MAX + 7], n = pmgr_row_ids(a, UPLANE_MAX + 7);
    for (int i = 0; i < n; i++) if (a[i] == plane_active[2]) return "G";
    return "G*";                                         /* FUN_180412640 l. "pcVar17[0] = '*'" */
}
static const McamPlaneInfo *part_plane(int id) {
    if (!F.planes_ok) return NULL;
    for (int i = 0; i < F.nplanes_list; i++) if (F.planes_list[i].cls == 2 && F.planes_list[i].id == id) return &F.planes_list[i];
    return NULL;
}
/* cells[8]: pointers into buf */
void planes_mgr_row(int i, char *cells[8], char *buf, int bufn) {
    int a[UPLANE_MAX + 7], n = pmgr_row_ids(a, UPLANE_MAX + 7), id = i >= 0 && i < n ? a[i] : 0, off = 0;
    const McamPlaneInfo *q = part_plane(id); const UPlane *u = uplane(id);
    int disp = u ? u->disp : q ? q->disp : 1, wofs = u ? u->wofs : q ? q->wofs : -1, coord = u ? u->coord : q ? q->coord : -1;
    static const char *CD[3] = { "Mill", "Lathe X", "Lathe D" };   /* opmanres 142 / 153 / 187 */
#define CELL(k, ...) do { cells[k] = buf + off; off += snprintf(buf + off, off < bufn ? bufn - off : 0, __VA_ARGS__) + 1; if (off > bufn) off = bufn; } while (0)
    CELL(0, "%s", plane_name(id));
    CELL(1, "%s", id == plane_active[2] ? "G" : "");
    CELL(2, "%s", id == plane_active[3] ? "WCS" : "");
    CELL(3, "%s", id == plane_active[0] ? "C" : "");
    CELL(4, "%s", id == plane_active[1] ? "T" : "");
    CELL(5, "%s", disp ? "X" : "");
    if (wofs >= 0) CELL(6, "%d", wofs); else CELL(6, "%s", "");
    CELL(7, "%s", coord >= 0 && coord < 3 ? CD[coord] : "");
#undef CELL
}
const char *plane_name(int id) {   /* GetPlaneName; mccoreres 4974 when not found */
    if (id <= 0) id = 1;
    if (id <= 7) return cui_sys_plane(id - 1);
    const UPlane *u = uplane(id); return u ? u->name : "UNDEFINED";
}
/* wpt_to_op / op_to_wpt, the system planes' matrices and the Tplane generation: cam2d.c (cam_plane_*, cam_generate) */
void plane_w2v(int id, const double org[3], const double w[3], double v[3]) { cam_plane_w2v(id, org, w, v); }
void plane_v2w(int id, const double org[3], const double v[3], double w[3]) { cam_plane_v2w(id, org, v, w); }
/* the same plane (matrix and origin): the 3-axis post can output the op (nc_post.c op_postable, ours) */
int op_tplane_is_wcs(const CamParams *p) {
    int t = p->tplane <= 0 ? 1 : p->tplane, w = p->wcs <= 0 ? 1 : p->wcs;
    return t == w && !memcmp(p->torg, p->worg, sizeof p->torg);
}
/* the chain depth in the op frame (get_chain_depth_op -> wpt_to_op of the first entity / point) */
double op_chain_depth(const CamParams *p, const Chain *ch, int nch, const Hole *h, int nh) {
    double w[3] = { 0, 0, 0 }, v[3];
    if (nch && ch[0].n) { const Ent *e0 = &F.ents[ch[0].ent[0]]; const V3 *q = e0->type == E_ARC ? &e0->c : &e0->a; w[0] = q->x; w[1] = q->y; w[2] = q->z; }
    else if (nh) { w[0] = h[0].x; w[1] = h[0].y; }
    else return 0;
    plane_w2v(p->tplane, p->torg, w, v);
    return v[2];
}
/* the op's work offset as stored (Tplane op_view +0x92): MapToOp - the value when Manual and >= 0, else -1 */
int op_wofs(const CamParams *p) { return p->wofs_manual && p->wofs >= 0 ? p->wofs : -1; }
static int pid(int id) { return id <= 0 ? 1 : id; }
/* PlanesPageBase::MapToOp (0x1805c5ab0, at the op dialog's OK through UpdateLocalOp): Tplane +0x92 = the page's value when
   Manual and >= 0, else -1; the WCS (+0x13afa) and the Cplane (+0x13a4a) +0x92 take it when their plane id is the Tplane's */
void planes_map_to_op(CamParams *p) {
    int t = op_wofs(p);
    p->wofs = t; p->wofs_manual = t >= 0;
    if (pid(p->wcs) == pid(p->tplane)) p->wwofs = t;
    if (pid(p->cplane) == pid(p->tplane)) p->cwofs = t;
}
/* RefreshData (0x1805c69e0) / SetToolPlane (0x1805c79e0): the page's value = the Tplane's +0x92 (< -1 -> -1), Manual =
   value >= 0 (the page shown, a plane selected / copied) */
void planes_refresh(CamParams *p) { if (p->wofs < -1) p->wofs = -1; p->wofs_manual = p->wofs >= 0; }

/* ---- the page model on the pending op */
int pls_open, pls_which = -1, pls_sel = -1;           /* Plane Selection open for WCS 0 / Tplane 1 / Cplane 2; the row */
int upl_open, upl_choice; char upl_text[240];         /* the Update Plane dialog 10269 */
static int *plane_slot(CamParams *p, int which) { return which == 0 ? &p->wcs : which == 1 ? &p->tplane : &p->cplane; }
static double *org_slot(CamParams *p, int which) { return which == 0 ? p->worg : which == 1 ? p->torg : p->corg; }
static int *wofs_slot(CamParams *p, int which) { return which == 0 ? &p->wwofs : which == 1 ? &p->wofs : &p->cwofs; }   /* op_view +0x92 */
/* the Tplane moved: the chain depth (incremental heights are relative to it: get_chain_depth_op, mccore set_depths) follows */
static void tplane_changed(double old_cz) {
    double cz = op_chain_depth(&pending.p, pending.chains, pending.nchains, pending.holes, pending.nholes), d = cz - old_cz;
    CamParams *p = &pending.p;
    if (pp_hf.top_inc) { p->top_z += d; if (p->type != CAM_DRILL && p->retract_z > -1e29 && pp_hf.ret_inc) p->retract_z += d;
                         if (pp_hf.clr_inc) { p->clearance += d; pp_hf.clr_abs += d; } if (p->type == CAM_DRILL && pp_hf.fp_inc) p->r_plane += d; }
    if (pp_hf.dep_inc) p->final_z += d;
    pp_hf.cz = cz;
}
/* CheckWorkOffset (0x1805c4240) for a new value v (old = the value before): > 0x7fff -> -1, Manual = v >= 0; a value
   other than the Tplane plane's work offset (-1 for the system planes) opens the Update Plane dialog (DoModalWorkOffset
   0x1808bc1c0 shows it when param_2 != the plane's +0x330 = this +0x1898, and the "Work Offset Warning" registry value is
   not set); the op keeps v in every branch of that dialog (OK with any choice, Cancel: the op's +0x92 either holds v from
   choice 2 or is unchanged, and CheckWorkOffset keeps v in both cases). The message: mccoreres 375 with the plane name;
   when MastercamPlanes::WorkOffsetInUse is true mccoreres 5605 + ... + 5190 are appended only if 5605 is not empty, and
   5605 is an empty string in this mccoreres.dll - so nothing is appended (WorkOffsetInUse itself not needed) */
static void check_wofs(CamParams *p, int old, int v) {
    if (v == old) return;
    if (v > 0x7fff) v = -1;
    p->wofs = v; p->wofs_manual = v >= 0;
    if (v != plane_wofs(p->tplane)) {                     /* CheckWorkOffset: the Tplane plane's +0x330 (DoModalWorkOffset 0x1808bc1c0: param_2 != this + 0x1898) */
        upl_open = 1; upl_choice = 0;                    /* InitMemberVariables / ReadRegistrySettings: "Update Plane" 0 */
        const char *n = plane_name(p->tplane);
        snprintf(upl_text, sizeof upl_text, "Changing your work offset value will update plane %s and any operations that use %s.", n, n);   /* mccoreres 375 */
    }
}
/* EnableDisableWorkOffset (0x1805c47f0) with the page value `before` (sVar2): Automatic -> -1; Manual -> the Tplane
   plane's work offset (-1), then the defaults library op's (default_ops_get, op +0x602 = record 0x66a) when not -1;
   CheckWorkOffset(before) when the value changed */
static void enable_disable_wofs(CamParams *p, int before) {
    int v = -1;
    if (p->wofs_manual) {
        v = plane_wofs(p->tplane);                       /* EnableDisableWorkOffset 0x1805c47f0: the Tplane plane's +0x330 (GetPlanePtrByID(+0x13970)) */
        if (v == -1) { uint32_t n = 0; const uint8_t *r = cam_default_op_record(p->type, mcam_is_metric(&F), &n);
            if (r && n >= 0x66c) { int16_t d; memcpy(&d, r + 0x66a, 2); if (d != -1) v = d; } }
    }
    p->wofs = before;
    if (v != before) check_wofs(p, before, v);
}
/* ResyncOpViews (0x1805c73b0) after op_view `which` changed: every other op_view with the same plane id becomes a copy of
   it (id, matrix, origin, +0x92); then the Tplane's +0x92 = the page value when the page was Manual with a value >= 0
   (bVar3 / sVar8, taken before the copies) */
static void resync_views(CamParams *p, int which, int man, int pg) {
    for (int o = 0; o < 3; o++) if (o != which && pid(*plane_slot(p, o)) == pid(*plane_slot(p, which))) {
        *plane_slot(p, o) = *plane_slot(p, which); memcpy(org_slot(p, o), org_slot(p, which), 3 * sizeof(double));
        *wofs_slot(p, o) = *wofs_slot(p, which);
    }
    if (man) p->wofs = pg;
}
/* OnSelViewPb (0x1805c6010) for op_view `which` with system plane `id`: Plane::Convert (id, matrix, origin, +0x92 = the
   plane's -1), ResyncOpViews, SetToolPlane(the Tplane op_view) = RefreshData's Manual rule; for the Tplane then the page
   value = the plane's work offset (-1), Manual off, EnableDisableWorkOffset */
static void sel_view(int which, int id, const double org[3]) {
    CamParams *p = &pending.p; double cz0 = pp_hf.cz;
    int pg = p->wofs, man = p->wofs_manual && pg >= 0;
    *plane_slot(p, which) = id; memcpy(org_slot(p, which), org, 3 * sizeof(double)); *wofs_slot(p, which) = plane_wofs(id);   /* Plane::Convert: +0x92 = the plane's +0x330 */
    resync_views(p, which, man, pg);
    planes_refresh(p);                                   /* SetToolPlane(this + 0x13908) */
    if (which == 1) { p->wofs = plane_wofs(id); if (p->wofs < -1) p->wofs = -1; p->wofs_manual = p->wofs >= 0; enable_disable_wofs(p, p->wofs); tplane_changed(cz0); }   /* OnSelViewPb 0x1805c6010: +0x15da2 = the plane's +0x330, +0x15da0 = (value < 0x8000) */
}
void pls_begin(int which) {                            /* which 3 = the Stock Setup's stock plane (FUN_1801f1d70, cui_stock.c); 4 = the
                                                          Plane Selection dialog's Named (get_plane_info: SelectPlaneFromList(0, ..), nothing preselected) */
    pls_open = 1; pls_which = which;
    int id = which == 4 ? 0 : which == 3 ? pid(F.stock_rec ? F.stock_vid : 1) : pid(*plane_slot(&pending.p, which));
    pls_sel = id - 1;
    if (id > 7) for (int i = 0; i < nuplanes; i++) if (uplanes[i].id == id) pls_sel = 7 + i;
}
/* MastercamPlanes::GetPlanes: the seven system planes, then the planes made here (not for the stock plane: ours) */
int pls_rows(void) { return pls_which == 3 ? 7 : 7 + nuplanes; }
static int pls_row_id(int i) { return i < 7 ? i + 1 : i - 7 < nuplanes ? uplanes[i - 7].id : 0; }
const char *pls_row(int i) { return plane_name(pls_row_id(i)); }
void pls_ok(void) {
    if (pls_open && pls_which == 4) { int id = pls_sel >= 0 && pls_sel < pls_rows() ? pls_row_id(pls_sel) : 0; pls_open = 0; pls_which = -1; psel_named_answer(id); return; }
    if (pls_open && pls_which == 3) { int id = pls_sel >= 0 && pls_sel < 7 ? pls_sel + 1 : 0; pls_open = 0; pls_which = -1; if (id) st_plane_selected(id); else st_plane_cancel(); return; }
    if (pls_open && pls_sel >= 0 && pls_sel < pls_rows()) {
        CamParams before = pending.p; OpHFlags hb = pp_hf; int id = pls_row_id(pls_sel);
        sel_view(pls_which, id, plane_origin(id));         /* the system planes' origin: the world origin */
        pp_note_change(&before, &hb);
    }
    pls_open = 0; pls_which = -1;
}
void pls_cancel(void) { int st = pls_which == 3, ps = pls_which == 4; pls_open = 0; pls_which = -1; if (st) st_plane_cancel(); if (ps) psel_named_answer(0); }  /* SelectPlaneFromList returns 0: RegenAndGet(0) is invalid, no change */
/* the copy buttons: OnSelViewPb(destination, the source's plane); into the Tplane (3289 OnXferWCSToTPlanePb 0x1805c6700,
   3292 OnXferCPlaneToTPlanePb 0x1805c64d0) then SetToolPlane(the source op_view as it is after that) = its +0x92 becomes
   the Tplane's work offset (no CheckWorkOffset) */
void planes_copy(int id) {
    CamParams *p = &pending.p, before = *p; OpHFlags hb = pp_hf;
    if (id < 3289 || id > 3292) return;
    int from = id == 3289 ? 0 : id == 3290 ? 1 : id == 3291 ? 1 : 2, to = id == 3289 ? 1 : id == 3290 ? 0 : id == 3291 ? 2 : 1;
    int src_id = pid(*plane_slot(p, from)); double org[3]; memcpy(org, org_slot(p, from), sizeof org);
    sel_view(to, src_id, org);
    if (to == 1) { p->wofs = *wofs_slot(p, from); planes_refresh(p); }
    pp_note_change(&before, &hb);
}
/* the Work Offset radios / edit: manual 1 / 0 = OnWorkOffsetManual (only when the radio state changes), v =
   OnKillfocusWorkOffsetEdit with the text v (atoi, < -1 -> -1, CheckWorkOffset; changed -> SetToolPlane) */
void planes_wofs(int manual, int v) {
    CamParams *p = &pending.p, before = *p; OpHFlags hb = pp_hf;
    if (manual >= 0) { if ((manual != 0) != (p->wofs_manual != 0)) { int b = p->wofs; p->wofs_manual = manual != 0; enable_disable_wofs(p, b); } }
    else { int old = p->wofs; if (v < -1) v = -1; check_wofs(p, old, v); if (p->wofs != old) planes_refresh(p); }
    pp_note_change(&before, &hb);
}
/* Update Plane OK: choices 0 (ChangeWorkOffsetInPlane / MastercamPlanes::Set, the WCS / Cplane +0x92) and 1 (a new plane)
   change the planes archive - NOT PORTED, so OK is enabled only with choice 2 (ours); Cancel: the op keeps the value */
int upl_ok_enabled(void) { return upl_choice == 2; }
void upl_close(int ok) { if (ok && !upl_ok_enabled()) return; upl_open = 0; }

/* Plane Selection 842 (242 x 160 du) and Update Plane 10269 (230 x 137 du): template controls (draw order) */
const TpCtl PLS842[PLS842_N] = {
    { TPK_LIST, 1457, 8, 20, 225, 110, "", 0 },                       /* PlanesListCtrl (columns NOT DECODED: names) */
    { TPK_BTN, 546, 8, 135, 24, 19, "Select plane icon", -1 },       /* NOT PORTED */
    { TPK_CHECK, 630, 46, 135, 66, 10, "Set new origin", -1 },       /* NOT PORTED */
    { TPK_BTN, 1, 121, 135, 36, 18, "OK", 0 },
    { TPK_BTN, 2, 159, 135, 36, 18, "Cancel", 0 },
    { TPK_BTN, 9, 197, 135, 36, 18, "Help", -1 },
};
const TpCtl UPL10269[UPL10269_N] = {
    { TPK_LABEL, 10280, 12, 8, 210, 32, "", 0 },                      /* upl_text */
    { TPK_RADIO, 10281, 16, 52, 204, 10, "Update the plane and all operations that use this plane", -1 },   /* choice 0: NOT PORTED */
    { TPK_RADIO, 10282, 16, 66, 204, 10, "Create a copy of the plane and update just this operation", -1 }, /* 1: NOT PORTED */
    { TPK_RADIO, 10283, 16, 80, 204, 10, "Update just this operation without changing the plane", 0 },      /* 2 */
    { TPK_CHECK, 10177, 16, 98, 136, 10, "Don't show this warning again", -1 },   /* registry "Work Offset Warning": NOT PORTED */
    { TPK_BTN, 1, 110, 112, 36, 18, "OK", 0 },
    { TPK_BTN, 2, 148, 112, 36, 18, "Cancel", 0 },
    { TPK_BTN, 9, 186, 112, 36, 18, "Help", -1 },
};

/* the page's rows (PpOps: the front ends draw them as the other row-laid pages) */
void planes_page_body(const PpOps *ops, void *u) {
    CamParams *p = &pending.p;
    static const char *grp[3] = { "Working coordinate system", "Tool plane", "Comp / construction plane" };   /* 6547 group boxes */
    static const int which_of[3] = { 0, 1, 2 };
    for (int g = 0; g < 3; g++) {
        int w = which_of[g], id = pid(*plane_slot(p, w)); const double *o = org_slot(p, w); char t[160]; double v[3], z[3] = { 0, 0, 0 };
        plane_w2v(id, z, o, v);                            /* RefreshData: world_to_view(origin, view) */
        ops->row(u, 22, 0, 1); ops->label(u, grp[g], 0);
        ops->row(u, 22, 0, 1); ops->label(u, plane_name(id), 0);                          /* 3143 / 3147 / 969 */
        ops->row(u, 22, 0, 1); ops->label(u, "Origin", 0);
        ops->row(u, 22, 0, 3);
        for (int k = 0; k < 3; k++) { snprintf(t, sizeof t, "%c  %.4f", "XYZ"[k], v[k]); ops->label(u, t, 0); }   /* MCReal edits: NOT PORTED */
        ops->row(u, 22, 0, 2);
        if (ops->button(u, "Select Plane...", 1)) pls_begin(w);                          /* 3145 / 3149 / 3153 */
        (void)ops->button(u, "Select Point", 0);                                          /* 3146 / 3150 / 3154: NOT PORTED */
        if (g == 1) {                                                                     /* Work Offset (3155 / 3166 / 3157) */
            ops->row(u, 22, 0, 1); ops->label(u, "Work Offset", 0);
            ops->row(u, 22, 0, 2);
            if (ops->radio(u, "Manual", p->wofs_manual != 0)) planes_wofs(1, 0);
            if (ops->radio(u, "Automatic", !p->wofs_manual)) planes_wofs(0, 0);
            if (p->wofs_manual) { ops->row(u, 22, 0, 1); int n = ops->iprop(u, "#Work offset", p->wofs, -1, 0x7fff); if (n != p->wofs) planes_wofs(-1, n); }
        }
        if (g < 2) {                                                                      /* 3289 / 3290, 3291 / 3292 */
            ops->row(u, 22, 0, 2);
            if (ops->button(u, "->", 1)) planes_copy(g == 0 ? 3289 : 3291);
            if (ops->button(u, "<-", 1)) planes_copy(g == 0 ? 3290 : 3292);
        }
    }
}
void planes_dump(void) {
    const CamParams *p = &pending.p;
    for (int i = 0; i < nuplanes; i++) printf("    uplane %d '%s' org %.4f %.4f %.4f m %g %g %g %g %g %g %g %g %g\n", uplanes[i].id, uplanes[i].name, uplanes[i].org[0], uplanes[i].org[1], uplanes[i].org[2],
                                               uplanes[i].m[0], uplanes[i].m[1], uplanes[i].m[2], uplanes[i].m[3], uplanes[i].m[4], uplanes[i].m[5], uplanes[i].m[6], uplanes[i].m[7], uplanes[i].m[8]);
    printf("    active planes c %d t %d g %d w %d\n", plane_active[0], plane_active[1], plane_active[2], plane_active[3]);
    printf("    part planes read %d n %d maxclass %d hi2 %d\n", F.planes_ok, F.planes_n, F.planes_maxcls, F.planes_hi2);
    printf("    planes wcs %s (%g %g %g) tplane %s (%g %g %g) cplane %s (%g %g %g) wofs %s %d (op %d) views w %d c %d sel %d/%d upl %d/%d%s \"%s\"\n",
           plane_name(p->wcs), p->worg[0], p->worg[1], p->worg[2], plane_name(p->tplane), p->torg[0], p->torg[1], p->torg[2],
           plane_name(p->cplane), p->corg[0], p->corg[1], p->corg[2], p->wofs_manual ? "manual" : "automatic", p->wofs, op_wofs(p),
           p->wwofs, p->cwofs, pls_open ? pls_which : -1, pls_sel, upl_open, upl_choice, upl_ok_enabled() ? "" : " (OK grey)", upl_open ? upl_text : "");
}
/* script steps (both apps): plsel:W (Select Plane for WCS 0 / Tplane 1 / Cplane 2), plrow:I, plok, plcancel, plcopy:ID,
   plwofs:manual|auto|N, uplchoice:K, uplok, uplcancel, pldump */
int cui_planes_script(const char *st, const char *arg) {
#define IS(x) (!strcmp(st, x))
    if (IS("plsel") && arg) { pls_begin(atoi(arg)); return 1; }
    if (IS("plrow") && arg) { pls_sel = atoi(arg); return 1; }
    if (IS("plok")) { pls_ok(); return 1; }
    if (IS("plcancel")) { pls_cancel(); return 1; }
    if (IS("plcopy") && arg) { planes_copy(atoi(arg)); return 1; }
    if (IS("plwofs") && arg) { if (!strcmp(arg, "manual")) planes_wofs(1, 0); else if (!strcmp(arg, "auto")) planes_wofs(0, 0); else planes_wofs(-1, atoi(arg)); return 1; }
    if (IS("uplchoice") && arg) { int k = atoi(arg); if (upl_open && k >= 0 && k < 3 && UPL10269[1 + k].ref == 0) upl_choice = k; return 1; }
    if (IS("uplok")) { upl_close(1); return 1; }
    if (IS("uplcancel")) { upl_close(0); return 1; }
    if (IS("pldump")) { planes_dump(); return 1; }
    if (IS("plpartdump")) {   /* ours (tests): what opening a part restored: tools/tests/planes_open_check.py */
        printf("    plrestore rec %d gv %d mat %d constr %.17g act %d %d %d %d org %d %d %d %d nup %d\n", F.view_rec, F.view_gv, F.view_mat_ok, constr_depth,
               plane_active[0], plane_active[1], plane_active[2], plane_active[3], plane_active_org[0], plane_active_org[1], plane_active_org[2], plane_active_org[3], nuplanes);
        ViewState v; view_get(&v);
        printf("    plview m %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g c %.12g %.12g h %.12g\n", v.m[0], v.m[1], v.m[2], v.m[3], v.m[4], v.m[5], v.m[6], v.m[7], v.m[8], v.cx, v.cy, v.h);
        int t[4] = { 0, 1, 3, -1 };
        for (int k = 0; k < 3; k++) { const double *o = plane_active_origin(t[k]); printf("    plorg %d %.12g %.12g %.12g\n", t[k], o[0], o[1], o[2]); }
        { char buf[512], *c[8]; int n = planes_mgr_rows();
          printf("    plmgr %d %s\n", n, planes_mgr_g_header());
          for (int i = 0; i < n; i++) { planes_mgr_row(i, c, buf, sizeof buf); printf("    plrow %d|%s|%s|%s|%s|%s|%s|%s|%s\n", i, c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]); } }
        return 1; }
    return 0;
#undef IS
}
