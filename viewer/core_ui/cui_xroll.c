/* cui_xroll.c - Transform > Roll (MCCADUTIL do_xform_roll @180030940, dialog 6467 / 6468, parameters DAT_1802433a0 over mccore
 * roll_x / roll_y / unroll_x / unroll_y). Spec: ../../formats/wireframe.md "Roll - port spec (2026-11-04)" (every rule below is
 * cited there: MC = decomp/xform/mccadutil_xform.c, R / RB / RC = roll.c / roll_b.c / roll_c.c, CR = mccore_roll.c).
 *
 * A chain command of the wireframe framework (cui_geo.c, as Offset Chains / do_xform_offset_ctour): chain_it_point(3941), the
 * prompt 1523 with a click on a chain reversing it, the preview rebuilt on every change (panel code 0x66), OK commits.
 *
 * Ours / not ported: the Cplane is Top with the origin at 0 (the other transforms here alike); Type = Splines (make_spline_from_pts /
 * create_nurbs_from_points: no spline fitting here) refused with a message; the button "Select an arc" (721) greyed; "Select two
 * points" (829) ported (xrl_pick); the cylinder outline (FUN_18002c850) not drawn; the Ctrl-held seam split, the
 * b <= 0 screen-scale step and the toolpath-association question left out (spec "Port mapping"). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "../mcsurf.h"
#include "../mcsurf_build.h"

#define RL_TOL (mc_systol.tol)                              /* tol_exref (system tolerance: config 1000, mcam_load.h mc_systol) */
/* P (DAT_1802433a0): .data initial values; dia shows P+0x08 x 2 (combo 825) */
/* 829 "Select two points" (FUN_18002ab10, decomp/xform/roll.c l.663-760): get_event(point) with GetCoreString(0xf59) = mccoreres 3929
   "Select the point to translate from", then GetCoreString(0xf5a) = 3930 - empty in mccoreres, so MFC's string search takes the first
   module of the resource chain whose 3930 is not empty: mccadutilres "Select the point to translate to", opmanres, _axui_res and
   mwexceptions_res all have one (tools/pe_strings.py) and the chain's order is NOT DECODED (left empty here); both picked: P1 / P2 :=
   them (dlg +0x104d0 / +0x104e8), +0x10500 = 1 (enables 801), positioning := between points (+0xc908 = 1), FUN_18000bd80(0x66); an Esc
   (get_event 2) posts 100 = the dialog's Cancel */
int xrl_pick, xrl_pts_ok;
XRoll xrl = { .mode = 1, .roll = 1, .axisx = 1, .cw = 1, .dia = 2.0, .btol = 0.5, .ang = 1e38, .type = 0, .level = -1, .color = -1 };

/* the per-run reset (MC l.642-653): Roll, r = 0, angle 1e38, angle positioning, the points, Type 0, Use New Attributes off, level /
   colour -1; Move / Copy, the axis, the direction and the angle tolerance are kept */
void xroll_reset(void) {
    xrl.roll = 1; xrl.dia = 0; xrl.ang = 1e38; xrl.twopt = 0; memset(xrl.p1, 0, sizeof xrl.p1); memset(xrl.p2, 0, sizeof xrl.p2);
    xrl.type = 0; xrl.useattr = 0; xrl.level = -1; xrl.color = -1;
}

/* the point map M (FUN_180030150, RB l.200; roll_x / roll_y CR l.292 / 342, unroll_x / unroll_y CR l.392 / 504) in the roll frame */
typedef struct { double r, a, s_roll; int roll, axisx; double D[3]; double A; } RlCtx;
static void rl_map(RlCtx *k, const double p[3], double o[3]) {
    double r = k->r;
    if (k->roll) {
        double q[3] = { p[0] + k->D[0], p[1] + k->D[1], p[2] + k->D[2] };
        if (r < 1e-12) { memcpy(o, q, sizeof q); return; }
        if (k->axisx) { double t = k->s_roll * (q[1] + r * k->a) / r; o[0] = q[0]; o[1] = (q[2] + r) * cos(t); o[2] = (q[2] + r) * sin(t); }
        else { double t = k->s_roll * (q[0] + r * k->a) / r; o[0] = (q[2] + r) * cos(t); o[1] = q[1]; o[2] = (q[2] + r) * sin(t); }
        return;
    }
    double s_un = -k->s_roll, c1 = k->axisx ? p[1] : p[0];
    double rho = sqrt(c1 * c1 + p[2] * p[2]), f = atan2(c1, p[2]) - M_PI / 2;
    if (k->A < 1e38) { while (f <= k->A - M_PI) f += 2 * M_PI; while (f > k->A + M_PI) f -= 2 * M_PI; }   /* into (A - pi, A + pi] */
    k->A = f;
    double u = s_un * r * f, w = rho - r;
    if (fabs(u) < 1e-12) u = 0; if (fabs(w) < 1e-12) w = 0;
    u -= r * k->a;
    if (k->axisx) { o[0] = p[0]; o[1] = u; o[2] = w; } else { o[0] = u; o[1] = p[1]; o[2] = w; }
    o[0] += k->D[0]; o[1] += k->D[1]; o[2] += k->D[2];
}
/* FUN_1805faf10: atan2 in [0, 2 pi) with the axis cases (spec "Sample counts") */
static double rl_ang(double z, double x) {
    if (fabs(z) < 1e-12) return x >= 0 ? 0 : M_PI;
    if (fabs(x) < 1e-12) return z > 0 ? M_PI / 2 : 3 * M_PI / 2;
    double w = atan2(z, x); return w < 0 ? w + 2 * M_PI : w;
}
/* n_segs_to_wrap_line (CR l.192) / n_segs_to_unwrap_line (CR l.69): the sample count of a raw segment (no D); b = P+0x10 */
static int rl_nsegs(const RlCtx *k, double b, const double a[3], const double c[3]) {
    double dx = c[0] - a[0], dy = c[1] - a[1];
    if (k->roll) {
        double d = k->axisx ? dy : dx;
        if ((fabs(dx) < RL_TOL && fabs(dy) < RL_TOL) || fabs(d) < RL_TOL) return 1;
        double step = b * k->r * M_PI / 180; if (!(step > 0)) return 1;
        long n = labs((long)trunc(d / step)); return n < 1 ? 1 : (int)fmin(n, 1e6);
    }
    double w1 = k->axisx ? rl_ang(a[2], a[1]) : rl_ang(a[2], a[0]), w2 = k->axisx ? rl_ang(c[2], c[1]) : rl_ang(c[2], c[0]);
    if (fabs(w2 - w1) > M_PI) w2 += 2 * M_PI;              /* literal (CR l.69) */
    if (!(b > 0)) return 1;
    long n = labs((long)trunc((w2 - w1) / b)); return n < 1 ? 1 : (int)fmin(n, 1e6);   /* b as radians here (literal) */
}
/* check_colinear (mccore 0x1806665c0, decomp/surfprim/linuv2.c l.158): a step <= tol, or cos >= 0.99985 with the middle point within
   2 tol of the line */
static int rl_colinear(const double *p1, const double *p2, const double *p3) {
    double u[3] = { p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2] }, v[3] = { p3[0] - p2[0], p3[1] - p2[1], p3[2] - p2[2] };
    double lu = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]), lv = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (lu <= RL_TOL || lv <= RL_TOL) return 1;
    if ((u[0] * v[0] + u[1] * v[1] + u[2] * v[2]) / (lu * lv) < 0.99985) return 0;
    double w[3] = { p3[0] - p1[0], p3[1] - p1[1], p3[2] - p1[2] }, lw = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (lw <= 0) return 1;
    double cx = u[1] * w[2] - u[2] * w[1], cy = u[2] * w[0] - u[0] * w[2], cz = u[0] * w[1] - u[1] * w[0];
    return sqrt(cx * cx + cy * cy + cz * cz) / lw < RL_TOL * 2;
}

/* the node run of one source entity (CSplineNodeMgr): 1000 nodes per run for lines / points (RC l.257-272) */
typedef struct { double (*p)[3]; int n, cap; int lv, co, type; } RlRun;
static int rl_made;
static void rl_out(RlRun *u) {                            /* FUN_18002ce80 (RC l.309): the run's entities */
    V3 z0 = { 0, 0, 0 };
    if (u->type == 1) for (int i = 0; i < u->n; i++) { V3 q = { u->p[i][0], u->p[i][1], u->p[i][2] }; if (mcam_new_ent(&F, E_POINT, q, q, z0, 0, 0, 0, u->lv, u->co) >= 0) rl_made++; }
    else for (int i = 0; i + 1 < u->n; i++) { V3 a = { u->p[i][0], u->p[i][1], u->p[i][2] }, b = { u->p[i + 1][0], u->p[i + 1][1], u->p[i + 1][2] };
        if (mcam_new_ent(&F, E_LINE, a, b, z0, 0, 0, 0, u->lv, u->co) >= 0) rl_made++; }
    u->n = 0;
}
static void rl_push(RlRun *u, const double q[3]) {
    if (u->n && fabs(u->p[u->n - 1][0] - q[0]) < RL_TOL && fabs(u->p[u->n - 1][1] - q[1]) < RL_TOL && fabs(u->p[u->n - 1][2] - q[2]) < RL_TOL) return;   /* pt_equal */
    if (u->type != 1 && u->n >= 2 && rl_colinear(u->p[u->n - 2], u->p[u->n - 1], q)) { memcpy(u->p[u->n - 1], q, sizeof(double) * 3); return; }   /* merged */
    if (u->n == u->cap) { u->cap = u->cap ? u->cap * 2 : 64; u->p = realloc(u->p, sizeof *u->p * u->cap); }
    memcpy(u->p[u->n++], q, sizeof(double) * 3);
    if (u->n >= 1000) { double last[3]; memcpy(last, u->p[u->n - 1], sizeof last); rl_out(u); rl_push(u, last); }   /* flushed, restarted at the point */
}
/* wrap_line_as_arc (CR l.616): the exact arc of a segment along the wrap direction at constant Z (check_wrap_line_as_arc CR l.3) */
static int rl_wrap_arc(const RlCtx *k, const double a0[3], const double b0[3], int lv, int co, int dry) {
    double dx = b0[0] - a0[0], dy = b0[1] - a0[1], dz = b0[2] - a0[2];
    if (k->axisx ? !(fabs(dx) < RL_TOL && fabs(dy) > RL_TOL) : !(fabs(dy) < RL_TOL && fabs(dx) > RL_TOL)) return 0;
    if (fabs(dz) >= RL_TOL) return 0;
    double a[3] = { a0[0] + k->D[0], a0[1] + k->D[1], a0[2] + k->D[2] }, b[3] = { b0[0] + k->D[0], b0[1] + k->D[1], b0[2] + k->D[2] };
    double rr = a[2] + k->r; if (!(rr > RL_TOL)) return 0;
    if (dry) return 1;
    int view = k->axisx ? 5 : 2;                          /* find_view_k: normal (1, 0, 0) -> 5 Right, (0, -1, 0) -> 2 Front */
    const double *m = cam_plane_matrix(view);
    double st, sw; V3 c;
    if (k->axisx) { c = (V3){ a[0], 0, 0 }; st = k->s_roll * (a[1] / k->r + k->a); sw = k->s_roll * (b[1] - a[1]) / k->r; }
    else { c = (V3){ 0, a[1], 0 }; st = k->s_roll * (a[0] / k->r + k->a); sw = k->s_roll * (b[0] - a[0]) / k->r; }
    if (sw < 0) { st += sw; sw = -sw; }                   /* ours: a negative sweep's storage is NOT DECODED - the same arc, reversed */
    st = fmod(st, 2 * M_PI); if (st < 0) st += 2 * M_PI;
    double vm[9]; memcpy(vm, m, sizeof vm);
    if (mcam_new_arc_view_w(&F, view, vm, c, rr, st, sw, lv, co) < 0) return 0;
    rl_made++; return 1;
}
/* break2lines FUN_18002d040 (RC l.1): the segment's samples through M into the run */
static void rl_seg(RlCtx *k, RlRun *u, double b, const double a[3], const double c[3]) {
    if (k->roll && u->type == 0 && rl_wrap_arc(k, a, c, u->lv, u->co, 1)) {
        rl_out(u);                                        /* the pending nodes are flushed first */
        if (rl_wrap_arc(k, a, c, u->lv, u->co, 0)) return;
    }
    int n = rl_nsegs(k, b, a, c);
    for (int i = 0; i <= n; i++) {
        double p[3] = { a[0] + i * (c[0] - a[0]) / n, a[1] + i * (c[1] - a[1]) / n, a[2] + i * (c[2] - a[2]) / n }, q[3];
        rl_map(k, p, q); rl_push(u, q);
    }
}

/* the preview build (FUN_18002bf70 MC l.10747) over the chains; returns the entities made, -1 refused (msg) */
int xroll_run(const Chain *ch, int nch, int main_lv, int main_co, char *msg, size_t msgn) {
    double r = xrl.dia * 0.5;
    if (r < RL_TOL) { snprintf(msg, msgn, "Roll: set the rotary diameter"); return 0; }   /* nothing below tol (FUN_18002bf70) */
    if (xrl.type == 2) { snprintf(msg, msgn, "Roll: Type Splines (make_spline_from_pts / create_nurbs_from_points) is NOT PORTED - no wireframe splines are made here"); return -1; }
    const McSysTol *st = mc_systol_units(mcam_is_metric(&F));
    double chord = st->curve_chord_dev * 0.1;             /* break_spline_const_error_lines(ent, curve_chord_dev x 0.1) */
    RlCtx k = { r, xrl.ang >= 1e38 ? 0 : xrl.ang * M_PI / 180, xrl.cw ? -1.0 : 1.0, xrl.roll, xrl.axisx, { 0, 0, 0 }, 1e38 };
    if (fabs(k.a - 2 * M_PI) < 1e-6) k.a = 0;
    if (xrl.twopt) {                                      /* FUN_18002ef50 (RB l.375): D, the angle 0 */
        const double *P1 = xrl.p1, *P2 = xrl.p2; k.a = 0;
        if (!k.roll && k.axisx) { k.D[0] = P2[0] - P1[0]; k.D[1] = (atan2(P1[2], P1[1]) - M_PI / 2) * r + P2[1]; }
        else if (!k.roll) { k.D[0] = (atan2(P1[2], P1[0]) - M_PI / 2) * r + P2[0]; k.D[1] = P2[1] - P1[1]; }
        else if (k.axisx) { k.D[0] = P2[0] - P1[0]; k.D[1] = -((atan2(P2[2], P2[1]) - M_PI / 2) * r + P1[1]); }
        else { k.D[0] = -((atan2(P2[2], P2[0]) - M_PI / 2) * r + P1[0]); k.D[1] = P2[1] - P1[1]; }
    }
    rl_made = 0;
    RlRun u = { 0 }; u.type = xrl.type;
    int lvn = xrl.level < 1 ? main_lv : xrl.level, con = xrl.color < 0 ? main_co : xrl.color;
    for (int ci = 0; ci < nch; ci++) {
        const Chain *c = &ch[ci]; k.A = 1e38;                /* FUN_18002df50: the unroll angle memory per chain */
        for (int j = 0; j < c->n; j++) {
            int e = c->ent[j], rv = c->rev[j]; if (e < 0 || e >= F.nents) continue;
            const Ent *en = &F.ents[e];
            u.lv = xrl.useattr ? lvn : en->level; u.co = xrl.useattr ? con : en->color;
            if (en->type == E_POINT) {                        /* FUN_18002e190: always a point */
                double p[3] = { en->a.x, en->a.y, en->a.z }, q[3]; rl_map(&k, p, q);
                V3 qq = { q[0], q[1], q[2] }, z0 = { 0, 0, 0 };
                if (mcam_new_ent(&F, E_POINT, qq, qq, z0, 0, 0, 0, u.lv, u.co) >= 0) rl_made++;
                continue;
            }
            double (*pts)[3] = NULL; int np = 0;
            if (en->type == E_LINE) { pts = malloc(sizeof *pts * 2); pts[0][0] = en->a.x; pts[0][1] = en->a.y; pts[0][2] = en->a.z; pts[1][0] = en->b.x; pts[1][1] = en->b.y; pts[1][2] = en->b.z; np = 2; }
            else if (en->type == E_ARC || en->type == E_SPLINE) {
                /* break_spline_const_error_lines (@180062aa0): brk_spline_const_err from ent +0x98 to +0xa0; a segment kept when longer
                   than tol, one colinear with the last extends it */
                uint32_t rn = 0; uint8_t *rec = NULL; CurveRec cr; const CurveRec *cp = NULL;
                if (en->type == E_ARC) { rec = mcsb_arc_curve_rec(&F, en, RL_TOL, &rn); if (rec) { cr = (CurveRec){ 0, 0x80, rec, rn }; cp = &cr; } }
                else if (en->crv >= 0 && en->crv < F.ncrvs) cp = &F.crvs[en->crv];
                if (!cp || !mcs_spline_ok(cp)) { free(rec); snprintf(msg, msgn, "Roll: an entity could not be broken into lines"); continue; }
                double t1, t2, t, o[3][3]; mcs_spline_t1t2(cp, &t1, &t2); t = t1;
                int cap = 64; pts = malloc(sizeof *pts * cap);
                V3 s0 = en->type == E_ARC ? mcam_arc_pt(en, en->a0) : en->a, s1 = en->type == E_ARC ? mcam_arc_pt(en, en->a0 + en->sw) : en->b;
                pts[np][0] = s0.x; pts[np][1] = s0.y; pts[np][2] = s0.z; np++;
                for (int it = 0; it < 1000000 && t <= t2 - 2e-7; it++) {
                    double t0 = t; mcs_brk_spline_const_err(cp, st->curve_min_step, st->curve_max_step, chord, 0, RL_TOL, &t);
                    if (!(t > t0) || t > t2 - 2e-7 || mcs_spline_eval_d(cp, t, 0, o)) break;
                    double *L = pts[np - 1]; if (hypot(hypot(o[0][0] - L[0], o[0][1] - L[1]), o[0][2] - L[2]) <= chord) continue;
                    if (np >= 2 && rl_colinear(pts[np - 2], pts[np - 1], o[0])) { memcpy(pts[np - 1], o[0], sizeof(double) * 3); continue; }
                    if (np == cap) { cap *= 2; pts = realloc(pts, sizeof *pts * cap); }
                    memcpy(pts[np++], o[0], sizeof(double) * 3);
                }
                if (np == cap) { cap *= 2; pts = realloc(pts, sizeof *pts * cap); }
                pts[np][0] = s1.x; pts[np][1] = s1.y; pts[np][2] = s1.z; np++;
                free(rec);
            } else continue;                              /* "(trav) (unsupported entity type)" */
            if (rv) for (int i = 0; i < np / 2; i++) { double tmp[3]; memcpy(tmp, pts[i], sizeof tmp); memcpy(pts[i], pts[np - 1 - i], sizeof tmp); memcpy(pts[np - 1 - i], tmp, sizeof tmp); }
            for (int i = 0; i + 1 < np; i++) rl_seg(&k, &u, xrl.btol, pts[i], pts[i + 1]);
            rl_out(&u);                                   /* each source entity ends its run */
            free(pts);
        }
    }
    free(u.p);
    return rl_made;
}

/* the dialog (CXfRollDlg, template 6467 / 6468): labels from the template / the help pages (formats/wireframe.md) */
void xroll_panel(const CuiPanelOps *ops, void *u) {
    if (ops->exp(u, "Roll")) {
        ops->radio(u, "Delete original geometry (Move)", &xrl.mode, 0, 1); ops->radio(u, "Keep original geometry (Copy)", &xrl.mode, 1, 1);   /* 100 / 101 */
        ops->radio(u, "Roll", &xrl.roll, 1, 1); ops->radio(u, "Unroll", &xrl.roll, 0, 1);       /* 817 / 818 */
        ops->end(u); }
    if (ops->exp(u, "Rotate about")) { ops->radio(u, "X axis", &xrl.axisx, 1, 1); ops->radio(u, "Y axis", &xrl.axisx, 0, 1); ops->end(u); }   /* 819 / 821 */
    if (ops->exp(u, "Direction")) { ops->radio(u, "CW", &xrl.cw, 1, 1); ops->radio(u, "CCW", &xrl.cw, 0, 1); ops->end(u); }        /* 790 / 793 */
    if (ops->exp(u, "Parameters")) {
        ops->num(u, "Rotary diameter", "dia", &xrl.dia, NULL, 1, 0, 1e9);                     /* 825 (P+0x08 x 2) */
        ops->num(u, "Angle tolerance", "atol", &xrl.btol, NULL, 1, 5.7296e-5, 1e9);          /* 827 (spin min 5.7296e-5) */
        ops->toggle(u, "Select an arc to define the parameters", 1, -1);                     /* 721: NOT PORTED yet */
        ops->end(u); }
    if (ops->exp(u, "Positioning")) {
        ops->radio(u, "Angle", &xrl.twopt, 0, 1); ops->radio(u, "Between points", &xrl.twopt, 1, xrl_pts_ok);   /* 797 / 801 (801: after two picks, FUN_180029da0) */
        if (xrl.ang >= 1e38) xrl.ang = 0;                                                     /* (>= 1e38 shows 0) */
        ops->num(u, "Angle", "ang", &xrl.ang, NULL, !xrl.twopt, -360, 360);                   /* 804 */
        ops->toggle(u, "Select two points", 1, 9);                                           /* 829 (panel code 9, ours) */
        ops->end(u); }
    if (ops->exp(u, "Type")) { static const char *T[] = { "Lines/Arcs", "Points", "Splines" }; ops->combo(u, T, 3, &xrl.type, 2); ops->end(u); }   /* 3036 (147 / 157 / 158) */
    if (ops->exp(u, "Use New Attributes")) { ops->check(u, "Use New Attributes", &xrl.useattr, 1);   /* 1665 */
        ops->inum(u, "Level", "lvl", &xrl.level, NULL, xrl.useattr, 1, 100000); ops->inum(u, "Color", "col", &xrl.color, NULL, xrl.useattr, 0, 255); ops->end(u); }
}
