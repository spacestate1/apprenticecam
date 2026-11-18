/* ifk_planecut.h - PlaneCut (interfacial03.dll), stage A: cut-plane set, input, Run FUN_180025910 per plane up to
 * and including the split arrangement (FUN_180026140 cut, FUN_180027060 / FUN_180027500 pair intersections,
 * FUN_180028c20 splits with the FUN_18001d8d0 piece flags). Citations in ifk_planecut.c; write-up in
 * formats/interfacial.md 11.7. Stage B (FUN_180029630 onwards) is not here.
 *
 * Records are plain arrays indexed by int. Every list the DLL keeps as a push-front linked list is stored here in
 * push order (index 0 = first pushed); the DLL walks such a list from its head = our LAST element. */
#ifndef IFK_PLANECUT_H
#define IFK_PLANECUT_H

#include <stdint.h>

#define PC_SI_L 8          /* max limbs of a fixed-size SuperInteger<n> (n <= 5 used) */
#define PC_SD_L 80         /* max limbs of a PlaneCutSuperDouble value */

typedef struct { unsigned char pos; uint32_t l[PC_SI_L]; } PcSI;            /* sign byte 1 = positive, LSB first */
typedef struct { unsigned char pos; int n, e; uint32_t l[PC_SD_L]; } PcSD; /* value = sum l[i] 2^(32 (e + i)) */

typedef struct { int n, cap; int *v; } PcList;

typedef struct { double x[3]; PcSI s[3]; long key; } PcInNode;          /* InputNode (pool impl+0x108) */
typedef struct { int n[3]; int e[3]; int id; int flag; } PcInTri;         /* InputTriangle (+0x20.. nodes, +8.. edges) */
typedef struct { int a, b; } PcInEdge;                                    /* InputEdge: a = lower node address */
typedef struct { double off; PcSI soff; PcList tris; } PcPlane;           /* CutPlane */

typedef struct {                       /* CutNode (pool impl+0x140) */
    int kind;                          /* 0 = input node, 1 = input edge, 2 = intersection of two triangles */
    int ref, ref2;                     /* kind 0: node, 1: edge, 2: triangle A / triangle B */
    int pt;                            /* +0x18: its CutNodeCoordinate */
} PcCutNode;

typedef struct { PcSD U, V, D, W; int hasW; } PcExact;                    /* exact record (pool impl+0x1a0) */

typedef struct {                       /* CutNodeCoordinate (pool impl+0x150): a point of the plane */
    double c[6];                       /* u, eu, v, ev, w, ew (filtered value / error) */
    double s, es;                      /* +0x30: u + v and its error (point-tree key) */
    PcList nodes;                      /* +0x40: cut nodes at this point (push order) */
    int ex;                            /* +0x50: exact record index or -1 */
    int stamp;                         /* +0x58 */
    long key;                          /* emulated record address (pool order, see ifk_planecut.c) */
    int alive;
    PcList gel;                        /* +0x48 (stage B): graph edges at the point (FUN_180029630), push order;
                                        * on a point copy (FUN_18002a550) the curve edges of FUN_1800151f0 */
    int cnode;                         /* +0x60 (stage B): its CutCurveNode (FUN_1800131d0) or -1 */
    int tl, tr, tprio;                 /* point tree (ours) */
} PcPoint;

typedef struct {                       /* CutEdge (pool impl+0x148) */
    int n0, n1, tri;                   /* start / end cut node, triangle */
    double box[4];                     /* +0x18: umin, vmin, umax, vmax */
    int flag48;                        /* +0x48 */
    int stamp4c;                       /* +0x4c */
    PcList split;                      /* +0x40: intersection / touching nodes (push order) */
} PcCutEdge;

typedef struct {                       /* graph edge (pool impl+0x158), FUN_180029150 */
    int p0, p1;                        /* points, p0 = the lower record address */
    unsigned char f10, f11, f12, f13, f14, f15, f16;   /* f12: chain "visited" (FUN_180029630) */
    long data;                         /* [3]: triangle data pointer (here: always 0, Silhouette passes NULL) */
} PcGEdge;

/* ---- stage B records (formats/interfacial.md 11.8) ---- */
typedef struct {                       /* CutCurveNode (pool impl+0x170, FUN_1800131d0) */
    double x[3];                       /* +8: [ua] = point u, [va] = v, [axis] = w (x, y, z for a z cut) */
    int onground;                      /* +0x30: FUN_180012f50 == 0 */
    int pt;                            /* +0x38 record [0]: copy of the point (FUN_18002a550), -1 = none */
    int ref;                           /* +0x20: number of curve edges using it */
    int cache;                         /* +0x28: Polygon2D node (FUN_180015b30), then the output point (slot 3) */
} PcCNode;
typedef struct { int n0, n1; unsigned char both, vis; long data; } PcCEdge;   /* CutCurveEdge (pool +0x178):
                                        * [0] / [1] curve nodes, +0x10 = both directions, +0x11 visited (1 / 2) */
typedef struct { int p[2]; PcList edges; int plane; } PcCurve;   /* CutCurve (pool +0x180): [0] / [1] points
                                        * of one of its edges, +0x10 curve edges (push order), [3] plane */
typedef struct {                       /* CutTestPoint (pool +0x188, FUN_180014710) */
    double c[6];                       /* +8: x, ex, y, ey, z, ez (index = axis), live: slot 0 overwrites them */
    int a, b, curve;                   /* +0x38 / +0x40 the curve's two points, +0x48 the curve */
    int hasex; PcSD X[4];              /* +0x50: exact homogeneous X, Y, Z, W (FUN_1800148b0) */
    int passed;                        /* +0x58: PlaneCutBase::TestPointPassed 180021120 */
} PcTestPoint;
typedef struct { int cn; int deg; PcList edges; } PcPNode;              /* Polygon2DNode: [0] curve node,
                                        * +8 degree, +0x10 its edges (push order) */
typedef struct { int from, to; int stamp; } PcPEdge;                    /* Polygon2DEdge [0] -> [1], +0x10 stamp */
typedef struct {                       /* CutPolygon (pool tree+0x40) with its Polygon2D (FUN_18003e810) */
    PcList pedges;                     /* Polygon2D [0]: its edges (push order), [1] = count */
    double box[4];                     /* Polygon2D [3..6]: min x, min y, max x, max y */
    int byte38;                        /* Polygon2D +0x38 */
    PcList cedges;                     /* CutPolygon [1]: the curve edges walked (push order) */
    int parent;                        /* [2] */
    PcList children;                   /* [3] (push order) */
    int tnode;                         /* [4]: output tree node (slot 1) or -1 */
    int rev;                           /* +0x28 byte: FUN_18003cec0 result (edges reversed) */
    int ref;                           /* [6]: reference curve node for the nesting test */
    int open;                          /* [7]: on the leaf list (tree [6]) */
} PcPoly;

/* Output of the IO slots 0-5 (PlaneCutToSilhouette FUN_180024ab0 / af0 / b50 / bc0 / c40) as a plain tree. */
typedef struct {
    int nnode; int *parent;            /* tree nodes in slot-1 order; parent -1 = the root (+0x18) */
    int *nchild; int **child;          /* children in the DLL's list order (push-front: newest first) */
    int npt; double *pt;               /* slot 3 points in call order, 3 per point (Silhouette keeps x, y) */
    int nedge; int *edge;              /* slot 4 edges in call order, 4 ints: a, b, flag, tree node */
    int nroot; int *root;              /* the root's children (slot 5), in its list order (oldest first) */
} IfkPcTree;

typedef struct IfkPlaneCut IfkPlaneCut;
typedef struct {                       /* IO slots 7-10 (Run FUN_180025910 "Cleaning up...") */
    void (*begin)(void *user, IfkPlaneCut *pc);            /* slot 7 (+0x38) */
    void (*add)(void *user, IfkPlaneCut *pc, int tp);      /* slot 8 (+0x40), once per test point */
    void (*build)(void *user, IfkPlaneCut *pc);            /* slot 9 (+0x48) */
    void (*classify)(void *user, IfkPlaneCut *pc);         /* slot 10 (+0x50): calls ifk_pc_tp_passed */
    void *user;
} IfkPcIO;

struct IfkPlaneCut {
    double grid; int k;                /* DAT_18008f650, DAT_18008f668 */
    /* cut-plane set */
    double n[3]; PcSI sn[3]; int axis, ua, va, aligned, flipped, vertical;
    int nplane; PcPlane *plane;        /* sorted by offset (increasing) */
    double ground;                     /* impl+0x58 */
    /* input */
    int nin, cin; PcInNode *in;
    int ntri, ctri; PcInTri *tri;
    int nedge, cedge; PcInEdge *edge;
    /* per plane (cleared by ifk_pc_cut_plane) */
    int nnode, cnode; PcCutNode *node;
    int npt, cpt; PcPoint *pt;
    int nex, cex; PcExact *ex;
    int nce, cce; PcCutEdge *ce;
    PcList celist;                     /* impl+0x90 (push order) */
    PcList ptlist;                     /* impl+0xa0 (push order) */
    int nge, cge; PcGEdge *ge;         /* impl+0xa8 (push order = creation order) */
    int ptroot;
    void *nodemap, *gemap;             /* lookups (ours) */
    /* emulated pools (record address order) */
    PcList ptfree; int ptblocks; long innext;
    int stamp470, stamp518;            /* DAT_18008f470, DAT_18008f518 */
    int axis488;                       /* DAT_18008f488 */
    int overflow;                      /* DAT_18008f680 */
    int nerr; char err[8][96];         /* dialog slot +0x40 messages, first 8 */
    long npairs, nexact;               /* diagnostics (ours) */
    /* stage B */
    int mode;                          /* impl+0xfc */
    int ncn, ccn; PcCNode *cn;
    int nce2, cce2; PcCEdge *cedg;   /* CutCurveEdges */
    int ncv, ccv; PcCurve *cv;
    PcList curves;                     /* impl+0xb0 (append order) */
    int ntp, ctp; PcTestPoint *tp;
    int npn, cpn; PcPNode *pn;
    int npe, cpe; PcPEdge *pe;
    int npoly, cpoly; PcPoly *poly;
    int stamp5c0, stamp5f0;            /* DAT_18008f5c0, DAT_18008f5f0 (Polygon2D inside test) */
    int pg28, pg29, pg30;              /* Polygon2DMemory +0x28 / +0x29 / +0x30 (inside-test state) */
    long nangle;                       /* diagnostics: points angle-sorted (FUN_180029f90) */
};

IfkPlaneCut *ifk_pc_new(double grid);                                  /* PlaneCut ctor 180021130 */
void ifk_pc_free(IfkPlaneCut *pc);
int ifk_pc_new_cut_plane_set(IfkPlaneCut *pc, double n[3]);             /* FUN_180025350 (n is updated) */
double ifk_pc_add_cut_plane(IfkPlaneCut *pc, double d);                /* FUN_1800255e0 */
void ifk_pc_end_cut_plane_set(IfkPlaneCut *pc);                        /* 180021020 */
int ifk_pc_add_input_node(IfkPlaneCut *pc, const double xyz[3]);       /* AddInputNode 180021b20 (+ FUN_180008dc0) */
int ifk_pc_add_input_triangle(IfkPlaneCut *pc, int a, int b, int c, int id, int flag); /* 180021be0 */
void ifk_pc_end_input(IfkPlaneCut *pc, int edges);                     /* EndInput 180021d80 */

/* Run FUN_180025910 stage A: preprocessing (triangles to planes), then per plane in the DLL's order (decreasing
 * offset) the cut, the pair intersections and the split; after each plane cb(pc, plane index, user) is called with
 * pc->pt / pc->ge holding that plane's split arrangement. Returns 0, or -1 for a set this port does not handle
 * (not axis-aligned: FUN_180012180 / FUN_18000dfb0 / the non-aligned FUN_18001bde0 branches are not ported). */
typedef void (*IfkPcPlaneCb)(IfkPlaneCut *pc, int plane, void *user);
int ifk_pc_run_stage_a(IfkPlaneCut *pc, IfkPcPlaneCb cb, void *user);

/* Run FUN_180025910 with stage B (mode = Run's third argument; Silhouette passes 1): per plane stage A then
 * FUN_180029630 (prune, angle sort, chains), then "Cleaning up...": one test point per curve (FUN_180014710),
 * IO slots 7-10, and for mode 1 the passed curves (FUN_1800151f0) assembled into Polygon2D loops, nested
 * (FUN_180015d20) and written through slots 0-5 into *out. Returns 0, -1 when not ported (non-aligned set, more
 * than one cut plane, mode != 1). */
int ifk_pc_run(IfkPlaneCut *pc, int mode, const IfkPcIO *io, IfkPcTree *out);
void ifk_pc_tree_free(IfkPcTree *t);
/* for the IO: the live doubles of a test point (x, ex, y, ey, z, ez), its exact homogeneous coordinates (CutTestPoint
 * vtable slot 0 FUN_1800148b0: computed on the first call, overwrites the doubles), TestPointPassed 180021120 */
double *ifk_pc_tp_coords(IfkPlaneCut *pc, int tp);
void ifk_pc_tp_exact(IfkPlaneCut *pc, int tp, PcSD *X, PcSD *Y, PcSD *W);
void ifk_pc_tp_passed(IfkPlaneCut *pc, int tp);

#endif
