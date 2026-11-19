/* occt_xt_check - oracle for the OCCT -> XT writer (formats/occt_xt.md). Standalone (ours).
 *
 *   occt_xt_check rt SCHEMA MANIFEST OUTDIR [scale]
 *       round trip of every stored solid: XT -> OCCT (xt_occt) -> XT (occt_xt, keeping the face / edge / vertex
 *       node_ids) -> OUTDIR/<part>/<sld>.x_b; the written XT is read again by xtocc::parse + build (volume, area,
 *       validity against the first OCCT body) and by viewer/xt_mesh.c (parse + mesh). One TSV row per solid.
 *       The structural comparison with the original XT is tools-side: occt/xt_canon.py.
 *   occt_xt_check synth SCHEMA OUTDIR
 *       OCCT-made solids covering every written entity type (box, cylinder, cone, sphere, torus, fillets, chamfer,
 *       boolean, ellipse prism, non-uniform scale -> B-splines, revolved spline); each written XT is checked
 *       against the OCCT body with the Parasolid XT Format Reference formulas (xt_refgeom.h): surface points,
 *       face orientation, edge curves / directions / vertices, fin topology, planar loop areas. */
#include "occt_xt.h"
#include "xt_occt.h"
#include "xt_refgeom.h"
extern "C" {
#include "xt_mesh.h"
}
#include <algorithm>
#include <cmath>
#include <tuple>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Circle.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <Poly_Triangulation.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_GTrsf.hxx>

static std::vector<uint8_t> slurp(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void props(const TopoDS_Shape &s, double &vol, double &area) {
    GProp_GProps g; BRepGProp::VolumeProperties(s, g, 1e-10); vol = g.Mass();
    GProp_GProps h; BRepGProp::SurfaceProperties(s, h, 1e-10); area = h.Mass();
}

/* ------------------------------------------------------------------ the reference-formula oracle (synth mode) */
struct Oracle {
    const xtocc::Model &m;
    double scale;
    std::vector<std::string> fails;
    double max_face = 0, max_edge = 0, max_area_rel = 0;
    int nface = 0, nedge = 0;
    explicit Oracle(const xtocc::Model &m_, double s) : m(m_), scale(s) {}
    const xtocc::Node *at(int i) const { return m.node(i); }
    bool surf(const xtocc::Node *n, xtref::Surface &x) {
        x.p = m.has(n, "pvec") ? m.vec(n, "pvec") : m.vec(n, "centre");
        if (m.is(n, "PLANE")) { x.kind = xtref::Surface::PLANE; x.a = m.vec(n, "normal"); x.x = m.vec(n, "x_axis"); return true; }
        x.x = m.vec(n, "x_axis");
        if (m.is(n, "CYLINDER")) { x.kind = xtref::Surface::CYLINDER; x.a = m.vec(n, "axis"); x.r = m.dbl(n, "radius"); return true; }
        if (m.is(n, "CONE")) { x.kind = xtref::Surface::CONE; x.a = m.vec(n, "axis"); x.r = m.dbl(n, "radius");
                               x.sa = m.dbl(n, "sin_half_angle"); x.ca = m.dbl(n, "cos_half_angle"); return true; }
        if (m.is(n, "SPHERE")) { x.kind = xtref::Surface::SPHERE; x.a = m.vec(n, "axis"); x.r = m.dbl(n, "radius"); return true; }
        if (m.is(n, "TORUS")) { x.kind = xtref::Surface::TORUS; x.a = m.vec(n, "axis"); x.r = m.dbl(n, "major_radius"); x.r2 = m.dbl(n, "minor_radius"); return true; }
        return false;      /* B_SURFACE: evaluated via nurbs below */
    }
    std::vector<double> var(const xtocc::Node *n, const char *f, char t) {
        std::vector<double> v;
        /* the variable field is the last transmitted field: read count values after its offset */
        for (auto &d : m.s->nd[n->type].f) if (d.xmt && d.name == f) {
            for (uint32_t k = 0; k < n->count; k++) {
                if (t == 'f') { double x; memcpy(&x, n->p + d.off + 8 * k, 8); v.push_back(x); }
                else { int16_t x; memcpy(&x, n->p + d.off + 2 * k, 2); v.push_back(x); }
            }
        }
        return v;
    }
    bool bsurf(const xtocc::Node *n, xtref::Surface &x, bool vmajor) {
        const xtocc::Node *nb = m.pnode(n, "nurbs");
        if (!nb) return false;
        x.kind = xtref::Surface::BSURF; x.vmajor = vmajor;
        auto sh = [&](const char *f) { int16_t v; for (auto &d : m.s->nd[nb->type].f) if (d.xmt && d.name == f) { memcpy(&v, nb->p + d.off, 2); return (int)v; } return 0; };
        x.udeg = sh("u_degree"); x.vdeg = sh("v_degree"); x.dim = sh("vertex_dim");
        x.nu = m.ival(nb, "n_u_vertices"); x.nv = m.ival(nb, "n_v_vertices"); x.rational = m.chr(nb, "rational");
        x.verts = var(m.pnode(nb, "bspline_vertices"), "vertices", 'f');
        auto km = [&](const char *k, const char *mm) {
            std::vector<double> kk = var(m.pnode(nb, k), "knots", 'f'), mu = var(m.pnode(nb, mm), "mult", 'n');
            std::vector<int> mi(mu.begin(), mu.end()); return xtref::expand(kk, mi); };
        x.uk = km("u_knots", "u_knot_mult"); x.vk = km("v_knots", "v_knot_mult");
        return true;
    }
    bool curve(const xtocc::Node *n, xtref::Curve &c) {
        if (m.is(n, "LINE")) { c.kind = xtref::Curve::LINE; c.p = m.vec(n, "pvec"); c.d = m.vec(n, "direction"); }
        else if (m.is(n, "CIRCLE")) { c.kind = xtref::Curve::CIRCLE; c.p = m.vec(n, "centre"); c.n = m.vec(n, "normal"); c.x = m.vec(n, "x_axis"); c.r = m.dbl(n, "radius"); }
        else if (m.is(n, "ELLIPSE")) { c.kind = xtref::Curve::ELLIPSE; c.p = m.vec(n, "centre"); c.n = m.vec(n, "normal"); c.x = m.vec(n, "x_axis");
                                       c.r = m.dbl(n, "major_radius"); c.r2 = m.dbl(n, "minor_radius"); }
        else if (m.is(n, "B_CURVE")) {
            const xtocc::Node *nb = m.pnode(n, "nurbs");
            c.kind = xtref::Curve::BCURVE;
            int16_t deg, dim; for (auto &d : m.s->nd[nb->type].f) { if (d.xmt && d.name == "degree") memcpy(&deg, nb->p + d.off, 2); if (d.xmt && d.name == "vertex_dim") memcpy(&dim, nb->p + d.off, 2); }
            c.b.deg = deg; c.b.dim = dim; c.rational = m.chr(nb, "rational");
            c.b.v = var(m.pnode(nb, "bspline_vertices"), "vertices", 'f');
            std::vector<double> kk = var(m.pnode(nb, "knots"), "knots", 'f'), mu = var(m.pnode(nb, "knot_mult"), "mult", 'n');
            c.b.knots = xtref::expand(kk, std::vector<int>(mu.begin(), mu.end()));
        } else return false;
        if (m.chr(n, "sense") == '-') { fails.push_back("curve sense '-' (writer never writes it)"); }
        return true;
    }
    /* (u, v) of the point of a B_SURFACE nearest q: grid + pattern search on the XT formula (ours) */
    double sdist(const xtref::Surface &x, const gp_XYZ &q, double &u, double &v) {
        double u0 = x.uk[x.udeg], u1 = x.uk[x.nu], v0 = x.vk[x.vdeg], v1 = x.vk[x.nv];
        std::vector<std::tuple<double, double, double>> g;
        for (int a = 0; a <= 80; a++) for (int b = 0; b <= 80; b++) {
            double uu = u0 + (u1 - u0) * a / 80, vv = v0 + (v1 - v0) * b / 80;
            g.emplace_back((x.eval(uu, vv) - q).Modulus(), uu, vv);
        }
        std::partial_sort(g.begin(), g.begin() + 6, g.end());
        double best = 1e300;
        for (int s0 = 0; s0 < 6; s0++) {      /* multi-start: a point near a periodic seam has two basins */
            double d = std::get<0>(g[s0]), uu = std::get<1>(g[s0]), vv = std::get<2>(g[s0]);
            double hu = (u1 - u0) / 80, hv = (v1 - v0) / 80;
            for (int it = 0; it < 120; it++) {
                bool imp = false;
                for (int k = 0; k < 4; k++) {
                    double a2 = std::min(u1, std::max(u0, uu + (k == 0 ? hu : k == 1 ? -hu : 0))), b2 = std::min(v1, std::max(v0, vv + (k == 2 ? hv : k == 3 ? -hv : 0)));
                    double dd = (x.eval(a2, b2) - q).Modulus();
                    if (dd < d) { d = dd; uu = a2; vv = b2; imp = true; }
                }
                if (!imp) { hu *= 0.5; hv *= 0.5; }
            }
            if (d < best) { best = d; u = uu; v = vv; }
        }
        return best;
    }
    /* distance from q to the XT curve, coarse-to-fine search over its parameter (ours) */
    double cdist(const xtref::Curve &c, const gp_XYZ &q, double &tbest) {
        double t0, t1;
        if (c.kind == xtref::Curve::LINE) { tbest = (q - c.p).Dot(c.d); return (c.eval(tbest) - q).Modulus(); }
        if (c.kind == xtref::Curve::BCURVE) { t0 = c.b.knots[c.b.deg]; t1 = c.b.knots[c.b.n()]; } else { t0 = 0; t1 = 2 * M_PI; }
        double best = 1e300; tbest = t0;
        for (int k = 0; k <= 720; k++) { double t = t0 + (t1 - t0) * k / 720; double d = (c.eval(t) - q).Modulus(); if (d < best) { best = d; tbest = t; } }
        double h = (t1 - t0) / 720;
        for (int it = 0; it < 60; it++) {
            for (double t : {tbest - h, tbest + h}) { double d = (c.eval(t) - q).Modulus(); if (d < best) { best = d; tbest = t; } }
            h *= 0.6;
        }
        return best;
    }
};

static bool check_written(const std::vector<uint8_t> &xb, const xtocc::Schema &S, const TopoDS_Shape &shape,
                          const std::map<int, TopoDS_Face> &face_by_id, const std::map<int, TopoDS_Edge> &edge_by_id,
                          double scale, Oracle *&out, xtocc::Model &M, std::string &err) {
    if (!xtocc::parse(S, xb.data(), xb.size(), M, err)) return false;
    out = new Oracle(M, scale);
    Oracle &O = *out;
    const char *ord = getenv("OCCXT_BSURF_ORDER");
    BRepMesh_IncrementalMesh(shape, 0.01, false, 0.1, true);
    for (size_t i = 0; i < M.by_idx.size(); i++) {
        const xtocc::Node *n = M.node((int)i);
        if (!n) continue;
        if (M.is(n, "FACE")) {
            O.nface++;
            int id = M.ival(n, "node_id");
            auto it = face_by_id.find(id);
            if (it == face_by_id.end()) { O.fails.push_back("face id " + std::to_string(id) + " unknown"); continue; }
            const TopoDS_Face &F = it->second;
            const xtocc::Node *sn = M.pnode(n, "surface");
            xtref::Surface xs;
            bool bs = false;
            if (!O.surf(sn, xs)) { if (!O.bsurf(sn, xs, false /* u-major, decoded from data */)) { O.fails.push_back("face surface unreadable"); continue; } bs = true; }
            int sgn = (M.chr(n, "sense") == '-' ? -1 : 1) * (M.chr(sn, "sense") == '-' ? -1 : 1);
            TopLoc_Location L;
            Handle(Poly_Triangulation) T = BRep_Tool::Triangulation(F, L);
            Handle(Geom_Surface) gs = BRep_Tool::Surface(F);
            if (T.IsNull()) { O.fails.push_back("no triangulation"); continue; }
            for (int k = 1; k <= T->NbNodes(); k++) {
                gp_Pnt P = T->Node(k).Transformed(L.Transformation());
                gp_XYZ q = P.XYZ() / scale;
                double u, v;
                if (bs && !getenv("OCCXT_FORCE_SEARCH") && !Handle(Geom_BSplineSurface)::DownCast(gs).IsNull()) { gp_Pnt2d uv = T->UVNode(k); u = uv.X(); v = uv.Y(); }
                else if (bs) { if (k % 7) continue; O.sdist(xs, q, u, v); }
                else if (!xs.invert(q, u, v)) continue;
                double d = (xs.eval(u, v) - q).Modulus();
                if (getenv("OCCXT_DEBUG") && d > 1e-9) fprintf(stderr, "face %d node %d q %.6g %.6g %.6g u %.6g v %.6g d %.3g\n", id, k, q.X(), q.Y(), q.Z(), u, v, d);
                O.max_face = std::max(O.max_face, d);
                /* orientation: XT face normal (natural x senses) vs OCCT outward normal at that point */
                gp_Pnt2d uv = T->UVNode(k);
                gp_Pnt pp; gp_Vec du, dv; gs->D1(uv.X(), uv.Y(), pp, du, dv);
                gp_Vec no = du.Crossed(dv); if (F.Orientation() == TopAbs_REVERSED) no.Reverse();
                gp_XYZ nx = xs.normal(u, v) * sgn;
                if (no.Magnitude() > 1e-9 && nx.Modulus() > 1e-40 && nx.Normalized().Dot(no.XYZ().Normalized()) < 0.5)
                    { O.fails.push_back("face " + std::to_string(id) + ": normal opposes OCCT's"); break; }
            }
            /* planar loops: signed area along the XT fins (curve sampling) = OCCT face area */
            if (xs.kind == xtref::Surface::PLANE) {
                gp_XYZ N = xs.a * sgn, X = xs.x, Y = N.Crossed(X);
                double A = 0;
                for (const xtocc::Node *lp = M.pnode(n, "loop"); lp; lp = M.pnode(lp, "next")) {
                    const xtocc::Node *h0 = M.pnode(lp, "halfedge"), *h = h0;
                    int guard = 0;
                    do {
                        const xtocc::Node *e = M.pnode(h, "edge"), *cn = M.pnode(e, "curve");
                        xtref::Curve c; O.curve(cn, c);
                        const xtocc::Node *vend = M.pnode(h, "vertex"), *vst = M.pnode(M.pnode(h, "other"), "vertex");
                        double ta, tb;
                        bool fwd = M.chr(h, "sense") != '-';
                        if (vend && vst) {
                            O.cdist(c, M.vec(M.pnode(vst, "point"), "pvec"), ta); O.cdist(c, M.vec(M.pnode(vend, "point"), "pvec"), tb);
                        } else { ta = 0; tb = 2 * M_PI; if (!fwd) std::swap(ta, tb); }
                        if (c.kind != xtref::Curve::LINE && c.kind != xtref::Curve::BCURVE && vend && vst) {
                            if (fwd) { while (tb <= ta) tb += 2 * M_PI; } else { while (tb >= ta) tb -= 2 * M_PI; }
                        }
                        int ns = c.kind == xtref::Curve::LINE ? 1 : 256;
                        for (int k = 0; k < ns; k++) {
                            gp_XYZ a = c.eval(ta + (tb - ta) * k / ns), b = c.eval(ta + (tb - ta) * (k + 1) / ns);
                            A += 0.5 * (a.Dot(X) * b.Dot(Y) - a.Dot(Y) * b.Dot(X));
                        }
                        h = M.pnode(h, "forward");
                    } while (h && h != h0 && ++guard < 100000);
                }
                GProp_GProps g; BRepGProp::SurfaceProperties(F, g);
                double Ao = g.Mass() / (scale * scale);
                double rel = std::abs(A - Ao) / std::max(Ao, 1e-30);
                O.max_area_rel = std::max(O.max_area_rel, rel);
                if (A <= 0 || rel > 1e-3) O.fails.push_back("planar face " + std::to_string(id) + ": loop area " + std::to_string(A) + " vs OCCT " + std::to_string(Ao));
            }
        } else if (M.is(n, "EDGE")) {
            O.nedge++;
            int id = M.ival(n, "node_id");
            auto it = edge_by_id.find(id);
            if (it == edge_by_id.end()) { O.fails.push_back("edge id unknown"); continue; }
            TopoDS_Edge E = TopoDS::Edge(it->second.Oriented(TopAbs_FORWARD));
            xtref::Curve c;
            if (!O.curve(M.pnode(n, "curve"), c)) { O.fails.push_back("edge curve unreadable"); continue; }
            double f, l; Handle(Geom_Curve) gc = BRep_Tool::Curve(E, f, l);
            double tprev = 0;
            for (int k = 0; k <= 16; k++) {
                gp_Pnt P; gp_Vec D; gc->D1(f + (l - f) * k / 16, P, D);
                double t; double d = O.cdist(c, P.XYZ() / scale, t);
                O.max_edge = std::max(O.max_edge, d);
                /* direction: XT natural tangent (curve sense '+') along OCCT's FORWARD direction (positive fin) */
                gp_XYZ tg = c.eval(t + 1e-7) - c.eval(t - 1e-7);
                if (tg.Modulus() > 0 && D.Magnitude() > 0 && tg.Normalized().Dot(D.XYZ().Normalized()) < 0.5)
                    { O.fails.push_back("edge " + std::to_string(id) + ": curve direction opposes the edge"); break; }
                (void)tprev;
            }
            /* positive fin's vertex = OCCT last vertex; other fin's = first (ref 93) */
            const xtocc::Node *h = M.pnode(n, "halfedge");
            if (M.chr(h, "sense") != '+') O.fails.push_back("EDGE.halfedge not positive");
            const xtocc::Node *ve = M.pnode(h, "vertex"), *vs = M.pnode(M.pnode(h, "other"), "vertex");
            TopoDS_Vertex a, b; TopExp::Vertices(E, a, b);
            if (ve && vs) {
                double d1 = (M.vec(M.pnode(ve, "point"), "pvec") - BRep_Tool::Pnt(b).XYZ() / scale).Modulus();
                double d0 = (M.vec(M.pnode(vs, "point"), "pvec") - BRep_Tool::Pnt(a).XYZ() / scale).Modulus();
                if (std::max(d0, d1) > 1e-9) O.fails.push_back("edge " + std::to_string(id) + ": vertices swapped / off");
            }
        } else if (M.is(n, "HALFEDGE")) {
            const xtocc::Node *f = M.pnode(n, "forward"), *o = M.pnode(n, "other");
            if (!f || !o) { O.fails.push_back("fin without forward / other"); continue; }
            if (M.ptr(M.pnode(f, "other"), "vertex") != M.ptr(n, "vertex")) O.fails.push_back("loop not nose to tail");
            if (M.chr(o, "sense") == M.chr(n, "sense")) O.fails.push_back("edge fins with equal senses");
            if (M.pnode(o, "other") != n) O.fails.push_back("fin other ring broken");
        }
    }
    return true;
}

static TopoDS_Shape gscale(const TopoDS_Shape &s, double sx, double sy, double sz) {
    gp_GTrsf g; g.SetValue(1, 1, sx); g.SetValue(2, 2, sy); g.SetValue(3, 3, sz);
    return BRepBuilderAPI_GTransform(s, g, true).Shape();
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: see header\n"); return 2; }
    std::string mode = argv[1];
    auto S = xtocc::load_schema(argv[2]);
    if (!S) { fprintf(stderr, "schema?\n"); return 2; }
    XtSchema *xs = xt_schema_load(argv[2]);
    if (mode == "rt") {
        std::string outdir = argv[4];
        double scale = argc > 5 ? atof(argv[5]) : 1000;
        std::ifstream man(argv[3]);
        std::string line;
        printf("part\tsolid\tstatus\txt_in\txt_out\tseams_dropped\tring\tvol0\tdV_rel\tdA_rel\tvalid2\txt_mesh_faces\txt_mesh_meshed\tmax_occ_tol\tnotes\n");
        int ok = 0, tot = 0;
        while (std::getline(man, line)) {
            std::istringstream ls(line); std::string part, sld, path;
            std::getline(ls, part, '\t'); std::getline(ls, sld, '\t'); std::getline(ls, path, '\t');
            tot++;
            auto blob = slurp(path);
            xtocc::Model M; std::string err;
            xtocc::Result R;
            if (!xtocc::parse(*S, blob.data(), blob.size(), M, err) || !xtocc::build(M, scale, R)) {
                printf("%s\t%s\tREAD_FAIL\t\t\t\t\t\t\t\t\t\t\t\t%s\n", part.c_str(), sld.c_str(), (err + R.error).c_str()); continue;
            }
            occxt::Options o; o.scale = scale; o.key = sld;
            if (std::string(blob.begin(), blob.begin() + std::min<size_t>(blob.size(), 3000)).find("SCH_2800143_28002") != std::string::npos) o.pk = 28;
            /* keep DATE from the original header so the header compares equal */
            std::string hs(blob.begin(), blob.begin() + std::min<size_t>(blob.size(), 2000));
            size_t dp = hs.find("DATE="); if (dp != std::string::npos) o.date = hs.substr(dp + 5, hs.find(';', dp) - dp - 5);
            for (auto &kv : R.face_of) o.node_id.Bind(kv.second, M.ival(M.node(kv.first), "node_id"));
            for (auto &kv : R.edge_of) o.node_id.Bind(kv.second, M.ival(M.node(kv.first), "node_id"));
            for (auto &kv : R.vertex_of) o.node_id.Bind(kv.second, M.ival(M.node(kv.first), "node_id"));
            std::vector<uint8_t> out; occxt::Report rep;
            if (!occxt::write(R.shape, *S, o, out, rep)) {
                std::string u; for (auto &x : rep.unmapped) u += x + "; ";
                printf("%s\t%s\tWRITE_FAIL\t%zu\t\t\t\t\t\t\t\t\t\t\t%s %s\n", part.c_str(), sld.c_str(), blob.size(), rep.error.c_str(), u.c_str()); continue;
            }
            mkdir(outdir.c_str(), 0755); mkdir((outdir + "/" + part).c_str(), 0755);
            std::ofstream(outdir + "/" + part + "/" + sld + ".x_b", std::ios::binary).write((const char *)out.data(), out.size());
            xtocc::Model M2; xtocc::Result R2; std::string e2;
            bool rd = xtocc::parse(*S, out.data(), out.size(), M2, e2) && xtocc::build(M2, scale, R2);
            double v0, a0, v1 = 0, a1 = 0; props(R.shape, v0, a0);
            bool valid = false;
            if (rd) { props(R2.shape, v1, a1); valid = BRepCheck_Analyzer(R2.shape).IsValid(); }
            XtMesh xm{}; int mok = xs ? xt_mesh(xs, out.data(), out.size(), &xm) : 0;
            /* xt_mesh (the display mesher) meshes plane / cylinder faces only: its result is reported, not required (2026-11-18) */
            bool good = rd && valid && std::abs(v1 - v0) / v0 < 1e-9 && std::abs(a1 - a0) / a0 < 1e-9;
            ok += good;
            printf("%s\t%s\t%s\t%zu\t%zu\t%d\t%d\t%.6f\t%.2e\t%.2e\t%d\t%d\t%d\t%.2e\t%s\n", part.c_str(), sld.c_str(),
                   good ? "OK" : (rd ? "MISMATCH" : ("REREAD_FAIL " + e2 + R2.error).c_str()), blob.size(), out.size(),
                   rep.seams_dropped, rep.ring_edges, v0, rd ? (v1 - v0) / v0 : NAN, rd ? (a1 - a0) / a0 : NAN, valid,
                   xm.nfaces, xm.nfaces_meshed, rep.max_occ_tolerance, mok ? "" : xm.error);
            xt_mesh_free(&xm);
        }
        fprintf(stderr, "round trip: %d / %d OK\n", ok, tot);
        return 0;
    }
    if (mode == "synth") {
        std::string outdir = argv[3];
        mkdir(outdir.c_str(), 0755);
        struct Case { std::string name; TopoDS_Shape s; };
        std::vector<Case> cs;
        cs.push_back({"box", BRepPrimAPI_MakeBox(gp_Pnt(-5, -5, -6), 10, 10, 6).Shape()});
        cs.push_back({"cylinder", BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(1, 2, 3), gp_Dir(0, 0, 1)), 7.5, 12).Shape()});
        cs.push_back({"cone", BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 1)), 10, 4, 15).Shape()});
        cs.push_back({"cone_inv", BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 3, 9, 8).Shape()});
        cs.push_back({"sphere", BRepPrimAPI_MakeSphere(gp_Pnt(3, 0, 0), 6).Shape()});
        cs.push_back({"torus", BRepPrimAPI_MakeTorus(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), 20, 4).Shape()});
        {   TopoDS_Shape b = BRepPrimAPI_MakeBox(40, 30, 10).Shape();
            BRepFilletAPI_MakeFillet mf(b);
            for (TopExp_Explorer x(b, TopAbs_EDGE); x.More(); x.Next()) mf.Add(2.0, TopoDS::Edge(x.Current()));
            cs.push_back({"box_fillet_all", mf.Shape()}); }
        {   TopoDS_Shape b = BRepPrimAPI_MakeBox(40, 30, 10).Shape();
            BRepFilletAPI_MakeChamfer mc(b);
            TopExp_Explorer x(b, TopAbs_EDGE); mc.Add(1.5, TopoDS::Edge(x.Current()));
            cs.push_back({"box_chamfer", mc.Shape()}); }
        {   TopoDS_Shape b = BRepPrimAPI_MakeBox(gp_Pnt(-20, -20, 0), 40, 40, 10).Shape();
            TopoDS_Shape c = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1), gp_Dir(0, 0, 1)), 6, 20).Shape();
            TopoDS_Shape cut = BRepAlgoAPI_Cut(b, c).Shape();
            cs.push_back({"box_minus_cyl", cut});
            BRepFilletAPI_MakeFillet mf(cut);   /* torus fillet round the hole's top edge */
            for (TopExp_Explorer x(cut, TopAbs_EDGE); x.More(); x.Next()) {
                double f, l; Handle(Geom_Curve) g = BRep_Tool::Curve(TopoDS::Edge(x.Current()), f, l);
                if (!g.IsNull() && g->DynamicType() == STANDARD_TYPE(Geom_Circle) && BRep_Tool::Pnt(TopExp::FirstVertex(TopoDS::Edge(x.Current()))).Z() > 5)
                    mf.Add(1.5, TopoDS::Edge(x.Current()));
            }
            mf.Build();
            if (mf.IsDone()) cs.push_back({"hole_fillet_torus", mf.Shape()}); }
        {   gp_Elips el(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 12, 5);
            TopoDS_Face f = BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(el)).Wire());
            cs.push_back({"ellipse_prism", BRepPrimAPI_MakePrism(f, gp_Vec(0, 0, 7)).Shape()}); }
        cs.push_back({"cyl_scaled_xyz", gscale(BRepPrimAPI_MakeCylinder(5, 10).Shape(), 2, 1, 1.5)});
        cs.push_back({"box_scaled_xyz", gscale(BRepPrimAPI_MakeBox(10, 10, 10).Shape(), 1, 2, 3)});
        {   TColgp_Array1OfPnt pts(1, 5);
            pts(1) = gp_Pnt(5, 0, 0); pts(2) = gp_Pnt(8, 0, 3); pts(3) = gp_Pnt(6, 0, 6); pts(4) = gp_Pnt(9, 0, 9); pts(5) = gp_Pnt(5, 0, 12);
            Handle(Geom_BSplineCurve) bc = GeomAPI_PointsToBSpline(pts).Curve();
            BRepBuilderAPI_MakeWire mw;
            mw.Add(BRepBuilderAPI_MakeEdge(bc).Edge());
            mw.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(5, 0, 12), gp_Pnt(0, 0, 12)).Edge());
            mw.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 12), gp_Pnt(0, 0, 0)).Edge());
            mw.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(5, 0, 0)).Edge());
            TopoDS_Face f = BRepBuilderAPI_MakeFace(mw.Wire(), true);
            cs.push_back({"revolved_spline", BRepPrimAPI_MakeRevol(f, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), M_PI).Shape()}); }
        printf("case\tstatus\tvalid_in\tfaces\tedges\tmax_face_dev_m\tmax_edge_dev_m\tmax_planar_area_rel\tcounts\tnotes\n");
        for (auto &c : cs) {
            bool vin = BRepCheck_Analyzer(c.s).IsValid();
            occxt::Options o; o.scale = 1000; o.key = c.name; o.date = "Thu Oct 23 12:00:00 2026";
            std::map<int, TopoDS_Face> fb; std::map<int, TopoDS_Edge> eb;
            int id = 1;
            TopTools_IndexedMapOfShape fm, em; TopExp::MapShapes(c.s, TopAbs_FACE, fm); TopExp::MapShapes(c.s, TopAbs_EDGE, em);
            for (TopExp_Explorer x(c.s, TopAbs_FACE); x.More(); x.Next()) {
                if (o.node_id.IsBound(x.Current())) continue;
                o.node_id.Bind(x.Current(), id); fb[id++] = TopoDS::Face(x.Current());
            }
            for (int k = 1; k <= em.Extent(); k++) { o.node_id.Bind(em(k), id); eb[id++] = TopoDS::Edge(em(k)); }
            if (c.name == "box_chamfer") o.face_color.Bind(fm(1), 12);   /* CNC/mcColor on one face (12 = a sample value) */
            std::vector<uint8_t> out; occxt::Report rep;
            std::string counts, notes;
            if (!occxt::write(c.s, *S, o, out, rep)) {
                for (auto &u : rep.unmapped) notes += u + "; ";
                printf("%s\tWRITE_REFUSED\t%d\t\t\t\t\t\t\t%s %s\n", c.name.c_str(), vin, rep.error.c_str(), notes.c_str()); continue;
            }
            std::ofstream(outdir + "/" + c.name + ".x_b", std::ios::binary).write((const char *)out.data(), out.size());
            for (auto &kv : rep.counts) counts += kv.first + "=" + std::to_string(kv.second) + " ";
            for (auto &n : rep.notes) notes += n + "; ";
            notes += "seams " + std::to_string(rep.seams_dropped) + ", degenerate " + std::to_string(rep.degenerate_dropped) + ", rings " + std::to_string(rep.ring_edges);
            char tb[64]; snprintf(tb, sizeof tb, ", max OCCT tol %.2g mm", rep.max_occ_tolerance); notes += tb;
            Oracle *O = nullptr; xtocc::Model M; std::string err;
            if (!check_written(out, *S, c.s, fb, eb, 1000, O, M, err)) { printf("%s\tPARSE_FAIL\t%d\t\t\t\t\t\t%s\t%s\n", c.name.c_str(), vin, counts.c_str(), err.c_str()); continue; }
            std::string fl; for (size_t k = 0; k < O->fails.size() && k < 4; k++) fl += O->fails[k] + "; ";
            bool good = O->fails.empty() && O->max_face < 1e-9 && O->max_edge < 1e-9;
            XtMesh xm{}; int mok = xs ? xt_mesh(xs, out.data(), out.size(), &xm) : 0;
            notes += mok ? "; xt_mesh parse ok (" + std::to_string(xm.nfaces_meshed) + "/" + std::to_string(xm.nfaces) + " meshed)" : std::string("; xt_mesh: ") + xm.error;
            xt_mesh_free(&xm);
            printf("%s\t%s\t%d\t%d\t%d\t%.2e\t%.2e\t%.2e\t%s\t%s%s\n", c.name.c_str(), good ? "OK" : "FAIL", vin, O->nface, O->nedge,
                   O->max_face, O->max_edge, O->max_area_rel, counts.c_str(), fl.c_str(), notes.c_str());
            delete O;
        }
        return 0;
    }
    return 2;
}
