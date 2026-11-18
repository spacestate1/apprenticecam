/* hbe_filter.c - port of CMutlFilterHbeAra (mcmsurf.dll, Mastercam 2017), the line / arc filter that
 * CMutlCvrtIffToHbeFile runs on one curve's 0x68-byte HBE records (FUN_18020de20 -> FUN_18020dd40).
 * C99, no dependencies. Every step cites the function (address) it comes from; see notes.md.
 *
 * Scope:
 *  - FUN_18020dd40 / FUN_18020e0b0 (driver), FUN_180212a80 (init), FUN_18020ea40 (done test), FUN_18020f2c0
 *    (line deviation), FUN_18020ea80 = CMutlFilterHbeAra::FitArc3D with FUN_180212eb0 / FUN_180220040 /
 *    FUN_18021c280 (plane detection), FUN_180211a70 = GetTheorCenterPt, FUN_18020b750 (radial spread check),
 *    FUN_18020e260 (grid search of the centre), FUN_18021eec0 (commit), FUN_18020f6e0 (write choice),
 *    FUN_18021f470 (line writer), FUN_18021f080 (arc writer), FUN_18021e8e0 (advance), FUN_18020f630 (flush).
 *  - mccore helpers: world_to_2d (0x180019f70 -> world_to_view 0x180341b30: view < 2 is the identity), l_ints
 *    (0x1804b3480), cr_a_3p (0x1804b5c90 with center_3pa 0x1804b5200), constr_arc_new (0x1804b5830),
 *    constr_ns_arc (0x1804b5c00), proj_pt_ln (0x180002780), restrict_angle (0x1804b4440), rotate_pt
 *    (0x1804b4550), UnitNormal (0x1804b7480). l_ints / cr_a_3p / restrict_angle / find_sweep_angle are copied
 *    from viewer/mcspline_simplify.c of this project (already ported there from the same addresses).
 *  - Only the Top plane (plane index 0, view 1) of FitArc3D is ported: the converter never sets the other two
 *    plane flags (+0x59 / +0x5a of the filter = converter +0x179 / +0x17a, set 0 by FUN_1802095f0 and never
 *    written again in mcmsurf), and the other views would need the view matrices (world_to_view, mcViews).
 *  - NOT PORTED: CSpinBdryArcFitter (mcmachinedef.dll) used by FUN_18020de20 for Turn Profile with dat[0x4f]
 *    and !dat[0x53]; see notes.md.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.283185307179586

/* The 0x68-byte HBE record (layout from FUN_18020dd40 / FUN_18021f470 / FUN_18021f080 / FUN_180232540). */
typedef struct {
    double p0[3];      /* +0x00 start */
    double p1[3];      /* +0x18 end */
    double c[3];       /* +0x30 arc centre (arcs only) */
    double n[3];       /* +0x48 arc unit normal (arcs only) */
    uint8_t type;      /* +0x60 1 = line, 2 = arc */
    uint8_t first;     /* +0x61 1 on the first record written */
    uint8_t pad[6];    /* +0x62 never written by the filter */
} HbeRec;
typedef char hbe_rec_size_check[sizeof(HbeRec) == 0x68 ? 1 : -1];

/* ---------------------------------------------------------------------------------------------------------- mccore */
static void restrict_angle(double lo, double hi, double *a) {            /* restrict_angle 0x1804b4440 */
    double d = *a;
    if (hi < d) { do d = d + -6.283185307179586; while (hi < d); *a = d; }
    d = *a;
    if (d < lo) { do d = d + 6.283185307179586; while (d < lo); *a = d; }
}
static double sweep_angle(double a, double b, double c) {               /* find_sweep_angle 0x1804b6880 */
    restrict_angle(0.0, TWO_PI, &a); restrict_angle(0.0, TWO_PI, &b); restrict_angle(0.0, TWO_PI, &c);
    b = b - a; restrict_angle(0.0, TWO_PI, &b);
    c = c - a; restrict_angle(0.0, TWO_PI, &c);
    if (c <= b) c = c - TWO_PI;
    return c;
}
static double ang_of(double dy, double dx) {                            /* the atan2 block of cr_a_3p */
    if (1e-12 <= dy * dy + dx * dx) { double a = atan2(dy, dx); if (a < 0.0) a = a + TWO_PI; return a; }
    return 0.0;
}
typedef struct { double x, y, r, a0, sw; } A2;                          /* a_2d: centre, r +0x10, start +0x18, sweep +0x20 */

static void t_on_line(double systol, double *t, const double p[2], const double l[4]) {   /* t_on_line 0x1804b48a0 */
    double dx = l[2] - l[0], dy = l[3] - l[1], d = dy * dy + dx * dx;
    if (systol * systol < d) *t = (dy * (p[1] - l[1]) + dx * (p[0] - l[0])) / d;
    else *t = 0.0;
}
static int l_hv_ints(int k, const double *a, const double *b, double out[2], double *ta, double *tb) {   /* 0x180002330 */
    const double *p3, *p4;
    if (k == 2) { p3 = a + 3; p4 = a + 1; } else { p3 = a + 2; p4 = a; }
    double d1 = *p3, d2 = *p4;
    if (1e-12 <= fabs(d1 - d2)) {
        if (k == 1) { p3 = b + 3; p4 = b + 1; } else { p3 = b + 2; p4 = b; }
        double d7 = *p3 - *p4;
        if (1e-12 <= fabs(d7)) {
            if (k == 2) { p3 = b + 3; p4 = b + 1; } else { p3 = b + 2; p4 = b; }
            double d6 = sqrt((*p3 - *p4) * (*p3 - *p4) + d7 * d7);
            if (1e-12 <= d6 && 1.5e-10 <= fabs(d7 / d6)) {
                const double *b0 = k == 1 ? b + 1 : b, *a0 = k == 1 ? a + 1 : a;
                d7 = (*a0 - *b0) / d7;
                *tb = d7;
                out[0] = (b[2] - b[0]) * d7 + b[0];
                double *ov = k == 2 ? out + 1 : out;
                out[1] = (b[3] - b[1]) * *tb + b[1];
                const double *ax = k == 2 ? a + 1 : a;
                *ta = (*ov - *ax) / (d1 - d2);
                return 1;
            }
        }
    }
    return 0;
}
/* l_ints 0x1804b3480 (systol = mccore `tol`) */
static void l_ints(double systol, const double *L1, const double *L2, double out[2], double *t1, double *t2, int *ok) {
    double d17 = L1[3] - L1[1], d22 = L1[2] - L1[0], d19 = d17 * d17 + d22 * d22;
    if (1e-24 <= d19) {
        if (1e-24 <= (d22 * d22) / d19) {
            if (1e-24 <= (d17 * d17) / d19) {
                double y1 = L2[1], y2 = L2[3], x1 = L2[0];
                double d4 = (L2[2] - x1) * (L2[2] - x1), d15 = (y2 - y1) * (y2 - y1), d20 = d15 + d4;
                if (1e-24 <= d20) {
                    if (1e-24 <= d4 / d20) {
                        if (1e-24 <= d15 / d20) {
                            double d5 = x1 - L1[0], d18 = systol * systol, d21 = x1 - L2[2], Y1 = L1[1];
                            *ok = 1;
                            double dy2 = y1 - y2, den = dy2 * d22 - d21 * d17, num = d5 * dy2 - (y1 - Y1) * d21, d16 = dy2 * d17 + d21 * d22;
                            int gt, eq;
                            if (d18 < fabs(d16) || d18 == fabs(d16)) { eq = systol == fabs(den / d16); gt = systol < fabs(den / d16); }
                            else { eq = d18 == fabs(den); gt = d18 < fabs(den); }
                            if (gt || eq) {
                                num = num / den; *t1 = num;
                                double ox = d22 * num, oy = d17 * num;
                                out[0] = L1[0] + ox; out[1] = oy + L1[1];
                                *t2 = ((y1 - Y1) * d22 - d5 * d17) / den;
                            } else {
                                *ok = 0;
                                double ln = sqrt(d21 * d21 + dy2 * dy2);
                                if (systol < fabs(ln)) {
                                    double dd = num / ln;
                                    int a = fabs(dd) <= systol, b = systol != fabs(dd);
                                    *ok = a && b;
                                    if (*ok) {
                                        double mn = 1e38;
                                        int s8 = 1, s9 = 1;
                                        for (int i = 0; i < 2; i++) {
                                            double px = L1[2 * i], py = L1[2 * i + 1];
                                            for (int j = 0; j < 2; j++) {
                                                double d = sqrt((L2[2 * j + 1] - py) * (L2[2 * j + 1] - py) + (L2[2 * j] - px) * (L2[2 * j] - px));
                                                if (d <= mn && mn != d) { mn = d; s8 = j; s9 = i; }
                                            }
                                        }
                                        double oy2 = (L2[2 * s8 + 1] + L1[2 * s9 + 1]) * 0.5;
                                        out[0] = (L2[2 * s8] + L1[2 * s9]) * 0.5; out[1] = oy2;
                                        t_on_line(systol, t1, out, L1); t_on_line(systol, t2, out, L2);
                                    }
                                }
                            }
                        } else *ok = l_hv_ints(1, L2, L1, out, t2, t1);
                    } else *ok = l_hv_ints(2, L2, L1, out, t2, t1);
                } else *ok = 0;
            } else *ok = l_hv_ints(1, L1, L2, out, t1, t2);
        } else *ok = l_hv_ints(2, L1, L2, out, t1, t2);
    } else *ok = 0;
}
static void center_3pa(double systol, const double *p1, const double *p2, const double *p3, double out[2], int *ok) {   /* 0x1804b5200 */
    *ok = 0;
    double d4 = p2[1] - p1[1], d7 = p2[0] - p1[0], d8 = p2[0], d9 = p2[1], x1 = p1[0], y1 = p1[1];
    d4 = sqrt(d4 * d4 + d7 * d7);
    if (systol <= d4) {
        double x3 = p3[0], y3 = p3[1], d8b = x3 - d8, d9b = y3 - d9;
        d8b = sqrt(d9b * d9b + d8b * d8b);
        if (systol <= d8b) {
            double d2b = y1 - y3, d1b = x1 - x3;
            double dd = sqrt(d2b * d2b + d1b * d1b);
            if (systol <= dd) {
                double L1[4], L2[4];
                L1[0] = (p1[0] + p2[0]) * 0.5; double my1 = (p1[1] + p2[1]) * 0.5; L1[1] = my1;
                double my2 = (p3[1] + p2[1]) * 0.5, mx2 = (p3[0] + p2[0]) * 0.5;
                L2[0] = mx2; L2[1] = my2;
                L1[2] = -(p2[1] - p1[1]) + L1[0]; L1[3] = (p2[0] - p1[0]) + my1;
                L2[2] = -(p3[1] - p2[1]) + mx2; L2[3] = (p3[0] - p2[0]) + my2;
                double ta = 0, tb = 0;
                l_ints(systol, L1, L2, out, &ta, &tb, ok);
            }
        }
    }
}
static void cr_a_3p(double systol, const double *p1, const double *p2, const double *p3, A2 *a, int *ok) {   /* 0x1804b5c90 */
    double c[2] = { a->x, a->y };
    center_3pa(systol, p1, p2, p3, c, ok);
    a->x = c[0]; a->y = c[1];
    if (*ok) {
        a->r = sqrt((p1[1] - a->y) * (p1[1] - a->y) + (p1[0] - a->x) * (p1[0] - a->x));
        double s = ang_of(p1[1] - a->y, p1[0] - a->x);
        double m = ang_of(p2[1] - a->y, p2[0] - a->x);
        double e = ang_of(p3[1] - a->y, p3[0] - a->x);
        restrict_angle(0.0, TWO_PI, &s);
        a->a0 = s;
        a->sw = sweep_angle(s, m, e);
    }
}
/* constr_arc_new 0x1804b5830: arc about c from s to e (swapped when cw), radii within rtol; systol for s == e */
static void constr_arc_new(double systol, const double *p1, const double *p2, const double *c, int cw, double rtol, A2 *a, int *ok) {
    const double *s = cw ? p2 : p1, *e = cw ? p1 : p2;
    double sx = s[0] - c[0], sy = s[1] - c[1], ex = e[0] - c[0], ey = e[1] - c[1];
    double ss = sx * sx + sy * sy, r1 = sqrt(ss), ee = ex * ex + ey * ey, r2 = sqrt(ee);
    if (fabs(r1 - r2) < rtol || fabs(r1 - r2) == rtol) {
        double dx = e[0] - s[0], dy = e[1] - s[1], sa, sw;
        if (systol <= sqrt(dy * dy + dx * dx)) {
            if (1e-12 <= ss) { sa = atan2(sy, sx); if (sa < 0.0) sa += TWO_PI; } else sa = 0.0;
            double ea = 0.0;
            if (1e-12 <= ee) { ea = atan2(ey, ex); if (ea < 0.0) ea += TWO_PI; }
            for (sw = ea - sa; sw <= 1e-08; sw += TWO_PI) {}
        } else { sw = TWO_PI; sa = 0.0; }
        a->x = c[0]; a->y = c[1]; a->a0 = sa; a->r = r1; a->sw = sw;
        *ok = 1;
    } else *ok = 0;
}
/* exported for ifk_hbechain.c (mucbMakeMcArc3D): out = { cx, cy, r, start angle, sweep } (a_2d +0, +8, +0x10, +0x18, +0x20) */
int ifk_constr_arc_new(double systol, const double *p1, const double *p2, const double *c, int cw, double rtol, double out[5]) {
    A2 a = { 0, 0, 0, 0, 0 }; int ok = 0;
    constr_arc_new(systol, p1, p2, c, cw, rtol, &a, &ok);
    out[0] = a.x; out[1] = a.y; out[2] = a.r; out[3] = a.a0; out[4] = a.sw;
    return ok;
}
/* constr_ns_arc 0x1804b5c00 */
static void constr_ns_arc(double systol, const double *p1, const double *p2, const double *c, int cw, double rtol, A2 *a, int *ok) {
    constr_arc_new(systol, p1, p2, c, cw, rtol, a, ok);
    double sw = a->sw;
    if (TWO_PI < sw) { *ok = 0; return; }
    if (cw && *ok) { a->a0 = sw + a->a0; a->sw = -sw; }
}
/* proj_pt_ln 0x180002780 */
static void proj_pt_ln(const double *p, const double *l, double *foot, double *t, int *ok) {
    double dx = l[2] - l[0], dy = l[3] - l[1], d = dy * dy + dx * dx;
    if (d != 0.0) {
        double u = ((p[1] - l[1]) * dy + (p[0] - l[0]) * dx) / d;
        *t = u;
        foot[0] = (1.0 - u) * l[0] + u * l[2];
        foot[1] = (1.0 - u) * l[1] + u * l[3];
        *ok = 1;
    } else *ok = 0;
}
/* rotate_pt 0x1804b4550 */
static void rotate_pt(const double *p, const double *c, double co, double si, double *o) {
    double dx = p[0] - c[0], dy = p[1] - c[1];
    double y = dy * co + dx * si;
    o[0] = dx * co - dy * si + c[0];
    o[1] = y + c[1];
}
/* UnitNormal 0x1804b7480 (the error code is not used by the caller here) */
static void unit_normal(const double *a, const double *b, double *o) {
    o[0] = b[2] * a[1] - b[1] * a[2];
    o[1] = b[0] * a[2] - a[0] * b[2];
    o[2] = a[0] * b[1] - b[0] * a[1];
    double l = sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
    if (l < 1e-12) l = 1e-12;
    o[0] /= l; o[1] /= l; o[2] /= l;
}

/* ---------------------------------------------------------------------------------------------- CMutlFilterHbeAra */
typedef struct {
    double s[3], e[3], c[3], r, sa, sw;   /* FitArc3D result +0xa0..+0x100 (committed copy +0x108..+0x168) */
    short view;
} Arc3;

typedef struct {
    /* constructor FUN_1802097b0 */
    int plane_z, plane_x, plane_y;  /* +0x58 / +0x5a / +0x59 (FUN_180212eb0 order: [0] = +0x58, [1] = +0x5a, [2] = +0x59) */
    double systol;                  /* +0x60 = mccore `tol` (?tol@@3NA, IAT 0x1803e65a0, read in FUN_18020de20) */
    double tol;                     /* +0x68 = converter +0x180 */
    double max_piece;               /* +0x90 = 0x4008fe3cc6ff87db = 3.1241393610698487 (179 degrees) */
    short max_win;                  /* +0x1a2 = (short) converter +0x17c */
    /* point array FUN_18020bc80 / FUN_18020dd40 */
    const double *pts; int npts, cap;   /* +0x98, +0x174, +0x170 */
    /* state (FUN_180212a80) */
    double arc_dev, line_dev, c_arc_dev, c_line_dev;  /* +0x70, +0x78, +0x80, +0x88 */
    int s, e, dir, e_end, c_s, c_e, done_at, last, nout, last_written;  /* +0x178 +0x17c +0x180 +0x184 +0x188 +0x18c +0x190 +0x194 +0x198 +0x19c */
    short win;                      /* +0x1a0 */
    int arc_ok, c_arc;              /* +0x5b, +0x5c */
    int have_center;                /* +0x1a4 */
    Arc3 arc, c_arcd;               /* +0xa0.., +0x108.. */
    double *p2;                     /* PT_2D CArray +0x30 (data +0x38, size +0x40) */
    int n2;
    int undefined_hit;              /* set when the original would read stale stack (see FUN_18020e260) */
} Filt;

static const double *P(const Filt *f, int i) { return f->pts + 3 * (size_t)i; }

/* FUN_180212a80 */
static int filt_init(Filt *f) {
    f->arc_dev = f->line_dev = f->c_arc_dev = f->c_line_dev = 1e38;
    f->s = 0; f->e = 0;
    f->c_s = 0; f->done_at = 0; f->nout = 0;
    if (1 < f->npts) {
        f->last = f->npts - 1;
        f->e = 1; f->dir = 1; f->win = 1; f->c_e = 1;
        return 1;
    }
    f->dir = 0; f->e_end = 0; f->win = 0; f->c_e = 0;   /* +0x180 8 bytes = 0 covers +0x184 too */
    return 0;
}
/* FUN_18020ea40 */
static int filt_done(const Filt *f) {
    if (f->dir == 1 && f->last <= f->done_at) return 1;
    if (f->dir == -1 && f->done_at <= f->last) return 1;
    return 0;
}
/* FUN_18020f2c0: the largest deviation of the window's interior points from the chord s -> e */
static int line_dev(Filt *f) {
    int s = f->s, e = f->e;
    f->line_dev = 0.0;
    int lo = e < s ? e : s, hi = s < e ? e : s;
    if (!(-1 < lo && hi <= f->npts - 1 && 1 < hi - lo)) return 0;
    const double *A = P(f, s), *B = P(f, e);
    double ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2];
    double L = sqrt(uy * uy + ux * ux + uz * uz);
    if (L == 0.0) { ux = uy = uz = 0.0; } else { ux /= L; uy /= L; uz /= L; }
    for (int i = s + f->dir; i != e; i += f->dir) {
        const double *Q = P(f, i);
        double ax = Q[0] - A[0], ay = Q[1] - A[1], az = Q[2] - A[2];
        double da = sqrt(ay * ay + ax * ax + az * az);
        double bx = Q[0] - B[0], by = Q[1] - B[1], bz = Q[2] - B[2];
        double db = sqrt(by * by + bx * bx + bz * bz);
        double m = da <= db ? db : da;                       /* the farther end */
        double beyond = (m < L || m == L) ? 0.0 : (m - L) * 2.0;
        double cx = ay * uz - az * uy, cy = az * ux - uz * ax, cz = uy * ax - ay * ux;
        double d = sqrt(cy * cy + cx * cx + cz * cz);        /* |(Q - A) x u| */
        if (d <= beyond) d = beyond;
        if (f->line_dev <= d && d != f->line_dev) f->line_dev = d;
        if (f->tol < f->line_dev) { f->line_dev = f->tol + 1.0; return 1; }
    }
    return 1;
}

/* min / max distance from c to the PT_2D points and to their chord midpoints (the loops of FUN_18020b750 / FUN_18020e260) */
static void radial_minmax(const Filt *f, const double *c, double *mn, double *mx) {
    double lo = 1e38, hi = 0.0;
    for (int i = 0; i < f->n2; i++) {
        double dy = f->p2[2 * i + 1] - c[1], dx = f->p2[2 * i] - c[0], d = sqrt(dy * dy + dx * dx);
        if (d <= lo && lo != d) lo = d;
        if (hi <= d && d != hi) hi = d;
    }
    for (int i = 1; i < f->n2; i++) {
        double dx = (f->p2[2 * (i - 1)] * 0.5 + f->p2[2 * i] * 0.5) - c[0];
        double dy = (f->p2[2 * (i - 1) + 1] * 0.5 + f->p2[2 * i + 1] * 0.5) - c[1];
        double d = sqrt(dy * dy + dx * dx);
        if (d <= lo && lo != d) lo = d;
        if (hi <= d && d != hi) hi = d;
    }
    *mn = lo; *mx = hi;
}
/* FUN_18020b750: 0 as soon as (max - min) > lim; the original tests after every point (same result: max - min only grows) */
static int radial_ok(const Filt *f, const double *c, double lim) {
    double mn, mx;
    radial_minmax(f, c, &mn, &mx);
    return !(lim < mx - mn);
}
/* FUN_180211a70 = CMutlFilterHbeAra::GetTheorCenterPt: intersection of the perpendicular bisectors of the chord P0 -> Pm
   (m = (n - 1) / 2) and of the chord Pi -> Pj (i = m..1, j = n-1..n-m) most perpendicular to it */
static void theor_center(Filt *f, double *out) {
    int n = f->n2;
    const double *p = f->p2;
    out[0] = out[1] = 0.0;
    long m = (long)(n - 1) / 2, j = n - 1, i = m, bi = m, bj = n - 1;
    double ux = p[2 * m] - p[0], uy = p[2 * m + 1] - p[1], L = sqrt(uy * uy + ux * ux);
    if (L == 0.0) { uy = 0.0; ux = 0.0; } else { ux /= L; uy /= L; }
    double best = 1e38;
    do {
        double vx = p[2 * i] - p[2 * j], vy = p[2 * i + 1] - p[2 * j + 1], l = sqrt(vx * vx + vy * vy);
        if (l == 0.0) { vy = 0.0; vx = 0.0; } else { vx /= l; vy /= l; }
        double d = vy * uy + vx * ux;
        if (fabs(d) <= best && best != fabs(d)) { bj = j; bi = i; best = fabs(d); }
        i--; j--;
    } while (0 < i);
    /* 1.000000001 <= best: _MCAssert(MSurfUtl.cpp 0xfd2) only */
    if (1e-09 < fabs(best - 1.0) || fabs(best - 1.0) == 1e-09) {
        double x0 = p[0], y0 = p[1], xm = p[2 * m], ym = p[2 * m + 1];
        double xj = p[2 * bj], yj = p[2 * bj + 1], xi = p[2 * bi], yi = p[2 * bi + 1];
        double L1[4], L2[4], c[2], t1, t2;
        int ok = 0;
        L1[0] = (xm + x0) * 0.5; L1[1] = (ym + y0) * 0.5;
        L1[2] = -(ym - y0) + L1[0]; L1[3] = (xm - x0) + L1[1];
        L2[0] = (xi + xj) * 0.5; L2[1] = (yi + yj) * 0.5;
        L2[2] = -(yi - yj) + L2[0]; L2[3] = (xi - xj) + L2[1];
        l_ints(f->systol, L1, L2, c, &t1, &t2, &ok);
        if (!ok) f->have_center = 0;
        else {
            f->have_center = 1;
            out[0] = c[0]; out[1] = c[1];
            double d = sqrt((x0 - c[0]) * (x0 - c[0]) + (y0 - c[1]) * (y0 - c[1]));
            if (10000.0 < d) f->have_center = 0;
        }
    } else f->have_center = 0;
}
/* FUN_18020e260: 3 x 3 grid search (step h = |p3| / 2) for the centre with the smallest radial spread, recursive */
static void grid_center(Filt *f, const double *c0, double p3, double p4, double *oc, double *omin, double *omax) {
    if (p4 >= p3) return;                                 /* comisd xmm3, xmm2; jae: nothing written */
    double h = fabs(p3) * 0.5, ap4 = fabs(p4), g[9][2];
    double cx = c0[0], cy = c0[1];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) { g[3 * i + j][0] = cx + (j - 1) * h; g[3 * i + j][1] = cy + (1 - i) * h; }
    double best = 1e38;
    int bi = 0, bj = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            double mn, mx;
            radial_minmax(f, g[3 * i + j], &mn, &mx);
            double s = fabs(mx - mn);
            if (s <= best && best != s) { *omin = mn; bi = i; bj = j; *omax = mx; best = s; }
        }
    oc[0] = g[3 * bi + bj][0]; oc[1] = g[3 * bi + bj][1];
    if (!(h <= ap4 || best < ap4 || h * 2.0 <= best)) {
        double nc[2] = { oc[0], oc[1] };
        grid_center(f, nc, h, ap4, oc, omin, omax);
    } else if (ap4 * 10.0 < best) {
        *omin = 0.0; *omax = 1e38;
    } else {
        /* project the centre onto the perpendicular bisector of the first and last points */
        const double *a = f->p2, *b = f->p2 + 2 * (f->n2 - 1);
        double mx_ = b[0] * 0.5 + a[0] * 0.5, my_ = b[1] * 0.5 + a[1] * 0.5, dy = b[1] - a[1], dx = b[0] - a[0];
        double L[4] = { dy + mx_, -dx + my_, mx_ - dy, my_ - -dx }, foot[2], t;
        int ok;
        proj_pt_ln(oc, L, foot, &t, &ok);
        if (!ok) { f->undefined_hit = 1; *omin = 0.0; *omax = 1e38; return; }   /* original: stale stack as the centre (UNDEFINED); here: fail */
        oc[0] = foot[0]; oc[1] = foot[1];
        radial_minmax(f, oc, omin, omax);
    }
}
/* FUN_18021c280: plane coordinate k of a point: 0 -> z, 1 -> x, 2 -> -y */
static double plane_coord(const double *p, int k) { return k == 0 ? p[2] : k == 1 ? p[0] : -p[1]; }

/* FUN_18020ea80 = CMutlFilterHbeAra::FitArc3D: an arc s -> e within tol in a principal plane */
static int fit_arc(Filt *f) {
    f->arc_dev = f->tol + 1.0;
    int s = f->s, e = f->e;
    int lo = e < s ? e : s, hi = s < e ? e : s;
    if (lo < 0 || f->cap - 1 < hi || hi - lo < 2) return 0;
    if (!f->plane_z && !f->plane_y && !f->plane_x) return 0;
    double ref[3];
    int flag[3] = { f->plane_z, f->plane_x, f->plane_y };  /* FUN_180212eb0: +0x58, +0x5a, +0x59 */
    for (int k = 0; k < 3; k++) ref[k] = plane_coord(P(f, s), k);
    int e_end = f->dir + e, res = 0;
    f->e_end = e_end;
    int i = f->dir + s;
    if (i == e_end) return 0;
    for (; i != e_end; i += f->dir) {                       /* FUN_180220040 per point s+dir .. e */
        int cnt = 0, lastk = -1;
        for (int k = 0; k < 3; k++)
            if (flag[k]) {
                if (f->systol <= fabs(plane_coord(P(f, i), k) - ref[k])) flag[k] = 0;
                else { cnt++; lastk = k; }
            }
        res = cnt == 1 ? lastk : cnt == 0 ? -2 : -1;
        if (res == -2) return 0;
    }
    short view;
    if (res == 0) view = 1; else if (res == 1) view = 5; else if (res == 2) view = 2; else return 0;
    if (view != 1) return 0;                                 /* NOT PORTED: world_to_2d in views 5 / 2 (never reached from the converter) */
    /* PT_3D = points s..e (the dir order), PT_2D = world_to_2d(view) = (x, y) in view 1 */
    f->n2 = 0;
    for (int k = s;; k += f->dir) {
        f->p2[2 * f->n2] = P(f, k)[0]; f->p2[2 * f->n2 + 1] = P(f, k)[1]; f->n2++;
        if (k == e) break;
    }
    double c[2];
    theor_center(f, c);
    if (!f->have_center) return 0;
    if (!radial_ok(f, c, f->tol * 10.0)) return 0;
    double gc[2] = { c[0], c[1] }, mn = 0.0, mx = 0.0;   /* tol <= 0: FUN_18020e260 writes nothing and the original reads uninitialised stack (UNDEFINED) */
    grid_center(f, c, f->tol * 20.0, f->tol * 0.5, gc, &mn, &mx);
    f->arc_dev = fabs(mx - mn);
    if (f->tol < f->arc_dev) { f->arc_dev = f->tol + 1.0; return 0; }
    int n = f->n2;
    const double *p0 = f->p2, *pl = f->p2 + 2 * (n - 1);
    double best = 1e38;
    int mid = 1;
    for (int k = 1; k < n - 1; k++) {                        /* the point most equidistant from the two ends */
        const double *q = f->p2 + 2 * k;
        double d1 = sqrt((p0[0] - q[0]) * (p0[0] - q[0]) + (p0[1] - q[1]) * (p0[1] - q[1]));
        double d2 = sqrt((pl[0] - q[0]) * (pl[0] - q[0]) + (pl[1] - q[1]) * (pl[1] - q[1]));
        double d = fabs(d1 - d2);
        if (d < best) { best = d; mid = k; }
    }
    A2 a = { 0, 0, 0, 0, 0 };
    int ok = 0;
    cr_a_3p(f->systol, p0, f->p2 + 2 * mid, pl, &a, &ok);
    if (!ok) return 0;
    int cw = 0.0 > a.sw;
    constr_ns_arc(f->systol, p0, pl, gc, cw, f->tol, &a, &ok);
    if (!ok) return 0;
    /* f->tol <= |mx - mn|: _MCAssert(MSurfUtl.cpp 0x1326, FitArc3D) only */
    if (f->tol * 50.0 > a.r) return 0;
    if (5.969026041820607 < fabs(a.sw)) return 0;
    f->arc.c[0] = a.x; f->arc.c[1] = a.y; f->arc.c[2] = ref[res];
    f->arc.r = a.r; f->arc.sa = a.a0;
    restrict_angle(0.0, TWO_PI, &f->arc.sa);
    f->arc.view = view;
    f->arc.s[0] = P(f, s)[0]; f->arc.s[1] = P(f, s)[1]; f->arc.s[2] = ref[res];
    f->arc.e[0] = P(f, e)[0]; f->arc.e[1] = P(f, e)[1]; f->arc.e[2] = ref[res];
    f->arc.sw = a.sw;
    return 1;
}
/* FUN_18021eec0 */
static void commit(Filt *f) {
    f->c_s = f->s; f->c_e = f->e;
    f->c_line_dev = f->line_dev; f->c_arc_dev = f->arc_dev;
    f->c_arc = f->arc_ok;
    if (f->arc_ok) f->c_arcd = f->arc;
}
/* FUN_18021f470 */
static int write_line(Filt *f, int cap, HbeRec *out) {
    if (!(f->nout < cap)) return -2;
    HbeRec *r = out + f->nout;
    memcpy(r->p0, P(f, f->c_s), 24);
    memcpy(r->p1, P(f, f->c_e), 24);
    r->type = 1;
    r->first = f->nout == 0;
    f->nout++;
    f->last_written = f->c_e;
    return 0;
}
/* FUN_18021f080: the committed arc as (int)(|sweep| / 179 deg) + 1 equal pieces */
static int write_arc(Filt *f, int cap, HbeRec *out) {
    const Arc3 *A = &f->c_arcd;
    short np = (short)(int)(fabs(A->sw) / f->max_piece) + 1;
    int n = np;
    if (!(f->nout + -1 + n < cap)) return -2;
    double co = 0, si = 0, cen[2] = { 0, 0 }, ps[3] = { 0, 0, 0 }, pe[3] = { 0, 0, 0 }, nrm[3] = { 0, 0, 0 };
    for (short k = 0; k < np; k++) {
        if (k == 0) {
            memcpy(ps, A->s, 24);
            if (1 < np) {
                double t = A->sw / (double)n;
                co = cos(t); si = sin(t);
                cen[0] = A->c[0]; cen[1] = A->c[1];
            }
        } else memcpy(ps, pe, 24);
        if ((int)k == n - 1) memcpy(pe, A->e, 24);
        else { rotate_pt(ps, cen, co, si, pe); pe[2] = ps[2]; }
        if (k == 0) {
            double v0[3] = { ps[0] - A->c[0], ps[1] - A->c[1], ps[2] - A->c[2] };
            double v1[3] = { pe[0] - A->c[0], pe[1] - A->c[1], pe[2] - A->c[2] };
            unit_normal(v0, v1, nrm);
        }
        HbeRec *r = out + f->nout;
        memcpy(r->p0, ps, 24); memcpy(r->p1, pe, 24); memcpy(r->c, A->c, 24); memcpy(r->n, nrm, 24);
        r->type = 2;
        r->first = f->nout == 0;
        f->nout++;
    }
    f->last_written = f->c_e;
    return 0;
}
/* FUN_18020f6e0: the arc when the line misses tol or deviates more than the arc */
static int write_committed(Filt *f, int cap, HbeRec *out) {
    if (f->c_arc && (f->tol <= f->c_line_dev || f->c_arc_dev < f->c_line_dev)) return write_arc(f, cap, out);
    return write_line(f, cap, out);
}
/* FUN_18021e8e0 */
static void advance(Filt *f) {
    int k = f->c_e;
    f->c_line_dev = 0.0;
    f->done_at = k; f->c_s = k; f->c_e = f->dir + k;
    f->c_arc = 0; f->c_arc_dev = 1e38;
    f->s = k; f->e = f->dir + k; f->win = 1;
}
/* FUN_18020f630: the rest as single lines */
static int flush(Filt *f, int cap, HbeRec *out) {
    if (f->last_written < f->last) {
        f->c_s = f->last_written < 0 ? 0 : f->last_written;
        do {
            f->c_e = f->dir + f->c_s;
            if (write_line(f, cap, out) != 0) return 0;
            f->c_s = f->c_e;
        } while (f->last_written < f->last);
    }
    return 1;
}

/* FUN_18020dd40 + FUN_18020e0b0.
 *   in / n_in   : one curve's records (+0x80, count +0x160); all must be lines (+0x60 == 1), else -2
 *   out / cap   : output (+0x88, capacity +0x15c); only the bytes listed in HbeRec are written
 *   plane_z     : filter +0x58 = converter +0x178 (= dat[0x50]; 0 when FUN_18020de20 takes the arc-fitter branch)
 *   max_win     : (short) converter +0x17c (= int at dat+0x48; FUN_1802095f0 default 100)
 *   tol         : converter +0x180 (= double at dat+0; arc-fitter branch: 1e-6 when dat[0x4d] == 4, else dat[0x10] * 0.9)
 *   systol      : mccore `tol`
 * returns the number of records written, or < 0 (-1 filter failure, -2 non-line input, -4 allocation) */
int hbe_filter(const HbeRec *in, int n_in, HbeRec *out, int cap, int plane_z, short max_win, double tol, double systol) {
    Filt f;
    memset(&f, 0, sizeof f);
    f.plane_z = plane_z; f.plane_x = 0; f.plane_y = 0;      /* +0x5a / +0x59: converter +0x17a / +0x179, always 0 */
    f.systol = systol; f.tol = tol; f.max_piece = 3.1241393610698487; f.max_win = max_win;
    f.last_written = -1;                                     /* +0x19c = -1 (FUN_1802097b0) */
    if (n_in < 1) return -1;                                 /* n_in == 0: the original reads record -1 (UNDEFINED), then fails in FUN_180212a80 */
    f.cap = n_in + 10;                                       /* FUN_18020bc80(this, n + 10) */
    double *pts = (double *)malloc(sizeof(double) * 3 * (size_t)(n_in + 1));
    f.p2 = (double *)malloc(sizeof(double) * 2 * (size_t)(n_in + 1));
    if (!pts || !f.p2) { free(pts); free(f.p2); return -4; }
    for (int i = 0; i < n_in; i++) {
        if (in[i].type != 1) { free(pts); free(f.p2); return -2; }
        memcpy(pts + 3 * i, in[i].p0, 24);                  /* every record's start (+0x00) */
    }
    memcpy(pts + 3 * n_in, in[n_in - 1].p1, 24);            /* and the last record's end (+0x18) */
    f.pts = pts; f.npts = n_in + 1;
    int rc = -1;
    if (filt_init(&f)) {
        while (!filt_done(&f)) {
            f.win++;
            f.e += f.dir;
            int go_write = 0;
            if (f.max_win < f.win || f.last < f.e) go_write = 1;
            else {
                line_dev(&f);
                f.arc_ok = fit_arc(&f);
                if (!f.arc_ok) { if (f.tol < f.line_dev) go_write = 1; }
                else if (f.tol < f.line_dev && f.tol < f.arc_dev) go_write = 1;
                if (!go_write) commit(&f);
            }
            if (go_write) {
                if (write_committed(&f, cap, out) < 0) goto done;
                advance(&f);
            }
        }
        if (flush(&f, cap, out)) rc = f.nout;
    }
done:
    free(pts); free(f.p2);
    return rc;
}

#ifdef HBE_FILTER_TEST
#include <stdio.h>
int main(void) {
    /* a closed square 0..10 with a quarter circle (r 5) replacing one corner, as 1-unit-ish line records */
    HbeRec in[300], out[400];
    int n = 0;
    double pts[300][3];
    int np = 0;
    for (int i = 0; i <= 5; i++) { pts[np][0] = i; pts[np][1] = 0; pts[np][2] = 2; np++; }
    for (int i = 1; i <= 90; i++) { double t = -M_PI / 2 + (M_PI / 2) * i / 90.0; pts[np][0] = 5 + 5 * cos(t); pts[np][1] = 5 + 5 * sin(t); pts[np][2] = 2; np++; }
    for (int i = 1; i <= 15; i++) { pts[np][0] = 10; pts[np][1] = 5 + i * (5.0 / 15); pts[np][2] = 2; np++; }
    for (int i = 1; i <= 10; i++) { pts[np][0] = 10 - i; pts[np][1] = 10; pts[np][2] = 2; np++; }
    for (int i = 1; i <= 10; i++) { pts[np][0] = 0; pts[np][1] = 10 - i; pts[np][2] = 2; np++; }
    for (int i = 0; i + 1 < np; i++) {
        memset(&in[n], 0, sizeof in[n]);
        memcpy(in[n].p0, pts[i], 24); memcpy(in[n].p1, pts[i + 1], 24); in[n].type = 1; in[n].first = i == 0; n++;
    }
    memset(out, 0, sizeof out);
    int k = hbe_filter(in, n, out, 400, 1, 100, 0.001, 0.0001);
    printf("in %d records -> %d\n", n, k);
    for (int i = 0; i < k; i++)
        printf("%2d type %d first %d (%.4f %.4f %.4f) -> (%.4f %.4f %.4f)%s c (%.4f %.4f %.4f) n (%.3f %.3f %.3f)\n", i, out[i].type, out[i].first,
               out[i].p0[0], out[i].p0[1], out[i].p0[2], out[i].p1[0], out[i].p1[1], out[i].p1[2], "", out[i].c[0], out[i].c[1], out[i].c[2], out[i].n[0], out[i].n[1], out[i].n[2]);
    return 0;
}
#endif
