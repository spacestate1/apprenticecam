// offset.cpp - C wrapper around Clipper 6.2.0 (Angus Johnson, Boost Software License; vendor/clipper6)
// for polygon offsetting / booleans. Coordinates are scaled to integers (1e5 per unit).
#include "vendor/clipper6/clipper.hpp"
#include <cmath>
#include <cstdlib>
#include "offset.h"

using namespace ClipperLib;
static const double S = 1e5;
// llround (half away from zero) without the libm call (ours, speed-up): exact for |v| < 2^52 (the fraction v - trunc(v) is exact there)
static inline long long llround_ex(double v) {
    if (!(v > -4503599627370496.0 && v < 4503599627370496.0)) return llround(v);
    long long t = (long long)v; double f = v - (double)t;
    return f >= 0.5 ? t + 1 : f <= -0.5 ? t - 1 : t;
}
static thread_local int cl_err;                       // set when input was invalid or Clipper threw; read by cl_error_take()
extern "C" int cl_error_take(void) { int e = cl_err; cl_err = 0; return e; }

static Paths to_paths(const CPaths *in) {
    Paths out(in->n);
    for (int i = 0; i < in->n; i++) {
        out[i].reserve(in->p[i].n);
        for (int k = 0; k < in->p[i].n; k++)
        {
            double x = in->p[i].xy[2 * k] * S, y = in->p[i].xy[2 * k + 1] * S;
            if (!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > 1e15 || std::fabs(y) > 1e15) { cl_err = 1; continue; }   // never into Clipper
            out[i].push_back(IntPoint((cInt)llround_ex(x), (cInt)llround_ex(y)));
        }
    }
    return out;
}

static CPaths from_paths(const Paths &ps) {
    CPaths r;
    r.n = (int)ps.size();
    r.p = (CPath *)calloc(r.n ? r.n : 1, sizeof(CPath));
    for (int i = 0; i < r.n; i++) {
        r.p[i].n = (int)ps[i].size();
        r.p[i].xy = (double *)malloc(sizeof(double) * 2 * (r.p[i].n ? r.p[i].n : 1));
        for (int k = 0; k < r.p[i].n; k++) { r.p[i].xy[2 * k] = ps[i][k].X / S; r.p[i].xy[2 * k + 1] = ps[i][k].Y / S; }
    }
    return r;
}

extern "C" CPaths cl_offset(const CPaths *in, double delta, double arc_tol) {
    try {
    // input polygons are first normalised (even-odd union) so orientation is consistent: outer CCW, holes CW;
    // then a positive delta grows outer boundaries and shrinks holes
    Clipper c; Paths norm;
    c.AddPaths(to_paths(in), ptSubject, true);
    c.Execute(ctUnion, norm, pftEvenOdd, pftEvenOdd);
    ClipperOffset co(2.0, arc_tol * S);
    co.AddPaths(norm, jtRound, etClosedPolygon);
    Paths out; co.Execute(out, delta * S);
    return from_paths(out);
    } catch (...) { cl_err = 1; }
    CPaths r; r.n = 0; r.p = (CPath *)calloc(1, sizeof(CPath)); return r;
}

extern "C" CPaths cl_union_evenodd(const CPaths *in) {
    try {
    Clipper c; Paths out;
    c.AddPaths(to_paths(in), ptSubject, true);
    c.Execute(ctUnion, out, pftEvenOdd, pftEvenOdd);
    return from_paths(out);
    } catch (...) { cl_err = 1; }
    CPaths r; r.n = 0; r.p = (CPath *)calloc(1, sizeof(CPath)); return r;
}

extern "C" double cl_area(const CPath *p) {
    try {
    double a = 0;
    for (int i = 0; i < p->n; i++) {
        int j = (i + 1) % p->n;
        a += p->xy[2 * i] * p->xy[2 * j + 1] - p->xy[2 * j] * p->xy[2 * i + 1];
    }
    return a / 2;
    } catch (...) { cl_err = 1; }
    return 0;
}

extern "C" void cl_free(CPaths *ps) {
    for (int i = 0; i < ps->n; i++) free(ps->p[i].xy);
    free(ps->p);
    ps->p = nullptr; ps->n = 0;
}

extern "C" CPaths cl_offset_lines(const CPaths *in, double delta, int closed, double arc_tol) {
    try {
    // the area swept by a disc of radius delta along the given lines (closed loops or open polylines)
    ClipperOffset co(2.0, arc_tol * S);
    co.AddPaths(to_paths(in), jtRound, closed ? etClosedLine : etOpenRound);
    Paths out; co.Execute(out, delta * S);
    return from_paths(out);
    } catch (...) { cl_err = 1; }
    CPaths r; r.n = 0; r.p = (CPath *)calloc(1, sizeof(CPath)); return r;
}

extern "C" CPaths cl_difference(const CPaths *a, const CPaths *b) {
    try {
    Clipper c; Paths out;
    c.AddPaths(to_paths(a), ptSubject, true);
    c.AddPaths(to_paths(b), ptClip, true);
    c.Execute(ctDifference, out, pftEvenOdd, pftNonZero);
    return from_paths(out);
    } catch (...) { cl_err = 1; }
    CPaths r; r.n = 0; r.p = (CPath *)calloc(1, sizeof(CPath)); return r;
}

extern "C" CPaths cl_clip_open(const CPaths *lines, const CPaths *clip, int keep_inside) {
    try {
    // open polylines kept inside (or outside) the clip region (non-zero fill)
    Clipper c; PolyTree t; Paths out;
    c.AddPaths(to_paths(lines), ptSubject, false);
    c.AddPaths(to_paths(clip), ptClip, true);
    c.Execute(keep_inside ? ctIntersection : ctDifference, t, pftNonZero, pftNonZero);
    OpenPathsFromPolyTree(t, out);
    return from_paths(out);
    } catch (...) { cl_err = 1; }
    CPaths r; r.n = 0; r.p = (CPath *)calloc(1, sizeof(CPath)); return r;
}

extern "C" CPaths cl_bool(const CPaths *a, const CPaths *b, int op) {
    try {
    // 0 union, 1 intersection, 2 difference a - b, 3 xor; both operands non-zero filled
    static const ClipType ct[4] = { ctUnion, ctIntersection, ctDifference, ctXor };
    Clipper c; Paths out;
    c.AddPaths(to_paths(a), ptSubject, true);
    if (b && b->n) c.AddPaths(to_paths(b), ptClip, true);
    c.Execute(ct[op & 3], out, pftNonZero, pftNonZero);
    return from_paths(out);
    } catch (...) { cl_err = 1; }
    CPaths r; r.n = 0; r.p = (CPath *)calloc(1, sizeof(CPath)); return r;
}

extern "C" CPaths cl_offset_nz(const CPaths *in, double delta, double arc_tol) {
    try {
    // offset of a non-zero region (orientation as given: outer CCW, holes CW)
    ClipperOffset co(2.0, arc_tol * S);
    co.AddPaths(to_paths(in), jtRound, etClosedPolygon);
    Paths out; co.Execute(out, delta * S);
    return from_paths(out);
    } catch (...) { cl_err = 1; }
    CPaths r; r.n = 0; r.p = (CPath *)calloc(1, sizeof(CPath)); return r;
}

extern "C" double cl_total_area(const CPaths *p) {
    try {
    double a = 0;
    for (int i = 0; i < p->n; i++) a += cl_area(&p->p[i]);
    return a;
    } catch (...) { cl_err = 1; }
    return 0;
}

extern "C" CPaths cl_offset_join(const CPaths *in, double delta, int join, double arc_tol) {
    try {
    // offset of a non-zero region with the given corner join: 0 round, 1 miter (sharp, limit 4 x delta), 2 square
    static const JoinType jt[3] = { jtRound, jtMiter, jtSquare };
    ClipperOffset co(4.0, arc_tol * S);
    co.AddPaths(to_paths(in), jt[join < 0 || join > 2 ? 0 : join], etClosedPolygon);
    Paths out; co.Execute(out, delta * S);
    return from_paths(out);
    } catch (...) { cl_err = 1; }
    CPaths r; r.n = 0; r.p = (CPath *)calloc(1, sizeof(CPath)); return r;
}

static void add_tree(const PolyNode *n, Paths &out, std::vector<int> &own, int owner) {
    int me = owner;
    if (!n->IsHole()) { me = (int)out.size(); out.push_back(n->Contour); own.push_back(me); }
    else { out.push_back(n->Contour); own.push_back(owner); }
    for (int i = 0; i < n->ChildCount(); i++) add_tree(n->Childs[i], out, own, me);
}
extern "C" CPaths cl_group(const CPaths *in, int **owner) {
    try {
    // the region (non-zero) as outer boundaries (CCW) each followed by its holes (CW); owner[i] = index of the
    // outer boundary path i belongs to (itself for an outer)
    Clipper c; PolyTree tree;
    c.AddPaths(to_paths(in), ptSubject, true);
    c.Execute(ctUnion, tree, pftNonZero, pftNonZero);
    Paths out; std::vector<int> own;
    for (int i = 0; i < tree.ChildCount(); i++) add_tree(tree.Childs[i], out, own, -1);
    CPaths r = from_paths(out);
    *owner = (int *)malloc(sizeof(int) * (own.size() ? own.size() : 1));
    for (size_t i = 0; i < own.size(); i++) (*owner)[i] = own[i];
    return r;
    } catch (...) { cl_err = 1; }
    CPaths r; r.n = 0; r.p = (CPath *)calloc(1, sizeof(CPath)); return r;
}
