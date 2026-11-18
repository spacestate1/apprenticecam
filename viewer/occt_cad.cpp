/* occt_cad.cpp - see occt_cad.h. Glue only: OCCT's STEP / IGES readers (user decision, formats/cad_import.md), its BRep text form for the imported
 * solids, BRepMesh for their triangles. What the file's content becomes in the part is viewer/cad_import.c. */
#include "occt_cad.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>

#ifdef HAVE_OCCT
#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <GeomConvert.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Curve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Trsf.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>

bool occt_face_data(const TopoDS_Face &F, OpFace &o, std::string &err);   /* occt_prim.cpp */

struct OccCad { TopoDS_Shape s; std::vector<TopoDS_Shape> solids, shells, faces, edges, verts; };

static void E(char *err, size_t n, const char *m) { if (err && n) snprintf(err, n, "%s", m); }

static void classify(OccCad *c) {
    for (TopExp_Explorer x(c->s, TopAbs_SOLID); x.More(); x.Next()) c->solids.push_back(x.Current());
    for (TopExp_Explorer x(c->s, TopAbs_SHELL, TopAbs_SOLID); x.More(); x.Next()) c->shells.push_back(x.Current());
    for (TopExp_Explorer x(c->s, TopAbs_FACE, TopAbs_SHELL); x.More(); x.Next()) c->faces.push_back(x.Current());
    for (TopExp_Explorer x(c->s, TopAbs_EDGE, TopAbs_FACE); x.More(); x.Next()) c->edges.push_back(x.Current());
    for (TopExp_Explorer x(c->s, TopAbs_VERTEX, TopAbs_EDGE); x.More(); x.Next()) c->verts.push_back(x.Current());
}

extern "C" OccCad *occt_cad_read(const char *path, int kind, double scale, char *err, size_t errn) {
    try {
        TopoDS_Shape shape;
        if (kind == OCC_STEP) {
            STEPControl_Reader r;
            if (r.ReadFile(path) != IFSelect_RetDone) { E(err, errn, "the STEP file could not be read"); return nullptr; }
            r.TransferRoots(); shape = r.OneShape();
        } else {
            IGESControl_Reader r;
            if (r.ReadFile(path) != IFSelect_RetDone) { E(err, errn, "the IGES file could not be read"); return nullptr; }
            r.TransferRoots(); shape = r.OneShape();
        }
        if (shape.IsNull()) { E(err, errn, "no geometry was transferred from the file"); return nullptr; }
        if (scale != 1.0) { gp_Trsf t; t.SetScaleFactor(scale); shape = BRepBuilderAPI_Transform(shape, t, Standard_True).Shape(); }
        OccCad *c = new OccCad; c->s = shape; classify(c);
        return c;
    } catch (Standard_Failure &f) { if (err) snprintf(err, errn, "OpenCASCADE: %s", f.GetMessageString()); return nullptr; }
    catch (...) { E(err, errn, "OpenCASCADE failed on this file"); return nullptr; }
}
extern "C" void occt_cad_free(OccCad *c) { delete c; }
extern "C" int occt_cad_nsolids(const OccCad *c) { return (int)c->solids.size(); }
extern "C" int occt_cad_nshells(const OccCad *c) { return (int)c->shells.size(); }
extern "C" int occt_cad_nfaces(const OccCad *c) { return (int)c->faces.size(); }
extern "C" int occt_cad_nedges(const OccCad *c) { return (int)c->edges.size(); }
extern "C" int occt_cad_nverts(const OccCad *c) { return (int)c->verts.size(); }

static uint8_t *dup_str(const std::string &s, size_t *n) { uint8_t *p = (uint8_t *)malloc(s.size() + 1); memcpy(p, s.data(), s.size()); p[s.size()] = 0; *n = s.size(); return p; }
extern "C" uint8_t *occt_cad_solid_brep(const OccCad *c, int i, size_t *n) {
    if (i < 0 || i >= (int)c->solids.size()) return nullptr;
    std::ostringstream os; BRepTools::Write(c->solids[i], os);
    return dup_str(os.str(), n);
}

/* the shape's faces as occt_face_data's: BRepBuilderAPI_NurbsConvert of the shape, then each face's converted copy (as occt_xt_faces) */
static int shape_faces(const std::vector<TopoDS_Face> &faces, OpFace **out, int *nout, char *err, size_t errn) {
    *out = nullptr; *nout = 0;
    std::vector<OpFace> fs; std::string e;
    for (const TopoDS_Face &F : faces) {
        TopoDS_Face F2;
        { BRepBuilderAPI_NurbsConvert nc(F, Standard_True); TopoDS_Shape s2 = nc.Shape(); TopExp_Explorer x(s2, TopAbs_FACE); if (!x.More()) continue; F2 = TopoDS::Face(x.Current()); }
        OpFace o; if (!occt_face_data(F2, o, e)) { if (err) snprintf(err, errn, "%s", e.c_str()); for (auto &f : fs) occt_prim_free_one(&f); return -1; }
        fs.push_back(o);
    }
    *out = (OpFace *)calloc(fs.size() ? fs.size() : 1, sizeof(OpFace)); if (fs.size()) memcpy(*out, fs.data(), sizeof(OpFace) * fs.size());
    *nout = (int)fs.size();
    return 0;
}
extern "C" int occt_cad_faces(const OccCad *c, int solid, OpFace **out, int *nout, char *err, size_t errn) {
    try {
        std::vector<TopoDS_Face> faces;
        if (solid) for (auto &sh : c->solids) for (TopExp_Explorer x(sh, TopAbs_FACE); x.More(); x.Next()) faces.push_back(TopoDS::Face(x.Current()));
        else {
            for (auto &sh : c->shells) for (TopExp_Explorer x(sh, TopAbs_FACE); x.More(); x.Next()) faces.push_back(TopoDS::Face(x.Current()));
            for (auto &f : c->faces) faces.push_back(TopoDS::Face(f));
        }
        return shape_faces(faces, out, nout, err, errn);
    } catch (Standard_Failure &f) { if (err) snprintf(err, errn, "OpenCASCADE: %s", f.GetMessageString()); return -1; }
    catch (...) { E(err, errn, "OpenCASCADE failed"); return -1; }
}

bool occt_fill_curve(const TopoDS_Edge &e, OccCurve &o) {
    memset(&o, 0, sizeof o);
    Standard_Real f, l; if (BRep_Tool::Degenerated(e)) return false;
    Handle(Geom_Curve) g = BRep_Tool::Curve(e, f, l); if (g.IsNull()) return false;
    BRepAdaptor_Curve a(e);
    bool rev = e.Orientation() == TopAbs_REVERSED;
    gp_Pnt p0 = a.Value(a.FirstParameter()), p1 = a.Value(a.LastParameter());
    if (rev) std::swap(p0, p1);
    o.p[0] = p0.X(); o.p[1] = p0.Y(); o.p[2] = p0.Z(); o.p[3] = p1.X(); o.p[4] = p1.Y(); o.p[5] = p1.Z();
    if (a.GetType() == GeomAbs_Line) { o.type = 1; return true; }
    if (a.GetType() == GeomAbs_Circle) {
        gp_Circ ci = a.Circle(); gp_Ax2 ax = ci.Position();
        o.type = 2; o.r = ci.Radius();
        o.c[0] = ax.Location().X(); o.c[1] = ax.Location().Y(); o.c[2] = ax.Location().Z();
        o.n[0] = ax.Direction().X(); o.n[1] = ax.Direction().Y(); o.n[2] = ax.Direction().Z();
        o.xd[0] = ax.XDirection().X(); o.xd[1] = ax.XDirection().Y(); o.xd[2] = ax.XDirection().Z();
        double t0 = a.FirstParameter(), t1 = a.LastParameter();
        if (rev) { for (int k = 0; k < 3; k++) o.n[k] = -o.n[k]; double tt = 2 * M_PI - t1; t1 = 2 * M_PI - t0; t0 = tt; }
        o.a0 = t0; o.sw = t1 - t0; return true;
    }
    Handle(Geom_Curve) tc = new Geom_TrimmedCurve(g, f, l);
    if (rev) tc->Reverse();
    Handle(Geom_BSplineCurve) b = GeomConvert::CurveToBSplineCurve(tc);
    if (b.IsNull()) return false;
    if (b->IsPeriodic()) b->SetNotPeriodic();
    o.type = 3; o.deg = b->Degree(); o.nc = b->NbPoles(); o.rat = b->IsRational();
    int nk = b->NbKnots(); int total = 0; for (int i = 1; i <= nk; i++) total += b->Multiplicity(i);
    o.nk = total; o.k = (double *)malloc(sizeof(double) * total); o.cp = (double *)malloc(sizeof(double) * 4 * o.nc);
    int w = 0; for (int i = 1; i <= nk; i++) for (int j = 0; j < b->Multiplicity(i); j++) o.k[w++] = b->Knot(i);
    for (int i = 1; i <= o.nc; i++) { gp_Pnt p = b->Pole(i); double wt = b->Weight(i); double *q = o.cp + 4 * (i - 1); q[0] = wt; q[1] = wt * p.X(); q[2] = wt * p.Y(); q[3] = wt * p.Z(); }
    return true;
}
extern "C" int occt_cad_curves(const OccCad *c, int mode, OccCurve **out, int *n, char *err, size_t errn) {
    *out = nullptr; *n = 0;
    try {
        std::vector<OccCurve> v; OccCurve o;
        if (mode == 0) {
            TopTools_IndexedMapOfShape em;
            for (auto &sh : c->solids) TopExp::MapShapes(sh, TopAbs_EDGE, em);
            for (auto &sh : c->shells) TopExp::MapShapes(sh, TopAbs_EDGE, em);
            for (auto &sh : c->faces) TopExp::MapShapes(sh, TopAbs_EDGE, em);
            for (int i = 1; i <= em.Extent(); i++) if (occt_fill_curve(TopoDS::Edge(em(i)), o)) v.push_back(o);
        } else {
            for (auto &e : c->edges) if (occt_fill_curve(TopoDS::Edge(e), o)) v.push_back(o);
        }
        *out = (OccCurve *)calloc(v.size() ? v.size() : 1, sizeof(OccCurve)); if (v.size()) memcpy(*out, v.data(), sizeof(OccCurve) * v.size()); *n = (int)v.size();
        return 0;
    } catch (Standard_Failure &f) { if (err) snprintf(err, errn, "OpenCASCADE: %s", f.GetMessageString()); return -1; }
    catch (...) { E(err, errn, "OpenCASCADE failed"); return -1; }
}
extern "C" void occt_cad_curves_free(OccCurve *o, int n) { for (int i = 0; i < n; i++) { free(o[i].k); free(o[i].cp); } free(o); }
extern "C" int occt_cad_points(const OccCad *c, double **xyz, int *n) {
    *n = (int)c->verts.size(); *xyz = (double *)malloc(sizeof(double) * 3 * (*n ? *n : 1));
    for (int i = 0; i < *n; i++) { gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(c->verts[i])); (*xyz)[3 * i] = p.X(); (*xyz)[3 * i + 1] = p.Y(); (*xyz)[3 * i + 2] = p.Z(); }
    return *n;
}

/* ---- BRep blobs: a small cache of the parsed shapes by content hash */
static uint64_t fnv(const uint8_t *p, size_t n) { uint64_t h = 1469598103934665603ULL; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; } return h; }
bool occt_shape_of(const uint8_t *b, size_t n, TopoDS_Shape &s) {
    static std::map<uint64_t, TopoDS_Shape> cache;
    uint64_t h = fnv(b, n); auto it = cache.find(h); if (it != cache.end()) { s = it->second; return true; }
    std::string str((const char *)b, n); std::istringstream is(str); BRep_Builder B; BRepTools::Read(s, is, B);
    if (s.IsNull()) return false;
    cache[h] = s;
    return true;
}
static void mesh_of(const TopoDS_Shape &s, double defl) { BRepMesh_IncrementalMesh m(s, defl > 0 ? defl : 0.02, Standard_False, 0.5, Standard_False); }

extern "C" int occt_brep_mesh(const uint8_t *b, size_t n, double deflection, float **tri, char *err, size_t errn) {
    *tri = nullptr;
    try {
        TopoDS_Shape s; if (!occt_shape_of(b, n, s)) { E(err, errn, "bad BRep blob"); return -1; }
        mesh_of(s, deflection);
        std::vector<float> out; int nt = 0;
        for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face &F = TopoDS::Face(ex.Current()); TopLoc_Location loc;
            Handle(Poly_Triangulation) T = BRep_Tool::Triangulation(F, loc); if (T.IsNull()) continue;
            bool rev = F.Orientation() == TopAbs_REVERSED;
            for (int i = 1; i <= T->NbTriangles(); i++) {
                int a, c, d; T->Triangle(i).Get(a, c, d); if (rev) std::swap(c, d);
                gp_Pnt p[3] = { T->Node(a).Transformed(loc.Transformation()), T->Node(c).Transformed(loc.Transformation()), T->Node(d).Transformed(loc.Transformation()) };
                gp_Vec u(p[0], p[1]), v(p[0], p[2]); gp_Vec nm = u.Crossed(v); double l = nm.Magnitude(); if (l > 0) nm /= l;
                for (int k = 0; k < 3; k++) { out.push_back((float)p[k].X()); out.push_back((float)p[k].Y()); out.push_back((float)p[k].Z()); out.push_back((float)nm.X()); out.push_back((float)nm.Y()); out.push_back((float)nm.Z()); }
                nt++;
            }
        }
        *tri = (float *)malloc(sizeof(float) * (out.size() ? out.size() : 1)); if (out.size()) memcpy(*tri, out.data(), sizeof(float) * out.size());
        return nt;
    } catch (...) { E(err, errn, "OpenCASCADE failed on this solid"); return -1; }
}
extern "C" int occt_brep_drive(const uint8_t *b, size_t n, double deflection, double **tri, char *err, size_t errn) {
    *tri = nullptr;
    try {
        TopoDS_Shape s; if (!occt_shape_of(b, n, s)) { E(err, errn, "bad BRep blob"); return -1; }
        mesh_of(s, deflection);
        std::vector<double> out; int nt = 0;
        for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face &F = TopoDS::Face(ex.Current()); TopLoc_Location loc;
            Handle(Poly_Triangulation) T = BRep_Tool::Triangulation(F, loc); if (T.IsNull()) continue;
            bool rev = F.Orientation() == TopAbs_REVERSED;
            for (int i = 1; i <= T->NbTriangles(); i++) {
                int a, c, d; T->Triangle(i).Get(a, c, d); if (rev) std::swap(c, d);
                for (int k : { a, c, d }) { gp_Pnt p = T->Node(k).Transformed(loc.Transformation()); out.push_back(p.X()); out.push_back(p.Y()); out.push_back(p.Z()); }
                nt++;
            }
        }
        *tri = (double *)malloc(sizeof(double) * (out.size() ? out.size() : 1)); if (out.size()) memcpy(*tri, out.data(), sizeof(double) * out.size());
        return nt;
    } catch (...) { E(err, errn, "OpenCASCADE failed on this solid"); return -1; }
}
extern "C" int occt_brep_body_type(const uint8_t *b, size_t n) {
    try { TopoDS_Shape s; if (!occt_shape_of(b, n, s)) return -1; return s.ShapeType() == TopAbs_SOLID ? 1 : 3; } catch (...) { return -1; }
}
/* the kept edges (lines and circles, as xt_brep_edges) and the map edge -> its number there */
static void kept_edges(const TopoDS_Shape &s, TopTools_IndexedMapOfShape &em, std::vector<int> &num, int *nskip) {
    TopExp::MapShapes(s, TopAbs_EDGE, em); num.assign(em.Extent() + 1, -1); int k = 0; if (nskip) *nskip = 0;
    for (int i = 1; i <= em.Extent(); i++) {
        if (BRep_Tool::Degenerated(TopoDS::Edge(em(i)))) { if (nskip) (*nskip)++; continue; }
        BRepAdaptor_Curve a(TopoDS::Edge(em(i)));
        if (a.GetType() == GeomAbs_Line || a.GetType() == GeomAbs_Circle) num[i] = k++; else if (nskip) (*nskip)++;
    }
}
extern "C" int occt_brep_edges(const uint8_t *b, size_t n, XtBEdge **out, int *nskip) {
    *out = nullptr;
    try {
        TopoDS_Shape s; if (!occt_shape_of(b, n, s)) return 0;
        TopTools_IndexedMapOfShape em; std::vector<int> num; kept_edges(s, em, num, nskip);
        int cnt = 0; for (int i = 1; i <= em.Extent(); i++) if (num[i] >= 0) cnt++;
        *out = (XtBEdge *)calloc(cnt ? cnt : 1, sizeof(XtBEdge));
        for (int i = 1; i <= em.Extent(); i++) if (num[i] >= 0) {
            XtBEdge *e = &(*out)[num[i]]; const TopoDS_Edge &E2 = TopoDS::Edge(em(i)); BRepAdaptor_Curve a(E2);
            if (a.GetType() == GeomAbs_Line) {
                e->arc = 0; e->np = 2; e->pts = (double *)malloc(sizeof(double) * 6);
                gp_Pnt p0 = a.Value(a.FirstParameter()), p1 = a.Value(a.LastParameter());
                e->pts[0] = p0.X(); e->pts[1] = p0.Y(); e->pts[2] = p0.Z(); e->pts[3] = p1.X(); e->pts[4] = p1.Y(); e->pts[5] = p1.Z();
            } else {
                gp_Circ ci = a.Circle(); gp_Ax2 ax = ci.Position(); double t0 = a.FirstParameter(), t1 = a.LastParameter();
                bool ring = fabs(t1 - t0 - 2 * M_PI) < 1e-7; e->arc = ring ? 2 : 1;
                e->c[0] = ax.Location().X(); e->c[1] = ax.Location().Y(); e->c[2] = ax.Location().Z();
                e->ax[0] = ax.Direction().X(); e->ax[1] = ax.Direction().Y(); e->ax[2] = ax.Direction().Z();
                e->xa[0] = ax.XDirection().X(); e->xa[1] = ax.XDirection().Y(); e->xa[2] = ax.XDirection().Z();
                e->r = ci.Radius(); e->t0 = ring ? 0 : t0; e->t1 = ring ? 2 * M_PI : t1;
                int steps = (int)std::ceil(fabs(e->t1 - e->t0) / (5.0 * M_PI / 180)); if (steps < 1) steps = 1;
                e->np = steps + 1; e->pts = (double *)malloc(sizeof(double) * 3 * e->np);
                for (int k = 0; k <= steps; k++) { gp_Pnt p = a.Value(a.FirstParameter() + (a.LastParameter() - a.FirstParameter()) * k / steps); e->pts[3 * k] = p.X(); e->pts[3 * k + 1] = p.Y(); e->pts[3 * k + 2] = p.Z(); }
            }
        }
        return cnt;
    } catch (...) { return 0; }
}
extern "C" int occt_brep_topo(const uint8_t *b, size_t n, BTopo *t) {
    memset(t, 0, sizeof *t);
    try {
        TopoDS_Shape s; if (!occt_shape_of(b, n, s)) return 0;
        TopTools_IndexedMapOfShape em; std::vector<int> num; kept_edges(s, em, num, nullptr);
        mesh_of(s, 0.02);
        std::vector<TopoDS_Face> faces; for (TopExp_Explorer x(s, TopAbs_FACE); x.More(); x.Next()) faces.push_back(TopoDS::Face(x.Current()));
        t->face = (TFace *)calloc(faces.size() ? faces.size() : 1, sizeof(TFace)); t->nface = (int)faces.size();
        std::vector<TLoop> loops; std::vector<TFin> fins;
        for (int fi = 0; fi < (int)faces.size(); fi++) {
            const TopoDS_Face &F = faces[fi]; TFace *tf = &t->face[fi]; tf->l0 = (int)loops.size();
            bool rev = F.Orientation() == TopAbs_REVERSED;
            BRepAdaptor_Surface sf(F);
            if (sf.GetType() == GeomAbs_Plane) { gp_Dir d = sf.Plane().Axis().Direction(); if (!sf.Plane().Direct()) d.Reverse(); if (rev) d.Reverse(); tf->nrm[0] = d.X(); tf->nrm[1] = d.Y(); tf->nrm[2] = d.Z(); tf->has_nrm = 1; }
            TopLoc_Location loc; Handle(Poly_Triangulation) T = BRep_Tool::Triangulation(F, loc);
            if (!T.IsNull()) {
                tf->ntri = T->NbTriangles(); tf->tri = (float *)malloc(sizeof(float) * 9 * tf->ntri);
                for (int i = 1; i <= tf->ntri; i++) { int a, c, d; T->Triangle(i).Get(a, c, d); int v[3] = { a, c, d }; for (int k = 0; k < 3; k++) { gp_Pnt p = T->Node(v[k]).Transformed(loc.Transformation()); float *q = tf->tri + 9 * (i - 1) + 3 * k; q[0] = (float)p.X(); q[1] = (float)p.Y(); q[2] = (float)p.Z(); } }
            }
            TopoDS_Wire outer = BRepTools::OuterWire(F);
            std::vector<TopoDS_Wire> ws; if (!outer.IsNull()) ws.push_back(outer);
            for (TopExp_Explorer wx(F, TopAbs_WIRE); wx.More(); wx.Next()) if (!wx.Current().IsSame(outer)) ws.push_back(TopoDS::Wire(wx.Current()));
            for (const TopoDS_Wire &W : ws) {
                std::vector<TFin> lf;
                for (BRepTools_WireExplorer wx(W, F); wx.More(); wx.Next()) {
                    const TopoDS_Edge &E2 = wx.Current();
                    int idx = em.FindIndex(E2); TFin fn; fn.e = idx >= 1 && idx <= em.Extent() ? num[idx] : -1;
                    bool along = wx.Orientation() == TopAbs_FORWARD; fn.pos = along ? 1 : 0;
                    lf.push_back(fn);
                }
                if (lf.empty()) continue;
                if (rev) { std::reverse(lf.begin(), lf.end()); for (auto &q : lf) q.pos = !q.pos; }
                TLoop L; L.f0 = (int)fins.size(); L.nf = (int)lf.size(); L.face = fi; loops.push_back(L);
                fins.insert(fins.end(), lf.begin(), lf.end());
            }
            tf->nl = (int)loops.size() - tf->l0;
        }
        t->loop = (TLoop *)malloc(sizeof(TLoop) * (loops.size() ? loops.size() : 1)); t->nloop = (int)loops.size(); if (loops.size()) memcpy(t->loop, loops.data(), sizeof(TLoop) * loops.size());
        t->fin = (TFin *)malloc(sizeof(TFin) * (fins.size() ? fins.size() : 1)); t->nfin = (int)fins.size(); if (fins.size()) memcpy(t->fin, fins.data(), sizeof(TFin) * fins.size());
        return t->nface;
    } catch (...) { btopo_free(t); return 0; }
}
extern "C" int occt_brep_faces(const uint8_t *b, size_t n, OpFace **out, int *nout, char *err, size_t errn) {
    try {
        TopoDS_Shape s; if (!occt_shape_of(b, n, s)) { E(err, errn, "bad BRep blob"); return -1; }
        std::vector<TopoDS_Face> faces; for (TopExp_Explorer x(s, TopAbs_FACE); x.More(); x.Next()) faces.push_back(TopoDS::Face(x.Current()));
        return shape_faces(faces, out, nout, err, errn);
    } catch (...) { E(err, errn, "OpenCASCADE failed"); return -1; }
}
#else
extern "C" OccCad *occt_cad_read(const char *, int, double, char *err, size_t errn) { if (err) snprintf(err, errn, "built without OpenCASCADE"); return nullptr; }
extern "C" void occt_cad_free(OccCad *) {}
extern "C" int occt_cad_nsolids(const OccCad *) { return 0; }
extern "C" int occt_cad_nshells(const OccCad *) { return 0; }
extern "C" int occt_cad_nfaces(const OccCad *) { return 0; }
extern "C" int occt_cad_nedges(const OccCad *) { return 0; }
extern "C" int occt_cad_nverts(const OccCad *) { return 0; }
extern "C" uint8_t *occt_cad_solid_brep(const OccCad *, int, size_t *) { return nullptr; }
extern "C" int occt_cad_faces(const OccCad *, int, OpFace **, int *, char *err, size_t errn) { if (err) snprintf(err, errn, "built without OpenCASCADE"); return -1; }
extern "C" int occt_cad_curves(const OccCad *, int, OccCurve **, int *, char *err, size_t errn) { if (err) snprintf(err, errn, "built without OpenCASCADE"); return -1; }
extern "C" void occt_cad_curves_free(OccCurve *, int) {}
extern "C" int occt_cad_points(const OccCad *, double **, int *n) { *n = 0; return 0; }
extern "C" int occt_brep_mesh(const uint8_t *, size_t, double, float **, char *err, size_t errn) { if (err) snprintf(err, errn, "built without OpenCASCADE"); return -1; }
extern "C" int occt_brep_drive(const uint8_t *, size_t, double, double **, char *err, size_t errn) { if (err) snprintf(err, errn, "built without OpenCASCADE"); return -1; }
extern "C" int occt_brep_edges(const uint8_t *, size_t, XtBEdge **, int *) { return 0; }
extern "C" int occt_brep_topo(const uint8_t *, size_t, BTopo *) { return 0; }
extern "C" int occt_brep_faces(const uint8_t *, size_t, OpFace **, int *, char *err, size_t errn) { if (err) snprintf(err, errn, "built without OpenCASCADE"); return -1; }
extern "C" int occt_brep_body_type(const uint8_t *, size_t) { return -1; }
#endif
