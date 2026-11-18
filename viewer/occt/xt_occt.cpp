/* xt_occt.cpp - Parasolid XT -> OpenCASCADE. Sources and conventions: xt_occt.h. */
#include "xt_occt.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <set>
#include <functional>
#include <chrono>

#include <BRepLib.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <ElCLib.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_Circle.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Line.hxx>
#include <Geom_Plane.hxx>
#include <ShapeBuild_ReShape.hxx>
#include <ShapeFix_Face.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <ShapeFix_Shape.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax22d.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Lin.hxx>
#include <gp_Elips.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <GeomProjLib.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <cstdlib>
/* xtfix (2026-10-26): CONE / SPHERE / TORUS / OFFSET_SURF / BLENDED_EDGE / INTERSECTION (formats/xt_occt_fixes.md) */
#include <Geom_ConicalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_ElementarySurface.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomAPI_PointsToBSplineSurface.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <TColgp_HArray1OfPnt2d.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <TColStd_HArray1OfReal.hxx>
#include <Precision.hxx>
#include "xt_refgeom.h"

namespace xtocc {

/* ------------------------------------------------------------------------------------------- schema + parse */
static int tsize(char t) {        /* field encodings: formats/xt.md, viewer/xt_mesh.c tsize */
    switch (t) {
    case 'd': case 't': return 4;
    case 'f': return 8;
    case 'p': case 'n': return 2;
    case 'c': case 'l': case 'u': return 1;
    case 'v': return 24;
    case 'b': return 48;
    case 'h': return 24;   /* xtfix: an hvec is transmitted as its pvec only - Parasolid XT Format Reference (2006) p.44 "only the pvec part of
                              an hvec is actually transmitted"; measured: RASTER.mcam's CHART / LIMIT nodes (32 stops the parse at 0xcdac) */
    case 'i': return 16;
    default: return -1;
    }
}

std::shared_ptr<Schema> load_schema(const std::string &path) {   /* same reading as viewer/xt_mesh.c xt_schema_load */
    std::ifstream in(path);
    if (!in) return nullptr;
    auto s = std::make_shared<Schema>();
    s->nd.resize(400);
    NodeDef *cur = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        int code, a, b, c; char name[64], ftype, fname[64];
        if (sscanf(line.c_str(), "%d %63[A-Z_0-9];", &code, name) == 2 && code > 0 && code < 400) {
            const char *p = strchr(line.c_str(), ';'); p = p ? strchr(p + 1, ';') : nullptr;
            if (p && sscanf(p + 1, "%d %d %d", &a, &b, &c) == 3) {
                cur = &s->nd[code]; cur->defined = true; cur->var = c; cur->name = name; cur->f.clear();
                continue;
            }
        }
        if (cur && sscanf(line.c_str(), "%63[a-z_0-9]; %c; %d %d %d", fname, &ftype, &a, &b, &c) == 5)
            cur->f.push_back({fname, ftype, a, c, -1});
    }
    for (auto &d : s->nd) {
        if (!d.defined) continue;
        int o = 0, last = -1;
        for (size_t i = 0; i < d.f.size(); i++) if (d.f[i].xmt) last = (int)i;
        for (size_t i = 0; i < d.f.size(); i++) {
            if (!d.f[i].xmt) continue;
            d.f[i].off = o;
            int sz = tsize(d.f[i].type);
            if (sz < 0) { d.size = -1; break; }
            if (d.var && (int)i == last) continue;
            o += sz * (d.f[i].n > 1 ? d.f[i].n : 1);
        }
        if (d.size == 0) d.size = o;
    }
    return s;
}

const Node *Model::node(int idx) const {
    if (idx <= 1 || idx >= (int)by_idx.size() || !by_idx[idx].p) return nullptr;
    return &by_idx[idx];
}
const char *Model::tname(const Node *n) const { return n ? s->nd[n->type].name.c_str() : "(null)"; }
bool Model::is(const Node *n, const char *t) const { return n && s->nd[n->type].name == t; }
static const FieldDef *fdef(const Schema *s, int type, const char *f) {
    for (auto &d : s->nd[type].f) if (d.off >= 0 && d.name == f) return &d;
    return nullptr;
}
bool Model::has(const Node *n, const char *f) const { return n && fdef(s, n->type, f); }
int Model::ptr(const Node *n, const char *f, int k) const {
    const FieldDef *d = n ? fdef(s, n->type, f) : nullptr;
    if (!d || k >= std::max(1, d->n)) return 1;
    return n->p[d->off + 2 * k] | n->p[d->off + 2 * k + 1] << 8;
}
double Model::dbl(const Node *n, const char *f, int k) const {
    const FieldDef *d = n ? fdef(s, n->type, f) : nullptr;
    double v = 0;
    if (d) memcpy(&v, n->p + d->off + 8 * k, 8);
    return v;
}
gp_XYZ Model::vec(const Node *n, const char *f) const { return gp_XYZ(dbl(n, f, 0), dbl(n, f, 1), dbl(n, f, 2)); }
int Model::chr(const Node *n, const char *f) const {
    const FieldDef *d = n ? fdef(s, n->type, f) : nullptr;
    return d ? n->p[d->off] : 0;
}

int Model::sval(const Node *n, const char *f, int k) const {
    const FieldDef *d = n ? fdef(s, n->type, f) : nullptr;
    int16_t v = 0;
    if (d) memcpy(&v, n->p + d->off + 2 * k, 2);
    return v;
}
int Model::ival(const Node *n, const char *f) const {
    const FieldDef *d = n ? fdef(s, n->type, f) : nullptr;
    int32_t v = 0;
    if (d) memcpy(&v, n->p + d->off, 4);
    return v;
}

bool parse(const Schema &s, const uint8_t *b, size_t len, Model &m, std::string &err) {   /* viewer/xt_mesh.c parse */
    m.s = &s;
    const uint8_t *h = (const uint8_t *)memmem(b, len, "END_OF_HEADER", 13);
    if (!h) { err = "no XT header"; return false; }
    const uint8_t *q = (const uint8_t *)memchr(h, '\n', b + len - h);
    if (!q || q + 1 >= b + len || q[1] != 'B') { err = "not a binary XT"; return false; }
    size_t o = q + 2 - b;
    uint32_t n1, n2, usfld;
    auto trunc = [&]() { err = "truncated XT data"; return false; };
    if (o + 4 > len) return trunc();
    memcpy(&n1, b + o, 4);
    if (n1 > len - o - 4) return trunc();
    o += 4 + n1;
    if (o + 4 > len) return trunc();
    memcpy(&n2, b + o, 4);
    if (n2 > len - o - 4) return trunc();
    o += 4 + n2;
    if (o + 4 > len) return trunc();
    memcpy(&usfld, b + o, 4);
    o += 4;
    if (usfld > 1024) { err = "implausible user field count"; return false; }
    m.by_idx.assign(65537, Node());
    while (o + 2 <= len) {
        int type = b[o] | b[o + 1] << 8; o += 2;
        if (type == 1) return true;
        if (type <= 0 || type >= 400 || !s.nd[type].defined || s.nd[type].size < 0) {
            char e[96]; snprintf(e, sizeof e, "unsupported node type %d at 0x%zx", type, o - 2); err = e; return false;
        }
        const NodeDef &d = s.nd[type];
        uint32_t count = 0;
        if (d.var) { if (o + 4 > len) return trunc(); memcpy(&count, b + o, 4); o += 4; }
        if (o + 2 > len) return trunc();
        int idx = b[o] | b[o + 1] << 8; o += 2;
        size_t sz = d.size;
        if (d.var) {
            int last = -1;
            for (size_t i = 0; i < d.f.size(); i++) if (d.f[i].xmt) last = (int)i;
            if (last >= 0) { if (count > len) return trunc(); sz += (size_t)count * tsize(d.f[last].type); }
        }
        if (sz > len - o || 4 * (size_t)usfld > len - o - sz) return trunc();
        m.by_idx[idx].type = type; m.by_idx[idx].p = b + o; m.by_idx[idx].count = count;
        o += sz + 4 * (size_t)usfld;
    }
    err = "no end marker";
    return false;
}

/* ------------------------------------------------------------------------------------------- build */
namespace {

std::set<const Geom_Surface *> g_xtfix_surfs;   /* xtfix: surfaces of the new types (CONE ... BLENDED_EDGE) of the body being built */
bool g_timed_out = false;                        /* xtfix: Ctx::tick ran out of time */
bool g_prof = getenv("XTOCC_PROF") != nullptr;   /* xtfix2: profiling of Ctx::foot (testing) */
std::map<std::string, std::pair<long, double>> g_prof_t;

struct Ctx {
    const Model &m;
    double S, tol;
    Result &r;
    BRep_Builder B;
    std::map<int, TopoDS_Vertex> vtx;
    std::map<int, TopoDS_Edge> edge;
    std::map<int, Handle(Geom_Curve)> ecurve;   /* edge -> its 3D curve (effective sense applied) */
    std::map<int, std::pair<double, double>> erange;
    std::map<int, gp_Pnt> ring_at;              /* ring edge -> where its (OCCT-only) vertex goes: on the seam (ours) */
    Ctx(const Model &m_, double S_, Result &r_) : m(m_), S(S_), tol(0), r(r_) {}

    gp_Pnt pnt(const gp_XYZ &v) const { return gp_Pnt(v * S); }
    const Node *vertex_point(const Node *v) const { return v ? m.pnode(v, "point") : nullptr; }

    TopoDS_Vertex vertex(int idx) {
        auto it = vtx.find(idx);
        if (it != vtx.end()) return it->second;
        const Node *v = m.node(idx), *p = vertex_point(v);
        TopoDS_Vertex V;
        if (!p) return V;
        B.MakeVertex(V, pnt(m.vec(p, "pvec")), tol);
        vtx[idx] = V;
        return V;
    }

    /* ---- 3drough: NURBS / SP / trimmed / ellipse curves (xt_occt.h head; ours: the OCCT representation) */
    std::map<int, Handle(Geom_Surface)> *surfs = nullptr;       /* the face surfaces built so far (B_SURFACE for SP_CURVE) */
    std::map<const Geom_Curve *, std::pair<const Geom_Surface *, Handle(Geom2d_Curve)>> sp_pc;   /* SP_CURVE edge -> its pcurve */
    bool newtypes = false;                                       /* an edge used a new curve type */
    bool trim_unscaled = false;                                  /* xtfix: any_curve -> general_edge: the trim parameters are the built basis' own */
    std::map<int, double> etol;                                  /* 3drough: EDGE.tolerance x S of a tolerant edge (Parasolid; null = < 0) */
    bool nurbs_arrays(const Node *nc, int &deg, int &np, int &dim, bool &rat, std::vector<double> &v, std::vector<double> &kn, std::vector<int> &mu) {
        deg = m.sval(nc, "degree"); np = m.ival(nc, "n_vertices"); dim = m.sval(nc, "vertex_dim"); rat = m.chr(nc, "rational") != 0;
        int nk = m.ival(nc, "n_knots");
        const Node *bv = m.pnode(nc, "bspline_vertices"), *km = m.pnode(nc, "knot_mult"), *ks = m.pnode(nc, "knots");
        if (!bv || !km || !ks) return false;   /* xtfix2: periodic ones too (made periodic by the callers, checked) */
        if ((int)bv->count < np * dim || (int)km->count < nk || (int)ks->count < nk) return false;
        for (int i = 0; i < np * dim; i++) v.push_back(m.dbl(bv, "vertices", i));
        for (int i = 0; i < nk; i++) { kn.push_back(m.dbl(ks, "knots", i)); mu.push_back(m.sval(km, "mult", i)); }
        return true;
    }
    /* rational vertices: XT keeps them homogeneous (x w, y w, z w, w) - ours, checked by the gap metrics */
    Handle(Geom_BSplineCurve) bcurve3(const Node *bc) {
        const Node *nc = m.pnode(bc, "nurbs"); int deg, np, dim; bool rat; std::vector<double> v, kn; std::vector<int> mu;
        if (!nc || !nurbs_arrays(nc, deg, np, dim, rat, v, kn, mu) || dim != (rat ? 4 : 3)) return nullptr;
        TColgp_Array1OfPnt P(1, np); TColStd_Array1OfReal W(1, np), K(1, (int)kn.size()); TColStd_Array1OfInteger M(1, (int)mu.size());
        for (int i = 0; i < np; i++) { double w = rat ? v[dim * i + 3] : 1; P(i + 1) = gp_Pnt(gp_XYZ(v[dim * i], v[dim * i + 1], v[dim * i + 2]) * (S / w)); W(i + 1) = w; }
        for (size_t i = 0; i < kn.size(); i++) { K((int)i + 1) = kn[i]; M((int)i + 1) = mu[i]; }
        Handle(Geom_BSplineCurve) c;
        try { c = rat ? new Geom_BSplineCurve(P, W, K, M, deg) : new Geom_BSplineCurve(P, K, M, deg); } catch (...) { return nullptr; }
        if (m.chr(nc, "periodic") && !make_periodic(c)) return nullptr;
        return c;
    }
    /* xtfix2: periodic NURBS (ref p.35 "Periodic B-curves": over its valid range t_degree .. t_(n_knots-1-degree) the curve is the B-spline
       of its knots and vertices; periodic parametrisation only repeats it every period; p.37: the same for surfaces in u / v). OCCT needs
       its periodic form for edges / pcurves across the period end: Geom_BSpline*::SetPeriodic (drops the repeated end vertices). Accepted
       only when the periodic form equals the B-spline built from the data at 33 (x 33) points of the valid range within 1e-9 x S
       (rounding; ours), else refused (Result::unmapped "periodic B-curve / B-surface"). */
    template <class F> bool same_on_range(F f, double a, double b, double c0, double c1, bool two) {
        for (int i = 0; i <= 32; i++) for (int j = 0; j <= (two ? 32 : 0); j++)
            if (!(f(a + (b - a) * i / 32, c0 + (c1 - c0) * j / 32) <= 1e-9 * S)) return false;
        return true;
    }
    bool make_periodic(Handle(Geom_BSplineCurve) &c) {
        Handle(Geom_BSplineCurve) p = Handle(Geom_BSplineCurve)::DownCast(c->Copy());
        try { p->SetPeriodic(); } catch (...) { return false; }
        double a = c->FirstParameter(), b = c->LastParameter();
        if (std::abs(p->Period() - (b - a)) > 1e-12 * std::max(1.0, b - a) ||
            !same_on_range([&](double t, double) { return p->Value(t).Distance(c->Value(t)); }, a, b, 0, 0, false)) return false;
        c = p; return true;
    }
    bool make_periodic(Handle(Geom_BSplineSurface) &s, bool up, bool vp) {
        Handle(Geom_BSplineSurface) p = Handle(Geom_BSplineSurface)::DownCast(s->Copy());
        try { if (up) p->SetUPeriodic(); if (vp) p->SetVPeriodic(); } catch (...) { return false; }
        double u0, u1, v0, v1; s->Bounds(u0, u1, v0, v1);
        if (!same_on_range([&](double u, double v) { return p->Value(u, v).Distance(s->Value(u, v)); }, u0, u1, v0, v1, true)) return false;
        s = p; return true;
    }
    Handle(Geom_BSplineSurface) bsurface(const Node *bs) {
        const Node *ns = m.pnode(bs, "nurbs");
        if (!ns) return nullptr;
        int ud = m.sval(ns, "u_degree"), vd = m.sval(ns, "v_degree"), nu = m.ival(ns, "n_u_vertices"), nv = m.ival(ns, "n_v_vertices");
        int nku = m.ival(ns, "n_u_knots"), nkv = m.ival(ns, "n_v_knots"), dim = m.sval(ns, "vertex_dim"); bool rat = m.chr(ns, "rational") != 0;
        const Node *bv = m.pnode(ns, "bspline_vertices"), *mu = m.pnode(ns, "u_knot_mult"), *mv = m.pnode(ns, "v_knot_mult"), *ku = m.pnode(ns, "u_knots"), *kv = m.pnode(ns, "v_knots");
        if (!bv || !mu || !mv || !ku || !kv || dim != (rat ? 4 : 3) || (int)bv->count < nu * nv * dim) return nullptr;
        TColgp_Array2OfPnt P(1, nu, 1, nv); TColStd_Array2OfReal W(1, nu, 1, nv);
        TColStd_Array1OfReal KU(1, nku), KV(1, nkv); TColStd_Array1OfInteger MU(1, nku), MV(1, nkv);
        bool vfast = !getenv("XTOCC_NURBS_UFAST");   /* vertex order: v varies fastest (ours, the gap metrics decide; XTOCC_NURBS_UFAST=1 = the other) */
        for (int i = 0; i < nu; i++) for (int j = 0; j < nv; j++) {
            int k = vfast ? i * nv + j : j * nu + i;
            double w = rat ? m.dbl(bv, "vertices", dim * k + 3) : 1;
            P(i + 1, j + 1) = gp_Pnt(gp_XYZ(m.dbl(bv, "vertices", dim * k), m.dbl(bv, "vertices", dim * k + 1), m.dbl(bv, "vertices", dim * k + 2)) * (S / w));
            W(i + 1, j + 1) = w;
        }
        for (int i = 0; i < nku; i++) { KU(i + 1) = m.dbl(ku, "knots", i); MU(i + 1) = m.sval(mu, "mult", i); }
        for (int i = 0; i < nkv; i++) { KV(i + 1) = m.dbl(kv, "knots", i); MV(i + 1) = m.sval(mv, "mult", i); }
        Handle(Geom_BSplineSurface) g;
        try { g = rat ? new Geom_BSplineSurface(P, W, KU, KV, MU, MV, ud, vd) : new Geom_BSplineSurface(P, KU, KV, MU, MV, ud, vd); } catch (...) { return nullptr; }
        bool up = m.chr(ns, "u_periodic") != 0, vp = m.chr(ns, "v_periodic") != 0;
        if ((up || vp) && !make_periodic(g, up, vp)) return nullptr;   /* xtfix2 */
        return g;
    }
    Handle(Geom2d_BSplineCurve) bcurve2(const Node *bc) {
        const Node *nc = m.pnode(bc, "nurbs"); int deg, np, dim; bool rat; std::vector<double> v, kn; std::vector<int> mu;
        if (!nc || !nurbs_arrays(nc, deg, np, dim, rat, v, kn, mu) || dim != (rat ? 3 : 2)) return nullptr;
        TColgp_Array1OfPnt2d P(1, np); TColStd_Array1OfReal W(1, np), K(1, (int)kn.size()); TColStd_Array1OfInteger M(1, (int)mu.size());
        for (int i = 0; i < np; i++) { double w = rat ? v[dim * i + 2] : 1; P(i + 1) = gp_Pnt2d(v[dim * i] / w, v[dim * i + 1] / w); W(i + 1) = w; }
        for (size_t i = 0; i < kn.size(); i++) { K((int)i + 1) = kn[i]; M((int)i + 1) = mu[i]; }
        Handle(Geom2d_BSplineCurve) c;
        try { c = rat ? new Geom2d_BSplineCurve(P, W, K, M, deg) : new Geom2d_BSplineCurve(P, K, M, deg); } catch (...) { return nullptr; }
        if (m.chr(nc, "periodic")) {   /* xtfix2: periodic (ref p.35), as make_periodic: kept only when equal on the valid range */
            Handle(Geom2d_BSplineCurve) p = Handle(Geom2d_BSplineCurve)::DownCast(c->Copy());
            try { p->SetPeriodic(); } catch (...) { return nullptr; }
            double a = c->FirstParameter(), b = c->LastParameter();
            if (!same_on_range([&](double t, double) { return p->Value(t).Distance(c->Value(t)); }, a, b, 0, 0, false)) return nullptr;
            c = p;
        }
        return c;
    }
    /* XT surface parameters (u, v) -> the OCCT surface's: u' = su u, v' = sv v, then for a SPUN_SURF (u', v') -> (-v', u') (the
       revolution, xtfix2). The model is scaled by S: lengths in the parametrisation scale - plane u, v (ref p.52), cylinder v (p.53), cone
       v / cos a (p.55, xt_refgeom.h), a swept surface's v (p.71) and a LINE section / profile's parameter (its length); angles, B-surface
       and B-curve parameters do not. OFFSET_SURF: its surface's parametrisation (ref p.63). false: not reproduced (a blend's fit) */
    bool uv_map(const Node *sn, double &su, double &sv, bool &spun) {
        su = sv = 1; spun = false;
        while (m.is(sn, "OFFSET_SURF")) sn = m.pnode(sn, "surface");
        auto cs = [&](const Node *cv) { return m.is(cv, "LINE") ? S : 1.0; };
        if (m.is(sn, "PLANE")) su = sv = S;
        else if (m.is(sn, "CYLINDER")) sv = S;
        else if (m.is(sn, "CONE")) sv = S / m.dbl(sn, "cos_half_angle");
        else if (m.is(sn, "SWEPT_SURF")) { su = cs(m.pnode(sn, "section")); sv = S; }
        else if (m.is(sn, "SPUN_SURF")) { su = cs(m.pnode(sn, "profile")); spun = true; }
        else if (m.is(sn, "BLENDED_EDGE") || !sn) return false;
        return true;
    }
    /* an SP_CURVE's 2D B-spline in the OCCT surface's parameters (its own parameter) + the built surface */
    bool sp_2d(const Node *c, Handle(Geom2d_BSplineCurve) &b2, Handle(Geom_Surface) &ps) {
        int si = m.ptr(c, "surface"); const Node *bc = m.pnode(c, "b_curve");
        if (surfs && !surfs->count(si) && m.node(si)) surface(si);   /* xtfix: the surface on demand */
        if (!surfs || !surfs->count(si) || !bc) return false;
        b2 = bcurve2(bc); if (b2.IsNull()) return false;
        ps = (*surfs)[si];
        double su, sv; bool spun;
        if (!uv_map(m.node(si), su, sv, spun)) return false;
        if (su != 1 || sv != 1 || spun) for (int i = 1; i <= b2->NbPoles(); i++) {
            gp_Pnt2d q = b2->Pole(i); q.SetCoord(q.X() * su, q.Y() * sv);
            if (spun) q.SetCoord(-q.Y(), q.X());
            b2->SetPole(i, q);
        }
        return true;
    }
    /* xtfix2: a tolerant edge's other fins (ref p.21: "every fin of that edge will reference a trimmed SP-curve. The underlying surface
       of the SP-curve must be the same as that of the corresponding face. The curve must not deviate by more than the edge tolerance
       from curves on other fins of the edge"): each face takes its own fin's SP curve as the pcurve (before: the edge's one fin curve
       projected on the other face, whose projection error pushed the gap past the tolerance). The 2D curve is trimmed to the fin's
       TRIMMED_CURVE interval, oriented along the edge curve and its parameter mapped linearly onto the edge's [t0, t1]
       (BRepLib::SameParameter makes it exact in parameter; the geometry is the stored one) */
    std::map<std::pair<const Geom_Curve *, const Geom_Surface *>, Handle(Geom2d_Curve)> fin_pc;
    std::map<std::pair<const Geom_Curve *, const Geom_Surface *>, double> fin_vt;   /* the larger tolerance of the edge's vertices */
    void fin_pcurves(const Node *e, const Node *used, const Handle(Geom_Curve) &gc, double t0, double t1, int v0, int v1) {
        const Node *h0 = m.pnode(e, "halfedge"), *h = h0; int g = 0;
        do {
            const Node *fc = m.pnode(h, "curve");
            if (fc && fc != used) {
                const Node *sp = m.is(fc, "TRIMMED_CURVE") ? m.pnode(fc, "basis_curve") : fc;
                Handle(Geom2d_BSplineCurve) b2; Handle(Geom_Surface) ps;
                if (m.is(sp, "SP_CURVE") && sp_2d(sp, b2, ps)) {
                    double a = b2->FirstParameter(), b = b2->LastParameter();
                    if (m.is(fc, "TRIMMED_CURVE")) { double p1 = m.dbl(fc, "parm_1"), p2 = m.dbl(fc, "parm_2"); a = std::min(p1, p2); b = std::max(p1, p2); }
                    try {
                        if (b2->IsPeriodic()) b2->Segment(a, b);   /* the trim interval as given (ref p.46: up to a period, either way) */
                        else if (b2->Degree() == 1 && b2->NbPoles() == 2 && (a < b2->FirstParameter() - 1e-9 || b > b2->LastParameter() + 1e-9)) {
                            /* a straight 2D segment trimmed beyond its range (as any_curve): the line itself */
                            TColgp_Array1OfPnt2d P(1, 2); P(1) = b2->Value(a); P(2) = b2->Value(b);
                            TColStd_Array1OfReal K(1, 2); K(1) = a; K(2) = b; TColStd_Array1OfInteger M(1, 2); M(1) = M(2) = 2;
                            b2 = new Geom2d_BSplineCurve(P, K, M, 1);
                        } else if (a > b2->FirstParameter() + 1e-12 || b < b2->LastParameter() - 1e-12) b2->Segment(std::max(a, b2->FirstParameter()), std::min(b, b2->LastParameter()));
                        auto at = [&](double t) { gp_Pnt2d q = b2->Value(t); return ps->Value(q.X(), q.Y()); };
                        /* orientation along the edge curve: ends and quarter points (a closed curve's ends coincide) */
                        double f0 = b2->FirstParameter(), f1 = b2->LastParameter(), fwd = 0, rev = 0;
                        for (double w : { 0.0, 0.25, 0.75, 1.0 }) {
                            gp_Pnt e = gc->Value(t0 + (t1 - t0) * w);
                            fwd += at(f0 + (f1 - f0) * w).Distance(e); rev += at(f1 - (f1 - f0) * w).Distance(e);
                        }
                        if (rev < fwd) b2->Reverse();
                        /* ref p.21: the fin curve's "ends must be within vertex tolerance of the corresponding vertices" - checked (a wrong trim /
                           orientation / parameter mapping fails it; the curve is then not used and the face falls back to the projection) */
                        auto vtol = [&](int vi) { double t = m.dbl(m.node(vi), "tolerance"); return (t > 0 ? t * S : 0.5 * tol) + tol; };
                        double d0 = v0 > 1 ? at(b2->FirstParameter()).Distance(BRep_Tool::Pnt(vertex(v0))) : 0, d1 = v1 > 1 ? at(b2->LastParameter()).Distance(BRep_Tool::Pnt(vertex(v1))) : 0;
                        if (getenv("XTOCC_DEBUG")) fprintf(stderr, "fin curve of edge %d: ends %g %g from its vertices (tolerances %g %g)\n", idx_of(e), d0, d1, v0 > 1 ? vtol(v0) : 0, v1 > 1 ? vtol(v1) : 0);
                        dev("tolerant edges: fin curve end to its vertex / vertex tolerance (ratio)", std::max(v0 > 1 ? d0 / vtol(v0) : 0, v1 > 1 ? d1 / vtol(v1) : 0));
                        if (!((v0 > 1 && d0 > vtol(v0)) || (v1 > 1 && d1 > vtol(v1)))) {
                            TColStd_Array1OfReal K(1, b2->NbKnots()); b2->Knots(K);
                            double k0 = b2->FirstParameter(), k1 = b2->LastParameter();
                            for (int i = K.Lower(); i <= K.Upper(); i++) K(i) = t0 + (K(i) - k0) / (k1 - k0) * (t1 - t0);
                            b2->SetKnots(K);
                            fin_pc[{ gc.get(), ps.get() }] = b2;
                            fin_vt[{ gc.get(), ps.get() }] = std::max(v0 > 1 ? vtol(v0) : 0, v1 > 1 ? vtol(v1) : 0);
                        }
                    } catch (...) {}
                }
            }
            h = m.pnode(h, "other");
        } while (h && h != h0 && ++g < 64);
    }
    /* a curve node -> 3D curve (+ pcurve / surface for an SP_CURVE); trimmed: its parameter interval */
    bool any_curve(const Node *c, Handle(Geom_Curve) &gc, Handle(Geom2d_Curve) &pc, Handle(Geom_Surface) &ps, bool &trim, double &p1, double &p2, bool sensed = true) {
        /* a TRIMMED_CURVE's parm_1 / parm_2 are parameters of its basis as defined, so the basis is built without its own sense
           (sensed = false); the direction comes from the fin's vertices (general_edge) */
        if (m.is(c, "TRIMMED_CURVE")) {
            const Node *b = m.pnode(c, "basis_curve"); bool t2;
            Handle(Geom2d_BSplineCurve) b2p; Handle(Geom_Surface) psp;
            bool sp_ok = m.is(b, "SP_CURVE") && sp_2d(b, b2p, psp);
            if (sp_ok && !b2p->IsPeriodic() && b2p->Degree() == 1 && b2p->NbPoles() == 2 && (psp->IsUPeriodic() || psp->IsVPeriodic())) {
                /* xtfix2: a straight 2D segment trimmed beyond its range on a periodic surface (PORT 5 AXIS.mcam edge 10: 0 .. -1, point_1 =
                   point_2 - one period round the surface): the line itself, exact (degree 1); point_1 / point_2 are checked below */
                double q1 = m.dbl(c, "parm_1"), q2 = m.dbl(c, "parm_2"), lo = std::min(q1, q2), hi = std::max(q1, q2);
                if (lo < b2p->FirstParameter() - 1e-9 || hi > b2p->LastParameter() + 1e-9) {
                    TColgp_Array1OfPnt2d P(1, 2); P(1) = b2p->Value(lo); P(2) = b2p->Value(hi);   /* OCCT extends a degree-1 span linearly */
                    TColStd_Array1OfReal K(1, 2); K(1) = lo; K(2) = hi; TColStd_Array1OfInteger M(1, 2); M(1) = M(2) = 2;
                    b2p = new Geom2d_BSplineCurve(P, K, M, 1);
                    p1 = q1; p2 = q2;
                    TopoDS_Edge E = BRepBuilderAPI_MakeEdge(Handle(Geom2d_Curve)(b2p), psp, lo, hi);
                    if (E.IsNull() || !BRepLib::BuildCurve3d(E, tol)) return false;
                    TopLoc_Location L; double f, l; gc = BRep_Tool::Curve(E, L, f, l);
                    if (gc.IsNull()) return false;
                    double d = std::max(gc->Value(q1).Distance(pnt(m.vec(c, "point_1"))), gc->Value(q2).Distance(pnt(m.vec(c, "point_2"))));
                    dev("TRIMMED_CURVE on SP_CURVE beyond its range: point_1 / point_2 to the curve", d);
                    if (d > std::max(tol, 1e-6 * S)) return false;
                    pc = b2p; ps = psp; trim = true;
                    return true;
                }
            }
            if (sp_ok && b2p->IsPeriodic()) {   /* xtfix2: on a periodic 2D basis (bcurve2) the 3D curve over the
                    trim interval itself (ref p.46: parm_1 in the base range, parm_2 up to a period away, below parm_1 for a '-' basis - e.g.
                    PORT 5 AXIS.mcam: 0 .. -1, the whole curve); open bases: as before */
                p1 = m.dbl(c, "parm_1"); p2 = m.dbl(c, "parm_2");
                double lo = std::min(p1, p2), hi = std::max(p1, p2);
                if (!(hi > lo)) return false;
                TopoDS_Edge E = BRepBuilderAPI_MakeEdge(Handle(Geom2d_Curve)(b2p), psp, lo, hi);
                if (E.IsNull() || !BRepLib::BuildCurve3d(E, tol)) return false;
                TopLoc_Location L; double f, l; gc = BRep_Tool::Curve(E, L, f, l);
                if (gc.IsNull()) return false;
                pc = b2p; ps = psp; trim = true;
                return true;
            }
            if (!b || !any_curve(b, gc, pc, ps, t2, p1, p2, false)) return false;
            trim = true; p1 = m.dbl(c, "parm_1"); p2 = m.dbl(c, "parm_2");
            if (m.is(b, "INTERSECTION")) {   /* xtfix: parm_1 / parm_2 are chart parameters; point_1 / point_2 lie on the basis (ref p.46) */
                GeomAPI_ProjectPointOnCurve q1(pnt(m.vec(c, "point_1")), gc), q2(pnt(m.vec(c, "point_2")), gc);
                if (!q1.NbPoints() || !q2.NbPoints()) return false;
                p1 = q1.LowerDistanceParameter(); p2 = q2.LowerDistanceParameter();
                dev("TRIMMED_CURVE on INTERSECTION: point_1 / point_2 to the basis", std::max(q1.LowerDistance(), q2.LowerDistance()));
                trim_unscaled = true;
            }
            return true;
        }
        if (m.is(c, "LINE")) { gp_Dir d(m.vec(c, "direction")); if (sensed && m.chr(c, "sense") == '-') d.Reverse(); gc = new Geom_Line(gp_Lin(pnt(m.vec(c, "pvec")), d)); return true; }
        if (m.is(c, "CIRCLE")) { gp_Dir n(m.vec(c, "normal")), x(m.vec(c, "x_axis")); if (sensed && m.chr(c, "sense") == '-') n.Reverse();
            gc = new Geom_Circle(gp_Circ(gp_Ax2(pnt(m.vec(c, "centre")), n, x), m.dbl(c, "radius") * S)); return true; }
        if (m.is(c, "ELLIPSE")) { gp_Dir n(m.vec(c, "normal")), x(m.vec(c, "x_axis")); if (sensed && m.chr(c, "sense") == '-') n.Reverse();
            double a = m.dbl(c, "major_radius") * S, b = m.dbl(c, "minor_radius") * S; if (a < b) return false;
            gc = new Geom_Ellipse(gp_Elips(gp_Ax2(pnt(m.vec(c, "centre")), n, x), a, b)); return true; }
        if (m.is(c, "B_CURVE")) { Handle(Geom_BSplineCurve) b = bcurve3(c); if (b.IsNull()) return false; if (sensed && m.chr(c, "sense") == '-') b->Reverse(); gc = b; return true; }
        if (m.is(c, "INTERSECTION")) {   /* xtfix: icurve (chart order = XT parameter order, ref p.42) */
            Handle(Geom_Curve) ic = icurve(idx_of(c));
            if (ic.IsNull()) return false;
            gc = (sensed && m.chr(c, "sense") == '-') ? ic->Reversed() : ic;
            return true;
        }
        if (m.is(c, "SP_CURVE")) {
            Handle(Geom2d_BSplineCurve) b2;
            if (!sp_2d(c, b2, ps)) return false;
            double a = b2->FirstParameter(), z = b2->LastParameter();
            TopoDS_Edge E = BRepBuilderAPI_MakeEdge(b2, ps, a, z);
            if (E.IsNull() || !BRepLib::BuildCurve3d(E, tol)) return false;
            TopLoc_Location L; double f, l; gc = BRep_Tool::Curve(E, L, f, l);
            if (gc.IsNull()) return false;
            pc = b2;
            if (sensed && m.chr(c, "sense") == '-') { gc = gc->Reversed(); pc = pc->Reversed(); }
            return true;
        }
        return false;
    }
    /* ============================================================================ xtfix (2026-10-26, formats/xt_occt_fixes.md)
       New geometry, every rule from the Parasolid XT Format Reference (UGS, Oct 2006; "ref p.N" = its page numbers;
       ~/mastercam-re/work/occt/ref/) or measured on the stored data (labelled):
         CONE / SPHERE / TORUS: the OCCT analytic surface with the same parametrisation as the XT formula (xt_refgeom.h; the cone's
           "+ v A" sign is the measured one recorded there), so u, v and the natural normal are XT's - exact;
         OFFSET_SURF (ref p.63): Geom_OffsetSurface of the underlying surface (the same parametrisation);
         BLENDED_EDGE (rolling ball, ref p.59-62): the exact definition evaluated pointwise - the ball centre on the spine C(u), the
           boundary on support k = the support's point nearest C(u) (the spine is the supports offset by range[k], ref p.61; for a
           cliff edge the zero-range support's spine curve), the section = the arc of radius |range| about C(u) from the boundary
           on surface[0] to the one on surface[1] (swapped when the spine's sense is '-'), angle proportional to v. OCCT cannot
           represent it exactly: a cubic B-spline interpolating the exact points, refined until the exact points between the
           samples lie within fit_tol (1e-7 m) of it - checked, the deviation in Result::accuracy;
         INTERSECTION (ref p.42-46): the curve is the exact intersection of its two surfaces; its stored chart points lie on it
           (the chart is only a chordal approximation, chordal_error up to 0.3 mm in RASTER.mcam). Ours: a C2 B-spline interpolating
           the stored points plus points computed on the exact intersection (Newton on the two tangent planes; for a BLEND_BOUND
           side, ref p.62-63: the blend's boundary on blend.surface[1 - boundary] = its support foot above the spine point), refined
           until every interval's midpoint is within fit_tol of the exact intersection. 'T' limits: the singular point (hvec[0])
           ends the curve; 'H' limits: the curve is closed (periodic B-spline);
         TRIMMED_CURVE on an INTERSECTION basis: parm_1 / parm_2 are XT chart parameters, so the trim uses point_1 / point_2, which
           "lie exactly on the basis curve" (ref p.46), projected on the built basis.
       Anything else stays refused (Result::unmapped). */
    std::map<int, std::vector<int>> faces_of_surf;     /* surface node -> its FACE nodes */
    std::map<int, std::vector<gp_Pnt>> demand;         /* blend surface node -> stored points it must cover (model units) */
    std::set<int> ring_surf;                           /* surfaces of faces with a vertexless fin (a ring loop: the whole period) */
    std::map<int, Handle(Geom_Curve)> icurves;         /* INTERSECTION node -> its curve, parameter increasing along the chart */
    std::set<const Geom_Surface *> &xtfix_surfs = g_xtfix_surfs;
    std::map<int, double> spine_ext;                   /* INTERSECTION node that is a blend's spine -> the blend's radius */
    std::set<int> busy, failed;                        /* recursion guard; nodes that could not be built */
    std::map<const Geom_Surface *, Handle(ShapeAnalysis_Surface)> sas;
    std::map<const Geom_Surface *, gp_Pnt2d> last_uv;
    double fit_tol = 0;                                /* 1e-7 m x S (ours): the B-spline fits' target tolerance */
    double accept_tol = 0;                             /* 1e-5 m x S (ours): a fit that cannot get within it is refused */
    double max_fit = 0;                                /* the largest accepted fit deviation (the edges' tolerance grows by 2x it) */
    /* time budget (ours): the fits of a big body can run for minutes (impeller blades: blends on B-surfaces); past XTOCC_TIME_LIMIT
       seconds (default 120; RASTER.mcam takes 31 s) the body is refused instead of hanging the app */
    std::chrono::steady_clock::time_point t_start = std::chrono::steady_clock::now();
    double time_limit = getenv("XTOCC_TIME_LIMIT") ? atof(getenv("XTOCC_TIME_LIMIT")) : 120;
    struct TimeLimit {};
    void tick() { if (g_timed_out || std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count() > time_limit) { g_timed_out = true; throw TimeLimit(); } }
    void dev(const std::string &k, double d) { auto &x = r.accuracy[k]; x.n++; if (d > x.max) x.max = d; }
    void refuse(int idx, const std::string &why) {
        if (failed.insert(idx).second) r.unmapped.push_back(std::string(m.tname(m.node(idx))) + " " + std::to_string(idx) + ": " + why);
    }
    int idx_of(const Node *n) const { return (int)(n - &m.by_idx[0]); }
    gp_Pnt hvec(const Node *n, int k) const { return gp_Pnt(gp_XYZ(m.dbl(n, "hvec", 3 * k), m.dbl(n, "hvec", 3 * k + 1), m.dbl(n, "hvec", 3 * k + 2)) * S); }

    /* the point of s nearest p and the unit natural normal there (OCCT's projection) */
    const char *ftag = "";   /* profiling: the caller */
    bool foot(const Handle(Geom_Surface) &s, const gp_Pnt &p, gp_Pnt &f, gp_Vec &n, gp_Pnt2d *uvo = nullptr, double local_max = -1) {
        Handle(ShapeAnalysis_Surface) &a = sas[s.get()];
        if (a.IsNull()) a = new ShapeAnalysis_Surface(s);
        /* speed (ours): from the previous foot on this surface first (consecutive queries are neighbours), kept only when it is a true
           foot (p - f along the normal); else OCCT's global search.
           xtfix2 (speed, ours): on an OFFSET_SURF O = S + d n the feet are found on its basis S first - O and S have the same tangent plane
           at the same (u, v), so p - O(u, v) is normal to O exactly where p - S(u, v) is normal to S; the (u, v) is kept only when it
           passes the same foot test on O (and the caller's bound). ShapeAnalysis on an offset of a B-surface costs ~1 ms a call, on the
           B-surface ~100x less (IMPELLER WITH TILTED CURVE: 60 s of 60 s in offset projections) */
        auto lu = last_uv.find(s.get());
        Handle(Geom_OffsetSurface) os = Handle(Geom_OffsetSurface)::DownCast(s);
        Handle(ShapeAnalysis_Surface) ab;
        if (!os.IsNull() && local_max > 0 && !getenv("XTOCC_NO_BASIS_FEET")) { Handle(ShapeAnalysis_Surface) &x = sas[os->BasisSurface().get()]; if (x.IsNull()) x = new ShapeAnalysis_Surface(os->BasisSurface()); ab = x; }
        bool seeded = local_max > 0 && lu != last_uv.end() && !s->IsKind(STANDARD_TYPE(Geom_ElementarySurface));
        /* passes: 0 local on s, 2 local on the basis, 3 global on the basis (verified, bounded callers only), 1 global on s */
        std::vector<int> passes;
        if (!ab.IsNull()) { if (lu != last_uv.end()) passes.push_back(2); passes.push_back(3); }
        else if (seeded) passes.push_back(0);
        passes.push_back(1);
        for (int pass : passes) {
            auto tq0 = std::chrono::steady_clock::now();
            gp_Pnt2d uv = pass == 1 ? a->ValueOfUV(p, 1e-4 * fit_tol) : pass == 0 ? a->NextValueOfUV(lu->second, p, 1e-4 * fit_tol, -1)
                        : pass == 2 ? ab->NextValueOfUV(lu->second, p, 1e-4 * fit_tol, -1) : ab->ValueOfUV(p, 1e-4 * fit_tol);
            if (g_prof) { auto &x = g_prof_t[std::string(ftag) + " " + std::string(pass == 1 ? "global " : pass == 0 ? "local " : pass == 2 ? "basis local " : "basis global ") + s->DynamicType()->Name() + (local_max > 0 ? " bounded" : "") + (lu != last_uv.end() ? " seeded" : "")];
                x.first++; x.second += std::chrono::duration<double>(std::chrono::steady_clock::now() - tq0).count(); }
            gp_Vec du, dv; s->D1(uv.X(), uv.Y(), f, du, dv);
            n = du.Crossed(dv); double l = n.Magnitude();
            if (!(l > 1e-300)) { if (pass == 1) return false; continue; }
            n /= l;
            gp_Vec w(f, p);
            if (pass == 3 && (w - n * w.Dot(n)).Magnitude() > 1e-3 * fit_tol + 1e-6 * w.Magnitude()) {
                /* the basis' nearest point is not a foot: on the basis' parameter boundary, p is beyond the bounded surface (no interior foot
                   is nearer on the basis); the offset's nearest point is then its boundary too - this point is the answer (as pass 1 would
                   give a boundary point; the callers treat it as "not evaluable" by its residual). Else the global search on the offset. */
                double U1, U2, V1, V2; os->BasisSurface()->Bounds(U1, U2, V1, V2);
                double eu = 1e-9 * std::max(1.0, U2 - U1), ev = 1e-9 * std::max(1.0, V2 - V1);
                bool onb = (!os->BasisSurface()->IsUPeriodic() && (uv.X() <= U1 + eu || uv.X() >= U2 - eu)) || (!os->BasisSurface()->IsVPeriodic() && (uv.Y() <= V1 + ev || uv.Y() >= V2 - ev));
                if (!onb || getenv("XTOCC_NO_BASIS_BOUNDARY")) continue;
                if (uvo) *uvo = uv;
                last_uv[s.get()] = uv;
                return true;
            }
            if (pass != 1 && ((w - n * w.Dot(n)).Magnitude() > 1e-3 * fit_tol + 1e-6 * w.Magnitude() || (pass != 3 && w.Magnitude() > local_max))) continue;   /* not a
                foot, or farther than the caller's bound (another branch); the basis' global search (pass 3) stands for the global one on the
                offset (pass 1, which does not use the bound either): its nearest foot, provided it is a true foot of the offset */
            if (uvo) *uvo = uv;
            last_uv[s.get()] = uv;
            return true;
        }
        return false;
    }

    /* ---- surfaces (built on demand: a blend needs its supports, an offset its basis) */
    Handle(Geom_Surface) surface(int si) {
        auto it = surfs->find(si);
        if (it != surfs->end()) return it->second;
        if (failed.count(si)) return nullptr;
        if (busy.count(si)) { refuse(si, "cyclic surface definition"); return nullptr; }
        const Node *sn = m.node(si);
        if (!sn) return nullptr;
        busy.insert(si);
        Handle(Geom_Surface) g;
        double dcos = 1;                              /* cone: OCCT v = XT v / cos(half angle) */
        if (m.is(sn, "PLANE")) {
            g = new Geom_Plane(gp_Ax3(pnt(m.vec(sn, "pvec")), gp_Dir(m.vec(sn, "normal")), gp_Dir(m.vec(sn, "x_axis"))));
        } else if (m.is(sn, "CYLINDER")) {
            g = new Geom_CylindricalSurface(gp_Ax3(pnt(m.vec(sn, "pvec")), gp_Dir(m.vec(sn, "axis")), gp_Dir(m.vec(sn, "x_axis"))),
                                            m.dbl(sn, "radius") * S
#ifdef XTOCC_MUTATE_RADIUS          /* test hook (ours): +0.002 mm on every cylinder */
                                            + 0.002
#endif
                                            );
        } else if (m.is(sn, "B_SURFACE")) {   /* 3drough: NURBS_SURF (xt_occt.h head) */
            Handle(Geom_BSplineSurface) bs = bsurface(sn);
            if (bs.IsNull()) { busy.erase(si); failed.insert(si); r.unmapped.push_back(std::string("surface B_SURFACE (periodic / unexpected layout)")); return nullptr; }
            g = bs; newsurf = true;
        } else if (m.is(sn, "CONE")) {        /* xtfix: R = P + v A + (X cos u + Y sin u)(r + v tan a), Y = A x X (xt_refgeom.h) */
            double sa = m.dbl(sn, "sin_half_angle"), ca = m.dbl(sn, "cos_half_angle"), a = atan2(sa, ca);
            if (!(ca > 0) || std::abs(a) < 1e-12 || std::abs(a) > M_PI / 2 - 1e-12 || m.dbl(sn, "radius") < 0) { busy.erase(si); refuse(si, "cone half angle / radius outside OCCT's range"); return nullptr; }
            g = new Geom_ConicalSurface(gp_Ax3(pnt(m.vec(sn, "pvec")), gp_Dir(m.vec(sn, "axis")), gp_Dir(m.vec(sn, "x_axis"))), a, m.dbl(sn, "radius") * S);
            dcos = ca; newsurf = true;
        } else if (m.is(sn, "SPHERE")) {      /* ref p.56-57 */
            g = new Geom_SphericalSurface(gp_Ax3(pnt(m.vec(sn, "centre")), gp_Dir(m.vec(sn, "axis")), gp_Dir(m.vec(sn, "x_axis"))), m.dbl(sn, "radius") * S);
            newsurf = true;
        } else if (m.is(sn, "TORUS")) {       /* ref p.58-59; a lemon (major radius < 0) has no OCCT form */
            double a = m.dbl(sn, "major_radius"), b = m.dbl(sn, "minor_radius");
            if (b <= 0 || (a < 0 && -a >= b)) { busy.erase(si); refuse(si, "torus radii outside ref p.58 (minor <= 0, or a lemon with |major| >= minor)"); return nullptr; }
            if (a >= 0) g = new Geom_ToroidalSurface(gp_Ax3(pnt(m.vec(sn, "centre")), gp_Dir(m.vec(sn, "axis")), gp_Dir(m.vec(sn, "x_axis"))), a * S, b * S);
            else {   /* xtfix2: a lemon (ref p.58: a < 0, |a| < b) - OCCT's torus needs a >= 0. The same formula R(u, v) = C + (X cos u + Y sin u)
                        (a + b cos v) + b A sin v is the circle Q(v) = C + a X + b (X cos v + A sin v) (centre C + a X, x direction X, y direction A,
                        normal X x A = -Y) turned by u about the axis (C, A): Geom_SurfaceOfRevolution, OCCT U = rotation angle = XT u, V = the
                        circle's parameter = XT v - the same parametrisation and natural normal, exact (checked against the formula below) */
                gp_Dir A(m.vec(sn, "axis")), X(m.vec(sn, "x_axis")); gp_Pnt C = pnt(m.vec(sn, "centre"));
                Handle(Geom_Circle) q = new Geom_Circle(gp_Ax2(C.Translated(gp_Vec(X) * (a * S)), X.Crossed(A), X), b * S);
                g = new Geom_SurfaceOfRevolution(q, gp_Ax1(C, A));
            }
            newsurf = true;
        } else if (m.is(sn, "SWEPT_SURF") || m.is(sn, "SPUN_SURF")) {
            /* xtfix2: SWEPT_SURF (ref p.71) R(u, v) = C(u) + v D -> Geom_SurfaceOfLinearExtrusion(C, D): Value(u, v) = C(u) + v D, the same
               parametrisation (v x S: lengths) and natural normal C' x D. SPUN_SURF (ref p.72-73) R(u, v) = Z(u) + (C(u) - Z(u)) cos v +
               A x (C(u) - Z(u)) sin v, i.e. C(u) turned by v about the axis (base, A) -> Geom_SurfaceOfRevolution(C, axis (base, -A)): OCCT
               (U, V) = (-v, u) (OCCT's U is the angle, V the curve's parameter; turning by U about -A = by -U about A), so the natural normal
               dR/dU x dR/dV = -R_v x R_u = R_u x R_v is XT's. C = the section / profile curve as defined (ref: never an intersection or
               trimmed curve), built without its sense. Checked against the formula below; the stored edges on them by the edge checks */
            bool spun = m.is(sn, "SPUN_SURF");
            const Node *cv = m.pnode(sn, spun ? "profile" : "section");
            Handle(Geom_Curve) gc; Handle(Geom2d_Curve) pc; Handle(Geom_Surface) ps; bool t = false; double p1, p2;
            bool ok = false;
            try { ok = cv && !m.is(cv, "INTERSECTION") && !m.is(cv, "TRIMMED_CURVE") && any_curve(cv, gc, pc, ps, t, p1, p2, false) && !gc.IsNull() && !t; } catch (...) { ok = false; }
            if (!ok) { busy.erase(si); refuse(si, std::string(spun ? "profile " : "section ") + m.tname(cv) + " not built"); return nullptr; }
            try {
                if (spun) g = new Geom_SurfaceOfRevolution(gc, gp_Ax1(pnt(m.vec(sn, "base")), gp_Dir(m.vec(sn, "axis")).Reversed()));
                else g = new Geom_SurfaceOfLinearExtrusion(gc, gp_Dir(m.vec(sn, "sweep")));
            } catch (...) { g.Nullify(); }
            if (g.IsNull()) { busy.erase(si); refuse(si, "OCCT surface not built"); return nullptr; }
            if (r.check) {   /* the OCCT surface against the ref formula, at the profile's own parameters */
                double f = gc->FirstParameter(), l = gc->LastParameter();
                if (Precision::IsInfinite(f) || Precision::IsInfinite(l)) { f = -0.01 * S; l = 0.01 * S; }
                gp_XYZ A(m.vec(sn, spun ? "axis" : "sweep")), P = m.vec(sn, "base") * S;
                for (int i = 0; i <= 6; i++) for (int j = 0; j < 7; j++) {
                    double u = f + (l - f) * i / 6, v = spun ? 2 * M_PI * j / 7 : (j - 3) * 0.003 * S;
                    gp_XYZ Cu = gc->Value(u).XYZ(), e;
                    if (spun) { gp_XYZ Z = P + A * ((Cu - P) * A), w = Cu - Z; e = Z + w * cos(v) + A.Crossed(w) * sin(v); }
                    else e = Cu + A * v;
                    gp_Pnt q = spun ? g->Value(-v, u) : g->Value(u, v);
                    dev(std::string(m.tname(sn)) + ": OCCT point vs XT formula", q.Distance(gp_Pnt(e)));
                }
            }
            newsurf = true;
        } else if (m.is(sn, "OFFSET_SURF")) { /* ref p.63: the underlying surface offset along its normal "taking into account the surface sense" */
            Handle(Geom_Surface) b = surface(m.ptr(sn, "surface"));
            if (!b.IsNull()) {
                double off = m.dbl(sn, "offset") * S;
                /* the sense taken into account = the underlying surface's (measured: RASTER.mcam's 16 offsets of '-' B-surfaces put their
                   edges on the surface only this way, formats/xt_occt_fixes.md) */
                if (m.chr(m.pnode(sn, "surface"), "sense") == '-' && !getenv("XTOCC_OFFSET_NATURAL")) off = -off;
                try { g = new Geom_OffsetSurface(b, off); } catch (...) { g.Nullify(); }
                if (g.IsNull()) { busy.erase(si); refuse(si, "OCCT offset surface not built"); return nullptr; }
                newsurf = true;
            }
        } else if (m.is(sn, "BLENDED_EDGE")) {
            g = blend_surface(si);
            if (!g.IsNull()) newsurf = true;
        } else {
            busy.erase(si); failed.insert(si);
            r.unmapped.push_back(std::string("surface ") + m.tname(sn));
            return nullptr;
        }
        busy.erase(si);
        if (g.IsNull()) { failed.insert(si); return nullptr; }
        (*surfs)[si] = g;
        if (!m.is(sn, "PLANE") && !m.is(sn, "CYLINDER") && !m.is(sn, "B_SURFACE")) xtfix_surfs.insert(g.get());
        if (r.check && (m.is(sn, "CONE") || m.is(sn, "SPHERE") || m.is(sn, "TORUS"))) {   /* OCCT vs the XT formula (xt_refgeom.h) */
            xtref::Surface X;
            X.kind = m.is(sn, "CONE") ? xtref::Surface::CONE : m.is(sn, "SPHERE") ? xtref::Surface::SPHERE : xtref::Surface::TORUS;
            X.p = m.vec(sn, X.kind == xtref::Surface::CONE ? "pvec" : "centre") * S; X.a = m.vec(sn, "axis"); X.x = m.vec(sn, "x_axis");
            X.r = m.dbl(sn, X.kind == xtref::Surface::TORUS ? "major_radius" : "radius") * S; X.r2 = m.dbl(sn, "minor_radius") * S;
            X.sa = m.dbl(sn, "sin_half_angle"); X.ca = m.dbl(sn, "cos_half_angle");
            for (int i = 0; i < 7; i++) for (int j = 0; j < 7; j++) {
                double u = 2 * M_PI * i / 7, v = X.kind == xtref::Surface::CONE ? (j - 3) * 0.003 * S : (j - 3) * 0.4;
                gp_Pnt q = g->Value(u, v / dcos);
                gp_XYZ e = X.eval(u, v), nx = X.normal(u, v, 1e-5);
                gp_Pnt qq; gp_Vec du, dv; g->D1(u, v / dcos, qq, du, dv); gp_Vec no = du.Crossed(dv);
                std::string k = std::string(m.tname(sn)) + ": OCCT point vs XT formula";
                dev(k, q.Distance(gp_Pnt(e)));
                if (no.Magnitude() > 1e-300 && nx.Modulus() > 1e-300) dev(std::string(m.tname(sn)) + ": OCCT natural normal vs XT's (angle, rad)", no.Angle(gp_Vec(nx)));
            }
        }
        return g;
    }
    bool newsurf = false;                              /* a new surface type was built (the 3drough tolerant path, g_new_types) */

    /* ---- rolling-ball blends */
    struct BlendDef { int id = 0; Handle(Geom_Curve) spine; Handle(Geom_Surface) sup[2]; double so[2] = {0, 0}; Handle(Geom_Curve) supc[2]; double range[2] = {0, 0}; bool neg = false; };
    std::map<int, BlendDef> bdefs;
    const BlendDef *blenddef(int bi) {
        auto it = bdefs.find(bi);
        if (it != bdefs.end()) return &it->second;
        if (failed.count(bi)) return nullptr;
        const Node *n = m.node(bi);
        if (!m.is(n, "BLENDED_EDGE")) return nullptr;
        if (busy.count(bi + 1000000)) { refuse(bi, "cyclic blend definition"); return nullptr; }
        busy.insert(bi + 1000000);
        BlendDef d; d.id = bi; std::string why;
        char bt = (char)m.chr(n, "blend_type");
        if (bt != 'R' && bt != 'E') why = std::string("blend type '") + bt + "' (ref p.61: only R / E)";
        const Node *sp = m.pnode(n, "spine");
        if (why.empty()) {
            Handle(Geom2d_Curve) pc; Handle(Geom_Surface) ps; bool t = false; double a, b;
            if (!sp || !any_curve(sp, d.spine, pc, ps, t, a, b, false) || d.spine.IsNull() || t) why = std::string("spine ") + m.tname(sp) + " not built";
        }
        if (why.empty()) propagate(bi, d.spine);
        for (int k = 0; k < 2 && why.empty(); k++) {
            d.range[k] = m.dbl(n, "range", k) * S;
            int sk = m.ptr(n, "surface", k); const Node *sn = m.node(sk);
            if (m.is(sn, "BLENDED_EDGE") && m.dbl(sn, "range", 0) == 0 && m.dbl(sn, "range", 1) == 0) {   /* cliff edge (ref p.61): its spine */
                Handle(Geom2d_Curve) pc; Handle(Geom_Surface) ps; bool t = false; double a, b;
                if (!any_curve(m.pnode(sn, "spine"), d.supc[k], pc, ps, t, a, b, false) || d.supc[k].IsNull() || t) why = "cliff-edge curve not built";
            } else if ((d.sup[k] = surface(sk)).IsNull()) why = std::string("support ") + m.tname(sn) + " not built";
            else {   /* ref p.61: the spine lies on the support offset by "the natural unit surface normal, times the range, times -1 if the
                        sense is negative"; the contact is the base point of that offset surface (not always the nearest point: a ball can
                        wrap a thinner support, e.g. RASTER.mcam blend 4812 on a 0.508 mm torus) */
                d.so[k] = d.range[k] * (m.chr(sn, "sense") == '-' ? -1 : 1);
            }
        }
        d.neg = m.chr(sp, "sense") == '-';
        busy.erase(bi + 1000000);
        if (!why.empty()) { refuse(bi, why); return nullptr; }
        return &(bdefs[bi] = d);
    }
    /* a blend's support that is itself a blend must exist where the ball touches it: the ball centres over this blend's spine interval
       go into that support's demand (exact: the centre lies in the support's section plane through the contact, on the contact's
       radius, so it projects to the contact's u and v) - before the support is built */
    std::set<int> propagated;
    void propagate(int bi, const Handle(Geom_Curve) &spine) {
        if (!propagated.insert(bi).second) return;
        const Node *n = m.node(bi);
        double rr = std::abs(m.dbl(n, "range", 0)) * S, t0, t1; bool full;
        if (!(rr > 0) || !urange(bi, spine, rr, t0, t1, full)) return;
        for (int k = 0; k < 2; k++) {
            int sk = m.ptr(n, "surface", k); const Node *sn = m.node(sk);
            if (!m.is(sn, "BLENDED_EDGE") || (m.dbl(sn, "range", 0) == 0 && m.dbl(sn, "range", 1) == 0)) continue;
            if (surfs->count(sk) && getenv("XTOCC_DEBUG")) fprintf(stderr, "blend %d: support blend %d already built\n", bi, sk);
            for (int i = 0; i <= 64; i++) demand[sk].push_back(spine->Value(t0 + (t1 - t0) * i / 64));
        }
    }
    /* the parameter of the curve point nearest p (OCCT's extrema report interior stationary points only: the ends too) */
    static bool cparam(const Handle(Geom_Curve) &gc, const gp_Pnt &p, double &u) {
        GeomAPI_ProjectPointOnCurve pr(p, gc);
        double best = 1e300;
        if (pr.NbPoints()) { u = pr.LowerDistanceParameter(); best = pr.LowerDistance(); }
        for (double t : { gc->FirstParameter(), gc->LastParameter() })
            if (!Precision::IsInfinite(t) && !gc->IsPeriodic() && gc->Value(t).Distance(p) < best) { best = gc->Value(t).Distance(p); u = t; }
        return best < 1e299;
    }
    bool spine_param(const BlendDef &d, const gp_Pnt &p, double &u) { return cparam(d.spine, p, u); }
    /* the ball's contact with support k when its centre is c (the nearest point; ref p.61) */
    /* the support point F with c = F + so[k] n(F) (n = unit natural normal): the nearest point when it qualifies, else the best of all
       the distance extrema (a ball can wrap a support thinner than itself); res = |c - F - so n| */
    bool foot_k(const BlendDef &d, int k, const gp_Pnt &c, gp_Pnt &f, double &res) {
        if (!d.supc[k].IsNull()) { double t; if (!cparam(d.supc[k], c, t)) return false; f = d.supc[k]->Value(t); res = std::abs(c.Distance(f) - std::abs(d.range[k])); return true; }
        gp_Vec nn;
        ftag = "foot_k"; bool fk = foot(d.sup[k], c, f, nn, nullptr, std::abs(d.so[k]) * 1.001 + fit_tol); ftag = "";
        if (!fk) return false;
        res = (gp_Vec(f, c) - nn * d.so[k]).Magnitude();
        if (res < 1e-3 * std::abs(d.so[k])) return true;
        if (!Handle(Geom_BSplineSurface)::DownCast(d.sup[k]).IsNull() || !Handle(Geom_OffsetSurface)::DownCast(d.sup[k]).IsNull()) return true;   /* all
            extrema only on analytic supports (a thin torus / cylinder; on B-splines too slow - the nearest point, its residual reported) */
        tick();
        GeomAPI_ProjectPointOnSurf pr(c, d.sup[k]);
        for (int i = 1; i <= pr.NbPoints(); i++) {
            double u, v; pr.Parameters(i, u, v); gp_Pnt q; gp_Vec du, dv; d.sup[k]->D1(u, v, q, du, dv);
            gp_Vec n2 = du.Crossed(dv); if (!(n2.Magnitude() > 1e-300)) continue; n2.Normalize();
            double r2 = (gp_Vec(q, c) - n2 * d.so[k]).Magnitude();
            if (r2 < res) { res = r2; f = q; }
        }
        return true;
    }
    bool support_foot(const BlendDef &d, int k, const gp_Pnt &c, gp_Pnt &f) {
        double res;
        if (!foot_k(d, k, c, f, res)) return false;
        if (record) dev("BLENDED_EDGE: ball centre to the support's offset (spine exactness)", res);
        if (record && getenv("XTOCC_DEBUG") && res > 1e-3 * S) fprintf(stderr, "spine off blend %d k %d: %g at %.6f %.6f %.6f\n", d.id, k, res, c.X(), c.Y(), c.Z());
        if (record) dev("BLENDED_EDGE: ball centre to support distance - |range|", std::abs(c.Distance(f) - std::abs(d.range[k])));
        if (getenv("XTOCC_DEBUG") && std::abs(c.Distance(f) - std::abs(d.range[k])) > 1e-5) fprintf(stderr, "support_foot blend %d k %d curve %d: c %.6f %.6f %.6f f %.6f %.6f %.6f dist %g range %g\n", d.id, k, (int)!d.supc[k].IsNull(), c.X(), c.Y(), c.Z(), f.X(), f.Y(), f.Z(), c.Distance(f), d.range[k]);
        return true;
    }
    /* BLEND_BOUND side of an INTERSECTION: p -> the boundary point on support k at the spine point of p (exact up to the spine) */
    bool contact(int bi, int k, gp_Pnt &p) {
        const BlendDef *d = blenddef(bi); double u;
        if (!d || !spine_param(*d, p, u)) return false;
        if (!d->spine->IsPeriodic() && (std::abs(u - d->spine->FirstParameter()) < 1e-12 || std::abs(u - d->spine->LastParameter()) < 1e-12)) return false;   /* beyond the spine */
        gp_Pnt c = d->spine->Value(u), f; double res;
        if (!foot_k(*d, k, c, f, res) || res > 10 * fit_tol) return false;   /* not evaluable here (a bounded support ends) */
        record = true; bool ok = support_foot(*d, k, c, p); record = false;
        return ok;
    }
    bool record = false;                               /* support_foot: record the deviations (fit / refinement samples only) */
    /* the exact blend section at spine parameter u: centre c, unit a (v = 0), unit bp perpendicular to a in the section plane towards
       v = 1, th = the angle of v = 1. R(u, v) = c + r (a cos(v th) + bp sin(v th)) (ref p.59-61) - also beyond [0, 1]: the
       surface continues round the circle (an offset blend can meet a spine-defining surface there) */
    /* xtfix2 (speed, ours): a blend's section at a spine parameter is computed once - the fit rounds re-sample the same u (Nu doubles:
       the old rows and cell centres come back) and the v interval / error rows repeat them */
    struct Sec { bool ok; gp_Pnt c; gp_Vec a, bp; double th; };
    std::map<std::pair<int, double>, Sec> sec_cache;
    bool section(const BlendDef &d, double u, gp_Pnt &c, gp_Vec &a, gp_Vec &bp, double &th) {
        auto key = std::make_pair(d.id, u);
        auto it = sec_cache.find(key);
        if (it != sec_cache.end()) { c = it->second.c; a = it->second.a; bp = it->second.bp; th = it->second.th; return it->second.ok; }
        bool ok = section0(d, u, c, a, bp, th);
        if (getenv("XTOCC_DEBUG_BLEND") && atoi(getenv("XTOCC_DEBUG_BLEND")) == d.id) fprintf(stderr, "section %d u %.9g ok %d th %.9g c %.6f %.6f %.6f a %.6f %.6f %.6f bp %.6f %.6f %.6f\n", d.id, u, (int)ok, th, c.X(), c.Y(), c.Z(), a.X(), a.Y(), a.Z(), bp.X(), bp.Y(), bp.Z());
        if (sec_cache.size() < 4000000) sec_cache[key] = { ok, c, a, bp, th };
        return ok;
    }
    bool section0(const BlendDef &d, double u, gp_Pnt &c, gp_Vec &a, gp_Vec &bp, double &th) {
        gp_Pnt f0, f1; c = d.spine->Value(u);
        if (!support_foot(d, 0, c, f0) || !support_foot(d, 1, c, f1)) return false;
        a = gp_Vec(c, f0); gp_Vec b(c, f1);
        if (d.neg) std::swap(a, b);
        double la = a.Magnitude(), lb = b.Magnitude(), rr = std::abs(d.range[0]);
        if (la < 1e-6 * rr || lb < 1e-6 * rr) { if (getenv("XTOCC_DEBUG")) fprintf(stderr, "section blend %d u %g: foot at the centre (%g %g)\n", d.id, u, la, lb); return false; }
        a /= la; b /= lb; th = a.Angle(b);
        if (th < 1e-9) { th = 0; bp = gp_Vec(0, 0, 0); return true; }   /* the boundaries meet (a terminator of the blend): a(u) = 0, the section is a point */
        bp = b - a * b.Dot(a); bp.Normalize();
        return true;
    }
    bool blend_row(const BlendDef &d, double u, double v0, double v1, int nv, gp_Pnt *row) {
        tick();
        gp_Pnt c; gp_Vec a, bp; double th, rr = std::abs(d.range[0]);
        record = true; bool ok = section(d, u, c, a, bp, th); record = false;
        if (!ok) return false;
        for (int j = 0; j < nv; j++) {
            double ph = th * (v0 + (v1 - v0) * j / (nv - 1));
            row[j] = c.Translated((a * cos(ph) + bp * sin(ph)) * rr);
        }
        return true;
    }
    /* the spine interval of blend si: the stored points on / bounding it projected on the spine (ours), + 5 % and 0.1 radius (more can
       run into a fold of the blend past a terminator) */
    double core0 = 0, core1 = 0;                       /* urange: the interval of the stored points alone (open spine) */
    bool urange(int si, const Handle(Geom_Curve) &spine, double rr, double &t0, double &t1, bool &full, const std::function<bool(double)> &valid = nullptr) {
        std::vector<double> ts;
        for (auto &p : demand[si]) { double u; bool cp = cparam(spine, p, u), vv = cp && (!valid || valid(u)); if (vv) ts.push_back(u);
            if (getenv("XTOCC_DEBUG_BLEND") && atoi(getenv("XTOCC_DEBUG_BLEND")) == si) fprintf(stderr, "urange %d: demand %.6f %.6f %.6f -> u %g (spine %g..%g) valid %d dist %g\n", si, p.X(), p.Y(), p.Z(), u, spine->FirstParameter(), spine->LastParameter(), (int)vv, cp ? spine->Value(u).Distance(p) : -1.0); }
        if (ts.empty()) return false;
        bool per = spine->IsPeriodic(); double P = per ? spine->Period() : 0;
        auto arclen = [&](double a, double b) { double L = 0; gp_Pnt q = spine->Value(a); for (int i = 1; i <= 64; i++) { gp_Pnt w = spine->Value(a + (b - a) * i / 64); L += q.Distance(w); q = w; } return L; };
        full = false;
        if (per) {
            double f0 = spine->FirstParameter();
            for (auto &t : ts) t = ElCLib::InPeriod(t, f0, f0 + P);
            std::sort(ts.begin(), ts.end());
            double gap = -1; size_t gi = 0;
            for (size_t i = 0; i < ts.size(); i++) { double g = (i + 1 < ts.size() ? ts[i + 1] : ts[0] + P) - ts[i]; if (g > gap) { gap = g; gi = i; } }
            t0 = gi + 1 < ts.size() ? ts[gi + 1] : ts[0]; t1 = t0 + (P - gap);
            if (ring_surf.count(si) || ts.size() < 2) full = true;
            else {
                double L = arclen(t0, t1), e = 0.05 * (t1 - t0) + (L > 0 ? (t1 - t0) * 0.1 * rr / L : 0);
                t0 -= e; t1 += e;
                if (t1 - t0 >= P) full = true;
            }
            if (full) { t0 = f0; t1 = f0 + P; }
        } else {
            t0 = *std::min_element(ts.begin(), ts.end()); t1 = *std::max_element(ts.begin(), ts.end());
            core0 = t0; core1 = t1;
            double L = arclen(t0, t1), e = 0.05 * (t1 - t0) + (L > 0 ? (t1 - t0) * 0.1 * rr / L : 0.1 * rr);
            t0 -= e; t1 += e;
            if (!Precision::IsInfinite(spine->FirstParameter())) t0 = std::max(t0, spine->FirstParameter());
            if (!Precision::IsInfinite(spine->LastParameter())) t1 = std::min(t1, spine->LastParameter());
        }
        return t1 > t0;
    }
    Handle(Geom_Surface) blend_surface(int si) {
        const Node *n = m.node(si);
        const BlendDef *dp = blenddef(si);
        if (!dp) return nullptr;
        const BlendDef d = *dp;
        double rr = std::abs(d.range[0]);
        if (!(rr > 0) || std::abs(rr - std::abs(d.range[1])) > 1e-9 * rr) { refuse(si, "blend with unequal / zero ranges (not a constant rolling ball)"); return nullptr; }
        (void)n;
        double t0, t1; bool full = false;
        /* the stored points count where the ball touches both supports at |range| (a bounded support, e.g. a B-surface, may end
           before a chart's artificial limit; ours), and the margin is kept only there */
        std::function<bool(double)> ok = [&](double u) { gp_Pnt c = d.spine->Value(u), f; for (int k = 0; k < 2; k++) {
                double res; if (!foot_k(d, k, c, f, res)) return false;
                if (res > 10 * fit_tol) return false; } return true; };
        if (!urange(si, d.spine, rr, t0, t1, full, ok)) { refuse(si, "no stored point on the blend where the ball touches both supports"); return nullptr; }
        if (!d.spine->IsPeriodic()) {
            double c0 = core0, c1 = core1;
            for (int it = 0; it < 30 && !ok(t0) && t0 < c0; it++) t0 = (it < 29) ? t0 + (c0 - t0) / 2 : c0;
            for (int it = 0; it < 30 && !ok(t1) && t1 > c1; it++) t1 = (it < 29) ? t1 - (t1 - c1) / 2 : c1;
        }
        if (!(t1 > t0)) { refuse(si, "empty spine interval"); return nullptr; }
        /* the v interval: [0, 1] and the stored points' v (ours: + 5 % of the arc) */
        double v0 = 0, v1 = 1;
        for (auto &p : demand[si]) {
            double u; gp_Pnt cc; gp_Vec a, bp; double th;
            if (!spine_param(d, p, u) || !section(d, u, cc, a, bp, th) || th < 1e-6) continue;
            gp_Vec w(cc, p); double v = atan2(w.Dot(bp), w.Dot(a)) / th;
            if (v < -0.5 / (th / M_PI) || v > 1 + 0.5 / (th / M_PI)) continue;   /* the far side of the circle: not this blend's point */
            v0 = std::min(v0, v); v1 = std::max(v1, v);
        }
        if (v0 < 0) v0 -= 0.05; if (v1 > 1) v1 += 0.05;
        /* fit, refined until the exact section points between the samples are within fit_tol */
        int Nu = 17, Nv = 9, worse = 0; Handle(Geom_BSplineSurface) g, best; double err = 1e300, best_err = 1e300;
        for (int round = 0; round < 10; round++) {
            int nrow = full ? Nu - 1 : Nu;    /* periodic: the last row is the first */
            auto uof = [&](double i) { return t0 + (t1 - t0) * i / (Nu - 1); };
            TColgp_Array2OfPnt A(1, nrow, 1, Nv);
            std::vector<gp_Pnt> row(Nv);
            for (int i = 0; i < nrow; i++) {
                if (!blend_row(d, uof(i), v0, v1, Nv, row.data())) { refuse(si, "blend section not computable (support projection)"); return nullptr; }
                for (int j = 0; j < Nv; j++) A(i + 1, j + 1) = row[j];
            }
            try { GeomAPI_PointsToBSplineSurface fit; fit.Interpolate(A, Approx_IsoParametric, full); g = fit.IsDone() ? fit.Surface() : nullptr; } catch (...) { g.Nullify(); }
            if (g.IsNull()) { refuse(si, "blend B-spline interpolation failed"); return nullptr; }
            double U1, U2, V1, V2; g->Bounds(U1, U2, V1, V2);
            Handle(ShapeAnalysis_Surface) sa = new ShapeAnalysis_Surface(g);
            auto dist = [&](double fi, double fj, const gp_Pnt &q) {
                gp_Pnt2d seed(U1 + (U2 - U1) * fi / (full ? Nu - 1 : Nu - 1), V1 + (V2 - V1) * fj / (Nv - 1));
                sa->NextValueOfUV(seed, q, 1e-4 * fit_tol, -1); return sa->Gap(); };
            double eu = 0, ev = 0; std::vector<gp_Pnt> rm(2 * Nv - 1);
            for (int i = 0; i + 1 < Nu; i++) {                   /* rows at the cell centres in u, every v sample and v centre */
                if (!blend_row(d, uof(i + 0.5), v0, v1, 2 * Nv - 1, rm.data())) { refuse(si, "blend section not computable (support projection)"); return nullptr; }
                for (int j = 0; j < 2 * Nv - 1; j++) eu = std::max(eu, dist(i + 0.5, j / 2.0, rm[j]));
            }
            for (int i = 0; i < nrow; i += std::max(1, nrow / 16)) { /* v centres on the sample rows */
                if (!blend_row(d, uof(i), v0, v1, 2 * Nv - 1, rm.data())) { refuse(si, "blend section not computable"); return nullptr; }
                for (int j = 1; j < 2 * Nv - 1; j += 2) ev = std::max(ev, dist(i, j / 2.0, rm[j]));
            }
            err = std::max(eu, ev);
            if (getenv("XTOCC_DEBUG")) {
                double es = 0, em = 0, eg = 0;
                for (int i = 0; i < nrow; i++) for (int j = 0; j < Nv; j++) es = std::max(es, g->Value(U1 + (U2 - U1) * i / (Nu - 1), V1 + (V2 - V1) * j / (Nv - 1)).Distance(A(i + 1, j + 1)));
                for (int i = 0; i + 1 < Nu; i++) { blend_row(d, uof(i + 0.5), v0, v1, 2 * Nv - 1, rm.data());
                    for (int j = 0; j < 2 * Nv - 1; j++) { em = std::max(em, g->Value(U1 + (U2 - U1) * (i + 0.5) / (Nu - 1), V1 + (V2 - V1) * j / 2.0 / (Nv - 1)).Distance(rm[j]));
                        if (getenv("XTOCC_DEBUG_EXTREMA")) { GeomAPI_ProjectPointOnSurf pp(rm[j], g); if (pp.NbPoints()) eg = std::max(eg, pp.LowerDistance()); } } }
                fprintf(stderr, "blend %d: v %g..%g u %g..%g%s Nu %d Nv %d eu %g ev %g | param: samples %g mids %g | extrema mids %g | U %g..%g V %g..%g\n", si, v0, v1, t0, t1, full ? " (full)" : "", Nu, Nv, eu, ev, es, em, eg, U1, U2, V1, V2);
            }
            if (err < best_err) { best_err = err; best = g; }
            else if (++worse >= 2) break;    /* denser sampling no longer helps (the samples' own noise, e.g. a fitted spine): the best */
            if (err <= fit_tol || Nu > 1024) break;
            if (eu > fit_tol) Nu = 2 * Nu - 1;
            if (ev > fit_tol && Nv < 65) Nv = 2 * Nv - 1;
        }
        g = best; err = best_err;
        /* the natural normal: XT's is the opposite of du x dv of this construction (u along the spine's parameter, v from the
           surface[0] boundary, swapped for a '-' spine). Measured, RASTER.mcam: the 13 blends that support other blends put those
           blends' spines on their offset (ref p.61: natural normal x range x sense) only with the reversed normal (13 / 13, spines
           ELLIPSE / INTERSECTION of both senses; the other sign misses by 2 x range); formats/xt_occt_fixes.md */
        g->VReverse();
        dev("BLENDED_EDGE: B-spline fit vs the rolling-ball definition", err);
        if (err > accept_tol) { char b[96]; snprintf(b, sizeof b, "blend fit deviation %.3g > 10 micron", err); refuse(si, b); return nullptr; }
        max_fit = std::max(max_fit, err);
        return g;
    }

    /* ---- INTERSECTION curves */
    struct ISide { Handle(Geom_Surface) s; int blend = 0, k = -1; };
    bool iside(int sn, ISide &o) {
        const Node *n = m.node(sn);
        if (m.is(n, "BLEND_BOUND")) {   /* ref p.62-63: the support blend.surface[1 - boundary] */
            o.blend = m.ptr(n, "blend"); o.k = 1 - m.sval(n, "boundary");
            return (o.k == 0 || o.k == 1) && blenddef(o.blend);
        }
        o.s = surface(sn); return !o.s.IsNull();
    }
    double side_dist(const ISide &a, const gp_Pnt &p) {
        gp_Pnt q = p; gp_Vec nn;
        if (a.blend) return contact(a.blend, a.k, q) ? q.Distance(p) : 1e300;
        gp_Pnt2d uv;
        if (!foot(a.s, p, q, nn, &uv)) return 1e300;
        gp_Vec w(q, p); double dn = w.Dot(nn);
        if (w.Magnitude() > 10 * fit_tol && (w - nn * dn).Magnitude() > 0.1 * w.Magnitude()) {   /* the nearest point is on the surface's
                                                                                                   boundary: p is beyond a bounded surface */
            if (r.check) { double d = ext_lin_off(a.s, p, uv); if (getenv("XTOCC_DEBUG_EXT")) fprintf(stderr, "ext %g type %s\n", d, a.s->DynamicType()->Name());
                if (d < 1e299) dev("INTERSECTION: stored chart points beyond an offset B-surface -> offset of its tangent-extended basis (measured)", d); }
            return 1e300;
        }
        return w.Magnitude();
    }
    /* xtfix2 (measurement, check mode): Parasolid evaluates an OFFSET_SURF of a B-surface beyond the basis' knot range (formats/
       xt_occt_fixes.md 7 "B-surface extension"). Measured on the stored chart points that lie there (RASTER.mcam 12, TUTORIAL #3 MULTIAXIS,
       Z AXIS LIMITS): they lie on the offset of the basis' tangent (C1 linear) continuation Eb(u, v) = Sb(ub, v) + (u - ub) Sb_u(ub, v)
       (or in v) to <= 2e-10 model units; the polynomial continuation of the end span misses by up to 26 in. Not yet used to build the
       surfaces (an OCCT surface for it is open, formats/TODO.md): the distance is reported. Gauss-Newton on (u, v), finite differences */
    double ext_lin_off(const Handle(Geom_Surface) &s, const gp_Pnt &p, gp_Pnt2d uv) {
        Handle(Geom_OffsetSurface) os = Handle(Geom_OffsetSurface)::DownCast(s); if (os.IsNull()) return 1e300;
        Handle(Geom_Surface) sb = os->BasisSurface(); double d0 = os->Offset();
        double U1, U2, V1, V2; sb->Bounds(U1, U2, V1, V2);
        double eu = 1e-7 * (U2 - U1), ev = 1e-7 * (V2 - V1);
        int dir = (uv.X() <= U1 + eu || uv.X() >= U2 - eu) ? 0 : (uv.Y() <= V1 + ev || uv.Y() >= V2 - ev) ? 1 : -1;
        if (dir < 0) return 1e300;
        double ub = dir == 0 ? uv.X() : uv.Y();
        auto Eb = [&](double u, double v) { gp_Pnt q; gp_Vec su, sv; if (dir == 0) { sb->D1(ub, v, q, su, sv); return q.Translated(su * (u - ub)); }
            sb->D1(u, ub, q, su, sv); return q.Translated(sv * (v - ub)); };
        auto E = [&](double u, double v) { double h = 1e-6; gp_Vec a(Eb(u - h, v), Eb(u + h, v)), b(Eb(u, v - h), Eb(u, v + h)); gp_Vec n = a.Crossed(b); n.Normalize();
            return Eb(u, v).Translated(n * d0); };
        double u = uv.X(), v = uv.Y(), d = 1e300;
        for (int it = 0; it < 80; it++) {
            gp_Vec r(p, E(u, v)); d = r.Magnitude(); double h = 1e-7;
            gp_Vec ju(E(u - h, v), E(u, v)), jv(E(u, v - h), E(u, v)); ju /= h; jv /= h;
            double a11 = ju.Dot(ju), a12 = ju.Dot(jv), a22 = jv.Dot(jv), g1 = ju.Dot(r), g2 = jv.Dot(r), det = a11 * a22 - a12 * a12;
            if (std::abs(det) < 1e-300) break;
            double du = -(a22 * g1 - a12 * g2) / det, dv = -(a11 * g2 - a12 * g1) / det; u += du; v += dv;
            if (std::abs(du) + std::abs(dv) < 1e-14) break;
        }
        return d;
    }
    /* p -> a point of the exact intersection near it */
    bool refine(const ISide &A, const ISide &B, gp_Pnt &p) {
        tick();
        if (A.blend || B.blend) return contact(A.blend ? A.blend : B.blend, A.blend ? A.k : B.k, p);
        double res = 1e300;
        for (int it = 0; it < 60; it++) {
            gp_Pnt fa, fb; gp_Vec na, nb;
            ftag = "refine";
            if (!foot(A.s, p, fa, na, nullptr, 1e-4 * S) || !foot(B.s, p, fb, nb, nullptr, 1e-4 * S)) { ftag = ""; return false; }
            ftag = "";
            res = std::max(p.Distance(fa), p.Distance(fb));
            if (res < 1e-3 * fit_tol) return true;
            double c = na.Dot(nb), det = 1 - c * c, ga = gp_Vec(p, fa).Dot(na), gb = gp_Vec(p, fb).Dot(nb);
            if (det < 1e-10) { p = gp_Pnt((fa.XYZ() + fb.XYZ()) / 2); continue; }   /* tangential: alternate */
            double al = (ga - c * gb) / det, be = (gb - c * ga) / det;
            gp_Vec step = na * al + nb * be;
            p.Translate(step);
            if (step.Magnitude() < 1e-6 * fit_tol && res > fit_tol) break;   /* xtfix2 (speed): stalled (a foot on a surface's boundary, p beyond it) */
        }
        return res < fit_tol;
    }
    Handle(Geom_Curve) icurve(int ci) {
        auto it = icurves.find(ci);
        if (it != icurves.end()) return it->second;
        if (failed.count(ci)) return nullptr;
        if (busy.count(ci)) { refuse(ci, "cyclic curve definition"); return nullptr; }
        busy.insert(ci);
        Handle(Geom_Curve) out = icurve_build(ci);
        busy.erase(ci);
        if (out.IsNull()) { failed.insert(ci); return nullptr; }
        return icurves[ci] = out;
    }
    Handle(Geom_Curve) icurve_build(int ci) {
        const Node *c = m.node(ci), *ch = m.pnode(c, "chart"), *ls = m.pnode(c, "start"), *le = m.pnode(c, "end");
        ISide A, B;
        if (!ch || ch->count < 1) { refuse(ci, "no chart"); return nullptr; }
        if (!iside(m.ptr(c, "surface", 0), A) || !iside(m.ptr(c, "surface", 1), B)) { refuse(ci, std::string("its surfaces (") + m.tname(m.pnode(c, "surface")) + ", " + m.tname(m.node(m.ptr(c, "surface", 1))) + ") not built"); return nullptr; }
        double dup = 1e-9 * S;
        std::vector<gp_Pnt> P;
        auto add = [&](const gp_Pnt &q) { if (P.empty() || P.back().Distance(q) > dup) P.push_back(q); };
        if (ls && m.chr(ls, "type") == 'T' && ls->count >= 2) add(hvec(ls, 0));   /* ref p.45: the terminator, then the branch point = chart end */
        for (uint32_t k = 0; k < ch->count; k++) add(hvec(ch, (int)k));
        if (le && m.chr(le, "type") == 'T' && le->count >= 2) add(hvec(le, 0));
        bool closed = ls && le && m.chr(ls, "type") == 'H' && P.size() > 3 && P.front().Distance(P.back()) <= dup * 10;
        if (closed) P.pop_back();
        if (P.size() < 2) { refuse(ci, "fewer than 2 chart points"); return nullptr; }
        for (auto &q : P) {   /* the stored points on our two surfaces, where both are evaluable there */
            double da = side_dist(A, q), db = side_dist(B, q);
            if (da < 1e299 && db < 1e299) dev("INTERSECTION: stored chart points to its two surfaces", std::max(da, db));
            else dev("INTERSECTION: stored chart points not evaluable (beyond a bounded surface / spine; count)", 0);
            if (getenv("XTOCC_DEBUG")) fprintf(stderr, "intersection %d (%s %s): stored point %.6f %.6f %.6f -> %g %g\n", ci, m.tname(m.pnode(c, "surface")), m.tname(m.node(m.ptr(c, "surface", 1))), q.X(), q.Y(), q.Z(), da, db);
        }
        auto se = spine_ext.find(ci);
        if (se != spine_ext.end() && !closed) {   /* a blend's spine: continued past its 'L' limits (ref p.45: "an artificial boundary ... on an
                                                     otherwise potentially infinite branch") so the ball reaches the blend's ends - marched on the
                                                     exact intersection (ours: 20 % of its length + 4 radii) */
            double L = 0; for (size_t i = 1; i < P.size(); i++) L += P[i].Distance(P[i - 1]);
            double E = 0.2 * L + 4 * se->second, h = std::max(L / (P.size() - 1), dup * 100);
            h = std::min(h, E / 4);
            for (int end = 0; end < 2; end++) {
                const Node *lim = end ? le : ls;
                if (!lim || m.chr(lim, "type") != 'L') continue;
                std::vector<gp_Pnt> X;
                gp_Pnt a = end ? P[P.size() - 2] : P[1], b = end ? P.back() : P.front();
                for (double done = 0; done < E && X.size() < 200; ) {
                    gp_Vec dir(a, b); if (dir.Magnitude() < dup) break; dir.Normalize();
                    gp_Pnt q = b.Translated(dir * h);
                    if (!refine(A, B, q) || q.Distance(b) < h / 2 || gp_Vec(b, q).Dot(dir) <= 0) break;
                    X.push_back(q); done += q.Distance(b); a = b; b = q;
                }
                if (end) P.insert(P.end(), X.begin(), X.end()); else P.insert(P.begin(), X.rbegin(), X.rend());
            }
        }
        /* a refined point in the middle of every chord, then refine where the fit leaves the intersection */
        std::vector<gp_Pnt> Q;
        size_t np = P.size();
        for (size_t i = 0; i < np; i++) {
            Q.push_back(P[i]);
            if (i + 1 == np && !closed) break;
            gp_Pnt mid((P[i].XYZ() + P[(i + 1) % np].XYZ()) / 2);
            if (refine(A, B, mid) && mid.Distance(P[i]) > dup && mid.Distance(P[(i + 1) % np]) > dup) Q.push_back(mid);
        }
        Handle(Geom_BSplineCurve) bc, best; double err = 1e300, best_err = 1e300; int worse = 0;
        for (int round = 0; round < 24 && Q.size() < 20000; round++) {
            int n = (int)Q.size();
            Handle(TColgp_HArray1OfPnt) H = new TColgp_HArray1OfPnt(1, n);
            Handle(TColStd_HArray1OfReal) T = new TColStd_HArray1OfReal(1, closed ? n + 1 : n);
            double L = 0;
            for (int i = 0; i < n; i++) { if (i) L += Q[i].Distance(Q[i - 1]); H->SetValue(i + 1, Q[i]); T->SetValue(i + 1, L); }
            if (closed) T->SetValue(n + 1, L + Q[n - 1].Distance(Q[0]));
            try { GeomAPI_Interpolate ip(H, T, closed, dup / 10); ip.Perform(); bc = ip.IsDone() ? ip.Curve() : nullptr; } catch (...) { bc.Nullify(); }
            if (bc.IsNull()) { refuse(ci, "B-spline interpolation failed"); return nullptr; }
            std::vector<gp_Pnt> R; err = 0; bool added = false;
            int nint = closed ? n : n - 1;
            for (int i = 0; i < n; i++) {
                R.push_back(Q[i]);
                if (i >= nint) break;
                for (double fr : { 0.25, 0.5, 0.75 }) {   /* the interval's quarter points against the exact intersection */
                    double tm = T->Value(i + 1) + (T->Value(i + 2) - T->Value(i + 1)) * fr;
                    gp_Pnt q = bc->Value(tm), e = q;
                    if (!refine(A, B, e)) continue;
                    double d = q.Distance(e);
                    err = std::max(err, d);
                    if (d > fit_tol && e.Distance(Q[i]) > dup && e.Distance(Q[(i + 1) % n]) > dup && (R.empty() || e.Distance(R.back()) > dup)) { R.push_back(e); added = true; }
                }
            }
            if (getenv("XTOCC_DEBUG")) fprintf(stderr, "intersection %d: round %d points %d err %g\n", ci, round, n, err);
            if (err < best_err) { best_err = err; best = bc; worse = 0; } else if (++worse >= 2) break;   /* no better: the samples' noise */
            if (!added) break;
            Q = R;
        }
        bc = best; err = best_err;
        dev("INTERSECTION: B-spline fit vs the exact intersection", err);
        if (err > accept_tol) { char b[96]; snprintf(b, sizeof b, "fit deviation %.3g > 10 micron", err); refuse(ci, b); return nullptr; }
        max_fit = std::max(max_fit, err);
        return bc;
    }

    bool general_edge(int eidx, const Node *c, int v0, int v1, Handle(Geom_Curve) &gc, double &t0, double &t1) {
        Handle(Geom2d_Curve) pc; Handle(Geom_Surface) ps; bool trim = false, ok = false; double p1 = 0, p2 = 0;
        try { ok = any_curve(c, gc, pc, ps, trim, p1, p2); } catch (...) { ok = false; }
        if (!ok) { r.unmapped.push_back(std::string("curve ") + m.tname(c)); return false; }
        newtypes = true;
        if (v0 <= 1 && v1 <= 1 && !trim && gc->IsPeriodic()) {   /* xtfix: a ring edge on a periodic curve (closed INTERSECTION, ELLIPSE): one period */
            t0 = gc->FirstParameter(); t1 = t0 + gc->Period();
            if (!pc.IsNull()) sp_pc[gc.get()] = { ps.get(), pc };
            { double et = m.dbl(m.node(eidx), "tolerance"); if (et > 0) etol[eidx] = et * S; }
            return true;
        }
        if (v0 <= 1 || v1 <= 1) { r.error = "edge without vertices on a new curve type (ring edges on NURBS / ellipses: not mapped)"; return false; }
        gp_Pnt a = BRep_Tool::Pnt(vertex(v0)), b = BRep_Tool::Pnt(vertex(v1));
        auto param = [&](const Handle(Geom_Curve) &g, const gp_Pnt &p) {
            double u = g->FirstParameter(); cparam(g, p, u); return u; };   /* xtfix: the nearest of the interior extrema and the ends (OCCT's
                extrema skip the ends: a vertex at an end got the first parameter, or a distance maximum inside) */
        /* the parameter interval: a TRIMMED_CURVE's parm_1 .. parm_2 on its basis (metres on a line: x S), increasing (on a periodic basis
           the arc from parm_1 to parm_2, or the other one); else the vertices' projections. The fin order then picks the direction:
           the curve is reversed when its interval starts at the end vertex (ours) */
        double P = gc->IsPeriodic() ? gc->Period() : 0;
        std::vector<std::pair<double, double>> cand;
        if (trim) {
            double ls = Handle(Geom_Line)::DownCast(gc).IsNull() ? 1 : S, q1 = p1 * ls, q2 = p2 * ls;
            if (trim_unscaled) { q1 = p1; q2 = p2; trim_unscaled = false; }   /* xtfix: parameters of the built basis itself */
            const Node *bb = m.pnode(c, "basis_curve");
            if (P > 0 && !getenv("XTOCC_TRIM_BOTH_ARCS")) {   /* xtfix2: ref p.46 - the trimmed portion runs from parm_1 to parm_2, increasing on a
                    '+' basis ("parm_2 > parm_1"), decreasing on a '-' one, at most a period: one arc (before: both arcs between the vertices
                    were candidates and a rounding tie could take the complementary one - IMPELLER WITH TILTED CURVE sld_1 edge 3797) */
                if (m.chr(bb, "sense") != '-') { double h = q2; while (h <= q1) h += P; while (h > q1 + P) h -= P; cand.push_back({q1, h}); }
                else { double h = q2; while (h >= q1) h -= P; while (h < q1 - P) h += P; cand.push_back({h, q1}); }
            }
            else if (P > 0) { double h = q2; while (h <= q1) h += P; cand.push_back({q1, h}); h = q1; while (h <= q2) h += P; cand.push_back({q2, h}); }
            else cand.push_back({std::min(q1, q2), std::max(q1, q2)});
        } else {
            double u = param(gc, a), w = param(gc, b);
            if (P > 0) { double h = w; if (v0 == v1) h = u + P; else while (h <= u) h += P; cand.push_back({u, h}); h = u; while (h <= w) h += P; cand.push_back({w, h}); }
            else cand.push_back({std::min(u, w), std::max(u, w)});
        }
        double best = 1e300; bool rev = false;
        for (auto &iv : cand) {
            double df = gc->Value(iv.first).Distance(a) + gc->Value(iv.second).Distance(b), dr = gc->Value(iv.first).Distance(b) + gc->Value(iv.second).Distance(a);
            if (df < best) { best = df; t0 = iv.first; t1 = iv.second; rev = false; }
            if (dr < best - (P > 0 && !trim ? tol : 0)) { best = dr; t0 = iv.first; t1 = iv.second; rev = true; }   /* xtfix: on a periodic curve
                both arcs fit the vertices (forward a -> b and reversed b -> a): a tie (rounding) keeps the forward one, the curve order the
                fin sense gives (make_edge) - the reversed one is the other arc */
        }
        if (rev) {
            double r0 = gc->ReversedParameter(t1), r1 = gc->ReversedParameter(t0);
            gc = gc->Reversed(); if (!pc.IsNull()) pc = pc->Reversed(); t0 = r0; t1 = r1;
        }
        r.max_vertex_gap = std::max({r.max_vertex_gap, gc->Value(t0).Distance(a), gc->Value(t1).Distance(b)});
        if (getenv("XTOCC_DEBUG") && std::max(gc->Value(t0).Distance(a), gc->Value(t1).Distance(b)) > 1e-3) {
            const Node *bb = m.is(c, "TRIMMED_CURVE") ? m.pnode(c, "basis_curve") : c;
            fprintf(stderr, "edge %d %s/%s trim %d p %g..%g t %g..%g gaps %g %g (curve %g..%g)\n", eidx, m.tname(c), m.tname(bb), trim, p1, p2, t0, t1,
                    gc->Value(t0).Distance(a), gc->Value(t1).Distance(b), gc->FirstParameter(), gc->LastParameter()); }
        if (!pc.IsNull()) sp_pc[gc.get()] = { ps.get(), pc };
        { double et = m.dbl(m.node(eidx), "tolerance"); if (et > 0) etol[eidx] = et * S; }
        return true;
    }

    /* the edge's curve-order ends: the fin EDGE.halfedge runs from the partner's vertex to its own (formats/xt.md);
       sense '-' = the fin runs against the curve */
    bool make_edge(int eidx) {
        const Node *e = m.node(eidx);
        const Node *hf = m.pnode(e, "halfedge");
        if (!hf) { r.error = "edge without fin"; return false; }
        const Node *ot = m.pnode(hf, "other");
        const Node *c = m.pnode(e, "curve");
        if (!c) c = m.pnode(hf, "curve");
        if (!m.pnode(e, "curve") && ot && m.pnode(ot, "curve")) {   /* xtfix: a tolerant edge (curves on its fins, ref p.46): not the fin curve
                                                                    that is an SP curve on a blend (its parametrisation is not reproduced) */
            auto on_blend = [&](const Node *q) { if (m.is(q, "TRIMMED_CURVE")) q = m.pnode(q, "basis_curve"); return m.is(q, "SP_CURVE") && m.is(m.pnode(q, "surface"), "BLENDED_EDGE"); };
            if (on_blend(c) && !on_blend(m.pnode(ot, "curve"))) c = m.pnode(ot, "curve");
        }
        if (!c) {   /* xtfix2: EDGE.halfedge is a dummy fin (ref p.92: "Dummy fins have fin->loop = ... = fin->curve = null"; a laminar edge
                       of a sheet body, e.g. HST DYNAMIC OPTIROUGH AND REST MATERIAL.mcam): the curve of a real fin of the edge */
            const Node *h = ot; int g = 0;
            while (h && h != hf && !m.pnode(h, "curve") && ++g < 64) h = m.pnode(h, "other");
            if (h) c = m.pnode(h, "curve");
        }
        if (!c) { r.error = "edge without curve"; return false; }
        int vf = m.ptr(hf, "vertex"), vo = ot ? m.ptr(ot, "vertex") : 1;
        bool along = m.chr(hf, "sense") != '-';
        int v0 = along ? vo : vf, v1 = along ? vf : vo;      /* curve-order start / end vertices */
        TopoDS_Edge E;
        Handle(Geom_Curve) gc;
        double t0, t1;
        if (m.is(c, "LINE")) {
            gp_Dir d(m.vec(c, "direction"));
            if (m.chr(c, "sense") == '-') d.Reverse();
            gp_Lin L(pnt(m.vec(c, "pvec")), d);
            if (v0 <= 1 || v1 <= 1) { r.error = "line edge without vertices"; return false; }
            gp_Pnt a = BRep_Tool::Pnt(vertex(v0)), b = BRep_Tool::Pnt(vertex(v1));
            t0 = ElCLib::Parameter(L, a); t1 = ElCLib::Parameter(L, b);
            r.max_vertex_gap = std::max({r.max_vertex_gap, L.Distance(a), L.Distance(b)});
            if (!(t1 > t0)) {
                char s[128]; snprintf(s, sizeof s, "LINE edge %d: end parameter %.9g <= start %.9g (sense rule contradicted)", eidx, t1, t0);
                r.notes.push_back(s);
            }
            gc = new Geom_Line(L);
        } else if (m.is(c, "CIRCLE")) {
            gp_Dir n(m.vec(c, "normal")), x(m.vec(c, "x_axis"));
#ifndef XTOCC_MUTATE_CIRCLE_SENSE   /* test hook (ours): proves the accuracy check catches a wrong sense rule */
            if (m.chr(c, "sense") == '-') n.Reverse();          /* viewer/xt_mesh.c sample_edge */
#endif
            gp_Circ C(gp_Ax2(pnt(m.vec(c, "centre")), n, x), m.dbl(c, "radius") * S);
            if (v0 <= 1 && v1 <= 1) {                            /* ring edge: full circle (formats/xt.md) */
                t0 = 0;
                auto ra = ring_at.find(eidx);
                if (ra != ring_at.end()) t0 = ElCLib::Parameter(C, ra->second);
                t1 = t0 + 2 * M_PI;
            } else {
                if (v0 <= 1 || v1 <= 1) { r.error = "circle edge with one vertex missing"; return false; }
                gp_Pnt a = BRep_Tool::Pnt(vertex(v0)), b = BRep_Tool::Pnt(vertex(v1));
                r.max_vertex_gap = std::max({r.max_vertex_gap, C.Distance(a), C.Distance(b)});
                t0 = ElCLib::Parameter(C, a); t1 = ElCLib::Parameter(C, b);
                if (v0 == v1) t1 = t0 + 2 * M_PI;
                else { while (t1 <= t0) t1 += 2 * M_PI; }
            }
            gc = new Geom_Circle(C);
        } else if (!general_edge(eidx, c, v0, v1, gc, t0, t1)) {   /* 3drough: TRIMMED_CURVE / B_CURVE / SP_CURVE / ELLIPSE */
            return false;
        }
        B.MakeEdge(E, gc, tol);
        TopoDS_Vertex A, Bv;
        if (v0 <= 1) {                       /* ring: one vertex (OCCT needs one), at the seam of its cylinder face (ours) */
            B.MakeVertex(A, gc->Value(t0), tol); Bv = A;
        } else { A = vertex(v0); Bv = vertex(v1); }
        B.Add(E, A.Oriented(TopAbs_FORWARD));
        B.Add(E, Bv.Oriented(TopAbs_REVERSED));
        B.Range(E, t0, t1);
        edge[eidx] = E; ecurve[eidx] = gc; erange[eidx] = {t0, t1};
        if (!m.pnode(e, "curve") && !getenv("XTOCC_NO_FIN_PCURVES")) fin_pcurves(e, c, gc, t0, t1, v0, v1);   /* xtfix2: a tolerant edge's other fins */
        return true;
    }
};

/* analytic pcurve of an edge curve on a plane / cylinder (ours: exact, same parameter as the 3D curve).
   ushift: u offset (k * 2 pi) chosen by the caller for wire continuity on a cylinder. */
static Handle(Geom2d_Curve) pcurve0(const Handle(Geom_Curve) &gc, const Handle(Geom_Surface) &gs, double ushift, std::string &why) {
    if (auto pl = Handle(Geom_Plane)::DownCast(gs)) {
        gp_Ax3 ax = pl->Position();
        auto uv = [&](const gp_Pnt &p) { gp_Vec d(ax.Location(), p); return gp_Pnt2d(d.Dot(ax.XDirection()), d.Dot(ax.YDirection())); };
        auto dv = [&](const gp_Dir &d) { return gp_Dir2d(d.Dot(ax.XDirection()), d.Dot(ax.YDirection())); };
        if (auto ln = Handle(Geom_Line)::DownCast(gc)) {
            gp_Lin L = ln->Lin();
            if (std::abs(L.Direction().Dot(ax.Direction())) > 1e-6) { why = "line not in its face's plane"; return nullptr; }
            return new Geom2d_Line(uv(L.Location()), dv(L.Direction()));
        }
        if (auto ci = Handle(Geom_Circle)::DownCast(gc)) {
            gp_Circ C = ci->Circ();
            if (std::abs(std::abs(C.Axis().Direction().Dot(ax.Direction())) - 1) > 1e-6) { why = "circle not in its face's plane"; return nullptr; }
            return new Geom2d_Circle(gp_Ax22d(uv(C.Location()), dv(C.XAxis().Direction()), dv(C.YAxis().Direction())), C.Radius());
        }
    }
    if (auto cy = Handle(Geom_CylindricalSurface)::DownCast(gs)) {
        gp_Ax3 ax = cy->Position();
        gp_Dir a = ax.Direction();
        auto uof = [&](const gp_Pnt &p) {
            gp_Vec d(ax.Location(), p);
            double u = atan2(d.Dot(ax.YDirection()), d.Dot(ax.XDirection()));
            return u < 0 ? u + 2 * M_PI : u;
        };
        auto vof = [&](const gp_Pnt &p) { return gp_Vec(ax.Location(), p).Dot(a); };
        if (auto ln = Handle(Geom_Line)::DownCast(gc)) {
            gp_Lin L = ln->Lin();
            double da = L.Direction().Dot(a);
            if (std::abs(std::abs(da) - 1) > 1e-6) { why = "line on a cylinder not parallel to its axis"; return nullptr; }
            return new Geom2d_Line(gp_Pnt2d(uof(L.Location()) + ushift, vof(L.Location())), gp_Dir2d(0, da > 0 ? 1 : -1));
        }
        if (auto ci = Handle(Geom_Circle)::DownCast(gc)) {
            gp_Circ C = ci->Circ();
            double dn = C.Axis().Direction().Dot(a);
            gp_Vec off(ax.Location(), C.Location());
            if (std::abs(std::abs(dn) - 1) > 1e-6) { why = "circle on a cylinder not coaxial"; return nullptr; }
            gp_Pnt x0 = C.Location().Translated(gp_Vec(C.XAxis().Direction()) * C.Radius());
            return new Geom2d_Line(gp_Pnt2d(uof(x0) + ushift, vof(x0)), gp_Dir2d(dn > 0 ? 1 : -1, 0));
        }
    }
    why = "no pcurve rule for this curve/surface pair";
    return nullptr;
}
std::map<const Geom_Curve *, std::pair<const Geom_Surface *, Handle(Geom2d_Curve)>> *g_sp_pc;   /* 3drough: Ctx::sp_pc */
std::map<std::pair<const Geom_Curve *, const Geom_Surface *>, Handle(Geom2d_Curve)> *g_fin_pc;     /* xtfix2: Ctx::fin_pc */
std::map<std::pair<const Geom_Curve *, const Geom_Surface *>, double> *g_fin_vt;                    /* xtfix2: Ctx::fin_vt */
bool g_new_types;                                                                                  /* 3drough: the body uses them */

/* the pcurve, accepted only if surface(pcurve(t)) stays within tol of curve(t) over [t0, t1] (Parasolid's own data
   is exact only to its resolution BODY.res_linear: e.g. buckle-v12 sld_401 has a line 8.5e-9 rad off its plane) */
double g_max_cs_gap = 0;
double g_pc_tol = 0;          /* xtfix2: the tolerance the last pcurve() result needs on its face (> tol only for a tolerant edge's stored fin curve) */
double g_fin_ratio = 0;       /* xtfix2: the largest (stored fin curves' separation) / EDGE.tolerance accepted */
double g_interp_gap = -1;     /* xtfix2: interp_pcurve's failure: the curve point's distance to the surface */
static Handle(Geom2d_Curve) interp_pcurve(const Handle(Geom_Curve) &gc, const Handle(Geom_Surface) &gs, double t0, double t1, double tol) {
    Handle(ShapeAnalysis_Surface) sa = new ShapeAnalysis_Surface(gs);
    bool up = gs->IsUPeriodic(), vp = gs->IsVPeriodic(); double UP = up ? gs->UPeriod() : 0, VP = vp ? gs->VPeriod() : 0;
    std::vector<double> T; for (int i = 0; i <= 16; i++) T.push_back(t0 + (t1 - t0) * i / 16);
    Handle(Geom2d_BSplineCurve) bc;
    for (int round = 0; round < 8; round++) {
        int n = (int)T.size();
        Handle(TColgp_HArray1OfPnt2d) H = new TColgp_HArray1OfPnt2d(1, n); Handle(TColStd_HArray1OfReal) P = new TColStd_HArray1OfReal(1, n);
        gp_Pnt2d prev;
        for (int i = 0; i < n; i++) {
            gp_Pnt q3 = gc->Value(T[i]);
            gp_Pnt2d uv = i ? sa->NextValueOfUV(prev, q3, 1e-12, -1) : sa->ValueOfUV(q3, 1e-12);
            if (i && sa->Gap() > tol) { gp_Pnt2d g2 = sa->ValueOfUV(q3, 1e-12); if (sa->Gap() < tol) uv = g2; }   /* the local search slid off: global */
            if (i) { if (up) uv.SetX(ElCLib::InPeriod(uv.X(), prev.X() - UP / 2, prev.X() + UP / 2)); if (vp) uv.SetY(ElCLib::InPeriod(uv.Y(), prev.Y() - VP / 2, prev.Y() + VP / 2)); }
            H->SetValue(i + 1, uv); P->SetValue(i + 1, T[i]); prev = uv;
        }
        try { Geom2dAPI_Interpolate ip(H, P, Standard_False, 1e-12); ip.Perform(); bc = ip.IsDone() ? ip.Curve() : nullptr; } catch (...) { return nullptr; }
        if (bc.IsNull()) return nullptr;
        std::vector<double> R; bool added = false;
        for (int i = 0; i < n; i++) {
            R.push_back(T[i]);
            if (i + 1 == n) break;
            double tm = (T[i] + T[i + 1]) / 2; gp_Pnt2d uv = bc->Value(tm);
            if (gc->Value(tm).Distance(gs->Value(uv.X(), uv.Y())) > tol) { R.push_back(tm); added = true; }
        }
        if (!added || R.size() > 4000) break;
        T = R;
    }
    if (bc.IsNull()) return bc;
    for (int k = 0; k <= 64; k++) {   /* the pcurve shares the edge's parameter: surface(pcurve(t)) against curve(t) */
        double t = t0 + (t1 - t0) * k / 64; gp_Pnt2d uv = bc->Value(t);
        if (gc->Value(t).Distance(gs->Value(uv.X(), uv.Y())) > 4 * tol) {
            { gp_Pnt q = gc->Value(t); sa->ValueOfUV(q, 1e-12); g_interp_gap = sa->Gap(); }
            if (getenv("XTOCC_DEBUG")) { gp_Pnt q = gc->Value(t); sa->ValueOfUV(q, 1e-12); fprintf(stderr, "interp_pcurve: k %d gap %g > 4 tol %g (curve point to the surface: %g)\n", k, gc->Value(t).Distance(gs->Value(uv.X(), uv.Y())), 4 * tol, sa->Gap()); }
            return nullptr;
        }
    }
    return bc;
}
Handle(Geom2d_Curve) pcurve(const Handle(Geom_Curve) &gc, const Handle(Geom_Surface) &gs, double ushift, double tol,
                            std::string &why, double t0, double t1) {
    Handle(Geom2d_Curve) pc = pcurve0(gc, gs, ushift, why);
    bool projected = false, finpc = false; double vt = 0;
    g_interp_gap = -1;
    g_pc_tol = tol;
    if (pc.IsNull() && g_new_types && why == "no pcurve rule for this curve/surface pair") {   /* 3drough (ours): the SP_CURVE's pcurve, else
                                                                                                 a projection (GeomProjLib::Curve2d) */
        bool own = false; finpc = false;
        if (g_sp_pc) { auto it = g_sp_pc->find(gc.get()); if (it != g_sp_pc->end() && it->second.first == gs.get()) { pc = it->second.second; own = true; } }
        if (!own && g_fin_pc) { auto it = g_fin_pc->find({ gc.get(), gs.get() }); if (it != g_fin_pc->end()) { pc = it->second; own = finpc = true; vt = (*g_fin_vt)[{ gc.get(), gs.get() }]; } }   /* xtfix2 */
        if (!own && g_xtfix_surfs.count(gs.get())) {   /* xtfix: on the new surfaces the pcurve shares the edge's parameter by construction
                                                           (interp_pcurve; a GeomProjLib projection may be parametrised otherwise, which
                                                           BRepCheck / ShapeFix then reject as self-intersecting wires) */
            pc = interp_pcurve(gc, gs, t0, t1, tol / 4); own = !pc.IsNull();
            if (own) { why.clear(); projected = true; }
        }
        if (!own) { double tu = tol; try { pc = GeomProjLib::Curve2d(gc, t0, t1, gs, tu); } catch (...) { pc.Nullify(); } }
        if (pc.IsNull() && !own) pc = interp_pcurve(gc, gs, t0, t1, tol / 4);   /* xtfix: the projection failed (ours, as below) */
        if (!pc.IsNull()) { why.clear(); projected = true; }
    }
    if (pc.IsNull() && (why == "line not in its face's plane" || why == "circle not in its face's plane" || why == "circle on a cylinder not coaxial" ||
                        why == "line on a cylinder not parallel to its axis") && !getenv("XTOCC_STRICT_ANALYTIC")) {
        /* xtfix2: the analytic rule's fixed angle test (1e-6 rad) failed - Parasolid's data is exact only to its resolution (e.g. WCS_EX 0.mcam
           sld_12 edge 5837: a line 1.13e-6 rad off its plane, 5e-9 m at its point). The curve's own points' surface parameters
           (interp_pcurve, the same parameter as the curve) and the measured curve-surface gap below decides against the edge's tolerance
           (ref p.21: the curve lies in the surfaces "to within modeller resolution", or the edge is tolerant) */
        Handle(Geom2d_Curve) q = interp_pcurve(gc, gs, t0, t1, tol / 4);
        if (!q.IsNull() && ushift != 0) q = Handle(Geom2d_Curve)::DownCast(q->Translated(gp_Vec2d(ushift, 0)));   /* the caller's period shift */
        if (!q.IsNull()) { pc = q; why.clear(); }
    }
    if (pc.IsNull() && g_xtfix_surfs.count(gs.get()) && g_interp_gap > 0) {   /* xtfix2: say why (was "no pcurve rule ...") */
        char b[200]; snprintf(b, sizeof b, "edge curve not on the built surface (a curve point %.3g from it; e.g. a support evaluated beyond its range, formats/xt_occt_fixes.md 7)", g_interp_gap);
        why = b;
    }
    if (pc.IsNull()) return pc;
    double gap = 0;
    for (int k = 0; k <= 16; k++) {
        double t = t0 + (t1 - t0) * k / 16;
        gp_Pnt2d uv = pc->Value(t);
        gp_Pnt q = gs->Value(uv.X(), uv.Y());
        if (projected) {   /* 3drough: a projected / SP pcurve need not share the 3D curve's parameter (SameParameter fixes it at the
                              end): the distance of its points to the curve */
            GeomAPI_ProjectPointOnCurve pr(q, gc);
            gap = std::max(gap, std::min(gc->Value(t).Distance(q), pr.NbPoints() ? pr.LowerDistance() : 1e300));
        } else gap = std::max(gap, gc->Value(t).Distance(q));
    }
    if (finpc && gap > tol && gap <= 10 * std::max(tol, vt)) {   /* xtfix2: a tolerant edge's stored fin curve is that face's boundary by definition (ref p.21;
            its ends were checked against the vertices, fin_pcurves). Measured, stored fin curves can be further apart than EDGE.tolerance (ref
            p.21 says they may not; TUTORIAL 4 IMPELLER blades 1.02 - 1.9 x, HST DYNAMIC OPTIROUGH sheet 3.6 x - Parasolid apparently checks
            at sample points only): the OCCT edge takes the measured separation as its tolerance on this face (BRepLib::SameParameter
            refines it); the ratio is reported (Result::accuracy). Near a tolerant vertex the two curves only have to end within its tolerance
            (HST DYNAMIC OPTIROUGH edge 1874: 11 x the edge tolerance at its end). Ours: 10 x the larger of the edge's and its vertices'
            tolerances is only a gross-error guard */
        if (ushift == 0) g_fin_ratio = std::max(g_fin_ratio, gap / tol);
        g_pc_tol = gap * 1.001;
        if (getenv("XTOCC_DEBUG")) fprintf(stderr, "fin pcurve gap %g tol %g (accepted)\n", gap, tol);
        g_max_cs_gap = std::max(g_max_cs_gap, gap);
        return pc;
    }
    if (finpc && ushift == 0) g_fin_ratio = std::max(g_fin_ratio, gap / tol);   /* (not the cylinder's period-shifted re-call, made with the resolution) */
    if (finpc && getenv("XTOCC_DEBUG")) fprintf(stderr, "fin pcurve gap %g tol %g\n", gap, tol);
    if (gap > tol && projected && !finpc) {   /* xtfix (ours): the projection is too coarse (e.g. an ellipse on a cylinder) - a 2D B-spline interpolating the
                                       exact surface parameters of curve points at the curve's own parameter, refined until surface(pcurve(t)) is
                                       within tol / 4 of curve(t) at the interval midpoints */
        Handle(Geom2d_Curve) q = interp_pcurve(gc, gs, t0, t1, tol / 4);
        if (!q.IsNull()) {
            double g2 = 0;
            for (int k = 0; k <= 64; k++) { double t = t0 + (t1 - t0) * k / 64; gp_Pnt2d uv = q->Value(t); g2 = std::max(g2, gc->Value(t).Distance(gs->Value(uv.X(), uv.Y()))); }
            if (g2 < gap) { pc = q; gap = g2; }
        }
    }
    g_max_cs_gap = std::max(g_max_cs_gap, gap);
    if (gap > tol) {
        char b[160]; snprintf(b, sizeof b, "edge curve leaves its face's surface by %.3g (> tolerance %.3g)", gap, tol);
        why = b; return nullptr;
    }
    return pc;
}

}  // namespace

static bool build_impl(const Model &m, double S, Result &r);
bool build(const Model &m, double S, Result &r) {
    g_timed_out = false;
    bool ok = false;
    try { ok = build_impl(m, S, r); } catch (...) { ok = false; if (r.error.empty()) r.error = "OCCT exception"; }
    if (g_prof) for (auto &kv : g_prof_t) fprintf(stderr, "prof %-60s calls %8ld  %.2f s\n", kv.first.c_str(), kv.second.first, kv.second.second);
    if (g_timed_out) {
        r.error = "time limit exceeded fitting the blend / intersection geometry (XTOCC_TIME_LIMIT, default 120 s)";
        r.shape.Nullify(); return false;
    }
    return ok;
}
static bool build_impl(const Model &m, double S, Result &r) {
    for (size_t i = 0; i < m.by_idx.size(); i++)
        if (m.by_idx[i].p) r.counts[m.s->nd[m.by_idx[i].type].name]++;
    const Node *body = nullptr;
    for (size_t i = 0; i < m.by_idx.size() && !body; i++) if (m.by_idx[i].p && m.is(&m.by_idx[i], "BODY")) body = &m.by_idx[i];
    if (!body) { r.error = "no BODY"; return false; }
    g_max_cs_gap = 0; g_fin_ratio = 0; g_new_types = false; g_sp_pc = nullptr; g_fin_pc = nullptr; g_fin_vt = nullptr; g_xtfix_surfs.clear();
    Ctx c(m, S, r);
    double res = m.dbl(body, "res_linear");               /* Parasolid's linear resolution (BODY.res_linear, metres) */
    c.tol = (res > 0 && res < 1e-3 ? res : 1e-8) * S;
#ifdef XTOCC_MUTATE_LOOSE           /* test hook (ours): loose tolerance so a mutant builds and reaches the metrics */
    c.tol = 0.01;
#endif
    r.tol = c.tol;

    /* surfaces */
    std::map<int, Handle(Geom_Surface)> surf;
    std::map<int, int> ssense;
    std::vector<int> faces;
    c.surfs = &surf; c.fit_tol = 1e-7 * S; c.accept_tol = 1e-5 * S;
    /* xtfix: what the blends must cover - the stored points of the INTERSECTION curves on them (charts, limits) and their faces'
       vertices; faces with ring loops (formats/xt_occt_fixes.md) */
    for (size_t i = 0; i < m.by_idx.size(); i++) {
        const Node *n = m.by_idx[i].p ? &m.by_idx[i] : nullptr;
        if (m.is(n, "INTERSECTION")) {
            std::vector<gp_Pnt> P;
            for (const char *f : { "chart", "start", "end" }) { const Node *h = m.pnode(n, f); if (h) for (uint32_t k = 0; k < h->count; k++) P.push_back(c.hvec(h, (int)k)); }
            for (int k = 0; k < 2; k++) {
                int sk = m.ptr(n, "surface", k); const Node *sn = m.node(sk);
                if (m.is(sn, "BLEND_BOUND")) sk = m.ptr(sn, "blend");
                auto &D = c.demand[sk]; D.insert(D.end(), P.begin(), P.end());
            }
        } else if (m.is(n, "BLENDED_EDGE")) {   /* ref p.62: LIMITs of a degenerate periodic blend bound its spine */
            int sp = m.ptr(n, "spine"); if (m.is(m.node(sp), "INTERSECTION")) c.spine_ext[sp] = std::max(c.spine_ext[sp], std::abs(m.dbl(n, "range", 0)) * S);
            for (const char *f : { "start", "end" }) { const Node *h = m.pnode(n, f); if (h) for (uint32_t k = 0; k < h->count; k++) c.demand[(int)i].push_back(c.hvec(h, (int)k)); }
        } else if (m.is(n, "FACE")) {
            int si = m.ptr(n, "surface");
            c.faces_of_surf[si].push_back((int)i);
            for (const Node *lp = m.pnode(n, "loop"); lp; lp = m.pnode(lp, "next")) {
                const Node *h0 = m.pnode(lp, "halfedge"), *h = h0; int g = 0;
                do { const Node *v = m.pnode(h, "vertex"), *pt = v ? m.pnode(v, "point") : nullptr;
                     if (pt) c.demand[si].push_back(c.pnt(m.vec(pt, "pvec"))); else c.ring_surf.insert(si);
                     h = m.pnode(h, "forward"); } while (h && h != h0 && ++g < 100000);
            }
        }
    }
    /* xtfix: the analytic edges (LINE / CIRCLE / ELLIPSE) of blend faces, sampled along the arc the edge really takes (a face can run the
       long way round a periodic spine) - into the blend's demand */
    for (auto &kv : c.faces_of_surf) {
        if (!m.is(m.node(kv.first), "BLENDED_EDGE")) continue;
        for (int fi : kv.second)
            for (const Node *lp = m.pnode(m.node(fi), "loop"); lp; lp = m.pnode(lp, "next")) {
                const Node *h0 = m.pnode(lp, "halfedge"), *h = h0; int g = 0;
                do {
                    int ei = m.ptr(h, "edge"); const Node *cv = m.pnode(m.node(ei), "curve");
                    if ((m.is(cv, "LINE") || m.is(cv, "CIRCLE") || m.is(cv, "ELLIPSE")) && m.ptr(h, "vertex") > 1) {
                        bool ok = false; std::string e0 = r.error;
                        try { ok = c.make_edge(ei); } catch (...) {}
                        r.error = e0;
                        if (ok) { double t0 = c.erange[ei].first, t1 = c.erange[ei].second;
                            for (int k = 0; k <= 16; k++) c.demand[kv.first].push_back(c.ecurve[ei]->Value(t0 + (t1 - t0) * k / 16)); }
                    }
                    h = m.pnode(h, "forward");
                } while (h && h != h0 && ++g < 100000);
            }
    }
    /* xtfix: the blends' spines + their ball centres into their blend supports' demand, supported blends first (Ctx::propagate) */
    {
        std::vector<int> order; std::set<int> seen;
        std::function<void(int)> visit = [&](int bi) {   /* post-order on "is supported by": supports after the blends they support */
            if (!seen.insert(bi).second) return;
            const Node *n = m.node(bi);
            for (size_t i = 0; i < m.by_idx.size(); i++) {
                const Node *a = m.by_idx[i].p ? &m.by_idx[i] : nullptr;
                if (m.is(a, "BLENDED_EDGE") && (m.ptr(a, "surface", 0) == bi || m.ptr(a, "surface", 1) == bi)) visit((int)i);
            }
            (void)n; order.push_back(bi);
        };
        for (size_t i = 0; i < m.by_idx.size(); i++) if (m.by_idx[i].p && m.is(&m.by_idx[i], "BLENDED_EDGE")) visit((int)i);
        for (int bi : order) {   /* every supported blend of bi is earlier in order */
            const Node *sp = m.pnode(m.node(bi), "spine"); Handle(Geom_Curve) gc; Handle(Geom2d_Curve) pc; Handle(Geom_Surface) ps; bool t = false; double a, b;
            try { if (sp && c.any_curve(sp, gc, pc, ps, t, a, b, false) && !gc.IsNull() && !t) c.propagate(bi, gc); } catch (...) {}
        }
    }
    for (size_t i = 0; i < m.by_idx.size(); i++) {
        const Node *n = m.by_idx[i].p ? &m.by_idx[i] : nullptr;
        if (!m.is(n, "FACE")) continue;
        faces.push_back((int)i);
        const Node *sn = m.pnode(n, "surface");
        int si = m.ptr(n, "surface");
        if (!sn) { r.error = "face without surface"; return false; }
        if (ssense.count(si)) continue;
        Handle(Geom_Surface) g;
        try { g = c.surface(si); } catch (...) { g.Nullify(); c.refuse(si, "OCCT exception"); }
        if (g.IsNull()) { ssense[si] = 1; continue; }
        if (c.newsurf) g_new_types = true;
        ssense[si] = m.chr(sn, "sense") == '-' ? -1 : 1;
#ifdef XTOCC_MUTATE_SURFACE_SENSE   /* test hook (ours) */
        ssense[si] = 1;
#endif
    }
    /* edges */
    g_sp_pc = &c.sp_pc; g_fin_pc = &c.fin_pc; g_fin_vt = &c.fin_vt;
    std::vector<int> edges;
    for (size_t i = 0; i < m.by_idx.size(); i++)
        if (m.by_idx[i].p && m.is(&m.by_idx[i], "EDGE")) {
            edges.push_back((int)i);
            bool ok = false;
            try { ok = c.make_edge((int)i); } catch (...) { r.error = "edge " + std::to_string(i) + ": OCCT exception"; }
            if (!ok && !r.error.empty()) return false;
        }
    if (c.newsurf) g_new_types = true;
    if (!r.unmapped.empty()) { r.error = "unmapped entity types"; return false; }
    if (c.newtypes) g_new_types = true;

    /* loops in the face's TShape: wires are oriented for the face's own surface (FORWARD); a face whose sensed
       normal is opposite to the surface's natural normal is used REVERSED, its fins reversed inside the TShape (the
       traversal order is then the reverse of the fin order). */
    struct TF { int ei; TopAbs_Orientation o; };
    struct LoopT { std::vector<TF> seq; bool ring = false, wraps = false; double du = 0, h = 0; };
    auto face_eff = [&](const Node *fn) { return ssense[m.ptr(fn, "surface")] * (m.chr(fn, "sense") == '-' ? -1 : 1); };
    auto tshape_loops = [&](const Node *fn, std::vector<LoopT> &out) -> bool {
        int eff = face_eff(fn);
        Handle(Geom_Surface) gs = surf[m.ptr(fn, "surface")];
        bool cyl = !Handle(Geom_CylindricalSurface)::DownCast(gs).IsNull();
        for (const Node *lp = m.pnode(fn, "loop"); lp; lp = m.pnode(lp, "next")) {
            LoopT L;
            const Node *h0 = m.pnode(lp, "halfedge"), *h = h0;
            int guard = 0;
            if (h0 && m.ptr(h0, "edge") <= 1 && m.pnode(h0, "forward") == h0) continue;   /* xtfix2: an isolated loop (ref p.21 / p.91: a single
                vertex, "a 0-dimensional hole in the face", its fin has no edge) - e.g. a pole of a B-surface; OCCT has no point loop and the
                face needs none (the point is on its surface) */
            do {
                int ei = m.ptr(h, "edge");
                if (!c.edge.count(ei)) { r.error = "fin on an unbuilt edge"; return false; }
                bool fwd = m.chr(h, "sense") != '-';
                L.seq.push_back({ei, (fwd == (eff > 0)) ? TopAbs_FORWARD : TopAbs_REVERSED});
                h = m.pnode(h, "forward");
            } while (h && h != h0 && ++guard < 100000);
            if (eff < 0) std::reverse(L.seq.begin(), L.seq.end());
            if (cyl) {      /* winding round the axis: sum of u changes along the TShape traversal */
                std::string why;
                for (auto &t : L.seq) {
                    double etl = c.tol;   /* 3drough: a tolerant edge of a body with the new types (as `put` below) */
                    if (g_new_types) { double et = m.dbl(m.node(t.ei), "tolerance"); if (et > 0) etl = std::max(etl, et * c.S); etl = std::max(etl, 1e-6 * c.S + 2 * c.max_fit); }   /* ours: >= 1 micron (+ twice the fits' deviation, xtfix) */
                    Handle(Geom2d_Curve) pc = pcurve(c.ecurve[t.ei], gs, 0, etl, why, c.erange[t.ei].first, c.erange[t.ei].second);
                    if (pc.IsNull() && getenv("XTOCC_DEBUG")) fprintf(stderr, "cyl face edge %d curve %s etl %g: %s\n", t.ei, m.tname(m.pnode(m.node(t.ei), "curve")), etl, why.c_str());
                    if (pc.IsNull()) { r.error = "face: " + why; return false; }
                    double t0 = c.erange[t.ei].first, t1 = c.erange[t.ei].second;
                    double us = pc->Value(t.o == TopAbs_FORWARD ? t0 : t1).X(), ue = pc->Value(t.o == TopAbs_FORWARD ? t1 : t0).X();
                    L.du += ue - us;
                    L.h = pc->Value(t0).Y();
                }
                L.wraps = std::abs(L.du) > M_PI;
                L.ring = L.seq.size() == 1 && c.erange[L.seq[0].ei].second - c.erange[L.seq[0].ei].first > 2 * M_PI - 1e-12
                     && !Handle(Geom_Circle)::DownCast(c.ecurve[L.seq[0].ei]).IsNull();   /* xtfix: a ring of a circle (a closed INTERSECTION /
                                                                                              ELLIPSE ring has no seam rule here: ShapeFix's) */
            }
            out.push_back(L);
        }
        return true;
    };
    auto tstart = [&](const TF &t) {             /* TShape start vertex of an oriented edge */
        TopoDS_Vertex a, b; TopExp::Vertices(c.edge[t.ei], a, b);
        return t.o == TopAbs_FORWARD ? a : b;
    };
    /* ours: a cylinder face whose two loops wrap round the axis needs a seam in OCCT (Parasolid has none). The seam is
       an axial line through a vertex V: both rings -> the cylinder's u = 0 line; a ring + a loop with vertices -> the
       loop's vertex nearest (in height) to the ring, and the ring edge's vertex is moved there. */
    std::map<int, gp_Pnt> seam_at;               /* face -> seam point on the lower / any wrapping loop */
    bool moved = false;
    for (int fi : faces) {
        const Node *fn = m.node(fi);
        auto cy = Handle(Geom_CylindricalSurface)::DownCast(surf[m.ptr(fn, "surface")]);
        if (cy.IsNull()) continue;
        std::vector<LoopT> Ls;
        if (!tshape_loops(fn, Ls)) return false;
        std::vector<LoopT *> w;
        for (auto &L : Ls) if (L.wraps) w.push_back(&L);
        if (w.size() != 2) { if (!w.empty()) r.notes.push_back("face " + std::to_string(fi) + ": " + std::to_string(w.size()) + " wrapping loops"); continue; }
        LoopT *ring = w[0]->ring ? w[0] : (w[1]->ring ? w[1] : nullptr), *other = ring == w[0] ? w[1] : w[0];
        if (!ring) continue;    /* two vertex loops: seam only if they share a u (checked at build) */
        gp_Pnt V;
        if (other->ring) {      /* both rings: the seam on the cylinder's u = 0 line (its x_axis) */
            gp_Circ C = Handle(Geom_Circle)::DownCast(c.ecurve[other->seq[0].ei])->Circ();
            V = C.Location().Translated(gp_Vec(cy->Position().XDirection()) * C.Radius());
            int eo = other->seq[0].ei;
            auto it = c.ring_at.find(eo);
            if (it != c.ring_at.end() && it->second.Distance(V) > c.tol) { r.notes.push_back("ring edge " + std::to_string(eo) + " needed by two seams"); continue; }
            c.ring_at[eo] = V; moved = true;
        }
        double best = 1e300;
        if (!other->ring)
        for (auto &t : other->seq) {
            gp_Pnt p = BRep_Tool::Pnt(tstart(t));
            double hp = gp_Vec(cy->Position().Location(), p).Dot(cy->Position().Direction());
            if (std::abs(hp - ring->h) < best) { best = std::abs(hp - ring->h); V = p; }
        }
        seam_at[fi] = V;
        /* the ring's vertex on the seam: the circle point at V's angle */
        gp_Ax3 ax = cy->Position();
        gp_Vec d(ax.Location(), V);
        gp_Vec rad = d - gp_Vec(ax.Direction()) * d.Dot(gp_Vec(ax.Direction()));
        gp_Circ C = Handle(Geom_Circle)::DownCast(c.ecurve[ring->seq[0].ei])->Circ();
        gp_Pnt at = C.Location().Translated(rad.Normalized() * C.Radius());
        int ei = ring->seq[0].ei;
        auto it = c.ring_at.find(ei);
        if (it != c.ring_at.end() && it->second.Distance(at) > c.tol) { r.notes.push_back("ring edge " + std::to_string(ei) + " needed by two seams"); continue; }
        c.ring_at[ei] = at; moved = true;
    }
    if (moved) for (auto &kv : c.ring_at) if (!c.make_edge(kv.first)) return false;   /* rebuild moved rings */

    /* faces */
    Handle(ShapeBuild_ReShape) reshape = new ShapeBuild_ReShape;
    std::map<int, TopoDS_Face> rawface;
    for (int fi : faces) {
        const Node *fn = m.node(fi);
        Handle(Geom_Surface) gs = surf[m.ptr(fn, "surface")];
        int eff = face_eff(fn);
        auto cy = Handle(Geom_CylindricalSurface)::DownCast(gs);
        TopoDS_Face F;
        c.B.MakeFace(F, gs, c.tol);
        std::vector<LoopT> Ls;
        if (!tshape_loops(fn, Ls)) return false;
        std::string why;
        /* pcurves of a TShape-ordered fin sequence, continuous in u from ustart (NaN = as computed) */
        auto put = [&](const std::vector<TF> &seq, double ustart, TopoDS_Wire *W) {
            double uprev = ustart;
            for (auto &t : seq) {
                double etl = c.tol;   /* 3drough: a tolerant edge of a body with the new types: its own tolerance */
                if (g_new_types) { double et = c.m.dbl(c.m.node(t.ei), "tolerance"); if (et > 0) etl = std::max(etl, et * c.S); etl = std::max(etl, 1e-6 * c.S + 2 * c.max_fit); }   /* ours: >= 1 micron (+ twice the fits' deviation, xtfix) */
                Handle(Geom2d_Curve) pc = pcurve(c.ecurve[t.ei], gs, 0, etl, why, c.erange[t.ei].first, c.erange[t.ei].second);
                if (pc.IsNull() && getenv("XTOCC_DEBUG")) fprintf(stderr, "face %d edge %d curve %s surface %s etl %g: %s\n", fi, t.ei,
                    c.m.tname(c.m.pnode(c.m.node(t.ei), "curve")), c.m.tname(c.m.pnode(fn, "surface")), etl, why.c_str());
                double t0 = c.erange[t.ei].first, t1 = c.erange[t.ei].second;
                if (pc.IsNull()) { if (why.empty()) why = "no pcurve"; return; }   /* xtfix: guard (the error is reported below) */
                if (!cy.IsNull()) {
                    double us = pc->Value(t.o == TopAbs_FORWARD ? t0 : t1).X();
                    if (!std::isnan(uprev)) {
                        double k = std::round((uprev - us) / (2 * M_PI));
                        if (k != 0) {
                            Handle(Geom2d_Curve) p0 = pc; std::string w0 = why;
                            pc = pcurve(c.ecurve[t.ei], gs, 2 * M_PI * k, c.tol, why, c.erange[t.ei].first, c.erange[t.ei].second);
                            if (pc.IsNull() && g_new_types) {   /* xtfix: a projected pcurve (no analytic rule): translated by the period */
                                pc = Handle(Geom2d_Curve)::DownCast(p0->Translated(gp_Vec2d(2 * M_PI * k, 0))); why = w0; }
                        }
                    }
                    if (pc.IsNull()) { if (why.empty()) why = "no pcurve"; return; }
                    uprev = pc->Value(t.o == TopAbs_FORWARD ? t1 : t0).X();
                }
                c.B.UpdateEdge(c.edge[t.ei], pc, F, std::max(etl, g_pc_tol));   /* xtfix2: a stored fin curve's measured tolerance */
                if (W) c.B.Add(*W, c.edge[t.ei].Oriented(t.o));
            }
        };
        std::vector<LoopT *> w;
        for (auto &L : Ls) if (L.wraps) w.push_back(&L);
        /* xtfix: a face on a new periodic surface (cone / sphere / torus / periodic blend / offset of one): its pcurves are projections,
           the seam OCCT needs comes from ShapeFix_Face::FixMissingSeam (ours, as the cylinder fallback below) */
        bool newper = Handle(Geom_Plane)::DownCast(gs).IsNull() && cy.IsNull() && (gs->IsUPeriodic() || gs->IsVPeriodic() || gs->IsUClosed());
        bool seamed = false, fallback = newper;
        if (w.size() == 2) {
            LoopT *lo = w[0], *hi = w[1];
            if (lo->h > hi->h) std::swap(lo, hi);
            if (getenv("XTOCC_DEBUG")) fprintf(stderr, "face %d band: lo du %g h %g (%zu edges) hi du %g h %g (%zu edges)\n", fi, lo->du, lo->h, lo->seq.size(), hi->du, hi->h, hi->seq.size());
            if (!(lo->du > 0 && hi->du < 0)) { r.error = "face " + std::to_string(fi) + ": wrapping loops do not bound a band"; return false; }
            /* seam vertices: the lower and upper loop's vertices at the same u */
            gp_Ax3 ax = cy->Position();
            auto uof = [&](const gp_Pnt &p) { gp_Vec d(ax.Location(), p); return atan2(d.Dot(ax.YDirection()), d.Dot(ax.XDirection())); };
            int ilo = -1, ihi = -1;
            double useam = 0;
            for (size_t a = 0; a < lo->seq.size() && ilo < 0; a++)
                for (size_t b = 0; b < hi->seq.size(); b++) {
                    gp_Pnt pa = BRep_Tool::Pnt(tstart(lo->seq[a])), pb = BRep_Tool::Pnt(tstart(hi->seq[b]));
                    double du = std::remainder(uof(pa) - uof(pb), 2 * M_PI);
                    if (std::abs(du) * cy->Radius() < c.tol) {
                        if (seam_at.count(fi) && seam_at[fi].Distance(pa) > c.tol && seam_at[fi].Distance(pb) > c.tol) continue;
                        ilo = (int)a; ihi = (int)b; useam = uof(pa); break;
                    }
                }
            if (ilo >= 0) {
                std::rotate(lo->seq.begin(), lo->seq.begin() + ilo, lo->seq.end());
                std::rotate(hi->seq.begin(), hi->seq.begin() + ihi, hi->seq.end());
                TopoDS_Vertex vlo = tstart(lo->seq[0]), vhi = tstart(hi->seq[0]);
                gp_Pnt plo = BRep_Tool::Pnt(vlo), phi = BRep_Tool::Pnt(vhi);
                gp_Dir sd(gp_Vec(plo, phi));
                TopoDS_Edge seam;
                c.B.MakeEdge(seam, new Geom_Line(plo, sd), c.tol);
                c.B.Add(seam, vlo.Oriented(TopAbs_FORWARD));
                c.B.Add(seam, vhi.Oriented(TopAbs_REVERSED));
                c.B.Range(seam, 0, plo.Distance(phi));
                double v0 = gp_Vec(ax.Location(), plo).Dot(ax.Direction()), dv = sd.Dot(ax.Direction()) > 0 ? 1 : -1;
                TopoDS_Wire W;
                c.B.MakeWire(W);
                /* lower loop +u from useam to useam + 2 pi, seam up at useam + 2 pi, upper loop -u back to useam,
                   seam down at useam: counter-clockwise in (u, v), the face on the left (OCCT's rule) */
                put(lo->seq, useam, &W);
                c.B.Add(W, seam.Oriented(TopAbs_FORWARD));
                put(hi->seq, useam + 2 * M_PI, &W);
                c.B.Add(W, seam.Oriented(TopAbs_REVERSED));
                c.B.UpdateEdge(seam, new Geom2d_Line(gp_Pnt2d(useam + 2 * M_PI, v0), gp_Dir2d(0, dv)),
                               new Geom2d_Line(gp_Pnt2d(useam, v0), gp_Dir2d(0, dv)), F, c.tol);
                W.Closed(true);
                c.B.Add(F, W);
                seamed = true;
                r.seams_added++;
            } else fallback = true;
        } else if (!w.empty()) fallback = true;
        for (auto &L : Ls) {
            if (seamed && L.wraps) continue;
            TopoDS_Wire W;
            c.B.MakeWire(W);
            put(L.seq, NAN, &W);
            W.Closed(true);
            c.B.Add(F, W);
        }
        if (!why.empty()) { r.error = "face " + std::to_string(fi) + " (" + m.tname(m.pnode(fn, "surface")) + "): " + why; return false; }
        F.Orientation(eff > 0 ? TopAbs_FORWARD : TopAbs_REVERSED);
        if (fallback) {        /* ours: no common seam vertex -> ShapeFix's missing-seam fix (may split edges) */
            ShapeFix_Face sff;
            sff.Init(F);
            sff.SetContext(reshape);
            sff.SetPrecision(c.tol);
            if (sff.FixMissingSeam()) {
                r.seams_added++;
                r.notes.push_back("face " + std::to_string(fi) + ": seam added by ShapeFix_Face::FixMissingSeam");
                F = sff.Face();
            } else if (!newper) r.notes.push_back("face " + std::to_string(fi) + ": missing seam could not be added");
        }
        {   /* xtfix (ours): a face on a CONE / SPHERE / TORUS / OFFSET_SURF / BLENDED_EDGE that BRepCheck rejects (typically a loop round a
               sphere's / cone's pole, or projected pcurves a period apart) -> ShapeFix_Face (wires, seams, degenerate pole edges, wire
               orientation); only those faces, so the plane / cylinder / B-surface translation is unchanged */
            const Node *sn = m.pnode(fn, "surface");
            if (m.is(sn, "CONE") || m.is(sn, "SPHERE") || m.is(sn, "TORUS") || m.is(sn, "OFFSET_SURF") || m.is(sn, "BLENDED_EDGE") || m.is(sn, "SWEPT_SURF") || m.is(sn, "SPUN_SURF")
                || (m.is(sn, "B_SURFACE") && (gs->IsUPeriodic() || gs->IsVPeriodic()))) {   /* xtfix2: + swept / spun / periodic B-surfaces */
                bool valid = false; try { valid = BRepCheck_Analyzer(F).IsValid(); } catch (...) {}
                if (!valid) {
                    try { ShapeFix_Face sff; sff.Init(F); sff.SetContext(reshape); sff.SetPrecision(c.tol); sff.SetMaxTolerance(1e-5 * c.S); sff.Perform();
                          F = sff.Face(); r.notes.push_back("face " + std::to_string(fi) + " (" + m.tname(sn) + "): healed by ShapeFix_Face"); }
                    catch (...) { r.notes.push_back("face " + std::to_string(fi) + ": ShapeFix_Face failed"); }
                    bool v2 = false; try { v2 = BRepCheck_Analyzer(F).IsValid(); } catch (...) {}
                    if (!v2) {   /* still invalid (projected pcurves across a degenerate blend section / pole): the pcurves dropped and
                                    recomputed by ShapeFix (ShapeConstruct_ProjectCurveOnSurface handles singular surfaces) */
                        try {
                            TopoDS_Face F2 = TopoDS::Face(F.EmptyCopied()); F2.Orientation(TopAbs_FORWARD);
                            TopoDS_Face Ff = TopoDS::Face(F.Oriented(TopAbs_FORWARD));
                            for (TopExp_Explorer ew(Ff, TopAbs_WIRE); ew.More(); ew.Next()) {
                                TopoDS_Wire W2; c.B.MakeWire(W2);
                                for (TopExp_Explorer ee(ew.Current(), TopAbs_EDGE); ee.More(); ee.Next()) {
                                    TopoDS_Edge E = TopoDS::Edge(ee.Current());
                                    c.B.UpdateEdge(E, Handle(Geom2d_Curve)(), Ff, BRep_Tool::Tolerance(E));
                                    c.B.Add(W2, E);
                                }
                                c.B.Add(F2, W2);
                            }
                            F2.Orientation(F.Orientation());
                            ShapeFix_Face sf2; sf2.Init(F2); sf2.SetContext(reshape); sf2.SetPrecision(c.tol); sf2.SetMaxTolerance(1e-5 * c.S); sf2.Perform();
                            TopoDS_Face F3 = sf2.Face(); bool v3 = BRepCheck_Analyzer(F3).IsValid();
                            if (v3) { F = F3; v2 = true; r.notes.push_back("face " + std::to_string(fi) + ": pcurves recomputed by ShapeFix"); }
                        } catch (...) {}
                    }
                    c.dev(std::string("faces healed by ShapeFix_Face (count; still invalid: max 1) on ") + m.tname(sn), v2 ? 0 : 1);
                }
            }
        }
        rawface[fi] = F;
    }
    r.max_curve_surface_gap = g_max_cs_gap;
    if (g_fin_ratio > 0) c.dev("tolerant edges: stored fin curves' separation / EDGE.tolerance (ratio)", g_fin_ratio);
    for (auto &kv : rawface) kv.second = TopoDS::Face(reshape->Apply(kv.second));
    for (auto &kv : c.edge) {
        TopoDS_Shape s = reshape->Apply(kv.second);
        r.edge_of[kv.first] = s.ShapeType() == TopAbs_EDGE ? TopoDS::Edge(s) : kv.second;
        if (s.ShapeType() != TopAbs_EDGE) r.notes.push_back("edge " + std::to_string(kv.first) + " split by the seam fix");
    }

    /* regions -> solids: each solid region's shells; FACE.shell = the shell behind the face (normal points out of
       its region), FACE.front_shell = the shell in front (face reversed for that shell) */
    TopoDS_Compound comp;
    c.B.MakeCompound(comp);
    int nsolid = 0;
    for (const Node *rg = m.pnode(body, "region"); rg; rg = m.pnode(rg, "next")) {
        if (m.chr(rg, "type") != 'S') continue;
        TopoDS_Solid So;
        c.B.MakeSolid(So);
        for (const Node *sh = m.pnode(rg, "shell"); sh; sh = m.pnode(sh, "next")) {
            TopoDS_Shell Sh;
            c.B.MakeShell(Sh);
            int nf = 0;
            int shi = (int)(sh - &m.by_idx[0]);
            for (int fi = m.ptr(sh, "face"); m.node(fi); fi = m.ptr(m.node(fi), "next")) {
                if (!rawface.count(fi)) break;
                if (m.ptr(m.node(fi), "shell") != shi) { r.notes.push_back("FACE.next left its shell"); break; }
                c.B.Add(Sh, rawface[fi]); r.face_of[fi] = rawface[fi]; nf++;
                if (nf > 100000) break;
            }
            for (int fi = m.ptr(sh, "front_face"); m.node(fi); fi = m.ptr(m.node(fi), "next_front")) {
                if (!rawface.count(fi)) break;
                if (m.ptr(m.node(fi), "front_shell") != shi) { r.notes.push_back("FACE.next_front left its shell"); break; }
                TopoDS_Face Fr = TopoDS::Face(rawface[fi].Reversed());
                c.B.Add(Sh, Fr); r.face_of[fi] = Fr; nf++;
                if (nf > 100000) break;
            }
            Sh.Closed(BRep_Tool::IsClosed(Sh));
            c.B.Add(So, Sh);
        }
        c.B.Add(comp, So);
        nsolid++;
    }
    if (!nsolid && m.chr(body, "body_type") == 3 && !getenv("XTOCC_NO_SHEETS")) {
        /* xtfix2: a sheet body (BODY.body_type SCH_sheet_body = 3, ref p.86; "All the regions of a sheet body must be void", ref p.25):
           its faces as open shells, one per XT shell, each face once as listed in FACE.shell's face list (its own orientation); the
           drive mesh needs the faces only. E.g. the 2017 Multiaxis tutorial impeller blades. */
        std::set<int> seen;
        for (const Node *rg = m.pnode(body, "region"); rg; rg = m.pnode(rg, "next"))
            for (const Node *sh = m.pnode(rg, "shell"); sh; sh = m.pnode(sh, "next")) {
                TopoDS_Shell Sh; c.B.MakeShell(Sh); int nf = 0, shi = (int)(sh - &m.by_idx[0]);
                for (int fi = m.ptr(sh, "face"); m.node(fi) && nf <= 100000; fi = m.ptr(m.node(fi), "next")) {
                    if (!rawface.count(fi) || m.ptr(m.node(fi), "shell") != shi) break;
                    if (!seen.insert(fi).second) continue;
                    c.B.Add(Sh, rawface[fi]); r.face_of[fi] = rawface[fi]; nf++;
                }
                for (int fi = m.ptr(sh, "front_face"); m.node(fi) && nf <= 100000; fi = m.ptr(m.node(fi), "next_front")) {
                    if (!rawface.count(fi) || m.ptr(m.node(fi), "front_shell") != shi) break;
                    if (!seen.insert(fi).second) continue;
                    c.B.Add(Sh, rawface[fi]); r.face_of[fi] = rawface[fi]; nf++;
                }
                if (nf) { c.B.Add(comp, Sh); nsolid++; }
            }
        if (nsolid) r.notes.push_back("sheet body: " + std::to_string(nsolid) + " open shell(s)");
    }
    if (!nsolid) { r.error = "no solid region"; return false; }
    if (r.face_of.size() != rawface.size()) r.notes.push_back("faces not reached from a solid region's shells: " +
                                                             std::to_string(rawface.size() - r.face_of.size()));
    if (g_new_types) {   /* 3drough (ours): the projected / SP pcurves to one parameter with their 3D curves, then OCCT's shape healing
                            (wires / pcurves / tolerances of the B-surface faces; face_of keeps the faces before it) */
        try { BRepLib::SameParameter(comp, c.tol, Standard_True); } catch (...) { r.notes.push_back("SameParameter failed"); }
        try { ShapeFix_Shape sfs(comp); sfs.SetPrecision(c.tol); sfs.SetMaxTolerance(1e-2 * c.S / 1000); sfs.Perform();
              TopoDS_Shape fixed = sfs.Shape(); if (!fixed.IsNull()) { TopoDS_Compound cc; c.B.MakeCompound(cc); c.B.Add(cc, fixed); comp = cc; } }
        catch (...) { r.notes.push_back("ShapeFix_Shape failed"); }
    }
    r.new_geometry = !g_xtfix_surfs.empty() || !c.icurves.empty();
    if (r.check) {   /* xtfix: every edge's curve sampled -> the surfaces of the faces of its fins; every vertex -> its edges' curves */
        for (auto &kv : c.edge) {
            const Node *e = m.node(kv.first), *h = m.pnode(e, "halfedge");
            const Node *cv = m.pnode(e, "curve"); if (!cv && h) cv = m.pnode(h, "curve");
            const Node *cb = m.is(cv, "TRIMMED_CURVE") ? m.pnode(cv, "basis_curve") : cv;
            Handle(Geom_Curve) gc = c.ecurve[kv.first]; double t0 = c.erange[kv.first].first, t1 = c.erange[kv.first].second;
            for (int side = 0; side < 2 && h; side++, h = m.pnode(h, "other")) {
                const Node *fn = m.pnode(m.pnode(h, "loop"), "face"); int si = m.ptr(fn, "surface");
                if (!surf.count(si)) continue;
                double dmax = 0;
                for (int k = 0; k <= 8; k++) { gp_Pnt q = gc->Value(t0 + (t1 - t0) * k / 8), f; gp_Vec nn; if (c.foot(surf[si], q, f, nn)) dmax = std::max(dmax, q.Distance(f)); }
                c.dev(std::string("edge (") + m.tname(cb) + ") on face surface " + m.tname(m.node(si)), dmax);
            }
            TopoDS_Vertex a, b; TopExp::Vertices(kv.second, a, b);
            if (!a.IsNull() && !b.IsNull()) c.dev(std::string("vertex on edge curve end (") + m.tname(cb) + ")", std::max(std::min(BRep_Tool::Pnt(a).Distance(gc->Value(t0)), BRep_Tool::Pnt(a).Distance(gc->Value(t1))),
                                                                                                            std::min(BRep_Tool::Pnt(b).Distance(gc->Value(t0)), BRep_Tool::Pnt(b).Distance(gc->Value(t1)))));
        }
    }
    r.shape = comp;
    for (auto &kv : c.vtx) r.vertex_of[kv.first] = kv.second;
    return true;
}

}  // namespace xtocc
