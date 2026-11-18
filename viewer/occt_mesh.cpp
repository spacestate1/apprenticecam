/* occt_mesh.cpp - see occt_mesh.h. Ours (3drough, 2026-10-24): the glue only. The B-rep comes from viewer/occt/xt_occt.cpp
 * (formats/occt_accuracy.md: within 2.8e-6 mm of the XT on the user's solids), the triangles from OCCT's BRepMesh. */
#include "occt_mesh.h"
#include "occt_prim.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

extern "C" {
#include "xt_mesh.h"
}

#ifdef HAVE_OCCT
#include "occt/xt_occt.h"
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <Geom_Surface.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <map>
#include <memory>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <Geom_BSplineSurface.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepLib.hxx>
#include <Geom2d_Line.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_Shape.hxx>

extern "C" int occt_mesh_available(void) { return 1; }

extern "C" int occt_mesh_xt(const uint8_t *xb, size_t n, double scale, double deflection, double **tri, char *err, size_t errn) {
    *tri = nullptr;
    char path[2200] = "";
    if (!xt_schema_find(xb, n, path, sizeof path) || !path[0]) { snprintf(err, errn, "no Parasolid schema file for this solid"); return -1; }
    static std::map<std::string, std::shared_ptr<xtocc::Schema>> cache;
    auto it = cache.find(path);
    std::shared_ptr<xtocc::Schema> sch = it != cache.end() ? it->second : (cache[path] = xtocc::load_schema(path));
    if (!sch) { snprintf(err, errn, "cannot read the schema %s", path); return -1; }
    xtocc::Model m; std::string e;
    if (!xtocc::parse(*sch, xb, n, m, e)) { snprintf(err, errn, "XT: %s", e.c_str()); return -1; }
    xtocc::Result res;
    try {
        if (!xtocc::build(m, scale, res)) {
            std::string u; for (auto &s : res.unmapped) { if (u.find(s) == std::string::npos) u += (u.empty() ? "" : ", ") + s; }
            snprintf(err, errn, "solid not translated (%s%s%s)", res.error.c_str(), u.empty() ? "" : ": ", u.c_str()); return -1; }
        if (!(deflection > 0)) deflection = 0.01;
        BRepMesh_IncrementalMesh mesh(res.shape, deflection, Standard_False, 0.5, Standard_False);
        std::vector<double> out;
        int nskip = 0, nface = 0;
        for (TopExp_Explorer ex(res.shape, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face &F = TopoDS::Face(ex.Current());
            TopLoc_Location loc;
            Handle(Poly_Triangulation) T = BRep_Tool::Triangulation(F, loc);
            nface++;
            if (T.IsNull() && res.new_geometry) { nskip++; continue; }   /* xtfix (formats/xt_occt_fixes.md): a face with the new geometry
                that BRepMesh cannot triangulate is left out and reported (err, below), not the whole solid */
            if (T.IsNull()) { snprintf(err, errn, "BRepMesh left a face without triangles"); return -1; }
            bool rev = F.Orientation() == TopAbs_REVERSED;
            for (int i = 1; i <= T->NbTriangles(); i++) {
                int a, b, c; T->Triangle(i).Get(a, b, c);
                if (rev) std::swap(b, c);
                for (int k : { a, b, c }) {
                    gp_Pnt p = T->Node(k).Transformed(loc.Transformation());
                    out.push_back(p.X()); out.push_back(p.Y()); out.push_back(p.Z());
                }
            }
        }
        int nt = (int)(out.size() / 9);
        if (nskip) {
            if (nskip == nface) { snprintf(err, errn, "BRepMesh triangulated no face"); return -1; }
            snprintf(err, errn, "warning: %d of %d faces not meshed (BRepMesh), left out of the drive mesh", nskip, nface);
            if (getenv("MCAM_3D_DEBUG") || getenv("XTOCC_DEBUG")) fprintf(stderr, "occt_mesh_xt: %s\n", err);
        }
        *tri = (double *)malloc(sizeof(double) * (out.size() ? out.size() : 1));
        if (!*tri) { snprintf(err, errn, "out of memory"); return -1; }
        memcpy(*tri, out.data(), sizeof(double) * out.size());
        return nt;
    } catch (...) { snprintf(err, errn, "OpenCASCADE failed on this solid"); free(*tri); *tri = nullptr; return -1; }
}
/* the face's triangles -> out (9 doubles each), its orientation kept */
static bool face_tris(const TopoDS_Face &F, std::vector<double> &out) {
    TopLoc_Location loc;
    Handle(Poly_Triangulation) T = BRep_Tool::Triangulation(F, loc);
    if (T.IsNull()) return false;
    bool rev = F.Orientation() == TopAbs_REVERSED;
    for (int i = 1; i <= T->NbTriangles(); i++) {
        int a, b, c; T->Triangle(i).Get(a, b, c);
        if (rev) std::swap(b, c);
        for (int k : { a, b, c }) { gp_Pnt p = T->Node(k).Transformed(loc.Transformation()); out.push_back(p.X()); out.push_back(p.Y()); out.push_back(p.Z()); }
    }
    return true;
}
/* a SURF_ID record (class 0x100) -> triangles in model units. Kind 0 (NURBS, nbs_type at ent +0x100 = surf +0x98; mccore
   MaNURBSurf(DB_LIST_ENT*, nbs_type*, bool) decomp/mccore_curves.c l.1470): i32 nku, nkv, ncu, ncv, u8 values per point
   (+0x10: 3 = xyz, weight 1; 4 = w x y z when rational), u / v degree (+0x11 / +0x12), +0x13 bit 0 = non-rational; then the
   u knots, the v knots and the control net (u index fastest). Rational points are homogeneous (w x, w y, w z): measured on
   TUTORIAL 2 SWARF's arc surfaces, which are exact circles only that way (and u fastest). The knot vectors are taken as
   stored (non-periodic B-spline: the periodic flags, MaSurface::IsUPeriodic, are not read - a full knot vector evaluates the
   same). Other kinds: NOT DECODED here (-1). */
/* kind 0 (NURBS) -> the OCCT surface (see occt_mesh_surf); null with err */
static Handle(Geom_BSplineSurface) nurbs_surface(const uint8_t *r, size_t n, char *err, size_t errn) {
    if (n < 0x118 || r[0x68] != 0) { snprintf(err, errn, "surface kind %d is not read here (only NURBS)", n > 0x68 ? r[0x68] : -1); return nullptr; }
    int32_t nku, nkv, ncu, ncv; memcpy(&nku, r + 0x100, 4); memcpy(&nkv, r + 0x104, 4); memcpy(&ncu, r + 0x108, 4); memcpy(&ncv, r + 0x10c, 4);
    int vpp = r[0x110], ud = r[0x111], vd = r[0x112]; bool rat = !(r[0x113] & 1);
    if (ncu < 2 || ncv < 2 || ud < 1 || vd < 1 || ud > 25 || vd > 25 || nku != ncu + ud + 1 || nkv != ncv + vd + 1 || (vpp != 3 && vpp != 4) || (rat && vpp != 4)) {
        snprintf(err, errn, "NURBS surface sizes not as expected (%d %d %d %d, %d values, degree %d %d)", nku, nkv, ncu, ncv, vpp, ud, vd); return nullptr; }
    size_t need = 0x118 + 8 * ((size_t)nku + nkv + (size_t)vpp * ncu * ncv);
    if (need > n) { snprintf(err, errn, "NURBS surface record too short"); return nullptr; }
    const uint8_t *q = r + 0x118;
    auto rd = [&](size_t i) { double v; memcpy(&v, q + 8 * i, 8); return v; };
    auto knots = [&](size_t base, int nk, std::vector<double> &kv, std::vector<int> &km) {
        for (int i = 0; i < nk; i++) { double k = rd(base + i); if (!kv.empty() && k - kv.back() <= 1e-12 * (1 + fabs(k))) km.back()++; else { kv.push_back(k); km.push_back(1); } } };
    std::vector<double> ukv, vkv; std::vector<int> ukm, vkm;
    knots(0, nku, ukv, ukm); knots(nku, nkv, vkv, vkm);
    TColStd_Array1OfReal UK(1, (int)ukv.size()), VK(1, (int)vkv.size()); TColStd_Array1OfInteger UM(1, (int)ukm.size()), VM(1, (int)vkm.size());
    for (size_t i = 0; i < ukv.size(); i++) { UK((int)i + 1) = ukv[i]; UM((int)i + 1) = ukm[i]; }
    for (size_t i = 0; i < vkv.size(); i++) { VK((int)i + 1) = vkv[i]; VM((int)i + 1) = vkm[i]; }
    TColgp_Array2OfPnt P(1, ncu, 1, ncv); TColStd_Array2OfReal W(1, ncu, 1, ncv);
    size_t cp = (size_t)nku + nkv;
    for (int j = 0; j < ncv; j++) for (int i = 0; i < ncu; i++) {
        size_t o = cp + (size_t)vpp * ((size_t)j * ncu + i);
        double w = vpp == 4 ? rd(o) : 1.0, x = rd(o + vpp - 3), y = rd(o + vpp - 2), z = rd(o + vpp - 1);
        if (vpp == 4 && !rat) w = 1.0;
        if (rat) { if (!(w > 0)) { snprintf(err, errn, "NURBS surface weight %g", w); return nullptr; } x /= w; y /= w; z /= w; }
        P(i + 1, j + 1) = gp_Pnt(x, y, z); W(i + 1, j + 1) = w;
    }
    return rat ? new Geom_BSplineSurface(P, W, UK, VK, UM, VM, ud, vd) : new Geom_BSplineSurface(P, UK, VK, UM, VM, ud, vd);
}
/* a SURF_ID record (class 0x100) -> triangles in model units.
   Kind 0 (NURBS, nbs_type at ent +0x100 = surf +0x98; mccore MaNURBSurf(DB_LIST_ENT*, nbs_type*, bool) decomp/mccore_curves.c
   l.1470): i32 nku, nkv, ncu, ncv, u8 values per point (+0x10: 3 = xyz, weight 1; 4 = w x y z when rational), u / v degree
   (+0x11 / +0x12), +0x13 bit 0 = non-rational; then the u knots, the v knots and the control net (u index fastest). Rational
   points are homogeneous (w x, w y, w z): measured on TUTORIAL 2 SWARF's arc surfaces, which are exact circles only that way (and
   u fastest). The knot vectors are taken as stored (non-periodic B-spline: the periodic flags, MaSurface::IsUPeriodic, are not
   read - a full knot vector evaluates the same).
   Kind 10 (trimmed; MaSurface::LoadTrimmedSurf -> MaFace::MaFace(DB_LIST_ENT*, bool, double), decomp/trimsrf/loadtrim.c l.97):
   ent +0x100 = the base surface (its eptr, ent +0x20: measured), +0x10c i32 m = uv points, +0x128 i32 loops; the tail at +0x138:
   m (u, v) doubles, then per loop i32 start, i32 count (the loop = points start .. start + count, closed back to its first; a
   loop of 1 point is skipped; loop 0 is the outer boundary, the rest holes). The uv points are in the base record's parameter
   range (+0x70 u0, u1, v0, v1; MaPolyLine::UVTransform maps them onto MaSurface's when they differ - for a NURBS base the stored
   range is its knot range (measured), which OCCT uses as is). base / base_n: the base's record (kind 0 here). The loops are made
   wires of straight uv segments on the base (as MaPolyLine), the face fixed by OCCT's ShapeFix (wire orientation; ours).
   Other kinds: NOT DECODED here (-1). */
/* a surface given by its positions (mcsurf.c, Mastercam's evaluator) -> a degree 1 B-spline through a uniform grid of them at
   Mastercam's own (u, v) (the knots are the grid parameters, so trimming uv points stay valid); the grid is doubled in u / v while a
   cell's centre is more than the deflection from the bilinear patch (ours; up to 512 x 512) */
static Handle(Geom_BSplineSurface) eval_surface(occt_surf_eval_fn ev, void *ctx, const double d[4], double defl, char *err, size_t errn) {
    if (!(d[1] > d[0]) || !(d[3] > d[2])) { snprintf(err, errn, "surface parameter range empty"); return nullptr; }
    int nu = 8, nv = 8;
    std::vector<gp_Pnt> G;
    auto at = [&](int i, int j) -> gp_Pnt & { return G[(size_t)j * (nu + 1) + i]; };
    for (int it = 0; it < 12; it++) {
        G.assign((size_t)(nu + 1) * (nv + 1), gp_Pnt());
        for (int j = 0; j <= nv; j++) for (int i = 0; i <= nu; i++) {
            double p[3]; if (ev(ctx, d[0] + (d[1] - d[0]) * i / nu, d[2] + (d[3] - d[2]) * j / nv, p)) { snprintf(err, errn, "surface evaluation failed"); return nullptr; }
            at(i, j) = gp_Pnt(p[0], p[1], p[2]);
        }
        double eu = 0, evv = 0;   /* the worst midpoint deviation along u (cell u-centres on grid v lines) and along v */
        for (int j = 0; j <= nv; j++) for (int i = 0; i < nu; i++) {
            double p[3]; ev(ctx, d[0] + (d[1] - d[0]) * (i + 0.5) / nu, d[2] + (d[3] - d[2]) * j / nv, p);
            gp_XYZ m = (at(i, j).XYZ() + at(i + 1, j).XYZ()) * 0.5; eu = std::max(eu, (m - gp_XYZ(p[0], p[1], p[2])).Modulus()); }
        for (int j = 0; j < nv; j++) for (int i = 0; i <= nu; i++) {
            double p[3]; ev(ctx, d[0] + (d[1] - d[0]) * i / nu, d[2] + (d[3] - d[2]) * (j + 0.5) / nv, p);
            gp_XYZ m = (at(i, j).XYZ() + at(i, j + 1).XYZ()) * 0.5; evv = std::max(evv, (m - gp_XYZ(p[0], p[1], p[2])).Modulus()); }
        bool ru = eu > defl && nu < 512, rv = evv > defl && nv < 512;
        if (!ru && !rv) break;
        if (ru) nu *= 2;
        if (rv) nv *= 2;
    }
    TColgp_Array2OfPnt P(1, nu + 1, 1, nv + 1);
    for (int j = 0; j <= nv; j++) for (int i = 0; i <= nu; i++) P(i + 1, j + 1) = at(i, j);
    TColStd_Array1OfReal UK(1, nu + 1), VK(1, nv + 1); TColStd_Array1OfInteger UM(1, nu + 1), VM(1, nv + 1);
    for (int i = 0; i <= nu; i++) { UK(i + 1) = d[0] + (d[1] - d[0]) * i / nu; UM(i + 1) = i == 0 || i == nu ? 2 : 1; }
    for (int j = 0; j <= nv; j++) { VK(j + 1) = d[2] + (d[3] - d[2]) * j / nv; VM(j + 1) = j == 0 || j == nv ? 2 : 1; }
    return new Geom_BSplineSurface(P, UK, VK, UM, VM, 1, 1);
}
static Handle(Geom_BSplineSurface) any_surface(const uint8_t *g, size_t gn, occt_surf_eval_fn ev, void *ctx, double defl, char *err, size_t errn) {
    if (gn > 0x68 && g[0x68] == 0) return nurbs_surface(g, gn, err, errn);
    if (!ev || gn < 0x90) { snprintf(err, errn, "surface kind %d is not read here", gn > 0x68 ? g[0x68] : -1); return nullptr; }
    double d[4]; for (int k = 0; k < 4; k++) memcpy(&d[k], g + 0x70 + 8 * k, 8);
    return eval_surface(ev, ctx, d, defl, err, errn);
}
extern "C" int occt_mesh_surf(const uint8_t *r, size_t n, const uint8_t *base, size_t base_n, occt_surf_eval_fn ev, void *ectx, double deflection, double **tri, double **seg, int *nseg, char *err, size_t errn) {
    *tri = nullptr; if (seg) *seg = nullptr; if (nseg) *nseg = 0;
    if (n < 0x69) { snprintf(err, errn, "surface record too short"); return -1; }
    if (!(deflection > 0)) deflection = 0.01;
    try {
        TopoDS_Face F;
        if (r[0x68] != 10) {
            Handle(Geom_BSplineSurface) S = any_surface(r, n, ev, ectx, deflection, err, errn);
            if (S.IsNull()) return -1;
            BRepBuilderAPI_MakeFace mf(S, 1e-7);
            if (!mf.IsDone()) { snprintf(err, errn, "OpenCASCADE could not make a face of this surface"); return -1; }
            F = mf.Face();
        } else if (r[0x68] == 10) {
            if (n < 0x138) { snprintf(err, errn, "trimmed surface record too short"); return -1; }
            if (!base) { snprintf(err, errn, "trimmed surface: its base surface is not in the part"); return -1; }
            Handle(Geom_BSplineSurface) S = any_surface(base, base_n, ev, ectx, deflection, err, errn);
            if (S.IsNull()) { char e2[160]; snprintf(e2, sizeof e2, "trimmed surface base: %s", err); snprintf(err, errn, "%s", e2); return -1; }
            int32_t m, nl; memcpy(&m, r + 0x10c, 4); memcpy(&nl, r + 0x128, 4);
            if (m < 1 || nl < 1 || 0x138 + 16 * (size_t)m + 8 * (size_t)nl > n) { snprintf(err, errn, "trimmed surface sizes not as expected (%d points, %d loops)", m, nl); return -1; }
            const uint8_t *t = r + 0x138;
            auto uv = [&](int i) { double a[2]; memcpy(a, t + 16 * (size_t)i, 16); return gp_Pnt2d(a[0], a[1]); };
            std::vector<TopoDS_Wire> wires;
            for (int l = 0; l < nl; l++) {
                int32_t st, cnt; memcpy(&st, t + 16 * (size_t)m + 8 * l, 4); memcpy(&cnt, t + 16 * (size_t)m + 8 * l + 4, 4);
                if (cnt + 1 == 1) continue;
                if (st < 0 || cnt < 1 || st + cnt >= m) { snprintf(err, errn, "trimmed surface loop %d out of range", l); return -1; }
                BRepBuilderAPI_MakeWire mw;
                std::vector<gp_Pnt2d> pts; for (int i = st; i <= st + cnt; i++) pts.push_back(uv(i));
                if (pts.front().Distance(pts.back()) > 1e-12) pts.push_back(pts.front());
                /* OCCT wants the outer wire counter-clockwise in (u, v) and the holes clockwise; Mastercam stores either way (measured:
                   TURBO_IMPELLER's outer loops run both ways) - reversed here by the loop's signed area (ours). A loop without area (one
                   repeated point, seen there) is skipped (ours; MaFace::CheckForValidLoops is not decoded) */
                double ar = 0; for (size_t i = 0; i + 1 < pts.size(); i++) ar += pts[i].X() * pts[i + 1].Y() - pts[i + 1].X() * pts[i].Y();
                if (fabs(ar) < 1e-18) continue;
                if ((wires.empty() && ar < 0) || (!wires.empty() && ar > 0)) std::reverse(pts.begin(), pts.end());
                for (size_t i = 0; i + 1 < pts.size(); i++) {
                    if (pts[i].Distance(pts[i + 1]) <= 1e-12) continue;
                    Handle(Geom2d_Line) L = new Geom2d_Line(pts[i], gp_Dir2d(gp_Vec2d(pts[i], pts[i + 1])));
                    BRepBuilderAPI_MakeEdge me(L, S, 0.0, pts[i].Distance(pts[i + 1]));
                    if (!me.IsDone()) { snprintf(err, errn, "trimmed surface: an edge of loop %d failed", l); return -1; }
                    mw.Add(me.Edge());
                }
                if (!mw.IsDone()) { snprintf(err, errn, "trimmed surface: loop %d is not a wire", l); return -1; }
                TopoDS_Wire w = mw.Wire(); BRepLib::BuildCurves3d(w); wires.push_back(w);
            }
            if (wires.empty()) { snprintf(err, errn, "trimmed surface without a loop"); return -1; }
            BRepBuilderAPI_MakeFace mf(S, wires[0], Standard_True);
            for (size_t i = 1; i < wires.size() && mf.IsDone(); i++) mf.Add(wires[i]);
            if (!mf.IsDone()) { snprintf(err, errn, "OpenCASCADE could not make the trimmed face"); return -1; }
            /* the edges' 3D curves come from the uv lines (BuildCurves3d): SameParameter and OCCT's shape fixer make the face valid
               for BRepMesh (ShapeFix_Face alone left TURBO_IMPELLER's faces invalid: 31 of 217 unmeshed; ours) */
            { TopoDS_Face f0 = mf.Face(); BRepLib::SameParameter(f0, 1e-6, Standard_True);
              ShapeFix_Shape sfs(f0); sfs.SetPrecision(1e-6); sfs.SetMaxTolerance(1e-3); sfs.Perform();
              TopExp_Explorer fx(sfs.Shape(), TopAbs_FACE); F = fx.More() ? TopoDS::Face(fx.Current()) : f0; }
        }
        BRepMesh_IncrementalMesh mesh(F, deflection, Standard_False, 0.5, Standard_False);
        std::vector<double> out;
        if (!face_tris(F, out)) { snprintf(err, errn, "BRepMesh left the surface without triangles"); return -1; }
        if (seg && nseg) {   /* the face's boundary edges as segments (6 doubles each), from the edges' polygons on the triangulation */
            std::vector<double> sv; TopLoc_Location loc; Handle(Poly_Triangulation) T = BRep_Tool::Triangulation(F, loc);
            for (TopExp_Explorer ex(F, TopAbs_EDGE); ex.More(); ex.Next()) {
                const TopoDS_Edge &E = TopoDS::Edge(ex.Current());
                Handle(Poly_PolygonOnTriangulation) PT = BRep_Tool::PolygonOnTriangulation(E, T, loc);
                if (PT.IsNull()) continue;
                const TColStd_Array1OfInteger &nd = PT->Nodes();
                for (int i = nd.Lower(); i < nd.Upper(); i++) for (int k : { nd(i), nd(i + 1) }) {
                    gp_Pnt p = T->Node(k).Transformed(loc.Transformation()); sv.push_back(p.X()); sv.push_back(p.Y()); sv.push_back(p.Z()); }
            }
            *seg = (double *)malloc(sizeof(double) * (sv.size() ? sv.size() : 1));
            if (*seg) { memcpy(*seg, sv.data(), sizeof(double) * sv.size()); *nseg = (int)(sv.size() / 6); }
        }
        *tri = (double *)malloc(sizeof(double) * (out.size() ? out.size() : 1));
        if (!*tri) { snprintf(err, errn, "out of memory"); return -1; }
        memcpy(*tri, out.data(), sizeof(double) * out.size());
        return (int)(out.size() / 9);
    } catch (...) { snprintf(err, errn, "OpenCASCADE failed on this surface"); free(*tri); *tri = nullptr; if (seg) { free(*seg); *seg = nullptr; } return -1; }
}
/* the XT's BODY.body_type (1 solid, 3 sheet: formats/occt_xt.md; mcgeomsld is_solid_a_sheet = PK_BODY_ask_type != solid), -1 = unread */
extern "C" int occt_xt_body_type(const uint8_t *xb, size_t n) {
    char path[2200] = "";
    if (!xt_schema_find(xb, n, path, sizeof path) || !path[0]) return -1;
    static std::map<std::string, std::shared_ptr<xtocc::Schema>> cache;
    auto it = cache.find(path);
    std::shared_ptr<xtocc::Schema> sch = it != cache.end() ? it->second : (cache[path] = xtocc::load_schema(path));
    if (!sch) return -1;
    xtocc::Model m; std::string e;
    if (!xtocc::parse(*sch, xb, n, m, e)) return -1;
    for (size_t i = 0; i < m.by_idx.size(); i++) if (m.by_idx[i].p && m.is(&m.by_idx[i], "BODY")) return m.chr(&m.by_idx[i], "body_type");   /* a byte field in the user parts' schema (an int32 read ran into the next field) */
    return -1;
}
/* From Solid (formats/surfaces_tab_plan.md "From Solid"): the body's faces in XT FACE node order, each as occt_prim.cpp's
   occt_face_data after BRepBuilderAPI_NurbsConvert of the built shape (the converted faces found through BRepBuilderAPI_NurbsConvert's
   ModifiedShape of each XT face) */
bool occt_face_data(const TopoDS_Face &F, OpFace &o, std::string &err);
extern "C" int occt_xt_faces(const uint8_t *xb, size_t n, double scale, OpFace **out, int *nout, char *err, size_t errn) {
    *out = nullptr; *nout = 0;
    char path[2200] = "";
    if (!xt_schema_find(xb, n, path, sizeof path) || !path[0]) { snprintf(err, errn, "no Parasolid schema file for this solid"); return -1; }
    static std::map<std::string, std::shared_ptr<xtocc::Schema>> cache;
    auto it = cache.find(path);
    std::shared_ptr<xtocc::Schema> sch = it != cache.end() ? it->second : (cache[path] = xtocc::load_schema(path));
    if (!sch) { snprintf(err, errn, "cannot read the schema %s", path); return -1; }
    xtocc::Model m; std::string e;
    if (!xtocc::parse(*sch, xb, n, m, e)) { snprintf(err, errn, "XT: %s", e.c_str()); return -1; }
    xtocc::Result res;
    try {
        if (!xtocc::build(m, scale, res)) { snprintf(err, errn, "solid not translated (%s)", res.error.c_str()); return -1; }
        BRepBuilderAPI_NurbsConvert nc(res.shape, Standard_True);
        std::vector<OpFace> fs;
        for (auto &kv : res.face_of) {               /* std::map: ascending XT node index */
            TopoDS_Shape s2 = nc.ModifiedShape(kv.second);
            if (s2.IsNull() || s2.ShapeType() != TopAbs_FACE) { snprintf(err, errn, "face %d not converted", kv.first); for (auto &f : fs) occt_prim_free_one(&f); return -1; }
            TopoDS_Face F2 = TopoDS::Face(s2); if (kv.second.Orientation() != F2.Orientation()) F2.Orientation(kv.second.Orientation());
            OpFace o; if (!occt_face_data(F2, o, e)) { snprintf(err, errn, "%s", e.c_str()); for (auto &f : fs) occt_prim_free_one(&f); return -1; }
            fs.push_back(o);
        }
        *out = (OpFace *)calloc(fs.size() ? fs.size() : 1, sizeof(OpFace)); if (fs.size()) memcpy(*out, fs.data(), sizeof(OpFace) * fs.size());
        *nout = (int)fs.size();
        return 0;
    } catch (...) { snprintf(err, errn, "OpenCASCADE failed on this solid"); return -1; }
}
/* the Plane Selection's solid face (mcgeomsld sld_is_surf_planar 0x1800bdec0 + sld_check_face_normal 0x1800aecc0; formats/plane_selection.md
   4.5): FACE fi (XT FACE node order, = sld_pick_part_face's index) of a part solid. PK_FACE_ask_oriented_surf -> the class: PLANE
   (0xfa1), or B_SURFACE (0xfa6) whose NURBS_SURF surface_form is SCH_planar (3 = PK_BSURF_form_planar_c 0x21fe) or of degree 1 x 1,
   also under one OFFSET_SURF (0xfa8, PK_OFFSET_ask). *planar = 1 / 0. The point and normal at the middle of the face's uv box
   (PK_FACE_find_uvbox, PK_SURF_eval_with_normal) - here on the face xt_occt built (the same parametrisation as the XT surface,
   formats/xt_occt_fixes.md; the surface / face senses folded into the face orientation), scale = model units per metre (Mastercam:
   x 1000 mm / 39.37007874015748 inch). 0 = ok, -1 with err */
extern "C" int occt_xt_face_plane(const uint8_t *xb, size_t n, double scale, int fi, int *planar, double P[3], double N[3], char *err, size_t errn) {
    *planar = 0;
    char path[2200] = "";
    if (!xt_schema_find(xb, n, path, sizeof path) || !path[0]) { snprintf(err, errn, "no Parasolid schema file for this solid"); return -1; }
    static std::map<std::string, std::shared_ptr<xtocc::Schema>> cache;
    auto it = cache.find(path);
    std::shared_ptr<xtocc::Schema> sch = it != cache.end() ? it->second : (cache[path] = xtocc::load_schema(path));
    if (!sch) { snprintf(err, errn, "cannot read the schema %s", path); return -1; }
    xtocc::Model m; std::string e;
    if (!xtocc::parse(*sch, xb, n, m, e)) { snprintf(err, errn, "XT: %s", e.c_str()); return -1; }
    xtocc::Result res;
    try {
        if (!xtocc::build(m, scale, res)) { snprintf(err, errn, "solid not translated (%s)", res.error.c_str()); return -1; }
        int k = 0; const TopoDS_Face *F = nullptr; int node = -1;
        for (auto &kv : res.face_of) { if (k++ == fi) { F = &kv.second; node = kv.first; break; } }
        if (!F) { snprintf(err, errn, "no such face"); return -1; }
        const xtocc::Node *sn = m.pnode(m.node(node), "surface");
        if (sn && m.is(sn, "OFFSET_SURF")) sn = m.pnode(sn, "surface");
        if (sn && m.is(sn, "PLANE")) *planar = 1;
        else if (sn && m.is(sn, "B_SURFACE")) {
            const xtocc::Node *nb = m.pnode(sn, "nurbs");
            if (nb && (m.chr(nb, "surface_form") == 3 || (m.sval(nb, "u_degree") == 1 && m.sval(nb, "v_degree") == 1))) *planar = 1;
        }
        if (!*planar) return 0;
        double u0, u1, v0, v1; BRepTools::UVBounds(*F, u0, u1, v0, v1);
        Handle(Geom_Surface) g = BRep_Tool::Surface(*F);
        gp_Pnt p; gp_Vec du, dv; g->D1((u0 + u1) * 0.5, (v0 + v1) * 0.5, p, du, dv);
        gp_Vec nv = du.Crossed(dv); double l = nv.Magnitude();
        if (!(l > 0)) { snprintf(err, errn, "the face's normal is degenerate at the middle of its uv box"); return -1; }
        nv /= l; if (F->Orientation() == TopAbs_REVERSED) nv.Reverse();
        P[0] = p.X(); P[1] = p.Y(); P[2] = p.Z(); N[0] = nv.X(); N[1] = nv.Y(); N[2] = nv.Z();
        return 0;
    } catch (...) { snprintf(err, errn, "OpenCASCADE failed on this solid"); return -1; }
}
#else
extern "C" int occt_xt_face_plane(const uint8_t *xb, size_t n, double scale, int fi, int *planar, double P[3], double N[3], char *err, size_t errn) { (void)xb; (void)n; (void)scale; (void)fi; (void)P; (void)N; *planar = 0; snprintf(err, errn, "built without OpenCASCADE"); return -1; }
extern "C" int occt_xt_faces(const uint8_t *xb, size_t n, double scale, OpFace **out, int *nout, char *err, size_t errn) { (void)xb; (void)n; (void)scale; *out = nullptr; *nout = 0; snprintf(err, errn, "built without OpenCASCADE"); return -1; }
extern "C" int occt_xt_body_type(const uint8_t *xb, size_t n) { (void)xb; (void)n; return -1; }
extern "C" int occt_mesh_available(void) { return 0; }
extern "C" int occt_mesh_surf(const uint8_t *r, size_t n, const uint8_t *base, size_t base_n, occt_surf_eval_fn ev, void *ectx, double deflection, double **tri, double **seg, int *nseg, char *err, size_t errn) {
    (void)r; (void)n; (void)base; (void)base_n; (void)ev; (void)ectx; (void)deflection; *tri = nullptr; if (seg) *seg = nullptr; if (nseg) *nseg = 0; snprintf(err, errn, "built without OpenCASCADE"); return -1;
}
extern "C" int occt_mesh_xt(const uint8_t *xb, size_t n, double scale, double deflection, double **tri, char *err, size_t errn) {
    (void)xb; (void)n; (void)scale; (void)deflection; *tri = nullptr;
    snprintf(err, errn, "built without OpenCASCADE (the 3D toolpaths need it: viewer/Makefile OCC_INC, formats/hst3d_port_plan.md 6)");
    return -1;
}
#endif
