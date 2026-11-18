/* ifk_sil_shell2d.c - port of the interfacial11.dll Shell2D as Silhouette's test-point classifier uses it
 * (FUN_180022e80, >= 2 facets listed on a test point): FUN_1800043f0 (ctor), FUN_18000a1a0 (component),
 * FUN_180004840 (vertex), FUN_1800049a0 (segment), FUN_180008ed0 (component close), FUN_180004a50 (process:
 * FUN_1800057f0 quadtree, FUN_180005b10 seed, FUN_180006310 resolve, FUN_180006b80 pair test, the walk),
 * FUN_180007cc0 (reset) and FUN_180021830 (+ FUN_180021960, FUN_180022260) = test point on an output edge.
 * Decompile: decomp/interfacial/i11_silhouette_all.c; disassembly i11_dis_4a50.txt (FUN_180004a50) and
 * i11_dis_6b80.txt (FUN_180006b80), same folder.
 *
 * FUNCTION-BY-FUNCTION COMPARISON with the ifspinsil Shell2D port (viewer/ifk/ifk_spinsil.c, interfacial.md 6
 * items 3-4). ifk_spinsil.c keeps its code static; this file carries its own copy where the verdict is "same"
 * (cited to both addresses), and the i11 version where it differs.
 * | interfacial11                         | ifspinsil (ifk_spinsil.c)          | verdict |
 * |---------------------------------------|------------------------------------|---------|
 * | FUN_1800043f0 ctor: [0] = g^2, [1] = g^4, [2] = g^6 | FUN_18003dad0          | same (g = 2^-20 -> grid 2^-40) |
 * | FUN_180004840 vertex: floor(x / g) g, no dedup, extreme vertex of the component (FUN_1800035b0 on x, replaced when the new one is greater, first kept on ties) | FUN_18003bce0 + FUN_18003b890 | same computation (the extreme tracking is inlined in i11; notes_i11_silhouette.md sec. 3 said it had none: wrong) |
 * | FUN_1800049a0 segment (equal ends -> none; [2] = itself; on both incidence lists; +0x48 = component) | FUN_18003ab80 | same |
 * | FUN_180008ed0 component tree keyed by FUN_180003820 (extreme x, equal goes right), walked from the right | component order of walk() | same |
 * | FUN_1800035b0 compare on an axis (filtered, exact FUN_1800036b0) | FUN_18003b890 / FUN_18003b670 | same (same sum order; exact = sign of a math quantity) |
 * | FUN_180003b10 edge vs its segment direction (error 8) | FUN_18003be10 | same |
 * | FUN_180003a90 axis of a segment (error 5) | FUN_18003a1d0 | same |
 * | FUN_180003cb0 filtered orientation | FUN_180039eb0 | DIFFERENT summation order of the error bound (i11: the uy dx product's terms first, one chain, then the ux dy terms, then |v|); ported from i11 |
 * | FUN_180003eb0 exact orientation | FUN_18003b370 | same sign (exact) |
 * | FUN_180002c90 exact vertex (crossing: den, num, X, Y; doubles by FUN_1800085c0) | FUN_18003adb0 + FUN_180004cc0 | same (FUN_1800085c0 is the 4x unrolled sequential sum; division error with commuted 2-term sums only). i11 frees the exact records after each use (+0x28 list) and recomputes them; recomputation gives the same values, so the cache is kept here |
 * | FUN_180008500 double -> 2 limbs | FUN_180004f20 | same (own overflow flag: TLS DAT_180268398) |
 * | FUN_180002a20 filtered product | FUN_180004030 | same (first two error terms commuted: one 2-term sum) |
 * | FUN_180003250 merge vertices (input vertex stays the root; error 10) | FUN_18003a440 | DIFFERENT: i11 stamps the root's edges with its own counter DAT_1802682a8 in field +0x30 (the port shares one stamp with the candidate search) and marks the component of every moved edge visited (+8, list +0x88). Ported from i11 |
 * | FUN_180003430 / FUN_1800034c0 incidence add / remove (error 9) | FUN_18003a3c0 / FUN_18003a300 | same |
 * | FUN_180007810 sub-edge (+0x38 list) | FUN_18003a9e0 | same (the +0x68 shared-crossing branch is off: ctor sets +0x68 = 0) |
 * | FUN_180007680 collinear end | FUN_18003ba10 | same |
 * | FUN_180007400 pair classification | FUN_18003bb30 | same |
 * | FUN_180006b80 pair test | FUN_18003c5e0 | same order and formulas (checked in the disassembly that the touch result of the exact classification is re-read, 180006f55) |
 * | FUN_180006310 resolve (candidate stamp DAT_180268290 / +0x2c, vertex stamp DAT_180268260 / +0x50, sorted set FUN_18000a790 equal goes right) | FUN_18003cd90 | same logic; separate stamps as in i11 |
 * | FUN_180003980 point on edge | FUN_18003c120 | same |
 * | FUN_180005b10 seed | FUN_18003d4c0 | DIFFERENT: when the extreme vertex has no incident edge, i11 first looks (vertex list +0x18 from its head) for a vertex with edges that is equal in x and y (FUN_1800035b0) and makes it the extreme; candidates come from the groups of the extreme's head edge only (none when that edge has no groups). Ported from i11 |
 * | FUN_1800079f0 / FUN_180007b60 / FUN_180007970 turn choice | FUN_18003c2f0 / FUN_18003c230 / FUN_18003b980 | same |
 * | FUN_180004a50 walk | FUN_18003e860 | DIFFERENT: an item whose edge belongs to a component already done (+9) discards the walk of this component (output list back to its start, queue dropped); components visited during a walk (+8) are all marked done after it (+0x88); split loop twice (R15D = 2 at 180004cc3). Ported from i11 |
 * | FUN_180021830 / FUN_180021960 / FUN_180022260 | none | new |
 *
 * QUADTREE (FUN_1800057f0, Shell2D::EdgeQuadtree, groups at edge +0x20) - replaced [inferred, same argument as
 * interfacial.md 6 / 11.7.4]: it only proposes candidate pairs; two segments that meet share a leaf. An input
 * segment "has groups" here when another input segment's box overlaps its box, and the candidates of E are the
 * input segments with overlapping boxes. Non-meeting pairs produce nothing in FUN_180006b80. The order in which
 * pairs are tested is ours (it can only change which of two equal vertices becomes the root). */
#include "ifk_sil_shell2d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define U52 2.220446049250313e-16
#define U53 1.1102230246251565e-16

/* ------------------------------------------------------------------ lists (push order; DLL head = last) */
typedef struct { int n, cap; int *v; } IL;
static void il_push(IL *l, int x)
{
    if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 4; l->v = realloc(l->v, (size_t)l->cap * sizeof(int)); }
    l->v[l->n++] = x;
}
static int il_find(const IL *l, int x) { for (int i = l->n - 1; i >= 0; i--) if (l->v[i] == x) return i; return -1; }
static void il_remove_at(IL *l, int i) { memmove(l->v + i, l->v + i + 1, (size_t)(l->n - i - 1) * sizeof(int)); l->n--; }
static void il_free(IL *l) { free(l->v); l->v = NULL; l->n = l->cap = 0; }

/* ------------------------------------------------------------------ filtered numbers */
typedef struct { double v, e; } Fn;
static Fn f_abs(double x) { Fn r = { x, fabs(x) }; return r; }                      /* FUN_18003fdd0 */
static Fn f_dd(double a, double b) { Fn r; r.v = a - b; r.e = fabs(r.v); return r; } /* diff of two plain doubles */
static Fn f_sub(Fn a, Fn b) { Fn r; r.v = a.v - b.v; r.e = a.e + b.e + fabs(r.v); return r; }
static Fn f_add(Fn a, Fn b) { Fn r; r.v = a.v + b.v; r.e = a.e + b.e + fabs(r.v); return r; }
static Fn f_mul(Fn a, Fn b)   /* FUN_180002a20 (= ifspinsil FUN_180004030) */
{
    Fn r; r.v = a.v * b.v;
    r.e = b.e * fabs(a.v) + a.e * fabs(b.v) + b.e * a.e * U53 + fabs(r.v);
    return r;
}
static Fn f_div(Fn a, Fn b)   /* inline in FUN_180006b80 / FUN_180002c90 (= ifspinsil FUN_180004180) */
{
    Fn r; r.v = a.v / b.v;
    r.e = (b.e * fabs(r.v) + a.e) / (fabs(b.v) - b.e * U52) + fabs(r.v);
    return r;
}
static int f_maybe0(Fn a) { return fabs(a.v) <= a.e * U52; }

/* ------------------------------------------------------------------ big integers (same as ifk_spinsil.c) */
#define BL 12
typedef struct { unsigned char pos; uint32_t l[BL]; } Big;

/* ------------------------------------------------------------------ records */
typedef struct {
    double x, ex, y, ey;    /* +0 .. +0x18 */
    int sa, sb;             /* +0x20 / +0x28: segments of a crossing vertex, -1 = inserted vertex */
    int parent;             /* +0x38 union-find */
    int ex_ok; Big X, Y, D; /* +0x40 exact record */
    IL inc;                 /* +0x48 incident edges (push order) */
    int st50;               /* +0x50 (DAT_180268260) */
} SV;
typedef struct {
    int a, b;               /* [0], [1] */
    int seg;                /* [2]: the input segment (itself for one) */
    IL hits;                /* +0x18 */
    int groups;             /* +0x20 non-empty (input segments only, see QUADTREE above) */
    char visited, rev;      /* +0x28 / +0x29 */
    int st2c;               /* +0x2c (DAT_180268290) */
    int st30;               /* +0x30 (DAT_1802682a8) */
    int comp;               /* +0x48 (segments) */
    double bb[4];
} SE;
typedef struct { int ext; char visited, done; } SC;   /* FUN_18000a1a0 record: [0], +8, +9 */

struct IfkS2 {
    double g, g2, g3;       /* [0] [1] [2] */
    SV *v; int nv, cv;
    SE *e; int ne, ce;
    SC *c; int nc, cc;
    IL verts;               /* +0x18 inserted vertices */
    IL segs;                /* +0x30 input segments */
    IL subs;                /* +0x38 sub-edges */
    IL out;                 /* +0x40 output (push-front: head = last) */
    IL compl;               /* +0x58 closed components (insertion order; the tree order is built in process) */
    IL touched;             /* +0x88 */
    int cur;                /* +0x60, -1 = none */
    int axis;               /* DAT_180268278 */
    int st290, st260, st2a8;
    int overflow;           /* TLS DAT_180268398 */
    int ok;
    int ncheck;
    char err[160];
};

static void s_err(IfkS2 *S, const char *m) { if (!S->err[0]) snprintf(S->err, sizeof S->err, "%s", m); }

/* ------------------------------------------------------------------ big integer helpers */
static void big_from_double(IfkS2 *S, Big *b, double v, double unit)   /* FUN_180008500 */
{
    double q = v / unit, hi;
    memset(b, 0, sizeof *b);
    if (v <= 0.0) { b->pos = 0; q = -q; } else b->pos = 1;
    hi = floor(q * 2.3283064365386963e-10);
    b->l[0] = (uint32_t)(int64_t)(q - hi * 4294967296.0);
    if (4294967296.0 <= hi) S->overflow = 1;
    b->l[1] = (uint32_t)(int64_t)hi;
}
static void mag_sub(uint32_t *d, const uint32_t *a, const uint32_t *b, int top)
{
    int borrow = a[0] < b[0];
    d[0] = a[0] - b[0];
    for (int i = 1; i <= top; i++) {
        if (borrow) { borrow = (a[i] == 0) || (a[i] - 1 < b[i]); d[i] = a[i] - b[i] - 1; }
        else { borrow = a[i] < b[i]; d[i] = a[i] - b[i]; }
    }
}
static void big_addsub(IfkS2 *S, Big *d, const Big *a, unsigned char bsign, const uint32_t *bl, int n)
{
    Big r; memset(&r, 0, sizeof r);
    if (a->pos == bsign) {
        int carry = (uint32_t)~a->l[0] < bl[0];
        r.l[0] = a->l[0] + bl[0];
        for (int i = 1; i < n; i++) {
            if (carry) { carry = (~a->l[i] == 0) || (~a->l[i] - 1 < bl[i]); r.l[i] = a->l[i] + 1 + bl[i]; }
            else { carry = ~a->l[i] < bl[i]; r.l[i] = a->l[i] + bl[i]; }
        }
        if (carry) S->overflow = 1;
        r.pos = a->pos;
    } else {
        int i = n;
        while (--i >= 0 && a->l[i] == bl[i]) r.l[i] = 0;
        if (i < 0) r.pos = 1;
        else if (bl[i] < a->l[i]) { mag_sub(r.l, a->l, bl, i); r.pos = a->pos; }
        else { mag_sub(r.l, bl, a->l, i); r.pos = bsign; }
    }
    *d = r;
}
static void big_sub(IfkS2 *S, Big *d, const Big *a, const Big *b, int n) { big_addsub(S, d, a, (unsigned char)(b->pos == 0), b->l, n); }
static void big_add(IfkS2 *S, Big *d, const Big *a, const Big *b, int n) { big_addsub(S, d, a, b->pos, b->l, n); }
static void big_mul(Big *d, const Big *a, int n, const Big *b, int m)
{
    Big r; memset(&r, 0, sizeof r);
    for (int j = 0; j < m; j++) {
        uint64_t carry = 0;
        for (int i = 0; i < n; i++) {
            uint64_t t = (uint64_t)a->l[i] * b->l[j] + r.l[i + j] + carry;
            r.l[i + j] = (uint32_t)t; carry = t >> 32;
        }
        r.l[j + n] = (uint32_t)carry;
    }
    r.pos = (unsigned char)(a->pos == b->pos);
    *d = r;
}
static int big_sign(const Big *a, int n) { for (int i = 0; i < n; i++) if (a->l[i] != 0) return a->pos ? 1 : -1; return 0; }
static int mag_cmp(const Big *a, const Big *b, int n)
{
    for (int i = n - 1; i >= 0; i--) { if (b->l[i] < a->l[i]) return 1; if (a->l[i] < b->l[i]) return -1; }
    return 0;
}
static int big_cmp(const Big *a, const Big *b, int n)
{
    int sa = big_sign(a, n), sb = big_sign(b, n);
    if (sa == -1) return sb == -1 ? -mag_cmp(a, b, n) : -1;
    if (sa == 1) return sb == 1 ? mag_cmp(a, b, n) : 1;
    return -sb;
}
static double big_to_double(const Big *a, int n, double scale)   /* FUN_1800085c0 (sequential, lowest limb first) */
{
    double sum = 0.0, s = scale;
    for (int i = 0; i < n; i++) { sum = sum + (double)a->l[i] * s; s = s * 4294967296.0; }
    return a->pos ? sum : -sum;
}

/* ------------------------------------------------------------------ records */
static int sv_new(IfkS2 *S)
{
    if (S->nv == S->cv) { S->cv = S->cv ? 2 * S->cv : 64; S->v = realloc(S->v, (size_t)S->cv * sizeof(SV)); }
    SV *v = &S->v[S->nv]; memset(v, 0, sizeof *v);
    v->sa = v->sb = -1; v->parent = -1;
    return S->nv++;
}
static int se_new(IfkS2 *S)
{
    if (S->ne == S->ce) { S->ce = S->ce ? 2 * S->ce : 64; S->e = realloc(S->e, (size_t)S->ce * sizeof(SE)); }
    SE *e = &S->e[S->ne]; memset(e, 0, sizeof *e);
    e->comp = -1;
    return S->ne++;
}
static int vfind(IfkS2 *S, int v)   /* the inline +0x38 path-compression loops */
{
    int r = v;
    while (S->v[r].parent >= 0) r = S->v[r].parent;
    while (S->v[v].parent >= 0 && S->v[v].parent != r) { int n = S->v[v].parent; S->v[v].parent = r; v = n; }
    return r;
}

/* FUN_180002c90: exact record; crossings also get their doubles from it */
static void sv_exact(IfkS2 *S, int vi)
{
    SV *v = &S->v[vi];
    if (v->ex_ok) return;
    v->ex_ok = 1;
    if (v->sa < 0) {
        big_from_double(S, &v->X, v->x, S->g);
        big_from_double(S, &v->Y, v->y, S->g);
        return;
    }
    SE *A = &S->e[v->sa], *B = &S->e[v->sb];
    int a0 = A->a, a1 = A->b, b0 = B->a, b1 = B->b;
    sv_exact(S, a0); sv_exact(S, a1); sv_exact(S, b0); sv_exact(S, b1);
    v = &S->v[vi];
    Big dax, day, dbx, dby, t1, t2, den, nx, ny, num, X, Y;
    big_sub(S, &dax, &S->v[a1].X, &S->v[a0].X, 2); big_sub(S, &day, &S->v[a1].Y, &S->v[a0].Y, 2);
    big_sub(S, &dbx, &S->v[b1].X, &S->v[b0].X, 2); big_sub(S, &dby, &S->v[b1].Y, &S->v[b0].Y, 2);
    big_mul(&t1, &dax, 2, &dby, 2); big_mul(&t2, &day, 2, &dbx, 2);
    big_sub(S, &den, &t2, &t1, 4);
    int ds = big_sign(&den, 4);
    if (ds == 0) s_err(S, "Shell2D error 2: bad intersection.");
    big_sub(S, &nx, &S->v[a0].X, &S->v[b0].X, 2); big_sub(S, &ny, &S->v[a0].Y, &S->v[b0].Y, 2);
    big_mul(&t1, &nx, 2, &dby, 2); big_mul(&t2, &ny, 2, &dbx, 2);
    big_sub(S, &num, &t1, &t2, 4);
    big_mul(&t1, &S->v[a0].X, 2, &den, 4); big_mul(&t2, &num, 4, &dax, 2); big_add(S, &X, &t1, &t2, 6);
    big_mul(&t1, &S->v[a0].Y, 2, &den, 4); big_mul(&t2, &num, 4, &day, 2); big_add(S, &Y, &t1, &t2, 6);
    if (ds < 0) { X.pos = !X.pos; Y.pos = !Y.pos; den.pos = !den.pos; }
    v->X = X; v->Y = Y; v->D = den;
    Fn fx = f_abs(big_to_double(&X, 6, S->g3)), fy = f_abs(big_to_double(&Y, 6, S->g3));
    Fn fd = f_abs(big_to_double(&den, 4, S->g2));
    Fn qx = f_div(fx, fd), qy = f_div(fy, fd);
    v->x = qx.v; v->ex = qx.e; v->y = qy.v; v->ey = qy.e;
}

/* line record (FUN_180003c10 and the inline copies): base / other end of the segment, oriented like the edge */
typedef struct { Fn dx, dy; int base, other; int seg; } Line;
static int sv_cmp(IfkS2 *S, int a, int b);
static int same_dir(IfkS2 *S, int ei)   /* FUN_180003b10 */
{
    SE *e = &S->e[ei];
    if (e->seg == ei) return 1;
    SE *in = &S->e[e->seg];
    double x0 = S->v[in->a].x, x1 = S->v[in->b].x, y0 = S->v[in->a].y, y1 = S->v[in->b].y;
    int sgn;
    if (x1 <= x0) {
        if (x0 <= x1) {
            if (y1 <= y0) {
                if (y0 <= y1) { s_err(S, "Shell2D error 8: invalid edge."); sgn = 0; }   /* local_res8 unset: 0 here [ours] */
                else sgn = 1;
            } else sgn = -1;
            S->axis = 1;
        } else { sgn = 1; S->axis = 0; }
    } else { sgn = -1; S->axis = 0; }
    return sv_cmp(S, e->a, e->b) == sgn;
}
static void mkline(IfkS2 *S, Line *L, int ei)
{
    SE *in = &S->e[S->e[ei].seg];
    if (same_dir(S, ei)) { L->base = in->a; L->other = in->b; } else { L->base = in->b; L->other = in->a; }
    L->dx = f_dd(S->v[L->other].x, S->v[L->base].x);
    L->dy = f_dd(S->v[L->other].y, S->v[L->base].y);
    L->seg = S->e[ei].seg;
}
/* FUN_180003cb0: filtered orientation (value ux dy - uy dx), i11 summation order of the bound */
static int orient_f(IfkS2 *S, const Line *L, int pi, int *certain, Fn *val)
{
    SV *P = &S->v[pi], *B = &S->v[L->base];
    double ux = P->x - B->x, uy = P->y - B->y, eux, euy;
    if (P->sa < 0) { eux = fabs(ux); euy = fabs(uy); }
    else { eux = fabs(ux) + P->ex; euy = fabs(uy) + P->ey; }
    double dx = L->dx.v, edx = L->dx.e, dy = L->dy.v, edy = L->dy.e;
    double p1 = uy * dx, p2 = ux * dy, v = p2 - p1;
    double e = edx * fabs(uy) + euy * fabs(dx) + edx * euy * U53 + fabs(p1)
             + edy * fabs(ux) + eux * fabs(dy) + edy * eux * U53 + fabs(p2) + fabs(v);
    val->v = v; val->e = e;
    if (e * U52 < fabs(v)) { *certain = 1; return 0.0 < v ? 1 : -1; }
    *certain = e == 0.0;
    return 0;
}
/* FUN_180003eb0: exact orientation */
static int orient_x(IfkS2 *S, const Line *L, int pi)
{
    sv_exact(S, pi); sv_exact(S, L->base); sv_exact(S, L->other);
    SV *P = &S->v[pi], *B = &S->v[L->base], *O = &S->v[L->other];
    Big dx, dy, ux, uy, t1, t2, r;
    big_sub(S, &dx, &O->X, &B->X, 2); big_sub(S, &dy, &O->Y, &B->Y, 2);   /* FUN_1800040d0 */
    if (P->sa < 0) {
        big_sub(S, &ux, &P->X, &B->X, 2); big_sub(S, &uy, &P->Y, &B->Y, 2);
        big_mul(&t1, &ux, 2, &dy, 2); big_mul(&t2, &uy, 2, &dx, 2);
        big_sub(S, &r, &t1, &t2, 4);
        return big_sign(&r, 4);
    }
    Big bx, by;
    big_mul(&bx, &P->D, 4, &B->X, 2); big_mul(&by, &P->D, 4, &B->Y, 2);
    big_sub(S, &ux, &P->X, &bx, 6); big_sub(S, &uy, &P->Y, &by, 6);
    big_mul(&t1, &ux, 6, &dy, 2); big_mul(&t2, &uy, 6, &dx, 2);
    big_sub(S, &r, &t1, &t2, 8);
    return big_sign(&r, 8);
}
static int orient(IfkS2 *S, const Line *L, int pi)
{
    int c; Fn v;
    int s = orient_f(S, L, pi, &c, &v);
    if (!c) s = orient_x(S, L, pi);
    return s;
}
static int sv_cmp_exact(IfkS2 *S, int a, int b, int ax)   /* FUN_1800036b0 */
{
    int r;
    if (S->v[a].sa < 0 || S->v[b].sa < 0) {
        int in = a, ot = b;
        if (S->v[a].sa >= 0) { in = b; ot = a; }
        sv_exact(S, ot); sv_exact(S, in);
        if (S->v[ot].sa < 0) {   /* both inserted vertices */
            r = big_cmp(ax ? &S->v[a].Y : &S->v[a].X, ax ? &S->v[b].Y : &S->v[b].X, 2);
            return r == 0 ? 0 : (r < 0 ? -1 : 1);
        }
        Big t; big_mul(&t, &S->v[ot].D, 4, ax ? &S->v[in].Y : &S->v[in].X, 2);
        r = big_cmp(&t, ax ? &S->v[ot].Y : &S->v[ot].X, 6);
        if (a != in) r = -r;
    } else {
        sv_exact(S, a); sv_exact(S, b);
        Big t1, t2;
        big_mul(&t1, &S->v[b].D, 4, ax ? &S->v[a].Y : &S->v[a].X, 6);
        big_mul(&t2, &S->v[a].D, 4, ax ? &S->v[b].Y : &S->v[b].X, 6);
        r = big_cmp(&t1, &t2, 10);
    }
    return r == 0 ? 0 : (r < 0 ? -1 : 1);
}
static int sv_cmp(IfkS2 *S, int a, int b)   /* FUN_1800035b0 on axis DAT_180268278 */
{
    double pa = S->axis ? S->v[a].y : S->v[a].x, pb = S->axis ? S->v[b].y : S->v[b].x;
    double ea = S->axis ? S->v[a].ey : S->v[a].ex, eb = S->axis ? S->v[b].ey : S->v[b].ex;
    double d = pa - pb;
    if (fabs(d) <= (ea + eb + fabs(d)) * U52) {
        if (ea == 0.0 && eb == 0.0) return 0;
        return sv_cmp_exact(S, a, b, S->axis);
    }
    return (pa <= pb && pb != pa) ? -1 : 1;
}
static int seg_axis(IfkS2 *S, int ei)   /* FUN_180003a90 */
{
    SE *in = &S->e[S->e[ei].seg];
    if (S->v[in->a].x != S->v[in->b].x) return 0;
    if (S->v[in->a].y != S->v[in->b].y) return 1;
    s_err(S, "Shell2D error 5: invalid edge.");
    return -1;
}

/* component visited (+8) with the +0x88 list (FUN_180003250, FUN_180004a50) */
static void comp_touch(IfkS2 *S, int c)
{
    if (!S->c[c].visited) il_push(&S->touched, c);
    S->c[c].visited = 1;
}
/* FUN_180003250: merge vertices p, q (the inserted vertex stays the root) */
static void sv_union(IfkS2 *S, int p, int q)
{
    int child = q, root = p;
    if (S->v[p].sa >= 0 && S->v[q].sa < 0) { child = p; root = q; }
    if (child == root) return;
    S->v[child].parent = root;
    S->st2a8 += 2;
    for (int i = 0; i < S->v[root].inc.n; i++) S->e[S->v[root].inc.v[i]].st30 = S->st2a8;
    while (S->v[child].inc.n > 0) {
        int e = S->v[child].inc.v[--S->v[child].inc.n];   /* pop the head */
        comp_touch(S, S->e[S->e[e].seg].comp);
        if (S->e[e].st30 != S->st2a8) il_push(&S->v[root].inc, e);
    }
}
static int add_inc(IfkS2 *S, int v, int e)   /* FUN_180003430 */
{
    if (il_find(&S->v[v].inc, e) >= 0) return 0;
    il_push(&S->v[v].inc, e);
    return 1;
}
static void rm_inc(IfkS2 *S, int v, int e)   /* FUN_1800034c0 */
{
    int i = il_find(&S->v[v].inc, e);
    if (i < 0) { s_err(S, "Shell2D error 9: corrupt edge record."); return; }
    il_remove_at(&S->v[v].inc, i);
}
static int sub_edge(IfkS2 *S, int a, int b, int seg)   /* FUN_180007810 + FUN_1800038e0 */
{
    int e = se_new(S);
    S->e[e].a = a; S->e[e].b = b; S->e[e].seg = seg;
    il_push(&S->v[a].inc, e); il_push(&S->v[b].inc, e);
    il_push(&S->subs, e);
    return e;
}
static void colin_end(IfkS2 *S, int p, int ei)   /* FUN_180007680 */
{
    int c1 = sv_cmp(S, p, S->e[ei].a);
    if (c1 == 0) { sv_union(S, vfind(S, p), vfind(S, S->e[ei].a)); return; }
    int c2 = sv_cmp(S, p, S->e[ei].b);
    if (c2 == 0) { sv_union(S, vfind(S, p), vfind(S, S->e[ei].b)); return; }
    if (c1 != c2) { int r = vfind(S, p); if (add_inc(S, r, ei)) il_push(&S->e[ei].hits, r); }
}
static int classify(IfkS2 *S, int s1, int s2, int A, int *touch, int B)   /* FUN_180007400 */
{
    if (s1 == 0) {
        if (s2 == 0) {
            S->axis = seg_axis(S, A);
            colin_end(S, S->e[A].a, B); colin_end(S, S->e[A].b, B);
            colin_end(S, S->e[B].a, A); colin_end(S, S->e[B].b, A);
            return 1;
        }
        *touch = S->e[A].a;
    } else if (s2 == 0) *touch = S->e[A].b;
    else return s1 == s2;
    int t = vfind(S, *touch);
    return t == vfind(S, S->e[B].a) || t == vfind(S, S->e[B].b);
}
static void pair_test(IfkS2 *S, int E, const Line *LE, int F)   /* FUN_180006b80 */
{
    int unc = 0, tF = -1, tE = -1, c1, c2;
    Fn v1, v2, vE;
    int s1 = orient_f(S, LE, S->e[F].a, &c1, &v1);
    if (!c1) { int r = vfind(S, S->e[F].a); if (r == vfind(S, S->e[E].a) || r == vfind(S, S->e[E].b)) c1 = 1; }
    int s2 = orient_f(S, LE, S->e[F].b, &c2, &v2);
    if (!c2) { int r = vfind(S, S->e[F].b); if (r == vfind(S, S->e[E].a) || r == vfind(S, S->e[E].b)) c2 = 1; }
    if (!c1 || !c2) unc = 1;
    else if (classify(S, s1, s2, F, &tF, E)) return;
    Line LF; mkline(S, &LF, F);
    s1 = orient_f(S, &LF, S->e[E].a, &c1, &vE);
    s2 = orient_f(S, &LF, S->e[E].b, &c2, &v2);
    if (c1 && c2) {
        if (classify(S, s1, s2, E, &tE, F)) return;
    } else unc = 1;
    if (unc) {
        s1 = orient_x(S, LE, S->e[F].a); s2 = orient_x(S, LE, S->e[F].b);
        if (classify(S, s1, s2, F, &tF, E)) return;
        s1 = orient_x(S, &LF, S->e[E].a); s2 = orient_x(S, &LF, S->e[E].b);
        if (classify(S, s1, s2, E, &tE, F)) return;
    }
    if (tE < 0) {
        if (tF < 0) {
            int nv = sv_new(S);
            S->v[nv].sa = S->e[E].seg; S->v[nv].sb = S->e[F].seg;
            Fn a = f_mul(LE->dx, LF.dy), b = f_mul(LE->dy, LF.dx);
            Fn den = f_sub(b, a);
            if (f_maybe0(den)) sv_exact(S, nv);
            else {
                Fn t = f_div(vE, den);
                SV *ea = &S->v[S->e[E].a];
                Fn x0 = { ea->x, ea->ex }, y0 = { ea->y, ea->ey };
                Fn x = f_add(f_mul(LE->dx, t), x0), y = f_add(f_mul(LE->dy, t), y0);
                S->v[nv].x = x.v; S->v[nv].ex = x.e; S->v[nv].y = y.v; S->v[nv].ey = y.e;
            }
            il_push(&S->e[E].hits, nv); il_push(&S->e[F].hits, nv);
            il_push(&S->v[nv].inc, E); il_push(&S->v[nv].inc, F);
        } else {
            int r = vfind(S, tF);
            if (add_inc(S, r, E)) il_push(&S->e[E].hits, r);
        }
    } else if (tF < 0) {
        int r = vfind(S, tE);
        if (add_inc(S, r, F)) il_push(&S->e[F].hits, r);
    } else sv_union(S, vfind(S, tE), vfind(S, tF));
}
static int bb_overlap(const double *a, const double *b) { return !(a[2] < b[0] || b[2] < a[0] || a[3] < b[1] || b[3] < a[1]); }

/* FUN_180006310: crossings of E with its candidates, then the split */
static void resolve(IfkS2 *S, int E)
{
    if (!S->e[E].groups) return;
    Line LE; mkline(S, &LE, E);
    S->st290 += 2; S->e[E].st2c = S->st290;
    for (int k = S->segs.n - 1; k >= 0; k--) {   /* the groups (QUADTREE note); order ours */
        int F = S->segs.v[k];
        if (S->e[F].st2c == S->st290 || !S->e[F].groups) continue;
        if (!bb_overlap(S->e[E].bb, S->e[F].bb)) continue;
        S->e[F].st2c = S->st290;
        pair_test(S, E, &LE, F);
    }
    S->e[E].groups = 0;
    if (S->e[E].hits.n == 0) return;   /* [inferred: the decompile lost this test; ifspinsil FUN_18003cd90 has it] */
    int a = vfind(S, S->e[E].a), b = vfind(S, S->e[E].b), seg = S->e[E].seg;
    if (S->e[E].hits.n == 1) {
        int h = vfind(S, S->e[E].hits.v[0]);
        S->e[E].hits.n = 0;
        rm_inc(S, a, E); rm_inc(S, b, E); rm_inc(S, h, E);
        sub_edge(S, a, h, seg); sub_edge(S, h, b, seg);
        return;
    }
    rm_inc(S, a, E); rm_inc(S, b, E);
    S->axis = seg_axis(S, E);
    S->st260 += 2;
    int *set = malloc((size_t)(S->e[E].hits.n + 2) * sizeof(int)); int ns = 0;
    while (S->e[E].hits.n > 0) {
        int h = vfind(S, S->e[E].hits.v[--S->e[E].hits.n]);
        if (S->v[h].st50 == S->st260) continue;
        S->v[h].st50 = S->st260;
        rm_inc(S, h, E);
        int j, dup = -1;
        for (j = 0; j < ns; j++) { int c = sv_cmp(S, h, set[j]); if (c == 0) { dup = j; break; } if (c < 0) break; }
        if (dup >= 0) { sv_union(S, vfind(S, set[dup]), h); continue; }
        memmove(set + j + 1, set + j, (size_t)(ns - j) * sizeof(int)); set[j] = h; ns++;
    }
    for (int q = 0; q < 2; q++) {   /* FUN_18000a790: equal goes right */
        int v = q ? b : a, j;
        for (j = 0; j < ns; j++) if (sv_cmp(S, v, set[j]) < 0) break;
        memmove(set + j + 1, set + j, (size_t)(ns - j) * sizeof(int)); set[j] = v; ns++;
    }
    int fwd = set[0] == a;
    for (int j = 1; j < ns; j++) {
        if (fwd) sub_edge(S, vfind(S, set[j - 1]), vfind(S, set[j]), seg);
        else sub_edge(S, vfind(S, set[j]), vfind(S, set[j - 1]), seg);
    }
    free(set);
}
static int on_edge(IfkS2 *S, int ei, int p)   /* FUN_180003980 */
{
    if (p == S->e[ei].a || p == S->e[ei].b) return 1;
    Line L; mkline(S, &L, ei);
    if (orient(S, &L, p) != 0) return 0;
    S->axis = seg_axis(S, ei);
    int c1 = sv_cmp(S, p, S->e[ei].a);
    if (c1 == 0) return 1;
    int c2 = sv_cmp(S, p, S->e[ei].b);
    return c2 == 0 || c1 != c2;
}
static void e_reverse(IfkS2 *S, int ei) { int t = S->e[ei].a; S->e[ei].a = S->e[ei].b; S->e[ei].b = t; S->e[ei].rev = !S->e[ei].rev; }

typedef struct { int e, v; } Item;
typedef struct { Item *v; int n, cap, head; } Queue;
static void q_push(Queue *q, Item it)
{
    if (q->n == q->cap) { q->cap = q->cap ? 2 * q->cap : 16; q->v = realloc(q->v, (size_t)q->cap * sizeof(Item)); }
    q->v[q->n++] = it;
}

/* FUN_180005b10: seed of component c, two items appended */
static int seed(IfkS2 *S, int c, Queue *q)
{
    int ext = S->c[c].ext;
    if (S->v[ext].inc.n == 0) {   /* look for an equal vertex with edges (vertex list +0x18 from its head) */
        for (int k = S->verts.n - 1; k >= 0; k--) {
            int w = S->verts.v[k];
            if (S->v[w].inc.n == 0) continue;
            SV *A = &S->v[w], *B = &S->v[ext];
            double d = A->x - B->x;
            if (!(fabs(d) <= (B->ex + A->ex + fabs(d)) * U52)) continue;
            d = A->y - B->y;
            if (!(fabs(d) <= (A->ey + B->ey + fabs(d)) * U52)) continue;
            int ax = 0, r = 0;
            S->axis = 0;
            while (ax < 2 && (r = sv_cmp(S, ext, w)) == 0) { ax++; S->axis = ax; }
            (void)r;
            if (ax == 2) { S->c[c].ext = ext = w; break; }
        }
    }
    S->st290 += 2;
    IL cand = { 0, 0, NULL };
    if (S->v[ext].inc.n > 0) {
        int e0 = S->v[ext].inc.v[S->v[ext].inc.n - 1];
        if (S->e[e0].groups) {   /* the groups of e0 (QUADTREE note): input segments with overlapping boxes, e0 too */
            for (int k = S->segs.n - 1; k >= 0; k--) {
                int F = S->segs.v[k];
                if (S->e[F].st2c == S->st290) continue;
                if (!bb_overlap(S->e[e0].bb, S->e[F].bb)) continue;
                if (on_edge(S, F, ext)) il_push(&cand, F);
                S->e[F].st2c = S->st290;
            }
        }
    }
    int sd = -1, f8 = 0;
    for (int i = cand.n - 1; i >= 0; i--) {
        int e = cand.v[i]; Line L; mkline(S, &L, e); f8 = 0;
        for (int j = cand.n - 1; j >= 0; j--) {
            if (cand.v[j] == e) continue;
            for (int k = 0; k < 2; k++) {
                int s = orient(S, &L, k ? S->e[cand.v[j]].b : S->e[cand.v[j]].a);
                if (s >= 1) f8 |= 1; else if (s < 0) f8 |= 2;
            }
            if (f8 == 3) break;
        }
        if (f8 != 3) { sd = e; break; }
    }
    il_free(&cand);
    if (sd < 0) { s_err(S, "Shell2D error 6: unable to find seed edge."); return 0; }   /* i11 goes on with NULL: not ported */
    if (f8 == 0) {
        SE *in = &S->e[S->e[sd].seg];
        if (S->v[in->a].x == S->v[in->b].x) { S->axis = 1; f8 = sv_cmp(S, S->e[sd].a, S->e[sd].b) != 1 ? 2 : 1; }
    }
    if (f8 == 1) e_reverse(S, sd);
    int ns0 = S->subs.n;
    resolve(S, sd);
    int start = vfind(S, ext), ue = -1;   /* item [1] = root of the extreme [inferred: the store is lost in the decompile] */
    if (S->subs.n == ns0) { if (vfind(S, S->e[sd].a) == start || vfind(S, S->e[sd].b) == start) ue = sd; }
    else for (int k = S->subs.n - 1; k >= ns0; k--) {
        int e = S->subs.v[k];
        if (vfind(S, S->e[e].a) == start || vfind(S, S->e[e].b) == start) { ue = e; break; }
    }
    if (ue < 0) { s_err(S, "Shell2D error 6: unable to find seed edge."); return 0; }   /* i11: item edge NULL, not ported */
    q_push(q, (Item){ ue, start });
    int other = vfind(S, S->e[ue].a) == start ? vfind(S, S->e[ue].b) : vfind(S, S->e[ue].a);
    q_push(q, (Item){ ue, other });
    S->e[ue].visited = 1;
    il_push(&S->out, ue);
    return 1;
}

/* turn choice record (local_e8 ..) */
typedef struct { int best, cur, bestfar, curfar, prev; Line LE; int cls, dc, back; } Pick;
static int b980(IfkS2 *S, int prev, int far, int v, int E)   /* FUN_180007970 */
{
    S->axis = seg_axis(S, E);
    return sv_cmp(S, prev, v) == sv_cmp(S, far, v);
}
static int c230(IfkS2 *S, Pick *p, int v)   /* FUN_180007b60 */
{
    Line L; mkline(S, &L, p->best);
    int s = S->e[p->cur].seg == S->e[p->best].seg ? 0 : orient(S, &L, p->curfar);
    int end = p->dc ? S->e[p->best].b : S->e[p->best].a;
    if (vfind(S, end) == v) s = -s;
    return s;
}
static void replace(Pick *p, int cls) { p->cls = cls; p->best = p->cur; p->bestfar = p->curfar; }
static void c2f0(IfkS2 *S, int E, Pick *p, int v)   /* FUN_1800079f0 */
{
    int c = S->e[p->cur].seg == S->e[E].seg ? 0 : orient(S, &p->LE, p->curfar);
    char bvis = S->e[p->best].visited;
    if (c == 0) {
        if (b980(S, p->prev, p->curfar, v, E)) {
            if (p->cls == 0 && p->back && !bvis) { replace(p, 0); p->back = 1; }
        } else if (p->cls < 0 || (p->cls == 0 && (p->back || !bvis))) { replace(p, 0); p->back = 0; }
    } else if (c < 1) {
        if (p->cls < 0) { int t = c230(S, p, v); if (0 < t || (!bvis && t == 0)) replace(p, -1); }
        else if (p->cls == 0 && p->back) replace(p, -1);
    } else if (p->cls < 1) replace(p, 1);
    else { int t = c230(S, p, v); if (0 < t || (!bvis && t == 0)) replace(p, 1); }
}
static int excluded(IfkS2 *S, int dc, int e, int v)
{
    if (!S->e[e].visited) return 0;
    return vfind(S, dc ? S->e[e].b : S->e[e].a) == v;
}

/* ================================================================== API */
IfkS2 *ifk_s2_new(double g)   /* FUN_1800043f0 */
{
    IfkS2 *S = calloc(1, sizeof *S);
    g = g * g;
    S->g = g; S->g2 = g * g; S->g3 = g * g * g;
    S->cur = -1;
    return S;
}
void ifk_s2_reset(IfkS2 *S)   /* FUN_180007cc0 */
{
    for (int i = 0; i < S->nv; i++) il_free(&S->v[i].inc);
    for (int i = 0; i < S->ne; i++) il_free(&S->e[i].hits);
    S->nv = S->ne = S->nc = 0;
    S->verts.n = S->segs.n = S->subs.n = S->out.n = S->compl.n = S->touched.n = 0;
    S->cur = -1; S->overflow = 0; S->ok = 0; S->ncheck = 0; S->err[0] = 0;
}
void ifk_s2_free(IfkS2 *S)
{
    if (!S) return;
    ifk_s2_reset(S);
    free(S->v); free(S->e); free(S->c);
    il_free(&S->verts); il_free(&S->segs); il_free(&S->subs); il_free(&S->out); il_free(&S->compl); il_free(&S->touched);
    free(S);
}
void ifk_s2_begin(IfkS2 *S)   /* +0x60 = FUN_18000a1a0(+0xc8) */
{
    if (S->nc == S->cc) { S->cc = S->cc ? 2 * S->cc : 4; S->c = realloc(S->c, (size_t)S->cc * sizeof(SC)); }
    S->c[S->nc].ext = -1; S->c[S->nc].visited = S->c[S->nc].done = 0;
    S->cur = S->nc++;
}
int ifk_s2_vertex(IfkS2 *S, double x, double y)   /* FUN_180004840 */
{
    int v = sv_new(S);
    S->v[v].x = floor(x / S->g) * S->g;   /* FUN_18019e9e4 = CRT floor */
    S->v[v].y = floor(y / S->g) * S->g;
    il_push(&S->verts, v);
    if (S->cur >= 0) {
        if (S->c[S->cur].ext < 0) S->c[S->cur].ext = v;
        else { S->axis = 0; if (sv_cmp(S, v, S->c[S->cur].ext) == 1) S->c[S->cur].ext = v; }
    }
    return v;
}
void ifk_s2_segment(IfkS2 *S, int a, int b)   /* FUN_1800049a0 */
{
    if (S->v[a].x == S->v[b].x && S->v[a].y == S->v[b].y) return;
    int e = se_new(S);
    S->e[e].a = a; S->e[e].b = b; S->e[e].seg = e;
    il_push(&S->v[a].inc, e); il_push(&S->v[b].inc, e);
    il_push(&S->segs, e);
    S->e[e].comp = S->cur;
    double x0 = S->v[a].x, x1 = S->v[b].x, y0 = S->v[a].y, y1 = S->v[b].y;
    S->e[e].bb[0] = x0 < x1 ? x0 : x1; S->e[e].bb[2] = x0 < x1 ? x1 : x0;
    S->e[e].bb[1] = y0 < y1 ? y0 : y1; S->e[e].bb[3] = y0 < y1 ? y1 : y0;
}
void ifk_s2_end(IfkS2 *S)   /* FUN_180008ed0(+0x58, +0x60); +0x60 = 0 */
{
    if (S->cur >= 0) il_push(&S->compl, S->cur);
    S->cur = -1;
}

int ifk_s2_process(IfkS2 *S)   /* FUN_180004a50 */
{
    S->ok = 1;   /* local_198 = 1 */
    /* FUN_1800057f0: groups (QUADTREE note) */
    for (int i = 0; i < S->segs.n; i++)
        for (int j = i + 1; j < S->segs.n; j++) {
            int A = S->segs.v[i], B = S->segs.v[j];
            if (bb_overlap(S->e[A].bb, S->e[B].bb)) S->e[A].groups = S->e[B].groups = 1;
        }
    /* component tree +0x58 (FUN_180008ed0 / FUN_180003820: extreme x, -1 left, else right), walked from the
     * rightmost to its predecessors */
    int nc = S->compl.n, *asc = malloc(sizeof(int) * (size_t)(nc + 1));
    for (int i = 0; i < nc; i++) {
        int c = S->compl.v[i], j = 0;
        S->axis = 0;
        while (j < i && sv_cmp(S, S->c[c].ext, S->c[asc[j]].ext) != -1) j++;
        memmove(asc + j + 1, asc + j, sizeof(int) * (size_t)(i - j)); asc[j] = c;
    }
    Queue q = { NULL, 0, 0, 0 };
    for (int ci = nc - 1; ci >= 0; ci--) {
        int c = asc[ci];
        if (S->c[c].visited || S->c[c].ext < 0) continue;
        int saved = S->out.n;   /* local_138 */
        q.n = q.head = 0;
        if (!seed(S, c, &q)) { free(asc); free(q.v); return -1; }   /* i11 continues with a NULL edge: not ported */
        while (q.head < q.n) {
            Item it = q.v[q.head++];
            int E = it.e, comp = S->e[S->e[E].seg].comp;
            if (S->c[comp].done) {   /* 180004c65: discard this component's walk */
                S->out.n = saved;
                q.n = q.head = 0;
                break;
            }
            comp_touch(S, comp);
            for (int k = 0; k < 2; k++) {   /* R15D = 2 (180004cc3) */
                int v = vfind(S, it.v), n = S->v[v].inc.n;
                int *tmp = malloc(sizeof(int) * (size_t)(n + 1));
                for (int i = 0; i < n; i++) tmp[i] = S->v[v].inc.v[i];   /* reversed onto a stack: oldest first */
                for (int i = 0; i < n; i++) resolve(S, tmp[i]);
                free(tmp);
            }
            Pick p; memset(&p, 0, sizeof p); p.best = -1;
            mkline(S, &p.LE, E);
            int v = vfind(S, it.v);
            if (vfind(S, S->e[E].a) == v) { p.dc = 0; p.prev = vfind(S, S->e[E].b); }
            else { p.dc = 1; p.prev = vfind(S, S->e[E].a); }
            IL *inc = &S->v[v].inc;
            int i = inc->n - 1;
            for (; i >= 0; i--) {
                int e = inc->v[i];
                if (e == E || excluded(S, p.dc, e, v)) continue;
                p.cur = p.best = e;
                p.curfar = p.bestfar = vfind(S, S->e[e].a) == v ? vfind(S, S->e[e].b) : vfind(S, S->e[e].a);
                if (S->e[E].seg == S->e[e].seg) p.cls = 0;
                else p.cls = orient(S, &p.LE, p.bestfar);
                if (p.cls == 0) p.back = b980(S, p.prev, p.bestfar, v, E);
                i--;
                break;
            }
            for (; i >= 0; i--) {
                int e = inc->v[i];
                if (e == E || excluded(S, p.dc, e, v)) continue;
                p.cur = e;
                p.curfar = vfind(S, S->e[e].a) == v ? vfind(S, S->e[e].b) : vfind(S, S->e[e].a);
                c2f0(S, E, &p, v);
            }
            if (p.best < 0) S->ok = 0;
            else if (!S->e[p.best].visited) {
                S->e[p.best].visited = 1;
                q_push(&q, (Item){ p.best, p.bestfar });
                il_push(&S->out, p.best);
                if (vfind(S, p.dc ? S->e[p.best].b : S->e[p.best].a) == v) e_reverse(S, p.best);
            }
        }
        for (int k = 0; k < S->touched.n; k++) S->c[S->touched.v[k]].done = 1;   /* LAB_180005468 */
        S->touched.n = 0;
    }
    free(asc); free(q.v);
    for (int k = 0; k < S->out.n; k++) {   /* output ends to their roots */
        int e = S->out.v[k];
        S->e[e].a = vfind(S, S->e[e].a); S->e[e].b = vfind(S, S->e[e].b);
    }
    if (S->overflow) { S->overflow = 0; s_err(S, "Shell2D error 4: overflow."); }
    return S->ok;
}

/* ------------------------------------------------------------------ exact helpers for the test point */
static void sd_trim(PcSD *a)
{
    int lo = 0, hi = a->n - 1;
    while (hi > 0 && a->l[hi] == 0) hi--;
    while (lo < hi && a->l[lo] == 0) lo++;
    if (lo) memmove(a->l, a->l + lo, sizeof(uint32_t) * (size_t)(hi - lo + 1));
    a->e += lo; a->n = hi - lo + 1;
}
/* Big of n limbs in units 2^ub -> PcSD (FUN_18002c790 / FUN_18002c910 convert the Shell2D integers with the
 * k of the grid; any exact conversion gives the same value) */
static void sd_from_big(PcSD *r, const Big *b, int n, int ub)
{
    memset(r, 0, sizeof *r);
    r->pos = b->pos;
    int E = ub >= 0 ? ub / 32 : -((-ub + 31) / 32), sh = ub - 32 * E;   /* 0 <= sh < 32 */
    r->e = E; r->n = n + 1;
    for (int i = 0; i < n; i++) {
        r->l[i] |= sh ? b->l[i] << sh : b->l[i];
        if (sh) r->l[i + 1] |= b->l[i] >> (32 - sh);
    }
    sd_trim(r);
}
static void sd_mul(PcSD *r, const PcSD *a, const PcSD *b)
{
    PcSD t; memset(&t, 0, sizeof t);
    if (a->n + b->n > PC_SD_L) { fprintf(stderr, "ifk_sil_shell2d: SuperDouble overflow\n"); abort(); }
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
static void sd_sub(PcSD *r, const PcSD *a, const PcSD *b)   /* a - b exactly */
{
    int e = a->e < b->e ? a->e : b->e;
    int ta = a->e + a->n, tb = b->e + b->n, top = (ta > tb ? ta : tb) + 1, n = top - e;
    uint32_t A[PC_SD_L + 2], B[PC_SD_L + 2];
    PcSD t; memset(&t, 0, sizeof t);
    if (n > PC_SD_L) { fprintf(stderr, "ifk_sil_shell2d: SuperDouble overflow\n"); abort(); }
    memset(A, 0, sizeof A); memset(B, 0, sizeof B);
    for (int i = 0; i < a->n; i++) A[a->e - e + i] = a->l[i];
    for (int i = 0; i < b->n; i++) B[b->e - e + i] = b->l[i];
    unsigned char bpos = (unsigned char)(b->pos == 0);
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
static int sd_sign(const PcSD *a)
{
    for (int i = 0; i < a->n; i++) if (a->l[i]) return a->pos ? 1 : -1;
    return 0;
}

/* FUN_180021960: side of the test point P against the line L (filtered, exact through the test point's slot 0) */
static int tp_orient(IfkS2 *S, const double *P, const Line *L, IfkS2Exact exact, void *user)
{
    SV *B = &S->v[L->base];
    double ux = P[0] - B->x, eux = fabs(ux) + P[1];
    double uy = P[2] - B->y, euy = fabs(uy) + P[3];
    double dx = L->dx.v, edx = L->dx.e, dy = L->dy.v, edy = L->dy.e;
    double p2 = ux * dy, p1 = uy * dx, v = p2 - p1;
    double e = edx * fabs(uy) + euy * fabs(dx) + euy * edx * U53 + fabs(p1)
             + edy * fabs(ux) + eux * fabs(dy) + eux * edy * U53 + fabs(p2) + fabs(v);
    if (e * U52 < fabs(v)) return 0.0 < v ? 1 : -1;
    if (e == 0.0) return 0;
    /* exact: (X - bx W) dy - (Y - by W) dx, units: Shell2D integers in g (2 limbs) */
    PcSD X, Y, W, bx, by, ddx, ddy, t, u1, u2, r;
    exact(user, &X, &Y, &W);
    sv_exact(S, L->base); sv_exact(S, L->other);
    Big bdx, bdy;
    big_sub(S, &bdx, &S->v[L->other].X, &S->v[L->base].X, 2);   /* FUN_1800040d0 */
    big_sub(S, &bdy, &S->v[L->other].Y, &S->v[L->base].Y, 2);
    int ub = ilogb(S->g);
    sd_from_big(&bx, &S->v[L->base].X, 2, ub); sd_from_big(&by, &S->v[L->base].Y, 2, ub);
    sd_from_big(&ddx, &bdx, 2, ub); sd_from_big(&ddy, &bdy, 2, ub);
    sd_mul(&t, &bx, &W); sd_sub(&u1, &X, &t);
    sd_mul(&t, &by, &W); sd_sub(&u2, &Y, &t);
    PcSD a, b;
    sd_mul(&a, &u1, &ddy); sd_mul(&b, &u2, &ddx);
    sd_sub(&r, &a, &b);
    return sd_sign(&r);
}
/* FUN_180022260: sign of P[axis] - vertex[axis] */
static int tp_cmp(IfkS2 *S, const double *P, int vi, int ax, IfkS2Exact exact, void *user)
{
    SV *V = &S->v[vi];
    double pv = ax ? P[2] : P[0], pe = ax ? P[3] : P[1];
    double vv = ax ? V->y : V->x, ve = ax ? V->ey : V->ex;
    double d = vv - pv;
    if ((ve + pe + fabs(d)) * U52 < fabs(d)) return (pv <= vv && vv != pv) ? -1 : 1;
    if (ve == 0.0 && pe == 0.0) return 0;
    S->ncheck++;   /* dialog slot +0x40: "IFSilhouette checkpoint 1" */
    PcSD X, Y, W, a, b, t;
    exact(user, &X, &Y, &W);
    sv_exact(S, vi); V = &S->v[vi];
    int ub = ilogb(S->g);
    if (V->sa < 0) {
        sd_from_big(&t, ax ? &V->Y : &V->X, 2, ub);
        sd_mul(&b, &t, &W);
        a = ax ? Y : X;
    } else {
        PcSD D; sd_from_big(&D, &V->D, 4, 2 * ub);
        sd_mul(&a, ax ? &Y : &X, &D);
        sd_from_big(&t, ax ? &V->Y : &V->X, 6, 3 * ub);
        sd_mul(&b, &t, &W);
    }
    PcSD r; sd_sub(&r, &a, &b);   /* FUN_18002c6e0(P, v) [inferred: sign of P - v, as the filtered branch] */
    return sd_sign(&r);
}
/* FUN_180021830: the test point lies on output edge ei (closed segment) */
static int tp_on_edge(IfkS2 *S, const double *P, int ei, IfkS2Exact exact, void *user)
{
    Line L; mkline(S, &L, ei);
    if (tp_orient(S, P, &L, exact, user) != 0) return 0;
    int ax = seg_axis(S, ei);
    if (ax < 0) ax = 0;   /* error 5 returned -1 (0xffffffff) in i11: not ported */
    int i3 = tp_cmp(S, P, S->e[ei].a, ax, exact, user);
    if (i3 == 0) return 1;
    int i5 = tp_cmp(S, P, S->e[ei].b, ax, exact, user);
    return i5 == 0 || i3 != i5;
}
int ifk_s2_point_on_output(IfkS2 *S, const double *P, IfkS2Exact exact, void *user)
{
    for (int k = S->out.n - 1; k >= 0; k--)   /* +0x40 from its head */
        if (tp_on_edge(S, P, S->out.v[k], exact, user)) return 1;
    return 0;
}
const char *ifk_s2_error(IfkS2 *S) { return S->err[0] ? S->err : NULL; }
int ifk_s2_checkpoints(IfkS2 *S) { return S->ncheck; }
int ifk_s2_noutput(IfkS2 *S) { return S->out.n; }
void ifk_s2_output_edge(IfkS2 *S, int k, double xy[4])
{
    int e = S->out.v[S->out.n - 1 - k];
    xy[0] = S->v[S->e[e].a].x; xy[1] = S->v[S->e[e].a].y; xy[2] = S->v[S->e[e].b].x; xy[3] = S->v[S->e[e].b].y;
}
