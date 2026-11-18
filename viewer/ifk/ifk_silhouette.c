/* ifk_silhouette.c - Silhouette::Run (interfacial11 18001f1c0) end to end, see ifk_silhouette.h and
 * formats/interfacial.md 11.9. Decompiles: decomp/interfacial/i11_silhouette_all.c (Run l.24006, the post-PlaneCut
 * loop l.24440-24760), i11_vt_stitch2d.txt (Stitch2D / Octree vtables), i01_surfer_curves.c and i01_surfer_all.c
 * (interfacial01 Surfer). Every step cites its function; what is not decoded is marked NOT DECODED.
 *
 * Orders. The DLL keeps everything in singly linked lists; "push-front" lists are walked from the head (newest
 * first), "append" lists from the oldest. The port keeps arrays in creation order and walks them in the DLL's list
 * order (comments say which). Nothing below depends on record addresses except the two places noted:
 *  - FUN_180012e00 keys a tree by the two point pointers: only equal / not equal and the orientation relative to the
 *    smaller pointer are used, and its stack logic is symmetric in the two orientations, so the result does not
 *    depend on the pointer order.
 *  - Stitch2D's octree (Octree<Stitch2D::Point>, FUN_180014360 / FUN_180014720 with the OctreeBox / OctreeEdgePolygon
 *    classifiers) only proposes candidate points; the decisive tests are exact code (FUN_180012a70's distance,
 *    FUN_180011660). The port proposes every point inside an x window that provably contains every point that can
 *    pass those tests [inferred-equivalent, the stage A / B "all candidates" convention, interfacial.md 11.8.6].
 *    The candidate ORDER from the octree only matters for split points with exactly equal projection keys on one
 *    edge (FUN_180012340's tree puts equal keys after earlier ones); that order is NOT DECODED, the port uses the
 *    point-list order and counts such ties (res->nties). */
#include "ifk_silhouette.h"
#include "ifk_silhouette_in.h"
#include "ifk_planecut.h"
#include "ifk_sil_classify.h"

#include <float.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRID 9.5367431640625e-07   /* 2^-20: FUN_18001e350 / DAT_180268368 (PlaneCut::PlaneCut's tolerance) */

typedef struct { int n, cap; int *v; } IVec;
static void iv_push(IVec *a, int x)
{
    if (a->n == a->cap) { a->cap = a->cap ? 2 * a->cap : 8; a->v = realloc(a->v, sizeof(int) * (size_t)a->cap); }
    a->v[a->n++] = x;
}
static void iv_push_front(IVec *a, int x)
{
    iv_push(a, x);
    memmove(a->v + 1, a->v, sizeof(int) * (size_t)(a->n - 1));
    a->v[0] = x;
}
static void iv_free(IVec *a) { free(a->v); a->v = NULL; a->n = a->cap = 0; }

static void set_msg(IfkSilResult *r, const char *m) { if (r && !r->msg[0]) snprintf(r->msg, sizeof r->msg, "%s", m); }

/* ================================================================================================================
 * Stitch2D (interfacial11; constructor FUN_180011fb0, 0xa8 bytes): +8 point list (push-front), +0x10 edge list
 * (push-front), +0x18..+0x40 box, +0x48 Probe (vftable 18021b850: slot 0 = FUN_180002bf0, empty), +0x50 = &Probe.
 * Point (pool +0x60, FUN_180013ce0, 0x38 bytes): x, y, z, +0x18 representative, +0x20 stamp, +0x28 Run's record.
 * Edge (pool +0x68, FUN_180013ef0, 0x28 bytes): [0], [1] points, +0x10 flag byte, +0x18 user (left 0: the Probe
 * exists, so FUN_180012340 calls its empty slot 0 instead of writing +0x18). */
typedef struct {
    int np, cp; double *x, *y, *z; int *rep;
    int *rec_t;        /* Run's StitchPointOutput record (FUN_180020590): [1] = first-tree node */
    int *rec_pt;       /* [0] = the second tree's point, -1 until made */
    int ne, ce; int *ea, *eb;              /* input edges in creation order (the +0x10 list is push-front) */
    int no, co; int *oa, *ob, *oflag;      /* FUN_180012340's rebuilt +0x10 list, stored from the head */
    /* candidate index: representatives sorted by x (ours, see the file header) */
    int nx; int *xs;
    double scale;
} St2;

static int st_point(St2 *s, double x, double y, double z)
{
    if (s->np == s->cp) {
        s->cp = s->cp ? 2 * s->cp : 64;
        s->x = realloc(s->x, sizeof(double) * (size_t)s->cp); s->y = realloc(s->y, sizeof(double) * (size_t)s->cp);
        s->z = realloc(s->z, sizeof(double) * (size_t)s->cp); s->rep = realloc(s->rep, sizeof(int) * (size_t)s->cp);
        s->rec_t = realloc(s->rec_t, sizeof(int) * (size_t)s->cp); s->rec_pt = realloc(s->rec_pt, sizeof(int) * (size_t)s->cp);
    }
    s->x[s->np] = x; s->y[s->np] = y; s->z[s->np] = z; s->rep[s->np] = -1; s->rec_t[s->np] = -1; s->rec_pt[s->np] = -1;
    return s->np++;
}
static void st_edge(St2 *s, int a, int b)
{
    if (s->ne == s->ce) {
        s->ce = s->ce ? 2 * s->ce : 64;
        s->ea = realloc(s->ea, sizeof(int) * (size_t)s->ce); s->eb = realloc(s->eb, sizeof(int) * (size_t)s->ce);
    }
    s->ea[s->ne] = a; s->eb[s->ne] = b; s->ne++;
}
static void st_out(St2 *s, int a, int b)   /* pushed front on the rebuilt list: stored in push order, reversed later */
{
    if (s->no == s->co) {
        s->co = s->co ? 2 * s->co : 64;
        s->oa = realloc(s->oa, sizeof(int) * (size_t)s->co); s->ob = realloc(s->ob, sizeof(int) * (size_t)s->co);
        s->oflag = realloc(s->oflag, sizeof(int) * (size_t)s->co);
    }
    s->oa[s->no] = a; s->ob[s->no] = b; s->oflag[s->no] = 0; s->no++;
}
static void st_reset(St2 *s)   /* FUN_180012260: points and edges back to their pools, box reset */
{
    s->np = 0; s->ne = 0; s->no = 0; s->nx = 0;
}
static void st_free(St2 *s)    /* FUN_1800204a0 */
{
    free(s->x); free(s->y); free(s->z); free(s->rep); free(s->rec_t); free(s->rec_pt);
    free(s->ea); free(s->eb); free(s->oa); free(s->ob); free(s->oflag); free(s->xs);
    memset(s, 0, sizeof *s);
}

static St2 *g_sx;
static int cmp_x(const void *a, const void *b)
{
    double u = g_sx->x[*(const int *)a], v = g_sx->x[*(const int *)b];
    return u < v ? -1 : u > v ? 1 : (*(const int *)a - *(const int *)b);
}
/* first index in s->xs (sorted by x) with x >= lo */
static int lower_x(const St2 *s, const int *xs, int n, double lo)
{
    int a = 0, b = n;
    while (a < b) { int m = (a + b) / 2; if (s->x[xs[m]] < lo) a = m + 1; else b = m; }
    return a;
}

/* FUN_180011660(rep P, tol^2, dir, A, len2): the projection of P on the edge strictly inside (0 < t < len2) and the
 * squared distance to the foot below tol^2 (operand order as decompiled) */
static int near_edge(const St2 *s, int P, double tol2, const double dir[3], int A, double len2)
{
    double t = dir[1] * (s->y[P] - s->y[A]) + dir[0] * (s->x[P] - s->x[A]) + dir[2] * (s->z[P] - s->z[A]);
    if (t <= 0.0 || len2 <= t) return 0;
    t = t / len2;
    double dy = s->y[P] - (dir[1] * t + s->y[A]);
    double dx = s->x[P] - (dir[0] * t + s->x[A]);
    double dz = s->z[P] - (dir[2] * t + s->z[A]);
    return dy * dy + dx * dx + dz * dz < tol2;
}

typedef struct { double t; int p; int k; } Cand;
static int cmp_cand(const void *a, const void *b)   /* ascending key, equal keys in insertion order (stable) */
{
    const Cand *u = a, *v = b;
    if (u->t < v->t) return -1;
    if (u->t > v->t) return 1;
    return u->k - v->k;
}

/* FUN_180012340(stitch, tol) */
static void st_run(St2 *s, double tol, IfkSilResult *res)
{
    double tol2 = tol * tol;                          /* param_2 * param_2 */
    /* window margin for the candidate index: |x difference| < tol is necessary for both tests (monotone rounding of
     * non-negative sums); 2 tol + a few ulps of the coordinates covers the rounding of the feet (ours) */
    double big = 0.0;
    for (int i = 0; i < s->np; i++) { double a = fabs(s->x[i]) + fabs(s->y[i]); if (a > big) big = a; }
    double margin = 2.0 * tol + 64.0 * DBL_EPSILON * (big + 1.0);
    int all = !(tol > 0.0) || !(2.0 * tol > 8.0 * DBL_EPSILON * big);   /* tiny tol: every point is a candidate */

    /* every point goes into the octree root (FUN_180013540 wrapper, +0x10 = 1); FUN_180014360 builds it (not needed:
     * the index below proposes the candidates). Then FUN_180012a70 for each point of +8 from the head: */
    int *xs = malloc(sizeof(int) * (size_t)(s->np + 1));
    for (int i = 0; i < s->np; i++) xs[i] = i;
    g_sx = s; qsort(xs, (size_t)s->np, sizeof(int), cmp_x);
    for (int P = s->np - 1; P >= 0; P--) {            /* +8 is push-front: head = newest */
        if (s->rep[P] >= 0) continue;                 /* param_2[3] != 0 */
        s->rep[P] = P;                                /* param_2[3] = param_2 */
        int k0 = all ? 0 : lower_x(s, xs, s->np, s->x[P] - margin);
        for (int k = k0; k < s->np; k++) {
            int Q = xs[k];
            if (!all && s->x[Q] > s->x[P] + margin) break;
            if (s->rep[Q] >= 0) continue;
            double dy = s->y[Q] - s->y[P], dx = s->x[Q] - s->x[P], dz = s->z[Q] - s->z[P];
            if (dy * dy + dx * dx + dz * dz < tol2) s->rep[Q] = P;   /* pdVar4[3] = param_2 */
        }
    }
    for (int i = 0; i < s->np; i++) if (s->rep[i] != i) res->nmoved++;
    /* representatives sorted by x; the candidate order (ties only) is the point-list order: NOT DECODED */
    int nr = 0;
    for (int k = 0; k < s->np; k++) if (s->rep[xs[k]] == xs[k]) xs[nr++] = xs[k];

    /* the edge loop: pop the head of +0x10 until empty; results pushed front on a new list */
    Cand *cand = NULL; int ccap = 0;
    for (int i = s->ne - 1; i >= 0; i--) {
        int a = s->rep[s->ea[i]], b = s->rep[s->eb[i]];   /* *plVar13 = [0]->+0x18; plVar13[1] = [1]->+0x18 */
        if (a == b) { res->ndegenerate++; continue; }      /* Probe slot 0 (empty), the edge back to the pool */
        /* FUN_180012ba0(stitch, E, octree, tol, tol^2, &list): stamp both ends (DAT_1802682d8 += 2), the query
         * polygon FUN_1800119b0 around the segment (cross-section +-tol, FUN_180014720), every candidate's
         * REPRESENTATIVE (point +0x18) not yet stamped is stamped and kept when FUN_180011660 holds */
        double dir[3] = { s->x[b] - s->x[a], s->y[b] - s->y[a], s->z[b] - s->z[a] };   /* local_68, local_60, local_58 */
        double len2 = dir[1] * dir[1] + dir[0] * dir[0] + dir[2] * dir[2];
        double lo = (s->x[a] < s->x[b] ? s->x[a] : s->x[b]) - margin, hi = (s->x[a] < s->x[b] ? s->x[b] : s->x[a]) + margin;
        int nc = 0;
        int k0 = all ? 0 : lower_x(s, xs, nr, lo);
        for (int k = k0; k < nr; k++) {
            int R = xs[k];
            if (!all && s->x[R] > hi) break;
            if (R == a || R == b) continue;           /* stamped */
            if (!near_edge(s, R, tol2, dir, a, len2)) continue;
            if (nc == ccap) { ccap = ccap ? 2 * ccap : 16; cand = realloc(cand, sizeof(Cand) * (size_t)ccap); }
            cand[nc].p = R; nc++;
        }
        if (nc == 0) { st_out(s, a, b); continue; }   /* the edge itself pushed front */
        res->nsplit++;
        {   /* candidate order: the point list from the head = newest point first (NOT DECODED: the octree's order;
             * it only matters for equal keys, counted in res->nties) */
            for (int x1 = 1; x1 < nc; x1++) {
                Cand c = cand[x1]; int y1 = x1 - 1;
                while (y1 >= 0 && cand[y1].p < c.p) { cand[y1 + 1] = cand[y1]; y1--; }
                cand[y1 + 1] = c;
            }
        }
        for (int k = 0; k < nc; k++) {
            int R = cand[k].p;
            /* PointSort key (FUN_1800140c0): (R - A) . (B - A), operand order as decompiled */
            cand[k].t = (s->y[R] - s->y[a]) * (s->y[b] - s->y[a]) + (s->x[R] - s->x[a]) * (s->x[b] - s->x[a]) +
                        (s->z[R] - s->z[a]) * (s->z[b] - s->z[a]);
            cand[k].k = k;
        }
        /* binary tree: key < node -> left ([4]), else right ([5]) (FUN_18000b0c0 rebalances, in-order kept);
         * leftmost first (FUN_18000b200) = ascending key, equal keys in insertion order */
        qsort(cand, (size_t)nc, sizeof(Cand), cmp_cand);
        for (int k = 1; k < nc; k++) if (cand[k].t == cand[k - 1].t) res->nties++;
        st_out(s, a, cand[0].p);                      /* (E[0], first) */
        for (int k = 1; k < nc; k++) st_out(s, cand[k - 1].p, cand[k].p);
        st_out(s, cand[nc - 1].p, b);                 /* (last, E[1]); E back to the pool */
    }
    free(cand);
    /* *(param_1 + 0x10) = the new list: reverse the push order so that oa[0] is the head */
    for (int i = 0, j = s->no - 1; i < j; i++, j--) {
        int t = s->oa[i]; s->oa[i] = s->oa[j]; s->oa[j] = t;
        t = s->ob[i]; s->ob[i] = s->ob[j]; s->ob[j] = t;
    }
    free(xs);
    /* FUN_180014e30 / FUN_1800129e0: the octree freed */
}

/* FUN_180012e00: opposite edges cancel. Per unordered pair a tree record with two stacks [2] (edges running from the
 * smaller pointer) and [3] (the others). Edge E (list from the head): new pair -> flag 0, pushed on its stack; else
 * when the other stack is not empty, E and the other stack's head both get flag 1 (head popped), otherwise E is pushed
 * on its own stack. Pairs are independent, so they are processed group by group in list order. */
typedef struct { int lo, hi, pos; } Key;
static int cmp_key(const void *a, const void *b)
{
    const Key *u = a, *v = b;
    if (u->lo != v->lo) return u->lo - v->lo;
    if (u->hi != v->hi) return u->hi - v->hi;
    return u->pos - v->pos;
}
static void st_cancel(St2 *s, IfkSilResult *res)
{
    Key *k = malloc(sizeof(Key) * (size_t)(s->no + 1));
    for (int i = 0; i < s->no; i++) {
        k[i].lo = s->oa[i] < s->ob[i] ? s->oa[i] : s->ob[i];
        k[i].hi = s->oa[i] < s->ob[i] ? s->ob[i] : s->oa[i];
        k[i].pos = i;
    }
    qsort(k, (size_t)s->no, sizeof(Key), cmp_key);
    int *st[2]; st[0] = malloc(sizeof(int) * (size_t)(s->no + 1)); st[1] = malloc(sizeof(int) * (size_t)(s->no + 1));
    for (int g = 0; g < s->no;) {
        int h = g, n0 = 0, n1 = 0;
        while (h < s->no && k[h].lo == k[g].lo && k[h].hi == k[g].hi) h++;
        for (int j = g; j < h; j++) {
            int e = k[j].pos, fw = s->oa[e] == k[j].lo;   /* *puVar5 == *puVar11 */
            int *other = fw ? &n1 : &n0, **os = fw ? &st[1] : &st[0];
            if (*other > 0) {                               /* E flagged, the other stack's head popped + flagged */
                s->oflag[e] = 1; s->oflag[(*os)[--(*other)]] = 1; res->ncancel++;
            } else if (fw) st[0][n0++] = e; else st[1][n1++] = e;
        }
        g = h;
    }
    free(st[0]); free(st[1]); free(k);
}

/* ================================================================================================================
 * The second tree (PlaneCutToSilhouette FUN_180024830, Run local_3b8): tree nodes (FUN_180028440 pool, 0x58 bytes:
 * [0] parent, [1] union-find link (first tree only), +0x10 edge list (push-front), [3] +0x18 counterpart / Surfer node,
 * +0x20 / +0x28 children list head / tail); points (FUN_180028290: x, y, +0x10 mark); edges (a, b). */
typedef struct {
    int nn, cn; int *par; IVec *ch; IVec *ed; int *sn; int *dead;
    int np, cp; double *xy; int *mark;
    int ne, ce; int *ea, *eb, *enode;
} Tree2;
static int t2_node(Tree2 *t)
{
    if (t->nn == t->cn) {
        t->cn = t->cn ? 2 * t->cn : 16;
        t->par = realloc(t->par, sizeof(int) * (size_t)t->cn); t->ch = realloc(t->ch, sizeof(IVec) * (size_t)t->cn);
        t->ed = realloc(t->ed, sizeof(IVec) * (size_t)t->cn); t->sn = realloc(t->sn, sizeof(int) * (size_t)t->cn);
        t->dead = realloc(t->dead, sizeof(int) * (size_t)t->cn);
    }
    t->par[t->nn] = -1; memset(&t->ch[t->nn], 0, sizeof(IVec)); memset(&t->ed[t->nn], 0, sizeof(IVec));
    t->sn[t->nn] = -1; t->dead[t->nn] = 0;
    return t->nn++;
}
static int t2_point(Tree2 *t, double x, double y)
{
    if (t->np == t->cp) {
        t->cp = t->cp ? 2 * t->cp : 64;
        t->xy = realloc(t->xy, sizeof(double) * 2 * (size_t)t->cp); t->mark = realloc(t->mark, sizeof(int) * (size_t)t->cp);
    }
    t->xy[2 * t->np] = x; t->xy[2 * t->np + 1] = y; t->mark[t->np] = -1;   /* +0x10 = 0 from the pool */
    return t->np++;
}
static int t2_edge(Tree2 *t, int a, int b, int node)
{
    if (t->ne == t->ce) {
        t->ce = t->ce ? 2 * t->ce : 64;
        t->ea = realloc(t->ea, sizeof(int) * (size_t)t->ce); t->eb = realloc(t->eb, sizeof(int) * (size_t)t->ce);
        t->enode = realloc(t->enode, sizeof(int) * (size_t)t->ce);
    }
    t->ea[t->ne] = a; t->eb[t->ne] = b; t->enode[t->ne] = node;
    return t->ne++;
}
static void t2_free(Tree2 *t)
{
    for (int i = 0; i < t->nn; i++) { iv_free(&t->ch[i]); iv_free(&t->ed[i]); }
    free(t->par); free(t->ch); free(t->ed); free(t->sn); free(t->dead); free(t->xy); free(t->mark);
    free(t->ea); free(t->eb); free(t->enode);
    memset(t, 0, sizeof *t);
}

/* ================================================================================================================
 * Run l.24440-24760: the breadth-first loop over the first tree (IfkPcTree t1) with a Stitch2D per tree node. */
static int uf_find(int *uf, int T)
{
    int r = T;
    while (uf[r] >= 0) r = uf[r];
    while (uf[T] >= 0 && uf[T] != r) { int n = uf[T]; uf[T] = r; T = n; }   /* path compression (no effect on results) */
    return r;
}
static void stitch_loop(const IfkPcTree *t1, double tol, Tree2 *t2, IfkSilResult *res)
{
    int N1 = t1->nnode, R1 = N1;                      /* R1 = the first tree's root (+0x18) */
    IVec *ch = calloc((size_t)N1 + 1, sizeof(IVec));  /* children lists from the head (+0x20) */
    IVec *ed = calloc((size_t)N1 + 1, sizeof(IVec));  /* edge lists from the head (+0x10) */
    int *par1 = malloc(sizeof(int) * ((size_t)N1 + 1)), *uf = malloc(sizeof(int) * ((size_t)N1 + 1));
    int *cnt = malloc(sizeof(int) * ((size_t)N1 + 1));
    for (int k = 0; k <= N1; k++) { uf[k] = -1; cnt[k] = -1; par1[k] = k < N1 ? (t1->parent[k] < 0 ? R1 : t1->parent[k]) : -1; }
    for (int k = 0; k < N1; k++) for (int i = 0; i < t1->nchild[k]; i++) iv_push(&ch[k], t1->child[k][i]);
    for (int i = 0; i < t1->nroot; i++) iv_push(&ch[R1], t1->root[i]);
    for (int e = t1->nedge - 1; e >= 0; e--) iv_push(&ed[t1->edge[4 * e + 3]], e);   /* slot 4 pushes front */
    int *mark = malloc(sizeof(int) * ((size_t)t1->npt + 1));   /* CurvePoint +0x10: 0 = not inserted yet */
    for (int p = 0; p < t1->npt; p++) mark[p] = -1;

    int root2 = t2_node(t2);                          /* FUN_180024830: param_1[3] = FUN_180028440 */
    St2 s; memset(&s, 0, sizeof s);                   /* FUN_180011fb0 (operator_new(0xa8)) */
    IVec q = { 0, 0, NULL }; int qh = 0;
    iv_push(&q, R1);                                  /* list node {root (+0x18 of local_248), 0} */
    while (qh < q.n) {
        int X = q.v[qh++];
        if (ch[X].n == 0) continue;                   /* plVar18 = node +0x20 == 0 */
        res->nlevel++;
        /* every child's edges (child list from the head, its +0x10 list from the head): points with +0x10 == 0 get a
         * Stitch2D point (x, y, 0.0; FUN_180013ce0, pushed front on +8, box) and a record (FUN_180020590: [1] =
         * the child, stored at point +0x28); then a Stitch2D edge (FUN_180013ef0: the two points' +0x10, [3] = 0)
         * pushed front on +0x10 */
        for (int i = 0; i < ch[X].n; i++) {
            int c = ch[X].v[i];
            for (int j = 0; j < ed[c].n; j++) {
                const int *e = t1->edge + 4 * ed[c].v[j];
                for (int k = 0; k < 2; k++) {
                    int p = e[k];
                    if (mark[p] < 0) {
                        mark[p] = st_point(&s, t1->pt[3 * p], t1->pt[3 * p + 1], 0.0);
                        s.rec_t[mark[p]] = c;
                        res->nstitch_pt++;
                    }
                }
                st_edge(&s, mark[e[0]], mark[e[1]]);
            }
        }
        st_run(&s, tol, res);                         /* FUN_180012340(stitch, tol) */
        st_cancel(&s, res);                           /* FUN_180012e00(stitch) */
        /* union-find over the first-tree nodes (+8 link): for each unflagged edge (+0x10 list from the head) the roots
         * of its two points' records' nodes; different -> rb[1] = ra and rb's children appended to ra's (tail +0x28) */
        for (int i = 0; i < s.no; i++) {
            if (s.oflag[i]) continue;
            int ra = uf_find(uf, s.rec_t[s.oa[i]]), rb = uf_find(uf, s.rec_t[s.ob[i]]);
            if (ra == rb) continue;
            uf[rb] = ra; res->nunion++;
            for (int k = 0; k < ch[rb].n; k++) iv_push(&ch[ra], ch[rb].v[k]);
            ch[rb].n = 0;
        }
        /* rebuild into the second tree: per unflagged edge (list from the head): R = root of [0]'s record node (the
         * record's [1] is set to R); R +0x18 == 0 -> a new tree node (FUN_180028440, list node pushed front on
         * local_398 = +0x20), R +0x18 = it, its parent = (root of R's first-tree parent) +0x18, or the root (local_3a0)
         * when that is 0; pushed front on the parent's children (tail +0x28 kept). Points: a record's [0] == 0 -> a
         * CurvePoint (FUN_180028290, x, y) pushed front on +8. A CurveEdge (FUN_180009e30 local_380) pushed front on
         * +0x10 (local_3a8) and on the node's +0x10. */
        for (int i = 0; i < s.no; i++) {
            if (s.oflag[i]) continue;
            int A = s.oa[i], B = s.ob[i];
            int R = uf_find(uf, s.rec_t[A]); s.rec_t[A] = R;
            if (cnt[R] < 0) {
                int N = t2_node(t2);
                cnt[R] = N;
                int P = uf_find(uf, par1[R]);
                int pn = cnt[P] < 0 ? root2 : cnt[P];
                t2->par[N] = pn;
                iv_push_front(&t2->ch[pn], N);
            }
            int ps[2] = { A, B };
            for (int k = 0; k < 2; k++)
                if (s.rec_pt[ps[k]] < 0) s.rec_pt[ps[k]] = t2_point(t2, s.x[ps[k]], s.y[ps[k]]);
            int e2 = t2_edge(t2, s.rec_pt[A], s.rec_pt[B], cnt[R]);
            iv_push_front(&t2->ed[cnt[R]], e2);
        }
        /* records back to their pool (local_420); the node's children popped from the head: each child's edge list
         * freed with both points' +0x10 = 0; a child without union-find link (+8 == 0) appended to the queue */
        for (int i = 0; i < ch[X].n; i++) {
            int c = ch[X].v[i];
            for (int j = 0; j < ed[c].n; j++) { const int *e = t1->edge + 4 * ed[c].v[j]; mark[e[0]] = -1; mark[e[1]] = -1; }
            ed[c].n = 0;
            if (uf[c] < 0) iv_push(&q, c);
        }
        ch[X].n = 0;
        st_reset(&s);                                 /* FUN_180012260 */
    }
    /* FUN_1800204a0 (Stitch2D freed); the pending list local_398 popped: nodes without parent go under the root
     * (none here: every node got its parent when made) */
    st_free(&s);
    iv_free(&q);
    for (int k = 0; k <= N1; k++) { iv_free(&ch[k]); iv_free(&ed[k]); }
    free(ch); free(ed); free(par1); free(uf); free(cnt); free(mark);
}

/* FUN_180024600(X, Y's edge list, tol^2): every end of every edge of Y (list from the head, [0] then [1]) lies within
 * squared distance tol^2 of X's edges (minimum over X's list); a minimum above tol^2 -> 0 */
static int near_all(const Tree2 *t, int X, int Y, double tol2)
{
    for (int i = 0; i < t->ed[Y].n; i++) {
        int e = t->ed[Y].v[i];
        int pp[2] = { t->ea[e], t->eb[e] };
        for (int k = 0; k < 2; k++) {
            double px = t->xy[2 * pp[k]], py = t->xy[2 * pp[k] + 1];
            double m = DBL_MAX;                       /* 0x7fefffffffffffff */
            for (int j = 0; j < t->ed[X].n; j++) {
                int f = t->ed[X].v[j];
                double ax = t->xy[2 * t->ea[f]], ay = t->xy[2 * t->ea[f] + 1];
                double bx = t->xy[2 * t->eb[f]], by = t->xy[2 * t->eb[f] + 1];
                double dx = px - ax, dy = py - ay, sx = bx - ax, sy = by - ay;
                double L = sy * sy + sx * sx + 0.0;
                double T = dy * sy + dx * sx + 0.0;
                double D = dy * dy + dx * dx + 0.0;
                if (0.0 < T) {
                    if (T <= L) {
                        if (L != 0.0) { D = D - (T * T) / L; if (D < 0.0) D = 0.0; }
                    } else {
                        double ex = px - bx, ey = py - by;
                        D = ey * ey + ex * ex + 0.0;
                    }
                }
                if (D < m) m = D;
            }
            if (tol2 < m) return 0;
        }
    }
    return 1;
}
/* FUN_180025000(tree, tol^2): breadth-first from the root's children; a node A with exactly one child B that has no
 * children, where each loop lies within tol of the other (FUN_180024600 both ways): A is unlinked from its parent's
 * children and A and B are freed (zero-width outer / hole pair); otherwise A's children are appended to the queue */
static void zero_width(Tree2 *t, double tol2, IfkSilResult *res)
{
    IVec q = { 0, 0, NULL }; int qh = 0;
    for (int i = 0; i < t->ch[0].n; i++) iv_push(&q, t->ch[0].v[i]);
    while (qh < q.n) {
        int A = q.v[qh++];
        int B = t->ch[A].n == 1 ? t->ch[A].v[0] : -1;
        if (B >= 0 && t->ch[B].n == 0 && near_all(t, A, B, tol2) && near_all(t, B, A, tol2)) {
            IVec *pc = &t->ch[t->par[A]];
            for (int i = 0; i < pc->n; i++) if (pc->v[i] == A) {
                memmove(pc->v + i, pc->v + i + 1, sizeof(int) * (size_t)(pc->n - i - 1)); pc->n--; break;
            }
            t->ed[B].n = 0; t->ed[A].n = 0; t->ch[A].n = 0;
            t->dead[A] = t->dead[B] = 1;
            res->nremoved++;
        } else {
            for (int i = 0; i < t->ch[A].n; i++) iv_push(&q, t->ch[A].v[i]);
        }
    }
    iv_free(&q);
}

/* ================================================================================================================
 * Surfer (interfacial01) write side. Impl fields: +0x18 node count, +0x1c edge count, +0x40 / +0x48 node list
 * (append), +0x50 / +0x58 edge list (append), +0x7c tree flag (NewWriteCurves(true)), +0x7d z-cut mode (0 from
 * FUN_180013d70; Run never calls SetZCutMode), +0x80 tree node counter.
 * Node (FUN_180003990): x, y, z, +0x18 id (1-based), +0x1c group (-1), +0x20 edge list (push-front), +0x28 byte
 * (false), +0x2a degree. Edge (FUN_180003d30): [0] / [1] nodes, +0x10 visited, +0x11 reversed, +0x12 byte (false),
 * +0x14 stamp, +0x18 tree node. Tree node (FUN_1800220c0): id, +8 parent. */
typedef struct {
    int nn, cn; double *xyz; int *deg; IVec *nedges;
    int ne, ce; int *a, *b, *vis, *rev, *tree, *stamp;
    int nt, ct; int *tpar;                          /* tree node parent (-1 = NULL); id = index + 1 */
    int stampc;
} Surf;
static int sf_node(Surf *s, double x, double y, double z)   /* AddWriteCurveNode(x, y, z, false, -1) */
{
    if (s->nn == s->cn) {
        s->cn = s->cn ? 2 * s->cn : 64;
        s->xyz = realloc(s->xyz, sizeof(double) * 3 * (size_t)s->cn); s->deg = realloc(s->deg, sizeof(int) * (size_t)s->cn);
        s->nedges = realloc(s->nedges, sizeof(IVec) * (size_t)s->cn);
    }
    s->xyz[3 * s->nn] = x; s->xyz[3 * s->nn + 1] = y; s->xyz[3 * s->nn + 2] = z; s->deg[s->nn] = 0;
    memset(&s->nedges[s->nn], 0, sizeof(IVec));
    return s->nn++;                                   /* id = ++(+0x18) = index + 1 */
}
static void sf_edge(Surf *s, int a, int b, int tree)   /* AddWriteCurveEdge -> FUN_180017a20 */
{
    if (s->ne == s->ce) {
        s->ce = s->ce ? 2 * s->ce : 64;
        s->a = realloc(s->a, sizeof(int) * (size_t)s->ce); s->b = realloc(s->b, sizeof(int) * (size_t)s->ce);
        s->vis = realloc(s->vis, sizeof(int) * (size_t)s->ce); s->rev = realloc(s->rev, sizeof(int) * (size_t)s->ce);
        s->tree = realloc(s->tree, sizeof(int) * (size_t)s->ce); s->stamp = realloc(s->stamp, sizeof(int) * (size_t)s->ce);
    }
    int e = s->ne++;                                  /* appended to +0x50, +0x1c += 1 */
    s->a[e] = a; s->b[e] = b; s->vis[e] = 0; s->rev[e] = 0; s->tree[e] = tree; s->stamp[e] = 0;
    iv_push_front(&s->nedges[a], e);                  /* pushed front on both nodes' +0x20 */
    iv_push_front(&s->nedges[b], e);
    s->deg[a]++; s->deg[b]++;                         /* +0x2a */
}
static int sf_tree(Surf *s, int parent)               /* AddCurveTreeNode + AddCurveTreeBranch(parent, node) */
{
    if (s->nt == s->ct) { s->ct = s->ct ? 2 * s->ct : 16; s->tpar = realloc(s->tpar, sizeof(int) * (size_t)s->ct); }
    s->tpar[s->nt] = parent;
    return s->nt++;
}
static void sf_free(Surf *s)
{
    for (int i = 0; i < s->nn; i++) iv_free(&s->nedges[i]);
    free(s->xyz); free(s->deg); free(s->nedges); free(s->a); free(s->b); free(s->vis); free(s->rev); free(s->tree);
    free(s->stamp); free(s->tpar);
    memset(s, 0, sizeof *s);
}

/* FUN_180024da0(tree, surfer) */
static void write_tree(Tree2 *t, Surf *s)
{
    /* Surfer::NewWriteCurves(true): +0x7c = 1, FUN_180017810(.., 1) clears nodes, edges, counts and tree nodes */
    IVec q = { 0, 0, NULL }; int qh = 0;
    iv_push(&q, 0);
    while (qh < q.n) {
        int X = q.v[qh++];
        for (int i = 0; i < t->ch[X].n; i++) {
            int c = t->ch[X].v[i];
            if (t->sn[c] < 0) t->sn[c] = sf_tree(s, X == 0 ? -1 : t->sn[X]);   /* the root's +0x18 is 0 (NULL) */
            for (int j = 0; j < t->ed[c].n; j++) {
                int e = t->ed[c].v[j];
                int pp[2] = { t->ea[e], t->eb[e] };
                for (int k = 0; k < 2; k++)
                    if (t->mark[pp[k]] < 0) t->mark[pp[k]] = sf_node(s, t->xy[2 * pp[k]], t->xy[2 * pp[k] + 1], 0.0);
                sf_edge(s, t->mark[pp[0]], t->mark[pp[1]], t->sn[c]);
            }
        }
        /* the children popped from the head: their edge lists freed with both points' +0x10 = 0, each appended */
        for (int i = 0; i < t->ch[X].n; i++) {
            int c = t->ch[X].v[i];
            for (int j = 0; j < t->ed[c].n; j++) { int e = t->ed[c].v[j]; t->mark[t->ea[e]] = -1; t->mark[t->eb[e]] = -1; }
            iv_push(&q, c);
        }
    }
    iv_free(&q);
    /* Surfer::EndWriteCurves: export RVA 0x19d0 = an empty function (shared with EndWriteSurface etc.) */
}

/* FUN_180019490(surfer, edge): the edge's component over unvisited edges (breadth-first through the nodes' +0x20
 * lists, stamp DAT_1800748e8 += 2), then the start: the first edge of the component list with +0x12 == 0 whose [0]
 * node has degree 1; (z-cut mode branch skipped: +0x7d = 0); else the first with +0x12 == 0 = the edge itself */
static int sf_pick(Surf *s, int e0)
{
    s->stampc += 2;
    IVec comp = { 0, 0, NULL };
    iv_push(&comp, e0); s->stamp[e0] = s->stampc;
    for (int i = 0; i < comp.n; i++) {
        int ab[2] = { s->a[comp.v[i]], s->b[comp.v[i]] };
        for (int k = 0; k < 2; k++) {
            IVec *L = &s->nedges[ab[k]];
            for (int j = 0; j < L->n; j++) {
                int e = L->v[j];
                if (!s->vis[e] && s->stamp[e] != s->stampc) { s->stamp[e] = s->stampc; iv_push(&comp, e); }
            }
        }
    }
    int r = comp.v[0];
    for (int i = 0; i < comp.n; i++) if (s->deg[s->a[comp.v[i]]] == 1) { r = comp.v[i]; break; }
    iv_free(&comp);
    return r;
}
/* FUN_18001a430(surfer, start, &list): follow the first unvisited edge leaving the current node (its +0x20 list from
 * the head; +0x12 is false for every edge here, so the "both ways" fallback never applies); tree mode: an edge of
 * another tree node -> "Surfer error 62: conflicting tree groups." */
static int sf_chain(Surf *s, int st, IVec *list, int *err62)
{
    list->n = 0;
    iv_push(list, st); s->vis[st] = 1; s->rev[st] = 0;
    int node = s->b[st];
    for (int i = 0; i < list->n; i++) {
        int f = -1;
        IVec *L = &s->nedges[node];
        for (int j = 0; j < L->n; j++) {
            int e = L->v[j];
            if (s->vis[e]) continue;
            if (s->a[e] == node) { f = e; break; }
        }
        if (f < 0) break;
        if (s->tree[list->v[i]] != s->tree[f]) { *err62 = 1; return list->n; }
        s->vis[f] = 1; s->rev[f] = 0;
        iv_push(list, f);
        node = s->b[f];
    }
    return list->n;
}

static void put32(FILE *f, int v) { if (f) { int32_t x = v; fwrite(&x, 4, 1, f); } }

/* Surfer::WriteCurves(out, binary = true, false, table, buffer) -> FUN_180017b20(.., param_7 = 0); the byte table
 * FUN_180016350 is left out (IFSilhouette's FUN_180026a40 maps the file back to these plain bytes) */
static int write_curves(Surf *s, const char *path, IfkSilResult *res)
{
    FILE *f = NULL;
    if (path) {
        f = fopen(path, "wb");
        if (!f) { set_msg(res, "Surfer error 25: I could not open the write file."); return IFK_SIL_OPEN; }
        fwrite("binary tree curves", 1, 18, f);         /* +0x7c set, param_7 = 0: 0x12 bytes */
    }
    put32(f, s->nn); put32(f, s->ne);                 /* +0x18, +0x1c */
    res->fnode = s->nn; res->fedge = s->ne;
    res->fxyz = malloc(sizeof(double) * 3 * ((size_t)s->nn + 1));
    for (int i = 0; i < s->nn; i++) {                 /* "Writing the nodes...": node list in order, 24 bytes */
        if (f) fwrite(s->xyz + 3 * i, 8, 3, f);
        memcpy(res->fxyz + 3 * i, s->xyz + 3 * i, 24);
        /* param_4 (WriteCurves' third argument) false: warnings for nodes with +0x28 == 0 */
        if (s->deg[i] < 2) res->nwarn59++;            /* "Surfer warning 59: hanging node detected in group -1" */
        if (s->deg[i] % 2 == 1 && s->deg[i] > 1) res->nwarn29++;   /* "Surfer warning 29: odd junction ..." */
    }
    /* "Writing the edges...": for each edge of +0x50 in order, while it is not visited: FUN_180019490, FUN_180019880 */
    int cap = 16, tot = 0, tcap = 64, err = 0;
    res->curve_start = malloc(sizeof(int) * (size_t)(cap + 1)); res->curve_tree = malloc(sizeof(int) * 2 * (size_t)cap);
    res->cseg = malloc(sizeof(int) * 2 * (size_t)tcap);
    res->curve_start[0] = 0;
    IVec list = { 0, 0, NULL };
    for (int e = 0; e < s->ne && !err; e++) {
        while (!s->vis[e] && !err) {
            int st = sf_pick(s, e);
            int e62 = 0;
            int n = sf_chain(s, st, &list, &e62);     /* FUN_180019880 -> FUN_18001a430 */
            if (e62) { set_msg(res, "Surfer error 62: conflicting tree groups."); err = IFK_SIL_ERR62; break; }
            /* binary: int32 count; tree: int32 id of the first edge's tree node, int32 id of its parent (the 4 bytes
             * at node +8 = NULL when there is none: 0); then per edge 8 bytes: the two node ids (+0x11 swaps them) */
            int tn = s->tree[list.v[0]];
            int pid = s->tpar[tn] >= 0 ? s->tpar[tn] + 1 : 0;
            put32(f, n); put32(f, tn + 1); put32(f, pid);
            int c = res->ncurve;
            if (c == cap) {
                cap *= 2;
                res->curve_start = realloc(res->curve_start, sizeof(int) * (size_t)(cap + 1));
                res->curve_tree = realloc(res->curve_tree, sizeof(int) * 2 * (size_t)cap);
            }
            res->curve_tree[2 * c] = tn + 1; res->curve_tree[2 * c + 1] = pid;
            for (int i = 0; i < n; i++) {
                int g = list.v[i];
                int u = s->rev[g] ? s->b[g] : s->a[g], v = s->rev[g] ? s->a[g] : s->b[g];
                put32(f, u + 1); put32(f, v + 1);
                if (tot == tcap) { tcap *= 2; res->cseg = realloc(res->cseg, sizeof(int) * 2 * (size_t)tcap); }
                res->cseg[2 * tot] = u + 1; res->cseg[2 * tot + 1] = v + 1; tot++;
            }
            res->curve_start[++res->ncurve] = tot;
        }
    }
    iv_free(&list);
    if (f) fclose(f);
    /* every edge's +0x10 reset afterwards (no effect here) */
    return err;
}

/* ================================================================================================================ */
int ifk_silhouette_run(const IfkMesh *in, double tol, const char *out_path, IfkSilResult *resp)
{
    IfkSilResult tmp, *res = resp ? resp : &tmp;
    memset(res, 0, sizeof *res);
    /* blob (IFSilhouette::Run 1800264f0): write 1, binary 1, tol, mode 1, min 0.0, max 90.0, route 0 (Surfer ->
     * SurferToSilhouette); Run l.24232: |tol - 0.1234567| <= 1e-6 -> the message and FUN_180199d54(0) (exit) */
    double d = tol <= 0.1234567 ? 0.1234567 - tol : tol - 0.1234567;
    if (d <= 1e-06) { set_msg(res, "May 10 1954 and July 27 1976."); if (!resp) ifk_sil_result_free(res); return IFK_SIL_TOLCHECK; }
    /* FUN_18001e350 grid, Surfer::ReadFile -> SurferToSilhouette slots (ifk_silhouette_walls, 11.6) */
    IfkSilWalls w;
    int rc = ifk_silhouette_walls(in->node, in->nnode, in->tri, in->ntri, 0.0, 90.0, 1, &w);
    if (rc != 0) {
        ifk_silhouette_walls_free(&w);
        set_msg(res, "IFSilhouette error 2: overflow.");
        if (!resp) ifk_sil_result_free(res);
        return IFK_SIL_OVERFLOW;
    }
    /* end slot FUN_180023f80: cut-plane set (0, 0, 1) at 0.0, the walls; PlaneCut::Run(label, 1, 0.0) with
     * PlaneCutToSilhouette (FUN_180024830, slots 7-10 = ifk_sil_classify) */
    IfkPlaneCut *pc = ifk_pc_new(GRID);
    double n[3] = { 0.0, 0.0, 1.0 };
    ifk_pc_new_cut_plane_set(pc, n);
    ifk_pc_add_cut_plane(pc, 0.0);
    ifk_pc_end_cut_plane_set(pc);
    for (int i = 0; i < w.nwnode; i++) ifk_pc_add_input_node(pc, w.wnode + 3 * i);
    for (int i = 0; i < w.nwtri; i++) ifk_pc_add_input_triangle(pc, w.wtri[3 * i], w.wtri[3 * i + 1], w.wtri[3 * i + 2], w.wtri_id[i], 0);
    ifk_pc_end_input(pc, 1);
    IfkSilClassify cl; IfkPcIO io; IfkPcTree t1;
    ifk_sil_classify_init(&cl, &io, &w, GRID);
    rc = ifk_pc_run(pc, 1, &io, &t1);
    if (getenv("IFK_TRACE_PC")) {   /* the PlaneCut output in slot order, as tools/peloader --tpif logs it (TPIF_TRACE_I11) */
        for (int i = 0; i < t1.npt; i++) fprintf(stderr, "I11 P %d %.17g %.17g %.17g\n", i, t1.pt[3 * i], t1.pt[3 * i + 1], t1.pt[3 * i + 2]);
        for (int i = 0; i < t1.nedge; i++) fprintf(stderr, "I11 E %d %d %d\n", t1.edge[4 * i], t1.edge[4 * i + 1], t1.edge[4 * i + 2]);
    }
    res->pc_nerr = pc->nerr;
    if (pc->nerr) set_msg(res, pc->err[0]);
    if (cl.err[0]) set_msg(res, cl.err);
    if (rc != 0) {
        ifk_pc_tree_free(&t1); ifk_sil_classify_free(&cl); ifk_pc_free(pc); ifk_silhouette_walls_free(&w);
        if (!resp) ifk_sil_result_free(res);
        return IFK_SIL_NOTPORTED;
    }
    /* FUN_180022af0: the input freed; the second PlaneCutToSilhouette and the Stitch2D loop */
    Tree2 t2; memset(&t2, 0, sizeof t2);
    stitch_loop(&t1, tol, &t2, res);
    ifk_pc_tree_free(&t1); ifk_sil_classify_free(&cl); ifk_pc_free(pc); ifk_silhouette_walls_free(&w);
    /* dialog slot +0x10; FUN_180025000(tree, tol * tol) */
    zero_width(&t2, tol * tol, res);
    /* the second tree as the result */
    res->nnode = t2.nn; res->parent = malloc(sizeof(int) * ((size_t)t2.nn + 1));
    for (int i = 0; i < t2.nn; i++) res->parent[i] = t2.dead[i] ? -2 : (i == 0 ? -1 : t2.par[i]);
    res->npt = t2.np; res->pt = malloc(sizeof(double) * 2 * ((size_t)t2.np + 1));
    memcpy(res->pt, t2.xy, sizeof(double) * 2 * (size_t)t2.np);
    res->nedge = 0; res->edge = malloc(sizeof(int) * 3 * ((size_t)t2.ne + 1));
    for (int e = 0; e < t2.ne; e++) {
        if (t2.dead[t2.enode[e]]) continue;
        res->edge[3 * res->nedge] = t2.ea[e]; res->edge[3 * res->nedge + 1] = t2.eb[e];
        res->edge[3 * res->nedge + 2] = t2.enode[e]; res->nedge++;
    }
    /* FUN_180024da0 through Surfer, then (blob byte write = 1; param_4 false: no CompressCurves; path not empty)
     * Surfer::WriteCurves(out, binary 1, false, table, buffer) */
    Surf s; memset(&s, 0, sizeof s);
    write_tree(&t2, &s);
    rc = write_curves(&s, out_path, res);
    sf_free(&s); t2_free(&t2);
    if (!resp) ifk_sil_result_free(res);
    return rc;
}

void ifk_sil_result_free(IfkSilResult *r)
{
    if (!r) return;
    free(r->parent); free(r->pt); free(r->edge); free(r->fxyz); free(r->curve_start); free(r->cseg); free(r->curve_tree);
    char msg[160]; memcpy(msg, r->msg, sizeof msg);
    memset(r, 0, sizeof *r);
    memcpy(r->msg, msg, sizeof msg);
}
