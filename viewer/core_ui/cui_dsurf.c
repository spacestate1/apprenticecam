/* cui_dsurf.c - dyno_surf (mccore 0x1803210c0): the arrow on a surface, moved with the cursor, used by Curve Dynamic (mode 5: collect points, FUN_180282390 sketcher_curve_4)
 * and Curve Constant Parameter (mode 3: one position, FUN_1802980f0 sketcher_curve_2); Curve One Edge's arrow is mode 3 without the AutoCursor (param_4 0).
 * Source: decomp/curves/core1.c dyno_surf; dyno_get_event (0x180320e80); hit_polygon / in_polygon / hold_uv_to_trim (decomp/curves/ed_core1.c, core1.c); formats/wireframe.md "Curves on surfaces".
 * What is ported of it: the screen delta -> (du, dv) through the projected tangents (v_lincoeffs 0x1804b8900), wrap on closed directions, the trim polygon hold, the clamped uv, the points of mode 5, the Enter / Cancel exit.
 * The events: with the AutoCursor (param_4 1: get_event mask + 1) a click on a locked AutoCursor point is event 1 (SetupPointEvent) -> 0xf with that point, any other click event 4 (SetupSketchEvent) -> 0xe (get_event button handling, decomp/getevent/mccore_getevent_msgs.c:668-740, FUN_1804e27d0); a move with the point locked (DAT_1813f6049) reports it (dyno_get_event core1.c:4055-4064). cui_geo.c runs the AutoCursor on each move (a locked point = geo_snap_ent, not the grid);
 * project_pt_onto_surf (FUN_1805ed4a0 not read) is mcsc_uv_of_point; the fill-drag branch (event 2 / 0x2000, cVar7) is not ported: it needs a solid face under the cursor (local_2c8c), and the callers here pass param_10 = param_11 = 0 (the condition then keeps cVar7 at 0 without a solid). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "../mcsurf.h"
#include "../mcsrfcrv.h"

const Surf *ds_surf(int e) { return e == DS_TEMP ? ce_temp_surf : &F.surfs[F.ents[e].srf]; }   /* DS_TEMP: a solid face taken as a surface (cui_cedge.c ce_face_temp; FUN_180282390 / FUN_1802980f0 event 0x2000) */

#define DS_MAXPTS 499   /* param_5[1] = 1500 doubles (FUN_180282390 local_bbd8[1] = 1500.0, mode 1 = xyz): local_3030 = 1500 - 3 = 1497, index 2 step 3 while below it: 499 points */

static struct {
    int on, mode, p4, ent, first;   /* first: no screen position yet (before the dyno_get_event preceding the loop) */
    double uv[2], uu[2], tu[2], tv[2], P[3], N[3];
    int cv6, inside, has_frame;    /* cv6: "the cursor moved" cVar6 at the top of the loop (1 before the first iteration); inside: local_2ff8 (1 at the start: the trim hold applies from inside) */
    int npts, last_code;
    double pts[DS_MAXPTS][3];
    double last_snap[3]; int have_last_snap;
} D;

int ds_running(void) { return D.on; }
int ds_autocursor(void) { return D.on && D.p4; }   /* param_4: dyno_get_event asks for the AutoCursor (mask | 1) */

/* in_polygon (0x18046c960): the loops of a trimmed surface record: 1 inside, 0 on, -1 outside (the 1e-10 edge test and the odd-even count of the decompile) */
static int ds_in_polygon(const uint8_t *r, uint32_t rn, double u, double v) {
    int m, nl; memcpy(&m, r + 0x10c, 4); memcpy(&nl, r + 0x128, 4);
    if (m <= 0 || nl <= 0 || 0x138 + 16 * (size_t)m + 8 * (size_t)nl > rn) return 1;
    const uint8_t *t = r + 0x138; int odd = 0;
    for (int l = 0; l < nl; l++) {
        int32_t st, cn; memcpy(&st, t + 16 * (size_t)m + 8 * l, 4); memcpy(&cn, t + 16 * (size_t)m + 8 * l + 4, 4);
        double p0[2]; memcpy(p0, t + 16 * (size_t)(st + cn), 16);
        for (int i = st; i <= st + cn; i++) {
            double p1[2]; memcpy(p1, t + 16 * (size_t)i, 16);
            double dy = p1[1] - p0[1];
            if (1e-10 <= dy || 1e-10 <= -dy) {
                double v2 = v - 1e-10;
                if ((p0[1] < v2 && v2 <= p1[1]) || (p1[1] <= v2 && v2 < p0[1])) {
                    double x = (v - p0[1]) * (p1[0] - p0[0]) / dy + p0[0], d = u - x;
                    if (d < 1e-10 && -d < 1e-10) return 0;
                    if (u < x) odd ^= 1;
                }
            } else {
                double d = p0[1] - v;
                if (d < 1e-10 && -d <= 1e-10 && -d != 1e-10 && ((u - 1e-10 <= p0[0] && p1[0] <= u + 1e-10) || (p0[0] <= u + 1e-10 && u - 1e-10 <= p1[0]))) return 0;
            }
            p0[0] = p1[0]; p0[1] = p1[1];
        }
    }
    return odd ? 1 : -1;
}
/* hit_polygon (0x18046be80) with param_4 '\0' (the hit snapped to the end along the edge): every crossing of (a -> b) with the loops; hold_uv_to_trim (0x1804c41d0, param_2 0, param_3 1) keeps the hit nearest to a */
static int ds_hold(const uint8_t *r, uint32_t rn, const double a[2], const double b[2], double o[2]) {
    int m, nl; memcpy(&m, r + 0x10c, 4); memcpy(&nl, r + 0x128, 4); o[0] = b[0]; o[1] = b[1];
    if (m <= 0 || nl <= 0 || 0x138 + 16 * (size_t)m + 8 * (size_t)nl > rn) return 0;
    const uint8_t *t = r + 0x138; double dx = b[0] - a[0], dy = b[1] - a[1], H[0x100][2]; int nh = 0;
    for (int l = 0; l < nl; l++) {
        int32_t st, cn; memcpy(&st, t + 16 * (size_t)m + 8 * l, 4); memcpy(&cn, t + 16 * (size_t)m + 8 * l + 4, 4);
        double q0[2]; memcpy(q0, t + 16 * (size_t)(st + cn), 16); double px = q0[0] - a[0], py = q0[1] - a[1], c0 = py * dx - px * dy;
        for (int i = st; i <= st + cn; i++) {
            double q[2]; memcpy(q, t + 16 * (size_t)i, 16);
            double ex = q[0] - a[0], ey = q[1] - a[1], c1 = ey * dx - ex * dy;
            if (((c1 <= 0 && 0 <= c0) || (0 <= c1 && c0 <= 0)) && 1e-12 < fabs(c1 - c0)) {
                double s = (ey * px - ex * py) / (c1 - c0);
                if (-1e-12 < s && s <= 1.000000000001) {
                    double wx = ex - px, wy = ey - py, d2 = (dx - px) * wx + (dy - py) * wy, w2 = wx * wx + wy * wy, f = d2 / w2, hx, hy;
                    if (1e-12 < f) { if (0.999999999999 <= f) { hx = q[0]; hy = q[1]; } else { hx = q[0] - wx * (1 - f); hy = q[1] - (1 - f) * wy; } }
                    else { hx = px + a[0]; hy = py + a[1]; }
                    int dup = 0; for (int k = 0; k < nh; k++) if (sqrt((H[k][0] - hx) * (H[k][0] - hx) + (H[k][1] - hy) * (H[k][1] - hy)) < 1e-06) { dup = 1; break; }
                    if (!dup) { H[nh][0] = hx; H[nh][1] = hy; if (++nh == 0x100) return 0; }
                }
            }
            px = ex; py = ey; c0 = c1;
        }
    }
    if (!nh) return 0;
    int bi = 0; double bd = hypot(a[0] - H[0][0], a[1] - H[0][1]);
    for (int k = 1; k < nh; k++) { double d = hypot(a[0] - H[k][0], a[1] - H[k][1]); if (d <= bd && bd != d) { bd = d; bi = k; } }
    o[0] = H[bi][0]; o[1] = H[bi][1]; return 1;
}

/* the body at the top of the do loop, with sVar10 = code (0 motion, 0xe / 0xf click). cVar7 = 0 in these callers, the event-2 branch stays out. */
static void ds_head(int code, const double *snap) {
    const Surf *s = ds_surf(D.ent);
    if (D.cv6 || (D.mode > 3 && (code == 0xe || code == 0xf))) {
        if (D.p4 && D.mode >= 4 && code == 0xf && snap) { double u, v; if (mcsc_uv_of_point(&F, s, snap, &u, &v)) { D.uv[0] = u; D.uv[1] = v; D.cv6 = 1; } else D.cv6 = 0; }   /* FUN_1803204b0 -> project_pt_onto_surf */
        double Su[3], Sv[3], N[3], P[3];
        if (!mcsc_surf_frame(&F, s, D.uv[0], D.uv[1], P, Su, Sv, N)) return;
        memcpy(D.P, P, sizeof P); memcpy(D.N, N, sizeof N); D.has_frame = 1;
        if (D.mode == 5 && (code == 0xe || code == 0xf) && D.npts < DS_MAXPTS) memcpy(D.pts[D.npts++], P, sizeof P);   /* mode 5: add_repaint_pt + the stored S(u, v) */
        /* proj_iso_pt of the tangents: their view components; a zero one is replaced by (0.1, 0) */
        D.tu[0] = vmat[0] * Su[0] + vmat[1] * Su[1] + vmat[2] * Su[2]; D.tu[1] = vmat[3] * Su[0] + vmat[4] * Su[1] + vmat[5] * Su[2];
        D.tv[0] = vmat[0] * Sv[0] + vmat[1] * Sv[1] + vmat[2] * Sv[2]; D.tv[1] = vmat[3] * Sv[0] + vmat[4] * Sv[1] + vmat[5] * Sv[2];
        if (sqrt(D.tu[0] * D.tu[0] + D.tu[1] * D.tu[1]) < 1e-12) D.tu[0] = 0.1;
        if (sqrt(D.tv[0] * D.tv[0] + D.tv[1] * D.tv[1]) < 1e-12) D.tv[0] = 0.1;
        overlay_dirty = 1;
    }
}
/* the part after the next dyno_get_event: the screen delta, the new (u, v). code = the new event's. cVar6 is set here. */
static void ds_tail(int code, const double uu[2], const double *ac) {
    const Surf *s = ds_surf(D.ent);
    double dx = uu[0] - D.uu[0], dy = uu[1] - D.uu[1]; D.uu[0] = uu[0]; D.uu[1] = uu[1];
    D.cv6 = mc_systol_units(mcam_is_metric(&F))->tol < fabs(dx) + fabs(dy);
    if (D.cv6 || (D.mode > 3 && (code == 0xe || code == 0xf))) {
        /* a move on a locked AutoCursor point (param_4, DAT_1813f6049, code != 0xf; core1.c:758-785): the point projected on the surface, when that fails LAB_180321f08: v_lincoeffs */
        double t1 = D.tu[0], t2 = D.tu[1], w1 = D.tv[0], w2 = D.tv[1];
        double a = t1 * t1 + t2 * t2, b = w1 * t1 + w2 * t2, c = w1 * w1 + w2 * w2, e = dx * w1 + dy * w2, g = dx * t1 + dy * t2, det = c * a - b * b;
        if (fabs(det) < 1e-12) det = det <= 0 ? -1e-12 : 1e-12;
        double du = (g * c - e * b) / det, dv = (e * a - g * b) / det, nu = D.uv[0] + du, nv = D.uv[1] + dv, uv0[2] = { D.uv[0], D.uv[1] }, pu, pv;
        if (D.p4 && ac && code != 0xf && mcsc_uv_of_point(&F, s, ac, &pu, &pv)) { nu = pu; nv = pv; }
        double d[4]; mcs_surf_domain(s, d); uint8_t fl = s->rec[0x6e];   /* surf_type +6 */
        if (fl & 1) { while (nu < d[0]) nu += d[1] - d[0]; while (nu > d[1]) nu -= d[1] - d[0]; }
        if (fl & 2) { while (nv < d[2]) nv += d[3] - d[2]; while (nv > d[3]) nv -= d[3] - d[2]; }
        if (s->kind == 10) {
            int r = ds_in_polygon(s->rec, s->n, nu, nv), inn = r;
            if (r == -1) {
                if (D.inside == 1) { double a2[2] = { uv0[0], uv0[1] }, b2[2] = { nu, nv }, o[2]; ds_hold(s->rec, s->n, a2, b2, o); D.uv[0] = o[0]; D.uv[1] = o[1]; }
                else { D.uv[0] = uv0[0]; D.uv[1] = uv0[1]; }
                inn = 0;
            } else { D.uv[0] = nu; D.uv[1] = nv; }
            D.inside = inn;
        } else {
            D.uv[0] = nu; D.uv[1] = nv;
            if (D.uv[0] < d[0]) D.uv[0] = d[0]; if (D.uv[0] > d[1]) D.uv[0] = d[1];
            if (D.uv[1] < d[2]) D.uv[1] = d[2]; if (D.uv[1] > d[3]) D.uv[1] = d[3];
        }
    }
}

void ds_start(int ent, int mode, int p4, double u, double v) {
    memset(&D, 0, sizeof D); D.on = 1; D.mode = mode; D.p4 = p4; D.ent = ent; D.uv[0] = u; D.uv[1] = v; D.first = 1; D.cv6 = 1; D.inside = 1; overlay_dirty = 1;
}
/* dyno_surf's exit: mode 3 returns (u, v) (a click 0xf with the AutoCursor on projects its point, param_4 only); mode 5 the points. code: the event that ended it */
static void ds_finish(int code, const double *snap) {
    if (D.mode == 3 && D.p4 && code == 0xf && snap) { double u, v; const Surf *s = ds_surf(D.ent); if (mcsc_uv_of_point(&F, s, snap, &u, &v)) { D.uv[0] = u; D.uv[1] = v; } }
    D.on = 0; overlay_dirty = 1;
}
/* one event: code 0 motion (snap = the locked AutoCursor point, NULL when none), 0xe a click (plain), 0xf a click on the AutoCursor (snap = its point), 0xb Enter / Cancel */
int ds_event(int code, const double uu[2], const double *snap) {
    if (!D.on) return 0;
    if (D.first) { D.uu[0] = uu ? uu[0] : 0; D.uu[1] = uu ? uu[1] : 0; D.first = 0; }   /* the first dyno_get_event (before the loop): scrn_to_uu of it */
    else if (uu) ds_tail(code, uu, snap);
    D.last_code = code;
    if (code == 0xb) { ds_finish(code, snap); return 1; }
    if (code != 0 && D.mode < 4) { ds_finish(code, snap); return 1; }   /* "if ((sVar10 != 0) && (sVar8 < 4)) break;" */
    ds_head(code, snap);
    return 0;
}
int ds_debug_poly(void) { const Surf *s = ds_surf(D.ent); return s->kind == 10 ? ds_in_polygon(s->rec, s->n, D.uv[0], D.uv[1]) : 99; }   /* tests */
int ds_last_aborted(void) { return D.last_code == 0xb; }
int ds_uv(double *u, double *v) { *u = D.uv[0]; *v = D.uv[1]; return 1; }
int ds_point_count(void) { return D.npts; }
const double (*ds_points(void))[3] { return (const double (*)[3])D.pts; }

/* the screen as the view-plane point (proj_iso_pt of the construction-plane cursor equals the view axes' components of the ray's point): dyno_get_event's scrn_to_uu */
void ds_uu(const double raw[3], double uu[2]) { uu[0] = vmat[0] * raw[0] + vmat[1] * raw[1] + vmat[2] * raw[2]; uu[1] = vmat[3] * raw[0] + vmat[4] * raw[1] + vmat[5] * raw[2]; }

/* the arrow (GrTemp_AddArrow, size 0.1) at the current position: the same style as cui_surf.c srf_ov; the placed points of mode 5 (add_repaint_pt: ours small crosses) */
void ds_overlay(float **pv, int *pn, int *pcap) {
    if (!D.on || !D.has_frame) return;
    float rgb[3]; mc_rgb(cfg_back_color == 2 || cfg_back_color == 10 ? 4 : 0x56, rgb);
    double L = scene_view_height() * 0.1, h = L * 0.2; const double *p = D.P, *n = D.N;
    V3 a = { p[0], p[1], p[2] }, t = { p[0] + n[0] * L, p[1] + n[1] * L, p[2] + n[2] * L };
    ov_pt(pv, pn, pcap, a, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, t, rgb[0], rgb[1], rgb[2]);
    double e[3] = { 1, 0, 0 }; if (fabs(n[0]) > 0.9) { e[0] = 0; e[1] = 1; }
    double s[3] = { n[1] * e[2] - n[2] * e[1], n[2] * e[0] - n[0] * e[2], n[0] * e[1] - n[1] * e[0] }; double l = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); if (l > 0) for (int k = 0; k < 3; k++) s[k] /= l;
    V3 b1 = { t.x - n[0] * h + s[0] * h * 0.4, t.y - n[1] * h + s[1] * h * 0.4, t.z - n[2] * h + s[2] * h * 0.4 }, b2 = { t.x - n[0] * h - s[0] * h * 0.4, t.y - n[1] * h - s[1] * h * 0.4, t.z - n[2] * h - s[2] * h * 0.4 };
    ov_pt(pv, pn, pcap, t, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, b1, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, t, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, b2, rgb[0], rgb[1], rgb[2]);
    for (int i = 0; i < D.npts; i++) { double h2 = h * 0.2; V3 q = { D.pts[i][0], D.pts[i][1], D.pts[i][2] }, q1 = q, q2 = q; q1.x -= h2; q2.x += h2; ov_pt(pv, pn, pcap, q1, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, q2, rgb[0], rgb[1], rgb[2]); q1 = q2 = q; q1.y -= h2; q2.y += h2; ov_pt(pv, pn, pcap, q1, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, q2, rgb[0], rgb[1], rgb[2]); }
}

/* ------------------------------------------------------------------------- Curve Dynamic (G_CDYN, sketcher_curve_4, mccad FUN_180282390) */
int srf_surface_type(void);   /* cui_surf.c: config 10014 */
int sp_fit_points(const double (*Q)[3], int n);   /* cui_spline.c: make_spline_from_pts / create_nurbs_from_points with natural ends */
static double cd_p[DS_MAXPTS][3]; static int cd_n;   /* the last points (kept for the panel's rebuilds: cVar6 stays 1 after the first dyno_surf, FUN_180282390 runs it once) */
int cdyn_active(void) { return geo_cmd == G_CDYN && D.on; }
/* the surface pick (event 2): the arrow starts at the pick's (u, v) (the event's p_2d; ours: mcsc_uv_of_point of the point the click returned) */
int cdyn_take(int e, V3 w) {
    if (e == DS_TEMP && !ce_temp_surf->rec) return 0;
    if (e != DS_TEMP && (e < 0 || e >= F.nents || F.ents[e].type != E_SURF)) return 0;
    const Surf *s = ds_surf(e);
    double P[3] = { w.x, w.y, w.z }, u, v, f[3], n[3], m[3];
    if (!mcsc_uv_of_point(&F, s, P, &u, &v) || !mcsc_surf_frame(&F, s, u, v, f, n, m, n)) { snprintf(cam_msg, sizeof cam_msg, "Curve Dynamic: this surface kind (%d) is not supported by the evaluators (NOT PORTED)", s->kind); return 0; }
    ds_start(e, 5, 1, u, v);
    return 1;
}
/* Curve Constant Parameter (FUN_1802980f0): the surface pick starts dyno_surf mode 3, param_4 1 at the pick's (u, v) (the same start as Curve Dynamic's) */
int srf_crv_edge_begin(int e, V3 w, char *msg, size_t msgn) {
    const Surf *s = ds_surf(e); double P[3] = { w.x, w.y, w.z }, u, v;
    if (!mcsc_uv_of_point(&F, s, P, &u, &v)) { snprintf(msg, msgn, "Curve One Edge: the surface has no parameter range"); return 0; }
    ds_start(e, 3, 0, u, v); return 1;   /* FUN_1802a2940: dyno_surf mode 3, param_4 '\0' */
}
int srf_crv_const_begin(int e, V3 w, char *msg, size_t msgn) {
    if (e == DS_TEMP && !ce_temp_surf->rec) return 0;
    const Surf *s = ds_surf(e); double P[3] = { w.x, w.y, w.z }, u, v;
    if (!mcsc_uv_of_point(&F, s, P, &u, &v)) { snprintf(msg, msgn, "Curve Constant Parameter: the surface has no parameter range"); return 0; }
    ds_start(e, 3, 1, u, v); return 1;
}
/* the points of the finished dyno_surf (Enter / Cancel): at least two make a spline (FUN_180282390 "1 < sVar8": fewer: no spline, no message) */
int cdyn_build(void) {
    if (cd_n < 2) return -1;
    return sp_fit_points((const double (*)[3])cd_p, cd_n);
}
void cdyn_collect(void) { cd_n = D.npts; memcpy(cd_p, D.pts, sizeof cd_p); }
int cdyn_points(void) { return cd_n; }
