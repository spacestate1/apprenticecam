/* mcglhit.c - gl_Hit_Surfs (mastercam.dll FUN_180224220, GuiOglPub.cpp), the shading-on surface pick: formats/chaining.md "The shading-on
 * pick (port)". Decompiles decomp/surfpick/mc_pick.c (FUN_180224220, FUN_180221770, surf_screen_box, view_to_world), decomp/
 * chain_srfedge_dl3.c (FUN_180136510 / FUN_180136f70: the node's triangles, FUN_180226780 / FUN_1802267b0, world_to_view); the
 * disassembly decomp/chain_srfedge_glhit.asm (gl_Hit_Surfs with FUN_180221510's 27-slot capture block) and decomp/chain_srfedge_glhit2.asm
 * (FUN_180221770, FUN_180226780 / FUN_1802267b0, FUN_180130980, view_to_world, gl_Hit_Surfs' tail): the sums in the order of the code.
 * mccore: lp_ints 0x1804b3ed0 (decomp/mccore_inters.c). Oracle: tools/tests/glhit_oracle.sh (mastercam.dll's own gl_Hit_Surfs). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mcglhit.h"
#include "mcpartline.h"

static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }

int glh_mesh(const McamFile *f, const Surf *s, double gl_tol, GlhMesh *m, const char **why) {
    memset(m, 0, sizeof *m);
    PlMesh pm; int nt = plm_mesh(f, s, gl_tol, &pm, why);   /* make_surf_polygons(1, 1, ent, gl_tolerance, 0, DAT_18087b854 = 0, ..) */
    if (nt < 0) return -1;
    if (nt == 0) { plm_free(&pm); return 0; }
    m->ntri = pm.ntri; m->nv = pm.nv;
    m->tri = malloc(sizeof(int) * 3 * (size_t)pm.ntri); m->xyz = malloc(sizeof(float) * 3 * (size_t)pm.nv);
    if (!m->tri || !m->xyz) { plm_free(&pm); glh_free(m); if (why) *why = "out of memory"; return -1; }
    memcpy(m->tri, pm.tri, sizeof(int) * 3 * (size_t)pm.ntri);
    if (s->rec[0x6e] & 4)   /* FUN_180136f70: ent +0x6e bit 4 swaps each triangle's first two indices */
        for (int k = 0; k < m->ntri; k++) { int t = m->tri[3 * k]; m->tri[3 * k] = m->tri[3 * k + 1]; m->tri[3 * k + 1] = t; }
    for (int k = 0; k < 3 * pm.nv; k++) m->xyz[k] = (float)pm.xyz[k];   /* FUN_180136510 (asm 1801367d0..): (float) of each double */
    plm_free(&pm);
    return 0;
}
void glh_free(GlhMesh *m) { free(m->tri); free(m->xyz); memset(m, 0, sizeof *m); }

/* view_to_world (0x180341520, asm 18034158e..18034161c): rows r0 r1 r2 at +8 / +0x20 / +0x38 of the view's entry; x = (y r1x + x r0x)
   + z r2x, y = (x r0y + y r1y) + z r2y, z = (x r0z + y r1z) + z r2z */
static void v2w(const double *m, const double p[3], double w[3]) {
    w[0] = p[1] * m[3] + p[0] * m[0] + p[2] * m[6];
    w[1] = p[0] * m[1] + p[1] * m[4] + p[2] * m[7];
    w[2] = p[0] * m[2] + p[1] * m[5] + p[2] * m[8];
}
/* world_to_view (0x180341b30): row i . p = (r_i0 x + r_i1 y) + r_i2 z (asm 180341c3a..180341c62) */
static double w2v(const double *m, int i, const double p[3]) { return m[3 * i] * p[0] + m[3 * i + 1] * p[1] + m[3 * i + 2] * p[2]; }
void glh_view(GlhView *v, const double m[9], const double cur[2]) {
    memcpy(v->m, m, sizeof v->m); v->cur[0] = cur[0]; v->cur[1] = cur[1];
    double a[3] = { cur[0], cur[1], 1.0 }, b[3] = { cur[0], cur[1], 0.0 };   /* local_3d8 = 1.0 (.rdata 0x1805c1298), then 0 */
    v2w(m, a, v->ray); v2w(m, b, v->ray + 3);
}
/* (asm 180224603..18022470a) the centre projected with the Matrix33's rows 0 / 1: (r0y cy + r0x cx) + r0z cz; skipped when (cur y - y)^2
   + (cur x - x)^2 > r^2; surf_screen_box (mcsrfdl.c sdl_screen_box) against the cursor: skipped when box x min > x, y min > y, x > x max,
   y > y max */
#include "mcsrfdl.h"
int glh_candidate(const Surf *s, const GlhView *v) {
    const uint8_t *r = s->rec; const double *m = v->m;
    double px = m[1] * rdd(r + 0xc8) + m[0] * rdd(r + 0xc0) + m[2] * rdd(r + 0xd0), py = m[4] * rdd(r + 0xc8) + m[3] * rdd(r + 0xc0) + m[5] * rdd(r + 0xd0);
    double dx = v->cur[0] - px, dy = v->cur[1] - py, rr = rdd(r + 0xd8);
    if (dy * dy + dx * dx > rr * rr) return 0;
    double bx[4]; sdl_screen_box(r, m, bx);
    if (bx[0] > v->cur[0] || bx[1] > v->cur[1] || v->cur[0] > bx[2] || v->cur[1] > bx[3]) return 0;
    return 1;
}
/* lp_ints (mccore 0x1804b3ed0): the line l (two points) with the plane through q of normal n */
static int lp_ints(const double *n, const double *q, const double *l, double *o, double *t, double tol) {
    double dx = l[3] - l[0], dy = l[4] - l[1], dz = l[5] - l[2];
    double den = n[1] * dy + n[0] * dx + n[2] * dz;
    if (fabs(den) <= tol * tol && tol * tol != fabs(den)) {
        double nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), ll = sqrt(dx * dx + dy * dy + dz * dz);
        if (nl == 0.0 || ll == 0.0 || fabs(den) / (ll * nl) < tol) return 0;
    }
    double u = (n[0] * (q[0] - l[0]) + n[1] * (q[1] - l[1]) + n[2] * (q[2] - l[2])) / den;
    *t = u;
    o[0] = dx * u + l[0]; o[1] = dy * u + l[1]; o[2] = dz * u + l[2];
    return 1;
}
/* FUN_1802267b0(c, p0, p1, p2) (asm 1802267b0..1802269c3): the three edge cross products' signs agree: d1 = (p1x - p0x)(cy - p0y) -
   (cx - p0x)(p1y - p0y) >= -1e-8, d2 = (p2x - p1x)(cy - p1y) - (cx - p1x)(p2y - p1y) > -1e-8, d3 = (p0x - p2x)(cy - p2y) - (cx - p2x)(p0y -
   p2y) > -1e-8 (.rdata 0x1805f4218); 1 when all three are the same truth value */
static int in_tri(const double *c, const double *p0, const double *p1, const double *p2) {
    double d1 = (p1[0] - p0[0]) * (c[1] - p0[1]) - (c[0] - p0[0]) * (p1[1] - p0[1]);
    int b1 = d1 >= -1e-08;
    double d2 = (p2[0] - p1[0]) * (c[1] - p1[1]) - (c[0] - p1[0]) * (p2[1] - p1[1]);
    if (b1 != (d2 > -1e-08)) return 0;
    double d3 = (p0[0] - p2[0]) * (c[1] - p2[1]) - (c[0] - p2[0]) * (p0[1] - p2[1]);
    return b1 == (d3 > -1e-08);
}
/* the view x / y of a vertex: (v.y r.y + v.x r.x) + v.z r.z (asm 1802217a9..1802217ca) */
static double vdot(const double *p, const double *r) { return p[1] * r[1] + p[0] * r[0] + p[2] * r[2]; }
/* FUN_180221770 (asm 180221770..180221b95) */
static void tri_test(const double *V0, const double *V1, const double *V2, const GlhView *v, double tol, int idx, GlhBest *b) {
    const double *X = v->m, *Y = v->m + 3;
    double P0[2], P1[2], P2[2], xmin, xmax, ymin, ymax;
    P0[0] = vdot(V0, X); xmin = xmax = P0[0];
    P0[1] = vdot(V0, Y); ymin = ymax = P0[1];
    P1[0] = vdot(V1, X); P1[1] = vdot(V1, Y);
    if (xmin > P1[0]) xmin = P1[0]; else if (P1[0] > xmax) xmax = P1[0];
    if (ymin > P1[1]) ymin = P1[1]; else if (P1[1] > ymax) ymax = P1[1];
    P2[0] = vdot(V2, X); P2[1] = vdot(V2, Y);
    if (xmin > P2[0]) xmin = P2[0]; else if (P2[0] > xmax) xmax = P2[0];
    if (ymin > P2[1]) ymin = P2[1]; else if (P2[1] > ymax) ymax = P2[1];
    const double *c = v->cur;
    if (!(c[0] >= xmin && xmax >= c[0] && c[1] >= ymin && ymax >= c[1])) return;   /* FUN_180226780 */
    if (!in_tri(c, P0, P1, P2)) return;
    double D[3] = { V1[0] - V0[0], V1[1] - V0[1], V1[2] - V0[2] }, E[3] = { V2[0] - V0[0], V2[1] - V0[1], V2[2] - V0[2] };
    double N[3] = { E[2] * D[1] - E[1] * D[2], E[0] * D[2] - D[0] * E[2], D[0] * E[1] - E[0] * D[1] };   /* asm 180221a35..180221a9e */
    double l = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);                                          /* FUN_180130980 */
    if (l == 0.0) { N[0] = N[1] = N[2] = 0.0; } else { N[0] = N[0] / l; N[1] = N[1] / l; N[2] = N[2] / l; }
    double H[3], t;
    if (!lp_ints(N, V0, v->ray, H, &t, tol)) return;
    double z = w2v(v->m, 2, H);
    if (!b->found || z > b->z) { b->hit[0] = H[0]; b->hit[1] = H[1]; b->hit[2] = H[2]; b->z = z; b->idx = idx; }
    b->found = 1;
}
void glh_surface(const GlhMesh *m, const GlhView *v, double tol, int idx, GlhBest *b) {
    for (int k = 0; k < m->ntri; k++) {   /* gl_Hit_Surfs l.311-327 (asm 180224a60..180224b40): the floats as doubles */
        const int *q = m->tri + 3 * k; double V[3][3];
        for (int j = 0; j < 3; j++) for (int c = 0; c < 3; c++) V[j][c] = (double)m->xyz[3 * q[j] + c];
        tri_test(V[0], V[1], V[2], v, tol, idx, b);
    }
}
