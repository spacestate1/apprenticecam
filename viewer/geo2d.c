/* geo2d - see geo2d.h */
#include <math.h>
#include <string.h>
#include "geo2d.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TAU (2 * M_PI)

static P2 p2(double x, double y) { P2 p = { x, y }; return p; }
static P2 sub(P2 a, P2 b) { return p2(a.x - b.x, a.y - b.y); }
static double dot(P2 a, P2 b) { return a.x * b.x + a.y * b.y; }
static double cross(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }
static double len(P2 a) { return hypot(a.x, a.y); }
static double nang(double a) { a = fmod(a, TAU); return a < 0 ? a + TAU : a; }   /* [0, 2pi) */

GE ge_point(P2 p, double z) { GE e; memset(&e, 0, sizeof e); e.t = GE_POINT; e.a = e.b = p; e.z = z; return e; }
GE ge_line(P2 a, P2 b, double z) { GE e; memset(&e, 0, sizeof e); e.t = GE_LINE; e.a = a; e.b = b; e.z = z; return e; }
GE ge_arc(P2 c, double r, double a0, double sw, double z) {
    GE e; memset(&e, 0, sizeof e); e.t = GE_ARC; e.c = c; e.r = fabs(r); e.z = z;
    if (sw < 0) { a0 += sw; sw = -sw; }
    if (sw > TAU) sw = TAU;
    e.a0 = nang(a0); e.sw = sw;
    e.a = p2(c.x + e.r * cos(e.a0), c.y + e.r * sin(e.a0));
    e.b = p2(c.x + e.r * cos(e.a0 + sw), c.y + e.r * sin(e.a0 + sw));
    return e;
}
int ge_is_circle(const GE *e) { return e->t == GE_ARC && e->sw > TAU - 1e-9; }

P2 ge_at(const GE *e, double t) {
    if (e->t == GE_LINE) return p2(e->a.x + t * (e->b.x - e->a.x), e->a.y + t * (e->b.y - e->a.y));
    if (e->t == GE_ARC) { double a = e->a0 + t * e->sw; return p2(e->c.x + e->r * cos(a), e->c.y + e->r * sin(a)); }
    return e->a;
}
P2 ge_dir(const GE *e, double t) {
    if (e->t == GE_LINE) { P2 d = sub(e->b, e->a); double l = len(d); return l > 0 ? p2(d.x / l, d.y / l) : p2(1, 0); }
    if (e->t == GE_ARC) { double a = e->a0 + t * e->sw; return p2(-sin(a), cos(a)); }
    return p2(1, 0);
}
double ge_len(const GE *e) { return e->t == GE_LINE ? len(sub(e->b, e->a)) : e->t == GE_ARC ? e->r * e->sw : 0; }

double ge_param(const GE *e, P2 p) {
    if (e->t == GE_LINE) { P2 d = sub(e->b, e->a); double l2 = dot(d, d); return l2 > 0 ? dot(sub(p, e->a), d) / l2 : 0; }
    if (e->t != GE_ARC || e->sw <= 0) return 0;
    double d = nang(atan2(p.y - e->c.y, p.x - e->c.x) - e->a0);
    if (ge_is_circle(e) || d <= e->sw) return d / e->sw;
    return (d - e->sw) < (TAU - d) ? d / e->sw : -(TAU - d) / e->sw;
}

P2 ge_nearest(const GE *e, P2 p, int bounded) {
    if (e->t == GE_POINT) return e->a;
    double t = ge_param(e, p);
    if (bounded && (t < 0 || t > 1)) {
        if (e->t == GE_LINE) t = t < 0 ? 0 : 1;
        else return len(sub(p, e->a)) < len(sub(p, e->b)) ? e->a : e->b;
    }
    return ge_at(e, t);
}
double ge_dist(const GE *e, P2 p) { return len(sub(p, ge_nearest(e, p, 1))); }

GE ge_sub(const GE *e, double t0, double t1) {
    if (e->t == GE_LINE) return ge_line(ge_at(e, t0), ge_at(e, t1), e->z);
    if (e->t == GE_ARC) return ge_arc(e->c, e->r, e->a0 + t0 * e->sw, (t1 - t0) * e->sw, e->z);
    return *e;
}

static int line_line(P2 a, P2 b, P2 c, P2 d, P2 *o) {
    P2 r = sub(b, a), s = sub(d, c); double den = cross(r, s);
    if (fabs(den) < 1e-12 * (len(r) * len(s) + 1e-30)) return 0;
    double t = cross(sub(c, a), s) / den; *o = p2(a.x + t * r.x, a.y + t * r.y); return 1;
}
static int line_circle(P2 a, P2 b, P2 c, double r, P2 out[2]) {
    P2 d = sub(b, a); double l = len(d); if (l <= 0) return 0;
    P2 u = p2(d.x / l, d.y / l); double t = dot(sub(c, a), u);
    P2 f = p2(a.x + t * u.x, a.y + t * u.y); double h = len(sub(c, f));
    if (h > r * (1 + 1e-9) + 1e-12) return 0;
    double w = h >= r ? 0 : sqrt(r * r - h * h);
    out[0] = p2(f.x - w * u.x, f.y - w * u.y); if (w < 1e-9 * (r + 1)) return 1;
    out[1] = p2(f.x + w * u.x, f.y + w * u.y); return 2;
}
static int circle_circle(P2 c1, double r1, P2 c2, double r2, P2 out[2]) {
    P2 d = sub(c2, c1); double l = len(d);
    if (l < 1e-12 || l > r1 + r2 + 1e-9 * (r1 + r2) || l < fabs(r1 - r2) - 1e-9 * (r1 + r2)) return 0;
    double a = (r1 * r1 - r2 * r2 + l * l) / (2 * l), h2 = r1 * r1 - a * a, h = h2 > 0 ? sqrt(h2) : 0;
    P2 m = p2(c1.x + a * d.x / l, c1.y + a * d.y / l);
    out[0] = p2(m.x - h * d.y / l, m.y + h * d.x / l); if (h < 1e-9 * (r1 + 1)) return 1;
    out[1] = p2(m.x + h * d.y / l, m.y - h * d.x / l); return 2;
}
int ge_intersect(const GE *e1, const GE *e2, P2 out[2]) {
    if (e1->t == GE_LINE && e2->t == GE_LINE) return line_line(e1->a, e1->b, e2->a, e2->b, &out[0]);
    if (e1->t == GE_LINE && e2->t == GE_ARC) return line_circle(e1->a, e1->b, e2->c, e2->r, out);
    if (e1->t == GE_ARC && e2->t == GE_LINE) return line_circle(e2->a, e2->b, e1->c, e1->r, out);
    if (e1->t == GE_ARC && e2->t == GE_ARC) return circle_circle(e1->c, e1->r, e2->c, e2->r, out);
    return 0;
}
int ge_on(const GE *e, P2 p, double tol) { return e->t != GE_POINT && ge_dist(e, p) <= tol; }

int ge_offset(const GE *e, double d, GE *out) {
    if (e->t == GE_LINE) {
        P2 u = ge_dir(e, 0), n = p2(-u.y * d, u.x * d);
        *out = ge_line(p2(e->a.x + n.x, e->a.y + n.y), p2(e->b.x + n.x, e->b.y + n.y), e->z); return 1;
    }
    if (e->t == GE_ARC) { double r = e->r - d; if (r <= 1e-12) return 0; *out = *e; *out = ge_arc(e->c, r, e->a0, e->sw, e->z); return 1; }
    return 0;
}
int ge_side(const GE *e, P2 p) {
    if (e->t == GE_LINE) return cross(sub(e->b, e->a), sub(p, e->a)) >= 0 ? 1 : -1;
    if (e->t == GE_ARC) return len(sub(p, e->c)) >= e->r ? 1 : -1;
    return 1;
}
/* offset of the carrier towards side s (+1 left of a line / outside a circle) */
static int off_side(const GE *e, int s, double r, GE *o) { return ge_offset(e, e->t == GE_LINE ? s * r : -s * r, o); }

int ge_fillet(const GE *e1, P2 k1, const GE *e2, P2 k2, double r, GE *arc, double *t1, double *t2) {
    if (r <= 0 || e1->t == GE_POINT || e2->t == GE_POINT) return 0;
    /* the fillet is on the side of each entity where the other entity's kept part is */
    int s1 = ge_side(e1, k2), s2 = ge_side(e2, k1);
    GE o1, o2; P2 c[2]; int best = -1; double bd = 1e300; P2 O = { 0, 0 };
    for (int pass = 0; pass < 2 && best < 0; pass++) {     /* second pass: the other side for arcs (internal fillets) */
        int a1 = pass && e1->t == GE_ARC ? -s1 : s1, a2 = pass && e2->t == GE_ARC ? -s2 : s2;
        if (!off_side(e1, a1, r, &o1) || !off_side(e2, a2, r, &o2)) continue;
        int n = ge_intersect(&o1, &o2, c);
        for (int i = 0; i < n; i++) {
            /* find_fil_gt (decomp/wfacc/mccadutil_kern3.c:817, gp_ints with the midpoint of the two entities' pick points): the candidate nearer the midpoint */
            P2 m = { (ge_at(e1, ge_param(e1, k1)).x + ge_at(e2, ge_param(e2, k2)).x) / 2, (ge_at(e1, ge_param(e1, k1)).y + ge_at(e2, ge_param(e2, k2)).y) / 2 };
            double d = len(sub(c[i], m));
            if (d < bd) { bd = d; best = i; O = c[i]; }
        }
    }
    if (best < 0) return 0;
    P2 T1 = ge_nearest(e1, O, 0), T2 = ge_nearest(e2, O, 0);
    double a1 = atan2(T1.y - O.y, T1.x - O.x), a2 = atan2(T2.y - O.y, T2.x - O.x), sw = a2 - a1;
    while (sw > M_PI) sw -= TAU;
    while (sw <= -M_PI) sw += TAU;
    *arc = ge_arc(O, r, a1, sw, e1->z);
    *t1 = ge_param(e1, T1); *t2 = ge_param(e2, T2);
    return 1;
}

int ge_circles_tan2(const GE *e1, const GE *e2, double r, P2 out[8]) {
    int n = 0;
    for (int s1 = -1; s1 <= 1; s1 += 2) for (int s2 = -1; s2 <= 1; s2 += 2) {
        GE o1, o2; P2 c[2];
        if (!off_side(e1, s1, r, &o1) || !off_side(e2, s2, r, &o2)) continue;
        int k = ge_intersect(&o1, &o2, c);
        for (int i = 0; i < k && n < 8; i++) out[n++] = c[i];
    }
    return n;
}
int ge_circles_tan_pt(const GE *e, P2 p, double r, P2 out[4]) {
    int n = 0; GE pc = ge_arc(p, r, 0, TAU, e->z);
    for (int s = -1; s <= 1; s += 2) {
        GE o; P2 c[2]; if (!off_side(e, s, r, &o)) continue;
        int k = ge_intersect(&o, &pc, c);
        for (int i = 0; i < k && n < 4; i++) out[n++] = c[i];
    }
    return n;
}
int ge_tangent_points(P2 c, double r, P2 p, P2 out[2]) {
    P2 d = sub(p, c); double l = len(d); if (l <= r) return 0;
    double base = atan2(d.y, d.x), al = acos(r / l);
    out[0] = p2(c.x + r * cos(base + al), c.y + r * sin(base + al));
    out[1] = p2(c.x + r * cos(base - al), c.y + r * sin(base - al));
    return 2;
}

void ge_closest(const GE *e1, const GE *e2, P2 *pa, P2 *pb) {
    double bd = 1e300; P2 x[2];
    int n = ge_intersect(e1, e2, x);
    for (int i = 0; i < n; i++) if (ge_on(e1, x[i], 1e-7) && ge_on(e2, x[i], 1e-7)) { *pa = *pb = x[i]; return; }
#define TRY(A, B) do { P2 A_ = (A), B_ = (B); double d_ = len(sub(A_, B_)); if (d_ < bd) { bd = d_; *pa = A_; *pb = B_; } } while (0)
    TRY(e1->a, ge_nearest(e2, e1->a, 1)); TRY(e1->b, ge_nearest(e2, e1->b, 1));
    TRY(ge_nearest(e1, e2->a, 1), e2->a); TRY(ge_nearest(e1, e2->b, 1), e2->b);
    if (e2->t == GE_ARC) { P2 q = ge_nearest(e1, e2->c, 1); TRY(q, ge_nearest(e2, q, 1)); }
    if (e1->t == GE_ARC) { P2 q = ge_nearest(e2, e1->c, 1); TRY(ge_nearest(e1, q, 1), q); }
    if (e1->t == GE_ARC && e2->t == GE_ARC) { P2 q = ge_nearest(e1, e2->c, 1); TRY(q, ge_nearest(e2, q, 1)); }
#undef TRY
}

GE ge_trim_keep(const GE *e, double tx, double tk) {
    if (tx > 0 && tx < 1) return tk < tx ? ge_sub(e, 0, tx) : ge_sub(e, tx, 1);
    if (tx >= 1) return ge_sub(e, 0, tx);
    return ge_sub(e, tx, 1);
}

int ge_circle3(P2 a, P2 b, P2 c, P2 *o, double *r) {
    double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    double sc = fmax(fmax(len(sub(b, a)), len(sub(c, a))), 1e-30);
    if (fabs(d) < 1e-12 * sc * sc) return 0;
    double a2 = dot(a, a), b2 = dot(b, b), c2 = dot(c, c);
    *o = p2((a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d, (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d);
    *r = len(sub(a, *o)); return 1;
}
int ge_arc3(P2 a, P2 m, P2 b, double z, GE *out) {
    P2 o; double r; if (!ge_circle3(a, m, b, &o, &r)) return 0;
    double t0 = atan2(a.y - o.y, a.x - o.x), tm = nang(atan2(m.y - o.y, m.x - o.x) - t0), tb = nang(atan2(b.y - o.y, b.x - o.x) - t0);
    *out = tm < tb ? ge_arc(o, r, t0, tb, z) : ge_arc(o, r, t0, tb - TAU, z);   /* through m: counter-clockwise or the other way */
    return 1;
}
