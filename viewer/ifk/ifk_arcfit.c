/* ifk_arcfit.c - port of CSpinBdryArcFitter (mcmachinedef.dll, Mastercam 2017) as Wireframe > Turn Profile runs it
 * (mcmsurf FUN_18020de20 -> constructor 0x1800e06f0). C99, no dependencies. Every step cites the function (address) it
 * comes from; [asm] marks details read from the disassembly where the decompile drops arguments or mixes registers.
 * Decode notes: formats/interfacial.md 14 (written from this file).
 *
 * Layers (all in this file):
 *  1. mccore.dll / systypes.dll geometry helpers mcmachinedef imports (restrict_angle, constr_arc_new, ll_ints_3d, ...),
 *     with their addresses in those DLLs.
 *  2. The tp_ent boundary library of mcmachinedef (QuadrantBdryArcs, FindOuterBoundary = FUN_180156d50 and its callees):
 *     works on the 0x78-byte tp_ent images.
 *  3. The C3d classes (C3dPoint / C3dLine / C3dArc / CBdryEntList, BEPoint.cpp / BELine.cpp / BEArc.cpp) as values, and
 *     CSpinBdryArcFitter itself (FindArcs.cpp).
 * Tolerances: g_sysTol (180369030) = 1e-8 (initialiser 1800030d0, data 1801f6588), g_angTol (180369050) = 1e-6
 * (180003070, data 1801f6838), g_distTol (180369040) = |mccore tol| at load (1800030a0) - env->disttol; mccore `tol`
 * (env->systol) is read by the mccore / systypes helpers and directly by the fitter (tol_exref).
 * Not ported (the Turn Profile path does not reach them, or they are NOT DECODED): the solid path
 * (FindSpinBdrySolidTrueLinesAndArcs with a solid: ProcessSolid / ProcessWFGeometry / ProcessEdgeCurves / ValidateTrueLines,
 * Parasolid), the Parasolid constructor 0x1800e0560 (EvalPsBdry), what MastercamPlanes::SearchPlanePtr finds and the id /
 * view number MastercamPlanes::Add gives a new plane (the plane itself is computed and reported), views >= 2 in
 * world_to_view / view_to_world / arc_ep_3d (the mcViews matrices), the colinear cases of C3dLine::SaveColinearIntPts.
 * Checked against the DLL: tools/tests/ifk_arcfit_oracle.sh (tools/peloader --arcfit); byte-identical with msvcr120.dll's
 * sin / cos / atan2 / acos (ifk_arcfit_set_libm), within 4 ulps with the C library's. */
#include "ifk_arcfit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.283185307179586
#define PI_ 3.141592653589793

/* ------------------------------------------------------------------------------------------------------------- libm */
/* The DLLs use msvcr120.dll's sin / cos / atan2 / acos; glibc's differ from them in the last ulp for some arguments (seen:
   sin(0.8138806869715102)). ifk_arcfit_set_libm lets a test harness substitute the DLL's own functions
   (tools/peloader --arcfit-port); the default is the C library. sqrt / floor / ceil / fabs are exact in both. */
static double (*SIN)(double) = sin, (*COS)(double) = cos, (*ACOS)(double) = acos;
static double (*ATAN2)(double, double) = atan2;
void ifk_arcfit_set_libm(double (*s)(double), double (*c)(double), double (*a2)(double, double), double (*ac)(double))
{
    SIN = s ? s : sin; COS = c ? c : cos; ATAN2 = a2 ? a2 : atan2; ACOS = ac ? ac : acos;
}

/* ------------------------------------------------------------------------------------------------------------- state */
static double TOL;      /* mccore tol */
static double DTOL;     /* g_distTol */
static const double SYSTOL = 1e-08;   /* g_sysTol */
static const double ANGTOL = 1e-06;   /* g_angTol */
static double MINARC;   /* mccore min_arc_length */
static int g_nassert, g_notdec, g_limit;
static void mc_assert(void) { g_nassert++; }   /* _MCAssert (mccore): reports, the caller continues */

/* ===================================================================================== 1. mccore / systypes helpers */
/* a_2d: centre x, y (+0, +8), r (+0x10), start angle (+0x18), sweep (+0x20). l_2d: x1 y1 x2 y2. */

/* restrict_angle (mccore 1804b4440) */
static void restrict_angle(double lo, double hi, double *a)
{
    double d = *a;
    if (hi < d) { do d = d + -6.283185307179586; while (hi < d); *a = d; }
    d = *a;
    if (d < lo) { do d = d + 6.283185307179586; while (d < lo); *a = d; }
}
/* arc_endpoints (systypes 180001140) */
static void arc_endpoints(const double *a, double *p, double *q)
{
    double sa = a[3], sw = a[4];
    double c = COS(sa), r = a[2], s = SIN(sa), r2 = a[2], cy = a[1];
    p[0] = c * r + a[0];
    p[1] = s * r2 + cy;
    double c2 = COS(sa + sw), r3 = a[2], s2 = SIN(sa + sw), r4 = a[2], cy2 = a[1];
    q[0] = c2 * r3 + a[0];
    q[1] = s2 * r4 + cy2;
}
/* proj_pt_ln (systypes 180002780): t unclamped; false only for a zero-length line */
static void proj_pt_ln(const double *p, const double *l, double *o, double *t, char *ok)
{
    double dx = l[2] - l[0], dy = l[3] - l[1], d = dy * dy + dx * dx;
    if (d != 0.0) {
        double u = ((p[1] - l[1]) * dy + (p[0] - l[0]) * dx) / d;
        *t = u;
        double y = (1.0 - u) * l[1] + u * l[3];
        o[0] = (1.0 - u) * l[0] + u * l[2];
        o[1] = y;
        *ok = 1;
        return;
    }
    *ok = 0;
}
/* pt_on_line_tol (systypes 1800028b0) */
static char pt_on_line_tol(const double *p, const double *l, double tl)
{
    double d6 = p[0] - l[2], d3 = p[0] - l[0], d4 = p[1] - l[1], d5 = p[1] - l[3];
    double d1 = d6 * d3 + d5 * d4;
    d3 = sqrt(d3 * d3 + d4 * d4);
    d4 = sqrt(d5 * d5 + d6 * d6);
    if (fabs(d3) < 1e-12 || fabs(d4) < 1e-12) return 1;
    if (d1 / (d4 * d3) + 1.0 <= tl) {
        if (d1 <= tl) return 1;
        if (d1 == 0.0) return d3 < tl;
    }
    return 0;
}
/* pt_on_arc (systypes 1800014e0) */
static char pt_on_arc(const double *p, const double *a, double tl)
{
    double y = p[1], x = p[0], r = a[2];
    double dx = a[0] - x, dy = a[1] - y;
    double d = sqrt(dx * dx + dy * dy);
    if (fabs(d - r) <= tl) {
        x = x - a[0];
        y = y - a[1];
        double ang;
        if (1e-12 <= y * y + x * x) { ang = ATAN2(y, x); if (ang < 0.0) ang = ang + 6.283185307179586; }
        else ang = 0.0;
        double e = 0.0, sw = a[4], lo, hi;
        if (sw <= 0.0) { hi = a[3]; lo = hi + sw; }
        else { lo = a[3]; hi = lo + sw; }
        if (1e-12 < r) e = tl / r;
        for (; hi + e < ang; ang = ang + -6.283185307179586) {}
        for (; ang < lo - e; ang = ang + 6.283185307179586) {}
        if (ang < lo - e || hi + e < ang) return 0;
        return 1;
    }
    return 0;
}
/* aa_ints_new (systypes 180001fb0) */
static void aa_ints_new(const double *a1, const double *a2, double tl, double *p, double *q, char *ok)
{
    *ok = 0;
    double x2 = a2[0], y2 = a2[1], r2 = a2[2], x1 = a1[0], y1 = a1[1], r1 = a1[2];
    double dx = x1 - x2, dy = y1 - y2;
    double dd = dy * dy + dx * dx;
    double d = sqrt(dd);
    if (tl <= d) {
        if (tl < fabs(r1 - r2) - d) return;
        if (tl < (d - r1) - r2) return;
        double ux = 0.0;
        double a = ((r1 * r1 - r2 * r2) + dd) / (d * 2.0);
        double h2 = r1 * r1 - a * a, h;
        if (0.0 <= h2) h = sqrt(h2);
        else {
            h = 0.0;
            if (a == 0.0) a = r1 * 0.0;
            else if (0.0 <= a) a = r1 * 1.0;
            else a = r1 * -1.0;
        }
        double vx = x2 - x1, vy = y2 - y1;
        double l = sqrt(vx * vx + vy * vy);
        if (l == 0.0) vx = 0.0;
        else { vx = vx / l; ux = vy / l; }
        double py = ux * a + vx * h + y1;
        p[0] = -ux * h + vx * a + x1;
        p[1] = py;
        double qy = ux * a + vx * -h + y1;
        q[0] = -ux * -h + vx * a + x1;
        q[1] = qy;
    } else {
        if (tl <= fabs(r1 - r2)) return;
        arc_endpoints(a1, p, q);
    }
    *ok = 1;
}
/* arc_3d_to_2d (mccore 1804b4d90): a_3d centre +0x30, r +0x48, start +0x50, sweep +0x58 */
typedef struct { double s[3], e[3], c[3], r, sa, sw; int16_t view; uint8_t pad[6]; } A3;   /* a_3d, 0x68 bytes */
typedef char a3_size_check[sizeof(A3) == 0x68 ? 1 : -1];
static void arc_3d_to_2d(double *a2, const A3 *a)
{
    a2[0] = a->c[0]; a2[1] = a->c[1]; a2[2] = a->r; a2[3] = a->sa; a2[4] = a->sw;
}
/* view_to_world / world_to_view (Mastercam.dll 180341520 / 180341b30): views < 2 are the identity; the others need the
   mcViews matrices - NOT DECODED here (flagged, the result is then refused). */
static void view_to_world(const double *p, short v, double *o)
{
    if (v >= 2) g_notdec = 1;
    double x = p[0], y = p[1], z = p[2];
    o[0] = x; o[1] = y; o[2] = z;
}
#define world_to_view view_to_world
/* constr_arc_new (mccore 1804b5830): the arc about c from p1 to p2 (swapped when cw), radii equal within *rtol; s == e
   within tol gives a full circle */
static void constr_arc_new(const double *p1, const double *p2, const double *c, char cw, double rtol, double *a, char *ok)
{
    const double *s = cw ? p2 : p1, *e = cw ? p1 : p2;
    double d1 = s[0] - c[0], ey = e[1] - c[1], d2 = s[1] - c[1], ex = e[0] - c[0];
    double ss = d1 * d1 + d2 * d2, r1 = sqrt(ss);
    double ee = ex * ex + ey * ey, r2 = sqrt(ee);
    if (fabs(r1 - r2) < rtol || fabs(r1 - r2) == rtol) {
        double dy = e[1] - s[1], dx = e[0] - s[0], sa, sw;
        double dl = sqrt(dy * dy + dx * dx);
        if (TOL <= dl) {
            if (1e-12 <= ss) { sa = ATAN2(d2, d1); if (sa < 0.0) sa = sa + 6.283185307179586; }
            else sa = 0.0;
            double ea = 0.0;
            if (1e-12 <= ee && (ea = ATAN2(ey, ex), ea < 0.0)) ea = ea + 6.283185307179586;
            for (sw = ea - sa; sw <= 1e-08; sw = sw + 6.283185307179586) {}
        } else { sw = 6.283185307179586; sa = 0.0; }
        a[0] = c[0]; a[1] = c[1]; a[3] = sa; a[2] = r1; a[4] = sw;
        *ok = 1;
    } else *ok = 0;
}
/* constr_ns_arc (mccore 1804b5c00): constr_arc_new, sweep > 2 pi refused, cw arcs get a negative sweep */
static void constr_ns_arc(const double *p1, const double *p2, const double *c, char cw, double rtol, double *a, char *ok)
{
    constr_arc_new(p1, p2, c, cw, rtol, a, ok);
    double sw = a[4];
    if (6.283185307179586 < sw) { *ok = 0; return; }
    if (cw && *ok) { a[3] = sw + a[3]; a[4] = -sw; }
}
/* dist_pt_l (mccore 1804b32a0): distance to the infinite line (to the start when the line is shorter than tol) */
static void dist_pt_l(const double *p, const double *l, double *d)
{
    double d5 = p[0] - l[0], d1 = p[1] - l[1], d3 = l[3] - l[1], d2 = l[2] - l[0];
    double len = sqrt(d3 * d3 + d2 * d2);
    if (len <= TOL) *d = sqrt(d1 * d1 + d5 * d5);
    else { double v = (d5 * d3 - d1 * d2) / len; *d = v; if (v < 0.0) *d = -v; }
}
/* proj_pt_ar (mccore 1804b41b0) */
static void proj_pt_ar(const double *p, const double *a, double *o, char *ok)
{
    double cx = a[0], cy = a[1], dx = p[0] - cx, dy = p[1] - cy;
    double d = sqrt(dx * dx + dy * dy);
    if (TOL <= d) {
        double y = cy + dy * (a[2] / d);
        o[0] = cx + dx * (a[2] / d);
        o[1] = y;
        *ok = 1;
    } else *ok = 0;
}
/* proj_pt_ln_3d (mccore 1804b42c0): output and t untouched when the line is shorter than tol */
static void proj_pt_ln_3d(const double *p, const double *l, double *o, double *t, char *ok)
{
    double py = p[1], ly = l[1], dx = l[3] - l[0], dy = l[4] - l[1], dz = l[5] - l[2];
    double px = p[0], pz = p[2], lx = l[0], lz = l[2];
    double len = sqrt(dy * dy + dx * dx + dz * dz);
    if (TOL <= len) {
        double u = ((py - ly) * dy + (px - lx) * dx + (pz - lz) * dz) / (len * len);
        *t = u;
        double w = 1.0 - u;
        double ez = l[5], sz = l[2];
        double oy = w * l[1] + u * l[4];
        o[0] = w * l[0] + u * l[3];
        o[1] = oy;
        o[2] = w * sz + u * ez;
        *ok = 1;
    } else *ok = 0;
}
/* lp_ints (mccore 1804b3ed0): line l with the plane through q with normal n */
static void lp_ints(const double *n, const double *q, const double *l, double *o, double *t, char *ok)
{
    double tl = TOL;
    double qx = q[0], lx = l[0], dx = l[3] - l[0], dy = l[4] - l[1], dz = l[5] - l[2];
    double qy = q[1], ly = l[1], qz = q[2], lz = l[2], ny = n[1], nx = n[0], nz = n[2];
    double den = ny * dy + nx * dx + nz * dz;
    if (fabs(den) <= TOL * TOL && TOL * TOL != fabs(den)) {
        double nl = sqrt(nx * nx + ny * ny + nz * nz);
        double ll = sqrt(dx * dx + dy * dy + dz * dz);
        if (nl == 0.0 || ll == 0.0 || fabs(den) / (ll * nl) < tl) { *ok = 0; return; }
    }
    double u = (nx * (qx - lx) + ny * (qy - ly) + nz * (qz - lz)) / den;
    *t = u;
    double sz = l[2];
    double oy = dy * u + l[1];
    o[0] = dx * u + l[0];
    o[1] = oy;
    o[2] = dz * u + sz;
    *ok = 1;
}
/* ll_ints_3d (mccore 1804b3af0): p1 on l1 and p2 on l2, the closest points of the two infinite lines */
static void ll_ints_3d(const double *l1, const double *l2, double *p1, double *p2, char *ok)
{
    double d8 = l1[5] - l1[2], d9 = l1[4] - l1[1], d1 = l1[3] - l1[0];
    double d4 = l2[4] - l2[1], d3 = l2[5] - l2[2], d6 = l2[3] - l2[0];
    double d5 = d3 * d9 - d4 * d8, d2 = d8 * d6 - d3 * d1, d10 = d4 * d1 - d9 * d6;
    double n1[3] = { d2 * d8 - d10 * d9, d10 * d1 - d5 * d8, d5 * d9 - d2 * d1 };
    double n2[3] = { d2 * d3 - d10 * d4, d10 * d6 - d5 * d3, d5 * d4 - d2 * d6 };
    double t;
    lp_ints(n2, l2, l1, p1, &t, ok);
    if (*ok) lp_ints(n1, l1, l2, p2, &t, ok);
}
/* t_on_line (mccore 1804b48a0) */
static void t_on_line(double *t, const double *p, const double *l)
{
    double dx = l[2] - l[0], dy = l[3] - l[1], d = dy * dy + dx * dx;
    if (TOL * TOL < d) { *t = (dy * (p[1] - l[1]) + dx * (p[0] - l[0])) / d; return; }
    *t = 0.0;
}
/* t_on_arc (mccore 1804b45f0) */
static void t_on_arc(double *t, const double *p, const double *a)
{
    if (fabs(a[4]) < 1e-06) { *t = 0.0; return; }
    double dx = p[0] - a[0], dy = p[1] - a[1], ang;
    if (1e-12 <= dy * dy + dx * dx) { ang = ATAN2(dy, dx); if (ang < 0.0) ang = ang + 6.283185307179586; }
    else ang = 0.0;
    double t0 = (ang - a[3]) / a[4];
    *t = t0;
    if (t0 < 0.0 || 1.0 < t0) {
        double t1 = ((ang + 6.283185307179586) - a[3]) / a[4];
        if (0.0 <= t1 && t1 <= 1.0) { *t = t1; return; }
        double t2 = ((ang - 6.283185307179586) - a[3]) / a[4];
        if (t2 < 0.0 || 1.0 < t2) {
            double m0 = fabs(1.0 - t0); if (fabs(t0) < m0) m0 = fabs(t0);
            double m1 = fabs(1.0 - t1); if (fabs(t1) < m1) m1 = fabs(t1);
            if (m1 < m0) { *t = t1; m0 = m1; }
            double m2 = fabs(1.0 - t2); if (fabs(t2) < m2) m2 = fabs(t2);
            if (m0 <= m2) return;
        }
        *t = t2;
    }
}
/* la_ints (mccore 1804b6d90): line / circle */
static void la_ints(const double *l, const double *a, double *p1, double *p2, char *ok)
{
    double f[2], t;
    proj_pt_ln(a, l, f, &t, ok);
    if (*ok) {
        double r = a[2];
        double h2 = r * r - ((f[1] - a[1]) * (f[1] - a[1]) + (f[0] - a[0]) * (f[0] - a[0]));
        if (-(r * 2.0 * TOL) <= h2) {
            double uy = 0.0, h = 0.0 <= h2 ? sqrt(h2) : 0.0, ux;
            double dx = l[2] - l[0], dy = l[3] - l[1];
            double L = sqrt(dx * dx + dy * dy);
            if (L == 0.0) ux = 0.0;
            else { ux = dx / L; uy = dy / L; }
            double y1 = uy * h + f[1];
            p1[0] = ux * h + f[0]; p1[1] = y1;
            double y2 = uy * -h + f[1];
            p2[0] = ux * -h + f[0]; p2[1] = y2;
        } else *ok = 0;
    }
}
/* Equal (mccore 1804b3230) */
static int Equal(const double *a, const double *b)
{
    double dx = b[0] - a[0];
    if (dx <= TOL && (-TOL < dx || -TOL == dx)) {
        double dy = b[1] - a[1];
        if (dy <= TOL && (-TOL < dy || -TOL == dy)) return dy * dy + dx * dx < TOL * TOL;
    }
    return 0;
}
/* dist_to_finite_line (mccore 1804b6680) */
static void dist_to_finite_line(const double *p, const double *l, double tl, double *d, char *ok)
{
    double y0 = l[1], x0 = l[0], dx = l[2] - x0, dy = l[3] - y0;
    double len = sqrt(dx * dx + dy * dy), q;
    if (TOL <= len) {
        double px = p[0], py = p[1];
        double dd = fabs((px - x0) * dy - (py - y0) * dx) / len;
        *d = dd;
        if (tl <= dd) { *ok = 0; return; }
        double s = ((px - x0) * dx + (py - y0) * dy) / len;
        if (0.0 <= s) {
            if (s <= len) { *ok = 1; return; }
            if (len + tl <= s) { *ok = 0; return; }
            x0 = l[2]; y0 = l[3];
        } else {
            if (s <= -tl) { *ok = 0; return; }
            x0 = l[0]; y0 = l[1];
        }
    }
    q = (y0 - p[1]) * (y0 - p[1]) + (x0 - p[0]) * (x0 - p[0]);
    q = sqrt(q);
    *d = q;
    *ok = q < tl;
}
/* gt (mccore): type byte at +0 ('P' 0x50, 'L' 0x4c, 'A' 0x41), geometry at +0x10 */
typedef struct { char type; double g[5]; } GT;
/* pt_on_gt_new (mccore 1804b7080) */
static char pt_on_gt_new(const double *p, const GT *g, double tl)
{
    if (g->type == 'P') return (char)Equal(p, g->g);
    if (g->type == 'L') { double d; char ok; dist_to_finite_line(p, g->g, tl, &d, &ok); return ok; }
    return pt_on_arc(p, g->g, tl);
}
/* FUN_1804b4dc0 (mccore, the 2D part of break_arc_3d) [asm 1804b4dc0..1804b5012]: points along the arc every `step`: the
   start, then multiples of step (through a short) beyond it, the last one clipped to the end */
static void break_arc_2d(const double *a, double step, double *pts, int *n)
{
    double st = fabs(step), sst, dir;
    if (a[4] < 0.0) { sst = -st; dir = -1.0; } else { sst = st; dir = 1.0; }
    *n = 0;
    double r = a[2];
    if (TOL > r) return;
    double ang = a[3];
    while (0.0 > ang) ang = ang + 6.283185307179586;
    double end = ang + a[4];
    pts[0] = COS(ang) * r + a[0];
    pts[2 * *n + 1] = SIN(ang) * a[2] + a[1];
    (*n)++;
    if ((ang - end) * dir > -1e-06) return;
    do {
        int k = *n, add = 1;
        if (k == 1) {
            double q = ang / st;
            q = (0.0 > q) ? ceil(q) : floor(q);
            ang = (double)(int)(short)(int)q * st;
            if (dir > 0.0) ang = ang + st;
            if (!(1e-06 > fabs(ang - a[3]))) add = 0;
        }
        if (add) ang = ang + sst;
        if ((ang - end) * dir > 0.0) ang = end;
        pts[2 * k] = COS(ang) * a[2] + a[0];
        pts[2 * *n + 1] = SIN(ang) * a[2] + a[1];
        (*n)++;
    } while (!((ang - end) * dir > -1e-06));
}
/* break_arc_3d (mccore 1804b5020): step 1.570796326794896 when the arc's view is `view`, else 0.392699081; the points
   (x, y, centre z) through view_to_world(arc view) */
static void break_arc_3d(const A3 *a, short view, double *pts, int *n)
{
    double a2[5], tmp[2 * 40];
    arc_3d_to_2d(a2, a);
    break_arc_2d(a2, a->view == view ? 1.570796326794896 : 0.392699081, tmp, n);
    for (int i = 0; i < *n; i++) {
        double q[3] = { tmp[2 * i], tmp[2 * i + 1], a->c[2] };
        view_to_world(q, a->view, pts + 3 * i);
    }
}
/* arc_ep_3d: Mastercam.dll 18032d730 takes the view's matrix mcViews[a->view] + 8 and calls systypes arc_ep_3d (180001210):
   the 2D end points (cos / sin about the centre) through that matrix. The boundary library only makes view-1 arcs
   (arc_2d_to_3d(.., 1, ..)); mcViews[1] is taken as the identity [inferred: view 1 = Top, view_to_world / world_to_view
   treat views < 2 as the identity]; other views are NOT DECODED (flagged). */
static void arc_ep_3d(const A3 *a, double *p, double *q)
{
    static const double M[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    if (a->view != 1) g_notdec = 1;
    double sa = a->sa, sw = a->sw, r = a->r;
    double c1 = COS(sa), s1 = SIN(sa);
    double x1 = c1 * r + a->c[0], y1 = s1 * r + a->c[1];
    double c2 = COS(sw + sa), s2 = SIN(sw + sa);
    double x2 = c2 * r + a->c[0], y2 = s2 * r + a->c[1];
    double z = a->c[2];
    double py = y1 * M[4] + x1 * M[1] + z * M[7];
    p[0] = y1 * M[3] + x1 * M[0] + z * M[6];
    p[1] = py;
    p[2] = x1 * M[2] + y1 * M[5] + z * M[8];
    double qy = y2 * M[4] + x2 * M[1] + z * M[7];
    q[0] = y2 * M[3] + x2 * M[0] + z * M[6];
    q[1] = qy;
    q[2] = x2 * M[2] + y2 * M[5] + z * M[8];
}
/* arc_2d_to_3d (mccore 180012ae0) */
static void arc_2d_to_3d(A3 *a, short view, double z, const double *a2)
{
    a->view = view;
    a->c[0] = a2[0]; a->c[1] = a2[1]; a->c[2] = z;
    a->r = a2[2]; a->sa = a2[3]; a->sw = a2[4];
    arc_ep_3d(a, a->s, a->e);
}
/* ints_gt (systypes 180001670), the arc / arc branch arc_arc_ints reaches: aa_ints_new, the two points ordered by their
   distance to ref (the nearer first; ties: the second) */
static void ints_gt_aa(const GT *g1, const GT *g2, const double *ref, double tl, double *o1, double *o2, char *ok)
{
    double q1[2], q2[2];
    if (g1->type != 'A' || g2->type != 'A') { g_notdec = 1; *ok = 0; return; }
    aa_ints_new(g1->g, g2->g, tl, q1, q2, ok);
    if (*ok) {
        double d9 = q1[0] - ref[0], d8 = q2[0] - ref[0], d6 = q1[1] - ref[1], d7 = q2[1] - ref[1];
        if (d8 * d8 + d7 * d7 <= d9 * d9 + d6 * d6) {
            o1[0] = q2[0]; o1[1] = q2[1]; o2[0] = q1[0]; o2[1] = q1[1];
        } else {
            o1[0] = q1[0]; o1[1] = q1[1]; o2[0] = q2[0]; o2[1] = q2[1];
        }
    }
}
/* ints_on_gt_new (mccore 1804b6c40) */
static void ints_on_gt_new(const GT *g1, const GT *g2, const double *ref, double *out, int *n, double tl, char *ok)
{
    double p[2][2];
    ints_gt_aa(g1, g2, ref, tl, p[0], p[1], ok);
    if (*ok) {
        *ok = 0;
        int k = (g1->type == 'A' || g2->type == 'A') ? 2 : 1;
        *n = 0;
        for (int i = 0; i < k; i++)
            if (pt_on_gt_new(p[i], g1, tl) && pt_on_gt_new(p[i], g2, tl)) {
                out[2 * *n] = p[i][0]; out[2 * *n + 1] = p[i][1];
                (*n)++;
                *ok = 1;
            }
    }
}
/* pt_on_line (mccore 1804b7210) */
static char pt_on_line(const double *p, const double *l)
{
    double d5 = p[1] - l[3], d3 = p[0] - l[0], d4 = p[1] - l[1], d6 = p[0] - l[2];
    double d1 = d6 * d3 + d5 * d4;
    d3 = sqrt(d3 * d3 + d4 * d4);
    d4 = sqrt(d5 * d5 + d6 * d6);
    if (fabs(d3) < 1e-12 || fabs(d4) < 1e-12) return 1;
    if (d1 / (d4 * d3) + 1.0 <= TOL) {
        if (d1 <= TOL) return 1;
        if (d1 == 0.0) return d3 < TOL;
    }
    return 0;
}

/* ============================================================================ 2. the tp_ent boundary library (mdef) */
/* tp_ent (0x78 bytes): +0 int type (2 line, 4 arc), +4 byte colour, +6 byte (3 from FUN_18020c830), +7 the walk's "used"
   flag (FUN_180157090 / FUN_180157170 / FUN_180156d50), +8 FUN_1801563a0's "removed" flag, +0x10 l_3d (start, end) or
   a_3d (start +0x10, end +0x28, centre +0x40, r +0x58, start angle +0x60, sweep +0x68, view +0x70). */
typedef struct { int32_t type; uint8_t b[12]; A3 g; } TP;
typedef char tp_size_check[sizeof(TP) == 0x78 ? 1 : -1];
#define F7(t) ((t)->b[3])
#define F8(t) ((t)->b[4])

static TP *tp_alloc(long n) { return (TP *)calloc((size_t)(n > 0 ? n : 1), sizeof(TP)); }   /* AllocBdryEntMemNormal 18003f150 */
/* IFK_AF_TRACE=1: the stage arrays to stderr (debugging only) */
static void trace_tp(const char *what, const TP *t, int n)
{
    if (!getenv("IFK_AF_TRACE")) return;
    fprintf(stderr, "%s: %d\n", what, n);
    for (int i = 0; i < n; i++)
        fprintf(stderr, "  %d t%d (%.17g %.17g) -> (%.17g %.17g) c (%.17g %.17g) r %.17g sa %.17g sw %.17g\n", i, t[i].type, t[i].g.s[0],
                t[i].g.s[1], t[i].g.e[0], t[i].g.e[1], t[i].g.c[0], t[i].g.c[1], t[i].g.r, t[i].g.sa, t[i].g.sw);
}

/* p2Equal (1800ee380), p3Equal (1800ee3e0) [asm: the z lower bound re-tests y] */
static int p2Equal(const double *a, const double *b, double t)
{
    double dx = a[0] - b[0];
    if (dx <= t && (-t < dx || -t == dx)) {
        double dy = a[1] - b[1];
        if (dy <= t && (-t < dy || -t == dy)) return dy * dy + dx * dx < t * t;
    }
    return 0;
}
static int p3Equal(const double *a, const double *b, double t)
{
    double dx = b[0] - a[0];
    if (dx > t || -t > dx) return 0;
    double dy = b[1] - a[1];
    if (dy > t || -t > dy) return 0;
    double dz = b[2] - a[2];
    if (dz > t || -t > dy) return 0;
    return t * t > dy * dy + dx * dx + dz * dz;
}
static void V2ToV3(const double *p, double z, double *o) { double x = p[0], y = p[1]; o[2] = z; o[0] = x; o[1] = y; }   /* 1800edf80 */
/* RotatePt (1800edd70): p about c by a */
static void RotatePt(const double *p, const double *c, double a, double *o)
{
    double py = p[1], cy = c[1], px = p[0], cx = c[0];
    double co = COS(a), si = SIN(a);
    o[0] = ((px - cx) * co + c[0]) - (py - cy) * si;
    o[1] = (px - cx) * si + c[1] + (py - cy) * co;
}
/* CalcVAngle (1800ed870) [asm]: the signed angle from a to b, 1e30 when either is shorter than 1e-7 */
static double CalcVAngle(const double *a, const double *b)
{
    double la = sqrt(a[0] * a[0] + a[1] * a[1]);        /* FUN_1800edfa0 */
    double lb = sqrt(b[0] * b[0] + b[1] * b[1]);
    double cz = a[0] * b[1] - b[0] * a[1];               /* FUN_1800ee020 -> FUN_1800edfc0 z */
    double dot = a[1] * b[1] + a[0] * b[0] + 0.0 * 0.0;  /* FUN_1800ed980 -> FUN_1800ed950 */
    if (la > 1e-07 && lb > 1e-07) {
        double m = lb * la;
        double s = fabs(cz) / m, c = dot / m, f;
        if (cz == 0.0) f = 0.0;
        else if (0.0 > cz) f = -1.0;
        else f = 1.0;
        return ATAN2(s, c) * f;
    }
    return 1e+30;
}
/* t_to_pt_arc2 (180047f00) */
static void t_to_pt_arc2(double t, const double *a, double *p)
{
    double ang = t * a[4] + a[3];
    double c = COS(ang), r = a[2], s = SIN(ang), r2 = a[2], cy = a[1];
    p[0] = c * r + a[0];
    p[1] = s * r2 + cy;
}
/* GetEntPt2d (180044a60) */
static void GetEntPt2d(const TP *e, double t, double *p)
{
    double tl = TOL;
    if (e->type == 2) {
        if (fabs(t) < tl) { p[0] = e->g.s[0]; p[1] = e->g.s[1]; return; }
        if (tl <= fabs(1.0 - t)) {
            double ey = e->g.e[1], sy = e->g.s[1];
            p[0] = (e->g.e[0] - e->g.s[0]) * t + e->g.s[0];
            p[1] = (ey - sy) * t + e->g.s[1];
            return;
        }
    } else {
        if (fabs(t) < tl) { p[0] = e->g.s[0]; p[1] = e->g.s[1]; return; }
        if (tl <= fabs(1.0 - t)) {
            double ang = t * e->g.sw + e->g.sa;
            double c = COS(ang);
            p[0] = c * e->g.r + e->g.c[0];
            double s = SIN(ang);
            p[1] = s * e->g.r + e->g.c[1];
            return;
        }
    }
    p[0] = e->g.e[0]; p[1] = e->g.e[1];
}
/* FUN_1801859b0 / FUN_180185a20 / FUN_180185a60 / FUN_1801859d0: boxes (xmin, xmax, ymin, ymax) */
static void box_empty(double *b) { b[0] = b[1] = b[2] = b[3] = 1e+30; }
static void box_arc(double *b, const double *a) { b[0] = a[0] - a[2]; b[1] = a[0] + a[2]; b[2] = a[1] - a[2]; b[3] = a[1] + a[2]; }
static void box_line(double *b, const double *l)
{
    if (l[2] <= l[0]) { b[0] = l[2]; b[1] = l[0]; } else { b[0] = l[0]; b[1] = l[2]; }
    if (l[1] < l[3]) { b[2] = l[1]; b[3] = l[3]; } else { b[2] = l[3]; b[3] = l[1]; }
}
static int box_overlap(const double *a, const double *b, double t)
{
    if (b[0] <= t + a[1] && a[0] <= t + b[1] && b[2] <= t + a[3]) return a[2] <= t + b[3];
    return 0;
}
static void line2(const TP *e, double *l) { l[0] = e->g.s[0]; l[1] = e->g.s[1]; l[2] = e->g.e[0]; l[3] = e->g.e[1]; }
/* RemoveBdryEnt (180046580) */
static void RemoveBdryEnt(int i, TP *tp, int *n)
{
    for (int k = i + 1; k < *n; k++) tp[k - 1] = tp[k];
    (*n)--;
}
/* FUN_180158970: the reversed a_3d (start and end swapped, start angle = start + sweep restricted to [0, 2 pi], sweep negated) */
static void reverse_a3(A3 *o, const A3 *a)
{
    A3 t = *a;
    double s[3] = { a->s[0], a->s[1], a->s[2] }, e[3] = { a->e[0], a->e[1], a->e[2] };
    t.s[0] = e[0]; t.s[1] = e[1]; t.s[2] = e[2];
    t.e[0] = s[0]; t.e[1] = s[1]; t.e[2] = s[2];
    t.sa = a->sw + a->sa;
    restrict_angle(0.0, 6.283185307179586, &t.sa);
    t.sw = a->sw * -1.0;
    *o = t;
}
/* ReverseBdryEnt (180046600) */
static void ReverseBdryEnt(TP *o, const TP *e)
{
    TP t = *e;
    if (e->type == 2) {
        memcpy(t.g.s, e->g.e, 24);
        memcpy(t.g.e, e->g.s, 24);
    } else reverse_a3(&t.g, &e->g);
    *o = t;
}
/* AdjustArcCenter (18003eba0) [asm]: when CalcVAngle(p2 - p1, c - p1) is defined, the centre moves to the intersection of
   the line p1 -> c with the line p2 -> (p1 rotated about p2 by -angle) (ln_ln_ints2 code 0); returns |c - p1| then, else 1e30 */
static short ln_ln_ints2(const double *L1, const double *L2, double *P, double *T, double tl);
static double AdjustArcCenter(const double *p1, const double *p2, const double *c, double *out)
{
    double d1 = sqrt((c[1] - p1[1]) * (c[1] - p1[1]) + (c[0] - p1[0]) * (c[0] - p1[0]));
    double res = 1e+30;
    out[0] = c[0]; out[1] = c[1];
    double v1[2] = { p2[0] - p1[0], p2[1] - p1[1] }, v2[2] = { c[0] - p1[0], c[1] - p1[1] };
    double ang = CalcVAngle(v1, v2);
    if (fabs(TOL) > fabs(ang - 1e+30)) return 1e+30;
    double rot[2];
    RotatePt(p1, p2, -ang, rot);
    double l1[4] = { p1[0], p1[1], c[0], c[1] }, l2[4] = { p2[0], p2[1], rot[0], rot[1] }, P[4], T[4];
    if (ln_ln_ints2(l1, l2, P, T, TOL) == 0) {
        double d2 = sqrt((out[1] - p1[1]) * (out[1] - p1[1]) + (out[0] - p1[0]) * (out[0] - p1[0]));
        res = d2;
        if (fabs((d2 - d1) / d1) > 0.1) res = 1e+30;
        else { out[0] = P[0]; out[1] = P[1]; }
    }
    return res;
}
/* PtsTo2DArc (180046440): constr_ns_arc, retried up to 100 times with the centre moved by AdjustArcCenter (c is updated) */
static void PtsTo2DArc(const double *p1, const double *p2, double *c, char cw, double tl, double *a, char *ok)
{
    constr_ns_arc(p1, p2, c, cw, tl, a, ok);
    for (short k = 0; !*ok && k < 100; k++) {
        double nc[2];
        double d = AdjustArcCenter(p1, p2, c, nc);
        if (fabs(TOL) < fabs(d - 1e+30) || fabs(TOL) == fabs(d - 1e+30)) {
            c[0] = nc[0]; c[1] = nc[1];
            constr_ns_arc(p1, p2, c, cw, tl, a, ok);
        } else { *ok = 0; k = 100; }
    }
}
/* FUN_18015ad50: p projected on l, measured from the end of l nearer to p (t = 1 - t when the start is not farther) */
static void proj_near(const double *p, const double *l, double *o, double *t, char *ok)
{
    double d1 = sqrt((l[0] - p[0]) * (l[0] - p[0]) + (l[1] - p[1]) * (l[1] - p[1]));
    double d2 = sqrt((l[2] - p[0]) * (l[2] - p[0]) + (l[3] - p[1]) * (l[3] - p[1]));
    double m[4], u = 0.0;
    if (d1 <= d2) { m[0] = l[2]; m[1] = l[3]; m[2] = l[0]; m[3] = l[1]; }
    else { m[0] = l[0]; m[1] = l[1]; m[2] = l[2]; m[3] = l[3]; }
    proj_pt_ln(p, m, o, &u, ok);
    if (d1 <= d2) u = 1.0 - u;
    *t = u;
}
/* FUN_18015a420 [asm 18015a420..18015ac4a]: line / line when no end point of one lies on the other within tol (code -5 of
   ln_ln_ints2): 0 = crossing (P[0], T[0] on L1, T[1] on L2), 1 = degenerate, 2 = parallel, 3 = collinear and disjoint,
   4 = touching at one point, -1 / -2 / -4 = overlaps, -5 = none */
static short ll_general(const double *L1, const double *L2, double *P, double *T, double tl)
{
    double dx1 = L1[2] - L1[0], mdx2 = L2[0] - L2[2], dy1 = L1[3] - L1[1], mdy2 = L2[1] - L2[3];
    double ax = L2[0] - L1[0], bx = L2[0] - L1[2];
    double t2 = tl * tl;
    double ay = L2[1] - L1[1], by = L2[1] - L1[3];
    double det = dx1 * mdy2 - dy1 * mdx2;
    double num = ax * mdy2;
    double dot = dx1 * mdx2 + dy1 * mdy2;
    double q14 = ay * mdx2;
    num = num - q14;
    int inter = 0;
    if (t2 > fabs(dot)) {
        if (t2 > fabs(det)) return 1;
        inter = 1;
    } else if (!(tl > fabs(det / dot))) inter = 1;
    if (!inter) {
        double d1, d2;
        dist_pt_l(L1, L2, &d1);
        dist_pt_l(L1 + 2, L2, &d2);
        int cross = 0;
        if (d1 > tl && d2 > tl) {
            double s1 = q14 - ax * mdy2;
            double s2 = by * mdx2 - bx * mdy2;
            if (fabs(s1) > 2e-07 && fabs(s2) > 2e-07 && 0.0 > s2 * s1) cross = 1;
        }
        if (!cross) {
            double len2 = sqrt(mdx2 * mdx2 + mdy2 * mdy2);
            if (!(fabs(len2) > tl)) return 2;
            if (!(tl > fabs(num / len2))) return 2;
            /* collinear */
            double p[4][2] = { { L1[0], L1[1] }, { L1[2], L1[3] }, { L2[0], L2[1] }, { L2[2], L2[3] } };
            char a = pt_on_line(p[0], L2), b = pt_on_line(p[1], L2), c = pt_on_line(p[2], L1), d = pt_on_line(p[3], L1);
            if (!a) { a = (char)p2Equal(p[0], p[2], TOL); if (!a) a = (char)p2Equal(p[0], p[3], TOL); }
            if (!b) { b = (char)p2Equal(p[1], p[2], TOL); if (!b) b = (char)p2Equal(p[1], p[3], TOL); }
            if (!c) { c = (char)p2Equal(p[2], p[0], TOL); if (!c) c = (char)p2Equal(p[2], p[1], TOL); }
            if (!d) { d = (char)p2Equal(p[3], p[0], TOL); if (!d) d = (char)p2Equal(p[3], p[1], TOL); }
            short code = -5;
            if (a && b) code = (c && d) ? 1 : 2;
            else if (a || b) code = ((c && d) || (!c && !d)) ? -5 : 5;
            else if (c && d) code = 3;
            else if (!c && !d) code = 0;
            switch (code) {
            case 0: return 3;
            case 1:
                P[0] = p[0][0]; P[1] = p[0][1]; P[2] = p[1][0]; P[3] = p[1][1];
                t_on_line(&T[0], P, L1); t_on_line(&T[1], P + 2, L1); t_on_line(&T[2], P, L2); t_on_line(&T[3], P + 2, L2);
                return -4;
            case 2:
                P[0] = p[0][0]; P[1] = p[0][1]; P[2] = p[1][0]; P[3] = p[1][1];
                t_on_line(&T[0], P, L1); t_on_line(&T[1], P + 2, L1); t_on_line(&T[2], P, L2); t_on_line(&T[3], P + 2, L2);
                return -2;
            case 3:
                P[0] = p[2][0]; P[1] = p[2][1]; P[2] = p[3][0]; P[3] = p[3][1];
                t_on_line(&T[0], P, L1); t_on_line(&T[1], P + 2, L1); t_on_line(&T[2], P, L2); t_on_line(&T[3], P + 2, L2);
                return -1;
            case 5:
                if (a) { P[0] = p[0][0]; P[1] = p[0][1]; } else if (b) { P[0] = p[1][0]; P[1] = p[1][1]; }
                t_on_line(&T[0], P, L1); t_on_line(&T[1], P, L2);
                return 4;
            default: {
                int e1 = p2Equal(p[0], p[2], TOL); if (!e1) e1 = p2Equal(p[0], p[3], TOL);
                int e2 = p2Equal(p[1], p[2], TOL); if (!e2) e2 = p2Equal(p[1], p[3], TOL);
                if (e1 && e2) {
                    P[0] = p[0][0]; P[1] = p[0][1]; P[2] = p[1][0]; P[3] = p[1][1];
                    t_on_line(&T[0], P, L1); t_on_line(&T[1], P + 2, L1); t_on_line(&T[2], P, L2); t_on_line(&T[3], P + 2, L2);
                    return -4;
                }
                if (!e1 && !e2) return -5;
                double l1 = (p[1][1] - p[0][1]) * (p[1][1] - p[0][1]) + (p[1][0] - p[0][0]) * (p[1][0] - p[0][0]);
                double l2 = (p[3][1] - p[2][1]) * (p[3][1] - p[2][1]) + (p[3][0] - p[2][0]) * (p[3][0] - p[2][0]);
                short rc;
                if (l1 > l2) { P[0] = p[2][0]; P[1] = p[2][1]; P[2] = p[3][0]; P[3] = p[3][1]; rc = -1; }
                else { P[0] = p[0][0]; P[1] = p[0][1]; P[2] = p[1][0]; P[3] = p[1][1]; rc = -2; }
                t_on_line(&T[0], P, L1); t_on_line(&T[1], P + 2, L1); t_on_line(&T[2], P, L2); t_on_line(&T[3], P + 2, L2);
                return rc;
            }
            }
        }
    }
    /* crossing */
    double t1 = num / det;
    T[0] = t1;
    double px = t1 * dx1;
    double u = ay * dx1 - ax * dy1;
    double py = dy1 * T[0];
    u = u / det;
    P[0] = L1[0] + px;
    P[1] = L1[1] + py;
    T[1] = u;
    return 0;
}
/* ln_ln_ints2 (180159bf0) [asm 180159bf0..18015a41b]: the shorter line S against the longer G; shared end points first
   (codes -4 identical, -1 / -2 overlap, -3 partial overlap, 4 touching, 0 / 3 others), then ll_general (-5) */
static short ln_ln_ints2(const double *L1, const double *L2, double *P, double *T, double tl)
{
    P[0] = P[1] = P[2] = P[3] = 0.0;
    T[0] = T[1] = T[2] = T[3] = 0.0;
    short ebx = -5;
    if (p2Equal(L1, L1 + 2, tl) || p2Equal(L2, L2 + 2, tl)) return 1;
    double R0[2] = { 0, 0 }, R1[2] = { 0, 0 }, A = 0, B = 0, C = 0, D = 0, x6 = 0, x8 = 0;
    double dl1 = (L1[1] - L1[3]) * (L1[1] - L1[3]) + (L1[0] - L1[2]) * (L1[0] - L1[2]);
    double dl2 = (L2[1] - L2[3]) * (L2[1] - L2[3]) + (L2[0] - L2[2]) * (L2[0] - L2[2]);
    int r12;
    double S[4], G[4];
    if (dl2 > dl1) { r12 = 0; memcpy(S, L1, 32); memcpy(G, L2, 32); }
    else { r12 = 1; memcpy(S, L2, 32); memcpy(G, L1, 32); }
    double d, t, Pt[2];
    char ok;
    enum { OUT, TAIL, GEN } next;
    if (p2Equal(S, G, tl)) {
        int q = p2Equal(S + 2, G + 2, tl);
        R0[0] = S[0]; R0[1] = S[1];
        if (q) goto ident;
        x8 = 0.0;
        dist_pt_l(S + 2, G, &d);
        if (!(tl > d)) { ebx = 0; x6 = 0.0; next = OUT; goto done; }
        proj_near(S + 2, G, Pt, &t, &ok);
        if (!ok) return -5;
        if (0.0 > t || t > 1.0) { ebx = 4; x6 = 0.0; next = OUT; goto done; }
        B = 1.0; C = 0.0;
        goto shared;
    }
    if (p2Equal(S + 2, G + 2, tl)) {
        R0[0] = S[2]; R0[1] = S[3];
        x6 = 1.0; x8 = 1.0;
        dist_pt_l(S, G, &d);
        if (!(tl > d)) { ebx = 0; next = OUT; goto done; }
        proj_near(S, G, Pt, &t, &ok);
        if (!ok) return -5;
        if (0.0 > t || t > x6) { ebx = 4; next = OUT; goto done; }
        B = 1.0; C = 1.0;
        goto shared;
    }
    if (p2Equal(S, G + 2, tl)) {
        int q = p2Equal(S + 2, G, tl);
        R0[0] = S[0]; R0[1] = S[1];
        if (q) goto ident;
        x8 = 0.0;
        dist_pt_l(S + 2, G, &d);
        if (!(tl > d)) { x6 = 1.0; ebx = 0; next = OUT; goto done; }
        proj_near(S + 2, G, Pt, &t, &ok);
        if (!ok) return -5;
        x6 = 1.0;
        if (0.0 > t || t > x6) { ebx = 4; next = OUT; goto done; }
        B = 1.0; C = 1.0;
        goto shared;
    }
    if (p2Equal(S + 2, G, tl)) {
        R0[0] = S[2]; R0[1] = S[3];
        x8 = 1.0;
        dist_pt_l(S, G, &d);
        if (!(tl > d)) { ebx = 0; x6 = 0.0; next = OUT; goto done; }
        proj_near(S, G, Pt, &t, &ok);
        if (!ok) return -5;
        if (0.0 > t || t > x8) { ebx = 4; x6 = 0.0; next = OUT; goto done; }
        B = 1.0; C = 0.0;
        goto shared;
    }
    {   /* no shared end point */
        double d1, d2, t1 = 0.0, t2 = 0.0, P1[2] = { 0, 0 }, P2[2] = { 0, 0 };
        dist_pt_l(S, G, &d1);
        if (tl > d1) {
            proj_near(S, G, P1, &t1, &ok);
            if (!ok) return -5;
            if (t1 >= 0.0 && 1.0 >= t1) { R0[0] = P1[0]; R0[1] = P1[1]; ebx = 0; x8 = 0.0; x6 = t1; }
            else { x6 = B; x8 = A; }
        } else { x6 = B; x8 = A; }
        dist_pt_l(S + 2, G, &d2);
        if (tl > d2) {
            proj_near(S + 2, G, P2, &t2, &ok);
            if (!ok) return -5;
            if (t2 >= 0.0 && 1.0 >= t2) { ebx = 0; x8 = 1.0; R0[0] = P2[0]; R0[1] = P2[1]; x6 = t2; }
        }
        if (!(tl > d1) || !(tl > d2)) { next = (ebx == -5) ? GEN : TAIL; goto done; }
        int in1 = t1 >= 0.0 && 1.0 >= t1, in2 = t2 >= 0.0 && 1.0 >= t2;
        if (!in1) {
            if (in2) { C = t2; ebx = -3; x8 = 1.0; R0[0] = P2[0]; R0[1] = P2[1]; goto part; }
            if (ebx == -5) return 3;
            next = TAIL; goto done;
        }
        if (!in2) { R0[0] = P1[0]; R0[1] = P1[1]; C = t1; ebx = -3; x8 = 0.0; goto part; }
        R0[0] = P1[0]; R0[1] = P1[1];
        ebx = r12 ? -1 : -2;
        C = t1; D = t2; A = 0.0; B = 1.0; x6 = B; x8 = A;
        R1[0] = P2[0]; R1[1] = P2[1];
        next = TAIL; goto done;
    }
ident:   /* 180159dc4: both ends shared */
    A = 0.0; B = 1.0;
    ebx = -4;
    x6 = B; x8 = A;
    R1[0] = S[2]; R1[1] = S[3];
    C = 0.0; D = 1.0;
    next = OUT; goto done;
shared:  /* 180159e63 / 18015a006: one end shared, the other projects inside G */
    ebx = r12 ? -1 : -2;
    x6 = B;
    D = t;
    R1[0] = Pt[0]; R1[1] = Pt[1];
    next = TAIL; goto done;
part:    /* 18015a232: ebx == -3 */
    proj_near(G, S, Pt, &t, &ok);
    D = 0.0;
    if (!ok) return -5;
    x6 = t;
    if (0.0 > t || t > 1.0) {
        proj_near(G + 2, S, Pt, &t, &ok);
        D = 1.0;
        if (!ok) return -5;
        x6 = t;
    }
    R1[0] = Pt[0]; R1[1] = Pt[1];
    if (p2Equal(R0, R1, tl)) { x6 = C; ebx = 0; }
    next = OUT;
done:
    if (next == GEN) return ll_general(L1, L2, P, T, tl);
    if (next == TAIL) {
        if (ebx == 2 || ebx == 3) return ebx;
        if (ebx == -3 || ebx == -2 || ebx == -1) { if (p2Equal(R0, R1, tl)) { x6 = C; ebx = 0; } }
    }
    P[0] = R0[0]; P[1] = R0[1]; P[2] = R1[0]; P[3] = R1[1];
    if (r12) {
        if (ebx > -1) { T[0] = x6; T[1] = x8; }
        else { T[0] = C; T[1] = D; T[2] = x8; T[3] = x6; }
    } else { T[0] = x8; T[1] = x6; T[2] = C; T[3] = D; }
    return ebx;
}
/* FUN_180158680: p on the line l (an end within tol, else systypes pt_on_line_tol) */
static char on_line_or_end(const double *p, const double *l, double tl)
{
    if (p2Equal(l, p, tl) || p2Equal(l + 2, p, tl)) return 1;
    return pt_on_line_tol(p, l, tl);
}
/* line_arc_ints (180159630): P (2 points), T (t on the line, t on the arc, for each point); 0 none, 1 / 2 points, -1 touching */
static short line_arc_ints(const double *L, const A3 *arc, double *P, double *T, double tl)
{
    double a[5], f;
    char ok;
    arc_3d_to_2d(a, arc);
    proj_near(a, L, P, T, &ok);
    if (!ok) return 0;
    double dx = P[0] - a[0], dy = P[1] - a[1];
    double d = sqrt(dy * dy + dx * dx);
    if (!(d <= a[2] + tl)) return 0;
    dist_pt_l(a, L, &f);
    d = sqrt(dy * dy + dx * dx);
    if (fabs(tl / 10.0) < fabs(d - a[2]) || fabs(tl / 10.0) == fabs(d - a[2]) || f <= tl) {
        la_ints(L, a, P, P + 2, &ok);
        if (ok && (on_line_or_end(P, L, tl) || on_line_or_end(P + 2, L, tl)) && (pt_on_arc(P, a, tl) || pt_on_arc(P + 2, a, tl))) {
            t_on_line(&T[0], P, L);
            t_on_arc(&T[1], P, a);
            t_on_line(&T[2], P + 2, L);
            t_on_arc(&T[3], P + 2, a);
            return p2Equal(P, P + 2, tl) ? 1 : 2;
        }
        return 0;
    }
    double ex = P[0] - a[0], ey = P[1] - a[1];
    double l = sqrt(ey * ey + ex * ex);
    double qx = ex * (a[2] / l), qy = ey * (a[2] / l);
    P[2] = qx + a[0]; P[3] = qy + a[1];
    t_on_arc(&T[1], P + 2, a);
    double as[2] = { arc->s[0], arc->s[1] }, ae[2] = { arc->e[0], arc->e[1] };
    if (T[0] < 0.0 || 1.0 < T[0] || T[1] < 0.0 || 1.0 < T[1]) {
        if (p2Equal(as, L, tl) || p2Equal(as, L + 2, tl) || p2Equal(ae, L, tl) || p2Equal(ae, L + 2, tl)) return 0;
        proj_pt_ar(L, a, P + 2, &ok);
        if (ok && p2Equal(P + 2, L, tl)) {
            t_on_arc(&T[1], P + 2, a);
            if (0.0 <= T[1] && T[1] < 1.0) { P[0] = L[0]; P[1] = L[1]; T[0] = 0.0; return -1; }
        }
        proj_pt_ar(L + 2, a, P + 2, &ok);
        if (ok && p2Equal(P + 2, L + 2, tl)) {
            t_on_arc(&T[1], P + 2, a);
            if (0.0 <= T[1] && T[1] < 1.0) { P[0] = L[2]; P[1] = L[3]; T[0] = 1.0; return -1; }
        }
        proj_near(as, L, P, T, &ok);
        if (!ok || !p2Equal(P, as, tl) || T[0] < 0.0 || 1.0 < T[0]) {
            proj_near(ae, L, P, T, &ok);
            if (!ok) return 0;
            if (!p2Equal(P, ae, tl)) return 0;
            if (T[0] < 0.0) return 0;
            if (T[0] <= 1.0) { P[2] = ae[0]; P[3] = ae[1]; T[1] = 1.0; return -1; }
            return 0;
        }
        P[2] = as[0]; P[3] = as[1];
        T[1] = 0.0;
    }
    return -1;
}
/* arc_arc_ints (180158bc0) [asm 180158bc0..18015962e]: 0 none, 1 / 2 / 7 overlaps (A2 in A1, A1 in A2, identical), 5 / 6 =
   1 / 2 points (P, T: t on A1, t on A2 per point), 4 = no point; *conc: same circle */
static int arc_arc_ints(const A3 *A1, const A3 *A2, double *P, double *T, int *cnt, char *conc, double tl)
{
    double a1[5], a2[5];
    arc_3d_to_2d(a1, A1);
    arc_3d_to_2d(a2, A2);
    P[0] = P[1] = P[2] = P[3] = 0.0;
    T[0] = T[1] = T[2] = T[3] = 0.0;
    *cnt = 0;
    double rsum = a2[2] + a1[2], rdif = fabs(a1[2] - a2[2]);
    *conc = 0;
    double dc = sqrt((a2[1] - a1[1]) * (a2[1] - a1[1]) + (a2[0] - a1[0]) * (a2[0] - a1[0]));
    if (dc > rsum + tl) return 0;
    if (p2Equal(a1, a2, tl) && fabs(tl) > fabs(a1[2] - a2[2])) *conc = 1;
    GT g1 = { 'A', { a1[0], a1[1], a1[2], a1[3], a1[4] } }, g2 = { 'A', { a2[0], a2[1], a2[2], a2[3], a2[4] } };
    double s1[2] = { A1->s[0], A1->s[1] }, e1[2] = { A1->e[0], A1->e[1] }, m1[2];
    t_to_pt_arc2(0.5, a1, m1);
    char on2[3], on1[3];
    on2[0] = pt_on_arc(s1, a2, tl); on2[1] = pt_on_arc(e1, a2, tl); on2[2] = pt_on_arc(m1, a2, tl);
    double s2[2] = { A2->s[0], A2->s[1] }, e2[2] = { A2->e[0], A2->e[1] }, m2[2];
    t_to_pt_arc2(0.5, a2, m2);
    on1[0] = pt_on_arc(s2, a1, tl); on1[1] = pt_on_arc(e2, a1, tl); on1[2] = pt_on_arc(m2, a1, tl);
    char ok;
    if (!*conc && (fabs(TOL) > fabs(dc - rsum) || fabs(TOL) > fabs(dc - rdif))) {
        /* tangent circles [asm 180158e83..180159256] */
        proj_pt_ar(s1, a2, P, &ok);
        if (ok && p2Equal(P, s1, tl)) {
            T[0] = 0.0; t_on_arc(&T[1], P, a2);
            if ((T[1] >= 0.0 && 1.0 >= T[1]) || p2Equal(P, s2, tl) || p2Equal(P, e2, tl)) return 5;
        }
        proj_pt_ar(e1, a2, P, &ok);
        if (ok && p2Equal(P, e1, tl)) {
            T[0] = 0.0; t_on_arc(&T[1], P, a2);
            if ((T[1] >= 0.0 && 1.0 >= T[1]) || p2Equal(P, s2, tl) || p2Equal(P, e2, tl)) return 5;
        }
        proj_pt_ar(s2, a1, P, &ok);
        if (ok && p2Equal(P, s2, tl)) {
            T[1] = 0.0; t_on_arc(&T[0], P, a1);
            if ((T[0] >= 0.0 && 1.0 >= T[0]) || p2Equal(P, s1, tl) || p2Equal(P, e1, tl)) return 5;
        }
        proj_pt_ar(e2, a1, P, &ok);
        if (ok && p2Equal(P, e2, tl)) {
            T[1] = 0.0; t_on_arc(&T[0], P, a1);
            if ((T[0] >= 0.0 && 1.0 >= T[0]) || p2Equal(P, s1, tl) || p2Equal(P, e1, tl)) return 5;
        }
        double vx, vy, ang;
        if (a1[2] > a2[2]) { vx = a2[0] - a1[0]; vy = a2[1] - a1[1]; }
        else { vx = a1[0] - a2[0]; vy = a1[1] - a2[1]; }
        if (1e-12 > vy * vy + vx * vx) ang = 0.0;
        else { ang = ATAN2(vy, vx); if (0.0 > ang) ang = ang + 6.283185307179586; }
        if (fabs(1e-06) > fabs(ang - 0.0)) ang = 0.0;
        if (fabs(1e-06) > fabs(ang - 6.283185307179586)) ang = 6.283185307179586;
        restrict_angle(0.0, 6.283185307179586, &ang);
        if (a1[2] > a2[2]) { P[0] = COS(ang) * a1[2] + a1[0]; P[1] = SIN(ang) * a1[2] + a1[1]; }   /* RThetaToXY 1800edbf0 */
        else { P[0] = COS(ang) * a2[2] + a2[0]; P[1] = SIN(ang) * a2[2] + a2[1]; }
        if (pt_on_gt_new(P, &g1, tl) && pt_on_gt_new(P, &g2, tl)) {
            t_on_arc(&T[0], P, a1); t_on_arc(&T[1], P, a2);
            return 5;
        }
        return 0;
    }
    int code;
    if (on2[0] && on2[1] && on2[2]) code = (on1[0] && on1[1] && on1[2]) ? 1 : 2;
    else if (on1[0]) code = (on1[1] && on1[2]) ? 3 : 5;
    else code = (on1[1] || on1[2] || on2[0] || on2[1] || on2[2]) ? 5 : 4;
    switch (code) {
    case 1:
        P[0] = s1[0]; P[1] = s1[1]; P[2] = e1[0]; P[3] = e1[1];
        T[0] = 0.0;
        if (p2Equal(s1, s2, tl)) { T[1] = 0.0; T[2] = 1.0; } else { T[1] = 1.0; T[2] = 0.0; }
        T[3] = 1.0;
        return 7;
    case 2:
        P[0] = s1[0]; P[1] = s1[1]; P[2] = e1[0]; P[3] = e1[1];
        T[0] = 0.0; t_on_arc(&T[1], P, a2); T[2] = 1.0; t_on_arc(&T[3], P + 2, a2);
        return 2;
    case 3:
        P[0] = s2[0]; P[1] = s2[1]; P[2] = e2[0]; P[3] = e2[1];
        t_on_arc(&T[0], P, a1); T[1] = 0.0; t_on_arc(&T[2], P + 2, a1); T[3] = 1.0;
        return 1;
    case 4: {
        *cnt = 0;
        char ok2;
        ints_on_gt_new(&g1, &g2, a1, P, cnt, tl, &ok2);
        if (ok2 && *cnt == 2 && p2Equal(P, P + 2, tl)) *cnt = 1;
        if (*cnt >= 1) { t_on_arc(&T[0], P, a1); t_on_arc(&T[1], P, a2); }
        if (*cnt == 2) { t_on_arc(&T[2], P + 2, a1); t_on_arc(&T[3], P + 2, a2); }
        return *cnt >= 1 ? *cnt + 4 : 0;
    }
    default: {   /* 5 */
        if (*conc) {
            *cnt = 0;
            P[0] = 1e+30; P[1] = 1e+30;
            const double *ends1[2] = { s1, e1 }, *ends2[2] = { s2, e2 };
            for (int k = 0; k < 2; k++) {
                if (*cnt >= 2) break;
                if (on2[k] && !p2Equal(P, ends1[k], tl)) { P[2 * *cnt] = ends1[k][0]; P[2 * *cnt + 1] = ends1[k][1]; (*cnt)++; }
                if (on1[k] && !p2Equal(P, ends2[k], tl)) { P[2 * *cnt] = ends2[k][0]; P[2 * *cnt + 1] = ends2[k][1]; (*cnt)++; }
            }
        } else {
            char ok2;
            ints_on_gt_new(&g1, &g2, a1, P, cnt, tl, &ok2);
        }
        if (*cnt >= 1) { t_on_arc(&T[0], P, a1); t_on_arc(&T[1], P, a2); }
        if (*cnt == 2) { t_on_arc(&T[2], P + 2, a1); t_on_arc(&T[3], P + 2, a2); }
        return *cnt + 4;
    }
    }
}

/* the intersection list of BrkBdryEntsInts (intpt_type, 0x30 bytes: point, t on the current entity, 3 unused doubles; at most
   100, FUN_1800418d0 clears it) */
typedef struct { double p[2], t, pad[3]; } IP;
#define MAXIP 100
/* FUN_18003e790: adds p (t = tA on A) unless it is an end of A, outside B (tB beyond [0, 1] and not at B's end), or already
   in the list; the list stays sorted by t */
static void add_intpt(const TP *A, const TP *B, const double *p, double tA, double tB, IP *ip, int *nip)
{
    if (*nip == MAXIP || !(0.0 <= tA) || !(tA <= 1.0)) return;
    if (p2Equal(p, A->g.s, TOL) || p2Equal(p, A->g.e, TOL)) return;
    if (!((0.0 <= tB) || p2Equal(p, B->g.s, TOL))) return;
    if (!((tB <= 1.0) || p2Equal(p, B->g.e, TOL))) return;
    int pos = 0;
    for (int k = 0; k < *nip; k++) {
        if (p2Equal(p, ip[k].p, TOL)) return;
        if (ip[k].t < tA) pos++;
    }
    for (int k = *nip; k > pos; k--) ip[k] = ip[k - 1];
    ip[pos].p[0] = p[0]; ip[pos].p[1] = p[1]; ip[pos].t = tA;
    (*nip)++;
}
/* FUN_180047660: a partial overlap of two lines (ln_ln_ints2 -3): the end of line j that is at P[0] or P[1] (its start when
   either point is its start) moves to P[k], k = 0 when T[0] is 0 or 1 (within 1e-8) */
static void trim_line(const double *P, const double *T, const double *Lo, int j, TP *tp)
{
    int k = (fabs(T[0] - 0.0) < 1e-08 || fabs(T[0] - 1.0) < 1e-08) ? 0 : 1;
    double *dst = (p2Equal(P, Lo, TOL) || p2Equal(P + 2, Lo, TOL)) ? tp[j].g.s : tp[j].g.e;
    V2ToV3(P + 2 * k, 0.0, dst);
}
/* FUN_180047350 ("TrimBdryArc", BoundaryMath.cpp): two arcs of one circle overlapping (arc_arc_ints 6, concentric): the longer
   one (|sweep| x r; ties: j) is cut back at P[0] or P[1] */
static void trim_arc(const double *P, int i, int j, TP *tp)
{
    int k = (fabs(tp[j].g.sw * tp[j].g.r) < fabs(tp[i].g.sw * tp[i].g.r)) ? i : j;
    A3 *g = &tp[k].g;
    double s[2] = { g->s[0], g->s[1] }, e[2] = { g->e[0], g->e[1] }, c[2] = { g->c[0], g->c[1] }, a[5];
    char cw = g->sw <= 0.0 && g->sw != 0.0, ok;
    if (p2Equal(P, s, TOL)) {
        PtsTo2DArc(P, e, c, cw, TOL, a, &ok); arc_2d_to_3d(g, 1, 0.0, a); V2ToV3(P, 0.0, g->s); V2ToV3(e, 0.0, g->e);
    } else if (p2Equal(P + 2, s, TOL)) {
        PtsTo2DArc(P + 2, e, c, cw, TOL, a, &ok); arc_2d_to_3d(g, 1, 0.0, a); V2ToV3(P + 2, 0.0, g->s); V2ToV3(e, 0.0, g->e);
    } else if (p2Equal(P, e, TOL)) {
        PtsTo2DArc(s, P, c, cw, TOL, a, &ok); arc_2d_to_3d(g, 1, 0.0, a); V2ToV3(s, 0.0, g->s); V2ToV3(P, 0.0, g->e);
    } else if (p2Equal(P + 2, e, TOL)) {
        PtsTo2DArc(s, P + 2, c, cw, TOL, a, &ok); arc_2d_to_3d(g, 1, 0.0, a); V2ToV3(s, 0.0, g->s); V2ToV3(P + 2, 0.0, g->e);
    } else mc_assert();
}
/* FUN_18003db80: appends e to out unless it is degenerate, a full circle, or a duplicate of an entry (same ends, and for
   arcs the same centre / sweep or the same mid point; a line and an arc with the same ends when the arc's mid point is on
   the line); a too short arc whose chord is at least tol becomes a line */
static void add_ent(const TP *in, TP *out, int *cnt)
{
    TP e = *in;
    int skip = 0, keep = 1, valid;
    if (e.type == 2) valid = !p3Equal(e.g.s, e.g.e, TOL);
    else {
        restrict_angle(0.0, 6.283185307179586, &e.g.sa);
        restrict_angle(-6.283185307179586, 6.283185307179586, &e.g.sw);
        double sx = e.g.s[0], sy = e.g.s[1], ex = e.g.e[0], ey = e.g.e[1];
        valid = MINARC < fabs(e.g.sw) * e.g.r;
        keep = 0.0001 <= fabs(fabs(e.g.sw) - 6.283185307179586);
        if (!keep) return;
        if (!valid) {
            double d = sqrt((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy));
            if (fabs(TOL) < fabs(d - 0.0) || fabs(TOL) == fabs(d - 0.0)) {
                double p[2] = { sx, sy }, q[2] = { ex, ey };
                V2ToV3(p, 0.0, e.g.s);
                V2ToV3(q, 0.0, e.g.e);
                e.type = 2;
                valid = 1;
            }
        }
    }
    for (int k = 0; valid && !skip && k < *cnt; k++) {
        const TP *o = &out[k];
        if (e.type == 2 && o->type == 2) {
            if ((p3Equal(o->g.s, e.g.s, TOL) && p3Equal(o->g.e, e.g.e, TOL)) || (p3Equal(o->g.e, e.g.s, TOL) && p3Equal(o->g.s, e.g.e, TOL))) skip = 1;
        } else if (e.type == 4 && o->type == 4) {
            if (p3Equal(o->g.c, e.g.c, TOL)) {
                if (p3Equal(o->g.s, e.g.s, TOL) && p3Equal(o->g.e, e.g.e, TOL) && fabs(o->g.sw - e.g.sw) < 1e-06) { skip = 1; continue; }
                double dd = fabs(o->g.sw) - fabs(e.g.sw);
                if (p3Equal(o->g.s, e.g.e, TOL) && p3Equal(o->g.e, e.g.s, TOL) && fabs(dd) < 1e-06 && !(0.0 <= o->g.sw * e.g.sw)) { skip = 1; continue; }
            }
            if ((p3Equal(o->g.s, e.g.s, TOL) && p3Equal(o->g.e, e.g.e, TOL)) || (p3Equal(o->g.s, e.g.e, TOL) && p3Equal(o->g.e, e.g.s, TOL))) {
                double m1[2], m2[2];
                GetEntPt2d(o, 0.5, m1);
                GetEntPt2d(&e, 0.5, m2);
                if (p2Equal(m1, m2, TOL)) skip = 1;
            }
        } else {
            /* one line and one arc: a = the arc, l = the line (LAB_18003e145 / the e-arc branch) */
            const TP *arc = (o->type == 2) ? &e : o, *ln = (o->type == 2) ? o : &e;
            double a[5], as[2] = { arc->g.s[0], arc->g.s[1] }, ae[2] = { arc->g.e[0], arc->g.e[1] };
            double l[4] = { ln->g.s[0], ln->g.s[1], ln->g.e[0], ln->g.e[1] };
            arc_3d_to_2d(a, &arc->g);
            if ((p2Equal(l, as, TOL) && p2Equal(l + 2, ae, TOL)) || (p2Equal(l, ae, TOL) && p2Equal(l + 2, as, TOL))) {
                double m[2], d;
                t_to_pt_arc2(0.5, a, m);
                dist_pt_l(m, l, &d);
                if (d < TOL) skip = 1;
            }
        }
    }
    if (keep && valid && !skip) {
        if (*cnt >= 10000) { g_limit = 1; return; }
        out[(*cnt)++] = e;
    }
}
/* FUN_18003e3d0: the entity split at its sorted intersection points (lines stay lines; arc pieces through PtsTo2DArc about the
   centre, a line when that fails or the piece would not be shorter than the arc), each piece through add_ent */
static void split_ent(const TP *ent, const IP *ip, int nip, TP *out, int *cnt)
{
    if (*cnt == 9999) return;
    if (nip == 0) { add_ent(ent, out, cnt); return; }
    double cur[3] = { ent->g.s[0], ent->g.s[1], ent->g.s[2] };
    double c2[2] = { 0, 0 };
    char cw = 0, ok;
    if (ent->type != 2) { c2[0] = ent->g.c[0]; c2[1] = ent->g.c[1]; cw = ent->g.sw <= 0.0 && ent->g.sw != 0.0; }
    for (int k = 0; k < nip; k++) {
        TP piece = *ent;
        if (ent->type != 2) {
            double a[5], p[2] = { cur[0], cur[1] };
            PtsTo2DArc(p, ip[k].p, c2, cw, TOL, a, &ok);
            if (!ok || fabs(ent->g.sw) < fabs(a[4]) || fabs(ent->g.sw) == fabs(a[4])) piece.type = 2;
            else arc_2d_to_3d(&piece.g, 1, 0.0, a);
        }
        piece.g.s[2] = cur[2];
        piece.g.s[0] = cur[0]; piece.g.s[1] = cur[1];
        V2ToV3(ip[k].p, 0.0, piece.g.e);
        cur[0] = piece.g.e[0]; cur[1] = piece.g.e[1]; cur[2] = piece.g.e[2];
        add_ent(&piece, out, cnt);
    }
    TP piece = *ent;
    const double *last = ip[nip - 1].p;
    if (ent->type != 2) {
        double a[5], e[2] = { ent->g.e[0], ent->g.e[1] };
        PtsTo2DArc(last, e, c2, cw, TOL, a, &ok);
        if (!ok || fabs(ent->g.sw) < fabs(a[4]) || fabs(ent->g.sw) == fabs(a[4])) piece.type = 2;
        else arc_2d_to_3d(&piece.g, 1, 0.0, a);
    }
    V2ToV3(last, 0.0, piece.g.s);
    piece.g.e[0] = ent->g.e[0]; piece.g.e[1] = ent->g.e[1]; piece.g.e[2] = ent->g.e[2];
    add_ent(&piece, out, cnt);
}
static int g_brkflag;   /* DAT_1802f9350: the current entity survives the intersection pass */
/* IntersectBdryLineBdry (1800457a0) [asm for the argument order] */
static void IntersectBdryLineBdry(TP *tp, int *n, int *pi, IP *ip, int *nip)
{
    double tol0 = TOL, b1[4], b2[4];
    box_empty(b1); box_empty(b2);
    int ii = *pi, stop = 0;
    if (ii < 0 || tp[ii].type != 2) return;
    memset(ip, 0, sizeof(IP) * MAXIP); *nip = 0;
    double L[4];
    line2(&tp[ii], L);
    box_line(b1, L);
    for (int j = 0; j < *n; j++) {
        if (stop) break;
        if (j == ii) continue;
        TP *o = &tp[j];
        double P[6] = { 0 }, T[4] = { 0 };
        if (o->type == 2) {
            double Lo[4];
            line2(o, Lo);
            box_line(b2, Lo);
            if (!box_overlap(b1, b2, tol0 * 2.0)) continue;
            short r = ln_ln_ints2(L, Lo, P, T, TOL);
            int rem_j = 0, rem_i = 0;
            if (r == -4 || r == -1) rem_j = 1;
            else if (r == -3) {
                if ((p2Equal(P, L, TOL) && p2Equal(P + 2, L + 2, TOL)) || (p2Equal(P, L + 2, TOL) && p2Equal(P + 2, L, TOL))) rem_i = 1;
                else if ((p2Equal(P, Lo, TOL) && p2Equal(P + 2, Lo + 2, TOL)) || (p2Equal(P, Lo + 2, TOL) && p2Equal(P + 2, Lo, TOL))) rem_j = 1;
                else trim_line(P, T, Lo, j, tp);
            } else if (r == -2) rem_i = 1;
            else if (r == 0) add_intpt(&tp[ii], o, P, T[0], T[1], ip, nip);
            if (rem_i) { RemoveBdryEnt(ii, tp, n); *nip = 0; g_brkflag = 0; stop = 1; }
            if (rem_j) { RemoveBdryEnt(j, tp, n); if (j < ii) ii--; j--; }
        } else {
            double a[5];
            arc_3d_to_2d(a, &o->g);
            box_arc(b2, a);
            if (!box_overlap(b1, b2, tol0 * 2.0)) continue;
            double os[2] = { o->g.s[0], o->g.s[1] }, oe[2] = { o->g.e[0], o->g.e[1] }, dd;
            if ((p2Equal(L, os, TOL) && p2Equal(L + 2, oe, TOL)) || (p2Equal(L, oe, TOL) && p2Equal(L + 2, os, TOL))) {
                double m[2];
                t_to_pt_arc2(0.5, a, m);
                dist_pt_l(m, L, &dd);
            } else dd = 1.0;
            if (dd < TOL) { RemoveBdryEnt(j, tp, n); if (j < ii) ii--; j--; continue; }
            short r = line_arc_ints(L, &o->g, P, T, TOL);
            if (r < 0) add_intpt(&tp[ii], o, P, T[0], T[1], ip, nip);
            if (r >= 1) add_intpt(&tp[ii], o, P, T[0], T[1], ip, nip);
            if (r == 2) add_intpt(&tp[ii], o, P + 2, T[2], T[3], ip, nip);
        }
    }
    *pi = ii;
}
/* IntersectBdryArcBdry (180045190) [asm for the argument order] */
static void IntersectBdryArcBdry(TP *tp, int *n, int *pi, IP *ip, int *nip)
{
    double tol0 = TOL, b1[4], b2[4];
    box_empty(b1); box_empty(b2);
    int ii = *pi, stop = 0;
    if (ii < 0 || tp[ii].type != 4) return;
    memset(ip, 0, sizeof(IP) * MAXIP); *nip = 0;
    double a[5];
    arc_3d_to_2d(a, &tp[ii].g);
    box_arc(b1, a);
    for (int j = 0; j < *n; j++) {
        if (stop) break;
        if (j == ii) continue;
        TP *o = &tp[j];
        double P[6] = { 0 }, T[4] = { 0 };
        if (o->type == 4) {
            double a2[5];
            arc_3d_to_2d(a2, &o->g);
            box_arc(b2, a2);
            if (!box_overlap(b1, b2, tol0 * 2.0)) continue;
            int cnt; char conc;
            int r = arc_arc_ints(&tp[ii].g, &o->g, P, T, &cnt, &conc, TOL);
            if (r == 1 || r == 7) { RemoveBdryEnt(j, tp, n); if (j < ii) ii--; j--; }
            else if (r == 2) { RemoveBdryEnt(ii, tp, n); *nip = 0; g_brkflag = 0; stop = 1; }
            else if (r == 5) { if (!conc) add_intpt(&tp[ii], o, P, T[0], T[1], ip, nip); }
            else if (r == 6) {
                if (conc) trim_arc(P, ii, j, tp);
                else { add_intpt(&tp[ii], o, P, T[0], T[1], ip, nip); add_intpt(&tp[ii], o, P + 2, T[2], T[3], ip, nip); }
            }
        } else {
            double Lo[4];
            line2(o, Lo);
            box_line(b2, Lo);
            if (!box_overlap(b1, b2, tol0 * 2.0)) continue;
            double as[2] = { tp[ii].g.s[0], tp[ii].g.s[1] }, ae[2] = { tp[ii].g.e[0], tp[ii].g.e[1] }, dd;
            if ((p2Equal(Lo, as, TOL) && p2Equal(Lo + 2, ae, TOL)) || (p2Equal(Lo, ae, TOL) && p2Equal(Lo + 2, as, TOL))) {
                double m[2];
                t_to_pt_arc2(0.5, a, m);
                dist_pt_l(m, Lo, &dd);
            } else dd = 1.0;
            if (dd < TOL) { RemoveBdryEnt(j, tp, n); if (j < ii) ii--; j--; continue; }
            short r = line_arc_ints(Lo, &tp[ii].g, P, T, TOL);
            if (r < 0) add_intpt(&tp[ii], o, P + 2, T[1], T[0], ip, nip);
            if (r >= 1) add_intpt(&tp[ii], o, P, T[1], T[0], ip, nip);
            if (r == 2) add_intpt(&tp[ii], o, P + 2, T[3], T[2], ip, nip);
        }
    }
    *pi = ii;
}
/* BrkBdryEntsInts (1800410e0) [asm]: every entity split at its intersections with the others (the list shrinks when the
   intersection pass removes an entity); out has 10000 entries */
static TP *BrkBdryEntsInts(TP *tp, int *n)
{
    TP *out = tp_alloc(10000);
    if (!out) return NULL;
    int nin = *n, i = 0, nip = 0;
    static IP ip[MAXIP + 1];
    *n = 0;
    while (i < nin) {
        g_brkflag = 1;
        if (tp[i].type == 2) IntersectBdryLineBdry(tp, &nin, &i, ip, &nip);
        else IntersectBdryArcBdry(tp, &nin, &i, ip, &nip);
        if (g_brkflag) { split_ent(&tp[i], ip, nip, out, n); i++; }
        if (g_limit) break;
    }
    return out;
}
/* FUN_180155bf0: the entities (not i, not removed) touching i's start (sl, *sc) and end (el, *ec); counts -1 when none, and
   both -1 when i itself is removed */
static void touching(const TP *c, int n, int i, int *sl, int *sc, int *el, int *ec)
{
    *sc = -1; *ec = -1;
    if (n < 3 || F8(&c[i]) != 0) return;
    for (int j = 0; j < n; j++) {
        if (j == i || F8(&c[j]) != 0) continue;
        if (p3Equal(c[j].g.s, c[i].g.s, TOL) || p3Equal(c[j].g.e, c[i].g.s, TOL)) sl[++*sc] = j;
        if (p3Equal(c[j].g.s, c[i].g.e, TOL) || p3Equal(c[j].g.e, c[i].g.e, TOL)) el[++*ec] = j;
    }
    if (*sc > -1) (*sc)++;
    if (*ec > -1) (*ec)++;
}
/* FUN_1801563a0: removes dangling chains (entities with a free end, and the chain leading to them) */
static void remove_dangling(TP *tp, int *n)
{
    if (*n <= 2) return;
    TP *c = tp_alloc(*n);
    memcpy(c, tp, sizeof(TP) * (size_t)*n);
    memset(tp, 0, sizeof(TP) * (size_t)*n);
    int nn = *n, sc, ec;
    int *sl = malloc(sizeof(int) * (size_t)nn), *el = malloc(sizeof(int) * (size_t)nn);
    for (int i = 0; i < nn; i++) {
        if (F8(&c[i]) != 0) continue;
        touching(c, nn, i, sl, &sc, el, &ec);
        if (!(sc < 0 || ec < 0)) continue;
        F8(&c[i]) = 1;
        if (sc == -1 && ec == -1) continue;
        for (;;) {
            int j = (-1 < sc) ? sl[0] : el[0];
            touching(c, nn, j, sl, &sc, el, &ec);
            if (sc == -1) { if (ec == -1) { F8(&c[j]) = 1; break; } }
            else if (-1 < sc && ec >= 0) break;
            F8(&c[j]) = 1;
        }
    }
    int k = 0;
    for (int i = 0; i < nn; i++) if (F8(&c[i]) == 0) tp[k++] = c[i];
    *n = k;
    free(sl); free(el); free(c);
}
/* BreakBdryArcQuadrants (1801547f0): an arc cut at the quadrant points (break_arc_3d view 1); pieces whose length is below
   min_arc_length become lines, pieces shorter than tol are dropped; start angles near 0 / pi/2 / pi / 3pi/2 / 2pi and sweeps
   near pi/2 snap */
static short BreakBdryArcQuadrants(const TP *e, TP *out)
{
    A3 a = e->g;
    a.view = 1;
    double pts[3 * 40];
    int n;
    break_arc_3d(&a, 1, pts, &n);
    if (n < 3) { out[0] = *e; return 1; }
    double c[2] = { e->g.c[0], e->g.c[1] };
    double sw = e->g.sw;
    for (int k = 0; k < n; k++) {
        if (fabs(pts[3 * k] - c[0]) < fabs(TOL)) pts[3 * k] = c[0];
        if (fabs(pts[3 * k + 1] - c[1]) < fabs(TOL)) pts[3 * k + 1] = c[1];
    }
    short cnt = 0;
    for (int i = 1; i < n; i++) {
        double *p0 = pts + 3 * (i - 1), *p1 = pts + 3 * i;
        if (fabs(p0[0] - c[0]) < fabs(TOL)) p0[0] = c[0];
        double p[2] = { p0[0], p0[1] }, q[2] = { p1[0], p1[1] }, a2[5];
        char ok;
        if (p2Equal(p, q, TOL)) continue;
        PtsTo2DArc(p, q, c, sw <= 0.0 && sw != 0.0, TOL, a2, &ok);
        if (!ok || !(TOL <= fabs(a2[4] * a2[2]))) continue;
        TP *o = &out[cnt];
        *o = *e;
        if (MINARC <= fabs(a2[4] * a2[2])) {
            arc_2d_to_3d(&o->g, 1, 0.0, a2);
            restrict_angle(0.0, 6.283185307179586, &o->g.sa);
            double s = o->g.sa;
            memcpy(o->g.s, p0, 24);
            memcpy(o->g.e, p1, 24);
            double T = TOL;
            if (fabs(T) < fabs(s - 0.0) || fabs(T) == fabs(s - 0.0)) {
                if (fabs(T) < fabs(s - 1.570796326794897) || fabs(T) == fabs(s - 1.570796326794897)) {
                    if (fabs(T) < fabs(s - 3.141592653589793) || fabs(T) == fabs(s - 3.141592653589793)) {
                        if (fabs(T) < fabs(s - 4.71238898038469) || fabs(T) == fabs(s - 4.71238898038469)) {
                            if (!(fabs(T) < fabs(s - 6.283185307179586) || fabs(T) == fabs(s - 6.283185307179586))) o->g.sa = 6.283185307179586;
                        } else o->g.sa = 4.71238898038469;
                    } else o->g.sa = 3.141592653589793;
                } else o->g.sa = 1.570796326794897;
            } else o->g.sa = 0.0;
            T = TOL;
            if (fabs(T) < fabs(o->g.sw - 1.570796326794897) || fabs(T) == fabs(o->g.sw - 1.570796326794897)) {
                if (fabs(o->g.sa - -1.570796326794897) <= fabs(T) && fabs(T) != fabs(o->g.sa - -1.570796326794897)) o->g.sa = -1.570796326794897;
            } else o->g.sw = 1.570796326794897;
        } else {
            o->type = 2;
            memcpy(o->g.s, p0, 24);
            memcpy(o->g.e, p1, 24);
        }
        cnt++;
    }
    return cnt;
}
/* QuadrantBdryArcs (1801586f0): lines copied, arcs through BreakBdryArcQuadrants; the result has n x 4 slots */
static TP *QuadrantBdryArcs(const TP *in, int *n)
{
    if (*n <= 0) return NULL;
    TP *out = tp_alloc((long)*n * 4);
    if (!out) { *n = 0; return NULL; }
    int k = 0;
    TP tmp[40];
    for (int i = 0; i < *n; i++) {
        if (in[i].type == 2) out[k++] = in[i];
        else {
            short m = BreakBdryArcQuadrants(&in[i], tmp);
            for (int q = 0; q < m; q++) out[k++] = tmp[q];
        }
    }
    *n = k;
    return out;
}
/* FUN_180158b70: z of the ends (and of an arc's centre) set to 0, arc view 1 */
static void zero_z(TP *e)
{
    e->g.s[2] = 0.0; e->g.e[2] = 0.0;
    if (e->type != 2) { e->g.c[2] = 0.0; e->g.view = 1; }
}
/* FUN_1801588f0: restrict to [0, 2 pi], values within t of 0 or 2 pi become 0 */
static void angle_snap(double *a, double t)
{
    restrict_angle(0.0, 6.283185307179586, a);
    double d = fabs(*a - 0.0);
    if ((d <= fabs(t) && fabs(t) != d) || (d = fabs(*a - 6.283185307179586), d <= fabs(t) && fabs(t) != d)) *a = 0.0;
}
static double sgn3(double v) { return v == 0.0 ? 0.0 : (0.0 <= v ? 1.0 : -1.0); }
/* FUN_180153ea0: two candidates leaving in the same direction: the deviation of the arc from the line (samples at
   t = 0.7.. / 0.2.. step 0.05) or the radii decide whether the turn d becomes 2 pi - d */
static void same_dir_turn(const TP *cur, const TP *cand, const double *dir, const double *cdir, double *d)
{
    double by = 0.0, bx = 0.0, s = 0.0, best = 0.0, t, a[5], p[2], f[2], u;
    char ok = 0;
    int code = (cur->type == 2 ? 0 : 10) + (cand->type != 2);
    if (code == 1 || code == 10) {
        const TP *arc = (code == 1) ? cand : cur, *ln = (code == 1) ? cur : cand;
        arc_3d_to_2d(a, &arc->g);
        do {
            if (fabs(arc->g.sw) < 1.570796326794897 || fabs(arc->g.sw) == 1.570796326794897) t = s + 0.7;
            else t = s + 0.2;
            t_to_pt_arc2(t, a, p);
            double l[4] = { ln->g.s[0], ln->g.s[1], ln->g.e[0], ln->g.e[1] };
            proj_pt_ln(p, l, f, &u, &ok);
            if (ok) {
                double vx = p[0] - f[0], vy = p[1] - f[1];
                double dd = sqrt(vx * vx + vy * vy);
                if (best < dd) { best = dd; bx = vx; by = vy; }
            }
            s = s + 0.05;
        } while (t < 0.96);
        if (0.0 < best && best < TOL) {
            double k = (TOL * 1.1) / best;
            by = by * k;
            bx = k * bx;
            best = sqrt(bx * bx + by * by);
        }
        if (best < TOL || best == TOL) return;
        const double *w = (code == 1) ? dir : cdir;
        double c = by * w[0] - bx * w[1];
        if (code == 1) {
            if (0.0 < c && PI_ < *d) { *d = 6.283185307179586 - *d; return; }
            if (0.0 < c || c == 0.0) return;
        } else {
            if (c < 0.0 && PI_ < *d) { *d = 6.283185307179586 - *d; return; }
            if (c < 0.0 || c == 0.0) return;
        }
        if (PI_ < *d || *d == PI_) return;
        *d = 6.283185307179586 - *d;
        return;
    }
    if (code != 11) return;
    double s1 = cur->g.sw, s2 = cand->g.sw, r1 = cur->g.r, r2 = cand->g.r;
    if (0.0 < s1 && s2 <= 0.0 && s2 != 0.0) {
        if (r1 < r2 && PI_ < *d) { *d = 6.283185307179586 - *d; return; }
        if (r1 < r2 || r1 == r2) return;
    } else if (s1 < 0.0 && 0.0 < s2) {
        if (r2 < r1 && PI_ < *d) { *d = 6.283185307179586 - *d; return; }
        if (r2 < r1 || r2 == r1) return;
    } else if (!(s1 <= 0.0 || s2 <= 0.0)) {
        if (*d < PI_ || *d == PI_) return;
        *d = 6.283185307179586 - *d;
        return;
    }
    if (PI_ < *d || *d == PI_) return;
    *d = 6.283185307179586 - *d;
}
typedef struct { int i; uint8_t rev; uint8_t pad[3]; } CAND;   /* 8 bytes: entity index, reversed */
/* FUN_180157090: the unused entities with an end at p (reversed when it is their end) */
static int candidates(TP *b, int n, const double *p, CAND *c)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (F7(&b[i]) != 0) continue;
        if (p3Equal(b[i].g.s, p, TOL)) { c[m].rev = 0; c[m].i = i; m++; }
        else if (p3Equal(b[i].g.e, p, TOL)) { c[m].rev = 1; c[m].i = i; m++; }
    }
    return m;
}
/* FUN_180157170: the next entity of the walk: the candidate turning most to the left of the current entity's end direction
   (largest turn angle in [0, 2 pi), ties within tolA settled by the arc rules, two lines: tolA / 10 and again, at most 10
   rounds); candidates whose end hits a used entity (other than at the first start fs) are skipped. Marks it used. */
static int next_ent(const TP *cur, TP *b, int n, const CAND *idx, int m, const double *fs, TP *tmp)
{
    double tolA = 1e-06;
    if (m == 0) return 0;
    if (m == 1) {
        if (!idx[0].rev) *tmp = b[idx[0].i];
        else ReverseBdryEnt(tmp, &b[idx[0].i]);
        F7(&b[idx[0].i]) = 1;
        return idx[0].i;
    }
    memset(tmp, 0, sizeof *tmp);
    double org[2] = { 0.0, 0.0 }, dir[2];
    if (cur->type == 2) { dir[0] = cur->g.s[0] - cur->g.e[0]; dir[1] = cur->g.s[1] - cur->g.e[1]; }
    else {
        double v[2] = { cur->g.e[0] - cur->g.c[0], cur->g.e[1] - cur->g.c[1] };
        RotatePt(v, org, sgn3(cur->g.sw) * -1.570796326794897, dir);
    }
    int iter = 0, bestj = 0;
    for (;;) {
        double best = -1e+30, a0;
        iter++;
        int retry = 0;
        if (1e-12 <= dir[1] * dir[1] + dir[0] * dir[0]) { a0 = ATAN2(dir[1], dir[0]); if (a0 < 0.0) a0 = a0 + 6.283185307179586; }
        else a0 = 0.0;
        angle_snap(&a0, tolA);
        bestj = 0;
        for (int j = 0; j < m; j++) {
            TP c;
            if (!idx[j].rev) c = b[idx[j].i]; else ReverseBdryEnt(&c, &b[idx[j].i]);
            double ce[2] = { c.g.e[0], c.g.e[1] };
            int hit = 0;
            for (int k = 0; k < n && !hit; k++) {
                if (F7(&b[k]) == 0) continue;
                double q[2];
                GetEntPt2d(&b[k], 0.0, q);
                if (!p2Equal(ce, q, TOL) || p2Equal(fs, q, TOL)) {
                    GetEntPt2d(&b[k], 1.0, q);
                    if (p2Equal(ce, q, TOL) && !p2Equal(fs, q, TOL)) hit = 1;
                } else hit = 1;
            }
            if (hit) continue;
            double cdir[2];
            int arcdir = 0;
            if (cur->type == 4) {
                if (c.type != 4) arcdir = (c.type != 2);
                else {
                    double dsg = sgn3(cur->g.sw) - sgn3(c.g.sw), T = fabs(TOL);
                    if ((T < fabs(dsg) || T == fabs(dsg)) && (fabs(cur->g.r - c.g.r) <= T && T != fabs(cur->g.r - c.g.r))) {
                        if (p2Equal(cur->g.c, c.g.c, TOL)) continue;   /* back along the same circle */
                        arcdir = 1;   /* LAB_1801576b3 -> c is an arc */
                    } else arcdir = 1;
                }
            } else arcdir = (c.type != 2);
            if (arcdir) {
                double v[2] = { c.g.s[0] - c.g.c[0], c.g.s[1] - c.g.c[1] };
                RotatePt(v, org, sgn3(c.g.sw) * 1.570796326794897, cdir);
            } else { cdir[0] = c.g.e[0] - c.g.s[0]; cdir[1] = c.g.e[1] - c.g.s[1]; }
            double a1;
            if (1e-12 <= cdir[1] * cdir[1] + cdir[0] * cdir[0]) { a1 = ATAN2(cdir[1], cdir[0]); if (a1 < 0.0) a1 = a1 + 6.283185307179586; }
            else a1 = 0.0;
            angle_snap(&a1, tolA);
            double d = a1 - a0;
            angle_snap(&d, tolA);
            if ((fabs(d - 0.0) <= 1e-05 && fabs(d - 0.0) != 1e-05) || (fabs(d - 6.283185307179586) <= 1e-05 && fabs(d - 6.283185307179586) != 1e-05))
                same_dir_turn(cur, &c, dir, cdir, &d);
            if (fabs(tolA) < fabs(d - best) || fabs(tolA) == fabs(d - best)) {
                if (best <= d && d != best) { *tmp = c; best = d; bestj = j; }
                continue;
            }
            int code = (tmp->type == 2 ? 0 : 10) + (c.type != 2), take = 0;
            if (code == 1) take = !(c.g.sw < 0.0 || c.g.sw == 0.0);
            else if (code == 10) take = !(0.0 < tmp->g.sw || tmp->g.sw == 0.0);
            else if (code != 11) { tolA = tolA / 10.0; retry = 1; continue; }
            else if (0.0 < tmp->g.sw || tmp->g.sw == 0.0) take = (0.0 < c.g.sw) ? !(tmp->g.r < c.g.r || tmp->g.r == c.g.r) : 0;
            else take = (c.g.sw < 0.0) ? !(c.g.r < tmp->g.r || c.g.r == tmp->g.r) : 1;
            if (take) { *tmp = c; bestj = j; }
        }
        if (!retry || 9 < iter) break;
    }
    F7(&b[idx[bestj].i]) = 1;
    return idx[bestj].i;
}
/* FUN_180156620 with side 1 (case 0 of its switch) [asm 180156620..180156d13]: the entity whose higher end (larger y; equal y
   within tol: larger x) is the topmost / rightmost; ties go through next_ent from a virtual line (0,0) -> (0,-1);
   *rev = the chosen entity does not start there */
static int start_ent(TP *b, int n, char *rev)
{
    if (n < 3) return -1;
    TP ref;
    memset(&ref, 0, sizeof ref);
    ref.type = 2;
    ref.g.e[0] = 0.0; ref.g.e[1] = -1.0; ref.g.e[2] = 0.0;
    double bestX = -1e+30, bestY = -1e+30;
    CAND *list = malloc(sizeof(CAND) * (size_t)(n + 1));
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        const double *s = b[i].g.s, *e = b[i].g.e;
        int useS;
        if (fabs(TOL) > fabs(s[1] - e[1])) useS = s[0] > e[0];
        else useS = s[1] > e[1];
        const double *P = useS ? s : e;
        int add = 0, reset = 0;
        if (fabs(TOL) > fabs(P[1] - bestY)) {
            if (fabs(TOL) > fabs(P[0] - bestX)) add = 1;
            else if (P[0] > bestX) add = reset = 1;
        } else if (P[1] > bestY) add = reset = 1;
        if (add) {   /* FUN_180158a30 */
            if (reset) cnt = 0;
            list[cnt].i = i; list[cnt].rev = !useS; cnt++;
            bestX = useS ? s[0] : e[0];
            bestY = useS ? s[1] : e[1];
        }
    }
    double P[3] = { bestX, bestY, 0.0 }, fs[2] = { 1e+30, 1e+30 };
    TP tmp;
    int k = next_ent(&ref, b, 0, list, cnt, fs, &tmp);
    *rev = !p3Equal(b[k].g.s, P, TOL);
    free(list);
    return k;
}
/* FUN_180153d00: appends t to the walk, merged into the last entry when both are lines and t's start is on the line from the
   last start to t's end, or both are arcs of the same circle */
static void walk_append(TP *t, TP *out, int *k)
{
    zero_z(t);
    TP *last = &out[*k];
    if (last->type == 2 && t->type == 2) {
        double l[4] = { last->g.s[0], last->g.s[1], t->g.e[0], t->g.e[1] }, p[2] = { t->g.s[0], t->g.s[1] }, d;
        dist_pt_l(p, l, &d);
        if (d < TOL) { memcpy(last->g.e, t->g.e, 24); return; }
    } else if (last->type == 4 && t->type == 4) {
        double T = TOL, dr = fabs(last->g.r - t->g.r);
        if (dr <= fabs(T) && fabs(T) != dr && p3Equal(last->g.c, t->g.c, T)) {
            memcpy(last->g.e, t->g.e, 24);
            last->g.sw = t->g.sw + last->g.sw;
            return;
        }
    }
    (*k)++;
    out[*k] = *t;
}
/* AdjustBdryEndPts (18003edf0): consecutive ends within t snapped together (a line end takes the next start, a line start
   the previous end; between arcs the end that is on its own circle within tol) */
static void AdjustBdryEndPts(TP *o, int n, double t)
{
    if (!o || n <= 1) return;
    for (int i = 1; i <= n; i++) {
        TP *cur = &o[i - 1], *nx = (i == n) ? &o[0] : &o[i];
        if (cur->type == 2) { if (p3Equal(cur->g.e, nx->g.s, t)) memcpy(cur->g.e, nx->g.s, 24); }
        else if (nx->type == 2) { if (p3Equal(cur->g.e, nx->g.s, t)) memcpy(nx->g.s, cur->g.e, 24); }
        else if (p3Equal(cur->g.e, nx->g.s, TOL)) {
            double T = TOL;
            double d = sqrt((cur->g.c[1] - nx->g.s[1]) * (cur->g.c[1] - nx->g.s[1]) + (cur->g.c[0] - nx->g.s[0]) * (cur->g.c[0] - nx->g.s[0]) +
                            (cur->g.c[2] - nx->g.s[2]) * (cur->g.c[2] - nx->g.s[2]));
            if (fabs(d - cur->g.r) <= T && T != fabs(d - cur->g.r)) { memcpy(cur->g.e, nx->g.s, 24); continue; }
            d = sqrt((nx->g.c[1] - cur->g.e[1]) * (nx->g.c[1] - cur->g.e[1]) + (nx->g.c[0] - cur->g.e[0]) * (nx->g.c[0] - cur->g.e[0]) +
                     (nx->g.c[2] - cur->g.e[2]) * (nx->g.c[2] - cur->g.e[2]));
            if (fabs(d - nx->g.r) <= T && T != fabs(d - nx->g.r)) memcpy(nx->g.s, cur->g.e, 24);
        }
    }
}
/* FindOuterBoundary (180157a00) = FUN_180156d50(1, 1, tp, n) [asm 180156d50..180157085]: z to 0, QuadrantBdryArcs,
   BrkBdryEntsInts, dangling chains removed, the start (start_ent), then the walk (candidates / next_ent / walk_append) until
   the walk is back at the first start; AdjustBdryEndPts; *tp replaced. 0 when a walk step has no candidate. */
static int FindOuterBoundary(TP **tp, int *n)
{
    int cnt = *n;
    if (cnt <= 0) return 0;
    for (int i = 0; i < cnt; i++) zero_z(&(*tp)[i]);   /* FUN_180158b90 */
    TP *q = QuadrantBdryArcs(*tp, &cnt);
    if (!q) return 0;
    trace_tp("quadrant", q, cnt);
    TP *b = BrkBdryEntsInts(q, &cnt);
    free(q);
    if (!b) return 0;
    if (g_limit) { free(b); return 0; }
    trace_tp("broken", b, cnt);
    remove_dangling(b, &cnt);
    trace_tp("dangling removed", b, cnt);
    char rev = 0;
    int start = start_ent(b, cnt, &rev);
    if (getenv("IFK_AF_TRACE")) fprintf(stderr, "start %d rev %d\n", start, rev);
    if (start == -1) { free(b); return 0; }
    TP *out = tp_alloc(cnt);
    CAND *idx = malloc(sizeof(CAND) * (size_t)(cnt + 1));
    if (rev) ReverseBdryEnt(&out[0], &b[start]);
    else out[0] = b[start];
    int k = 0, ok = 1;
    F7(&b[start]) = 1;
    double fs[3] = { out[0].g.s[0], out[0].g.s[1], out[0].g.s[2] };
    for (;;) {
        double e[3] = { out[k].g.e[0], out[k].g.e[1], out[k].g.e[2] };
        if (p3Equal(e, fs, TOL)) break;
        int m = candidates(b, cnt, e, idx);
        if (m <= 0) { ok = 0; break; }
        TP t;
        next_ent(&out[k], b, cnt, idx, m, fs, &t);
        walk_append(&t, out, &k);
    }
    if (ok) {
        k++;
        AdjustBdryEndPts(out, k, TOL);
        free(*tp);
        *tp = tp_alloc(k);
        memcpy(*tp, out, sizeof(TP) * (size_t)k);
        *n = k;
    }
    free(idx); free(out); free(b);
    return ok;
}

/* ===================================================================== 3. C3d classes (BEPoint / BELine / BEArc.cpp) */
typedef struct { double s[3], e[3]; } LN;   /* C3dLine +0x10 (l_3d) */

/* C3dPoint::operator== (18002c4c0): |b - a| < g_distTol */
static int pt_eq(const double *a, const double *b)
{
    double dy = b[1] - a[1], dx = b[0] - a[0], dz = b[2] - a[2];
    return sqrt(dy * dy + dx * dx + dz * dz) < DTOL;
}
/* C3dPoint::DistanceTo (18002cc70): this a, argument b */
static double pt_dist(const double *a, const double *b)
{
    double dy = b[1] - a[1], dx = b[0] - a[0], dz = b[2] - a[2];
    return sqrt(dy * dy + dx * dx + dz * dz);
}
static double v_len(const double *v) { return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }   /* C3dPoint::Length 18002ce60 */
/* FUN_1800271d0: an angle into [0 or -2 pi, 2 pi], snapped to 0 / pi / pi/2 / 3pi/2 (and their negatives) within g_angTol;
   keep2pi = 0 also snaps +-2 pi to 0 */
static void ang_norm(double *a, char neg, char keep2pi)
{
    double lo = neg ? -6.283185307179586 : 0.0, d = *a;
    if (6.283185307179586 < d) { do d = d + -6.283185307179586; while (6.283185307179586 < d); *a = d; }
    d = *a;
    if (d < lo) { do d = d + 6.283185307179586; while (d < lo); *a = d; }
    if (fabs(*a - 0.0) < ANGTOL) *a = 0.0;
    if (!keep2pi && (fabs(*a - 6.283185307179586) < ANGTOL || fabs(*a - -6.283185307179586) < ANGTOL)) *a = 0.0;
    if (fabs(*a - 3.141592653589793) < ANGTOL) *a = 3.141592653589793;
    if (fabs(*a - -3.141592653589793) < ANGTOL) *a = -3.141592653589793;
    if (fabs(*a - 1.5707963267948966) < ANGTOL) *a = 1.5707963267948966;
    if (fabs(*a - -1.5707963267948966) < ANGTOL) *a = -1.5707963267948966;
    if (fabs(*a - 4.71238898038469) < ANGTOL) *a = 4.71238898038469;
    if (fabs(*a - -4.71238898038469) < ANGTOL) *a = -4.71238898038469;
}
/* C3dPoint::RotateXY (18002d290): p about c by a (x, y; z unchanged) */
static void pt_rotxy(double *p, double a, const double *c)
{
    ang_norm(&a, 0, 0);
    double dx = p[0] - c[0], dy = p[1] - c[1];
    double co = COS(a), si = SIN(a);
    p[0] = (dx * co + c[0]) - dy * si;
    p[1] = dx * si + c[1] + dy * co;
}
/* C3dLine::PointFromT (18002b6a0) [asm]: (end - start) t + start */
static void ln_point(const LN *l, double t, double *p)
{
    p[0] = (l->e[0] - l->s[0]) * t + l->s[0];
    p[1] = (l->e[1] - l->s[1]) * t + l->s[1];
    p[2] = (l->e[2] - l->s[2]) * t + l->s[2];
}
static double ln_len(const LN *l) { double v[3] = { l->e[0] - l->s[0], l->e[1] - l->s[1], l->e[2] - l->s[2] }; return v_len(v); }   /* C3dLine::Length 18002ac70 */
/* C3dLine::ProjectPoint (18002b7f0): proj_pt_ln_3d; the point stays (0, 0, 0) when the line is shorter than tol */
static void ln_project(const LN *l, const double *p, double *o, double *t)
{
    double L[6] = { l->s[0], l->s[1], l->s[2], l->e[0], l->e[1], l->e[2] };
    char ok;
    o[0] = o[1] = o[2] = 0.0;
    proj_pt_ln_3d(p, L, o, t, &ok);
}
/* C3dLine::PassesThru (18002b560) */
static int ln_passes(const LN *l, const double *p, double tl, int bounded)
{
    double o[3], t = 0.0;
    ln_project(l, p, o, &t);
    if (!(pt_dist(p, o) <= tl)) return 0;
    return !bounded || (0.0 <= t && t <= 1.0);
}
/* C3dLine::PerpendicularBisector(false) (18002b600) [asm: called with false = +pi/2]: the line rotated by pi/2 about its midpoint */
static LN ln_bisector(const LN *l)
{
    LN b = *l;
    double m[3];
    ln_point(l, 0.5, m);
    pt_rotxy(b.s, 1.5707963267948966, m);   /* C3dLine::RotateXY 18002b8b0 */
    pt_rotxy(b.e, 1.5707963267948966, m);
    return b;
}
/* C3dLine::operator== (18002a030) */
static int ln_eq(const LN *a, const LN *b)
{
    if (pt_eq(a->s, b->s) && pt_eq(a->e, b->e)) return 1;
    return pt_eq(a->s, b->e) && pt_eq(a->e, b->s);
}
/* C3dLine::DistanceTo(p, false) (18002a7d0) */
static double ln_dist(const LN *l, const double *p)
{
    double o[3], t = 0.0;
    ln_project(l, p, o, &t);
    return pt_dist(p, o);
}
/* C3dLine::LineIntersect (18002acc0) [asm], only as far as FitArcsToIFLines uses it: 7 (one crossing point *ip) or another
   code (the colinear cases of SaveColinearIntPts 18002b980 never give 7 and are not ported) */
static int ln_intersect(const LN *a, const LN *b, double *ip)
{
    if (fabs(ln_len(a) - 0.0) < DTOL || fabs(ln_len(b) - 0.0) < DTOL) return 0;   /* IsZeroLength 18002abe0 */
    if (ln_eq(a, b)) return 1;
    double la = ln_len(a), lb = ln_len(b), d1, d2;
    if (lb > la) { d1 = ln_dist(b, a->s); d2 = ln_dist(b, a->e); }
    else { d1 = ln_dist(a, b->s); d2 = ln_dist(a, b->e); }
    if (SYSTOL > fabs(d1 - d2)) {
        if (DTOL > fabs(d1 - 0.0)) return 8;   /* SaveColinearIntPts: codes 1 / 3..6 / 8, never 7 */
        double v1[3] = { a->e[0] - a->s[0], a->e[1] - a->s[1], a->e[2] - a->s[2] };
        double v2[3] = { b->e[0] - b->s[0], b->e[1] - b->s[1], b->e[2] - b->s[2] };
        if (fabs(v2[1] * v1[1] + v1[0] * v2[0] + v2[2] * v1[2]) > 2e-07) return 2;
    }
    double L1[6] = { a->s[0], a->s[1], a->s[2], a->e[0], a->e[1], a->e[2] }, L2[6] = { b->s[0], b->s[1], b->s[2], b->e[0], b->e[1], b->e[2] };
    double p1[3] = { 0, 0, 0 }, p2[3] = { 0, 0, 0 };
    char ok;
    ll_ints_3d(L1, L2, p1, p2, &ok);
    if (ok && pt_eq(p1, p2)) { ip[0] = p1[0]; ip[1] = p1[1]; ip[2] = p1[2]; return 7; }
    return 8;
}
/* C3dArc::Init (180027730) [asm]: r (kept when > g_distTol, else 0), centre, start angle (ang_norm 0, 0), sweep (ang_norm 1, 1:
   SetSweepAngle 1800281f0 [asm]), view, end points from systypes arc_endpoints at the centre's z (CalcEndpoints 180026930) */
static void arc_init(A3 *a, double r, const double *c, double sa, double sw, short view)
{
    memset(a, 0, sizeof *a);
    if (DTOL <= fabs(r) && fabs(r) != DTOL) a->r = fabs(r);
    a->c[0] = c[0]; a->c[1] = c[1]; a->c[2] = c[2];
    a->sa = sa; ang_norm(&a->sa, 0, 0);
    a->sw = sw; ang_norm(&a->sw, 1, 1);
    a->view = view;
    double a2[5] = { a->c[0], a->c[1], a->r, a->sa, a->sw }, p[2], q[2];   /* A_2d 1800267e0 */
    arc_endpoints(a2, p, q);
    V2ToV3(p, a->c[2], a->s);
    V2ToV3(q, a->c[2], a->e);
}
/* C3dArc::FromXYPoints (180027650) [asm]: constr_arc_new(s, e, c, cw, g_distTol) and Init(r, c, start, sweep, view) */
static int arc_from_xy(A3 *a, const double *s, const double *e, const double *c, char cw, short view)
{
    double a2[5];
    char ok;
    constr_arc_new(s, e, c, cw, DTOL, a2, &ok);
    if (ok) arc_init(a, a2[2], c, a2[3], a2[4], view);
    return ok;
}
/* C3dArc::From2PointsAndRadius (180027390) [asm] */
static int arc_from_2pr(A3 *a, double r, const double *p1, const double *p2, char cw, short view)
{
    if (DTOL > r) return 0;
    LN l = { { p1[0], p1[1], p1[2] }, { p2[0], p2[1], p2[2] } };
    double len = ln_len(&l);
    if (DTOL > len) return 0;
    double q = len / (r * 2.0);
    if (q > 1.0) return 0;
    double ang = ACOS(q);
    if (cw) ang = ang * -1.0;
    double c[3];
    if (SYSTOL > fabs(q - 0.0)) ln_point(&l, 0.5, c);
    else { ln_point(&l, r / len, c); pt_rotxy(c, ang, p1); }
    return arc_from_xy(a, p1, p2, c, cw, view);
}
/* C3dArc::FitToPoints = FitToClosestCenter (180026d10, 1800271c0 jumps there) [asm]: the intersection of the perpendicular
   bisectors of two chords (all pairs) with the smallest spread of the distances to all points; the arc from the first to the
   last point about it (FromXYPoints, else From2PointsAndRadius with the distance to the first point), clockwise when
   (p0 - c) x (p[n-2] - c) has z < 0 */
static int arc_fit_points(A3 *a, const double *pts, int n)
{
    if (n < 3) return 0;
    double best = 1e+30, bc[3] = { 0, 0, 0 }, ip[3];
    for (int i = 1; i < n; i++) {
        LN l1 = { { pts[3 * (i - 1)], pts[3 * (i - 1) + 1], pts[3 * (i - 1) + 2] }, { pts[3 * i], pts[3 * i + 1], pts[3 * i + 2] } };
        LN b1 = ln_bisector(&l1);
        for (int k = i + 1; k < n; k++) {
            LN l2 = { { pts[3 * (k - 1)], pts[3 * (k - 1) + 1], pts[3 * (k - 1) + 2] }, { pts[3 * k], pts[3 * k + 1], pts[3 * k + 2] } };
            LN b2 = ln_bisector(&l2);
            if (ln_intersect(&b1, &b2, ip) == 7) {
                double mn = 1e+30, mx = -1e+30;
                for (int m = 0; m < n; m++) {
                    double d = pt_dist(ip, pts + 3 * m);
                    if (d < mn) mn = d;
                    if (mx < d) mx = d;
                }
                double sp = mx - mn;
                if (sp < best) { bc[0] = ip[0]; bc[1] = ip[1]; bc[2] = ip[2]; best = sp; }
            } else mc_assert();
        }
    }
    double r = pt_dist(bc, pts);
    double v1[3] = { pts[0] - bc[0], pts[1] - bc[1], pts[2] - bc[2] };
    const double *pn2 = pts + 3 * (n - 2), *pn1 = pts + 3 * (n - 1);
    double v2[3] = { pn2[0] - bc[0], pn2[1] - bc[1], pn2[2] - bc[2] };
    double cz = v2[1] * v1[0] - v2[0] * v1[1];   /* C3dPoint::Cross 18002cac0, z */
    char cw = 0.0 > cz;
    if (arc_from_xy(a, pts, pn1, bc, cw, 1)) return 1;
    return arc_from_2pr(a, r, pts, pn1, cw, 1);
}

/* ======================================================================================== 4. CSpinBdryArcFitter */
typedef struct {
    LN *ln; int nln, cln;      /* +0x160 std::vector<C3dLine>: the IF lines */
    A3 *arc; int narc, carc;   /* +0xe0 std::vector<C3dArc>: arcs found (true arcs and fitted ones) */
    TP *list; int nlist;       /* the CBdryEntList base: the result entities (lines / arcs) */
    double tol;                /* +0x158 */
    LN axis;                   /* +0x118 */
    int flag;                  /* +0x190 */
    int mirrored;              /* +0xaa */
    short prof;                /* +0xd8 */
} FIT;

static int fit_push_arc(FIT *f, const A3 *a)   /* FUN_1800e6b10 (vector push_back) */
{
    if (f->narc == f->carc) { int c = f->carc ? 2 * f->carc : 16; A3 *p = realloc(f->arc, sizeof(A3) * (size_t)c); if (!p) return 0; f->arc = p; f->carc = c; }
    f->arc[f->narc++] = *a;
    return 1;
}
static void fit_erase_lines(FIT *f, int at, int k)   /* vector::erase */
{
    memmove(f->ln + at, f->ln + at + k, sizeof(LN) * (size_t)(f->nln - at - k));
    f->nln -= k;
}
/* Next3LinesAreAttached (1800e48d0) */
static int next3_attached(const FIT *f, int i)
{
    if (f->nln < 4 || (unsigned)(f->nln - 3) < (unsigned)i) return 0;
    return pt_eq(f->ln[i].e, f->ln[i + 1].s) && pt_eq(f->ln[i + 1].e, f->ln[i + 2].s);
}
/* FitArcThroughPoints (1800e33d0): the points start(i), end(i), ..., end(j) through C3dArc::FitToPoints */
static int fit_arc_through(FIT *f, A3 *a, int i, int j)
{
    if (i < 0 || f->nln - 1 < j) { mc_assert(); return 0; }
    int n = j - i + 2;
    double *pts = malloc(sizeof(double) * 3 * (size_t)n);
    if (!pts) return 0;
    memcpy(pts, f->ln[i].s, 24);
    for (int k = i; k <= j; k++) memcpy(pts + 3 * (k - i + 1), f->ln[k].e, 24);
    int ok = arc_fit_points(a, pts, n);
    free(pts);
    return ok;
}
/* FitArcsToIFLines (1800e3540) [asm 1800e3540..1800e3e79] */
static int FitArcsToIFLines(FIT *f)   /* returns 0 only when out of memory */
{
    if (f->nln < 4) return 1;
    double tolA = f->tol;
    if (DTOL > tolA) tolA = DTOL;
    if (tolA > 0.001) tolA = 0.001;
    double *cx = malloc(sizeof(double) * 3 * (size_t)(f->nln + 1)), *rad = malloc(sizeof(double) * (size_t)(f->nln + 1));
    if (!cx || !rad) { free(cx); free(rad); return 0; }
    int i = 0;
    double prevLen = 0.0;
    A3 arc;
    memset(&arc, 0, sizeof arc);
    while (i < f->nln - 2) {
        int att = next3_attached(f, i), nc = 0, k = i + 1;
        LN b1;
        if (att) {
            b1 = ln_bisector(&f->ln[i]);
            prevLen = ln_len(&f->ln[i]);
            for (; k < f->nln; k++) {
                LN b2 = ln_bisector(&f->ln[k]);
                double ip[3], c[3], R;
                if (ln_intersect(&b1, &b2, ip) != 7) break;
                if (k == i + 1) { c[0] = ip[0]; c[1] = ip[1]; c[2] = ip[2]; R = pt_dist(c, f->ln[i].s); }
                else {   /* AvgArcCenter 1800e1540 / AvgArcRadius 1800e16a0 */
                    double sx = 0.0, sy = 0.0, sr = 0.0;
                    for (int q = 0; q < nc; q++) { sx = sx + cx[3 * q]; sy = sy + cx[3 * q + 1]; }
                    c[0] = sx / (double)nc; c[1] = sy / (double)nc; c[2] = 0.0;
                    for (int q = 0; q < nc; q++) sr = sr + rad[q];
                    R = sr / (double)nc;
                }
                double d = pt_dist(f->ln[k].e, c);
                double dR = fabs(R - d);
                double mid[3];
                ln_point(&f->ln[k], 0.5, mid);
                double dm = pt_dist(mid, c);
                double len = ln_len(&f->ln[k]);
                double ratio = len / prevLen;
                if (1.0 > ratio) ratio = 1.0 / ratio;
                if (dR > tolA || ratio > 2.0 || fabs(d - dm) > f->tol * 1.1) break;
                double v1[3] = { f->ln[i].s[0] - c[0], f->ln[i].s[1] - c[1], f->ln[i].s[2] - c[2] };
                double v2[3] = { f->ln[k].e[0] - c[0], f->ln[k].e[1] - c[1], f->ln[k].e[2] - c[2] };
                double cz = v2[1] * v1[0] - v2[0] * v1[1];
                char cw = 0.0 > cz;
                if (!arc_from_xy(&arc, f->ln[i].s, f->ln[k].e, c, cw, 1)) break;
                if (!(fabs(arc.sw) <= 3.141592653589793)) break;
                cx[3 * nc] = ip[0]; cx[3 * nc + 1] = ip[1]; cx[3 * nc + 2] = ip[2];
                rad[nc] = d;
                nc++;
                prevLen = len;
            }
        }
        if (i + 1 < k) {
            if (fit_arc_through(f, &arc, i, k - 1)) {
                arc.view = 1;   /* C3dArc::SetView(1) */
                if (!fit_push_arc(f, &arc)) { free(cx); free(rad); return 0; }
                fit_erase_lines(f, i, k - i);
            } else i = k;
        } else i = i + 1;
    }
    free(cx); free(rad);
    return 1;
}
/* LinesAndArcsTo_tp_ents (1800e4630): the lines (type 2, l_3d), then the arcs (type 4, the 0x68-byte a_3d with +0x70 = 1),
   through QuadrantBdryArcs */
static TP *lines_arcs_to_tp(const FIT *f, int *n)
{
    int cnt = f->narc + f->nln;
    *n = cnt;
    TP *t = tp_alloc(cnt);
    if (!t) { mc_assert(); return NULL; }
    int k = 0;
    for (int i = 0; i < f->nln; i++, k++) { t[k].type = 2; memcpy(t[k].g.s, f->ln[i].s, 24); memcpy(t[k].g.e, f->ln[i].e, 24); }
    for (int i = 0; i < f->narc; i++, k++) { t[k].type = 4; t[k].g = f->arc[i]; t[k].g.view = 1; }
    TP *q = QuadrantBdryArcs(t, n);
    free(t);
    return q;
}
/* C3dArc(a_3d) (180026030) = Init(r, centre, start, sweep, view); CBdryEntList::From_tp_ents (180024820) builds one, assigns it
   to a local (operator= 180026280 = Init again) and stores a copy (AllocateCopy: copy constructor = Init again) */
static void arc_from_a3(A3 *o, const A3 *a)
{
    A3 t1, t2;
    arc_init(&t1, a->r, a->c, a->sa, a->sw, a->view);
    arc_init(&t2, t1.r, t1.c, t1.sa, t1.sw, t1.view);
    arc_init(o, t2.r, t2.c, t2.sa, t2.sw, t2.view);
}
/* FindOuterBdry (1800e31d0): LinesAndArcsTo_tp_ents, FindOuterBoundary; on success the list = From_tp_ents of the result
   (lines copied, arcs rebuilt by Init), else FromLinesAndArcs (not used afterwards: EvalIFBdry fails) */
static int FindOuterBdry(FIT *f)
{
    int n;
    TP *t = lines_arcs_to_tp(f, &n);
    if (!t || n <= 0) { free(t); return 0; }
    int r = FindOuterBoundary(&t, &n);
    if (g_limit) { free(t); return 0; }
    free(f->list); f->list = NULL; f->nlist = 0;
    if (r) {
        f->list = tp_alloc(n);
        if (!f->list) { free(t); return 0; }
        for (int i = 0; i < n; i++) {
            TP *o = &f->list[i];
            o->type = t[i].type;
            if (t[i].type == 2) { memcpy(o->g.s, t[i].g.s, 24); memcpy(o->g.e, t[i].g.e, 24); }
            else if (t[i].type == 4) arc_from_a3(&o->g, &t[i].g);
            else g_notdec = 1;   /* the slot would stay NULL and ToIFBdry would fault */
        }
        f->nlist = n;
    }
    free(t);
    return r;
}
/* ToIFBdry (1800e5980): the list written into the caller's array: a new zeroed array when the list is longer than the input
   count (the old one freed), +0 = type, lines +0x10..+0x3f (L_3d), arcs +0x10..+0x77 (A_3d, view at +0x70); the other bytes
   stay */
static int ToIFBdry(FIT *f, uint8_t **tp, int *alloc_n, short *n, int *realloc_done)
{
    if (!*tp || f->nlist < 1) { mc_assert(); return 0; }
    long size = f->nlist;
    if (*n < size) {
        free(*tp);
        *tp = calloc((size_t)size, IFK_TPENT_SIZE);
        if (!*tp) { mc_assert(); return IFK_AF_NOMEM; }
        *alloc_n = (int)size;
        *realloc_done = 1;
    }
    *n = (short)size;
    for (long k = 0; k < size; k++) {
        uint8_t *d = *tp + IFK_TPENT_SIZE * k;
        int32_t ty = f->list[k].type;
        if (ty == 2) { memcpy(d, &ty, 4); memcpy(d + 0x10, f->list[k].g.s, 24); memcpy(d + 0x28, f->list[k].g.e, 24); }
        else if (ty == 4) { memcpy(d, &ty, 4); memcpy(d + 0x10, &f->list[k].g, 0x68); }
        else mc_assert();
    }
    return 0;
}
/* EvalIFBdry (1800e25a0) */
static int EvalIFBdry(FIT *f, int with_solids, uint8_t **tp, int *alloc_n, short *n, int *realloc_done)
{
    free(f->list); f->list = NULL; f->nlist = 0;   /* CBdryEntList::DeleteAll */
    if (!tp || !*tp || *n < 3) return 0;
    f->nln = 0;   /* FUN_1800e6820: clear */
    if (f->cln < *n) { LN *p = realloc(f->ln, sizeof(LN) * (size_t)*n); if (!p) return 0; f->ln = p; f->cln = *n; }
    for (int i = 0; i < *n; i++) {
        const uint8_t *e = *tp + IFK_TPENT_SIZE * (size_t)i;
        LN l;
        memcpy(l.s, e + 0x10, 24); memcpy(l.e, e + 0x28, 24);
        f->ln[f->nln++] = l;
        if (!f->mirrored && (!(0.0 < l.s[1] || l.s[1] == 0.0) || !(0.0 < l.e[1] || l.e[1] == 0.0))) f->mirrored = 1;
    }
    if (with_solids) {
        /* FindSpinBdrySolidTrueLinesAndArcs (1800e3260): the true-line / arc lists cleared, then get_ent(5, 0x10000) over the
           part's solids - none here (a part with a solid takes the Parasolid edge path, NOT DECODED / not ported) */
        f->narc = 0;
        /* EvalIFLinesAndTrueArcs (1800e2820) without true lines / arcs: RemoveIFLinesOnLine / RemoveIFLinesOnArc have nothing
           to do, LineEndPtStatus / ExtendDeadEndLineToArc change nothing (no arcs); the lines whose ends are equal
           (C3dPoint::operator==) are removed */
        for (int i = 0; i < f->nln;) {
            if (pt_eq(f->ln[i].s, f->ln[i].e)) fit_erase_lines(f, i, 1);
            else i++;
        }
    }
    if (!FitArcsToIFLines(f)) return 0;
    int ok = FindOuterBdry(f);
    if (g_limit) return 0;
    if (ok) {
        int r = ToIFBdry(f, tp, alloc_n, n, realloc_done);
        if (r < 0) return r;
    } else mc_assert();
    return ok;
}
/* UseNamedView (1800e5bb0): the plane's X axis line (origin, origin + X) passes through both axis points within tol */
static int UseNamedView(const FIT *f, const IfkAfPlane *p)
{
    if (!p || p->id <= 0) return 0;
    LN l;
    memset(&l, 0, sizeof l);
    l.e[0] = p->x[0]; l.e[1] = p->x[1]; l.e[2] = p->x[2];
    for (int k = 0; k < 3; k++) { l.s[k] = p->origin[k] + l.s[k]; l.e[k] = p->origin[k] + l.e[k]; }   /* C3dLine::operator+= 18002a3d0 */
    return ln_passes(&l, f->axis.s, TOL, 0) && ln_passes(&l, f->axis.e, TOL, 0);
}
/* DefineNamedView (1800e1fb0) [asm]: the profile plane = the active WCS (PlaneType 3) or C-plane (0) whose X axis carries the
   axis (WCS first unless flag), else the first plane of GetPlanes that does (and then SetActivePlane(0, it)); else the DLL
   builds a plane from the axis and the C-plane view (SearchPlanePtr, MastercamPlanes::Add) - NOT DECODED here */
static int DefineNamedView(FIT *f, const IfkAfEnv *env, IfkAfResult *res)
{
    if (ln_len(&f->axis) < TOL) return 0;
    const IfkAfPlane *first = f->flag ? &env->cplane : &env->wcs, *second = f->flag ? &env->wcs : &env->cplane;
    const IfkAfPlane *prof = NULL;
    if (UseNamedView(f, first)) prof = first;
    else if (UseNamedView(f, second)) prof = second;
    f->prof = prof ? prof->id : 0;
    short s1 = f->prof;
    if (f->prof == 0) {
        for (int i = 0; i < env->nplanes && f->prof == 0; i++)
            if (UseNamedView(f, &env->planes[i])) { f->prof = env->planes[i].id; prof = &env->planes[i]; }
    }
    if (f->prof == 0) {
        /* [asm 1800e215d..1800e2449] the plane the DLL looks up (SearchPlanePtr) and, when it is not in the part, adds
           (MastercamPlanes::Add(plane, false, true)): rows v = unit axis, w = unit(v in the C-plane view with z = 0, rotated
           by pi/2 about the origin, back to world) - or the C-plane's X when that is shorter than tol -, n = unit(v x w);
           origin = the axis start. Which plane SearchPlanePtr finds and the id / view number Add gives are Mastercam.dll's
           (NOT DECODED): the result reports the plane and new_plane = 1 */
        short cview = env->cplane.view;
        double v[3] = { f->axis.e[0] - f->axis.s[0], f->axis.e[1] - f->axis.s[1], f->axis.e[2] - f->axis.s[2] };
        double w[3], n[3];
        #define NORMALIZE(p) do { double l_ = v_len(p); if (fabs(l_ - 0.0) < SYSTOL) mc_assert(); \
            else { double s_ = 1.0 / l_; if (fabs(s_ - 0.0) < SYSTOL) mc_assert(); else { p[0] = s_ * p[0]; p[1] = s_ * p[1]; p[2] = s_ * p[2]; } } } while (0)
        NORMALIZE(v);
        double vv[3] = { v[0], v[1], v[2] };
        w[0] = v[0]; w[1] = v[1]; w[2] = v[2];
        world_to_view(w, cview, w);
        w[2] = 0.0;
        double lw = v_len(w);
        if (fabs(TOL) < fabs(lw - 0.0) || fabs(TOL) == fabs(lw - 0.0)) {
            const double org[3] = { 0.0, 0.0, 0.0 };
            pt_rotxy(w, 1.5707963267948966, org);
            view_to_world(w, cview, w);
        } else { w[0] = env->cplane.x[0]; w[1] = env->cplane.x[1]; w[2] = env->cplane.x[2]; }
        NORMALIZE(w);
        n[0] = w[2] * vv[1] - w[1] * vv[2];   /* C3dPoint::Cross 18002cac0: v x w */
        n[1] = w[0] * vv[2] - w[2] * vv[0];
        n[2] = w[1] * vv[0] - w[0] * vv[1];
        NORMALIZE(n);
        #undef NORMALIZE
        memcpy(res->new_plane_m, vv, 24); memcpy(res->new_plane_m + 3, w, 24); memcpy(res->new_plane_m + 6, n, 24);
        memcpy(res->new_plane_o, f->axis.s, 24);
        res->new_plane = 1;
        res->prof_view = -1;
        res->set_active_cplane = 1;
        return 1;
    }
    res->prof_view = prof->view;
    if (s1 == 0) res->set_active_cplane = 1;   /* MastercamPlanes::SetActivePlane(0, ProfileView) */
    return 1;
}

int ifk_arcfit(uint8_t **tp, int *alloc_n, short n, const double axis[6], double tol, int flag, const IfkAfEnv *env, IfkAfResult *res)
{
    memset(res, 0, sizeof *res);
    res->count = n;
    TOL = env->systol; DTOL = fabs(env->disttol); MINARC = env->min_arc_length;
    g_nassert = 0; g_notdec = 0; g_limit = 0;
    FIT f;
    memset(&f, 0, sizeof f);
    memcpy(f.axis.s, axis, 24); memcpy(f.axis.e, axis + 3, 24);
    f.tol = tol;
    f.flag = flag;
    if (f.tol <= 0.0) f.tol = env->systol;   /* constructor 1800e06f0: tol <= 0 -> mccore tol */
    short cnt = n;
    int ok = DefineNamedView(&f, env, res);
    int rc = IFK_AF_OK;
    if (ok) {
        int r = EvalIFBdry(&f, 1, tp, alloc_n, &cnt, &res->reallocated);
        if (r < 0) rc = r;
        else if (!r && !g_notdec && !g_limit) {
            r = EvalIFBdry(&f, 0, tp, alloc_n, &cnt, &res->reallocated);
            if (r < 0) rc = r;
        }
        ok = r > 0;
    }
    res->ok = ok;
    res->count = cnt;
    res->prof_id = f.prof;
    res->nassert = g_nassert;
    free(f.ln); free(f.arc); free(f.list);
    if (rc == IFK_AF_OK && g_notdec) rc = IFK_AF_NOT_DECODED;
    if (rc == IFK_AF_OK && g_limit) rc = IFK_AF_LIMIT;
    return rc;
}
