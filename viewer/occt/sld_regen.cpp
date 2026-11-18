/* sld_regen - replay a Mastercam solid's Extrude / Extrude Cut / Extrude Boss history in OpenCASCADE (ours; the
 * Extrude part of the "OCCT is the kernel" plan, formats/occt_xt.md "Proof", formats/solid_history.md 4).
 *
 *   sld_regen SCHEMA PART.mcx-9 SPEC OUTDIR [N]
 *     SPEC = `sld_ops.py spec PART SPEC` (each solid's ops: P fields, chains of entity eptrs + reversed flags, chain
 *     direction). Chain geometry comes from apprenticeCAM's own loader (viewer/mcam_load.o, read-only). For every
 *     solid (or only N): replay, compare with the stored XT (xt_occt), write the regenerated body as XT
 *     (occt_xt) to OUTDIR/sld_N.x_b. One TSV row per solid.
 *
 * Extrude semantics (formats/extrude.md 4.1, 4.2, 4.5, 4.7): direction = the chain's SLD_EXT_CHAIN_DATA direction
 * (the group's first chain), negated by P+0x39 Reverse; length = Distance, or for Cut + Through all the target's box
 * projected on the direction from every chain point, + 0.01 mm (metric); chains grouped by even-odd nesting (a loop
 * at even depth starts a group, its direct children are its holes); Create = one body per group, Cut = boolean
 * cut of the swept groups, Boss = fuse. Refused (not replayed): Both directions, Draft, Thin wall, Trim to faces
 * (their kernel conventions are partly NOT DECODED, extrude.md 4.3 / 4.6). OCCT booleans run exact (no fuzzy value):
 * the Parasolid boolean tolerance 1e-5 m (occt_3d_plan S7) is a user decision (Q5). */
#include "occt_xt.h"
#include "xt_occt.h"
extern "C" {
#include "mcam_load.h"
}
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <map>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Circ.hxx>

struct ChainSpec { double dir[3]; std::vector<std::pair<long long, int>> ents; };
struct OpSpec { int optype, ptype, method, reverse, both, draft, thin, trim; double distance; std::vector<ChainSpec> chains; };
struct SolidSpec { int N; std::vector<OpSpec> ops; int skipped = 0; };

static double vol(const TopoDS_Shape &s) { GProp_GProps g; BRepGProp::VolumeProperties(s, g, 1e-10); return g.Mass(); }
static double area(const TopoDS_Shape &s) { GProp_GProps g; BRepGProp::SurfaceProperties(s, g, 1e-10); return g.Mass(); }

/* point of a Mastercam wireframe entity at its arc angle t (formats/mcam.md "Arc views"; mcam_load.h Ent) */
static gp_Pnt arc_pt(const Ent &e, double t) {
    if (e.vw >= 2) return gp_Pnt(e.c.x + e.r * (cos(t) * e.vm[0] + sin(t) * e.vm[3]), e.c.y + e.r * (cos(t) * e.vm[1] + sin(t) * e.vm[4]),
                                 e.c.z + e.r * (cos(t) * e.vm[2] + sin(t) * e.vm[5]));
    return gp_Pnt(e.c.x + e.r * cos(t), e.c.y + e.r * sin(t), e.c.z);
}

static bool chain_wire(const McamFile &f, const ChainSpec &c, TopoDS_Wire &W, std::vector<gp_Pnt> &pts, std::string &err) {
    /* pass 1: each entity's exact edge + its traversal ends (flag 1 = traversed end -> start, solid_history.md 2.4) */
    std::vector<TopoDS_Edge> es; std::vector<int> rev; std::vector<gp_Pnt> ts, te;
    for (auto &er : c.ents) {
        const Ent *e = nullptr;
        for (int i = 0; i < f.nents; i++) if (f.ents[i].type && f.ents[i].eptr == er.first) { e = &f.ents[i]; break; }
        if (!e) { err = "chain entity eptr " + std::to_string(er.first) + " not in the part"; return false; }
        TopoDS_Edge E;
        if (e->type == E_LINE) {
            gp_Pnt a(e->a.x, e->a.y, e->a.z), b(e->b.x, e->b.y, e->b.z);
            E = BRepBuilderAPI_MakeEdge(a, b); pts.push_back(a); pts.push_back(b);
        } else if (e->type == E_ARC) {
            if (std::abs(std::abs(e->sw) - 2 * M_PI) < 1e-12) {
                gp_Pnt ce(e->c.x, e->c.y, e->c.z), p0 = arc_pt(*e, e->a0);
                gp_Dir n = e->vw >= 2 ? gp_Dir(e->vm[6], e->vm[7], e->vm[8]) : gp_Dir(0, 0, 1);
                if (e->sw < 0) n.Reverse();
                E = BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(ce, n, gp_Dir(gp_Vec(ce, p0))), e->r));
            } else {
                E = BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(arc_pt(*e, e->a0), arc_pt(*e, e->a0 + e->sw / 2), arc_pt(*e, e->a0 + e->sw)).Value());
            }
            for (int k = 0; k <= 32; k++) pts.push_back(arc_pt(*e, e->a0 + e->sw * k / 32));
        } else { err = "chain entity not a line / arc"; return false; }
        gp_Pnt s0 = BRep_Tool::Pnt(TopExp::FirstVertex(E, true)), s1 = BRep_Tool::Pnt(TopExp::LastVertex(E, true));
        es.push_back(E); rev.push_back(er.second); ts.push_back(er.second ? s1 : s0); te.push_back(er.second ? s0 : s1);
    }
    /* pass 2 (ours): Mastercam chains within its chaining tolerance (gaps up to 2.2e-4 mm in the user parts; how
       chain_to_pock_bound closes them is NOT DECODED): one shared vertex per joint at the gap's midpoint, tolerance =
       half the gap; the entities' curves are kept exactly */
    size_t n = es.size();
    std::vector<TopoDS_Vertex> J(n);
    BRep_Builder B;
    double maxgap = 0;
    for (size_t k = 0; k < n; k++) {
        gp_Pnt a = te[(k + n - 1) % n], b = ts[k];
        double g = a.Distance(b); maxgap = std::max(maxgap, g);
        B.MakeVertex(J[k], gp_Pnt((a.XYZ() + b.XYZ()) / 2), std::max(1e-7, g / 2 + 1e-7));
    }
    if (maxgap > 0.01) { char b[96]; snprintf(b, sizeof b, "chain gap %.3g mm > 0.01 (open chain, Mastercam sld 680)", maxgap); err = b; return false; }
    BRepBuilderAPI_MakeWire mw;
    for (size_t k = 0; k < n; k++) {
        double f0, l0; Handle(Geom_Curve) C = BRep_Tool::Curve(es[k], f0, l0);
        TopoDS_Vertex vs = J[k], ve = J[(k + 1) % n];                 /* traversal start / end joints */
        TopoDS_Vertex v0 = rev[k] ? ve : vs, v1 = rev[k] ? vs : ve;   /* at the curve's first / last parameter */
        TopoDS_Edge E2; B.MakeEdge(E2, C, 1e-7);
        B.Add(E2, v0.Oriented(TopAbs_FORWARD)); B.Add(E2, v1.Oriented(TopAbs_REVERSED)); B.Range(E2, f0, l0);
        mw.Add(rev[k] ? TopoDS::Edge(E2.Reversed()) : E2);
        if (!mw.IsDone()) { char b[96]; snprintf(b, sizeof b, "chain not connected (gap %.3g)", maxgap); err = b; return false; }
    }
    W = mw.Wire();
    if (!W.Closed() && !BRep_Tool::IsClosed(W)) { err = "open chain"; return false; }
    return true;
}

static bool replay_op(const McamFile &f, const OpSpec &op, TopoDS_Shape &body, std::string &err) {
    if (op.both || op.draft || op.thin || op.trim) { err = "Both / Draft / Thin wall / Trim to faces: not replayed"; return false; }
    struct L { TopoDS_Face face; std::vector<gp_Pnt> pts; double a; int parent = -1, depth = 0; gp_Dir dir; };
    std::vector<L> loops;
    for (auto &c : op.chains) {
        L l; TopoDS_Wire W;
        if (!chain_wire(f, c, W, l.pts, err)) return false;
        l.face = BRepBuilderAPI_MakeFace(W, true);
        if (l.face.IsNull()) { err = "chain not planar"; return false; }
        l.a = std::abs(area(l.face));
        l.dir = gp_Dir(c.dir[0], c.dir[1], c.dir[2]);
        loops.push_back(l);
    }
    /* nesting: parent = smallest loop containing this loop's first point (even-odd grouping, extrude.md 4.5) */
    for (size_t i = 0; i < loops.size(); i++) {
        double best = 1e300;
        for (size_t j = 0; j < loops.size(); j++) {
            if (i == j || loops[j].a <= loops[i].a) continue;
            BRepClass_FaceClassifier fc(loops[j].face, loops[i].pts[0], 1e-7);
            if (fc.State() == TopAbs_IN && loops[j].a < best) { best = loops[j].a; loops[i].parent = (int)j; }
        }
    }
    for (auto &l : loops) { int d = 0; for (int p = l.parent; p >= 0; p = loops[p].parent) d++; l.depth = d; }
    TopoDS_Shape tools;
    std::vector<TopoDS_Shape> prisms;
    for (size_t i = 0; i < loops.size(); i++) {
        if (loops[i].depth % 2) continue;                      /* holes go with their group */
        BRepBuilderAPI_MakeFace mf(loops[i].face);
        for (size_t j = 0; j < loops.size(); j++)
            if (loops[j].parent == (int)i && loops[j].depth % 2)
                for (TopExp_Explorer w(loops[j].face, TopAbs_WIRE); w.More(); w.Next()) mf.Add(TopoDS::Wire(w.Current().Reversed()));
        ShapeFix_Face sf(mf.Face()); sf.Perform();
        TopoDS_Face F = sf.Face();
        gp_Dir d = loops[i].dir;                             /* the group's first chain's direction (4.1) */
        if (op.reverse) d.Reverse();
        double len = op.distance;
        if (op.ptype == 1 && op.method == 1) {               /* Cut + Through all (4.2) */
            if (body.IsNull()) { err = "through all without a target"; return false; }
            Bnd_Box bb; BRepBndLib::AddOptimal(body, bb, false, false);
            double x0, y0, z0, x1, y1, z1; bb.Get(x0, y0, z0, x1, y1, z1);
            double mx = -1e300;
            for (int k = 0; k < 8; k++) {
                gp_Pnt c(k & 1 ? x1 : x0, k & 2 ? y1 : y0, k & 4 ? z1 : z0);
                mx = std::max(mx, gp_Vec(c.XYZ()).Dot(gp_Vec(d)));
            }
            len = -1e300;
            for (int j = 0; j < (int)loops.size(); j++) for (auto &p : loops[j].pts) len = std::max(len, mx - gp_Vec(p.XYZ()).Dot(gp_Vec(d)));
            len += 0.01;                                     /* bbox + 1000e-5 metric (4.2) */
            if (len <= 1e-6) len = 1.0;
        }
        prisms.push_back(BRepPrimAPI_MakePrism(F, gp_Vec(d) * len).Shape());
    }
    for (auto &p : prisms) {
        if (op.ptype == 0) body = body.IsNull() ? p : BRepAlgoAPI_Fuse(body, p).Shape();
        else if (op.ptype == 1) body = BRepAlgoAPI_Cut(body, p).Shape();
        else body = BRepAlgoAPI_Fuse(body, p).Shape();
    }
    /* no face merging (ShapeUpgrade_UnifySameDomain): Parasolid keeps one side face per chain entity (kind 4 records,
       solid_history.md 3.2); whether its booleans merge faces is NOT DECODED */
    return !body.IsNull();
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: sld_regen SCHEMA PART SPEC OUTDIR [N]\n"); return 2; }
    auto S = xtocc::load_schema(argv[1]);
    static McamFile F;
    if (!mcam_load(&F, argv[2])) { fprintf(stderr, "load: %s\n", mcam_error()); return 1; }
    int only = argc > 5 ? atoi(argv[5]) : 0;
    std::vector<SolidSpec> sp;
    std::ifstream in(argv[3]); std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line); std::string k; ls >> k;
        if (k == "solid") { sp.emplace_back(); ls >> sp.back().N; }
        else if (k == "skip") sp.back().skipped++;
        else if (k == "op") { OpSpec o; int nch; ls >> o.optype >> o.ptype >> o.method >> o.distance >> o.reverse >> o.both >> o.draft >> o.thin >> o.trim >> nch; sp.back().ops.push_back(o); }
        else if (k == "chain") { ChainSpec c; int n; ls >> c.dir[0] >> c.dir[1] >> c.dir[2] >> n; sp.back().ops.back().chains.push_back(c); }
        else if (k == "ent") { long long e; int r; ls >> e >> r; sp.back().ops.back().chains.back().ents.push_back({e, r}); }
    }
    printf("N\tops\tskipped_ops\tstatus\tvol_stored\tvol_regen\tdV_rel\tsymdiff_rel\tfaces_stored\tfaces_regen\tvalid\txt_bytes\tnote\n");
    for (auto &s : sp) {
        if (only && s.N != only) continue;
        std::string err;
        TopoDS_Shape body;
        bool ok = true;
        for (auto &op : s.ops) if (!replay_op(F, op, body, err)) { ok = false; break; }
        /* the stored body */
        const Solid *st = nullptr;
        char nm[32]; snprintf(nm, sizeof nm, "sld_%d", s.N);
        for (int i = 0; i < F.nsolids; i++) if (!strcmp(F.solids[i].name, nm)) st = &F.solids[i];
        xtocc::Model M; xtocc::Result R; std::string e2;
        bool rd = st && xtocc::parse(*S, st->xt, st->xt_size, M, e2) && xtocc::build(M, 1000, R);
        if (!ok || !rd) { printf("%d\t%zu\t%d\tFAIL\t\t\t\t\t\t\t\t\t%s %s\n", s.N, s.ops.size(), s.skipped, err.c_str(), e2.c_str()); continue; }
        double v0 = vol(R.shape), v1 = vol(body);
        BRepAlgoAPI_Cut c1(R.shape, body), c2(body, R.shape);
        double sd = (c1.HasErrors() || c2.HasErrors()) ? NAN : vol(c1.Shape()) + vol(c2.Shape());
        TopTools_IndexedMapOfShape f0, f1; TopExp::MapShapes(R.shape, TopAbs_FACE, f0); TopExp::MapShapes(body, TopAbs_FACE, f1);
        bool valid = BRepCheck_Analyzer(body).IsValid();
        occxt::Options o; o.scale = 1000; o.key = nm;
        std::vector<uint8_t> xb; occxt::Report rep;
        bool w = occxt::write(body, *S, o, xb, rep);
        if (w) std::ofstream(std::string(argv[4]) + "/" + nm + ".x_b", std::ios::binary).write((const char *)xb.data(), xb.size());
        bool good = valid && w && std::abs(v1 - v0) / v0 < 1e-9 && sd / v0 < 1e-9;
        printf("%d\t%zu\t%d\t%s\t%.9g\t%.9g\t%.2e\t%.2e\t%d\t%d\t%d\t%zu\t%s\n", s.N, s.ops.size(), s.skipped, good ? "SAME" : "DIFF",
               v0, v1, (v1 - v0) / v0, sd / v0, f0.Extent(), f1.Extent(), valid, xb.size(), w ? "" : rep.error.c_str());
    }
    mcam_free(&F);
    return 0;
}
