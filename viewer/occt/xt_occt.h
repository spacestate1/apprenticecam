/* xt_occt - Parasolid binary transmit (XT, as embedded in Mastercam solids) -> OpenCASCADE B-rep.
 *
 * Sources (no improvisation):
 *   - file layout, node encoding: formats/xt.md, tools/xt_parse.py, viewer/xt_mesh.c parse() (schema-driven reader,
 *     schema = Parasolid's own sch_<key>.s_t shipped with Mastercam, ~/mastercam-re/files/sch_26105.s_t);
 *   - node fields: sch_26105.s_t (BODY, REGION, SHELL, FACE, LOOP, HALFEDGE, EDGE, VERTEX, POINT, LINE, CIRCLE,
 *     PLANE, CYLINDER);
 *   - fin conventions: formats/xt.md "Topology used for meshing" + "Edges" (a fin's vertex is at its forward end; an
 *     edge runs along its fin EDGE.halfedge from the partner fin's vertex to the fin's own; HALFEDGE.sense '-' = the
 *     fin runs against its curve), viewer/btopo.h (loops run with the face on their left seen along the face's outward
 *     normal), viewer/xt_mesh.c sample_edge (CIRCLE.sense '-' = the circle runs about -normal);
 *   - FACE.shell / FACE.front_shell: the shell behind / in front of the face (data: every solid's faces have
 *     shell = the solid region's shell, front_shell = the void region's shell; formats/occt_accuracy.md).
 * Ours (labelled): the OCCT representation choices - pcurves built analytically, missing seams on periodic faces
 * added by ShapeFix_Face::FixMissingSeam (Parasolid needs no seam edge, OCCT does), tolerances = BODY.res_linear.
 *
 * Mapped exactly: LINE, CIRCLE, PLANE, CYLINDER (every entity in the user's parts, formats/occt_accuracy.md).
 * 3drough (2026-10-24, formats/hst3d_port_plan.md 6.4): also ELLIPSE, B_CURVE (NURBS_CURVE), SP_CURVE (a 2D NURBS on a surface),
 * TRIMMED_CURVE and B_SURFACE (NURBS_SURF) - node fields from the schema (sch_26105.s_t); the vertex order of NURBS_SURF and the
 * rational vertex convention are chosen by the accuracy metrics (vertex / curve-surface gaps), not documented here: ours. Pcurves on
 * the new surfaces: the SP_CURVE's own, else GeomProjLib::Curve2d (ours). The planes / cylinders / lines / circles keep their
 * exact analytic path (unchanged output for the user's parts).
 * Everything else (ELLIPSE, B-curves, intersection / SP / trimmed curves, CONE, SPHERE, TORUS, B-surfaces, offset,
 * swept, spun, blend surfaces): NOT MAPPED - the body is refused with the type named in Result::unmapped (no sample
 * exists anywhere to verify their parametrisation / sense conventions).
 * Since xtfix (2026-10-26) / xtfix2 (2026-10-27): CONE, SPHERE, TORUS (lemons too), OFFSET_SURF, SWEPT_SURF, SPUN_SURF, rolling-ball
 * BLENDED_EDGE, INTERSECTION, periodic NURBS, sheet bodies, tolerant edges' own fin curves - formats/xt_occt_fixes.md. */
#ifndef XT_OCCT_H
#define XT_OCCT_H
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_XYZ.hxx>

namespace xtocc {

struct FieldDef { std::string name; char type; int xmt, n, off; };
struct NodeDef { bool defined = false; int var = 0; std::string name; std::vector<FieldDef> f; int size = 0; };
struct Schema { std::vector<NodeDef> nd; };
std::shared_ptr<Schema> load_schema(const std::string &path);   /* nullptr on failure */

struct Node { int type = 0; const uint8_t *p = nullptr; uint32_t count = 0; };
struct Model {                                     /* a parsed XT blob; the blob must outlive the model */
    const Schema *s = nullptr;
    std::vector<Node> by_idx;                      /* index = XT node index */
    const Node *node(int idx) const;               /* NULL for null pointers (1) / missing nodes */
    const char *tname(const Node *n) const;
    bool is(const Node *n, const char *t) const;
    int ptr(const Node *n, const char *f, int k = 0) const;   /* node index, 1 = null; k = array element (xtfix: BLENDED_EDGE.surface[2]) */
    const Node *pnode(const Node *n, const char *f) const { return node(ptr(n, f)); }
    double dbl(const Node *n, const char *f, int k = 0) const;
    gp_XYZ vec(const Node *n, const char *f) const;
    int chr(const Node *n, const char *f) const;
    int ival(const Node *n, const char *f) const;  /* int32 field ('d'), 0 if absent (added for occt_xt_check) */
    int sval(const Node *n, const char *f, int k = 0) const;   /* int16 field ('n'), element k (3drough: NURBS degrees / knot multiplicities) */
    bool has(const Node *n, const char *f) const;
};
bool parse(const Schema &s, const uint8_t *b, size_t len, Model &m, std::string &err);

struct Result {
    TopoDS_Shape shape;                            /* a compound of solids (one per solid region) */
    std::map<int, TopoDS_Face> face_of;            /* XT FACE node index -> OCCT face (final, oriented as in the shell) */
    std::map<int, TopoDS_Edge> edge_of;            /* XT EDGE node index -> OCCT edge (final) */
    std::map<int, TopoDS_Shape> vertex_of;         /* XT VERTEX node index -> OCCT vertex (added for occt_xt_check) */
    std::map<std::string, int> counts;             /* XT node type -> count */
    std::vector<std::string> unmapped;             /* entity types met that are not mapped (body refused if non-empty) */
    std::vector<std::string> notes;                /* representation changes (seams added, ...) and anomalies */
    int seams_added = 0;
    double max_vertex_gap = 0;                     /* largest distance XT vertex -> its XT curve (model units) */
    double max_curve_surface_gap = 0;              /* largest distance XT edge curve -> XT face surface (sampled) */
    double tol = 0;                                /* tolerance used (BODY.res_linear x scale) */
    std::string error;
    /* xtfix (2026-10-26, formats/xt_occt_fixes.md): the accuracy checks of the CONE / SPHERE / TORUS / OFFSET_SURF / BLENDED_EDGE
       surfaces and INTERSECTION curves - check name -> samples, largest deviation (model units). Filled always for the cheap checks
       (construction residuals); with `check` = true (input, set before build) also the edge-on-surface / formula samples. */
    struct Dev { long n = 0; double max = 0; };
    std::map<std::string, Dev> accuracy;
    bool check = false;
    bool new_geometry = false;                     /* xtfix: the body has CONE / SPHERE / TORUS / OFFSET_SURF / BLENDED_EDGE / INTERSECTION geometry */
    std::vector<std::string> refused;              /* xtfix: faces not built (face index: reason); the body is refused if non-empty */
};
/* scale: model units per metre (1000 = mm). Returns false (and Result::error) if the body cannot be built. */
bool build(const Model &m, double scale, Result &out);

}  // namespace xtocc
#endif
