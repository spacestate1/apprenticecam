/* occt_xt.cpp - OpenCASCADE -> Parasolid XT (binary transmit, Mastercam's SOLIDXMT payload). Sources: occt_xt.h. */
#include "occt_xt.h"
#include "xt_refgeom.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <set>

#include <BRepClass3d.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <BSplCLib.hxx>
#include <Standard_Failure.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomConvert.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom_Circle.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Hyperbola.hxx>
#include <Geom_Line.hxx>
#include <Geom_Parabola.hxx>
#include <Geom_Plane.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopTools_DataMapIteratorOfDataMapOfShapeInteger.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>

namespace occxt {
namespace {

const double NULL_DOUBLE = -31415800000000.0;    /* Parasolid's null double (ref 16); every FACE / EDGE / VERTEX
                                                    tolerance in the 108 user solids (survey.txt) */
/* ------------------------------------------------------------------------------------------- node store */
struct WNode { int type = 0; std::map<std::string, std::vector<double>> f; };

struct Store {
    const xtocc::Schema &s;
    std::vector<WNode> n;                       /* n[0] unused: index 0 = null pointer (ref 3) */
    std::string err;
    explicit Store(const xtocc::Schema &s_) : s(s_) { n.resize(1); }
    int code(const char *name) const {
        for (size_t i = 0; i < s.nd.size(); i++) if (s.nd[i].defined && s.nd[i].name == name) return (int)i;
        return 0;
    }
    int add(const char *name) {
        int c = code(name);
        if (!c && err.empty()) err = std::string("schema has no node ") + name;
        n.push_back(WNode()); n.back().type = c;
        return (int)n.size() - 1;
    }
    void set(int i, const char *f, double v) { n[i].f[f] = {v}; }
    void setv(int i, const char *f, const gp_XYZ &v) { n[i].f[f] = {v.X(), v.Y(), v.Z()}; }
    void seta(int i, const char *f, const std::vector<double> &v) { n[i].f[f] = v; }
    const char *tname(int i) const { return s.nd[n[i].type].name.c_str(); }
    bool has_field(int i, const char *f) const {
        for (auto &d : s.nd[n[i].type].f) if (d.xmt && d.name == f) return true;
        return false;
    }
};

/* binary encodings: formats/xt.md, ref 16-17 (bare binary, little-endian as in every Mastercam blob) */
struct Out {
    std::vector<uint8_t> &b;
    void u8(int v) { b.push_back((uint8_t)v); }
    void i16(int v) { u8(v & 0xff); u8((v >> 8) & 0xff); }
    void i32(int64_t v) { uint32_t u = (uint32_t)(int32_t)v; for (int k = 0; k < 4; k++) u8((u >> (8 * k)) & 0xff); }
    void f64(double d) { uint8_t t[8]; memcpy(t, &d, 8); b.insert(b.end(), t, t + 8); }
    void ptr(int idx) {                         /* ref 17: index + 1 as a short; big indices as a pair */
        if (idx < 32767) i16(idx + 1);
        else { i16(-(idx % 32767 + 1)); i16(idx / 32767); }
    }
};

/* ------------------------------------------------------------------------------------------- writer */
struct Writer {
    const Options &o;
    Report &r;
    Store st;
    double S;                                   /* metres per model unit (1 / scale) */
    int body = 0, attdef = 0;
    std::vector<int> attrs;
    std::vector<int> faces_n, edges_n, verts_n, points_n, curves_n, surfs_n;
    std::map<const void *, int> vnode, enode;   /* TShape -> VERTEX / EDGE node */
    TopTools_IndexedMapOfShape keys;            /* ours: a key per sub-shape, TShape + Location (IsSame), orientation-free */
    const void *key(const TopoDS_Shape &x) { return (const void *)(intptr_t)keys.Add(x); }
    std::map<int, TopoDS_Shape> node_shape;     /* FACE / EDGE / VERTEX node -> its shape (node_id hints) */
    const char *bsurf_order = getenv("OCCXT_BSURF_ORDER");
    Handle(Geom_BSplineSurface) last_bs;        /* the B-surface just written (its parameters = the XT ones) */
    Writer(const xtocc::Schema &s, const Options &o_, Report &r_) : o(o_), r(r_), st(s), S(1.0 / o_.scale) {}

    gp_XYZ m(const gp_XYZ &p) const { return p * S; }     /* model units -> metres */

    void attribute(int owner) {                 /* CNC/mcEptrPtr on faces and edges (survey: 4810 = 1445 + 3365) */
        if (!o.mc_attributes) return;
        int a = st.add("ATTRIBUTE");
        st.set(a, "definition", attdef); st.set(a, "owner", owner);
        st.seta(a, "fields", {0});              /* one pointer field, transmitted empty (ref 104; survey fields=[null]) */
        st.set(owner, "attributes_features", a);
        attrs.push_back(a);
    }
    /* CNC/mcColor: Mastercam's per-face colour (an int, the colour index), seen on 93 faces of the internet samples
       (netx/attdefs.py): ATTRIB_DEF type 9000, one int field, actions as mcEptrPtr, legal owners face + edge; on a
       face it comes first in the attribute chain, then mcEptrPtr (0104_MULTIAXIS_ROUGHING_FALSE_FLOOR) */
    int coldef = 0;
    std::vector<int> colattrs;
    void colour(int owner, int c) {
        if (!coldef) {
            coldef = st.add("ATTRIB_DEF"); int id = st.add("ATT_DEF_ID");
            std::string nm = "CNC/mcColor";
            st.seta(id, "string", std::vector<double>(nm.begin(), nm.end()));
            st.set(coldef, "next", 0); st.set(coldef, "identifier", id); st.set(coldef, "type_id", 9000);
            st.seta(coldef, "actions", {0, 0, 0, 0, 3, 6, 0, 0}); st.set(coldef, "field_names", 0);
            st.seta(coldef, "legal_owners", {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0});
            st.seta(coldef, "fields", {1});      /* SCH_int_field (ref 101) */
        }
        int a = st.add("ATTRIBUTE"), iv = st.add("INT_VALUES");
        st.seta(iv, "values", {(double)c});
        st.set(a, "definition", coldef); st.set(a, "owner", owner); st.seta(a, "fields", {(double)iv});
        int old = st.n[owner].f.count("attributes_features") ? (int)st.n[owner].f["attributes_features"][0] : 0;
        st.set(a, "next", old); st.set(a, "previous", 0);
        if (old) st.set(old, "previous", a);
        st.set(owner, "attributes_features", a);
        colattrs.push_back(a);
    }

    /* ---- curves (ref 29-38) */
    void nurbs_put(int nb, int deg, bool periodic, bool closed, bool rational, int dim,
                   const std::vector<double> &verts, const std::vector<double> &k, const std::vector<int> &mu, const char *pre) {
        (void)pre;
        st.set(nb, "degree", deg); st.set(nb, "n_vertices", (double)(verts.size() / dim)); st.set(nb, "vertex_dim", dim);
        st.set(nb, "n_knots", (double)k.size());
        st.set(nb, "knot_type", 1);             /* SCH_unset (ref 35): we make no claim about the knot spacing */
        st.set(nb, "periodic", periodic); st.set(nb, "closed", closed); st.set(nb, "rational", rational);
        st.set(nb, "curve_form", 1);            /* SCH_unset (ref 35) */
        int bv = st.add("BSPLINE_VERTICES"); st.seta(bv, "vertices", verts);
        int km = st.add("KNOT_MULT"); std::vector<double> md(mu.begin(), mu.end()); st.seta(km, "mult", md);
        int ks = st.add("KNOT_SET"); st.seta(ks, "knots", k);
        st.set(nb, "bspline_vertices", bv); st.set(nb, "knot_mult", km); st.set(nb, "knots", ks);
    }
    static void distinct(const std::vector<double> &flat, std::vector<double> &k, std::vector<int> &mu) {
        for (double t : flat) { if (!k.empty() && t == k.back()) mu.back()++; else { k.push_back(t); mu.push_back(1); } }
    }
    /* poles (+ weights) of a B-spline in Parasolid form; periodic OCCT curves become Parasolid periodic curves:
       the first `degree` vertices repeated at the end, n_knots = n_vertices + degree + 1 (ref 34) - verified here by
       evaluating the result against OCCT (ours) */
    bool bcurve(const Handle(Geom_BSplineCurve) &bc, xtref::Curve &xc, int &node) {
        int deg = bc->Degree(), np = bc->NbPoles();
        bool rat = bc->IsRational(), per = bc->IsPeriodic();
        int dim = rat ? 4 : 3;
        TColStd_Array1OfReal kn(1, bc->NbKnots()); bc->Knots(kn);
        TColStd_Array1OfInteger mu(1, bc->NbKnots()); bc->Multiplicities(mu);
        int len = BSplCLib::KnotSequenceLength(mu, deg, per);
        TColStd_Array1OfReal flat(1, len);
        BSplCLib::KnotSequence(kn, mu, deg, per, flat);
        std::vector<double> fv(flat.begin(), flat.end());
        int nv = len - deg - 1;
        auto vert = [&](int i, std::vector<double> &out) {
            gp_Pnt P = bc->Pole(i % np + 1); double w = rat ? bc->Weight(i % np + 1) : 1;
            gp_XYZ q = m(P.XYZ());
            out.push_back(q.X() * w); out.push_back(q.Y() * w); out.push_back(q.Z() * w);
            if (rat) out.push_back(w);
        };
        xc.kind = xtref::Curve::BCURVE; xc.rational = rat; xc.b.deg = deg; xc.b.dim = dim; xc.b.knots = fv;
        double best = 1e300; std::vector<double> bestv;
        for (int off = 0; off < (per ? np : 1); off++) {      /* ours: find the pole alignment of the periodic form */
            std::vector<double> v;
            for (int i = 0; i < nv; i++) vert(i + off, v);
            xc.b.v = v;
            double e = 0;
            double t0 = fv[deg], t1 = fv[nv];
            for (int k = 0; k <= 20; k++) {
                double t = t0 + (t1 - t0) * k / 20;
                e = std::max(e, (xc.eval(t) - m(bc->Value(t).XYZ())).Modulus());
            }
            if (e < best) { best = e; bestv = v; }
            if (e < 1e-14) break;
        }
        xc.b.v = bestv;
        if (best > 1e-12) { r.unmapped.push_back("B-spline curve: periodic conversion off by " + std::to_string(best) + " m"); return false; }
        std::vector<double> k; std::vector<int> mm; distinct(fv, k, mm);
        node = st.add("B_CURVE");
        int nb = st.add("NURBS_CURVE");
        nurbs_put(nb, deg, per, per || bc->IsClosed(), rat, dim, bestv, k, mm, "");
        st.set(node, "nurbs", nb);
        int cd = st.add("CURVE_DATA");          /* Parasolid writes one with every B_CURVE (3295 / 3295 in the samples);
                                                   self_int 1 = SCH_unset "check not performed" (ref 36; 163 samples) */
        st.set(cd, "self_int", 1); st.set(cd, "analytic_form", 0);
        st.set(node, "data", cd);
        return true;
    }
    int curve(const Handle(Geom_Curve) &c0, double f, double l, xtref::Curve &xc) {
        Handle(Geom_Curve) c = c0;
        while (auto t = Handle(Geom_TrimmedCurve)::DownCast(c)) c = t->BasisCurve();
        int node = 0;
        if (auto ln = Handle(Geom_Line)::DownCast(c)) {
            gp_Lin L = ln->Lin();
            node = st.add("LINE");
            xc.kind = xtref::Curve::LINE; xc.p = m(L.Location().XYZ()); xc.d = L.Direction().XYZ();
            /* the parameter: OCCT t (model units) = XT t (metres) * scale; the self-check below uses the XT one */
            st.setv(node, "pvec", xc.p); st.setv(node, "direction", xc.d);
        } else if (auto ci = Handle(Geom_Circle)::DownCast(c)) {
            gp_Circ C = ci->Circ();              /* gp_Ax2 is right-handed: Y = N x X as in ref 30 */
            node = st.add("CIRCLE");
            xc.kind = xtref::Curve::CIRCLE; xc.p = m(C.Location().XYZ()); xc.n = C.Axis().Direction().XYZ();
            xc.x = C.XAxis().Direction().XYZ(); xc.r = C.Radius() * S;
            st.setv(node, "centre", xc.p); st.setv(node, "normal", xc.n); st.setv(node, "x_axis", xc.x); st.set(node, "radius", xc.r);
        } else if (auto el = Handle(Geom_Ellipse)::DownCast(c)) {
            gp_Elips E = el->Elips();            /* XAxis = major axis (OCCT), x_axis = major axis (ref 32) */
            node = st.add("ELLIPSE");
            xc.kind = xtref::Curve::ELLIPSE; xc.p = m(E.Location().XYZ()); xc.n = E.Axis().Direction().XYZ();
            xc.x = E.XAxis().Direction().XYZ(); xc.r = E.MajorRadius() * S; xc.r2 = E.MinorRadius() * S;
            st.setv(node, "centre", xc.p); st.setv(node, "normal", xc.n); st.setv(node, "x_axis", xc.x);
            st.set(node, "major_radius", xc.r); st.set(node, "minor_radius", xc.r2);
        } else {
            Handle(Geom_BSplineCurve) bc = Handle(Geom_BSplineCurve)::DownCast(c);
            if (bc.IsNull() && (!Handle(Geom_BezierCurve)::DownCast(c).IsNull() || !Handle(Geom_Parabola)::DownCast(c).IsNull() ||
                                !Handle(Geom_Hyperbola)::DownCast(c).IsNull())) {
                bc = GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(c, f, l));   /* exact for these (ours) */
                r.notes.push_back(std::string("curve ") + c->DynamicType()->Name() + " written as an exact B-spline");
            }
            if (bc.IsNull()) { r.unmapped.push_back(std::string("curve ") + c->DynamicType()->Name()); return 0; }
            if (!bcurve(bc, xc, node)) return 0;
            c = bc;
        }
        st.set(node, "sense", '+');             /* ours: edge direction = OCCT parameter direction */
        /* self-check (ours): the XT formula reproduces the OCCT curve over the edge */
        for (int k = 0; k <= 8; k++) {
            double t = f + (l - f) * k / 8;
            double tx = xc.kind == xtref::Curve::LINE ? t * S : t;
            double d = (xc.eval(tx) - m(c->Value(t).XYZ())).Modulus();
            if (d > 1e-12 + 1e-12 * xc.eval(tx).Modulus()) {
                r.unmapped.push_back(std::string("curve ") + c->DynamicType()->Name() + ": XT form off by " + std::to_string(d) + " m");
                return 0;
            }
        }
        return node;
    }

    /* ---- surfaces (ref 52-70) */
    int surface(const Handle(Geom_Surface) &s0, const TopoDS_Face &F, xtref::Surface &xs) {
        Handle(Geom_Surface) s = s0;
        while (auto t = Handle(Geom_RectangularTrimmedSurface)::DownCast(s)) s = t->BasisSurface();
        int node = 0;
        if (auto pl = Handle(Geom_Plane)::DownCast(s)) {
            gp_Ax3 A = pl->Position();           /* natural normal of OCCT = X x Y; XT normal = that, Y = N x X */
            xs.kind = xtref::Surface::PLANE; xs.p = m(A.Location().XYZ()); xs.x = A.XDirection().XYZ();
            xs.a = A.XDirection().XYZ().Crossed(A.YDirection().XYZ());
            node = st.add("PLANE");
            st.setv(node, "pvec", xs.p); st.setv(node, "normal", xs.a); st.setv(node, "x_axis", xs.x);
        } else if (auto cy = Handle(Geom_CylindricalSurface)::DownCast(s)) {
            gp_Ax3 A = cy->Position();
            xs.kind = xtref::Surface::CYLINDER; xs.p = m(A.Location().XYZ()); xs.a = A.Direction().XYZ();
            xs.x = A.XDirection().XYZ(); xs.r = cy->Radius() * S;
            node = st.add("CYLINDER");
            st.setv(node, "pvec", xs.p); st.setv(node, "axis", xs.a); st.set(node, "radius", xs.r); st.setv(node, "x_axis", xs.x);
        } else if (auto co = Handle(Geom_ConicalSurface)::DownCast(s)) {
            /* OCCT: L + (R + v sin g)(cos u X + sin u Y) + v cos g Z.  XT: P + v A + (..)(r + v tan a), a > 0 (xt_refgeom.h:
               the sign is the one Mastercam's data shows, not ref 55's): A = +Z for g > 0, -Z for g < 0; same P, r, X */
            gp_Ax3 A = co->Position();
            double g = co->SemiAngle();
            xs.kind = xtref::Surface::CONE; xs.p = m(A.Location().XYZ()); xs.x = A.XDirection().XYZ();
            xs.a = A.Direction().XYZ() * (g > 0 ? 1.0 : -1.0); xs.r = co->RefRadius() * S;
            xs.sa = sin(std::abs(g)); xs.ca = cos(std::abs(g));
            node = st.add("CONE");
            st.setv(node, "pvec", xs.p); st.setv(node, "axis", xs.a); st.set(node, "radius", xs.r);
            st.set(node, "sin_half_angle", xs.sa); st.set(node, "cos_half_angle", xs.ca); st.setv(node, "x_axis", xs.x);
        } else if (auto sp = Handle(Geom_SphericalSurface)::DownCast(s)) {
            gp_Ax3 A = sp->Position();
            xs.kind = xtref::Surface::SPHERE; xs.p = m(A.Location().XYZ()); xs.a = A.Direction().XYZ();
            xs.x = A.XDirection().XYZ(); xs.r = sp->Radius() * S;
            node = st.add("SPHERE");
            st.setv(node, "centre", xs.p); st.set(node, "radius", xs.r); st.setv(node, "axis", xs.a); st.setv(node, "x_axis", xs.x);
        } else if (auto to = Handle(Geom_ToroidalSurface)::DownCast(s)) {
            if (!(to->MajorRadius() > to->MinorRadius())) {
                r.unmapped.push_back("torus with major <= minor radius (Parasolid apple / lemon split NOT DECODED)"); return 0;
            }
            gp_Ax3 A = to->Position();
            xs.kind = xtref::Surface::TORUS; xs.p = m(A.Location().XYZ()); xs.a = A.Direction().XYZ();
            xs.x = A.XDirection().XYZ(); xs.r = to->MajorRadius() * S; xs.r2 = to->MinorRadius() * S;
            node = st.add("TORUS");
            st.setv(node, "centre", xs.p); st.setv(node, "axis", xs.a); st.set(node, "major_radius", xs.r);
            st.set(node, "minor_radius", xs.r2); st.setv(node, "x_axis", xs.x);
        } else {
            Handle(Geom_BSplineSurface) bs = Handle(Geom_BSplineSurface)::DownCast(s);
            if (bs.IsNull() && (!Handle(Geom_BezierSurface)::DownCast(s).IsNull() || !Handle(Geom_SurfaceOfRevolution)::DownCast(s).IsNull() ||
                                !Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(s).IsNull())) {
                /* ours: exact rational conversion of the patch the face uses (unbounded surfaces cannot be converted) */
                double u0, u1, v0, v1; BRepTools::UVBounds(F, u0, u1, v0, v1);
                try {
                    Handle(Geom_Surface) b = s;
                    if (!Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(s).IsNull()) b = new Geom_RectangularTrimmedSurface(s, v0, v1, false);
                    bs = GeomConvert::SurfaceToBSplineSurface(b);
                } catch (Standard_Failure &e) { r.notes.push_back(std::string("B-surface conversion failed: ") + e.GetMessageString()); }
                if (!bs.IsNull()) r.notes.push_back(std::string("surface ") + s->DynamicType()->Name() + " written as a B-surface");
            }
            if (bs.IsNull()) { r.unmapped.push_back(std::string("surface ") + s->DynamicType()->Name()); return 0; }
            if (!bsurface(bs, xs, node)) return 0;
        }
        st.set(node, "sense", '+');             /* ours: orientation carried by FACE.sense (ref 78) */
        return node;
    }
    bool bsurface(const Handle(Geom_BSplineSurface) &bs0, xtref::Surface &xs, int &node) {
        Handle(Geom_BSplineSurface) bs = bs0;
        last_bs = bs;
        /* periodic directions in Parasolid's periodic form (ref 34 for curves, extended to surfaces ref 35): flat knots
           of one period plus wrap-round, the first `degree` vertex rows repeated; the pole alignment is found by
           evaluation (ours) */
        bool up = bs->IsUPeriodic(), vp = bs->IsVPeriodic();
        bool uc = up || bs->IsUClosed(), vc = vp || bs->IsVClosed();
        bool rat = bs->IsURational() || bs->IsVRational();
        int dim = rat ? 4 : 3, np_u = bs->NbUPoles(), np_v = bs->NbVPoles();
        auto flat = [](const TColStd_Array1OfReal &k, const TColStd_Array1OfInteger &mu, int deg, bool per) {
            int len = BSplCLib::KnotSequenceLength(mu, deg, per);
            TColStd_Array1OfReal f(1, len); BSplCLib::KnotSequence(k, mu, deg, per, f);
            return std::vector<double>(f.begin(), f.end()); };
        TColStd_Array1OfReal uk(1, bs->NbUKnots()), vk(1, bs->NbVKnots());
        TColStd_Array1OfInteger um(1, bs->NbUKnots()), vm(1, bs->NbVKnots());
        bs->UKnots(uk); bs->VKnots(vk); bs->UMultiplicities(um); bs->VMultiplicities(vm);
        xs.kind = xtref::Surface::BSURF; xs.udeg = bs->UDegree(); xs.vdeg = bs->VDegree();
        xs.uk = flat(uk, um, xs.udeg, up); xs.vk = flat(vk, vm, xs.vdeg, vp);
        int nu = (int)xs.uk.size() - xs.udeg - 1, nv = (int)xs.vk.size() - xs.vdeg - 1;
        xs.nu = nu; xs.nv = nv; xs.dim = dim; xs.rational = rat; xs.vmajor = bsurf_order && bsurf_order[0] == 'v';   /* u-major (xt_refgeom.h); the env
                                                               test hook writes the other order (mutation test) */
        double u0 = xs.uk[xs.udeg], u1 = xs.uk[nu], v0 = xs.vk[xs.vdeg], v1 = xs.vk[nv];
        auto fill = [&](int ou, int ov) {
            xs.verts.assign((size_t)nu * nv * dim, 0);
            for (int i = 0; i < nu; i++) for (int j = 0; j < nv; j++) {
                int pi = (i + ou) % np_u + 1, pj = (j + ov) % np_v + 1;
                gp_XYZ q = m(bs->Pole(pi, pj).XYZ()); double w = rat ? bs->Weight(pi, pj) : 1;
                size_t idx = xs.vmajor ? (size_t)j * nu + i : (size_t)i * nv + j;
                double *d = &xs.verts[idx * dim];
                d[0] = q.X() * w; d[1] = q.Y() * w; d[2] = q.Z() * w; if (rat) d[3] = w;
            }
            double e = 0;
            for (int a = 0; a <= 6; a++) for (int b = 0; b <= 6; b++) {
                double u = u0 + (u1 - u0) * a / 6, v = v0 + (v1 - v0) * b / 6;
                e = std::max(e, (xs.eval(u, v) - m(bs->Value(u, v).XYZ())).Modulus());
            }
            return e;
        };
        double e = 1e300; int bu = 0, bvo = 0;
        for (int ou = 0; ou < (up ? np_u : 1) && e > 1e-14; ou++)
            for (int ov = 0; ov < (vp ? np_v : 1) && e > 1e-14; ov++) { double x = fill(ou, ov); if (x < e) { e = x; bu = ou; bvo = ov; } }
        fill(bu, bvo);
        if (e > 1e-12) { r.unmapped.push_back("B-surface: XT form off by " + std::to_string(e) + " m"); return false; }
        node = st.add("B_SURFACE");
        int nb = st.add("NURBS_SURF");
        std::vector<double> k; std::vector<int> mm;
        st.set(nb, "u_periodic", up); st.set(nb, "v_periodic", vp);
        st.set(nb, "u_degree", xs.udeg); st.set(nb, "v_degree", xs.vdeg);
        st.set(nb, "n_u_vertices", nu); st.set(nb, "n_v_vertices", nv);
        st.set(nb, "u_knot_type", 1); st.set(nb, "v_knot_type", 1);          /* SCH_unset (ref 35, 67) */
        st.set(nb, "rational", rat); st.set(nb, "u_closed", uc); st.set(nb, "v_closed", vc);
        st.set(nb, "surface_form", 1);                                        /* SCH_unset (ref 66) */
        st.set(nb, "vertex_dim", dim);
        int bv = st.add("BSPLINE_VERTICES"); st.seta(bv, "vertices", xs.verts);
        std::vector<double> ukd, vkd; std::vector<int> umm, vmm;
        distinct(xs.uk, ukd, umm); distinct(xs.vk, vkd, vmm);
        st.set(nb, "n_u_knots", (double)ukd.size()); st.set(nb, "n_v_knots", (double)vkd.size());
        int ukm = st.add("KNOT_MULT"), vkm = st.add("KNOT_MULT"), uks = st.add("KNOT_SET"), vks = st.add("KNOT_SET");
        st.seta(ukm, "mult", std::vector<double>(umm.begin(), umm.end())); st.seta(vkm, "mult", std::vector<double>(vmm.begin(), vmm.end()));
        st.seta(uks, "knots", ukd); st.seta(vks, "knots", vkd);
        st.set(nb, "bspline_vertices", bv); st.set(nb, "u_knot_mult", ukm); st.set(nb, "v_knot_mult", vkm);
        st.set(nb, "u_knots", uks); st.set(nb, "v_knots", vks);
        st.set(node, "nurbs", nb); st.set(node, "data", 0);   /* SURFACE_DATA optional (ref 68) */
        return true;
    }

    /* XT face sense: '+' iff the face's outward normal (OCCT face orientation in its solid) agrees with the XT
       surface's natural normal from the reference formula (ref 78; surface sense '+') */
    int face_sense(const TopoDS_Face &F, const Handle(Geom_Surface) &gs, const xtref::Surface &xs) {
        double u0, u1, v0, v1;
        BRepTools::UVBounds(F, u0, u1, v0, v1);
        for (int k = 0; k < 9; k++) {
            double u = u0 + (u1 - u0) * (0.37 + 0.07 * k), v = v0 + (v1 - v0) * (0.41 + 0.05 * k);
            gp_Pnt P; gp_Vec du, dv;
            gs->D1(u, v, P, du, dv);
            gp_Vec no = du.Crossed(dv);
            if (no.Magnitude() < 1e-12 * std::max(1.0, du.Magnitude() * dv.Magnitude())) continue;
            gp_XYZ q = m(P.XYZ());
            double xu = u, xv = v;
            if (xs.kind == xtref::Surface::BSURF) {
                GeomAPI_ProjectPointOnSurf pr(P, last_bs);
                if (!pr.NbPoints()) continue;
                pr.LowerDistanceParameters(xu, xv);
                gp_Vec a1, a2; gp_Pnt pp; last_bs->D1(xu, xv, pp, a1, a2); no = a1.Crossed(a2);   /* the normal of the
                                                                         converted surface at the same point */
            } else if (!xs.invert(q, xu, xv)) continue;
            gp_XYZ back = xs.eval(xu, xv);
            if ((back - q).Modulus() > 1e-9) {
                r.unmapped.push_back(std::string("surface ") + gs->DynamicType()->Name() + ": point not reproduced by the XT form ("
                                     + std::to_string((back - q).Modulus()) + " m; e.g. a cone face on the other nappe)");
                return 0;
            }
            gp_XYZ nx = xs.normal(xu, xv);
            if (nx.Modulus() < 1e-40) continue;     /* singular point (pole, apex) */
            bool agree = nx.Dot(no.XYZ()) > 0;
            bool fwd = F.Orientation() == TopAbs_FORWARD;
            return fwd == agree ? '+' : '-';
        }
        r.unmapped.push_back("face: no regular point to orient it");
        return 0;
    }

    bool run(const TopoDS_Shape &shape);
};

struct FinT { TopoDS_Edge e; bool fwd; const void *ek; const void *vs, *ve; };   /* vs / ve: start / end vertex keys */

bool Writer::run(const TopoDS_Shape &shape) {
    std::vector<TopoDS_Solid> solids;
    for (TopExp_Explorer x(shape, TopAbs_SOLID); x.More(); x.Next()) solids.push_back(TopoDS::Solid(x.Current()));
    if (solids.empty()) { r.error = "no solid in the shape (sheet / wire bodies: not written)"; return false; }
    for (auto &so : solids) {
        GProp_GProps g; BRepGProp::VolumeProperties(so, g);
        if (!(g.Mass() > 0)) { r.error = "solid with non-positive volume (inside-out?)"; return false; }
    }

    /* --- edges kept: not degenerate, not a seam of its face (Parasolid needs no seam, ref 20-22) */
    std::map<const void *, std::vector<std::pair<TopoDS_Face, TopAbs_Orientation>>> uses;
    std::map<const void *, TopoDS_Edge> edge_of;
    for (TopExp_Explorer fx(shape, TopAbs_FACE); fx.More(); fx.Next()) {
        TopoDS_Face F = TopoDS::Face(fx.Current());
        for (TopExp_Explorer ex(F, TopAbs_EDGE); ex.More(); ex.Next()) {
            TopoDS_Edge E = TopoDS::Edge(ex.Current());
            r.max_occ_tolerance = std::max(r.max_occ_tolerance, BRep_Tool::Tolerance(E));
            if (BRep_Tool::Degenerated(E)) { r.degenerate_dropped++; continue; }
            if (BRep_Tool::IsClosed(E, F)) { r.seams_dropped++; continue; }
            uses[key(E)].push_back({F, E.Orientation()});
            edge_of[key(E)] = TopoDS::Edge(E.Oriented(TopAbs_FORWARD));
        }
    }
    r.seams_dropped /= 2;
    for (auto &kv : uses) {
        if (kv.second.size() != 2 || kv.second[0].second == kv.second[1].second) {
            std::string d;
            for (auto &u : kv.second) d += std::string(" ") + BRep_Tool::Surface(u.first)->DynamicType()->Name() + (u.second == TopAbs_FORWARD ? "+" : "-");
            double f, l; Handle(Geom_Curve) gc = BRep_Tool::Curve(edge_of[kv.first], f, l);
            r.error = "edge (" + std::string(gc.IsNull() ? "no curve" : gc->DynamicType()->Name()) + ") with " +
                      std::to_string(kv.second.size()) + " fins / equal senses (not a manifold solid edge, ref 25):" + d;
            return false;
        }
    }
    /* vertices: an edge's ends; a closed edge whose vertex has no other kept edge becomes a ring edge (no vertex) */
    std::map<const void *, std::set<const void *>> vedges;
    std::map<const void *, std::pair<const void *, const void *>> ends;
    std::map<const void *, TopoDS_Vertex> vert_of;
    for (auto &kv : edge_of) {
        TopoDS_Vertex a, b; TopExp::Vertices(kv.second, a, b);
        const void *ka = a.IsNull() ? nullptr : key(a), *kb = b.IsNull() ? nullptr : key(b);
        ends[kv.first] = {ka, kb};
        if (ka) { vedges[ka].insert(kv.first); vert_of[ka] = a; }
        if (kb) { vedges[kb].insert(kv.first); vert_of[kb] = b; }
        r.max_occ_tolerance = std::max({r.max_occ_tolerance, a.IsNull() ? 0.0 : BRep_Tool::Tolerance(a), b.IsNull() ? 0.0 : BRep_Tool::Tolerance(b)});
    }
    for (auto &kv : ends) {
        auto &e = kv.second;
        if (e.first == e.second && (!e.first || vedges[e.first].size() == 1)) {
            if (e.first) { vedges.erase(e.first); }
            e = {nullptr, nullptr};
            r.ring_edges++;
        }
    }

    /* --- fixed nodes: BODY first (root = index 1, ref 4), the attribute machinery (survey) */
    body = st.add("BODY");
    int list = 0, plb = 0;
    if (o.mc_attributes) {
        list = st.add("LIST"); plb = st.add("POINTER_LIS_BLOCK");
        attdef = st.add("ATTRIB_DEF"); int adid = st.add("ATT_DEF_ID");
        std::string nm = "CNC/mcEptrPtr";                       /* ATT_DEF_ID of all 108 solids */
        st.seta(adid, "string", std::vector<double>(nm.begin(), nm.end()));
        st.set(attdef, "next", 0);              /* ours: null (Mastercam's blobs hold an unresolved index here;
                                                   ref 99: ignored except in partition files) */
        st.set(attdef, "identifier", adid); st.set(attdef, "type_id", 9000);
        st.seta(attdef, "actions", {0, 0, 0, 0, 3, 6, 0, 0});
        st.set(attdef, "field_names", 0);
        st.seta(attdef, "legal_owners", {0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0});
        st.seta(attdef, "fields", {9});         /* SCH_pointer_field (ref 101) */
    }
    /* --- regions / shells (ref 19-20, 88-89): region 0 = the infinite void region */
    int vreg = st.add("REGION");
    st.set(vreg, "type", 'V');
    std::vector<int> regions{vreg}, cavity_regions;
    std::vector<int> vshells;                   /* shells of the void region */
    std::map<const void *, std::pair<int, int>> face_shells;   /* face TShape -> (back shell, front shell) */
    std::map<int, std::vector<TopoDS_Face>> back_faces, front_faces;
    std::vector<int> sregions;
    int first_solid_shell = 0;
    for (auto &so : solids) {
        int sreg = st.add("REGION");
        st.set(sreg, "type", 'S');
        sregions.push_back(sreg);
        TopoDS_Shell outer = BRepClass3d::OuterShell(so);
        std::vector<int> sh_of_reg;
        for (TopoDS_Iterator it(so); it.More(); it.Next()) {
            if (it.Value().ShapeType() != TopAbs_SHELL) continue;
            TopoDS_Shell sh = TopoDS::Shell(it.Value());
            int ssh = st.add("SHELL");
            st.set(ssh, "region", sreg); st.set(ssh, "body", body);
            if (!first_solid_shell) first_solid_shell = ssh;
            sh_of_reg.push_back(ssh);
            int fsh = st.add("SHELL");
            st.set(fsh, "body", 0);             /* void shells: body null (ref 89; survey) */
            if (sh.IsSame(outer)) { st.set(fsh, "region", vreg); vshells.push_back(fsh); }
            else {                               /* a cavity: its own void region (ours, by ref 19-20; not in the data) */
                int creg = st.add("REGION"); st.set(creg, "type", 'V'); st.set(fsh, "region", creg);
                st.set(creg, "shell", fsh); cavity_regions.push_back(creg);
                r.notes.push_back("solid with an internal void (cavity region written, unverified against Mastercam data)");
            }
            for (TopExp_Explorer fx(sh, TopAbs_FACE); fx.More(); fx.Next()) {
                TopoDS_Face F = TopoDS::Face(fx.Current());
                if (face_shells.count(key(F))) { r.error = "face used twice (non-manifold)"; return false; }
                face_shells[key(F)] = {ssh, fsh};
                back_faces[ssh].push_back(F); front_faces[fsh].push_back(F);
            }
        }
        for (size_t k = 0; k < sh_of_reg.size(); k++) st.set(sh_of_reg[k], "next", k + 1 < sh_of_reg.size() ? sh_of_reg[k + 1] : 0);
        st.set(sreg, "shell", sh_of_reg.empty() ? 0 : sh_of_reg[0]);
    }
    for (size_t k = 0; k < vshells.size(); k++) st.set(vshells[k], "next", k + 1 < vshells.size() ? vshells[k + 1] : 0);
    st.set(vreg, "shell", vshells.empty() ? 0 : vshells[0]);
    for (int x : sregions) regions.push_back(x);
    for (int x : cavity_regions) regions.push_back(x);
    for (size_t k = 0; k < regions.size(); k++) {
        st.set(regions[k], "body", body);
        st.set(regions[k], "next", k + 1 < regions.size() ? regions[k + 1] : 0);
        st.set(regions[k], "previous", k ? regions[k - 1] : 0);
    }

    /* --- vertices + points */
    for (auto &kv : vedges) {
        int v = st.add("VERTEX"), p = st.add("POINT");
        vnode[kv.first] = v; node_shape[v] = vert_of[kv.first];
        st.set(v, "point", p); st.set(v, "tolerance", NULL_DOUBLE); st.set(v, "owner", body);
        st.set(p, "owner", v); st.setv(p, "pvec", m(BRep_Tool::Pnt(vert_of[kv.first]).XYZ()));
        verts_n.push_back(v); points_n.push_back(p);
    }
    /* --- edges + curves */
    for (auto &kv : edge_of) {
        double f, l;
        Handle(Geom_Curve) gc = BRep_Tool::Curve(kv.second, f, l);
        if (gc.IsNull()) { r.unmapped.push_back("edge without a 3D curve"); continue; }
        xtref::Curve xc;
        int c = curve(gc, f, l, xc);
        if (!c) continue;
        int e = st.add("EDGE");
        enode[kv.first] = e; node_shape[e] = kv.second;
        st.set(e, "tolerance", NULL_DOUBLE); st.set(e, "curve", c); st.set(e, "owner", body);
        st.set(c, "owner", e);
        edges_n.push_back(e); curves_n.push_back(c);
        attribute(e);
        /* vertices on the XT curve (ours, self-check) */
        auto en = ends[kv.first];
        for (const void *vk : {en.first, en.second}) if (vk) {
            gp_XYZ q = m(BRep_Tool::Pnt(vert_of[vk]).XYZ());
            double best = 1e300;
            for (int k = 0; k <= 64; k++) {
                double t = f + (l - f) * k / 64;
                best = std::min(best, (xc.eval(xc.kind == xtref::Curve::LINE ? t * S : t) - q).Modulus());
            }
            if (best > 1e-9) r.notes.push_back("vertex " + std::to_string(best) + " m off its edge curve (ours, sampled)");
        }
    }
    if (!r.unmapped.empty()) { r.error = "geometry that cannot be written exactly"; return false; }

    /* --- faces, surfaces, loops, fins */
    std::map<const void *, std::vector<int>> fins_of_edge;
    std::map<const void *, std::vector<int>> fins_at_vertex;
    std::map<const void *, int> fnode;
    for (TopExp_Explorer fx(shape, TopAbs_FACE); fx.More(); fx.Next()) {
        TopoDS_Face F = TopoDS::Face(fx.Current());
        if (fnode.count(key(F))) continue;
        Handle(Geom_Surface) gs = BRep_Tool::Surface(F);
        xtref::Surface xs;
        int sn = surface(gs, F, xs);
        if (!sn) continue;
        int sense = face_sense(F, gs, xs);
        if (!sense) continue;
        int fn = st.add("FACE");
        fnode[key(F)] = fn; node_shape[fn] = F;
        st.set(fn, "tolerance", NULL_DOUBLE); st.set(fn, "surface", sn); st.set(fn, "sense", sense);
        st.set(fn, "next_on_surface", 0); st.set(fn, "previous_on_surface", 0);
        auto sh = face_shells[key(F)];
        st.set(fn, "shell", sh.first); st.set(fn, "front_shell", sh.second);
        st.set(sn, "owner", fn);
        faces_n.push_back(fn); surfs_n.push_back(sn);
        attribute(fn);
        if (o.face_color.IsBound(F) && o.mc_attributes) colour(fn, o.face_color(F));
        /* fins in wire order (BRepTools_WireExplorer: connected order, orientation composed with the face's) */
        std::vector<FinT> fins;
        for (TopExp_Explorer wx(F, TopAbs_WIRE); wx.More(); wx.Next()) {
            for (BRepTools_WireExplorer we(TopoDS::Wire(wx.Current()), F); we.More(); we.Next()) {
                TopoDS_Edge E = we.Current();
                const void *ek = key(E);
                if (!enode.count(ek)) continue;     /* seam / degenerate */
                bool fwd = E.Orientation() == TopAbs_FORWARD;
                auto en = ends[ek];
                fins.push_back({E, fwd, ek, fwd ? en.first : en.second, fwd ? en.second : en.first});
            }
        }
        /* loops: chain the fins nose to tail (ref 21); after removing seams a wire may fall into several loops */
        std::vector<bool> used(fins.size(), false);
        std::vector<std::vector<int>> loops;
        for (size_t a = 0; a < fins.size(); a++) {
            if (used[a]) continue;
            std::vector<int> L{(int)a}; used[a] = true;
            if (fins[a].vs == nullptr) { loops.push_back(L); continue; }   /* ring edge */
            size_t cur = a;
            int guard = 0;
            while (fins[cur].ve != fins[a].vs && ++guard < 1000000) {
                size_t nx = SIZE_MAX;
                for (size_t k = 1; k <= fins.size(); k++) {
                    size_t c = (cur + k) % fins.size();
                    if (!used[c] && fins[c].vs == fins[cur].ve) { nx = c; break; }
                }
                if (nx == SIZE_MAX) { r.error = "open loop in a face"; return false; }
                used[nx] = true; L.push_back((int)nx); cur = nx;
            }
            loops.push_back(L);
        }
        int prevloop = 0;
        for (auto &L : loops) {
            int ln = st.add("LOOP");
            st.set(ln, "face", fn); st.set(ln, "next", 0);
            if (prevloop) st.set(prevloop, "next", ln); else st.set(fn, "loop", ln);
            prevloop = ln;
            std::vector<int> hn;
            for (int k : L) {
                int h = st.add("HALFEDGE");
                hn.push_back(h);
                const FinT &t = fins[k];
                st.set(h, "loop", ln); st.set(h, "edge", enode[t.ek]); st.set(h, "curve", 0);
                st.set(h, "sense", t.fwd ? '+' : '-');
                st.set(h, "vertex", t.ve ? vnode[t.ve] : 0);          /* the fin's forward vertex (ref 21, 92) */
                fins_of_edge[t.ek].push_back(h);
                if (t.ve) fins_at_vertex[t.ve].push_back(h);
            }
            for (size_t k = 0; k < hn.size(); k++) {
                st.set(hn[k], "forward", hn[(k + 1) % hn.size()]);
                st.set(hn[k], "backward", hn[(k + hn.size() - 1) % hn.size()]);
            }
            st.set(ln, "halfedge", hn[0]);
        }
        if (!prevloop) st.set(fn, "loop", 0);
    }
    if (!r.unmapped.empty()) { r.error = "geometry that cannot be written exactly"; return false; }
    for (auto &kv : fins_of_edge) {
        auto &h = kv.second;
        int pos = st.n[h[0]].f["sense"][0] == '+' ? 0 : 1;
        st.set(h[0], "other", h[1]); st.set(h[1], "other", h[0]);
        st.set(enode[kv.first], "halfedge", h[pos]);                 /* EDGE.fin = the positive fin (ref 93; survey3) */
    }
    for (auto &kv : fins_at_vertex) {                                  /* VERTEX.fin + next_at_vx chain (ref 93) */
        auto &h = kv.second;
        st.set(vnode[kv.first], "halfedge", h[0]);
        for (size_t k = 0; k < h.size(); k++) st.set(h[k], "next_at_vx", k + 1 < h.size() ? h[k + 1] : 0);
    }
    /* --- face chains in shells: back faces next / previous, front faces next_front / previous_front (ref 90) */
    for (auto &kv : back_faces) {
        std::vector<int> v; for (auto &F : kv.second) v.push_back(fnode[key(F)]);
        st.set(kv.first, "face", v.empty() ? 0 : v[0]); st.set(kv.first, "front_face", 0);
        for (size_t k = 0; k < v.size(); k++) { st.set(v[k], "next", k + 1 < v.size() ? v[k + 1] : 0); st.set(v[k], "previous", k ? v[k - 1] : 0); }
    }
    for (auto &kv : front_faces) {
        std::vector<int> v; for (auto &F : kv.second) v.push_back(fnode[key(F)]);
        st.set(kv.first, "front_face", v.empty() ? 0 : v[0]); st.set(kv.first, "face", 0);
        for (size_t k = 0; k < v.size(); k++) { st.set(v[k], "next_front", k + 1 < v.size() ? v[k + 1] : 0); st.set(v[k], "previous_front", k ? v[k - 1] : 0); }
    }
    for (auto *sh : {&back_faces, &front_faces}) for (auto &kv : *sh) { st.set(kv.first, "edge", 0); st.set(kv.first, "vertex", 0); }
    /* --- body chains (ref 85-87) */
    auto chain = [&](const std::vector<int> &v) {
        for (size_t k = 0; k < v.size(); k++) { st.set(v[k], "next", k + 1 < v.size() ? v[k + 1] : 0); st.set(v[k], "previous", k ? v[k - 1] : 0); }
        return v.empty() ? 0 : v[0];
    };
    st.set(body, "edge", chain(edges_n)); st.set(body, "vertex", chain(verts_n));
    st.set(body, "boundary_point", chain(points_n)); st.set(body, "boundary_curve", chain(curves_n));
    st.set(body, "boundary_surface", chain(surfs_n));
    for (int a = 0, n = (int)attrs.size(); a < n; a++) {             /* attributes of one type, one chain (ref 104) */
        st.set(attrs[a], "next_of_type", a + 1 < n ? attrs[a + 1] : 0);
        st.set(attrs[a], "previous_of_type", a ? attrs[a - 1] : 0);
        st.set(attrs[a], "next", 0);
        if (!st.n[attrs[a]].f.count("previous")) st.set(attrs[a], "previous", 0);
    }
    for (int a = 0, n = (int)colattrs.size(); a < n; a++) {
        st.set(colattrs[a], "next_of_type", a + 1 < n ? colattrs[a + 1] : 0);
        st.set(colattrs[a], "previous_of_type", a ? colattrs[a - 1] : 0);
    }
    if (o.mc_attributes) {
        st.set(list, "node_id", 0); st.set(list, "list_type", 4); st.set(list, "notransmit", 1);
        st.set(list, "owner", body); st.set(list, "next", 0); st.set(list, "previous", 0);
        int nd = colattrs.empty() ? 1 : 2;
        st.set(list, "list_length", nd); st.set(list, "block_length", 20); st.set(list, "finger_index", 1);
        st.set(list, "finger_block", plb); st.set(list, "list_block", plb);
        std::vector<double> ent(20, 0); ent[0] = attrs.empty() ? 0 : attrs[0];
        if (nd == 2) ent[1] = colattrs[0];
        st.set(plb, "n_entries", nd); st.set(plb, "index_map_offset", 0); st.set(plb, "next_block", 0); st.seta(plb, "entries", ent);
    }
    st.set(body, "attribute_chains", list);
    st.set(body, "res_size", 1000.0000000000001);  /* exactly as in all 108 solids (survey) */
    st.set(body, "res_linear", 1e-8);
    st.set(body, "state", 1); st.set(body, "body_type", 1); st.set(body, "nom_geom_state", 1);   /* ref 85-86 */
    st.set(body, "shell", first_solid_shell); st.set(body, "region", vreg);
    st.set(body, "index_map_offset", 0); st.set(body, "lowest_node_id", 0);

    /* --- node ids (ref 23, 87: present and distinct; LIST 0) */
    int next_id = 1;
    for (TopTools_DataMapIteratorOfDataMapOfShapeInteger it(o.node_id); it.More(); it.Next()) next_id = std::max(next_id, it.Value() + 1);
    int hi = 0;
    for (size_t i = 1; i < st.n.size(); i++) {
        if (!st.has_field((int)i, "node_id") || st.n[i].f.count("node_id")) continue;
        int id = 0;
        auto ns = node_shape.find((int)i);
        if (ns != node_shape.end() && o.node_id.IsBound(ns->second)) id = o.node_id(ns->second);
        if (!id) id = next_id++;
        st.set((int)i, "node_id", id);
        hi = std::max(hi, id);
    }
    st.set(body, "highest_node_id", hi);
    if (!st.err.empty()) { r.error = st.err; return false; }
    return true;
}

std::string header(const Options &o) {          /* byte for byte as Mastercam's blobs (survey2.py: 108 identical) */
    std::string date = o.date;
    if (date.empty()) { time_t t = time(nullptr); char b[64]; strftime(b, sizeof b, "%a %b %d %H:%M:%S %Y", localtime(&t)); date = b; }
    std::string file = o.key;
    for (auto &c : file) c = (char)toupper((unsigned char)c);
    std::string h;
    h += "**ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz**************************\n";
    h += "**PARASOLID !\"#%&'()*+,-./:;<=>?@[\\]^_`{|}~0123456789***************************\n";
    h += "**PART1;\nMC=unknown;\nMC_MODEL=unknown;\nMC_ID=unknown;\nOS=Windows 95;\nOS_RELEASE=unknown;\n"
         "FRU=CNC Software, Inc.;\nAPPL=Mastercam;\nSITE=unknown;\nUSER=unknown;\nFORMAT=binary;\nGUISE=transmit;\n";
    h += "KEY=" + o.key + ";\nFILE=" + file + ".X_B;\nDATE=" + date + ";\n";
    h += "**PART2;\nSCH=SCH_2700142_26105;\nUSFLD_SIZE=1;\n**PART3;\n";
    h += "**END_OF_HEADER*****************************************************************\n";
    return h;
}

}  // namespace

bool write(const TopoDS_Shape &shape, const xtocc::Schema &s, const Options &o, std::vector<uint8_t> &out, Report &r) {
    Writer w(s, o, r);
    if (!w.run(shape)) return false;
    out.clear();
    std::string h = header(o);
    out.insert(out.end(), h.begin(), h.end());
    Out b{out};
    /* binary prefix: 'B', u32 len + modeller string, u32 len + schema key, u32 user field size (formats/xt.md: the
       Mastercam blobs use a 4-byte length for the modeller string too, where ref 16 says 2 bytes) */
    std::string mv = ": TRANSMIT FILE created by modeller version 2700142", sk = "SCH_2700142_26105";
    b.u8('B');
    b.i32((int)mv.size()); out.insert(out.end(), mv.begin(), mv.end());
    b.i32((int)sk.size()); out.insert(out.end(), sk.begin(), sk.end());
    b.i32(1);
    for (size_t i = 1; i < w.st.n.size(); i++) {
        const WNode &n = w.st.n[i];
        const xtocc::NodeDef &d = s.nd[n.type];
        r.counts[d.name]++;
        int last = -1;
        for (size_t k = 0; k < d.f.size(); k++) if (d.f[k].xmt) last = (int)k;
        uint32_t count = 0;
        if (d.var) {
            auto it = n.f.find(d.f[last].name);
            count = it == n.f.end() ? 0 : (uint32_t)(it->second.size() / (d.f[last].type == 'v' ? 3 : 1));
        }
        b.i16(n.type);
        if (d.var) b.i32(count);
        b.ptr((int)i);
        for (size_t k = 0; k < d.f.size(); k++) {
            const xtocc::FieldDef &fd = d.f[k];
            if (!fd.xmt) continue;
            auto it = n.f.find(fd.name);
            int elems = (d.var && (int)k == last) ? (int)count : (fd.n > 1 ? fd.n : 1);
            int per = fd.type == 'v' ? 3 : fd.type == 'b' ? 6 : fd.type == 'h' ? 3 : fd.type == 'i' ? 2 : 1;
            if (it == n.f.end() && fd.type != 'p') {
                r.error = "internal: " + d.name + "." + fd.name + " not set"; return false;
            }
            for (int e = 0; e < elems * per; e++) {
                double v = it == n.f.end() ? 0 : (e < (int)it->second.size() ? it->second[e] : 0);
                switch (fd.type) {
                case 'p': b.ptr((int)v); break;
                case 'd': case 't': b.i32((int64_t)v); break;
                case 'n': case 'w': b.i16((int)v); break;
                case 'c': case 'u': case 'l': b.u8((int)v); break;
                default: b.f64(v); break;
                }
            }
        }
        b.i32(0);                               /* user field (USFLD_SIZE=1): 0 on every node of the 108 solids */
    }
    b.i16(1);                                   /* terminator: node type 1, then index 0 (ref 4; 2 trailing bytes) */
    b.ptr(0);
    return true;
}

}  // namespace occxt
