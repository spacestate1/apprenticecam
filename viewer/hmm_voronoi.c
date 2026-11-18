/* hmm_voronoi.c - port of mchmm.dll's own Voronoi library (McHmm "voronoi\VoronoiFunctions.cpp"; formats/hst3d_port_plan.md
 * 7 "W3", user decision N13-12). Sources: hv = decomp/hmm_vorlib.c (mchmm.dll 180540730-18055ff90), hz = decomp/hmm_zprofile.c,
 * the disassembly of mchmm.dll where Ghidra drops arguments or return values (noted at each place).
 * Floating point: the DLL's SSE2 double arithmetic, kept term by term (compile with -ffp-contract=off).
 *
 * Records (payload offsets; every list is a std::list: node +0 next, +8 prev, +0x10 payload; the sentinel is a node too):
 *  graph (0xb0, FUN_1805475b0): +0 / +8 / +0x10 vector of polygon records (0x48 each, npoly + 1 of them); +0x18 int polygon
 *    count; +0x20 / +0x38 vectors (begin, end, cap) of bridge vertex pointers (FUN_18054c120); +0x50 the start vertex;
 *    +0x58 / +0x60 node list (payload 0x88); +0x68 / +0x70 edge list (payload 0x60); +0x78 2 (w + h) of the input box;
 *    +0x80 1 / VoronoiParallelVanishingDistanceReciprocal; +0x88 VoronoiSmallBisectorLength; +0x90 -Split; +0x98 the
 *    perturbation of this attempt; +0xa0 / +0xa8 bottleneck node list (payload 0x88).
 *  polygon record (0x48, FUN_1805462f0): +0 / +8 vertex list (payload 0xa8); +0x10 / +0x18 site list (payload 0x60); +0x20
 *    byte hole (index != 0); +0x28 a site of the loop (the last made, FUN_180546680; FUN_180550600 moves it to the extreme
 *    one); +0x30 / +0x38 the extreme point, +0x40 its key (FUN_180550600).
 *  vertex (0xa8, FUN_180546380): +0 x, +8 y, +0x10 0, +0x18 int kind (7 plain, 8 normal set / reflex split, 9 bridge, 10 split
 *    point, 0xb bridge pair: +0x88 / +0x90 the real vertex for the site leaving / entering), +0x20 site entering, +0x28 1.0,
 *    +0x30 site leaving, +0x60 its bisector edge, +0x84 int -1, +0x88 / +0x90 see kind 0xb, +0x98 / +0xa0 unit normal.
 *  site (0x60, FUN_180546490 / FUN_180547360): +0 from vertex, +8 to vertex, +0x10 byte 1 = in use, +0x11 byte 1 = arc (a point
 *    site: radius 0), +0x18 / +0x20 segment: unit normal (pointing into the region), arc: centre; +0x28 segment: n . from, arc:
 *    radius; +0x38 / +0x40 its first / last Voronoi node (FUN_18054b150), +0x58 int -1, +0x5c byte flag (vertex kinds).
 *  edge (0x60, FUN_180547500): +0 int kind (0 dead, 1 line: point + direction, 2 parallel sites, 3 vertex normal / split,
 *    4 curved (segment / point), 5 two arcs), +8 / +0x10 point, +0x18 / +0x20 direction, +0x28 / +0x30 left / right site,
 *    +0x38 end node, +0x40 start node or vertex, +0x48 int, +0x50 ptr, +0x58 int, +0x5c byte.
 *  node (0x88, FUN_18040a9a0): +0 x, +8 y, +0x10 clearance r, +0x18 int kind (3 = valid, 0 = dead, 2, 4 bridge, 5, 6 split),
 *    +0x20 / +0x30 / +0x40 sites A / B / C with their foot parameters at +0x28 / +0x38 / +0x48, +0x50 / +0x58 more (4 / 5),
 *    +0x60 / +0x68 the two edges that meet, +0x70 a fourth, +0x78 the edge that leaves, +0x84 int -1. */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "hmm_voronoi.h"

typedef unsigned char u8;
#define F64(p, o) (*(double *)((u8 *)(p) + (o)))
#define PTR(p, o) (*(u8 **)((u8 *)(p) + (o)))
#define I32(p, o) (*(int32_t *)((u8 *)(p) + (o)))
#define U8(p, o) (*(u8 *)((u8 *)(p) + (o)))
#define U64(p, o) (*(uint64_t *)((u8 *)(p) + (o)))

/* ------------------------------------------------------------------ memory: one arena per graph */
typedef struct HvBlk { struct HvBlk *next; } HvBlk;
struct HvGraph {
    u8 rec[0xb0];            /* the graph record of FUN_1805475b0 */
    HvBlk *arena;
    HvParams par;
    const HvPoly *polys;     /* the input of the current build */
    int npoly;
};
static HvGraph *g_cur;       /* the graph whose arena takes the allocations (single-threaded, set by hv_build) */

static u8 *hv_alloc(size_t n)
{
    HvBlk *b = calloc(1, sizeof(HvBlk) + 16 + n);
    if (!b) abort();
    b->next = g_cur->arena; g_cur->arena = b;
    return (u8 *)b + sizeof(HvBlk) + 16 - ((sizeof(HvBlk)) & 15);
}

/* std::list: a sentinel node linked to itself; push_back = a node before the sentinel */
static u8 *ls_new(size_t payload) { u8 *h = hv_alloc(0x10 + payload); PTR(h, 0) = h; PTR(h, 8) = h; return h; }
static u8 *ls_push_back(u8 *head, size_t payload, int64_t *count)
{
    u8 *n = hv_alloc(0x10 + payload);
    u8 *prev = PTR(head, 8);
    PTR(n, 0) = head; PTR(n, 8) = prev;
    PTR(head, 8) = n; PTR(prev, 0) = n;
    if (count) (*count)++;
    return n;
}
static void ls_unlink(u8 *n, int64_t *count)
{
    PTR(PTR(n, 8), 0) = PTR(n, 0);
    PTR(PTR(n, 0), 8) = PTR(n, 8);
    if (count) (*count)--;
}
static void ls_clear(u8 *head, int64_t *count) { PTR(head, 0) = head; PTR(head, 8) = head; if (count) *count = 0; }

#define G (g_cur->rec)
#define G_NODES (PTR(G, 0x58))
#define G_EDGES (PTR(G, 0x68))
#define G_BNECK (PTR(G, 0xa0))

/* vertex of a site: kind 0xb indirection (+0x88 for a site's start, +0x90 for its end) */
static u8 *vstart(u8 *v) { if (v && I32(v, 0x18) == 0xb) v = PTR(v, 0x88); return v; }
static u8 *vend(u8 *v) { if (v && I32(v, 0x18) == 0xb) v = PTR(v, 0x90); return v; }

/* the foot parameter a node holds for a site: +0x20 / +0x30 / +0x40 sites, +0x28 / +0x38 / +0x48 parameters; -1 when the
   node does not hold it (the pattern repeated all through hv) */
static double node_t(u8 *n, u8 *site)
{
    if (PTR(n, 0x20) == site) return F64(n, 0x28);
    if (PTR(n, 0x30) == site) return F64(n, 0x38);
    if (PTR(n, 0x40) == site) return F64(n, 0x48);
    return -1.0;
}

/* ------------------------------------------------------------------ small helpers of mchmm.dll */
/* FUN_18004d7e0: pseudo-angle of (x, y) in [0, 4) (disassembly; 0 for the zero vector) */
static double pang(const double *p)
{
    double x = p[0], y = p[1];
    if (x == 0.0 && y == 0.0) return 0.0;
    if (y >= 0.0) {
        if (x >= 0.0) return y / (y + x);
        return 1.0 - x / (y - x);
    }
    if (x < 0.0) return 2.0 - y / (-x - y);
    double r = x / (x - y) + 3.0;
    if (r == 4.0) r = 0.0;
    return r;
}
/* FUN_18000b4e0: a + t (b - a) as (1 - t) a + t b, a itself when a == b (disassembly) */
static void lerp(double *o, double t, const double *a, const double *b)
{
    if (a[0] == b[0] && a[1] == b[1]) { o[0] = a[0]; o[1] = a[1]; return; }
    double u = 1.0 - t;
    o[0] = b[0] * t + a[0] * u;
    o[1] = a[1] * u + t * b[1];
}
/* FUN_1800d4e10: the same with a float parameter */
static void lerpf(double *o, float tf, const double *a, const double *b)
{
    if (a[0] == b[0] && a[1] == b[1]) { o[0] = a[0]; o[1] = a[1]; return; }
    double t = (double)tf, u = 1.0 - t;
    o[0] = b[0] * t + a[0] * u;
    o[1] = u * a[1] + t * b[1];
}
/* FUN_180054c20: midpoint, a itself when a == b */
static void midp(double *o, const double *a, const double *b)
{
    if (a[0] == b[0] && a[1] == b[1]) { o[0] = a[0]; o[1] = a[1]; return; }
    o[0] = (a[0] + b[0]) * 0.5;
    o[1] = (a[1] + b[1]) * 0.5;
}
/* FUN_1802e67b0 (disassembly): roots of a t^2 + 2 b t + c = 0; lim bounds a usable root; returns the root count (0, 1, 2) */
static int quad(double *r1, double *r2, double a, double b, double c, double lim)
{
    if (a == 0.0) {
        if (b == 0.0) {
            if (c == 0.0) { *r1 = 0.0; *r2 = 0.0; return 1; }
            return 0;
        }
        double t = -(c / (b * 2.0));
        *r1 = t; *r2 = t; return 1;
    }
    double d = b * b - a * c;
    if (d < 0.0) return 0;
    double s;
    if (d <= 0.0 || (s = sqrt(d)) == 0.0) { double t = -(b / a); *r1 = t; *r2 = t; return 1; }
    double ia = 1.0 / a;
    double t1 = (-b - s) * ia, t2 = (s - b) * ia;
    *r1 = t1; *r2 = t2;
    double a1 = fabs(t1), x9 = 0.0;
    int cl;
    if (a1 > lim) {
        x9 = t2;
        cl = !(b == 0.0 || fabs(t2) >= lim);
    } else if (!(fabs(t2) > lim)) cl = 0;
    else {
        x9 = t1;
        cl = !(b == 0.0 || a1 >= lim);
    }
    double ab = fabs(b), ac = fabs(c);
    double m = (ac > ab) ? ac : ab;
    if (fabs(a) > m * (double)9.999999747378752e-06) {
        if (!cl) return 2;
    } else if (!cl) x9 = (a1 >= fabs(t2)) ? t2 : t1;
    if (b == 0.0) return 2;
    double b2 = b * 2.0, tl = -(c / b2);
    double f9 = fabs((x9 * a + b2) * x9 + c), fl = fabs((tl * a + b2) * tl + c);
    if (fl >= f9) return 2;
    *r1 = tl; *r2 = tl; return 1;
}

/* ------------------------------------------------------------------ site geometry */
/* FUN_180545540 (hv l.3402): the foot parameter of point p on site s, [0, 1] along a segment (snapped from -1e-10 / 1 + 1e-10);
   on an arc of radius 0 the pseudo-angle of p round the centre, from the normal of the from vertex to that of the to vertex;
   -1 on an arc of radius != 0 */
static double s_foot(u8 *s, const double *p)
{
    double t;
    if (U8(s, 0x11) == 0) {
        double *a = (double *)PTR(s, 0), *b = (double *)PTR(s, 8);
        double dy = b[1] - a[1], dx = b[0] - a[0];
        t = ((p[1] - a[1]) * dy + (p[0] - a[0]) * dx) / (dy * dy + dx * dx);
    } else {
        if (F64(s, 0x28) != 0.0) return -1.0;
        u8 *a = PTR(s, 0), *b = PTR(s, 8);
        double b1 = F64(b, 0x98), b2 = F64(b, 0xa0), a1 = F64(a, 0x98), a2 = F64(a, 0xa0);
        double q[2];
        q[0] = a2 * (p[1] - F64(s, 0x20)) + a1 * (p[0] - F64(s, 0x18));
        q[1] = -a1 * (p[1] - F64(s, 0x20)) + a2 * (p[0] - F64(s, 0x18));
        t = pang(q);
        if (2.0 < t) t = t + -4.0;
        q[0] = a1 * b1 + a2 * b2;
        q[1] = -a1 * b2 + a2 * b1;
        double w = pang(q);
        if (2.0 < w) w = w + -4.0;
        if (w == 0.0) return -1.0;
        t = t / w;
    }
    if (0.0 <= t || t < -1e-10) {
        if (1.0 < t && t <= 1.0000000001) t = 1.0;
    } else t = 0.0;
    return t;
}
/* FUN_180545760 (hv l.3462): distance of p from site s; side -1 / 1 keeps the segment's start / end from clamping */
static double s_dist(u8 *s, const double *p, int side)
{
    if (U8(s, 0x11) != 0) {
        double dy = p[1] - F64(s, 0x20), dx = p[0] - F64(s, 0x18);
        return fabs(sqrt(dy * dy + dx * dx) - F64(s, 0x28));
    }
    double *a = (double *)PTR(s, 0), *b = (double *)PTR(s, 8);
    double x = p[0], y = p[1];
    double dy = b[1] - a[1], dx = b[0] - a[0];
    double t = ((y - a[1]) * dy + (x - a[0]) * dx) / (dy * dy + dx * dx);
    if (t < 0.0 && side != -1) return sqrt((y - a[1]) * (y - a[1]) + (x - a[0]) * (x - a[0]));
    if (1.0 < t && side != 1) return sqrt((y - b[1]) * (y - b[1]) + (x - b[0]) * (x - b[0]));
    return fabs((y * F64(s, 0x20) + x * F64(s, 0x18)) - F64(s, 0x28));
}
/* FUN_180545860 (hv l.3508): the point at distance r along the ray o + r d that is equidistant from o's... : the ray from point
   o in direction d meets site s where its distance from s equals r; out = point, *rr = r, *t = the foot parameter; 1 when
   r >= 0 and the foot lies on the site */
static int s_ray(u8 *s, double *out, double *rr, double *t, const double *d, const double *o)
{
    double d1 = d[1], d0 = d[0], o0 = o[0], o1 = o[1];
    double num, den;
    if (U8(s, 0x11) == 0) {
        num = F64(s, 0x28) - (o1 * F64(s, 0x20) + o0 * F64(s, 0x18));
        den = (d1 * F64(s, 0x20) + d0 * F64(s, 0x18)) - 1.0;
    } else {
        double R = F64(s, 0x28), ex = o0 - F64(s, 0x18), ey = o1 - F64(s, 0x20);
        num = R * R - (ey * ey + ex * ex);
        den = ((d1 * ey + d0 * ex) - R) * 2.0;
    }
    double r = num / den;
    if (0.0 <= r) {
        *rr = r;
        out[0] = o0 + d0 * r; out[1] = o1 + d1 * r;
        *t = s_foot(s, out);
        if (0.0 <= *t && *t <= 1.0) return 1;
    }
    return 0;
}
/* FUN_1805459f0 (hv l.3586): the bisector of sites a and b into an edge record (kind, point, direction): 1 two segments
   meeting (point = the lines' intersection, direction scaled so the distance grows by 1 per unit), 2 parallel opposite
   segments (the mid line, direction scaled by the half gap), 4 a segment / arc or two arcs about different centres, 5 two
   arcs about one centre, 0 none */
static void s_bisector(u8 *o, u8 *a, u8 *b, double tol)
{
    memset(o + 8, 0, 0x20);
    if (U8(a, 0x11) == 0 && U8(b, 0x11) == 0) {
        double ax = F64(a, 0x18), ay = F64(a, 0x20), ac = F64(a, 0x28), bx = F64(b, 0x18), by = F64(b, 0x20), bc = F64(b, 0x28);
        double cr = -ax * by + ay * bx;
        double crd = cr; if (cr == 0.0) crd = 1.0;
        double dot = by * ay + ax * bx;
        double dp1 = dot + 1.0;
        if (cr == 0.0 || tol <= fabs((bc - (ay * by + ax * bx) * ac) / crd) || dp1 == 0.0) {
            if (0.0 <= dot) I32(o, 0) = 0;
            else {
                I32(o, 0) = 2;
                double h = (-bc - ac) * 0.5;
                if (h <= 0.0) I32(o, 0) = 0;
                else {
                    double m = ac + h;
                    F64(o, 8) = m * ax; F64(o, 0x10) = m * ay;
                    F64(o, 0x18) = -ay * h; F64(o, 0x20) = ax * h;
                }
            }
        } else {
            I32(o, 0) = 1;
            double inv = 1.0 / dp1;
            double u = (bc - (ay * by + ax * bx) * ac) / cr;
            F64(o, 8) = ay * u + ac * ax; F64(o, 0x10) = -ax * u + ac * ay;
            F64(o, 0x18) = inv * (bx + ax); F64(o, 0x20) = inv * (by + ay);
        }
    } else if (U8(a, 0x11) == 0 || U8(b, 0x11) == 0 ||
               !(F64(a, 0x18) == F64(b, 0x18) && F64(a, 0x20) == F64(b, 0x20))) {   /* FUN_18000bcf0: the centres equal */
        I32(o, 0) = 4;
    } else if (0.0 < F64(b, 0x28) * F64(a, 0x28)) I32(o, 0) = 0;
    else {
        I32(o, 0) = 5;
        F64(o, 8) = (F64(a, 0x28) - F64(b, 0x28)) * 0.5 + F64(a, 0x18);
        F64(o, 0x10) = F64(a, 0x20);
    }
}
/* FUN_180545ce0 (hv l.3687): the bisector leaving vertex v (between its entering and leaving sites): kind 1 (direction from
   the two unit normals / radial directions, scaled), 3 (kind 8 vertices: their own normal), 4 (bridge vertices, kind 9) */
static void v_bisector(u8 *o, u8 *v)
{
    memset(o + 8, 0, 0x20);
    if (I32(v, 0x18) == 9) { I32(o, 0) = 4; F64(o, 0x18) = 0; F64(o, 0x20) = 0; return; }
    double a1, a2, b1, b2;
    u8 *s = PTR(v, 0x20);
    if (U8(s, 0x11) == 0) { a1 = F64(s, 0x18); a2 = F64(s, 0x20); }
    else {
        double r = F64(s, 0x28); if (r == 0.0) r = 1.0;
        double ir = 1.0 / r;
        a2 = ir * (F64(v, 8) - F64(s, 0x20)); a1 = ir * (F64(v, 0) - F64(s, 0x18));
    }
    s = PTR(v, 0x30);
    if (U8(s, 0x11) == 0) { b1 = F64(s, 0x18); b2 = F64(s, 0x20); }
    else {
        double r = F64(s, 0x28); if (r == 0.0) r = 1.0;
        double ir = 1.0 / r;
        b2 = ir * (F64(v, 8) - F64(s, 0x20)); b1 = ir * (F64(v, 0) - F64(s, 0x18));
    }
    F64(o, 8) = F64(v, 0); F64(o, 0x10) = F64(v, 8);
    if (I32(v, 0x18) == 8) {
        I32(o, 0) = 3;
        F64(o, 0x18) = F64(v, 0x98); F64(o, 0x20) = F64(v, 0xa0);
        return;
    }
    I32(o, 0) = 1;
    double d = b2 * a2 + b1 * a1 + 1.0;
    if (d != 0.0) {
        double inv = 1.0 / d;
        F64(o, 0x18) = inv * (b1 + a1); F64(o, 0x20) = inv * (b2 + a2);
        return;
    }
    F64(o, 0x18) = 0; F64(o, 0x20) = 0;
}
/* the result of the equidistant-point solvers (FUN_180545eb0, FUN_180550790, FUN_180550cb0, FUN_180550f40): two candidate
   points (x, y, clearance) and their count */
typedef struct { double x1, y1, r1, x2, y2, r2; int n; } HvSol;
static void sol_none(HvSol *s) { s->x1 = s->y1 = 0; s->r1 = -1.0; s->x2 = s->y2 = 0; s->r2 = -1.0; s->n = 0; }
/* FUN_180545eb0 (hv l.3774): where the edge line P + t D (t = the clearance) is as far from site s as from the edge's own sites */
static void e_meet_site(const u8 *e, HvSol *o, u8 *s, double lim)
{
    double Px = F64(e, 8), Py = F64(e, 0x10), Dx = F64(e, 0x18), Dy = F64(e, 0x20);
    if (U8(s, 0x11) == 0) {
        double den = 1.0 - (F64(s, 0x20) * Dy + F64(s, 0x18) * Dx);
        double num = (Py * F64(s, 0x20) + F64(s, 0x18) * Px) - F64(s, 0x28);
        if (fabs(den) * lim <= fabs(num)) { sol_none(o); return; }
        double t = num / den;
        o->x1 = t * Dx + Px; o->y1 = t * Dy + Py; o->r1 = t;
        o->x2 = 0; o->y2 = 0; o->r2 = -1.0; o->n = 1;
        return;
    }
    double ex = Px - F64(s, 0x18), R = F64(s, 0x28), ey = Py - F64(s, 0x20);
    double A = (Dx * Dx + Dy * Dy) - 1.0;
    double B = (ey * Dy + ex * Dx) - R;
    double C = (ey * ey + ex * ex) - R * R;
    if (I32(e, 0) == 3 || A == 0.0) {
        if (B != 0.0) {
            double t = C / (B * 2.0);
            o->x1 = -t * Dx + Px; o->y1 = -t * Dy + Py; o->r1 = -t;
            o->x2 = 0; o->y2 = 0; o->r2 = -1.0; o->n = 1;
            return;
        }
    } else {
        double d = B * B - C * A;
        if (0.0 <= d) {
            double sq = sqrt(d);
            double t1 = (-B - sq) * (1.0 / A), t2 = (sq - B) * (1.0 / A);
            double t7; int cond;
            if (lim < fabs(t1)) { t7 = t2; cond = 1; } else { t7 = t1; cond = lim < fabs(t2); }
            if (cond && B != 0.0 && fabs(t7) < lim) {
                double B2 = B * 2.0, tl = C / B2;
                if (fabs((-tl * A + B2) * -tl + C) < fabs((t7 * A + B2) * t7 + C)) {
                    o->x1 = -tl * Dx + Px; o->y1 = -tl * Dy + Py; o->r1 = -tl;
                    o->x2 = 0; o->y2 = 0; o->r2 = -1.0; o->n = 1;
                    return;
                }
            }
            o->x1 = t1 * Dx + Px; o->y1 = t1 * Dy + Py; o->r1 = t1;
            o->x2 = t2 * Dx + Px; o->y2 = t2 * Dy + Py; o->r2 = t2;
            o->n = 2;
            return;
        }
    }
    sol_none(o);
}

/* ------------------------------------------------------------------ the input polygons as sites */
/* the FGridC entries of polygon k (FUN_18004f610 on a closed dense record: cnt = n + 1 entries, entry n reads entry 0) */
static int in_cnt(int k) { return g_cur->polys[k].n + 1; }
static const HvVert *in_v(int k, int64_t j) { const HvPoly *p = &g_cur->polys[k]; return &p->v[j == p->n ? 0 : j]; }
/* the vertex-kind sign of FUN_180546680: bits 0 -> 1, 0x40000000 -> -1, others 0 */
static int in_kind(int k, int64_t j)
{
    uint64_t b = in_v(k, j)->kind & 0xfffffffff0000000ull;
    if (b == 0) return 1;
    return (b != 0x40000000ull) - 1;
}

/* FUN_180546380 (hv l.3937): a vertex record at point p (kind 7) at the end of the polygon record's vertex list */
static u8 *v_new(u8 *pr, const double *p)
{
    u8 *n = ls_push_back(PTR(pr, 0), 0xa8, (int64_t *)(pr + 8));
    u8 *v = n + 0x10;
    F64(v, 0) = p[0]; F64(v, 8) = p[1];
    I32(v, 0x18) = 7; F64(v, 0x28) = 1.0; I32(v, 0x84) = -1;
    return v;
}
/* FUN_180546490 (hv l.4011): a site from vertex a to vertex b at the end of the site list; byte +0x5c = flag */
static u8 *s_new(u8 *pr, u8 *a, u8 *b, int flag)
{
    u8 *n = ls_push_back(PTR(pr, 0x10), 0x60, (int64_t *)(pr + 0x18));
    u8 *s = n + 0x10;
    I32(s, 0x58) = -1;
    PTR(s, 0) = a; PTR(s, 8) = b;
    PTR(vstart(a), 0x30) = s;
    PTR(vend(PTR(s, 8)), 0x20) = s;
    U8(s, 0x5c) = (u8)flag;
    return s;
}
/* FUN_1805465a0 (hv l.4065): a segment site a -> b: the unit normal (-dy, dx) / len and n . a */
static u8 *s_new_seg(u8 *pr, u8 *a, u8 *b, int flag)
{
    u8 *s = s_new(pr, a, b, flag);
    U8(s, 0x11) = 0;
    double *pa = (double *)vstart(PTR(s, 0)), *pb = (double *)vend(PTR(s, 8));
    double dx = pb[0] - pa[0], ny = -(pb[1] - pa[1]);
    double len = sqrt(-(pb[1] - pa[1]) * ny + dx * dx);
    if (len == 0.0) len = 1.0;
    F64(s, 0x18) = (1.0 / len) * ny; F64(s, 0x20) = (1.0 / len) * dx;
    U8(s, 0x10) = 1;
    double *p0 = (double *)vstart(PTR(s, 0));
    F64(s, 0x28) = F64(s, 0x20) * p0[1] + F64(s, 0x18) * p0[0];
    return s;
}
/* the low 32 bits of a float's encoding (the hash of FUN_180547070 reads the float as an int: disassembly 1805471fb-18054725f;
   Ghidra shows a conversion) */
static uint32_t fbits(double d) { float f = (float)d; uint32_t u; memcpy(&u, &f, 4); return u; }
/* FUN_180547070 (hv l.4558): input vertex j of polygon k, moved by the perturbation pert of this attempt: every other vertex
   (by the index parity and the sign of pert) along the sum of its two edge directions turned, by |pert| x a hash fraction */
static void in_point(double *o, int k, int64_t j, double pert)
{
    const HvVert *v = in_v(k, j);
    double px = v->x, py = v->y;
    int64_t cnt = in_cnt(k);
    if (j == 0) j = cnt - 1;
    if (pert == 0.0 || (uint32_t)(0.0 < pert) == ((uint32_t)j & 1)) { o[0] = px; o[1] = py; return; }
    int64_t jn = j + 1; if (jn == cnt) jn = 1;
    int64_t jp = (j == 0) ? cnt - 2 : j - 1;
    const HvVert *nx = in_v(k, jn), *pv = in_v(k, jp);
    double yv = -(pv->y - py) + -(py - nx->y);
    double xv = (pv->x - px) + (px - nx->x);
    int32_t h1 = (int32_t)((fbits(xv - yv) + 0xff5bcd15u) & 0x7ffffffu) % 20000;
    int32_t h2 = (int32_t)((fbits(yv + pert) + 0xff5bcd15u) & 0x7ffffffu) % 20000;
    double s = (double)h1 * 5e-05 + (double)h2 * 5e-05;
    double r = fabs(pert) * (s - (double)(int32_t)s);
    double len = sqrt(xv * xv + yv * yv);
    if (len == 0.0) len = 1.0;
    double il = 1.0 / len;
    o[1] = il * xv * r + py;
    o[0] = il * yv * r + px;
}
/* FUN_180546680 (hv l.4110): polygon k's vertices and sites into polygon record pr. The vertex list runs against the input
   order (the input's last vertex first); a segment site per edge; at a reflex turn (sine below -1 / recip_inv) the vertex is
   doubled with a point site between the copies, each copy kind 8 with its edge's normal; a nearly straight turn (|sine| <=
   1 / recip_inv, forward) gives kind 8 with the mean normal. Site flags from the vertex kinds (see in_kind). */
static void poly_sites(u8 *pr, int k, int hole, double recip_inv, double pert)
{
    U8(pr, 0x20) = (u8)hole;
    PTR(pr, 0x28) = NULL;
    int64_t cnt = in_cnt(k);
    u8 *first = NULL, *prev = NULL;
    for (int64_t L = cnt; L != 0; L--) {
        int64_t j = L - 1;
        u8 *cur;
        double nxt[2], p[2];
        if (j == 0) {
            cur = first;
            in_point(nxt, k, (int64_t)((int)cnt + -2), pert);
        } else {
            in_point(p, k, j, pert);
            cur = v_new(pr, p);
            in_point(nxt, k, j - 1, pert);
        }
        if (L == cnt) { first = cur; prev = cur; continue; }
        int kind_prev = in_kind(k, L);
        int flag = (kind_prev == -1);
        double ax = F64(cur, 0) - F64(prev, 0), ay = F64(cur, 8) - F64(prev, 8);
        double by = nxt[1] - F64(cur, 8), bx = nxt[0] - F64(cur, 0);
        double ax2 = ax * ax, bx2 = bx * bx;
        double q = sqrt((ay * ay + ax2) * (by * by + bx2));
        if (q == 0.0) q = 1.0;
        double sn = (by * ax + (-bx) * ay) / q;
        double inv = 1.0 / recip_inv;
        if (sn >= inv * -1.0) {
            if (!(sn > inv) && by * ay + bx * ax >= 0.0) {
                double nay = -ay;
                double la = sqrt(nay * nay + ax2); if (la == 0.0) la = 1.0;
                double ia = 1.0 / la;
                double nby = -by;
                double lb = sqrt(nby * nby + bx2);
                double t4 = ia * nay, t1 = ia * ax;
                if (lb == 0.0) lb = 1.0;
                double ib = 1.0 / lb;
                double X = ib * bx + t1, Y = ib * nby + t4;
                double n = sqrt(X * X + Y * Y); if (n == 0.0) n = 1.0;
                I32(cur, 0x18) = 8;
                double in_ = 1.0 / n;
                F64(cur, 0x98) = in_ * Y; F64(cur, 0xa0) = in_ * X;
            }
            PTR(pr, 0x28) = s_new_seg(pr, prev, cur, flag);
        } else {
            double cp[2] = { F64(cur, 0), F64(cur, 8) };
            u8 *dup = v_new(pr, cp);
            double nay = -ay;
            double la = sqrt(nay * nay + ax2); if (la == 0.0) la = 1.0;
            I32(dup, 0x18) = 8;
            double ia = 1.0 / la;
            F64(dup, 0x98) = ia * nay; F64(dup, 0xa0) = ia * ax;
            double nby = -by;
            double lb = sqrt(nby * nby + bx2); if (lb == 0.0) lb = 1.0;
            I32(cur, 0x18) = 8;
            double ib = 1.0 / lb;
            F64(cur, 0x98) = ib * nby; F64(cur, 0xa0) = ib * bx;
            PTR(pr, 0x28) = s_new_seg(pr, prev, dup, flag);
            int kc = in_kind(k, j == 0 ? cnt - 1 : j);
            u8 *s = s_new(pr, dup, cur, flag && kc == -1);
            U8(s, 0x11) = 1;
            F64(s, 0x28) = 0.0;
            U8(s, 0x10) = 1;
            F64(s, 0x18) = F64(cur, 0); F64(s, 0x20) = F64(cur, 8);
            PTR(pr, 0x28) = s;
        }
        prev = cur;
    }
}

/* ------------------------------------------------------------------ equidistant points of three sites */
static void sol_put(HvSol *o, double x1, double y1, double r1, double x2, double y2, double r2, int k)
{
    o->x1 = x1; o->y1 = y1; o->r1 = r1;
    if (k == 1) { o->x2 = 0.0; o->y2 = 0.0; o->r2 = -1.0; o->n = 1; }
    else { o->x2 = x2; o->y2 = y2; o->r2 = r2; o->n = 2; }
}
/* FUN_180550790 (hv l.11655): segment s and two arcs p, q */
static void meet_seg_arcs(HvSol *o, u8 *s, u8 *p, u8 *q, double lim)
{
    double R1 = F64(p, 0x28), nx = F64(s, 0x18), ny = F64(s, 0x20);
    double r1, r2;
    if (R1 == 0.0 && F64(q, 0x28) == 0.0) {
        double dx = F64(q, 0x18) - F64(p, 0x18), dy = F64(q, 0x20) - F64(p, 0x20);
        double m[2];
        midp(m, (double *)(p + 0x18), (double *)(q + 0x18));
        double L2 = dx * dx + dy * dy;
        double d4 = dx * ny + -dy * nx;
        double d3 = (m[1] * ny + m[0] * nx) - F64(s, 0x28);
        double inv;
        if (!(sqrt(L2) == 0.0 || (inv = 1.0 / sqrt(L2), fabs(inv * d4) <= (double)0.0010000000474974513f))) {
            int k = quad(&r1, &r2, d4 * d4 - L2, d3 * d4, d3 * d3 - L2 * 0.25, inv * lim);
            if (k != 0) {
                sol_put(o, -dy * r1 + m[0], dx * r1 + m[1], r1 * d4 + d3, -dy * r2 + m[0], dx * r2 + m[1], r2 * d4 + d3, k);
                return;
            }
            sol_none(o);
            return;
        }
    }
    double c = F64(s, 0x28), qR = F64(q, 0x28);
    double d5 = (ny * F64(p, 0x20) + nx * F64(p, 0x18)) - c;
    double d4 = (R1 + d5) * 2.0;
    double d10 = -nx * F64(p, 0x20) + ny * F64(p, 0x18);
    double d8 = (F64(q, 0x20) * ny + nx * F64(q, 0x18)) - c;
    double d9 = -nx * F64(q, 0x20) + ny * F64(q, 0x18);
    double d7 = (qR + d8) * 2.0;
    if (d4 != 0.0 && d7 != 0.0) {
        double d5s = d5 * d5;
        int k = quad(&r1, &r2, d7 - d4, -d10 * d7 - -d9 * d4,
                     ((d10 * d10 - R1 * R1) + d5s) * d7 - ((d9 * d9 - qR * qR) + d8 * d8) * d4, lim);
        if (k != 0) {
            double e = (((r1 - d10) * (r1 - d10) - R1 * R1) + d5s) * (1.0 / d4);
            double f = (((r2 - d10) * (r2 - d10) - R1 * R1) + d5s) * (1.0 / d4);
            sol_put(o, (c + e) * nx + ny * r1, (c + e) * ny + -nx * r1, e,
                    (c + f) * nx + ny * r2, (c + f) * ny + -nx * r2, f, k);
            return;
        }
    }
    sol_none(o);
}
/* FUN_180550cb0 (hv l.11778): three arcs (point sites): the circumcentre */
static void meet_arcs(HvSol *o, u8 *a, u8 *b, u8 *c)
{
    double d1 = F64(a, 0x20) - F64(c, 0x20), d2 = F64(a, 0x18) - F64(c, 0x18);
    double d4 = F64(b, 0x20) - F64(c, 0x20), d3 = F64(b, 0x18) - F64(c, 0x18);
    if (-d4 * d2 + d3 * d1 == 0.0) { sol_none(o); return; }
    double d9 = d3 * d2 + d4 * d1;
    double d5 = d9 / (d2 * d2 + d1 * d1);
    d9 = d9 / (d4 * d4 + d3 * d3);
    if (d5 != 1.0 || d9 != 1.0) {
        double d8 = d5 - 1.0, d10 = d9 - 1.0, al, be;
        if (fabs(d10) <= fabs(d8)) {
            double d = (d10 / d8 + d9) * 2.0;
            if (d != 0.0) { al = 1.0 / d; be = 0.5 - al * d9; goto put; }
        } else {
            double d = (d8 / d10 + d5) * 2.0;
            if (d != 0.0) { be = 1.0 / d; al = 0.5 - be * d5; goto put; }
        }
        sol_none(o);
        return;
    put:;
        double X = d2 * al + d3 * be, Y = d1 * al + d4 * be;
        sol_put(o, X + F64(c, 0x18), Y + F64(c, 0x20), sqrt(Y * Y + X * X), 0, 0, 0, 1);
        return;
    }
    sol_none(o);
}
/* FUN_180550f40 (hv l.11862): parallel segments s1, s2 and a third site s3 (read as a segment) */
static void meet_par_seg(HvSol *o, u8 *s1, u8 *s2, u8 *s3)
{
    double ax = F64(s1, 0x18), ay = F64(s1, 0x20), ac = F64(s1, 0x28);
    double bx = F64(s2, 0x18), by = F64(s2, 0x20), bc = F64(s2, 0x28);
    double cx = F64(s3, 0x18), cy = F64(s3, 0x20), cc = F64(s3, 0x28);
    double d7 = by * ay + ax * bx;
    double d6 = ax * by + -ay * bx;
    double d2 = 1.0 / (d7 - 1.0);
    double d5 = d2 * d6;
    d2 = (ac - bc * d7) * d2;
    d7 = d5 * d6 + d7;
    double d8 = d2 + cc;
    double d9 = (bc + d2) * d6 - (ax * cy + -ay * cx);
    d6 = d2 - ((cy * ay + cx * ax) - ac);
    double d11 = (d9 * d7 + d6 * d5) - d8 * d5;
    double disc = d11 * d11 - ((d9 * d9 + d6 * d6) - d8 * d8) * d7 * d7;
    if (0.0 <= disc) {
        double d4 = 1.0 / (d7 * d7), s = sqrt(disc);
        double t1 = (-d11 - s) * d4, t2 = (s - d11) * d4;
        double d12 = t1 * d5 + d2, d3 = bc + d12;
        double e2 = t2 * d5 + d2, e1 = bc + e2;
        o->x1 = -by * t1 + d3 * bx; o->y1 = bx * t1 + d3 * by; o->r1 = d12;
        o->x2 = -by * t2 + e1 * bx; o->y2 = bx * t2 + e1 * by; o->r2 = e2;
        o->n = 2;
        return;
    }
    sol_none(o);
}

/* ------------------------------------------------------------------ the node between two edges */
/* FUN_18054d020 (hv l.9049, disassembly): along edge e, the right site's foot parameter at point p (x, y, r) minus the one
   held by e's start node; kind 3 edges: p's clearance */
static double e_tR(u8 *e, const double *p)
{
    if (I32(e, 0) == 3) return p[2];
    u8 *R = PTR(e, 0x30);
    return s_foot(R, p) - node_t(PTR(e, 0x40), R);
}
/* FUN_18054d0d0 (hv l.9084): node n (with feet ta / tb on e's left / right sites) lies on edge e between its start and end */
static int e_holds(u8 *e, double ta, double tb, u8 *n)
{
    if (I32(e, 0) == 3) return !(PTR(e, 0x38) && F64(PTR(e, 0x38), 0x10) < F64(n, 0x10));
    u8 *L = PTR(e, 0x28), *R = PTR(e, 0x30);
    u8 *st = PTR(e, 0x40);
    if (ta <= node_t(st, L) && node_t(st, R) <= tb) {
        u8 *en = PTR(e, 0x38);
        if (en) {
            if (ta < node_t(en, L) || node_t(en, R) < tb) return 0;
        }
        return 1;
    }
    return 0;
}
/* FUN_18054d1f0 (hv l.9166): the end node q of edge o lies closer to e's left (side 0) / right (side 1) site than its
   clearance; *flag set when it also lies behind that site */
static int e_end_closer(u8 *e, u8 *flag, u8 *o, int side)
{
    double *q = (double *)PTR(o, 0x38);
    if (I32(e, 0) == 3) {
        double lx, ly;
        if (side == 0) { lx = -F64(e, 0x20); ly = F64(e, 0x18); }
        else { lx = F64(e, 0x20); ly = -F64(e, 0x18); }
        return 0.0 <= (q[1] - F64(e, 0x10)) * ly + (q[0] - F64(e, 8)) * lx;
    }
    u8 *s;
    int b4, b5;
    double *os = (double *)PTR(o, 0x40);
    if (side == 0) {
        s = PTR(e, 0x28);
        b4 = s_dist(s, q, -1) < q[2];
        if (!b4) return 0;
        if (U8(s, 0x11) == 0) goto seg;
        if (I32(vstart(PTR(s, 0)), 0x18) == 9) return b4;
        if (I32(vend(PTR(s, 8)), 0x18) == 9) return b4;
        u8 *v = vend(PTR(s, 8));
        if (0.0 <= (os[1] - F64(s, 0x20)) * F64(v, 0xa0) + (os[0] - F64(s, 0x18)) * F64(v, 0x98)) return b4;
        b5 = (q[1] - F64(s, 0x20)) * F64(v, 0xa0) + (q[0] - F64(s, 0x18)) * F64(v, 0x98) < 0.0;
    } else {
        s = PTR(e, 0x30);
        b4 = s_dist(s, q, 1) < q[2];
        if (!b4) return 0;
        if (U8(s, 0x11) == 0) {
        seg:
            if (0.0 <= (F64(s, 0x20) * os[1] + F64(s, 0x18) * os[0]) - F64(s, 0x28)) return b4;
            b5 = (F64(s, 0x20) * q[1] + F64(s, 0x18) * q[0]) - F64(s, 0x28) < 0.0;
        } else {
            if (I32(vstart(PTR(s, 0)), 0x18) == 9) return b4;
            if (I32(vend(PTR(s, 8)), 0x18) == 9) return b4;
            u8 *v = vstart(PTR(s, 0));
            if (0.0 <= (os[1] - F64(s, 0x20)) * F64(v, 0xa0) + (os[0] - F64(s, 0x18)) * F64(v, 0x98)) return b4;
            b5 = (q[1] - F64(s, 0x20)) * F64(v, 0xa0) + (q[0] - F64(s, 0x18)) * F64(v, 0x98) < 0.0;
        }
    }
    if (b5) *flag = 1;
    return b4;
}
/* the side of point (x, y) along a segment site's line, as FUN_18054d4b0 tests it: 1 / -1 */
static int sgn_along(double ref, double nx, double ny, double x, double y) { return (ref < -nx * y + ny * x) ? 1 : -1; }
/* FUN_18054d4b0 (hv l.9267; the dispatch read in the disassembly 18054d69c-18054d8c5): the node where edges e1 and e2 (e1's
   right site = e2's left site) meet: nb gets the point, clearance, sites A / B / C (e1 left, shared, e2 right) with their
   foot parameters and kind 3 when valid (on both edges between their ends). p4: prefer the smaller clearance of two
   candidates; p5: which edge's parameter ranks them. Returns 1 when nb is a node (or a later node of the edges is ruled out). */
static int e_meet(u8 *nb, u8 *e1, u8 *e2, int p4, int p5, double lim, double tol)
{
    u8 *A = PTR(e1, 0x28), *B = PTR(e1, 0x30), *C = PTR(e2, 0x30);
    PTR(nb, 0x20) = A; PTR(nb, 0x30) = B; PTR(nb, 0x40) = C;
    if (C == A) return 0;
    PTR(nb, 0x60) = e1; PTR(nb, 0x78) = NULL; PTR(nb, 0x70) = NULL; PTR(nb, 0x68) = e2;
    int k1 = I32(e1, 0);
    if (k1 == 3 && I32(e2, 0) == 3) return 0;
    u8 res8 = 0;
    int c4, b3;
    u8 *e1end = PTR(e1, 0x38), *e2end = PTR(e2, 0x38);
    if (!e1end && !e2end) { c4 = 0; b3 = 0; }
    else {
        int b18 = 0;
        if (e1end) {
            if (node_t(e1end, B) > node_t(PTR(e2, 0x40), B)) b18 = 1;
            else b18 = e_end_closer(e2, &res8, e1, 1);
        }
        b3 = b18;
        if (e2end) {
            int al;
            if (node_t(e2end, B) < node_t(PTR(e1, 0x40), B)) al = 1;
            else al = e_end_closer(e1, &res8, e2, 0);
            b3 = e1end ? (b18 && al) : al;
        }
        c4 = 1;
    }
    HvSol sol;
    u8 *vC = vend(PTR(C, 8)), *vA = vstart(PTR(A, 0));
    int k2 = I32(e2, 0);
    if (vC == vA && ((I32(PTR(vC, 0x60), 0) - 1) & ~2) == 0 && k1 != 3 && k2 != 3)
        e_meet_site(PTR(vC, 0x60), &sol, B, lim);
    else if (k1 == 3) e_meet_site(e1, &sol, C, lim);
    else if (k2 == 3) e_meet_site(e2, &sol, A, lim);
    else if (k1 == 1) e_meet_site(e1, &sol, C, lim);
    else if (k2 == 1) e_meet_site(e2, &sol, A, lim);
    else if (U8(A, 0x11) == 0 && U8(C, 0x11) == 0) {
        u8 tmp[0x60];
        s_bisector(tmp, C, A, tol);
        if (I32(tmp, 0) == 2) meet_par_seg(&sol, A, C, B);
        else if (I32(tmp, 0) == 0) return c4 && b3;
        else e_meet_site(tmp, &sol, B, lim);
    } else if (k1 == 2) meet_par_seg(&sol, A, B, C);
    else if (k2 == 2) meet_par_seg(&sol, B, C, A);
    else if (U8(A, 0x11) == 0) meet_seg_arcs(&sol, A, B, C, lim);
    else if (U8(B, 0x11) == 0) meet_seg_arcs(&sol, B, C, A, lim);
    else if (U8(C, 0x11) == 0) meet_seg_arcs(&sol, C, A, B, lim);
    else meet_arcs(&sol, A, B, C);
    if (sol.n == 0) return c4 && b3;
    double s1[3] = { sol.x1, sol.y1, sol.r1 }, s2[3] = { sol.x2, sol.y2, sol.r2 };
    if (sol.n == 1) { F64(nb, 0) = sol.x1; F64(nb, 8) = sol.y1; F64(nb, 0x10) = sol.r1; }
    else {
        int b13;
        if (0.0 < sol.r1) {
            if (0.0 < sol.r2) {
                if (p4 == 0) {
                    if (U8(B, 0x11) != 0 && (U8(A, 0x11) == 0 || U8(C, 0x11) == 0)) {
                        int b17 = 0, b18 = 0, b19 = 0, b13x;
                        double bx = F64(B, 0x18), by = F64(B, 0x20);
                        if (U8(A, 0x11) == 0) {
                            double ny = F64(A, 0x20), nx = F64(A, 0x18);
                            double ref = -nx * by + ny * bx;
                            u8 *p = vstart(PTR(A, 0));
                            double *st = (double *)PTR(e1, 0x40);
                            int i14 = sgn_along(ref, nx, ny, F64(p, 0), F64(p, 8)) + sgn_along(ref, nx, ny, st[0], st[1]);
                            int i5 = sgn_along(ref, nx, ny, sol.x2, sol.y2);
                            int i11 = sgn_along(ref, nx, ny, sol.x1, sol.y1);
                            b18 = i14 != i11 - i5;
                            b17 = !(i14 == 0 || i11 == i5);
                        }
                        if (U8(C, 0x11) == 0) {
                            double ny = F64(C, 0x20), nx = F64(C, 0x18);
                            double ref = -nx * by + ny * bx;
                            u8 *p = vend(PTR(C, 8));
                            double *st = (double *)PTR(e2, 0x40);
                            int i14 = sgn_along(ref, nx, ny, F64(p, 0), F64(p, 8));
                            int i5 = sgn_along(ref, nx, ny, st[0], st[1]);
                            int i11 = sgn_along(ref, nx, ny, sol.x2, sol.y2);
                            int i16 = sgn_along(ref, nx, ny, sol.x1, sol.y1);
                            b13x = !(i14 + i5 == 0 || i16 == i11);
                            b19 = (i14 + i5) != (i16 - i11);
                            if (!b13x) goto dc3c;
                            if (b17) {
                                b13 = b18;
                                if (b18 == b19) goto pick;
                                goto dbc1;
                            }
                        } else {
                        dc3c:
                            if (!b17) goto dbc1;
                        }
                        b13 = b17 ? b18 : b19;
                        goto pick;
                    }
                dbc1:;
                    u8 *pe = p5 ? e1 : e2, *oe = p5 ? e2 : e1;
                    double d21 = e_tR(pe, s1), d22 = e_tR(pe, s2), d23 = e_tR(oe, s1), d24 = e_tR(oe, s2);
                    int b17, b18;
                    if (d21 <= 0.0) {
                    dc76:
                        if (d22 <= 0.0) { b13 = d21 < d22; goto pick; }
                        b18 = d21 == 0.0; b17 = d21 < 0.0;
                    } else if (d22 <= 0.0) {
                        b18 = d21 == 0.0; b17 = d21 < 0.0;
                        if (d21 <= 0.0) goto dc76;
                    } else {
                        if (0.0 < d23) {
                            if (0.0 < d24) { b13 = d22 < d21; goto pick; }
                            b18 = d23 == 0.0; b17 = d23 < 0.0;
                            if (!isnan(d23) && !b17 && !b18) goto dc8b;
                        }
                        if (d24 <= 0.0) { b13 = d21 < d22; goto pick; }
                        b18 = d23 == 0.0; b17 = d23 < 0.0;
                    }
                dc8b:
                    b13 = b17 || b18;
                } else b13 = sol.r2 < sol.r1;
            } else b13 = 0;
        } else b13 = 1;
    pick:;
        double *pk = b13 ? s2 : s1;
        F64(nb, 0) = pk[0]; F64(nb, 8) = pk[1]; F64(nb, 0x10) = pk[2];
    }
    double r = F64(nb, 0x10);
    if (r <= 0.0 || lim < r) return c4 && b3;
    u8 *E1 = PTR(nb, 0x60), *E2 = PTR(nb, 0x68);
    double *np = (double *)nb;
    if (I32(E1, 0) == 3) {
        F64(nb, 0x38) = 0.0; F64(nb, 0x28) = 1.0;
        F64(nb, 0x48) = s_foot(PTR(nb, 0x40), np);
    } else if (I32(E2, 0) != 3) {
        F64(nb, 0x28) = s_foot(PTR(nb, 0x20), np);
        F64(nb, 0x38) = s_foot(PTR(nb, 0x30), np);
        F64(nb, 0x48) = s_foot(PTR(nb, 0x40), np);
    } else {
        F64(nb, 0x28) = s_foot(PTR(nb, 0x20), np);
        F64(nb, 0x48) = 0.0; F64(nb, 0x38) = 1.0;
    }
    double tA = F64(nb, 0x28), tB = F64(nb, 0x38), tC = F64(nb, 0x48);
    if (tA < 0.0 || 1.0 < tA || tB < 0.0 || 1.0 < tB || tC < 0.0 || 1.0 < tC) {
        if (c4 && b3) {
            if (tA <= 1.0 && 0.0 <= F64(nb, 0x38) && F64(nb, 0x38) <= 1.0 && 0.0 <= F64(nb, 0x48)) I32(nb, 0x18) = 1;
            return 1;
        }
        return 0;
    }
    u8 *nA = PTR(nb, 0x20), *nB = PTR(nb, 0x30), *nC = PTR(nb, 0x40);
    u8 *st = PTR(E1, 0x40);
    if (st) {
        double a = node_t(st, nA), b = node_t(st, nB);
        if (a < tA && tA < a + 1e-06) F64(nb, 0x28) = a;
        if (tB < b && b - 1e-06 < tB) F64(nb, 0x38) = b;
    }
    st = PTR(E2, 0x40);
    if (st) {
        double b = node_t(st, nB), c = node_t(st, nC);
        double cb = F64(nb, 0x38);
        if (b < cb && cb < b + 1e-06) F64(nb, 0x38) = b;
        if (tC < c && c - 1e-06 < tC) F64(nb, 0x48) = c;
    }
    if (c4 && b3) {
        u8 *en = PTR(E1, 0x38);
        if (en) {
            double a = node_t(en, nA), b = node_t(en, nB);
            if (F64(nb, 0x28) < a && a - 1e-06 < F64(nb, 0x28)) F64(nb, 0x28) = a;
            double cb = F64(nb, 0x38);
            if (b < cb && cb < b + 1e-06) F64(nb, 0x38) = b;
        }
        en = PTR(E2, 0x38);
        if (en) {
            double b = node_t(en, nB), c = node_t(en, nC);
            if (F64(nb, 0x38) < b && b - 1e-06 < F64(nb, 0x38)) F64(nb, 0x38) = b;
            double cc = F64(nb, 0x48);
            if (c < cc && cc < c + 1e-06) F64(nb, 0x48) = c;
        }
    }
    double fb = F64(nb, 0x38);
    int ok = e_holds(E1, F64(nb, 0x28), fb, nb) && e_holds(E2, fb, F64(nb, 0x48), nb);
    if (ok) I32(nb, 0x18) = 3;
    if (!c4) return ok;
    if (!ok) return b3 ? 1 : 0;
    if (b3) return 1;
    I32(nb, 0x18) = 0;
    return 0;
}

/* ------------------------------------------------------------------ the incremental build */
static void nb_init(u8 *nb) { memset(nb, 0, 0x88); F64(nb, 0x10) = -1.0; I32(nb, 0x84) = -1; }
/* the node list: push_back a copy of nb (FUN_18040a150 + FUN_18040a9a0), then retire the later nodes of its two edges
   (the loop of FUN_1805477e0 hv l.4838, also inline in FUN_1805487a0 and FUN_180548c00) */
static u8 *node_add(u8 *nb)
{
    u8 *n = ls_push_back(G_NODES, 0x88, (int64_t *)(G + 0x60));
    u8 *p = n + 0x10;
    memcpy(p, nb, 0x88);
    for (int k = 0; k < 2; k++) {
        u8 *e = PTR(p, 0x60 + 8 * k);
        u8 *q = PTR(e, 0x38);
        while (q) {
            u8 *x = PTR(q, 0x78);
            I32(q, 0x18) = 0;
            PTR(PTR(q, 0x60), 0x38) = NULL;
            PTR(PTR(q, 0x68), 0x38) = NULL;
            if (!x) break;
            q = PTR(x, 0x38);
            PTR(x, 0x40) = NULL;
            I32(x, 0) = 0;
        }
        PTR(e, 0x38) = p;
    }
    return p;
}
/* the event queue of FUN_1805494d0: a list of node pointers by clearance; FUN_180549430 (hv l.6257) inserts before the
   first with a larger clearance */
typedef struct { u8 *head; int64_t n; } HvQ;
static void q_insert(HvQ *q, u8 *node)
{
    u8 *it = PTR(q->head, 0);
    double r = F64(node, 0x10);
    while (it != q->head) {
        if (r < F64(PTR(it, 0x10), 0x10)) break;
        it = PTR(it, 0);
    }
    u8 *n = hv_alloc(0x18);
    PTR(n, 0x10) = node;
    u8 *prev = PTR(it, 8);
    PTR(n, 0) = it; PTR(n, 8) = prev;
    PTR(it, 8) = n; PTR(prev, 0) = n;
    q->n++;
}
static u8 *q_front(HvQ *q) { return PTR(PTR(q->head, 0), 0x10); }
static u8 *q_pop(HvQ *q) { u8 *n = PTR(q->head, 0); u8 *p = PTR(n, 0x10); ls_unlink(n, &q->n); return p; }
/* FUN_1805487a0 (hv l.5634): the first events: round the boundary from the start vertex, the node of each vertex's edge with
   the next vertex's edge (where the site between them is a segment, or a bridge vertex is involved) */
static void q_first(HvQ *q)
{
    u8 *v = PTR(G, 0x50);
    for (;;) {
        u8 *s = PTR(v, 0x30);
        u8 *w = vend(PTR(s, 8));
        if (U8(s, 0x11) == 0 || (F64(s, 0x28) <= 0.0 && F64(s, 0x28) != 0.0) || I32(v, 0x18) == 9 || I32(w, 0x18) == 9) {
            u8 nb[0x88];
            nb_init(nb);
            e_meet(nb, PTR(v, 0x60), PTR(w, 0x60), 1, 0, F64(G, 0x78), F64(G, 0x80));
            if (I32(nb, 0x18) == 3) q_insert(q, node_add(nb));
        }
        v = w;
        if (w == PTR(G, 0x50)) return;
    }
}
/* FUN_180548a00 (hv l.5747): walking up from node *pn along the leaving edges, the first node farther from edge e's left
   (side 0) / right (side 1) site than its clearance: 1 (with *pn = that node); 0 when *pn itself is not closer than its
   clearance or none is */
static int walk_farther(u8 *e, u8 **pn, int side)
{
    u8 *n = *pn;
    u8 *s0 = side ? PTR(e, 0x30) : PTR(e, 0x28);
    double d = s_dist(s0, (double *)n, side ? 1 : -1);
    if (F64(n, 0x10) < d) return 0;
    u8 *x = PTR(n, 0x78);
    while (x) {
        u8 *m = PTR(x, 0x38);
        *pn = m;
        if (!m) break;
        double *pm = (double *)m, dist;
        u8 *s = side ? PTR(e, 0x30) : PTR(e, 0x28);
        if (U8(s, 0x11) != 0) {
            double dy = pm[1] - F64(s, 0x20), dx = pm[0] - F64(s, 0x18);
            dist = fabs(sqrt(dy * dy + dx * dx) - F64(s, 0x28));
        } else {
            double *a = (double *)PTR(s, 0), *b = (double *)PTR(s, 8);
            double sy = b[1] - a[1], sx = b[0] - a[0];
            double t = ((pm[1] - a[1]) * sy + (pm[0] - a[0]) * sx) / (sy * sy + sx * sx);
            double *c = NULL;
            if (side == 0) { if (!(t <= 1.0)) c = b; }
            else { if (t < 0.0) c = a; }
            if (c) dist = sqrt((pm[1] - c[1]) * (pm[1] - c[1]) + (pm[0] - c[0]) * (pm[0] - c[0]));
            else dist = fabs((pm[1] * F64(s, 0x20) + pm[0] * F64(s, 0x18)) - F64(s, 0x28));
        }
        if (pm[2] < dist) return 1;
        x = PTR(m, 0x78);
    }
    return 0;
}
/* the edge record between sites a and b leaving node / vertex from (FUN_1805459f0 + FUN_180547500): +0x28 / +0x30 the sites,
   +0x38 end 0, +0x40 from, +0x50 0, +0x58 -1 */
static u8 *edge_add(u8 *a, u8 *b, u8 *from)
{
    u8 *n = ls_push_back(G_EDGES, 0x60, (int64_t *)(G + 0x70));
    u8 *e = n + 0x10;
    s_bisector(e, a, b, F64(G, 0x80));
    PTR(e, 0x28) = a; PTR(e, 0x30) = b;
    I32(e, 0x58) = -1;
    PTR(e, 0x38) = NULL; PTR(e, 0x40) = from;
    return e;
}
/* FUN_180548c00 (hv l.5843): the events in clearance order. Each live node N (sites A, B, C) starts the edge between A and C;
   its end is the first node met walking from A's start vertex up the left edges, or from C's end vertex up the right edges
   (the lower of the two when both are found); the last node closes the boundary. Returns 1 when the diagram closes. */
static int q_run(HvQ *q)
{
    double dprev = -1.0;
    u8 *N, *E, *L;
    while (q->n != 0) {
        while (q->n != 0 && I32(q_front(q), 0x18) == 0) q_pop(q);
        if (q->n == 0) break;
        N = q_pop(q);
        if (q->n == 0) {
        d98:
            if (vstart(PTR(PTR(N, 0x20), 0)) == vend(PTR(PTR(N, 0x40), 8))) {
            d6b:
                if (q->n != 0) {
                    L = q_pop(q);
                    while (q->n != 0) {
                        if (I32(q_front(q), 0x18) != 0) return 0;
                        q_pop(q);
                    }
                    goto f1;
                }
                E = PTR(vstart(PTR(PTR(N, 0x20), 0)), 0x60);
                goto close;
            }
        } else {
            while (q->n != 0 && I32(q_front(q), 0x18) == 0) q_pop(q);
            if (q->n == 0) goto d98;
            u8 *F = q_front(q);
            if (PTR(N, 0x20) == PTR(F, 0x40) && PTR(N, 0x40) == PTR(F, 0x20)) goto d6b;
        }
        E = edge_add(PTR(N, 0x20), PTR(N, 0x40), N);
        PTR(N, 0x78) = E;
        u8 nb1[0x88], nb2[0x88];
        nb_init(nb1);
        u8 *x = PTR(vstart(PTR(PTR(E, 0x28), 0)), 0x60);
        if (x) {
            for (;;) {
                u8 *m = PTR(x, 0x38);
                if (PTR(x, 0x28) == PTR(E, 0x30)) break;
                if (e_meet(nb1, x, E, 0, 1, F64(G, 0x78), F64(G, 0x80))) {
                    if (I32(nb1, 0x18) != 1 || !m || !walk_farther(E, &m, 1)) break;
                    I32(nb1, 0x18) = 0;
                }
                if (!m || !(x = PTR(m, 0x78))) break;
            }
        }
        nb_init(nb2);
        int k2 = 0;
        x = PTR(vend(PTR(PTR(E, 0x30), 8)), 0x60);
        if (x) {
            for (;;) {
                u8 *m = PTR(x, 0x38);
                k2 = I32(nb2, 0x18);
                if (PTR(x, 0x30) == PTR(E, 0x28)) break;
                int r = e_meet(nb2, E, x, 0, 0, F64(G, 0x78), F64(G, 0x80));
                k2 = I32(nb2, 0x18);
                if (r) {
                    if (k2 != 1 || !m || !walk_farther(E, &m, 0)) break;
                    I32(nb2, 0x18) = 0;
                }
                k2 = I32(nb2, 0x18);
                if (!m || !(x = PTR(m, 0x78))) break;
            }
        }
        if (I32(nb1, 0x18) == 3) {
            if (k2 == 3) {
                double r1 = F64(nb1, 0x10), r2 = F64(nb2, 0x10);
                if (I32(E, 0) != 3) {
                    r1 = node_t(nb1, PTR(E, 0x30));
                    r2 = node_t(nb2, PTR(E, 0x30));
                }
                if (r2 <= r1) { q_insert(q, node_add(nb2)); continue; }
            }
            q_insert(q, node_add(nb1));
        } else if (k2 == 3) q_insert(q, node_add(nb2));
    }
    return 0;
f1:
    E = edge_add(PTR(L, 0x20), PTR(L, 0x40), L);
    PTR(L, 0x78) = E;
close:
    if (I32(E, 0) == 3) dprev = F64(N, 0x10);
    else {
        u8 *R = PTR(E, 0x30);
        if (PTR(N, 0x20) == R) dprev = F64(N, 0x28);
        else if (PTR(N, 0x30) == R) dprev = F64(N, 0x38);
        else if (PTR(N, 0x40) == R) dprev = F64(N, 0x48);
    }
    if (0.0 <= dprev) {
        PTR(E, 0x38) = N;
        PTR(N, 0x78) = E;
        return 1;
    }
    return 0;
}
/* FUN_1805494d0 (hv l.6295): the first events, the run, then every live edge must have both ends */
static int build_incr(void)
{
    HvQ q;
    q.head = ls_new(8); q.n = 0;
    q_first(&q);
    int ok = q_run(&q);
    int bad = 0;
    for (u8 *n = PTR(G_EDGES, 0); n != G_EDGES; n = PTR(n, 0)) {
        u8 *e = n + 0x10;
        if (I32(e, 0) != 0 && (PTR(e, 0x38) == NULL || PTR(e, 0x40) == NULL)) bad++;   /* "Tail unlinked!!!" */
    }
    return ok && bad == 0;   /* else "Un-linked edges were found in voidiagram " */
}

/* ------------------------------------------------------------------ bottlenecks */
/* FUN_18054fbb0 (hv l.11122): edge e is kind 4 and node n has a kind-4 edge, a kind-3 edge and a kind-2 or -5 edge among
   +0x78 / +0x60 / +0x68 */
static int n_mixed(u8 *n, u8 *e)
{
    if (I32(e, 0) != 4) return 0;
    int h4 = 0, h3 = 0, h25 = 0;
    for (int k = 0; k < 3; k++) {
        static const int off[3] = { 0x78, 0x60, 0x68 };
        int kd = I32(PTR(n, off[k]), 0);
        if (kd == 4) h4 = 1;
        else if (kd == 3) h3 = 1;
        else if (kd == 2 || kd == 5) h25 = 1;
    }
    return h4 && h3 && h25;
}
/* FUN_18054f480 (hv l.10823, disassembly 18054f480-18054fbae): the narrowest point of edge e (kinds 2, 4, 5: between two
   sites facing each other) as a node of kind 6 in nb: 1 when it lies strictly inside the edge (a bottleneck); 0 otherwise
   (and e +0x50 = its clearance when it was found too close to an end, sbs = VoronoiSmallBisectorSplit) */
static int bottleneck(u8 *nb, u8 *e, double sbs)
{
    int k = I32(e, 0);
    F64(e, 0x50) = -1.0;
    double x, y, r;
    if (k == 2) {
        u8 *st = PTR(e, 0x40), *en = PTR(e, 0x38);
        r = F64(en, 0x10);
        if (r != F64(st, 0x10)) r = r * 0.5 + F64(st, 0x10) * 0.5;
        double p[2];
        lerpf(p, 0.5f, (double *)en, (double *)st);
        x = p[0]; y = p[1];
    } else if (k == 4) {
        u8 *L = PTR(e, 0x28), *R = PTR(e, 0x30);
        u8 *S, *Q;   /* the segment and the arc */
        if (U8(L, 0x11) == 0) { S = L; Q = R; }
        else if (U8(R, 0x11) != 0) {
            double dy = F64(R, 0x20) - F64(L, 0x20), dx = F64(R, 0x18) - F64(L, 0x18);
            double D = sqrt(dy * dy + dx * dx);
            r = (D - F64(R, 0x28)) - F64(L, 0x28);
            if (r < 0.0) return 0;
            r = r * 0.5;
            double p[2];
            lerp(p, (r + F64(L, 0x28)) / D, (double *)(L + 0x18), (double *)(R + 0x18));
            x = p[0]; y = p[1];
            goto found;
        } else { S = R; Q = L; }
        double cx = F64(Q, 0x18), cy = F64(Q, 0x20), QR = F64(Q, 0x28), c = F64(S, 0x28);
        double d = (cy * F64(S, 0x20) + cx * F64(S, 0x18)) - c;
        double fx = cx - d * F64(S, 0x18), fy = cy - d * F64(S, 0x20);
        d = (cy * F64(S, 0x20) + cx * F64(S, 0x18)) - c;
        r = d - QR;
        if (r < 0.0) return 0;
        r = r * 0.5;
        double t = r / d;
        if (fx == cx && fy == cy) { x = fx; y = fy; }
        else { double u = 1.0 - t; x = fx * u + cx * t; y = fy * u + cy * t; }
    } else if (k == 5) {
        u8 *L = PTR(e, 0x28);
        double rL = F64(L, 0x28), rR = F64(PTR(e, 0x30), 0x28);
        double m[2];
        lerpf(m, 0.5f, (double *)PTR(e, 0x40), (double *)PTR(e, 0x38));
        r = fabs(rL - rR) * 0.5;
        double dx = m[0] - F64(L, 0x18), dy = m[1] - F64(L, 0x20);
        double s = r / sqrt(dy * dy + dx * dx);
        x = dx * s + F64(L, 0x18); y = dy * s + F64(L, 0x20);
    } else return 0;
found:
    F64(nb, 0x10) = r; I32(nb, 0x18) = 6;
    F64(nb, 0) = x; F64(nb, 8) = y;
    u8 *L = PTR(e, 0x28), *R = PTR(e, 0x30);
    PTR(nb, 0x20) = L; PTR(nb, 0x30) = R; PTR(nb, 0x60) = e;
    double tA = s_foot(L, (double *)nb);
    F64(nb, 0x28) = tA;
    double tB = s_foot(R, (double *)nb);
    F64(nb, 0x38) = tB;
    u8 *st = PTR(e, 0x40), *en = PTR(e, 0x38);
    double loL = node_t(st, L), hiL = loL, z = node_t(en, L);
    if (z < loL) loL = z; else if (z > loL) hiL = z;
    double loR = node_t(st, R), hiR = loR;
    z = node_t(en, R);
    if (z < loR) loR = z; else if (z > loR) hiR = z;
    int inL = (tA >= loL && !(tA > hiL)), inR = (tB >= loR && !(tB > hiR));
    int m_en = n_mixed(en, e), m_st = n_mixed(st, e);
    if (!inL || !inR) return 0;
    double dyE = F64(nb, 8) - F64(en, 8), dxE = F64(nb, 0) - F64(en, 0);
    double dyS = F64(nb, 8) - F64(st, 8), dxS = F64(nb, 0) - F64(st, 0);
    double dS = sqrt(dyS * dyS + dxS * dxS), dE = sqrt(dyE * dyE + dxE * dxE);
    if (dS >= sbs && dE >= sbs) return 1;
    if (m_en || m_st || dS == 0.0 || dE == 0.0 || tA <= loL || tA >= hiL || tB <= loR || tB >= hiR) goto near_end;
    {
        double eps = 2.220446049250313e-15;
        double a = fabs(tA), b = fabs(1.0 - tA), m = (a <= b) ? a : b;
        if (eps <= m) return 1;
        a = fabs(tB); b = fabs(1.0 - tB); m = (a <= b) ? a : b;
        if (eps > m) goto near_end;
        return 1;
    }
near_end:
    F64(e, 0x50) = F64(nb, 0x10);
    return 0;
}
/* the incoming-edge count test of FUN_1805478a0: edge x counts at node n when it is a bottleneck edge or ends at n */
static int in_at(u8 *x, u8 *n) { return I32(x, 0x48) == 1 || PTR(x, 0x38) == n; }
/* FUN_1805478a0 (hv l.4886): the bottleneck nodes of the kind 2 / 4 / 5 edges (list +0xa0); the other edges are turned to
   run from the lower clearance up; then every node must have two incoming edges: an edge whose far end is no more than
   VoronoiSmallBisectorLength off in clearance and is complete there is turned; 3 passes ("iError ... Flipped ... on attempt") */
static int bottlenecks(void)
{
    for (u8 *n = PTR(G_EDGES, 0); n != G_EDGES; n = PTR(n, 0)) {
        u8 *e = n + 0x10;
        int k = I32(e, 0);
        if (k == 0) continue;
        if (((unsigned)(k - 2) & ~3u) == 0 && k != 3) {
            u8 nb[0x88];
            nb_init(nb);
            if (bottleneck(nb, e, F64(G, 0x90))) {
                u8 *b = ls_push_back(G_BNECK, 0x88, (int64_t *)(G + 0xa8));
                memcpy(b + 0x10, nb, 0x88);
                I32(e, 0x48) = 1;
                continue;
            }
        }
        I32(e, 0x48) = 0;
        u8 *st = PTR(e, 0x40), *en = PTR(e, 0x38);
        if (1u < (unsigned)(I32(st, 0x18) - 7) && F64(en, 0x10) < F64(st, 0x10)) {
            PTR(e, 0x38) = st; PTR(e, 0x40) = en;
            u8 *t = PTR(e, 0x28); PTR(e, 0x28) = PTR(e, 0x30); PTR(e, 0x30) = t;
        }
    }
    for (int attempt = 0;;) {
        int errors = 0;
        for (u8 *ln = PTR(G_NODES, 0); ln != G_NODES; ln = PTR(ln, 0)) {
            u8 *N = ln + 0x10;
            if (I32(N, 0x18) != 3) continue;
            unsigned s = in_at(PTR(N, 0x60), N) + in_at(PTR(N, 0x68), N) + in_at(PTR(N, 0x78), N);
            if (s >= 2) continue;
            errors++;
            static const int off[3] = { 0x60, 0x68, 0x78 };
            for (int k = 0; k < 3 && s < 2; k++) {
                u8 *x = PTR(N, off[k]);
                u8 *m = PTR(x, 0x38);
                if (m != N && I32(x, 0x48) != 1 && fabs(F64(m, 0x10) - F64(PTR(x, 0x40), 0x10)) <= F64(G, 0x88)) {
                    if (in_at(PTR(m, 0x60), m) + in_at(PTR(m, 0x68), m) + in_at(PTR(m, 0x78), m) == 3) {
                        PTR(x, 0x38) = PTR(x, 0x40); PTR(x, 0x40) = m;
                        u8 *t = PTR(x, 0x28); PTR(x, 0x28) = PTR(x, 0x30); PTR(x, 0x30) = t;
                        s++;
                    }
                }
            }
        }
        if (errors == 0) return 1;
        attempt++;
        if (2 < attempt) return 0;
    }
}
/* FUN_180546b90 (hv l.4310): site s split at the foot of point p: a vertex of kind 10 there (normal towards p) in polygon
   record pr; s dies, two sites of its geometry and flag take its place */
static u8 *s_split(u8 *pr, u8 *s, const double *p)
{
    double fx, fy;
    if (U8(s, 0x11) == 0) {
        double d = (p[1] * F64(s, 0x20) + p[0] * F64(s, 0x18)) - F64(s, 0x28);
        fy = p[1] - d * F64(s, 0x20);
        fx = p[0] - d * F64(s, 0x18);
    } else {
        double dy = p[1] - F64(s, 0x20), dx = p[0] - F64(s, 0x18);
        double k = F64(s, 0x28) / sqrt(dy * dy + dx * dx);
        fx = dx * k + F64(s, 0x18);
        fy = dy * k + F64(s, 0x20);
    }
    double f[2] = { fx, fy };
    u8 *v = v_new(pr, f);
    double dx = p[0] - fx, dy = p[1] - fy;
    double len = sqrt(dy * dy + dx * dx);
    if (len == 0.0) len = 1.0;
    I32(v, 0x18) = 10;
    F64(v, 0x98) = (1.0 / len) * dx; F64(v, 0xa0) = (1.0 / len) * dy;
    u8 *a = vstart(PTR(s, 0)), *b = vend(PTR(s, 8));
    double n0 = F64(s, 0x18), n1 = F64(s, 0x20), c = F64(s, 0x28);
    u8 arc = U8(s, 0x11), flag = U8(s, 0x5c);
    PTR(vstart(PTR(s, 0)), 0x30) = NULL;
    PTR(vend(PTR(s, 8)), 0x20) = NULL;
    PTR(s, 0) = NULL; PTR(s, 8) = NULL; U8(s, 0x10) = 0;
    u8 *s1 = s_new(pr, a, v, flag), *s2 = s_new(pr, v, b, flag);
    U8(s1, 0x11) = arc; U8(s1, 0x10) = 1; F64(s1, 0x28) = c; F64(s1, 0x18) = n0; F64(s1, 0x20) = n1;
    F64(s2, 0x28) = c; F64(s2, 0x18) = n0; F64(s2, 0x20) = n1; U8(s2, 0x11) = arc; U8(s2, 0x10) = 1;
    return v;
}
/* the vertex edge record of FUN_180549ff0 / FUN_180547f10 / FUN_180547410: v's bisector (FUN_180545ce0) between its entering
   and leaving sites, starting at v; v +0x60 = it */
static u8 *edge_of_vertex(u8 *v)
{
    u8 *n = ls_push_back(G_EDGES, 0x60, (int64_t *)(G + 0x70));
    u8 *e = n + 0x10;
    v_bisector(e, v);
    PTR(e, 0x28) = PTR(v, 0x20); PTR(e, 0x30) = PTR(v, 0x30);
    PTR(e, 0x38) = NULL; PTR(e, 0x40) = v; I32(e, 0x48) = -1; PTR(e, 0x50) = NULL; I32(e, 0x58) = -1;
    PTR(v, 0x60) = e;
    return e;
}
/* replace edge old by edge new among node n's edge slots (+0x78, +0x60, +0x68, +0x70 in that order) */
static void n_replace(u8 *n, u8 *old, u8 *nw)
{
    if (PTR(n, 0x78) == old) PTR(n, 0x78) = nw;
    else if (PTR(n, 0x60) == old) PTR(n, 0x60) = nw;
    else if (PTR(n, 0x68) == old) PTR(n, 0x68) = nw;
    else if (PTR(n, 0x70) == old) PTR(n, 0x70) = nw;
}
static int g_abort;   /* the DLL's assert "voronoi\VoronoiFunctions.cpp" 0x375 (FUN_1802e24f0 does not return): the build fails */
/* the relabelling walks of FUN_180547f10 / FUN_1805496d0: from edge x0 round the nodes up to edge stop, site s replaces
   the edges' left (walk_l: entering from a site's end vertex) or right (walk_r: from a site's start vertex) site and the
   nodes' site slots with their foot parameters */
static void walk_l(u8 *s, u8 *x, u8 *stop)
{
    int b16 = 1;
    while (x != stop) {
        u8 *m;
        if (b16) { m = PTR(x, 0x38); PTR(x, 0x28) = s; }
        else { PTR(x, 0x30) = s; m = PTR(x, 0x40); }
        double t;
        if (I32(x, 0) == 3) t = 1.0;
        else { t = s_foot(s, (double *)m); if (t < 0.0) t = 0.0; else if (1.0 < t) t = 1.0; }
        int nk = 1u < (unsigned)(I32(m, 0x18) - 4);
        u8 *nx;
        if (x == PTR(m, 0x78)) {
            if (nk) { F64(m, 0x48) = t; PTR(m, 0x40) = s; nx = PTR(m, 0x68); }
            else { F64(m, 0x58) = t; PTR(m, 0x50) = s; nx = PTR(m, 0x70); }
        } else if (x == PTR(m, 0x60)) { F64(m, 0x28) = t; PTR(m, 0x20) = s; nx = PTR(m, 0x78); }
        else if (x == PTR(m, 0x68)) { F64(m, 0x38) = t; PTR(m, 0x30) = s; nx = PTR(m, 0x60); }
        else {
            if (nk || x != PTR(m, 0x70)) { g_abort = 1; return; }
            F64(m, 0x48) = t; PTR(m, 0x40) = s; nx = PTR(m, 0x68);
        }
        b16 = PTR(nx, 0x40) == m;
        x = nx;
    }
}
static void walk_r(u8 *s, u8 *x, u8 *stop)
{
    int b16 = 1;
    while (x != stop) {
        u8 *m;
        if (b16) { m = PTR(x, 0x38); PTR(x, 0x30) = s; }
        else { m = PTR(x, 0x40); PTR(x, 0x28) = s; }
        double t;
        if (I32(x, 0) == 3) t = 0.0;
        else { t = s_foot(s, (double *)m); if (t < 0.0) t = 0.0; else if (1.0 < t) t = 1.0; }
        u8 *nx = PTR(m, 0x78);
        int nk = 1u < (unsigned)(I32(m, 0x18) - 4);
        if (x == nx) { nx = PTR(m, 0x60); F64(m, 0x28) = t; PTR(m, 0x20) = s; }
        else if (x == PTR(m, 0x60)) { nx = PTR(m, 0x68); F64(m, 0x38) = t; PTR(m, 0x30) = s; }
        else if (x == PTR(m, 0x68)) { F64(m, 0x48) = t; PTR(m, 0x40) = s; if (!nk) nx = PTR(m, 0x70); }
        else {
            if (nk || x != PTR(m, 0x70)) { g_abort = 1; return; }
            F64(m, 0x58) = t; PTR(m, 0x50) = s;
        }
        b16 = PTR(nx, 0x40) == m;
        x = nx;
    }
}
/* FUN_180547f10 (hv l.5183): each bottleneck node B (on edge E between sites L and R) splits L and R at its feet (vertices
   of kind 8 with their own edges ending at B); E dies, replaced by Ea (sites R2 / L2, from B down to E's start) and Eb (L1 /
   R1, from B up to E's end); the split pieces take over the edges and nodes round them; B becomes kind 5 */
static void split_bottlenecks(void)
{
    u8 *pr = PTR(G, 8) - 0x48;   /* the extra polygon record */
    for (u8 *ln = PTR(G_BNECK, 0); ln != G_BNECK; ln = PTR(ln, 0)) {
        u8 *B = ln + 0x10;
        u8 *E = PTR(B, 0x60);
        u8 *R = PTR(E, 0x30);
        u8 *v1 = s_split(pr, PTR(E, 0x28), (double *)B);
        I32(v1, 0x18) = 8;
        PTR(B, 0x30) = PTR(v1, 0x20); PTR(B, 0x40) = PTR(v1, 0x30); F64(B, 0x38) = 1.0; F64(B, 0x48) = 0.0;
        u8 *e1 = edge_of_vertex(v1);
        PTR(B, 0x68) = e1; PTR(e1, 0x38) = B;
        u8 *v2 = s_split(pr, R, (double *)B);
        I32(v2, 0x18) = 8;
        PTR(B, 0x50) = PTR(v2, 0x20); PTR(B, 0x20) = PTR(v2, 0x30); F64(B, 0x58) = 1.0; F64(B, 0x28) = 0.0;
        u8 *e2 = edge_of_vertex(v2);
        PTR(B, 0x78) = e2; PTR(e2, 0x38) = B;
        u8 *Ea = edge_add(PTR(B, 0x50), PTR(B, 0x40), NULL);
        u8 *Eb = edge_add(PTR(B, 0x30), PTR(B, 0x20), NULL);
        I32(Ea, 0x48) = -1; I32(Eb, 0x48) = -1;   /* the record buffer's stale +0x48 of the vertex edge just made */
        u8 *S = PTR(E, 0x40);
        PTR(Ea, 0x38) = S;
        n_replace(S, E, Ea);
        PTR(Ea, 0x40) = B; PTR(B, 0x70) = Ea;
        PTR(Eb, 0x40) = B; PTR(B, 0x60) = Eb;
        u8 *T = PTR(E, 0x38);
        PTR(Eb, 0x38) = T;
        n_replace(T, E, Eb);
        I32(E, 0) = 0;
        u8 *s = PTR(B, 0x40);
        walk_l(s, PTR(vend(PTR(s, 8)), 0x60), PTR(B, 0x70));
        s = PTR(B, 0x30);
        walk_r(s, PTR(vstart(PTR(s, 0)), 0x60), PTR(B, 0x60));
        s = PTR(B, 0x20);
        walk_l(s, PTR(vend(PTR(s, 8)), 0x60), PTR(B, 0x60));
        s = PTR(B, 0x50);
        walk_r(s, PTR(vstart(PTR(s, 0)), 0x60), PTR(B, 0x70));
        if (g_abort) return;
        I32(B, 0x18) = 5;
    }
}
/* FUN_18054b290 (hv l.7739): each node's edge slots turned so +0x60 is the edge leaving it (kind 2 when none does); each
   kind 4 edge's byte +0x5c = which side of its sites' line its end node lies */
static void orient(void)
{
    for (u8 *ln = PTR(G_NODES, 0); ln != G_NODES; ln = PTR(ln, 0)) {
        u8 *N = ln + 0x10;
        if (I32(N, 0x18) != 3) continue;
        u8 *a = PTR(N, 0x60), *b = PTR(N, 0x68), *c = PTR(N, 0x78);
        if (PTR(b, 0x40) == N) { PTR(N, 0x60) = b; PTR(N, 0x68) = c; PTR(N, 0x78) = a; }
        else if (PTR(c, 0x40) == N) { PTR(N, 0x60) = c; PTR(N, 0x78) = b; PTR(N, 0x68) = a; }
        else if (PTR(a, 0x40) != N) I32(N, 0x18) = 2;
    }
    for (u8 *ln = PTR(G_EDGES, 0); ln != G_EDGES; ln = PTR(ln, 0)) {
        u8 *e = ln + 0x10;
        if (I32(e, 0) != 4) continue;
        u8 *L = PTR(e, 0x28), *R = PTR(e, 0x30);
        double d5, d6, d7, d8;
        if (U8(L, 0x11) == 0 || U8(R, 0x11) != 0) {
            if (U8(L, 0x11) == 0) {
                d5 = -F64(L, 0x20); d7 = F64(L, 0x18);
                d6 = d5 * F64(R, 0x18); d8 = d7 * F64(R, 0x20);
            } else {
                d7 = F64(R, 0x18) - F64(L, 0x18);
                d5 = -(F64(R, 0x20) - F64(L, 0x20));
                d8 = d7 * F64(R, 0x20); d6 = d5 * F64(R, 0x18);
            }
        } else {
            d5 = -F64(R, 0x20); d7 = F64(R, 0x18);
            d6 = d5 * F64(L, 0x18); d8 = d7 * F64(L, 0x20);
        }
        double *en = (double *)PTR(e, 0x38);
        U8(e, 0x5c) = d8 + d6 < d7 * en[1] + d5 * en[0];
    }
}
/* FUN_18054b150 (hv l.7656): each live site's first node (+0x38: up from its start vertex while the edges keep it on their
   right) and the first node beyond the kind-3 chain (+0x40) */
static void site_nodes(void)
{
    int np = I32(G, 0x18);
    for (int k = 0; k <= np; k++) {
        u8 *pr = PTR(G, 0) + 0x48 * k;
        for (u8 *ln = PTR(PTR(pr, 0x10), 0); ln != PTR(pr, 0x10); ln = PTR(ln, 0)) {
            u8 *S = ln + 0x10;
            if (U8(S, 0x10) == 0) continue;
            u8 *v = vstart(PTR(S, 0));
            u8 *n5 = PTR(PTR(v, 0x60), 0x38);
            if (I32(n5, 0x18) == 5) {
                u8 *e = (PTR(v, 0x60) == PTR(n5, 0x78)) ? PTR(n5, 0x60) : PTR(n5, 0x70);
                n5 = PTR(e, 0x38);
            }
            while (I32(n5, 0x18) != 2 && PTR(PTR(n5, 0x60), 0x30) == S) n5 = PTR(PTR(n5, 0x60), 0x38);
            u8 *w = vend(PTR(S, 8));
            u8 *n6 = PTR(PTR(w, 0x60), 0x38);
            if (I32(n6, 0x18) == 5) {
                u8 *e = (PTR(w, 0x60) == PTR(n6, 0x78)) ? PTR(n6, 0x70) : PTR(n6, 0x60);
                n6 = PTR(e, 0x38);
            }
            while (I32(n6, 0x18) != 2 && PTR(PTR(n6, 0x60), 0x28) == S) n6 = PTR(PTR(n6, 0x60), 0x38);
            PTR(S, 0x38) = n5;
            while (I32(n5, 0x18) == 3) n5 = PTR(PTR(n5, 0x60), 0x38);
            PTR(S, 0x40) = n5;
        }
    }
}

/* ------------------------------------------------------------------ the perturbation cap (FUN_18054e170) */
/* MSVC 2013 std::sort of point indices by x (FUN_180551950: introsort, FUN_1805525e0 median of 3 / ninther above 40,
   FUN_180551cd0 the three-way partition, FUN_1805529e0 insertion sort up to 32, FUN_180552440 / FUN_1805524e0 heap sort) */
static const double *g_sx;   /* x of point index i: g_sx[i] */
static int lt(int a, int b) { return g_sx[a] < g_sx[b]; }
static void iswap(int *a, int *b) { int t = *a; *a = *b; *b = t; }
static void med3(int *f, int *m, int *l)
{
    if (lt(*m, *f)) iswap(m, f);
    if (lt(*l, *m)) { iswap(l, m); if (lt(*m, *f)) iswap(m, f); }
}
static void median(int *f, int *m, int *l)
{
    if (40 < l - f) {
        ptrdiff_t st = (l - f + 1) / 8;
        med3(f, f + st, f + 2 * st);
        med3(m - st, m, m + st);
        med3(l - 2 * st, l - st, l);
        med3(f + st, m, l - st);
    } else med3(f, m, l);
}
static void upart(int *first, int *last, int **pf, int **pl)
{
    int *mid = first + (last - first) / 2;
    median(first, mid, last - 1);
    int *Pf = mid, *Pl = Pf + 1;
    while (first < Pf && !lt(*(Pf - 1), *Pf) && !lt(*Pf, *(Pf - 1))) --Pf;
    while (Pl < last && !lt(*Pl, *Pf) && !lt(*Pf, *Pl)) ++Pl;
    int *Gf = Pl, *Gl = Pf;
    for (;;) {
        for (; Gf < last; ++Gf) {
            if (lt(*Pf, *Gf)) ;
            else if (lt(*Gf, *Pf)) break;
            else if (Pl++ != Gf) iswap(Pl - 1, Gf);
        }
        for (; first < Gl; --Gl) {
            if (lt(*(Gl - 1), *Pf)) ;
            else if (lt(*Pf, *(Gl - 1))) break;
            else if (--Pf != Gl - 1) iswap(Pf, Gl - 1);
        }
        if (Gl == first && Gf == last) { *pf = Pf; *pl = Pl; return; }
        if (Gl == first) {
            if (Pl != Gf) iswap(Pf, Pl);
            ++Pl;
            iswap(Pf++, Gf++);
        } else if (Gf == last) {
            if (--Gl != --Pf) iswap(Gl, Pf);
            iswap(Pf, --Pl);
        } else iswap(Gf++, --Gl);
    }
}
static void push_heap_(int *f, ptrdiff_t hole, ptrdiff_t top, int val)
{
    for (ptrdiff_t i = (hole - 1) / 2; top < hole && lt(f[i], val); i = (hole - 1) / 2) { f[hole] = f[i]; hole = i; }
    f[hole] = val;
}
static void adjust_heap(int *f, ptrdiff_t hole, ptrdiff_t bottom, int val)
{
    ptrdiff_t top = hole, i = 2 * hole + 2;
    for (; i < bottom; i = 2 * i + 2) {
        if (lt(f[i], f[i - 1])) --i;
        f[hole] = f[i]; hole = i;
    }
    if (i == bottom) { f[hole] = f[bottom - 1]; hole = bottom - 1; }
    push_heap_(f, hole, top, val);
}
static void isort(int *f, int *l)
{
    if (f == l) return;
    for (int *nx = f; ++nx != l;) {
        int *n1 = nx, v = *nx;
        if (lt(v, *f)) { memmove(f + 1, f, (size_t)(nx - f) * sizeof *f); *f = v; }
        else {
            for (int *f1 = n1; lt(v, *--f1); n1 = f1) *n1 = *f1;
            *n1 = v;
        }
    }
}
static void msort(int *first, int *last, ptrdiff_t ideal)
{
    ptrdiff_t cnt;
    for (; 32 < (cnt = last - first) && 0 < ideal;) {
        int *mf, *ml;
        upart(first, last, &mf, &ml);
        ideal /= 2; ideal += ideal / 2;
        if (mf - first < last - ml) { msort(first, mf, ideal); first = ml; }
        else { msort(ml, last, ideal); last = mf; }
    }
    if (32 < cnt) {
        ptrdiff_t b = last - first;
        for (ptrdiff_t h = b / 2; 0 < h;) { --h; adjust_heap(first, h, b, first[h]); }
        for (; 1 < last - first; --last) {
            int v = *(last - 1);
            *(last - 1) = *first;
            adjust_heap(first, 0, last - 1 - first, v);
        }
    } else if (1 < cnt) isort(first, last);
}
/* FUN_18005ab30 (disassembly): squared distance of point p from segment a b (3D; z = 0 here) */
static double seg_d2(const double *p, const double *a, const double *b)
{
    double dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    double L = dx * dx + dy * dy;
    L = L + dz * dz;
    double wx = p[0] - a[0], wy = p[1] - a[1], wz = p[2] - a[2];
    double t = L;
    if (L != 0.0) { double q = wy * dy + wx * dx; q = q + wz * dz; t = q / L; }
    if (t >= 1.0) {
        double ey = p[1] - b[1], ex = p[0] - b[0], ez = p[2] - b[2];
        double s = ey * ey + ex * ex;
        return s + ez * ez;
    }
    if (t > 0.0) { wx = wx - dx * t; wy = wy - dy * t; wz = wz - dz * t; }
    double s = wy * wy + wx * wx;
    return s + wz * wz;
}
/* FUN_18054e170 (hv l.9974): the smallest distance of an input vertex from an input edge it is not an end of (scanned in x
   order with a window), starting from the distance of the first two vertices in x order; the perturbation of the retries is
   capped at this / 2 sqrt 2 */
static double pert_cap(void)
{
    int np = g_cur->npoly, n = 0;
    for (int k = 0; k < np; k++) n += in_cnt(k) - 1;
    double *pts = malloc(sizeof(double) * 3 * (size_t)(n > 0 ? n : 1));
    int *idx = malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
    int *base = malloc(sizeof(int) * (size_t)(np + 1));
    double *xs = malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    int m = 0;
    for (int k = 0; k < np; k++) {
        base[k] = m;
        for (int j = 0; j < in_cnt(k) - 1; j++) {
            const HvVert *v = in_v(k, j);
            pts[3 * m] = v->x; pts[3 * m + 1] = v->y; pts[3 * m + 2] = 0.0; xs[m] = v->x; idx[m] = m; m++;
        }
    }
    g_sx = xs;
    msort(idx, idx + n, n);
    double *p0 = &pts[3 * idx[0]], *p1 = &pts[3 * idx[1 < n ? 1 : 0]];
    double dz = p0[2] - p1[2];
    double best2 = (p0[0] - p1[0]) * (p0[0] - p1[0]) + (p0[1] - p1[1]) * (p0[1] - p1[1]) + dz * dz;
    double best = sqrt(best2);
    for (int k = 0; k < np; k++) {
        int c1 = in_cnt(k) - 1;
        for (int j = 0; j < c1; j++) {
            int jp = (j == 0) ? (int)in_cnt(k) - 2 : j - 1;
            double *a = &pts[3 * (base[k] + jp)], *b = &pts[3 * (base[k] + j)];
            double lo, hi;
            if (b[0] <= a[0]) { lo = b[0]; hi = a[0]; } else { lo = a[0]; hi = b[0]; }
            double w = best + 0.01;
            double xmax = w + hi;
            int u = n, l = -1;
            for (int span = n + 1; 1 < span; span = u - l) {
                int mid = (u + l) / 2;
                if (lo - w <= xs[idx[mid]]) u = mid; else l = mid;
            }
            for (int i = u; i < n; i++) {
                double *q = &pts[3 * idx[i]];
                if (xmax < q[0]) break;
                int eqa = q[0] == a[0] && q[1] == a[1] && q[2] == a[2];
                int eqb = q[0] == b[0] && q[1] == b[1] && q[2] == b[2];
                if (!eqa && !eqb) {
                    double d2 = seg_d2(q, a, b);
                    if (d2 < best2) { best = sqrt(d2); best2 = d2; }
                }
            }
        }
    }
    free(pts); free(idx); free(base); free(xs);
    return best;
}

/* ------------------------------------------------------------------ the graph */
/* the polygon records of the vector at graph +0 (FUN_18054c670 -> FUN_1805462f0 each) */
static void recs_make(int n)
{
    u8 *r = hv_alloc(0x48 * (size_t)n);
    for (int k = 0; k < n; k++) {
        u8 *pr = r + 0x48 * k;
        PTR(pr, 0) = ls_new(0xa8);
        PTR(pr, 0x10) = ls_new(0x60);
    }
    PTR(G, 0) = r; PTR(G, 8) = r + 0x48 * n; PTR(G, 0x10) = r + 0x48 * n;
}
/* FUN_1805476e0 (hv l.4782): the graph emptied for a retry */
static void g_clear(void)
{
    PTR(G, 8) = PTR(G, 0);
    I32(G, 0x18) = -1;
    ls_clear(G_NODES, (int64_t *)(G + 0x60));
    ls_clear(G_EDGES, (int64_t *)(G + 0x70));
    ls_clear(G_BNECK, (int64_t *)(G + 0xa8));
    PTR(G, 0x28) = PTR(G, 0x20);
    PTR(G, 0x40) = PTR(G, 0x38);
    PTR(G, 0x50) = NULL;
}
static int link_boundaries(void);
static void unlink_bridges(void);
/* FUN_180549ff0 (hv l.6893): one construction with perturbation pert */
static int build_once(double pert)
{
    F64(G, 0x98) = pert;
    int np = g_cur->npoly;
    I32(G, 0x18) = np;
    recs_make(np + 1);
    const HvVert *v0 = in_v(0, 0);
    double minx = v0->x, maxx = v0->x, miny = v0->y, maxy = v0->y;
    for (int k = 0; k < np; k++) {
        for (int j = 0; j < in_cnt(k); j++) {
            const HvVert *v = in_v(k, j);
            double x = v->x, y = v->y;
            if (minx <= x) { if (maxx < x) maxx = x; } else minx = x;
            if (miny <= y) { if (maxy < y) maxy = y; } else miny = y;
        }
    }
    F64(G, 0x78) = ((maxy - miny) + (maxx - minx)) * 2.0;
    for (int k = 0; k < np; k++) poly_sites(PTR(G, 0) + 0x48 * k, k, k != 0, F64(G, 0x80), pert);
    if (np < 2) PTR(G, 0x50) = vstart(PTR(PTR(PTR(G, 0), 0x28), 0));
    else if (!link_boundaries()) return 0;   /* "Failed link boundaries, perturb: " */
    u8 *v = PTR(G, 0x50);
    do {
        edge_of_vertex(v);
        v = vend(PTR(PTR(v, 0x30), 8));
    } while (v != PTR(G, 0x50));
    if (!build_incr()) return 0;   /* "Failed incrementally build, perturb: " */
    if (1 < np) { unlink_bridges(); if (g_abort) return 0; }
    if (!bottlenecks()) return 0;   /* "Failed detect bottlenecks, perturb: " */
    split_bottlenecks();
    if (g_abort) return 0;
    return 1;
}
/* DAT_1807ba3f0: the factors of the 7 retries (x VoronoiPerturbationTolerance, sign kept) */
static const double k_pert[7] = { -0.07568000257015228, 0.7567999958992004, -0.352400004863739, 0.8971999883651733,
                                  -0.35923, 2.299999952316284, -1.887665033340454 };

void hv_params_default(HvParams *p) { p->recip = 1.0 / 1000000.0; p->sbl = 2e-5; p->sbs = 2e-5; p->ptol = 0.002; }
/* FUN_1805475b0 (hv l.4736) */
HvGraph *hv_new(const HvParams *p)
{
    HvGraph *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->par = *p;
    HvGraph *save = g_cur; g_cur = g;
    PTR(G, 0x58) = ls_new(0x88);
    PTR(G, 0x68) = ls_new(0x60);
    PTR(G, 0xa0) = ls_new(0x88);
    F64(G, 0x80) = 1.0 / p->recip; F64(G, 0x88) = p->sbl; F64(G, 0x90) = p->sbs;
    I32(G, 0x18) = -1;
    g_cur = save;
    return g;
}
/* FUN_18054a440 (hv l.7069): build; on failure up to 7 retries with perturbed vertices; then the node / site finishing */
int hv_build(HvGraph *g, const HvPoly *polys, int npoly)
{
    HvGraph *save = g_cur; g_cur = g;
    g->polys = polys; g->npoly = npoly;
    g_abort = 0;
    int ok = build_once(0.0);
    if (!ok) {
        double cap = pert_cap();
        int i;
        for (i = 0; i < 7; i++) {
            g_clear();
            g_abort = 0;
            double p = fabs(k_pert[i] * g->par.ptol);
            if (!(p <= cap / 2.8284270763397217)) p = cap / 2.8284270763397217;
            if (k_pert[i] < 0.0) p = -p;
            if (build_once(p)) break;
        }
        if (i == 7) { g_cur = save; return 0; }   /* "Perturb times " */
    }
    orient();
    site_nodes();
    g_cur = save;
    return 1;
}
void hv_free(HvGraph *g)
{
    if (!g) return;
    for (HvBlk *b = g->arena; b;) { HvBlk *n = b->next; free(b); b = n; }
    free(g);
}
unsigned char *hv_graph_raw(HvGraph *g) { return g->rec; }

/* ------------------------------------------------------------------ several polygons: bridges */
/* the direction of the folded pseudo-angle (y <= 0 turned over), FUN_18054fc60's key */
static double fold_pang(double x, double y) { double p[2] = { x, y }; if (y <= 0.0) { p[0] = x * -1.0; p[1] = y * -1.0; } return pang(p); }
static void set_ins(double *s, int *n, double v)
{
    int lo = 0, len = *n;
    while (0 < len) { int h = len / 2; if (s[lo + h] < v) { lo = lo + h + 1; len = len - 1 - h; } else len = h; }
    if (lo == *n || v != s[lo]) { memmove(s + lo + 1, s + lo, sizeof(double) * (size_t)(*n - lo)); s[lo] = v; (*n)++; }
}
/* FUN_18054fd50 (hv l.11240, its third argument = the polygon count): the bridge direction: the folded pseudo-angles of the
   segment normals and arc end radii in a sorted set with 0 and 2; the middle (0.37894232399 / 0.62105767601) of the widest
   gap, back to a unit vector */
static void bridge_dir(double *o, int np)
{
    int cap = 8;
    for (int k = 0; k < np; k++) cap += 2 * (int)*(int64_t *)(PTR(G, 0) + 0x48 * k + 0x18) + 4;
    double *s = malloc(sizeof(double) * (size_t)cap);
    int n = 0;
    set_ins(s, &n, 0.0);
    set_ins(s, &n, 2.0);
    for (int k = 0; k < np; k++) {
        u8 *s0 = PTR(PTR(G, 0) + 0x48 * k, 0x28), *x = s0;
        do {
            if (U8(x, 0x11) == 0) set_ins(s, &n, fold_pang(F64(x, 0x18), F64(x, 0x20)));
            else if (F64(x, 0x28) != 0.0) {
                u8 *a = vstart(PTR(x, 0)), *b = vend(PTR(x, 8));
                set_ins(s, &n, fold_pang(F64(a, 0) - F64(x, 0x18), F64(a, 8) - F64(x, 0x20)));
                set_ins(s, &n, fold_pang(F64(b, 0) - F64(x, 0x18), F64(b, 8) - F64(x, 0x20)));
            }
            x = PTR(vend(PTR(x, 8)), 0x30);
        } while (x != s0);
    }
    size_t best = (size_t)-1; double gap = 0.0;
    for (size_t i = 1; i < (size_t)n; i++) {
        if (i == 1 || gap < s[i] - s[i - 1]) { best = i; gap = s[i] - s[i - 1]; }
    }
    double v = s[best - 1];
    if (v != s[best]) v = s[best] * 0.37894232399 + v * 0.62105767601;
    free(s);
    int q = (int)v;
    double f = v - (double)q, x, y;
    if (q == 0 || q == 4) { y = f; x = 1.0 - f; }
    else if (q == 1) { x = -f; y = 1.0 - f; }
    else if (q == 2) { x = f - 1.0; y = -f; }
    else if (q == 3) { y = f - 1.0; x = f; }
    else { x = 0.0; y = 0.0; }
    double len = sqrt(y * y + x * x);
    if (len == 0.0) len = 1.0;
    o[0] = x * (1.0 / len); o[1] = y * (1.0 / len);
}
/* FUN_180550600 (hv l.11557): the loop's extreme point in direction d: +0x30 the point, +0x40 its height, +0x28 its site
   (segments by their higher end, arcs by the centre; a tie goes to the later site when the held one is a segment) */
static void extreme(u8 *pr, const double *d)
{
    u8 *s0 = PTR(pr, 0x28), *x = s0, *best = NULL;
    int bseg = 0;
    do {
        double px, py, h; int seg;
        if (U8(x, 0x11) == 0) {
            u8 *a = vstart(PTR(x, 0)), *b = vend(PTR(x, 8));
            double ha = d[1] * F64(a, 8) + d[0] * F64(a, 0), hb = d[1] * F64(b, 8) + d[0] * F64(b, 0);
            seg = 1;
            if (ha <= hb) { px = F64(b, 0); py = F64(b, 8); h = hb; }
            else { px = F64(a, 0); py = F64(a, 8); h = ha; }
        } else {
            px = F64(x, 0x18); py = F64(x, 0x20); seg = 0;
            h = py * d[1] + px * d[0];
        }
        if (!best || F64(pr, 0x40) < h || (h == F64(pr, 0x40) && bseg)) {
            F64(pr, 0x40) = h; F64(pr, 0x30) = px; F64(pr, 0x38) = py;
            best = x; bseg = seg;
        }
        x = PTR(vend(PTR(x, 8)), 0x30);
    } while (x != s0);
    PTR(pr, 0x28) = best;
}
/* FUN_180546df0 (hv l.4412): a bridge vertex (kind 9) at point p (x, y, r) joining a's entering site to b's leaving site:
   a +0x90 / b +0x88 point at it (a, b become kind 0xb) */
static u8 *bridge_v(u8 *pr, const double *p, u8 *a, u8 *b)
{
    u8 *n = ls_push_back(PTR(pr, 0), 0xa8, (int64_t *)(pr + 8));
    u8 *v = n + 0x10;
    F64(v, 0) = p[0]; F64(v, 8) = p[1]; F64(v, 0x10) = p[2];
    I32(v, 0x18) = 9; F64(v, 0x28) = 1.0; I32(v, 0x84) = -1;
    PTR(v, 0x90) = b; PTR(v, 0x88) = a;
    PTR(a, 0x90) = v; PTR(b, 0x88) = v;
    PTR(v, 0x20) = PTR(a, 0x20);
    PTR(v, 0x30) = PTR(b, 0x30);
    return v;
}
/* FUN_18054c120 (hv l.8432): the islands joined to the outer loop: the loops in order of their extreme height (FUN_180550600
   along the bridge direction, highest first); from each one's extreme point a ray along the direction to the nearest site of
   the loops before it; both sites split there (vertices in the vectors +0x20 / +0x38) and two bridge vertices close the cut */
static int link_boundaries(void)
{
    int np = I32(G, 0x18);
    double d[2];
    bridge_dir(d, np);
    for (int k = 0; k < np; k++) extreme(PTR(G, 0) + 0x48 * k, d);
    u8 **ord = malloc(sizeof(u8 *) * (size_t)np);
    int no = 0;
    for (int k = 0; k < np; k++) {
        u8 *pr = PTR(G, 0) + 0x48 * k;
        int i = 0;
        while (i < no && !(F64(ord[i], 0x40) < F64(pr, 0x40))) i++;
        memmove(ord + i + 1, ord + i, sizeof(u8 *) * (size_t)(no - i));
        ord[i] = pr; no++;
    }
    u8 **A = (u8 **)hv_alloc(sizeof(u8 *) * (size_t)np), **B = (u8 **)hv_alloc(sizeof(u8 *) * (size_t)np);
    PTR(G, 0x20) = (u8 *)A; PTR(G, 0x28) = (u8 *)(A + np); PTR(G, 0x30) = (u8 *)(A + np);
    PTR(G, 0x38) = (u8 *)B; PTR(G, 0x40) = (u8 *)(B + np); PTR(G, 0x48) = (u8 *)(B + np);
    A[0] = NULL; B[0] = NULL;
    double *ent = calloc((size_t)np, 3 * sizeof(double));
    u8 *xr = PTR(G, 8) - 0x48;
    int ok = 1;
    for (int i = 1; i < np; i++) {
        u8 *best = NULL;
        for (int j = 0; j < i; j++) {
            u8 *s0 = PTR(ord[j], 0x28), *x = s0;
            do {
                double pt[2], r = -1.0, t;
                if (s_ray(x, pt, &r, &t, d, (double *)(ord[i] + 0x30)) && (!best || r < ent[3 * i + 2])) {
                    ent[3 * i] = pt[0]; ent[3 * i + 1] = pt[1]; ent[3 * i + 2] = r;
                    best = x;
                }
                x = PTR(vend(PTR(x, 8)), 0x30);
            } while (x != s0);
        }
        if (!best) { ok = 0; break; }
        for (int j = 0; j < i; j++) if (PTR(ord[j], 0x28) == best) PTR(ord[j], 0x28) = PTR(vend(PTR(best, 8)), 0x30);
        A[i] = s_split(xr, best, &ent[3 * i]);
        u8 *nx = PTR(vend(PTR(PTR(ord[i], 0x28), 8)), 0x30);
        B[i] = s_split(xr, PTR(ord[i], 0x28), &ent[3 * i]);
        PTR(ord[i], 0x28) = nx;
    }
    if (ok) {
        for (int i = 1; i < np; i++) {
            bridge_v(xr, &ent[3 * i], A[i], B[i]);
            PTR(G, 0x50) = bridge_v(xr, &ent[3 * i], B[i], A[i]);
            I32(A[i], 0x18) = 0xb; I32(B[i], 0x18) = 0xb;
        }
    }
    free(ent); free(ord);
    return ok;
}
/* FUN_180546f40 (hv l.4489): the two sites at vertex v merged back into one (v dies) */
static u8 *s_merge(u8 *pr, u8 *v)
{
    u8 *so = PTR(v, 0x30), *si = PTR(v, 0x20);
    u8 *end = vend(PTR(so, 8)), *start = vstart(PTR(si, 0));
    double n0 = F64(so, 0x18), n1 = F64(so, 0x20), c = F64(so, 0x28);
    u8 arc = U8(so, 0x11), flag = U8(so, 0x5c);
    PTR(vstart(PTR(so, 0)), 0x30) = NULL; PTR(vend(PTR(so, 8)), 0x20) = NULL;
    PTR(so, 0) = NULL; PTR(so, 8) = NULL; U8(so, 0x10) = 0;
    si = PTR(v, 0x20);
    PTR(vstart(PTR(si, 0)), 0x30) = NULL; PTR(vend(PTR(si, 8)), 0x20) = NULL;
    PTR(si, 0) = NULL; PTR(si, 8) = NULL; U8(si, 0x10) = 0;
    I32(v, 0x18) = 0;
    u8 *s = s_new(pr, start, end, flag);
    U8(s, 0x11) = arc; U8(s, 0x10) = 1; F64(s, 0x28) = c; F64(s, 0x18) = n0; F64(s, 0x20) = n1;
    return s;
}
/* FUN_1805496d0 (hv l.6388): bridge node N (kind 4) removed: the split sites on both sides merged (the walks hand the edges
   and nodes over to the merged sites), N's two bridge-side edges replaced by one edge between the merged sites */
static void unbridge(u8 *N)
{
    u8 *xr = PTR(G, 8) - 0x48;
    u8 *s1 = s_merge(xr, vend(PTR(PTR(N, 0x30), 8)));
    walk_l(s1, PTR(vend(PTR(s1, 8)), 0x60), PTR(N, 0x70));
    walk_r(s1, PTR(vstart(PTR(s1, 0)), 0x60), PTR(N, 0x60));
    u8 *s2 = s_merge(xr, vend(PTR(PTR(N, 0x50), 8)));
    walk_l(s2, PTR(vend(PTR(s2, 8)), 0x60), PTR(N, 0x60));
    walk_r(s2, PTR(vstart(PTR(s2, 0)), 0x60), PTR(N, 0x70));
    if (g_abort) return;
    u8 *E = edge_add(s1, s2, NULL);
    I32(E, 0x48) = 0;
    u8 *a = PTR(PTR(N, 0x60), 0x40);
    if (a == N) a = PTR(PTR(N, 0x60), 0x38);
    PTR(E, 0x38) = a;
    u8 *b = PTR(PTR(N, 0x70), 0x40);
    if (b == N) b = PTR(PTR(N, 0x70), 0x38);
    PTR(E, 0x40) = b;
    n_replace(a, PTR(N, 0x60), E);
    n_replace(b, PTR(N, 0x70), E);
    I32(PTR(N, 0x70), 0) = 0; I32(PTR(N, 0x60), 0) = 0; I32(PTR(N, 0x78), 0) = 0; I32(PTR(N, 0x68), 0) = 0;
    I32(N, 0x18) = 0;
}
/* FUN_180549d10 (hv l.6759): after the build, each bridge (last first) becomes a node of kind 4 at the bridge point (the
   split vertices get their own normal edges, FUN_180547410), then the bridges are removed (FUN_1805496d0) */
static void unlink_bridges(void)
{
    int np = I32(G, 0x18);
    u8 **A = (u8 **)PTR(G, 0x20), **B = (u8 **)PTR(G, 0x38);
    u8 **tmp = calloc((size_t)np, sizeof(u8 *));
    for (int i = np - 1; 0 < i; i--) {
        u8 *a = A[i], *vb = PTR(a, 0x88), *va = PTR(a, 0x90);
        I32(a, 0x18) = 8; PTR(a, 0x88) = NULL; PTR(a, 0x90) = NULL; edge_of_vertex(a);
        u8 *b = B[i];
        I32(b, 0x18) = 8; PTR(b, 0x88) = NULL; PTR(b, 0x90) = NULL; edge_of_vertex(b);
        u8 nb[0x88];
        nb_init(nb);
        u8 *N = ls_push_back(G_NODES, 0x88, (int64_t *)(G + 0x60)) + 0x10;
        memcpy(N, nb, 0x88);
        I32(N, 0x18) = 4;
        tmp[i] = N;
        F64(N, 0) = F64(va, 0); F64(N, 8) = F64(va, 8); F64(N, 0x10) = F64(va, 0x10);
        PTR(N, 0x20) = PTR(b, 0x30); F64(N, 0x28) = 0.0;
        PTR(N, 0x30) = PTR(a, 0x20); F64(N, 0x38) = 1.0;
        PTR(N, 0x40) = PTR(a, 0x30); F64(N, 0x48) = 0.0;
        PTR(N, 0x50) = PTR(b, 0x20); F64(N, 0x58) = 1.0;
        PTR(N, 0x68) = PTR(a, 0x60); PTR(PTR(a, 0x60), 0x38) = N;
        PTR(N, 0x78) = PTR(b, 0x60); PTR(PTR(b, 0x60), 0x38) = N;
        PTR(N, 0x60) = PTR(va, 0x60); PTR(PTR(va, 0x60), 0x40) = N;
        PTR(N, 0x70) = PTR(vb, 0x60); PTR(PTR(vb, 0x60), 0x40) = N;
        I32(va, 0x18) = 0; I32(vb, 0x18) = 0;
    }
    for (int i = np - 1; 0 < i; i--) { unbridge(tmp[i]); if (g_abort) break; }
    free(tmp);
}

/* ------------------------------------------------------------------ the offset radius (FUN_18054a640) */
typedef struct { double *v; int n, cap; } HvDv;            /* a sorted vector of doubles (std::vector<double>) */
typedef struct { double *v; int n, cap; } HvRv;            /* a sorted vector of disjoint ranges (lo, hi) */
static void dv_grow(double **v, int *cap, int need, int w)
{
    if (need <= *cap) return;
    int c = *cap ? *cap * 2 : 16;
    while (c < need) c *= 2;
    *v = realloc(*v, sizeof(double) * (size_t)c * (size_t)w);
    *cap = c;
}
/* FUN_18005f660 (hv1 l.2367): insert v unless present */
static void dv_ins(HvDv *s, double v)
{
    int i = 0;
    while (i < s->n && s->v[i] < v) i++;
    if (i == s->n || v != s->v[i]) {
        dv_grow(&s->v, &s->cap, s->n + 1, 1);
        memmove(s->v + i + 1, s->v + i, sizeof(double) * (size_t)(s->n - i));
        s->v[i] = v; s->n++;
    }
}
/* FUN_180060480 (disassembly): the first range whose hi is not below v */
static int rv_lb(const HvRv *r, double v) { int i = 0; while (i < r->n && v > r->v[2 * i + 1]) i++; return i; }
/* FUN_18005fb40 (hv1 l.2432): range (lo, hi) merged into the sorted disjoint ranges */
static void rv_ins(HvRv *r, double lo, double hi)
{
    if (r->n == 0) { dv_grow(&r->v, &r->cap, 1, 2); r->v[0] = lo; r->v[1] = hi; r->n = 1; return; }
    int i = rv_lb(r, lo);
    if (i != r->n && r->v[2 * i] <= hi) {
        if (lo < r->v[2 * i]) r->v[2 * i] = lo;
        while (i + 1 < r->n && r->v[2 * (i + 1)] <= hi) {
            r->v[2 * i + 1] = r->v[2 * (i + 1) + 1];
            memmove(r->v + 2 * (i + 1), r->v + 2 * (i + 2), sizeof(double) * 2 * (size_t)(r->n - i - 2));
            r->n--;
        }
        if (hi <= r->v[2 * i + 1]) return;
        r->v[2 * i + 1] = hi;
        return;
    }
    dv_grow(&r->v, &r->cap, r->n + 1, 2);
    memmove(r->v + 2 * (i + 1), r->v + 2 * i, sizeof(double) * 2 * (size_t)(r->n - i));
    r->v[2 * i] = lo; r->v[2 * i + 1] = hi; r->n++;
}
/* the push-up search of FUN_18054a640 (disassembly 18054a8c7-18054aad6, again 18054ad3e-18054af46): from d, alternately
   past every critical clearance of s within pushup and every range of r within pushup, until neither moves d or d reaches
   rmax */
static double push_search(const HvDv *s, const HvRv *r, double d, double pushup, double rmax)
{
    int chk_s = 1, first = 1;
    for (;;) {
        double prev;
        if (chk_s) {
            prev = d;
            double lo = d - pushup;
            int i = 0;
            while (i < s->n && lo > s->v[i]) i++;
            for (; i < s->n; i++) {
                if (s->v[i] >= d + pushup) break;
                d = s->v[i] + pushup;
                if (d >= rmax) { d = rmax; break; }
            }
            if (d == rmax) return d;
            if (!first && d == prev) { chk_s = 0; continue; }
        } else if (!first) return d;
        prev = d;
        double lo = d - pushup;
        int i = rv_lb(r, lo);
        for (; i < r->n; i++) {
            if (!(d + pushup > r->v[2 * i])) break;
            d = pushup + r->v[2 * i + 1];
            if (d >= rmax) { d = rmax; break; }
        }
        if (d == rmax) return d;
        chk_s = d != prev;
        first = 0;
    }
}
/* FUN_18054a640 (hv l.7145, read in the disassembly): the offset radius r0 moved up, by no more than limit
   (RadPushUpLimit 0x1bcb), until it is at least pushup (RadPushUp 0x1bca) away from the clearance of every node of kind 2 / 3
   and of every bottleneck, and outside every clearance range of a rising edge (kinds 1 / 4) or of a node's kind 2 / 5 edge
   ("Inflexion offset radius moved in from ... to ..." at the caller); when that fails, the middle of the widest gap
   between those ranges and points within [r0, r0 + limit] ("Find Best range gap for vroffset revised to ..."), else the
   same search up to the largest critical clearance + 1 ("Offset displaced to ... beyond ...") */
double hv_push_radius(HvGraph *g, double r0, double pushup, double limit)
{
    HvGraph *save = g_cur; g_cur = g;
    HvDv S = { 0 }; HvRv R = { 0 };
    for (u8 *ln = PTR(G_BNECK, 0); ln != G_BNECK; ln = PTR(ln, 0)) {
        u8 *B = ln + 0x10, *E = PTR(B, 0x60);
        if (I32(E, 0) == 2 || I32(E, 0) == 5) {
            double a = F64(PTR(E, 0x38), 0x10), b = F64(PTR(PTR(B, 0x70), 0x38), 0x10);
            if (a >= b) rv_ins(&R, b, a); else rv_ins(&R, a, b);
        } else dv_ins(&S, F64(B, 0x10));
    }
    for (u8 *ln = PTR(G_NODES, 0); ln != G_NODES; ln = PTR(ln, 0)) {
        u8 *N = ln + 0x10;
        if (I32(N, 0x18) == 2) {
            dv_ins(&S, F64(N, 0x10));
            static const int off[3] = { 0x60, 0x68, 0x78 };
            for (int k = 0; k < 3; k++) {
                u8 *e = PTR(N, off[k]);
                if ((I32(e, 0) == 2 || I32(e, 0) == 5) && I32(PTR(e, 0x40), 0x18) != 5)
                    rv_ins(&R, F64(PTR(e, 0x40), 0x10), F64(N, 0x10));
            }
        } else if (I32(N, 0x18) == 3) dv_ins(&S, F64(N, 0x10));
    }
    for (u8 *ln = PTR(G_EDGES, 0); ln != G_EDGES; ln = PTR(ln, 0)) {
        u8 *e = ln + 0x10;
        if (I32(e, 0) == 1 || I32(e, 0) == 4) {
            double a = F64(PTR(e, 0x38), 0x10), b = F64(PTR(e, 0x40), 0x10);
            if (a < b) rv_ins(&R, a, b);
        }
    }
    double rmax = r0 + limit;
    double d = push_search(&S, &R, r0, pushup, rmax);
    if (d == rmax) {
        for (int i = 0; i < S.n; i++) rv_ins(&R, S.v[i], S.v[i]);
        rv_ins(&R, r0, r0);
        double best = -1.0, mid0 = 0.0;
        for (int i = 1; i < R.n; i++) {
            double a = R.v[2 * (i - 1) + 1], b = R.v[2 * i];
            double m = (a == b) ? a : (b + a) * 0.5;
            if (m >= r0 && !(m > rmax)) {
                double gap = b - a;
                if (gap > best) { best = gap; mid0 = m; }
            }
        }
        if (best != -1.0) d = mid0;   /* "Find Best range gap for vroffset revised to " (best x 0.5) " from " pushup */
        else {
            double rmax2 = (S.n ? S.v[S.n - 1] : 0.0) + 1.0 + pushup;
            d = push_search(&S, &R, r0, pushup, rmax2);   /* "Offset displaced to " d " beyond " rmax */
        }
    }
    free(S.v); free(R.v);
    g_cur = save;
    return d;
}

/* ------------------------------------------------------------------ the regions at an offset radius (FUN_18054b480) */
/* FUN_18054b480 (hv l.7820): the regions of clearance above r, numbered from 0 (returned: their count). Each unlabelled top
   node (kind 2) above r starts a region: its sites whose first node lies above r and whose last node carries the label are
   walked as loops round the region (site +0x58 = the label; a kind-5 node met on the way hands the label to the far
   site's last node when above r). Then every live edge rising above r takes its left site's label (+0x58) and gives it to
   its end node (+0x84); an edge that starts at or below r is the crossing of its sites (left site +0x50, right site +0x48). */
int hv_regions(HvGraph *g, double r)
{
    HvGraph *save = g_cur; g_cur = g;
    unsigned u = 0;
    for (u8 *ln = PTR(G_NODES, 0); ln != G_NODES; ln = PTR(ln, 0)) {
        u8 *N = ln + 0x10;
        if (!(I32(N, 0x18) == 2 && I32(N, 0x84) == -1 && r < F64(N, 0x10))) continue;
        I32(N, 0x84) = (int32_t)u;
        for (;;) {
            if (I32(G, 0x18) < 0) break;
            u8 *S0 = NULL;
            for (int k = 0; k <= I32(G, 0x18) && !S0; k++) {
                u8 *head = PTR(PTR(G, 0) + 0x48 * k, 0x10);
                for (u8 *sn = PTR(head, 0); sn != head; sn = PTR(sn, 0)) {
                    u8 *S = sn + 0x10;
                    if (U8(S, 0x10) == 0 || F64(PTR(S, 0x38), 0x10) <= r || I32(S, 0x58) != -1 ||
                        (unsigned)I32(PTR(S, 0x40), 0x84) != u) continue;
                    S0 = S;
                    break;
                }
            }
            if (!S0) break;
            u8 *S = S0;
            do {
                if (r < F64(PTR(S, 0x38), 0x10)) I32(S, 0x58) = (int32_t)u;
                u8 *e = PTR(vstart(PTR(S, 0)), 0x60);
                u8 *n = PTR(e, 0x38), *e2 = e;
                if (I32(n, 0x18) == 5) {
                    if (F64(n, 0x10) <= r && (e2 = PTR(n, 0x68), e == e2)) e2 = PTR(n, 0x78);
                    u8 *m = PTR(PTR(e2, 0x28), 0x40);
                    if (r < F64(m, 0x10) && I32(m, 0x84) == -1) I32(m, 0x84) = (int32_t)u;
                    I32(m, 0x84) = (r < F64(m, 0x10)) ? (int32_t)u : -1;
                }
                S = PTR(e2, 0x28);
            } while (S != S0);
        }
        u++;
    }
    for (u8 *ln = PTR(G_EDGES, 0); ln != G_EDGES; ln = PTR(ln, 0)) {
        u8 *e = ln + 0x10;
        if (I32(e, 0) == 0 || !(r < F64(PTR(e, 0x38), 0x10))) continue;
        I32(e, 0x58) = I32(PTR(e, 0x28), 0x58);
        if (I32(PTR(e, 0x38), 0x84) == -1) I32(PTR(e, 0x38), 0x84) = I32(e, 0x58);
        if (F64(PTR(e, 0x40), 0x10) <= r) { PTR(PTR(e, 0x28), 0x50) = e; PTR(PTR(e, 0x30), 0x48) = e; }
    }
    g_cur = save;
    return (int)u;
}

/* ------------------------------------------------------------------ stage 2b: the region's compact copy (FUN_18054ec00)
 * hv1 = decomp/hmm_vor1.c. FUN_180406b00 (hp l.1811-1855) builds the copy object on its stack (0xf8 bytes, zeroed; +0x78 / +0x80 /
 * +0x88 = VoronoiPolyEpsilon 0x1bce / RadRadialPrecision 0x1bcd / RadAvoidBottleneck 0x1bcc) and calls FUN_18054ec00(copy, graph,
 * region, r'). Layout (vectors are begin / end / cap):
 *   +0x00 vertex nodes (0x78; one per site of the region, indexed by the site's +0x30 index: the crossing of clearance r')
 *   +0x18 sites (0x50)   +0x30 vector of the loops' far vertex nodes (FUN_1805540e0)   +0x48 vector of the loops' diameters
 *   +0x60 the largest diameter   +0x68 / +0x70 the highest top node's point   +0x90 graph +0x98 (the perturbation)
 *   +0x98 top nodes (graph kind 2, copy kind 2)   +0xb0 bottleneck nodes (copy kind 3)   +0xc8 ordinary nodes (graph kind 3, copy kind 1)
 *   +0xe0 edges (0x28)
 * copy node (0x78): +0 x, +8 y, +0x10 clearance - r', +0x18 int kind (1 ordinary, 2 top, 3 bottleneck, 4 vertex), +0x20 / +0x28 /
 *   +0x30 / +0x38 / +0x40 edges (graph node +0x68 / +0x78 / +0x60 (top) / +0x60 (ordinary, bottleneck) / +0x70 (bottleneck); vertex
 *   node: +0x38 = its crossing edge), +0x48 / +0x50 parents (bottleneck: the lowest bottleneck or top node above each side; vertex
 *   node: +0x48 the loop's lowest bottleneck / top node), +0x58 / +0x60 children, +0x68 / +0x70 the clearance range (FUN_180553a70).
 * copy site (0x50): +0 / +8 normal or centre, +0x10 graph +0x28 + r' (the offset line / circle), +0x18 int arc, +0x20 / +0x28 the vertex
 *   nodes at its two crossings, +0x30 / +0x38 nodes (FUN_1805538a0), +0x40 int loop index, +0x44 byte flag (graph +0x5c), +0x48 int
 *   position in the loop (FUN_180554e80).
 * copy edge (0x28, FUN_180554f60): +0 int kind (1 line, 2 vertex normal, 3 parallel, 4 / 5 curved by side, 6 two arcs), +8 left site,
 *   +0x10 right site, +0x18 start node, +0x20 end node.
 * The DLL leaves stack garbage in +0x1c of nodes and sites, +0x45..+0x47 / +0x4c of sites and +4 of edges (not read). */
struct HvCopy { u8 raw[0xf8]; };
#define CV(o) (PTR(C, (o)))
#define BIGF 99999.9921875                         /* 0x40f869ffe0000000 */
static double negf(void) { uint64_t b = 0xc5f1f37180000000ull; double d; memcpy(&d, &b, 8); return d; }

HvCopy *hv_copy_new(void) { return calloc(1, sizeof(HvCopy)); }
unsigned char *hv_copy_raw(HvCopy *c) { return c->raw; }
void hv_copy_free(HvCopy *c)
{
    if (!c) return;
    static const int vo[] = { 0, 0x18, 0x30, 0x48, 0x98, 0xb0, 0xc8, 0xe0 };
    for (unsigned i = 0; i < sizeof vo / sizeof *vo; i++) free(PTR(c->raw, vo[i]));
    free(c);
}
static void cvec(u8 *C, int o, int n, int sz)
{
    free(PTR(C, o));
    u8 *p = calloc((size_t)(n > 0 ? n : 1), (size_t)sz);
    if (!p) abort();
    PTR(C, o) = p; PTR(C, o + 8) = p + (size_t)n * sz; PTR(C, o + 0x10) = p + (size_t)n * sz;
}
static int cnt(u8 *C, int o, int sz) { return (int)((PTR(C, o + 8) - PTR(C, o)) / sz); }

/* FUN_18054cd70 (hv1 l.1656; disassembly 18054cd70-18054d010 for the call arguments Ghidra drops and the operand order): the point
   of a graph edge at clearance r */
static void edge_at(double *o, u8 *e, double r)
{
    u8 *st = PTR(e, 0x40);
    double r0 = F64(st, 0x10);
    if (r == r0) { o[0] = F64(st, 0); o[1] = F64(st, 8); return; }
    int k = I32(e, 0);
    if (k == 1 || k == 3) {
        double d = F64(PTR(e, 0x38), 0x10) - r0, t = d == 0.0 ? 0.0 : (r - r0) / d;
        lerp(o, t, (double *)st, (double *)PTR(e, 0x38));
        return;
    }
    if (k != 4) { o[0] = 0.0; o[1] = 0.0; return; }
    u8 *L = PTR(e, 0x28), *R = PTR(e, 0x30);
    double f = U8(e, 0x5c) ? 1.0 : -1.0;
    if (U8(L, 0x11) && U8(R, 0x11)) {
        double dx = F64(R, 0x18) - F64(L, 0x18), dy = -(F64(R, 0x20) - F64(L, 0x20));
        double rr = r + F64(R, 0x28), rl = r + F64(L, 0x28);
        double inv = 1.0 / (dx * dx + dy * dy);
        double t = ((rl * rl - rr * rr) * inv + 1.0) * 0.5;
        double q = rl * rl * inv - t * t, s = 0.0;
        if (q > 0.0) s = sqrt(q);
        s = s * f;
        double m[2]; lerp(m, t, (double *)(L + 0x18), (double *)(R + 0x18));
        o[1] = dx * s + m[1];
        o[0] = dy * s + m[0];
        return;
    }
    u8 *P = U8(L, 0x11) ? L : R, *S = U8(L, 0x11) ? R : L;      /* the point site and the segment site */
    double rp = r + F64(P, 0x28);
    double d9 = (r - (F64(S, 0x20) * F64(P, 0x20) + F64(P, 0x18) * F64(S, 0x18))) + F64(S, 0x28);
    double q = rp * rp - d9 * d9, s = 0.0;
    if (q > 0.0) s = sqrt(q);
    s = s * f;
    o[0] = -F64(S, 0x20) * s + (d9 * F64(S, 0x18) + F64(P, 0x18));
    o[1] = F64(S, 0x18) * s + (d9 * F64(S, 0x20) + F64(P, 0x20));
}
/* FUN_180554f60 (hv1 l.2209): a copy edge from the graph edge kind; kind 4 splits by the side of the end node */
static void edge_rec(u8 *o, u8 *L, u8 *R, u8 *st, u8 *en, int kind)
{
    PTR(o, 0x18) = st; PTR(o, 8) = L; PTR(o, 0x10) = R; PTR(o, 0x20) = en;
    int k = 0;
    if (kind == 1) k = 1; else if (kind == 2) k = 3; else if (kind == 3) k = 2; else if (kind == 5) k = 6;
    else if (kind == 4) {
        double d1, d2, d4;
        if (U8(L, 0x18) && U8(R, 0x18)) { d4 = F64(R, 0) - F64(L, 0); d2 = -(F64(R, 8) - F64(L, 8)); d1 = F64(R, 8) * d4 + F64(R, 0) * d2; }
        else if (!U8(L, 0x18)) { d2 = -F64(L, 8); d4 = F64(L, 0); d1 = d4 * F64(R, 8) + d2 * F64(R, 0); }
        else { d2 = -F64(R, 8); d4 = F64(R, 0); d1 = d4 * F64(L, 8) + d2 * F64(L, 0); }
        k = (d4 * F64(en, 8) + d2 * F64(en, 0) <= d1) + 4;
    }
    I32(o, 0) = k;
}
/* FUN_1805539c0 (disassembly): node a lies below node b (lower clearance, or the same clearance and an earlier bottleneck index) */
static int below(u8 *C, u8 *a, u8 *b)
{
    double ra = F64(a, 0x10), rb = F64(b, 0x10);
    if (ra < rb) return 1;
    if (ra != rb) return 0;
    return (a - CV(0xb0)) / 0x78 < (b - CV(0xb0)) / 0x78;
}
/* FUN_180553fd0 (hv l.13775): widen the range lo / hi by an edge's start (side 1) or end node; edges falling in clearance give
   both ends (log "Absorbing non-parallel bit") */
static void absorb(double *lh, u8 *e, int side)
{
    int k = I32(e, 0);
    if (k == 3 || k == 6) {
        double v = F64(PTR(e, side ? 0x18 : 0x20), 0x10);
        if (lh[0] <= v) { if (lh[1] < v) lh[1] = v; } else lh[0] = v;
    } else if (F64(PTR(e, 0x20), 0x10) < F64(PTR(e, 0x18), 0x10)) {
        double v = F64(PTR(e, 0x18), 0x10);
        if (F64(PTR(e, 0x20), 0x10) < lh[0]) lh[0] = F64(PTR(e, 0x20), 0x10);
        if (lh[1] < v) lh[1] = v;
    }
}
/* FUN_180553a70 (hv l.13489): the clearance range of a node: its children's (+0x58 / +0x60, not vertex nodes) +0x70 above its
   clearance and its parents' (+0x48 / +0x50) +0x68 below it, depth first */
static void node_range(u8 *n)
{
    int cap = 64, sp = 0; u8 **st = malloc(cap * sizeof *st);
    if (!st) abort();
    #define PUSH(x) do { if (sp == cap) { cap *= 2; st = realloc(st, cap * sizeof *st); if (!st) abort(); } st[sp++] = (x); } while (0)
    PUSH(n);
    while (sp) {
        u8 *m = st[--sp];
        double v = F64(m, 0x70);
        if (m != n && F64(n, 0x10) < v) {
            if (F64(n, 0x68) <= v) { if (F64(n, 0x70) < v) F64(n, 0x70) = v; } else F64(n, 0x68) = v;
        }
        if (PTR(m, 0x58) && I32(PTR(m, 0x58), 0x18) != 4) PUSH(PTR(m, 0x58));
        if (PTR(m, 0x60) && I32(PTR(m, 0x60), 0x18) != 4) PUSH(PTR(m, 0x60));
    }
    PUSH(n);
    while (sp) {
        u8 *m = st[--sp];
        double v = F64(m, 0x68);
        if (m != n && v < F64(n, 0x10)) {
            if (F64(n, 0x68) <= v) { if (F64(n, 0x70) < v) F64(n, 0x70) = v; } else F64(n, 0x68) = v;
        }
        if (PTR(m, 0x48)) PUSH(PTR(m, 0x48));
        if (PTR(m, 0x50)) PUSH(PTR(m, 0x50));
    }
    #undef PUSH
    free(st);
}
/* the lowest bottleneck / top node above bottleneck n on the loop of edge e0's left site (FUN_1805540e0 hv1 l.2061-2124): walk the
   loop by the crossings' edges, stepping over the bottlenecks not above n; *last = the vertex node the walk ends at */
static u8 *bn_parent(u8 *C, u8 *n, u8 *e0, u8 **last)
{
    u8 *s = PTR(e0, 8), *best = PTR(s, 0x30), *cur = s;
    do {
        u8 *e = PTR(PTR(cur, 0x20), 0x38), *en = PTR(e, 0x20);
        if (I32(en, 0x18) == 3 && !below(C, n, en)) {
            u8 *o = PTR(en, 0x28); if (e == o) o = PTR(en, 0x20);
            cur = PTR(o, 8);
        } else {
            if (I32(en, 0x18) == 3 && (I32(best, 0x18) == 2 || (en != best && below(C, en, best)))) best = en;
            cur = PTR(e, 8);
        }
    } while (PTR(PTR(PTR(cur, 0x20), 0x38), 0x20) != n);
    *last = PTR(cur, 0x20);
    return best;
}
static void add_child(u8 *p, u8 *c) { if (!PTR(p, 0x58)) PTR(p, 0x58) = c; else PTR(p, 0x60) = c; }

/* FUN_18054ec00 (hv1 l.617) + FUN_1805538a0 + FUN_1805540e0 + FUN_180554e80 */
int hv_copy(HvCopy *cp, HvGraph *g, int reg, double r)
{
    HvGraph *save = g_cur; g_cur = g;
    u8 *C = cp->raw;
    F64(C, 0x90) = F64(G, 0x98);
    int ns = 0, ne = 0, n2 = 0, n3 = 0, nb = 0;
    for (int k = 0; k <= I32(G, 0x18); k++) {
        u8 *head = PTR(PTR(G, 0) + 0x48 * k, 0x10);
        for (u8 *sn = PTR(head, 0); sn != head; sn = PTR(sn, 0)) if (I32(sn + 0x10, 0x58) == reg) I32(sn + 0x10, 0x30) = ns++;
    }
    for (u8 *ln = PTR(G_EDGES, 0); ln != G_EDGES; ln = PTR(ln, 0)) if (I32(ln + 0x10, 0x58) == reg) I32(ln + 0x10, 0x48) = ne++;
    for (u8 *ln = PTR(G_NODES, 0); ln != G_NODES; ln = PTR(ln, 0)) {
        u8 *N = ln + 0x10;
        if (I32(N, 0x84) != reg) continue;
        if (I32(N, 0x18) == 2) I32(N, 0x80) = n2++;
        else if (I32(N, 0x18) == 3) I32(N, 0x80) = n3++;
    }
    for (u8 *ln = PTR(G_BNECK, 0); ln != G_BNECK; ln = PTR(ln, 0)) if (I32(ln + 0x10, 0x84) == reg) I32(ln + 0x10, 0x80) = nb++;
    cvec(C, 0, ns, 0x78); cvec(C, 0x18, ns, 0x50); cvec(C, 0x98, n2, 0x78); cvec(C, 0xb0, nb, 0x78); cvec(C, 0xc8, n3, 0x78); cvec(C, 0xe0, ne, 0x28);
    #define EC(e) (CV(0xe0) + (ptrdiff_t)I32((e), 0x48) * 0x28)
    #define VN(s) (CV(0) + (ptrdiff_t)I32((s), 0x30) * 0x78)
    for (int k = 0; k <= I32(G, 0x18); k++) {
        u8 *head = PTR(PTR(G, 0) + 0x48 * k, 0x10);
        for (u8 *sn = PTR(head, 0); sn != head; sn = PTR(sn, 0)) {
            u8 *S = sn + 0x10;
            if (I32(S, 0x58) != reg) continue;
            u8 *e = PTR(S, 0x48);
            double pt[2]; edge_at(pt, e, r);
            u8 *v = VN(PTR(e, 0x28));
            memset(v, 0, 0x78);
            F64(v, 0) = pt[0]; F64(v, 8) = pt[1]; I32(v, 0x18) = 4; PTR(v, 0x38) = EC(e);
            F64(v, 0x68) = BIGF; F64(v, 0x70) = negf();
            u8 *c = CV(0x18) + (ptrdiff_t)I32(S, 0x30) * 0x50;
            memset(c, 0, 0x50);
            F64(c, 0) = F64(S, 0x18); F64(c, 8) = F64(S, 0x20); F64(c, 0x10) = r + F64(S, 0x28); I32(c, 0x18) = U8(S, 0x11);
            PTR(c, 0x20) = VN(PTR(PTR(S, 0x48), 0x28)); PTR(c, 0x28) = VN(PTR(PTR(S, 0x50), 0x28));
            I32(c, 0x40) = -1; U8(c, 0x44) = U8(S, 0x5c); I32(c, 0x48) = -1;
        }
    }
    for (u8 *ln = PTR(G_NODES, 0); ln != G_NODES; ln = PTR(ln, 0)) {
        u8 *N = ln + 0x10, *d;
        if (I32(N, 0x84) != reg) continue;
        if (I32(N, 0x18) == 3) {
            d = CV(0xc8) + (ptrdiff_t)I32(N, 0x80) * 0x78; memset(d, 0, 0x78);
            I32(d, 0x18) = 1; PTR(d, 0x38) = EC(PTR(N, 0x60));
        } else if (I32(N, 0x18) == 2) {
            d = CV(0x98) + (ptrdiff_t)I32(N, 0x80) * 0x78; memset(d, 0, 0x78);
            I32(d, 0x18) = 2; PTR(d, 0x30) = EC(PTR(N, 0x60));
        } else continue;
        F64(d, 0) = F64(N, 0); F64(d, 8) = F64(N, 8); F64(d, 0x10) = F64(N, 0x10) - r;
        PTR(d, 0x20) = EC(PTR(N, 0x68)); PTR(d, 0x28) = EC(PTR(N, 0x78));
        F64(d, 0x68) = BIGF; F64(d, 0x70) = negf();
    }
    #define NODEC(N) (I32((N), 0x18) == 2 ? CV(0x98) + (ptrdiff_t)I32((N), 0x80) * 0x78 : I32((N), 0x18) == 3 ? CV(0xc8) + (ptrdiff_t)I32((N), 0x80) * 0x78 : \
                      I32((N), 0x18) == 5 ? CV(0xb0) + (ptrdiff_t)I32((N), 0x80) * 0x78 : (unsigned)(I32((N), 0x18) - 7) < 2 ? CV(0) + (ptrdiff_t)I32((N), 0x80) * 0x78 : NULL)
    for (u8 *ln = PTR(G_BNECK, 0); ln != G_BNECK; ln = PTR(ln, 0)) {
        u8 *N = ln + 0x10;
        if (I32(N, 0x84) != reg) continue;
        u8 *d = NODEC(N);
        if (!d) { g_cur = save; return 0; }   /* the DLL writes through NULL */
        memset(d, 0, 0x78);
        F64(d, 0) = F64(N, 0); F64(d, 8) = F64(N, 8); F64(d, 0x10) = F64(N, 0x10) - r; I32(d, 0x18) = 3;
        PTR(d, 0x20) = EC(PTR(N, 0x68)); PTR(d, 0x28) = EC(PTR(N, 0x78)); PTR(d, 0x38) = EC(PTR(N, 0x60)); PTR(d, 0x40) = EC(PTR(N, 0x70));
        F64(d, 0x68) = BIGF; F64(d, 0x70) = negf();
    }
    for (u8 *ln = PTR(G_EDGES, 0); ln != G_EDGES; ln = PTR(ln, 0)) {
        u8 *e = ln + 0x10;
        if (I32(e, 0x58) != reg) continue;
        u8 *st = PTR(e, 0x40), *s9 = r < F64(st, 0x10) ? NODEC(st) : VN(PTR(e, 0x28));
        u8 *en = NODEC(PTR(e, 0x38));
        edge_rec(EC(e), CV(0x18) + (ptrdiff_t)I32(PTR(e, 0x28), 0x30) * 0x50, CV(0x18) + (ptrdiff_t)I32(PTR(e, 0x30), 0x30) * 0x50, s9, en, I32(e, 0));
    }
    #undef NODEC
    #undef VN
    #undef EC
    g_cur = save;
    /* FUN_1805538a0 (hv1 l.2298): each site's nodes: +0x38 the first node above its start crossing not reached along its own side,
       +0x30 the first non-ordinary node from there */
    for (int i = 0; i < cnt(C, 0, 0x78); i++) {
        u8 *s = CV(0x18) + (ptrdiff_t)i * 0x50;
        u8 *e = PTR(PTR(s, 0x20), 0x38), *n = PTR(e, 0x20);
        if (I32(n, 0x18) == 3) { e = e == PTR(n, 0x28) ? PTR(n, 0x38) : PTR(n, 0x40); n = PTR(e, 0x20); }
        while (I32(n, 0x18) != 2 && PTR(PTR(n, 0x38), 0x10) == s) n = PTR(PTR(n, 0x38), 0x20);
        PTR(s, 0x38) = n;
        while (I32(n, 0x18) == 1) n = PTR(PTR(n, 0x38), 0x20);
        PTR(s, 0x30) = n;
    }
    /* FUN_1805540e0 (hv1 l.1787): per loop of crossings its diameter (three farthest-point passes) and its far vertex node, linked to
       the loop's lowest bottleneck (or its top node); the bottlenecks' parents; the clearance ranges */
    int nsite = cnt(C, 0x18, 0x50);
    cvec(C, 0x30, nsite, 8); PTR(C, 0x38) = PTR(C, 0x30);
    cvec(C, 0x48, nsite, 8); PTR(C, 0x50) = PTR(C, 0x48);
    int nloop = 0;
    for (int i = 0; i < nsite; i++) {
        u8 *s = CV(0x18) + (ptrdiff_t)i * 0x50;
        if (I32(s, 0x40) != -1) continue;
        u8 *p0 = PTR(s, 0x28), *a = p0, *b, *v;
        double dm = 0.0;
        #define NEXTV(x) PTR(PTR(PTR((x), 0x38), 8), 0x20)
        #define DIST(p, q) sqrt((F64(p, 8) - F64(q, 8)) * (F64(p, 8) - F64(q, 8)) + (F64(p, 0) - F64(q, 0)) * (F64(p, 0) - F64(q, 0)))
        v = p0; do { double d = DIST(v, p0); if (dm < d) { a = v; dm = d; } v = NEXTV(v); } while (v != p0);
        dm = 0.0; b = a; v = a; do { double d = DIST(v, a); if (dm < d) { b = v; dm = d; } v = NEXTV(v); } while (v != a);
        dm = 0.0; u8 *bb = b; v = b; do { double d = DIST(v, b); if (dm < d) { bb = v; dm = d; } v = NEXTV(v); } while (v != b);
        #undef DIST
        #undef NEXTV
        F64(C, 0x60) = nloop ? (dm <= F64(C, 0x60) ? F64(C, 0x60) : dm) : dm;
        ((double *)PTR(C, 0x48))[nloop] = dm;
        ((u8 **)PTR(C, 0x30))[nloop] = bb;
        nloop++;
        PTR(C, 0x38) = PTR(C, 0x30) + nloop * 8; PTR(C, 0x50) = PTR(C, 0x48) + nloop * 8;
        u8 *best = PTR(s, 0x30), *cur = s;
        do {
            I32(cur, 0x40) = nloop - 1;
            u8 *e = PTR(PTR(cur, 0x20), 0x38), *en = PTR(e, 0x20);
            if (I32(en, 0x18) == 3 && (I32(best, 0x18) == 2 || (en != best && below(C, en, best)))) best = en;
            cur = PTR(e, 8);
        } while (cur != s);
        PTR(bb, 0x48) = best;
        add_child(best, bb);
    }
    double top = -1.0;
    for (int i = 0; i < cnt(C, 0x98, 0x78); i++) {
        u8 *n = CV(0x98) + (ptrdiff_t)i * 0x78;
        F64(n, 0x68) = F64(n, 0x10); F64(n, 0x70) = F64(n, 0x10);
        absorb((double *)(n + 0x68), PTR(n, 0x20), 1); absorb((double *)(n + 0x68), PTR(n, 0x28), 1); absorb((double *)(n + 0x68), PTR(n, 0x30), 1);
        if (top < F64(n, 0x10)) { F64(C, 0x68) = F64(n, 0); F64(C, 0x70) = F64(n, 8); top = F64(n, 0x10); }
    }
    for (int i = 0; i < cnt(C, 0xb0, 0x78); i++) {
        u8 *n = CV(0xb0) + (ptrdiff_t)i * 0x78, *l1, *l2;
        F64(n, 0x68) = F64(n, 0x10); F64(n, 0x70) = F64(n, 0x10);
        absorb((double *)(n + 0x68), PTR(n, 0x38), 0); absorb((double *)(n + 0x68), PTR(n, 0x40), 0);
        u8 *v13 = PTR(PTR(PTR(n, 0x38), 8), 0x28);
        u8 *p1 = bn_parent(C, n, PTR(n, 0x38), &l1);
        PTR(n, 0x48) = p1;
        u8 *p2 = bn_parent(C, n, PTR(n, 0x40), &l2);
        PTR(n, 0x50) = p2;
        if (v13 == l1) {
            if (p1 != p2 && (I32(p1, 0x18) == 2 || (I32(p2, 0x18) == 3 && below(C, p2, p1)))) PTR(n, 0x48) = p2;
            PTR(n, 0x50) = NULL;
        }
        add_child(PTR(n, 0x48), n);
        if (PTR(n, 0x50)) add_child(PTR(n, 0x50), n);
    }
    for (int i = 0; i < cnt(C, 0x98, 0x78); i++) node_range(CV(0x98) + (ptrdiff_t)i * 0x78);
    for (int i = 0; i < cnt(C, 0xb0, 0x78); i++) node_range(CV(0xb0) + (ptrdiff_t)i * 0x78);
    /* FUN_180554e80 (hv1 l.1589): each site's position in its loop (+0x48), from the loop's far vertex node */
    for (int i = 0; i < nloop; i++) {
        u8 *s0 = PTR(PTR(((u8 **)PTR(C, 0x30))[i], 0x38), 0x10), *s = s0;
        int j = 0;
        do {
            I32(s, 0x48) = j++;
            if (j > nsite) return 0;     /* assert "Voronoi\xoiDiagram.cpp" 0x125 */
            s = PTR(PTR(PTR(s, 0x28), 0x38), 0x10);
        } while (s != s0);
    }
    return 1;
}
#undef CV

/* ------------------------------------------------------------------ stage 3: the offset loops (FUN_180555f60 t = 0, FUN_180557f60)
 * hv = decomp/hmm_vorlib.c, hac = decomp/hmm_ac1.c; the disassembly where Ghidra drops arguments (noted). */

/* FUN_180555130 (hv l.14588; disassembly 1805551f0-180555490 for the operand order): the point of a copy edge at clearance t
   (copy clearances: r - r'); kinds 3 / 6 (constant clearance) assert "Voronoi\xoiEdge.cpp" 0xc5 (returns 0 here) */
static int cedge_at(double *o, u8 *e, double t)
{
    o[0] = 1111.1; o[1] = 2222.2;                       /* 0x40915c6666666666 / 0x40a15c6666666666 */
    int k = I32(e, 0);
    if (k < 1) return 0;
    if (k < 3) {
        u8 *st = PTR(e, 0x18), *en = PTR(e, 0x20);
        double r0 = F64(st, 0x10), d = F64(en, 0x10) - r0, u = d == 0.0 ? 0.0 : (t - r0) / d;
        lerp(o, u, (double *)st, (double *)en);
        return 1;
    }
    if ((unsigned)(k - 4) > 1) return 0;
    u8 *L = PTR(e, 8), *R = PTR(e, 0x10);
    double f = k == 4 ? 1.0 : -1.0;
    if (U8(L, 0x18) && U8(R, 0x18)) {
        double dx = F64(R, 0) - F64(L, 0), ndy = -(F64(R, 8) - F64(L, 8));
        double inv = 1.0 / (dx * dx + ndy * ndy), rl = t + F64(L, 0x10), rr = t + F64(R, 0x10);
        double u = ((rl * rl - rr * rr) * inv + 1.0) * 0.5, q = rl * rl * inv - u * u, s = 0.0;
        if (q > 0.0) s = sqrt(q);
        s = s * f;
        double m[2]; lerp(m, u, (double *)L, (double *)R);
        o[0] = ndy * s + m[0];
        o[1] = dx * s + m[1];
        return 1;
    }
    u8 *P = U8(L, 0x18) ? L : R, *S = U8(L, 0x18) ? R : L;
    double d = (t - (F64(P, 0) * F64(S, 0) + F64(P, 8) * F64(S, 8))) + F64(S, 0x10);
    double q = (t + F64(P, 0x10)) * (t + F64(P, 0x10)) - d * d, s = 0.0;
    if (q > 0.0) s = sqrt(q);
    s = s * f;
    o[0] = -F64(S, 8) * s + (F64(P, 0) + F64(S, 0) * d);
    o[1] = F64(S, 0) * s + (F64(P, 8) + F64(S, 8) * d);
    return 1;
}
/* FUN_1805532e0 / FUN_180553430 (hv l.13170 / 13220): the unit direction at a copy site's start (+0x20 vertex node) / end (+0x28) */
static void site_dir(double *o, u8 *s, int end)
{
    double r = F64(s, 0x10);
    if (r != 0.0) {
        double sg = r <= 0.0 ? -1.0 : 1.0;
        u8 *v = PTR(s, end ? 0x28 : 0x20);
        double dx = F64(v, 0) - F64(s, 0), dy = F64(v, 8) - F64(s, 8), l = sqrt(dy * dy + dx * dx);
        if (l == 0.0) l = 1.0;
        o[1] = 1.0 / l * dy * sg; o[0] = 1.0 / l * dx * sg;
        return;
    }
    u8 *e = PTR(PTR(s, end ? 0x28 : 0x20), 0x38), *q = PTR(e, end ? 0x10 : 8);
    if (U8(q, 0x18)) {
        u8 *a = PTR(e, 0x18), *b = PTR(e, 0x20);
        double dx = F64(b, 0) - F64(a, 0), dy = F64(b, 8) - F64(a, 8), l = sqrt(dy * dy + dx * dx);
        if (l == 0.0) l = 1.0;
        o[1] = 1.0 / l * dy; o[0] = 1.0 / l * dx;
        return;
    }
    o[0] = F64(q, 0); o[1] = F64(q, 8);
}
/* FUN_18005acc0 (disassembly): the pseudo-angle from direction a to b in (-2, 2] */
static double pang_between(const double *a, const double *b)
{
    double p[2] = { b[0] * a[0] + b[1] * a[1], b[0] * a[1] + b[1] * -a[0] };
    double v = pang(p);
    if (v > 2.0) v += -4.0;
    return v;
}
/* FUN_180553580 (hv l.13274; disassembly 1805535d0-180553700): the foot parameter of point p on a copy site (segment: along
   +0x20 -> +0x28; arc: the pseudo-angle fraction of its sweep; -1 for a negative radius or a zero sweep) */
static double site_param(u8 *s, const double *p)
{
    if (!U8(s, 0x18)) {
        u8 *a = PTR(s, 0x20), *b = PTR(s, 0x28);
        double dy = F64(b, 8) - F64(a, 8), dx = F64(b, 0) - F64(a, 0);
        return ((p[1] - F64(a, 8)) * dy + (p[0] - F64(a, 0)) * dx) / (dy * dy + dx * dx);
    }
    if (F64(s, 0x10) < 0.0) return -1.0;
    if (p[0] == F64(s, 0) && p[1] == F64(s, 8)) return 0.0;       /* FUN_18000bcf0 */
    double dy = p[1] - F64(s, 8), dx = p[0] - F64(s, 0), e1[2], e0[2];
    site_dir(e1, s, 1); site_dir(e0, s, 0);
    double q[2] = { e0[0] * dx + e0[1] * dy, e0[1] * dx + -e0[0] * dy };
    double a = pang(q);
    if (a > 2.0) a += -4.0;
    double w = pang_between(e0, e1);
    return w == 0.0 ? -1.0 : a / w;
}

/* the elements of FUN_180557f60 (0x88-byte payload of a std::list node, FUN_180556de0 hv l.15717): +0 the copy edge, +8 t, +0x10
   centre, +0x20 radius above t, then per side (+0x28 / +0x58) the edge, the crossing index, the site (edge +8 / +0x10), its foot
   parameter, the arc's end point on it */
typedef struct HvEl {
    struct HvEl *next, *prev;
    u8 *edge; double t, c[2], rad;
    u8 *e1; int i1; u8 *s1; double f1, p1[2];
    u8 *e2; int i2; u8 *s2; double f2, p2[2];
} HvEl;
static HvEl *el_new(HvEl *before, u8 *e, int i, double t)
{
    HvEl *n = calloc(1, sizeof *n);
    if (!n) abort();
    n->edge = e; n->t = t;
    cedge_at(n->c, e, t);
    n->rad = 0.0;
    n->e1 = e; n->i1 = i; n->s1 = PTR(e, 8); n->f1 = site_param(n->s1, n->c); n->p1[0] = n->c[0]; n->p1[1] = n->c[1];
    n->e2 = e; n->i2 = i; n->s2 = PTR(e, 0x10); n->f2 = site_param(n->s2, n->c); n->p2[0] = n->c[0]; n->p2[1] = n->c[1];
    if (I32(e, 0) == 2) { n->f2 = 0.0; n->f1 = 1.0; }
    n->next = before; n->prev = before->prev; before->prev->next = n; before->prev = n;    /* FUN_1805590e0: insert before */
    return n;
}
static void el_del(HvEl *n) { n->prev->next = n->next; n->next->prev = n->prev; free(n); }

/* ------------------------------------------------------------------ stage 4: Corner rounding (the block of FUN_180557f60 run when
 * CornerTolerance - t != 0, hv l.16337-16407, and the element arcs of its writer, hv l.16842-16905). Ghidra drops the return
 * values of FUN_180556ec0 / FUN_180557520 and several register arguments here, so every function below follows its
 * disassembly (addresses noted); the comparisons keep the DLL's NaN behaviour (comisd / ucomisd: ueq = equal or unordered). */
static int ueq(double a, double b) { return a == b || isnan(a) || isnan(b); }

/* the arc library of mchmm.dll (not in decomp/; disassembly 180051d20-1800522b6). Arc record: centre, radius, start, end, byte
   direction (+0x38). The libm calls are MSVCR120's sin / cos / atan2 (imports at 0x180746758 / 0x180746738 / 0x1807465a8);
   2 pi = 2 acos(-1) (the static 0x1809bf198 set at 180002300 from 0x1809bf190 = acos(-1.0), 1800022d0). */
typedef struct { double c[2], r, s[2], e[2]; int dir; } HvArc;
static double (*hv_sin)(double) = sin, (*hv_cos)(double) = cos, (*hv_atan2)(double, double) = atan2;
void hv_set_libm(double (*s)(double), double (*c)(double), double (*a2)(double, double)) { hv_sin = s; hv_cos = c; hv_atan2 = a2; }
static void arc_normal(double *n, const HvArc *A)
{
    double dy = A->s[1] - A->c[1], dx = A->s[0] - A->c[0];
    if (A->dir) { n[0] = dy; n[1] = -dx; } else { n[0] = -dy; n[1] = dx; }
}
/* FUN_180051d20: the sweep in [0, 2 pi) */
static double arc_sweep(const HvArc *A)
{
    double n[2], twopi = 2.0 * acos(-1.0);
    arc_normal(n, A);
    double ey = A->e[1] - A->c[1], ex = A->e[0] - A->c[0];
    double a = hv_atan2(ey * n[1] + ex * n[0], ey * (A->s[1] - A->c[1]) + ex * (A->s[0] - A->c[0]));
    if (!(a >= 0.0)) a = a + twopi;
    if (a >= twopi) a = a - twopi;
    return a;
}
/* FUN_180052110 (disassembly 180052110-1800522b6): the parameter of point p on the arc (angle / sweep; the angle or the angle +
   2 pi, whichever lies nearer the sweep's range); a zero sweep gives 2, 0.5 or -1 */
static double arc_param(const HvArc *A, const double *p)
{
    double n[2], twopi = 2.0 * acos(-1.0);
    arc_normal(n, A);
    double sw = arc_sweep(A);
    double py = p[1] - A->c[1], px = p[0] - A->c[0];
    double a = hv_atan2(py * n[1] + px * n[0], (A->s[1] - A->c[1]) * py + (A->s[0] - A->c[0]) * px);
    double a2 = twopi + a;
    double d2 = !(a2 >= 0.0) ? -a2 : (a2 > sw ? a2 - sw : 0.0);
    double d1 = !(a >= 0.0) ? -a : (a > sw ? a - sw : 0.0);
    double v = d2 >= d1 ? a : a2;
    if (ueq(sw, 0.0)) return v > 0.0 ? 2.0 : v >= 0.0 ? 0.5 : -1.0;     /* 0x1807bb7a0 / 0x1807bb628 / 0x1807bba88 */
    return v / sw;
}
/* FUN_180051e00 (disassembly 180051e00-180051f33): the point at parameter f */
static void arc_point(double *o, const HvArc *A, double f)
{
    double n[2];
    arc_normal(n, A);
    double ang = arc_sweep(A) * f, s = hv_sin(ang), c = hv_cos(ang);
    o[0] = (A->s[0] - A->c[0]) * c + n[0] * s + A->c[0];
    o[1] = (A->s[1] - A->c[1]) * c + n[1] * s + A->c[1];
}

/* the arc library and FUN_18005ab30 for the pass writer's thinning (hmm_pipethin.c), on the same libm */
double hv_arc_sweep(const double *c, double r, const double *s, const double *e, int dir)
{ HvArc A = { { c[0], c[1] }, r, { s[0], s[1] }, { e[0], e[1] }, dir }; return arc_sweep(&A); }
void hv_arc_point(double *o, const double *c, double r, const double *s, const double *e, int dir, double f)
{ HvArc A = { { c[0], c[1] }, r, { s[0], s[1] }, { e[0], e[1] }, dir }; arc_point(o, &A, f); }
double hv_arc_param(const double *c, double r, const double *s, const double *e, int dir, const double *p)
{ HvArc A = { { c[0], c[1] }, r, { s[0], s[1] }, { e[0], e[1] }, dir }; return arc_param(&A, p); }
double hv_seg_d2(const double *p, const double *a, const double *b) { return seg_d2(p, a, b); }
void hv_lerp(double *o, double t, const double *a, const double *b) { lerp(o, t, a, b); }
double hv_libm_cos(double x) { return hv_cos(x); }

/* FUN_18005bd20 (disassembly): the direction of pseudo-angle a (the inverse of pang, quadrant by trunc(a)) */
static void pang_dir(double *o, double a)
{
    int k = (a > -2147483648.0 && a < 2147483648.0) ? (int)a : INT32_MIN;     /* cvttsd2si */
    double f = a - (double)k;
    switch (k) {
    case 0: case 4: o[1] = f; o[0] = 1.0 - f; return;
    case 1: o[0] = -f; o[1] = 1.0 - f; return;
    case 2: o[0] = f - 1.0; o[1] = -f; return;
    case 3: o[0] = f; o[1] = f - 1.0; return;
    default: o[0] = 0.0; o[1] = 0.0; return;
    }
}
/* FUN_180553720 (hv l.13332; disassembly 180553720-18055389a for the dropped parameter f in xmm3): the point at foot parameter f of
   copy site s, moved out by t (segment: lerp(f, start, end) + t n; arc: the direction at f of its sweep, radius site + t) */
static void site_point(double *o, u8 *s, double t, double f)
{
    if (!U8(s, 0x18)) {
        double L[2];
        lerp(L, f, (double *)PTR(s, 0x20), (double *)PTR(s, 0x28));
        o[0] = t * F64(s, 0) + L[0];
        o[1] = t * F64(s, 8) + L[1];
        return;
    }
    double e1[2], e0[2], d[2];
    site_dir(e1, s, 1); site_dir(e0, s, 0);
    double a = pang_between(e0, e1) * f;
    if (!(a >= 0.0)) a = a + 4.0;                                     /* 0x1807bb7c8 */
    pang_dir(d, a);
    double X = e0[0] * d[0] + e0[1] * d[1], Y = e0[1] * d[0] + -e0[0] * d[1];
    double l = sqrt(Y * Y + X * X);
    if (ueq(l, 0.0)) l = 1.0;
    double rr = t + F64(s, 0x10), inv = 1.0 / l;
    o[1] = inv * Y * rr + F64(s, 8);
    o[0] = inv * X * rr + F64(s, 0);
}

/* FUN_180556ec0 (disassembly 180556ec0-18055750d; return in xmm0): the clearance along copy edge e up to which a corner arc
   centred on e stays clear of the neighbouring offset points p (next element's second point, on e's left site) and q (previous
   element's first point, right site), tolerance tol (copy +0x78 VoronoiPolyEpsilon); -1 when no bound */
static double edge_far_r(u8 *e, const double *p, const double *q, double tol)
{
    int k = I32(e, 0);
    u8 *E = PTR(e, 0x20), *S = PTR(e, 0x18), *L = PTR(e, 8), *R = PTR(e, 0x10);
    if ((((unsigned)k - 1) & ~2u) == 0) {             /* kinds 1 / 3: two segment sites (180556f1f) */
        double lx = F64(L, 0), nly = -F64(L, 8), ry = F64(R, 8), nrx = -F64(R, 0);
        double aE = lx * F64(E, 8) + nly * F64(E, 0), bE = nrx * F64(E, 8) + ry * F64(E, 0);
        double aP = lx * p[1] + nly * p[0], bQ = nrx * q[1] + ry * q[0];
        if (!(aE >= aP + tol) && !(bE >= bQ + tol)) return F64(E, 0x10);
        double aS = lx * F64(S, 8) + nly * F64(S, 0), bS = nrx * F64(S, 8) + ry * F64(S, 0);
        double u1 = (aP - aS) / (aE - aS), u2 = (bQ - bS) / (bE - bS);
        double u = !(u1 > u2) ? u1 : u2;
        if (u >= 1.0) return F64(E, 0x10);
        if (!(u > 0.0)) return F64(S, 0x10);
        if (ueq(F64(S, 0x10), F64(E, 0x10))) return F64(S, 0x10);
        return (1.0 - u) * F64(S, 0x10) + F64(E, 0x10) * u;
    }
    /* the other kinds (180557082) */
    double fE = site_param(L, (double *)E), fp = site_param(L, p);
    if (fE >= fp - tol) {
        double fE2 = site_param(R, (double *)E), fq = site_param(R, q);
        if (!(fE2 > fq + tol)) return F64(E, 0x10);
    }
    double mn;
    if (U8(L, 0x18) && U8(R, 0x18)) {                  /* two arcs (180557111) */
        double m[2];
        lerpf(m, 0.5f, (double *)L, (double *)R);
        double sg = (double)(k == 4 ? 1.0f : -1.0f);
        double A = (F64(R, 0) - F64(L, 0)) * sg, B = -(F64(R, 8) - F64(L, 8)) * sg;
        double px = p[0] - F64(L, 0), npy = -(p[1] - F64(L, 8)), qx = q[0] - F64(R, 0), nqy = -(q[1] - F64(R, 8));
        double D1 = px * A + npy * B, D2 = qx * A + nqy * B;
        if (ueq(D1, 0.0) || ueq(D2, 0.0)) return -1.0;
        double s1 = ((F64(L, 0) - m[0]) * npy + (F64(L, 8) - m[1]) * px) / D1;
        double s2 = ((F64(R, 0) - m[0]) * nqy + (F64(R, 8) - m[1]) * qx) / D2;
        if (!(s2 > 0.0) || !(s1 > 0.0)) return -1.0;
        double x1 = m[0] + B * s1 - F64(L, 0), y1 = m[1] + A * s1 - F64(L, 8);
        double x2 = m[0] + B * s2 - F64(R, 0), y2 = m[1] + A * s2 - F64(R, 8);
        double d1 = sqrt(y1 * y1 + x1 * x1) - F64(L, 0x10), d2 = sqrt(y2 * y2 + x2 * x2) - F64(R, 0x10);
        mn = !(d1 > d2) ? d1 : d2;
    } else {                                           /* an arc P and a segment G (18055735c / 180557376) */
        u8 *P, *Gs; const double *a, *b;
        if (U8(L, 0x18)) { P = L; Gs = R; a = p; b = q; } else { P = R; Gs = L; a = q; b = p; }
        double dx = a[0] - F64(P, 0), dy = a[1] - F64(P, 8), s = dy * dy + dx * dx, r1;
        if (ueq(s, 0.0)) r1 = F64(E, 0x10);
        else {
            double l = sqrt(s);
            if (ueq(l, 0.0)) l = 1.0;
            double inv = 1.0 / l, nx = dx * inv, ny = dy * inv;
            double c = F64(Gs, 0) * nx + F64(Gs, 8) * ny, den = 1.0 - c;
            if (ueq(den, 0.0)) r1 = F64(E, 0x10);
            else r1 = (F64(Gs, 8) * F64(P, 8) + F64(Gs, 0) * F64(P, 0) + c * F64(P, 0x10) - F64(Gs, 0x10)) / den;
        }
        double h = F64(Gs, 0) * b[0] + F64(Gs, 8) * b[1] - F64(Gs, 0x10);
        double fy = b[1] - F64(Gs, 8) * h - F64(P, 8), fx = b[0] - F64(Gs, 0) * h - F64(P, 0);
        double num = F64(P, 0x10) * F64(P, 0x10) - (fy * fy + fx * fx);
        double r2 = num / ((F64(Gs, 0) * fx + F64(Gs, 8) * fy - F64(P, 0x10)) * 2.0);
        mn = !(r1 > r2) ? r1 : r2;
    }
    if (mn >= F64(E, 0x10)) return F64(E, 0x10);
    if (mn > F64(S, 0x10)) return mn;
    return F64(S, 0x10);
}

/* FUN_180557520 (disassembly 180557520-1805579bc; return in xmm0; tt = {t, CornerTolerance} of the spec): the clearance along copy
   edge e at which a circle centred on e, of radius clearance - t + (tau - t), reaches point p; e's end clearance when p is out of
   reach; v (t from FUN_180557c80, e's start clearance from FUN_180557d20) or e's start clearance where there is no solution */
static double edge_reach_r(u8 *e, const double *p, const double *tt, double v)
{
    double t = tt[0], w = tt[1] - t;
    u8 *E = PTR(e, 0x20), *S = PTR(e, 0x18);
    double ex = F64(E, 0) - p[0], ey = F64(E, 8) - p[1];
    double d = sqrt(ex * ex + ey * ey);
    if (!(d > F64(E, 0x10) - t + w)) return F64(E, 0x10);
    int k = I32(e, 0);
    if (k == 1) {                                      /* 1805575e6 */
        double dx = F64(E, 0) - F64(S, 0), dy = F64(E, 8) - F64(S, 8), sr = F64(S, 0x10);
        double ax = F64(S, 0) - p[0], ay = F64(S, 8) - p[1];
        double dr = F64(E, 0x10) - sr, g = sr - t + w;
        double inv = 1.0 / ((dy * dy + dx * dx) - dr * dr);
        double b = -(((ay * dy + dx * ax) - g * dr) * inv);
        double c = ((ay * ay + ax * ax) - g * g) * inv;
        double disc = b * b - c;
        if (!(disc >= 0.0)) return v == t ? v : sr;
        double u = sqrt(disc) + b;
        if (u >= 1.0) return F64(E, 0x10);
        if (!(u > 0.0)) return sr;
        if (ueq(sr, F64(E, 0x10))) return sr;
        return (1.0 - u) * sr + F64(E, 0x10) * u;
    }
    u8 *L = PTR(e, 8), *R = PTR(e, 0x10);
    if (U8(L, 0x18) && U8(R, 0x18)) return v == t ? v : F64(S, 0x10);     /* 180557732 */
    u8 *P = U8(L, 0x18) ? L : R, *Gs = U8(L, 0x18) ? R : L;               /* 180557765 */
    double h = F64(P, 0) * F64(Gs, 0) + F64(P, 8) * F64(Gs, 8) - F64(Gs, 0x10);
    double sg = (double)(k == 4 ? 1.0f : -1.0f);
    double W = w - t;                                  /* (tau - t) - t: 1805577e1 */
    double dpx = p[0] - F64(P, 0), dpy = p[1] - F64(P, 8);
    double kk = dpx * F64(Gs, 0) + dpy * F64(Gs, 8);
    double m = F64(Gs, 0) * sg * dpy + -F64(Gs, 8) * sg * dpx;
    double Q = F64(P, 0x10) + h, Z = W + h + kk, kh = kk + h;
    double V = (kh * kh + m * m - W * W) * Q;
    double den = Q - Z, inv = 1.0 / den;
    double B = -(-(Q * m) * inv);
    double hp = h * h - F64(P, 0x10) * F64(P, 0x10);
    double disc = B * B - (V - hp * Z) * inv;
    if (ueq(den, 0.0) || !(disc >= 0.0)) return v == t ? v : F64(S, 0x10);
    double sq = sqrt(disc), i2q = 1.0 / (Q * 2.0);
    double rp = (hp + (sq + B) * (sq + B)) * i2q, rm = (hp + (B - sq) * (B - sq)) * i2q;
    if (!(sq + B >= 0.0)) return v == t ? v : F64(S, 0x10);
    double r = rp;
    if (B - sq >= 0.0 && rm > rp) r = rm;
    if (!(r >= F64(S, 0x10))) return F64(S, 0x10);
    if (r > F64(E, 0x10)) return F64(E, 0x10);
    return r;
}

/* FUN_1805579c0 (hv l.15962; disassembly 1805579c0-180557c73): move element n's centre up its edge to clearance r (capped at
   t + MaxRadius unless MaxRadius is -1, never below the element's own), its radius = clearance - t, the arc's ends on its two
   sites (the foot parameters clamped to 0..1, snapped where the centre is a node of a vertex-normal edge) */
static void el_round(HvEl *n, double t, double r, double cap)
{
    if (!ueq(cap, -1.0)) { double lim = cap + t; if (r > lim) r = lim; }
    if (r >= n->t) n->t = r;
    u8 *e = n->edge, *E = PTR(e, 0x20), *S = PTR(e, 0x18);
    double T = n->t, P[2];
    if (T == F64(E, 0x10)) { P[0] = F64(E, 0); P[1] = F64(E, 8); }
    else if (T == F64(S, 0x10)) { P[0] = F64(S, 0); P[1] = F64(S, 8); }
    else cedge_at(P, e, T);
    n->c[0] = P[0]; n->c[1] = P[1];
    n->rad = T - t;
    u8 *s1 = n->s1, *s2 = n->s2;
    n->f1 = site_param(s1, n->c);
    n->f2 = site_param(s2, n->c);
    #define VEDGE(s, o) PTR(PTR((s), (o)), 0x38)
    if (n->c[0] == F64(E, 0) && n->c[1] == F64(E, 8)) {
        if (I32(VEDGE(s1, 0x20), 0) == 2 && PTR(VEDGE(s1, 0x20), 0x20) == E) n->f1 = 0.0;
        if (I32(VEDGE(s2, 0x28), 0) == 2 && PTR(VEDGE(s2, 0x28), 0x20) == E) n->f2 = 1.0;
    }
    if (n->c[0] == F64(S, 0) && n->c[1] == F64(S, 8)) {
        if (I32(VEDGE(s1, 0x28), 0) == 2 && PTR(VEDGE(s1, 0x28), 0x20) == S) n->f1 = 1.0;
        if (I32(VEDGE(s2, 0x20), 0) == 2 && PTR(VEDGE(s2, 0x20), 0x20) == S) n->f2 = 0.0;
    }
    #undef VEDGE
    if (ueq(0.0, n->rad)) {
        n->p1[0] = n->p2[0] = P[0]; n->p1[1] = n->p2[1] = P[1];
        return;
    }
    n->f1 = n->f1 >= 0.0 ? (n->f1 > 1.0 ? 1.0 : n->f1) : 0.0;
    n->f2 = n->f2 >= 0.0 ? (n->f2 > 1.0 ? 1.0 : n->f2) : 0.0;
    site_point(n->p1, s1, t, n->f1);
    site_point(n->p2, s2, t, n->f2);
}
/* FUN_180557c80 (hv l.16121; disassembly 180557c80-180557d14): Corner rounding of element n on a kind 1 / 4 / 5 edge, between the
   next element's second point np2 and the previous one's first point pp1 */
static void el_corner(HvEl *n, const double *np2, const double *pp1, const double *tt, double cap, double tol)
{
    double r1 = edge_far_r(n->edge, np2, pp1, tol);
    if (ueq(r1, -1.0)) r1 = n->t;
    double r2 = edge_reach_r(n->edge, n->c, tt, tt[0]);
    el_round(n, tt[0], !(r2 > r1) ? r2 : r1, cap);
}
/* FUN_180557d20 (hv l.16149; disassembly 180557d20-180557f56): the element replacing a (next) and b (current) whose edges end at
   the same ordinary node: centred on that node's leaving edge (+0x38) from its start, a's first side and b's second side; its
   clearance the least of FUN_180556ec0 and FUN_180557520 for every non-vertex-normal crossing it spans */
static HvEl *el_merge(HvEl *before, const HvEl *a, const HvEl *b, const int *ev, int ne, u8 *C, const double *nnp2,
                      const double *pvp1, const double *tt, double cap, double tol)
{
    HvEl *n = calloc(1, sizeof *n);
    if (!n) abort();
    u8 *e = PTR(PTR(a->edge, 0x20), 0x38), *S = PTR(e, 0x18);
    n->edge = e; n->t = F64(S, 0x10); n->c[0] = F64(S, 0); n->c[1] = F64(S, 8); n->rad = n->t - tt[0];
    n->e1 = a->e1; n->i1 = a->i1; n->s1 = a->s1; n->f1 = a->f1; n->p1[0] = a->p1[0]; n->p1[1] = a->p1[1];
    n->e2 = b->e2; n->i2 = b->i2; n->s2 = b->s2; n->f2 = b->f2; n->p2[0] = b->p2[0]; n->p2[1] = b->p2[1];
    double r1 = edge_far_r(e, nnp2, pvp1, tol);
    if (ueq(r1, -1.0)) r1 = n->t;
    double m = F64(PTR(e, 0x20), 0x10);
    int i = n->i2 - 1;
    if (i != n->i1) {
        do {
            i = (i + 1) % ne;
            u8 *x = PTR(C, 0xe0) + (ptrdiff_t)ev[i] * 0x28;
            if (I32(x, 0) != 2) {
                double q[2]; cedge_at(q, x, tt[0]);
                double v = edge_reach_r(e, q, tt, F64(PTR(e, 0x18), 0x10));
                m = !(v > m) ? v : m;
            }
        } while (i != n->i1);
    }
    el_round(n, tt[0], !(m > r1) ? m : r1, cap);
    n->next = before; n->prev = before->prev; before->prev->next = n; before->prev = n;
    return n;
}

/* FUN_180554a20 (hv l.14280): the site indices of the loop of vertex node B under node best (r9 = best, r8 = B in the call from
   FUN_180554c70; disassembly 180554a4a), stepping across the bottlenecks not above best; returns the lowest one met */
static int loop_sites(u8 *C, int **out, int *n, u8 *B, u8 *best)
{
    u8 *s0;
    if (PTR(best, 0x60) == NULL) s0 = PTR(PTR(best, 0x20), 8);
    else {
        if (I32(B, 0x18) == 3) return -1;      /* FUN_1805549c0 (the children's loops): not reached from FUN_180557f60 (B is a vertex node) */
        int lp = I32(PTR(PTR(B, 0x38), 8), 0x40);
        s0 = PTR(PTR(best, 0x20), 8);
        if (I32(s0, 0x40) != lp) s0 = PTR(PTR(best, 0x28), 8);
    }
    int cap = 64; *out = malloc(cap * sizeof **out); *n = 0;
    if (!*out) abort();
    u8 *s = s0, *low = NULL;
    do {
        if (*n == cap) { cap *= 2; *out = realloc(*out, cap * sizeof **out); if (!*out) abort(); }
        (*out)[(*n)++] = (int)((s - PTR(C, 0x18)) / 0x50);
        u8 *e = PTR(PTR(s, 0x20), 0x38), *en = PTR(e, 0x20);
        if (I32(en, 0x18) == 3 && (I32(best, 0x18) != 3 || (en != best && !below(C, best, en)))) {
            if (low != en && (low == NULL || below(C, low, en))) low = en;
            u8 *o = PTR(en, 0x28); if (e == o) o = PTR(en, 0x20);
            s = PTR(o, 8);
        } else s = PTR(e, 8);
        if (*n > 4 * cnt(C, 0x18, 0x50) + 8) return -1;
    } while (s != s0);
    return 0;
}

/* FUN_180557f60 (hv l.16253) for one spec {B, best, t, tau}: the offset loop at clearance t, written through w, with Corner
   rounding when tau != t (FUN_180557c80 / FUN_180557d20, stage 4). Returns 1, or 0 where the DLL would assert or corrupt its
   list. */
int hv_offset_loop(HvCopy *cp, const HvWriter *w, u8 *B, u8 *best, double t, double tau, double mindev, double maxrad,
                   double minrad, double minchord, int kind, int p9)
{
    u8 *C = cp->raw;
    const double tt[2] = { t, tau };
    int *sv = NULL, ns = 0;
    if (loop_sites(C, &sv, &ns, B, best) < 0) { free(sv); return 0; }
    /* FUN_180554c70 (hv l.14394): per site its crossing edge, walked up past the nodes at or below t; consecutive repeats dropped */
    int *ev = malloc((ns + 1) * sizeof *ev), ne = 0;
    if (!ev) abort();
    u8 *last = NULL;
    for (int i = 0; i < ns; i++) {
        u8 *e = PTR(PTR(PTR(C, 0x18) + (ptrdiff_t)sv[i] * 0x50, 0x20), 0x38);
        while (F64(PTR(e, 0x20), 0x10) <= t) {
            u8 *m = PTR(e, 0x20), *x = PTR(m, 0x40);
            if (x == NULL || e == PTR(m, 0x28)) x = PTR(m, 0x38);
            e = x;
        }
        if (e != last) { ev[ne++] = (int)((e - PTR(C, 0xe0)) / 0x28); last = e; }
    }
    free(sv);
    #define EDGE(i) (PTR(C, 0xe0) + (ptrdiff_t)ev[(i)] * 0x28)
    HvEl head; head.next = head.prev = &head;
    for (int i = 0; i < ne; i++) el_new(&head, EDGE(i), i, t);
    if (!ueq(tau - t, 0.0)) {
        /* Corner rounding (hv l.16337-16372; disassembly 1805580e0-180558219): per element on a kind 1 / 4 / 5 edge FUN_180557c80
           with the next element's second point and the previous one's first point (cyclic) and the copy's +0x78 (VoronoiPolyEpsilon);
           on a vertex-normal edge (kind 2) the centre goes to the edge's end, at most MaxRadius above t (MaxRadius -1: no cap) */
        double tol = F64(C, 0x78);
        for (HvEl *n = head.next; n != &head; n = n->next) {
            int k = I32(n->edge, 0);
            if (k == 1 || (unsigned)(k - 4) <= 1) {
                HvEl *pv = n == head.next ? head.prev : n->prev, *nx = n == head.prev ? head.next : n->next;
                el_corner(n, nx->p2, pv->p1, tt, maxrad, tol);
            } else if (k == 2) {
                u8 *E = PTR(n->edge, 0x20);
                n->t = F64(E, 0x10);
                if (!ueq(maxrad, -1.0) && n->t - t > maxrad) { n->t = t + maxrad; cedge_at(n->c, n->edge, n->t); }
                else { n->c[0] = F64(E, 0); n->c[1] = F64(E, 8); }
                n->rad = n->t - t;
            }
        }
        /* the merge (hv l.16373-16407; disassembly 180558224-1805583ec): two successive elements whose edges end at the same
           ordinary node (kind 1), both centred at its clearance, become one (FUN_180557d20) inserted before the one after them;
           the walk resumes at the element before the new one (or at the new one when it is first) */
        for (HvEl *n = head.next; n != &head;) {
            HvEl *nx = n == head.prev ? head.next : n->next;
            u8 *m = PTR(n->edge, 0x20);
            if (m == PTR(nx->edge, 0x20) && I32(m, 0x18) == 1 && n->t == F64(m, 0x10) && nx->t == F64(PTR(nx->edge, 0x20), 0x10)) {
                if (nx == n) { while (head.next != &head) el_del(head.next); free(ev); return 0; }   /* one element: the DLL frees it twice */
                HvEl *pv = n == head.next ? head.prev : n->prev, *nn = nx == head.prev ? head.next : nx->next;
                HvEl *ins = el_merge(nn, nx, n, ev, ne, C, nn->p2, pv->p1, tt, maxrad, tol);
                el_del(n); el_del(nx);
                n = ins == head.next ? ins : ins->prev;
            } else n = n->next;
        }
    }
    /* the keep test (hv l.16416-16465): an element is kept when its radius >= MinRadius, its deviation (the farthest crossing point
       it spans from the centre, minus the radius) >= MinDeviation and its chord >= MinArcChordLength; else replaced by the plain
       crossings it spans */
    for (HvEl *n = head.next; n != &head;) {
        if (minrad <= n->rad) {
            double dv = 0.0; int i = n->i2 - 1;
            do {
                i = (i + 1) % ne;
                double p[2]; cedge_at(p, EDGE(i), t);
                double d = sqrt((n->c[0] - p[0]) * (n->c[0] - p[0]) + (n->c[1] - p[1]) * (n->c[1] - p[1])) - n->rad;
                dv = d <= dv ? dv : d;
            } while (i != n->i1);
            if (mindev <= dv && minchord <= sqrt((n->p1[0] - n->p2[0]) * (n->p1[0] - n->p2[0]) + (n->p1[1] - n->p2[1]) * (n->p1[1] - n->p2[1]))) { n = n->next; continue; }
        }
        int i = n->i2 - 1;
        do { i = (int)((long long)(i + 1) % ne); el_new(n, EDGE(i), i, t); } while (i != n->i1);
        HvEl *nx = n->next; el_del(n); n = nx;
    }
    /* the writer (hv l.16582-16920): start at the last element's first point; per element the span from the previous element's
       first point to its second point along the previous element's first site (a line, or an arc of radius site + t about a point
       site - a line when the arc would be 180 degrees or more, log "Write line rather than major arc"), then its own arc */
    if (head.next == &head) { free(ev); return 1; }
    HvEl *L = head.prev;
    w->start(w->ctx, L->p1, (p9 == 0 && U8(L->s1, 0x44)) ? 0 : 1, kind);
    double pen[2] = { L->p1[0], L->p1[1] };
    for (HvEl *n = head.next; n != &head; n = n->next) {
        HvEl *pv = n == head.next ? head.prev : n->prev;
        u8 *s3 = pv->s1, *s4 = n->s2;
        int fl = (p9 == 0 && U8(s4, 0x44)) ? 0 : 1;
        if (sqrt((n->p2[0] - pv->p1[0]) * (n->p2[0] - pv->p1[0]) + (n->p2[1] - pv->p1[1]) * (n->p2[1] - pv->p1[1])) != 0.0) {
            if (!U8(s3, 0x18)) w->line(w->ctx, n->p2, fl);
            else {
                double rr = F64(s3, 0x10) + t;
                int major = 0;
                if (s3 != s4) {
                    double d34 = -(pen[1] - F64(s3, 8)) * (n->p2[0] - F64(s3, 0)) + (pen[0] - F64(s3, 0)) * (n->p2[1] - F64(s3, 8));
                    double d35 = (n->p2[1] - F64(s3, 8)) * (pen[1] - F64(s3, 8)) + (pen[0] - F64(s3, 0)) * (n->p2[0] - F64(s3, 0));
                    if (d35 != 0.0 || d34 != 0.0) {
                        double a; int skip = 0;
                        if (d34 < 0.0) {
                            if (0.0 <= d35) { a = d35 / (d35 - d34) + 3.0; if (a == 4.0) skip = 1; }
                            else a = 2.0 - d34 / (-d35 - d34);
                        } else if (d35 < 0.0) a = 1.0 - d35 / (d34 - d35);
                        else a = d34 / (d35 + d34);
                        if (!skip && 2.0 <= a) major = 1;
                    }
                }
                if (major) w->line(w->ctx, n->p2, fl);
                else w->arc(w->ctx, n->p2, (double *)s3, rr, 0, 0, fl);
            }
            pen[0] = n->p2[0]; pen[1] = n->p2[1];
        }
        if (!ueq(sqrt((n->p1[1] - n->p2[1]) * (n->p1[1] - n->p2[1]) + (n->p1[0] - n->p2[0]) * (n->p1[0] - n->p2[0])), 0.0)) {
            /* the element's own arc (hv l.16842-16905; disassembly 180558ce8-180558fe3). Where param_9 is 0 and its two sites' flags
               differ, the arc is split first: at the first crossing it spans whose left / right sites carry the first / second
               site's flags, the direction from the centre to that crossing's point at t gives the split parameter on the arc
               p2 -> p1 (FUN_180052110), clamped to 0.1 .. 0.9 (0x1809c0f60 / 68 from 0x1807bbc10), the point FUN_180051e00; the
               first part carries the second site's flag */
            if (p9 == 0 && U8(n->s2, 0x44) != U8(n->s1, 0x44)) {
                int i = n->i2 - 1;
                do {
                    i = (i + 1) % ne;
                    if (U8(PTR(EDGE(i), 8), 0x44) == U8(n->s1, 0x44) && U8(PTR(EDGE(i), 0x10), 0x44) == U8(n->s2, 0x44)) break;
                } while (i != n->i1);
                double q[2]; cedge_at(q, EDGE(i), t);
                double dy = q[1] - n->c[1], dx = q[0] - n->c[0], k = n->rad / sqrt(dy * dy + dx * dx);
                double m[2] = { k * dx + n->c[0], k * dy + n->c[1] };
                double ry = n->p2[1] - n->c[1], rx = n->p2[0] - n->c[0];
                HvArc A = { { n->c[0], n->c[1] }, sqrt(ry * ry + rx * rx), { n->p2[0], n->p2[1] }, { n->p1[0], n->p1[1] }, 1 };
                double f = arc_param(&A, m);
                f = f >= 0.1 ? (f > 0.9 ? 0.9 : f) : 0.1;
                A.r = n->rad;
                double sp[2]; arc_point(sp, &A, f);
                w->arc(w->ctx, sp, n->c, n->rad, 1, 1, U8(n->s2, 0x44) == 0);
            }
            w->arc(w->ctx, n->p1, n->c, n->rad, 1, 1, (p9 == 0 && U8(n->s1, 0x44)) ? 0 : 1);
            pen[0] = n->p1[0]; pen[1] = n->p1[1];
        }
    }
    w->end(w->ctx);
    #undef EDGE
    while (head.next != &head) el_del(head.next);
    free(ev);
    return 1;
}

/* FUN_180555f60's t = 0 branch (hv l.15164-15182: one spec {loop vertex node, its +0x48 node, 0, CornerTolerance} per loop) and
   FUN_180406b00's loop over the specs (hp l.2000-2050, the kind-0 call): MaxRadius 0x1bd2, the kind 0x1e for the loop whose
   diameter is the largest (copy +0x60 == +0x48[i]) else 0x1f */
int hv_offset_region(HvCopy *cp, const HvWriter *w, double tau, double mindev, double maxrad, double minrad, double minchord, int p9)
{
    u8 *C = cp->raw;
    int nl = (int)((PTR(C, 0x38) - PTR(C, 0x30)) / 8);
    for (int i = 0; i < nl; i++) {
        u8 *B = ((u8 **)PTR(C, 0x30))[i];
        int kind = 0x1f - (F64(C, 0x60) == ((double *)PTR(C, 0x48))[i]);
        int r = hv_offset_loop(cp, w, B, PTR(B, 0x48), 0.0, tau, mindev, maxrad, minrad, minchord, kind, p9);
        if (r != 1) return r;
    }
    return 1;
}
