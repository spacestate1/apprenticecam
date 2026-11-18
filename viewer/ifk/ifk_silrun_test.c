/* ifk_silrun_test.c - self-test of the whole Silhouette Boundary kernel port (ifk_silhouette.c, formats/interfacial.md
 * 11.9): meshes made here -> ifk_silhouette_run -> the written binary tree curve file read back with
 * ifk_read_bin_curves(format 1) (the mcmsurf reader layout) -> loops checked against what is known exactly:
 * vertex sets (snapped to 2^-20 like AddNode FUN_180023710), closure, |area|, bounding box, tree ids / parents,
 * file size. NOT an oracle against Mastercam (decision N11-5).  Usage: ifk_silrun_test [-v] [DIR] */
#include "ifk_silhouette.h"
#include "ifk_curvexfer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G 9.5367431640625e-07
#define PI 3.141592653589793
static int verbose;
static const char *dir = ".";
static double snap(double v) { return floor(v / G) * G; }

typedef struct { double *v; int nv, cv; int *t; int nt, ct; } Mesh;
static int addv(Mesh *m, double x, double y, double z)
{
    if (m->nv == m->cv) { m->cv = m->cv ? 2 * m->cv : 64; m->v = realloc(m->v, (size_t)m->cv * 3 * sizeof(double)); }
    m->v[3 * m->nv] = x; m->v[3 * m->nv + 1] = y; m->v[3 * m->nv + 2] = z;
    return m->nv++;
}
static void addt(Mesh *m, int a, int b, int c)
{
    if (m->nt == m->ct) { m->ct = m->ct ? 2 * m->ct : 64; m->t = realloc(m->t, (size_t)m->ct * 3 * sizeof(int)); }
    m->t[3 * m->nt] = a; m->t[3 * m->nt + 1] = b; m->t[3 * m->nt + 2] = c; m->nt++;
}
static void quad(Mesh *m, int a, int b, int c, int d) { addt(m, a, b, c); addt(m, a, c, d); }
static void box_mesh(Mesh *m, double x0, double y0, double z0, double dx, double dy, double dz)
{
    int v[8];
    for (int i = 0; i < 8; i++) v[i] = addv(m, x0 + dx * (i & 1), y0 + dy * ((i >> 1) & 1), z0 + dz * (i >> 2));
    quad(m, v[0], v[2], v[3], v[1]); quad(m, v[4], v[5], v[7], v[6]);
    quad(m, v[0], v[1], v[5], v[4]); quad(m, v[2], v[6], v[7], v[3]);
    quad(m, v[0], v[4], v[6], v[2]); quad(m, v[1], v[3], v[7], v[5]);
}
/* square tube: outer [0, a]^2, hole [d, a - d]^2, height h (top / bottom annuli, outer and hole walls) */
static void tube_mesh(Mesh *m, double a, double d, double h)
{
    double o[4][2] = { { 0, 0 }, { a, 0 }, { a, a }, { 0, a } };
    double in[4][2] = { { d, d }, { a - d, d }, { a - d, a - d }, { d, a - d } };
    int ob[4], ot[4], hb[4], ht[4];
    for (int i = 0; i < 4; i++) {
        ob[i] = addv(m, o[i][0], o[i][1], 0.0); ot[i] = addv(m, o[i][0], o[i][1], h);
        hb[i] = addv(m, in[i][0], in[i][1], 0.0); ht[i] = addv(m, in[i][0], in[i][1], h);
    }
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        quad(m, ot[i], ot[j], ht[j], ht[i]); quad(m, ob[j], ob[i], hb[i], hb[j]);
        quad(m, ob[i], ob[j], ot[j], ot[i]); quad(m, hb[j], hb[i], ht[i], ht[j]);
    }
}

/* expected loop: snapped vertex set, parent loop (-1 = top level), |area| */
typedef struct { int n; double xy[80][2]; int parent; double area; } Loop;
static void loop_add(Loop *L, double x, double y)
{
    for (int i = 0; i < L->n; i++) if (L->xy[i][0] == snap(x) && L->xy[i][1] == snap(y)) return;
    L->xy[L->n][0] = snap(x); L->xy[L->n][1] = snap(y); L->n++;
}

static long fsize(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return -1; fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); return n; }

/* runs the kernel, reads the file back, matches each curve to an expected loop. nexp < 0: only totals are checked
 * (total segments = -nexp, all curves closed, total |area| = area0) */
static int check(const char *name, Mesh *m, double tol, int nexp, Loop *exp, double area0)
{
    char path[512]; snprintf(path, sizeof path, "%s/ifk_silrun_%d.tmp", dir, (int)(strlen(name) * 131 + (unsigned char)name[7]));
    IfkMesh im = { m->nv, m->nt, m->v, m->t };
    IfkSilResult r;
    int rc = ifk_silhouette_run(&im, tol, path, &r);
    IfkCurveFile cf; memset(&cf, 0, sizeof cf);
    int ok = rc == 0 && ifk_read_bin_curves(path, 1, &cf) == 0;
    char hdr[19] = { 0 };
    FILE *f = fopen(path, "rb"); if (f) { if (fread(hdr, 1, 18, f) != 18) ok = 0; fclose(f); }
    ok = ok && !strcmp(hdr, "binary tree curves");
    long want = 18 + 8 + 24L * r.fnode;
    for (int k = 0; k < r.ncurve; k++) want += 12 + 8L * (r.curve_start[k + 1] - r.curve_start[k]);
    ok = ok && fsize(path) == want && cf.nnode == r.fnode && cf.nseg == r.fedge && cf.ncurve == r.ncurve;
    int match[16]; double tot = 0.0; int totseg = 0;
    for (int k = 0; ok && k < cf.ncurve; k++) {
        int s0 = cf.curve_start[k], s1 = cf.curve_start[k + 1], n = s1 - s0;
        totseg += n;
        /* closed chain: each segment starts where the previous ended, the last ends at the first's start */
        for (int i = s0; i < s1; i++) {
            int nx = i + 1 < s1 ? i + 1 : s0;
            if (cf.cseg[2 * i + 1] != cf.cseg[2 * nx]) ok = 0;
        }
        double a = 0.0, bx0 = 1e300, by0 = 1e300, bx1 = -1e300, by1 = -1e300;
        for (int i = s0; i < s1; i++) {
            const double *p = cf.node + 3 * cf.cseg[2 * i], *q = cf.node + 3 * cf.cseg[2 * i + 1];
            if (p[2] != 0.0) ok = 0;
            a += p[0] * q[1] - q[0] * p[1];
            if (p[0] < bx0) bx0 = p[0];
            if (p[0] > bx1) bx1 = p[0];
            if (p[1] < by0) by0 = p[1];
            if (p[1] > by1) by1 = p[1];
        }
        a = fabs(a) / 2.0; tot += a;
        if (nexp < 0) continue;
        match[k] = -1;
        for (int j = 0; j < nexp && match[k] < 0; j++) {
            if (exp[j].n != n) continue;
            int all = 1;
            for (int i = s0; i < s1 && all; i++) {
                const double *p = cf.node + 3 * cf.cseg[2 * i];
                int fnd = 0;
                for (int q = 0; q < exp[j].n; q++) if (p[0] == exp[j].xy[q][0] && p[1] == exp[j].xy[q][1]) fnd = 1;
                all = fnd;
            }
            if (all) match[k] = j;
        }
        if (match[k] < 0) { ok = 0; break; }
        const Loop *L = &exp[match[k]];
        double ex0 = 1e300, ey0 = 1e300, ex1 = -1e300, ey1 = -1e300;
        for (int q = 0; q < L->n; q++) {
            if (L->xy[q][0] < ex0) ex0 = L->xy[q][0];
            if (L->xy[q][0] > ex1) ex1 = L->xy[q][0];
            if (L->xy[q][1] < ey0) ey0 = L->xy[q][1];
            if (L->xy[q][1] > ey1) ey1 = L->xy[q][1];
        }
        if (bx0 != ex0 || by0 != ey0 || bx1 != ex1 || by1 != ey1) ok = 0;
        if (fabs(a - L->area) > 1e-9 * (1.0 + L->area)) ok = 0;
        if (verbose) printf("    curve %d: %d segs, area %.12g (expected %.12g), box [%g, %g] x [%g, %g], tree %d parent %d\n",
                            k, n, a, L->area, bx0, bx1, by0, by1, cf.curve_x[2 * k], cf.curve_x[2 * k + 1]);
    }
    if (ok && nexp >= 0) {
        ok = cf.ncurve == nexp;
        for (int k = 0; ok && k < cf.ncurve; k++) {
            int ep = exp[match[k]].parent, pid = cf.curve_x[2 * k + 1];
            if (ep < 0) { if (pid != 0) ok = 0; }
            else {
                int pk = -1;
                for (int j = 0; j < cf.ncurve; j++) if (match[j] == ep) pk = j;
                if (pk < 0 || cf.curve_x[2 * pk] != pid) ok = 0;
            }
        }
    }
    if (ok && nexp < 0) ok = totseg == -nexp && fabs(tot - area0) <= 1e-9 * (1.0 + area0);
    printf("%s: rc %d, file nodes %d segs %d curves %d; stitch levels %d moved %d degenerate %d split %d cancelled %d unions %d, removed pairs %d, warnings %d/%d: %s\n",
           name, rc, cf.nnode, cf.nseg, cf.ncurve, r.nlevel, r.nmoved, r.ndegenerate, r.nsplit, r.ncancel, r.nunion,
           r.nremoved, r.nwarn59, r.nwarn29, ok ? "PASS" : "FAIL");
    if (!ok || verbose) {
        if (r.msg[0]) printf("    msg: %s\n", r.msg);
        for (int k = 0; k < cf.ncurve; k++) {
            printf("    curve %d tree %d parent %d:", k, cf.curve_x[2 * k], cf.curve_x[2 * k + 1]);
            for (int i = cf.curve_start[k]; i < cf.curve_start[k + 1]; i++)
                printf(" (%.9g,%.9g)", cf.node[3 * cf.cseg[2 * i]], cf.node[3 * cf.cseg[2 * i] + 1]);
            printf("\n");
        }
    }
    ifk_curvefile_free(&cf);
    ifk_sil_result_free(&r);
    remove(path);
    return ok;
}

static double poly_area(const Loop *L)   /* the expected loop's vertices are given in boundary order */
{
    double a = 0.0;
    for (int i = 0; i < L->n; i++) { int j = (i + 1) % L->n; a += L->xy[i][0] * L->xy[j][1] - L->xy[j][0] * L->xy[i][1]; }
    return fabs(a) / 2.0;
}

static int t_box(void)
{
    Mesh m = { 0 }; Loop L[1]; memset(L, 0, sizeof L);
    box_mesh(&m, 0.3, -0.7, 0.1, 3.0, 2.0, 1.5);
    loop_add(&L[0], 0.3, -0.7); loop_add(&L[0], 3.3, -0.7); loop_add(&L[0], 3.3, 1.3); loop_add(&L[0], 0.3, 1.3);
    L[0].parent = -1; L[0].area = poly_area(&L[0]);
    int ok = check("box 3 x 2 x 1.5 -> one rectangle", &m, 1e-7, 1, L, 0);
    free(m.v); free(m.t); return ok;
}
static int t_cyl_standing(void)
{
    Mesh m = { 0 }; int n = 24, b[24], tt[24]; Loop L[1]; memset(L, 0, sizeof L);
    int cb = addv(&m, 0.0, 0.0, 0.0), ct = addv(&m, 0.0, 0.0, 5.0);
    for (int i = 0; i < n; i++) {
        double a = 2.0 * PI * i / n;
        b[i] = addv(&m, 2.0 * cos(a), 2.0 * sin(a), 0.0); tt[i] = addv(&m, 2.0 * cos(a), 2.0 * sin(a), 5.0);
        loop_add(&L[0], 2.0 * cos(a), 2.0 * sin(a));
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        addt(&m, cb, b[j], b[i]); addt(&m, ct, tt[i], tt[j]); quad(&m, b[i], b[j], tt[j], tt[i]);
    }
    L[0].parent = -1; L[0].area = poly_area(&L[0]);   /* ~ 12 sin(pi / 12) x 2^2 / 2 ... = 12.0 */
    int ok = check("cylinder standing (axis Z, r 2, 24 facets) -> the 24-gon", &m, 1e-7, 1, L, 0);
    free(m.v); free(m.t); return ok;
}
static int t_cyl_lying(void)
{
    /* axis X, x 0..6, r 2: the rectangle [0, 6] x [-2, 2] carrying the projected ring vertices on its short sides
     * (both half rings, snapped: see ifk_planecut_test.c tb_cyl_lying) */
    Mesh m = { 0 }; int n = 24, l[24], r[24]; Loop L[1]; memset(L, 0, sizeof L);
    int cl = addv(&m, 0.0, 0.0, 1.0), cr = addv(&m, 6.0, 0.0, 1.0);
    for (int i = 0; i < n; i++) {
        double a = 2.0 * PI * i / n;
        l[i] = addv(&m, 0.0, 2.0 * cos(a), 1.0 + 2.0 * sin(a)); r[i] = addv(&m, 6.0, 2.0 * cos(a), 1.0 + 2.0 * sin(a));
        loop_add(&L[0], 0.0, 2.0 * cos(a)); loop_add(&L[0], 6.0, 2.0 * cos(a));
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        addt(&m, cl, l[j], l[i]); addt(&m, cr, r[i], r[j]); quad(&m, l[i], l[j], r[j], r[i]);
    }
    double y0 = 1e300, y1 = -1e300;
    for (int q = 0; q < L[0].n; q++) { if (L[0].xy[q][1] < y0) y0 = L[0].xy[q][1]; if (L[0].xy[q][1] > y1) y1 = L[0].xy[q][1]; }
    L[0].parent = -1; L[0].area = 6.0 * (y1 - y0);
    int ok = check("cylinder lying (axis X, 24 facets) -> its rectangle outline", &m, 1e-7, 1, L, 0);
    free(m.v); free(m.t); return ok;
}
static int t_sphere(void)
{
    Mesh m = { 0 }; int ns = 32, nst = 16, idx[17][32]; Loop L[1]; memset(L, 0, sizeof L);
    int np = addv(&m, 1.0, 2.0, 3.0), sp = addv(&m, 1.0, 2.0, -3.0);
    for (int k = 1; k < nst; k++) {
        double ph = PI * k / nst;
        for (int i = 0; i < ns; i++) {
            double th = 2.0 * PI * i / ns;
            idx[k][i] = addv(&m, 1.0 + 3.0 * sin(ph) * cos(th), 2.0 + 3.0 * sin(ph) * sin(th), k == nst / 2 ? 0.0 : 3.0 * cos(ph));
            if (k == nst / 2) loop_add(&L[0], 1.0 + 3.0 * sin(ph) * cos(th), 2.0 + 3.0 * sin(ph) * sin(th));
        }
    }
    for (int i = 0; i < ns; i++) {
        int j = (i + 1) % ns;
        addt(&m, np, idx[1][i], idx[1][j]);
        addt(&m, sp, idx[nst - 1][j], idx[nst - 1][i]);
        for (int k = 1; k + 1 < nst; k++) quad(&m, idx[k][i], idx[k + 1][i], idx[k + 1][j], idx[k][j]);
    }
    L[0].parent = -1; L[0].area = poly_area(&L[0]);
    int ok = check("UV sphere r 3, 32 x 16 -> the equator 32-gon", &m, 1e-7, 1, L, 0);
    free(m.v); free(m.t); return ok;
}
static int t_box_hole(void)
{
    Mesh m = { 0 }; Loop L[2]; memset(L, 0, sizeof L);
    tube_mesh(&m, 4.0, 1.5, 2.0);
    loop_add(&L[0], 0, 0); loop_add(&L[0], 4, 0); loop_add(&L[0], 4, 4); loop_add(&L[0], 0, 4);
    loop_add(&L[1], 1.5, 1.5); loop_add(&L[1], 2.5, 1.5); loop_add(&L[1], 2.5, 2.5); loop_add(&L[1], 1.5, 2.5);
    L[0].parent = -1; L[0].area = 16.0; L[1].parent = 0; L[1].area = 1.0;
    int ok = check("box 4 x 4 with a 1 x 1 through hole -> outer square + the hole as its child", &m, 1e-7, 2, L, 0);
    free(m.v); free(m.t); return ok;
}
static int t_gap(void)
{
    /* two unit boxes 2^-12 apart, tol 1e-3: PlaneCut gives two loops; Stitch2D maps the gap's corner points onto one
     * representative each (FUN_180012a70), the two gap edges become opposite and cancel (FUN_180012e00), the union
     * merges the two tree nodes: one loop of 6 segments, area 2 + 2^-12 */
    Mesh m = { 0 }; double d = 1.0 / 4096.0;
    box_mesh(&m, 0, 0, 0, 1, 1, 1); box_mesh(&m, 1 + d, 0, 0, 1, 1, 1);
    int ok = check("two unit boxes 2^-12 apart, tol 1e-3 -> stitched into one loop (6 segs, area 2 + 2^-12)", &m, 1e-3, -6, NULL, 2.0 + d);
    free(m.v); free(m.t); return ok;
}
static int t_tjunction(void)
{
    /* box A [0, 1]^2 and box B [1 + d, 2 + d] x [0.25, 0.75], d = 2^-12, tol 1e-3: B's left corners lie within tol of
     * A's right edge (and far from its corners): FUN_180012ba0 splits that edge at them (sorted along the edge), the
     * middle piece and B's left edge cancel, the union merges A and B: one loop of 8 segments through B's corners */
    Mesh m = { 0 }; Loop L[1]; memset(L, 0, sizeof L); double d = 1.0 / 4096.0;
    box_mesh(&m, 0, 0, 0, 1, 1, 1); box_mesh(&m, 1 + d, 0.25, 0, 1, 0.5, 1);
    double v[8][2] = { { 0, 0 }, { 1, 0 }, { 1 + d, 0.25 }, { 2 + d, 0.25 }, { 2 + d, 0.75 }, { 1 + d, 0.75 }, { 1, 1 }, { 0, 1 } };
    for (int i = 0; i < 8; i++) loop_add(&L[0], v[i][0], v[i][1]);
    L[0].parent = -1; L[0].area = 1.5 + 0.75 * d;
    int ok = check("box + smaller box 2^-12 off its side, tol 1e-3 -> edge split at the T-junctions, one loop of 8", &m, 1e-3, 1, L, 0);
    free(m.v); free(m.t); return ok;
}
static int t_thin_tube(void)
{
    /* square tube wall 2^-12 < tol 1e-3: outer and hole within tol of each other -> FUN_180025000 removes both */
    Mesh m = { 0 };
    tube_mesh(&m, 4.0, 1.0 / 4096.0, 1.0);
    int ok = check("square tube, wall 2^-12 < tol 1e-3 -> zero-width pair removed, empty file", &m, 1e-3, 0, NULL, 0);
    free(m.v); free(m.t); return ok;
}
static int t_tube_kept(void)
{
    Mesh m = { 0 }; Loop L[2]; memset(L, 0, sizeof L); double d = 1.0 / 64.0;
    tube_mesh(&m, 4.0, d, 1.0);
    loop_add(&L[0], 0, 0); loop_add(&L[0], 4, 0); loop_add(&L[0], 4, 4); loop_add(&L[0], 0, 4);
    loop_add(&L[1], d, d); loop_add(&L[1], 4 - d, d); loop_add(&L[1], 4 - d, 4 - d); loop_add(&L[1], d, 4 - d);
    L[0].parent = -1; L[0].area = 16.0; L[1].parent = 0; L[1].area = (4 - 2 * d) * (4 - 2 * d);
    int ok = check("square tube, wall 2^-6 > tol 1e-3 -> outer + hole kept", &m, 1e-3, 2, L, 0);
    free(m.v); free(m.t); return ok;
}
static int t_corner(void)
{
    /* boxes touching at the corner (1, 1): one tree node (PlaneCut stage B); totals only: 8 segments, area 2 */
    Mesh m = { 0 };
    box_mesh(&m, 0, 0, 0, 1, 1, 1); box_mesh(&m, 1, 1, 0, 1, 1, 2);
    int ok = check("two boxes touching at a corner -> 8 segments in closed curves, total area 2", &m, 1e-7, -8, NULL, 2.0);
    free(m.v); free(m.t); return ok;
}
static int t_tolcheck(void)
{
    Mesh m = { 0 }; box_mesh(&m, 0, 0, 0, 1, 1, 1);
    IfkMesh im = { m.nv, m.nt, m.v, m.t };
    IfkSilResult r;
    int rc = ifk_silhouette_run(&im, 0.1234567, NULL, &r);
    int ok = rc == IFK_SIL_TOLCHECK && !strcmp(r.msg, "May 10 1954 and July 27 1976.");
    printf("tol 0.1234567 -> Run's tolerance check (l.24232), nothing written: %s\n", ok ? "PASS" : "FAIL");
    ifk_sil_result_free(&r); free(m.v); free(m.t);
    return ok;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "-v")) verbose = 1; else dir = argv[i]; }
    int all = 1;
    all &= t_box(); all &= t_cyl_standing(); all &= t_cyl_lying(); all &= t_sphere(); all &= t_box_hole();
    all &= t_gap(); all &= t_tjunction(); all &= t_thin_tube(); all &= t_tube_kept(); all &= t_corner(); all &= t_tolcheck();
    printf("%s\n", all ? "ALL PASS" : "SOME FAIL");
    return all ? 0 : 1;
}
