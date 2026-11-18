/* mcfillet_ent.c - find_fil_ent (mccadutil @18005d780, decomp/wfacc/mccadutil_kern3.c:521): a fillet of a line, arc or spline with a spline. Newton on the offset tangent lines; called by make_fillet (core_kernels.c:1216) with param_9 = 1
 * (sketcher_fillet_1.c:529 passes 1 as make_fillet's param_12). Oracle: tools/tests/fillet_oracle.sh, FIE cases (the real mccadutil.dll under tools/peloader). */
#include <math.h>
#include <string.h>
#include "mcfillet.h"
#include "mcsurf.h"
#ifdef MCF_TRACE
#include <stdio.h>
#define TR(...) fprintf(stderr, __VA_ARGS__)
#else
#define TR(...) ((void)0)
#endif

/* pt_on_ent_from_t / tan_on_ent_from_t (eval_curve order 1) */
void mcf_ent_eval(const McfEnt *e, double t, double p[2], double d[3]) {
    if (e->kind == 0) { double dx = e->bx - e->ax, dy = e->by - e->ay, dz = e->bz - e->az; p[0] = e->ax + t * dx; p[1] = e->ay + t * dy; d[0] = dx; d[1] = dy; d[2] = dz; return; }
    if (e->kind == 1) { double a = e->a0 + t * e->sw; p[0] = e->cx + e->r * cos(a); p[1] = e->cy + e->r * sin(a); d[0] = -e->r * e->sw * sin(a); d[1] = e->r * e->sw * cos(a); d[2] = 0; return; }
    double o[3][3]; memset(o, 0, sizeof o); mcs_spline_eval_d(e->c, t, 1, o); p[0] = o[0][0]; p[1] = o[0][1]; d[0] = o[1][0]; d[1] = o[1][1]; d[2] = o[1][2];
}
/* the seed t = the pick parameters (param_4); on success the tangent points' parameters t (param_6) and the arc (param_5 / FUN_18005c600). tol = tol_exref */
int mcf_find_fil_ent(const McfEnt E[2], double R, double tol, const double seed[2], int style, double t[2], McfArc *arc) {
    double step[2] = { 0, 0 }, ref[2][2] = { { 0, 0 }, { 0, 0 } }; t[0] = seed[0]; t[1] = seed[1];
    for (int it = 0; it < 100; it++) {
        double p[2][2], d[2][3], O[2][2]; mcf_ent_eval(&E[0], t[0], p[0], d[0]); mcf_ent_eval(&E[1], t[1], p[1], d[1]);
        TR("P %a %a %a %a\nP %a %a %a %a\nT %a %a %a %a\nT %a %a %a %a\n", t[0], p[0][0], p[0][1], 0.0, t[1], p[1][0], p[1][1], 0.0, t[0], d[0][0], d[0][1], d[0][2], t[1], d[1][0], d[1][1], d[1][2]);
        if (it == 0) { ref[0][0] = p[1][0] - p[0][0]; ref[0][1] = p[1][1] - p[0][1]; ref[1][0] = -ref[0][0]; ref[1][1] = -ref[0][1]; }   /* :626-631 */
        for (int i = 0; i < 2; i++) { double nx = -d[i][1], ny = d[i][0]; if (ny * ref[i][1] + nx * ref[i][0] < 0.0) { nx = -nx; ny = -ny; } double l = sqrt(ny * ny + nx * nx); if (l == 0.0) nx = ny = 0.0; else { nx /= l; ny /= l; } O[i][0] = p[i][0] + nx * R; O[i][1] = p[i][1] + ny * R; }
        if (hypot(O[1][0] - O[0][0], O[1][1] - O[0][1]) < tol) { mcf_arc_of(O[0][0], O[0][1], R, p[0], p[1], style, arc); return !(fabs(arc->sw) * arc->r < tol); }   /* :682-690, FUN_18005c600; the direction test :691 only with Fillet Chains' control (set_use_fil_dir) */
        double A[4] = { O[0][0], O[0][1], O[0][0] + d[0][0], O[0][1] + d[0][1] }, B[4] = { O[1][0], O[1][1], O[1][0] + d[1][0], O[1][1] + d[1][1] }, X[2];
        double dy1 = A[3] - A[1], dx1 = A[2] - A[0], l1 = dy1 * dy1 + dx1 * dx1, dx2 = B[0] - B[2], dy2 = B[1] - B[3], l2 = dx2 * dx2 + dy2 * dy2; if (!(1e-24 <= l1) || !(1e-24 <= l2)) return 0;
        double det = dy2 * dx1 - dx2 * dy1, dot = dy2 * dy1 + dx2 * dx1, ok, tt = tol * tol, nn = (B[0] - A[0]) * dy2 - (B[1] - A[1]) * dx2;   /* l_ints */
        if (tt <= fabs(dot)) ok = tol <= fabs(det / dot); else ok = tt <= fabs(det); if (!ok) return 0;
        X[0] = A[0] + dx1 * (nn / det); X[1] = A[1] + dy1 * (nn / det);
        TR("L %a %a %a %a %a %a %a %a -> 1 %a %a\n", A[0], A[1], A[2], A[3], B[0], B[1], B[2], B[3], X[0], X[1]);
        for (int i = 0; i < 2; i++) {
            double vx = X[0] - O[i][0], vy = X[1] - O[i][1], l = sqrt(d[i][0] * d[i][0] + d[i][1] * d[i][1] + d[i][2] * d[i][2]); if (l == 0.0) return 0;
            double s = (vx * d[i][0] + vy * d[i][1]) / l, pr = step[i];
            if (pr != 0.0) { if (fabs(pr) * 0.5 <= fabs(s) && fabs(s) != fabs(pr) * 0.5) s = 0.0 <= pr * s ? pr * 0.5 : pr * -0.5; if (0.0 <= pr * s) goto no_store; }
            step[i] = s;
        no_store:
            if (E[i].kind == 1) t[i] += s / (E[i].sw * E[i].r);
            else { double v = t[i] + s / l; t[i] = v;
                if (E[i].kind == 2) { uint32_t cls = E[i].c->cls; double a = E[i].t1, b = E[i].t2;   /* :766-800 */
                    if (cls == 8) { int32_t ns_; memcpy(&ns_, E[i].c->rec + 0xb0, 4); double nsd = (double)ns_; if (nsd < v) t[i] = nsd; if (t[i] < 0.0) t[i] = 0.0; }
                    else if (v < a) { if (!E[i].closed) t[i] = a; else { double w = a - b; t[i] = (v - (ceil(((v - a) - w) / w) * w + a)) + b; } }
                    else if (b < v) { if (!E[i].closed) t[i] = b; else { double w = b - a; t[i] = (v - (ceil(((v - b) - w) / w) * w + b)) + a; } } } }
        }
    }
    return 0;
}
