/* mcspline_fit.c - Mastercam's spline fitting (mccore.dll), the numerics of the Wireframe spline commands:
 * make_spline_coefs_open / _closed (parametric cubic through nodes), convert_points_to_nurbs (cubic NURBS through nodes),
 * natural_tan, tan_3p_arc, pt_tan_to_sp_coefs, convert_coeffs_to_cpts, and the entity records put_spline_in_db / put_nurbs_in_db build.
 * No UI here. Spec: ../formats/wireframe.md "Wireframe spline commands (N9-16)". Sources: decompiles of mccore.dll (Ghidra project
 * ~/mastercam-re/ghidra_core, cited by function name; decomp/surfloft/loft3.c make_spline_from_pts). Oracle: tools/tests/spline_fit_oracle.sh
 * (the real exports under tools/peloader). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcspline_fit.h"
#include "mcsurf.h"

static double norm3(const double v[3]) { return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static void normalize3(double v[3]) {          /* FUN_18000f2f0: unit vector, 0 stays 0 */
    double l = norm3(v);
    if (l == 0.0) { v[0] = v[1] = v[2] = 0.0; return; }
    v[0] /= l; v[1] /= l; v[2] /= l;
}
static double dist3(const double a[3], const double b[3]) {
    double d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }; return sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

/* FUN_1806630b0 (mccore 0x1806630b0, called by make_spline_coefs_* / natural_tan): t[i] = |Q[i] - Q[i-1]|, i = 1 .. n-1 (t[0] untouched:
   CSplineNodeMgr::get_val_from_spline_ts); a chord below tol: post_warning 0x16ee (mccoreres 5870) and failure */
int mcsf_chords(const double (*Q)[3], int n, double tol, double *t) {
    for (int i = 1; i < n; i++) { t[i] = dist3(Q[i - 1], Q[i]); if (t[i] < tol) return 0; }
    return 1;
}

/* pt_tan_to_sp_coefs (mccore 0x180665510): one segment on [0, 1] (power basis, ascending, 3 per coefficient, 12 doubles) from the end
   points p0, p1 and the end tangents d0, d1 scaled by h (the span length; h = 0: 1): a0 = p0, a1 = d0 h, a2 = 3 (p1 - p0) - (2 d0 + d1) h,
   a3 = 2 (p0 - p1) + (d0 + d1) h (local_148 = the Hermite basis, dVar25 = 1 / h, and the rescale by h, h^2, h^3 of the degree 1 .. 3 terms) */
void mcsf_pt_tan_coefs(const double p0[3], const double p1[3], const double d0[3], const double d1[3], double h, double out[12]) {
    if (h == 0.0) h = 1.0;
    for (int k = 0; k < 3; k++) {
        double m0 = d0[k], m1 = d1[k];
        double a2 = (p1[k] - p0[k]) * (3.0 / (h * h)) + m0 * (-2.0 / h) + m1 * (-1.0 / h);
        double a3 = (p0[k] - p1[k]) * (2.0 / (h * h * h)) + (m0 + m1) / (h * h);
        out[k] = p0[k]; out[3 + k] = m0 * h; out[6 + k] = a2 * h * h; out[9 + k] = a3 * h * h * h;
    }
}

/* make_spline_coefs_open (mccore 0x180664360): end0 / end1 the end condition codes (param_5 / param_7): 0 natural, 1 quadratic "from 3
   points" (the -1.5 / 2 / -0.5 estimate, falling back to the chord when it opposes the first chord: tol), 2, 3 = a given tangent
   t0 / t1 (unit) used as it stands. Interior rows and pivots as in the decompile (the 1e-6 skips kept). c = (n-1) segments of 12 doubles. */
int mcsf_coefs_open(double *c, const double (*Q)[3], int n, int end0, const double t0[3], int end1, const double t1[3], double tol) {
    int m = n - 1;
    double *t = malloc(sizeof(double) * n), (*D)[3] = calloc((size_t)n, 24), (*row)[3] = calloc((size_t)n, 24);
    int ok = t && D && row && mcsf_chords(Q, n, tol, t);
    if (ok) {
        double dq[3], a;
        if (end0 >= 1 && end0 <= 3) {
            row[0][0] = 0; row[0][1] = 1; row[0][2] = 0;
            if (end0 != 1) memcpy(D[0], t0, 24);
            else if (n > 2) {
                for (int k = 0; k < 3; k++) { dq[k] = Q[1][k] - Q[0][k]; D[0][k] = Q[0][k] * -1.5 + Q[1][k] * 2.0 + Q[2][k] * -0.5; }
                if (D[0][0] * dq[0] + D[0][1] * dq[1] + D[0][2] * dq[2] >= tol) normalize3(D[0]);
                else { for (int k = 0; k < 3; k++) D[0][k] = dq[k]; normalize3(D[0]); }
            } else { for (int k = 0; k < 3; k++) D[0][k] = Q[1][k] - Q[0][k]; normalize3(D[0]); }
        } else {                                    /* natural */
            row[0][0] = 0; row[0][1] = 2; row[0][2] = 1;
            a = 3.0 / t[1]; for (int k = 0; k < 3; k++) D[0][k] = a * Q[1][k] + -a * Q[0][k];
        }
        if (end1 >= 1 && end1 <= 3) {
            row[m][0] = 0; row[m][1] = 1; row[m][2] = 0;
            if (end1 != 1) memcpy(D[m], t1, 24);
            else if (n > 2) {
                for (int k = 0; k < 3; k++) { dq[k] = Q[m][k] - Q[m - 1][k]; D[m][k] = Q[m][k] * 1.5 + Q[m - 1][k] * -2.0 + Q[m - 2][k] * 0.5; }
                if (D[m][0] * dq[0] + D[m][1] * dq[1] + D[m][2] * dq[2] >= tol) normalize3(D[m]);
                else { for (int k = 0; k < 3; k++) D[m][k] = dq[k]; normalize3(D[m]); }
            } else { for (int k = 0; k < 3; k++) D[m][k] = Q[m][k] - Q[m - 1][k]; normalize3(D[m]); }
        } else {
            row[m][0] = 1; row[m][1] = 2; row[m][2] = 0;
            a = 3.0 / t[m]; for (int k = 0; k < 3; k++) D[m][k] = a * Q[m][k] + -a * Q[m - 1][k];
        }
        for (int i = 1; i <= n - 2; i++) {
            double ta = t[i], tb = t[i + 1], r = tb * 3.0 / ta, s = ta * 3.0 / tb, cf[3] = { -r, r - s, s };
            row[i][0] = tb; row[i][1] = (tb + ta) * 2.0; row[i][2] = ta;
            for (int k = 0; k < 3; k++) D[i][k] = cf[0] * Q[i - 1][k] + cf[1] * Q[i][k] + cf[2] * Q[i + 1][k];
        }
        for (int i = m - 1; i >= 0; i--) if (fabs(row[i + 1][1]) > 1e-6) {
            double f = row[i][2] / row[i + 1][1];
            row[i][1] += -f * row[i + 1][0]; for (int k = 0; k < 3; k++) D[i][k] += -f * D[i + 1][k];
        }
        for (int i = 1; i <= m; i++) if (fabs(row[i - 1][1]) > 1e-6) {
            double f = row[i][0] / row[i - 1][1];
            for (int k = 0; k < 3; k++) D[i][k] += -f * D[i - 1][k];
        }
        for (int i = 0; i <= m; i++) {
            if (fabs(row[i][1]) <= 1e-6) { D[i][0] = D[i][1] = D[i][2] = 0.0; }
            else { double inv = 1.0 / row[i][1]; for (int k = 0; k < 3; k++) D[i][k] = inv * D[i][k]; }
        }
        for (int i = 0; i <= m - 1; i++) mcsf_pt_tan_coefs(Q[i], Q[i + 1], D[i], D[i + 1], t[i + 1], c + i * 12);
    }
    free(t); free(D); free(row);
    return ok;
}

/* make_spline_coefs_closed (mccore 0x1806633a0), the elimination of a cyclic tridiagonal system (m = n - 2, nodes 0 .. m, node n-1 = node 0):
   rows (tb, 2 (ta + tb), ta) with the chords wrapped (t[0] of the row 0 = t[n-1]), the column-m element of a row in its slot 0 (row 0's
   left neighbour) or slot 2 (row m-1) */
int mcsf_coefs_closed(double *c, const double (*Q)[3], int n, double tol) {
    int m = n - 2;
    double *t = malloc(sizeof(double) * n), (*D)[3] = calloc((size_t)n, 24), (*row)[3] = calloc((size_t)n, 24);
    int ok = t && D && row && mcsf_chords(Q, n, tol, t);
    if (ok) {
        int last = n - 1;
        for (int i = 0; i <= m; i++) {
            double ta = t[i < 1 ? last : i], tb = t[i + 1 > last ? 1 : i + 1], r = tb * 3.0 / ta, s = ta * 3.0 / tb, cf[3] = { -r, r - s, s };
            row[i][0] = tb; row[i][1] = (tb + ta) * 2.0; row[i][2] = ta;
            for (int a = 0; a < 3; a++) {
                int j = i - 1 + a; if (j < 0) j = m; else if (j > m) j = 0;
                for (int k = 0; k < 3; k++) D[i][k] += cf[a] * Q[j][k];
            }
        }
        for (int i = 1; i <= m; i++) if (fabs(row[i - 1][1]) > 1e-6) {
            double f = row[i][0] / row[i - 1][1];
            if (i == m) row[i][0] = 0.0;
            else if (i == n - 3) { row[i][0] = 0.0; row[i][2] += -f * row[i - 1][0]; }
            else row[i][0] = -f * row[i - 1][0];
            row[i][1] += -f * row[i - 1][2];
            for (int k = 0; k < 3; k++) D[i][k] += -f * D[i - 1][k];
        }
        for (int u = n - 3; u >= 1; u--) if (fabs(row[u][1]) > 1e-6) {
            double f = row[u - 1][2] / row[u][1], a = u == n - 3 ? -f * row[u][2] : -f * row[u][0];
            row[u - 1][0] += a; row[u - 1][2] = 0.0;
            for (int k = 0; k < 3; k++) D[u - 1][k] += -f * D[u][k];
        }
        if (fabs(row[0][1]) > 1e-6) {
            double f = row[m][2] / row[0][1];
            row[m][1] += -f * row[0][0];
            for (int k = 0; k < 3; k++) D[m][k] += -f * D[0][k];
        }
        double p = row[m][1];
        if (fabs(p) > 1e-6) for (int r = m; r >= 1; r--) {
            double co = r == m ? row[r - 1][2] : row[r - 1][0], f = co / p;
            for (int k = 0; k < 3; k++) D[r - 1][k] += -f * D[m][k];
        }
        for (int i = 0; i <= m; i++) {
            if (fabs(row[i][1]) <= 1e-6) D[i][0] = D[i][1] = D[i][2] = 0.0;
            else { double inv = 1.0 / row[i][1]; for (int k = 0; k < 3; k++) D[i][k] = inv * D[i][k]; }
        }
        memcpy(D[last], D[0], 24);
        for (int i = 0; i <= m; i++) mcsf_pt_tan_coefs(Q[i], Q[i + 1], D[i], D[i + 1], t[i + 1], c + i * 12);
    }
    free(t); free(D); free(row);
    return ok;
}

/* natural_tan (mccore 0x180661240): the unit natural end tangent of the cubic through Q[0 .. n-1] (n <= 5), start end when start != 0:
   rows (0, 2, 1) / (t_{i+1}, 2 (t_i + t_{i+1}), t_i) / (1, 2, 0) with the same right sides as make_spline_coefs_open; ok = 0 when a chord < tol
   (natural_tan's param_5); the result is the zero vector when the pivot is <= 1e-6 (*param_3 = v_z_3d) */
void mcsf_natural_tan(const double (*Q)[3], int start, double out[3], int n, double tol, int *ok) {
    double t[6], row[5][3], rhs[5][3];
    int m = n - 1;
    *ok = mcsf_chords(Q, n, tol, t);
    if (!*ok) return;
    row[0][0] = 0; row[0][1] = 2; row[0][2] = 1; { double a = 3.0 / t[1]; for (int k = 0; k < 3; k++) rhs[0][k] = a * Q[1][k] + -a * Q[0][k]; }
    row[m][0] = 1; row[m][1] = 2; row[m][2] = 0; { double a = 3.0 / t[m]; for (int k = 0; k < 3; k++) rhs[m][k] = a * Q[m][k] + -a * Q[m - 1][k]; }
    for (int i = 1; i <= m - 1; i++) {
        double ta = t[i], tb = t[i + 1], r = tb * 3.0 / ta, s = ta * 3.0 / tb, cf[3] = { -r, r - s, s };
        row[i][0] = tb; row[i][1] = (tb + ta) * 2.0; row[i][2] = ta;
        for (int k = 0; k < 3; k++) rhs[i][k] = cf[0] * Q[i - 1][k] + cf[1] * Q[i][k] + cf[2] * Q[i + 1][k];
    }
    for (int i = m - 1; i >= 0; i--) if (fabs(row[i + 1][1]) > 1e-6) {
        double f = row[i][2] / row[i + 1][1];
        row[i][1] += -f * row[i + 1][0]; for (int k = 0; k < 3; k++) rhs[i][k] += -f * rhs[i + 1][k];
    }
    int si = 0;
    if (!start) {
        for (int i = 0; i < m; i++) if (fabs(row[i][1]) > 1e-6) {
            double f = row[i + 1][0] / row[i][1];
            for (int k = 0; k < 3; k++) rhs[i + 1][k] += -f * rhs[i][k];
        }
        si = m;
    }
    if (fabs(row[si][1]) > 1e-6) { memcpy(out, rhs[si], 24); normalize3(out); }
    else out[0] = out[1] = out[2] = 0.0;
}

/* tan_3p_arc (mccore 0x180661bd0) with make_view_k (0x180017080), cr_a_3p (0x1804b5c90): the end tangent at node `a` (param_4 = start 1: at a,
   else at c) of the arc through a, b, c; 0 when the three points are collinear or a pair is closer than tol (make_view_k fails: the chord
   a -> b, end: b -> c, normalized); the arc's tangent direction follows its sweep sign (tan_3p_arc l.0x661fa0: 0.0 <= sweep turned) */
int mcsf_tan_3p_arc(const double a[3], const double b[3], const double c[3], int start, double out[3], double tol) {
    double ac[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] }, bc[3] = { c[0] - b[0], c[1] - b[1], c[2] - b[2] }, ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    if (norm3(ac) < tol || norm3(bc) < tol || norm3(ab) < tol) return 0;
    double N[3] = { ac[2] * bc[1] - ac[1] * bc[2] != 0 ? 0 : 0, 0, 0 };   /* n = bc x ac */
    N[0] = bc[1] * ac[2] - bc[2] * ac[1]; N[1] = bc[2] * ac[0] - bc[0] * ac[2]; N[2] = bc[0] * ac[1] - bc[1] * ac[0];
    double n[3] = { N[0], N[1], N[2] }; normalize3(n);
    double ok = 0; ok = norm3(n);
    if (!(ok >= 0.5)) {                               /* make_view_k: a zero normal, the three points in line */
        if (start) { for (int k = 0; k < 3; k++) out[k] = ab[k]; } else { for (int k = 0; k < 3; k++) out[k] = bc[k]; }
        normalize3(out);
        return norm3(out) >= tol ? 1 : 0;
    }
    /* make_view_k: the view rows x, y, z with z the unit normal; the x row = n x (the axis of the smallest |n| component, first of equals) ... */
    int sm = 0;
    if (fabs(n[1]) < fabs(n[0])) sm = 1;
    if (fabs(n[2]) < fabs(sm == 1 ? n[1] : n[0])) sm = 2;
    double aux[3] = { 0, 0, 0 }; aux[sm] = 1.0;
    double y[3] = { aux[1] * n[2] - aux[2] * n[1] < 0 ? 0 : 0, 0, 0 };    /* y row (local_b0) = aux x n ... */
    y[0] = aux[2] * n[1] - aux[1] * n[2]; y[1] = aux[0] * n[2] - aux[2] * n[0]; y[2] = aux[1] * n[0] - aux[0] * n[1];
    { double l = sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]); if (l == 0.0) y[0] = y[1] = y[2] = 0.0; else { y[0] /= l; y[1] /= l; y[2] /= l; } }
    double x[3] = { y[1] * n[2] - y[2] * n[1], y[2] * n[0] - y[0] * n[2], y[0] * n[1] - y[1] * n[0] };   /* x = y x n, normalized */
    { double l = norm3(x); if (l == 0.0) x[0] = x[1] = x[2] = 0.0; else { x[0] /= l; x[1] /= l; x[2] /= l; } }
    /* the three points in view coordinates (matrix rows x, y, z) */
    double v[3][2];
    const double *pts[3] = { a, b, c };
    for (int i = 0; i < 3; i++) { v[i][0] = x[0] * pts[i][0] + x[1] * pts[i][1] + x[2] * pts[i][2]; v[i][1] = y[0] * pts[i][0] + y[1] * pts[i][1] + y[2] * pts[i][2]; }
    /* cr_a_3p / center_3pa: the circle through the 2D points (chords below tol, or parallel bisectors: failure, the chord) */
    double ax = v[0][0], ay = v[0][1], bx = v[1][0], by = v[1][1], cx = v[2][0], cy = v[2][1];
    double d1x = bx - ax, d1y = by - ay, d2x = cx - bx, d2y = cy - by, l1 = hypot(d1x, d1y), l2 = hypot(d2x, d2y), l3 = hypot(ax - cx, ay - cy);
    double det = d1x * d2y - d1y * d2x;
    if (l1 < tol || l2 < tol || l3 < tol || det == 0.0) {   /* center_3pa fails: the chord (tan_3p_arc l.0x661f4f) */
        if (start) { for (int k = 0; k < 3; k++) out[k] = ab[k]; } else { for (int k = 0; k < 3; k++) out[k] = bc[k]; }
        normalize3(out);
        return norm3(out) >= tol ? 1 : 0;
    }
    /* center: the bisectors' intersection (l_ints of the two perpendicular bisectors) */
    double m1x = (ax + bx) * 0.5, m1y = (ay + by) * 0.5, m2x = (bx + cx) * 0.5, m2y = (by + cy) * 0.5;
    double s = ((m2x - m1x) * d2x + (m2y - m1y) * d2y) / (-d1y * d2x + d1x * d2y);
    double ccx = m1x + s * (-d1y), ccy = m1y + s * d1x;
    double pi2 = 6.283185307179586;
    double sa = atan2(ay - ccy, ax - ccx); if (sa < 0) sa += pi2;
    double ba = atan2(by - ccy, bx - ccx); if (ba < 0) ba += pi2;
    double ea = atan2(cy - ccy, cx - ccx); if (ea < 0) ea += pi2;
    /* find_sweep_angle(sa, ba, ea) */
    double d2 = ba - sa; while (d2 > pi2) d2 -= pi2; while (d2 < 0) d2 += pi2;
    double d3 = ea - sa; while (d3 > pi2) d3 -= pi2; while (d3 < 0) d3 += pi2;
    double sw = d3 <= d2 ? d3 - pi2 : d3;
    double px = start ? ax : cx, py = start ? ay : cy;
    double rx = px - ccx, ry = py - ccy, tx, ty;
    if (0.0 <= sw) { tx = -ry; ty = rx; } else { tx = ry; ty = -rx; }
    out[0] = tx * x[0] + ty * y[0]; out[1] = tx * x[1] + ty * y[1]; out[2] = tx * x[2] + ty * y[2];
    double l = norm3(out);
    if (l < tol) return 0;                         /* tan_3p_arc: |tan| < tol -> *param_6 = 0 */
    out[0] /= l; out[1] /= l; out[2] /= l;
    return 1;
}

/* ---- NURBS through points (convert_points_to_nurbs, mccore 0x180260e60, with FUN_18026c320 / FUN_18026b8e0 / FUN_18026b510) */
static void nb_knots(double *K, const double *P, int n) {   /* FUN_18026c320(3, 3, n, P, 3, K): 4 zero knots, chord-length, the last repeated 3 x */
    K[0] = K[1] = K[2] = K[3] = 0.0;
    int s = 4;
    for (int i = 1; i < n; i++, s++) {
        double d = 0;
        for (int k = 0; k < 3; k++) { double a = P[3 * i + k] - P[3 * (i - 1) + k]; d += a * a; }
        K[s] = K[s - 1] + sqrt(d);
    }
    for (int j = 0; j < 3; j++) K[s + j] = K[s - 1];
}

/* FUN_18026b8e0(n, end0, end1, closed, K + 3, a, b, c): the tridiagonal rows of the C2 interpolation (a sub, b diag, c super) */
static void nb_rows(int n, int end0, int end1, int closed, const double *K, double *a, double *b, double *c) {
    int e0 = (end0 >= 1 && end0 <= 3) || closed, e1 = (end1 >= 1 && end1 <= 3) || closed;
    if (n == 2) {
        a[0] = 0; c[1] = 0;
        if (!end0) { b[0] = 2.0; c[0] = -1.0; } else { b[0] = 1.0; c[0] = 0.0; }
        if (end1) { a[1] = 0; b[1] = 1.0; } else { a[1] = -1.0; b[1] = 2.0; }
        return;
    }
    if (n == 3) {
        double d1 = K[1] - K[0], d2 = K[2] - K[1], s = d1 + d2;
        a[0] = 0;
        if (e0) { b[0] = 1.0; c[0] = 0.0; } else { b[0] = (d1 * 2.0 + d2) / s; c[0] = -(d1 / s); }
        a[1] = (d2 * d2) / s; b[1] = (d2 * 2.0 * d1) / s; c[1] = (d1 * d1) / s;
        a[1] /= s; b[1] /= s; c[1] /= s;
        if (e1) { a[2] = 0; b[2] = 1.0; c[2] = 0; }
        else { a[2] = -(d2 / s); b[2] = (d2 * 2.0 + d1) / s; c[2] = 0.0; }
        return;
    }
    double d0 = K[1] - K[0], d1 = K[2] - K[1], d2 = K[3] - K[2], s = d0 + d1, S = d2 + s;
    if (e0) { a[0] = 0; b[0] = 1.0; c[0] = 0.0; }
    else { a[0] = 0; b[0] = (d0 * 2.0 + d1) / s; c[0] = -(d0 / s); }
    a[1] = (d1 * d1) / s; b[1] = ((d2 + d1) * d0) / S + (d1 * d0) / s; c[1] = (d0 * d0) / S;
    a[1] /= s; b[1] /= s; c[1] /= s;
    int m = n - 2;
    for (int i = 2; i < m; i++) {
        double h0 = K[i - 1] - K[i - 2], h1 = K[i] - K[i - 1], h2 = K[i + 1] - K[i], h3 = K[i + 2] - K[i + 1], w = h2 + h1, W0 = h0 + w, W1 = h3 + w;
        double p0 = (h2 * h2) / W0, p1 = ((h3 + h2) * h1) / W1 + ((h0 + h1) * h2) / W0, p2 = (h1 * h1) / W1;
        a[i] = p0 / w; b[i] = p1 / w; c[i] = p2 / w;
    }
    double e_d = K[m + 1] - K[m], e_c = K[m] - K[m - 1], e_b = K[m - 1] - K[m - 2], s2 = e_d + e_c, S2 = e_c + e_d + e_b;
    a[m] = (e_d * e_d) / S2; b[m] = ((e_b + e_c) * e_d) / S2 + (e_d * e_c) / s2; c[m] = (e_c * e_c) / s2;
    a[m] /= s2; b[m] /= s2; c[m] /= s2;
    int l = n - 1;
    if (e1) { a[l] = 0; b[l] = 1.0; }
    else { a[l] = -(e_d / s2); b[l] = (e_d * 2.0 + e_c) / s2; }
    c[l] = 0.0;
}

/* FUN_18026b510(n, 3, 3, a, b, c, gam, R, X): the Thomas algorithm (forward with gam, then back-substitution) for 3 coordinates. c (super) is the
   decompile's param_6 (the shifted diagonals: dVar16 = c[i-1] / bet ...), a the sub, b the diag */
static void nb_solve(int n, const double *a, const double *b, const double *c, double *gam, const double *R, double *X) {
    double bet = b[0];
    for (int k = 0; k < 3; k++) X[k] = R[k] / bet;
    for (int i = 1; i < n; i++) {
        gam[i] = c[i - 1] / bet;
        bet = b[i] - gam[i] * a[i];
        for (int k = 0; k < 3; k++) X[3 * i + k] = (R[3 * i + k] - X[3 * (i - 1) + k] * a[i]) / bet;
    }
    for (int i = n - 2; i >= 0; i--) for (int k = 0; k < 3; k++) X[3 * i + k] -= X[3 * (i + 1) + k] * gam[i + 1];
}

int mcsf_nurbs_fit(const double (*Q)[3], int n, int end0, const double t0[3], int end1, const double t1[3], double tol, McsfNb *o) {
    if (n < 2) return 0x1811;                       /* post_warning 0x1811 (mccoreres 6161) */
    for (int i = 1; i < n; i++) {
        double e = fabs(Q[i - 1][0] - Q[i][0]) + fabs(Q[i - 1][1] - Q[i][1]) + fabs(Q[i - 1][2] - Q[i][2]);
        if (e < tol) return 1;                       /* sVar12 = 1: the caller's warning 0x16ee */
    }
    int closed = fabs(Q[0][1] - Q[n - 1][1]) + fabs(Q[0][0] - Q[n - 1][0]) + fabs(Q[0][2] - Q[n - 1][2]) < tol;
    double *a = calloc((size_t)n, 8), *b = calloc((size_t)n, 8), *cc = calloc((size_t)n, 8), *gam = calloc((size_t)n, 8);
    double *P = malloc((size_t)n * 24), *K = calloc((size_t)n + 7, 8), *C = calloc((size_t)(n + 2) * 3, 8);
    if (!a || !b || !cc || !gam || !P || !K || !C) { free(a); free(b); free(cc); free(gam); free(P); free(K); free(C); return 8; }
    memcpy(P, Q, (size_t)n * 24);
    nb_knots(K, P, n);
    nb_rows(n, end0, end1, closed, K + 3, a, b, cc);
    memcpy(C, Q[0], 24);                              /* the first / last control point = the first / last node */
    memcpy(C + 3 * (n + 1), Q[n - 1], 24);
    double dt0[3], dt1[3]; memcpy(dt0, t0, 24); memcpy(dt1, t1, 24);
    if (closed && n > 3) {                            /* a closed curve's end tangents: the chord node 1 - node n-2 */
        double d[3] = { Q[1][0] - Q[n - 2][0], Q[1][1] - Q[n - 2][1], Q[1][2] - Q[n - 2][2] }, l = norm3(d);
        if (l == 0.0) d[0] = d[1] = d[2] = 0.0; else { d[0] /= l; d[1] /= l; d[2] /= l; }
        memcpy(dt0, d, 24); memcpy(dt1, d, 24);
    }
    if ((end0 >= 1 && end0 <= 3) || closed) {
        double f = (K[4] - K[3]) / 3.0;
        for (int k = 0; k < 3; k++) P[k] += dt0[k] * f;
    }
    if ((end1 >= 1 && end1 <= 3) || closed) {
        double f = (K[n + 2] - K[n + 1]) / 3.0;
        for (int k = 0; k < 3; k++) P[3 * (n - 1) + k] -= dt1[k] * f;
    }
    nb_solve(n, a, b, cc, gam, P, C + 3);
    o->ncp = n + 2; o->nk = n + 6; o->deg = 3; o->dim = 3; o->rational = 0; o->closed = closed; o->k = K; o->cp = C;
    free(a); free(b); free(cc); free(gam); free(P);
    return 0;
}

/* write a record field */
static void wd(uint8_t *p, double v) { memcpy(p, &v, 8); }
static void w32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }

/* the curvature limit +0xa8 of a parametric spline: put_spline_in_db (step 1 / order) / recalc_spline (step 0.1 / order): DAT_1812d9064 is 1 in .data, only FUN_180429de0 turns it off around one call; the largest sp_parm_curvature (0x180666210: |C' x C''| / |C'| of eval_spline with its flag 1 = derivatives x nseg, then x 1 / (t2 - t1); 0 when |C'| <= 1e-6) over t = 0 .. nseg, 1.0 when <= 1e-9 or order < 3 / dim != 3; at most 1 / tol */
void mcsf_spline_finish(uint8_t *r, uint32_t n, double step, double tol) {
    int32_t ns; memcpy(&ns, r + 0xb0, 4); int ord = r[0xb4], dim = r[0xb5]; double cmax = 0;
    CurveRec cr = { 0, 8, r, n }; double t1, t2; memcpy(&t1, r + 0x98, 8); memcpy(&t2, r + 0xa0, 8);
    if (ord < 3 || dim != 3) cmax = 1.0;
    else {
        double s = (double)ns / (t2 - t1);
        for (double t = 0; t <= (double)ns; t += step / (double)ord) {
            double d[3][3]; if (mcs_spline_eval_d(&cr, t, 2, d)) continue;
            double a[3] = { d[1][0] * s, d[1][1] * s, d[1][2] * s }, b[3] = { d[2][0] * s * s, d[2][1] * s * s, d[2][2] * s * s };
            double c[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }, la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), k = la <= 1e-6 ? 0 : fabs(sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) / la);
            if (cmax < k) cmax = k;
        }
        if (!(cmax >= 1e-9)) cmax = 1.0;
    }
    if (cmax > 1.0 / tol) cmax = 1.0 / tol;
    memcpy(r + 0xa8, &cmax, 8);
}

uint8_t *mcsf_spline_rec(const double *coef, int nseg, const double p0[3], const double p1[3], int closed, double tol, uint32_t *rn) {
    /* put_spline_in_db (mccore 0x1802daee0): the ent record of a parametric cubic (class 8, spline struct at +0x68): +0x68 start, +0x80 end, +0x98 t1 = 0,
       +0xa0 t2 = nseg, +0xa8 the curvature limit (1 / tol when the build's curvature option DAT_1812d9064 is off (see below)), +0xb0 nseg, +0xb4 order 4, +0xb5 dim 3, +0xb7
       closed, coefficients at +0xc0 (nseg x 12 doubles, [power][xyz]); +0xcd = 1 is set on put_special_undo's copy. */
    uint32_t n = 0xc0 + 8 * (uint32_t)nseg * 12;
    uint8_t *r = calloc(1, n); if (!r) return NULL;
    w32(r, 8);
    for (int k = 0; k < 3; k++) { wd(r + 0x68 + 8 * k, p0[k]); wd(r + 0x80 + 8 * k, p1[k]); }
    wd(r + 0x98, 0.0); wd(r + 0xa0, (double)nseg);
    w32(r + 0xb0, (uint32_t)nseg); r[0xb4] = 4; r[0xb5] = 3; r[0xb7] = (uint8_t)closed;
    memcpy(r + 0xc0, coef, 8 * (size_t)nseg * 12);
    wd(r + 0x98, 0.0); wd(r + 0xa0, (double)nseg);
    mcsf_spline_finish(r, n, 1.0, tol);
    *rn = n;
    return r;
}

/* DEVIATION approved by the user, N9-26 (formats/user_decisions.md): the parametric spline flattened in 2D construction. squash_spline (mccore 0x180288be0) keeps the record's fields (+0xa8 included), projects the end points with
   FUN_180288410 (P + n (depth - P.n)) and each segment's power 0 row with FUN_180288410, the higher powers with FUN_1802884c0 (V - n (V.n)); but its loop memcpy's the row to the output and projects a stack variable nothing fills
   (disassembly 0x180288d80-0x180288dc5), so Mastercam stores garbage. Here the row itself is projected. */
void mcsf_squash_spline(uint8_t *r, const double n[3], double depth) {
    int32_t ns; memcpy(&ns, r + 0xb0, 4); int ord = r[0xb4], dim = r[0xb5]; if (dim != 3) return;
    double p[3];
    for (int w = 0; w < 2; w++) { uint8_t *q = r + (w ? 0x80 : 0x68); memcpy(p, q, 24); double h = depth - (n[0] * p[0] + n[1] * p[1] + n[2] * p[2]); for (int k = 0; k < 3; k++) p[k] += n[k] * h; memcpy(q, p, 24); }
    uint8_t *c = r + 0xc0;
    for (int i = 0; i < ns; i++) for (int j = 0; j < ord; j++, c += 24) { memcpy(p, c, 24); double dv = n[0] * p[0] + n[1] * p[1] + n[2] * p[2], h = j == 0 ? depth - dv : -dv; for (int k = 0; k < 3; k++) p[k] += n[k] * h; memcpy(c, p, 24); }
}

/* nb_parm_curvature (mccore 0x1805b..; gaps/core2b.c, as mcsb_arc_nurbs_rec) evaluated on the raw non-rational cubic: |d1 x d2| / |d1|^3 scaled; 0 when |d1| <= 1e-6.
   The maximum is taken at t1 + i (t2 - t1) / (2 ncp - 2) below 1e9, at least 1e-9 (put_nurbs_in_db) */
uint8_t *mcsf_nurbs_rec(const McsfNb *o, double tol, uint32_t *rn) {
    (void)tol;
    int nk = o->nk, ncp = o->ncp, vpp = o->rational ? o->dim + 1 : o->dim, deg = o->deg;
    uint32_t n = 0xd8 + 8 * (uint32_t)(nk + vpp * ncp);
    uint8_t *r = calloc(1, n); if (!r) return NULL;
    w32(r, 0x80); w32(r + 0xc8, (uint32_t)nk); w32(r + 0xcc, (uint32_t)ncp);
    r[0xd0] = (uint8_t)deg; r[0xd1] = (uint8_t)vpp;
    /* flags: bit 2 (0x4) non-rational, bit 1 (0x2) closed (put_nurbs_in_db reads, convert_points_to_nurbs sets 0x303 / flags bVar5 * 2 | ... | 4) */
    r[0xd2] = (uint8_t)((o->closed ? 2 : 0) | (o->rational ? 0 : 4));
    memcpy(r + 0xd8, o->k, 8 * (size_t)nk);
    memcpy(r + 0xd8 + 8 * (size_t)nk, o->cp, 8 * (size_t)vpp * ncp);
    double t1 = o->k[deg], t2 = o->k[nk - 1 - deg];
    wd(r + 0x98, t1); wd(r + 0xa0, t2);
    const double *cp = o->cp;
    const double *P0 = cp, *P1 = cp + vpp * (ncp - 1);
    int off = o->rational ? 1 : 0;
    for (int k = 0; k < 3; k++) { wd(r + 0x68 + 8 * k, P0[off + k]); wd(r + 0x80 + 8 * k, P1[off + k]); }
    /* the curvature limit (put_nurbs_in_db, mcsb_arc_nurbs_rec's reading of nb_parm_curvature): the largest |d1 x d2| / |d1| with the derivatives x knot range / (t2 - t1) (0 when
       |d1| <= 1e-6) at t1 + i (t2 - t1) / (2 ncp - 2) that is below 1e9, at least 1e-9; degree < 2: 1e-9 */
    double cmax = 1e-9;
    if (deg >= 2) {
        CurveRec cr = { 0, 0x80, r, n };
        double dt = (t2 - t1) / (double)(ncp * 2 - 2), s = (o->k[nk - 1] - o->k[0]) / (t2 - t1);   /* nb_parm_curvature: eval_nurbs' param_6 = 1 multiplies the derivatives by the knot range, then 1 / (t2 - t1) (checked on 601 stored NURBS of range > 1.5: median relative difference 2.7e-5; without the knot range, none match) */
        cmax = 0.0;
        for (double tt = t1; tt <= t2; tt += dt) {
            double d[3][3]; if (mcs_spline_eval_d(&cr, tt, 2, d)) continue;
            double a[3] = { d[1][0] * s, d[1][1] * s, d[1][2] * s }, b[3] = { d[2][0] * s * s, d[2][1] * s * s, d[2][2] * s * s };
            double c[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
            double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), k = 0;
            if (la > 1e-6) k = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) / la;
            if (cmax < k && k < 1e9) cmax = k;
        }
        if (cmax < 1e-9) cmax = 1e-9;
    }
    wd(r + 0xa8, cmax);
    *rn = n;
    (void)rdd;
    return r;
}

/* ---- convert_ppspline_to_nbspline (mccore 0x180261480) + FUN_180059f40 (mccadutil, convert_spline_to_nb param_3 = 1, order 4 only): a parametric cubic (class 8 record) as a NURBS record. Bezier segments:
   degree order-1, the power coefficients through the matrix FUN_18026c490 (rows C(a,k) / C(order-1,k), the convert_coeffs_to_cpts values at order 4); knots 0, i (deg times, i = 0 .. nseg), nseg. Then the
   knot removal FUN_180059f40: FUN_180058570's chord knots (cumulative Bezier segment end-point chords), per interior joint the C1 test (FUN_180058380: 2e-7) and C2 test (FUN_180058430): joint kept (3 knots, 3 points), 1
   knot removed (2 / 2) or 2 removed (1 / 1, the point (d0 + d1) / d0 P_{3j-1} - d1 / d0 P_{3j-2}). Returns the record (malloc), NULL when not order 4 / dim 3 */
uint8_t *mcsf_spline_to_nurbs(const uint8_t *src, uint32_t srcn, double tol, uint32_t *rn) {
    int32_t ns; memcpy(&ns, src + 0xb0, 4); int ord = src[0xb4], dim = src[0xb5]; (void)srcn;
    if (ord != 4 || dim != 3 || ns < 1) return NULL;
    int N = ns, ncp = 3 * N + 1, closed = src[0xb7] & 1;
    double *P = malloc(sizeof(double) * 3 * ncp), *Pn = malloc(sizeof(double) * 3 * ncp), *K = malloc(sizeof(double) * ((N + 1) * 3 + 2)), *Kn = malloc(sizeof(double) * ((N + 1) * 3 + 2)), *d = calloc((size_t)N, 8);
    if (!P || !Pn || !K || !Kn || !d) { free(P); free(Pn); free(K); free(Kn); free(d); return NULL; }
    for (int i = 0; i < N; i++) {                                 /* FUN_18026c490(4): the row (1, 0, 0, 0), (1, 1/3, 0, 0), (1, 2/3, 1/3, 0), (1, 1, 1, 1) */
        for (int k = 0; k < 3; k++) { double c[4]; for (int m = 0; m < 4; m++) memcpy(&c[m], src + 0xc0 + 8 * ((size_t)i * 12 + m * 3 + k), 8);
            P[3 * (3 * i + 0) + k] = c[0]; P[3 * (3 * i + 1) + k] = c[0] + c[1] / 3.0; P[3 * (3 * i + 2) + k] = c[0] + c[1] * (2.0 / 3.0) + c[2] / 3.0; P[3 * (3 * i + 3) + k] = c[0] + c[1] + c[2] + c[3]; }
    }
    /* FUN_180058570: chord knots: K[0 .. 3] = 0, then cumulative chord lengths of the Bezier end points, 3 at each joint, the last value 4 times */
    double S = 0; K[0] = K[1] = K[2] = K[3] = 0;
    for (int j = 0; j < N; j++) {
        double a[3], b[3]; for (int k = 0; k < 3; k++) { a[k] = P[3 * (3 * j) + k]; b[k] = P[3 * (3 * (j + 1)) + k]; }
        d[j] = sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
        S += d[j]; K[4 + 3 * j] = K[5 + 3 * j] = K[6 + 3 * j] = S;
    }
    K[4 + 3 * N] = S;
    int n = 6, nkn = 4;                                           /* new control points (flat doubles) / knots */
    memcpy(Pn, P, sizeof(double) * 6);
    memcpy(Kn, K, sizeof(double) * 4);
    for (int j = 1; j <= N - 1; j++) {
        double h0 = d[j - 1], h1 = d[j], ca = h1 / (h1 + h0), cb = h0 / (h1 + h0), c1 = 0, c2 = 0;
        for (int k = 0; k < 3; k++) { double v = (ca * P[3 * (3 * j - 1) + k] + cb * P[3 * (3 * j + 1) + k]) - P[3 * (3 * j) + k]; c1 += v * v; }
        int flag = sqrt(c1) <= 2e-7;
        if (flag) {
            double r = h1 / h0, a = (h1 + h0) / h0, q = (h1 + h0) / h1, w = h0 / h1;
            for (int k = 0; k < 3; k++) { double v = (q * P[3 * (3 * j + 1) + k] + -w * P[3 * (3 * j + 2) + k]) - (a * P[3 * (3 * j - 1) + k] + -r * P[3 * (3 * j - 2) + k]); c2 += v * v; }
            if (sqrt(c2) <= 2e-7) flag = 2;
        }
        if (flag == 0) {                                          /* the joint kept: its 3 points, 3 knots */
            for (int m = -1; m <= 1; m++) for (int k = 0; k < 3; k++) Pn[n++] = P[3 * (3 * j + m) + k];
            for (int m = 0; m < 3; m++) Kn[nkn++] = K[4 + 3 * (j - 1) + m];
        } else if (flag == 1) {                                   /* one knot removed */
            for (int k = 0; k < 3; k++) Pn[n++] = P[3 * (3 * j - 1) + k];
            for (int k = 0; k < 3; k++) Pn[n++] = P[3 * (3 * j + 1) + k];
            Kn[nkn++] = K[4 + 3 * (j - 1)]; Kn[nkn++] = K[4 + 3 * (j - 1)];
        } else {                                                  /* two knots removed: one point */
            double r = h1 / h0, a = (h1 + h0) / h0;
            for (int k = 0; k < 3; k++) Pn[n++] = a * P[3 * (3 * j - 1) + k] - r * P[3 * (3 * j - 2) + k];
            Kn[nkn++] = K[4 + 3 * (j - 1)];
        }
    }
    for (int k = 0; k < 3; k++) Pn[n++] = P[3 * (3 * N - 1) + k];   /* the last two control points */
    for (int k = 0; k < 3; k++) Pn[n++] = P[3 * (3 * N) + k];
    for (int m = 0; m < 4; m++) Kn[nkn + m] = K[4 + 3 * (N - 1) + m];
    nkn += 4;
    McsfNb o = { n / 3, nkn, 3, 3, 0, closed, Kn, Pn };
    uint8_t *r = mcsf_nurbs_rec(&o, tol, rn);
    if (r) { r[0xd2] = (uint8_t)((closed ? 2 : 0) | 4); }
    free(P); free(Pn); free(K); free(Kn); free(d);
    return r;
}
