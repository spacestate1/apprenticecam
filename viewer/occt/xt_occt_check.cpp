/* xt_occt_check - accuracy check of the XT -> OCCT translation (formats/occt_accuracy.md).
 *
 * Usage: xt_occt_check SCHEMA.s_t MANIFEST.tsv [-v]      (manifest from xt_extract.py: part \t solid \t file.x_b)
 *
 * Per solid (model units mm = XT metres x 1000):
 *   OCCT side : BRepCheck_Analyzer, BRepAlgoAPI_Check (self-intersection / small edges / ...), free edges,
 *               volume / area (BRepGProp, adaptive), bounding box (BRepBndLib::AddOptimal, exact geometry).
 *   XT side   : (ours, independent of OCCT) exact volume and per-face area by the divergence / Green theorems over the
 *               XT loops (plane faces with line and arc edges, cylinder faces bounded by axial lines and coaxial
 *               arcs - formulas in formats/occt_accuracy.md), exact bounding box from the edges (for planes and
 *               cylinders the extremes lie on edges).
 *   deviation : (a) points sampled on each XT face from its exact surface inside its trim -> distance to the OCCT face;
 *               (b) points on each XT edge (exact curve) -> distance to the OCCT edge;
 *               (c) OCCT triangulation nodes (BRepMesh, 0.005 mm) of each face -> distance to the XT face (exact
 *                   surface; outside the XT trim -> distance to the XT boundary);
 *               (d) cross-check with viewer/xt_mesh.c: its vertices -> OCCT faces, its volume / area vs OCCT.
 * Flag: any geometric deviation > 0.001 mm; volume difference / area (an equivalent mean normal shift) > 0.001 mm;
 * per-face area difference / face perimeter > 0.001 mm; invalid shape. */
#include "xt_occt.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <set>

#include <BRepAlgoAPI_Check.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_ListOfStatus.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Poly_Triangulation.hxx>
#include <ShapeAnalysis_Curve.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

extern "C" {
#include "../xt_mesh.h"
}

using xtocc::Model;
using xtocc::Node;

static const double FLAG = 1e-3;   /* mm */

/* ------------------------------------------------------------------------------- XT exact geometry (ours) */
struct Fin {
    int edge = 0;
    bool line = true;
    gp_XYZ a, b;                       /* fin start / end (line) */
    gp_XYZ c, e1, e2, n; double R = 0, phi0 = 0, sw = 0;   /* arc: p(phi) = c + R(cos e1 + sin e2), phi0 .. phi0+sw */
    gp_XYZ at(double t) const {        /* t in [0,1] along the fin */
        if (line) return a + (b - a) * t;
        double p = phi0 + sw * t;
        return c + (e1 * cos(p) + e2 * sin(p)) * R;
    }
};
struct XFace {
    int idx = 0;
    bool plane = true, ok = true;
    std::string why;
    gp_XYZ p0, n, x, y; double r = 0;  /* plane: origin, normal, frame; cylinder: point on axis, axis n, frame, radius */
    int s_eff = 1;                     /* sensed normal vs natural normal */
    std::vector<std::vector<Fin>> loops;
    double area = 0, vol = 0, perim = 0;
};

static gp_XYZ vpos(const Model &m, const Node *fin, double S) {
    const Node *v = m.pnode(fin, "vertex");
    const Node *p = v ? m.pnode(v, "point") : nullptr;
    return p ? m.vec(p, "pvec") * S : gp_XYZ(NAN, NAN, NAN);
}

/* the fin's geometry: it runs from the previous fin's vertex to its own (formats/xt.md); sense '-' = against the curve */
static bool fin_geom(const Model &m, const Node *h, double S, Fin &f, std::string &why) {
    f.edge = m.ptr(h, "edge");
    const Node *e = m.node(f.edge);
    const Node *c = e ? m.pnode(e, "curve") : nullptr;
    if (!c) c = m.pnode(h, "curve");
    const Node *bk = m.pnode(h, "backward");
    gp_XYZ a = bk ? vpos(m, bk, S) : gp_XYZ(NAN, NAN, NAN), b = vpos(m, h, S);
    bool along = m.chr(h, "sense") != '-';
    if (m.is(c, "LINE")) {
        f.line = true; f.a = a; f.b = b;
        if (std::isnan(a.X()) || std::isnan(b.X())) { why = "line fin without vertices"; return false; }
        return true;
    }
    if (m.is(c, "CIRCLE")) {
        f.line = false;
        f.c = m.vec(c, "centre") * S; f.R = m.dbl(c, "radius") * S;
        f.n = m.vec(c, "normal"); f.n.Normalize();
        if (m.chr(c, "sense") == '-') f.n.Reverse();
        f.e1 = m.vec(c, "x_axis"); f.e1.Normalize();
        f.e2 = f.n ^ f.e1;
        auto ph = [&](const gp_XYZ &p) { gp_XYZ d = p - f.c; return atan2(d * f.e2, d * f.e1); };
        if (std::isnan(a.X()) && std::isnan(b.X())) { f.phi0 = 0; f.sw = along ? 2 * M_PI : -2 * M_PI; return true; }
        if (std::isnan(a.X()) || std::isnan(b.X())) { why = "arc fin with one vertex"; return false; }
        double p0 = ph(a), p1 = ph(b);
        f.phi0 = p0;
        bool same = m.ptr(h, "vertex") == (bk ? m.ptr(bk, "vertex") : -1);
        if (along) { double d = fmod(p1 - p0 + 4 * M_PI, 2 * M_PI); if (same || d < 1e-12) d = 2 * M_PI; f.sw = d; }
        else { double d = fmod(p0 - p1 + 4 * M_PI, 2 * M_PI); if (same || d < 1e-12) d = 2 * M_PI; f.sw = -d; }
        return true;
    }
    why = std::string("fin on curve ") + m.tname(c);
    return false;
}
static double cross2(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

static bool xt_face(const Model &m, const Node *fn, double S, XFace &F) {
    const Node *sn = m.pnode(fn, "surface");
    int ss = m.chr(sn, "sense") == '-' ? -1 : 1, fs = m.chr(fn, "sense") == '-' ? -1 : 1;
    F.s_eff = ss * fs;
    if (m.is(sn, "PLANE")) {
        F.plane = true; F.p0 = m.vec(sn, "pvec") * S; F.n = m.vec(sn, "normal"); F.x = m.vec(sn, "x_axis");
    } else if (m.is(sn, "CYLINDER")) {
        F.plane = false; F.p0 = m.vec(sn, "pvec") * S; F.n = m.vec(sn, "axis"); F.x = m.vec(sn, "x_axis"); F.r = m.dbl(sn, "radius") * S;
    } else { F.ok = false; F.why = std::string("surface ") + m.tname(sn); return false; }
    F.n.Normalize(); F.x.Normalize(); F.y = F.n ^ F.x;
    for (const Node *lp = m.pnode(fn, "loop"); lp; lp = m.pnode(lp, "next")) {
        std::vector<Fin> L;
        const Node *h0 = m.pnode(lp, "halfedge"), *h = h0;
        int g = 0;
        do {
            Fin f;
            if (!fin_geom(m, h, S, f, F.why)) { F.ok = false; return false; }
            L.push_back(f);
            h = m.pnode(h, "forward");
        } while (h && h != h0 && ++g < 100000);
        F.loops.push_back(L);
    }
    /* exact area / volume contribution */
    double sum = 0, vsum = 0, perim = 0;
    for (auto &L : F.loops)
        for (auto &f : L) {
            perim += f.line ? (f.b - f.a).Modulus() : std::abs(f.sw) * f.R;
            if (F.plane) {
                auto u = [&](const gp_XYZ &p) { return (p - F.p0) * F.x; };
                auto v = [&](const gp_XYZ &p) { return (p - F.p0) * F.y; };
                if (f.line) {
                    if (std::abs((f.a - F.p0) * F.n) > 1e-6 || std::abs((f.b - F.p0) * F.n) > 1e-6) { F.ok = false; F.why = "line off plane"; }
                    sum += cross2(u(f.a), v(f.a), u(f.b), v(f.b));
                } else {
                    if (std::abs((f.c - F.p0) * F.n) > 1e-6 || std::abs(std::abs(f.n * F.n) - 1) > 1e-9) { F.ok = false; F.why = "arc off plane"; }
                    double cu = u(f.c), cv = v(f.c), e1u = f.e1 * F.x, e1v = f.e1 * F.y, e2u = f.e2 * F.x, e2v = f.e2 * F.y;
                    gp_XYZ pa = f.at(0), pb = f.at(1);
                    sum += cross2(cu, cv, u(pb) - u(pa), v(pb) - v(pa)) + f.R * f.R * cross2(e1u, e1v, e2u, e2v) * f.sw;
                }
            } else {
                if (f.line) {
                    gp_XYZ d = f.b - f.a;
                    if ((d - F.n * (d * F.n)).Modulus() > 1e-6) { F.ok = false; F.why = "non-axial line on cylinder"; }
                    continue;                 /* d theta = 0 */
                }
                gp_XYZ off = f.c - F.p0;
                if ((off - F.n * (off * F.n)).Modulus() > 1e-6 || std::abs(std::abs(f.n * F.n) - 1) > 1e-9 || std::abs(f.R - F.r) > 1e-6) {
                    F.ok = false; F.why = "non-coaxial arc on cylinder"; continue;
                }
                double h = off * F.n, sg = f.n * F.n > 0 ? 1 : -1;
                double th0 = atan2(f.e1 * F.y, f.e1 * F.x) + sg * f.phi0, th1 = th0 + sg * f.sw;
                sum += -h * F.r * (th1 - th0);
                vsum += -h * F.r * ((F.p0 * F.x) * (sin(th1) - sin(th0)) - (F.p0 * F.y) * (cos(th1) - cos(th0)) + F.r * (th1 - th0));
            }
        }
    F.perim = perim;
    if (F.plane) { F.area = F.s_eff * 0.5 * sum; F.vol = (F.p0 * F.n) * 0.5 * sum / 3.0; }
    else { F.area = F.s_eff * sum; F.vol = vsum / 3.0; }
    if (F.area <= 0) { F.ok = false; F.why = "non-positive exact area (loop orientation rule contradicted)"; }
    return F.ok;
}

/* region tests in the face's own parameters (ours) */
struct Poly2 { std::vector<std::vector<std::pair<double, double>>> rings; };
static Poly2 plane_poly(const XFace &F) {
    Poly2 P;
    for (auto &L : F.loops) {
        std::vector<std::pair<double, double>> ring;
        for (auto &f : L) {
            int n = f.line ? 1 : std::max(2, (int)ceil(std::abs(f.sw) / (0.1 * M_PI / 180)));
            for (int k = 0; k < n; k++) { gp_XYZ p = f.at((double)k / n) - F.p0; ring.push_back({p * F.x, p * F.y}); }
        }
        P.rings.push_back(ring);
    }
    return P;
}
static bool in_poly(const Poly2 &P, double u, double v) {
    bool in = false;
    for (auto &r : P.rings)
        for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
            if ((r[i].second > v) != (r[j].second > v) &&
                u < (r[j].first - r[i].first) * (v - r[i].second) / (r[j].second - r[i].second) + r[i].first) in = !in;
    return in;
}
struct CylArc { double th0, dth, h; };
static std::vector<CylArc> cyl_arcs(const XFace &F) {
    std::vector<CylArc> A;
    for (auto &L : F.loops)
        for (auto &f : L) {
            if (f.line) continue;
            double sg = f.n * F.n > 0 ? 1 : -1;
            double th0 = atan2(f.e1 * F.y, f.e1 * F.x) + sg * f.phi0, th1 = th0 + sg * f.sw;
            if (th1 < th0) std::swap(th0, th1);
            A.push_back({th0, th1 - th0, (f.c - F.p0) * F.n});
        }
    return A;
}
static bool in_cyl(const std::vector<CylArc> &A, double th, double h) {   /* parity of arcs above (th, h) */
    bool in = false;
    for (auto &a : A) {
        if (a.h <= h) continue;
        double d = fmod(th - a.th0 + 8 * M_PI, 2 * M_PI);
        if (d < a.dth || a.dth >= 2 * M_PI - 1e-12) in = !in;
    }
    return in;
}
static void xt_param(const XFace &F, const gp_XYZ &p, double &u, double &v) {
    gp_XYZ d = p - F.p0;
    if (F.plane) { u = d * F.x; v = d * F.y; }
    else { u = atan2(d * F.y, d * F.x); v = d * F.n; }
}
static gp_XYZ xt_point(const XFace &F, double u, double v) {
    if (F.plane) return F.p0 + F.x * u + F.y * v;
    return F.p0 + F.n * v + (F.x * cos(u) + F.y * sin(u)) * F.r;
}
static double xt_surf_dist(const XFace &F, const gp_XYZ &p) {
    gp_XYZ d = p - F.p0;
    if (F.plane) return std::abs(d * F.n);
    gp_XYZ rad = d - F.n * (d * F.n);
    return std::abs(rad.Modulus() - F.r);
}
static double seg_dist(const gp_XYZ &p, const gp_XYZ &a, const gp_XYZ &b) {
    gp_XYZ ab = b - a; double L2 = ab.SquareModulus();
    double t = L2 > 0 ? std::clamp(((p - a) * ab) / L2, 0.0, 1.0) : 0;
    return (p - (a + ab * t)).Modulus();
}
static double boundary_dist(const XFace &F, const gp_XYZ &p) {
    double best = 1e300;
    for (auto &L : F.loops)
        for (auto &f : L) {
            int n = f.line ? 1 : std::max(2, (int)ceil(std::abs(f.sw) / (0.05 * M_PI / 180)));
            gp_XYZ prev = f.at(0);
            for (int k = 1; k <= n; k++) { gp_XYZ q = f.at((double)k / n); best = std::min(best, seg_dist(p, prev, q)); prev = q; }
        }
    return best;
}

/* ------------------------------------------------------------------------------- OCCT helpers */
static double dist_to_face(const TopoDS_Face &F, const gp_Pnt &p) {
    Handle(Geom_Surface) S = BRep_Tool::Surface(F);
    GeomAPI_ProjectPointOnSurf pr(p, S);
    if (pr.NbPoints() > 0) {
        double u, v; pr.LowerDistanceParameters(u, v);
        BRepClass_FaceClassifier cl(F, gp_Pnt2d(u, v), 1e-9);
        if (cl.State() == TopAbs_IN || cl.State() == TopAbs_ON) return pr.LowerDistance();
    }
    BRepExtrema_DistShapeShape d(BRepBuilderAPI_MakeVertex(p).Vertex(), F);
    return d.IsDone() ? d.Value() : 1e300;
}
static double dist_to_edge(const TopoDS_Edge &E, const gp_Pnt &p) {
    double f, l;
    Handle(Geom_Curve) C = BRep_Tool::Curve(E, f, l);
    if (C.IsNull()) return 1e300;
    gp_Pnt proj; double par;
    ShapeAnalysis_Curve sac;
    return sac.Project(C, p, 1e-9, proj, par, f, l, Standard_True);
}

struct Row {
    std::string part, solid, status;
    int nfaces = 0, nedges = 0, seams = 0;
    bool valid = false, boolok = false;
    int free_edges = 0;
    double v_occ = 0, v_xt = 0, a_occ = 0, a_xt = 0, face_da = 0, bbox_d = 0;
    double dev_face = 0, dev_edge = 0, dev_occ2xt = 0, dev_mesh_vtx = 0, mesh_dv = 0, mesh_da = 0, max_tol = 0, vgap = 0;
    long nsamp = 0, mesh_flipped = 0;
    std::vector<std::string> flags, notes;
};

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s SCHEMA.s_t MANIFEST.tsv [-v]\n", argv[0]); return 2; }
    bool verbose = argc > 3 && std::string(argv[3]) == "-v";
    auto sch = xtocc::load_schema(argv[1]);
    XtSchema *msch = xt_schema_load(argv[1]);
    if (!sch || !msch) { fprintf(stderr, "schema?\n"); return 2; }
    std::ifstream man(argv[2]);
    std::string line;
    std::map<std::string, int> types_all;
    std::set<std::string> unmapped_all;
    printf("part\tsolid\tstatus\tfaces\tedges\tseams\tvalid\tbool_check\tfree_edges\tvol_occt\tvol_xt\tdvol/area\tarea_occt\tarea_xt\tmax_face_dA/perim\tbbox_d\tdev_xt2occ_face\tdev_xt2occ_edge\tdev_occ2xt\tsamples\tmax_tol\txt_gap(vtx-curve,curve-surf)\txtmesh_vtx_dev\txtmesh_dV_rel\txtmesh_dA_rel\txtmesh_flipped_tris\tflags\n");
    while (std::getline(man, line)) {
        std::istringstream ss(line);
        Row R; std::string file;
        std::getline(ss, R.part, '\t'); std::getline(ss, R.solid, '\t'); std::getline(ss, file, '\t');
        std::ifstream in(file, std::ios::binary);
        std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)), {});
        const double S = 1000.0;               /* XT metres -> mm (formats/xt.md) */
        Model m; std::string err;
        xtocc::Result res;
        if (!xtocc::parse(*sch, blob.data(), blob.size(), m, err)) { R.status = "PARSE: " + err; }
        else if (!xtocc::build(m, S, res)) {
            R.status = "BUILD: " + res.error;
            for (auto &u : res.unmapped) { R.status += " [" + u + "]"; unmapped_all.insert(u); }
        } else R.status = "ok";
        for (auto &kv : res.counts) types_all[kv.first] += kv.second;
        if (R.status != "ok") {
            printf("%s\t%s\t%s\n", R.part.c_str(), R.solid.c_str(), R.status.c_str());
            fflush(stdout);
            continue;
        }
        R.notes = res.notes; R.seams = res.seams_added; R.vgap = std::max(res.max_vertex_gap, res.max_curve_surface_gap);
        const TopoDS_Shape &sh = res.shape;
        TopTools_IndexedMapOfShape fm, em;
        TopExp::MapShapes(sh, TopAbs_FACE, fm); TopExp::MapShapes(sh, TopAbs_EDGE, em);
        R.nfaces = fm.Extent(); R.nedges = em.Extent();
        /* validity */
        BRepCheck_Analyzer ana(sh, Standard_True);
        R.valid = ana.IsValid();
        if (!R.valid) {
            std::map<std::string, int> st;
            for (TopExp_Explorer ex(sh, TopAbs_FACE); ex.More(); ex.Next()) {
                Handle(BRepCheck_Result) rr = ana.Result(ex.Current());
                if (rr.IsNull()) continue;
                for (rr->InitContextIterator(); rr->MoreShapeInContext(); rr->NextShapeInContext())
                    for (auto it = rr->StatusOnShape().begin(); it != rr->StatusOnShape().end(); ++it)
                        if (*it != BRepCheck_NoError) st["face status " + std::to_string((int)*it)]++;
            }
            for (TopExp_Explorer ex(sh, TopAbs_SHELL); ex.More(); ex.Next()) {
                Handle(BRepCheck_Result) rr = ana.Result(ex.Current());
                if (rr.IsNull()) continue;
                for (auto it = rr->Status().begin(); it != rr->Status().end(); ++it)
                    if (*it != BRepCheck_NoError) st["shell status " + std::to_string((int)*it)]++;
            }
            for (auto &kv : st) R.notes.push_back("BRepCheck " + kv.first + " x" + std::to_string(kv.second));
            R.flags.push_back("INVALID");
        }
        BRepAlgoAPI_Check bc(sh, Standard_True, Standard_True);
        R.boolok = bc.IsValid();
        if (!R.boolok) R.flags.push_back("BOOL_CHECK");
        ShapeAnalysis_FreeBounds fb(sh, Standard_False);
        TopTools_IndexedMapOfShape fe;
        TopExp::MapShapes(fb.GetClosedWires(), TopAbs_EDGE, fe);
        TopExp::MapShapes(fb.GetOpenWires(), TopAbs_EDGE, fe);
        R.free_edges = fe.Extent();
        if (R.free_edges) R.flags.push_back("FREE_EDGES");
        for (int i = 1; i <= em.Extent(); i++) R.max_tol = std::max(R.max_tol, BRep_Tool::Tolerance(TopoDS::Edge(em(i))));
        for (int i = 1; i <= fm.Extent(); i++) R.max_tol = std::max(R.max_tol, BRep_Tool::Tolerance(TopoDS::Face(fm(i))));
        /* properties */
        GProp_GProps vp, sp;
        BRepGProp::VolumeProperties(sh, vp, 1e-10, Standard_True);
        BRepGProp::SurfaceProperties(sh, sp, 1e-10, Standard_True);
        R.v_occ = vp.Mass(); R.a_occ = sp.Mass();
        Bnd_Box bb; BRepBndLib::AddOptimal(sh, bb, Standard_False, Standard_False);
        /* XT exact */
        std::map<int, XFace> xf;
        bool xt_ok = true;
        double bmin[3] = {1e300, 1e300, 1e300}, bmax[3] = {-1e300, -1e300, -1e300};
        for (auto &kv : res.face_of) {
            XFace F; F.idx = kv.first;
            if (!xt_face(m, m.node(kv.first), S, F)) { xt_ok = false; R.notes.push_back("XT exact: face " + std::to_string(kv.first) + ": " + F.why); }
            R.v_xt += F.s_eff * 0 + F.vol; R.a_xt += F.area;
            /* bbox: fin end points + arc axis extremes inside the arc */
            for (auto &L : F.loops)
                for (auto &f : L) {
                    auto addp = [&](const gp_XYZ &p) { for (int k = 0; k < 3; k++) { bmin[k] = std::min(bmin[k], p.Coord(k + 1)); bmax[k] = std::max(bmax[k], p.Coord(k + 1)); } };
                    addp(f.at(0)); addp(f.at(1));
                    if (!f.line)
                        for (int k = 1; k <= 3; k++) {
                            double ps = atan2(f.e2.Coord(k), f.e1.Coord(k));
                            for (double cand : {ps, ps + M_PI}) {
                                double lo = std::min(f.phi0, f.phi0 + f.sw);
                                double d = fmod(cand - lo + 8 * M_PI, 2 * M_PI);
                                if (d <= std::abs(f.sw)) addp(f.c + (f.e1 * cos(cand) + f.e2 * sin(cand)) * f.R);
                            }
                        }
                }
            /* per-face area */
            GProp_GProps fp; BRepGProp::SurfaceProperties(kv.second, fp, 1e-10, Standard_True);
            double dA = std::abs(fp.Mass() - F.area) / std::max(F.perim, 1e-9);
            if (dA > R.face_da) R.face_da = dA;
            xf[kv.first] = F;
        }
        if (!xt_ok) R.flags.push_back("XT_EXACT_INCOMPLETE");
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bb.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        double ob[6] = {xmin, ymin, zmin, xmax, ymax, zmax};
        for (int k = 0; k < 3; k++) { R.bbox_d = std::max({R.bbox_d, std::abs(ob[k] - bmin[k]), std::abs(ob[k + 3] - bmax[k])}); }
        double dvol = std::abs(R.v_occ - R.v_xt) / std::max(R.a_xt, 1e-9);
        if (dvol > FLAG) R.flags.push_back("VOLUME");
        if (R.face_da > FLAG) R.flags.push_back("FACE_AREA");
        if (R.bbox_d > FLAG) R.flags.push_back("BBOX");
        if (R.v_occ <= 0) R.flags.push_back("NEG_VOLUME");
        /* deviation (a) faces, (b) edges */
        for (auto &kv : xf) {
            const XFace &F = kv.second;
            if (!F.ok) continue;
            const TopoDS_Face &OF = res.face_of[kv.first];
            std::vector<gp_XYZ> pts;
            if (F.plane) {
                Poly2 P = plane_poly(F);
                double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
                for (auto &r : P.rings) for (auto &q : r) { u0 = std::min(u0, q.first); u1 = std::max(u1, q.first); v0 = std::min(v0, q.second); v1 = std::max(v1, q.second); }
                const int N = 14;
                for (int i = 0; i <= N; i++) for (int j = 0; j <= N; j++) {
                    double u = u0 + (u1 - u0) * (i + 0.37) / (N + 1), v = v0 + (v1 - v0) * (j + 0.61) / (N + 1);
                    if (in_poly(P, u, v)) pts.push_back(xt_point(F, u, v));
                }
            } else {
                auto A = cyl_arcs(F);
                double h0 = 1e300, h1 = -1e300;
                for (auto &a : A) { h0 = std::min(h0, a.h); h1 = std::max(h1, a.h); }
                const int NT = 36, NH = 8;
                for (int i = 0; i < NT; i++) for (int j = 0; j <= NH; j++) {
                    double th = 2 * M_PI * (i + 0.37) / NT, h = h0 + (h1 - h0) * (j + 0.61) / (NH + 1);
                    if (in_cyl(A, th, h)) pts.push_back(xt_point(F, th, h));
                }
            }
            for (auto &p : pts) { R.dev_face = std::max(R.dev_face, dist_to_face(OF, gp_Pnt(p))); R.nsamp++; }
            for (auto &L : F.loops)
                for (auto &f : L) {
                    auto it = res.edge_of.find(f.edge);
                    if (it == res.edge_of.end()) continue;
                    for (int k = 0; k <= 8; k++) { R.dev_edge = std::max(R.dev_edge, dist_to_edge(it->second, gp_Pnt(f.at(k / 8.0)))); R.nsamp++; }
                }
        }
        /* (c) OCCT mesh nodes -> XT face */
        BRepMesh_IncrementalMesh mesh(sh, 0.005, Standard_False, 0.05, Standard_False);
        for (auto &kv : res.face_of) {
            const XFace &F = xf[kv.first];
            if (!F.ok) continue;
            TopLoc_Location loc;
            Handle(Poly_Triangulation) T = BRep_Tool::Triangulation(kv.second, loc);
            if (T.IsNull()) { R.notes.push_back("no triangulation for face " + std::to_string(kv.first)); continue; }
            Poly2 P; std::vector<CylArc> A;
            if (F.plane) P = plane_poly(F); else A = cyl_arcs(F);
            int step = std::max(1, T->NbNodes() / 400);
            for (int i = 1; i <= T->NbNodes(); i += step) {
                gp_Pnt q = T->Node(i).Transformed(loc.Transformation());
                gp_XYZ p = q.XYZ();
                double u, v; xt_param(F, p, u, v);
                bool in = F.plane ? in_poly(P, u, v) : in_cyl(A, u, v);
                double d = in ? xt_surf_dist(F, p) : std::max(xt_surf_dist(F, p), boundary_dist(F, p));
                R.dev_occ2xt = std::max(R.dev_occ2xt, d); R.nsamp++;
            }
        }
        /* (d) viewer/xt_mesh.c cross-check */
        XtMesh xm; memset(&xm, 0, sizeof xm);
        if (xt_mesh(msch, blob.data(), blob.size(), &xm) && xm.ntri > 0) {
            double mv = 0, ma = 0;
            std::set<std::tuple<float, float, float>> uniq;
            for (int t = 0; t < xm.ntri; t++) {
                gp_XYZ v[3];
                for (int k = 0; k < 3; k++) {
                    float *q = xm.tri + (t * 3 + k) * 6;
                    v[k] = gp_XYZ(q[0], q[1], q[2]) * S;
                    uniq.insert({q[0], q[1], q[2]});
                }
                /* xt_mesh's winding is not always outward (it is a display mesh); orient each triangle by its stored
                   normal before summing the signed volume */
                float *q0 = xm.tri + t * 18;
                gp_XYZ nrm(q0[3], q0[4], q0[5]), wn = (v[1] - v[0]) ^ (v[2] - v[0]);
                double sv = v[0] * (v[1] ^ v[2]) / 6.0;
                if (wn * nrm < 0) { sv = -sv; R.mesh_flipped++; }
                mv += sv;
                ma += wn.Modulus() / 2;
            }
            R.mesh_dv = (mv - R.v_occ) / R.v_occ; R.mesh_da = (ma - R.a_occ) / R.a_occ;
            TopoDS_Compound fc; BRep_Builder bld; bld.MakeCompound(fc);
            for (auto &kv : res.face_of) bld.Add(fc, kv.second);
            int step = std::max(1, (int)uniq.size() / 300), k = 0;
            for (auto &u : uniq) {
                if (k++ % step) continue;
                gp_Pnt p(std::get<0>(u) * S, std::get<1>(u) * S, std::get<2>(u) * S);
                BRepExtrema_DistShapeShape d(BRepBuilderAPI_MakeVertex(p).Vertex(), fc);
                if (d.IsDone()) R.dev_mesh_vtx = std::max(R.dev_mesh_vtx, d.Value());
            }
            if (std::abs(R.mesh_da) > 2e-3) {
                /* locate the disagreement: area per plane (normal, offset) in xt_mesh vs the OCCT planar faces */
                std::map<std::tuple<long, long, long, long>, std::pair<double, double>> grp;
                auto key = [](const gp_XYZ &n, double d) {
                    return std::make_tuple(std::lround(n.X() * 1e4), std::lround(n.Y() * 1e4), std::lround(n.Z() * 1e4), std::lround(d * 1e3));
                };
                for (int t = 0; t < xm.ntri; t++) {
                    float *q = xm.tri + t * 18;
                    gp_XYZ v0(q[0], q[1], q[2]), v1(q[6], q[7], q[8]), v2(q[12], q[13], q[14]);
                    v0 *= S; v1 *= S; v2 *= S;
                    gp_XYZ wn = (v1 - v0) ^ (v2 - v0);
                    double a = wn.Modulus() / 2;
                    if (a <= 0) continue;
                    gp_XYZ n = wn / (2 * a);
                    gp_XYZ sn(q[3], q[4], q[5]);
                    if (n * sn < 0) n.Reverse();
                    grp[key(n, n * v0)].first += a;
                }
                for (auto &kv : res.face_of) {
                    const XFace &F = xf[kv.first];
                    if (!F.plane) continue;
                    gp_XYZ n = F.n * F.s_eff;
                    grp[key(n, n * F.p0)].second += F.area;
                }
                for (auto &g : grp) {
                    double am = g.second.first, ao = g.second.second;
                    if (std::abs(am - ao) > 1e-3 * std::max(1.0, ao) && ao > 0) {
                        char b[200];
                        snprintf(b, sizeof b, "xt_mesh plane n=(%.4f %.4f %.4f) d=%.3f: mesh area %.4f vs exact %.4f", std::get<0>(g.first) / 1e4,
                                 std::get<1>(g.first) / 1e4, std::get<2>(g.first) / 1e4, std::get<3>(g.first) / 1e3, am, ao);
                        R.notes.push_back(b);
                    }
                }
            }
            if (xm.nfaces_meshed != xm.nfaces) R.notes.push_back("xt_mesh meshed " + std::to_string(xm.nfaces_meshed) + "/" + std::to_string(xm.nfaces) + " faces");
        } else R.notes.push_back(std::string("xt_mesh failed: ") + xm.error);
        xt_mesh_free(&xm);
        if (R.dev_face > FLAG) R.flags.push_back("DEV_FACE");
        if (R.dev_edge > FLAG) R.flags.push_back("DEV_EDGE");
        if (R.dev_occ2xt > FLAG) R.flags.push_back("DEV_OCC2XT");
        std::string fl;
        for (auto &f : R.flags) fl += (fl.empty() ? "" : ",") + f;
        printf("%s\t%s\tok\t%d\t%d\t%d\t%d\t%d\t%d\t%.9f\t%.9f\t%.3e\t%.9f\t%.9f\t%.3e\t%.3e\t%.3e\t%.3e\t%.3e\t%ld\t%.1e\t%.3e\t%.3e\t%.3e\t%.3e\t%ld\t%s\n",
               R.part.c_str(), R.solid.c_str(), R.nfaces, R.nedges, R.seams, R.valid, R.boolok, R.free_edges, R.v_occ, R.v_xt,
               dvol, R.a_occ, R.a_xt, R.face_da, R.bbox_d, R.dev_face, R.dev_edge, R.dev_occ2xt, R.nsamp, R.max_tol, R.vgap,
               R.dev_mesh_vtx, R.mesh_dv, R.mesh_da, R.mesh_flipped, fl.empty() ? "-" : fl.c_str());
        if (verbose || !R.flags.empty()) for (auto &n : R.notes) printf("#\t%s\t%s\t%s\n", R.part.c_str(), R.solid.c_str(), n.c_str());
        fflush(stdout);
    }
    printf("# XT node types over all solids:");
    for (auto &kv : types_all) printf(" %s=%d", kv.first.c_str(), kv.second);
    printf("\n# unmapped:");
    for (auto &u : unmapped_all) printf(" %s", u.c_str());
    printf("\n");
    xt_schema_free(msch);
    return 0;
}
