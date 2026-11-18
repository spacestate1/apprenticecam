/* cam3d_pencil.c - HST Pencil (op 132, UI style 6, kernel style 5): McHmm's FullPencilNet ("Machining\FullPencilNet.cpp").
 * Included by cam2d.c after cam3d_finish.c (needs C3Mesh / c3_mesh_op, hst_grid_open, MB / mv). API: cam3d_pencil.h.
 * Decode: formats/pencil.md (sections 6 - 8 here); McHmm decompiles in decomp/pencil/: pnet.c ("pn", FullPencilNet / PencilQuad
 * 18014f000-18016a000), pplan.c (CPencilPassesPlan / EvaPencilPassesPlan), d2.c, g/step.c ("st": FUN_180253d20 / 180253f30 / 1802541f0 /
 * 180253690 / 1802538d0, the step tests and the contact decoders).
 *
 * The net, as ported (pencil.md 6):
 *  1. Grid: nodes at the weave fibre positions (CWeave over the Surfaces' box grown by PencilBigCellResolution, n = max(3, int(range /
 *     spacing) + 2) evenly spaced per axis; FUN_180150120 / FUN_180150520 pn l.661-1046); a tool drop at every node (FUN_180222a90).
 *  2. Edges: between two drops a and b, split at the midpoint while f30(a, b) (FUN_180253f30 st l.86) and mark the leaf intervals
 *     where d20(a, b) (FUN_180253d20 st l.1) - a bitangent bracket (FUN_180160ad0 / FUN_1801611d0 pn l.12439-12890; with no
 *     overthickness the mark is the bare interval, FUN_180162eb0 pn l.14181).
 *        d20: |n(a) - n(b)| > MaxContactPointChordChange (or, tool type 1, |z(a) - z(b)| > MaxVerticalStepforward), n = the contact
 *             normal of the drop (FUN_1802538d0);
 *        f30: XY |ab| > MinHorizontalChange and (d20, or |ab| > CellResolution, or |z(a) - z(b)| > MaxVerticalStepforward).
 *  3. Cells (PencilQuad, FUN_180161570 pn l.12891): a cell whose diagonal s >= MinHorizontalChange is split in four at its centre
 *     (FUN_18015f530 pn l.11600) when it has 1 or more than 2 marks, or s >= CellResolution, or its 2 marks fail the pairing test
 *     FUN_1802541f0 (st l.232) either way; the new edges are searched as in 2.
 *  4. Chains (FUN_180162190 / FUN_180161f40 / FUN_18015d700 pn l.10014): the walk passes the leaf cells with exactly 2 marks; a cell
 *     with more ends every chain that reaches it, at its corner drop with the lowest contact (FUN_1801602a0). An entry is a mark's
 *     bracket (in, out); appending E drops the previous entry when the one before it, P, passes FUN_1802541f0(P.in, E.in, P.out, E.out)
 *     and FUN_1802541f0(P.out, E.out, P.in, E.in) (a thinning to about CellResolution).
 *  5. Filter and split (FUN_18015c4c0 pn l.9223, FUN_18015c020 pn l.9086, FUN_18015bcf0 pn l.8936): the chain is dropped unless more
 *     than 2 marks change the normal by more than the threshold chord and its convex length C <= 0.9 L; it is split where its climb
 *     (in's contact height - out's) changes sign beyond MaxVerticalStepforward; each piece is turned so that "in" is its lower contact
 *     side (climbing down: reversed; climbing up: in / out swapped), filtered again with MinFragLength.
 *  6. Pass (FUN_180150e90 / FUN_180150b70 pn l.1046-1230): the piece's "in" drops, the junction drops at its ends; between two of them
 *     a drop at the XY midpoint is inserted while FUN_1802541f0(a, b, a, b) fails.
 * Ours (labelled; Mastercam's rule NOT DECODED or not ported):
 *  - the drop: the exact ball drop on our triangles (OCCT tessellation); Mastercam's tool box drop FUN_180222a90 on its own
 *    tessellation is not ported; contact normal = (centre - contact point) / R, the quantity FUN_1802538d0 decodes from the drop;
 *  - the cell types (type 3 cells are skipped by the net; the fibre test at pn l.871-891 reads weave intervals whose computation is
 *    NOT DECODED): every cell is used, and a mark against a drop with no contact (off the drive) is not used;
 *  - the row scan / step back / pairing bookkeeping (FUN_180155740, FUN_1801549b0) is replaced by one global pass over the quadtree;
 *    which end a chain starts from (only the thinning can depend on it);
 *  - the order of the passes (nearest start) and the SAFE links (the Pencil Linking Plan 0x1442 is NOT DECODED);
 *  - decoded: between two pass points the tool moves in a straight line (the pass object FUN_180118ee0 / FUN_1801194d0 / FUN_180121900, hst3d_weave_drop.c, only records the points; FUN_1801194d0 skips a point equal to the last one (FUN_18002d030) and keeps a count of the in-range flag FUN_18015e160's changes, it does not end a fragment).
 * Refused (NOT DECODED): offsets (Constant Stepover Passes From Passes 0x140d), Overthickness (the grown tool FUN_18013e330, the
 * refined marks of FUN_180162eb0 and the pre-split FUN_18015e210), tools other than a ball (the contact codes of flat / bull profiles
 * and the tool type field +0x10), unequal XY / Z stock, Zigzag, and (from hst_grid_open) Down / Up Mill, the approximate start point,
 * contact angles other than 0..90, Use Z depths. Not read: check / rest surfaces. */
#include "cam3d_pencil.h"

PcStats pc_stats;
const char *pc_dump_path;

/* ---------------------------------------------------------------------------------------------------------- the drop (ours) */
typedef struct { double x, y, z, n[3]; int hit; } PcNode;   /* a tool drop: tip (x, y, z), contact normal n, hit = a contact */
typedef struct { const C3Mesh *M; double x0, y0, bs, r; int nx, ny; int *start, *list; } PcIdx;

static void pc_idx_build(PcIdx *I, const C3Mesh *M, double r) {
    memset(I, 0, sizeof *I); I->M = M; I->r = r;
    I->bs = fmax(r, fmax(M->hi[0] - M->lo[0], M->hi[1] - M->lo[1]) / 400.0);
    I->x0 = M->lo[0] - r; I->y0 = M->lo[1] - r;
    I->nx = (int)((M->hi[0] - M->lo[0] + 2 * r) / I->bs) + 1; I->ny = (int)((M->hi[1] - M->lo[1] + 2 * r) / I->bs) + 1;
    size_t nb = (size_t)I->nx * I->ny; I->start = calloc(nb + 1, sizeof(int));
    for (int pass = 0; pass < 2; pass++) {
        int *fill = pass ? calloc(nb, sizeof(int)) : NULL;
        for (int t = 0; t < M->n; t++) {
            const double *P = M->t + 9 * t;
            double xl = fmin(P[0], fmin(P[3], P[6])) - r, xh = fmax(P[0], fmax(P[3], P[6])) + r;
            double yl = fmin(P[1], fmin(P[4], P[7])) - r, yh = fmax(P[1], fmax(P[4], P[7])) + r;
            int i0 = (int)floor((xl - I->x0) / I->bs), i1 = (int)floor((xh - I->x0) / I->bs);
            int j0 = (int)floor((yl - I->y0) / I->bs), j1 = (int)floor((yh - I->y0) / I->bs);
            if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0; if (i1 >= I->nx) i1 = I->nx - 1; if (j1 >= I->ny) j1 = I->ny - 1;
            for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
                size_t k = (size_t)j * I->nx + i;
                if (!pass) I->start[k + 1]++; else I->list[I->start[k] + fill[k]++] = t;
            }
        }
        if (!pass) { for (size_t k = 0; k < nb; k++) I->start[k + 1] += I->start[k]; I->list = malloc(sizeof(int) * (I->start[nb] + 1)); }
        free(fill);
    }
}
static void pc_idx_free(PcIdx *I) { free(I->start); free(I->list); memset(I, 0, sizeof *I); }

/* the highest ball centre over (x, y) touching triangle P; returns 1 and updates *zc / contact point c when higher */
static int pc_ball_tri(const double *P, double x, double y, double r, double *zc, double *c) {
    int up = 0; double r2 = r * r;
    /* facet */
    double e1[3] = { P[3] - P[0], P[4] - P[1], P[5] - P[2] }, e2[3] = { P[6] - P[0], P[7] - P[1], P[8] - P[2] };
    double n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
    double nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl > 1e-300) {
        for (int k = 0; k < 3; k++) n[k] /= nl;
        if (n[2] < 0) for (int k = 0; k < 3; k++) n[k] = -n[k];
        if (n[2] > 1e-9) {
            double px = x - r * n[0], py = y - r * n[1];   /* the contact point's XY */
            double den = e1[0] * e2[1] - e1[1] * e2[0];
            if (fabs(den) > 1e-300) {
                double qx = px - P[0], qy = py - P[1], b1 = (qx * e2[1] - qy * e2[0]) / den, b2 = (e1[0] * qy - e1[1] * qx) / den;
                if (b1 >= -1e-12 && b2 >= -1e-12 && b1 + b2 <= 1 + 1e-12) {
                    double pz = P[2] + b1 * e1[2] + b2 * e2[2], z = pz + r * n[2];
                    if (z > *zc) { *zc = z; c[0] = px; c[1] = py; c[2] = pz; up = 1; }
                }
            }
        }
    }
    /* vertices and edges */
    for (int k = 0; k < 3; k++) {
        const double *a = P + 3 * k, *b = P + 3 * ((k + 1) % 3);
        double dx = x - a[0], dy = y - a[1], d2 = dx * dx + dy * dy;
        if (d2 <= r2) { double z = a[2] + sqrt(r2 - d2); if (z > *zc) { *zc = z; c[0] = a[0]; c[1] = a[1]; c[2] = a[2]; up = 1; } }
        double ux = b[0] - a[0], uy = b[1] - a[1], L = sqrt(ux * ux + uy * uy);
        if (L < 1e-12) continue;
        ux /= L; uy /= L;
        double t = dx * ux + dy * uy, d = -dx * uy + dy * ux;   /* along / across the edge in XY */
        if (fabs(d) >= r) continue;
        double rr = sqrt(r2 - d * d), m = (b[2] - a[2]) / L, s = sqrt(1 + m * m);
        double tp = t + rr * m / s;                     /* the contact's parameter on the edge (the circle of radius rr in the edge's plane) */
        if (tp < 0 || tp > L) continue;
        double z = a[2] + m * tp + rr / s;
        if (z > *zc) { *zc = z; c[0] = a[0] + ux * tp; c[1] = a[1] + uy * tp; c[2] = a[2] + m * tp; up = 1; }
    }
    return up;
}
static void pc_drop(const PcIdx *I, double x, double y, PcNode *o) {
    o->x = x; o->y = y; o->hit = 0; o->n[0] = o->n[1] = 0; o->n[2] = 1; o->z = -1e30;
    pc_stats.drops++;
    int i = (int)floor((x - I->x0) / I->bs), j = (int)floor((y - I->y0) / I->bs);
    if (i < 0 || j < 0 || i >= I->nx || j >= I->ny) return;
    size_t k = (size_t)j * I->nx + i; double zc = -1e30, c[3] = { 0, 0, 0 };
    for (int q = I->start[k]; q < I->start[k + 1]; q++) pc_ball_tri(I->M->t + 9 * I->list[q], x, y, I->r, &zc, c);
    if (zc < -1e29) return;
    o->hit = 1; o->z = zc - I->r;
    o->n[0] = (x - c[0]) / I->r; o->n[1] = (y - c[1]) / I->r; o->n[2] = (zc - c[2]) / I->r;
    double l = sqrt(o->n[0] * o->n[0] + o->n[1] * o->n[1] + o->n[2] * o->n[2]); if (l > 0) for (int q = 0; q < 3; q++) o->n[q] /= l;
}

/* ------------------------------------------------------------------------------------------------- the step tests (decoded) */
typedef struct { PcParams P; PcIdx I; PcNode *nd; int nn, cap; } PcNet;
static int pc_node(PcNet *N, double x, double y) {
    if (N->nn == N->cap) { N->cap = N->cap ? 2 * N->cap : 4096; N->nd = realloc(N->nd, sizeof(PcNode) * N->cap); }
    pc_drop(&N->I, x, y, &N->nd[N->nn]); pc_stats.nodes++; return N->nn++;
}
static double pc_dn2(const PcNode *a, const PcNode *b) {
    double d0 = a->n[0] - b->n[0], d1 = a->n[1] - b->n[1], d2 = a->n[2] - b->n[2]; return d0 * d0 + d1 * d1 + d2 * d2; }
static double pc_dxy2(const PcNode *a, const PcNode *b) { double dx = a->x - b->x, dy = a->y - b->y; return dx * dx + dy * dy; }
/* FUN_180253d20 (st l.1-85): a contact discontinuity between a and b. The tool's profile segment test (step +0x30 / +0x34) only
   concerns shank contacts, which a ball drop does not make here; tool type 1 (tool +0x10, the |dz| test) is not a ball. */
static int pc_d20(const PcNet *N, const PcNode *a, const PcNode *b) {
    return pc_dn2(a, b) > N->P.chord * N->P.chord;
}
/* FUN_180253f30 (st l.86-161): split the interval further. step [2] / [8] are -1 for the pencil (FUN_180253300, pencil.md 2.2 / 6.2). */
static int pc_f30(const PcNet *N, const PcNode *a, const PcNode *b) {
    double d2 = pc_dxy2(a, b);
    if (d2 <= N->P.hmin * N->P.hmin) return 0;
    if (pc_d20(N, a, b)) return 1;
    if (d2 <= N->P.cell * N->P.cell && fabs(a->z - b->z) <= N->P.vstep) return 0;
    return 1;
}
/* FUN_1802541f0 (st l.232-280): 0 = the pair (p3 -> p4 on one side, p5 -> p6 on the other) continues one bitangent */
static int pc_cons(const PcNet *N, const PcNode *p3, const PcNode *p4, const PcNode *p5, const PcNode *p6) {
    double d2 = pc_dxy2(p3, p4);
    if (d2 <= N->P.hmin * N->P.hmin) return 0;
    if (fabs(p5->z - p6->z) <= N->P.vstep && pc_dn2(p5, p6) <= N->P.chord * N->P.chord)
        return (d2 <= N->P.cell * N->P.cell && fabs(p3->z - p4->z) <= N->P.vstep) ? 0 : 1;
    return 1;
}

/* ------------------------------------------------------------------------------------------------------------- edges */
typedef struct { double x0, y0, x1, y1; int n, cap; double *t; int *id; } PcEdge;   /* nodes sorted by t in [0, 1] */
typedef struct { PcEdge *e; int ne, cap; } PcEdges;
static int pc_edge_new(PcEdges *E, double x0, double y0, double x1, double y1) {
    if (E->ne == E->cap) { E->cap = E->cap ? 2 * E->cap : 1024; E->e = realloc(E->e, sizeof(PcEdge) * E->cap); }
    PcEdge *e = &E->e[E->ne]; memset(e, 0, sizeof *e); e->x0 = x0; e->y0 = y0; e->x1 = x1; e->y1 = y1; return E->ne++;
}
static void pc_edge_ins(PcEdge *e, int at, double t, int id) {
    if (e->n == e->cap) { e->cap = e->cap ? 2 * e->cap : 8; e->t = realloc(e->t, sizeof(double) * e->cap); e->id = realloc(e->id, sizeof(int) * e->cap); }
    memmove(e->t + at + 1, e->t + at, sizeof(double) * (e->n - at)); memmove(e->id + at + 1, e->id + at, sizeof(int) * (e->n - at));
    e->t[at] = t; e->id[at] = id; e->n++;
}
/* FUN_180160ad0: bisect the interval (i, i + 1) of edge ei while f30 */
static void pc_bisect(PcNet *N, PcEdges *E, int ei, int i) {
    PcEdge *e = &E->e[ei];
    if (!pc_f30(N, &N->nd[e->id[i]], &N->nd[e->id[i + 1]])) return;
    double tm = (e->t[i] + e->t[i + 1]) * 0.5;
    if (!(tm > e->t[i] && tm < e->t[i + 1])) return;
    int id = pc_node(N, e->x0 + (e->x1 - e->x0) * tm, e->y0 + (e->y1 - e->y0) * tm);
    e = &E->e[ei]; pc_edge_ins(e, i + 1, tm, id);
    pc_bisect(N, E, ei, i + 1);
    pc_bisect(N, E, ei, i);
}
/* the node at t on edge ei (made, with its two intervals searched, when missing) */
static int pc_edge_at(PcNet *N, PcEdges *E, int ei, double t) {
    PcEdge *e = &E->e[ei]; int i = 0;
    while (i < e->n && e->t[i] < t - 1e-12) i++;
    if (i < e->n && fabs(e->t[i] - t) <= 1e-12) return e->id[i];
    int id = pc_node(N, e->x0 + (e->x1 - e->x0) * t, e->y0 + (e->y1 - e->y0) * t);
    e = &E->e[ei]; pc_edge_ins(e, i, t, id);
    pc_bisect(N, E, ei, i);          /* (i, i + 1) first keeps i's index */
    pc_bisect(N, E, ei, i - 1);
    return id;
}
static int pc_is_mark(const PcNet *N, const PcEdge *e, int i) {
    const PcNode *a = &N->nd[e->id[i]], *b = &N->nd[e->id[i + 1]];
    return a->hit && b->hit && pc_d20(N, a, b);   /* ours: a mark against a drop without contact is not used */
}

/* ------------------------------------------------------------------------------------------------------- the quadtree */
typedef struct { int e; double t0, t1; } PcSide;   /* a cell side: edge e over [t0, t1] */
typedef struct { PcSide s[4]; int child; int leaf; } PcCell;   /* sides: 0 bottom, 1 right, 2 top, 3 left; child = first of 4 */
typedef struct { PcCell *c; int n, cap; } PcCells;
static int pc_cell_new(PcCells *C) {
    if (C->n == C->cap) { C->cap = C->cap ? 2 * C->cap : 4096; C->c = realloc(C->c, sizeof(PcCell) * C->cap); }
    memset(&C->c[C->n], 0, sizeof(PcCell)); C->c[C->n].child = -1; C->c[C->n].leaf = 1; return C->n++;
}
/* the marks of a cell, counter-clockwise: (edge, index of the lower-t node) and the bracket nodes in CCW order (X, Y) */
typedef struct { int e, i, X, Y; } PcMk;
static int pc_cell_marks(const PcNet *N, const PcEdges *E, const PcCell *c, PcMk *out, int max) {
    int k = 0;
    for (int s = 0; s < 4; s++) {
        const PcEdge *e = &E->e[c->s[s].e]; double t0 = c->s[s].t0, t1 = c->s[s].t1;
        int rev = s >= 2;   /* bottom / right run with t, top / left against it */
        int i0 = 0; while (i0 < e->n && e->t[i0] < t0 - 1e-12) i0++;
        int i1 = i0; while (i1 + 1 < e->n && e->t[i1 + 1] <= t1 + 1e-12) i1++;
        for (int q = 0; q < i1 - i0; q++) {
            int i = rev ? i1 - 1 - q : i0 + q;
            if (!pc_is_mark(N, e, i)) continue;
            if (k < max) { out[k].e = c->s[s].e; out[k].i = i; out[k].X = rev ? e->id[i + 1] : e->id[i]; out[k].Y = rev ? e->id[i] : e->id[i + 1]; }
            k++;
        }
    }
    return k;
}
/* FUN_180161570: does the cell split? */
static int pc_cell_splits(PcNet *N, PcEdges *E, PcCell *c) {
    PcEdge *eb = &E->e[c->s[0].e], *et = &E->e[c->s[2].e];
    double x0 = eb->x0 + (eb->x1 - eb->x0) * c->s[0].t0, y0 = eb->y0 + (eb->y1 - eb->y0) * c->s[0].t0;
    double x1 = et->x0 + (et->x1 - et->x0) * c->s[2].t1, y1 = et->y0 + (et->y1 - et->y0) * c->s[2].t1;
    double s = hypot(x1 - x0, y1 - y0);
    if (s < N->P.hmin) return 0;
    PcMk m[8]; int k = pc_cell_marks(N, E, c, m, 8);
    if (k > 2 || k == 1 || s >= N->P.cell) return 1;
    if (k == 2) {
        const PcNode *X1 = &N->nd[m[0].X], *Y1 = &N->nd[m[0].Y], *X2 = &N->nd[m[1].X], *Y2 = &N->nd[m[1].Y];
        if (pc_cons(N, Y1, X2, X1, Y2) || pc_cons(N, Y2, X1, X2, Y1)) return 1;
    }
    return 0;
}
/* FUN_18015f530: split at the centre; the side midpoints and the two new edges through the centre are searched */
static void pc_cell_split(PcNet *N, PcEdges *E, PcCells *C, int ci) {
    PcSide S[4]; memcpy(S, C->c[ci].s, sizeof S);
    double tm[4];
    for (int s = 0; s < 4; s++) { tm[s] = (S[s].t0 + S[s].t1) * 0.5; pc_edge_at(N, E, S[s].e, tm[s]); }
    PcEdge *eb = &E->e[S[0].e], *er = &E->e[S[1].e];
    double xm = eb->x0 + (eb->x1 - eb->x0) * tm[0], ym = er->y0 + (er->y1 - er->y0) * tm[1];
    PcEdge *el = &E->e[S[3].e], *et = &E->e[S[2].e];
    double xl = el->x0, xr = er->x0, yb = eb->y0, yt = et->y0;
    int ml = pc_edge_at(N, E, S[3].e, tm[3]), mr = pc_edge_at(N, E, S[1].e, tm[1]);
    int mb = pc_edge_at(N, E, S[0].e, tm[0]), mt = pc_edge_at(N, E, S[2].e, tm[2]);
    int cn = pc_node(N, xm, ym);
    int h = pc_edge_new(E, xl, ym, xr, ym), v = pc_edge_new(E, xm, yb, xm, yt);
    double th = (xm - xl) / (xr - xl), tv = (ym - yb) / (yt - yb);
    pc_edge_ins(&E->e[h], 0, 0.0, ml); pc_edge_ins(&E->e[h], 1, th, cn); pc_edge_ins(&E->e[h], 2, 1.0, mr);
    pc_edge_ins(&E->e[v], 0, 0.0, mb); pc_edge_ins(&E->e[v], 1, tv, cn); pc_edge_ins(&E->e[v], 2, 1.0, mt);
    pc_bisect(N, E, h, 1); pc_bisect(N, E, h, 0); pc_bisect(N, E, v, 1); pc_bisect(N, E, v, 0);
    int k0 = pc_cell_new(C); pc_cell_new(C); pc_cell_new(C); pc_cell_new(C);
    PcCell *c = C->c;   /* BL, BR, TR, TL */
    c[k0 + 0].s[0] = (PcSide){ S[0].e, S[0].t0, tm[0] }; c[k0 + 0].s[1] = (PcSide){ v, 0, tv }; c[k0 + 0].s[2] = (PcSide){ h, 0, th }; c[k0 + 0].s[3] = (PcSide){ S[3].e, S[3].t0, tm[3] };
    c[k0 + 1].s[0] = (PcSide){ S[0].e, tm[0], S[0].t1 }; c[k0 + 1].s[1] = (PcSide){ S[1].e, S[1].t0, tm[1] }; c[k0 + 1].s[2] = (PcSide){ h, th, 1 }; c[k0 + 1].s[3] = (PcSide){ v, 0, tv };
    c[k0 + 2].s[0] = (PcSide){ h, th, 1 }; c[k0 + 2].s[1] = (PcSide){ S[1].e, tm[1], S[1].t1 }; c[k0 + 2].s[2] = (PcSide){ S[2].e, tm[2], S[2].t1 }; c[k0 + 2].s[3] = (PcSide){ v, tv, 1 };
    c[k0 + 3].s[0] = (PcSide){ h, 0, th }; c[k0 + 3].s[1] = (PcSide){ v, tv, 1 }; c[k0 + 3].s[2] = (PcSide){ S[2].e, S[2].t0, tm[2] }; c[k0 + 3].s[3] = (PcSide){ S[3].e, tm[3], S[3].t1 };
    c[ci].leaf = 0; c[ci].child = k0;
}

/* --------------------------------------------------------------------------------------------------------- the chains */
typedef struct { int X, Y; } PcEnt;          /* a chain entry: in, out drops */
typedef struct { PcEnt *v; int n, cap, closed, j0, j1; } PcChain;   /* j0 / j1: junction drops at the ends (-1: none) */
static void pc_ent_push(PcChain *c, PcEnt e) { if (c->n == c->cap) { c->cap = c->cap ? 2 * c->cap : 32; c->v = realloc(c->v, sizeof(PcEnt) * c->cap); } c->v[c->n++] = e; }

/* FUN_18015c020 (pn l.9086-9222): 1 = discard. Strong marks: |n(in) - n(out)| > the threshold chord; L = the XY length of the moves
   prev.out -> cur.in; C = the part of L where p . (n(in) - n(out)) <= 0 at both ends, p = the move's left normal (side 0) or right
   normal (side 1); kept only when strong > 2, L > minlen (-1: any) and C <= 0.9 L. why: 1 convex, 2 short, 3 too few strong marks */
static int pc_discard(const PcNet *N, const PcEnt *v, int n, double minlen, int side, int *why) {
    int strong = 0; double L = 0, C = 0;
    for (int k = 0; k < n; k++) {
        const PcNode *in = &N->nd[v[k].X], *out = &N->nd[v[k].Y];
        if (pc_dn2(in, out) > N->P.chord_thr * N->P.chord_thr) strong++;
        if (!k) continue;
        const PcNode *pin = &N->nd[v[k - 1].X], *pout = &N->nd[v[k - 1].Y];
        double dx = in->x - pout->x, dy = in->y - pout->y, d = sqrt(dx * dx + dy * dy);
        if (d == 0) continue;
        L += d;
        double px = side ? dy : -dy, py = side ? -dx : dx;
        double a = px * (pin->n[0] - pout->n[0]) + py * (pin->n[1] - pout->n[1]);
        double b = px * (in->n[0] - out->n[0]) + py * (in->n[1] - out->n[1]);
        if (a <= 0 && b <= 0) C += d;   /* (tool type 1 - not a ball - takes another branch here, param_7) */
    }
    if (strong > 2 && (minlen == -1.0 || minlen < L)) {
        if (C <= L * 0.9) return 0;
        *why = 1; return 1;
    }
    *why = strong > 2 ? 2 : 3; return 1;
}
/* the climb of an entry: the contact height of in minus that of out (tip z + the contact's height on the ball, FUN_180253690 +0x10) */
static double pc_climb(const PcNet *N, const PcEnt *e) {
    const PcNode *in = &N->nd[e->X], *out = &N->nd[e->Y];
    return (in->z + N->P.R * (1 - in->n[2])) - (out->z + N->P.R * (1 - out->n[2]));
}
static void pc_chain_add(PcChain **L, int *nL, PcChain c) { *L = realloc(*L, sizeof(PcChain) * (*nL + 1)); (*L)[(*nL)++] = c; }
/* FUN_18015c4c0 (pn l.9223-9898) without overthickness (Eva +0x1350 = the net's PencilOverthickness = 0: no pre-split by
   FUN_18015e210): split the chain where its climb changes sign beyond MaxVerticalStepforward v, each split moved back to where the
   change starts; each piece copied by FUN_18015bcf0 (pn l.8936): a piece that climbs down (b = 0) is reversed, one that climbs up has
   in / out swapped - so "in" is the lower contact side of every piece; a piece overlaps the next by one entry unless that entry
   already climbs the other way by v or more, when a two-entry "vertical climb-changing piece" joins them; pieces filtered by
   FUN_18015c020 (side 1, MinFragLength). */
static void pc_split(const PcNet *N, const PcChain *ch, PcChain **out, int *nout) {
    int n = ch->n; double v = N->P.vstep, ext = 0;
    double *c = malloc(sizeof(double) * (n + 1)); int *S = malloc(sizeof(int) * (n + 1)), ns = 0, b = 0;
    for (int k = 0; k < n; k++) {
        c[k] = pc_climb(N, &ch->v[k]);
        if (fabs(c[k]) > fabs(ext)) ext = c[k];
        if (c[k] > v && (!ns || !b)) { S[ns++] = k; b = 1; }
        if (c[k] < -v && (!ns || b)) { S[ns++] = k; b = 0; }
    }
    int bb = b;
    for (int i = ns - 2; i >= 0; i--) {   /* move split i + 1 back to the entry with the least wrong-way climb */
        double best = 0; int sb = S[i];
        for (int s0 = S[i]; s0 < S[i + 1]; s0++) {
            double M = 0;
            for (int e = S[i]; e < S[i + 1]; e++) { int dir = e < s0 ? !bb : bb; double val = -c[e] * (dir ? 1 : -1); if (val > M) M = val; }
            if (s0 == S[i] || M < best) { best = M; sb = s0; }
        }
        if (best < v) S[i + 1] = sb;
        bb = !bb;
    }
    if (!ns) { S[ns++] = 0; bb = ext >= 0; } else S[0] = 0;
    PcEnt *v2 = malloc(sizeof(PcEnt) * (n + 2));
    for (int i = 0; i < ns; i++) {
        int nx = i + 1 < ns ? S[i + 1] : n, mode = i + 1 < ns, m = 0;
        for (int k = S[i]; k < nx; k++) v2[m++] = ch->v[k];
        PcChain P = { 0 }; P.closed = ns == 1 && ch->closed; P.j0 = i == 0 ? ch->j0 : -1; P.j1 = nx == n ? ch->j1 : -1;
        int climb_piece = 0;
        if (mode && m) {
            double cn = c[nx], lim = bb ? -v : v;
            int beyond = bb ? cn <= lim : cn >= lim;
            if (beyond && v <= fabs(cn)) climb_piece = 1; else v2[m++] = ch->v[nx];
        }
        PcEnt last = m ? v2[m - 1] : ch->v[S[i]];
        if (!bb) { for (int a = 0, z = m - 1; a < z; a++, z--) { PcEnt t = v2[a]; v2[a] = v2[z]; v2[z] = t; } int t = P.j0; P.j0 = P.j1; P.j1 = t; }
        else for (int a = 0; a < m; a++) { int t = v2[a].X; v2[a].X = v2[a].Y; v2[a].Y = t; }
        int w = 0;
        if (m && !pc_discard(N, v2, m, N->P.minfrag, 1, &w)) {
            P.v = malloc(sizeof(PcEnt) * m); memcpy(P.v, v2, sizeof(PcEnt) * m); P.n = P.cap = m; pc_chain_add(out, nout, P); pc_stats.kept++;
        } else if (m) { if (w == 1) pc_stats.conv_dropped++; else if (w == 2) pc_stats.short_dropped++; else pc_stats.few_dropped++; }
        if (climb_piece) {   /* the vertical climb-changing piece: (a copy of the last entry, the next one), in / out of one swapped */
            PcChain Q = { 0 }; Q.j0 = Q.j1 = -1; Q.v = malloc(sizeof(PcEnt) * 2); Q.n = Q.cap = 2;
            Q.v[0] = last; Q.v[1] = ch->v[nx];
            PcEnt *sw = bb ? &Q.v[0] : &Q.v[1]; int t = sw->X; sw->X = sw->Y; sw->Y = t;
            pc_chain_add(out, nout, Q); pc_stats.climb_pieces++;
        }
        bb = !bb;
    }
    free(v2); free(c); free(S);
}

static int pc_generate(const McamFile *f, CamOp *op, MB *b, HstGrid *gp, uint8_t *rec, double D, double Rc, double szs, double bt, int other_way);
static int gen_pencil(const McamFile *f, CamOp *op, MB *b) {
    memset(&pc_stats, 0, sizeof pc_stats);
    CamParams *p = &op->p; uint32_t rn = 0; HstGrid g;
    uint8_t *rec = hst_grid_open(f, op, "Pencil", &rn, &g);
    if (!rec) return 0;
    #define RD(o) c3d(rec, rn, o)
    #define RB(o) c3b(rec, rn, o)
    const char *why = NULL;
    double D = p->tool_dia > 0 ? p->tool_dia : RD(0x350), Rc = p->tool_dia > 0 ? p->corner_r : RD(0x358);
    double sx = RD(0x13c0), szs = RD(0x13c8), sxy = fmax(sx, szs), stol = c3_step_tol(rec, rn, mcam_is_metric(f));   /* StepMachTolerance job +0xa0 */
    int omode = RB(0x16cb); int32_t noff = 0; if (rn >= 0x1544) memcpy(&noff, rec + 0x1540, 4);
    double over = RD(0x12d0);
    if (Rc + fmin(sxy, szs) < -2 * stol) over += fabs(Rc + fmin(sxy, szs)) + stol;   /* mk l.87625-87655 (before local_868) */
    if (Rc < stol) { double l868 = (stol - Rc) * 0.5; sxy += l868; szs += l868; }   /* style 5 only: local_868 = (tol - Rc) / 2 is added to ToolThicknessXY / Z, mk l.87400-87407, l.87573-87611 */
    if (p->file_op_id > 0 && RB(0x16bc) != 6) why = "the op is not an HST Pencil (record 0x16bc != 6)";
    else if (omode == 2 || (omode == 1 && noff >= 1)) why = "offsets (Limit # of offsets): the Constant Stepover Passes From Passes plan 0x140d is NOT DECODED";
    else if (over > 1e-9) why = "Overthickness: the grown tool (FUN_18013e330) and the refined bitangent marks (FUN_180162eb0) are not ported";
    else if (fabs(Rc - D * 0.5) > 1e-9 * fmax(1, D)) why = "only a ball tool: the contact codes of flat / bull profiles and the tool type test (tool +0x10) are NOT DECODED";
    else if (fabs(sxy - szs) > 1e-12) why = "unequal XY / Z stock (the stock-inflated tool is no longer a ball)";
    else if (RB(0x16bd)) why = "Zigzag (LinkDirectionStyle 1): the Pencil Linking Plan 0x1442 is NOT DECODED";
    if (why) { snprintf(op->error, sizeof op->error, "Pencil: %s", why); free(rec); hst_grid_close(&g); return 0; }
    double bt = RD(0x12d8) * 180.0 / M_PI;   /* Bitangency angle: radians in the HST record (pencil.md 1) */
    int other_way = RB(0x16d5);
    #undef RD
    #undef RB
    return pc_generate(f, op, b, &g, rec, D, Rc, szs, bt, other_way);
}

/* the net, the passes and the links of both Pencil ops (HST op 132 style 6: gen_pencil; classic op 39: gen_sfpencil - both run McHmm's
   Pencil Passes Plan 0x140f, formats/surf_classic3.md 9.3 / 10). gp: the drive mesh, the containment and the feeds (hst_grid_open, or
   gen_sfpencil's own), closed here; rec is freed here; D / Rc the tool; szs = ToolThicknessZ (= ToolThicknessXY: ball tools with equal
   stock only); bt = the Bitangency angle in degrees; other_way = LinkReverseDirection. */
static int pc_generate(const McamFile *f, CamOp *op, MB *b, HstGrid *gp, uint8_t *rec, double D, double Rc, double szs, double bt, int other_way) {
    const char *why = NULL; HstGrid g = *gp;   /* closed by hst_grid_done / hst_grid_close below */
    /* parameters (pencil.md 1, 1.1) */
    PcNet N; memset(&N, 0, sizeof N);
    double sys = 1e-9;   /* SystemContext +0x10: a tolerance [inferred] */
    N.P.max_normal_deg = bt >= sys ? 180.0 - bt : 15.0;
    N.P.chord = 2.0 * sin(N.P.max_normal_deg * 0.5 * M_PI / 180.0);          /* the interpreter's sin takes degrees (pencil.md 1.1) */
    N.P.chord_thr = 2.0 * sin((N.P.max_normal_deg + 5.0) * 0.5 * M_PI / 180.0);
    /* McHmm works in mm: job +0x80 = 25.4 when the part is inch (FUN_18007bb48 mk l.91625-91633) and every length goes through it (FUN_1800edd8c), so
       the plan defaults below are mm values: u = part units per mm */
    double u = mcam_is_metric(f) ? 1.0 : 1.0 / 25.4;
    N.P.hmin = 0.003 * u;                                /* StepMinHorizontalChange default (scallop.md 1) */
    double shaft = D * 0.5;                              /* ToolShaftRadius of a Mastercam tool: D / 2 [inferred] */
    N.P.cell = fmin(2.5 * u, shaft * 2.0); N.P.big = N.P.cell * 7.0;
    N.P.vstep = ((Rc + szs == 0.0) ? 0.5 : 2.0) * u;
    N.P.minfrag = 0.1 * u;
    N.P.R = D * 0.5 + szs;
    free(g.H.z); g.H.z = NULL;
    pc_idx_build(&N.I, &g.M, N.P.R);
    /* 1. the grid (the weave fibres over the Surfaces' box grown by the spacing) */
    double lo[2] = { g.M.lo[0] - N.P.big, g.M.lo[1] - N.P.big }, hi[2] = { g.M.hi[0] + N.P.big, g.M.hi[1] + N.P.big };
    int n[2]; double *pos[2];
    for (int a = 0; a < 2; a++) {
        n[a] = (int)((hi[a] - lo[a]) / N.P.big) + 2; if (n[a] < 3) n[a] = 3;
        pos[a] = malloc(sizeof(double) * n[a]);
        for (int i = 0; i < n[a]; i++) pos[a][i] = lo[a] + i * (hi[a] - lo[a]) / (n[a] - 1);
    }
    int *gid = malloc(sizeof(int) * n[0] * n[1]);
    for (int j = 0; j < n[1]; j++) for (int i = 0; i < n[0]; i++) gid[j * n[0] + i] = pc_node(&N, pos[0][i], pos[1][j]);
    PcEdges E = { 0 }; PcCells C = { 0 };
    int *he = malloc(sizeof(int) * n[0] * n[1]), *ve = malloc(sizeof(int) * n[0] * n[1]);
    for (int j = 0; j < n[1]; j++) for (int i = 0; i < n[0]; i++) {
        if (i + 1 < n[0]) { int e = pc_edge_new(&E, pos[0][i], pos[1][j], pos[0][i + 1], pos[1][j]);
            pc_edge_ins(&E.e[e], 0, 0, gid[j * n[0] + i]); pc_edge_ins(&E.e[e], 1, 1, gid[j * n[0] + i + 1]); pc_bisect(&N, &E, e, 0); he[j * n[0] + i] = e; }
        if (j + 1 < n[1]) { int e = pc_edge_new(&E, pos[0][i], pos[1][j], pos[0][i], pos[1][j + 1]);
            pc_edge_ins(&E.e[e], 0, 0, gid[j * n[0] + i]); pc_edge_ins(&E.e[e], 1, 1, gid[(j + 1) * n[0] + i]); pc_bisect(&N, &E, e, 0); ve[j * n[0] + i] = e; }
    }
    for (int j = 0; j + 1 < n[1]; j++) for (int i = 0; i + 1 < n[0]; i++) {
        int c = pc_cell_new(&C);
        C.c[c].s[0] = (PcSide){ he[j * n[0] + i], 0, 1 }; C.c[c].s[1] = (PcSide){ ve[j * n[0] + i + 1], 0, 1 };
        C.c[c].s[2] = (PcSide){ he[(j + 1) * n[0] + i], 0, 1 }; C.c[c].s[3] = (PcSide){ ve[j * n[0] + i], 0, 1 };
    }
    free(he); free(ve); free(gid); free(pos[0]); free(pos[1]);
    /* 3. split until no leaf splits (ours: repeated global passes; a split also changes its neighbours' sides) */
    for (int pass = 0, changed = 1; changed && pass < 64; pass++) {
        changed = 0;
        for (int c = 0; c < C.n; c++) if (C.c[c].leaf && pc_cell_splits(&N, &E, &C.c[c])) { pc_cell_split(&N, &E, &C, c); changed = 1; }
    }
    /* 4. chains: the marks (edge, index) get ids; each leaf joins its marks */
    int *mbase = malloc(sizeof(int) * (E.ne + 1)); int nm = 0;
    for (int e = 0; e < E.ne; e++) { mbase[e] = nm; nm += E.e[e].n; }
    mbase[E.ne] = nm;
    int (*lnk)[2] = malloc(sizeof(int[2]) * (nm + 1)); int *mX = malloc(sizeof(int) * (nm + 1)), *mY = malloc(sizeof(int) * (nm + 1)), *ism = calloc(nm + 1, sizeof(int)), *jnc = malloc(sizeof(int) * (nm + 1));
    for (int q = 0; q < nm; q++) { lnk[q][0] = lnk[q][1] = -1; jnc[q] = -1; }
    for (int e = 0; e < E.ne; e++) for (int i = 0; i + 1 < E.e[e].n; i++) if (pc_is_mark(&N, &E.e[e], i)) { int q = mbase[e] + i; ism[q] = 1; mX[q] = E.e[e].id[i]; mY[q] = E.e[e].id[i + 1]; pc_stats.marks++; }
    for (int c = 0; c < C.n; c++) {
        if (!C.c[c].leaf) continue;
        pc_stats.leaves++;
        PcMk m[16]; int k = pc_cell_marks(&N, &E, &C.c[c], m, 16); if (k > 16) k = 16;
        if (k == 2) {   /* FUN_18015d700: the walk passes a cell with exactly 2 marks */
            int qa = mbase[m[0].e] + m[0].i, qb = mbase[m[1].e] + m[1].i;
            if (qa == qb) continue;
            for (int s = 0; s < 2; s++) if (lnk[qa][s] < 0) { lnk[qa][s] = qb; break; }
            for (int s = 0; s < 2; s++) if (lnk[qb][s] < 0) { lnk[qb][s] = qa; break; }
        } else if (k > 2) {   /* a junction: every mark ends a chain there, at the cell's corner with the lowest contact (FUN_1801602a0) */
            int jn = -1; double jz = 0;
            for (int s = 0; s < 4; s++) {
                const PcSide *S = &C.c[c].s[s]; const PcEdge *e = &E.e[S->e];
                int id = -1; double t = s < 2 ? S->t0 : S->t1;   /* corners: bottom-left, bottom-right, top-right, top-left */
                for (int i = 0; i < e->n; i++) if (fabs(e->t[i] - t) <= 1e-12) { id = e->id[i]; break; }
                if (id < 0 || !N.nd[id].hit) continue;
                double z = N.nd[id].z + N.P.R * (1 - N.nd[id].n[2]);   /* tip z + the contact's height on the ball (FUN_180253690 +0x10) */
                if (jn < 0 || z < jz) { jn = id; jz = z; }
            }
            for (int a = 0; a < k && a < 16; a++) jnc[mbase[m[a].e] + m[a].i] = jn;
        }
    }
    char *used = calloc(nm + 1, 1);
    PcChain *ch = NULL; int nch = 0;
    for (int round = 0; round < 2; round++)          /* open chains from their ends first, then the loops */
        for (int q0 = 0; q0 < nm; q0++) {
            if (!ism[q0] || used[q0]) continue;
            int deg = (lnk[q0][0] >= 0) + (lnk[q0][1] >= 0);
            if (round == 0 && deg == 2) continue;
            int *seq = NULL, ns = 0, cs = 0, prev = -1, q = q0, closed = 0;
            while (q >= 0 && !used[q]) {
                used[q] = 1; if (ns == cs) { cs = cs ? 2 * cs : 64; seq = realloc(seq, sizeof(int) * cs); } seq[ns++] = q;
                int nx = lnk[q][0] == prev ? lnk[q][1] : lnk[q][0];
                if (lnk[q][0] == prev && lnk[q][1] == prev) nx = -1;
                if (nx == q0 && ns > 2) closed = 1;
                prev = q; q = nx;
            }
            /* the entries (the thinning of FUN_18015d700): "in" = the node on the right of the walk. The walk's side rule (local_68) is
               not followed through the PencilQuad lists; "right" is what the two decoded convex tests of FUN_18015c020 (side 0 on the
               chain, side 1 on the normalised pieces) need to keep a concave seam, which Mastercam keeps (pencil.md 6.7). Our walk
               direction (which end a chain starts from) is ours; the pieces' final direction does not depend on it (pc_split). */
            PcChain c = { 0 }; c.closed = closed; c.j0 = closed ? -1 : jnc[seq[0]]; c.j1 = closed ? -1 : jnc[seq[ns - 1]];
            for (int k = 0; k < ns; k++) {
                int qa = seq[k > 0 ? k - 1 : k], qb = seq[k + 1 < ns ? k + 1 : k];
                const PcNode *A = &N.nd[mX[qa]], *B = &N.nd[mX[qb]];
                double dx = B->x - A->x, dy = B->y - A->y;
                const PcNode *x = &N.nd[mX[seq[k]]], *y = &N.nd[mY[seq[k]]];
                double mx = (x->x + y->x) * 0.5, my = (x->y + y->y) * 0.5;
                int x_left = dx * (x->y - my) - dy * (x->x - mx) > 0;
                PcEnt E1 = { x_left ? mY[seq[k]] : mX[seq[k]], x_left ? mX[seq[k]] : mY[seq[k]] };
                if (c.n >= 2) {
                    const PcEnt *P = &c.v[c.n - 2];
                    if (!pc_cons(&N, &N.nd[P->X], &N.nd[E1.X], &N.nd[P->Y], &N.nd[E1.Y]) && !pc_cons(&N, &N.nd[P->Y], &N.nd[E1.Y], &N.nd[P->X], &N.nd[E1.X])) c.n--;
                }
                pc_ent_push(&c, E1);
            }
            free(seq);
            ch = realloc(ch, sizeof(PcChain) * (nch + 1)); ch[nch++] = c; pc_stats.chains++;
        }
    free(used); free(ism); free(jnc); free(lnk); free(mbase); free(mX); free(mY);
    /* 5. filter (FUN_18015c4c0): the chain (side 0, no minimum length), then the split into pieces (pc_split) */
    FILE *dump = pc_dump_path ? fopen(pc_dump_path, "w") : NULL;
    PcChain *pcs = NULL; int npc = 0;
    for (int c = 0; c < nch; c++) {
        PcChain *C1 = &ch[c]; int w = 0;
        if (dump) { fprintf(dump, "chain %d n %d closed %d\n", c, C1->n, C1->closed);
            for (int k = 0; k < C1->n; k++) { const PcNode *a = &N.nd[C1->v[k].X], *o = &N.nd[C1->v[k].Y];
                fprintf(dump, "%f %f %f  in n %.3f %.3f %.3f  out %f %f %f n %.3f %.3f %.3f\n", a->x, a->y, a->z, a->n[0], a->n[1], a->n[2], o->x, o->y, o->z, o->n[0], o->n[1], o->n[2]); } }
        if (pc_discard(&N, C1->v, C1->n, -1.0, 0, &w)) { if (w == 1) pc_stats.conv_dropped++; else pc_stats.few_dropped++; continue; }
        pc_split(&N, C1, &pcs, &npc);
    }
    int npass = 0; double **pp = NULL; int *pn = NULL;
    for (int c = 0; c < npc; c++) {
        PcChain *C1 = &pcs[c];
        /* 6. the pass: the in drops, a midpoint drop inserted while FUN_1802541f0(a, b, a, b) fails (FUN_180150b70) */
        int cap = C1->n * 2 + 8, m = 0; int *ids = malloc(sizeof(int) * cap);
        if (C1->j0 >= 0 && C1->j0 != C1->v[0].X) ids[m++] = C1->j0;   /* FUN_180150e90: the junction drops at the ends (chain +0x30 / +0x38) */
        for (int k = 0; k < C1->n; k++) ids[m++] = C1->v[k].X;
        if (C1->closed) ids[m++] = C1->v[0].X;
        if (C1->j1 >= 0 && C1->j1 != C1->v[C1->n - 1].X) ids[m++] = C1->j1;
        for (int k = 0; k + 1 < m; ) {
            const PcNode *A = &N.nd[ids[k]], *B = &N.nd[ids[k + 1]];
            if (pc_cons(&N, A, B, A, B)) {   /* FUN_180150b70 pn l.1046-1188: cons alone (0 once |AB| <= MinHorizontalChange) */
                int id = pc_node(&N, (A->x + B->x) * 0.5, (A->y + B->y) * 0.5);
                if (m == cap) { cap *= 2; ids = realloc(ids, sizeof(int) * cap); }
                memmove(ids + k + 2, ids + k + 1, sizeof(int) * (m - k - 1)); ids[k + 1] = id; m++; pc_stats.inserted++;
            } else k++;
        }
        /* the pass: straight moves between these drops, decoded: the object at Eva +0x4f0 only records them (FUN_180118ee0 starts, FUN_1801194d0 adds a point: a point equal to the last (FUN_18002d030, every coordinate exactly equal) is
           skipped, and +0x170 counts changes of the per-point flag without ending anything (hst3d_weave_drop.c l.5940-5945), FUN_180121900 closes; l.5622-5726 / 5922-5975 / 11848-12122; the flag FUN_18015e160 pn: tip z + ToolThicknessZ in PosZLo..PosZHi and the
           contact's nz in the contact-angle window, always true at the limits hst_grid_open accepts). The midpoints are inserted only when net+0x360 = 0, FUN_180150e90 (PencilOverthickness = 0: anything above 1e-9 is refused here). */
        double *xyz = malloc(sizeof(double) * 3 * m); int q = 0;
        for (int k = 0; k < m; k++) { const PcNode *A = &N.nd[ids[k]]; if (!A->hit) continue; /* ours: a point with no contact has no Z here (Mastercam's drop over nothing: the cell types, NOT DECODED) */ if (q && xyz[3 * q - 3] == A->x && xyz[3 * q - 2] == A->y && xyz[3 * q - 1] == A->z + szs) continue;   /* FUN_18002d030 */ xyz[3 * q] = A->x; xyz[3 * q + 1] = A->y; xyz[3 * q + 2] = A->z + szs; q++; }   /* z + (float)Eva+0x140 (FUN_180150e90 pn l.1189-1346), Eva+0x140 = ToolThicknessZ 0x1d20 (FUN_1802f4c80, stored at 1802f2cff in FUN_1802f2340) */
        free(ids);
        if (dump) { fprintf(dump, "pass %d n %d\n", npass, q); for (int k = 0; k < q; k++) fprintf(dump, "%f %f %f\n", xyz[3 * k], xyz[3 * k + 1], xyz[3 * k + 2]); }
        for (int k = 0; dump && k + 1 < q; k++) {   /* ours (testing, with the dump): the deepest gouge of the straight moves (vertical or sideways) */
            double *A = xyz + 3 * k, *B = A + 3; int ns = (int)ceil(hypot(B[0] - A[0], B[1] - A[1]) / 0.005);
            for (int j = 1; j < ns; j++) { double t = (double)j / ns; PcNode o; pc_drop(&N.I, A[0] + (B[0] - A[0]) * t, A[1] + (B[1] - A[1]) * t, &o);
                double zt = A[2] + (B[2] - A[2]) * t, gz = o.hit ? o.z + szs - zt : 0;   /* the smaller of the vertical and the sideways clearance; both sides as the tip with ToolThicknessZ added */
                if (gz > 1e-9) { double L = hypot(B[0] - A[0], B[1] - A[1]), ux = -(B[1] - A[1]) / L, uy = (B[0] - A[0]) / L, best = gz;
                    for (double sd = 0.0005 * N.P.R / 0.125; sd < best && sd < N.P.R; sd += 0.0005 * N.P.R / 0.125) for (int sg = -1; sg <= 1; sg += 2) { PcNode w;
                        pc_drop(&N.I, o.x + sg * sd * ux, o.y + sg * sd * uy, &w); if (!w.hit || w.z + szs <= zt + 1e-9) { if (sd < best) best = sd; } }
                    gz = best; }
                if (gz > pc_stats.gouge) { pc_stats.gouge = gz;
                    pc_stats.gxy[0] = o.x; pc_stats.gxy[1] = o.y; pc_stats.gxy[2] = o.z; } }
        }
        pp = realloc(pp, sizeof(double *) * (npass + 1)); pn = realloc(pn, sizeof(int) * (npass + 1)); pp[npass] = xyz; pn[npass] = q; npass++;
        pc_stats.points += q;
    }
    if (dump) fclose(dump);
    for (int c = 0; c < nch; c++) free(ch[c].v);
    free(ch);
    for (int c = 0; c < npc; c++) free(pcs[c].v);
    free(pcs);
    for (int e = 0; e < E.ne; e++) { free(E.e[e].t); free(E.e[e].id); }
    free(E.e); free(C.c); free(N.nd); pc_idx_free(&N.I);
    if (why) { snprintf(op->error, sizeof op->error, "Pencil: %s", why); for (int k = 0; k < npass; k++) free(pp[k]); free(pp); free(pn); free(rec); hst_grid_close(&g); return 0; }
    /* 7. the passes: cut to the containment (as hst_grid_pass), each in its direction (LinkDirectionStyle 0; Other Way = LinkReverseDirection),
       the next one = the nearest start (ours), SAFE links (ours): the Pencil Linking Plan 0x1442 is NOT DECODED */
    char *done = calloc(npass + 1, 1); double cx = 0, cy = 0; int first = 1;
    for (int it = 0; it < npass; it++) {
        int best = -1, rev = 0; double bd = 1e300;
        for (int k = 0; k < npass; k++) {
            if (done[k] || pn[k] < 2) continue;
            double *s = other_way ? pp[k] + 3 * (pn[k] - 1) : pp[k];   /* one way: each pass in the net's direction (Other Way: reversed) */
            double d0 = first ? k : hypot(s[0] - cx, s[1] - cy);
            if (d0 < bd) { bd = d0; best = k; }
        }
        if (best < 0) break;
        done[best] = 1; first = 0; rev = other_way;
        int m = pn[best]; double *s = pp[best];
        int k = 0;
        while (k < m) {
            int j = rev ? m - 1 - k : k;
            if (g.region.n && !wl_inside(&g.region, s[3 * j], s[3 * j + 1])) { k++; continue; }
            int a0 = k;
            while (k < m) { int jj = rev ? m - 1 - k : k; if (g.region.n && !wl_inside(&g.region, s[3 * jj], s[3 * jj + 1])) break; k++; }
            if (k - a0 < 2) continue;
            for (int t = a0; t < k; t++) {
                int jj = rev ? m - 1 - t : t; double x = s[3 * jj], y = s[3 * jj + 1], z = s[3 * jj + 2];
                if (t == a0) { if (g.down) { V3 l = lastp(b); mv(b, 1, l.x, l.y, g.fp, 0); } mv(b, 1, x, y, g.fp, 0); mv(b, 0, x, y, z, g.plunge); g.down = 1; }
                else mv(b, 0, x, y, z, g.feed);
                cx = x; cy = y;
            }
            g.npass++;
        }
    }
    free(done); for (int k = 0; k < npass; k++) free(pp[k]); free(pp); free(pn); free(rec);
    return hst_grid_done(&g, op, b);
}

/* Surface Finish Pencil (classic op 39 = 0x27; the Toolpaths manager's Surface finish > Pencil). Decode: formats/surf_classic3.md 9.3 / 10
   (ms l.N = decomp/mcmsurf_all.c, mk l.N = decomp/mckernel_all.c; record = op struct + 0x68; TP+x = the MCHSTToolPath copy's fields).
   Without check surfaces Mastercam does not run the classic finish engine (FUN_180133f10 modes 9 / 8): msurf_use_3rd_party_pencil (1 by
   default, read_appmch_cfg ms l.18484) gives the op msurf_use_3rd_party_algorithm (bnci_surface ms l.12256-12267) and
   process_3rd_party_algorithm FUN_1800199d0 (ms l.13350-13377) -> mccore run_mchardmill_macro -> MakeMCHSTToolPath FUN_180573610 (type
   0x27 -> the generic MCHSTToolPath FUN_1809228d0, decomp/poszlo/hardmill.c l.3-54) -> FUN_1809232b0 / FUN_180923dd0 copy the op
   (decomp/poszlo/setup.c l.49-83, decomp/proj/d4.c l.16-59) -> mckernel FUN_18007bb48 (mk l.91845) -> FUN_180081850 (mk l.94155-94300):
   "// Mastercam X - Pencil Pass Macro", the Pencil Passes Plan 0x140f (the HST Pencil's net: pc_generate) and the Pencil Linker 0x1442.
   Kernel parameters (FUN_18007a988 mk l.91071-91199):
     ToolThicknessXY = ToolThicknessZ = the stock 0x2e0 (TP+0x40) + l, StepMachTolerance = tol / 2 + l, tol = 0x1240 (TP+0x128),
       l = (tol / 2 - Rc) / 2 when the corner radius Rc < tol / 2, else 0;
     PencilOverthickness = 0x1340 (TP+0x190); PencilBiTangentMaxNormalChange = 180 - Bitangency angle 0x1338 (TP+0x198, degrees) when it is
       >= SystemContext +0x10, else 15 (page 240 stores 0 for its default 165, CMsPnclPg OK FUN_1800ec2c0 ms l.152543);
     PosZLo / PosZHi only with Depth limits 0x12e8 (TP+0x168); PosContactOnly = true; ContactAngleLo / Hi = 0 / tol only when neither
       Allow negative (0x1300, TP+0x180) nor Allow positive Z motion (0x1301, TP+0x181) is set.
   Links (FUN_18007b240, read from the asm 18007b240-18007b534): with both Allow flags LinkPencilDirectionStyle 0 and LinkClimbMilling =
     (Machining direction off 0x1320 (TP+0x1a6) and Cut method Zigzag 0x1326 = 0 (TP+0x1a4)) ? false : (0x1320 or Climb 0x1304 (TP+0x1a7));
     otherwise LinkPencilDirectionStyle 1 (negative only) / 2 (positive only) / 0 and LinkClimbMilling = 0x1320 or 0x1304. LinkStartX / Y = the
     approximate start point. The linker's use of these is NOT DECODED.
   Offsets (FUN_180081850): Parallel passes 0x1324 (radio 642: 0 None, 1 Number per side, 2 Unlimited) >= 1 and its stepover 0x1328 > tol ->
     Constant Stepover Passes From Passes 0x140d + Constant Surface Stepover Linker 0x1420.
   Ported: the parameters above into the net (pc_generate). Ours, as gen_pencil's: the drop on our tessellation (at the tolerance 0x1240), the
   cell types, the pass order and the SAFE links. The containment: every chain of the op (c3_containment, ms l.13325-13337) with the classic
   tool containment offset (FUN_180128390 ms l.196444-196457: 0x12c0 + D / 2 when 0x12b8 = 0; 0x12c8 = 0 Inside / 1 Center / 2 Outside).
   Refused: offsets, Overthickness, non-ball tools, a negative stock (FUN_18007a988 clears it under a job flag +0x38: NOT DECODED), Depth
   limits, the approximate start point 0x1305, every link setting but the default op's (LinkClimbMilling false, LinkPencilDirectionStyle 0),
   check surfaces (with them the op takes FUN_180133f10 / FUN_1801cfb60: NOT PORTED; c3_mesh_op's classic refusal). */
static int gen_sfpencil(const McamFile *f, CamOp *op, MB *b) {
    memset(&pc_stats, 0, sizeof pc_stats);
    CamParams *p = &op->p; const char *nm = "Surface Finish Pencil";
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "%s here needs the Top tool plane at the origin", nm); return 0; }
    uint32_t rn = 0; uint8_t *r = sr_record(f, p, &rn);
    if (!r || rn < 0x1700) { free(r); snprintf(op->error, sizeof op->error, "no %s record (Mastercam's default op 39 is missing)", nm); return 0; }
    double D = p->tool_dia > 0 ? p->tool_dia : sr_d(r, rn, 0x350), Rc = p->tool_dia > 0 ? p->corner_r : sr_d(r, rn, 0x358);
    double tol = sr_d(r, rn, 0x1240), stock = sr_d(r, rn, 0x2e0), th = tol * 0.5, l = Rc < th ? (th - Rc) * 0.5 : 0.0;   /* FUN_18007a988 */
    int both = sr_b(r, rn, 0x1300) && sr_b(r, rn, 0x1301), mdoff = sr_b(r, rn, 0x1320), zig = sr_s(r, rn, 0x1326) == 0, climb = sr_b(r, rn, 0x1304);
    int link_climb = both && mdoff && zig ? 0 : (mdoff || climb);   /* LinkClimbMilling (FUN_18007b240) */
    const char *why = NULL;
    if (p->file_op_id > 0 && sr_s(r, rn, 0x6c) != 0x27) why = "the op is not a classic Pencil (opcode != 39)";
    else if (!(D > 0)) why = "the tool diameter must be > 0";
    else if (sr_s(r, rn, 0x1324) >= 1 && sr_d(r, rn, 0x1328) > tol) why = "Parallel passes (Number per side / Unlimited): the Constant Stepover Passes From Passes plan 0x140d is NOT DECODED";
    else if (sr_d(r, rn, 0x1340) > 1e-9) why = "Overthickness: the grown tool (FUN_18013e330) and the refined bitangent marks (FUN_180162eb0) are not ported";
    else if (fabs(Rc - D * 0.5) > 1e-9 * fmax(1, D)) why = "only a ball tool: the contact codes of flat / bull profiles and the tool type test (tool +0x10) are NOT DECODED";
    else if (stock < 0) why = "a negative stock to leave (FUN_18007a988 clears it under the job's +0x38 flag: NOT DECODED)";
    else if (sr_b(r, rn, 0x12e8)) why = "Depth limits (PosZLo / PosZHi): how the passes are cut at the Z limits is NOT DETERMINED";
    else if (sr_b(r, rn, 0x1305)) why = "the approximate start point (LinkStartX / Y) is not ported";
    else if (!both) why = "Allow negative / positive Z motion off (LinkPencilDirectionStyle 1 / 2, the contact angles 0 .. tolerance): the Pencil Linker 0x1442 is NOT DECODED";
    else if (link_climb) why = "LinkClimbMilling on (Cut method One way, or Machining direction Climb): the Pencil Linker 0x1442 is NOT DECODED";
    if (why) { snprintf(op->error, sizeof op->error, "%s: %s", nm, why); free(r); return 0; }
    HstGrid g; memset(&g, 0, sizeof g); char err[200];
    if (!c3_mesh_op(f, op, tol > 1e-9 ? tol : 0.001, &g.M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s: %s", nm, err); free(r); return 0; }
    if (!c3_containment(f, op, 0xff, &g.region, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s: %s", nm, err); free(r); hst_grid_close(&g); return 0; }
    if (g.region.n) {   /* FUN_180128390 ms l.196444-196457 -> offsetWriteTcb(position 0x12c8, distance, tolerance 0x1240) */
        double off = sr_d(r, rn, 0x12c0) + (sr_b(r, rn, 0x12b8) == 0 ? D * 0.5 : 0.0); double pos = sr_d(r, rn, 0x12c8);
        if (pos != 1.0 && fabs(off) > 1e-4) { CPaths o = cl_offset_nz(&g.region, pos == 2.0 ? off : -off, tol > 1e-9 ? tol : 0.001); cl_free(&g.region); g.region = o;
            if (!g.region.n) { snprintf(op->error, sizeof op->error, C3_TCB_FAIL); free(r); hst_grid_close(&g); return 0; } }   /* warning 0x127, no toolpath (ms l.196459-196461) */
    }
    double szs = stock + l;
    g.fp = p->top_z + p->feed_plane; if (g.fp < g.M.hi[2] + szs + p->feed_plane) g.fp = g.M.hi[2] + szs + p->feed_plane;   /* ours, as hst_grid_open2 */
    g.feed = p->feed > 0 ? p->feed : 100; g.plunge = p->plunge_feed > 0 ? p->plunge_feed : g.feed; g.lo = g.M.lo[2];
    double bt = sr_d(r, rn, 0x1338);   /* degrees in the classic record (CMsPnclPg) */
    return pc_generate(f, op, b, &g, r, D, Rc, szs, bt, 0);   /* LinkPencilDirectionStyle 0: no reversal */
}

int cam3d_pencil(const McamFile *f, CamOp *op) {
    MB b = { 0 };
    if (!(op->p.type == CAM_SFPENCIL ? gen_sfpencil : gen_pencil)(f, op, &b)) { free(b.m); return 0; }
    op->m = b.m; op->nm = b.n;
    return 1;
}
