/* draft_create_ang - Mastercam 2017's ANGULAR dimension creation (see draft_create_ang.h).
 *
 * Port of mccore.dll (Ghidra VAs, image base 0x180000000); decompiles in decomp/draft/ ("file:line"; ang_core_a1.c / ang_core_a2.c are
 * this port's new dumps), argument order checked in `objdump -d mccore.dll` where Ghidra truncated the call ("asm"):
 *   calc_ang_dimension @180021a40 (core_calcdim.c 3-147)                        -> dc_calc_ang
 *   FUN_1800226b0 driver (core_calcdim.c 3295-3447; asm 0x1800226b0-0x180022cfa)  -> f26b0
 *   FUN_1800206f0 arcs / witnesses / text (core_dimlayout.c 122-787; asm 0x1800206f0-0x180021a32, the arc branch table
 *     0x180020d94-0x180021353 translated from the asm)                               -> f06f0
 *   FUN_180022d40 record assembly (core_dimlayout.c 789-1034)                       -> f22d40
 *   FUN_180020470 quadrant code / ratio (core_calcdim.c 3487-3605)                  -> f0470
 *   FUN_180021e70 / FUN_180021f00 / FUN_180022300 / FUN_1800223c0 / FUN_180022470 (asm 0x180022470-0x1800226ae) / FUN_180023440 /
 *   FUN_180023630 / FUN_180023650 / FUN_180022d00 / FUN_1800206c0 / FUN_180020320 (ang_core_a1.c)
 *   df_angular_set_ep_angles @180241fa0, ang_code2quad @18023f2f0, determine_ang_quad @18023fe30, FUN_18023f360 (core_calcdim.c 3865),
 *   FUN_180242330, df_ang_vlines_set @180022220, df_angular_extend_vlines @180241ba0, la_ints @1804b6d90, pt_near_line @1804b6f70,
 *   pt_equal @180018c90 (asm), l_ints @1804b3480 with its parameters (core_dimgeo.c 826), l_hv_ints (decomp/pocket_b6/
 *   zigzag_decomp_zz_sys.c 68), t_on_line (decomp/mccore_filter.c 4691), proj_pt_ln (SysTypes, decomp/mccore_filter.c 4912)
 *   FUN_18030f410 kind 2 -> FUN_18030ec10 (core_draftwf.c 6989): the angle text (copied from viewer/draft.c fmt_ang, corpus-checked)
 * Not ported: tolerance modes 1-3, degrees-minutes-seconds (format 1), TrueType extents, DraftIsAssoc, view 0 (get_gr_view_n). */
#include "draft_create_ang.h"
#include "draft_create_int.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.141592653589793

/* ------------------------------------------------------------------------------------------------ small mccore helpers */
static double wrap2(double a) { if (TWOPI <= a) a -= TWOPI; return a; }              /* FUN_180023650 */
static double wrap0(double a) { if (a < 0.0) a += TWOPI; return a; }                 /* FUN_180023630 */
static void restrict_ang(double lo, double hi, double *a) {                         /* restrict_angle @1804b4440 */
    double v = *a; while (hi < v) v -= TWOPI; while (v < lo) v += TWOPI; *a = v;
}
static double lang(const double *l) { return wrap0(atan2(l[3] - l[1], l[2] - l[0])); }   /* FUN_180022d00 (atan2 < 2 pi: FUN_180023630) */
static double f300(double a, double b) {                                            /* FUN_180022300: the smaller angle between, (0, pi] */
    restrict_ang(0, TWOPI, &a); restrict_ang(0, TWOPI, &b);
    double d = a < b ? b - a : a - b;
    if (PI < d) d = fabs(d - TWOPI);
    if (1e-06 < d) { restrict_ang(0, TWOPI, &d); return d; }
    return TWOPI;
}
static double f3c0(double a, double b) {                                            /* FUN_1800223c0: counter-clockwise a -> b */
    restrict_ang(0, TWOPI, &a); restrict_ang(0, TWOPI, &b);
    if (b < a) b += TWOPI;
    double d = b - a;
    if (1e-06 < d) { restrict_ang(0, TWOPI, &d); return d; }
    return TWOPI;
}
static int between(double x0, double x1, double x2) {                               /* FUN_1800206c0: x1 within x0 .. x2 (ccw) */
    if ((x1 < x0 || x2 < x1) && (x0 <= x2 || ((x2 < x1 || x0 < x1) && (x1 < x2 || x1 < x0)))) return 0;
    return 1;
}
static int pt_near_line(const double *p, const double *l) {                         /* pt_near_line @1804b6f70 */
    return (p[1] - l[1]) * (p[1] - l[3]) + (p[0] - l[0]) * (p[0] - l[2]) <= TOL;
}
static int proj_pt_ln(const double *p, const double *l, double *foot, double *t) {   /* proj_pt_ln (SysTypes; mccore_filter.c 4912) */
    double dx = l[2] - l[0], dy = l[3] - l[1], d = dy * dy + dx * dx;
    if (d == 0.0) return 0;
    double u = ((p[1] - l[1]) * dy + (p[0] - l[0]) * dx) / d; *t = u;
    foot[0] = (1.0 - u) * l[0] + u * l[2]; foot[1] = (1.0 - u) * l[1] + u * l[3];
    return 1;
}
static int la_ints(const double *l, const double *c, double r, double *p1, double *p2) {   /* la_ints @1804b6d90 (ang_core_a2.c 217) */
    double f[2], t;
    if (!proj_pt_ln(c, l, f, &t)) return 0;
    double d = r * r - ((f[1] - c[1]) * (f[1] - c[1]) + (f[0] - c[0]) * (f[0] - c[0]));
    if (!(-(r * 2.0 * TOL) <= d)) return 0;
    double s = 0.0 <= d ? sqrt(d) : 0.0, ux = l[2] - l[0], uy = l[3] - l[1], L = sqrt(ux * ux + uy * uy), dx = 0, dy = 0;
    if (L != 0.0) { dx = ux / L; dy = uy / L; }
    p1[0] = dx * s + f[0]; p1[1] = dy * s + f[1]; p2[0] = dx * -s + f[0]; p2[1] = dy * -s + f[1];
    return 1;
}
static double abl(double x0, double y0, double x1, double y1, double u0, double v0, double u1, double v1) {   /* df_angle_between_lines_rads -> FUN_180241810 */
    double a = ang_rads(x0, y0, x1, y1), b = ang_rads(u0, v0, u1, v1), d = a <= b ? b - a : a - b;
    if (PI < d) d -= TWOPI;
    return fabs(d);
}
static int pt_equal3(const double *a, const double *b) {                            /* pt_equal @180018c90 (asm): |dx|,|dy|,|dz| < tol */
    return fabs(a[0] - b[0]) < TOL && fabs(a[1] - b[1]) < TOL && fabs(a[2] - b[2]) < TOL;
}
static void aptol(double *p) { for (int k = 0; k < 3; k++) if (fabs(p[k]) < TOL) p[k] = 0.0; }   /* aPointTestTol (as make_wl_single's) */

/* l_hv_ints (zigzag_decomp_zz_sys.c 68) with its parameters: K 1 = A horizontal, 2 = A vertical; *ta / *tb on A / B */
static int l_hv_t(int k, const double *A, const double *B, double *pt, double *ta, double *tb) {
    double a1 = k == 2 ? A[3] : A[2], a0 = k == 2 ? A[1] : A[0];
    if (!(1e-12 <= fabs(a1 - a0))) return 0;
    double d7 = k == 1 ? B[3] - B[1] : B[2] - B[0];
    if (!(1e-12 <= fabs(d7))) return 0;
    double o = k == 2 ? B[3] - B[1] : B[2] - B[0], d6 = sqrt(o * o + d7 * d7);
    if (!(1e-12 <= d6) || !(1.5e-10 <= fabs(d7 / d6))) return 0;
    double t = k == 1 ? (A[1] - B[1]) / d7 : (A[0] - B[0]) / d7;
    *tb = t; pt[0] = (B[2] - B[0]) * t + B[0]; pt[1] = (B[3] - B[1]) * t + B[1];
    *ta = ((k == 2 ? pt[1] : pt[0]) - (k == 2 ? A[1] : A[0])) / (a1 - a0);
    return 1;
}
static double t_on_line(const double *p, const double *l) {                         /* t_on_line @1804b48a0 */
    double dx = l[2] - l[0], dy = l[3] - l[1], d = dy * dy + dx * dx;
    return TOL * TOL < d ? (dy * (p[1] - l[1]) + dx * (p[0] - l[0])) / d : 0.0;
}
/* l_ints @1804b3480 (core_dimgeo.c 826-990) with t1 / t2 */
static int l_ints_t(const double *L1, const double *L2, double *pt, double *t1, double *t2) {
    double dy1 = L1[3] - L1[1], dx1 = L1[2] - L1[0], n1 = dy1 * dy1 + dx1 * dx1;
    if (!(1e-24 <= n1)) return 0;
    if (!(1e-24 <= dx1 * dx1 / n1)) return l_hv_t(2, L1, L2, pt, t1, t2);
    if (!(1e-24 <= dy1 * dy1 / n1)) return l_hv_t(1, L1, L2, pt, t1, t2);
    double by0 = L2[1], by1 = L2[3], bx0 = L2[0], dx2s = (L2[2] - bx0) * (L2[2] - bx0), dy2s = (by1 - by0) * (by1 - by0), n2 = dy2s + dx2s;
    if (!(1e-24 <= n2)) return 0;
    if (!(1e-24 <= dx2s / n2)) return l_hv_t(2, L2, L1, pt, t2, t1);
    if (!(1e-24 <= dy2s / n2)) return l_hv_t(1, L2, L1, pt, t2, t1);
    double d5 = bx0 - L1[0], t2t = TOL * TOL, d21 = bx0 - L2[2], d1 = by0 - by1;
    double det = d1 * dx1 - d21 * dy1, d15 = d5 * d1 - (by0 - L1[1]) * d21, d16 = d1 * dy1 + d21 * dx1;
    int ok = t2t <= fabs(d16) ? TOL <= fabs(det / d16) : t2t <= fabs(det);
    if (ok) {
        double t = d15 / det; *t1 = t; pt[0] = L1[0] + dx1 * t; pt[1] = L1[1] + dy1 * t;
        *t2 = ((by0 - L1[1]) * dx1 - d5 * dy1) / det;
        return 1;
    }
    double l2 = sqrt(d21 * d21 + d1 * d1);
    if (!(TOL < fabs(l2)) || !(fabs(d15 / l2) <= TOL && fabs(d15 / l2) != TOL)) return 0;
    double best = 1e38; int i2 = 1, i1 = 1;                         /* 0x47d2ced32a16a1b1 */
    for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
        double d = sqrt((L2[2 * b + 1] - L1[2 * a + 1]) * (L2[2 * b + 1] - L1[2 * a + 1]) + (L2[2 * b] - L1[2 * a]) * (L2[2 * b] - L1[2 * a]));
        if (d < best) { best = d; i2 = b; i1 = a; }
    }
    pt[0] = (L2[2 * i2] + L1[2 * i1]) * 0.5; pt[1] = (L2[2 * i2 + 1] + L1[2 * i1 + 1]) * 0.5;
    *t1 = t_on_line(pt, L1); *t2 = t_on_line(pt, L2);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ quadrants, end-point angles */
int dc_ang_quad_code(int q, const short *c) {                                       /* FUN_18023f360 (core_calcdim.c 3865) */
    if ((unsigned)(q - 1) < 4) {
        int s3 = q < 4 ? c[q] : c[0];
        if (s3 != c[q - 1]) {
            s3 += 1; int s1 = c[q - 1] + 1;
            if (s1 < s3) s1 *= 10; else s3 *= 10;
            unsigned v = (unsigned short)(s1 + s3);
            if (v < 0x23 && ((0x400805000ULL >> (v & 0x3f)) & 1)) return (int)v;
        }
    }
    return 0xc;                                                     /* FUN_180242fa0(0x6987): a message */
}
int dc_ang_code2quad(int code, const short *c) {                                    /* ang_code2quad @18023f2f0 */
    for (int q = 1; q < 5; q++) if (dc_ang_quad_code(q, c) == code) return q;
    return 1;
}
int dc_ang_quad(const double *v, const double *cur, const double *a, const short *c, int *code) {   /* determine_ang_quad @18023fe30 */
    double t = ang_rads(v[0], v[1], cur[0], cur[1]); int q = 0;
    if (a[0] <= t && t < a[1]) q = 1;
    if (a[1] <= t && t < a[2]) q = 2;
    if (a[2] <= t && t < a[3]) q = 3;
    if (a[3] <= t || t < a[0]) q = 4;
    *code = dc_ang_quad_code(q, c);
    return q;
}
/* df_angular_extend_vlines @180241ba0 (ang_core_a2.c 53): both lines lengthened at both ends until no end point of one equals one of
   the other (at most 1000 rounds) */
static int extend_vlines(double L[2][6]) {
    double e[2], d2 = TOL * 2.0;
    for (int i = 0; i < 2; i++) {
        if (pt_equal3(L[i], L[i] + 3)) return 0;
        double a = ang_rads(L[i][0], L[i][1], L[i][3], L[i][4]);
        if (fabs(a - 4.71238898038469) < 1e-06 || fabs(a - PI) < 1e-06 || fabs(a - 1.5707963267948966) < 1e-06 || fabs(a) < 1e-06) e[i] = d2;
        else {
            double d1;
            do { d1 = a; a = d1 - 0.7853981633974483; } while (1e-06 < d1 - 0.7853981633974483);
            double c = cos(d1), s = sin(d1), r = fabs(d2 / c) <= fabs(d2 / s) ? (d2 / s) / s : (d2 / c) / c;
            e[i] = fabs(r);
        }
    }
    for (int it = 0;; it++) {
        for (int i = 0; i < 2; i++) for (int s = 0; s < 2; s++) {
            const double *b = s == 0 ? L[i] + 3 : L[i]; double *p = s == 0 ? L[i] : L[i] + 3;
            double dx = p[0] - b[0], dy = p[1] - b[1], dz = p[2] - b[2], len = sqrt(dy * dy + dx * dx + dz * dz), k = (e[i] + len) / len;
            double nx = b[0] + dx * k, ny = dy * k + b[1], nz = dz * k + b[2];
            p[0] = nx; p[1] = ny; p[2] = nz;
        }
        int touch = pt_equal3(L[0], L[1]) || pt_equal3(L[0], L[1] + 3) || pt_equal3(L[0] + 3, L[1]) || pt_equal3(L[0] + 3, L[1] + 3);
        if (!touch) return 1;
        if (999 < it) return 0;
    }
}
static void w2v3(const double *vm, const uint8_t *ap, double *o) {
    P3 v; w2v(vm, (const double *)(const void *)(ap + 8), &v); o[0] = v.x; o[1] = v.y; o[2] = v.z;
}
int dc_ang_set_ep(const uint8_t ap[4][0x98], const double vm[9], double L[2][6], double *v, double *a, short *c, DcAngVlines *vl) {
    static const int idx[2][2] = { { 0, 2 }, { 1, 3 } };            /* FUN_180242330: +8 / +0x138 (ap0 / ap2), +0xa0 / +0x1d0 (ap1 / ap3) */
    for (int i = 0; i < 2; i++) for (int s = 0; s < 2; s++) { w2v3(vm, ap[idx[i][s]], L[i] + 3 * s); L[i][3 * s + 2] = 0.0; aptol(L[i] + 3 * s); }
    vl->saved = 0;                                                  /* df_ang_vlines_off */
    for (int i = 0; i < 2; i++) {
        double t = ang_rads(L[i][0], L[i][1], L[i][3], L[i][4]);
        a[i] = t;
        if (PI <= t) { a[i + 2] = t; a[i] = t - PI; c[i + 2] = (short)(i + 2); c[i] = (short)i; }
        else { double tmp[3]; memcpy(tmp, L[i], 24); memcpy(L[i], L[i] + 3, 24); memcpy(L[i] + 3, tmp, 24);
               a[i + 2] = a[i] + PI; c[i] = (short)(i + 2); c[i + 2] = (short)i; }
    }
    if (a[1] < a[0]) {
        double tl[6]; memcpy(tl, L[0], 48); memcpy(L[0], L[1], 48); memcpy(L[1], tl, 48);
        double ta[4] = { a[1], a[0], a[3], a[2] }; memcpy(a, ta, sizeof ta);
        short tc[4] = { c[1], c[0], c[3], c[2] }; memcpy(c, tc, sizeof tc);
    }
    double l0[4] = { L[0][0], L[0][1], L[0][3], L[0][4] }, l1[4] = { L[1][0], L[1][1], L[1][3], L[1][4] }, t[2];
    if (!l_ints_t(l0, l1, v, &t[0], &t[1])) return 0;
    vl->saved = 1; memcpy(vl->l[0], l0, 32); memcpy(vl->l[1], l1, 32); memcpy(vl->qangles, a, 32); memcpy(vl->qcodes, c, 8);   /* df_ang_vlines_set */
    double p[3] = { v[0], v[1], 0.0 }; aptol(p);
    for (int i = 0; i < 2; i++) {
        if (t[i] < 0.0) memcpy(L[i], p, 24);
        if (1.0 < t[i]) memcpy(L[i] + 3, p, 24);
    }
    return extend_vlines(L);
}
void dc_ang_regen_cursor(const uint8_t *E, const double *v, const double *a, const short *c, double *out) {
    int q = dc_ang_code2quad(E[0x2ea], c);                          /* FUN_180303b70 asm 0x180303ce7-0x180303d55 */
    double ratio = D(E, 0x2f0), R = D(E, 0x2e0);
    int ok = 0;
    if (ratio <= 100.0 && TOL <= ratio && (unsigned)(q - 1) < 4) {   /* FUN_180020320 (ang_core_a1.c 345) */
        int s = 3 < q ? 0 : q;
        double d = a[s] - a[q - 1];
        if (d < 0.0) d += TWOPI;
        if (d < PI) {
            d *= ratio; if (fabs(d) < 1e-06) d = 0.0;
            d += a[q - 1];
            rot(v[0] + R, v[1], v[0], v[1], cos(d), sin(d), &out[0], &out[1]);
            ok = 1;
        }
    }
    if (!ok) { out[0] = v[0] + D(E, 0x360); out[1] = v[1] + D(E, 0x368); }
    out[2] = D(E, 0x370);
}

/* ------------------------------------------------------------------------------------------------ the angle text (kind 2) */
static void zeros(char *s, double v, double lim, int lead, int trail) {             /* FUN_18030e2d0 (viewer/draft.c zeros) */
    double l = 1.0 - TOL * lim;
    if (!lead && fabs(v * lim) < l && s[0] == '0') memmove(s, s + 1, strlen(s));
    if (!trail) { size_t n = strlen(s); while (n && s[n - 1] == '0') s[--n] = 0; if (n && s[n - 1] == '.') s[--n] = 0; }
    if (!*s) strcpy(s, "0");
}
static void fixed(char *out, size_t on, int digits, double v) {                     /* "%.*f" of the 2017 CRT (viewer/draft.c fixed) */
    char b[400]; if (digits < 0) digits = 0; if (digits > 15) digits = 15;
    snprintf(b, sizeof b, "%.60f", fabs(v));
    char *dot = strchr(b, '.'); size_t ip = (size_t)(dot - b);
    char dg[400]; size_t n = 0; for (size_t i = 0; i < ip; i++) dg[n++] = b[i];
    for (int i = 0; i < digits; i++) dg[n++] = dot[1 + i];
    int up = dot[1 + digits] >= '5';
    for (long i = (long)n - 1; up && i >= 0; i--) { if (dg[i] == '9') dg[i] = '0'; else { dg[i]++; up = 0; } }
    char r[400]; size_t k = 0; if (up) r[k++] = '1';
    for (size_t i = 0; i < n; i++) { if (i == n - (size_t)digits && digits) r[k++] = '.'; r[k++] = dg[i]; }
    r[k] = 0; snprintf(out, on, "%s%s", v < 0 && strspn(r, "0.") != strlen(r) ? "-" : "", r);
}
static int fmt_ang(char *out, size_t on, int fmt, int digits, double rad, int lead, int trail, int comma) {   /* FUN_18030ec10 */
    char b[300];
    if (fmt == 0 || fmt == 4) { fixed(b, sizeof b, digits, rad * DEG); zeros(b, rad, DEG, lead, trail); snprintf(out, on, "%s\xb0", b); }
    else if (fmt == 2) { fixed(b, sizeof b, digits, rad); zeros(b, rad, 1.0, lead, trail); snprintf(out, on, "%srad", b); }
    else if (fmt == 3) { fixed(b, sizeof b, digits, rad * DEG * 1.1111111111111112); zeros(b, rad, 63.66197723675813, lead, trail); snprintf(out, on, "%sg", b); }
    else return 0;                                                  /* 1 degrees / minutes / seconds (FUN_18030f070): NOT PORTED */
    if (comma) { char *c = strchr(out, '.'); if (c) *c = ','; }
    return 1;
}
/* FUN_18030f410 kind 2, tolerance mode 0 (asm 0x180020825-0x180020928: value = sweep (2 pi - sweep when flipped), scale 1.0, digits
   num_digits_ang; digits 0 keeps leading / trailing zeros as for linear); the template replaces %v for get_ext and is what is kept */
static int f410_ang(Ctx *X, const DcAngInput *in, double value, char *text, double *w, double *h, int *nstr) {
    const DcSettings *S = X->S;
    memset(text, 0, TXT); w[0] = w[1] = w[2] = h[0] = h[1] = h[2] = 0; *nstr = 1;
    int lead = in->digits ? S->lead0 : 1, trail = in->digits ? S->trail0 : 1;
    char num[300];
    if (!fmt_ang(num, sizeof num, in->ang_fmt, in->digits, value, lead, trail, S->comma)) { X->why = "angle format 1 (degrees-minutes-seconds, NOT PORTED)"; return -1; }
    char meas[TXT];
    if (in->tmpl) { replace_v(in->tmpl, num, meas, sizeof meas); snprintf(text, 0x12d, "%s", in->tmpl); }
    else { snprintf(text, 0x12d, "%s", num); snprintf(meas, sizeof meas, "%s", num); }
    double eh, ew;
    if (get_ext(X->F, meas, X->fa, &eh, &ew)) return 0;
    w[0] = ew; h[0] = eh;
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the layout */
typedef struct {                                                    /* the DAT_181371d6x flags FUN_1800226b0 sets */
    int d69, d6a, d6b, d6c, d6d, d6e, d6f, d70, d71, d73, d74, d8;
} Fl;

/* FUN_180022470 (asm 0x180022470-0x1800226ae): the angle of the text box edge / dimension circle crossing nearest to the cursor angle,
   on the ray's side (FLAG 1: between the ray and the cursor, 0: between the cursor and the ray); none: the cursor angle */
static double f2470(const double *box, const double *v, const double *ray, int lv, double R, double cur, int flag) {
    static const int ed[4][2] = { { 0, 1 }, { 0, 2 }, { 1, 3 }, { 2, 3 } };   /* DAT_1812ab0e0 */
    double ra = lang(ray), best = lv ? TWOPI : 0.0, res = cur;
    for (int k = 0; k < 4; k++) {
        double edge[4] = { box[2 * ed[k][0]], box[2 * ed[k][0] + 1], box[2 * ed[k][1]], box[2 * ed[k][1] + 1] }, p1[2], p2[2];
        if (!la_ints(edge, v, R, p1, p2)) continue;
        const double *ch;
        if (pt_near_line(p1, edge)) {
            if (pt_near_line(p2, edge)) {
                double l[4] = { v[0], v[1], p1[0], p1[1] }, a1 = lang(l);
                ch = (flag ? between(ra, a1, cur) : between(cur, a1, ra)) ? p1 : p2;
            } else ch = p1;
        } else if (pt_near_line(p2, edge)) ch = p2;
        else continue;
        double l[4] = { v[0], v[1], ch[0], ch[1] }, a = lang(l);
        if (!(flag ? between(ra, a, cur) : between(cur, a, ra))) continue;
        if (lv) { double d = wrap0(a - cur); if (d < best) { res = a; best = d; } }
        else { double d = wrap0(cur - a); if (best < d) { res = a; best = d; } }
    }
    return res;
}
/* FUN_180023440 (ang_core_a1.c 263): o7 = the nearer box corner distance, at most R - toth / 2 - pad; o8 = the farthest of R - toth / 2
   and the two corners */
static void f3440(const double *p1, const double *p2, const double *p3, double R, double toth, double pad, double *o7, double *o8) {
    double d1 = hypot(p3[0] - p1[0], p3[1] - p1[1]), d2 = hypot(p3[0] - p2[0], p3[1] - p2[1]), m = d2 <= d1 ? d2 : d1;
    double r2 = R - toth * 0.5;
    if (r2 - pad <= m) m = r2 - pad;
    *o7 = m;
    double r3 = r2; if (r2 < d1) r3 = d1;
    *o8 = d2 <= r3 ? r3 : d2;
}
/* the witness of one side (FUN_1800206f0 tail, core_dimlayout.c 560-760, asm 0x1800213c4-0x1800218f0) */
static void witness(const Fl *f, const DcAngInput *in, const DcSettings *S, const double *v, double R, double ang, const double *ray, int li, WL *W) {
    double q[2] = { cos(ang) * R + v[0], sin(ang) * R + v[1] };
    int near = f->d73 ? pt_near_line(q, in->vl.l[li]) : pt_near_line(q, ray);
    double *p0 = W->pts[0], *p1 = W->pts[1];
    if (near) { p0[0] = p1[0] = q[0]; p0[1] = p1[1] = q[1]; return; }
    if (f->d73) {
        double ft[2], t; int ok = proj_pt_ln(q, in->vl.l[li], ft, &t);
        const double *e = (!ok || 0.5 < t) ? in->vl.l[li] + 2 : in->vl.l[li];
        p0[0] = e[0]; p0[1] = e[1];
    } else { p0[0] = ray[2]; p0[1] = ray[3]; }
    p1[0] = q[0]; p1[1] = q[1];
    double dx = q[0] - p0[0], dy = q[1] - p0[1], a = 0.0;
    if (1e-12 <= dy * dy + dx * dx) a = wrap0(atan2(dy, dx));
    p0[0] += cos(a) * S->gap; p0[1] += sin(a) * S->gap;
    p1[0] += cos(a) * S->ext; p1[1] += sin(a) * S->ext;
}

/* FUN_1800206f0: text, box, the two arcs (kind 2: centre +0x20, z +0x30, radius +0x38, start +0x40, sweep +0x48, +0x50) and witnesses */
static int f06f0(Ctx *X, const DcAngInput *in, const Fl *f, const double *v, const double *rA, const double *rB, double R, double aA, double sweep,
                 const double *cur, WL *W0, WL *W1, WL *W2, WL *W3, double *tang, double *ws, double *hs, int *nstr) {
    const DcSettings *S = X->S;
    double value = f->d70 ? TWOPI - sweep : sweep;
    int r = f410_ang(X, in, value, X->text, ws, hs, nstr);
    if (r <= 0) return r;
    double toth = hs[0];                                            /* param_19: the running height sum (one string) */
    double lc[4] = { v[0], v[1], cur[0], cur[1] }, ca = lang(lc), ra = lang(rA), aB = lang(rB);
    int nearA = f300(aB, ca) > f300(ra, ca);                        /* local_277 (asm 0x1800209c9-0x1800209f6) */
    if (!f->d6d) *tang = wrap2((0.0 < ca && ca <= PI) ? ca - 1.570796326794896 : ca + 1.570796326794896);
    else *tang = 0.0;
    double pad = in->extra_ln_sp * in->text_h, bx, by, box[8];       /* extra_ln_sp x dim_text_height (asm 0x180020a45) */
    double pad4 = f->d6a ? pad : 0.0;                               /* FUN_18024c160 */
    box_limits(in->tol_mode, *nstr, pad, hs, ws, &bx, &by);
    box[0] = box[4] = cur[0] - (bx + pad4); box[1] = box[3] = cur[1] - (by + pad4);
    box[2] = box[6] = bx + pad4 + cur[0]; box[5] = box[7] = by + pad4 + cur[1];
    for (int k = 0; k < 4; k++) rot(box[2 * k], box[2 * k + 1], cur[0], cur[1], cos(*tang), sin(*tang), &box[2 * k], &box[2 * k + 1]);
    double half = sweep * 0.5, mid = wrap2(half + aA), midp = mid;
    if (f->d70) midp = wrap2(mid + PI);
    if (f->d69) {                                                   /* leader style 1: the arc radius kept clear of the text (asm 0x180020b07-0x180020c88) */
        double o7 = 0, o8 = 0, nr = 0; int use7 = 0;
        const double *pa = box, *pb = box + 2;
        if (!f->d6d) {
            if (ca < PI) { nr = R - toth * 0.5 - pad; goto have; }
            pa = box; pb = box + 2; use7 = 0;
        } else if (0.7853981633974483 < ca && ca <= 2.356194490192345) { pa = box; pb = box + 2; use7 = 1; }
        else if (2.356194490192345 < ca && ca <= 3.9269908169872414) {
            if (3.665191429188092 <= ca) { pa = box; pb = box + 4; use7 = 0; } else { pa = box + 2; pb = box + 6; use7 = 1; }
        } else if (3.9269908169872414 < ca && ca <= 5.497787143782138) { pa = box; pb = box + 2; use7 = 0; }
        else if (ca < 0.7853981633974483 && 0.5235987755982988 <= ca) { pa = box; pb = box + 4; use7 = 1; }
        else { pa = box + 2; pb = box + 6; use7 = 0; }
        f3440(pa, pb, v, R, toth, pad, &o7, &o8);
        nr = use7 ? o7 : o8;
    have:
        if (0.0 < nr) R = nr;
    }
    double g = 0.25, gg = TWOPI / ((R * TWOPI) / (in->text_h * 2.0));   /* the arrow gap angle (asm 0x180020c90-0x180020cce) */
    if (TOL < gg && gg < 0.25) g = gg;
    uint8_t *b0 = W0->b, *b1 = W1->b;
    PD(b0, 0x20, v[0]); PD(b0, 0x28, v[1]); PD(b0, 0x50, 0); PD(b0, 0x48, 0); PD(b0, 0x38, R); PD(b0, 0x40, aA);
    PD(b1, 0x20, v[0]); PD(b1, 0x28, v[1]); PD(b1, 0x48, 0); PD(b1, 0x50, 0); PD(b1, 0x38, R); PD(b1, 0x40, aA);
    double eA = f2470(box, v, rA, S->ldr_vis, R, ca, 1), eB = f2470(box, v, rB, S->ldr_vis, R, ca, 0);
    int pig = in->pigtail != 0;
    double s0 = D(b0, 0x40), w0 = D(b0, 0x48), x0 = D(b0, 0x50), s1 = D(b1, 0x40), w1 = D(b1, 0x48), x1 = D(b1, 0x50);
    /* the branch table, asm 0x180020d94-0x180021353 (s/w/x = start +0x40, sweep +0x48, +0x50 of arc 0 / arc 1) */
    if (f->d71) {
        if (f->d6a) {
            if (f->d6c) {
                s0 = aA;
                if (f->d6f) { w0 = wrap0(eA - aA); s1 = eB; w1 = wrap0(wrap2(aA + sweep) - eB); }
                else {
                    w0 = half; s1 = mid; w1 = w0;
                    goto e1e;
                }
            } else {                                                /* 0x180020e62 */
                s1 = wrap0(aA - g); w1 = g;
                double vv = wrap2(aA + sweep);
                w0 = g; s0 = vv;
                if (pig) {
                    if (f->d6f) {
                        if (nearA) { w1 = f300(eA, s1); x1 = f300(ra, eA); }
                        else { double t = wrap2(vv + g); s0 = eB; w0 = f300(eB, t); x0 = f300(aB, eB); }
                    } else {
                        if (nearA) { s1 = eB; w1 = f300(aA, eB); }
                        else w0 = f300(eA, vv);
                    }
                }
            }
        } else {                                                    /* 0x180020f55 */
            if (f->d6c) {
                w0 = half; s0 = aA; s1 = mid; w1 = w0;
                if (!f->d6f) goto e1e;
            } else {                                                /* 0x180020fb0 */
                s1 = wrap0(aA - g); w1 = f300(mid, s1); x1 = half; s0 = mid; w0 = w1; x0 = half;
                if (pig && !f->d6f) {
                    if (!nearA) w0 = f300(eA, mid);
                    else { s1 = eB; w1 = f300(eB, mid); }
                }
            }
        }
        goto done;
    e1e:                                                            /* 0x180020e1e (xmm0 = eB here) */
        if (pig) {
            if (!nearA) { w1 = f300(eA, mid); x1 = f300(aB, eA); }
            else { s0 = eB; w0 = f300(eB, mid); x0 = f300(ra, eB); }
        }
    } else {                                                        /* 0x180021039 */
        if (f->d6a) {
            if (f->d6c) {
                s0 = aB;
                if (f->d6f) { w0 = f3c0(aB, eA); s1 = eB; w1 = f3c0(eB, aA); }
                else {
                    w0 = f3c0(aB, midp); s1 = midp; w1 = w0;
                    if (pig) {
                        if (nearA) { x1 = f3c0(ra, eA); w1 = x1 + w1; }
                        else { s0 = eB; x0 = f3c0(eB, aB); w0 = x0 + w0; }
                    }
                }
            } else {                                                /* 0x180021113 */
                s1 = wrap0(wrap2(aA + sweep) - g);
                if (f->d6f) { w1 = f3c0(s1, eA); x1 = w1 - g; s0 = eB; w0 = f3c0(eB, aA) + g; x0 = w0 - g; }
                else goto p12ac;
            }
        } else {                                                    /* 0x18002118a */
            if (f->d6c) {
                s0 = aB;
                if (f->d6f) { w0 = f3c0(aB, ca); s1 = ca; w1 = f3c0(ca, aA); }
                else {
                    w0 = f3c0(aB, midp); s1 = midp; w1 = w0;
                    if (pig) {
                        if (nearA) { x1 = f3c0(ra, eA); w1 = x1 + w1; }
                        else { s0 = eB; x0 = f3c0(eB, aB); w0 = x0 + w0; }
                    }
                }
            } else {                                                /* 0x180021235 */
                s1 = wrap0(wrap2(aA + sweep) - g);
                if (f->d6f) { w1 = f3c0(s1, ca); x1 = w1 - g; s0 = ca; w0 = f3c0(ca, aA) + g; x0 = w0 - g; }
                else goto p12ac;
            }
        }
        goto done;
    p12ac:                                                          /* 0x1800212ac */
        w1 = f3c0(s1, midp); x1 = w1 - g; s0 = midp; w0 = w1; x0 = x1;
        if (pig) {
            if (nearA) { double t = f3c0(ra, eA); w0 = (w0 - g) + t; x0 = w0 - t; }
            else { s1 = eB; double t = f3c0(eB, aB); w1 = (w1 - g) + t; x1 = w1 - t; }
        }
    }
done:
    restrict_ang(0.0, 6.283184307179586, &s0); restrict_ang(0.0, 6.283184307179586, &w0);   /* 0x180cf5568 */
    restrict_ang(0.0, 6.283184307179586, &s1); restrict_ang(0.0, 6.283184307179586, &w1);
    if (w0 < 1e-06) w0 = 2e-06;                                     /* 0x3ec0c6f7a0b5ed8d */
    if (w1 < 1e-06) w1 = 2e-06;
    PD(b0, 0x40, s0); PD(b0, 0x48, w0); PD(b0, 0x50, x0); PD(b1, 0x40, s1); PD(b1, 0x48, w1); PD(b1, 0x50, x1);
    witness(f, in, S, v, R, aA, rA, f->d74, W2);
    witness(f, in, S, v, R, aA + sweep, rB, f->d8, W3);
    double z = cur[2];
    W2->pts[0][2] = W2->pts[1][2] = z; W3->pts[0][2] = W3->pts[1][2] = z; PD(b1, 0x30, z); PD(b0, 0x30, z);
    *npts(W0) = *npts(W1) = *npts(W2) = *npts(W3) = 2;
    W1->b[2] = 1; W0->b[2] = W2->b[2] = W3->b[2] = 0;
    int wv = S->wit_vis, lv = S->ldr_vis;
    W2->b[3] = (((wv - 1) & 0xfffd) == 0); W3->b[3] = (unsigned short)(wv - 2) <= 1;
    if (f->d6a) {
        W0->b[3] = (((lv - 1) & 0xfffd) == 0); W1->b[3] = (unsigned short)(lv - 2) <= 1;
        if (f->d70 && f->d6e) W0->b[3] = W1->b[3] = 1;
    } else W0->b[3] = W1->b[3] = 1;
    return 1;
}

/* FUN_180022d40: the record (core_dimlayout.c 789-1034) */
static void f22d40(Ctx *X, const DcAngInput *in, const Fl *f, uint8_t *E, double sweep, const double *cur, double tang,
                   const double *ws, const double *hs, int nstr, WL *W0, WL *W1, WL *W2, WL *W3) {
    const DcSettings *S = X->S;
    int lv = S->ldr_vis, wv = S->wit_vis;
    E[0x7c9] = (uint8_t)S->auto_align;                              /* auto_alignment: the caller passes the possibly snapped value */
    E[0x308] = 1; PD(E, 0x310, cur[0]); PD(E, 0x318, cur[1]); P16(E, 0x328, in->view); PD(E, 0x320, cur[2]); PD(E, 0x330, S->scale);
    PD(E, 0x338, f->d70 ? TWOPI - sweep : sweep);
    E[0x7cb] = (uint8_t)S->comma; E[0x7cc] = (uint8_t)S->lead0; E[0x7cf] = (uint8_t)S->trail0; E[0x7cd] = (uint8_t)in->digits;
    E[0x7d0] = (uint8_t)S->constr3d; E[0x7ca] = 0;
    P16(E, 0x7ac, wv & 0xff); P16(E, 0x7ae, lv & 0xff); P32(E, 0x7a2, 0x10001); P32(E, 0x7a6, 0x10001);
    if (!f->d69) { W0->b[3] = (((lv - 1) & 0xfffd) == 0); W1->b[3] = lv == 2 || lv == 3; }
    else W0->b[3] = W1->b[3] = 1;
    int has0 = !f->d69 || ((lv - 1) & 0xfffd) == 0, has1 = !f->d69 || lv == 2 || lv == 3;
    P16(E, 0x7b2, 0); P16(E, 0x7c0, in->ang_fmt & 0xff); P16(E, 0x7b0, f->d6b); P16(E, 0x7c2, S->frac_units & 0xff); P16(E, 0x7b4, f->d69);
    P16(E, 0x7b6, in->tol_mode);                                    /* mode 1 -> 3 with 2 strings: tolerance modes NOT PORTED (0 only) */
    PD(E, 0x340, in->pos_tol); PD(E, 0x348, in->neg_tol); E[0x7c7] = (uint8_t)in->din_chr; E[0x7c6] = (uint8_t)in->din_tol; E[0x7c8] = (uint8_t)in->din_val;   /* FUN_1802579a0 */
    P16(E, 0x7b8, 0);
    make_wl_single(W2, 0, 0, S->arrow_type, S->arrow_h, S->arrow_w); memcpy(E + 0x378, W2->b, 0x98);
    make_wl_single(W0, 2, has0, S->arrow_type, S->arrow_h, S->arrow_w); memcpy(E + 0x4a8, W0->b, 0x98);
    /* FUN_180251460 slot 0 (tolerance mode 0): x = T.x - bx + w0 / 2 at T.y, rotated about T by the text angle */
    double tbx, tby, px, py; box_limits(in->tol_mode, nstr, D(X->fa, 0x40) * D(X->fa, 0x28), hs, ws, &tbx, &tby);
    rot((cur[0] - tbx) + ws[0] * 0.5, cur[1], cur[0], cur[1], cos(tang), sin(tang), &px, &py);
    slot_font(E + 0x5d8, X->fa, tang); PD(E, 0x638, px); PD(E, 0x640, py); PD(E, 0x648, cur[2]);
    memset(E + 0x650, 0, 0x58); { size_t n = strlen(X->text); memcpy(E + 0x650, X->text, n < 0x57 ? n : 0x57); } memset(E + 0x790, 0, 8);
    make_wl_single(W1, 2, has1, S->arrow_type, S->arrow_h, S->arrow_w); memcpy(E + 0x540, W1->b, 0x98);
    E[0x542] = 1;
    make_wl_single(W3, 0, 0, S->arrow_type, S->arrow_h, S->arrow_w); memcpy(E + 0x410, W3->b, 0x98);
}

/* FUN_180020470 (core_calcdim.c 3487): the quadrant code of the cursor (+0x2ea) and its ratio along the angle (+0x2f0) */
static void f0470(uint8_t *E, const DcAngInput *in, const double *cur, const double *v) {
    int code = 0xc, q = 0;
    if (in->vl.saved) q = dc_ang_quad(v, cur, in->vl.qangles, in->vl.qcodes, &code);
    if (!in->vl.saved || q == 0) { E[0x2ea] = 0xc; E[0x7c9] = 1; PD(E, 0x2f0, 0.5); return; }
    const double (*L)[6] = in->lines; const double *e0, *e1;
    if (q == 1) { e0 = L[0]; e1 = L[1]; } else if (q == 2) { e0 = L[1]; e1 = L[0] + 3; } else if (q == 3) { e0 = L[0] + 3; e1 = L[1] + 3; } else { e0 = L[1] + 3; e1 = L[0]; }
    double a1 = abl(v[0], v[1], e0[0], e0[1], v[0], v[1], e1[0], e1[1]), a2 = abl(v[0], v[1], cur[0], cur[1], v[0], v[1], e0[0], e0[1]);
    E[0x2ea] = (uint8_t)code; PD(E, 0x2f0, a2 / a1);
}

/* FUN_1800226b0 (asm 0x1800226b0-0x180022cfa) */
static int f26b0(Ctx *X, const DcAngInput *in, uint8_t *E, double *cur, double *vtx, double *Rout) {
    const DcSettings *S = X->S;
    Fl f; memset(&f, 0, sizeof f);
    f.d6d = S->text_orient != 0; f.d69 = S->ldr_style == 1; f.d6a = !f.d69; f.d6b = S->arrows_out == 1; f.d6c = !f.d6b;
    f.d71 = in->flip == 0; f.d70 = in->flip != 0;
    double l0[4] = { in->lines[0][0], in->lines[0][1], in->lines[0][3], in->lines[0][4] }, l1[4] = { in->lines[1][0], in->lines[1][1], in->lines[1][3], in->lines[1][4] }, t1, t2;
    if (!l_ints_t(l0, l1, vtx, &t1, &t2)) return 0;
    double rA[4] = { vtx[0], vtx[1] }, rB[4] = { vtx[0], vtx[1] };
    switch (in->quad) {                                             /* asm 0x1800227dd-0x180022870 */
    case 1: rA[2] = l0[0]; rA[3] = l0[1]; rB[2] = l1[0]; rB[3] = l1[1]; f.d74 = 0; f.d8 = 1; break;
    case 2: rA[2] = l1[0]; rA[3] = l1[1]; rB[2] = l0[2]; rB[3] = l0[3]; f.d74 = 1; f.d8 = 0; break;
    case 3: rA[2] = l0[2]; rA[3] = l0[3]; rB[2] = l1[2]; rB[3] = l1[3]; f.d74 = 0; f.d8 = 1; break;
    case 4: rA[2] = l1[2]; rA[3] = l1[3]; rB[2] = l0[0]; rB[3] = l0[1]; f.d74 = 1; f.d8 = 0; break;
    default: return 0;
    }
    double R = hypot(vtx[0] - cur[0], vtx[1] - cur[1]), aA = lang(rA), sweep = wrap0(lang(rB) - aA);   /* FUN_180021e70 */
    if (!(1e-06 < fabs(sweep))) return 0;
    double lc[4] = { vtx[0], vtx[1], cur[0], cur[1] }, ca = lang(lc), aB = lang(rB);
    f.d6f = between(aA, ca, aB);
    if (f.d70) f.d6f = !f.d6f;
    f.d6e = !f.d6f;
    f.d73 = in->vl.saved;
    DcSettings S2 = *S; X->S = &S2;                                 /* auto_alignment is saved / restored around the call */
    if (!S2.auto_align) {
        if (!in->ctrl) {                                            /* FUN_180021f00 (ang_core_a1.c 1): the centring snap on the bisector */
            double a1 = aA, a2 = aB; if (f.d70) { a1 += PI; a2 += PI; }
            double p1[2], p2[2]; rot(100.0, 0.0, vtx[0], vtx[1], cos(a1), sin(a1), &p1[0], &p1[1]); rot(100.0, 0.0, vtx[0], vtx[1], cos(a2), sin(a2), &p2[0], &p2[1]);
            double ln[4] = { vtx[0], vtx[1], (p2[0] + p1[0]) * 0.5, (p2[1] + p1[1]) * 0.5 }, ft[2], t;
            if (proj_pt_ln(cur, ln, ft, &t) && 0.0 <= t) {
                double d = hypot(ft[0] - cur[0], ft[1] - cur[1]), m = S2.sel_dist <= in->text_h ? S2.sel_dist : in->text_h;
                if (fabs(d) < m * 0.5) S2.auto_align = 1;
            }
        }
    }
    if (S2.auto_align) {
        double a = wrap2(sweep * 0.5 + aA); if (f.d70) a += PI;
        cur[0] = cos(a) * R + vtx[0]; cur[1] = sin(a) * R + vtx[1];
        f.d6e = 0; f.d6f = 1;
    }
    WL W0, W1, W2, W3; memset(&W0, 0, sizeof W0); memset(&W1, 0, sizeof W1); memset(&W2, 0, sizeof W2); memset(&W3, 0, sizeof W3);
    double tang = 0, ws[3], hs[3]; int nstr = 1;
    int r = f06f0(X, in, &f, vtx, rA, rB, R, aA, sweep, cur, &W0, &W1, &W2, &W3, &tang, ws, hs, &nstr);
    if (r <= 0) { X->S = S; return r; }
    f22d40(X, in, &f, E, sweep, cur, tang, ws, hs, nstr, &W0, &W1, &W2, &W3);
    X->S = S;
    *Rout = R;
    return 1;
}

DcResult dc_calc_ang(uint8_t E[0x810], const DcAngInput *in, const DcSettings *S, const DcFonts *F) {
    DcResult R; memset(&R, 0, sizeof R);
    if (in->tol_mode != 0) { R.not_ported = 1; R.why = "angular tolerance mode 1-3 (NOT PORTED)"; return R; }
    if (!in->view) { R.not_ported = 1; R.why = "view 0 (get_gr_view_n, NOT PORTED)"; return R; }
    DcInput li; memset(&li, 0, sizeof li); li.tmpl = in->tmpl; li.view = in->view; memcpy(li.vm, in->vm, sizeof li.vm); li.ctrl = in->ctrl;
    Ctx X; memset(&X, 0, sizeof X); X.S = S; X.F = F; X.in = &li; memcpy(X.fa, S->fa, 0x60);
    P3 T; w2v(in->vm, in->cursor, &T);
    double cur[3] = { T.x, T.y, T.z };
    if (S->constr3d) {                                              /* core_calcdim.c 87-98: the 4 aPoints at one depth -> that depth */
        double z[4]; for (int i = 0; i < 4; i++) { double p[3]; w2v3(in->vm, in->ap[i], p); z[i] = p[2]; }
        if (fabs(z[0] - z[2]) < TOL && fabs(z[2] - z[1]) < TOL && fabs(z[1] - z[3]) < TOL && fabs(z[3] - z[0]) < TOL) cur[2] = z[0];
    }
    P32(E, 0, 0x40); memset(E + 0x20, 0, 8); E[6] = (uint8_t)S->color; P32(E, 8, (uint32_t)S->level); memcpy(E + 0x10, S->attrib, 16); P16(E, 4, 1);
    E[0x308] = 1; PD(E, 0x310, cur[0]); PD(E, 0x318, cur[1]); PD(E, 0x320, cur[2]); P16(E, 0x328, in->view); PD(E, 0x330, S->scale);
    E[0x7cb] = (uint8_t)S->comma; E[0x7cc] = (uint8_t)S->lead0; E[0x7cf] = (uint8_t)S->trail0; E[0x7cd] = (uint8_t)in->digits;
    PD(E, 0x340, in->pos_tol); PD(E, 0x348, in->neg_tol); PD(E, 0x350, S->gap); PD(E, 0x358, S->ext);
    P16(E, 0x7ba, in->metric); E[0x7d0] = (uint8_t)S->constr3d;
    double vtx[2], Rad = 0;
    int r = f26b0(&X, in, E, cur, vtx, &Rad);
    if (r < 0) { R.not_ported = 1; R.why = X.why; return R; }
    if (r == 0) { R.why = X.why ? X.why : "no angle (parallel lines / zero sweep / fonts)"; return R; }
    E[0x7ca] = 0;
    for (int i = 0; i < 4; i++) memcpy(E + 0x68 + 0x98 * i, in->ap[i], 0x98);
    PD(E, 0x2c8, vtx[0]); PD(E, 0x2d0, vtx[1]); PD(E, 0x2d8, cur[2]); PD(E, 0x2e0, Rad);
    E[0x2e8] = (uint8_t)dc_ang_quad_code(in->quad, in->vl.qcodes); /* FUN_18023f360(param_3, &qcodes) */
    E[0x2e9] = (uint8_t)in->flip; E[0x2ea] = 0; PD(E, 0x2f0, 0.5); E[0x2f8] = (uint8_t)in->pigtail;
    double pl[3];                                                   /* FUN_18023d590 case 1: placement, offsets from body+0x260 */
    if (!placement(&X, E, pl)) { R.why = "get_ext failed (fonts)"; return R; }
    PD(E, 0x310, pl[0]); PD(E, 0x318, pl[1]); PD(E, 0x320, pl[2]);
    P3 P = { vtx[0], vtx[1], cur[2] };
    offsets(&X, E, &P, 0.0);
    f0470(E, in, cur, vtx);
    R.valid = 1;                                                    /* DraftIsAssoc: NOT PORTED */
    { double th, tw; if (!get_ext(F, (const char *)E + 0x650, E + 0x5d8, &th, &tw)) { R.text_w = tw; R.text_h = th; } }
    return R;
}
