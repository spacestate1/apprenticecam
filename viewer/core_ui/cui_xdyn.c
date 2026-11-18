/* cui_xdyn.c - Transform > Dynamic's gnomon widget (gafgr.dll CncGaf::GrGnomon / GrRefGnomon, mccore SelXformGnomon /
 * SelGnomonBase), shared by the front ends. The data side (the round, T = B^-1 C, the commits) is cui_xform.c (G_XDYN).
 * Spec: ../../formats/wireframe.md "Dynamic - port spec (2026-11-04)" / "The gnomon widget - decoded 2026-11-05"; dumps in
 * ../../decomp/xform/dynamic/: GD gnomon_draw.c, GS gnomon_select.c, RG refgnomon.c, RC ruler_compass.c, GA gafgr.c,
 * EV gnomon_editvalue.c, MC mccore_gnomon*.c; the handle meshes (MESH gnomon_mesh.txt + the SelItem table of
 * gnomon_data.txt) are generated into cui_xdyn_mesh.inc by tools/gen_gnomon_mesh.py and drawn / hit-tested as dumped.
 *
 * Frames here (GnMat) are cui_xform.c's XM layout: p' = m p + t, the columns of m = the gnomon's X / Y / Z axes in world,
 * t = its origin (Mastercam's MtTmat3d is the transpose: row vectors, translation in row 3 - GetDefMat). Mesh units go to
 * model units by s = size x 2.65 x k / ppu (Tessellate GD l.448-475; k = +0x310 = 1.0), ppu = pixels per model unit.
 *
 * Ours / deviations (listed in the reply of 2026-11-05 too):
 *  - the lit shading: render type 8's parameters are NOT DECODED - this program's mesh shading (scene.c mprog: colour x
 *    (0.30 + 0.70 |n . headlight|)); depth-sorted drawing (GetAxesDrawOrder) is replaced by a depth buffer of the gnomon's
 *    own (opaque parts, then the translucent ones);
 *  - not drawn: the GrRuler (DrawRuler) and the GrCompass (DrawCompass / DrawRotation) shown during a drag, the gnomon
 *    button (GrButton states 0x28f6 / 0x28f7: bitmaps NOT DECODED - the panel's "Gnomon button" stands in), the dashed
 *    snap line of DragOrigin (+0x524 points) and the ref gnomon's dashed line / sphere (RG Tessellate);
 *  - the ruler snap band ignores the label width (ComputeRulerWidth not followed: width 0) and takes |h| (the side of the
 *    band, from the s_PlaneDef plane's orientation, not followed); snap-to-zero (+0xf0 / +0xf2 mode) left out; the ruler's
 *    zero = the grab point [inferred] (the ruler start +0x1c0 setter for a gnomon drag NOT DECODED); its units mode +0xe8
 *    NOT DECODED: metric 0.1 mm / inch 1/8 in base steps (ScaleRuler's "+0xe8 != 2" branches), then DrawRuler's x / / sub;
 *  - the rotation snap is applied wherever the cursor is: the compass inner radius (+0x4a0 / +0x4a8) setter is NOT DECODED
 *    (its ctor value 0 would never snap); the H/V snap to 0/90/180/270 (table @0x10fa39f0) is left out; the start angle
 *    +0x1b0 = the cursor's at the press [inferred];
 *  - align (modes 5-7): lines only (arcs / splines / solid edges of s_SelMask: NOT PORTED), the axis direction's sign =
 *    the one nearer the current axis and the other two axes turned the least (SetAlignedXAxis's choice NOT DECODED); face
 *    edits (8-10, here the Z plane quadrant) need a solid face / surface pick (NOT PORTED): they fall back to DragOrigin
 *    as GrGnomon::DragAxis does without a point;
 *  - the read-out: anchored at its bottom-left (GrEdit's anchor NOT DECODED); the ruler text without its units suffix
 *    (GetRulerText's param_4 NOT DECODED); the typed values (UIFloatingCoordEdit / UIFloatingDoubleEdit) NOT PORTED;
 *  - the settings are kept for the session (no registry, task rule); their dialog (GnomonSettingsDlg 25610) is the
 *    panel's Advanced tab group; the Ctrl-right-click path to it is NOT DECODED. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "cui_xdyn_mesh.inc"

#define D2R (M_PI / 180)

/* ---------------------------------------------------------------- settings (SelGnomonBase statics, .data @0x181325d70) */
int gn_size = 120;                 /* s_GnomonSize (pixels), dialog 10527, 50-250 */
int gn_ang_on = 1;                 /* s_AngularActive 10526 */
double gn_ang_inc = 5.0;           /* s_AngularInc 10525, 1-90 (deg) */
int gn_ruler_on = 1;               /* s_RulerActive 10518 */
int gn_ruler_size = 35;            /* s_RulerSize 10528, 20-70 */
int gn_selmask = 0x3f;             /* s_SelMask: 1 line, 2 arc, 4 spline, 8 solid edge, 0x10 solid face, 0x20 surface */

/* ---------------------------------------------------------------- state */
GnMat gn_C;                        /* the gnomon frame (+0x328, GetDefMat) */
static GnMat gn_E, gn_D;           /* the frame when the drag started / the drag frame (+0x3b0, ApplyDrag) */
int gn_on;                         /* shown (EmAddSelObj .. EmRemoveSelObj) */
int gn_hover;                      /* the highlighted SelItem (+0x30c / +0xfcc), 0 none */
int gn_drag;                       /* the SelItem being edited (0 none) */
static int gn_mode;                /* its EditMode (+0x308) */
static double gn_d0, gn_d, gn_a0, gn_a;   /* translation: grab distance / the value; rotation: start angle / the value (deg) */
static int gn_kind;                /* the read-out: 0 none, 1 ruler, 2 angle, 3 origin (Tessellate GD l.658-699) */
static float gn_mx, gn_my; static int gn_mouse;   /* the last mouse point (+0xfa8) */
int gn_snapped3d;                  /* the last drag took an AutoCursor 3-D point (Is3dSnap +0x322) */

/* item-mask bits enabled for the Dynamic gnomon: CreateSelXformGnomon (MC 0x180a07f90) item mask 0x1e7f */
#define GN_MASK 0x1e7f
static int gn_enabled(int id) {    /* GrGnomon ctor's switch (GD l.47-90): the item-mask bit of each SelItem */
    unsigned b = id == 2 ? 1 : id == 3 || id == 6 ? 2 : id == 4 || id == 7 ? 4 : id == 5 || id == 8 ? 8 : id >= 9 && id <= 0xb ? 0x10
        : id >= 0xc && id <= 0xe ? 0x20 : id >= 0xf && id <= 0x11 ? 0x40 : id == 0x12 ? 0x80 : id == 0x13 ? 0x100 : id == 0x14 ? 0x200 : 0;
    return (GN_MASK & b) != 0;
}
int gn_edit_mode(int id) {         /* ResetEditMap (RG l.1228) */
    static const int M[0x15] = { [2] = 1, [3] = 2, [4] = 3, [5] = 4, [6] = 5, [7] = 6, [8] = 7, [9] = 0xd, [0xa] = 0x13, [0xb] = 0xf,
                                 [0xc] = 0xb, [0xd] = 0x16, [0xe] = 0x10, [0xf] = 0xc, [0x10] = 0x12, [0x11] = 0xe, [0x12] = 8, [0x13] = 9, [0x14] = 10 };
    return id > 0 && id < 0x15 ? M[id] : 0;
}
int gn_edit_kind(int mode) {       /* IsOriginEdit 1 / IsTranslationEdit 2-4 / IsAxisEdit 5-10 / IsRotationEdit 0xb-0x16 (GA) */
    return mode == 1 ? 1 : mode >= 2 && mode <= 4 ? 2 : mode >= 5 && mode <= 10 ? 3 : mode >= 0xb && mode <= 0x16 ? 4 : 0;
}
/* the rotation axis of an edit mode: X arc items 9-11 = modes 0xd / 0x13 / 0xf, Y 0xb / 0x16 / 0x10, Z 0xc / 0x12 / 0xe
   (ResetEditMap), the pattern Y Z X Z X Y over 0xb..0x10 and again over 0x11..0x16 */
static int rot_axis(int mode) { static const int P[6] = { 1, 2, 0, 2, 0, 1 }; return P[(mode - 0xb) % 6]; }

/* ---------------------------------------------------------------- small maths */
static GnMat gm_id(void) { GnMat a = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 0, 0, 0 } }; return a; }
static void col(const GnMat *F, int k, double v[3]) { v[0] = F->m[k]; v[1] = F->m[3 + k]; v[2] = F->m[6 + k]; }
static void setcol(GnMat *F, int k, const double v[3]) { F->m[k] = v[0]; F->m[3 + k] = v[1]; F->m[6 + k] = v[2]; }
static double dot3(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const double *a, const double *b, double *r) { r[0] = a[1] * b[2] - a[2] * b[1]; r[1] = a[2] * b[0] - a[0] * b[2]; r[2] = a[0] * b[1] - a[1] * b[0]; }
static void norm3(double *a) { double l = sqrt(dot3(a, a)); if (l > 1e-300) { a[0] /= l; a[1] /= l; a[2] /= l; } }
static void rot_vec(const double *ax, double ang, const double *v, double *r) {   /* Rodrigues, about the unit axis ax */
    double c = cos(ang), s = sin(ang), k[3], d = dot3(ax, v); cross3(ax, v, k);
    for (int i = 0; i < 3; i++) r[i] = v[i] * c + k[i] * s + ax[i] * d * (1 - c);
}
static double gn_ppu(void) { return vht > 0 ? view_h / vht : 1; }   /* PvcToDc(0,0) x pipe+0xc0: model units -> pixels */
double gn_scale(void) {            /* Tessellate GD l.448-460 (k = 1: SetSize / state != 0) */
    int sz = gn_size < 40 ? 40 : gn_size;                    /* SetSize: < 40 -> 40 (GD l.2077) */
    return sz * 2.65 / gn_ppu();
}
static double V_Z(int i) { return vmat[6 + i]; }             /* the view's Z (towards the viewer) */

/* the cursor's view ray: a point well in front of the gnomon and the direction into the screen */
static void gn_ray(float mx, float my, const GnMat *F, double O[3], double dir[3]) {
    double vx, vy; view_px_to_view(mx - vp_x, my - vp_y, &vx, &vy);
    double z = vmat[6] * F->t[0] + vmat[7] * F->t[1] + vmat[8] * F->t[2] + 1000 * gn_scale();
    view_to_world3(vx, vy, z, O);
    for (int i = 0; i < 3; i++) dir[i] = -V_Z(i);
}
/* MtVector2d::Project (tol 5e-5): the ray onto the plane (o, n); 0 = edge-on */
static int ray_plane(const double O[3], const double d[3], const double o[3], const double n[3], double q[3]) {
    double dn = dot3(d, n); if (fabs(dn) < 5e-5) return 0;
    double w[3] = { o[0] - O[0], o[1] - O[1], o[2] - O[2] }, t = dot3(w, n) / dn;
    for (int i = 0; i < 3; i++) q[i] = O[i] + d[i] * t;
    return 1;
}

/* ---------------------------------------------------------------- view-dependent masks (Select GS l.95-119) */
/* a = |axis . view Z| per gnomon axis: > 0.995 -> its shaft / head out (hit bits 0x82 / 0x104 / 0x208), <= 0.1 -> the arc
   about it out (0x145000 / 0x218400 / 0xa2800); PrepDrawMasks (GD l.1691) moves the same parts to the faint pass */
static unsigned gn_vis(const GnMat *F) {
    unsigned m = 0xffffffffu;
    for (int k = 0; k < 3; k++) {
        double a[3]; col(F, k, a); norm3(a); double v = fabs(a[0] * V_Z(0) + a[1] * V_Z(1) + a[2] * V_Z(2));
        static const unsigned AX[3] = { 0xffffff7du, 0xfffffefbu, 0xfffffdf7u }, ARC[3] = { 0xffebafffu, 0xffde7bffu, 0xfff5d7ffu };
        if (v > 0.1) { if (v > 0.995) m &= AX[k]; } else m &= ARC[k];
    }
    return m;
}

/* ---------------------------------------------------------------- hit test (GrGnomon::Select GS l.95-175) */
static int tri_hit(const double O[3], const double d[3], const double *a, const double *b, const double *c, double *t) {   /* GrTriangleData::Intersect */
    double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] }, p[3], q[3], s[3];
    cross3(d, e2, p); double det = dot3(e1, p); if (fabs(det) < 1e-18) return 0;
    double inv = 1 / det; for (int i = 0; i < 3; i++) s[i] = O[i] - a[i];
    double u = dot3(s, p) * inv; if (u < 0 || u > 1) return 0;
    cross3(s, e1, q); double v = dot3(d, q) * inv; if (v < 0 || u + v > 1) return 0;
    *t = dot3(e2, q) * inv; return *t >= 0;
}
static void mesh_pt(const GnMat *F, double s, const float *v, double out[3]) {
    double l[3] = { v[0] * s, v[1] * s, v[2] * s };
    for (int i = 0; i < 3; i++) out[i] = F->m[3 * i] * l[0] + F->m[3 * i + 1] * l[1] + F->m[3 * i + 2] * l[2] + F->t[i];
}
int gn_pick(float mx, float my) {
    if (!gn_on) return 0;
    const GnMat *F = &gn_C; double s = gn_scale(), O[3], d[3]; gn_ray(mx, my, F, O, d);
    unsigned vis = gn_vis(F); int best = 0; double bt = 0;
    for (int k = 0; k < 19; k++) {
        int id = GN_SEL[k].id;
        if (!(GN_SEL[k].hit & vis) || !gn_edit_mode(id) || !gn_enabled(id)) continue;
        if (id == 0x12 && best) break;                       /* the planes only when nothing else was hit (GS l.155) */
        for (int i = 0; i + 2 < GN_SEL[k].n; i += 3) {
            double a[3], b[3], c[3], t; mesh_pt(F, s, GN_SEL[k].v + 6 * i, a); mesh_pt(F, s, GN_SEL[k].v + 6 * (i + 1), b); mesh_pt(F, s, GN_SEL[k].v + 6 * (i + 2), c);
            if (tri_hit(O, d, a, b, c, &t) && (!best || t < bt)) { best = id; bt = t; }   /* strictly nearer wins */
        }
    }
    return best;
}
void gn_set_hover(int id) { if (id != gn_hover) { gn_hover = id; overlay_dirty = 1; } }   /* Highlight */

/* ---------------------------------------------------------------- the ruler step (ScaleRuler RC l.2313 + DrawRuler RC l.620-647) */
static double ruler_step(void) {
    int metric = mcam_is_metric(&F);
    double base = metric ? 0.1 : 0.125;                      /* +0x1c8: metric 0.1 of mm (LAB_10e6d309), inch 1/8 in (the +0xe8 != 2 branch) */
    int sub = metric ? 10 : 2, per = metric ? 5 : 8;         /* +0x1dc subdivision, +0x1d8 */
    double f = gn_ruler_size / 29.0; if (f < 1) f = 1;       /* +0x274 [spec: ruler size / 29, min 1] */
    double p = f * per, ppu = gn_ppu(), st = base;
    if (st * ppu < p) { do st *= sub; while (st * ppu < p); }                 /* 0x1f0: grow while the step is under p pixels */
    else if (st * ppu > p * sub) { do st /= sub; while (st * ppu > p * sub); }   /* 0x1f1: shrink while over p x subdivision */
    return st;
}
/* GetRulerText (RC l.2693): "%.Nlf", N = the step's decimals (+1 metric), trailing zeros dropped down to one after '.' */
static void ruler_text(double v, char *out, int n) {
    double st = ruler_step(); char b[64]; snprintf(b, sizeof b, "%.15f", st); int L = (int)strlen(b); b[L - 1] = '0';
    char *dp = strchr(b, '.'); int dec = 0;
    if (dp) { int i = L - 2; char last = b[i]; while (i > (int)(dp - b) && b[i - 1] == last) i--; dec = i - (int)(dp - b); if (last == '0') dec--; if (dec < 0) dec = 0; }
    if (mcam_is_metric(&F)) dec++;
    snprintf(out, n, "%.*f", dec, v);
    char *d2 = strchr(out, '.');
    if (d2) { int e = (int)strlen(out) - 1; while (e > (int)(d2 - out) + 1 && out[e] == '0') out[e--] = 0; }
}

/* ---------------------------------------------------------------- drags (GrGnomon::Drag GA l.194) */
GnMat gn_frame(void) { return gn_drag ? gn_D : gn_C; }
void gn_begin(int id, float mx, float my) {
    gn_E = gn_D = gn_C; gn_drag = id; gn_mode = gn_edit_mode(id); gn_kind = 0; gn_d = gn_a = 0; gn_snapped3d = 0;
    gn_mx = mx; gn_my = my; gn_mouse = 1;
    int k = gn_edit_kind(gn_mode);
    if (k == 2 || k == 4) {
        double O[3], d[3], o[3] = { gn_E.t[0], gn_E.t[1], gn_E.t[2] }; gn_ray(mx, my, &gn_E, O, d);
        if (k == 2) {                                        /* the ruler's start = the grab point [inferred] */
            int ax = gn_mode - 2; double a[3], n[3], q[3]; col(&gn_E, ax, a);
            double b1[3], b2[3]; col(&gn_E, (ax + 1) % 3, b1); col(&gn_E, (ax + 2) % 3, b2);
            memcpy(n, fabs(dot3(b1, d)) > fabs(dot3(b2, d)) ? b1 : b2, sizeof n);
            gn_d0 = ray_plane(O, d, o, n, q) ? (q[0] - o[0]) * a[0] + (q[1] - o[1]) * a[1] + (q[2] - o[2]) * a[2] : 0;
        } else {
            int ax = rot_axis(gn_mode); double a[3], u[3], v[3], q[3]; col(&gn_E, ax, a); col(&gn_E, (ax + 1) % 3, u); col(&gn_E, (ax + 2) % 3, v);
            gn_a0 = ray_plane(O, d, o, a, q) ? atan2(dot3((double[]){ q[0] - o[0], q[1] - o[1], q[2] - o[2] }, v), dot3((double[]){ q[0] - o[0], q[1] - o[1], q[2] - o[2] }, u)) / D2R : 0;
        }
    }
    overlay_dirty = 1;
}
/* the drag frame for a translation of d along axis ax (ApplyDrag: C := Translation(+0x500) C, +0x500 = axis x d) */
static void set_trans(int ax, double d) { double a[3]; col(&gn_E, ax, a); gn_D = gn_E; for (int i = 0; i < 3; i++) gn_D.t[i] += a[i] * d; gn_d = d; gn_kind = 1; }
/* a rotation of ang deg about the gnomon axis ax through its origin (C := CompassRot C) */
static void set_rot(int ax, double ang) {
    double a[3]; col(&gn_E, ax, a); norm3(a); gn_D = gn_E;
    for (int k = 0; k < 3; k++) { double c[3], r[3]; col(&gn_E, k, c); rot_vec(a, ang * D2R, c, r); setcol(&gn_D, k, r); }
    gn_a = ang; gn_kind = 2;
}
static void set_origin(const double p[3]) { gn_D = gn_E; memcpy(gn_D.t, p, sizeof gn_D.t); gn_kind = 3; }
/* an AutoCursor point at the mouse (SelXformGnomon: Get3dSnap 2 -> the app's snap), *p in / out */
static int ac_point(float mx, float my, double p[3]) {
    V3 w = { p[0], p[1], p[2] };
    geo_snap_ptr(mx, my); int sn = geo_snap(&w, NULL, -1, 1); geo_snap_ptr_off();
    if (sn) { p[0] = w.x; p[1] = w.y; p[2] = w.z; }
    return sn != 0;
}
/* DragOrigin (GA l.556): an AutoCursor 3-D point as is, else the screen point projected onto the gnomon's XY plane */
static int drag_origin(float mx, float my) {
    double O[3], d[3], n[3], q[3]; gn_ray(mx, my, &gn_E, O, d); col(&gn_E, 2, n);
    int on = ray_plane(O, d, gn_E.t, n, q);
    double p[3] = { q[0], q[1], q[2] };
    if (!on) { double c[3]; cui_cursor_world(mx, my, c, p); }   /* (the snap search still needs a start point) */
    if (ac_point(mx, my, p)) { gn_snapped3d = 1; set_origin(p); return 1; }
    gn_snapped3d = 0;
    if (!on) return 0;                                       /* Project failed: no change */
    set_origin(q); return 1;
}
int gn_move(float mx, float my) {
    if (!gn_drag) return 0;
    gn_mx = mx; gn_my = my; gn_mouse = 1;
    int k = gn_edit_kind(gn_mode); double O[3], d[3]; gn_ray(mx, my, &gn_E, O, d);
    double o[3] = { gn_E.t[0], gn_E.t[1], gn_E.t[2] };
    if (k == 1) drag_origin(mx, my);
    else if (k == 2) {                                       /* GrRuler::DragRuler (RC l.1473) */
        int ax = gn_mode - 2; double a[3], b1[3], b2[3], n[3], q[3]; col(&gn_E, ax, a); col(&gn_E, (ax + 1) % 3, b1); col(&gn_E, (ax + 2) % 3, b2);
        int b1face = fabs(dot3(b1, d)) > fabs(dot3(b2, d));   /* the s_PlaneDef plane containing the axis that faces the view most */
        memcpy(n, b1face ? b1 : b2, sizeof n);
        double p[3]; int sn = 0;
        if (ray_plane(O, d, o, n, q)) {
            double w[3] = { q[0] - o[0], q[1] - o[1], q[2] - o[2] }, along = dot3(w, a), h = dot3(w, b1face ? b2 : b1), dd = along - gn_d0;
            memcpy(p, q, sizeof p);
            if ((sn = ac_point(mx, my, p))) dd = (p[0] - o[0]) * a[0] + (p[1] - o[1]) * a[1] + (p[2] - o[2]) * a[2];   /* a 3-D point projects exactly */
            else if (gn_ruler_on) {
                double qq = gn_ppu() / (gn_ruler_size * 8.44), lo = 0.03429 / qq, hi = 0.10287 / qq;   /* the band (RC l.1833-1838) */
                if (fabs(h) >= lo && fabs(h) <= hi) {           /* inside: rounded to the minor step (RC l.1922-1931) */
                    double st = ruler_step(); dd = dd >= 0 ? floor((st * 0.5 + dd) / st) * st : ceil((dd - st * 0.5) / st) * st;
                }
            }
            gn_snapped3d = sn; set_trans(ax, dd);
        }
    } else if (k == 3) {                                     /* DragAxis (GA l.387): a picked line + a 3-D point, else DragOrigin */
        int e = pick_entity(mx, my); double p[3]; double c[3]; cui_cursor_world(mx, my, c, p);
        if (e >= 0 && e < F.nents && F.ents[e].type == E_LINE && (gn_selmask & 1) && gn_mode <= 7 && ac_point(mx, my, p)) {
            const Ent *en = &F.ents[e]; double dir[3] = { en->b.x - en->a.x, en->b.y - en->a.y, en->b.z - en->a.z }; norm3(dir);
            int ax = gn_mode - 5; double cur[3]; col(&gn_E, ax, cur);
            if (dot3(dir, cur) < 0) for (int i = 0; i < 3; i++) dir[i] = -dir[i];
            double r[3]; cross3(cur, dir, r); double sl = sqrt(dot3(r, r)), cl = dot3(cur, dir);
            gn_D = gn_E;
            if (sl > 1e-12) { norm3(r); double ang = atan2(sl, cl); for (int k2 = 0; k2 < 3; k2++) { double v[3], w[3]; col(&gn_E, k2, v); rot_vec(r, ang, v, w); setcol(&gn_D, k2, w); } }
            memcpy(gn_D.t, p, sizeof gn_D.t); gn_snapped3d = 1; gn_kind = 0;
        } else drag_origin(mx, my);
    } else if (k == 4) {                                     /* GrCompass::DragRotation (RC l.3965) */
        int ax = rot_axis(gn_mode); double a[3], u[3], v[3], q[3]; col(&gn_E, ax, a); col(&gn_E, (ax + 1) % 3, u); col(&gn_E, (ax + 2) % 3, v);
        if (ray_plane(O, d, o, a, q)) {
            double p[3] = { q[0], q[1], q[2] }; int sn = ac_point(mx, my, p);
            double w[3] = { p[0] - o[0], p[1] - o[1], p[2] - o[2] };
            double ang = atan2(dot3(w, v), dot3(w, u)) / D2R - gn_a0;
            ang = fmod(ang, 360); if (ang < 0) ang += 360;  /* MtNormalizeAngle [inferred 0..360] */
            if (!sn && gn_ang_on && gn_ang_inc > 0) ang = floor((gn_ang_inc * 0.5 + ang) / gn_ang_inc) * gn_ang_inc;   /* (ours: everywhere, see the header) */
            gn_snapped3d = sn; set_rot(ax, ang);
        }
    }
    overlay_dirty = 1;
    return 1;
}
void gn_apply(void) { if (gn_drag) gn_C = gn_D; gn_drag = 0; gn_kind = 0; overlay_dirty = 1; }   /* ApplyDrag */
void gn_cancel(void) { gn_drag = 0; gn_kind = 0; overlay_dirty = 1; }                           /* EmCancelSelObjEdit: C kept */
/* tests (ours): the drag of item id set to a value, as a typed value would (SetEditValue EV: translation SetEditTranslation,
   rotation SetRotationAngles; origin SetEditOrigin); align: along line e with the origin at p */
void gn_set_value(int id, const double *v, int nv) {
    gn_begin(id, 0, 0); gn_mouse = 0;
    int k = gn_edit_kind(gn_mode);
    if (k == 1 || (k == 3 && gn_mode >= 8)) { if (nv >= 3) set_origin(v); }
    else if (k == 2) set_trans(gn_mode - 2, nv ? v[0] : 0);
    else if (k == 4) set_rot(rot_axis(gn_mode), nv ? v[0] : 0);
    else if (k == 3 && nv >= 1) {
        int e = (int)v[0]; if (e < 0 || e >= F.nents || F.ents[e].type != E_LINE) return;
        const Ent *en = &F.ents[e]; double dir[3] = { en->b.x - en->a.x, en->b.y - en->a.y, en->b.z - en->a.z }; norm3(dir);
        int ax = gn_mode - 5; double cur[3], r[3]; col(&gn_E, ax, cur); if (dot3(dir, cur) < 0) for (int i = 0; i < 3; i++) dir[i] = -dir[i];
        cross3(cur, dir, r); double sl = sqrt(dot3(r, r)), cl = dot3(cur, dir); gn_D = gn_E;
        if (sl > 1e-12) { norm3(r); double ang = atan2(sl, cl); for (int k2 = 0; k2 < 3; k2++) { double c[3], w[3]; col(&gn_E, k2, c); rot_vec(r, ang, c, w); setcol(&gn_D, k2, w); } }
        if (nv >= 4) { gn_D.t[0] = v[1]; gn_D.t[1] = v[2]; gn_D.t[2] = nv >= 4 ? v[3] : 0; }
        else { gn_D.t[0] = en->a.x; gn_D.t[1] = en->a.y; gn_D.t[2] = en->a.z; }
    }
}
void gn_show(int on) { gn_on = on; if (!on) { gn_drag = 0; gn_hover = 0; gn_kind = 0; } overlay_dirty = 1; }
void gn_reset(void) { gn_C = gm_id(); gn_show(0); }

/* ---------------------------------------------------------------- drawing (Tessellate GD l.293, DrawAxes GD l.717, DrawItems l.1131) */
/* colours, GrGnomon ctor GD l.97-143 (ColorIdx = +0x100 + 0x18 i): rgb + alpha */
static const unsigned char GN_COL[16][4] = {
    { 0x66, 0x66, 0x66, 0xff }, { 0xff, 0xff, 0, 0xff }, { 0x96, 0x96, 0x96, 0xff }, { 0xff, 0, 0, 0xff }, { 0, 0xaa, 0, 0xff },
    { 0x1a, 0x1a, 0xff, 0xff }, { 0xff, 0xff, 0xff, 0x32 }, { 0xff, 0xff, 0xff, 0x32 }, { 0xff, 0xff, 0xff, 0x32 }, { 0xff, 0xff, 0, 0x50 },
    { 0xff, 0xff, 0, 0xff }, { 0xff, 0, 0, 0xff }, { 0xff, 0, 0, 0xff }, { 0, 0xb4, 0, 0xff }, { 0, 0, 0, 0xff }, { 0xff, 0xff, 0xff, 0xff } };
static int hl(int id) { return gn_hover == id || gn_drag == id; }   /* GetHighlightMask: the highlighted item (kept during its drag) */
/* the colour index of an item (DrawAxes GD l.836-1098): origin 2, X / Y / Z 3 / 4 / 5, planes 6 / 7 / 8, the highlighted item 1
   (planes 9); a highlighted arc end lights the axis it lies next to (0xc / 0xf -> X, 9 / 0x11 -> Y, 0xb / 0xe -> Z) */
static int item_color(int id) {
    if (id == 2) return hl(2) ? 1 : 2;
    if (id >= 0x12) return hl(id) ? 9 : 6 + (id - 0x12);
    int ax = id <= 8 ? (id - 3) % 3 : (id - 9) / 3;          /* shafts 3-5 / heads 6-8, arcs 9-11 X, 12-14 Y, 15-17 Z */
    if (hl(id)) return 1;
    if (id <= 8) { static const int A[3][2] = { { 0xc, 0xf }, { 9, 0x11 }, { 0xb, 0xe } }; if (hl(A[ax][0]) || hl(A[ax][1])) return 1; }
    return 3 + ax;
}
static float *gt; static int gnt, gcapt; static float *gl; static int gnl, gcapl;
static void put_t(const double p[3], const double n[3], const float c[4]) {
    if (gnt == gcapt) { gcapt = gcapt ? gcapt * 2 : 4096; gt = realloc(gt, sizeof(float) * 10 * gcapt); }
    float *q = gt + 10 * gnt++; for (int i = 0; i < 3; i++) { q[i] = (float)p[i]; q[3 + i] = (float)n[i]; } memcpy(q + 6, c, 16);
}
static void put_l(const double p[3], const float c[3]) {
    if (gnl == gcapl) { gcapl = gcapl ? gcapl * 2 : 512; gl = realloc(gl, sizeof(float) * 6 * gcapl); }
    float *q = gl + 6 * gnl++; for (int i = 0; i < 3; i++) { q[i] = (float)p[i]; q[3 + i] = c[i]; }
}
static void draw_item(const GnMat *Fm, double s, int k, double alpha) {
    int id = GN_SEL[k].id, ci = item_color(id); const unsigned char *u = GN_COL[ci];
    float c[4] = { u[0] / 255.f, u[1] / 255.f, u[2] / 255.f, ci >= 6 && ci <= 9 ? u[3] / 255.f : (float)alpha };   /* SetProps GD l.1998 */
    for (int i = 0; i < GN_SEL[k].n; i++) {
        const float *v = GN_SEL[k].v + 6 * i; double p[3], n[3], ln[3] = { v[3], v[4], v[5] };
        mesh_pt(Fm, s, v, p);
        for (int r = 0; r < 3; r++) n[r] = Fm->m[3 * r] * ln[0] + Fm->m[3 * r + 1] * ln[1] + Fm->m[3 * r + 2] * ln[2];
        put_t(p, n, c);
    }
}
/* the two small circles on a shown arc (DrawAxes l.818-893, 960-1098): radius 0.007747 at 30 / 60 deg on radius 0.254, in the
   plane of the radius and the arc's axis, grey 0.75, width 1, on the full-alpha pass */
static void arc_marks(const GnMat *Fm, double s, int ax) {
    double a[3], u[3], v[3]; col(Fm, ax, a); col(Fm, (ax + 1) % 3, u); col(Fm, (ax + 2) % 3, v);
    const float g[3] = { 0.75f, 0.75f, 0.75f };
    for (int j = 1; j <= 2; j++) {
        double th = j * 30 * D2R, r[3], c[3], prev[3];
        for (int i = 0; i < 3; i++) { r[i] = cos(th) * u[i] + sin(th) * v[i]; c[i] = Fm->t[i] + 0.254 * s * r[i]; }
        for (int q = 0; q <= 24; q++) {                       /* (24 chords: ours) */
            double ph = 2 * M_PI * q / 24, p[3];
            for (int i = 0; i < 3; i++) p[i] = c[i] + 0.007747 * s * (cos(ph) * r[i] + sin(ph) * a[i]);
            if (q) { put_l(prev, g); put_l(p, g); }
            memcpy(prev, p, sizeof prev);
        }
    }
}
void gn_upload(void) {
    gnt = gnl = 0;
    if (gn_on && vht > 0) {
        double s = gn_scale(); GnMat Fm = gn_frame(), Ff = gn_drag ? gn_E : gn_C;
        unsigned vis = gn_vis(&Fm);
        for (int k = 0; k < 19; k++) {
            int id = GN_SEL[k].id; if (!gn_enabled(id)) continue;
            int main = (GN_SEL[k].hit & vis) && id < 0x12;       /* PrepDrawMasks: planes and hidden parts on the faint pass */
            if (main) draw_item(&Fm, s, k, 1.0);
            else draw_item(&Ff, s, k, 0.1);                      /* +0xf98: alpha 0.1 (DrawAxes l.812) */
        }
        for (int ax = 0; ax < 3; ax++) {                         /* the arcs' marks: X arc hit 0x1000, Y 0x400, Z 0x800 */
            static const unsigned AH[3] = { 0x1000, 0x400, 0x800 };
            if (vis & AH[ax]) arc_marks(&Fm, s, ax);
        }
    }
    scene_set_gnomon(gt, gnt, gl, gnl);
}

/* ---------------------------------------------------------------- texts for the front end: the labels and the read-out */
static double label_h, label_pos;  /* s_LabelHeight / s_LabelPos (GrGnomon ctor GD l.244-255) */
static void label_consts(void) {
    if (label_h > 0) return;
    float lo = 1e30f, hi = -1e30f; for (int i = 0; i < GM_LABEL_GLYPH_N; i++) { float y = GM_LABEL_GLYPH[6 * i + 1]; if (y < lo) lo = y; if (y > hi) hi = y; }
    label_h = (hi - lo) * 0.3;                               /* the "X" glyph's Y extent x 0.3 */
    float mx = -1e30f; for (int i = 0; i < GM_110ca010_N; i++) if (GM_110ca010[6 * i] > mx) mx = GM_110ca010[6 * i];
    label_pos = mx;                                          /* s_XPointBox max x: the X arrow tip */
}
int gn_texts(GnText *t, int max) {
    int n = 0; if (!gn_on || vht <= 0) return 0;
    label_consts();
    double s = gn_scale(); GnMat Fm = gn_frame(); unsigned vis = gn_vis(&Fm);
    static const unsigned AXH[3] = { 0x2, 0x4, 0x8 };          /* a label goes with its shaft (PrepDrawMasks 0x402 / 0x804 / 0x1008) */
    for (int ax = 0; ax < 3 && n < max; ax++) {                /* DrawLabel GD l.1546: dir x (len x H x 0.5 + Pos + H x 0.4), Tahoma */
        double a[3], p[3]; col(&Fm, ax, a); double L = (1 * label_h * 0.5 + label_pos + label_h * 0.4) * s;
        for (int i = 0; i < 3; i++) p[i] = Fm.t[i] + a[i] * L;
        float x, y; if (!scene_to_screen(view_w, view_h, p[0], p[1], p[2], &x, &y)) continue;
        GnText *g = &t[n++]; memset(g, 0, sizeof *g);
        g->x = x; g->y = y; g->px = (float)(label_h * s * gn_ppu()); g->bold = gn_size < 41;
        const unsigned char *c = GN_COL[3 + ax]; double al = (vis & AXH[ax]) ? 1.0 : 0.1;
        g->rgba[0] = c[0]; g->rgba[1] = c[1]; g->rgba[2] = c[2]; g->rgba[3] = (unsigned char)(al * 255);
        snprintf(g->s, sizeof g->s, "%c", "XYZ"[ax]);
    }
    if (gn_drag && gn_kind && gn_mouse && n < max) {           /* the GrEdit read-out at the cursor + (24, -24), black on white */
        GnText *g = &t[n]; memset(g, 0, sizeof *g);
        if (gn_kind == 1) ruler_text(gn_d, g->s, sizeof g->s);
        else if (gn_kind == 2) snprintf(g->s, sizeof g->s, "%.2f\xc2\xb0", gn_a);   /* "%.2lf%c" 0xb0 */
        else snprintf(g->s, sizeof g->s, "%.3f, %.3f, %.3f", gn_D.t[0], gn_D.t[1], gn_D.t[2]);   /* "%.3lf, %.3lf, %.3lf" */
        g->x = gn_mx + 24; g->y = gn_my - 24; g->box = 1; g->rgba[3] = 255;
        n++;
    }
    return n;
}
/* the read-out's value (tests) */
double gn_value(int *kind) { if (kind) *kind = gn_kind; return gn_kind == 1 ? gn_d : gn_kind == 2 ? gn_a : 0; }
