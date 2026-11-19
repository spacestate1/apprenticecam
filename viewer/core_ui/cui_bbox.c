/* cui_bbox.c - the Bounding Box function panel (Wireframe > Bounding Box, and Stock Setup's "Bounding box" button), shared
 * by both front ends.
 *
 * Decoded 2026-10-22 (spec: formats/wireframe.md "## Bounding Box"; decomp/mccad_bbox_stock.c, mccad_bbox_panel.c,
 * mccad_bbox_manager*.c, mccad_bcyl*.c, mccad_points_extra*.c): mccad.dll create_bbox @180349300 -> FUN_180347380 and
 * create_boundingshape_either_method @18005ecb0 both run the driver FUN_180343ae0 (class BoundingBoxApp) over the native
 * model BoundingBoxManager (vtable 0x1804441e8, ctor FUN_1801598c0) behind the WPF view mccad.ui
 * boundingbox/boundingboxmanagerview.baml (MCCAD.Interop.BoundingBoxManagerViewModel; tooltips
 * MCCAD.UI.BoundingBox.BoundingBoxManagerViewToolTips.resx).
 *
 * Ported: the settings struct and its statics (DAT_18064fc20 wireframe, DAT_180649fc0 stock; modal path, copied back on OK),
 * the driver's enable rules, the entity step (Manual = select_ents mask 0x1905ff, prompt mccadres 81; All shown =
 * set_clr_sel_bits(4, 0, 4, 1, 0x1905ff)), the compute FUN_180346eb0 (recompute / resize FUN_180346490, 2D construction
 * FUN_180347840 / FUN_180347960, sizes := extents FUN_180345f80, Volume FUN_180348590, Solids rule), the panel codes 0 OK,
 * 1 Apply, 2 Cancel, 4 / 5 / 7 / 8 (FUN_18015cb60 + the ctor lambdas), the output FUN_18005a2e0 (rect) / FUN_18005b220
 * (cylinder), the stock writes FUN_1803481e0 / FUN_1803483b0 and the stock-mode result for the page (cui_stock.c
 * st_bbox_panel_result). The points come from cui_stock.c's decoded rules (stock_bbox_sel = make_bounding_box /
 * add_to_minmax / CalculatePointsOnCurve, stock_bcyl_sel = FUN_18005bab0).
 *
 * Also ported (2026-10-22, specs formats/bbox_pushpull.md / formats/bbox_toolplane.md): the cylinder's end circles in the
 * find_view_k view (mastercam.dll 0x180337960: X -> 5 Right, Y -> 2 Front, Z -> 1 Top) as view arcs (Ent.vw); the live solid
 * (FUN_180345b10 / FUN_180345c50 / FUN_180347b00 / FUN_180347f70) and the Solids output (bit 8, sld_basic_primitive 0xf /
 * 0x11 -> a Block / Cylinder feature, cui_feat.c prim_body); Push Pull (FUN_180344b30 arrow, FUN_180343900, FUN_1803450b0,
 * FUN_180345180 drag, FUN_180346a60 commit, code 6 = a cancelled drag); Create Tool Plane (FUN_180345fd0 / FUN_180346160:
 * origin by RectangularOrigin / CylindricalOrigin, the Cplane's matrix, Plane(Matrix33, P3D, Tplane), MastercamPlanes::Add
 * naming, SetActivePlane Tplane / WCS -> cui_planes.c).
 *
 * Greyed / NOT PORTED: the STL-file mode (p7), plane-change events (MCEvent 0x8000 ids 0x15 / 0xd / 10 / mask 0x75000), the
 * Help topic, the panel caption (NOT DECODED), a typed value on the push-pull arrow (SelArrowBase edit field NOT DECODED),
 * the machine group's group_view update of SetActivePlane, views made by find_view_k / GetViewNo for non-reserved matrices.
 * Deviations ("ours"): the live solid is this program's 2.5D body (drawn with the solids, not in the live colour); a cylinder
 * whose axis is not Z cannot be built by that kernel, so its Solids check is greyed and its preview stays the outline (the
 * Lines and arcs shapes FUN_18005a2e0 / FUN_18005b220 flag 2 in the live colour, as for invalid shapes); the face pick is a
 * ray test on the box / cylinder and the side face's mid point is taken in the reference direction; the arrow is drawn as a
 * line with a head (gafgr's GrArrow drawing NOT DECODED), its colour UtColor(0x80, 0, 0, 0xff) read as r, g, b; the Solids
 * feature is its own undo step; the Manual selection is this program's entity selection, ended by Enter (bb_enter), the
 * panel greyed meanwhile and the live solid deleted until it ends; solid bodies are not part of a Manual selection; after a
 * stock-mode run the machine group's stock is put back as it was at the start (Mastercam overwrites group +0x1228 from
 * create_bbox_stock_no_dlg's cache DAT_1806a1790), so only the page's own OK / Cancel decides what is stored. The active
 * Cplane is Top here: view coordinates = world. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"

#define BB_MASK 0x1905ff                     /* P,L,A,S,N,W,D,NB | SURF | CONS | SOLID | RELIEF | PMESH (FUN_1803470e0) */
#define BB_TOL  (mc_systol.tol)              /* tol_exref (system tolerance, config 1000: McSysTol) */

/* the settings struct (0x68 bytes, FUN_18005d4f0) as far as ported; spec table "Settings struct" */
typedef struct {
    int all, lines, pts, ctr, solids;        /* +0x00 AllEntities, +0x01..+0x04 create flags */
    double sx, sy, sz, r, h;                 /* +0x08..+0x28 */
    int rect, rorg, corg, axis, center;      /* +0x30, +0x34, +0x38, +0x3c (0 Z, 1 Y, 2 X), +0x40 */
    int incr, both, ctp, tpcur, tpwcs;       /* +0x41, +0x42, +0x43, +0x50, +0x51 */
    char name[64];                           /* +0x48 ToolPlaneName ("" = DAT_18040c670) */
} BBSet;
static BBSet bb_wf = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 9, 1, 2, 0, 1, 0, 0, 0, 0 };   /* DAT_18064fc20 raw .data */
static BBSet bb_st = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 9, 1, 2, 0, 1, 0, 0, 0, 0 };   /* DAT_180649fc0 (= DAT_180649ee0) raw .data */

static int bb_active, bb_stock, bb_pend_stock;
static int bb_phase;                          /* 1 = the Manual selection (select_ents) runs */
static unsigned char *bb_pick; static int bb_npick, bb_pick_all;   /* the entities with selection bit 4 (All shown: NULL = shown + solids) */
static double bb_box[6], bb_cyl[8];           /* box[6] xmin,xmax,ymin,ymax,zmin,zmax; cyl[8] r, len, c[3], dir[3] */
static int bb_ok, bb_have;                    /* the last compute's result; something to preview */
static GeoParams bb_seen;                     /* the panel values the last event was handled with */
static struct { double stock[3], org[3], lines[12][6], arc[2][3], arc_r; int sel, disp, shape, axis, center, dirty, arc_view, doc; } bb_snap;

#define G (&gps[G_BBOX])
int bb_on(void) { return bb_active; }
int bb_selecting(void) { return bb_active && bb_phase == 1; }
int bb_apply_enabled(void) { return bb_active && !bb_stock && !bb_phase; }   /* EnableApply: false in stock mode (and STL mode) */

static void bb_load(const BBSet *s) {
    GeoParams *g = G;
    g->bb_all = s->all; g->bb_lines = s->lines; g->bb_pts = s->pts; g->bb_ctr = s->ctr; g->bb_solids = s->solids;
    g->bb_sx = s->sx; g->bb_sy = s->sy; g->bb_sz = s->sz; g->bb_r = s->r; g->bb_h = s->h;
    g->bb_shape = s->rect ? 0 : 1;           /* VM Shape: Rectangular 0 (FUN_18015d2c0) */
    g->bb_rorg = s->rorg >= 0 && s->rorg <= 10 ? s->rorg : 9;   /* out of range -> 9 (FUN_18015c3d0 default) */
    g->bb_corg = s->corg; g->bb_axis = s->axis; g->bb_center = s->center;
    g->bb_incr = s->incr; g->bb_both = s->both; g->bb_ctp = s->ctp; g->bb_tpcur = s->tpcur; g->bb_tpwcs = s->tpwcs;
    snprintf(g->bb_tpname, sizeof g->bb_tpname, "%s", s->name);
}
static void bb_store(BBSet *s) {            /* FUN_18015c3d0 panel -> struct (negative sizes clamped to 0) */
    const GeoParams *g = G;
    s->all = g->bb_all; s->lines = g->bb_lines; s->pts = g->bb_pts; s->ctr = g->bb_ctr; s->solids = g->bb_solids;
    s->sx = fmax(g->bb_sx, 0); s->sy = fmax(g->bb_sy, 0); s->sz = fmax(g->bb_sz, 0); s->r = fmax(g->bb_r, 0); s->h = fmax(g->bb_h, 0);
    s->rect = g->bb_shape == 0; s->rorg = g->bb_rorg; s->corg = g->bb_corg; s->axis = g->bb_axis; s->center = g->bb_center;
    s->incr = g->bb_incr; s->both = g->bb_both; s->ctp = g->bb_ctp; s->tpcur = g->bb_tpcur; s->tpwcs = g->bb_tpwcs;
    snprintf(s->name, sizeof s->name, "%s", g->bb_tpname);
}
/* FUN_180345540: the output bits (bit 1 Points +2, bit 2 Lines and arcs +1, bit 4 Center point +3, bit 8 Solids +4) */
static int bb_flags(void) { const GeoParams *g = G; return (g->bb_pts ? 1 : 0) | (g->bb_lines ? 2 : 0) | (g->bb_ctr ? 4 : 0) | (g->bb_solids ? 8 : 0); }
/* the axis view: find_view_k(dir) of the fit's view = Top (code 0), Back (1, RotateRelative 3), Right (2, RotateRelative 5) */
static int bb_axis_view(int code) { return code == 1 ? 3 : code == 2 ? 5 : 1; }
/* find_view_k (mastercam.dll 0x180337960, decomp/mastercam_findviewk.c): the views 1.. in index order, d = dir . k (the view's
   z row); | |d| - 1 | <= 1e-12 returns that view at once (either sense), else the best one within 2e-7 after the loop; no match
   with create = 1 makes a new view (make_view_k + AddMCView) - NOT PORTED (only the reserved views 1..8 exist here; the
   bounding cylinder's axis is always a reserved view's z: X -> 5 Right, Y -> 2 Front, Z -> 1 Top). dflt = the caller's
   preloaded view (FUN_18005b220: GetActiveViewNo = Top here) */
static int bb_find_view_k(const double d[3], int dflt) {
    int best = 0; double bd = 2e-7;
    for (int v = 1; v <= 8; v++) {
        double M[9]; if (!mcam_std_view(v, M)) continue;
        double e = fabs(fabs(d[0] * M[6] + d[1] * M[7] + d[2] * M[8]) - 1);
        if (e <= 1e-12) return v;
        if (e < bd) { bd = e; best = v; }
    }
    return best ? best : dflt;
}
static void w2v(const double M[9], const double w[3], double v[3]) { for (int k = 0; k < 3; k++) v[k] = M[3 * k] * w[0] + M[3 * k + 1] * w[1] + M[3 * k + 2] * w[2]; }
static void v2w(const double M[9], const double v[3], double w[3]) { for (int k = 0; k < 3; k++) w[k] = M[k] * v[0] + M[3 + k] * v[1] + M[6 + k] * v[2]; }
static int span_ok(double a, double b) { return fabs(b - a) > BB_TOL; }     /* tol <= |d| && |d| != tol */
static void bb_compute(int recompute, int final);
static void bb_seen_sync(void);
static void bb_tool_plane(void);
static double bb_depth(void);
static void bb_resize_with(const GeoParams *g, double *b, double *cy);
static double bb_vol_of(void) {               /* FUN_180348590: rect |dx dy dz|, cylinder pi r^2 len */
    if (G->bb_shape == 0) return fabs(bb_box[2] - bb_box[3]) * fabs(bb_box[0] - bb_box[1]) * fabs(bb_box[4] - bb_box[5]);
    return bb_cyl[0] * M_PI * bb_cyl[0] * bb_cyl[1];
}
static void bb_volume(void) { G->bb_vol = bb_vol_of(); }

/* ---- the machine group's stock while the stock-mode panel runs */
static void bb_snap_take(void) {
    for (int k = 0; k < 3; k++) { bb_snap.stock[k] = F.stock[k]; bb_snap.org[k] = F.stock_org[k]; }
    memcpy(bb_snap.lines, F.stock_lines, sizeof bb_snap.lines); memcpy(bb_snap.arc, F.stock_arc, sizeof bb_snap.arc);
    bb_snap.arc_r = F.stock_arc_r; bb_snap.arc_view = F.stock_arc_view;
    bb_snap.sel = F.stock_sel; bb_snap.disp = F.stock_disp; bb_snap.shape = F.stock_shape; bb_snap.axis = F.stock_axis;
    bb_snap.center = F.stock_center; bb_snap.dirty = F.stock_dirty; bb_snap.doc = doc_dirty;
}
static void bb_snap_put(void) {              /* ours: the stock as it was at the start (see the file comment) */
    for (int k = 0; k < 3; k++) { F.stock[k] = bb_snap.stock[k]; F.stock_org[k] = bb_snap.org[k]; }
    memcpy(F.stock_lines, bb_snap.lines, sizeof bb_snap.lines); memcpy(F.stock_arc, bb_snap.arc, sizeof bb_snap.arc);
    F.stock_arc_r = bb_snap.arc_r; F.stock_arc_view = bb_snap.arc_view;
    F.stock_sel = (int16_t)bb_snap.sel; F.stock_disp = bb_snap.disp; F.stock_shape = bb_snap.shape; F.stock_axis = bb_snap.axis;
    F.stock_center = bb_snap.center; F.stock_dirty = bb_snap.dirty; doc_dirty = bb_snap.doc;
    overlay_dirty = 1;
}
/* FUN_1803481e0: Display +0x1260 = 1, shape +0x1264 = 0, size = the spans, origin = (xmin, ymin, zmin), corner +0x1690 = 8,
   stock_ok(1) (stock_calc_geom); the stock plane is the active plane (Top here) */
static void bb_stock_rect(void) {
    if (!bb_stock) return;
    F.stock_disp = 1; F.stock_shape = 0;
    F.stock[0] = bb_box[1] - bb_box[0]; F.stock[1] = bb_box[3] - bb_box[2]; F.stock[2] = bb_box[5] - bb_box[4];
    F.stock_org[0] = bb_box[0]; F.stock_org[1] = bb_box[2]; F.stock_org[2] = bb_box[4];
    F.stock_sel = 8;
    stock_calc_store(); overlay_dirty = 1;
}
/* FUN_1803483b0: shape 1, axis +0x126c (code 0 -> 2, 1 -> 1, 2 -> 0), centre +0x1270, +0x1230 = 2 r, +0x1238 = len,
   +0x1240 = c.z (cyl[4]), origin = world_to_view(c) (Top: c) */
static void bb_stock_cyl(void) {
    if (!bb_stock) return;
    const GeoParams *g = G;
    F.stock_disp = 1; F.stock_shape = 1;
    F.stock_axis = g->bb_axis == 0 ? 2 : g->bb_axis == 1 ? 1 : 0; F.stock_center = g->bb_center != 0;
    F.stock[0] = bb_cyl[0] * 2.0; F.stock[1] = bb_cyl[1]; F.stock[2] = bb_cyl[4];
    for (int k = 0; k < 3; k++) F.stock_org[k] = bb_cyl[2 + k];
    stock_calc_store(); overlay_dirty = 1;
}

/* ---- the output (final compute): entities with the main attributes, one undo step */
/* FUN_18005a2e0: the 8 corners P0 (xmin,ymin,zmin) P1 (xmax,ymin,zmin) P2 (xmin,ymax,zmin) P3 (xmax,ymax,zmin), P4..P7 the
   same at zmax; the points / lines / face centres in its order and conditions. edge() is used for the preview too */
static void bb_corners(const double *b, double P[8][3]) {
    for (int i = 0; i < 8; i++) { P[i][0] = b[(i & 1) ? 1 : 0]; P[i][1] = b[(i & 2) ? 3 : 2]; P[i][2] = b[(i & 4) ? 5 : 4]; }
}
static int bb_edges(const double *b, int E[12][2]) {       /* bit 2: the edges, zero-length ones skipped */
    int dx = span_ok(b[0], b[1]), dy = span_ok(b[2], b[3]), dz = span_ok(b[4], b[5]), n = 0;
#define ED(c, p, q) if (c) { E[n][0] = p; E[n][1] = q; n++; }
    if (dx) { ED(1, 0, 1); ED(dy, 2, 3); if (dz) { ED(1, 4, 5); ED(dy, 6, 7); } }
    if (dy) { ED(1, 0, 2); ED(dx, 1, 3); if (dz) { ED(1, 4, 6); ED(dx, 5, 7); } }
    if (dz) { ED(1, 0, 4); ED(dx, 1, 5); if (dy) { ED(1, 2, 6); ED(dx, 3, 7); } }
#undef ED
    return n;
}
static void bb_pt(const double p[3]) { V3 q = { p[0], p[1], p[2] }, z = { 0, 0, 0 }; geo_add(E_POINT, q, q, z, 0, 0, 0); }
static void bb_out_rect(int flags) {
    const double *b = bb_box; double P[8][3]; bb_corners(b, P);
    int dx = span_ok(b[0], b[1]), dy = span_ok(b[2], b[3]), dz = span_ok(b[4], b[5]);
    if (flags & 1) {                                     /* the corners, degenerate axes deduplicated */
        bb_pt(P[0]); if (dx) bb_pt(P[1]);
        if (dy) { bb_pt(P[2]); if (dx) bb_pt(P[3]); }
        if (dz) { bb_pt(P[4]); if (dx) bb_pt(P[5]); if (dy) { bb_pt(P[6]); if (dx) bb_pt(P[7]); } }
    }
    if (flags & 4) {                                     /* the centre (average of the 8 corners), then the face centres */
        double c[3] = { 0, 0, 0 }; for (int i = 0; i < 8; i++) for (int k = 0; k < 3; k++) c[k] += P[i][k];
        for (int k = 0; k < 3; k++) c[k] *= 0.125;
        bb_pt(c);
        static const int FC[6][4] = { { 2, 0, 4, 6 }, { 3, 1, 5, 7 }, { 1, 0, 4, 5 }, { 3, 2, 6, 7 }, { 5, 4, 6, 7 }, { 1, 0, 2, 3 } };
        int on[6] = { dy || dz, dx && (dy || dz), dx || dz, dy && (dx || dz), dz && (dx || dy), dz && (dx || dy) };   /* xmin, xmax, ymin, ymax, zmax, zmin */
        for (int f = 0; f < 6; f++) if (on[f]) {
            double q[3]; for (int k = 0; k < 3; k++) q[k] = (P[FC[f][0]][k] + P[FC[f][1]][k] + P[FC[f][2]][k] + P[FC[f][3]][k]) * 0.25;
            bb_pt(q);
        }
    }
    if (flags & 2) {
        int E[12][2], n = bb_edges(b, E);
        for (int i = 0; i < n; i++) { V3 a = { P[E[i][0]][0], P[E[i][0]][1], P[E[i][0]][2] }, c = { P[E[i][1]][0], P[E[i][1]][1], P[E[i][1]][2] }, z = { 0, 0, 0 };
            geo_add(E_LINE, a, c, z, 0, 0, 0); }
    }
    /* bit 8 (sld_basic_primitive 0xf, only when all three spans > tol): NOT PORTED (Solids greyed) */
}
/* FUN_18005b220(c, dir, r, len, flags): needs r >= tol; the top centre c2 = c moved by len along the axis view's k (the sign
   by dir . k), mid = (c + c2) / 2 */
static int bb_cyl_ends(double c2[3], double mid[3], double M[9]) {
    int view = bb_find_view_k(bb_cyl + 5, 1);
    mcam_std_view(view, M);
    double v[3]; w2v(M, bb_cyl + 2, v);
    double dot = M[6] * bb_cyl[5] + M[7] * bb_cyl[6] + M[8] * bb_cyl[7];
    v[2] += dot >= 0 ? bb_cyl[1] : -bb_cyl[1];
    v2w(M, v, c2);
    for (int k = 0; k < 3; k++) mid[k] = (c2[k] + bb_cyl[2 + k]) * 0.5;
    return view;
}
static void bb_out_cyl(int flags) {
    if (!(bb_cyl[0] >= BB_TOL)) return;
    double c2[3], mid[3], M[9]; int view = bb_cyl_ends(c2, mid, M);
    int top = bb_cyl[1] > BB_TOL;
    if (flags & 1) { bb_pt(bb_cyl + 2); if (top) bb_pt(c2); }
    if (flags & 4) bb_pt(mid);
    if (flags & 2) {          /* full circles (A_ID, start 0, sweep 2 pi) at both ends in the find_view_k view, centre =
                                 world_to_view(c, view) (a_3d +0x30 / +0x60), no lines; Z -> Top (an XY arc), X -> 5, Y -> 2 */
        double v[3]; w2v(M, bb_cyl + 2, v);
        mcam_new_arc_view(&F, view, M, (V3){ v[0], v[1], v[2] }, bb_cyl[0], 0, 2 * M_PI, geo_level, geo_color);
        if (top) { w2v(M, c2, v); mcam_new_arc_view(&F, view, M, (V3){ v[0], v[1], v[2] }, bb_cyl[0], 0, 2 * M_PI, geo_level, geo_color); }
        doc_dirty = 1;
    }
    /* bit 8 (sld_basic_primitive 0x11, only if len > tol): bb_out_solid */
}

/* ---- the live solid (FUN_180345b10 / FUN_180345c50, decomp/mccad_points_extra2.c / mccad_bcyl.c; spec
   bbox_pushpull_spec.md 1.x): with a valid shape (rect: all three spans >= tol, FUN_180345830; cylinder: r >= tol and len > tol,
   FUN_180345800 / FUN_18005b220 bit 8) the output runs with flags 8 only and its solid becomes the live solid; later changes
   update it in place (FUN_180347b00 / FUN_180347f70 -> sldModifyBBoxSolidPrim); an invalid shape deletes it (FUN_180347310)
   and the preview is the outline (flag 2). Ours: the solid is this program's body (cui_feat.c prim_body; only a Z-axis
   cylinder can be made - another axis keeps the outline preview), drawn with the part's solids (not in the live colour:
   per-body colours are not drawn here) and listed nowhere until OK / Apply keeps it. */
static Body bb_lsol; static int bb_lsol_on;
static int bb_en_solids;                      /* EnableSolidsCheckBox */
static int bb_solid_shape_ok(void) {          /* FUN_180345830 / FUN_180345800 (+ FUN_18005b220's len > tol for the solid) */
    if (G->bb_shape == 0) return fabs(bb_box[1] - bb_box[0]) >= BB_TOL && fabs(bb_box[3] - bb_box[2]) >= BB_TOL && fabs(bb_box[5] - bb_box[4]) >= BB_TOL;
    return bb_cyl[0] >= BB_TOL && bb_cyl[1] > BB_TOL;
}
static int bb_solid_ok(void) { return G->bb_shape == 0 || fabs(fabs(bb_cyl[7]) - 1) < 1e-9; }   /* ours: the kernel's limit */
static int bb_prim_of(double pv[8], const double *box, const double *cyl) {   /* the current shape as a Block / Cylinder */
    if (G->bb_shape == 0) { memcpy(pv, box, 6 * sizeof(double)); pv[6] = pv[7] = 0; return 15; }
    memcpy(pv, cyl, 8 * sizeof(double)); return 17;
}
static void bb_live_show(void) {              /* the part's bodies + the live solid (cui_feat.c bodies_show_with) */
    bodies_show_with(sbodies, nsbodies, bb_lsol_on ? &bb_lsol : NULL);
}
static void bb_live_drop(void) {
    if (!bb_lsol_on) return;
    body_free(&bb_lsol); bb_lsol_on = 0; bb_live_show();
}
static int bb_live_build(const double *box, const double *cyl) {   /* make / update the live solid from box / cyl */
    double pv[8]; int prim = bb_prim_of(pv, box, cyl); Body b;
    if (!bb_solid_ok() || !prim_body(prim, pv, "", &b)) return 0;
    if (bb_lsol_on) body_free(&bb_lsol);
    bb_lsol = b; bb_lsol_on = 1; bb_live_show();
    return 1;
}
static void bb_live_preview(void) {
    if (bb_solid_shape_ok()) { if (!bb_live_build(bb_box, bb_cyl)) bb_live_drop(); }
    else bb_live_drop();
}
/* final (OK / Apply): the live solid kept when bit 8 is asked for (flags &= 7), else deleted. Ours: kept = a Block / Cylinder
   feature (ExFeat.prim) of the current shape, one undo step of its own (U_FEAT) */
static void bb_live_final(int flags) {
    int keep = (flags & 8) && bb_solid_shape_ok() && bb_solid_ok();
    if (keep) {
        ExFeat f; memset(&f, 0, sizeof f); f.prim = bb_prim_of(f.pv, bb_box, bb_cyl); f.level = geo_level; f.color = cfg_solid_color;   /* SOL-5d */
        Body t; if (prim_body(f.prim, f.pv, "", &t)) {
            body_free(&t);
            feat_push(&f);
            FeatPack *k = malloc(sizeof *k);
            if (k) { k->n = 1; k->f = malloc(sizeof(ExFeat)); if (k->f) k->f[0] = feat_dup(&feats[nfeats - 1]); else { free(k); k = NULL; } }
            if (k) { URec r; memset(&r, 0, sizeof r); r.kind = U_FEAT; r.feat = k; r.idx = 0; undo_push(r); }
            doc_dirty = 1;
        }
    }
    if (bb_lsol_on) { body_free(&bb_lsol); bb_lsol_on = 0; }
    if (keep) solids_rebuild(); else bb_live_show();
}

/* ---- Push Pull (spec bbox_pushpull_spec.md 2 / 3): a face of the live solid picked (event 0x2000) -> an arrow
   (FUN_180344b30); dragging it previews (0x800000 -> FUN_1803469a0 -> FUN_180345180 commit 0), the end commits
   (0x1000000 / 0x2000000 -> FUN_180346a60), a cancelled drag posts code 6 (FUN_180346850: get_event 1 while editing ->
   EmCancelSelObjEdit) = the panel sizes re-applied and the arrow made again.
   Face ids (sldAskPrimBBoxSolidFaceId, meaning from FUN_180345180 / FUN_180343900): rect 1 +Z, 2 -Y, 3 -X, 4 +Y, 5 -Z, 6 +X;
   cylinder 1 the top end (c + dir len), 2 the base (c), 3 the side. Ours: the face is found by a ray test on the box /
   cylinder (the Parasolid pick is NOT DECODED); sldFaceNormalAtMidPt's point = the face centre (planar faces) and, on the
   side, the point at half height in the direction of FUN_18005b220's reference vector (NOT DECODED which point
   Parasolid takes); the arrow takes no typed value (SelArrowBase's edit field use NOT DECODED): mouse drags only. */
static int bb_face;                           /* the current face id (driver local_f64), 0 = none */
static int bb_arrow, bb_ar_cyl;               /* an arrow exists; SelArrow (the cylinder side) vs SelRefArrow */
static double bb_ar_a[3], bb_ar_d[3], bb_ar_off, bb_base;   /* anchor, direction, offset (GrArrow), base value */
static int bb_edit; static double bb_last;    /* editActive, last (local_f68 / local_f28) */
static double bb_pp_box[6], bb_pp_cyl[8];     /* the drag preview's shape (FUN_180345180 on copies) */
static int bb_pp_on;
static void bb_cyl_frame(const double *cyl, double ref[3]) {   /* FUN_18005b220 / FUN_180347f70: ref = normalize((dir x h) x dir) */
    const double *d = cyl + 5; double h[3] = { 1, 0, 0 };
    if (!(fabs(d[1]) > BB_TOL || fabs(d[2]) > BB_TOL)) { h[0] = 0; h[1] = 1; }
    double a[3] = { d[1] * h[2] - d[2] * h[1], d[2] * h[0] - d[0] * h[2], d[0] * h[1] - d[1] * h[0] };
    double b[3] = { a[1] * d[2] - a[2] * d[1], a[2] * d[0] - a[0] * d[2], a[0] * d[1] - a[1] * d[0] };
    double l = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]); for (int k = 0; k < 3; k++) ref[k] = l > 0 ? b[k] / l : 0;
}
/* sldFaceNormalAtMidPt for the face id (ours, see above): P, outward N; 0 if none */
static int bb_face_pn(int id, double P[3], double N[3]) {
    memset(N, 0, 3 * sizeof(double));
    if (G->bb_shape == 0) {
        const double *b = bb_box; double c[3] = { (b[0] + b[1]) / 2, (b[2] + b[3]) / 2, (b[4] + b[5]) / 2 };
        memcpy(P, c, sizeof c);
        switch (id) {
        case 1: P[2] = b[5]; N[2] = 1; break;  case 5: P[2] = b[4]; N[2] = -1; break;
        case 2: P[1] = b[2]; N[1] = -1; break; case 4: P[1] = b[3]; N[1] = 1; break;
        case 3: P[0] = b[0]; N[0] = -1; break; case 6: P[0] = b[1]; N[0] = 1; break;
        default: return 0;
        }
        return 1;
    }
    const double *c = bb_cyl + 2, *d = bb_cyl + 5;
    if (id == 1) { for (int k = 0; k < 3; k++) { P[k] = c[k] + d[k] * bb_cyl[1]; N[k] = d[k]; } return 1; }
    if (id == 2) { for (int k = 0; k < 3; k++) { P[k] = c[k]; N[k] = -d[k]; } return 1; }
    if (id == 3) { double ref[3]; bb_cyl_frame(bb_cyl, ref);
        for (int k = 0; k < 3; k++) { P[k] = c[k] + d[k] * bb_cyl[1] * 0.5 + ref[k] * bb_cyl[0]; N[k] = ref[k]; } return 1; }
    return 0;
}
/* FUN_180343900: the size of the face's dimension (rect 1 / 5 SizeZ, 2 / 4 SizeY, 3 / 6 SizeX; cylinder 1 / 2 Height) */
static double bb_face_size(int id) {
    const GeoParams *g = G;
    if (g->bb_shape == 0) return id == 1 || id == 5 ? g->bb_sz : id == 2 || id == 4 ? g->bb_sy : id == 3 || id == 6 ? g->bb_sx : 0;
    return id == 1 || id == 2 ? g->bb_h : 0;
}
static void bb_arrow_remove(void) { bb_arrow = 0; bb_edit = 0; bb_pp_on = 0; overlay_dirty = 1; }   /* FUN_180345470 */
/* FUN_180344b30(face): base := 0; the old arrow removed; sldFaceNormalAtMidPt; a cylindrical face (sldIsSldFaceCyl, convexity
   0 here: the bounding cylinder is convex): offset := rad, P := P - N rad (the axis), SelArrow(false), limits [0, 1e38]; a
   planar face: offset 0, SelRefArrow; Absolute (+0x41 == 0) and planar: P := P - N size, base := -size, offset := size */
static void bb_arrow_make(int id) {
    bb_base = 0; bb_arrow_remove();
    double P[3], N[3]; if (!bb_lsol_on || !bb_face_pn(id, P, N)) return;
    int cyl = G->bb_shape != 0 && id == 3;
    if (cyl) { for (int k = 0; k < 3; k++) P[k] -= N[k] * bb_cyl[0]; bb_ar_off = bb_cyl[0]; }
    else {
        bb_ar_off = 0;
        if (!G->bb_incr) { double sz = bb_face_size(id); for (int k = 0; k < 3; k++) P[k] -= N[k] * sz; bb_base = -sz; bb_ar_off = sz; }
    }
    memcpy(bb_ar_a, P, sizeof bb_ar_a); memcpy(bb_ar_d, N, sizeof bb_ar_d);
    bb_ar_cyl = cyl; bb_arrow = 1; bb_face = id; overlay_dirty = 1;
}
/* FUN_180345180(value, commit): on copies of s / box / cyl; d = value, doubled with Both directions; rect 1 / 5 SizeZ, 2 / 4
   SizeY, 3 / 6 SizeX += d with the anchor 9 / 6 / 6 / 7 / 4 / 7 (Both: 10); cylinder 1 Height += d, CylOrigin 1; 2 Height += d,
   CylOrigin 0 (Both: 2); 3 Radius := value; then FUN_180346490 on the copies and the live solid updated; commit: the
   copy back into s with the original origins, box / cyl := the copies */
static void bb_pp_apply(double value, int commit) {
    GeoParams *g = G, c = *g;
    double box[6], cyl[8]; memcpy(box, bb_box, sizeof box); memcpy(cyl, bb_cyl, sizeof cyl);
    double d = g->bb_both ? value * 2 : value; int id = bb_face;
    if (g->bb_shape == 0) {
        static const int AN[7] = { 0, 9, 6, 6, 7, 4, 7 };
        if (id < 1 || id > 6) return;
        if (id == 1 || id == 5) c.bb_sz += d; else if (id == 2 || id == 4) c.bb_sy += d; else c.bb_sx += d;
        c.bb_rorg = g->bb_both ? 10 : AN[id];
    } else {
        if (id == 1) { c.bb_h += d; c.bb_corg = 1; } else if (id == 2) { c.bb_h += d; c.bb_corg = 0; }
        else if (id == 3) c.bb_r = value; else return;
        if (g->bb_both && (id == 1 || id == 2)) c.bb_corg = 2;
    }
    bb_resize_with(&c, box, cyl);
    bb_live_build(box, cyl);                     /* FUN_180347b00 / FUN_180347f70 (update in place) */
    if (commit) {
        c.bb_rorg = g->bb_rorg; c.bb_corg = g->bb_corg;
        g->bb_sx = c.bb_sx; g->bb_sy = c.bb_sy; g->bb_sz = c.bb_sz; g->bb_r = c.bb_r; g->bb_h = c.bb_h;   /* FUN_18005d7b0 */
        memcpy(bb_box, box, sizeof box); memcpy(bb_cyl, cyl, sizeof cyl); bb_pp_on = 0;
    } else { memcpy(bb_pp_box, box, sizeof box); memcpy(bb_pp_cyl, cyl, sizeof cyl); bb_pp_on = 1; }
    overlay_dirty = 1;
}
/* the live solid's face under the cursor (event 0x2000; ours: ray test), 0 = none */
static int bb_face_hit(float mx, float my) {
    double o[3], w[3], vx, vy;
    view_px_to_view(mx - vp_x, my - vp_y, &vx, &vy); view_to_world3(vx, vy, 1e6, o); view_to_world3(vx, vy, 0, w);
    double D[3] = { w[0] - o[0], w[1] - o[1], w[2] - o[2] };
    int best = 0; double bt = 1e300;
    for (int id = 1; id <= (G->bb_shape == 0 ? 6 : 3); id++) {
        double P[3], N[3]; if (!bb_face_pn(id, P, N)) continue;
        double t;
        if (G->bb_shape != 0 && id == 3) {       /* the side: |(q - c) x dir| = r, between the ends */
            const double *c = bb_cyl + 2, *a = bb_cyl + 5; double u[3] = { o[0] - c[0], o[1] - c[1], o[2] - c[2] };
            double ua = u[0] * a[0] + u[1] * a[1] + u[2] * a[2], da = D[0] * a[0] + D[1] * a[1] + D[2] * a[2];
            double up[3] = { u[0] - a[0] * ua, u[1] - a[1] * ua, u[2] - a[2] * ua }, dp[3] = { D[0] - a[0] * da, D[1] - a[1] * da, D[2] - a[2] * da };
            double A = dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2], B = 2 * (up[0] * dp[0] + up[1] * dp[1] + up[2] * dp[2]),
                   C = up[0] * up[0] + up[1] * up[1] + up[2] * up[2] - bb_cyl[0] * bb_cyl[0], disc = B * B - 4 * A * C;
            if (A < 1e-30 || disc < 0) continue;
            t = (-B - sqrt(disc)) / (2 * A); if (t < 0) continue;
            double h = ua + da * t; if (h < 0 || h > bb_cyl[1]) continue;
        } else {
            double dn = D[0] * N[0] + D[1] * N[1] + D[2] * N[2]; if (fabs(dn) < 1e-30) continue;
            t = ((P[0] - o[0]) * N[0] + (P[1] - o[1]) * N[1] + (P[2] - o[2]) * N[2]) / dn; if (t < 0) continue;
            double q[3] = { o[0] + D[0] * t, o[1] + D[1] * t, o[2] + D[2] * t };
            if (G->bb_shape == 0) { const double *b = bb_box; double e = 1e-9 * (1 + ext);
                if (q[0] < fmin(b[0], b[1]) - e || q[0] > fmax(b[0], b[1]) + e || q[1] < fmin(b[2], b[3]) - e || q[1] > fmax(b[2], b[3]) + e
                    || q[2] < fmin(b[4], b[5]) - e || q[2] > fmax(b[4], b[5]) + e) continue; }
            else { double r2 = 0; for (int k = 0; k < 3; k++) r2 += (q[k] - P[k]) * (q[k] - P[k]); if (r2 > bb_cyl[0] * bb_cyl[0]) continue; }
        }
        if (t < bt) { bt = t; best = id; }
    }
    return best;
}
int bb_face_pickable(void) { return bb_active && !bb_phase && bb_lsol_on && !bb_edit; }
/* geo_click while the live solid exists (FUN_180346850's mask 0x1000a000: 0x2000 = a face of the live solid) */
int bb_click(float mx, float my) {
    if (!bb_face_pickable()) return 0;
    int id = bb_face_hit(mx, my);
    if (id) bb_arrow_make(id);
    return 1;
}
/* the arrow as drawn: the shaft from anchor + N offset along N (ours: the GrArrow drawing is gafgr's, its length ext * 0.08) */
static void bb_arrow_seg(double p0[3], double p1[3]) {
    double L = ext * 0.08 + 1e-6;
    for (int k = 0; k < 3; k++) { p0[k] = bb_ar_a[k] + bb_ar_d[k] * bb_ar_off; p1[k] = p0[k] + bb_ar_d[k] * L; }
}
int bb_arrow_hit(float mx, float my) {        /* 0x400000: the arrow grabbed (ours: 8 px aperture as ex_arrow_hit) */
    if (!bb_active || !bb_arrow || bb_edit) return 0;
    double p0[3], p1[3]; bb_arrow_seg(p0, p1); float x0, y0, x1, y1;
    if (!scene_to_screen(vp_w, vp_h, p0[0], p0[1], p0[2], &x0, &y0) || !scene_to_screen(vp_w, vp_h, p1[0], p1[1], p1[2], &x1, &y1)) return 0;
    return ex_seg_dist(mx - vp_x, my - vp_y, x0, y0, x1, y1) < 8;
}
int bb_dragging(void) { return bb_active && bb_edit; }
void bb_drag_start(void) { bb_edit = 1; bb_last = 0; overlay_dirty = 1; }   /* 0x400000: editActive 1, last 0 */
/* 0x800000 (FUN_1803469a0 -> FUN_1803450b0): value = base + the arrow's offset (ours: the cursor projected on the arrow's
   axis, as ex_drag_move; GrArrow +0x1a8 NOT DECODED), a change >= tol -> the preview */
void bb_drag_move(float mx, float my) {
    if (!bb_dragging()) return;
    double vx, vy, o[3], o2[3], w[3];
    view_px_to_view(mx - vp_x, my - vp_y, &vx, &vy); view_to_world3(vx, vy, 1e6, o); view_to_world3(vx, vy, 0, o2);
    for (int k = 0; k < 3; k++) w[k] = o2[k] - o[k];
    double lw = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]); if (lw < 1e-12) return;
    for (int k = 0; k < 3; k++) w[k] /= lw;
    const double *A = bb_ar_a, *D = bb_ar_d; double u[3] = { A[0] - o[0], A[1] - o[1], A[2] - o[2] };
    double c = D[0] * w[0] + D[1] * w[1] + D[2] * w[2]; if (fabs(c) > 0.999) return;
    double ud = u[0] * D[0] + u[1] * D[1] + u[2] * D[2], uw = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
    double off = (c * uw - ud) / (1 - c * c);
    if (bb_ar_cyl && off < 0) off = 0;           /* GrArrow::SetLimits(0, 1e38) */
    bb_drag_value(bb_base + off);
}
void bb_drag_value(double value) {             /* FUN_1803450b0 / FUN_180345180(value, commit 0) */
    if (!bb_dragging()) return;
    if (fabs(bb_last - value) >= BB_TOL) { bb_last = value; bb_pp_apply(value, 0); }
}
/* the end: commit (FUN_180346a60: |last| > tol -> FUN_180345180(last, commit 1), the stock write, the panel pushed, Volume,
   the arrow made again on the face); cancel = code 6 (FUN_180346eb0 without recompute, then FUN_180344b30(face)) */
void bb_drag_end(int cancel) {
    if (!bb_dragging()) return;
    bb_edit = 0; int face = bb_face;
    if (cancel) { bb_pp_on = 0; bb_compute(0, 0); bb_arrow_make(face); return; }
    if (fabs(bb_last) > BB_TOL) {
        bb_pp_apply(bb_last, 1);
        if (G->bb_shape == 0) bb_stock_rect(); else bb_stock_cyl();
        bb_volume(); bb_seen_sync();             /* FUN_18015d2c0(model, s, 1): pushed without posting codes */
    }
    bb_pp_on = 0; bb_arrow_make(face);
}

/* ---- Create Tool Plane (FUN_180345fd0 -> FUN_180346160, decomp/mccad_bcyl.c; spec bbox_toolplane_spec.md B), at the final
   compute with +0x43: the origin from the box by RectangularOrigin (0 (x0,y1,z1) 1 (x1,y1,z1) 2 (x0,y0,z1) 3 (x1,y0,z1)
   4 (xm,ym,z1) 5 (x0,y1,z0) 6 (x1,y1,z0) 7 (x0,y0,z0) 8 (x1,y0,z0) 10 (xm,ym,zm), else (xm,ym,z0)) or from the cylinder by
   CylindricalOrigin (Top c + dir len, Center c + dir len / 2, else c; then world_to_view of the active Cplane); p3 = 1: in 2D
   construction the view z := the construction depth (+ the active origin's z: none here); P = view_to_world (rotation
   only). The matrix = the active Cplane's (Top here), Plane(Matrix33, P3D, 1 Tplane); a non-empty +0x48 -> SetName; Add;
   +0x50 -> SetActivePlane(1 Tplane), +0x51 -> SetActivePlane(3 WCS) (cui_planes.c planes_add / plane_active). The machine
   group's group_view update of SetActivePlane (+0x708 + type * 0x68) is NOT PORTED (not written to the part). */
static int bb_tp_last;                        /* the last plane made (tests) */
static void bb_tool_plane(void) {
    const GeoParams *g = G; double v[3];
    static const double I[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };   /* the active Cplane: Top (active-plane switching of the Cplane NOT PORTED) */
    if (g->bb_shape == 0) {
        const double *b = bb_box; double xm = (b[1] - b[0]) * 0.5 + b[0], ym = (b[3] - b[2]) * 0.5 + b[2], zm = (b[5] - b[4]) * 0.5 + b[4];
        switch (g->bb_rorg) {
        case 0: v[0] = b[0]; v[1] = b[3]; v[2] = b[5]; break;  case 1: v[0] = b[1]; v[1] = b[3]; v[2] = b[5]; break;
        case 2: v[0] = b[0]; v[1] = b[2]; v[2] = b[5]; break;  case 3: v[0] = b[1]; v[1] = b[2]; v[2] = b[5]; break;
        case 4: v[0] = xm; v[1] = ym; v[2] = b[5]; break;
        case 5: v[0] = b[0]; v[1] = b[3]; v[2] = b[4]; break;  case 6: v[0] = b[1]; v[1] = b[3]; v[2] = b[4]; break;
        case 7: v[0] = b[0]; v[1] = b[2]; v[2] = b[4]; break;  case 8: v[0] = b[1]; v[1] = b[2]; v[2] = b[4]; break;
        case 10: v[0] = xm; v[1] = ym; v[2] = zm; break;
        default: v[0] = xm; v[1] = ym; v[2] = b[4]; break;
        }
    } else {
        double f = g->bb_corg == 0 ? 1 : g->bb_corg == 2 ? 0.5 : 0, w[3];
        for (int k = 0; k < 3; k++) w[k] = bb_cyl[2 + k] + bb_cyl[5 + k] * bb_cyl[1] * f;
        w2v(I, w, v);
    }
    if (!cfg_constr_3d) v[2] = bb_depth();
    double P[3]; v2w(I, v, P);
    int id = planes_add(g->bb_tpname, I, P);
    if (!id) { cui_message("Bounding Box", "The tool plane could not be added."); return; }   /* ours ("unable to add plane" is a debug string) */
    if (g->bb_tpcur) plane_set_active(1, id);           /* SetActivePlane(1, plane, SET_ACTIVE_ORIGIN 1) mccad_bcyl.c:196: the plane and its origin */
    if (g->bb_tpwcs) plane_set_active(3, id);           /* (3, .., 1) mccad_bcyl.c:199 */
    bb_tp_last = id; doc_dirty = 1;
}

/* ---- the compute FUN_180346eb0 */
/* FUN_180346490: the panel sizes applied keeping the anchor side (RectangularOrigin) / the cylinder's origin end */
static void bb_resize_with(const GeoParams *g, double *b, double *cy) {
    if (g->bb_shape != 0) {
        double d = 0; int mv = 1;
        if (g->bb_corg == 0) d = cy[1] - g->bb_h;                  /* Top: c += dir (len_old - H) */
        else if (g->bb_corg == 2) d = cy[1] * 0.5 - g->bb_h * 0.5;   /* Center */
        else mv = 0;                                                /* Bottom: c stays */
        if (mv) for (int k = 0; k < 3; k++) cy[2 + k] += d * cy[5 + k];
        cy[1] = g->bb_h; cy[0] = g->bb_r;
        return;
    }
    int o = g->bb_rorg; double X = g->bb_sx, Y = g->bb_sy, Z = g->bb_sz;
    int left = o == 0 || o == 2 || o == 5 || o == 7, right = o == 1 || o == 3 || o == 6 || o == 8;
    int back = o == 0 || o == 1 || o == 5 || o == 6, front = o == 2 || o == 3 || o == 7 || o == 8;
    if (left) b[1] = b[0] + X; else if (right) b[0] = b[1] - X; else { double m = (b[1] - b[0]) * 0.5 + b[0] - X * 0.5; b[0] = m; b[1] = m + X; }
    if (back) b[2] = b[3] - Y; else if (front) b[3] = b[2] + Y; else { double m = (b[3] - b[2]) * 0.5 + b[2] - Y * 0.5; b[2] = m; b[3] = m + Y; }
    if (o <= 4) b[4] = b[5] - Z;                                    /* Top */
    else if (o != 10) b[5] = b[4] + Z;                              /* Bottom */
    else { double m = (b[5] - b[4]) * 0.5 + b[4]; b[4] = m - Z * 0.5; b[5] = Z * 0.5 + m; }
}
static void bb_resize(void) { bb_resize_with(G, bb_box, bb_cyl); }
/* the construction depth (ConstrDepth + the active origin's z, origin off): this program's grid Z, as cui_stock.c's
   create_bbox_stock_no_dlg port */
static double bb_depth(void) { return grid_z(); }
/* FUN_180347840 (2D construction mode): Z at the construction depth, then (not recomputing) SizeZ below (Top anchors), above
   (Bottom) or both halves (TrueCenter) */
static void bb_2d_rect(int recompute) {
    if (cfg_constr_3d) return;
    double d = bb_depth(); bb_box[4] = bb_box[5] = d;
    if (recompute) return;
    const GeoParams *g = G;
    if (g->bb_rorg < 5) bb_box[4] -= g->bb_sz; else if (g->bb_rorg < 10) bb_box[5] = d + g->bb_sz;
    else { bb_box[4] -= g->bb_sz * 0.5; bb_box[5] = g->bb_sz * 0.5 + d; }
}
/* FUN_180347960: the centre moved to the construction depth in the axis view, len 0; not recomputing: Height applied by the
   origin (Top c -= dir H, Center c -= dir H / 2), len = Height */
static void bb_2d_cyl(int recompute) {
    if (cfg_constr_3d) return;
    double M[9], v[3]; view_std_matrix(bb_axis_view(G->bb_axis), M);
    w2v(M, bb_cyl + 2, v); v[2] = bb_depth(); v2w(M, v, bb_cyl + 2);
    bb_cyl[1] = 0;
    if (recompute) return;
    const GeoParams *g = G; double d = g->bb_corg == 0 ? -g->bb_h : g->bb_corg == 2 ? -g->bb_h * 0.5 : 0;
    for (int k = 0; k < 3; k++) bb_cyl[2 + k] += d * bb_cyl[5 + k];
    bb_cyl[1] = g->bb_h;
}
static void bb_seen_sync(void) { bb_seen = *G; }
static void bb_compute(int recompute, int final) {
    GeoParams *g = G;
    int rect = g->bb_shape == 0, flags = bb_flags(), have = 0, ok = 0, from = F.nents;
    bb_arrow_remove();                                   /* FUN_180346eb0: the push-pull arrow removed */
    if (!recompute) bb_resize();
    if (flags || bb_stock) {                             /* FUN_180345930 / FUN_180345da0: nothing to make (flags 0, not stock) -> fail */
        const unsigned char *pk = bb_pick_all ? NULL : bb_pick;
        if (rect) {
            int got = !recompute || stock_bbox_sel(BB_MASK, pk, bb_npick, bb_box);   /* make_bounding_box(box, true) */
            if (got) {
                bb_2d_rect(recompute);
                have = span_ok(bb_box[0], bb_box[1]) || span_ok(bb_box[2], bb_box[3]) || span_ok(bb_box[4], bb_box[5]);
                if (final) { bb_live_final(flags); bb_out_rect(flags & 7); }   /* FUN_180345b10 final: the live solid kept for bit 8 */
                else bb_live_preview();                  /* FUN_180345b10 preview: the live solid made / updated in place */
                bb_stock_rect();
                ok = fabs(bb_box[1] - bb_box[0]) >= BB_TOL && fabs(bb_box[3] - bb_box[2]) >= BB_TOL && fabs(bb_box[5] - bb_box[4]) >= BB_TOL;   /* FUN_180345830 */
                if (final && g->bb_ctp) bb_tool_plane();  /* FUN_180345fd0 (final and +0x43) */
            }
        } else {
            int got = 1;
            if (recompute) {                             /* FUN_18005bab0(centre on axis, axis code): the page-axis order of bcyl_fit */
                double c[3] = { 0, 0, 0 }, d[3] = { 0, 0, 1 }, r = 0, len = 0;
                int page = g->bb_axis == 0 ? 2 : g->bb_axis == 1 ? 1 : 0;
                got = stock_bcyl_sel(BB_MASK, pk, bb_npick, page, g->bb_center, c, d, &r, &len);
                if (got) { bb_cyl[0] = r; bb_cyl[1] = len; for (int k = 0; k < 3; k++) { bb_cyl[2 + k] = c[k]; bb_cyl[5 + k] = d[k]; } }
            }
            if (got) {
                bb_2d_cyl(recompute);
                have = bb_cyl[0] >= BB_TOL;
                if (final) { bb_live_final(flags); bb_out_cyl(flags & 7); }   /* FUN_180345c50 */
                else bb_live_preview();
                bb_stock_cyl();
                ok = fabs(bb_cyl[0]) > 0.0;
                if (final && g->bb_ctp) bb_tool_plane();  /* FUN_180345da0 -> FUN_180345fd0 */
            }
        }
    }
    if (recompute) {                                     /* no live geometry -> sizes and box 0, else sizes := extents (FUN_180345f80) */
        if (!have) { g->bb_sx = g->bb_sy = g->bb_sz = g->bb_r = g->bb_h = 0; memset(bb_box, 0, sizeof bb_box); }
        else if (rect) { g->bb_sx = bb_box[1] - bb_box[0]; g->bb_sy = bb_box[3] - bb_box[2]; g->bb_sz = bb_box[5] - bb_box[4]; }
        else { g->bb_r = bb_cyl[0]; g->bb_h = bb_cyl[1]; }
    }
    /* EnableSolidsCheckBox := the compute succeeded (sld_solids_ok: solids exist here); ours: a cylinder whose axis is not Z
       cannot be built by this program's kernel (bb_solid_ok), so its Solids check is greyed too */
    bb_en_solids = ok && bb_solid_ok();
    if (!bb_en_solids) g->bb_solids = 0;                 /* Solids := false on failure */
    if (!final && !bb_solid_shape_ok()) bb_live_drop();  /* an invalid shape: the live solid deleted (FUN_180347310) */
    g->bb_vol = !ok ? 0 : bb_vol_of();             /* FUN_180348590 */
    bb_ok = ok; bb_have = have;
    if (final && F.nents > from) { undo_ents_added(from); scene_reupload(); }
    bb_seen_sync(); overlay_dirty = 1;
}

/* ---- the entity step */
static void bb_select_all_shown(void) {      /* set_clr_sel_bits(4, 0, 4, 1, 0x1905ff): bit 4 on every shown entity (+ the solids) */
    sel_grow(); nselected = 0;
    for (int i = 0; i < F.nents; i++) {
        int on = F.ents[i].type && !F.ents[i].gen && F.ents[i].type != E_SURF && F.ents[i].type != E_NOTE && F.ents[i].type != E_MESH && !(ent_hidden && i < nent_hidden && ent_hidden[i]);   /* surfaces (mask 0x100) / notes: their extents NOT PORTED */
        ent_sel[i] = (unsigned char)on; nselected += on;
    }
    bb_pick_all = 1; overlay_dirty = 1;
}
static void bb_prompt(void) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()[0] ? geo_prompt() : geo_name(geo_cmd)); }
/* FUN_1803470e0: the previous selection cleared (set_clr_sel_bits(0, 4, 0, 4, mask)), select_ents with mccadres 81; the
   preview is hidden while it runs (the live solid blanked) */
static void bb_begin_selection(void) {
    sel_clear(); overlay_dirty = 1;
    bb_arrow_remove(); bb_live_drop();           /* the live solid blanked while select_ents runs (ours: deleted, made again after it) */
    bb_phase = 1; bb_prompt();
}
static void bb_close(int result);
/* the end of select_ents (Enter): it succeeds with at least one entity; a failure at the start destroys the panel (result
   3) and on code 7 ends the command as Cancel */
void bb_enter(void) {
    if (!bb_selecting()) return;
    bb_phase = 0;
    int n = 0; for (int i = 0; i < F.nents; i++) n += is_sel(i);
    if (n < 1) { bb_close(3); geo_end(); return; }
    free(bb_pick); bb_npick = F.nents; bb_pick = calloc(bb_npick ? bb_npick : 1, 1);
    for (int i = 0; i < F.nents; i++) bb_pick[i] = (unsigned char)is_sel(i);
    bb_pick_all = 0;
    bb_compute(1, 0);
    bb_prompt();
}
/* code 7 (AllEntities changed, the Select button, after Apply): Manual -> select_ents again; All shown -> every shown
   entity selected, recompute */
static void bb_code7(void) {
    if (G->bb_all) { bb_select_all_shown(); bb_compute(1, 0); bb_prompt(); }
    else bb_begin_selection();
}

/* ---- start / end */
void bb_stock_run(void) {                     /* Stock Setup 25451 -> mc:FUN_1801f1150 (the sheet suspended) */
    if (!show_stock_setup) return;
    st_pick = ST_PICK_BBOX; bb_pend_stock = 1;
    if (!geo_start("Bounding Box")) st_pick = 0;
    bb_pend_stock = 0; overlay_dirty = 1;
}
void bb_begin(int stock) {
    stock = stock || bb_pend_stock;
    GeoParams *g = G;
    bb_load(stock ? &bb_st : &bb_wf);        /* enable_dialog_modality = 1: the modal statics (non-modal hard defaults: NOT USED) */
    if (stock) {                             /* create_boundingshape_either_method's overrides */
        g->bb_all = 0; g->bb_lines = g->bb_pts = g->bb_ctr = 0; g->bb_solids = 0;
        g->bb_shape = stp.shape == 0 ? 0 : 1;                       /* +0x30 := *rect_inout */
        g->bb_axis = stp.axis == 1 ? 1 : stp.axis == 2 ? 0 : 2;     /* page axis 1 -> 1, 2 -> 0, else 2 */
        g->bb_center = stp.center != 0; g->bb_ctp = 0;
    }
    /* sld_solids_ok is true here (solids exist): +0x04 kept; EnableSolidsCheckBox from the first compute */
    bb_face = 0; bb_arrow_remove(); bb_en_solids = 0;
    if (!stock && !g->bb_lines && !g->bb_pts && !g->bb_ctr && !g->bb_solids) g->bb_lines = 1;
    if (stock) g->bb_ctp = 0;                /* +0x43 := 0 (EnableCreateToolPlane false) */
    bb_active = 1; bb_stock = stock; bb_phase = 0; bb_ok = bb_have = 0; bb_pick_all = 0;
    memset(bb_box, 0, sizeof bb_box); memset(bb_cyl, 0, sizeof bb_cyl); g->bb_vol = 0;
    if (stock) bb_snap_take();
    bb_seen_sync();
    if (g->bb_all) { bb_select_all_shown(); bb_compute(1, 0); }      /* the entity step, then UndoTransStart + the first compute */
    else bb_begin_selection();
}
/* the end of FUN_180343ae0: the selection bits cleared; result 0 (OK) and modal -> the struct back to its static; stock mode:
   the result for the page (create_boundingshape_either_method + its caller), the sheet back (AfxResumeWindow) */
static void bb_close(int result) {
    if (!bb_active) return;
    if (result == 0) bb_store(bb_stock ? &bb_st : &bb_wf);
    sel_clear();
    if (bb_stock) {
        bb_snap_put();
        if (result == 0) st_bbox_panel_result(G->bb_shape == 0, bb_box, bb_cyl, G->bb_axis, G->bb_center);
        if (st_pick == ST_PICK_BBOX) st_pick = 0;
    }
    bb_arrow_remove(); bb_face = 0;
    if (bb_lsol_on) { body_free(&bb_lsol); bb_lsol_on = 0; bodies_show(sbodies, nsbodies); }   /* Cancel / end: the live solid deleted */
    bb_active = 0; bb_stock = 0; bb_phase = 0; bb_have = 0;
    free(bb_pick); bb_pick = NULL; bb_npick = 0;
    overlay_dirty = 1; scene_reupload();
}
void bb_abort(void) { bb_close(3); }
/* the panel codes (FUN_180343ae0's switch on event 0x10000000) */
int bb_button(int code) {
    if (!bb_active) return 0;
    switch (code) {
    case 0:                                  /* OK: the final compute (output), main colour, clear selection, UndoTransCommit */
        if (bb_phase) return 1;
        bb_compute(0, 1); bb_close(0); geo_end(); return 1;
    case 1:                                  /* Apply (not STL; greyed in stock mode): final compute, commit, then code 7 */
        if (bb_phase || !bb_apply_enabled()) return 1;
        bb_compute(0, 1); sel_clear(); bb_code7(); return 1;
    case 2: bb_close(3); geo_end(); return 1;   /* Cancel: UndoTransTerminate, result 3 */
    case 7:                                  /* SelectEntitiesCommand: AllEntities := false, then code 7 */
        if (bb_phase) return 1;
        G->bb_all = 0; bb_seen.bb_all = 0; bb_code7(); return 1;
    }
    return 0;
}
/* a panel value changed: the code its observer posts (FUN_18015cb60 / the ctor lambdas) and its handling */
void bb_poll(void) {
    if (!bb_active) return;
    GeoParams *g = G, *o = &bb_seen;
    if (bb_phase) { bb_seen_sync(); return; }   /* select_ents runs: no panel events (the panel is greyed) */
    if (g->bb_all != o->bb_all) { o->bb_all = g->bb_all; bb_code7(); if (bb_phase) return; }   /* code 7 */
    if (g->bb_shape != o->bb_shape || g->bb_axis != o->bb_axis || g->bb_center != o->bb_center) {   /* code 5: recompute */
        bb_compute(1, 0); return;
    }
    if (g->bb_rorg != o->bb_rorg || g->bb_corg != o->bb_corg) {      /* code 8: only in 2D, only when the Z side changes */
        int oa = o->bb_rorg, na = g->bb_rorg, same = o->bb_corg == g->bb_corg, run;
        if (oa < 5) run = na < 5 ? !((na != 10 || oa == 10) && same) : 1;
        else run = na > 4 ? !((na != 10 || oa == 10) && same) : 1;
        o->bb_rorg = g->bb_rorg; o->bb_corg = g->bb_corg;
        if (!cfg_constr_3d && run) bb_compute(0, 0);
    }
    if (g->bb_sx != o->bb_sx || g->bb_sy != o->bb_sy || g->bb_sz != o->bb_sz || g->bb_r != o->bb_r || g->bb_h != o->bb_h || g->bb_incr != o->bb_incr) {
        for (double *v = &g->bb_sx; v <= &g->bb_h; v++) if (*v < 0) *v = 0;   /* FUN_18015c3d0 clamps negative values to 0 */
        bb_compute(0, 0); return;                                    /* code 4 */
    }
    bb_seen_sync();                          /* the create flags, Both directions, tool plane: no code */
}

/* ---- the preview (ours): the Lines and arcs outline of the current shape in the live colour */
void bb_ov(float **pv, int *pn, int *pcap) {
    if (!bb_active || bb_phase) return;
    if (bb_arrow) {                               /* the push-pull arrow: UtColor(0x80, 0, 0, 0xff) taken as r, g, b (argument
                                                     order NOT DECODED); shaft + head, ours as ex_ov_arrows */
        double p0[3], p1[3]; bb_arrow_seg(p0, p1); float r = 0x80 / 255.f, gg = 0, b = 0; double hl = ext * 0.02 + 1e-6;
        V3 a = { p0[0], p0[1], p0[2] }, t = { p1[0], p1[1], p1[2] };
        ov_pt(pv, pn, pcap, a, r, gg, b); ov_pt(pv, pn, pcap, t, r, gg, b);
        double up[3] = { 0, 0, 1 }; if (fabs(bb_ar_d[2]) > 0.9) { up[0] = 1; up[2] = 0; }
        double q[3] = { bb_ar_d[1] * up[2] - bb_ar_d[2] * up[1], bb_ar_d[2] * up[0] - bb_ar_d[0] * up[2], bb_ar_d[0] * up[1] - bb_ar_d[1] * up[0] };
        double l = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
        if (l > 1e-12) for (int sg = -1; sg <= 1; sg += 2) {
            V3 h = { p1[0] - bb_ar_d[0] * hl + sg * q[0] / l * hl * .4, p1[1] - bb_ar_d[1] * hl + sg * q[1] / l * hl * .4, p1[2] - bb_ar_d[2] * hl + sg * q[2] / l * hl * .4 };
            ov_pt(pv, pn, pcap, t, r, gg, b); ov_pt(pv, pn, pcap, h, r, gg, b);
        }
    }
    if (!bb_have || bb_lsol_on) return;           /* the live solid is the preview when it exists */
    float rgb[3] = { 1, 1, 1 }; mc_rgb(cfg_live_color, rgb);
    if (G->bb_shape == 0) {
        double P[8][3]; int E[12][2]; bb_corners(bb_box, P); int n = bb_edges(bb_box, E);
        for (int i = 0; i < n; i++) {
            V3 a = { P[E[i][0]][0], P[E[i][0]][1], P[E[i][0]][2] }, b = { P[E[i][1]][0], P[E[i][1]][1], P[E[i][1]][2] };
            ov_pt(pv, pn, pcap, a, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, b, rgb[0], rgb[1], rgb[2]);
        }
        return;
    }
    double c2[3], mid[3], M[9]; bb_cyl_ends(c2, mid, M);
    for (int e = 0; e < (bb_cyl[1] > BB_TOL ? 2 : 1); e++) {
        double v[3]; w2v(M, e ? c2 : bb_cyl + 2, v);
        int n = 120;                                      /* add_arc's 3 degree chords */
        for (int i = 0; i < n; i++) {
            double t0 = 2 * M_PI * i / n, t1 = 2 * M_PI * (i + 1) / n, p0[3] = { v[0] + bb_cyl[0] * cos(t0), v[1] + bb_cyl[0] * sin(t0), v[2] },
                   p1[3] = { v[0] + bb_cyl[0] * cos(t1), v[1] + bb_cyl[0] * sin(t1), v[2] }, w0[3], w1[3];
            v2w(M, p0, w0); v2w(M, p1, w1);
            ov_pt(pv, pn, pcap, (V3){ w0[0], w0[1], w0[2] }, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, (V3){ w1[0], w1[1], w1[2] }, rgb[0], rgb[1], rgb[2]);
        }
    }
}

/* ---- the panel (boundingboxmanagerview.baml order and texts, formats/ui_wireframe_panels.txt) */
void bb_panel_body(const CuiPanelOps *ops, void *u) {
    GeoParams *g = G;
    int rect = g->bb_shape == 0, en_cyl = 1;     /* EnableCylindrical: allowCyl (p5 = 1; stock: the Agie test FUN_180343a50 is not ported) */
    if (!ops->tab || ops->tab(u, 0, "Basic")) {
        if (ops->exp(u, "Entities")) {           /* content enabled = EnableEntitySelection (false only in the STL mode: not ported) */
            ops->label(u, "Select:");
            ops->radio(u, "_Manual", &g->bb_all, 0, 1);
            /* Button SelectEntitiesCommand (ImageSelect16 icon; text = its tooltip header EntitiesSelectButton_ToolTip "Select
               Entities", ours): posts code 7 */
            ops->toggle(u, "Select Entities", 1, 7);
            ops->radio(u, "_All shown", &g->bb_all, 1, 1);
            ops->end(u);
        }
        if (ops->exp(u, "Shape")) {
            ops->radio(u, "_Rectangular", &g->bb_shape, 0, 1);
            ops->radio(u, "_Cylindrical", &g->bb_shape, 1, en_cyl);
            ops->end(u);
        }
        if (ops->exp(u, "Rectangular Settings")) {   /* content enabled when Shape == Rectangular (IsExpanded follows Shape: not modelled) */
            /* the cube picture's 11 image radios (no text): labelled with their BAML CommandParameter, in BAML order */
            static const struct { const char *t; int v; } RO[11] = { { "TopBackLeft", 0 }, { "TopBackRight", 1 }, { "BottomBackLeft", 5 },
                { "BottomBackRight", 6 }, { "TopFrontLeft", 2 }, { "TopFrontRight", 3 }, { "BottomFrontLeft", 7 }, { "BottomFrontRight", 8 },
                { "TopCenter", 4 }, { "TrueCenter", 10 }, { "BottomCenter", 9 } };
            ops->label(u, "_Origin:");
            for (int i = 0; i < 11; i++) ops->radio(u, RO[i].t, &g->bb_rorg, RO[i].v, rect);
            ops->label(u, "Size:");
            ops->num(u, "_X:", "bbx", &g->bb_sx, NULL, rect, 0, 1e9);
            ops->num(u, "_Y:", "bby", &g->bb_sy, NULL, rect, 0, 1e9);
            ops->num(u, "_Z:", "bbz", &g->bb_sz, NULL, rect, 0, 1e9);
            ops->end(u);
        }
        if (ops->exp(u, "Cylindrical Settings")) {   /* content enabled = Shape == Cylindrical && EnableCylindrical */
            int en = !rect && en_cyl;
            ops->label(u, "Ori_gin:");               /* the cylinder picture's image radios: their BAML CommandParameter */
            ops->radio(u, "Top", &g->bb_corg, 0, en); ops->radio(u, "Center", &g->bb_corg, 2, en); ops->radio(u, "Bottom", &g->bb_corg, 1, en);
            ops->label(u, "Size:");
            ops->num(u, "Radi_us:", "bbr", &g->bb_r, NULL, en, 0, 1e9);
            ops->num(u, "Heigh_t:", "bbh", &g->bb_h, NULL, en, 0, 1e9);
            ops->label(u, "Axis:");                  /* VM Axis X / Y / Z = struct code 2 / 1 / 0 */
            ops->radio(u, "_X", &g->bb_axis, 2, en); ops->radio(u, "_Y", &g->bb_axis, 1, en); ops->radio(u, "_Z", &g->bb_axis, 0, en);
            ops->check(u, "Ce_nter on axis", &g->bb_center, en);   /* IsEnabled = CenterOnAxis.IsEnabled: its setter NOT DECODED (enabled) */
            ops->end(u);
        }
        if (ops->exp(u, "Push Pull")) {          /* the live solid's push-pull arrows (FUN_180344b30 / FUN_180345180) */
            ops->label(u, "Arrow values:");
            ops->radio(u, "Ab_solute", &g->bb_incr, 0, 1); ops->radio(u, "_Incremental", &g->bb_incr, 1, 1);
            ops->check(u, "_Both directions", &g->bb_both, 1);
            ops->end(u);
        }
        if (ops->exp(u, "Create Geometry")) {
            ops->check(u, "_Lines and arcs", &g->bb_lines, 1);
            ops->check(u, "_Points", &g->bb_pts, 1);
            ops->check(u, "C_enter point", &g->bb_ctr, 1);
            ops->check(u, "Soli_ds", &g->bb_solids, bb_en_solids);   /* EnableSolidsCheckBox (the compute succeeded; ours: not for an X / Y cylinder) */
            ops->end(u);
        }
    }
    if (!ops->tab || ops->tab(u, 1, "Advanced")) {
        if (ops->exp(u, "_Create Tool Plane")) { /* header check box (IsCheckBoxEnabled = EnableCreateToolPlane: false in stock mode) */
            ops->check(u, "_Create Tool Plane", &g->bb_ctp, !bb_stock);
            if (ops->text) ops->text(u, "_Name:", "bbtpname", g->bb_tpname, (int)sizeof g->bb_tpname, g->bb_ctp);   /* enabled = CreateToolPlane */
            else ops->label(u, "_Name:");
            ops->check(u, "_Set as current tool plane", &g->bb_tpcur, g->bb_ctp);
            ops->check(u, "Set _WCS from new tool plane", &g->bb_tpwcs, g->bb_ctp);
            ops->end(u);
        }
        if (ops->exp(u, "Volume")) {             /* read-only TextBox, F4 (the greyed field's "%.4f") */
            ops->num(u, NULL, "bbvol", &g->bb_vol, NULL, 0, 0, 1e300);
            ops->end(u);
        }
    }
}

/* ---- tests: the panel's state (script step bbdump) */
void bb_dump(void) {
    const GeoParams *g = G;
    printf("    bbox on %d stock %d selecting %d shape %d all %d rorg %d corg %d axis %d center %d flags %d size %.4f %.4f %.4f r %.4f h %.4f vol %.4f ok %d apply %d panel %d pick %d\n",
           bb_active, bb_stock, bb_phase, g->bb_shape, g->bb_all, g->bb_rorg, g->bb_corg, g->bb_axis, g->bb_center, bb_flags(),
           g->bb_sx, g->bb_sy, g->bb_sz, g->bb_r, g->bb_h, g->bb_vol, bb_ok, geo_cmd == G_BBOX && geo_panel_apply_enabled(), geo_cmd == G_BBOX && geo_panel_enabled(), st_pick);
    printf("    bbox box %.4f %.4f %.4f %.4f %.4f %.4f cyl r %.4f len %.4f c %.4f %.4f %.4f dir %.4f %.4f %.4f prompt '%s'\n",
           bb_box[0], bb_box[1], bb_box[2], bb_box[3], bb_box[4], bb_box[5], bb_cyl[0], bb_cyl[1], bb_cyl[2], bb_cyl[3], bb_cyl[4], bb_cyl[5], bb_cyl[6], bb_cyl[7], cam_msg);
    printf("    bbox stock disp %d shape %d size %.4f %.4f %.4f org %.4f %.4f %.4f corner %d axis %d center %d\n",
           F.stock_disp, F.stock_shape, F.stock[0], F.stock[1], F.stock[2], F.stock_org[0], F.stock_org[1], F.stock_org[2], F.stock_sel, F.stock_axis, F.stock_center);
    printf("    bbox live %d vol %.4f ensolids %d face %d arrow %d cyl %d a %.4f %.4f %.4f d %.4f %.4f %.4f off %.4f base %.4f edit %d last %.4f pp %d incr %d both %d ctp %d '%s' cur %d wcs %d plane %d feats %d bodies %d\n",
           bb_lsol_on, bb_lsol_on ? body_volume(&bb_lsol) : 0.0, bb_en_solids, bb_face, bb_arrow, bb_ar_cyl, bb_ar_a[0], bb_ar_a[1], bb_ar_a[2], bb_ar_d[0], bb_ar_d[1], bb_ar_d[2],
           bb_ar_off, bb_base, bb_edit, bb_last, bb_pp_on, g->bb_incr, g->bb_both, g->bb_ctp, g->bb_tpname, g->bb_tpcur, g->bb_tpwcs, bb_tp_last, nfeats, nsbodies);
}
/* script steps (ours, tests; both apps): bbface:ID (the face pick FUN_180344b30 on face ID), bbclick:X,Y (a view click at
   window X,Y through bb_click), bbgrab (the arrow grabbed, 0x400000), bbdrag:V (a drag to the arrow value V = base + offset),
   bbdragend (the release), bbdragesc (Esc: code 6) */
int bb_script(const char *st, const char *arg) {
#define IS(x) (!strcmp(st, x))
    if (IS("bbface") && arg) { if (bb_face_pickable()) bb_arrow_make(atoi(arg)); return 1; }
    if (IS("bbclick") && arg) { float x = 0, y = 0; sscanf(arg, "%f,%f", &x, &y); bb_click(x, y); return 1; }
    if (IS("bbgrab")) { if (bb_active && bb_arrow && !bb_edit) bb_drag_start(); return 1; }
    if (IS("bbdrag") && arg) { bb_drag_value(atof(arg)); return 1; }
    if (IS("bbdragend")) { bb_drag_end(0); return 1; }
    if (IS("bbdragesc")) { bb_drag_end(1); return 1; }
    return 0;
#undef IS
}
