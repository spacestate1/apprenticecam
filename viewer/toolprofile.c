/* toolprofile - see toolprofile.h. Port of tlcore.dll TlProfileBuilderParametric (../formats/tool_profiles.md).
 * Section numbers (sN) refer to that spec; addresses are tlcore.dll thunk -> body. */
#include <math.h>
#include <string.h>
#include "toolprofile.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TAU (2 * M_PI)
#define DEG (M_PI / 180)

typedef struct { double x, y; } Pt;
typedef struct { const TpParams *p; TpSeg *o; int max, n, err; double tol; } K;   /* builder state */

/* s0.4 comparisons (IsEqualTo 0x18013fef0, IsGreaterThan 0x18013ff10, IsLessThan 0x18013ff40,
   IsGreaterThanOrEqTo 0x18013ff20, IsLessThanOrEqTo 0x18013ff60), tol = params+0x10 */
#define EQ(a, b) (fabs((a) - (b)) < fabs(k->tol))
#define GT(a, b) ((a) - (b) > k->tol)
#define LT(a, b) ((b) - (a) > k->tol)
#define GE(a, b) ((a) - (b) > -k->tol)
#define LE(a, b) ((b) - (a) > -k->tol)
#define FAIL(c) do { if (!k->err) k->err = (c); return; } while (0)

static Pt pt(double x, double y) { Pt p = { x, y }; return p; }
static double tand(double v) { return tan(v * DEG); }               /* "tan°" */
static double ang(Pt p, Pt c) { double a = atan2(p.y - c.y, p.x - c.x); return a < 0 ? a + TAU : a; }   /* θ: [0, 2π) */

/* s0.2 GetLineSegment 0x18013fe60 / GetArcSegment 0x18013fd40 (end points by arc_ep_3d, identity view) */
static TpSeg *add(K *k) {
    if (k->err) return NULL;
    if (k->n >= k->max) { k->err = TP_ERR_OVERFLOW; return NULL; }
    TpSeg *s = &k->o[k->n++]; memset(s, 0, sizeof *s); return s;
}
static void line(K *k, double x0, double y0, double x1, double y1) {
    TpSeg *s = add(k); if (!s) return;
    s->type = 2; s->x0 = x0; s->y0 = y0; s->x1 = x1; s->y1 = y1;
}
static void arc(K *k, double cx, double cy, double r, double a0, double sw) {
    TpSeg *s = add(k); if (!s) return;
    s->type = 4; s->cx = cx; s->cy = cy; s->r = r; s->a0 = a0; s->sw = sw;
    s->x0 = cx + r * cos(a0); s->y0 = cy + r * sin(a0); s->x1 = cx + r * cos(a0 + sw); s->y1 = cy + r * sin(a0 + sw);
}
static Pt seg_end(const TpSeg *s) { return pt(s->x1, s->y1); }

/* ---------- geometry helpers used by the builders (l_offset_tol, l_ints_tol, line / arc intersections) ----------
   The spec names these tlcore helpers but not their internals; here: lines are infinite except where the spec checks
   segment parameters in [0, 1] (checked with the s0.4 tolerant tests), and a tangent line within tol of the circle
   counts as touching it. */
static void offset_toward(Pt *p, Pt *q, Pt toward, double r) {         /* l_offset_tol: shift by r to the side of TOWARD */
    double dx = q->x - p->x, dy = q->y - p->y, l = hypot(dx, dy); if (l <= 0) return;
    double nx = -dy / l, ny = dx / l;
    if ((toward.x - p->x) * nx + (toward.y - p->y) * ny < 0) { nx = -nx; ny = -ny; }
    p->x += r * nx; p->y += r * ny; q->x += r * nx; q->y += r * ny;
}
static int ints(Pt p1, Pt q1, Pt p2, Pt q2, double *s, double *u, Pt *x) {   /* l_ints_tol: line-line, parameters s, u */
    double d1x = q1.x - p1.x, d1y = q1.y - p1.y, d2x = q2.x - p2.x, d2y = q2.y - p2.y, den = d1x * d2y - d1y * d2x;
    if (den == 0) return 0;                                             /* parallel */
    double ex = p2.x - p1.x, ey = p2.y - p1.y;
    *s = (ex * d2y - ey * d2x) / den; *u = (ex * d1y - ey * d1x) / den;
    *x = pt(p1.x + *s * d1x, p1.y + *s * d1y); return 1;
}
#define IN01(v) (GE((v), 0) && LE((v), 1))
static Pt foot(Pt c, Pt p, Pt q) {                                      /* foot of the perpendicular from C on line PQ */
    double dx = q.x - p.x, dy = q.y - p.y, t = ((c.x - p.x) * dx + (c.y - p.y) * dy) / (dx * dx + dy * dy);
    return pt(p.x + t * dx, p.y + t * dy);
}
/* line PQ with arc (C, r, a0, sw > 0): the intersection nearest REF; 0 = none */
static int line_arc(K *k, Pt p, Pt q, Pt c, double r, double a0, double sw, Pt ref, Pt *out) {
    Pt f = foot(c, p, q); double dist = hypot(f.x - c.x, f.y - c.y);
    if (dist > r + fabs(k->tol)) return 0;
    double h = dist >= r ? 0 : sqrt(r * r - dist * dist), l = hypot(q.x - p.x, q.y - p.y);
    double ux = (q.x - p.x) / l, uy = (q.y - p.y) / l, best = -1;
    for (int i = -1; i <= 1; i += 2) {
        Pt x = pt(f.x + i * h * ux, f.y + i * h * uy);
        double d = fmod(ang(x, c) - a0, TAU), e = fabs(k->tol) / r; if (d < 0) d += TAU;
        if (!(d <= sw + e || d >= TAU - e)) continue;
        double dr = hypot(x.x - ref.x, x.y - ref.y);
        if (best < 0 || dr < best) { best = dr; *out = x; }
    }
    return best >= 0;
}
/* line PQ with the segment (x, 0)-(x, oal): the vertical side used by s3.2 and s3.10 (P2) */
static int line_vert(K *k, Pt p, Pt q, double x, double oal, Pt *out) {
    double s, u; if (!ints(pt(x, 0), pt(x, oal), p, q, &s, &u, out)) return 0;
    return IN01(s);
}

/* ---------- s2 makeShankAboveShoulder 0x1800062f8 -> 0x180148e90 ---------- */
static void shank_straight(K *k, double x, double y) {                 /* 0x180002c0c -> 0x180149690 */
    const TpParams *p = k->p; double Ra = p->da / 2;
    if (!EQ(x, Ra)) line(k, x, y, Ra, y);
    line(k, Ra, y, Ra, p->oal);
    line(k, Ra, p->oal, 0, p->oal);
}
static void shank_tapered(K *k, double x, double y) {                  /* 0x18000904d -> 0x180149ca0 */
    const TpParams *p = k->p; double Ra = p->da / 2, Rsh = p->dsh / 2;
    if (LE(Ra, x) || LE(p->tl, 0)) { shank_straight(k, x, y); return; }
    double xt = Ra, yt = y + p->tl;
    if (LT(p->oal, y + p->tl)) { xt = tan(atan((Ra - Rsh) / p->tl)) * (p->oal - p->sl) + Rsh; yt = p->oal; }   /* SL / Rsh, not x / y: literal */
    line(k, x, y, xt, yt);
    if (GT(p->oal, yt)) line(k, xt, yt, xt, p->oal);
    line(k, xt, p->oal, 0, p->oal);
}
static void shank_reduced(K *k, double x, double y) {                  /* 0x180007f86 -> 0x180148800 */
    const TpParams *p = k->p; double rn = p->dn / 2, a = p->dsh / 2 - rn, b = p->da / 2 - rn, oal = p->oal;
    if (LE(rn, 0)) FAIL(TP_IllegalReducedNeckRadius);
    if (LT(a, 0) || LT(b, 0)) FAIL(TP_ReducedNeckLargerThanShoulderShank);
    if (LE(p->tl - a - b, 0)) FAIL(TP_NoNeckLength);
    double x1 = x, y1 = y;
    if (GT(oal, y) && GT(a, 0)) {                                       /* 1. 45° step in */
        double d = LE(oal, y + a) ? oal - y : a;
        line(k, x, y, x - d, y + d); x1 = x - d; y1 = y + d;
    }
    double y2 = y1;
    if (GT(oal, y1)) {                                                  /* 2. neck */
        double L = p->tl - b - a; if (LE(oal, y1 + L)) L = oal - y1;
        line(k, x1, y1, x1, y1 + L); y2 = y1 + L;
    }
    double x3 = x1, y3 = y2;
    if (GT(oal, y2) && GT(b, 0)) {                                      /* 3. 45° step out */
        double d = LE(oal, y2 + b) ? oal - y2 : b;
        line(k, x1, y2, x1 + d, y2 + d); x3 = x1 + d; y3 = y2 + d;
    }
    if (GT(oal, y3)) line(k, x3, y3, x3, oal);                          /* 4 */
    line(k, x3, oal, 0, oal);                                           /* 5 */
}
static void shank(K *k, double x, double y) {
    if (k->err) return;
    if (EQ(y, k->p->oal)) { line(k, x, k->p->oal, 0, k->p->oal); return; }
    switch (k->p->shank_type) {
    case 0: shank_straight(k, x, y); break;
    case 1: shank_tapered(k, x, y); break;
    case 2: shank_reduced(k, x, y); break;
    default: break;                                                     /* any other value appends nothing */
    }
}

/* ---------- s3 cutter outlines ---------- */
/* s3.1 makeDrill 0x18000a4bb -> 0x180145200 (A = included angle; s3.2 passes its A') */
static void make_drill(K *k, double A) {
    const TpParams *p = k->p; double R = p->d / 2, h = R * tand(90 - A / 2);
    if (LT(h, 0) || GT(h, p->sl)) FAIL(TP_TipToolDiameterIntersectionError);
    line(k, 0, 0, R, h);
    line(k, R, h, R, p->sl);
    shank(k, R, p->sl);
}

/* s3.2 makeCounterSinkOrTapOrNullTool 0x18000e070 -> 0x180144000 */
static void make_csink_tap_null(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, r1 = p->r1, A = p->a;
    if (p->type == 0 && EQ(A, 0)) A = 180;
    if (LE(r1, 0)) { make_drill(k, A); return; }
    double t = tand(90 - A / 2);
    if (t < 1e-12) FAIL(TP_IllegalCornerRadius);
    if (GT(r1, R)) FAIL(TP_CornerRadiusTooLargeForRadius);
    Pt a = pt(-r1 / t, 0), b = pt(0, r1);                               /* cone y = t·x + r1 (through the centre) */
    offset_toward(&a, &b, pt(R, 0), r1);
    Pt P1, P2;
    if (!line_arc(k, a, b, pt(0, r1), r1, 3 * M_PI / 2, M_PI, pt(R, 0), &P1)) FAIL(TP_CheckCornerRadius);
    if (!line_vert(k, a, b, R, p->oal, &P2)) FAIL(TP_CheckCornerRadiusArborDiameter);   /* single hit: "nearest (R, r1)" */
    arc(k, 0, r1, r1, 3 * M_PI / 2, ang(P1, pt(0, r1)) - 3 * M_PI / 2);
    line(k, P1.x, P1.y, P2.x, P2.y);
    line(k, P2.x, P2.y, P2.x, p->sl);
    shank(k, P2.x, p->sl);
}

/* s3.3 makeCenterDrill 0x18000db93 -> 0x180142ec0 */
static void make_center_drill(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, rp = p->dp / 2, Lp = p->lp;
    double h1 = rp * tand(90 - p->a / 2), h2 = tand(90 - p->b / 2) * (R - rp) + Lp;
    if (LE(rp, 0)) FAIL(TP_IllegalToolDiameter);
    if (LE(Lp, 0)) FAIL(TP_IllegalToolLength);
    if (GE(h1, Lp)) FAIL(TP_TipAngleTooSteep);
    if (GT(h2, p->sl)) FAIL(TP_ShoulderAngleTooSteep);
    line(k, 0, 0, rp, h1);
    line(k, rp, h1, rp, Lp);
    line(k, rp, Lp, R, h2);
    line(k, R, h2, R, p->sl);
    shank(k, R, p->sl);
}

/* s3.4 makeReamer 0x18000c42d -> 0x1801484b0 */
static void make_reamer(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, c1 = p->c1;
    if (!EQ(p->da / 2, R) && LT(p->sl, R / 4)) FAIL(TP_ShoulderLengthTooShortForDiameter);
    if (GT(c1, R)) FAIL(TP_ChamferDistanceTooLargeForDiameter);
    line(k, 0, 0, R - c1, 0);
    if (GT(c1, 0)) line(k, R - c1, 0, R, c1);
    line(k, R, c1, R, p->sl);
    shank(k, R, p->sl);
}

/* s3.5 makeCounterBore 0x1800051f0 -> 0x180143db0 (pilot not read) */
static void make_counterbore(K *k) {
    const TpParams *p = k->p; double R = p->d / 2;
    if (LE(p->fl, 0)) FAIL(TP_IllegalFluteLength);
    line(k, 0, 0, R, 0);
    line(k, R, 0, R, p->sl);
    shank(k, R, p->sl);
}

/* flat-endmill tail from (R, y0) (s3.6 steps 3-4, reused by s3.7 ball and s3.8 bull) */
static void flat_tail(K *k, double R, double y0) {
    const TpParams *p = k->p; double Rsh = p->dsh / 2;
    if (EQ(R, Rsh)) { line(k, R, y0, R, p->sl); shank(k, R, p->sl); return; }
    line(k, R, y0, R, p->fl);
    line(k, R, p->fl, Rsh, p->fl);
    if (!EQ(p->fl, p->sl)) { line(k, Rsh, p->fl, Rsh, p->sl); shank(k, Rsh, p->sl); }
    else shank(k, Rsh, p->fl);
}

/* s3.6 makeFlatEndMill 0x18000cfa9 -> 0x1801472e0 (r1 not used) */
static void make_flat(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, c1 = p->c1;
    if (LE(p->fl, 0)) FAIL(TP_IllegalFluteLength);
    if (GT(c1, R)) FAIL(TP_ChamferDistanceTooLargeForDiameter);
    if (EQ(p->dsh / 2, 0)) FAIL(TP_ShoulderDiameterCantBeZero);
    line(k, 0, 0, R - c1, 0);
    if (GT(c1, 0)) line(k, R - c1, 0, R, c1);
    flat_tail(k, R, c1);
}

/* s3.7 makeBallEndMill 0x18000c450 -> 0x1801406d0 (ball radius = R, not r1) */
static void make_ball(K *k) {
    const TpParams *p = k->p; double R = p->d / 2;
    if (LT(p->sl, p->r1)) FAIL(TP_ShoulderLengthCornerRadError);
    if (EQ(p->dsh / 2, 0)) FAIL(TP_ShoulderDiameterCantBeZero);
    arc(k, 0, R, R, 3 * M_PI / 2, M_PI / 2);
    flat_tail(k, R, R);
}

/* s3.8 makeBullEndMill 0x180003d00 -> 0x180142130 */
static void make_bull(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, r1 = p->r1;
    if (GT(r1, R)) FAIL(TP_CornerRadiusTooLargeForRadius);
    if (EQ(p->dsh / 2, 0)) FAIL(TP_ShoulderDiameterCantBeZero);
    if (LT(r1, R)) line(k, 0, 0, R - r1, 0);
    arc(k, R - r1, r1, r1, 3 * M_PI / 2, M_PI / 2);
    flat_tail(k, R, r1);
}

/* s3.9 makeChamferMill 0x180005f0b -> 0x180143370 (A = angle per side) */
static void make_chamfer(K *k) {
    const TpParams *p = k->p; double Rb = fmax(p->d, p->dt) / 2, Rt = fmin(p->d, p->dt) / 2, r1 = p->r1;
    double h = tand(90 - p->a) * (Rb - Rt);
    if (GE(Rt, Rb)) FAIL(TP_TipDiameterExceedsToolDiameter);
    if (LT(h, 0) || GT(h, p->sl)) FAIL(TP_TipToolDiameterIntersectionError);
    if (LT(r1, 0)) FAIL(TP_IllegalCornerRadius);
    if (EQ(r1, 0)) {
        if (GT(Rt, 0)) line(k, 0, 0, Rt, 0);
        line(k, Rt, 0, Rb, h);
    } else {
        Pt b0 = pt(0, 0), b1 = pt(Rb, 0), f0 = pt(Rt, 0), f1 = pt(Rb, h), C; double s, u;
        offset_toward(&b0, &b1, pt(Rb, r1), r1);
        offset_toward(&f0, &f1, pt(r1, 0), r1);
        if (!ints(b0, b1, f0, f1, &s, &u, &C) || !IN01(s) || !IN01(u) ||
            !GE(f0.x, 0) || !GE(f0.y, 0) || !GE(f1.x, 0) || !GE(f1.y, 0)) FAIL(TP_CheckCornerRadius);
        Pt T = foot(C, pt(Rt, 0), pt(Rb, h));                           /* circle (C, r1) touches the flank */
        line(k, 0, 0, C.x, 0);
        arc(k, C.x, C.y, r1, 3 * M_PI / 2, ang(T, C) - 3 * M_PI / 2);
        line(k, T.x, T.y, Rb, h);
    }
    if (LT(h, p->sl)) line(k, Rb, h, Rb, p->sl);
    shank(k, Rb, p->sl);
}

/* s3.10 makeFaceMill 0x1800020d1 -> 0x1801454a0 */
static void make_face(K *k) {
    const TpParams *p = k->p; double Rb = fmax(p->d, p->dt) / 2, Rt = fmin(p->d, p->dt) / 2, r1 = p->r1, A = p->a;
    int ct = p->corner_type; double h, d4;
    if (GE(A, 90) || EQ(A, 0)) { h = 0; d4 = Rb; }
    else { h = tand(90 - A) * (Rb - Rt); d4 = h / 4; }
    if (GT(Rt, Rb)) FAIL(TP_TipDiameterExceedsToolDiameter);
    if (LT(h, 0) || GT(h, p->sl)) FAIL(TP_TipToolDiameterIntersectionError);
    if (LT(r1, 0) || GT(r1, p->fl)) FAIL(TP_IllegalCornerRadius);
    if (EQ(Rb, Rt) && !EQ(A, 0)) FAIL(TP_TipToolDiameterIntersectionError);
    if (EQ(A, 0) && LT(p->sl, r1)) FAIL(TP_TipToolDiameterIntersectionError);
    if (EQ(A, 0) || (ct == 3 && EQ(Rb - Rt, r1))) {                     /* path F1 */
        if (GT(r1, 0)) {
            line(k, 0, 0, Rt, 0);
            arc(k, Rt, r1, r1, 3 * M_PI / 2, M_PI / 2);
            if (!EQ(Rb, Rt) && !EQ(Rt + r1, Rb)) line(k, Rt + r1, r1, Rb, r1);
            if (GT(p->sl, r1)) line(k, Rb, r1, Rb, p->sl);
        } else {
            line(k, 0, 0, Rb, 0);
            line(k, Rb, 0, Rb, p->sl);
        }
        shank(k, Rb, p->sl);
        return;
    }
    if (ct == 3) FAIL(TP_CheckCornerRadius);                            /* ct = 3 and Rb - Rt != r1 */
    double t = tand(90 - A), s, u; Pt X;                                /* path F2 */
    if (!ints(pt(0, d4), pt(Rt, d4), pt(Rt, 0), pt(0, t * Rt), &s, &u, &X) || !IN01(s) || !IN01(u)) FAIL(TP_CheckTaperAngle);
    if (LE(r1, 0)) {
        line(k, 0, d4, X.x, d4);
        if (!EQ(Rt, Rb)) { line(k, X.x, d4, Rt, 0); line(k, Rt, 0, Rb, h); }
        if (LT(h, p->sl)) line(k, Rb, h, Rb, p->sl);
        shank(k, Rb, p->sl);
        return;
    }
    double D = Rb - Rt; Pt a = pt(Rt - D, r1 - t * D), b = pt(Rb, r1 + t * D), P1, P2;
    offset_toward(&a, &b, pt(Rb, 0), r1);
    if (!line_arc(k, a, b, pt(Rt, r1), r1, 3 * M_PI / 2, M_PI, pt(Rb, r1), &P1) || GE(P1.x, Rb)) FAIL(TP_CheckCornerRadius);
    if (!line_vert(k, a, b, Rb, p->oal, &P2)) FAIL(TP_CheckCornerRadiusArborDiameter);
    line(k, 0, d4, X.x, d4);
    line(k, X.x, d4, Rt, 0);
    arc(k, Rt, r1, r1, 3 * M_PI / 2, ang(P1, pt(Rt, r1)) - 3 * M_PI / 2);
    line(k, P1.x, P1.y, P2.x, P2.y);
    line(k, P2.x, P2.y, P2.x, p->sl);
    shank(k, Rb, p->sl);
}

/* s3.11 makeSlotMill 0x180007ccf -> 0x180149080 */
static void make_slot(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, FL = p->fl, r1 = p->r1, r2 = p->r2, c1 = p->c1, c2 = p->c2;
    double e1 = GT(r1, 0) ? r1 : c1, e2 = GT(r2, 0) ? r2 : c2;
    if (LT(r1, 0)) FAIL(TP_IllegalCornerRadius);
    if (GT(e1, FL / 2) || GT(e2, FL / 2) || LT(FL, e1 + e2)) FAIL(TP_CornerRadiusOrFluteLengthError);
    if (GT(p->da / 2, R - e2)) FAIL(TP_ArborDiameterTooLarge);
    line(k, 0, 0, R - e1, 0);
    if (GT(c1, 0)) line(k, R - c1, 0, R, c1);                           /* wins over r1 > 0: literal */
    else if (GT(r1, 0)) arc(k, R - r1, r1, r1, 3 * M_PI / 2, M_PI / 2);
    if (!(EQ(e1, FL / 2) && EQ(e2, FL / 2))) line(k, R, e1, R, FL - e2);
    if (GT(c2, 0)) line(k, R, FL - c2, R - c2, FL);
    else if (GT(r2, 0)) arc(k, R - r2, FL - r2, r2, 0, M_PI / 2);
    shank(k, R - e2, FL);                                               /* from FL, not SL */
}

/* s3.12 makeRadiusMill 0x180001951 -> 0x1801481b0 */
static void make_radius_mill(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, r1 = p->r1;
    if (LE(r1, 0)) FAIL(TP_IllegalCornerRadius);
    if (GE(r1, p->sl)) FAIL(TP_ShoulderLengthCornerRadError);
    if (GT(R, 0)) line(k, 0, 0, R, 0);
    arc(k, R + r1, 0, r1, M_PI, -M_PI / 2);                             /* concave quarter, clockwise */
    line(k, R + r1, r1, R + r1, p->sl);
    shank(k, R + r1, p->sl);
}

/* s3.13 makeDoveTailMill 0x18000939f -> 0x180144760 */
static void make_dove(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, Ra = p->da / 2, r1 = p->r1, FL = p->fl, A = p->a;
    double tA = tand(A), xFL = R - tA * FL;
    if (LT(r1, 0)) FAIL(TP_IllegalCornerRadius);
    if (GT(r1, FL)) FAIL(TP_CornerRadiusOrFluteLengthError);
    if (GT(r1, R)) FAIL(TP_CornerRadiusTooLargeForRadius);
    if (GT(Ra, R)) FAIL(TP_ArborRadiusExceedsToolRadius);
    if (EQ(R, Ra)) FAIL(TP_ToolDiameterEqualsArborDiameter);
    if (GT(A, 89)) FAIL(TP_CheckTaperAngle);
    if (!GT(r1, 0)) {
        if (LE(xFL, 0)) FAIL(TP_TaperAngleOrFluteLengthError);
        line(k, 0, 0, R, 0);
        line(k, R, 0, xFL, FL);
        if (GT(p->sl, FL)) line(k, xFL, FL, xFL, p->sl);
        shank(k, xFL, p->sl);
        return;
    }
#ifndef TP_PARTLY_DECODED
    /* r1 > 0: the spec decodes it from stack-aliased locals and does not identify the lines of the error-20 check
       intersection with y = hA (s3.13, s6 "partly decoded") */
    FAIL(TP_ERR_NOT_DECODED);
#else
    /* steps as written in s3.13; the error-20 check intersection (lines not identified) is NOT applied */
    double hA = tand(90 - A) * (R - Ra), cx = R - r1, sw;
    Pt P0 = pt(cx, r1), q = pt(Ra - R + cx, hA + r1);
    offset_toward(&P0, &q, pt(R, 2 * r1), r1);
    int clipped = GT(P0.y, FL);
    if (clipped) {
        double th = FL - r1 < k->tol ? 0 : asin((FL - r1) / r1);
        P0 = pt(cx + r1 * cos(th), FL); sw = th + M_PI / 2;
    } else sw = ang(P0, pt(cx, r1)) - 3 * M_PI / 2 + TAU;
    double xp = P0.x - (FL - P0.y) * tA;
    if (LE(xp, 0)) FAIL(TP_TaperAngleOrFluteLengthError);
    line(k, 0, 0, cx, 0);
    arc(k, cx, r1, r1, 3 * M_PI / 2, sw);
    if (!clipped) line(k, P0.x, P0.y, xp, FL);
    if (LT(FL, p->sl)) line(k, xp, FL, xp, p->sl);
    shank(k, xp, p->sl);
#endif
}

/* s3.14 makeTaperMill 0x18000a001 -> 0x180149910 */
static void make_taper(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, r1 = p->r1, A = p->a, xt;
    if (LT(r1, 0)) FAIL(TP_IllegalCornerRadius);
    if (GT(R, r1)) line(k, 0, 0, R - r1, 0);
    if (GT(r1, 0)) {
        arc(k, R - r1, r1, r1, 3 * M_PI / 2, (90 - A) * DEG);
        if (k->err) return;
        Pt E = seg_end(&k->o[k->n - 1]);
        xt = tand(A) * (p->sl - E.y) + E.x;
        line(k, E.x, E.y, xt, p->sl);
    } else {
        xt = tand(A) * p->sl + R;
        line(k, R, 0, xt, p->sl);
    }
    shank(k, xt, p->sl);
}

/* s3.15 makeLolliPopMill 0x180001712 -> 0x1801477a0 */
static void make_lollipop(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, Rsh = p->dsh / 2, Ra = p->da / 2;
    if (LT(p->r1, 0)) FAIL(TP_IllegalCornerRadius);
    if (p->shank_type == 0 || LE(p->tl, 0) || EQ(Rsh, Ra)) { if (GT(Ra, R)) FAIL(TP_ArborRadiusExceedsToolRadius); }
    else if (EQ(Ra, Rsh)) FAIL(TP_ShoulderDiameterLargerThanArbor);     /* unreachable (spec), kept literal */
    if (LT(Ra, Rsh)) FAIL(TP_ShoulderDiameterLargerThanArbor);
    if (EQ(Rsh, 0)) FAIL(TP_ShoulderDiameterCantBeZero);
    if (LT(R, Rsh)) FAIL(TP_ShoulderDiameterLargerThanToolDiameter);
    double h = R + sqrt(R * R - Rsh * Rsh);
    if (LT(p->sl, h)) FAIL(TP_ShoulderLengthTooShortForDiameter);
    arc(k, 0, R, R, 3 * M_PI / 2, acos(Rsh / R) + M_PI / 2);
    if (GT(p->sl, h)) line(k, Rsh, h, Rsh, p->sl);
    shank(k, Rsh, p->sl);
}

/* s3.16 makeFeedMill 0x1800015c8 -> 0x1801466e0 (A1 = angle1, A2 = angle2 [C], Rel = angle3 [B]) */
static void make_feed(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, Rsh = p->dsh / 2, Rt = p->dt / 2, r1 = p->r1, r2 = p->r2;
    double A1 = p->a, A2 = p->c, Rel = p->b, SL = p->sl;
    if (GT(Rel, 0) && LE(R, Rsh)) FAIL(TP_ReliefAngleNonZeroWithInvalidDiameters);
    if (GT(Rel, 90) || LT(Rel, 0)) FAIL(TP_InvalidReliefTaperAngle);
    if (LE(R, Rt)) FAIL(TP_TipDiameterExceedsToolDiameter);
    if (GT(A1, 90)) FAIL(TP_InvalidLowerTaperAngle);
    if (GE(A2, 90) || LT(A2, 0)) FAIL(TP_InvalidUpperTaperAngle);
    if (LT(A1, A2)) FAIL(TP_InvalidLowerTaperAngle);
    if (EQ(Rsh, 0)) FAIL(TP_ShoulderDiameterCantBeZero);
    double a1 = (90 - A1) * DEG, a2 = (90 - A2) * DEG, x, y;
    line(k, 0, 0, Rt, 0);                                               /* 1 */
    Pt P = pt(Rt, 0);
    if (GT(r1, 0)) {                                                    /* 2. lower radius */
        arc(k, Rt - sin(a1) * r1, cos(a1) * r1, r1, a1 + 3 * M_PI / 2, a2 - a1);
        if (k->err) return;
        P = seg_end(&k->o[k->n - 1]);
        if (LT(R, P.x)) FAIL(TP_LowerRadiusTooLarge);
    }
    if (GT(r2, 0)) {                                                    /* 3. upper radius */
        double s0 = a2 + 3 * M_PI / 2;
        double cx = R - r2, cy = ((cos(s0) * r2 + R - r2) - P.x) * tan(a2) + P.y - sin(s0) * r2;
        Pt S = pt(cx + r2 * cos(s0), cy + r2 * sin(s0));
        if (LT(S.x, P.x)) FAIL(TP_UpperRadiusTooLarge);
        if (!(EQ(S.x, P.x) && EQ(S.y, P.y))) {
            double dx = S.x - P.x, dy = S.y - P.y;                      /* checked before the line is appended */
            if (!(fabs(dx) > k->tol && fabs(dy) > k->tol && fabs(a2 - atan(dy / dx)) < k->tol)) FAIL(TP_UpperRadiusTooLarge);
            line(k, P.x, P.y, S.x, S.y);
        }
        arc(k, cx, cy, r2, s0, (Rel * DEG + TAU) - s0);
        if (k->err) return;
        Pt E = seg_end(&k->o[k->n - 1]); x = E.x; y = E.y;
    } else {
        x = R; y = tan(a2) * (R - P.x) + P.y;
        line(k, P.x, P.y, x, y);
    }
    if (GT(y, SL)) FAIL(TP_ShoulderLengthTooShortForDiameter);          /* 4 */
    if (EQ(Rel, 90)) {                                                  /* 5. relief */
        line(k, x, y, Rsh, y); line(k, Rsh, y, Rsh, SL); shank(k, Rsh, SL);
    } else if (EQ(Rel, 0)) {
        if (!EQ(y, SL)) line(k, x, y, x, SL);
        shank(k, x, SL);
    } else {
        double yt = (R - Rsh) / tand(Rel) + y, xe = Rsh;
        if (GT(yt, SL)) { xe = x - (SL - y) * tand(Rel); yt = SL; }
        line(k, x, y, xe, yt);
        if (!EQ(yt, SL)) line(k, xe, yt, xe, SL);
        shank(k, xe, SL);
    }
}

/* ---------- s3.17 makeThreadMill 0x180009048 -> 0x18014a220, makeThread 0x180003d32 -> 0x18014a050 ----------
   Each tooth builder appends one tooth step to T (<= 4 pieces) and advances *y; a piece that would reach FL
   (y + piece >= FL) is truncated to end at FL and sets y = FL. Returns a TlProfileResultCode. */
typedef struct { TpSeg s[4]; int n; } Tooth;
static void tl(Tooth *T, double x0, double y0, double x1, double y1) {
    TpSeg *s = &T->s[T->n++]; memset(s, 0, sizeof *s); s->type = 2; s->x0 = x0; s->y0 = y0; s->x1 = x1; s->y1 = y1;
}
static int tooth_un(K *k, double *y, Tooth *T) {                      /* makeUNISOThread 0x18000766c -> 0x18014a6f0 */
    const TpParams *p = k->p; double R = p->d / 2, P = p->pitch, FL = p->fl, ri = R - 0.866 * P, half = P / 2, t30 = tand(30);
    if (LE(ri, 0)) return TP_ThreadPitchTooLargeForDiameter;
    if (GE(*y + half, FL)) { tl(T, ri, *y, (FL - *y) / t30 + ri, FL); *y = FL; return 0; }
    tl(T, ri, *y, R, *y + half); *y += half;
    if (GE(*y + half, FL)) { tl(T, R, *y, R - (FL - *y) / t30, FL); *y = FL; return 0; }
    tl(T, R, *y, ri, *y + half); *y += half;
    return 0;
}
static int tooth_acme(K *k, double *y, Tooth *T) {                    /* makeACMEThread 0x18000204f -> 0x1801400c0 */
    const TpParams *p = k->p; double R = p->d / 2, P = p->pitch, FL = p->fl;
    double d = (p->thread_shape == 3 ? 0.3 : 0.5) * P, t = tan(0.2530727), dyf = t * d, flat = (P - 2 * dyf) / 2, ri = R - d;
    if (LE(ri, 0)) return TP_ThreadPitchTooLargeForDiameter;
    if (GE(*y + dyf, FL)) { tl(T, ri, *y, ri + (FL - *y) / t, FL); *y = FL; return 0; }      /* flank up */
    tl(T, ri, *y, R, *y + dyf); *y += dyf;
    if (GE(*y + flat, FL)) { tl(T, R, *y, R, FL); *y = FL; return 0; }                       /* crest flat */
    tl(T, R, *y, R, *y + flat); *y += flat;
    if (GE(*y + dyf, FL)) { tl(T, R, *y, R - (FL - *y) / t, FL); *y = FL; return 0; }        /* flank down */
    tl(T, R, *y, ri, *y + dyf); *y += dyf;
    if (GE(*y + flat, FL)) { tl(T, ri, *y, ri, FL); *y = FL; return 0; }                     /* root flat */
    tl(T, ri, *y, ri, *y + flat); *y += flat;
    return 0;
}
static int tooth_npt(K *k, double *y, Tooth *T) {                     /* makeNPTThread 0x18000e395 -> 0x180147bc0 */
    const TpParams *p = k->p; double R = p->d / 2, P = p->pitch, FL = p->fl, kk = tan(atan(1.0 / 32)), t30 = tand(30);
    double up = ((kk * P) / tand(60) + P) / 2, down = P - up, h = 0.8657 * P;
#define XR(v) (R - ((FL - (v)) * kk + h))
    if (LE(XR(*y), 0) || LE(XR(*y + up + down), 0)) return TP_ThreadPitchTooLargeForDiameter;
    double y1 = *y + up, xc = R - (FL - y1) * kk, x0 = XR(*y);
    /* truncated pieces "use tan 30° as for UN": from the piece's start x */
    if (GE(y1, FL)) { tl(T, x0, *y, (FL - *y) / t30 + x0, FL); *y = FL; return 0; }
    tl(T, x0, *y, xc, y1); *y = y1;
    if (GE(y1 + down, FL)) { tl(T, xc, y1, xc - (FL - y1) / t30, FL); *y = FL; return 0; }
    tl(T, xc, y1, XR(y1 + down), y1 + down); *y = y1 + down;
#undef XR
    return 0;
}
static int make_thread(K *k, double *y, Tooth *T) {
    T->n = 0;
    switch (k->p->thread_shape) {
    case 0: case 1: return tooth_un(k, y, T);
    case 2: case 3: return tooth_acme(k, y, T);
    case 6: return tooth_npt(k, y, T);
    default: return TP_ERR_NOT_DECODED;     /* 4 / 5 buttress: argument values NOT DECODED (s3.17); others: not in the spec */
    }
}
static void make_thread_mill(K *k) {
    const TpParams *p = k->p; double Rsh = p->dsh / 2, y = 0, x;
    if (EQ(Rsh, 0)) FAIL(TP_ShoulderDiameterCantBeZero);
    Tooth T; int first = 1;
    while (first || LT(y, p->fl)) {
        int e = make_thread(k, &y, &T);
        if (e) FAIL(e);
        if (T.n == 0) break;
        if (first) line(k, 0, 0, T.s[0].x0, 0);
        for (int i = 0; i < T.n; i++) { TpSeg *s = add(k); if (!s) return; *s = T.s[i]; }
        first = 0;
    }
    if (k->err) return;
    double xe = k->o[k->n - 1].x1;
    x = xe;
    if (!EQ(Rsh, xe) && !EQ(Rsh, y)) { line(k, xe, y, Rsh, y); x = Rsh; }   /* "Rsh != y": literal */
    if (!EQ(p->sl, y)) line(k, x, y, x, p->sl);
    shank(k, x, p->sl);
}

/* s3.18 makeBarrelMill 0x180009052 -> 0x180140a80 (r1 lower radius, Rp = r2 profile radius) */
static void make_barrel(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, Rsh = p->dsh / 2, r1 = p->r1, Rp = p->r2, FL = p->fl;
    if (GE(r1, Rp)) FAIL(TP_LowerRadiusLargerThanProfileRadius);
    if (LT(R, Rsh)) FAIL(TP_ShoulderDiameterLargerThanToolDiameter);
    if (GT(FL, 2 * Rp) || GT(R - Rsh, Rp)) FAIL(TP_ProfileRadiusTooSmall);
    if (EQ(Rsh, 0)) FAIL(TP_ShoulderDiameterCantBeZero);
    double a = Rp + Rsh - R, cx = R - Rp, cy = FL - sqrt(Rp * Rp - a * a);
    if (GT(cy, Rp)) FAIL(TP_ProfileRadiusTooSmall);
    if (LT(cy, 0)) FAIL(TP_ProfileRadiusTooLarge);
    double pt_ = acos(a / Rp), pb = asin(cy / Rp), st = TAU - pb;
    Pt S = pt(cx + Rp * cos(st), cy + Rp * sin(st));                    /* probe arc start (only S is checked) */
    if (!LE(S.y, 0) || !(GT(r1, 0) ? GT(S.x, 0) : GE(S.x, 0))) FAIL(TP_ProfileRadiusTooSmall);
    if (LT(cy, r1)) FAIL(TP_LowerRadiusLargerThanProfileRadius);
    double d = Rp - r1, e = cy - r1, w = sqrt(d * d - e * e);
    if (GT(cx, w) && GT(r1, 0)) FAIL(TP_ProfileRadiusTooSmall);
    double th = acos(e / d) + 3 * M_PI / 2, xl = w + cx;
    if (GT(xl, 0)) line(k, 0, 0, xl, 0);
    if (GT(r1, 0)) arc(k, xl, r1, r1, 3 * M_PI / 2, th - 3 * M_PI / 2);
    if (!EQ(th, TAU) && !EQ(th, 0)) arc(k, cx, cy, Rp, th, TAU - th);
    if (GT(pt_, 0)) arc(k, cx, cy, Rp, 0, pt_);
    if (!EQ(FL, p->sl)) line(k, Rsh, FL, Rsh, p->sl);
    shank(k, Rsh, p->sl);
}

/* s3.19 makeBoringBar 0x18000b1d1 -> 0x180141320 (A = taper angle) */
static void make_bore(K *k) {
    const TpParams *p = k->p; double R = p->d / 2, Ra = p->da / 2, r1 = p->r1, FL = p->fl, A = p->a;
    double xFL = R - FL / tand(90 - A);
    if (LT(r1, 0)) FAIL(TP_IllegalCornerRadius);
    if (GT(Ra, R)) FAIL(TP_ArborRadiusExceedsToolRadius);
    if (EQ(R, Ra)) FAIL(TP_ToolDiameterEqualsArborDiameter);
    if (LE(xFL, 0)) FAIL(TP_TaperAngleOrFluteLengthError);
    if (GT(r1, 0) && LT(xFL, R)) {
        Pt a = pt(R - r1, r1), b = pt(xFL - R + (R - r1), FL + r1), X; double s, u;
        offset_toward(&a, &b, pt(R, 2 * r1), r1);
        /* [0, 1] is checked on the flank parameter; the y = FL line's extent is not given by the spec */
        if (!ints(a, b, pt(0, FL), pt(R, FL), &s, &u, &X) || !IN01(s)) FAIL(TP_CheckCornerRadius);
        line(k, 0, 0, R - r1, 0);
        arc(k, R - r1, r1, r1, 3 * M_PI / 2, A * DEG + M_PI / 2);
        if (k->err) return;
        Pt E = seg_end(&k->o[k->n - 1]);
        line(k, E.x, E.y, X.x, FL);
        xFL = X.x;
    } else if (GT(r1, 0)) {
        Pt b0 = pt(0, 0), b1 = pt(R, 0), f0 = pt(R, 0), f1 = pt(xFL, FL), C; double s, u;
        offset_toward(&b0, &b1, pt(R, r1), r1);
        offset_toward(&f0, &f1, pt(0, r1), r1);
        if (!ints(b0, b1, f0, f1, &s, &u, &C) || !IN01(s) || !IN01(u)) FAIL(TP_CheckCornerRadius);
        Pt T = foot(C, pt(R, 0), pt(xFL, FL));
        line(k, 0, 0, C.x, 0);
        arc(k, C.x, C.y, r1, 3 * M_PI / 2, ang(T, C) - 3 * M_PI / 2 + TAU);
        line(k, T.x, T.y, xFL, FL);
    } else {
        line(k, 0, 0, R, 0);
        line(k, R, 0, xFL, FL);
    }
    if (!EQ(p->sl, FL)) line(k, xFL, FL, xFL, p->sl);
    if (!EQ(xFL, Ra)) line(k, xFL, p->sl, Ra, p->sl);
    shank(k, Ra, p->sl);
}

/* ---------- s1 CheckToolValidity 0x180007815 -> 0x18013fb70, CalcProfile 0x1800086c0 -> 0x18013f7e0 ---------- */
static int check_validity(K *k) {
    const TpParams *p = k->p;
    if (LE(p->oal, 0)) return TP_IllegalToolLength;
    if (LE(p->d, 0)) return TP_IllegalToolDiameter;
    if (LE(p->fl, 0)) return TP_IllegalFluteLength;
    if (GT(p->sl, p->oal)) return TP_IllegalShoulderLength;
    if (GT(p->fl, p->oal)) return TP_IllegalFluteLength;
    if (GT(p->fl, p->sl)) return TP_IllegalFluteShoulderLengths;
    if (LE(p->da, 0)) return TP_IllegalArborDiameter;
    if (LT(p->r1, 0)) return TP_IllegalCornerRadius;
    return 0;
}

int tp_build(const TpParams *p, TpSeg *out, int max, int *n) {
    K kk = { p, out, max, 0, 0, p->tol }, *k = &kk;
    k->err = check_validity(k);
    if (!k->err) switch (p->type) {
    case 0: case 4: case 5: case 9: case 20: make_csink_tap_null(k); break;
    case 1: make_center_drill(k); break;
    case 2: case 3: case 22: make_drill(k, p->a); break;
    case 6: make_reamer(k); break;
    case 7: make_bore(k); break;
    case 8: make_counterbore(k); break;
    case 10: make_flat(k); break;
    case 11: make_ball(k); break;
    case 12: case 21: make_chamfer(k); break;
    case 13: make_face(k); break;
    case 14: make_slot(k); break;
    case 15: make_radius_mill(k); break;
    case 16: make_dove(k); break;
    case 17: make_taper(k); break;
    case 18: make_lollipop(k); break;
    case 19: make_bull(k); break;
    case 23: make_feed(k); break;
    case 24: make_thread_mill(k); break;
    case 25: make_barrel(k); break;
    default: k->err = TP_UnknownError; break;
    }
    if (n) *n = k->n;
    return k->err;
}

int tp_polyline(const TpSeg *s, int n, double *r, double *z, int max, double arc_step_rad) {
    int m = 0;
#define P(a, b) do { if (m < max) { r[m] = (a); z[m] = (b); m++; } } while (0)
    for (int i = 0; i < n; i++) {
        if (m == 0 || fabs(r[m - 1] - s[i].x0) > 1e-9 || fabs(z[m - 1] - s[i].y0) > 1e-9) P(s[i].x0, s[i].y0);
        if (s[i].type == 4) {
            int k = arc_step_rad > 0 ? (int)ceil(fabs(s[i].sw) / arc_step_rad) : 1; if (k < 1) k = 1;
            for (int j = 1; j <= k; j++) { double a = s[i].a0 + s[i].sw * j / k; P(s[i].cx + s[i].r * cos(a), s[i].cy + s[i].r * sin(a)); }
        } else P(s[i].x1, s[i].y1);
    }
#undef P
    return m;
}

void tp_params_from_tool(const Tool *t, TpParams *p) {
    memset(p, 0, sizeof *p);
    /* s4.3 build_auto_mill_tool / toollib_net.md 4.2 arguments 4-7: entColor 9, IsMetric, nc_tol 1e-4, tol 5e-5 */
    p->ent_color = 9; p->is_metric = t->is_metric; p->nc_tol = 1e-4; p->tol = 5e-5;
    /* common arguments (toollib_net.md 4.2: 9 OAL, 10 D, 13 Da, 14 SL, 16 FL, 18 Dsh, 19 TL, 28 shank, 29 Dn); unset
       arguments are 0 except angle A = 180, angle C = 45, corner type 0 */
    p->type = t->type; p->oal = t->oal; p->d = t->dia; p->da = t->arbor_dia; p->sl = t->shoulder_len; p->fl = t->flute_len;
    p->dsh = t->shoulder_dia; p->a = 180; p->c = 45;
    if (!t->has_extras) {                   /* a part-file tool (TL_ID): only the common record - tip / taper angle from +0x20 */
        p->r1 = t->corner_r; p->a = t->tip_angle; p->dt = t->tip_dia; p->pitch = t->pitch;
        return;
    }
    p->tl = t->taper_len; p->shank_type = t->shank_type; p->dn = t->neck_dia;
    /* per class (PU L68-176, most-derived first) */
    switch (t->type) {
    case 14:                                /* SlotMill */
        p->r1 = t->corner_r; p->c1 = t->chamfer_len; p->a = t->taper_angle; p->corner_type = t->radius_type;
        p->c2 = t->sec_chamfer; p->r2 = t->sec_corner_r; break;
    case 10: case 11: case 12: case 13: case 15: case 16: case 17: case 18: case 19: case 21:   /* Endmill */
        p->r1 = t->corner_r; p->c1 = t->chamfer_len; p->dt = t->tip_dia; p->a = t->taper_angle; p->corner_type = t->radius_type; break;
    case 1:                                 /* CenterDrill */
        p->a = t->tip_angle; p->b = t->shoulder_angle; p->dp = t->drill_dia; p->lp = t->drill_len; break;
    case 8:                                 /* CounterBore */
        p->dp = t->pilot_dia; p->lp = t->pilot_len; break;
    case 4: case 5: case 24:                /* Threading */
        p->a = t->tip_angle; p->r1 = t->corner_r; p->corner_type = t->radius_type; p->thread_shape = t->thread_shape; p->pitch = t->pitch; break;
    case 7:                                 /* Boring */
        p->a = t->taper_angle; p->b = t->shoulder_angle; p->r1 = t->corner_r; p->corner_type = t->radius_type; break;
    case 2: case 3: case 9: case 22:        /* Drill */
        p->a = t->tip_angle; p->b = t->shoulder_angle; p->r1 = t->corner_r; break;
    case 6:                                 /* Reamer */
        p->c1 = t->chamfer_len; break;
    case 25:                                /* BarrelMill */
        p->r1 = t->corner_r; p->r2 = t->profile_r; break;
    case 23:                                /* FeedMill */
        p->r1 = t->lower_r; p->r2 = t->upper_r; p->dt = t->tip_dia; p->a = t->lower_taper; p->c = t->upper_taper; p->b = t->relief_taper; break;
    }
}

const char *tp_err_name(int c) {
    switch (c) {
    case 0: return "NoError"; case 1: return "IllegalToolDiameter"; case 2: return "IllegalToolLength";
    case 3: return "IllegalShoulderLength"; case 4: return "IllegalFluteLength"; case 5: return "IllegalArborDiameter";
    case 6: return "IllegalFluteShoulderLengths"; case 8: return "ShoulderLengthCornerRadError";
    case 9: return "CornerRadiusTooLargeForRadius"; case 10: return "IllegalCornerRadius";
    case 11: return "ArborRadiusExceedsToolRadius"; case 12: return "TipToolDiameterIntersectionError";
    case 13: return "ArborDiameterTooLarge"; case 14: return "CornerRadiusOrFluteLengthError";
    case 15: return "TipAngleTooSteep"; case 16: return "ShoulderAngleTooSteep";
    case 18: return "ShoulderLengthTooShortForDiameter"; case 19: return "TipDiameterExceedsToolDiameter";
    case 20: return "CheckCornerRadius"; case 21: return "ToolDiameterEqualsArborDiameter";
    case 23: return "CheckCornerRadiusArborDiameter"; case 25: return "TaperAngleOrFluteLengthError";
    case 26: return "CheckTaperAngle"; case 27: return "ShoulderDiameterLargerThanArbor";
    case 30: return "ShoulderDiameterCantBeZero"; case 33: return "IllegalReducedNeckRadius";
    case 34: return "ReducedNeckLargerThanShoulderShank"; case 35: return "NoNeckLength";
    case 36: return "LowerRadiusLargerThanProfileRadius"; case 37: return "ShoulderDiameterLargerThanToolDiameter";
    case 38: return "ThreadPitchTooLargeForDiameter"; case 39: return "ChamferDistanceTooLargeForDiameter";
    case 40: return "ProfileRadiusTooSmall"; case 41: return "ProfileRadiusTooLarge"; case 42: return "LowerRadiusTooLarge";
    case 43: return "UpperRadiusTooLarge"; case 44: return "InvalidLowerTaperAngle"; case 45: return "InvalidUpperTaperAngle";
    case 46: return "InvalidReliefTaperAngle"; case 47: return "ReliefAngleNonZeroWithInvalidDiameters";
    case 99: return "UnknownError";
    case TP_ERR_OVERFLOW: return "output array full"; case TP_ERR_NOT_DECODED: return "not decoded (spec)";
    default: return "?";
    }
}
