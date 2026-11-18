/* ifk_s2_test.c - self-test of ifk_sil_shell2d.c (the interfacial11 Shell2D side path of Silhouette's test-point
 * classifier). Exactly known cases: facets as Silhouette lists them on a test point, the Shell2D of their edges,
 * and whether the test point lies on its output (= the boundary of the facets' union). NOT an oracle. */
#include "ifk_sil_shell2d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G 9.5367431640625e-07   /* 2^-20 */

/* test point: exact (X, Y, W) as integers X / W in units of G (W = 2^20 w) */
typedef struct { double P[4]; long long X, Y, w; int nexact; } TP;
static void sd_int(PcSD *r, long long m, int e2)   /* m * 2^e2, e2 multiple of 32 not needed */
{
    memset(r, 0, sizeof *r);
    r->pos = m > 0; if (m < 0) m = -m;
    unsigned __int128 v = (unsigned __int128)m;
    int E = e2 >= 0 ? e2 / 32 : -((-e2 + 31) / 32), sh = e2 - 32 * E;
    v <<= sh;
    r->e = E; r->n = 0;
    while (v) { r->l[r->n++] = (uint32_t)v; v >>= 32; }
    if (r->n == 0) { r->n = 1; r->pos = 1; }
}
static void exact_cb(void *user, PcSD *X, PcSD *Y, PcSD *W)
{
    TP *t = user;
    t->nexact++;
    sd_int(X, t->X, -20); sd_int(Y, t->Y, -20); sd_int(W, t->w, 0);
    /* the test point's slot 0 overwrites its doubles (interfacial03 FUN_180017bd0 / FUN_180016cc0) */
    double x = (double)t->X * G, y = (double)t->Y * G, w = (double)t->w, aw = fabs(w);
    double qx = x / w, qy = y / w;
    t->P[0] = qx; t->P[1] = (aw * fabs(qx) + fabs(x)) / (aw - aw * 2.220446049250313e-16) + fabs(qx);
    t->P[2] = qy; t->P[3] = (aw * fabs(qy) + fabs(y)) / (aw - aw * 2.220446049250313e-16) + fabs(qy);
}
/* midpoint of grid points a, b (interfacial03 FUN_180014710) */
static TP mid(double ax, double ay, double bx, double by)
{
    TP t; memset(&t, 0, sizeof t);
    double sx = ax + bx, sy = ay + by;
    t.P[0] = sx * 0.5; t.P[1] = fabs(sx) * 0.5; t.P[2] = sy * 0.5; t.P[3] = fabs(sy) * 0.5;
    t.X = llround(sx / G); t.Y = llround(sy / G); t.w = 2;
    return t;
}
static TP at(double x, double y)
{
    TP t; memset(&t, 0, sizeof t);
    t.P[0] = x; t.P[2] = y;
    t.X = llround(x / G); t.Y = llround(y / G); t.w = 1;
    return t;
}

/* facets: n triangles over a node table (vertices cached per node, FUN_180022e80) */
static int run(const char *name, const double (*node)[2], int nn, const int (*tri)[3], int nt, TP t, int expect)
{
    IfkS2 *s = ifk_s2_new(G);
    int cache[64]; for (int i = 0; i < nn; i++) cache[i] = -1;
    ifk_s2_begin(s);
    for (int f = 0; f < nt; f++) {
        for (int k = 0; k < 3; k++) { int n = tri[f][k]; if (cache[n] < 0) cache[n] = ifk_s2_vertex(s, node[n][0], node[n][1]); }
        ifk_s2_segment(s, cache[tri[f][0]], cache[tri[f][1]]);
        ifk_s2_segment(s, cache[tri[f][1]], cache[tri[f][2]]);
        ifk_s2_segment(s, cache[tri[f][2]], cache[tri[f][0]]);
    }
    ifk_s2_end(s);
    int ok = ifk_s2_process(s);
    int on = ifk_s2_point_on_output(s, t.P, exact_cb, &t);
    int pass = ok == 1 && on == expect && !ifk_s2_error(s);
    printf("%-44s process %d, output %d edges, on output %d (expected %d), exact calls %d%s%s  %s\n", name, ok,
           ifk_s2_noutput(s), on, expect, t.nexact, ifk_s2_error(s) ? ", " : "", ifk_s2_error(s) ? ifk_s2_error(s) : "",
           pass ? "PASS" : "FAIL");
    ifk_s2_free(s);
    return pass;
}

int main(void)
{
    int all = 1;
    {   /* two triangles sharing the diagonal from opposite sides: the diagonal is inside the union */
        double n[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        int t[2][3] = { { 0, 1, 3 }, { 1, 2, 3 } };
        all &= run("shared edge, opposite sides", n, 4, t, 2, mid(1, 0, 0, 1), 0);
    }
    {   /* the second triangle inside the first, sharing its edge: the edge is on the union boundary */
        double n[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 0.25, 0.25 } };
        int t[2][3] = { { 0, 1, 2 }, { 1, 2, 3 } };
        all &= run("shared edge, same side (overlap)", n, 4, t, 2, mid(1, 0, 0, 1), 1);
    }
    {   /* a box's top and bottom triangles listed on the midpoint of a square edge (other diagonals) */
        double n[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        int t[2][3] = { { 0, 1, 2 }, { 0, 1, 3 } };
        all &= run("box top + bottom, square edge midpoint", n, 4, t, 2, mid(0, 0, 1, 0), 1);
    }
    {   /* the same two facets, test point at their common vertex */
        double n[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        int t[2][3] = { { 0, 1, 2 }, { 0, 1, 3 } };
        all &= run("box top + bottom, facet vertex", n, 4, t, 2, at(1, 0), 1);
    }
    {   /* two crossing triangles: a point inside the other triangle is not on the boundary */
        double n[6][2] = { { 0, 0 }, { 2, 0 }, { 1, 2 }, { 0, 1 }, { 2, 1 }, { 1, -1 } };
        int t[2][3] = { { 0, 1, 2 }, { 3, 5, 4 } };
        all &= run("crossing triangles, edge midpoint inside", n, 6, t, 2, mid(0, 0, 2, 0), 0);
        /* the crossing point (0.5, 1) of edge (0,0)-(1,2) with y = 1: on the boundary of the union */
        TP c; memset(&c, 0, sizeof c);
        c.P[0] = 0.5; c.P[1] = 0.5; c.P[2] = 1.0; c.P[3] = 1.0;   /* filtered, error > 0 -> exact */
        c.X = llround(1.0 / G); c.Y = llround(2.0 / G); c.w = 2;
        all &= run("crossing triangles, crossing vertex", n, 6, t, 2, c, 1);
        /* a point on the part of edge (0,1)-(2,1) inside the first triangle: interior of the union */
        all &= run("crossing triangles, edge part inside", n, 6, t, 2, mid(0, 1, 2, 1), 0);
        /* a point on the part of edge (0,1)-(2,1) outside the first triangle: boundary */
        all &= run("crossing triangles, edge part outside", n, 6, t, 2, mid(0, 1, 0.5, 1), 1);
    }
    puts(all ? "ALL PASS" : "SOME FAILED");
    return all ? 0 : 1;
}
