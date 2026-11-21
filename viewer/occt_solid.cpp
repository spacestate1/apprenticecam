/* occt_solid - the solids made here on OpenCASCADE (occt_solid.h). Sources:
 *   formats/extrude.md 4 (sld_extrude 0x1800d5730: direction 4.1, distance 4.2, Both / Draft 4.3, grouping 4.5, thin wall 4.6, bodies and
 *   booleans 4.7, ps_arc_to_ps_arc 4.9), formats/solid_history.md 3 (McEptr names; 3.3 / 3.4 the Create and Cut / Boss rules decoded
 *   2026-11-18 from ps_sweep_sheet 0x1800530c0, FUN_18004a8f0, FUN_1800c6580, FUN_18003e630, FUN_18004a4b0, FUN_18004b9b0, FUN_1802656b0),
 *   formats/occt_xt.md (the XT writer viewer/occt/occt_xt.cpp).
 * Ours (labelled where used): OCCT stands in for Parasolid (user decision SOL-3): its booleans' face splitting and its history (Modified /
 * Generated / IsDeleted) stand in for Parasolid's tracking; chain gaps closed by one shared vertex per joint (formats/occt_xt.md 5);
 * the order of names inside a kind group (Parasolid's PK_BODY_ask_edges / ask_faces order is NOT DECODED); the display mesh / edge
 * discretisation; draft and thin wall geometry conventions that extrude.md marks NOT DECODED keep this program's earlier choices. */
#include "occt_solid.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef HAVE_OCCT
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include "occt/occt_xt.h"
#include "occt/xt_occt.h"
extern "C" {
#include "xt_mesh.h"
}
#include <BRepAlgoAPI_Common.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepLib.hxx>
#include <Geom_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GProp_GProps.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Line.hxx>
#include <Geom_Plane.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_DataMapOfShapeInteger.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Ax2d.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>

/* McEptr kinds (formats/solid_history.md 3.2, as corrected 2026-11-18: ps_sweep_sheet / FUN_18004a8f0) */
enum : uint32_t {
    K_CAP_EDGE_START = 0x01,     /* a profile (sheet) edge: the edge in the chain plane, ref1 = the chain entity */
    K_CAP_EDGE_END = 0x04,       /* its swept copy on the end cap, ref1 = the entity */
    K_LATERAL_EDGE = 0x13,       /* the edge swept from a profile vertex (0x8000013), refs = the records of its two faces */
    K_NEW_EDGE = 0x18,           /* an edge made by a boolean (FUN_18003e630 0x8000018), refs = its two faces' records */
    K_SIDE_FACE = 0x1f,          /* the face swept from a chain entity, ref1 = the entity */
    K_SPLIT_EDGE = 0x2d,         /* a piece of a split edge (FUN_18004b9b0), ref1 = the record it carried before */
    K_SPLIT_FACE = 0x33,         /* a piece of a split face, ref1 = the old record */
    K_UNNAMED_FACE = 0x34,       /* a face without a name (FUN_1802656b0; edge: 0) */
    K_CAP_FACE = 0x38,           /* the start / end cap faces */
    K_TOOL = 0x10000000          /* a tool body's name in the n2 list (FUN_1800c6580) */
};

struct OcsBody {
    int ref = 1;
    TopoDS_Shape s;
    std::vector<OcsRec> recs;
    struct Op { std::vector<int> l[3]; bool named = true; std::string why; int grp = -1, tag = -1; };   /* grp: the input group (-1 all) */
    std::vector<Op> ops;
    TopTools_DataMapOfShapeInteger nm;       /* face / edge (TShape + Location) -> record */
};

namespace {


/* user decision SOL-5c (2026-11-18): the booleans run with Parasolid's tolerance - FUN_18003e630 (ps_boolean_union / _cut / _isect ->
   PK_BODY_boolean_2) fills its options with 1e-5 (0x3ee4f8b588e368f1, decomp/solid_history/h13_boolean_naming.c l.127 / 136 / 192; the
   model is in metres, formats/occt_3d_plan.md S7) - as OCCT's fuzzy value in the part's units (0.01 mm, 3.937e-4 in) */
double g_fuzzy = 0;
/* NOT the default (2026-11-18, question back to the user): OCCT's fuzzy value merges geometry within it, and Through all extends the tool by
   exactly 1000e-5 mm (= 1e-5 m, extrude.md 4.2) past the target - the tool's end cap then fuses with the target's face (wrong hole, edges
   off their faces by 0.01 mm). Replay of the user parts: names as stored 277 -> 226 / 295 ops, volumes equal 83 -> 66 / 108, 2 failures;
   test-cube's stored cut has a NEW edge (kind 24) at the bottom, i.e. Parasolid did not merge there. So Parasolid's 1e-5 option is not an
   OCCT fuzzy value; MCAM_OCS_FUZZY=1 turns it on for comparison. */
void set_fuzzy(int metric) { const char *e = getenv("MCAM_OCS_FUZZY"); g_fuzzy = e && atoi(e) ? 1e-5 * (metric ? 1000.0 : 39.37007874015748) : 0; }
TopoDS_Shape boolop(int kind, const TopoDS_Shape &a, const TopoDS_Shape &b, bool &ok) {   /* kind 0 fuse, 1 cut, 2 common */
    std::unique_ptr<BRepAlgoAPI_BooleanOperation> o;
    if (kind == 0) o.reset(new BRepAlgoAPI_Fuse()); else if (kind == 1) o.reset(new BRepAlgoAPI_Cut()); else o.reset(new BRepAlgoAPI_Common());
    TopTools_ListOfShape A, B; A.Append(a); B.Append(b);
    o->SetArguments(A); o->SetTools(B); if (g_fuzzy > 0) o->SetFuzzyValue(g_fuzzy); o->Build();
    ok = !o->HasErrors(); return ok ? o->Shape() : TopoDS_Shape();
}
double volume_of(const TopoDS_Shape &s) { GProp_GProps g; BRepGProp::VolumeProperties(s, g); return g.Mass(); }

/* the record list helpers */
int add_rec(OcsBody &b, uint32_t kind, int op, int r1t = OCS_REF_NONE, int64_t r1 = 0, int r2t = OCS_REF_NONE, int64_t r2 = 0) {
    OcsRec r; r.kind = kind; r.op = op; r.r1t = r1t; r.r1 = r1; r.r2t = r2t; r.r2 = r2;
    b.recs.push_back(r); return (int)b.recs.size() - 1;
}
int rec_of(const OcsBody &b, const TopoDS_Shape &s) { return b.nm.IsBound(s) ? b.nm.Find(s) : -1; }

/* ---------------------------------------------------------------- chains -> profile faces */
struct Prof {                                   /* a chain built as edges: the edge of each entity + its joint vertices */
    std::vector<TopoDS_Edge> e;                 /* per entity, in the curve's own direction */
    std::vector<int> ent;                       /* the entity of e[i] */
    std::vector<TopoDS_Vertex> joint;           /* joint[i] = between entity i-1 and i (closed: n joints; open: n + 1) */
    TopoDS_Wire w;                              /* in chain order */
    std::vector<gp_Pnt> pts;                    /* sample points in chain order (orientation / nesting) */
    bool closed = false;
};

Handle(Geom_Curve) seg_curve(const OcsSeg &s, double &f, double &l) {
    if (s.type == 1) {
        gp_Pnt a(s.p0[0], s.p0[1], s.p0[2]), b(s.p1[0], s.p1[1], s.p1[2]);
        f = 0; l = a.Distance(b);
        if (l < 1e-12) return nullptr;
        return new Geom_Line(a, gp_Dir(gp_Vec(a, b)));
    }
    /* formats/extrude.md 4.9: ps_arc_to_ps_arc - PK_CIRCLE centre = the arc centre, axis = the view's Z row (negated for a negative sweep),
       ref_direction = the view's X row; the entity's interval [t0, t1] on it */
    gp_Ax2 ax(gp_Pnt(s.c[0], s.c[1], s.c[2]), gp_Dir(s.ax[0], s.ax[1], s.ax[2]), gp_Dir(s.xd[0], s.xd[1], s.xd[2]));
    f = s.t0; l = s.t1;
    return new Geom_Circle(ax, s.r);
}

/* ours (formats/occt_xt.md 5, sld_regen): Mastercam chains within its chaining tolerance; the joints of the chain become one shared vertex at
   the gap's midpoint with tolerance half the gap (chain_to_pock_bound's gap closing is NOT DECODED). The entities' curves are kept exactly. */
bool build_prof(const OcsChain &c, Prof &p, std::string &err) {
    int n = c.n; if (n < 1) { err = "empty chain"; return false; }
    std::vector<Handle(Geom_Curve)> C(n); std::vector<double> F(n), L(n); std::vector<gp_Pnt> ts(n), te(n);
    for (int i = 0; i < n; i++) {
        C[i] = seg_curve(c.s[i], F[i], L[i]);
        if (C[i].IsNull()) { err = "degenerate chain entity"; return false; }
        gp_Pnt a = C[i]->Value(F[i]), b = C[i]->Value(L[i]);
        ts[i] = c.s[i].rev ? b : a; te[i] = c.s[i].rev ? a : b;
    }
    p.closed = c.closed != 0;
    int nj = p.closed ? n : n + 1;
    p.joint.resize(nj);
    BRep_Builder B; double maxgap = 0;
    for (int k = 0; k < nj; k++) {
        gp_Pnt a, b;
        if (p.closed) { a = te[(k + n - 1) % n]; b = ts[k]; }
        else if (k == 0) { a = b = ts[0]; }
        else if (k == n) { a = b = te[n - 1]; }
        else { a = te[k - 1]; b = ts[k]; }
        double g = a.Distance(b); maxgap = std::max(maxgap, g);
        B.MakeVertex(p.joint[k], gp_Pnt((a.XYZ() + b.XYZ()) / 2), std::max(1e-7, g / 2 + 1e-7));
    }
    if (maxgap > 0.01) { err = "chain gap > 0.01"; return false; }
    BRepBuilderAPI_MakeWire mw;
    for (int i = 0; i < n; i++) {
        TopoDS_Vertex vs = p.joint[i], ve = p.joint[p.closed ? (i + 1) % n : i + 1];
        TopoDS_Vertex v0 = c.s[i].rev ? ve : vs, v1 = c.s[i].rev ? vs : ve;      /* at the curve's first / last parameter */
        TopoDS_Edge E; B.MakeEdge(E, C[i], 1e-7);
        B.Add(E, v0.Oriented(TopAbs_FORWARD)); B.Add(E, v1.Oriented(TopAbs_REVERSED)); B.Range(E, F[i], L[i]);
        p.e.push_back(E); p.ent.push_back(c.s[i].ent);
        mw.Add(c.s[i].rev ? TopoDS::Edge(E.Reversed()) : E);
        if (!mw.IsDone()) { err = "chain not connected"; return false; }
        for (int k = 0; k < 16; k++) {
            double t = F[i] + (L[i] - F[i]) * k / 16.0; if (c.s[i].rev) t = L[i] - (L[i] - F[i]) * k / 16.0;
            p.pts.push_back(C[i]->Value(t));
        }
    }
    p.w = mw.Wire();
    return true;
}

double signed_area(const std::vector<gp_Pnt> &pts, const gp_Ax3 &pl) {   /* in the plane's coordinates, about its normal */
    double a = 0; size_t n = pts.size();
    gp_Vec X(pl.XDirection()), Y(pl.YDirection()); gp_Pnt O = pl.Location();
    for (size_t i = 0; i < n; i++) {
        gp_Vec p(O, pts[i]), q(O, pts[(i + 1) % n]);
        a += p.Dot(X) * q.Dot(Y) - q.Dot(X) * p.Dot(Y);
    }
    return a / 2;
}

/* the group's profile face: ch[0] the root loop, the rest its holes (formats/extrude.md 4.5). The plane's normal = dir side (ours: the face
   orientation does not change the prism's volume). */
bool profile_face(std::vector<Prof> &ps, const gp_Dir &dir, TopoDS_Face &face, std::string &err) {
    gp_Pnt o = ps[0].pts[0];
    gp_Ax3 pl(o, dir);
    /* the plane must hold the chain (extrude.md: sld 657 "Operation requires planar chain set", checked by the caller) */
    TopoDS_Wire outer = ps[0].w;
    if (signed_area(ps[0].pts, pl) < 0) outer.Reverse();
    BRepBuilderAPI_MakeFace mf(gp_Pln(pl), outer, true);
    if (!mf.IsDone()) { err = "profile face"; return false; }
    for (size_t k = 1; k < ps.size(); k++) {
        TopoDS_Wire h = ps[k].w;
        if (signed_area(ps[k].pts, pl) > 0) h.Reverse();
        mf.Add(h);
    }
    face = mf.Face();
    if (!BRepCheck_Analyzer(face).IsValid()) { err = "profile face not valid"; return false; }
    return true;
}

/* ---------------------------------------------------------------- the swept tool + its names (ps_sweep_sheet) */
struct Tool {
    TopoDS_Shape s;
    std::vector<int> edges, faces;              /* its records: edges then faces (FUN_18004a8f0 order) */
    bool named = true; std::string why;
};

/* Create's names of one swept group (formats/solid_history.md 3.2 / 3.3): lateral edges 0x13 (refs = its two side faces), end cap edges 4,
   start cap edges 1 (ref1 = entity), side faces 0x1f (ref1 = entity), the two cap faces 0x38. Kind order as Mastercam's records (data:
   [19 x N, 4 x N, 1 x N | 31 x N, 56, 56]); the order inside a kind = chain order, joints from the last -> first one (ours). */
void name_prism(OcsBody &b, BRepPrimAPI_MakePrism &mp, const std::vector<Prof> &ps, int op, uint32_t flag, Tool &t) {
    std::vector<std::vector<int>> side(ps.size());
    std::vector<int> lat, endc, startc, sidef;
    /* faces first (the lateral edges refer to them); listed after the edges */
    for (size_t c = 0; c < ps.size(); c++)
        for (size_t i = 0; i < ps[c].e.size(); i++) {
            TopTools_ListOfShape G = mp.Generated(ps[c].e[i]);   /* a copy: Generated() returns a list it overwrites on the next call */
            int r = -1;
            for (TopTools_ListIteratorOfListOfShape it(G); it.More(); it.Next())
                if (it.Value().ShapeType() == TopAbs_FACE) { r = add_rec(b, K_SIDE_FACE | flag, op, OCS_REF_ENT, ps[c].ent[i]); b.nm.Bind(it.Value(), r); break; }
            side[c].push_back(r); if (r >= 0) sidef.push_back(r);
        }
    for (size_t c = 0; c < ps.size(); c++) {
        size_t n = ps[c].e.size();
        for (size_t k = 0; k < ps[c].joint.size(); k++) {
            if (!ps[c].closed && (k == 0 || k == n)) {   /* an open chain's end vertex (thin wall: not named here) */ }
            TopTools_ListOfShape G = mp.Generated(ps[c].joint[k]);   /* a copy (see above) */
            for (TopTools_ListIteratorOfListOfShape it(G); it.More(); it.Next()) {
                if (it.Value().ShapeType() != TopAbs_EDGE) continue;
                TopoDS_Edge E = TopoDS::Edge(it.Value());
                if (BRep_Tool::Degenerated(E)) continue;
                size_t prev = ps[c].closed ? (k + n - 1) % n : k - 1, next = k;
                /* a seam (the vertex of a full circle: the edge lies twice on its cylinder) is no Parasolid edge: no name */
                bool seam = false;
                for (int f : { (int)prev, (int)next }) if (f >= 0 && f < (int)n && side[c][f] >= 0) {
                    TopTools_ListOfShape G2 = mp.Generated(ps[c].e[f]);
                    for (TopTools_ListIteratorOfListOfShape g2(G2); g2.More(); g2.Next())
                        if (g2.Value().ShapeType() == TopAbs_FACE && BRep_Tool::IsClosed(E, TopoDS::Face(g2.Value()))) seam = true;
                }
                if (seam) continue;
                int r1 = prev < n ? side[c][prev] : -1, r2 = next < n ? side[c][next] : -1;
                int r = add_rec(b, K_LATERAL_EDGE | flag, op, r1 >= 0 ? OCS_REF_REC : OCS_REF_NONE, r1, r2 >= 0 ? OCS_REF_REC : OCS_REF_NONE, r2);
                b.nm.Bind(E, r); lat.push_back(r);
                break;
            }
        }
    }
    for (size_t c = 0; c < ps.size(); c++)
        for (size_t i = 0; i < ps[c].e.size(); i++) {
            TopoDS_Shape L = mp.LastShape(ps[c].e[i]);
            if (!L.IsNull()) { int r = add_rec(b, K_CAP_EDGE_END | flag, op, OCS_REF_ENT, ps[c].ent[i]); b.nm.Bind(L, r); endc.push_back(r); }
        }
    for (size_t c = 0; c < ps.size(); c++)
        for (size_t i = 0; i < ps[c].e.size(); i++) {
            TopoDS_Shape S0 = mp.FirstShape(ps[c].e[i]);
            if (!S0.IsNull()) { int r = add_rec(b, K_CAP_EDGE_START | flag, op, OCS_REF_ENT, ps[c].ent[i]); b.nm.Bind(S0, r); startc.push_back(r); }
        }
    int f0 = add_rec(b, K_CAP_FACE | flag, op), f1 = add_rec(b, K_CAP_FACE | flag, op);
    b.nm.Bind(mp.FirstShape(), f0); b.nm.Bind(mp.LastShape(), f1);
    t.edges = lat; t.edges.insert(t.edges.end(), endc.begin(), endc.end()); t.edges.insert(t.edges.end(), startc.begin(), startc.end());
    t.faces = sidef; t.faces.push_back(f0); t.faces.push_back(f1);
}

/* 4.2: Cut + Through all distances from the target's box (extended by 1000e-5 metric / 39.37007874015748e-5 inch) projected on the
   direction, from every chain point: d1 = max(box max - point), d2 = max(point - box min); a result <= tol -> 1.0 */
void through_all(const TopoDS_Shape &target, const std::vector<Prof> &ps, const gp_Dir &d, int metric, double tol, double &d1, double &d2) {
    Bnd_Box bb; BRepBndLib::AddOptimal(target, bb, false, false);
    double x0, y0, z0, x1, y1, z1; bb.Get(x0, y0, z0, x1, y1, z1);
    double mg = metric ? 1000e-5 : 39.37007874015748e-5;
    x0 -= mg; y0 -= mg; z0 -= mg; x1 += mg; y1 += mg; z1 += mg;
    double hi = -1e300, lo = 1e300;
    for (int k = 0; k < 8; k++) { gp_XYZ c(k & 1 ? x1 : x0, k & 2 ? y1 : y0, k & 4 ? z1 : z0); double v = c.Dot(d.XYZ()); hi = std::max(hi, v); lo = std::min(lo, v); }
    d1 = -1e300; d2 = -1e300;
    for (auto &p : ps) for (auto &q : p.pts) { double v = q.XYZ().Dot(d.XYZ()); d1 = std::max(d1, hi - v); d2 = std::max(d2, v - lo); }
    if (d1 <= tol) d1 = 1.0;
    if (d2 <= tol) d2 = 1.0;
}

/* the 2D offset of a planar face's boundary (thin wall, extrude.md 4.6): sharp joins (this program's earlier cl_offset_join(.., 1) rule; the
   Parasolid thicken's corner treatment is NOT DECODED) */
bool offset_face(const TopoDS_Face &f, double d, TopoDS_Shape &out) {
    if (std::abs(d) < 1e-12) { out = f; return true; }
    BRepOffsetAPI_MakeOffset mo(f, GeomAbs_Intersection);
    mo.Perform(d);
    if (!mo.IsDone()) return false;
    TopoDS_Shape w = mo.Shape();
    /* the offset wires -> a face on the same plane */
    Handle(Geom_Surface) S = BRep_Tool::Surface(f);
    std::vector<TopoDS_Wire> ws; for (TopExp_Explorer x(w, TopAbs_WIRE); x.More(); x.Next()) ws.push_back(TopoDS::Wire(x.Current()));
    if (ws.empty()) return false;
    Handle(Geom_Plane) P = Handle(Geom_Plane)::DownCast(S); if (P.IsNull()) return false;
    gp_Ax3 pl = P->Position();
    /* the largest wire is the outer one */
    auto area = [&](const TopoDS_Wire &W) { BRepBuilderAPI_MakeFace m(P->Pln(), W, true); GProp_GProps g; BRepGProp::SurfaceProperties(m.Face(), g); return std::abs(g.Mass()); };
    std::sort(ws.begin(), ws.end(), [&](const TopoDS_Wire &a, const TopoDS_Wire &b) { return area(a) > area(b); });
    BRepBuilderAPI_MakeFace mf(P->Pln(), ws[0], true);
    for (size_t i = 1; i < ws.size(); i++) mf.Add(TopoDS::Wire(ws[i].Reversed()));
    if (!mf.IsDone()) return false;
    TopoDS_Face r = mf.Face();
    if (!BRepCheck_Analyzer(r).IsValid()) { mf = BRepBuilderAPI_MakeFace(P->Pln(), ws[0], true); for (size_t i = 1; i < ws.size(); i++) mf.Add(ws[i]); r = mf.Face(); }
    (void)pl;
    out = r; return BRepCheck_Analyzer(r).IsValid();
}

/* a draft on a swept half (4.3: ps_draft_extrude_faces -> PK_FACE_install_surfs_isocline on the swept side faces). Which way a positive
   angle tapers is NOT DECODED: this program's earlier rule is kept - with a > 0 the section grows with the distance from the chain plane
   (solid25.c group_tool: offset t x h, t = tan(angle)). Neutral plane = the chain plane, pull direction = the half's sweep direction.
   OCCT's sign (BRepOffsetAPI_DraftAngle::Add): measured by tools/tests/sld_kernel_test.cpp (draft cases). */
#ifndef OCS_DRAFT_SIGN
#define OCS_DRAFT_SIGN (-1)
#endif
bool draft_half(TopoDS_Shape &s, const gp_Pnt &o, const gp_Dir &dv, double a) {
    if (std::abs(a) < 1e-12) return true;
    gp_Pln neutral(gp_Ax3(o, dv));
    BRepOffsetAPI_DraftAngle da(s);
    for (TopExp_Explorer x(s, TopAbs_FACE); x.More(); x.Next()) {
        const TopoDS_Face &F = TopoDS::Face(x.Current());
        BRepAdaptor_Surface su(F);
        if (su.GetType() == GeomAbs_Plane && std::abs(su.Plane().Axis().Direction().Dot(dv)) > 1 - 1e-9) continue;   /* the caps */
        da.Add(F, dv, OCS_DRAFT_SIGN * a, neutral);
        if (!da.AddDone()) return false;
    }
    da.Build();
    if (!da.IsDone()) return false;
    s = da.Shape();
    return true;
}

/* the band of an open chain for a thin wall (4.6; this program's earlier rule, solid25.c open_band: Direction 1 thickness on the left of
   the path, Direction 2 on the right, square ends; sharp joins = the offset curves extended to their intersection). The path runs as
   feat_groups orders it: reversed when (chain direction) x (first entity traversed reversed ? -1 : 1) > 0 (ps_sweep_wire + FUN_180097650,
   decomp/mcgeomsld_draft_sweep.c: Direction 1 goes to the side away from v = d x t). Lines and arcs are offset exactly. */
struct OSeg { int type; gp_Pnt2d a, b; gp_Pnt2d c; double r; bool ccw; };   /* in path order; arc: centre, radius, travel about +Z */
static gp_Vec2d tan_at(const OSeg &s, bool end) {
    if (s.type == 1) { gp_Vec2d v(s.a, s.b); return v.Magnitude() > 0 ? v.Normalized() : gp_Vec2d(1, 0); }
    gp_Pnt2d p = end ? s.b : s.a; gp_Vec2d rv(s.c, p); gp_Vec2d t(-rv.Y(), rv.X()); if (!s.ccw) t.Reverse(); return t.Normalized();
}
/* the offset of segment s by w to the left (w < 0: right) as a 2D curve through its two offset end points */
static bool off_seg(const OSeg &s, double w, Handle(Geom2d_Curve) &C, gp_Pnt2d &p0, gp_Pnt2d &p1) {
    gp_Vec2d t0 = tan_at(s, false), t1 = tan_at(s, true);
    gp_Vec2d n0(-t0.Y(), t0.X()), n1(-t1.Y(), t1.X());
    p0 = s.a.Translated(n0 * w); p1 = s.b.Translated(n1 * w);
    if (s.type == 1) { C = new Geom2d_Line(p0, gp_Dir2d(t0)); return true; }
    double r = s.ccw ? s.r - w : s.r + w;                   /* left of a counter-clockwise arc = toward its centre */
    if (r <= 1e-9) return false;
    C = new Geom2d_Circle(gp_Ax2d(s.c, gp_Dir2d(1, 0)), r);
    return true;
}
/* the joint of two consecutive offset curves near q: the intersection nearest q (sharp join), or q when they meet there (tangent) */
static gp_Pnt2d join(const Handle(Geom2d_Curve) &A, const Handle(Geom2d_Curve) &B, const gp_Pnt2d &qa, const gp_Pnt2d &qb) {
    if (qa.Distance(qb) < 1e-9) return qa;
    Geom2dAPI_InterCurveCurve ic(A, B, 1e-9);
    gp_Pnt2d best = qa; double bd = 1e300; gp_Pnt2d mid((qa.XY() + qb.XY()) / 2);
    for (int i = 1; i <= ic.NbPoints(); i++) { double dd = ic.Point(i).Distance(mid); if (dd < bd) { bd = dd; best = ic.Point(i); } }
    return bd < 1e299 ? best : gp_Pnt2d((qa.XY() + qb.XY()) / 2);
}
static TopoDS_Edge edge2(const Handle(Geom2d_Curve) &C, const gp_Pnt2d &a, const gp_Pnt2d &b, int type, bool ccw_hint, double z, const OSeg &s) {
    gp_Pnt A(a.X(), a.Y(), z), B(b.X(), b.Y(), z);
    if (type == 1) return BRepBuilderAPI_MakeEdge(A, B);
    Handle(Geom2d_Circle) ci = Handle(Geom2d_Circle)::DownCast(C);
    gp_Ax2 ax(gp_Pnt(s.c.X(), s.c.Y(), z), gp_Dir(0, 0, ccw_hint ? 1 : -1), gp_Dir(1, 0, 0));
    return BRepBuilderAPI_MakeEdge(gp_Circ(ax, ci->Radius()), A, B);
}
bool open_band_face(const Prof &p, const OcsChain &c, double a, double b, double z, int flip, TopoDS_Face &face, std::string &err) {
    std::vector<OSeg> segs;
    for (int i = 0; i < c.n; i++) {
        const OcsSeg &s = c.s[i]; OSeg o; o.type = s.type;
        gp_Pnt s0 = BRep_Tool::Pnt(TopExp::FirstVertex(p.e[i], true)), s1 = BRep_Tool::Pnt(TopExp::LastVertex(p.e[i], true));
        gp_Pnt t0 = s.rev ? s1 : s0, t1 = s.rev ? s0 : s1;
        o.a = gp_Pnt2d(t0.X(), t0.Y()); o.b = gp_Pnt2d(t1.X(), t1.Y());
        if (s.type == 2) { o.c = gp_Pnt2d(s.c[0], s.c[1]); o.r = s.r; o.ccw = (s.ax[2] > 0) != (s.rev != 0); }
        segs.push_back(o);
    }
    if (flip) {                                            /* the path reversed */
        std::reverse(segs.begin(), segs.end());
        for (auto &o : segs) { std::swap(o.a, o.b); o.ccw = !o.ccw; }
    }
    size_t n = segs.size();
    std::vector<Handle(Geom2d_Curve)> L(n), R(n); std::vector<gp_Pnt2d> l0(n), l1(n), r0(n), r1(n);
    for (size_t i = 0; i < n; i++)
        if (!off_seg(segs[i], a, L[i], l0[i], l1[i]) || !off_seg(segs[i], -b, R[i], r0[i], r1[i])) { err = "thin wall: an arc offset to radius 0"; return false; }
    std::vector<gp_Pnt2d> lj(n + 1), rj(n + 1);           /* the joints along each side */
    lj[0] = l0[0]; rj[0] = r0[0]; lj[n] = l1[n - 1]; rj[n] = r1[n - 1];
    for (size_t i = 1; i < n; i++) { lj[i] = join(L[i - 1], L[i], l1[i - 1], l0[i]); rj[i] = join(R[i - 1], R[i], r1[i - 1], r0[i]); }
    BRepBuilderAPI_MakeWire mw;
    try {
        for (size_t i = 0; i < n; i++) if (lj[i].Distance(lj[i + 1]) > 1e-9) mw.Add(edge2(L[i], lj[i], lj[i + 1], segs[i].type, segs[i].ccw, z, segs[i]));
        if (lj[n].Distance(rj[n]) > 1e-9) mw.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(lj[n].X(), lj[n].Y(), z), gp_Pnt(rj[n].X(), rj[n].Y(), z)));
        for (size_t i = n; i-- > 0;) if (rj[i + 1].Distance(rj[i]) > 1e-9) mw.Add(edge2(R[i], rj[i + 1], rj[i], segs[i].type, !segs[i].ccw, z, segs[i]));
        if (rj[0].Distance(lj[0]) > 1e-9) mw.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(rj[0].X(), rj[0].Y(), z), gp_Pnt(lj[0].X(), lj[0].Y(), z)));
    } catch (...) { err = "thin wall band"; return false; }
    if (!mw.IsDone()) { err = "thin wall band not closed"; return false; }
    BRepBuilderAPI_MakeFace mf(gp_Pln(gp_Pnt(0, 0, z), gp_Dir(0, 0, 1)), mw.Wire(), true);
    if (!mf.IsDone()) { err = "thin wall band face"; return false; }
    face = mf.Face();
    if (!BRepCheck_Analyzer(face).IsValid()) { face.Reverse(); }
    if (!BRepCheck_Analyzer(face).IsValid()) { err = "thin wall band not valid (self-intersecting)"; return false; }
    return true;
}

/* one group's tool body. Names follow ps_sweep_sheet for a plain sweep (Create / Cut / Boss, Distance / Through all, Reverse); Both
   directions, Draft and Thin wall make the body but their names are NOT DECODED (t.named = false). */
bool group_tool(OcsBody &b, const OcsGroup &g, const OcsParams &p, const TopoDS_Shape *target, double tol, int metric, int op, uint32_t flag,
                Tool &t, int &eid, int &ecap, std::string &err) {
    std::vector<Prof> ps(g.nch);
    for (int c = 0; c < g.nch; c++) if (!build_prof(g.ch[c], ps[c], err)) { eid = 337; ecap = 201; return false; }
    bool anyopen = false; for (auto &q : ps) if (!q.closed) anyopen = true;
    if (!p.thin && anyopen) { eid = 680; ecap = -1; return false; }        /* 4.5: sld 680, message_box_ok(0x2a8, -1, -1) */
    gp_Dir d(g.dir[0], g.dir[1], g.dir[2]);
    if (p.reverse) d.Reverse();                                             /* 4.1: P+0x39 Reverse all */
    double d1 = p.dist, d2 = p.dist;
    if (p.type == 1 && p.method == 1 && target) through_all(*target, ps, d, metric, tol, d1, d2);
    if (d1 <= tol || (p.both && d2 <= tol)) { eid = 315; ecap = 201; return false; }   /* ps_sweep_sheet 0x13b */
    std::vector<Prof> closed_ps; std::vector<int> open_ix;
    for (int c = 0; c < g.nch; c++) { if (ps[c].closed) closed_ps.push_back(ps[c]); else open_ix.push_back(c); }
    TopoDS_Face base;
    if (!closed_ps.empty() && !profile_face(closed_ps, d, base, err)) { eid = p.thin ? 557 : 337; ecap = 201; return false; }
    gp_Pnt o = ps[0].pts[0];
    /* 4.3: the draft angle per half - P+0x44 Reverse negates it; the second half of Both uses Split ? P+0x44 : !P+0x44 */
    double A = p.draft ? p.draft_ang : 0;
    double a1 = p.draft_rev ? -A : A, a2 = (p.split ? p.draft_rev : !p.draft_rev) ? -A : A;
    bool plain = !p.thin && !p.both && std::abs(A) < 1e-12;
    if (plain) {                                                            /* the swept profile with Mastercam's names */
        BRepPrimAPI_MakePrism mp(base, gp_Vec(d) * d1, Standard_False, Standard_True);
        if (!mp.IsDone()) { eid = 557; ecap = 201; return false; }
        t.s = mp.Shape();
        name_prism(b, mp, ps, op, flag, t);
        return true;
    }
    /* the section(s) to sweep: not thin = the profile; thin (4.6) = the closed chains' wall between the offsets (Direction 1 inward,
       Direction 2 outward: WPF ThinWallInward / OutwardDistance) and every open chain's band */
    double t1 = p.thin_dir != 1 ? p.thick : 0, t2 = p.thin_dir != 0 ? p.thick2 : 0;
    std::vector<TopoDS_Face> secs; std::vector<TopoDS_Face> holes_of;   /* thin closed: outer offset face + inner offset face (cut) */
    TopoDS_Shape fo, fi;
    if (!p.thin) secs.push_back(base);
    else {
        if (!closed_ps.empty()) {
            if (!offset_face(base, t2, fo) || (t1 > 0 && !offset_face(base, -t1, fi))) { eid = 557; ecap = 201; return false; }
        }
        if (!open_ix.empty() && p.draft && !p.draft_ends && std::abs(A) > 1e-12) {
            eid = 555; ecap = 201; err = "Draft of an open chain's thin wall without Draft ends: not on OpenCASCADE (the end faces would be drafted)"; return false; }
        for (int c : open_ix) {
            int cd = g.dir[2] < 0 ? -1 : 1, r = g.ch[c].n && g.ch[c].s[0].rev ? -1 : 1;
            TopoDS_Face band;
            if (!open_band_face(ps[c], g.ch[c], t1, t2, ps[c].pts[0].Z(), cd * r > 0, band, err)) { eid = 557; ecap = 201; return false; }
            secs.push_back(band);
        }
    }
    auto half = [&](const gp_Dir &dv, double len, double a, TopoDS_Shape &out) -> bool {
        std::vector<TopoDS_Shape> parts;
        if (p.thin && !fo.IsNull()) {
            TopoDS_Shape po = BRepPrimAPI_MakePrism(fo, gp_Vec(dv) * len, Standard_True).Shape();
            if (!draft_half(po, o, dv, a)) return false;
            if (!fi.IsNull()) {
                TopoDS_Shape pi = BRepPrimAPI_MakePrism(fi, gp_Vec(dv) * len, Standard_True).Shape();
                if (!draft_half(pi, o, dv, -a)) return false;               /* the inner wall: the band widens with the outer one (region offset) */
                bool okb; po = boolop(1, po, pi, okb); if (!okb) return false;
            }
            parts.push_back(po);
        }
        for (auto &f : secs) {
            TopoDS_Shape pr = BRepPrimAPI_MakePrism(f, gp_Vec(dv) * len, Standard_True).Shape();
            if (!draft_half(pr, o, dv, a)) return false;
            parts.push_back(pr);
        }
        if (parts.empty()) return false;
        out = parts[0];
        for (size_t i = 1; i < parts.size(); i++) { bool okb; out = boolop(0, out, parts[i], okb); if (!okb) return false; }
        return true;
    };
    TopoDS_Shape h1, h2;
    if (!half(d, d1, a1, h1)) { eid = std::abs(A) > 0 ? 555 : 557; ecap = 201; return false; }
    t.s = h1;
    if (p.both) {                                                           /* 4.3: the copy swept d2 the other way, then united */
        gp_Dir dm = d; dm.Reverse();
        if (!half(dm, d2, a2, h2)) { eid = std::abs(A) > 0 ? 555 : 557; ecap = 201; return false; }
        bool okb; t.s = boolop(0, h1, h2, okb); if (!okb) { eid = 557; ecap = 201; return false; }
    }
    t.named = false; t.why = p.thin ? "Thin wall" : p.both ? "Both directions" : "Draft";
    return true;
}

/* ---------------------------------------------------------------- booleans with Mastercam's names (FUN_18003e630 / FUN_18004a4b0) */
bool named_boolean(OcsBody &res, const OcsBody &target, std::vector<Tool> &tools, bool cut, int op, std::string &err) {
    TopTools_ListOfShape args, tl; args.Append(target.s);
    for (auto &t : tools) tl.Append(t.s);
    std::unique_ptr<BRepAlgoAPI_BooleanOperation> bo;
    if (cut) bo.reset(new BRepAlgoAPI_Cut()); else bo.reset(new BRepAlgoAPI_Fuse());
    bo->SetArguments(args); bo->SetTools(tl); if (g_fuzzy > 0) bo->SetFuzzyValue(g_fuzzy); bo->Build();
    if (bo->HasErrors()) { err = "boolean failed"; return false; }
    TopoDS_Shape R = bo->Shape();
    if (R.IsNull() || volume_of(R) <= 0) { err = "no body"; return false; }
    OcsBody::Op &O = res.ops[op];
    /* n2 (FUN_1800c6580): the tools' names, edges then faces per tool, concatenated, walked backwards */
    std::vector<int> fw;
    for (auto &t : tools) { fw.insert(fw.end(), t.edges.begin(), t.edges.end()); fw.insert(fw.end(), t.faces.begin(), t.faces.end()); }
    O.l[1].assign(fw.rbegin(), fw.rend());
    /* the result's faces / edges and where they came from */
    TopTools_IndexedMapOfShape rf, re; TopExp::MapShapes(R, TopAbs_FACE, rf); TopExp::MapShapes(R, TopAbs_EDGE, re);
    TopTools_DataMapOfShapeInteger orig, split;        /* result sub-shape -> record it came from; split: 1 = one of 2+ pieces */
    std::vector<int> gone_e, gone_f;                   /* n3: target records no longer on the result (deleted or split) */
    auto scan = [&](const TopTools_DataMapOfShapeInteger &names, const TopoDS_Shape &in, TopAbs_ShapeEnum ty, bool is_target) {
        TopTools_IndexedMapOfShape m; TopExp::MapShapes(in, ty, m);
        const TopTools_IndexedMapOfShape &rm = ty == TopAbs_FACE ? rf : re;
        for (int i = 1; i <= m.Extent(); i++) {
            const TopoDS_Shape &S = m(i);
            if (!names.IsBound(S)) continue;
            int r = names.Find(S);
            if (bo->IsDeleted(S)) { if (is_target) (ty == TopAbs_FACE ? gone_f : gone_e).push_back(r); continue; }
            TopTools_ListOfShape M = bo->Modified(S);   /* a copy: the next Modified() call reuses the list */
            std::vector<TopoDS_Shape> img;
            for (TopTools_ListIteratorOfListOfShape it(M); it.More(); it.Next()) if (it.Value().ShapeType() == ty && rm.Contains(it.Value())) img.push_back(it.Value());
            if (M.IsEmpty()) { if (rm.Contains(S)) { orig.Bind(S, r); split.Bind(S, 0); } else if (is_target) (ty == TopAbs_FACE ? gone_f : gone_e).push_back(r); continue; }
            if (img.empty()) { if (is_target) (ty == TopAbs_FACE ? gone_f : gone_e).push_back(r); continue; }
            for (auto &x : img) { orig.Bind(x, r); split.Bind(x, img.size() > 1 ? 1 : 0); }
            if (img.size() > 1 && is_target) (ty == TopAbs_FACE ? gone_f : gone_e).push_back(r);
        }
    };
    scan(target.nm, target.s, TopAbs_EDGE, true); scan(target.nm, target.s, TopAbs_FACE, true);
    for (auto &t : tools) { scan(res.nm, t.s, TopAbs_EDGE, false); scan(res.nm, t.s, TopAbs_FACE, false); }
    /* names of the result: faces first (a new edge's refs are its faces' final names), listed after the edges */
    TopTools_DataMapOfShapeInteger fin;
    std::vector<int> n1e, n1f;
    auto name_one = [&](const TopoDS_Shape &S, bool face, std::vector<int> &lst) {
        if (orig.IsBound(S)) {
            int r = orig.Find(S); bool sp = split.Find(S) != 0;
            if (sp) { int q = add_rec(res, face ? K_SPLIT_FACE : K_SPLIT_EDGE, op, OCS_REF_REC, r); lst.push_back(q); fin.Bind(S, q); return; }
            if (res.recs[r].kind & K_TOOL) {                /* a surviving tool entity: a copy of its tool name (kind & 0x7ffffff) */
                OcsRec c = res.recs[r]; c.kind &= 0x7ffffff; c.op = op;
                res.recs.push_back(c); int q = (int)res.recs.size() - 1; lst.push_back(q); fin.Bind(S, q); return; }
            fin.Bind(S, r); return;                          /* a target entity kept / modified, not split: keeps its name */
        }
        if (face) { int q = add_rec(res, K_UNNAMED_FACE, op); lst.push_back(q); fin.Bind(S, q); return; }
        fin.Bind(S, -1);                                     /* a new edge: named below (needs the faces) */
    };
    for (int i = 1; i <= rf.Extent(); i++) name_one(rf(i), true, n1f);
    TopTools_IndexedDataMapOfShapeListOfShape ef; TopExp::MapShapesAndAncestors(R, TopAbs_EDGE, TopAbs_FACE, ef);
    for (int i = 1; i <= re.Extent(); i++) {
        /* OCCT's seams (an edge closed on its face) and degenerated edges are no Parasolid edges (formats/occt_xt.md 2: the writer drops
           them): no name */
        bool seam = BRep_Tool::Degenerated(TopoDS::Edge(re(i)));
        if (ef.Contains(re(i))) for (TopTools_ListIteratorOfListOfShape it(ef.FindFromKey(re(i))); it.More(); it.Next())
            if (BRep_Tool::IsClosed(TopoDS::Edge(re(i)), TopoDS::Face(it.Value()))) seam = true;
        if (seam) { fin.Bind(re(i), -1); continue; }
        name_one(re(i), false, n1e);
        if (fin.Find(re(i)) >= 0) continue;
        /* FUN_18003e630: an edge with no name after the boolean gets 0x8000018 with refs = its two faces (ps_get_edge_faces; bit 27 resolved
           to the faces' final names by FUN_1802652b0) */
        int fr[2] = { -1, -1 }, k = 0;
        if (ef.Contains(re(i))) for (TopTools_ListIteratorOfListOfShape it(ef.FindFromKey(re(i))); it.More() && k < 2; it.Next()) {
            int q = fin.IsBound(it.Value()) ? fin.Find(it.Value()) : -1;
            if (k == 1 && it.Value().IsSame(ef.FindFromKey(re(i)).First())) continue;
            fr[k++] = q;
        }
        int q = add_rec(res, K_NEW_EDGE, op, fr[0] >= 0 ? OCS_REF_REC : OCS_REF_NONE, fr[0], fr[1] >= 0 ? OCS_REF_REC : OCS_REF_NONE, fr[1]);
        n1e.push_back(q); fin.Bind(re(i), q);
    }
    if (getenv("OCS_DEBUG")) {
        for (int i = 1; i <= re.Extent(); i++) { int q = fin.Find(re(i)); BRepAdaptor_Curve c(TopoDS::Edge(re(i)));
            fprintf(stderr, "ocs: edge %d type %d orig %d split %d rec %d kind %x\n", i, (int)c.GetType(), orig.IsBound(re(i)) ? orig.Find(re(i)) : -1,
                    split.IsBound(re(i)) ? split.Find(re(i)) : -1, q, q >= 0 ? res.recs[q].kind : 0); }
        for (int i = 1; i <= rf.Extent(); i++) { int q = fin.Find(rf(i)); BRepAdaptor_Surface su(TopoDS::Face(rf(i)));
            fprintf(stderr, "ocs: face %d type %d orig %d split %d rec %d kind %x\n", i, (int)su.GetType(), orig.IsBound(rf(i)) ? orig.Find(rf(i)) : -1,
                    split.IsBound(rf(i)) ? split.Find(rf(i)) : -1, q, q >= 0 ? res.recs[q].kind : 0); }
    }
    O.l[0] = n1e; O.l[0].insert(O.l[0].end(), n1f.begin(), n1f.end());
    O.l[2] = gone_e; O.l[2].insert(O.l[2].end(), gone_f.begin(), gone_f.end());
    /* the result's names */
    res.nm.Clear();
    for (TopTools_DataMapIteratorOfDataMapOfShapeInteger it(fin); it.More(); it.Next()) if (it.Value() >= 0) res.nm.Bind(it.Key(), it.Value());
    res.s = R;
    return true;
}

/* the schema of a Parasolid version, found as the app finds the part solids' schemas (xt_mesh.c xt_schema_find: MCAM_SCHEMA_DIR, paths.conf
   "schema", ./schema) */
std::shared_ptr<xtocc::Schema> schema_for(int pk) {
    static std::map<int, std::shared_ptr<xtocc::Schema>> cache;
    auto it = cache.find(pk); if (it != cache.end()) return it->second;
    std::string hdr = pk == 28 ? "SCH=SCH_2800143_28002;" : "SCH=SCH_2700142_26105;";
    char path[2200] = "";
    std::shared_ptr<xtocc::Schema> s;
    if (xt_schema_find((const uint8_t *)hdr.data(), hdr.size(), path, sizeof path) && path[0]) s = xtocc::load_schema(path);
    cache[pk] = s; return s;
}

}  // namespace

/* ================================================================ C API */
extern "C" {

OcsBody *ocs_ref(OcsBody *b) { if (b) b->ref++; return b; }
void ocs_unref(OcsBody *b) { if (b && --b->ref <= 0) delete b; }

int ocs_extrude(const OcsBody *target, const OcsGroup *g, int ng, const OcsParams *p, double tol, int metric, OcsBody ***out, int *nout,
                int *err_id, int *err_cap, char *err, size_t errn) {
    *out = nullptr; *nout = 0; *err_id = 0; *err_cap = -1; if (err && errn) err[0] = 0;
    if (ng < 1) { *err_id = 337; *err_cap = 201; return 0; }
    set_fuzzy(metric);
    std::string e;
    try {
        if (p->type == 0) {                                 /* 4.7 Create: each group its own new body, one op each */
            std::vector<OcsBody *> bs;
            for (int i = 0; i < ng; i++) {
                OcsBody *b = new OcsBody; b->ops.resize(1);
                Tool t;
                if (!group_tool(*b, g[i], *p, nullptr, tol, metric, 0, 0, t, *err_id, *err_cap, e)) { delete b; for (auto *x : bs) ocs_unref(x); if (err) snprintf(err, errn, "%s", e.c_str()); return 0; }
                b->s = t.s;
                b->ops[0].l[0] = t.edges; b->ops[0].l[0].insert(b->ops[0].l[0].end(), t.faces.begin(), t.faces.end());
                b->ops[0].named = t.named; b->ops[0].why = t.why; b->ops[0].grp = i;
                if (!t.named) b->nm.Clear();
                if (getenv("OCS_DEBUG")) {
                    TopTools_IndexedMapOfShape fm, em; TopExp::MapShapes(b->s, TopAbs_FACE, fm); TopExp::MapShapes(b->s, TopAbs_EDGE, em);
                    for (int q = 1; q <= fm.Extent(); q++) { int r = rec_of(*b, fm(q)); fprintf(stderr, "ocs: create face %d rec %d kind %x\n", q, r, r >= 0 ? b->recs[r].kind : 0); }
                    for (int q = 1; q <= em.Extent(); q++) { int r = rec_of(*b, em(q)); fprintf(stderr, "ocs: create edge %d rec %d kind %x\n", q, r, r >= 0 ? b->recs[r].kind : 0); }
                }
                bs.push_back(b);
            }
            *out = (OcsBody **)malloc(sizeof(OcsBody *) * bs.size()); for (size_t i = 0; i < bs.size(); i++) (*out)[i] = bs[i];
            *nout = (int)bs.size(); return 1;
        }
        if (!target) return 0;
        /* Cut: ps_boolean_cut(target, tools); Boss: ps_boolean_union. 4.7: "Create a single operation" (or an edit) -> FUN_1800d2c80, all
           groups' tools in one boolean = one op; otherwise FUN_1800d38f0, one result (op) per group, in group order */
        OcsBody *r = new OcsBody(*target); r->ref = 1;
        int nrun = p->single || ng == 1 ? 1 : ng;
        for (int run = 0; run < nrun; run++) {
            int g0 = nrun == 1 ? 0 : run, g1 = nrun == 1 ? ng : run + 1;
            OcsBody before(*r); before.ref = 1;
            int op = (int)r->ops.size(); r->ops.resize(op + 1); r->ops[op].grp = nrun == 1 ? -1 : run;
            std::vector<Tool> tools(g1 - g0);
            bool named = true; std::string why;
            for (int i = g0; i < g1; i++) {
                if (!group_tool(*r, g[i], *p, &before.s, tol, metric, op, K_TOOL, tools[i - g0], *err_id, *err_cap, e)) { delete r; if (err) snprintf(err, errn, "%s", e.c_str()); return 0; }
                if (!tools[i - g0].named) { named = false; why = tools[i - g0].why; }
            }
            bool prev_named = true; for (auto &o : before.ops) if (!o.named) prev_named = false;
            if (!named_boolean(*r, before, tools, p->type == 1, op, e)) { delete r; *err_id = 559; *err_cap = 553; if (err) snprintf(err, errn, "%s", e.c_str()); return 0; }
            if (!named || !prev_named) { r->ops[op].named = false; r->ops[op].why = named ? "an earlier op's names" : why; }
        }
        *out = (OcsBody **)malloc(sizeof(OcsBody *)); (*out)[0] = r; *nout = 1;
        return 1;
    } catch (Standard_Failure &x) {
        if (err) snprintf(err, errn, "OpenCASCADE: %s", x.GetMessageString()); *err_id = 557; *err_cap = 201; return 0;
    } catch (...) { if (err) snprintf(err, errn, "OpenCASCADE failed"); *err_id = 557; *err_cap = 201; return 0; }
}

OcsBody *ocs_primitive(int prim, const double *pv, char *err, size_t errn) {
    try {
        OcsBody *b = new OcsBody; b->ops.resize(1); b->ops[0].named = false; b->ops[0].why = "SldPrimitiveOp (NOT DECODED)";
        if (prim == 15) {
            double x0 = std::min(pv[0], pv[1]), x1 = std::max(pv[0], pv[1]), y0 = std::min(pv[2], pv[3]), y1 = std::max(pv[2], pv[3]), z0 = std::min(pv[4], pv[5]), z1 = std::max(pv[4], pv[5]);
            b->s = BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), gp_Pnt(x1, y1, z1)).Shape();
        } else if (prim == 17) {
            gp_Dir dir(pv[5], pv[6], pv[7]);
            b->s = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(pv[2], pv[3], pv[4]), dir), pv[0], pv[1]).Shape();
        } else { delete b; return nullptr; }
        return b;
    } catch (...) { if (err) snprintf(err, errn, "OpenCASCADE failed"); return nullptr; }
}

static bool clash(const TopoDS_Shape &a, const TopoDS_Shape &b) {
    /* PsClashTest (mcgeomsld 0x1800308d0): a clash that is not abutting = shared volume (solid25.c body_clash's reading) */
    bool okb; TopoDS_Shape c = boolop(2, a, b, okb); if (!okb) return false;
    return volume_of(c) > 1e-9;
}
int ocs_target_search(OcsBody *const *bs, int nb, const OcsGroup *g, int ng, const OcsParams *p0, double tol, int metric, int *flip) {
    if (nb < 1 || ng < 1) return -1;
    set_fuzzy(metric);
    try {
        /* FUN_1800da210: the extents of the bodies and the chains; FUN_1800d8d70: the test op = the panel's with method Distance and
           Distance = that box's diagonal; type, target and trim cleared (FUN_1800d26d0) */
        Bnd_Box bb;
        for (int i = 0; i < nb; i++) BRepBndLib::Add(bs[i]->s, bb);
        for (int k = 0; k < ng; k++) for (int c = 0; c < g[k].nch; c++) for (int s = 0; s < g[k].ch[c].n; s++) {
            const OcsSeg &q = g[k].ch[c].s[s]; bb.Add(gp_Pnt(q.p0[0], q.p0[1], q.p0[2])); bb.Add(gp_Pnt(q.p1[0], q.p1[1], q.p1[2]));
            if (q.type == 2) for (int a = 0; a < 3; a++) { bb.Add(gp_Pnt(q.c[0] - q.r, q.c[1] - q.r, q.c[2])); bb.Add(gp_Pnt(q.c[0] + q.r, q.c[1] + q.r, q.c[2])); }
        }
        double x0, y0, z0, x1, y1, z1; bb.Get(x0, y0, z0, x1, y1, z1);
        OcsParams ep = *p0; ep.type = 0; ep.method = 0; ep.both = ep.both; ep.dist = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
        OcsBody scratch; scratch.ops.resize(1);
        std::vector<TopoDS_Shape> fw(ng), rv;
        int eid, ecap; std::string e;
        for (int k = 0; k < ng; k++) { Tool t; if (!group_tool(scratch, g[k], ep, nullptr, tol, metric, 0, 0, t, eid, ecap, e)) return -1; fw[k] = t.s; }
        if (!ep.both) {
            OcsParams er = ep; er.reverse = !er.reverse; rv.resize(ng);
            for (int k = 0; k < ng; k++) { Tool t; if (!group_tool(scratch, g[k], er, nullptr, tol, metric, 0, 0, t, eid, ecap, e)) { rv.clear(); break; } rv[k] = t.s; }
        }
        int best = -1; double bv = 1e38; int bn = 0; std::vector<int> fl(ng);
        for (int i = 0; i < nb; i++) {
            bool ok = true; int n = 0; std::fill(fl.begin(), fl.end(), 0);
            for (int k = 0; ok && k < ng; k++) {
                if (clash(bs[i]->s, fw[k])) continue;
                if ((int)rv.size() != ng || !clash(bs[i]->s, rv[k])) ok = false; else { fl[k] = 1; n++; }
            }
            if (!ok) continue;
            /* FUN_1800d9d40: per group min(|far side - chain plane|, |near side - chain plane|) along Z (this program's Z-plane chains) */
            Bnd_Box b1; BRepBndLib::AddOptimal(bs[i]->s, b1, false, false); double a0, a1, a2, c0, c1, c2; b1.Get(a0, a1, a2, c0, c1, c2);
            double sum = 0;
            for (int k = 0; k < ng; k++) { double zc = g[k].ch[0].s[0].p0[2]; if (g[k].ch[0].s[0].type == 2) zc = g[k].ch[0].s[0].c[2];
                double a = std::abs(c2 - zc), b = std::abs(a2 - zc); sum += a < b ? a : b; }
            double v = sum / ng;
            if (best < 0 || (std::abs(v - bv) >= tol ? v < bv : n < bn)) { best = i; bv = v; bn = n; for (int k = 0; k < ng; k++) flip[k] = fl[k]; }
        }
        return best;
    } catch (...) { return -1; }
}

double ocs_volume(const OcsBody *b) { try { return b && !b->s.IsNull() ? volume_of(b->s) : 0; } catch (...) { return 0; } }
int ocs_bbox(const OcsBody *b, double lo[3], double hi[3]) {
    if (!b || b->s.IsNull()) return 0;
    try { Bnd_Box bb; BRepBndLib::AddOptimal(b->s, bb, false, false); if (bb.IsVoid()) return 0; bb.Get(lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]); return 1; }
    catch (...) { return 0; }
}
int ocs_nfaces(const OcsBody *b) { TopTools_IndexedMapOfShape m; if (b) TopExp::MapShapes(b->s, TopAbs_FACE, m); return m.Extent(); }
int ocs_nedges(const OcsBody *b) { TopTools_IndexedMapOfShape m; if (b) TopExp::MapShapes(b->s, TopAbs_EDGE, m); return m.Extent(); }
int ocs_valid(const OcsBody *b) { try { return b && BRepCheck_Analyzer(b->s).IsValid() ? 1 : 0; } catch (...) { return 0; } }

/* ours: the display tessellation (Mastercam's ps_gen_disp_list tolerance is not used here) */
int ocs_mesh(const OcsBody *b, double defl, float **out) {
    *out = nullptr; if (!b || b->s.IsNull()) return 0;
    try {
        BRepMesh_IncrementalMesh m(b->s, defl > 0 ? defl : 0.01, Standard_False, 0.35, Standard_False);
        std::vector<float> v; int nt = 0;
        for (TopExp_Explorer x(b->s, TopAbs_FACE); x.More(); x.Next()) {
            const TopoDS_Face &F = TopoDS::Face(x.Current()); TopLoc_Location loc;
            Handle(Poly_Triangulation) T = BRep_Tool::Triangulation(F, loc); if (T.IsNull()) continue;
            bool rev = F.Orientation() == TopAbs_REVERSED;
            BRepAdaptor_Surface su(F, Standard_False);
            for (int i = 1; i <= T->NbTriangles(); i++) {
                int a, c, d; T->Triangle(i).Get(a, c, d); if (rev) std::swap(c, d);
                for (int k : { a, c, d }) {
                    gp_Pnt p = T->Node(k).Transformed(loc.Transformation());
                    gp_Vec n(0, 0, 1);
                    if (T->HasUVNodes()) { gp_Pnt2d uv = T->UVNode(k); gp_Pnt q; gp_Vec du, dv; su.D1(uv.X(), uv.Y(), q, du, dv); n = du.Crossed(dv);
                        if (n.Magnitude() > 1e-15) { n.Normalize(); if (rev) n.Reverse(); } else n = gp_Vec(0, 0, 1); }
                    v.push_back((float)p.X()); v.push_back((float)p.Y()); v.push_back((float)p.Z());
                    v.push_back((float)n.X()); v.push_back((float)n.Y()); v.push_back((float)n.Z());
                }
                nt++;
            }
        }
        *out = (float *)malloc(sizeof(float) * (v.size() ? v.size() : 1)); if (!v.empty()) memcpy(*out, v.data(), sizeof(float) * v.size());
        return nt;
    } catch (...) { return 0; }
}

/* the B-rep edges for display (sld_draw_solid_wire draws the body's edges; seams are OCCT's, not Parasolid edges: left out) */
int ocs_edges(const OcsBody *b, double defl, float **out) {
    *out = nullptr; if (!b || b->s.IsNull()) return 0;
    try {
        std::vector<float> v; int ns = 0;
        TopTools_IndexedDataMapOfShapeListOfShape ef; TopExp::MapShapesAndAncestors(b->s, TopAbs_EDGE, TopAbs_FACE, ef);
        for (int i = 1; i <= ef.Extent(); i++) {
            const TopoDS_Edge &E = TopoDS::Edge(ef.FindKey(i));
            if (BRep_Tool::Degenerated(E)) continue;
            bool seam = false; for (TopTools_ListIteratorOfListOfShape it(ef(i)); it.More(); it.Next()) if (BRep_Tool::IsClosed(E, TopoDS::Face(it.Value()))) seam = true;
            if (seam) continue;
            BRepAdaptor_Curve c(E);
            GCPnts_TangentialDeflection d(c, 0.1, defl > 0 ? defl : 0.01);
            for (int k = 1; k < d.NbPoints(); k++) {
                gp_Pnt p = d.Value(k), q = d.Value(k + 1);
                float s[6] = { (float)p.X(), (float)p.Y(), (float)p.Z(), (float)q.X(), (float)q.Y(), (float)q.Z() };
                v.insert(v.end(), s, s + 6); ns++;
            }
        }
        *out = (float *)malloc(sizeof(float) * (v.size() ? v.size() : 1)); if (!v.empty()) memcpy(*out, v.data(), sizeof(float) * v.size());
        return ns;
    } catch (...) { return 0; }
}

int ocs_hit(const OcsBody *b, const double o[3], const double d[3], double *t) {
    if (!b || b->s.IsNull()) return 0;
    try {
        IntCurvesFace_ShapeIntersector in; in.Load(b->s, 1e-7);
        gp_Lin L(gp_Pnt(o[0], o[1], o[2]), gp_Dir(d[0], d[1], d[2]));
        in.Perform(L, 0, 1e300);
        if (!in.IsDone() || in.NbPnt() < 1) return 0;
        double best = 1e300; for (int i = 1; i <= in.NbPnt(); i++) best = std::min(best, in.WParameter(i));
        double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        *t = best / (l > 0 ? l : 1); return 1;
    } catch (...) { return 0; }
}

uint8_t *ocs_xt_ex(const OcsBody *b, double scale, int pk, const char *key, const char *date, int entinfo, size_t *n, int *ne, int *nf,
                   char *err, size_t errn) {
    *n = 0; if (ne) *ne = 0; if (nf) *nf = 0;
    if (!b || b->s.IsNull()) { if (err) snprintf(err, errn, "no body"); return nullptr; }
    auto S = schema_for(pk);
    if (!S) { if (err) snprintf(err, errn, "no Parasolid schema file (sch_%s.s_t: paths.conf schema)", pk == 28 ? "28002" : "26105"); return nullptr; }
    try {
        occxt::Options o; o.scale = scale; o.pk = pk; o.key = key ? key : "sld_1"; if (date) o.date = date; o.mc_entinfo = entinfo != 0;
        std::vector<uint8_t> v; occxt::Report r;
        if (!occxt::write(b->s, *S, o, v, r)) {
            std::string u; for (auto &x : r.unmapped) { if (u.find(x) == std::string::npos) u += (u.empty() ? "" : "; ") + x; }
            if (err) snprintf(err, errn, "XT writer: %s%s%s", r.error.c_str(), u.empty() ? "" : " ", u.c_str()); return nullptr; }
        if (ne) *ne = r.counts.count("EDGE") ? r.counts["EDGE"] : 0;
        if (nf) *nf = r.counts.count("FACE") ? r.counts["FACE"] : 0;
        uint8_t *m = (uint8_t *)malloc(v.size()); memcpy(m, v.data(), v.size()); *n = v.size(); return m;
    } catch (Standard_Failure &x) { if (err) snprintf(err, errn, "XT writer: %s", x.GetMessageString()); return nullptr; }
    catch (...) { if (err) snprintf(err, errn, "XT writer failed"); return nullptr; }
}
uint8_t *ocs_xt(const OcsBody *b, double scale, int pk, const char *key, size_t *n, char *err, size_t errn) {
    return ocs_xt_ex(b, scale, pk, key, nullptr, 0, n, nullptr, nullptr, err, errn);
}

uint8_t *ocs_brep(const OcsBody *b, size_t *n) {
    *n = 0; if (!b) return nullptr;
    std::ostringstream os; BRepTools::Write(b->s, os);
    std::string s = os.str(); uint8_t *m = (uint8_t *)malloc(s.size() + 1); memcpy(m, s.data(), s.size()); m[s.size()] = 0; *n = s.size(); return m;
}
OcsBody *ocs_from_brep(const uint8_t *s, size_t n) {
    try { std::istringstream is(std::string((const char *)s, n)); BRep_Builder B; OcsBody *b = new OcsBody; BRepTools::Read(b->s, is, B);
          if (b->s.IsNull()) { delete b; return nullptr; } return b; } catch (...) { return nullptr; }
}
OcsBody *ocs_from_xt(const uint8_t *xb, size_t n, double scale, char *err, size_t errn) {
    char path[2200] = "";
    if (!xt_schema_find(xb, n, path, sizeof path) || !path[0]) { if (err) snprintf(err, errn, "no Parasolid schema file for this solid"); return nullptr; }
    auto S = xtocc::load_schema(path); if (!S) { if (err) snprintf(err, errn, "cannot read %s", path); return nullptr; }
    try {
        xtocc::Model m; std::string e; if (!xtocc::parse(*S, xb, n, m, e)) { if (err) snprintf(err, errn, "XT: %s", e.c_str()); return nullptr; }
        xtocc::Result r; if (!xtocc::build(m, scale, r)) { if (err) snprintf(err, errn, "XT: %s", r.error.c_str()); return nullptr; }
        OcsBody *b = new OcsBody; b->s = r.shape;
        /* a compound of one solid -> the solid */
        TopExp_Explorer x(r.shape, TopAbs_SOLID); if (x.More()) { TopoDS_Shape one = x.Current(); x.Next(); if (!x.More()) b->s = one; }
        return b;
    } catch (...) { if (err) snprintf(err, errn, "OpenCASCADE failed"); return nullptr; }
}

int ocs_nrecs(const OcsBody *b) { return b ? (int)b->recs.size() : 0; }
const OcsRec *ocs_rec(const OcsBody *b, int i) { return b && i >= 0 && i < (int)b->recs.size() ? &b->recs[i] : nullptr; }
int ocs_nops(const OcsBody *b) { return b ? (int)b->ops.size() : 0; }
int ocs_op_list(const OcsBody *b, int op, int list, const int **idx) {
    *idx = nullptr; if (!b || op < 0 || op >= (int)b->ops.size() || list < 0 || list > 2) return 0;
    *idx = b->ops[op].l[list].data(); return (int)b->ops[op].l[list].size();
}
int ocs_op_named(const OcsBody *b, int op, char *why, size_t wn) {
    if (!b || op < 0 || op >= (int)b->ops.size()) { if (why) snprintf(why, wn, "no such op"); return 0; }
    if (why) snprintf(why, wn, "%s", b->ops[op].why.c_str());
    return b->ops[op].named ? 1 : 0;
}
int ocs_op_group(const OcsBody *b, int op) { return b && op >= 0 && op < (int)b->ops.size() ? b->ops[op].grp : -1; }
int ocs_op_tag(const OcsBody *b, int op) { return b && op >= 0 && op < (int)b->ops.size() ? b->ops[op].tag : -1; }
void ocs_op_set_tag(OcsBody *b, int op, int tag) { if (b && op >= 0 && op < (int)b->ops.size()) b->ops[op].tag = tag; }
int ocs_face_rec(const OcsBody *b, int face) {
    TopTools_IndexedMapOfShape m; TopExp::MapShapes(b->s, TopAbs_FACE, m); return face >= 0 && face < m.Extent() ? rec_of(*b, m(face + 1)) : -1;
}
int ocs_edge_rec(const OcsBody *b, int edge) {
    TopTools_IndexedMapOfShape m; TopExp::MapShapes(b->s, TopAbs_EDGE, m); return edge >= 0 && edge < m.Extent() ? rec_of(*b, m(edge + 1)) : -1;
}

}  /* extern "C" */


/* ================================================================ Constant Fillet (formats/solid_fillet.md 3 / 5; occt_solid.h) */
namespace {
const uint32_t K_BLEND_FACE = 0x2e;     /* FUN_180036580: a blend face, ref1 = the record of the edge under it (topols[i]) */
const uint32_t K_BLEND_EDGE = 0x17;     /* 0x8000017: an edge of a blend face / any edge without a record, refs = its two faces */

/* what a face / edge of the shape being filleted came from: rec = its record before the op (-1 none), split = one of 2+ pieces,
   blend >= -1: a blend face (value = the record of the edge it replaced, -1 none: a vertex blend or an edge without a record) */
struct Prov { int rec = -1; bool split = false; int blend = -3; };

bool seam_or_degen(const TopoDS_Edge &E, const TopTools_IndexedDataMapOfShapeListOfShape &ef) {
    if (BRep_Tool::Degenerated(E)) return true;
    if (ef.Contains(E)) for (TopTools_ListIteratorOfListOfShape it(ef.FindFromKey(E)); it.More(); it.Next())
        if (BRep_Tool::IsClosed(E, TopoDS::Face(it.Value()))) return true;
    return false;
}

/* ours: OCCT rebuilds an edge / face it only trims (a neighbour of a fillet) as a new shape on the same 3D curve / surface, without
   history; Parasolid keeps the entity (its record stays, FUN_18004a4b0 lists only entities without a record). Such a shape is matched to the
   old one by its curve / surface handle. */
Handle(Geom_Curve) curve_of(const TopoDS_Shape &E) { double f, l; TopLoc_Location L; return BRep_Tool::Curve(TopoDS::Edge(E), L, f, l); }
Handle(Geom_Surface) surf_of(const TopoDS_Shape &F) { TopLoc_Location L; return BRep_Tool::Surface(TopoDS::Face(F), L); }

/* one MakeFillet build on S with edges L; the provenance (pv, indexed through P) carried to the result; rest = the input edges still to
   fillet (mitered), mapped to their images */
bool fillet_step(TopoDS_Shape &S, std::vector<TopoDS_Edge> &L, double r, std::vector<Prov> &pv, TopTools_DataMapOfShapeInteger &P,
                 std::vector<TopoDS_Edge> &rest, std::string &err) {
    BRepFilletAPI_MakeFillet mf(S, ChFi3d_Rational);
    for (auto &e : L) mf.Add(r, e);
    mf.Build();
    if (!mf.IsDone()) { err = "BRepFilletAPI_MakeFillet: not done"; return false; }
    TopoDS_Shape R = mf.Shape();
    if (R.IsNull()) { err = "no result"; return false; }
    TopTools_IndexedMapOfShape rf, re; TopExp::MapShapes(R, TopAbs_FACE, rf); TopExp::MapShapes(R, TopAbs_EDGE, re);
    TopTools_DataMapOfShapeInteger NP;
    std::map<const Standard_Transient *, int> ecurve, fsurf;          /* old edge curve / face surface -> provenance (unmatched only) */
    auto carry = [&](TopAbs_ShapeEnum ty) {
        TopTools_IndexedMapOfShape m; TopExp::MapShapes(S, ty, m);
        const TopTools_IndexedMapOfShape &rm = ty == TopAbs_FACE ? rf : re;
        for (int i = 1; i <= m.Extent(); i++) {
            const TopoDS_Shape &X = m(i);
            if (!P.IsBound(X)) continue;
            int q = P.Find(X);
            if (rm.Contains(X)) { NP.Bind(X, q); continue; }
            if (mf.IsDeleted(X)) continue;
            TopTools_ListOfShape M = mf.Modified(X);
            std::vector<TopoDS_Shape> img;
            for (TopTools_ListIteratorOfListOfShape it(M); it.More(); it.Next()) if (it.Value().ShapeType() == ty && rm.Contains(it.Value())) img.push_back(it.Value());
            if (img.empty()) {                                       /* no history: the curve / surface match below (ours) */
                if (ty == TopAbs_EDGE) { Handle(Geom_Curve) c = curve_of(X); if (!c.IsNull()) ecurve[c.get()] = q; }
                else { Handle(Geom_Surface) s = surf_of(X); if (!s.IsNull()) fsurf[s.get()] = q; }
                continue;
            }
            for (auto &y : img) { Prov p = pv[q]; if (img.size() > 1) p.split = true; pv.push_back(p); NP.Bind(y, (int)pv.size() - 1); }
        }
    };
    carry(TopAbs_FACE); carry(TopAbs_EDGE);
    /* the blend faces: Generated(edge) -> the edge's record; Generated(vertex) -> a vertex blend (no edge record) */
    for (auto &e : L) {
        int er = P.IsBound(e) ? pv[P.Find(e)].rec : -1;
        if (P.IsBound(e) && pv[P.Find(e)].split) er = -1;
        TopTools_ListOfShape G = mf.Generated(e);
        for (TopTools_ListIteratorOfListOfShape it(G); it.More(); it.Next())
            if (it.Value().ShapeType() == TopAbs_FACE && rf.Contains(it.Value()) && !NP.IsBound(it.Value())) { Prov p; p.blend = er; pv.push_back(p); NP.Bind(it.Value(), (int)pv.size() - 1); }
    }
    TopTools_IndexedMapOfShape vm; TopExp::MapShapes(S, TopAbs_VERTEX, vm);
    for (int i = 1; i <= vm.Extent(); i++) {
        TopTools_ListOfShape G = mf.Generated(vm(i));
        for (TopTools_ListIteratorOfListOfShape it(G); it.More(); it.Next())
            if (it.Value().ShapeType() == TopAbs_FACE && rf.Contains(it.Value()) && !NP.IsBound(it.Value())) { Prov p; p.blend = -1; pv.push_back(p); NP.Bind(it.Value(), (int)pv.size() - 1); }
    }
    for (int i = 1; i <= re.Extent(); i++) if (!NP.IsBound(re(i))) {
        Handle(Geom_Curve) c = curve_of(re(i)); auto f = c.IsNull() ? ecurve.end() : ecurve.find(c.get());
        if (f != ecurve.end()) { NP.Bind(re(i), f->second); ecurve.erase(f); }
    }
    for (int i = 1; i <= rf.Extent(); i++) if (!NP.IsBound(rf(i))) {
        Handle(Geom_Surface) s = surf_of(rf(i)); auto f = s.IsNull() ? fsurf.end() : fsurf.find(s.get());
        if (f != fsurf.end()) { NP.Bind(rf(i), f->second); fsurf.erase(f); }
    }
    /* the edges still to fillet (mitered): their images in R (FUN_18004b210 maps a replaced input edge to its successor) */
    std::vector<TopoDS_Edge> nr;
    for (auto &e : rest) {
        if (re.Contains(e)) { nr.push_back(e); continue; }
        if (mf.IsDeleted(e)) continue;
        TopTools_ListOfShape M = mf.Modified(e); bool any = false;
        for (TopTools_ListIteratorOfListOfShape it(M); it.More(); it.Next()) if (it.Value().ShapeType() == TopAbs_EDGE && re.Contains(it.Value())) { nr.push_back(TopoDS::Edge(it.Value())); any = true; }
        if (!any) { Handle(Geom_Curve) c = curve_of(e);
            for (int i = 1; i <= re.Extent() && !c.IsNull(); i++) if (curve_of(re(i)).get() == c.get()) { nr.push_back(TopoDS::Edge(re(i))); break; } }
    }
    rest = nr;
    S = R; P = NP;
    return true;
}
}  // namespace

extern "C" {
int ocs_fillet(const OcsBody *b, const int *edges, int ne, const OcsFilP *p, OcsBody **out, char *err, size_t errn) {
    *out = nullptr; if (err && errn) err[0] = 0;
    if (!b || b->s.IsNull() || !p || !(p->radius > 0)) { if (err) snprintf(err, errn, "no body / radius"); return 0; }
    try {
        OcsBody *r = new OcsBody(*b); r->ref = 1;
        int op = (int)r->ops.size(); r->ops.resize(op + 1);
        TopTools_IndexedMapOfShape em; TopExp::MapShapes(b->s, TopAbs_EDGE, em);
        std::vector<TopoDS_Edge> E;
        for (int i = 0; i < ne; i++) if (edges[i] >= 0 && edges[i] < em.Extent()) {
            TopoDS_Edge e = TopoDS::Edge(em(edges[i] + 1)); bool dup = false;
            for (auto &x : E) if (x.IsSame(e)) dup = true;
            if (!dup) E.push_back(e);
        }
        if (E.empty()) { *out = r; return 1; }                         /* FUN_180036310: nothing left, success, no change */
        std::vector<Prov> pv; TopTools_DataMapOfShapeInteger P;       /* provenance = the input names */
        for (TopTools_DataMapIteratorOfDataMapOfShapeInteger it(b->nm); it.More(); it.Next()) { Prov q; q.rec = it.Value(); pv.push_back(q); P.Bind(it.Key(), (int)pv.size() - 1); }
        TopoDS_Shape S = b->s; std::string e;
        if (!p->mitered) {                                             /* all edges in one PK_EDGE_set_blend_constant + PK_BODY_fix_blends */
            std::vector<TopoDS_Edge> none;
            if (!fillet_step(S, E, p->radius, pv, P, none, e)) { delete r; if (err) snprintf(err, errn, "%s", e.c_str()); return 0; }
        } else {                                                       /* ps_fillet's per-edge loop: one edge, then the rest carried */
            std::vector<TopoDS_Edge> rest(E.begin(), E.end());
            while (!rest.empty()) {
                std::vector<TopoDS_Edge> one(1, rest[0]); rest.erase(rest.begin());
                if (!fillet_step(S, one, p->radius, pv, P, rest, e)) { delete r; if (err) snprintf(err, errn, "%s", e.c_str()); return 0; }
            }
        }
        if (volume_of(S) <= 0 || !BRepCheck_Analyzer(S).IsValid()) { delete r; if (err) snprintf(err, errn, "the filleted body is not valid"); return 0; }
        /* the names (FUN_180036580's tail + FUN_18004b9b0 + FUN_18004a4b0): faces first (an edge's refs are faces' final names) */
        TopTools_IndexedMapOfShape rf, re; TopExp::MapShapes(S, TopAbs_FACE, rf); TopExp::MapShapes(S, TopAbs_EDGE, re);
        TopTools_IndexedDataMapOfShapeListOfShape ef; TopExp::MapShapesAndAncestors(S, TopAbs_EDGE, TopAbs_FACE, ef);
        TopTools_DataMapOfShapeInteger fin; std::vector<int> n1e, n1f; std::vector<char> kept(r->recs.size(), 0);
        for (int i = 1; i <= rf.Extent(); i++) {
            int q = P.IsBound(rf(i)) ? P.Find(rf(i)) : -1; Prov pr = q >= 0 ? pv[q] : Prov();
            int nr;
            if (pr.blend >= -1) nr = add_rec(*r, K_BLEND_FACE, op, pr.blend >= 0 ? OCS_REF_REC : OCS_REF_NONE, pr.blend >= 0 ? pr.blend : 0);
            else if (pr.rec >= 0 && pr.split) nr = add_rec(*r, K_SPLIT_FACE, op, OCS_REF_REC, pr.rec);
            else if (pr.rec >= 0 && !kept[pr.rec]) { fin.Bind(rf(i), pr.rec); kept[pr.rec] = 1; continue; }
            else nr = add_rec(*r, K_UNNAMED_FACE, op);
            n1f.push_back(nr); fin.Bind(rf(i), nr);
        }
        for (int i = 1; i <= re.Extent(); i++) {
            const TopoDS_Edge &E1 = TopoDS::Edge(re(i));
            if (seam_or_degen(E1, ef)) continue;                         /* no Parasolid edge (formats/occt_xt.md 2) */
            int q = P.IsBound(E1) ? P.Find(E1) : -1; Prov pr = q >= 0 ? pv[q] : Prov();
            int nr;
            if (pr.rec >= 0 && pr.split) nr = add_rec(*r, K_SPLIT_EDGE, op, OCS_REF_REC, pr.rec);
            else if (pr.rec >= 0 && !kept[pr.rec]) { fin.Bind(E1, pr.rec); kept[pr.rec] = 1; continue; }
            else {
                int fr[2] = { -1, -1 }, k = 0;
                if (ef.Contains(E1)) for (TopTools_ListIteratorOfListOfShape it(ef.FindFromKey(E1)); it.More() && k < 2; it.Next()) {
                    if (k == 1 && it.Value().IsSame(ef.FindFromKey(E1).First())) continue;
                    fr[k++] = fin.IsBound(it.Value()) ? fin.Find(it.Value()) : -1;
                }
                nr = add_rec(*r, K_BLEND_EDGE, op, fr[0] >= 0 ? OCS_REF_REC : OCS_REF_NONE, fr[0], fr[1] >= 0 ? OCS_REF_REC : OCS_REF_NONE, fr[1]);
            }
            n1e.push_back(nr); fin.Bind(E1, nr);
        }
        /* n3: the input's records that are no longer on the body (edges first, then faces, as FUN_18004a8f0 lists them) */
        std::vector<int> ge, gf;
        TopTools_IndexedMapOfShape bf, be; TopExp::MapShapes(b->s, TopAbs_FACE, bf); TopExp::MapShapes(b->s, TopAbs_EDGE, be);
        for (int i = 1; i <= be.Extent(); i++) { int q = rec_of(*b, be(i)); if (q >= 0 && !kept[q]) ge.push_back(q); }
        for (int i = 1; i <= bf.Extent(); i++) { int q = rec_of(*b, bf(i)); if (q >= 0 && !kept[q]) gf.push_back(q); }
        OcsBody::Op &O = r->ops[op];
        O.l[0] = n1e; O.l[0].insert(O.l[0].end(), n1f.begin(), n1f.end());
        O.l[2] = ge; O.l[2].insert(O.l[2].end(), gf.begin(), gf.end());
        bool prev = true; for (int k = 0; k < op; k++) if (!r->ops[k].named) prev = false;
        if (!prev) { O.named = false; O.why = "an earlier op's names"; }
        r->nm.Clear();
        for (TopTools_DataMapIteratorOfDataMapOfShapeInteger it(fin); it.More(); it.Next()) r->nm.Bind(it.Key(), it.Value());
        r->s = S;
        *out = r; return 1;
    } catch (Standard_Failure &x) { if (err) snprintf(err, errn, "OpenCASCADE: %s", x.GetMessageString()); return 0; }
    catch (...) { if (err) snprintf(err, errn, "OpenCASCADE failed"); return 0; }
}

static bool sub_maps(const OcsBody *b, TopTools_IndexedMapOfShape &fm, TopTools_IndexedMapOfShape &em) {
    if (!b || b->s.IsNull()) return false;
    TopExp::MapShapes(b->s, TopAbs_FACE, fm); TopExp::MapShapes(b->s, TopAbs_EDGE, em); return true;
}
int ocs_edge_faces(const OcsBody *b, int edge, int f[2]) {
    f[0] = f[1] = -1; TopTools_IndexedMapOfShape fm, em; if (!sub_maps(b, fm, em) || edge < 0 || edge >= em.Extent()) return 0;
    TopTools_IndexedDataMapOfShapeListOfShape ef; TopExp::MapShapesAndAncestors(b->s, TopAbs_EDGE, TopAbs_FACE, ef);
    int k = 0; const TopoDS_Shape &E = em(edge + 1);
    if (ef.Contains(E)) for (TopTools_ListIteratorOfListOfShape it(ef.FindFromKey(E)); it.More() && k < 2; it.Next()) {
        int fi = fm.FindIndex(it.Value()) - 1; if (fi < 0 || (k == 1 && fi == f[0])) continue; f[k++] = fi; }
    return k;
}
int ocs_edge_smooth(const OcsBody *b, int edge) {
    int f[2]; if (ocs_edge_faces(b, edge, f) < 2) return 0;
    try {
        TopTools_IndexedMapOfShape fm, em; sub_maps(b, fm, em);
        return BRepLib::ContinuityOfFaces(TopoDS::Edge(em(edge + 1)), TopoDS::Face(fm(f[0] + 1)), TopoDS::Face(fm(f[1] + 1)), 1.0e-10) >= GeomAbs_G1 ? 1 : 0;
    } catch (...) { return 0; }
}
int ocs_face_edges(const OcsBody *b, int face, int **out) {
    *out = nullptr; TopTools_IndexedMapOfShape fm, em; if (!sub_maps(b, fm, em) || face < 0 || face >= fm.Extent()) return 0;
    TopTools_IndexedDataMapOfShapeListOfShape ef; TopExp::MapShapesAndAncestors(b->s, TopAbs_EDGE, TopAbs_FACE, ef);
    std::vector<int> v; TopTools_IndexedMapOfShape fe; TopExp::MapShapes(fm(face + 1), TopAbs_EDGE, fe);
    for (int i = 1; i <= fe.Extent(); i++) { if (seam_or_degen(TopoDS::Edge(fe(i)), ef)) continue; int k = em.FindIndex(fe(i)); if (k > 0) v.push_back(k - 1); }
    *out = (int *)malloc(sizeof(int) * (v.size() + 1)); for (size_t i = 0; i < v.size(); i++) (*out)[i] = v[i];
    return (int)v.size();
}
static bool tangent_at(const TopoDS_Edge &a, const TopoDS_Edge &c, const TopoDS_Vertex &v) {
    auto tan = [&](const TopoDS_Edge &e, gp_Vec &t) {
        BRepAdaptor_Curve C(e); double u = BRep_Tool::Parameter(v, e); gp_Pnt p; C.D1(u, p, t);
        if (t.Magnitude() < 1e-12) return false; t.Normalize(); return true; };
    gp_Vec t1, t2; if (!tan(a, t1) || !tan(c, t2)) return false;
    return t1.IsParallel(t2, 1e-6);
}
int ocs_tangent_edges(const OcsBody *b, int edge, int **out) {
    *out = nullptr; TopTools_IndexedMapOfShape fm, em; if (!sub_maps(b, fm, em) || edge < 0 || edge >= em.Extent()) return 0;
    TopTools_IndexedDataMapOfShapeListOfShape ve, ef; TopExp::MapShapesAndAncestors(b->s, TopAbs_VERTEX, TopAbs_EDGE, ve);
    TopExp::MapShapesAndAncestors(b->s, TopAbs_EDGE, TopAbs_FACE, ef);
    std::vector<int> v(1, edge); std::vector<char> in(em.Extent(), 0); in[edge] = 1;
    try {
        for (size_t k = 0; k < v.size(); k++) {
            const TopoDS_Edge &E = TopoDS::Edge(em(v[k] + 1));
            for (TopExp_Explorer x(E, TopAbs_VERTEX); x.More(); x.Next()) {
                const TopoDS_Vertex &V = TopoDS::Vertex(x.Current());
                if (!ve.Contains(V)) continue;
                for (TopTools_ListIteratorOfListOfShape it(ve.FindFromKey(V)); it.More(); it.Next()) {
                    int j = em.FindIndex(it.Value()) - 1; if (j < 0 || in[j]) continue;
                    const TopoDS_Edge &E2 = TopoDS::Edge(it.Value());
                    if (seam_or_degen(E2, ef)) continue;
                    if (tangent_at(E, E2, V)) { in[j] = 1; v.push_back(j); }
                }
            }
        }
    } catch (...) {}
    *out = (int *)malloc(sizeof(int) * v.size()); for (size_t i = 0; i < v.size(); i++) (*out)[i] = v[i];
    return (int)v.size();
}
int ocs_tangent_faces(const OcsBody *b, int face, int **out) {
    *out = nullptr; TopTools_IndexedMapOfShape fm, em; if (!sub_maps(b, fm, em) || face < 0 || face >= fm.Extent()) return 0;
    std::vector<int> v(1, face); std::vector<char> in(fm.Extent(), 0); in[face] = 1;
    for (size_t k = 0; k < v.size(); k++) {
        int *fe = nullptr; int n = ocs_face_edges(b, v[k], &fe);
        for (int i = 0; i < n; i++) { if (!ocs_edge_smooth(b, fe[i])) continue; int f[2]; int c = ocs_edge_faces(b, fe[i], f);
            for (int j = 0; j < c; j++) if (f[j] >= 0 && !in[f[j]]) { in[f[j]] = 1; v.push_back(f[j]); } }
        free(fe);
    }
    *out = (int *)malloc(sizeof(int) * v.size()); for (size_t i = 0; i < v.size(); i++) (*out)[i] = v[i];
    return (int)v.size();
}
int ocs_sub_center(const OcsBody *b, int kind, int idx, double c[3]) {
    TopTools_IndexedMapOfShape fm, em; if (!sub_maps(b, fm, em)) return 0;
    const TopTools_IndexedMapOfShape &m = kind ? em : fm; if (idx < 0 || idx >= m.Extent()) return 0;
    try { Bnd_Box bb; BRepBndLib::Add(m(idx + 1), bb, false); if (bb.IsVoid()) return 0; double lo[3], hi[3];
          bb.Get(lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]); for (int k = 0; k < 3; k++) c[k] = (lo[k] + hi[k]) / 2; return 1; } catch (...) { return 0; }
}
int ocs_find_rec(const OcsBody *b, int kind, int rec) {
    TopTools_IndexedMapOfShape fm, em; if (!sub_maps(b, fm, em) || rec < 0) return -1;
    const TopTools_IndexedMapOfShape &m = kind ? em : fm;
    for (int i = 1; i <= m.Extent(); i++) if (rec_of(*b, m(i)) == rec) return i - 1;
    return -1;
}
int ocs_pick(const OcsBody *b, const double o[3], const double d[3], double ap, int *face, int *edge, double *t) {
    *face = *edge = -1; *t = 0; TopTools_IndexedMapOfShape fm, em; if (!sub_maps(b, fm, em)) return 0;
    try {
        gp_Dir D(d[0], d[1], d[2]); gp_Pnt O(o[0], o[1], o[2]); gp_Lin L(O, D);
        IntCurvesFace_ShapeIntersector in; in.Load(b->s, 1e-7); in.Perform(L, -1e300, 1e300);
        double best = 1e300;
        if (in.IsDone()) for (int i = 1; i <= in.NbPnt(); i++) if (in.WParameter(i) < best) {
            best = in.WParameter(i); *face = fm.FindIndex(in.Face(i)) - 1; }
        /* edges: the closest within ap of the ray (sampled at 65 points), taken when not behind the face hit */
        TopTools_IndexedDataMapOfShapeListOfShape ef; TopExp::MapShapesAndAncestors(b->s, TopAbs_EDGE, TopAbs_FACE, ef);
        double ebest = 1e300, eperp = ap;
        for (int i = 1; i <= em.Extent(); i++) {
            const TopoDS_Edge &E = TopoDS::Edge(em(i)); if (seam_or_degen(E, ef)) continue;
            BRepAdaptor_Curve C(E); double f = C.FirstParameter(), l = C.LastParameter();
            for (int k = 0; k <= 64; k++) {
                gp_Pnt p = C.Value(f + (l - f) * k / 64.0); gp_Vec v(O, p); double along = v.Dot(gp_Vec(D));
                double perp = (v - gp_Vec(D) * along).Magnitude();
                if (perp < eperp - 1e-12 || (perp <= eperp && along < ebest)) { eperp = perp; ebest = along; *edge = i - 1; }
            }
        }
        if (*edge >= 0 && (*face < 0 || ebest <= best + ap)) *t = ebest; else { *edge = -1; *t = best; }
        return *face >= 0 || *edge >= 0;
    } catch (...) { return 0; }
}
int ocs_sub_lines(const OcsBody *b, int kind, int idx, double defl, float **out) {
    *out = nullptr; TopTools_IndexedMapOfShape fm, em; if (!sub_maps(b, fm, em)) return 0;
    std::vector<int> es;
    if (kind == 1) { if (idx < 0 || idx >= em.Extent()) return 0; es.push_back(idx); }
    else { int *fe = nullptr; int n = ocs_face_edges(b, idx, &fe); for (int i = 0; i < n; i++) es.push_back(fe[i]); free(fe); }
    std::vector<float> v; int ns = 0;
    try {
        for (int e : es) {
            BRepAdaptor_Curve c(TopoDS::Edge(em(e + 1)));
            GCPnts_TangentialDeflection d(c, 0.1, defl > 0 ? defl : 0.01);
            for (int k = 1; k < d.NbPoints(); k++) { gp_Pnt p = d.Value(k), q = d.Value(k + 1);
                float s[6] = { (float)p.X(), (float)p.Y(), (float)p.Z(), (float)q.X(), (float)q.Y(), (float)q.Z() }; v.insert(v.end(), s, s + 6); ns++; }
        }
    } catch (...) {}
    *out = (float *)malloc(sizeof(float) * (v.size() + 1)); if (!v.empty()) memcpy(*out, v.data(), sizeof(float) * v.size());
    return ns;
}
}  /* extern "C" */

#else   /* without OpenCASCADE */
extern "C" {
static void nocc(char *err, size_t errn) { if (err && errn) snprintf(err, errn, "built without OpenCASCADE"); }
int ocs_extrude(const OcsBody *, const OcsGroup *, int, const OcsParams *, double, int, OcsBody ***out, int *nout, int *eid, int *ecap, char *err, size_t errn) {
    *out = nullptr; *nout = 0; *eid = 0; *ecap = -1; nocc(err, errn); return 0; }
OcsBody *ocs_primitive(int, const double *, char *err, size_t errn) { nocc(err, errn); return nullptr; }
int ocs_target_search(OcsBody *const *, int, const OcsGroup *, int, const OcsParams *, double, int, int *) { return -1; }
OcsBody *ocs_ref(OcsBody *b) { return b; }
void ocs_unref(OcsBody *) {}
double ocs_volume(const OcsBody *) { return 0; }
int ocs_bbox(const OcsBody *, double *, double *) { return 0; }
int ocs_mesh(const OcsBody *, double, float **out) { *out = nullptr; return 0; }
int ocs_edges(const OcsBody *, double, float **out) { *out = nullptr; return 0; }
int ocs_hit(const OcsBody *, const double *, const double *, double *) { return 0; }
int ocs_nfaces(const OcsBody *) { return 0; }
int ocs_nedges(const OcsBody *) { return 0; }
int ocs_valid(const OcsBody *) { return 0; }
uint8_t *ocs_xt(const OcsBody *, double, int, const char *, size_t *n, char *err, size_t errn) { *n = 0; nocc(err, errn); return nullptr; }
uint8_t *ocs_xt_ex(const OcsBody *, double, int, const char *, const char *, int, size_t *n, int *, int *, char *err, size_t errn) { *n = 0; nocc(err, errn); return nullptr; }
uint8_t *ocs_brep(const OcsBody *, size_t *n) { *n = 0; return nullptr; }
OcsBody *ocs_from_brep(const uint8_t *, size_t) { return nullptr; }
OcsBody *ocs_from_xt(const uint8_t *, size_t, double, char *err, size_t errn) { nocc(err, errn); return nullptr; }
int ocs_nrecs(const OcsBody *) { return 0; }
const OcsRec *ocs_rec(const OcsBody *, int) { return nullptr; }
int ocs_nops(const OcsBody *) { return 0; }
int ocs_op_list(const OcsBody *, int, int, const int **idx) { *idx = nullptr; return 0; }
int ocs_op_named(const OcsBody *, int, char *why, size_t wn) { nocc(why, wn); return 0; }
int ocs_op_group(const OcsBody *, int) { return -1; }
int ocs_op_tag(const OcsBody *, int) { return -1; }
void ocs_op_set_tag(OcsBody *, int, int) {}
int ocs_face_rec(const OcsBody *, int) { return -1; }
int ocs_edge_rec(const OcsBody *, int) { return -1; }
int ocs_fillet(const OcsBody *, const int *, int, const OcsFilP *, OcsBody **out, char *err, size_t errn) { *out = nullptr; nocc(err, errn); return 0; }
int ocs_edge_smooth(const OcsBody *, int) { return 0; }
int ocs_face_edges(const OcsBody *, int, int **out) { *out = nullptr; return 0; }
int ocs_edge_faces(const OcsBody *, int, int f[2]) { f[0] = f[1] = -1; return 0; }
int ocs_tangent_edges(const OcsBody *, int, int **out) { *out = nullptr; return 0; }
int ocs_tangent_faces(const OcsBody *, int, int **out) { *out = nullptr; return 0; }
int ocs_sub_center(const OcsBody *, int, int, double *) { return 0; }
int ocs_find_rec(const OcsBody *, int, int) { return -1; }
int ocs_pick(const OcsBody *, const double *, const double *, double, int *face, int *edge, double *t) { *face = *edge = -1; *t = 0; return 0; }
int ocs_sub_lines(const OcsBody *, int, int, double, float **out) { *out = nullptr; return 0; }
}
#endif
