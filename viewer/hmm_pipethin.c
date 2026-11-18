/* hmm_pipethin.c - port of mchmm.dll's pass writer CPipeThinToolpath (vftable at the writer +0, ctor FUN_180118410) and its flush
 * FUN_18011ca00 into TGridC pass records (formats/hst3d_port_plan.md 7 "W6"). Sources: decomp/hst3d_weave_drop.c ("hw", Ghidra) and
 * the disassembly of mchmm.dll (objdump -d -M intel, "dis ADDR") where Ghidra drops arguments. Thinning: template mode 1 (lines,
 * FUN_180121f80) and mode 2 (lines and arcs, FUN_180122380 with the arc fit FUN_18011f6a0; Waterline's default, plan 7 "W7").
 *
 * Layout in the DLL (kept here as C structs, compared through the records only):
 *  writer +8 / +0x10 vector of curves (0x58 bytes: the 0x48-byte template + +0x48 std::list of composites, +0x50 its size),
 *         +0x20 the minimum arc chord (FUN_180406b00: 0x3f33a92a30553261), +0x28 / +0x70 the templates, +0xb8 / +0xc0 / +0xc8 /
 *         +0xd0 the points header (FUN_18011ca00; +0xd0 = the level z), +0xd8 vector<int> of the start kinds, +0xf0 the index of
 *         the kind-0x1e curve, +0xf8 the pen (x, y, z), +0x110 / +0x111 the start / last flag, +0x112 / +0x113 the flag rule,
 *         +0x114 PosContactOnly, +0x115, +0x170 the count of flag changes, +0x178 the near-full-arc flag.
 *  composite (list node payload +0x10): an element (0xb8 bytes: +0 kind 0 open / 1 line / 2 arc / 3 / 5 marker, +4 flag 1 / -1 /
 *         0, +8 the points TArray (line: start, end), +0x20 the arc sweep, +0x38 / +0x40 centre, +0x48 radius, +0x50 / +0x58 start,
 *         +0x60 / +0x68 end, +0x70 direction byte, +0x78 / +0x80 start / end z) + +0x110 the list of its sub-elements, +0x118 size.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "hmm_pipethin.h"

typedef struct {
    int kind, flag;
    double p[3][3]; int np;      /* the points TArray (+8): line start / end; kind 3 three points */
    double sweep;                /* +0x20 (FUN_180051d20, 0 when start == end) */
    double c[2], r, s[2], e[2];  /* +0x38 .. +0x68 */
    int dir;                     /* +0x70 */
    double zs, ze;               /* +0x78 / +0x80 */
    double len2;                 /* +0x30 the squared length, -1 until FUN_180116600 fills it */
} HptEl;
/* a composite: its element, the deviations FUN_18011f370 writes (+0xb8 .. +0xf8: the farthest sub-element point from the chord
   and where, the same for an arc chord, the smallest |cos| and where; "where" = sub index + parameter 0 / 1), its sub-elements */
typedef struct {
    HptEl el;
    double dev, mincos, adev; int dev_k, adev_k, cos_k; double dev_t, adev_t, cos_t;
    HptEl *sub; int nsub, csub;
} HptComp;
typedef struct { HptTemplate t; HptComp *c; int nc, cc; } HptCurve;
struct HptWriter {
    HptTemplate t;
    HptCurve *cv; int ncv, ccv;
    int *kinds; int nk, ck;
    double minchord;   /* +0x20 */
    double z;          /* +0xd0 */
    long f0;           /* +0xf0 */
    double pen[3];     /* +0xf8 */
    int f110, f111, f112, f113, f114, f115;
    int f170, f178;
};

/* coverage counters of the arc thinning (hpt_stats) */
static long g_st[16];
#define GROW(p, n, c) do { if ((n) >= (c)) { (c) = (c) ? 2 * (c) : 8; (p) = realloc((p), sizeof *(p) * (size_t)(c)); } } while (0)

static void el_start(const HptEl *e, double *o)
{ if (e->kind == 2) { o[0] = e->s[0]; o[1] = e->s[1]; o[2] = e->zs; } else memcpy(o, e->p[0], sizeof e->p[0]); }
static void el_end(const HptEl *e, double *o)
{
    if (e->kind == 2) { o[0] = e->e[0]; o[1] = e->e[1]; o[2] = e->ze; }
    else memcpy(o, e->p[e->kind == 3 ? 2 : 1], sizeof e->p[0]);
}
/* FUN_180115e20(e, kind): an empty element, flag 1 (dis 180115e3a-180115e3c) */
static void el_kind(HptEl *e, int kind) { memset(e, 0, sizeof *e); e->kind = kind; e->flag = 1; e->len2 = -1.0; }
/* FUN_180115ed0: a line a -> b */
static void el_line(HptEl *e, const double *a, const double *b, int flag)
{ el_kind(e, 1); e->flag = flag; memcpy(e->p[0], a, 24); memcpy(e->p[1], b, 24); e->np = 2; }
/* FUN_180115fb0(e, start, centre, end, dir, flag) (hw l.3445; dis 180115fb0-1801161a0): an arc, or the line start -> end when
   either end lies within sqrt(5e-5) of the centre, measured in float (cvtpd2ps / subss / mulss / addss, the threshold the double
   0x1807bb3e0 = 5e-05) */
static void el_arc(HptEl *e, const double *a, const double *c, const double *b, int dir, int flag)
{
    el_kind(e, 2); e->flag = flag;
    double dy = a[1] - c[1], dx = a[0] - c[0];
    e->c[0] = c[0]; e->c[1] = c[1]; e->r = sqrt(dx * dx + dy * dy);
    e->s[0] = a[0]; e->s[1] = a[1]; e->e[0] = b[0]; e->e[1] = b[1]; e->dir = dir;
    float fy = (float)a[1] - (float)c[1], fx = (float)a[0] - (float)c[0], gy = (float)b[1] - (float)c[1], gx = (float)b[0] - (float)c[0];
    float s1 = fx * fx + fy * fy, s2 = gx * gx + gy * gy;
    if ((double)s1 < 5e-05 || (double)s2 < 5e-05) { el_line(e, a, b, flag); return; }
    e->zs = a[2]; e->ze = b[2];
    e->sweep = (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) ? 0.0 : hv_arc_sweep(e->c, e->r, e->s, e->e, e->dir);   /* FUN_180051d20 */
}

static HptCurve *last_curve(HptWriter *w) { return &w->cv[w->ncv - 1]; }
static HptComp *comp_push(HptCurve *cv, int kind)
{ GROW(cv->c, cv->nc, cv->cc); HptComp *k = &cv->c[cv->nc++]; memset(k, 0, sizeof *k); el_kind(&k->el, kind); return k; }
static void comp_pop(HptCurve *cv) { free(cv->c[cv->nc - 1].sub); cv->nc--; }

/* FUN_180121820 (hw l.11823): the element goes into the last composite's sub-list; template mode 0 makes it the composite and
   opens an empty one (FUN_18011e4a0(.., 0) + FUN_180125030) */
static void curve_add(HptCurve *cv, const HptEl *e)
{
    HptComp *k = &cv->c[cv->nc - 1];
    GROW(k->sub, k->nsub, k->csub); k->sub[k->nsub++] = *e;
    if (cv->t.mode == 0) { k->el = k->sub[0]; comp_push(cv, 0); }
}

/* the element flag of FUN_1801194d0 / FUN_18011a730: with +0x112 a change of flag takes the new one; else +0x113 ? last && new :
   last || new (hw l.5935-5958, dis 18011953b-18011956c) */
static int el_flag(const HptWriter *w, int nf)
{
    int lf = w->f111, v;
    if (w->f112 && lf != nf) v = nf;
    else v = w->f113 ? (lf && nf) : (lf || nf);
    return v ? 1 : -1;
}

/* FUN_1801192a0 (hw l.5824): a new curve from the template (kind 0x20 the second one, not used here), its first composite of kind
   5, the pen at p with the writer's z (+0x108 = +0xd0) */
static void w_start(void *ctx, const double *p, int flag, int kind)
{
    HptWriter *w = ctx;
    if (kind == 0x1e) w->f0 = w->nk;
    GROW(w->kinds, w->nk, w->ck); w->kinds[w->nk++] = kind;
    GROW(w->cv, w->ncv, w->ccv); HptCurve *cv = &w->cv[w->ncv++]; memset(cv, 0, sizeof *cv); cv->t = w->t;
    comp_push(cv, 5);
    w->f115 = 0; w->pen[0] = p[0]; w->pen[1] = p[1]; w->pen[2] = w->z;
    w->f110 = w->f111 = flag != 0;
}
/* FUN_1801194d0 (hw l.5926): a line to p (z = the writer's: FUN_180557f60 passes +0xd0, dis 180558c2d); nothing when p equals the pen
   exactly (FUN_18002d030, dis 18002d030: x, y, z ==), the flag then not updated */
static void line_to(HptWriter *w, const double *p3, int flag)
{
    flag = flag != 0;
    if (flag != w->f111) w->f170++;
    if (p3[0] == w->pen[0] && p3[1] == w->pen[1] && p3[2] == w->pen[2]) return;
    HptEl e; el_line(&e, w->pen, p3, el_flag(w, flag));
    curve_add(last_curve(w), &e);
    memcpy(w->pen, p3, 24); w->f111 = flag;
}
static void w_line(void *ctx, const double *p, int flag)
{ HptWriter *w = ctx; double p3[3] = { p[0], p[1], w->z }; line_to(w, p3, flag); }
/* FUN_18011a730 (hw l.6656, dis 180119861-1801198a1 for its arguments): an arc element pen -> p about c */
static void arc_to(HptWriter *w, const double *p3, const double *c, int dir, int flag)
{
    flag = flag != 0;
    if (flag != w->f111) w->f170++;
    HptEl e; el_arc(&e, w->pen, c, p3, dir, el_flag(w, flag));
    curve_add(last_curve(w), &e);
    memcpy(w->pen, p3, 24); w->f111 = flag;
}
/* FUN_18004d7e0 (dis 18004d7e0-18004d87e): the pseudo-angle of (a, b) in [0, 4) */
static double pseudo(double a, double b)
{
    if (a == 0.0 && b == 0.0) return 0.0;
    if (b >= 0.0) return a >= 0.0 ? b / (b + a) : 1.0 - a / (b - a);
    if (a < 0.0) return 2.0 - b / (-a - b);
    double r = a / (a - b) + 3.0;
    return r == 4.0 ? 0.0 : r;
}
/* FUN_1801195f0 (hw l.5980, dis 1801195f0-1801198c5): a line when the chord is below +0x20; else the sweep's pseudo-angle (3.9 or
   more without b6 sets +0x178), one arc up to 1.9, else two arcs through the point at radius r (r = -1: |p - c|) on the chord's
   perpendicular (z the mean of the ends' when they differ) */
static void w_arc(void *ctx, const double *p, const double *c, double r, int dir, int b6, int flag)
{
    HptWriter *w = ctx;
    double p3[3] = { p[0], p[1], w->z };
    double dy0 = w->pen[1] - p[1], dx0 = w->pen[0] - p[0];
    if (!(sqrt(dy0 * dy0 + dx0 * dx0) >= w->minchord)) { line_to(w, p3, flag); return; }
    double ux = w->pen[0] - c[0], uy = w->pen[1] - c[1], vx = p[0] - c[0], vy = p[1] - c[1];
    double wx = dir ? uy : -uy, wy = dir ? -ux : ux;
    double a = vy * uy + vx * ux, b = vy * wy + vx * wx;
    double s = pseudo(a, b);
    if (s >= 3.9 && !b6) w->f178 = 1;
    if (s <= 1.9) { arc_to(w, p3, c, dir, flag); return; }
    double dx = p[0] - w->pen[0], dy = p[1] - w->pen[1];
    if (r == -1.0) r = sqrt(vy * vy + vx * vx);
    double nx = dir ? -dy : dy, ny = dir ? dx : -dx;
    double k = r / sqrt(dy * dy + dx * dx);
    double m[3] = { k * nx + c[0], k * ny + c[1], w->pen[2] };
    if (m[2] != p3[2]) m[2] = (p3[2] + m[2]) * 0.5;
    arc_to(w, m, c, dir, flag);
    arc_to(w, p3, c, dir, flag);
}

/* FUN_180121900(curve, pen, +0x110, +0x111, +0x115) (hw l.11852; arguments dis 180558ff4-18055901f): the open composite dropped,
   an empty curve gets a zero line at the pen, the last composite closed (one sub-element: itself; more: the line from the first's
   start to the last's end with the last flag); template mode 0: a kind-5 marker at the end unless the curve closes (its last end
   == its first start, exactly: FUN_1800082c0 dis 1800082c0) with more than one composite and param_5 = 0 */
static int w_end_curve(HptWriter *w, HptCurve *cv, const double *pen, int p3, int p4, int p5)
{
    (void)p3;
    if (cv->c[cv->nc - 1].el.kind == 0) comp_pop(cv);
    if (cv->c[cv->nc - 1].nsub == 0) {
        HptEl e; el_line(&e, pen, pen, p4 ? 1 : -1);
        curve_add(cv, &e);
        if (cv->c[cv->nc - 1].el.kind == 0) comp_pop(cv);
    }
    HptComp *L = &cv->c[cv->nc - 1];
    if (L->nsub == 1) L->el = L->sub[0];
    else { double a[3], b[3]; el_start(&L->sub[0], a); el_end(&L->sub[L->nsub - 1], b); el_line(&L->el, a, b, p4 ? 1 : -1); }
    double a[3], b[3]; el_end(&cv->c[cv->nc - 1].el, b); el_start(&cv->c[0].el, a);
    int closes = a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
    /* template mode != 0: a closed curve ends here; where its first and last sub-elements' flags differ FUN_180121eb0 adds an
       attribute point (FUN_1801166d0: +0x88 / +0xa0, not read by the records) */
    if (cv->t.mode != 0) { if (closes) return 1; }
    else if (closes && cv->nc != 1 && !p5) return 1;
    comp_push(cv, 5);
    (void)w;
    return 1;
}
/* ------------------------------------------------------------------ the thinning FUN_18011d020 (template mode != 0) */
static double g_rad170;   /* FUN_1802e73a0(170.0) = (2 pi x 1/360) x 170 (2 pi = the static 0x1809bf198, 0x1807bb478, 0x1807bb898) */
static int g_thin_bad;    /* 1: a split the DLL would make through its list head (not reproducible); 2: NOT DECODED */

/* FUN_180116600 with the +0x30 cache (FUN_18011e460 / FUN_18011edd0 test it) */
static double el_len2(HptEl *e)
{
    if (e->len2 >= 0.0) return e->len2;
    double v;
    if (e->kind == 1) {
        v = (e->p[0][1] - e->p[1][1]) * (e->p[0][1] - e->p[1][1]) + (e->p[0][0] - e->p[1][0]) * (e->p[0][0] - e->p[1][0]) +
            (e->p[0][2] - e->p[1][2]) * (e->p[0][2] - e->p[1][2]);
    } else if (e->kind == 2) {
        double l = e->r * e->sweep, dz = e->zs - e->ze;
        if (0.0 < l && fabs(dz) > 0.0) {
            double q = fabs(dz) * fabs(dz) + l * l, L = 0.0;
            if (0.0 < q) L = sqrt(q);
            double m = (L / l) * l; v = m * m;
        } else v = l * 1.0 * l * 1.0;
    } else if (e->kind == 3) v = e->r * e->sweep * e->r * e->sweep;
    else v = -1.0;
    e->len2 = v;
    return v;
}
/* FUN_180116b40: the point at parameter t (0 the start, 1 FUN_180117c40 the end; a line FUN_18005aa50 (dis 18005aa50: the start
   when both ends are equal, else (1 - t) a + t b); an arc FUN_180051e00 at t, z interpolated when the ends' differ) */
static void el_point(const HptEl *e, double t, double *o)
{
    if (t == 0.0) { el_start(e, o); return; }
    if (t == 1.0) { el_end(e, o); return; }
    if (e->kind == 1) {
        const double *a = e->p[0], *b = e->p[1];
        if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) { memcpy(o, a, 24); return; }
        double u = 1.0 - t;
        o[0] = u * a[0] + t * b[0]; o[1] = u * a[1] + t * b[1]; o[2] = u * a[2] + t * b[2];
        return;
    }
    if (e->kind == 2) {
        double z = e->zs;
        if (z != e->ze) z = (1.0 - t) * z + e->ze * t;
        hv_arc_point(o, e->c, e->r, e->s, e->e, e->dir, t); o[2] = z;
        return;
    }
    o[0] = o[1] = o[2] = 0.0;
}
/* FUN_180116770: e split at the point q into e (start -> q) and o (q -> end), both rebuilt by FUN_180115ed0 / FUN_180115fb0 with
   e's flag (the attribute points +0x88 are shared out by side, not read by the records) */
static void el_split(HptEl *e, HptEl *o, const double *q)
{
    HptEl a = *e;
    if (a.kind == 1) { el_line(e, a.p[0], q, a.flag); el_line(o, q, a.p[1], a.flag); }
    else if (a.kind == 2) {
        double s3[3] = { a.s[0], a.s[1], a.zs }, e3[3] = { a.e[0], a.e[1], a.ze };
        el_arc(e, s3, a.c, q, a.dir, a.flag); el_arc(o, q, a.c, e3, a.dir, a.flag);
    }
}
static void comp_insert(HptCurve *cv, int at, int kind)   /* a new composite at index at (FUN_18011df80 / FUN_18011df10) */
{
    GROW(cv->c, cv->nc, cv->cc);
    memmove(&cv->c[at + 1], &cv->c[at], sizeof *cv->c * (size_t)(cv->nc - at)); cv->nc++;
    HptComp *k = &cv->c[at]; memset(k, 0, sizeof *k); el_kind(&k->el, kind);
}
static void sub_insert(HptComp *k, int at, const HptEl *e)
{
    GROW(k->sub, k->nsub, k->csub);
    memmove(&k->sub[at + 1], &k->sub[at], sizeof *k->sub * (size_t)(k->nsub - at)); k->nsub++; k->sub[at] = *e;
}
/* a composite's element from its sub-elements: one - a copy (FUN_180116d80); more - the line from the first's start to the last's
   end with the given flag (FUN_18011e550 / FUN_180120e60 / FUN_180121900) */
static void comp_chord(HptComp *k, int flag)
{
    if (k->nsub == 1) { k->el = k->sub[0]; return; }
    double a[3], b[3]; el_start(&k->sub[0], a); el_end(&k->sub[k->nsub - 1], b); el_line(&k->el, a, b, flag);
}
static int comp_common_flag(const HptComp *k)   /* FUN_180124f40 / FUN_180120e60: the sub-elements' flag, 0 when mixed, -1 none */
{
    if (!k->nsub) return -1;
    int f = k->sub[0].flag;
    for (int i = 1; i < k->nsub; i++) if (k->sub[i].flag != f) return 0;
    return f;
}
/* the sub-elements from j on move to a new composite after ki (FUN_18011e550 with FUN_180120d20's splice) */
static void comp_split_at(HptCurve *cv, int ki, int j)
{
    comp_insert(cv, ki + 1, 0);
    HptComp *k = &cv->c[ki], *n = &cv->c[ki + 1];
    int m = k->nsub - j;
    n->sub = malloc(sizeof *n->sub * (size_t)m); n->csub = m; n->nsub = m;
    memcpy(n->sub, &k->sub[j], sizeof *n->sub * (size_t)m); k->nsub = j;
}

/* FUN_180120e60(curve, PosContactOnly) (hw l.11295): a composite whose sub-elements turn from the first one's direction (a negative
   dot, or dot^2 < |d|^2 |d0|^2 s^2 with s = the template +0x40 = sin(StraightLineAngleTolerance), both lengths^2 above 1e-12), or
   (PosContactOnly) change flag, is split there; both parts get their chord and the common flag */
static void thin_straight(HptCurve *cv, int pco)
{
    double sn = cv->t.ssin;
    for (int i = 0; i < cv->nc; i++) {
        HptComp *C = &cv->c[i];
        if (C->el.kind == 5 || C->nsub < 1) continue;
        const HptEl *f = &C->sub[0];
        double P0[3], P1[3]; el_start(f, P0); el_end(f, P1);
        double d0z = P1[2] - P0[2], d0y = P1[1] - P0[1], d0x = P1[0] - P0[0];
        double L0 = d0x * d0x + d0y * d0y + d0z * d0z;
        int j = 1;
        while (j < C->nsub && (!pco || C->sub[j].flag == f->flag)) {
            double a[3], b[3]; el_start(&C->sub[j], a); el_end(&C->sub[j], b);
            double dz = b[2] - a[2], dy = b[1] - a[1], dx = b[0] - a[0];
            double dot = dx * d0x + dy * d0y + dz * d0z;
            if (dot < 0.0) break;
            if (1e-12 < L0) { double Ld = dx * dx + dy * dy + dz * dz; if (1e-12 < Ld && dot * dot < Ld * L0 * sn * sn) break; }
            j++;
        }
        if (j >= C->nsub) continue;
        comp_split_at(cv, i, j);
        C = &cv->c[i]; HptComp *N = &cv->c[i + 1];
        if (N->nsub == 1) N->el = N->sub[0]; else { comp_chord(N, 1); N->el.flag = comp_common_flag(N); }
        if (C->nsub == 1) C->el = C->sub[0]; else { comp_chord(C, 1); C->el.flag = comp_common_flag(C); }
    }
}

typedef struct { int k; double t; } HptSpl;   /* a split: before sub-element k (k = nsub: none), or inside it at t */
/* FUN_18011edd0(k, &target, &tol) (hw l.9719): the sub-element where the length along the composite reaches target, at t =
   target / its length; the split moves to the next one (t 0) when the point lies within tol of its end (or t = 1), to the start
   of this one when within tol of its start; arcs measure those two by arc length when beyond tol */
static HptSpl split_len(HptComp *k, double *target, const double *tol)
{
    double tt = *tol * *tol;
    for (int i = 0; i < k->nsub; i++) {
        HptEl *e = &k->sub[i];
        double L = sqrt(el_len2(e)), g = *target;
        if (g < L) {
            g = g / L;
            if (0.0 <= g) { if (1.0 < g) g = 1.0; } else g = 0.0;
            double P[3]; el_point(e, g, P);
            double da, db;
            if (e->kind == 1) {
                double a[3], b[3]; el_start(e, a); el_end(e, b);
                double dz = a[2] - P[2];
                da = (a[1] - P[1]) * (a[1] - P[1]) + (a[0] - P[0]) * (a[0] - P[0]) + dz * dz;
                double ex = P[0] - b[0];
                db = (P[1] - b[1]) * (P[1] - b[1]) + ex * ex + (P[2] - b[2]) * (P[2] - b[2]);
            } else if (e->kind == 2) {
                double dz = e->zs - P[2];
                da = (e->s[1] - P[1]) * (e->s[1] - P[1]) + (e->s[0] - P[0]) * (e->s[0] - P[0]) + dz * dz;
                if (tt < da) {
                    double dy = e->s[1] - e->c[1], dx = e->s[0] - e->c[0], r = sqrt(dy * dy + dx * dx);
                    double sw = hv_arc_sweep(e->c, r, e->s, P, e->dir) * r; da = sw * sw;
                }
                db = (P[1] - e->e[1]) * (P[1] - e->e[1]) + (P[0] - e->e[0]) * (P[0] - e->e[0]) + (P[2] - e->ze) * (P[2] - e->ze);
                if (tt < db) {
                    double r = sqrt((P[1] - e->c[1]) * (P[1] - e->c[1]) + (P[0] - e->c[0]) * (P[0] - e->c[0]));
                    double sw = hv_arc_sweep(e->c, r, P, e->e, e->dir) * r; db = sw * sw;
                }
            } else { db = -1.0; da = -1.0; }
            HptSpl r;
            double t2 = *tol * *tol;
            if (g == 1.0 || db <= t2) r = (HptSpl){ i + 1, 0.0 };
            else if (t2 < da) r = (HptSpl){ i, g };
            else r = (HptSpl){ i, 0.0 };
            *target = 0.0;
            return r;
        }
        *target = g - L;
    }
    return (HptSpl){ k->nsub, 0.0 };
}
/* FUN_180058620 (dis 180058620-18005870f): squared distance of p from the segment a b in the plane */
static double seg2_d2(const double *p, const double *a, const double *b)
{
    double dy = b[1] - a[1], dx = b[0] - a[0];
    double L = dx * dx + dy * dy;
    double wx = p[0] - a[0], wy = p[1] - a[1];
    double t = L;
    if (L != 0.0) t = (wx * dx + wy * dy) / L;
    if (t >= 1.0) { double ey = p[1] - b[1], ex = p[0] - b[0]; return ey * ey + ex * ex; }
    if (t > 0.0) { wx = wx - dx * t; wy = wy - dy * t; }
    return wy * wy + wx * wx;
}
/* FUN_180117780 (hw l.4589): an arc sub-element's deviation from the chord ch = (x0, y0, x1, y1): the arc points on the chord's
   normal through the centre (FUN_180052110 their parameters), the farther from the chord of those inside the arc (FUN_180058620),
   else the farther arc end (by which ends lie beside the chord); a parameter within 3e-5 (9e-10 squared) of an end snaps to it */
static void arcchord_dev(const HptEl *e, const double *ch, double *d, double *t)
{
    double x0 = ch[0], y0 = ch[1], x1 = ch[2], y1 = ch[3], r = e->r;
    double dx = x1 - x0, ndy = -(y1 - y0);
    double L = sqrt(dx * dx + -(y1 - y0) * ndy); if (L == 0.0) L = 1.0;
    double P1[2] = { e->c[0] + (1.0 / L) * ndy * r, e->c[1] + (1.0 / L) * dx * r };
    double P2[2] = { e->c[0] - (1.0 / L) * ndy * r, e->c[1] - (1.0 / L) * dx * r };
    double f1 = hv_arc_param(e->c, e->r, e->s, e->e, e->dir, P1), f2 = hv_arc_param(e->c, e->r, e->s, e->e, e->dir, P2);
    int in1 = 0.0 < f1 && f1 < 1.0, in2 = 0.0 < f2 && f2 < 1.0;
    double D, T;
    g_st[6]++;
    if (in1 && in2) {
        double d6 = seg2_d2(P1, ch, ch + 2), d5 = seg2_d2(P2, ch, ch + 2);
        if (d6 <= d5) { D = d5; T = f2; } else { D = d6; T = f1; }
    } else if (in1) { D = seg2_d2(P1, ch, ch + 2); T = f1; }
    else if (in2) { D = seg2_d2(P2, ch, ch + 2); T = f2; }
    else {
        double a = (y1 - y0) * (e->s[1] - y0) + (x1 - x0) * (e->s[0] - x0);
        double b = (y1 - y0) * (e->e[1] - y0) + (x1 - x0) * (e->e[0] - x0);
        double c4 = (y1 - y0) * (e->s[1] - y1) + (x1 - x0) * (e->s[0] - x1);
        double c5 = (y1 - y0) * (e->e[1] - y1) + (x1 - x0) * (e->e[0] - x1);
        double d6 = seg2_d2(e->s, ch, ch + 2), d7 = seg2_d2(e->e, ch, ch + 2);
        g_st[7]++;
        int sin_ = 0.0 < a && 0.0 < -c4, eout = b <= 0.0 || -c5 <= 0.0;
        if ((a <= 0.0 && b <= 0.0) || (-c4 <= 0.0 && -c5 <= 0.0)) { *d = 0.0; *t = 0.0; }
        else if ((sin_ && eout) || ((eout || sin_) && d7 < d6)) { *d = d6; *t = 0.0; }
        else { *d = d7; *t = 1.0; }
        return;
    }
    if (0.0 < T && T < 1.0) {
        double P[2]; hv_arc_point(P, e->c, e->r, e->s, e->e, e->dir, T);
        if ((e->s[1] - P[1]) * (e->s[1] - P[1]) + (e->s[0] - P[0]) * (e->s[0] - P[0]) < 9e-10) T = 0.0;
        else if ((e->e[1] - P[1]) * (e->e[1] - P[1]) + (e->e[0] - P[0]) * (e->e[0] - P[0]) < 9e-10) T = 1.0;
    }
    *d = D; *t = T;
}
/* FUN_180116e10 (hw l.4085): a sub-element's squared deviation from the line chord ch (3D start, end): a line the larger of its
   ends' (FUN_18005ab30), at 1 when the end's is not smaller; a closed arc its start's; an arc FUN_180117780 on the chord's xy */
static void sub_dev(const HptEl *e, const double *ch, double *d, double *t)
{
    *d = 0.0; *t = 0.0;
    if (e->kind == 1) {
        double d1 = hv_seg_d2(e->p[0], ch, ch + 3), d2 = hv_seg_d2(e->p[1], ch, ch + 3);
        *d = d2 < d1 ? d1 : d2; *t = d1 <= d2 ? 1.0 : 0.0;
    } else if (e->kind == 2) {
        if (e->s[0] == e->e[0] && e->s[1] == e->e[1] && e->zs == e->ze) {
            double s3[3] = { e->s[0], e->s[1], e->zs }; *d = hv_seg_d2(s3, ch, ch + 3); *t = 0.0; return;
        }
        double c2[4] = { ch[0], ch[1], ch[3], ch[4] };
        arcchord_dev(e, c2, d, t);
    }
}
/* FUN_1800522c0 (dis 1800522c0-1800523ca): the parameter of p on the arc (FUN_180052110) and, within [0, 1], the squared distance
   of p from the circle; else the nearer arc end's squared distance at 0 / 1 */
static void arc_pdist(const HptEl *A, const double *p, double *f, double *d)
{
    double v = hv_arc_param(A->c, A->r, A->s, A->e, A->dir, p);
    if (0.0 <= v && v <= 1.0) {
        double q = sqrt((p[0] - A->c[0]) * (p[0] - A->c[0]) + (p[1] - A->c[1]) * (p[1] - A->c[1])) - A->r;
        *f = v; *d = q * q; return;
    }
    double d1 = (p[1] - A->s[1]) * (p[1] - A->s[1]) + (p[0] - A->s[0]) * (p[0] - A->s[0]);
    double d2 = (p[1] - A->e[1]) * (p[1] - A->e[1]) + (p[0] - A->e[0]) * (p[0] - A->e[0]);
    if (d1 >= d2) { *f = 1.0; *d = d2; } else { *f = 0.0; *d = d1; }
}
/* FUN_1801172b0 (hw l.4323): a sub-element's (kind 1 / 2) squared deviation from the composite's arc A: the larger of its ends'
   (FUN_1800522c0; at 1 when the end's is not smaller), or the circle's distance from its chord at the centre's foot when larger */
static void sub_adev(const HptEl *e, const HptEl *A, double *d, double *t)
{
    *d = 0.0; *t = 0.0;
    if (e->kind != 1 && e->kind != 2) return;
    double S[3], E[3], f1, d1, f2, d2; el_start(e, S); el_end(e, E);
    arc_pdist(A, S, &f1, &d1); arc_pdist(A, E, &f2, &d2);
    double D = d1 <= d2 ? d2 : d1, T = d1 <= d2 ? 1.0 : 0.0, dev = 0.0, u;
    double dx = E[0] - S[0], dy = E[1] - S[1], L = dy * dy + dx * dx;
    if (L == 0.0) u = -1.0;
    else {
        u = (dy * (A->c[1] - S[1]) + dx * (A->c[0] - S[0])) / L;
        if (0.0 <= u && u <= 1.0) {
            double qy = (A->c[1] - S[1]) - dy * u, qx = (A->c[0] - S[0]) - dx * u;
            dev = A->r - sqrt(qy * qy + qx * qx); dev = dev * dev;
        }
    }
    if (D <= dev && dev != D) { g_st[8]++; D = dev; T = u; }
    *d = D; *t = T;
}
/* FUN_180117550 (hw l.4466): cos^2 at each end of a sub-element (kind 1 / 2) between (end - arc start) and (arc end - end), 0 when
   either is zero; the larger, at 1 when the second's is not smaller */
static void sub_acos(const HptEl *e, const HptEl *A, double *c, double *t)
{
    *c = 0.0; *t = 0.0;
    if (e->kind != 1 && e->kind != 2) return;
    double S[3], E[3]; el_start(e, S); el_end(e, E);
    double v1x = S[0] - A->s[0], v1y = S[1] - A->s[1], n1 = v1y * v1y + v1x * v1x;
    double w1x = A->e[0] - S[0], w1y = A->e[1] - S[1], m1 = w1y * w1y + w1x * w1x, c1;
    if (n1 == 0.0 || m1 == 0.0) c1 = 0.0; else { c1 = w1y * v1y + w1x * v1x; c1 = (c1 * c1) / (m1 * n1); }
    double v2x = E[0] - A->s[0], v2y = E[1] - A->s[1], n2 = v2y * v2y + v2x * v2x;
    double w2y = A->e[1] - E[1], w2x = A->e[0] - E[0], m2 = w2y * w2y + w2x * w2x, c2;
    if (n2 == 0.0 || m2 == 0.0) c2 = 0.0; else { c2 = w2y * v2y + w2x * v2x; c2 = (c2 * c2) / (m2 * n2); }
    if (c1 <= c2) { *c = c2; *t = 1.0; } else { *c = c1; *t = 0.0; }
}
/* FUN_180116fa0 (kind 1 / 2): cos^2 of the angle at each end between (end - chord start) and (chord end - end); the larger, at 1
   when the second's is not smaller */
static void sub_cos(const HptEl *e, const double *ch, double *c, double *t)
{
    if (e->kind != 1 && e->kind != 2) { *c = 0.0; *t = 0.0; return; }
    double a[3], b[3]; el_start(e, a); el_end(e, b);
    double v1y = a[1] - ch[1], v1z = a[2] - ch[2], v1x = a[0] - ch[0];
    double n1 = v1y * v1y + v1x * v1x + v1z * v1z;
    double w1x = ch[3] - a[0], w1y = ch[4] - a[1], w1z = ch[5] - a[2];
    double m1 = w1y * w1y + w1x * w1x + w1z * w1z, c1;
    if (n1 == 0.0 || m1 == 0.0) c1 = 1.0; else { c1 = w1y * v1y + w1x * v1x + w1z * v1z; c1 = (c1 * c1) / (m1 * n1); }
    double v2y = b[1] - ch[1], v2z = b[2] - ch[2], v2x = b[0] - ch[0];
    double n2 = v2y * v2y + v2x * v2x + v2z * v2z;
    double w2y = ch[4] - b[1], w2x = ch[3] - b[0], w2z = ch[5] - b[2];
    double m2 = w2y * w2y + w2x * w2x + w2z * w2z, c2;
    if (n2 == 0.0 || m2 == 0.0) c2 = 1.0; else { c2 = w2y * v2y + w2x * v2x + w2z * v2z; c2 = (c2 * c2) / (m2 * n2); }
    if (c1 <= c2) { *c = c2; *t = 1.0; } else { *c = c1; *t = 0.0; }
}
/* FUN_18011f370 (hw l.9969): the composite's deviations over its sub-elements (strictly larger distance / smaller cos wins): a line
   composite FUN_180116e10 / FUN_180116fa0, an arc composite FUN_1801172b0 / FUN_180117550 against its arc (el +0x38) */
static int comp_devs(HptComp *k)
{
    k->dev_k = k->adev_k = k->cos_k = 0; k->dev_t = k->adev_t = k->cos_t = 0.0;
    if (k->nsub == 1) { k->dev = 0.0; k->adev = 0.0; k->mincos = 1.0; return 1; }
    double ch[6]; el_start(&k->el, ch); el_end(&k->el, ch + 3);
    double md = 0.0, ma = 0.0, mc = 1.0;
    for (int i = 0; i < k->nsub; i++) {
        double d = 0.0, td = 0.0, a = 0.0, ta = 0.0, c = 1.0, tc = 0.0;
        if (k->el.kind == 1) { sub_dev(&k->sub[i], ch, &d, &td); sub_cos(&k->sub[i], ch, &c, &tc); }
        else if (k->el.kind == 2) { sub_adev(&k->sub[i], &k->el, &a, &ta); sub_acos(&k->sub[i], &k->el, &c, &tc); }
        if (md <= d && d != md) { k->dev_t = td; k->dev_k = i; md = d; }
        if (ma <= a && a != ma) { k->adev_t = ta; k->adev_k = i; ma = a; }
        if (c < mc) { k->cos_t = tc; k->cos_k = i; mc = c; }
    }
    k->dev = sqrt(md); k->adev = sqrt(ma); k->mincos = sqrt(mc);
    return 1;
}
/* FUN_1801208d0(k, first, &cos) (hw l.10993): along the line sub-elements (after one of length != 0, kind 1 / 2) the cosine
   between successive unit directions (an arc: its end tangent); the first change between at least cos and below it marks the
   split (going below: at the later one; coming back: at the earlier one); k->nsub when none or fewer than 3 */
static int comp_straight(HptComp *k, double sc)
{
    if (k->nsub < 3) return k->nsub;
    HptEl *e = &k->sub[0];
    double ux, uy, uz;
    if (e->kind == 1) {
        double L2 = el_len2(e), a[3], b[3]; el_start(e, a); el_end(e, b);
        double q = 1.0 / sqrt(L2);
        ux = (b[0] - a[0]) * q; uy = (b[1] - a[1]) * q; uz = (b[2] - a[2]) * q;
    } else if (e->kind == 2) {
        double q = 1.0 / e->r;
        ux = q * -(e->e[1] - e->c[1]); uy = (e->e[0] - e->c[0]) * q; uz = (e->ze - e->zs) * q;
    } else return k->nsub;
    int first = 1; double pc = 0.0;
    for (int i = 1; i < k->nsub; i++) {
        HptEl *p = &k->sub[i - 1], *c = &k->sub[i];
        double L2p = el_len2(p);
        if (sqrt(L2p) != 0.0 && (p->kind == 1 || p->kind == 2) && c->kind == 1) {
            double L2 = el_len2(c), a[3], b[3]; el_start(c, a); el_end(c, b);
            double q = 1.0 / sqrt(L2);
            double vx = (b[0] - a[0]) * q, vz = (b[2] - a[2]) * q, vy = (b[1] - a[1]) * q;
            double cs = ux * vx + uy * vy + uz * vz;
            if (-1.0 <= cs) { if (1.0 < cs) cs = 1.0; } else cs = -1.0;
            if (!first) {
                if (cs < sc) { if (!(pc < sc)) return i; }
                else if (!(sc <= pc)) return i - 1;
            }
            pc = cs; first = 0; ux = vx; uy = vy; uz = vz;
        }
    }
    return k->nsub;
}
static HptSpl half_rule(const HptComp *k, int i, double t, int senab)   /* FUN_18011e8f0's StraightLineEnable rounding */
{
    if (!senab) return (HptSpl){ i, t };
    if (i == k->nsub - 1 && 0.5 < t) return (HptSpl){ i, t };
    if (i == 0) return t <= 0.5 ? (HptSpl){ i, t } : (HptSpl){ i, 1.0 };
    return (HptSpl){ i, 0.5 < t ? 1.0 : 0.0 };
}
/* FUN_18011e8f0(k, template, flag changes, kind-3 elements) (hw l.9498): where composite k is split next */
static HptSpl comp_fit(HptComp *k, const HptTemplate *T, int p4, int p5)
{
    int n = k->nsub;
    if (n == 0) return (HptSpl){ 0, 0.0 };
    if (T->mode == 0) return (HptSpl){ 0, 0.0 };
    if (n == 1) {
        if (k->el.kind != 2 || k->el.sweep <= g_rad170) return (HptSpl){ n, 0.0 };
        double half = sqrt(el_len2(&k->el)) * 0.5, z0 = 0.0;
        return split_len(k, &half, &z0);
    }
    if (p4) for (int i = 1; i < n; i++) if (k->sub[i].flag != k->sub[0].flag) return (HptSpl){ i, 0.0 };
    if (p5) for (int i = 0; i < n; i++) if (k->sub[i].kind == 3) return (HptSpl){ i, i == 0 ? 1.0 : 0.0 };
    if (T->mode == 1) for (int i = 0; i < n; i++) if (k->sub[i].kind == 2) return (HptSpl){ i, i == 0 ? 1.0 : 0.0 };
    double sm = T->smax;
    if (0.0 < sm && k->el.kind != 2) {
        double tol = sm * 0.0001, q = (sm - tol) * (sm - tol);
        if (q < el_len2(&k->el)) { double g = sm; HptSpl r = split_len(k, &g, &tol); if (r.k != n) return r; }
    }
    comp_devs(k);
    if (T->ctol < k->dev) return half_rule(k, k->dev_k, k->dev_t, T->senab);
    if (T->ctol < k->adev) return half_rule(k, k->adev_k, k->adev_t, T->senab);
    if (fabs(T->cang) <= k->mincos) {
        if (k->el.kind != 2 && T->senab) {
            int i = comp_straight(k, T->scos);
            if (i != n) return i != 0 ? (HptSpl){ i, 0.0 } : (HptSpl){ 0, 1.0 };
        }
        sm = T->smax;
        if (0.0 < sm) {
            double tol = sm * 0.0001, q = (sm - tol) * (sm - tol);
            if (q < el_len2(&k->el)) { double g = sm; return split_len(k, &g, &tol); }
        }
        if (k->el.kind == 2 && g_rad170 < k->el.sweep) {
            double half = sqrt(el_len2(&k->el)) * 0.5, z0 = 0.0;
            return split_len(k, &half, &z0);
        }
        return (HptSpl){ n, 0.0 };
    }
    return half_rule(k, k->cos_k, k->cos_t, T->senab);
}
/* FUN_180121f80 (mode 1, hw l.12161): composite ki split at comp_fit's points until none is left, the new parts processed in turn;
   returns the index of the composite that followed ki */
static int thin_lines(HptCurve *cv, int ki, int p4, int p5)
{
    int end = ki + 1, cur = ki;
    while (cur != end) {
        HptComp *k = &cv->c[cur];
        HptSpl s = comp_fit(k, &cv->t, p4, p5);
        if (g_thin_bad) return cv->nc;
        if (s.k == k->nsub) { cur++; continue; }
        int j = s.k;
        if (s.t != 0.0) {
            if (s.t != 1.0) {
                double q[3]; el_point(&k->sub[j], s.t, q);
                HptEl o; el_kind(&o, 0); el_split(&k->sub[j], &o, q);
                sub_insert(k, j + 1, &o);
            }
            j++;
        }
        if (j >= k->nsub || j <= 0) { g_thin_bad = 1; return cv->nc; }
        comp_split_at(cv, cur, j);
        comp_chord(&cv->c[cur + 1], 1); comp_chord(&cv->c[cur], 1);   /* FUN_18011e550 */
        end++;
    }
    return end;
}
/* ------------------------------------------------------------------ template mode 2 (ThinArcEnable): the arc fit */
/* FUN_180054820 (dis 180054820-180054c18): the intersection of the lines A -> B and C -> D as parameters (s along A B, t along
   C D); only t is used here. Parallel lines: the middle of the overlap of [0, 1] with C D's span on A B (dis 180054aa6-180054b93) */
static double isect_t(const double *A, const double *B, const double *C, const double *D)
{
    double wy = A[1] - C[1], wx = A[0] - C[0], ux = B[0] - A[0], uy = B[1] - A[1], vy = D[1] - C[1], vx = D[0] - C[0];
    double lv2 = vy * vy + vx * vx, lu2 = uy * uy + ux * ux;
    double wv = wy * vy + wx * vx;
    if (lv2 == 0.0) return 0.5;
    if (lu2 == 0.0) return wv / lv2;
    double lu = sqrt(lu2), lv = sqrt(lv2);
    double iu = 1.0 / lu, uhy = iu * uy, uhx = iu * ux;
    double iv = 1.0 / lv, vhy = iv * vy, vhx = iv * vx;
    double c = vhy * uhy + vhx * uhx, s2 = 1.0 - c * c;
    if (!(s2 >= 0.0)) s2 = 0.0;
    if (s2 == 0.0) {
        double il = 1.0 / lu;
        double p = ((C[1] - A[1]) * uhy + (C[0] - A[0]) * uhx) * il, q = ((D[1] - A[1]) * uhy + (D[0] - A[0]) * uhx) * il;
        double lo = p > q ? q : p, hi = p > q ? p : q, sv;
        if (hi < 0.0) sv = hi;
        else if (lo > 1.0) sv = lo;
        else {
            double b = hi <= 1.0 ? hi : 1.0, a = lo >= 0.0 ? lo : 0.0;
            sv = a == b ? a : (b + a) * 0.5;
        }
        return (sv - p) / (q - p);
    }
    double k = 1.0 / (s2 * lv);
    return (vhy - uhy * c) * k * wy + (vhx - uhx * c) * k * wx;
}
/* FUN_180051610(arc, A, B, C, tol) (dis 180051610-1800519fb): the arc A -> C through B. Radius -1 when two of the points lie
   within tol, when the turn at B is outside (0.5, 87.5) degrees from straight (cos(pi - FUN_1802e73a0(deg)) with MSVCR120's cos),
   when A B C are collinear, or when the diameter is below tol; the centre where the perpendicular bisectors meet (FUN_180054820),
   direction byte = the turn is counter-clockwise */
static double g_cmax, g_cmin;
static void arc3(HptEl *a, const double *A, const double *B, const double *C, double tol)
{
    memset(a, 0, sizeof *a); a->kind = 2;
    a->s[0] = A[0]; a->s[1] = A[1]; a->e[0] = C[0]; a->e[1] = C[1];
    double t2 = tol * tol;
    if ((A[0] - B[0]) * (A[0] - B[0]) + (A[1] - B[1]) * (A[1] - B[1]) < t2 ||
        (B[1] - C[1]) * (B[1] - C[1]) + (B[0] - C[0]) * (B[0] - C[0]) < t2 ||
        (A[1] - C[1]) * (A[1] - C[1]) + (A[0] - C[0]) * (A[0] - C[0]) < t2) { a->r = -1.0; return; }
    if (g_cmax == 0.0) {
        double pi = acos(-1.0), d2r = 2.0 * pi * 0.002777777777777778;
        g_cmax = hv_libm_cos(pi - d2r * 87.5); g_cmin = hv_libm_cos(pi - d2r * 0.5);
    }
    double cbx = C[0] - B[0], cby = C[1] - B[1], aby = B[1] - A[1], abx = B[0] - A[0];
    double dot = -cby * aby + -cbx * abx;
    double lbc = sqrt(cby * cby + cbx * cbx), lab = sqrt(aby * aby + abx * abx);
    double cv = dot / (lbc * lab);
    if (!(cv > g_cmin) || !(cv < g_cmax)) { a->r = -1.0; return; }
    double ncby = -cby, naby = -aby;
    double cr = aby * cbx + abx * ncby;
    if (cr == 0.0 || isnan(cr)) { a->r = -1.0; return; }
    a->dir = cr > 0.0;
    double M1[2], M2[2]; hv_lerp(M1, 0.5, A, B); hv_lerp(M2, 0.5, B, C);
    double P2[2] = { M2[0] + ncby, M2[1] + cbx }, P1[2] = { M1[0] + naby, M1[1] + abx };
    double t = isect_t(M1, P1, M2, P2);
    a->c[0] = ncby * t + M2[0]; a->c[1] = cbx * t + M2[1];
    double r = sqrt((A[1] - a->c[1]) * (A[1] - a->c[1]) + (A[0] - a->c[0]) * (A[0] - a->c[0]));
    a->r = r * 2.0 >= tol ? r : -1.0;
}
/* FUN_18011fbc0 (hw l.10402): the sub-elements' ends lie on one side of the composite's chord, ahead of its start: the first end
   off the start sets the side (its dot with the chord >= 0); every end but the last one's has 0 <= cos < 1 with the chord and
   (when off the start) the same side */
static int arc_side(HptComp *k)
{
    double S[3], E[3]; el_start(&k->el, S); el_end(&k->el, E);
    double dx = E[0] - S[0], dy = E[1] - S[1], qx = 0.0, qy = 0.0;
    for (int i = 0; i < k->nsub; i++) {
        double Q[3]; el_end(&k->sub[i], Q);
        qx = Q[0] - S[0]; qy = Q[1] - S[1]; double qz = Q[2] - S[2];
        if (!(qy * qy + qx * qx + qz * qz == 0.0)) break;
    }
    if (!(0.0 <= qx * dx + qy * dy)) return 0;
    double cross0 = qx * dy - qy * dx;
    double L = sqrt(dx * dx + dy * dy); if (L == 0.0) L = 1.0;
    double uy = (1.0 / L) * dy, ux = (1.0 / L) * dx;
    for (int i = 0; i < k->nsub - 1; i++) {
        double Q[3]; el_end(&k->sub[i], Q);
        double vx = Q[0] - S[0], vy = Q[1] - S[1], n2 = vy * vy + vx * vx, l = sqrt(n2);
        if (l == 0.0) l = 1.0;
        double c = (1.0 / l) * vy * uy + (1.0 / l) * vx * ux;
        if (c == 1.0 || c < 0.0) return 0;
        if (0.0 < (Q[2] - S[2]) * (Q[2] - S[2]) + n2 && ((vx * uy - vy * ux < 0.0) != (cross0 < 0.0))) return 0;
    }
    return 1;
}
/* FUN_18011ff30 (dis 18011ff30-1801202eb): an all-arc composite's centre: the foot of the sub-arcs' mean centre on the chord's
   perpendicular bisector (through the start when the chord is closed) */
static void arc_centre_mean(HptComp *k, double *C)
{
    double ax = 0.0, ay = 0.0; int n = 0;
    for (int i = 0; i < k->nsub; i++) {
        const HptEl *e = &k->sub[i];
        double px = e->kind == 2 ? e->c[0] : e->p[1][0], py = e->kind == 2 ? e->c[1] : e->p[1][1];
        n++; ax = px + ax; ay = py + ay;
    }
    double in = 1.0 / (double)n; ax = ax * in; ay = in * ay;
    double S[3], E[3]; el_start(&k->el, S); el_end(&k->el, E);
    double dx = E[0] - S[0], dy = E[1] - S[1];
    double bx = S[0], by = S[1];
    if (!(S[0] == E[0] && S[1] == E[1] && S[2] == E[2])) { bx = (S[0] + E[0]) * 0.5; by = (S[1] + E[1]) * 0.5; }
    double ndy = -dy, ny = (by + dx) - by, nx = (ndy + bx) - bx, n2 = ny * ny + nx * nx, t = 0.5;
    if (n2 != 0.0) t = (ny * (ay - by) + nx * (ax - bx)) / n2;
    C[1] = dx * t + by; C[0] = bx + ndy * t;
}
/* FUN_1801202f0(k, &centre, ThinCurveTolerance) (dis 1801202f0-1801208c2): per sub-element of non-zero length, the arc through the
   composite's start, its end and the composite's end (FUN_180051610, tol 0.3 = 0x1807bb598) bounds the radius so that the circle
   passes within ThinCurveTolerance of that end; the bounds intersected over the sub-elements (empty: no arc); the centre at the
   middle radius on the side of the first such arc's turn */
static int arc_centre_fit(HptComp *k, double ctol, double *C)
{
    double S[3], E[3]; el_start(&k->el, S); el_end(&k->el, E);
    double dx = E[0] - S[0], dy = E[1] - S[1], L2 = dy * dy + dx * dx, L = sqrt(L2), Ld = L == 0.0 ? 1.0 : L;
    double q = L2 * 0.25, inv = 1.0 / Ld, ux = dx * inv, uy = dy * inv;
    double M[2]; hv_lerp(M, 0.5, S, E);
    int have = 0, dir = 0; double lo = 0.0, hi = 0.0;
    for (int i = 0; i < k->nsub; i++) {
        HptEl *e = &k->sub[i];
        double Q[3]; el_end(e, Q);
        if (el_len2(e) == 0.0) continue;
        HptEl A; arc3(&A, S, Q, E, 0.3);
        if (!(A.r > 0.0)) continue;
        double Am[2]; hv_arc_point(Am, A.c, A.r, A.s, A.e, A.dir, 0.5);
        double proj = (Q[1] - S[1]) * uy + (Q[0] - S[0]) * ux;
        double h = sqrt((Am[1] - M[1]) * (Am[1] - M[1]) + (Am[0] - M[0]) * (Am[0] - M[0]));
        double py = (S[1] + uy * proj) - Q[1], px = (S[0] + ux * proj) - Q[0];
        double dist = sqrt(py * py + px * px), a = fabs(proj - L * 0.5), a2 = a * a;
        double dp = dist + ctol, dm = dist - ctol, dp2 = dp * 2.0;
        double l = ((q - dp * dp) + dp2 * h - a2) / dp2, u;
        if (dm > 0.0) { double dm2 = dm * 2.0; u = ((q - dm * dm) + dm2 * h - a2) / dm2; } else u = A.r * 2.0;
        if (!have) { dir = A.dir; lo = l; hi = u; have = 1; continue; }
        double nl = l > lo ? l : lo, nh = hi > u ? u : hi;
        if (nl > nh) return 0;
        lo = nl; hi = nh;
    }
    if (!have) return 0;
    double s = (hi + lo) * 0.5, s2 = s * s - q, kk = 0.0;
    if (s2 > 0.0) kk = sqrt(s2);
    if (dir) { C[0] = M[0] + uy * kk; C[1] = M[1] + -ux * kk; }
    else { C[0] = M[0] + -uy * kk; C[1] = M[1] + ux * kk; }
    return 1;
}
/* FUN_18011f6a0(k, template) (hw l.10143): the composite becomes an arc when its sub-elements (no kind 3) are planar, more than two
   lines or any arcs, all arcs of one direction: one arc alone is copied; else FUN_18011fbc0 and the centre (all arcs:
   FUN_18011ff30, else FUN_1801202f0), kept when the arc is not far beyond a half circle (end - centre against start - centre:
   dot > -0.97 r^2), its radius within ThinArcMaxRadius (when > 0) and its direction the sub-arcs' */
static int arc_fit(HptComp *k, const HptTemplate *T)
{
    double z0 = k->sub[0].kind == 2 ? k->sub[0].zs : k->sub[0].p[0][2], zmax = z0, zmin = z0;
    int nl = 0, na = 0, n3 = 0, nd1 = 0, nd0 = 0;
    for (int i = 0; i < k->nsub && n3 == 0; i++) {
        const HptEl *e = &k->sub[i];
        double z = e->kind == 2 ? e->zs : e->p[0][2];
        if (zmin <= z) { if (zmax < z) zmax = z; } else zmin = z;
        double ez = e->kind == 2 ? e->ze : e->p[e->kind == 3 ? 2 : 1][2];
        if (zmin <= ez) { if (zmax <= ez && ez != zmax) zmax = ez; } else zmin = ez;
        if (e->kind == 1) nl++;
        else if (e->kind == 2) {
            na++;
            if (zmin <= e->zs) { if (zmax < e->zs) zmax = e->zs; } else zmin = e->zs;
            if (e->dir) nd1++; else nd0++;
        } else if (e->kind == 3) n3++;
    }
    if (!(n3 < 1 && (2 < nl || na != 0) && zmax == zmin && (nd1 < 1 || nd0 < 1))) return 0;
    if (na == 1 && nl == 0) { g_st[0]++; k->el = k->sub[0]; return 1; }
    if (!arc_side(k)) { g_st[1]++; return 0; }
    double C[2];
    if (nl == 0 && 0 < na) { g_st[2]++; arc_centre_mean(k, C); }
    else if (!arc_centre_fit(k, T->ctol, C)) { g_st[3]++; return 0; }
    double S[3], E[3]; el_start(&k->el, S); el_end(&k->el, E);
    double ux = C[0] - S[0], uy = C[1] - S[1], vx = C[0] - E[0], vy = C[1] - E[1];
    double r2 = ux * ux + uy * uy;
    double cross = vy * ux - uy * vx;
    if (-(r2 * 0.97) < vx * ux + vy * uy && (T->amax <= 0.0 || sqrt(r2) <= T->amax) && (na < 1 || ((0 < nd1) == (cross < 0.0)))) {
        HptEl a; el_arc(&a, S, C, E, cross < 0.0, k->el.flag);
        g_st[4]++; k->el = a; return 1;
    }
    g_st[5]++; return 0;
}
void hpt_stats(void) { fprintf(stderr, "HPT copy %ld side-fail %ld mean %ld fit-fail %ld arc %ld arc-refused %ld | arcchord in %ld ends %ld | adev %ld\n", g_st[0], g_st[1], g_st[2], g_st[3], g_st[4], g_st[5], g_st[6], g_st[7], g_st[8]); }
/* FUN_180122380 (mode 2, hw l.12369): the split of FUN_18011e8f0; when there is one and the arc fit succeeds, FUN_18011e8f0 again
   on the arc: no split - the composite stays an arc; else the split is the first one (dis 180122437-180122497); the part after it
   a new composite processed in turn */
static int thin_arcs(HptCurve *cv, int ki, int p4, int p5)
{
    int end = ki + 1, cur = ki;
    while (cur != end) {
        HptComp *k = &cv->c[cur];
        HptSpl s = comp_fit(k, &cv->t, p4, p5);
        if (g_thin_bad) return cv->nc;
        if (s.k == k->nsub) { cur++; continue; }
        if (arc_fit(k, &cv->t)) {
            HptSpl s2 = comp_fit(k, &cv->t, p4, p5);
            if (g_thin_bad) return cv->nc;
            if (s2.k == k->nsub) { cur++; continue; }
        }
        int j = s.k;
        if (0.0 < s.t) {
            if (s.t != 1.0) {
                double q[3]; el_point(&k->sub[j], s.t, q);
                HptEl o; el_kind(&o, 0); el_split(&k->sub[j], &o, q);
                sub_insert(k, j + 1, &o);
            }
            j++;
        }
        if (j == k->nsub) { cur++; continue; }
        if (j <= 0) { g_thin_bad = 1; return cv->nc; }
        comp_split_at(cv, cur, j);
        comp_chord(&cv->c[cur + 1], 1); comp_chord(&cv->c[cur], 1);   /* FUN_18011e550 */
        end++;
    }
    return end;
}
/* FUN_18011d020 per curve: FUN_180120e60, then FUN_180124f40 (mode 1 FUN_180121f80, modes 0 / 2 FUN_180122380) and every
   composite's flag = its sub-elements' common one */
static int thin_curve(HptCurve *cv, int pco, int p4, int p5)
{
    thin_straight(cv, pco);
    for (int i = 0; i < cv->nc;) {
        if (cv->c[i].el.kind == 5) { i++; continue; }
        i = cv->t.mode == 1 ? thin_lines(cv, i, p4, p5) : thin_arcs(cv, i, p4, p5);
        if (g_thin_bad) return 0;
    }
    for (int i = 0; i < cv->nc; i++) cv->c[i].el.flag = comp_common_flag(&cv->c[i]);
    return 1;
}

/* ------------------------------------------------------------------ the intersection repair FUN_18011ce60 (FUN_180407c10's fallback, hzp l.263-546)
   hw = decomp/hst3d_weave_drop.c, dis = objdump of mchmm.dll. A composite stands for its chord (its element: a line from its first sub-element's start
   to its last one's end once the thinning has run); the repair splits composites whose chords cross so that the next records follow the sub-elements
   more closely. */
/* FUN_1800566e0(A, &s, B) (dis 1800566e0-180056867): B's ends strictly on different sides of the line A0 A1 (f(P) = dx Py + (-dy) Px against
   f(A0), "above" = strictly greater), the crossing Q = B0 + t (B1 - B0) by FUN_18000b4e0 (dis 18000b4e0: B0 when B0 == B1 exactly, else
   (B1x t + B0x (1 - t), (1 - t) B0y + t B1y)), s = its projection onto A ((Qy - A0y) dy + dx (Qx - A0x)) / (dy^2 + dx^2) (0.5 for a point A) */
static int isect_side(const double *A, const double *B, double *s)
{
    double dx = A[2] - A[0], ndy = -(A[3] - A[1]);
    double c = A[1] * dx + A[0] * ndy;
    double f1 = dx * B[3] + ndy * B[2], f0 = dx * B[1] + ndy * B[0];
    if ((f1 > c) == (f0 > c)) return 0;
    double t = (f0 - c) / (f0 - f1), Q[2];
    if (B[0] == B[2] && B[1] == B[3]) { Q[0] = B[0]; Q[1] = B[1]; }
    else { double u = 1.0 - t; Q[0] = B[2] * t + B[0] * u; Q[1] = u * B[1] + t * B[3]; }
    double ey = A[3] - A[1], ex = A[2] - A[0], L = ey * ey + ex * ex;
    if (L == 0.0) { *s = 0.5; return 1; }
    *s = (ey * (Q[1] - A[1]) + ex * (Q[0] - A[0])) / L;
    return 1;
}
/* FUN_180122710(A, B) (dis 180122710-1801229ec): the composites' chords. The box test as the DLL codes it (it is not a plain overlap test):
   rejected when (maxAx < minBx and (maxAy < minAy or maxBy < minAy)) and (maxBx < minAx and (maxAy < minBy or maxBy < minAy)), the first
   clause only deciding when it fails over to the second (dis 180122861-18012288b); then a zero-length A is no crossing; else FUN_1800566e0
   with 0 <= s <= 1 (0x1807bb718 = 1.0) */
static int comp_isect(const HptComp *a, const HptComp *b)
{
    double A0[3], A1[3], B0[3], B1[3]; el_start(&a->el, A0); el_end(&a->el, A1); el_start(&b->el, B0); el_end(&b->el, B1);
    double aminx = A0[0] > A1[0] ? A1[0] : A0[0], amaxx = A0[0] > A1[0] ? A0[0] : A1[0];
    double aminy = A0[1] > A1[1] ? A1[1] : A0[1], amaxy = A0[1] > A1[1] ? A0[1] : A1[1];
    double bminx = B0[0] > B1[0] ? B1[0] : B0[0], bmaxx = B0[0] > B1[0] ? B0[0] : B1[0];
    double bminy = B0[1] > B1[1] ? B1[1] : B0[1], bmaxy = B0[1] > B1[1] ? B0[1] : B1[1];
    if (!(amaxx >= bminx)) { if (amaxy < aminy || bmaxy < aminy) return 0; }
    if (!(bmaxx >= aminx)) { if (amaxy < bminy) return 0; if (!(bmaxy >= aminy)) return 0; }
    double Aq[4] = { A0[0], A0[1], A1[0], A1[1] }, Bq[4] = { B0[0], B0[1], B1[0], B1[1] };
    double ey = Aq[3] - Aq[1], ex = Aq[2] - Aq[0];
    if (ey * ey + ex * ex == 0.0) return 0;
    double s;
    if (!isect_side(Aq, Bq, &s)) return 0;
    return 0.0 <= s && s <= 1.0;
}
/* FUN_180122a00(curve, first, &k, &P) (hw l.12701; arguments dis 180123c20-180123c2f): over the composites c (from the second) and every
   composite b before c's predecessor (the pair last / first left out): the first c crossing some b becomes the candidate - unless c's
   successor (the first after the last) crosses that b as well: 2 with k = b and P = c's end; a second b crossing the candidate: 2 with
   k = c and P = b's start. Returns 1 with the candidate (no second crossing), 0 when none */
static int curve_cross(const HptCurve *cv, int *k, double *P)
{
    int cand = -1, n = cv->nc;
    for (int i = 1; i < n; i++)
        for (int j = 0; j < i - 1; j++) {
            if (i == n - 1 && j == 0) continue;
            if (!comp_isect(&cv->c[i], &cv->c[j])) continue;
            if (cand < 0) {
                int nx = i + 1 == n ? 0 : i + 1;
                int r = comp_isect(&cv->c[nx], &cv->c[j]);
                cand = i;
                if (r) { el_end(&cv->c[i].el, P); *k = j; return 2; }
            } else if (cand == i) { el_start(&cv->c[j].el, P); *k = i; return 2; }
        }
    *k = cand;
    return cand >= 0;
}
/* the split of FUN_180123060 / FUN_180122bc0 (hw l.13012 / l.12803): FUN_18011e8f0 with the template {mode 1, ctol 0, cos(DAT_1809c0558 x 180)
   = cos(pi) = -1, 0, 0, -1.0, StraightLineEnable 1, cos / sin(0.0001)} and no flag / kind-3 tests; at the split point (a sub-element cut there
   by FUN_180116b40 / FUN_180116770 when 0 < t < 1) the rest moves to a new composite after it (FUN_18011df10 + FUN_18011e550). 1 = split,
   0 = no split point (FUN_18011e8f0 at the list head), -1 = the split would leave an empty part (not reproducible, refused) */
static int comp_split_fix(HptCurve *cv, int ki)
{
    HptTemplate T = { 1, 0.0, cos(2.0 * acos(-1.0) * 0.002777777777777778 * 180.0), 0.0, 0.0, -1.0, 1, cos(0.0001), sin(0.0001) };
    HptComp *k = &cv->c[ki];
    HptSpl s = comp_fit(k, &T, 0, 0);
    if (g_thin_bad) return -1;
    if (s.k == k->nsub) return 0;
    int j = s.k;
    if (s.t != 0.0) {
        if (s.t != 1.0) {
            double q[3]; el_point(&k->sub[j], s.t, q);
            HptEl o; el_kind(&o, 0); el_split(&k->sub[j], &o, q);
            sub_insert(k, j + 1, &o);
        }
        j++;
    }
    if (j >= k->nsub || j <= 0) return -1;
    comp_split_at(cv, ki, j);
    comp_chord(&cv->c[ki + 1], 1); comp_chord(&cv->c[ki], 1);
    return 1;
}
/* FUN_180123be0(curve) (hw l.13471): while FUN_180122a00 finds a crossing: 2 -> FUN_180122bc0 (split; counted when the split moved the chord
   across P: (cross(A0 - P, A0 - S) > 0) != (cross(A0 - P, A0 - A1) > 0), A0 A1 the chord before, S its end after - hw l.13001-13005), 1 ->
   FUN_180123060 (counted whether it splits or not); no split ends the loop ("Failed to remove intersection. Could not split."). Returns the
   count; -1 refused */
static int curve_fix_self(HptCurve *cv)
{
    int cnt = 0, k; double P[3];
    for (int r = curve_cross(cv, &k, P); r; r = curve_cross(cv, &k, P)) {
        int ok;
        if (r == 2) {
            double A0[3], A1[3]; el_start(&cv->c[k].el, A0); el_end(&cv->c[k].el, A1);
            ok = comp_split_fix(cv, k);
            if (ok < 0) return -1;
            if (ok) {
                double S[3]; el_end(&cv->c[k].el, S);
                double dy19 = A0[1] - P[1], dx20 = A0[0] - P[0];
                int f1 = 0.0 < -(A0[1] - P[1]) * (A0[0] - S[0]) + (A0[0] - P[0]) * (A0[1] - S[1]);
                int f0 = 0.0 < (A0[0] - A1[0]) * -dy19 + dx20 * (A0[1] - A1[1]);
                if (f1 != f0) cnt++;
            }
        } else {
            ok = comp_split_fix(cv, k);
            if (ok < 0) return -1;
            cnt++;
        }
        if (!ok) return cnt;
    }
    return cnt;
}
/* FUN_180123ea0(curve 0, the ToolBoxSet of the other polygons) (hw l.13614): per composite a MeasureRadSegs2D (vftable 0x1807759d0) of its chord
   P0 P1 (r = 0x1807bb3f0 = 0.0001, the box the chord's grown by 0x1807bb3f8 = 0.00011, dis 180123f06-1801240de) over the set (FUN_18023ada0):
   per line element meeting the box FUN_18028a970 (dis 18028a970-18028ab5d) records the crossing FUN_180057040 (dis 180057040-1800571fa: the
   element's ends strictly on different sides of the chord's line, the parameter v of the crossing point projected onto P0 P1) and the range of
   the chord within r of the element (FUN_180055b50); the composite is taken when a crossing lies in a range (hw l.13743-13810). A crossing with
   0 <= v <= 1 lies in its own element's range (the point is on the element, the range reaches r / |P1 - P0| beyond it), a crossing outside
   [0, 1] in none (the ranges are clamped to [0, 1]): the test is coded as 0 <= v <= 1 (FUN_180055b50 itself not ported). The composites taken
   are split by FUN_180123060 in turn (no split: "Failed to remove intersection between different grids."); repeated while a split happened.
   segs: the elements, 4 doubles each (x0 y0 x1 y1). Returns the number of splits; -1 refused */
static int curve_fix_other(HptCurve *cv, const double *segs, int nseg)
{
    int tot = 0, n;
    do {
        int *list = malloc(sizeof(int) * (size_t)(cv->nc + 1)), nl = 0;
        for (int i = 0; i < cv->nc; i++) {
            double A0[3], A1[3]; el_start(&cv->c[i].el, A0); el_end(&cv->c[i].el, A1);
            double ey = A1[1] - A0[1], ex = A1[0] - A0[0];
            if (ey * ey + ex * ex == 0.0) continue;                   /* FUN_18028a970 dis 18028aac8-18028aaef: no crossings for a point chord */
            double bx0 = fmin(A0[0], A1[0]) - 0.00011000000000000002, bx1 = fmax(A0[0], A1[0]) + 0.00011000000000000002;
            double by0 = fmin(A0[1], A1[1]) - 0.00011000000000000002, by1 = fmax(A0[1], A1[1]) + 0.00011000000000000002;
            int hit = 0;
            for (int e = 0; e < nseg && !hit; e++) {
                const double *E = segs + 4 * e;
                double ex0 = fmin(E[0], E[2]), ex1 = fmax(E[0], E[2]), ey0 = fmin(E[1], E[3]), ey1 = fmax(E[1], E[3]);
                if (ex1 < bx0 || ex0 > bx1 || ey1 < by0 || ey0 > by1) continue;
                /* FUN_180057040 (L = P0 P1, E = S -> F): sides of S and F by -(P1y - P0y)(Px - P0x) + (Py - P0y)(P1x - P0x) > 0 */
                double dx = A1[0] - A0[0], ndy = -(A1[1] - A0[1]);
                double s1 = ndy * (E[0] - A0[0]) + (E[1] - A0[1]) * dx;
                double s2 = ndy * (E[2] - A0[0]) + dx * (E[3] - A0[1]);
                if ((s1 > 0.0) == (s2 > 0.0)) continue;
                double t = s1 / (s1 - s2), Q[2];
                if (E[0] == E[2] && E[1] == E[3]) { Q[0] = E[0]; Q[1] = E[1]; }
                else { double u = 1.0 - t; Q[0] = E[2] * t + E[0] * u; Q[1] = u * E[1] + t * E[3]; }
                double v = ((A1[1] - A0[1]) * (Q[1] - A0[1]) + (A1[0] - A0[0]) * (Q[0] - A0[0])) / ((A1[1] - A0[1]) * (A1[1] - A0[1]) + (A1[0] - A0[0]) * (A1[0] - A0[0]));
                if (0.0 <= v && v <= 1.0) hit = 1;
            }
            if (hit) list[nl++] = i;
        }
        n = 0;
        for (int q = 0, sh = 0; q < nl; q++) {
            int r = comp_split_fix(cv, list[q] + sh);
            if (r < 0) { free(list); return -1; }
            if (r) { n++; sh++; }
        }
        free(list);
        tot += n;
    } while (n > 0);
    return tot;
}
int hpt_fix_isect(HptWriter *w, const double *segs, int nseg)
{
    if (g_rad170 == 0.0) g_rad170 = 2.0 * acos(-1.0) * 0.002777777777777778 * 170.0;
    g_thin_bad = 0;
    if (w->ncv <= 0) return -1;
    for (int i = 0; i < w->ncv; i++) for (int k = 0; k < w->cv[i].nc; k++) if (w->cv[i].c[k].el.kind != 1) return -1;   /* closed line curves only (the filter's) */
    int tot = 0;
    for (int i = 0; i < w->ncv; i++) { int c = curve_fix_self(&w->cv[i]); if (c < 0) { g_thin_bad = 0; return -1; } tot += c; }
    int c = curve_fix_other(&w->cv[0], segs, nseg);
    g_thin_bad = 0;
    if (c < 0) return -1;
    return tot + c;
}

static int g_end_ok = 1;
static void w_end(void *ctx)
{ HptWriter *w = ctx; if (!w_end_curve(w, last_curve(w), w->pen, w->f110, w->f111, w->f115)) g_end_ok = 0; }

HptWriter *hpt_new(const HptTemplate *t, double z, int pco)
{
    HptWriter *w = calloc(1, sizeof *w);
    w->t = *t; w->z = z; w->f114 = pco != 0;
    { uint64_t u = 0x3f33a92a30553261ull; memcpy(&w->minchord, &u, 8); }
    w->f0 = -1; w->f112 = 1;
    return w;
}
/* the angle filter's writer at Eva +0x1440 as FUN_18040b6f0 sets it up (hz l.12262-12277): FUN_180118870 (hw l.5350) copies the template
   with +0x20 = 0x3f33a92a30553261, +0xb8 = 0, +0xc0 / +0xc8 / +0xd0 = 0 (z 0), +0xf0 = -1, +0x170 / +0x178 = 0; then +0x112 = !Eva +0x15c0,
   +0x113 = Eva +0x15c0, +0x114 = 1 (Eva +0x1554). Its starts are FUN_180118ee0 (hw l.5622) with param_5 = 0, which is w_start without the
   kind-0x1e index (kinds 0xf / 0xe) and with the point's own z (0) */
HptWriter *hpt_new_filter(const HptTemplate *t, int e15c0)
{
    HptWriter *w = hpt_new(t, 0.0, 1);
    w->f112 = !e15c0; w->f113 = e15c0 != 0;
    return w;
}
static void clear_curves(HptWriter *w)   /* FUN_180118930 */
{
    for (int i = 0; i < w->ncv; i++) { for (int k = 0; k < w->cv[i].nc; k++) free(w->cv[i].c[k].sub); free(w->cv[i].c); }
    w->ncv = 0; w->nk = 0;
}
void hpt_free(HptWriter *w) { if (!w) return; clear_curves(w); free(w->cv); free(w->kinds); free(w); }
void hpt_hvwriter(HptWriter *w, HvWriter *o) { o->ctx = w; o->start = w_start; o->line = w_line; o->arc = w_arc; o->end = w_end; }
int hpt_f178(const HptWriter *w) { return w->f178; }

static long pt_add(HptPasses *P, const double *p)   /* FUN_1800130c0 on the 2D points object; returns the point's index */
{
    if (P->npt + 1 > P->cpt) { P->cpt = P->cpt ? 2 * P->cpt : 64; P->xy = realloc(P->xy, sizeof(double) * 2 * (size_t)P->cpt); }
    P->xy[2 * P->npt] = p[0]; P->xy[2 * P->npt + 1] = p[1];
    return P->npt++;
}
static void vert(HptPasses *P, HptRec *R, const double *p, uint64_t bits)
{
    long q = pt_add(P, p);
    if (R->n == 0) R->first = q & 0xffffff;
    if (P->nbits + 1 > P->cbits) { P->cbits = P->cbits ? 2 * P->cbits : 64; P->bits = realloc(P->bits, sizeof(uint64_t) * (size_t)P->cbits); }
    P->bits[P->nbits++] = bits;
    R->n++;
}
static uint64_t flag_bits(int f) { return f == 1 ? 0 : f == -1 ? 0x40000000u : 0x20000000u; }
/* FUN_180123330(curve, .., closed, rec, points, closed, &n) (hw l.13142; arguments dis 18011af8a-18011afd5): the first composite's
   start, then per composite its end with its flag's bits (an arc first its centre, 0x4000000 when the direction byte is set else
   0x5000000; kind 3 its middle point, 0x6000000 / 0x7000000 by +0x28); a closed curve pops its last point (the count kept) */
static void curve_rec(const HptCurve *cv, HptPasses *P, HptRec *R, int closed)
{
    double q[3];
    el_start(&cv->c[0].el, q); vert(P, R, q, flag_bits(cv->c[0].el.flag));
    for (int i = 0; i < cv->nc; i++) {
        const HptEl *e = &cv->c[i].el;
        uint64_t b = flag_bits(e->flag);
        if (e->kind == 1) { el_end(e, q); vert(P, R, q, b); }
        else if (e->kind == 2) { double c[3] = { e->c[0], e->c[1], e->zs }; vert(P, R, c, e->dir ? 0x4000000u : 0x5000000u); el_end(e, q); vert(P, R, q, b); }
        else if (e->kind == 3) { vert(P, R, e->p[1], 0x7000000u); el_end(e, q); vert(P, R, q, b); }   /* +0x28 byte: not set by this writer */
    }
    if (closed) { P->npt--; R->closed = 1; }
}

/* debugging (HPT_CURVES=file): the curves in hvd_curves' format (tools/peloader/hmm_vor_dump.h) */
static FILE *g_dbg;
static void dbg_el(const char *tag, const HptEl *e)
{
    if (e->kind == 2) fprintf(g_dbg, "%s k2 f%d %a %a -> %a %a c %a %a d%d\n", tag, e->flag, e->s[0], e->s[1], e->e[0], e->e[1], e->c[0], e->c[1], e->dir);
    else if (e->kind == 1 || e->kind == 3) fprintf(g_dbg, "%s k%d f%d %a %a -> %a %a\n", tag, e->kind, e->flag, e->p[0][0], e->p[0][1], e->p[e->kind == 3 ? 2 : 1][0], e->p[e->kind == 3 ? 2 : 1][1]);
    else fprintf(g_dbg, "%s k%d f%d\n", tag, e->kind, e->flag);
}
static void dbg_curves(const HptWriter *w, const char *stage)
{
    if (!g_dbg) return;
    for (int i = 0; i < w->ncv; i++) {
        fprintf(g_dbg, "CURVE %s %d\n", stage, i);
        for (int k = 0; k < w->cv[i].nc; k++) { dbg_el("K", &w->cv[i].c[k].el); for (int j = 0; j < w->cv[i].c[k].nsub; j++) dbg_el(" S", &w->cv[i].c[k].sub[j]); }
    }
    fflush(g_dbg);
}

static int hpt_flush_k(HptWriter *w, HptPasses *P, char *msg, int nmsg, int keep)
{
    if (msg && nmsg) msg[0] = 0;
    if (!g_dbg && getenv("HPT_CURVES")) g_dbg = fopen(getenv("HPT_CURVES"), "w");
    if (g_dbg) {
        dbg_curves(w, "pre");
        if (w->t.mode != 0) {
            for (int i = 0; i < w->ncv; i++) thin_straight(&w->cv[i], w->f114);
            dbg_curves(w, "straight");
            if (g_rad170 == 0.0) g_rad170 = 2.0 * acos(-1.0) * 0.002777777777777778 * 170.0;
            for (int i = 0; i < w->ncv; i++)
                for (int k = 0; k < w->cv[i].nc; k++) {
                    if (w->cv[i].c[k].el.kind == 5) continue;
                    HptSpl sp = comp_fit(&w->cv[i].c[k], &w->cv[i].t, w->f170 > 0, 0);
                    fprintf(g_dbg, "FIT %d %d %a\n", k, sp.k, sp.t);
                }
            if (g_rad170 == 0.0) g_rad170 = 2.0 * acos(-1.0) * 0.002777777777777778 * 170.0;
            for (int i = 0; i < w->ncv; i++) {
                HptCurve *cv = &w->cv[i];
                for (int k = 0; k < cv->nc && !g_thin_bad;) { if (cv->c[k].el.kind == 5) { k++; continue; } k = cv->t.mode == 1 ? thin_lines(cv, k, w->f170 > 0, 0) : thin_arcs(cv, k, w->f170 > 0, 0); }
                for (int k = 0; k < cv->nc; k++) cv->c[k].el.flag = comp_common_flag(&cv->c[k]);
            }
            dbg_curves(w, g_thin_bad ? "thinBAD" : "thin");
            g_thin_bad = 0;
        }
        clear_curves(w); return 0;
    }
    if (!g_end_ok) { g_end_ok = 1; if (msg) snprintf(msg, (size_t)nmsg, "the thinned curve close FUN_180121eb0 is NOT DECODED"); clear_curves(w); return 0; }
    if (w->f178 && !keep) clear_curves(w);   /* message 0x4c4 (FUN_18011ca00 only; FUN_18011cbc0 has no +0x178 test), FUN_180118930 (hw l.8031-8049); +0x178 itself is reset by FUN_180406b00 */
    if (w->t.mode != 0) {
        if (g_rad170 == 0.0) g_rad170 = 2.0 * acos(-1.0) * 0.002777777777777778 * 170.0;
        g_thin_bad = 0;
        for (int i = 0; i < w->ncv; i++)
            if (!thin_curve(&w->cv[i], w->f114, w->f170 > 0, 0)) {
                if (msg) snprintf(msg, (size_t)nmsg, "a thinning split through the list head (FUN_18011e550 on an empty part)");
                g_thin_bad = 0; clear_curves(w); return 0;
            }
    }
    for (int i = 0; i < w->ncv; i++) {
        const HptCurve *cv = &w->cv[i];
        if (P->nrec + 1 > P->crec) { P->crec = P->crec ? 2 * P->crec : 16; P->rec = realloc(P->rec, sizeof *P->rec * (size_t)P->crec); }
        HptRec *R = &P->rec[P->nrec++]; memset(R, 0, sizeof *R);
        R->kind = w->kinds[i]; R->vb = P->nbits;
        curve_rec(cv, P, R, cv->c[cv->nc - 1].el.kind != 5);
    }
    if (!keep) clear_curves(w);
    return 1;
}
int hpt_flush(HptWriter *w, HptPasses *P, char *msg, int nmsg) { return hpt_flush_k(w, P, msg, nmsg, 0); }
/* FUN_18011cbc0(writer, points, records, 1) (hw l.8091): the thinning and the records, the curves kept (FUN_180407c10 clears them at its end,
   FUN_180118930 hzp l.707) */
int hpt_records(HptWriter *w, HptPasses *P, char *msg, int nmsg) { return hpt_flush_k(w, P, msg, nmsg, 1); }
void hpt_clear(HptWriter *w) { clear_curves(w); }
void hpt_passes_free(HptPasses *p) { free(p->xy); free(p->rec); free(p->bits); memset(p, 0, sizeof *p); }
