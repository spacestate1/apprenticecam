/* cui_slice.c - Wireframe > Curve Slice (sketcher_curve_5, FUN_18028d0d0 mccad). Spec: ../../formats/wireframe.md "Curve Slice".
 * Panel: wireframecurveslicemanagerview.baml; values FUN_180242280 / FUN_1802425c0; loop FUN_18028d0d0; surfaces slice_sel_surfs_with_a_plane (mccore 0x18056e000) -> FUN_18056ac50 below -> viewer/mcsrfint.c;
 * curves slice_sel_curves_with_a_plane (0x18056db40) -> FUN_18056d090 below. Not decoded / not ported, greyed with a message: the plane dialog get_plane_info, Offset (offset_a_surface), trimmed surfaces on NURBS bases are ported; other kinds refused. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "../mcsurf.h"
#include "../mcsurf_build.h"
#include "../mcsrfint.h"
#include "../mcsrfcrv.h"

static double sl_spacing = 0.0, sl_offset = 0.0, sl_pt[3], sl_n[3] = { 0, 0, 1 };   /* FUN_1802425c0: Spacing 0, Offset 0 */
int sp_nurbs_cfg(void);   /* cfg_createNurbs (cui_spline.c) */
static int sl_force_nurbs = -1;   /* tests: overrides the config (slset:nurbs=0/1) */
static double sl_last_sp, sl_last_off; static int sl_last_join, sl_last_multi;   /* the values of the last build (sl_panel_sync) */
static int sl_join = 0, sl_multi = 1, sl_built = 0, sl_undo_made = 0;               /* local_538 = 0x100: Join off, Find multiple solutions on; bVar6 */

int sl_cmd(int c) { return c == G_CSLICE; }
static const char *sl_prompt(void) { return sl_built ? "Set options, press <ENTER>, or OK" : "Select surfaces or curves, press Apply when done"; }   /* mccadres 5970 (0x1752), 5971 (0x1753) */
const char *sl_prompt_text(void) { return sl_prompt(); }
/* the plane on entry: the active Cplane's origin and normal (view_to_world of 0 / (0,0,1), AddActiveOrigin, FUN_18028d0d0) */
int sl_begin(int c) {
    (void)c; sl_built = 0; sl_undo_made = 0;
    int id = plane_active[0]; const double *org = plane_active_origin(0);   /* AddActiveOrigin: GetActiveOrigin(0) */ double v[3] = { 0, 0, 0 }, w[3], z[3] = { 0, 0, 1 }, wz[3], o0[3];
    plane_v2w(id, org, v, w); memcpy(sl_pt, w, sizeof w);
    plane_v2w(id, org, z, wz); plane_v2w(id, org, v, o0);
    for (int k = 0; k < 3; k++) sl_n[k] = wz[k] - o0[k];
    double l = sqrt(sl_n[0] * sl_n[0] + sl_n[1] * sl_n[1] + sl_n[2] * sl_n[2]); if (l > 0) for (int k = 0; k < 3; k++) sl_n[k] /= l;
    return 1;
}
void sl_end(void) { sl_built = 0; sl_undo_made = 0; }
int sl_need(void) { return 0; }
void sl_take(int e, V3 p) { (void)e; (void)p; }
int sl_click(float mx, float my, const double w[3], int ctrl) { (void)mx; (void)my; (void)w; (void)ctrl; return 0; }

/* lp_ints (mccore 0x1804b3ed0): the plane (n, P) against the segment a b */
static int sl_lp(const double n[3], const double P[3], const double a[3], const double b[3], double o[3], double *t) {
    double d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, dn = n[1] * d[1] + n[0] * d[0] + n[2] * d[2], tol = mc_systol.tol;
    if (fabs(dn) <= tol * tol && tol * tol != fabs(dn)) {
        double a1 = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), a2 = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (a1 == 0.0 || a2 == 0.0 || fabs(dn) / (a2 * a1) < tol) return 0;
    }
    double s = (n[0] * (P[0] - a[0]) + n[1] * (P[1] - a[1]) + n[2] * (P[2] - a[2])) / dn; *t = s;
    for (int k = 0; k < 3; k++) o[k] = d[k] * s + a[k];
    return 1;
}
/* ent_above_below (0x18053ad60) */
static double sl_dist(const double p[3], const double n[3], const double P[3]) { return n[0] * (p[0] - P[0]) + n[1] * (p[1] - P[1]) + n[2] * (p[2] - P[2]); }   /* dist_above_below */
static int sl_point_side(const double p[3], const double n[3], const double P[3]) {   /* FUN_1805502c0: 0 within tol / 2 */
    double d = sl_dist(p, n, P); if (fabs(d) < mc_systol.tol * 0.5) return 0; if (d <= -1e-12) return -1; return 1e-12 <= d ? 1 : 0;
}
static int sl_side(const Ent *en, const double n[3], const double P[3]) {
    if (en->type == E_LINE) { double a[3] = { en->a.x, en->a.y, en->a.z }, b[3] = { en->b.x, en->b.y, en->b.z }; int s1 = sl_point_side(a, n, P), s2 = sl_point_side(b, n, P); return s1 != s2 ? 0 : s1; }   /* FUN_18054bce0 */
    if (en->type == E_ARC) {   /* FUN_180531b00: the centre (world) above the plane by more than r + tol / 2 */
        V3 cw = en->c; if (en->vw) { cw = mcam_arc_pt(en, 0); V3 q = mcam_arc_pt(en, 3.14159265358979323846); cw.x = (cw.x + q.x) * 0.5; cw.y = (cw.y + q.y) * 0.5; cw.z = (cw.z + q.z) * 0.5; }
        double c[3] = { cw.x, cw.y, cw.z }, d = sl_dist(c, n, P), lim = mc_systol.tol * 0.5 + en->r;
        if (fabs(d) < lim) return 0; if (-1e-12 < d) return 1e-12 <= d ? 1 : 0; return -1;
    }
    const CurveRec *c = &F.crvs[en->crv]; double t1, t2; mcs_spline_t1t2(c, &t1, &t2);
    if (c->cls == 8) {                                                       /* FUN_180553210: the first point's side, then the points at i + k / (order - 1) inside the range, the end */
        double p[3]; memcpy(p, c->rec + 0x68, 24); int s0 = sl_point_side(p, n, P); if (s0 == 0) return 0;
        int nseg = *(const int *)(c->rec + 0xb0), ord = c->rec[0xb4];
        for (int i = 0; i < nseg; i++) for (int k = 1; k < ord; k++) { double t = (double)i + (double)k / (double)(ord - 1), q[3]; if (t1 + 1e-08 < t && t < t2 - 1e-08) { if (mcs_curve_eval(&F, c, t, q)) return 0; if (sl_point_side(q, n, P) != s0) return 0; } }
        memcpy(p, c->rec + 0x80, 24); return sl_point_side(p, n, P) == s0 ? s0 : 0;
    }
    /* FUN_18054e270: class 0x80: the sides of the control points agree */
    const uint8_t *r = c->rec; int nk = *(const int *)(r + 0xc8), ncp = *(const int *)(r + 0xcc), vals = r[0xd1], poly = (r[0xd2] & 4) != 0; const double *cp = (const double *)(r + 0xd8) + nk; int sd = 0;
    for (int i = 0; i < ncp; i++) { double q[3]; for (int k = 0; k < 3; k++) q[k] = poly ? cp[i * vals + k] : cp[i * vals + 1 + k] / cp[i * vals]; int s = sl_point_side(q, n, P); if (i == 0) { sd = s; if (s == 0) return 0; } else if (s != sd) return 0; }
    return sd;
}
static void sl_store(const double p[3], int *hits) { V3 q = { p[0], p[1], p[2] }; mcam_new_ent(&F, E_POINT, q, q, (V3){ 0, 0, 0 }, 0, 0, 0, geo_level, geo_color); (*hits)++; }   /* store_point, main attributes */
/* brk_spline_const_err (mccore 0x180162540), forward (param_5 = 0): the next parameter at the chord error err, with knot / segment ends respected */
static double sl_speed(const CurveRec *c, double t) { double E[3][3]; mcs_spline_eval_d(c, t, 1, E); return sqrt(E[1][0] * E[1][0] + E[1][1] * E[1][1] + E[1][2] * E[1][2]); }   /* FUN_180162c70 */
static double sl_curv(const CurveRec *c, double t) { double E[3][3]; mcs_spline_eval_d(c, t, 2, E); double d = sqrt(E[1][0] * E[1][0] + E[1][1] * E[1][1] + E[1][2] * E[1][2]); if (d < 1e-08) return 0.0; double cx = E[1][1] * E[2][2] - E[1][2] * E[2][1], cy = E[1][2] * E[2][0] - E[1][0] * E[2][2], cz = E[1][0] * E[2][1] - E[1][1] * E[2][0]; return sqrt(cx * cx + cy * cy + cz * cz) / d; }   /* FUN_180162d60: |C' x C''| / |C'| */
static double sl_step_at(const CurveRec *c, double t, double hmin, double hmax, double err) { double k = sl_curv(c, t), h = 1e-08 < k ? sqrt(err * 8.0 / k) : 1.0; if (h < hmin) h = hmin; if (hmax < h) h = hmax; return h; }   /* FUN_180162cf0 */
static int sl_interval(const CurveRec *c, double t) {
    if (c->cls == 8) { int nseg = *(const int *)(c->rec + 0xb0), i = (int)floor(t) + 1; if (i <= nseg && i < 1) i = 1; if (nseg < i) i = nseg; return i; }
    int nk = *(const int *)(c->rec + 0xc8), deg = c->rec[0xd0]; const double *K = (const double *)(c->rec + 0xd8);   /* nurb_interval (0x1805b0630) */
    if (t < K[0]) t = K[0]; if (K[nk - 1] < t) t = K[nk - 1];
    int i3 = deg + 1, i4 = nk - i3, i1 = i4; for (long l = i3; l <= i4 && (i1 = i3, K[l] - 1e-10 <= t); l++) { i3++; i1 = i4; }
    i1--; int i9 = i1;
    if (K[i1 + 1] - K[i1] <= 1e-10) { int v = nk - 1; long l = v; if (0 < v) { while (i9 = v, t <= K[l] + 1e-10) { l--; v--; if (l < 1) return i1; } } }
    return i9;
}
static void sl_brk(const CurveRec *c, double p2, double p3, double p4, double *t) {
    double t1, t2; mcs_spline_t1t2(c, &t1, &t2); double dv1 = *t, sp = sl_speed(c, *t), hmin, hmax;
    int i14 = (c->cls == 8) ? sl_interval(c, *t) : sl_interval(c, *t) + 1;
    if (fabs(sp) < 1e-12 || 1e12 < fabs(sp)) { hmin = 0.001; hmax = 100.0; } else { hmin = p2 / sp; hmax = p3 / sp; }
    double dt = sl_step_at(c, *t, hmin, hmax, p4), rem = t2 - *t; if (rem < dt) dt = rem;
    if (0.1 < dt) { double d20 = 0.1, d4 = 0.0, d3;
        do { d3 = d20; d20 = sl_step_at(c, d3 + *t, hmin, hmax, p4); if (d20 < d3) { dt = d20; if (0.0 < d4) dt = d4; break; } d20 = d3 + 0.1; d4 = d3; } while (d3 + 0.1 < dt); }
    if (dt <= rem) *t = dt + *t; else *t = t2;
    int i11 = (c->cls == 8) ? sl_interval(c, *t) : sl_interval(c, *t) + 1;
    if (i11 != i14) { double k = (c->cls == 8) ? (double)i14 : ((const double *)(c->rec + 0xd8))[i14]; if (1e-08 < fabs(k - dv1) && t1 <= k && k <= t2) *t = k; }
    /* the check loop: the chord from t1 (start) to t must keep the middle within err * 1.1 and the chord within p3 * 1.1; else t halves (s13: two consecutive passes keep the first) */
    int s13 = 0; double keep = *t, Ps[3], Pe[3], Pm[3];
    mcs_curve_eval(&F, c, dv1, Ps);
    for (;;) {
        double mid = (*t + dv1) * 0.5; mcs_curve_eval(&F, c, *t, Pe); mcs_curve_eval(&F, c, mid, Pm);
        double e[3] = { Pe[0] - Ps[0], Pe[1] - Ps[1], Pe[2] - Ps[2] }, l = sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]), dd;
        if (l > 0) { double u[3] = { e[0] / l, e[1] / l, e[2] / l }, q[3] = { Pm[0] - Ps[0], Pm[1] - Ps[1], Pm[2] - Ps[2] }, tt = q[0] * u[0] + q[1] * u[1] + q[2] * u[2], r[3] = { q[0] - tt * u[0], q[1] - tt * u[1], q[2] - tt * u[2] }; dd = sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]); }   /* dist_pt_l_3d */
        else { double q[3] = { Pm[0] - Ps[0], Pm[1] - Ps[1], Pm[2] - Ps[2] }; dd = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]); }
        if ((p4 * 1.1 <= dd) || (p3 * 1.1 < l)) s13 = 0; else { s13++; if (s13 == 1) keep = *t; else if (1 < s13) { *t = keep; break; } }
        if (fabs(*t - dv1) < 1e-06 || (l <= p2 && p2 != l)) break;   /* local_198 = param_2 */
        *t = mid;
    }
    if (*t < t1 || t2 < *t) *t = t1 < *t ? t2 : t1;   /* the end clamp */
}
/* FUN_18056d090: per curve: planes at the position, stepped by Spacing along the normal on the curve's side (at most 1000); param_6 = 1 (points), param_7 = 0, chord error 0.0009 (brk_spline_const_err(ent, 0.0009, 1 or 25, 0.0009, 0, &t)) */
static int sl_curve(const Ent *en, const double n[3], const double P0[3], double spacing, int *hits) {
    int s0 = sl_side(en, n, P0), any = 0;
    if (!(mc_systol.tol <= spacing || s0 == 0)) return 0;
    double step[3] = { 0, 0, 0 }, P[3] = { P0[0], P0[1], P0[2] };
    if (mc_systol.tol < spacing) { for (int k = 0; k < 3; k++) step[k] = spacing * n[k]; if (s0 < 0) for (int k = 0; k < 3; k++) step[k] *= -1.0; }
    int N = spacing < mc_systol.tol ? 1 : 1000, cur = s0, s5e = s0 == 0 ? 1 : s0, s13 = s5e, s67 = 0;
    for (int i = 0; i < N; i++) {
        int found = 0;
        if (0 < i) { for (int k = 0; k < 3; k++) P[k] += step[k]; cur = sl_side(en, n, P); }
        if (cur == 0) {
            if (en->type == E_LINE) { double a[3] = { en->a.x, en->a.y, en->a.z }, b[3] = { en->b.x, en->b.y, en->b.z }, x[3], t; if (sl_lp(n, P, a, b, x, &t) && 0.0 <= t && t <= 1.0) { found = 1; sl_store(x, hits); } }
            else if (en->type == E_ARC) {
                double dv = pow(3.84e-10 / (en->r * 5.0), 0.25); int NN = (int)(fabs(en->sw) / dv) + 1; if (NN < 2) NN = 2;
                double dt = 1.0 / (double)NN, tcur = 0, a[3], b[3], x[3], s; V3 q = mcam_arc_pt(en, en->a0); a[0] = q.x; a[1] = q.y; a[2] = q.z; int done = 0;
                while (!done) {
                    double tn = tcur + dt; if (1.0 - 1e-08 < tn) { tn = 1.0; done = 1; }
                    q = mcam_arc_pt(en, en->a0 + en->sw * tn); b[0] = q.x; b[1] = q.y; b[2] = q.z;
                    if (sl_lp(n, P, a, b, x, &s) && 0.0 <= s && s <= 1.0 && (s <= -1e-12 || 1e-12 <= s || tcur <= 0.0)) { found = 1; sl_store(x, hits); }
                    memcpy(a, b, 24); tcur = tn;
                }
            } else {
                const CurveRec *c = &F.crvs[en->crv]; double t1, t2, tc, tn, a[3], b[3], x[3], s; mcs_spline_t1t2(c, &t1, &t2); tc = t1; int done = 0;
                if (mcs_curve_eval(&F, c, tc, a)) return any;
                while (!done) {
                    tn = tc; sl_brk(c, 0.0009, mcam_is_metric(&F) ? 25.0 : 1.0, 0.0009, &tn);
                                    if (t2 - 1e-08 < tn) { tn = t2; done = 1; }
                    if (mcs_curve_eval(&F, c, tn, b)) return any;
                    if (sl_lp(n, P, a, b, x, &s) && 0.0 <= s && s <= 1.0 && (s <= -1e-12 || 1e-12 <= s || tc <= t1)) { found = 1; sl_store(x, hits); }
                    memcpy(a, b, 24); tc = tn;
                }
            }
            any |= found;
            { int c8 = found;
              if (s67 != 0 && (c8 = s67, found == 0)) {
                  if (s0 != 0 || (s13 = -s13, s13 == 1)) return any;
                  for (int k = 0; k < 3; k++) step[k] = -spacing * n[k];
                  memcpy(P, P0, 24);
              }
              s67 = c8; }
        } else {
            int c8 = s67;
            if (s67 == 0) { if (cur != s0) return any; }
            else {
                if (s0 != 0 || s13 != s5e) return any;
                for (int k = 0; k < 3; k++) step[k] *= -1.0;
                s13 = -s5e; memcpy(P, P0, 24);
            }
            s67 = c8;
        }
    }
    return any;
}
/* the curve to the database: MaCurve::Simplify else MakeSplineOrNURBCurve (mcsrfcrv.c emit) */
/* with Join on every cut is held until the whole selection is done: slice_sel_surfs_with_a_plane (0x18056e000) calls MaCurve::JoinCurves once after the last surface */
static MiCurve *sl_all; static int sl_nall, sl_capall;
static int sl_emit_list(MiCurve *c, int nc, int *total) {
    if (sl_join && nc) { for (int j = 0; j < nc; j++) if (c[j].t0 > c[j].k[0] || c[j].t1 < c[j].k[c[j].nk - 1]) { mcsc_truncate_mi(&c[j].deg, &c[j].ncp, &c[j].nk, &c[j].k, &c[j].p, c[j].t0, c[j].t1); c[j].t0 = c[j].k[0]; c[j].t1 = c[j].k[c[j].nk - 1]; }   /* JoinSplines 0x180450940 truncates the trimmed curves; FindConnectedCurveSets sees the trimmed ends */
        if (sl_nall + nc > sl_capall) { sl_capall = (sl_nall + nc) * 2; sl_all = realloc(sl_all, sizeof(MiCurve) * sl_capall); } memcpy(sl_all + sl_nall, c, sizeof(MiCurve) * nc); sl_nall += nc; free(c); *total += nc; return nc; }
    for (int j = 0; j < nc; j++) {
        int made = 0, out[8]; const char *why = NULL;
        mcsc_emit_mi_range(&F, c[j].deg, c[j].ncp, c[j].nk, c[j].k, c[j].p, c[j].t0, c[j].t1, mc_systol.surf_step_chord_dev, mc_systol.tol, geo_level, geo_color, out, 8, &made, &why);
        if (!made && why) { snprintf(cam_msg, sizeof cam_msg, "%s", why); }
        *total += made; free(c[j].k); free(c[j].p);
    }
    free(c); return nc;
}
/* FUN_18056ac50 (mccore): per smooth piece: the plane at P, then stepped by Spacing: +n until a plane finds nothing, then -n from P; when the first plane finds nothing: the hunting grid's distance (DistToPlane) gives the first spacing multiple inside the surface */
static void sl_driver(ISrf *s, MiFace *F_, const double *P0, const double *N0, double spacing, int multi, int *total) {
    int metric = s->metric; double chord = mc_systol.surf_step_chord_dev;
    double l = sqrt(N0[2] * N0[2] + N0[0] * N0[0] + N0[1] * N0[1]), n[3] = { 0, 0, 0 }; if (l != 0.0) { n[0] = N0[0] / l; n[1] = N0[1] / l; n[2] = N0[2] / l; }
    ISrf *pieces; int npc = mi_pieces(s, &pieces);
    for (int ip = 0; ip < npc; ip++) {
        ISrf *q = &pieces[ip]; double P[3] = { P0[0], P0[1], P0[2] }, pl[4] = { n[0], n[1], n[2], n[0] * P0[0] + n[1] * P0[1] + n[2] * P0[2] };
        int b5 = 1, b6 = 1, b17 = 0, guard = 0; double d4 = spacing, gmin = 0, gmax = 0, gmid = 0;
        for (;;) {
            if (++guard > 20000) break;
            MiCurve *c; int nc = F_ ? mi_plane_piece_face(q, F_, pl, chord, chord, metric ? 12.0 : 0.5, metric ? 0.0025 : 1e-4, multi, &c) : mi_plane_piece(q, pl, chord, chord, metric ? 12.0 : 0.5, metric ? 0.0025 : 1e-4, multi, &c);
            if (nc > 0) { b5 = 0; sl_emit_list(c, nc, total); } else free(c);
            if (nc != 0) goto LAB_2b4;
            if (b5 && spacing != 0.0) {
                if (!b6) break;
                if (b17) goto LAB_1e8;
                mi_dist_to_plane(q, pl, &gmin, &gmax, &gmid);
                { double d1 = gmid, d18; d4 = spacing;
                  if (gmid != 0.0) { d18 = gmid / spacing;
                      if (0.0 <= gmid) { d4 = floor(d18) * spacing; d18 = ceil(d18) * spacing;
                          if (d4 <= gmin || gmax <= d4) d4 = d18;
                          else if (gmin < d18) { int be = gmax == d18, bl = gmax < d18; b17 = be; if (!bl && !be && fabs(d18 - d1) <= fabs(d4 - d1)) d4 = d18; }
                      } else { d4 = ceil(fabs(d18)) * spacing * -1.0; d18 = floor(fabs(d18)) * spacing * -1.0;
                          if (gmin <= d4 || d4 <= gmax) d4 = d18;
                          else if (d18 < gmin) { int be = d18 == gmax, bl = d18 < gmax; b17 = be; if (!bl && !be && fabs(d18 - d1) <= fabs(d4 - d1)) d4 = d18; }
                      } }
                }
                for (int k = 0; k < 3; k++) P[k] = d4 * n[k] + P0[k];
                b17 = b6; goto LAB_191;
            }
            if (!b6) break;
        LAB_1e8:
            b6 = 0;
            if (!b17 || b5) memcpy(P, P0, sizeof P); else for (int k = 0; k < 3; k++) P[k] = d4 * n[k] + P0[k];
        LAB_2b4:
            if (spacing == 0.0) break;
            for (int k = 0; k < 3; k++) P[k] += b6 ? spacing * n[k] : -spacing * n[k];
        LAB_191:
            pl[3] = P[1] * n[1] + P[0] * n[0] + P[2] * n[2];
        }
    }
    mi_free_pieces(pieces, npc);
}
/* the build (FUN_18028d0d0, bVar6): slice_sel_surfs_with_a_plane + slice_sel_curves_with_a_plane on the selection */
static void sl_build(void) {
    int *l, *ls; int n = sel_list(&l, 0), ns = sel_list_surfs(&ls);
    if (n + ns <= 0) { free(l); free(ls); snprintf(cam_msg, sizeof cam_msg, "%s", sl_prompt()); return; }
    if (fabs(sl_offset) > mc_systol.tol) { snprintf(cam_msg, sizeof cam_msg, "Curve Slice Offset: NOT PORTED (the MaOffsetSurf kernel is in viewer/mcsrfint.c, but its hunting grid differs from TessellateUntrimmed's: formats/wireframe.md \"Curve Slice: Offset\")"); free(l); free(ls); return; }
    mcsc_nurbs_cfg = sl_force_nurbs >= 0 ? sl_force_nurbs : sp_nurbs_cfg();   /* slice_sel_surfs_with_a_plane: msf_nurbs_splines = cfg_createNurbs for the build */
    mod_begin();
    int total = 0, hits = 0; cam_msg[0] = 0; int any_trim = 0, any_reparam = 0;
    for (int i = 0; i < ns; i++) {
        const Ent *en = &F.ents[ls[i]];
        if (en->type == E_SURF) {
            const Surf *s = &F.surfs[en->srf];
            if (s->kind == 10) {   /* a trimmed surface on a NURBS base: MaFace::MaFace + SubsetToFace(0.05) (slice_sel_surfs_with_a_plane 0x18056e000; formats/wireframe.md "Curve Slice: trimmed surfaces") */
                const Surf *base = NULL; int rep = 0, nl = 0, *np = NULL; double (**pts)[2] = NULL;
                if (!mcsc_face_data(&F, s, &base, &rep, &nl, &np, &pts)) { any_trim = 1; continue; }
                /* LoadSurf(ent, true, 30.0): the base through MaNURBSurf's DB constructor (mcsrfcrv.c mcsc_load_nbs -> mcsrfint.c mi_db_knots, knot
                   regeneration included), the stored loop points normalised and, for regenerated knots, converted (ConvertUVParams) as Trim does */
                ISrf srf; MiRegen RG; memset(&RG, 0, sizeof RG); double k0[2], ks[2];
                int rg = mcsc_load_nbs(&F, base, &srf, &RG, k0, ks);
                if (rg < 0) { for (int l = 0; l < nl; l++) free(pts[l]); free(pts); free(np); any_trim = 1; continue; }
                rep = rg & 3; srf.rev = (s->rec[0x6e] & 4) != 0;   /* MaFace 0x180413ba0: the trimmed record's +0x6e bit 2 */
                double H[4]; mcs_surf_domain(base, H); int xf = H[0] != RG.d0[0] || H[1] != RG.d0[1] || H[2] != RG.d0[2] || H[3] != RG.d0[3]; (void)k0; (void)ks;
                for (int l = 0; l < nl; l++) for (int k = 0; k < np[l]; k++) { double a = pts[l][k][0], b = pts[l][k][1];
                    if (xf) mi_uv_transform(&a, &b, H, RG.d0);   /* MaFace's DB constructor 0x180413ba0: MaPolyLine::UVTransform, then ConvertUVParams */
                    if (rep) { a = mi_regen_map(&RG, 0, a); b = mi_regen_map(&RG, 1, b); } pts[l][k][0] = a; pts[l][k][1] = b; }
                mi_regen_free(&RG);
                MiFace *Face = mi_face_build(&srf, nl, np, (double (*const *)[2])pts, mc_systol.surf_step_chord_dev, 30.0, mc_systol.chain_tol);   /* the angle: LoadSurf(ent, true, 30.0), MaFace::InitFaceData's 30.0 */
                for (int l = 0; l < nl; l++) free(pts[l]); free(pts); free(np);
                if (Face && mi_face_reparam(Face)) { any_reparam = 1; mi_face_free(Face); Face = NULL; }
                else if (Face) { mi_face_subset(Face, 0.05); sl_driver(&srf, Face, sl_pt, sl_n, sl_spacing, sl_multi, &total); mi_face_free(Face); } else any_trim = 1;
                free(srf.KU); free(srf.KV); free(srf.V); srf_mi_free(&srf);
                continue;
            }
            if (s->kind != 0) { any_trim = 1; continue; }
            ISrf srf; if (mcsc_load_nbs(&F, s, &srf, NULL, NULL, NULL) < 0) { any_trim = 1; continue; }   /* LoadSurf(ent, true, 30.0): MaNURBSurf's DB constructor */
            sl_driver(&srf, NULL, sl_pt, sl_n, sl_spacing, sl_multi, &total);
            free(srf.KU); free(srf.KV); free(srf.V); srf_mi_free(&srf);
        }
    }
    if (sl_join && sl_nall) {   /* JoinCurves(msf_join_tol) after every surface, the tolerance halved to spacing * 0.5 when 0 < spacing <= msf_join_tol (mccore 0x18056e000, msf_join_tol: mccore_systol.c 0.2 metric / 0.01 inch) */
        double jt = mcam_is_metric(&F) ? 0.2 : 0.01; if (sl_spacing != 0.0 && sl_spacing <= jt) jt = sl_spacing * 0.5;
        int nj = mi_join_curves(&sl_all, sl_nall, jt, mcam_is_metric(&F)); total = 0;
        for (int j = 0; j < nj; j++) { int made = 0, out[8]; const char *why = NULL; mcsc_emit_mi(&F, sl_all[j].deg, sl_all[j].ncp, sl_all[j].nk, sl_all[j].k, sl_all[j].p, mc_systol.surf_step_chord_dev, mc_systol.tol, geo_level, geo_color, out, 8, &made, &why); total += made; free(sl_all[j].k); free(sl_all[j].p); }
        free(sl_all); sl_all = NULL; sl_nall = 0; sl_capall = 0;
    }
    /* slice_sel_curves_with_a_plane after the surfaces (FUN_18028d0d0) */
    for (int i = 0; i < n; i++) { Ent en = F.ents[l[i]]; if (en.type == E_LINE || en.type == E_ARC || en.type == E_SPLINE) sl_curve(&en, sl_n, sl_pt, sl_spacing, &hits); }
    mod_end(); free(l); free(ls);
    if (any_reparam) snprintf(cam_msg, sizeof cam_msg, "Curve Slice: a trimmed surface whose face is under 1/1000 of the surface: MaFace::CheckFaceToSurfaceDeltaUVRatios reparameterisation NOT PORTED");
    else if (any_trim) snprintf(cam_msg, sizeof cam_msg, "Curve Slice: a surface kind other than NURBS or trimmed NURBS: NOT PORTED (kinds refused by the kernel build; formats/wireframe.md)");
    else if (total + hits == 0) snprintf(cam_msg, sizeof cam_msg, "No intersections were found");   /* post_warning 0x1575 */
    else { doc_dirty = 1; sl_built = 1; sl_last_sp = sl_spacing; sl_last_off = sl_offset; sl_last_join = sl_join; sl_last_multi = sl_multi; snprintf(cam_msg, sizeof cam_msg, "%s", sl_prompt()); }
    overlay_dirty = 1;
}
void sl_enter_key(void) { if (!sl_built) sl_build(); else { sl_built = 0; sel_clear(); snprintf(cam_msg, sizeof cam_msg, "%s", sl_prompt()); } }
int sl_esc(void) { return 0; }
/* the panel codes FUN_18028d0d0: 0 OK and 1 Apply build when not built; with a build they commit (sktcher_main_color); 0 then leaves, 1 goes back to selecting; 2 Cancel */
int sl_button(int code) {
    if (code == 2) return 0;
    if (code == 0 || code == 1) {
        if (!sl_built) { sl_build(); return code == 0 ? 0 : 1; }   /* no build yet: the build (bVar6 = true), code 0 then leaves */
        sl_built = 0; sel_clear(); snprintf(cam_msg, sizeof cam_msg, "%s", sl_prompt());   /* a build present: UndoTransCommit, sktcher_main_color, selection cleared; code 0 leaves, code 1 goes back to selecting */
        return code == 0 ? 0 : 1;
    }
    if (code == 4) { snprintf(cam_msg, sizeof cam_msg, "Plane selection: get_plane_info (mccore 0x180a2aea0, 1600 decompiled lines) NOT PORTED, the active Cplane is in use"); return 1; }
    return 1;
}
void sl_script(const char *arg) {
    double a, b, c, d, e, f; int k;
    if (sscanf(arg, "plane=%lf,%lf,%lf,%lf,%lf,%lf", &a, &b, &c, &d, &e, &f) == 6) { sl_pt[0] = a; sl_pt[1] = b; sl_pt[2] = c; double l = sqrt(d * d + e * e + f * f); sl_n[0] = d / l; sl_n[1] = e / l; sl_n[2] = f / l; }
    else if (sscanf(arg, "spacing=%lf", &a) == 1) sl_spacing = a;
    else if (sscanf(arg, "offset=%lf", &a) == 1) sl_offset = a;
    else if (sscanf(arg, "join=%d", &k) == 1) sl_join = k;
    else if (sscanf(arg, "multi=%d", &k) == 1) sl_multi = k;
    else if (sscanf(arg, "nurbs=%d", &k) == 1) sl_force_nurbs = k;
    sl_panel_sync();   /* a panel value by script: the same rebuild */
}
/* a panel change rebuilds (FUN_18028d0d0: codes 5..8; every build first UndoTransTerminate the previous one): the live step undone and its redo entry discarded as geo_live_drop does */
void sl_panel_sync(void) {
    if (!sl_built) return;
    if (sl_last_sp == sl_spacing && sl_last_off == sl_offset && sl_last_join == sl_join && sl_last_multi == sl_multi) return;
    if (fabs(sl_offset) > mc_systol.tol) { sl_build(); return; }   /* refused with the message; the live result stays */
    do_undo(); if (nredo) urec_free(&redo_st[--nredo]);
    sl_built = 0; sl_build(); overlay_dirty = 1;
}
void sl_panel_body(const CuiPanelOps *ops, void *u) {
    if (ops->exp(u, "Plane")) { ops->toggle(u, "_Reselect", 1, 4); ops->end(u); }   /* PlaneSelectionButton: code 4 */
    if (ops->exp(u, "_Spacing")) { ops->num(u, "", "slsp", &sl_spacing, NULL, 1, 0, 1e6); ops->end(u); }
    if (ops->exp(u, "_Offset")) { ops->num(u, "", "sloff", &sl_offset, NULL, 1, -1e6, 1e6); ops->end(u); }
    if (ops->exp(u, "Settings")) { ops->check(u, "_Join curves into entity", &sl_join, 0); ops->check(u, "_Find multiple solutions", &sl_multi, 1); ops->end(u); }
}
