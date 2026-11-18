/* ifk_silhouette_in.c - input stage of the Silhouette Boundary kernel, port of interfacial11.dll:
 *   Silhouette::Run 18001f1c0 (grid, angle conversion l.24194-24220 of i11_silhouette_all.c),
 *   SurferToSilhouette slots: begin FUN_180023600, AddNode FUN_180023710, AddTriangle FUN_180023920,
 *   end FUN_180023f80 (silhouette edges -> vertical wall triangles handed to PlaneCut).
 * Arithmetic helpers: FUN_18001e350 (grid), FUN_18002bad0 (filtered cross product), FUN_180020b10 / FUN_18002ca90 /
 * FUN_18002bc50 / FUN_180002ab0 / FUN_180010340 / FUN_180010900 (exact sign-magnitude cross product),
 * FUN_180026d90 (double -> SuperInteger<1>), FUN_180026d10 (SuperInteger<2> -> double).
 *
 * Data structures are ours (arrays + indices instead of MemoryPool records and binary trees).
 * ORDER CONVENTION: the original keys the edge tree (SurferToSilhouette +8, FUN_180023920 / FUN_180026fc0) by the
 * node record POINTERS (smaller pointer first in an edge, edges visited smallest pair first by FUN_180023f80).
 * We emulate those addresses (naddr): the MemoryPool<InputNode> (blocks of 1000, Run l.26299) is filled by pushing
 * every record of a new block on the free-list head (FUN_180027740), so a block hands out DECREASING addresses, and
 * AddNode returns a duplicate's record to the free list at once (FUN_180023710), so unique node i (AddNode order) is
 * record 999 - i % 1000 of block i / 1000. Block order is the heap's (assumed increasing, NOT DECODED). Checked
 * against the DLL with tools/peloader --tpif (formats/interfacial.md 13).
 * NOT DECODED here: the order in which Surfer::ReadFile (interfacial01) calls AddNode / AddTriangle; we call AddNode
 * for every input node in array order, then AddTriangle for every triangle in array order. */
#include "ifk_silhouette_in.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GRID 9.5367431640625e-07        /* DAT_18021c028 = 2^-20, passed to FUN_18001e350 by Run (disasm 18001f500) */
#define ULP52 2.220446049250313e-16     /* 2^-52, filter in FUN_180023920 */
#define PI_ 3.141592653589793           /* Run l.24210 and FUN_180023920 (0x400921fb54442d18) */

static int g_overflow;                  /* TLS DAT_180268398 */
int ifk_sil_exact_calls;                /* diagnostics: number of exact-path cross products (not in the original) */

/* ------------------------------------------------------------------ SuperInteger (sign byte: 1 = positive) */
typedef struct { unsigned char pos; uint32_t m; } SI1;          /* SuperInteger<1> */
typedef struct { unsigned char pos; uint32_t l[2]; } SI2;       /* SuperInteger<2>, limbs LSB first */

static SI1 si1_from_double(double v)    /* FUN_180026d90 */
{
    SI1 r; double q = v / GRID;
    if (v <= 0.0) {
        r.pos = 0;
        if (q <= -4294967296.0) g_overflow = 1;
        q = -q;
    } else {
        r.pos = 1;
        if (4294967296.0 <= q) g_overflow = 1;
    }
    r.m = (uint32_t)(int64_t)q;         /* (int)(longlong)dVar1 */
    return r;
}

/* a + (b with sign bpos), 1 limb: FUN_18002ca90 (called with bpos = (b.sign == 0), i.e. a - b) */
static SI1 si1_add(SI1 a, unsigned char bpos, uint32_t bm)
{
    SI1 r;
    if (a.pos == bpos) {
        r.m = a.m + bm;
        if (~a.m < bm) g_overflow = 1;
        r.pos = a.pos;
    } else if (a.m == bm) {
        r.m = 0; r.pos = 1;             /* equal magnitudes: zero, sign byte 1 */
    } else if (bm < a.m) {
        r.m = a.m - bm; r.pos = a.pos;  /* FUN_180010900 with 1 limb */
    } else {
        r.m = bm - a.m; r.pos = bpos;
    }
    return r;
}

static SI2 si_mul(SI1 a, SI1 b)         /* FUN_180002ab0 with 1 limb (exact 32x32 -> 64) + sign (a.pos == b.pos) */
{
    SI2 r; uint64_t p = (uint64_t)a.m * (uint64_t)b.m;
    r.l[0] = (uint32_t)p; r.l[1] = (uint32_t)(p >> 32);
    r.pos = (unsigned char)(a.pos == b.pos);
    return r;
}

static void mag_sub2(uint32_t *d, const uint32_t *a, const uint32_t *b, int top)   /* FUN_180010900 */
{
    int borrow = a[0] < b[0];
    d[0] = a[0] - b[0];
    for (int i = 1; i <= top; i++) {
        if (borrow) { borrow = (a[i] == 0) || (a[i] - 1 < b[i]); d[i] = a[i] - b[i] - 1; }
        else { borrow = a[i] < b[i]; d[i] = a[i] - b[i]; }
    }
}

/* a + (b with sign bpos), 2 limbs: FUN_180010340 */
static SI2 si2_add(SI2 a, unsigned char bpos, const uint32_t *bl)
{
    SI2 r; memset(&r, 0, sizeof r);
    if (a.pos == bpos) {
        int ov;
        r.l[0] = a.l[0] + bl[0];
        if (~a.l[0] < bl[0]) {
            uint32_t t = ~a.l[1];
            ov = (t == 0) || (t - 1 < bl[1]);
            r.l[1] = a.l[1] + 1 + bl[1];
        } else {
            ov = ~a.l[1] < bl[1];
            r.l[1] = bl[1] + a.l[1];
        }
        if (ov) g_overflow = 1;
        r.pos = a.pos;
    } else {
        int i = 1;
        while (i >= 0 && a.l[i] == bl[i]) { r.l[i] = 0; i--; }
        if (i < 0) r.pos = 1;
        else if (bl[i] < a.l[i]) { mag_sub2(r.l, a.l, bl, i); r.pos = a.pos; }
        else { mag_sub2(r.l, bl, a.l, i); r.pos = bpos; }
    }
    return r;
}

static double si2_to_double(const SI2 *s, double scale)   /* FUN_180026d10 */
{
    double lo = (double)s->l[0], hi = (double)s->l[1];
    double d = lo * scale + 0.0 + hi * scale * 4294967296.0;
    return s->pos ? d : -d;
}

/* exact (p2 - p1) x (p0 - p1) on the node SuperIntegers: FUN_180020b10 (differences FUN_18002ca90,
 * cross FUN_18002bc50: x = A.y B.z - B.y A.z, y = A.z B.x - B.z A.x, z = A.x B.y - B.x A.y) */
static void exact_cross(SI2 out[3], const SI1 *p0, const SI1 *p1, const SI1 *p2)
{
    SI1 A[3], B[3];
    for (int k = 0; k < 3; k++) {
        A[k] = si1_add(p2[k], (unsigned char)(p1[k].pos == 0), p1[k].m);
        B[k] = si1_add(p0[k], (unsigned char)(p1[k].pos == 0), p1[k].m);
    }
    for (int k = 0; k < 3; k++) {
        int i = (k + 1) % 3, j = (k + 2) % 3;   /* x: (y,z), y: (z,x), z: (x,y) */
        SI2 P = si_mul(A[i], B[j]);
        SI2 Q = si_mul(B[i], A[j]);
        out[k] = si2_add(P, (unsigned char)(Q.pos == 0), Q.l);
    }
}

/* ------------------------------------------------------------------ node dedup (AddNode tree, exact compare) */
typedef struct { double c[3]; SI1 s[3]; } Node;   /* record +0x00 xyz, +0x18 SuperInteger<1> x3 */

typedef struct { int *slot; int cap; } NodeHash;

static uint64_t hbits(double v) { uint64_t u; v += 0.0; memcpy(&u, &v, 8); return u; }  /* -0 == +0 as in the compare */
static uint64_t nhash(const double *c)
{
    uint64_t h = 1469598103934665603ULL;
    for (int k = 0; k < 3; k++) { h ^= hbits(c[k]); h *= 1099511628211ULL; h ^= h >> 29; }
    return h;
}

/* ------------------------------------------------------------------ edges */
typedef struct { int a, b, k; } EdgeRec;            /* key (a, b) a < b; k = kept index */
static int naddr(int i) { return (i / 1000) * 1000 + 999 - i % 1000; }   /* emulated node record address */
#define NLESS(p, q) (naddr(p) < naddr(q))
static int edge_cmp(const void *pa, const void *pb)
{
    const EdgeRec *x = pa, *y = pb;
    if (x->a != y->a) return NLESS(x->a, y->a) ? -1 : 1;
    if (x->b != y->b) return NLESS(x->b, y->b) ? -1 : 1;
    return x->k > y->k ? -1 : (x->k < y->k);        /* newest first = the push-front triangle list of the edge */
}

static int idx_in(const int *t, int v) { return t[0] == v ? 0 : (t[1] == v ? 1 : 2); }
/* does triangle t traverse a -> b ? (FUN_180023f80: t[(index(a) + 1) % 3] == b) */
static int goes(const int *t, int a, int b) { return t[(idx_in(t, a) + 1) % 3] == b; }

void ifk_silhouette_walls_free(IfkSilWalls *w)
{
    free(w->node); free(w->node_of_input); free(w->kept_tri); free(w->kept_src); free(w->kept_angle);
    free(w->sil_edge); free(w->wnode); free(w->wtri); free(w->wtri_id);
    memset(w, 0, sizeof *w);
}

int ifk_silhouette_walls(const double *node, int nnode_in, const int *tri, int ntri,
                         double minang_deg, double maxang_deg, int mode, IfkSilWalls *out)
{
    /* Run l.24210 / l.24220: angles to radians */
    double amin = (minang_deg * PI_) / 180.0, amax = (maxang_deg * PI_) / 180.0;
    Node *nodes = NULL; NodeHash hs = { NULL, 0 };
    int nn = 0, nkeep = 0;
    int (*ktri)[3] = NULL; int *ksrc = NULL; double *kang = NULL;
    EdgeRec *er = NULL;
    int *wnode_of = NULL;
    int rc = -1;

    memset(out, 0, sizeof *out);
    g_overflow = 0;

    /* begin FUN_180023600: SuperIntegers 0 (+0x10) and 1.0 (+0x18), box = +-DBL_MAX */
    SI1 si_zero = si1_from_double(0.0), si_one = si1_from_double(1.0);
    out->box[0] = DBL_MAX; out->box[1] = DBL_MAX; out->box[2] = -DBL_MAX; out->box[3] = -DBL_MAX;

    nodes = malloc((size_t)(nnode_in > 0 ? nnode_in : 1) * sizeof *nodes);
    out->node_of_input = malloc((size_t)(nnode_in > 0 ? nnode_in : 1) * sizeof(int));
    hs.cap = 16; while (hs.cap < 2 * nnode_in) hs.cap *= 2;
    hs.slot = malloc((size_t)hs.cap * sizeof(int));
    if (!nodes || !out->node_of_input || !hs.slot) goto done;
    for (int i = 0; i < hs.cap; i++) hs.slot[i] = -1;

    /* AddNode FUN_180023710: each coordinate snapped DOWN: floor(v / g) * g (FUN_18019e9e4 = floor); dedup on exact
     * lexicographic (x, y, z) equality; the record gets the SuperInteger copies (FUN_180026d90) */
    for (int i = 0; i < nnode_in; i++) {
        double c[3];
        for (int k = 0; k < 3; k++) c[k] = floor(node[3 * i + k] / GRID) * GRID;
        uint64_t h = nhash(c) & (uint64_t)(hs.cap - 1);
        int found = -1;
        while (hs.slot[h] >= 0) {
            const double *o = nodes[hs.slot[h]].c;
            if (o[0] == c[0] && o[1] == c[1] && o[2] == c[2]) { found = hs.slot[h]; break; }
            h = (h + 1) & (uint64_t)(hs.cap - 1);
        }
        if (found < 0) {
            found = nn++;
            hs.slot[h] = found;
            memcpy(nodes[found].c, c, sizeof c);
            for (int k = 0; k < 3; k++) nodes[found].s[k] = si1_from_double(c[k]);
        }
        out->node_of_input[i] = found;
    }

    /* AddTriangle FUN_180023920 */
    ktri = malloc((size_t)(ntri > 0 ? ntri : 1) * sizeof *ktri);
    ksrc = malloc((size_t)(ntri > 0 ? ntri : 1) * sizeof(int));
    kang = malloc((size_t)(ntri > 0 ? ntri : 1) * sizeof(double));
    if (!ktri || !ksrc || !kang) goto done;
    for (int t = 0; t < ntri; t++) {
        int v[3];
        for (int k = 0; k < 3; k++) v[k] = out->node_of_input[tri[3 * t + k]];
        const double *p0 = nodes[v[0]].c, *p1 = nodes[v[1]].c, *p2 = nodes[v[2]].c;
        double a[3] = { p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2] };   /* local_b0.. : p2 - p1 */
        double b[3] = { p0[0] - p1[0], p0[1] - p1[1], p0[2] - p1[2] };   /* local_f8.. : p0 - p1 */
        /* FUN_18002bad0(out, a, b): value and error bound per component */
        double d5 = b[2] * a[1], d7 = b[0] * a[2], d2 = a[0] * b[1], d1 = b[1] * a[2];
        double d8 = a[0] * b[2], d3 = b[0] * a[1];
        double nx = d5 - d1, ny = d7 - d8, nz = d2 - d3;
        double ez = fabs(d3) + fabs(d2) + fabs(nz);
        if (!(ez * ULP52 < fabs(nz) || ez == 0.0)) {
            /* uncertain sign of z: exact cross on the SuperInteger copies (FUN_180020b10), back to double with
             * scale g * g (FUN_180026d10, xmm2 = g * g at disasm 180023a86..180023ab5) */
            SI2 e[3];
            ifk_sil_exact_calls++;
            exact_cross(e, nodes[v[0]].s, nodes[v[1]].s, nodes[v[2]].s);
            nx = si2_to_double(&e[0], GRID * GRID);
            ny = si2_to_double(&e[1], GRID * GRID);
            nz = si2_to_double(&e[2], GRID * GRID);
        }
        if (nz == 0.0) { out->ndrop_vertical++; continue; }           /* vertical facet: record freed */
        if (nz <= 0.0 && nz != 0.0) {                                    /* downward: swap p0 / p2, negate n */
            int s = v[0]; v[0] = v[2]; v[2] = s;
            nx = nx * -1.0; ny = ny * -1.0; nz = nz * -1.0;
        }
        double len = sqrt(ny * ny + nx * nx + nz * nz);                 /* FUN_18019f368, operand order as there */
        double ang = 0.0, c;
        if (len == 0.0) ang = acos(0.0);                                 /* FUN_1801b1890 */
        else {
            c = nz / len;
            if (-1.0 <= c) { if (c <= 1.0) ang = acos(c); /* else stays 0.0 */ }
            else ang = PI_;
        }
        int keep;
        if (mode == 0) keep = (ang < amin) || (amax < ang);              /* +0x70 == 0 */
        else keep = (amin <= ang) && (ang <= amax);
        if (!keep) { out->ndrop_angle++; continue; }
        /* kept: pushed on +0xa0, xy box over the 3 nodes, edges into the tree (below) */
        for (int k = 0; k < 3; k++) {
            double x = nodes[v[k]].c[0], y = nodes[v[k]].c[1];
            if (x < out->box[0]) out->box[0] = x;
            if (out->box[2] < x) out->box[2] = x;
            if (y < out->box[1]) out->box[1] = y;
            if (out->box[3] < y) out->box[3] = y;
        }
        memcpy(ktri[nkeep], v, sizeof v);
        ksrc[nkeep] = t; kang[nkeep] = ang;
        nkeep++;
    }

    /* +0xa0 list is push-front: newest kept first */
    out->nkept = nkeep;
    out->kept_tri = malloc((size_t)(nkeep > 0 ? nkeep : 1) * 3 * sizeof(int));
    out->kept_src = malloc((size_t)(nkeep > 0 ? nkeep : 1) * sizeof(int));
    out->kept_angle = malloc((size_t)(nkeep > 0 ? nkeep : 1) * sizeof(double));
    if (!out->kept_tri || !out->kept_src || !out->kept_angle) goto done;
    for (int i = 0; i < nkeep; i++) {
        int k = nkeep - 1 - i;
        memcpy(out->kept_tri + 3 * i, ktri[k], 3 * sizeof(int));
        out->kept_src[i] = ksrc[k]; out->kept_angle[i] = kang[k];
    }

    /* edge tree (FUN_180023920 loop: edges (t[i], t[(i+1)%3]), smaller node first, triangle pushed on the edge's
     * list); sorting = in-order traversal of the tree as FUN_180023f80 consumes it (leftmost first) */
    er = malloc((size_t)(nkeep > 0 ? nkeep : 1) * 3 * sizeof *er);
    if (!er) goto done;
    for (int k = 0; k < nkeep; k++)
        for (int i = 0; i < 3; i++) {
            int p = ktri[k][i], q = ktri[k][(i + 1) % 3];
            er[3 * k + i].a = NLESS(p, q) ? p : q; er[3 * k + i].b = NLESS(p, q) ? q : p; er[3 * k + i].k = k;
        }
    qsort(er, (size_t)nkeep * 3, sizeof *er, edge_cmp);

    /* end FUN_180023f80 */
    if (g_overflow) out->overflow = 1;      /* message "IFSilhouette error 2: overflow." via vtable +0x40 */
    /* NewCutPlaneSet((0,0,1)), AddCutPlane(0.0), EndCutPlaneSet, PlaneCut::NewInput: not modelled here */
    out->sil_edge = malloc((size_t)(nkeep > 0 ? nkeep : 1) * 3 * 2 * sizeof(int));
    out->wnode = malloc((size_t)(nkeep > 0 ? nkeep : 1) * 3 * 3 * 3 * sizeof(double));
    out->wtri = malloc((size_t)(nkeep > 0 ? nkeep : 1) * 3 * 3 * sizeof(int));
    out->wtri_id = malloc((size_t)(nkeep > 0 ? nkeep : 1) * 3 * sizeof(int));
    wnode_of = malloc((size_t)(nn > 0 ? nn : 1) * sizeof(int));       /* node record +0x30 (PlaneCut node) */
    if (!out->sil_edge || !out->wnode || !out->wtri || !out->wtri_id || !wnode_of) goto done;
    for (int i = 0; i < nn; i++) wnode_of[i] = -1;
    int id = 1;
    for (int g0 = 0; g0 < nkeep * 3;) {
        int g1 = g0 + 1;
        while (g1 < nkeep * 3 && er[g1].a == er[g0].a && er[g1].b == er[g0].b) g1++;
        int a = er[g0].a, b = er[g0].b, sil = 0;
        if (g1 - g0 == 1) sil = 1;                                        /* one triangle */
        else {
            /* some pair traverses a -> b the same way (fold seen from above) */
            for (int i = g0; i < g1 && !sil; i++)
                for (int j = i + 1; j < g1 && !sil; j++)
                    if (goes(ktri[er[i].k], a, b) == goes(ktri[er[j].k], a, b)) sil = 1;
        }
        if (sil) {
            const int *t = ktri[er[g0].k];                                /* head of the edge's list */
            int s = a, e = b;
            if (goes(t, a, b)) { s = a; e = b; } else { s = b; e = a; }  /* pdVar20 = start, pdVar17 = end */
            const double *ps = nodes[s].c, *pe = nodes[e].c;
            double mx = (pe[0] + ps[0]) * 0.5, my = (ps[1] + pe[1]) * 0.5, mz = 1.0;
            double fx = floor(mx / GRID);
            mx = fx * GRID;
            my = floor(my / GRID) * GRID;
            mz = floor(mz / GRID) * GRID;
            (void)si1_from_double(fx * GRID); (void)si1_from_double(my);  /* mid SuperIntegers (overflow check) */
            (void)si_one;                                                 /* mid z SuperInteger = +0x18 */
            int mid = out->nwnode++;
            out->wnode[3 * mid] = mx; out->wnode[3 * mid + 1] = my; out->wnode[3 * mid + 2] = mz;
            int ends[2] = { s, e };
            for (int q = 0; q < 2; q++) {
                int nd = ends[q];
                if (wnode_of[nd] < 0) {                                   /* (x, y, 0), z SuperInteger = +0x10 */
                    int w = out->nwnode++;
                    out->wnode[3 * w] = nodes[nd].c[0]; out->wnode[3 * w + 1] = nodes[nd].c[1];
                    out->wnode[3 * w + 2] = 0.0;
                    wnode_of[nd] = w;
                }
            }
            (void)si_zero;
            out->sil_edge[2 * out->nsil] = s; out->sil_edge[2 * out->nsil + 1] = e; out->nsil++;
            /* PlaneCut::AddInputTriangle(start, end, mid, id++, false, NULL) */
            out->wtri[3 * out->nwtri] = wnode_of[s]; out->wtri[3 * out->nwtri + 1] = wnode_of[e];
            out->wtri[3 * out->nwtri + 2] = mid; out->wtri_id[out->nwtri] = id++; out->nwtri++;
        }
        g0 = g1;
    }
    /* PlaneCut::EndInput(true) follows. A flag raised by the mid SuperIntegers above is not reported by this slot
     * (its check precedes the loop). */

    out->nnode = nn;
    out->node = malloc((size_t)(nn > 0 ? nn : 1) * 3 * sizeof(double));
    if (!out->node) goto done;
    for (int i = 0; i < nn; i++) memcpy(out->node + 3 * i, nodes[i].c, 3 * sizeof(double));
    rc = out->overflow ? 1 : 0;
done:
    free(nodes); free(hs.slot); free(ktri); free(ksrc); free(kang); free(er); free(wnode_of);
    if (rc < 0) ifk_silhouette_walls_free(out);
    return rc;
}
