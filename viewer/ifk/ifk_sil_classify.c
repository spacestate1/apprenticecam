/* ifk_sil_classify.c - PlaneCutToSilhouette IO slots 7-10 (interfacial11.dll), see ifk_sil_classify.h.
 * Decompile: decomp/interfacial/i11_silhouette_all.c (FUN_180022c00, FUN_180022cb0, FUN_180022da0, FUN_180022e80,
 * FUN_180020c10, FUN_180020d90); the test point's exact slot is interfacial03 FUN_1800148b0 (ifk_pc_tp_exact).
 *
 * The test-point quadtree (FUN_180027af0 / FUN_180027bd0 query / FUN_180008b60 build) is replaced by "every test
 * point not yet covered" [inferred equivalence: the query proposes candidates per facet box; FUN_180020c10 rejects
 * points certainly outside the facet's box itself, and a point is tested at most once per facet (stamp
 * DAT_1802684e8). The classification codes are exact facts (filtered when certain, exact otherwise), so a point the
 * query does not return can only be one FUN_180020c10 would answer 0 for]. */
#include "ifk_sil_classify.h"
#include "ifk_sil_shell2d.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define U52 2.220446049250313e-16

/* ---- PcSD helpers (exact; the value is what matters, see formats/interfacial.md 11.7.1) */
static void sd_norm(PcSD *a)
{
    int lo = 0, hi = a->n - 1;
    while (hi > 0 && a->l[hi] == 0) hi--;
    while (lo < hi && a->l[lo] == 0) lo++;
    if (lo) memmove(a->l, a->l + lo, sizeof(uint32_t) * (size_t)(hi - lo + 1));
    a->e += lo; a->n = hi - lo + 1;
}
static void sd_mulx(PcSD *r, const PcSD *a, const PcSD *b)
{
    PcSD t; memset(&t, 0, sizeof t);
    if (a->n + b->n > PC_SD_L) { fprintf(stderr, "ifk_sil_classify: SuperDouble overflow\n"); abort(); }
    for (int j = 0; j < b->n; j++) {
        uint64_t carry = 0;
        for (int i = 0; i < a->n; i++) {
            uint64_t x = (uint64_t)a->l[i] * b->l[j] + t.l[i + j] + carry;
            t.l[i + j] = (uint32_t)x; carry = x >> 32;
        }
        t.l[j + a->n] = (uint32_t)carry;
    }
    t.n = a->n + b->n; t.e = a->e + b->e; t.pos = (unsigned char)(a->pos == b->pos);
    sd_norm(&t); *r = t;
}
static void sd_addx(PcSD *r, const PcSD *a, unsigned char bpos, const PcSD *b)   /* a + (b with sign bpos) */
{
    int e = a->e < b->e ? a->e : b->e;
    int ta = a->e + a->n, tb = b->e + b->n, n = (ta > tb ? ta : tb) + 1 - e;
    uint32_t A[PC_SD_L + 2] = { 0 }, B[PC_SD_L + 2] = { 0 };
    PcSD t; memset(&t, 0, sizeof t);
    if (n > PC_SD_L) { fprintf(stderr, "ifk_sil_classify: SuperDouble overflow\n"); abort(); }
    for (int i = 0; i < a->n; i++) A[a->e - e + i] = a->l[i];
    for (int i = 0; i < b->n; i++) B[b->e - e + i] = b->l[i];
    t.n = n; t.e = e;
    if (a->pos == bpos) {
        uint64_t c = 0;
        for (int i = 0; i < n; i++) { uint64_t x = (uint64_t)A[i] + B[i] + c; t.l[i] = (uint32_t)x; c = x >> 32; }
        t.pos = a->pos;
    } else {
        int i = n - 1;
        while (i >= 0 && A[i] == B[i]) i--;
        const uint32_t *X = A, *Y = B; t.pos = a->pos;
        if (i < 0) t.pos = 1;
        else if (A[i] < B[i]) { X = B; Y = A; t.pos = bpos; }
        int64_t br = 0;
        for (int k = 0; k < n; k++) { int64_t x = (int64_t)X[k] - Y[k] - br; br = x < 0; t.l[k] = (uint32_t)x; }
    }
    sd_norm(&t); *r = t;
}
static int sd_sgn(const PcSD *a) { for (int i = 0; i < a->n; i++) if (a->l[i]) return a->pos ? 1 : -1; return 0; }
/* FUN_180026d90 (double -> SuperInteger<1>: trunc(v / g), v <= 0 negative, |q| >= 2^32 overflow) followed by
 * FUN_18002c0b0 (SuperInteger<1> -> SuperDouble, value q g): built here as q x g exactly */
static void sd_from_grid(IfkSilClassify *c, PcSD *r, double v)
{
    double q = v / c->grid;
    int neg = v <= 0.0;
    if (neg) q = -q;
    if (4294967296.0 <= q) c->overflow = 1;
    uint32_t m = (uint32_t)(int64_t)q;
    PcSD Q; memset(&Q, 0, sizeof Q); Q.pos = (unsigned char)!neg; Q.n = 1; Q.l[0] = m;
    PcSD G; memset(&G, 0, sizeof G);           /* g = 2^-20 exactly: limb 2^12 at word -1 */
    int e2; frexp(c->grid, &e2); e2 -= 1;      /* g = 2^e2 (the grid is a power of two: 2^-20) */
    int we = (e2 >= 0) ? e2 / 32 : -((-e2 + 31) / 32);
    G.pos = 1; G.n = 1; G.e = we; G.l[0] = (uint32_t)1u << (e2 - 32 * we);
    sd_mulx(r, &Q, &G);
}

/* ---- slots ---------------------------------------------------------------------------------------------------- */
static void s_begin(void *u, IfkPlaneCut *pc)   /* slot 7: FUN_180022c00 */
{
    (void)pc;
    IfkSilClassify *c = u;
    c->box[0] = c->box[1] = DBL_MAX; c->box[2] = c->box[3] = -DBL_MAX;
}
static void s_add(void *u, IfkPlaneCut *pc, int tp)   /* slot 8: FUN_180022cb0 */
{
    IfkSilClassify *c = u;
    if (c->n == c->cap) {
        c->cap = c->cap ? 2 * c->cap : 64;
        c->tp = realloc(c->tp, sizeof(int) * (size_t)c->cap);
        c->fac = realloc(c->fac, sizeof(int *) * (size_t)c->cap);
        c->nfac = realloc(c->nfac, sizeof(int) * (size_t)c->cap);
    }
    c->tp[c->n] = tp; c->fac[c->n] = NULL; c->nfac[c->n] = 0;
    c->n++;                                    /* pushed on +0xa8 (push-front: the classify loop walks newest first) */
    const double *P = ifk_pc_tp_coords(pc, tp);
    if (P[0] < c->box[0]) c->box[0] = P[0];
    if (c->box[2] <= P[0] && P[0] != c->box[2]) c->box[2] = P[0];
    if (P[2] < c->box[1]) c->box[1] = P[2];
    if (c->box[3] <= P[2] && P[2] != c->box[3]) c->box[3] = P[2];
}
static void s_build(void *u, IfkPlaneCut *pc) { (void)u; (void)pc; }   /* slot 9: FUN_180022da0 quadtree (replaced) */

typedef struct { IfkSilClassify *c; IfkPlaneCut *pc; int tp; } ExCtx;
/* FUN_180020d90(a, b, P): (P.x - a.x)(b.y - a.y) - (P.y - a.y)(b.x - a.x), filtered, exact through the test
 * point's vtable slot 0 (interfacial03 FUN_1800148b0) */
static int orient_pt(IfkSilClassify *c, IfkPlaneCut *pc, int tp, const double *a, const double *b)
{
    const double *P = ifk_pc_tp_coords(pc, tp);
    double dx = b[0] - a[0], dy = b[1] - a[1];
    double tx = P[0] - a[0], ty = P[2] - a[1];
    double p1 = tx * dy, p2 = ty * dx, v = p1 - p2;
    double e = fabs(dx) * (fabs(ty) + P[3]) + fabs(p2) + fabs(dy) * (fabs(tx) + P[1]) + fabs(p1) + fabs(v);
    if (!(fabs(v) <= e * U52)) return 0.0 < v ? 1 : (v < 0.0 ? -1 : 0);
    if (e == 0.0) return 0;
    PcSD X, Y, W, sdx, sdy, ax, ay, t1, t2, l, r, o;
    ifk_pc_tp_exact(pc, tp, &X, &Y, &W);
    sd_from_grid(c, &sdx, dx); sd_from_grid(c, &sdy, dy);
    sd_from_grid(c, &ax, a[0]); sd_from_grid(c, &ay, a[1]);
    sd_mulx(&t1, &ax, &W); sd_mulx(&t2, &ay, &W);                 /* FUN_18002c1d0 x 2 */
    sd_addx(&l, &X, (unsigned char)(t1.pos == 0), &t1);          /* FUN_18001ce10: X - ax W */
    sd_addx(&r, &Y, (unsigned char)(t2.pos == 0), &t2);          /*                Y - ay W */
    sd_mulx(&t1, &l, &sdy); sd_mulx(&t2, &r, &sdx);              /* FUN_18002c3a0 x 2 */
    sd_addx(&o, &t1, (unsigned char)(t2.pos == 0), &t2);
    return sd_sgn(&o);
}
/* FUN_180020c10(facet, P, min, max): 0 outside, 3 strictly inside, 1 at a vertex, 2 on an edge */
static int pt_facet(IfkSilClassify *c, IfkPlaneCut *pc, int tp, const double *v[3], const double mn[2], const double mx[2])
{
    const double *P = ifk_pc_tp_coords(pc, tp);
    double x = P[0], y = P[2], d;
    if (x < mn[0] && (d = mn[0] - x, (d + P[1]) * U52 < d)) return 0;
    if (y < mn[1] && (d = mn[1] - y, (d + P[3]) * U52 < d)) return 0;
    if (mx[0] < x && (d = x - mx[0], (d + P[1]) * U52 < d)) return 0;
    if (mx[1] < y && (d = y - mx[1], (d + P[3]) * U52 < d)) return 0;
    int o0 = orient_pt(c, pc, tp, v[0], v[1]);
    if (0 < o0) return 0;
    int o1 = orient_pt(c, pc, tp, v[1], v[2]);
    if (0 < o1) return 0;
    int o2 = orient_pt(c, pc, tp, v[2], v[0]);
    if (0 < o2) return 0;
    if (o0 == 0) { if (o1 == 0) return 1; }
    else if (o1 != 0) return o2 != 0 ? 3 : 2;
    return o2 == 0 ? 1 : 2;
}
static void ex_cb(void *user, PcSD *X, PcSD *Y, PcSD *W)
{
    ExCtx *x = user;
    ifk_pc_tp_exact(x->pc, x->tp, X, Y, W);
}
static void s_classify(void *u, IfkPlaneCut *pc)   /* slot 10: FUN_180022e80(this, ?, 0) */
{
    IfkSilClassify *c = u;
    const IfkSilWalls *w = c->w;
    for (int f = 0; f < w->nkept; f++) {           /* +0xa0 from its head (kept_tri[0] = head) */
        const double *v[3];
        for (int k = 0; k < 3; k++) v[k] = w->node + 3 * w->kept_tri[3 * f + k];
        double mn[2], mx[2];
        for (int a = 0; a < 2; a++) {
            mn[a] = mx[a] = v[0][a];
            for (int k = 1; k < 3; k++) { if (v[k][a] < mn[a]) mn[a] = v[k][a]; if (mx[a] < v[k][a]) mx[a] = v[k][a]; }
        }
        for (int i = 0; i < c->n; i++) {
            if (c->tp[i] < 0) continue;            /* covered records leave the quadtree leaves */
            int r = pt_facet(c, pc, c->tp[i], v, mn, mx);
            if (r == 3) { c->tp[i] = -1; c->nfac[i] = 0; }          /* strictly inside: covered */
            else if (r != 0) {                                       /* on the boundary: facet listed (push-front) */
                c->fac[i] = realloc(c->fac[i], sizeof(int) * (size_t)(c->nfac[i] + 1));
                c->fac[i][c->nfac[i]++] = f;
            }
        }
    }
    IfkS2 *s = ifk_s2_new(c->grid);                /* FUN_1800043f0(DAT_180268368) */
    int *vcache = malloc(sizeof(int) * (size_t)(w->nnode > 0 ? w->nnode : 1));   /* node +0x30 */
    for (int k = 0; k < w->nnode; k++) vcache[k] = -1;
    for (int i = c->n - 1; i >= 0; i--) {          /* +0xa8 from its head: newest record first */
        int tp = c->tp[i];
        if (tp < 0) continue;
        if (c->nfac[i] <= 1) { ifk_pc_tp_passed(pc, tp); continue; }   /* none / one facet listed: passed */
        c->nshell++;
        ifk_s2_begin(s);                           /* +0x60 = FUN_18000a1a0(+0xc8) */
        for (int j = c->nfac[i] - 1; j >= 0; j--) {                    /* listed facets from the head */
            const int *t = w->kept_tri + 3 * c->fac[i][j];
            for (int k = 0; k < 3; k++)
                if (vcache[t[k]] < 0) vcache[t[k]] = ifk_s2_vertex(s, w->node[3 * t[k]], w->node[3 * t[k] + 1]);
            ifk_s2_segment(s, vcache[t[0]], vcache[t[1]]);             /* FUN_1800049a0 x 3 */
            ifk_s2_segment(s, vcache[t[1]], vcache[t[2]]);
            ifk_s2_segment(s, vcache[t[2]], vcache[t[0]]);
        }
        ifk_s2_end(s);                             /* FUN_180008ed0(+0x58, +0x60) */
        int pr = ifk_s2_process(s);                /* FUN_180004a50 */
        if (pr != 1) c->nshell_fail++;
        ExCtx x = { c, pc, tp };
        if (ifk_s2_point_on_output(s, ifk_pc_tp_coords(pc, tp), ex_cb, &x)) ifk_pc_tp_passed(pc, tp);   /* FUN_180021830 */
        const char *e = ifk_s2_error(s);
        if (e && !c->err[0]) snprintf(c->err, sizeof c->err, "%s", e);
        for (int j = 0; j < c->nfac[i]; j++) {     /* node +0x30 caches cleared */
            const int *t = w->kept_tri + 3 * c->fac[i][j];
            vcache[t[0]] = vcache[t[1]] = vcache[t[2]] = -1;
        }
        ifk_s2_reset(s);                           /* FUN_180007cc0 */
    }
    free(vcache);
    ifk_s2_free(s);
}

void ifk_sil_classify_init(IfkSilClassify *c, IfkPcIO *io, const IfkSilWalls *w, double grid)
{
    memset(c, 0, sizeof *c);
    c->w = w; c->grid = grid;
    io->begin = s_begin; io->add = s_add; io->build = s_build; io->classify = s_classify; io->user = c;
}
void ifk_sil_classify_free(IfkSilClassify *c)
{
    for (int i = 0; i < c->n; i++) free(c->fac[i]);
    free(c->fac); free(c->nfac); free(c->tp);
    memset(c, 0, sizeof *c);
}
