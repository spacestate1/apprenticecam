/* ifk_planecut.c - PlaneCut (interfacial03.dll), stage A: port of
 *   PlaneCut ctor 180021130 / FUN_180041800 (grid), NewCutPlaneSet FUN_180025350, AddCutPlane FUN_1800255e0,
 *   EndCutPlaneSet 180021020, AddInputNode 180021b20, AddInputTriangle 180021be0, EndInput 180021d80,
 *   Run FUN_180025910 (preprocessing + the per-plane loop) up to and including FUN_180028c20:
 *   FUN_180026140 (cut), FUN_1800267e0, FUN_180026a30, FUN_18001b800, FUN_180028aa0 / FUN_180030470 (points),
 *   FUN_180027060 / FUN_180027500 (pair intersections), FUN_180028580 / FUN_1800285f0, FUN_180028830 (collinear),
 *   FUN_180029390, FUN_180013330, FUN_180028090, FUN_180001040, FUN_180028230 (exact orientation),
 *   FUN_18001bde0 (exact cut-node coordinates), FUN_180028c20, FUN_180029150, FUN_1800322c0, FUN_18001d780,
 *   FUN_18001d8d0 (piece flags), FUN_18001e4d0.
 * Decompile: decomp/interfacial/i03_planecut_all.c (functions start with "// ==== NAME @ ADDR"), quadtree slots
 * decomp/interfacial/i03_qtree.c, disassembly of FUN_180027500 decomp/interfacial/i03_dis_27500.txt.
 * Write-up: formats/interfacial.md 11.7.
 *
 * Scope: axis-aligned cut planes with a non-zero z normal (the only set Silhouette Boundary uses: normal (0,0,1),
 * interfacial11 FUN_180023f80). Not ported (refused with -1): other normals (FUN_180012180, FUN_18000dfb0,
 * FUN_180016e20, the non-aligned branches of FUN_18001b800 / FUN_18001bde0 / FUN_180027500 / FUN_18001d8d0, and
 * FUN_18002a610 for vertical planes). The quadtree FUN_180026b60 / FUN_18002fee0 is replaced (see pc_pairs).
 *
 * ORDERS FROM ADDRESSES: the DLL compares record pointers in three places that change results: InputEdge ends
 * (EndInput: smaller node pointer first; FUN_18001b800 interpolates from that end), graph-edge ends
 * (FUN_180029150: smaller CutNodeCoordinate pointer = [0]) and lookups keyed by pointers (results independent of
 * the order). The interfacial03 MemoryPool hands out records from a free list that a new block of 1000 fills
 * from its first to its last record (FUN_18000d200, FUN_1800267e0 inline, FUN_1800313f0), so a fresh block
 * gives DECREASING addresses and a freed record is reused first (LIFO) [code]. We emulate this: key = block x 1000
 * + slot, later blocks assumed at higher addresses [assumption: heap order between blocks is not knowable]. This is
 * the section-8 convention (creation order stands in for addresses) refined by the decoded pool order. */
#include "ifk_planecut.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define U52 2.220446049250313e-16       /* 2^-52, the filter constant used throughout (e.g. FUN_180026140) */
#define U53 1.1102230246251565e-16      /* 2^-53, FUN_180001040 / FUN_180028090 */
#define POOL 1000                       /* MemoryPool block size (impl ctor FUN_180024420, l.2396 of the dump) */

/* ================================================================== small helpers (ours) */
static void lpush(PcList *l, int v)
{
    if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 4; l->v = realloc(l->v, (size_t)l->cap * sizeof(int)); }
    l->v[l->n++] = v;
}
static void lfree(PcList *l) { free(l->v); l->v = NULL; l->n = l->cap = 0; }
#define GROW(arr, n, cap) do { if ((n) == (cap)) { (cap) = (cap) ? 2 * (cap) : 64; \
    (arr) = realloc((arr), (size_t)(cap) * sizeof *(arr)); } } while (0)

static void pc_error(IfkPlaneCut *pc, const char *msg)   /* dialog vtable slot +0x40 */
{
    if (pc->nerr < 8) snprintf(pc->err[pc->nerr], sizeof pc->err[0], "%s", msg);
    pc->nerr++;
}

/* open-addressing map int64 -> int (ours; replaces the DLL's pointer-keyed trees, lookup only) */
typedef struct { long long *k; int *v; int cap, n; } Map;
static Map *map_new(void) { Map *m = calloc(1, sizeof *m); m->cap = 1024; m->k = malloc(sizeof(long long) * 1024);
    m->v = malloc(sizeof(int) * 1024); for (int i = 0; i < 1024; i++) m->k[i] = -1; return m; }
static void map_free(Map *m) { if (m) { free(m->k); free(m->v); free(m); } }
static unsigned map_h(long long k, int cap) { unsigned long long x = (unsigned long long)k * 0x9e3779b97f4a7c15ULL;
    return (unsigned)(x >> 32) & (unsigned)(cap - 1); }
static int map_get(Map *m, long long k)
{
    for (unsigned i = map_h(k, m->cap);; i = (i + 1) & (unsigned)(m->cap - 1)) {
        if (m->k[i] == -1) return -1;
        if (m->k[i] == k) return m->v[i];
    }
}
static void map_put(Map *m, long long k, int v)
{
    if (2 * (m->n + 1) > m->cap) {
        Map t = { malloc(sizeof(long long) * (size_t)m->cap * 2), malloc(sizeof(int) * (size_t)m->cap * 2), m->cap * 2, 0 };
        for (int i = 0; i < t.cap; i++) t.k[i] = -1;
        for (int i = 0; i < m->cap; i++) if (m->k[i] != -1) {
            unsigned j = map_h(m->k[i], t.cap);
            while (t.k[j] != -1) j = (j + 1) & (unsigned)(t.cap - 1);
            t.k[j] = m->k[i]; t.v[j] = m->v[i]; t.n++;
        }
        free(m->k); free(m->v); *m = t;
    }
    unsigned i = map_h(k, m->cap);
    while (m->k[i] != -1 && m->k[i] != k) i = (i + 1) & (unsigned)(m->cap - 1);
    if (m->k[i] == -1) m->n++;
    m->k[i] = k; m->v[i] = v;
}

/* ================================================================== SuperInteger<n> (fixed size)
 * Layout as in the DLL: sign byte (1 = positive; zero from an equal-magnitude subtraction gets 1), 32-bit limbs,
 * least significant first. Same computation as the Shell2D layer of ifk_spinsil.c (big_addsub = FUN_18000ed50 /
 * mag_sub = FUN_18000f090 / big_mul = FUN_18000eac0), see formats/interfacial.md 11.7.1. */
static PcSI si_from_double(IfkPlaneCut *pc, double v)   /* FUN_180008dc0 (= interfacial11 FUN_180026d90) */
{
    PcSI r; memset(&r, 0, sizeof r);
    double q = v / pc->grid;
    if (v <= 0.0) {
        r.pos = 0;
        if (q <= -4294967296.0) pc->overflow = 1;
        q = -q;
    } else {
        r.pos = 1;
        if (4294967296.0 <= q) pc->overflow = 1;
    }
    r.l[0] = (uint32_t)(int64_t)q;      /* *(int *)(param_1 + 4) = (int)(longlong)dVar1 */
    return r;
}
static void mag_sub(uint32_t *d, const uint32_t *a, const uint32_t *b, int top)   /* FUN_180018490 */
{
    int borrow = a[0] < b[0];
    d[0] = a[0] - b[0];
    for (int i = 1; i <= top; i++) {
        if (borrow) { borrow = (a[i] == 0) || (a[i] - 1 < b[i]); d[i] = a[i] - b[i] - 1; }
        else { borrow = a[i] < b[i]; d[i] = a[i] - b[i]; }
    }
}
/* r = a + (b with sign byte bpos), n limbs: FUN_18001b700 (n = 1), FUN_180017c90 (2), FUN_180018190 /
 * FUN_180018320 (3), FUN_18001fe90 (4), FUN_1800200f0 (5); carry out of the top limb sets DAT_18008f680 */
static PcSI si_add(IfkPlaneCut *pc, const PcSI *a, unsigned char bpos, const PcSI *b, int n)
{
    PcSI r; memset(&r, 0, sizeof r);
    if (a->pos == bpos) {
        int carry = (uint32_t)~a->l[0] < b->l[0];
        r.l[0] = a->l[0] + b->l[0];
        for (int i = 1; i < n; i++) {
            if (carry) { carry = (~a->l[i] == 0) || (~a->l[i] - 1 < b->l[i]); r.l[i] = a->l[i] + 1 + b->l[i]; }
            else { carry = ~a->l[i] < b->l[i]; r.l[i] = a->l[i] + b->l[i]; }
        }
        if (carry) pc->overflow = 1;
        r.pos = a->pos;
    } else {
        int i = n;
        while (--i >= 0 && a->l[i] == b->l[i]) r.l[i] = 0;
        if (i < 0) r.pos = 1;
        else if (b->l[i] < a->l[i]) { mag_sub(r.l, a->l, b->l, i); r.pos = a->pos; }
        else { mag_sub(r.l, b->l, a->l, i); r.pos = bpos; }
    }
    return r;
}
static PcSI si_sub(IfkPlaneCut *pc, const PcSI *a, const PcSI *b, int n)
{ return si_add(pc, a, (unsigned char)(b->pos == 0), b, n); }
/* exact product, na x nb limbs -> na + nb, sign = (a.pos == b.pos): FUN_180001180 and its users FUN_180016dc0
 * (1x1), FUN_18001b5c0 (2x1), FUN_180020000 (2x2), FUN_18001f600 (3x2), FUN_18001f510 (1x4) */
static PcSI si_mul(const PcSI *a, int na, const PcSI *b, int nb)
{
    PcSI r; memset(&r, 0, sizeof r);
    for (int j = 0; j < nb; j++) {
        uint64_t carry = 0;
        for (int i = 0; i < na; i++) {
            uint64_t t = (uint64_t)a->l[i] * b->l[j] + r.l[i + j] + carry;
            r.l[i + j] = (uint32_t)t; carry = t >> 32;
        }
        r.l[j + na] = (uint32_t)carry;
    }
    r.pos = (unsigned char)(a->pos == b->pos);
    return r;
}
static int si_sign(const PcSI *a, int n)   /* FUN_18001f4c0 (4 limbs) and the inline loops of FUN_18001bde0 */
{
    for (int i = 0; i < n; i++) if (a->l[i] != 0) return a->pos ? 1 : -1;
    return 0;
}

/* ================================================================== PlaneCutSuperDouble
 * DLL layout (0x68 bytes): sign byte, limbs (+4), pointer to the lowest limb (+0x58), exponent E (+0x60, in words)
 * and count n (+0x62); value = sum limb[i] 2^(32 (E - n + i)) (FUN_1800161f0). Limbs sit on absolute 32-bit
 * boundaries (E is in words), so the limb decomposition of a value is unique; we keep e = E - n. */
static void sd_trim(PcSD *a)
{
    int lo = 0, hi = a->n - 1;
    while (hi > 0 && a->l[hi] == 0) hi--;
    while (lo < hi && a->l[lo] == 0) lo++;
    if (lo) memmove(a->l, a->l + lo, sizeof(uint32_t) * (size_t)(hi - lo + 1));
    a->e += lo; a->n = hi - lo + 1;
}
/* SuperInteger<nl> -> SuperDouble: FUN_18001eda0 (nl 1), FUN_18001eec0 / FUN_18001fb10 (2), FUN_18001f230 (4),
 * FUN_18001f730 (5): shift left by (nl k) mod 32, E = (nl k) / 32 + 1, n = nl + 1, trimmed at both ends. With
 * k = DAT_18008f668 = 12 (grid 2^-20) the value is m x 2^(nl (k - 32)) = m g^nl. */
static void sd_from_si(PcSD *r, const PcSI *a, int nl, int k)
{
    int b = (nl * k) % 32, E = (nl * k) / 32 + 1;
    memset(r, 0, sizeof *r);
    r->pos = a->pos; r->n = nl + 1; r->e = E - r->n;
    for (int i = 0; i < nl; i++) {
        r->l[i] |= b ? a->l[i] << b : a->l[i];
        if (b) r->l[i + 1] |= a->l[i] >> (32 - b);
    }
    sd_trim(r);
}
static void sd_one(PcSD *r) { memset(r, 0, sizeof *r); r->pos = 1; r->n = 1; r->l[0] = 1; r->e = 0; }
/* exact product (FUN_18000e0b0, FUN_180017170, FUN_180017480): sign = equal sign bytes [ours for zero] */
static void sd_mul(PcSD *r, const PcSD *a, const PcSD *b)
{
    PcSD t; memset(&t, 0, sizeof t);
    if (a->n + b->n > PC_SD_L) { fprintf(stderr, "ifk_planecut: SuperDouble overflow\n"); abort(); }
    for (int j = 0; j < b->n; j++) {
        uint64_t carry = 0;
        for (int i = 0; i < a->n; i++) {
            uint64_t x = (uint64_t)a->l[i] * b->l[j] + t.l[i + j] + carry;
            t.l[i + j] = (uint32_t)x; carry = x >> 32;
        }
        t.l[j + a->n] = (uint32_t)carry;
    }
    t.n = a->n + b->n; t.e = a->e + b->e; t.pos = (unsigned char)(a->pos == b->pos);
    sd_trim(&t); *r = t;
}
static int sd_iszero(const PcSD *a) { for (int i = 0; i < a->n; i++) if (a->l[i]) return 0; return 1; }
/* exact a + (b with sign bpos): FUN_180017340 / FUN_1800203d0 (subtract), FUN_18001fc50 (add) */
static void sd_addsigned(PcSD *r, const PcSD *a, unsigned char bpos, const PcSD *b)
{
    int e = a->e < b->e ? a->e : b->e;
    int ta = a->e + a->n, tb = b->e + b->n, top = (ta > tb ? ta : tb) + 1;
    int n = top - e;
    uint32_t A[PC_SD_L + 2], B[PC_SD_L + 2];
    PcSD t; memset(&t, 0, sizeof t);
    if (n > PC_SD_L) { fprintf(stderr, "ifk_planecut: SuperDouble overflow\n"); abort(); }
    memset(A, 0, sizeof A); memset(B, 0, sizeof B);
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
        if (i < 0) t.pos = 1;
        else if (B[i] < A[i]) { mag_sub(t.l, A, B, n - 1); t.pos = a->pos; }
        else { mag_sub(t.l, B, A, n - 1); t.pos = bpos; }
    }
    sd_trim(&t); *r = t;
}
static int sd_sign(const PcSD *a) { return sd_iszero(a) ? 0 : (a->pos ? 1 : -1); }
static int sd_cmp(const PcSD *a, const PcSD *b)   /* FUN_18000e260: sign of a - b */
{
    PcSD t; sd_addsigned(&t, a, (unsigned char)(b->pos == 0), b); return sd_sign(&t);
}
/* a * bden - b * aden (FUN_180016fd0 / FUN_18001fcf0) */
static void sd_xsub(PcSD *r, const PcSD *a, const PcSD *aden, const PcSD *b, const PcSD *bden)
{
    PcSD p, q; sd_mul(&p, a, bden); sd_mul(&q, b, aden);
    sd_addsigned(r, &p, (unsigned char)(q.pos == 0), &q);
}
static double sd_to_double(const PcSD *a)   /* FUN_1800161f0: lowest limb first, sequential sum */
{
    double acc = 0.0, s = ldexp(1.0, 32 * a->e);
    for (int i = 0; i < a->n; i++) { acc = acc + (double)a->l[i] * s; s = s * 4294967296.0; }
    return a->pos ? acc : -acc;
}

/* ================================================================== filtered arithmetic */
typedef struct { double v, e; } F;
static F f_mul(F a, F b)   /* FUN_180001040(r, a, b): e = eb|a| + ea|b| + eb ea 2^-53 + |v| */
{
    F r; r.v = b.v * a.v;
    r.e = b.e * fabs(a.v) + a.e * fabs(b.v) + b.e * a.e * U53 + fabs(r.v);
    return r;
}
/* FUN_180028090(r, P, A, d): (P.u - A.u) d.v - d.u (P.v - A.v), points as (u, eu, v, ev) */
static F f_orient(const double *P, const double *A, const double *d)
{
    double pu = P[0] - A[0], epu = P[1] + A[1] + fabs(pu);
    double pv = P[2] - A[2], epv = P[3] + A[3] + fabs(pv);
    double dv = d[2], edv = d[3], du = d[0], edu = d[1];
    double x1 = dv * pu, x2 = du * pv;
    F r; r.v = x1 - x2;
    r.e = edu * fabs(pv) + epv * fabs(du) + edu * epv * U53 + fabs(x2) + edv * fabs(pu) + epu * fabs(dv)
        + edv * epu * U53 + fabs(x1) + fabs(r.v);
    return r;
}
static int f_sure(F a) { return a.e * U52 < fabs(a.v); }

/* ================================================================== pools (emulated record addresses) */
static long in_key(long n) { return (n / POOL) * POOL + (POOL - 1 - n % POOL); }   /* InputNode pool, no frees */
static int pt_alloc(IfkPlaneCut *pc)   /* FUN_18000a1b0 (+ FUN_18000d200 block fill) */
{
    if (pc->ptfree.n == 0) {
        for (int j = 0; j < POOL; j++) lpush(&pc->ptfree, pc->ptblocks * POOL + j);
        pc->ptblocks++;
    }
    long key = pc->ptfree.v[--pc->ptfree.n];
    GROW(pc->pt, pc->npt, pc->cpt);
    PcPoint *p = &pc->pt[pc->npt];
    memset(p, 0, sizeof *p);
    p->ex = -1; p->key = key; p->alive = 1; p->tl = p->tr = -1; p->cnode = -1;
    p->tprio = (int)(((unsigned)pc->npt * 2654435761u) >> 1);
    return pc->npt++;
}
static void pt_free(IfkPlaneCut *pc, int i)   /* FUN_18000a360 */
{
    lpush(&pc->ptfree, (int)pc->pt[i].key);
    pc->pt[i].alive = 0;
}

/* ================================================================== object */
IfkPlaneCut *ifk_pc_new(double grid)
{
    IfkPlaneCut *pc = calloc(1, sizeof *pc);
    pc->grid = grid;
    pc->k = 1;                                           /* FUN_180041800 */
    for (uint32_t u = (uint32_t)(1.0 / grid); u < 0x80000000u; u *= 2) pc->k++;
    pc->ground = -DBL_MAX;                               /* ClearInput: impl+0x58 = 0xffefffffffffffff */
    pc->ptroot = -1;
    return pc;
}
static void pc_clear_plane(IfkPlaneCut *pc)
{
    for (int i = 0; i < pc->npt; i++) { lfree(&pc->pt[i].nodes); lfree(&pc->pt[i].gel); }
    for (int i = 0; i < pc->nce; i++) lfree(&pc->ce[i].split);
    pc->nnode = pc->npt = pc->nex = pc->nce = pc->nge = 0;
    pc->celist.n = pc->ptlist.n = 0;
    pc->ptroot = -1;
    map_free(pc->nodemap); map_free(pc->gemap);
    pc->nodemap = map_new(); pc->gemap = map_new();
}
void ifk_pc_free(IfkPlaneCut *pc)
{
    if (!pc) return;
    pc_clear_plane(pc);
    map_free(pc->nodemap); map_free(pc->gemap);
    for (int i = 0; i < pc->nplane; i++) lfree(&pc->plane[i].tris);
    free(pc->plane); free(pc->in); free(pc->tri); free(pc->edge); free(pc->node); free(pc->pt);
    free(pc->ex); free(pc->ce); free(pc->ge);
    for (int i = 0; i < pc->ncv; i++) lfree(&pc->cv[i].edges);
    for (int i = 0; i < pc->npn; i++) lfree(&pc->pn[i].edges);
    for (int i = 0; i < pc->npoly; i++) { lfree(&pc->poly[i].pedges); lfree(&pc->poly[i].cedges); lfree(&pc->poly[i].children); }
    free(pc->cn); free(pc->cedg); free(pc->cv); free(pc->tp); free(pc->pn); free(pc->pe); free(pc->poly);
    lfree(&pc->curves);
    lfree(&pc->celist); lfree(&pc->ptlist); lfree(&pc->ptfree);
    free(pc);
}

/* ================================================================== cut-plane set */
int ifk_pc_new_cut_plane_set(IfkPlaneCut *pc, double n[3])   /* FUN_180025350 */
{
    double g = pc->grid, a, b;
    for (int i = 0; i < pc->nplane; i++) lfree(&pc->plane[i].tris);   /* FUN_180025850 */
    pc->nplane = 0;
    for (int i = 0; i < 3; i++) pc->n[i] = floor(n[i] / g) * g;       /* FUN_180062e3c = floor */
    for (int i = 0; i < 3; i++) pc->sn[i] = si_from_double(pc, pc->n[i]);
    a = n[0] <= 0.0 ? -n[0] : n[0];
    pc->axis = 0;
    b = n[1] <= 0.0 ? -n[1] : n[1];
    if (a < b) { pc->axis = 1; a = b; }
    b = n[2] <= 0.0 ? -n[2] : n[2];
    if (a < b) { pc->axis = 2; a = b; }
    if (a == 0.0) pc_error(pc, "PlaneCut error 32: invalid cut plane normal.");
    pc->ua = (pc->axis + 1) % 3; pc->va = (pc->axis + 2) % 3;
    pc->aligned = pc->n[pc->ua] == 0.0 && pc->n[pc->va] == 0.0;
    if (0.0 < pc->n[pc->axis] || pc->n[pc->axis] == 0.0) pc->flipped = 0;
    else {
        pc->flipped = 1;
        for (int i = 0; i < 3; i++) { pc->n[i] = pc->n[i] * -1.0; pc->sn[i].pos = (unsigned char)(pc->sn[i].pos == 0); }
    }
    pc->vertical = pc->n[2] == 0.0;
    for (int i = 0; i < 3; i++) n[i] = pc->n[i];
    if (pc->aligned) n[pc->axis] = 1.0;
    return 0;
}
double ifk_pc_add_cut_plane(IfkPlaneCut *pc, double d)   /* FUN_1800255e0 */
{
    double g = pc->grid, off;
    if (pc->aligned) d = d * pc->n[pc->axis];
    off = pc->flipped ? -(floor(d / g) * g) : floor(d / g) * g;
    PcSI s = si_from_double(pc, off);
    if (pc->overflow) pc_error(pc, "PlaneCut error 17: overflow");
    int pos = 0;
    while (pos < pc->nplane && pc->plane[pos].off < off) pos++;
    if (pos < pc->nplane && pc->plane[pos].off == off) return off;   /* equal offset: not duplicated */
    pc->plane = realloc(pc->plane, sizeof(PcPlane) * (size_t)(pc->nplane + 1));
    memmove(pc->plane + pos + 1, pc->plane + pos, sizeof(PcPlane) * (size_t)(pc->nplane - pos));
    memset(&pc->plane[pos], 0, sizeof(PcPlane));
    pc->plane[pos].off = off; pc->plane[pos].soff = s;
    pc->nplane++;
    return off;
}
void ifk_pc_end_cut_plane_set(IfkPlaneCut *pc)   /* 180021020 */
{
    if (pc->aligned) pc->n[pc->axis] = 1.0;
}

/* ================================================================== input */
int ifk_pc_add_input_node(IfkPlaneCut *pc, const double xyz[3])   /* 180021b20 */
{
    GROW(pc->in, pc->nin, pc->cin);
    PcInNode *p = &pc->in[pc->nin];
    /* the caller passes the SuperInteger<1> copies (Silhouette: FUN_180026d90, the same code as FUN_180008dc0);
     * its overflow flag is the caller's (IFSilhouette error 2), so PlaneCut's flag is left alone */
    int ovf = pc->overflow;
    for (int i = 0; i < 3; i++) { p->x[i] = xyz[i]; p->s[i] = si_from_double(pc, xyz[i]); }
    pc->overflow = ovf;
    p->key = in_key(pc->nin);
    return pc->nin++;
}
int ifk_pc_add_input_triangle(IfkPlaneCut *pc, int a, int b, int c, int id, int flag)   /* 180021be0 */
{
    GROW(pc->tri, pc->ntri, pc->ctri);
    PcInTri *t = &pc->tri[pc->ntri];
    t->n[0] = a; t->n[1] = b; t->n[2] = c; t->id = id; t->flag = flag;
    t->e[0] = t->e[1] = t->e[2] = -1;
    return pc->ntri++;   /* pushed on impl+0x88 (push-front): the DLL walks the triangles newest first */
}
void ifk_pc_end_input(IfkPlaneCut *pc, int edges)   /* 180021d80 */
{
    if (!edges) return;
    Map *m = map_new();
    /* walk impl+0x88 from its head (newest triangle first); edge (n[i], n[(i+1)%3]) keyed by the node pointer
     * pair, smaller pointer first; a new key makes an InputEdge (pool impl+0x110) */
    for (int ti = pc->ntri - 1; ti >= 0; ti--) {
        PcInTri *t = &pc->tri[ti];
        for (int i = 0; i < 3; i++) {
            int a = t->n[i], b = t->n[(i + 1) % 3];
            if (pc->in[b].key < pc->in[a].key) { int x = a; a = b; b = x; }
            long long key = (long long)a * 0x100000000LL + b;
            int e = map_get(m, key);
            if (e < 0) {
                GROW(pc->edge, pc->nedge, pc->cedge);
                pc->edge[pc->nedge].a = a; pc->edge[pc->nedge].b = b;
                e = pc->nedge++;
                map_put(m, key, e);
            }
            t->e[i] = e;
        }
    }
    map_free(m);
}

/* ================================================================== exact cut-node coordinates: FUN_18001bde0 */
static PcInNode *innode(IfkPlaneCut *pc, int i) { return &pc->in[i]; }

/* FUN_18001b150: triangle plane n = (p2 - p1) x (p0 - p1) (FUN_18001b700 x 6, FUN_18001b390), d = n . p0
 * (FUN_18001b5c0) */
static void tri_plane(IfkPlaneCut *pc, int ti, PcSI n[3], PcSI *d)
{
    PcInTri *t = &pc->tri[ti];
    PcInNode *p0 = innode(pc, t->n[0]), *p1 = innode(pc, t->n[1]), *p2 = innode(pc, t->n[2]);
    PcSI a[3], b[3];
    for (int i = 0; i < 3; i++) { a[i] = si_sub(pc, &p2->s[i], &p1->s[i], 1); b[i] = si_sub(pc, &p0->s[i], &p1->s[i], 1); }
    for (int i = 0; i < 3; i++) {   /* x: a.y b.z - b.y a.z; y: a.z b.x - b.z a.x; z: a.x b.y - b.x a.y */
        int j = (i + 1) % 3, k = (i + 2) % 3;
        PcSI P = si_mul(&a[j], 1, &b[k], 1), Q = si_mul(&b[j], 1, &a[k], 1);
        n[i] = si_sub(pc, &P, &Q, 2);
    }
    PcSI x0 = si_mul(&n[0], 2, &p0->s[0], 1), x1 = si_mul(&n[1], 2, &p0->s[1], 1), x2 = si_mul(&n[2], 2, &p0->s[2], 1);
    PcSI s = si_add(pc, &x0, x1.pos, &x1, 3);   /* FUN_180018190(.., 3) */
    *d = si_add(pc, &s, x2.pos, &x2, 3);        /* FUN_180018320 */
}

static const PcPlane *g_plane;   /* the plane being processed (TLS DAT_18008f500 in the DLL) */

static void pc_exact(IfkPlaneCut *pc, int ni, int wantW)   /* FUN_18001bde0(node, plane, wantW) */
{
    PcCutNode *nd = &pc->node[ni];
    PcPoint *p = &pc->pt[nd->pt];
    PcExact *X;
    if (p->ex < 0) {
        GROW(pc->ex, pc->nex, pc->cex);
        memset(&pc->ex[pc->nex], 0, sizeof(PcExact));
        p->ex = pc->nex++;
    } else {
        if (!wantW) return;
        if (pc->ex[p->ex].hasW) return;
    }
    pc->nexact++;
    X = &pc->ex[p->ex];
    X->hasW = wantW;
    int u = pc->ua, v = pc->va, w = pc->axis, sgn = 1;
    const PcSI *c = &g_plane->soff;            /* param_2 + 8: the plane's SuperInteger offset */
    if (nd->kind == 0) {
        PcInNode *q = innode(pc, nd->ref);
        sd_from_si(&X->U, &q->s[u], 1, pc->k);
        sd_from_si(&X->V, &q->s[v], 1, pc->k);
        sd_one(&X->D);                       /* +0xd0: sign 1, E 1, n 1, limb 1 */
        if (wantW) sd_from_si(&X->W, &q->s[w], 1, pc->k);
        return;                               /* kind 0 keeps its doubles (set by FUN_18001b800) */
    }
    if (nd->kind == 1) {
        /* aligned branch, l.30598-30625 of the dump */
        PcInNode *a = innode(pc, pc->edge[nd->ref].a), *b = innode(pc, pc->edge[nd->ref].b);
        PcSI dl[3];
        for (int i = 0; i < 3; i++) dl[i] = si_sub(pc, &b->s[i], &a->s[i], 1);
        sgn = si_sign(&dl[w], 1);
        if (sgn == 0) pc_error(pc, "PlaneCut error 2: invalid triangle.");
        PcSI cw = si_sub(pc, c, &a->s[w], 1);                    /* plane - p0.w */
        for (int k = 0; k < 2; k++) {
            int ax = k ? v : u;
            PcSI t1 = si_mul(&cw, 1, &dl[ax], 1);                  /* (c - p0w) d[ax] */
            PcSI t2 = si_mul(&a->s[ax], 1, &dl[w], 1);             /* p0[ax] d[w] */
            PcSI s = si_add(pc, &t1, t2.pos, &t2, 2);              /* FUN_180017c90 */
            sd_from_si(k ? &X->V : &X->U, &s, 2, pc->k);           /* FUN_18001eec0 */
        }
        sd_from_si(&X->D, &dl[w], 1, pc->k);                       /* FUN_18001eda0 */
        if (wantW) { PcSI t = si_mul(c, 1, &dl[w], 1); sd_from_si(&X->W, &t, 2, pc->k); }
    } else {
        /* kind 2, aligned branch (FUN_18001b150 x 2, FUN_18001f380, then l.30699-30780) */
        PcSI nA[3], nB[3], dA, dB, cr[3];
        tri_plane(pc, nd->ref, nA, &dA);
        tri_plane(pc, nd->ref2, nB, &dB);
        for (int i = 0; i < 3; i++) {   /* FUN_18001f380: nA x nB (FUN_180020000 products, FUN_18001fe90) */
            int j = (i + 1) % 3, k = (i + 2) % 3;
            PcSI P = si_mul(&nA[j], 2, &nB[k], 2), Q = si_mul(&nB[j], 2, &nA[k], 2);
            cr[i] = si_sub(pc, &P, &Q, 4);
        }
        sgn = si_sign(&cr[w], 4);                                  /* FUN_18001f4c0 */
        if (sgn == 0) pc_error(pc, "PlaneCut error 5: assertion failed.");
        /* U = c cr[u] + dA nB[v] - dB nA[v];  V = c cr[v] - dA nB[u] + dB nA[u]  (SuperInteger<5>, FUN_1800200f0) */
        PcSI a1 = si_mul(c, 1, &cr[u], 4), a2 = si_mul(&dA, 3, &nB[v], 2), a3 = si_mul(&dB, 3, &nA[v], 2);
        PcSI s = si_add(pc, &a1, a2.pos, &a2, 5);
        s = si_add(pc, &s, (unsigned char)(a3.pos == 0), &a3, 5);
        sd_from_si(&X->U, &s, 5, pc->k);                           /* FUN_18001f730 */
        a1 = si_mul(c, 1, &cr[v], 4); a2 = si_mul(&dA, 3, &nB[u], 2); a3 = si_mul(&dB, 3, &nA[u], 2);
        s = si_add(pc, &a1, (unsigned char)(a2.pos == 0), &a2, 5);
        s = si_add(pc, &s, a3.pos, &a3, 5);
        sd_from_si(&X->V, &s, 5, pc->k);
        sd_from_si(&X->D, &cr[w], 4, pc->k);                       /* FUN_18001f230 */
        if (wantW) { PcSI t = si_mul(c, 1, &cr[w], 4); sd_from_si(&X->W, &t, 5, pc->k); }
    }
    /* LAB_18001c41a: make D > 0, then the doubles of the point from the exact values */
    if (sgn < 0) {
        X->D.pos = (unsigned char)(X->D.pos == 0); X->U.pos = (unsigned char)(X->U.pos == 0);
        X->V.pos = (unsigned char)(X->V.pos == 0); X->W.pos = (unsigned char)(X->W.pos == 0);
    }
    double D = sd_to_double(&X->D), aD = fabs(D), eD = aD * U52, x, q;
    x = sd_to_double(&X->U); q = x / D;
    p->c[0] = q; p->c[1] = (aD * fabs(q) + fabs(x)) / (fabs(D) - eD) + fabs(q);
    x = sd_to_double(&X->V); q = x / D;
    p->c[2] = q; p->c[3] = (aD * fabs(q) + fabs(x)) / (fabs(D) - eD) + fabs(q);
    if (wantW) { x = sd_to_double(&X->W); q = x / D; p->c[4] = q; p->c[5] = (aD * fabs(q) + fabs(x)) / (fabs(D) - eD) + fabs(q); }
}

static int head(IfkPlaneCut *pc, int pt) { PcList *l = &pc->pt[pt].nodes; return l->v[l->n - 1]; }  /* **(pt+0x40) */
/* exact records of two points (both made first: pc->ex may move when a record is added) */
static void ex_two(IfkPlaneCut *pc, int a, int b, PcExact **XA, PcExact **XB)
{
    pc_exact(pc, head(pc, a), 0); pc_exact(pc, head(pc, b), 0);
    *XA = &pc->ex[pc->pt[a].ex]; *XB = &pc->ex[pc->pt[b].ex];
}

/* ================================================================== point comparisons */
static int cmp_axis(IfkPlaneCut *pc, int a, int b)   /* FUN_180013330(a, b): sign of a - b on axis DAT_18008f488 */
{
    int ax = pc->axis488;
    const double *A = pc->pt[a].c + 2 * ax, *B = pc->pt[b].c + 2 * ax;
    double d = A[0] - B[0];
    if ((A[1] + B[1] + fabs(d)) * U52 < fabs(d)) return (A[0] <= B[0] && B[0] != A[0]) ? -1 : 1;
    if (A[1] == 0.0 && B[1] == 0.0) return 0;
    PcExact *XA, *XB; ex_two(pc, a, b, &XA, &XB);
    PcSD l, r;
    sd_mul(&l, &XB->D, ax ? &XA->V : &XA->U);
    sd_mul(&r, &XA->D, ax ? &XB->V : &XB->U);
    return sd_cmp(&l, &r);
}
static int pick_axis(IfkPlaneCut *pc, int a, int b)   /* FUN_180029390: 0 = the two points are equal */
{
    for (int ax = 0; ax < 2; ax++) {
        const double *A = pc->pt[a].c + 2 * ax, *B = pc->pt[b].c + 2 * ax;
        double d = A[0] - B[0];
        if ((A[1] + B[1] + fabs(d)) * U52 < fabs(d)) { pc->axis488 = ax; return 1; }
    }
    PcExact *XA, *XB; ex_two(pc, a, b, &XA, &XB);
    for (int ax = 0; ax < 2; ax++) {
        PcSD l, r;
        sd_mul(&l, &XB->D, ax ? &XA->V : &XA->U);
        sd_mul(&r, &XA->D, ax ? &XB->V : &XB->U);
        if (sd_cmp(&l, &r) != 0) { pc->axis488 = ax; return 1; }
    }
    return 0;
}
static int same_def(IfkPlaneCut *pc, int na, int nb)   /* FUN_1800286f0 */
{
    PcCutNode *a = &pc->node[na], *b = &pc->node[nb];
    if (a->kind != b->kind) return 0;
    if (a->kind == 0) {
        const double *x = pc->in[a->ref].x, *y = pc->in[b->ref].x;
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    }
    if (a->kind == 1) {
        const double *a0 = pc->in[pc->edge[a->ref].a].x, *a1 = pc->in[pc->edge[a->ref].b].x;
        const double *b0 = pc->in[pc->edge[b->ref].a].x, *b1 = pc->in[pc->edge[b->ref].b].x;
        if (a0[0] == b0[0] && a0[1] == b0[1] && a0[2] == b0[2])
            return a1[0] == b1[0] && a1[1] == b1[1] && a1[2] == b1[2];
        if (a0[0] == b1[0] && a0[1] == b1[1] && a0[2] == b1[2])
            return a1[0] == b0[0] && a1[1] == b0[1] && a1[2] == b0[2];
    }
    return 0;
}
/* FUN_180030470 comparator: new point a against tree point b; -1 left, +1 right, 0 found */
static int pt_tree_cmp(IfkPlaneCut *pc, int a, int b)
{
    PcPoint *P = &pc->pt[a], *Q = &pc->pt[b];
    double d = P->s - Q->s;
    if (!(fabs(d) <= (Q->es + P->es + fabs(d)) * U52)) return Q->s <= P->s ? 1 : -1;
    if (P->es != 0.0 || Q->es != 0.0) {
        if (same_def(pc, head(pc, a), head(pc, b))) return 0;
        PcExact *XA, *XB; ex_two(pc, a, b, &XA, &XB);
        PcSD sa, sb, l, r;
        sd_addsigned(&sa, &XA->U, XA->V.pos, &XA->V);   /* FUN_1800173c0 */
        sd_addsigned(&sb, &XB->U, XB->V.pos, &XB->V);
        sd_mul(&l, &sa, &XB->D); sd_mul(&r, &sb, &XA->D);   /* FUN_18000e470 */
        int c = sd_cmp(&l, &r);
        for (int i = 0; c == 0 && i < 2; i++) {
            sd_mul(&l, &XB->D, i ? &XA->V : &XA->U);
            sd_mul(&r, &XA->D, i ? &XB->V : &XB->U);
            c = sd_cmp(&l, &r);
        }
        return c;   /* 0 = found, >= 0 right, < 0 left */
    }
    if (P->c[0] < Q->c[0]) return -1;
    if (P->c[0] <= Q->c[0]) {
        if (P->c[2] < Q->c[2]) return -1;
        if (P->c[2] <= Q->c[2]) return 0;
    }
    return 1;
}
/* point tree: the DLL's binary tree at impl+0xc8 (FUN_180030470 find, FUN_180009020 insert); ours is a treap with
 * the same comparator (the found / not-found answer does not depend on the shape) */
static int treap_ins(IfkPlaneCut *pc, int root, int x, int *found)
{
    if (root < 0) return x;
    int c = pt_tree_cmp(pc, x, root);
    if (c == 0) { *found = root; return root; }
    if (c < 0) {
        int l = treap_ins(pc, pc->pt[root].tl, x, found);
        pc->pt[root].tl = l;
        if (*found < 0 && pc->pt[l].tprio > pc->pt[root].tprio) {
            pc->pt[root].tl = pc->pt[l].tr; pc->pt[l].tr = root; return l;
        }
    } else {
        int r = treap_ins(pc, pc->pt[root].tr, x, found);
        pc->pt[root].tr = r;
        if (*found < 0 && pc->pt[r].tprio > pc->pt[root].tprio) {
            pc->pt[root].tr = pc->pt[r].tl; pc->pt[r].tl = root; return r;
        }
    }
    return root;
}
static void pt_insert(IfkPlaneCut *pc, int ni)   /* FUN_180028aa0 */
{
    int p = pc->node[ni].pt, found = -1;
    PcPoint *P = &pc->pt[p];
    double s = P->c[2] + P->c[0];
    P->s = s; P->es = P->c[3] + P->c[1] + fabs(s);
    int root = treap_ins(pc, pc->ptroot, p, &found);
    if (found >= 0) {
        PcPoint *Q = &pc->pt[found];
        for (int i = P->nodes.n - 1; i >= 0; i--) lpush(&Q->nodes, P->nodes.v[i]);   /* move, head first */
        if (P->ex >= 0 && Q->ex < 0) { Q->ex = P->ex; P->ex = -1; }
        P->nodes.n = 0;
        pt_free(pc, p);
        pc->node[ni].pt = found;
        return;
    }
    pc->ptroot = root;
    lpush(&pc->ptlist, p);   /* impl+0xa0 */
}

/* ================================================================== cut nodes */
static int node_new(IfkPlaneCut *pc, int kind, int ref, int ref2)
{
    GROW(pc->node, pc->nnode, pc->cnode);
    PcCutNode *n = &pc->node[pc->nnode];
    n->kind = kind; n->ref = ref; n->ref2 = ref2; n->pt = -1;
    return pc->nnode++;
}
static int node_dedup(IfkPlaneCut *pc, int ni)   /* FUN_180026a30: tree impl+0xc0 keyed by the input pointer */
{
    PcCutNode *n = &pc->node[ni];
    long long key = (long long)n->kind * 0x100000000LL + n->ref;
    int e = map_get(pc->nodemap, key);
    if (e >= 0) { pt_free(pc, n->pt); n->pt = -1; return e; }   /* FUN_18000a360, record back to the pool */
    map_put(pc->nodemap, key, ni);
    lpush(&pc->pt[n->pt].nodes, ni);
    pt_insert(pc, ni);
    return ni;
}
/* FUN_18001b800: filtered coordinates of a kind 0 / 1 cut node (aligned set) */
static void node_coords(IfkPlaneCut *pc, int ni, const PcPlane *pl)
{
    PcCutNode *n = &pc->node[ni];
    double *c = pc->pt[n->pt].c;
    int u = pc->ua, v = pc->va, w = pc->axis;
    if (n->kind == 0) {
        const double *x = pc->in[n->ref].x;
        c[0] = x[u]; c[1] = 0; c[2] = x[v]; c[3] = 0; c[4] = x[w]; c[5] = 0;
        return;
    }
    const double *a = pc->in[pc->edge[n->ref].a].x, *b = pc->in[pc->edge[n->ref].b].x;
    double d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    if (d[w] == 0.0) pc_error(pc, "PlaneCut error 20: invalid triangle.");
    double t = (pl->off - a[w]) / d[w], at = fabs(t), x;
    c[4] = pl->off; c[5] = 0;
    x = t * d[u]; c[0] = x + a[u]; c[1] = fabs(c[0]) + fabs(d[u]) * at + fabs(x);
    x = t * d[v]; c[2] = x + a[v]; c[3] = fabs(c[2]) + fabs(d[v]) * at + fabs(x);
}

/* ================================================================== FUN_180026140: cut one triangle */
static void cut_triangle(IfkPlaneCut *pc, int ti, const PcPlane *pl)
{
    PcInTri *t = &pc->tri[ti];
    int s[3], w = pc->axis;
    for (int i = 0; i < 3; i++) {   /* aligned: exact double compare with the offset */
        double z = pc->in[t->n[i]].x[w];
        s[i] = z == pl->off ? 0 : (pl->off < z ? 1 : -1);
    }
    int cnt = 0, rkind[3], rref[3], ridx[3];
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3, kind = -1, ref = 0, idx = i;
        if (s[i] == 0) { if (s[j] != 0) { kind = 0; ref = t->n[i]; idx = i; } }
        else if (s[j] == 0) { kind = 0; ref = t->n[j]; idx = j; }
        else if ((s[i] < 0) != (s[j] < 0)) { kind = 1; ref = t->e[i]; idx = i; }
        if (kind < 0) continue;
        int dup = 0;   /* FUN_1800267e0: no duplicates (compares the pointer only) */
        for (int k = 0; k < cnt; k++) if (rkind[k] == kind && rref[k] == ref) dup = 1;
        if (dup) continue;
        rkind[cnt] = kind; rref[cnt] = ref; ridx[cnt] = idx; cnt++;
    }
    if (cnt != 2) return;   /* 1: the record goes back to the pool */
    int r22 = 0, r15 = 1, i16 = ridx[0], i13 = ridx[1];
    if (rkind[0] == 0 && rref[0] == t->n[1] && rkind[1] == 0 && rref[1] == t->n[0]) {
        r22 = 1; r15 = 0; i16 = ridx[1]; i13 = ridx[0];
    }
    int na = node_new(pc, rkind[r22], rref[r22], -1), nb = node_new(pc, rkind[r15], rref[r15], -1);
    pc->node[na].pt = pt_alloc(pc); node_coords(pc, na, pl);
    pc->node[nb].pt = pt_alloc(pc); node_coords(pc, nb, pl);
    GROW(pc->ce, pc->nce, pc->cce);
    PcCutEdge *E = &pc->ce[pc->nce];
    memset(E, 0, sizeof *E);
    const double *A = pc->pt[pc->node[na].pt].c, *B = pc->pt[pc->node[nb].pt].c;
    E->box[0] = A[0] - A[1] * U52; E->box[2] = A[1] * U52 + A[0];
    double x = B[0] - B[1] * U52; if (x < E->box[0]) E->box[0] = x;
    x = B[1] * U52 + B[0]; if (E->box[2] < x) E->box[2] = x;
    E->box[1] = A[2] - A[3] * U52; E->box[3] = A[3] * U52 + A[2];
    x = B[2] - B[3] * U52; if (x < E->box[1]) E->box[1] = x;
    x = B[3] * U52 + B[2]; if (E->box[3] < x) E->box[3] = x;
    double zmax = A[5] * U52 + A[4];   /* local_68: max of the third coordinate (axis 2 for a z set) */
    x = B[5] * U52 + B[4]; if (zmax < x) zmax = x;
    if (zmax < pc->ground) {           /* discard (IO slot 6 of PlaneCutToSilhouette, FUN_180020b00, returns 0) */
        pt_free(pc, pc->node[na].pt); pt_free(pc, pc->node[nb].pt);
        pc->nnode -= 2; return;
    }
    int b23;
    if (i16 == i13) b23 = s[(i16 + 2) % 3] < 0;
    else if (i16 == 0) {
        if (i13 == 1) b23 = s[2] < 0;
        else if (!(pc->node[na].kind == 0 && pc->node[na].ref == t->n[0])) b23 = s[0] < 0;   /* *puVar22 != node0 */
        else b23 = 0 < s[1];
    } else b23 = s[0] < 0;
    int first, second;
    if (b23) { first = node_dedup(pc, nb); second = node_dedup(pc, na); }
    else { first = node_dedup(pc, na); second = node_dedup(pc, nb); }
    E->n0 = first; E->n1 = second; E->tri = ti;
    if (pc->node[first].kind == 0 && pc->node[second].kind == 0) {
        if (!pc->flipped) E->flag48 = (0 < s[0] || 0 < s[1] || 0 < s[2]);
        else E->flag48 = (s[0] < 0 || s[1] < 0 || s[2] < 0);
    } else E->flag48 = 1;
    lpush(&pc->celist, pc->nce);   /* impl+0x90 (push-front) */
    pc->nce++;
}

/* ================================================================== pair intersections */
static int ptof(IfkPlaneCut *pc, int ni) { return pc->node[ni].pt; }

static void collinear(IfkPlaneCut *pc, int ea, int eb)   /* FUN_180028830 */
{
    PcCutEdge *A = &pc->ce[ea], *B = &pc->ce[eb];
    int a0 = ptof(pc, A->n0), a1 = ptof(pc, A->n1), b0 = ptof(pc, B->n0), b1 = ptof(pc, B->n1);
    if (!pick_axis(pc, a0, a1)) pc_error(pc, "PlaneCut error 4: invalid edge.");
    int x, y;
    x = cmp_axis(pc, a0, b0); if (x) { y = cmp_axis(pc, a0, b1); if (y && x != y) lpush(&pc->ce[eb].split, A->n0); }
    x = cmp_axis(pc, a1, b0); if (x) { y = cmp_axis(pc, a1, b1); if (y && x != y) lpush(&pc->ce[eb].split, A->n1); }
    x = cmp_axis(pc, b0, a0); if (x) { y = cmp_axis(pc, b0, a1); if (y && x != y) lpush(&pc->ce[ea].split, B->n0); }
    x = cmp_axis(pc, b1, a0); if (x) { y = cmp_axis(pc, b1, a1); if (y && x != y) lpush(&pc->ce[ea].split, B->n1); }
}
static int diverge(IfkPlaneCut *pc, int ea, int n, int other)   /* FUN_180028580 + FUN_1800285f0 */
{
    PcCutEdge *A = &pc->ce[ea];
    int far;
    if (A->n0 == n) far = A->n1; else if (A->n1 == n) far = A->n0; else return 0;
    const double *P = pc->pt[ptof(pc, other)].c, *Fp = pc->pt[ptof(pc, far)].c, *N = pc->pt[ptof(pc, n)].c;
    for (int ax = 0; ax < 2; ax++) {
        const double *p = P + 2 * ax, *f = Fp + 2 * ax, *nn = N + 2 * ax;
        double d = p[0] - nn[0];
        if (!((nn[1] + p[1] + fabs(d)) * U52 < fabs(d))) continue;
        d = f[0] - nn[0];
        if (!((nn[1] + f[1] + fabs(d)) * U52 < fabs(d))) continue;
        if ((p[0] < nn[0]) != (f[0] < nn[0])) return 1;
    }
    return 0;
}
/* FUN_180028230: sign of the exact orientation of point P against the line through A0 with direction (dx, dy) */
static int ex_orient(IfkPlaneCut *pc, const PcSD *dx, const PcSD *dy, int P, int A0)
{
    PcExact *XP, *XA; ex_two(pc, P, A0, &XP, &XA);
    PcSD tu, tv, l, r, o;
    sd_xsub(&tu, &XP->U, &XP->D, &XA->U, &XA->D);
    sd_xsub(&tv, &XP->V, &XP->D, &XA->V, &XA->D);
    sd_mul(&l, &tu, dy); sd_mul(&r, &tv, dx);
    sd_addsigned(&o, &l, (unsigned char)(r.pos == 0), &r);
    return sd_sign(&o);
}
static void pc_pair(IfkPlaneCut *pc, int ea, int eb, const PcPlane *pl)   /* FUN_180027500 */
{
    PcCutEdge *A = &pc->ce[ea], *B = &pc->ce[eb];
    if (pc->tri[A->tri].id == pc->tri[B->tri].id) return;
    pc->npairs++;
    int mark = pc->stamp470;
    int pa0 = ptof(pc, A->n0), pa1 = ptof(pc, A->n1), pb0 = ptof(pc, B->n0), pb1 = ptof(pc, B->n1);
    int m0 = pc->pt[pb0].stamp == mark, m1 = pc->pt[pb1].stamp == mark;
    if (m0) { if (m1) return; if (diverge(pc, ea, B->n0, B->n1)) return; }
    else if (m1) { if (diverge(pc, ea, B->n1, B->n0)) return; }
    else {
        int rej = B->box[0] <= A->box[0] ? B->box[2] < A->box[0] : A->box[2] < B->box[0];
        if (rej) return;
        rej = B->box[1] <= A->box[1] ? B->box[3] < A->box[1] : A->box[3] < B->box[1];
        if (rej) return;
    }
    const double *P0 = pc->pt[pa0].c, *P1 = pc->pt[pa1].c, *Q0 = pc->pt[pb0].c, *Q1 = pc->pt[pb1].c;
    double dA[4], dB[4];
    dA[0] = P1[0] - P0[0]; dA[1] = P1[1] + P0[1] + fabs(dA[0]);
    dA[2] = P1[2] - P0[2]; dA[3] = P1[3] + P0[3] + fabs(dA[2]);
    F o0 = f_orient(Q0, P0, dA), o1 = f_orient(Q1, P0, dA);
    int exact, cand;
    if (f_sure(o0)) {
        if (!f_sure(o1)) {
            if (m1) return;
            if (o1.e != 0.0) { exact = 1; cand = -1; }
            else { exact = 0; cand = B->n1; }
        } else {
            if (0.0 <= o0.v) { if (0.0 < o1.v) return; } else if (o1.v < 0.0) return;
            exact = 0; cand = -1;
        }
    } else if (m0) {
        if (f_sure(o1)) return;
        if (m1) return;
        if (o1.e == 0.0) { collinear(pc, ea, eb); return; }
        exact = 1; cand = -1;
    } else if (o0.e == 0.0) {
        if (f_sure(o1)) { exact = 0; cand = B->n0; }
        else {
            if (m1) { collinear(pc, ea, eb); return; }
            if (o1.e == 0.0) { collinear(pc, ea, eb); return; }
            exact = 1; cand = B->n0;
        }
    } else { exact = 1; cand = -1; }
    dB[0] = Q1[0] - Q0[0]; dB[1] = Q1[1] + Q0[1] + fabs(dB[0]);
    dB[2] = Q1[2] - Q0[2]; dB[3] = Q1[3] + Q0[3] + fabs(dB[2]);
    F q0 = f_orient(P0, Q0, dB), q1 = f_orient(P1, Q0, dB);
    int goexact = 0;
    if (f_sure(q0)) {
        if (f_sure(q1)) {
            int opp = 0.0 <= q0.v ? q1.v < 0.0 : 0.0 < q1.v;
            if (!opp && q1.v != 0.0) return;
            if (exact) goexact = 1;
        } else {
            if (q1.e != 0.0) goexact = 1;
            else { cand = A->n1; if (exact) goexact = 1; }
        }
    } else if (q0.e == 0.0) {
        cand = A->n0;
        if (f_sure(q1)) { if (exact) goexact = 1; }
        else if (q1.e == 0.0) { collinear(pc, ea, eb); return; }
        else goexact = 1;
    } else goexact = 1;
    if (goexact) {   /* l.180027a14: exact records of the four ends, exact orientations */
        pc_exact(pc, head(pc, pa0), 0); pc_exact(pc, head(pc, pa1), 0);
        pc_exact(pc, head(pc, pb0), 0); pc_exact(pc, head(pc, pb1), 0);
        PcExact *XA0 = &pc->ex[pc->pt[pa0].ex], *XA1 = &pc->ex[pc->pt[pa1].ex];
        PcExact *XB0 = &pc->ex[pc->pt[pb0].ex], *XB1 = &pc->ex[pc->pt[pb1].ex];
        PcSD dx, dy;
        sd_xsub(&dx, &XA1->U, &XA1->D, &XA0->U, &XA0->D);
        sd_xsub(&dy, &XA1->V, &XA1->D, &XA0->V, &XA0->D);
        int s7 = ex_orient(pc, &dx, &dy, pb0, pa0), s8 = ex_orient(pc, &dx, &dy, pb1, pa0);
        cand = -1;
        if (s7 < 0) {
            if (s8 == 0) { cand = B->n1; if (m1) return; }
            else if (s8 < 0) return;
        } else if (s7 == 0) {
            if (s8 == 0) { collinear(pc, ea, eb); return; }
            cand = B->n0; if (m0) return;
        } else {
            if (s8 == 0) { cand = B->n1; if (m1) return; }
            else if (0 < s8) return;
        }
        XA0 = &pc->ex[pc->pt[pa0].ex]; XB0 = &pc->ex[pc->pt[pb0].ex]; XB1 = &pc->ex[pc->pt[pb1].ex];
        sd_xsub(&dx, &XB1->U, &XB1->D, &XB0->U, &XB0->D);
        sd_xsub(&dy, &XB1->V, &XB1->D, &XB0->V, &XB0->D);
        int s9 = ex_orient(pc, &dx, &dy, pa0, pb0), sa = ex_orient(pc, &dx, &dy, pa1, pb0);
        if (s9 < 0) {
            if (sa == 0) { if (cand >= 0) pc_error(pc, "PlaneCut error 41: undetected equivalent nodes."); cand = A->n1; }
            else if (sa < 0) return;
        } else if (s9 == 0) {
            if (cand >= 0) pc_error(pc, "PlaneCut error 43: undetected equivalent nodes.");
            cand = A->n0;
        } else {
            if (sa == 0) { if (cand >= 0) pc_error(pc, "PlaneCut error 42: undetected equivalent nodes."); cand = A->n1; }
            else if (0 < sa) return;
        }
    }
    if (cand < 0) {   /* LAB_180027cd6: new intersection node (kind 2: the two triangles) */
        cand = node_new(pc, 2, A->tri, B->tri);
        int p = pt_alloc(pc);
        pc->node[cand].pt = p;
        lpush(&pc->pt[p].nodes, cand);                 /* FUN_180008e80 */
        F dau = { dA[0], dA[1] }, dav = { dA[2], dA[3] }, dbu = { dB[0], dB[1] }, dbv = { dB[2], dB[3] };
        F x1 = f_mul(dau, dbv), x2 = f_mul(dav, dbu);
        double den = x2.v - x1.v, eden = x1.e + x2.e + fabs(den);
        if (!(eden * U52 < fabs(den))) pc_exact(pc, cand, 1);
        else {
            double t = q0.v / den, at = fabs(t);
            double et = (eden * at + q0.e) / (fabs(den) - eden * U52) + at;
            double *c = pc->pt[p].c, y;
            P0 = pc->pt[pa0].c;
            y = dA[0] * t; c[0] = y + P0[0];
            c[1] = dA[1] * at + et * fabs(dA[0]) + dA[1] * et * U53 + fabs(y) + P0[1] + fabs(c[0]);
            y = dA[2] * t; c[2] = y + P0[2];
            c[3] = dA[3] * at + et * fabs(dA[2]) + dA[3] * et * U53 + fabs(y) + P0[3] + fabs(c[2]);
            c[4] = pl->off; c[5] = 0.0;               /* aligned: *param_4 */
        }
        pt_insert(pc, cand);                          /* FUN_180028aa0 */
    }
    lpush(&pc->ce[ea].split, cand);                   /* +0x40 lists of both edges (pool +0x1d8) */
    lpush(&pc->ce[eb].split, cand);
}

/* FUN_180027060 with the candidate search replacing the IntersectionOctree (see 11.7): for each cut edge E in
 * list order (impl+0x90 from its head), every later edge F whose closed box meets E's box, in list order.
 * The quadtree (FUN_180026b60, FUN_18002fee0, slots FUN_18001d5c0 / FUN_18001d700 / FUN_1800301e0, edge
 * classifier FUN_18001e6e0) only groups edges per leaf; FUN_180027500 does all geometry and rejects pairs whose
 * boxes are disjoint unless they share an end point (and such pairs meet in a leaf), so the pairs that produce
 * nodes are the same; the ORDER in which they are tested is ours. */
typedef struct { double lo; int pos; } SweepItem;
static int sweep_cmp(const void *a, const void *b)
{
    const SweepItem *x = a, *y = b;
    if (x->lo < y->lo) return -1;
    if (x->lo > y->lo) return 1;
    return x->pos - y->pos;
}
static int int_cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static void pc_pairs(IfkPlaneCut *pc, const PcPlane *pl)
{
    int n = pc->celist.n;
    int *order = malloc(sizeof(int) * (size_t)(n + 1));     /* list position -> cut edge */
    for (int i = 0; i < n; i++) order[i] = pc->celist.v[n - 1 - i];
    SweepItem *it = malloc(sizeof(SweepItem) * (size_t)(n + 1));
    for (int i = 0; i < n; i++) { it[i].lo = pc->ce[order[i]].box[0]; it[i].pos = i; }
    qsort(it, (size_t)n, sizeof *it, sweep_cmp);
    PcList *cand = calloc((size_t)(n + 1), sizeof(PcList));
    for (int i = 0; i < n; i++) {
        const double *bi = pc->ce[order[it[i].pos]].box;
        for (int j = i + 1; j < n && it[j].lo <= bi[2]; j++) {
            const double *bj = pc->ce[order[it[j].pos]].box;
            if (bj[3] < bi[1] || bi[3] < bj[1]) continue;
            int a = it[i].pos, b = it[j].pos;
            if (a < b) lpush(&cand[a], b); else lpush(&cand[b], a);
        }
    }
    /* pairs sharing an end point with disjoint filtered boxes cannot exist (the shared point lies in both) */
    for (int i = 0; i < n; i++) {
        int E = order[i];
        pc->stamp518 += 2; pc->stamp470 += 2;
        PcCutEdge *e = &pc->ce[E];
        pc->pt[ptof(pc, e->n0)].stamp = pc->stamp470;
        pc->pt[ptof(pc, e->n1)].stamp = pc->stamp470;
        for (int k = e->split.n - 1; k >= 0; k--) pc->pt[ptof(pc, e->split.v[k])].stamp = pc->stamp470;
        if (cand[i].n > 1) qsort(cand[i].v, (size_t)cand[i].n, sizeof(int), int_cmp);
        for (int k = 0; k < cand[i].n; k++) {
            int F = order[cand[i].v[k]];
            if (pc->ce[F].stamp4c == pc->stamp518) continue;
            pc->ce[F].stamp4c = pc->stamp518;
            pc_pair(pc, E, F, pl);
        }
        lfree(&cand[i]);
    }
    free(cand); free(it); free(order);
}

/* ================================================================== split: FUN_180028c20 */
static int gedge(IfkPlaneCut *pc, int pa, int pb, int ti, int f15)   /* FUN_180029150 */
{
    int lo = pa, hi = pb;
    if (!(pc->pt[pa].key < pc->pt[pb].key)) { lo = pb; hi = pa; }
    long long key = (long long)lo * 0x100000000LL + hi;
    int g = map_get(pc->gemap, key);
    const PcInTri *t = &pc->tri[ti];
    /* InputTriangle vtable slot 0 = FUN_1800012c0 returns 0 (decomp/interfacial/i03_qtree.c) */
    if (g >= 0) {
        PcGEdge *G = &pc->ge[g];
        if (t->flag) G->f11 = 1;
        if (G->f16) { G->f13 = G->f14 = G->f15 = G->f16 = 0; }
        if (f15 && G->f15 == 0) { G->f14 = 0; G->f15 = 1; G->f13 = 0; }
        if (G->p0 == pa) G->f13 = 1; else G->f14 = 1;
        return g;   /* data: max(data, triangle +0x40) = 0 here */
    }
    GROW(pc->ge, pc->nge, pc->cge);
    PcGEdge *G = &pc->ge[pc->nge];
    memset(G, 0, sizeof *G);
    G->p0 = lo; G->p1 = hi;
    if (t->flag) G->f11 = 1;
    if (G->p0 == pa) G->f13 = 1; else G->f14 = 1;
    G->f15 = (unsigned char)f15;
    G->f16 = 0;                      /* pool default 1, cleared because slot 0 returned 0 */
    G->data = 0;
    map_put(pc->gemap, key, pc->nge);
    return pc->nge++;
}
typedef struct { int p0, p1, tri; double d[4]; int haveex; PcSD ex[2]; } SideCtx;   /* FUN_18001d780 record */
static void side_init(IfkPlaneCut *pc, SideCtx *s, int p0, int p1, int tri)   /* FUN_18001d780 */
{
    const double *a = pc->pt[p0].c, *b = pc->pt[p1].c;
    s->p0 = p0; s->p1 = p1; s->tri = tri; s->haveex = 0;
    s->d[0] = b[0] - a[0]; s->d[1] = a[1] + b[1] + fabs(s->d[0]);
    s->d[2] = b[2] - a[2]; s->d[3] = b[3] + a[3] + fabs(s->d[2]);
}
/* FUN_18001d8d0: at point X, the first kind-2 node of X's list made with this edge's triangle names the other
 * triangle T; sign of (T's normal, projected) . (edge direction); 0 when there is none / T flagged / T has no
 * vertex strictly above the plane (below for a flipped set) */
static int side_at(IfkPlaneCut *pc, SideCtx *s, int X, const PcPlane *pl)
{
    PcList *L = &pc->pt[X].nodes;
    int T = -1;
    for (int k = L->n - 1; k >= 0 && T < 0; k--) {
        PcCutNode *n = &pc->node[L->v[k]];
        if (n->kind != 2) continue;
        if (n->ref == s->tri) T = n->ref2; else if (n->ref2 == s->tri) T = n->ref;
    }
    if (T < 0) return 0;
    PcInTri *t = &pc->tri[T];
    if (t->flag) return 0;
    int w = pc->axis, u = pc->ua, v = pc->va;
    const double *x0 = pc->in[t->n[0]].x, *x1 = pc->in[t->n[1]].x, *x2 = pc->in[t->n[2]].x;
    if (!pc->flipped) { if (!(pl->off < x0[w] || pl->off < x1[w] || pl->off < x2[w])) return 0; }
    else if (!(x0[w] < pl->off || x1[w] < pl->off || x2[w] < pl->off)) return 0;
    double a[3] = { x2[0] - x1[0], x2[1] - x1[1], x2[2] - x1[2] }, b[3] = { x0[0] - x1[0], x0[1] - x1[1], x0[2] - x1[2] };
    double p20 = a[v] * b[w], p18 = a[w] * b[u], p19 = a[w] * b[v], p16 = a[u] * b[w];
    F Nu, Nv;
    Nu.v = p20 - p19; Nu.e = fabs(p19) + fabs(p20) + fabs(Nu.v);
    Nv.v = p18 - p16; Nv.e = fabs(p16) + fabs(p18) + fabs(Nv.v);
    F du = { s->d[0], s->d[1] }, dv = { s->d[2], s->d[3] };
    F x1p = f_mul(Nu, du), x2p = f_mul(Nv, dv);
    double sum = x2p.v + x1p.v, e = x2p.e + x1p.e + fabs(sum);
    if (e * U52 < fabs(sum)) return 0.0 < sum ? 1 : -1;
    /* exact: FUN_18001e4d0 (direction P1 - P0 from the exact records, FUN_18001fcf0), SuperInteger normal
     * (FUN_18001b700, FUN_180016dc0, FUN_180017c90, FUN_18001fb10), FUN_180017480 / FUN_18001fc50 */
    if (!s->haveex) {
        pc_exact(pc, head(pc, s->p0), 0); pc_exact(pc, head(pc, s->p1), 0);
        PcExact *A = &pc->ex[pc->pt[s->p0].ex], *B = &pc->ex[pc->pt[s->p1].ex];
        sd_xsub(&s->ex[0], &B->U, &B->D, &A->U, &A->D);
        sd_xsub(&s->ex[1], &B->V, &B->D, &A->V, &A->D);
        s->haveex = 1;
    }
    PcSI sa[3], sb[3];
    PcInNode *n0 = &pc->in[t->n[0]], *n1 = &pc->in[t->n[1]], *n2 = &pc->in[t->n[2]];
    for (int i = 0; i < 3; i++) { sa[i] = si_sub(pc, &n2->s[i], &n1->s[i], 1); sb[i] = si_sub(pc, &n0->s[i], &n1->s[i], 1); }
    PcSI m1 = si_mul(&sa[v], 1, &sb[w], 1), m2 = si_mul(&sa[w], 1, &sb[u], 1);
    PcSI m3 = si_mul(&sa[w], 1, &sb[v], 1), m4 = si_mul(&sa[u], 1, &sb[w], 1);
    PcSI nu = si_sub(pc, &m1, &m3, 2), nv = si_sub(pc, &m2, &m4, 2);
    PcSD Su, Sv, l, r, o;
    sd_from_si(&Su, &nu, 2, pc->k); sd_from_si(&Sv, &nv, 2, pc->k);
    sd_mul(&l, &Su, &s->ex[0]); sd_mul(&r, &Sv, &s->ex[1]);
    sd_addsigned(&o, &l, r.pos, &r);
    int sg = sd_sign(&o);
    if (sg == 0) pc_error(pc, "PlaneCut error 8: assertion failed.");
    return sg;
}
static void pc_split(IfkPlaneCut *pc, const PcPlane *pl)
{
    for (int li = pc->celist.n - 1; li >= 0; li--) {   /* pop impl+0x90 from its head */
        int E = pc->celist.v[li];
        PcCutEdge *e = &pc->ce[E];
        int a0 = ptof(pc, e->n0), a1 = ptof(pc, e->n1);
        if (e->split.n == 0) { gedge(pc, a0, a1, e->tri, e->flag48); continue; }
        pc->stamp470 += 2;
        pc->pt[a0].stamp = pc->stamp470; pc->pt[a1].stamp = pc->stamp470;
        PcList L = { 0, 0, NULL };                     /* puVar17: push-front of the unmarked points */
        for (int k = e->split.n - 1; k >= 0; k--) {
            int p = ptof(pc, e->split.v[k]);
            if (pc->pt[p].stamp != pc->stamp470) { pc->pt[p].stamp = pc->stamp470; lpush(&L, p); }
        }
        e->split.n = 0;
        if (L.n == 0) gedge(pc, a0, a1, e->tri, e->flag48);
        else if (L.n == 1) {
            int X = L.v[0];
            int g1 = gedge(pc, a0, X, e->tri, e->flag48), g2 = gedge(pc, X, a1, e->tri, e->flag48);
            SideCtx s; side_init(pc, &s, a0, a1, e->tri);
            int r = side_at(pc, &s, X, pl);
            if (0 < r) pc->ge[g1].f10 = 1; else if (r < 0) pc->ge[g2].f10 = 1;
        } else {
            if (!pick_axis(pc, a0, a1)) pc_error(pc, "PlaneCut error 7: redundancy.");
            /* FUN_1800322c0 tree: insert L from its head, then A0, A1; equal -> error 3, goes right */
            int m = L.n + 2, *seq = malloc(sizeof(int) * (size_t)m), cnt = 0;
            int *ins = malloc(sizeof(int) * (size_t)m);
            for (int k = L.n - 1; k >= 0; k--) ins[cnt++] = L.v[k];
            ins[cnt++] = a0; ins[cnt++] = a1;
            int ns = 0;
            for (int k = 0; k < cnt; k++) {   /* in-order position: after every element not greater */
                int pos = ns;
                for (int j = 0; j < ns; j++) {
                    int c = cmp_axis(pc, ins[k], seq[j]);
                    if (c == 0) pc_error(pc, "PlaneCut error 3: duplicate nodes.");
                    if (c == -1) { pos = j; break; }
                }
                memmove(seq + pos + 1, seq + pos, sizeof(int) * (size_t)(ns - pos));
                seq[pos] = ins[k]; ns++;
            }
            int first = seq[0];
            SideCtx s;
            if (first == a0) side_init(pc, &s, a1, a0, e->tri); else side_init(pc, &s, a0, a1, e->tri);
            int carry = 0;
            for (int k = 1; k < ns; k++) {
                int prev = seq[k - 1], cur = seq[k], g;
                if (first != a0) g = gedge(pc, cur, prev, e->tri, e->flag48);
                else g = gedge(pc, prev, cur, e->tri, e->flag48);
                if (carry) { pc->ge[g].f10 = 1; carry = 0; }
                int r = side_at(pc, &s, cur, pl);
                if (r >= 1) carry = 1; else if (r < 0) pc->ge[g].f10 = 1;
            }
            free(seq); free(ins);
        }
        lfree(&L);
    }
}

static void pc_chains(IfkPlaneCut *pc, int plane);
/* ================================================================== Run, stage A */
static int pc_run_impl(IfkPlaneCut *pc, IfkPcPlaneCb cb, void *user, int stageb)
{
    if (pc->nplane == 0) return 0;                     /* impl+0x84 == 0 */
    if (!pc->aligned || pc->vertical) return -1;       /* not ported (see the file header) */
    double nn = pc->n[0] * pc->n[0] + pc->n[1] * pc->n[1] + pc->n[2] * pc->n[2];
    /* "Preprocessing...": impl+0x88 from its head (newest triangle first) */
    for (int ti = pc->ntri - 1; ti >= 0; ti--) {
        const PcInTri *t = &pc->tri[ti];
        double h[3];
        for (int i = 0; i < 3; i++) {
            const double *x = pc->in[t->n[i]].x;
            h[i] = (pc->n[1] * x[1] + pc->n[0] * x[0] + pc->n[2] * x[2]) / nn;
        }
        double lo, hi;
        if (h[1] <= h[0]) {
            if (h[2] <= h[1]) { lo = h[2]; hi = h[0]; }
            else if (h[2] <= h[0]) { lo = h[1]; hi = h[0]; }
            else { lo = h[1]; hi = h[2]; }
        } else {
            if (h[0] < h[2]) { lo = h[0]; hi = h[2] <= h[1] ? h[1] : h[2]; }
            else { lo = h[2]; hi = h[1]; }
        }
        lo = lo - 1e-07; hi = hi + 1e-07;
        if (pc->aligned && pc->axis == 2 && lo < pc->ground) lo = pc->ground;
        for (int p = 0; p < pc->nplane; p++)           /* FUN_1800252b0 .. FUN_180025210 */
            if (lo <= pc->plane[p].off && pc->plane[p].off <= hi) lpush(&pc->plane[p].tris, ti);
    }
    for (int p = pc->nplane - 1; p >= 0; p--) {        /* rightmost first: decreasing offset */
        PcPlane *pl = &pc->plane[p];
        g_plane = pl;
        pc_clear_plane(pc);
        for (int k = pl->tris.n - 1; k >= 0; k--) cut_triangle(pc, pl->tris.v[k], pl);   /* pop from head */
        pl->tris.n = 0;
        pc_pairs(pc, pl);
        pc_split(pc, pl);
        if (stageb) pc_chains(pc, p);                  /* FUN_180029630 */
        if (pc->overflow) pc_error(pc, "PlaneCut error 6: overflow");
        if (cb) cb(pc, p, user);
    }
    return 0;
}

/* ==================================================================================================================
 * STAGE B (formats/interfacial.md 11.8): FUN_180029630 onwards. Decompiles: decomp/interfacial/i03_planecut_all.c,
 * i03_curvenode.c (CutCurveNode / CutTestPoint vtables, FUN_1800148b0), i03_stageb_extra.c, i03_vt_curve.txt.
 * Scope: one cut plane (Silhouette's), mode 1 (the tree; Silhouette's Run(label, 1, 0.0)).
 * ================================================================================================================ */
static void lpop_at(PcList *l, int i) { memmove(l->v + i, l->v + i + 1, sizeof(int) * (size_t)(l->n - i - 1)); l->n--; }
/* remove the first element equal to v walking from the head (= our end) */
static void lremove_head(PcList *l, int v)
{
    for (int i = l->n - 1; i >= 0; i--) if (l->v[i] == v) { lpop_at(l, i); return; }
}
typedef struct { int *v; int h, t, cap; } Fifo;   /* the DLL's head/tail lists appended at the tail (ours) */
static void fpush(Fifo *q, int x)
{
    if (q->t == q->cap) { q->cap = q->cap ? 2 * q->cap : 16; q->v = realloc(q->v, sizeof(int) * (size_t)q->cap); }
    q->v[q->t++] = x;
}
static int fempty(const Fifo *q) { return q->h == q->t; }
static int fpop(Fifo *q) { return q->v[q->h++]; }

/* FUN_180012f50(point, plane): sign of (w - ground), filtered. The node scan and the exact branch after it are only
 * reached when |w - ground| is uncertain; with the ground -DBL_MAX (Silhouette never calls SetGround) the
 * difference is DBL_MAX and the filtered branch always decides, so they are not ported (reported if reached). */
static int pc_ground(IfkPlaneCut *pc, int p)
{
    int i = (pc->ua == 2) ? 0 : ((pc->va != 2) + 1);
    const double *c = pc->pt[p].c;
    double d = c[2 * i] - pc->ground, ad = fabs(d);
    if ((c[2 * i + 1] + ad) * U52 < ad) return (pc->ground <= c[2 * i] && c[2 * i] != pc->ground) ? 1 : -1;
    if (c[2 * i + 1] == 0.0) return 0;
    pc_error(pc, "ifk_planecut: FUN_180012f50 exact branch not ported");
    return 0;
}

/* FUN_18002a550: copy of a point - the six doubles and the head cut node (kind, refs) - for a curve */
static int pt_copy(IfkPlaneCut *pc, int p)
{
    int q = pt_alloc(pc);
    memcpy(pc->pt[q].c, pc->pt[p].c, sizeof pc->pt[q].c);
    PcCutNode h = pc->node[head(pc, p)];
    int n = node_new(pc, h.kind, h.ref, h.ref2);
    pc->node[n].pt = q;
    lpush(&pc->pt[q].nodes, n);
    return q;
}

/* FUN_1800131d0(point, plane): the point's CutCurveNode, made once (+0x60) */
static int curve_node(IfkPlaneCut *pc, int p)
{
    if (pc->pt[p].cnode >= 0) return pc->pt[p].cnode;
    GROW(pc->cn, pc->ncn, pc->ccn);
    int k = pc->ncn++;
    PcCNode *N = &pc->cn[k];
    memset(N, 0, sizeof *N);
    N->x[pc->ua] = pc->pt[p].c[0]; N->x[pc->va] = pc->pt[p].c[2]; N->x[pc->axis] = pc->pt[p].c[4];
    pc->pt[p].cnode = k;
    N->onground = pc_ground(pc, p) == 0;
    N->pt = -1;
    if (pc->mode == 1) { int q = pt_copy(pc, p); pc->cn[k].pt = q; }   /* +0x38 = {copy, plane} (pool +0x1b0) */
    return k;
}

/* FUN_180029e40: prune dangling edges; q holds points whose +0x48 list had exactly one edge when queued */
static void pc_prune(IfkPlaneCut *pc, Fifo *q)
{
    while (!fempty(q)) {
        int p = fpop(q);
        PcList *L = &pc->pt[p].gel;
        if (L->n == 0) continue;
        int g = L->v[--L->n];                       /* pop the head */
        PcGEdge *G = &pc->ge[g];
        if (G->f11) continue;                       /* +0x11 set: not removed */
        int o = (G->p0 == p) ? G->p1 : G->p0;
        lremove_head(&pc->pt[o].gel, g);
        G->f10 = 1;
        if (pc->pt[o].gel.n == 1) fpush(q, o);
    }
}

/* ---- FUN_180029f90: angle sort at a point of degree >= 3 ---------------------------------------------------- */
typedef struct {                       /* CutEdgeAngleSort record (pool impl+0x190, 0x50 bytes) */
    double key, err;                   /* [0] / [1]: dx^2 / (dx^2 + dy^2) and its error */
    int hasex; PcSD num, den;          /* [2]: exact dx^2 (+0) and dx^2 + dy^2 (+0x68) */
    int edge;                          /* [3] */
    int X, Y;                          /* +0x20: dx < 0, +0x21: dy < 0 */
    int par, l, r, h;                  /* AVL: +0x30 parent, +0x38 left, +0x40 right, +0x48 height */
} AngRec;
static int g_angP;                     /* DAT_18008f4a0: the point being sorted */
static void ang_exact(IfkPlaneCut *pc, AngRec *a)   /* FUN_180013880 */
{
    if (a->hasex) return;
    a->hasex = 1;
    const PcGEdge *G = &pc->ge[a->edge];
    pc_exact(pc, head(pc, G->p0), 0);
    pc_exact(pc, head(pc, G->p1), 0);
    int O = (G->p0 == g_angP) ? G->p1 : G->p0;
    const PcExact *XO = &pc->ex[pc->pt[O].ex], *XP = &pc->ex[pc->pt[g_angP].ex];
    PcSD dx, dy, dy2;
    sd_xsub(&dx, &XO->U, &XO->D, &XP->U, &XP->D);   /* FUN_180016fd0 */
    sd_xsub(&dy, &XO->V, &XO->D, &XP->V, &XP->D);
    a->X = sd_sign(&dx) < 0; a->Y = sd_sign(&dy) < 0;
    sd_mul(&a->num, &dx, &dx);                       /* FUN_180017630 */
    sd_mul(&dy2, &dy, &dy);                          /* FUN_180017170 */
    sd_addsigned(&a->den, &a->num, dy2.pos, &dy2);   /* FUN_1800177e0 */
    double n = sd_to_double(&a->num), d = sd_to_double(&a->den), q = n / d;   /* FUN_1800161f0 */
    a->key = q;
    a->err = (fabs(d) * fabs(q) + fabs(n)) / (fabs(d) - fabs(d) * U52) + fabs(q);
}
static void ang_key(IfkPlaneCut *pc, AngRec *a, int g)   /* FUN_180013580(record, edge) */
{
    a->edge = g;
    const PcGEdge *G = &pc->ge[g];
    const double *P = pc->pt[g_angP].c, *O = pc->pt[(G->p0 == g_angP) ? G->p1 : G->p0].c;
    double dx = O[0] - P[0], edx = O[1] + P[1] + fabs(dx);
    double dy = O[2] - P[2], edy = O[3] + P[3] + fabs(dy);
    if (edx * U52 < fabs(dx) && edy * U52 < fabs(dy)) {
        a->X = dx < 0.0; a->Y = dy < 0.0;
        double x2 = dx * dx, ex2 = edx * fabs(dx) + edx * fabs(dx) + edx * edx * U53 + fabs(x2);
        double y2 = dy * dy, s = y2 + x2;
        double es = edy * fabs(dy) + edy * fabs(dy) + edy * edy * U53 + fabs(y2) + ex2 + fabs(s);
        if (es * U52 < fabs(s)) {
            double q = x2 / s;
            a->key = q;
            a->err = (es * fabs(q) + ex2) / (fabs(s) - es * U52) + fabs(q);
            return;
        }
    }
    ang_exact(pc, a);
}
static int ang_gt(IfkPlaneCut *pc, AngRec *a, AngRec *b)   /* FUN_180013c40: a.key > b.key */
{
    double d = a->key - b->key;
    if ((b->err + a->err + fabs(d)) * U52 < fabs(d)) return b->key < a->key;
    ang_exact(pc, a); ang_exact(pc, b);
    PcSD l, r;
    sd_mul(&l, &a->num, &b->den); sd_mul(&r, &b->num, &a->den);   /* FUN_18000e0b0 x 2 */
    int c = sd_cmp(&l, &r);                                       /* FUN_18000e260 */
    if (c == 0) pc_error(pc, "PlaneCut error 13: assertion failed.");
    return 0 < c;
}
/* FUN_1800323b0 descent: 1 = the new record n goes right of the tree record t, 0 = left. The resulting in-order is
 * [dy >= 0: angle from pi down to 0] then [dy < 0: angle from 2 pi down to pi] (formats/interfacial.md 11.8.2). */
static int ang_right(IfkPlaneCut *pc, AngRec *n, AngRec *t)
{
    if (!n->Y) {
        if (t->Y) return 0;                                                   /* LAB_18003242b */
        if (!n->X) { if (t->X) return 1; return ang_gt(pc, n, t) ? 1 : 0; }   /* LAB_18003243f: 1 / -1 */
        if (!t->X) return 0;
        return ang_gt(pc, n, t) ? 0 : 1;                                      /* LAB_180032404: -1 / 1 */
    }
    if (!t->Y) return 1;                                                      /* LAB_180032457 */
    if (!n->X) { if (!t->X) return ang_gt(pc, n, t) ? 0 : 1; return 0; }
    if (t->X) return ang_gt(pc, n, t) ? 1 : 0;
    return 1;
}
static int avl_h(AngRec *A, int i) { return i < 0 ? 0 : A[i].h; }
static void avl_fix_h(AngRec *A, int x)
{
    int hl = avl_h(A, A[x].l), hr = avl_h(A, A[x].r);
    A[x].h = (hr < hl ? hl : hr) + 1;
}
static void avl_rot_left(AngRec *A, int *root, int x)    /* FUN_18003c3a0 */
{
    int y = A[x].r;
    if (x == *root) *root = y;
    else { int p = A[x].par; if (A[p].l == x) A[p].l = y; else A[p].r = y; }
    A[y].par = A[x].par;
    int b = A[y].l; A[y].l = x; A[x].par = y; A[x].r = b; if (b >= 0) A[b].par = x;
    avl_fix_h(A, x); avl_fix_h(A, y);
}
static void avl_rot_right(AngRec *A, int *root, int x)   /* FUN_18003c2e0 */
{
    int y = A[x].l;
    if (x == *root) *root = y;
    else { int p = A[x].par; if (A[p].l == x) A[p].l = y; else A[p].r = y; }
    A[y].par = A[x].par;
    int b = A[y].r; A[y].r = x; A[x].par = y; A[x].l = b; if (b >= 0) A[b].par = x;
    avl_fix_h(A, x); avl_fix_h(A, y);
}
static void avl_insert(IfkPlaneCut *pc, AngRec *A, int *root, int n)   /* FUN_1800323b0 + FUN_18003b3a0 */
{
    A[n].h = 1; A[n].l = A[n].r = -1; A[n].par = -1;
    if (*root < 0) { *root = n; return; }
    int t = *root, side;
    for (;;) {
        side = ang_right(pc, &A[n], &A[t]);
        int nx = side ? A[t].r : A[t].l;
        if (nx < 0) break;
        t = nx;
    }
    if (side) A[t].r = n; else A[t].l = n;
    A[n].par = t;
    int p = n, x = t;                               /* FUN_18003b3a0 */
    for (;;) {
        int L = A[x].l, R = A[x].r, hl = avl_h(A, L), hr = avl_h(A, R);
        if (hr < hl) {
            if (1 < hl - hr) {
                if (avl_h(A, A[L].l) < avl_h(A, A[L].r)) avl_rot_left(A, root, L);
                avl_rot_right(A, root, x); return;
            }
        } else if (1 < hr - hl) {
            if (avl_h(A, A[R].r) < avl_h(A, A[R].l)) avl_rot_right(A, root, R);
            avl_rot_left(A, root, x); return;
        }
        if (p == L) { if (R >= 0 && A[p].h <= A[R].h) return; }
        else if (L >= 0 && A[p].h <= A[L].h) return;
        A[x].h++;
        p = x; x = A[x].par;
        if (x < 0) return;
    }
}
static int avl_succ(AngRec *A, int c)
{
    if (A[c].r >= 0) { c = A[c].r; while (A[c].l >= 0) c = A[c].l; return c; }
    int p = A[c].par;
    while (p >= 0 && A[p].l != c) { c = p; p = A[p].par; }
    return p;
}
static int dir_start(const PcGEdge *G) { return G->f13 ? G->p0 : G->p1; }   /* +0x13 == 0 -> [1], else [0] */
static void pc_angle_sort(IfkPlaneCut *pc, int P, Fifo *q)   /* FUN_180029f90 */
{
    g_angP = P;
    PcList *L = &pc->pt[P].gel;
    int n = L->n, root = -1;
    AngRec *A = calloc((size_t)n, sizeof *A);
    for (int k = 0; k < n; k++) {                    /* +0x48 from its head: FUN_180013580, FUN_1800323b0 */
        ang_key(pc, &A[k], L->v[n - 1 - k]);
        avl_insert(pc, A, &root, k);
    }
    pc->nangle++;
    int first = -1, last = -1;                       /* lVar10 = leftmost; the rightmost when needed */
    if (root >= 0) {
        first = root; while (A[first].l >= 0) first = A[first].l;
        last = root; while (A[last].r >= 0) last = A[last].r;
    }
    int prev = -1, cur = first;                      /* lVar12 / lVar11 */
    for (;;) {
        int E, predE, succE;
        for (;;) {                                   /* innermost loop: in-order; skip both directions or +0x15 = 0 */
            if (cur < 0) { free(A); return; }        /* the records are freed (FUN_18003b4e0): no effect */
            predE = prev; E = cur;
            succE = avl_succ(A, cur);
            prev = cur; cur = succE;
            const PcGEdge *G = &pc->ge[A[E].edge];
            if (!((G->f13 && G->f14) || G->f15 == 0)) break;
        }
        int Eout = dir_start(&pc->ge[A[E].edge]) == P;
        int F;                                       /* outgoing E: cyclic predecessor; incoming: successor */
        if (Eout) F = (predE < 0) ? last : predE;
        else F = (succE < 0) ? first : succE;
        const PcGEdge *GF = &pc->ge[A[F].edge];
        if ((GF->f13 && GF->f14) || GF->f11) continue;           /* middle loop */
        if ((dir_start(GF) == P) != Eout) continue;              /* outer loop condition */
        int g = A[F].edge;                                       /* both outgoing or both incoming: drop F */
        lremove_head(&pc->pt[pc->ge[g].p0].gel, g);
        lremove_head(&pc->pt[pc->ge[g].p1].gel, g);
        pc->ge[g].f10 = 1;
        int o = (pc->ge[g].p0 == P) ? pc->ge[g].p1 : pc->ge[g].p0;
        if (pc->pt[o].gel.n == 1) fpush(q, o);
    }
}

/* ---- FUN_180029630: prune, angle sort, chains ------------------------------------------------------------------ */
static int cedge_new(IfkPlaneCut *pc)
{
    GROW(pc->cedg, pc->nce2, pc->cce2);
    memset(&pc->cedg[pc->nce2], 0, sizeof(PcCEdge));
    return pc->nce2++;
}
static void pc_chains(IfkPlaneCut *pc, int plane)
{
    Fifo q = { 0 };
    /* impl+0xa8 from its head (= the newest graph edge) */
    for (int g = pc->nge - 1; g >= 0; g--) {
        PcGEdge *G = &pc->ge[g];
        if (!G->f15 && !G->f16) G->f10 = 1;
        int r0 = pc_ground(pc, G->p0);
        if (r0 < 1) {
            int r1 = pc_ground(pc, G->p1);
            if (r1 < 0 || (r1 == 0 && r0 < 0)) G->f10 = 1;
        }
        if (!G->f10) { lpush(&pc->pt[G->p0].gel, g); lpush(&pc->pt[G->p1].gel, g); }   /* push-front, pool +0x138 */
    }
    /* points of impl+0xa0 (from its head) with exactly one edge, appended in that order (pool +0x1e0) */
    for (int i = pc->ptlist.n - 1; i >= 0; i--) {
        int p = pc->ptlist.v[i];
        if (pc->pt[p].gel.n == 1) fpush(&q, p);
    }
    pc_prune(pc, &q);                                                      /* FUN_180029e40 */
    for (int i = pc->ptlist.n - 1; i >= 0; i--) {
        int p = pc->ptlist.v[i];
        if (pc->pt[p].gel.n >= 3) pc_angle_sort(pc, p, &q);                /* FUN_180029f90 */
    }
    pc_prune(pc, &q);
    /* impl+0xd8 (PlaneCutBuffered) is 0 for PlaneCut: the FUN_180003d90 loop is skipped */
    pc->stamp470 += 2;
    PcList discard = { 0, 0, NULL };                                       /* local_res8 (pool +0x1c0, push-front) */
    for (int g0 = pc->nge - 1; g0 >= 0; g0--) {
        PcGEdge *G0 = &pc->ge[g0];
        if (G0->f12 || G0->f10 || G0->f11 || G0->f16) continue;
        GROW(pc->cv, pc->ncv, pc->ccv);                                    /* FUN_18000c040 (pool +0x180) */
        int C = pc->ncv++;
        memset(&pc->cv[C], 0, sizeof(PcCurve));
        pc->cv[C].plane = plane;
        G0->f12 = 1;
        Fifo w = { 0 };                                                    /* pool +0x1e8 list */
        fpush(&w, g0);
        int last = -1;                                                     /* plVar9 */
        while (!fempty(&w)) {
            int cur = fpop(&w);
            int P0 = pc->ge[cur].p0, P1 = pc->ge[cur].p1;
            curve_node(pc, P0); curve_node(pc, P1);                        /* FUN_1800131d0 x 2 */
            for (int s = 0; s < 2; s++) {                                  /* [0] side, then [1] side */
                PcList *L = &pc->pt[s ? P1 : P0].gel;
                if (L->n < 2) continue;    /* the DLL reads the second record unguarded: never < 2 after pruning
                                            * unless +0x11 edges survive (InputTriangle flag; Silhouette: 0) */
                if (L->n != 2) continue;                                   /* degree exactly 2 */
                int o = L->v[L->n - 1];
                if (o == cur) o = L->v[L->n - 2];
                PcGEdge *O = &pc->ge[o];
                if (!O->f12 && !O->f11 && !O->f16) { O->f12 = 1; fpush(&w, o); }
            }
            int ce = cedge_new(pc);                                        /* FUN_18000b8c0 (pool +0x178) */
            PcGEdge *G = &pc->ge[cur];
            PcCEdge *E = &pc->cedg[ce];
            if (!G->f13) { E->n0 = pc->pt[P1].cnode; E->n1 = pc->pt[P0].cnode; E->both = 0; last = cur; }
            else {
                E->n0 = pc->pt[P0].cnode; E->n1 = pc->pt[P1].cnode; E->both = G->f14;
                if (!G->f14) last = cur;
            }
            pc->cn[E->n0].ref++; pc->cn[E->n1].ref++;
            E->data = G->data;
            lpush(&pc->cv[C].edges, ce);                                   /* push-front on curve [2] */
        }
        free(w.v);
        if (last >= 0) {
            lpush(&pc->curves, C);                                         /* FUN_180009de0: appended to impl+0xb0 */
            int ends[2] = { pc->ge[last].p0, pc->ge[last].p1 };
            for (int s = 0; s < 2; s++) {
                int p = ends[s];
                if (pc->pt[p].stamp == pc->stamp470) pc->cv[C].p[s] = pt_copy(pc, p);   /* FUN_18002a550 */
                else { pc->pt[p].stamp = pc->stamp470; pc->cv[C].p[s] = p; }
            }
        } else lpush(&discard, C);
    }
    /* discarded curves (all edges both ways): edges freed, nodes whose count drops to 0 freed (FUN_18002a480: the
     * node's point copy goes back to the pool) */
    for (int i = discard.n - 1; i >= 0; i--) {
        PcCurve *Cv = &pc->cv[discard.v[i]];
        for (int k = Cv->edges.n - 1; k >= 0; k--) {
            PcCEdge *E = &pc->cedg[Cv->edges.v[k]];
            int ns[2] = { E->n0, E->n1 };
            for (int s = 0; s < 2; s++)
                if (--pc->cn[ns[s]].ref == 0 && pc->cn[ns[s]].pt >= 0) { pt_free(pc, pc->cn[ns[s]].pt); pc->cn[ns[s]].pt = -1; }
        }
        Cv->edges.n = 0;
    }
    lfree(&discard);
    free(q.v);
    /* tail: graph edges freed (impl+0xa8 emptied), point tree impl+0xc8 = 0, points of impl+0xa0 not stamped by a
     * curve freed with their nodes and exact records (pool order kept by pt_free). */
    for (int i = pc->ptlist.n - 1; i >= 0; i--) {
        int p = pc->ptlist.v[i];
        pc->pt[p].gel.n = 0;
        if (pc->pt[p].stamp != pc->stamp470) pt_free(pc, p);
    }
    pc->ptlist.n = 0;
    pc->ptroot = -1;
}

/* ---- test points (FUN_180014710, CutTestPoint vtable slot 0 FUN_1800148b0) ----------------------------------- */
static int tp_new(IfkPlaneCut *pc, int C)   /* FUN_180031680 (pool impl+0x188) + FUN_180014710(tp, curve) */
{
    GROW(pc->tp, pc->ntp, pc->ctp);
    int t = pc->ntp++;
    PcTestPoint *T = &pc->tp[t];
    memset(T, 0, sizeof *T);
    T->a = pc->cv[C].p[0]; T->b = pc->cv[C].p[1]; T->curve = C;
    const double *A = pc->pt[T->a].c, *B = pc->pt[T->b].c;
    int ax[3] = { pc->ua, pc->va, pc->axis };
    for (int k = 0; k < 3; k++) {                    /* (+0x4c axis) u, (+0x50) v, (+0x48) w */
        double s = A[2 * k] + B[2 * k];
        T->c[2 * ax[k]] = s;
        T->c[2 * ax[k] + 1] = A[2 * k + 1] + B[2 * k + 1] + fabs(s);
    }
    for (int i = 0; i < 6; i++) T->c[i] = T->c[i] * 0.5;
    return t;
}
double *ifk_pc_tp_coords(IfkPlaneCut *pc, int tp) { return pc->tp[tp].c; }
void ifk_pc_tp_passed(IfkPlaneCut *pc, int tp) { pc->tp[tp].passed = 1; }   /* PlaneCutBase::TestPointPassed */
void ifk_pc_tp_exact(IfkPlaneCut *pc, int tp, PcSD *X, PcSD *Y, PcSD *W)   /* FUN_1800148b0 */
{
    PcTestPoint *T = &pc->tp[tp];
    if (!T->hasex) {
        T->hasex = 1;
        pc_exact(pc, head(pc, T->a), 1);             /* FUN_18001bde0(.., plane, 1) on [0] then [1] */
        pc_exact(pc, head(pc, T->b), 1);
        const PcExact *A = &pc->ex[pc->pt[T->a].ex], *B = &pc->ex[pc->pt[T->b].ex];
        const PcSD *ua[3] = { &A->U, &A->V, &A->W }, *ub[3] = { &B->U, &B->V, &B->W };
        int ax[3] = { pc->ua, pc->va, pc->axis };
        for (int k = 0; k < 3; k++) {                /* FUN_180017890: a bden + b aden */
            PcSD p, q;
            sd_mul(&p, ua[k], &B->D); sd_mul(&q, ub[k], &A->D);
            sd_addsigned(&T->X[ax[k]], &p, q.pos, &q);
        }
        PcSD dd, two; sd_mul(&dd, &A->D, &B->D);     /* FUN_18000e0b0, FUN_180017a30 with the constant 2 */
        memset(&two, 0, sizeof two); two.pos = 1; two.n = 1; two.l[0] = 2;
        sd_mul(&T->X[3], &dd, &two);
        /* FUN_180017bd0 -> FUN_180016cc0: the doubles are replaced by X[i] / W */
        double w = sd_to_double(&T->X[3]), aw = fabs(w);
        for (int i = 0; i < 3; i++) {
            double x = sd_to_double(&T->X[i]), q = x / w;
            T->c[2 * i] = q;
            T->c[2 * i + 1] = (aw * fabs(q) + fabs(x)) / (fabs(w) - aw * U52) + fabs(q);
        }
    }
    if (X) *X = T->X[pc->ua];
    if (Y) *Y = T->X[pc->va];
    if (W) *W = T->X[3];
}

/* ---- curve-node predicates (CutCurveNode vtable 1800739d8; handle -1 = the reference node of FUN_1800152c0) --- */
static int g_refpt = -1;               /* the reference node's point (FUN_18000a1b0 in FUN_1800152c0) */
static double g_refxy[2];              /* the reference node's +8 / +0x10 (slot 4 FUN_180012d90) */
static int g_raynode = -1;             /* the node FUN_18003d480 tests (its ray starts at the stack node local_48) */
static int cn_pt(IfkPlaneCut *pc, int h) { return h < 0 ? g_refpt : pc->cn[h].pt; }
static void cn_xy(IfkPlaneCut *pc, int h, double *x, double *y)   /* slot 0 FUN_180012470: +8, +0x10 */
{
    if (h < 0) { *x = g_refxy[0]; *y = g_refxy[1]; return; }
    *x = pc->cn[h].x[0]; *y = pc->cn[h].x[1];
}
/* exact double -> PlaneCutSuperDouble (FUN_1800160c0) */
static void sd_from_double(PcSD *r, double v)
{
    memset(r, 0, sizeof *r);
    r->pos = (unsigned char)(v >= 0.0); r->n = 1;
    if (v == 0.0) return;
    int e2; double m = frexp(fabs(v), &e2);
    uint64_t M = (uint64_t)ldexp(m, 53); e2 -= 53;           /* |v| = M 2^e2 */
    int we = (e2 >= 0) ? e2 / 32 : -((-e2 + 31) / 32), sh = e2 - 32 * we;
    uint32_t l[4] = { 0 };
    unsigned __int128 x = (unsigned __int128)M << sh;
    for (int i = 0; i < 4; i++) l[i] = (uint32_t)(x >> (32 * i));
    r->n = 4; r->e = we;
    memcpy(r->l, l, sizeof l);
    sd_trim(r);
}
static const PcExact *cn_exact(IfkPlaneCut *pc, int h)   /* FUN_180012dc0 */
{
    int p = cn_pt(pc, h);
    if (pc->pt[p].ex < 0) {
        if (pc->pt[p].nodes.n == 0) {                /* no cut node: from the doubles, D = 1 */
            GROW(pc->ex, pc->nex, pc->cex);
            PcExact *X = &pc->ex[pc->nex];
            memset(X, 0, sizeof *X);
            sd_from_double(&X->U, pc->pt[p].c[0]);
            sd_from_double(&X->V, pc->pt[p].c[2]);
            sd_one(&X->D);
            pc->pt[p].ex = pc->nex++;
        } else pc_exact(pc, head(pc, p), 0);         /* FUN_18001bde0(head, plane, 0) */
    }
    return &pc->ex[pc->pt[p].ex];
}
/* slot 1 FUN_180012480(t, &sign, a, b): filtered (t - a) x (b - a); returns "certain" */
static int cn_orient_f(IfkPlaneCut *pc, int t, int a, int b, int *sg)
{
    const double *A = pc->pt[cn_pt(pc, a)].c, *B = pc->pt[cn_pt(pc, b)].c, *T = pc->pt[cn_pt(pc, t)].c;
    double dx = B[0] - A[0], edx = A[1] + B[1] + fabs(dx);
    double dy = B[2] - A[2], edy = A[3] + B[3] + fabs(dy);
    double tx = T[0] - A[0], etx = A[1] + T[1] + fabs(tx);
    double ty = T[2] - A[2], ety = A[3] + T[3] + fabs(ty);
    double p1 = tx * dy, p2 = ty * dx, v = p1 - p2;
    double e = ety * fabs(dx) + edx * fabs(ty) + ety * edx * U53 + fabs(p2) + etx * fabs(dy) + edy * fabs(tx)
             + etx * edy * U53 + fabs(p1) + fabs(v);
    if (e * U52 < fabs(v)) { *sg = 0.0 < v ? 1 : -1; return 1; }
    *sg = 0;
    return e == 0.0;
}
static int cn_orient_x(IfkPlaneCut *pc, int t, int a, int b)   /* slot 2 FUN_1800126c0 */
{
    cn_exact(pc, a); cn_exact(pc, b); cn_exact(pc, t);
    const PcExact *A = cn_exact(pc, a), *B = cn_exact(pc, b), *T = cn_exact(pc, t);
    PcSD d1x, d1y, d2x, d2y, l, r, o;
    sd_xsub(&d1x, &B->U, &B->D, &A->U, &A->D); sd_xsub(&d1y, &B->V, &B->D, &A->V, &A->D);
    sd_xsub(&d2x, &T->U, &T->D, &A->U, &A->D); sd_xsub(&d2y, &T->V, &T->D, &A->V, &A->D);
    sd_mul(&l, &d2x, &d1y); sd_mul(&r, &d2y, &d1x);
    sd_addsigned(&o, &l, (unsigned char)(r.pos == 0), &r);   /* FUN_180017340 */
    return sd_sign(&o);
}
static int cn_orient(IfkPlaneCut *pc, int t, int a, int b)   /* slot 1, slot 2 when not certain */
{
    int s;
    if (cn_orient_f(pc, t, a, b, &s)) return s;
    return cn_orient_x(pc, t, a, b);
}
static int cn_cmp(IfkPlaneCut *pc, int t, int o, int axis)   /* slot 3 FUN_180012b20: sign of t - o on axis */
{
    const double *T = pc->pt[cn_pt(pc, t)].c, *O = pc->pt[cn_pt(pc, o)].c;
    double a = T[2 * axis], b = O[2 * axis], d = a - b;
    if (fabs(d) <= (T[2 * axis + 1] + O[2 * axis + 1] + fabs(d)) * U52) {
        cn_exact(pc, t); cn_exact(pc, o);
        const PcExact *XT = cn_exact(pc, t), *XO = cn_exact(pc, o);
        PcSD l, r;
        sd_mul(&l, &XO->D, axis ? &XT->V : &XT->U);
        sd_mul(&r, &XT->D, axis ? &XO->V : &XO->U);
        return sd_cmp(&l, &r);
    }
    if (a < b) return -1;
    return b < a;
}

/* ---- Polygon2D (FUN_18003e810 / FUN_18003cd10 / FUN_18003cdc0 / FUN_18003cec0 / FUN_18003d480) ---------------- */
#define PG_TOL 1e-07                   /* Polygon2DMemory +0x38 = 0x3e7ad7f29abcaf48 (FUN_180014f70 -> FUN_18003e510) */
static int pn_new(IfkPlaneCut *pc, PcPoly *P, int cn)   /* FUN_18003cd10 */
{
    GROW(pc->pn, pc->npn, pc->cpn);
    PcPNode *N = &pc->pn[pc->npn];
    memset(N, 0, sizeof *N);
    N->cn = cn;
    double x, y; cn_xy(pc, cn, &x, &y);
    if (x < P->box[0]) P->box[0] = x;
    if (P->box[2] <= x && x != P->box[2]) P->box[2] = x;
    if (y < P->box[1]) P->box[1] = y;
    if (P->box[3] <= y && y != P->box[3]) P->box[3] = y;
    return pc->npn++;
}
static void pe_new(IfkPlaneCut *pc, PcPoly *P, int from, int to)   /* FUN_18003cdc0 */
{
    GROW(pc->pe, pc->npe, pc->cpe);
    int e = pc->npe++;
    pc->pe[e].from = from; pc->pe[e].to = to; pc->pe[e].stamp = 0;
    lpush(&P->pedges, e);
    lpush(&pc->pn[from].edges, e); pc->pn[from].deg++;
    lpush(&pc->pn[to].edges, e); pc->pn[to].deg++;
}
/* Ray / edge test. The ray is (R, A): pnode -1 stands for the stack node local_48 whose [0] is the reference node. */
static int pn_cn(IfkPlaneCut *pc, int pn) { return pn < 0 ? g_raynode : pc->pn[pn].cn; }
/* FUN_18003e060(ray, E): both ends of the ray certainly on the same side of E */
static int pg_e060(IfkPlaneCut *pc, int r0, int r1, int q0, int q1)
{
    int s0, s1;
    if (!cn_orient_f(pc, pn_cn(pc, r0), pn_cn(pc, q0), pn_cn(pc, q1), &s0)) return 0;
    if (!cn_orient_f(pc, pn_cn(pc, r1), pn_cn(pc, q0), pn_cn(pc, q1), &s1)) return 0;
    if (s0 != 0 && s1 != 0) return s0 == s1;
    return 0;
}
/* FUN_18003e0f0(o0, o1, ray, E, &res, &flag7): returns 1 when decided (res = "no crossing") */
static int pg_e0f0(IfkPlaneCut *pc, int o0, int o1, int r0, int r1, int q0, int q1, int *res, int *flag7)
{
    *flag7 = 0;
    if (o0 == 0) {
        if (o1 == 0) {
            int axis = 0;
            if (cn_cmp(pc, pn_cn(pc, r0), pn_cn(pc, r1), 0) == 0) {   /* 3rd argument dropped by the decompiler:
                                                                      * 0 [inferred], the second call passes 1 */
                if (cn_cmp(pc, pn_cn(pc, r0), pn_cn(pc, r1), 1) == 0) pc_error(pc, "Polygon2D error 3: redundant points.");
                axis = 1;
            }
            int cnt = 0, q[2] = { q0, q1 };
            for (int i = 0; i < 2; i++) {
                if (q[i] == r0) { cnt++; continue; }
                int s0 = cn_cmp(pc, pn_cn(pc, q[i]), pn_cn(pc, r0), axis);
                if (s0 == 0) { cnt++; continue; }
                if (q[i] == r1) { cnt++; continue; }
                int s1 = cn_cmp(pc, pn_cn(pc, q[i]), pn_cn(pc, r1), axis);
                if (s1 == 0) { cnt++; continue; }
                if (s0 != s1) { pc->pg28 = 1; *res = 0; return 1; }
            }
            if (cnt == 2) *res = 0;
            else { if (cnt != 0) pc->pg29 = 1; *res = 1; }
            return 1;
        }
        *flag7 = 1; return 0;
    }
    if (o1 != 0) {
        if (o0 == o1) { *res = 1; return 1; }
        return 0;
    }
    *flag7 = 1; return 0;
}
/* FUN_18003e2a0(q0, q1, E, flag7, o0) */
static int pg_e2a0(IfkPlaneCut *pc, int q0s, int q1s, int e0, int e1, int flag7, int o0)
{
    if (q0s == 0 || q1s == 0) { if (flag7) { pc->pg29 = 1; return 1; } return 0; }
    if (q0s == q1s) return 1;
    if (flag7) { pc->pg30 = (o0 == 0) ? e0 : e1; return 0; }
    return 0;
}
/* FUN_18003ddc0(ray, E, &deferred): 0 = counts as a crossing (subject to +0x28 / +0x30) */
static int pg_ddc0(IfkPlaneCut *pc, int r0, int r1, int E, PcList *def)
{
    int q0 = pc->pe[E].from, q1 = pc->pe[E].to, o0, o1, res, f7;
    int c0 = cn_orient_f(pc, pn_cn(pc, q0), pn_cn(pc, r0), pn_cn(pc, r1), &o0);
    int c1 = cn_orient_f(pc, pn_cn(pc, q1), pn_cn(pc, r0), pn_cn(pc, r1), &o1);
    if (!c0) {
        if (!c1) {
            if ((q0 == r0 || q0 == r1) && (q1 == r0 || q1 == r1)) return 0;
            if (pg_e060(pc, r0, r1, q0, q1)) return 1;
            lpush(def, E); return 1;                                   /* LAB_18003dec1 (FUN_180008e80) */
        }
        if (o1 != 0 && (q0 == r0 || q0 == r1)) return 1;
        if (!pg_e060(pc, r0, r1, q0, q1)) lpush(def, E);
        return 1;
    }
    if (!c1) {
        if (o0 != 0 && (q1 == r0 || q1 == r1)) return 1;
        if (!pg_e060(pc, r0, r1, q0, q1)) lpush(def, E);
        return 1;
    }
    if (pg_e0f0(pc, o0, o1, r0, r1, q0, q1, &res, &f7)) return res;
    int s0, s1;
    int d0 = cn_orient_f(pc, pn_cn(pc, r0), pn_cn(pc, q0), pn_cn(pc, q1), &s0);
    int d1 = cn_orient_f(pc, pn_cn(pc, r1), pn_cn(pc, q0), pn_cn(pc, q1), &s1);
    int pv;
    if (!d0) {
        if (!d1 || s1 == 0) { lpush(def, E); return 1; }
        pv = r0;
    } else {
        if (d1) return pg_e2a0(pc, s0, s1, q0, q1, f7, o0);
        if (s0 == 0) { lpush(def, E); return 1; }
        pv = r1;
    }
    if (pv != q0 && pv != q1) { lpush(def, E); return 1; }
    return 1;
}
static int pg_e340(IfkPlaneCut *pc, int r0, int r1, int E)   /* FUN_18003e340: the exact version */
{
    int q0 = pc->pe[E].from, q1 = pc->pe[E].to, res, f7;
    int o0 = cn_orient_x(pc, pn_cn(pc, q0), pn_cn(pc, r0), pn_cn(pc, r1));
    int o1 = cn_orient_x(pc, pn_cn(pc, q1), pn_cn(pc, r0), pn_cn(pc, r1));
    if (pg_e0f0(pc, o0, o1, r0, r1, q0, q1, &res, &f7)) return res;
    int s0 = cn_orient_x(pc, pn_cn(pc, r0), pn_cn(pc, q0), pn_cn(pc, q1));
    int s1 = cn_orient_x(pc, pn_cn(pc, r1), pn_cn(pc, q0), pn_cn(pc, q1));
    return pg_e2a0(pc, s0, s1, q0, q1, f7, o0);
}
static void pg_e430(IfkPlaneCut *pc, int r0, int r1, int *count)   /* FUN_18003e430: vertex +0x30 on the ray */
{
    int V = pc->pg30;
    PcList *L = &pc->pn[V].edges;
    for (int i = L->n - 1; i >= 0; i--) {
        const PcPEdge *E = &pc->pe[L->v[i]];
        int W = (E->from == V) ? E->to : E->from;
        if (0 < cn_orient(pc, pn_cn(pc, W), pn_cn(pc, r0), pn_cn(pc, r1))) (*count)++;
    }
}
static void pg_count_one(IfkPlaneCut *pc, int r, int r0, int r1, int *count, int *vstamp)
{
    if (r) return;
    if (pc->pg30 < 0) { if (!pc->pg28) (*count)++; return; }
    if (vstamp[pc->pg30] != pc->stamp5f0) { vstamp[pc->pg30] = pc->stamp5f0; pg_e430(pc, r0, r1, count); }
}
/* FUN_18003d6e0(polygon, ray): crossings of the ray (r0 = the reference node, r1 = a polygon node). The root cell
 * holds every edge (<= 100 edges: FUN_18003cec0 does not subdivide); the quadtree query FUN_18003eaa0 for larger
 * polygons is replaced by all edges [inferred: it only proposes candidates; each edge is tested once by stamp]. */
static int pg_crossings(IfkPlaneCut *pc, PcPoly *P, int r0, int r1)
{
    int count = 0;
    PcList def = { 0, 0, NULL };
    pc->stamp5c0 += 2; pc->stamp5f0 += 2;
    int *vstamp = calloc((size_t)pc->npn + 1, sizeof(int));
    for (int k = 0; k < pc->npn; k++) vstamp[k] = -1;
    for (int i = 0; i < P->pedges.n; i++) {          /* root cell list: push-front over the head-first edge walk */
        int E = P->pedges.v[i];
        if (pc->pe[E].stamp == pc->stamp5c0) continue;
        pc->pe[E].stamp = pc->stamp5c0;
        pc->pg28 = 0; pc->pg30 = -1;
        int r = pg_ddc0(pc, r0, r1, E, &def);
        pg_count_one(pc, r, r0, r1, &count, vstamp);
    }
    for (int i = def.n - 1; i >= 0; i--) {           /* deferred list from its head */
        pc->pg28 = 0; pc->pg30 = -1;
        int r = pg_e340(pc, r0, r1, def.v[i]);
        pg_count_one(pc, r, r0, r1, &count, vstamp);
    }
    free(vstamp); lfree(&def);
    return count;
}
/* FUN_18003dba0(A, R, &flag): A has two edges; R must not lie on either edge's line */
static int pg_dba0(IfkPlaneCut *pc, int A, int *flag)
{
    PcList *L = &pc->pn[A].edges;
    const PcPEdge *e1 = &pc->pe[L->v[L->n - 1]], *e2 = &pc->pe[L->v[L->n - 2]];
    int B1 = (e1->from == A) ? e1->to : e1->from;
    int s = cn_orient(pc, pn_cn(pc, B1), pn_cn(pc, e2->from), pn_cn(pc, e2->to));
    int o1, o2, c1, c2;
    c1 = cn_orient_f(pc, g_raynode, pn_cn(pc, e1->from), pn_cn(pc, e1->to), &o1);
    if (s < 0) {
        if (c1 && 1 <= o1) { *flag = 0; return 1; }
        c2 = cn_orient_f(pc, g_raynode, pn_cn(pc, e2->from), pn_cn(pc, e2->to), &o2);
        if (c2 && 1 <= o2) { *flag = 0; return 1; }
        if (!c1) { o1 = cn_orient_x(pc, g_raynode, pn_cn(pc, e1->from), pn_cn(pc, e1->to)); if (0 < o1) { *flag = 0; return 1; } }
        if (!c2) { o2 = cn_orient_x(pc, g_raynode, pn_cn(pc, e2->from), pn_cn(pc, e2->to)); if (0 < o2) { *flag = 0; return 1; } }
        if (o1 == 0 || o2 == 0) return 0;
        *flag = 1; return 1;
    }
    if (c1 && o1 < 0) { *flag = 1; return 1; }
    c2 = cn_orient_f(pc, g_raynode, pn_cn(pc, e2->from), pn_cn(pc, e2->to), &o2);
    if (c2 && o2 < 0) { *flag = 1; return 1; }
    if (!c1) { o1 = cn_orient_x(pc, g_raynode, pn_cn(pc, e1->from), pn_cn(pc, e1->to)); if (o1 < 0) { *flag = 1; return 1; } }
    if (!c2) { o2 = cn_orient_x(pc, g_raynode, pn_cn(pc, e2->from), pn_cn(pc, e2->to)); if (o2 < 0) { *flag = 1; return 1; } }
    if (o1 == 0 || o2 == 0) return 0;
    *flag = 0; return 1;
}
/* FUN_18003d480(polygon, node h): is the node inside? (h = -1: the reference node) */
static int pg_inside(IfkPlaneCut *pc, int pi, int h)
{
    PcPoly *P = &pc->poly[pi];
    double x, y; cn_xy(pc, h, &x, &y);           /* slot 0 */
    if (x <= P->box[0] && PG_TOL < P->box[0] - x) return P->byte38;
    if (P->box[2] <= x && PG_TOL < x - P->box[2]) return P->byte38;
    if (y <= P->box[1] && PG_TOL < P->box[1] - y) return P->byte38;
    if (P->box[3] <= y && PG_TOL < y - P->box[3]) return P->byte38;
    int save = g_raynode; g_raynode = h;
    int A = -1, flag = 0;
    for (int i = P->pedges.n - 1; i >= 0 && A < 0; i--) {   /* edges from the head: their [0] node of degree 2 */
        int f = pc->pe[P->pedges.v[i]].from;
        if (pc->pn[f].deg == 2 && pg_dba0(pc, f, &flag)) A = f;
    }
    int r = 0;
    if (A < 0) pc_error(pc, "Polygon2D error 2: inside test failed.");
    else {
        int c = pg_crossings(pc, P, -1, A) & 1;      /* FUN_18003d6e0 with the edge (local_48 -> A) */
        r = flag ? (c == 0) : (c == 1);
    }
    g_raynode = save;
    return r;
}

/* ---- FUN_180015b30 (assembly), FUN_18003cec0 (close), FUN_180015d20 (nesting), FUN_1800152c0 (output) ----------- */
static int pg_close(IfkPlaneCut *pc, int pi)   /* FUN_18003cec0(polygon, _, reference node, 1) */
{
    PcPoly *P = &pc->poly[pi];
    P->byte38 = 0;
    P->box[0] = P->box[0] - PG_TOL * 2.0;
    P->box[1] = P->box[1] - PG_TOL * 2.0;
    int big = 0x65 <= P->pedges.n;            /* > 100 edges: Polygon2DQuadtree (FUN_18003cc70 / FUN_18002fee0) */
    for (int i = P->pedges.n - 1; i >= 0; i--) {
        const PcPEdge *E = &pc->pe[P->pedges.v[i]];
        if (pc->pn[E->from].deg < 2 || pc->pn[E->to].deg < 2)
            pc_error(pc, big ? "Polygon2D error 4: open polygon." : "Polygon2D error 5: open polygon.");
    }
    /* reference node: slot 4 FUN_180012d90 = (min x, min y) of the widened box; its point (x, 0, y, 0) */
    g_refxy[0] = P->box[0]; g_refxy[1] = P->box[1];
    double *c = pc->pt[g_refpt].c;
    c[0] = P->box[0]; c[1] = 0.0; c[2] = P->box[1]; c[3] = 0.0;
    if (pg_inside(pc, pi, -1)) {
        P = &pc->poly[pi];
        for (int i = 0; i < P->pedges.n; i++) {
            PcPEdge *E = &pc->pe[P->pedges.v[i]];
            int t = E->from; E->from = E->to; E->to = t;
        }
        return 1;
    }
    return 0;
}
static int pg_assemble(IfkPlaneCut *pc, int e0)   /* FUN_180015b30(tree, curve edge, reference node, counter, 1) */
{
    GROW(pc->poly, pc->npoly, pc->cpoly);
    int pi = pc->npoly++;
    PcPoly *P = &pc->poly[pi];
    memset(P, 0, sizeof *P);
    P->box[0] = P->box[1] = DBL_MAX; P->box[2] = P->box[3] = -DBL_MAX;   /* FUN_18003e810 */
    P->parent = -1; P->tnode = -1; P->ref = -1;
    Fifo w = { 0 };
    fpush(&w, e0);
    pc->cedg[e0].vis = 1;
    while (!fempty(&w)) {
        int cur = fpop(&w);
        P = &pc->poly[pi];
        lpush(&P->cedges, cur);                                            /* plVar3[1] push-front */
        PcCEdge E = pc->cedg[cur];
        if (!pc->cn[E.n0].cache) pc->cn[E.n0].cache = pn_new(pc, P, E.n0) + 1;
        if (!pc->cn[E.n1].cache) pc->cn[E.n1].cache = pn_new(pc, P, E.n1) + 1;
        int from, to, tonode;
        if (E.vis == 2) { from = pc->cn[E.n1].cache - 1; to = pc->cn[E.n0].cache - 1; tonode = E.n0; }
        else { from = pc->cn[E.n0].cache - 1; to = pc->cn[E.n1].cache - 1; tonode = E.n1; }
        pe_new(pc, P, from, to);
        PcList *L = &pc->pt[pc->cn[tonode].pt].gel;                        /* curve edges at the node's point */
        for (int i = L->n - 1; i >= 0; i--) {
            PcCEdge *f = &pc->cedg[L->v[i]];
            if (f->vis) continue;
            if (!f->both) { if (f->n0 != tonode) continue; f->vis = 1; }
            else f->vis = (unsigned char)((f->n0 != tonode) + 1);
            fpush(&w, L->v[i]);
        }
    }
    free(w.v);
    int rev = pg_close(pc, pi);
    P = &pc->poly[pi];
    P->rev = rev;
    P->ref = pc->pn[pc->pe[P->pedges.v[P->pedges.n - 1]].from].cn;        /* [6] = head edge's [0] node */
    return pi;
}
/* FUN_180015f10: the polygons of list L contained in P move under P */
static int nest_f10(IfkPlaneCut *pc, int P, PcList *L)
{
    int moved = 0;
    for (int i = L->n - 1; i >= 0; i--) {
        int Q = L->v[i];
        if (pg_inside(pc, P, pc->poly[Q].ref)) {
            lpop_at(L, i);
            lpush(&pc->poly[P].children, Q);
            pc->poly[Q].parent = P;
            moved = 1;
        }
    }
    return moved;
}
static void pg_nest(IfkPlaneCut *pc, PcList *roots, PcList *leaves, int P)   /* FUN_180015d20 */
{
    pc->poly[P].parent = -1;
    int first = 1;
    int c = nest_f10(pc, P, roots);
    int scan = -1;                                    /* -1: the root list, else that polygon's children */
    while (!c) {
        int cand = -1;
        if (first) {
            first = 0;
            for (int i = leaves->n - 1; i >= 0; i--)
                if (pg_inside(pc, leaves->v[i], pc->poly[P].ref)) { cand = leaves->v[i]; break; }
        }
        if (cand < 0) {
            PcList *L = scan < 0 ? roots : &pc->poly[scan].children;
            if (L->n == 0) break;
            for (int i = L->n - 1; i >= 0; i--)
                if (pg_inside(pc, L->v[i], pc->poly[P].ref)) {
                    if (cand >= 0) pc_error(pc, "PlaneCut error 47: multiple parents.");
                    cand = L->v[i];
                }
            if (cand < 0) break;
        }
        pc->poly[P].parent = cand;
        scan = cand;
        c = nest_f10(pc, P, &pc->poly[cand].children);
    }
    int par = pc->poly[P].parent;
    if (par < 0) lpush(roots, P);
    else {
        if (pc->poly[par].open) { lremove_head(leaves, par); pc->poly[par].open = 0; }
        lpush(&pc->poly[par].children, P);
    }
    if (pc->poly[P].children.n == 0) { lpush(leaves, P); pc->poly[P].open = 1; }
}
static int tree_node(IfkPcTree *t)   /* slot 1 FUN_180024ab0 */
{
    t->parent = realloc(t->parent, sizeof(int) * (size_t)(t->nnode + 1));
    t->parent[t->nnode] = -2;                         /* -2: not linked yet ([0] = 0) */
    return t->nnode++;
}
static void pc_output(IfkPlaneCut *pc, IfkPcTree *t)   /* FUN_1800152c0 after the assembly loop */
{
    /* slot 0 (+0x00): no-op in PlaneCutToSilhouette */
    int *order = NULL, norder = 0;                    /* children lists (push-front) kept as link order */
    for (int pi = 0; pi < pc->npoly; pi++) {
        PcPoly *P = &pc->poly[pi];
        if (P->tnode < 0) P->tnode = tree_node(t);
        if (P->parent >= 0) {
            PcPoly *Q = &pc->poly[P->parent];
            if (Q->tnode < 0) Q->tnode = tree_node(t);
            t->parent[P->tnode] = Q->tnode;           /* slot 2 FUN_180024af0(parent, child) */
            order = realloc(order, sizeof(int) * (size_t)(norder + 1)); order[norder++] = P->tnode;
        }
        for (int i = 0; i < P->pedges.n; i++) {
            pc->cn[pc->pn[pc->pe[P->pedges.v[i]].from].cn].cache = 0;
            pc->cn[pc->pn[pc->pe[P->pedges.v[i]].to].cn].cache = 0;
        }
    }
    for (int pi = 0; pi < pc->npoly; pi++) {
        PcPoly *P = &pc->poly[pi];
        for (int i = P->cedges.n - 1; i >= 0; i--) {  /* popped from the head */
            const PcCEdge *E = &pc->cedg[P->cedges.v[i]];
            int ns[2] = { E->n0, E->n1 };
            for (int s = 0; s < 2; s++) {
                PcCNode *N = &pc->cn[ns[s]];
                if (N->cache == 0) {                  /* slot 3 FUN_180024b50(+8, +0x10, +0x18) */
                    t->pt = realloc(t->pt, sizeof(double) * 3 * (size_t)(t->npt + 1));
                    memcpy(t->pt + 3 * t->npt, N->x, sizeof N->x);
                    N->cache = ++t->npt;
                }
            }
            t->edge = realloc(t->edge, sizeof(int) * 4 * (size_t)(t->nedge + 1));   /* slot 4 FUN_180024bc0 */
            int *q = t->edge + 4 * t->nedge++;
            q[0] = pc->cn[E->n0].cache - 1; q[1] = pc->cn[E->n1].cache - 1; q[2] = E->both;
            q[3] = P->tnode;                          /* 5th argument (dropped by the decompiler) [inferred] */
        }
    }
    /* slot 5 FUN_180024c40: nodes without parent go under the root (+0x18) */
    t->nchild = calloc((size_t)t->nnode + 1, sizeof(int));
    t->child = calloc((size_t)t->nnode + 1, sizeof(int *));
    for (int i = 0; i < t->nnode; i++) if (t->parent[i] == -2) t->parent[i] = -1;
    /* children in the DLL's list order: slot 2 pushes front (newest link first); slot 5 walks the node list
     * (newest node first) and pushes each orphan front, so the root's list is oldest orphan first */
    for (int k = norder - 1; k >= 0; k--) {
        int c = order[k], p = t->parent[c];
        t->child[p] = realloc(t->child[p], sizeof(int) * (size_t)(t->nchild[p] + 1));
        t->child[p][t->nchild[p]++] = c;
    }
    int *rootc = NULL, nr = 0;
    for (int i = 0; i < t->nnode; i++) {
        int linked = 0;
        for (int k = 0; k < norder; k++) if (order[k] == i) { linked = 1; break; }
        if (!linked) { rootc = realloc(rootc, sizeof(int) * (size_t)(nr + 1)); rootc[nr++] = i; }
    }
    t->nroot = nr; t->root = rootc;
    free(order);
}
void ifk_pc_tree_free(IfkPcTree *t)
{
    if (!t) return;
    if (t->child) for (int i = 0; i < t->nnode; i++) free(t->child[i]);
    free(t->child); free(t->nchild); free(t->parent); free(t->pt); free(t->edge); free(t->root);
    memset(t, 0, sizeof *t);
}

/* ---- Run FUN_180025910 with stage B --------------------------------------------------------------------------- */
static int pc_run_impl(IfkPlaneCut *pc, IfkPcPlaneCb cb, void *user, int stageb);
int ifk_pc_run(IfkPlaneCut *pc, int mode, const IfkPcIO *io, IfkPcTree *out)
{
    memset(out, 0, sizeof *out);
    if (pc->nplane == 0) return 0;                   /* impl+0x84 == 0 */
    if (!pc->aligned || pc->vertical || pc->flipped || pc->nplane != 1 || mode != 1) return -1;   /* not ported */
    pc->mode = mode;                                 /* impl+0xfc */
    int r = pc_run_impl(pc, NULL, NULL, 1);
    if (r) return r;
    /* "Cleaning up...": slot 7, a test point per curve of impl+0xb0 (in order) + slot 8, slot 9, slot 10 */
    if (io && io->begin) io->begin(io->user, pc);
    for (int i = 0; i < pc->curves.n; i++) {
        int t = tp_new(pc, pc->curves.v[i]);
        if (io && io->add) io->add(io->user, pc, t);
    }
    if (io && io->build) io->build(io->user, pc);
    if (io && io->classify) io->classify(io->user, pc);
    /* mode 1: FUN_180014f70 tree; per test point: passed -> FUN_1800151f0, else FUN_18002a2c0 (curve freed) */
    PcList tcurves = { 0, 0, NULL };
    for (int t = 0; t < pc->ntp; t++) {
        if (!pc->tp[t].passed) continue;
        int C = pc->tp[t].curve;
        lpush(&tcurves, C);
        PcList *E = &pc->cv[C].edges;
        for (int k = E->n - 1; k >= 0; k--) {        /* the curve's edges from the head, onto their nodes' points */
            const PcCEdge *ce = &pc->cedg[E->v[k]];
            lpush(&pc->pt[pc->cn[ce->n0].pt].gel, E->v[k]);
            lpush(&pc->pt[pc->cn[ce->n1].pt].gel, E->v[k]);
        }
    }
    /* FUN_1800152c0: reference node (point from FUN_18000a1b0), "Postprocessing (phase 2)..." */
    g_refpt = pt_alloc(pc);
    PcList roots = { 0, 0, NULL }, leaves = { 0, 0, NULL };
    for (int i = 0; i < tcurves.n; i++) {
        PcList *E = &pc->cv[tcurves.v[i]].edges;
        for (int k = E->n - 1; k >= 0; k--) {
            int e = E->v[k];
            if (pc->cedg[e].both || pc->cedg[e].vis) continue;
            int P = pg_assemble(pc, e);
            pg_nest(pc, &roots, &leaves, P);
            pc->pt[g_refpt].ex = -1;                 /* the reference point's exact record is freed */
        }
    }
    pc_output(pc, out);
    lfree(&tcurves); lfree(&roots); lfree(&leaves);
    if (pc->overflow) pc_error(pc, "PlaneCut error 22: overflow.");
    return 0;
}
int ifk_pc_run_stage_a(IfkPlaneCut *pc, IfkPcPlaneCb cb, void *user) { return pc_run_impl(pc, cb, user, 0); }
