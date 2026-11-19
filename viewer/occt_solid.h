/* occt_solid - the solids made here on OpenCASCADE (user decisions SOL-1..SOL-3, formats/occt_xt.md 9, formats/extrude.md 4): the Extrude
 * kernel (sld_extrude 0x1800d5730), Mastercam's face / edge names of each op (McEptr records, formats/solid_history.md 3), the body as
 * Parasolid XT (viewer/occt/occt_xt.cpp) and the display / pick queries of solid25.h's Body API.
 * Without HAVE_OCCT every call fails ("built without OpenCASCADE"). Units: the part's (mm or inch); XT is written in metres. */
#ifndef OCCT_SOLID_H
#define OCCT_SOLID_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct OcsBody OcsBody;        /* a solid body + the names of its faces / edges and its ops' record lists; reference counted */

/* one chain entity, exact (the chain's line / arc entities; formats/extrude.md 4.9: ps_arc_to_ps_arc's circle) */
typedef struct {
    int type;                          /* 1 line, 2 arc */
    double p0[3], p1[3];               /* line: the entity's start / end (its own direction) */
    double c[3], ax[3], xd[3], r;      /* arc: centre, circle axis, parameter-0 direction, radius (ps_arc_to_ps_arc: axis = view Z, negated
                                          for a negative sweep; xd = view X) */
    double t0, t1;                     /* arc: the entity's interval on that circle, t0 < t1 (t1 - t0 = 2 pi: a full circle) */
    int rev;                           /* traversed end -> start in the chain */
    int ent;                           /* the entity (F.ents index): ref1 of its names (written as its eptr) */
} OcsSeg;
typedef struct { const OcsSeg *s; int n; int closed; } OcsChain;
/* one chain group (sec. 4.5): ch[0] the root loop, then its holes (not thin); thin: the group's closed and open chains */
typedef struct { const OcsChain *ch; int nch; double dir[3]; } OcsGroup;   /* dir: the group's first chain's direction (4.1), unit */
typedef struct {                       /* SLD_EXTRUDE_PARAMS (sld_ops.h SldExtP) */
    int type, method, reverse, both, split, draft, draft_rev, thin, thin_dir, draft_ends;
    int single;                        /* "Create a single operation" (app+0x340): Cut / Boss of several groups in one boolean, else one op each */
    double dist, draft_ang, thick, thick2;   /* draft_ang in radians */
} OcsParams;

/* sld_extrude: Create -> one new body per group (*out, *nout = ng); Cut / Boss -> the target modified (*out[0], *nout = 1). op = the op id
   the names are made for (Create: 0; Cut / Boss: the target's op count). tol = system tolerance; metric = part units (Through all margin).
   0 on failure with *err_id / *err_cap = the mcgeomsldres message / caption (extrude.md 5; 0 = none decoded). */
int ocs_extrude(const OcsBody *target, const OcsGroup *g, int ng, const OcsParams *p, double tol, int metric, OcsBody ***out, int *nout,
                int *err_id, int *err_cap, char *err, size_t errn);
/* a Block (prim 15: pv = xmin xmax ymin ymax zmin zmax) / Cylinder (17: pv = r, len, c[3], dir[3]) of the Bounding Box panel; no names */
OcsBody *ocs_primitive(int prim, const double *pv, char *err, size_t errn);
/* the target search (FUN_1800d8d70 / FUN_1800da960, extrude.md 1.1 step 6) over bodies[0..nb-1]: the body every group's tool clashes with
   (or its reversed tool: flip[k] = 1), the smallest mean distance of its Z faces to the chain planes; -1 = none */
int ocs_target_search(OcsBody *const *bodies, int nb, const OcsGroup *g, int ng, const OcsParams *p, double tol, int metric, int *flip);

OcsBody *ocs_ref(OcsBody *b);
void ocs_unref(OcsBody *b);
double ocs_volume(const OcsBody *b);
int ocs_bbox(const OcsBody *b, double lo[3], double hi[3]);      /* the tight box (BRepBndLib::AddOptimal); 0 = empty */
/* triangles (3 vertices x (x y z nx ny nz) per triangle, outward), deflection in part units; returns the count, *out malloc'd */
int ocs_mesh(const OcsBody *b, double defl, float **out);
/* the B-rep edges as line segments (x y z x y z each) for display; returns the count */
int ocs_edges(const OcsBody *b, double defl, float **out);
int ocs_hit(const OcsBody *b, const double o[3], const double d[3], double *t);   /* the nearest face hit t >= 0 along o + t d */
int ocs_nfaces(const OcsBody *b);
int ocs_nedges(const OcsBody *b);
int ocs_valid(const OcsBody *b);                                  /* BRepCheck_Analyzer */

/* the body as Parasolid XT (formats/occt_xt.md): scale = part units per metre (1000 mm, 39.37007874015748 inch); pk = 27 (X9:
   SCH_2700142_26105) or 28 (2017: SCH_2800143_28002); key = "sld_N". Malloc'd; NULL with err (what the writer refused) */
uint8_t *ocs_xt(const OcsBody *b, double scale, int pk, const char *key, size_t *n, char *err, size_t errn);
/* the shape as OCCT BRep text (malloc'd), e.g. for tests */
uint8_t *ocs_brep(const OcsBody *b, size_t *n);
OcsBody *ocs_from_brep(const uint8_t *s, size_t n);
OcsBody *ocs_from_xt(const uint8_t *xb, size_t n, double scale, char *err, size_t errn);   /* XT -> OCCT (xt_occt), no names */

/* ---- names (formats/solid_history.md 3; McEptr records). Records are numbered per body (0 .. ocs_nrecs - 1); a ref is a chain entity,
   another record, or none. Each op of the body has its three lists (McOperation +0x44 / +0x60 / +0x80). */
enum { OCS_REF_NONE = 0, OCS_REF_ENT = 1, OCS_REF_REC = 2 };
typedef struct { uint32_t kind; int op; int r1t, r2t; int64_t r1, r2; } OcsRec;
int ocs_nrecs(const OcsBody *b);
const OcsRec *ocs_rec(const OcsBody *b, int i);
int ocs_nops(const OcsBody *b);
/* the op's list (0: n1 names made, 1: n2 tool names, 2: n3 names gone) as record numbers; returns the count, *idx points into the body */
int ocs_op_list(const OcsBody *b, int op, int list, const int **idx);
/* 1 when the op's names follow decoded rules; 0 = an op whose naming is NOT DECODED (Both directions, Draft, Thin wall, primitives):
   why says which (the save refuses such solids) */
int ocs_op_named(const OcsBody *b, int op, char *why, size_t wn);
/* the op's input group (ocs_extrude's g index; -1 = all groups in one op) and a caller's tag (cui_feat.c: the feature index) */
int ocs_op_group(const OcsBody *b, int op);
int ocs_op_tag(const OcsBody *b, int op);
void ocs_op_set_tag(OcsBody *b, int op, int tag);
/* the record on a face / edge of the body (index in TopExp::MapShapes order of faces / edges), -1 none */
int ocs_face_rec(const OcsBody *b, int face);
int ocs_edge_rec(const OcsBody *b, int edge);

/* ---- Constant Fillet (formats/solid_fillet.md; sld_fillet 0x1800e2540 -> ps_fillet 0x180046d30). Sub-shape indices are 0-based in
   TopExp::MapShapes order of the body's faces / edges. */
typedef struct {                       /* SLD_FILLET_PARAMS (solid_fillet.md 0) */
    double radius;                     /* P+0x08, part units */
    int overflow;                      /* P+0x10: 0 Default, 1 Maintain blend, 2 Maintain edges - stored only; OCCT's own overflow handling
                                          builds all three (user decision SOL-F2, ours) */
    int mitered;                       /* P+0x14: one edge at a time (ps_fillet's per-edge loop) */
    int propagate;                     /* P+0x15 (the command refuses 0 before the kernel: SOL-F1) */
} OcsFilP;
/* ps_fillet on OCCT: BRepFilletAPI_MakeFillet (ChFi3d_Rational, Add(radius, edge) per edge: OCCT builds each contour over the tangent
   edges), all edges in one build, or (mitered) one build per edge with the remaining edges carried through the history (FUN_18004b210).
   The result is a copy of b with one op appended: n1 = the new names (0x2e blend faces with ref1 = the record of the edge it replaced,
   0x17 edges with refs = their two faces, 0x33 / 0x2d split pieces), n3 = the records gone (FUN_18004a4b0, solid_fillet.md 5.3).
   ne == 0: success, an op with empty lists (FUN_180036310 with no edge left). 0 on failure (*out NULL, err = OCCT's reason). */
int ocs_fillet(const OcsBody *b, const int *edges, int ne, const OcsFilP *p, OcsBody **out, char *err, size_t errn);
/* PK_EDGE_is_smooth (FUN_180036310 drops such edges) stood in by BRepLib::ContinuityOfFaces(edge, f1, f2, 1e-10) >= G1 (user decision SOL-F5,
   ours: EncodeRegularity's default angle). An edge with fewer than two faces is not smooth. */
int ocs_edge_smooth(const OcsBody *b, int edge);
int ocs_face_edges(const OcsBody *b, int face, int **out);        /* PK_FACE_ask_edges (no seams / degenerated edges); malloc'd */
int ocs_edge_faces(const OcsBody *b, int edge, int f[2]);         /* ps_get_edge_faces: the count (0..2) */
/* Shift + click (sld_add_tangent_edge_sel -> ps_edge_find_tangents / sld_add_tangent_face_sel): the edges joined to `edge` at their vertices
   with tangent continuity / the faces joined to `face` over smooth edges, incl. the start; malloc'd. The tangency test is ours (angle 1e-6
   rad between the edge tangents at the shared vertex; ps_edge_find_tangents is NOT DECODED) */
int ocs_tangent_edges(const OcsBody *b, int edge, int **out);
int ocs_tangent_faces(const OcsBody *b, int face, int **out);
int ocs_sub_center(const OcsBody *b, int kind, int idx, double c[3]);   /* ps_get_bbox_center: kind 0 face, 1 edge */
int ocs_find_rec(const OcsBody *b, int kind, int rec);            /* the face (0) / edge (1) carrying record rec, -1 */
/* the picks of sld_pick_one_ent with mask 7 on one body: the nearest face hit along o + t d (*face, *t); *edge = an edge of the body within
   `ap` (part units, measured perpendicular to the ray) of the ray, the nearest along it; -1 when none. 1 when something was hit */
int ocs_pick(const OcsBody *b, const double o[3], const double d[3], double ap, int *face, int *edge, double *t);
/* line segments (x y z x y z) of an edge (kind 1) or of a face's boundary edges (kind 0) for the selection highlight; malloc'd */
int ocs_sub_lines(const OcsBody *b, int kind, int idx, double defl, float **out);

#ifdef __cplusplus
}
#endif
#endif
