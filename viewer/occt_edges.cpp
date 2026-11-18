/* occt_edges.cpp - the edges of a solid as Mastercam curves (Curve One Edge / Curve All Edges, formats/wireframe.md "Curves on surfaces, solid edges"):
 * the XT -> OpenCASCADE translator (occt/xt_occt.cpp, user decision: OCCT stands in for Parasolid) gives one edge per XT EDGE node; here each edge becomes
 * the data ps_edge_to_mc_curve (mcgeomsld 0x1800434d0) makes from PK_EDGE_ask_geometry: a line, an arc, or a NURBS (FUN_180043850 / FUN_180043e40). The curve
 * data comes through occt_fill_curve (occt_cad.cpp); the direction is the XT edge's (PK_EDGE_ask_geometry's sense: HALFEDGE.sense of EDGE.halfedge,
 * "along" below), as FUN_180043850 (line ends, arc sweep sign), FUN_180044820 (FUN_18003ada0 reverses the control points) use it.
 * Ours (labelled): the sampled polyline (.pts) for picking; order = ascending XT EDGE node (PK_BODY_ask_edges' order NOT DECODED, FUN_180096570). */
extern "C" {
#include "xt_mesh.h"
}
#include "occt_cad.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <string>
#include <vector>


#ifdef HAVE_OCCT
#include "occt/xt_occt.h"
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <memory>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Compound.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Precision.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>

bool occt_fill_curve(const TopoDS_Edge &e, OccCurve &o);
bool occt_shape_of(const uint8_t *b, size_t n, TopoDS_Shape &s);

static void poly_of(const TopoDS_Edge &e, double *&pts, int &np) {
    BRepAdaptor_Curve a(e);
    int n = a.GetType() == GeomAbs_Line ? 1 : 96;   /* ours (picking): chord sampling of the curve */
    double t0 = a.FirstParameter(), t1 = a.LastParameter();
    pts = (double *)malloc(sizeof(double) * 3 * (size_t)(n + 1)); np = n + 1;
    for (int i = 0; i <= n; i++) { gp_Pnt p = a.Value(t0 + (t1 - t0) * i / n); pts[3 * i] = p.X(); pts[3 * i + 1] = p.Y(); pts[3 * i + 2] = p.Z(); }
}
static bool edge_data(const TopoDS_Edge &E0, int along, OccEdge &o) {
    memset(&o, 0, sizeof o); o.along = along; o.pk_along = along; o.f[0] = o.f[1] = -1;
    TopoDS_Edge E = E0; E.Orientation(TopAbs_FORWARD);
    OccCurve c; if (BRep_Tool::Degenerated(E) || !occt_fill_curve(E, c)) return false;
    o.c = c;
    if (c.type == 3 && !along) {                     /* FUN_180044820: not along the curve: FUN_18003ada0 reverses (the same as OCCT's Reverse: k' = k0 + kn - k) */
        for (int i = 0, j = c.nc - 1; i < j; i++, j--) for (int k = 0; k < 4; k++) { double t = c.cp[4 * i + k]; c.cp[4 * i + k] = c.cp[4 * j + k]; c.cp[4 * j + k] = t; }
        double a = c.k[0], b = c.k[c.nk - 1];
        for (int i = 0, j = c.nk - 1; i <= j; i++, j--) { double x = c.k[i], y = c.k[j]; c.k[i] = a + b - y; c.k[j] = a + b - x; }
        o.c = c;
    }
    poly_of(E, o.pts, o.np);
    return true;
}
template <class FaceIndex>
static void edge_faces(const TopTools_IndexedDataMapOfShapeListOfShape &map, const TopoDS_Edge &E, const FaceIndex &fi, OccEdge &o) {
    if (!map.Contains(E)) return;
    int k = 0;
    for (TopTools_ListOfShape::Iterator it(map.FindFromKey(E)); it.More() && k < 2; it.Next()) { int x = fi(TopoDS::Face(it.Value())); if (x >= 0) o.f[k++] = x; }
}

static std::shared_ptr<xtocc::Schema> schema_for(const uint8_t *xb, size_t n, char *err, size_t errn) {
    char path[2200] = "";
    if (!xt_schema_find(xb, n, path, sizeof path) || !path[0]) { snprintf(err, errn, "no Parasolid schema file for this solid"); return nullptr; }
    static std::map<std::string, std::shared_ptr<xtocc::Schema>> cache;
    auto it = cache.find(path);
    std::shared_ptr<xtocc::Schema> sch = it != cache.end() ? it->second : (cache[path] = xtocc::load_schema(path));
    if (!sch) snprintf(err, errn, "cannot read the schema %s", path);
    return sch;
}
extern "C" int occt_xt_edges(const uint8_t *xb, size_t n, double scale, OccEdge **out, int *nout, int *nface, char *err, size_t errn) {
    *out = nullptr; *nout = 0; if (nface) *nface = 0;
    auto sch = schema_for(xb, n, err, errn); if (!sch) return -1;
    xtocc::Model m; std::string e;
    if (!xtocc::parse(*sch, xb, n, m, e)) { snprintf(err, errn, "XT: %s", e.c_str()); return -1; }
    xtocc::Result res;
    try {
        if (!xtocc::build(m, scale, res)) { snprintf(err, errn, "solid not translated (%s)", res.error.c_str()); return -1; }
        std::vector<TopoDS_Face> faces; for (auto &kv : res.face_of) faces.push_back(kv.second);   /* ascending XT node (occt_xt_faces' order) */
        TopTools_IndexedDataMapOfShapeListOfShape emap; TopExp::MapShapesAndAncestors(res.shape, TopAbs_EDGE, TopAbs_FACE, emap);
        auto fi = [&](const TopoDS_Face &f) { for (size_t i = 0; i < faces.size(); i++) if (faces[i].IsSame(f)) return (int)i; return -1; };
        std::vector<OccEdge> v;
        for (auto &kv : res.edge_of) {                 /* ascending XT EDGE node */
            const xtocc::Node *en = m.node(kv.first); const xtocc::Node *hf = en ? m.pnode(en, "halfedge") : nullptr;
            int along = !hf || m.chr(hf, "sense") != '-';
            OccEdge o; if (!edge_data(kv.second, along, o)) continue;
            /* PK_EDGE_ask_geometry's sense is the edge against the CURVE AS STORED in the XT (its own orientation, PK_CIRCLE_ask's axis), while xt_occt builds gc with CURVE.sense '-' reversed:
               an arc made from a '-' circle gets the stored normal (the opposite of c.n here) and sense false (FUN_180043850 :337: the sweep negative). Checked: stored arcs of the corpus, sldscan, formats/wireframe.md */
            const xtocc::Node *cn = en ? m.pnode(en, "curve") : nullptr;
            if (cn && m.chr(cn, "sense") == '-') { o.pk_along = !along; if (o.c.type == 2) for (int k = 0; k < 3; k++) o.c.n[k] = -o.c.n[k]; }
            o.xt_edge = kv.first; edge_faces(emap, kv.second, fi, o);
            v.push_back(o);
        }
        *out = (OccEdge *)calloc(v.size() ? v.size() : 1, sizeof(OccEdge)); if (v.size()) memcpy(*out, v.data(), sizeof(OccEdge) * v.size());
        *nout = (int)v.size(); if (nface) *nface = (int)faces.size();
        return 0;
    } catch (...) { snprintf(err, errn, "OpenCASCADE failed on this solid"); return -1; }
}
/* the same for a BRep text blob (imported STEP / IGES, session solids through occt_cad): the edges in TopExp map order (OCCT's own edges, more breaks than Parasolid's at face seams; formats/wireframe.md) */
extern "C" int occt_brep_edge_list(const uint8_t *b, size_t n, OccEdge **out, int *nout, int *nface, char *err, size_t errn) {
    *out = nullptr; *nout = 0; if (nface) *nface = 0;
    try {
        TopoDS_Shape s; if (!occt_shape_of(b, n, s)) { snprintf(err, errn, "bad BRep blob"); return -1; }
        TopTools_IndexedMapOfShape fm, em; TopExp::MapShapes(s, TopAbs_FACE, fm); TopExp::MapShapes(s, TopAbs_EDGE, em);
        TopTools_IndexedDataMapOfShapeListOfShape emap; TopExp::MapShapesAndAncestors(s, TopAbs_EDGE, TopAbs_FACE, emap);
        auto fi = [&](const TopoDS_Face &f) { return fm.FindIndex(f) - 1; };
        std::vector<OccEdge> v;
        for (int i = 1; i <= em.Extent(); i++) { TopoDS_Edge E = TopoDS::Edge(em(i)); OccEdge o; if (!edge_data(E, E.Orientation() != TopAbs_REVERSED, o)) continue; o.xt_edge = i; edge_faces(emap, E, fi, o); v.push_back(o); }
        *out = (OccEdge *)calloc(v.size() ? v.size() : 1, sizeof(OccEdge)); if (v.size()) memcpy(*out, v.data(), sizeof(OccEdge) * v.size());
        *nout = (int)v.size(); if (nface) *nface = fm.Extent();
        return 0;
    } catch (...) { snprintf(err, errn, "OpenCASCADE failed on this solid"); return -1; }
}
extern "C" void occt_edges_free(OccEdge *e, int n) { for (int i = 0; i < n; i++) { free(e[i].c.k); free(e[i].c.cp); free(e[i].pts); } free(e); }

/* surface_open_edges (mcgeomsld 0x1800d0a30): StitchSurfaces (FUN_1800f2af0 / FUN_1800f2290: each surface a sheet, bs_body_heal_and_sew at the Only Open Edges tolerance) then
   PK_BODY_find_laminar_edges of the stitched sheets. Here: OCCT sewing (BRepBuilderAPI_Sewing, decision: OCCT stands in for Parasolid; the edge breaks follow OCCT's cutting at partial
   overlaps, not Parasolid's: see formats/wireframe.md) of faces built from the untrimmed surfaces' NURBS data (trimmed surfaces: refused by the caller, decision N9-28), the free edges as OccEdge
   in OCCT's order. nf faces: OccOpenSurf with the NURBS control data (weights in cp[4*i]). */
static Handle(Geom_BSplineSurface) nurbs_of(const OccOpenSurf &s) {
    if (s.nu < 2 || s.nv < 2) return nullptr;
    TColgp_Array2OfPnt P(1, s.nu, 1, s.nv); TColStd_Array2OfReal W(1, s.nu, 1, s.nv); bool rat = false;
    for (int j = 0; j < s.nv; j++) for (int i = 0; i < s.nu; i++) { const double *q = s.cp + 4 * (size_t)(j * s.nu + i); double w = q[0] > 0 ? q[0] : 1; P(i + 1, j + 1) = gp_Pnt(q[1] / w, q[2] / w, q[3] / w); W(i + 1, j + 1) = w; if (fabs(w - 1) > 1e-12) rat = true; }
    std::vector<double> uk, vk; std::vector<int> um, vm;
    for (int i = 0; i < s.nku; i++) { if (i && s.ku[i] == s.ku[i - 1]) um.back()++; else { uk.push_back(s.ku[i]); um.push_back(1); } }
    for (int i = 0; i < s.nkv; i++) { if (i && s.kv[i] == s.kv[i - 1]) vm.back()++; else { vk.push_back(s.kv[i]); vm.push_back(1); } }
    TColStd_Array1OfReal UK(1, (int)uk.size()), VK(1, (int)vk.size()); TColStd_Array1OfInteger UM(1, (int)um.size()), VM(1, (int)vm.size());
    for (size_t i = 0; i < uk.size(); i++) { UK((int)i + 1) = uk[i]; UM((int)i + 1) = um[i]; }
    for (size_t i = 0; i < vk.size(); i++) { VK((int)i + 1) = vk[i]; VM((int)i + 1) = vm[i]; }
    return rat ? new Geom_BSplineSurface(P, W, UK, VK, UM, VM, s.du, s.dv) : new Geom_BSplineSurface(P, UK, VK, UM, VM, s.du, s.dv);
}
extern "C" int occt_open_edges(const OccOpenSurf *surfs, int nsurf, double tol, OccEdge **out, int *nout, int *nsheet, char *err, size_t errn) {
    *out = nullptr; *nout = 0; if (nsheet) *nsheet = 0;
    try {
        BRepBuilderAPI_Sewing sew(tol > 0 ? tol : 1e-6, Standard_True, Standard_True, Standard_True, Standard_False);   /* cutting on: the free boundaries are cut at contact ends */
        int nf = 0;
        for (int i = 0; i < nsurf; i++) {
            const OccOpenSurf &s = surfs[i];
            Handle(Geom_BSplineSurface) g = nurbs_of(s); if (g.IsNull()) continue;
            BRepBuilderAPI_MakeFace mf(g, Precision::Confusion()); if (!mf.IsDone()) continue;
            TopoDS_Face F = mf.Face();
            sew.Add(F); nf++;
        }
        if (!nf) { *nsheet = 0; return 0; }
        sew.Perform();
        std::vector<OccEdge> v;
        int nfree = sew.NbFreeEdges();
        for (int i = 1; i <= nfree; i++) { TopoDS_Edge E = sew.FreeEdge(i); OccEdge o; if (!edge_data(E, 1, o)) continue; v.push_back(o); }
        *out = (OccEdge *)calloc(v.size() ? v.size() : 1, sizeof(OccEdge)); if (v.size()) memcpy(*out, v.data(), sizeof(OccEdge) * v.size());
        *nout = (int)v.size();
        if (nsheet) { TopoDS_Shape r = sew.SewedShape(); int c = 0; for (TopExp_Explorer ex(r, TopAbs_SHELL); ex.More(); ex.Next()) c++; *nsheet = c ? c : nf; }
        return 0;
    } catch (...) { snprintf(err, errn, "OpenCASCADE failed on these surfaces"); return -1; }
}
#else
extern "C" int occt_xt_edges(const uint8_t *, size_t, double, OccEdge **out, int *nout, int *nface, char *err, size_t errn) { *out = nullptr; *nout = 0; if (nface) *nface = 0; snprintf(err, errn, "built without OpenCASCADE"); return -1; }
extern "C" int occt_brep_edge_list(const uint8_t *, size_t, OccEdge **out, int *nout, int *nface, char *err, size_t errn) { *out = nullptr; *nout = 0; if (nface) *nface = 0; snprintf(err, errn, "built without OpenCASCADE"); return -1; }
extern "C" void occt_edges_free(OccEdge *, int) {}
extern "C" int occt_open_edges(const OccOpenSurf *, int, double, OccEdge **out, int *nout, int *nsheet, char *err, size_t errn) { *out = nullptr; *nout = 0; if (nsheet) *nsheet = 0; snprintf(err, errn, "built without OpenCASCADE"); return -1; }
#endif
