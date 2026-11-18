/* mcfillet.c - Mastercam's fillet of two lines or arcs: find_fil_gt (mccadutil.dll @18005df80, decomp/wfacc/mccadutil_kern3.c:817), with its helpers offset_gt_by_dir (mccore @180017ca0), gt_offset, gp_ints,
 * l_ints, la_ints, aa_ints, proj_pt_ln, proj_pt_ar, FUN_180060490 (the tangent points), FUN_18005c600 (the arc), t_on_gt. Oracle: tools/tests/fillet_oracle.sh (the real mccadutil.dll under tools/peloader).
 * Not here: the relief branch (DAT_180255601 = SetReliefFilletActive, style 4 of the panel, greyed), find_fil_ent (splines; make_fillet's second path). No UI: when several solutions come back the caller shows them (SelectArcToKeep, prompt 0x16ea). */
#include <math.h>
#include <string.h>
#include "mcfillet.h"

#define PI_ 3.141592653589793
#define TAU_ 6.283185307179586

typedef struct { double x, y; } P;
static double dist(P a, P b) { return hypot(a.x - b.x, a.y - b.y); }

/* l_ints (mccore_filter.c:3957): non parallel test on the dot and the cross of the directions; the point on the first line */
static int l_ints(const McfG *a, const McfG *b, P *out, double tol) {
    double dy1 = a->y2 - a->y1, dx1 = a->x2 - a->x1, l1 = dy1 * dy1 + dx1 * dx1;
    if (!(1e-24 <= l1)) return 0;
    double dx2 = b->x1 - b->x2, dy2 = b->y1 - b->y2, l2 = dx2 * dx2 + dy2 * dy2;
    if (!(1e-24 <= l2)) return 0;
    double det = dy2 * dx1 - dx2 * dy1, dot = dy2 * dy1 + dx2 * dx1, tt = tol * tol, ok;
    double n = (b->x1 - a->x1) * dy2 - (b->y1 - a->y1) * dx2;
    if (tt <= fabs(dot)) ok = tol <= fabs(det / dot); else ok = tt <= fabs(det);
    if (!ok) return 0;
    double t = n / det; out->x = a->x1 + dx1 * t; out->y = a->y1 + dy1 * t;
    return 1;
}
/* proj_pt_ln: t unclamped; false only for a zero line */
static int proj_ln(P p, const McfG *l, P *o) {
    double dx = l->x2 - l->x1, dy = l->y2 - l->y1, n = dy * dy + dx * dx;
    if (n == 0.0) return 0;
    double t = ((p.y - l->y1) * dy + (p.x - l->x1) * dx) / n;
    o->x = (1.0 - t) * l->x1 + t * l->x2; o->y = (1.0 - t) * l->y1 + t * l->y2; return 1;
}
/* proj_pt_ar: radial, the point at least tol from the centre */
static int proj_ar(P p, P c, double r, P *o, double tol) {
    double dx = p.x - c.x, dy = p.y - c.y, d = sqrt(dx * dx + dy * dy);
    if (!(tol <= d)) return 0;
    o->x = c.x + dx * (r / d); o->y = c.y + dy * (r / d); return 1;
}
/* la_ints: the line's foot of the centre; tangent within 2 r tol */
static int la_ints(const McfG *l, const McfG *a, P *p1, P *p2, double tol) {
    P f; P c = { a->cx, a->cy }; if (!proj_ln(c, l, &f)) return 0;
    double dq = a->r * a->r - ((f.y - a->cy) * (f.y - a->cy) + (f.x - a->cx) * (f.x - a->cx));
    if (!(-(a->r * 2.0 * tol) <= dq)) return 0;
    double h = 0.0 <= dq ? sqrt(dq) : 0.0, dx = l->x2 - l->x1, dy = l->y2 - l->y1, n = sqrt(dx * dx + dy * dy), ux = 0, uy = 0;
    if (n != 0.0) { ux = dx / n; uy = dy / n; }
    p1->x = ux * h + f.x; p1->y = uy * h + f.y; p2->x = ux * -h + f.x; p2->y = uy * -h + f.y; return 1;
}
/* aa_ints (mccore_inters.c:1808); concentric within tol: not a solution here (arc_endpoints only with equal radii) */
static int aa_ints(const McfG *a, const McfG *b, P *p1, P *p2, double tol) {
    double dx = a->cx - b->cx, dy = a->cy - b->cy, d2 = dx * dx + dy * dy, d = sqrt(d2), r1 = a->r, r2 = b->r;
    if (!(tol <= d)) return 0;
    if (tol < fabs(r1 - r2) - d) return 0;
    if (tol < (d - r1) - r2) return 0;
    double h = 0.0, q = ((r1 * r1 - r2 * r2) + d2) / (d * 2.0), hq = r1 * r1 - q * q;
    if (0.0 <= hq) h = sqrt(hq);
    else if (q == 0.0) q = r1 * 0.0;
    else if (0.0 <= q) q = r1 * 1.0;
    else q = r1 * -1.0;
    double ex = b->cx - a->cx, ey = b->cy - a->cy, n = sqrt(ex * ex + ey * ey); ex /= n; ey /= n;
    p1->x = -ey * h + ex * q + a->cx; p1->y = ey * q + ex * h + a->cy;
    p2->x = -ey * -h + ex * q + a->cx; p2->y = ey * q + ex * -h + a->cy;
    return 1;
}
/* gp_ints (mccore_inters.c:512): the two solutions, first the nearer the seed (closest_pt) */
static int gp_ints(const McfG *a, const McfG *b, P seed, P *x1, P *x2, double tol) {
    int k = (!a->line ? 2 : 0) + (!b->line ? 1 : 0);
    if (k == 0) { if (!l_ints(a, b, x1, tol)) return 0; *x2 = *x1; return 1; }
    P p, q; int ok;
    if (k == 1) ok = la_ints(a, b, &p, &q, tol);
    else if (k == 2) ok = la_ints(b, a, &p, &q, tol);
    else ok = aa_ints(a, b, &p, &q, tol);
    if (!ok) return 0;
    if (dist(q, seed) < dist(p, seed)) { *x1 = q; *x2 = p; } else { *x1 = p; *x2 = q; }
    return 1;
}
int mcf_gp_ints(const McfG *a, const McfG *b, double sx, double sy, double x[4], double tol) {
    P s = { sx, sy }, p, q; if (!gp_ints(a, b, s, &p, &q, tol)) return 0;
    x[0] = p.x; x[1] = p.y; x[2] = q.x; x[3] = q.y; return 1;
}
/* offset_gt_by_dir / gt_offset / l_offset: the line moved d to the side of s; the circle r + d (s outside) or r - d (s inside), r <= 0 fails */
static int off_dir(const McfG *g, double d, int dir, P other, McfG *o) {
    P s = other;
    if (dir != 0) {
        if (g->line) { s.x = (double)dir * (g->y2 - g->y1) + g->x1; s.y = (double)-dir * (g->x2 - g->x1) + g->y1; }
        else if (dir != -1) { s.x = g->cx + 0.0; s.y = g->r * 2.0 + g->cy; }
        else { s.x = g->cx; s.y = g->cy; }
    }
    *o = *g;
    if (g->line) {
        double dx = g->x2 - g->x1, dy = g->y2 - g->y1, n = sqrt(dx * dx + dy * dy); if (n == 0.0) return 0;
        double nx = dy / n, ny = -dx / n;                    /* l_offset: to the side of s */
        double sd = (s.x - g->x1) * nx + (s.y - g->y1) * ny;
        if (!(sd >= 0)) { nx = -nx; ny = -ny; }
        o->x1 = g->x1 + nx * d; o->y1 = g->y1 + ny * d; o->x2 = g->x2 + nx * d; o->y2 = g->y2 + ny * d;
        return 1;
    }
    double dc = sqrt((g->cx - s.x) * (g->cx - s.x) + (g->cy - s.y) * (g->cy - s.y)), rr = g->r <= dc ? g->r + d : g->r - d;
    if (!(0.0 < rr)) return 0;
    o->r = rr; return 1;
}
/* FUN_180060490: the tangent point on each entity of the circle (x, R) */
static int feet(const McfG g[2], double R, P x, P f[2], double tol) {
    for (int i = 0; i < 2; i++) {
        if (g[i].line) { if (!proj_ln(x, &g[i], &f[i])) return 0; }
        else { P c = { g[i].cx, g[i].cy };
            if (g[i].r <= R) { if (!proj_ar(c, x, R, &f[i], tol)) return 0; }
            else { if (!proj_ar(x, c, g[i].r, &f[i], tol)) return 0; } }
    }
    return 1;
}
static double ang02(double dy, double dx) { if (1e-12 <= dy * dy + dx * dx) { double a = atan2(dy, dx); if (a < 0.0) a += TAU_; return a; } return 0.0; }
/* FUN_18005c600: a0 / sw from the feet; style 0 (small): sw > pi -> complement from the end (FUN_180060c90); style 1 / 2: sw < pi -> complement; style 2 the full circle */
static void arc_of(P c, double R, P f[2], int style, McfArc *o) {
    double a0 = ang02(f[0].y - c.y, f[0].x - c.x), a1 = ang02(f[1].y - c.y, f[1].x - c.x), sw = a1 - a0;
    if (sw < 0.0) sw += TAU_;
    o->small = 1;
    if (style == 0) { if (PI_ < sw) { a0 = a1; sw = TAU_ - sw; o->small = 0; } }
    else { if (sw < PI_) { a0 = a1; sw = TAU_ - sw; o->small = 0; } if (style == 2) { a0 = 0.0; sw = TAU_; } }
    o->cx = c.x; o->cy = c.y; o->r = R; o->sa = a0; o->sw = sw;
}
void mcf_arc_of(double cx, double cy, double R, const double p0[2], const double p1[2], int style, McfArc *o) { P c = { cx, cy }, f[2] = { { p0[0], p0[1] }, { p1[0], p1[1] } }; arc_of(c, R, f, style, o); }   /* FUN_18005c600, for find_fil_ent (cui_geo_mc.inc) */
/* eval_gt_pt / t_on_line / t_on_arc */
static P gt_at(const McfG *g, double t) { P p; if (g->line) { p.x = (1.0 - t) * g->x1 + t * g->x2; p.y = (1.0 - t) * g->y1 + t * g->y2; } else { double a = g->sa + t * g->sw; p.x = g->cx + g->r * cos(a); p.y = g->cy + g->r * sin(a); } return p; }
static double t_on(const McfG *g, P p) {
    if (g->line) { double dx = g->x2 - g->x1, dy = g->y2 - g->y1, l = dy * dy + dx * dx; return l > 0 ? ((p.y - g->y1) * dy + (p.x - g->x1) * dx) / l : 0; }
    if (fabs(g->sw) < 1e-6) return 0;
    double a = 0; { double dx = p.x - g->cx, dy = p.y - g->cy; if (dx * dx + dy * dy >= 1e-12) { a = atan2(dy, dx); if (a < 0) a += TAU_; } }
    double t = (a - g->sa) / g->sw;
    if (t >= 0 && t <= 1) return t;
    double t2 = (a + TAU_ - g->sa) / g->sw;
    if (t2 >= 0 && t2 <= 1) return t2;
    double t3 = (a - TAU_ - g->sa) / g->sw;
    if (t3 >= 0 && t3 <= 1) return t3;
    double r = t, d1 = fmin(fabs(t), fabs(1 - t)), d2 = fmin(fabs(t2), fabs(1 - t2)); if (d2 < d1) { r = t2; d1 = d2; }
    double d3 = fmin(fabs(t3), fabs(1 - t3)); if (d3 < d1) r = t3;
    return r;
}
static int g_lolli; static double g_clr;   /* set_lollipop_corner (DAT_180255601) / set_clearance_offset (DAT_180255618): the panel's Clearance style (sketcher_fillet_1.c:510-522) */
void mcf_set_lollipop(int on, double clr) { g_lolli = on; g_clr = clr; }
static P gt_tan(const McfG *g, double t) { if (g->line) { P d = { g->x2 - g->x1, g->y2 - g->y1 }; double l = hypot(d.x, d.y); if (l != 0) { d.x /= l; d.y /= l; } else d.x = d.y = 0; return d; }
    double a = t * g->sw + g->sa; P d = { -sin(a), cos(a) }; if (g->sw <= 0.0 && g->sw != 0.0) { d.x *= -1.0; d.y *= -1.0; } return d; }   /* eval_gt_tan core_kernels3.c:3 */
/* the Clearance corner (find_fil_gt's lollipop branch, wfacc/mccadutil_kern3.c:1160-1370): a full circle of radius R through the point X + n (R - clearance), n the unit sum of the directions towards the picked sides; the arc between its intersections with the curves */
static int lolli_cand(const McfG g[2], double R, const double pick[2], P *mid_, int style, double tol, McfArc *o, int use_dir, int fil_dir, int *rejected) {
    P mid = *mid_;   /* dStack_19f98 is updated in place (:1282-1285): the next (u, v) pass starts from the shifted seed */
    P X, X2; if (!gp_ints(&g[0], &g[1], mid, &X, &X2, tol)) return 0;
    P a[2];
    for (int i = 0; i < 2; i++) { double t = t_on(&g[i], X); P d;
        if (!g[i].line) { d = t <= pick[i] ? gt_tan(&g[i], 0.0) : gt_tan(&g[i], 1.0); if (!(t <= pick[i])) { d.x *= -1.0; d.y *= -1.0; } }
        else { if (t <= pick[i]) { d.x = g[i].x2 - X.x; d.y = g[i].y2 - X.y; } else { d.x = g[i].x1 - X.x; d.y = g[i].y1 - X.y; } }
        double l = hypot(d.x, d.y); if (l != 0.0) { d.x /= l; d.y /= l; } else d.x = d.y = 0.0; a[i] = d; }
    double nx = a[0].x + a[1].x, ny = a[0].y + a[1].y, nl = hypot(nx, ny); if (nl == 0.0) nx = ny = 0.0; else { nx /= nl; ny /= nl; }
    P C = { (nx * R + X.x) - nx * g_clr, (ny * R + X.y) - ny * g_clr }, sd = { g_clr * 100.0 * nx + mid.x, g_clr * 100.0 * ny + mid.y }; *mid_ = sd;
    McfG circ; memset(&circ, 0, sizeof circ); circ.cx = C.x; circ.cy = C.y; circ.r = R; circ.sa = 0.0; circ.sw = 6.283185307179586;
    P f[2], b;                                                /* gp_ints: the nearer solution, the other in b (:1293, :1304) */
    if (!gp_ints(&circ, &g[0], sd, &f[0], &b, tol)) return 0; if (hypot(X.x - f[0].x, X.y - f[0].y) < tol) f[0] = b;
    if (!gp_ints(&circ, &g[1], sd, &f[1], &b, tol)) return 0; if (hypot(X.x - f[1].x, X.y - f[1].y) < tol) f[1] = b;
    arc_of(C, R, f, style, o);
    McfG ag; memset(&ag, 0, sizeof ag); ag.cx = o->cx; ag.cy = o->cy; ag.r = o->r; ag.sa = o->sa; ag.sw = o->sw;
    double t = t_on(&ag, X);
    if (t < 0.0 || 1.0 < t) { double v = 6.283185307179586 - fabs(o->sw); if (0.0 < o->sw) v = -v; o->sw = v; o->small = !o->small; }
    if (use_dir && (fil_dir == 0 ? !o->small : fil_dir == 1 && o->small)) { *rejected = 1; return 0; }
    for (int i = 0; i < 2; i++) { o->t1[i] = t_on(&g[i], f[i]); o->t2[i] = t_on(&g[i], X); }
    return 1;
}
int mcf_find_fil(const McfG g[2], double R, const double pick[2], int style, int both, double tol, double min_arc, McfArc *o, int maxc, int *sel, int use_dir, int fil_dir) {
    P s[2] = { gt_at(&g[0], pick[0]), gt_at(&g[1], pick[1]) }, mid = { (s[0].x + s[1].x) * 0.5, (s[0].y + s[1].y) * 0.5 };
    int small = 0;
    if (both) for (int i = 0; i < 2; i++) if (!g[i].line && g[i].r <= R && R != g[i].r) small = 1;     /* :923-933 param_8: an arc smaller than the fillet */
    int nl = small ? 2 : 1, nc = 0; P ls = mid;
    for (int u = 0; u < nl; u++) for (int v = 0; v < nl; v++) {
        if (g_lolli) { int rej = 0; if (nc < maxc && lolli_cand(g, R, pick, &ls, style, tol, &o[nc], use_dir, fil_dir, &rej)) nc++; if (rej) { *sel = -1; return 0; } continue; }
        int dir[2] = { 0, 0 };
        if (small) { dir[0] = u == 1 ? -1 : 1; dir[1] = v == 1 ? -1 : 1; }
        McfG of[2]; int okf[2];
        for (int i = 0; i < 2; i++) {
            double d = R; int dd = dir[i];
            if (dd == -1 && !g[i].line && g[i].r <= R && R != g[i].r) { dir[i] = 1; dd = 1; d = R - g[i].r * 2.0; }
            okf[i] = off_dir(&g[i], d, dd, s[1 - i], &of[i]);
        }
        if (!(okf[0] && okf[1])) continue;
        P x[2]; if (!gp_ints(&of[0], &of[1], mid, &x[0], &x[1], tol)) continue;
        int n2 = small ? 2 : 1;                               /* the second solution only with an arc smaller than the fillet (:986-990) */
        for (int m = 0; m < n2; m++) {
            P f[2]; if (!feet(g, R, x[m], f, tol)) continue;
            if (nc >= maxc) continue;
            arc_of(x[m], R, f, style, &o[nc]);
            if (use_dir && (fil_dir == 0 ? !o[nc].small : fil_dir == 1 && o[nc].small)) { *sel = -1; return 0; }   /* 18005eb0b-18005ec2f: the corner direction not wanted: *param_10 = -1 and find_fil_gt returns at once, no fillet at this corner */
            /* the t on each curve: param_6 = t_on_gt(foot), param_7 = the same or t_on_gt of the curves' own intersection (LAB_18005f14c :1626-1650) */
            P i1, i2; int oki = gp_ints(&g[0], &g[1], mid, &i1, &i2, tol);
            for (int i = 0; i < 2; i++) { o[nc].t1[i] = t_on(&g[i], f[i]); o[nc].t2[i] = oki ? t_on(&g[i], i1) : o[nc].t1[i]; }
            nc++;
        }
    }
    *sel = -1;
    if (nc == 1) { if (!(fabs(o[0].sw) <= tol)) *sel = 0; return nc; }  /* :1422-1430 */
    /* several: the one nearest both picks |dist(centre, seed) - r| summed, among r > tol and length > min_arc (:1466-1495); SelectArcToKeep / 0x16ea lets the user change it */
    double best = 1e38;
    for (int i = 0; i < nc; i++) { double r = o[i].r; if (tol < r && fabs(r * o[i].sw) > min_arc) { P c = { o[i].cx, o[i].cy }; double d = fabs(dist(c, s[0]) - r) + fabs(dist(c, s[1]) - r); if (d < best) { best = d; *sel = i; } } }
    return nc;
}
