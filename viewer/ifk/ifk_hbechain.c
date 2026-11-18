/* ifk_hbechain.c - port of mcmsurf.dll FUN_180232540 (HBE file -> Mastercam curves and chains), the last step of
 * mucbMakeIffSiluetBndy (Wireframe Turn Profile / Silhouette Boundary). formats/interfacial.md 14.2.
 * Sources (decomp/mcmsurf_all.c unless noted; [asm] = objdump of ~/mastercam-re/files/mcmsurf.dll):
 *  - FUN_180232540 (the record loop), FUN_1802323a0 (flush), FUN_18022fbd0 (densify), FUN_180230b60 (tangents),
 *    FUN_18020b970 (angle test), the point list CMutlDlpDir: FUN_180209760, FUN_180215700, FUN_18021e5d0 (duplicate
 *    test on, tol), FUN_18020ad20 / FUN_180213040 (add at the tail / after a node, [asm] the duplicate test FUN_18021d2a0
 *    against that node with FUN_180239180 = dy^2 + dx^2 + dz^2 < tol^2), FUN_18023f2c0 / FUN_18023f250 (insert),
 *    FUN_180210220 / FUN_180210b20 / FUN_1802110a0 / FUN_180210b70 / FUN_18021d270 (walk), FUN_180211650 (tail point),
 *    FUN_18020d570 -> FUN_18023a770 (reset), FUN_180242820 (midpoint), FUN_18002dcd0 (unit vector, 0 stays 0).
 *  - mucbMakeMcSpline 180232c00, mucbMakeMcArc3D 1802321e0, mucbAddCurveToChain 18022f730.
 *  - mccore: make_spline_from_pts 180667d30 (decomp/surfloft/loft3.c), make_spline_coefs_open / _closed (ported in
 *    viewer/mcspline_fit.c), calc_chord_ht 1805eca80 (copied from viewer/mcsurf_tonurbs.c), constr_arc_new 1804b5830
 *    (viewer/ifk/ifk_hbefilter.c), add_curve_to_chain 1801db260, set_chain_closed 1801e8590, chain_endpoint 1801dde80,
 *    last_chain_curve 1801e6690, FUN_1801e61d0, FUN_1801e6b50, FUN_1801dddb0 (decomp/mccore_chain_range.c).
 *  - Mastercam.dll: arc_2d_to_3d 180012ae0 (decomp/draft/core_drawwl.c), arc_ep_3d 18032d730 -> systypes.dll arc_ep_3d
 *    180001210, view_to_world 180341520, world_to_view 180341b30.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ifk_hbechain.h"
#include "../mcspline_fit.h"

int ifk_constr_arc_new(double systol, const double *p1, const double *p2, const double *c, int cw, double rtol, double out[5]);   /* ifk_hbefilter.c */

/* ---------------------------------------------------------------- small helpers */
static void unit(double v[3])                       /* FUN_18002dcd0 */
{
    double x = v[0], y = v[1], z = v[2], l = sqrt(x * x + y * y + z * z);
    if (l == 0.0) { v[0] = v[1] = v[2] = 0.0; return; }
    v[0] = x / l; v[1] = y / l; v[2] = z / l;
}
static void view_to_world(const double *v, const IfkHbeChainParams *p, double *w)   /* Mastercam.dll 180341520 */
{
    if (p->view < 2) { w[0] = v[0]; w[1] = v[1]; w[2] = v[2]; return; }
    const double *m = p->vm;   /* matrix +8.. = rows: x' = y m[3] + x m[0] + z m[6] (decompile order) */
    double x = v[0], y = v[1], z = v[2];
    w[0] = y * m[3] + x * m[0] + z * m[6];
    w[1] = x * m[1] + y * m[4] + z * m[7];
    w[2] = x * m[2] + y * m[5] + z * m[8];
}
static void world_to_view(const double *w, const IfkHbeChainParams *p, double *v)   /* Mastercam.dll 180341b30 */
{
    if (p->view < 2) { v[0] = w[0]; v[1] = w[1]; v[2] = w[2]; return; }
    const double *m = p->vm;
    for (int i = 0; i < 3; i++) v[i] = m[3 * i] * w[0] + m[3 * i + 1] * w[1] + m[3 * i + 2] * w[2];
}
/* calc_chord_ht (mccore 0x1805eca80), as ported in viewer/mcsurf_tonurbs.c */
static double chord_ht(const double p0[3], const double t0[3], const double p1[3], const double t1[3])
{
    double d[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    double l = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    double lt0 = sqrt(t0[0] * t0[0] + t0[1] * t0[1] + t0[2] * t0[2]), lt1 = sqrt(t1[0] * t1[0] + t1[1] * t1[1] + t1[2] * t1[2]);
    double c0 = (t0[0] * d[0] + t0[1] * d[1] + t0[2] * d[2]) / (lt0 * l), c1 = (t1[0] * d[0] + t1[1] * d[1] + t1[2] * d[2]) / (lt1 * l);
    if (!(c0 <= 1.0)) c0 = 1.0; else if (c0 < -1.0) c0 = -1.0;
    if (!(c1 <= 1.0)) c1 = 1.0; else if (c1 < -1.0) c1 = -1.0;
    return (fabs(tan(acos(c1))) + fabs(tan(acos(c0)))) * l * 0.25;
}

/* ---------------------------------------------------------------- the point list (CMutlDlpDir, forward: +0x69 = 1) */
typedef struct { double p[3]; int next, prev, live; } Node;   /* node [0] next, [1] prev, +0x10 point */
typedef struct {
    Node *nd; int nnd, cap;
    int head, tail, count;    /* list +8, +0x10, +0x30 */
    int last_added;           /* CMutlDlpDir +0x48: the node the last add returned (kept on a duplicate; not cleared by a reset) */
    double tol2;              /* +0x60 = tol^2 (FUN_18021e5d0) */
} PList;

static int pl_new(PList *L, const double p[3])
{
    if (L->nnd == L->cap) { int c = L->cap ? 2 * L->cap : 256; Node *n = realloc(L->nd, sizeof *n * (size_t)c); if (!n) return -1; L->nd = n; L->cap = c; }
    Node *n = &L->nd[L->nnd]; memcpy(n->p, p, 24); n->next = n->prev = -1; n->live = 1;
    return L->nnd++;
}
static int dup_of(const PList *L, const double p[3], int node)   /* FUN_18021d2a0 [asm]: FUN_180239180(node + 0x10, p) < tol^2 */
{
    if (node < 0) return 0;
    const double *q = L->nd[node].p;
    double dy = p[1] - q[1], dx = p[0] - q[0], dz = p[2] - q[2];
    double d2 = dy * dy + dx * dx; d2 += dz * dz;
    return d2 < L->tol2;
}
/* insert after `after` (-1: at the head of an empty list): FUN_18023f2c0 + FUN_18023f250 */
static int pl_insert(PList *L, const double p[3], int after)
{
    int k = pl_new(L, p); if (k < 0) return -2;
    int nx = after < 0 ? -1 : L->nd[after].next;
    L->nd[k].next = nx; L->nd[k].prev = after;
    if (after >= 0) { L->nd[after].next = k; if (nx < 0) L->tail = k; else L->nd[nx].prev = k; }
    else { L->head = k; if (nx < 0) L->tail = k; else L->nd[nx].prev = k; }
    L->count++;
    return k;
}
static int pl_add(PList *L, const double p[3])       /* FUN_18020ad20: at the tail, skipped when within tol of the tail */
{
    if (!dup_of(L, p, L->tail)) { int k = pl_insert(L, p, L->tail); if (k < -1) return -1; L->last_added = k; }
    return 0;
}
static int pl_add_after(PList *L, const double p[3], int after)   /* FUN_180213040: returns +0x48 (stale on a duplicate) */
{
    if (!dup_of(L, p, after)) { int k = pl_insert(L, p, after); if (k < -1) return -2; L->last_added = k; }
    return L->last_added;
}
static void pl_reset(PList *L)                        /* FUN_18020d570 -> FUN_18023a770: the nodes go to the free list */
{
    for (int k = L->head; k >= 0; k = L->nd[k].next) L->nd[k].live = 0;
    L->head = L->tail = -1; L->count = 0;
}

/* ---------------------------------------------------------------- output */
static int ent_new(IfkHbeChains *o)
{
    if (o->nents == o->capents) { int c = o->capents ? 2 * o->capents : 64; IfkCurve *e = realloc(o->ents, sizeof *e * (size_t)c); if (!e) return -1; o->ents = e; o->capents = c; }
    memset(&o->ents[o->nents], 0, sizeof o->ents[0]);
    return o->nents++;
}
typedef struct { IfkChain cur; int have; IfkChain *closed; int nclosed, capclosed; } ChainSt;

static int chain_push(IfkChain *c, int at_head, int ent, int ep)
{
    if (c->n == c->cap) { int k = c->cap ? 2 * c->cap : 16; IfkChainItem *it = realloc(c->it, sizeof *it * (size_t)k); if (!it) return -1; c->it = it; c->cap = k; }
    if (at_head) { memmove(c->it + 1, c->it, sizeof *c->it * (size_t)c->n); c->it[0] = (IfkChainItem){ ent, ep }; }
    else c->it[c->n] = (IfkChainItem){ ent, ep };
    c->n++;
    return 0;
}
/* FUN_1801e61d0: the point of an entity at end ep (ep != 0: +0x80, else +0x68) */
static const double *ent_pt(const IfkHbeChains *o, int e, int ep) { return ep ? o->ents[e].p1 : o->ents[e].p0; }
static double d3(const double *a, const double *b)   /* mucbAddCurveToChain's distances (decompile order y, x, z) */
{
    double dy = a[1] - b[1], dx = a[0] - b[0], dz = a[2] - b[2];
    return sqrt(dy * dy + dx * dx + dz * dz);
}
/* mucbAddCurveToChain 18022f730 with add_curve_to_chain / set_chain_closed / append_chain */
static int add_to_chain(IfkHbeChains *o, ChainSt *cs, int e, const IfkHbeChainParams *p)
{
    int ep = 0, append = 1;
    if (!cs->have) { memset(&cs->cur, 0, sizeof cs->cur); cs->have = 1; }   /* RecordBoundaries_Add_Chain */
    else {
        const double *s = o->ents[e].p0, *t = o->ents[e].p1;        /* curve_endpoints */
        IfkChain *c = &cs->cur;
        const double *c0 = ent_pt(o, c->it[0].ent, c->it[0].ep);                    /* chain_endpoint(chain, 0) */
        const double *c1 = ent_pt(o, c->it[c->n - 1].ent, !c->it[c->n - 1].ep);     /* chain_endpoint(chain, 1): FUN_1801e6b50 flips */
        double nc = p->nc_tol;
        if (d3(c1, s) < nc) ep = 0;
        else if (d3(c1, t) < nc) ep = 1;
        else if (d3(c0, s) < nc) { ep = 0; append = 0; }
        else if (d3(c0, t) < nc) { ep = 1; append = 0; }
        else ep = 0;
    }
    /* add_curve_to_chain: appended with +0x18 = ep, or prepended with +0x18 = FUN_1801e6b50(ep) (the other end) */
    if (chain_push(&cs->cur, !append, e, append ? ep : !ep)) return -1;
    /* set_chain_closed(chain, nc_tol): FUN_1801dddb0(start, end) < nc_tol */
    IfkChain *c = &cs->cur;
    double a[3], b[3];
    memcpy(a, ent_pt(o, c->it[0].ent, c->it[0].ep), 24);
    memcpy(b, ent_pt(o, c->it[c->n - 1].ent, !c->it[c->n - 1].ep), 24);
    double d;
    if (p->chain_ignore_depths) {     /* world_to_2d in the active view */
        double va[3], vb[3]; world_to_view(a, p, va); world_to_view(b, p, vb);
        d = sqrt((vb[0] - va[0]) * (vb[0] - va[0]) + (vb[1] - va[1]) * (vb[1] - va[1]));
    } else d = sqrt((b[1] - a[1]) * (b[1] - a[1]) + (b[0] - a[0]) * (b[0] - a[0]) + (b[2] - a[2]) * (b[2] - a[2]));
    c->closed = d < p->nc_tol;
    if (c->closed) {                   /* moved to the output list (append_chain), a new chain starts with the next curve */
        if (cs->nclosed == cs->capclosed) { int k = cs->capclosed ? 2 * cs->capclosed : 8; IfkChain *n = realloc(cs->closed, sizeof *n * (size_t)k); if (!n) return -1; cs->closed = n; cs->capclosed = k; }
        cs->closed[cs->nclosed++] = *c;
        memset(c, 0, sizeof *c); cs->have = 0;
    }
    return 0;
}

/* mucbMakeMcSpline 180232c00: n == 2 a line, n < 2 nothing, else make_spline_from_pts(pts, n, main colour, main level, 2, t0, 2, t1, 0)
   with t0 = unit(P1 - P0), t1 = unit(Pn-1 - Pn-2). Returns the entity or -1 (none), -2 memory */
static int make_spline(IfkHbeChains *o, const double (*P)[3], int n, const IfkHbeChainParams *p)
{
    if (n < 2) return -1;
    if (n == 2) {
        int e = ent_new(o); if (e < 0) return -2;
        IfkCurve *c = &o->ents[e]; c->type = IFK_ENT_LINE; memcpy(c->p0, P[0], 24); memcpy(c->p1, P[1], 24);
        return e;
    }
    double t0[3] = { P[1][0] - P[0][0], P[1][1] - P[0][1], P[1][2] - P[0][2] }, t1[3] = { P[n - 1][0] - P[n - 2][0], P[n - 1][1] - P[n - 2][1], P[n - 1][2] - P[n - 2][2] };
    unit(t0); unit(t1);
    if (p->max_spline_pts < n) return -1;    /* make_spline_from_pts: message 0x1812, nothing (not reached: chunks <= max) */
    const double *a = P[0], *b = P[n - 1];
    double cl = sqrt((b[1] - a[1]) * (b[1] - a[1]) + (b[0] - a[0]) * (b[0] - a[0]) + (b[2] - a[2]) * (b[2] - a[2]));
    int closed = cl < p->tol;
    double *coef = malloc(sizeof(double) * 12 * (size_t)n);
    if (!coef) return -2;
    int ok = closed ? mcsf_coefs_closed(coef, P, n, p->tol) : mcsf_coefs_open(coef, P, n, 2, t0, 2, t1, p->tol);
    if (!ok) { free(coef); return -1; }        /* a chord below tol (message 0x16ee): no entity */
    int e = ent_new(o); if (e < 0) { free(coef); return -2; }
    IfkCurve *c = &o->ents[e];
    c->type = IFK_ENT_SPLINE; c->closed = closed; c->nnode = n; c->coef = coef;
    c->node = malloc(24 * (size_t)n); if (!c->node) return -2;
    memcpy(c->node, P, 24 * (size_t)n);
    memcpy(c->t0, t0, 24); memcpy(c->t1, t1, 24);
    /* put_spline_in_db (mccore 1802daee0, decomp/surfloft/loft3.c): +0x68 / +0x80 = eval_spline at t = 0 and t = nseg, Horner from the top
       (viewer/mcspline.c eval_spline): a0, and ((a3 + a2) + a1) + a0 of the last segment */
    const double *L = coef + 12 * (size_t)(n - 2);
    for (int k = 0; k < 3; k++) {
        double a = 0.0, b = 0.0;
        for (int j = 3; j >= 0; j--) { a = a * 0.0 + coef[3 * j + k]; b = b * 1.0 + L[3 * j + k]; }
        c->p0[k] = a; c->p1[k] = b;
    }
    return e;
}

/* mucbMakeMcArc3D 1802321e0: constr_arc_new(start, end, centre (x, y), cw, tol) -> arc_2d_to_3d(view, depth = centre z) -> arc_ep_3d */
static int make_arc(IfkHbeChains *o, const double *s, const double *t, const double *c, int cw, const IfkHbeChainParams *p)
{
    double a[5];
    if (!ifk_constr_arc_new(p->tol, s, t, c, cw, p->tol, a)) return -1;
    int e = ent_new(o); if (e < 0) return -2;
    IfkCurve *k = &o->ents[e];
    k->type = IFK_ENT_ARC; k->view = p->view;
    k->c[0] = a[0]; k->c[1] = a[1]; k->c[2] = c[2]; k->r = a[2]; k->sa = a[3]; k->sw = a[4];
    /* systypes arc_ep_3d 180001210 with the view's matrix (Mastercam.dll arc_ep_3d: mcViews[view] +8; Top = the identity) */
    double sa = k->sa, sw = k->sw, r = k->r, d = k->c[2];
    double cs = cos(sa), sn = sin(sa);
    double x0 = cs * r + k->c[0], y0 = sn * r + k->c[1];
    double ce = cos(sw + sa), se = sin(sw + sa);
    double x1 = ce * r + k->c[0], y1 = se * r + k->c[1];
    static const double I[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    const double *m = p->view == 1 ? I : p->vm;
    k->p0[0] = y0 * m[3] + x0 * m[0] + d * m[6]; k->p0[1] = y0 * m[4] + x0 * m[1] + d * m[7]; k->p0[2] = x0 * m[2] + y0 * m[5] + d * m[8];
    k->p1[0] = y1 * m[3] + x1 * m[0] + d * m[6]; k->p1[1] = y1 * m[4] + x1 * m[1] + d * m[7]; k->p1[2] = x1 * m[2] + y1 * m[5] + d * m[8];
    return e;
}

/* FUN_180230b60: t0 / t1 of the segment node -> next */
static int tangents(const PList *L, int nd, double t0[3], double t1[3])
{
    int nx = L->nd[nd].next; if (nx < 0) return 0;              /* FUN_180210220 needs the next node */
    const double *p0 = L->nd[nd].p, *p1 = L->nd[nx].p;
    int pv = L->nd[nd].prev;
    if (pv >= 0) for (int k = 0; k < 3; k++) t0[k] = p0[k] - L->nd[pv].p[k];
    else for (int k = 0; k < 3; k++) t0[k] = p1[k] - p0[k];
    unit(t0);
    int n2 = L->nd[nx].next;
    if (n2 >= 0) for (int k = 0; k < 3; k++) t1[k] = L->nd[n2].p[k] - p1[k];
    else for (int k = 0; k < 3; k++) t1[k] = p1[k] - p0[k];
    unit(t1);
    return 1;
}
/* FUN_18022fbd0(depth, list, node, chord_dev, &max) */
static int densify(PList *L, int depth, int nd, double dev, double *mx, int *undef)
{
    double t0[3], t1[3];
    if (depth <= 0 || nd < 0) return 0;
    if (!L->nd[nd].live) { *undef = 1; return 0; }   /* a stale node (see below): the original reads freed memory */
    if (!tangents(L, nd, t0, t1)) return 0;
    double p0[3], p1[3]; memcpy(p0, L->nd[nd].p, 24); memcpy(p1, L->nd[L->nd[nd].next].p, 24);
    double h = chord_ht(p0, t0, p1, t1);
    if (h <= dev) { if (*mx < h) *mx = h; return 0; }
    double m[3];   /* FUN_180242820 */
    m[0] = (p1[0] - p0[0]) * 0.5 + p0[0]; m[1] = (p1[1] - p0[1]) * 0.5 + p0[1]; m[2] = (p1[2] - p0[2]) * 0.5 + p0[2];
    int k = pl_add_after(L, m, nd);
    if (k == -2) return -1;
    if (k >= 0) {
        /* a midpoint within tol of the node returns +0x48 unchanged: the last node added anywhere before (14.2.3) */
        if (densify(L, depth - 1, nd, dev, mx, undef)) return -1;
        if (densify(L, depth - 1, k, dev, mx, undef)) return -1;
    }
    return 0;
}
/* FUN_1802323a0(list, curve_chord_dev, ...): densify (more than 2 points), then the points in chunks of max_n_spline_pts */
static int flush(PList *L, IfkHbeChains *o, ChainSt *cs, const IfkHbeChainParams *p)
{
    int made = 0;
    if (2 < L->count) {
        double mx = -1.0;
        for (int nd = L->head; nd >= 0;) { int nx = L->nd[nd].next; if (densify(L, 20, nd, p->chord_dev, &mx, &o->undefined_hit)) return -1; nd = nx; }
    }
    int maxn = p->max_spline_pts;
    double (*buf)[3] = malloc(24 * (size_t)(maxn > 0 ? maxn : 1));
    if (!buf) return -1;
    int nb = 0;
    for (int nd = L->head; nd >= 0; nd = L->nd[nd].next) {
        memcpy(buf[nb++], L->nd[nd].p, 24);
        if (nb == maxn) {
            int e = make_spline(o, (const double (*)[3])buf, nb, p);
            if (e == -2) { free(buf); return -1; }
            if (e >= 0) { made = 1; if (add_to_chain(o, cs, e, p)) { free(buf); return -1; } }
            memcpy(buf[0], buf[nb - 1], 24); nb = 1;
        }
    }
    if (0 < nb) {
        int e = make_spline(o, (const double (*)[3])buf, nb, p);
        if (e == -2) { free(buf); return -1; }
        if (e >= 0) { if (add_to_chain(o, cs, e, p)) { free(buf); return -1; } made = 1; }
    }
    free(buf);
    return made;
}
/* FUN_18020b970: the turn at the tail within maxang */
static int angle_ok(const PList *L, const double q[3], double maxang)
{
    if (L->count < 2) return 1;
    if (!(0.0 <= maxang)) return 0;
    const double *b = L->nd[L->tail].p, *a = L->nd[L->nd[L->tail].prev].p;
    double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { q[0] - b[0], q[1] - b[1], q[2] - b[2] };
    unit(u); unit(v);
    double ang = acos(v[1] * u[1] + v[0] * u[0] + v[2] * u[2]);
    return fabs(ang) <= maxang;
}

int ifk_hbe_to_chains(const IfkHbe68 *rec, int n, const IfkHbeChainParams *p, IfkHbeChains *out)
{
    memset(out, 0, sizeof *out);
    PList L; memset(&L, 0, sizeof L); L.head = L.tail = L.last_added = -1; L.tol2 = p->tol * p->tol;
    ChainSt cs; memset(&cs, 0, sizeof cs);
    int made = 0, rc = 0, r;
    for (int i = 0; i < n; i++) {
        IfkHbe68 R = rec[i];
        if (R.first && 0 < L.count) { if ((r = flush(&L, out, &cs, p)) < 0) goto fail; made = r; pl_reset(&L); }
        if (R.type == 1) {
            if (p->flat) { R.p0[2] = p->depth; R.p1[2] = p->depth; }
            double w[3], e[3];
            if (L.count == 0) { view_to_world(R.p0, p, w); if (pl_add(&L, w)) goto fail; }
            view_to_world(R.p1, p, e);
            if (1 < L.count && !angle_ok(&L, e, p->maxang)) {
                if ((r = flush(&L, out, &cs, p)) < 0) goto fail;
                made = r;
                double last[3]; memcpy(last, L.nd[L.tail].p, 24);
                pl_reset(&L); if (pl_add(&L, last)) goto fail;
            }
            if (pl_add(&L, e)) goto fail;
            if (L.count == p->max_spline_pts) {
                if ((r = flush(&L, out, &cs, p)) < 0) goto fail;
                made = r;
                double last[3]; memcpy(last, L.nd[L.tail].p, 24);
                pl_reset(&L); if (pl_add(&L, last)) goto fail;
            }
        } else if (R.type == 2) {
            if (p->flat) { R.p0[2] = p->depth; R.p1[2] = p->depth; R.c[2] = p->depth; }
            int cnt = L.count;
            if (1 < cnt) { if ((r = flush(&L, out, &cs, p)) < 0) goto fail; made = r; }
            if (0 < cnt) pl_reset(&L);
            int e = make_arc(out, R.p0, R.p1, R.c, R.n[2] < 0.0, p);   /* the record's own (view) coordinates */
            if (e == -2) goto fail;
            if (e >= 0) { made = 1; if (add_to_chain(out, &cs, e, p)) goto fail; }
            double w[3]; view_to_world(R.p1, p, w);
            if (pl_add(&L, w)) goto fail;
        }
    }
    if (1 < L.count) { if ((r = flush(&L, out, &cs, p)) < 0) goto fail; made = r; pl_reset(&L); }
    out->made = made;
    if (made) {
        if (cs.nclosed) {
            out->chains = cs.closed; out->nchains = cs.nclosed; cs.closed = NULL;
            if (cs.have) { out->nopen_dropped = cs.cur.n; free(cs.cur.it); }
        } else if (cs.have) {
            out->chains = malloc(sizeof *out->chains); if (!out->chains) goto fail;
            out->chains[0] = cs.cur; out->nchains = 1;
        }
    } else { if (cs.have) free(cs.cur.it); for (int i = 0; i < cs.nclosed; i++) free(cs.closed[i].it); }
    free(cs.closed); free(L.nd);
    return rc;
fail:
    free(L.nd); free(cs.closed);
    return -1;
}

void ifk_hbe_chains_free(IfkHbeChains *o)
{
    for (int i = 0; i < o->nents; i++) { free(o->ents[i].node); free(o->ents[i].coef); }
    free(o->ents);
    for (int i = 0; i < o->nchains; i++) free(o->chains[i].it);
    free(o->chains);
    memset(o, 0, sizeof *o);
}

int ifk_hbe_read(const char *path, IfkHbe68 **rec)
{
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    int n = 0, cap = 0; IfkHbe68 *r = NULL, x;
    typedef char sz[sizeof(IfkHbe68) == 0x68 ? 1 : -1]; (void)sizeof(sz);
    while (fread(&x, 0x68, 1, f) == 1) {
        if (n == cap) { cap = cap ? 2 * cap : 64; IfkHbe68 *q = realloc(r, sizeof *q * (size_t)cap); if (!q) { free(r); fclose(f); return -1; } r = q; }
        r[n++] = x;
    }
    fclose(f);
    *rec = r;
    return n;
}
