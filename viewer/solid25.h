/* solid25 - solid bodies made by extruding wireframe chains along Z (Solids > Extrude).
 * A body is a stack of Z slabs, each a 2D region (outer boundaries with islands): exact for prismatic parts, so
 * Create Body / Cut Body / Add Boss are 2D booleans per slab. Draft is approximated by thin slabs.
 * The parameters and rules follow formats/extrude.md (MCGEOMSLD.DLL sld_extrude / SLD_EXTRUDE_PARAMS). The kernel
 * only extrudes along +Z / -Z (chains in XY planes, any Z): a general direction would need a real B-rep kernel. */
#ifndef SOLID25_H
#define SOLID25_H
#include "offset.h"
#include "btopo.h"

typedef struct { double z0, z1; CPaths reg; } Slab;          /* region: non-zero, outer CCW / holes CW */
/* a true arc the slabs' faceted outlines came from (formats/extrude.md "Round edges"): the source chain's arc entity
   (centre, radius, counter-clockwise start a0 / sweep sw > 0 in radians, XY plane, cw = drawn clockwise) and the tolerance within which its
   facets lie (tol: the chord tolerance the chain was flattened with + the offset term of a thin wall). The facets
   of the cap outlines are matched back to these in body_brep_edges. */
typedef struct { double cx, cy, r, a0, sw, tol; int cw; } BArc;   /* cw: the entity's sweep was negative */
typedef struct { Slab *s; int n; char name[64]; BArc *arc; int narc; } Body;

enum { EX_CREATE = 0, EX_CUT = 1, EX_BOSS = 2 };              /* P+0x00 (BAML OperationTypeCreateBody/Cut/Boss) */
enum { EXM_DISTANCE = 0, EXM_THROUGH_ALL = 1 };                /* P+0x10 (BAML DistanceMethodDistance/ThroughAll) */
enum { THIN_DIR1 = 0, THIN_DIR2 = 1, THIN_BOTH = 2 };          /* P+0x54 (BAML ThinWallDirection1/2/Both) */
/* SLD_EXTRUDE_PARAMS (formats/extrude.md sec. 0) */
typedef struct {
    int type;               /* P+0x00 EX_* */
    int target;             /* P+0x08 target body (here: index into the body list, -1 = none) */
    int method;             /* P+0x10 EXM_* */
    double dist;            /* P+0x18 Distance */
    double vec[3];          /* P+0x20 extrusion vector (copy of the first chain's direction) */
    int trim;               /* P+0x38 Trim to faces */
    int reverse;            /* P+0x39 reverse direction ("Reverse all" button) */
    int both;               /* P+0x3a Both directions */
    int split;              /* P+0x3b Split (draft) */
    int draft;              /* P+0x40 Draft */
    int draft_rev;          /* P+0x44 draft Reverse (native DraftOutward) */
    double draft_ang;       /* P+0x48 draft angle, degrees here (radians in P; the panel shows degrees) */
    int thin;               /* P+0x50 Thin wall */
    int thin_dir;           /* P+0x54 THIN_* */
    double thick, thick2;   /* P+0x58 Direction 1 / P+0x60 Direction 2 thickness */
    int draft_ends;         /* P+0x68 Draft ends */
    int legacy;             /* ours: read from an old project line, direction still to convert (mcamui feats_read) */
} ExtrudeParams;

/* one chain group (sec. 4.5: non-thin = one nesting group of a plane - a root loop plus its holes, islands are
   separate groups; thin = one plane, the wire grouping NOT DECODED): closed loops and open polylines at height z.
   dir = +1 / -1: the group's first chain's direction along Z (sec. 4.1). Open polylines keep the chain's travel. */
typedef struct { CPaths closed, open; double z; int dir; BArc *arc; int narc; int first; } Profile;   /* arc: the chains' arcs; first: index of the chain dir came from */

/* an error as Mastercam reports it: mcgeomsldres message id and caption id (formats/extrude.md sec. 5);
   id 0 = no message decoded for this failure; caption -1 = message_box_ok(id, -1, -1) (no caption) */
typedef struct { int id, caption; } ExErr;

/* sld_extrude: Create makes one new body per group (named `name`); Cut / Boss modify (*bodies)[ep->target].
   tol = system tolerance, metric = part units (Through all bbox margin). Returns 0 with *err set on failure. */
int solid_extrude(Body **bodies, int *nbodies, const Profile *groups, int ngroups, const ExtrudeParams *ep,
                  const char *name, double tol, int metric, ExErr *err);
/* the target search of a new op (FUN_1800d8d70 / FUN_1800da960, formats/extrude.md 1.1 step 6): the body the groups' tools hit;
   flip[k] = 1 when group k only hits it going the other way. Returns the body's index, or -1 (nothing found / changed). */
int solid_target_search(const Body *bodies, int nbodies, const Profile *g, int ng, const ExtrudeParams *ep, double tol, int metric, int *flip);
void body_free(Body *b);
Body body_copy(const Body *b);
/* triangles (x y z nx ny nz per vertex) for display; returns the triangle count, *out is malloc'd */
int body_mesh(const Body *b, float **out);
/* the body's edges as line segments (x y z x y z each) for display - sld_draw_solid_wire; returns the count */
int body_edges(const Body *b, float **out);
double body_volume(const Body *b);
/* ray o + t d against the body's faces: 1 and the nearest t >= 0 when hit (picking a body in the graphics) */
int body_hit(const Body *b, const double o[3], const double d[3], double *t);
/* the body's B-rep-like edges for the AutoCursor (core_ui cui_snap.c, formats/ui.md "AutoCursor on solids"): the
   outlines of the faces looking up / down at every slab height (the caps body_mesh draws) and the vertical edges at
   the region vertices whose wall normals differ by more than EDGE_DEG (the vertical edges body_edges draws), merged
   through vertices where exactly two segments meet in line (stacked slabs, collinear splits).
   Round edges (formats/extrude.md "Round edges"): a cap-outline facet lying on one of the body's BArc carriers (every
   point within the carrier's tol of the circle, inside its sweep, at most the faceting step long) belongs to that
   arc; a run of facets of one carrier between two vertices is an arc edge (arc = 1: the true circle in c / ax / xa /
   r with the interval t0 .. t1, t0 in [0, 2 pi) from xa about ax), a closed loop of one carrier a ring edge
   (arc = 2, no vertex, t0 = 0, t1 = 2 pi - Parasolid's ring edges have no vertex, formats/xt.md). A vertex where two
   carriers (or a carrier and a straight / unmatched segment) meet is a body vertex. A run through vertices where two
   horizontal segments of no carrier turn by less than EDGE_DEG is one edge with curved = 1 (geometry unknown: e.g.
   drafted slabs); closed loops of those are left out.
   pts = np points (x y z each, np >= 2; straight edges np = 2; a ring edge repeats its first point at the end),
   pts[0] = the start vertex, pts[np - 1] the end. */
typedef struct {
    double *pts; int np, curved;
    int arc;                         /* 0 straight / curved, 1 arc between two vertices, 2 ring edge (full circle) */
    double c[3], ax[3], xa[3], r, t0, t1;   /* the circle (arc != 0): centre, axis, parameter-0 direction, interval */
} BEdge;
int body_brep_edges(const Body *b, BEdge **out);
void bedges_free(BEdge *e, int n);
/* the body's faces / loops / fins over body_brep_edges' edges (btopo.h; rebuilt from the slabs, ours); returns the face count */
int body_brep_topo(const Body *b, BTopo *t);

#endif
