/* cui_spline.c - the Wireframe tab's spline commands (Splines group, Modify Spline group; ribbon rows in viewer/ui/layout.txt, FT functions sketcher_spline_1 .. 4,
 * x_2_nurbs, UntrimSpline, simplify, cpts_nurbs, RefitSpline). Spec: ../../formats/wireframe.md "Wireframe spline commands (N9-16)". Sources (decompiled, cited per rule): mccad.dll
 * WireframeSplineManualApp FUN_180260ef0 (sketcher_spline_1), FUN_180266560 (sketcher_spline_2 Automatic), FUN_1801ee790 (sketcher_spline_4 Blended), the base FUN_180227990
 * (create) / FUN_180227cd0 (end conditions) / FUN_180228600 (one end's condition); mccadutil UntrimSpline, convert_to_nurbs; MCCAD.UI / MCCADUtil.UI BAML panels; mccore: mcspline_fit.c.
 * The rubber bands (mastercam.dll draw_rubber_band cases 9, 0x21: decomp/rubber/rb.c, FUN_18033d520) are drawn as Mastercam computes them, solid and at a fixed sampling (ours: the overlay has no dash here). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "../scene.h"
#include "../mcsurf.h"
#include "../mcsurf_build.h"
#include "../mcspline_fit.h"
#include "../mcam_saveops.h"
#include "../mcspline_simplify.h"
#include "../mcspline_chain.h"
#include "../mcspline_refit.h"
#include "../mcsurf_tonurbs.h"
#include "../cam2d.h"

int sp_cmd(int c) { return c == G_SPLMAN || c == G_SPLAUTO || c == G_SPLBLEND || c == G_X2NURBS || c == G_UNTRIM || c == G_SIMPL || c == G_MODIFYSP || c == G_REFIT || c == G_CSURF; }   /* + Curve Surface (cui_csurf.c): the same one-select_ents flow as Convert to NURBS */

/* ---- CSplineNodeMgr's node list (mccore add_pt_to_spline_nodes1 / get_pt_from_spline_nodes1): AllocateMem starts at 4000 (DAT_1813c8000), grown on demand (FUN_18027c5e0 from max - 5): so unbounded here */
static V3 *sp_n; static int sp_nn, sp_ncap;
static void sp_grow(int n) { if (n > sp_ncap) { sp_ncap = n + 1000; sp_n = realloc(sp_n, sizeof(V3) * (size_t)sp_ncap); } }
static double sp_tan[2][3]; static int sp_code[2];              /* the end conditions as FUN_180227990 passes them (0 natural, 2 / 3 given tangent) */
static int sp_livent = -1, sp_livefrom = -1, sp_live_top;       /* the spline created by the current Manual spline's creation (F.nents index), the undo mark */
static int sp_in_edit;                                          /* FUN_180227cd0: the end conditions loop on the made spline */
static int sp_prompt;                                           /* the current prompt selector */
/* panel values (WireframeSplineManualManagerView / WireframeSplineEndpointConditionView BAML; FUN_1801d3830 / FUN_1801d3c20: combo index 0 Natural 1 3 Point 2 To Entity 3 To End 4 Angle) */
static int sp_edit_ec;                                          /* the "Edit End Conditions" checkbox (FUN_1801d6b30), default off: FunctionPanelEditableBool ctor */
static int sp_cb[2];                                            /* combo: start / end */
static double sp_ang[2];                                        /* degrees (FUN_1801d3830: x 0.01745329251994329) */
static int sp_dir1[2] = { 1, 1 };                               /* the "Direction 1" radio (IsDirection1, "1" default) */
static int sp_pick_side = -1;                                   /* FUN_180228600's select_ent (conditions 2 To Entity / 3 To End: mccoreres 1877): the side waiting for an entity */
static int sp_side_code[2], sp_flip_done[2];

int srf_surface_type(void);   /* cui_surf.c: config 10014 */
extern int srf_front_color;   /* cui_surf.c */
static int sp_mod_e = -1, sp_mod_cp = -1;                       /* Modify Spline: the curve, the control point / node picked (waiting for its new position) */
static void sp_ref_begin(void); static void sp_ref_build(void); static void sp_ref_commit(void); static void sp_ref_drop(void); static int sp_ref_on;
static int sp_mod_ep[2]; static double sp_mod_tan[2][3];         /* Modify (parametric): the panel's Tangency Endpoint 1 / 2 (local_2c34), the end tangents kept (FUN_18029e630) */
static int sp_ble_flip[2];                                      /* Blended: FUN_1801ee220's reversal flags */
/* cfg_createNurbs (mccore surftype_fns.c case 0x271e / 0x271d; decomp/surfedit/core_backdraw.c): surface_type (config 10014, factory 2) 2 -> NURBS, 1 -> parametric, 0 -> nurbs_as_backup (config 10013, factory 1) */
int sp_nurbs_cfg(void) {
    int st = srf_surface_type();
    if (st == 2) return 1;
    if (st != 0) return 0;
    char v[32]; const char *s = mcam_config_value(mcam_is_metric(&F), 10013, v, sizeof v);
    return s && s[0] ? atoi(s) != 0 : 1;
}

static int sp_near(V3 a, V3 b, double tol) { double d = sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); return d < tol || d == tol; }

/* create the spline through the node list with the end conditions: FUN_180227990: at least 2 nodes (4 when the last node is on the first: closed) else warning 0x16ed (mccoreres
   5869); parametric: make_spline_from_pts (make_spline_coefs_open / _closed, put_spline_in_db); NURBS: convert_points_to_nurbs (create_nurbs_from_points), put_nurbs_in_db. Returns the Ent index, -1 */
static int sp_fit_nodes(void);
static int sp_make(void) {
    double tol = mc_systol.tol;
    if (sp_nn < 1) return -1;
    double last[3] = { sp_n[sp_nn - 1].x, sp_n[sp_nn - 1].y, sp_n[sp_nn - 1].z }, first[3] = { sp_n[0].x, sp_n[0].y, sp_n[0].z };
    double cl = sqrt((last[0] - first[0]) * (last[0] - first[0]) + (last[1] - first[1]) * (last[1] - first[1]) + (last[2] - first[2]) * (last[2] - first[2]));
    int mn = cl < tol ? 4 : 2;
    if (sp_nn < mn) { snprintf(cam_msg, sizeof cam_msg, "Warning - illegal spline (too few points)"); overlay_dirty = 1; return -1; }    /* mccoreres 5869 */
    return sp_fit_nodes();
}
/* Curve Dynamic (FUN_180282390): make_spline_from_pts / create_nurbs_from_points of the points with natural ends (codes 0, v_z tangents); its check is "more than one point", no minimum for a closed one */
int sp_fit_points(const double (*Q)[3], int n) {
    sp_grow(n);                                          /* the node list grows on demand (CSplineNodeMgr, steps of 1000, FUN_18027c5e0) */
    sp_nn = n; for (int i = 0; i < n; i++) sp_n[i] = (V3){ Q[i][0], Q[i][1], Q[i][2] };
    sp_code[0] = sp_code[1] = 0; memset(sp_tan, 0, sizeof sp_tan); sp_tan[0][2] = sp_tan[1][2] = 1;   /* v_z_3d, unused with code 0 */
    return sp_fit_nodes();
}
static int sp_fit_nodes(void) {
    double tol = mc_systol.tol;
    double last[3] = { sp_n[sp_nn - 1].x, sp_n[sp_nn - 1].y, sp_n[sp_nn - 1].z }, first[3] = { sp_n[0].x, sp_n[0].y, sp_n[0].z };
    double cl = sqrt((last[0] - first[0]) * (last[0] - first[0]) + (last[1] - first[1]) * (last[1] - first[1]) + (last[2] - first[2]) * (last[2] - first[2]));
    double (*Q)[3] = malloc(sizeof(double) * 3 * sp_nn);
    for (int i = 0; i < sp_nn; i++) { Q[i][0] = sp_n[i].x; Q[i][1] = sp_n[i].y; Q[i][2] = sp_n[i].z; }
    uint32_t rn = 0; uint8_t *rec = NULL; int res = -1;
    if (sp_nurbs_cfg()) {
        McsfNb o; memset(&o, 0, sizeof o);
        int r = mcsf_nurbs_fit((const double (*)[3])Q, sp_nn, sp_code[0], sp_tan[0], sp_code[1], sp_tan[1], tol, &o);
        if (r == 1) snprintf(cam_msg, sizeof cam_msg, "Warning - cannot create spline: two or more node points are repeated");   /* 0x16ee: mccoreres 5870 */
        else if (r == 8) snprintf(cam_msg, sizeof cam_msg, "Warning - cannot create spline: insufficient memory available");     /* 0x1813: mccoreres 6163 */
        else if (r == 0) { rec = mcsf_nurbs_rec(&o, tol, &rn); free(o.k); free(o.cp); }
    } else if (cl < tol) {                          /* make_spline_from_pts: closed */
        double *C = malloc(sizeof(double) * 12 * sp_nn);
        if (mcsf_coefs_closed(C, (const double (*)[3])Q, sp_nn, tol)) rec = mcsf_spline_rec(C, sp_nn - 1, first, last, 1, tol, &rn);
        else snprintf(cam_msg, sizeof cam_msg, "Warning - cannot create spline: two or more node points are repeated");
        free(C);
    } else {
        double *C = malloc(sizeof(double) * 12 * sp_nn);
        if (mcsf_coefs_open(C, (const double (*)[3])Q, sp_nn, sp_code[0], sp_tan[0], sp_code[1], sp_tan[1], tol)) rec = mcsf_spline_rec(C, sp_nn - 1, first, last, 0, tol, &rn);
        else snprintf(cam_msg, sizeof cam_msg, "Warning - cannot create spline: two or more node points are repeated");
        free(C);
    }
    free(Q);
    if (rec) { res = mcam_new_curve(&F, rec, rn, geo_level, geo_color); free(rec); if (res >= 0) { attr_new_ent(res); doc_dirty = 1; } overlay_dirty = 1; }
    return res;
}

/* the current spline made by the last sp_make replaced: FUN_180227820 deletes it */
static void sp_remove(int k) {
    if (k < 0 || k >= F.nents || F.ents[k].type != E_SPLINE) return;
    ent_off(k); F.ents[k].type = 0; overlay_dirty = 1;
}

/* sktcher_live_color / sktcher_main_color: the live spline's colour */
static void sp_set_color(int k, int c) {
    if (k < 0 || k >= F.nents || !F.ents[k].type) return;
    F.ents[k].color = c;
    for (int q = 0; q < F.ents[k].npoly; q++) { int pi = F.ents[k].first_poly + q; if (pi < F.npolys) F.polys[pi].color = (uint8_t)c; }
    if (F.ents[k].crv >= 0 && F.ents[k].crv < F.ncrvs) F.crvs[F.ents[k].crv].rec[6] = (uint8_t)c;
    scene_reupload(); overlay_dirty = 1;
}
/* FUN_180227990 again with the current conditions: the end conditions' rebuild (FUN_180228600 l.0x228d70 ...), sktcher_live_color */
static void sp_rebuild(void) {
    if (sp_livent >= 0) { sp_remove(sp_livent); sp_livent = -1; }
    sp_livent = sp_make();
    if (sp_in_edit && sp_livent >= 0) sp_set_color(sp_livent, cfg_live_color);
}

/* the construction view's z axis and depth (get_view_matrix row 2; constr_depth + the active origin's view z when ActiveOriginIsOn) */
static void sp_constr_plane(double n[3], double *depth) {
    double z[3] = { 0, 0, 1 }, zero[3] = { 0, 0, 0 }; plane_v2w(plane_active[0], zero, z, n);
    const double *o = plane_active_origin(0); *depth = constr_depth + (o ? o[0] * n[0] + o[1] * n[1] + o[2] * n[2] : 0);
}
/* proj_a_point (mccore 0x180662d00): in 2D construction a point is projected along the Cplane's z to the construction depth (proj_pt_to_view); 3D: as it is */
static V3 sp_proj_a(V3 q) {
    if (cfg_constr_3d) return q;
    double n[3], dp; sp_constr_plane(n, &dp);
    double h = dp - (q.x * n[0] + q.y * n[1] + q.z * n[2]);
    q.x += n[0] * h; q.y += n[1] * h; q.z += n[2] * h; return q;
}
static void sp_reset_conditions(void) {                         /* FUN_1802278b0: both conditions = natural (combo 0, FunctionPanelEditableBool true: direction 1) */
    sp_cb[0] = sp_cb[1] = 0; sp_dir1[0] = sp_dir1[1] = 1; sp_side_code[0] = sp_side_code[1] = 0; sp_flip_done[0] = sp_flip_done[1] = 0;
    sp_code[0] = sp_code[1] = 0; memset(sp_tan, 0, sizeof sp_tan);
}
/* the node at the end of a side (side 0 start, 1 end) */
static V3 sp_node(int i) { return sp_n[i]; }

/* ---- eval_curve (mccore; core9.c) on an entity of the app, curve_t1t2, proj_pt_on_ent (0x1800184e0) */
static void sp_t1t2(int e, double *t1, double *t2) {           /* curve_t1t2: line / arc 0 .. 1, a spline ent +0x98 / +0xa0 */
    const Ent *en = &F.ents[e];
    if (en->type == E_SPLINE) mcs_spline_t1t2(&F.crvs[en->crv], t1, t2); else { *t1 = 0; *t2 = 1; }
}
static int sp_ev(int e, double t, double P[3], double D[3]) {  /* eval_curve(ent, 1, t): the point and the first derivative in the entity's parameter */
    const Ent *en = &F.ents[e];
    if (en->type == E_LINE) { P[0] = en->a.x + (en->b.x - en->a.x) * t; P[1] = en->a.y + (en->b.y - en->a.y) * t; P[2] = en->a.z + (en->b.z - en->a.z) * t; D[0] = en->b.x - en->a.x; D[1] = en->b.y - en->a.y; D[2] = en->b.z - en->a.z; return 1; }
    if (en->type == E_ARC) {                                    /* t x sweep + start in the arc's view; the derivative's radial vector rotated, x sweep (eval_curve l.718-753) */
        double a = t * en->sw + en->a0, cx = cos(a) * en->r, cy = sin(a) * en->r, X[3] = { 1, 0, 0 }, Y[3] = { 0, 1, 0 };
        if (en->vw) for (int k = 0; k < 3; k++) { X[k] = en->vm[k]; Y[k] = en->vm[3 + k]; }
        P[0] = en->c.x + cx * X[0] + cy * Y[0]; P[1] = en->c.y + cx * X[1] + cy * Y[1]; P[2] = en->c.z + cx * X[2] + cy * Y[2];
        for (int k = 0; k < 3; k++) D[k] = en->sw * (-cy * X[k] + cx * Y[k]);
        return 1;
    }
    if (en->type == E_SPLINE && en->crv >= 0 && en->crv < F.ncrvs) { double o[3][3]; if (mcs_spline_eval_d(&F.crvs[en->crv], t, 1, o)) return 0; memcpy(P, o[0], 24); memcpy(D, o[1], 24); return 1; }
    return 0;
}
/* proj_pt_on_coeff (0x180018200): the foot on one polynomial segment [power][xyz] (order 4, dim 3), Newton from 0.5, clamped twice at an end = the end */
static void sp_proj_coeff(const double T[3], const uint8_t *c, double *u, double Q[3], double *dist) {
    double tol = mc_systol.tol, a[4][3]; int at0 = 0, at1 = 0;
    memcpy(a, c, sizeof a); *u = 0.5;
    for (int it = 0; it < 100; it++) {
        double x = *u, d[3];
        for (int k = 0; k < 3; k++) { Q[k] = ((a[3][k] * x + a[2][k]) * x + a[1][k]) * x + a[0][k]; d[k] = ((3 * a[3][k] * x + 2 * a[2][k]) * x + a[1][k]); }
        double R[3] = { T[0] - Q[0], T[1] - Q[1], T[2] - Q[2] }; *dist = sqrt(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]);
        if (*dist < tol) return;
        double f = d[0] * R[0] + d[1] * R[1] + d[2] * R[2]; if (fabs(f) < 2e-7) return;
        double l = d[0] * d[0] + d[1] * d[1] + d[2] * d[2]; x = f / l + *u; *u = x;
        int end = 0;
        if (x > 1.0) { at0 = 0; *u = 1.0; if (at1) end = 1; at1 = 1; }
        else if (x < 0.0) { at1 = 0; *u = 0.0; if (at0) end = 1; at0 = 1; }
        if (end) { x = *u; for (int k = 0; k < 3; k++) Q[k] = ((a[3][k] * x + a[2][k]) * x + a[1][k]) * x + a[0][k];
            *dist = sqrt((T[0] - Q[0]) * (T[0] - Q[0]) + (T[1] - Q[1]) * (T[1] - Q[1]) + (T[2] - Q[2]) * (T[2] - Q[2])); return; }
    }
}
/* proj_pt_on_ent(ent, T, view 0, seed, Q, t, ok, 0): a parametric spline: every segment (FUN_180018af0), the nearest, stopping at one within tol (ok = 1); else Newton on t from the seed with the range rules */
static void sp_proj_core(int e, const CurveRec *cr, const Ent *en_type, const double T[3], double seed, double Q[3], double *tout, int *ok) {
    double tol = mc_systol.tol; *tout = seed; *ok = 0;
    int cls = cr ? (int)cr->cls : 0;
    if (cls == 8) {
        const uint8_t *r = cr->rec; int32_t ns; memcpy(&ns, r + 0xb0, 4); int ord = r[0xb4], dim = r[0xb5];
        if (ord != 4 || dim != 3) return;                       /* proj_pt_on_coeff(p, 4, ...): cubics in 3D */
        double best = 1e38;
        for (int i = 0; i < ns; i++) { double u, q[3], d; sp_proj_coeff(T, r + 0xc0 + 8 * (size_t)(i * 12), &u, q, &d);
            if (d < best) { best = d; memcpy(Q, q, 24); *tout = (double)i + u; if (d < tol) break; } }
        *ok = 1; return;
    }
    double t1, t2, Pa[3], Pb[3], D[3], clos;
    if (cr) { mcs_spline_t1t2(cr, &t1, &t2); memcpy(Pa, cr->rec + 0x68, 24); memcpy(Pb, cr->rec + 0x80, 24); }
    else { t1 = 0; t2 = 1; if (!sp_ev(e, 0, Pa, D) || !sp_ev(e, 1, Pb, D)) return; }
    (void)en_type;
    clos = sqrt((Pb[0] - Pa[0]) * (Pb[0] - Pa[0]) + (Pb[1] - Pa[1]) * (Pb[1] - Pa[1]) + (Pb[2] - Pa[2]) * (Pb[2] - Pa[2]));   /* curve_endpoints */
    if (cls == 0x80) { if (*tout < t1) *tout = t1; else if (*tout > t2) *tout = t2; }
    int at1 = 0, at2 = 0, damp = 0; double prev = 0;
    for (int it = 0; it <= 99; it++) {
        double o[3][3];
        if (cr) { if (mcs_spline_eval_d(cr, *tout, 1, o)) return; memcpy(Q, o[0], 24); memcpy(D, o[1], 24); }
        else if (!sp_ev(e, *tout, Q, D)) return;
        double R[3] = { T[0] - Q[0], T[1] - Q[1], T[2] - Q[2] };
        if (sqrt(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]) < tol) { *ok = 1; return; }
        double f = D[1] * R[1] + D[0] * R[0] + D[2] * R[2];
        if (fabs(f) < 2e-7) { *ok = 1; return; }
        if (!damp) { if (it > 0x1e) damp = 1; }
        else {
            int keep = 0;
            if (prev != 0.0) { if (fabs(prev) * 0.5 < fabs(f)) f = 0.0 <= f * prev ? prev * 0.5 : prev * -0.5; keep = 0.0 <= f * prev; }
            if (!keep) prev = f;
        }
        double l = D[0] * D[0] + D[1] * D[1] + D[2] * D[2];
        *tout = f / l + *tout;
        if (cls == 0x80) {
            double u = *tout, r = t2 - t1;
            if (t1 <= u) {
                if (t2 < u) { if (tol <= clos) { *tout = t2; if (at2) return; at2 = 1; at1 = 0; continue; } *tout = u - (double)(int)fabs(u / r) * r; }
                at2 = 0; at1 = 0;
            } else if (tol <= clos) { *tout = t1; if (at1) return; at1 = 1; at2 = 0; }
            else { at1 = 0; at2 = 0; *tout = (double)((int)fabs(u / r) + 1) * r + u; }
        }
    }
}
static void sp_proj(int e, const double T[3], double seed, double Q[3], double *t, int *ok) {
    const Ent *en = &F.ents[e];
    sp_proj_core(e, en->type == E_SPLINE ? &F.crvs[en->crv] : NULL, en, T, seed, Q, t, ok);
}
/* the t a pick gives on an entity (select_ent's returned t: its mastercam.dll picking is NOT DECODED - ours, as spline_chain.md: line / arc the nearest point of the click; a spline the nearest point of its drawn polyline (the break points of brk_spline_const_err at curve_chord_dev), interpolated in t) */
double sp_pick_t(int e, V3 w) {
    const Ent *en = &F.ents[e]; double t1, t2; sp_t1t2(e, &t1, &t2);
    if (en->type == E_LINE) { double dx = en->b.x - en->a.x, dy = en->b.y - en->a.y, L2 = dx * dx + dy * dy; if (L2 < 1e-12) return 0; double s = ((w.x - en->a.x) * dx + (w.y - en->a.y) * dy) / L2; return s < 0 ? 0 : s > 1 ? 1 : s; }
    if (en->type == E_ARC) { double a = atan2(w.y - en->c.y, w.x - en->c.x), sw = en->sw, d = a - en->a0; while (d < 0) d += 2 * M_PI; while (d > 2 * M_PI) d -= 2 * M_PI; if (sw < 0) { d = 2 * M_PI - d; sw = -sw; } double f = sw > 0 ? d / sw : 0; return f < 0 ? 0 : f > 1 ? 1 : f; }
    const CurveRec *cr = &F.crvs[en->crv]; double t = t1, pt = t1, best = 1e300, bt = t1, o[3][3], P0[3] = { 0, 0, 0 };
    double dev = mc_systol.curve_chord_dev, mi = mc_systol.curve_min_step, ma = mc_systol.curve_max_step; int it = 0;
    if (mcs_spline_eval_d(cr, t, 0, o)) return t1;
    memcpy(P0, o[0], 24);
    while (t < t2 - 2e-7 && it++ < 100000) {
        pt = t; mcs_brk_spline_const_err(cr, mi, ma, dev, 0, mc_systol.tol, &t);
        if (mcs_spline_eval_d(cr, t, 0, o)) break;
        double dx = o[0][0] - P0[0], dy = o[0][1] - P0[1], L2 = dx * dx + dy * dy, s = L2 > 0 ? ((w.x - P0[0]) * dx + (w.y - P0[1]) * dy) / L2 : 0; if (s < 0) s = 0; if (s > 1) s = 1;
        double qx = P0[0] + s * dx, qy = P0[1] + s * dy, D = (qx - w.x) * (qx - w.x) + (qy - w.y) * (qy - w.y);
        if (D < best) { best = D; bt = pt + s * (t - pt); }
        memcpy(P0, o[0], 24);
    }
    return bt;
}

/* natural_tan of the first / last up to 5 nodes (FUN_180228600 condition 1) */
static void sp_natural_side(int side) {
    int st = side == 0, n = sp_nn < 6 ? sp_nn : 5, ok; double Q[5][3], t[3];
    for (int i = 0; i < n; i++) { V3 q = st ? sp_node(i) : sp_node(sp_nn - n + i); Q[i][0] = q.x; Q[i][1] = q.y; Q[i][2] = q.z; }
    mcsf_natural_tan((const double (*)[3])Q, st, t, n, mc_systol.tol, &ok);
    if (ok) { memcpy(sp_tan[side], t, 24); sp_code[side] = 0; }
}
/* one end's condition (FUN_180228600): side 0 start, 1 end; cond = the combo's code (FUN_1801d3830: 1 Natural, 0 3 Point, 2 To Entity, 3 To End, 4 Angle); flip = the direction radio's reversal */
static void sp_apply_side(int side, int cond, double ang_rad, int flip) {
    double tol = mc_systol.tol;
    int st = side == 0;                       /* the decompile's param_3 (1 = start) */
    if (flip && sp_code[side] == 2) { for (int k = 0; k < 3; k++) sp_tan[side][k] = -sp_tan[side][k]; sp_flip_done[side] = 1; }   /* the direction radio on a kept tangent */
    double t[3] = { 0, 0, 0 };
    if (cond == 0) {                          /* 3 Point: tan_3p_arc of the 3 nodes at that end, or the chord with 2 nodes */
        if (sp_code[side] != 3 && !flip) {
            if (sp_nn == 2) { V3 a = sp_node(0), b = sp_node(1); t[0] = b.x - a.x; t[1] = b.y - a.y; t[2] = b.z - a.z; double l = sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]); if (l == 0) t[0] = t[1] = t[2] = 0; else { t[0] /= l; t[1] /= l; t[2] /= l; } memcpy(sp_tan[side], t, 24); sp_code[side] = 3; }
            else {
                V3 p9 = st ? sp_node(2) : sp_node(sp_nn - 1), p10 = st ? sp_node(1) : sp_node(sp_nn - 2), p11 = st ? sp_node(0) : sp_node(sp_nn - 3);
                double a[3] = { p11.x, p11.y, p11.z }, b[3] = { p10.x, p10.y, p10.z }, c[3] = { p9.x, p9.y, p9.z };
                if (mcsf_tan_3p_arc(a, b, c, st, sp_tan[side], tol)) sp_code[side] = 3;
            }
        }
    } else if (cond == 1) {                   /* Natural: natural_tan of the first / last up to 5 nodes */
        if (sp_code[side] != 0 && !flip) sp_natural_side(side);
    } else if ((cond == 2 || cond == 3) && !flip) {
        sp_pick_side = side;                  /* select_ent(mccoreres 1877, mask 0x8e): the next click (sp_click) */
        snprintf(cam_msg, sizeof cam_msg, "Select line, arc, or spline"); overlay_dirty = 1;
        return;
    } else if (cond == 4 && sp_code[side] != 2 && !flip) {   /* Angle: FUN_180228600 l.0x228d20 */
        double v[3] = { cos(ang_rad), sin(ang_rad), 0 }, zero[3] = { 0, 0, 0 }; plane_v2w(plane_active[0], zero, v, t);   /* view_to_world(.., GetActiveViewNo(Planes, 0)): the Cplane */
        memcpy(sp_tan[side], t, 24); sp_code[side] = 2;
    }
    sp_rebuild();
}

/* the To Entity / To End pick of FUN_180228600 l.0x228a20-0x228cc0: To Entity: proj_pt_on_ent(ent, the end node, 0, pick t), the tangent at it; To End: the end t2, or t1 when 2 pick t < t1 + t2; tan_on_ent_from_t (below tol: no change), oriented along the chord of the two end nodes, unit; condition code 2 */
static void sp_cond_entity(int e, V3 w) {
    int side = sp_pick_side; sp_pick_side = -1;
    if (side < 0 || e < 0 || !(F.ents[e].type == E_LINE || F.ents[e].type == E_ARC || F.ents[e].type == E_SPLINE)) return;
    int st = side == 0, to_end = sp_cb[side] == 3;
    V3 node = st ? sp_node(0) : sp_node(sp_nn - 1);
    double t1, t2, t, P[3], D[3], pick = sp_pick_t(e, w); sp_t1t2(e, &t1, &t2);
    if (to_end) t = pick * 2.0 < t1 + t2 ? t1 : t2;
    else { double T[3] = { node.x, node.y, node.z }, Q[3]; int ok; sp_proj(e, T, pick, Q, &t, &ok); }   /* FUN_180228600: the flag proj_pt_on_ent sets is not read: the returned t is used */
    if (!sp_ev(e, t, P, D)) return;
    double L = sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]); if (L < mc_systol.tol) return;
    V3 a = st ? sp_node(0) : sp_node(sp_nn - 2), b = st ? sp_node(1) : sp_node(sp_nn - 1);
    if ((b.x - a.x) * D[0] + (b.y - a.y) * D[1] + (b.z - a.z) * D[2] < 0) { D[0] = -D[0]; D[1] = -D[1]; D[2] = -D[2]; }
    D[0] /= L; D[1] /= L; D[2] /= L;
    memcpy(sp_tan[side], D, 24); sp_code[side] = 2; sp_flip_done[side] = 0;
    sp_rebuild();
}

/* the finish of a Manual / Automatic spline (FUN_180260ef0 LAB_180261641): when more than one node: UndoTransStart, create, the end conditions loop if the box is checked, commit */
static void sp_undo_start(void) { sp_livefrom = F.nents; sp_live_top = F.nents; }
static void sp_commit(void) {
    if (sp_in_edit && sp_livent >= 0) sp_set_color(sp_livent, geo_color);   /* sktcher_main_color */
    if (sp_livefrom >= 0 && F.nents > sp_livefrom) undo_ents_added(sp_livefrom);
    sp_livefrom = -1;
}
static void sp_finish(int more) {
    if (sp_nn > 1) {
        sp_undo_start();
        sp_code[0] = sp_code[1] = 0; memset(sp_tan, 0, sizeof sp_tan);
        sp_livent = sp_make();
        if (sp_edit_ec && sp_livent >= 0) {   /* FUN_180227cd0: the end conditions loop on the made spline, conditions natural (FUN_1802278b0), then FUN_180228600 for both ends */
            sp_in_edit = 1; sp_prompt = 1;
            sp_reset_conditions();
            sp_side_code[0] = sp_side_code[1] = 1;
            sp_rebuild();
            for (int side = 0; side < 2; side++) sp_natural_side(side);   /* FUN_180227cd0's first two FUN_180228600 (codes -1: natural_tan, no rebuild: the spline pointer is 0 there): the arrows' tangents */
            snprintf(cam_msg, sizeof cam_msg, "Adjust end conditions. Press <ENTER> or <APPLY> when done.");
            overlay_dirty = 1;
            return;
        }
        sp_commit();
    }
    sp_nn = 0; sp_livent = -1;
    if (more) { sp_prompt = 0; snprintf(cam_msg, sizeof cam_msg, "Select points. Press <Enter> or <Apply> when done."); }
    overlay_dirty = 1;
}

/* ---- Spline Automatic: sketcher_spline_2 (FUN_180266560): three points picked (points only: mask 1, CleanEvent), all visible points then walked by get_a_points_process */
static int sp_auto_n;                                          /* picks so far */
static V3 sp_auto_p[3];
static void sp_auto_process(void) {
    /* get_a_points_process (mccore 0x1806625a0) over the visible points (the display list): the first two picks flagged 0x40, the last pick not (FUN_180266560: uVar17 == 2 clears the flag): the walk minimises 1.5 d - 0.5 d.dir among the unflagged
       points farther than tol from the current one and either on the last pick (within tol) or with |d.dir + d| >= tol; it ends on reaching the last pick. The capacity is CountVisiblePoints + 2 (FUN_1806621f0: at the limit, messages 5591 / 5592 and stop); FUN_180266560 then asks (5875: Yes
       makes a spline of the nodes so far and goes on from its last two nodes, No leaves it) */
    double tol = mc_systol.tol; int npt = 0, cap0 = 16;
    int *ptset = malloc(sizeof(int) * (size_t)cap0);
    for (int i = 0; i < F.nents; i++) if (F.ents[i].type == E_POINT && ENT_LV_SHOWN(&F.ents[i])) { if (npt == cap0) { cap0 *= 2; ptset = realloc(ptset, sizeof(int) * (size_t)cap0); } ptset[npt++] = i; }
    char *used = calloc((size_t)(npt > 0 ? npt : 1), 1);
    int limit = npt + 2;
    V3 *N = malloc(sizeof(V3) * (size_t)(limit + 3));
    for (int k = 0; k < npt; k++) { V3 q = F.ents[ptset[k]].a; for (int j = 0; j < 2; j++) if (sp_near(q, sp_auto_p[j], tol)) used[k] = 1; }
    V3 end = sp_auto_p[2];
    N[0] = sp_auto_p[0]; N[1] = sp_auto_p[1];
    int count = 1, more = 1, ok = 1;                       /* *param_4: the index of the last node */
    while (more) {
        ok = 1;
        for (;;) {
            V3 cur = N[count], prev = N[count - 1];
            double dx = cur.x - prev.x, dy = cur.y - prev.y, dz = cur.z - prev.z, dl = sqrt(dx * dx + dy * dy + dz * dz);
            if (dl == 0) { dx = dy = dz = 0; } else { dx /= dl; dy /= dl; dz /= dl; }
            double bestv = 1e38; int b = -1;
            for (int k = 0; k < npt; k++) {
                if (used[k]) continue;
                V3 q = sp_proj_a(F.ents[ptset[k]].a);
                double vx = q.x - cur.x, vy = q.y - cur.y, vz = q.z - cur.z, d = sqrt(vx * vx + vy * vy + vz * vz);
                if (d < tol) { used[k] = 1; continue; }
                double proj = vx * dx + vy * dy + vz * dz, v = d * 1.5 - proj * 0.5;
                double e2 = sqrt((end.x - q.x) * (end.x - q.x) + (end.y - q.y) * (end.y - q.y) + (end.z - q.z) * (end.z - q.z)), s = proj + d;
                if (v < bestv && (e2 < tol || (tol <= fabs(s) && fabs(s) != tol))) { b = k; bestv = v; }
            }
            if (b < 0) break;
            used[b] = 1; count++;
            V3 a = sp_proj_a(F.ents[ptset[b]].a);
            N[count] = a;
            if (count >= limit) { ok = 0; break; }                   /* FUN_1806621f0 */
            double dq = sqrt((end.x - a.x) * (end.x - a.x) + (end.y - a.y) * (end.y - a.y) + (end.z - a.z) * (end.z - a.z));
            if (!(tol <= dq)) break;
        }
        more = 0;
        if (!ok && count == limit) {
            char q[256]; snprintf(q, sizeof q, "More than the maximum number of spline points (%d) \nwere found before reaching the selected endpoint.\n\nDo you want to create multiple splines?", limit);   /* mccadres 5875 */
            if (!cui_hooks.ask_yes_no || cui_hooks.ask_yes_no("Create spline", q)) {   /* 5100; message_box_yes_no flags 0x24 */
                sp_grow(count); memcpy(sp_n, N, sizeof(V3) * (size_t)count); sp_nn = count;
                sp_finish(0);
                N[0] = N[count - 1]; N[1] = N[count]; count = 1; N[2] = end; more = 1;
            }
        }
    }
    free(used); free(ptset);
    if (ok) { sp_grow(count + 1); memcpy(sp_n, N, sizeof(V3) * (size_t)(count + 1)); sp_nn = count + 1; sp_finish(0); }
    free(N);
    if (sp_in_edit) return;
    sp_auto_n = 0;
    snprintf(cam_msg, sizeof cam_msg, "Select the first point");
    overlay_dirty = 1;
}

/* ---- Spline Blended (FUN_1801ee790): pick curve 1 (mask 0x8e), slide the arrow (dyno_curve), pick curve 2, slide, the spline: make_one_segment_spline / _nurb */
static int sp_ble_state, sp_ble_resel = -1, sp_ble_live;          /* 0 / 2: pick curve 1 / 2, 1 / 3: slide; resel: bVar9 (Reselect: the other entity stays); live: the spline made, its undo step open (mod_*) */
static int sp_ble_e[2]; static double sp_ble_t[2];               /* the entity and the parameter on it (dyno_curve's result) */
static double sp_mag[2] = { 1.0, 1.0 };                          /* local_aa88 / uStack_aa80 = 1.0 (Magnitude1 / Magnitude2) */
static int sp_trim = 1;                                          /* the Trim radio: 0 None, 1 Both, 2 Entity 1, 3 Entity 2 (FUN_1801ee790: local_aa78 = 0x101; FUN_1801e8c30: value 1 or 2 trims entity 1, 1 or 3 entity 2) */
static V3 sp_ble_p[2];
static double sp_dyn_t; static double sp_dyn_x, sp_dyn_y; static int sp_dyn_on;   /* dyno_curve: the running parameter and the last mouse position (user units in the view) */

/* the point and unit tangent of an entity at t (eval_curve 1; FUN_18000e720) */
static int sp_ent_ptan(int e, double t, V3 *P, double T[3]) {
    double p[3], d[3]; if (!sp_ev(e, t, p, d)) return 0;
    P->x = p[0]; P->y = p[1]; P->z = p[2];
    double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (l == 0) T[0] = T[1] = T[2] = 0; else { T[0] = d[0] / l; T[1] = d[1] / l; T[2] = d[2] / l; }
    return 1;
}
/* the blended spline: FUN_1801ee220: T0 / T1 reversed when (P1 - P0) . Ti < 0 (again when the magnitude is negative), make_one_segment_nurb (NURBS) / _spline (parametric): the end tangents |m| chord x T when |m| >= 0.01, else the chord; pt_tan_to_sp_coefs(.., 1.0). In 2D construction the result is flattened to the construction depth (squash_nurbs_curve; a parametric spline: DEVIATION approved by the user, N9-26: squash_spline reads uninitialised memory, see mcsf_squash_spline) */
static void sp_ble_build(void) {
    double tol = mc_systol.tol, T0[3], T1[3]; V3 P0, P1;
    if (!sp_ent_ptan(sp_ble_e[0], sp_ble_t[0], &P0, T0) || !sp_ent_ptan(sp_ble_e[1], sp_ble_t[1], &P1, T1)) return;
    double D[3] = { P1.x - P0.x, P1.y - P0.y, P1.z - P0.z };
    sp_ble_p[0] = P0; sp_ble_p[1] = P1;
    int f0 = D[0] * T0[0] + D[1] * T0[1] + D[2] * T0[2] < 0, f1 = D[0] * T1[0] + D[1] * T1[1] + D[2] * T1[2] < 0;
    if (sp_mag[0] < 0) f0 = !f0;
    if (sp_mag[1] < 0) f1 = !f1;
    sp_ble_flip[0] = f0; sp_ble_flip[1] = f1;
    double t0[3], t1[3]; for (int k = 0; k < 3; k++) { t0[k] = f0 ? -T0[k] : T0[k]; t1[k] = f1 ? -T1[k] : T1[k]; }
    double a0[3] = { P0.x, P0.y, P0.z }, a1[3] = { P1.x, P1.y, P1.z };
    double d = sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
    if (d < tol) return;
    double m0[3], m1[3], ma = fabs(sp_mag[0]), mb = fabs(sp_mag[1]);
    if (ma >= 0.01) { for (int k = 0; k < 3; k++) m0[k] = ma * d * t0[k]; } else { m0[0] = D[0]; m0[1] = D[1]; m0[2] = D[2]; }
    if (mb >= 0.01) { for (int k = 0; k < 3; k++) m1[k] = mb * d * t1[k]; } else { m1[0] = D[0]; m1[1] = D[1]; m1[2] = D[2]; }
    double c[12]; mcsf_pt_tan_coefs(a0, a1, m0, m1, 1.0, c);
    uint32_t rn = 0; uint8_t *rec = NULL;
    if (sp_nurbs_cfg()) {
        double K[8] = { 0, 0, 0, 0, 1, 1, 1, 1 }, CP[12];
        for (int k = 0; k < 3; k++) { CP[k] = c[k]; CP[3 + k] = c[3 + k] / 3.0 + c[k]; CP[6 + k] = (c[3 + k] * 2.0 + c[6 + k]) / 3.0 + c[k]; CP[9 + k] = c[3 + k] + c[k] + c[6 + k] + c[9 + k]; }
        if (!cfg_constr_3d) { double n[3], dp; sp_constr_plane(n, &dp); for (int i = 0; i < 4; i++) { double h = CP[3 * i] * n[0] + CP[3 * i + 1] * n[1] + CP[3 * i + 2] * n[2] - dp; for (int k = 0; k < 3; k++) CP[3 * i + k] -= h * n[k]; } }   /* squash_nurbs_curve */
        McsfNb o = { 4, 8, 3, 3, 0, 0, K, CP }; rec = mcsf_nurbs_rec(&o, tol, &rn);
    } else {
        rec = mcsf_spline_rec(c, 1, a0, a1, 0, tol, &rn);
        if (rec && !cfg_constr_3d) { double n[3], dp; sp_constr_plane(n, &dp); mcsf_squash_spline(rec, n, dp); }   /* DEVIATION approved by the user, N9-26: squash_spline's intent, not its broken coefficient loop */
    }
    if (rec) { sp_livent = mcam_new_curve(&F, rec, rn, geo_level, cfg_live_color); free(rec); if (sp_livent >= 0) { attr_new_ent(sp_livent); doc_dirty = 1; } }
    overlay_dirty = 1;
}
/* FUN_1801efa00: entity 1: flag set: the start side, entity 2: flag clear: the start side: a line's end point; an arc's a0 / sweep (t a fraction); a spline's +0x98 / +0xa0 and end point. rewrite_ent in place: ours a new record taking the id (mod_pair).
   MASK: bit 0 entity 1's trim, bit 1 entity 2's; both trims of one entity picked twice go on the same record (FUN_1801efa00: when the eptrs are equal and entity 1 was trimmed, param_5 = the updated param_4) */
static void sp_ble_trim_ent(int e, int mask) {
    if (e < 0 || e >= F.nents) return;
    Ent en = F.ents[e]; int k = -1;
    uint8_t *cp = NULL; uint32_t n = 0;
    if (en.type == E_SPLINE && en.crv >= 0) { CurveRec *cr = &F.crvs[en.crv]; n = cr->n; cp = malloc(n); memcpy(cp, cr->rec, n); }
    double a0 = en.a0, sw = en.sw; V3 ea = en.a, eb = en.b;
    for (int s = 0; s < 2; s++) { if (!(mask >> s & 1)) continue;
        double t = sp_ble_t[s]; V3 pos = sp_ble_p[s]; int fl = s == 0 ? sp_ble_flip[0] : !sp_ble_flip[1];
        if (en.type == E_LINE) { if (fl) ea = pos; else eb = pos; }
        else if (en.type == E_ARC) { if (fl) { a0 = a0 + sw * t; sw = sw * (1.0 - t); } else sw = sw * t; }
        else if (cp) { double pt[3] = { pos.x, pos.y, pos.z }; if (fl) { memcpy(cp + 0x98, &t, 8); memcpy(cp + 0x68, pt, 24); } else { memcpy(cp + 0xa0, &t, 8); memcpy(cp + 0x80, pt, 24); } }
    }
    if (en.type == E_LINE) { mod_del(e); k = mcam_new_ent(&F, E_LINE, ea, eb, en.c, 0, 0, 0, en.level, en.color); }
    else if (en.type == E_ARC) { mod_del(e); k = en.vw ? mcam_new_arc_view_w(&F, en.vw, en.vm, en.c, en.r, a0, sw, en.level, en.color) : mcam_new_ent(&F, E_ARC, en.a, en.b, en.c, en.r, a0, sw, en.level, en.color); }
    else if (cp) { mod_del(e); k = mcam_new_curve(&F, cp, n, en.level, en.color); free(cp); }
    if (k >= 0) { F.ents[k].ln_style = en.ln_style; F.ents[k].width = en.width; if (F.ents[k].first_poly < F.npolys) F.polys[F.ents[k].first_poly].ln_style = (uint8_t)en.ln_style; mod_pair(e, k); }
}
static void sp_ble_make(void) {                                 /* UndoTransStart; FUN_1801ee220 + FUN_1801efa00; sktcher_live_color */
    mod_begin(); sp_livent = -1; sp_ble_build();
    if (sp_livent < 0) { urec_free(&mod_rec); mod_from = -1; memset(&mod_rec, 0, sizeof mod_rec); return; }
    int t1 = sp_trim == 1 || sp_trim == 2, t2 = sp_trim == 1 || sp_trim == 3;   /* FUN_1801e8c30 */
    if (t1 && t2 && sp_ble_e[0] == sp_ble_e[1]) sp_ble_trim_ent(sp_ble_e[0], 3);
    else { if (t1) sp_ble_trim_ent(sp_ble_e[0], 1); if (t2) sp_ble_trim_ent(sp_ble_e[1], 2); }
    sp_ble_live = 1;
}
static void sp_ble_commit(void) {                               /* sktcher_main_color + UndoTransCommit */
    if (!sp_ble_live) return;
    sp_set_color(sp_livent, geo_color); mod_end(); sp_ble_live = 0; sp_livent = -1;
}
static void sp_ble_rollback(void) {                             /* FUN_180227820 + the entities' copies rewritten (local_7bf8 / local_a538) */
    if (!sp_ble_live) return;
    for (int i = mod_from; i >= 0 && i < F.nents; i++) ent_off(i);
    for (int k = 0; k < mod_rec.nd; k++) ent_on(mod_rec.dents[k], mod_rec.dtypes[k]);
    mod_unpair();                                                /* the chains mod_pair moved to the trimmed copies go back (rewrite_ent never moved them) */
    urec_free(&mod_rec); mod_from = -1; memset(&mod_rec, 0, sizeof mod_rec); sp_ble_live = 0; sp_livent = -1; scene_reupload(); overlay_dirty = 1;
}
/* dyno_curve's mouse move (0x180320530 l.0x180320c98): t += (dmouse . T2d) / |T2d|^2 with T2d the tangent at t in the view (proj_iso_pt), wrapped on a closed curve, else clamped */
static void sp_dyn_motion(int e) {
    if (!cui_ptr_ok) return;
    double wpp = scene_view_height() / (vp_h > 0 ? vp_h : 1), X = cui_ptr_x * wpp, Y = -cui_ptr_y * wpp;
    if (!sp_dyn_on) { sp_dyn_x = X; sp_dyn_y = Y; sp_dyn_on = 1; return; }
    double dx = X - sp_dyn_x, dy = Y - sp_dyn_y; sp_dyn_x = X; sp_dyn_y = Y;
    if (!(mc_systol.tol < fabs(dx) + fabs(dy))) return;
    double P[3], D[3], t1, t2; if (!sp_ev(e, sp_dyn_t, P, D)) return; sp_t1t2(e, &t1, &t2);
    double tx = vmat[0] * D[0] + vmat[1] * D[1] + vmat[2] * D[2], ty = vmat[3] * D[0] + vmat[4] * D[1] + vmat[5] * D[2], l = tx * tx + ty * ty, t = sp_dyn_t;
    if (1e-12 < l) t += (ty * dy + dx * tx) / l;
    double Pa[3], Pb[3], A[3]; int closed = 0;
    if (sp_ev(e, t1, Pa, A) && sp_ev(e, t2, Pb, A)) closed = sqrt((Pb[0] - Pa[0]) * (Pb[0] - Pa[0]) + (Pb[1] - Pa[1]) * (Pb[1] - Pa[1]) + (Pb[2] - Pa[2]) * (Pb[2] - Pa[2])) < mc_systol.tol;
    if (t < t1) t = closed ? t + (t2 - t1) : t1; else if (t2 < t) t = closed ? t - (t2 - t1) : t2;
    sp_dyn_t = t; overlay_dirty = 1;
}
void sp_move(void) { if (geo_cmd == G_SPLBLEND && (sp_ble_state == 1 || sp_ble_state == 3)) sp_dyn_motion(sp_ble_e[sp_ble_state / 2]); }

/* ---- the state of the family's panel: one prompt per command (mccadres) */
static const char *sp_prompt_str(void) {
    switch (geo_cmd) {
    case G_SPLMAN: return sp_in_edit ? "Adjust end conditions. Press <ENTER> or <APPLY> when done." : "Select points. Press <Enter> or <Apply> when done.";   /* 5868 / 5869 */
    case G_SPLAUTO: return sp_auto_n == 0 ? "Select the first point" : sp_auto_n == 1 ? "Select the second point" : "Select the last point";     /* 5871-5873 */
    case G_SPLBLEND: return sp_ble_state == 0 ? "Select curve 1 " : sp_ble_state == 2 ? "Select curve 2 " : "Slide the arrow and click the tangent position on the curve";   /* 5497-5499 */
    case G_UNTRIM: return "Select spline and NURBS curves to untrim. Press ENTER to end selection";   /* mccadutilres 449 */
    case G_X2NURBS: return "Select line, arc, spline, or surface to convert to NURBS";                /* mccadutilres 2404 */
    case G_CSURF: return "Select curves to convert to surface curves";                                 /* mccadres 5967 (0x174f, sketcher_curve_6) */
    case G_MODIFYSP: return sp_mod_e < 0 ? "Select a spline or surface" : sp_mod_cp < 0 ? (F.ents[sp_mod_e].cls == 8 ? "Select a node.  Press <Enter> when done." : "Select a control point.  Press <Enter> when done.") : (F.ents[sp_mod_e].cls == 8 ? "Drag to new position, click to stop" : "Drag to new position and click to stop");   /* mccoreres 7414, 7415 / mccadres 7417 + ".  " + 8924, 0x1412 = 5138, mccadres 5102 */
    case G_SIMPL: return "Select splines to simplify";                                                 /* mccadres 8925 */
    case G_REFIT: return "Select splines to refit";                                                   /* mccadutilres 12 */
    default: return "";
    }
}
const char *sp_prompt_text(void) { return sp_prompt_str(); }

/* ---- Untrim Spline (mccadutil UntrimSpline 0x18012b6a0): every selected spline / NURBS: t1 / t2 back to the full range, the end points evaluated there, rewrite (one undo step) */
static int sp_ent_kept_type(int e) { return F.ents[e].type == E_SPLINE && F.ents[e].crv >= 0 && F.ents[e].crv < F.ncrvs; }
static void sp_untrim_one(int e) {
    CurveRec *cr = &F.crvs[F.ents[e].crv];
    uint8_t *r = cr->rec; double t1, t2, u1, u2;
    memcpy(&t1, r + 0x98, 8); memcpy(&t2, r + 0xa0, 8);
    if (cr->cls == 8) { u1 = 0; { int32_t ns; memcpy(&ns, r + 0xb0, 4); u2 = (double)ns; } }
    else { int32_t nk; memcpy(&nk, r + 0xc8, 4); memcpy(&u1, r + 0xd8, 8); memcpy(&u2, r + 0xd8 + 8 * (nk - 1), 8); }
    if (!(fabs(u1 - t1) > 1e-12) && !(fabs(u2 - t2) > 1e-12)) return;      /* the range as it is: no change */
    int level = F.ents[e].level, color = F.ents[e].color, st = F.ents[e].ln_style; double wd = F.ents[e].width;
    uint8_t *cp = malloc(cr->n); memcpy(cp, r, cr->n); uint32_t n = cr->n;
    memcpy(cp + 0x98, &u1, 8); memcpy(cp + 0xa0, &u2, 8);
    CurveRec tc = { 0, cr->cls, cp, n }; double d[3][3];
    if (!mcs_spline_eval_d(&tc, u1, 0, d)) { for (int k = 0; k < 3; k++) memcpy(cp + 0x68 + 8 * k, &d[0][k], 8); }
    if (!mcs_spline_eval_d(&tc, u2, 0, d)) { for (int k = 0; k < 3; k++) memcpy(cp + 0x80 + 8 * k, &d[0][k], 8); }
    mod_del(e);
    int k = mcam_new_curve(&F, cp, n, level, color); free(cp);
    if (k >= 0) { F.ents[k].ln_style = st; F.ents[k].width = wd; F.polys[F.ents[k].first_poly].ln_style = (uint8_t)st; mod_pair(e, k); }   /* rewrite_ent: same entity: the new record takes the id and the chains */
}
static void sp_untrim_run(void) {
    int *l; int n = sel_list(&l, 0);
    mod_begin(); int ch = 0;
    for (int i = 0; i < n; i++) if (sp_ent_kept_type(l[i])) { sp_untrim_one(l[i]); ch = 1; }
    mod_end(); free(l); sel_clear();
    if (ch) doc_dirty = 1;
    snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str()); overlay_dirty = 1;
}

/* ---- Spline Convert to NURBS (mccadutil x_2_nurbs -> convert_to_nurbs -> convert_entity_to_nurbs): the selected lines, arcs, parametric splines (mask 0x1f1fff select_ents; get_ent mask 0x10e with surfaces when the
   product allows) replaced by NURBS curves / surfaces (put_nurbs_in_db / put_surf_in_db with the entity's colour / level); the original: delete_ent(.., blank = ent +0x28 >= 1, ..), put_special_undo(blank ? 1 : 2) (ent +0x28 = ours: sp_used_by_ops, as Refit); its references: tp_substitute_ent + DfReAssocNE (mod_pair, then a new id).
   The Group Manager part of convert_to_nurbs (eptrs_from_sel, TpGrpMgr::GetMainGrpList, TpGrpList::GetGroupEptrs / IsEptrInGroup, list_grp_append_ents: a converted entity joins its original's groups) is NOT PORTED: this app has no Mastercam entity groups. */
static int sp_used_by_ops(int e);
static void sp_x2n_orig(int e, int used) { if (used) mod_blank(e); else mod_del(e); }   /* convert_to_nurbs: put_special_undo + delete_ent(.., !bVar16, ..) */
static void sp_x2n_refs(int e, int k) { int id = F.ents[k].idn; mod_pair(e, k); F.ents[k].idn = id; }   /* tp_substitute_ent(.., 1, 1, ..) + DfReAssocNE: the references, a new id */
static void sp_x2n_line(int e) {
    Ent en = F.ents[e];
    double K[4] = { 0, 0, 1, 1 }, CP[6] = { en.a.x, en.a.y, en.a.z, en.b.x, en.b.y, en.b.z };
    McsfNb o = { 2, 4, 1, 3, 0, 0, K, CP };          /* convert_line_to_nb: 0x301: degree 1, 3 values, knots 0 0 1 1, flags 0x14 (bit 2: non-rational) */
    uint32_t n; uint8_t *r = mcsf_nurbs_rec(&o, mc_systol.tol, &n);
    if (!r) return;
    r[0xd2] = 0x14;                                  /* convert_line_to_nb: | 0x14 ... */
    int lv = en.level, co = en.color, used = sp_used_by_ops(e); sp_x2n_orig(e, used);
    int k = mcam_new_curve(&F, r, n, lv, co); free(r);
    if (k >= 0) { F.ents[k].ln_style = en.ln_style; F.ents[k].width = en.width; sp_x2n_refs(e, k); }
}
static void sp_x2n_arc(int e) {
    Ent en = F.ents[e]; uint32_t n; uint8_t *r = mcsb_arc_nurbs_rec(&F, &F.ents[e], mc_systol.tol, &n);
    if (!r) return;
    int lv = en.level, co = en.color, used = sp_used_by_ops(e); sp_x2n_orig(e, used);
    int k = mcam_new_curve(&F, r, n, lv, co); free(r);
    if (k >= 0) { F.ents[k].ln_style = en.ln_style; F.ents[k].width = en.width; sp_x2n_refs(e, k); }
}
static void sp_x2n_run(void) {
    int *l; int n = sel_list(&l, 0);
    mod_begin(); int done = 0, spline = 0;
    for (int i = 0; i < n; i++) {
        int e = l[i], t = F.ents[e].type;
        if (t == E_LINE) { sp_x2n_line(e); done = 1; }
        else if (t == E_ARC) { sp_x2n_arc(e); done = 1; }
        else if (t == E_SPLINE && F.ents[e].cls == 8 && F.ents[e].crv >= 0) {
            /* convert_entity_to_nurbs: the parametric spline */
            const CurveRec *cr = &F.crvs[F.ents[e].crv]; double t1, t2; int32_t ns; memcpy(&t1, cr->rec + 0x98, 8); memcpy(&t2, cr->rec + 0xa0, 8); memcpy(&ns, cr->rec + 0xb0, 4);
            uint32_t rn; uint8_t *r = mcsf_spline_to_nurbs(cr->rec, cr->n, mc_systol.tol, &rn);
            if (r) {                                         /* convert_entity_to_nurbs: t1 / t2 = the first / last knot; a trimmed end: proj_pt_on_ent(the NURBS, the stored end, 0, the end knot) gives t (always) and the end point (when found) */
                CurveRec tc = { 0, 0x80, r, rn }; int32_t nk; memcpy(&nk, r + 0xc8, 4); double kf, kl, u, q[3], p0[3]; int ok;
                memcpy(&kf, r + 0xd8, 8); memcpy(&kl, r + 0xd8 + 8 * (size_t)(nk - 1), 8);
                memcpy(r + 0x98, &kf, 8); memcpy(r + 0xa0, &kl, 8);
                if (fabs(t1) > 1e-6) { memcpy(p0, cr->rec + 0x68, 24); sp_proj_core(-1, &tc, NULL, p0, kf, q, &u, &ok); memcpy(r + 0x98, &u, 8); if (ok) memcpy(r + 0x68, q, 24); }
                if (fabs(t2 - ns) > 1e-6) { memcpy(p0, cr->rec + 0x80, 24); sp_proj_core(-1, &tc, NULL, p0, kl, q, &u, &ok); memcpy(r + 0xa0, &u, 8); if (ok) memcpy(r + 0x80, q, 24); }
            }
            if (r) { int lv = F.ents[e].level, co = F.ents[e].color, st = F.ents[e].ln_style, used = sp_used_by_ops(e); double wd = F.ents[e].width; sp_x2n_orig(e, used); int k = mcam_new_curve(&F, r, rn, lv, co); free(r); if (k >= 0) { F.ents[k].ln_style = st; F.ents[k].width = wd; sp_x2n_refs(e, k); } done = 1; }
        }
    }
    /* convert_to_nurbs, surfaces (get_ent mask 0x10e): mcstn_rule 0: only redrawn; else create_nurbsurf_from_surf + put_surf_in_db on the original's colour / level, the original deleted (tp_substitute_ent: the toolpath references: mod_pair, a new id) */
    int *sl, ns = sel_list_surfs(&sl), refused = 0; const char *swhy = NULL;
    for (int i = 0; i < ns; i++) {
        int e = sl[i], si = F.ents[e].srf; if (si < 0 || si >= F.nsurfs || !mcstn_rule(&F, &F.surfs[si])) continue;
        uint32_t rn = 0; const char *why = NULL; uint8_t *r = mcstn_convert(&F, &F.surfs[si], solid_mode != 0, mc_systol.tol, &rn, &why);
        if (!r) {                                               /* kinds 8 / 10 on a non-NURBS root: create_nurbsurf_from_surf: the new NURBS base (blanked, main colour / level), then make_new_trim's rebuild on it; the source deleted (delete_ent also deletes its old base when blanked, ent +0x28 < 2, +0x38 < 1) */
            int shaded = solid_mode != 0, lv = F.ents[e].level, co = F.ents[e].color; uint32_t bn = 0, tn = 0;
            uint8_t *b = mcstn_base(&F, &F.surfs[si], shaded, mc_systol.tol, &bn, &why);
            if (!b) { refused++; swhy = why; continue; }
            int bcol = geo_color != cfg_back_color ? srf_front_color : geo_color;   /* put_surf_in_db: the main colour = surfaceFrontColor (as srf_faces_make) */
            const Surf *ob = mcam_surf_base(&F, &F.surfs[si]); int obe = ob ? ob->ent : -1;
            int be = mcam_new_surf(&F, b, bn, geo_level, bcol); free(b);
            if (be < 0) { refused++; continue; }
            Surf *ns = &F.surfs[F.ents[be].srf]; ns->blanked = 1; F.ents[be].blanked = 1;
            uint8_t *t = mcstn_rebuild(&F, &F.surfs[F.ents[e].srf], F.ents[be].septr, F.surfs[F.ents[be].srf].rec, bn, shaded, mc_systol.tol, &tn, &why);
            if (!t) { ent_off(be); refused++; swhy = why; continue; }
            int k = mcam_new_surf(&F, t, tn, lv, co); free(t);
            sp_x2n_orig(e, sp_used_by_ops(e));
            if (obe >= 0 && obe < F.nents && F.ents[obe].type == E_SURF && F.ents[obe].blanked) { int16_t r28, r38; const uint8_t *orec = F.surfs[F.ents[obe].srf].rec; memcpy(&r28, orec + 0x28, 2); memcpy(&r38, orec + 0x38, 2); if (r28 < 2 && r38 < 1) mod_del(obe); }
            if (k >= 0) { int id = F.ents[k].idn; mod_pair(e, k); F.ents[k].idn = id; done = 1; }
            continue;
        }
        int lv = F.ents[e].level, co = F.ents[e].color; sp_x2n_orig(e, sp_used_by_ops(e));
        int k = mcam_new_surf(&F, r, rn, lv, co); free(r);
        if (k >= 0) { int id = F.ents[k].idn; mod_pair(e, k); F.ents[k].idn = id; done = 1; }
    }
    free(sl);
    if (ns) scene_surfs_update(0);
    mod_end(); free(l); sel_clear();
    if (done) doc_dirty = 1;
    (void)spline; if (refused) snprintf(cam_msg, sizeof cam_msg, "Convert to NURBS: %d surface(s) refused: %s", refused, swhy ? swhy : ""); else snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str());
    overlay_dirty = 1;
}

/* ---- Modify Spline (mccadutil cpts_nurbs 0x180057530 -> modify_nurbspline_process 0x180055c20, a NURBS curve): prompt 7414 picks the curve (mask 0x188: spline, NURBS, surface), prompt 7415 + 8924
   "Select a control point" / "Press <Enter> when done." pick a control point (FUN_1800554d0: the nearest by the view distance of the projected control points), the rubber band (0x1d, NOT DECODED), the next point
   (get_event mask 9) replaces it (FUN_180056530: rational: homogeneous), then FUN_180056e10: the curvature limit, the end points, the closed flag, rewrite. Each move is its own undo step (UndoXformTransStart / Commit,
   translate_silent's copy replaced here by a new entity with the changed record, passReferences: mod_pair, a new id). Enter ends the command (cpts_nurbs returns). Parametric splines: sketcher_spline_5 below. Surfaces (modify_nurbsurf_process): NOT PORTED, refused with a message. */
static void sp_mod_prompt(void) { int p8 = sp_mod_e >= 0 && F.ents[sp_mod_e].cls == 8; snprintf(cam_msg, sizeof cam_msg, "%s", sp_mod_e < 0 ? "Select a spline or surface" : sp_mod_cp < 0 ? (p8 ? "Select a node.  Press <Enter> when done." : "Select a control point.  Press <Enter> when done.") : (p8 ? "Drag to new position, click to stop" : "Drag to new position and click to stop")); overlay_dirty = 1; }   /* 7415 / 7417 + ".  " (DAT_18017e6b4 mccadutil, DAT_180493670 mccad) + 8924 */
/* proj_iso_pt: a world point in the view's 2D coordinates (the view matrix's first two rows, as sp_dyn_motion) */
static void sp_view2d(double x, double y, double z, double *u, double *v) { *u = vmat[0] * x + vmat[1] * y + vmat[2] * z; *v = vmat[3] * x + vmat[4] * y + vmat[5] * z; }
static void sp_mod_set_cp(int e, int icp, V3 p) {
    CurveRec *cr = &F.crvs[F.ents[e].crv]; uint32_t n = cr->n; uint8_t *r = malloc(n); memcpy(r, cr->rec, n);
    int32_t nk, ncp; memcpy(&nk, r + 0xc8, 4); memcpy(&ncp, r + 0xcc, 4); int vpp = r[0xd1], rat = !((r[0xd2] >> 2) & 1);
    double *C = (double *)(r + 0xd8 + 8 * (size_t)nk);
    double *c = C + vpp * icp, xyz[3] = { p.x, p.y, p.z };
    if (rat) { double w = c[0]; c[1] = xyz[0] * w; c[2] = xyz[1] * w; c[3] = xyz[2] * w; } else { c[0] = xyz[0]; c[1] = xyz[1]; c[2] = xyz[2]; }
    /* FUN_180056e10: the curvature limit as put_nurbs_in_db, the end points eval_nurbs at t1 / t2, the closed bit 1 of +0xd2 */
    CurveRec tc = { 0, 0x80, r, n }; double t1, t2, a[3], b[3], d[3][3]; memcpy(&t1, r + 0x98, 8); memcpy(&t2, r + 0xa0, 8);
    if (!mcs_spline_eval_d(&tc, t1, 0, d)) { memcpy(a, d[0], 24); memcpy(r + 0x68, a, 24); }
    if (!mcs_spline_eval_d(&tc, t2, 0, d)) { memcpy(b, d[0], 24); memcpy(r + 0x80, b, 24); }
    double cl = sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
    r[0xd2] = (uint8_t)((r[0xd2] & ~2) | (cl < mc_systol.tol ? 2 : 0));
    double cmax = 1e-9; int deg = r[0xd0];
    if (deg >= 2) { double kf, kl; memcpy(&kf, r + 0xd8, 8); memcpy(&kl, r + 0xd8 + 8 * (size_t)(nk - 1), 8); double dt = (t2 - t1) / (double)(ncp * 2 - 2), s = (kl - kf) / (t2 - t1); cmax = 0.0;
        for (double tt = t1; tt <= t2; tt += dt) { double o[3][3]; if (mcs_spline_eval_d(&tc, tt, 2, o)) continue;
            double aa[3] = { o[1][0] * s, o[1][1] * s, o[1][2] * s }, bb[3] = { o[2][0] * s * s, o[2][1] * s * s, o[2][2] * s * s };
            double cc[3] = { aa[1] * bb[2] - aa[2] * bb[1], aa[2] * bb[0] - aa[0] * bb[2], aa[0] * bb[1] - aa[1] * bb[0] };
            double la = sqrt(aa[0] * aa[0] + aa[1] * aa[1] + aa[2] * aa[2]), k = la > 1e-6 ? sqrt(cc[0] * cc[0] + cc[1] * cc[1] + cc[2] * cc[2]) / la : 0; if (cmax < k && k < 1e9) cmax = k; }
        if (cmax < 1e-9) cmax = 1e-9;
        if (cmax > 1e9) cmax = 1e9; }
    memcpy(r + 0xa8, &cmax, 8);
    int level = F.ents[e].level, color = F.ents[e].color, st = F.ents[e].ln_style; double wd = F.ents[e].width;
    mod_begin(); mod_del(e); int k = mcam_new_curve(&F, r, n, level, color); if (k >= 0) { int id = F.ents[k].idn; F.ents[k].ln_style = st; F.ents[k].width = wd; F.polys[F.ents[k].first_poly].ln_style = (uint8_t)st; mod_pair(e, k); F.ents[k].idn = id; } mod_end();   /* modify_nurbspline_process: translate_silent's copy, passReferences, delete_ent: the references, a new id */
    free(r); sp_mod_e = k; doc_dirty = 1; overlay_dirty = 1;
}
/* ---- Modify Spline, a parametric spline (sketcher_spline_5, FUN_18029e630): the panel's Tangency Endpoints keep the end tangents; the nodes = the curve at t = 0 .. nseg (FUN_18029d850); pick a node (FUN_18029e120, prompts 7417 + 8924), drag (FUN_18029db40, prompt 5102, rubber band 0x21: the moved node keeps its depth in the construction view, FUN_18033d520), then recalc_spline (mccadutil 0x180057080: make_spline_coefs_open / _closed from the nodes, curvature limit at steps 0.1 / order, ends at t1 / t2, the closed flag) on a copy (translate_silent, passReferences, delete_ent: FUN_18029e500); UndoXformTransCommit per move */
static void sp_mod_param_start(void) {
    const CurveRec *cr = &F.crvs[F.ents[sp_mod_e].crv]; int32_t ns; memcpy(&ns, cr->rec + 0xb0, 4); double t1, t2, o[3][3];
    sp_grow(ns + 1); sp_nn = ns + 1;
    for (int i = 0; i <= ns; i++) { if (!mcs_spline_eval_d(cr, (double)i, 0, o)) { sp_n[i].x = o[0][0]; sp_n[i].y = o[0][1]; sp_n[i].z = o[0][2]; } }
    mcs_spline_t1t2(cr, &t1, &t2);
    for (int j = 0; j < 2; j++) { if (!mcs_spline_eval_d(cr, j ? t2 : t1, 1, o)) { double l = sqrt(o[1][0] * o[1][0] + o[1][1] * o[1][1] + o[1][2] * o[1][2]); for (int k = 0; k < 3; k++) sp_mod_tan[j][k] = l == 0 ? 0 : o[1][k] / l; } }
}
static void sp_mod_param_recalc(void) {                         /* FUN_18029d920 + recalc_spline */
    int e = sp_mod_e; const CurveRec *cr = &F.crvs[F.ents[e].crv]; uint32_t n = cr->n; uint8_t *r = malloc(n); memcpy(r, cr->rec, n);
    double (*Q)[3] = malloc(sizeof(double) * 3 * (size_t)sp_nn), tol = mc_systol.tol, *C = (double *)(r + 0xc0);
    for (int i = 0; i < sp_nn; i++) { Q[i][0] = sp_n[i].x; Q[i][1] = sp_n[i].y; Q[i][2] = sp_n[i].z; }
    double cl = sqrt((Q[sp_nn - 1][0] - Q[0][0]) * (Q[sp_nn - 1][0] - Q[0][0]) + (Q[sp_nn - 1][1] - Q[0][1]) * (Q[sp_nn - 1][1] - Q[0][1]) + (Q[sp_nn - 1][2] - Q[0][2]) * (Q[sp_nn - 1][2] - Q[0][2]));
    double z[3] = { 0, 0, 0 }; int ok = tol <= cl ? mcsf_coefs_open(C, (const double (*)[3])Q, sp_nn, sp_mod_ep[0] ? 2 : 0, sp_mod_ep[0] ? sp_mod_tan[0] : z, sp_mod_ep[1] ? 2 : 0, sp_mod_ep[1] ? sp_mod_tan[1] : z, tol) : mcsf_coefs_closed(C, (const double (*)[3])Q, sp_nn, tol);
    free(Q);
    if (ok) {
        mcsf_spline_finish(r, n, 0.1, tol);
        r[0xb7] = cl < tol;
        CurveRec tc = { 0, 8, r, n }; double t1, t2, o[3][3]; mcs_spline_t1t2(&tc, &t1, &t2);
        if (!mcs_spline_eval_d(&tc, t1, 0, o)) memcpy(r + 0x68, o[0], 24);
        if (!mcs_spline_eval_d(&tc, t2, 0, o)) memcpy(r + 0x80, o[0], 24);
        Ent en = F.ents[e];
        mod_begin(); mod_del(e); int k = mcam_new_curve(&F, r, n, en.level, en.color);
        if (k >= 0) { int id = F.ents[k].idn; F.ents[k].ln_style = en.ln_style; F.ents[k].width = en.width; F.polys[F.ents[k].first_poly].ln_style = (uint8_t)en.ln_style; mod_pair(e, k); F.ents[k].idn = id; sp_mod_e = k; }   /* passReferences: the references, a new id */
        mod_end(); doc_dirty = 1;
    }
    free(r); overlay_dirty = 1;
}
static V3 sp_mod_drag_pos(V3 p, int i) {                        /* FUN_18033d520: 2D construction: the cursor in the Cplane with the node's depth (rubber_band +0x1b8); 3D: the event point */
    if (cfg_constr_3d) return p;
    double zero[3] = { 0, 0, 0 }, w[3] = { p.x, p.y, p.z }, v[3], nw[3] = { sp_n[i].x, sp_n[i].y, sp_n[i].z }, nv[3], out[3];
    plane_w2v(plane_active[0], zero, w, v); plane_w2v(plane_active[0], zero, nw, nv); v[2] = nv[2]; plane_v2w(plane_active[0], zero, v, out);
    V3 r = { out[0], out[1], out[2] }; return r;
}
static void sp_mod_pick(int e, V3 w) {
    if (sp_mod_e < 0) {
        if (e >= 0 && F.ents[e].type == E_SURF) { snprintf(cam_msg, sizeof cam_msg, "Modify Spline: a surface (modify_nurbsurf_process) is NOT PORTED"); overlay_dirty = 1; return; }   /* cpts_nurbs: mask 0x188 */
        if (e < 0 || F.ents[e].type != E_SPLINE) return;
        sp_mod_e = e; sp_mod_cp = -1;
        if (F.ents[e].cls == 8) sp_mod_param_start();           /* sketcher_spline_5 */
        sp_mod_prompt(); return;
    }
    double wu, wv; sp_view2d(w.x, w.y, w.z, &wu, &wv);          /* the click in the view's 2D coordinates (FUN_18029d650 / FUN_1800554d0: param 1: proj_iso_pt) */
    if (F.ents[sp_mod_e].cls == 8) {                           /* FUN_18029e120: the nearest node (FUN_18029d650: the view distance) */
        double bd = 1e300; int bi = 0;
        for (int i = 0; i < sp_nn; i++) { double u, v; sp_view2d(sp_n[i].x, sp_n[i].y, sp_n[i].z, &u, &v); double dd = (u - wu) * (u - wu) + (v - wv) * (v - wv); if (dd < bd) { bd = dd; bi = i; } }
        sp_mod_cp = bi; sp_mod_prompt(); return;
    }
    if (sp_mod_cp < 0) {                                       /* FUN_1800554d0: the nearest control point (projected: the view distance) */
        CurveRec *cr = &F.crvs[F.ents[sp_mod_e].crv]; uint8_t *r = cr->rec; int32_t nk, ncp; memcpy(&nk, r + 0xc8, 4); memcpy(&ncp, r + 0xcc, 4); int vpp = r[0xd1], rat = !((r[0xd2] >> 2) & 1);
        double *C = (double *)(r + 0xd8 + 8 * (size_t)nk); double bd = 1e300; int bi = -1;
        for (int i = 0; i < ncp; i++) { double *c = C + vpp * i, x, y, z, u, v; if (rat) { x = c[1] / c[0]; y = c[2] / c[0]; z = c[3] / c[0]; } else { x = c[0]; y = c[1]; z = c[2]; } sp_view2d(x, y, z, &u, &v); double dd = (u - wu) * (u - wu) + (v - wv) * (v - wv); if (dd < bd) { bd = dd; bi = i; } }
        sp_mod_cp = bi; sp_mod_prompt(); return;
    }
}

/* ---- Simplify Spline (mccad simplify FUN_180292940, FUN_180293490, FUN_1802926a0; engine mccadutil convert_ent_to_line_or_arc: mcspline_simplify.c): per selected spline / NURBS on OK / Apply: a line or an arc on the main attributes;
   the original (panel Original Spline: 0 Delete, 1 Keep, 2 Blank: delete_ent(.., blank = mode 2)); one undo step; the status text 8926 / 8927 (FUN_180293570). Panel default tolerance 0.0002 (local_2de0), mode 0.
   Ours / not decoded: ent +0x28 (> 0 also blanks instead of deleting) taken as 0; tp_substitute_ent / DfReAssocEptrs (the toolpath / associative references) as mod_pair for mode 0. */
static double sp_sim_tol = 0.0002; static int sp_sim_lock, sp_sim_mode;
static void sp_simpl_run(void) {
    int *l; int n = sel_list(&l, 0), done = 0; mod_begin();
    McsSimpView *v = calloc((size_t)F.nfviews + 1, sizeof *v);
    for (int i = 0; i < F.nfviews; i++) { v[i].n = F.fviews[i].n; memcpy(v[i].m, F.fviews[i].m, sizeof v[i].m); }
    for (int i = 0; i < n; i++) {
        int e = l[i]; if (F.ents[e].type != E_SPLINE || F.ents[e].crv < 0) continue;
        McsSimplify o; memset(&o, 0, sizeof o);
        if (!mcs_simplify_curve(&F.crvs[F.ents[e].crv], sp_sim_tol, mc_systol.tol, v, F.nfviews, &o)) continue;
        done++;
        if (sp_sim_mode == 0) mod_del(e); else if (sp_sim_mode == 2) mod_blank(e);
        int k;
        if (o.is_line) { V3 a = { o.line[0], o.line[1], o.line[2] }, b = { o.line[3], o.line[4], o.line[5] }, z = { 0, 0, 0 }; k = mcam_new_ent(&F, E_LINE, a, b, z, 0, 0, 0, geo_level, geo_color); }
        else {
            if (o.view_is_new) { F.fviews = realloc(F.fviews, sizeof *F.fviews * (size_t)(F.nfviews + 1)); F.fviews[F.nfviews].n = o.view; memcpy(F.fviews[F.nfviews].m, o.view_m, sizeof o.view_m); F.nfviews++; v = realloc(v, sizeof *v * (size_t)(F.nfviews + 1)); v[F.nfviews - 1].n = o.view; memcpy(v[F.nfviews - 1].m, o.view_m, sizeof o.view_m); }
            V3 c = { o.c[0], o.c[1], o.c[2] }; k = mcam_new_arc_view(&F, o.view, o.view_m, c, o.r, o.a0, o.sw, geo_level, geo_color);
        }
        if (k >= 0) { attr_new_ent(k); if (sp_sim_mode == 0) { int id = F.ents[k].idn; mod_pair(e, k); F.ents[k].idn = id; } }
    }
    free(v); mod_end(); free(l); sel_clear();
    if (done) doc_dirty = 1;
    if (done) snprintf(cam_msg, sizeof cam_msg, "%d splines were converted", done); else snprintf(cam_msg, sizeof cam_msg, "No splines were converted");   /* mccadres 8927 / 8926 */
    overlay_dirty = 1;
}


/* ---- entity references the delete of the originals tests: ent +0x28 > 0 (delete_ent: blanked instead when toolpaths use it; put_special_undo kinds): the toolpath references of the entity */
int sp_used_by_ops_pub(int e);
int sp_used_by_ops_pub(int e) { return sp_used_by_ops(e); }   /* Curve Surface (cui_csurf.c) */
static int sp_used_by_ops(int e) {
    for (int j = 0; j < ncamops; j++) for (int c = 0; c < camops[j]->nchains; c++) for (int k = 0; k < camops[j]->chains[c].n; k++) if (camops[j]->chains[c].ent[k] == e) return 1;
    for (int i = 0; i < F.ntp; i++) if (F.tp[i].kind == 7 && F.tp[i].ent == F.ents[e].idn) return 1;
    return 0;
}
/* ---- Spline From Curves (mccad sketcher_spline_3 FUN_1801fc2a0: chain_it(4233), panel; mccore make_nurbs_from_chains / make_spline_from_chain: mcspline_chain.c): a geo chain command (cui_geo.c geo_chain_cmd).
   Panel (FUN_1801f58e0): +0 Tolerance (0.02 mm / 0.001 in), +8 Conversion (0 Keep, 1 Blank, 2 Delete (blank when toolpaths use it), else Move to level), +0xc Level (main level), +0x11 Sharp Corner Smoothing, +0x18 Detection angle (10), +0x20 Blend distance (2.0 mm / 0.1 in). */
static struct { double tol, ang, blend; int action, level, sharp; } sp_cv;
void sp_curv_reset(void) { int mm = mcam_is_metric(&F); sp_cv.tol = mm ? 0.02 : 0.001; sp_cv.ang = 10.0; sp_cv.blend = mm ? 2.0 : 0.1; sp_cv.action = 0; sp_cv.level = geo_level; sp_cv.sharp = 0; }
static int sp_copy_level(int e, int lv) {                        /* make_nurbs_from_chains: the level rewritten in place (rewrite_ent): a new record taking the id */
    Ent en = F.ents[e]; int k = -1;
    if (en.type == E_LINE) { mod_del(e); k = mcam_new_ent(&F, E_LINE, en.a, en.b, en.c, 0, 0, 0, lv, en.color); }
    else if (en.type == E_ARC) { mod_del(e); k = en.vw ? mcam_new_arc_view_w(&F, en.vw, en.vm, en.c, en.r, en.a0, en.sw, lv, en.color) : mcam_new_ent(&F, E_ARC, en.a, en.b, en.c, en.r, en.a0, en.sw, lv, en.color); }
    else if (en.type == E_SPLINE && en.crv >= 0) { CurveRec *cr = &F.crvs[en.crv]; uint32_t n = cr->n; uint8_t *cp = malloc(n); memcpy(cp, cr->rec, n); mod_del(e); k = mcam_new_curve(&F, cp, n, lv, en.color); free(cp); }
    if (k >= 0) { F.ents[k].ln_style = en.ln_style; F.ents[k].width = en.width; mod_pair(e, k); }
    return k;
}
void sp_curv_run(void) {
    double tol = mc_systol.tol; int nurbs = sp_nurbs_cfg(), made = 0;
    mod_begin();
    for (int c = 0; c < geo_nchains; c++) {
        const Chain *ch = &geo_chains[c]; McscEnt *E = calloc((size_t)ch->n + 1, sizeof *E); int m = 0;
        for (int k = 0; k < ch->n; k++) {
            int e = ch->ent[k]; if (e < 0 || e >= F.nents || !F.ents[e].type) continue;
            const Ent *en = &F.ents[e]; McscEnt *q = &E[m];
            q->cls = en->type == E_LINE ? 2 : en->type == E_ARC ? 4 : en->type == E_SPLINE ? (int)en->cls : 0;
            q->a[0] = en->a.x; q->a[1] = en->a.y; q->a[2] = en->a.z; q->b[0] = en->b.x; q->b[1] = en->b.y; q->b[2] = en->b.z;
            if (en->type == E_ARC) { q->r = en->r; q->a0 = en->a0; q->sw = en->sw; double w[3] = { en->c.x, en->c.y, en->c.z };
                if (en->vw) { q->vm = en->vm; for (int r = 0; r < 3; r++) q->c[r] = en->vm[3 * r] * w[0] + en->vm[3 * r + 1] * w[1] + en->vm[3 * r + 2] * w[2]; } else memcpy(q->c, w, 24); }
            if (en->type == E_SPLINE && en->crv >= 0) { q->rec = F.crvs[en->crv].rec; q->n = F.crvs[en->crv].n; }
            q->reverse = ch->rev[k] != 0; m++;
        }
        uint8_t *rec = NULL; uint32_t rn = 0; double t_s = 0, t_e = 0;
        if (ch->cut0 > 0 && ch->n) { double t1, t2; sp_t1t2(ch->ent[0], &t1, &t2); t_s = t1 + (t2 - t1) * (ch->rev[0] ? 1.0 - ch->cut0 : ch->cut0); }      /* CHAIN +0x28 / +0x30: a Dynamic chain's first / last entity parameter */
        if (ch->cut1 > 0 && ch->n) { double t1, t2; sp_t1t2(ch->ent[ch->n - 1], &t1, &t2); t_e = t1 + (t2 - t1) * (ch->rev[ch->n - 1] ? ch->cut1 : 1.0 - ch->cut1); }
        McscPlane pl, *pp = NULL; if (!cfg_constr_3d) { sp_constr_plane(pl.n, &pl.depth); pp = &pl; }   /* make_*: constr_view_3d false: squash_nurbs_curve / squash_spline on the construction view's z and depth */
        int r = mcsc_chain_record(E, m, nurbs ? 0 : 1, ch->closed, t_s, t_e, sp_cv.tol, tol, pp, &rec, &rn);
        free(E);
        if (r != MCSC_OK) { if (r == MCSC_TOO_LONG) snprintf(cam_msg, sizeof cam_msg, "Spline From Curves: too many spline segments"); else if (r == MCSC_DLL_UNDEFINED) snprintf(cam_msg, sizeof cam_msg, "Spline From Curves: this chain's result is undefined in Mastercam (stacked vertices): NOT DECODED"); continue; }
        if (sp_cv.sharp) {                                        /* FUN_1801fc020: MaCompositeCurve::Refit(the new spline, tolerance, angle, blend), the result replaces it */
            McrParams P; memset(&P, 0, sizeof P); P.chord_dev = sp_cv.tol; P.angle_deg = sp_cv.ang; P.fair_dist = sp_cv.blend; P.allow_more_nodes = 1; P.break_at_sharp = 0; P.max_fair = 1e38; P.tol = tol; P.step_chord_dev = mc_systol.surf_step_chord_dev; P.metric = mcam_is_metric(&F); P.nurbs = nurbs;
            McrResult o; memset(&o, 0, sizeof o);
            if (mcr_refit(rec, rn, &P, &o) == 0 && o.nrec > 0) { free(rec); rec = malloc(o.rec[0].n); memcpy(rec, o.rec[0].rec, o.rec[0].n); rn = o.rec[0].n; mcr_free(&o); }
        }
        int k = mcam_new_curve(&F, rec, rn, geo_level, geo_color); free(rec);
        if (k < 0) continue;
        attr_new_ent(k); made++;
        for (int j = 0; j < ch->n; j++) {                         /* make_nurbs_from_chains's end loop over the chain's entities (the 0x40 flag), param_3 = Conversion */
            int e = ch->ent[j]; if (e < 0 || e >= F.nents || !F.ents[e].type) continue;
            if (sp_cv.action == 1) mod_blank(e);
            else if (sp_cv.action == 2) { if (sp_used_by_ops(e)) mod_blank(e); else mod_del(e); }
            else if (sp_cv.action >= 3) sp_copy_level(e, sp_cv.level);
        }
    }
    mod_end();
    if (made) doc_dirty = 1;
    overlay_dirty = 1;
}
void sp_curv_panel(const CuiPanelOps *ops, void *u) {             /* WireframeSplineCurvesManagerView */
    if (ops->exp(u, "Chains")) { ops->toggle(u, "_Reselect", 1, 4); ops->end(u); }
    if (ops->exp(u, "_Tolerance")) { ops->num(u, "", "cvtol", &sp_cv.tol, NULL, 1, 0, 1e12); ops->end(u); }
    if (ops->exp(u, "Original Curves")) { ops->radio(u, "_Delete curves", &sp_cv.action, 2, 1); ops->radio(u, "_Keep curves", &sp_cv.action, 0, 1); ops->radio(u, "_Blank curves", &sp_cv.action, 1, 1); ops->radio(u, "_Move to level", &sp_cv.action, 3, 1); ops->end(u); }
    if (ops->exp(u, "_Levels")) { ops->inum(u, "", "cvlvl", &sp_cv.level, NULL, sp_cv.action >= 3, 1, 1000000); ops->end(u); }
    if (ops->exp(u, "_Sharp Corner Smoothing")) { ops->check(u, "_Sharp Corner Smoothing", &sp_cv.sharp, 1); ops->num(u, "Detection _angle", "cvang", &sp_cv.ang, NULL, sp_cv.sharp, 0, 180); ops->num(u, "Bl_end distance", "cvblend", &sp_cv.blend, NULL, sp_cv.sharp, 0, 1e12); ops->end(u); }
}

/* ---- Refit Spline (mccadutil RefitSpline FUN_180134ca0; the engine FUN_1801347c0 / MaCompositeCurve::Refit: mcspline_refit.c): the selected splines (select_ents mask 0x88: Enter), rebuilt live on every panel change; OK / Apply: Delete: the originals deleted (FUN_180134020: blanked when used by toolpaths), the undo step committed, main colour; Cancel / Reselect: terminated. */
static struct { double chord, ang, blend; int allow, action, brk; } sp_ref; static int *sp_ref_src, sp_ref_ns, sp_ref_failed; static double sp_ref_maxfair;
static void sp_ref_begin(void) { sp_ref.chord = mc_systol.curve_chord_dev; sp_ref.ang = 10.0; sp_ref.blend = 0.1; sp_ref.allow = 0; sp_ref.action = 1; sp_ref.brk = 0; sp_ref_on = 0; sp_ref_maxfair = 1e38; }
static void sp_ref_drop(void) {
    if (!sp_ref_on) return;
    for (int i = mod_from; i >= 0 && i < F.nents; i++) ent_off(i);
    for (int k = 0; k < mod_rec.nd; k++) ent_on(mod_rec.dents[k], mod_rec.dtypes[k]);
    urec_free(&mod_rec); mod_from = -1; memset(&mod_rec, 0, sizeof mod_rec); sp_ref_on = 0; scene_reupload(); overlay_dirty = 1;
}
static void sp_ref_build(void) {                                  /* FUN_1801347c0 */
    if (sp_ref_on) sp_ref_drop();
    else { free(sp_ref_src); sp_ref_ns = sel_list(&sp_ref_src, 0); sp_ref_maxfair = 1e38; }   /* FUN_180134ca0: bVar4 (a new selection): local_488 = 1e38 */
    McrParams P; memset(&P, 0, sizeof P); P.chord_dev = sp_ref.chord; P.angle_deg = sp_ref.ang; P.fair_dist = sp_ref.blend; P.allow_more_nodes = sp_ref.allow; P.break_at_sharp = sp_ref.brk == 1; P.max_fair = sp_ref_maxfair; P.tol = mc_systol.tol; P.step_chord_dev = mc_systol.surf_step_chord_dev; P.metric = mcam_is_metric(&F); P.nurbs = sp_nurbs_cfg();
    int from = F.nents, n = 0, np = 0; sp_ref_failed = 0;
    mod_begin();
    for (int i = 0; i < sp_ref_ns; i++) {
        int e = sp_ref_src[i]; if (e < 0 || e >= F.nents || F.ents[e].type != E_SPLINE) continue;
        McrResult o; memset(&o, 0, sizeof o); CurveRec *cr = &F.crvs[F.ents[e].crv];
        int r = mcr_refit(cr->rec, cr->n, &P, &o);
        if (r == -1) { np++; continue; }
        if (r != 0) { sp_ref_failed = 1; mcr_free(&o); break; }
        if (o.clamped) sp_ref_maxfair = P.fair_dist;              /* Refit returned 2: *param_3 keeps the clamped distance */
        for (int q = 0; q < o.nrec; q++) { int k = mcam_new_curve(&F, o.rec[q].rec, o.rec[q].n, geo_level, cfg_live_color); if (k >= 0) { attr_new_ent(k); n++; } }
        mcr_free(&o);
    }
    (void)from;
    sp_ref_on = 1;
    if (sp_ref_failed) { sp_ref_drop(); sp_ref_failed = 0; n = -1; }
    if (np) snprintf(cam_msg, sizeof cam_msg, "Refit Spline: this spline input is NOT PORTED (%d skipped)", np); else snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str());
    overlay_dirty = 1;
}
static void sp_ref_commit(void) {
    if (!sp_ref_on) return;
    if (sp_ref.action == 0) for (int i = 0; i < sp_ref_ns; i++) { int e = sp_ref_src[i]; if (e >= 0 && e < F.nents && F.ents[e].type) { if (sp_used_by_ops(e)) mod_blank(e); else mod_del(e); } }
    for (int i = mod_from; i >= 0 && i < F.nents; i++) sp_set_color(i, geo_color);
    mod_end(); doc_dirty = 1; sp_ref_on = 0; sel_clear(); overlay_dirty = 1;
}

/* ---- the panel, the picks */
int sp_begin(int c) {
    sp_nn = 0; sp_livent = -1; sp_livefrom = -1; sp_in_edit = 0; sp_prompt = 0; sp_pick_side = -1; sp_auto_n = 0; sp_ble_state = 0; sp_ble_e[0] = sp_ble_e[1] = -1; sp_ble_resel = -1; sp_ble_live = 0; sp_mod_e = sp_mod_cp = -1;
    sp_reset_conditions();
    /* the panels are new objects at every command (FUN_180260330 / FUN_1801ee790 ...: make_shared, no static; the exits only SaveAutoCompleteList): nothing carries over between runs */
    sp_mag[0] = sp_mag[1] = 1.0; sp_trim = 1; sp_edit_ec = 0; sp_ang[0] = sp_ang[1] = 0; sp_sim_tol = 0.0002; sp_sim_mode = 0; sp_mod_ep[0] = sp_mod_ep[1] = 0;
    if (c == G_REFIT) sp_ref_begin();
    snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str()); overlay_dirty = 1;
    return 1;
}
void sp_end(void) {
    if (geo_cmd == G_REFIT) sp_ref_drop();
    if ((geo_cmd == G_SPLMAN || geo_cmd == G_SPLAUTO) && sp_in_edit) { sp_commit(); sp_in_edit = 0; }
    if (geo_cmd == G_SPLBLEND) sp_ble_commit();
    sp_nn = 0; sp_livent = -1; sp_pick_side = -1; overlay_dirty = 1;
}
int sp_need(void) {
    if (sp_pick_side >= 0) return 'e';
    switch (geo_cmd) {
    case G_SPLMAN: return sp_in_edit ? 0 : 'p';
    case G_SPLAUTO: return sp_in_edit ? 0 : 'P';                /* an existing point only (mask 1) */
    case G_SPLBLEND: return sp_ble_state == 0 || sp_ble_state == 2 ? 'e' : sp_ble_state == 1 || sp_ble_state == 3 ? 'e' : 0;
    case G_UNTRIM: case G_X2NURBS: case G_SIMPL: case G_REFIT: case G_CSURF: return 0;
    case G_MODIFYSP: return sp_mod_cp >= 0 ? 'p' : sp_mod_e >= 0 && F.ents[sp_mod_e].cls == 8 ? 'c' : 'e';   /* 'c': a cursor position (get_event 0x1000000c) */
    default: return 0;
    }
}
/* Manual: a point: FUN_180260ef0 event 1 (l.0x180261523): repeat of the previous node (within tol): ignored; added; closing on the first node (within tol): finished */
static void sp_man_point(V3 p) {
    if (sp_nn > 0 && sp_near(p, sp_n[sp_nn - 1], mc_systol.tol)) return;
    sp_grow(sp_nn + 1); sp_n[sp_nn++] = p;
    overlay_dirty = 1;
    if (sp_nn != 1 && !(sqrt((p.x - sp_n[0].x) * (p.x - sp_n[0].x) + (p.y - sp_n[0].y) * (p.y - sp_n[0].y) + (p.z - sp_n[0].z) * (p.z - sp_n[0].z)) >= mc_systol.tol)) {
        sp_finish(1); return;                                    /* the spline closed on its first node (dist < tol: the decompile's tol <= dist goes on) */
    }
}
/* a pick of the running command (the view click, or a test's geopick): e = the entity under it (-1 none), p = the (snapped) point */
void sp_take(int e, V3 p) {
    int need = sp_need(); if (!need) return;
    if (geo_cmd == G_MODIFYSP) {
        if (sp_mod_e >= 0 && F.ents[sp_mod_e].cls == 8 && sp_mod_cp >= 0) { sp_n[sp_mod_cp] = sp_mod_drag_pos(p, sp_mod_cp); sp_mod_param_recalc(); sp_mod_cp = -1; sp_mod_prompt(); return; }
        if (need == 'e' || sp_mod_cp < 0) sp_mod_pick(e, p);
        else { sp_mod_set_cp(sp_mod_e, sp_mod_cp, p); sp_mod_cp = -1; sp_mod_prompt(); }
        return;
    }
    if (sp_pick_side >= 0) { sp_cond_entity(e, p); return; }
    if (geo_cmd == G_SPLMAN) { sp_man_point(p); return; }
    if (geo_cmd == G_SPLAUTO) {                                  /* points only: event 2 with get_ent_from_eptr (get_event mask 1): the point under the cursor, its coordinates (proj_a_point) */
        if (e < 0 || F.ents[e].type != E_POINT) return;
        if (sp_auto_n > 0 && sp_near(F.ents[e].a, sp_auto_p[sp_auto_n - 1], mc_systol.tol)) return;
        sp_auto_p[sp_auto_n++] = sp_proj_a(F.ents[e].a);          /* proj_a_point(1) */
        snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str());
        if (sp_auto_n >= 3) sp_auto_process();
        overlay_dirty = 1; return;
    }
    if (geo_cmd == G_SPLBLEND) {                                 /* FUN_1801ee790: event 2 (mask 0x8e): first a pick, then dyno_curve; a new first pick commits the live spline */
        if (sp_ble_state == 0 || sp_ble_state == 2) {
            if (e < 0 || !(F.ents[e].type == E_LINE || F.ents[e].type == E_ARC || F.ents[e].type == E_SPLINE)) return;
            if (sp_ble_state == 0 && sp_ble_resel < 0) sp_ble_commit();
            sp_ble_e[sp_ble_state / 2] = e; sp_ble_t[sp_ble_state / 2] = sp_dyn_t = sp_pick_t(e, p); sp_dyn_on = 0; sp_ble_state++;
            snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str()); overlay_dirty = 1; return;
        }
        if (sp_ble_state == 1 || sp_ble_state == 3) {            /* dyno_curve's click: event 1 -> FUN_180320420: proj_pt_on_ent(the event point, 0, the running t) */
            int s = sp_ble_state / 2, se = sp_ble_e[s]; double T[3] = { p.x, p.y, p.z }, Q[3], u; int ok;
            sp_proj(se, T, sp_dyn_t, Q, &u, &ok);
            sp_ble_t[s] = ok ? u : sp_dyn_t;
            sp_ble_state = sp_ble_resel >= 0 || s == 1 ? 4 : 2;
            if (sp_ble_state == 4) { sp_ble_resel = -1; sp_ble_make(); sp_ble_state = 0; }
            snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str()); overlay_dirty = 1; return;
        }
    }
}
int sp_click(float mx, float my, const double w[3], int ctrl) {
    int need = sp_need(); if (!need) return 1;
    V3 p = { w[0], w[1], grid_z() }; int e = -1;
    if (need == 'c') { }                                     /* the raw cursor */
    else if (need == 'p') {
        p.z = constr_depth;
        const V3 *an = sp_nn ? &sp_n[sp_nn - 1] : NULL;
        geo_snap_ptr(mx, my); int sn = geo_snap(&p, an, geo_rb_type(), 1); geo_snap_ptr_off();
        if (!sn) sn = geo_ang_snap(&p, an, geo_rb_type(), ctrl) ? -1 : 0;
        if (!cfg_constr_3d) p.z = constr_depth;
    } else e = pick_entity(mx, my);
    sp_take(e, p);
    return 1;
}
static void sp_edit_done(int more) {                             /* the end conditions loop left: kept, the next spline (FUN_180227cd0's return); Automatic: uVar17 = 0 (FUN_180266560: three new picks) */
    sp_commit(); sp_in_edit = 0; sp_nn = 0; sp_livent = -1; sp_pick_side = -1; sp_auto_n = 0;   /* a To Entity pick still pending (select_ent is modal inside FUN_180228600) ends with the loop */
    if (more) snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str());
    overlay_dirty = 1;
}
int sp_esc(void) {                                               /* FUN_180260ef0: Esc (get_event != 0) finishes the spline in progress and leaves; Blended: get_event 2 leaves, the live spline committed */
    if (sp_in_edit && sp_pick_side >= 0) { sp_pick_side = -1; snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str()); overlay_dirty = 1; return 1; }   /* select_ent's Esc (FUN_180228600): no change, the loop goes on */
    if ((geo_cmd == G_SPLMAN || geo_cmd == G_SPLAUTO) && sp_in_edit) { sp_edit_done(1); return 1; }   /* FUN_180227cd0: get_event != 0: break, cVar4 = 1: the spline kept, the command goes on (FUN_180260ef0 local_558, FUN_180266560 local_2f78) */
    if (geo_cmd == G_SPLMAN) sp_finish(0);
    return 0;
}
void sp_enter_key(void) {                                        /* <Enter>: the finish path of Manual (event 0x800 l.0x180261641); selection commands: run; Automatic. In the end conditions loop: as Apply, from prompt 5869 ("Press <ENTER> or <APPLY> when done") only: FUN_180227cd0's mask 0x10008008 has no 0x800 and get_event's Enter path there is NOT DECODED */
    if (geo_cmd == G_SPLMAN || geo_cmd == G_SPLAUTO) {
        if (sp_in_edit) sp_edit_done(1);
        else if (geo_cmd == G_SPLMAN) sp_finish(1);
    } else if (geo_cmd == G_MODIFYSP) { if (sp_mod_e >= 0) geo_end(); else sp_mod_prompt(); }   /* prompt 8924; cpts_nurbs (one select_ent, no loop) returns */
    else if (geo_cmd == G_UNTRIM) { sp_untrim_run(); geo_end(); }   /* UntrimSpline: one select_ents, no loop */
    else if (geo_cmd == G_X2NURBS) { sp_x2n_run(); geo_end(); }     /* x_2_nurbs: one select_ents, no loop */
    else if (geo_cmd == G_CSURF) csf_run();                         /* sketcher_curve_6: one select_ents, no loop (csf_run ends the command) */
    else if (geo_cmd == G_SIMPL) sp_simpl_run();
    else if (geo_cmd == G_REFIT && !sp_ref_on) sp_ref_build();
}
/* the panel buttons: 0 OK, 1 Apply (OK-New), 2 Cancel, 4 Back up one point (Manual) - FUN_180260ef0 l.0x180261364-0x1802613b0 */
int sp_button(int code) {
    if (geo_cmd == G_SPLMAN || geo_cmd == G_SPLAUTO) {
        if (code == 0 || code == 2 || code == 1) {               /* FUN_180227cd0: OK / Cancel leave the loop (cVar4 = 0), Apply keeps the spline, resets the panel (FUN_1802278b0) and goes on; FUN_180260ef0: 0 / 2 finish and leave, 1 finish and go on */
            if (sp_in_edit && code == 1) sp_reset_conditions();
            if (sp_in_edit) sp_edit_done(code == 1);
            else if (geo_cmd == G_SPLMAN) sp_finish(code == 1);
            return code == 1;
        }
        if (code == 4 && geo_cmd == G_SPLMAN) {                  /* Back up one point (FUN_180260ef0 l.0x180261364); in the edit loop codes 4 / 5: FUN_1802278b0 (natural again) and the loop ends */
            if (sp_in_edit) { sp_reset_conditions(); sp_rebuild(); sp_edit_done(1); }
            else if (sp_nn > 0) sp_nn--;
            overlay_dirty = 1;
        }
        return 1;
    }
    if (geo_cmd == G_SPLBLEND) {                                 /* FUN_1801ee790 l.0x1801ef7f9-: 2 Cancel / 0 OK leave (the live spline committed), 1 Apply commits; 4 / 5 Reselect entity 1 / 2; 6 - 8 a panel value: the result rebuilt */
        if (code == 0 || code == 2) { sp_ble_commit(); return 0; }
        if (code == 1) { sp_ble_commit(); return 1; }
        if ((code == 4 || code == 5) && !sp_ble_live) return 1;   /* BAML: Reselect IsEnabled = EnableEntity1 / 2; FUN_1801ee790 enables them with the spline made (vtable +0x30 / FUN_1801ee6b0) */
        if (code == 4 || code == 5) { sp_ble_rollback(); sp_ble_resel = code - 4; sp_ble_state = code == 4 ? 0 : 2; snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str()); overlay_dirty = 1; return 1; }
        return 1;
    }
    if (geo_cmd == G_MODIFYSP) {
        if (code == 0 || code == 2) return 0;
        if ((code == 4 || code == 5) && sp_mod_e >= 0 && F.ents[sp_mod_e].cls == 8 && sp_mod_cp < 0) sp_mod_param_recalc();   /* FUN_18029e120 codes 4 / 5 (Endpoint 1 / 2): the copy, recalc_spline */
        return 1;
    }
    if (geo_cmd == G_REFIT) {                                    /* FUN_180134ca0: 0 / 1 FUN_180133fe0 (Delete: the originals deleted), UndoTransCommit, sktcher_main_color; 1 goes on selecting; 2 leaves (the undo step terminated); 5 Reselect */
        if (code == 0 || code == 1) { if (!sp_ref_on) sp_ref_build(); sp_ref_commit(); if (code == 1) { snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str()); overlay_dirty = 1; } return code == 1; }
        if (code == 2) { sp_ref_drop(); return 0; }
        if (code == 5) { sp_ref_drop(); snprintf(cam_msg, sizeof cam_msg, "%s", sp_prompt_str()); overlay_dirty = 1; }
        return 1;
    }
    if (geo_cmd == G_SIMPL) { if (code == 0 || code == 1) sp_simpl_run(); return code == 1; }   /* FUN_180292940 codes 0 / 1: FUN_180293490 */
    if (geo_cmd == G_UNTRIM || geo_cmd == G_X2NURBS || geo_cmd == G_CSURF) {   /* one select_ents, no panel in Mastercam: its End Selection ran the command once and returned */
        if (geo_cmd == G_CSURF) { if (code == 0 || code == 1) { csf_run(); return 1; } return 0; }   /* csf_run ends the command itself (its message kept) */
        if (code == 0 || code == 1) { if (geo_cmd == G_UNTRIM) sp_untrim_run(); else sp_x2n_run(); }
        return 0;
    }
    return code != 0 && code != 2;
}
void sp_panel_sync(void) {}
/* Manual / Automatic: the end conditions controls (WireframeSplineEndpointConditionView; StartEndpointCondition labels "_Start point:", "Start direction:", "Direction _1", "Direction _2", "Start _angle:"; the End view "_End point:" ...) */
static void sp_cond_ui(const CuiPanelOps *ops, void *u, int side) {
    static const char *items[5] = { "Natural", "3 Point", "To Entity", "To End", "Angle" };
    ops->label(u, side ? "_End point:" : "_Start point:");
    int before = sp_cb[side]; ops->combo(u, items, 5, &sp_cb[side], -1);
    ops->label(u, side ? "End direction:" : "Start direction:");
    int d1 = sp_dir1[side]; int r1 = ops->radio(u, "Direction _1", &sp_dir1[side], 1, sp_in_edit); int r2 = ops->radio(u, "Direction _2", &sp_dir1[side], 0, sp_in_edit); (void)r1; (void)r2;
    double ad = sp_ang[side]; ops->num(u, side ? "End _angle:" : "Start _angle:", side ? "spang1" : "spang0", &sp_ang[side], NULL, sp_cb[side] == 4 && sp_in_edit, -1e12, 1e12);
    if (sp_in_edit) {                                            /* FUN_180227cd0: codes 100 / 101 (a combo), 0x68 / 0x69 (the direction), 0x66 / 0x67 (Angle): FUN_180228600 for that end */
        static const int cmap[5] = { 1, 0, 2, 3, 4 };            /* FUN_1801d3830 */
        if (before != sp_cb[side]) { sp_code[side] = sp_code[side]; sp_apply_side(side, cmap[sp_cb[side]], sp_ang[side] * 0.01745329251994329, 0); }
        else if (d1 != sp_dir1[side]) sp_apply_side(side, cmap[sp_cb[side]], sp_ang[side] * 0.01745329251994329, 1);
        else if (sp_cb[side] == 4 && ad != sp_ang[side]) { sp_code[side] = 0; sp_apply_side(side, 4, sp_ang[side] * 0.01745329251994329, 0); }
    }
}
void sp_panel_body(const CuiPanelOps *ops, void *u) {
    switch (geo_cmd) {
    case G_SPLMAN:
        if (ops->exp(u, "Position")) { ops->label(u, "_Back up one point:"); ops->toggle(u, "_Back up one point:", 1, 4); ops->end(u); }
        if (ops->exp(u, "Edit End _Conditions")) { ops->check(u, "Edit End _Conditions", &sp_edit_ec, !sp_in_edit && sp_nn == 0); sp_cond_ui(ops, u, 0); sp_cond_ui(ops, u, 1); ops->end(u); }
        break;
    case G_SPLAUTO:
        if (ops->exp(u, "Edit End _Conditions")) { ops->check(u, "Edit End _Conditions", &sp_edit_ec, 1); sp_cond_ui(ops, u, 0); sp_cond_ui(ops, u, 1); ops->end(u); }
        break;
    case G_SPLBLEND:
    {   double m0 = sp_mag[0], m1 = sp_mag[1]; int tr = sp_trim;
        if (ops->exp(u, "Entity 1")) { ops->toggle(u, "_Reselect", sp_ble_live, 4); ops->num(u, "_Magnitude:", "spmag0", &sp_mag[0], NULL, 1, -1e12, 1e12); ops->end(u); }
        if (ops->exp(u, "Entity 2")) { ops->toggle(u, "Reselec_t", sp_ble_live, 5); ops->num(u, "M_agnitude:", "spmag1", &sp_mag[1], NULL, 1, -1e12, 1e12); ops->end(u); }
        if (ops->exp(u, "Settings")) { ops->label(u, "Trim:"); ops->radio(u, "_None", &sp_trim, 0, 1); ops->radio(u, "_Both", &sp_trim, 1, 1); ops->radio(u, "Entity _1", &sp_trim, 2, 1); ops->radio(u, "Entity _2", &sp_trim, 3, 1); ops->end(u); }
        if ((m0 != sp_mag[0] || m1 != sp_mag[1] || tr != sp_trim) && sp_ble_live) { sp_ble_rollback(); sp_ble_make(); }   /* codes 6 - 8 */
        break; }
    case G_SIMPL:                                                /* WireframeSplineSimplifyManagerView */
        if (ops->exp(u, "_Tolerance")) { ops->num(u, "", "sptol", &sp_sim_tol, &sp_sim_lock, 1, 0, 1e12); ops->end(u); }
        if (ops->exp(u, "Original Spline")) { ops->radio(u, "_Delete", &sp_sim_mode, 0, 1); ops->radio(u, "_Keep", &sp_sim_mode, 1, 1); ops->radio(u, "_Blank", &sp_sim_mode, 2, 1); ops->end(u); }
        break;
    case G_REFIT: {                                              /* WireframeSplineRefitManagerView; FUN_1801305f0: +0 chord deviation, +8 angle, +0x10 blend distance, +0x18 Allow, +0x1c Original Spline (0 Delete, 1 Keep), +0x20 Sharp handling (0 Blend, 1 Break); any change: codes 6 - 10 rebuild */
        unsigned long h = 0;
        double a = sp_ref.chord, b = sp_ref.ang, c = sp_ref.blend; int i1 = sp_ref.allow, i2 = sp_ref.action, i3 = sp_ref.brk; (void)h;
        if (ops->exp(u, "Chains")) { ops->toggle(u, "_Reselect", 1, 5); ops->end(u); }
        if (ops->exp(u, "Original Spline")) { ops->radio(u, "_Delete", &sp_ref.action, 0, 1); ops->radio(u, "_Keep", &sp_ref.action, 1, 1); ops->end(u); }
        if (ops->exp(u, "Additional Control Points")) { ops->check(u, "_Allow", &sp_ref.allow, 1); ops->end(u); }
        if (ops->exp(u, "_Curve Chordal Deviation")) { ops->num(u, "", "rfchord", &sp_ref.chord, NULL, 1, 0, 1e12); ops->end(u); }
        if (ops->exp(u, "Sharp Corners")) { ops->num(u, "De_tection angle:", "rfang", &sp_ref.ang, NULL, 1, 0, 180); ops->label(u, "Method:"); ops->radio(u, "_Blend", &sp_ref.brk, 0, 1); ops->num(u, "", "rfblend", &sp_ref.blend, NULL, sp_ref.brk == 0, 0, 1e12); ops->radio(u, "Br_eak", &sp_ref.brk, 1, 1); ops->end(u); }
        if (sp_ref_on && (a != sp_ref.chord || b != sp_ref.ang || c != sp_ref.blend || i1 != sp_ref.allow || i2 != sp_ref.action || i3 != sp_ref.brk)) sp_ref_build();
        break; }
    case G_MODIFYSP:
        if (sp_mod_e >= 0 && F.ents[sp_mod_e].cls == 8 && ops->exp(u, "Tangency")) { int a = sp_mod_ep[0], b = sp_mod_ep[1]; ops->check(u, "Endpoint _1", &sp_mod_ep[0], 1); ops->check(u, "Endpoint _2", &sp_mod_ep[1], 1); ops->end(u);
            if ((a != sp_mod_ep[0] || b != sp_mod_ep[1]) && sp_mod_cp < 0) sp_mod_param_recalc(); }
        break;
    default: break;
    }
}
/* draw_rubber_band case 9 (decomp/rubber/rb.c l.627): the spline through the picked nodes and the cursor, natural ends, or closed when the cursor is on the first node (tol); not when the cursor is within tol of the last node. Sampled per segment (ours: the fixed sampling) */
static void sp_ov_curve(float **pv, int *pn, int *pcap, const double (*Q)[3], int n, int e0, const double *t0, int e1, const double *t1) {
    if (n < 2) return;
    double *C = malloc(sizeof(double) * 12 * (size_t)n), cl = sqrt((Q[n - 1][0] - Q[0][0]) * (Q[n - 1][0] - Q[0][0]) + (Q[n - 1][1] - Q[0][1]) * (Q[n - 1][1] - Q[0][1]) + (Q[n - 1][2] - Q[0][2]) * (Q[n - 1][2] - Q[0][2])), z[3] = { 0, 0, 0 }, tol = mc_systol.tol;
    int ok = cl <= tol && n >= 4 ? mcsf_coefs_closed(C, Q, n, tol) : mcsf_coefs_open(C, Q, n, e0, t0 ? t0 : z, e1, t1 ? t1 : z, tol);
    if (ok) { float c[3]; mc_rgb(geo_color, c); V3 last = { Q[0][0], Q[0][1], Q[0][2] };
        for (int i = 0; i < n - 1; i++) for (int j = 1; j <= 16; j++) { double u = j / 16.0, *a = C + i * 12, P[3]; for (int k = 0; k < 3; k++) P[k] = ((a[9 + k] * u + a[6 + k]) * u + a[3 + k]) * u + a[k];
            V3 v = { P[0], P[1], P[2] }; ov_pt(pv, pn, pcap, last, c[0], c[1], c[2]); ov_pt(pv, pn, pcap, v, c[0], c[1], c[2]); last = v; } }
    free(C);
}
void sp_overlay(float **pv, int *pn, int *pcap) {
    if (geo_cmd == G_SPLMAN && !sp_in_edit && sp_nn >= 1 && geo_cursor_ok) {
        V3 cu = geo_cursor;
        if (sp_near(cu, sp_n[sp_nn - 1], mc_systol.tol)) return;
        double (*Q)[3] = malloc(sizeof(double) * 3 * (size_t)(sp_nn + 1));
        for (int i = 0; i < sp_nn; i++) { Q[i][0] = sp_n[i].x; Q[i][1] = sp_n[i].y; Q[i][2] = sp_n[i].z; }
        Q[sp_nn][0] = cu.x; Q[sp_nn][1] = cu.y; Q[sp_nn][2] = cu.z;
        sp_ov_curve(pv, pn, pcap, (const double (*)[3])Q, sp_nn + 1, 0, NULL, 0, NULL);
        free(Q);
    }
    if (geo_cmd == G_MODIFYSP && sp_mod_e >= 0 && sp_mod_cp >= 0 && geo_cursor_ok && F.ents[sp_mod_e].cls == 8) {   /* case 0x21 (FUN_18033d520) */
        double (*Q)[3] = malloc(sizeof(double) * 3 * (size_t)sp_nn); V3 m = sp_mod_drag_pos(geo_cursor, sp_mod_cp);
        for (int i = 0; i < sp_nn; i++) { V3 q = i == sp_mod_cp ? m : sp_n[i]; Q[i][0] = q.x; Q[i][1] = q.y; Q[i][2] = q.z; }
        sp_ov_curve(pv, pn, pcap, (const double (*)[3])Q, sp_nn, sp_mod_ep[0] ? 2 : 0, sp_mod_tan[0], sp_mod_ep[1] ? 2 : 0, sp_mod_tan[1]);
        free(Q);
    }
    if ((geo_cmd == G_SPLMAN || geo_cmd == G_SPLAUTO) && sp_in_edit && sp_nn >= 2) {   /* FUN_180227be0: an arrow at the end node along the end tangent while the condition is 0, 2 or 3 (GrTemp_AddArrow(node, tangent, false, 0.1, start ? 2 : 4, false); its size 0.1 and style int are NOT DECODED: the tick of dyno_curve's arrow below, ours) */
        float c[3]; mc_rgb(geo_color, c); double wpp = scene_view_height() / (vp_h > 0 ? vp_h : 1) * 30;
        for (int side = 0; side < 2; side++) { if (sp_code[side] != 0 && sp_code[side] != 2 && sp_code[side] != 3) continue;
            V3 P = side ? sp_node(sp_nn - 1) : sp_node(0), q = { P.x + sp_tan[side][0] * wpp, P.y + sp_tan[side][1] * wpp, P.z + sp_tan[side][2] * wpp };
            ov_pt(pv, pn, pcap, P, c[0], c[1], c[2]); ov_pt(pv, pn, pcap, q, c[0], c[1], c[2]); }
    }
    if (geo_cmd == G_SPLBLEND && (sp_ble_state == 1 || sp_ble_state == 3)) {   /* dyno_curve's arrow (GrTemp_AddArrow): a tick at the running t along the tangent */
        V3 P; double T[3]; float c[3]; mc_rgb(geo_color, c); double wpp = scene_view_height() / (vp_h > 0 ? vp_h : 1) * 30;
        if (sp_ent_ptan(sp_ble_e[sp_ble_state / 2], sp_dyn_t, &P, T)) { V3 q = { P.x + T[0] * wpp, P.y + T[1] * wpp, P.z + T[2] * wpp }; ov_pt(pv, pn, pcap, P, c[0], c[1], c[2]); ov_pt(pv, pn, pcap, q, c[0], c[1], c[2]); }
    }
}
/* tests (the spldump hook, wf_scenes): each spline's record: class, knots, control points (%.10g), t1 t2, the stored ends */
void cui_spldump(void) {
    for (int i = 0; i < F.nents; i++) {
        const Ent *E = &F.ents[i]; if (E->type != E_SPLINE || E->crv < 0 || E->crv >= F.ncrvs) continue;
        const CurveRec *cr = &F.crvs[E->crv]; const uint8_t *r = cr->rec; double v;
        double t1, t2; memcpy(&t1, r + 0x98, 8); memcpy(&t2, r + 0xa0, 8);
        printf("      %d spline cls %u t %.10g %.10g", i, cr->cls, t1, t2);
        if (cr->cls == 8) { int32_t ns; memcpy(&ns, r + 0xb0, 4); printf(" nseg %d ord %d dim %d closed %d\n", ns, r[0xb4], r[0xb5], r[0xb7]);
            for (int k = 0; k < ns * 12 && k < 36; k++) { memcpy(&v, r + 0xc0 + 8 * k, 8); printf("      %d  c %.10g\n", i, v); } }
        else { int32_t nk, ncp; memcpy(&nk, r + 0xc8, 4); memcpy(&ncp, r + 0xcc, 4); int vpp = r[0xd1]; printf(" nk %d ncp %d deg %d vpp %d flags %d\n", nk, ncp, r[0xd0], vpp, r[0xd2]);
            for (int k = 0; k < nk && k < 24; k++) { memcpy(&v, r + 0xd8 + 8 * k, 8); printf("      %d  k %.10g\n", i, v); }
            for (int k = 0; k < ncp * vpp && k < 60; k++) { memcpy(&v, r + 0xd8 + 8 * (nk + k), 8); printf("      %d  p %.10g\n", i, v); } }
    }
}
/* tests: spset:edit=1,cb0=2,ang0=30,dir0=0 - the panel's end conditions, as sp_cond_ui applies them */
void sp_script(const char *arg) {
    if (!strncmp(arg, "srfkind=", 8)) { int kd = atoi(arg + 8); char bb[32]; for (int i = 0; i < F.nsurfs; i++) if (F.surfs[i].kind == kd && F.surfs[i].ent >= 0 && !F.surfs[i].blanked && mcstn_rule(&F, &F.surfs[i])) { snprintf(bb, sizeof bb, "%d", F.surfs[i].ent); sel_set_list(bb); break; } return; }
    if (!strncmp(arg, "cplane=", 7)) { plane_set_active(0, atoi(arg + 7)); return; }   /* ours (tests): the active Cplane */
    if (!strncmp(arg, "chainref=", 9)) { if (ncamops && camops[0]->nchains && camops[0]->chains[0].n) camops[0]->chains[0].ent[0] = atoi(arg + 9); return; }   /* ours (tests): the first chain's first entity (the Qt chain path does not chain splines into a new op; a loaded part does) */
    if (!strncmp(arg, "view=", 5)) { view_set_matrix(cam_plane_matrix(atoi(arg + 5))); return; }   /* ours (tests): the graphics view at once, without view_std_animated's animation */
    if (!strcmp(arg, "dump")) {                                 /* ours (tests): the arrows' conditions and tangents, the toolpath chains' entities and types, the Refit clamp ("    rb " lines of wf_compare) */
        if (sp_in_edit) for (int s = 0; s < 2; s++) printf("    rb arrow %d code %d %.6f %.6f %.6f\n", s, sp_code[s], sp_tan[s][0], sp_tan[s][1], sp_tan[s][2]);
        for (int j = 0; j < ncamops; j++) for (int c = 0; c < camops[j]->nchains; c++) for (int k = 0; k < camops[j]->chains[c].n; k++) { int e = camops[j]->chains[c].ent[k]; printf("    rb chainref op %d chain %d #%d ent %d type %d\n", j, c, k, e, e >= 0 && e < F.nents ? F.ents[e].type : -1); }
        for (int i = 0; i < F.nents; i++) if (F.ents[i].type && F.ents[i].blanked) printf("    rb blanked %d\n", i);
        printf("    rb maxfair %.6g need %d active %d view %.3f %.3f %.3f msg %s\n", sp_ref_maxfair, sp_need(), sp_cmd(geo_cmd), vmat[6], vmat[7], vmat[8], cam_msg); return;
    }
    char buf[256]; snprintf(buf, sizeof buf, "%s", arg);
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
        char *e = strchr(t, '='); if (!e) continue; *e++ = 0; double v = atof(e);
        static const int cmap[5] = { 1, 0, 2, 3, 4 };
        if (!strcmp(t, "edit")) sp_edit_ec = (int)v;
        else if (!strcmp(t, "cb0") || !strcmp(t, "cb1")) { int side = t[2] - '0', c = (int)v; sp_cb[side] = c; if (sp_in_edit) sp_apply_side(side, cmap[c], sp_ang[side] * 0.01745329251994329, 0); }
        else if (!strcmp(t, "trim")) { sp_trim = (int)v; if (sp_ble_live) { sp_ble_rollback(); sp_ble_make(); } }
        else if (!strcmp(t, "mag0") || !strcmp(t, "mag1")) { sp_mag[t[3] - '0'] = v; if (sp_ble_live) { sp_ble_rollback(); sp_ble_make(); } }
        else if (!strcmp(t, "simtol")) sp_sim_tol = v; else if (!strcmp(t, "simmode")) sp_sim_mode = (int)v;
        else if (!strcmp(t, "cvaction")) sp_cv.action = (int)v; else if (!strcmp(t, "cvlevel")) sp_cv.level = (int)v;
        else if (!strcmp(t, "rfaction")) { sp_ref.action = (int)v; if (sp_ref_on) sp_ref_build(); } else if (!strcmp(t, "rfbreak")) { sp_ref.brk = (int)v; if (sp_ref_on) sp_ref_build(); } else if (!strcmp(t, "rfallow")) { sp_ref.allow = (int)v; if (sp_ref_on) sp_ref_build(); } else if (!strcmp(t, "rfchord")) { sp_ref.chord = v; if (sp_ref_on) sp_ref_build(); } else if (!strcmp(t, "rfblend")) { sp_ref.blend = v; if (sp_ref_on) sp_ref_build(); }
        else if (!strcmp(t, "ep0") || !strcmp(t, "ep1")) { sp_mod_ep[t[2] - '0'] = (int)v; if (sp_mod_e >= 0 && F.ents[sp_mod_e].cls == 8 && sp_mod_cp < 0) sp_mod_param_recalc(); }
        else if (!strcmp(t, "ang0") || !strcmp(t, "ang1")) { int side = t[3] - '0'; sp_ang[side] = v; if (sp_in_edit && sp_cb[side] == 4) { sp_code[side] = 0; sp_apply_side(side, 4, v * 0.01745329251994329, 0); } }
        else if (!strcmp(t, "dir0") || !strcmp(t, "dir1")) { int side = t[3] - '0'; sp_dir1[side] = (int)v; if (sp_in_edit) sp_apply_side(side, cmap[sp_cb[side]], sp_ang[side] * 0.01745329251994329, 1); }
    }
}
