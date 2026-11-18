/* ifk_planecut_test.c - self-test of PlaneCut stage A (ifk_planecut.c): split arrangements of Silhouette-style wall
 * inputs whose answer is known exactly. Walls are fed like interfacial11 FUN_180023f80 does: AddInputNode in the
 * order mid, start, end; AddInputTriangle(start, end, mid, id, false); one cut plane, normal (0,0,1) at 0.0.
 * Usage: ifk_planecut_test [-v]   (-v prints every arrangement) */
#include "ifk_planecut.h"
#include "ifk_silhouette_in.h"
#include "ifk_sil_classify.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G 9.5367431640625e-07   /* 2^-20, Silhouette's grid (interfacial11 DAT_18021c028) */
static int verbose;

typedef struct { int nedge, npt; IfkPlaneCut *pc; } Res;
static void grab(IfkPlaneCut *pc, int plane, void *user) { (void)plane; Res *r = user; r->nedge = pc->nge; r->pc = pc; }

static double snap(double v) { return floor(v / G) * G; }

static IfkPlaneCut *start(void)
{
    IfkPlaneCut *pc = ifk_pc_new(G);
    double n[3] = { 0, 0, 1 };
    ifk_pc_new_cut_plane_set(pc, n);
    ifk_pc_add_cut_plane(pc, 0.0);
    ifk_pc_end_cut_plane_set(pc);
    return pc;
}
static void wall(IfkPlaneCut *pc, double x0, double y0, double x1, double y1, int id)
{
    double m[3] = { snap((x0 + x1) * 0.5), snap((y0 + y1) * 0.5), 1.0 }, s[3] = { x0, y0, 0 }, e[3] = { x1, y1, 0 };
    int im = ifk_pc_add_input_node(pc, m), is = ifk_pc_add_input_node(pc, s), ie = ifk_pc_add_input_node(pc, e);
    ifk_pc_add_input_triangle(pc, is, ie, im, id, 0);
}
static void dump(const char *name, IfkPlaneCut *pc)
{
    printf("  [%s] %d graph edges, %d points (exact records %d, pair tests %ld)\n", name, pc->nge, pc->ptlist.n, pc->nex,
           pc->npairs);
    if (!verbose) return;
    for (int i = 0; i < pc->nge; i++) {
        const PcGEdge *g = &pc->ge[i];
        const double *a = pc->pt[g->p0].c, *b = pc->pt[g->p1].c;
        printf("    e%d (%.17g, %.17g) - (%.17g, %.17g) f10 %d f13 %d f14 %d f15 %d\n", i, a[0], a[2], b[0], b[2],
               g->f10, g->f13, g->f14, g->f15);
    }
    for (int e = 0; e < pc->nerr && e < 8; e++) printf("    error: %s\n", pc->err[e]);
}
static int run(IfkPlaneCut *pc, Res *r)
{
    memset(r, 0, sizeof *r);
    ifk_pc_end_input(pc, 1);
    return ifk_pc_run_stage_a(pc, grab, r);
}
/* does the arrangement contain an edge between (ax, ay) and (bx, by) (either way), exactly? */
static int has_edge(IfkPlaneCut *pc, double ax, double ay, double bx, double by)
{
    for (int i = 0; i < pc->nge; i++) {
        const double *a = pc->pt[pc->ge[i].p0].c, *b = pc->pt[pc->ge[i].p1].c;
        if ((a[0] == ax && a[2] == ay && b[0] == bx && b[2] == by) || (a[0] == bx && a[2] == by && b[0] == ax && b[2] == ay))
            return 1;
    }
    return 0;
}
static int degree(IfkPlaneCut *pc, int p)
{
    int d = 0;
    for (int i = 0; i < pc->nge; i++) d += (pc->ge[i].p0 == p) + (pc->ge[i].p1 == p);
    return d;
}
/* expected FUN_18001d8d0 flag of the piece of wall A = (s, e) cut by wall B = (bs, be) at their crossing:
 * N = horizontal part of (mid - end) x (start - end) of B = (-(bs - be).y, (bs - be).x); dot with A's direction
 * > 0 flags A's piece before the crossing, < 0 the piece after it (formats/interfacial.md 11.7) */
static int flag_ok(IfkPlaneCut *pc, double sx, double sy, double ex, double ey, double bsx, double bsy, double bex,
                   double bey, double cx, double cy)
{
    double dot = -(bsy - bey) * (ex - sx) + (bsx - bex) * (ey - sy);
    for (int i = 0; i < pc->nge; i++) {
        const double *a = pc->pt[pc->ge[i].p0].c, *b = pc->pt[pc->ge[i].p1].c;
        int before = (a[0] == sx && a[2] == sy && b[0] == cx && b[2] == cy) || (b[0] == sx && b[2] == sy && a[0] == cx && a[2] == cy);
        int after = (a[0] == ex && a[2] == ey && b[0] == cx && b[2] == cy) || (b[0] == ex && b[2] == ey && a[0] == cx && a[2] == cy);
        if (before && pc->ge[i].f10 != (dot > 0)) return 0;
        if (after && pc->ge[i].f10 != (dot < 0)) return 0;
    }
    return 1;
}
static int report(const char *name, int ok, IfkPlaneCut *pc)
{
    if (pc->nerr) ok = 0;
    dump(name, pc);
    printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    return ok;
}

static int t_square(void)
{
    IfkPlaneCut *pc = start(); Res r;
    wall(pc, 0, 0, 2, 0, 1); wall(pc, 2, 0, 2, 2, 2); wall(pc, 2, 2, 0, 2, 3); wall(pc, 0, 2, 0, 0, 4);
    int ok = run(pc, &r) == 0 && pc->nge == 4 && pc->ptlist.n == 4 && has_edge(pc, 0, 0, 2, 0) && has_edge(pc, 2, 0, 2, 2)
          && has_edge(pc, 2, 2, 0, 2) && has_edge(pc, 0, 2, 0, 0);
    for (int i = 0; i < pc->nge; i++) if (pc->ge[i].f10 || !pc->ge[i].f15) ok = 0;
    ok = report("square: 4 walls -> 4 edges, 4 points", ok, pc); ifk_pc_free(pc); return ok;
}
static int t_cross(void)
{
    IfkPlaneCut *pc = start(); Res r;
    wall(pc, 0, 0, 3, 1, 1); wall(pc, 0, 1, 3, 0, 2);
    int ok = run(pc, &r) == 0 && pc->nge == 4 && pc->ptlist.n == 5 && has_edge(pc, 0, 0, 1.5, 0.5) &&
             has_edge(pc, 1.5, 0.5, 3, 1) && has_edge(pc, 0, 1, 1.5, 0.5) && has_edge(pc, 1.5, 0.5, 3, 0) &&
             flag_ok(pc, 0, 0, 3, 1, 0, 1, 3, 0, 1.5, 0.5) && flag_ok(pc, 0, 1, 3, 0, 0, 0, 3, 1, 1.5, 0.5);
    ok = report("cross: 2 walls -> 4 pieces, node (1.5, 0.5) exact", ok, pc); ifk_pc_free(pc); return ok;
}
static int t_cross_third(void)
{
    IfkPlaneCut *pc = start(); Res r;
    wall(pc, 0, 0, 3, 1, 1); wall(pc, 1, -1, 1, 2, 2);
    int ok = run(pc, &r) == 0 && pc->nge == 4 && pc->ptlist.n == 5, found = 0;
    double cx = 0, cy = 0;
    for (int i = 0; i < pc->ptlist.n; i++) {
        const double *c = pc->pt[pc->ptlist.v[i]].c;
        if (degree(pc, pc->ptlist.v[i]) == 4) { cx = c[0]; cy = c[2]; found = c[0] == 1.0 && fabs(c[2] - 1.0 / 3.0) <= 2.3e-16; }
    }
    ok = ok && found && flag_ok(pc, 0, 0, 3, 1, 1, -1, 1, 2, cx, cy) && flag_ok(pc, 1, -1, 1, 2, 0, 0, 3, 1, cx, cy);
    ok = report("cross at (1, 1/3): 4 pieces, node within 1 ulp", ok, pc); ifk_pc_free(pc); return ok;
}
static int t_tjunction(void)
{
    IfkPlaneCut *pc = start(); Res r;
    wall(pc, 0, 0, 2, 0, 1); wall(pc, 1, 0, 1, 1, 2);
    int ok = run(pc, &r) == 0 && pc->nge == 3 && pc->ptlist.n == 4 && has_edge(pc, 0, 0, 1, 0) && has_edge(pc, 1, 0, 2, 0)
          && has_edge(pc, 1, 0, 1, 1);
    ok = report("T-junction: 3 edges, 4 points", ok, pc); ifk_pc_free(pc); return ok;
}
static int t_tjunction_exact(void)
{
    IfkPlaneCut *pc = start(); Res r;
    wall(pc, 0, 0, 3, 1, 1); wall(pc, 1.5, 0.5, 1.5, 2, 2);   /* B's start on A: filtered orientation uncertain */
    int ok = run(pc, &r) == 0 && pc->nge == 3 && pc->ptlist.n == 4 && has_edge(pc, 0, 0, 1.5, 0.5) &&
             has_edge(pc, 1.5, 0.5, 3, 1) && has_edge(pc, 1.5, 0.5, 1.5, 2) && pc->nex > 0;
    ok = report("T-junction on a sloped wall (exact orientation): 3 edges", ok, pc); ifk_pc_free(pc); return ok;
}
static int t_collinear(void)
{
    IfkPlaneCut *pc = start(); Res r;
    wall(pc, 0, 0, 2, 0, 1); wall(pc, 1, 0, 3, 0, 2);
    int ok = run(pc, &r) == 0 && pc->nge == 3 && pc->ptlist.n == 4 && has_edge(pc, 0, 0, 1, 0) && has_edge(pc, 1, 0, 2, 0)
          && has_edge(pc, 2, 0, 3, 0);
    for (int i = 0; i < pc->nge; i++) {   /* the shared piece is seen in both directions only if the walls agree */
        const double *a = pc->pt[pc->ge[i].p0].c, *b = pc->pt[pc->ge[i].p1].c;
        if (((a[0] == 1 && b[0] == 2) || (a[0] == 2 && b[0] == 1)) && !(pc->ge[i].f13 ^ pc->ge[i].f14)) ok = 0;
    }
    ok = report("collinear overlap: 3 edges (shared piece deduplicated)", ok, pc); ifk_pc_free(pc); return ok;
}
static int t_concurrent(void)
{
    IfkPlaneCut *pc = start(); Res r;
    wall(pc, 0, 0, 2, 2, 1); wall(pc, 0, 2, 2, 0, 2); wall(pc, 1, 0, 1, 2, 3);
    int ok = run(pc, &r) == 0 && pc->nge == 6 && pc->ptlist.n == 7, centre = 0;
    for (int i = 0; i < pc->ptlist.n; i++) {
        const double *c = pc->pt[pc->ptlist.v[i]].c;
        if (c[0] == 1.0 && c[2] == 1.0 && degree(pc, pc->ptlist.v[i]) == 6) centre = 1;
    }
    ok = report("three concurrent walls: one node (1, 1) of degree 6", ok && centre, pc); ifk_pc_free(pc); return ok;
}
static int t_same_id(void)
{
    IfkPlaneCut *pc = start(); Res r;
    wall(pc, 0, 0, 3, 1, 7); wall(pc, 0, 1, 3, 0, 7);
    int ok = run(pc, &r) == 0 && pc->nge == 2 && pc->ptlist.n == 4;
    ok = report("same triangle id: pair skipped, 2 edges", ok, pc); ifk_pc_free(pc); return ok;
}
static int t_pentagram(void)
{
    IfkPlaneCut *pc = start(); Res r; double x[5], y[5];
    for (int i = 0; i < 5; i++) { x[i] = snap(4 * cos(2 * M_PI * i / 5)); y[i] = snap(4 * sin(2 * M_PI * i / 5)); }
    for (int i = 0; i < 5; i++) wall(pc, x[i], y[i], x[(i + 2) % 5], y[(i + 2) % 5], i + 1);
    int ok = run(pc, &r) == 0 && pc->nge == 15 && pc->ptlist.n == 10;
    for (int i = 0; i < pc->ptlist.n; i++) { int d = degree(pc, pc->ptlist.v[i]); if (d != 2 && d != 4) ok = 0; }
    ok = report("pentagram: 15 pieces, 10 points (5 crossings of degree 4)", ok, pc); ifk_pc_free(pc); return ok;
}

/* Silhouette walls from ifk_silhouette_walls (the stage before PlaneCut) */
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
static int t_mesh(const char *name, Mesh *m, int nexp)
{
    IfkSilWalls w; Res r;
    int ok = ifk_silhouette_walls(m->v, m->nv, m->t, m->nt, 0.0, 90.0, 1, &w) == 0;
    IfkPlaneCut *pc = start();
    for (int i = 0; i < w.nwnode; i++) ifk_pc_add_input_node(pc, w.wnode + 3 * i);
    for (int i = 0; i < w.nwtri; i++) ifk_pc_add_input_triangle(pc, w.wtri[3 * i], w.wtri[3 * i + 1], w.wtri[3 * i + 2], w.wtri_id[i], 0);
    ok = ok && run(pc, &r) == 0 && pc->nge == nexp && pc->ptlist.n == nexp;
    for (int i = 0; i < pc->ptlist.n; i++) if (degree(pc, pc->ptlist.v[i]) != 2) ok = 0;
    for (int i = 0; i < pc->nge; i++) if (pc->ge[i].f10) ok = 0;
    char buf[160]; snprintf(buf, sizeof buf, "%s: %d silhouette walls -> %d edges, %d points of degree 2", name, w.nwtri, nexp, nexp);
    ok = report(buf, ok, pc);
    ifk_pc_free(pc); ifk_silhouette_walls_free(&w);
    return ok;
}
static int t_box(void)
{
    Mesh m = { 0 }; int v[8];
    for (int i = 0; i < 8; i++) v[i] = addv(&m, 0.3 + 3.0 * (i & 1), -0.7 + 2.0 * ((i >> 1) & 1), 0.1 + 1.5 * (i >> 2));
    quad(&m, v[0], v[2], v[3], v[1]); quad(&m, v[4], v[5], v[7], v[6]);
    quad(&m, v[0], v[1], v[5], v[4]); quad(&m, v[2], v[6], v[7], v[3]);
    quad(&m, v[0], v[4], v[6], v[2]); quad(&m, v[1], v[3], v[7], v[5]);
    int ok = t_mesh("box (top and bottom squares coincide)", &m, 4);
    free(m.v); free(m.t); return ok;
}
static int t_cylinder(void)
{
    Mesh m = { 0 }; int n = 24, b[24], t[24];
    int cb = addv(&m, 0.0, 0.0, 0.0), ct = addv(&m, 0.0, 0.0, 5.0);
    for (int i = 0; i < n; i++) {
        double a = 2.0 * 3.141592653589793 * i / n;
        b[i] = addv(&m, 2.0 * cos(a), 2.0 * sin(a), 0.0); t[i] = addv(&m, 2.0 * cos(a), 2.0 * sin(a), 5.0);
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        addt(&m, cb, b[j], b[i]); addt(&m, ct, t[i], t[j]); quad(&m, b[i], b[j], t[j], t[i]);
    }
    int ok = t_mesh("cylinder 24 facets", &m, 24);
    free(m.v); free(m.t); return ok;
}
static int t_sphere(void)
{
    Mesh m = { 0 }; int ns = 32, nst = 16, idx[17][32];
    int np = addv(&m, 1.0, 2.0, 3.0), sp = addv(&m, 1.0, 2.0, -3.0);
    for (int k = 1; k < nst; k++) {
        double ph = 3.141592653589793 * k / nst;
        for (int i = 0; i < ns; i++) {
            double th = 2.0 * 3.141592653589793 * i / ns;
            idx[k][i] = addv(&m, 1.0 + 3.0 * sin(ph) * cos(th), 2.0 + 3.0 * sin(ph) * sin(th), k == nst / 2 ? 0.0 : 3.0 * cos(ph));
        }
    }
    for (int i = 0; i < ns; i++) {
        int j = (i + 1) % ns;
        addt(&m, np, idx[1][i], idx[1][j]);
        addt(&m, sp, idx[nst - 1][j], idx[nst - 1][i]);
        for (int k = 1; k + 1 < nst; k++) quad(&m, idx[k][i], idx[k + 1][i], idx[k + 1][j], idx[k][j]);
    }
    int ok = t_mesh("UV sphere 32 x 16 (equator)", &m, 32);
    free(m.v); free(m.t); return ok;
}

/* ================================================================== stage B: the whole PlaneCut with Silhouette's
 * classifier (ifk_sil_classify.c) -> curve tree. Expected loops are given by their vertex sets (snapped like
 * interfacial11 AddNode FUN_180023710: floor(v / g) g); each tree node's edges must form one closed cycle on exactly
 * that vertex set, and the nesting must match. */
typedef struct { int n; double xy[64][2]; int parent; } Loop;
static void loop_add(Loop *L, double x, double y)   /* distinct snapped vertices */
{
    for (int i = 0; i < L->n; i++) if (L->xy[i][0] == snap(x) && L->xy[i][1] == snap(y)) return;
    L->xy[L->n][0] = snap(x); L->xy[L->n][1] = snap(y); L->n++;
}
static int t_tree(const char *name, Mesh *m, int nexp, Loop *exp)
{
    IfkSilWalls w; IfkPcTree t; IfkSilClassify cl; IfkPcIO io;
    int ok = ifk_silhouette_walls(m->v, m->nv, m->t, m->nt, 0.0, 90.0, 1, &w) == 0;
    IfkPlaneCut *pc = start();
    for (int i = 0; i < w.nwnode; i++) ifk_pc_add_input_node(pc, w.wnode + 3 * i);
    for (int i = 0; i < w.nwtri; i++) ifk_pc_add_input_triangle(pc, w.wtri[3 * i], w.wtri[3 * i + 1], w.wtri[3 * i + 2], w.wtri_id[i], 0);
    ifk_pc_end_input(pc, 1);
    ifk_sil_classify_init(&cl, &io, &w, G);
    ok = ifk_pc_run(pc, 1, &io, &t) == 0 && ok;
    int match[16];
    ok = ok && t.nnode == nexp && t.nnode <= 16 && pc->nerr == 0 && cl.err[0] == 0 && cl.nshell_fail == 0;
    int *deg = calloc((size_t)t.npt + 1, sizeof(int));
    for (int k = 0; ok && k < t.nnode; k++) {
        int ne = 0, used = 0;
        memset(deg, 0, sizeof(int) * ((size_t)t.npt + 1));
        for (int e = 0; e < t.nedge; e++) if (t.edge[4 * e + 3] == k) { ne++; deg[t.edge[4 * e]]++; deg[t.edge[4 * e + 1]]++; }
        for (int p = 0; p < t.npt; p++) if (deg[p]) { used++; if (deg[p] != 2) ok = 0; }
        if (used != ne || ne == 0) { ok = 0; break; }
        /* one closed cycle: walk it once around */
        int st = -1, cur, prev = -1, steps = 0;
        for (int e = 0; e < t.nedge; e++) if (t.edge[4 * e + 3] == k) { st = t.edge[4 * e]; break; }
        cur = st;
        do {
            int nx = -1;
            for (int e = 0; e < t.nedge && nx < 0; e++) {
                if (t.edge[4 * e + 3] != k || e == prev) continue;
                if (t.edge[4 * e] == cur || t.edge[4 * e + 1] == cur) nx = e;
            }
            if (nx < 0) { ok = 0; break; }
            cur = t.edge[4 * nx] == cur ? t.edge[4 * nx + 1] : t.edge[4 * nx];
            prev = nx; steps++;
        } while (cur != st && steps <= ne);
        if (steps != ne) ok = 0;
        /* which expected loop: the same vertex set, exactly */
        match[k] = -1;
        for (int j = 0; j < nexp && match[k] < 0; j++) {
            if (exp[j].n != used) continue;
            int all = 1;
            for (int p = 0; p < t.npt && all; p++) {
                if (!deg[p]) continue;
                int f = 0;
                for (int q = 0; q < exp[j].n; q++) if (t.pt[3 * p] == exp[j].xy[q][0] && t.pt[3 * p + 1] == exp[j].xy[q][1]) f = 1;
                all = f;
            }
            if (all) match[k] = j;
        }
        if (match[k] < 0) ok = 0;
    }
    free(deg);
    for (int k = 0; ok && k < t.nnode; k++) {
        int p = t.parent[k], ep = exp[match[k]].parent;
        if ((p < 0) != (ep < 0) || (p >= 0 && match[p] != ep)) ok = 0;
    }
    printf("%s: %d tree node(s), %d points, %d edges; curves %d, test points %d (Shell2D %d), angle-sorted points %ld: %s\n",
           name, t.nnode, t.npt, t.nedge, pc->curves.n, pc->ntp, cl.nshell, pc->nangle, ok ? "PASS" : "FAIL");
    if (verbose || !ok) {
        for (int i = 0; i < pc->nerr && i < 8; i++) printf("    %s\n", pc->err[i]);
        if (cl.err[0]) printf("    %s\n", cl.err);
        for (int k = 0; k < t.nnode; k++) {
            printf("    node %d parent %d:", k, t.parent[k]);
            for (int e = 0; e < t.nedge; e++) if (t.edge[4 * e + 3] == k)
                printf(" (%.6g,%.6g)->(%.6g,%.6g)%s", t.pt[3 * t.edge[4 * e]], t.pt[3 * t.edge[4 * e] + 1],
                       t.pt[3 * t.edge[4 * e + 1]], t.pt[3 * t.edge[4 * e + 1] + 1], t.edge[4 * e + 2] ? "b" : "");
            printf("\n");
        }
    }
    ifk_pc_tree_free(&t); ifk_sil_classify_free(&cl); ifk_pc_free(pc); ifk_silhouette_walls_free(&w);
    return ok;
}
static void box_mesh(Mesh *m, double x0, double y0, double z0, double dx, double dy, double dz)
{
    int v[8];
    for (int i = 0; i < 8; i++) v[i] = addv(m, x0 + dx * (i & 1), y0 + dy * ((i >> 1) & 1), z0 + dz * (i >> 2));
    quad(m, v[0], v[2], v[3], v[1]); quad(m, v[4], v[5], v[7], v[6]);
    quad(m, v[0], v[1], v[5], v[4]); quad(m, v[2], v[6], v[7], v[3]);
    quad(m, v[0], v[4], v[6], v[2]); quad(m, v[1], v[3], v[7], v[5]);
}
static int tb_box(void)
{
    Mesh m = { 0 }; Loop L[1]; memset(L, 0, sizeof L);
    box_mesh(&m, 0.3, -0.7, 0.1, 3.0, 2.0, 1.5);
    loop_add(&L[0], 0.3, -0.7); loop_add(&L[0], 3.3, -0.7); loop_add(&L[0], 3.3, 1.3); loop_add(&L[0], 0.3, 1.3);
    L[0].parent = -1;
    int ok = t_tree("tree: box -> one square loop", &m, 1, L);
    free(m.v); free(m.t); return ok;
}
static int tb_cyl_standing(void)
{
    Mesh m = { 0 }; int n = 24, b[24], tt[24]; Loop L[1]; memset(L, 0, sizeof L);
    int cb = addv(&m, 0.0, 0.0, 0.0), ct = addv(&m, 0.0, 0.0, 5.0);
    for (int i = 0; i < n; i++) {
        double a = 2.0 * 3.141592653589793 * i / n;
        b[i] = addv(&m, 2.0 * cos(a), 2.0 * sin(a), 0.0); tt[i] = addv(&m, 2.0 * cos(a), 2.0 * sin(a), 5.0);
        loop_add(&L[0], 2.0 * cos(a), 2.0 * sin(a));
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        addt(&m, cb, b[j], b[i]); addt(&m, ct, tt[i], tt[j]); quad(&m, b[i], b[j], tt[j], tt[i]);
    }
    L[0].parent = -1;
    int ok = t_tree("tree: cylinder standing (axis Z, 24 facets) -> one 24-gon", &m, 1, L);
    free(m.v); free(m.t); return ok;
}
static int tb_cyl_lying(void)
{
    /* axis X from x = 0 to x = 6, radius 2, 24 facets, ring angle 0 at y = +2: the outline is the rectangle
     * [0, 6] x [-2, 2]; its short sides carry the ring vertices projected (y = 2 cos a) - of both half rings, since
     * snapping (floor to 2^-20) can separate y = 2 cos a and 2 cos -a (e.g. 1.2e-16 -> 0, -3.7e-16 -> -2^-20) */
    Mesh m = { 0 }; int n = 24, l[24], r[24]; Loop L[1]; memset(L, 0, sizeof L);
    int cl = addv(&m, 0.0, 0.0, 1.0), cr = addv(&m, 6.0, 0.0, 1.0);
    for (int i = 0; i < n; i++) {
        double a = 2.0 * 3.141592653589793 * i / n;
        l[i] = addv(&m, 0.0, 2.0 * cos(a), 1.0 + 2.0 * sin(a)); r[i] = addv(&m, 6.0, 2.0 * cos(a), 1.0 + 2.0 * sin(a));
        loop_add(&L[0], 0.0, 2.0 * cos(a)); loop_add(&L[0], 6.0, 2.0 * cos(a));   /* both half rings, snapped */
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        addt(&m, cl, l[j], l[i]); addt(&m, cr, r[i], r[j]); quad(&m, l[i], l[j], r[j], r[i]);
    }
    L[0].parent = -1;
    int ok = t_tree("tree: cylinder lying (axis X, 24 facets) -> its rectangle outline", &m, 1, L);
    free(m.v); free(m.t); return ok;
}
static int tb_sphere(void)
{
    Mesh m = { 0 }; int ns = 32, nst = 16, idx[17][32]; Loop L[1]; memset(L, 0, sizeof L);
    int np = addv(&m, 1.0, 2.0, 3.0), sp = addv(&m, 1.0, 2.0, -3.0);
    for (int k = 1; k < nst; k++) {
        double ph = 3.141592653589793 * k / nst;
        for (int i = 0; i < ns; i++) {
            double th = 2.0 * 3.141592653589793 * i / ns;
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
    L[0].parent = -1;
    int ok = t_tree("tree: UV sphere 32 x 16 -> the equator 32-gon", &m, 1, L);
    free(m.v); free(m.t); return ok;
}
static int tb_box_hole(void)
{
    /* [0, 4] x [0, 4] x [0, 2] with a vertical square hole [1.5, 2.5]^2: the outer square + the hole as its child */
    Mesh m = { 0 }; Loop L[2]; memset(L, 0, sizeof L);
    double o[4][2] = { { 0, 0 }, { 4, 0 }, { 4, 4 }, { 0, 4 } };
    double h[4][2] = { { 1.5, 1.5 }, { 2.5, 1.5 }, { 2.5, 2.5 }, { 1.5, 2.5 } };
    int ob[4], ot[4], hb[4], ht[4];
    for (int i = 0; i < 4; i++) {
        ob[i] = addv(&m, o[i][0], o[i][1], 0.0); ot[i] = addv(&m, o[i][0], o[i][1], 2.0);
        hb[i] = addv(&m, h[i][0], h[i][1], 0.0); ht[i] = addv(&m, h[i][0], h[i][1], 2.0);
        loop_add(&L[0], o[i][0], o[i][1]); loop_add(&L[1], h[i][0], h[i][1]);
    }
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        quad(&m, ot[i], ot[j], ht[j], ht[i]);         /* top annulus (normal +z) */
        quad(&m, ob[j], ob[i], hb[i], hb[j]);         /* bottom annulus (normal -z) */
        quad(&m, ob[i], ob[j], ot[j], ot[i]);         /* outer walls */
        quad(&m, hb[j], hb[i], ht[i], ht[j]);         /* hole walls */
    }
    L[0].parent = -1; L[1].parent = 0;
    int ok = t_tree("tree: box with a vertical through hole -> outer square + the hole as its child", &m, 2, L);
    free(m.v); free(m.t); return ok;
}
static int tb_two_boxes(void)
{
    /* A = [0, 2]^2 x [0, 1], B = [1, 3]^2 x [0.5, 2]: the union outline, 8 vertices incl. the crossings (2, 1), (1, 2) */
    Mesh m = { 0 }; Loop L[1]; memset(L, 0, sizeof L);
    box_mesh(&m, 0.0, 0.0, 0.0, 2.0, 2.0, 1.0);
    box_mesh(&m, 1.0, 1.0, 0.5, 2.0, 2.0, 1.5);
    double v[8][2] = { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 3, 1 }, { 3, 3 }, { 1, 3 }, { 1, 2 }, { 0, 2 } };
    for (int i = 0; i < 8; i++) loop_add(&L[0], v[i][0], v[i][1]);
    L[0].parent = -1;
    int ok = t_tree("tree: two overlapping boxes -> one loop, the union outline", &m, 1, L);
    free(m.v); free(m.t); return ok;
}
static int tb_tower(void)
{
    /* a small box [1, 2]^2 x [1, 2] standing on a big box [0, 3]^2 x [0, 1]: the big square only (the small
     * outline lies strictly inside the big top facets, its test point is covered) */
    Mesh m = { 0 }; Loop L[1]; memset(L, 0, sizeof L);
    box_mesh(&m, 0.0, 0.0, 0.0, 3.0, 3.0, 1.0);
    box_mesh(&m, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0);
    loop_add(&L[0], 0, 0); loop_add(&L[0], 3, 0); loop_add(&L[0], 3, 3); loop_add(&L[0], 0, 3);
    L[0].parent = -1;
    int ok = t_tree("tree: small box on a big box -> the big square only", &m, 1, L);
    free(m.v); free(m.t); return ok;
}
static int tb_corner(void)
{
    /* boxes [0, 1]^2 x [0, 1] and [1, 2]^2 x [0, 2] touching at the corner (1, 1): the outline is the 8 edges of the
     * two squares (point (1, 1) of degree 4: the angle sort FUN_180029f90 runs there and removes nothing, in / out
     * alternate). The grouping into tree nodes is whatever FUN_180015b30 makes of it (reported, not asserted). */
    Mesh m = { 0 };
    box_mesh(&m, 0, 0, 0, 1, 1, 1); box_mesh(&m, 1, 1, 0, 1, 1, 2);
    IfkSilWalls w; IfkPcTree t; IfkSilClassify cl; IfkPcIO io;
    int ok = ifk_silhouette_walls(m.v, m.nv, m.t, m.nt, 0.0, 90.0, 1, &w) == 0;
    IfkPlaneCut *pc = start();
    for (int i = 0; i < w.nwnode; i++) ifk_pc_add_input_node(pc, w.wnode + 3 * i);
    for (int i = 0; i < w.nwtri; i++) ifk_pc_add_input_triangle(pc, w.wtri[3 * i], w.wtri[3 * i + 1], w.wtri[3 * i + 2], w.wtri_id[i], 0);
    ifk_pc_end_input(pc, 1);
    ifk_sil_classify_init(&cl, &io, &w, G);
    ok = ifk_pc_run(pc, 1, &io, &t) == 0 && ok && t.nedge == 8 && t.npt == 7 && pc->nangle == 1 && pc->nerr == 0;
    double sq[2][4][2] = { { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }, { { 1, 1 }, { 2, 1 }, { 2, 2 }, { 1, 2 } } };
    for (int s = 0; ok && s < 2; s++)
        for (int i = 0; i < 4; i++) {
            const double *a = sq[s][i], *b = sq[s][(i + 1) % 4];
            int cnt = 0;
            for (int e = 0; e < t.nedge; e++) {
                const double *p = t.pt + 3 * t.edge[4 * e], *q = t.pt + 3 * t.edge[4 * e + 1];
                if ((p[0] == a[0] && p[1] == a[1] && q[0] == b[0] && q[1] == b[1]) ||
                    (p[0] == b[0] && p[1] == b[1] && q[0] == a[0] && q[1] == a[1])) cnt++;
            }
            if (cnt != 1) ok = 0;
        }
    printf("tree: two boxes touching at a corner -> the 8 square edges (angle sort at the degree-4 point): %d tree node(s), "
           "%d points, %d edges; curves %d, angle-sorted points %ld: %s\n", t.nnode, t.npt, t.nedge, pc->curves.n, pc->nangle,
           ok ? "PASS" : "FAIL");
    ifk_pc_tree_free(&t); ifk_sil_classify_free(&cl); ifk_pc_free(pc); ifk_silhouette_walls_free(&w);
    free(m.v); free(m.t); return ok;
}

int main(int argc, char **argv)
{
    verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    int all = 1;
    all &= t_square(); all &= t_cross(); all &= t_cross_third(); all &= t_tjunction(); all &= t_tjunction_exact();
    all &= t_collinear(); all &= t_concurrent(); all &= t_same_id(); all &= t_pentagram();
    all &= t_box(); all &= t_cylinder(); all &= t_sphere();
    all &= tb_box(); all &= tb_cyl_standing(); all &= tb_cyl_lying(); all &= tb_sphere(); all &= tb_box_hole();
    all &= tb_two_boxes(); all &= tb_tower();
    all &= tb_corner();
    printf("%s\n", all ? "ALL PASS" : "SOME FAIL");
    return all ? 0 : 1;
}
