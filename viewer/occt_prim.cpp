/* occt_prim.cpp - Surfaces > Simple's solid built with OpenCASCADE, its faces as B-spline data (occt_prim.h). */
#include "occt_prim.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void free_members(OpFace *f, int n);
#ifdef HAVE_OCCT
#include <string>
#include <vector>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dConvert.hxx>
#include <GeomConvert.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax2.hxx>

static double *dup(const std::vector<double> &v) { double *p = (double *)malloc(sizeof(double) * (v.size() ? v.size() : 1)); if (v.size()) memcpy(p, v.data(), sizeof(double) * v.size()); return p; }
/* the full knot vector (multiplicities expanded) */
static std::vector<double> knots_of(const TColStd_Array1OfReal &k, const TColStd_Array1OfInteger &m) {
    std::vector<double> o; for (int i = k.Lower(); i <= k.Upper(); i++) for (int j = 0; j < m(i); j++) o.push_back(k(i)); return o;
}

bool occt_face_data(const TopoDS_Face &F, OpFace &o, std::string &err) {   /* also occt_mesh.cpp occt_xt_faces (From Solid) */
    memset(&o, 0, sizeof o);
    Handle(Geom_Surface) g = BRep_Tool::Surface(F);
    Handle(Geom_BSplineSurface) b = Handle(Geom_BSplineSurface)::DownCast(g);
    if (b.IsNull()) { b = GeomConvert::SurfaceToBSplineSurface(g); if (b.IsNull()) { err = "a face is not a B-spline surface"; return false; } }
    b = Handle(Geom_BSplineSurface)::DownCast(b->Copy());
    if (b->IsUPeriodic()) b->SetUNotPeriodic();
    if (b->IsVPeriodic()) b->SetVNotPeriodic();
    /* PK_FACE_output_surf_trimmed trims the surface to the face's box (ps_face_to_mc_surf's options): the face's uv bounds */
    double fu0, fu1, fv0, fv1; BRepTools::UVBounds(F, fu0, fu1, fv0, fv1);
    double su0, su1, sv0, sv1; b->Bounds(su0, su1, sv0, sv1);
    if (fu0 > su0 + 1e-9 || fu1 < su1 - 1e-9 || fv0 > sv0 + 1e-9 || fv1 < sv1 - 1e-9) b->Segment(fu0 > su0 ? fu0 : su0, fu1 < su1 ? fu1 : su1, fv0 > sv0 ? fv0 : sv0, fv1 < sv1 ? fv1 : sv1);
    b->Bounds(su0, su1, sv0, sv1);
    double du = su1 - su0, dv = sv1 - sv0;
    if (!(du > 0) || !(dv > 0)) { err = "a face with an empty parameter range"; return false; }
    o.udeg = b->UDegree(); o.vdeg = b->VDegree(); o.ncu = b->NbUPoles(); o.ncv = b->NbVPoles();
    o.rat = b->IsURational() || b->IsVRational();
    TColStd_Array1OfReal uk(1, b->NbUKnots()), vk(1, b->NbVKnots()); TColStd_Array1OfInteger um(1, b->NbUKnots()), vm(1, b->NbVKnots());
    b->UKnots(uk); b->UMultiplicities(um); b->VKnots(vk); b->VMultiplicities(vm);
    std::vector<double> KU = knots_of(uk, um), KV = knots_of(vk, vm);
    for (double &k : KU) k = (k - su0) / du;
    for (double &k : KV) k = (k - sv0) / dv;
    o.nku = (int)KU.size(); o.nkv = (int)KV.size(); o.ku = dup(KU); o.kv = dup(KV);
    std::vector<double> cp;
    for (int j = 1; j <= o.ncv; j++) for (int i = 1; i <= o.ncu; i++) {   /* u fastest */
        gp_Pnt p = b->Pole(i, j); double w = o.rat ? b->Weight(i, j) : 1.0;
        if (o.rat) { cp.push_back(w); cp.push_back(w * p.X()); cp.push_back(w * p.Y()); cp.push_back(w * p.Z()); }
        else { cp.push_back(p.X()); cp.push_back(p.Y()); cp.push_back(p.Z()); }
    }
    o.cp = dup(cp);
    o.reversed = F.Orientation() == TopAbs_REVERSED;
    /* the loops: the outer wire first, each wire's edges in order with their pcurves (oriented as the wire runs) */
    std::vector<TopoDS_Wire> ws; TopoDS_Wire ow = BRepTools::OuterWire(F); if (!ow.IsNull()) ws.push_back(ow);
    for (TopExp_Explorer wx(F, TopAbs_WIRE); wx.More(); wx.Next()) { TopoDS_Wire W = TopoDS::Wire(wx.Current()); if (!W.IsSame(ow)) ws.push_back(W); }
    std::vector<int> ln; std::vector<OpUv> cs;
    for (const TopoDS_Wire &W : ws) {
        int n = 0;
        for (BRepTools_WireExplorer ex(W, F); ex.More(); ex.Next()) {
            const TopoDS_Edge &E = ex.Current(); double a, c;
            Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(E, F, a, c);
            if (pc.IsNull()) continue;
            Handle(Geom2d_BSplineCurve) bc = Geom2dConvert::CurveToBSplineCurve(new Geom2d_TrimmedCurve(pc, a, c));
            if (bc.IsNull()) { err = "a trimming curve could not be converted"; return false; }
            if (bc->IsPeriodic()) bc->SetNotPeriodic();
            if (E.Orientation() == TopAbs_REVERSED) bc->Reverse();
            OpUv u; memset(&u, 0, sizeof u);
            u.deg = bc->Degree(); u.nc = bc->NbPoles(); u.rat = bc->IsRational();
            TColStd_Array1OfReal k(1, bc->NbKnots()); TColStd_Array1OfInteger m(1, bc->NbKnots()); bc->Knots(k); bc->Multiplicities(m);
            std::vector<double> K = knots_of(k, m); u.nk = (int)K.size(); u.k = dup(K);
            std::vector<double> P;
            for (int i = 1; i <= u.nc; i++) { gp_Pnt2d q = bc->Pole(i); P.push_back((q.X() - su0) / du); P.push_back((q.Y() - sv0) / dv); if (u.rat) P.push_back(bc->Weight(i)); }
            u.cp = dup(P); cs.push_back(u); n++;
        }
        if (n) ln.push_back(n);
    }
    o.nloops = (int)ln.size(); o.loop_ncrv = (int *)malloc(sizeof(int) * (ln.size() ? ln.size() : 1)); if (ln.size()) memcpy(o.loop_ncrv, ln.data(), sizeof(int) * ln.size());
    o.crv = (OpUv *)calloc(cs.size() ? cs.size() : 1, sizeof(OpUv)); if (cs.size()) memcpy(o.crv, cs.data(), sizeof(OpUv) * cs.size());
    return true;
}

int occt_prim_faces(int kind, const double org[3], const double axis[3], const double ref[3], const double d[6], OpFace **out, int *nout, char *err, size_t errn) {
    *out = NULL; *nout = 0;
    try {
        gp_Dir A(axis[0], axis[1], axis[2]), X(ref[0], ref[1], ref[2]);
        TopoDS_Shape S;
        if (kind == 0) {                                   /* PK_BODY_create_solid_block: centred in x / y on the location, z from 0 */
            gp_Dir Y = A.Crossed(X);
            gp_Pnt c(org[0] - 0.5 * d[0] * X.X() - 0.5 * d[1] * Y.X(), org[1] - 0.5 * d[0] * X.Y() - 0.5 * d[1] * Y.Y(), org[2] - 0.5 * d[0] * X.Z() - 0.5 * d[1] * Y.Z());
            S = BRepPrimAPI_MakeBox(gp_Ax2(c, A, X), d[0], d[1], d[2]).Shape();
        } else {                                           /* the solids of revolution: from the start angle about the axis (CRCYSO /
                                                              ps_create_sphere / ps_create_cone / ps_create_torus: full when start < tol and
                                                              end > 360 - tol, else their sweep builders) */
            double a0 = kind == 1 ? d[2] : kind == 2 ? d[1] : kind == 3 ? d[3] : d[2], a1 = kind == 1 ? d[3] : kind == 2 ? d[2] : kind == 3 ? d[4] : d[3];
            double sw = a1 - a0; while (sw <= 0) sw += 360.0;
            int full = sw >= 360.0 - 1e-9; double ang = sw * M_PI / 180.0;
            gp_Ax2 ax(gp_Pnt(org[0], org[1], org[2]), A, X);
            if (fabs(a0) > 1e-12) ax.Rotate(gp_Ax1(ax.Location(), A), a0 * M_PI / 180.0);
            if (kind == 1) S = full ? BRepPrimAPI_MakeCylinder(ax, d[0], d[1]).Shape() : BRepPrimAPI_MakeCylinder(ax, d[0], d[1], ang).Shape();
            else if (kind == 2) S = full ? BRepPrimAPI_MakeSphere(ax, d[0]).Shape() : BRepPrimAPI_MakeSphere(ax, d[0], ang).Shape();
            else if (kind == 3) {                          /* base radius, top radius, height (OCCT: equal radii = a cylinder) */
                if (fabs(d[0] - d[1]) < 1e-12) S = full ? BRepPrimAPI_MakeCylinder(ax, d[0], d[2]).Shape() : BRepPrimAPI_MakeCylinder(ax, d[0], d[2], ang).Shape();
                else S = full ? BRepPrimAPI_MakeCone(ax, d[0], d[1], d[2]).Shape() : BRepPrimAPI_MakeCone(ax, d[0], d[1], d[2], ang).Shape();
            } else S = full ? BRepPrimAPI_MakeTorus(ax, d[0], d[1]).Shape() : BRepPrimAPI_MakeTorus(ax, d[0], d[1], ang).Shape();
        }
        BRepBuilderAPI_NurbsConvert nc(S, Standard_True);
        TopoDS_Shape N = nc.Shape();
        std::vector<OpFace> fs; std::string e;
        for (TopExp_Explorer fx(N, TopAbs_FACE); fx.More(); fx.Next()) {
            OpFace o; if (!occt_face_data(TopoDS::Face(fx.Current()), o, e)) { free_members(fs.data(), (int)fs.size()); snprintf(err, errn, "%s", e.c_str()); return -1; }
            fs.push_back(o);
        }
        *out = (OpFace *)calloc(fs.size() ? fs.size() : 1, sizeof(OpFace)); if (fs.size()) memcpy(*out, fs.data(), sizeof(OpFace) * fs.size());
        *nout = (int)fs.size();
        return 0;
    } catch (Standard_Failure &x) {
        snprintf(err, errn, "OpenCASCADE: %s", x.GetMessageString() ? x.GetMessageString() : "failure"); return -1;
    }
}
#else
int occt_prim_faces(int kind, const double org[3], const double axis[3], const double ref[3], const double d[6], OpFace **out, int *nout, char *err, size_t errn) {
    (void)kind; (void)org; (void)axis; (void)ref; (void)d; *out = NULL; *nout = 0; snprintf(err, errn, "built without OpenCASCADE"); return -1;
}
#endif
static void free_members(OpFace *f, int n) {
    for (int i = 0; i < n; i++) {
        free(f[i].ku); free(f[i].kv); free(f[i].cp); int nc = 0;
        for (int l = 0; l < f[i].nloops; l++) nc += f[i].loop_ncrv[l];
        for (int c = 0; c < nc; c++) { free(f[i].crv[c].k); free(f[i].crv[c].cp); }
        free(f[i].loop_ncrv); free(f[i].crv);
    }
}
void occt_prim_free_one(OpFace *f) { free_members(f, 1); }
void occt_prim_free(OpFace *f, int n) { if (f) { free_members(f, n); free(f); } }
