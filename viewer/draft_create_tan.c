/* draft_create_tan.c - Mastercam 2017's TANGENT (type 6) and PERPENDICULAR (type 5) dimensions, ported from the decompile (see
 * draft_create_tan.h for the function list). Sources (mccore unless named): decomp/draft/core_calcdim.c (calc_perp_dimension 2072,
 * calc_tangent_dimension 2379, FUN_18023d590 3618, regeneration dispatch FUN_1803042b0 8716), decomp/draft/tan_core.c (calc_tangent_eps,
 * calc_tangent_rbox, DfGetTangentArc, FUN_18023f890, FUN_18023f7a0), tan_core2.c (FUN_18023ee40, FUN_18023f400, FUN_180305ca0,
 * FUN_1803056d0, rpt_from_qangle, Point2Plane), core_createport.c (FUN_18023de60, FUN_18023e0b0, find_orient_angle, df_calc_orient_ang,
 * df_angle_hv), core_create.c (determine_h_or_v, determine_hvp, df_angle_hv_rel), core_create2.c (DfCheckAngle), core_draftwf.c
 * (df_rotate_pt_about_pt), core_draft_rec.c (DimIsAssoc, aPointInitWorld, aPointSaveEntSel), core_draft_rec2.c (FUN_180023970),
 * circ_core.c (FUN_180245a40); mccad: tan_cad.c / perp_cad.c (FUN_18012ac50, FUN_18004e3b0, FUN_18004e290, FUN_180047aa0,
 * FUN_180048460). Asm where Ghidra truncated: mccore 0x18024f8c0-0x18024fbd3 (calc_tangent_dimension: +0x238 = the INPUT
 * orientation r15, calc_linear_dimension's arguments), 0x18023f7a0-0x18023f885 (FUN_18023f7a0 passes the out array for every
 * mode), 0x18023f400-0x18023f560 (FUN_18023f400: endpoint +0x80 kind '1' t 1.0, +0x68 kind '2' t 0.0), mccad 0x18012ac50-0x18012acfe
 * (FUN_18012ac50). "ours" marks glue. */
#include "draft_create_tan.h"
#include "draft_create_int.h"
#include <math.h>
#include <stdio.h>

#define BIG 1e38                                                    /* 0x47d2ced32a16a1b1 (calc_tangent_rbox's box start) */

/* ------------------------------------------------------------------------------------------------ small helpers (each cites its function) */
static void W2V(const double m[9], const double w[3], double v[3]) { for (int i = 0; i < 3; i++) v[i] = m[3 * i] * w[0] + m[3 * i + 1] * w[1] + m[3 * i + 2] * w[2]; }   /* world_to_view */
static void V2W(const double m[9], const double v[3], double w[3]) { for (int i = 0; i < 3; i++) w[i] = m[i] * v[0] + m[3 + i] * v[1] + m[6 + i] * v[2]; }       /* view_to_world */
static void APW(const uint8_t *ap, double w[3]) { memcpy(w, ap + 8, 24); }                                                                                            /* aPoint +8 world */
/* Point2Plane @180023da0 (tan_core2.c 73): world_to_view, z = DEPTH, view_to_world */
static void p2plane(const double m[9], const double w[3], double depth, double v[3], double wo[3]) { W2V(m, w, v); v[2] = depth; V2W(m, v, wo); }
/* DfCheckAngle @18023e410 (core_create2.c 3-45): +-360 / 270 / 180 / 90 snapped (sign of the input), |a| < tol -> 0 */
static double check_angle(double d) {
    double v = d;
    if (abs_same(v, 360.0, 1)) v = d < 0.0 ? -360.0 : 360.0;
    if (abs_same(v, 270.0, 1)) v = d < 0.0 ? -270.0 : 270.0;
    if (abs_same(v, 180.0, 1)) v = d < 0.0 ? -180.0 : 180.0;
    if (abs_same(v, 90.0, 1)) v = d < 0.0 ? -90.0 : 90.0;
    return fabs(v) >= ANGTOL ? v : 0.0;
}
/* df_angle_of_line @180241a00 (asm: df_angle_of_line_rads x 57.29..) of the view points A -> B */
static double angle_of_line(const double a[3], const double b[3]) { return ang_rads(a[0], a[1], b[0], b[1]) * DEG; }
double dc_angle_of_line(const double a[3], const double b[3]) { return angle_of_line(a, b); }
/* df_angle_hv @180241870 (core_createport.c 577) */
static int angle_hv(double d) {
    if (abs_same(d, 270.0, 1)) return 1;
    if (abs_same(d, 180.0, 1)) return 0;
    if (abs_same(d, 90.0, 1)) return 1;
    if (abs_same(d, 0.0, 1)) return 0;
    return -1;
}
/* df_angle_hv_rel @180241900 (core_create.c 399-440) */
int dc_angle_hv_rel(double a, double b) {
    double d = a + b;
    while (360.0 <= d) d -= 360.0;
    while (d < 0.0) d += 360.0;
    if (abs_same(d, 360.0, 1)) d = 0.0;
    if (fabs(d) < ANGTOL) d = 0.0;
    return angle_hv(d);
}
/* df_calc_orient_ang @180242d60 (core_createport.c 510-575): into (-90, 90]; ~90 -> 0 with *ORIENT = 1 */
static double calc_orient_ang(double a, int *orient) {
    *orient = 0;
    while (a <= -360.0) a += 360.0;
    while (360.0 <= a) a -= 360.0;
    if (180.0 <= a) a -= 180.0;
    if (90.0 < a) a -= 180.0;
    if (abs_same(a, 90.0, 1)) { *orient = 1; return 0.0; }
    if (fabs(a) >= ANGTOL) return check_angle(a);
    return 0.0;
}
/* find_orient_angle @180251ee0 (core_createport.c 158-180): an H / V line -> 0, else df_calc_orient_ang of its angle */
static double find_orient_angle(const double a[3], const double b[3]) {
    double d = angle_of_line(a, b); int h = angle_hv(d), o;
    if (h == 0 || h == 1) return 0.0;
    return calc_orient_ang(d, &o);
}
/* df_rotate_pt_about_pt @180244960 (core_draftwf.c 6077-6150): rotate_pt about C when |deg| >= tol, z of P kept */
void dc_tan_rot_about(const double p[3], const double c[3], double deg, double o[3]) {
    double z = p[2];
    if (fabs(deg) >= TOL) { rot(p[0], p[1], c[0], c[1], cos(deg * RAD), sin(deg * RAD), &o[0], &o[1]); }
    else { o[0] = p[0]; o[1] = p[1]; }
    o[2] = z;
}
/* pt_on_arc (systypes.dll, decomp/pocket_b6/zigzag_decomp_zz_sys.c 3-64), as viewer/draft_create_circ.c ports it */
static double ang2_(double dx, double dy) { double a = atan2(dy, dx); if (a < 0) a += TWOPI; return a; }
static int pt_on_arc(double px, double py, const DtArc *A, double tolr) {
    if (fabs(hypot(A->c[0] - px, A->c[1] - py) - A->r) > tolr) return 0;
    double a = ang2_(px - A->c[0], py - A->c[1]), lo, hi, e = 0.0;
    if (A->sweep <= 0.0) { hi = A->start; lo = A->start + A->sweep; } else { lo = A->start; hi = A->start + A->sweep; }
    if (1e-12 < A->r) e = tolr / A->r;
    while (hi + e < a) a -= TWOPI;
    while (a < lo - e) a += TWOPI;
    return !(a < lo - e || hi + e < a);
}
/* rpt_from_qangle @180245bc0 (tan_core2.c 1-70): FUN_180245a40 (circ_core.c: the quadrant points E N W S of the arc in its view,
   rotated by HVP about the centre when |HVP| > tol, each with pt_on_arc(tol)); the one of QANG (0 / 90 / 180 / 270) -> WORLD (the
   arc's view), *OK = it lies on the arc */
static void rpt_from_qangle(const DtArc *A, double hvp, int qang, double w[3], int *ok) {
    static const int dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };
    int k = qang / 90;
    double q[3] = { A->c[0] + dx[k] * A->r, A->c[1] + dy[k] * A->r, A->c[2] };
    if (!(fabs(hvp) <= TOL)) { double o[3]; dc_tan_rot_about(q, A->c, hvp, o); q[0] = o[0]; q[1] = o[1]; }
    *ok = pt_on_arc(q[0], q[1], A, TOL);
    V2W(A->vm, q, w);
}
/* the four extreme slots: out[0] max x, out[1] max y, out[2] min x, out[3] min y (strict comparisons: the first candidate wins a tie) */
static void ext_take(const double rp[3], const uint8_t *ap, double mn[2], double mx[2], uint8_t out[4][0x98]) {
    if (rp[0] > mx[0]) { if (out) memcpy(out[0], ap, 0x98); mx[0] = rp[0]; }
    if (rp[1] > mx[1]) { if (out) memcpy(out[1], ap, 0x98); mx[1] = rp[1]; }
    if (rp[0] < mn[0]) { if (out) memcpy(out[2], ap, 0x98); mn[0] = rp[0]; }
    if (rp[1] < mn[1]) { if (out) memcpy(out[3], ap, 0x98); mn[1] = rp[1]; }
}
/* FUN_18023f400 (tan_core2.c 432-556; asm 0x18023f400-0x18023f560): the candidates of arc A - the quadrant points 0 / 90 / 180 / 270
   (+ HVP; kind 'A', t = angle + HVP, only those on the arc: rpt_from_qangle) then (DAT_1813c79b0 = 0: 6 candidates) +0x68 (kind '2',
   t 0.0) and +0x80 (kind '1', t 1.0) projected to the arc's depth (Point2Plane) - each taken to A's view and rotated by -HVP about
   REF's centre (df_rotate_pt_about_pt), aPointInitWorld + aPointSaveEntSel(slot 0, A, t) */
static void arc_ext(const DtArc *A, const DtArc *ref, double hvp, double mn[2], double mx[2], uint8_t out[4][0x98]) {
    for (int k = 0; k < 6; k++) {
        double w[3], v[3], rp[3], t; int ok = 1, kind;
        if (k == 5) { p2plane(A->vm, A->ep1, A->c[2], v, w); kind = '1'; t = 1.0; }
        else if (k == 4) { p2plane(A->vm, A->ep0, A->c[2], v, w); kind = '2'; t = 0.0; }
        else { rpt_from_qangle(A, hvp, k * 90, w, &ok); kind = 'A'; t = k * 90.0 + hvp; }
        if (!ok) continue;
        W2V(A->vm, w, v);
        dc_tan_rot_about(v, ref->c, -hvp, rp);
        uint8_t ap[0x98]; dc_apoint_world(ap, w, kind); dc_apoint_ent(ap, 0, A->eptr, t);
        ext_take(rp, ap, mn, mx, out);
    }
}
/* FUN_18023ee40 (tan_core2.c 399-430): a point candidate: aPoint2Plane(REF's view, REF's depth +0xa8), rotated by -HVP about REF's
   centre; the slot gets the aPoint itself (aPointCopy) */
static void pt_ext(const uint8_t *ap, const DtArc *ref, double hvp, double mn[2], double mx[2], uint8_t out[4][0x98]) {
    double w[3], v[3], wo[3], rp[3]; APW(ap, w);
    p2plane(ref->vm, w, ref->c[2], v, wo);
    dc_tan_rot_about(v, ref->c, -hvp, rp);
    ext_take(rp, ap, mn, mx, out);
}
/* FUN_18023f7a0 (tan_core.c; asm 0x18023f7a0-0x18023f885): WHICH 0 -> arc 1; 1 -> mode 1 arc 2, mode 2 the point (slot 1), modes 3 / 4
   the line ends (slots 1, 2); every candidate measured about arc 1's centre */
static void rbox_part(int mode, const DtArc arc[2], const uint8_t slot[3][0x98], int which, double hvp, double mn[2], double mx[2], uint8_t out[4][0x98]) {
    if (which == 0) { arc_ext(&arc[0], &arc[0], hvp, mn, mx, out); return; }
    if (mode == 1) arc_ext(&arc[1], &arc[0], hvp, mn, mx, out);
    else if (mode == 2) pt_ext(slot[1], &arc[0], hvp, mn, mx, out);
    else if (mode == 3 || mode == 4) { pt_ext(slot[1], &arc[0], hvp, mn, mx, out); pt_ext(slot[2], &arc[0], hvp, mn, mx, out); }
}
/* calc_tangent_rbox @18023fc80 (tan_core.c) */
void dc_tan_rbox(int mode, const DtArc arc[2], const uint8_t slot[3][0x98], double hvp,
                 double mn1[2], double mx1[2], uint8_t out1[4][0x98], double mn2[2], double mx2[2], uint8_t out2[4][0x98],
                 double mn3[2], double mx3[2]) {
    mn1[0] = mn1[1] = BIG; mx1[0] = mx1[1] = -BIG; rbox_part(mode, arc, slot, 0, hvp, mn1, mx1, out1);
    mn2[0] = mn2[1] = BIG; mx2[0] = mx2[1] = -BIG; rbox_part(mode, arc, slot, 1, hvp, mn2, mx2, out2);
    mn3[0] = mn3[1] = BIG; mx3[0] = mx3[1] = -BIG;
    rbox_part(mode, arc, slot, 0, hvp, mn3, mx3, NULL); rbox_part(mode, arc, slot, 1, hvp, mn3, mx3, NULL);   /* the stack dummy out (local_2b8) */
}
/* calc_tangent_eps @18023f9c0 (tan_core.c 1-110) */
int dc_tan_eps(int mode, const DtArc arc[2], const uint8_t slot[3][0x98], int orient, const int sides[2], int rbox,
               uint8_t out1[4][0x98], uint8_t out2[4][0x98], uint8_t res[2][0x98], double *hvp, int assoc) {
    *hvp = check_angle(*hvp);                                      /* DfCheckAngle(&DfHvpAngle) */
    if (rbox) { double a[2], b[2], c[2], d[2], e[2], f[2]; dc_tan_rbox(mode, arc, slot, *hvp, a, b, out1, c, d, out2, e, f); }
    int i1 = (sides[0] != 0 ? 2 : 0) + (orient == 1), i2 = (sides[1] != 0 ? 2 : 0) + (orient == 1);
    if (!assoc) { double w[3]; APW(out1[i1], w); double ww[3]; V2W(arc[0].vm, w, ww); dc_apoint_world(res[0], ww, 'K'); }   /* aPointInitView(arc view,
                                                                      the slot's WORLD point as a view point, 'K') - as compiled */
    else memcpy(res[0], out1[i1], 0x98);
    if (!assoc) { double w[3]; APW(out2[i2], w); double ww[3]; V2W(arc[0].vm, w, ww); dc_apoint_world(res[1], ww, 'K'); }
    else memcpy(res[1], out2[i2], 0x98);
    double w0[3], w1[3], v0[3], v1[3], q[3];
    APW(res[0], w0); APW(res[1], w1);
    p2plane(arc[0].vm, w0, 0.0, v0, q); p2plane(arc[0].vm, w1, 0.0, v1, q);   /* aPoint2Plane(.., arc view, 0.0) */
    double la = angle_of_line(v0, v1);
    int s = dc_angle_hv_rel(la, *hvp);
    if (s == 0) { if (orient == 1) return 0; }
    else if (s == 1 && orient == 0) return 0;
    s = dc_angle_hv_rel(la, -*hvp);                                /* LAB_18023fc10 */
    if (s == 0) return orient == 1 ? 0 : 1;
    if (s == 1) return orient == 0 ? 0 : 1;
    return 1;
}
/* FUN_18012ac50 (mccad; asm 0x18012ac50-0x18012acfe) */
int dc_tan_orient_by_box(const double mn[2], const double mx[2], const double r[2], int old) {
    double x = r[0], y = r[1];
    if (x > mx[0] && (y > mx[1] || mn[1] > y)) return old;          /* corners */
    if (mn[0] > x && (y > mx[1] || mn[1] > y)) return old;
    if (x >= mn[0] && y >= mn[1] && mx[0] >= x && mx[1] >= y) return old;   /* inside */
    if (!(mn[1] > y) && !(y > mx[1])) return (mn[0] > x || x > mx[0]) ? 1 : old;   /* beside: vertical */
    return 0;                                                      /* above / below: horizontal */
}
/* FUN_18004e3b0 (mccad tan_cad.c): the distances of the cursor to entity 1's max / min edge along the orientation's axis; 180 when the
   min edge is nearer (ties: 180) */
int dc_tan_side(const double mn[2], const double mx[2], const double r[2], int orient, double *dist) {
    double d2 = orient == 0 ? r[0] - mx[0] : r[1] - mx[1], d4 = orient == 0 ? r[0] - mn[0] : r[1] - mn[1], best = BIG; int s = 0;
    if (fabs(d4) < BIG) { s = 180; best = fabs(d4); }
    if (fabs(d2) < best) { s = 0; best = fabs(d2); }
    *dist = best;
    return s;
}
/* FUN_18004e290 (mccad tan_cad.c; creation, REGEN 0) / FUN_180305ca0's DfMaintainTang branch (REGEN 1; tan_core2.c 1-230): mode 1 / 2
   the line slot 0 -> slot 1 (regeneration: arc 1's centre -> arc 2's centre / -> the point projected to DEPTH), modes 3 / 4 the line
   slot 1 -> slot 2 + 90 degrees; df_calc_orient_ang -> the angle and the orientation (1 when ~90). Other modes: 0 / 0 */
void dc_tan_maintain(int mode, const DtArc *a0, const DtArc *a1, const uint8_t slot[3][0x98], int regen, double depth, double *hvp, int *orient) {
    double p[3], q[3], w[3], x[3];
    if (mode < 1 || mode > 4) { *hvp = 0.0; *orient = 0; return; }
    if (mode < 3) {
        if (!regen) { APW(slot[0], w); p2plane(a0->vm, w, 0.0, p, x); APW(slot[1], w); p2plane(a0->vm, w, 0.0, q, x); }
        else if (mode == 1) { memcpy(p, a0->c, sizeof p); memcpy(q, a1->c, sizeof q); }
        else { memcpy(p, a0->c, sizeof p); APW(slot[1], w); p2plane(a0->vm, w, depth, q, x); }
        *hvp = angle_of_line(p, q);
    } else {
        APW(slot[1], w); p2plane(a0->vm, w, regen ? depth : 0.0, p, x); APW(slot[2], w); p2plane(a0->vm, w, regen ? depth : 0.0, q, x);
        *hvp = angle_of_line(p, q) + 90.0;
    }
    *hvp = calc_orient_ang(*hvp, orient);
}

/* FUN_180023970 (core_draft_rec2.c 149-182): the aPoint references a live entity (kinds 1 2 A C E M P R U: slot 0; I: slot 0 or 1;
   O: always) */
static int ap_assoc(const uint8_t *ap, DtEntOf ent_of, void *u) {
    uint64_t e0, e1; memcpy(&e0, ap + 0x28, 8); memcpy(&e1, ap + 0x40, 8);
    switch (ap[0]) {
    case '1': case '2': case 'A': case 'C': case 'E': case 'M': case 'P': case 'R': case 'U': return e0 && ent_of && ent_of(u, e0, NULL) != 0;
    case 'I': return (e0 && ent_of && ent_of(u, e0, NULL)) || (e1 && ent_of && ent_of(u, e1, NULL));
    case 'O': return 1;
    default: return 0;
    }
}
/* DfGetTangentArc @18023e6a0 (tan_core.c) with DimIsAssoc @1802f2720 (core_draft_rec.c 761-850: type 6 = the first 2 aPoints, 3 for
   modes > 2; +0x7ca == 'c' -> never). A resolved arc of another (parallel) view would go through map_arc_to_parallel_view: NOT PORTED
   (the record's own view is required; the arc is rebuilt then - ours, flagged by the callers' view check). The rebuild: class 4,
   eptr 0, centre = the aPoint in the record's view with the depth +0x320, r / start / sweep +0x250 / +0x260 / +0x270 (+8 for arc 2),
   view +0x328, endpoints by arc_ep_3d (the a_3d's start / end: the arc's points at start / start + sweep, formats/mcam.md "Arc views") */
void dc_tan_get_arc(const uint8_t E[0x810], int idx, const double vm[9], DtEntOf ent_of, void *u, DtArc *out) {
    if (E[0x68] < 2 && idx > 1) idx = 0;
    int assoc = 0;
    if (E[0x7ca] != 0x63) for (int i = 0; i < (E[0x68] > 2 ? 3 : 2) && !assoc; i++) assoc = ap_assoc(E + 0x70 + 0x98 * i, ent_of, u);
    uint64_t ep; memcpy(&ep, E + 0x70 + 0x98 * idx + 0x28, 8);
    DtArc A; memset(&A, 0, sizeof A);
    if (assoc && ep && ent_of && ent_of(u, ep, &A) == 4 && A.view == I16(E, 0x328)) { *out = A; return; }
    memset(out, 0, sizeof *out);
    double w[3]; APW(E + 0x70 + 0x98 * idx, w); W2V(vm, w, out->c); out->c[2] = D(E, 0x320);
    out->r = D(E, 0x250 + 8 * idx); out->start = D(E, 0x260 + 8 * idx); out->sweep = D(E, 0x270 + 8 * idx);
    out->view = I16(E, 0x328); memcpy(out->vm, vm, sizeof out->vm); out->eptr = 0;
    double p[3] = { out->c[0] + out->r * cos(out->start), out->c[1] + out->r * sin(out->start), out->c[2] };
    double q[3] = { out->c[0] + out->r * cos(out->start + out->sweep), out->c[1] + out->r * sin(out->start + out->sweep), out->c[2] };
    V2W(vm, p, out->ep0); V2W(vm, q, out->ep1);
}

/* FUN_18023e0b0 with FUN_18023de60's type-5 / type-6 branch (core_createport.c 332-412): the angle 0 -> type 6: calc_tangent_eps of the
   record (DfGetTangentArc, the record's aPoints, +0x238, sides +0x24a / +0x24b, rbox) then calc_linear_angle(the two points, arc 1's
   view, +0x238, +0x240); eps failing -> 0. Type 5: the first line's angle through df_calc_orient_ang, calc_linear_angle(the aPoints
   +0x2d0 / +0x2d1, +0x2d2) */
static double de60(const Ctx *X, const uint8_t *E, DtEntOf ent_of, void *u, int assoc) {
    const double *vm = X->in->vm;
    if (E[0x308] == 5) {
        double w0[3], w1[3], v0[3], v1[3]; APW(E + 0x70, w0); APW(E + 0x108, w1); W2V(vm, w0, v0); W2V(vm, w1, v1);
        int o; double a = calc_orient_ang(angle_of_line(v0, v1), &o);
        return dc_linear_angle(E + 0x70 + 0x98 * E[0x2d0], E + 0x70 + 0x98 * E[0x2d1], vm, E[0x2d2], a);
    }
    if (E[0x308] != 6) return 0.0;
    DtArc arc[2]; memset(arc, 0, sizeof arc);
    dc_tan_get_arc(E, 0, vm, ent_of, u, &arc[0]); if (E[0x68] == 1) dc_tan_get_arc(E, 1, vm, ent_of, u, &arc[1]);
    uint8_t slot[3][0x98], o1[4][0x98], o2[4][0x98], res[2][0x98];
    memset(o1, 0, sizeof o1); memset(o2, 0, sizeof o2);
    memcpy(slot, E + 0x70, sizeof slot);
    int sides[2] = { E[0x24a], E[0x24b] }; double h = D(E, 0x240);
    if (!dc_tan_eps(E[0x68], arc, (const uint8_t (*)[0x98])slot, E[0x238], sides, 1, o1, o2, res, &h, assoc)) return 0.0;
    return dc_linear_angle(res[0], res[1], arc[0].vm, E[0x238], D(E, 0x240));
}
static void offs(const Ctx *X, uint8_t *E, const double v[3], double ang, DtEntOf ent_of, void *u, int assoc) {
    if (fabs(ang) < ANGTOL) ang = de60(X, E, ent_of, u, assoc);
    P3 P = { v[0], v[1], v[2] };
    offsets(X, E, &P, ang);                                        /* FUN_18023e0b0 (draft_create.c; its own FUN_18023de60 call is reached only for
                                                                      |ang| < tol, where both give no rotation) */
}
static double off_len(const uint8_t *E, int orient) {             /* FUN_18023d590: |x| (H), |y| (V), hypot (parallel) */
    return orient == 0 ? fabs(D(E, 0x360)) : orient == 1 ? fabs(D(E, 0x368)) : orient == 2 ? sqrt(D(E, 0x360) * D(E, 0x360) + D(E, 0x368) * D(E, 0x368)) : 0.0;
}

/* ------------------------------------------------------------------------------------------------ calc_tangent_dimension */
DcResult dc_calc_tangent(uint8_t E[0x810], const DtTanInput *in, const DcSettings *S, const DcFonts *F) {
    DcResult R; memset(&R, 0, sizeof R); R.orient = in->orient;
    if (in->orient == 2) return R;                                 /* l.2440: param_7 == 2 -> *param_12 stays 0 */
    if (!in->view) { R.not_ported = 1; R.why = "view 0 (FUN_1802579d0's graphics-view fallback NOT PORTED)"; return R; }
    for (int i = 0; i < (in->mode == 1) + 1; i++)                  /* views_parallel / views_equal / map_arc_to_parallel_view (l.2444-2455) */
        if (in->arc[i].view != in->view) { R.not_ported = 1; R.why = "arc of another view (views_parallel / map_arc_to_parallel_view NOT PORTED)"; return R; }
    DcInput lin; memset(&lin, 0, sizeof lin);                      /* calc_linear_dimension(E, P2, P3, P4, view, &orient, 0, swapped, ok) (asm 0x18024f981-0x18024f9d8) */
    memcpy(lin.ap1, in->ap1, 0x98); memcpy(lin.ap2, in->ap2, 0x98); memcpy(lin.cursor, in->cursor, sizeof lin.cursor);
    lin.view = in->view; memcpy(lin.vm, in->vm, sizeof lin.vm); lin.orient = in->orient; lin.hvp = in->hvp; lin.value_mode = 0;
    lin.tmpl = in->tmpl; lin.ctrl = in->ctrl;
    R = dc_calc_linear(E, &lin, S, F);
    R.orient = in->orient;
    if (R.not_ported || !R.valid) return R;
    R.valid = 0;
    const DtArc *a0 = &in->arc[0], *a1 = in->mode == 1 ? &in->arc[1] : &in->arc[0];
    double w[3], z[3] = { 0, 0, 0 };
    V2W(in->vm, a0->c, w); dc_apoint_world(E + 0x70, w, a0->eptr ? 'C' : 'K'); dc_apoint_ent(E + 0x70, 0, a0->eptr, 0.0);   /* l.2460-2465 (t = local_68 = 0) */
    if (in->mode == 1) {
        V2W(in->vm, in->arc[1].c, w); dc_apoint_world(E + 0x108, w, in->arc[1].eptr ? 'C' : 'K'); dc_apoint_ent(E + 0x108, 0, in->arc[1].eptr, 0.0);
        V2W(in->vm, z, w); dc_apoint_world(E + 0x1a0, w, 'K');     /* aPointInitView(view, local_58 = 0, 'K') */
    } else if (in->mode == 2) { memcpy(E + 0x108, in->slot[1], 0x98); memcpy(E + 0x1a0, in->slot[1], 0x98); }
    else if (in->mode == 3 || in->mode == 4) { memcpy(E + 0x108, in->slot[1], 0x98); memcpy(E + 0x1a0, in->slot[2], 0x98); }
    else return R;                                                 /* l.2480: *pcVar1 = 0 */
    E[0x308] = 6; E[0x68] = (uint8_t)in->mode; E[0x238] = (uint8_t)in->orient;   /* asm 0x18024fb02-0x18024fb4d: +0x238 = the input orientation */
    PD(E, 0x240, in->hvp); E[0x248] = (uint8_t)in->maintain; E[0x249] = 1;
    E[0x24a] = (uint8_t)in->sides[0]; E[0x24b] = (uint8_t)in->sides[1];   /* the low bytes of the two shorts (0 / 0xb4) */
    PD(E, 0x250, a0->r); PD(E, 0x260, a0->start); PD(E, 0x270, a0->sweep);
    PD(E, 0x258, a1->r); PD(E, 0x268, a1->start); PD(E, 0x278, a1->sweep);
    if (!in->assoc) { R.not_ported = 1; R.why = "DfAssoc off: DfTurnOffApAssoc (FUN_1802f20a0) NOT DECODED"; return R; }
    /* FUN_18023d590 case 6 (core_calcdim.c 3618-3760): the placement (FUN_18023dad0 -> +0x310), the two points again (FUN_18023f890:
       DfGetTangentArc + calc_tangent_eps of the record with DfHvpAngle = +0x240), the offsets from each (FUN_18023e0b0, angle +0x240),
       the anchor +0x249 = the nearer (orientation +0x238: |x| / |y|; ties: 1) */
    Ctx X; memset(&X, 0, sizeof X); X.S = S; X.F = F; X.in = &lin; memcpy(X.fa, S->fa, 0x60);
    double pl[3];
    if (!placement(&X, E, pl)) return R;
    PD(E, 0x310, pl[0]); PD(E, 0x318, pl[1]); PD(E, 0x320, pl[2]);
    DtArc arc[2]; memset(arc, 0, sizeof arc);
    dc_tan_get_arc(E, 0, in->vm, in->ent_of, in->u, &arc[0]); if (E[0x68] == 1) dc_tan_get_arc(E, 1, in->vm, in->ent_of, in->u, &arc[1]);
    if (!in->ent_of) { arc[0] = in->arc[0]; arc[1] = in->arc[1]; }   /* ours: no resolver given - the input arcs (what DfGetTangentArc finds for
                                                                      associative arcs) */
    uint8_t slot[3][0x98], o1[4][0x98], o2[4][0x98], res[2][0x98]; memset(o1, 0, sizeof o1); memset(o2, 0, sizeof o2);
    memcpy(slot, E + 0x70, sizeof slot);
    int sides[2] = { E[0x24a], E[0x24b] }; double h = D(E, 0x240);
    dc_tan_eps(E[0x68], arc, (const uint8_t (*)[0x98])slot, E[0x238], sides, 1, o1, o2, res, &h, in->assoc);   /* the result flag is ignored */
    double v0[3], v1[3], w0[3], w1[3]; APW(res[0], w0); APW(res[1], w1); W2V(in->vm, w0, v0); W2V(in->vm, w1, v1);
    double ang = D(E, 0x240);
    offs(&X, E, v0, ang, in->ent_of, in->u, in->assoc); double d1 = off_len(E, E[0x238]); E[0x249] = 1;
    offs(&X, E, v1, ang, in->ent_of, in->u, in->assoc); double d2 = off_len(E, E[0x238]); E[0x249] = 2;
    if (d1 <= d2) { offs(&X, E, v0, ang, in->ent_of, in->u, in->assoc); E[0x249] = 1; }
    R.valid = 1;                                                   /* DraftIsAssoc: NOT PORTED (as calc_linear) */
    return R;
}

/* FUN_180305ca0 (tan_core2.c 1-230): DfHvpAngle +0x240 / orientation +0x238 (DfMaintainTang +0x248: dc_tan_maintain), calc_tangent_eps
   (rbox), the cursor = the anchor point (+0x249: 1 the first, else the second, in arc 1's view) + the offsets +0x360 / +0x368 / +0x370
   rotated (df_rotate_pt) by calc_linear_angle(the two points, arc 1's view, orientation, DfHvpAngle) */
static int regen_in(const uint8_t Es[0x810], const DtArc arc[2], const uint8_t slot[3][0x98], int assoc, DtTanInput *in, double cw[3]) {
    memset(in, 0, sizeof *in);
    in->arc[0] = arc[0]; in->arc[1] = arc[1];
    const double *avm = arc[0].vm;                                 /* sVar8 = arc 1's view (local_5210) */
    in->mode = Es[0x68]; in->orient = Es[0x238]; in->sides[0] = Es[0x24a]; in->sides[1] = Es[0x24b]; in->maintain = Es[0x248];
    memcpy(in->slot, slot, (size_t)((in->mode > 2) + 2) * 0x98);
    in->hvp = D(Es, 0x240);
    if (in->maintain) {
        if (in->mode < 1 || in->mode > 4) return 0;
        dc_tan_maintain(in->mode, &in->arc[0], &in->arc[1], (const uint8_t (*)[0x98])in->slot, 1, D(Es, 0x320), &in->hvp, &in->orient);
    }
    uint8_t o1[4][0x98], o2[4][0x98], res[2][0x98]; memset(o1, 0, sizeof o1); memset(o2, 0, sizeof o2);
    if (!dc_tan_eps(in->mode, in->arc, (const uint8_t (*)[0x98])in->slot, in->orient, in->sides, 1, o1, o2, res, &in->hvp, assoc)) return 0;
    double w0[3], w1[3], v0[3], v1[3]; APW(res[0], w0); APW(res[1], w1); W2V(avm, w0, v0); W2V(avm, w1, v1);
    double a = dc_linear_angle(res[0], res[1], avm, in->orient, in->hvp);
    P3 off = { D(Es, 0x360), D(Es, 0x368), D(Es, 0x370) }, ro; rot_deg(off, a, &ro);   /* df_rotate_pt */
    const double *an = Es[0x249] != 1 ? v1 : v0;
    double cv[3] = { an[0] + ro.x, an[1] + ro.y, ro.z };
    V2W(avm, cv, cw);
    memcpy(in->ap1, res[0], 0x98); memcpy(in->ap2, res[1], 0x98);
    in->view = arc[0].view; memcpy(in->vm, avm, sizeof in->vm); in->assoc = assoc;
    return 1;
}
int dc_tan_regen_cursor(const uint8_t Es[0x810], const DtArc arc[2], const uint8_t slot[3][0x98], int assoc, double cw[3]) {
    DtTanInput in; return regen_in(Es, arc, slot, assoc, &in, cw);
}
DcResult dc_regen_tangent(uint8_t Eo[0x810], const uint8_t Es[0x810], const double vm[9], DtEntOf ent_of, void *u,
                          const DcSettings *S, const DcFonts *F, int ctrl, int use_placement, int assoc) {
    DcResult R; memset(&R, 0, sizeof R);
    memcpy(Eo, Es, 0x810);
    DtArc arc[2]; memset(arc, 0, sizeof arc);
    dc_tan_get_arc(Es, 0, vm, ent_of, u, &arc[0]);
    if (Es[0x68] == 1) dc_tan_get_arc(Es, 1, vm, ent_of, u, &arc[1]);
    uint8_t slot[3][0x98]; memcpy(slot, Es + 0x70, sizeof slot);   /* the aPoints as stored (FUN_180303a80's refresh not done) */
    DtTanInput in; double cw[3];
    if (!regen_in(Es, arc, (const uint8_t (*)[0x98])slot, assoc, &in, cw)) return R;
    if (use_placement) { double cv[3] = { D(Es, 0x310), D(Es, 0x318), D(Es, 0x320) }; V2W(arc[0].vm, cv, cw); }   /* DAT_1813d8cd2 */
    memcpy(in.cursor, cw, sizeof in.cursor);
    in.ctrl = ctrl; in.ent_of = ent_of; in.u = u;
    char tmpl[0x60];
    if (Es[0x7a0]) { memcpy(tmpl, Es + 0x650, 0x58); tmpl[0x58] = 0; in.tmpl = tmpl; }   /* FUN_180310b30 on the record copy */
    return dc_calc_tangent(Eo, &in, S, F);
}

/* ------------------------------------------------------------------------------------------------ perpendicular */
/* calc_perp_dimension @18024eb60 (core_calcdim.c 2072-2125) */
DcResult dc_calc_perp(uint8_t E[0x810], const DtPerpInput *in, const DcSettings *S, const DcFonts *F) {
    DcInput lin; memset(&lin, 0, sizeof lin);                      /* calc_linear_dimension(E, P2, P3, P4, P5, param_9, 0, P10, P11) */
    memcpy(lin.ap1, in->ap1, 0x98); memcpy(lin.ap2, in->ap2, 0x98); memcpy(lin.cursor, in->cursor, sizeof lin.cursor);
    lin.view = in->view; memcpy(lin.vm, in->vm, sizeof lin.vm); lin.orient = in->orient; lin.hvp = in->hvp; lin.value_mode = 0;
    lin.tmpl = in->tmpl; lin.ctrl = in->ctrl;
    DcResult R = dc_calc_linear(E, &lin, S, F);
    if (R.not_ported || !R.valid) return R;
    uint8_t anchor = E[0x1a8];                                     /* eVar1 = param_1[0x1a8] (FUN_18023d590 case 0's anchor), read first */
    E[0x308] = 5; E[0x68] = (uint8_t)in->mode;
    memcpy(E + 0x70, in->aps[0], 0x98); memcpy(E + 0x108, in->aps[1], 0x98); memcpy(E + 0x1a0, in->aps[2], 0x98);
    if (in->mode == 1) dc_apoint_world(E + 0x238, in->cursor, 'K'); /* aPointInitWorld(+0x238, param_4, 'K') */
    else memcpy(E + 0x238, in->aps[3], 0x98);
    E[0x2d0] = (uint8_t)in->idx[0]; E[0x2d1] = (uint8_t)in->idx[1]; E[0x2d2] = (uint8_t)R.orient;   /* *param_8 / param_8[1] / *param_9 */
    PD(E, 0x2d8, in->hvp); E[0x2e0] = anchor;
    return R;
}
/* FUN_180048460 (perp_cad.c) + FUN_1800480c0 (find_orient_angle of the first line, DfCheckAngle) */
void dc_perp_setup(int mode, const uint8_t slot[4][0x98], const double vm[9], uint8_t aps[4][0x98], double *hvp) {
    static const int m1[4] = { 0, 1, 2, 2 }, m2[4] = { 0, 1, 2, 3 };
    const int *m = mode == 1 || mode == 4 ? m1 : m2;
    double p[4][3];
    for (int k = 0; k < 4; k++) { memcpy(aps[k], slot[m[k]], 0x98); double w[3]; APW(slot[m[k]], w); W2V(vm, w, p[k]); }
    double d2 = sqrt((p[2][1] - p[0][1]) * (p[2][1] - p[0][1]) + (p[2][0] - p[0][0]) * (p[2][0] - p[0][0]) + (p[2][2] - p[0][2]) * (p[2][2] - p[0][2]));
    double d3 = sqrt((p[3][1] - p[0][1]) * (p[3][1] - p[0][1]) + (p[3][0] - p[0][0]) * (p[3][0] - p[0][0]) + (p[3][2] - p[0][2]) * (p[3][2] - p[0][2]));
    if (d3 < d2) { uint8_t t[0x98]; memcpy(t, aps[2], 0x98); memcpy(aps[2], aps[3], 0x98); memcpy(aps[3], t, 0x98);
                   double q[3]; memcpy(q, p[2], sizeof q); memcpy(p[2], p[3], sizeof q); memcpy(p[3], q, sizeof q); }
    *hvp = check_angle(find_orient_angle(p[0], p[1]));
}
/* FUN_180047aa0 (perp_cad.c 36-107) */
int dc_perp_pick(int mode, const uint8_t aps[4][0x98], const double vm[9], const double tv[3], int idx[2], uint8_t ap1[0x98], uint8_t ap2[0x98]) {
    int pick[2] = { 9, 9 }, excl = 9, n = (mode != 1) + 3;
    for (int pass = 0; pass < 2; pass++) {
        double best = BIG;
        for (int s = 0; s < n; s++) {
            double w[3], v[3]; APW(aps[s], w); W2V(vm, w, v); v[2] = tv[2];   /* the candidate's z = the cursor's (FUN_18000d150(.., 2)) */
            double d = sqrt((v[0] - tv[0]) * (v[0] - tv[0]) + (v[1] - tv[1]) * (v[1] - tv[1]) + (v[2] - tv[2]) * (v[2] - tv[2]));
            if (d < best && excl != s) { pick[pass] = s; best = d; excl = pick[0]; }
        }
    }
    int changed = !(pick[0] == idx[0] && pick[1] == idx[1]);
    idx[0] = pick[0]; idx[1] = pick[1];
    if (pick[0] < 4) memcpy(ap1, aps[pick[0]], 0x98);
    if (pick[1] < 4) memcpy(ap2, aps[pick[1]], 0x98);
    return changed;
}
/* determine_hvp @180240400 (core_create.c 49-360) for param_5 = 0: the angle test (both angles < 90 -> parallel) is skipped */
int dc_determine_hvp0(const double p1v[3], const double p2v[3], const double tv[3], int prev, double hvp) {
    hvp = check_angle(hvp);
    P3 a = { p1v[0], p1v[1], p1v[2] }, b = { p2v[0], p2v[1], p2v[2] }, t = { tv[0], tv[1], tv[2] }, ra, rb, rt;
    rot_deg(a, -hvp, &ra); rot_deg(b, -hvp, &rb); rot_deg(t, -hvp, &rt);
    double P1[2] = { ra.x, ra.y }, P2[2] = { rb.x, rb.y }, T[2] = { rt.x, rt.y };
    for (int k = 0; k < 2; k++) {
        if (P2[k] <= P1[k]) { if (P2[k] < T[k] && T[k] < P1[k]) return k; }
        else if (P1[k] < T[k] && T[k] < P2[k]) return k;
    }
    return prev == 2 ? 1 : prev;
}
/* FUN_1803056d0 (tan_core2.c 231-398): two lines that intersect (l_ints, line_3d_to_2d) -> invalid (DAT_1813d8ce4 = 0x6991); DfHvpAngle
   = the first line's angle through df_calc_orient_ang; the cursor = the anchor (+0x2e0: 1 aps[+0x2d0], else aps[+0x2d1]) + the offsets
   rotated by calc_linear_angle(aps[+0x2d0], aps[+0x2d1], +0x2d2, DfHvpAngle) */
int dc_perp_regen_cursor(const uint8_t Es[0x810], const uint8_t aps[4][0x98], const double vm[9], double *hvp, double cw[3]) {
    int i0 = Es[0x2d0], i1 = Es[0x2d1];
    if (i0 > 3 || i1 > 3) return 0;
    double v[4][3]; for (int k = 0; k < 4; k++) { double w[3]; APW(aps[k], w); W2V(vm, w, v[k]); }
    if (Es[0x68] > 1) {
        double L1[4] = { v[0][0], v[0][1], v[1][0], v[1][1] }, L2[4] = { v[2][0], v[2][1], v[3][0], v[3][1] }, ip[2];
        if (l_ints(L1, L2, ip)) return 0;
    }
    int o; *hvp = calc_orient_ang(angle_of_line(v[0], v[1]), &o);
    double a = dc_linear_angle(aps[i0], aps[i1], vm, Es[0x2d2], *hvp);
    P3 off = { D(Es, 0x360), D(Es, 0x368), D(Es, 0x370) }, ro; rot_deg(off, a, &ro);
    const double *an = Es[0x2e0] != 1 ? v[i1] : v[i0];
    double cv[3] = { an[0] + ro.x, an[1] + ro.y, ro.z };
    V2W(vm, cv, cw);
    return 1;
}
DcResult dc_regen_perp(uint8_t Eo[0x810], const uint8_t Es[0x810], const double vm[9], const DcSettings *S, const DcFonts *F, int ctrl, int use_placement) {
    DcResult R; memset(&R, 0, sizeof R);
    memcpy(Eo, Es, 0x810);
    DtPerpInput in; memset(&in, 0, sizeof in);
    in.mode = Es[0x68]; in.view = I16(Es, 0x328); memcpy(in.vm, vm, sizeof in.vm);
    memcpy(in.aps, Es + 0x70, 4 * 0x98);                           /* (the 4th of a mode-1 record is not read by calc_perp_dimension) */
    in.idx[0] = Es[0x2d0]; in.idx[1] = Es[0x2d1]; in.orient = Es[0x2d2];
    if (!dc_perp_regen_cursor(Es, (const uint8_t (*)[0x98])in.aps, vm, &in.hvp, in.cursor)) return R;
    if (use_placement) { double cv[3] = { D(Es, 0x310), D(Es, 0x318), D(Es, 0x320) }; V2W(vm, cv, in.cursor); }   /* DAT_1813d8cd2 */
    memcpy(in.ap1, in.aps[in.idx[0]], 0x98); memcpy(in.ap2, in.aps[in.idx[1]], 0x98);
    in.ctrl = ctrl;
    char tmpl[0x60];
    if (Es[0x7a0]) { memcpy(tmpl, Es + 0x650, 0x58); tmpl[0x58] = 0; in.tmpl = tmpl; }
    return dc_calc_perp(Eo, &in, S, F);
}
