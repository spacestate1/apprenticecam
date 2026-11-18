/* cui_xform.c - the Transform ribbon tab (Mastercam 2017 MCCADUTIL.DLL do_xform_* / TransformDistribute over mccore's Xf*
 * machinery), shared by the front ends. Spec: ../../formats/wireframe.md "Transform"; dumps in ../../decomp/xform/:
 * mccadutil_xform.c (the entry points + callees), mccore_xform.c (XfDoFinalAction / XfCreatePreview / FUN_1808d8820 /
 * xform_store_ent / XfApplyColorLevelToResult_Index ...), mccore_xf_colors_join.c (find_color, FUN_1808e48b0 the join
 * lines, XfCheckVirgin, the config reader FUN_1801b7d30, ClearColors), trans_dlg_*.c / trans3d_array.c (CXfTransDlg,
 * CXfTrans3dDlg, CXfArrayDlg), rotate.c, mirror.c, scale.c, stretch_distribute.c, mastercam_dbselwin.c (Stretch's window),
 * roll*.c / distribute*.c (decoded, not ported).
 *
 * Common machinery (every command but Move to Origin / Roll):
 *  - select_ents(prompt, mask 0x1f1fff) first (preselected entities are taken at once, MC_PRESEL 2), then the MFC dialog
 *    (CXf*Dlg, mccadutilres templates) with the preview: panel code 0x66 rebuilds it (XfDeletePreview + XfCreatePreview),
 *    0x65 (OK / Apply) = XfDoFinalAction + UndoXformTransCommit, 100 (Cancel) = XfDeletePreview; Apply runs select_ents
 *    again (the loop on dlg +0x5091, do_xform_trans.c).
 *  - XfCreatePreview(type, MoveCopyJoin, ...) -> FUN_1808d8820: MoveCopyJoin 0 Move (originals deleted: the Move radio's
 *    tooltip "Delete original geometry"; Mastercam rewrites them in place, keeping their ids - ours: replaced, as the
 *    other edit commands here), 1 Copy, 2 Join (+ FUN_1808e48b0's connecting lines). One XfInfo entry per copy, each with
 *    its own matrix; the copies keep the originals' level / colour / attributes (FUN_1808d8820 param_7 = 1 ->
 *    DAT_18143d6e8 -> xform_store_ent l.1260-1268). The originals get the group bit 2, the results the result bit 8
 *    (xform_store_ent l.1350-1352, set_clr_sel_bits_all(4, 2, ...) l.1843); find_color (mccore 0x1802e72d0) draws bit 2 in
 *    Group color (config 5010) and bit 8 in Result color (5031) while config 5044 "Use Group and Result color in Xform?"
 *    is on (use_group_color / use_result_color = 1, mastercam.dll .data 0x18087b7e9 / 0x18087b7ea); Clear Colors
 *    (mastercam.dll ClearColors 0x180272880) = set_clr_sel_bits_all(0, 0xe, ...): bits 2 / 4 / 8 off on every entity.
 *  - Use New Attributes: XfApplyColorLevelToResult_Index(colour, level, IncrLevels): every result of XfInfo entry k gets
 *    the colour and level (+ k with IncrLevels); level < 1 = main level, colour < 0 = main colour (FUN_180045f00).
 *  - Preview check box = XfPreview (config 10026 "Preview?", mccore FUN_1801b7d30 case 0x272a; XfCheckVirgin); Regen is
 *    enabled while it is off.
 *
 * Ours / deviations: the parameters live for the session (Mastercam writes XFORM.PRM; every do_xform_* resets most
 * fields at its start anyway - kept); the copies are new entities (new ids) and a Move replaces the originals: the first copy keeps
 * the original's id and its operations (mod_pair, formats/align_geo.md 2.2), which get dirty; the marks (group / result bits) are not
 * saved with the part; the dialog bitmaps' labels are NOT DECODED (the decoded parameter names are shown); Fit, Help,
 * the on-screen handles (SelObjHelper) and the plane dialogs are greyed. Solids (the extrude
 * features here are rebuilt from their chains; Mastercam's sld_xform_solid is not ported) and toolpaths are not
 * transformed: points, lines, arcs, spline curves (made new E_SPLINE records, mcsb_xform_curve) and surfaces of kinds 0 / 1 / 10
 * are. An arc whose transformed plane is neither XY nor one of the seven system views would need a new view (AddMCView in
 * FUN_1808d1300): refused with a message. A non-uniform scale makes a NURBS spline of an arc (convert_arc_to_nb + put_nurbs_in_db,
 * mcsb_arc_nurbs_rec). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "core_ui.h"
#include "mcsurf_build.h"

#define XF_TOL (mc_systol.tol)                          /* tol_exref (system tolerance: config 1000, mcam_load.h mc_systol) */
#define D2R (M_PI / 180)

/* ---------------------------------------------------------------- matrices: p' = m p + t (m row-major) */
typedef struct { double m[9], t[3]; } XM;
static XM xm_id(void) { XM a = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 0, 0, 0 } }; return a; }
static XM xm_mul(const XM *a, const XM *b) {            /* a after b */
    XM r; for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) r.m[3 * i + j] = a->m[3 * i] * b->m[j] + a->m[3 * i + 1] * b->m[3 + j] + a->m[3 * i + 2] * b->m[6 + j];
        r.t[i] = a->m[3 * i] * b->t[0] + a->m[3 * i + 1] * b->t[1] + a->m[3 * i + 2] * b->t[2] + a->t[i]; }
    return r;
}
static V3 xm_pt(const XM *a, V3 p) { return (V3){ a->m[0] * p.x + a->m[1] * p.y + a->m[2] * p.z + a->t[0], a->m[3] * p.x + a->m[4] * p.y + a->m[5] * p.z + a->t[1], a->m[6] * p.x + a->m[7] * p.y + a->m[8] * p.z + a->t[2] }; }
static V3 xm_vec(const XM *a, V3 p) { return (V3){ a->m[0] * p.x + a->m[1] * p.y + a->m[2] * p.z, a->m[3] * p.x + a->m[4] * p.y + a->m[5] * p.z, a->m[6] * p.x + a->m[7] * p.y + a->m[8] * p.z }; }
static XM xm_trans(double x, double y, double z) { XM a = xm_id(); a.t[0] = x; a.t[1] = y; a.t[2] = z; return a; }
static XM xm_inv(const XM *a) {                         /* MtTmat3d::Inverse (the 3 x 3 part by cofactors) */
    const double *m = a->m; XM r; double c[9] = { m[4] * m[8] - m[5] * m[7], m[2] * m[7] - m[1] * m[8], m[1] * m[5] - m[2] * m[4],
                                                 m[5] * m[6] - m[3] * m[8], m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
                                                 m[3] * m[7] - m[4] * m[6], m[1] * m[6] - m[0] * m[7], m[0] * m[4] - m[1] * m[3] };
    double det = m[0] * c[0] + m[1] * c[3] + m[2] * c[6]; if (fabs(det) < 1e-300) return xm_id();
    for (int i = 0; i < 9; i++) r.m[i] = c[i] / det;
    for (int i = 0; i < 3; i++) r.t[i] = -(r.m[3 * i] * a->t[0] + r.m[3 * i + 1] * a->t[1] + r.m[3 * i + 2] * a->t[2]);
    return r;
}
static XM xm_pow(const XM *a, int k) { XM r = xm_id(); for (int i = 0; i < k; i++) r = xm_mul(a, &r); return r; }   /* mul_xmatrix repeated */
static double v_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 v_cross(V3 a, V3 b) { return (V3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static double v_len(V3 a) { return sqrt(v_dot(a, a)); }
static V3 v_scale(V3 a, double s) { return (V3){ a.x * s, a.y * s, a.z * s }; }

/* ---------------------------------------------------------------- the group / result marks */
int cfg_group_color = 12, cfg_result_color = 13, cfg_xf_grpres = 1;   /* config 5010 / 5031 / 5044 (mastercam.dll .data 0x18087b853 = 12,
                                                                         0x18087b868 = 13, 0x18087b7eb = 1; read by mccore FUN_1801d3400) */
int cfg_xf_preview = 1;                                /* config 10026 "Preview?" (XfPreview, mccore .data 0x181312ec6 = 1) */
static unsigned char *xf_bits; static int xf_nbits;   /* per entity: 2 group, 8 result (the ent sel bits) */
static void xf_bits_grow(void) {
    if (xf_nbits >= F.nents) return;
    unsigned char *b = calloc(F.nents + 64, 1); if (!b) return;
    if (xf_bits) memcpy(b, xf_bits, xf_nbits); free(xf_bits); xf_bits = b; xf_nbits = F.nents + 64;
}
static void xf_mark(int e, int bit) { if (e < 0) return; xf_bits_grow(); if (e < xf_nbits) xf_bits[e] = (unsigned char)((xf_bits[e] & ~10) | bit); }
/* per result entity: its XfInfo entry + 1 (FUN_1808eb180: ent +0x50 = XfInfoIndex + 1 while XfInfoApplyPartNumber is set - Rotate /
   Rectangular Array set it, MX l.926 / l.1419), what XfWfMgrSelectCopy reads from a clicked entity (gaps/core_removed.c) */
static int *xf_part; static int xf_npart;
static void xf_set_part(int e, int k) {
    if (e < 0) return;
    if (e >= xf_npart) { int n = e + 256; int *q = realloc(xf_part, sizeof(int) * (size_t)n); if (!q) return; memset(q + xf_npart, 0, sizeof(int) * (size_t)(n - xf_npart)); xf_part = q; xf_npart = n; }
    xf_part[e] = k;
}
static void xf_marks_clear(void) { if (xf_bits) memset(xf_bits, 0, xf_nbits); overlay_dirty = 1; }   /* XfClearBITS(0, 2, 8, ...) */
void xf_clear_colors(void) { xf_marks_clear(); sel_clear(); overlay_dirty = 1; }   /* ClearColors: bits 2 / 4 / 8 off */
void xf_reset_doc(void) { free(xf_bits); xf_bits = NULL; xf_nbits = 0; }
/* the marked entities in the group / result colour (find_color: only while config 5044 is on) */
void xf_ov(float **pv, int *pn, int *pcap) {
    if (!cfg_xf_grpres || !xf_bits) return;
    for (int e = 0; e < F.nents && e < xf_nbits; e++) {
        if (!xf_bits[e] || !F.ents[e].type || (ent_sel && e < ent_sel_n && ent_sel[e])) continue;   /* bit 4 (selected) first */
        float rgb[3] = { 1, 1, 1 }; mc_rgb(xf_bits[e] & 2 ? cfg_group_color : cfg_result_color, rgb);
        ov_entity_pat(pv, pn, pcap, &F.ents[e], 0xffff, rgb);
    }
}
int xf_ent_mark(int e) { return xf_bits && e >= 0 && e < xf_nbits ? xf_bits[e] : 0; }
void xf_mark_ent(int e, int bit) { xf_mark(e, bit); overlay_dirty = 1; }   /* sel_bit_turn_on(2 | 8) with the other one off */

/* ---------------------------------------------------------------- one entity through a matrix */
/* the plane frame of an arc: x / y axes (vw = 0: world X / Y) */
static void arc_frame(const Ent *en, V3 *X, V3 *Y) {
    if (en->vw) { *X = (V3){ en->vm[0], en->vm[1], en->vm[2] }; *Y = (V3){ en->vm[3], en->vm[4], en->vm[5] }; }
    else { *X = (V3){ 1, 0, 0 }; *Y = (V3){ 0, 1, 0 }; }
}
static int xf_refused, xf_refused_nb;                  /* arcs left out of the last build (all; those a non-uniform scale makes splines of) */
/* the xform_type a command hands xform_store_ent (mccadutil do_xform_*: the dialogs' +0x133e4 / +0xda24 / +0x16d0c / +0x117e4 / +0x2f0e,
   decomp/xform/mccadutil_xform.c): Mirror 0; a linear part of identity (a translation, a 0 deg rotation, a scale of 1, Translate 3D
   between parallel planes) 3; Scale 2; Rotate / Translate 3D with a rotation 1 */
static int xf_c;                                         /* (the running command, declared with the others below) */
static int xf_force_type;
static int xf_xtype(const XM *M) {
    if (xf_force_type >= 0) return xf_force_type;
    if (xf_c == G_XMIRROR) return 0;
    int id = 1; for (int i = 0; i < 9; i++) if (fabs(M->m[i] - (i % 4 == 0)) > 1e-12) id = 0;
    if (id) return 3;
    return xf_c == G_XSCALE ? 2 : 1;
}
/* a surface through M (cui_surf.c / mcsurf_build.c mcsb_xform: kinds 0 / 1 / 10; a trimmed surface's base first, as a new blanked
   surface); -1 = not made (the other kinds transform their curves: NOT PORTED) */
static int xf_surf(int e, const XM *M, int lv, int co) {
    const Surf *sf = &F.surfs[F.ents[e].srf]; int ty = xf_xtype(M); int64_t nb = 0; uint32_t n = sf->n;
    if (sf->kind == 10) {
        const Surf *b = mcam_surf_base(&F, sf); if (!b) { xf_refused++; return -1; }
        uint8_t *br = mcsb_xform(&F, b->rec, b->n, M->m, M->t, ty, 0); if (!br) { xf_refused++; return -1; }
        int be = b->ent >= 0 ? b->ent : -1;
        int bk = mcam_new_surf(&F, br, b->n, be >= 0 ? F.ents[be].level : lv, be >= 0 ? F.ents[be].color : co); free(br);
        Surf *ns = &F.surfs[F.ents[bk].srf]; ns->blanked = 1; ns->rec[5] |= 1; F.ents[bk].blanked = 1;   /* sel bit 0x100: hidden, as every base */
        nb = F.ents[bk].septr; sf = &F.surfs[F.ents[e].srf];   /* (F.surfs may have moved) */
    }
    uint8_t *r = mcsb_xform(&F, sf->rec, n, M->m, M->t, ty, nb); if (!r) { xf_refused++; return -1; }
    int k = mcam_new_surf(&F, r, n, lv, co); free(r);
    scene_surfs_update(0);
    return k;
}
/* DAT_18143d668: FUN_1808d8820's param_11 (XfCreatePreview: the p_3d argument's x for a scale - the Scale dialog's factor, +0x16d10;
   fmu_scale_db: the units ratio, XMM6 at 0x1808d384b), multiplied by itself at every pass (l.2908) - only a scale (type 2) uses it */
static double xf_fac = 1.0;
/* XfScaleFromMatrix (mccore 0x1808e1040): a non-uniform scale = the diagonals differ by more than 1e-8 (asked for type 2 only) */
static int xf_nonuni(const XM *M) { return fabs(M->m[0] - M->m[4]) > 1e-8 || fabs(M->m[4] - M->m[8]) > 1e-8 || fabs(M->m[8] - M->m[0]) > 1e-8; }
static int xf_nbarcs;                                   /* arcs made NURBS splines by the last build */
/* a spline curve (E_SPLINE) / an arc as a NURBS through M (mcsurf_build.c mcsb_xform_curve: FUN_1808d7f00 / FUN_1808da030) */
static int xf_curve(const uint8_t *rec, uint32_t n, const XM *M, int lv, int co) {
    uint8_t *r = mcsb_xform_curve(rec, n, M->m, M->t, xf_xtype(M), xf_fac); if (!r) { xf_refused++; return -1; }
    int k = mcam_new_curve(&F, r, n, lv, co); free(r);
    if (k < 0) xf_refused++;
    return k;
}
/* ---- an arc's view after a rotation / mirror (decomp/xform/gaps: core_arc.c FUN_1808d20f0 / FUN_1808e07d0, core2.c FUN_1808d1300,
   core_removed.c XfGetArcPoints, mccore_xform.c XfKeepView / XfStdView, mc_views.c views_parallel / get_view_matrix). The views: the
   reserved 1..8 (mcam_std_view) and the part's own (F.fviews); n_views = the highest number. */
static int xv_rows(int v, double R[9]) {
    if (v >= 1 && v <= 8) return mcam_std_view(v, R);
    for (int i = 0; i < F.nfviews; i++) if (F.fviews[i].n == v) { memcpy(R, F.fviews[i].m, sizeof(double) * 9); return 1; }
    return 0;
}
static int xv_count(void) { int n = 8; for (int i = 0; i < F.nfviews; i++) if (F.fviews[i].n > n) n = F.fviews[i].n; return n; }
/* views_parallel (mastercam.dll 0x1803418c0): |the normals' dot| > 0.9999996; *anti = dot < -0.9999996 (the view's +0x60 vector: the
   normal [inferred]) */
static int xv_parallel(int a, int b, int *anti) {
    double A[9], B[9]; if (!xv_rows(a < 1 ? 1 : a, A) || !xv_rows(b < 1 ? 1 : b, B)) return 0;
    double d = A[6] * B[6] + A[7] * B[7] + A[8] * B[8]; *anti = d < -0.9999996; return fabs(d) > 0.9999996;
}
int mc_view_rows(int v, double R[9]) { return xv_rows(v, R); }          /* (core_ui.h) */
int mc_view_count(void) { return xv_count(); }
int mc_views_parallel(int a, int b, int *anti) { return xv_parallel(a, b, anti); }
typedef struct { V3 c; double r, a0, sw; int view; double R[9]; } XArc;   /* the a_3d being rebuilt: world centre, the view's rows */
static V3 xa_pt(const XArc *q, double t) { double ca = q->r * cos(t), sa = q->r * sin(t);
    return (V3){ q->c.x + ca * q->R[0] + sa * q->R[3], q->c.y + ca * q->R[1] + sa * q->R[4], q->c.z + ca * q->R[2] + sa * q->R[5] }; }
static V3 xa_w2v(const double R[9], V3 p) { return (V3){ R[0] * p.x + R[1] * p.y + R[2] * p.z, R[3] * p.x + R[4] * p.y + R[5] * p.z, R[6] * p.x + R[7] * p.y + R[8] * p.z }; }
/* XfGetArcPoints: start, the middle (FUN_1808e08f0 at 0.5) and end; a closed arc (start == end, pt_equal): the points at 1/3 and 2/3 */
static void xa_points(const XArc *q, V3 p[3]) {
    V3 s = xa_pt(q, q->a0), e = xa_pt(q, q->a0 + q->sw);
    int closed = fabs(s.x - e.x) < XF_TOL && fabs(s.y - e.y) < XF_TOL && fabs(s.z - e.z) < XF_TOL;
    p[0] = s;
    if (!closed) { p[1] = xa_pt(q, q->a0 + 0.5 * q->sw); p[2] = e; }
    else { p[1] = xa_pt(q, q->a0 + 0.3333333333333333 * q->sw); p[2] = xa_pt(q, q->a0 + 0.6666666666666666 * q->sw); }
}
/* FUN_1808e07d0: the arc in view v: start angle = atan2 of start - centre in v (0 under 1e-12), restrict_angle 0..2 pi, sweep negated
   when the views are antiparallel */
static void xa_reexpress(XArc *q, int v, int anti) {
    double R[9]; if (!xv_rows(v, R)) return;
    V3 s = xa_w2v(R, xa_pt(q, q->a0)), c = xa_w2v(R, q->c); double dx = s.x - c.x, dy = s.y - c.y, a = 0;
    if (dx * dx + dy * dy >= 1e-12) { a = atan2(dy, dx); if (a < 0) a += 2 * M_PI; }
    while (a > 2 * M_PI) a -= 2 * M_PI; while (a < 0) a += 2 * M_PI;
    q->a0 = a; q->view = v; memcpy(q->R, R, sizeof R); if (anti) q->sw = -q->sw;
}
/* the three points at one depth (within tol) in view v (XfKeepView / XfStdView, the depth argument 1e38: any depth) */
static int xa_flat_in(const XArc *q, int v) {
    double R[9]; if (!xv_rows(v, R)) return 0; V3 p[3]; xa_points(q, p);
    double z0 = xa_w2v(R, p[0]).z, z1 = xa_w2v(R, p[1]).z, z2 = xa_w2v(R, p[2]).z;
    return fabs(z0 - z1) <= XF_TOL && fabs(z1 - z2) <= XF_TOL && fabs(z2 - z0) <= XF_TOL;
}
static int xf_arc_views(const Ent *en, const XM *M, int *view, double vm[9], double *a0, double *sw) {
    int ov = en->vw ? en->vw : 1; double V[9], Rm[9];
    if (en->vw) memcpy(V, en->vm, sizeof V); else if (!xv_rows(1, V)) return 0;
    /* FUN_1808d1300(1, M, ov): R = V M - the view's rows through the linear part - each row normalised (FUN_18000f2f0) */
    for (int i = 0; i < 3; i++) { V3 r = xm_vec(M, (V3){ V[3 * i], V[3 * i + 1], V[3 * i + 2] }); double l = v_len(r);
        if (l == 0) r = (V3){ 0, 0, 0 }; else r = v_scale(r, 1 / l); Rm[3 * i] = r.x; Rm[3 * i + 1] = r.y; Rm[3 * i + 2] = r.z; }
    int eq = 1; for (int i = 0; i < 9; i++) if (fabs(V[i] - Rm[i]) > XF_TOL) eq = 0;   /* "tol <= |d| && |d| != tol": differs */
    XArc q = { xm_pt(M, en->c), en->r, en->a0, en->sw, ov, { 0 } }; memcpy(q.R, V, sizeof V);
    double ofs = 0; int flip = 0, nv = ov;
    if (!eq) {
        int cand = -1, nvw = xv_count();
        if (fabs(1 - (V[6] * Rm[6] + V[7] * Rm[7] + V[8] * Rm[8])) < 1e-10) cand = ov;   /* param_1 = 1: the same normal, same sense */
        nv = -1;
        for (int v = 1; v <= nvw && nv < 0; v++) {
            double W[9]; if (!xv_rows(v, W)) continue;
            int same = 1; for (int i = 0; i < 9; i++) if (fabs(W[i] - Rm[i]) > XF_TOL) same = 0;
            if (same) { nv = v; break; }                  /* the image is that view: offset 0, no flip */
            if (cand < 0 && fabs(1 - (Rm[6] * W[6] + Rm[7] * W[7] + Rm[8] * W[8])) < 1e-10) cand = v;
        }
        if (nv < 0) {
            if (cand < 0) return 0;                        /* AddMCView (mastercam.dll 0x18031df40: a new plane in MastercamPlanes): NOT PORTED */
            double W[9]; xv_rows(cand, W);
            double ex = Rm[0] * W[0] + Rm[1] * W[1] + Rm[2] * W[2], ey = Rm[0] * W[3] + Rm[1] * W[4] + Rm[2] * W[5];
            if (ex * ex + ey * ey >= 1e-12) { ofs = atan2(ey, ex); if (ofs < 0) ofs += 2 * M_PI; }
            V3 xy = v_cross((V3){ Rm[0], Rm[1], Rm[2] }, (V3){ Rm[3], Rm[4], Rm[5] });
            flip = xy.x * W[6] + xy.y * W[7] + xy.z * W[8] < 0;
            nv = cand;
        }
        if (!xv_rows(nv, q.R)) return 0;
        q.view = nv;
        if (!flip) q.a0 = en->a0 + ofs; else { q.a0 = ofs - en->a0; q.sw = -en->sw; }
        while (q.a0 > 2 * M_PI) q.a0 -= 2 * M_PI; while (q.a0 < 0) q.a0 += 2 * M_PI;   /* restrict_angle(0, 2 pi) */
    }
    /* XfKeepView(arc, the arc's old view, 1e38): parallel to it and flat in it -> back in that view; else XfStdView(arc, 1e38, false):
       the first view 1 .. max(view, 9) - 1 parallel and flat */
    int anti = 0;
    if (q.view != ov && xv_parallel(q.view, ov, &anti) && xa_flat_in(&q, ov)) xa_reexpress(&q, ov, anti);
    else {
        int top = q.view < 10 ? 9 : q.view;
        for (int v = 1; v < top; v++) if (xv_parallel(q.view, v, &anti) && xa_flat_in(&q, v)) { xa_reexpress(&q, v, anti); break; }
    }
    *view = q.view; memcpy(vm, q.R, sizeof q.R); *a0 = q.a0; *sw = q.sw;
    return 1;
}
/* entity e through M as a new entity with level lv / colour co; -1 = not made */
static int xf_ent(int e, const XM *M, int lv, int co) {
    const Ent *en = &F.ents[e]; int k = -1; V3 z0 = { 0, 0, 0 };
    const int ln_style = en->ln_style, pt_style = en->pt_style;   /* (copied: mcam_new_curve / mcam_new_ent grow F.ents, en is stale after them) */
    if (en->type == E_SURF) return xf_surf(e, M, lv, co);
    if (en->type == E_NOTE) return -1;                       /* notes: the transform of a drafting entity is NOT PORTED */
    if (en->type == E_MESH) { xf_refused++; return -1; }      /* meshes: the transform of a PMESH_ID (its matrix +0x78) is NOT PORTED */
    if (en->type == E_SPLINE) {                           /* xform_store_ent cases 8 / 0x80 */
        if (en->crv < 0 || en->crv >= F.ncrvs) { xf_refused++; return -1; }
        k = xf_curve(F.crvs[en->crv].rec, F.crvs[en->crv].n, M, lv, co);
        if (k >= 0) F.ents[k].ln_style = ln_style;
        return k;
    }
    if (en->type == E_ARC && xf_xtype(M) == 2 && xf_nonuni(M)) {   /* case 4 with XfScaleFromMatrix true: convert_arc_to_nb + put_nurbs_in_db, then
                                                                     FUN_1808d7f00 on the stored NURBS (l.1253-1316) */
        uint32_t n = 0; uint8_t *r = mcsb_arc_nurbs_rec(&F, en, XF_TOL, &n); if (!r) { xf_refused++; return -1; }
        k = xf_curve(r, n, M, lv, co); free(r);
        if (k >= 0) { F.ents[k].ln_style = ln_style; xf_nbarcs++; }
        return k;
    }
    if (en->type == E_POINT) { V3 p = xm_pt(M, en->a); k = mcam_new_ent(&F, E_POINT, p, p, z0, 0, 0, 0, lv, co); }
    else if (en->type == E_LINE) { V3 a = xm_pt(M, en->a), b = xm_pt(M, en->b); if (v_len((V3){ b.x - a.x, b.y - a.y, b.z - a.z }) < 1e-9) return -1;
        k = mcam_new_ent(&F, E_LINE, a, b, z0, 0, 0, 0, lv, co); }
    else if (en->type == E_ARC && (xf_xtype(M) == 2 || xf_xtype(M) == 3)) {
        /* FUN_1808d5e90 -> FUN_1808d2330 (decomp/xform/gaps/core_arc.c) for a scale / a translation: the centre through the matrix
           (view_to_world, xform_pt, world_to_view in the same view), a scale's radius x |DAT_18143d668|, the view and the angles kept */
        V3 c = xm_pt(M, en->c); double r = xf_xtype(M) == 2 ? en->r * fabs(xf_fac) : en->r;
        if (!en->vw) k = mcam_new_ent(&F, E_ARC, c, c, c, r, en->a0, en->sw, lv, co);
        else { double vm[9]; memcpy(vm, en->vm, sizeof vm); k = mcam_new_arc_view_w(&F, en->vw, vm, c, r, en->a0, en->sw, lv, co); }
    }
    else if (en->type == E_ARC) {                         /* a rotation / mirror (types 0 / 1): FUN_1808d2070 -> FUN_1808d20f0 */
        /* the arc's frame through the linear part must stay a similarity (else an ellipse: only a non-uniform scale is decoded - the
           NURBS branch above; anything else non-similar is refused) */
        V3 X, Y; arc_frame(en, &X, &Y);
        V3 AX = xm_vec(M, X), AY = xm_vec(M, Y); double sx = v_len(AX), sy = v_len(AY);
        if (sx < 1e-12 || fabs(sx - sy) > 1e-9 * (sx > 1 ? sx : 1) || fabs(v_dot(AX, AY)) > 1e-9 * sx * sy) { xf_refused++; return -1; }
        double a0, sw; int view; double vm[9];
        if (!xf_arc_views(en, M, &view, vm, &a0, &sw)) { xf_refused++; return -1; }
        V3 c = xm_pt(M, en->c); double r = en->r;
        /* view 1 (Top: XfStdView takes it before Bottom): an XY arc with the sweep as mccore leaves it - a mirrored arc runs clockwise (a
           negative sweep), as Mastercam's own files have them (the loader keeps their sign: mcam_load.c arc_finish) */
        if (view == 1) k = mcam_new_ent(&F, E_ARC, c, c, c, r, a0, sw, lv, co);
        else k = mcam_new_arc_view_w(&F, view, vm, c, r, a0, sw, lv, co);
    }
    if (k >= 0) { F.ents[k].pt_style = pt_style; F.ents[k].ln_style = ln_style; }
    return k;
}

/* ---------------------------------------------------------------- the commands' parameters */
/* MoveCopyJoin 0 Move / 1 Copy / 2 Join; Direction (MCButton 0x463..0x465) 0 forward, 1 reverse, 2 both sides
   (the copy builders: (dir & ~2) == 0 forward, dir - 1 < 2 reverse) */
typedef struct {
    int mode, copies, bytotal, dir;                     /* +0x20 MoveCopyJoin, +0x24 Copies, +0x28 ByTotal, +0x48 Direction */
    double dx, dy, dz, pang, plen;                      /* +0x30 Delta; dialog polar angle dlg+0x178a8 / length dlg+0x178b0 */
    V3 from, to; int fromto, ft_set;                    /* dlg FROM / TO, +0x178f0 From/To mode, +0x17875 (first pick) */
    int useattr, incr, level, color;                    /* +0x4d UseAttrs, +0x4e IncrLevels, +0x50 Level, +0x54 Color */
} XfTrans;
typedef struct {                                        /* CXfRotateParams (FUN_180031320) */
    int mode, copies, bytotal, translate, dir; V3 ctr; double ang;
    int useattr, incr, level, color;
} XfRot;
typedef struct {                                        /* CXfMirrorParams (FUN_180013c10) */
    int mode, axis; V3 ref; double ang; V3 p1, p2; int have_line, have_pts;
    int useattr, level, color;
} XfMir;
typedef struct {                                        /* CXfArrayParams (FUN_18003a210) */
    int n1, n2; double l1, l2, a1, a2; int d1, d2; int useattr, incr, level, color;
} XfArr;
typedef struct {                                        /* CXfTrans3dParams (FUN_18003a530) */
    int mode, copies, vfrom, vto; V3 pfrom, pto; int useattr, incr, level, color;
} XfT3;
typedef struct {                                        /* CXfScaleParams (FUN_1800350d0) */
    int mode, copies, xyz, pct; double s, sx, sy, sz; V3 org; int useattr, incr, level, color;
} XfScl;
static XfTrans xt = { .mode = 1, .copies = 1 };                    /* ctor FUN_18003ac00(&p, 1): MoveCopyJoin 1 */
static XfRot xr = { .mode = 1, .copies = 1 };
static XfMir xmi = { .mode = 1, .axis = -1 };
static XfArr xa;
static XfT3 x3 = { .mode = 1, .copies = 1, .vfrom = 1, .vto = 1 };
static XfScl xs = { .mode = 1, .copies = 1 };
static XfTrans xst = { .mode = 0, .copies = 1 };                         /* Stretch: CXfTransParams ctor FUN_18003ac00(&p, 0) - Move */
/* Distribute (TransformDistribute, struct DAT_180254930; formats/wireframe.md "Distribute - port spec"): mode 0 Move / 1 Copy (+0x49
   Move flag: MoveCopyJoin = !Move), margins +0x18 / +0x20 (lock 10317), equal +0x28, N +0x2c, s +0x30, just +0x38, adjust offset
   +0x3c, offset +0x40, rot = combo 10329 (0 Original, 1 Align Box +0x48, 2 Align geometry: edge +0x18a20 = 1), center +0x4a */
typedef struct { int mode; double ms, me; int mlock, equal, n; double s; int just, adjust; double off; int rot, center, useattr, incr, level, color;
                 V3 p1, p2; int novec, edge; V3 poff; int poff_set; } XfDist;
static XfDist xd;
static int xs_auto = 1;                                /* Scale "Auto center" 3025: global DAT_180243a50 (.data 1), kept for the session */
/* the Scale Calculator (CXfScaleCompDlg, mccadutilres template 6452 "Scale Calculator"; decomp/xform/distribute_b.c FUN_1800387c0 ctor /
   FUN_180039100 / FUN_180038b90 / FUN_180038ff0 / FUN_180038f20 / FUN_180038f90, stretch_distribute.c (its OnInitDialog), gaps/scalecalc.c
   FUN_1800392c0 / FUN_180039610 / FUN_180039920 / FUN_180039310 and the four callers FUN_180035fb0 / FUN_1800360f0 / FUN_180036230 /
   FUN_180036370; message map @18017bf20, gaps/maps1.txt). A modal dialog: while it is up the Scale panel shows only it. */
typedef struct { int on, target; int cb, cr; double base, res, fac; } XsCalc;
static XsCalc xsc;                                     /* target 1 Scale (723) / 2 X / 3 Y / 4 Z; cb / cr the combos 1001 / 1002 (+0x60f8 /
                                                          +0x60fc), base / res / fac the edits 1005 / 1006 / 1007 (+0x4310 / +0x5108 / +0x5f00) */
static int xsc_which;                                  /* the measurement running: 0 base, 1 result */
/* Stretch: per original, which line ends move (sel bits 0x200 start / 0x40 end; both = the whole entity) */
static unsigned char *xst_end;

/* ---------------------------------------------------------------- state */
enum { XP_SEL = 1, XP_DLG, XP_PICK, XP_DYN };
/* toggle codes (ours: the dialog buttons that start a pick, posted through geo_panel_button) */
enum { XB_FROM = 10, XB_LINE = 11, XB_TO = 12, XB_REGEN = 13, XB_RESEL = 15, XB_RCTR = 20, XB_MX = 30, XB_MY = 31, XB_MP = 32,
       XB_MLINE = 33, XB_M2P = 34, XB_SORG = 40, XB_DORG = 41, XB_SREF = 60, XB_DVEC = 70, XB_DOFF = 71, XB_RMITEM = 80, XB_RSITEMS = 81,
       XB_RMEND = 82, XB_CALC = 90, XB_CSELB = 94, XB_CSELR = 95, XB_COK = 96, XB_CCANCEL = 97 };
   /* XP_DYN: Dynamic's gnomon rounds (placement / idle / drags) */
static int xf_c, xf_phase, xf_pickfor, xf_pickstep;   /* running command (geo_cmd), phase, what a pick is for */
static int *xf_l, xf_n;                                /* the originals (the selection select_ents returned) */
static int xf_prev, xf_prev_nu; static void *xf_prev_rec[2];   /* the preview (an undo step, UndoXformTransStart) */
static int xf_last_from;                               /* first entity of the preview */
int xf_cmd(int c) { return c >= G_XTRANS && c <= G_XSCALE; }
int xf_on(void) { return xf_c != 0; }
int xf_selecting(void) { return xf_c && xf_phase == XP_SEL; }

static const char *xf_sel_prompt(void) {
    switch (xf_c) {
    case G_XROT: return "Rotate: select entities to rotate";          /* mccadutilres 3946 (do_xform_rotate) */
    case G_XMIRROR: return "Mirror: select entities to mirror";       /* 3945 (do_xform_mirror) */
    case G_XSCALE: return "Scale: select entities to scale";          /* 3947 (do_xform_scale) */
    case G_XSTRETCH: return "Stretch:  Window intersect entities to stretch";   /* 3949 (do_xform_stretch) */
    case G_XDIST: return "Select the geometry to be fit along the vector";    /* mccadutilres 4109 (TransformDistribute l.108) */
    case G_XDYN: return "Select entities to move/copy";                       /* mccadutilres 440 (FUN_1800eb6c0, MX l.13203) */
    default: return "Translate: select entities to translate";       /* 3948 (do_xform_trans, trans3d, array) */
    }
}
static const char *xdy_prompt(void);
const char *xf_prompt(void) {
    if (!xf_c) return "";
    if (xf_c == G_XDYN && xf_phase == XP_DYN) return xdy_prompt();
    if (xf_phase == XP_SEL) return xf_sel_prompt();
    if (xf_phase == XP_PICK) switch (xf_pickfor) {
        case 10: return "Select the point to translate from";        /* mccoreres 3929 (FUN_180040450) */
        case 12: return "";                                           /* mccoreres 3930: not in the string tables (NOT DECODED) */
        case 11: return "Define the fit vector";                      /* mccoreres 1538 (FUN_18003fab0) */
        case 20: return "Select the point to rotate about";           /* mccoreres 3932 (FUN_180033b80) */
        case 30: case 31: case 32: return "Select reference point";   /* mccoreres 1489 (mirror point buttons) */
        case 33: return "Select line about which to mirror";          /* mccoreres 3931 */
        case 34: return xf_pickstep ? "Select second point of mirror line" : "Select first point of mirror line";   /* 3937 / 3936 */
        case 40: return "Select the point to translate from";         /* Translate 3D 716: mccoreres 3929 */
        case 41: return "";                                           /* 717: 3930 NOT DECODED */
        case 50: return "Select the point to translate from";         /* Move to Origin: point_proc(mccoreres 3929) */
        case 60: return "Select the point to scale about";            /* Scale 716: mccoreres 3921 (FUN_1800370f0) */
        case 70: return xf_pickstep ? "" : "Define the fit vector";   /* Distribute FUN_180012b80: mccoreres 1538; the 2nd point 3930 NOT DECODED */
        case 71: return "Select point";                               /* 716: id 5321 [inferred mccoreres 5321] */
        case 80: return "Select copy to remove or Press <ENTER>";     /* Remove Item: GetCoreString(0x5fe) = mccoreres 1534 (FUN_1800335f0) */
        /* the Scale Calculator's measurements: "%s\n%s" of mccadutilres 458 "Define the base length" / 459 "Define the result length"
           (FUN_180039100 / FUN_180038b90) and the core string of the method */
        case 90: return xsc_which ? "Define the result length\nSelect a line" : "Define the base length\nSelect a line";   /* 0x764 = 1892 */
        case 91: return xf_pickstep ? (xsc_which ? "Define the result length\nSelect second point" : "Define the base length\nSelect second point")
                                    : (xsc_which ? "Define the result length\nSelect first point" : "Define the base length\nSelect first point");   /* 0x756 / 0x757 = 1878 / 1879 */
        case 92: case 93: return xsc_which ? "Define the result length\n\nSelect an arc or radial face" : "Define the base length\n\nSelect an arc or radial face";   /* 0x753 = 1875 (it starts with \n) */
        }
    return "";
}
static void xf_msg(void) { snprintf(cam_msg, sizeof cam_msg, "%s", xf_prompt()[0] ? xf_prompt() : geo_name(geo_cmd)); }

/* ---------------------------------------------------------------- the XfInfo list of the running command */
/* XfInfo (0x80 bytes): the matrix, +0x70 / +0x72 the copy's key (two shorts; the builders: Rotate FUN_180031900 / FUN_180032010 forward i,
   reverse -i, +0x72 0; Rectangular Array FUN_18003c160 / FUN_18003c4e0 (column, row): the base row's columns (c + 1, 1) / (-1 - c, 1), row k
   (1, k + 1) / (1, -1 - k), that row's columns (c + 1, row) / (-1 - c, row)); the other builders leave the key 0 (XfInfoInit / calloc) */
typedef struct { XM M; short ka, kb; } XfInfo;
static XfInfo *xi; static int nxi, capxi;
static void xi_addk(XM M, int ka, int kb) { if (nxi == capxi) { capxi = capxi ? capxi * 2 : 64; xi = realloc(xi, sizeof *xi * capxi); } xi[nxi].M = M; xi[nxi].ka = (short)ka; xi[nxi].kb = (short)kb; nxi++; }
static void xi_add(XM M) { xi_addk(M, 0, 0); }
/* the removed copies (mccore XfInfoRemovedAdd @1808eb680 appends the entry's +0x70 / +0x72; XfInfoRemovedFree @1808eb6a0; XfRemovedYes
   @1808eb820 = count > 0); FUN_1808eb7b0 (decomp/xform/gaps/core3.c): an entry whose two shorts equal a removed pair is skipped by the
   preview / final action's entry loop (FUN_1808eb340 / FUN_1808eb720, gaps/core2.c) */
static int *xf_rm; static int xf_nrm;
static int xf_removed(const XfInfo *x) { for (int i = 0; i < xf_nrm; i++) if ((short)(xf_rm[i] & 0xffff) == x->ka && (short)(xf_rm[i] >> 16) == x->kb) return 1; return 0; }
static void xf_rm_add(const XfInfo *x) { xf_rm = realloc(xf_rm, sizeof(int) * (size_t)(xf_nrm + 1)); xf_rm[xf_nrm++] = (int)(((unsigned)(unsigned short)x->kb << 16) | (unsigned short)x->ka); }
static void xf_rm_free(void) { free(xf_rm); xf_rm = NULL; xf_nrm = 0; }
static int xf_rm_identity;                             /* the entries are XfInfoFillWithI33's (its keys are NOT DECODED) */

/* Translate: FUN_18003d210 (v = Delta, / copies with ByTotal; XfInfoALLOC(fwd, copies, rev); T = +-i v) */
static void build_trans(void) {
    XfTrans *p = &xt; double vx = p->dx, vy = p->dy, vz = p->dz;
    if (!cfg_constr_3d && (fabs(vx) > XF_TOL || fabs(vy) > XF_TOL)) vz = 0;   /* 2D construction: Z only alone (FUN_180044860) */
    int n = p->copies < 1 ? 1 : p->copies;
    if (p->bytotal && n > 1) { vx /= n; vy /= n; vz /= n; }
    XM s = xm_trans(vx, vy, vz), r = xm_trans(-vx, -vy, -vz);
    if ((p->dir & ~2) == 0) for (int i = 1; i <= n; i++) xi_add(xm_pow(&s, i));
    if ((unsigned)(p->dir - 1) < 2) for (int i = 1; i <= n; i++) xi_add(xm_pow(&r, i));
}
/* Rotate: FUN_180031900 / FUN_180032010 (step = Angle / copies with Total sweep; |angle| < 5.7296e-5 deg -> identities) */
static void build_rot(void) {
    XfRot *p = &xr; int n = p->copies < 1 ? 1 : p->copies;
    double step = p->ang * D2R; if (p->bytotal && n > 1) step /= n;
    if (fabs(p->ang * (p->bytotal ? n : 1)) < 5.7296e-5) { xf_rm_identity = 1; for (int i = 0; i < n; i++) xi_add(xm_id()); return; }   /* XfInfoFillWithI33 */
    for (int side = 0; side < 2; side++) {
        if (side == 0 && (p->dir & ~2) != 0) continue;
        if (side == 1 && (unsigned)(p->dir - 1) >= 2) continue;
        double a = side ? -step : step, c = cos(a), s = sin(a);
        XM R = xm_id();
        if (!p->translate) { R.m[0] = c; R.m[1] = -s; R.m[3] = s; R.m[4] = c;   /* about the centre, CCW positive */
            R.t[0] = p->ctr.x * (1 - c) + p->ctr.y * s; R.t[1] = p->ctr.y * (1 - c) - p->ctr.x * s; }
        for (int i = 1; i <= n; i++) {
            int ka = side ? -i : i;                          /* +0x70: i forward, -i reverse (FUN_180031900 / FUN_180032010) */
            if (!p->translate) { xi_addk(xm_pow(&R, i), ka, 0); continue; }
            /* Translate: T = rotate_pt(ref, centre, i * step) - ref; ref = XfBBox_GetParams output 3 ([inferred] the
               selection's box centre: here the centre of the originals' extents) */
            V3 lo = { 1e300, 1e300, 1e300 }, hi = { -1e300, -1e300, -1e300 };
            for (int k = 0; k < xf_n; k++) { const Ent *en = &F.ents[xf_l[k]]; V3 q[2] = { en->a, en->b };
                for (int j = 0; j < 2; j++) { lo.x = fmin(lo.x, q[j].x); lo.y = fmin(lo.y, q[j].y); hi.x = fmax(hi.x, q[j].x); hi.y = fmax(hi.y, q[j].y); } }
            double rx = (lo.x + hi.x) / 2 - p->ctr.x, ry = (lo.y + hi.y) / 2 - p->ctr.y, ai = a * i;
            xi_addk(xm_trans(rx * cos(ai) - ry * sin(ai) - rx, rx * sin(ai) + ry * cos(ai) - ry, 0), ka, 0);
        }
    }
}
/* Mirror: FUN_180016f80 axis ends, FUN_180014230 matrix (k = 2 / L; m00 = k dx^2 - 1 ...), one copy */
static void build_mirror(void) {
    XfMir *p = &xmi; V3 a, b;
    switch (p->axis) {
    case 0: a = (V3){ p->ref.x - 2, p->ref.y, 0 }; b = (V3){ p->ref.x + 2, p->ref.y, 0 }; break;
    case 1: a = (V3){ p->ref.x, p->ref.y - 2, 0 }; b = (V3){ p->ref.x, p->ref.y + 2, 0 }; break;
    case 2: a = p->ref; b = (V3){ p->ref.x + 2 * cos(p->ang * D2R), p->ref.y + 2 * sin(p->ang * D2R), 0 }; break;
    case 3: case 4: a = p->p1; b = p->p2; break;
    default: return;                                     /* no axis yet (Mode -1 at the start) */
    }
    double dx = b.x - a.x, dy = b.y - a.y, L = dx * dx + dy * dy;
    if (L < XF_TOL * XF_TOL) { snprintf(cam_msg, sizeof cam_msg, "Zero length line!"); return; }   /* mccadutilres 3912 */
    double k = 2 / L; XM M = xm_id();
    M.m[0] = k * dx * dx - 1; M.m[4] = k * dy * dy - 1; M.m[1] = M.m[3] = k * dx * dy;
    M.t[0] = a.x * k * dy * dy - a.y * k * dx * dy; M.t[1] = a.y * k * dx * dx - a.x * k * dx * dy;
    xi_add(M);
}
/* Rectangular Array: FUN_18003c8b0 / FUN_18003c160 / FUN_18003c4e0 (translations; the originals are entry (1, 1)) */
static void arr_row(V3 base, const XfArr *p, V3 v1, int mode, int row) {
    /* FUN_18003c160: every forward column first, then every reverse one (two loops) */
    if ((mode & ~2) == 0) for (int i = 1; i < p->n1; i++) xi_addk(xm_trans(base.x + i * v1.x, base.y + i * v1.y, 0), i + 1, row);   /* (c + 1, row) */
    if ((unsigned)(mode - 1) < 2) for (int i = 1; i < p->n1; i++) xi_addk(xm_trans(base.x - i * v1.x, base.y - i * v1.y, 0), -1 - i, row);   /* (-1 - c, row) */
}
static void build_array(void) {
    XfArr *p = &xa; V3 v1 = { 0, 0, 0 }, v2 = { 0, 0, 0 }; double a1 = 0;
    if (p->n1 <= 1 && p->n2 <= 1) return;                /* XfInfoFillWithI33(1, 1, 0, 0): nothing new */
    if (fabs(p->l1) > XF_TOL) { a1 = p->a1 * D2R; v1 = (V3){ p->l1 * cos(a1), p->l1 * sin(a1), 0 }; if (v_len(v1) < XF_TOL) return; }
    if (p->n2 > 1 && fabs(p->l2) > XF_TOL) { double a = a1 + p->a2 * D2R; v2 = (V3){ p->l2 * cos(a), p->l2 * sin(a), 0 }; }
    V3 z = { 0, 0, 0 };
    if (p->n1 > 1) arr_row(z, p, v1, p->d1, 1);
    for (int k = 1; k < p->n2; k++) if ((p->d2 & ~2) == 0) { V3 b = v_scale(v2, k); xi_addk(xm_trans(b.x, b.y, 0), 1, k + 1); if (p->n1 > 1) arr_row(b, p, v1, p->d1, k + 1); }   /* FUN_18003c4e0 */
    for (int k = 1; k < p->n2; k++) if ((unsigned)(p->d2 - 1) < 2) { V3 b = v_scale(v2, -k); xi_addk(xm_trans(b.x, b.y, 0), 1, -1 - k); if (p->n1 > 1) arr_row(b, p, v1, p->d1, -1 - k); }
}
/* Translate 3D: FUN_18003cc10 - same view: T = To - From; else p' = To + Mdst^T Msrc (p - From); copies = M^i */
static void build_t3(void) {
    XfT3 *p = &x3; int n = p->copies < 1 ? 1 : p->copies;
    const double *ms = cam_plane_matrix(p->vfrom), *md = cam_plane_matrix(p->vto); XM M = xm_id();
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) M.m[3 * i + j] = md[i] * ms[j] + md[3 + i] * ms[3 + j] + md[6 + i] * ms[6 + j];
    if (p->vfrom == p->vto) M = xm_id();
    V3 f = xm_vec(&M, p->pfrom); M.t[0] = p->pto.x - f.x; M.t[1] = p->pto.y - f.y; M.t[2] = p->pto.z - f.z;
    for (int i = 1; i <= n; i++) xi_add(xm_pow(&M, i));
}
/* the originals' box centre (CalcAutoCursorTemporaryCenterPoint -> FUN_18001a7e0: the union of the entities' world
   boxes, EntityMinMax; arcs: their extreme points) */
static V3 sel_box_centre(void) {
    V3 lo = { 1e300, 1e300, 1e300 }, hi = { -1e300, -1e300, -1e300 }; int any = 0;
#define ADDP(P) do { V3 q_ = (P); lo.x = fmin(lo.x, q_.x); lo.y = fmin(lo.y, q_.y); lo.z = fmin(lo.z, q_.z); hi.x = fmax(hi.x, q_.x); hi.y = fmax(hi.y, q_.y); hi.z = fmax(hi.z, q_.z); any = 1; } while (0)
    for (int k = 0; k < xf_n; k++) {
        const Ent *en = &F.ents[xf_l[k]]; if (!en->type) continue;
        ADDP(en->a); ADDP(en->b);
        if (en->type == E_ARC) for (int q = 0; q < 4; q++) {         /* the quadrant points inside the sweep (a clockwise arc - sweep < 0 -
                                                                        as the counter-clockwise one from its end: ge_arc's normalisation) */
            double a0 = en->sw < 0 ? en->a0 + en->sw : en->a0, sw = fabs(en->sw);
            double t = q * M_PI / 2, d = fmod(t - a0, 2 * M_PI); if (d < 0) d += 2 * M_PI;
            if (d <= sw + 1e-12) ADDP(mcam_arc_pt(en, t)); }
        if (en->type == E_SPLINE)                        /* EntityMinMax of a spline: NOT DECODED - ours: its drawn polyline */
            for (int pi = en->first_poly; pi < en->first_poly + en->npoly; pi++) for (int j = 0; j < F.polys[pi].count; j++) ADDP(F.pts[F.polys[pi].first + j]);
    }
#undef ADDP
    if (!any) return (V3){ 0, 0, 0 };                    /* nothing qualifies: 0,0,0 (FUN_180035350) */
    return (V3){ (lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2 };
}
/* Scale: FUN_180035820 (Uniform diag(s), XYZ diag(sx, sy, sz) in the Cplane (Top here), translation c (1 - s); copies
   M^i - the factor is cumulative; all factors within tol of 1 -> identities, XfInfoFillWithI33) */
static void build_scale(void) {
    XfScl *p = &xs; int n = p->copies < 1 ? 1 : p->copies; double k = p->pct ? 0.01 : 1;
    double sx = (p->xyz ? p->sx : p->s) * k, sy = (p->xyz ? p->sy : p->s) * k, sz = (p->xyz ? p->sz : p->s) * k;
    if (fabs(sx - 1) <= XF_TOL && fabs(sy - 1) <= XF_TOL && fabs(sz - 1) <= XF_TOL) { for (int i = 0; i < n; i++) xi_add(xm_id()); return; }
    V3 c = xs_auto ? sel_box_centre() : p->org;
    XM M = xm_id(); M.m[0] = sx; M.m[4] = sy; M.m[8] = sz;
    M.t[0] = c.x * (1 - sx); M.t[1] = c.y * (1 - sy); M.t[2] = c.z * (1 - sz);
    for (int i = 1; i <= n; i++) xi_add(xm_pow(&M, i));
}
/* the selection's box in a frame (rows X / Y / Z, world -> frame = the dot products): GetEntityBoundingBox under the plane (FUN_180012090,
   MX l.10027) - points and line ends exact, arcs by their extremes along the frame's axes (+ ends), other entities by their drawn
   polylines (ours) */
static int dist_box(const V3 X, const V3 Y, const V3 Z, V3 *lo, V3 *hi) {
    int any = 0; V3 l = { 0, 0, 0 }, h = { 0, 0, 0 };
#define DB(P) do { V3 p_ = (P), q_ = { v_dot(p_, X), v_dot(p_, Y), v_dot(p_, Z) }; if (!any) { l = h = q_; any = 1; } else { \
        l.x = fmin(l.x, q_.x); l.y = fmin(l.y, q_.y); l.z = fmin(l.z, q_.z); h.x = fmax(h.x, q_.x); h.y = fmax(h.y, q_.y); h.z = fmax(h.z, q_.z); } } while (0)
    for (int q = 0; q < xf_n; q++) {
        int e = xf_l[q]; if (e < 0 || e >= F.nents || !F.ents[e].type) continue; const Ent *en = &F.ents[e];
        if (en->type == E_POINT) DB(en->a);
        else if (en->type == E_LINE) { DB(en->a); DB(en->b); }
        else if (en->type == E_ARC) {
            V3 U, W; arc_frame(en, &U, &W); DB(mcam_arc_pt(en, en->a0)); DB(mcam_arc_pt(en, en->a0 + en->sw));
            const V3 ax[3] = { X, Y, Z };
            double a0 = en->sw < 0 ? en->a0 + en->sw : en->a0;   /* a clockwise arc (sweep < 0) from its end (ge_arc's normalisation) */
            for (int k = 0; k < 3; k++) for (int sg = 0; sg < 2; sg++) {
                double t = atan2(v_dot(W, ax[k]), v_dot(U, ax[k])) + sg * M_PI, d = fmod(t - a0, 2 * M_PI); if (d < 0) d += 2 * M_PI;
                if (d <= fabs(en->sw) + 1e-12 || fabs(en->sw) >= 2 * M_PI - 1e-12) DB(mcam_arc_pt(en, a0 + d)); }
        } else for (int pi = en->first_poly; pi < en->first_poly + en->npoly; pi++) for (int k = 0; k < F.polys[pi].count; k++) DB(F.pts[F.polys[pi].first + k]);
    }
#undef DB
    *lo = l; *hi = h; return any;
}
/* FUN_18000e090 (decomp/xform/distribute_math.c; the spec's pseudo-code): the copies along the fit vector. The Cplane is Top (av = world);
   fv = the frame get_unique_view_from_2L makes (mastercam.dll 0x18033a4e0: Plane::GetMatrixFrom2lines(P1->P2, P1->left normal) ->
   X along the vector, Y its left normal, Z = +Z; GetViewNo only names it - its matrix is all the math uses) */
static void build_distribute(void) {
    XfDist *p = &xd;
    if (p->novec) return;
    double tol = XF_TOL;
    V3 v = { p->p2.x - p->p1.x, p->p2.y - p->p1.y, 0 }; double vl = hypot(v.x, v.y);
    double ang = v.x * v.x + v.y * v.y >= 1e-12 ? atan2(v.y, v.x) : 0; if (ang < 0) ang += 2 * M_PI;
    ang += p->edge == 0 ? M_PI / 2 : p->edge == 2 ? 3 * M_PI / 2 : p->edge == 3 ? M_PI : 0;
    while (ang > 2 * M_PI + tol) ang -= 2 * M_PI;
    if (vl < tol) return;                                   /* (CheckFlat2lines fails: no view) */
    V3 X = { v.x / vl, v.y / vl, 0 }, Y = { -X.y, X.x, 0 }, Z = { 0, 0, 1 }, WX = { 1, 0, 0 }, WY = { 0, 1, 0 };
#define W2V(P) ((V3){ v_dot((P), X), v_dot((P), Y), v_dot((P), Z) })
#define V2W(Q) ((V3){ (Q).x * X.x + (Q).y * Y.x + (Q).z * Z.x, (Q).x * X.y + (Q).y * Y.y + (Q).z * Z.y, (Q).x * X.z + (Q).y * Y.z + (Q).z * Z.z })
    V3 lo, hi;
    if (!p->rot) {                                          /* not Align Box (+0x16d38 == 0): the box in av, then into fv */
        if (!dist_box(WX, WY, Z, &lo, &hi)) return;
        V3 c1 = { lo.x, hi.y, lo.z }, c2 = { hi.x, lo.y, lo.z };
        lo = W2V(lo); hi = W2V(hi);
        if (p->edge < 0) {
            V3 a = lo, b = hi; lo = (V3){ fmin(a.x, b.x), fmin(a.y, b.y), fmin(a.z, b.z) }; hi = (V3){ fmax(a.x, b.x), fmax(a.y, b.y), fmax(a.z, b.z) };
            V3 d1 = W2V(c1), d2 = W2V(c2), d[2] = { d1, d2 };
            for (int k = 0; k < 2; k++) { lo.x = fmin(lo.x, d[k].x); lo.y = fmin(lo.y, d[k].y); lo.z = fmin(lo.z, d[k].z); hi.x = fmax(hi.x, d[k].x); hi.y = fmax(hi.y, d[k].y); hi.z = fmax(hi.z, d[k].z); }
        }
    } else if (!dist_box(X, Y, Z, &lo, &hi)) return;         /* Align Box: the box in fv */
    V3 A = W2V(p->p1), B = W2V(p->p2), c = { (lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2 };
    double dx = 0, dy = 0;
    { double ex = B.x - A.x, ey = B.y - A.y, L2 = ey * ey + ex * ex;   /* proj_pt_ln (systypes.dll 0x180002780, decomp/xform/gaps/projptln.c):
                                                                         t = ((c - A) . e) / |e|^2, F = (1 - t) A + t B; |e|^2 == 0: not ok */
      if (L2 != 0.0) { double t = ((c.y - A.y) * ey + (c.x - A.x) * ex) / L2; dx = c.x - ((1.0 - t) * A.x + t * B.x); dy = c.y - ((1.0 - t) * A.y + t * B.y); } }
    if (!p->adjust) p->off = dy;
    else if (p->poff_set) {                                 /* dist_pt_l from the picked point, negative below the vector */
        V3 q = W2V(p->poff); double ex = B.x - A.x, ey = B.y - A.y, L = sqrt(ey * ey + ex * ex);   /* dist_pt_l (mccore 0x1804b32a0,
                                                          gaps/distpt.c): |((q - A) x e)| / |e|, |e| <= tol: |q - A| */
        p->off = L <= XF_TOL ? hypot(q.x - A.x, q.y - A.y) : fabs(((q.x - A.x) * ey - (q.y - A.y) * ex) / L); if (q.y < A.y) p->off = -p->off;
    }
    dy = p->off;
    if (p->off < 0) { ang += M_PI; while (ang > 2 * M_PI + tol) ang -= 2 * M_PI; }
    V3 cw = V2W(c); double ca = cos(ang), sa = sin(ang);
    XM R = xm_id(); R.m[0] = ca; R.m[1] = -sa; R.m[3] = sa; R.m[4] = ca;   /* FUN_180012a60: CCW by ang about c (x_matrix rows transposed) */
    R.t[0] = cw.x * (1 - ca) + cw.y * sa; R.t[1] = cw.y * (1 - ca) - cw.x * sa;
    if (p->edge >= 0) {                                     /* the box after the rotation, in fv */
        V3 a = W2V(xm_pt(&R, V2W(lo))), b = W2V(xm_pt(&R, V2W(hi)));
        lo = (V3){ fmin(a.x, b.x), fmin(a.y, b.y), fmin(a.z, b.z) }; hi = (V3){ fmax(a.x, b.x), fmax(a.y, b.y), fmax(a.z, b.z) };
    }
    double w = hi.x - lo.x, Lx = (B.x - A.x) + p->ms + p->me + (p->center ? w : 0), Vy = B.y - A.y;
    int n;
    if (!p->equal) {                                        /* the count by fit (l.535-554) */
        if (p->n < 1) return;
        double len = sqrt(Lx * Lx + Vy * Vy), acc = 0; n = 0;
        do { acc += w; if (acc >= len + tol) break; acc += p->s; n++; } while (n < p->n);
    } else n = p->n;
    if (n < 1) return;                                      /* (both returns precede XfInfoFree: ours, nothing is made) */
    V3 step;
    if (!p->equal) { double len = sqrt(Lx * Lx + Vy * Vy); step = (V3){ (w + p->s) / len * Lx, (w + p->s) / len * Vy, 0 }; }
    else { step = (V3){ Lx - w, Vy, 0 }; if (n > 2) { step.x /= n - 1; step.y /= n - 1; } p->s = n < 2 ? 0 : hypot(step.x, step.y) - w; }
    if (p->just == 2) { step.x = -step.x; step.y = -step.y; }
    double tx, ty;
    if (p->just == 0) { tx = A.x - lo.x - p->ms - (p->center ? w / 2 : 0); ty = A.y - c.y; }
    else if (p->just == 1) { double a0 = A.x - p->ms; tx = ((B.x + p->me) - a0) / 2 + a0 - c.x - (p->equal ? (n - 1) * step.x / 2 : (w + p->s) * (n - 1) / 2); ty = A.y - c.y; }
    else { tx = B.x - hi.x + p->me + (p->center ? w / 2 : 0); ty = B.y - c.y; }
    V3 t0 = V2W(((V3){ dx + tx, dy + ty, 0 })), sw = V2W(step);
    XM T0 = xm_trans(t0.x, t0.y, t0.z), M0 = p->edge >= 0 ? xm_mul(&T0, &R) : T0, S = xm_trans(sw.x, sw.y, sw.z);
    for (int i = 0; i < n; i++) { XM Si = xm_pow(&S, i); xi_add(xm_mul(&Si, &M0)); }   /* entry i = S^i after M0 */
#undef W2V
#undef V2W
}
static void build_dyn(void);
static int xdp_copy(void);
static void xf_build_list(void) {
    nxi = 0; xf_rm_identity = 0;
    switch (xf_c) {
    case G_XTRANS: build_trans(); break;
    case G_XROT: build_rot(); break;
    case G_XMIRROR: build_mirror(); break;
    case G_XARRAY: build_array(); break;
    case G_XTRANS3D: build_t3(); break;
    case G_XSCALE: build_scale(); break;
    case G_XDIST: build_distribute(); break;
    case G_XDYN: build_dyn(); break;
    case G_XSTRETCH: { XfTrans keep = xt; xt = xst; xt.dir = 0; build_trans(); xt = keep; break; }   /* FUN_18003d210, no Direction button */
    default: break;
    }
}
/* the command's MoveCopyJoin / attributes */
static void xf_attrs(int *mode, int *ua, int *incr, int *lv, int *co) {
    *mode = 1; *ua = 0; *incr = 0; *lv = -1; *co = -1;
    switch (xf_c) {
    case G_XTRANS: *mode = xt.mode; *ua = xt.useattr; *incr = xt.incr; *lv = xt.level; *co = xt.color; break;
    case G_XROT: *mode = xr.mode; *ua = xr.useattr; *incr = xr.incr; *lv = xr.level; *co = xr.color; break;
    case G_XMIRROR: *mode = xmi.mode; *ua = xmi.useattr; *lv = xmi.level; *co = xmi.color; break;
    case G_XARRAY: *mode = 1; *ua = xa.useattr; *incr = xa.incr; *lv = xa.level; *co = xa.color; break;   /* XfDoFinalAction(3, 1, ...) */
    case G_XTRANS3D: *mode = x3.mode; *ua = x3.useattr; *incr = x3.incr; *lv = x3.level; *co = x3.color; break;
    case G_XSCALE: *mode = xs.mode; *ua = xs.useattr; *incr = xs.incr && xs.copies >= 2; *lv = xs.level; *co = xs.color; break;
    case G_XSTRETCH: *mode = xst.mode; *ua = xst.useattr; *incr = xst.incr; *lv = xst.level; *co = xst.color; break;
    case G_XDYN: *mode = xdp_copy(); break;                                /* xform_silent(copy): keep-attributes 1, no new attributes */
    case G_XDIST: *mode = xd.mode; *ua = xd.useattr; *incr = xd.incr && xd.n >= 2; *lv = xd.level; *co = xd.color; break;   /* FUN_180011a40 */
    }
}

/* ---------------------------------------------------------------- preview / final action */
static int xf_prev_valid(void) {
    return xf_prev && nundo > 0 && nundo == xf_prev_nu && (void *)undo_st[nundo - 1].ents == xf_prev_rec[0] && (void *)undo_st[nundo - 1].dents == xf_prev_rec[1];
}
static void xf_drop(void) {                            /* XfDeletePreview + UndoTransTerminate */
    if (xf_prev_valid()) { do_undo(); if (nredo) urec_free(&redo_st[--nredo]); }
    xf_prev = 0; overlay_dirty = 1; scene_reupload();
}
/* join lines of entity e to its image under M (FUN_1808e48b0: point -> 1, line -> 2, arc -> start + end unless closed) */
static void xf_join(int e, const XM *M, int lv, int co) {
    const Ent *en = &F.ents[e]; V3 z0 = { 0, 0, 0 }, q[2] = { en->a, en->b }; int nq = en->type == E_POINT ? 1 : 2;
    if (en->type == E_ARC && v_len((V3){ en->b.x - en->a.x, en->b.y - en->a.y, en->b.z - en->a.z }) < XF_TOL) nq = 1;
    for (int j = 0; j < nq; j++) {
        V3 a = q[j], b = xm_pt(M, a);
        if (v_len((V3){ b.x - a.x, b.y - a.y, b.z - a.z }) < XF_TOL) continue;
        /* del_duplicate over the results (FUN_1808d8820 end): a join line equal to one made already is left out */
        int dup = 0;
        for (int i = xf_last_from; i < F.nents && !dup; i++) { const Ent *o = &F.ents[i]; if (o->type != E_LINE) continue;
            V3 d1 = { o->a.x - a.x, o->a.y - a.y, o->a.z - a.z }, d2 = { o->b.x - b.x, o->b.y - b.y, o->b.z - b.z };
            V3 d3 = { o->a.x - b.x, o->a.y - b.y, o->a.z - b.z }, d4 = { o->b.x - a.x, o->b.y - a.y, o->b.z - a.z };
            dup = (v_len(d1) < XF_TOL && v_len(d2) < XF_TOL) || (v_len(d3) < XF_TOL && v_len(d4) < XF_TOL); }
        if (dup) continue;
        int k = mcam_new_ent(&F, E_LINE, a, b, z0, 0, 0, 0, lv, co);
        xf_mark(k, 8);
    }
}
/* XfCreatePreview -> FUN_1808d8820: every entry, every original */
static void xf_make(void) {
    xf_drop();
    if (!xf_n) return;
    nxi = 0; xf_build_list();
    xf_refused = xf_refused_nb = 0; xf_nbarcs = 0;
    /* DAT_18143d668 starts at param_11 and is multiplied by it at each pass (FUN_1808d8820 l.2612 / l.2908): the scale's factor (Uniform:
       Scale, XYZ: Scale X - the p_3d's x, MX l.6564-6587; Percentage(s) / 100) */
    double fac1 = xf_c == G_XSCALE ? (xs.xyz ? xs.sx : xs.s) * (xs.pct ? 0.01 : 1) : 1.0; int pass = 0;
    if (!nxi) { scene_reupload(); overlay_dirty = 1; return; }
    int mode, ua, incr, lv, co; xf_attrs(&mode, &ua, &incr, &lv, &co);
    int mlv = lv < 1 ? geo_level : lv, mco = co < 0 ? geo_color : co;   /* FUN_180045f00: < 1 main level, < 0 main colour */
    xf_marks_clear();
    int nu = nundo; xf_last_from = F.nents;
    mod_begin();
    for (int k = 0; k < nxi; k++) {
        if (xf_removed(&xi[k])) continue;                   /* FUN_1808eb340: a removed copy is skipped */
        pass++; xf_fac = 1.0; for (int i = 0; i < pass; i++) xf_fac *= fac1;
        for (int q = 0; q < xf_n; q++) {
            int e = xf_l[q]; if (e < 0 || e >= F.nents || !F.ents[e].type) continue;
            int elv = ua ? mlv + (incr ? k : 0) : F.ents[e].level, eco = ua ? mco : F.ents[e].color;   /* XfApplyColorLevelToResult_Index */
            int r;
            if (xf_c == G_XSTRETCH && xst_end && F.ents[e].type == E_LINE && (xst_end[q] == 1 || xst_end[q] == 2)) {
                /* xform_store_ent, stretch flag: only the end inside the window goes through xform_pt (0x200 start, 0x40
                   end); a result shorter than tol is not made and the line is deleted (local_2b06 -> delete_ent) */
                V3 a = F.ents[e].a, b = F.ents[e].b, z0 = { 0, 0, 0 };
                if (xst_end[q] == 2) a = xm_pt(&xi[k].M, a); else b = xm_pt(&xi[k].M, b);
                if (v_len((V3){ b.x - a.x, b.y - a.y, b.z - a.z }) < XF_TOL) { if (mode == 0) mod_del(e); continue; }
                r = mcam_new_ent(&F, E_LINE, a, b, z0, 0, 0, 0, elv, eco);
                if (r >= 0) F.ents[r].ln_style = F.ents[e].ln_style;
            } else r = xf_ent(e, &xi[k].M, elv, eco);
            if (mode == 0 && k == 0 && r >= 0) mod_pair(e, r);    /* Move: the first copy is the moved original (move && copy 1) */
            if (mode == 0 && k == 0 && r >= 0 && F.ents[e].type == E_SURF) srf_drop_base(e);   /* delete_ent's rule: the moved trimmed surface's old base */
            xf_mark(r, 8); xf_set_part(r, k + 1);
            /* FUN_1808e48b0: the join lines in the main colour (cVar7 = main_color; its other branch's flag is NOT DECODED);
               whether XfApplyColorLevelToResult reaches them is NOT DECODED - ours: they get the new attributes too */
            if (mode == 2) xf_join(e, &xi[k].M, ua ? elv : geo_level, ua ? eco : geo_color);
        }
    }
    for (int q = 0; q < xf_n; q++) { int e = xf_l[q]; if (e < 0 || e >= F.nents || !F.ents[e].type) continue;
        if (mode == 0) mod_del(e); else xf_mark(e, 2); }
    mod_end();
    if (nundo != nu) { xf_prev = 1; xf_prev_nu = nundo; xf_prev_rec[0] = undo_st[nundo - 1].ents; xf_prev_rec[1] = undo_st[nundo - 1].dents; }
    if (xf_refused) snprintf(cam_msg, sizeof cam_msg, "%d arc(s) not transformed: the result is not an arc in the XY plane or a system view (NOT PORTED)", xf_refused);
    doc_dirty = 1; scene_reupload(); overlay_dirty = 1;
}
static void xf_rebuild(void) { if (cfg_xf_preview) xf_make(); else xf_drop(); }   /* 0x66 */
static void xf_commit(void) {                          /* 0x65: XfDoFinalAction (+ UndoXformTransCommit) */
    if (!xf_prev_valid()) xf_make();
    xf_prev = 0;
}

/* ---------------------------------------------------------------- start / selection / end */
static void xf_take_selection(void) {
    free(xf_l); xf_l = NULL; xf_n = 0;
    int *l, n = sel_list(&l, 0), k = 0;
    for (int i = 0; i < n; i++) if (F.ents[l[i]].type && !F.ents[l[i]].gen) l[k++] = l[i];   /* points, lines, arcs (solids: not here) */
    if (xf_c != G_XSTRETCH) {                            /* surfaces (select_ents mask 0x1f1fff has 0x100; Stretch takes lines) */
        int *ls, ns = sel_list_surfs(&ls); l = realloc(l, sizeof(int) * (size_t)(k + ns + 1));
        for (int i = 0; i < ns; i++) l[k++] = ls[i];
        free(ls);
    }
    xf_l = l; xf_n = k;
}
static void xf_begin_selection(void) { xf_drop(); sel_clear(); xf_phase = XP_SEL; overlay_dirty = 1; xf_msg(); }
static void xf_snap(void);
static void xf_pick(int what);
/* the dialog (params -> dialog, FUN_180045f00 and its siblings: Level < 1 shows the main level, Color < 0 the main colour) */
/* the dialog's +0x5090 ("something was done": FUN_18000bd80 posting 0x66, CXf*Dlg OnCommand FUN_18000b900 on any BN_CLICKED / menu
   command (notify 0), PreTranslateMessage FUN_18000bfd0 on Tab / Enter; cleared by the ctor FUN_18000a8e0, decomp/xform/mccadutil_xform.c
   l.9081): Apply 12321 is enabled only while it is set (FUN_18000b030 + FUN_18000b380 = +0x50a4, set by the ctor's FUN_18000c080(1)); OK
   (0x65) with it clear is handled as Cancel (100) (do_xform_* MX l.479 / l.968 / ...); Esc ends the dialog as OK, i.e. Cancel when clear
   (bVar1: the forced 0x65). The first round posts no 0x66 (cVar11 = 0 before the loop, MX l.884 / l.925): no preview until the first
   change; the rounds after an Apply post it at once (cVar11 = +0x5091 of the last round). */
static int xf_mod, xf_round2;                         /* +0x5090; 0x66 to post when the dialog comes up (cVar11 / a reselect) */
static void xf_dialog(void) {
    int *lv[] = { &xt.level, &xr.level, &xmi.level, &xa.level, &x3.level, &xs.level, &xst.level, &xd.level }, *co[] = { &xt.color, &xr.color, &xmi.color, &xa.color, &x3.color, &xs.color, &xst.color, &xd.color };
    for (int i = 0; i < 8; i++) { if (*lv[i] < 1) *lv[i] = geo_level; if (*co[i] < 0) *co[i] = geo_color; }
    xf_phase = XP_DLG; sel_clear();
    xf_mod = xf_round2 || xf_c == G_XDIST;               /* Distribute: FUN_1800129c0 posts 0x66 once the vector is in (MX l.2807) */
    xf_round2 = 0;
    if (xf_mod) xf_rebuild(); else xf_drop();            /* the first round: no 0x66 posted, no preview until the dialog is used */
    xf_snap(); xf_msg();
}
static void xf_close(void);
static void xdy_close(void);
/* Stretch's selection: windows in In + Intersect mode with dbselwin_stretching on (mastercam.dll FUN_18020e440 /
   FUN_18020d670): an entity wholly inside is selected (code 3: moved whole), a line crossing with its start inside gets
   code 2 (sel bit 0x200), with its end inside code 1 (0x40), other crossing entities are not selected; several windows
   add up (FUN_180204d80: 1 + 2 = 3). Ours: a window is two picked corners in world XY (the Top view), not a drag. */
static unsigned char *xst_code; static int xst_win_n; static V3 xst_c0;
static int in_box(V3 p, double x0, double y0, double x1, double y1) { return p.x >= x0 - XF_TOL && p.x <= x1 + XF_TOL && p.y >= y0 - XF_TOL && p.y <= y1 + XF_TOL; }
static void xst_window(V3 a, V3 b) {
    double x0 = fmin(a.x, b.x), x1 = fmax(a.x, b.x), y0 = fmin(a.y, b.y), y1 = fmax(a.y, b.y);
    unsigned char *c = calloc(F.nents + 1, 1); if (!c) return;
    if (xst_code) memcpy(c, xst_code, xst_win_n); free(xst_code); xst_code = c; xst_win_n = F.nents;
    for (int e = 0; e < F.nents; e++) {
        const Ent *en = &F.ents[e]; if (!en->type || en->gen || en->type == E_SPLINE || en->type == E_SURF || en->type == E_NOTE) continue;   /* spline curves / surfaces / notes: not transformed here (NOT PORTED) */
        int all = 1;
        for (int pi = en->first_poly; pi < en->first_poly + en->npoly && all; pi++)
            for (int k = 0; k < F.polys[pi].count && all; k++) all = in_box(F.pts[F.polys[pi].first + k], x0, y0, x1, y1);
        if (en->type == E_POINT) all = in_box(en->a, x0, y0, x1, y1);
        if (all) { xst_code[e] = 3; continue; }
        if (en->type != E_LINE) continue;                     /* a crossing non-line: deselected (LAB_18020da31) */
        int ia = in_box(en->a, x0, y0, x1, y1), ib = in_box(en->b, x0, y0, x1, y1);
        if (ia && !ib) xst_code[e] |= 2; else if (ib && !ia) xst_code[e] |= 1;
    }
    sel_grow(); for (int e = 0; e < F.nents && e < ent_sel_n; e++) if (xst_code[e]) { if (!ent_sel[e]) nselected++; ent_sel[e] = 1; }
    overlay_dirty = 1;
}
/* the end of the selection: dbselwin_stretching(0) is true only when a line has exactly one end inside; else the command
   ends without the dialog (do_xform_stretch: cVar1 == 0 -> LAB_1800474d7) */
static void xst_done(void) {
    int any = 0; for (int e = 0; e < xst_win_n; e++) if (xst_code[e] == 1 || xst_code[e] == 2) any = 1;
    if (!any) { xf_close(); geo_end(); return; }
    free(xf_l); xf_l = malloc(sizeof(int) * (xst_win_n + 1)); xf_n = 0; free(xst_end); xst_end = malloc(xst_win_n + 1);
    for (int e = 0; e < xst_win_n; e++) if (xst_code[e] && F.ents[e].type) { xst_end[xf_n] = xst_code[e]; xf_l[xf_n++] = e; }
    xf_dialog();
}
/* the reset each do_xform_* makes at its start (the agents' spec: formats/wireframe.md "Transform") */
static void xf_reset(int c) {
    int metric = mcam_is_metric(&F);
    switch (c) {
    case G_XTRANS: { int m = xt.mode; memset(&xt, 0, sizeof xt); xt.mode = m; xt.copies = 1; xt.level = -1; xt.color = -1; break; }
    case G_XROT: { int m = xr.mode; memset(&xr, 0, sizeof xr); xr.mode = m; xr.copies = 1; xr.level = -1; xr.color = -1; break; }
    case G_XMIRROR: { int m = xmi.mode; memset(&xmi, 0, sizeof xmi); xmi.mode = m; xmi.axis = -1; xmi.ang = 45; xmi.level = -1; xmi.color = -1; break; }
    case G_XARRAY: memset(&xa, 0, sizeof xa); xa.n1 = xa.n2 = 2; xa.l1 = xa.l2 = metric ? 25.0 : 1.0; xa.a1 = 0; xa.a2 = 90; xa.level = -1; xa.color = -1; break;
    case G_XSCALE: { int m = xs.mode; memset(&xs, 0, sizeof xs); xs.mode = m; xs.copies = 1; xs.s = xs.sx = xs.sy = xs.sz = 1.0; xs.level = -1; xs.color = -1; break; }
    case G_XSTRETCH: { int m = xst.mode; memset(&xst, 0, sizeof xst); xst.mode = m; xst.copies = 1; xst.level = -1; xst.color = -1; break; }
    case G_XDIST:                                        /* MX l.92-105 (+ the dialog ctor's: no vector, edge -1, margin lock on) */
        memset(&xd, 0, sizeof xd); xd.mode = 1; xd.equal = 1; xd.n = 1; xd.just = 1; xd.level = -1; xd.color = -1; xd.novec = 1; xd.edge = -1; xd.mlock = 1; break;
    case G_XTRANS3D: { int m = x3.mode; memset(&x3, 0, sizeof x3); x3.mode = m; x3.copies = 1; x3.vfrom = x3.vto = 1;   /* the active view (the Cplane is Top here) */ x3.level = -1; x3.color = -1; break; }
    }
}
static void xdy_begin(void);
int xf_begin(int c) {
    xf_c = c; xf_pickfor = 0; xf_prev = 0; xf_mod = 0; xf_round2 = 0; xf_rm_free();
    if (c == G_XDYN) { xdy_begin(); return 1; }          /* DynamicXform (cui_xdyn.c + below) */
    if (c == G_XORIGIN) { xf_phase = XP_PICK; xf_pickfor = 50; xf_msg(); return 1; }   /* point_proc only */
    xf_reset(c);
    int n = 0; for (int i = 0; i < F.nents; i++) n += is_sel(i);
    if (c == G_XSTRETCH) { free(xst_end); xst_end = NULL; xst_win_n = 0; free(xst_code); xst_code = NULL; n = 0; }   /* window selection (below) */
    if (n) { xf_take_selection(); if (c == G_XDIST) xf_pick(70); else xf_dialog(); }   /* preselected: select_ents returns at once (MC_PRESEL 2) */
    else xf_begin_selection();
    return 1;
}
static void xf_close(void) {
    if (!xf_c) return;
    if (xf_c == G_XDYN) { gn_reset(); xdy_close(); }      /* the gnomon removed (EmRemoveSelObj, FUN_1800ee980) */
    xf_c = 0; xf_phase = 0; xf_pickfor = 0; free(xf_l); xf_l = NULL; xf_n = 0; memset(&xsc, 0, sizeof xsc);
    sel_clear(); overlay_dirty = 1;
}
void xf_abort(void) { if (xf_c) { xf_drop(); xf_close(); } }   /* another command / the panel closed: as Cancel */
static void xdy_selected(void);
int xf_enter_ok(void) { return xf_selecting() || (xf_c && xf_phase == XP_PICK && xf_pickfor == 80); }
void xf_enter(void) {                                  /* the end of select_ents; Remove Item's Enter (XfWfMgrSelectCopy returns 3) */
    if (xf_c && xf_phase == XP_PICK && xf_pickfor == 80) { xf_button(XB_RMEND); return; }
    if (!xf_selecting()) return;
    if (xf_c == G_XSTRETCH) { xst_done(); return; }
    if (xf_c == G_XDYN) { xdy_selected(); return; }
    xf_take_selection();
    if (!xf_n) { xf_close(); geo_end(); return; }       /* nothing selected: the command ends (local_328[0] == 0) */
    if (xf_c == G_XDIST && xd.novec) { xf_pick(70); return; }   /* TransformDistribute: the fit vector next (FUN_180012b80) */
    xf_dialog();
}
/* ---------------------------------------------------------------- Transform > Dynamic (DynamicXform) */
/* formats/wireframe.md "Dynamic - port spec (2026-11-04)": MX = decomp/xform/mccadutil_xform.c (main FUN_1800eb210, round
   FUN_1800eb6c0 l.12987), decomp/xform/dynamic/dyn1.c / dyn2.c / dyn3.c (idle FUN_1800ec560, drag FUN_1800e9540, commit
   FUN_1800e89e0, the panel codes FUN_1800eac50, Origin FUN_1800ea940 / Axes FUN_1800e7c50, Ctrl FUN_1800eb0a0). The gnomon
   widget is cui_xdyn.c. Matrices (MtTmat3d row vectors in Mastercam) are XM here: T = B^-1 C there = C after B^-1 here.
   Ours / deviations: the preview is the result itself (an undo step, as the other Transform commands here), not WfMgr's
   phantom-coloured copies, so A (+0x278, the transforms committed this round) is not needed - a keep-commit makes the
   results the selection and the next instances start from them; the preview of Only-apply-last uses the commit's rule
   (T then Tlast^(k-1), not FUN_1800e9260's P x Tlast with a bank); the banked phantoms (path 7, df_phantom_color NOT
   DECODED) are not drawn apart - the preview shows P T; Move with more than one instance places the originals as the
   other commands here (NOT DECODED in xform_silent); the gnomon button (GrButton, bitmaps NOT DECODED) is the panel's
   "Gnomon button" (ours); the 2-D construction flag of the Origin buttons (FUN_1800ea940 param_4) is not used. */
typedef struct { int copy, single, count, last, setgeom; } XfDyn;   /* XformDynamicManager: IsCopy +0x50, IsSingle +0x60, CopyCount
                                                                       +0x90, OnlyApplyLastTransform +0x70, SetToGeometryWhenPlaced +0x80 */
static XfDyn xdp = { 0, 1, 1, 0, 1 };                 /* FUN_1800f1440 / FUN_1800f29c0: Move, Single, 1, off, on; the session statics
                                                         DAT_1802447d0 (enable_dialog_modality = 1: kept between runs) */
static int xdp_copy(void) { return xdp.copy; }
static int sm[6] = { 1, 1, 1, 1, 1, 1 };               /* the gnomon settings' s_SelMask bits as check boxes (default 0x3f) */
enum { DY_PLACE = 1, DY_IDLE, DY_DRAG };
static int xdy_st, xdy_item;                           /* the round's step; the SelItem being dragged (local_7d4) */
static int xdy_mode = 1;                               /* +0x389: 1 gnomon ("Manipulate Axes"), 0 geometry ("Manipulate geometry") */
static int xdy_38a = 1, xdy_38e = 1;                   /* +0x38a (gnomon button / Ctrl allowed in geometry mode), +0x38e (ctor 0x10001) */
static int xdy_pend, xdy_bank, xdy_drag, xdy_pend0;    /* +0x38f a transform is pending, +0x394 banked, +0x38d dragging, local_7d6 */
static int xdy_s390, xdy_s391 = 1, xdy_s398 = 1;       /* IsCopy / IsSingle / CopyCount kept for the next round (ctor: 0 / 1 / 1) */
static int xdy_again, xdy_attach;                      /* the round re-runs after the gnomon button (param_4); Multiple: the edit goes on */
static XM xdy_B, xdy_Bi, xdy_C, xdy_E, xdy_P;          /* +0x58 B, +0x1f0 B^-1, +0x168 C, +0xe0 E, +0x300 P */
static int xdy_n = 1;                                  /* the instances of the next build (preview k / the commit's n) */
int xdy_ctrl_held;
static XM gm2xm(GnMat g) { XM a; memcpy(a.m, g.m, sizeof a.m); memcpy(a.t, g.t, sizeof a.t); return a; }
static void xdy_eab50(int b) { xdy_C = gm2xm(gn_C); if (b) { xdy_B = xdy_C; xdy_Bi = xm_inv(&xdy_B); } }   /* FUN_1800eab50 */
static XM xdy_T(void) { return xm_mul(&xdy_C, &xdy_Bi); }                                      /* T = B^-1 C */
static XM xdy_M(void) { XM T = xdy_T(); return xdy_bank ? xm_mul(&T, &xdy_P) : T; }            /* with a bank: P T */
/* the instances (FUN_1800e89e0 -> FUN_1800ed800 = xform_silent: instance k = M^k; Only apply last and n >= 2: M once, then
   E^-1 C n - 1 times on its results) */
static XM xdy_bM, xdy_bE; static int xdy_bkey[3] = { -1, -1, -1 };   /* what the preview was built from (ours: a commit with the same values keeps it) */
static void build_dyn(void) {
    XM M = xdy_M(); int n = xdy_n < 1 ? 1 : xdy_n;
    xdy_bM = M; xdy_bE = xdy_E; xdy_bkey[0] = xdp.copy; xdy_bkey[1] = n; xdy_bkey[2] = xdp.last;
    if (xdp.last && n >= 2) {
        XM Ei = xm_inv(&xdy_E), Tl = xm_mul(&xdy_C, &Ei); xi_add(M);
        for (int k = 2; k <= n; k++) { XM a = xm_pow(&Tl, k - 1); xi_add(xm_mul(&a, &M)); }
    } else for (int k = 1; k <= n; k++) xi_add(xm_pow(&M, k));
}
/* the preview (FUN_1800e9260, notify 0x18): k = CopyCount when Single and not dragging, else 1; shown while a transform is
   pending (+0x392 := +0x38f, FUN_1800eb120) */
static void xdy_preview(void) {
    if (xdy_pend) { xdy_n = xdp.single && !xdy_drag ? xdp.count : 1; xf_make(); }
    else xf_drop();
}
/* FUN_1800e89e0(copy, n, keep): the instances made (UndoXformTransStart .. Commit); keep: B := C and the results become the
   selection (+0x3c8); always P := I */
static void xdy_commit(int copy, int n, int keep) {
    XM M = xdy_M();
    int same = xf_prev_valid() && xdy_bkey[0] == copy && xdy_bkey[1] == (n < 1 ? 1 : n) && xdy_bkey[2] == xdp.last && !memcmp(&M, &xdy_bM, sizeof M)
               && (!xdp.last || !memcmp(&xdy_E, &xdy_bE, sizeof xdy_E));
    if (!same) { int sc = xdp.copy; xdp.copy = copy; xdy_n = n; xf_make(); xdp.copy = sc; }   /* (the preview already is this result: kept) */
    int from = xf_last_from, ok = xf_prev_valid(); xf_prev = 0;
    if (keep) {
        xdy_eab50(1);
        if (ok) {
            int k = 0, *l = malloc(sizeof(int) * (size_t)(F.nents - from + 1));
            for (int e = from; e < F.nents && l; e++) if (F.ents[e].type && (xf_ent_mark(e) & 8)) l[k++] = e;
            if (l) { free(xf_l); xf_l = l; xf_n = k; }
        }
    }
    xdy_P = xm_id(); xdy_bank = 0;
}
static void xdy_round(int presel);
/* the round's end, LAB_1800ec0e3: 4 OK / 5 Apply commit a pending transform (IsCopy, CopyCount, keep 0); 0 / 3 / 6 drop it
   (FUN_1800ed100, P := I); 0 / 4 end the command, 3 / 5 / 6 select again */
static void xdy_end(int u) {
    if (xdy_drag) { gn_cancel(); xdy_drag = 0; }
    xdy_attach = 0; gn_show(0);                         /* EmRemoveSelObj */
    int commit = !(u == 0 || u == 3 || u == 6) && xdy_pend;
    if (commit) xdy_commit(xdp.copy, xdp.count, 0);
    if (!commit || u == 5) { xf_drop(); xdy_P = xm_id(); xdy_bank = 0; }
    sel_clear();                                        /* set_clr_sel_bits_all(0, 4, 0, 4, 0x1f1fff) */
    xdy_pend = 0; xdy_st = 0;
    if (u == 0 || u == 4) { xf_close(); geo_end(); return; }
    xdy_round(0);
}
/* step 3 (LAB_1800eb9a0): gnomon mode, the panel off, select_ents(440, mask 0x171fff, MC_PRESEL 2 the first time) unless a bank */
static void xdy_after(void);
static void xdy_round(int presel) {
    xdy_mode = 1; xdy_st = 0; gn_show(0);
    if (xdy_bank) { xdy_after(); return; }
    if (presel) { int n = 0; for (int i = 0; i < F.nents; i++) n += is_sel(i); if (n) { xdy_selected(); return; } }
    xf_begin_selection();
}
static void xdy_selected(void) {                        /* select_ents returned: FUN_1800ea720 */
    xf_take_selection();
    if (!xf_n) { xf_close(); geo_end(); return; }       /* nothing: the command ends (LAB_1800ec417) */
    xf_marks_clear();                                    /* set_clr_sel_bits_all(0, 10, ...): bits 2 / 8 off */
    xdy_after();
}
/* steps 4 / 5 (LAB_1800eba65): the gnomon on the Cplane (again: where it is), A := I, B := C, the panel on with IsSingle /
   CopyCount from +0x391 / +0x398, then the placement drag of the origin (EmSetSelObjEdit(gnomon, 2)) unless again */
static void xdy_after(void) {
    int again = xdy_again; xdy_again = 0;
    xf_phase = XP_DYN;
    if (!again) {                                       /* SetDefMat(cplaneMat^-1): the Cplane (Top here, cui_planes.c) */
        const double *m = cam_plane_matrix(plane_active[0]), *o = plane_active_origin(0);
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) gn_C.m[3 * r + c] = m[3 * c + r];
        memcpy(gn_C.t, o, sizeof gn_C.t);
    }
    gn_show(1); xdy_eab50(1);
    xdp.single = xdy_s391; xdp.count = xdy_s398;        /* FUN_1800f2cd0 */
    xdy_pend = xdy_bank;                                 /* +0x38f := +0x394 */
    if (!again) { xdy_st = DY_PLACE; xdy_drag = 1; xdy_item = 2; xdy_E = xdy_C; gn_begin(2, 0, 0); }
    else xdy_st = DY_IDLE;
    xdy_preview(); xf_snap(); xf_msg(); overlay_dirty = 1;
}
static void xdy_begin(void) {                           /* XformDynamicApp ctor FUN_1800e6fc0 + FUN_1800eb210 */
    xdy_mode = 1; xdy_38a = 1; xdy_38e = 1; xdy_pend = xdy_bank = xdy_drag = xdy_attach = xdy_again = 0;
    xdy_s390 = xdp.copy; xdy_s391 = 1; xdy_s398 = 1; xdy_P = xm_id(); xdy_B = xdy_Bi = xdy_C = xdy_E = xm_id();
    gn_reset();
    xdy_round(1);
}
static void xdy_close(void) { xdy_st = 0; xdy_drag = xdy_attach = xdy_pend = xdy_bank = 0; xdy_P = xm_id(); }
/* a drag of item id starts (MX l.13409-13447): geometry mode makes a transform pending (local_7d6 kept), E := C */
static void xdy_drag_start(int id, float mx, float my) {
    xdy_item = id;
    if (!xdy_mode) { xdy_pend0 = xdy_pend; xdy_pend = 1; }
    xdy_E = gm2xm(gn_C); xdy_drag = 1; xdy_st = DY_DRAG;
    gn_begin(id, mx, my);
    xf_msg();
}
static void xdy_drag_moved(void) {                      /* 0x800000: geometry mode -> C := GetDefMat, the live preview */
    if (!xdy_mode) { xdy_C = gm2xm(gn_frame()); xdy_preview(); }
    overlay_dirty = 1;
}
static void xdy_place_done(int code);
/* the drag's end (FUN_1800e9540 returns 1 / 2 / 4 / 5 / 6): code 1 released, 2 cancelled (Esc / right click) */
static void xdy_drag_done(int code, float mx, float my) {
    if (xdy_st == DY_PLACE) { xdy_place_done(code); return; }
    if (code == 1 && !xdy_mode && !xdp.single) {         /* geometry + Multiple: a copy at once, the edit goes on (0x1800e9b32) */
        gn_apply(); xdy_C = gm2xm(gn_C);                 /* +0x168 := GetDefMat (the dragged frame) */
        xdy_commit(1, 1, 1);                             /* FUN_1800e89e0(1, 1 [inferred], 1): B := C, the copies become the selection */
        xdy_E = gm2xm(gn_C); gn_begin(xdy_item, mx, my);   /* EmSetSelObjEdit(param_2) */
        xdy_attach = 1; xdy_C = gm2xm(gn_C); xdy_preview(); xf_msg(); overlay_dirty = 1;
        return;
    }
    if (code == 1) gn_apply(); else gn_cancel();
    xdy_drag = 0; xdy_attach = 0;
    if (!xdp.single && !xdy_mode) { xdy_eab50(xdy_mode); xdy_end(6); return; }   /* Multiple in geometry mode: code 6 */
    if (code == 2) { xdy_38a = 1; xdy_pend = xdy_pend0; }   /* EmCancelSelObjEdit */
    xdy_eab50(xdy_mode);                                  /* gnomon mode: B := C (the gnomon moves alone) */
    xdy_st = DY_IDLE; xdy_preview(); xf_msg(); overlay_dirty = 1;
}
/* the placement drag's end (bVar4): Esc -> code 6; else Set to geometry when placed -> geometry mode */
static void xdy_place_done(int code) {
    if (code != 1) { gn_cancel(); xdy_drag = 0; xdy_end(6); return; }
    gn_apply(); xdy_drag = 0;
    xdy_eab50(xdy_mode);
    if (xdp.setgeom && (xdy_mode || xdy_38a)) xdy_mode = 0;   /* FUN_1800ecda0(param_1, +0x389 == 0) (MX l.13510) */
    xdy_st = DY_IDLE; xdy_preview(); xf_msg(); overlay_dirty = 1;
}
/* path 7, the gnomon button with a transform pending (FUN_1800e8df0 + MX l.13285-13340): P := P T, IsCopy / IsSingle /
   CopyCount kept, the round again with the gnomon where it is and no selection */
static void xdy_bank_it(void) {
    XM T = xdy_T(); xdy_P = xm_mul(&T, &xdy_P);
    xdy_s398 = xdp.count; xdy_s391 = xdp.single; xdy_s390 = xdp.copy;
    xdy_bank = 1; xdy_again = 1;
    xdy_round(0);
}
static void xdy_gnomon_button(void) {                   /* event 0x4000000 (FUN_1800ec560): needs +0x38a, else MessageBeep */
    if (xdy_st != DY_IDLE) return;
    if (!xdy_38a) { snprintf(cam_msg, sizeof cam_msg, "%s", xdy_prompt()); return; }
    if (!xdy_mode) xdy_mode = 1;                          /* FUN_1800eac00: gnomon mode */
    if (xdy_pend) { xdy_bank_it(); return; }
    xf_msg(); overlay_dirty = 1;
}
/* the Origin / Axes buttons (FUN_1800eac50 0x1a-0x21): Pl = Cplane / World / Tplane / WCS; Origin: C's translation := the plane's
   origin (FUN_1800ea940), Axes: C's axes := the plane's (FUN_1800e7c50); geometry mode: a transform pending (T = B^-1 C') */
static void xdy_plane(int code) {
    int which = (code - 0x1a) % 4, id = which == 0 ? plane_active[0] : which == 2 ? plane_active[1] : which == 3 ? plane_active[3] : 0;
    GnMat P = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 0, 0, 0 } };
    if (id) { const double *m = cam_plane_matrix(id), *o = plane_active_origin(which == 2 ? 1 : which == 3 ? 3 : 0);   /* UtGetCPlaneMat / UtGetTPlaneMat / UtGetWcsMat */
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) P.m[3 * r + c] = m[3 * c + r];
        memcpy(P.t, o, sizeof P.t); }
    GnMat C = gn_C;
    if (code < 0x1e) memcpy(C.t, P.t, sizeof C.t); else memcpy(C.m, P.m, sizeof C.m);
    gn_C = C;                                            /* vt +0x188 */
    if (!xdy_mode) { xdy_pend = 1; xdy_38a = 0; }
    xdy_eab50(xdy_mode);
    xdy_preview(); overlay_dirty = 1;
}
static const char *xdy_prompt(void) {                   /* mccadutilres 441-474 (FUN_1800ec560 idle, FUN_1800e9540 drag) */
    if (xdy_st == DY_PLACE) return "Pick gnomon origin position or Ctrl-right-click for gnomon settings";   /* 455 */
    int g = xdy_mode;
    if (xdy_st == DY_DRAG) switch (gn_edit_kind(gn_edit_mode(xdy_item))) {
        case 1: return g ? "Pick gnomon origin position or Ctrl-right-click for gnomon settings" : "Pick new origin position of selected geometry or Ctrl-right-click for gnomon settings";   /* 455 / 457 */
        case 2: return g ? "Pick gnomon translation position or Ctrl-right-click for gnomon settings" : "Pick new translation position of selected geometry or Ctrl-right-click for gnomon settings";   /* 464 / 465 */
        case 3: return g ? "Pick entity to align gnomon to or Ctrl-right-click for gnomon settings" : "Pick entity to align selected geometry to or Ctrl-right-click for gnomon settings";   /* 469 / 471 */
        case 4: return g ? "Pick gnomon rotation position or Ctrl-right-click for gnomon settings" : "Pick new rotation position of selected geometry or Ctrl-right-click for gnomon settings";   /* 472 / 474 */
        default: return "";
    }
    switch (gn_edit_kind(gn_edit_mode(gn_hover))) {     /* the highlighted item's edit mode (+0xfcc) */
    case 1: return g ? "Manipulate axes: Translate XYZ/Polar" : "Manipulate geometry: Translate XYZ/Polar";   /* 442 / 447 */
    case 2: return g ? "Manipulate axes: Translate Along" : "Manipulate geometry: Translate Along";   /* 443 / 448 */
    case 3: return g ? "Manipulate axes: Xform Align" : "Manipulate geometry: Xform Align";   /* 445 / 454 */
    case 4: return g ? "Manipulate axes: Xform Rotate" : "Manipulate geometry: Xform Rotate";   /* 444 / 452 */
    default: return g ? "Manipulate Axes: Select gnomon axes to edit, switch mode to geometry manipulation when complete"   /* 441 */
                      : "Manipulate geometry: Select gnomon axes to edit or press Apply/OK or Double-click the mouse to accept the results";   /* 446 */
    }
}
/* the panel codes while the gnomon is up (FUN_1800ec560 idle / FUN_1800e9540 drag): 0 OK -> 4, 1 Apply -> 5 (Ctrl: commit with
   keep and stay, 0x1800ebc92), our 2 = Cancel (3) -> 6, Reselect (6) -> 3, the Origin / Axes buttons */
enum { XB_DBTN = 90, XB_DCTRL = 101 };
static int xdy_button(int code) {
    if (code == XB_DCTRL) { code = 1; xdy_ctrl_held = 1; }
    int ctrl = xdy_ctrl_held; xdy_ctrl_held = 0;
    if (code >= 0x1a && code <= 0x21) { xdy_plane(code); return 1; }
    if (code == XB_DBTN) { xdy_gnomon_button(); return 1; }
    if (code == 1 && ctrl && xdy_st == DY_IDLE) {         /* Ctrl+Apply: FUN_1800e89e0(IsCopy, CopyCount, keep 1), the same geometry */
        xdy_commit(xdp.copy, xdp.count, 1); xdy_preview(); xf_msg(); return 1; }
    if (code == 0) { xdy_end(4); return 1; }
    if (code == 1) { xdy_end(5); return 1; }
    if (code == 2) {                                     /* Cancel / Esc: the placement / a drag returns 2 first (get_event 1), else 6 */
        if (xdy_st == DY_DRAG || xdy_st == DY_PLACE) { xdy_drag_done(2, 0, 0); return 1; }
        xdy_end(6); return 1; }
    if (code == 15) { xdy_end(3); return 1; }             /* XB_RESEL (the Reselect button, code 6 there) */
    return 1;
}
/* ---- the front end's view events (ours: the get_event masks' mouse cases, routed here) */
int xdyn_on(void) { return xf_c == G_XDYN && xf_phase == XP_DYN; }
int xdyn_press(float mx, float my, int dbl) {
    if (!xdyn_on()) return 0;
    if (xdy_st != DY_IDLE) return 1;                     /* the placement / a drag: its end is the release */
    if (dbl) { if (xdy_pend) xdy_end(5); return 1; }      /* event 0x800 -> 5 while Apply is enabled (prompt 446) */
    int id = gn_pick(mx, my);                            /* 0x400000: EmGetSelItem */
    if (id) xdy_drag_start(id, mx, my);
    return 1;                                            /* the idle get_event takes no entity picks */
}
int xdyn_move(float mx, float my) {
    if (!xdyn_on()) return 0;
    if (xdy_st == DY_PLACE || xdy_st == DY_DRAG) { if (gn_move(mx, my)) xdy_drag_moved(); return 1; }
    int id = gn_pick(mx, my);
    if (id != gn_hover) { gn_set_hover(id); xf_msg(); return 1; }
    return 0;
}
int xdyn_release(float mx, float my) {
    if (!xdyn_on()) return 0;
    if (xdy_st == DY_PLACE || xdy_st == DY_DRAG) { gn_move(mx, my); xdy_drag_done(1, mx, my); }   /* 0x2000000 */
    return 1;
}
int xdyn_rclick(void) { if (!xdyn_on()) return 0; xf_button(2); return 1; }
int xdyn_ctrl(void) {                                    /* FUN_1800eb0a0(0x11): not while dragging, gnomon mode or +0x38a */
    if (!xdyn_on() || xdy_drag || !(xdy_mode || xdy_38a)) return 0;
    xdy_mode = !xdy_mode; xf_msg(); overlay_dirty = 1;
    return 1;
}
int xdyn_enter(void) { if (!xdyn_on()) return 0; xf_button(1); return 1; }   /* (Ctrl+Enter: the UI sets xdy_ctrl_held) */   /* Enter (8, 0xd) -> panel code 1 */
/* tests (ours): dyndrag:ID,V[,V2,V3] - a whole drag of SelItem ID to a value (cui_xdyn.c gn_set_value), dynplace:X,Y,Z -
   the placement click at a point, dynctrl, dynbtn, dyndump */
static void xdy_dump(void) {
    XM T = xdy_M();
    printf("    xfdyn st %d mode %s pend %d bank %d copy %d single %d n %d last %d setgeom %d sel %d\n", xdy_st, xdy_mode ? "gnomon" : "geometry",
           xdy_pend, xdy_bank, xdp.copy, xdp.single, xdp.count, xdp.last, xdp.setgeom, xf_n);
    printf("    xfdyn C %.4f %.4f %.4f | %.4f %.4f %.4f | %.4f %.4f %.4f + %.4f %.4f %.4f\n", gn_C.m[0], gn_C.m[1], gn_C.m[2], gn_C.m[3], gn_C.m[4], gn_C.m[5], gn_C.m[6], gn_C.m[7], gn_C.m[8], gn_C.t[0], gn_C.t[1], gn_C.t[2]);
    printf("    xfdyn T %.4f %.4f %.4f | %.4f %.4f %.4f | %.4f %.4f %.4f + %.4f %.4f %.4f\n", T.m[0], T.m[1], T.m[2], T.m[3], T.m[4], T.m[5], T.m[6], T.m[7], T.m[8], T.t[0], T.t[1], T.t[2]);
}
static int xdy_script(const char *st, const char *arg) {
    if (!strcmp(st, "dyndump")) { if (xf_c == G_XDYN) xdy_dump(); return 1; }
    if (xf_c != G_XDYN) return 0;
    if (!strcmp(st, "dynctrl")) { xdyn_ctrl(); return 1; }
    if (!strcmp(st, "dynbtn")) { xf_button(XB_DBTN); return 1; }
    if (!strcmp(st, "dynplace") && arg) {
        double v[3] = { 0, 0, 0 }; sscanf(arg, "%lf,%lf,%lf", &v[0], &v[1], &v[2]);
        if (xdy_st == DY_PLACE) { gn_set_value(2, v, 3); xdy_drag_done(1, 0, 0); }
        return 1; }
    if (!strcmp(st, "dyndrag") && arg) {
        int id = 0; double v[4] = { 0, 0, 0, 0 }; int nv = sscanf(arg, "%d,%lf,%lf,%lf,%lf", &id, &v[0], &v[1], &v[2], &v[3]) - 1;
        if (xdy_st != DY_IDLE && !(xdy_st == DY_DRAG && xdy_attach)) return 1;
        if (!(xdy_st == DY_DRAG && xdy_attach && id == xdy_item)) xdy_drag_start(id, 0, 0);
        gn_set_value(id, v, nv < 0 ? 0 : nv); xdy_drag_moved(); xdy_drag_done(1, 0, 0);
        printf("    dyndrag %d snapped %d\n", id, gn_snapped3d);
        return 1; }
    return 0;
}
/* Move to Origin (do_xform_movetoorigin): every shown entity, T = active origin - the picked point; in 2D construction
   the picked point's depth is kept (only the Cplane XY moves); translate_silent = a move, one undo step */
static void xf_origin(V3 p) {
    int from = F.nents; (void)from;
    V3 to = { 0, 0, cfg_constr_3d ? 0 : p.z };          /* GetActiveOrigin (Top, the world origin here) */
    XM M = xm_trans(to.x - p.x, to.y - p.y, to.z - p.z);
    xf_refused = 0; xf_marks_clear(); mod_begin();
    int n0 = F.nents;
    for (int e = 0; e < n0; e++) {
        if (!F.ents[e].type || F.ents[e].gen || F.ents[e].blanked || (ent_hidden && e < nent_hidden && ent_hidden[e])) continue;   /* blanked: a trimmed surface's base goes with it */
        int r = xf_ent(e, &M, F.ents[e].level, F.ents[e].color); if (r < 0) continue;
        xf_mark(r, 8); mod_del(e);
    }
    mod_end(); doc_dirty = 1; scene_reupload(); overlay_dirty = 1;
}

/* ---------------------------------------------------------------- picks from the dialog's buttons */
int xf_need(void) {
    if (xf_c == G_XSTRETCH && xf_phase == XP_SEL) return 'p';   /* the window corners */
    if (xf_c == G_XMIRROR && xf_phase == XP_DLG) return 'e';     /* a line clicked while the dialog is up = the Line button (do_xform_mirror's get_event) */
    if (!xf_c || xf_phase != XP_PICK) return 0;
    if (xf_pickfor == 70 && !xf_pickstep) return 'e';
    if (xf_pickfor == 80) return 'e';                    /* XfWfMgrSelectCopy: get_event(.., 10, 0x1f1fff, 8): an entity */
    if (xf_pickfor == 90 || xf_pickfor == 92 || xf_pickfor == 93) return 'e';   /* get_event(.., 0x4002, 0x400002) a line / (0x6002, 0x400004) an arc */
    if (xf_pickfor == 91) return 'p';                    /* get_event(.., 1, 0, 0) twice: points */   /* get_event(.., 3, 2, 1): a line, or a point (ours: a click off a line = the point, unsnapped) */
    return xf_pickfor == 11 || xf_pickfor == 33 ? 'e' : 'p';
}
static void xf_pick(int what) { xf_phase = XP_PICK; xf_pickfor = what; xf_pickstep = 0; xf_msg(); overlay_dirty = 1; }
static void xf_back(void) { xf_phase = XP_DLG; xf_pickfor = 0; xf_mod = 1; xf_rebuild(); xf_snap(); xf_msg(); }   /* the pick buttons end with FUN_18000bd80(0x66) */   /* the pick is no panel edit */
static void trans_from_to(XfTrans *p) {               /* FUN_180045390 -> FUN_180045430: delta = TO - FROM (Top: view = world) */
    if (!cfg_constr_3d && (fabs(p->to.x - p->from.x) >= XF_TOL || fabs(p->to.y - p->from.y) >= XF_TOL)) p->to.z = p->from.z;   /* FUN_180044f80 */
    p->dx = p->to.x - p->from.x; p->dy = p->to.y - p->from.y; p->dz = p->to.z - p->from.z;
    double L = hypot(p->dx, p->dy);
    if (L > XF_TOL) { p->plen = L; p->pang = atan2(p->dy, p->dx) / D2R; if (p->pang < 0) p->pang += 360; }   /* FUN_1800457d0 */
}
/* the calculator: the value measured (FUN_180039100 base / FUN_180038b90 result): kept at >= tol (+0x4300 / +0x50f8 = tol, the ctor) and
   <= +0x4308 / +0x5100 (the MCReal's maximum: MCReal::MCReal is mastercam.dll, NOT DECODED - not clamped here), then factor = result / base */
static void xsc_set(double v) {
    double *t = xsc_which ? &xsc.res : &xsc.base;
    *t = v < XF_TOL ? XF_TOL : v;
    xsc.fac = xsc.res / xsc.base;
}
static void xsc_back(void) { xf_phase = XP_DLG; xf_pickfor = 0; xf_pickstep = 0; xf_snap(); xf_msg(); overlay_dirty = 1; }
static V3 xsc_p1;
/* the length of a - b: 2D construction (constr_view_3d false) in the Cplane's 2D (world_to_2d in the active view: Top here), else 3D;
   kept only when >= tol (FUN_180039610 / FUN_180039920) */
static double xsc_len(V3 a, V3 b) { double dx = b.x - a.x, dy = b.y - a.y, dz = cfg_constr_3d ? b.z - a.z : 0; return sqrt(dx * dx + dy * dy + dz * dz); }
static void xsc_take(int e, V3 w) {
    if (xf_pickfor == 91) {                            /* FUN_180039920: two points */
        if (!xf_pickstep) { xsc_p1 = w; xf_pickstep = 1; xf_msg(); return; }
        double L = xsc_len(xsc_p1, w); if (L >= XF_TOL) xsc_set(L);
        xsc_back(); return;
    }
    if (e < 0 || e >= F.nents || !F.ents[e].type) { xsc_back(); return; }   /* no entity: the measurement fails */
    const Ent *en = &F.ents[e];
    if (xf_pickfor == 90) {                            /* FUN_180039610: a line (ent type 2), its length */
        if (en->type == E_LINE) { double L = xsc_len(en->a, en->b); if (L >= XF_TOL) xsc_set(L); }
    } else if (en->type == E_ARC) {                     /* FUN_180039310(.., 2.0) diameter / (.., 1.0) radius: an arc (type 4) - the solid face
                                                           branch (event 0x2000, sld_face_radius) has no counterpart here */
        double v = en->r * (xf_pickfor == 92 ? 2.0 : 1.0); if (v >= XF_TOL) xsc_set(v);
    }
    xsc_back();
}
static void xsc_measure(int which) { xsc_which = which; xf_phase = XP_PICK; xf_pickfor = 90 + (which ? xsc.cr : xsc.cb); xf_pickstep = 0; xf_msg(); overlay_dirty = 1; }
void xf_take(int e, V3 w) {
    if (xf_c == G_XMIRROR && xf_phase == XP_DLG) { if (e < 0 || e >= F.nents || F.ents[e].type != E_LINE) return; xf_phase = XP_PICK; xf_pickfor = 33; }
    if (xf_c == G_XSTRETCH && xf_phase == XP_SEL) {
        if (!xf_pickstep) { xst_c0 = w; xf_pickstep = 1; } else { xst_window(xst_c0, w); xf_pickstep = 0; }
        return;
    }
    if (!xf_c || xf_phase != XP_PICK) return;
    XfTrans *tp = xf_c == G_XSTRETCH ? &xst : &xt;     /* Stretch shares Translate's handlers (message map 18017e210) */
    switch (xf_pickfor) {
    case 50: xf_origin(w); xf_close(); geo_end(); return;
    case 10:                                           /* FROM (first use: then TO) */
        tp->from = w;
        if (!tp->ft_set) { tp->ft_set = 1; xf_pickfor = 12; xf_msg(); return; }
        break;
    case 12: tp->to = w; tp->ft_set = 1; break;
    case 11: {                                         /* a line: the picked end is FROM (pick parameter >= 0.5: end 2) */
        if (e < 0 || e >= F.nents || F.ents[e].type != E_LINE) return;
        const Ent *en = &F.ents[e]; double t = 0;
        { double dx = en->b.x - en->a.x, dy = en->b.y - en->a.y, L2 = dx * dx + dy * dy; if (L2 > 0) t = ((w.x - en->a.x) * dx + (w.y - en->a.y) * dy) / L2; }
        if (t >= 0.5) { tp->from = en->b; tp->to = en->a; } else { tp->from = en->a; tp->to = en->b; }
        tp->ft_set = 1; break; }
    case 20: xr.ctr = w; break;
    case 30: case 31: case 32:                          /* X axis / Y axis / Polar through the point */
        xmi.ref = w; xmi.axis = xf_pickfor - 30; break;
    case 33:
        if (e < 0 || e >= F.nents || F.ents[e].type != E_LINE) return;   /* FUN_180016c40: a line, not zero length in the view */
        if (hypot(F.ents[e].b.x - F.ents[e].a.x, F.ents[e].b.y - F.ents[e].a.y) < XF_TOL) return;
        xmi.p1 = F.ents[e].a; xmi.p2 = F.ents[e].b; xmi.have_line = 1; xmi.axis = 3; break;
    case 34:
        if (!xf_pickstep) { xmi.p1 = w; xf_pickstep = 1; xf_msg(); return; }
        xmi.p2 = w;
        if (hypot(xmi.p2.x - xmi.p1.x, xmi.p2.y - xmi.p1.y) < XF_TOL) { xmi.ref = xmi.p1; xmi.axis = 0; }   /* FUN_180016ac0: X axis */
        else { xmi.have_pts = 1; xmi.axis = 4; }
        break;
    case 60: xs.org = w; xs_auto = 0; break;           /* FUN_180037950: the point, Auto center off */
    case 70: {                                         /* FUN_180012b80: a line (its ends, reversed when picked past the middle) or two points */
        V3 a, b;
        if (!xf_pickstep && e >= 0 && e < F.nents && F.ents[e].type == E_LINE) {
            const Ent *en = &F.ents[e]; double t = 0, ddx = en->b.x - en->a.x, ddy = en->b.y - en->a.y, L2 = ddx * ddx + ddy * ddy;
            if (L2 > 0) t = ((w.x - en->a.x) * ddx + (w.y - en->a.y) * ddy) / L2;
            if (t >= 0.5) { a = en->b; b = en->a; } else { a = en->a; b = en->b; }
        } else if (!xf_pickstep) { xd.p1 = w; xf_pickstep = 1; xf_msg(); return; }
        else { a = xd.p1; b = w; }
        a.z = b.z = constr_depth;                          /* to the construction depth (Top: world Z) */
        if (hypot(b.x - a.x, b.y - a.y) <= XF_TOL) { xf_pickstep = 0; return; }   /* kept only when longer than tol */
        xd.p1 = a; xd.p2 = b; int first = xd.novec; xd.novec = 0;
        if (first) { xf_pickfor = 0; xf_dialog(); return; }
        break; }
    case 71: xd.poff = w; xd.poff_set = 1; break;      /* FUN_180010b40 */
    case 90: case 91: case 92: case 93: xsc_take(e, w); return;
    case 80: {                                         /* XfWfMgrSelectCopy: the clicked entity's ent +0x50 in 1..XfInfoSize -> its entry; FUN_1800335f0:
                                                          XfInfoRemovedAdd(entry +0x70 / +0x72), XfDeletePreview, the preview again (while XfPreview or
                                                          XfTempPreview 1), and the loop asks again */
        int k = e >= 0 && e < xf_npart && F.ents[e].type && (xf_ent_mark(e) & 8) ? xf_part[e] : 0;
        if (k < 1 || k > nxi) return;
        if (xf_rm_identity) { snprintf(cam_msg, sizeof cam_msg, "Remove Item: the copies of a zero angle (XfInfoFillWithI33) - their keys are NOT DECODED"); return; }
        xf_rm_add(&xi[k - 1]);
        if (cfg_xf_preview) xf_make(); else xf_drop();
        xf_msg(); return; }
    case 40: x3.pfrom = w; break;
    case 41: x3.pto = w; break;
    default: return;
    }
    if (xf_pickfor == 10 || xf_pickfor == 12 || xf_pickfor == 11) {
        if (hypot(tp->to.x - tp->from.x, tp->to.y - tp->from.y) > XF_TOL || fabs(tp->to.z - tp->from.z) > XF_TOL) { tp->fromto = 1; tp->dir = 0; trans_from_to(tp); }
    }
    xf_back();
}

/* ---------------------------------------------------------------- the dialog as a function panel */
/* toggle codes (ours: the dialog buttons that start a pick, posted through geo_panel_button) */
/* Remove Item 725 / Reset Items 726 enabled (Rotate FUN_1800315e0 rot_mirror_vt.c l.900-908: copies >= 2 and XfPreviewValid / XfRemovedYes;
   Rectangular Array FUN_18003b890 trans_dlg_a.c l.866-893: #1 or #2 >= 2 and the same) */
static int xf_items_enabled(int reset) {
    int many = xf_c == G_XROT ? xr.copies >= 2 : xf_c == G_XARRAY ? (xa.n1 >= 2 || xa.n2 >= 2) : 0;
    if (!many || xf_phase != XP_DLG) return 0;
    return reset ? xf_nrm > 0 : xf_prev_valid();
}
#define E(x) ops->exp(u, x)
#define END ops->end(u)
static void p_mode(const CuiPanelOps *ops, void *u, int *mode, int join_en, int has_join) {
    /* Move / Copy / Join radios 100 / 101 / 102 (statics 1303 / 1318 / 1325; tooltips "Delete original geometry", "Keep
       original geometry", "Keep original geometry and Join") */
    ops->radio(u, "Move", mode, 0, 1); ops->radio(u, "Copy", mode, 1, 1); if (has_join) ops->radio(u, "Join", mode, 2, join_en);
}
static void p_dir(const CuiPanelOps *ops, void *u, const char *hdr, int *dir, int en) {
    /* the Direction MCButton (1623, bitmaps 0x463-0x465 NOT DECODED): states 0 / 1 / 2 as radios (ours: the names) */
    if (E(hdr)) { ops->radio(u, "One way", dir, 0, en); ops->radio(u, "Opposite", dir, 1, en); ops->radio(u, "Both", dir, 2, en); END; }
}
static void p_preview(const CuiPanelOps *ops, void *u) {
    if (E("Preview")) { ops->check(u, "Preview", &cfg_xf_preview, 1);      /* 1774 (XfPreview) */
        ops->toggle(u, "Regen", 1, cfg_xf_preview ? -1 : XB_REGEN);         /* 2973: enabled while Preview is off */
        ops->toggle(u, "Fit", 1, -1); END; }                                /* 1778 XfAutoFit: NOT PORTED */
}
static void p_attr(const CuiPanelOps *ops, void *u, int *ua, int *incr, int *lv, int *co, int incr_en) {
    if (E("Attributes")) {
        ops->check(u, "Use New Attributes", ua, 1);                         /* 1665 "Assign results new level and color" */
        /* the 2nd template (FUN_180044bd0: + UseAttrs) adds these; here always listed, enabled with the check box (ours:
           the panel keeps one layout) */
        ops->inum(u, "Level", "lv", lv, NULL, *ua, 1, 0x7fffffff);           /* 1685 (the Level dialog 1683: greyed) */
        if (incr) ops->check(u, "Increment level per copy", incr, *ua && incr_en);   /* 1688 */
        ops->inum(u, "Color", "co", co, NULL, *ua, 0, 255);                  /* 1717 (the colour dialog 1714: greyed) */
        END; }
}
static const char **plane_items(int *n) {
    static const char *it[7 + UPLANE_MAX]; int k = 0;
    for (int id = 1; id <= 7; id++) it[k++] = plane_name(id);
    for (int i = 0; i < nuplanes && k < 7 + UPLANE_MAX; i++) it[k++] = uplanes[i].name;
    *n = k; return it;
}
static int plane_ix(int id) { if (id <= 7) return id - 1; for (int i = 0; i < nuplanes; i++) if (uplanes[i].id == id) return 7 + i; return 0; }
static int plane_id_of(int ix) { return ix < 7 ? ix + 1 : ix - 7 < nuplanes ? uplanes[ix - 7].id : 1; }
static int x3_six, x3_dix;
void xf_panel_body(const CuiPanelOps *ops, void *u) {
    switch (xf_c) {
    case G_XTRANS: case G_XSTRETCH: {
        XfTrans *p = xf_c == G_XTRANS ? &xt : &xst; int en = p->copies > 1;
        if (E(xf_c == G_XTRANS ? "Translate" : "Stretch")) {
            ops->toggle(u, "Add/Remove Geometry", 1, xf_c == G_XTRANS ? XB_RESEL : -1);   /* 1250 */
            p_mode(ops, u, &p->mode, xf_c == G_XTRANS, 1);                   /* Stretch: Join disabled (FUN_18003b990) */
            ops->inum(u, "#", "copies", &p->copies, NULL, 1, 1, 30000);      /* 1375 */
            ops->radio(u, "Distance between", &p->bytotal, 0, en); ops->radio(u, "Total distance", &p->bytotal, 1, en);   /* 1396 / 1399 */
            END; }
        if (E("Delta")) { ops->num(u, "Delta X", "dx", &p->dx, NULL, 1, -1e9, 1e9); ops->num(u, "Delta Y", "dy", &p->dy, NULL, 1, -1e9, 1e9);
            ops->num(u, "Delta Z", "dz", &p->dz, NULL, 1, -1e9, 1e9); END; }   /* 2485 / 2488 / 2492 */
        if (E("From / To")) { ops->toggle(u, "Select FROM point", 1, xf_c == G_XTRANS ? XB_FROM : XB_FROM);
            ops->toggle(u, "Select line", 1, XB_LINE); ops->toggle(u, "Select TO point", 1, XB_TO); END; }   /* 1469 / 1472 / 1476 */
        if (E("Polar")) { ops->num(u, "Angle", "pang", &p->pang, NULL, 1, -360, 360); ops->num(u, "Length", "plen", &p->plen, NULL, 1, 0, 1e9); END; }   /* 1540 / 1581 */
        if (xf_c == G_XTRANS) p_dir(ops, u, "Direction", &p->dir, 1);       /* 1623 */
        p_preview(ops, u); p_attr(ops, u, &p->useattr, &p->incr, &p->level, &p->color, en);
        break; }
    case G_XROT: {
        XfRot *p = &xr; int en = p->copies > 1;
        if (E("Rotate")) {
            ops->toggle(u, "Add/Remove Geometry", 1, XB_RESEL);
            p_mode(ops, u, &p->mode, !p->translate, 1);                      /* Join only with Rotate (FUN_1800315e0) */
            ops->inum(u, "#", "copies", &p->copies, NULL, 1, 1, 30000);
            ops->radio(u, "Angle between", &p->bytotal, 0, en); ops->radio(u, "Total sweep", &p->bytotal, 1, en);
            ops->toggle(u, "Define center (point) of rotation", 1, XB_RCTR);  /* 716 */
            ops->num(u, "Center X", "cx", &p->ctr.x, NULL, 0, -1e9, 1e9); ops->num(u, "Center Y", "cy", &p->ctr.y, NULL, 0, -1e9, 1e9);
            ops->num(u, "Angle", "ang", &p->ang, NULL, 1, -360, 360);         /* 1540 (spinner +-360) */
            ops->radio(u, "Rotate", &p->translate, 0, 1); ops->radio(u, "Translate", &p->translate, 1, 1);   /* 711 / 712 */
            END; }
        p_dir(ops, u, "Direction", &p->dir, 1);
        if (E("Items")) { ops->toggle(u, "Remove Item", 1, xf_items_enabled(0) ? XB_RMITEM : -1); ops->toggle(u, "Reset Items", 1, xf_items_enabled(1) ? XB_RSITEMS : -1); END; }   /* 725 / 726 */
        p_preview(ops, u); p_attr(ops, u, &p->useattr, &p->incr, &p->level, &p->color, en);
        break; }
    case G_XMIRROR: {
        XfMir *p = &xmi;
        if (E("Mirror")) { ops->toggle(u, "Add/Remove Geometry", 1, XB_RESEL); p_mode(ops, u, &p->mode, 1, 1); END; }
        if (E("Axis")) {                                                     /* 1428 / 1433 / 1437 / 1442 / 1445 -> +0x60ec */
            ops->radio(u, "Mirror about X axis", &p->axis, 0, 1); ops->toggle(u, "X axis: Select point", 1, XB_MX); ops->num(u, "Y", "my", &p->ref.y, NULL, p->axis == 0, -1e9, 1e9);
            ops->radio(u, "Mirror about Y axis", &p->axis, 1, 1); ops->toggle(u, "Y axis: Select point", 1, XB_MY); ops->num(u, "X", "mx", &p->ref.x, NULL, p->axis == 1, -1e9, 1e9);
            ops->radio(u, "Mirror about Polar axis", &p->axis, 2, 1); ops->toggle(u, "Polar: Select point", 1, XB_MP); ops->num(u, "A", "ma", &p->ang, NULL, p->axis == 2, -360, 360);
            ops->radio(u, "Mirror about Line", &p->axis, 3, p->have_line); ops->toggle(u, "Select line", 1, XB_MLINE);
            ops->radio(u, "Mirror about Two points", &p->axis, 4, p->have_pts); ops->toggle(u, "Select two points", 1, XB_M2P);
            END; }
        { int no = 0; ops->check(u, "Mirror note and label text", &no, 0); }   /* 3018: notes / labels are not in this program */
        p_preview(ops, u); p_attr(ops, u, &p->useattr, NULL, &p->level, &p->color, 0);
        break; }
    case G_XARRAY: {
        XfArr *p = &xa; int en = p->n1 > 1 || p->n2 > 1;
        if (E("Rectangular Array")) { ops->toggle(u, "Add/Remove Geometry", 1, XB_RESEL); END; }
        if (E("Direction 1")) { ops->inum(u, "#", "n1", &p->n1, NULL, 1, 1, 30000); ops->num(u, "Length", "l1", &p->l1, NULL, 1, 0, 1e9);
            ops->num(u, "Angle", "a1", &p->a1, NULL, 1, -359.99994, 359.99994); END; }
        p_dir(ops, u, "Direction 1 ", &p->d1, p->n1 > 1);                    /* 1623: enabled when N1 > 1 */
        if (E("Direction 2")) { ops->inum(u, "#", "n2", &p->n2, NULL, 1, 1, 30000); ops->num(u, "Length", "l2", &p->l2, NULL, 1, 0, 1e9);
            ops->num(u, "Angle", "a2", &p->a2, NULL, 1, 5.7296e-5, 179.99994); END; }
        p_dir(ops, u, "Direction 2 ", &p->d2, p->n2 > 1);                    /* 724 */
        if (E("Items")) { ops->toggle(u, "Remove Item", 1, xf_items_enabled(0) ? XB_RMITEM : -1); ops->toggle(u, "Reset Items", 1, xf_items_enabled(1) ? XB_RSITEMS : -1); END; }   /* 725 / 726 */
        p_preview(ops, u); p_attr(ops, u, &p->useattr, &p->incr, &p->level, &p->color, en);
        break; }
    case G_XTRANS3D: {
        XfT3 *p = &x3; int n; const char **it = plane_items(&n);
        if (E("Translate 3D")) { ops->toggle(u, "Add/Remove Geometry", 1, XB_RESEL); p_mode(ops, u, &p->mode, 0, 0);
            ops->inum(u, "#", "copies", &p->copies, NULL, 1, 1, 30000); END; }
        if (E("Planes")) {
            x3_six = plane_ix(p->vfrom); x3_dix = plane_ix(p->vto);
            ops->label(u, "Source:"); ops->combo(u, it, n, &x3_six, -1); ops->toggle(u, "Select source plane", 1, -1);   /* 10101; 1469: the plane dialog NOT PORTED */
            ops->toggle(u, "Define center (point) of rotation", 1, XB_SORG);   /* 716 */
            ops->label(u, "Destination:"); ops->combo(u, it, n, &x3_dix, -1); ops->toggle(u, "Select destination plane", 1, -1);
            ops->toggle(u, "Define center (point) of rotation", 1, XB_DORG);   /* 717 */
            p->vfrom = plane_id_of(x3_six); p->vto = plane_id_of(x3_dix);
            END; }
        if (E("Points")) { ops->toggle(u, "Select FROM by points", 1, -1); ops->toggle(u, "Select FROM/TO by points", 1, -1);
            ops->toggle(u, "Select TO by points", 1, -1); END; }              /* 1500 / 1472 / 1512: NOT PORTED */
        p_preview(ops, u); p_attr(ops, u, &p->useattr, &p->incr, &p->level, &p->color, 1);
        break; }
    case G_XDIST: {                                      /* CXfFitDlg (templates 25527 / 25528; DDX FUN_18000dec0) */
        XfDist *p = &xd;
        if (E("Distribute")) {
            ops->toggle(u, "Add/Remove Geometry", 1, XB_RESEL);                  /* 1250 */
            ops->radio(u, "Move", &p->mode, 0, 1); ops->radio(u, "Copy", &p->mode, 1, 1);   /* 100 / 101 */
            ops->toggle(u, "Select fit vector", 1, XB_DVEC);                     /* 1472 (FUN_18000f7f0) */
            END; }
        if (E("Instances")) {
            ops->radio(u, "Number of instances equally spaced", &p->equal, 1, 1); ops->radio(u, "Specific instance spacing", &p->equal, 0, 1);   /* 10324 / 10325 */
            ops->inum(u, "#", "n", &p->n, NULL, 1, 1, 30000);                     /* 1375 */
            ops->num(u, "Spacing", "s", &p->s, NULL, !p->equal, 0, 30000);         /* 1581 */
            END; }
        if (E("Margins")) {
            ops->num(u, "Start margin", "ms", &p->ms, NULL, 1, 0, 30000); ops->num(u, "End margin", "me", &p->me, NULL, 1, 0, 30000);   /* 10314 / 10315 */
            ops->check(u, "Lock margins", &p->mlock, 1);                          /* 10317: the other margin follows (xf_poll) */
            END; }
        if (E("Justification")) {
            ops->radio(u, "1st endpoint", &p->just, 0, 1); ops->radio(u, "Center", &p->just, 1, 1); ops->radio(u, "2nd endpoint", &p->just, 2, 1);   /* trackbar 10318 (tooltip 398) */
            ops->check(u, "Center instances on fit vector endpoints", &p->center, 1);   /* 285: Contain / Center */
            END; }
        if (E("Rotation / Alignment")) {
            static const char *R[] = { "Original", "Align Box", "Align geometry" };   /* mccadutilres 399 / 400 / 401 */
            int r0 = p->rot; ops->combo(u, R, 3, &p->rot, -1);
            if (p->rot != r0) p->edge = p->rot == 2 ? 1 : -1;                    /* FUN_180010d10 */
            ops->toggle(u, "Select an edge of the result bounding box", 1, -1);   /* 1251: the box lines are not drawn here (NOT PORTED) */
            END; }
        if (E("Offset")) {
            ops->check(u, "Adjust instance offset", &p->adjust, 1);             /* 10319 */
            ops->num(u, "Offset", "off", &p->off, NULL, p->adjust, -30000, 30000);   /* 1582 */
            ops->toggle(u, "Select point", 1, p->adjust ? XB_DOFF : -1);         /* 716 */
            END; }
        p_preview(ops, u); p_attr(ops, u, &p->useattr, &p->incr, &p->level, &p->color, p->n > 1);
        break; }
    case G_XDYN: {                                       /* xformdynamicmanagerview.baml (tools/baml_panel.py): Basic / Advanced */
        if (!ops->tab || ops->tab(u, 0, "Basic")) {
            if (E("Entity")) {
                ops->label(u, "Mode:"); ops->radio(u, "Move", &xdp.copy, 0, 1); ops->radio(u, "Copy", &xdp.copy, 1, 1);   /* IsCopy */
                ops->label(u, "Type:"); ops->radio(u, "Single", &xdp.single, 1, 1);                                  /* IsSingle */
                ops->inum(u, "Instances:", "n", &xdp.count, NULL, xdp.single, 1, 30000);   /* CopyCount, enabled = IsSingle (max: ours) */
                ops->radio(u, "Multiple", &xdp.single, 0, 1);
                ops->check(u, "Only apply last transform", &xdp.last, 1);
                END; }
            if (E("Selection")) { ops->toggle(u, "Reselect", 1, XB_RESEL); END; }   /* ReselectAllCommand (code 6) */
            if (E("Origin")) { ops->toggle(u, "Cplane", 1, 0x1a); ops->toggle(u, "World", 1, 0x1b); ops->toggle(u, "Tplane", 1, 0x1c); ops->toggle(u, "WCS", 1, 0x1d); END; }
            if (E("Axes")) { ops->toggle(u, "Cplane", 1, 0x1e); ops->toggle(u, "World", 1, 0x1f); ops->toggle(u, "Tplane", 1, 0x20); ops->toggle(u, "WCS", 1, 0x21); END; }
        }
        if (!ops->tab || ops->tab(u, 1, "Advanced")) {
            if (E("Gnomon Mode")) { ops->check(u, "Set to geometry when placed", &xdp.setgeom, 1); END; }
            /* ours: the gnomon's GrButton (path 7) - its bitmaps / place are NOT DECODED */
            if (E("Gnomon button")) { ops->toggle(u, "Gnomon button", 1, XB_DBTN); END; }
            /* ours: the Gnomon settings dialog (mccoreres 25610, SelGnomonBase::RunSettingsDlg) as a group; Ctrl-right-click NOT DECODED */
            if (E("Gnomon settings")) {
                ops->label(u, "Axis alignment:");
                ops->check(u, "Line (along)", &sm[0], 1); ops->check(u, "Arc (tangent/normal)", &sm[1], 1);       /* 10519 / 10520 */
                ops->check(u, "Spline (tangent)", &sm[2], 1); ops->check(u, "Solid edge (along)", &sm[3], 1);     /* 10521 / 10522 */
                ops->label(u, "Face alignment:");
                ops->check(u, "Solid face (normal)", &sm[4], 1); ops->check(u, "Surface (normal)", &sm[5], 1);    /* 10523 / 10524 */
                ops->check(u, "Activate angular snap", &gn_ang_on, 1);                                          /* 10526 */
                ops->num(u, "Angular increment (deg)", "ainc", &gn_ang_inc, NULL, 1, 1, 90);                    /* 10525 */
                ops->check(u, "Activate ruler", &gn_ruler_on, 1);                                               /* 10518 */
                ops->inum(u, "Ruler size (pixels)", "rsize", &gn_ruler_size, NULL, 1, 20, 70);                  /* 10528 */
                ops->inum(u, "Gnomon size (pixels)", "gsize", &gn_size, NULL, 1, 50, 250);                      /* 10527 */
                END; }
        }
        break; }
    case G_XSCALE: {
        XfScl *p = &xs; int en = p->copies > 1;
        if (xsc.on) {                                    /* CXfScaleCompDlg (template 6452): Base / Result combos with Select, the three edits */
            static const char *M[] = { "Line", "Points", "Diameter", "Radius" };   /* OnInitDialog: mccoreres 12135 / 523 / 5127 / 6025 */
            if (E("Scale Calculator")) {
                ops->label(u, "Base"); ops->combo(u, M, 4, &xsc.cb, -1); ops->toggle(u, "Select", 1, XB_CSELB); ops->num(u, "Base", "cbase", &xsc.base, NULL, 1, XF_TOL, 1e38);
                ops->label(u, "Result"); ops->combo(u, M, 4, &xsc.cr, -1); ops->toggle(u, "Select", 1, XB_CSELR); ops->num(u, "Result", "cres", &xsc.res, NULL, 1, XF_TOL, 1e38);
                ops->num(u, "Scale factor", "cfac", &xsc.fac, NULL, 1, -1e38, 1e38);
                ops->toggle(u, "OK", 1, XB_COK); ops->toggle(u, "Cancel", 1, XB_CCANCEL);
                END; }
            break;
        }
        if (E("Scale")) {
            ops->toggle(u, "Add/Remove Geometry", 1, XB_RESEL); p_mode(ops, u, &p->mode, 1, 1);
            ops->inum(u, "#", "copies", &p->copies, NULL, 1, 1, 30000);
            ops->toggle(u, "Define scaling reference point", 1, XB_SREF); ops->check(u, "Auto center", &xs_auto, 1);   /* 716 / 3025 */
            ops->radio(u, "Uniform", &p->xyz, 0, 1); ops->radio(u, "XYZ", &p->xyz, 1, 1);   /* 718 / 720 */
            END; }
        /* the Uniform template (6445) / the XYZ one (6449) as two groups, the other one greyed (ours: one layout) */
        double lo = XF_TOL;                                                  /* the combos' minimum = tol (CXfScaleDlg ctor) */
        if (E("Uniform")) {
            ops->radio(u, "Factor", &p->pct, 0, !p->xyz); ops->radio(u, "Percentage", &p->pct, 1, !p->xyz);   /* 1301 / 3024 */
            ops->num(u, "Scale", "s", &p->s, NULL, !p->xyz, lo, 1e9); ops->toggle(u, "Calculator...", 1, !p->xyz ? XB_CALC : -1);   /* 723 / 3026 */
            END; }
        if (E("XYZ")) {
            ops->radio(u, "Factors", &p->pct, 0, p->xyz); ops->radio(u, "Percentages", &p->pct, 1, p->xyz);
            ops->num(u, "Scale X", "sx", &p->sx, NULL, p->xyz, lo, 1e9); ops->num(u, "Scale Y", "sy", &p->sy, NULL, p->xyz, lo, 1e9);
            ops->num(u, "Scale Z", "sz", &p->sz, NULL, p->xyz, lo, 1e9);   /* 2485 / 2488 / 2492 */
            ops->toggle(u, "Calculator X...", 1, p->xyz ? XB_CALC + 1 : -1); ops->toggle(u, "Calculator Y...", 1, p->xyz ? XB_CALC + 2 : -1);
            ops->toggle(u, "Calculator Z...", 1, p->xyz ? XB_CALC + 3 : -1);   /* 3027-3029 (ours: the labels) */
            END; }
        p_preview(ops, u); p_attr(ops, u, &p->useattr, &p->incr, &p->level, &p->color, en);
        break; }
    }
}
#undef E
#undef END

/* ---------------------------------------------------------------- panel changes (the dialogs' edit handlers, then 0x66) */
static XfTrans s_xt, s_xst; static XfRot s_xr; static XfMir s_xmi; static XfArr s_xa; static XfT3 s_x3; static XfScl s_xs; static int s_prev, s_auto;
static XfDist s_xd; static XfDyn s_xdp; static XsCalc s_xsc; static int s_gn[6]; static double s_gninc;
static void xf_snap(void) { s_xdp = xdp; s_gn[0] = gn_size; s_gn[1] = gn_ang_on; s_gn[2] = gn_ruler_on; s_gn[3] = gn_ruler_size; s_gn[4] = gn_selmask; s_gninc = gn_ang_inc; s_xt = xt; s_xst = xst; s_xr = xr; s_xmi = xmi; s_xa = xa; s_x3 = x3; s_xs = xs; s_xsc = xsc; s_prev = cfg_xf_preview; s_auto = xs_auto; s_xd = xd; }
/* Translate / Stretch: a Delta edit leaves From/To mode and refreshes the polar values (FUN_180044020(dlg, 0),
   FUN_1800457d0); a polar edit sets X / Y from length and angle, Z = 0 (FUN_180045540); 2D construction: a Z alone
   zeroes X / Y, an X / Y zeroes Z (FUN_180041a10) */
static void trans_edit(XfTrans *p, const XfTrans *o) {
    if (p->pang != o->pang || p->plen != o->plen) {
        if (p->plen < XF_TOL) p->dx = p->dy = p->dz = 0;
        else { p->dx = p->plen * cos(p->pang * D2R); p->dy = p->plen * sin(p->pang * D2R); p->dz = 0; }
        p->fromto = 0; return;
    }
    if (p->dx != o->dx || p->dy != o->dy || p->dz != o->dz) {
        if (!cfg_constr_3d) { if (p->dz != o->dz && fabs(p->dz) > XF_TOL) p->dx = p->dy = 0; else if (p->dx != o->dx || p->dy != o->dy) p->dz = 0; }
        p->fromto = 0;
        double L = hypot(p->dx, p->dy); if (L > XF_TOL) { p->plen = L; p->pang = atan2(p->dy, p->dx) / D2R; if (p->pang < 0) p->pang += 360; }
    }
}
/* Dynamic's observers (FUN_1800f1de0..f1f90 -> FUN_1800eac50): Move -> IsSingle := true, Multiple -> IsCopy := true (codes 10-13 keep
   IsSingle / IsCopy in +0x391 / +0x390), then the preview; the gnomon settings (the dialog's OK: SetSize / SetRotSnap ...) redraw */
static void xdy_poll(void) {
    { int m = 0; for (int i = 0; i < 6; i++) m |= (sm[i] != 0) << i; gn_selmask = m; }
    if (xdp.copy != s_xdp.copy) { if (!xdp.copy) xdp.single = 1; xdy_s391 = xdp.single; xdy_s390 = xdp.copy; }
    if (xdp.single != s_xdp.single) { if (!xdp.single) xdp.copy = 1; xdy_s391 = xdp.single; xdy_s390 = xdp.copy; }
    if (xdp.count < 1) xdp.count = 1;                    /* the Instances box: min 1 [inferred, FUN_1800b5f50(1, 1)] */
    if (gn_size < 50) gn_size = 50; if (gn_size > 250) gn_size = 250;   /* the settings dialog's limits */
    if (gn_ang_inc < 1) gn_ang_inc = 1; if (gn_ang_inc > 90) gn_ang_inc = 90;
    if (gn_ruler_size < 20) gn_ruler_size = 20; if (gn_ruler_size > 70) gn_ruler_size = 70;
    int ch = memcmp(&xdp, &s_xdp, sizeof xdp) != 0;
    int gch = s_gn[0] != gn_size || s_gn[1] != gn_ang_on || s_gn[2] != gn_ruler_on || s_gn[3] != gn_ruler_size || s_gn[4] != gn_selmask || s_gninc != gn_ang_inc;
    xf_snap();
    if (ch && !xdy_drag) xdy_preview();
    if (ch || gch) overlay_dirty = 1;
}
void xf_poll(void) {
    if (xf_c == G_XDYN && xf_phase == XP_DYN) { xdy_poll(); return; }
    if (!xf_c || xf_phase != XP_DLG) { xf_snap(); return; }
    if (xsc.on) {                                       /* the modal calculator: CBN_SELCHANGE (FUN_1800390b0 / 180039060: a new index measures at
                                                           once), EN_KILLFOCUS of the edits (FUN_180038ff0 / 180038f20: a change >= tol -> factor =
                                                           result / base; 1007 FUN_180038f90 only takes the value) */
        if (xsc.cb != s_xsc.cb) { xf_snap(); xsc_measure(0); return; }
        if (xsc.cr != s_xsc.cr) { xf_snap(); xsc_measure(1); return; }
        if (fabs(xsc.base - s_xsc.base) >= XF_TOL || fabs(xsc.res - s_xsc.res) >= XF_TOL) xsc.fac = xsc.res / xsc.base;
        xf_snap(); return;
    }
    int changed = 0;
#define CH(a, b) (memcmp(&a, &b, sizeof a) != 0)
    if (CH(xt, s_xt)) { trans_edit(&xt, &s_xt); changed = 1; }
    if (CH(xst, s_xst)) { trans_edit(&xst, &s_xst); changed = 1; }
    if (CH(xr, s_xr)) { if (xr.translate && xr.mode == 2) xr.mode = 1; changed = 1; }   /* Translate with Join -> Copy (FUN_180033a20) */
    if (CH(xmi, s_xmi)) {                                /* a value edit switches the mode (FUN_180015210 / FUN_1800150c0 / FUN_180014f70) */
        if (xmi.ref.y != s_xmi.ref.y) xmi.axis = 0; else if (xmi.ref.x != s_xmi.ref.x) xmi.axis = 1; else if (xmi.ang != s_xmi.ang) xmi.axis = 2;
        if (fabs(fabs(xmi.ang) - 360) < 1e-12) xmi.ang = 0;   /* 360 -> 0 */
        changed = 1; }
    if (CH(xa, s_xa) || CH(x3, s_x3)) changed = 1;
    if (CH(xs, s_xs) || xs_auto != s_auto) {
        if (xs.pct != s_xs.pct) { double k = xs.pct ? 100 : 0.01; xs.s *= k; xs.sx *= k; xs.sy *= k; xs.sz *= k; }   /* FUN_180037290 / FUN_180036530 */
        changed = 1; }
    if (CH(xd, s_xd)) {                                  /* Distribute: the margin lock (FUN_180010200 / 1800102b0; checking it copies start -> end, FUN_18000f960) */
        if (xd.mlock && (xd.mlock != s_xd.mlock || xd.ms != s_xd.ms)) xd.me = xd.ms; else if (xd.mlock && xd.me != s_xd.me) xd.ms = xd.me;
        if (xd.off != s_xd.off) xd.poff_set = 0;           /* an edit of 1582 clears the picked point (FUN_180010360) */
        changed = 1; }
    if (cfg_xf_preview != s_prev) changed = 1;
#undef CH
    if (changed) { xf_mod = 1; xf_snap(); xf_rebuild(); s_xd = xd; }   /* FUN_18000bd80(0x66) sets +0x5090; (the math writes the spacing / offset back) */
}

/* the panel codes: 0 OK, 1 Apply, 2 Cancel; >= 10 the dialog's buttons */
int xf_button(int code) {
    if (!xf_c) return 0;
    if (xf_c == G_XDYN && xf_phase == XP_DYN) return xdy_button(code);   /* DynamicXform's codes (above) */
    if (xf_phase == XP_SEL) { if (code == 2) { xf_abort(); geo_end(); } return 1; }   /* select_ents: Esc ends the command */
    if (xf_phase == XP_PICK && xf_pickfor >= 90 && xf_pickfor <= 93) { if (code == 2) xsc_back(); return 1; }   /* get_event fails: the value is kept */
    if (xf_phase == XP_PICK && xf_pickfor == 80) {       /* Remove Item's XfWfMgrSelectCopy loop: Enter (get_event key 0xd -> 3) ends it,
                                                           Esc (2) posts 100 = the dialog's Cancel (FUN_1800335f0 / trans_dlg_b.c l.1258) */
        if (code == 2) { xf_drop(); xf_rm_free(); xf_close(); geo_end(); }
        else if (code == XB_RMEND) { xf_phase = XP_DLG; xf_pickfor = 0; xf_snap(); xf_msg(); }
        return 1; }
    if (xf_phase == XP_PICK) { if (code == 2) { if (xf_c == G_XORIGIN || (xf_c == G_XDIST && xd.novec)) { xf_close(); geo_end(); } else xf_back(); } return 1; }   /* Distribute: FUN_180012b80 failing ends it */   /* Esc: the pick ends (Move to Origin: point_proc's Esc ends the command) */
    if (code >= 10) xf_mod = 1;                           /* a button: BN_CLICKED -> FUN_18000b900 sets +0x5090 */
    switch (code) {
    case 0:                                              /* OK (0x65): Cancel (100) while +0x5090 is clear */
        if (!xf_mod) { xf_drop(); xf_rm_free(); xf_close(); geo_end(); return 1; }
        xf_commit(); xf_rm_free(); xf_close(); geo_end(); return 1;
    case 1:                                              /* Apply 12321 = FUN_18000b7d0 (gaps/apply_bytes.txt: mov byte [rcx+0x5091], 1; jmp FUN_18000b230 =
                                                           OK): the commit, then the outer loop's next round (select_ents again, the parameters kept,
                                                           XfInfoRemovedFree at the round's end MX l.1034); enabled only with +0x5090 */
        if (!xf_apply_enabled()) return 1;
        xf_commit(); xf_rm_free(); xf_round2 = 1; xf_begin_selection(); return 1;
    case 2: xf_drop(); xf_rm_free(); xf_close(); geo_end(); return 1;                         /* Cancel (100) */
    case XB_REGEN: xf_make(); return 1;
    case XB_RESEL: xf_drop(); xf_rm_free(); xf_round2 = 1; xf_begin_selection(); return 1;   /* 1250 (FUN_180032ed0): select_ents again, XfInfoRemovedFree,
                                                                                    FUN_18000bd80(0x66) */
    case XB_RMITEM:                                      /* 725 (FUN_1800335f0 / array trans_dlg_b.c l.1240): XfWfMgrSelectCopy(mccoreres 1534) */
        if (!xf_items_enabled(0)) return 1;
        xf_pick(80); return 1;
    case XB_CALC: case XB_CALC + 1: case XB_CALC + 2: case XB_CALC + 3:   /* the Calculator buttons (FUN_180035fb0 / 1800360f0 / 180036230 / 180036370) */
        if (xf_c != G_XSCALE || (code == XB_CALC) == (xs.xyz != 0)) return 1;   /* the Uniform one / the XYZ ones (their template only) */
        memset(&xsc, 0, sizeof xsc); xsc.on = 1; geo_panel_layout++; xsc.target = code - XB_CALC + 1;
        xsc.base = xsc.res = xsc.fac = 1.0;              /* the ctor FUN_1800387c0: +0x4310 / +0x5108 / +0x5f00 = 1.0; OnInitDialog: both combos 0 */
        xf_snap(); return 1;
    case XB_CSELB: case XB_CSELR: if (xsc.on) xsc_measure(code == XB_CSELR); return 1;   /* 1003 / 1004 Select */
    case XB_CCANCEL: memset(&xsc, 0, sizeof xsc); geo_panel_layout++; xf_snap(); return 1;
    case XB_COK: {                                       /* DoModal == IDOK: the factor (x 100 with Percentage(s), +0x7e64 == 1) into the field, kept
                                                           within its combo's bounds, then FUN_18000bd80(0x66) */
        if (!xsc.on) return 1;
        double v = xsc.fac * (xs.pct ? 100.0 : 1.0), *t = xsc.target == 1 ? &xs.s : xsc.target == 2 ? &xs.sx : xsc.target == 3 ? &xs.sy : &xs.sz;
        if (v < XF_TOL) v = XF_TOL; if (v > 1e9) v = 1e9;  /* the panel's bounds (lo = tol as the CXfScaleDlg ctor; hi: ours, as the panel field) */
        *t = v; memset(&xsc, 0, sizeof xsc); geo_panel_layout++; xf_snap(); xf_rebuild(); return 1; }
    case XB_RSITEMS:                                     /* 726 (FUN_180033840): XfInfoRemovedFree + FUN_18000bd80(0x66) */
        if (!xf_items_enabled(1)) return 1;
        xf_rm_free(); xf_rebuild(); return 1;
    case XB_FROM: xf_pick(10); return 1;
    case XB_TO: xf_pick((xf_c == G_XSTRETCH ? xst.ft_set : xt.ft_set) ? 12 : 10); return 1;   /* never set: the FROM flow (FUN_1800435b0) */
    case XB_LINE: xf_pick(11); return 1;
    case XB_RCTR: xf_pick(20); return 1;
    case XB_MX: case XB_MY: case XB_MP: xf_pick(code); return 1;
    case XB_MLINE: xf_pick(33); return 1;
    case XB_M2P: xf_pick(34); return 1;
    case XB_SORG: xf_pick(40); return 1;
    case XB_DORG: xf_pick(41); return 1;
    case XB_SREF: xf_pick(60); return 1;
    case XB_DVEC: xf_pick(70); return 1;
    case XB_DOFF: xf_pick(71); return 1;
    }
    return 1;
}
/* Esc while the dialog is up (do_xform_*: get_event returns 2 -> bVar1, the next pass forces 0x65 and clears +0x5092 / cVar11 - no Apply
   round): OK while +0x5090 is set, else Cancel (100). Esc in select_ents / a pick: as before (xf_button 2). */
void xf_escape(void) {
    if (!xf_c) return;
    if (xf_c == G_XDYN || xf_phase != XP_DLG) { xf_button(2); return; }
    xf_button(xf_mod ? 0 : 2);
}
int xf_apply_enabled(void) { if (xf_c == G_XDYN) return xf_phase == XP_DYN && xdy_pend; return xf_c && xf_phase == XP_DLG && xf_mod; }   /* FUN_18000b030: +0x5090 && +0x50a4 */   /* Dynamic: EnableApply(+0x38f) */
int xf_panel_enabled(void) { if (xf_c == G_XDYN) return xf_phase == XP_DYN; return xf_c && xf_phase == XP_DLG; }   /* Dynamic: off while select_ents runs (FUN_1800e8540) */

/* ---------------------------------------------------------------- tests (ours) */
void xf_dump(void) {
    printf("    xf cmd '%s' phase %d sel %d preview %d entries %d refused %d\n", geo_name(geo_cmd), xf_phase, xf_n, xf_prev_valid(), nxi, xf_refused);
    if (xf_c == G_XDYN) xdy_dump();
    for (int e = 0; e < F.nents && xf_bits && e < xf_nbits; e++) if (xf_bits[e] && F.ents[e].type) printf("    xfmark %d = %d\n", e, xf_bits[e]);
}
int xf_script(const char *st, const char *arg) {
    if (xdy_script(st, arg)) return 1;                                     /* Dynamic's test steps (above) */
    if (!strcmp(st, "xfenter")) { xf_enter(); return 1; }                   /* Enter: the end of select_ents */
    if (!strcmp(st, "xfdump")) { xf_dump(); return 1; }
    if (!strcmp(st, "xfstate")) {                                            /* ours: the Scale calculator / Remove Item / +0x5090 state */
        if (xf_c == G_XSCALE) printf("    xfscale s %.4f x %.4f y %.4f z %.4f calc %d target %d combos %d %d base %.4f result %.4f factor %.4f mod %d\n", xs.s, xs.sx, xs.sy, xs.sz,
                                     xsc.on, xsc.target, xsc.cb, xsc.cr, xsc.base, xsc.res, xsc.fac, xf_mod);
        if (xf_c == G_XROT || xf_c == G_XARRAY) printf("    xfitems removed %d mod %d apply %d\n", xf_nrm, xf_mod, xf_apply_enabled());
        return 1; }
    if (!strcmp(st, "xfbtn") && arg) { xf_button(atoi(arg)); return 1; }   /* a dialog button (codes above) */
    if (!strcmp(st, "clearcolors")) { xf_clear_colors(); return 1; }
    if (!strcmp(st, "xfents")) {                                            /* the entities' level / colour / mark */
        for (int e = 0; e < F.nents; e++) if (F.ents[e].type) printf("    xfent %d type %d level %d color %d mark %d\n", e, F.ents[e].type, F.ents[e].level, F.ents[e].color, xf_ent_mark(e));
        return 1; }
    if (!strcmp(st, "xfset") && arg) {                                      /* xfset:k=v,... the running dialog's fields */
        char a[512]; snprintf(a, sizeof a, "%s", arg);
        XfTrans *t = xf_c == G_XSTRETCH ? &xst : &xt;
        const struct { const char *k; int c; double *d; int *i; } T[] = {
            { "mode", G_XTRANS, NULL, &xt.mode }, { "copies", G_XTRANS, NULL, &xt.copies }, { "total", G_XTRANS, NULL, &xt.bytotal }, { "dir", G_XTRANS, NULL, &xt.dir },
            { "dx", G_XTRANS, &t->dx }, { "dy", G_XTRANS, &t->dy }, { "dz", G_XTRANS, &t->dz }, { "pang", G_XTRANS, &t->pang }, { "plen", G_XTRANS, &t->plen },
            { "ua", G_XTRANS, NULL, &xt.useattr }, { "incr", G_XTRANS, NULL, &xt.incr }, { "lv", G_XTRANS, NULL, &xt.level }, { "co", G_XTRANS, NULL, &xt.color },
            { "mode", G_XSTRETCH, NULL, &xst.mode }, { "copies", G_XSTRETCH, NULL, &xst.copies }, { "total", G_XSTRETCH, NULL, &xst.bytotal },
            { "mode", G_XROT, NULL, &xr.mode }, { "copies", G_XROT, NULL, &xr.copies }, { "total", G_XROT, NULL, &xr.bytotal }, { "dir", G_XROT, NULL, &xr.dir },
            { "ang", G_XROT, &xr.ang }, { "cx", G_XROT, &xr.ctr.x }, { "cy", G_XROT, &xr.ctr.y }, { "translate", G_XROT, NULL, &xr.translate },
            { "ua", G_XROT, NULL, &xr.useattr }, { "incr", G_XROT, NULL, &xr.incr }, { "lv", G_XROT, NULL, &xr.level }, { "co", G_XROT, NULL, &xr.color },
            { "mode", G_XMIRROR, NULL, &xmi.mode }, { "axis", G_XMIRROR, NULL, &xmi.axis }, { "my", G_XMIRROR, &xmi.ref.y }, { "mx", G_XMIRROR, &xmi.ref.x }, { "ma", G_XMIRROR, &xmi.ang },
            { "n1", G_XARRAY, NULL, &xa.n1 }, { "n2", G_XARRAY, NULL, &xa.n2 }, { "l1", G_XARRAY, &xa.l1 }, { "l2", G_XARRAY, &xa.l2 }, { "a1", G_XARRAY, &xa.a1 }, { "a2", G_XARRAY, &xa.a2 },
            { "d1", G_XARRAY, NULL, &xa.d1 }, { "d2", G_XARRAY, NULL, &xa.d2 }, { "ua", G_XARRAY, NULL, &xa.useattr }, { "incr", G_XARRAY, NULL, &xa.incr }, { "lv", G_XARRAY, NULL, &xa.level },
            { "mode", G_XTRANS3D, NULL, &x3.mode }, { "copies", G_XTRANS3D, NULL, &x3.copies }, { "vfrom", G_XTRANS3D, NULL, &x3.vfrom }, { "vto", G_XTRANS3D, NULL, &x3.vto },
            { "fx", G_XTRANS3D, &x3.pfrom.x }, { "fy", G_XTRANS3D, &x3.pfrom.y }, { "fz", G_XTRANS3D, &x3.pfrom.z }, { "tx", G_XTRANS3D, &x3.pto.x }, { "ty", G_XTRANS3D, &x3.pto.y }, { "tz", G_XTRANS3D, &x3.pto.z },
            { "mode", G_XSCALE, NULL, &xs.mode }, { "copies", G_XSCALE, NULL, &xs.copies }, { "xyz", G_XSCALE, NULL, &xs.xyz }, { "pct", G_XSCALE, NULL, &xs.pct },
            { "s", G_XSCALE, &xs.s }, { "sx", G_XSCALE, &xs.sx }, { "sy", G_XSCALE, &xs.sy }, { "sz", G_XSCALE, &xs.sz }, { "auto", G_XSCALE, NULL, &xs_auto },
            { "mode", G_XDIST, NULL, &xd.mode }, { "equal", G_XDIST, NULL, &xd.equal }, { "n", G_XDIST, NULL, &xd.n }, { "s", G_XDIST, &xd.s }, { "ms", G_XDIST, &xd.ms }, { "me", G_XDIST, &xd.me },
            { "just", G_XDIST, NULL, &xd.just }, { "center", G_XDIST, NULL, &xd.center }, { "rot", G_XDIST, NULL, &xd.rot }, { "edge", G_XDIST, NULL, &xd.edge }, { "adjust", G_XDIST, NULL, &xd.adjust }, { "off", G_XDIST, &xd.off },
            { "copy", G_XDYN, NULL, &xdp.copy }, { "single", G_XDYN, NULL, &xdp.single }, { "n", G_XDYN, NULL, &xdp.count }, { "last", G_XDYN, NULL, &xdp.last },
            { "setgeom", G_XDYN, NULL, &xdp.setgeom }, { "gsize", G_XDYN, NULL, &gn_size }, { "ainc", G_XDYN, &gn_ang_inc }, { "aon", G_XDYN, NULL, &gn_ang_on },
            { "ron", G_XDYN, NULL, &gn_ruler_on }, { "rsize", G_XDYN, NULL, &gn_ruler_size },
            { "cb", G_XSCALE, NULL, &xsc.cb }, { "cr", G_XSCALE, NULL, &xsc.cr }, { "cbase", G_XSCALE, &xsc.base }, { "cres", G_XSCALE, &xsc.res }, { "cfac", G_XSCALE, &xsc.fac },
            { "ox", G_XSCALE, &xs.org.x }, { "oy", G_XSCALE, &xs.org.y }, { "ua", G_XSCALE, NULL, &xs.useattr }, { "incr", G_XSCALE, NULL, &xs.incr }, { "lv", G_XSCALE, NULL, &xs.level } };
        for (char *sv = NULL, *kv = strtok_r(a, ",", &sv); kv; kv = strtok_r(NULL, ",", &sv)) {
            char *eq = strchr(kv, '='); if (!eq) continue; *eq++ = 0; int hit = 0;
            for (size_t k = 0; k < sizeof T / sizeof T[0] && !hit; k++) {
                int cmd = T[k].c == G_XTRANS && xf_c == G_XSTRETCH && T[k].d ? G_XSTRETCH : T[k].c;
                if (cmd != xf_c || strcmp(T[k].k, kv)) continue;
                if (T[k].d) *T[k].d = atof(eq); else *T[k].i = atoi(eq); hit = 1; }
            if (!hit) printf("    (unknown xform option %s)\n", kv);
        }
        xf_poll();
        return 1;
    }
    return 0;
}
/* config items (cui_config_item): 5010 Group color, 5031 Result color, 5044 Use Group and Result color in Xform?,
   10026 Preview? (mccore FUN_1801d3400 / FUN_1801b7d30 case 0x272a) */
void xf_config_item(int key, const char *q) {
    char yn = 0; sscanf(q, " %c", &yn); int v;
    if (key == 5044) cfg_xf_grpres = yn == 'y';
    else if (key == 10026) cfg_xf_preview = yn == 'y';
    else if (sscanf(q, "%d", &v) == 1) { if (key == 5010) cfg_group_color = v; else if (key == 5031) cfg_result_color = v; }
}

/* ---------------------------------------------------------------- units change: scale the part (formats/units.md 4) */
/* fmu_scale_db (mccore @1808d3590, decomp/units/core_scale.c): after "Scale current part to Metric / English?" (mccoreres 5304 /
   5303, fmu_scale_yn) every geometry entity (mask 0x1f1fff) goes through FUN_1808d8820 with the matrix diag(25.4) (to metric) /
   diag(1 / 25.4) (to inch) and no translation - a scale about the world origin, the originals replaced (Move); the call
   (decomp/xform/gaps/fmu_call.txt, 0x1808d3884): param_10 = 2 (xform_type: a scale), param_11 = XMM6 = the ratio (the diagonal's value,
   fmu_call2.txt 0x1808d36b6 / 36c0), param_12 = 1, param_7 = 1 (attributes kept). Ours: points, lines, arcs, spline curves (E_SPLINE:
   FUN_1808d7f00 / FUN_1808da030, +0xa8 x |ratio|) and surfaces of kinds 0 / 1 / 10 (mcsb_xform; a trimmed surface's blanked base goes
   with it, as Move to Origin) through xf_ent as Xform Scale's Move does (one undo step). Solids (sld_xform_solid; on "No"
   sld_scale_sldBodies) are not scaled here: this program's solids are Extrude features rebuilt from their chains (NOT PORTED) - their
   count and that of the entities xf_ent refuses are returned in *skipped. */
static int xf_force_type = -1;                          /* the xform_type of a call outside the dialogs (fmu_scale_db: 2) */
int xf_scale_all(double s, int *skipped) {
    int ns = 0, nd = 0, n0 = F.nents;
    XM M = xm_id(); M.m[0] = M.m[4] = M.m[8] = s;
    unsigned char *base = calloc((size_t)n0 + 1, 1);    /* the blanked bases of trimmed surfaces: transformed with them (xf_surf) */
    for (int e = 0; e < n0 && base; e++) if (F.ents[e].type == E_SURF && F.ents[e].srf >= 0 && F.surfs[F.ents[e].srf].kind == 10) {
        const Surf *b = mcam_surf_base(&F, &F.surfs[F.ents[e].srf]); if (b && b->ent >= 0 && b->ent < n0) base[b->ent] = 1; }
    xf_force_type = 2; xf_fac = s; xf_refused = 0;
    mod_begin();
    for (int e = 0; e < n0; e++) {
        int ty = F.ents[e].type;
        if (!ty || F.ents[e].gen || (base && base[e])) continue;
        int r = xf_ent(e, &M, F.ents[e].level, F.ents[e].color);
        if (r < 0) { ns++; continue; }
        mod_pair(e, r);
        if (F.ents[e].type == E_SURF) srf_drop_base(e);   /* delete_ent's rule: the old base goes */
        mod_del(e); nd++;
    }
    mod_end();
    xf_force_type = -1; xf_fac = 1.0; free(base);
    ns += F.nsolids;
    if (skipped) *skipped = ns;
    if (nd) { doc_dirty = 1; scene_reupload(); overlay_dirty = 1; }
    return nd;
}
