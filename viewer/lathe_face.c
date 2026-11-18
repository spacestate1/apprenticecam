/* lathe_face.c - Mastercam 2017 Lathe Face (opcode 66): CLFaceTp::bnci_lface 0x180045c80 and what it calls, for the cases
 * decoded in ../formats/lathe.md section 6. Sources: ~/mastercam-re/work/lathe/dump/lrough_finish_face.c / mclathe_all.c
 * (mclathe.dll) and work/lathe/dump/core_lathe.c (mccore.dll); every rule below names its function. Oracle:
 * tools/tests/lathe_face_test.sh (millturn.mcam_8 op 1).
 *
 * Coordinates: the 2D contour frame of the op's view (op +0x6d0, Top here): x = lathe Z, y = lathe X (radius); the NCI
 * point = (X, 0, Z) through the op's tool plane (XY_to_lathe, measured: section 6.6).
 *
 * Face from the stock (op +0xb68, formats/lathe.md 7.3): GetAllGeometry's stock branch (SetMaxMinPtsFromOp, GetOpXStockLimits,
 * GetFaceBoundaryFromStock) on the stock polygon lathe_stock.c reads, and the passes' stock clipping (GetStockStartEndHeight).
 * Refused (NOT DECODED / NOT PORTED, with a message): the stock on the right spindle / bottom turret (polygons 0x1c..0x1e),
 * entry / exit vectors (op +0x888), corner strategies (op +0x1358: RoughStrategy1..5 / FinishStrategy1..3), cutter
 * compensation in control / wear / reverse wear (op +0x568 1..3), surface-speed feeds (op +0x32e), the finish spindle
 * override (op +0x13a9), the return to reference point (op +0x292), planes other than the measured ones. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lathe_face.h"

#define OPB 0x68                                 /* op = ent + 0x68 (bnci_lface: ent local_2988, op local_2920) */
static double od(const LfIn *in, uint32_t o) { double v; memcpy(&v, in->rec + OPB + o, 8); return v; }
static int ob(const LfIn *in, uint32_t o) { return in->rec[OPB + o]; }
static int os16(const LfIn *in, uint32_t o) { int16_t v; memcpy(&v, in->rec + OPB + o, 2); return v; }
static double rd(const uint8_t *r, uint32_t o) { double v; memcpy(&v, r + o, 8); return v; }
static int r16(const uint8_t *r, uint32_t o) { int16_t v; memcpy(&v, r + o, 2); return v; }

#define PRM 0x11c0                               /* prm_lathe_face = op +0x11c0 (GenerateRough / GenerateFinish ppVar1) */
#define TL 0x4b8                                 /* ltool_rec = op +0x4b8 (bnci_lface local_2468) */

typedef struct { double x, y; } P2;

/* the CLNciFmt state the face writer touches */
typedef struct {
    LfMove *m; int n, cap;
    P2 prev; int have_prev;                      /* prev_nc_pt (1e38 = none) */
    int last_tc;                                 /* fmt +0x292e in 1000..1002: the last record written is the tool change */
    double tol;
    int plunge;                                  /* CLNciFmt::GetPlungeDirection 0x1800bcb30, case 0x42 */
} W;

static void push(W *w, int rapid, P2 p, double feed, int ctf) {
    if (w->n == w->cap) { w->cap = w->cap ? 2 * w->cap : 64; w->m = realloc(w->m, (size_t)w->cap * sizeof *w->m); }
    w->m[w->n++] = (LfMove){ rapid, p.y, p.x, rapid ? 0 : feed, ctf };
}
/* CLNciFmt::gr_lin_move 0x1800bf810: a coordinate within tol / 10 of the previous point's is set to it; a move shorter than tol
   is not written unless the last record is the tool change; written: prev_nc_pt = the point, fmt +0x292e = (gcode != 0) */
static void gr_lin(W *w, int g01, P2 p, double feed, int ctf) {
    if (w->have_prev) {
        if (fabs(p.x - w->prev.x) < fabs(w->tol / 10)) p.x = w->prev.x;
        if (fabs(p.y - w->prev.y) < fabs(w->tol / 10)) p.y = w->prev.y;
        if (!w->last_tc && hypot(p.x - w->prev.x, p.y - w->prev.y) < w->tol) return;
    }
    push(w, !g01, p, feed, ctf);
    w->prev = p; w->have_prev = 1; w->last_tc = 0;
}
/* CLNciFmt::wr_lin_move 0x1800c27c0: NCI gcode G -> G0 when G is 0, -1, -3, -4 (else G1 at FEED); not written when shorter than
   tol (unless after the tool change); G not 1 / -1 and both coordinates changing by tol or more: two moves (dogleg), the first
   to (prev.x, p.y) when G is -3 / -5 or the plunge direction says so (GetPlungeDirection: 2 -> p.x <= prev.x, 3 -> prev.x < p.x),
   else to (p.x, prev.y). fmt +0x293c (prev_nc_pt = the home position, add_rapid_from_prev_pt) skips the dogleg: the home
   position (get_lhome_pos_sys) is NOT DECODED, taken as never equal (section 6.5). */
static void wr_lin(W *w, int g, P2 p, double feed, int ctf) {
    if (w->have_prev && !w->last_tc && hypot(p.x - w->prev.x, p.y - w->prev.y) < w->tol) return;
    int g01 = (g == -2 || g < -4 || g > 0) ? 1 : 0;
    if (!(((unsigned)(g + 1) & 0xfffd) == 0) && w->have_prev &&
        !(fabs(w->prev.y - p.y) < fabs(w->tol)) && !(fabs(w->prev.x - p.x) < fabs(w->tol))) {
        int b;
        if (((unsigned)(g + 5) & 0xfffd) == 0) b = 1;
        else if (((unsigned)(g + 6) & 0xfffd) == 0) b = 0;
        else b = w->plunge == 2 ? p.x <= w->prev.x : w->prev.x < p.x;
        P2 mid = b ? (P2){ w->prev.x, p.y } : (P2){ p.x, w->prev.y };
        gr_lin(w, g01, mid, feed, ctf & 1);
        gr_lin(w, g01, p, feed, 0);
        return;
    }
    gr_lin(w, g01, p, feed, ctf);
}

/* the face's frame (CFaceTpGeometry +0xa8 .. +0xc0, CLFaceTp +0x98 / +0xa0) */
typedef struct { double a8, b0, b8, c0; double s98, sa0; } G;

/* CLFaceTp::GetToolEdgeComp 0x180044440 + TestFrontBackEdge 0x180044a10 (front / back edge of the insert on the pass from P3
   to P4; ltool_rec +0 orientation, +0x40 / +0x48 front, +0x50 / +0x58 back, +0x61 back edge defined) */
static void tool_edge(const LfIn *in, P2 p3, P2 p4, int *front, int *back) {
    const uint8_t *t = in->rec + OPB + TL; int type = (uint16_t)r16(t, 0), ccdir = ob(in, 0x56a);
    double tol = in->tol, t40 = rd(t, 0x40), t48 = rd(t, 0x48), t50 = rd(t, 0x50), t58 = rd(t, 0x58);
    double dot = (int16_t)type < 5 ? 0.0 : (p4.y - p3.y) * t48;   /* (x - x) * t40 + dy * (type < 5 ? 0 : t48) */
    int b5;
    if (tol * tol <= fabs(dot)) b5 = dot < 0;
    else { b5 = ccdir != 1; if (type < 8 && ((0xb4u >> type) & 1)) b5 = !b5; }
    int c2;
    if (type < 9 && ((0x14au >> type) & 1)) c2 = !((ccdir == 1 && !b5) || (ccdir == 0 && b5));
    else c2 = !((ccdir == 1 && b5) || (ccdir == 0 && !b5));
    P2 q = { p4.x, p3.y }, a = b5 ? q : p4, c = b5 ? p4 : q;    /* TestFrontBackEdge(param_2 = a, param_3 = c) */
    double dx = c2 ? c.x - a.x : a.x - c.x, dy = c2 ? c.y - a.y : a.y - c.y;
    if (fabs(dx) < tol * 0.2) dx = 0;
    if (fabs(dy) < tol * 0.2) dy = 0;
    int flip = type < 8 && ((0xb4u >> type) & 1);
    double v = dy * t40 - dx * t48;
    *front = fabs(v) >= 1e-12 ? (flip ? v <= 1e-12 : v > 1e-12) : 0;
    if (!t[0x61]) { *back = 0; return; }
    v = dy * t50 - dx * t58;
    *back = fabs(v) >= 1e-12 ? (flip ? v <= 1e-12 : v > 1e-12) : 0;
    if (*front && *back) *front = *back = 0;
}

/* CLToolComp::lthcc0 0x1800bc0b0 -> LatheCutterComp0 0x1800b74e0 ('F'): coincident records deleted (1 left: fails), then mccore
   ccomp_pock_from_ctour 0x1805d5e50 by |r| + min(stock X, stock Z) to side 'L' (op +0x56a 0) / 'R' (1); a distance <= 1e-12
   leaves the contour.
   CORRELATED (decision L-3; matched to millturn op 1, not traced): ccomp_pock itself (the mccore pocket offset) is not decoded;
   the 2-record face line is moved sideways by the distance to that side of its direction, ends not extended (lathe.md 6.4).
   Trace next: mccore ccomp_pock (called by ccomp_pock_from_ctour 0x1805d5e50 after ctour_to_pock_bound). */
static int comp_line(const LfIn *in, P2 *r1, P2 *r2, double d, char *err, size_t errn) {
    double dx = r2->x - r1->x, dy = r2->y - r1->y, L = hypot(dx, dy);
    if (L <= 1e-12) { snprintf(err, errn, "Lathe Face: the face line has no length (LatheCutterComp0: one record left)"); return 0; }
    if (fabs(d) <= 1e-12) return 1;
    double s = ob(in, 0x56a) == 1 ? -1 : 1;      /* 'L': left of the direction (-dy, dx); 'R': right */
    double nx = -dy / L * s, ny = dx / L * s;
    r1->x += nx * d; r1->y += ny * d; r2->x += nx * d; r2->y += ny * d;
    return 1;
}

/* one pass at OFFSET from the face (FinishStrategy0 0x180203090 / RoughStrategy0 0x180204e60 loop body, CompFinish 0x180202f60 /
   CompRough 0x1802049d0, OutputOnePass 0x180044760, then the retract) */
static int one_pass(W *w, const LfIn *in, const G *g, double offset, int rough, int front, int back, double feed, double ds, double de, char *err, size_t errn) {
    const uint8_t *t = in->rec + OPB + TL; int cc = os16(in, 0x568);
    double r = rd(t, 8);
    P2 r1 = { g->b8, g->b0 }, r2 = { g->b8, g->c0 };
    if (back || front) { double e = rd(t, back ? 0x50 : 0x40); r1.x += e; r2.x += e; }
    if (cc != 4) {                               /* CompRough / CompFinish: op +0x568 == 4 -> no compensation */
        double rr = cc == 1 ? 0.0 : r, d;
        if (rough && ob(in, 0x56b) && !(offset <= in->tol)) d = offset + rr;   /* CompRough: lthcc0(r 0, offset) then lthcc0(r, 0):
                                                                                     one line, the same side - the sum */
        else d = rr + offset;
        if (!comp_line(in, &r1, &r2, d, err, errn)) return 0;
    }
    /* the pass ends: start + (-sa0 * r) [twice in control], then + (-sa0 * lead-in amount prm +0x30); end + sa0 * overcut prm +0x48,
       + the edge's ltool_rec +0x48 / +0x58 */
    /* FinishStrategy0 / RoughStrategy0 after CompFinish / CompRough: the stock deltas (GetStockStartEndHeight: stock start / end y -
       the contour's first / last y; 0 without stock) added to the first / last record's y */
    r1.y += ds; r2.y += de;
    r1.y += -g->sa0 * r; if (cc == 1) r1.y += -g->sa0 * r;
    r1.y += -g->sa0 * od(in, PRM + 0x30);
    r2.y -= -g->sa0 * od(in, PRM + 0x48);
    if (back) r2.y += rd(t, 0x58); else if (front) r2.y += rd(t, 0x48);
    /* OutputOnePass: computer / wear / reverse wear comp (op +0x568 & ~3 == 0, != 1): the contour moved by -(ltool_rec +0x30, +0x38)
       (the tool's reference point), plus the view origin op +0x720 (0: checked) */
    if ((cc & 0xfffc) == 0 && cc != 1) { r1.x -= rd(t, 0x30); r1.y -= rd(t, 0x38); r2.x -= rd(t, 0x30); r2.y -= rd(t, 0x38); }
    /* CLEntryExit::enter_contour 0x18003f7d0 with op +0x888 == 0: add_rapid_from_prev_pt, then wr_lin_move(gcode fmt +0x293e or
       NCI_GCODE 0 - both 0 here) to the first record.
       CORRELATED (decision L-3; matched to millturn op 1, not traced): the op's first move goes straight to the pass start - the
       rapid from the home position that add_rapid_from_prev_pt can write after lwr_sof (fmt +0x2940) is NOT DECODED (lathe.md 6.5).
       Trace next: CLNciFmt::add_rapid_from_prev_pt 0x1800bdc90's home branch (get_lhome_pos_sys, add_rapid_pts,
       TOOLPATHOPCLASS::WriteHomePosPts). */
    wr_lin(w, 0, r1, 0, 0);
    /* OutputOnePass: records 1..n with gcode 1 at op +0x310; flags 1 for record 2, 2 for the last */
    wr_lin(w, 1, r1, feed, 0);
    wr_lin(w, 1, r2, feed, 1 | 2);
    /* exit_contour 0x180040080 with op +0x888 == 0 writes nothing. The retract: |prm +0x38| > tol -> wr_lin_move(this +0xb0 =
       -3 rapid when prm +0x40, else -5 feed at op +0x310) to (prev.x - s98 * prm +0x38, prev.y) */
    double ra = od(in, PRM + 0x38);
    if (fabs(ra) > in->tol) {
        P2 q = { w->prev.x - g->s98 * ra, w->prev.y };
        wr_lin(w, ob(in, PRM + 0x40) ? -3 : -5, q, feed, 0);
    }
    return 1;
}

/* CFaceTpGeometry::GetOpStockFace 0x1801e9e10 (TopTurret = the op's turret byte op +0x540: CORRELATED, lathe.md 6.3 / 6.7a) */
static int top_turret(const LfIn *in) { int t540 = ob(in, 0x540); return ((unsigned)(t540 - 1) & 0xfc) == 0 && t540 != 2; }
static double stock_face(const LfIn *in) {
    int top = top_turret(in), ccdir = ob(in, 0x56a), away = ob(in, 0x1210);
    return !top ? (ccdir != 0 ? (away ? 1.0 : -1.0) : (away ? -1.0 : 1.0)) : (ccdir == 0 ? (away ? 1.0 : -1.0) : (away ? -1.0 : 1.0));
}
/* GetOpXStockLimits 0x1801e9e70 (polygon 0x1b..0x1e): stock face 1 -> [MinX, MaxX - 0.002], else [MinX + 0.002, MaxX] (0.002 as
   written, in either unit) */
static void x_limits(const LfIn *in, double *lo, double *hi) {
    if (stock_face(in) == 1.0) { *lo = in->stock->minx; *hi = in->stock->maxx - 0.002; }
    else { *lo = in->stock->minx + 0.002; *hi = in->stock->maxx; }
}
/* the CFaceTpGeometry fields of the stock branch: +0x88 / +0x90 (point A: the finish x, y), +0x98 / +0xa0 (point B: the stock face) */
typedef struct { double g88, g90, g98, ga0; } SG;
/* CFaceTpGeometry::SetMaxMinPtsFromOp 0x1801eac20 (polygon 0x1b) */
static void set_max_min_pts(const LfIn *in, SG *s) {
    double lo, hi; x_limits(in, &lo, &hi);
    if (in->npts < 2) {                           /* no face points: +0x88 kept when inside the limits, else the far (op +0x56a 1) / near end */
        if (ob(in, 0x56a) == 1) { if (s->g88 < lo || hi < s->g88) s->g88 = lo; }
        else if (s->g88 < lo || hi < s->g88) s->g88 = hi;
        s->g98 = hi;
        s->g90 = in->stock->miny; s->ga0 = in->stock->maxy;     /* (idx - 0x1b & 0xfd) == 0: MinY / MaxY */
    } else {                                      /* the first two points (tp_get_all), - op +0x720 (0: checked), Top view */
        double p1x = in->pts[0][0], p1y = in->pts[0][1], p2x = in->pts[1][0], p2y = in->pts[1][1];
        int two = p2x <= p1x;
        if (stock_face(in) == -1.0) { s->g88 = two ? p2x : p1x; s->g98 = two ? p1x : p2x; }
        else { s->g88 = two ? p1x : p2x; s->g98 = two ? p2x : p1x; }
        if (fabs(p2y) <= fabs(p1y)) { s->g90 = p2y; s->ga0 = p1y; } else { s->g90 = p1y; s->ga0 = p2y; }
    }
    if (s->g88 < lo) s->g88 = lo;
    if (hi < s->g88) s->g88 = hi;
}
/* CLFaceProp::InitZFinEd 0x180041d90 as the Face page's OnInitDialog 0x1800426d0 calls it (param 1e30) on the page's fresh
   CFaceTpGeometry (ctor 0x1801e8450: +0x88 = 0): SetMaxMinPtsFromOp (no op points: +0x88 kept inside GetOpXStockLimits, else the
   near / far end by op +0x56a), the limits through wcsX_to_latheZ (the measured plane: lathe Z = wcs x, lathe_face_gen checks it),
   the value snapped to an end within 1e-7 (disassembly 0x180041ec7..0x180041f05; the rounded value only widens the max). The edit
   range the MCReal gets (vfunc +0x348) is [*lo - tol, *hi + tol]. 0 = no stock polygon */
int lathe_face_z_init(const LfIn *in, double *z, double *lo, double *hi) {
    if (!in->stock || in->stock->n <= 0) return 0;
    SG s = { 0, 0, 1e30, 1e30 };
    LfIn q = *in; q.npts = 0;
    set_max_min_pts(&q, &s);
    double a, b; x_limits(in, &a, &b);
    if (b < a) { double t = a; a = b; b = t; }
    double v = s.g88;
    if (fabs(v - a) < 1e-7) v = a;
    if (fabs(v - b) < 1e-7) v = b;
    *z = v; *lo = a; *hi = b;
    return 1;
}
/* GetAllGeometry 0x1801e8990, stock branch (op +0xb68 1 and GetOpStockBoundary != 0xff) -> GetFaceBoundaryFromStock 0x1801e9410: the
   two face point entities it makes, A = (+0x88, +0x90) and B = (+0x98, +0xa0), view_to_world(Top) + op +0x720 (0) */
static int stock_points(const LfIn *in, double pts[2][3], char *err, size_t errn) {
    SG s = { 0, 0, 1e30, 1e30 };                 /* the CFaceTpGeometry ctor 0x1801e8450: +0x88 = +0x90 = 0, +0x98 = +0xa0 = 1e30 */
    double tol = in->tol, lo, hi;
    if (in->dialog_ok) {                          /* CLFaceProp::OnOK: geom +0x88 = latheZ_to_wcsX(Finish Z); GetFaceBoundaryFromStock(op,
                                                     PointsDefined, 0): +0x88 clamped to GetOpXStockLimits, no SetMaxMinPtsFromOp */
        set_max_min_pts(in, &s);                  /* (InitZFinEd / SetRestCtrls ran it on the page's geom before) */
        s.g88 = in->finish_x;
        x_limits(in, &lo, &hi);
        if (s.g88 < lo) s.g88 = lo;
        if (hi < s.g88) s.g88 = hi;
    } else {                                      /* GetAllGeometry: SetMaxMinPtsFromOp, then GetFaceBoundaryFromStock(op, 1, 1) runs it again */
        set_max_min_pts(in, &s);
        set_max_min_pts(in, &s);
    }
    int top = top_turret(in), ok1, ok2;
    s.g98 = stock_face(in) == -1.0 ? in->stock->maxx : in->stock->minx;   /* MaxX / MinX (raw) */
    double a = s.g98 <= s.g88 ? s.g98 : s.g88, b = s.g98 <= s.g88 ? s.g88 : s.g98;
    if (top) { ok1 = ls_get_max_y(in->stock, a, b, 1e30, tol, &s.ga0); ok2 = ls_get_min_y(in->stock, a, b, -1e30, tol, &s.g90); }
    else { ok1 = ls_get_min_y(in->stock, a, b, -1e30, tol, &s.ga0); ok2 = ls_get_max_y(in->stock, a, b, 1e30, tol, &s.g90); }
    if (!ok1 || !ok2) { snprintf(err, errn, "Lathe Face: the stock has no edge at the face (x %g .. %g: BOUNDARYCLASS::GetMaxY / GetMinY find nothing)", a, b); return 0; }
    pts[0][0] = s.g88; pts[0][1] = s.g90; pts[0][2] = 0;
    pts[1][0] = s.g98; pts[1][1] = s.ga0; pts[1][2] = 0;
    return 1;
}
/* CLFaceTp::GetStockStartEndHeight 0x180044240: op +0xb68 and the stock polygon (this[0x93]); over x in [X, XPREV]: YStatus 1 -> start
   = the greatest y, end = the least (swapped with cut away from the centre, prm +0x50); YStatus -1 the other way; 0 -> nothing.
   0 = no change (the start not found) */
static int stock_start_end(const LfIn *in, double x, double xprev, double *st, double *en) {
    if (!ob(in, 0xb68) || !in->stock) return 0;
    int ys = ls_ystatus(in->stock, in->tol), away = ob(in, PRM + 0x50), mxfirst;
    double lo = x < xprev ? x : xprev, hi = x < xprev ? xprev : x, a = 1e30, b = 1e30;
    if (ys == 1) mxfirst = !away; else if (ys == -1) mxfirst = away; else return 0;
    if (mxfirst) { ls_get_max_y(in->stock, lo, hi, 1e30, in->tol, &a); ls_get_min_y(in->stock, lo, hi, -1e30, in->tol, &b); }
    else { ls_get_min_y(in->stock, lo, hi, -1e30, in->tol, &a); ls_get_max_y(in->stock, lo, hi, 1e30, in->tol, &b); }
    if (fabs(fabs(a) - 1e30) < fabs(in->tol)) return 0;
    *st = a; *en = b;
    return 1;
}

int lathe_face_gen(const LfIn *in, LfMove **out, int *nout, char *err, size_t errn) {
    *out = NULL; *nout = 0;
    const uint8_t *R = in->rec;
    if (!R || in->rn != 0x2938) { snprintf(err, errn, "Lathe Face: the operation record is not the 2017 layout (0x2938 bytes); other layouts are NOT DECODED"); return 0; }
    if (os16(in, 4) != 0x42) { snprintf(err, errn, "Lathe Face: not a Lathe Face record (opcode %d)", os16(in, 4)); return 0; }
    double tol = in->tol;
    /* ---- refusals: what bnci_lface reaches that is not decoded here */
    /* GetAllGeometry: the stock branch when op +0xb68 and a stock polygon (GetOpStockBoundary 0x1801e9d60: the left spindle -
       OpActiveSpindle taken as 0, CORRELATED lathe.md 7.3 - and the top turret: 0x1b when YStatus(0x1b) == 1, else the mirrored 0x1c).
       Without it the op's own points (GetFaceBoundaryFromStock(op, 1, 1) returns at once when op +0xb68 is 0; op +0x88 is not read) */
    int from_stock = ob(in, 0xb68) && in->stock && in->stock->n > 0;
    if (from_stock && !top_turret(in)) { snprintf(err, errn, "Lathe Face: the face from the stock with a bottom-turret tool (the mirrored stock polygon 0x1c: BOUNDARYCLASS::Mirror) is NOT PORTED"); return 0; }
    if (from_stock && ls_ystatus(in->stock, tol) != 1) { snprintf(err, errn, "Lathe Face: the stock is not above the centre line (YStatus %d: the mirrored polygon 0x1c) - NOT PORTED", ls_ystatus(in->stock, tol)); return 0; }
    int cc = os16(in, 0x568);
    if (cc != 0 && cc != 4) { snprintf(err, errn, "Lathe Face: compensation type %d (in control / wear / reverse wear: G41 / G42 codes) is NOT PORTED", cc); return 0; }
    if (ob(in, 0x888)) { snprintf(err, errn, "Lathe Face: entry / exit vectors (op +0x888: CLEntryExit rotate_tp_vec / calc_arc_pts / CalcEEPoints) are NOT DECODED"); return 0; }
    {   /* GenerateRough / GenerateFinish / MakeContour: the corner strategies when op +0x1358 and not (type 0, size < tol) and not
           (type 1 and (angle out of (tol, pi/2 - tol) rad or length < tol)) */
        int s = os16(in, 0x135a); double a = od(in, 0x1378) * 0.01745329251994329;
        int plain = !ob(in, 0x1358) || (s == 0 && od(in, 0x1360) < tol) || (s == 1 && (a < tol || 1.570796326794896 - tol < a || od(in, 5000) < tol));
        if (!plain) { snprintf(err, errn, "Lathe Face: the corner option (op +0x1358, type %d: RoughStrategy1..5 / FinishStrategy1..3) is NOT DECODED", s); return 0; }
    }
    if (ob(in, 0x32e)) { snprintf(err, errn, "Lathe Face: surface-speed feed (op +0x32e: feedrate_from_surface_finish) is NOT DECODED"); return 0; }
    if (ob(in, PRM + 0x10) && ob(in, 0x139a) && ob(in, 0x13a8) == 0x53) { snprintf(err, errn, "Lathe Face: the finish feed override in surface feed (op +0x13a8 'S') is NOT DECODED"); return 0; }
    if (ob(in, PRM + 0x10) && ob(in, 0x13a9)) { snprintf(err, errn, "Lathe Face: the finish spindle speed override (op +0x13a9: wr_dwell_ss record) is NOT PORTED"); return 0; }
    if (ob(in, 0x292)) { snprintf(err, errn, "Lathe Face: the return to the reference point (op +0x292: SendLToolHome calc_lathe_ref_pt) is NOT DECODED"); return 0; }
    /* planes: the op's view (op +0x6d0, ent 0x738) Top at the origin (world_to_view = identity; op +0x720 = 0), the tool plane
       (op +0x570, ent 0x5d8) the one measured (rows (0,1,0) (0,0,1) (1,0,0) at the origin: XY_to_lathe NOT DECODED for others) */
    {
        static const double TP[9] = { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, ID[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        int okv = r16(R, 0x738 + 2) == 1;
        for (int k = 0; k < 9; k++) { if (rd(R, 0x738 + 8 + 8 * k) != ID[k]) okv = 0; if (rd(R, 0x5d8 + 8 + 8 * k) != TP[k]) okv = 0; }
        for (int k = 0; k < 3; k++) { if (rd(R, 0x738 + 0x50 + 8 * k) != 0 || rd(R, 0x5d8 + 0x50 + 8 * k) != 0) okv = 0; }
        if (!okv) { snprintf(err, errn, "Lathe Face: the op's view / tool plane are not the ones measured (Top view, lathe tool plane at the origin); XY_to_lathe for others is NOT DECODED"); return 0; }
    }
    int rough = ob(in, PRM), fin = ob(in, PRM + 0x10);
    if (!rough && !fin) { snprintf(err, errn, "Lathe Face: neither rough nor finish is on (CLFaceTp::InitVariables assert)"); return 0; }
    if (in->npts > 2) { snprintf(err, errn, "Lathe Face: the op has %d face points (GetPtsFromOp deletes the extra ones; not ported)", in->npts); return 0; }
    double sp[2][3];
    if (in->nmade) *in->nmade = 0;
    if (from_stock) {                             /* GetFaceBoundaryFromStock: the two face points made from the stock (they replace the op's) */
        if (!stock_points(in, sp, err, errn)) return 0;
        if (in->made) { memcpy(in->made, sp, sizeof sp); if (in->nmade) *in->nmade = 2; }
    } else {
        if (in->npts < 2) {
            if (ob(in, 0xb68)) snprintf(err, errn, "Lathe Face: the face from the stock needs a left-spindle stock (machine group / earlier ops: none) or two face points (the op has %d)", in->npts);
            else snprintf(err, errn, "Lathe Face: the op has %d face point(s); GetAllGeometry needs 2 (FaceGeometry.cpp assert)", in->npts);
            return 0;
        }
        memcpy(sp, in->pts, sizeof sp);
    }

    /* ---- CFaceTpGeometry::GetAllGeometry 0x1801e8990: the two points (- op +0x720, world_to_view: Top) */
    G g; double x1 = sp[0][0], y1 = sp[0][1], x2 = sp[1][0], y2 = sp[1][1];
    g.a8 = x2 < x1 ? x1 : x2;                    /* +0xa8 the larger x, +0xb8 the smaller */
    g.b8 = x1 <= x2 ? x1 : x2;
    /* GetOpStockFace 0x1801e9e10: TopTurret (pclsLTool +0x761 = is_turret_top, mccore 0x1800344b0: 1, 3, 4).
       CORRELATED (decision L-3; matched to millturn op 1, not traced): the face side comes from the op's own turret byte op +0x540
       (ltool_rec +0x88) taken for pclsLTool's +0x761 (lathe.md 6.3). Trace next: what loads the op's tool into pclsLTool before
       bnci_lface - FixLatheToolOrigin (mccore, bnci_lface's first call) and the callers of LATHETOOLCLASS::SetActiveTurret
       0x180155790. */
    int t540 = ob(in, 0x540), top = ((unsigned)(t540 - 1) & 0xfc) == 0 && t540 != 2;
    int ccdir = ob(in, 0x56a), away = ob(in, 0x1210);
    double face = !top ? (ccdir != 0 ? (away ? 1.0 : -1.0) : (away ? -1.0 : 1.0)) : (ccdir == 0 ? (away ? 1.0 : -1.0) : (away ? -1.0 : 1.0));
    if (face == 1.0) { double s = g.a8; g.a8 = g.b8; g.b8 = s; }
    g.b0 = fabs(y2) < fabs(y1) ? y1 : y2;        /* +0xb0 the larger |y|, +0xc0 the smaller; swapped with "cut away from centre" */
    g.c0 = fabs(y1) <= fabs(y2) ? y1 : y2;
    if (away) { double s = g.b0; g.b0 = g.c0; g.c0 = s; }
    /* InitVariables 0x1800445e0: the signs */
    if (g.c0 + g.b0 <= 0) { g.s98 = ccdir == 0 ? (away ? -1 : 1) : (away ? 1 : -1); g.sa0 = away ? -1 : 1; }
    else { g.s98 = ccdir == 0 ? (away ? 1 : -1) : (away ? -1 : 1); g.sa0 = away ? 1 : -1; }

    W w = { 0 }; w.tol = tol; w.last_tc = 1;       /* lwr_sof wrote the tool change */
    w.plunge = face != -1.0 ? 3 : 2;              /* GetPlungeDirection case 0x42 */
    int front = 0, back = 0, npass = 0;
    tool_edge(in, (P2){ g.b8, g.b0 }, (P2){ g.b8, g.c0 }, &front, &back);
    double feed = od(in, 0x310);

    /* ---- GenerateRough 0x180204c50 -> RoughStrategy0 0x180204e60 */
    if (rough) {
        double fin_allow = od(in, PRM + 0x28);
        if (fin) fin_allow += (double)(int16_t)os16(in, PRM + 0x20) * od(in, PRM + 0x18);
        double span = fabs(g.b8 - g.a8) - fin_allow;
        if (!(span < -tol)) {
            if (span < tol) span = 0;
            double step = od(in, PRM + 8), dstep; int n;
            if (step * 0.9 <= span) {
                double q = span / step; q = q >= 0 ? floor(q) : ceil(q);
                n = (int16_t)(int)q;
                if (tol < span - fabs(step) * n) n++;
                if (n < 1) n = 1;
                dstep = span / n;
            } else { dstep = 0; n = 1; }
            double xprev = 0;
            for (int k = 1; k <= n; k++) {
                /* RoughStrategy0: the stock probe x = ((n - k) * step - tol + stock to leave) * -s98 + the contour's x (the finish allowance
                   not included); the interval [this x, the previous pass's x] (the first: itself; disassembly 0x180205114 / 0x180205183) */
                double xk = ((n - k) * dstep - tol + od(in, PRM + 0x28)) * -g.s98 + g.b8, st, en, ds = 0, de = 0;
                if (k == 1) xprev = xk;
                if (stock_start_end(in, xk, xprev, &st, &en)) { ds = st - g.b0; de = en - g.c0; }
                xprev = xk;
                if (!one_pass(&w, in, &g, (n - k) * dstep + fin_allow, 1, front, back, feed, ds, de, err, errn)) { free(w.m); return 0; }
                npass++;
            }
        }
    }
    /* ---- GenerateFinish 0x1802046b0 -> FinishStrategy0 0x180203090 (op +0x139a: the finish feed op +0x13a0, negated for 'R') */
    if (fin) {
        double ff = feed;
        if (ob(in, 0x139a)) ff = ob(in, 0x13a8) == 0x52 ? -od(in, 0x13a0) : od(in, 0x13a0);
        int n = (int16_t)os16(in, PRM + 0x20);
        double xprev = 0;
        for (int k = 1; k <= n; k++) {
            /* FinishStrategy0: the probe x = ((n - k) * finish step + stock to leave - tol) * -s98 + the contour's x; interval [x, the
               previous pass's x] (disassembly 0x180203247 / 0x1802032b6) */
            double xk = ((n - k) * od(in, PRM + 0x18) + od(in, PRM + 0x28) - tol) * -g.s98 + g.b8, st, en, ds = 0, de = 0;
            if (k == 1) xprev = xk;
            if (stock_start_end(in, xk, xprev, &st, &en)) { ds = st - g.b0; de = en - g.c0; }
            xprev = xk;
            if (!one_pass(&w, in, &g, (n - k) * od(in, PRM + 0x18) + od(in, PRM + 0x28), 0, front, back, ff, ds, de, err, errn)) { free(w.m); return 0; }
            npass++;
        }
    }
    if (!npass) { free(w.m); snprintf(err, errn, "Lathe Face: no pass to cut (bnci_lface: warning 0x1281)"); return 0; }
    /* SendLToolHome 0x180114f90: op +0x292 0 (checked) - its components branch (ComponentsDefined -> WriteHomePosPts) is NOT
       DECODED; millturn op 1 ends at the retract */
    *out = w.m; *nout = w.n;
    return 1;
}
