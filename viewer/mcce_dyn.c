/* mcce_dyn.c - port of Mastercam 2017's mcce.dll Dynamic Mill engine (MCCECalcDynPock and the CCePock machine).
 *
 * Every rule cites the spec ../formats/mcce_engines.md (§x) or the decompile decomp/mcce_all.c (l.N). Geometry kernels
 * that are standard operations with decoded semantics (offset, boolean, point-in-polygon, nearest point, intersection)
 * are our own code or the Clipper wrapper (offset.h); see ../formats/mcce_port_plan.md §3. Pieces the spec marks
 * NOT DECODED are stubbed with a "NOT DECODED" comment and listed in mcce_port_plan.md §5. */
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcce_dyn.h"
#include "mcce_ctour.h"   /* mcce_region_bool: the DLL's region boolean (mcce_sobool.inc) */
/* McSysTol (mcam_load.h, which this file cannot include: its own Ent): tol / chain_tol of the current configuration */
double mc_systol_tol(void);
double mc_systol_chain_tol(int metric);
#include <stdatomic.h>
/* ours: set from another thread (the Qt dialog's Cancel while OK regenerates) - the computation stops at its next step and
   mcce_calc_dyn_pock fails with "cancelled" (its partial result is dropped); 0 = run to the end, so every caller that never
   sets it gets the same result as before */
static atomic_int g_mcce_abort;
void mcce_abort_set(int on) { atomic_store(&g_mcce_abort, on != 0); }
int mcce_abort_get(void) { return atomic_load_explicit(&g_mcce_abort, memory_order_relaxed); }
#define mcce_abort mcce_abort_get()

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define FLT_MAXD 3.4028234663852886e38          /* the engine's "no value yet" (e.g. FUN_18006d330 l.62015-62029) */

/* ============================================================================================ kernel: entities */
typedef struct { double x, y; } P2;
static P2 p2(double x, double y) { P2 r = { x, y }; return r; }
static P2 padd(P2 a, P2 b) { return p2(a.x + b.x, a.y + b.y); }
static P2 psub(P2 a, P2 b) { return p2(a.x - b.x, a.y - b.y); }
static P2 pmul(P2 a, double k) { return p2(a.x * k, a.y * k); }
static double pdot(P2 a, P2 b) { return a.x * b.x + a.y * b.y; }
static double pcross(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }
static double plen(P2 a) { return sqrt(a.x * a.x + a.y * a.y); }
static double pdist2(P2 a, P2 b) { P2 d = psub(a, b); return d.x * d.x + d.y * d.y; }
static double pdist(P2 a, P2 b) { return sqrt(pdist2(a, b)); }
static double wrap_pi(double a) { while (a > M_PI) a -= 2 * M_PI; while (a < -M_PI) a += 2 * M_PI; return a; }   /* to [-pi, pi] */

/* pock_chn (0x88 bytes): id +0x08, pass byte +0x09, CeFlags +0x12, byte +0x13, line +0x18..+0x30, arc +0x18..+0x38,
   bbox +0x40..+0x58, relative Z +0x78 (§3 D conventions, §6.1 header) */
typedef struct {
    char id;                 /* 'L', 'A', 'P' (0 = none) */
    uint8_t pass, ce, b13;
    P2 p1, p2;               /* line e1 / e2; 'P': p1 */
    P2 c; double r, sa, sw;  /* arc */
    double z;                /* +0x78 */
    int ref_ent, ref_sh;     /* +0x0c / +0x80: window copies (FUN_180029c30 l.27246-27247) */
} Ent;

static Ent ent_line(P2 a, P2 b) { Ent e; memset(&e, 0, sizeof e); e.id = 'L'; e.p1 = a; e.p2 = b; return e; }
static Ent ent_arc(P2 c, double r, double sa, double sw) { Ent e; memset(&e, 0, sizeof e); e.id = 'A'; e.c = c; e.r = r; e.sa = sa; e.sw = sw; return e; }
/* entity start / end / point at t: 'L' e1 / e2 / lerp; 'A' c + r (cos, sin)(sa + t sw); 'P' pt; else (0, 0)
   (entity start formula §1 PeelMotMat, FUN_18006f1c0 l.62971-63070 through FUN_180021580) */
static P2 e_at(const Ent *e, double t) {
    if (e->id == 'L') return p2(e->p1.x + (e->p2.x - e->p1.x) * t, e->p1.y + (e->p2.y - e->p1.y) * t);
    if (e->id == 'A') { double a = e->sa + t * e->sw; return p2(e->c.x + e->r * cos(a), e->c.y + e->r * sin(a)); }
    if (e->id == 'P') return e->p1;
    return p2(0, 0);
}
static P2 e_start(const Ent *e) { return e_at(e, 0); }
static P2 e_end(const Ent *e) { return e_at(e, 1); }
/* length: 'L' chord, 'A' |r sw| (FUN_180121980 / FUN_18006f1c0 l.62999-63008) */
static double e_len(const Ent *e) {
    if (e->id == 'L') return pdist(e->p1, e->p2);
    if (e->id == 'A') return fabs(e->sw * e->r);
    return 0;
}
/* tangent (unnormalised for lines: direction of travel; arcs: radial rotated by the sweep sign) */
static P2 e_tan(const Ent *e, double t) {
    if (e->id == 'L') return psub(e->p2, e->p1);
    if (e->id == 'A') { double a = e->sa + t * e->sw, s = e->sw >= 0 ? 1 : -1; return p2(-sin(a) * s * e->r, cos(a) * s * e->r); }
    return p2(0, 0);
}
static P2 unit(P2 v, int *ok) { double l = plen(v); if (l < 1e-12) { if (ok) *ok = 0; return p2(0, 0); } if (ok) *ok = 1; return pmul(v, 1 / l); }
/* entity cut to fraction u of itself, start kept: line end lerp, arc sw x u (FUN_18006de20 l.62203-62256, FUN_180022a50) */
static Ent e_cut(const Ent *e, double u) {
    Ent r = *e;
    if (e->id == 'L') r.p2 = e_at(e, u);
    else if (e->id == 'A') r.sw = e->sw * u;
    return r;
}
/* the piece of an entity between parameters t0 <= t1 */
static Ent e_sub(const Ent *e, double t0, double t1) {
    Ent r = *e;
    if (e->id == 'L') { r.p1 = e_at(e, t0); r.p2 = e_at(e, t1); }
    else if (e->id == 'A') { r.sa = e->sa + e->sw * t0; r.sw = e->sw * (t1 - t0); }
    return r;
}
static Ent e_rev(const Ent *e) {   /* reversed entity: 'L' endpoints swapped, 'A' sa += sw, sw = -sw (FUN_18011d800 l.153632-153871) */
    Ent r = *e;
    if (e->id == 'L') { r.p1 = e->p2; r.p2 = e->p1; }
    else if (e->id == 'A') { r.sa = e->sa + e->sw; r.sw = -e->sw; }
    return r;
}
/* min / max without the libm call (fmin / fmax are not inlined; same values for the finite inputs used here) */
static inline double dmin(double a, double b) { return b < a ? b : a; }
static inline double dmax(double a, double b) { return b > a ? b : a; }
static void e_bbox(const Ent *e, double bb[4]) {
    P2 a = e_start(e), b = e_end(e);
    bb[0] = dmin(a.x, b.x); bb[1] = dmin(a.y, b.y); bb[2] = dmax(a.x, b.x); bb[3] = dmax(a.y, b.y);
    if (e->id == 'A') {   /* add the axis extremes the sweep passes */
        double lo = dmin(e->sa, e->sa + e->sw), hi = dmax(e->sa, e->sa + e->sw);
        for (int k = (int)floor(lo / (M_PI / 2)) - 1; k <= (int)ceil(hi / (M_PI / 2)) + 1; k++) {
            double a = k * M_PI / 2; if (a < lo || a > hi) continue;
            P2 q = p2(e->c.x + e->r * cos(a), e->c.y + e->r * sin(a));
            bb[0] = dmin(bb[0], q.x); bb[1] = dmin(bb[1], q.y); bb[2] = dmax(bb[2], q.x); bb[3] = dmax(bb[3], q.y);
        }
    }
}
/* ours (speed only): a box that contains e_bbox's box - lines: e_bbox itself; arcs: the full circle box (every arc point
   c + r (cos, sin) lies in it, rounding included). A test that fails on it fails on e_bbox's box. */
static void e_qbox(const Ent *e, double bb[4]) {
    if (e->id == 'A') { bb[0] = e->c.x - e->r; bb[1] = e->c.y - e->r; bb[2] = e->c.x + e->r; bb[3] = e->c.y + e->r; return; }
    e_bbox(e, bb);
}
/* line-line intersection of the carriers: point and parameters (a + s da = b + u db); 0 if parallel
   (role of mcce::CalcLnLnIntersectPt; internals NOT DECODED, plain carrier intersection used) */
static int ln_ln(P2 a, P2 da, P2 b, P2 db, double *s, double *u, P2 *x) {
    double den = pcross(da, db);
    if (fabs(den) < 1e-300) return 0;
    P2 w = psub(b, a);
    *s = pcross(w, db) / den; if (u) *u = pcross(w, da) / den;
    *x = padd(a, pmul(da, *s));
    return 1;
}
/* parameter of a point (assumed on the carrier) on an entity */
static double e_param(const Ent *e, P2 q) {
    if (e->id == 'L') { P2 d = psub(e->p2, e->p1); double l2 = pdot(d, d); return l2 > 0 ? pdot(psub(q, e->p1), d) / l2 : 0; }
    if (e->id == 'A') {
        double a = atan2(q.y - e->c.y, q.x - e->c.x), rel = a - e->sa;
        if (e->sw >= 0) { while (rel < -1e-9) rel += 2 * M_PI; while (rel >= 2 * M_PI - 1e-12) rel -= 2 * M_PI; }
        else { while (rel > 1e-9) rel -= 2 * M_PI; while (rel <= -2 * M_PI + 1e-12) rel += 2 * M_PI; }
        double u = fabs(e->sw) > 1e-15 ? rel / e->sw : 0;
        /* near the start from the other side: a small negative parameter */
        if (u > 1 && fabs(e->sw) < 2 * M_PI - 1e-9) { double back = (2 * M_PI * (e->sw >= 0 ? 1 : -1) - rel) / e->sw; if (back * fabs(e->sw) * e->r < 1e-6) u = -back; }
        return u;
    }
    return 0;
}
/* bounded intersections of two entities (role of mcce::CalcEntIntersect: up to 2 points with the parameters on each;
   internals NOT DECODED, own line / arc code); tol = parameter-space slack in length units */
/* ra / rb (may be NULL): the unclamped parameters (exact model: FUN_180035f30 counts t values outside [0, 1]) */
static int e_isect_r(const Ent *a, const Ent *b, double tol, double ta[2], double tb[2], P2 pt[2], double ra[2], double rb[2]);
static int e_isect(const Ent *a, const Ent *b, double tol, double ta[2], double tb[2], P2 pt[2]) { return e_isect_r(a, b, tol, ta, tb, pt, NULL, NULL); }
static int e_isect_r(const Ent *a, const Ent *b, double tol, double ta[2], double tb[2], P2 pt[2], double ra[2], double rb[2]) {
    P2 cand[2]; int nc = 0;
    if (a->id == 'L' && b->id == 'L') {
        double s, u; P2 x;
        if (ln_ln(a->p1, psub(a->p2, a->p1), b->p1, psub(b->p2, b->p1), &s, &u, &x)) cand[nc++] = x;
    } else if (a->id == 'A' && b->id == 'A') {
        double d = pdist(a->c, b->c);
        if (d > 1e-12 && d <= a->r + b->r + tol && d >= fabs(a->r - b->r) - tol) {
            double aa = (a->r * a->r - b->r * b->r + d * d) / (2 * d), h2 = a->r * a->r - aa * aa, h = h2 > 0 ? sqrt(h2) : 0;
            P2 u = pmul(psub(b->c, a->c), 1 / d), m = padd(a->c, pmul(u, aa));
            cand[nc++] = p2(m.x - u.y * h, m.y + u.x * h);
            if (h > 1e-12) cand[nc++] = p2(m.x + u.y * h, m.y - u.x * h);
        }
    } else {
        const Ent *l = a->id == 'L' ? a : b, *c = a->id == 'L' ? b : a;
        if (l->id == 'L' && c->id == 'A') {
            P2 d = psub(l->p2, l->p1); double L2 = pdot(d, d);
            if (L2 > 0) {
                P2 f = psub(l->p1, c->c); double B = 2 * pdot(f, d), C = pdot(f, f) - c->r * c->r, disc = B * B - 4 * L2 * C;
                double sl = tol * tol * 4 * L2;   /* tangency slack */
                if (disc > -sl) {
                    double sq = disc > 0 ? sqrt(disc) : 0;
                    cand[nc++] = padd(l->p1, pmul(d, (-B - sq) / (2 * L2)));
                    if (sq > 1e-12) cand[nc++] = padd(l->p1, pmul(d, (-B + sq) / (2 * L2)));
                }
            }
        }
    }
    int n = 0;
    for (int i = 0; i < nc; i++) {
        double u = e_param(a, cand[i]), v = e_param(b, cand[i]);
        double la = fmax(e_len(a), 1e-12), lb = fmax(e_len(b), 1e-12);
        if (u < -tol / la || u > 1 + tol / la || v < -tol / lb || v > 1 + tol / lb) continue;
        if (ra) ra[n] = u; if (rb) rb[n] = v;
        ta[n] = fmin(1, fmax(0, u)); tb[n] = fmin(1, fmax(0, v)); pt[n] = cand[i]; n++;
    }
    return n;
}

/* ============================================================================================ kernel: shapes */
/* CSh: entity vector +0x18/+0x20, bbox +0x30..+0x48, dir +0x50 (1 CCW, -1 CW, 0 open), flags +0x54 (bit0 island),
   tag +0x58, z +0x60 (§3 B header, CopyPockBndsToShAr l.519-536) */
typedef struct {
    Ent *e; int n, cap;
    int dir, fl, tag;
    double bb[4];
    double z;
    uint8_t b90;             /* +0x90 byte (MOAT_FIRST tests; never set here) */
} Sh;
typedef struct { Sh **s; int n, cap; } ShL;

static Sh *sh_new(void) { Sh *s = calloc(1, sizeof(Sh)); return s; }
static void sh_free(Sh *s) { if (s) { free(s->e); free(s); } }
static void sh_push(Sh *s, Ent e) {
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 8; s->e = realloc(s->e, s->cap * sizeof(Ent)); }
    s->e[s->n++] = e;
}
static void sh_clear(Sh *s) { s->n = 0; }
static Sh *sh_copy(const Sh *a) {
    Sh *s = sh_new(); *s = *a; s->e = malloc((a->n ? a->n : 1) * sizeof(Ent)); s->cap = a->n ? a->n : 1;
    memcpy(s->e, a->e, a->n * sizeof(Ent)); return s;
}
static void sh_upd_bbox(Sh *s) {
    s->bb[0] = s->bb[1] = 1e300; s->bb[2] = s->bb[3] = -1e300;
    for (int i = 0; i < s->n; i++) { double b[4]; e_bbox(&s->e[i], b);
        s->bb[0] = dmin(s->bb[0], b[0]); s->bb[1] = dmin(s->bb[1], b[1]); s->bb[2] = dmax(s->bb[2], b[2]); s->bb[3] = dmax(s->bb[3], b[3]); }
}
static double sh_len(const Sh *s) { double L = 0; for (int i = 0; i < s->n; i++) L += e_len(&s->e[i]); return L; }
static int sh_island(const Sh *s) { return s->fl & 1; }
/* RevSh: entities in reverse order, each reversed; dir negated */
static void sh_rev(Sh *s) {
    for (int i = 0, j = s->n - 1; i <= j; i++, j--) { Ent a = e_rev(&s->e[i]), b = e_rev(&s->e[j]); s->e[i] = b; s->e[j] = a; }
    s->dir = -s->dir;
}
/* Retrace FUN_18011d800 (l.153632-153871): append the reverse of the entity list; dir = -1 */
static void sh_retrace(Sh *s) {
    int n = s->n;
    for (int i = n - 1; i >= 0; i--) sh_push(s, e_rev(&s->e[i]));
    s->dir = -1;
}

static void sl_push(ShL *l, Sh *s) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 8; l->s = realloc(l->s, l->cap * sizeof(Sh *)); }
    l->s[l->n++] = s;
}
static void sl_clear(ShL *l) { for (int i = 0; i < l->n; i++) sh_free(l->s[i]); l->n = 0; }
static void sl_free(ShL *l) { sl_clear(l); free(l->s); l->s = NULL; l->cap = 0; }
static ShL sl_copy(const ShL *a) { ShL r = { 0 }; for (int i = 0; i < a->n; i++) sl_push(&r, sh_copy(a->s[i])); return r; }
static void sl_move_append(ShL *dst, ShL *src) { for (int i = 0; i < src->n; i++) sl_push(dst, src->s[i]); src->n = 0; }   /* AppendByMove */
static void sl_erase(ShL *l, int i) { sh_free(l->s[i]); memmove(l->s + i, l->s + i + 1, (l->n - i - 1) * sizeof(Sh *)); l->n--; }
static void sl_bbox(const ShL *l, double bb[4]) {
    bb[0] = bb[1] = 1e300; bb[2] = bb[3] = -1e300;
    for (int i = 0; i < l->n; i++) { bb[0] = fmin(bb[0], l->s[i]->bb[0]); bb[1] = fmin(bb[1], l->s[i]->bb[1]); bb[2] = fmax(bb[2], l->s[i]->bb[2]); bb[3] = fmax(bb[3], l->s[i]->bb[3]); }
}
/* FUN_180112860 (l.148058-148080): number of shapes that are not islands */
static int sl_nonisland(const ShL *l) { int k = 0; for (int i = 0; i < l->n; i++) if (!sh_island(l->s[i])) k++; return k; }
/* nest group starting at index i: shapes while islands, then the first non-island (FUN_18010cf20 l.145168-145194);
   returns the index after the group, or -1 when no non-island is left */
static int sl_group_end(const ShL *l, int i) {
    for (int k = i; k < l->n; k++) if (!sh_island(l->s[k])) return k + 1;
    return -1;
}

/* ---------------------------------------------------------------------------- positions and walks */
typedef struct { int sh, ent; double t; P2 pt; } Pos;

/* FUN_18006f1c0 (l.62971-63070): walk d forward along shape from->sh from (ent, t); mode & 1 = stop at the next
   entity end; closed shapes (dir != 0) wrap; open shapes clamp at t = 1 and fail when more than 1e-16 is left;
   *walked = d - left. Returns 1 on success. */
static int walk_fwd(int mode, double d, const ShL *L, const Pos *from, Pos *out, double *walked) {
    const Sh *s = L->s[from->sh]; int ok = 1, it = 0;
    if (d < 0) d = 0;
    *out = *from; double left = d;
    for (;;) {
        double el = e_len(&s->e[out->ent]), rem = (1 - out->t) * el;
        if (left < rem) {
            if (el > 1e-12) { out->t = left / el + out->t; if (out->t < 0) out->t = 0; if (out->t > 1) out->t = 1; }
            left = 0; break;
        }
        out->ent++; left -= rem;
        if (out->ent >= s->n) {
            if (s->dir == 0) { out->t = 1; out->ent = s->n - 1; if (left > 1e-16) ok = 0; break; }
            out->ent = 0;
        }
        out->t = 0;
        if ((mode & 1) || left < 0) break;
        if (++it > s->n) { ok = 0; break; }
    }
    out->pt = e_at(&s->e[out->ent], out->t);
    if (walked) *walked = d - left;
    return ok;
}
/* FUN_18006f4f0 (l.63073-63155): the same walk backwards */
static int walk_back(int mode, double d, const ShL *L, const Pos *from, Pos *out, double *walked) {
    const Sh *s = L->s[from->sh]; int ok = 1, it = 0;
    *out = *from; double left = d;
    for (;;) {
        double el = e_len(&s->e[out->ent]);
        if (left < el * out->t && el > 1e-6) { out->t -= left / el; left = 0; break; }
        out->ent--; left -= el * out->t;
        if (out->ent < 0) {
            if (s->dir == 0) { out->ent = 0; out->t = 0; if (left <= 1e-16) break; ok = 0; break; }
            out->ent = s->n - 1;
        }
        out->t = 1;
        if ((mode & 1) || left < 0) break;
        if (++it > s->n) { ok = 0; break; }
    }
    out->pt = e_at(&s->e[out->ent], out->t);
    if (walked) *walked = d - left;
    return ok;
}

/* GetNearestEntPt (l.8319-8360): 'L' projection clamped to [0,1] (|E-S|^2 < 1e-14: midpoint, t 0.5); 'A' GetNearestArcPt
   (l.6061-6118): phi = atan2(P - c) - sa normalised to [0, 2pi) ((-2pi, 0] when sw < 0); outside the sweep the end
   whose side of the excluded arc it is on; P at the centre -> sw / 2 (§6.11.6). far = 1: the farthest point instead
   (FUN_1800acff0: farther line end; arc: the same rule with atan2(c - P)). */
static int swx_on(void);
static double e_near_far(const Ent *e, P2 p, int far, double *t, P2 *q) {
    if (e->id == 'L') {
        P2 d = psub(e->p2, e->p1); double l2 = pdot(d, d), u;
        if (far) u = pdist2(e->p1, p) >= pdist2(e->p2, p) ? 0 : 1;   /* FUN_1800ad050: the farther end */
        else if (l2 < 1e-14) u = 0.5;
        else { u = pdot(psub(p, e->p1), d) / l2; if (u < 0) u = 0; if (u > 1) u = 1; }
        *t = u; *q = e_at(e, u); return pdist(*q, p);
    }
    if (e->id == 'A' && swx_on()) {   /* mcce::GetNearestArcPt (l.6061-6127) / far: FUN_1800acd50 (atan2(c - P)), operation for operation */
        double v;
        if (1.1920928955078125e-07 <= fabs(p.x - e->c.x) || 1.1920928955078125e-07 <= fabs(p.y - e->c.y)) {
            double ph = far ? atan2(e->c.y - p.y, e->c.x - p.x) : atan2(p.y - e->c.y, p.x - e->c.x);
            for (ph = ph - e->sa; 6.283185307179586 < ph; ph = ph + -6.283185307179586) ;
            for (; ph < 0.0; ph = ph + 6.283185307179586) ;
            if (e->sw < 0.0) for (; 0.0 < ph; ph = ph + -6.283185307179586) ;
            v = ph;
            if (fabs(e->sw) < fabs(ph)) {
                double h = 0.0 <= e->sw ? e->sw * 0.5 + 3.141592653589793 : e->sw * 0.5 - 3.141592653589793;
                v = e->sw; if (fabs(h) < fabs(ph)) v = 0.0;
            }
        } else v = e->sw * 0.5;
        *q = p2(cos(v + e->sa) * e->r + e->c.x, sin(v + e->sa) * e->r + e->c.y);
        *t = fabs(e->sw) <= 1e-12 ? 0.0 : v / e->sw;
        return pdist(*q, p);
    }
    if (e->id == 'A') {
        double ph;
        if (fabs(p.x - e->c.x) <= 1.1920928955078125e-07 && fabs(p.y - e->c.y) <= 1.1920928955078125e-07) ph = e->sw / 2;
        else {
            ph = far ? atan2(e->c.y - p.y, e->c.x - p.x) - e->sa : atan2(p.y - e->c.y, p.x - e->c.x) - e->sa;
            if (e->sw >= 0) { while (ph < 0) ph += 2 * M_PI; while (ph >= 2 * M_PI) ph -= 2 * M_PI; }
            else { while (ph > 0) ph -= 2 * M_PI; while (ph <= -2 * M_PI) ph += 2 * M_PI; }
            if (fabs(ph) > fabs(e->sw)) ph = fabs(ph) <= fabs(e->sw / 2 + (e->sw >= 0 ? M_PI : -M_PI)) ? e->sw : 0;
        }
        double u = fabs(e->sw) <= 1e-12 ? 0 : ph / e->sw;
        *t = u; *q = e_at(e, u); return pdist(*q, p);
    }
    *t = 0; *q = e->p1; return pdist(e->p1, p);
}
/* FUN_18013d200 (l.170449-170586): nearest point of one shape; ties go to the later entity */
static int sh_nearest(const Sh *s, P2 p, Pos *out) {
    double bd = 1e300; int found = 0;
    out->sh = 0; out->ent = -1; out->t = 0; out->pt = p2(0, 0);
    int x = swx_on();   /* FUN_18013d280 compares (p.y - q.y)^2 + (p.x - q.x)^2 itself (MCCE_MEASX=0: the root squared) */
    for (int k = 0; k < s->n; k++) { double t; P2 q; double d = e_near_far(&s->e[k], p, 0, &t, &q);
        double d2 = x ? (p.y - q.y) * (p.y - q.y) + (p.x - q.x) * (p.x - q.x) : d * d, b2 = x ? bd : bd * bd;
        if (!found || d2 <= b2) { bd = x ? d2 : d; out->ent = k; out->t = t; out->pt = q; found = 1; } }
    return found;
}
/* FUN_18013d010 / FUN_18013cfe0 (l.170368-170444): nearest over a list; ties go to the earlier shape; an empty shape
   scores the point (0, 0) */
static int sl_nearest(const ShL *l, P2 p, Pos *out) {
    double bd = FLT_MAXD; int found = 0;
    out->sh = -1; out->ent = -1; out->t = 0; out->pt = p2(0, 0);
    for (int i = 0; i < l->n; i++) {
        Pos q; double d2;
        if (sh_nearest(l->s[i], p, &q)) d2 = pdist2(q.pt, p); else { q.ent = -1; q.t = 0; q.pt = p2(0, 0); d2 = pdist2(q.pt, p); }
        if (!found || d2 < bd) { bd = d2; *out = q; out->sh = i; found = 1; }
    }
    return found && out->ent >= 0;
}
/* FUN_18013caf0 (l.170150-170226): farthest point of one shape; ties go to the earlier entity */
static int sh_farthest(const Sh *s, P2 p, Pos *out) {
    double bd = -1.0; int found = 0;
    int x = swx_on();   /* FUN_18013caf0 compares (p.y - q.y)^2 + (p.x - q.x)^2 (l.170201) */
    for (int k = 0; k < s->n; k++) { double t; P2 q; double d = e_near_far(&s->e[k], p, 1, &t, &q);
        double d2 = x ? (p.y - q.y) * (p.y - q.y) + (p.x - q.x) * (p.x - q.x) : d * d;
        if (d2 > bd) { bd = d2; out->sh = 0; out->ent = k; out->t = t; out->pt = q; found = 1; } }
    return found;
}

/* mcce::PtInSh (l.33-206): 2 inside, 4 outside, 1 on the boundary (within tol). The DLL-exact port is mcce_ctour.c's pt_in_sh
   (FUN_180143460 band collection, FUN_1801453d0 rotation, the ray to box x1 + 10 counted per run with FUN_180144460); it runs on the
   shape's entities with their SetChnMinMax boxes (e_bbox) and the shape's stored box. Before (2026-11-13) this was ours: a 5-degree
   chord crossing count, which put a link smoothing piece 0.03 outside its region "inside" (ct oracle buckle-v8 op 42 call 21:
   FUN_180113300 2 vs the DLL's 6 at d = 0.4725). MCCE_PIS=0: the old count (measurement only). */
static int pt_in_sh_old(P2 p, const Sh *s, double tol);
static int pt_in_sh(P2 p, const Sh *s, double tol) {
    static int mode = -1;
    if (mode < 0) { const char *v = getenv("MCCE_PIS"); mode = !(v && *v == '0'); }
    if (!mode) return pt_in_sh_old(p, s, tol);
    if (!s->n || p.x <= s->bb[0] - tol || s->bb[2] + tol <= p.x || p.y <= s->bb[1] - tol || s->bb[3] + tol <= p.y) return 4;
    static _Thread_local McceEnt *buf; static _Thread_local int cap;
    if (s->n > cap) { cap = s->n * 2; buf = realloc(buf, (size_t)cap * sizeof *buf); }
    for (int k = 0; k < s->n; k++) {
        const Ent *e = &s->e[k]; McceEnt *q = &buf[k];
        q->id = e->id; q->ce = 0; q->src_sh = q->src_ent = 0;
        if (e->id == 'A') { q->a[0] = e->c.x; q->a[1] = e->c.y; q->a[2] = e->r; q->a[3] = e->sa; q->a[4] = e->sw; }
        else { q->a[0] = e->p1.x; q->a[1] = e->p1.y; q->a[2] = e->p2.x; q->a[3] = e->p2.y; q->a[4] = 0; }
        e_bbox(e, q->box);
    }
    return mcce_pt_in_shape_ents(p.x, p.y, buf, s->n, s->bb, tol);
}
static int pt_in_sh_old(P2 p, const Sh *s, double tol) {
    int cr = 0;
    if (!s->n || p.x <= s->bb[0] - tol || p.x >= s->bb[2] + tol || p.y <= s->bb[1] - tol || p.y >= s->bb[3] + tol) return 4;
    for (int k = 0; k < s->n; k++) {
        const Ent *e = &s->e[k]; double t; P2 q;
        /* ours (speed only): skip the nearest-point test when p is clearly farther than tol from the entity's containing box
           (margin 1e-9 over tol for the rounding of the nearest point); an arc whose circle lies wholly above / below p or
           left of p has no crossing piece */
        double qb[4]; e_qbox(e, qb); double mg = tol + 1e-9;
        if (!(p.x < qb[0] - mg || p.x > qb[2] + mg || p.y < qb[1] - mg || p.y > qb[3] + mg) && e_near_far(e, p, 0, &t, &q) <= tol) return 1;
        if (e->id == 'A' && (p.y > qb[3] || p.y < qb[1] || p.x > qb[2] + 1e-9)) continue;
        if (e->id == 'L') {
            P2 a = e->p1, b = e->p2;
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) cr++;
        } else if (e->id == 'A') {   /* split into monotone pieces by sampling at <= 5 degrees */
            int m = (int)ceil(fabs(e->sw) / (M_PI / 36)); if (m < 1) m = 1;
            P2 a = e_at(e, 0);
            for (int j = 1; j <= m; j++) { P2 b = e_at(e, (double)j / m);
                if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) cr++;
                a = b; }
        }
    }
    return (cr & 1) ? 2 : 4;
}
/* FUN_180112a00 (l.148133-148165) over a list: PtInSh 1 -> 1; 2 -> 4 for a hole, else 2; no hit -> 4 */
static int sl_inside(const ShL *l, double tol, P2 p) {
    for (int i = 0; i < l->n; i++) {
        int c = pt_in_sh(p, l->s[i], tol);
        if (c == 1) return 1;
        if (c == 2) return sh_island(l->s[i]) ? 4 : 2;
    }
    return 4;
}

/* FUN_180147ed0 (l.176360-176395): entity count and mode of a section (2 = wrap); CSh+0x50 not consulted */
static int sec_count(const Sh *s, int e1, double t1, int e2, double t2, int *full) {
    if (*full && (e1 != e2 || fabs(t1 - t2) > 1e-12)) *full = 0;
    if (e2 < e1 || (e1 == e2 && t2 < t1) || *full) return *full ? s->n + 1 : e2 + 1 + (s->n - e1);
    return e2 - e1 + 1;
}
/* FUN_180122fb0 / FUN_180123010 (l.156331-156366): next / previous entity, wrapping only on closed shapes */
static int e_next(const Sh *s, int *e) { if (*e + 1 < s->n) { (*e)++; return 1; } if (s->dir != 0) { *e = 0; return 1; } return 0; }
static int e_prev(const Sh *s, int *e) { if (*e > 0) { (*e)--; return 1; } if (s->dir != 0) { *e = s->n - 1; return 1; } return 0; }
/* FUN_180146e20 (l.175722-176061, §6.11.1): forward section copy (e1, t1) -> (e2, t2) appended to out; end before start
   wraps through the last entity to entity 0 (open shapes too); `full` (start == end) copies the whole loop; only a
   degenerate first / last piece (< 1e-12) is dropped. Returns the number of entities appended. */
static int sh_section_full(const Sh *s, int e1, double t1, int e2, double t2, Sh *out, int full) {
    if (!s->n) return 0;
    t1 = fmin(1, fmax(0, t1)); t2 = fmin(1, fmax(0, t2));
    if (full && (e1 != e2 || fabs(t1 - t2) >= 1e-12)) full = 0;
    /* trimEnds FUN_1801483e0 (l.176504-176617) */
    int n = sec_count(s, e1, t1, e2, t2, &full);
    if (n != 1) {
        const Ent *a = &s->e[e1]; double rest = a->id == 'L' ? pdist(a->p2, e_at(a, t1)) : a->id == 'A' ? fabs((a->sw - a->sw * t1) * a->r) : 0;
        if ((a->id != 'L' && a->id != 'A') || rest < 1e-12) { full = 0; n--; t1 = 0.0; e_next(s, &e1); }
        const Ent *b = &s->e[e2]; double head = b->id == 'L' ? pdist(e_at(b, t2), b->p1) : b->id == 'A' ? fabs(b->sw * t2 * b->r) : 0;
        if ((b->id != 'L' && b->id != 'A') || head < 1e-12) { full = 0; n--; t2 = 1.0; e_prev(s, &e2); }
    } else {
        const Ent *a = &s->e[e1]; double piece = a->id == 'L' ? pdist(e_at(a, t2), e_at(a, t1)) : a->id == 'A' ? fabs((t2 - t1) * a->sw * a->r) : 0;
        if ((a->id != 'L' && a->id != 'A') || piece < 1e-12) { n = 0; full = 0; }
    }
    if (n <= 0) return 0;
    n = sec_count(s, e1, t1, e2, t2, &full);
    int wrap = e2 < e1 || (e1 == e2 && t2 < t1) || full;
    int old = out->n;
    if (wrap) { for (int k = e1; k < s->n; k++) sh_push(out, s->e[k]); for (int k = 0; k <= e2; k++) sh_push(out, s->e[k]); }
    else for (int k = e1; k <= e2; k++) sh_push(out, s->e[k]);
    int got = out->n - old;
    if (got == 1) out->e[old] = e_sub(&s->e[e1], t1, t2);
    else if (got > 1) {
        Ent f = s->e[e1], l = s->e[e2];
        if (f.id == 'L') f.p1 = e_at(&s->e[e1], t1); else if (f.id == 'A') { f.sa = f.sa + f.sw * t1; f.sw = f.sw - f.sw * t1; }
        if (l.id == 'L') l.p2 = e_at(&s->e[e2], t2); else if (l.id == 'A') l.sw = l.sw * t2;
        out->e[old] = f; out->e[out->n - 1] = l;
    }
    sh_upd_bbox(out);
    return got;
}
static int sh_section(const Sh *s, int e1, double t1, int e2, double t2, Sh *out) { return sh_section_full(s, e1, t1, e2, t2, out, 0); }
/* FUN_180147500 (l.176065-176218): bridge line from out's end to the section start when farther than sqrt(tol2), then the section */
static int sh_section_bridge(double tol2, const Sh *s, int e1, double t1, int e2, double t2, Sh *out) {
    int k = 0;
    if (s->n && out->n) {
        P2 P = e_end(&out->e[out->n - 1]), Q = e_at(&s->e[e1], fmin(1, fmax(0, t1)));
        if (pdist2(P, Q) > tol2) { sh_push(out, ent_line(P, Q)); k = 1; }
    }
    return k + sh_section(s, e1, t1, e2, t2, out);
}
/* FUN_180146b10 (l.175608-175635): append all entities */
static void sh_append(Sh *dst, const Sh *src) { for (int i = 0; i < src->n; i++) sh_push(dst, src->e[i]); sh_upd_bbox(dst); }
/* FUN_180147fd0 (l.176398-176501): close with a line when the gap is >= sqrt(tol2) */
static void sh_close(double tol2, Sh *s) { if (s->n && pdist2(e_end(&s->e[s->n - 1]), e_start(&s->e[0])) >= tol2) sh_push(s, ent_line(e_end(&s->e[s->n - 1]), e_start(&s->e[0]))); }

/* ---------------------------------------------------------------------------- intersections and classification (§6.11) */
typedef struct { P2 pt; int xs, xe; double xt; int ys, ye; double yt; int next, prev, cls; } Isab;   /* ISAB 0x50 */
typedef struct { Isab *r; int n, cap; } IsabL;
static void il_push(IsabL *l, Isab a) { if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 16; l->r = realloc(l->r, l->cap * sizeof(Isab)); } l->r[l->n++] = a; }
static void il_free(IsabL *l) { free(l->r); l->r = NULL; l->n = l->cap = 0; }
/* FUN_18014b720 (l.178227-178372): forward arc length along sh from (e1, t1) to (e2, t2) <= tol */
static int short_sec(const Sh *s, int e1, double t1, int e2, double t2, double tol) {
    t1 = fmin(1, fmax(0, t1)); t2 = fmin(1, fmax(0, t2));
    if (e1 == e2 && t2 >= t1) { const Ent *a = &s->e[e1]; double pc = a->id == 'L' ? pdist(e_at(a, t2), e_at(a, t1)) : a->id == 'A' ? fabs((t2 - t1) * a->sw * a->r) : 0; return pc <= tol; }
    const Ent *a = &s->e[e1];
    double acc = a->id == 'L' ? pdist(a->p2, e_at(a, t1)) : a->id == 'A' ? fabs((a->sw - a->sw * t1) * a->r) : 0;
    if (acc > tol) return 0;
    int e = e1; e_next(s, &e);
    for (int g = 0; g <= s->n + 1; g++) {
        if (e == e2) { const Ent *b = &s->e[e2]; double hd = b->id == 'L' ? pdist(e_at(b, t2), b->p1) : b->id == 'A' ? fabs(b->sw * t2 * b->r) : 0; return acc + hd <= tol; }
        acc += e_len(&s->e[e]);
        if (!(acc <= tol && e_next(s, &e))) break;
    }
    return 0;
}
static int isab_cmp_x(const void *a, const void *b) {   /* list order LAB_1800074dc -> FUN_180133630 (disasm 0x180133630-0x18013366f): +0x10 X shape, +0x14 X entity, +0x18 X t, ascending */
    const Isab *p = a, *q = b;
    if (p->xs != q->xs) return p->xs < q->xs ? -1 : 1;
    if (p->xe != q->xe) return p->xe < q->xe ? -1 : 1;
    return p->xt < q->xt ? -1 : p->xt > q->xt ? 1 : 0;
}
static _Thread_local const Isab *g_isab_base;
static int isab_cmp_y(const void *a, const void *b) {   /* link order LAB_180004e21 -> FUN_180133680 (disasm 0x180133680-0x1801336da): +0x20 Y shape, +0x24 Y entity, +0x28 Y t, ascending */
    const Isab *p = &g_isab_base[*(const int *)a], *q = &g_isab_base[*(const int *)b];
    if (p->ys != q->ys) return p->ys < q->ys ? -1 : 1;
    if (p->ye != q->ye) return p->ye < q->ye ? -1 : 1;
    return p->yt < q->yt ? -1 : p->yt > q->yt ? 1 : 0;
}
/* FUN_1801349c0 (l.165867-165972): next / prev cyclic per Y shape in the second order */
static void isab_link(IsabL *l) {
    if (!l->n) return;
    int *idx = malloc(l->n * sizeof(int)); for (int i = 0; i < l->n; i++) idx[i] = i;
    g_isab_base = l->r; qsort(idx, l->n, sizeof(int), isab_cmp_y);
    for (int g0 = 0; g0 < l->n; ) {
        int g1 = g0; while (g1 < l->n && l->r[idx[g1]].ys == l->r[idx[g0]].ys) g1++;
        for (int k = g0; k < g1; k++) { l->r[idx[k]].next = idx[k + 1 < g1 ? k + 1 : g0]; l->r[idx[k]].prev = idx[k > g0 ? k - 1 : g1 - 1]; }
        g0 = g1;
    }
    free(idx);
}
/* FUN_180134270 (l.165674-165779): FUN_180133c70 (gp_pair_isect, DLL-exact: CalcEntIntersect gp_ent_isect, SetChnMinMax boxes,
   CRT qsort, dedupe FUN_180133b30 / FUN_180134800) for every X shape i and Y shape j, records stamped X shape i (+0x10) / Y
   shape j (+0x20) and appended (FUN_18003ef50); the whole list CRT qsort by X (LAB_1800074dc), then FUN_1801349c0 links
   next / prev per Y shape (qsort_s by Y). Was our own e_isect / glibc qsort version (ct oracle buckle-v8 op 42 call 25:
   a joint record on entity 48 t = 1 instead of the DLL's 49 t = 0). */
static void gp_pair_isect(const Sh *A, const Sh *B, IsabL *out);
static void gp_msqsort(void *base, size_t num, size_t w, int (*cmp)(const void *, const void *));
static void isect(const ShL *X, const ShL *Y, IsabL *out) {
    out->n = 0;
    for (int i = 0; i < X->n; i++) for (int j = 0; j < Y->n; j++) {
        IsabL t = { 0 }; gp_pair_isect(X->s[i], Y->s[j], &t);
        for (int k = 0; k < t.n; k++) { Isab r = t.r[k]; r.xs = i; r.ys = j; il_push(out, r); }
        il_free(&t);
    }
    if (!out->n) return;
    gp_msqsort(out->r, out->n, sizeof(Isab), isab_cmp_x);
    int *idx = malloc(out->n * sizeof(int)); for (int i = 0; i < out->n; i++) idx[i] = i;
    g_isab_base = out->r; gp_msqsort(idx, out->n, sizeof(int), isab_cmp_y);
    for (int g0 = 0; g0 < out->n; ) {
        int g1 = g0; while (g1 < out->n && out->r[idx[g1]].ys == out->r[idx[g0]].ys) g1++;
        for (int k = g0; k < g1; k++) { out->r[idx[k]].next = idx[k + 1 < g1 ? k + 1 : g0]; out->r[idx[k]].prev = idx[k > g0 ? k - 1 : g1 - 1]; }
        g0 = g1;
    }
    free(idx);
}
static void isect1(const Sh *x, const ShL *Y, IsabL *out) { ShL X = { (Sh **)&x, 1, 1 }; isect(&X, Y, out); }   /* FUN_180134690 */
/* FUN_180148820 + FUN_1801489a0 (l.176621-176760): sample a section at 1/3, 2/3, 1 of each entity (the last entity's end
   is skipped); the first PtInSh result that is not 1 decides; all on the boundary -> 1; no sample -> 4 */
static int pt_in_sh(P2 p, const Sh *s, double tol);
static int sec_in_shape(const Sh *sh, int e1, double t1, int e2, double t2, int full, const Sh *S, double tol) {
    int n = (e2 < e1 || (e1 == e2 && t2 < t1) || full) ? e2 + 1 + sh->n - e1 : e2 - e1 + 1;
    int any = 0, cur = e1;
    for (int k = 0; k < n; k++, cur = (cur + 1) % sh->n) {
        double a = k == 0 ? t1 : 0, b = k == n - 1 ? t2 : 1;
        for (int st = 0; st < 3; st++) {
            if (st == 2 && k + 1 >= n) break;
            double f = st == 0 ? 0.3333333333333333 : st == 1 ? 0.6666666666666666 : 1.0;
            int r = pt_in_sh(e_at(&sh->e[cur], a + (b - a) * f), S, tol); any = 1;
            if (r != 1) return r;
        }
    }
    return any ? 1 : 4;
}
/* FUN_1801130f0 (l.148390-148438): 2 inside a non-hole shape and not in a hole before it, 1 on a boundary, else 4 */
static int sec_in_list(const Sh *sh, int e1, double t1, int e2, double t2, int full, const ShL *L, double tol) {
    int onB = 0, any1 = 0, inHole = 0;
    for (int i = 0; i < L->n; i++) {
        int r = sec_in_shape(sh, e1, t1, e2, t2, full, L->s[i], tol);
        if (r == 1) { onB = any1 = 1; }
        else if (r == 2) {
            if (sh_island(L->s[i])) inHole = 1;
            else { if (!inHole && !onB) return 2; onB = inHole = 0; }
        }
    }
    return any1 ? 1 : 4;
}
/* FUN_180149d80 (l.177357-177448): forward section length (entity wrap without closed test) */
static double sec_len(const Sh *s, int e1, double t1, int e2, double t2) {
    if (!s || !s->n) return 0;
    t1 = fmin(1, fmax(0, t1)); t2 = fmin(1, fmax(0, t2));
    if (e1 < 0) e1 = 0;
    if (e1 >= s->n) e1 = s->n - 1;
    if (e2 < 0) e2 = 0;
    if (e2 >= s->n) e2 = s->n - 1;
    int full = 0, n = sec_count(s, e1, t1, e2, t2, &full); double L = 0; int e = e1;
    for (int k = 0; k < n; k++, e = (e + 1) % s->n) {
        double l = e_len(&s->e[e]);
        if (n == 1) L += (t2 - t1) * l; else if (k == 0) L += (1 - t1) * l; else if (k == n - 1) L += t2 * l; else L += l;
    }
    return L;
}
/* FUN_180113300 (l.148467-148567): OR of the classes of sh's pieces between its crossings with L (sh cut as an open chain);
   bit 1 on a boundary, 2 inside, 4 outside */
static _Thread_local FILE *g_ct_f;
static _Thread_local int g_vs_dbg;   /* ours (ct unit oracle, CT_SMDBG): the link smoothing's call prints its crossings / sections */
static void ct_isab(const char *st, const IsabL *I);
static int sh_vs_list(const Sh *sh, const ShL *L, double tol) {
    if (!sh->n) return 0;
    IsabL I = { 0 }; isect1(sh, L, &I);
    if (g_vs_dbg) ct_isab("vsi", &I);
    int n = I.n + 2; int *ee = malloc(n * sizeof(int)); double *tt = malloc(n * sizeof(double)); int m = 0;
    if (!I.n) { ee[m] = 0; tt[m++] = 0.0; ee[m] = sh->n - 1; tt[m++] = 1.0; }
    else {
        if (sec_len(sh, 0, 0.0, I.r[0].xe, I.r[0].xt) > tol) { ee[m] = 0; tt[m++] = 0.0; }
        for (int i = 0; i < I.n; i++) { ee[m] = I.r[i].xe; tt[m++] = I.r[i].xt; }
        if (sec_len(sh, I.r[I.n - 1].xe, I.r[I.n - 1].xt, sh->n - 1, 1.0) > tol) { ee[m] = sh->n - 1; tt[m++] = 1.0; }
    }
    int r = 0;
    for (int i = 1; i < m; i++) {
        int c = sec_in_list(sh, ee[i - 1], tt[i - 1], ee[i], tt[i], 0, L, tol); r |= c;
        if (g_vs_dbg && g_ct_f) fprintf(g_ct_f, "stage vss %d %.17g %d %.17g %d\n", ee[i - 1], tt[i - 1], ee[i], tt[i], c);
    }
    free(ee); free(tt); il_free(&I);
    return r;
}
/* FUN_180112e80 (l.148298-148387): groups [holes..., outer]; 1 or 2 from the first group that contains the point, else 4 */
static int sl_inside(const ShL *l, double tol, P2 p);
static int sl_inside_grouped(const ShL *l, double tol, P2 p) {
    int i = 0;
    while (i < l->n) {
        int e = sl_group_end(l, i); if (e < 0) e = l->n;
        ShL g = { l->s + i, e - i, e - i };
        int r = sl_inside(&g, tol, p);
        if (r == 1 || r == 2) return r;
        i = e;
    }
    return 4;
}

/* ---------------------------------------------------------------------------- Clipper bridges (§3 of the plan) */
static _Thread_local double g_chord = 0.001;                  /* arc linearisation tolerance for the Clipper bridges */
/* distance equalities between offset curves (e.g. "at R - r from the material") hold only to the polygonisation error of
   the Clipper offsets: such tests use max(the engine's tolerance, 1.5 x chord tolerance) (plan §3) */
static double eqtol(double t) { return fmax(t, 1.5 * g_chord); }
static int arc_segs(double r, double sw, double tol) {
    double a = r > tol ? 2 * acos(fmax(-1, 1 - tol / r)) : M_PI / 2;
    int n = (int)ceil(fabs(sw) / fmax(a, 1e-3)); return n < 1 ? 1 : n;
}
static void cp_push(CPath *c, int *cap, P2 p) {
    if (c->n && fabs(c->xy[2 * c->n - 2] - p.x) < 1e-9 && fabs(c->xy[2 * c->n - 1] - p.y) < 1e-9) return;
    if (c->n == *cap) { *cap = *cap ? *cap * 2 : 16; c->xy = realloc(c->xy, *cap * 2 * sizeof(double)); }
    c->xy[2 * c->n] = p.x; c->xy[2 * c->n + 1] = p.y; c->n++;
}
/* shape -> polygon; outer = 1: convex arcs (sweep sign = shape direction) are replaced by their circumscribed tangent
   polygon so the polygon contains the true region (used for swept regions: inscribed chords would leave slivers of up to
   one chord tolerance at every wall) */
static CPath sh_to_cpath_o(const Sh *s, double tol, int outer) {
    CPath c = { NULL, 0 }; int cap = 0;
    for (int k = 0; k < s->n; k++) {
        const Ent *e = &s->e[k];
        if (k == 0) cp_push(&c, &cap, e_start(e));
        if (e->id == 'A') {
            int m = arc_segs(e->r, e->sw, tol);
            if (outer && s->dir != 0 && (e->sw > 0) == (s->dir > 0) && m > 1) {
                double d = e->sw / m, rr = e->r / cos(d / 2);
                for (int j = 0; j < m; j++) { double a = e->sa + d * (j + 0.5); cp_push(&c, &cap, p2(e->c.x + rr * cos(a), e->c.y + rr * sin(a))); }
                cp_push(&c, &cap, e_end(e));
            } else for (int j = 1; j <= m; j++) cp_push(&c, &cap, e_at(e, (double)j / m));
        }
        else cp_push(&c, &cap, e_end(e));
    }
    if (c.n > 1 && fabs(c.xy[0] - c.xy[2 * c.n - 2]) < 1e-9 && fabs(c.xy[1] - c.xy[2 * c.n - 1]) < 1e-9) c.n--;
    if (!c.xy) c.xy = malloc(2 * sizeof(double));
    return c;
}
static CPath sh_to_cpath(const Sh *s, double tol) { return sh_to_cpath_o(s, tol, 0); }
/* shapes -> Clipper paths with the orientation Clipper expects for non-zero filling: outer CCW, islands CW */
static CPaths sl_to_cpaths(const ShL *l, double tol) {
    CPaths r; r.n = 0; r.p = calloc(l->n ? l->n : 1, sizeof(CPath));
    for (int i = 0; i < l->n; i++) {
        if (l->s[i]->dir == 0 || l->s[i]->n == 0) continue;   /* open shapes are not regions */
        CPath c = sh_to_cpath(l->s[i], tol);
        if (c.n < 3) { free(c.xy); continue; }
        double a = cl_area(&c); int want = sh_island(l->s[i]) ? -1 : 1;
        if ((a > 0 ? 1 : -1) != want) for (int x = 0, y = c.n - 1; x < y; x++, y--) {
            double tx = c.xy[2 * x], ty = c.xy[2 * x + 1]; c.xy[2 * x] = c.xy[2 * y]; c.xy[2 * x + 1] = c.xy[2 * y + 1]; c.xy[2 * y] = tx; c.xy[2 * y + 1] = ty; }
        r.p[r.n++] = c;
    }
    return r;
}
/* Clipper region -> shapes in mcce's nest order [islands..., outer] per group (DetNestLvlShAr reading of §3 B.6:
   "list order is [islands..., outer]"); island bit = hole; dir from the orientation (outer CCW +1, island CW -1);
   the order of the groups is Clipper's (NOT DECODED in mcce: §3 B.7) */
static ShL cpaths_to_sl(const CPaths *in, double z) {
    ShL r = { 0 };
    int *own = NULL; CPaths g = cl_group(in, &own);
    for (int i = 0; i < g.n; i++) {
        if (own[i] != i) continue;   /* an outer: its holes first */
        for (int pass = 0; pass < 2; pass++)
            for (int j = 0; j < g.n; j++) {
                if (own[j] != i || (pass == 0) != (j != i)) continue;
                CPath *c = &g.p[j]; if (c->n < 3) continue;
                Sh *s = sh_new(); s->z = z;
                for (int k = 0; k < c->n; k++) { int k2 = (k + 1) % c->n;
                    sh_push(s, ent_line(p2(c->xy[2 * k], c->xy[2 * k + 1]), p2(c->xy[2 * k2], c->xy[2 * k2 + 1]))); }
                s->fl = j != i ? 1 : 0;
                s->dir = cl_area(c) > 0 ? 1 : -1;
                sh_upd_bbox(s); sl_push(&r, s);
            }
    }
    free(own); cl_free(&g);
    return r;
}
static double sl_area(const ShL *l) {   /* total area, holes subtracted (FUN_18010f0e0 reading: area of a list) */
    CPaths c = sl_to_cpaths(l, g_chord); double a = 0;
    for (int i = 0; i < c.n; i++) a += cl_area(&c.p[i]);
    cl_free(&c); return a;
}
/* MCCECalcOffset(1, d, shapes, mode, ...): region offset, d > 0 grows / d < 0 shrinks the region given by the island bits
   (sign rule decoded, formats/mcce_engines.md 6.13.1: frame trick FUN_1800d92f0 for d > 0, re-orientation FUN_1800da5e0);
   the raw-offset core FUN_1800d50d0.. and the mode bits are NOT DECODED;
   Clipper round-join offset used) */
static ShL sl_offset_cl(const ShL *l, double d, double z) {
    CPaths c = sl_to_cpaths(l, g_chord);
    CPaths o = fabs(d) < 1e-12 ? cl_bool(&c, NULL, 0) : cl_offset_nz(&c, d, g_chord);
    ShL r = cpaths_to_sl(&o, z); cl_free(&c); cl_free(&o); return r;
}
/* FUN_180153b30(tol, X, Y): X minus Y (§3 B.3 reading; internals FUN_180153390 NOT DECODED) */
static ShL sl_bool_cl(const ShL *x, const ShL *y, int op, double z) {
    CPaths a = sl_to_cpaths(x, g_chord), b = sl_to_cpaths(y, g_chord);
    CPaths o = cl_bool(&a, &b, op);
    ShL r = cpaths_to_sl(&o, z); cl_free(&a); cl_free(&b); cl_free(&o); return r;
}
static ShL sl_union_evenodd_cl(const ShL *x, double z) {
    CPaths c = sl_to_cpaths(x, g_chord), u = cl_union_evenodd(&c); ShL r = cpaths_to_sl(&u, z); cl_free(&c); cl_free(&u); return r;
}

/* ---------------------------------------------------------------------------- exact regions (exact model, plan §9)
   MCCECalcOffset / FUN_180153b30 / SoPockUnionAr keep lines and arcs (every output entity points to its source entity,
   FUN_1800da810 l.118128-118170, §6.13.1). The Clipper bridges above return polygons; fed to the exact material model
   their facets leave sub-micron wedges of material that the engine later chases (buckle-v12 op 22, plan §9). With
   In the exact model the same operations (same semantics, §3 table: non-zero fill, island bit = hole, d > 0 grows) are built
   exactly on lines and arcs - own construction (the raw-offset / boolean cores are NOT DECODED):
   - boolean: the boundaries split at all their crossings; a piece is kept (or reversed) when the fill differs on its two
     sides (points 1e-6 left / right of its middle; winding numbers by an exact ray crossing on lines and arcs);
   - offset: raw offset (lines shifted, arc radii changed by d, round joins about the vertices) split at its crossings,
     the pieces at distance >= |d| from the whole boundary and on the right side kept;
   - loops linked end to end, collinear / co-circular neighbours merged, nested as [islands..., outer].
   The loop order and start entity are Clipper's (NOT DECODED either way, §5 item 1). Any failure -> the Clipper result. */
static int exact_on(void);
/* MCCE_DEBUG level (ours, diagnostics) */
static int dbg_level(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_DEBUG"); v = e ? atoi(e) : 0; } return v; }
static _Thread_local long g_exr_n, g_exr_fb;                  /* exact region calls / fallbacks to Clipper (statistics) */
typedef struct { Ent *e; int n, cap; } EV;
typedef struct { EV *l; int n, cap; } LV;
static void ev_push(EV *v, Ent e) { if (v->n == v->cap) { v->cap = v->cap ? 2 * v->cap : 32; v->e = realloc(v->e, (size_t)v->cap * sizeof(Ent)); } v->e[v->n++] = e; }
static void lv_push(LV *v, EV l) { if (v->n == v->cap) { v->cap = v->cap ? 2 * v->cap : 16; v->l = realloc(v->l, (size_t)v->cap * sizeof(EV)); } v->l[v->n++] = l; }
static void lv_free(LV *v) { for (int i = 0; i < v->n; i++) free(v->l[i].e); free(v->l); v->l = NULL; v->n = v->cap = 0; }
static Ent ex_clean(Ent e) { Ent r = e.id == 'L' ? ent_line(e.p1, e.p2) : ent_arc(e.c, e.r, e.sa, e.sw); return r; }
static double ex_area(const EV *l) {   /* signed: 1/2 sum of x dy - y dx (arc: r^2 sw + cx dy - cy dx) */
    double a = 0;
    for (int k = 0; k < l->n; k++) { const Ent *e = &l->e[k];
        if (e->id == 'L') a += pcross(e->p1, e->p2);
        else { P2 s = e_start(e), t = e_end(e); a += e->r * e->r * e->sw + e->c.x * (t.y - s.y) - e->c.y * (t.x - s.x); } }
    return 0.5 * a;
}
static void ev_rev(EV *l) {
    for (int i = 0, j = l->n - 1; i < j; i++, j--) { Ent t = l->e[i]; l->e[i] = l->e[j]; l->e[j] = t; }
    for (int i = 0; i < l->n; i++) l->e[i] = e_rev(&l->e[i]);
}
/* tagged entity with its bbox */
typedef struct { Ent e; double bb[4]; } XE;
typedef struct { XE *x; int n, cap; } XV;
static void xv_push(XV *v, Ent e) { if (v->n == v->cap) { v->cap = v->cap ? 2 * v->cap : 64; v->x = realloc(v->x, (size_t)v->cap * sizeof(XE)); } v->x[v->n].e = e; e_bbox(&e, v->x[v->n].bb); v->n++; }
static void xv_loops(XV *v, const LV *L) { for (int i = 0; i < L->n; i++) for (int k = 0; k < L->l[i].n; k++) xv_push(v, L->l[i].e[k]); }
/* signed crossings of the ray p -> +x (half-open in y) */
static int ex_cross_pt(P2 a, P2 b, P2 p, int arcside, P2 c, double r) {
    if ((a.y > p.y) == (b.y > p.y)) return 0;
    double x;
    if (!arcside) x = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
    else { double dy = p.y - c.y, h = r * r - dy * dy; x = c.x + arcside * sqrt(h > 0 ? h : 0); }
    return x > p.x ? (b.y > a.y ? 1 : -1) : 0;
}
static int ex_cross(const XE *x, P2 p) {
    const Ent *e = &x->e;
    if (p.y < x->bb[1] || p.y > x->bb[3] || p.x > x->bb[2]) return 0;
    if (e->id == 'L') return ex_cross_pt(e->p1, e->p2, p, 0, p, 0);
    /* y-monotone pieces: split at the angles pi/2 + k pi inside the sweep */
    double a0 = e->sa, a1 = e->sa + e->sw, lo = fmin(a0, a1), hi = fmax(a0, a1);
    double cut[8]; int nc = 0;
    for (int k = (int)floor((lo - M_PI / 2) / M_PI); k <= (int)ceil((hi - M_PI / 2) / M_PI) && nc < 6; k++) { double a = M_PI / 2 + k * M_PI; if (a > lo && a < hi) cut[nc++] = a; }
    if (e->sw < 0) for (int i = 0, j = nc - 1; i < j; i++, j--) { double t = cut[i]; cut[i] = cut[j]; cut[j] = t; }
    int w = 0; double ua = a0; P2 A = e_start(e);
    for (int i = 0; i <= nc; i++) {
        double ub = i < nc ? cut[i] : a1;
        P2 B = i < nc ? p2(e->c.x, e->c.y + e->r * (sin(ub) > 0 ? 1 : -1)) : e_end(e);
        int side = cos(0.5 * (ua + ub)) >= 0 ? 1 : -1;
        w += ex_cross_pt(A, B, p, side, e->c, e->r);
        A = B; ua = ub;
    }
    return w;
}
static int ex_wind(const XV *v, P2 p) { int w = 0; for (int i = 0; i < v->n; i++) w += ex_cross(&v->x[i], p); return w; }
/* split every entity of v at its crossings / overlaps with the others; pieces longer than 1e-9 */
static void ex_split(const XV *v, EV *out) {
    double **T = calloc(v->n ? v->n : 1, sizeof(double *)); int *nt = calloc(v->n ? v->n : 1, sizeof(int)), *ct = calloc(v->n ? v->n : 1, sizeof(int));
#define EXT(i, val) do { if (nt[i] == ct[i]) { ct[i] = ct[i] ? 2 * ct[i] : 8; T[i] = realloc(T[i], (size_t)ct[i] * sizeof(double)); } T[i][nt[i]++] = (val); } while (0)
    for (int i = 0; i < v->n; i++) for (int j = i + 1; j < v->n; j++) {
        const XE *a = &v->x[i], *b = &v->x[j];
        if (a->bb[0] > b->bb[2] + 1e-7 || b->bb[0] > a->bb[2] + 1e-7 || a->bb[1] > b->bb[3] + 1e-7 || b->bb[1] > a->bb[3] + 1e-7) continue;
        /* coincident carriers (to 1e-7: offsets of offsets agree only that far): split at each other's end points only (their
           intersection is numerically meaningless) */
        int coinc = 0;
        if (a->e.id == 'L' && b->e.id == 'L') {
            P2 da = psub(a->e.p2, a->e.p1); double la = plen(da);
            if (la > 0 && fabs(pcross(da, psub(b->e.p1, a->e.p1))) / la < 1e-7 && fabs(pcross(da, psub(b->e.p2, a->e.p1))) / la < 1e-7) coinc = 1;
        } else if (a->e.id == 'A' && b->e.id == 'A') coinc = pdist(a->e.c, b->e.c) < 1e-7 && fabs(a->e.r - b->e.r) < 1e-7;
        if (coinc) {
            P2 q[2] = { e_start(&b->e), e_end(&b->e) }, s[2] = { e_start(&a->e), e_end(&a->e) };
            for (int h = 0; h < 2; h++) { double u = e_param(&a->e, q[h]); if (u > 0 && u < 1 && pdist(e_at(&a->e, u), q[h]) < 2e-7) EXT(i, u);
                                          u = e_param(&b->e, s[h]); if (u > 0 && u < 1 && pdist(e_at(&b->e, u), s[h]) < 2e-7) EXT(j, u); }
            continue;
        }
        double ta[2], tb[2]; P2 pt[2]; int k = e_isect(&a->e, &b->e, 1e-9, ta, tb, pt);
        for (int h = 0; h < k; h++) { EXT(i, ta[h]); EXT(j, tb[h]); }
    }
#undef EXT
    for (int i = 0; i < v->n; i++) {
        double *t = T[i]; int n = nt[i];
        for (int x = 1; x < n; x++) for (int y = x; y > 0 && t[y] < t[y - 1]; y--) { double s = t[y]; t[y] = t[y - 1]; t[y - 1] = s; }
        /* split points closer than 1e-7 to an end or to the previous one are merged (no piece shorter than 1e-7: crossings
           computed on different entities meet only to ~1e-8; the linker joins ends within 1e-6) */
        double L = e_len(&v->x[i].e), mt = L > 0 ? 1e-7 / L : 1, prev = 0;
        for (int k = 0; k <= n; k++) {
            double u = k < n ? t[k] : 1;
            if (k < n && (u - prev < mt || 1 - u < mt)) continue;
            Ent p = e_sub(&v->x[i].e, prev, u); if (e_len(&p) > 1e-9) ev_push(out, p);
            prev = u;
        }
        free(t);
    }
    free(T); free(nt); free(ct);
}
static void ex_tan_unit(const Ent *e, double t, P2 *u) { int ok; *u = unit(e_tan(e, t), &ok); }
/* drop repeated pieces (same ends, same middle, same direction) */
static void ex_dedupe(EV *pc) {
    int m = 0;
    for (int i = 0; i < pc->n; i++) {
        P2 a = e_start(&pc->e[i]), b = e_end(&pc->e[i]), c = e_at(&pc->e[i], 0.5); int dup = 0;
        for (int j = 0; j < m && !dup; j++) if (pdist2(e_start(&pc->e[j]), a) < 1e-12 && pdist2(e_end(&pc->e[j]), b) < 1e-12 && pdist2(e_at(&pc->e[j], 0.5), c) < 1e-12) dup = 1;
        if (!dup) pc->e[m++] = pc->e[i];
    }
    pc->n = m;
}
/* merge neighbours on the same line / circle */
static int ex_mergeable(const Ent *a, const Ent *b) {
    if (a->id != b->id) return 0;
    if (a->id == 'L') { P2 da = psub(a->p2, a->p1), db = psub(b->p2, b->p1); double la = plen(da), lb = plen(db);
        return la > 0 && lb > 0 && pdot(da, db) > 0 && fabs(pcross(da, db)) / (la * lb) < 1e-12 && fabs(pcross(da, psub(b->p2, a->p1))) / la < 1e-10; }
    return pdist(a->c, b->c) < 1e-10 && fabs(a->r - b->r) < 1e-10 && (a->sw > 0) == (b->sw > 0) && fabs(a->sw + b->sw) < 2 * M_PI - 1e-9;
}
static Ent ex_merge2(const Ent *a, const Ent *b) { Ent r = *a; if (a->id == 'L') r.p2 = b->p2; else r.sw = a->sw + b->sw; return r; }
static void ex_merge_loop(EV *l) {
    int m = 0;
    for (int i = 0; i < l->n; i++) { if (m && ex_mergeable(&l->e[m - 1], &l->e[i])) l->e[m - 1] = ex_merge2(&l->e[m - 1], &l->e[i]); else l->e[m++] = l->e[i]; }
    l->n = m;
    while (l->n > 1 && ex_mergeable(&l->e[l->n - 1], &l->e[0])) { l->e[0] = ex_merge2(&l->e[l->n - 1], &l->e[0]); l->n--; }
}
/* ours: set by the guided planner (FUN_180153b30 / SoPockUnionAr there keep every output entity a piece of one source
   entity, §6.13.1): no merge of neighbours on the same line / circle */
static _Thread_local int g_ex_keep_pieces;
/* pieces -> closed loops; 0 if a piece cannot be continued */
static int ex_link(EV *pc, LV *out) {
    const double LT = 1e-6;
    char *used = calloc(pc->n ? pc->n : 1, 1); int ok = 1;
    for (int i = 0; i < pc->n && ok; i++) {
        if (used[i]) continue;
        EV L = { 0 }; ev_push(&L, pc->e[i]); used[i] = 1;
        P2 st = e_start(&pc->e[i]), cur = e_end(&pc->e[i]);
        for (int guard = 0; pdist(cur, st) > LT; guard++) {
            if (guard > pc->n) { ok = 0; break; }
            P2 tin; ex_tan_unit(&L.e[L.n - 1], 1, &tin);
            int best = -1; double ba = -1e9;
            for (int j = 0; j < pc->n; j++) {
                if (used[j] || pdist(e_start(&pc->e[j]), cur) > LT) continue;
                P2 to; ex_tan_unit(&pc->e[j], 0, &to); double a = atan2(pcross(tin, to), pdot(tin, to));
                if (a > ba) { ba = a; best = j; }
            }
            if (best < 0) {
                ok = 0; break; }
            ev_push(&L, pc->e[best]); used[best] = 1; cur = e_end(&pc->e[best]);
        }
        if (!ok) { free(L.e); break; }
        if (!g_ex_keep_pieces) ex_merge_loop(&L);
        if (L.n && fabs(ex_area(&L)) > 1e-12) lv_push(out, L); else free(L.e);
    }
    free(used);
    return ok;
}
/* input shapes -> loops oriented by the island bit (outer counter-clockwise, island clockwise; sl_to_cpaths's rule) */
static void ex_loops_from(const ShL *l, LV *out) {
    for (int i = 0; i < l->n; i++) {
        const Sh *s = l->s[i]; if (s->dir == 0 || s->n == 0) continue;
        EV L = { 0 };
        for (int k = 0; k < s->n; k++) if ((s->e[k].id == 'L' || s->e[k].id == 'A') && e_len(&s->e[k]) > 1e-12) ev_push(&L, ex_clean(s->e[k]));
        if (L.n && pdist(e_end(&L.e[L.n - 1]), e_start(&L.e[0])) > 1e-9) ev_push(&L, ent_line(e_end(&L.e[L.n - 1]), e_start(&L.e[0])));
        double a = L.n ? ex_area(&L) : 0;
        if (fabs(a) < 1e-12) { free(L.e); continue; }
        if ((a > 0) != !sh_island(s)) ev_rev(&L);
        lv_push(out, L);
    }
}
/* fill rule of the booleans: op 0 union, 1 intersection, 2 a - b, 3 xor (non-zero, as cl_bool), 4 even-odd of a */
static int ex_fill(int op, int wa, int wb) {
    int a = wa != 0, b = wb != 0;
    switch (op) { case 0: return a || b; case 1: return a && b; case 2: return a && !b; case 3: return a != b; default: return (wa & 1) != 0; }
}
static int ex_bool_loops(const LV *A, const LV *B, int op, LV *out) {
    XV va = { 0 }, vb = { 0 }, all = { 0 }; xv_loops(&va, A); xv_loops(&vb, B); xv_loops(&all, A); xv_loops(&all, B);
    EV pc = { 0 }, keep = { 0 }; ex_split(&all, &pc);
    for (int i = 0; i < pc.n; i++) {
        P2 m = e_at(&pc.e[i], 0.5), t; ex_tan_unit(&pc.e[i], 0.5, &t); P2 nl = p2(-t.y * 1e-6, t.x * 1e-6);
        P2 qL = padd(m, nl), qR = psub(m, nl);
        int inL = ex_fill(op, ex_wind(&va, qL), ex_wind(&vb, qL)), inR = ex_fill(op, ex_wind(&va, qR), ex_wind(&vb, qR));
        if (inL && !inR) ev_push(&keep, pc.e[i]); else if (!inL && inR) ev_push(&keep, e_rev(&pc.e[i]));
    }
    ex_dedupe(&keep);
    int ok = ex_link(&keep, out);
    free(va.x); free(vb.x); free(all.x); free(pc.e); free(keep.e);
    return ok;
}
/* raw offset of one loop (interior on the left) by d (> 0 outwards): entities moved by d; at a convex vertex a round join
   about the vertex; at a concave one the two moved entities are cut at their crossing (the join there would run backwards
   and lie only ~d theta^2 / 2 inside the trimming distance at small turns); no crossing -> the join, left to the trim */
static void ex_raw_offset(const EV *L, double d, XV *raw) {
    int n = L->n; Ent *o = calloc(n, sizeof(Ent)); char *deg = calloc(n, 1); double *t0 = calloc(n, sizeof(double)), *t1 = calloc(n, sizeof(double));
    char *join = calloc(n, 1);
    for (int k = 0; k < n; k++) {
        const Ent *e = &L->e[k]; t0[k] = 0; t1[k] = 1;
        if (e->id == 'L') { int ok; P2 u = unit(psub(e->p2, e->p1), &ok), nr = p2(u.y * d, -u.x * d); o[k] = ent_line(padd(e->p1, nr), padd(e->p2, nr)); }
        else { double rr = e->r + d * (e->sw > 0 ? 1 : -1);
            if (fabs(rr) < 1e-9) { deg[k] = 1; o[k] = ent_line(e->c, e->c); }
            else o[k] = rr > 0 ? ent_arc(e->c, rr, e->sa, e->sw) : ent_arc(e->c, -rr, e->sa + M_PI, e->sw); }
    }
    for (int k = 0; k < n; k++) {   /* vertex k: end of entity k / start of entity k2 */
        int k2 = (k + 1) % n; P2 V = e_end(&L->e[k]), A = deg[k] ? o[k].p1 : e_end(&o[k]), B = deg[k2] ? o[k2].p1 : e_start(&o[k2]);
        if (pdist(A, B) <= 1e-12) continue;
        double al = atan2(A.y - V.y, A.x - V.x), be = atan2(B.y - V.y, B.x - V.x), dl = wrap_pi(be - al);
        if (fabs(fabs(dl) - M_PI) < 1e-9) dl = d > 0 ? M_PI : -M_PI;
        if (dl * d < 0 && !deg[k] && !deg[k2] && n > 1 && k2 != k) {   /* concave: cut at the crossing nearest V */
            double ta[2], tb[2], ra[2], rb[2]; P2 pt[2]; int m = e_isect_r(&o[k], &o[k2], 1e-12, ta, tb, pt, ra, rb), bi = -1; double bd = 1e300;
            for (int h = 0; h < m; h++) if (ra[h] > t0[k] + 1e-12 && rb[h] < 1 - 1e-12 && pdist(pt[h], V) < bd) { bd = pdist(pt[h], V); bi = h; }
            if (bi >= 0) { t1[k] = fmin(t1[k], ra[bi]); t0[k2] = fmax(t0[k2], rb[bi]); continue; }
        }
        join[k] = 1;
    }
    for (int k = 0; k < n; k++) {
        if (!deg[k] && t1[k] - t0[k] > 1e-12) xv_push(raw, e_sub(&o[k], t0[k], t1[k]));
        if (!join[k]) continue;
        int k2 = (k + 1) % n; P2 V = e_end(&L->e[k]), A = deg[k] ? o[k].p1 : e_end(&o[k]), B = deg[k2] ? o[k2].p1 : e_start(&o[k2]);
        double al = atan2(A.y - V.y, A.x - V.x), be = atan2(B.y - V.y, B.x - V.x), dl = wrap_pi(be - al);
        if (fabs(fabs(dl) - M_PI) < 1e-9) dl = d > 0 ? M_PI : -M_PI;
        xv_push(raw, ent_arc(V, fabs(d), al, dl));
    }
    free(o); free(deg); free(t0); free(t1); free(join);
}
static int ex_offset_loops(const LV *A0, double d, LV *out) {
    LV A = { 0 };
    if (!ex_bool_loops(A0, &(LV){ 0 }, 0, &A)) { lv_free(&A); return 0; }   /* a clean region first (input loops may overlap) */
    XV orig = { 0 }, raw = { 0 }; xv_loops(&orig, &A);
    for (int i = 0; i < A.n; i++) ex_raw_offset(&A.l[i], d, &raw);
    EV pc = { 0 }, keep = { 0 }; ex_split(&raw, &pc);
    double ad = fabs(d);
    for (int i = 0; i < pc.n; i++) {
        P2 m = e_at(&pc.e[i], 0.5); double best = 1e300;
        for (int k = 0; k < orig.n && best >= ad - 1e-8; k++) {
            const double *bb = orig.x[k].bb; double dx = fmax(0, fmax(bb[0] - m.x, m.x - bb[2])), dy = fmax(0, fmax(bb[1] - m.y, m.y - bb[3]));
            if (dx * dx + dy * dy >= best * best) continue;
            double t; P2 q; double dd = e_near_far(&orig.x[k].e, m, 0, &t, &q); if (dd < best) best = dd;
        }
        if (best < ad - 1e-8) continue;
        int in = ex_wind(&orig, m) != 0;
        if ((d > 0) == in) continue;
        ev_push(&keep, pc.e[i]);
    }
    ex_dedupe(&keep);
    int ok = ex_link(&keep, out);
    free(orig.x); free(raw.x); free(pc.e); free(keep.e); lv_free(&A);
    return ok;
}
static double ex_iou(const double *a, const double *b) {
    double ix = fmin(a[2], b[2]) - fmax(a[0], b[0]), iy = fmin(a[3], b[3]) - fmax(a[1], b[1]);
    if (ix <= 0 || iy <= 0) return 0;
    double i = ix * iy, u = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - i;
    return u > 0 ? i / u : 0;
}
/* loops -> shapes [islands..., outer] per outer; group order and start entities from the Clipper result ref */
static ShL ex_assemble(LV *L, double z, const ShL *ref) {
    int n = L->n; Sh **S = calloc(n ? n : 1, sizeof(Sh *)); double *ar = calloc(n ? n : 1, sizeof(double)); int *own = calloc(n ? n : 1, sizeof(int));
    for (int i = 0; i < n; i++) {
        Sh *s = sh_new(); s->z = z;
        for (int k = 0; k < L->l[i].n; k++) sh_push(s, ex_clean(L->l[i].e[k]));
        ar[i] = ex_area(&L->l[i]); s->fl = ar[i] < 0 ? 1 : 0; s->dir = ar[i] > 0 ? 1 : -1; sh_upd_bbox(s); S[i] = s; own[i] = -1;
    }
    for (int i = 0; i < n; i++) {   /* island -> the smallest outer around it (a point just inside the hole) */
        if (ar[i] > 0) { own[i] = i; continue; }
        P2 m = e_at(&S[i]->e[0], 0.5), t; ex_tan_unit(&S[i]->e[0], 0.5, &t); P2 q = p2(m.x + t.y * 1e-6, m.y - t.x * 1e-6);
        double ba = 1e300;
        for (int j = 0; j < n; j++) { if (ar[j] <= 0 || ar[j] >= ba) continue; XV v = { 0 }; LV one = { &L->l[j], 1, 1 }; xv_loops(&v, &one); if (ex_wind(&v, q) != 0) { ba = ar[j]; own[i] = j; } free(v.x); }
    }
    /* ref groups: outer index and bbox */
    int ng = 0, *gs = calloc(ref->n + 1, sizeof(int)), *ge = calloc(ref->n + 1, sizeof(int));
    for (int i = 0; i < ref->n; ) { int e = sl_group_end(ref, i); if (e < 0) e = ref->n; gs[ng] = i; ge[ng] = e; ng++; i = e; }
    int *key = calloc(n ? n : 1, sizeof(int));
    for (int i = 0; i < n; i++) { key[i] = ng; if (ar[i] <= 0) continue; double bi = 0; for (int g = 0; g < ng; g++) { double v = ex_iou(S[i]->bb, ref->s[ge[g] - 1]->bb); if (v > bi) { bi = v; key[i] = g; } } }
    ShL r = { 0 };
    for (int g = 0; g <= ng; g++) for (int i = 0; i < n; i++) {
        if (ar[i] <= 0 || key[i] != g) continue;
        /* members: the islands in the order of their matching Clipper islands, then the outer */
        int *mem = malloc((size_t)(n + 1) * sizeof(int)), nm = 0; double *mk = malloc((size_t)(n + 1) * sizeof(double));
        for (int j = 0; j < n; j++) if (ar[j] < 0 && own[j] == i) {
            double bv = 0, kk = 1e9 + j;
            if (g < ng) for (int k = gs[g]; k < ge[g]; k++) if (sh_island(ref->s[k])) { double v = ex_iou(S[j]->bb, ref->s[k]->bb); if (v > bv) { bv = v; kk = k; } }
            int x = nm++; while (x > 0 && mk[x - 1] > kk) { mem[x] = mem[x - 1]; mk[x] = mk[x - 1]; x--; } mem[x] = j; mk[x] = kk;
        }
        mem[nm++] = i;
        for (int q = 0; q < nm; q++) {
            int j = mem[q], pass = q < nm - 1 ? 0 : 1;
            Sh *s = S[j]; S[j] = NULL;
            if (g < ng) {   /* start entity nearest the start of the matching Clipper loop */
                int bk = -1; double bv = 0;
                for (int k = gs[g]; k < ge[g]; k++) if ((sh_island(ref->s[k]) != 0) == (pass == 0)) { double v = ex_iou(s->bb, ref->s[k]->bb); if (v > bv) { bv = v; bk = k; } }
                if (bk >= 0 && ref->s[bk]->n) {
                    P2 p0 = e_start(&ref->s[bk]->e[0]); int be = 0; double bd = 1e300;
                    for (int k = 0; k < s->n; k++) { double dd = pdist2(e_start(&s->e[k]), p0); if (dd < bd) { bd = dd; be = k; } }
                    if (be) { Ent *tmp = malloc((size_t)s->n * sizeof(Ent)); for (int k = 0; k < s->n; k++) tmp[k] = s->e[(be + k) % s->n]; memcpy(s->e, tmp, (size_t)s->n * sizeof(Ent)); free(tmp); }
                }
            }
            sl_push(&r, s);
        }
        free(mem); free(mk);
    }
    for (int i = 0; i < n; i++) sh_free(S[i]);   /* islands without an outer */
    free(S); free(ar); free(own); free(gs); free(ge); free(key);
    return r;
}
static int g_exr_dbg = -1;
static void ex_dump(const char *kind, int op, double d, const ShL *x, const ShL *y) {   /* MCCE_EXRDUMP=DIR: failing inputs */
    const char *dir = getenv("MCCE_EXRDUMP"); if (!dir) return;
    static _Thread_local int seq; char fn[512]; snprintf(fn, sizeof fn, "%s/exr_%03d.txt", dir, seq++);
    FILE *f = fopen(fn, "w"); if (!f) return;
    fprintf(f, "%s %d %.17g\n", kind, op, d);
    const ShL *ls[2] = { x, y };
    for (int w = 0; w < 2; w++) { const ShL *l = ls[w]; int n = l ? l->n : 0; fprintf(f, "set %d\n", n);
        for (int i = 0; i < n; i++) { const Sh *s = l->s[i]; fprintf(f, "shape %d %d %d\n", s->fl, s->dir, s->n);
            for (int k = 0; k < s->n; k++) { const Ent *e = &s->e[k];
                if (e->id == 'A') fprintf(f, "A %.17g %.17g %.17g %.17g %.17g\n", e->c.x, e->c.y, e->r, e->sa, e->sw);
                else fprintf(f, "L %.17g %.17g %.17g %.17g\n", e->p1.x, e->p1.y, e->p2.x, e->p2.y); } } }
    fclose(f);
}
static ShL ex_result(LV *loops, int ok, ShL ref, double z, const char *what) {
    g_exr_n++;
    if (!ok) { g_exr_fb++; if (g_exr_dbg) fprintf(stderr, "mcce exact region: %s failed, Clipper used\n", what); lv_free(loops); return ref; }
    ShL r = ex_assemble(loops, z, &ref); lv_free(loops); sl_free(&ref); return r;
}
static ShL sl_offset_m(const ShL *l, double d, double z) {
    ShL ref = sl_offset_cl(l, d, z);
    if (!exact_on()) return ref;
    if (g_exr_dbg < 0) g_exr_dbg = getenv("MCCE_DEBUG") != NULL;
    LV A = { 0 }, out = { 0 }; ex_loops_from(l, &A);
    int ok = fabs(d) < 1e-12 ? ex_bool_loops(&A, &(LV){ 0 }, 0, &out) : ex_offset_loops(&A, d, &out);
    lv_free(&A);
    if (!ok) ex_dump("offset", 0, d, l, NULL);
    return ex_result(&out, ok, ref, z, "offset");
}
static ShL sl_bool_m(const ShL *x, const ShL *y, int op, double z) {
    ShL ref = sl_bool_cl(x, y, op, z);
    if (!exact_on()) return ref;
    if (g_exr_dbg < 0) g_exr_dbg = getenv("MCCE_DEBUG") != NULL;
    LV A = { 0 }, B = { 0 }, out = { 0 }; ex_loops_from(x, &A); ex_loops_from(y, &B);
    int ok = ex_bool_loops(&A, &B, op, &out);
    lv_free(&A); lv_free(&B);
    if (!ok) ex_dump("bool", op, 0, x, y);
    return ex_result(&out, ok, ref, z, "boolean");
}
static ShL sl_minus_m(const ShL *x, const ShL *y, double z) { return sl_bool_m(x, y, 2, z); }
/* mcce.dll's own region boolean (viewer/mcce_sobool.inc via mcce_ctour.h, DLL-identical on the unit oracle
   tools/tests/sobool_oracle.sh): op MCCE_RB_MINUS = FUN_180153b30(tol, x, y), MCCE_RB_ISECT = FUN_18015b2e0, MCCE_RB_UNION =
   SoPockUnionAr(tol, x). The output entities keep the fields of the input entity they are a piece of (the DLL's section
   copy FUN_180146e20 copies the pock_chn records); dir and +0x54 (the nesting level) as the DLL sets them. Returns 0 when
   the port refuses (a path not ported, e.g. FUN_18015f790): *out is then empty. */
static _Thread_local long g_rb_n, g_rb_refused;   /* statistics (MCCE_DEBUG) */
static McceShape *rb_in(const ShL *l, int base, int *n) {
    McceShape *m = calloc((size_t)(l && l->n ? l->n : 1), sizeof *m); *n = 0;
    for (int i = 0; l && i < l->n; i++) {
        const Sh *s = l->s[i];
        if (s->dir == 0) continue;   /* CopyPockBndsToShAr makes only closed shapes here (dir = ccw ? 1 : -1) */
        McceShape *o = &m[(*n)++];
        o->e = calloc((size_t)(s->n ? s->n : 1), sizeof(McceEnt)); o->n = s->n; o->dir = s->dir; o->isl = s->fl & 1;
        for (int k = 0; k < s->n; k++) {
            const Ent *e = &s->e[k]; McceEnt *q = &o->e[k];
            q->id = e->id; q->ce = e->ce; q->src_sh = base + i; q->src_ent = k;
            if (e->id == 'A') { q->a[0] = e->c.x; q->a[1] = e->c.y; q->a[2] = e->r; q->a[3] = e->sa; q->a[4] = e->sw; }
            else { q->a[0] = e->p1.x; q->a[1] = e->p1.y; q->a[2] = e->p2.x; q->a[3] = e->p2.y; }
        }
    }
    return m;
}
static void rb_free(McceShape *m, int n) { for (int i = 0; i < n; i++) free(m[i].e); free(m); }
static int sl_bool_dll(int op, double tol, const ShL *x, const ShL *y, double z, ShL *out) {
    int nx, ny;
    McceShape *mx = rb_in(x, 0, &nx), *my = rb_in(y, x->n, &ny);
    McceResult r = mcce_region_bool(op, tol, mx, nx, my, ny, NULL);
    rb_free(mx, nx); rb_free(my, ny);
    g_rb_n++;
    memset(out, 0, sizeof *out);
    if (r.refused) {
        g_rb_refused++;
        if (dbg_level()) fprintf(stderr, "mcce: region boolean refused (%s)\n", r.refused);
        return 0;
    }
    for (int i = 0; i < r.n; i++) {
        Sh *s = sh_new(); s->z = z; s->dir = r.s[i].dir; s->fl = r.s[i].isl;
        for (int k = 0; k < r.s[i].n; k++) {
            const McceEnt *q = &r.s[i].e[k]; Ent e; memset(&e, 0, sizeof e);
            int si = q->src_sh, ek = q->src_ent;
            const ShL *src = si >= 0 && si < x->n ? x : y; int sj = si >= 0 && si < x->n ? si : si - x->n;
            if (si >= 0 && sj >= 0 && src && sj < src->n && ek >= 0 && ek < src->s[sj]->n) e = src->s[sj]->e[ek];
            e.id = q->id; e.ce = q->ce;
            if (q->id == 'A') { e.c = p2(q->a[0], q->a[1]); e.r = q->a[2]; e.sa = q->a[3]; e.sw = q->a[4]; e.p1 = e.p2 = p2(0, 0); }
            else { e.p1 = p2(q->a[0], q->a[1]); e.p2 = p2(q->a[2], q->a[3]); e.c = p2(0, 0); e.r = e.sa = e.sw = 0; }
            sh_push(s, e);
        }
        sh_upd_bbox(s);
        sl_push(out, s);
    }
    mcce_shapes_free(&r);
    return 1;
}
/* mcce.dll's own MCCECalcOffset (viewer/mcce_offset.inc via mcce_ctour.h, DLL-identical on the unit oracle tools/tests/off_oracle.sh),
   as FUN_180062a70 calls it for the region sets: MCCECalcOffset(1, d, x, out, 0x100, 0.0, 1e-6, 1e-6) (l.56628, 56710, 56713,
   56808). nest = 1: the caller's UpdDirShAr(0.001) + DetNestLvlShAr first (l.56625-56627 for out1). The output entities keep the
   fields of the input entity they come from (the source link +0x70, FUN_1800da810); dir / +0x54 (the nesting level) and CeFlags as
   the DLL sets them. Returns 0 when the port refuses (a path not ported): *out is then empty. */
static _Thread_local long g_off_calls, g_off_refused;   /* statistics (MCCE_DEBUG) */
static int dlloff_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_DLLOFF"); v = !(e && *e == '0'); } return v; }
/* MCCE_DLLRG = bit mask of the region-builder sets built with the DLL's own MCCECalcOffset (mcce_offset.inc) + FUN_180153b30
   (mcce_sobool.inc) instead of the old construction (Clipper / ours): 1 the tool-centre region RG+0x30 (FUN_18002b840), 2 A1 / A2,
   the material / bull regions (FUN_180028cb0) and the sliver filter (FUN_1800298d0), 4 C+0x3c0 / 0x3e8 (FUN_18002a2a0). Default 6
   (2026-11-13, formats/mcce_port_plan.md 9 "M2 sets"): with bit 1 too the initial material equals the DLL's (buckle-v12 op 10 / v6 op
   32 run in lockstep with mcce.dll much longer), but the NCI oracle gets much worse (median 0.072 -> 0.194, 20 ops worse) because the
   per-step consumers of RG+0x30 (the window / link sets and the material subtraction) are not DLL-exact yet: off until they are.
   User decision N13-24 (2026-11-13): default 7 (bit 1 on) together with the window sets, after the window target fix (mcce_port_plan.md 9
   "M2 window targets": 0.062 / 0.435, 27 / 37 ops commit for commit = mcce.dll); MCCE_DLLRG=6 = the old default. Bit 8 (the start regions
   FUN_18008f4a0 / FUN_180090fa0 / FUN_180090ae0 through the DLL ports) stays opt-in. */
/* MCCE_DLLWIN: the local window sets of FUN_180066ff0 (S1 / S2 / S3 / S4 on the window) and the window W with the DLL's own offset +
   boolean. On by default (user decision N13-24, superseding N13-20); MCCE_DLLWIN=0 = the old Clipper / own construction */
static int dllwin_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_DLLWIN"); v = !(e && *e == '0'); } return v; }
/* N13-25 (2026-11-13): default 15 (bit 8, the start regions, on too); MCCE_DLLRG=7 = the N13-24 default */
static int dllrg_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_DLLRG"); v = e && *e ? atoi(e) : 15; } return v; }
/* MCCE_DLLS2=1: S2 = C+0x340 minus out1 with the DLL's boolean too (sl_bool_dll), with the DLL offset on */
/* on by default (user decision N13-25); MCCE_DLLS2=0 = the Clipper difference */
static int dlls2_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_DLLS2"); v = !(e && *e == '0'); } return v; }
static int sl_offset_dllf(const ShL *x, double d, double z, int nest, unsigned flags, ShL *out);
static _Thread_local const char *g_offdump_tag;   /* ours (oracle): set around a call site to dump its MCCECalcOffset input */
static int sl_offset_dll(const ShL *x, double d, double z, int nest, ShL *out) { return sl_offset_dllf(x, d, z, nest, 0x100, out); }
/* flags as the call site passes them: 0x100 (the region / window sets, C+0x3c0 / 0x3e8), 0x127 (the region builder FUN_18002b840 /
   FUN_18002ae40 / FUN_180028cb0), 0x101 (the sliver filter's shrink, FUN_1800298d0) */
static int sl_offset_dllf(const ShL *x, double d, double z, int nest, unsigned flags, ShL *out) {
    memset(out, 0, sizeof *out);
    McceShape *m = calloc((size_t)(x->n ? x->n : 1), sizeof *m); int *map = calloc((size_t)(x->n ? x->n : 1), sizeof(int)), n = 0;
    for (int i = 0; i < x->n; i++) {
        const Sh *s = x->s[i];
        if (s->dir == 0 || !s->n) continue;   /* every set passed here is closed (CopyPockBndsToShAr: dir = ccw ? 1 : -1) */
        McceShape *o = &m[n]; map[n++] = i;
        o->e = calloc((size_t)s->n, sizeof(McceEnt)); o->n = s->n; o->dir = s->dir; o->isl = s->fl & 1;
        for (int k = 0; k < s->n; k++) {
            const Ent *e = &s->e[k]; McceEnt *q = &o->e[k];
            q->id = e->id; q->ce = e->ce;
            if (e->id == 'A') { q->a[0] = e->c.x; q->a[1] = e->c.y; q->a[2] = e->r; q->a[3] = e->sa; q->a[4] = e->sw; }
            else { q->a[0] = e->p1.x; q->a[1] = e->p1.y; q->a[2] = e->p2.x; q->a[3] = e->p2.y; }
        }
    }
    if (nest) mcce_dir_nest(m, n, map);
    if (g_offdump_tag || (getenv("MCCE_PORT_OFFDUMP_ALL") && *getenv("MCCE_PORT_OFFDUMP_ALL") == '1')) {   /* ours (oracle): MCCE_PORT_OFFDUMP=PREFIX writes this call's input as tools/peloader MCCE_OFFDUMP does (every call with MCCE_PORT_OFFDUMP_ALL=1) */
        static _Thread_local int nd; const char *pre = getenv("MCCE_PORT_OFFDUMP");
        if (pre && *pre) { char nm[512]; snprintf(nm, sizeof nm, "%s.%d.txt", pre, nd++); FILE *f = fopen(nm, "w");
            if (f) { fprintf(f, "OFF 1\n# site %s\nn 1\nd %a\nflags %x\nang %a\ntol %a %a\nX %d\n", g_offdump_tag ? g_offdump_tag : "all", d, flags, 0.0, 1e-06, 1e-06, n);
                for (int i = 0; i < n; i++) { fprintf(f, "s %d %d %d\n", m[i].dir, m[i].isl, m[i].n);
                    for (int k = 0; k < m[i].n; k++) { const McceEnt *q = &m[i].e[k];
                        if (q->id == 'A') fprintf(f, " A %a %a %a %a %a\n", q->a[0], q->a[1], q->a[2], q->a[3], q->a[4]);
                        else fprintf(f, " %c %a %a %a %a\n", q->id ? q->id : '?', q->a[0], q->a[1], q->a[2], q->a[3]); } }
                fprintf(f, "end\n"); fclose(f); } } }
    McceResult r = mcce_offset(m, n, 1, d, flags, 0.0, 1e-06, 1e-06, NULL);
    g_off_calls++;
    int ok = !r.refused;
    if (!ok) { g_off_refused++; if (dbg_level()) fprintf(stderr, "mcce: MCCECalcOffset refused (%s)\n", r.refused); }
    for (int i = 0; ok && i < r.n; i++) {
        Sh *s = sh_new(); s->z = z; s->dir = r.s[i].dir; s->fl = r.s[i].isl;
        for (int k = 0; k < r.s[i].n; k++) {
            const McceEnt *q = &r.s[i].e[k]; Ent e; memset(&e, 0, sizeof e);
            int si = q->src_sh, ek = q->src_ent;
            if (si >= 0 && si < n && ek >= 0 && ek < x->s[map[si]]->n) e = x->s[map[si]]->e[ek];
            e.id = q->id; e.ce = q->ce;
            if (q->id == 'A') { e.c = p2(q->a[0], q->a[1]); e.r = q->a[2]; e.sa = q->a[3]; e.sw = q->a[4]; e.p1 = e.p2 = p2(0, 0); }
            else { e.p1 = p2(q->a[0], q->a[1]); e.p2 = p2(q->a[2], q->a[3]); e.c = p2(0, 0); e.r = e.sa = e.sw = 0; }
            sh_push(s, e);
        }
        sh_upd_bbox(s);
        sl_push(out, s);
    }
    mcce_shapes_free(&r);
    for (int i = 0; i < n; i++) free(m[i].e);
    free(m); free(map);
    return ok;
}
/* the Clipper bridges (every other caller: link and target sets are rebuilt per step and never become material) */
static ShL sl_offset(const ShL *l, double d, double z) { return sl_offset_cl(l, d, z); }
/* MCCECalcOffset keeps the orientation of its input (exact model; Clipper mode unchanged): FUN_1800da5e0 (l.118051-118075)
   reverses each input shape to outer -1 / island +1 and marks it (CSh+0x90 |= 2); the tail FUN_1800d3b60 -> FUN_1800d3cc0
   (l.114136-114208) reverses an output shape back when every one of its source shapes (CSh+0x78 list) was marked. For an
   input oriented one way (outers d, islands -d, as every set passed here is), all sources are marked or none, so the outputs
   come back with outers d / islands -d. The Clipper bridge returns its own orientation: re-oriented to the input's. */
/* MCCE_EXSETS=1 (exact model, opt-in, 2026-11-07): the region sets of FUN_180062a70 (out1..out4: MCCECalcOffset /
   FUN_180153b30) built with the exact line / arc construction (MCCECalcOffset keeps arcs, FUN_1800da810 l.118128-118170).
   Oracle: 11 ops better, 3 worse (buckle-v8/v9 op 42 0.28 -> 0.43), CPU x1.6; mixed with the Clipper tool-centre region the
   sets disagree with it (op 42 leaves 135 instead of 52 mm of Mastercam's path unreproduced), so off by default. Not
   usable: the exact tool-centre region (buckle-v6/v7 op 36 end early, 88.7 % missed) and exact local-link window sets
   (FUN_180066ff0: up to 90 % missed) - formats/mcce_port_plan.md §9 "Status 2026-11-07 (accuracy)". */
static _Thread_local int g_exsets;
/* MCCE_EXTCR=1 (exact model, experiment): the tool-centre region RG+0x30 (B.6) and C+0x3c0 / C+0x3e8 built exactly */
static int extcr_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_EXTCR"); v = exact_on() && e && *e == '1'; } return v; }
static int exsets_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_EXSETS"); v = e && *e == '1'; } return v; }
static ShL sl_offset_k(const ShL *l, double d, double z) {
    ShL o = (g_exsets && exact_on()) ? sl_offset_m(l, d, z) : sl_offset(l, d, z);
    if (exact_on()) {
        int od = 0;
        for (int i = 0; i < l->n && !od; i++) if (l->s[i]->dir && !(l->s[i]->fl & 1)) od = l->s[i]->dir;
        for (int i = 0; i < l->n && !od; i++) if (l->s[i]->dir && (l->s[i]->fl & 1)) od = -l->s[i]->dir;
        if (od) for (int i = 0; i < o.n; i++) { Sh *s = o.s[i]; if (!s->dir) continue; int want = (s->fl & 1) ? -od : od; if (s->dir != want) sh_rev(s); }
    }
    return o;
}
static ShL sl_bool(const ShL *x, const ShL *y, int op, double z) { return sl_bool_cl(x, y, op, z); }
/* FUN_180153b30(tol, X, Y): X minus Y (§3 B.3 reading; internals FUN_180153390 NOT DECODED) */
static ShL sl_minus(const ShL *x, const ShL *y, double z) { return sl_bool_cl(x, y, 2, z); }
/* nested loops -> region, a shape inside another is its hole (DetNestLvlShAr reading; even-odd) */
static ShL sl_union_evenodd_m(const ShL *x, double z) {
    ShL ref = sl_union_evenodd_cl(x, z);
    if (!exact_on()) return ref;
    if (g_exr_dbg < 0) g_exr_dbg = getenv("MCCE_DEBUG") != NULL;
    LV A = { 0 }, out = { 0 };
    for (int i = 0; i < x->n; i++) { ShL one = { &x->s[i], 1, 1 }; Sh tmp = *x->s[i]; tmp.fl &= ~1; Sh *tp = &tmp; one.s = &tp; ex_loops_from(&one, &A); }   /* parity: orientation free */
    int ok = ex_bool_loops(&A, &(LV){ 0 }, 4, &out);
    lv_free(&A);
    return ex_result(&out, ok, ref, z, "even-odd union");
}

/* ============================================================================================ parameters */
static double rdd_(const uint8_t *r, uint32_t n, uint32_t off) { double v = 0; if (r && off + 8 <= n) memcpy(&v, r + off, 8); return v; }
static int rd8_(const uint8_t *r, uint32_t n, uint32_t off) { return r && off < n ? r[off] : 0; }
static int rd32_(const uint8_t *r, uint32_t n, uint32_t off) { int32_t v = 0; if (r && off + 4 <= n) memcpy(&v, r + off, 4); return v; }

/* mcmill LoadDynPockParm FUN_1801af380 (decomp/mcmill_hst2d_dyn.c 1497-1780). Its param_1 = operation = record - 0x68,
   so "op + X" below is record offset X + 0x68 (OPD / OPB). Tool geometry FUN_1801afa30 (4420): R = op[0x2e8] / 2,
   r = op[0x2f0]. The tool-manager refinement for tool types 0x17 / 0x19 (Pl2ConvertMCTooltoInterfacialToolConvex) / 0xc / 0x15 (dyn l.4424-4470)
   is not ported: those tool types are refused (cam2d_dyn.c hst_input_refused). */
void mcce_parm_from_record(McceParm *p, const uint8_t *rec, uint32_t n, int metric) {
#define OPD(x) rdd_(rec, n, (x) + 0x68)
#define OPB(x) rd8_(rec, n, (x) + 0x68)
    memset(p, 0, sizeof *p);                                         /* ClearCeParm */
    int style = OPB(0x11c0);
    p->flags = (style == 0 && OPB(0x14ff)) ? 0x42 : 0x12;
    if ((style == 2 ? OPB(0x56a) : OPB(0x14fc)) == 0) p->flags |= 1;
    switch (OPB(0x1653)) { case 1: p->flags |= 0x200; break; case 2: p->flags |= 0x400; break; case 3: p->flags |= 0x800; break;
                           case 4: p->flags |= 0x1000; break; case 5: p->flags |= 0x8000; break; default: p->flags |= 0x100; }
    switch (OPB(0x1651)) { case 1: p->flags |= 0x10000; break; case 2: p->flags |= 0x40000; break; case 3: p->flags |= 0x80000; break;
                           case 4: p->flags |= 0x20000; break; }
    if (OPB(0x1656)) p->flags |= 0x100000;
    if (OPB(0x1655) == 0) p->flags |= 0x2000;
    p->order_mode = OPB(0x1657) <= 2 ? OPB(0x1657) : 0;
    if (style == 2) { p->flags |= 0x200000; if (OPB(0x56a) == 0) p->flags |= 0x600000; }
    if (OPB(0x1654)) p->flags |= 0x40000000;
    if (metric) p->flags |= 8;
    if (OPB(0x171c) & 2) p->flags |= 4;
    if (style == 3) { p->start_sel = 8; p->start_x = OPD(0x14e0); p->start_y = OPD(0x14e8); }
    else p->start_sel = OPB(0x1728);
    p->R = OPD(0x2e8) * 0.5; p->r = OPD(0x2f0);                       /* FUN_1801afa30 */
    double D = OPD(0x2e8), so = (OPB(0x1698) && OPB(0x1699)) ? D * OPD(0x16c8) : D * OPD(0x1518);
    { double x = 1 - so * 0.01 / (D * 0.5); if (x < -1) x = -1; if (x > 1) x = 1; p->eng = acos(x); }   /* CalcLnEngAng(R, s) l.9016-9033 */
    p->min_rad = OPD(0x15f0) * 0.01 * D;
    p->micro_lift = OPD(0x1668);
    p->troch_r = OPD(0x1660);
    p->approach = ((style & 0xfc) == 0 && style != 1) ? OPD(0x15f8) : 0;
    p->helix_r = OPD(0x1658); p->slot_add = OPD(0x1628);
    p->plunge_ang = OPD(0x1610) * 0.01745329251994329; p->pitch = OPD(0x1618); p->zclear = OPD(0x1620);
    p->gap = OPB(0x1565) ? OPD(0x15a0) * D * 0.01 : OPD(0x1598);
    p->chain_tol = mc_systol_chain_tol(metric);                      /* chain_tol_exref: config 1001 (McSysTol; defaults metric 0.002 /
                                                                        inch 0.0001, mccore FUN_1803bc7d0) */
    {   double d6 = OPD(0x1220) + (OPB(0x2d2) ? OPD(0x2b8) : 0);
        if (d6 <= (metric ? 0.025 : 0.001) * 10.0) { double t = metric ? 0.002 : 0.0001; if (t * 10.0 < p->chain_tol) p->chain_tol = t; } }
    p->sliver = p->chain_tol * 10.0 * 2.0;                            /* 1669 */
    p->ct_t0 = OPD(0x16e0); p->ct_t1 = OPD(0x16e8); p->ct_t2 = OPD(0x16f0);
    if (OPB(0x11d9)) { p->ct_n = rd32_(rec, n, 0x1718 + 0x68); p->ct_w = OPD(0x1720); }
    p->first_off = OPD(0x1758);
    p->fp_red = (style == 0 || style == 3) ? OPD(0x1748) : 0;
    double feed = OPD(0x310);
    p->f_retract = OPD(800);
    p->f_back = OPD(0x11e0) > 1e-5 ? OPD(0x11e0) : feed;
    p->f_ramp = (OPB(0x1654) && OPD(0x1600) > 1e-5) ? OPD(0x1600) : feed;
    p->f_cut = feed;
    p->f_first = p->fp_red <= 1e-5 ? feed : (1.0 - p->fp_red * 0.01) * feed;
    p->f_conv = (OPB(0x171c) & 2) ? OPD(0x1768) : feed;
    if (p->f_conv < 1e-5) p->f_conv = feed;
    if (p->helix_r < 0) p->helix_r = 0;
    double tol = mc_systol_tol();                                     /* tol_exref = config 1000 (McSysTol; default 5e-5, FUN_1803bc7d0) */
    if (p->plunge_ang <= tol) p->plunge_ang = 0.026179938779914945;    /* 0x3f9acee9f37bebd2 = 1.5 deg */
    if (p->pitch <= tol) p->pitch = metric ? 2.54 : 0.1;
    if (p->troch_r <= tol) p->troch_r = metric ? 2.54 : 0.1;
#undef OPD
#undef OPB
}

/* FUN_180068ff0 defaults (l.60038-60070) + registry FUN_180069840 (l.60275-60380, the values Mastercam reads; with an
   installed Mastercam the key exists and these are the registry defaults) + validation FUN_18006a3b0 (l.60520-60630)
   + derivation FUN_180069560 (l.60203-60268) + march step FUN_180069350 / FUN_180068e10 (l.60161-60200, 59977-59987) */
static double ln_stepover(double th, double d) { return d * (1 - cos(th)); }   /* mcce::CalcLnStepover */
static double ln_eng_ang(double d, double s) {                                   /* mcce::CalcLnEngAng l.9016-9033 */
    double x = 1 - s / d; if (x < -1) return M_PI; if (x > 1) x = 1; return acos(x);
}
static void parm_derive(McceParm *p) {
    /* defaults */
    p->reg_read = 1; p->assert_on = 0; p->var_rad_avail = 1; p->bull_avail = 1; p->min_cut_rad = -1.0; p->rend_ang = 22.5;
    p->air_dwell = 0.01; p->write_matmod = 0; p->bands = 1; p->cut_ord_len = 0; p->air_cut_v3 = 0; p->rate_of_eng = 0;
    p->call_num = -1; p->num_red_pass = 1; p->dis_trans_ovr = p->dis_trans_tot = 0; p->old_gsl_cut_ord = p->ctour_guide = 0;
    /* registry (default values) */
    if (p->reg_read) {
        p->assert_on = 0; p->var_rad_avail = 1; p->bull_avail = 1; p->write_matmod = 0; p->min_cut_rad = -1.0; p->rend_ang = 22.5;
        p->bands = 1; p->cut_ord_len = 0; p->air_cut_v3 = 0; p->rate_of_eng = 1;
        /* bOverrideMinPathRad 0; bTurnOnMoatFirst 0; dStkPlaceErr -1 (no override) */
        p->air_dwell = 0.01;
        if (!(p->flags & 0x20000000)) { p->call_num = -1; p->num_red_pass = 1; p->dis_trans_ovr = p->R * 2.0; p->dis_trans_tot = p->R * 2.0 * 2.0; }
        p->old_gsl_cut_ord = 0;
        /* bUseCtourGuide (FUN_180069840 l.60377, registry default 0): 1 here - user decision D-3 (formats/user_decisions.md
           2026-11-07): the user's X9 NCIs follow mcce's P+0x261 = 1 branch (DLL with bUseCtourGuide = 1 vs the NCI, 37 ops p95
           median 0.065); MCCE_CTOURGUIDE=0 gives the registry default */
        { const char *e = getenv("MCCE_CTOURGUIDE"); p->ctour_guide = (e && *e) ? atoi(e) != 0 : 1; }
    }
    /* validation */
    if (p->eng < 0.1745329) p->eng = 0.17453290000000000; else if (p->eng > 2.9670597) p->eng = 2.9670597;
    { double s = ln_stepover(p->eng, p->R);
      if (fabs(p->R - p->r) < 1e-7) p->r = 0;
      if ((p->R - p->r) * 2.0 < s) p->r = 0;
      if (!p->bull_avail || p->r <= 1e-7) p->r = 0; else { if (p->r < 0) p->r = 0; if (p->R * 0.95 < p->r) p->r = p->R * 0.95; } }
    if (p->min_rad < 0.05 * p->R) p->min_rad = 0.05 * p->R;          /* GetMinRadK */
    if (p->approach < 0) p->approach = 0;
    if (p->sliver < 0.001) p->sliver = 0.001;
    if (p->plunge_ang < 0.0017453292519943296) p->plunge_ang = 0.0017453292519943296;
    if (p->pitch < 0.0001) p->pitch = 0.0001;
    if (p->ct_n < 0) p->ct_n = 0;
    if (p->ct_w < 0) p->ct_w = 0;
    if (p->fp_red < 0) p->fp_red = 0; else if (p->fp_red > 99.0) p->fp_red = 99.0;
    if (p->f_retract < 0) p->f_retract = 0;
    if (p->f_back < 0) p->f_back = 0;
    if (p->f_ramp < 0) p->f_ramp = 0;
    if (p->f_cut < 0) p->f_cut = 0;
    if (p->f_first < 0) p->f_first = 0;
    if (p->f_conv < 0) p->f_conv = 0;
    if (p->f_cut <= 1e-7) p->f_cut = p->f_conv;
    if (p->f_conv <= 1e-7) p->f_conv = p->f_cut;
    if (!(p->flags & 4)) { if (!(p->flags & 1)) p->f_cut = p->f_conv; else p->f_conv = p->f_cut; }
    if ((p->flags & 0x200000) && p->ct_t0 < 0.001) p->ct_t0 = 0.001;  /* GetMinMatThick */
    /* derivation */
    p->climb = p->flags & 1;
    p->s_10 = ln_stepover(0.1745329, p->R - p->r);
    p->s_170 = ln_stepover(2.9670597, p->R - p->r);
    p->s_025 = ln_stepover(0.004363323129985824, p->R - p->r);
    p->s_work = ln_stepover(p->eng, p->R);
    if (p->s_work < p->s_10) p->s_work = p->s_10; else if (p->s_work > p->s_170) p->s_work = p->s_170;
    p->ramp_len = p->R * 10.666;
    p->var_rad = p->R * 0.45;
    if (p->R * 0.45 < p->min_rad || !p->var_rad_avail) p->var_rad = p->min_rad;
    p->d1c0 = (p->flags & 8) ? 0.254 : 0.01;
    if (p->R * 0.125 < p->d1c0) p->d1c0 = p->R * 0.125;
    double rr = p->var_rad_avail ? p->var_rad : p->min_rad;
    p->d1c8 = p->d1c0 + rr * 0.25;
    p->d1d0 = (p->min_rad * 0.25 + p->d1c0) * 2.5;
    {   double d = p->R - p->r, rl = p->min_cut_rad;                 /* FUN_180069350 */
        if (rl < 1e-7) rl = d * 0.7;
        if (d * 0.9 < rl) rl = d * 0.9;
        if (rl < d * 0.01) rl = d * 0.01;
        p->min_cut_rad = rl;
        double a = p->rend_ang * 0.017453292519943295, chord = hypot(rl - cos(a) * rl, 0 - sin(a) * rl);
        double t = p->eng <= 1.0 ? p->eng : 1.0;                     /* FUN_180068e10 */
        double step = fabs(((t - 0.1745329) * 0.38913485000000003 / 0.8254671 + 0.61086515) * d) * 2.0 * 0.05;
        if (p->bull_avail && chord < step) step = chord;
        p->step = step; }
    p->step15 = p->step * 1.5;
    p->pi_minrad = fabs(p->min_rad * 3.141592653589793);
}

/* ============================================================================================ CCetp output */
typedef struct { Ent e; uint32_t mf; double feed; double v[4]; } Rec;
typedef struct {
    Rec *r; int n, cap;      /* every record (T+0x1c = n); the 16-slot ring T+0x40 = the last 16 */
    int last_cut;            /* T+0x20: index of the last record with move flag 0x40 */
    int fail_plunge;         /* T+0x24 (header 0x24) */
    uint32_t skipped;        /* T+0x28 (header 0x28) */
} Cetp;

/* FUN_18008a0a0 / FUN_18008a190 (l.76717-76765): feed of a record by CeFlags and move flags (§3 D 4.3) */
static double rec_feed(const McceParm *P, uint8_t ce, uint32_t mf) {
    if (ce & 1) return P->f_retract;
    if (ce & 2) return P->f_back;
    if (ce & 8) return P->f_ramp;
    if (ce & 0x20) return P->f_first;
    if (mf & 0x1400) return P->f_conv;
    return P->f_cut;
}
/* FUN_1800870f0 (l.75241-75318): append a record; arcs with a chord < 1e-4 become lines (FUN_1800874e0 l.75357-75420);
   the relative Z (+0x78) gets the cut level P+0x80 added; mf & 0x40 -> T+0x20 = this index */
static void cetp_emit(Cetp *T, const McceParm *P, const Ent *e, uint8_t ce, uint32_t mf, double v0, double v1, double v2, double v3) {
    if (T->n == T->cap) { T->cap = T->cap ? T->cap * 2 : 1024; T->r = realloc(T->r, T->cap * sizeof(Rec)); }
    Rec *r = &T->r[T->n]; memset(r, 0, sizeof *r);
    r->e = *e; r->e.ce = ce; r->e.z = e->z + P->depth;
    /* FUN_1800874e0 (l.75357-75420): an arc whose chord S-Q < 1e-4 becomes 'L' S -> Q; Q = the end for |sw| <= pi, else the
       point opposite S (c + r e(sa + pi): a full circle stays an arc; mcce_engines.md 4.2 correction) */
    if (r->e.id == 'A') {
        P2 S = e_start(e), Q = fabs(e->sw) <= 3.141592653589793 ? e_end(e) : p2(cos(e->sa + 3.141592653589793) * e->r + e->c.x, sin(e->sa + 3.141592653589793) * e->r + e->c.y);
        if (pdist(S, Q) < 1e-4) { Ent l = ent_line(S, Q); l.ce = ce; l.z = r->e.z; l.pass = e->pass; r->e = l; }
    }
    r->mf = mf; r->v[0] = v0; r->v[1] = v1; r->v[2] = v2; r->v[3] = v3;
    r->feed = rec_feed(P, ce, mf);
    if (mf & 0x40) T->last_cut = T->n;
    T->n++;
    /* FUN_18008aa50 first-pass length bookkeeping (active only with C+0x758): NOT DECODED (FUN_180089cd0, FUN_18008a510,
       FUN_180086860); the first-pass feed class still comes from the CeFlags 0x20 of each record */
}
/* FUN_18008a4a0 (l.76846-76868) */
static void mark_air_feed(int climb, uint8_t ce, uint32_t *mf) {
    if (!(ce & (1 | 2 | 8 | 0x20)) && !(*mf & (0x200 | 0x400 | 0x80 | 0x40))) *mf |= climb ? 0x800 : 0x1000;
}
/* FUN_18008a1e0 (l.76768-76841): per-entity move flags of a shape; with split the entities before the first CeFlags-2
   entity (or the first half if none) take the opposite climb sense */
static uint32_t *shape_mf(const Sh *s, int climb, int split) {
    uint32_t *mf = calloc(s->n ? s->n : 1, sizeof(uint32_t));
    int k2 = -1; for (int i = 0; i < s->n; i++) if (s->e[i].ce & 2) { k2 = i; break; }
    int lim = split ? (k2 >= 0 ? k2 : s->n / 2) : 0;
    for (int i = 0; i < s->n; i++) mark_air_feed(i < lim ? !climb : climb, s->e[i].ce, &mf[i]);
    return mf;
}
/* FUN_180087390 (l.75321-75354): emit every entity of a shape with its own CeFlags and move flags */
static void cetp_emit_shape(Cetp *T, const McceParm *P, const Sh *s, const uint32_t *mf) {
    for (int i = 0; i < s->n; i++) cetp_emit(T, P, &s->e[i], s->e[i].ce, mf ? mf[i] : 0, 0, 0, 0, 0);
}
/* FUN_18008e4b0 (l.78740-78836): end point of the newest record; no record -> end of the previous-position entity
   C+0x120 (type 0 -> none) */
static int cetp_last_pt(const Cetp *T, const Ent *prev, P2 *p) {
    if (T->n) { *p = e_end(&T->r[T->n - 1].e); return 1; }
    if (!prev || !prev->id) { *p = p2(0, 0); return 0; }
    *p = e_end(prev); return 1;
}
/* FUN_18008e710 (l.78839-79019): end tangent (unnormalised derivative) of the newest record; if degenerate (|v|^2 <=
   ~1e-14) walk back over the ring records (FUN_18011ffd0 on the 16 newest); no record -> the previous-position entity */
static P2 gp_tan_vec(const Ent *e, double t);
/* the end tangent as FUN_18008e710 computes it (l.78956-78995 newest record / l.78902-78940 previous-position entity: 'L' p2 - p1, 'A' (end - centre) |sw| turned
   by +-90 deg = gp_tan_vec); MCCE_MEASX=0: e_tan (arcs |v| = r: same direction, other last bits) */
static P2 cetp_tan1(const Ent *e) { return swx_on() ? gp_tan_vec(e, 1.0) : e_tan(e, 1); }
static int cetp_last_tan(const Cetp *T, const Ent *prev, P2 *v) {
    const double eps = 9.999999999999998e-15;
    if (T->n) {
        P2 t = cetp_tan1(&T->r[T->n - 1].e); if (pdot(t, t) > eps) { *v = t; return 1; }
        int lo = T->n >= 16 ? T->n - 16 : 0;
        for (int i = T->n - 2; i >= lo; i--) { t = cetp_tan1(&T->r[i].e); if (pdot(t, t) > eps) { *v = t; return 1; } }
        /* reread of older records from the file (FUN_18008d020): NOT DECODED; not done */
    } else if (prev && prev->id) { P2 t = cetp_tan1(prev); if (pdot(t, t) > eps) { *v = t; return 1; } }
    *v = p2(0, 0); return 0;
}
/* FUN_18008ee40 (l.79086-79099): truncate the output to k records */
static void cetp_trim(Cetp *T, int k) {
    if (k < T->n) T->n = k;
    /* FUN_18008ee40 (l.79086-79099) and its helpers FUN_18008eeb0 / FUN_18008efe0 (l.79102-79195) do not touch T+0x20: the
       last-cut index keeps pointing past the trimmed records (exact model; the Clipper mode keeps the old rescan) */
    if (exact_on()) return;
    if (T->last_cut >= T->n) { T->last_cut = -1; for (int i = T->n - 1; i >= 0; i--) if (T->r[i].mf & 0x40) { T->last_cut = i; break; } }
}

/* ============================================================================================ CCePock */
typedef struct {
    int valid;               /* G+8 */
    Sh lead;                 /* G+0x10 lead-in entities (G+0x28 / +0x30) */
    int sh, ent; double t; P2 pt;   /* G+0xa8 / +0xac / +0xb0 / +0xb8 */
} Gbap;
static void gbap_clear(Gbap *G) { G->valid = 0; G->lead.n = 0; G->sh = -1; G->ent = 0; G->t = 0; G->pt = p2(0, 0); }   /* FUN_18001cef0 */
static void gbap_copy(Gbap *d, const Gbap *s) {
    Ent *keep = d->lead.e; int cap = d->lead.cap;
    *d = *s; d->lead.e = keep; d->lead.cap = cap; d->lead.n = 0;
    for (int i = 0; i < s->lead.n; i++) sh_push(&d->lead, s->lead.e[i]);
}
static void gbap_free(Gbap *G) { free(G->lead.e); G->lead.e = NULL; G->lead.n = G->lead.cap = 0; }
typedef struct {             /* CMmCut measurement block (M1 = E+8, M2 = E+0x140; §6.1.0) */
    double len;              /* M+0x98 */
    double area_sw;          /* M+0xc8 */
    double w;                /* M+0x120 width */
    double sweep;            /* M+0x128 engaged sweep on the end cap */
    uint32_t flags;          /* M+0x130 */
    int npts;                /* engagement points (M+0xf8 list size) */
} MBlk;
typedef struct {             /* CEnCut trial record E (§3 C.0) */
    Ent e;                   /* E+0x10 step entity */
    double head;             /* E+0x98 heading change */
    MBlk m1, m2;             /* circle R - r / circle R */
    uint8_t pass;            /* E+0x13c */
} EnCut;

typedef struct {
    McceParm P;
    Cetp T;
    /* region object RG = C+0x298 (§3 B.6, §6.4.1) */
    ShL M, tcr, mat_all, bull_all, cur, c3c0, c3e8, mat, bull, s3, air;
    P2 rg3a0;                /* RG+0x3a0 */
    Ent prev;                /* C+0x120 previous position (P+0x118) */
    int memo_on, memo_idx; double memo[2];   /* C+0x710 / +0x728 / +0x718 */
    int fp_on;               /* C+0x758 */
    int dir0;                /* C+0x1b8 = original flags & 1 */
    Pos mpos;                /* material position (state locals local_2f8 ...) */
    P2 *bl; int nbl, cbl;    /* C+0x730: target points that produced no output (§6.12.4) */
    double z;                /* Z of the shapes (cut level) */
    /* local material windows RG+0x1c8 / RG+0x1f0 (FUN_18002a140 l.27270-27302, FUN_180029c30 l.27124-27263): one bag of
       entity copies each (ref_sh / ref_ent = source shape / entity), built at the start of a state-0/1 trial step and
       emptied at its end (FUN_18006db90 l.62163, l.62186-62198); win_n = number of shapes in the CVe (0 = empty -> the
       full material is used, FUN_180030de0 l.30695-30701). Exact model only. */
    Sh win1, win2; int win1_n, win2_n;
    long nmeasure;           /* statistics */
    char nd[200];            /* ours: an entry method reached a NOT DECODED piece -> the run fails with this message */
} Pock;

static void sl_orient(ShL *l, int outer_dir) {
    for (int i = 0; i < l->n; i++) { Sh *s = l->s[i]; if (s->dir == 0) continue; int want = sh_island(s) ? -outer_dir : outer_dir; if (s->dir != want) sh_rev(s); }
}
/* FUN_18002c1f0 (l.28377-28394) tool-centre region: climb -> outer +1 / island -1, else the reverse */
static void orient_tcr(uint32_t flags, ShL *l) { sl_orient(l, (flags & 1) ? 1 : -1); }
/* FUN_18002c0e0 kind 4 (l.28327-28356) material: climb -> outer -1 / island +1; else the reverse */
static void orient_mat(uint32_t flags, ShL *l) { sl_orient(l, (flags & 1) ? -1 : 1); }
/* FUN_18002c0e0 kind 2 (l.28316-28361): key 0x20000 | flags & 1: climb -> outer +1 / island -1 */
static void orient_k2(uint32_t flags, ShL *l) { sl_orient(l, (flags & 1) ? 1 : -1); }

/* ---------------------------------------------------------------------------- input preprocessing (§3 B.3) */
static ShL bnds_to_sl(const McceBnd *b, int n, double z) {
    ShL r = { 0 };
    for (int i = 0; i < n; i++) {
        if (b[i].n < 2) continue;
        Sh *s = sh_new(); s->z = z;
        int m = b[i].open ? b[i].n - 1 : b[i].n;
        for (int k = 0; k < m; k++) { int k2 = (k + 1) % b[i].n;
            sh_push(s, ent_line(p2(b[i].xy[2 * k], b[i].xy[2 * k + 1]), p2(b[i].xy[2 * k2], b[i].xy[2 * k2 + 1]))); }
        if (b[i].open) s->dir = 0;
        else { CPath c = { b[i].xy, b[i].n }; s->dir = cl_area(&c) > 0 ? 1 : -1; }   /* +1 if ccw (CopyPockBndsToShAr) */
        s->fl = b[i].island ? 1 : 0;
        sh_upd_bbox(s); sl_push(&r, s);
    }
    return r;
}
/* FUN_18008f4a0 (l.79315-79480): islands -> union -> isl; non-islands -> union -> out (as solid regions) */
static void split_isl(const ShL *A, double z, ShL *isl, ShL *out) {
    ShL a = { 0 }, b = { 0 };
    for (int i = 0; i < A->n; i++) { if (A->s[i]->dir == 0) continue; Sh *s = sh_copy(A->s[i]); int was = sh_island(s); s->fl &= ~1; sl_push(was ? &a : &b, s); }
    *isl = sl_offset_m(&a, 0, z); *out = sl_offset_m(&b, 0, z);
    sl_free(&a); sl_free(&b);
}
/* MCCE_DLLRG bit 8: the engine's start regions through the DLL ports (SoPockUnionAr / FUN_180153b30 = mcce_sobool.inc,
   MCCECalcOffset = mcce_offset.inc, DetNestLvlShAr = mcce_nest_only) instead of our own boolean / offset (sl_*_m) */
/* DetNestLvlShAr on an engine list (no UpdDirShAr): reordered, fl = the level; 0 where the port refuses (more than 64 shapes) */
static int sl_nest_dll(ShL *L) {
    int n = L->n; if (n <= 0) return 1;
    McceShape *m = calloc((size_t)n, sizeof *m); int *map = malloc((size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) {
        const Sh *x = L->s[i]; map[i] = i; m[i].n = x->n; m[i].dir = x->dir; m[i].isl = x->fl;
        m[i].e = calloc((size_t)(x->n ? x->n : 1), sizeof(McceEnt));
        for (int k = 0; k < x->n; k++) { const Ent *e = &x->e[k]; McceEnt *q = &m[i].e[k]; q->id = e->id; q->ce = e->ce;
            if (e->id == 'A') { q->a[0] = e->c.x; q->a[1] = e->c.y; q->a[2] = e->r; q->a[3] = e->sa; q->a[4] = e->sw; }
            else { q->a[0] = e->p1.x; q->a[1] = e->p1.y; q->a[2] = e->p2.x; q->a[3] = e->p2.y; } }
    }
    int r = mcce_nest_only(m, n, map);
    if (r >= 0) {
        Sh **ns = malloc((size_t)n * sizeof(Sh *));
        for (int i = 0; i < n; i++) { ns[i] = L->s[map[i]]; ns[i]->fl = m[i].isl; ns[i]->dir = m[i].dir; }
        memcpy(L->s, ns, (size_t)n * sizeof(Sh *)); free(ns);
    }
    for (int i = 0; i < n; i++) free(m[i].e);
    free(m); free(map);
    return r >= 0;
}
/* FUN_18008f4a0 (l.79315-79480): every island (+0x54 bit 0) copied with +0x54 = 0 -> SoPockUnionAr(1e-6) -> isl; the other
   shapes -> SoPockUnionAr(1e-6) -> out (l.79400-79457; no dir test) */
static int split_isl_dll(const ShL *A, double z, ShL *isl, ShL *out) {
    ShL a = { 0 }, b = { 0 }, e = { 0 };
    memset(isl, 0, sizeof *isl); memset(out, 0, sizeof *out);
    for (int i = 0; i < A->n; i++) { Sh *c = sh_copy(A->s[i]); if (sh_island(A->s[i])) { c->fl = 0; sl_push(&a, c); } else sl_push(&b, c); }
    int ok = (!a.n || sl_bool_dll(MCCE_RB_UNION, 1e-06, &a, &e, z, isl)) && (!b.n || sl_bool_dll(MCCE_RB_UNION, 1e-06, &b, &e, z, out));
    sl_free(&a); sl_free(&b);
    if (!ok) { sl_free(isl); sl_free(out); }
    return ok;
}
/* FUN_180090fa0 (l.80220-80269) START_INSIDE: M = union(outers) - union(islands) */
static ShL prep_inside(const ShL *A, double z) {
    if (dllrg_on() & 8) {   /* l.80245-80247: FUN_18008f4a0, then FUN_180153b30(1e-6, out, isl, M) */
        ShL isl, out, M;
        if (split_isl_dll(A, z, &isl, &out)) {
            int ok = sl_bool_dll(MCCE_RB_MINUS, 1e-06, &out, &isl, z, &M);
            sl_free(&isl); sl_free(&out);
            if (ok) return M;
        }
    }
    ShL isl, out; split_isl(A, z, &isl, &out);
    ShL M = sl_minus_m(&out, &isl, z); sl_free(&isl); sl_free(&out); return M;
}
static Sh *rect_sh(double x0, double y0, double x1, double y1, double z) {   /* FUN_180124c50: one CSh of 4 lines, dir -1 */
    Sh *s = sh_new(); s->z = z;
    sh_push(s, ent_line(p2(x0, y0), p2(x0, y1))); sh_push(s, ent_line(p2(x0, y1), p2(x1, y1)));
    sh_push(s, ent_line(p2(x1, y1), p2(x1, y0))); sh_push(s, ent_line(p2(x1, y0), p2(x0, y0)));
    s->dir = -1; sh_upd_bbox(s); return s;
}
/* FUN_180090ae0 (l.80034-80217) START_FACING */
static void prep_facing(const McceParm *P, const ShL *A, double z, ShL *M, ShL *AIR) {
    double rr = P->var_rad_avail ? P->var_rad : P->min_rad;
    double m = 2 * (2 * P->R + 2 * rr + P->approach + P->first_off);   /* FUN_18008f150 l.79201-79310 */
    double bb[4]; sl_bbox(A, bb);
    ShL rect = { 0 }; sl_push(&rect, rect_sh(bb[0] - m, bb[1] - m, bb[2] + m, bb[3] + m, z));
    if (dllrg_on() & 8) {   /* FUN_180090ae0 l.80140-80165 with the DLL ports */
        ShL isl, out;
        if (split_isl_dll(A, z, &isl, &out)) {
            /* M = copy(isl), each +0x54 + 1 (l.80142-80148), the rectangle appended (FUN_18011f2e0 l.80149), DetNestLvlShAr (l.80150) */
            ShL MM = sl_copy(&isl); for (int i = 0; i < MM.n; i++) MM.s[i]->fl += 1;
            sl_push(&MM, sh_copy(rect.s[0]));
            ShL g, u = { 0 }, t0, air;
            /* MCCECalcOffset(1, P+0x60, out, g, 0x127) (l.80151-80153); g + isl -> SoPockUnionAr (l.80154-80157); FUN_180153b30(rect, t0) */
            if (sl_nest_dll(&MM) && sl_offset_dllf(&out, P->first_off, z, 0, 0x127, &g)) {
                sl_move_append(&u, &g); sl_free(&g); ShL ic = sl_copy(&isl); sl_move_append(&u, &ic); sl_free(&ic);
                ShL e = { 0 };
                if (sl_bool_dll(MCCE_RB_UNION, 1e-06, &u, &e, z, &t0)) {
                    int ok = sl_bool_dll(MCCE_RB_MINUS, 1e-06, &rect, &t0, z, &air);
                    sl_free(&t0);
                    if (ok) { *M = MM; *AIR = air; sl_free(&u); sl_free(&isl); sl_free(&out); sl_free(&rect); return; }
                }
                sl_free(&u);
            }
            sl_free(&MM); sl_free(&isl); sl_free(&out);
        }
    }
    ShL isl, out; split_isl(A, z, &isl, &out);
    *M = sl_minus_m(&rect, &isl, z);                                     /* islands become outer shapes inside rect: holes */
    ShL g = sl_offset_m(&out, P->first_off, z), t0 = sl_bool_m(&g, &isl, 0, z);
    *AIR = sl_minus_m(&rect, &t0, z);
    sl_free(&rect); sl_free(&isl); sl_free(&out); sl_free(&g); sl_free(&t0);
}

/* FUN_180091100 (l.80274-80393), S2 present (Peel Mill's Material list, decomp/hst2/peel_mcmill_spec.txt 3):
   M = FUN_180090fa0(S1) (outers - islands); FUN_1800900c0 (l.79708-79761) puts every S2 shape that FUN_180162010 finds
   meeting another S2 shape into Bx, the others into Bi; E = FUN_180090fa0(Bx) + Bi (DetNestLvlShAr);
   AIR = FUN_180153b30(M, MCCECalcOffset(1, P+0x60, E, 0x127)) - so the material is M inside E grown by the first pass offset.
   FUN_180162010's internals are NOT DECODED: "meets" is read as "their boundaries cross" (isect) [inferred]; with Peel's
   single Material shape the split is not reached. */
static void prep_material(const McceParm *P, const ShL *A, const ShL *B, double z, ShL *M, ShL *AIR) {
    *M = prep_inside(A, z);
    ShL Bx = { 0 }, E = { 0 };
    for (int i = 0; i < B->n; i++) {
        int hit = 0;
        for (int j = 0; j < B->n && !hit; j++) { if (j == i) continue; IsabL I = { 0 }; ShL Y = { &B->s[j], 1, 1 }; isect1(B->s[i], &Y, &I); hit = I.n > 0; il_free(&I); }
        Sh *c = sh_copy(B->s[i]);
        if (hit) sl_push(&Bx, c); else { c->fl &= ~1; sl_push(&E, c); }
    }
    if (Bx.n) { ShL ex = prep_inside(&Bx, z); sl_move_append(&E, &ex); sl_free(&ex); }
    /* DetNestLvlShAr: nesting by geometry (a shape inside another becomes its hole) = the even-odd region */
    ShL Eu = sl_union_evenodd_m(&E, z);
    ShL g = sl_offset_m(&Eu, P->first_off, z);
    *AIR = sl_minus_m(M, &g, z);
    sl_free(&Bx); sl_free(&E); sl_free(&Eu); sl_free(&g);
}

/* FUN_180092950 (l.81078-81359): Dynamic Contour regions; closed walls via FUN_180091d40 (l.80681-80839) and
   FUN_1800922e0 (l.80883-81075). Open chains (FUN_1800930c0 with the contour band kernels of §6.5, MCCECalcContourOffset
   internals NOT DECODED) are not ported: they are skipped. */
static void prep_ctour(const McceParm *P, const ShL *A, double z, ShL *M, ShL *AIR) {
    int sep = (P->flags & 0x200000) && !(P->flags & 0x800000);    /* IsEaShSepCtour (l.848) */
    double W = P->ct_n * P->ct_w, rad = P->min_rad + P->R, t = P->ct_t0 + P->ct_t1 + P->ct_t2, u = P->ct_t1 + P->ct_t2;
    double rr = P->var_rad_avail ? P->var_rad : P->min_rad;
    double Pp = 2 * (P->R + rr) + 2 * P->R + P->ct_t1 + P->ct_t0 + P->ct_t2 + P->approach;   /* FUN_180069120 (l.60075-60088) */
    ShL L = { 0 };
    if (sep) { for (int i = 0; i < A->n; i++) if (A->s[i]->dir) { Sh *c = sh_copy(A->s[i]); c->fl &= ~1; sl_push(&L, c); } }
    else L = prep_inside(A, z);
    ShL REG = { 0 }, WALL = { 0 };
    for (int i = 0; i < L.n; ) {
        int e = sep ? i + 1 : sl_group_end(&L, i); if (e < 0) e = L.n;
        ShL G = { L.s + i, e - i, e - i };
        int left = sep ? (((P->flags & 0x400000) && G.s[0]->dir == 1) || (!(P->flags & 0x400000) && G.s[0]->dir == -1)) : 1;
        double c = left ? 1 : -1;
        ShL o1 = sl_offset_m(&G, -(W + rad) * c, z), O = sl_offset_m(&o1, rad * c, z); sl_free(&o1);
        for (int k = 0; k < O.n; ) {
            int ke = sl_group_end(&O, k); if (ke < 0) ke = O.n;
            ShL K = { O.s + k, ke - k, ke - k };
            ShL t1a = sl_offset_m(&K, -t * c, z), T1 = sl_offset_m(&t1a, u * c, z); sl_free(&t1a);
            /* FUN_1800922e0 param_4 (the caller's local_70 = REG, l.81268-81270; M = local_70, AIR = local_70 - offset(local_c0), l.81276-81287; formats/mcce_engines.md 6.13.3) gets nest(K u offset(K, -P c)) (l.80980-80996)
               or K itself with CTOUR_POCKET; param_5 (local_c0 = WALL) gets nest(K u T1) (l.81007-81012). (The earlier reading
               had the two swapped, which left AIR = REG - grown WALL empty and the band as thin as t: "nothing to cut".) */
            if (!(P->flags & 0x800000)) { ShL kp = sl_offset_m(&K, -Pp * c, z), w = sl_bool_m(&K, &kp, 3, z); sl_move_append(&REG, &w); sl_free(&w); sl_free(&kp); }
            else { ShL w = sl_copy(&K); sl_move_append(&REG, &w); sl_free(&w); }
            ShL r = sl_bool_m(&K, &T1, 3, z);   /* nest(K u T1): the band between the two loops */
            sl_move_append(&WALL, &r); sl_free(&r); sl_free(&T1);
            k = ke;
        }
        sl_free(&O);
        i = e;
    }
    ShL wg = sl_offset_m(&WALL, P->first_off, z);
    *AIR = sl_minus_m(&REG, &wg, z);
    *M = REG;
    sl_free(&wg); sl_free(&WALL); sl_free(&L);
}

/* ---------------------------------------------------------------------------- region builder (§3 B.6) and next region (B.7) */
/* FUN_1800298d0 (l.27050-27120): unless CTOUR_ON, drop slivers thinner than 2h, h = 0.5 x P+0x58 */
static int dllrg_on(void);
static void sliver_filter(const McceParm *P, ShL *mat, double z) {
    if (P->flags & 0x200000) return;
    double h = 0.5 * P->sliver;
    if (h < 1e-12) return;
    if (dllrg_on() & 2) {   /* l.27075-27084: MCCECalcOffset(1, -h, mat, tmp, 0x101, ..), then MCCECalcOffset(1, h, tmp, mat, 0x100, ..) - the DLL's own */
        ShL a, b;
        if (sl_offset_dllf(mat, -h, z, 0, 0x101, &a)) {
            int ok = sl_offset_dllf(&a, h, z, 0, 0x100, &b);
            sl_free(&a);
            if (ok) { sl_free(mat); *mat = b; return; }
        }
    }
    ShL a = sl_offset_m(mat, -h, z), b = sl_offset_m(&a, h, z);
    sl_free(&a); sl_free(mat); *mat = b;
}
static void regions_build(Pock *C) {
    McceParm *P = &C->P;
    double d = P->min_rad + ((P->flags & 0x2000000) ? 0 : P->R);       /* l.28158-28162 */
    /* RG+0x3a0: start point of the first entity of the last non-island shape of M (l.28118-28151) */
    for (int i = C->M.n - 1; i >= 0; i--) if (!sh_island(C->M.s[i]) && C->M.s[i]->n) { C->rg3a0 = e_start(&C->M.s[i]->e[0]); break; }
    /* the tool-centre region stays on the Clipper bridge in the exact model too: built exactly, the link / target sets derived
       from it per step (Clipper, region_sets / local_link) no longer agree with it and the engine stalls (buckle-v6 op 36:
       2865 iterations, 253 mm2 left; formats/TODO.md) */
    int dll = dllrg_on(), tdone = 0;
    if (dll & 1) {   /* FUN_18002b840 l.28163-28171: MCCECalcOffset(1, -d, RG+8, tmp, 0x127, ..); non-empty: MCCECalcOffset(1, P+0x48, tmp, RG+0x30,
                    0x100, ..); FUN_1800d96d0(RG+0x30, 1) - the DLL's own offset (mcce_offset.inc); where it refuses, the old construction */
        ShL a;
        if (sl_offset_dllf(&C->M, -d, C->z, 0, 0x127, &a)) {
            if (!a.n) tdone = 1;
            else if (sl_offset_dllf(&a, P->min_rad, C->z, 0, 0x100, &C->tcr)) {
                tdone = 1;
                for (int k = 0; k < C->tcr.n; k++) for (int j = 0; j < C->tcr.s[k]->n; j++) C->tcr.s[k]->e[j].ce = 0;   /* FUN_1800d96d0 l.117594-117640: CeFlags 0 */
            }
            sl_free(&a);
        }
    }
    if (!tdone) { int ex = extcr_on();
      ShL a = ex ? sl_offset_m(&C->M, -d, C->z) : sl_offset(&C->M, -d, C->z); C->tcr = ex ? sl_offset_m(&a, P->min_rad, C->z) : sl_offset(&a, P->min_rad, C->z); sl_free(&a); }
    /* FUN_1800d96d0(RG+0x30, 1): CeFlags 0, +0x60 / +0x70 = 0 (§6.1.9): entities are created clean here */
    orient_tcr(P->flags, &C->tcr);
    /* FUN_18002ae40 (l.27668-28003) */
    ShL A1 = { 0 }, A2 = { 0 };
    int bnd = (P->flags & 0x2000000) != 0;
    /* l.27756-27770: MCCECalcOffset(1, .., RG+0x240, .., 0x127, ..) - the DLL's own where it does not refuse */
    if (C->air.n && (!(dll & 2) || !sl_offset_dllf(&C->air, bnd ? P->R - P->r : -P->r, C->z, 0, 0x127, &A1))) A1 = sl_offset_m(&C->air, bnd ? P->R - P->r : -P->r, C->z);
    if (C->air.n && P->r > 1e-7 && (!(dll & 2) || !sl_offset_dllf(&C->air, bnd ? P->R : 0, C->z, 0, 0x127, &A2))) A2 = sl_offset_m(&C->air, bnd ? P->R : 0, C->z);
    /* MOAT_FIRST preparation FUN_18002be40 / FUN_18002c360: NOT DECODED (flag never set: registry default 0) */
    ShL keep = { 0 };
    int i = 0, tag = 0;
    while (i < C->tcr.n) {
        int e = sl_group_end(&C->tcr, i); if (e < 0) e = C->tcr.n;
        ShL G = { 0 }; for (int k = i; k < e; k++) sl_push(&G, sh_copy(C->tcr.s[k]));
        tag++;
        /* FUN_180028cb0 (l.26664-26751) */
        /* l.26700-26703: MCCECalcOffset(1, max(R - r - 1e-4, 0), G, tmp, 0x127, ..), FUN_180153b30(1e-6, tmp, A1, MAT) - the DLL's own
           offset and boolean (mcce_offset.inc / mcce_sobool.inc) where they do not refuse */
        ShL g1, MAT; int gd = 0;
        if ((dll & 2) && sl_offset_dllf(&G, fmax(P->R - P->r - 1e-4, 0), C->z, 0, 0x127, &g1)) {
            gd = sl_bool_dll(MCCE_RB_MINUS, 1e-06, &g1, &A1, C->z, &MAT);
            sl_free(&g1);
        }
        if (!gd) { g1 = sl_offset_m(&G, fmax(P->R - P->r - 1e-4, 0), C->z); MAT = sl_minus_m(&g1, &A1, C->z); sl_free(&g1); }
        sliver_filter(P, &MAT, C->z);
        orient_mat(P->flags, &MAT);
        for (int k = 0; k < MAT.n; k++) { MAT.s[k]->tag = tag; for (int j = 0; j < MAT.s[k]->n; j++) MAT.s[k]->e[j].pass = 0; }
        if (MAT.n) {
            for (int k = 0; k < G.n; k++) sl_push(&keep, sh_copy(G.s[k]));
            sl_move_append(&C->mat_all, &MAT);
            if (P->r > 1e-7) {
                /* l.27942: FUN_180028cb0 with P+0x30 = R, so the offset is max(R - 1e-4, 0) (l.26696-26701); the old construction used R */
                ShL g2, B; int bd = 0;
                if ((dll & 2) && sl_offset_dllf(&G, fmax(P->R - 1e-4, 0), C->z, 0, 0x127, &g2)) {
                    bd = sl_bool_dll(MCCE_RB_MINUS, 1e-06, &g2, &A2, C->z, &B);
                    sl_free(&g2);
                }
                if (!bd) { g2 = sl_offset_m(&G, P->R, C->z); B = sl_minus_m(&g2, &A2, C->z); sl_free(&g2); }
                sliver_filter(P, &B, C->z); orient_mat(P->flags, &B);
                for (int k = 0; k < B.n; k++) B.s[k]->tag = tag;
                sl_move_append(&C->bull_all, &B); sl_free(&B);
            }
        }
        sl_free(&MAT); sl_free(&G);
        i = e;
    }
    /* keep the groups that have material, tagged in the same order */
    sl_free(&C->tcr); C->tcr = keep;
    if (dbg_level() >= 3) {
        fprintf(stderr, "regions_build: M %d shapes, tcr %d shapes (kept), material %d shapes\n", C->M.n, C->tcr.n, C->mat_all.n);
        for (int k = 0; k < C->M.n; k++) fprintf(stderr, "  M[%d] %s dir %d n %d bb %.3f,%.3f-%.3f,%.3f\n", k, sh_island(C->M.s[k]) ? "isl" : "out", C->M.s[k]->dir, C->M.s[k]->n, C->M.s[k]->bb[0], C->M.s[k]->bb[1], C->M.s[k]->bb[2], C->M.s[k]->bb[3]);
        for (int k = 0; k < C->tcr.n; k++) fprintf(stderr, "  tcr[%d] %s n %d bb %.3f,%.3f-%.3f,%.3f\n", k, sh_island(C->tcr.s[k]) ? "isl" : "out", C->tcr.s[k]->n, C->tcr.s[k]->bb[0], C->tcr.s[k]->bb[1], C->tcr.s[k]->bb[2], C->tcr.s[k]->bb[3]);
        for (int k = 0; k < C->mat_all.n; k++) fprintf(stderr, "  mat[%d] tag %d %s n %d bb %.3f,%.3f-%.3f,%.3f\n", k, C->mat_all.s[k]->tag, sh_island(C->mat_all.s[k]) ? "isl" : "out", C->mat_all.s[k]->n, C->mat_all.s[k]->bb[0], C->mat_all.s[k]->bb[1], C->mat_all.s[k]->bb[2], C->mat_all.s[k]->bb[3]);
        if (dbg_level() >= 7) {   /* MATDUMP lines: shape, sampled points (for the comparison scripts) */
            const ShL *Ls[3] = { &C->mat_all, &C->tcr, &C->air }; const char *nm[3] = { "mat", "tcr", "air" };
            for (int w = 0; w < 3; w++) for (int k = 0; k < Ls[w]->n; k++) for (int j = 0; j < Ls[w]->s[k]->n; j++) for (int q = 0; q < 8; q++) {
                P2 p = e_at(&Ls[w]->s[k]->e[j], q / 8.0); fprintf(stderr, "MATDUMP %s %d %.5f %.5f\n", nm[w], k, p.x, p.y); }
        }
    }
    { int t = 0; for (int k = 0; k < C->tcr.n; k++) { if (k == 0 || !sh_island(C->tcr.s[k - 1])) t++; C->tcr.s[k]->tag = 0; } (void)t; }
    sl_free(&A1); sl_free(&A2);
}
/* FUN_18002a2a0 (l.27305-27665): move the next region to the current lists */
static int region_next(Pock *C) {
    McceParm *P = &C->P;
    sl_clear(&C->cur); sl_clear(&C->mat); sl_clear(&C->bull); sl_clear(&C->c3c0); sl_clear(&C->c3e8);
    if (!C->tcr.n) return 0;
    int e = sl_group_end(&C->tcr, 0); if (e < 0) e = C->tcr.n;
    for (int k = 0; k < e; k++) sl_push(&C->cur, C->tcr.s[k]);
    memmove(C->tcr.s, C->tcr.s + e, (C->tcr.n - e) * sizeof(Sh *)); C->tcr.n -= e;
    /* CBands per current shape (bIsBandsAvail, FUN_180143d20): NOT DECODED construction; not built */
    if (C->mat_all.n) {
        int tag = C->mat_all.s[0]->tag, k = 0;
        while (k < C->mat_all.n && C->mat_all.s[k]->tag == tag) { C->mat_all.s[k]->tag = 0; sl_push(&C->mat, C->mat_all.s[k]); k++; }
        if (C->mat_all.n > k) memmove(C->mat_all.s, C->mat_all.s + k, (C->mat_all.n - k) * sizeof(Sh *)); C->mat_all.n -= k;
        k = 0;
        while (k < C->bull_all.n && C->bull_all.s[k]->tag == tag) { C->bull_all.s[k]->tag = 0; sl_push(&C->bull, C->bull_all.s[k]); k++; }
        if (C->bull_all.n > k) memmove(C->bull_all.s, C->bull_all.s + k, (C->bull_all.n - k) * sizeof(Sh *)); C->bull_all.n -= k;
    }
    /* l.27643-27651: MCCECalcOffset(1, max(R - r - 1e-4, 0), C+0xa8, C+0x150, 0x100, ..) and MCCECalcOffset(1, -P+0x48, C+0xa8, C+0x128,
       0x100, ..) - the DLL's own offset where it does not refuse */
    g_exsets = extcr_on();
    int dll = dllrg_on() & 4;
    if (!dll || !sl_offset_dllf(&C->cur, fmax(P->R - P->r - 1e-4, 0), C->z, 0, 0x100, &C->c3e8)) C->c3e8 = sl_offset_k(&C->cur, fmax(P->R - P->r - 1e-4, 0), C->z);
    if (!dll || !sl_offset_dllf(&C->cur, -P->min_rad, C->z, 0, 0x100, &C->c3c0)) C->c3c0 = sl_offset_k(&C->cur, -P->min_rad, C->z);
    g_exsets = 0;
    return C->mat.n && C->cur.n;
}

/* ============================================================================================ material model (§6.1) */
static void drop_slivers(ShL *l);
/* FUN_1800321e0 (l.31198-31571) / line case FUN_180032200 (l.31574-31767): swept region of one step with circle radius
   rad. Returns 0 on failure (flag 8 in the caller). The end cap at E carries CeFlags 1. */
/* ours: MCCE_MEASX=0 = the measure before its bit-exact form (2026-11-13): the swept region's cap angles (FUN_180032200) and
   arc-parameter wrap (FUN_1800321e0), the crossings by e_isect_r instead of CalcEntIntersect (gp_ent_isect) */
static int swx_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_MEASX"); v = !(e && *e == '0'); } return v; }
/* FUN_1800321e0 after CalcEntIntersect: an arc parameter t > 1 is taken back by one turn (angle sw t wrapped into
   [-2 pi, 2 pi], then -/+ 2 pi, over sw) */
static double sw_wrap_t(double t, double sw) {
    if (!(1.0 < t)) return t;
    for (t = sw * t; 6.283185307179586 < t; t = t + -6.283185307179586) ;
    for (; t < -6.283185307179586; t = t + 6.283185307179586) ;
    return (t + (0.0 <= t ? -6.283185307179586 : 6.283185307179586)) / sw;
}
static int gp_ent_isect(double tol, const Ent *a, const Ent *b, double ta[2], double tb[2], P2 pt[2]);   /* mcce::CalcEntIntersect (below) */
static int swept_region(uint32_t flags, double rad, const Ent *st, Sh *out) {
    sh_clear(out);
    if (st->id == 'L') {
        P2 S = st->p1, E = st->p2, d = psub(E, S); double L = plen(d);
        if (L < 1e-12) return 0;
        P2 n = pmul(p2(d.y, -d.x), rad / L);                          /* right normal (l.31699-31700) */
        /* the cap angles from the scaled normal n, as FUN_180032200 does (atan2(-n.y, -n.x) / atan2(n.y, n.x)); atan2(d.x, -d.y)
           of the unscaled direction can differ in the last bit (MCCE_MEASX=0: the old form) */
        int swx = swx_on();
        sh_push(out, ent_arc(S, rad, swx ? atan2(-n.y, -n.x) : atan2(d.x, -d.y), M_PI));   /* e0: from the left normal, +pi */
        sh_push(out, ent_line(padd(S, n), padd(E, n)));                /* e1 */
        Ent e2 = ent_arc(E, rad, swx ? atan2(n.y, n.x) : atan2(-d.x, d.y), M_PI); e2.ce = 1;   /* e2: from the right normal, end cap */
        sh_push(out, e2);
        sh_push(out, ent_line(psub(E, n), psub(S, n)));                /* e3 */
    } else if (st->id == 'A') {
        P2 c = st->c; double rho = st->r, sa = st->sa, sw = st->sw;
        P2 S = e_start(st), E = e_end(st);
        if (sw > 0) {                                                  /* l.31446-31557 */
            Ent e0 = ent_arc(S, rad, sa + M_PI, M_PI), e1 = ent_arc(c, rho + rad, sa, sw), e2 = ent_arc(E, rad, sa + sw, M_PI), e3 = ent_arc(c, rho - rad, sa + sw, -sw);
            e2.ce = 1;
            if (rho <= rad) {
                e3.r = rad - rho; e3.sa = sa + sw + M_PI;
                double t0[2], t2[2]; P2 x[2]; int k = swx_on() ? gp_ent_isect(1e-6, &e0, &e2, t0, t2, x) : e_isect(&e0, &e2, 1e-6, t0, t2, x);   /* mcce::CalcEntIntersect(1e-6, e0, e2) */
                if (k == 0) return 0;
                int j = 0;
                if (k == 2) { P2 mid = e_at(&e3, 0.5); j = pdist2(x[1], mid) < pdist2(x[0], mid) ? 1 : 0; }
                e2 = e_cut(&e2, t2[j]);
                if (swx_on()) t0[j] = sw_wrap_t(t0[j], e0.sw);       /* l.31526-31537 */
                e0.sa += e0.sw * t0[j]; e0.sw -= e0.sw * t0[j];
                sh_push(out, e0); sh_push(out, e1); sh_push(out, e2);    /* entity 3 erased */
            } else { sh_push(out, e0); sh_push(out, e1); sh_push(out, e2); sh_push(out, e3); }
        } else {                                                       /* l.31341-31444 */
            Ent e0 = ent_arc(S, rad, sa, M_PI), e1 = ent_arc(c, rho - rad, sa, sw), e2 = ent_arc(E, rad, sa + sw + M_PI, M_PI), e3 = ent_arc(c, rho + rad, sa + sw, -sw);
            e2.ce = 1;
            if (rho - rad < 0) {
                e1.r = rad - rho; e1.sa = sa + M_PI;
                double t0[2], t2[2]; P2 x[2]; int k = swx_on() ? gp_ent_isect(1e-6, &e0, &e2, t0, t2, x) : e_isect(&e0, &e2, 1e-6, t0, t2, x);   /* mcce::CalcEntIntersect(1e-6, e0, e2) */
                if (k == 0) return 0;
                int j = 0;
                if (k == 2) { P2 mid = e_at(&e1, 0.5); j = pdist2(x[1], mid) < pdist2(x[0], mid) ? 1 : 0; }
                e0 = e_cut(&e0, t0[j]);                               /* e0 end cut */
                if (swx_on()) t2[j] = sw_wrap_t(t2[j], e2.sw);       /* l.31416-31427 */
                e2.sa += e2.sw * t2[j]; e2.sw -= e2.sw * t2[j];        /* e2 start trimmed */
                sh_push(out, e0); sh_push(out, e2); sh_push(out, e3);  /* entity 1 erased */
            } else { sh_push(out, e0); sh_push(out, e1); sh_push(out, e2); sh_push(out, e3); }
        }
    } else return 0;
    out->dir = 1; out->fl = 1;
    if (!(flags & 1)) {                                               /* rotate to start at entity 1, then RevSh */
        Ent first = out->e[0]; memmove(out->e, out->e + 1, (out->n - 1) * sizeof(Ent)); out->e[out->n - 1] = first;
        sh_rev(out);
    }
    sh_upd_bbox(out);
    return 1;
}

/* indices of the nest groups of a list whose outer bbox overlaps bb (slack 1e-7) */
static int groups_overlap(const ShL *l, const double bb[4], int *gs, int *ge, int max) {
    int n = 0, i = 0;
    while (i < l->n && n < max) {
        int e = sl_group_end(l, i); if (e < 0) e = l->n;
        const Sh *o = l->s[e - 1];
        if (o->bb[0] - 1e-7 <= bb[2] && bb[0] <= o->bb[2] + 1e-7 && o->bb[1] - 1e-7 <= bb[3] && bb[1] <= o->bb[3] + 1e-7) { gs[n] = i; ge[n] = e; n++; }
        i = e;
    }
    return n;
}
static CPaths groups_cpaths(const ShL *l, const int *gs, const int *ge, int ng) {
    ShL t = { 0 };
    for (int g = 0; g < ng; g++) for (int k = gs[g]; k < ge[g]; k++) sl_push(&t, l->s[k]);
    CPaths c = sl_to_cpaths(&t, g_chord);
    free(t.s);
    return c;
}
#define MAXG 4096
static _Thread_local int g_gs[MAXG], g_ge[MAXG];

/* ours, pure speed-up (no engine rule): measure_block tessellated every material shape again on every trial (some 12
   trials per committed move, the Brent search of FUN_18001aa70). Each shape's sl_to_cpaths polygon and its Clipper Y
   (cl_int, = to_paths) are kept here and reused only when the shape is byte-for-byte what it was (entities, dir, fl and
   the chord), so a hit gives exactly the polygon sl_to_cpaths would. Shapes are changed in place (sh_rev, sh_push ...)
   and freed pointers come back, hence the content check rather than a pointer key alone. */
typedef struct { const Sh *s; Ent *e; int n, dir, fl; double chord; CPath c; long long *iy; } TessC;
#define TESS_N 2048
static _Thread_local TessC g_tess[TESS_N];
/* g_tess_pin[h] = the groups_cpaths_c call that lent out slot h. Slots are direct-mapped by the shape pointer, so two
   shapes of one material set can land on the same slot; refilling it for the second one freed the polygon the first
   one's path still pointed at (heap-use-after-free, ASan: tess_get free -> crop_band_y / cl_bool read). Clipper then
   got freed memory ("the chain geometry has invalid coordinates"), and which op failed depended only on where malloc
   put the shapes. A lent slot is now never refilled within the call: the colliding shape gets its own polygon, which
   the caller frees. */
static _Thread_local unsigned g_tess_call, g_tess_pin[TESS_N];
static long long cl_int(double v);
static void tess_reset(void) {
    for (int i = 0; i < TESS_N; i++) { free(g_tess[i].e); free(g_tess[i].c.xy); free(g_tess[i].iy); }
    memset(g_tess, 0, sizeof g_tess); memset(g_tess_pin, 0, sizeof g_tess_pin); g_tess_call = 0;
}
static int tess_hit(const TessC *t, const Sh *s) {
    return t->s == s && t->n == s->n && t->dir == s->dir && t->fl == s->fl && t->chord == g_chord && !memcmp(t->e, s->e, sizeof(Ent) * (size_t)s->n);
}
/* the Clipper Y of a path's vertices, then (ours, crop_band_y's index) for each block b of CROP_BS edges (b CROP_BS .. b CROP_BS + CROP_BS - 1,
   vertices b CROP_BS .. b CROP_BS + CROP_BS, the last one wrapping to 0) the least and the greatest of those Y: [n Y][nb min][nb max] */
#define CROP_BS 32
static long long cl_int(double v);
static long long *iy_alloc(const CPath *c) {
    int n = c->n, nb = (n + CROP_BS - 1) / CROP_BS;
    long long *y = malloc(sizeof(long long) * (size_t)(n + 2 * nb + 1));
    for (int k = 0; k < n; k++) y[k] = cl_int(c->xy[2 * k + 1]);
    long long *mn = y + n, *mx = y + n + nb;
    for (int b = 0; b < nb; b++) { long long a = LLONG_MAX, z = LLONG_MIN;
        for (int k = b * CROP_BS; k <= b * CROP_BS + CROP_BS && k <= n; k++) { long long v = y[k < n ? k : 0]; if (v < a) a = v; if (v > z) z = v; }
        mn[b] = a; mx[b] = z; }
    return y;
}
static void tess_fill(TessC *t, const Sh *s) {                  /* t empty: s's polygon, its Clipper Y and the key */
    ShL one = { (Sh **)&s, 1, 1 };
    CPaths r = sl_to_cpaths(&one, g_chord);                      /* the same call groups_cpaths makes, one shape */
    if (r.n) { t->c = r.p[0]; t->iy = iy_alloc(&t->c); }
    free(r.p);
    t->s = s; t->n = s->n; t->dir = s->dir; t->fl = s->fl; t->chord = g_chord;
    t->e = malloc(sizeof(Ent) * (size_t)(s->n ? s->n : 1)); memcpy(t->e, s->e, sizeof(Ent) * (size_t)s->n);
}
/* = groups_cpaths, the polygons borrowed from the cache (free only .p) + their Clipper Y; (*own)[i] = 1: path i is not
   the cache's (its slot was already lent in this call), the caller frees its xy and (*iy)[i] too */
static CPaths groups_cpaths_c(const ShL *l, const int *gs, const int *ge, int ng, const long long ***iy, char **own) {
    int m = 0; for (int g = 0; g < ng; g++) m += ge[g] - gs[g];
    CPaths c; c.n = 0; c.p = calloc(m ? (size_t)m : 1, sizeof(CPath)); *iy = calloc(m ? (size_t)m : 1, sizeof(long long *));
    *own = calloc(m ? (size_t)m : 1, 1);
    if (++g_tess_call == 0) { memset(g_tess_pin, 0, sizeof g_tess_pin); g_tess_call = 1; }
    for (int g = 0; g < ng; g++) for (int k = gs[g]; k < ge[g]; k++) {
        const Sh *s = l->s[k];
        size_t h = ((size_t)s >> 4) * 2654435761u % TESS_N;
        TessC *t = &g_tess[h];
        int hit = tess_hit(t, s);
        if (!hit && g_tess_pin[h] == g_tess_call) {             /* slot lent to an earlier shape of this call: a private copy */
            TessC tmp; memset(&tmp, 0, sizeof tmp); tess_fill(&tmp, s); free(tmp.e);
            if (!tmp.c.n) { free(tmp.c.xy); free(tmp.iy); continue; }
            (*own)[c.n] = 1; (*iy)[c.n] = tmp.iy; c.p[c.n++] = tmp.c;
            continue;
        }
        if (!hit) { free(t->e); free(t->c.xy); free(t->iy); memset(t, 0, sizeof *t); tess_fill(t, s); }
        g_tess_pin[h] = g_tess_call;
        if (!t->c.n) continue;
        (*iy)[c.n] = t->iy; c.p[c.n++] = t->c;
    }
    return c;
}

/* ours, pure speed-up of the Clipper bridge (no engine rule; formats/qt_port_plan.md "Dynamic measure_block speed-up"):
   measure_block clips a whole material group against one small swept polygon on every trial, so the cost grows with the
   length of the material boundary. crop_band hands Clipper only the material edges that matter, such that Clipper's
   output is bit-identical (not just the same region):
   - Clipper 6 sweeps horizontal scanbeams whose boundaries are the Y values of the input vertices, and the rounding of
     the intersections depends on them (IntersectPoint clamps to the beam, clipper.cpp l.677-696). Let [L, H] be the
     integer Y range (Clipper units, the llround(y x 1e5) of to_paths) of the clip polygon +- CROP_M, widened to the
     nearest material vertex Y below / above it: every ring edge whose Y span meets [L, H] is kept verbatim, so every
     scanbeam that meets the clip polygon has the same boundaries, edges and winding counts;
   - a run of the other edges (it stays wholly above H or wholly below L: consecutive edges share a vertex) from a to b
     is shortened to a -> r1 -> rn -> b (r1 / rn = its first / last inner vertex). The closed loop run + shortcut lies in
     a half-plane that misses the band, so the winding number of every band point is unchanged, and a, b keep their
     original neighbours. A run is kept whole when r1 = rn, a = r1 or rn = b, or a, r1, rn / r1, rn, b are collinear in
     integers (Clipper's AddPath would merge them and give a or b other neighbours, clipper.cpp l.1107-1131);
   - a ring with no edge in the band lies in one half-plane: winding 0 in the band, dropped.
   MCCE_CROP_CHECK=1 runs every trial both ways and compares the raw Clipper outputs bit for bit (count on stderr). */
#define CROP_M 0.01
static _Thread_local long g_crop_n, g_crop_bad;
static void cp_add(CPath *c, int *cap, const double *xy) {
    if (c->n == *cap) { *cap = *cap ? *cap * 2 : 16; c->xy = realloc(c->xy, (size_t)*cap * 2 * sizeof(double)); }
    c->xy[2 * c->n] = xy[0]; c->xy[2 * c->n + 1] = xy[1]; c->n++;
}
/* llround (half away from zero) without the libm call (ours, speed-up): exact for |v| < 2^52 - the fraction v - trunc(v) is exact there */
static inline long long llround_ex(double v) {
    if (!(v > -4503599627370496.0 && v < 4503599627370496.0)) return llround(v);
    long long t = (long long)v; double f = v - (double)t;
    return f >= 0.5 ? t + 1 : f <= -0.5 ? t - 1 : t;
}
static long long cl_int(double v) { return llround_ex(v * 1e5); }   /* = to_paths (offset.cpp) */
static int int_collinear(const double *p, const double *q, const double *r) {
    __int128 ax = cl_int(q[0]) - cl_int(p[0]), ay = cl_int(q[1]) - cl_int(p[1]), bx = cl_int(r[0]) - cl_int(q[0]), by = cl_int(r[1]) - cl_int(q[1]);
    return ax * by == ay * bx;
}
static int int_same(const double *p, const double *q) { return cl_int(p[0]) == cl_int(q[0]) && cl_int(p[1]) == cl_int(q[1]); }
static CPaths crop_band_y(const CPaths *mp, const CPath *clip, const long long *const *iy);
static CPaths crop_band(const CPaths *mp, const CPath *clip) { return crop_band_y(mp, clip, NULL); }
static CPaths crop_band_lin(const CPaths *mp, const CPath *clip, const long long *const *iy);
static int cpaths_same(const CPaths *a, const CPaths *b);
static int crop_check(void);
/* ours, speed-up only (2026-11-07): crop_band_lin's output, with the per-block Y range of each cached path (iy_alloc) - blocks wholly
   below L / above H are passed over without visiting their edges (no edge of such a block meets the band, and no vertex of a block
   wholly on one side of lo / hi can be the nearest one on the other side). MCCE_CROP_CHECK=2 compares it with crop_band_lin. */
static int crop_out(long long a, long long b, long long L, long long H) { return (a < b ? a : b) > H || (a > b ? a : b) < L; }
static CPaths crop_band_y(const CPaths *mp, const CPath *clip, const long long *const *iy) {   /* iy: the Clipper Y per path, or NULL */
    if (!iy) return crop_band_lin(mp, clip, iy);
    long long lo = LLONG_MAX, hi = LLONG_MIN;
    for (int k = 0; k < clip->n; k++) { long long y = cl_int(clip->xy[2 * k + 1]); if (y < lo) lo = y; if (y > hi) hi = y; }
    lo -= (long long)(CROP_M * 1e5); hi += (long long)(CROP_M * 1e5);
    long long L = lo, H = hi, yl = LLONG_MIN, yh = LLONG_MAX;
    for (int i = 0; i < mp->n; i++) {
        int n = mp->p[i].n; if (n <= 0) continue;
        const long long *y = iy[i]; int nb = (n + CROP_BS - 1) / CROP_BS; const long long *mn = y + n, *mx = y + n + nb;
        for (int b = 0; b < nb; b++) {
            int k1 = b * CROP_BS, k2 = k1 + CROP_BS < n ? k1 + CROP_BS : n;   /* the block's own vertices k1 .. k2 - 1 */
            if (mn[b] >= lo && mx[b] <= hi) continue;            /* every vertex in [lo, hi] */
            if (mx[b] < lo) { if (mx[b] > yl) yl = mx[b]; continue; }   /* all below lo (the range's vertices are vertices of the ring) */
            if (mn[b] > hi) { if (mn[b] < yh) yh = mn[b]; continue; }
            for (int k = k1; k < k2; k++) { long long v = y[k]; if (v < lo && v > yl) yl = v; if (v > hi && v < yh) yh = v; }
        }
    }
    if (yl != LLONG_MIN) L = yl;
    if (yh != LLONG_MAX) H = yh;
    CPaths r; r.n = 0; r.p = calloc(mp->n > 0 ? (size_t)mp->n : 1, sizeof(CPath));
    for (int i = 0; i < mp->n; i++) {
        const CPath *p = &mp->p[i]; const double *v = p->xy; int n = p->n;
        if (n <= 0) continue;
        const long long *y = iy[i]; int nb = (n + CROP_BS - 1) / CROP_BS; const long long *mn = y + n, *mx = y + n + nb;
        #define EIN(k) (!crop_out(y[k], y[(k) + 1 < n ? (k) + 1 : 0], L, H))
        #define BOUT(k) (mn[(k) / CROP_BS] > H || mx[(k) / CROP_BS] < L)
        int k0 = -1;                                              /* the first kept edge in ring order */
        for (int k = 0; k < n && k0 < 0; ) { if (BOUT(k)) { k = ((k / CROP_BS) + 1) * CROP_BS; continue; } if (EIN(k)) k0 = k; else k++; }
        if (k0 < 0) continue;                                     /* winding 0 in the band */
        CPath c = { NULL, 0 }; int cap = 0;
        int j = 0;
        while (j < n) {
            int k = (k0 + j) % n;
            if (EIN(k)) { cp_add(&c, &cap, v + 2 * k); j++; continue; }
            int j1 = j;
            while (j < n) {                                       /* the run of edges out of the band */
                int q = (k0 + j) % n;
                if (BOUT(q)) { int end = ((q / CROP_BS) + 1) * CROP_BS; if (end > n) end = n; int step = end - q; if (j + step > n) step = n - j; j += step; continue; }
                if (EIN(q)) break;
                j++;
            }
            int na = (k0 + j1) % n, nb2 = (k0 + j) % n, ninner = j - j1 - 1;
            const double *A = v + 2 * na, *B = v + 2 * nb2, *R1 = v + 2 * ((na + 1) % n), *RN = v + 2 * ((nb2 + n - 1) % n);
            int whole = ninner <= 2 || int_same(R1, RN) || int_same(A, R1) || int_same(RN, B) || int_collinear(A, R1, RN) || int_collinear(R1, RN, B);
            if (whole) for (int q = j1; q < j; q++) cp_add(&c, &cap, v + 2 * ((k0 + q) % n));
            else { cp_add(&c, &cap, A); cp_add(&c, &cap, R1); cp_add(&c, &cap, RN); }
        }
        #undef EIN
        #undef BOUT
        r.p[r.n++] = c;
    }
    if (crop_check() == 2) {                              /* the reference: crop_band_lin */
        CPaths o = crop_band_lin(mp, clip, iy); g_crop_n++; if (!cpaths_same(&o, &r)) g_crop_bad++;
        for (int i = 0; i < o.n; i++) free(o.p[i].xy); free(o.p);
    }
    return r;
}
static CPaths crop_band_lin(const CPaths *mp, const CPath *clip, const long long *const *iy) {   /* the plain scan (2026-10-23) */
    long long lo = LLONG_MAX, hi = LLONG_MIN;
    for (int k = 0; k < clip->n; k++) { long long y = cl_int(clip->xy[2 * k + 1]); if (y < lo) lo = y; if (y > hi) hi = y; }
    lo -= (long long)(CROP_M * 1e5); hi += (long long)(CROP_M * 1e5);
    long long L = lo, H = hi, yl = LLONG_MIN, yh = LLONG_MAX;
    size_t nv = 0; for (int i = 0; i < mp->n; i++) if (mp->p[i].n > 0) nv += (size_t)mp->p[i].n;
    long long *ys = malloc((nv ? nv : 1) * sizeof(long long)), *yp = ys;   /* the Clipper Y of every vertex */
    for (int i = 0; i < mp->n; i++) for (int k = 0; k < mp->p[i].n; k++) {
        long long y = *yp++ = iy ? iy[i][k] : cl_int(mp->p[i].xy[2 * k + 1]);
        if (y < lo && y > yl) yl = y;
        if (y > hi && y < yh) yh = y;
    }
    yp = ys;
    if (yl != LLONG_MIN) L = yl;
    if (yh != LLONG_MAX) H = yh;
    CPaths r; r.n = 0; r.p = calloc(mp->n > 0 ? (size_t)mp->n : 1, sizeof(CPath));
    char *in = NULL; int icap = 0;
    for (int i = 0; i < mp->n; i++) {
        const CPath *p = &mp->p[i]; const double *v = p->xy; int n = p->n, any = 0;
        if (n <= 0) continue;
        const long long *y = yp; yp += n;
        if (n + 1 > icap) { icap = n + 1; in = realloc(in, (size_t)icap); }
        for (int k = 0; k < n; k++) {
            long long y0 = y[k], y1 = y[k + 1 < n ? k + 1 : 0];
            in[k] = (y0 < y1 ? y0 : y1) <= H && (y0 > y1 ? y0 : y1) >= L;
            any |= in[k];
        }
        if (!any) continue;                                       /* winding 0 in the band */
        int k0 = 0; while (!in[k0]) k0++;                         /* start at a kept edge, in ring order */
        CPath c = { NULL, 0 }; int cap = 0;
        int j = 0;
        while (j < n) {
            int k = (k0 + j) % n;
            if (in[k]) { cp_add(&c, &cap, v + 2 * k); j++; continue; }
            int j1 = j; while (j < n && !in[(k0 + j) % n]) j++;   /* run of edges j1 .. j - 1: vertices a = k, inner, b */
            int na = (k0 + j1) % n, nb = (k0 + j) % n, ninner = j - j1 - 1;
            const double *A = v + 2 * na, *B = v + 2 * nb, *R1 = v + 2 * ((na + 1) % n), *RN = v + 2 * ((nb + n - 1) % n);
            int whole = ninner <= 2 || int_same(R1, RN) || int_same(A, R1) || int_same(RN, B) || int_collinear(A, R1, RN) || int_collinear(R1, RN, B);
            if (whole) for (int q = j1; q < j; q++) cp_add(&c, &cap, v + 2 * ((k0 + q) % n));
            else { cp_add(&c, &cap, A); cp_add(&c, &cap, R1); cp_add(&c, &cap, RN); }
        }
        r.p[r.n++] = c;
    }
    free(in); free(ys);
    return r;
}
static int cpaths_same(const CPaths *a, const CPaths *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++)
        if (a->p[i].n != b->p[i].n || memcmp(a->p[i].xy, b->p[i].xy, sizeof(double) * 2 * a->p[i].n)) return 0;
    return 1;
}
static int g_crop_check = -1;
static void crop_report(void) { fprintf(stderr, "MCCE_CROP_CHECK: %ld clips compared, %ld differ\n", g_crop_n, g_crop_bad); }
static int crop_check(void) {
    if (g_crop_check < 0) { int v = getenv("MCCE_CROP_CHECK") ? atoi(getenv("MCCE_CROP_CHECK")) : 0; g_crop_check = v > 0 ? v : 0; if (g_crop_check) atexit(crop_report); }
    return g_crop_check;
}

/* ============================================================================================ exact material model (§6.1) */
/* Exact material model (formats/mcce_port_plan.md §9; the default since 2026-11-07, user decision D-2): Mastercam's own measure and commit on
   the exact loops instead of the Clipper bridges (substitution 8): crossings of the swept outline with the material entities
   (in the local window of a state-0/1 trial step), sections classified by samples, loops rebuilt from material and outline
   pieces; the material regions themselves built exactly (sl_offset_m ...). Kernels (entity intersection, section copy, point
   in shape) are ours (plan §3). The classifier's step frame (FUN_180034ac0) is decoded (§6.1.2): evaluated here by dot
   products with the same axes. MCCE_MEASURE=clipper (or MCCE_EXACT=0) gives the Clipper bridges;
   MCCE_MEASURE=exact / MCCE_EXACT=1 the exact model. */
static int exact_on(void) {
    static _Thread_local int v = -1;
    if (v < 0) {
        const char *m = getenv("MCCE_MEASURE"), *e = getenv("MCCE_EXACT");
        if (m && *m) v = strcmp(m, "clipper") != 0;
        else if (e && *e) v = *e != '0';
        else v = 1;   /* default: the exact model (user decision D-2, formats/user_decisions.md 2026-11-07) */
    }
    return v;
}
/* ISEC record (0x50, §6.1.0): far = ours (a shape whose bbox misses the swept region by more than 1e-3: its marker is
   class 0 without sampling - every sample of it classifies 4, so FUN_1800337e0 leaves it at 0) */
typedef struct { P2 pt; int sh, ent; double t; int swe; double swt; int nx, pv, cls; char mk, far; int nsh, nent; double nt; } Isec;
typedef struct { Isec *r; int n, cap; } IsL;
static void is_push(IsL *L, Isec x) { if (L->n == L->cap) { L->cap = L->cap ? 2 * L->cap : 64; L->r = realloc(L->r, (size_t)L->cap * sizeof(Isec)); } L->r[L->n++] = x; }
static void is_del(IsL *L, int i) { memmove(L->r + i, L->r + i + 1, (size_t)(L->n - i - 1) * sizeof(Isec)); L->n--; }   /* FUN_180032900 */
static void is_mkmarker(Isec *r) { r->ent = -1; r->swe = -1; r->nx = r->pv = -1; r->mk = 1; }   /* FUN_180033b30 (l.32330-32343) */
static Isec is_marker(int sh) { Isec x; memset(&x, 0, sizeof x); x.sh = sh; x.nsh = -1; is_mkmarker(&x); return x; }
/* nextSame FUN_180034720 / prevSame FUN_180034890 (the list is grouped by shape) */
static int is_next_same(const IsL *L, int i) { int s = L->r[i].sh; if (i + 1 < L->n && L->r[i + 1].sh == s) return i + 1; int k = i; while (k > 0 && L->r[k - 1].sh == s) k--; return k; }
static int is_prev_same(const IsL *L, int i) { int s = L->r[i].sh; if (i > 0 && L->r[i - 1].sh == s) return i - 1; int k = i; while (k + 1 < L->n && L->r[k + 1].sh == s) k++; return k; }
static int is_next_cls0(const IsL *L, int *i) { for (int k = *i + 1; k < L->n; k++) if (L->r[k].cls == 0) { *i = k; return 1; } return 0; }   /* FUN_1800347e0 */
static void is_posA(const IsL *L, int i, int *e, double *t) { if (L->r[i].mk) { *e = 0; *t = 0.0; } else { *e = L->r[i].ent; *t = L->r[i].t; } }   /* FUN_1800349a0 */
static void is_posB(const ShL *mat, const IsL *L, int i, int *e, double *t) { if (L->r[i].mk) { *e = mat->s[L->r[i].sh]->n - 1; *t = 1.0; } else { *e = L->r[i].ent; *t = L->r[i].t; } }   /* FUN_180034a00 */
static int is_chain(const IsL *L, int i) {   /* FUN_1800346a0 */
    int j = is_next_same(L, i), k = L->r[j].nx;
    while (k >= 0 && k != j && L->r[k].cls == 2) k = L->r[k].nx;
    return k;
}
static int is_cmp_ent(const void *a, const void *b) {   /* LAB_180002969 = FUN_1800314a0: shape, entity, t ascending */
    const Isec *x = a, *y = b;
    if (x->sh != y->sh) return x->sh < y->sh ? -1 : 1;
    if (x->ent != y->ent) return x->ent < y->ent ? -1 : 1;
    return x->t < y->t ? -1 : x->t > y->t ? 1 : 0;
}
static _Thread_local const Isec *g_is_base;
static int is_cmp_sw(const void *a, const void *b) {   /* LAB_180005c72 = FUN_180031280: swept entity, t ascending */
    const Isec *x = &g_is_base[*(const int *)a], *y = &g_is_base[*(const int *)b];
    if (x->swe != y->swe) return x->swe < y->swe ? -1 : 1;
    return x->swt < y->swt ? -1 : x->swt > y->swt ? 1 : 0;
}
/* the classifier frame FUN_180034ac0 + classify FUN_180035b40 (§6.1.2); line frame: origin S + o, x along the step,
   y across from the o side. Decoded from the disassembly (0x180034d7c-0x180034e4b): M = TranslationMx(-(S + o)) .
   RotationMx(wrap(pi/2 - atan2(-o.y, -o.x))) [. ReflectionMx(1) = x -> -x when dir == 1], row vectors (CTMatrix::operator*=
   l.3414 = M . X, operator*(CPt2) l.5284); the rotation takes -o to +y. Ours: the same axes by dot products (u = the step's
   unit vector, ny = -o / rad), equal up to rounding. */
typedef struct { int arc; P2 S, E, u, O, ny; double rad, len, lo98; P2 c; double sa, sw, d0, d8, e0, e8; } Clf;
static int clf_make(int dir, double rad, const Ent *st, Clf *F) {
    memset(F, 0, sizeof *F);
    F->S = e_start(st); F->E = e_end(st); F->rad = rad;
    if (st->id == 'L') {
        P2 d = psub(st->p2, st->p1); double L = plen(d);
        if (L < 1e-6) return 0;                                       /* l.32947 */
        P2 o = dir == 1 ? pmul(p2(-d.y, d.x), rad / L) : pmul(p2(d.y, -d.x), rad / L);
        F->len = L; F->u = pmul(d, 1 / L); F->O = padd(F->S, o); F->ny = pmul(o, -1 / rad);
        F->lo98 = dir ? -2e-4 : -1e-4;                                /* l.32996-33015 */
        return 1;
    }
    if (st->id == 'A') {
        F->arc = 1; F->c = st->c; F->sa = wrap_pi(st->sa); F->sw = st->sw;
        double a = st->r + rad, b1 = st->r - rad + 1e-4, b2 = st->r - rad - 1e-4;
        F->d0 = (a + 1e-4) * (a + 1e-4); F->d8 = (a - 1e-4) * (a - 1e-4);
        F->e0 = (b1 < 0 ? -1 : 1) * b1 * b1; F->e8 = (b2 < 0 ? -1 : 1) * b2 * b2;
        return 1;
    }
    return 0;
}
static int clf(const Clf *F, P2 p) {   /* 2 inside, 1 on the band, 4 outside */
    double r = F->rad, dE2 = pdist2(p, F->E), dS2 = pdist2(p, F->S), ri = (r - 1e-4) * (r - 1e-4), ro = (r + 1e-4) * (r + 1e-4);
    if (dE2 < ri || dS2 < ri) return 2;
    if (!F->arc) {
        double x = pdot(psub(p, F->S), F->u), y = pdot(psub(p, F->O), F->ny);
        if (1e-4 < x && x < F->len - 1e-4 && 1e-4 < y && y < 2 * r - 1e-4) return 2;
        if (dE2 <= ro || dS2 <= ro || (-1e-4 < x && x < F->len + 1e-4 && F->lo98 < y && y < 2 * r + 1e-4)) return 1;
        return 4;
    }
    double d2 = pdist2(p, F->c);
    if (!(F->e8 <= d2 && d2 <= F->d0)) return 4;
    int strict = !(d2 >= F->d8 || d2 <= F->e0);
    double a = wrap_pi(atan2(p.y - F->c.y, p.x - F->c.x) - F->sa);
    if (F->sw * a < 0) a += F->sw > 0 ? 2 * M_PI : -2 * M_PI;
    if (fabs(F->sw) <= fabs(a)) return (dS2 > ro && dE2 > ro) ? 4 : 1;
    return strict ? 2 : 1;
}
static int bb_ov(const double a[4], const double b[4], double s) { return a[0] - s <= b[2] && b[0] <= a[2] + s && a[1] - s <= b[3] && b[1] <= a[3] + s; }
/* the crossings of one material entity e (shape s, entity k) with the swept outline, sorted (FUN_180032b30 l.31986-32043 /
   FUN_1800331a0 l.32128-32206): CalcEntIntersect tol 1e-5 per swept entity whose bbox meets e's within 1e-7, hits pushed
   last-first, then the records added for e qsorted with LAB_180002969 */
static void ex_cross_ent(const Ent *e, int s, int k, const Sh *sw, const double (*wb)[4], IsL *L) {
    double eb[4]; e_qbox(e, eb);
    if (!bb_ov(eb, sw->bb, 1e-7)) return;   /* ours: quick reject on the containing box first */
    if (e->id == 'A') { e_bbox(e, eb); if (!bb_ov(eb, sw->bb, 1e-7)) return; }
    int k0 = L->n;
    for (int w = 0; w < sw->n; w++) {
        if (!bb_ov(eb, wb[w], 1e-7)) continue;
        double tE[2], tW[2], rE[2], rW[2]; P2 x[2];
        int c = swx_on() ? gp_ent_isect(1e-5, e, &sw->e[w], rE, rW, x)   /* mcce::CalcEntIntersect(1e-5, mat ent, swept ent): FUN_180032b30 l.32009, FUN_1800331a0 l.32173 */
                         : e_isect_r(e, &sw->e[w], 1e-5, tE, tW, x, rE, rW);
        for (int h = c; h >= 1; h--) { Isec r; memset(&r, 0, sizeof r); r.pt = x[h - 1]; r.sh = s; r.ent = k; r.t = rE[h - 1]; r.swe = w; r.swt = rW[h - 1]; r.nx = r.pv = -1; r.nsh = -1; is_push(L, r); }
    }
    if (L->n - k0 > 1) qsort(L->r + k0, (size_t)(L->n - k0), sizeof(Isec), is_cmp_ent);
}
/* crossings: full material FUN_180032b30 (win NULL) or the local window FUN_1800331a0 (l.32059-32219: the window's entity
   copies, reported with their source shape / entity +0x80 / +0x0c); then markers FUN_18002fa30 (l.30045-30094): every
   material shape index without a record gets a marker, appended at the END of the list (FUN_180039c70 l.35401-35460 =
   push_back), in ascending shape order */
static void ex_crossings(const ShL *mat, const Sh *win, const Sh *sw, IsL *L) {
    L->n = 0;
    double (*wb)[4] = malloc(sizeof(double[4]) * (size_t)(sw->n ? sw->n : 1));
    for (int w = 0; w < sw->n; w++) e_bbox(&sw->e[w], wb[w]);
    if (win) {
        for (int q = 0; q < win->n; q++) ex_cross_ent(&win->e[q], win->e[q].ref_sh, win->e[q].ref_ent, sw, (const double (*)[4])wb, L);
    } else {
        for (int s = 0; s < mat->n; s++) {
            const Sh *m = mat->s[s];
            if (!bb_ov(m->bb, sw->bb, 1e-7)) continue;   /* ours: no entity of the shape can pass the per-entity test */
            for (int k = 0; k < m->n; k++) ex_cross_ent(&m->e[k], s, k, sw, (const double (*)[4])wb, L);
        }
    }
    free(wb);
    int n = L->n, i = 0;
    for (int s = 0; s < mat->n; s++) {
        if (i < n && L->r[i].sh == s) { while (i < n && L->r[i].sh == s) i++; continue; }
        Isec mk = is_marker(s); mk.far = !bb_ov(mat->s[s]->bb, sw->bb, 1e-3); is_push(L, mk);
    }
}
/* the samples of a section (iterator FUN_180148820): per entity 1/3, 2/3, 1 of [tStart, tEnd], the last end skipped;
   returns the first classification that is 2 or 4, else 1 (0 = no sample) */
static int ex_sample(const Clf *F, const Sh *s, int ea, double ta, int eb, double tb, int wrap) {
    if (!s->n) return 0;
    int cnt = wrap ? eb + 1 + (s->n - ea) : eb - ea + 1, res = 0;
    for (int q = 0; q < cnt; q++) {
        const Ent *e = &s->e[(ea + q) % s->n]; double ts = q == 0 ? ta : 0, te = q == cnt - 1 ? tb : 1;
        for (int f = 1; f <= 3; f++) {
            if (q == cnt - 1 && f == 3) break;
            int c = clf(F, e_at(e, ts + (te - ts) * f / 3.0));
            if (c == 2 || c == 4) return c;
            res = 1;
        }
    }
    return res;
}
/* FUN_1800337e0 + FUN_180031150 + FUN_1800354c0 + FUN_180035630 */
static void ex_classify(const ShL *mat, const Clf *F, IsL *L) {
    for (int i = 0; i < L->n; i++) {
        if (L->r[i].far) continue;
        int j = is_next_same(L, i), ea, eb; double ta, tb;
        is_posA(L, i, &ea, &ta); is_posB(mat, L, j, &eb, &tb);
        int wrap = eb < ea || (ea == eb && tb < ta) || i == j;
        int c = ex_sample(F, mat->s[L->r[i].sh], ea, ta, eb, tb, wrap);
        if (c == 2) { L->r[i].cls = 2; continue; }
        if (c == 4) continue;
        if (L->r[i].mk) { L->r[i].cls = 2; continue; }
        if (i == j) { is_mkmarker(&L->r[i]); L->r[i].cls = 2; continue; }
        if (pdist2(L->r[i].pt, L->r[j].pt) >= 1e-8) { L->r[i].cls = 2; continue; }
        /* FUN_180035f30: the record with more parameters outside [0, 1] (i on a tie) */
        int bi = (L->r[i].t < 0 || L->r[i].t > 1) + (L->r[i].swt < 0 || L->r[i].swt > 1), bj = (L->r[j].t < 0 || L->r[j].t > 1) + (L->r[j].swt < 0 || L->r[j].swt > 1);
        int d = bi < bj ? j : i;
        is_del(L, d); i -= (d < i) + 1;
    }
    for (int i = 0; i < L->n; i++) {                                  /* FUN_180031150 */
        if (L->r[i].mk) continue;
        int p = is_prev_same(L, i);
        if (p == i) is_mkmarker(&L->r[i]);
        else if (L->r[i].cls == L->r[p].cls) { is_del(L, i); i--; }
    }
    int outer2 = 0;                                                   /* FUN_1800354c0 */
    for (int i = 0; i < L->n; i++) if (L->r[i].cls == 2 && !sh_island(mat->s[L->r[i].sh])) { outer2 = 1; break; }
    if (!outer2) for (int i = 0; i < L->n; i++) if (L->r[i].mk && L->r[i].cls == 2 && sh_island(mat->s[L->r[i].sh])) L->r[i].cls = 0;
    int *m = malloc(sizeof(int) * (size_t)(L->n ? L->n : 1)), n = 0;   /* FUN_180035630 */
    for (int i = 0; i < L->n; i++) if (!L->r[i].mk) m[n++] = i;
    g_is_base = L->r; if (n > 1) qsort(m, (size_t)n, sizeof(int), is_cmp_sw);
    for (int k = 0; k < n; k++) { L->r[m[k]].nx = m[(k + 1) % n]; L->r[m[k]].pv = m[(k - 1 + n) % n]; }
    free(m);
}
/* mcce::CalcSharedArcSw (l.9611-9712): total angular overlap of two arcs, lo / hi = the largest piece */
static double shared_arc_sw(double sa1, double sw1, double sa2, double sw2, double *lo, double *hi) {
    *lo = *hi = 0;
    if (fabs(sw1) > 2 * M_PI || fabs(sw2) > 2 * M_PI) {
        double sa = sa2, sw = sw2; if (fabs(sw1) < fabs(sw2)) { sa = sa1; sw = sw1; }
        if (sw <= 0) { *hi = sa; *lo = sa + sw; } else { *lo = sa; *hi = sa + sw; }
        return fabs(sw);
    }
    double l1 = sw1 <= 0 ? sa1 + sw1 : sa1, h1 = sw1 <= 0 ? sa1 : sa1 + sw1, l2 = sw2 <= 0 ? sa2 + sw2 : sa2, h2 = sw2 <= 0 ? sa2 : sa2 + sw2;
    while (l1 < h2) { h2 -= 2 * M_PI; l2 -= 2 * M_PI; }
    double tot = 0, best = -FLT_MAXD;
    while (l2 < h1) {
        l2 += 2 * M_PI; h2 += 2 * M_PI;
        double a = l2 < l1 ? l1 : l2, b = h1 < h2 ? h1 : h2;
        if (a < b) { double d = b - a; tot += d; if (best < d) { *lo = a; *hi = b; best = d; } }
    }
    return tot;
}
/* FUN_1800312f0: an engagement angle's offset from the cap start, wrapped and clamped into the cap sweep */
static double cap_off(double sa, double sw, double a) {
    double d = a - sa;
    if (d < -M_PI) d += 2 * M_PI; else if (d > M_PI) d -= 2 * M_PI;
    if ((d > 0 && sw < 0) || (d < 0 && sw > 0)) d = fabs(d) < (2 * M_PI - fabs(sw)) * 0.5 ? 0.0 : sw;
    if (fabs(d) > fabs(sw)) d = sw;
    return d;
}
static _Thread_local double g_cap_sa, g_cap_sw;
static int cap_cmp(const void *a, const void *b) { double x = fabs(cap_off(g_cap_sa, g_cap_sw, *(const double *)a)), y = fabs(cap_off(g_cap_sa, g_cap_sw, *(const double *)b)); return x < y ? -1 : x > y ? 1 : 0; }
/* exact loop area FUN_180126ac0: 0 when two consecutive entities are more than tol apart. The DLL's own sum (gp_area_dir: the
   trapezoid rule from the last entity's end, arcs FUN_1801270e0) - the cross-product form below is equal in exact arithmetic but
   not in floating point: the measured width (swept area - loops) / length cancels, so it moved widths by 1e-12 and with them every
   later step (mcce_port_plan.md 9 "M2 bit-exact"). MCCE_AREAX=0: the old sum. */
static void gp_area_dir(double tol, const Sh *s, double *area, int *dir);
static int areax_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_AREAX"); v = !(e && *e == '0'); } return v; }
static double sh_area_x(const Sh *s, double tol) {
    if (areax_on()) { double a; int d; gp_area_dir(tol, s, &a, &d); return a; }
    double sum = 0;
    for (int k = 0; k < s->n; k++) {
        const Ent *e = &s->e[k];
        if (k > 0 && pdist(e_end(&s->e[k - 1]), e_start(e)) > tol) return 0;
        if (e->id == 'L') sum += e->p1.x * e->p2.y - e->p2.x * e->p1.y;
        else if (e->id == 'A') { P2 S = e_start(e), E = e_end(e); sum += e->r * e->r * e->sw + e->c.x * (E.y - S.y) - e->c.y * (E.x - S.x); }
    }
    return fabs(sum / 2);
}
/* the records of the last measures, for the commit of the same step (FUN_180037790 uses the measurement's list) */
typedef struct { int used; const ShL *set; const Sh *win; long ver; double rad; Ent st; IsL L; } RecC;
#define RECC_N 1024
static _Thread_local RecC g_recc[RECC_N]; static _Thread_local long g_mat_ver;
static size_t recc_h(const ShL *set, double rad, const Ent *st) {
    const unsigned char *b = (const unsigned char *)st; size_t h = (size_t)set * 31u; for (size_t i = 0; i < sizeof *st; i++) h = h * 131u + b[i];
    const unsigned char *r = (const unsigned char *)&rad; for (size_t i = 0; i < sizeof rad; i++) h = h * 131u + r[i];
    return h % RECC_N;
}
static void recc_put(const ShL *set, const Sh *win, double rad, const Ent *st, const IsL *L) {
    RecC *c = &g_recc[recc_h(set, rad, st)];
    c->used = 1; c->set = set; c->win = win; c->ver = g_mat_ver; c->rad = rad; c->st = *st;
    if (c->L.cap < L->n) { c->L.cap = L->n; c->L.r = realloc(c->L.r, sizeof(Isec) * (size_t)L->n); }
    if (L->n) memcpy(c->L.r, L->r, sizeof(Isec) * (size_t)L->n);
    c->L.n = L->n;
}
static int recc_get(const ShL *set, const Sh *win, double rad, const Ent *st, IsL *out) {
    RecC *c = &g_recc[recc_h(set, rad, st)];
    if (!c->used || c->set != set || c->win != win || c->ver != g_mat_ver || c->rad != rad || memcmp(&c->st, st, sizeof *st)) return 0;
    out->n = 0; for (int i = 0; i < c->L.n; i++) is_push(out, c->L.r[i]);
    return 1;
}
/* ours: a worker thread's engine caches freed before it ends (regeneration on several threads, core_ui cui_dirty.c) */
void mcce_thread_release(void) {
    tess_reset();
    for (int i = 0; i < RECC_N; i++) free(g_recc[i].L.r);
    memset(g_recc, 0, sizeof g_recc);
}
static _Thread_local int g_meas_dir;                                                 /* measure()'s side argument (the classifier frame) */
static int ex_records(int dir, double rad, const ShL *mat, const Sh *win, const Ent *st, const Sh *sw, IsL *L) {
    Clf F; if (!clf_make(dir, rad, st, &F)) return 0;
    ex_crossings(mat, win, sw, L);
    ex_classify(mat, &F, L);
    return 1;
}
static void mcce_trace_swept(const Sh *sw, double area, double wl);
static void mcce_trace_loops(const ShL *lps, double removed);   /* ours: MCCE_TRACE_SW (mcce_trace.c) */
static void mcce_trace_ev(const char *name);
static void mcce_trace_leadin(const Ent *st, P2 X, const Sh *lead);
static void mcce_trace_pstep(const Sh *path, const Pos *a, const Pos *b, const Ent *out);
static int mcce_trace_sw_on(void);   /* ours: MCCE_TRACE_SW (mcce_trace.c) */
/* FUN_180030de0 with the exact material (§6.1.5 / §6.1.6) */
static void measure_exact(Pock *C, double rad, const ShL *mat, const Sh *win, const Ent *st, const Pos *pos, MBlk *M, Sh *sw) {
    const McceParm *P = &C->P;
    M->area_sw = sh_area_x(sw, 0.001);
    IsL L = { 0 };
    if (!ex_records(g_meas_dir, rad, mat, win, st, sw, &L)) { M->flags |= 8; free(L.r); return; }
    /* FUN_18002fc80: the engaged angles on the end cap */
    double ang[128]; int na = 0, i0 = -1;
    for (int i = 0; i < L.n; i++) if (!L.r[i].mk) { i0 = i; break; }
    const Ent *cap = NULL; for (int k = 0; k < sw->n; k++) if (sw->e[k].ce & 1) { cap = &sw->e[k]; break; }
    if (L.n >= 2 && i0 >= 0 && cap && cap->id == 'A') {
        int i = i0;
        do {
            int p = L.r[i].pv;
            if (p >= 0 && L.r[i].cls == 0 && L.r[p].cls == 2) {
                P2 dp = psub(L.r[p].pt, cap->c), di = psub(L.r[i].pt, cap->c);
                double ap = fabs(dp.x) <= 1e-12 && fabs(dp.y) <= 1e-12 ? 0 : atan2(dp.y, dp.x), ai = fabs(di.x) <= 1e-12 && fabs(di.y) <= 1e-12 ? 0 : atan2(di.y, di.x);
                double s = wrap_pi(ai - ap);
                if ((P->flags & 1) && s < 0) s += 2 * M_PI;
                if (!(P->flags & 1) && s > 0) s -= 2 * M_PI;
                double lo, hi, ov = shared_arc_sw(cap->sa, cap->sw, ap, s, &lo, &hi);
                M->sweep += ov;
                if (ov > 1e-12 && na < 126) { ang[na++] = lo; ang[na++] = hi; }
            }
            i = L.r[i].nx;
        } while (i >= 0 && i != i0);
        g_cap_sa = cap->sa; g_cap_sw = cap->sw; if (na > 1) qsort(ang, (size_t)na, sizeof(double), cap_cmp);
    }
    /* FUN_18003a140: loops from the inside sections (classes swapped) and the outline pieces; removed = swept - loops */
    IsL W = { 0 }; for (int i = 0; i < L.n; i++) { Isec r = L.r[i]; if (r.cls == 0) r.cls = 2; else if (r.cls == 2) r.cls = 0; is_push(&W, r); }
    double loops = 0; int nl = 0, i = -1;
    ShL tl = { 0 }; int trsw = mcce_trace_sw_on();
    Sh *lp = sh_new();
    while (is_next_cls0(&W, &i)) {
        if (W.r[i].mk) { W.r[i].cls = 1; continue; }
        sh_clear(lp);
        int start = i, ea, eb; double ta, tb, j = 0;
        int jj = is_next_same(&W, i); W.r[i].cls = 1;
        is_posA(&W, i, &ea, &ta); is_posB(mat, &W, jj, &eb, &tb);
        sh_section(mat->s[W.r[i].sh], ea, ta, eb, tb, lp);
        int cur = i;
        while (jj != start) {
            int k = is_chain(&W, cur);
            if (k < 0) break;
            sh_section(sw, W.r[jj].swe, W.r[jj].swt, W.r[k].swe, W.r[k].swt, lp);
            if (W.r[k].cls != 0) break;
            cur = k; jj = is_next_same(&W, k); W.r[k].cls = 1;
            is_posA(&W, k, &ea, &ta); is_posB(mat, &W, jj, &eb, &tb);
            sh_section(mat->s[W.r[k].sh], ea, ta, eb, tb, lp);
        }
        (void)j; sh_upd_bbox(lp);
        loops += sh_area_x(lp, 0.001); nl++;
        if (trsw) sl_push(&tl, sh_copy(lp));   /* ours: MCCE_TRACE_SW */
    }
    sh_free(lp); free(W.r);
    double removed = nl ? M->area_sw - loops : 0.0;
    if (trsw) { mcce_trace_loops(&tl, removed); sl_free(&tl); }
    M->w = M->len <= 1e-12 ? 0 : removed / M->len;
    if (mat == &C->mat) mcce_trace_swept(sw, M->area_sw, M->w * M->len);   /* ours: MCCE_TRACE_SW */
    int any2 = 0; for (int k = 0; k < L.n; k++) if (L.r[k].cls == 2) { any2 = 1; break; }   /* FUN_180035360 */
    if (M->w < 1e-12 && !any2) { M->flags |= 0x10; na = 0; }
    M->npts = na;
    if (na > 2) M->flags |= 2;
    {   /* flag 4 FUN_180037070 */
        int ok; P2 T = unit(e_tan(st, 1), &ok);
        if (ok) {
            P2 E = e_end(st), Q = (P->flags & 1) ? padd(E, pmul(p2(T.y, -T.x), rad)) : padd(E, pmul(p2(-T.y, T.x), rad));
            int hit = 0;
            for (int k = 0; k < na; k += 2) { P2 q = p2(cap->c.x + cap->r * cos(ang[k]), cap->c.y + cap->r * sin(ang[k])); if (pdist2(q, Q) < 1e-8) { hit = 1; break; } }
            if (!hit) M->flags |= 4;
        }
    }
    if (pos && pos->sh >= 0) {   /* flag 0x100 FUN_1800380f0 */
        M->flags &= ~0x100u; int done = 0;
        for (int k = 0; k < L.n && !done; k++) {
            if (L.r[k].sh != pos->sh) continue;
            if (L.r[k].mk) { if (L.r[k].cls == 2) M->flags |= 0x100; done = 1; break; }
            int q = is_next_same(&L, k);
            if (L.r[k].ent != -1 && L.r[q].ent != -1 && pdist2(pos->pt, L.r[k].pt) > 1e-14 && pdist2(pos->pt, L.r[q].pt) > 1e-14) {   /* FUN_1800353f0 */
                double u = pos->ent + pos->t, ui = L.r[k].ent + L.r[k].t, uk = L.r[q].ent + L.r[q].t;
                int in = uk <= ui ? (u > ui || u < uk) : (u > ui && u < uk);
                if (in) { if (L.r[k].cls == 2) M->flags |= 0x100; done = 1; break; }
            }
        }
        if (!done && pt_in_sh(pos->pt, sw, 1e-5) == 2) M->flags |= 0x100;
    }
    recc_put(mat, win, rad, st, &L);
    free(L.r);
}
static int gp_lnln(P2 a, P2 da, P2 b, P2 db, double *s, double *u, P2 *x);
static P2 gp_raw_start(const Ent *e);
static P2 gp_raw_end(const Ent *e);
/* FUN_180036250 (l.33600-33997) operation for operation (MCCE_MEASX): the last k = 3 .. 2 entities (fewer when the loop has
   fewer) merge into the line A -> B (A = start of the first, B = end of the last) when every entity end, and every arc's
   middle, has its foot on A B (CalcLnLnIntersectPt with the normal (-dy, dx)) within 1e-6 and, beyond an end, lies within 1e-6
   of that end; with the first-pass feed (C+0x758, fp) also no pass-byte change between the entities. The new line carries
   the last entity's pass byte only with fp (else 0); its +0x78 (z) stays the last entity's */
static void col_merge_x(Sh *s, int fp) {
    if (s->n < 2) return;
    for (int k = s->n < 3 ? s->n : 3; k >= 2; k--) {
        int b = s->n - k; P2 A = gp_raw_start(&s->e[b]), B = gp_raw_end(&s->e[s->n - 1]);
        P2 d = p2(B.x - A.x, B.y - A.y), nrm = p2(-d.y, d.x);
        int ok = 1;
        for (int q = b; q < s->n && ok; q++) {
            const Ent *e = &s->e[q]; P2 pts[2]; int np = 0;
            pts[np++] = gp_raw_end(e);
            if (e->id == 'A') { double g = e->sw * 0.5 + e->sa; pts[np++] = p2(cos(g) * e->r + e->c.x, sin(g) * e->r + e->c.y); }
            for (int h = 0; h < np && ok; h++) {
                P2 P = pts[h], f; double t, u;
                if (!gp_lnln(A, d, P, nrm, &t, &u, &f)) ok = 0;
                else if (1e-12 < (f.x - P.x) * (f.x - P.x) + (f.y - P.y) * (f.y - P.y)) ok = 0;
                else if (t < 0.0 && 1e-12 < (A.y - P.y) * (A.y - P.y) + (A.x - P.x) * (A.x - P.x)) ok = 0;
                else if (1.0 < t && 1e-12 < (B.y - P.y) * (B.y - P.y) + (B.x - P.x) * (B.x - P.x)) ok = 0;
                else if (h == 0 && fp && q != b && s->e[q].pass != s->e[q - 1].pass) ok = 0;
            }
        }
        if (ok) { Ent l = ent_line(A, B); l.pass = fp ? s->e[s->n - 1].pass : 0; l.z = s->e[s->n - 1].z; s->n = b; sh_push(s, l); sh_upd_bbox(s); return; }
    }
}
/* FUN_180036250: collinear merge of the tail of a new loop (the pass-byte condition only with the first-pass feed: NOT PORTED) */
static void col_merge(Sh *s) {
    if (s->n < 2) return;
    for (int n = s->n < 3 ? s->n : 3; n >= 2; n--) {
        int b = s->n - n; P2 A = e_start(&s->e[b]), B = e_end(&s->e[s->n - 1]), d = psub(B, A); double L2 = pdot(d, d);
        if (L2 <= 0) continue;
        int ok = 1;
        for (int q = b; q < s->n && ok; q++) {
            const Ent *e = &s->e[q]; P2 pts[2]; int np = 0; pts[np++] = e_end(e); if (e->id == 'A') pts[np++] = e_at(e, 0.5);
            for (int h = 0; h < np && ok; h++) {
                double u = pdot(psub(pts[h], A), d) / L2; P2 f = padd(A, pmul(d, u));
                if (pdist2(f, pts[h]) > 1e-12) ok = 0;
                else if ((u < 0 || u > 1) && pdist2(pts[h], A) > 1e-12 && pdist2(pts[h], B) > 1e-12) ok = 0;
            }
        }
        if (ok) { Ent l = ent_line(A, B); l.pass = s->e[s->n - 1].pass; l.z = s->e[s->n - 1].z; s->n = b; sh_push(s, l); sh_upd_bbox(s); return; }
    }
}
/* FUN_180037790: material := the kept sections + the outline pieces; pos (set 1) -> FUN_180037540 */
static void commit_exact(Pock *C, ShL *mat, const Sh *win, double rad, const Ent *st, const Sh *sw, Pos *pos) {
    IsL L = { 0 };
    if (!recc_get(mat, win, rad, st, &L) && !ex_records(0, rad, mat, win, st, sw, &L)) { free(L.r); return; }   /* a miss: side 0 (ours) */
    P2 Pend = e_end(st);
    ShL NEW = { 0 };
    int i = -1;
    while (is_next_cls0(&L, &i)) {
        if (L.r[i].mk) { L.r[i].cls = 1; if (mat->s[L.r[i].sh]) { sl_push(&NEW, mat->s[L.r[i].sh]); mat->s[L.r[i].sh] = NULL; } continue; }
        Sh *Ls = sh_new(); Ls->z = C->z; sl_push(&NEW, Ls);
        int start = i, cur = i, ea, eb; double ta, tb;
        for (;;) {
            int j = is_next_same(&L, cur); L.r[cur].cls = 1;
            int n0 = Ls->n;
            is_posA(&L, cur, &ea, &ta); is_posB(mat, &L, j, &eb, &tb);
            sh_section_bridge(1e-16, mat->s[L.r[cur].sh], ea, ta, eb, tb, Ls);
            if (swx_on()) col_merge_x(Ls, C->fp_on); else col_merge(Ls);
            L.r[cur].nsh = NEW.n - 1; L.r[cur].nent = n0; L.r[cur].nt = 0.0;   /* FUN_180036f70 */
            L.r[j].nsh = NEW.n - 1; L.r[j].nent = Ls->n - 1; L.r[j].nt = 1.0;
            if (j == start) break;
            int k = is_chain(&L, cur);
            if (k < 0) break;
            sh_section_bridge(1e-16, sw, L.r[j].swe, L.r[j].swt, L.r[k].swe, L.r[k].swt, Ls);
            cur = k;
            if (L.r[k].cls != 0) break;
        }
        sh_close(1e-16, Ls);
        for (int k = 0; k < Ls->n; k++) Ls->e[k].z = C->z;
        sh_upd_bbox(Ls);                                                /* FUN_180032a20 */
        int isl = pt_in_sh(Pend, Ls, 1e-5) == 2;
        Ls->fl = isl ? 1 : 0;
        Ls->dir = isl ? ((C->P.flags & 1) ? 1 : -1) : ((C->P.flags & 1) ? -1 : 1);
        Ls->tag = C->T.n;
    }
    if (pos) {   /* FUN_180037540 */
        double bd = FLT_MAXD, mt = -1.0; int bi = -1, me = -1, mi = -1;
        for (int k = 0; k < L.n; k++) {
            if (L.r[k].mk || L.r[k].cls != 1) continue;
            double d = pdist2(L.r[k].pt, pos->pt); if (d < bd) { bd = d; bi = k; }
            if (me < L.r[k].swe || (L.r[k].swe == me && mt < L.r[k].swt)) { me = L.r[k].swe; mt = L.r[k].swt; mi = k; }
        }
        int pick = -1;
        if (bi != -1 && bd <= 1e-8 && (me < L.r[bi].swe || (me == L.r[bi].swe && mt <= L.r[bi].swt))) pick = bi;
        else if (mi != -1) pick = mi;
        if (pick >= 0 && L.r[pick].nsh >= 0) { pos->sh = L.r[pick].nsh; pos->ent = L.r[pick].nent; pos->t = 0.0; pos->pt = L.r[pick].pt; }
        else pos->sh = -1;
    }
    for (int k = 0; k < mat->n; k++) sh_free(mat->s[k]);                /* every shape not moved */
    free(mat->s); *mat = NEW;
    g_mat_ver++;
    if (dbg_level() >= 6) {
        fprintf(stderr, "commit rec %d step (%.3f,%.3f)-(%.3f,%.3f): %d loops", C->T.n, e_start(st).x, e_start(st).y, e_end(st).x, e_end(st).y, mat->n);
        for (int k = 0; k < mat->n; k++) fprintf(stderr, " [%s %.4f bb %.2f,%.2f-%.2f,%.2f]", sh_island(mat->s[k]) ? "isl" : "out", sh_area_x(mat->s[k], 1e300), mat->s[k]->bb[0], mat->s[k]->bb[1], mat->s[k]->bb[2], mat->s[k]->bb[3]);
        fprintf(stderr, "\n");
    }
    free(L.r);
}

/* FUN_180030de0 (l.30654-30732): measure one step against one material set; rad = R - r (M1) or R (M2).
   Removed area = area(swept ∩ material) (§6.1.5); width = removed / length; flags 8 (failed), 0x10 (air), 2 (more than
   one engaged arc on the end cap), 4 (FUN_180037070), 0x100 (FUN_1800380f0: material position inside the swept region) */
static void measure_block_(Pock *C, double rad, const ShL *mat, const Sh *win, const Ent *st, const Pos *pos, MBlk *M, Sh *sw_out);
/* FUN_180030de0 ORs its flags into the block (only FUN_18001cfe0 = encut_clear zeroes them): flags set before the measure stay
   (e.g. the 0x20 of FUN_1800167d0 l.17797); 0x100 is cleared by FUN_1800380f0 (l.34529) only on the successful path (a failed
   measure, flag 8, keeps it). MCCE_MEASX=0: the block starts from zero flags */
static void measure_block(Pock *C, double rad, const ShL *mat, const Sh *win, const Ent *st, const Pos *pos, MBlk *M, Sh *sw_out) {
    uint32_t of = M->flags;
    measure_block_(C, rad, mat, win, st, pos, M, sw_out);
    if (swx_on()) M->flags |= (M->flags & 8) ? of : (of & ~0x100u);
}
static void measure_block_(Pock *C, double rad, const ShL *mat, const Sh *win, const Ent *st, const Pos *pos, MBlk *M, Sh *sw_out) {
    const McceParm *P = &C->P;
    memset(M, 0, sizeof *M);
    C->nmeasure++;
    M->len = e_len(st);
    Sh *sw = sw_out ? sw_out : sh_new();
    if (!swept_region(P->flags, rad, st, sw) || (st->id == 'L' && pdist(st->p1, st->p2) < 1e-6)) {   /* FUN_180034ac0 l.32947 */
        M->flags |= 8; if (!sw_out) sh_free(sw); return;
    }
    if (exact_on()) { measure_exact(C, rad, mat, win, st, pos, M, sw); if (!sw_out) sh_free(sw); return; }
    CPath poly = sh_to_cpath_o(sw, g_chord, 1); CPaths pp = { &poly, 1 };
    M->area_sw = fabs(cl_area(&poly));
    int ng = groups_overlap(mat, sw->bb, g_gs, g_ge, MAXG);
    const long long **miy; char *mown; CPaths mfull = groups_cpaths_c(mat, g_gs, g_ge, ng, &miy, &mown);
    CPaths mp = crop_band_y(&mfull, &poly, miy);                            /* ours: speed-up only, same output (crop_band) */
    int chk = crop_check();
    CPaths in = mp.n ? cl_bool(&mp, &pp, 1) : (CPaths){ calloc(1, sizeof(CPath)), 0 };
    if (chk == 1) {
        CPaths in2 = mfull.n ? cl_bool(&mfull, &pp, 1) : (CPaths){ calloc(1, sizeof(CPath)), 0 };
        g_crop_n++; if (!cpaths_same(&in, &in2)) g_crop_bad++;
        cl_free(&in2);
    }
    int mp_n = mfull.n;
    double removed = fabs(cl_total_area(&in));
    M->w = M->len <= 1e-12 ? 0 : removed / M->len;
    if (M->w < 1e-12 && in.n == 0) M->flags |= 0x10;                 /* l.30718-30720 (FUN_180035360: no class-2 record) */
    else if (mp_n) {
        /* engaged arcs on the end cap (FUN_18002fc80 l.30114-30521): the cap arc pieces inside the material */
        const Ent *cap = NULL; for (int k = 0; k < sw->n; k++) if (sw->e[k].ce & 1) { cap = &sw->e[k]; break; }
        if (cap && cap->id == 'A') {
            int m = arc_segs(cap->r, cap->sw, g_chord) * 2;
            CPath cl; cl.n = m + 1; cl.xy = malloc(sizeof(double) * 2 * (m + 1));
            for (int j = 0; j <= m; j++) { P2 q = e_at(cap, (double)j / m); cl.xy[2 * j] = q.x; cl.xy[2 * j + 1] = q.y; }
            CPaths mcap = crop_band_y(&mfull, &cl, miy);                   /* ours: band of the cap polyline (crop_band) */
            CPaths cls = { &cl, 1 }, pc = cl_clip_open(&cls, &mcap, 1);
            cl_free(&mcap);
            if (chk) { CPaths pc2 = cl_clip_open(&cls, &mfull, 1); g_crop_n++; if (!cpaths_same(&pc, &pc2)) g_crop_bad++; cl_free(&pc2); }
            double us[64]; int nu = 0;
            for (int j = 0; j < pc.n && nu < 62; j++) {
                if (pc.p[j].n < 2) continue;
                P2 a = p2(pc.p[j].xy[0], pc.p[j].xy[1]), b = p2(pc.p[j].xy[2 * pc.p[j].n - 2], pc.p[j].xy[2 * pc.p[j].n - 1]);
                double ua = fmin(1, fmax(0, e_param(cap, a))), ub = fmin(1, fmax(0, e_param(cap, b)));
                if (fabs(ub - ua) * fabs(cap->sw) <= 1e-12) continue;
                us[nu++] = fmin(ua, ub); us[nu++] = fmax(ua, ub);
                M->sweep += fabs(ub - ua) * fabs(cap->sw);
            }
            /* sort by the angle from the cap start: comparator LAB_1800034b8 -> FUN_1800312f0 (thunk jmp at 0x1800034b8;
               l.30776-30826): offset from sa wrapped to [-pi, pi], clamped into the cap sweep, ascending |offset| */
            for (int x = 1; x < nu; x++) for (int y = x; y > 0 && us[y] < us[y - 1]; y--) { double t = us[y]; us[y] = us[y - 1]; us[y - 1] = t; }
            M->npts = nu;
            if (nu > 2) M->flags |= 2;                                 /* l.30722-30724 */
            /* flag 4 FUN_180037070 (l.34023-34116): Q = E + rad (T.y, -T.x) (climb) / E + rad (-T.y, T.x) */
            int ok; P2 T = unit(e_tan(st, 1), &ok);
            if (ok) {
                P2 E = e_end(st), Q = (P->flags & 1) ? padd(E, pmul(p2(T.y, -T.x), rad)) : padd(E, pmul(p2(-T.y, T.x), rad));
                int hit = 0;
                for (int j = 0; j < nu; j += 2) if (pdist2(e_at(cap, us[j]), Q) < 1e-8) { hit = 1; break; }
                if (!hit) M->flags |= 4;
            }
            free(cl.xy); cl_free(&pc);
        }
        /* flag 0x100 FUN_1800380f0 (l.34494-34669): the material section at pos lies inside the swept region
           (point classification of pos.pt; fallback PtInSh(pos.pt, swept) == 2) */
        if (pos && pos->sh >= 0 && pt_in_sh(pos->pt, sw, 1e-5) == 2) M->flags |= 0x100;
    }
    for (int i = 0; i < mfull.n; i++) if (mown[i]) { free(mfull.p[i].xy); free((void *)miy[i]); }
    free(poly.xy); cl_free(&mp); free(mfull.p); free(miy); free(mown); cl_free(&in);   /* mfull: borrowed from the tessellation cache (but the mown paths) */
    if (!sw_out) sh_free(sw);
}
static void mcce_trace_measure(const EnCut *E, int line);   /* ours: MCCE_TRACE_LEVEL >= 4 "M" lines (mcce_trace.c), as tools/peloader's hook_measure */
/* FUN_180030be0 (l.30614-30651): both circles */
static void measure_(Pock *C, const Pos *pos, int dir, EnCut *E);
static void measure_l(Pock *C, const Pos *pos, int dir, EnCut *E, int line) { measure_(C, pos, dir, E); mcce_trace_measure(E, line); }
#define measure(C, pos, dir, E) measure_l(C, pos, dir, E, __LINE__)   /* the call site's line for the trace (level 5) */
static void measure_(Pock *C, const Pos *pos, int dir, EnCut *E) {
    g_meas_dir = dir;   /* side argument of the point classifier frame (FUN_180034ac0): only shifts its 1e-4 band (exact model) */
    const McceParm *P = &C->P;
    measure_block(C, P->R - P->r, &C->mat, C->win1_n ? &C->win1 : NULL, &E->e, pos, &E->m1, NULL);
    if (!swx_on()) memset(&E->m2, 0, sizeof E->m2);   /* FUN_180030be0 (l.30619-30650) leaves M2 as it was when it is not measured */
    if (C->bull.n && !(E->m1.flags & 0x18)) {
        memset(&E->m2, 0, sizeof E->m2);                /* FUN_18001cfe0(E + 0x140) */
        measure_block(C, P->R, &C->bull, C->win2_n ? &C->win2 : NULL, &E->e, NULL, &E->m2, NULL);
        E->m2.len = E->m1.len;
    }
}
/* material minus a swept region (FUN_180037790, §6.1.7), group by group; untouched groups are kept as they are.
   New loops: attributes of FUN_180032a20 (island = hole, dir by orient_mat, tag +0x58 = CCetp record count) */
static void subtract(Pock *C, ShL *mat, const Sh *sw) {
    int ng = groups_overlap(mat, sw->bb, g_gs, g_ge, MAXG);
    if (!ng) return;
    CPath poly = sh_to_cpath_o(sw, g_chord, 1); CPaths pp = { &poly, 1 };
    ShL out = { 0 }; char *used = calloc(mat->n ? mat->n : 1, 1);
    for (int g = 0; g < ng; g++) {
        CPaths mp = groups_cpaths(mat, &g_gs[g], &g_ge[g], 1);
        double a0 = cl_total_area(&mp);
        CPaths d = cl_bool(&mp, &pp, 2);
        double a1 = cl_total_area(&d);
        if (fabs(a1 - a0) > 1e-12 * fmax(1, fabs(a0)) || d.n != mp.n) {
            for (int k = g_gs[g]; k < g_ge[g]; k++) used[k] = 1;
            ShL nl = cpaths_to_sl(&d, C->z);
            drop_slivers(&nl);
            orient_mat(C->P.flags, &nl);
            for (int k = 0; k < nl.n; k++) nl.s[k]->tag = C->T.n;
            sl_move_append(&out, &nl); sl_free(&nl);
        }
        cl_free(&mp); cl_free(&d);
    }
    ShL keep = { 0 };
    for (int k = 0; k < mat->n; k++) if (!used[k]) sl_push(&keep, mat->s[k]); else sh_free(mat->s[k]);
    sl_move_append(&keep, &out);
    free(mat->s); *mat = keep; sl_free(&out);
    free(used); free(poly.xy);
}
/* numeric substitution (plan §3): outer material loops thinner than the polygonisation error (2 area / perimeter < 2 x
   chord tolerance) are polygon artefacts of the Clipper subtraction (the exact-geometry engine has no such loops):
   dropped together with their islands */
static void drop_slivers(ShL *l) {
    int i = 0;
    while (i < l->n) {
        int e = sl_group_end(l, i); if (e < 0) e = l->n;
        Sh *o = l->s[e - 1]; CPath c = sh_to_cpath(o, g_chord); double a = fabs(cl_area(&c)), per = sh_len(o); free(c.xy);
        if (per > 0 && 2 * a / per < 2 * g_chord) { for (int k = e - 1; k >= i; k--) sl_erase(l, k); continue; }
        i = e;
    }
}
/* FUN_180037540 (l.34119-34210): new material position = the crossing of the swept outline with the old material
   boundary that has the largest swept-outline parameter, on the new material; none -> shape -1 */
static void new_pos(Pock *C, const ShL *oldmat, const Sh *sw) {
    double best = -1; P2 bp = { 0, 0 };
    int ng = groups_overlap(oldmat, sw->bb, g_gs, g_ge, MAXG);
    for (int g = 0; g < ng; g++) for (int k = g_gs[g]; k < g_ge[g]; k++) {
        const Sh *s = oldmat->s[k];
        for (int j = 0; j < s->n; j++) {
            double bb[4]; e_bbox(&s->e[j], bb);
            if (bb[0] > sw->bb[2] + 1e-7 || bb[2] < sw->bb[0] - 1e-7 || bb[1] > sw->bb[3] + 1e-7 || bb[3] < sw->bb[1] - 1e-7) continue;
            for (int m = 0; m < sw->n; m++) {
                double ta[2], tb[2]; P2 x[2]; int c = e_isect(&s->e[j], &sw->e[m], 1e-5, ta, tb, x);
                for (int h = 0; h < c; h++) { double u = m + tb[h]; if (u > best) { best = u; bp = x[h]; } }
            }
        }
    }
    if (best < 0 || !C->mat.n) { C->mpos.sh = -1; return; }
    Pos q; if (!sl_nearest(&C->mat, bp, &q)) { C->mpos.sh = -1; return; }
    C->mpos = q;
}
/* commit FUN_180035990 (l.33361-33389) */
#include "mcce_trace.c"   /* ours: oracle traces MCCE_TRACE / MCCE_DUMPIN (off unless set; tools/peloader --dyn) */
static void commit(Pock *C, EnCut *E) {
    const McceParm *P = &C->P;
    Sh *sw = sh_new();
    if (exact_on()) {
        if (swept_region(P->flags, P->R - P->r, &E->e, sw)) commit_exact(C, &C->mat, C->win1_n ? &C->win1 : NULL, P->R - P->r, &E->e, sw, &C->mpos);
    } else if (swept_region(P->flags, P->R - P->r, &E->e, sw)) {
        ShL old = { 0 };
        int ng = groups_overlap(&C->mat, sw->bb, g_gs, g_ge, MAXG);
        for (int g = 0; g < ng; g++) for (int k = g_gs[g]; k < g_ge[g]; k++) sl_push(&old, sh_copy(C->mat.s[k]));
        subtract(C, &C->mat, sw);
        new_pos(C, &old, sw);
        sl_free(&old);
    }
    E->m1.flags |= 0x80;
    if (E->m1.w > 1e-6) E->m1.flags |= 0x40;
    E->m1.flags |= (P->flags & 1) ? 0x200 : 0x400;
    if (C->bull.n && !(E->m2.flags & 0x18)) {
        Sh *s2 = sh_new();
        if (swept_region(P->flags, P->R, &E->e, s2)) { if (exact_on()) commit_exact(C, &C->bull, C->win2_n ? &C->win2 : NULL, P->R, &E->e, s2, NULL); else subtract(C, &C->bull, s2); }
        sh_free(s2);
    }
    /* pass byte E+0x13c (FUN_180033cf0 / FUN_180035ff0 / FUN_180033b60): only used with the first-pass feed
       (C+0x758); the material entities' pass bytes are not kept through the Clipper subtraction: NOT PORTED */
    E->pass = 0xff;
    sh_free(sw);
    mcce_trace_commit(C, E);   /* ours: MCCE_TRACE (mcce_trace.c) */
}

/* ============================================================================================ numerics */
/* FUN_1800b0200 (l.95479ff): Brent's method (Numerical Recipes zbrent form: tol1 = 2e-12 |b| + tol / 2, 100
   iterations); f is evaluated at a, then b, then at each iterate, in that order (callers rely on the side effects).
   Returns 1 with the root, 0 after 100 iterations (root = last iterate). */
typedef double (*RootFn)(void *ctx, double x);
static int brent(RootFn f, void *ctx, double x1, double x2, double tol, double *root) {
    double a = x1, b = x2, c = x2, d = 0, e = 0, fa = f(ctx, a), fb = f(ctx, b), fc = fb;
    for (int it = 1; it <= 100; it++) {
        if ((fb > 0 && fc > 0) || (fb < 0 && fc < 0)) { c = a; fc = fa; e = d = b - a; }
        if (fabs(fc) < fabs(fb)) { a = b; b = c; c = a; fa = fb; fb = fc; fc = fa; }
        double tol1 = fabs(b) * 2e-12 + tol * 0.5, xm = (c - b) * 0.5;
        if (fabs(xm) <= tol1 || fb == 0.0) { *root = b; return 1; }
        if (fabs(e) >= tol1 && fabs(fa) > fabs(fb)) {
            double s = fb / fa, p, q;
            if (a == c) { p = 2.0 * xm * s; q = 1.0 - s; }
            else { q = fa / fc; double r = fb / fc; p = s * (2.0 * xm * q * (q - r) - (b - a) * (r - 1.0)); q = (q - 1.0) * (r - 1.0) * (s - 1.0); }
            if (p > 0) q = -q;
            p = fabs(p);
            double m1 = 3.0 * xm * q - fabs(tol1 * q), m2 = fabs(e * q);
            if (2.0 * p < (m1 < m2 ? m1 : m2)) { e = d; d = p / q; } else { d = xm; e = d; }
        } else { d = xm; e = d; }
        a = b; fa = fb;
        if (fabs(d) > tol1) b += d; else b += xm < 0 ? -fabs(tol1) : fabs(tol1);
        fb = f(ctx, b);
    }
    *root = b; return 0;
}

/* ============================================================================================ section copy */
/* ============================================================================================ cutting states */
/* FUN_18006f7a0 (l.63160-63181): side of a tool-centre shape */
static int sh_side(const Sh *s) {
    if (!s) return 0;
    if (sh_island(s)) return s->dir == -1 ? -1 : 1;
    return s->dir == -1 ? 1 : -1;
}
/* FUN_18006cf10 (l.61813-61871): width limits with the rate-of-engagement ramp (§3 C.2) */
static void limits(Pock *C, int *ramp, double *l1, double *l2) {
    const McceParm *P = &C->P;
    *l1 = *l2 = P->s_work;
    if (P->ramp_len >= 1e-7 && *ramp && P->rate_of_eng) {
        double p1 = 0, p2 = 0;
        if (C->T.n) { p1 = C->T.r[C->T.n - 1].v[0]; p2 = C->T.r[C->T.n - 1].v[1]; }   /* FUN_18008ee10: last ring record */
        double k = (P->s_work - P->s_10) / P->ramp_len * P->step;
        *l1 = k + p1;
        if (P->r > 1e-7) *l2 = k + p2;
        if (*l1 < P->s_10) *l1 = P->s_10; else if (*l1 > P->s_170) *l1 = P->s_170;
        if (*l1 > P->s_work) *l1 = P->s_work;
        if (*l2 < P->s_10) *l2 = P->s_10; else if (*l2 > P->s_170) *l2 = P->s_170;
        if (*l2 > P->s_work) *l2 = P->s_work;
        if (!P->bull_avail && fabs(*l1 - P->s_work) < P->s_025) *ramp = 0;
    }
}
/* FUN_18006ee90 (l.62855-62865) */
static uint8_t firstpass(const Pock *C, const EnCut *E) { return (C->fp_on && E->pass <= C->P.num_red_pass) ? 0x20 : 0; }
static void encut_clear(EnCut *E) { memset(&E->m1, 0, sizeof E->m1); memset(&E->m2, 0, sizeof E->m2); }   /* FUN_18001cfe0 x 2 */

/* FUN_18006e250 / FUN_18006de20 (l.62203-62436): fraction u of the step where width(u) = lim - 1e-6 */
typedef struct { Pock *C; int sel; double lim; int dir; EnCut *E; Ent orig; } ShortenCtx;
static double shorten_f(void *vctx, double u) {
    ShortenCtx *x = vctx;
    encut_clear(x->E);
    x->E->e = e_cut(&x->orig, u);
    measure(x->C, NULL, x->dir, x->E);
    return (x->sel ? x->E->m1.w : x->E->m2.w) - (x->lim - 1e-6);
}
static int shorten_one(Pock *C, int sel, double lim, int dir, EnCut *E, double *u) {
    if (!sel && !C->bull.n) return 0;
    ShortenCtx x = { C, sel, lim, dir, E, E->e };
    double f0 = shorten_f(&x, 0.0), f1 = shorten_f(&x, 1.0);
    E->e = x.orig;
    if ((f0 > 0 && f1 > 0) || (f0 < 0 && f1 < 0)) return 0;
    brent(shorten_f, &x, 0.0, 1.0, 1e-7, u);
    E->e = x.orig;
    return 1;
}
/* FUN_18006dfa0 (l.62259-62367) */
static int shorten(Pock *C, double l1, double l2, int dir, EnCut *E) {
    Ent orig = E->e; double w1 = E->m1.w, w2 = E->m2.w, u1 = 0, u2 = 0, u = 0;
    int c1 = 0, c2 = 0;
    if (l1 <= w1) c1 = shorten_one(C, 1, l1, dir, E, &u1);
    if (l2 <= w2) { E->e = orig; c2 = shorten_one(C, 0, l2, dir, E, &u2); }
    if (c1 && c2) { if (u1 < u2) c2 = 0; else c1 = 0; }
    u = c1 ? u1 : c2 ? u2 : 0;
    encut_clear(E);
    E->e = e_cut(&orig, u);
    return c1 || c2;
}
/* FUN_18006e470 (l.62472-62537): no material, or the step end is farther than (R - r) + 1e-7 from it */
static int clear_of_material(Pock *C, const EnCut *E) {
    if (!C->mat.n) return 1;
    Pos q; if (!sl_nearest(&C->mat, e_end(&E->e), &q)) return 1;
    return pdist(q.pt, e_end(&E->e)) > (C->P.R - C->P.r) + 1e-7;
}
/* FUN_18006caa0 (l.61676-61810): lead-in of G + one full loop of tool-centre shape G.sh from the attach point */
static Sh *candidate_path(Pock *C, const Gbap *G) {
    Sh *p = sh_new(); p->dir = 0;
    for (int i = 0; i < G->lead.n; i++) sh_push(p, G->lead.e[i]);
    const Sh *b = C->cur.s[G->sh];
    double t0 = fmin(1, fmax(0, G->t)), t1 = t0 - 1e-12; int e1 = G->ent;
    if (t1 < 0) { e1 = G->ent - 1; if (e1 < 0) e1 = b->n - 1; t1 = 1.0; }
    sh_section(b, G->ent, t0, e1, t1, p);
    p->dir = 0; sh_upd_bbox(p);
    return p;
}
/* FUN_18006fc10 (l.63400-63418) -> FUN_18006f7d0 (l.63184-63397): the step = the longest piece (earliest wins) of the
   walked section, trimmed; a line becomes the chord pos.pt -> npos.pt */
static Ent path_step(const Sh *path, const Pos *a, const Pos *b) {
    double best = -1; int be = a->ent; double bta = a->t, btb = a->t;
    int e = a->ent; double ta = a->t, tb = a->ent == b->ent ? b->t : 1.0;
    for (int guard = 0; guard <= path->n; guard++) {
        if (e == b->ent) tb = b->t;
        const Ent *x = &path->e[e];
        double pl = x->id == 'L' ? pdist(e_at(x, tb), e_at(x, ta)) : x->id == 'A' ? fabs((tb - ta) * x->sw * x->r) : 0;
        if (pl > best) { best = pl; be = e; bta = ta; btb = tb; }
        if (e == b->ent) break;
        ta = 0; tb = 1.0; e = (e + 1) % path->n;
    }
    Ent r = e_sub(&path->e[be], bta, btb); r.ce = 0; r.z = 0;
    if (r.id == 'L') { r.p1 = a->pt; r.p2 = b->pt; }
    mcce_trace_pstep(path, a, b, &r);   /* ours: MCCE_TRACE_SW */
    return r;
}

/* FUN_18006d330 (l.61912-62145): state 2, march along the boundary */
static void state2(Pock *C, int unlimited, int *ramp, EnCut *E, Gbap *G, int *state) {
    McceParm *P = &C->P; Cetp *T = &C->T;
    double air_max = P->step15;
    double cut = 0, air = 0; int cutSeen = 0, trimAt = -1, last = 0;
    *ramp = 0;
    encut_clear(E);
    int dir = sh_side(C->cur.s[G->sh]);
    Sh *path = candidate_path(C, G);
    ShL pl = { 0 }; sl_push(&pl, path);
    if (!path->n) { sl_free(&pl); *state = 3; return; }
    Pos pos = { 0, 0, 0, e_start(&path->e[0]) }, npos;
    do {
        double l1, l2, len = 0;
        if (!unlimited) limits(C, ramp, &l1, &l2); else l1 = l2 = FLT_MAXD;
        if (!walk_fwd(1, P->step, &pl, &pos, &npos, &len)) break;
        encut_clear(E);
        E->e = path_step(path, &pos, &npos);
        measure(C, NULL, dir, E);
        if (!(E->m1.flags & 0x18) && E->m1.w < l1 && E->m2.w < l2) { commit(C, E); cut += len; }
        if (!(E->m1.flags & 0x98)) {
            if (shorten(C, l1, l2, dir, E)) {
                len = e_len(&E->e); last = 1;
                measure(C, NULL, dir, E);
                if (!(E->m1.flags & 0x18) && E->m1.w < l1 && E->m2.w < l2) { commit(C, E); cut += len; }
            }
        }
        uint32_t f = E->m1.flags;
        if (dbg_level() >= 4) fprintf(stderr, "  state2 step %c (%.4f,%.4f)-(%.4f,%.4f) len %.4f flags %x w1 %.5f l1 %.5f cut %.3f air %.3f mat %d\n", E->e.id, e_start(&E->e).x, e_start(&E->e).y, e_end(&E->e).x, e_end(&E->e).y, len, f, E->m1.w, l1, cut, air, C->mat.n);
        if (!(f & 8)) {
            if (!(f & 0x10)) {
                if (!(f & 0x80)) break;
                trimAt = -1;
                cetp_emit(T, P, &E->e, firstpass(C, E) | 0x10, f, E->m1.w, E->m2.w, E->m1.sweep, E->m2.sweep);
                cutSeen = 1; air = 0;
                if (unlimited && P->step - 1e-12 < cut) break;
            } else {
                air += len;
                if (cutSeen) {
                    if (air_max * 3.0 < air && clear_of_material(C, E)) break;
                    if (air > air_max && trimAt == -1) trimAt = T->n;
                } else if (air_max * 1.5 < air) break;
                mark_air_feed(P->flags & 1, E->e.ce, &E->m1.flags);
                cetp_emit(T, P, &E->e, 0x10, E->m1.flags, E->m1.w, E->m2.w, E->m1.sweep, E->m2.sweep);
            }
        } else air += len;
        pos = npos;
    } while (!last);
    *state = (!(E->m1.flags & 0x10) && C->mpos.sh != -1) ? 0 : 3;
    if (trimAt != -1) { if (getenv("MCCE_DEBUG") && atoi(getenv("MCCE_DEBUG")) >= 3) fprintf(stderr, "state2 trim %d -> %d\n", T->n, trimAt); cetp_trim(T, trimAt); }
    sl_free(&pl);
}

/* ============================================================================================ states 0/1 (§3 C.3, §6.2) */
/* FUN_1801204a0 (l.155171-155323): tangent leaving (ent, t), stepping over degenerate entities (tol2 squared) */
static P2 gp_tan_out(const Sh *s, int k, double t, double tol2);
static P2 gp_tan_in(const Sh *s, int k, double t, double tol2);
static P2 tan_out(const Sh *s, int ent, double t, double tol2) {
    if (swx_on()) return gp_tan_out(s, ent, t, tol2);   /* the DLL's own vector (gp_tan_vec); MCCE_MEASX=0: e_tan below */
    if (!s->n) return p2(0, 0);
    P2 P = e_at(&s->e[ent], t), E = e_end(&s->e[ent]);
    if (pdist2(P, E) < tol2 && (s->dir != 0 || ent != s->n - 1)) { ent = (ent + 1) % s->n; t = 0; }
    int e0 = ent, wrapped = 0;
    for (;;) {
        P2 v = e_tan(&s->e[ent], t);
        if (wrapped || pdot(v, v) > tol2 || (s->dir == 0 && ent == s->n - 1)) return v;
        ent = (ent + 1) % s->n; t = 0; if (ent == e0) wrapped = 1;
    }
}
/* FUN_18011ffd0 (l.155017-155168): tangent arriving at (ent, t), stepping back over degenerate entities */
static P2 tan_in(const Sh *s, int ent, double t, double tol2) {
    if (swx_on()) return gp_tan_in(s, ent, t, tol2);    /* the DLL's own vector (gp_tan_vec); MCCE_MEASX=0: e_tan below */
    if (!s->n) return p2(0, 0);
    P2 P = e_at(&s->e[ent], t), S = e_start(&s->e[ent]);
    if (pdist2(P, S) < tol2 && (s->dir != 0 || ent != 0)) { ent = ent > 0 ? ent - 1 : s->n - 1; t = 1.0; }
    int e0 = ent, wrapped = 0;
    for (;;) {
        P2 v = e_tan(&s->e[ent], t);
        if (wrapped || pdot(v, v) > tol2 || (s->dir == 0 && ent == 0)) return v;
        ent = ent > 0 ? ent - 1 : s->n - 1; t = 1.0; if (ent == e0) wrapped = 1;
    }
}
/* FUN_180028ac0 (l.26583-26615): nearest point of the current material to q and the material tangent there */
static int mat_dir(Pock *C, P2 q, P2 *pt, P2 *tan) {
    Pos p; if (!sl_nearest(&C->mat, q, &p)) return 0;
    if (pt) *pt = p.pt;
    *tan = tan_out(C->mat.s[p.sh], p.ent, p.t, 1.0000000000000002e-10);
    return 1;
}
static int last_pt(Pock *C, P2 *p) { return cetp_last_pt(&C->T, &C->prev, p); }
static int last_tan(Pock *C, P2 *v) { return cetp_last_tan(&C->T, &C->prev, v); }

/* ---------------------------------------------------------------------------- attach (FUN_180024720 -> FUN_180021e40, §6.2.5) */
/* FUN_180023dd0 (l.24413-24496): first record whose following section of the one-shape step is on / outside B */
static int first_exit(const Sh *S, const ShL *B, IsabL *I) {
    I->n = 0;
    if (!B->n || S->n < 1 || S->n > 2) return 0;
    isect1(S, B, I);
    for (int i = 0; i < I->n; i++) {
        int e2; double t2, tol;
        if (i + 1 < I->n) { e2 = I->r[i + 1].xe; t2 = I->r[i + 1].xt; tol = 1e-5; } else { e2 = I->r[i].xe; t2 = 1.0; tol = 1e-7; }
        if (!short_sec(S, I->r[i].xe, I->r[i].xt, e2, t2, tol)) {
            int c = sec_in_list(S, I->r[i].xe, I->r[i].xt, e2, t2, 0, B, 1e-5);
            if (c == 1 || c == 4) return i;
        }
    }
    return I->n;
}
/* FUN_1800241c0 (l.24503-24573): a single-entity step wholly outside B whose start is within 1e-4 of B */
static int touch_start(const Sh *S, const ShL *B, IsabL *I) {
    I->n = 0;
    if (S->n == 1 && sec_in_list(S, 0, 0.0, 0, 1.0, 0, B, 1e-5) == 4) {
        Pos q; if (sl_nearest(B, e_start(&S->e[0]), &q) && pdist(q.pt, e_start(&S->e[0])) <= 1e-4) {
            Isab r; memset(&r, 0, sizeof r); r.pt = q.pt; r.ys = q.sh; r.ye = q.ent; r.yt = q.t; r.next = r.prev = -1; il_push(I, r);
        }
    }
    return 0;
}
/* FUN_180022a50 (l.23796-23976) */
static void attach_direct(Pock *C, Ent *step, Gbap *G) {
    Sh S; memset(&S, 0, sizeof S); sh_push(&S, *step); S.dir = 0; sh_upd_bbox(&S);
    IsabL I = { 0 };
    int i = first_exit(&S, &C->cur, &I);
    if (i >= I.n) { i = touch_start(&S, &C->cur, &I); if (i >= I.n) { il_free(&I); free(S.e); return; } }
    *step = e_cut(step, I.r[i].xt);
    G->valid = 1; G->lead.n = 0;
    G->sh = I.r[i].ys; G->ent = I.r[i].ye; G->t = I.r[i].yt; G->pt = I.r[i].pt;
    if (G->t < 0 || G->t > 1) {
        Pos q; if (sh_nearest(C->cur.s[G->sh], G->pt, &q) && pdist2(q.pt, G->pt) < 1e-18 && q.t >= 0 && q.t <= 1) { G->ent = q.ent; G->t = q.t; }
    }
    il_free(&I); free(S.e);
}
/* FUN_180020610 (l.22712-23019): half annulus at the step end */
static int half_annulus(Pock *C, const Ent *step, Sh *X) {
    const McceParm *P = &C->P; int climb = P->flags & 1, ok;
    P2 t = unit(e_tan(step, 1), &ok); if (!ok) return 0;
    P2 n = climb ? p2(t.y, -t.x) : p2(-t.y, t.x);
    P2 Pe = e_end(step), c = psub(Pe, pmul(n, P->min_rad));
    sh_clear(X);
    Ent e0 = ent_arc(c, (P->R - P->r) + P->min_rad, atan2(n.y, n.x), climb ? M_PI : -M_PI);
    Ent e2 = ent_arc(c, P->min_rad, atan2(e_end(&e0).y - c.y, e_end(&e0).x - c.x), -e0.sw);
    sh_push(X, e0); sh_push(X, ent_line(e_end(&e0), e_start(&e2))); sh_push(X, e2); sh_push(X, ent_line(e_end(&e2), e_start(&e0)));
    X->dir = climb ? 1 : -1; sh_upd_bbox(X);
    return 1;
}
/* FUN_1800244a0 (l.24578-24614): material sections (crossing -> next crossing on that shape) inside / on X -> class 3 */
static void mark3(Pock *C, const Sh *X, IsabL *I2) {
    for (int i = 0; i < I2->n; i++) {
        Isab *r = &I2->r[i]; const Sh *m = C->mat.s[r->ys]; int j = r->next >= 0 ? r->next : i;
        if (sec_in_shape(m, r->ye, r->yt, I2->r[j].ye, I2->r[j].yt, I2->n == 1, X, 1e-5) != 4) r->cls = 3;
    }
}
/* FUN_1800236f0 (l.24238-24408): circles of radius R - r along e2 between fractions must all meet the material */
static int circles_meet(Pock *C, const Sh *X, double t0, double t1) {
    const McceParm *P = &C->P;
    if (X->n != 4 || X->e[0].id != 'A' || X->e[1].id != 'L' || X->e[2].id != 'A' || X->e[3].id != 'L') return 0;
    if (t0 > t1 || fabs(X->e[2].sw + X->e[0].sw) > 1e-7 || fabs(X->e[2].r - P->min_rad) > 1e-7) return 0;
    double L = fabs(X->e[2].r * X->e[2].sw * (t1 - t0)); int n = (int)floor(L / (P->R - P->r) + 0.5); if (n < 3) n = 3;
    for (double t = t0; t < t1 + 1e-7; t += (t1 - t0) / n) {
        Sh c; memset(&c, 0, sizeof c); sh_push(&c, ent_arc(e_at(&X->e[2], 1 - t), P->R - P->r, 0, 2 * M_PI)); c.dir = 1; sh_upd_bbox(&c);
        IsabL I = { 0 }; isect1(&c, &C->mat, &I); int hit = I.n > 0; il_free(&I); free(c.e);
        if (!hit) return 0;
        if (t1 - t0 <= 0) break;
    }
    return 1;
}
/* cyclic scan from the last record: a class-3 record, then the first following non-class-3 record (FUN_180024d50 /
   FUN_180024a00 l.24750-24776) */
static int after_class3(const IsabL *I2) {
    int n = I2->n; if (!n) return -1;
    int i = n - 1; while (I2->r[i].cls != 3) { i = (i + 1) % n; if (i == n - 1) return -1; }
    int k = i; while (I2->r[k].cls == 3) { k = (k + 1) % n; if (k == i) return -1; }
    return k;
}
/* FUN_180023460 (l.24140-24233): probe; true = go on to the offset attach */
static int probe(Pock *C, const Ent *step) {
    Sh X; memset(&X, 0, sizeof X);
    if (!half_annulus(C, step, &X)) { free(X.e); return 0; }
    IsabL I1 = { 0 }, I2 = { 0 }; int ret = 0;
    isect1(&X, &C->c3e8, &I1);
    if (I1.n && I1.r[0].xe < 1) {
        isect1(&X, &C->mat, &I2);
        if (I2.n) {
            mark3(C, &X, &I2);
            int k = after_class3(&I2), contact = 0;
            if (k >= 0 && I2.r[k].xe == 0 && I2.r[k].xt <= I1.r[0].xt && pdist2(I2.r[k].pt, I1.r[0].pt) >= 1e-10 && circles_meet(C, &X, I2.r[k].xt, I1.r[0].xt)) contact = 1;
            ret = !contact;
        }
    }
    il_free(&I1); il_free(&I2); free(X.e);
    return ret;
}
/* FUN_18001ff60 (l.22510-22709): band outside the lead-in arc */
static int leadin_band(Pock *C, const Gbap *G, Sh *X) {
    const McceParm *P = &C->P;
    if (!G->valid || G->lead.n != 1 || G->lead.e[0].id != 'A' || fabs(G->lead.e[0].r * G->lead.e[0].sw) < 1e-7) return 0;
    const Ent *a = &G->lead.e[0];
    Ent e0 = *a; e0.r = a->r + ((P->R - P->r) - 1e-4);
    P2 P0 = e_end(&e0);
    if (fabs(P0.x - a->c.x) <= 1e-12 && fabs(P0.y - a->c.y) <= 1e-12) return 0;
    Ent e2 = *a; e2.sa = atan2(P0.y - a->c.y, P0.x - a->c.x); e2.sw = -a->sw;
    sh_clear(X); sh_push(X, e0); sh_push(X, ent_line(e_end(&e0), e_start(&e2))); sh_push(X, e2); sh_push(X, ent_line(e_end(&e2), e_start(&e0)));
    X->dir = (P->flags & 1) ? 1 : -1; sh_upd_bbox(X);
    return 1;
}
/* FUN_180023100 (l.24029-24135): drop material crossings that coincide with crossings of C+0x3e8 */
static void drop_common(const IsabL *I1, IsabL *I2) {
    for (int a = 0; a < I1->n; a++) {
        int k = -1; double bd = FLT_MAXD;
        for (int j = 0; j < I2->n; j++) { double d = pdist2(I2->r[j].pt, I1->r[a].pt); if (d < bd) { bd = d; k = j; } }
        if (k < 0 || !(bd <= 1e-6 && (bd <= 1e-12 || I1->r[a].xe == I2->r[k].xe))) continue;
        for (int j = 0; j < I2->n; j++) { if (j == k) continue;
            int *f[2] = { &I2->r[j].prev, &I2->r[j].next };
            for (int q = 0; q < 2; q++) { if (*f[q] == k) *f[q] = q == 0 ? I2->r[k].prev : I2->r[k].next; if (*f[q] == k) *f[q] = j; if (*f[q] > k) (*f[q])--; } }
        memmove(&I2->r[k], &I2->r[k + 1], (I2->n - k - 1) * sizeof(Isab)); I2->n--;
    }
}
/* FUN_1800247d0 (l.24638-24724): 1 = reject the attach */
static int leadin_reject(Pock *C, const Gbap *G) {
    Sh X; memset(&X, 0, sizeof X);
    if (!leadin_band(C, G, &X)) { free(X.e); return 1; }
    IsabL I1 = { 0 }, I2 = { 0 };
    isect1(&X, &C->c3e8, &I1); isect1(&X, &C->mat, &I2);
    drop_common(&I1, &I2);
    mark3(C, &X, &I2);
    int r = 0;
    if (I2.n) {   /* FUN_180024a00 (l.24727-24804) */
        int k = after_class3(&I2);
        if (k >= 0 && I2.r[k].xe == 0 && pdist2(I2.r[k].pt, e_end(&X.e[0])) >= 1.0000000000000002e-10) r = !circles_meet(C, &X, I2.r[k].xt, 1.0);
    }
    il_free(&I1); il_free(&I2); free(X.e);
    return r;
}
/* FUN_180025040 + FUN_180026120 / 26170 / 25f80 inside FUN_180025700 (l.24910-25457): offset copy of the step */
static int offset_step(Pock *C, const Ent *step, Sh *S2) {
    const McceParm *P = &C->P;
    sh_clear(S2);
    if (e_len(step) * e_len(step) < 1e-8) return 0;
    double rho = (P->flags & 1) ? P->min_rad : -P->min_rad;
    P2 lt;
    if (last_tan(C, &lt)) {
        double a = atan2(lt.y, lt.x); P2 bt = e_tan(step, 0); double b = atan2(bt.y, bt.x), d = wrap_pi(b - a);
        if (!((d > 0 && rho > 0) || (d < 0 && rho < 0))) {
            double sg = rho > 0 ? 1 : -1, sa = wrap_pi(a + sg * M_PI / 2);
            sh_push(S2, ent_arc(e_start(step), fabs(rho), sa, wrap_pi(wrap_pi(b + sg * M_PI / 2) - sa)));
        }
    }
    Ent o = *step;
    if (step->id == 'L') { int ok; P2 u = unit(psub(step->p2, step->p1), &ok); if (!ok) return 0; P2 sh = pmul(p2(-u.y, u.x), rho); o.p1 = padd(o.p1, sh); o.p2 = padd(o.p2, sh); }
    else if (step->id == 'A') {
        double r2 = step->r + (step->sw >= 0 ? -rho : rho);
        if (r2 < -1e-12) { o.r = fabs(r2); o.sa = wrap_pi(o.sa + M_PI); } else o.r = fmax(r2, 0);
        if (fabs(o.r * o.sw) < 1e-12) return 0;
    } else return 0;
    sh_push(S2, o); S2->dir = 0; sh_upd_bbox(S2);
    return 1;
}
/* FUN_180025a30 (l.25161-25302): lead-in arc from the crossing to the boundary */
static void lead_in(Pock *C, const Ent *s, P2 X, Gbap *G) {
    const McceParm *P = &C->P; double m = P->min_rad;
    P2 Pe = e_end(s); Pos Bp; sl_nearest(&C->cur, X, &Bp);
    G->lead.n = 0;
    if (fabs(Pe.x - X.x) <= 1e-6 && fabs(Pe.y - X.y) <= 1e-6) { G->valid = 1; mcce_trace_leadin(s, X, &G->lead); return; }
    double a = atan2(Pe.y - X.y, Pe.x - X.x), b = atan2(Bp.pt.y - X.y, Bp.pt.x - X.x), d = wrap_pi(b - a);
    if (P->flags & 1) { if (d < 0) d += 2 * M_PI; } else if (d > 0) d -= 2 * M_PI;
    if (fabs(d * m) <= 1e-7 || 2 * M_PI - fabs(d) < 0.017453292519943295) {
        Pos B2; sl_nearest(&C->cur, Pe, &B2);
        if (pdist2(Pe, Bp.pt) > pdist2(Pe, B2.pt)) Bp = B2;
    } else sh_push(&G->lead, ent_arc(X, m, a, d));
    sh_upd_bbox(&G->lead);
    G->sh = Bp.sh; G->ent = Bp.ent; G->t = Bp.t; G->pt = Bp.pt; G->valid = 1;
    mcce_trace_leadin(s, X, &G->lead);   /* ours: MCCE_TRACE_SW */
}
/* FUN_180021f70 (l.23368-23575) */
static int offset_attach(Pock *C, Ent *step, Gbap *G) {
    Gbap G2; memset(&G2, 0, sizeof G2); gbap_copy(&G2, G);
    Sh S2; memset(&S2, 0, sizeof S2); IsabL I = { 0 }; int ok = 0;
    if (offset_step(C, step, &S2)) {
        int i = first_exit(&S2, &C->c3c0, &I);
        if (i < I.n) {
            double u = (S2.n == 2 && I.r[i].xe == 0) ? 0 : I.r[i].xt;
            Ent s = e_cut(step, u);
            lead_in(C, &s, I.r[i].pt, &G2);
            int big = 0; for (int k = 0; k < G2.lead.n; k++) if (G2.lead.e[k].id == 'A' && fabs(G2.lead.e[k].sw) > M_PI) big = 1;   /* FUN_180024f90 */
            if (!big && !(!G->valid && leadin_reject(C, &G2))) { *step = s; gbap_copy(G, &G2); ok = 1; }
        }
    }
    il_free(&I); free(S2.e); gbap_free(&G2);
    return ok;
}
/* FUN_1800216c0 (l.23157-23330): U-turn arc at the step end */
static void uturn(Pock *C, const Ent *step, Gbap *G) {
    const McceParm *P = &C->P; int climb = P->flags & 1, ok;
    G->valid = 0; G->lead.n = 0;
    P2 t = unit(e_tan(step, 1), &ok); if (!ok) return;
    P2 n = climb ? p2(t.y, -t.x) : p2(-t.y, t.x);
    Ent a = ent_arc(psub(e_end(step), pmul(n, P->min_rad)), P->min_rad, atan2(n.y, n.x), climb ? M_PI : -M_PI);
    Sh L; memset(&L, 0, sizeof L); sh_push(&L, a); L.dir = 0; sh_upd_bbox(&L);
    IsabL I = { 0 }; isect1(&L, &C->cur, &I);
    if (I.n && I.r[0].xt >= 0) {
        a = e_cut(&a, I.r[0].xt);
        if (fabs(a.r * a.sw) >= 1e-7) {
            sh_push(&G->lead, a); sh_upd_bbox(&G->lead);
            G->sh = I.r[0].ys; G->ent = I.r[0].ye; G->t = I.r[0].yt; G->pt = I.r[0].pt; G->valid = 1;
            if (leadin_reject(C, G)) { G->valid = 0; G->lead.n = 0; }
        }
    }
    il_free(&I); free(L.e);
}
/* FUN_180022500 (l.23582-23793) */
static void uturn_attach(Pock *C, Ent *step, Gbap *G) {
    Ent orig = *step;
    uturn(C, step, G);
    if (G->valid) {
        double lo = 0, hi = 1;
        Gbap Gm; memset(&Gm, 0, sizeof Gm);
        for (int k = 0; k < 4; k++) {
            double mid = (lo + hi) / 2; Ent sm = e_cut(&orig, mid);
            uturn(C, &sm, &Gm);
            if (Gm.valid) { *step = sm; gbap_copy(G, &Gm); hi = mid; } else lo = mid;
        }
        gbap_free(&Gm);
    }
}
/* FUN_180024720 -> FUN_180021e40 (l.23335-23361) */
static void attach(Pock *C, Ent *step, Gbap *G) {
    gbap_clear(G);
    attach_direct(C, step, G);
    if ((G->valid || probe(C, step)) && !offset_attach(C, step, G) && !G->valid) uturn_attach(C, step, G);
}

/* ---------------------------------------------------------------------------- trial steps */
static void trial_line(Pock *C, double ang, double len, const Pos *pos, EnCut *E, Gbap *G);
/* FUN_18001c510 (l.20655-20905): zero-length attached step: the step becomes the walked piece of the candidate path from its
   start (FUN_18006fc10(path, start, np, step) l.20759 - the start position local_a8 is set once before the walk, l.20740-20743),
   and the attach point moves to its end. (Before 2026-11-13 the step was left at zero length: buckle-v12 op 10 commit 446, the
   DLL's trial to (76.4419, 13.4669) measured as a zero step, the engine then jumped.) */
static void zero_len_step(Pock *C, Ent *step, Gbap *G) {
    Sh *path = candidate_path(C, G); ShL pl = { 0 }; sl_push(&pl, path);
    if (path->n) {
        Pos pos = { 0, 0, 0, e_start(&path->e[0]) }, np; double acc = 0, l;
        const Pos start = pos;
        for (int g = 0; g < 100000; g++) {
            if (!walk_fwd(1, C->P.step, &pl, &pos, &np, &l)) break;
            acc += l;
            if (acc > 1e-6) {
                *step = path_step(path, &start, &np);
                if (G->lead.n) {
                    Pos q; sh_nearest(&G->lead, np.pt, &q);
                    Sh rest; memset(&rest, 0, sizeof rest);
                    sh_section(&G->lead, q.ent, q.t, G->lead.n - 1, 1.0, &rest);
                    G->lead.n = 0;
                    if (sh_len(&rest) > 1e-8) for (int k = 0; k < rest.n; k++) sh_push(&G->lead, rest.e[k]);
                    free(rest.e);
                }
                P2 X = G->lead.n ? e_end(&G->lead.e[G->lead.n - 1]) : np.pt;
                Pos q; if (sh_nearest(C->cur.s[G->sh], X, &q)) { G->ent = q.ent; G->t = q.t; G->pt = q.pt; }
                break;
            }
            pos = np;
        }
    }
    sl_free(&pl);
}
/* FUN_180017a10 (l.18381-18471): trial line from the last point, heading = last tangent (or the material direction) + ang */
static void mcce_trace_trial(double ang, double len);   /* ours: MCCE_TRACE_LEVEL >= 5 "T" lines (mcce_trace.c) */
static void mcce_trace_tan(const char *k, int r, P2 v);   /* ours: MCCE_TRACE_SW level 5 "TAN" lines (mcce_trace.c) */
static void trial_line(Pock *C, double ang, double len, const Pos *pos, EnCut *E, Gbap *G) {
    mcce_trace_trial(ang, len);
    encut_clear(E); gbap_clear(G);
    P2 P = p2(0, 0), t = p2(0, 0);
    last_pt(C, &P);
    int lt = last_tan(C, &t); mcce_trace_tan("lt", lt, t);
    if (!lt) { int md = mat_dir(C, P, NULL, &t); mcce_trace_tan("md", md, t); }
    double base = (fabs(t.x) > 1e-12 || fabs(t.y) > 1e-12) ? atan2(t.y, t.x) : 0;
    E->e = ent_line(P, p2(cos(ang + base) * len + P.x, sin(ang + base) * len + P.y));
    attach(C, &E->e, G);
    int dir = 0;
    double l2 = e_len(&E->e); l2 *= l2;
    if (l2 < 1.0000000000000002e-10 && G->valid) {
        zero_len_step(C, &E->e, G);
        if (!G->lead.n) dir = sh_side(C->cur.s[G->sh]);
    }
    E->head = ang;
    measure(C, pos, dir, E);
}
/* predicates (l.20296-20365) */
static int inside_lim(double l1, double l2, const EnCut *E) { return E->m1.w < l1 && E->m2.w < l2; }   /* FUN_18001bb30 */
static int within_tol(const Pock *C, double l1, double l2, const EnCut *E) {   /* FUN_18001bab0 */
    double s = C->P.s_025, w1 = E->m1.w, w2 = E->m2.w;
    return !((fabs(w1 - l1) >= s || w2 >= l2 + s) && (fabs(w2 - l2) >= s || w1 >= l1 + s));
}
static int above_half_deg(const Pock *C, const EnCut *E) {   /* FUN_18001b9a0 */
    double s = ln_stepover(0.008726646, C->P.R - C->P.r); return !inside_lim(s, s, E);
}
static int wider_than_last(const Pock *C, const EnCut *E) {  /* FUN_18001ba10 */
    double p1 = 0, p2v = 0; if (C->T.n) { p1 = C->T.r[C->T.n - 1].v[0]; p2v = C->T.r[C->T.n - 1].v[1]; }
    return !(E->m1.w <= p1 && (C->P.r <= 1e-7 || E->m2.w <= p2v));
}
/* FUN_18001c2c0 (l.20596-20648) */
static int small_piece(const Pock *C, const Pos *pos, double d) {
    if (pos->sh < 0 || pos->sh >= C->mat.n) return 0;
    double L = 0; const Sh *s = C->mat.s[pos->sh];
    for (int i = 0; i < s->n; i++) { L += e_len(&s->e[i]); if (L > 2 * d) return 0; }
    return 1;
}
/* FUN_18001efb0 (l.22132-22192): tool centre placed at width w against the wall point A, seen from B */
static void mcce_trace_place(double tol, double w, P2 A, P2 B, int r, P2 out);   /* ours: MCCE_PLACEDBG (mcce_trace.c) */
static int place_(const Pock *C, double tol, double w, P2 A, P2 B, P2 *out);
static int place(const Pock *C, double tol, double w, P2 A, P2 B, P2 *out) { *out = p2(0, 0); int r = place_(C, tol, w, A, B, out); mcce_trace_place(tol, w, A, B, r, *out); return r; }
static int place_(const Pock *C, double tol, double w, P2 A, P2 B, P2 *out) {
    double rho = C->P.R - C->P.r, phi = ln_eng_ang(rho, w), a = phi + M_PI / 2, b = a > M_PI ? 2 * M_PI - a : a;
    double h = rho * sin(b), D = pdist(B, A);
    if (D < 1.1920928955078125e-07 || D < h || (h < D && D < rho)) return 0;
    double al = (M_PI - b) - asin(h / D);
    double be = (fabs(B.x - A.x) <= 1.1920928955078125e-07 && fabs(B.y - A.y) <= 1.1920928955078125e-07) ? 0 : atan2(B.y - A.y, B.x - A.x);
    int climb = C->P.flags & 1;
    double th = (climb ? (a > M_PI) : (a <= M_PI)) ? be + al : be - al;
    *out = p2(A.x + rho * cos(th), A.y + rho * sin(th));
    return pdist2(*out, B) >= tol * tol;
}
/* FUN_1800186a0 (l.18717-18785) */
static int wall_target(Pock *C, double l1, const Pos *pos, double d, P2 *out) {
    const McceParm *P = &C->P;
    double tol = fmax(0.1 * P->step, 1e-4), Lmax = M_PI * (P->R - P->r);
    P2 B; if (!last_pt(C, &B)) return 0;
    if (pos->sh < 0 || pos->sh >= C->mat.n || pos->ent < 0) return 0;
    if (P->bull_avail && small_piece(C, pos, d)) {
        Pos A; if (sh_farthest(C->mat.s[pos->sh], B, &A) && place(C, 1e-5, l1, A.pt, B, out)) return 1;   /* FUN_18013caf0: farthest (§6.11.7) */
    }
    double walked = d; Pos A;
    int ok = walk_fwd(0, d, &C->mat, pos, &A, NULL);
    while (ok) {
        if (place(C, tol, l1, A.pt, B, out)) return 1;
        if (walked > Lmax) break;
        walked += d; ok = walk_fwd(0, walked, &C->mat, pos, &A, NULL);
    }
    return 0;
}
/* FUN_180019ec0 / FUN_180019de0 (l.19390-19490): candidate A aimed at the wall target */
static int cand_A(Pock *C, double l1, const Pos *pos, EnCut *E, Gbap *G, double *d) {
    const McceParm *P = &C->P; *d = 0;
    P2 B, Q; int ok = 0;
    if (last_pt(C, &B) && wall_target(C, l1, pos, P->step, &Q)) {
        /* length sqrt(dy dy + dx dx) (l.19454 / 19462; hypot can differ in the last bit: MCCE_MEASX=0) */
        double dx = Q.x - B.x, dy = Q.y - B.y; *d = swx_on() ? sqrt(dy * dy + dx * dx) : hypot(dx, dy);
        if (P->step * 1.5 < *d && wall_target(C, l1, pos, P->step * 0.5, &Q)) { dx = Q.x - B.x; dy = Q.y - B.y; *d = swx_on() ? sqrt(dy * dy + dx * dx) : hypot(dx, dy); }
        P2 t; if (!last_tan(C, &t)) mat_dir(C, B, NULL, &t);
        if (!((fabs(dx) <= 1e-12 && fabs(dy) <= 1e-12) || (fabs(t.x) <= 1e-12 && fabs(t.y) <= 1e-12))) {
            trial_line(C, wrap_pi(atan2(dy, dx) - atan2(t.y, t.x)), *d, pos, E, G); ok = 1;
        }
    }
    if (!ok) {
        if (!P->bull_avail) return 0;
        *d = P->step; trial_line(C, 0.0, P->step, pos, E, G);
    }
    return 1;
}
/* FUN_18001b5d0 (l.20153-20293): the last output entity as a line */
static int last_as_line(Pock *C, P2 *S, P2 *Pe) {
    if (!C->T.n) return 0;
    const Ent *e = &C->T.r[C->T.n - 1].e;
    if (e->id == 'L') { *S = e->p1; *Pe = e->p2; return 1; }
    if (e->id == 'A' && e->r >= 1e-7 && fabs(e->sw) > 1e-7) {
        double x = 1 - C->P.step * C->P.step / (2 * e->r * e->r); if (x < -1) x = -1; if (x > 1) x = 1;
        double ph = acos(x); if (fabs(e->sw) < ph) ph = fabs(e->sw);
        *S = e_at(e, (fabs(e->sw) - ph) / fabs(e->sw)); *Pe = e_end(e); return 1;
    }
    return 0;
}
/* FUN_18001bb60 (l.20372-20591): heading along a circle of radius rl through the last line and the step end */
static int circ_heading(Pock *C, const EnCut *A, double *out) {
    double rl = C->P.min_cut_rad; P2 S, Pe;
    if (!last_as_line(C, &S, &Pe)) return 0;
    P2 Q = e_end(&A->e), d = psub(Pe, S);
    double cr = pcross(d, psub(Q, S)); int side = cr > 0 ? 1 : cr < 0 ? -1 : 0;
    if (!side) return 0;
    double Lp = plen(d), h = sqrt(rl * rl - (Lp / 2) * (Lp / 2));
    P2 n = side == 1 ? p2(-d.y, d.x) : p2(d.y, -d.x); int ok; n = unit(n, &ok); if (!ok) return 0;
    P2 O = padd(pmul(padd(S, Pe), 0.5), pmul(n, h));
    if (!(pdist(O, Q) <= rl)) return 0;
    if (pdist2(Pe, O) == 0) return 0;
    double L = A->m1.len, al = acos(1 - L * L / (2 * rl * rl));   /* FUN_180018170 (l.18569-18596), no clamp */
    double th = atan2(Pe.y - O.y, Pe.x - O.x) + (side == 1 ? al : -al);
    P2 X = p2(O.x + rl * cos(th), O.y + rl * sin(th));
    P2 t; if (!last_tan(C, &t)) return 0;
    *out = wrap_pi(atan2(X.y - Pe.y, X.x - Pe.x) - atan2(t.y, t.x));
    return !isnan(*out);
}
/* FUN_180018990 (l.18790-18816) */
static int cand_half(Pock *C, double l1, double l2, double d, const Pos *pos, const EnCut *A, EnCut *E, Gbap *G) {
    double a; double ang = circ_heading(C, A, &a) ? a : A->head;
    trial_line(C, 0.5 * ang, d, pos, E, G);
    return inside_lim(l1, l2, E) && (E->m1.flags & 0x100);
}
/* perf (behaviour-identical): trial_line is a pure function of (the pocket state, heading, length, pos) and leaves the
   state unchanged, so within one cand_bracket (both circle searches + their Brent refinements, the state fixed) a
   heading evaluated before is taken from this memo instead of being measured again (the circle 1 and circle 2 scans
   sample the same headings when they run the same way; Brent's first two points are the scan's). The memo restores
   exactly what trial_line writes: E->e / head / m1 / m2 (pass untouched, as encut_clear) and the Gbap. Keyed on the
   heading's bits (len / pos are fixed per cand_bracket). */
#define TL_MEMO 160
typedef struct { double h; Ent e; double head; MBlk m1, m2; Gbap G; } TlMemoE;
typedef struct { TlMemoE r[TL_MEMO]; int n; double d; const Pos *pos; } TlMemo;
static void tl_memo_free(TlMemo *M) { for (int i = 0; i < M->n; i++) gbap_free(&M->r[i].G); M->n = 0; }
static void trial_line_m(TlMemo *M, Pock *C, double ang, double len, const Pos *pos, EnCut *E, Gbap *G) {
    static _Thread_local int nomemo = -1; if (nomemo < 0) nomemo = getenv("MCCE_NOMEMO") != NULL;   /* ours: oracle traces show every trial */
    if (nomemo) M = NULL;
    if (M && len == M->d && pos == M->pos)
        for (int i = 0; i < M->n; i++) if (!memcmp(&M->r[i].h, &ang, sizeof ang)) {
            TlMemoE *q = &M->r[i];
            E->e = q->e; E->head = q->head; E->m1 = q->m1; E->m2 = q->m2; gbap_copy(G, &q->G);
            return;
        }
    trial_line(C, ang, len, pos, E, G);
    if (M && len == M->d && pos == M->pos && M->n < TL_MEMO) {
        TlMemoE *q = &M->r[M->n++]; memset(q, 0, sizeof *q);
        q->h = ang; q->e = E->e; q->head = E->head; q->m1 = E->m1; q->m2 = E->m2; gbap_copy(&q->G, G);
    }
}
/* bracket search records (FUN_18001aa70 locals) */
typedef struct { Pock *C; int sel; double l1, l2, d; const Pos *pos; EnCut *Ehi, *Ebest; Gbap *Ghi, *Gbest; EnCut *Et; Gbap *Gt; TlMemo *M; } Srch;
static double ensum(const EnCut *E) { return E->m1.w + E->m2.w; }
/* FUN_18001d9e0 (l.21336-21377) */
static void keep(Srch *x, const EnCut *E, const Gbap *G) {
    const McceParm *P = &x->C->P;
    if (!inside_lim(x->l1 + P->s_025, x->l2 + P->s_025, E)) return;
    if (E->m1.flags & 0x100) { if (!(x->Ehi->m1.flags & 0x100) || ensum(E) > ensum(x->Ehi)) { *x->Ehi = *E; gbap_copy(x->Ghi, G); } }
    else if (!x->Ebest->e.id || ensum(E) > ensum(x->Ebest)) { *x->Ebest = *E; gbap_copy(x->Gbest, G); }
}
/* FUN_180019180 (l.19032-19147) = the Brent callback LAB_1800011ef (thunk: jmp 0x180019180 at 0x1800011ef) */
static double refine_f(void *vx, double h) {
    Srch *x = vx;
    trial_line_m(x->M, x->C, h, x->d, x->pos, x->Et, x->Gt); keep(x, x->Et, x->Gt);
    return x->sel ? x->Et->m1.w - x->l1 : x->Et->m2.w - x->l2;
}
/* FUN_18001cc60 (l.20910-20952) + FUN_180019960 (l.19293-19388) bracket; FUN_1800194e0 (l.19154-19288) refine;
   FUN_18001aa70 (l.19787-20123) search */
static int search(Srch *x, const EnCut *A) {
    Pock *C = x->C; const McceParm *P = &C->P;
    if (!(x->sel == 1 || C->bull.n)) return 0;
    double wA = x->sel ? A->m1.w : A->m2.w, lim = x->sel ? x->l1 : x->l2;
    int up = (P->flags & 1) ? (lim < wA) : (wA <= lim);
    P2 ta = e_tan(&A->e, 0); double hA = atan2(ta.y, ta.x);
    double span = wrap_pi(wrap_pi(hA - A->head + M_PI) - hA);
    if (up) { if (span < 0) span += 2 * M_PI; } else if (span > 0) span -= 2 * M_PI;
    int found = 0; double hlo = A->head;
    for (int i = 1; i <= 50; i++) {
        double h = A->head + (i / 50.0) * span;
        trial_line_m(x->M, C, h, x->d, x->pos, x->Et, x->Gt); keep(x, x->Et, x->Gt);
        double w = x->sel ? x->Et->m1.w : x->Et->m2.w;
        if ((wA >= lim && w < lim) || (wA < lim && w > lim)) { found = 1; hlo = h; break; }
    }
    if (found) { double r; brent(refine_f, x, A->head, hlo, 1e-7, &r); }
    return (x->Ehi->m1.flags & 0x100) || x->Ebest->e.id;
}
static int search_result(Srch *x, EnCut *E, Gbap *G) {
    if (x->Ehi->m1.flags & 0x100) { *E = *x->Ehi; gbap_copy(G, x->Ghi); return 1; }
    if (x->Ebest->e.id) { *E = *x->Ebest; gbap_copy(G, x->Gbest); return 1; }
    return 0;
}
/* FUN_18001a2b0 (l.19498-19779): circle 1 / circle 2 bracket searches */
static int cand_bracket(Pock *C, double l1, double l2, double d, const Pos *pos, const EnCut *A, EnCut *E, Gbap *G) {
    EnCut Eh[2], Eb[2], Et, R[2]; Gbap Gh[2], Gb[2], Gt, GR[2]; int ok[2] = { 0, 0 };
    memset(Eh, 0, sizeof Eh); memset(Eb, 0, sizeof Eb); memset(&Et, 0, sizeof Et); memset(R, 0, sizeof R);
    memset(Gh, 0, sizeof Gh); memset(Gb, 0, sizeof Gb); memset(&Gt, 0, sizeof Gt); memset(GR, 0, sizeof GR);
    static _Thread_local TlMemo memo; memo.n = 0; memo.d = d; memo.pos = pos;   /* static: 160 records are too big for the stack; cand_bracket does not recurse */
    for (int k = 0; k < 2; k++) {
        int sel = k == 0 ? 1 : 0;
        if (sel == 0 && !C->bull.n) break;
        Srch x = { C, sel, l1, l2, d, pos, &Eh[k], &Eb[k], &Gh[k], &Gb[k], &Et, &Gt, &memo };
        search(&x, A);
        ok[k] = search_result(&x, &R[k], &GR[k]);
    }
    int pick = -1;
    if (!ok[0]) pick = ok[1] ? 1 : -1;
    else if (!ok[1]) pick = 0;
    else {
        int f1 = R[0].m1.flags & 0x100, f2 = R[1].m1.flags & 0x100;
        if (!f1 && f2) pick = 1; else if (f1 && !f2) pick = 0;
        else { double a; int b1 = circ_heading(C, &R[0], &a), b2 = circ_heading(C, &R[1], &a);
               if (!b1 && b2) pick = 0; else if (b1 && !b2) pick = 1; else pick = ensum(&R[0]) < ensum(&R[1]) ? 1 : 0; }
    }
    if (pick >= 0) { *E = R[pick]; gbap_copy(G, &GR[pick]); }
    for (int k = 0; k < 2; k++) { gbap_free(&Gh[k]); gbap_free(&Gb[k]); gbap_free(&GR[k]); }
    gbap_free(&Gt); tl_memo_free(&memo);
    return pick >= 0;
}
/* FUN_180016cb0 (l.17914-18348) */
static int choose_cands(Pock *C, double l1, double l2, const Pos *pos, double *d, EnCut *Eo, Gbap *Go) {
    EnCut A, B, S; Gbap GA, GB, GS; int ret = 0, backup = 0;
    memset(&A, 0, sizeof A); memset(&B, 0, sizeof B); memset(&S, 0, sizeof S);
    memset(&GA, 0, sizeof GA); memset(&GB, 0, sizeof GB); memset(&GS, 0, sizeof GS);
    if (!cand_A(C, l1, pos, &A, &GA, d)) goto out;
    ret = 1;
    if (within_tol(C, l1, l2, &A) && (A.m1.flags & 0x100)) goto acceptA;
    trial_line(C, 0.0, *d, pos, &B, &GB);
    if (GA.valid) {
        if (inside_lim(l1, l2, &A) && !(B.m1.flags & 0x18) && (B.m1.flags & 0x100)) {
            S = A; gbap_copy(&GS, &GA); backup = 1;
            A = B; gbap_copy(&GA, &GB);
        }
        if (GA.valid && inside_lim(l1, l2, &A)) goto acceptA;
    }
    if (((A.m1.flags & 2) || (B.m1.flags & 2)) && cand_half(C, l1, l2, *d, pos, &A, Eo, Go)) goto out;
    if (!cand_bracket(C, l1, l2, *d, pos, &A, Eo, Go)) {
        if (backup) { if (inside_lim(l1, l2, &A)) goto acceptA; *Eo = S; gbap_copy(Go, &GS); goto out; }
        goto acceptA;
    }
    if (above_half_deg(C, &A) && inside_lim(l1, l2, &A) && fabs(A.head) < fabs(Eo->head) && (A.m1.flags & 0x100))
        if (wider_than_last(C, &A) || (A.m1.flags & 6)) goto acceptA;
    goto out;
acceptA:
    *Eo = A; gbap_copy(Go, &GA);
out:
    gbap_free(&GA); gbap_free(&GB); gbap_free(&GS);
    return ret;
}
/* FUN_1800167d0 (l.17744-17893) */
static int choose_refined(Pock *C, double l1, double l2, const Pos *pos, EnCut *E, Gbap *G) {
    double d = 0;
    if (!choose_cands(C, l1, l2, pos, &d, E, G)) return 0;
    double a;
    if (C->P.bull_avail && !(E->m1.flags & 0x18) && circ_heading(C, E, &a)) {
        EnCut E2; Gbap G2; memset(&E2, 0, sizeof E2); memset(&G2, 0, sizeof G2);
        trial_line(C, a, d, pos, &E2, &G2);
        E2.m1.flags |= 0x20;
        if ((E2.m1.w <= l1 || E2.m1.w <= E->m1.w) && (E2.m2.w <= l2 || E2.m2.w <= E->m2.w)) { *E = E2; gbap_copy(G, &G2); }
        gbap_free(&G2);
    }
    return 1;
}
/* ---------------------------------------------------------------------------- move toward a material piece (§6.2.1) */
/* FUN_18006c4e0 (l.61421-61437): no part of the material piece lies outside the swept tube of radius rad */
static int covers(Pock *C, const Sh *mat, const Ent *chn, double rad) {
    Sh sw; memset(&sw, 0, sizeof sw);
    int ok = swept_region(C->P.flags, rad, chn, &sw);
    if (ok) { sw.fl = 0; ShL l = { 0 }; Sh *c = sh_copy(&sw); sl_push(&l, c); ok = (sh_vs_list(mat, &l, 1e-5) & 4) == 0; sl_free(&l); }
    free(sw.e); return ok;
}
/* FUN_18006b170 (l.60838-60982): one arc that swallows a small material piece */
static int swallow_piece(Pock *C, const Sh *mat, Ent *chn) {
    const McceParm *P = &C->P;
    memset(chn, 0, sizeof *chn);
    if (!P->bull_avail || !mat->n) return 0;
    /* FUN_18006aba0 (l.60698-60742) target point */
    P2 M = p2((mat->bb[0] + mat->bb[2]) / 2, (mat->bb[1] + mat->bb[3]) / 2), Q = M;
    if (pt_in_sh(M, mat, 1e-5) != 2) { Pos q; sh_nearest(mat, M, &q); Q = q.pt; }
    /* FUN_18006bfa0 (l.61291-61416) tangent arc P -> Q */
    P2 Pp, Tg; if (!last_pt(C, &Pp) || !last_tan(C, &Tg)) return 0;
    double s1, u; P2 O;
    if (!ln_ln(Pp, p2(-Tg.y, Tg.x), pmul(padd(Pp, Q), 0.5), p2(-(Q.y - Pp.y), Q.x - Pp.x), &s1, &u, &O)) *chn = ent_line(Pp, Q);
    else {
        if (fabs(Pp.x - O.x) <= 1e-12 && fabs(Pp.y - O.y) <= 1e-12) return 0;
        if (fabs(Q.x - O.x) <= 1e-12 && fabs(Q.y - O.y) <= 1e-12) return 0;
        double sa = atan2(Pp.y - O.y, Pp.x - O.x), sw = wrap_pi(atan2(Q.y - O.y, Q.x - O.x) - sa);
        if (s1 > 0 && sw < 0) sw += 2 * M_PI;
        if (s1 < 0 && sw > 0) sw -= 2 * M_PI;
        *chn = ent_arc(O, pdist(O, Pp), sa, sw);
    }
    /* FUN_18006ad40 (l.60747-60831) radius fix-up: radius at least rl */
    Ent save = *chn;
    if (chn->id == 'A' && !(chn->r > P->min_cut_rad)) {
        P2 S = e_start(chn), v = psub(chn->c, S); double l = plen(v); int ok = 0;
        if (l >= 1e-12) {
            P2 c2 = padd(S, pmul(v, P->min_cut_rad / l));
            if (!(fabs(Q.x - c2.x) <= 1e-12 && fabs(Q.y - c2.y) <= 1e-12)) {
                double sw2 = wrap_pi(atan2(Q.y - c2.y, Q.x - c2.x) - chn->sa);
                if (chn->sw > 0 && sw2 < 0) sw2 += 2 * M_PI; else if (chn->sw <= 0 && sw2 > 0) sw2 -= 2 * M_PI;
                chn->c = c2; chn->r = P->min_cut_rad; chn->sw = sw2; ok = 1;
            }
        }
        if (!ok) *chn = save;
    }
    double rad = (P->R - P->r) - (P->R - P->r) * 0.30000000000000004 * 0.1;
    if (!covers(C, mat, chn, rad)) return 0;
    /* FUN_18006c690 (l.61498-61673): shortest covering fraction by bisection */
    Ent full = *chn; double lo = 0, hi = 1, len = e_len(&full), res = 1;
    for (int i = 0; ; i++) {
        double mid = (hi + lo) * 0.5; Ent t = e_cut(&full, mid);
        res = hi;
        if (covers(C, mat, &t, rad)) res = mid; else lo = mid;
        if ((res - lo) * len < P->step * 0.5 || 10 < i) break;
        hi = res;
    }
    *chn = e_cut(&full, res);
    return 1;
}
/* FUN_18006c580 (l.61444-61493): cut chn at its first crossing with arc (smaller chn-side parameter) */
static int cut_at_arc(const Ent *arc, Ent *chn) {
    double ta[2], tb[2]; P2 x[2]; int k = e_isect(arc, chn, 1e-7, ta, tb, x);
    if (!k) return 0;
    double t = tb[0]; if (k > 1 && tb[1] < t) t = tb[1];
    *chn = e_cut(chn, t); return 1;
}
/* FUN_18006b570 (l.60989-61202): aim at the material bbox */
static void aim_bbox(Pock *C, const Sh *mat, Ent *chn) {
    const McceParm *P = &C->P; double rr = P->R - P->r;
    memset(chn, 0, sizeof *chn);
    P2 Pp; if (!last_pt(C, &Pp) || !mat->n) return;
    double x0 = mat->bb[0] - 2e-4, y0 = mat->bb[1] - 2e-4, x1 = mat->bb[2] + 2e-4, y1 = mat->bb[3] + 2e-4;
    P2 cs[4] = { { x0, y0 }, { x0, y1 }, { x1, y1 }, { x1, y0 } };
    /* qsort_s comparator LAB_180001b9a -> FUN_18006c470 (disasm 0x18006c470-0x18006c4c1): ascending squared distance to
       the context point P */
    for (int i = 1; i < 4; i++) for (int j = i; j > 0 && pdist2(cs[j], Pp) < pdist2(cs[j - 1], Pp); j--) { P2 t = cs[j]; cs[j] = cs[j - 1]; cs[j - 1] = t; }
    P2 c2 = cs[2], c3 = cs[3];
    /* CalcCirCirIntersect(c3, R - r, c2, R - r) */
    Ent a = ent_arc(c3, rr, 0, 2 * M_PI), b = ent_arc(c2, rr, 0, 2 * M_PI); double ta[2], tb[2]; P2 I[2];
    int k = e_isect(&a, &b, 1e-9, ta, tb, I);
    if (k == 0) {
        double d = pdist(c3, Pp); P2 X = d >= rr ? padd(Pp, pmul(psub(c3, Pp), (d - rr) / d)) : Pp;
        *chn = ent_line(Pp, X);
    } else if (k == 1) *chn = ent_line(Pp, I[0]);
    else {
        double sa = atan2(I[0].y - c3.y, I[0].x - c3.x), sw = wrap_pi(atan2(I[1].y - c3.y, I[1].x - c3.x) - sa);
        double alt = sw >= 0 ? sw - 2 * M_PI : sw + 2 * M_PI;
        Ent e1 = ent_arc(c3, rr, sa, sw), e2 = ent_arc(c3, rr, sa, alt);
        Ent arc = pdist2(e_at(&e2, 0.5), c2) < pdist2(e_at(&e1, 0.5), c2) ? e2 : e1;
        /* FUN_18006bcb0 (l.61209-61284) */
        P2 Tg; int ok = 0, okT = last_tan(C, &Tg); P2 uu = unit(Tg, &okT);
        if (okT) {
            double th = atan2(uu.y, uu.x), m = P->min_rad;
            Ent cand[3] = { ent_line(Pp, padd(Pp, pmul(uu, M_PI * m))),
                            ent_arc(padd(Pp, pmul(p2(cos(th - M_PI / 2), sin(th - M_PI / 2)), m)), m, th + M_PI / 2, -M_PI),
                            ent_arc(padd(Pp, pmul(p2(cos(th + M_PI / 2), sin(th + M_PI / 2)), m)), m, th - M_PI / 2, M_PI) };
            for (int i = 0; i < 3 && !ok; i++) if (cut_at_arc(&arc, &cand[i])) { *chn = cand[i]; ok = 1; }
        }
        if (!ok) { double t; P2 X; e_near_far(&arc, Pp, 0, &t, &X); *chn = ent_line(Pp, X); }
    }
}
/* FUN_180017d30 (l.18474-18563) */
static void toward_material(Pock *C, const Pos *pos, EnCut *E, Gbap *G) {
    encut_clear(E);
    memset(&E->e, 0, sizeof E->e);
    if (pos->sh >= 0 && pos->sh < C->mat.n) {
        const Sh *m = C->mat.s[pos->sh];
        if (!swallow_piece(C, m, &E->e)) aim_bbox(C, m, &E->e);
    }
    P2 t0 = E->e.id ? e_tan(&E->e, 0) : p2(0, 0);
    double a = (fabs(t0.x) > 1e-12 || fabs(t0.y) > 1e-12) ? atan2(t0.y, t0.x) : 0, b = a;
    P2 lt; if (last_tan(C, &lt) && !(fabs(lt.x) <= 1e-12 && fabs(lt.y) <= 1e-12)) b = atan2(lt.y, lt.x);
    E->head = wrap_pi(a - b);
    gbap_clear(G);
    if (E->e.id) attach(C, &E->e, G);
    measure(C, NULL, 0, E);
}
/* FUN_180029c30 (l.27124-27263): dst = one shape holding a copy of every src entity whose bbox meets [P - rho - 1e-7,
   P + rho + 1e-7] and whose nearest point (GetNearestEntPt) is closer than rho to P; +0x0c / +0x80 = entity / shape index.
   An empty src leaves the window list empty (returns 0 shapes), otherwise it has its one (possibly empty) shape. */
static int win_clip(P2 P, double rho, const ShL *src, Sh *dst) {
    dst->n = 0;
    if (!src->n) return 0;
    double x0 = P.x - rho - 1e-7, y0 = P.y - rho - 1e-7, x1 = P.x + rho + 1e-7, y1 = P.y + rho + 1e-7;
    for (int s = 0; s < src->n; s++) {
        const Sh *m = src->s[s];
        for (int k = 0; k < m->n; k++) {
            double b[4]; e_qbox(&m->e[k], b);   /* ours: quick reject on the containing box first */
            if (!(x0 <= b[2] && b[0] <= x1 && y0 <= b[3] && b[1] <= y1)) continue;
            if (m->e[k].id == 'A') { e_bbox(&m->e[k], b); if (!(x0 <= b[2] && b[0] <= x1 && y0 <= b[3] && b[1] <= y1)) continue; }
            double t; P2 q; e_near_far(&m->e[k], P, 0, &t, &q);
            if (!(pdist2(P, q) < rho * rho)) continue;
            Ent c = m->e[k]; c.ref_ent = k; c.ref_sh = s; sh_push(dst, c);
        }
    }
    sh_upd_bbox(dst);
    return 1;
}
/* FUN_18002a140 (l.27270-27302): both windows emptied, then, when there is a last output point P (FUN_18008e4b0), rebuilt
   around it with rho = 10 x march step + R (C+0x1e8 x 10 + C+0x30). Exact model only (the Clipper bridge measures the full
   material). */
static void windows_build(Pock *C) {
    C->win1_n = C->win2_n = 0; C->win1.n = C->win2.n = 0;
    if (!exact_on()) return;
    P2 P; if (!last_pt(C, &P)) return;
    double rho = C->P.step * 10.0 + C->P.R;
    C->win1_n = win_clip(P, rho, &C->mat, &C->win1);
    C->win2_n = win_clip(P, rho, &C->bull, &C->win2);
}
/* FUN_18006db90 (l.62148-62200): one state-0/1 trial step */
static void trial_step(Pock *C, int *ramp, EnCut *E, Gbap *G, int state) {
    double l1, l2;
    windows_build(C);                                                  /* FUN_18002a140 (l.62163) */
    limits(C, ramp, &l1, &l2);
    /* FUN_180016c00 (l.17896-17911) */
    if (!(state != 1 && choose_refined(C, l1, l2, &C->mpos, E, G))) toward_material(C, &C->mpos, E, G);
    int dir = G->valid ? sh_side(C->cur.s[G->sh]) : 0;
    if (!(E->m1.flags & 8)) measure(C, NULL, dir, E);
    if (!(E->m1.flags & 0x18)) commit(C, E);
    uint32_t f = E->m1.flags;
    if (f & 0x80) cetp_emit(&C->T, &C->P, &E->e, firstpass(C, E) | 0x40, f, E->m1.w, E->m2.w, E->m1.sweep, E->m2.sweep);
    C->win1_n = C->win2_n = 0; C->win1.n = C->win2.n = 0;              /* both windows emptied (l.62186-62198) */
}
/* FUN_18006d170 (l.61874-61909) */
static void state01(Pock *C, int *ramp, EnCut *E, Gbap *G, int *state) {
    for (int guard = 0; guard < 100000; guard++) {
        if (mcce_abort) return;                          /* ours: Cancel (region_machine stops next) */
        encut_clear(E); gbap_clear(G);
        trial_step(C, ramp, E, G, *state);
        if (G->valid) { if (getenv("MCCE_DEBUG") && atoi(getenv("MCCE_DEBUG")) >= 3) { P2 lp = p2(0, 0); last_pt(C, &lp); fprintf(stderr, "state01 -> 2 at rec %d: last (%.3f,%.3f) step %c len %.3f flags %x G (%.3f,%.3f) lead %d\n", C->T.n, lp.x, lp.y, E->e.id ? E->e.id : '?', e_len(&E->e), E->m1.flags, G->pt.x, G->pt.y, G->lead.n); } *state = 2; return; }
        if ((E->m1.flags & 0x10) || C->mpos.sh == -1) break;
        if (*state == 1) *state = 0;
    }
    *state = 3;
}

/* ============================================================================================ Z profiles (§3 D 2.4) */
static void set_ce(Sh *s, uint8_t ce) { for (int i = 0; i < s->n; i++) s->e[i].ce = ce; }   /* FUN_18005c8e0 (l.53364-53391) */
/* FUN_18003d490 (l.36948-37016) with FUN_18003da50 / FUN_18003d670 / FUN_18003dd90 (breakpoints) and FUN_18003e2f0
   (l.37586-37898, split + Z + CeFlags). type 0 lift, 1 descent, 2 lift + descent. */
static void zprofile(Pock *C, int type, double h_in, double p10, Sh *s) {
    if (!s->n) return;
    int wz = h_in > 1e-7; double h = wz ? h_in : 1.0, Lr = C->P.pi_minrad, L = sh_len(s);
    double bs[8], bz[8]; int nb = 0;
#define CL(x) fmin(L, fmax(0, (x)))
    if (type == 0) { bs[nb] = 0; bz[nb++] = 0; if (Lr < L - 1e-4) { bs[nb] = Lr; bz[nb++] = h; } bs[nb] = L; bz[nb++] = h; }
    else if (type == 1) { double a = CL(L - p10), b = CL(L - p10 - Lr);
        bs[nb] = 0; bz[nb++] = h; if (b > 1e-4) { bs[nb] = b; bz[nb++] = h; } if (a < L - 1e-4) { bs[nb] = a; bz[nb++] = 0; } bs[nb] = L; bz[nb++] = 0; }
    else { double u = CL(Lr), a = CL(L - p10), b = CL(L - p10 - Lr);
        if (b < u) u = b = L / 2;
        if (a < b) a = b;
        bs[nb] = 0; bz[nb++] = 0; bs[nb] = u; bz[nb++] = h; if (fabs(u - b) > 1e-4) { bs[nb] = b; bz[nb++] = h; }
        if (a < L - 1e-4) { bs[nb] = a; bz[nb++] = 0; } bs[nb] = L; bz[nb++] = 0; }
#undef CL
    int ent8 = (s->e[0].ce & 8) != 0;
    /* split the path at the breakpoints */
    Sh o; memset(&o, 0, sizeof o); o.dir = s->dir; o.fl = s->fl; o.z = s->z;
    double acc = 0;
    for (int k = 0; k < s->n; k++) {
        Ent e = s->e[k]; double l = e_len(&e), t0 = 0;
        for (int j = 0; j < nb; j++) {
            double sj = bs[j];
            if (sj > acc + 1e-9 && sj < acc + l - 1e-9 && l > 0) { double t = (sj - acc) / l; Ent p = e_sub(&e, t0, t); sh_push(&o, p); t0 = t; }
        }
        Ent p = e_sub(&e, t0, 1.0); sh_push(&o, p);
        acc += l;
    }
    /* Z at the end of each entity by linear interpolation along the length */
    acc = 0;
    double zprev = bz[0];
    for (int k = 0; k < o.n; k++) {
        acc += e_len(&o.e[k]);
        double z = bz[nb - 1];
        for (int j = 1; j < nb; j++) if (acc <= bs[j] + 1e-9) { double f = bs[j] > bs[j - 1] ? (acc - bs[j - 1]) / (bs[j] - bs[j - 1]) : 1; z = bz[j - 1] + (bz[j] - bz[j - 1]) * f; break; }
        if (wz) o.e[k].z = z;
        if (fabs(z - h) <= 1e-7 && fabs(zprev - h) <= 1e-7) o.e[k].ce |= 2;
        o.e[k].ce |= ent8 ? 8 : 4;
        zprev = z;
    }
    if (type == 1) {   /* zero-length line at the start with z = first z */
        Ent z0 = ent_line(e_start(&o.e[0]), e_start(&o.e[0])); z0.z = wz ? bz[0] : 0; z0.ce = o.e[0].ce & ~2;
        if (fabs(bz[0] - h) <= 1e-7) z0.ce |= 0;
        sh_push(&o, z0); memmove(o.e + 1, o.e, (o.n - 1) * sizeof(Ent)); o.e[0] = z0;
    }
    free(s->e); s->e = o.e; s->n = o.n; s->cap = o.cap; sh_upd_bbox(s);
}

/* ============================================================================================ region sets (§6.4.2) */
static void region_sets(Pock *C, ShL *S1, ShL *S2, ShL *S3, ShL *S4) {
    const McceParm *P = &C->P;
    double d2 = P->d1c8, d1 = P->d1c0;
    if (P->var_rad_avail) { d1 = d2 - 1e-4; if (d1 < 0) d1 = 0.5 * d2; }
    memset(S1, 0, sizeof *S1); memset(S2, 0, sizeof *S2); memset(S3, 0, sizeof *S3); memset(S4, 0, sizeof *S4);
    g_exsets = exsets_on();
    int dll = dlloff_on();
    if (!C->mat.n) *S2 = sl_copy(&C->cur);
    else {
        ShL tmp = sl_copy(&C->mat);
        /* l.56625-56628: UpdDirShAr(0.001), DetNestLvlShAr, MCCECalcOffset(1, C+0x30 - C+0x38, .., 0x100, 0, 1e-6, 1e-6) - the DLL's
           own offset (mcce_offset.inc); where it refuses, the old construction */
        if (!dll || !sl_offset_dll(&tmp, P->R - P->r, C->z, 1, S1))
        *S1 = sl_offset_k(&tmp, P->R - P->r, C->z);   /* out1 keeps the material's orientation (FUN_18002c0e0 kind 4) */
        if ((g_exsets && exact_on()) || (dll && dlls2_on())) {   /* FUN_180062a70 l.56630: FUN_180153b30(1e-6, C+0x340, out1) - the DLL's boolean (mcce_sobool.inc) */
            if (!sl_bool_dll(MCCE_RB_MINUS, 1e-06, &C->cur, S1, C->z, S2)) *S2 = sl_minus_m(&C->cur, S1, C->z);   /* ours where the port refuses */
        } else *S2 = sl_minus(&C->cur, S1, C->z);
        sl_free(&tmp);
    }
    orient_k2(P->flags, S2);
    int i = 0;
    while (i < S2->n) {
        int e = sl_group_end(S2, i); if (e < 0) e = S2->n;
        ShL G = { S2->s + i, e - i, e - i };
        double f = 1.0;
        for (int k = 0; k < 3; k++) {
            /* l.56710-56713: MCCECalcOffset(1, -f d1 / -f d2, G, .., 0x100, 0, 1e-6, 1e-6) - the DLL's own offset where it does not refuse */
            ShL A, B;
            if (!dll || !sl_offset_dll(&G, -f * d1, C->z, 0, &A)) A = sl_offset_k(&G, -f * d1, C->z);
            if (!dll || !sl_offset_dll(&G, -f * d2, C->z, 0, &B)) B = sl_offset_k(&G, -f * d2, C->z);
            if (sl_nonisland(&A) == 1 && sl_nonisland(&B) == 1) { sl_move_append(S3, &A); sl_move_append(S4, &B); sl_free(&A); sl_free(&B); break; }
            sl_free(&A); sl_free(&B);
            f *= 0.5;
        }
        i = e;
    }
    g_exsets = 0;
}

/* ============================================================================================ approach / exit geometry */
/* FUN_1800636d0 (l.56898-56919) */
static double f_theta(const McceParm *P, double rho) {
    if (rho < 1e-12) return -1.0;
    double x = (rho - P->d1c8 * 0.25) / rho; if (x < -1) x = -1; else if (x > 1) x = 1;
    double a = acos(x); return a > 0.7853981633974483 ? 0.7853981633974483 : a;
}
/* FUN_180064980 (l.57612-57930) + FUN_18005c440 (l.53211-53363): candidate (rho, theta) table and Lmax = 8 P+0x1c8 */
static int cand_table(const McceParm *P, double *rho, double *th, double *Lmax) {
    int n = 0; *Lmax = P->d1c8 * 8.0;
#define C6(r) do { double r_ = (r); rho[n] = r_; th[n++] = f_theta(P, r_); rho[n] = r_; th[n++] = 0.7853981633974483; rho[n] = r_; th[n++] = 0.9817477042468103; \
                   rho[n] = r_; th[n++] = 1.1780972450961724; rho[n] = r_; th[n++] = 1.3744467859455345; rho[n] = r_; th[n++] = 1.5707963267948966; } while (0)
    double m = P->min_rad, v = P->var_rad;
    if (P->var_rad_avail && 1e-5 < v - m) { C6(v); C6((v - m) * 0.75 + m); C6((v - m) * 0.5 + m); C6((v - m) * 0.25 + m); }
    C6(m);
#undef C6
    double r5[5] = { m * 0.75, m * 0.5, m * 0.25, m * 0.1, m * 0.1 };
    for (int k = 0; k < 5; k++) { rho[n] = r5[k]; th[n++] = k == 4 ? 1.5707963267948966 : f_theta(P, r5[k]); }
    rho[n] = -1; th[n++] = -1;
    return n;
}
/* FUN_180064690 (l.57517-57608): the radius column, consecutive duplicates (< 1e-5) and negatives removed */
static int radius_list(const McceParm *P, double *out) {
    double rho[64], th[64], Lm; int n = cand_table(P, rho, th, &Lm), m = 0;
    for (int i = 0; i < n; i++) { if (m && fabs(rho[i] - out[m - 1]) < 1e-5) continue; out[m++] = rho[i]; }
    int k = 0; for (int i = 0; i < m; i++) if (out[i] >= 0) out[k++] = out[i];
    return k;
}
/* link target CTarg (0x108, §6.3.0 / §6.10.0) */
typedef struct {
    P2 pt, dir;              /* +0x00 / +0x10 */
    uint8_t b20, b21, b22;   /* +0x20 / +0x21 / +0x22 */
    double d28;              /* +0x28 run-in length */
    Pos A, B;                /* +0x30 / +0x50 */
    Sh path;                 /* +0x70 */
    int s1;                  /* S1 shape (FUN_1800522c0 'used' bookkeeping) */
} Targ;
typedef struct { Targ *t; int n, cap; } TargL;
/* ours (oracle trace, MCCE_TRACE + MCCE_TRACE_WIN=1): a target list, "<tag> cur x y | n N" + one " wt" line per target, as
   tools/peloader/mcce.c tr_targets (window list FUN_180066ad0 "WT", general list FUN_180052a90 "GT") */
static int mtr_win_on(void) { static _Thread_local int on = -1; if (on < 0) { const char *e = getenv("MCCE_TRACE_WIN"); on = e && *e == '1'; } return on; }
static void mcce_trace_targets(const char *tag, P2 cur, const TargL *T) {
    mtr_init();
    if (!g_trace_f || !mtr_win_on()) return;
    FILE *f = g_trace_f;
    fprintf(f, "%s cur %.6f %.6f | n %d\n", tag, cur.x, cur.y, T->n);
    for (int i = 0; i < T->n; i++) { const Targ *t = &T->t[i];
        fprintf(f, " wt %d pt %.6f %.6f dir %.6f %.6f b %d %d %d d28 %.6f A %.6f %.6f B %.6f %.6f\n", i, t->pt.x, t->pt.y, t->dir.x, t->dir.y,
                t->b20, t->b21, t->b22, t->d28, t->A.pt.x, t->A.pt.y, t->B.pt.x, t->B.pt.y); }
}
static Targ *tl_push(TargL *l) { if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 16; l->t = realloc(l->t, l->cap * sizeof(Targ)); } Targ *t = &l->t[l->n++]; memset(t, 0, sizeof *t); t->s1 = -1; return t; }
static void tl_erase(TargL *l, int i) { free(l->t[i].path.e); memmove(l->t + i, l->t + i + 1, (l->n - i - 1) * sizeof(Targ)); l->n--; }
static void tl_free(TargL *l) { for (int i = 0; i < l->n; i++) free(l->t[i].path.e); free(l->t); memset(l, 0, sizeof *l); }
static void tl_keep_only(TargL *l, int k) { for (int i = l->n - 1; i >= 0; i--) if (i != k) tl_erase(l, i); }

/* FUN_18005c980 (l.53399-53769): one approach candidate into target T */
static int approach_cand(uint32_t flags, const ShL *CLIP, const Targ *T, double rho, double th, double L, Sh *out) {
    sh_clear(out); out->dir = 0;
    if (!CLIP->n) return 0;
    if (T->b20 || rho <= 0 || th <= 0) { Pos q; if (!sl_nearest(CLIP, T->pt, &q)) return 0; sh_push(out, ent_line(q.pt, T->pt)); sh_upd_bbox(out); return 1; }
    P2 dd = (flags & 1) ? p2(T->dir.x, -T->dir.y) : p2(-T->dir.x, T->dir.y); int ok; P2 u = unit(dd, &ok); if (!ok) return 0;
    P2 c = padd(T->pt, pmul(p2(u.y, u.x), rho));
    double th0 = th; Sh pth; memset(&pth, 0, sizeof pth); IsabL I = { 0 };
    for (int k = 0; k < 3; k++) {
        double a0 = atan2(-u.x, -u.y), sa = a0 + ((flags & 1) ? -th : th);
        while (sa > M_PI) sa -= 2 * M_PI;
        while (sa <= -M_PI) sa += 2 * M_PI;
        Ent arc = ent_arc(c, rho, sa, (flags & 1) ? th : -th);
        P2 S = e_start(&arc); P2 v = unit(psub(S, c), &ok); if (!ok) v = p2(0, 0);
        P2 v2 = (flags & 1) ? p2(v.x, -v.y) : p2(-v.x, v.y);
        sh_clear(&pth); sh_push(&pth, ent_line(psub(S, pmul(p2(v2.y, v2.x), L)), S)); sh_push(&pth, arc); pth.dir = 0; sh_upd_bbox(&pth);
        isect1(&pth, CLIP, &I);
        if (I.n || th0 >= 1.9198621771937625) break;
        th = th + (1.9198621771937625 - th0) * 0.5;               /* l.53694 */
    }
    int r = 0;
    if (dbg_level() >= 5) {
        fprintf(stderr, "  pth line (%.4f,%.4f)-(%.4f,%.4f) arc c (%.4f,%.4f) r %.4f sa %.4f sw %.4f; %d crossings:", pth.e[0].p1.x, pth.e[0].p1.y, pth.e[0].p2.x, pth.e[0].p2.y, pth.e[1].c.x, pth.e[1].c.y, pth.e[1].r, pth.e[1].sa, pth.e[1].sw, I.n);
        for (int k = 0; k < I.n; k++) fprintf(stderr, " [e%d t%.4f (%.4f,%.4f) Y%d/%d]", I.r[k].xe, I.r[k].xt, I.r[k].pt.x, I.r[k].pt.y, I.r[k].ys, I.r[k].ye);
        fprintf(stderr, "\n"); }
    if (I.n) { sh_section(&pth, I.r[I.n - 1].xe, I.r[I.n - 1].xt, 1, 1.0, out); sh_upd_bbox(out); r = out->n > 0; }
    il_free(&I); free(pth.e);
    return r;
}
/* FUN_180060770 (l.55373-55560) */
static int approach(Pock *C, int allowFallback, const Targ *T, const ShL *CONT, const ShL *CLIP, Sh *out) {
    double rho[64], th[64], Lmax; int n = cand_table(&C->P, rho, th, &Lmax);
    double bb[4]; sl_bbox(CONT, bb); double L = hypot(bb[2] - bb[0], bb[3] - bb[1]);
    sh_clear(out);
    Sh p; memset(&p, 0, sizeof p);
    for (int i = 0; i < n; i++) {
        int ok = approach_cand(C->P.flags, CLIP, T, rho[i], th[i], L, &p);
        if (dbg_level() >= 4)
            fprintf(stderr, "approach T (%.4f,%.4f) dir (%.4f,%.4f) cand %d rho %.4f th %.4f: ok %d vs %d len %.4f Lmax %.4f\n", T->pt.x, T->pt.y, T->dir.x, T->dir.y, i, rho[i], th[i], ok, ok ? sh_vs_list(&p, CONT, 1e-5) : -1, ok ? sh_len(&p) : 0, Lmax);
        if (ok && (sh_vs_list(&p, CONT, 1e-5) & 4) == 0 && sh_len(&p) <= Lmax) { sh_append(out, &p); break; }
    }
    free(p.e);
    /* allowFallback: FUN_180060cd0 (l.55563-56067) NOT DECODED: no fallback path */
    (void)allowFallback;
    return out->n > 0;
}
/* FUN_18005dd40 (l.53991-54253): exit candidate: arc of radius rho turning by th, then a tangent line of length L,
   cut at the first crossing with CLIP; (rho, th) <= 0: a line to the nearest CLIP point */
static int exit_cand(int dir, const ShL *CLIP, P2 P, P2 T, double rho, double th, double L, Sh *out) {
    sh_clear(out); out->dir = 0;
    if (!CLIP->n) return 0;
    if (rho <= 0 || th <= 0) { Pos q; sl_nearest(CLIP, P, &q); sh_push(out, ent_line(P, q.pt)); sh_upd_bbox(out); return 1; }
    P2 tt = dir ? p2(T.x, -T.y) : p2(-T.x, T.y); double l = plen(tt); if (l < 1e-12) return 0;
    P2 c = p2(P.x + rho * tt.y / l, P.y + rho * tt.x / l);
    if (fabs(tt.y / l) <= 1e-12 && fabs(tt.x / l) <= 1e-12) return 0;
    double sa = atan2(-tt.x / l, -tt.y / l);
    Ent arc = ent_arc(c, rho, sa, dir ? th : -th);
    P2 E = e_end(&arc), r = psub(E, c); double rl = plen(r); if (rl >= 1e-12) r = pmul(r, 1 / rl);
    P2 r2 = dir ? p2(r.x, -r.y) : p2(-r.x, r.y);
    Sh pth; memset(&pth, 0, sizeof pth); sh_push(&pth, arc); sh_push(&pth, ent_line(E, p2(E.x + L * r2.y, E.y + L * r2.x))); pth.dir = 0; sh_upd_bbox(&pth);
    IsabL I = { 0 }; isect1(&pth, CLIP, &I); int ok = 0;
    if (I.n) { sh_section(&pth, 0, 0.0, I.r[0].xe, I.r[0].xt, out); ok = 1; }
    il_free(&I); free(pth.e);
    return ok;
}
/* FUN_18005eca0 (l.54451-54741): exit move from the last point; exitFlag = 1 iff the point is inside CLIP and P+0x21f */
static int exit_move(Pock *C, const ShL *CONT, const ShL *CLIP, Sh *out, int *exitFlag) {
    const McceParm *P = &C->P;
    sh_clear(out); out->dir = 0; *exitFlag = 0;
    double rho[64], th[64], Lmax; int n = cand_table(P, rho, th, &Lmax);
    double bb[4]; sl_bbox(CONT, bb); double L = hypot(bb[0] - bb[2], bb[1] - bb[3]);
    P2 Pp = p2(0, 0), T = p2(0, 0); last_pt(C, &Pp); last_tan(C, &T);
    if (sl_inside_grouped(CLIP, 1e-5, Pp) == 2) {
        double l = plen(T); if (l < 1e-12) return 0;
        T = pmul(T, 1 / l);
        if (P->bull_avail) *exitFlag = 1;
        double d = P->var_rad_avail ? P->var_rad : P->min_rad;
        sh_push(out, ent_line(Pp, padd(Pp, pmul(T, d)))); sh_upd_bbox(out);
        IsabL I = { 0 }; isect1(out, CLIP, &I);
        if (I.n) { out->e[0].p2 = I.r[0].pt; sh_upd_bbox(out); }
        il_free(&I);
        return out->n > 0;
    }
    Sh p; memset(&p, 0, sizeof p);
    for (int pass = 0; pass < 2 && !out->n; pass++) {
        int b = P->flags & 0xff; if (P->flags & 4) b = ~b;
        int dir = b & 1; if (pass) dir = !dir;
        for (int i = 0; i < n; i++)
            if (exit_cand(dir, CLIP, Pp, T, rho[i], th[i], L, &p) && (sh_vs_list(&p, CONT, 1e-5) & 4) == 0 && (i != n - 1 || pass != 0) && sh_len(&p) <= Lmax) {
                sh_append(out, &p); break; }
    }
    free(p.e);
    return out->n > 0;
}
/* FUN_180077360 (l.67390-67600): extend the approach backwards along its start tangent to the set boundary */
static void extend_back(const ShL *set, Sh *sh) {
    if (!sh->n) return;
    double bb[4]; sl_bbox(set, bb); double D = hypot(bb[2] - bb[0], bb[3] - bb[1]);
    P2 S = e_start(&sh->e[0]);
    P2 T = e_tan(&sh->e[0], 0); double l = plen(T); if (l < 1e-12) return;
    T = pmul(T, D / l);
    Sh Ls; memset(&Ls, 0, sizeof Ls); sh_push(&Ls, ent_line(psub(S, T), S)); Ls.dir = 0; sh_upd_bbox(&Ls);
    IsabL I = { 0 }; isect1(&Ls, set, &I);
    for (int k = I.n - 1; k >= 0; k--) if (pdist2(S, I.r[k].pt) > 1e-12) { Ls.e[0].p1 = I.r[k].pt; break; }
    P2 mid = pmul(padd(Ls.e[0].p1, Ls.e[0].p2), 0.5);
    if (sl_inside_grouped(set, 1e-5, mid) == 2) { sh_push(sh, Ls.e[0]); memmove(sh->e + 1, sh->e, (sh->n - 1) * sizeof(Ent)); sh->e[0] = Ls.e[0]; sh_upd_bbox(sh); }
    il_free(&I); free(Ls.e);
}
/* FUN_180077b30 (l.67700-67760): keep only the last d of the path */
static void keep_last(double d, Sh *sh) {
    double L = sh_len(sh);
    if (L - d < 1e-5) return;
    ShL l = { &sh, 1, 1 }; Pos p0 = { 0, 0, 0, e_start(&sh->e[0]) }, p;
    if (!walk_fwd(0, L - d, &l, &p0, &p, NULL)) return;
    Sh o; memset(&o, 0, sizeof o); o.dir = sh->dir;
    if (!sh_section(sh, p.ent, p.t, sh->n - 1, 1.0, &o)) sh_push(&o, ent_line(p.pt, p.pt));   /* FUN_180147a60 */
    free(sh->e); sh->e = o.e; sh->n = o.n; sh->cap = o.cap; sh_upd_bbox(sh);
}

/* ============================================================================================ routes (§6.3.0, §6.12.6) */
/* FUN_18010a6b0 (l.143923-143995): on-line (byte +0x13 bit 4) and off-line lengths */
static void on_off_len(const Sh *s, double *on, double *off) {
    double a = 0, b = 0; for (int i = 0; i < s->n; i++) { if (s->e[i].b13 & 4) a += e_len(&s->e[i]); else b += e_len(&s->e[i]); }
    if (on) *on = a;
    if (off) *off = b;
}
static void clear_b13(Sh *s) { for (int i = 0; i < s->n; i++) s->e[i].b13 &= ~4; }   /* FUN_18010a8c0 */
static Isab isab_end(P2 pt, int xe, double xt, int cls) { Isab r; memset(&r, 0, sizeof r); r.pt = pt; r.xe = xe; r.xt = xt; r.ys = -1; r.ye = -1; r.next = r.prev = -1; r.cls = cls; return r; }
static void il_insert(IsabL *l, int at, Isab a) { il_push(l, a); memmove(&l->r[at + 1], &l->r[at], (l->n - 1 - at) * sizeof(Isab)); l->r[at] = a; }
/* FUN_18010bd00 (l.144631-144754): re-link the records that lie on the set (end nodes off the set excluded) */
static void relink_mid(IsabL *I) {
    if (I->n < 2) return;
    int f = I->r[0].ys == -1, l = I->r[I->n - 1].ys == -1;
    IsabL m = { I->r + f, I->n - f - l, 0 };
    if (m.n >= 2) { isab_link(&m); if (f) for (int k = 0; k < m.n; k++) { if (m.r[k].next >= 0) m.r[k].next += 1; if (m.r[k].prev >= 0) m.r[k].prev += 1; } }
}
/* FUN_180146c10 (l.175639-175720): appends one 'L' pointAt(e1, t1) -> pointAt(e2, t2) (FUN_180021580, t as given), its
   SetChnMinMax box, and grows out's box */
static void sec_chord(const Sh *s, int e1, double t1, int e2, double t2, Sh *out) {
    Ent l; memset(&l, 0, sizeof l); l.id = 'L';
    l.p1 = e_at(&s->e[e1], t1); l.p2 = e_at(&s->e[e2], t2);
    sh_push(out, l); sh_upd_bbox(out);
}
/* FUN_18010af10 (l.144189-144437): edge from node i: mode 0 along the line to i+1 (entities marked on-line), mode 1 along
   the set forward to next, mode 2 along the set backward to prev; the target must lie after i. When the section copy
   appends nothing (FUN_180146e20 / FUN_180147d30 return 0) the edge is the chord FUN_180146c10: modes 0 / 1 from i to j
   (disasm 0x18010b0dc, 0x18010b52c), mode 2 from j to i (0x18010b316-0x18010b33c: edx = j's entity, r9d = i's) */
static int route_edge(const ShL *set, const Sh *L, const IsabL *I, int mode, int i, Sh *edge, int *j) {
    sh_clear(edge); edge->dir = 0;
    if (mode == 0) {
        *j = i + 1; if (*j >= I->n) return 0;
        if (!sh_section(L, I->r[i].xe, I->r[i].xt, I->r[*j].xe, I->r[*j].xt, edge))
            sec_chord(L, I->r[i].xe, I->r[i].xt, I->r[*j].xe, I->r[*j].xt, edge);
        for (int k = 0; k < edge->n; k++) edge->e[k].b13 |= 4;
        return 1;
    }
    const Isab *a = &I->r[i];
    *j = mode == 1 ? a->next : a->prev;
    /* l.144338-144352 / 144389-144403: nodes not beyond i along the line ((double)entity + t) are passed over along the set
       (fail when back at i); then the target must come after i in the list */
    if (a->ys < 0 || *j < 0) return 0;
    while ((double)I->r[*j].xe + I->r[*j].xt <= (double)a->xe + a->xt) {
        if (*j == i) return 0;
        *j = mode == 1 ? I->r[*j].next : I->r[*j].prev;
        if (*j < 0) return 0;
    }
    if (*j <= i || I->r[*j].ys != a->ys) return 0;
    const Sh *s = set->s[a->ys];
    if (mode == 1) { if (!sh_section(s, a->ye, a->yt, I->r[*j].ye, I->r[*j].yt, edge)) sec_chord(s, a->ye, a->yt, I->r[*j].ye, I->r[*j].yt, edge); }
    else if (sh_section(s, I->r[*j].ye, I->r[*j].yt, a->ye, a->yt, edge)) { sh_rev(edge); edge->dir = 0; }   /* FUN_180147d30 */
    else sec_chord(s, I->r[*j].ye, I->r[*j].yt, a->ye, a->yt, edge);
    return 1;
}
static void ct_stage_sl(const char *st, const ShL *L);
/* g_ct_f (defined above sh_vs_list), ours: MCCE_CTDUMP stage file of the current connector call (unit oracle, ct_dump_open) */
static void ct_isab(const char *st, const IsabL *I) {   /* ours (unit oracle): an ISAB list, the format of tools/peloader/mcce_ct.c */
    if (!g_ct_f) return;
    fprintf(g_ct_f, "stage %s %d\n", st, I->n);
    for (int k = 0; k < I->n; k++) { const Isab *r = &I->r[k];
        fprintf(g_ct_f, "i %.17g %.17g %d %.17g %d %d %.17g %d %d %d\n", r->pt.x, r->pt.y, r->xe, r->xt, r->ys, r->ye, r->yt, r->next, r->prev, r->cls); }
}
/* FUN_18010c040 (l.144757-144853): route from the path's start to its end, along the path inside `set` and along set
   boundaries elsewhere; returns 1 shape in out (or none). The path is a CSh: one line (FUN_1800f1580, FUN_18005ff90, ...) or
   an arc - line - arc connector (FUN_180042890 l.40220) */
static void route_sh(int both, const ShL *set, const Sh *path, ShL *out) {
    Sh *L = sh_new(); for (int k = 0; k < path->n; k++) sh_push(L, path->e[k]); L->dir = 0; sh_upd_bbox(L);
    IsabL I = { 0 }; isect1(L, set, &I); ct_isab("isab", &I);
    if (!I.n) {
        if (!(set->n && sec_in_shape(L, 0, 0, L->n - 1, 1, 0, set->s[set->n - 1], 1e-5) != 2)) {
            for (int k = 0; k < L->n; k++) L->e[k].b13 |= 4;
            sl_push(out, L); il_free(&I); return;
        }
    }
    /* FUN_180109960 (l.143552-143692): line ends as nodes */
    P2 S = e_start(&L->e[0]), E = e_end(&L->e[L->n - 1]);
    if (!I.n || pdist2(S, I.r[0].pt) > 1.0000000000000002e-10) {
        il_insert(&I, 0, isab_end(S, 0, 0.0, 0));
        for (int k = 1; k < I.n; k++) { if (I.r[k].next >= 0) I.r[k].next++; if (I.r[k].prev >= 0) I.r[k].prev++; }
    }
    if (pdist2(E, I.r[I.n - 1].pt) > 1e-10 || I.n == 1) il_push(&I, isab_end(E, L->n - 1, 1.0, 0));
    /* FUN_18010ad50 (l.144148-144186): classes */
    for (int k = 0; k + 1 < I.n; k++) I.r[k].cls = sec_in_list(L, I.r[k].xe, I.r[k].xt, I.r[k + 1].xe, I.r[k + 1].xt, 0, set, 1e-5) == 2 ? 1 : 3;
    I.r[I.n - 1].cls = 1;
    /* FUN_180109e20 (l.143696-143920): snap end nodes that lie off the set */
    int added = 0;
    if (I.r[0].cls != 1 && I.r[0].ys == -1) {
        Pos Q; sl_nearest(set, S, &Q);
        if (I.n < 3 || pdist2(I.r[1].pt, Q.pt) >= 1e-10) {
            Isab r = isab_end(Q.pt, 0, 0.0, 3); r.ys = Q.sh; r.ye = Q.ent; r.yt = Q.t; I.r[0] = r;
            for (int k = 1; k < I.n; k++) I.r[k].xe += 1;
            Ent ln = ent_line(Q.pt, S); sh_push(L, ln); memmove(L->e + 1, L->e, (L->n - 1) * sizeof(Ent)); L->e[0] = ln; sh_upd_bbox(L);
            added = 1;
        } else { memmove(&I.r[0], &I.r[1], (I.n - 1) * sizeof(Isab)); I.n--; for (int k = 0; k < I.n; k++) { if (I.r[k].next != -1) I.r[k].next--; if (I.r[k].prev != -1) I.r[k].prev--; } }
    }
    if (I.n >= 2 && I.r[I.n - 2].cls != 1 && I.r[I.n - 1].ys == -1) {
        Pos Q; sl_nearest(set, E, &Q);
        if (I.n < 3 || pdist2(I.r[I.n - 2].pt, Q.pt) >= 1e-10) {
            Isab r = isab_end(Q.pt, L->n, 1.0, 1); r.ys = Q.sh; r.ye = Q.ent; r.yt = Q.t; I.r[I.n - 1] = r;
            sh_push(L, ent_line(E, Q.pt)); sh_upd_bbox(L);
            added = 1;
        } else I.n--;
    }
    if (added) relink_mid(&I);
    ct_isab("nodes", &I);
    /* FUN_18010a960 (l.144026-144145): greedy forward walk with relaxation; FUN_18010b6f0 (l.144440-144628) path */
    int n = I.n;
    double *dist = malloc(n * sizeof(double)); int *pred = malloc(n * sizeof(int)); Sh **edge = calloc(n, sizeof(Sh *));
    ShL keep = { 0 };
    for (int k = 0; k < n; k++) { dist[k] = FLT_MAXD; pred[k] = -1; }
    if (n) dist[0] = 0;
    int i = 0, okB = 0;
#define RELAX(i_, j_, e_) do { double d_ = dist[i_] + sh_len(e_); if (d_ < dist[j_]) { dist[j_] = d_; pred[j_] = i_; edge[j_] = e_; } } while (0)
    while (n > 1 && i != n - 1) {
        if (I.r[i].cls == 1) {
            Sh *e = sh_new(); int j; if (!route_edge(set, L, &I, 0, i, e, &j)) { sh_free(e); break; }
            sl_push(&keep, e); RELAX(i, j, e); i = j;
        } else {
            Sh *eF = sh_new(), *eB = sh_new(); int jF = -1, jB = -1;
            int okF = route_edge(set, L, &I, 1, i, eF, &jF);
            if (okF) { sl_push(&keep, eF); RELAX(i, jF, eF); } else sh_free(eF);
            if (both) { okB = route_edge(set, L, &I, 2, i, eB, &jB); if (okB) { sl_push(&keep, eB); RELAX(i, jB, eB); } else sh_free(eB); } else sh_free(eB);
            int next;
            if (!okF) { if (!okB) break; next = jB; }
            else next = (okB && !(dist[jF] < dist[jB])) ? jB : jF;
            i = next;
        }
    }
#undef RELAX
    if (n && i == n - 1) {
        Sh *chain[4096]; int nc = 0, j = n - 1, ok = 1;
        while (j != 0) { if (pred[j] == -1 || !edge[j] || nc >= 4096) { ok = 0; break; } chain[nc++] = edge[j]; j = pred[j]; }
        if (ok) { Sh *o = sh_new(); o->dir = 0; for (int k = nc - 1; k >= 0; k--) sh_append(o, chain[k]); sh_upd_bbox(o); sl_push(out, o); }
    }
    free(dist); free(pred); free(edge); sl_free(&keep); il_free(&I); sh_free(L);
}
static void route(int both, const ShL *set, const Ent *line, ShL *out) { Sh l; memset(&l, 0, sizeof l); l.e = (Ent *)line; l.n = l.cap = 1; route_sh(both, set, &l, out); }

/* ============================================================================================ guided planner (§6.16) */
static _Thread_local FILE *g_gp_f;   /* ours: MCCE_GPDUMP stage file of the current call (unit oracle, see gp_dump_open) */
static void gp_upd_minmax(Sh *s);   /* UpdMinMaxSh, below */
/* FUN_1800f1080 (l.130609-130762): trapezoidal-decomposition link planner. Ported so far (MCCE_GUIDED=1, debug only - the
   plain route stays in use until the search FUN_1800eda20 and the region preparation are ported): the region orientation
   FUN_1800f33a0, the slicing angle FUN_1800f18a0 + rotation, the in-frame plain route FUN_1800f1580, the horizontal cell
   decomposition FUN_1801009c0 (vertex records, gap items, trapezoids) and the cell channel FUN_1800f61a0.
   Since ported: FUN_1800f2480, FUN_1800f3220, FUN_1800f3de0, FUN_1800f2b40, FUN_1800f2ca0, FUN_1800eda20 (checked stage by
   stage against mcce.dll: tools/peloader --gp). Since ported: FUN_1800e9790 (cuts; its booleans ours) and the
   verdict; the path is returned (MCCE_GUIDED=1). Since ported: the post-process FUN_180136370. */

/* raw pock_chn fields read by the planner without looking at the entity type (§3 D: line e1 +0x18/+0x20, e2 +0x28/+0x30;
   arc centre +0x18/+0x20, radius +0x28, start angle +0x30, sweep +0x38; point +0x18/+0x20). Other fields of 'P' and of
   unknown types: 0 (NOT DECODED: their content). */
static double gp_raw(const Ent *e, int off) {
    if (e->id == 'A') switch (off) { case 0x18: return e->c.x; case 0x20: return e->c.y; case 0x28: return e->r; case 0x30: return e->sa; case 0x38: return e->sw; default: return 0; }
    if (e->id == 'L') switch (off) { case 0x18: return e->p1.x; case 0x20: return e->p1.y; case 0x28: return e->p2.x; case 0x30: return e->p2.y; default: return 0; }
    if (e->id == 'P') switch (off) { case 0x18: return e->p1.x; case 0x20: return e->p1.y; default: return 0; }
    return 0;
}

/* ours [inferred]: the MSVC 2015 STL std::sort (introsort: insertion sort up to 32 elements, partition by median guess
   (median of 3, ninther above 40), heap sort when the depth budget runs out; budget = count, (b >> 1) + (b >> 2) per
   level, smaller side recursed). The structure matches FUN_1800fa160 (l.135521-135565) / FUN_1800fa390 and the insertion
   sorts FUN_1800f8a60 (l.134590-134644) / FUN_1800f8b60 (l.134647-134757); equal-key order follows the algorithm. */
typedef int (*GpLess)(const void *a, const void *b);
static void gp_swap(char *a, char *b, size_t w) { if (a != b) for (size_t k = 0; k < w; k++) { char t = a[k]; a[k] = b[k]; b[k] = t; } }
#define GP_AT(i) (base + (size_t)(i) * w)
static void gp_isort(char *base, long n, size_t w, GpLess lt, char *tmp) {
    for (long i = 1; i < n; i++) {
        memcpy(tmp, GP_AT(i), w);
        if (lt(tmp, GP_AT(0))) { memmove(GP_AT(1), GP_AT(0), (size_t)i * w); memcpy(GP_AT(0), tmp, w); }
        else { long j = i; while (lt(tmp, GP_AT(j - 1))) { memcpy(GP_AT(j), GP_AT(j - 1), w); j--; } memcpy(GP_AT(j), tmp, w); }
    }
}
static void gp_med3(char *base, size_t w, GpLess lt, long f, long m, long l) {
    if (lt(GP_AT(m), GP_AT(f))) gp_swap(GP_AT(m), GP_AT(f), w);
    if (lt(GP_AT(l), GP_AT(m))) { gp_swap(GP_AT(l), GP_AT(m), w); if (lt(GP_AT(m), GP_AT(f))) gp_swap(GP_AT(m), GP_AT(f), w); }
}
static void gp_heap_hole(char *base, size_t w, GpLess lt, long hole, long bottom, const char *val) {
    long top = hole, idx = hole, maxnl = (bottom - 1) >> 1;
    while (idx < maxnl) { idx = 2 * idx + 2; if (lt(GP_AT(idx), GP_AT(idx - 1))) idx--; memcpy(GP_AT(hole), GP_AT(idx), w); hole = idx; }
    if (idx == maxnl && bottom % 2 == 0) { memcpy(GP_AT(hole), GP_AT(bottom - 1), w); hole = bottom - 1; }
    for (long i = (hole - 1) >> 1; top < hole && lt(GP_AT(i), val); i = (hole - 1) >> 1) { memcpy(GP_AT(hole), GP_AT(i), w); hole = i; }
    memcpy(GP_AT(hole), val, w);
}
static void gp_heapsort(char *base, long n, size_t w, GpLess lt, char *tmp) {
    for (long hole = n >> 1; hole > 0; ) { hole--; memcpy(tmp, GP_AT(hole), w); gp_heap_hole(base, w, lt, hole, n, tmp); }
    for (long last = n; last >= 2; last--) { memcpy(tmp, GP_AT(last - 1), w); memcpy(GP_AT(last - 1), GP_AT(0), w); gp_heap_hole(base, w, lt, 0, last - 1, tmp); }
}
static void gp_partition(char *base, long first, long last, size_t w, GpLess lt, long *pf, long *pl) {
    long mid = first + ((last - first) >> 1), L = last - 1;
    if (40 < L - first) {
        long st = (L - first + 1) / 8;
        gp_med3(base, w, lt, first, first + st, first + 2 * st); gp_med3(base, w, lt, mid - st, mid, mid + st);
        gp_med3(base, w, lt, L - 2 * st, L - st, L); gp_med3(base, w, lt, first + st, mid, L - st);
    } else gp_med3(base, w, lt, first, mid, L);
    long Pf = mid, Pl = Pf + 1;
    while (first < Pf && !lt(GP_AT(Pf - 1), GP_AT(Pf)) && !lt(GP_AT(Pf), GP_AT(Pf - 1))) Pf--;
    while (Pl < last && !lt(GP_AT(Pl), GP_AT(Pf)) && !lt(GP_AT(Pf), GP_AT(Pl))) Pl++;
    long Gf = Pl, Gl = Pf;
    for (;;) {
        for (; Gf < last; Gf++) {
            if (lt(GP_AT(Pf), GP_AT(Gf))) ;
            else if (lt(GP_AT(Gf), GP_AT(Pf))) break;
            else if (Pl != Gf) { gp_swap(GP_AT(Pl), GP_AT(Gf), w); Pl++; }
            else Pl++;
        }
        for (; first < Gl; Gl--) {
            if (lt(GP_AT(Gl - 1), GP_AT(Pf))) ;
            else if (lt(GP_AT(Pf), GP_AT(Gl - 1))) break;
            else if (--Pf != Gl - 1) gp_swap(GP_AT(Pf), GP_AT(Gl - 1), w);
        }
        if (Gl == first && Gf == last) { *pf = Pf; *pl = Pl; return; }
        if (Gl == first) { if (Pl != Gf) gp_swap(GP_AT(Pf), GP_AT(Pl), w); Pl++; gp_swap(GP_AT(Pf), GP_AT(Gf), w); Pf++; Gf++; }
        else if (Gf == last) { if (--Gl != --Pf) gp_swap(GP_AT(Gl), GP_AT(Pf), w); gp_swap(GP_AT(Pf), GP_AT(--Pl), w); }
        else { gp_swap(GP_AT(Gf), GP_AT(Gl - 1), w); Gf++; Gl--; }
    }
}
static void gp_sort_r(char *base, long first, long last, long ideal, size_t w, GpLess lt, char *tmp) {
    for (;;) {
        long n = last - first;
        if (n <= 32) { if (n > 1) gp_isort(GP_AT(first), n, w, lt, tmp); return; }
        if (ideal <= 0) { gp_heapsort(GP_AT(first), n, w, lt, tmp); return; }
        long pf, pl; gp_partition(base, first, last, w, lt, &pf, &pl);
        ideal = (ideal >> 1) + (ideal >> 2);
        if (pf - first < last - pl) { gp_sort_r(base, first, pf, ideal, w, lt, tmp); first = pl; }
        else { gp_sort_r(base, pl, last, ideal, w, lt, tmp); last = pf; }
    }
}
#undef GP_AT
static void gp_stdsort(void *base, long n, size_t w, GpLess lt) {
    if (n < 2) return;
    char *tmp = malloc(w); gp_sort_r(base, 0, n, n, w, lt, tmp); free(tmp);
}
/* ours [inferred]: the Microsoft CRT qsort (copy of md_qsort, cam2d_pock_medax.c: median of three, CUTOFF 8 shortsort) -
   the order of equal keys in the planner's crossing list (FUN_1801066f0 l.142099) */
static void gp_msqsort(void *base, size_t num, size_t w, int (*cmp)(const void *, const void *)) {
    char *lostk[64], *histk[64]; int stkptr = 0;
    if (num < 2) return;
    char *lo = base, *hi = (char *)base + w * (num - 1);
recurse:;
    size_t size = (size_t)(hi - lo) / w + 1;
    if (size <= 8) {
        char *h = hi;
        while (h > lo) { char *max = lo; for (char *p = lo + w; p <= h; p += w) if (cmp(p, max) > 0) max = p; gp_swap(max, h, w); h -= w; }
    } else {
        char *mid = lo + (size / 2) * w;
        if (cmp(lo, mid) > 0) gp_swap(lo, mid, w);
        if (cmp(lo, hi) > 0) gp_swap(lo, hi, w);
        if (cmp(mid, hi) > 0) gp_swap(mid, hi, w);
        char *loguy = lo, *higuy = hi;
        for (;;) {
            if (mid > loguy) { do loguy += w; while (loguy < mid && cmp(loguy, mid) <= 0); }
            if (mid <= loguy) { do loguy += w; while (loguy <= hi && cmp(loguy, mid) <= 0); }
            do higuy -= w; while (higuy > mid && cmp(higuy, mid) > 0);
            if (higuy < loguy) break;
            gp_swap(loguy, higuy, w);
            if (mid == higuy) mid = loguy;
        }
        higuy += w;
        if (mid < higuy) { do higuy -= w; while (higuy > mid && cmp(higuy, mid) == 0); }
        if (mid >= higuy) { do higuy -= w; while (higuy > lo && cmp(higuy, mid) == 0); }
        if (higuy - lo >= hi - loguy) {
            if (lo < higuy) { lostk[stkptr] = lo; histk[stkptr] = higuy; ++stkptr; }
            if (loguy < hi) { lo = loguy; goto recurse; }
        } else {
            if (loguy < hi) { lostk[stkptr] = loguy; histk[stkptr] = hi; ++stkptr; }
            if (lo < higuy) { hi = higuy; goto recurse; }
        }
    }
    --stkptr;
    if (stkptr >= 0) { lo = lostk[stkptr]; hi = histk[stkptr]; goto recurse; }
}

/* ---------------------------------------------------------------------------- region orientation, slicing angle, rotation */
/* FUN_1800f33a0 (l.131850-131876): RevSh islands with dir +1 and non-islands with dir -1 (outers CCW, islands CW) */
static void gp_orient(ShL *G) {
    for (int i = 0; i < G->n; i++) { Sh *s = G->s[i]; if ((sh_island(s) && s->dir == 1) || (!sh_island(s) && s->dir == -1)) sh_rev(s); }
}
/* FUN_1800f3220 (l.131795-131847) = FUN_180124620(0.1 t, G) per shape FUN_180124110 (l.156996-157193): a copy of the
   shape with every arc replaced by the chords of ApxArcWithLnSegs(0.1 t, arc, pts, true) (point rule as decoded in
   mcce_medax.c arc_chords, FUN_1800ab630 + ApxArcWithLnSegs; NOT DECODED: the effect of the 4th argument), other
   entities copied; the shape takes its place */
static void gp_linearise(double t, ShL *G) {
    double tol = t * 0.1;
    for (int i = 0; i < G->n; i++) {
        Sh *s = G->s[i], o = *s; o.e = NULL; o.n = o.cap = 0;
        for (int k = 0; k < s->n; k++) {
            const Ent *a = &s->e[k];
            if (a->id != 'A') { sh_push(&o, *a); continue; }
            int n; double dth;
            if (1e-12 <= a->r) {
                if (tol <= a->r) { dth = acos(1.0 - tol / a->r) * 2.0; if (1e-12 <= dth) { if (1.5707963267948966 < dth) dth = 1.5707963267948966; } else dth = 1e-12; }
                else dth = 1.5707963267948966;
                double acc = 0.0;
                for (double x = fabs(a->sw); 0.0 < x; x += -3.141592653589793) acc += (3.141592653589793 < x ? 3.141592653589793 : x) / dth;
                n = (int)(acc + 1.0); if (n < 1) n = 1;
            } else n = 1;
            P2 q = p2(cos(a->sa) * a->r + a->c.x, sin(a->sa) * a->r + a->c.y); double ang = a->sa;
            for (int j = 1; j <= n; j++) {
                ang = a->sw / (double)n + ang; P2 r = p2(cos(ang) * a->r + a->c.x, sin(ang) * a->r + a->c.y);
                Ent l = *a; l.id = 'L'; l.p1 = q; l.p2 = r; l.c = p2(0, 0); l.r = l.sa = l.sw = 0; sh_push(&o, l); q = r;
            }
        }
        free(s->e); *s = o; gp_upd_minmax(s);
    }
}
/* FUN_1800f2480 (l.131308-131553): for P in (E, A): q = nearest point of G (FUN_18013cfe0); |P - q| <= 1e-4, 0 <= t <= 1
   and q at least 1e-6 from the entity's start and end (an arc's from its geometric ends, other types from raw +0x18 /
   +0x20) -> the entity is split at t (FUN_180071580: a copy inserted after it; 'L' first end := lerp, second start :=
   lerp; 'A' first sweep * t, second start + sw t, sweep - sw t), SetChnMinMax both */
static void gp_split_at(P2 E, P2 A, ShL *G) {
    for (int w = 0; w < 2; w++) {
        P2 P = w == 0 ? E : A; Pos q;
        if (!sl_nearest(G, P, &q)) continue;
        if (!(pdist(P, q.pt) <= 0.0001) || !(0.0 <= q.t && q.t <= 1.0)) continue;
        Sh *s = G->s[q.sh]; Ent *e = &s->e[q.ent];
        P2 st = e->id == 'A' ? p2(cos(e->sa) * e->r + e->c.x, sin(e->sa) * e->r + e->c.y) : p2(gp_raw(e, 0x18), gp_raw(e, 0x20));
        if (!(1e-06 <= pdist(st, q.pt))) continue;
        P2 en = e->id == 'L' ? e->p2 : e->id == 'A' ? p2(cos(e->sw + e->sa) * e->r + e->c.x, sin(e->sw + e->sa) * e->r + e->c.y) : p2(gp_raw(e, 0x18), gp_raw(e, 0x20));
        if (!(1e-06 <= pdist(en, q.pt))) continue;
        Ent c = *e; sh_push(s, c);
        memmove(&s->e[q.ent + 2], &s->e[q.ent + 1], (s->n - q.ent - 2) * sizeof(Ent));
        s->e[q.ent + 1] = c;
        Ent *a = &s->e[q.ent], *b = &s->e[q.ent + 1]; double t = q.t;
        if (a->id == 'L') a->p2 = p2((a->p2.x - a->p1.x) * t + a->p1.x, (a->p2.y - a->p1.y) * t + a->p1.y);
        else if (a->id == 'A') a->sw = t * a->sw;
        if (b->id == 'L') b->p1 = p2((b->p2.x - b->p1.x) * t + b->p1.x, (b->p2.y - b->p1.y) * t + b->p1.y);
        else if (b->id == 'A') { double d = b->sw * t; b->sw = b->sw - d; b->sa = d + b->sa; }
        gp_upd_minmax(s);
    }
}
/* ---------------------------------------------------------------------------- region preparation (FUN_1800f3de0) */
static void cs_merge(Sh *s, double tol);   /* FUN_18014f740, defined with the ChaikinSmoothSh port below */
/* FUN_1800f3440 (l.131879-131918): the first entity with the largest |raw +0x30 - raw +0x20| (a line's |dy|), strict >;
   FUN_18014fd50 (l.180573-180713): when that index is > 0 and the shape is closed, the entity list rotated to start there */
static void gp_start_steep(Sh *s) {
    double best = -1.0; int k = -1;
    for (int i = 0; i < s->n; i++) { double v = fabs(gp_raw(&s->e[i], 0x30) - gp_raw(&s->e[i], 0x20)); if (best < v) { best = v; k = i; } }
    if (k <= 0 || s->dir == 0) return;
    Ent *o = malloc(s->n * sizeof(Ent)); int m = 0;
    for (int i = k; i < s->n; i++) o[m++] = s->e[i];
    for (int i = 0; i < k; i++) o[m++] = s->e[i];
    memcpy(s->e, o, s->n * sizeof(Ent)); free(o);
}
/* LAB_180004566 = FUN_1800f0f70 (disasm): position order by entity (+0x04), then t (+0x08) */
static int gp_lt_pos(const void *a, const void *b) {
    const Pos *p = a, *q = b;
    return p->ent != q->ent ? p->ent < q->ent : q->t > p->t;
}
/* FUN_1800f3520 (l.131922-132262): positions {entity 0, t 0}, the nearest point to E and to A (FUN_18013d280, kept when
   sqrt(dy^2 + dx^2) < 1e-4), {last entity, t 1}, std::sort (FUN_1800f00e0, MSVC introsort [inferred]); fewer than 3: the
   shape itself is the only piece; else the sections between consecutive positions (FUN_180146e20, full = 0) into new
   header-less shapes (CSh ctor: dir 0, flags 0), empty ones erased */
static void gp_pieces(P2 E, P2 A, const Sh *s, ShL *out) {
    Pos pos[4]; int n = 0;
    memset(pos, 0, sizeof pos); n = 1;
    for (int w = 0; w < 2; w++) {
        P2 P = w ? A : E; Pos q;
        if (!sh_nearest(s, P, &q)) { q.sh = 0; q.ent = -1; q.t = 0; q.pt = p2(0, 0); }
        q.sh = 0;
        if (sqrt((q.pt.y - P.y) * (q.pt.y - P.y) + (q.pt.x - P.x) * (q.pt.x - P.x)) < 0.0001) pos[n++] = q;
    }
    pos[n].sh = 0; pos[n].ent = s->n - 1; pos[n].t = 1.0; pos[n].pt = p2(0, 0); n++;
    gp_stdsort(pos, n, sizeof(Pos), gp_lt_pos);
    if (n < 3) { sl_push(out, sh_copy(s)); return; }
    for (int k = 1; k < n; k++) {
        Sh *p = sh_new();
        sh_section_full(s, pos[k - 1].ent, pos[k - 1].t, pos[k].ent, pos[k].t, p, 0);
        if (p->n) sl_push(out, p); else sh_free(p);
    }
}
/* FUN_1800f3de0 (l.132267-132320): per shape: FUN_1800f3440; FUN_1800f3520 (pieces); FUN_18014f740(piece, t) on each
   (= cs_merge, the collinear merge of the ChaikinSmoothSh port); FUN_1800f21d0 (l.131208-131304): two or more pieces are
   joined into one shape with the original header (CSh::operator=, entities cleared, FUN_180146b10 appends); the shape is
   replaced by the (first) result - a single piece of a split keeps its zero header */
static void gp_prepare(double t, P2 E, P2 A, ShL *G) {
    for (int i = 0; i < G->n; i++) {
        Sh *s = G->s[i];
        gp_start_steep(s);
        ShL pc = { 0 }; gp_pieces(E, A, s, &pc);
        for (int k = 0; k < pc.n; k++) { cs_merge(pc.s[k], t); gp_upd_minmax(pc.s[k]); }
        Sh *r;
        if (pc.n > 1) { r = sh_copy(s); r->n = 0; for (int k = 0; k < pc.n; k++) for (int j = 0; j < pc.s[k]->n; j++) sh_push(r, pc.s[k]->e[j]); gp_upd_minmax(r); }
        else if (pc.n == 1) { r = pc.s[0]; pc.s[0] = NULL; }
        else r = NULL;   /* NOT DECODED: an empty piece list (FUN_1800f3de0 then reads *local_50 of an empty vector) */
        if (r) { sh_free(s); G->s[i] = r; }
        for (int k = 0; k < pc.n; k++) sh_free(pc.s[k]);
        free(pc.s);
    }
}
/* FUN_1801270e0 (l.158734-158835): an arc's term of FUN_180126ac0's sum. a = sa wrapped into [-pi, pi], w = sw clamped to
   [-2pi, 2pi]; the arc is walked from its low angle lo to its high angle hi (lo = a, hi = a + w; w < 0: swapped, dir -1) in
   pieces that end at 0 / pi (first) and then every pi (b = min(next, hi)): acc += (((b - lo) - sin(b - lo)) r^2 + (y_prev +
   y_b)(x_prev - x_b)) dir, until |b - hi| < 1e-12. Returns the end point in travel order (*pe; for dir -1 the swapped pair) */
static void gp_arc_area(const Ent *e, double *acc, P2 *pe) {
    double a = e->sa;
    while (a < -3.141592653589793) a = a + 6.283185307179586;
    while (3.141592653589793 < a) a = a + -6.283185307179586;
    double w = -6.283185307179586;
    if (-6.283185307179586 <= e->sw) { w = e->sw; if (6.283185307179586 < e->sw) w = 6.283185307179586; }
    int dir = 1; double hi = w + a, lo = a;
    if (w < 0.0) { dir = -1; lo = hi; hi = a; }
    P2 ps = p2(cos(lo) * e->r + e->c.x, sin(lo) * e->r + e->c.y), prev = ps, cur;
    double nxt = 0.0 <= lo ? 3.141592653589793 : 0.0;
    for (;;) {
        double b = hi < nxt ? hi : nxt;
        cur = p2(cos(b) * e->r + e->c.x, sin(b) * e->r + e->c.y);
        /* the segment term is ((b - lo) - sin(b - lo)) * (r * r) (disasm 0x180127289 mulsd xmm6,xmm6 = r^2, 0x18012729e; the
           decompile prints it as x * r * r); MCCE_MEASX=0: (x * r) * r */
        double seg = swx_on() ? ((b - lo) - sin(b - lo)) * (e->r * e->r) : ((b - lo) - sin(b - lo)) * e->r * e->r;
        *acc = (seg + (prev.y + cur.y) * (prev.x - cur.x)) * (double)dir + *acc;
        if (fabs(b - hi) < 1e-12) break;
        prev = cur; nxt = b + 3.141592653589793; lo = b;
    }
    *pe = dir == -1 ? ps : cur;
}
/* FUN_180126ac0 (l.158544-158718; arcs: FUN_1801270e0 above): twice the signed area by the trapezoid rule from the last
   entity's end, a gap > tol stops it (area 0, dir 0); dir = +1 when the sum is > 0, else -1; area = |sum / 2| */
static void gp_area_dir(double tol, const Sh *s, double *area, int *dir) {
    *area = 0; *dir = 0;
    if (!s->n) return;
    const Ent *l = &s->e[s->n - 1];
    double px, py;
    if (l->id == 'L') { px = l->p2.x; py = l->p2.y; }
    else if (l->id == 'A') { px = cos(l->sw + l->sa) * l->r + l->c.x; py = sin(l->sw + l->sa) * l->r + l->c.y; }
    else { px = gp_raw(l, 0x18); py = gp_raw(l, 0x20); }
    double acc = 0.0; int k;
    for (k = 0; k < s->n; k++) {
        const Ent *e = &s->e[k]; double sx, sy;
        if (e->id == 'A') { sx = cos(e->sa) * e->r + e->c.x; sy = sin(e->sa) * e->r + e->c.y; }
        else { sx = gp_raw(e, 0x18); sy = gp_raw(e, 0x20); }
        double dx = px - sx, d2 = (py - sy) * (py - sy) + dx * dx;
        if (tol * tol < d2) break;
        if (1.0000000000000001e-20 < d2) acc = acc + (sy + py) * dx;
        if (e->id == 'L') { acc = acc + (e->p2.y + e->p1.y) * (e->p1.x - e->p2.x); px = e->p2.x; py = e->p2.y; }
        else if (e->id == 'A') { P2 pe; gp_arc_area(e, &acc, &pe); px = pe.x; py = pe.y; }
    }
    if (s->n <= k) { *dir = 0.0 < acc ? 1 : -1; *area = fabs(acc * 0.5); }
}
/* FUN_1800f2b40 (l.131554-131604): 1 (planner fails) when a shape intersects itself (FUN_18012f2b0 = FUN_18012f3b0's
   ISIS list in mode 2 non-empty, gp_self_cross), or FUN_180126ac0(0.001) gives the last shape an area < 1e-6 or it has
   fewer than 3 entities, or a shape's dir differs from the computed one */
static int gp_self_cross(const Sh *s);
static int gp_degenerate(const ShL *G) {
    for (int i = 0; i < G->n; i++) {
        if (gp_self_cross(G->s[i])) return 1;
        double a; int dir; gp_area_dir(0.001, G->s[i], &a, &dir);
        if (i == G->n - 1 && (a < 1e-06 || G->s[i]->n < 3)) return 1;
        if (G->s[i]->dir != dir) return 1;
    }
    return 0;
}
/* FUN_1800f2ca0 (l.131608-131791): for P in (E, A) not strictly inside G (FUN_180112a00(1e-5) != 2): q = nearest point
   (FUN_18013cfe0, entity k); dP = |P - q|, d1 / d2 = |q - raw start| / |q - raw end| of entity k; when d1 or d2 < dP or
   < 1e-5 the nearer vertex moves to P (d1 < d2: the end of entity k - 1, cyclic), else a copy of entity k ending at q is
   inserted before it and entity k starts at q (FUN_180071580), then the vertex between them moves to P: entity k's raw end
   and entity (k + 1) % n's raw start := P (SetChnMinMax); any change: FUN_18013c390 = GetMinMaxViaShEnt per shape.
   The raw +0x18..+0x30 writes assume lines (the region is linearised here). */
static void gp_snap_ends(P2 E, P2 A, ShL *G) {
    if (!G->n || !G->s[0]->n) return;
    int changed = 0;
    for (int w = 0; w < 2; w++) {
        P2 P = w ? A : E; Pos q;
        if (sl_inside(G, 1e-05, P) == 2) continue;
        if (!sl_nearest(G, P, &q) || q.sh < 0 || q.ent < 0) continue;
        Sh *s = G->s[q.sh]; int k = q.ent;
        double dP = sqrt((P.y - q.pt.y) * (P.y - q.pt.y) + (P.x - q.pt.x) * (P.x - q.pt.x));
        double a = q.pt.y - gp_raw(&s->e[k], 0x20), b = q.pt.x - gp_raw(&s->e[k], 0x18), d1 = sqrt(a * a + b * b);
        double c = q.pt.x - gp_raw(&s->e[k], 0x28), e = q.pt.y - gp_raw(&s->e[k], 0x30), d2 = sqrt(e * e + c * c);
        if (d1 < dP || d2 < dP || d1 < 1e-05 || d2 < 1e-05) { if (d1 < d2) k = k == 0 ? s->n - 1 : k - 1; }
        else {
            Ent cp = s->e[k]; cp.p2 = q.pt;
            s->e[k].p1 = q.pt;
            sh_push(s, cp); memmove(&s->e[k + 1], &s->e[k], (s->n - 1 - k) * sizeof(Ent)); s->e[k] = cp;
        }
        s->e[k].p2 = P;
        s->e[(k + 1) % s->n].p1 = P;
        changed = 1;
    }
    if (changed) for (int i = 0; i < G->n; i++) gp_upd_minmax(G->s[i]);
}
/* ---------------------------------------------------------------------------- exact entity intersection (CalcEntIntersect) */
static int gp_segseg(double tol, P2 a1, P2 a2, P2 b1, P2 b2, double *s, double *u, P2 *x);
static int gp_lnln(P2 a, P2 da, P2 b, P2 db, double *s, double *u, P2 *x);
static int gp_isab_dup(const Sh *A, const Sh *B, const Isab *rp, const Isab *rq);
static void gp_ent_minmax(const Ent *e, double b[4]);
/* FUN_1800a9000 (l.92047-92171): circle (c, r) x carrier p + t d. The foot f of the perpendicular from c
   (CalcLnLnIntersectPt with the line c + u (-d.y, d.x), disasm 0x1800a905b-0x1800a90a9; its 0 - |d|^2 < 1e-14 or the
   residual check - goes to FUN_1800a9490, gp_cirln_poly below); D = |c - f|^2: inside (r - 1e-14 clamped at 0)^2 or beyond (r + 1e-6)^2 -> two points
   f -+ h d / |d| (none when D > r^2), parameters from the larger |d| component, a point whose distance to c is more than
   1e-6 off r dropped (the second moves first), two parameters within 1e-12 -> one; else (near tangent) one point, t = the
   foot's parameter. FUN_1800a5150 (l.90182-90260) only evaluates angles (no effect). */
static int poly_roots2(const double c0[3], double *out);   /* FUN_1800afd30 (mcce_medax.c) */
/* FUN_1800a9490 (l.92174-92208): |p + t d - c|^2 = r^2 as c0 + c1 t + c2 t^2 (the decompile's operation order), roots by
   FUN_1800afd30(2, ...) into t (ascending, at most 2) */
static int gp_cirln_poly(P2 c, double r, P2 p, P2 d, double t[2]) {
    double k[3];
    k[0] = ((((p.x * p.x + p.y * p.y) - c.x * 2.0 * p.x) - c.y * 2.0 * p.y) + c.x * c.x + c.y * c.y) - r * r;
    k[2] = d.x * d.x + d.y * d.y;
    k[1] = (((d.x * p.x + d.y * p.y) - d.x * c.x) - d.y * c.y) * 2.0;
    double x[4]; int n = poly_roots2(k, x);
    for (int i = 0; i < n && i < 2; i++) t[i] = x[i];
    return n;
}
static int gp_cirln(P2 c, double r, P2 p, P2 d, double t[2]) {
    t[0] = t[1] = 0.0;
    double s, u; P2 f;
    if (!gp_lnln(p, d, c, p2(-d.y, d.x), &s, &u, &f)) return gp_cirln_poly(c, r, p, d, t);
    double rr = r - 1e-14, D = (c.y - f.y) * (c.y - f.y) + (c.x - f.x) * (c.x - f.x);
    if (rr < 0.0) rr = 0.0;
    if (!(D <= rr * rr || (r + 1e-06) * (r + 1e-06) <= D)) { t[0] = s; return 1; }
    if (!(D <= r * r)) return 0;
    double h = sqrt(r * r - D), L = sqrt(d.y * d.y + d.x * d.x), ox = d.x * (1.0 / L) * h, oy = d.y * (1.0 / L) * h;
    P2 P[2] = { p2(f.x - ox, f.y - oy), p2(ox + f.x, oy + f.y) };
    if (fabs(d.x) <= fabs(d.y)) { t[0] = (P[0].y - p.y) / d.y; t[1] = (P[1].y - p.y) / d.y; }
    else { t[0] = (P[0].x - p.x) / d.x; t[1] = (P[1].x - p.x) / d.x; }
    int n = 2;
    for (int k = 0; k < n; k++) {
        double q = sqrt((c.y - P[k].y) * (c.y - P[k].y) + (c.x - P[k].x) * (c.x - P[k].x));
        if (fabs(q - r) > 1e-06) { if (k == 0 && n == 2) { P[0] = P[1]; t[0] = t[1]; } n--; k--; }
    }
    if (n == 2 && fabs(t[0] - t[1]) < 1e-12) n = 1;
    return n;
}
/* FUN_1800a4820 (l.89869-89925): the arc parameter of pt (no: pt within 1e-14 of the centre or outside the sweep grown by
   tol / r radians); t = (angle - low end) / |sw|, 1 - that for a clockwise arc; |sw| < 1e-12 -> 0.5 */
static int gp_arc_t(double tol, const Ent *a, P2 pt, double *t) {
    if (fabs(pt.x - a->c.x) <= 1e-14 && fabs(pt.y - a->c.y) <= 1e-14) return 0;
    double g = atan2(pt.y - a->c.y, pt.x - a->c.x), hi, lo;
    if (a->sw <= 0.0) { lo = a->sa + a->sw; hi = a->sa; } else { hi = a->sa + a->sw; lo = a->sa; }
    double e = a->r <= 1e-12 ? 0.0 : tol / a->r;
    while (e + hi < g) g = g + -6.283185307179586;
    while (g < lo - e) g = g + 6.283185307179586;
    if (e + hi < g) return 0;
    if (1e-12 <= fabs(a->sw)) { *t = (g - lo) / fabs(a->sw); if (a->sw < 0.0) *t = 1.0 - *t; }
    else *t = 0.5;
    return 1;
}
/* mcce::CalcCirCirIntersect (l.13288-13380): centres d apart (1e-7 <= d <= |r1 + r2| + 1e-7); from the smaller circle's
   centre A towards B: a = (d^2 + r^2 - R^2) / 2d, h^2 = r^2 - a^2 (< -1e-7: none; < 1e-13: the one point M) */
static int gp_circir(P2 c1, double r1, P2 c2, double r2, P2 pt[2]) {
    double dx = c1.x - c2.x, dy = c1.y - c2.y, d = sqrt(dy * dy + dx * dx);
    if (!(d <= fabs(r2 + r1) + 1e-07 && 1e-07 <= d)) return 0;
    P2 A = c1, B = c2; double r = r1, R = r2;
    if (r2 < r1) { A = c2; B = c1; R = r1; r = r2; }
    double ux = B.x - A.x, uy = B.y - A.y, a = ((d * d + r * r) - R * R) / (d * 2.0), L = sqrt(uy * uy + ux * ux);
    if (!(1e-12 <= L)) return 0;
    uy = uy / L; ux = ux / L;
    P2 M = p2(ux * a + A.x, uy * a + A.y);
    double h2 = r * r - a * a;
    if (!(-1e-07 <= h2)) return 0;
    if (1e-13 <= h2) { double h = sqrt(h2); pt[0] = p2(-uy * h + M.x, ux * h + M.y); pt[1] = p2(-uy * -h + M.x, ux * -h + M.y); return 2; }
    pt[0] = M; return 1;
}
/* the common tail of CalcArcLnSegIntersect (l.8514-8549) and FUN_1800a4460 (l.89800-89835): fl = per-point rejects; two
   kept points closer than tol, or the first one's second parameter outside [0, 1] while ..., collapse to one */
static int gp_isect_tail(double tol, int n, const char fl[2], double t1[2], double t2[2], P2 pt[2]) {
    if (n == 2 && !fl[0]) {
        if (!fl[1]) {
            if (tol * tol <= (pt[0].y - pt[1].y) * (pt[0].y - pt[1].y) + (pt[0].x - pt[1].x) * (pt[0].x - pt[1].x)) return 2;
            if (t2[0] < 0.0 || 1.0 < t2[0] || (0.0 <= t2[1] && t2[1] <= 1.0 && (t1[0] < 0.0 || 1.0 < t1[0]))) goto shift;
        }
        n--;
    } else if (fl[1]) n--;
    if (!fl[0]) return n;
shift:
    if (n - 1 != 0) { pt[0] = pt[1]; t1[0] = t1[1]; t2[0] = t2[1]; }
    return n - 1;
}
/* mcce::CalcArcLnSegIntersect (l.8395-8551) with bool false: FUN_1800a9000 on the line's carrier; per point pt = p + t d;
   kept when (t >= 0 or within tol of p1) and (t <= 1 or within tol of p2), FUN_1800a4820 gives its arc parameter and the
   arc point there is within 1e-6 (squared 1e-12) */
static int gp_arcln(double tol, const Ent *arc, const Ent *ln, double tA[2], double tL[2], P2 pt[2]) {
    P2 p = ln->p1, d = p2(ln->p2.x - ln->p1.x, ln->p2.y - ln->p1.y);
    char fl[2] = { 0, 0 };
    int n = gp_cirln(arc->c, arc->r, p, d, tL);
    for (int k = 0; k < n; k++) {
        pt[k] = p2(d.x * tL[k] + p.x, d.y * tL[k] + p.y);
        int ok = (0.0 <= tL[k] || (pt[k].y - p.y) * (pt[k].y - p.y) + (pt[k].x - p.x) * (pt[k].x - p.x) <= tol * tol)
              && (tL[k] <= 1.0 || (pt[k].y - ln->p2.y) * (pt[k].y - ln->p2.y) + (pt[k].x - ln->p2.x) * (pt[k].x - ln->p2.x) <= tol * tol)
              && gp_arc_t(tol, arc, pt[k], &tA[k]);
        if (ok) {
            double g = arc->sw * tA[k] + arc->sa, ex = (cos(g) * arc->r + arc->c.x) - pt[k].x, ey = (sin(g) * arc->r + arc->c.y) - pt[k].y;
            if (1e-12 < ey * ey + ex * ex) ok = 0;
        }
        if (!ok) fl[k] = 1;
    }
    return gp_isect_tail(tol, n, fl, tA, tL, pt);
}
/* FUN_1800a4460 (l.89743-89836) with param_7 0: CalcCirCirIntersect, each point needs FUN_1800a4820 on both arcs */
static int gp_arcarc(double tol, const Ent *a, const Ent *b, double ta[2], double tb[2], P2 pt[2]) {
    char fl[2] = { 0, 0 };
    int n = gp_circir(a->c, a->r, b->c, b->r, pt);
    for (int k = 0; k < n; k++) if (!gp_arc_t(tol, a, pt[k], &ta[k]) || !gp_arc_t(tol, b, pt[k], &tb[k])) fl[k] = 1;
    return gp_isect_tail(tol, n, fl, ta, tb, pt);
}
/* mcce::CalcEntIntersect (l.6348-6388): 'A' 'A' FUN_1800a4460; 'L' 'A' / 'A' 'L' CalcArcLnSegIntersect (arc first);
   'L' 'L' CalcLnSegLnSegIntersect; other pairs none */
static int gp_ent_isect(double tol, const Ent *a, const Ent *b, double ta[2], double tb[2], P2 pt[2]) {
    if (a->id == 'A' && b->id == 'A') return gp_arcarc(tol, a, b, ta, tb, pt);
    if (a->id == 'L' && b->id == 'A') return gp_arcln(tol, b, a, tb, ta, pt);
    if (a->id == 'A' && b->id == 'L') return gp_arcln(tol, a, b, ta, tb, pt);
    if (a->id == 'L' && b->id == 'L') return gp_segseg(tol, a->p1, a->p2, b->p1, b->p2, &ta[0], &tb[0], &pt[0]);
    return 0;
}
/* FUN_180133c70 (l.165530-165671): shape boxes within 1e-5, entity boxes (SetChnMinMax) within 1e-5, CalcEntIntersect(1e-5)
   records pushed last hit first; CRT qsort by X (LAB_1800074dc), dedupe FUN_180133b30 (cyclic predecessor), link
   FUN_1801349c0 (qsort_s by Y) */
static int gp_box_ov(const double a[4], const double b[4]) { return a[0] - 1e-05 <= b[2] && b[0] <= a[2] + 1e-05 && a[1] - 1e-05 <= b[3] && b[1] <= a[3] + 1e-05; }
static void gp_pair_isect(const Sh *A, const Sh *B, IsabL *out) {
    out->n = 0;
    if (!gp_box_ov(A->bb, B->bb)) return;
    for (int ka = 0; ka < A->n; ka++) {
        double ba[4]; gp_ent_minmax(&A->e[ka], ba);
        if (!gp_box_ov(ba, B->bb)) continue;
        for (int kb = 0; kb < B->n; kb++) {
            double bb[4]; gp_ent_minmax(&B->e[kb], bb);
            if (!gp_box_ov(ba, bb)) continue;
            double ta[2], tb[2]; P2 pt[2]; int k = gp_ent_isect(1e-05, &A->e[ka], &B->e[kb], ta, tb, pt);
            for (int h = k - 1; h >= 0; h--) { Isab r = { pt[h], 0, ka, ta[h], 0, kb, tb[h], -1, -1, 0 }; il_push(out, r); }
        }
    }
    if (!out->n) return;
    gp_msqsort(out->r, out->n, sizeof(Isab), isab_cmp_x);
    for (int i = 0; i < out->n; ) {
        int p = (i == 0 ? out->n : i) - 1;
        if (p != i && gp_isab_dup(A, B, &out->r[p], &out->r[i])) { memmove(&out->r[i], &out->r[i + 1], (out->n - i - 1) * sizeof(Isab)); out->n--; }
        else i++;
    }
    if (!out->n) return;
    int *idx = malloc(out->n * sizeof(int)); for (int i = 0; i < out->n; i++) idx[i] = i;
    g_isab_base = out->r; gp_msqsort(idx, out->n, sizeof(int), isab_cmp_y);
    for (int k = 0; k < out->n; k++) { out->r[idx[k]].next = idx[k + 1 < out->n ? k + 1 : 0]; out->r[idx[k]].prev = idx[k > 0 ? k - 1 : out->n - 1]; }
    free(idx);
}
/* ---------------------------------------------------------------------------- concave-corner cuts (FUN_1800ec930) */
/* FUN_180146e20 (l.175722-176061): the section, then GetMinMaxViaShEnt on out (l.176056) when something was appended */
static void gp_section(const Sh *s, int e1, double t1, int e2, double t2, Sh *out) {
    double bb[4]; memcpy(bb, out->bb, sizeof bb);
    if (sh_section_full(s, e1, t1, e2, t2, out, 0) > 0) gp_upd_minmax(out);
    else memcpy(out->bb, bb, sizeof bb);
}
/* the tangent of FUN_18011ffd0 / FUN_1801204a0 (l.155017-155323) at (e, t): 'L' p2 - p1 (raw); 'A' (point(t) - centre) |sw|
   turned by +90 deg (sw > 0) or -90 deg (sw < 0), sw == 0 or other types: 0 */
static P2 gp_tan_vec(const Ent *e, double t) {
    if (e->id == 'L') return p2(e->p2.x - e->p1.x, e->p2.y - e->p1.y);
    if (e->id != 'A') return p2(0, 0);
    double g = t * e->sw + e->sa, c = cos(g), sn = sin(g);
    double vx = (c * e->r + e->c.x) - e->c.x, vy = (sn * e->r + e->c.y) - e->c.y;
    vy = fabs(e->sw) * vy; vx = fabs(e->sw) * vx;
    if (e->sw <= 0.0) { if (0.0 <= e->sw) return p2(0, 0); return p2(vy, -vx); }
    return p2(-vy, vx);
}
static P2 gp_pt_at(const Ent *e, double t) { return e_at(e, t); }   /* FUN_180021580 (only compared with the ends below) */
static P2 gp_raw_start(const Ent *e) { return e->id == 'A' ? p2(cos(e->sa) * e->r + e->c.x, sin(e->sa) * e->r + e->c.y) : e->id == 'L' || e->id == 'P' ? e->p1 : p2(0, 0); }
static P2 gp_raw_end(const Ent *e) {
    if (e->id == 'L') return e->p2;
    if (e->id == 'A') { double g = e->sw + e->sa; return p2(cos(g) * e->r + e->c.x, sin(g) * e->r + e->c.y); }
    return e->id == 'P' ? e->p1 : p2(0, 0);
}
/* FUN_1801204a0 (l.155171-155323): tangent leaving (k, t); at the entity's end (squared distance < tol2) of a closed shape
   or a non-last entity: the next entity at t 0; entities with |v|^2 <= tol2 skipped forwards (open: stop at the last; one
   full turn at most) */
static P2 gp_tan_out(const Sh *s, int k, double t, double tol2) {
    if (!s->n) return p2(0, 0);
    P2 P = gp_pt_at(&s->e[k], t), Q = gp_raw_end(&s->e[k]);
    int e = k, last = 0;
    if ((P.x - Q.x) * (P.x - Q.x) + (P.y - Q.y) * (P.y - Q.y) < tol2 && (s->dir != 0 || s->n - 1 != k)) { t = 0.0; e = (k + 1) % s->n; }
    for (;;) {
        P2 v = gp_tan_vec(&s->e[e], t);
        if (last || tol2 < v.y * v.y + v.x * v.x || (s->dir == 0 && s->n - 1 == e)) return v;
        e = (e + 1) % s->n; t = 0.0;
        if (e == k) last = 1;
    }
}
/* FUN_18011ffd0 (l.155017-155168): tangent arriving at (k, t); at the entity's start of a closed shape or k > 0: the previous
   entity at t 1; degenerate entities skipped backwards (open: stop at entity 0; one full turn at most) */
static P2 gp_tan_in(const Sh *s, int k, double t, double tol2) {
    if (!s->n) return p2(0, 0);
    P2 P = gp_pt_at(&s->e[k], t), Q = gp_raw_start(&s->e[k]);
    int e = k, last = 0;
    if ((P.x - Q.x) * (P.x - Q.x) + (P.y - Q.y) * (P.y - Q.y) < tol2 && (s->dir != 0 || k != 0)) { e = k - 1 < 0 ? s->n - 1 : k - 1; t = 1.0; }
    for (;;) {
        P2 v = gp_tan_vec(&s->e[e], t);
        if (last || tol2 < v.y * v.y + v.x * v.x || (s->dir == 0 && e == 0)) return v;
        e = e - 1 < 0 ? s->n - 1 : e - 1; t = 1.0;
        if (e == k) last = 1;
    }
}
/* FUN_1800eb810 (l.127554-127640, disasm 0x1800eb8ac-0x1800eb90d): tangent arriving at entity k's start (FUN_18011ffd0)
   and leaving it (FUN_1801204a0), both at t 0 with 1.0000000000000002e-10; either zero (|x| and |y| <= 1e-12) -> no;
   turn = atan2(out) - atan2(in) wrapped to [-pi, pi]; turn < 0 (concave): V = entity k's start (arc: centre + r (cos sa,
   sin sa); other types raw +0x18 / +0x20), bisector angle = atan2(out) - (turn + pi) / 2 */
static int gp_corner(const Sh *s, int k, P2 *V, double *ang) {
    *V = p2(0, 0); *ang = 0.0;
    P2 ti = gp_tan_in(s, k, 0.0, 1.0000000000000002e-10), to = gp_tan_out(s, k, 0.0, 1.0000000000000002e-10);
    if (fabs(ti.x) <= 1e-12 && fabs(ti.y) <= 1e-12) return 0;
    double a1 = atan2(ti.y, ti.x);
    if (fabs(to.x) <= 1e-12 && fabs(to.y) <= 1e-12) return 0;
    double a2 = atan2(to.y, to.x), turn = a2 - a1;
    if (-3.141592653589793 <= turn) { if (3.141592653589793 < turn) turn = turn - 6.283185307179586; }
    else turn = turn + 6.283185307179586;
    if (0.0 <= turn) return 0;
    const Ent *e = &s->e[k];
    if (e->id == 'A') *V = p2(cos(e->sa) * e->r + e->c.x, sin(e->sa) * e->r + e->c.y);
    else *V = p2(gp_raw(e, 0x18), gp_raw(e, 0x20));
    *ang = a2 - (turn + 3.141592653589793) * 0.5;
    return 1;
}
/* FUN_1800eb6e0 (l.127504-127522): V within 1.5 step of E or of A (squared, strict) */
static int gp_near_ends(double step, P2 E, P2 A, P2 V) {
    double r2 = step * 1.5 * step * 1.5, dy = V.y - E.y, dx = V.x - E.x;
    if (dy * dy + dx * dx < r2) return 1;
    dy = V.y - A.y; dx = V.x - A.x;
    return dy * dy + dx * dx < r2;
}
/* FUN_1800ebc20 (l.127671-127722): the cut shape = a clockwise circle of radius s at c in four quarter arcs (start angles
   pi, pi/2, 0, -pi/2, sweep -pi/2), dir -1, UpdMinMaxSh */
static void gp_cut_circle(double s, P2 c, Sh *o) {
    o->n = 0;
    sh_push(o, ent_arc(c, s, 3.141592653589793, -1.5707963267948966));
    sh_push(o, ent_arc(c, s, 1.5707963267948966, -1.5707963267948966));
    sh_push(o, ent_arc(c, s, 0.0, -1.5707963267948966));
    sh_push(o, ent_arc(c, s, -1.5707963267948966, -1.5707963267948966));
    o->dir = -1;
    gp_upd_minmax(o);
}
/* FUN_1800ebae0 (l.127643-127668): the piece cut off - ClearAndClearObjSh (header and box zeroed), the shape's section
   hit i -> hit j (X side), RevSh, the cut circle's section hit i -> hit j (Y side), dir -1 */
static void gp_cut_piece(const Sh *S, const Sh *cut, const IsabL *H, int i, int j, Sh *o) {
    o->n = 0; o->dir = 0; o->fl = 0; o->tag = 0; o->z = 0; o->b90 = 0; memset(o->bb, 0, sizeof o->bb);
    gp_section(S, H->r[i].xe, H->r[i].xt, H->r[j].xe, H->r[j].xt, o);
    sh_rev(o);
    gp_section(cut, H->r[i].ye, H->r[i].yt, H->r[j].ye, H->r[j].yt, o);
    o->dir = -1;
}
/* FUN_1800eb550 (l.127451-127501): with more than one shape, a hit of the piece (FUN_180133c70) with any other shape */
static int gp_piece_hits(const ShL *G, int sh, const Sh *piece) {
    if (G->n < 2) return 0;
    for (int u = 0; u < G->n; u++) {
        if (u == sh) continue;
        IsabL I = { 0 }; gp_pair_isect(G->s[u], piece, &I); int hit = I.n != 0; il_free(&I);
        if (hit) return 1;
    }
    return 0;
}
/* FUN_1800eb750 (l.127525-127551): the circle centre c within s + 1e-7 of E or of A */
static int gp_centre_near(P2 E, P2 A, double s, P2 c) {
    double dy = c.y - E.y, dx = c.x - E.x;
    if (!(s + 1e-07 <= sqrt(dy * dy + dx * dx))) return 1;
    dy = c.y - A.y; dx = c.x - A.x;
    return sqrt(dy * dy + dx * dx) < s + 1e-07;
}
/* FUN_1800ebe50 (l.127725-127833): the cut shape = a copy of the shape's header (CSh::operator=), entities: the
   shape's (0, 0) -> hit i (only when j != 0), the circle's hit i -> hit j (*kres = the entity count - 1 after it), the
   shape's hit j -> (j == 0 ? hit i : (last, 1.0)) */
static Sh *gp_cut_write(const Sh *S, const Sh *cut, const IsabL *H, int i, int j, int *kres) {
    Sh *o = sh_copy(S); o->n = 0;
    if (j != 0) gp_section(S, 0, 0.0, H->r[i].xe, H->r[i].xt, o);
    gp_section(cut, H->r[i].ye, H->r[i].yt, H->r[j].ye, H->r[j].yt, o);
    *kres = o->n - 1;
    if (j == 0) gp_section(S, H->r[j].xe, H->r[j].xt, H->r[i].xe, H->r[i].xt, o);
    else gp_section(S, H->r[j].xe, H->r[j].xt, S->n - 1, 1.0, o);
    return o;
}
/* FUN_1800ec230 (l.127836-128100): one trial cut of size s at the corner (shape sh, entity k, t 0, V) - s >= 1e-7; centre
   c = V + 0.75 s (cos a, sin a); the circle FUN_1800ebc20(s, c); hits FUN_180133c70(shape, circle) (>= 2); the hit pair
   around the corner's key k + 0.5 t (keys xe + 0.5 xt): i = the first index whose predecessor (cyclic, n - 1 for 0) has a
   key < K and K < its own key, or 0 when the last key < K; it must be the ring successor of its predecessor and vice
   versa (+0x30 / +0x34); the piece FUN_1800ebae0 must not hit another shape (FUN_1800eb550) and c must keep s + 1e-7 from
   E and A (FUN_1800eb750); then FUN_1800ebe50 writes the result into *res (the previous one dropped) */
static int gp_try_cut(double s, P2 E, P2 A, double ang, int sh, int k, double t, P2 V, const ShL *G, Sh **res, int *kres) {
    if (!(1e-07 <= s)) return 0;
    double s75 = s * 0.75;   /* disasm 0x1800ec390-0x1800ec3c4: s * 0.75 first */
    P2 c = p2(cos(ang) * s75 + V.x, sin(ang) * s75 + V.y);
    Sh cut = { 0 }; gp_cut_circle(s, c, &cut);
    const Sh *S = G->s[sh];
    IsabL H = { 0 }; gp_pair_isect(S, &cut, &H);
    int ok = 0;
    if (H.n > 1) {
        double K = t * 0.5 + (double)k;
        int n = H.n, prev = n - 1, i = 0;
        for (i = 0; i < n; i++) {
            double kp = H.r[prev].xt * 0.5 + (double)H.r[prev].xe;
            if (i == 0) { if (kp < K) break; if (K < H.r[i].xt * 0.5 + (double)H.r[i].xe) break; }
            else if (kp < K && K < H.r[i].xt * 0.5 + (double)H.r[i].xe) break;
            prev = i;
        }
        if (i < n && H.r[prev].next == i && H.r[i].prev == prev) {
            Sh piece = { 0 }; gp_cut_piece(S, &cut, &H, prev, i, &piece);
            if (!gp_piece_hits(G, sh, &piece) && !gp_centre_near(E, A, s, c)) {
                if (*res) sh_free(*res);
                *res = gp_cut_write(S, &cut, &H, prev, i, kres);
                ok = 1;
            }
            free(piece.e);
        }
    }
    il_free(&H); free(cut.e);
    return ok;
}
/* FUN_1800e9820 (l.126642-126703): bisection on the cut size from step (hi = step, lo = 0): a success at the first trial
   stops at once, else up to 10 trials (s := (hi + lo) / 2 after each); then, when some trial succeeded, one more trial
   at (the final midpoint) - 1e-4 and the shape is swapped with the result list's shape (the last success, whichever
   trial wrote it) and the corner's entity index := its *kres */
static void gp_cut_search(double step, P2 E, P2 A, double ang, int sh, int *k, double t, P2 V, ShL *G) {
    double hi = step, lo = 0.0, s = step; Sh *res = NULL; int kres = -1;
    for (int it = 0;; it++) {
        int ok = gp_try_cut(s, E, A, ang, sh, *k, t, V, G, &res, &kres);
        double h2 = s;
        if (ok) { h2 = hi; lo = s; }
        hi = h2;
        s = (hi + lo) * 0.5;
        if (it == 0) { if (ok) break; }
        else if (8 < it) break;
    }
    if (res) {
        gp_try_cut(s - 0.0001, E, A, ang, sh, *k, t, V, G, &res, &kres);
        sh_free(G->s[sh]); G->s[sh] = res;
        *k = kres;
    }
}
/* FUN_1800ec930 (l.128104-128155): every entity k of every shape (counts re-read): a concave corner (FUN_1800eb810) not
   within 1.5 step of E or A (FUN_1800eb6e0) -> FUN_1800e9820 (which may replace the shape and move k); position t = 0 */
static void gp_corners(double step, P2 E, P2 A, ShL *G) {
    for (int sh = 0; sh < G->n; sh++)
        for (int k = 0; k < G->s[sh]->n; k++) {
            P2 V; double ang;
            if (gp_corner(G->s[sh], k, &V, &ang) && !gp_near_ends(step, E, A, V)) gp_cut_search(step, E, A, ang, sh, &k, 0.0, V, G);
        }
}
/* ---------------------------------------------------------------------------- end cuts (FUN_1800e9440) */
static void gp_dump_sl(const char *stage, const ShL *G);
/* FUN_180134270 (l.165674-165779) for X = {x}: FUN_180133c70 per Y shape j (Y index +0x20 = j, ring +0x30 / +0x34 offset by
   the count so far), CRT qsort by X (LAB_1800074dc), link FUN_1801349c0 (qsort_s by Y, rings per Y shape) */
static void gp_isect_list(const Sh *x, const ShL *Y, IsabL *out) {
    out->n = 0;
    for (int j = 0; j < Y->n; j++) {
        IsabL t = { 0 }; gp_pair_isect(x, Y->s[j], &t);
        for (int k = 0; k < t.n; k++) { Isab r = t.r[k]; r.ys = j; r.next += out->n; r.prev += out->n; il_push(out, r); }
        il_free(&t);
    }
    if (!out->n) return;
    gp_msqsort(out->r, out->n, sizeof(Isab), isab_cmp_x);
    int *idx = malloc(out->n * sizeof(int)); for (int i = 0; i < out->n; i++) idx[i] = i;
    g_isab_base = out->r; gp_msqsort(idx, out->n, sizeof(int), isab_cmp_y);
    for (int g0 = 0; g0 < out->n; ) {
        int g1 = g0; while (g1 < out->n && out->r[idx[g1]].ys == out->r[idx[g0]].ys) g1++;
        for (int k = g0; k < g1; k++) { out->r[idx[k]].next = idx[k + 1 < g1 ? k + 1 : g0]; out->r[idx[k]].prev = idx[k > g0 ? k - 1 : g1 - 1]; }
        g0 = g1;
    }
    free(idx);
}
/* FUN_1800ea7b0 (l.127051-127281): the cut size limit at P. q = FUN_18013cfe0 nearest point of G; |P - q| > 0.1 -> *sz :=
   min(*sz, 0.4 |P - q|); else, with the tangents in / out at q (FUN_18011ffd0 / FUN_1801204a0, 1e-6) both non-zero: the
   ray 'L' from q along a_in + turn / 2 + pi/2 (the inward bisector) of length (diagonal of the LAST shape's box) + 1,
   UpdMinMaxSh; its hits with G (FUN_180134690) in ray order: the class of the midpoint q..hit (FUN_180112a00, 1e-5): 1 next,
   4 stop, 2 -> *sz := min(*sz, |q - hit| / 2) and stop */
static void gp_end_limit(P2 P, const ShL *G, double *sz) {
    if (!G->n) return;
    Pos q; if (!sl_nearest(G, P, &q)) return;
    double dist = sqrt((q.pt.y - P.y) * (q.pt.y - P.y) + (q.pt.x - P.x) * (q.pt.x - P.x));
    if (!(dist <= 0.1)) { if (dist * 0.4 < *sz) *sz = dist * 0.4; return; }
    const Sh *S = G->s[q.sh];
    P2 ti = gp_tan_in(S, q.ent, q.t, 1e-06), to = gp_tan_out(S, q.ent, q.t, 1e-06);
    if (!(1e-12 < fabs(ti.x) || 1e-12 < fabs(ti.y))) return;
    double a1 = atan2(ti.y, ti.x);
    if (!(1e-12 < fabs(to.x) || 1e-12 < fabs(to.y))) return;
    double turn = atan2(to.y, to.x) - a1;
    if (-3.141592653589793 <= turn) { if (3.141592653589793 < turn) turn = turn - 6.283185307179586; }
    else turn = turn + 6.283185307179586;
    double ang = turn * 0.5 + a1 + 1.5707963267948966;
    const Sh *L = G->s[G->n - 1];
    double dx = L->bb[0] - L->bb[2], dy = L->bb[1] - L->bb[3], D = sqrt(dx * dx + dy * dy);
    double ey = sin(ang) * (D + 1.0) + q.pt.y, ex = cos(ang) * (D + 1.0) + q.pt.x;
    Sh ray = { 0 }; sh_push(&ray, ent_line(q.pt, p2(ex, ey))); gp_upd_minmax(&ray);
    IsabL I = { 0 }; gp_isect_list(&ray, G, &I);
    for (int k = 0; k < I.n; k++) {
        P2 M = p2((q.pt.x + I.r[k].pt.x) * 0.5, (q.pt.y + I.r[k].pt.y) * 0.5);
        int c = sl_inside(G, 1e-05, M);
        if (c == 1) continue;
        if (c == 4) break;
        if (c == 2) { double ax = q.pt.x - I.r[k].pt.x, ay = q.pt.y - I.r[k].pt.y, h = sqrt(ay * ay + ax * ax); if (h * 0.5 < *sz) *sz = h * 0.5; break; }
    }
    il_free(&I); free(ray.e);
}
/* FUN_1800eaf80 (l.127283-127448): one trial side circle of radius s at R.p + s R.n (FUN_1800ebc20); its centre must keep
   s - 1e-5 from E and A; hits with G (FUN_180134270, X = the circle): none -> success, nothing kept; else every pair of
   consecutive hits (cyclic, from (n - 1, 0)) whose circle section is outside G (FUN_1801130f0(..., full = n < 2, G, 1e-5)
   == 4) needs one end within d + 1e-5 of R.p, else failure; success -> *res := the circle */
static int gp_side_try(double d, P2 E, P2 A, double s, P2 Rp, P2 Rn, const ShL *G, Sh **res) {
    double cy = Rn.y * s + Rp.y, cx = Rn.x * s + Rp.x;
    Sh *c = sh_new(); gp_cut_circle(s, p2(cx, cy), c);
    double e1 = sqrt((E.y - cy) * (E.y - cy) + (E.x - cx) * (E.x - cx));
    if (!(s - 1e-05 <= e1)) { sh_free(c); return 0; }
    double e2 = sqrt((A.y - cy) * (A.y - cy) + (A.x - cx) * (A.x - cx));
    if (!(s - 1e-05 <= e2)) { sh_free(c); return 0; }
    IsabL I = { 0 }; gp_isect_list(c, G, &I);
    if (!I.n) { il_free(&I); sh_free(c); return 1; }
    for (int i = 0, p = I.n - 1; i < I.n; p = i, i++) {
        const Isab *a = &I.r[p], *b = &I.r[i];
        if (sec_in_list(c, a->xe, a->xt, b->xe, b->xt, I.n < 2, G, 1e-05) != 4) continue;
        double ax = Rp.x - a->pt.x, ay = Rp.y - a->pt.y;
        if (!(d + 1e-05 < sqrt(ay * ay + ax * ax))) continue;
        double bx = Rp.x - b->pt.x, by = Rp.y - b->pt.y;
        if (d + 1e-05 < sqrt(by * by + bx * bx)) { il_free(&I); sh_free(c); return 0; }
    }
    il_free(&I);
    if (*res) sh_free(*res);
    *res = c;
    return 1;
}
/* FUN_1800e9a80 (l.126709-126761): the FUN_1800e9820 bisection over FUN_1800eaf80 from sz (no final trial); the kept
   circle (the last success that had hits) is appended to out (AppendByMove) */
static void gp_side_search(double d, double sz, P2 E, P2 A, P2 Rp, P2 Rn, const ShL *G, ShL *out) {
    double hi = sz, lo = 0.0, s = sz; Sh *res = NULL;
    for (int it = 0;; it++) {
        int ok = gp_side_try(d, E, A, s, Rp, Rn, G, &res);
        double h2 = s;
        if (ok) { h2 = hi; lo = s; }
        hi = h2;
        s = (hi + lo) * 0.5;
        if (it == 0) { if (ok) break; }
        else if (8 < it) break;
    }
    if (res) sl_push(out, res);
}
/* FUN_1800e9c60 (l.126764-127048): the cut shapes at P = (mode ? E : A) with its tangent T = P+0x10 (mode 1: the exit end
   tangent, negated; mode 0: the approach start tangent): size step + 1e-4 limited by FUN_1800ea7b0, then + 1e-4; |T| < 1e-12
   -> none; side circles (FUN_1800e9a80) on the left (n = (-t.y, t.x)) and on the right ((t.y, -t.x)); every arc radius
   > 1e-4 shrunk by 1e-4 (box kept); two circles: a quadrilateral (4 lines, dir -1, UpdMinMaxSh) between them from
   P + 1e-4 t to P + min(r) / 2 t, and out = SoPockUnionAr(1e-6, the three) (mcce_sobool.inc);
   otherwise out = the circles */
static void gp_end_cuts(int mode, double d, double step, P2 E, P2 Ed, P2 A, P2 Ad, const ShL *G, ShL *out) {
    sl_clear(out);
    P2 P = mode ? E : A, T = mode ? Ed : Ad;
    double sz = step + 0.0001;
    gp_end_limit(P, G, &sz);
    double len = sqrt(T.x * T.x + T.y * T.y);
    if (!(1e-12 <= len)) return;
    double tx = T.x / len, ty = T.y / len;
    if (mode) { tx = -tx; ty = -ty; }
    ShL L = { 0 };
    double sz2 = sz + 0.0001;
    gp_side_search(d, sz2, E, A, P, p2(-ty, tx), G, &L);
    gp_side_search(d, sz2, E, A, P, p2(ty, -tx), G, &L);
    for (int i = 0; i < L.n; i++) for (int k = 0; k < L.s[i]->n; k++) { Ent *e = &L.s[i]->e[k]; if (e->id == 'A' && 0.0001 < e->r) e->r = e->r - 0.0001; }
    if (L.n != 2) { sl_move_append(out, &L); sl_free(&L); return; }
    double r0 = L.s[0]->e[0].r, r1 = L.s[1]->e[0].r, rm = r1 <= r0 ? r1 : r0;
    double n1x = -ty, n1y = tx, n2x = ty, n2y = -tx;
    double py = P.y + ty * 0.0001, px = P.x + tx * 0.0001;
    double qy = P.y + ty * rm * 0.5, qx = P.x + tx * rm * 0.5;
    P2 a = p2(n2x * r1 + px, py + n2y * r1), b = p2(n1x * r0 + px, py + n1y * r0);
    P2 c = p2(n1x * r0 + qx, qy + n1y * r0), e = p2(n2x * r1 + qx, qy + n2y * r1);
    Sh *q = sh_new();
    sh_push(q, ent_line(a, b)); sh_push(q, ent_line(b, c)); sh_push(q, ent_line(c, e)); sh_push(q, ent_line(e, a));
    q->dir = -1; gp_upd_minmax(q); sl_push(&L, q);
    ShL none = { 0 }, U;
    if (!sl_bool_dll(MCCE_RB_UNION, 1e-06, &L, &none, 0.0, &U)) {   /* SoPockUnionAr(1e-6, the three) (mcce_sobool.inc) */
        g_ex_keep_pieces = 1;
        U = sl_bool_m(&L, &none, 0, 0.0);   /* ours, only where the port refuses (FUN_18015f790 NOT PORTED): our exact union */
        g_ex_keep_pieces = 0;
    }
    sl_move_append(out, &U); sl_free(&U); sl_free(&L);
}
/* FUN_1800e9440 (l.126522-126627): for mode 0 then 1: X = FUN_1800e9c60(mode); R = FUN_180153b30(1e-6, G, X) = G minus X
   (the DLL's boolean, mcce_sobool.inc); exactly one non-island shape in R (FUN_180112860) -> G := R and
   FUN_1800f33a0 */
static void gp_end_cut(double d, double step, P2 E, P2 Ed, P2 A, P2 Ad, ShL *G) {
    for (int mode = 0; mode < 2; mode++) {
        ShL X = { 0 }; gp_end_cuts(mode, d, step, E, Ed, A, Ad, G, &X);
        char nm[16]; snprintf(nm, sizeof nm, "cutin%d", mode); gp_dump_sl(nm, G);
        snprintf(nm, sizeof nm, "cutset%d", mode); gp_dump_sl(nm, &X);
        ShL R;
        if (!sl_bool_dll(MCCE_RB_MINUS, 1e-06, G, &X, 0.0, &R)) {   /* FUN_180153b30(1e-6, G, X) (mcce_sobool.inc) */
            g_ex_keep_pieces = 1;
            R = sl_minus_m(G, &X, 0.0);   /* ours, only where the port refuses (FUN_18015f790 NOT PORTED): our exact boolean */
            g_ex_keep_pieces = 0;
        }
        snprintf(nm, sizeof nm, "cutbool%d", mode); gp_dump_sl(nm, &R);
        if (sl_nonisland(&R) == 1) { sl_free(G); *G = R; for (int i = 0; i < G->n; i++) gp_upd_minmax(G->s[i]); gp_orient(G); }
        else sl_free(&R);
        sl_free(&X);
    }
}
static int gp_lt_dbl(const void *a, const void *b) { return *(const double *)a < *(const double *)b; }
/* FUN_1800f18a0 (l.130890-131205): angles atan((y1 - y2) / (x2 - x1)) of every entity with x2 != x1 (raw +0x18..+0x30),
   plus pi/2 and -pi/2 (l.131054, l.131123); std::sort (FUN_1800eff40); the midpoint of the first largest gap */
static double gp_slice_angle(const ShL *G) {
    int n = 0; for (int i = 0; i < G->n; i++) n += G->s[i]->n;
    double *a = malloc((n + 2) * sizeof(double)); int m = 0;
    for (int i = 0; i < G->n; i++) for (int k = 0; k < G->s[i]->n; k++) {
        const Ent *e = &G->s[i]->e[k]; double dx = gp_raw(e, 0x28) - gp_raw(e, 0x18);
        if (dx != 0.0) a[m++] = atan((gp_raw(e, 0x20) - gp_raw(e, 0x30)) / dx);
    }
    a[m++] = 1.5707963267948966; a[m++] = -1.5707963267948966;
    gp_stdsort(a, m, sizeof(double), gp_lt_dbl);
    int best = 1;
    for (int k = 2; k < m; k++) if (a[best] - a[best - 1] < a[k] - a[k - 1]) best = k;
    double r = (a[best - 1] + a[best]) * 0.5; free(a); return r;
}
/* RotationMx(a) applied as p' = p.M (CCW by a, §6.6 note: RotationMx(t) = [cos sin; -sin cos], l.11801-11815) to a point
   and to shapes (FUN_180125ec0: arcs keep their radius, start angle + a; NOT DECODED: its body, assumed the matrix
   product on every point field) */
static P2 gp_rot(P2 p, double a) { double c = cos(a), s = sin(a); return p2(p.x * c - p.y * s, p.x * s + p.y * c); }
/* mcce::UpdMinMaxSh (l.3249-3330): SetChnMinMax (l.9234-9395) on every entity ('L' raw ends, 'A' start, end and the
   quadrant points round(ceil(lo / (pi/2))) .. round(floor(hi / (pi/2))), 'P' the point, other types: no box (0)), the shape
   box = the first entity's box, then the running min / max (the shape's value kept on ties) */
static void gp_ent_minmax(const Ent *e, double b[4]) {
    b[0] = b[1] = b[2] = b[3] = 0;
    if (e->id == 'A') {
        double x = cos(e->sa) * e->r + e->c.x, y = sin(e->sa) * e->r + e->c.y;
        b[0] = b[2] = x; b[1] = b[3] = y;
        double x2 = cos(e->sa + e->sw) * e->r + e->c.x, y2 = sin(e->sa + e->sw) * e->r + e->c.y;
        if (x2 < b[0]) b[0] = x2;
        if (y2 < b[1]) b[1] = y2;
        if (!(x2 <= b[2])) b[2] = x2;
        if (!(y2 <= b[3])) b[3] = y2;
        double lo = e->sa, hi = e->sa + e->sw;
        if (hi < lo) { double t = lo; lo = hi; hi = t; }
        double q0 = ceil(lo / 1.5707963267948966), q1 = floor(hi / 1.5707963267948966);
        q0 = q0 <= 0.0 ? q0 - 0.5 : q0 + 0.5; q1 = q1 <= 0.0 ? q1 - 0.5 : q1 + 0.5;
        for (int q = (int)q0; q <= (int)q1; q++) {
            double qx = cos((double)q * 1.5707963267948966) * e->r + e->c.x, qy = sin((double)q * 1.5707963267948966) * e->r + e->c.y;
            if (qx < b[0]) b[0] = qx;
            if (qy < b[1]) b[1] = qy;
            if (!(qx <= b[2])) b[2] = qx;
            if (!(qy <= b[3])) b[3] = qy;
        }
    } else if (e->id == 'L') {
        b[0] = e->p1.x <= e->p2.x ? e->p1.x : e->p2.x; b[1] = e->p1.y <= e->p2.y ? e->p1.y : e->p2.y;
        b[2] = e->p2.x <= e->p1.x ? e->p1.x : e->p2.x; b[3] = e->p2.y <= e->p1.y ? e->p1.y : e->p2.y;
    } else if (e->id == 'P') { b[0] = b[2] = e->p1.x; b[1] = b[3] = e->p1.y; }
}
static void gp_upd_minmax(Sh *s) {
    for (int k = 0; k < s->n; k++) {
        double b[4]; gp_ent_minmax(&s->e[k], b);
        if (k == 0) { memcpy(s->bb, b, sizeof b); continue; }
        if (!(s->bb[0] <= b[0])) s->bb[0] = b[0];
        if (!(s->bb[1] <= b[1])) s->bb[1] = b[1];
        if (!(b[2] <= s->bb[2])) s->bb[2] = b[2];
        if (!(b[3] <= s->bb[3])) s->bb[3] = b[3];
    }
}
/* FUN_180125ec0 (l.158095-158117) = FUN_180125b80 (l.157983-158092) per shape with the matrix RotationMx(a) (l.11801-11822:
   cos, sin, -sin, cos) applied as CTMatrix::operator*(CPt2) (l.5279-5300: x' = y m10 + x m00 + 0, y' = x m01 + y m11 + 0):
   'L' both ends, 'P' the point, 'A' the centre, radius := |start' - centre'| and start angle := atan2 of it (only when
   one component exceeds 1e-6; sweep kept); then UpdMinMaxSh */
static P2 gp_mx(P2 p, double c, double s) { return p2(p.y * -s + p.x * c + 0.0, p.x * s + p.y * c + 0.0); }
static void gp_rotate(ShL *G, double a) {
    double c = cos(a), sn = sin(a);
    for (int i = 0; i < G->n; i++) {
        Sh *s = G->s[i];
        for (int k = 0; k < s->n; k++) {
            Ent *e = &s->e[k];
            if (e->id == 'A') {
                P2 st = gp_mx(p2(cos(e->sa) * e->r + e->c.x, sin(e->sa) * e->r + e->c.y), c, sn);
                e->c = gp_mx(e->c, c, sn);
                double dy = st.y - e->c.y, dx = st.x - e->c.x;
                e->r = sqrt(dy * dy + dx * dx);
                if ((1e-06 <= fabs(dx) && fabs(dx) != 1e-06) || (1e-06 <= fabs(dy) && fabs(dy) != 1e-06)) e->sa = atan2(dy, dx);
            } else if (e->id == 'L') { e->p1 = gp_mx(e->p1, c, sn); e->p2 = gp_mx(e->p2, c, sn); }
            else if (e->id == 'P') e->p1 = gp_mx(e->p1, c, sn);
        }
        gp_upd_minmax(s);
    }
}

/* ---------------------------------------------------------------------------- horizontal crossings (FUN_1801066f0) */
/* CHp band index FUN_180106d60 (l.142165-...): one band per entity at its raw +0x20 (std::sort FUN_180104c50 by y, a band
   within 1.0000000000000002e-06 of its predecessor erased); every entity is filed in each band with
   ymin - 1e-4 < y < ymax + 1e-4 (its bbox), in (shape, entity) order */
typedef struct { double y; int *se; int n, cap; } GpBand;
typedef struct { GpBand *b; int n; } GpBands;
static int gp_lt_band(const void *a, const void *b) { return ((const GpBand *)a)->y < ((const GpBand *)b)->y; }
static void gp_bands(const ShL *G, GpBands *B) {
    int n = 0; for (int i = 0; i < G->n; i++) n += G->s[i]->n;
    B->b = calloc(n ? n : 1, sizeof(GpBand)); B->n = 0;
    for (int i = 0; i < G->n; i++) for (int k = 0; k < G->s[i]->n; k++) B->b[B->n++].y = gp_raw(&G->s[i]->e[k], 0x20);
    gp_stdsort(B->b, B->n, sizeof(GpBand), gp_lt_band);
    for (int k = 1; k < B->n; k++) if (B->b[k].y - B->b[k - 1].y < 1.0000000000000002e-06) { memmove(&B->b[k], &B->b[k + 1], (B->n - k - 1) * sizeof(GpBand)); B->n--; k--; }
    for (int i = 0; i < G->n; i++) for (int k = 0; k < G->s[i]->n; k++) {
        double bb[4]; e_bbox(&G->s[i]->e[k], bb);
        double lo = bb[1] - 0.0001, hi = bb[3] + 0.0001;
        int f = 0, c = B->n;   /* lower_bound: first band with y >= lo */
        while (c > 0) { int h = c / 2; if (B->b[f + h].y < lo) { f += h + 1; c -= h + 1; } else c = h; }
        if (f > B->n - 1) f = B->n - 1;
        if (f < 0) f = 0;
        for (int j = f; j < B->n; j++) {
            if (lo < B->b[j].y && B->b[j].y < hi) {
                GpBand *g = &B->b[j];
                if (g->n + 2 > g->cap) { g->cap = g->cap ? g->cap * 2 : 8; g->se = realloc(g->se, g->cap * 2 * sizeof(int)); }
                g->se[2 * g->n] = i; g->se[2 * g->n + 1] = k; g->n++;
            }
        }
    }
}
static void gp_bands_free(GpBands *B) { for (int k = 0; k < B->n; k++) free(B->b[k].se); free(B->b); B->b = NULL; B->n = 0; }
/* FUN_180106b70 (l.142111-142162): the band nearest to y (lower_bound, then the previous one when strictly nearer) */
static int gp_band_at(const GpBands *B, double y) {
    int f = 0, c = B->n;
    while (c > 0) { int h = c / 2; if (B->b[f + h].y < y) { f += h + 1; c -= h + 1; } else c = h; }
    if (f > B->n - 1) f = B->n - 1;
    if (f < 0) return 0;
    if (f != 0 && fabs(B->b[f - 1].y - y) < fabs(B->b[f].y - y)) f--;
    return f;
}
/* FUN_180134800 role (the duplicate test of isect(), FUN_180133b30): same point within 1e-10 and the same place on both
   shapes */
static int gp_isab_dup(const Sh *A, const Sh *B, const Isab *rp, const Isab *rq) {
    return fabs(rp->pt.x - rq->pt.x) <= 1e-10 && fabs(rp->pt.y - rq->pt.y) <= 1e-10
        && (rp->xe == rq->xe || short_sec(A, rp->xe, rp->xt, rq->xe, rq->xt, 1e-10) || short_sec(A, rq->xe, rq->xt, rp->xe, rp->xt, 1e-10))
        && (rp->ye == rq->ye || short_sec(B, rp->ye, rp->yt, rq->ye, rq->yt, 1e-10) || short_sec(B, rq->ye, rq->yt, rp->ye, rp->yt, 1e-10));
}
/* FUN_1801066f0 (l.142002-142106): the one-line shape X crossed with the entities of the band nearest to its start y:
   CalcEntIntersect(1e-5) per entity, only its first hit kept {pt, X (0, 0, t), Y (shape, entity, t)}; CRT qsort by the
   X position (LAB_1800074dc = isab_cmp_x); FUN_1801339b0 (l.165443-165490) erases a record equal to its cyclic
   predecessor (same X and Y shapes, FUN_180134800); FUN_1801349c0 links the rings */
/* mcce::CalcLnLnIntersectPt (l.3611-3640) on the carriers (a, da), (b, db), in its own operation order: parallel when
   |den| < 1e-14; the point is a + s da; true only when b + u db lies within 1e-6 of it (squared 1e-12) */
static int gp_lnln(P2 a, P2 da, P2 b, P2 db, double *s, double *u, P2 *x) {
    double den = -(db.y * da.x) - -(db.x * da.y);
    if (fabs(den) < 1e-14) return 0;
    *s = (-((b.x - a.x) * db.y) - -((b.y - a.y) * db.x)) / den;
    *x = p2(*s * da.x + a.x, *s * da.y + a.y);
    *u = ((b.y - a.y) * da.x - (b.x - a.x) * da.y) / den;
    double ey = (*u * db.y + b.y) - x->y, ex = (*u * db.x + b.x) - x->x;
    return ey * ey + ex * ex <= 1e-12;
}
/* mcce::CalcLnSegLnSegIntersect (l.12820-12897): gp_lnln of the segments a1 a2 / b1 b2 and no parameter outside [0, 1]
   farther than tol from the nearer end; s / u are returned unclamped */
static int gp_segseg(double tol, P2 a1, P2 a2, P2 b1, P2 b2, double *s, double *u, P2 *x) {
    if (!gp_lnln(a1, psub(a2, a1), b1, psub(b2, b1), s, u, x)) return 0;
    double t2 = tol * tol;
#define GP_D2(p, q) (((p).y - (q).y) * ((p).y - (q).y) + ((p).x - (q).x) * ((p).x - (q).x))
    if ((*s < 0.0 && t2 < GP_D2(*x, a1)) || (1.0 < *s && t2 < GP_D2(*x, a2)) || (*u < 0.0 && t2 < GP_D2(*x, b1)) || (1.0 < *u && t2 < GP_D2(*x, b2))) return 0;
#undef GP_D2
    return 1;
}
static void gp_cross(const Sh *X, const ShL *G, const GpBands *B, IsabL *out) {
    out->n = 0;
    if (!G->n || X->n != 1 || !B->n) return;
    const GpBand *g = &B->b[gp_band_at(B, gp_raw(&X->e[0], 0x20))];
    for (int q = 0; q < g->n; q++) {
        int i = g->se[2 * q], k = g->se[2 * q + 1];
        double ta[2], tb[2]; P2 pt[2]; const Ent *e = &G->s[i]->e[k];
        /* CalcEntIntersect (l.6348-6388): 'L'-'L' = CalcLnSegLnSegIntersect (unclamped parameters - FUN_1800ff8b0 tests
           them against [0, 1]); 'A' pairs: our e_isect (CalcArcLnSegIntersect / FUN_1800a4460 NOT DECODED) */
        if (X->e[0].id == 'L' && e->id == 'L') {
            if (gp_segseg(1e-5, X->e[0].p1, X->e[0].p2, e->p1, e->p2, &ta[0], &tb[0], &pt[0])) { Isab r = { pt[0], 0, 0, ta[0], i, k, tb[0], -1, -1, 0 }; il_push(out, r); }
        } else if (e_isect(&X->e[0], e, 1e-5, ta, tb, pt) > 0) { Isab r = { pt[0], 0, 0, ta[0], i, k, tb[0], -1, -1, 0 }; il_push(out, r); }
    }
    if (!out->n) return;
    gp_msqsort(out->r, out->n, sizeof(Isab), isab_cmp_x);
    for (int i = 0; i < out->n; i++) {
        int p = (i == 0 ? out->n : i) - 1;
        if (p == i) continue;
        Isab *rp = &out->r[p], *ri = &out->r[i];
        if (rp->xs == ri->xs && rp->ys == ri->ys && gp_isab_dup(X, G->s[rp->ys], rp, ri)) { memmove(ri, ri + 1, (out->n - i - 1) * sizeof(Isab)); out->n--; i--; }
    }
    isab_link(out);
}
/* FUN_180133700 (l.165341-...): erase record i, splice its ring, renumber */
static void gp_isab_erase(IsabL *I, int i) {
    I->r[I->r[i].prev].next = I->r[i].next;
    I->r[I->r[i].next].prev = I->r[i].prev;
    memmove(&I->r[i], &I->r[i + 1], (I->n - i - 1) * sizeof(Isab)); I->n--;
    for (int k = 0; k < I->n; k++) { if (I->r[k].next > i) I->r[k].next--; if (I->r[k].prev > i) I->r[k].prev--; }
}
/* FUN_1800ff8b0 (l.138563-138597): of two neighbours with |x - x'| <= 1e-10, the one whose Y parameter is outside [0, 1]
   is erased (both inside: both kept) */
static void gp_cross_clean(IsabL *I) {
    for (int k = 1; k < I->n; k++) {
        if (fabs(I->r[k].pt.x - I->r[k - 1].pt.x) <= 1e-10) {
            int e = k;
            if (0.0 <= I->r[k].yt && I->r[k].yt <= 1.0) { if (0.0 <= I->r[k - 1].yt && I->r[k - 1].yt <= 1.0) continue; e = k - 1; }
            gp_isab_erase(I, e); k--;
        }
    }
}

/* ---------------------------------------------------------------------------- gap classes (FUN_180108d70) */
/* FUN_1801089a0 (l.143031-143067): from entity e backwards to `stop` (exclusive), the first raw start (+0x18, +0x20) whose
   y differs from y by more than tol */
static int gp_back_pt(const Sh *s, int e, int stop, double y, double tol, P2 *out) {
    while (e != stop) {
        if (fabs(gp_raw(&s->e[e], 0x20) - y) > tol) { *out = p2(gp_raw(&s->e[e], 0x18), gp_raw(&s->e[e], 0x20)); return 1; }
        if (--e < 0) e = s->n - 1;
    }
    return 0;
}
/* FUN_180108a80 (l.143070-143115): forwards, raw end (+0x28, +0x30) */
static int gp_fwd_pt(const Sh *s, int e, int stop, double y, double tol, P2 *out) {
    while (e != stop) {
        if (fabs(gp_raw(&s->e[e], 0x30) - y) > tol) { *out = p2(gp_raw(&s->e[e], 0x28), gp_raw(&s->e[e], 0x30)); return 1; }
        if (++e >= s->n) e = 0;
    }
    return 0;
}
/* FUN_180108c80 (l.143151-143181, disasm 0x180108c80-0x180108d33): the entity of the previous record on the ring; when it
   is the entity before i's and one of the two parameters lies past its entity, the one before that */
static int gp_prev_stop(const IsabL *I, int i, const ShL *G) {
    int j = I->r[i].prev, e = I->r[i].ye - 1;
    if (e < 0) e = G->s[I->r[i].ys]->n - 1;
    if (I->r[j].ye == e && (I->r[i].yt < 0.0 || I->r[j].yt > 1.0) && I->r[j].prev != i) return I->r[I->r[j].prev].ye;
    return I->r[j].ye;
}
/* FUN_180108b80 (l.143118-143148, disasm 0x180108b80-0x180108c48): the same forwards */
static int gp_next_stop(const IsabL *I, int i, const ShL *G) {
    int j = I->r[i].next, e = I->r[i].ye + 1;
    if (e >= G->s[I->r[i].ys]->n) e = 0;
    if (I->r[j].ye == e && (I->r[i].yt > 1.0 || I->r[j].yt < 0.0) && I->r[j].next != i) return I->r[I->r[j].next].ye;
    return I->r[j].ye;
}
/* FUN_180126870 (l.158451-158539): sign of the area enclosed by the shape from position a to position b (chords of the raw
   fields, from a's point to its entity's raw end, whole entities, b's entity raw start to b's point) closed by b -> a:
   1 if > 0, else -1 (0 for an empty shape) */
static int gp_sec_sign(const Sh *s, const Pos *a, const Pos *b) {
    if (!s->n) return 0;
    double sum = 0, x1 = a->pt.x, y1 = a->pt.y, x2 = gp_raw(&s->e[a->ent], 0x28), y2 = gp_raw(&s->e[a->ent], 0x30);
    int u = a->ent, st = 0;
    if (a->ent == b->ent && a->t < b->t) { x2 = b->pt.x; y2 = b->pt.y; st = 1; }
    for (int g = 0; g < s->n + 3; g++) {
        if (++u >= s->n) u = 0;
        sum += (y2 + y1) * (x1 - x2);
        if (st == 1) { st = 2; x1 = b->pt.x; y1 = b->pt.y; x2 = a->pt.x; y2 = a->pt.y; continue; }
        if (st == 2) break;
        if (u == b->ent) { x1 = gp_raw(&s->e[u], 0x18); y1 = gp_raw(&s->e[u], 0x20); x2 = b->pt.x; y2 = b->pt.y; st = 1; }
        else { x1 = gp_raw(&s->e[u], 0x18); y1 = gp_raw(&s->e[u], 0x20); x2 = gp_raw(&s->e[u], 0x28); y2 = gp_raw(&s->e[u], 0x30); }
    }
    return 0.0 < sum ? 1 : -1;
}
static Pos gp_ypos(const Isab *r) { Pos p = { r->ys, r->ye, r->yt, r->pt }; return p; }
/* FUN_180109220 (l.143330-143404, disasm 0x180109316-0x18010936b): 4 when the section i -> j encloses area of the
   shape's own sense (or j -> i does not), for an outer; the reverse for an island; else 2 */
static int gp_gap_class2(int i, int j, const IsabL *I, const ShL *G) {
    const Sh *s = G->s[I->r[i].ys];
    Pos a = gp_ypos(&I->r[i]), b = gp_ypos(&I->r[j]);
    int flip = gp_sec_sign(s, &a, &b) != s->dir || gp_sec_sign(s, &b, &a) != s->dir;
    if (!sh_island(s)) return flip ? 4 : 2;
    return flip ? 2 : 4;
}
/* FUN_180108dd0 (l.143199-143327): class of the gap between crossings i and j: 2 different shapes; 1 (undecided) when
   the gap is shorter than 1e-5 at the midpoint; else per end crossing, when the boundary passes it monotonically in y (the
   points before / after it found by gp_back_pt / gp_fwd_pt within the neighbouring crossings), the side of the midpoint
   (cross >= 0: 2 inside, < 0: 4 outside); both ends must agree, else 1 */
static int gp_gap_class1(int i, int j, const IsabL *I, const ShL *G) {
    const Isab *ri = &I->r[i], *rj = &I->r[j];
    if (ri->ys != rj->ys) return 2;
    P2 M = p2((rj->pt.x + ri->pt.x) * 0.5, (rj->pt.y + ri->pt.y) * 0.5);
    if (sqrt((M.y - ri->pt.y) * (M.y - ri->pt.y) + (M.x - ri->pt.x) * (M.x - ri->pt.x)) < 1e-05) return 1;
    const Sh *s = G->s[ri->ys];
    int ci = 1, cj = 2; P2 a, b;
    if (gp_back_pt(s, ri->ye, gp_prev_stop(I, i, G), ri->pt.y, 1e-05, &a)) {
        if (gp_fwd_pt(s, ri->ye, gp_next_stop(I, i, G), ri->pt.y, 1e-05, &b)
            && ((M.y < a.y && b.y < M.y) || (a.y < M.y && M.y < b.y))) {
            double c = (M.y - a.y) * (ri->pt.x - a.x) - (M.x - a.x) * (ri->pt.y - a.y);
            ci = c >= 0.0 ? 2 : c < 0.0 ? 4 : 1;
            if (ci == 1) return 1;
        }
    }
    if (gp_back_pt(s, rj->ye, gp_prev_stop(I, j, G), rj->pt.y, 1e-05, &a)
        && gp_fwd_pt(s, rj->ye, gp_next_stop(I, j, G), rj->pt.y, 1e-05, &b)
        && ((M.y < a.y && b.y < M.y) || (a.y < M.y && M.y < b.y))) {
        double c = (M.y - a.y) * (rj->pt.x - a.x) - (M.x - a.x) * (rj->pt.y - a.y);
        if (c < 0.0) cj = 4;
    } else cj = 1;
    return ci == cj ? ci : 1;
}
static int gp_gap_class(int i, int j, const IsabL *I, const ShL *G) {   /* FUN_180108d70 (l.143184-143196, disasm 0x180108d70) */
    int c = gp_gap_class1(i, j, I, G);
    return c == 1 ? gp_gap_class2(i, j, I, G) : c;
}

/* ---------------------------------------------------------------------------- vertex records, items, trapezoids */
typedef struct { Pos L, R; int cls, used; } GpItem;                  /* 0x48: ends +0x00 / +0x20, class +0x40, used +0x44 */
typedef struct { GpItem *v; int n, cap; } GpItems;
typedef struct { int vtx; Pos p; } GpLink;                           /* 0x28 */
typedef struct { GpItems A, B; GpLink *c; int nc, capc; } GpRec;     /* CRail 0x80: lists +0x08, +0x30, +0x58 */
typedef struct { GpRec *r; int n; } GpRecs;
typedef struct { int idx; GpItem a; Pos bR, bL; } GpTrap;            /* CTrap 0x88: +0x00 index, +0x08 lower item, +0x48 upper
                                                                        item's right end, +0x68 its left end */
typedef struct { GpTrap *v; int n, cap; } GpTraps;
static void gi_push(GpItems *l, GpItem it) { if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 8; l->v = realloc(l->v, l->cap * sizeof(GpItem)); } l->v[l->n++] = it; }
static void gi_erase(GpItems *l, int i) { memmove(&l->v[i], &l->v[i + 1], (l->n - i - 1) * sizeof(GpItem)); l->n--; }
static void gt_push(GpTraps *l, const GpTrap *t) { if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 16; l->v = realloc(l->v, l->cap * sizeof(GpTrap)); } l->v[l->n++] = *t; }
static void gt_erase(GpTraps *l, int i, int cnt) { memmove(&l->v[i], &l->v[i + cnt], (l->n - i - cnt) * sizeof(GpTrap)); l->n -= cnt; }
static void gp_recs_free(GpRecs *R) { for (int i = 0; i < R->n; i++) { free(R->r[i].A.v); free(R->r[i].B.v); free(R->r[i].c); } free(R->r); R->r = NULL; R->n = 0; }
/* FUN_180100860 (l.139074-139098) / FUN_180100900 (l.139101-139130): vertex count; record index of (shape, entity) */
static int gp_nvtx(const ShL *G) { int n = 0; for (int i = 0; i < G->n; i++) n += G->s[i]->n; return n; }
static int gp_vidx(const ShL *G, int sh, int ent) { int n = 0; for (int i = 0; i < G->n && i < sh; i++) n += G->s[i]->n; return n + ent; }
/* FUN_1800ffc30 (l.138657-138695): the crossing nearest to p (first minimum), -1 if none */
static int gp_nearest_cross(const IsabL *I, P2 p) {
    int r = -1; double best = FLT_MAXD;
    for (int k = 0; k < I->n; k++) { double dx = I->r[k].pt.x - p.x, dy = I->r[k].pt.y - p.y, d = dx * dx + dy * dy; if (d < best) { r = k; best = d; } }
    return r;
}
/* FUN_1800fe1f0 (l.137746-138031): the horizontal line y = V.y across the box crossed with G; from V's crossing the gaps
   are classed to the right and to the left until the first outside (4) gap; every non-outside gap becomes an item; no
   item -> one item of class 1 at V itself */
static void gp_vertex_items(const double bb[4], const ShL *G, const GpBands *B, const Pos *V, GpItems *items) {
    items->n = 0;
    Sh X; memset(&X, 0, sizeof X); sh_push(&X, ent_line(p2(bb[0], V->pt.y), p2(bb[2], V->pt.y))); sh_upd_bbox(&X);
    IsabL I = { 0 }; gp_cross(&X, G, B, &I); gp_cross_clean(&I);
    int idx = gp_nearest_cross(&I, V->pt);
    if (idx >= 0) {
        int *cls = malloc(I.n * sizeof(int));
        for (int k = 0; k < I.n; k++) cls[k] = 4;
        for (int k = idx; k < I.n - 1; k++) { cls[k] = gp_gap_class(k, k + 1, &I, G); if (cls[k] == 4) break; }
        for (int k = idx - 1; k >= 0; k--) { cls[k] = gp_gap_class(k, k + 1, &I, G); if (cls[k] == 4) break; }
        if (I.n != 1) for (int k = 0; k < I.n - 1; k++) if (cls[k] != 4) {
            GpItem it; memset(&it, 0, sizeof it); it.cls = cls[k]; it.L = gp_ypos(&I.r[k]); it.R = gp_ypos(&I.r[k + 1]); gi_push(items, it);
        }
        if (!items->n) { GpItem it; memset(&it, 0, sizeof it); it.cls = 1; it.L = *V; it.R = *V; gi_push(items, it); }
        free(cls);
    }
    il_free(&I); free(X.e);
}
/* FUN_1800fffc0 (l.138769-138908): for an item whose ends lie on one shape: ends ordered along the shape (F before S);
   apart (>= 1e-15): when the forward length F -> S is the chord (within 1e-5), or shorter than S -> F: S.x <= F.x, else
   F.x <= S.x; together: the raw start y of the entity before (t < 0.5) or at F lies below the item's left end */
static int gp_item_test(const ShL *G, const GpItem *it) {
    if (it->L.sh != it->R.sh) return 0;
    const Pos *F, *S;
    if (it->L.ent < it->R.ent || (it->L.ent <= it->R.ent && it->L.t <= it->R.t)) { F = &it->L; S = &it->R; } else { F = &it->R; S = &it->L; }
    double dx = F->pt.x - S->pt.x, dy = F->pt.y - S->pt.y, d = sqrt(dy * dy + dx * dx);
    if (1e-15 <= d) {
        double d1 = sec_len(G->s[F->sh], F->ent, F->t, S->ent, S->t);
        double A = S->pt.x, Bx = F->pt.x;
        if (fabs(d1 - d) > 1e-05) { double d2 = sec_len(G->s[S->sh], S->ent, S->t, F->ent, F->t); if (!(d1 < d2)) { A = F->pt.x; Bx = S->pt.x; } }
        return A - Bx <= 0.0;
    }
    const Sh *s = G->s[F->sh]; int e = F->ent;
    if (F->t < 0.5) e = e == 0 ? s->n - 1 : e - 1;
    const Ent *x = &s->e[e];
    double y = x->id == 'L' ? x->p1.y : x->id == 'A' ? sin(x->sa) * x->r + x->c.y : x->id == 'P' ? x->p1.y : 0.0;
    return y < it->L.pt.y;
}
/* FUN_1800fdce0 (l.137628-137743): which side the boundary leaves the end p: backwards (from the entity before when
   t < 0.5) the first entity start differing in y by > 1e-6 gives 1 (above) / 2 (below), forwards (from the next entity
   when t > 0.5) the first end the same; OR of both */
static int gp_end_side(const ShL *G, const Pos *p) {
    const Sh *s = G->s[p->sh]; int r = 0, e = p->ent;
    if (p->t < 0.5) e = e == 0 ? s->n - 1 : e - 1;
    for (int g = 0; g < s->n; g++) {
        const Ent *x = &s->e[e];
        double y = x->id == 'A' ? sin(x->sa) * x->r + x->c.y : gp_raw(x, 0x20);
        if (1e-06 < y - p->pt.y) { r = 1; break; }
        if (y - p->pt.y < -1e-06) { r = 2; break; }
        if (--e < 0) e = s->n - 1;
    }
    e = p->ent;
    if (0.5 < p->t) e = e != s->n - 1 ? e + 1 : 0;
    for (int g = 0; g < s->n; g++) {
        const Ent *x = &s->e[e];
        double y = x->id == 'L' ? x->p2.y : x->id == 'A' ? sin(x->sa + x->sw) * x->r + x->c.y : gp_raw(x, 0x20);
        if (1e-06 < y - p->pt.y) return r | 1;
        if (y - p->pt.y < -1e-06) return r | 2;
        if (++e >= s->n) e = 0;
    }
    return r;
}
/* FUN_1800feab0 (l.138034-138509): the vertex's lists. A (upward cells): items of class 1 with gp_item_test erased; an
   item after a kept one whose left end side is 2 or 3 is merged into its predecessor (right end copied). B (downward
   cells): class-1 items with !gp_item_test erased; merge on side 1 or 3. Then every left end (and the last right end)
   is filed, once, in the link list of the record of the entity it lies on */
static void gp_file_vertex(const ShL *G, const GpItems *items, int vtx, GpRecs *R) {
    GpRec *rec = &R->r[vtx];
    for (int pass = 0; pass < 2; pass++) {
        GpItems *L = pass == 0 ? &rec->A : &rec->B;
        L->n = 0; for (int k = 0; k < items->n; k++) gi_push(L, items->v[k]);
        int erased = 1;
        for (int i = 0; i < L->n; i++) {
            if (L->v[i].cls == 1 && gp_item_test(G, &L->v[i]) == (pass == 0)) { gi_erase(L, i); i--; erased = 1; continue; }
            if (!erased) {
                int c = gp_end_side(G, &L->v[i].L);
                if (pass == 0 ? (c == 2 || c == 3) : (c == 1 || c == 3)) { L->v[i - 1].R = L->v[i].R; gi_erase(L, i); i--; }
            }
            erased = 0;
        }
    }
    if (!items->n) return;   /* the engine reads items[-1] here (l.138381); unreachable for a vertex on G */
    for (int k = 0; k <= items->n; k++) {
        const Pos *b = k < items->n ? &items->v[k].L : &items->v[items->n - 1].R;
        GpRec *t = &R->r[gp_vidx(G, b->sh, b->ent)]; int dup = 0;
        for (int q = 0; q < t->nc && !dup; q++) {
            const GpLink *c = &t->c[q];
            if (c->vtx == vtx && c->p.sh == b->sh && c->p.ent == b->ent && fabs(c->p.t - b->t) < 1e-14 && fabs(c->p.pt.x - b->pt.x) < 1e-14 && fabs(c->p.pt.y - b->pt.y) < 1e-14) dup = 1;
        }
        if (!dup) { if (t->nc == t->capc) { t->capc = t->capc ? t->capc * 2 : 4; t->c = realloc(t->c, t->capc * sizeof(GpLink)); } GpLink l = { vtx, *b }; t->c[t->nc++] = l; }
    }
}
static int gp_lt_link(const void *a, const void *b) { return ((const GpLink *)a)->p.t < ((const GpLink *)b)->p.t; }   /* FUN_1800f8a60: key +0x10 */
/* FUN_1801003d0 (l.138911-139071): one record per vertex (raw entity start +0x18/+0x20, t 0), box of G grown by 0.001,
   bands FUN_180106d60; per vertex FUN_1800fe1f0 + FUN_1800feab0; each link list std::sort'ed by t */
static void gp_records(const ShL *G, GpRecs *R) {
    R->n = gp_nvtx(G); R->r = calloc(R->n ? R->n : 1, sizeof(GpRec));
    GpBands B = { 0 }; gp_bands(G, &B);
    double bb[4]; sl_bbox(G, bb); bb[0] -= 0.001; bb[1] -= 0.001; bb[2] += 0.001; bb[3] += 0.001;   /* GetMinMaxViaSh */
    GpItems items = { 0 }; int v = 0;
    for (int i = 0; i < G->n; i++) for (int k = 0; k < G->s[i]->n; k++, v++) {
        Pos V = { i, k, 0.0, p2(gp_raw(&G->s[i]->e[k], 0x18), gp_raw(&G->s[i]->e[k], 0x20)) };
        gp_vertex_items(bb, G, &B, &V, &items);
        gp_file_vertex(G, &items, v, R);
    }
    for (int i = 0; i < R->n; i++) gp_stdsort(R->r[i].c, R->r[i].nc, sizeof(GpLink), gp_lt_link);
    free(items.v); gp_bands_free(&B);
}
/* FUN_1800ffa20 (l.138600-138654): from link q0 of record r on: the first linked vertex j with a B item (scanned from the
   last) whose right end lies on r's entity; unused -> (j, m); used -> *hit = 1, none */
static void gp_match(const ShL *G, const GpRecs *R, int r, int q0, int *j, int *m, int *hit) {
    for (int q = q0; q < R->r[r].nc; q++) {
        *j = R->r[r].c[q].vtx;
        for (*m = R->r[*j].B.n - 1; *m > -1; (*m)--) {
            const GpItem *it = &R->r[*j].B.v[*m];
            if (gp_vidx(G, it->R.sh, it->R.ent) == r) { if (!it->used) return; *hit = 1; *j = *m = -1; return; }
        }
    }
    *j = *m = -1;
}
/* FUN_1800ffd30 (l.138698-138766): the upper partner of item k of vertex i: up to 3 entities forward from the item's right
   end; on the first, the links after vertex i's own */
static void gp_partner(const ShL *G, const GpRecs *R, int i, int k, int *j, int *m) {
    *j = *m = -1;
    int sh = R->r[i].A.v[k].R.sh, ent = R->r[i].A.v[k].R.ent, hit = 0;
    for (int step = 0; step < 3; step++) {
        int r = gp_vidx(G, sh, ent), q0 = 0;
        if (step == 0) { int q = 0; while (q < R->r[r].nc && R->r[r].c[q].vtx != i) q++; q0 = q + 1; }
        gp_match(G, R, r, q0, j, m, &hit);
        if (hit || *j != -1) return;
        ent = (ent + 1) % G->s[sh]->n;
    }
}
static int gp_lt_trap(const void *a, const void *b) {   /* FUN_1800f8b60: key (+0x20 + +0x80) / 2 */
    const GpTrap *p = a, *q = b; return (p->a.L.pt.y + p->bL.pt.y) * 0.5 < (q->a.L.pt.y + q->bL.pt.y) * 0.5;
}
/* FUN_1801009c0 (l.139133-139292): every unused A item with a partner gives a trapezoid {index, item, partner's right and
   left ends}, both marked used; std::sort by the mid height */
static void gp_traps(const ShL *G, GpTraps *T) {
    T->n = 0;
    GpRecs R = { 0 }; gp_records(G, &R);
    for (int i = 0; i < R.n; i++) for (int k = 0; k < R.r[i].A.n; k++) {
        if (R.r[i].A.v[k].used) continue;
        int j, m; gp_partner(G, &R, i, k, &j, &m);
        if (j < 0) continue;
        GpTrap t; memset(&t, 0, sizeof t); t.idx = T->n; t.a = R.r[i].A.v[k]; t.bR = R.r[j].B.v[m].R; t.bL = R.r[j].B.v[m].L;
        gt_push(T, &t); R.r[i].A.v[k].used = 1; R.r[j].B.v[m].used = 1;
    }
    gp_stdsort(T->v, T->n, sizeof(GpTrap), gp_lt_trap);
    gp_recs_free(&R);
}

/* ---------------------------------------------------------------------------- cell channel (FUN_1800f61a0) */
/* mcce::CalcLnSegLnSegIntersect (l.12820-12897) = gp_segseg */
static int gp_seg_seg(double tol, P2 a, P2 b, P2 c, P2 d) {
    double s, u; P2 x;
    if (!gp_segseg(tol, a, b, c, d, &s, &u, &x)) return 0;
    return 1;
}
/* FUN_1800f6340 (l.133232-133359): nearest point of the trapezoid to p (left side Ls from the top-left by ld, right side
   from the bottom-right Rs by rd, pl / pr the side points at p's height) */
static P2 gp_trap_near(P2 p, P2 Ls, P2 ld, P2 Rs, P2 rd, P2 pl, P2 pr) {
    if (Rs.y <= p.y) {
        if (p.y <= Ls.y) {
            P2 o = Ls, d = ld;
            if ((pl.x + pr.x) * 0.5 <= p.x) { o = Rs; d = rd; }
            double s, u; P2 x;
            if (!ln_ln(o, d, p, p2(d.y, -d.x), &s, &u, &x)) { return pdist2(p, pl) < pdist2(p, pr) ? pl : pr; }
            if (s < 0.0) s = 0.0; else if (1.0 < s) s = 1.0;
            return p2(o.x + d.x * s, o.y + d.y * s);
        }
        if (Ls.x <= p.x) { if (Rs.x + rd.x < p.x) return p2(rd.x + Rs.x, Rs.y + rd.y); return p2(p.x, Ls.y); }
        return Ls;
    }
    if (Ls.x + ld.x <= p.x) { if (Rs.x < p.x) return Rs; return p2(p.x, Rs.y); }
    return p2(Ls.x + ld.x, ld.y + Ls.y);
}
/* FUN_1800f5b80 (l.133026-133162): the trapezoid containing p strictly (else the nearest, first minimum) appended unless it
   is the last one already */
static void gp_trap_at(P2 p, const GpTraps *T, GpTraps *out) {
    int best = -1; double bd = 0;
    for (int k = 0; k < T->n; k++) {
        const GpTrap *t = &T->v[k];
        P2 Ls = t->bL.pt, ld = p2(t->a.L.pt.x - t->bL.pt.x, t->a.L.pt.y - t->bL.pt.y);
        P2 Rs = t->a.R.pt, rd = p2(t->bR.pt.x - t->a.R.pt.x, t->bR.pt.y - t->a.R.pt.y);
        P2 pl, pr;
        if (1e-14 <= fabs(ld.y)) {
            double s = (p.y - Ls.y) / ld.y; pl = p2(Ls.x + s * ld.x, Ls.y + ld.y * s);
            double u = (p.y - Rs.y) / rd.y; pr = p2(Rs.x + u * rd.x, Rs.y + rd.y * u);
        } else {
            pl = p2(t->bL.pt.x <= t->a.L.pt.x ? t->bL.pt.x : t->a.L.pt.x, t->a.L.pt.y);
            pr = p2(t->a.R.pt.x <= t->bR.pt.x ? t->bR.pt.x : t->a.R.pt.x, t->a.L.pt.y);
        }
        if (Rs.y < p.y && p.y < Ls.y && pl.x < p.x && p.x < pr.x) {
            if (out->n && out->v[out->n - 1].idx == t->idx) return;
            gt_push(out, t); return;
        }
        P2 q = gp_trap_near(p, Ls, ld, Rs, rd, pl, pr);
        double d = (p.y - q.y) * (p.y - q.y) + (p.x - q.x) * (p.x - q.x);
        if (best == -1 || d < bd) { best = k; bd = d; }
    }
    if (best != -1 && (!out->n || out->v[out->n - 1].idx != T->v[best].idx)) gt_push(out, &T->v[best]);
}
/* FUN_1800f7440 (l.133812-133949): may trapezoid i follow the channel's last one: same index -> no; sharing its line
   (|dy| < 1e-7) with overlapping x ranges -> yes; else when i's near line is the last one's far line height, a trapezoid
   found from i on against the direction that overlaps both is appended first -> yes; else no */
static int gp_chan_ok(int up, int i, const GpTraps *T, GpTraps *out) {
    if (!out->n) return 1;
    const GpTrap *L = &out->v[out->n - 1], *t = &T->v[i];
    if (L->idx == t->idx) return 0;
    P2 a, b, c; double ar, br, cr;
    if (!up) { a = t->bL.pt; ar = t->bR.pt.x; b = L->a.L.pt; br = L->a.R.pt.x; c = L->bL.pt; cr = L->bR.pt.x; }
    else { a = t->a.L.pt; ar = t->a.R.pt.x; b = L->bL.pt; br = L->bR.pt.x; c = L->a.L.pt; cr = L->a.R.pt.x; }
    if (!(fabs(a.y - b.y) >= 1e-07 || ar <= b.x || br <= a.x)) return 1;
    if (!(fabs(a.y - c.y) <= 1e-07)) return 0;
    int st = up ? -1 : 1;
    for (int k = i + st; k > -1 && k < T->n; k += st) {
        const GpTrap *q = &T->v[k]; P2 kp; double kr;
        if (!up) { kp = q->a.L.pt; kr = q->a.R.pt.x; } else { kp = q->bL.pt; kr = q->bR.pt.x; }
        if (fabs(a.y - kp.y) < 1e-07 && kp.x < ar && a.x < kr && kp.x < cr && c.x < kr && q->idx != L->idx) { gt_push(out, q); return 1; }
    }
    return 0;
}
static int gp_lt_int(const void *a, const void *b) { return *(const int *)a < *(const int *)b; }
/* FUN_1800f61a0 (l.133165-133187): channel of the route's first shape through the trapezoids: FUN_1800f6d10
   (l.133612-133809) - the trapezoid at the start, per route entity the trapezoids whose mid line it crosses (scanned up or
   down by mid height from its start, until past its end), the one at the end; FUN_1800f6700 (l.133394-133450) drops
   trapezoids while their summed height stays below 1e-8; FUN_1800f68c0 (l.133453-133609) cuts loops: at a trapezoid seen
   before (count from the initial sorted index list, which is not updated) everything after its previous occurrence up to
   it is erased */
static void gp_channel(const ShL *route, const GpTraps *T, GpTraps *out) {
    out->n = 0;
    if (!route->n || !route->s[0]->n) return;
    const Sh *r = route->s[0];
    gp_trap_at(p2(gp_raw(&r->e[0], 0x18), gp_raw(&r->e[0], 0x20)), T, out);
    for (int k = 0; k < r->n; k++) {
        const Ent *e = &r->e[k];
        double y1 = gp_raw(e, 0x20), y2 = gp_raw(e, 0x30);
        int up = y1 <= y2 && y1 != y2;
        P2 f, g;   /* the segment: up P1 -> P2, else P2 -> P1 */
        if (!up) { f = p2(gp_raw(e, 0x28), y2); g = p2(gp_raw(e, 0x18), y1); } else { f = p2(gp_raw(e, 0x18), y1); g = p2(gp_raw(e, 0x28), y2); }
        double key = !up ? ((g.y + 1e-07) + (g.y + 1e-07)) * 0.5 : ((f.y - 1e-07) + (f.y - 1e-07)) * 0.5;
        int lo = 0, c = T->n;
        while (c > 0) { int h = c / 2; if ((T->v[lo + h].bL.pt.y + T->v[lo + h].a.L.pt.y) * 0.5 < key) { lo += h + 1; c -= h + 1; } else c = h; }
        int i = lo; if (i >= T->n) i = T->n - 1;
        while (i > -1 && i < T->n) {
            const GpTrap *t = &T->v[i];
            P2 M1 = p2((t->bL.pt.x - t->a.L.pt.x) * 0.5 + t->a.L.pt.x, (t->bL.pt.y - t->a.L.pt.y) * 0.5 + t->a.L.pt.y);
            P2 M2 = p2(t->a.R.pt.x + (t->bR.pt.x - t->a.R.pt.x) * 0.5, t->a.R.pt.y + (t->bR.pt.y - t->a.R.pt.y) * 0.5);
            if (gp_seg_seg(1e-05, f, g, M1, M2) && gp_chan_ok(up, i, T, out)) gt_push(out, t);
            if (!up) { double lim = f.y - 1e-07; if (!(lim < M1.y) && M1.y != lim) break; }
            else { double lim = g.y + 1e-07; if (!(M1.y < lim) && M1.y != lim) break; }
            i += up ? 1 : -1;
        }
    }
    const Ent *z = &r->e[r->n - 1];
    gp_trap_at(p2(gp_raw(z, 0x28), gp_raw(z, 0x30)), T, out);
    /* FUN_1800f6700 */
    double acc = 0;
    for (int k = 0; k < out->n; k++) { acc += out->v[k].bL.pt.y - out->v[k].a.L.pt.y; if (1e-08 <= acc) acc = 0; else { gt_erase(out, k, 1); k--; } }
    /* FUN_1800f68c0 */
    int n0 = out->n; int *ids = malloc((size_t)(n0 > 0 ? n0 : 1) * sizeof(int));
    for (int k = 0; k < n0; k++) ids[k] = out->v[k].idx;
    gp_stdsort(ids, n0, sizeof(int), gp_lt_int);
    for (int k = 0; k < out->n; k++) {
        int lo = 0, c = n0;
        while (c > 0) { int h = c / 2; if (ids[lo + h] < out->v[k].idx) { lo += h + 1; c -= h + 1; } else c = h; }
        if (lo < n0 - 1 && ids[lo] == ids[lo + 1]) {
            int j = k - 1; while (j > -1 && out->v[j].idx != out->v[k].idx) j--;
            if (j > -1) { gt_erase(out, j + 1, k - j); k = j; }
        }
    }
    free(ids);
}

/* ---------------------------------------------------------------------------- search (FUN_1800eda20, l.128580-128621) */
/* CHbt (0x28): +0x00 y, +0x08 / +0x10 the window on that line (stored high / low when flag = 0), +0x18 x of the path
   point, +0x20 flag (1 = the channel goes up there), +0x21 fixed */
typedef struct { double y, lo, hi, x; int flag, fixed; } GpHb;
/* FUN_1800edc30 (l.128654-128829): one record per channel boundary plus the route ends (fixed). Between cells k-1 and k
   the shared line is the nearest of |prev top - cur bottom| (0: up, flag 1, window = prev's top / cur's bottom),
   |prev bottom - cur top| (1: down, flag 0), |last y - cur bottom| (2), |last y - cur top| (3; 2 and 3 keep the last
   flag and window); window = [max of the left ends, min of the right ends], swapped when flag = 0 */
static GpHb *gp_hbt(const Sh *r, const GpTraps *C, int *nh) {
    int n = C->n + 1; GpHb *H = calloc(n, sizeof(GpHb)); *nh = n;
    H[0].y = gp_raw(&r->e[0], 0x20); H[0].lo = H[0].hi = H[0].x = gp_raw(&r->e[0], 0x18); H[0].fixed = 1;
    int k = 1;
    for (; k < n - 1; k++) {
        const GpTrap *pv = &C->v[k - 1], *cu = &C->v[k];
        double top = cu->bL.pt.y, ly = H[k - 1].y, d[3];
        d[0] = fabs(pv->bL.pt.y - cu->a.L.pt.y); d[1] = fabs(pv->a.L.pt.y - top); d[2] = fabs(ly - cu->a.L.pt.y);
        int c = d[1] < d[0]; if (d[2] < d[c]) c = 2;
        if (fabs(ly - top) < d[c]) c = 3;
        double e8, f8; P2 D; double R;
        if (c == 0) { H[k].flag = 1; e8 = pv->bL.pt.x; f8 = pv->bR.pt.x; }
        else if (c == 1) { H[k].flag = 0; e8 = pv->a.L.pt.x; f8 = pv->a.R.pt.x; }
        else { H[k].flag = H[k - 1].flag; e8 = H[k - 1].lo; f8 = H[k - 1].hi; if (!H[k - 1].flag) { double t = e8; e8 = f8; f8 = t; } }
        if (c == 1 || c == 3) { D = cu->bL.pt; R = cu->bR.pt.x; } else { D = cu->a.L.pt; R = cu->a.R.pt.x; }
        H[k].y = D.y; H[k].lo = e8 <= D.x ? D.x : e8; H[k].hi = R <= f8 ? R : f8;
        if (!H[k].flag) { double t = H[k].lo; H[k].lo = H[k].hi; H[k].hi = t; }
    }
    const Ent *z = &r->e[r->n - 1];
    H[k].flag = H[k - 1].flag; H[k].y = gp_raw(z, 0x30); H[k].lo = H[k].hi = H[k].x = gp_raw(z, 0x28); H[k].fixed = 1;
    if (n > 1) H[0].flag = H[1].flag;
    return H;
}
/* FUN_1800edb70 (l.128624-128651): the window end of record i nearer to the middle of record j's window (+0x10 on ties);
   j out of range: +0x08 */
static double gp_win_end(int i, int j, const GpHb *H, int n) {
    if (j < 0 || n <= j) return H[i].lo;
    double m = (H[j].hi + H[j].lo) * 0.5;
    return fabs(m - H[i].hi) <= fabs(m - H[i].lo) ? H[i].hi : H[i].lo;
}
/* FUN_1800ed5b0 (l.128468-128577): from fixed record i towards the next fixed one: of the records between, the window
   end (+0x08 side 0 / +0x10 side 1) turning most (strictly) to the right (side 0: angle < 0) / left (side 1: > 0) of the
   direction becomes fixed there; returns its index or -1 */
static int gp_pull(int side, int i, GpHb *H, int n) {
    double x0 = H[i].x, y0 = H[i].y; int k = i + 1;
    while (k < n && !H[k].fixed) k++;
    double dx = H[k].x - x0, dy = H[k].y - y0;
    if (!(1e-12 < fabs(dx) || 1e-12 < fabs(dy))) return -1;
    double dir = atan2(dy, dx), best = -1.0; int r = -1;
    for (int m = i + 1; m < k; m++) {
        double vx = (side == 0 ? H[m].lo : H[m].hi) - x0, vy = H[m].y - y0;
        if (!(1e-12 < fabs(vx) || 1e-12 < fabs(vy))) continue;
        double a = atan2(vy, vx) - dir;
        if (-3.141592653589793 <= a) { if (3.141592653589793 < a) a -= 6.283185307179586; } else a += 6.283185307179586;
        if (side == 0 ? a < 0.0 : 0.0 < a) { if (best < fabs(a)) { best = fabs(a); r = m; } }
    }
    if (r != -1) { H[r].x = side == 0 ? H[r].lo : H[r].hi; H[r].fixed = 1; }
    return r;
}
/* FUN_1800ed0f0 (l.128309-128349): pull both sides until neither moves, then on to the next fixed record, up to b */
static void gp_pull_run(int a, int b, GpHb *H, int n) {
    int u = a;
    for (;;) {
        int r1, r2;
        do { r1 = gp_pull(1, u, H, n); r2 = gp_pull(0, u, H, n); } while (r1 != -1 || r2 != -1);
        u++;
        while (u < n && !H[u].fixed) u++;
        if (b <= u) return;
    }
}
/* FUN_1800ee2e0 (l.128832-128881): per run of equal flags [i0, i1]: both ends fixed at the window end facing the
   neighbouring run (FUN_1800edb70), then pulled taut (FUN_1800ed0f0) */
static void gp_funnel(GpHb *H, int n) {
    int prev = -1;
    for (;;) {
        int i0 = prev + 1, i1 = i0;
        if (i0 < n) while (i1 < n && H[i1].flag == H[i0].flag) i1++;
        int last = i1 - 1;
        if (last < i0) return;
        H[i0].x = gp_win_end(i0, prev, H, n); H[i0].fixed = 1;
        H[last].x = gp_win_end(last, i1, H, n); H[last].fixed = 1;
        gp_pull_run(i0, last, H, n);
        prev = last;
    }
}
/* FUN_1800ed200 (l.128352-128465): one open shape of lines through the fixed records (from record 0), a point within
   sqrt(9.999999999999998e-15) of the last one skipped */
static void gp_hbt_path(const GpHb *H, int n, ShL *out) {
    if (n < 2) return;
    Sh *s = sh_new(); sl_push(out, s);
    for (int k = 1; k < n; k++) {
        if (!H[k].fixed) continue;
        P2 a = s->n ? s->e[s->n - 1].p2 : p2(H[0].x, H[0].y), b = p2(H[k].x, H[k].y);
        if (9.999999999999998e-15 < (a.y - b.y) * (a.y - b.y) + (a.x - b.x) * (a.x - b.x)) sh_push(s, ent_line(a, b));
    }
    s->dir = 0; gp_upd_minmax(s);
}
/* FUN_1800eda20: (cells non-empty) FUN_1800edc30, FUN_1800ee2e0, FUN_1800ed200 */
static void gp_search(const ShL *route, const GpTraps *C, ShL *out) {
    if (!C->n || !route->n || !route->s[0]->n) return;
    int n; GpHb *H = gp_hbt(route->s[0], C, &n);
    for (int pass = 0; pass < 2 && g_gp_f; pass++) {   /* ours: unit-oracle dump of the CHbt records before / after the funnel */
        if (pass) gp_funnel(H, n);
        fprintf(g_gp_f, "stage %s %d\n", pass ? "funnel" : "hbt", n);
        for (int k = 0; k < n; k++) fprintf(g_gp_f, "h %.17g %.17g %.17g %.17g %d %d\n", H[k].y, H[k].lo, H[k].hi, H[k].x, H[k].flag, H[k].fixed);
    }
    if (!g_gp_f) gp_funnel(H, n);
    gp_hbt_path(H, n, out); free(H);
}

/* FUN_1800f1580 (l.130763-130887): the plain route of the line E -> A rotated by a through set (FUN_18010c040), first
   shape kept (the off-line length output is not used here) */
static void gp_plain_route(int both, P2 E, P2 A, double a, const ShL *set, ShL *out) {
    Ent ln = ent_line(gp_rot(E, a), gp_rot(A, a)); route(both, set, &ln, out);
    while (out->n > 1) sl_erase(out, out->n - 1);
}
/* ours: planner unit oracle (tools/peloader --gp, tools/gp_cmp.py). MCCE_GPDUMP=PREFIX: each guided_plan call writes
   its input to PREFIX.N ("GPIN 1": d, step, both, E, the exit end tangent E+0x10, A, the approach start tangent A+0x10, the group G, exact %a) and the
   result of each ported stage to PREFIX.N.port, in the format the loader prints for the DLL's stages */
static _Thread_local int g_gp_n;
static void gp_dump_ent(FILE *f, int exact, const Ent *e) {
    if (e->id == 'A') fprintf(f, exact ? "A %a %a %a %a %a\n" : " A %.17g %.17g %.17g %.17g %.17g\n", e->c.x, e->c.y, e->r, e->sa, e->sw);
    else fprintf(f, exact ? "%c %a %a %a %a\n" : " %c %.17g %.17g %.17g %.17g\n", e->id ? e->id : '?', e->p1.x, e->p1.y, e->p2.x, e->p2.y);
}
static void gp_dump_sl(const char *stage, const ShL *G) {
    if (!g_gp_f) return;
    fprintf(g_gp_f, "stage %s %d\n", stage, G->n);
    for (int i = 0; i < G->n; i++) {
        const Sh *s = G->s[i]; fprintf(g_gp_f, "s %d dir %d isl %d n %d box %.17g %.17g %.17g %.17g\n", i, s->dir, s->fl & 1, s->n, s->bb[0], s->bb[1], s->bb[2], s->bb[3]);
        for (int k = 0; k < s->n; k++) gp_dump_ent(g_gp_f, 0, &s->e[k]);
    }
}
static void gp_dump_traps(const char *stage, const GpTraps *T) {
    if (!g_gp_f) return;
    fprintf(g_gp_f, "stage %s %d\n", stage, T->n);
    for (int k = 0; k < T->n; k++) { const GpTrap *t = &T->v[k];
        fprintf(g_gp_f, "t %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n", t->idx, t->a.L.pt.x, t->a.L.pt.y, t->a.R.pt.x, t->a.R.pt.y,
                t->bL.pt.x, t->bL.pt.y, t->bR.pt.x, t->bR.pt.y); }
}
static void gp_dump_open(double d, double step, int both, P2 E, P2 Ed, P2 A, P2 Ad, const ShL *G) {
    const char *pre = getenv("MCCE_GPDUMP");
    if (!pre || !*pre) return;
    char path[4096]; snprintf(path, sizeof path, "%s.%d", pre, g_gp_n++);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "GPIN 1\nd %a\nstep %a\nboth %d\nE %a %a\nEd %a %a\nA %a %a\nAd %a %a\n", d, step, both, E.x, E.y, Ed.x, Ed.y, A.x, A.y, Ad.x, Ad.y);
    for (int i = 0; i < G->n; i++) {
        const Sh *s = G->s[i]; fprintf(f, "s %d %d %d\n", s->dir, s->fl & 1, s->n);
        for (int k = 0; k < s->n; k++) gp_dump_ent(f, 1, &s->e[k]);
    }
    fprintf(f, "end\n"); fclose(f);
    strncat(path, ".port", sizeof path - strlen(path) - 1);
    g_gp_f = fopen(path, "w");
}
/* ours (unit oracle): with MCCE_GPFEED=1 the loader's --gp output for this call (PREFIX.N.dll, PREFIX = MCCE_GPDUMP)
   replaces G by the DLL's result of `stage`, so the stages after it run on Mastercam's own input (shapes
   "s i dir D isl I n N" + entities, %.17g) */
static void gp_feed(const char *stage, ShL *G) {
    const char *fe = getenv("MCCE_GPFEED"), *pre = getenv("MCCE_GPDUMP");
    if (!fe || *fe != '1' || !pre || !*pre) return;
    char path[4096]; snprintf(path, sizeof path, "%s.%d.dll", pre, g_gp_n - 1);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512], want[64]; int found = 0; snprintf(want, sizeof want, "stage %s ", stage);
    while (fgets(line, sizeof line, f)) if (!strncmp(line, want, strlen(want))) { found = 1; break; }
    if (found) {
        int ns = atoi(line + strlen(want));
        sl_free(G);
        for (int i = 0; i < ns; i++) {
            int si, dir, isl, n; Sh *s = sh_new();
            if (!fgets(line, sizeof line, f) || sscanf(line, "s %d dir %d isl %d n %d", &si, &dir, &isl, &n) != 4) { free(s); break; }
            s->dir = dir; s->fl = isl;
            for (int k = 0; k < n && fgets(line, sizeof line, f); k++) {
                char *p = line; while (*p == ' ') p++;
                char id = *p++; double v[5] = { 0 };
                for (int c = 0; c < (id == 'A' ? 5 : 4); c++) v[c] = strtod(p, &p);
                sh_push(s, id == 'A' ? ent_arc(p2(v[0], v[1]), v[2], v[3], v[4]) : ent_line(p2(v[0], v[1]), p2(v[2], v[3])));
            }
            gp_upd_minmax(s); sl_push(G, s);
        }
        if (g_gp_f) fprintf(g_gp_f, "# fed stage %s from %s\n", stage, path);
    }
    fclose(f);
}
/* ---------------------------------------------------------------- post-process FUN_180136370 (§6.16 "Pass 5") */
static int poly_roots2(const double c0[3], double *out);   /* FUN_1800afd30 (mcce_medax.c) */
static P2 cs_near_seg(P2 a, P2 b, P2 q);                    /* FUN_1800ad500 (below) */
typedef struct { P2 p, d; } GpLn;                           /* the {CPt2 point, CPt2 direction} line of CalcLnLnIntersectPt */
static int gp_ln_x(GpLn a, GpLn b, double *s, double *u, P2 *x) { double s0, u0; return gp_lnln(a.p, a.d, b.p, b.d, s ? s : &s0, u ? u : &u0, x); }
static int gp_big(double x, double y) { return 1e-12 < fabs(x) || 1e-12 < fabs(y); }   /* `1e-12 <= |v| && |v| != 1e-12` on either */
static double gp_wrap1(double a) { if (a < -3.141592653589793) a += 6.283185307179586; else if (3.141592653589793 < a) a -= 6.283185307179586; return a; }
static int gp_sgn(double v) { return v <= 0.0 ? (0.0 <= v) - 1 : 1; }
/* the start / end point the post-process reads: 'A' c + r (cos, sin)(sa (+ sw)); 'L' p1 / p2; other types the raw +0x18 */
static P2 gp_pp_start(const Ent *e) { return e->id == 'A' ? gp_raw_start(e) : e->p1; }
static P2 gp_pp_end(const Ent *e) { return e->id == 'L' || e->id == 'A' ? gp_raw_end(e) : e->p1; }
/* FUN_18013a3e0 (l.168776-168914): up to `cnt` more points walking from entity k by dir (-1: entity starts, +1: ends), a
   point within tol (strict, per coordinate) of a collected one skipped; closed shapes wrap; at most n steps */
static void gp_pp_walk(const Sh *s, int k, int cnt, int dir, double tol, P2 *pts, int *np) {
    int target = *np + cnt;
    if (*np >= target) return;
    for (int it = 0; k >= 0 && k < s->n && it < s->n; it++) {
        P2 q = dir < 0 ? gp_pp_start(&s->e[k]) : gp_pp_end(&s->e[k]);
        int dup = 0;
        for (int m = 0; m < *np; m++) if (fabs(pts[m].x - q.x) < tol && fabs(pts[m].y - q.y) < tol) { dup = 1; break; }
        if (!dup) { pts[(*np)++] = q; if (target <= *np) return; }
        k += dir < 0 ? -1 : 1;
        if (s->dir != 0) { if (k < 0) k = s->n - 1; else if (s->n <= k) k = 0; }
    }
}
/* FUN_18013a020 (l.168636-168775): the start of entity k (k >= n: the end of the last entity, k := n - 1), nb points
   backwards (reversed into path order), then na points forwards */
static int gp_pp_pts(const Sh *s, int k, int nb, int na, double tol, P2 *pts) {
    int np = 0;
    if (k < s->n) pts[np++] = gp_pp_start(&s->e[k]);
    else { k = s->n - 1; pts[np++] = gp_pp_end(&s->e[k]); }
    gp_pp_walk(s, k, nb, -1, tol, pts, &np);
    for (int i = 0; i < np / 2; i++) { P2 t = pts[i]; pts[i] = pts[np - 1 - i]; pts[np - 1 - i] = t; }
    gp_pp_walk(s, k, na, 1, tol, pts, &np);
    return np;
}
/* FUN_1800a7310 (l.91245-91340): the arc p0 -> p2 about the centre of p0 p1 p2 (crossing of the perpendicular bisectors);
   sweep atan2(p2 - c) - atan2(p0 - c) wrapped once to [-pi, pi] */
static int gp_arc3(const P2 p[3], Ent *a) {
    GpLn b1 = { p2((p[0].x + p[1].x) * 0.5, (p[0].y + p[1].y) * 0.5), p2(p[1].y - p[0].y, -(p[1].x - p[0].x)) };
    if (!(1e-12 <= fabs(b1.d.x) || 1e-12 <= fabs(b1.d.y))) return 0;
    GpLn b2 = { p2((p[1].x + p[2].x) * 0.5, (p[1].y + p[2].y) * 0.5), p2(p[2].y - p[1].y, -(p[2].x - p[1].x)) };
    if (!(1e-12 <= fabs(b2.d.x) || 1e-12 <= fabs(b2.d.y))) return 0;
    P2 c; if (!gp_ln_x(b1, b2, NULL, NULL, &c)) return 0;
    double vx = p[0].x - c.x, vy = p[0].y - c.y;
    if (!gp_big(vx, vy)) return 0;
    double sa = atan2(vy, vx), wx = p[2].x - c.x, wy = p[2].y - c.y;
    if (!gp_big(wx, wy)) return 0;
    double sw = gp_wrap1(atan2(wy, wx) - sa);
    *a = ent_arc(c, sqrt((c.x - p[0].x) * (c.x - p[0].x) + (c.y - p[0].y) * (c.y - p[0].y)), sa, sw);
    return 1;
}
/* FUN_1800a6f70 (l.91127-91221): the arc from p0 (tangent T there) to p1: centre = the normal at p0 x the perpendicular
   bisector of p0 p1; sweep wrapped once to [-pi, pi]. With r: FUN_1800a8c00 (l.91930-91985), the radius only */
static int gp_arc_tp(P2 T, P2 p0, P2 p1, Ent *a, double *r) {
    GpLn n = { p0, p2(T.y, -T.x) };
    if (!(1e-12 <= fabs(n.d.x) || 1e-12 <= fabs(n.d.y))) return 0;
    GpLn b = { p2((p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5), p2(p1.y - p0.y, -(p1.x - p0.x)) };
    if (!(1e-12 <= fabs(b.d.x) || 1e-12 <= fabs(b.d.y))) return 0;
    P2 c; if (!gp_ln_x(n, b, NULL, NULL, &c)) return 0;
    double rr = sqrt((c.y - p0.y) * (c.y - p0.y) + (c.x - p0.x) * (c.x - p0.x));
    if (r) { *r = rr; return 1; }
    double vx = p0.x - c.x, vy = p0.y - c.y;
    if (!gp_big(vx, vy)) return 0;
    double sa = atan2(vy, vx), wx = p1.x - c.x, wy = p1.y - c.y;
    if (!gp_big(wx, wy)) return 0;
    *a = ent_arc(c, rr, sa, gp_wrap1(atan2(wy, wx) - sa));
    return 1;
}
/* FUN_1800a8660 (l.91740-91929): the arc from S (tangent Ts) tangent to the line (D, Tl): X = the crossing of the two
   (unit) lines, the centre on the normal at S and the bisector at X, its side from the corner's turn */
static int gp_arc_tt(P2 S, P2 Ts, P2 D, P2 Tl, Ent *a) {
    double l = sqrt(Ts.x * Ts.x + Ts.y * Ts.y);
    if (l < 1e-12) return 0;
    P2 u = p2(Ts.x / l, Ts.y / l);
    double m = sqrt(Tl.x * Tl.x + Tl.y * Tl.y);
    if (m < 1e-12) return 0;
    P2 v = p2(Tl.x / m, Tl.y / m), X;
    if (!gp_ln_x((GpLn){ S, u }, (GpLn){ D, v }, NULL, NULL, &X)) return 0;
    double ay = X.y - u.y, by = X.y + v.y, bx = X.x + v.x, ax = X.x - u.x;
    GpLn bis = { X, p2((bx + ax) * 0.5 - X.x, (by + ay) * 0.5 - X.y) };
    int sg = gp_sgn((X.x - ax) * (by - ay) - (bx - ax) * (X.y - ay));
    P2 n = sg == 1 ? p2(-u.y, u.x) : p2(u.y, -u.x);
    P2 c; if (!gp_ln_x((GpLn){ S, n }, bis, NULL, NULL, &c)) return 0;
    double r = sqrt((S.y - c.y) * (S.y - c.y) + (S.x - c.x) * (S.x - c.x));
    if (!gp_big(-n.x, -n.y)) return 0;
    double sa = atan2(-n.y, -n.x);
    P2 w = sg == 1 ? p2(v.x, -v.y) : p2(-v.x, v.y);   /* end direction atan2(-w.x, -w.y) */
    if (!gp_big(-w.y, -w.x)) return 0;
    double sw = gp_wrap1(atan2(-w.x, -w.y) - sa);
    if (sg == 1) { if (sw < 0.0) sw += 6.283185307179586; }
    else if (sg == -1 && 0.0 < sw) sw -= 6.283185307179586;
    *a = ent_arc(c, r, sa, sw);
    return 1;
}
/* FUN_18013a8e0 (l.168915-168969): every chord midpoint of pts within d of the arc's circle (radially) */
static int gp_arc_fits(double d, const P2 *pts, int np, const Ent *a) {
    for (int k = 1; k < np; k++) {
        double mx = (pts[k].x + pts[k - 1].x) * 0.5, my = (pts[k].y + pts[k - 1].y) * 0.5, dx = mx - a->c.x, dy = my - a->c.y, dist;
        if (1e-12 < fabs(dx) || 1e-12 < fabs(dy)) {
            double g = atan2(dy, dx), ex = mx - (cos(g) * a->r + a->c.x), ey = my - (sin(g) * a->r + a->c.y);
            dist = sqrt(ey * ey + ex * ex);
        } else dist = a->r;
        if (d < dist) return 0;
    }
    return 1;
}
static P2 gp_arc_end_dir(const Ent *a) { double g = a->sw + a->sa, c = cos(g), s = sin(g); return a->sw <= 0.0 ? p2(s, -c) : p2(-s, c); }
static int gp_unit(P2 *t) { double l = sqrt(t->x * t->x + t->y * t->y); if (l < 1e-12) return 0; t->x /= l; t->y /= l; return 1; }
/* FUN_1801380f0 (l.167624-167757): the tangent at the start of entity i: out empty -> Ed, else the tangent arriving at the
   end of out's last entity (FUN_18011ffd0, 1e-10); normalised. (Out empty without Ed - the 3-point arc of the path's
   first points - does not occur in the planner: Ed is always given; not ported.) */
static int gp_pp_tan_i(const P2 *Ed, const Sh *o, P2 *t) {
    *t = o->n ? gp_tan_in(o, o->n - 1, 1.0, 1.0000000000000002e-10) : *Ed;
    return gp_unit(t);
}
/* FUN_180138510 (l.167758-167878): the tangent at path vertex j: closed path, j == n and out non-empty -> out's start
   tangent (FUN_1801204a0); else the points up to j (2 back, FUN_18013a020): 3 points -> the end tangent of their arc
   (FUN_1800a7310) when it fits within d (FUN_18013a8e0); 2 points and Ed -> the end tangent of the arc tangent to Ed
   (FUN_1800a6f70) when it fits; else the last chord; normalised */
static int gp_pp_tan_j(double d, const P2 *Ed, const Sh *p, const Sh *o, int j, P2 *t) {
    *t = p2(0, 0);
    if (p->dir != 0 && j == p->n && o->n) { *t = gp_tan_out(o, 0, 0.0, 1.0000000000000002e-10); return gp_unit(t); }
    P2 pts[3]; int np = gp_pp_pts(p, j, 2, 0, 1e-05, pts); Ent a;
    if ((np == 3 && gp_arc3(pts, &a) && gp_arc_fits(d, pts, np, &a)) || (np == 2 && Ed && gp_arc_tp(*Ed, pts[0], pts[1], &a, NULL) && gp_arc_fits(d, pts, np, &a))) {
        *t = gp_arc_end_dir(&a); return 1;
    }
    if (np < 2) return 0;
    *t = p2(pts[np - 1].x - pts[np - 2].x, pts[np - 1].y - pts[np - 2].y);
    return gp_unit(t);
}
/* FUN_180137b70 (l.167442-167623): P = start of entity i, Q = start of j (end of j - 1 at j == n); X = (P, ti) x (Q, tj)
   ahead of P and behind Q; C = the crossing of the bisector at X with the normal at P or at Q, whichever comes first;
   L1 = (P, X - P), L2 = (Q, Q - X), L3 = (X, C - X) */
static int gp_pp_corner(P2 ti, P2 tj, int i, int j, const Sh *p, GpLn *L1, GpLn *L2, GpLn *L3) {
    L1->p = gp_pp_start(&p->e[i]); L1->d = ti;
    L2->p = j < p->n ? gp_pp_start(&p->e[j]) : gp_pp_end(&p->e[j - 1]); L2->d = tj;
    double s, u; P2 X;
    if (!gp_ln_x(*L1, *L2, &s, &u, &X) || !(0.0 <= s) || !(u <= 0.0)) return 0;
    L3->p = X; L3->d = p2(((X.x + tj.x) + (X.x - ti.x)) * 0.5 - X.x, ((X.y + tj.y) + (X.y - ti.y)) * 0.5 - X.y);   /* disasm 0x180137e18-0x180137e61 */
    double e8, e0; P2 b8, a8;
    if (!gp_ln_x(*L3, (GpLn){ L1->p, p2(L1->d.y, -L1->d.x) }, &e8, NULL, &b8) || !(0.0 <= e8)) return 0;
    if (!gp_ln_x(*L3, (GpLn){ L2->p, p2(L2->d.y, -L2->d.x) }, &e0, NULL, &a8) || !(0.0 <= e0)) return 0;
    P2 C = e0 <= e8 ? a8 : b8;
    L3->d = p2(C.x - X.x, C.y - X.y);
    L1->d = p2(X.x - L1->p.x, X.y - L1->p.y);
    L2->d = p2(L2->p.x - X.x, L2->p.y - X.y);
    return 1;
}
/* FUN_180139a40 (l.168467-168635): the path vertices i+1 .. j-1 within d of the polyline P X Q are dropped from both ends
   (P - X from the front, X - Q from the back; the nearest to X remembered); the first far one must lie on the inner
   side; the range [r0, r1] is left; all dropped -> the remembered vertex */
static int gp_pp_range(double d, int i, int j, const Sh *p, const GpLn *L1, const GpLn *L2, const GpLn *L3, int r[2]) {
    P2 P = L1->p, Q = L2->p, X = L3->p;
    int sg = gp_sgn((X.x - P.x) * (Q.y - P.y) - (X.y - P.y) * (Q.x - P.x));
    if (sg != 1 && sg != -1) return 0;
    double best = 3.4028234663852886e+38, cur = 3.4028234663852886e+38; int bi = -1;
    r[0] = i + 1; r[1] = j - 1;
    while (r[0] <= r[1]) {
        P2 V = gp_pp_start(&p->e[r[0]]);
        double dv = (V.y - X.y) * (V.y - X.y) + (V.x - X.x) * (V.x - X.x);
        best = cur; if (dv < cur) { bi = r[0]; best = dv; }
        P2 f = cs_near_seg(P, X, V);
        if (d * d <= (V.y - f.y) * (V.y - f.y) + (V.x - f.x) * (V.x - f.x)) {
            if (gp_sgn((f.x - P.x) * (V.y - P.y) - (V.x - P.x) * (f.y - P.y)) != sg) return 0;
            break;
        }
        r[0]++; cur = best;
    }
    while (r[0] <= r[1]) {
        P2 V = gp_pp_start(&p->e[r[1]]);
        double dv = (V.y - X.y) * (V.y - X.y) + (V.x - X.x) * (V.x - X.x);
        if (dv < best) { bi = r[0]; best = dv; }   /* r0, not r1 (disasm 0x180139dd2: mov r12d, [rbx]) */
        P2 f = cs_near_seg(X, Q, V);
        if (d * d <= (V.y - f.y) * (V.y - f.y) + (V.x - f.x) * (V.x - f.x)) {
            double cr = (f.x - Q.x) * (V.y - Q.y) - (V.x - Q.x) * (f.y - Q.y);
            int s2 = 1; if (cr <= 0.0) s2 = (0.0 <= cr) - 1;
            if (s2 == sg) return 0;
            break;
        }
        r[1]--;
    }
    if (r[1] < r[0] && bi != -1) r[0] = r[1] = bi;
    return 1;
}
/* FUN_180137a60 (l.167404-167441): distance of C = X + L3.d from the line L1 (-1: parallel) */
static double gp_pp_rad(const GpLn *L1, const GpLn *L3) {
    P2 C = p2(L3->d.x + L3->p.x, L3->d.y + L3->p.y), F;
    if (!gp_ln_x(*L1, (GpLn){ C, p2(-L1->d.y, L1->d.x) }, NULL, NULL, &F)) return -1.0;
    return sqrt((F.y - C.y) * (F.y - C.y) + (F.x - C.x) * (F.x - C.x));
}
/* FUN_180137090 (l.167152-167183): the scale t of the corner circle (centre X + t L3.d, radius t R) through V: the largest
   root of |X - V + t L3.d|^2 = t^2 R^2 (FUN_1800afd30) */
static int gp_pp_scale(double R2, const GpLn *L3, P2 V, double *t) {
    double ax = L3->p.x - V.x, ay = L3->p.y - V.y, c[3], x[4];
    c[0] = ay * ay + ax * ax; c[1] = (L3->d.x * ax + L3->d.y * ay) * 2.0; c[2] = (L3->d.x * L3->d.x + L3->d.y * L3->d.y) - R2;
    int n = poly_roots2(c, x);
    if (n) *t = x[n - 1];
    return n != 0;
}
/* FUN_180137880 (l.167362-167403): the scale whose circle passes 0.99 d from X along L3, clamped to [0, 1] (-1: none) */
static double gp_pp_tmin(double d, double R2, const GpLn *L3) {
    double l = sqrt(L3->d.y * L3->d.y + L3->d.x * L3->d.x), t = -1.0;
    if (l < 1e-12) return -1.0;
    P2 V = p2(d * 0.99 * (L3->d.x / l) + L3->p.x, d * 0.99 * (L3->d.y / l) + L3->p.y);
    if (!gp_pp_scale(R2, L3, V, &t)) return -1.0;
    return t < 0.0 ? 0.0 : 1.0 < t ? 1.0 : t;
}
/* FUN_1801371c0 (l.167184-167255): no vertex in the range: the arc of sagitta d for the corner's turn, min(radius, R) / R */
static double gp_pp_turn_scale(double d, double R, P2 P, P2 X, P2 Q, int sg) {
    double hp = sg != -1 ? -1.5707963267948966 : 1.5707963267948966, v = 0.0;
    double dx = X.x - P.x, dy = X.y - P.y;
    if (!gp_big(dx, dy)) return 0.0;
    double a1 = hp + atan2(dy, dx);
    while (3.141592653589793 < a1) a1 -= 6.283185307179586;
    while (a1 < -3.141592653589793) a1 += 6.283185307179586;
    double ex = Q.x - X.x, ey = Q.y - X.y;
    if (!gp_big(ex, ey)) return 0.0;
    double a2 = hp + atan2(ey, ex);
    while (3.141592653589793 < a2) a2 -= 6.283185307179586;
    while (a2 < -3.141592653589793) a2 += 6.283185307179586;
    double tr = gp_wrap1(a2 - a1);
    if (sg == -1) { if (0.0 < tr) tr -= 6.283185307179586; }
    else if (sg == 1 && tr < 0.0) tr += 6.283185307179586;
    if (1e-12 <= fabs(tr)) {
        double w = d / (1.0 - cos(tr * 0.5));
        if (R < w) w = R;
        if (1e-12 <= fabs(R)) v = w / R;
    }
    return v;
}
/* FUN_18013ab60 (l.168970-169080): a vertex V on the outer side of P - X or X - Q is pulled to the nearest such segment
   point; farther than 1.1 d -> 1 (reject) */
static int gp_pp_outer(double d, P2 P, P2 X, P2 Q, int sg, P2 *V) {
    double best = 3.4028234663852886e+38; P2 bp = p2(0, 0);
    int s1 = gp_sgn((V->y - P.y) * (X.x - P.x) - (X.y - P.y) * (V->x - P.x));
    if (s1 != sg) { P2 f = cs_near_seg(P, X, *V); double dd = sqrt((V->y - f.y) * (V->y - f.y) + (V->x - f.x) * (V->x - f.x)); if (dd < 3.4028234663852886e+38) { bp = f; best = dd; } }
    int s2 = gp_sgn((Q.x - X.x) * (V->y - X.y) - (Q.y - X.y) * (V->x - X.x));
    if (s2 != sg) { P2 f = cs_near_seg(X, Q, *V); double dd = sqrt((V->y - f.y) * (V->y - f.y) + (V->x - f.x) * (V->x - f.x)); if (dd < best) { bp = f; best = dd; } }
    if (s1 != sg || s2 != sg) { if (d * 1.1 < best) return 1; *V = bp; }
    return 0;
}
/* FUN_1801395e0 (l.168321-168466): the arc scale = the mean over the range's vertices of the scale through each (outer
   vertices pulled in, FUN_18013ab60), each clamped to [tmin, 1]; empty range: FUN_1801371c0 */
static int gp_pp_mean(double d, const int r[2], const Sh *p, const GpLn *L1, const GpLn *L2, const GpLn *L3, double *out) {
    double R = gp_pp_rad(L1, L3), tmin = gp_pp_tmin(d, R * R, L3);
    P2 P = L1->p, X = L3->p, Q = L2->p;
    int sg = gp_sgn((X.x - P.x) * (Q.y - P.y) - (Q.x - P.x) * (X.y - P.y));
    *out = 0.0;
    if (R < -0.5 || tmin < -0.5) return 0;
    double sum = 0.0, v = 0.0; int cnt = 0;
    for (int k = r[0];; k++) {
        if (r[1] < k) {
            if (cnt) { *out = sum / (double)cnt; return 1; }
            v = gp_pp_turn_scale(d, R, P, X, Q, sg);
        } else {
            P2 V = gp_pp_start(&p->e[k]);
            if (gp_pp_outer(d, P, X, Q, sg, &V)) return 0;
            if (!gp_pp_scale(R * R, L3, V, &v)) continue;
        }
        v = tmin <= v ? (1.0 < v ? 1.0 : v) : tmin;
        sum += v; cnt++;
    }
}
static void gp_push_ok(Sh *s, Ent e) { if (e_len(&e) >= 1e-06) sh_push(s, e); }   /* the push, then the erase under 1e-6 */
/* FUN_18013ae30 (l.169081-169297): centre C = X + s L3.d; the feet T1 / T2 of C on L1 / L2; cand = line P T1, arc C
   (T1 -> T2, the corner's turn), line T2 Q (pieces under 1e-6 dropped) */
static int gp_pp_fillet(const GpLn *L1, const GpLn *L2, const GpLn *L3, double s, Sh *cand) {
    P2 P = L1->p, Q = L2->p, X = L3->p;
    int sg = gp_sgn((X.x - P.x) * (Q.y - P.y) - (Q.x - P.x) * (X.y - P.y));
    cand->n = 0;
    if (s < -0.5 || (sg != 1 && sg != -1)) return 0;
    P2 C = p2(s * L3->d.x + X.x, s * L3->d.y + X.y), T1, T2;
    if (!gp_ln_x(*L1, (GpLn){ C, p2(-L1->d.y, L1->d.x) }, NULL, NULL, &T1)) return 0;
    if (!gp_ln_x(*L2, (GpLn){ C, p2(-L2->d.y, L2->d.x) }, NULL, NULL, &T2)) return 0;
    double r = sqrt((C.x - T1.x) * (C.x - T1.x) + (C.y - T1.y) * (C.y - T1.y));
    if (!gp_big(T1.x - C.x, T1.y - C.y)) return 0;
    double a0 = atan2(T1.y - C.y, T1.x - C.x);
    if (!gp_big(T2.x - C.x, T2.y - C.y)) return 0;
    double sw = gp_wrap1(atan2(T2.y - C.y, T2.x - C.x) - a0);
    if (sg == -1) { if (0.0 < sw) sw -= 6.283185307179586; }
    else if (sg == 1 && sw < 0.0) sw += 6.283185307179586;
    gp_push_ok(cand, ent_line(P, T1)); gp_push_ok(cand, ent_arc(C, r, a0, sw)); gp_push_ok(cand, ent_line(T2, Q));
    gp_upd_minmax(cand);
    return 1;
}
/* FUN_180136090 (l.166606-166701): path entities i .. j-1 sampled at 0, 1/3, 2/3 (entity i from 1/3) all within d of
   cand (nearest point, FUN_18013d280), and cand not outside G (FUN_180113300(cand, G, 1e-5) & 4 == 0) */
static int gp_pp_valid(double d, const ShL *G, int i, int j, const Sh *p, const Sh *cand) {
    int f = 1;
    for (int k = i; k < j;) {
        const Ent *e = &p->e[k]; double fr = (double)f / 3.0; P2 s;
        if (e->id == 'L') s = p2((e->p2.x - e->p1.x) * fr + e->p1.x, (e->p2.y - e->p1.y) * fr + e->p1.y);
        else if (e->id == 'A') { double g = fr * e->sw + e->sa; s = p2(cos(g) * e->r + e->c.x, sin(g) * e->r + e->c.y); }
        else if (e->id == 'P') s = e->p1;
        else s = p2(0, 0);
        Pos q; sh_nearest(cand, s, &q);
        if (d * d < (s.y - q.pt.y) * (s.y - q.pt.y) + (s.x - q.pt.x) * (s.x - q.pt.x)) return 0;
        f = (f + 1) % 3;
        if (f == 0) k++;
    }
    if (G->n && (sh_vs_list(cand, G, 1e-05) & 4)) return 0;
    return 1;
}
/* FUN_180138910 (l.167879-168320): a short (< d) first line of cand (out empty or ending in an arc) and / or a short last
   line are folded into one arc: from S (cand's start, or the first line's end) with the start tangent, through cand's end
   (FUN_1800a72d0 = FUN_1800a6f70, when the last piece is short or an arc), else tangent to the last line (FUN_1800a8660)
   followed by a line to cand's end; kept when FUN_180136090 accepts it */
static void gp_pp_fold(double d, const ShL *G, int i, int j, const Sh *p, const Sh *o, Sh *cand) {
    int last = cand->n - 1;
    if (last < 1) return;
    const Ent *f = &cand->e[0], *l = &cand->e[last];
    int b10 = f->id == 'L' && (f->p1.y - f->p2.y) * (f->p1.y - f->p2.y) + (f->p1.x - f->p2.x) * (f->p1.x - f->p2.x) < d * d
              && (o->n == 0 || o->e[o->n - 1].id == 'A');
    int b11 = l->id == 'L' && (l->p1.y - l->p2.y) * (l->p1.y - l->p2.y) + (l->p1.x - l->p2.x) * (l->p1.x - l->p2.x) < d * d;
    if (!b10 && !b11) return;
    P2 S = b10 || f->id != 'L' ? gp_pp_start(f) : f->p2, Ts = gp_tan_vec(f, 0.0), D = gp_pp_end(l), Tl = gp_tan_vec(l, 1.0);
    Ent a;
    if (b11 || l->id == 'A') { if (!gp_arc_tp(Ts, S, D, &a, NULL)) return; }
    else if (!gp_arc_tt(S, Ts, D, Tl, &a)) return;
    Sh *N = sh_new();
    if (!b10 && f->id == 'L') sh_push(N, *f);
    gp_push_ok(N, a);
    if (!b11 && l->id != 'A') gp_push_ok(N, ent_line(gp_raw_end(&a), D));
    gp_upd_minmax(N);
    if (gp_pp_valid(d, G, i, j, p, N)) { Sh t = *cand; *cand = *N; *N = t; }
    sh_free(N);
}
/* FUN_1801353a0 (l.166191-166265): the replacement of path entities i .. j-1 by line / arc / line (FUN_180137b70,
   FUN_180139a40, FUN_1801395e0, FUN_18013ae30), accepted by FUN_180136090, then FUN_180138910 */
static int gp_pp_try(double d, const ShL *G, const Sh *p, const Sh *o, int i, int j, P2 ti, P2 tj, Sh *cand) {
    cand->n = 0;
    GpLn L1, L2, L3; int r[2]; double s;
    if (!(i < j) || !gp_pp_corner(ti, tj, i, j, p, &L1, &L2, &L3) || !gp_pp_range(d, i, j, p, &L1, &L2, &L3, r)
        || !gp_pp_mean(d, r, p, &L1, &L2, &L3, &s)) return 0;
    if (gp_pp_fillet(&L1, &L2, &L3, s, cand) && gp_pp_valid(d, G, i, j, p, cand)) { gp_pp_fold(d, G, i, j, p, o, cand); return 1; }
    cand->n = 0;
    return 0;
}
/* FUN_180135610 (l.166266-166605) with FUN_180137510 (l.167256-167361): entity i alone: an arc from its start S tangent to
   ti, radius min(d / 4 / (1 - cos turn), the radius of the circle tangent there through its end D (FUN_1800a8c00)), then
   the line from the arc's tangent point to D (FUN_1800a9600: CalcCirCirIntersect with the circle on C D; the smaller
   sweep; no point -> D itself); accepted by FUN_180136090 */
static int gp_pp_single(double d, const ShL *G, const Sh *p, int i, P2 ti, Sh *cand) {
    cand->n = 0;
    const Ent *e = &p->e[i];
    P2 S = gp_pp_start(e), D = gp_pp_end(e);
    double vx = D.x - S.x, vy = D.y - S.y;
    if (!gp_big(vx, vy)) return 0;
    double ang = atan2(vy, vx);
    if (!gp_big(ti.x, ti.y)) return 0;
    ang = gp_wrap1(ang - atan2(ti.y, ti.x));
    double aa = fabs(ang);
    if (aa < 1e-12) return 0;
    double k = 1.5707963267948966 <= aa ? cos((3.141592653589793 - aa) * 2.0 * 0.5) + 1.0 : 1.0 - cos(aa * 2.0 * 0.5);
    if (!(1e-12 <= fabs(k))) return 0;
    double r = d * 0.25 / k, rr;
    if (!gp_arc_tp(ti, S, D, NULL, &rr)) return 0;
    if (rr < r) r = rr;
    P2 n = ang <= 0.0 ? p2(ti.y, -ti.x) : p2(-ti.y, ti.x);
    P2 C = p2(S.x + n.x * r, S.y + n.y * r);
    int dir = 0.0 < ang ? 1 : -1;
    double sx = S.x - C.x, sy = S.y - C.y;
    if (!gp_big(sx, sy)) return 0;
    double sa = atan2(sy, sx);
    P2 E = gp_pp_end(e), m = p2((E.x + C.x) * 0.5, (E.y + C.y) * 0.5), T[2];
    int nt = gp_circir(C, r, m, sqrt((m.y - E.y) * (m.y - E.y) + (m.x - E.x) * (m.x - E.x)), T);
    if (nt == 0) { nt = 1; T[0] = D; }
    double sw[2] = { 0, 0 };
    for (int q = 0; q < nt; q++) {
        if (fabs(T[q].x - C.x) <= 1e-12 && fabs(T[q].y - C.y) <= 1e-12) return 0;
        double w = gp_wrap1(atan2(T[q].y - C.y, T[q].x - C.x) - sa);
        if ((dir == -1 && 0.0 < w) || (dir == 1 && w < 0.0)) w += 0.0 <= w ? -6.283185307179586 : 6.283185307179586;
        sw[q] = w;
    }
    int q = nt == 2 && fabs(sw[1]) < fabs(sw[0]) ? 1 : 0;
    gp_push_ok(cand, ent_arc(C, r, sa, sw[q])); gp_push_ok(cand, ent_line(T[q], D));
    gp_upd_minmax(cand);
    if (!gp_pp_valid(d, G, i, i + 1, p, cand)) { cand->n = 0; return 0; }
    return 1;
}
/* FUN_1801368c0 (l.166850-166999): from entity i, a binary search on the end vertex j (from n) for the farthest accepted
   replacement (FUN_1801380f0 / FUN_180138510 tangents, FUN_1801353a0); none -> entity i alone (FUN_180135610). *j := the
   end of the replacement (i when there is none) */
static void gp_pp_search(double d, const ShL *G, const P2 *Ed, const Sh *p, const Sh *o, int i, int *j, Sh **best) {
    Sh *s0 = sh_new(), *s1 = sh_new();
    P2 ti, tj; gp_pp_tan_i(Ed, o, &ti);
    int lo = i, hi = *j, step;
    do {
        gp_pp_tan_j(d, Ed, p, o, *j, &tj);
        if (!gp_pp_try(d, G, p, o, i, *j, ti, tj, s1)) { hi = *j; step = (*j - lo) >> 1; *j -= step; }
        else { Sh *t = s0; s0 = s1; s1 = t; lo = *j; step = (hi - *j) >> 1; *j += step; }
    } while (step != 0);
    if (s0->n == 0) {
        *j = i + 1;
        if (gp_pp_single(d, G, p, i, ti, s1)) { sh_free(s0); *best = s1; return; }
    }
    *j = lo;
    sh_free(s1); *best = s0;
}
/* FUN_180136d40 (l.167002-167151): out's last entity and cand's first merge: lines when cand's start lies within 1e-7 of
   the joined line; arcs of the same centre / radius (1e-7) when (F.sw > 0) * L.sw >= 0 (as compiled, disasm
   0x180136f04-0x180136f25) - sweeps added */
static int gp_pp_merge(const Sh *cand, const Sh *o, Ent *m) {
    if (!o->n || !cand->n) return 0;
    const Ent *L = &o->e[o->n - 1], *F = &cand->e[0];
    if (L->id != F->id) return 0;
    if (F->id == 'L') {
        *m = *L; m->p2 = F->p2;
        P2 f = cs_near_seg(m->p1, m->p2, F->p1);
        return sqrt((F->p1.y - f.y) * (F->p1.y - f.y) + (F->p1.x - f.x) * (F->p1.x - f.x)) <= 1e-07;
    }
    if (!(fabs(L->r - F->r) <= 1e-07)) return 0;
    double t = (double)(0.0 < F->sw) * L->sw;
    if (!(0.0 <= t) || !(fabs(L->c.x - F->c.x) <= 1e-07) || !(fabs(L->c.y - F->c.y) <= 1e-07)) return 0;
    *m = *L; m->sw = F->sw + m->sw;
    return 1;
}
/* FUN_1801351b0 (l.166114-166188): cand appended to out (its first entity merged into out's last when FUN_180136d40 allows;
   the rest by FUN_180146e20) */
static void gp_pp_append(const Sh *cand, Sh *o) {
    Ent m; int mg = gp_pp_merge(cand, o, &m);
    if (mg) o->e[o->n - 1] = m;
    if (mg < cand->n) sh_section_full(cand, mg, 0.0, cand->n - 1, 1.0, o, !mg);
    gp_upd_minmax(o);
}
/* FUN_180136370 (l.166702-166789) + FUN_180136680 (l.166792-166848): each path shape rebuilt, entity run by entity run
   (FUN_1801368c0); shape i reads the tangent at Ed + 16 i (only shape 0 is meaningful; ours: the planner's verdict needs
   exactly one shape, the others are left as they are); +0x50 (dir) copied */
static void gp_post(double d, const ShL *G, P2 Ed, ShL *paths) {
    if (paths->n < 1) return;
    Sh *p = paths->s[0], *o = sh_new();
    for (int i = 0; i < p->n;) {
        int j = p->n; Sh *c = NULL;
        gp_pp_search(d, G, &Ed, p, o, i, &j, &c);
        if (c->n == 0) { sh_push(o, p->e[i]); gp_upd_minmax(o); i++; }
        else { gp_pp_append(c, o); i = j; }
        sh_free(c);
    }
    o->dir = p->dir;
    paths->s[0] = o; sh_free(p);
}
static int guided_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_GUIDED"); v = exact_on() && e && *e == '1'; } return v; }
/* FUN_1800f1080 (MCCE_GUIDED=1, exact model): 1 and out = the planned path (one shape) when the verdict holds and out is
   given (connectors 4 / 5, strategy 5), after the post-process FUN_180136370. cross (FUN_1800f0fa0's 9th argument, disasm
   0x1800f0fb8 / 0x1800f10e8 / 0x1800f1265: only the in-frame plain route FUN_1800f1580 writes it, = FUN_18010a6b0's off-line
   length of that route) is set when given */
static int guided_plan(double d, double step, int both, P2 E, P2 Ed, P2 A, P2 Ad, const ShL *G, ShL *out, double *cross) {
    if (!G->n) return 0;
    gp_dump_open(d, step, both, E, Ed, A, Ad, G);
    ShL Gp = sl_copy(G);
    for (int i = 0; i < Gp.n; i++) gp_upd_minmax(Gp.s[i]);   /* the DLL CSh boxes are UpdMinMaxSh / SetChnMinMax ones */
    gp_orient(&Gp); gp_dump_sl("orient", &Gp);
    gp_split_at(E, A, &Gp); gp_dump_sl("split", &Gp);
    gp_linearise(d * 0.9, &Gp); gp_dump_sl("linearise1", &Gp);
    gp_prepare(d * 0.1, E, A, &Gp); gp_dump_sl("prepare", &Gp);
    int degen = gp_degenerate(&Gp);
    if (g_gp_f) fprintf(g_gp_f, "stage degenerate %d\n", degen);
    if (degen) { if (g_gp_f) { fclose(g_gp_f); g_gp_f = NULL; } sl_free(&Gp); return 0; }
    gp_corners(step, E, A, &Gp); gp_dump_sl("corners", &Gp);   /* FUN_1800e9790 (disasm 0x1800e9790-0x1800e97f3): the cut size is step */
    gp_end_cut(d, step, E, Ed, A, Ad, &Gp); gp_dump_sl("cuts", &Gp);   /* FUN_1800e9440 (tail jump, disasm 0x1800e97d6-0x1800e97f3) */
    gp_feed("cuts", &Gp);   /* ours (unit oracle): MCCE_GPFEED=1 restarts from the DLL's region (the subtraction's boolean is ours) */
    gp_linearise(d * 0.9, &Gp); gp_dump_sl("linearise2", &Gp);
    gp_snap_ends(E, A, &Gp); gp_dump_sl("f2ca0", &Gp);
    double a = gp_slice_angle(&Gp);
    if (g_gp_f) fprintf(g_gp_f, "stage angle %.17g\n", a);
    gp_rotate(&Gp, a); gp_dump_sl("rotate", &Gp);
    ShL rt = { 0 }; gp_plain_route(both, E, A, a, &Gp, &rt); gp_dump_sl("route", &rt);
    if (cross) { *cross = 0.0; if (rt.n) on_off_len(rt.s[0], NULL, cross); }   /* FUN_1800f1580 (l.130763-130880): 0, then FUN_18010a6b0 */
    GpTraps T = { 0 }, ch = { 0 };
    gp_traps(&Gp, &T); gp_dump_traps("traps", &T);
    gp_channel(&rt, &T, &ch); gp_dump_traps("channel", &ch);
    if (dbg_level() >= 3) {
        fprintf(stderr, "guided: angle %.6f vertices %d traps %d route %d channel %d:", a, gp_nvtx(&Gp), T.n, rt.n ? rt.s[0]->n : -1, ch.n);
        for (int k = 0; k < ch.n; k++) fprintf(stderr, " %d", ch.v[k].idx);
        fprintf(stderr, "\n");
        if (dbg_level() >= 4) for (int k = 0; k < T.n; k++) { const GpTrap *t = &T.v[k];
            fprintf(stderr, "  trap %d: (%.4f,%.4f)-(%.4f,%.4f) / (%.4f,%.4f)-(%.4f,%.4f)\n", t->idx, t->a.L.pt.x, t->a.L.pt.y, t->a.R.pt.x, t->a.R.pt.y, t->bL.pt.x, t->bL.pt.y, t->bR.pt.x, t->bR.pt.y); }
    }
    ShL path = { 0 }; gp_search(&rt, &ch, &path); gp_dump_sl("search", &path);
    gp_rotate(&path, -a);   /* RotationMx(-a) (disasm 0x1800f12c1: xmm6 ^ sign) */
    gp_dump_sl("rotate_back", &path);
    if (dbg_level() >= 3 && path.n) {
        fprintf(stderr, "guided: search path %d:", path.s[0]->n);
        for (int k = 0; k < path.s[0]->n; k++) fprintf(stderr, " (%.4f,%.4f)", path.s[0]->e[k].p1.x, path.s[0]->e[k].p1.y);
        if (path.s[0]->n) fprintf(stderr, " (%.4f,%.4f)", path.s[0]->e[path.s[0]->n - 1].p2.x, path.s[0]->e[path.s[0]->n - 1].p2.y);
        fprintf(stderr, "\n");
    }
    gp_post(d, G, Ed, &path); gp_dump_sl("post", &path);   /* FUN_180136370(d, G (the input group), E+0x10, out) (disasm 0x1800f12df-0x1800f12ec) */
    /* FUN_1800f1080's verdict (disasm 0x1800f12f1-0x1800f1320): exactly one shape and FUN_180113300(out[0], G, 1e-5) & 4 == 0 */
    int verdict = 0;
    if (path.n == 1) { int in = sh_vs_list(path.s[0], G, 1e-05); if (g_gp_f) fprintf(g_gp_f, "stage inside %#x\n", in); verdict = (in & 4) == 0; }
    if (g_gp_f) { fprintf(g_gp_f, "stage result %d\n", verdict); fclose(g_gp_f); g_gp_f = NULL; }
    if (verdict && out) { sl_clear(out); sl_move_append(out, &path); }
    sl_free(&path);
    free(T.v); free(ch.v); sl_free(&rt); sl_free(&Gp);
    return verdict && out;
}

/* ============================================================================================ link targets (§6.10) */
/* FUN_180047920 (l.42563-42688): the nest group of `set` containing the shapes nearest to P and Q */
static int group_of(const ShL *set, P2 Pp, P2 Q, ShL *out) {
    memset(out, 0, sizeof *out);
    if (sl_nonisland(set) == 1) { out->s = set->s; out->n = set->n; return 1; }
    Pos a, b; if (!sl_nearest(set, Pp, &a) || !sl_nearest(set, Q, &b)) return 0;
    int i = 0;
    while (i < set->n) { int e = sl_group_end(set, i); if (e < 0) e = set->n;
        if (a.sh >= i && a.sh < e && b.sh >= i && b.sh < e) { out->s = set->s + i; out->n = e - i; return 1; }
        i = e; }
    return 0;
}
/* FUN_1800475c0 (l.42418-42562) */
static int group_of_pos(const ShL *set, int ps, int qs, ShL *out) {
    memset(out, 0, sizeof *out);
    int i = 0;
    while (i < set->n) { int e = sl_group_end(set, i); if (e < 0) e = set->n;
        if (ps >= i && ps < e && qs >= i && qs < e) { out->s = set->s + i; out->n = e - i; return 1; }
        i = e; }
    return 0;
}
/* FUN_1801136d0 (l.148570-148622): first sample of sh that is inside (2) or outside (4) of set; else 1 */
static int sh_sample_in(const Sh *sh, const ShL *set, double tol) {
    if (!sh || !sh->n || !set->n) return 1;
    for (int k = 0; k < sh->n; k++) for (int st = 0; st < 3; st++) {
        if (st == 2 && k == sh->n - 1) break;
        double f = st == 0 ? 0.3333333333333333 : st == 1 ? 0.6666666666666666 : 1.0;
        int c = sl_inside(set, tol, e_at(&sh->e[k], f)); if (c == 2 || c == 4) return c;
    }
    return 1;
}
/* FUN_180050af0 (l.47321-47597): route from the last point to Q through S2 (and S4); the longer route is kept */
static int route_to(Pock *C, P2 Q, const ShL *S2, const ShL *S4, ShL *out, int *flag) {
    memset(out, 0, sizeof *out); *flag = 0;
    P2 Pp = p2(0, 0); last_pt(C, &Pp);
    Pos nP2, nQ2, nP4, nQ4; sl_nearest(S2, Pp, &nP2); sl_nearest(S2, Q, &nQ2); sl_nearest(S4, Pp, &nP4); sl_nearest(S4, Q, &nQ4);
    ShL G2; if (!group_of_pos(S2, nP2.sh, nQ2.sh, &G2)) return 0;
    if ((sl_inside(&G2, 1e-5, Pp) == 4 && pdist(Pp, nP2.pt) > 0.001) || (sl_inside(&G2, 1e-5, Q) == 4 && pdist(Q, nQ2.pt) > 0.001)) return 0;
    Ent ln = ent_line(nP2.pt, nQ2.pt); route(1, &G2, &ln, out);
    ShL G4;
    if (out->n && S4->n && group_of_pos(S4, nP4.sh, nQ4.sh, &G4) && sh_sample_in(G4.s[0], &G2, 1e-5) == 2) {
        ShL r = { 0 }; Ent l4 = ent_line(nP4.pt, nQ4.pt); route(1, &G4, &l4, &r);
        if (r.n) { *flag = 1; if (sh_len(out->s[0]) < sh_len(r.s[0])) { sl_free(out); *out = r; r.s = NULL; r.n = 0; } }
        sl_free(&r);
    }
    return out->n > 0;
}
/* FUN_180054450 (l.49090-49234): sections of B[i] between its crossings with S1, classified against S1 */
typedef struct { int a, b, cls; double len; } Sec;
static int sections_b(const Sh *Bi, const ShL *S1, const IsabL *X, Sec **out) {
    int n = X->n; Sec *s = malloc((n ? n : 1) * sizeof(Sec));
    for (int k = 0; k < n; k++) {
        s[k].a = k % n; s[k].b = (k + 1) % n;
        s[k].cls = sec_in_list(Bi, X->r[s[k].a].xe, X->r[s[k].a].xt, X->r[s[k].b].xe, X->r[s[k].b].xt, n == 1, S1, 1e-5);
        s[k].len = sec_len(Bi, X->r[s[k].a].xe, X->r[s[k].a].xt, X->r[s[k].b].xe, X->r[s[k].b].xt);
    }
    for (int k = 0; k < n; ) { int p = k == 0 ? n - 1 : k - 1;
        if (n > 1 && s[k].cls == s[p].cls && k != p) { s[p].b = s[k].b; s[p].len += s[k].len; memmove(&s[k], &s[k + 1], (n - k - 1) * sizeof(Sec)); n--; if (p > k) p--; continue; }
        k++; }
    for (int k = 0; k < n; ) { if (s[k].cls == 1 && s[k].len < 1e-7) { memmove(&s[k], &s[k + 1], (n - k - 1) * sizeof(Sec)); n--; continue; } k++; }
    *out = s; return n;
}
/* FUN_180053480 (l.48599-48668): keep only the S1 shapes that really cross b */
static void drop_touching(const Sh *b, const ShL *S1, IsabL *X) {
    for (int k = 0; k < X->n; ) {
        int in = 0, out = 0, cur = k, nxt = X->r[k].next, s1 = X->r[k].ys;
        for (int g = 0; g <= X->n; g++) {
            if (nxt < 0) break;
            Sh bb = *b; ShL bl = { (Sh **)&b, 1, 1 }; (void)bb;
            int p = sec_in_shape(S1->s[X->r[cur].ys], X->r[cur].ye, X->r[cur].yt, X->r[nxt].ye, X->r[nxt].yt, 0, b, 1e-5); (void)bl;
            if (p == 2) in++; else if (p == 4) out++;
            if ((in && out) || nxt == k) break;
            cur = nxt; nxt = X->r[nxt].next;
        }
        if (!in || !out) {   /* FUN_180133820: erase every record of that S1 shape */
            int w = 0; int *map = malloc(X->n * sizeof(int));
            for (int q = 0; q < X->n; q++) { if (X->r[q].ys == s1) map[q] = -1; else map[q] = w++; }
            int m = 0; for (int q = 0; q < X->n; q++) if (map[q] >= 0) { Isab r = X->r[q]; if (r.next >= 0) r.next = map[r.next]; if (r.prev >= 0) r.prev = map[r.prev]; X->r[m++] = r; }
            X->n = m; free(map);
        } else { int sk = X->r[k].ys; k++; while (k < X->n && X->r[k].ys == sk) k++; }
    }
}
static int blacklisted(const Pock *C, P2 pt) { for (int i = 0; i < C->nbl; i++) if (pdist2(pt, C->bl[i]) < 1.0000000000000002e-10) return 1; return 0; }   /* FUN_180053cd0 */
/* FUN_180052070 (l.48000-48063) + FUN_1800522c0 (l.48067-48313) + FUN_180053760 (l.48671-48767): boundary targets */
static void targets_boundary(Pock *C, const ShL *B, const ShL *S1, TargL *T, int *used, int *nused) {
    const McceParm *P = &C->P;
    for (int i = 0; i < B->n; i++) {
        IsabL X = { 0 }; isect1(B->s[i], S1, &X);
        drop_touching(B->s[i], S1, &X);
        if (!X.n) { il_free(&X); continue; }
        int n0 = T->n; Sec *sec; int ns = sections_b(B->s[i], S1, &X, &sec);
        int *K = malloc((ns + 1) * sizeof(int)), nk = 0;
        ShL Bl = { (Sh **)&B->s[i], 1, 1 };
        for (int k = 0; k < ns; k++) {
            int pv = k == 0 ? ns - 1 : k - 1, nx = k < ns - 1 ? k + 1 : 0;
            if (!(sec[k].cls == 2 || (sec[k].cls == 1 && sec[pv].cls == 4 && sec[nx].cls == 4))) continue;
            double d = P->step15; if (sec[pv].len <= d) d = 0.5 * sec[pv].len;
            Pos Bp = { 0, X.r[sec[k].a].xe, X.r[sec[k].a].xt, X.r[sec[k].a].pt }, Ap;
            walk_back(0, d, &Bl, &Bp, &Ap, NULL);
            if (blacklisted(C, Ap.pt)) continue;
            Targ *t = tl_push(T);
            t->pt = Ap.pt; t->dir = tan_in(B->s[i], Ap.ent, Ap.t, 1e-10);    /* FUN_18011ffd0 at A (l.48213-48216) */
            t->b22 = 1; t->d28 = d; t->A = Ap; t->A.sh = i; t->B = Bp; t->B.sh = i;
            t->s1 = X.r[sec[k].a].ys;
            K[nk++] = k;
            if (used) { int s1 = X.r[sec[k].a].ys, f = 0; for (int q = 0; q < *nused; q++) if (used[q] == s1) f = 1; if (!f) used[(*nused)++] = s1; }
        }
        /* FUN_180053760: an on-boundary target is dropped when the same B[i] gave an inside target on the same S1 shape */
        for (int p = n0; p < T->n; p++) {
            if (sec[K[p - n0]].cls != 1) continue;
            for (int q = n0; q < T->n; q++)
                if (sec[K[q - n0]].cls != 1 && X.r[sec[K[q - n0]].a].ys == X.r[sec[K[p - n0]].a].ys) {
                    tl_erase(T, p); memmove(&K[p - n0], &K[p - n0 + 1], (T->n - p) * sizeof(int)); p--; break; }
        }
        free(K); free(sec); il_free(&X);
    }
}
/* FUN_180053e80 (l.48900-48966): bbox diagonal of sh <= 2 (R - r) pct / 100 */
static int small_bbox(const Pock *C, const Sh *sh, double pct) {
    double lim = (C->P.R - C->P.r) * (pct / 100) * 2, bb[4] = { 1e300, 1e300, -1e300, -1e300 };
    for (int i = 0; i < sh->n; i++) { double b[4]; e_bbox(&sh->e[i], b);
        bb[0] = fmin(bb[0], b[0]); bb[1] = fmin(bb[1], b[1]); bb[2] = fmax(bb[2], b[2]); bb[3] = fmax(bb[3], b[3]);
        if ((bb[2] - bb[0]) * (bb[2] - bb[0]) + (bb[3] - bb[1]) * (bb[3] - bb[1]) > lim * lim) return 0; }
    return 1;
}
/* FUN_180050410 (l.47123-47318): the corner entities (CeFlags bit 2: the arcs / mitres MCCECalcOffset's raw ring inserts at the
   corners, FUN_1800c9d70) are stepped over. Not on one: the rest of this entity and the following ones are measured up to the
   start of a corner entity; reached within C+0x1e8 (P+0x1e0, the march step): pos moves to its start. On one (then): pos moves
   past the run of corner entities to the start of the next entity. The distance moved is added to *dist (FUN_1800502a0). */
static double skip_len(const Ent *e) { return e->id == 'L' ? sqrt((e->p2.y - e->p1.y) * (e->p2.y - e->p1.y) + (e->p2.x - e->p1.x) * (e->p2.x - e->p1.x)) : e->id == 'A' ? fabs(e->sw * e->r) : 0.0; }
static double skip_rest(const Ent *e, double t) {
    if (e->id == 'L') {
        double dy = e->p2.y - e->p1.y, dx = e->p2.x - e->p1.x;
        double a = (dy + e->p1.y) - (dy * t + e->p1.y), b = (dx + e->p1.x) - (dx * t + e->p1.x);
        return sqrt(a * a + b * b);
    }
    return e->id == 'A' ? fabs((1.0 - t) * e->sw * e->r) : 0.0;
}
static void skip_to(const Sh *s, int j, Pos *pos, double d, double *dist) {
    pos->ent = j; pos->t = 0.0;
    const Ent *e = &s->e[j];
    pos->pt = e->id == 'A' ? p2(cos(e->sa) * e->r + e->c.x, sin(e->sa) * e->r + e->c.y) : e->p1;
    *dist += d;
}
static void skip_corners(const McceParm *P, const Sh *s, Pos *pos, double *dist) {
    int n = s->n, i0 = pos->ent, j;
    if (n <= 0 || i0 < 0 || i0 >= n) return;
    if (!(s->e[i0].ce & 2)) {
        double d = skip_rest(&s->e[i0], pos->t);
        j = (i0 + 1) % n;
        if (j != i0) do {
            if (s->e[j].ce & 2) break;
            if (P->step < d) return;
            d += skip_len(&s->e[j]);
            j = (j + 1) % n;
        } while (j != i0);
        if (P->step < d || j == i0) return;
        skip_to(s, j, pos, d, dist);
    }
    i0 = pos->ent;
    if (s->e[i0].ce & 2) {
        double d = skip_rest(&s->e[i0], pos->t);
        j = (i0 + 1) % n;
        if (j != i0) do {
            if (!(s->e[j].ce & 2)) break;
            d += skip_len(&s->e[j]);
            j = (j + 1) % n;
        } while (j != i0);
        if (j != i0) skip_to(s, j, pos, d, dist);
    }
}
/* FUN_180051480 + FUN_1800502a0 / FUN_180050410 (l.47078-47891): first point of S1[i] where a tangent circle clears the
   two circles beside the current motion */
static int target_circle(Pock *C, const ShL *B, const ShL *S1, int i, double rho, TargL *T) {
    const McceParm *P = &C->P;
    double L = sh_len(S1->s[i]), lim = 4 * rho * rho;
    P2 cur, Tt; if (!last_pt(C, &cur) || !last_tan(C, &Tt)) return 0;
    double tl = plen(Tt); if (tl < 1e-12) return 0;
    P2 n = pmul(p2(Tt.y, -Tt.x), rho / tl), K0 = padd(cur, n), K1 = psub(cur, n);
    Pos pos; sh_nearest(S1->s[i], cur, &pos); pos.sh = i; double dist = 0;
    skip_corners(P, S1->s[i], &pos, &dist);   /* FUN_1800502a0 l.47109-47118 */
    while (dist <= 1.1 * L) {
        P2 t = tan_in(S1->s[i], pos.ent, pos.t, 1e-10);
        P2 ab = (P->flags & 1) ? p2(t.x, -t.y) : p2(-t.x, t.y); double m = plen(ab);
        if (getenv("MCCE_TCDBG")) fprintf(stderr, "tc rho %.6f i %d dist %.6f/%.6f pos %d %.6f (%.6f,%.6f) ce %x\n", rho, i, dist, 1.1 * L, pos.ent, pos.t, pos.pt.x, pos.pt.y, S1->s[i]->e[pos.ent].ce);
        if (m >= 1e-12) {
            P2 K = padd(pos.pt, pmul(p2(ab.y, ab.x), rho / m));
            if (pdist2(K, K0) >= lim && pdist2(K, K1) >= lim && sl_inside_grouped(B, 1e-5, pos.pt) != 4) {   /* FUN_180112af0 without bands */
                Targ *tg = tl_push(T); tg->pt = pos.pt; tg->dir = t; tg->s1 = i; return 1; }
        }
        Pos np; double w = 0; walk_fwd(0, 0.5 * rho, S1, &pos, &np, &w); pos = np; dist += w;
        if (w <= 0) break;
        skip_corners(P, S1->s[i], &pos, &dist);
    }
    return 0;
}
/* FUN_180051cd0 (l.47894-47997): three targets around a small piece */
static void targets_small(Pock *C, const ShL *B, const ShL *S1, int i, TargL *T) {
    double L = sh_len(S1->s[i]); P2 cur = p2(0, 0); last_pt(C, &cur);
    Pos p[3]; sh_nearest(S1->s[i], cur, &p[0]); p[0].sh = i;
    walk_fwd(0, L / 3, S1, &p[0], &p[1], NULL); walk_fwd(0, L / 3, S1, &p[1], &p[2], NULL);
    for (int k = 0; k < 3; k++) {
        if (sl_inside_grouped(B, 1e-5, p[k].pt) == 4) continue;
        P2 t = tan_in(S1->s[i], p[k].ent, p[k].t, 1e-10);
        Targ *tg = tl_push(T); tg->pt = p[k].pt; tg->dir = (C->P.flags & 1) ? p2(t.y, -t.x) : p2(-t.y, t.x); tg->b20 = 1; tg->s1 = i;
    }
}
/* FUN_18005b3c0 (l.52763-52780) */
static int path_ok(Pock *C, const ShL *B, const ShL *S1, const Sh *sh) {
    double rho = C->P.min_cut_rad;
    return sh_len(sh) <= 1.5 * fabs(2 * M_PI * rho) && (sh_vs_list(sh, B, 1e-5) & 5) == 0 && (sh_vs_list(sh, S1, 1e-5) & 2) == 0;
}
/* one-sided offset of an open polyline (role of MCCECalcContourOffset(1, d, [P], 0x100): parallel curve; internals NOT
   DECODED, own construction: segments shifted by d, inside corners joined at the intersection, outside corners by an arc) */
static void contour_offset(const Sh *Pp, double d, Sh *Q) {
    sh_clear(Q); Q->dir = 0;
    for (int k = 0; k < Pp->n; k++) {
        const Ent *e = &Pp->e[k]; if (e->id != 'L') continue;
        int ok; P2 u = unit(psub(e->p2, e->p1), &ok); if (!ok) continue;
        P2 nn = pmul(p2(-u.y, u.x), d); Ent o = ent_line(padd(e->p1, nn), padd(e->p2, nn));
        if (Q->n) {
            Ent *pv = &Q->e[Q->n - 1]; double s, w; P2 x;
            if (pdist(pv->p2, o.p1) > 1e-9) {
                if (ln_ln(pv->p1, psub(pv->p2, pv->p1), o.p1, psub(o.p2, o.p1), &s, &w, &x) && s > 0 && s < 1 + 1e-9 && w > -1e-9 && w < 1) { pv->p2 = x; o.p1 = x; }
                else { double a0 = atan2(pv->p2.y - e->p1.y, pv->p2.x - e->p1.x), a1 = atan2(o.p1.y - e->p1.y, o.p1.x - e->p1.x); sh_push(Q, ent_arc(e->p1, fabs(d), a0, wrap_pi(a1 - a0))); }
            }
        }
        sh_push(Q, o);
    }
    sh_upd_bbox(Q);
}
/* MCCE_NPCOFF (default on): FUN_18005a8a0's Q through the DLL-exact MCCECalcContourOffset port; 0 = the old own construction */
static int npcoff_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_NPCOFF"); v = !(e && *e == '0'); } return v; }
/* MCCECalcContourOffset(1, d, {in}, out, flags, 0.0, gap, 1e-6, 1e-6) on an engine shape (the CVe overload: in's dir kept) */
static int ctour_offset_sh(const Sh *in, double d, unsigned flags, double gap, ShL *out) {
    McceShape m; memset(&m, 0, sizeof m);
    m.e = calloc((size_t)(in->n ? in->n : 1), sizeof(McceEnt)); m.n = in->n; m.dir = in->dir; m.isl = in->fl & 1;
    for (int k = 0; k < in->n; k++) {
        const Ent *e = &in->e[k]; McceEnt *q = &m.e[k];
        q->id = e->id; q->src_sh = 0; q->src_ent = k;
        if (e->id == 'A') { q->a[0] = e->c.x; q->a[1] = e->c.y; q->a[2] = e->r; q->a[3] = e->sa; q->a[4] = e->sw; }
        else { q->a[0] = e->p1.x; q->a[1] = e->p1.y; q->a[2] = e->p2.x; q->a[3] = e->p2.y; }
    }
    McceResult r = mcce_contour_offset_sh(&m, 1, d, flags, 0.0, gap, 1e-06, 1e-06);
    free(m.e);
    if (r.refused) { if (dbg_level()) fprintf(stderr, "mcce: MCCECalcContourOffset (target path) refused (%s)\n", r.refused); return 0; }
    for (int i = 0; i < r.n; i++) {
        Sh *sh = sh_new(); sh->dir = r.s[i].dir; sh->fl = r.s[i].isl;
        for (int k = 0; k < r.s[i].n; k++) {
            const McceEnt *q = &r.s[i].e[k];
            Ent e = q->id == 'A' ? ent_arc(p2(q->a[0], q->a[1]), q->a[2], q->a[3], q->a[4]) : ent_line(p2(q->a[0], q->a[1]), p2(q->a[2], q->a[3]));
            e.ce = q->ce; sh_push(sh, e);
        }
        sh_upd_bbox(sh);
        sl_push(out, sh);
    }
    mcce_shapes_free(&r);
    return 1;
}
/* FUN_18005a000 (l.52108-52357) */
static int motion_circles(Pock *C, double rho, Sh *A, Sh *A2) {
    P2 cur, Tt; if (!last_pt(C, &cur) || !last_tan(C, &Tt)) return 0;
    int ok; P2 T = unit(Tt, &ok); if (!ok) return 0;
    int climb = C->P.flags & 1; P2 nrm = climb ? p2(-T.y, T.x) : p2(T.y, -T.x);
    P2 c0 = psub(cur, pmul(nrm, rho)); double sa = atan2(nrm.y, nrm.x), sw = climb ? -2 * M_PI : 2 * M_PI;
    sh_clear(A); sh_push(A, ent_arc(c0, rho, sa, sw)); A->dir = climb ? -1 : 1; sh_upd_bbox(A);
    if (A2) { sh_clear(A2); sh_push(A2, ent_arc(c0, 2 * rho, sa, sw)); A2->dir = A->dir; sh_upd_bbox(A2); }
    return 1;
}
/* FUN_18005ae90 (l.52563-52760) */
static Pos walk_out(const ShL *L, double D, P2 c, Pos st) {
    const Sh *s = L->s[st.sh]; Pos best = st; double bd = -1;
#define SAMPLE(e_, t_) do { P2 q_ = e_at(&s->e[e_], t_); double d_ = pdist2(q_, c); Pos p_ = { st.sh, e_, t_, q_ }; if (d_ > D * D) return p_; if (d_ > bd) { bd = d_; best = p_; } } while (0)
    SAMPLE(st.ent, (st.t + 1) / 2); SAMPLE(st.ent, 1.0);
    for (int e = (st.ent + 1) % s->n; e != st.ent; e = (e + 1) % s->n) { SAMPLE(e, 0.5); SAMPLE(e, 1.0); }
    SAMPLE(st.ent, st.t / 2);
#undef SAMPLE
    return best;
}
/* FUN_18005a8a0 (l.52360-52560) */
static int near_piece(Pock *C, const Sh *A, const ShL *S1, Sh *Pq, ShL *Q) {
    P2 cur; if (!last_pt(C, &cur)) return 0;
    Pos s; if (!sl_nearest(S1, cur, &s) || s.sh < 0 || !A->n || A->e[0].id != 'A') return 0;
    double rho = C->P.min_cut_rad;
    Pos end = walk_out(S1, 3 * rho, A->e[0].c, s);
    sh_clear(Pq); Pq->dir = 0;
    if (!sh_section(S1->s[s.sh], s.ent, s.t, end.ent, end.t, Pq)) return 0;
    if (!Q) return 1;   /* l.52466: param_6 == 0 */
    /* l.52455-52462: MCCECalcContourOffset(1, P+0x230 (negated unless climb), {P}, Q, 0x100, 0.0, 1e-6, 1e-6, 1e-6) - the DLL-exact
       port (viewer/mcce_ctour.c, the CVe overload: P stays open); success needs a non-empty Q (l.52466). MCCE_NPCOFF=0: the old
       own construction (contour_offset). */
    if (npcoff_on()) return ctour_offset_sh(Pq, (C->P.flags & 1) ? rho : -rho, 0x100, 1e-06, Q) && Q->n > 0;
    Sh *q = sh_new(); contour_offset(Pq, (C->P.flags & 1) ? rho : -rho, q); sl_push(Q, q);
    return 1;
}
/* FUN_180059630 / FUN_180059680 / FUN_180059ca0 / FUN_18005b480 / FUN_18005b760 (l.51758-52994): +0x70 paths for the S1
   shape nearest to the last point */
static int targets_path(Pock *C, const ShL *B, const ShL *S1, int c, TargL *T) {
    if (!C->P.bull_avail) return 0;
    double rho = C->P.min_cut_rad; int climb = C->P.flags & 1, ret = 0;
    Sh A, A2, Pq; memset(&A, 0, sizeof A); memset(&A2, 0, sizeof A2); memset(&Pq, 0, sizeof Pq); ShL Q = { 0 };
    if (c) {
        if (motion_circles(C, rho, &A, NULL) && near_piece(C, &A, S1, &Pq, NULL)) {
            /* Isect(A, P): X side = A; the record order is by A's parameter (inferred, §6.11.2) */
            IsabL J = { 0 }; ShL Pl = { (Sh *[]){ &Pq }, 1, 1 }; isect1(&A, &Pl, &J);
            if (J.n) {
                P2 v = psub(J.r[0].pt, A.e[0].c); P2 dir = A.e[0].sw > 0 ? p2(-v.y, v.x) : p2(v.y, -v.x);
                Targ *t = tl_push(T); t->pt = J.r[0].pt; t->dir = dir; t->b20 = 1;
                Ent a = A.e[0]; a.sw = A.e[0].sw * J.r[0].xt; sh_push(&t->path, a); sh_upd_bbox(&t->path);
                if (path_ok(C, B, S1, &t->path)) ret = 1; else tl_erase(T, T->n - 1);
            }
            il_free(&J);
        }
    } else if (motion_circles(C, rho, &A, &A2) && near_piece(C, &A, S1, &Pq, &Q)) {
        IsabL I = { 0 }; isect1(&A2, &Q, &I);   /* FUN_180134270({A2}, Q) l.52086 */
        if (I.n) {
            Pos q; if (sh_nearest(&Pq, I.r[0].pt, &q)) {
                Targ *t = tl_push(T); t->pt = q.pt; t->dir = tan_in(&Pq, q.ent, q.t, 1e-5);
                /* FUN_18005b760 */
                P2 p = I.r[0].pt; const Ent *a = &A.e[0]; int ok = 1;
                if ((fabs(p.x - a->c.x) <= 1e-12 && fabs(p.y - a->c.y) <= 1e-12) || (fabs(q.pt.x - p.x) <= 1e-12 && fabs(q.pt.y - p.y) <= 1e-12)) ok = 0;
                if (ok) {
                    double sw1 = wrap_pi(atan2(p.y - a->c.y, p.x - a->c.x) - a->sa);
                    if (climb) { if (sw1 > 0) sw1 -= 2 * M_PI; } else if (sw1 < 0) sw1 += 2 * M_PI;
                    double a2 = atan2(a->c.y - p.y, a->c.x - p.x), sw2 = wrap_pi(atan2(q.pt.y - p.y, q.pt.x - p.x) - a2);
                    if (climb) { if (sw2 < 0) sw2 += 2 * M_PI; } else if (sw2 > 0) sw2 -= 2 * M_PI;
                    Ent e1 = *a; e1.sw = sw1; sh_push(&t->path, e1); sh_push(&t->path, ent_arc(p, rho, a2, sw2)); sh_upd_bbox(&t->path);
                }
                if (ok && path_ok(C, B, S1, &t->path)) ret = 1; else tl_erase(T, T->n - 1);
            }
        }
        il_free(&I);
    }
    free(A.e); free(A2.e); free(Pq.e); sl_free(&Q);
    return ret;
}
/* FUN_180052d70 (l.48403-48473): targets on S1 shapes not used by boundary targets */
static void targets_s1(Pock *C, int onlyInside, const ShL *B, const ShL *S1, TargL *T, const int *used, int nused) {
    P2 cur; int curShape = -1; Pos q;
    if (last_pt(C, &cur) && sl_nearest(S1, cur, &q)) curShape = q.sh;
    for (int i = 0; i < S1->n; i++) {
        int u = 0; for (int k = 0; k < nused; k++) if (used[k] == i) u = 1;
        if (u) continue;
        int r = sh_vs_list(S1->s[i], B, 1e-5);
        if (r == 4 || (onlyInside && !(r & 2))) continue;
        /* FUN_180053d60 (l.48855-48897): small piece = the material loop nearest to S1[i] near the last point */
        int c = 0;
        if (last_pt(C, &cur)) { Pos a, m; sh_nearest(S1->s[i], cur, &a); if (sl_nearest(&C->mat, a.pt, &m) && m.sh >= 0) c = small_bbox(C, C->mat.s[m.sh], 35.0); }
        if (i == curShape && targets_path(C, B, S1, c, T)) continue;
        if (c == 0) {   /* FUN_1800512c0 (l.47600-47650) */
            double rl[64]; int nr = radius_list(&C->P, rl);
            for (int k = 0; k < nr; k++) { if (rl[k] <= 0) break; if (target_circle(C, B, S1, i, rl[k], T)) break; }
        } else targets_small(C, B, S1, i, T);
    }
}
static _Thread_local P2 g_sort_cur;
static int targ_cmp(const void *a, const void *b) { const Targ *p = a, *q = b; double x = pdist2(p->pt, g_sort_cur), y = pdist2(q->pt, g_sort_cur); return x < y ? -1 : x > y ? 1 : 0; }
/* FUN_180054a70 (l.49237-49462): mode 2 */
static void order_mode2(Pock *C, const ShL *S2, const ShL *S4, TargL *T) {
    if (T->n < 2) return;
    P2 cur = p2(0, 0); last_pt(C, &cur);
    double best = FLT_MAXD; int *ok = calloc(T->n, sizeof(int)), *fl = calloc(T->n, sizeof(int)); double *len = calloc(T->n, sizeof(double));
    for (int i = 0; i < T->n; i++) {
        if (pdist(cur, T->t[i].pt) > best) break;
        ShL r; route_to(C, T->t[i].pt, S2, S4, &r, &fl[i]);
        ok[i] = r.n > 0; if (ok[i]) { len[i] = sh_len(r.s[0]); if (fl[i] && len[i] < best) best = len[i]; }
        sl_free(&r);
    }
    int k = -1;
    for (int pass = 0; pass < 2 && k == -1; pass++)
        for (int i = 0; i < T->n; i++) if ((pass == 0 ? fl[i] : ok[i]) && (k == -1 || len[i] < len[k])) k = i;
    if (k == -1) k = 0;
    tl_keep_only(T, k);
    free(ok); free(fl); free(len);
}
/* on by default (user decision N13-26, 2026-11-13); MCCE_ORD1=0 = the old nearest-point rule */
static int ord1_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_ORD1"); v = !(e && *e == '0'); } return v; }
/* FUN_180055190 (l.49464-49795): mode 1 */
static void order_mode1(Pock *C, const ShL *S1, const ShL *S2, const ShL *S4, TargL *T) {
    if (T->n < 2) return;
    P2 cur = p2(0, 0); last_pt(C, &cur);
    typedef struct { int shape; P2 P1, P2v, R; int ok, flag; double len; } Eti;
    Eti *e = calloc(T->n, sizeof(Eti));
    /* MCCE_ORD1 (default on, N13-26; =0 the old form): the points P1 / P2 are the FARTHEST points of the S1 piece / the route from the
       last point (FUN_18013caf0 at l.49561 / 49573, was the nearest), a target with no S1 shape keeps shape -1, the shortest
       route of the chosen shape replaces the pick only when strictly shorter (l.49652-49654) */
    int x1 = ord1_on();
    for (int i = 0; i < T->n; i++) {
        if (x1) e[i].shape = -1;
        Pos q; if (!sl_nearest(S1, T->t[i].pt, &q) || q.sh < 0) continue;
        e[i].shape = q.sh;
        int o = q.sh; while (o < S1->n && sh_island(S1->s[o])) o++; if (o >= S1->n) o = q.sh;   /* FUN_180053c60 (l.48804-48825) */
        Pos a; if (x1) sh_farthest(S1->s[o], cur, &a); else sh_nearest(S1->s[o], cur, &a); e[i].P1 = a.pt;
        ShL r; route_to(C, T->t[i].pt, S2, S4, &r, &e[i].flag); e[i].ok = r.n > 0;
        if (e[i].ok) { e[i].len = sh_len(r.s[0]); Pos b; if (x1) sh_farthest(r.s[0], cur, &b); else sh_nearest(r.s[0], cur, &b); e[i].P2v = b.pt; e[i].R = pdist2(cur, e[i].P1) <= pdist2(cur, e[i].P2v) ? e[i].P2v : e[i].P1; }
        sl_free(&r);
    }
    int k = -1;
    for (int pass = 0; pass < 2 && k == -1; pass++) {
        for (int i = 0; i < T->n; i++) if ((pass == 0 ? e[i].flag : e[i].ok) && (k == -1 || pdist2(cur, e[i].R) < pdist2(cur, e[k].R))) k = i;
        if (k != -1 && x1) { for (int i = 0; i < T->n; i++) if ((pass == 0 ? e[i].flag : e[i].ok) && e[i].shape == e[k].shape && e[i].len < e[k].len) k = i; }
        else if (k != -1) { int k2 = -1; for (int i = 0; i < T->n; i++) if ((pass == 0 ? e[i].flag : e[i].ok) && e[i].shape == e[k].shape && (k2 == -1 || e[i].len < e[k2].len)) k2 = i; k = k2; }
    }
    if (k == -1) {
        for (int i = 0; i < T->n; i++) if (k == -1 || pdist2(cur, e[i].P1) < pdist2(cur, e[k].P1)) k = i;
        if (k != -1) { for (int i = 0; i < T->n; i++) if (e[i].shape == e[k].shape) { k = i; break; } }
        if (k == -1) k = 0;
    }
    tl_keep_only(T, k);
    free(e);
}
/* FUN_180054080 (l.48969-49086): mode 0: keep the targets on the most recently created material piece */
static void order_mode0(Pock *C, TargL *T) {
    if (T->n < 2) return;
    int *key = malloc(T->n * sizeof(int)), mx = -1;
    for (int i = 0; i < T->n; i++) { Pos q; key[i] = sl_nearest(&C->mat, T->t[i].pt, &q) && q.sh >= 0 ? C->mat.s[q.sh]->tag : -1; if (key[i] > mx) mx = key[i]; }
    for (int i = T->n - 1; i >= 0; i--) if (key[i] != mx) { tl_erase(T, i); memmove(&key[i], &key[i + 1], (T->n - i) * sizeof(int)); }
    free(key);
}
/* FUN_18006e3f0 (l.62446-62452) */
static int order_mode(const Pock *C) { return C->P.order_mode; }   /* MOAT_FIRST never set (registry default) */
/* FUN_180052a90 (l.48317-48400) */
static void targets_build(Pock *C, const ShL *S1, const ShL *S2, const ShL *S4, int mode, TargL *T) {
    T->n = 0;
    int *used = malloc((S1->n + 1) * sizeof(int)), nused = 0;
    targets_boundary(C, &C->cur, S1, T, used, &nused);
    targets_s1(C, 0, &C->cur, S1, T, used, nused);
    free(used);
    g_sort_cur = p2(0, 0); last_pt(C, &g_sort_cur);
    qsort(T->t, T->n, sizeof(Targ), targ_cmp);   /* introsort FUN_18004d4b0 by squared distance (ties: order NOT DECODED) */
    mcce_trace_targets("GT pre", g_sort_cur, T);   /* ours: oracle trace */
    if (mode == 1) order_mode1(C, S1, S2, S4, T);   /* bIsCutOrdLenBas = 0 (FUN_180055c90 not used) */
    else if (mode == 2) order_mode2(C, S2, S4, T);
    else order_mode0(C, T);
    mcce_trace_targets("GT fin", g_sort_cur, T);   /* ours: oracle trace */
}

/* ============================================================================================ links (§3 D 2, §6.3) */
/* mcce::CalcMaxShHdChg (l.6767ff): largest heading change between consecutive non-degenerate entities */
static double max_hd_chg(const Sh *s) {
    double mx = 0; int have = 0; double last = 0;
    for (int i = 0; i < s->n; i++) {
        P2 a = e_tan(&s->e[i], 0), b = e_tan(&s->e[i], 1);
        if (fabs(a.x) <= 1e-12 && fabs(a.y) <= 1e-12) continue;
        if (have) { double d = fabs(wrap_pi(atan2(a.y, a.x) - last)); if (d > mx) mx = d; }
        last = atan2(b.y, b.x); have = 1;
    }
    return mx;
}
/* FUN_18011f480 (l.154721-155014) arc - line - arc connector with FUN_1800a96d0 / FUN_1800a97c0 tangent pairs */
static int tangent_pairs(P2 c1, double r1, P2 c2, double r2, P2 *p, P2 *q, int *type) {
    int swp = r1 < r2; if (swp) { P2 t = c1; c1 = c2; c2 = t; double x = r1; r1 = r2; r2 = x; }
    double ph = atan2(c2.y - c1.y, c2.x - c1.x), D = pdist(c1, c2); int n = 0;
#define E2(a) p2(cos(a), sin(a))
    if (r1 - r2 <= D + 1e-13) {
        double al = acos(fmin(1, (r1 - r2) / D));
        p[n] = padd(c1, pmul(E2(ph + al), r1)); q[n] = padd(c2, pmul(E2(ph + al), r2)); type[n++] = al <= 1e-12 ? 5 : 1;
        if (al > 1e-12) { p[n] = padd(c1, pmul(E2(ph - al), r1)); q[n] = padd(c2, pmul(E2(ph - al), r2)); type[n++] = 1; }
        if (r1 + r2 <= D + 1e-13) {
            double be = acos(fmin(1, (r1 + r2) / D));
            p[n] = padd(c1, pmul(E2(ph + be), r1)); q[n] = padd(c2, pmul(E2(ph + M_PI + be), r2)); type[n++] = be <= 1e-12 ? 6 : 2;
            if (be > 1e-12) { p[n] = padd(c1, pmul(E2(ph - be), r1)); q[n] = padd(c2, pmul(E2(ph + M_PI - be), r2)); type[n++] = 2; }
        }
    }
#undef E2
    if (swp) for (int i = 0; i < n; i++) { P2 t = p[i]; p[i] = q[i]; q[i] = t; }
    return n;
}
static int degen_arc(double r, double sw) { return fabs(r * sw) < 1e-5 || fabs(r * 2 * M_PI) - fabs(r * sw) < 1e-5; }   /* FUN_180123070 */
static int arc_line_arc(P2 Pp, P2 T, double r1, int d1, P2 Q, P2 V, double r2, int d2, Sh *out) {
    sh_clear(out); out->dir = 0;
    int ok; P2 n1 = unit(d1 == 1 ? p2(-T.y, T.x) : p2(T.y, -T.x), &ok); if (!ok) return 0;
    P2 n2 = unit(d2 == 1 ? p2(-V.y, V.x) : p2(V.y, -V.x), &ok); if (!ok) return 0;
    P2 c1 = padd(Pp, pmul(n1, r1)), c2 = padd(Q, pmul(n2, r2));
    P2 p[4], q[4]; int ty[4], n = tangent_pairs(c1, r1, c2, r2, p, q, ty);
    for (int k = 0; k < n; k++) {
        if (!(d1 == d2 ? (ty[k] & 1) : (ty[k] & 2))) continue;
        double cr = pcross(psub(p[k], c1), psub(q[k], c1)); int s = cr > 0 ? 1 : cr < 0 ? -1 : 0;
        if (!(s == d1 || ty[k] == 6)) continue;   /* a pair on the wrong side: the next one (l.154854 / 155007; the break at l.155004 is taken only for degenerate directions) */
        double a1 = atan2(Pp.y - c1.y, Pp.x - c1.x), sw1 = wrap_pi(atan2(p[k].y - c1.y, p[k].x - c1.x) - a1);
        if (d1 == 1 && sw1 < 0) sw1 += 2 * M_PI; if (d1 == -1 && sw1 > 0) sw1 -= 2 * M_PI;
        double a2 = atan2(q[k].y - c2.y, q[k].x - c2.x), sw2 = wrap_pi(atan2(Q.y - c2.y, Q.x - c2.x) - a2);
        if (d2 == 1 && sw2 < 0) sw2 += 2 * M_PI; if (d2 == -1 && sw2 > 0) sw2 -= 2 * M_PI;
        int g1 = degen_arc(r1, sw1), g2 = degen_arc(r2, sw2);
        if (!g1) sh_push(out, ent_arc(c1, r1, a1, sw1));
        if (ty[k] != 6) sh_push(out, ent_line(g1 ? Pp : p[k], g2 ? Q : q[k]));
        if (!g2) sh_push(out, ent_arc(c2, r2, a2, sw2));
        sh_upd_bbox(out);
        return out->n > 0;
    }
    return 0;
}
static void append_runin(Pock *C, const Targ *t, Sh *link) {   /* run-in section A -> B on C+0x340 (§6.3.0) */
    if (t->b22 && t->A.sh >= 0 && t->A.sh < C->cur.n) sh_section(C->cur.s[t->A.sh], t->A.ent, t->A.t, t->B.ent, t->B.t, link);
}
/* FUN_180046f80 (l.42211-42414) */
static void head_tail(const ShL *G, const Sh *p, double *head, double *tail, int *ok) {
    IsabL I = { 0 }; isect1(p, G, &I);
    int n = I.n + 2; int *ee = malloc(n * sizeof(int)); double *tt = malloc(n * sizeof(double)); int m = 0;
    ee[m] = 0; tt[m++] = 0.0; for (int i = 0; i < I.n; i++) { ee[m] = I.r[i].xe; tt[m++] = I.r[i].xt; } ee[m] = p->n - 1; tt[m++] = 1.0;
    int first = -1, last = -1; *ok = 1;
    int *cls = malloc(m * sizeof(int));
    for (int k = 0; k + 1 < m; k++) { cls[k] = sec_in_list(p, ee[k], tt[k], ee[k + 1], tt[k + 1], 0, G, 1e-5); if (cls[k] == 2) { if (first < 0) first = k; last = k; } }
    if (first < 0) *ok = 0; else for (int k = first; k <= last; k++) if (cls[k] != 2) *ok = 0;
    *head = first >= 0 ? sec_len(p, 0, 0.0, ee[first], tt[first]) : 0;
    *tail = last >= 0 ? sec_len(p, ee[last + 1], tt[last + 1], p->n - 1, 1.0) : 0;
    free(ee); free(tt); free(cls); il_free(&I);
}
static const int g_dpair[4][2] = { { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };   /* local_d8 (l.39216-39223) */
/* the shrink rule of the arc - line - arc connector loops (l.39544-39596) */
static int shrink_lists(int round, double *LA, int *nA, double *LB, int *nB) {
    if (!(round & 1) && *nA >= 2) { memmove(LA, LA + 1, (--*nA) * sizeof(double)); return 1; }
    if (!(round & 1) || *nB < 2) {
        if (*nA < 2) { if (*nB < 2) return 0; memmove(LB, LB + 1, (--*nB) * sizeof(double)); return 1; }
        memmove(LA, LA + 1, (--*nA) * sizeof(double)); return 1;
    }
    memmove(LB, LB + 1, (--*nB) * sizeof(double)); return 1;
}
/* FUN_1800410f0 (l.39088-39751) connector 2 / FUN_18003fec0 (l.38544-39084) connector 3 */
static int conn_arcs(Pock *C, int variant, const ShL *set, const ShL *S2, int exitFlag, const Sh *exit, const Sh *appr, Sh *link) {
    mcce_trace_ev(variant == 2 ? "conn2" : "conn3");   /* ours: MCCE_TRACE_SW */
    const McceParm *P = &C->P;
    if (!exit->n || !appr->n) return 0;
    P2 E, Te, A, Ta;
    if (variant == 2) { E = e_start(&exit->e[0]); Te = e_tan(&exit->e[0], 0); A = e_end(&appr->e[appr->n - 1]); Ta = e_tan(&appr->e[appr->n - 1], 1); }   /* pre (BIDIR FUN_18005e690) only with flags & 4: not ported */
    else { E = e_end(&exit->e[exit->n - 1]); Te = e_tan(&exit->e[exit->n - 1], 1); A = e_start(&appr->e[0]); Ta = e_tan(&appr->e[0], 0); }
    ShL G; if (!group_of(set, E, A, &G)) return 0;
    double LA[64], LB[64], Lmax, th[64]; int nA, nB;
    { double rr[64]; nA = radius_list(P, rr); memcpy(LA, rr, nA * sizeof(double)); memcpy(LB, rr, nA * sizeof(double)); nB = nA; cand_table(P, rr, th, &Lmax); }
    if (!P->var_rad_avail) { nA = 0; if (P->bull_avail && exitFlag) LA[nA++] = P->min_cut_rad; LA[nA++] = P->min_rad; nB = 0; LB[nB++] = P->min_rad; }
    Sh best, p; memset(&best, 0, sizeof best); memset(&p, 0, sizeof p); double bl = 0;
    for (int round = 0; ; round++) {
        for (int k = 0; k < 4; k++) {
            if (!arc_line_arc(E, Te, LA[0], g_dpair[k][0], A, Ta, LB[0], g_dpair[k][1], &p)) continue;
            if (variant == 2) {
                if (sh_vs_list(&p, S2, 1e-5) & 4) continue;
                double hd, tl; int ok; head_tail(&G, &p, &hd, &tl, &ok);
                if (!(hd <= Lmax && tl <= Lmax && ok)) continue;
            } else if (sh_vs_list(&p, &G, 1e-5) & 4) continue;
            double l = sh_len(&p);
            if (!best.n || l < bl) { sh_clear(&best); sh_append(&best, &p); bl = l; }
        }
        if (best.n) break;
        if (!shrink_lists(round, LA, &nA, LB, &nB)) break;
    }
    int r = best.n > 0;
    if (r) { sh_clear(link); if (variant == 2) sh_append(link, &best); else { sh_append(link, exit); sh_append(link, &best); sh_append(link, appr); } }
    free(best.e); free(p.e);
    return r;
}
/* FUN_180044340 (l.40706-40879): exit + routed connector + approach (FUN_1800f0fa0: the guided planner FUN_1800f1080 with
   MCCE_GUIDED=1 - off by default, see mcce_port_plan.md §5 item 19 - else its plain-route fallback FUN_1800f1580) */
static int conn_route(Pock *C, const ShL *S4, const Sh *exit, const Sh *appr, Sh *link) {
    mcce_trace_ev("routed_conn");   /* ours: MCCE_TRACE_SW */
    if (!exit->n || !appr->n) return 0;
    P2 E = e_end(&exit->e[exit->n - 1]), A = e_start(&appr->e[0]);
    ShL G; if (!group_of(S4, E, A, &G)) return 0;
    /* FUN_1800f0fa0: planner first (MCCE_GUIDED=1); d = 0.9 C+0x1d0 (= P+0x1c8), step C+0x1e0; E / A as the caller
       builds them (l.40780-40835), E+0x10 = FUN_18011ffd0(exit, last, 1.0, 1e-10) (disasm 0x180044513-0x180044532),
       A+0x10 = FUN_1801204a0(approach, 0, 0.0, 1e-10) (l.40837), both raw */
    ShL r = { 0 };
    if (!(guided_on() && guided_plan(0.9 * C->P.d1c8, C->P.var_rad, 1, gp_raw_end(&exit->e[exit->n - 1]), gp_tan_in(exit, exit->n - 1, 1.0, 1.0000000000000002e-10),
                                     gp_raw_start(&appr->e[0]), gp_tan_out(appr, 0, 0.0, 1.0000000000000002e-10), &G, &r, NULL) && r.n == 1)) {
        sl_free(&r); Ent ln = ent_line(E, A); route(1, &G, &ln, &r);   /* FUN_1800f0fa0 (l.130591-130606): else FUN_1800f1580 */
    }
    int ok = r.n == 1;
    if (ok) { sh_clear(link); sh_append(link, exit); sh_append(link, r.s[0]); sh_append(link, appr); }
    sl_free(&r); return ok;
}
/* ============================================================================================ contour-guide links (P+0x261) */
/* bUseCtourGuide = 1 (CDynPockParmDer +0x261, read in FUN_180069840 l.60377; used only at l.40016, 40551, 54845): connectors 4
   (FUN_180042890) and 5 (FUN_180043c90) route an arc - line - arc connector / the straight line with FUN_18010c040 and smooth the
   result, strategy (5) routes the straight line (formats/mcce_engines.md §6.3.3 / §6.3.5 / §6.3.7). Default on: user decision
   D-3 (parm_derive). */

/* ---- self intersections FUN_18012f3b0(sh, list, 0) (l.163223-163303) */
typedef struct { P2 pt; int ent; double t; int partner; } Isis;   /* ISIS 0x38: +0x00 point, +0x10 entity, +0x18 t, +0x30 partner */
typedef struct { Isis *r; int n, cap; } IsisL;
static void ctis_push(IsisL *l, Isis a) { if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 16; l->r = realloc(l->r, l->cap * sizeof(Isis)); } l->r[l->n++] = a; }
static _Thread_local const Isis *g_isis_base;
/* FUN_18012e7d0 (l.162848-162890; its decompile lost the returns, read as): entity ascending; then, when the points differ by
   more than 1e-15 in x or y, t ascending; then the partners' entity and t ascending */
static int isis_cmp(const void *x, const void *y) {
    const Isis *a = &g_isis_base[*(const int *)x], *b = &g_isis_base[*(const int *)y];
    if (a->ent != b->ent) return a->ent < b->ent ? -1 : 1;
    if (1e-15 < fabs(a->pt.x - b->pt.x) || 1e-15 < fabs(a->pt.y - b->pt.y)) { if (a->t < b->t) return -1; if (b->t < a->t) return 1; }
    const Isis *pa = &g_isis_base[a->partner], *pb = &g_isis_base[b->partner];
    if (pa->ent != pb->ent) return pa->ent < pb->ent ? -1 : 1;
    return pa->t < pb->t ? -1 : pb->t < pa->t ? 1 : 0;
}
/* mode (FUN_18012f3b0's 3rd argument, passed to FUN_18012f620): 0 every crossing; 2 return after the first recorded pair
   (l.163434-163436; FUN_18012f2b0 uses it). Mode 1 (the next four entities only) is not used by the planner. */
static void self_isect_m(const Sh *s, IsisL *out, int mode) {
    out->n = 0;
    int n = s->n; double (*bb)[4] = malloc((n ? n : 1) * sizeof *bb);
    for (int k = 0; k < n; k++) gp_ent_minmax(&s->e[k], bb[k]);
    /* FUN_18012f620 (l.163304-163462): entity pairs i < j with boxes within 1e-5, CalcEntIntersect(1e-7), hits last first; a hit
       whose section between the two positions is short (FUN_18012ff20 = FUN_18014b720 both ways, 1e-6) is a junction, not a
       crossing; else two records, each the other's partner */
    for (int i = 0; i + 1 < n; i++) for (int j = i + 1; j < n; j++) {
        if (!gp_box_ov(bb[i], bb[j])) continue;
        double ti[2], tj[2]; P2 pt[2]; int k = gp_ent_isect(1e-07, &s->e[i], &s->e[j], ti, tj, pt);
        for (int h = k - 1; h >= 0; h--) {
            if (short_sec(s, i, ti[h], j, tj[h], 1e-06) || short_sec(s, j, tj[h], i, ti[h], 1e-06)) continue;
            int c = out->n; Isis A = { pt[h], i, ti[h], c + 1 }, B = { pt[h], j, tj[h], c }; ctis_push(out, A); ctis_push(out, B);
            if (mode == 2) goto first;
        }
    }
first:
    free(bb);
    if (!out->n) return;
    /* FUN_180130060 (l.163592-163653): index list sorted by FUN_18012e7d0 (qsort_s) */
    int m = out->n, *idx = malloc(m * sizeof(int)); for (int k = 0; k < m; k++) idx[k] = k;
    g_isis_base = out->r; gp_msqsort(idx, m, sizeof(int), isis_cmp);
    /* FUN_18012ed20 (l.163020-163186): neighbours (cyclic) at the same point (squared 1e-14) on a short section (1e-7) whose
       partners are on a short section too: the record and its partner leave the list (the same crossing found twice) */
    if (m > 1) for (int k = 0; k < m; k++) {
        const Isis *a = &out->r[idx[k]], *b = &out->r[idx[(k + 1) % m]];
        if (!(pdist2(a->pt, b->pt) <= 9.999999999999998e-15 && (short_sec(s, a->ent, a->t, b->ent, b->t, 1e-07) || short_sec(s, b->ent, b->t, a->ent, a->t, 1e-07)))) continue;
        const Isis *pa = &out->r[a->partner], *pb = &out->r[b->partner];
        if (!(short_sec(s, pa->ent, pa->t, pb->ent, pb->t, 1e-07) || short_sec(s, pb->ent, pb->t, pa->ent, pa->t, 1e-07))) continue;
        int q = 0; while (q < m && idx[q] != a->partner) q++;
        if (q >= m) continue;
        memmove(&idx[k], &idx[k + 1], (m - k - 1) * sizeof(int)); m--;
        int q2 = k <= q ? q - 1 : q;
        memmove(&idx[q2], &idx[q2 + 1], (m - q2 - 1) * sizeof(int)); m--;
        k -= q < k - 1 ? 2 : 1;
        if (m < 1) break;
    }
    /* FUN_18012e930 (l.162891-163019): records in list order, partners renumbered. (FUN_18012fc00 l.163463-163544 then
       re-sorts the (entity, t) pairs and writes the t values back: the identity on a list sorted by entity and t - not ported) */
    Isis *r = malloc((m ? m : 1) * sizeof(Isis)); int *nw = calloc(out->n, sizeof(int));
    for (int k = 0; k < m; k++) { r[k] = out->r[idx[k]]; nw[idx[k]] = k; }
    for (int k = 0; k < m; k++) r[k].partner = nw[r[k].partner];
    free(out->r); out->r = r; out->n = out->cap = m; free(nw); free(idx);
}
/* FUN_1800457d0 (l.41420-41445): the route crosses itself */
static void self_isect(const Sh *s, IsisL *out) { self_isect_m(s, out, 0); }
/* FUN_18012f2b0 (l.163187-163222): FUN_18012f3b0(shape, list, 2) - the shape crosses itself when the list is not empty */
static int gp_self_cross(const Sh *s) { IsisL I = { 0 }; self_isect_m(s, &I, 2); int r = I.n > 0; free(I.r); return r; }
static int has_self_isect(const Sh *s) { IsisL I = { 0 }; self_isect(s, &I); int r = I.n > 0; free(I.r); return r; }
/* FUN_180132200(tol, sh) (l.164690-164790): loops cut out at the self crossings. FUN_180131740 (l.164328-164353): after
   pushing the piece i (record i -> i + 1), a stacked piece that starts at the partner of record i + 1 closes a loop: it and
   every later piece leave the stack. FUN_180131240 (l.164180-164313): sh[start -> rec 0] + the stacked pieces + sh[rec last ->
   end], open. NOT DECODED: FUN_1800c7f00 (l.108013-108052: FUN_1801239f0 / FUN_180126ac0 checks that may drop the result) -
   a non-empty result is kept. */
static void remove_loops(double tol, Sh *s) {
    (void)tol;
    IsisL I = { 0 }; self_isect(s, &I);
    if (!I.n) { free(I.r); return; }
    int *st = malloc(I.n * sizeof(int)), ns = 0;
    for (int i = 0; i < I.n - 1; i++) {
        st[ns++] = i;
        int target = I.r[(st[ns - 1] + 1) % I.n].partner, k = ns - 1;
        while (k >= 0 && st[k] != target) k--;
        if (k >= 0) ns = k;
    }
    Sh o; memset(&o, 0, sizeof o);
    sh_section(s, 0, 0.0, I.r[0].ent, I.r[0].t, &o);
    for (int k = 0; k < ns; k++) sh_section(s, I.r[st[k]].ent, I.r[st[k]].t, I.r[st[k] + 1].ent, I.r[st[k] + 1].t, &o);
    sh_section(s, I.r[I.n - 1].ent, I.r[I.n - 1].t, s->n - 1, 1.0, &o);
    if (o.n) { free(s->e); s->e = o.e; s->n = o.n; s->cap = o.cap; s->dir = 0; sh_upd_bbox(s); } else free(o.e);
    free(st); free(I.r);
}
/* FUN_1800458c0 (l.41482-41563): the candidate leaves like the exit (FUN_1801204a0 at (0, 0.0)) and arrives like the approach
   (FUN_18011ffd0 at (last, 1.0)) within 1 deg (tangents with both components <= 1e-5 fail), and turns by at most 1 deg at
   every junction (CalcMaxShHdChg) */
static int ends_aligned(const Sh *exit, const Sh *appr, const Sh *cand) {
    const double tol2 = 1.0000000000000002e-10, one = 0.017453292519943295;
    P2 a = gp_tan_out(exit, 0, 0.0, tol2), b = gp_tan_out(cand, 0, 0.0, tol2);
    if (!(1e-05 < fabs(b.x) || 1e-05 < fabs(b.y))) return 0;
    double d = atan2(b.y, b.x);
    if (!(1e-05 < fabs(a.x) || 1e-05 < fabs(a.y))) return 0;
    d -= atan2(a.y, a.x);
    if (-M_PI <= d) { if (M_PI < d) d -= 2 * M_PI; } else d += 2 * M_PI;
    if (!(fabs(d) <= one)) return 0;
    a = gp_tan_in(appr, appr->n - 1, 1.0, tol2); b = gp_tan_in(cand, cand->n - 1, 1.0, tol2);
    if (!(1e-05 < fabs(b.x) || 1e-05 < fabs(b.y))) return 0;
    double e = atan2(b.y, b.x);
    if (!(1e-05 < fabs(a.x) || 1e-05 < fabs(a.y))) return 0;
    e -= atan2(a.y, a.x);
    if (-M_PI <= e) { if (M_PI < e) e -= 2 * M_PI; } else e += 2 * M_PI;
    if (!(fabs(e) <= one)) return 0;
    return fabs(max_hd_chg(cand)) <= one;
}

/* ---- MCCECalcContourOffset(1, d, {shape}, out, 0x101, 0.0, -1.0, 1e-6, 1e-6) (export l.8571-8807; the call of FUN_180048840,
   l.43188-43191): viewer/mcce_ctour.c mcce_contour_offset, the DLL-exact port (formats/mcce_ctour_offset.md). With gap -1 and bit 1
   FUN_1800da1c0 opens every shape (l.117956-117971), so the input's dir does not matter; bit 0x100 is the vanishing-arc limit
   |d| + 1e-15 and no FUN_1800d8d90 tail; no 0x20 (self-crossing split) / 0x20000 / 0x40000. Unit oracle: tools/tests/coffct_oracle.sh
   (the DLL's own pass inputs / outputs at the hooked call 0x180048a11, CT_COFF=1). Was ours (a raw one-sided offset with cutting
   circles, kept radius-0 arcs the DLL drops: ct oracle buckle-v8 op 42 x12). The output entities are new (the DLL carries only the
   +0x60 source link on offset entities); a refused input fails the pass (no stand-in). */
static int oc_offset(const Sh *in, double d, ShL *out) {
    McceShape m; memset(&m, 0, sizeof m);
    m.e = calloc((size_t)(in->n ? in->n : 1), sizeof(McceEnt)); m.n = in->n; m.dir = in->dir; m.isl = in->fl & 1;
    for (int k = 0; k < in->n; k++) {
        const Ent *e = &in->e[k]; McceEnt *q = &m.e[k];
        q->id = e->id; q->src_sh = 0; q->src_ent = k;
        if (e->id == 'A') { q->a[0] = e->c.x; q->a[1] = e->c.y; q->a[2] = e->r; q->a[3] = e->sa; q->a[4] = e->sw; }
        else { q->a[0] = e->p1.x; q->a[1] = e->p1.y; q->a[2] = e->p2.x; q->a[3] = e->p2.y; }
    }
    McceResult r = mcce_contour_offset(&m, 1, d, 0x101, 0.0, -1.0, 1e-06, 1e-06);
    free(m.e);
    if (r.refused) {
        if (dbg_level()) fprintf(stderr, "mcce: MCCECalcContourOffset (link smoothing) refused (%s)\n", r.refused);
        return 0;
    }
    for (int i = 0; i < r.n; i++) {
        Sh *sh = sh_new(); sh->dir = r.s[i].dir; sh->fl = r.s[i].isl;
        for (int k = 0; k < r.s[i].n; k++) {
            const McceEnt *q = &r.s[i].e[k];
            Ent e = q->id == 'A' ? ent_arc(p2(q->a[0], q->a[1]), q->a[2], q->a[3], q->a[4]) : ent_line(p2(q->a[0], q->a[1]), p2(q->a[2], q->a[3]));
            sh_push(sh, e);
        }
        sh_upd_bbox(sh);
        sl_push(out, sh);
    }
    mcce_shapes_free(&r);
    return 1;
}
/* FUN_180048f60 (l.43380-43473): the longest open shape (lengths compared with > from -1), else the longest closed one, stays
   alone in the list; 0 for an empty list */
static int keep_longest(ShL *L) {
    int io = -1, ic = -1; double bo = -1.0, bc = -1.0;
    for (int i = 0; i < L->n; i++) { double l = sh_len(L->s[i]); if (L->s[i]->dir == 0) { if (l > bo) { bo = l; io = i; } } else if (l > bc) { bc = l; ic = i; } }
    int k = io != -1 ? io : ic;
    if (k == -1) return 0;
    Sh *s = L->s[k]; L->s[k] = NULL;
    for (int i = 0; i < L->n; i++) if (L->s[i]) sh_free(L->s[i]);
    L->n = 0; sl_push(L, s);
    return 1;
}
/* FUN_180049150 (l.43474-43714): ends of an opened / closed route put back on the original: a moved start must lie on orig
   (nearest point FUN_18013d200 within squared 1e-12) and orig[start -> it] goes in front; a moved end likewise, orig[it ->
   end] behind; a non-empty result replaces res and sets *changed. 0 when an end left the original. */
static int reattach_ends(const Sh *orig, Sh *res, int *changed) {
    P2 S0 = gp_raw_start(&orig->e[0]), E0 = gp_raw_end(&orig->e[orig->n - 1]), S1 = gp_raw_start(&res->e[0]), E1 = gp_raw_end(&res->e[res->n - 1]);
    Sh t; memset(&t, 0, sizeof t);
    if (1e-12 < (S1.x - S0.x) * (S1.x - S0.x) + (S1.y - S0.y) * (S1.y - S0.y)) {
        Pos q; sh_nearest(orig, S1, &q);
        if ((S1.y - q.pt.y) * (S1.y - q.pt.y) + (S1.x - q.pt.x) * (S1.x - q.pt.x) > 1e-12) { free(t.e); return 0; }
        sh_section(orig, 0, 0.0, q.ent, q.t, &t); sh_append(&t, res);
    }
    if (1e-12 < (E1.y - E0.y) * (E1.y - E0.y) + (E1.x - E0.x) * (E1.x - E0.x)) {
        Pos q; sh_nearest(orig, E1, &q);
        if ((E1.y - q.pt.y) * (E1.y - q.pt.y) + (E1.x - q.pt.x) * (E1.x - q.pt.x) > 1e-12) { free(t.e); return 0; }
        if (!t.n) sh_append(&t, res);
        sh_section(orig, q.ent, q.t, orig->n - 1, 1.0, &t);
    }
    if (t.n) { free(res->e); res->e = t.e; res->n = t.n; res->cap = t.cap; sh_upd_bbox(res); *changed = 1; } else free(t.e);
    return 1;
}
/* FUN_180048840 (l.43097-43249): four MCCECalcContourOffset passes d, -d, -d, d on {copy(cur)}, each followed by FUN_180048f60;
   after passes 1 and 3 an open input gets its ends back (FUN_180049150 against cur). 0 on any failure (disasm 0x180048a21 /
   0x180048a48 -> 0x180048ae4) */
static int open_close(double d, const Sh *cur, Sh *out, int *changed) {
    const double dl[4] = { d, -d, -d, d };
    *changed = 0;
    ShL L = { 0 }; sl_push(&L, sh_copy(cur));
    for (int k = 0; k < 4; k++) {
        ShL R = { 0 }; int ok = oc_offset(L.s[0], dl[k], &R);
        if (g_ct_f && getenv("CT_COFF")) { fprintf(g_ct_f, "stage coffd %.17g\n", dl[k]); ct_stage_sl("coffin", &L); ct_stage_sl("coff", &R); }
        ok = ok && keep_longest(&R);
        if (ok && (k == 1 || k == 3) && cur->dir == 0) {
            ok = reattach_ends(cur, R.s[0], changed);
            if (g_ct_f && getenv("CT_SMDBG")) fprintf(g_ct_f, "stage reat %d %d\n", ok, *changed);
        }
        sl_free(&L); L = R;
        if (!ok) { sl_free(&L); return 0; }
    }
    sh_clear(out); sh_append(out, L.s[0]); out->dir = L.s[0]->dir;
    sl_free(&L);
    return 1;
}
/* FUN_180048c70 (l.43250-43369, disasm 0x180048d40-0x180048da3): from distance index i: an unchanged pass ends (its result);
   a changed one is the input of the next distance; a failure fails; *iout = the last index used */
static int smooth_from(const double *dl, int nd, int i, int *iout, const Sh *sec, Sh *out) {
    Sh *cur = sh_copy(sec), o; memset(&o, 0, sizeof o);
    *iout = i;
    while (*iout < nd) {
        int ch;
        if (!open_close(dl[*iout], cur, &o, &ch)) { sh_free(cur); free(o.e); return 0; }
        if (!ch) { sh_clear(out); sh_append(out, &o); out->dir = o.dir; sh_free(cur); free(o.e); return 1; }
        sh_clear(cur); sh_append(cur, &o); cur->dir = o.dir;
        (*iout)++;
    }
    sh_clear(out); sh_append(out, cur); out->dir = cur->dir;
    sh_free(cur); free(o.e);
    return 1;
}
static int cs_section_sel(const Sh *sh, const ShL *S, int *nI, int se[2], double st[2]);   /* FUN_180048320, below */
/* FUN_18004a070 (l.44002-44486, disasm 0x18004a6e0-0x18004a815): link smoothing. m = P+0x21e ? P+0x1d8 : P+0x40; the section of
   FUN_180048320; distances 10m, 4m, 2m, 1.125m, m, 0.9m .. 0.1m (0.9 lowered by 0.1 nine times, l.44295-44312); from i = 0:
   FUN_180048c70, the first result without a piece outside CONT (FUN_180113300 & 4, 1e-5) replaces the section and ends it,
   else i = (last index used) + 1 */
static void link_smooth(Pock *C, const ShL *CONT, Sh *link) {
    mcce_trace_ev("link_smooth");   /* ours: MCCE_TRACE_SW */
    const McceParm *P = &C->P;
    double m = P->var_rad_avail ? P->var_rad : P->min_rad;
    int nI = 0, se[2] = { 0, 0 }; double st[2] = { 0, 0 };
    if (!link->n || !cs_section_sel(link, CONT, &nI, se, st)) return;
    Sh sec; memset(&sec, 0, sizeof sec);
    if (nI == 0) sh_append(&sec, link); else sh_section(link, se[0], st[0], se[1], st[1], &sec);
    if (sec.n) {
        double dl[14]; int nd = 0; dl[nd++] = m * 10.0; dl[nd++] = m * 4.0; dl[nd++] = m * 2.0; dl[nd++] = m * 1.125; dl[nd++] = m;
        double f = 0.9; for (int k = 9; 0 < k; k--) { dl[nd++] = f * m; f = f + -0.1; }
        Sh out; memset(&out, 0, sizeof out);
        for (int i = 0; i < nd; ) {
            int iout = i;
            int okf = smooth_from(dl, nd, i, &iout, &sec, &out);
            if (g_ct_f && getenv("MCCE_CTDBG")) fprintf(g_ct_f, "# smooth i %d -> iout %d ok %d n %d vs %d\n", i, iout, okf, out.n, okf ? sh_vs_list(&out, CONT, 1e-05) : -1);
            g_vs_dbg = g_ct_f && getenv("CT_SMDBG");
            int vs = okf ? sh_vs_list(&out, CONT, 1e-05) : 0;
            g_vs_dbg = 0;
            if (okf && g_ct_f && getenv("CT_SMDBG")) fprintf(g_ct_f, "stage vs %d\n", vs);
            if (okf && !(vs & 4)) {
                if (nI == 0) { sh_clear(link); sh_append(link, &out); link->dir = out.dir; }
                else { Sh t; memset(&t, 0, sizeof t); sh_section(link, 0, 0.0, se[0], st[0], &t); sh_append(&t, &out); sh_section(link, se[1], st[1], link->n - 1, 1.0, &t);
                       free(link->e); link->e = t.e; link->n = t.n; link->cap = t.cap; link->dir = 0; sh_upd_bbox(link); }
                break;
            }
            i = iout + 1;
        }
        free(out.e);
    }
    free(sec.e);
}
/* FUN_180049950 (l.43715-44001): mid = len(link) - len(exit) - len(appr); if mid >= 1e-4: a = min(len(exit), 0.49 mid), b =
   min(len(appr), 0.49 mid); the link split at len(exit) + a from its start (FUN_18006f1c0) and len(appr) + b from its end
   (FUN_18006f4f0); the first and the last piece smoothed (FUN_18004a070 with CONT), the three joined again */
static void junction_smooth(Pock *C, const ShL *CONT, const Sh *exit, const Sh *appr, Sh *link) {
    mcce_trace_ev("junction_smooth");   /* ours: MCCE_TRACE_SW */
    double lx = sh_len(exit), la = sh_len(appr), mid = sh_len(link) - lx - la, h = mid * 0.49;
    double a = lx < h ? lx : h, b = la < h ? la : h;
    if (!(0.0001 <= mid) || !link->n) return;
    Sh *lp = link; ShL L = { &lp, 1, 1 };
    Pos s0 = { 0, 0, 0.0, gp_raw_start(&link->e[0]) }, e0 = { 0, link->n - 1, 1.0, gp_raw_end(&link->e[link->n - 1]) }, pa, pb;
    if (!walk_fwd(0, a + lx, &L, &s0, &pa, NULL)) return;
    if (!walk_back(0, b + la, &L, &e0, &pb, NULL)) return;
    Sh p1, p2_, p3; memset(&p1, 0, sizeof p1); memset(&p2_, 0, sizeof p2_); memset(&p3, 0, sizeof p3);
    sh_section(link, s0.ent, s0.t, pa.ent, pa.t, &p1);
    sh_section(link, pa.ent, pa.t, pb.ent, pb.t, &p2_);
    sh_section(link, pb.ent, pb.t, link->n - 1, 1.0, &p3);
    link_smooth(C, CONT, &p1); link_smooth(C, CONT, &p3);
    sh_clear(link); sh_append(link, &p1); sh_append(link, &p2_); sh_append(link, &p3);
    free(p1.e); free(p2_.e); free(p3.e);
}
/* FUN_180045c40 (l.41567-42210, disasm 0x1800463d0-0x1800465ef): in the FUN_180048320 section, every arc between the first and
   the last non-arc entity is replaced by the chords of ApxArcWithLnSegs(0.0001, arc, pts, true) unless |sw| > pi/2 and
   |r sw| > 1e-4, or |r sw| > m pi (m = P+0x21e ? P+0x1d8 : P+0x40) and FUN_1800ab630 gives more than one segment; a section
   of arcs only is left alone */
static void link_lin_arcs(Pock *C, const ShL *CONT, Sh *link) {
    mcce_trace_ev("link_lin_arcs");   /* ours: MCCE_TRACE_SW */
    const McceParm *P = &C->P;
    double m = P->var_rad_avail ? P->var_rad : P->min_rad;
    int nI = 0, se[2] = { 0, 0 }; double st[2] = { 0, 0 };
    if (!link->n || !cs_section_sel(link, CONT, &nI, se, st)) return;
    Sh sec; memset(&sec, 0, sizeof sec);
    if (nI == 0) sh_append(&sec, link); else sh_section(link, se[0], st[0], se[1], st[1], &sec);
    int first = 0; while (first < sec.n && sec.e[first].id == 'A') first++;
    if (first == sec.n) { free(sec.e); return; }
    int last = sec.n - 1; while (last >= 0 && sec.e[last].id == 'A') last--;
    Sh o; memset(&o, 0, sizeof o);
    for (int k = 0; k < sec.n; k++) {
        const Ent *a = &sec.e[k];
        if (k < first || k > last || a->id != 'A') { sh_push(&o, *a); continue; }
        double sw = fabs(a->sw), rsw = fabs(a->r * a->sw);
        if (1.5707963267948966 < sw && 0.0001 < rsw) { sh_push(&o, *a); continue; }
        /* FUN_1800ab630 (l.93134-93184) segment count = ApxArcWithLnSegs' (mcce_medax.c arc_chords) */
        int n; double tol = 0.0001, dth;
        if (1e-12 <= a->r) {
            if (tol <= a->r) { dth = acos(1.0 - tol / a->r) * 2.0; if (1e-12 <= dth) { if (1.5707963267948966 < dth) dth = 1.5707963267948966; } else dth = 1e-12; }
            else dth = 1.5707963267948966;
            double acc = 0.0;
            for (double x = sw; 0.0 < x; x += -3.141592653589793) acc += (3.141592653589793 < x ? 3.141592653589793 : x) / dth;
            n = (int)(acc + 1.0); if (n < 1) n = 1;
        } else n = 1;
        if (m * 3.141592653589793 < rsw && n != 1) { sh_push(&o, *a); continue; }
        P2 q = p2(cos(a->sa) * a->r + a->c.x, sin(a->sa) * a->r + a->c.y); double ang = a->sa;
        for (int j = 1; j <= n; j++) { ang = a->sw / (double)n + ang; P2 r = p2(cos(ang) * a->r + a->c.x, sin(ang) * a->r + a->c.y); sh_push(&o, ent_line(q, r)); q = r; }
    }
    if (nI == 0) { sh_clear(link); sh_append(link, &o); }
    else { Sh t; memset(&t, 0, sizeof t); sh_section(link, 0, 0.0, se[0], st[0], &t); sh_append(&t, &o); sh_section(link, se[1], st[1], link->n - 1, 1.0, &t);
           free(link->e); link->e = t.e; link->n = t.n; link->cap = t.cap; sh_upd_bbox(link); }
    free(o.e); free(sec.e);
}
/* ours: connector unit oracle (tools/peloader --ct, tools/gp_cmp.py PORT DLL). MCCE_CTDUMP=PREFIX: every connector 4 / 5 attempt with
   P+0x261 set writes its input to PREFIX.N ("CTIN 1": the CDynPockParmDer fields by offset, exitFlag, exit, approach, S2, S3,
   S4, exact %a) and its stages to PREFIX.N.port in the format the loader prints for the DLL's (hooked call sites) */
static _Thread_local int g_ct_n;
static void ct_sh(FILE *f, const char *tag, const Sh *s) { fprintf(f, "%s %d %d %d\n", tag, s->dir, s->fl & 1, s->n); for (int k = 0; k < s->n; k++) gp_dump_ent(f, 1, &s->e[k]); }
static void ct_set(FILE *f, const char *nm, const ShL *L) { fprintf(f, "set %s %d\n", nm, L->n); for (int i = 0; i < L->n; i++) ct_sh(f, "s", L->s[i]); }
static void ct_stage_sl(const char *st, const ShL *L) { if (!g_ct_f) return; FILE *o = g_gp_f; g_gp_f = g_ct_f; gp_dump_sl(st, L); g_gp_f = o; }
static void ct_stage_sh(const char *st, const Sh *s) { if (!g_ct_f) return; Sh *p = (Sh *)s; ShL L = { &p, 1, 1 }; ct_stage_sl(st, &L); }
static void ct_stage_v(const char *st, double v) { if (g_ct_f) fprintf(g_ct_f, "stage %s %.17g\n", st, v); }
static void ct_dump_open(const McceParm *P, int exitFlag, const Sh *exit, const Sh *appr, const ShL *S2, const ShL *S3, const ShL *S4) {
    const char *pre = getenv("MCCE_CTDUMP");
    if (!pre || !*pre) return;
    char path[4096]; snprintf(path, sizeof path, "%s.%d", pre, g_ct_n++);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "CTIN 1\nu32 0x8 %u\n", P->flags);
    const struct { int off; double v; } d[] = {
        { 0x28, P->R }, { 0x30, P->r }, { 0x38, P->eng }, { 0x40, P->min_rad }, { 0x48, P->micro_lift }, { 0x50, P->approach },
        { 0x58, P->sliver }, { 0x60, P->first_off }, { 0x68, P->plunge_ang }, { 0x70, P->pitch }, { 0x78, P->top }, { 0x80, P->depth },
        { 0x88, P->zclear }, { 0x90, P->helix_r }, { 0x98, P->troch_r }, { 0xa0, P->slot_add }, { 0xa8, P->fp_red },
        { 0xe0, P->gap }, { 0x110, P->chain_tol }, { 0x1b8, P->ramp_len }, { 0x1c0, P->d1c0 }, { 0x1c8, P->d1c8 }, { 0x1d0, P->d1d0 },
        { 0x1d8, P->var_rad }, { 0x1e0, P->step }, { 0x1e8, P->step15 }, { 0x1f0, P->pi_minrad }, { 0x1f8, P->s_work }, { 0x200, P->s_10 },
        { 0x208, P->s_170 }, { 0x210, P->s_025 }, { 0x228, P->min_cut_rad }, { 0x230, P->rend_ang }, { 0x240, P->air_dwell },
        { 0x250, P->dis_trans_ovr }, { 0x258, P->dis_trans_tot } };
    for (size_t i = 0; i < sizeof d / sizeof *d; i++) fprintf(f, "f64 %#x %a\n", d[i].off, d[i].v);
    const struct { int off; int v; } b[] = { { 0x1b0, P->climb }, { 0x21c, P->reg_read }, { 0x21d, P->assert_on }, { 0x21e, P->var_rad_avail },
        { 0x21f, P->bull_avail }, { 0x220, P->write_matmod }, { 0x238, P->bands }, { 0x239, P->cut_ord_len }, { 0x23a, P->air_cut_v3 },
        { 0x23b, P->rate_of_eng }, { 0x260, P->old_gsl_cut_ord }, { 0x261, P->ctour_guide } };
    for (size_t i = 0; i < sizeof b / sizeof *b; i++) fprintf(f, "u8 %#x %d\n", b[i].off, b[i].v);
    fprintf(f, "i32 0x248 %d\ni32 0x24c %d\nexitflag %d\n", P->call_num, P->num_red_pass, exitFlag);
    ct_sh(f, "exit", exit); ct_sh(f, "appr", appr); ct_set(f, "S2", S2); ct_set(f, "S3", S3); ct_set(f, "S4", S4);
    fprintf(f, "end\n"); fclose(f);
    strncat(path, ".port", sizeof path - strlen(path) - 1);
    g_ct_f = fopen(path, "w");
}
static void ct_dump_close(int r, const Sh *link, double cr) {
    if (!g_ct_f) return;
    fprintf(g_ct_f, "stage result %d\nstage cr %.17g\n", r, cr); ct_stage_sh("link", link);
    fclose(g_ct_f); g_ct_f = NULL;
}
/* FUN_180042890 (l.39816-40438), P+0x261 set: exit end (point, tangent) -> approach start; G = FUN_180047920(S4, E, A); radius
   lists as FUN_1800410f0; per direction pair the arc - line - arc connector routed through G (FUN_18010c040, both ways); one
   shape without self crossing (FUN_1800457d0) -> cr = its off-line length, FUN_18004a070(S3), cand = exit + route + approach,
   FUN_180132200(1e-6), kept when FUN_1800458c0 holds and shorter; lists shrunk as connector 2 until something is kept; then
   FUN_180045c40(S3) */
static int conn_ctour4(Pock *C, const ShL *S3, const ShL *S4, int exitFlag, const Sh *exit, const Sh *appr, Sh *link, double *cr) {
    mcce_trace_ev("conn4");   /* ours: MCCE_TRACE_SW */
    const McceParm *P = &C->P;
    sh_clear(link); link->dir = 0; *cr = 0;
    if (!exit->n || !appr->n) return 0;
    double LA[64], LB[64]; int nA = radius_list(P, LA), nB = nA; memcpy(LB, LA, nA * sizeof(double));
    const Ent *xe = &exit->e[exit->n - 1], *ae = &appr->e[0];
    P2 E = gp_raw_end(xe), Te = gp_tan_vec(xe, 1.0), A = gp_raw_start(ae), Ta = gp_tan_vec(ae, 0.0);
    ShL G; if (!group_of(S4, E, A, &G)) return 0;
    if (!P->var_rad_avail) { nA = 0; if (P->bull_avail && exitFlag) LA[nA++] = P->min_cut_rad; LA[nA++] = P->min_rad; nB = 0; LB[nB++] = P->min_rad; }
    if (!nA || !nB) return 0;
    Sh p, cand; memset(&p, 0, sizeof p); memset(&cand, 0, sizeof cand); double best = 0;
    for (int round = 0; ; round++) {
        for (int k = 0; k < 4; k++) {
            if (!arc_line_arc(E, Te, LA[0], g_dpair[k][0], A, Ta, LB[0], g_dpair[k][1], &p)) { if (g_ct_f) fprintf(g_ct_f, "stage ala 0\n"); continue; }
            ct_stage_sh("ala", &p);
            ShL r = { 0 }; route_sh(1, &G, &p, &r); ct_stage_sl("route", &r);
            int sx = r.n == 1 ? has_self_isect(r.s[0]) : 0;
            if (r.n == 1) ct_stage_v("selfx", sx);
            if (r.n == 1 && !sx) {
                double off; on_off_len(r.s[0], NULL, &off); clear_b13(r.s[0]);
                link_smooth(C, S3, r.s[0]); ct_stage_sh("smooth", r.s[0]);
                sh_clear(&cand); cand.dir = 0; sh_append(&cand, exit); sh_append(&cand, r.s[0]); sh_append(&cand, appr);
                remove_loops(1e-06, &cand); ct_stage_sh("loops", &cand);
                int al = ends_aligned(exit, appr, &cand); ct_stage_v("aligned", al);
                if (al) {
                    double l = sh_len(&cand);
                    if (!link->n || l < best) { sh_clear(link); sh_append(link, &cand); link->dir = 0; *cr = off; best = l; }
                }
            }
            sl_free(&r);
        }
        if (link->n) break;
        if (!shrink_lists(round, LA, &nA, LB, &nB)) break;
    }
    free(p.e); free(cand.e);
    if (!link->n) { *cr = 0; return 0; }
    link_lin_arcs(C, S3, link); ct_stage_sh("lin", link);
    return 1;
}
/* FUN_180043c90 (l.40442-40702), P+0x261 set: exit end -> approach start points; G = FUN_180047920(S4, E, A); the straight line
   routed through G (both ways); one shape -> cr = its off-line length, FUN_18004a070(S3), link = exit + route + approach,
   FUN_180049950(S2), FUN_180045c40(S3) */
static int conn_ctour5(Pock *C, const ShL *S2, const ShL *S3, const ShL *S4, const Sh *exit, const Sh *appr, Sh *link, double *cr) {
    mcce_trace_ev("conn5");   /* ours: MCCE_TRACE_SW */
    sh_clear(link); link->dir = 0; *cr = 0;
    if (!exit->n || !appr->n) return 0;
    P2 E = gp_raw_end(&exit->e[exit->n - 1]), A = gp_raw_start(&appr->e[0]);
    ShL G; if (!group_of(S4, E, A, &G)) return 0;
    ShL r = { 0 }; Ent ln = ent_line(E, A); route(1, &G, &ln, &r); ct_stage_sl("route", &r);
    if (r.n != 1) { sl_free(&r); return 0; }
    on_off_len(r.s[0], NULL, cr); clear_b13(r.s[0]);
    link_smooth(C, S3, r.s[0]); ct_stage_sh("smooth", r.s[0]);
    sh_append(link, exit); sh_append(link, r.s[0]); sh_append(link, appr); link->dir = 0;
    sl_free(&r);
    junction_smooth(C, S2, exit, appr, link); ct_stage_sh("junction", link);
    link_lin_arcs(C, S3, link); ct_stage_sh("lin", link);
    return 1;
}
/* FUN_180045470 (l.41347-41391) */
static int connectors(Pock *C, const ShL *S2, const ShL *S3, const ShL *S4, int exitFlag, const Sh *exit, const Sh *appr, Sh *link, double *cr) {
    mcce_trace_ev("conn_45470");   /* ours: MCCE_TRACE_SW */
    const ShL *set = C->P.var_rad_avail ? S4 : S3;
    *cr = 0;
    /* 1. FUN_180042630 (l.39755-39813): exit and approach meet */
    {   ShL Al = { (Sh **)&appr, 1, 1 }; IsabL I = { 0 }; isect1(exit, &Al, &I);
        if (I.n) { sh_clear(link); sh_section(exit, 0, 0.0, I.r[0].xe, I.r[0].xt, link); sh_section(appr, I.r[0].ye, I.r[0].yt, appr->n - 1, 1.0, link);
                   il_free(&I); if (link->n) { link_smooth(C, S2, link); if (dbg_level() >= 3) fprintf(stderr, "connector 1\n"); return 1; } }   /* FUN_18004a070(C, S2, link) (l.39791) */
        il_free(&I); }
    if (conn_arcs(C, 2, set, S2, exitFlag, exit, appr, link)) { if (dbg_level() >= 3) fprintf(stderr, "connector 2\n"); return 1; }
    if (conn_arcs(C, 3, set, S2, exitFlag, exit, appr, link)) { if (dbg_level() >= 3) fprintf(stderr, "connector 3\n"); return 1; }
    if (C->P.ctour_guide) {   /* P+0x261 set (l.40016 / l.40551) */
        ct_dump_open(&C->P, exitFlag, exit, appr, S2, S3, S4);
        int r = conn_ctour4(C, S3, S4, exitFlag, exit, appr, link, cr);
        if (r) { if (dbg_level() >= 3) fprintf(stderr, "connector 4 ctour\n"); ct_dump_close(4, link, *cr); return 1; }
        r = conn_ctour5(C, S2, S3, S4, exit, appr, link, cr);
        if (r && dbg_level() >= 3) fprintf(stderr, "connector 5 ctour\n");
        ct_dump_close(r ? 5 : 0, link, *cr);
        return r;
    }
    if (conn_route(C, S4, exit, appr, link)) { if (dbg_level() >= 3) fprintf(stderr, "connector 4/5 route\n"); return 1; }   /* 4 and 5 (P+0x261 = 0) */
    return 0;
}
/* FUN_180061cd0 (l.56068-56157): stay-down link */
static int stay_down(Pock *C, TargL *T, const ShL *B, const ShL *S2, const ShL *S3, const ShL *S4, int allowLast, Sh *out, int *idx, double *cross) {
    mcce_trace_ev("stay_down");   /* ours: MCCE_TRACE_SW */
    const McceParm *P = &C->P;
    *idx = -1; *cross = 0; sh_clear(out); out->dir = 0;
    int from12 = 0, strat = 0;
    P2 cur = p2(0, 0), curT = p2(0, 0); last_pt(C, &cur); last_tan(C, &curT);
    /* (1) precomputed path of target 0 */
    if (T->n && T->t[0].path.n) { sh_append(out, &T->t[0].path); *idx = 0; from12 = 1; strat = 1; }
    /* (2) FUN_18005ff90 (l.55114-55366): short boundary-hugging hop */
    if (!out->n && !(P->flags & 4)) {
        for (int i = 0; i < T->n; i++) {
            if (pdist(cur, T->t[i].pt) > P->d1d0) break;
            ShL G; if (!group_of(S2, cur, T->t[i].pt, &G)) continue;
            if (G.n > 1) { Pos a, b; if (!sl_nearest(&G, cur, &a) || !sl_nearest(&G, T->t[i].pt, &b) || a.sh != b.sh || pdist2(a.pt, cur) > 1e-10 || pdist2(b.pt, T->t[i].pt) > 1e-10) continue; }
            ShL r = { 0 }; Ent ln = ent_line(cur, T->t[i].pt); route(0, &G, &ln, &r);
            if (r.n && sh_len(r.s[0]) <= P->d1d0) {
                double on; on_off_len(r.s[0], &on, NULL); clear_b13(r.s[0]);
                if (on <= 1e-5) { sh_append(out, r.s[0]); append_runin(C, &T->t[i], out); *idx = i; from12 = 1; strat = 2; sl_free(&r); break; }
            }
            sl_free(&r);
        }
    }
    /* (3) FUN_180044870 (l.40883-41346): exit + connector + approach, shortest wins */
    if (!out->n && S4->n) {
        Sh ex, ap, lk, backup; memset(&ex, 0, sizeof ex); memset(&ap, 0, sizeof ap); memset(&lk, 0, sizeof lk); memset(&backup, 0, sizeof backup);
        int exitFlag = 0, bi = -1; double best = FLT_MAXD, bestB = FLT_MAXD, bcr = 0;
        if (exit_move(C, S2, S4, &ex, &exitFlag)) {
            for (int i = 0; i < T->n; i++) {
                if (pdist2(cur, T->t[i].pt) > best * best) break;
                if (!approach(C, 0, &T->t[i], S2, S4, &ap)) continue;
                double cr; if (!connectors(C, S2, S3, S4, exitFlag, &ex, &ap, &lk, &cr)) continue;
                append_runin(C, &T->t[i], &lk);
                double L = sh_len(&lk), h = max_hd_chg(&lk);
                if (!P->bull_avail) { if (L < best) { sh_clear(out); sh_append(out, &lk); *cross = cr; *idx = i; best = L; } }
                else {
                    if (L < best && fabs(h) < 0.17453292519943295) { sh_clear(out); sh_append(out, &lk); *cross = cr; *idx = i; best = L; }
                    if (L < bestB) { sh_clear(&backup); sh_append(&backup, &lk); bcr = cr; bi = i; bestB = L; }
                }
            }
            if (P->bull_avail && *idx == -1 && bi >= 0) { sh_clear(out); sh_append(out, &backup); *cross = bcr; *idx = bi; }
            if (out->n) strat = 3;
        }
        free(ex.e); free(ap.e); free(lk.e); free(backup.e);
    }
    /* (4) FUN_18005d5f0 (l.53770-53987): direct arc - line - arc to target 0 */
    if (!out->n && T->n) {
        double r0 = P->var_rad_avail ? P->var_rad : P->min_rad; int ok; P2 Tu = unit(curT, &ok);
        int d1 = (P->flags & 4) ? ((P->flags & 1) ? -1 : 1) : ((P->flags & 1) ? 1 : -1), d2 = (P->flags & 1) ? 1 : -1;
        P2 V; int okv = 0; if (ok) V = unit(T->t[0].dir, &okv);
        static const double ks[6] = { 1.0, 0.75, 0.5, 0.25, 0.1, 0.01 };
        Sh p; memset(&p, 0, sizeof p);
        for (int k = 0; ok && okv && k < 6; k++) {
            if (!arc_line_arc(cur, Tu, ks[k] * r0, d1, T->t[0].pt, V, ks[k] * r0, d2, &p)) continue;
            IsabL X = { 0 }; isect1(&p, S2, &X); int bad = 0;
            for (int j = 0; j < X.n; j++) if (pdist(X.r[j].pt, cur) > 1e-5 && pdist(X.r[j].pt, T->t[0].pt) > 1e-5) bad = 1;
            il_free(&X);
            if (bad) continue;
            ShL pl = { (Sh *[]){ &p }, 1, 1 }; Pos h0 = { 0, 0, 0, e_start(&p.e[0]) }, hm;
            if (!walk_fwd(0, sh_len(&p) / 2, &pl, &h0, &hm, NULL) || sl_inside_grouped(S2, 1e-4, hm.pt) != 2) continue;
            sh_append(out, &p); append_runin(C, &T->t[0], out); *idx = 0; strat = 4; break;
        }
        free(p.e);
    }
    /* (5) FUN_18005f4c0 -> FUN_18005fb50 (l.54973-55110): per target, FUN_1800f0fa0(d 1e-6, step C+0x1e0, both, E = the last
       point FUN_18008e4b0 with E+0x10 = the last tangent FUN_18008e710, A = the target's point with A+0x10 = its +0x10, G,
       out, cross) (disasm 0x18005fce4-0x18005fd26): the guided planner (MCCE_GUIDED=1), else the plain route */
    if (!out->n && allowLast) {
        for (int i = 0; i < T->n; i++) {
            ShL G; if (!group_of(S2, cur, T->t[i].pt, &G)) continue;
            ShL r = { 0 }; double off = 0.0;
            /* P+0x261 set: FUN_18005f4c0's own loop (l.54851-54909) = the plain route of the line, cross = its off-line length */
            if (P->ctour_guide || !(guided_on() && guided_plan(1e-6, P->var_rad, (P->flags >> 2) & 1, cur, curT, T->t[i].pt, T->t[i].dir, &G, &r, &off) && r.n == 1)) {
                sl_free(&r); Ent ln = ent_line(cur, T->t[i].pt); route((P->flags >> 2) & 1, &G, &ln, &r);
                if (r.n) on_off_len(r.s[0], NULL, &off);
            }
            if (r.n) { clear_b13(r.s[0]); sh_append(out, r.s[0]); append_runin(C, &T->t[i], out); *idx = i; *cross = off; strat = 5; sl_free(&r); break; }
            sl_free(&r);
        }
    }
    if (!out->n) return 0;
    if (getenv("MCCE_DEBUG") && atoi(getenv("MCCE_DEBUG")) >= 3) {
        fprintf(stderr, "stay_down: strategy %d idx %d from12 %d n %d:", strat, *idx, from12, out->n);
        for (int i = 0; i < out->n; i++) { P2 a = e_start(&out->e[i]), b = e_end(&out->e[i]); fprintf(stderr, " %c(%.3f,%.3f)-(%.3f,%.3f)", out->e[i].id, a.x, a.y, b.x, b.y); }
        fprintf(stderr, "\n"); }
    set_ce(out, 4);
    if (!from12) zprofile(C, 2, P->micro_lift, (*idx >= 0 && T->t[*idx].b22) ? T->t[*idx].d28 : 0, out);
    return 1;
}
/* FUN_180064180 (l.57308-57339) */
static int need_retract(const McceParm *P, const Sh *link, double cross) {
    double len = sh_len(link), gap = P->gap;
    if (P->flags & 0x10000) return cross > 1e-5;
    if (P->flags & 0x40000) return cross > 1e-5 || len > gap;
    if (P->flags & 0x80000) return cross > 1e-5 && len > gap;
    if (P->flags & 0x20000) return len > gap;
    return 0;
}
/* FUN_180062140 (l.56160-56516): exit, lift, traverse, approach, descent */
static int retract_link(Pock *C, TargL *T, const ShL *S2, const ShL *S4, Sh *out, int *idx) {
    const McceParm *P = &C->P;
    sh_clear(out); out->dir = 0; *idx = -1;
    Sh ex, ap; memset(&ex, 0, sizeof ex); memset(&ap, 0, sizeof ap); int fl;
    exit_move(C, S2, S4, &ex, &fl);
    set_ce(&ex, 4); zprofile(C, 0, P->micro_lift, 0, &ex);
    if (T->n && approach(C, 1, &T->t[0], S2, S4, &ap)) {
        *idx = 0; append_runin(C, &T->t[0], &ap);
        if (sh_len(&ap) < P->approach) { extend_back(S4->n ? S4 : S2, &ap); keep_last(P->approach, &ap); }
        set_ce(&ap, 4); zprofile(C, 1, P->micro_lift, T->t[0].b22 ? T->t[0].d28 : 0, &ap);
    }
    sh_append(out, &ex);
    if (ex.n && ap.n) { Ent tr = ent_line(e_end(&ex.e[ex.n - 1]), e_start(&ap.e[0])); tr.ce = 1; tr.z = ex.e[ex.n - 1].z; sh_push(out, tr); }
    sh_append(out, &ap);
    free(ex.e); free(ap.e);
    return out->n > 0;
}
/* FUN_180065ff0 (l.58378-58456): next state after a link */
static void after_link(Pock *C, TargL *T, int idx, Gbap *G, int *state) {
    P2 cur = p2(0, 0); last_pt(C, &cur);
    if (getenv("MCCE_DEBUG") && atoi(getenv("MCCE_DEBUG")) >= 3) fprintf(stderr, "after_link rec %d idx %d/%d cur (%.3f,%.3f)%s\n", C->T.n, idx, T->n, cur.x, cur.y, idx >= 0 && idx < T->n && T->t[idx].b22 ? " -> state 2" : "");
    sl_nearest(&C->mat, cur, &C->mpos);
    G->lead.n = 0;
    if (idx < 0 || idx >= T->n) { G->valid = 0; *state = 0; C->memo_on = 0; C->memo[0] = C->memo[1] = 0; C->memo_idx = -1; return; }
    Targ *t = &T->t[idx];
    if (!t->b22) { G->valid = 0; *state = t->b20 ? 1 : 0; }
    else { G->valid = 1; G->sh = t->B.sh; G->ent = t->B.ent; G->t = t->B.t; G->pt = t->B.pt; *state = 2; }
    C->memo_on = 1; C->memo[0] = t->pt.x; C->memo[1] = t->pt.y; C->memo_idx = C->T.last_cut;
}
static void memo_push(Pock *C) {   /* FUN_1800653b0 + push C+0x718 onto C+0x730 */
    if (C->memo_on && C->memo_idx == C->T.last_cut) {
        if (C->nbl == C->cbl) { C->cbl = C->cbl ? C->cbl * 2 : 16; C->bl = realloc(C->bl, C->cbl * sizeof(P2)); }
        C->bl[C->nbl++] = p2(C->memo[0], C->memo[1]);
    }
}
static void emit_link(Pock *C, Sh *link) {
    if (getenv("MCCE_DEBUG") && atoi(getenv("MCCE_DEBUG")) >= 3) fprintf(stderr, "emit_link at rec %d: %d entities\n", C->T.n, link->n);
    uint32_t *mf = shape_mf(link, C->P.flags & 1, (C->P.flags >> 2) & 1);
    cetp_emit_shape(&C->T, &C->P, link, mf); free(mf);
}
/* FUN_1800653d0 (l.57949-58207): general link */
static void general_link(Pock *C, Gbap *G, int *state) {
    mcce_trace_ev("general_link");   /* ours: MCCE_TRACE_SW */
    C->mpos.sh = -1; C->mpos.ent = -1;
    G->valid = 0; G->lead.n = 0; *state = 3;
    memo_push(C);
    ShL S1, S2, S3, S4; region_sets(C, &S1, &S2, &S3, &S4);
    TargL T = { 0 }; targets_build(C, &S1, &S2, &S4, order_mode(C), &T);
    Sh link; memset(&link, 0, sizeof link); int idx; double cross;
    int ok = stay_down(C, &T, &C->cur, &S2, &S3, &S4, 1, &link, &idx, &cross);
    if (!ok || need_retract(&C->P, &link, cross)) retract_link(C, &T, &S2, &S4, &link, &idx);
    emit_link(C, &link);
    after_link(C, &T, idx, G, state);
    free(link.e); tl_free(&T); sl_free(&S1); sl_free(&S2); sl_free(&S3); sl_free(&S4);
}
/* ours (oracle trace, MCCE_TRACE + MCCE_TRACE_WIN=1): the window target list of FUN_180066ad0 - "WT" line (window sets'
   loop / entity counts / areas, target count) and one "wt" line per target, as tools/peloader/mcce.c h_wintargets; stage
   "pre" = before the distance filter (the DLL's FUN_18004d4b0 sort), "fin" = the returned list */
static void mcce_trace_wintargets(const char *stage, P2 cur, const ShL *W, const ShL *S1, const ShL *S2, const ShL *S4, const TargL *T) {
    mtr_init();
    static _Thread_local int on = -1; if (on < 0) { const char *e = getenv("MCCE_TRACE_WIN"); on = e && *e == '1'; }
    if (!g_trace_f || !on) return;
    FILE *f = g_trace_f;
    fprintf(f, "WT %s cur %.6f %.6f |", stage, cur.x, cur.y);
    if (W) { mtr_mat(f, "W", W); mtr_mat(f, "S1", S1); mtr_mat(f, "S2", S2); mtr_mat(f, "S4", S4); }
    fprintf(f, " | n %d\n", T->n);
    for (int i = 0; i < T->n; i++) { const Targ *t = &T->t[i];
        fprintf(f, " wt %d pt %.6f %.6f dir %.6f %.6f b %d %d %d d28 %.6f A %.6f %.6f B %.6f %.6f\n", i, t->pt.x, t->pt.y, t->dir.x, t->dir.y,
                t->b20, t->b21, t->b22, t->d28, t->A.pt.x, t->A.pt.y, t->B.pt.x, t->B.pt.y); }
    if (g_trace_lvl >= 3 && W) { mtr_mat_detail(f, "W", W); mtr_mat_detail(f, "S1", S1); mtr_mat_detail(f, "S2", S2); mtr_mat_detail(f, "S4", S4); }
}
/* FUN_180067d80 (l.59317-59664): local link in a window around the material piece to cut next */
static int local_link(Pock *C, Gbap *G, int *state) {
    mcce_trace_ev("local_link");   /* ours: MCCE_TRACE_SW */
    const McceParm *P = &C->P;
    G->valid = 0; G->lead.n = 0; *state = 3;
    memo_push(C);
    if (!C->mat.n) return 0;
    P2 cur = p2(0, 0); last_pt(C, &cur);
    /* FUN_1800686c0 (l.59667-59752): modes 1 / 2: the piece whose farthest point from cur is nearest; else the largest +0x58 */
    int k = -1; int mode = order_mode(C);
    if (mode == 1 || mode == 2) { double bd = FLT_MAXD; for (int i = 0; i < C->mat.n; i++) { Pos f; if (sh_farthest(C->mat.s[i], cur, &f)) { double d = pdist2(f.pt, cur); if (d < bd) { bd = d; k = i; } } } }
    else { int bt = -1; for (int i = 0; i < C->mat.n; i++) if (C->mat.s[i]->tag > bt) { bt = C->mat.s[i]->tag; k = i; } }
    if (k < 0) return 0;
    /* FUN_180066320 (l.58475-58740): window */
    const Sh *pk = C->mat.s[k];
    double bx0 = fmin(pk->bb[0], cur.x), by0 = fmin(pk->bb[1], cur.y), bx1 = fmax(pk->bb[2], cur.x), by1 = fmax(pk->bb[3], cur.y);
    double m = fmax(2 * (P->var_rad + P->R + P->d1c8) + P->approach, fmax(bx1 - bx0, by1 - by0));
    /* mode 2: FUN_180067820 NOT DECODED (not reached with P+0x20 = 1) */
    ShL rect = { 0 }; sl_push(&rect, rect_sh(bx0 - m, by0 - m, bx1 + m, by1 + m, C->z));
    ShL W;   /* l.58607: FUN_18015b2e0(1e-6, rect, C+0x340, W) - the DLL's own (mcce_sobool.inc) by default (N13-24), Clipper with MCCE_DLLWIN=0 */
    if (!(dllwin_on() && sl_bool_dll(MCCE_RB_ISECT, 1e-06, &rect, &C->cur, C->z, &W))) W = sl_bool(&rect, &C->cur, 1, C->z);   /* FUN_18015b2e0: pairwise boolean, read as the intersection (§6.5.6) */
    sl_free(&rect);
    if (sl_nonisland(&W) != 1) {
        ShL W2 = { 0 }; int i = 0;
        while (i < W.n) { int e = sl_group_end(&W, i); if (e < 0) e = W.n; ShL g = { W.s + i, e - i, e - i }; int r = sl_inside(&g, 1e-5, cur);
            if (r == 1 || r == 2) { for (int q = i; q < e; q++) sl_push(&W2, sh_copy(W.s[q])); break; } i = e; }
        sl_free(&W); W = W2;
    }
    orient_k2(P->flags, &W);
    if (!W.n) { sl_free(&W); return 0; }
    /* FUN_180066ff0 (l.58896-59138): region sets on the window */
    ShL mat = { 0 }; double wb[4]; sl_bbox(&W, wb);
    for (int i = 0; i < C->mat.n; i++) { const Sh *s = C->mat.s[i]; double g = P->R + 1e-5;
        if (s->bb[0] <= wb[2] + g && s->bb[2] >= wb[0] - g && s->bb[1] <= wb[3] + g && s->bb[3] >= wb[1] - g) sl_push(&mat, sh_copy(s)); }
    /* (Clipper bridge here: built exactly as in region_sets, buckle-v6 op 36 / v8-v9 op 42 / op 32 / op 3 end early with
       64-90 % of the material missed, 2026-11-07) */
    /* MCCE_DLLWIN (default on, N13-24): l.58989-58995 UpdDirShAr(0.001) + DetNestLvlShAr + MCCECalcOffset(1, R - r, mat, S1, 0x100, ..),
       FUN_180153b30(1e-6, W, S1, S2); l.59065-59070 MCCECalcOffset(1, -f d1 / -f d2, G, .., 0x100, ..) - the DLL's own offset / boolean */
    int dw = dllwin_on();
    ShL S1, S2, S3 = { 0 }, S4 = { 0 };
    g_offdump_tag = "0x180067212";   /* ours (oracle): FUN_180066ff0's S1 call */
    if (!(dw && mat.n && sl_offset_dll(&mat, P->R - P->r, C->z, 1, &S1))) S1 = sl_offset_k(&mat, P->R - P->r, C->z);
    g_offdump_tag = NULL;
    if (!(dw && sl_bool_dll(MCCE_RB_MINUS, 1e-06, &W, &S1, C->z, &S2))) S2 = sl_minus(&W, &S1, C->z);
    orient_k2(P->flags, &S2);
    { double d2 = P->d1c8, d1 = P->d1c0; if (P->var_rad_avail) { d1 = d2 - 1e-4; if (d1 < 0) d1 = 0.5 * d2; }
      int i = 0; while (i < S2.n) { int e = sl_group_end(&S2, i); if (e < 0) e = S2.n; ShL Gg = { S2.s + i, e - i, e - i }; double f = 1.0;
        for (int q = 0; q < 3; q++) { ShL A, B;
            if (!(dw && sl_offset_dll(&Gg, -f * d1, C->z, 0, &A))) A = sl_offset_k(&Gg, -f * d1, C->z);
            if (!(dw && sl_offset_dll(&Gg, -f * d2, C->z, 0, &B))) B = sl_offset_k(&Gg, -f * d2, C->z);
            if (sl_nonisland(&A) == 1 && sl_nonisland(&B) == 1) { sl_move_append(&S3, &A); sl_move_append(&S4, &B); sl_free(&A); sl_free(&B); break; }
            sl_free(&A); sl_free(&B); f *= 0.5; }
        i = e; } }
    /* FUN_180066ad0 (l.58744-58892): targets on the window */
    TargL T = { 0 };
    { int *used = malloc((S1.n + 1) * sizeof(int)), nused = 0;
      targets_boundary(C, &W, &S1, &T, used, &nused);
      targets_s1(C, 1, &W, &S1, &T, used, nused); free(used);
      g_sort_cur = cur; qsort(T.t, T.n, sizeof(Targ), targ_cmp);
      mcce_trace_wintargets("pre", cur, NULL, NULL, NULL, NULL, &T);   /* ours: oracle trace */
      if (mode == 2) order_mode2(C, &S2, &S4, &T);
      else for (int i = T.n - 1; i >= 0; i--) {
          P2 q = T.t[i].b22 ? T.t[i].B.pt : T.t[i].pt; Pos n; sh_nearest(pk, q, &n);
          if (fabs(pdist(n.pt, q) - (P->R - P->r)) > eqtol(1e-4)) tl_erase(&T, i); } }
    mcce_trace_wintargets("fin", cur, &W, &S1, &S2, &S4, &T);   /* ours: oracle trace */
    /* window positions of targets are on W: the run-in and the state-2 start are re-expressed below */
    Sh link; memset(&link, 0, sizeof link); int idx; double cross; int ok = 0;
    {   /* run-in sections are on W shapes: temporarily use W as C+0x340 for append_runin */
        ShL save = C->cur; C->cur = W;
        ok = stay_down(C, &T, &W, &S2, &S3, &S4, 0, &link, &idx, &cross);
        if (ok && need_retract(P, &link, cross)) ok = retract_link(C, &T, &S2, &S4, &link, &idx);
        C->cur = save;
    }
    if (ok) {
        emit_link(C, &link);
        if (idx >= 0 && idx < T.n && T.t[idx].b22) {   /* l.59532-59556: re-map the run-in end onto C+0x340 */
            Pos q; sl_nearest(&C->cur, T.t[idx].B.pt, &q);
            if (pdist2(q.pt, T.t[idx].B.pt) >= 1.0000000000000002e-10) T.t[idx].b22 = 0; else T.t[idx].B = q;
        }
        after_link(C, &T, idx, G, state);
    }
    free(link.e); tl_free(&T); sl_free(&W); sl_free(&mat); sl_free(&S1); sl_free(&S2); sl_free(&S3); sl_free(&S4);
    return ok;
}
/* FUN_18006eec0 (l.62875-62882): BIDIR: reverse the region lists and toggle the climb bit */
static void bidir_toggle(Pock *C) {
    if (!(C->P.flags & 4)) return;
    ShL *ls[] = { &C->tcr, &C->mat_all, &C->bull_all, &C->cur, &C->c3c0, &C->c3e8, &C->mat, &C->bull, &C->air };
    for (unsigned k = 0; k < sizeof ls / sizeof *ls; k++) for (int i = 0; i < ls[k]->n; i++) if (ls[k]->s[i]->dir) sh_rev(ls[k]->s[i]);
    C->P.flags ^= 1; C->P.climb = C->P.flags & 1;
}
/* FUN_180067ce0 (l.59300-59314) */
static void link_state(Pock *C, int near, Gbap *G, int *state) {
    if (!C->P.air_cut_v3 && near && local_link(C, G, state)) return;
    general_link(C, G, state);
}

/* ============================================================================================ CalcMedAx (mcce_medax.c) */
#include "mcce_medax.c"
#include "mcce_medax_par.c"
/* Sh list (nest groups holes..., outer, as C->cur keeps them) -> CalcMedAx input shapes */
static void medax_from_shapes(const ShL *L, MxSL *R) {
    for (int i = 0; i < L->n; i++) {
        const Sh *s = L->s[i]; MxS m = { 0 }; m.dir = s->dir; m.hole = sh_island(s);
        for (int k = 0; k < s->n; k++) {
            const Ent *e = &s->e[k]; MxE x; memset(&x, 0, sizeof x); x.id = e->id;
            if (e->id == 'L') { x.p1 = e->p1; x.p2 = e->p2; } else if (e->id == 'A') { x.c = e->c; x.r = e->r; x.sa = e->sa; x.sw = e->sw; } else x.p1 = e->p1;
            mx_push(&m, x);
        }
        msl_push(R, m);
    }
}

/* ============================================================================================ entry (§3 D 1, §6.7, §6.12) */
/* largest inscribed circle of a region (role of mcce::CalcMedAx + FUN_1800c5df0 "medial edge end point with the largest
   clearance", §3 D 1.5; CalcMedAx internals NOT DECODED: substituted by a bisection on the inset distance) */
/* FUN_180079050 (l.68340-68415): CalcMedAx(P+0x21d, this+0x340, S, M, 0.1, 8.0) + FUN_1800c5df0 (the medial edge start /
   end with the largest clearance) + FUN_1800c5230 (its point). On by MCCE_MEDAX=1 only (it moves the helix / profile entry
   of the user's stored stay-inside Dynamic ops; formats/opdlg_port_audit.md "Medial axis"); else the bisection below (ours). */
/* on by default (user decision N13-25, answers opdlg_port_audit.md 21 M.6 1); MCCE_MEDAX=0 = the bisection (ours) */
static int medax_on(void) { static _Thread_local int v = -1; if (v < 0) { const char *e = getenv("MCCE_MEDAX"); v = !(e && *e == '0'); } return v; }
static int medax_centre(const ShL *reg, P2 *ctr, double *rho) {
    MxSL R = { 0 }, S = { 0 }; MaeL M = { 0 }; int ok = 0;
    medax_from_shapes(reg, &R);
    int cm = calc_med_ax(&R, &S, &M, 0.1, 8.0);
    if (getenv("MCCE_MEDAX_DUMP")) { fprintf(stderr, "calc_med_ax %d edges %d shapes %d:", cm, M.n, R.n); for (int k = 0; k < R.n; k++) { double A = 0; for (int q = 0; q < R.s[k].n; q++) { P2 a = mx_startpt(&R.s[k].e[q]), b = mx_endpt(&R.s[k].e[q]); A += a.x * b.y - a.y * b.x; } fprintf(stderr, " [%d ents dir %d hole %d area2 %.3f -> %d]", R.s[k].n, R.s[k].dir, R.s[k].hole, A, k < S.n ? S.s[k].n : -1); } fprintf(stderr, "\n"); }
    if (cm && M.n) {
        double s, r; int i = max_clear(&S, &M, &s, &r);
        if (i >= 0) { *ctr = mae_pt(&M.r[i], s); *rho = r; ok = 1; }
        if (getenv("MCCE_MEDAX_DUMP")) {
            for (int k = 0; k < M.n; k++) { const Mae *m = &M.r[k]; P2 a = mae_pt(m, mae_s0(m)), b = mae_end(m);
                fprintf(stderr, "mae %d type %d (%.4f,%.4f)-(%.4f,%.4f) r %.4f/%.4f sides %d:%d %d:%d par %d ch %d %d\n", k, m->type, a.x, a.y, b.x, b.y,
                        mae_rad(&S, m, mae_s0(m), NULL), mae_rad(&S, m, mae_s1(m), NULL), m->sh1, m->en1, m->sh2, m->en2, m->parent, m->ch1, m->ch2); }
            fprintf(stderr, "medax centre %d (%.4f,%.4f) rho %.4f\n", ok, ctr->x, ctr->y, *rho);
        }
    }
    msl_free(&R); msl_free(&S); free(M.r);
    return ok;
}
static int max_clearance(const Pock *C, const ShL *reg, P2 *ctr, double *rho) {
    if (medax_on()) return medax_centre(reg, ctr, rho);
    double bb[4]; sl_bbox(reg, bb); double hi = 0.5 * hypot(bb[2] - bb[0], bb[3] - bb[1]), lo = 0;
    if (!reg->n) return 0;
    for (int it = 0; it < 40 && hi - lo > 1e-4 * (C->P.R + 1); it++) {
        double mid = (lo + hi) / 2; ShL o = sl_offset(reg, -mid, C->z); int ne = o.n > 0; sl_free(&o);
        if (ne) lo = mid; else hi = mid;
    }
    ShL o = sl_offset(reg, -lo, C->z);
    if (!o.n) { sl_free(&o); return 0; }
    int bi = 0; double ba = -1; for (int i = 0; i < o.n; i++) if (!sh_island(o.s[i])) { CPath c = sh_to_cpath(o.s[i], g_chord); double a = fabs(cl_area(&c)); free(c.xy); if (a > ba) { ba = a; bi = i; } }
    P2 s = p2(0, 0); int n = 0; for (int k = 0; k < o.s[bi]->n; k++) { s = padd(s, e_start(&o.s[bi]->e[k])); n++; }
    *ctr = n ? pmul(s, 1.0 / n) : e_start(&o.s[bi]->e[0]);
    Pos q; sl_nearest(reg, *ctr, &q); *rho = pdist(q.pt, *ctr);
    sl_free(&o);
    return 1;
}
/* FUN_18007bc00 (l.69718-69986): ramp down a closed loop; descent with constant slope, then one flat loop */
static void ramp_loop(Pock *C, const Sh *loop, Sh *out) {
    const McceParm *P = &C->P;
    sh_clear(out); out->dir = 0;
    double H = fmax(0, (P->top - P->depth) + P->zclear), Lc = sh_len(loop);
    if (Lc < 1e-14) return;
    double pitch;
    if (!(P->flags & 0x2000)) pitch = P->pitch;
    else { double a = P->plunge_ang; if (fabs(fabs(a) - 1.5707963267948966) < 1e-6) a = 1.5707963199999999; pitch = tan(a) * Lc; }
    double X = H / fabs(pitch) + 1.0, D = (X - 1.0) * Lc;
    ShL L = { (Sh **)&loop, 1, 1 }; Pos p0 = { 0, 0, 0, e_start(&loop->e[0]) }, st;
    walk_fwd(0, ((double)(int)(X + 1.0) - X) * Lc, &L, &p0, &st, NULL);
    double s = 0; int e = st.ent;
    for (int i = 0; ; i++) {
        if (i == 0) sh_push(out, ent_line(st.pt, st.pt));
        else if (i == 1) sh_section(loop, st.ent, st.t, st.ent, 1.0, out);
        else { e = (e + 1) % loop->n; sh_push(out, loop->e[e]); }
        Ent *last = &out->e[out->n - 1]; double l = e_len(last);
        if (D <= s) last->z = 0; else { last->z = H - ((l + s) / D) * H; if (last->z < 0) last->z = 0; }
        s += l;
        if (X * Lc - 1e-6 < s) break;
        if (i > 100000) break;
    }
    sh_upd_bbox(out);
}
/* FUN_1800788d0 (l.68107-68337): helix loop about c with radius P+0x90, then the ramp */
static int helix_entry(Pock *C, P2 c, Sh *entry, ShL *loops) {
    const McceParm *P = &C->P; double rh = P->helix_r;
    Sh circ; memset(&circ, 0, sizeof circ); sh_push(&circ, ent_arc(c, rh, 0, 2 * M_PI)); circ.dir = 1; sh_upd_bbox(&circ);
    int fits = (sh_vs_list(&circ, &C->cur, 1e-5) & 5) == 0;   /* FUN_18007b130 (l.69352-69436) */
    free(circ.e);
    if (!fits) return 0;
    if (rh < 1e-7) { sh_clear(entry); sh_push(entry, ent_line(c, c)); entry->dir = 0; sh_upd_bbox(entry); return 1; }
    int dir = (P->flags & 1) ? 1 : -1;
    if (P->R - rh < -1e-4) dir = -dir;
    Sh *loop = sh_new(); double sa = 0, sw = dir * 1.5707963267948966;
    for (int k = 0; k < 4; k++) {
        sh_push(loop, ent_arc(c, rh, sa, sw)); sa += sw;
        if (k == 2) { while (sa > M_PI) sa -= 2 * M_PI; while (sa < -M_PI) sa += 2 * M_PI; }
    }
    loop->dir = sw > 0 ? 1 : -1; sh_upd_bbox(loop);
    sl_push(loops, loop);
    ramp_loop(C, loop, entry);
    return entry->n > 0;
}
/* ============================================================================================ entry loop smoothing
   FUN_18004ae80 (l.44487-44787) -> mcce::ChaikinSmoothSh (l.1872-1928, file ShSmooth.cpp): FUN_18014c250 (chord step),
   FUN_18014d5c0 (chord polygon), FUN_18014c820 (Chaikin corner cutting), FUN_18014f740 (collinear merge). Decoded
   2026-11-07, §6.7.7 / §6.7.10 of the spec. The callback LAB_180002c6b = FUN_180048f30 only reports progress (no
   geometry): not ported. */
/* chord-walk position (0x20 bytes: +0 "valid" int (-1 = none), +4 entity, +8 t, +0x10 point) */
typedef struct { int a, ent; double t; P2 pt; } CsPos;
/* FUN_1800ad500 (l.94199-94241): nearest point on segment a-b; |b - a|^2 < 1e-14 -> the midpoint */
static P2 cs_near_seg(P2 a, P2 b, P2 q) {
    double dx = b.x - a.x, dy = b.y - a.y, l2 = dy * dy + dx * dx;
    if (l2 < 1e-14) return p2((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
    double t = -(((a.y - q.y) * dy + (a.x - q.x) * dx) / l2);
    if (t < 0.0 || 1.0 < t) t = t < 0.0 ? 0.0 : 1.0;
    return p2(t * dx + a.x, t * dy + a.y);
}
/* FUN_18014c250 (l.178651-178733): *d = min(*d, max(bbox w, h) / 2); *step = 4 |X - (d, d)| with X = CalcLnLnIntersectPt
   (l.3611-3649) of the lines {(d, d) cos45, (-cos45, cos45)} and {(d, d), (1, 0)} (2 d when parallel - not reached);
   *tol = |(d, d) cos45 - (d, d)| / 4 */
static void cs_step(const Sh *s, double *d, double *step, double *tol) {
    double w = s->bb[2] - s->bb[0], h = s->bb[3] - s->bb[1], m = w; if (m <= h) m = h;
    if (m * 0.5 < *d) *d = m * 0.5;
    double D = *d, c = D * 0.7071067811865475;
    *tol = sqrt((c - D) * (c - D) + (c - D) * (c - D)) * 0.25;
    P2 p = p2(c, c), dp = p2(-0.7071067811865476, 0.7071067811865476), q = p2(D, D), dq = p2(1, 0);
    double den = -(dq.y * dp.x) - -(dq.x * dp.y);
    if (fabs(den) < 1e-14) { *step = D * 2.0; return; }
    double a = (-((q.x - p.x) * dq.y) - -((q.y - p.y) * dq.x)) / den;
    P2 X = p2(a * dp.x + p.x, a * dp.y + p.y);
    *step = sqrt((X.y - D) * (X.y - D) + (X.x - D) * (X.x - D)) * 4.0;
}
/* FUN_18014ef90 (l.180119-180261): closed shapes only - the first longest line (squared chord, strict >) if
   len^2 >= 4 step^2: out = the shape rotated to start at it (FUN_18014fd50 l.180573-180713) with a copy of that line
   appended; the first entity starts and the last one ends at the line's midpoint. Returns 0 (out untouched) otherwise. */
static int cs_rotate_long(const Sh *in, Sh *out, double step) {
    if (in->dir == 0) return 0;
    int bi = -1; double bl = -1.0;
    for (int i = 0; i < in->n; i++) {
        const Ent *e = &in->e[i]; if (e->id != 'L') continue;
        double l = pdist2(e->p1, e->p2); if (bl < l) { bi = i; bl = l; }
    }
    if (bi < 0 || !(step * 4.0 * step <= bl)) return 0;
    *out = *in; out->e = malloc((in->n + 1) * sizeof(Ent)); out->cap = in->n + 1; out->n = 0;
    for (int k = bi; k < in->n; k++) out->e[out->n++] = in->e[k];
    for (int k = 0; k < bi; k++) out->e[out->n++] = in->e[k];
    out->e[out->n] = out->e[0]; out->n++;
    Ent *f = &out->e[0], *l = &out->e[out->n - 1];
    f->p1 = p2((f->p2.x - f->p1.x) * 0.5 + f->p1.x, (f->p2.y - f->p1.y) * 0.5 + f->p1.y);
    l->p2 = p2((l->p2.x - l->p1.x) * 0.5 + l->p1.x, (l->p2.y - l->p1.y) * 0.5 + l->p1.y);
    return 1;
}
/* FUN_18014c4f0 (l.178736-178803) + FUN_1800abd30 (l.93346-93423): crossings of entity nxt->ent with the circle of radius s
   about cur->pt (mcce::CalcEntIntersect tol 1e-5 - e_isect, ours), ascending t; on cur's entity those with t < cur->t are
   dropped; the first one left -> nxt (t, point at t). Returns 1 when there is one. */
static int cs_circle_hit(const Sh *sh, const CsPos *cur, CsPos *nxt, double s) {
    const Ent *e = &sh->e[nxt->ent]; Ent circ = ent_arc(cur->pt, s, 0, 6.283185307179586);
    double ta[2], tb[2]; P2 pt[2]; int n = e_isect(e, &circ, 1e-5, ta, tb, pt);
    if (n == 2 && ta[1] < ta[0]) { double t = ta[0]; ta[0] = ta[1]; ta[1] = t; }
    if (cur->ent == nxt->ent) { int m = 0; for (int i = 0; i < n; i++) if (!(ta[i] < cur->t)) ta[m++] = ta[i]; n = m; }
    if (!n) return 0;
    nxt->t = ta[0]; nxt->pt = e_at(e, ta[0]);
    return 1;
}
/* FUN_18014dce0 (l.179539-179620): the end of entity nxt->ent whose distance from cur->pt is nearer to s (ties and cur's own
   entity: the end, t 1; else the start, t 0) */
static void cs_pick_end(const Sh *sh, const CsPos *cur, CsPos *nxt, double s) {
    const Ent *e = &sh->e[nxt->ent]; P2 a = e_start(e), b = e_end(e);
    double da = pdist(cur->pt, a), db = pdist(cur->pt, b);
    if (fabs(db - s) <= fabs(da - s) || cur->ent == nxt->ent) { nxt->t = 1.0; nxt->pt = b; }
    else { nxt->t = 0.0; nxt->pt = a; }
}
/* FUN_18014e330 (l.179712-179990): the chord cur -> nxt stays within tol of the shape between them: the end of every piece
   (cur's entity from cur->t, nxt's entity up to nxt->t) within tol of the chord segment, and for arcs also the point
   c -+ r n (n the chord's left normal; minus when the centre is on the left or on the chord) when it lies on the piece
   (FUN_1800a46e0 l.89839-89866, t <= 1). A chord shorter than 1e-12 passes. */
static int cs_chord_ok(const Sh *sh, const CsPos *cur, const CsPos *nxt, double tol) {
    P2 A = cur->pt, B = nxt->pt; double dx = B.x - A.x, dy = B.y - A.y, L = sqrt(dx * dx + -dy * -dy);
    if (L < 1e-12) return 1;
    for (int k = cur->ent; k <= nxt->ent; k++) {
        Ent p = sh->e[k];
        if (k == cur->ent && k == nxt->ent) p = e_sub(&sh->e[k], cur->t, nxt->t);
        else if (k == cur->ent) {
            if (p.id == 'L') p.p1 = e_at(&sh->e[k], cur->t);
            else if (p.id == 'A') { p.sa = p.sa + p.sw * cur->t; p.sw = p.sw - p.sw * cur->t; }
        } else if (k == nxt->ent) {
            if (p.id == 'L') p.p2 = e_at(&sh->e[k], nxt->t);
            else if (p.id == 'A') p.sw = p.sw * nxt->t;
        }
        P2 q = p.id == 'L' ? p.p2 : p.id == 'A' ? e_end(&p) : p.p1;
        if (tol * tol < pdist2(q, cs_near_seg(A, B, q))) return 0;
        if (p.id == 'A') {
            double side = (p.c.y - A.y) * dx - dy * (p.c.x - A.x);
            P2 n = p2((-dy / L) * p.r, (dx / L) * p.r);
            q = side >= 0.0 ? psub(p.c, n) : padd(p.c, n);
            /* FUN_1800a46e0: angle of q - sa in [0, 2pi] ((-2pi, 0] when sw < 0) / sw; needs q off the centre, |sw| >= 1e-12 */
            if (1e-14 < fabs(q.x - p.c.x) || 1e-14 < fabs(q.y - p.c.y)) {
                double a = atan2(q.y - p.c.y, q.x - p.c.x) - p.sa;
                while (6.283185307179586 < a) a -= 6.283185307179586;
                while (a < 0.0) a += 6.283185307179586;
                if (p.sw < 0.0) while (0.0 < a) a -= 6.283185307179586;
                if (1e-12 <= fabs(p.sw) && a / p.sw <= 1.0 && tol * tol < pdist2(q, cs_near_seg(A, B, q))) return 0;
            }
        }
    }
    return 1;
}
/* FUN_18014dfc0 (l.179623-179709): next chord end from cur, aiming at distance s: the end of cur's entity (then the next
   entities) is taken when it lies between 0.5 s and 1.5 s from cur (lines; arcs under 1.7278759594743864 rad), else the
   circle crossing (cs_circle_hit), else the next entity while the end is within s (stopping at the last entity), else
   cs_pick_end; a chord that fails cs_chord_ok is retried with s x 0.75 */
static void cs_next(const Sh *sh, const CsPos *cur, CsPos *nxt, double s, double tol) {
    for (int guard = 0; guard < 2000; guard++) {   /* ours: guard (s -> 0 always ends with a chord within tol) */
        *nxt = *cur; nxt->t = 1.0;
        for (;;) {
            const Ent *e = &sh->e[nxt->ent];
            nxt->pt = e->id == 'L' ? e->p2 : e->id == 'A' ? e_end(e) : e->p1;
            double d2 = pdist2(cur->pt, nxt->pt);
            if ((e->id == 'L' || (e->id == 'A' && fabs(e->sw) < 1.7278759594743864)) && s * 0.5 * s * 0.5 < d2 && d2 < s * 1.5 * s * 1.5) break;
            if (cs_circle_hit(sh, cur, nxt, s)) break;
            if (d2 <= s * s) { if (sh->n <= nxt->ent + 1) break; nxt->ent++; continue; }
            cs_pick_end(sh, cur, nxt, s);
            break;
        }
        if (cs_chord_ok(sh, cur, nxt, tol)) return;
        s *= 0.75;
    }
}
/* FUN_18014d5c0 (l.179292-179536): the chord polygon of sh (rotated by cs_rotate_long when it applies): chords cur -> next
   (FUN_18014bf20 l.178488-178608: a line) until the next point is on the last entity within 1e-3 of the shape's end;
   FUN_18014f470 (l.180264-180356): when there are two chords or more and the last is not longer than step / 2, and the
   chord from the one before to the end passes cs_chord_ok, the last chord is dropped and the one before ends at the end.
   out: the shape's header, the chords. */
static void cs_chords(const Sh *in, Sh *out, double step, double tol) {
    Sh rot; const Sh *s = cs_rotate_long(in, &rot, step) ? &rot : in;
    *out = *s; out->e = NULL; out->n = out->cap = 0;
    const Ent *le = &s->e[s->n - 1]; P2 E = le->id == 'L' ? le->p2 : le->id == 'A' ? e_end(le) : le->p1;
    CsPos cur = { 0, 0, 0.0, e_start(&s->e[0]) }, prev = { -1, 0, 0.0, { 0, 0 } }, nxt;
    for (long it = 0; ; it++) {
        cs_next(s, &cur, &nxt, step, tol);
        sh_push(out, ent_line(cur.pt, nxt.pt));
        if (nxt.ent == s->n - 1 && pdist2(nxt.pt, E) < 1e-6) break;
        if (it > 10000000 || mcce_abort) break;   /* ours: guard */
        prev = cur; cur = nxt;
    }
    if (prev.a != -1 && out->n >= 2 && !(step * 0.5 < e_len(&out->e[out->n - 1]))) {
        CsPos end = { 0, s->n - 1, 1.0, E };
        if (cs_chord_ok(s, &prev, &end, tol)) { out->n--; out->e[out->n - 1].p2 = E; }
    }
    sh_upd_bbox(out);
    if (s == &rot) free(rot.e);
}
/* FUN_18014c820 (l.178856-179289) with (tol 0.001, 6 passes, ratio 3): FUN_180124110 (l.156996-157192) chords arcs by
   ApxArcWithLnSegs first - not reached here (the chord polygon has lines only); each pass replaces every line a-b by
   (3a + b) / 4 -> (a + 3b) / 4 (ratio / (ratio + 1), 1 / (ratio + 1)) joined by lines; an open shape keeps its first start
   and last end, a closed one gets the closing joint. (The deviation output is not used by ChaikinSmoothSh.) */
static void cs_chaikin(Sh *s, int passes, double ratio) {
    if (passes < 1) passes = 1;
    if (ratio < 1.00001) ratio = 1.00001;
    double a = 1.0 / (ratio + 1.0), b = ratio / (ratio + 1.0);
    for (int it = 0; it < passes; it++) {
        if (!s->n) break;
        int cap = 2 * s->n + 1; Ent *o = malloc(cap * sizeof(Ent)); int m = 0;
        for (int i = 0; i < s->n; i++) {
            Ent e = s->e[i]; e.id = 'L';
            e.p1 = p2(s->e[i].p2.x * a + s->e[i].p1.x * b, s->e[i].p2.y * a + s->e[i].p1.y * b);
            e.p2 = p2(s->e[i].p2.x * b + s->e[i].p1.x * a, s->e[i].p2.y * b + s->e[i].p1.y * a);
            if (i) { Ent j; memset(&j, 0, sizeof j); j.id = 'L'; j.p1 = o[m - 1].p2; j.p2 = e.p1; o[m++] = j; }
            o[m++] = e;
        }
        if (s->dir == 0) { o[0].p1 = s->e[0].p1; o[m - 1].p2 = s->e[s->n - 1].p2; }
        else { Ent j; memset(&j, 0, sizeof j); j.id = 'L'; j.p1 = o[m - 1].p2; j.p2 = o[0].p1; o[m++] = j; }
        free(s->e); s->e = o; s->n = m; s->cap = cap;
    }
    sh_upd_bbox(s);
}
/* FUN_18014ec10 (l.179993-180116): every entity end from i to j - 1 within tol of the segment start(i) -> end(j) */
static int cs_collinear(const Sh *s, int i, int j, double tol) {
    P2 A = e_start(&s->e[i]); const Ent *ej = &s->e[j]; P2 B = ej->id == 'L' ? ej->p2 : ej->id == 'A' ? e_end(ej) : ej->p1;
    for (int k = i; k < j; k++) {
        const Ent *e = &s->e[k]; P2 q = e->id == 'L' ? e->p2 : e->id == 'A' ? e_end(e) : e->p1;
        if (tol * tol < pdist2(q, cs_near_seg(A, B, q))) return 0;
    }
    return 1;
}
/* FUN_18014f740 (l.180359-180570) with tol 1e-5: from entity i, FUN_18014c150 (l.178611-178648) bisects for the last j
   that passes cs_collinear; one line start(i) -> end(j) replaces them; on from j + 1 */
static void cs_merge(Sh *s, double tol) {
    Ent *o = malloc((s->n ? s->n : 1) * sizeof(Ent)); int m = 0;
    for (int i = 0; i < s->n; ) {
        int lo = i, hi = s->n - 1, failed = 0, j;
        for (;;) {
            int c = lo, mid = (hi + c) / 2;
            if (mid == c) { if (!failed) mid = hi; if (mid == c) { j = c; break; } }
            if (mid == hi && failed) { j = c; break; }
            if (cs_collinear(s, i, mid, tol)) lo = mid; else { failed = 1; lo = c; hi = mid; }
        }
        const Ent *ej = &s->e[j]; Ent l; memset(&l, 0, sizeof l); l.id = 'L';
        l.p1 = e_start(&s->e[i]); l.p2 = ej->id == 'L' ? ej->p2 : ej->id == 'A' ? e_end(ej) : ej->p1;
        o[m++] = l; i = j + 1;
    }
    free(s->e); s->e = o; s->n = m; s->cap = s->n ? s->n : 1;
    sh_upd_bbox(s);
}
/* mcce::ChaikinSmoothSh (l.1872-1928): empty -> nothing; one entity -> a copy (FUN_18011f2e0 l.154654-154718); else
   cs_step, cs_chords, cs_chaikin(6, 3.0), cs_merge(1e-5). Returns a new shape or NULL. */
static Sh *cs_smooth(double d, const Sh *in) {
    if (!in || !in->n) return NULL;
    if (in->n == 1) return sh_copy(in);
    double step, tol; cs_step(in, &d, &step, &tol);
    Sh *o = sh_new(); cs_chords(in, o, step, tol);
    cs_chaikin(o, 6, 3.0);
    cs_merge(o, 1e-5);
    return o;
}
/* FUN_180048320 (l.42938-43094): crossings I of sh with S (FUN_180134690); 0 or 2 -> 1; 1 -> [start of sh, I0] when the
   start is inside S (FUN_180112e80 == 2), else [I0, end of sh] with the answer "end inside S"; more -> 0. Records as
   (entity, t) pairs in se / st. */
static int cs_section_sel(const Sh *sh, const ShL *S, int *nI, int se[2], double st[2]) {
    IsabL I = { 0 }; isect1(sh, S, &I);
    *nI = I.n; int ok;
    if (I.n == 0 || I.n == 2) { for (int k = 0; k < I.n; k++) { se[k] = I.r[k].xe; st[k] = I.r[k].xt; } ok = 1; }
    else if (I.n == 1) {
        *nI = 2; se[0] = 0; st[0] = 0.0; se[1] = I.r[0].xe; st[1] = I.r[0].xt;
        P2 a = sh->e[0].id == 'L' || sh->e[0].id == 'A' || sh->e[0].id == 'P' ? e_start(&sh->e[0]) : p2(0, 0);
        if (sl_inside_grouped(S, 1e-5, a) == 2) ok = 1;
        else {
            se[0] = I.r[0].xe; st[0] = I.r[0].xt; se[1] = sh->n - 1; st[1] = 1.0;
            const Ent *l = &sh->e[sh->n - 1]; P2 b = l->id == 'L' || l->id == 'A' || l->id == 'P' ? e_end(l) : p2(0, 0);
            ok = sl_inside_grouped(S, 1e-5, b) == 2;
        }
    } else ok = 0;
    il_free(&I);
    return ok;
}
/* FUN_18004ae80 (l.44487-44787): smooth the entry loop inside S (this+0x340). d0 = P+0x21e ? P+0x1d8 : P+0x40
   (l.44561-44567); the whole loop (no crossing) or the section I0 -> I1 (FUN_180146e20, a header-less = open shape) is
   smoothed with d = d0, 0.9 d0 .. 0.2 d0 (l.44631-44692); the first result with no piece outside S (FUN_180113300 & 4 == 0,
   tol 1e-5, l.44654-44659) replaces the loop (whole: when not empty; section: start -> I0, the smoothed piece, I1 -> end,
   l.44670-44684, as an open shape: CSh header zero). */
static void smooth_entry_loop(Pock *C, const ShL *S, Sh *loop) {
    const McceParm *P = &C->P;
    int nI = 0, se[2] = { 0, 0 }; double st[2] = { 0, 0 };
    if (!cs_section_sel(loop, S, &nI, se, st)) return;
    Sh *sec;
    if (nI == 0) sec = sh_copy(loop);
    else { sec = sh_new(); sh_section(loop, se[0], st[0], se[1], st[1], sec); }
    double d0 = P->var_rad_avail ? P->var_rad : P->min_rad, d = d0;
    int k = 9;
    do {
        Sh *o = cs_smooth(d, sec);
        int u = (o && o->n) ? sh_vs_list(o, S, 1e-5) : 0;
        if (!(u & 4)) {
            if (nI == 0) {
                if (o && o->n) { free(loop->e); *loop = *o; free(o); o = NULL; }
            } else {
                Sh t; memset(&t, 0, sizeof t);
                sh_section(loop, 0, 0.0, se[0], st[0], &t);
                if (o && o->n) sh_append(&t, o);
                sh_section(loop, se[1], st[1], loop->n - 1, 1.0, &t);
                free(loop->e); *loop = t;
            }
            sh_free(o);
            break;
        }
        sh_free(o);
        d = (double)k * d0 * 0.1; k--;
    } while (0 < k);
    sh_free(sec);
}
/* FUN_18007cd50 (l.70215-70390): profile direction */
static int profile_dir(Pock *C, const Sh *loop) {
    const McceParm *P = &C->P; int dir = (P->flags & 1) ? 1 : -1;
    ShL L = { (Sh **)&loop, 1, 1 };
    ShL a = sl_offset(&L, P->R - P->r, C->z), b = sl_offset(&L, -(P->R - P->r), C->z), band = sl_minus(&a, &b, C->z), pieces = sl_minus(&C->mat, &band, C->z);
    int fail = 0;
    for (int i = 0; i < pieces.n; i++) if (pieces.s[i]->n && pt_in_sh(e_start(&pieces.s[i]->e[0]), loop, 1e-5) == 2 && !small_bbox(C, pieces.s[i], 34.5)) fail = 1;
    sl_free(&a); sl_free(&b); sl_free(&band); sl_free(&pieces);
    return fail ? -dir : dir;
}
/* FUN_18007cb70 (l.70167-70212): profile entry */
static int profile_entry(Pock *C, Sh *entry, ShL *loops) {
    const McceParm *P = &C->P; P2 ctr; double rmax;
    if (!max_clearance(C, &C->cur, &ctr, &rmax)) return 0;
    /* FUN_18007ca70: MCCECalcOffset keeps lines and arcs (FUN_1800da810, §6.13.1): the exact offset in the exact model */
    double dl = -fmax(0, rmax - 0.75 * (P->R - P->r));
    /* MCCE_DLLENT (default on, user decision N13-25; MCCE_DLLENT=0 old): l.70154-70158 MCCECalcOffset(1, -max(rmax - 0.75 (R - r), 0), C+0x340, out, 1, 0, 1e-6, 1e-6) - the DLL's
       own offset (mcce_offset.inc); else / where it refuses the old construction */
    static _Thread_local int dllent = -1; if (dllent < 0) { const char *e = getenv("MCCE_DLLENT"); dllent = !(e && *e == '0'); }   /* on by default: N13-25 */
    if (getenv("MCCE_DEBUG")) fprintf(stderr, "profile_entry: rmax %.17g ctr (%.9f,%.9f) d %.17g\n", rmax, ctr.x, ctr.y, dl);
    ShL o;
    if (!(dllent && sl_offset_dllf(&C->cur, dl, C->z, 0, 0x1, &o))) o = exact_on() ? sl_offset_m(&C->cur, dl, C->z) : sl_offset(&C->cur, dl, C->z);
    int bi = -1; double bl = -1; for (int i = 0; i < o.n; i++) { double l = sh_len(o.s[i]); if (l > bl) { bl = l; bi = i; } }   /* FUN_180123a30 */
    if (bi < 0) { sl_free(&o); return 0; }
    Sh *loop = sh_copy(o.s[bi]); loop->fl = 0; sl_free(&o);
    /* FUN_180123bc0(pi/2) (l.156874-156990): an arc with |sw| > pi/2 is replaced in place by its two halves (sw / 2 each, the
       second from sa + sw / 2), and the first half is checked again, until every arc is <= pi/2 */
    for (int k = 0; k < loop->n; k++) {
        Ent *e = &loop->e[k];
        if (e->id != 'A' || !(fabs(e->sw) > 1.5707963267948966)) continue;
        Ent a = *e, b = *e; a.sw = e->sw * 0.5; b.sa = e->sa + e->sw * 0.5; b.sw = e->sw - e->sw * 0.5;
        sh_push(loop, a); memmove(loop->e + k + 2, loop->e + k + 1, (size_t)(loop->n - k - 2) * sizeof(Ent));
        loop->e[k] = a; loop->e[k + 1] = b; k--;
    }
    sh_upd_bbox(loop);
    /* FUN_18004ae80(this, this+0x340, loop) l.70202; the Clipper model (MCCE_MEASURE=clipper) keeps its unsmoothed loop */
    if (exact_on()) smooth_entry_loop(C, &C->cur, loop);
    if (!loop->n) { sh_free(loop); return 0; }                                  /* l.70203-70205 */
    int dir = profile_dir(C, loop);
    if (loop->dir != dir) sh_rev(loop);
    sl_push(loops, loop);
    ramp_loop(C, loop, entry);
    return entry->n > 0;
}
/* FUN_1800a6aa0 (l.91022-91124) spiral (radius linear in the swept fraction) as line pieces (FUN_1800aab40 piece fit NOT
   DECODED: chord pieces within the tolerance) */
static void spiral(P2 c, double r0, double r1, double a0, double sweep, Sh *out) {
    int n = (int)ceil(fabs(sweep) / (M_PI / 36)); if (n < 4) n = 4;
    P2 prev = p2(c.x + r0 * cos(a0), c.y + r0 * sin(a0));
    for (int i = 1; i <= n; i++) { double f = (double)i / n, r = r0 + (r1 - r0) * f, a = a0 + sweep * f; P2 q = p2(c.x + r * cos(a), c.y + r * sin(a)); sh_push(out, ent_line(prev, q)); prev = q; }
}
/* FUN_18007c700 (l.70053-70141) */
static int short_entry_ext(Pock *C, Sh *E) {
    const McceParm *P = &C->P;
    if (!E->n) return 0;
    P2 Q = e_end(&E->e[E->n - 1]); double s = P->step, sweep = (P->flags & 1) ? 2 * M_PI : -2 * M_PI;
    Sh circ; memset(&circ, 0, sizeof circ); sh_push(&circ, ent_arc(Q, s, 0, 2 * M_PI)); circ.dir = 1; sh_upd_bbox(&circ);
    int bad = sh_vs_list(&circ, &C->cur, 1e-5) & 5; free(circ.e);
    if (bad) return 0;
    P2 T = tan_in(E, E->n - 1, 1.0, 1e-16);
    double a0 = (fabs(T.x) > 1e-12 || fabs(T.y) > 1e-12) ? atan2(T.y, T.x) : 0;
    double z = E->e[E->n - 1].z; int n0 = E->n;
    spiral(Q, 0.0, s, a0, sweep, E);
    for (int i = n0; i < E->n; i++) E->e[i].z = z;
    sh_upd_bbox(E);
    return 1;
}
/* FUN_180070b60 (l.63904-64209) + FUN_180070280 (l.63654-63800): remove the material swept by the entry (no output) */
static void entry_removal(Pock *C, Sh *E) {
    const McceParm *P = &C->P;
    if (!E->n) return;
    P2 S = e_start(&E->e[0]);
    for (int k = 0; k < 2; k++) {
        ShL *m = k == 0 ? &C->mat : &C->bull; double rad = k == 0 ? P->R - P->r : P->R;
        if (k == 1 && !C->bull.n) break;
        ShL D = { 0 }; Sh *d = sh_new(); sh_push(d, ent_arc(S, rad, 0, 2 * M_PI)); d->dir = 1; sh_upd_bbox(d); sl_push(&D, d);
        ShL o = sl_minus_m(m, &D, C->z); orient_mat(P->flags, &o);
        for (int i = 0; i < o.n; i++) o.s[i]->tag = C->T.n;
        sl_free(m); *m = o; sl_free(&D);
    }
    ShL L = { &E, 1, 1 }; Pos pos = { 0, 0, 0, S }, np; int dir = sh_side(E), last = 0;
    EnCut Ec; memset(&Ec, 0, sizeof Ec);
    for (int g = 0; g < 1000000 && !last; g++) {
        if (mcce_abort) break;                           /* ours: Cancel (the result is dropped) */
        int ok = walk_fwd(1, P->step, &L, &pos, &np, NULL);
        if (!ok) { last = 1; if (E->dir != 0) break; np.ent = E->n - 1; np.t = 1.0; np.pt = e_end(&E->e[E->n - 1]); }
        if (np.ent < pos.ent || (np.ent == pos.ent && np.t < pos.t)) last = 1;
        encut_clear(&Ec);
        Ec.e = path_step(E, &pos, &np);
        measure(C, NULL, dir, &Ec);
        if (!(Ec.m1.flags & 0x18)) { Pos keep = C->mpos; commit(C, &Ec); C->mpos = keep; }   /* commit with pos NULL */
        pos = np;
    }
    orient_mat(P->flags, &C->mat);
}
/* FUN_1800707b0 (l.63809-63901) */
static void entry_emit(Pock *C, Sh *E) {
    if (!E->n) return;
    P2 S = e_start(&E->e[0]);
    if (C->T.n) { P2 L; last_pt(C, &L); Ent a = ent_line(L, L); cetp_emit(&C->T, &C->P, &a, 1, 0, 0, 0, 0, 0); Ent b = ent_line(L, S); cetp_emit(&C->T, &C->P, &b, 1, 0, 0, 0, 0, 0); }
    if (e_len(&E->e[0]) * e_len(&E->e[0]) > 1e-10) { Ent z = ent_line(S, S); z.z = E->e[0].z; cetp_emit(&C->T, &C->P, &z, 8, 0, 0, 0, 0, 0); }
    for (int i = 0; i < E->n; i++) cetp_emit(&C->T, &C->P, &E->e[i], 8, 0, 0, 0, 0, 0);
}
/* ============================================================================================ medial-axis entries */
#include "mcce_medent.c"
/* FUN_18006fc70 (l.63421-63601): plunge entries */
static int plunge_entry(Pock *C, ShL *loops) {
    const McceParm *P = &C->P; uint32_t f = P->flags;
    Sh E; memset(&E, 0, sizeof E); int have = 0;
    if (f & 0x100) {   /* FUN_180078840 helix only: centre = max-clearance point (flag 0x100000 entry points: NOT DECODED source) */
        P2 c; double rho; if (max_clearance(C, &C->cur, &c, &rho)) have = helix_entry(C, c, &E, loops);
    } else if (f & 0x200) have = entry_full_medial(C, &E);                     /* FUN_180078130 (mcce_medent.c; loops stay empty) */
    else if (f & 0x400) have = entry_troch_medial(C, &E);                       /* FUN_180078500 (mcce_medent.c; loops stay empty) */
    else if (f & 0x800) have = profile_entry(C, &E, loops);
    else if (f & 0x1000) {                                                      /* FUN_18007b3c0 (mcce_medent.c) */
        int und = 0; have = entry_medial_ramp(C, &E, &und);
        if (und == 2) { snprintf(C->nd, sizeof C->nd, "entry method 4 (Medial): no pruned medial tour here and the clipped-tour candidate (FUN_18015b2e0) is NOT DECODED"); free(E.e); return 0; }
        /* und == 1: Mastercam takes the longer of the pruned tour and the clipped tours (l.69644-69656); the clipped one needs
           FUN_18015b2e0 (NOT DECODED) and could not be proven empty -> refused (no improvisation); MCCE_MEDENT4_PRUNED=1 (ours,
           testing) takes the pruned tour, Mastercam's result only when the clipped tours are not longer */
        if (und == 1 && !(getenv("MCCE_MEDENT4_PRUNED") && *getenv("MCCE_MEDENT4_PRUNED") == '1')) {
            snprintf(C->nd, sizeof C->nd, "entry method 4 (Medial): the clipped-tour candidate (FUN_18015b2e0) is NOT DECODED and not provably empty here (MCCE_MEDENT4_PRUNED=1 takes the pruned tour)"); free(E.e); return 0; }
    } else if (f & 0x8000) {                                                    /* FUN_180074560: refused (DE 7) */
        snprintf(C->nd, sizeof C->nd, "entry method 5 (Custom entry chain): entry chains (chain role 5) are refused; not reached (mcmill FUN_18029cb90 clears 0x8000 without them)");
        free(E.e); return 0;
    }
    if (!have && (f & 0x8700)) { sl_clear(loops); sh_clear(&E); have = profile_entry(C, &E, loops); }   /* l.63516-63519 */
    if (!have) { free(E.e); return 0; }
    if (sh_len(&E) < P->step && !short_entry_ext(C, &E)) { free(E.e); return 0; }
    entry_removal(C, &E);
    entry_emit(C, &E);
    /* FUN_180071df0 air dwell (flag 0x40000000): radius search FUN_180072280 NOT DECODED: not added */
    free(E.e);
    return 1;
}
/* FUN_180076500 (l.66889-67066): reference chn C+0x120 near the start-point selector */
static void ref_chn(Pock *C, const ShL *o2, const ShL *o3, const ShL *o4) {
    const ShL *sh = o4->n ? o4 : o3->n ? o3 : o2;
    if (!sh->n) return;
    double bb[4]; sl_bbox(sh, bb); double mx = (bb[0] + bb[2]) / 2, my = (bb[1] + bb[3]) / 2;
    P2 ref;
    switch (C->P.start_sel) {
    case 1: ref = p2(mx, bb[1]); break; case 2: ref = p2(bb[2], bb[1]); break; case 3: ref = p2(bb[2], my); break;
    case 4: ref = p2(bb[2], bb[3]); break; case 5: ref = p2(mx, bb[3]); break; case 6: ref = p2(bb[0], bb[3]); break;
    case 7: ref = p2(bb[0], my); break; case 8: ref = p2(C->P.start_x, C->P.start_y); break; default: ref = p2(bb[0], bb[1]);
    }
    Pos q; if (!sl_nearest(sh, ref, &q)) return;
    P2 t = tan_out(sh->s[q.sh], q.ent, q.t, 1.0000000000000002e-10); double l = plen(t); if (l >= 1e-12) t = pmul(t, 1 / l);
    const Sh *s = sh->s[q.sh];
    if (sh_island(s)) { if (s->dir != 1) t.y = -t.y; else t.x = -t.x; }
    else { if (s->dir == 1) t.y = -t.y; else t.x = -t.x; }
    double z = C->prev.z; Ent e = ent_line(p2(q.pt.x + t.y, q.pt.y + t.x), q.pt); e.z = z; C->prev = e;
    if (getenv("MCCE_TCDBG")) {   /* ours (diagnostic): as tools/peloader hook_refchn */
        const ShL *L3[3] = { o2, o3, o4 }; fprintf(stderr, "refchn sel %d |", C->P.start_sel); mtr_mat(stderr, "o2", o2); mtr_mat(stderr, "o3", o3); mtr_mat(stderr, "o4", o4); fputc('\n', stderr);
        for (int k = 0; k < 3; k++) for (int i = 0; i < L3[k]->n; i++) fprintf(stderr, "  set %d sh %d n %d box %.6f %.6f %.6f %.6f\n", k, i, L3[k]->s[i]->n, L3[k]->s[i]->bb[0], L3[k]->s[i]->bb[1], L3[k]->s[i]->bb[2], L3[k]->s[i]->bb[3]);
        fprintf(stderr, "refchn line %.6f %.6f %.6f %.6f\n", e.p1.x, e.p1.y, e.p2.x, e.p2.y); }
}
/* FUN_180076b10 (l.67069-67319) + FUN_180075a00 (l.66501-66702) + FUN_180075f60 (l.66709-66886): approach from outside */
static int approach_outside(Pock *C) {
    const McceParm *P = &C->P;
    ShL S1, S2, S3, S4; region_sets(C, &S1, &S2, &S3, &S4);
    Sh ap; memset(&ap, 0, sizeof ap); TargL T = { 0 }; int ti = -1, ok = 0;
    if (!S2.n) goto done;
    /* FUN_180077a50: MOAT_FIRST only: false */
    {   /* FUN_180075f60 */
        int mode = P->old_gsl_cut_ord == 0 ? 2 : order_mode(C);
        int reuse = 0;
        if (!C->T.n && C->prev.id && fabs(C->prev.z - P->depth) < 1e-5) { Pos q; reuse = sl_nearest(&S1, e_end(&C->prev), &q) && pdist(q.pt, e_end(&C->prev)) <= 1e-5; }   /* FUN_1800757e0 */
        if (reuse) { Targ *t = tl_push(&T); t->pt = e_end(&C->prev); t->b21 = 1; }
        else {
            ShL t0 = sl_offset(&S2, -0.001, C->z); int e = t0.n == 0; sl_free(&t0);
            if (e) goto done;
            ref_chn(C, &S2, &S3, &S4);
            targets_build(C, &S1, &S2, &S4, mode, &T);   /* (built on a private CCetp: no records yet influence it) */
            if (!T.n) goto done;
        }
    }
    if (dbg_level() >= 4)
        for (int i = 0; i < T.n; i++) fprintf(stderr, "entry target %d (%.4f,%.4f) dir (%.4f,%.4f) b20 %d b21 %d b22 %d d28 %.4f A sh %d ent %d t %.4f s1 %d\n", i, T.t[i].pt.x, T.t[i].pt.y, T.t[i].dir.x, T.t[i].dir.y, T.t[i].b20, T.t[i].b21, T.t[i].b22, T.t[i].d28, T.t[i].A.sh, T.t[i].A.ent, T.t[i].A.t, T.t[i].s1);
    for (int i = 0; i < T.n; i++) {
        if (T.t[i].b21) { ti = i; break; }
        if (approach(C, 1, &T.t[i], &S2, &S4, &ap)) { ti = i; break; }
    }
    if (ti < 0) goto done;
    if (T.t[ti].b22) append_runin(C, &T.t[ti], &ap);
    if (!T.t[ti].b21 && sh_len(&ap) < P->approach) { extend_back(S4.n ? &S4 : &S2, &ap); keep_last(P->approach, &ap); }
    ok = 1;
done:
    if (!ok) { C->fp_on = 0; tl_free(&T); free(ap.e); sl_free(&S1); sl_free(&S2); sl_free(&S3); sl_free(&S4); return 0; }
    C->fp_on = P->fp_red > 1e-7 && P->num_red_pass >= 1 && !(P->flags & 0x20000000);
    uint8_t ce = C->fp_on ? 0x24 : 4;
    set_ce(&ap, ce);
    if (!(P->flags & 0x8000000) && (C->T.n || !C->prev.id || fabs(C->prev.z - P->depth) >= 1e-5)) {
        double h = P->micro_lift; if (h < 1e-7 && (P->flags & 0x40000000)) h = P->air_dwell;
        double p10 = (ti >= 0 && ti < T.n && T.t[ti].b22) ? T.t[ti].d28 : 0;
        zprofile(C, 1, h, p10, &ap); set_ce(&ap, ce);
    }
    if (ap.n) {
        P2 S = e_start(&ap.e[0]);
        if (C->T.n) { P2 L; last_pt(C, &L); Ent a = ent_line(L, L); cetp_emit(&C->T, P, &a, 1, 0, 0, 0, 0, 0); Ent b = ent_line(L, S); cetp_emit(&C->T, P, &b, 1, 0, 0, 0, 0, 0); }
        Ent z = ent_line(S, S); z.z = ap.e[0].z; cetp_emit(&C->T, P, &z, ce, (P->flags & 1) ? 0x800 : 0x1000, 0, 0, 0, 0);
    }
    uint32_t *mf = shape_mf(&ap, P->flags & 1, 0); cetp_emit_shape(&C->T, P, &ap, mf); free(mf);
    tl_free(&T); free(ap.e); sl_free(&S1); sl_free(&S2); sl_free(&S3); sl_free(&S4);
    return 1;
}
/* FUN_18005bee0 (l.53064-53158) + FUN_18005bca0 (l.52997-53061): state after the entry */
static void after_entry(Pock *C, ShL *loops, Gbap *G, int *state) {
    const McceParm *P = &C->P;
    P2 cur = p2(0, 0); last_pt(C, &cur);
    Pos out; int found = 0;
    if (loops->n == 1) {
        for (int i = 0; i < C->mat.n; i++) {
            Sh *m = C->mat.s[i];
            if (sh_vs_list(m, loops, 1e-5) == 2) { Pos q; sh_nearest(m, cur, &q); q.sh = i;
                if (!found || pdist2(cur, q.pt) < pdist2(cur, out.pt)) { out = q; found = 1; } }
        }
    }
    if (!found) sl_nearest(&C->mat, cur, &out);
    C->mpos = out;
    G->lead.n = 0;
    if (out.sh < 0) { G->valid = 0; *state = 0; return; }
    if (fabs(pdist(out.pt, cur) - (P->R - P->r)) > eqtol(1e-5)) { general_link(C, G, state); return; }
    Pos q; sl_nearest(&C->cur, cur, &q);
    G->valid = pdist(q.pt, cur) < 1e-7; G->sh = q.sh; G->ent = q.ent; G->t = q.t; G->pt = q.pt;
    *state = G->valid ? 2 : (small_bbox(C, C->mat.s[out.sh], 35.0) ? 1 : 0);
}
/* FUN_180070130 (l.63602-63651) */
static int entry(Pock *C, Gbap *G, int *state) {
    ShL loops = { 0 };
    int ok = approach_outside(C);
    if (!ok) {
        if (C->P.flags & 0x4000000) { C->T.skipped |= 1; sl_free(&loops); return 0; }
        ok = plunge_entry(C, &loops);
        if (!ok) { C->T.fail_plunge++; sl_free(&loops); return 0; }
    }
    after_entry(C, &loops, G, state);
    sl_free(&loops);
    return 1;
}
/* FUN_1800688e0 (l.59755-59962): final exit with a lift */
static void final_exit(Pock *C) {
    ShL S1, S2, S3, S4; region_sets(C, &S1, &S2, &S3, &S4);
    Sh ex; memset(&ex, 0, sizeof ex); int fl;
    if (exit_move(C, &S2, &S4, &ex, &fl)) {
        if (getenv("MCCE_DEBUG")) { fprintf(stderr, "final_exit: %d ents len %.3f vsS2 %d S2 %d shapes:", ex.n, sh_len(&ex), sh_vs_list(&ex, &S2, 1e-5), S2.n);
            for (int k = 0; k < S2.n; k++) fprintf(stderr, " [%s dir %d n %d bb %.2f,%.2f-%.2f,%.2f]", sh_island(S2.s[k]) ? "isl" : "out", S2.s[k]->dir, S2.s[k]->n, S2.s[k]->bb[0], S2.s[k]->bb[1], S2.s[k]->bb[2], S2.s[k]->bb[3]);
            fprintf(stderr, " S4 %d\n", S4.n); }
        set_ce(&ex, 4); zprofile(C, 0, C->P.micro_lift, 0, &ex);
        uint32_t *mf = shape_mf(&ex, C->P.flags & 1, 0); cetp_emit_shape(&C->T, &C->P, &ex, mf); free(mf);
    }
    free(ex.e); sl_free(&S1); sl_free(&S2); sl_free(&S3); sl_free(&S4);
}

/* ============================================================================================ region machine (FUN_18006e620) */
static void region_machine(Pock *C) {
    int state = 0, ramp = 1, last = -1, i = 0, iProg = 0, saved = -1;
    C->memo_on = 0; C->memo[0] = C->memo[1] = 0; C->memo_idx = -1;
    C->nbl = 0;
    if ((C->P.flags & 1) != C->dir0) bidir_toggle(C);
    EnCut E; memset(&E, 0, sizeof E); Gbap G; memset(&G, 0, sizeof G); gbap_clear(&G);
    C->mpos.sh = -1;
    if (!entry(C, &G, &state)) { gbap_free(&G); return; }
    /* FUN_18006ef10: swept boundary output (param_5): not used here */
    while (C->mat.n) {
        if (mcce_abort) break;                                           /* ours: the user's Cancel (mcce_calc_dyn_pock fails) */
        if (C->T.n > 2000000 || i > 200000) { C->T.fail_plunge |= 0x10000; break; }   /* safety cap of this port (not in mcce) */
        if (C->T.last_cut != last) { last = C->T.last_cut; iProg = i; }
        if (iProg + 32 < i) break;
        if (state < 2) state01(C, &ramp, &E, &G, &state);
        else if (state == 2) { state2(C, C->T.n == saved, &ramp, &E, &G, &state); saved = C->T.n; }
        else if (C->mat.n) { bidir_toggle(C); link_state(C, i <= iProg + 16, &G, &state); ramp = 1; }
        i++;
    }
    if (getenv("MCCE_DEBUG")) { fprintf(stderr, "region end: iter %d, %d material loops left:", i, C->mat.n); for (int k = 0; k < C->mat.n && k < 8; k++) { CPath c = sh_to_cpath(C->mat.s[k], g_chord); fprintf(stderr, " [%s %.4f mm2 bb %.2f,%.2f-%.2f,%.2f]", sh_island(C->mat.s[k]) ? "isl" : "out", fabs(cl_area(&c)), C->mat.s[k]->bb[0], C->mat.s[k]->bb[1], C->mat.s[k]->bb[2], C->mat.s[k]->bb[3]); free(c.xy); } fprintf(stderr, "\n"); }
    final_exit(C);
    gbap_free(&G);
}

/* ============================================================================================ PeelAesthetics (§1.2) */
/* mcce PeelAesthetics, export 0x180005b64 = FUN_1800df710 (l.120596-120688), called once by mcmill
   CHST2DProcess::PreparePockBoundsPeel (decomp/mcmill_hst2d.c l.8716-8720). Every step below is decoded (formats/mcce_engines.md
   §1.2 and §6.14); kernels: isect (FUN_180134270), sec_in_list (FUN_1801130f0), sh_section_full (FUN_180146e20), sl_nearest
   (FUN_18013d010), walk_fwd / walk_back (FUN_18006f1c0 / FUN_18006f4f0), sl_offset (MCCECalcOffset, §6.13.1). */
/* FUN_1800de890 (l.120109-120170): class 3 for each B section isab[i] -> isab[i+1] (B = X side) inside A (FUN_1801130f0, tol
   0.001, full when there is one crossing), else 1; true iff exactly 2 sections are inside */
static int pa_classify(const ShL *A, const ShL *B, IsabL *I) {
    int in = 0;
    for (int i = 0; i < I->n; i++) {
        int j = i + 1 == I->n ? 0 : i + 1;
        int r = sec_in_list(B->s[I->r[i].xs], I->r[i].xe, I->r[i].xt, I->r[j].xe, I->r[j].xt, I->n == 1, A, 0.001);
        I->r[i].cls = r == 2 ? 3 : 1; if (r == 2) in++;
    }
    return in == 2;
}
/* FUN_1800de670 (l.120039-120106): Isect(B, A) + classify; on failure B = MCCECalcOffset(1, 1e-4, B, 0x100) and once more
   (the decompile's return value after the retry is a flow artefact: 0 when the retry fails [inferred from the structure]) */
static int pa_isect_inside(const ShL *A, ShL *B, IsabL *I) {
    isect(B, A, I);
    if (pa_classify(A, B, I)) return 1;
    ShL o = sl_offset(B, 1e-4, B->n ? B->s[0]->z : 0); sl_free(B); *B = o;
    isect(B, A, I);
    return pa_classify(A, B, I);
}
/* FUN_1800dea80 (l.120173-120392): the B sections that start at a class-3 crossing (to the next crossing, cyclic) become open
   pieces (zero CSh header, FUN_180146e20 full = 0); exactly 2 required; the one nearest to pt (FUN_18013d010) is "near" */
static int pa_split_inside(P2 pt, const ShL *B, const IsabL *I, int *inear, int *iother, Sh **nearp, Sh **otherp) {
    Sh *pc[2] = { NULL, NULL }; int ix[2] = { -1, -1 }, np = 0, ok = 0;
    *inear = *iother = -1; *nearp = *otherp = NULL;
    for (int i = 0; i < I->n; i++) {
        if (I->r[i].cls != 3) continue;
        int j = i + 1 < I->n ? i + 1 : 0;
        Sh *s = sh_new(); s->z = B->s[I->r[i].xs]->z;
        sh_section_full(B->s[I->r[i].xs], I->r[i].xe, I->r[i].xt, I->r[j].xe, I->r[j].xt, s, 0);
        if (np < 2) { pc[np] = s; ix[np] = i; } else sh_free(s);
        np++;
    }
    if (np == 2) {
        ShL L = { pc, 2, 2 }; Pos q;
        if (sl_nearest(&L, pt, &q) && q.sh >= 0) {
            int o = q.sh == 0; *nearp = pc[q.sh]; *otherp = pc[o]; *inear = ix[q.sh]; *iother = ix[o]; pc[0] = pc[1] = NULL; ok = 1;
        }
    }
    sh_free(pc[0]); sh_free(pc[1]);
    return ok;
}
/* first entity start / end point as FUN_1800de040 reads them ('L' e1 / e2, 'A' sa / sa + sw, other +0x18) */
static P2 pa_ent_start(const Ent *e) { return e->id == 'A' ? e_at(e, 0) : e->p1; }
static P2 pa_ent_end(const Ent *e) { return e->id == 'L' || e->id == 'A' ? e_at(e, 1) : e->p1; }
/* FUN_1800de040 (l.119886-119967): S = start of entity 0 of the first piece, E = end of entity (number of pieces - 1) of it -
   the decompile indexes the entity by the piece count (1 here, so entity 0); q = mid(S, E) + d4 x the left normal of S -> E
   (|E - S| >= 1e-12 required) */
static int pa_far_point(const Sh *far, double d4, P2 *q) {
    if (!far->n) return 0;
    P2 S = pa_ent_start(&far->e[0]), E = pa_ent_end(&far->e[0]);
    double dx = E.x - S.x, dy = E.y - S.y, L = sqrt(dx * dx + dy * dy);
    if (L < 1e-12) return 0;
    *q = p2((E.x + S.x) * 0.5 - dy / L * d4, (E.y + S.y) * 0.5 + d4 * (dx / L));
    return 1;
}
/* step 1, FUN_1800dfaf0 (l.120720-121022): push A out round the far B piece */
static int pa_step1(double R, double mr, P2 P, ShL *a, const ShL *b) {
    double d4 = R + mr + R * 0.25, z = a->s[0]->z;
    ShL b2 = sl_copy(b); IsabL I = { 0 }, I2 = { 0 }; int ok = 0;
    Sh *nearB = NULL, *farB = NULL, *n2 = NULL, *o2 = NULL; int i0, i1, j0, j1; ShL ring = { 0 };
    if (!pa_isect_inside(a, &b2, &I)) goto done;
    if (!pa_split_inside(P, &b2, &I, &i0, &i1, &nearB, &farB)) goto done;
    P2 q; if (!pa_far_point(farB, d4, &q)) goto done;
    /* Retrace(farB) l.120855 + MCCECalcOffset(1, d4, farB, 0x100) l.120856-120858: the region of the there-and-back loop grown
       by d4 = the area the disc of radius d4 sweeps along the open piece (the Clipper bridge cannot offset a zero-area loop) */
    {   CPath c = sh_to_cpath(farB, g_chord); CPaths in = { &c, 1 }, o = cl_offset_lines(&in, d4, 0, g_chord);
        ring = cpaths_to_sl(&o, z); free(c.xy); cl_free(&o); }
    if (ring.n != 1) goto done;
    if (ring.s[0]->dir != 1) sh_rev(ring.s[0]);                             /* l.120859-120862 */
    if (!pa_isect_inside(a, &ring, &I2)) goto done;
    if (!pa_split_inside(q, &ring, &I2, &j0, &j1, &n2, &o2)) goto done;
    {   int k = j1 + 1 < I2.n ? j1 + 1 : 0;
        Sh *na = sh_copy(a->s[0]); na->n = 0;                               /* copy of a[0]'s header, entities cleared (FUN_1800137a0) */
        sh_section_full(ring.s[I2.r[j1].xs], I2.r[j1].xe, I2.r[j1].xt, I2.r[k].xe, I2.r[k].xt, na, 0);   /* l.120878-120883 */
        sh_section_full(a->s[I2.r[k].ys], I2.r[k].ye, I2.r[k].yt, I2.r[j1].ye, I2.r[j1].yt, na, 0);      /* l.120885-120891 */
        sl_clear(a); sl_push(a, na); ok = 1; }
done:
    sh_free(nearB); sh_free(farB); sh_free(n2); sh_free(o2); sl_free(&ring); sl_free(&b2); il_free(&I); il_free(&I2);
    return ok;
}
/* cubic de Casteljau FUN_1800aa8d0 (l.92765-92790) */
static P2 pa_bez(const P2 c[4], double t) {
    P2 a = padd(c[0], pmul(psub(c[1], c[0]), t)), b = padd(c[1], pmul(psub(c[2], c[1]), t)), d = padd(c[2], pmul(psub(c[3], c[2]), t));
    P2 e = padd(a, pmul(psub(b, a), t)), f = padd(b, pmul(psub(d, b), t));
    return padd(e, pmul(psub(f, e), t));
}
/* split at t: left half to l, right half to r (FUN_1800ae060 l.94475-94557) */
static void pa_bez_split(const P2 c[4], double t, P2 l[4], P2 r[4]) {
    P2 a = padd(c[0], pmul(psub(c[1], c[0]), t)), b = padd(c[1], pmul(psub(c[2], c[1]), t)), d = padd(c[2], pmul(psub(c[3], c[2]), t));
    P2 e = padd(a, pmul(psub(b, a), t)), f = padd(b, pmul(psub(d, b), t)), m = padd(e, pmul(psub(f, e), t));
    l[0] = c[0]; l[1] = a; l[2] = e; l[3] = m; r[0] = m; r[1] = f; r[2] = d; r[3] = c[3];
}
/* AppendBezier FUN_1800a5b00 (l.90456-90566): split first at the inflection parameters in (0, 1) (FUN_1800ae3c0 /
   FUN_1800aa070: roots of cross(B', B''); processed from the largest, the next rescaled by it - so the roots are taken
   ascending [the root order of FUN_1800afd30 is inferred]); then a stack: the top cubic becomes the line P0 -> P3
   (FUN_1800aafd0) if Bez(0.25 / 0.5 / 0.75) are all within tol of it (FUN_1800aca30 with GetNearestEntPt), else it is split at
   0.5 and the first half is tried next */
static void pa_append_bezier(double tol, const P2 c0[4], Sh *out) {
    P2 st[64][4]; int n = 0;
    /* inflections: B'(t) = 3[(P1-P0)(1-t)^2 + 2(P2-P1)t(1-t) + (P3-P2)t^2]; cross(B', B'') = 0 is a quadratic in t */
    P2 a = psub(c0[1], c0[0]), b = psub(c0[2], c0[1]), c = psub(c0[3], c0[2]);
    P2 A2 = padd(psub(a, pmul(b, 2)), c), B2 = psub(b, a);              /* B'/3 = A2 t^2 + 2 B2 t + a */
    double qa = pcross(A2, B2), qb = pcross(A2, a), qc = pcross(B2, a); /* cross(B', B'') / 18 = qa t^2 + qb t + qc */
    double rt[2]; int nr = 0;
    if (fabs(qa) > 1e-300) { double D = qb * qb - 4 * qa * qc; if (D >= 0) { double sq = sqrt(D); rt[nr++] = (-qb - sq) / (2 * qa); rt[nr++] = (-qb + sq) / (2 * qa); } }
    else if (fabs(qb) > 1e-300) rt[nr++] = -qc / qb;
    double keep[2]; int nk = 0;
    for (int i = 0; i < nr; i++) if (rt[i] >= 0 && rt[i] <= 1) keep[nk++] = rt[i];
    if (nk == 2 && keep[0] > keep[1]) { double t = keep[0]; keep[0] = keep[1]; keep[1] = t; }
    memcpy(st[n++], c0, sizeof st[0]);
    for (int i = nk - 1; i >= 0; i--) {
        double t = keep[i];
        P2 l[4], r[4]; n--; pa_bez_split(st[n], t, l, r); memcpy(st[n++], r, sizeof st[0]); memcpy(st[n++], l, sizeof st[0]);
        if (i > 0) { if (fabs(t) <= 1e-12) return; keep[i - 1] /= t; }
    }
    while (n > 0) {
        P2 *t = st[n - 1]; Ent e = ent_line(t[0], t[3]); int ok = 1;
        for (int k = 1; k <= 3 && ok; k++) { double u; P2 qn; P2 bp = pa_bez(t, 0.25 * k); e_near_far(&e, bp, 0, &u, &qn); if (pdist2(qn, bp) > tol * tol) ok = 0; }
        if (ok || n >= 63) { sh_push(out, e); n--; continue; }
        P2 l[4], r[4]; pa_bez_split(t, 0.5, l, r); n--; memcpy(st[n++], r, sizeof st[0]); memcpy(st[n++], l, sizeof st[0]);
    }
}
static double pa_unit_to(P2 from, P2 to, P2 *u) { P2 d = psub(to, from); double l = plen(d); if (l < 1e-12) return 0; *u = pmul(d, 1 / l); return l; }
/* step 2, FUN_1800dc820 (l.119206-119393) / FUN_1800dcce0 (l.119396-119610): round the two corners where B meets A */
static int pa_step2(double R, double mr, P2 P, const ShL *a, const ShL *b, ShL *bout) {
    ShL b2 = sl_copy(b); IsabL I = { 0 }; int ok = 0, p8, p9;
    Sh *nearB = NULL, *otherB = NULL, *ap = NULL;
    if (!pa_isect_inside(a, &b2, &I)) goto done;
    if (!pa_split_inside(P, &b2, &I, &p8, &p9, &nearB, &otherB)) goto done;
    {   int i1 = p8 + 1 < I.n ? p8 + 1 : 0;
        if (I.r[p8].next != i1) goto done;                                  /* FUN_1800df120: isab[p8].+0x30 == p8 + 1 */
        ap = sh_new(); ap->z = a->s[I.r[p8].ys]->z;
        sh_section_full(a->s[I.r[p8].ys], I.r[p8].ye, I.r[p8].yt, I.r[i1].ye, I.r[i1].yt, ap, 0); }
    double h = R + mr, K = 0.5522847498307935;
    /* lengths FUN_1800de390 (l.119970-120036) */
    if (!ap->n || !nearB->n) goto done;
    double LA = sh_len(ap), LB = sh_len(nearB), L = (ap->n == 3 && nearB->n == 1) ? e_len(&ap->e[0]) : (LA - LB) / 2;
    if (L < 2 * h || (LB - 1e-4) / 2 < h) goto done;
    double s1 = 2 * h, s2 = fmin(1.5 * h, (LB - 1e-4) / 2);
    if (s2 < 1e-4) goto done;
    ShL AP = { &ap, 1, 1 }, NB = { &nearB, 1, 1 };
    P2 C1[4], C2[4], u;
    {   /* corner 1 FUN_1800dd510 (l.119613-119723) */
        Pos st = { 0, 0, 0, e_start(&ap->e[0]) }, o; P2 S = st.pt;
        if (!walk_fwd(0, s1, &AP, &st, &o, NULL)) goto done; C1[0] = o.pt;
        Pos sb = { 0, 0, 0, e_start(&nearB->e[0]) };
        if (!walk_fwd(0, s2, &NB, &sb, &o, NULL)) goto done; C1[3] = o.pt;
        if (pa_unit_to(C1[0], S, &u) == 0) goto done; C1[1] = padd(C1[0], pmul(u, K * h));
        if (pa_unit_to(C1[3], S, &u) == 0) goto done; C1[2] = padd(C1[3], pmul(u, K * h));
        if (sl_inside(a, 1e-5, pa_bez(C1, 0.5)) != 2) goto done; }
    {   /* corner 2 FUN_1800dd9f0 (l.119726-119883) */
        Pos ea = { 0, ap->n - 1, 1, e_end(&ap->e[ap->n - 1]) }, eb = { 0, nearB->n - 1, 1, e_end(&nearB->e[nearB->n - 1]) }, o;
        P2 E = eb.pt;
        if (!walk_back(0, s1, &AP, &ea, &o, NULL)) goto done; C2[3] = o.pt;
        if (!walk_back(0, s2, &NB, &eb, &o, NULL)) goto done; C2[0] = o.pt;
        if (pa_unit_to(C2[0], E, &u) == 0) goto done; C2[1] = padd(C2[0], pmul(u, K * h));
        if (pa_unit_to(C2[3], E, &u) == 0) goto done; C2[2] = padd(C2[3], pmul(u, K * h));
        if (sl_inside(a, 1e-5, pa_bez(C2, 0.5)) != 2) goto done; }
    {   /* assembly (l.119491-119606): header of b2[0], entities rebuilt */
        int i1 = p8 + 1 < I.n ? p8 + 1 : 0, i2 = p9 + 1 < I.n ? p9 + 1 : 0;
        Sh *nb = sh_copy(b2.s[0]); nb->n = 0;
        sh_section_full(b2.s[I.r[i1].xs], I.r[i1].xe, I.r[i1].xt, I.r[p9].xe, I.r[p9].xt, nb, 0);
        sh_section_full(a->s[I.r[p9].ys], I.r[p9].ye, I.r[p9].yt, I.r[i2].ye, I.r[i2].yt, nb, 0);
        sh_section_full(b2.s[I.r[i2].xs], I.r[i2].xe, I.r[i2].xt, I.r[p8].xe, I.r[p8].xt, nb, 0);
        if (nb->n < 3) { sh_free(nb); goto done; }
        sh_push(nb, ent_line(pa_ent_end(&nb->e[nb->n - 1]), C1[0])); pa_append_bezier(1e-4, C1, nb);
        sh_push(nb, ent_line(C1[3], C2[0])); pa_append_bezier(1e-4, C2, nb);
        sh_push(nb, ent_line(C2[3], pa_ent_start(&nb->e[0])));
        sh_upd_bbox(nb);
        sl_clear(bout); sl_push(bout, nb); ok = 1; }
done:
    sh_free(nearB); sh_free(otherB); sh_free(ap); sl_free(&b2); il_free(&I);
    return ok;
}
static McceBnd *pa_to_bnds(const ShL *l, int *n) {
    McceBnd *r = calloc(l->n + 1, sizeof(McceBnd)); *n = 0;
    for (int i = 0; i < l->n; i++) {
        CPath c = sh_to_cpath(l->s[i], g_chord);
        r[*n].xy = c.xy; r[*n].n = c.n; r[*n].island = sh_island(l->s[i]); r[*n].open = l->s[i]->dir == 0; (*n)++;
    }
    return r;
}
/* FUN_1800df710 + FUN_1800df410 (l.120491-120593): registry bIsPeelAestheticsAvail (default 1) and bOverrideMinPathRad (default 0)
   at their defaults; mr floored to 0.05 R (GetMinRadK); a / b must hold one shape each; both turned CCW (dir +1) first.
   Returns 1 when the lists were replaced (*ao / *bo malloc'd, the caller frees xy + array), 0 = keep the inputs. */
int mcce_peel_aesthetics(double R, double mr, double px, double py, double chain_tol, const McceBnd *A, int na, const McceBnd *B, int nb,
                         McceBnd **ao, int *nao, McceBnd **bo, int *nbo) {
    *ao = *bo = NULL; *nao = *nbo = 0;
    if (mr < 0.05 * R) mr = 0.05 * R;
    g_chord = fmax(chain_tol, 0.002 * R); tess_reset(); g_mat_ver++;   /* ours: a new run never sees the last run's measure records (a freed Pock's address comes back: same set pointer, same version) */
    ShL a = bnds_to_sl(A, na, 0), b = bnds_to_sl(B, nb, 0); int ok = 0;
    if (a.n == 1 && b.n == 1) {
        ShL a1 = sl_copy(&a), b1 = sl_copy(&b);
        if (a1.s[0]->dir != 1) sh_rev(a1.s[0]);
        if (b1.s[0]->dir != 1) sh_rev(b1.s[0]);
        P2 P = p2(px, py);
        if (pa_step1(R, mr, P, &a1, &b1)) {
            ok = 1;
            ShL bn = { 0 };
            if (pa_step2(R, mr, P, &a1, &b1, &bn)) { sl_free(&b1); b1 = bn; } else sl_free(&bn);
            *ao = pa_to_bnds(&a1, nao); *bo = pa_to_bnds(&b1, nbo);
        }
        sl_free(&a1); sl_free(&b1);
    }
    sl_free(&a); sl_free(&b);
    return ok;
}

/* ours: the wall offsets below work on chords (bnds_to_sl: McceBnd is a polyline at the chain tolerance); MCCECalcOffset works on the chain's lines and arcs
   (decomp/mccore_pock.c chain_to_pock_bound). */
/* the dummy-box offset of mcmill FUN_1801ac7e0 (decomp/mcmill_hst2d.c l.17371-17416 / l.17477-17522 regions, l.17194-17278 islands):
   the bound is flagged an island of the box AddDummyOuter FUN_1801abf80 (decomp/mcmill_all.c l.297424: bbox grown along its
   diagonal direction by 10 x |d| + 1), MCCECalcOffset(1, |d|, list, 0x101, 0, 1e-6, 1e-6) grows the region between them, so the
   hole - the bound - shrinks; with fewer than two output shapes nothing is left (LAB_1801ad23b, l.17208-17212), else the
   largest (pock_ccw area: the box) is dropped. Every shape keeps `dir` (MCCECalcOffset keeps the input orientation). */
static ShL wall_dummy(const ShL *one, double ad, int dir) {
    ShL L = { 0 }, r = { 0 }; double bb[4]; sl_bbox(one, bb);
    double dx = bb[2] - bb[0], dy = bb[3] - bb[1], h = hypot(dx, dy), ex = ad * 10.0 + 1.0;
    double vx = h > 0 ? dx / h * ex : 0, vy = h > 0 ? dy / h * ex : 0;
    sl_push(&L, rect_sh(bb[0] - vx, bb[1] - vy, bb[2] + vx, bb[3] + vy, 0));
    Sh *is = sh_copy(one->s[0]); is->fl |= 1; sl_push(&L, is);
    ShL g = sl_offset_m(&L, ad, 0);
    sl_free(&L);
    if (g.n >= 2) {
        int big = -1; double ba = -1;
        for (int k = 0; k < g.n; k++) { CPath c = sh_to_cpath(g.s[k], g_chord); double a = fabs(cl_area(&c)); free(c.xy); if (a > ba) { ba = a; big = k; } }
        for (int k = 0; k < g.n; k++) if (k != big && g.s[k]->n) { Sh *c = sh_copy(g.s[k]); c->fl &= ~1; if (c->dir != dir) sh_rev(c); sl_push(&r, c); }
    }
    sl_free(&g);
    return r;
}
static void bnds_add_sl(McceBnd **r, int *n, int *cap, const ShL *g, int island) {
    for (int k = 0; k < g->n; k++) {
        if (g->s[k]->dir == 0 || !g->s[k]->n) continue;
        CPath c = sh_to_cpath(g->s[k], g_chord);
        if (*n == *cap) { *cap *= 2; *r = realloc(*r, *cap * sizeof(McceBnd)); }
        (*r)[*n].xy = c.xy; (*r)[*n].n = c.n; (*r)[*n].island = island; (*r)[*n].open = 0; (*n)++;
    }
}
/* mcmill FUN_1801ac7e0 (decomp/mcmill_hst2d.c l.17164), islands, param_4 = s: each avoidance chain on its own (no union, mcmill
   passes them one by one). s > 0 (l.17281-17321): MCCECalcOffset(1, s, island, 0x101, 0, 1e-6, 1e-6), every output shape an island.
   s < 0 (l.17194-17278): the dummy-box offset by |s|. Our MCCECalcOffset is the exact-model sl_offset_m (Clipper with
   MCCE_MEASURE=clipper). Direction of the s > 0 growth: measured with the DLL's own MCCECalcOffset on the island-flagged chain
   (peloader MCCE_GROWISL, handoff 2026-11-07 evening: cut area 577 -> 570 = the NCI's): the chain is offset as a non-island
   shape by +s. MCCECalcOffset keeps the orientation of its input (note at sl_offset above). Returns malloc'd bnds (the caller
   frees xy + array). */
int mcce_grow_islands(double d, double chord, const McceBnd *in, int n, McceBnd **out, int *nout) {
    g_chord = chord; tess_reset();
    int cap = 4 * n + 4; McceBnd *r = calloc(cap, sizeof(McceBnd)); *nout = 0;
    for (int i = 0; i < n; i++) {
        McceBnd b = in[i]; b.island = 0; b.open = 0;
        ShL one = bnds_to_sl(&b, 1, 0);
        if (!one.n) { sl_free(&one); continue; }
        int dir = one.s[0]->dir;
        ShL g;
        if (d < -1e-12) g = wall_dummy(&one, -d, dir);
        else {
            g = d > 1e-12 ? sl_offset_m(&one, d, 0) : sl_copy(&one);
            for (int k = 0; k < g.n; k++) if (g.s[k]->dir && g.s[k]->dir != dir) sh_rev(g.s[k]);
        }
        bnds_add_sl(&r, nout, &cap, &g, 1);
        sl_free(&g); sl_free(&one);
    }
    *out = r;
    return *nout;
}
/* mcmill FUN_1801ac7e0, region, param_3 = d (the callers: ComputeOffsetsAtDepth + CHST2DDynamic vtable +0x38, PackageSomething l.5703-5722):
   |d| <= 1e-12: unchanged (l.17326-17327).
   Grown, the largest piece kept (l.17426-17476 d > 0 from outside, l.17329-17369 d < 0 stay inside): MCCECalcOffset(1, |d|, region, 0x101).
   Dummy box, every piece kept (l.17477-17522 d > 0 stay inside, l.17371-17416 d < 0 from outside): wall_dummy.
   from_out = cur_op +0x14ff = record 0x1567. Each output is one work item (PackageSomething l.5766-5821), chain direction kept. */
int mcce_wall_region(double d, int from_out, double chord, const McceBnd *reg, McceBnd **out, int *nout) {
    g_chord = chord; tess_reset();
    int cap = 8; McceBnd *r = calloc(cap, sizeof(McceBnd)); *nout = 0;
    McceBnd b = *reg; b.island = 0; b.open = 0;
    ShL one = bnds_to_sl(&b, 1, 0), g = { 0 };
    if (one.n) {
        int dir = one.s[0]->dir;
        if (fabs(d) <= 1e-12) g = sl_copy(&one);
        else if ((d > 0) == (from_out != 0)) {
            /* pock_ccw (decomp/mccore_pock.c l.2424: *area is signed, negative for counter-clockwise), compared signed (l.17333-17341, l.17431-17439): the greatest
               value; MCCECalcOffset keeps the input orientation, a hole of the output is the opposite way round */
            ShL o = sl_offset_m(&one, fabs(d), 0); int big = -1; double ba = -1e300;
            for (int k = 0; k < o.n; k++) { if (!o.s[k]->n || o.s[k]->dir == 0) continue; CPath c = sh_to_cpath(o.s[k], g_chord); double a = fabs(cl_area(&c)); free(c.xy); int orient = sh_island(o.s[k]) ? -dir : dir; a = -orient * a; if (a > ba) { ba = a; big = k; } }
            if (big >= 0) { Sh *c = sh_copy(o.s[big]); c->fl &= ~1; if (c->dir != dir) sh_rev(c); sl_push(&g, c); }
            sl_free(&o);
        } else g = wall_dummy(&one, fabs(d), dir);
        bnds_add_sl(&r, nout, &cap, &g, 0);
    }
    sl_free(&g); sl_free(&one);
    *out = r;
    return *nout;
}

/* ============================================================================================ top level */
static void pock_free(Pock *C) {
    ShL *ls[] = { &C->M, &C->tcr, &C->mat_all, &C->bull_all, &C->cur, &C->c3c0, &C->c3e8, &C->mat, &C->bull, &C->s3, &C->air };
    for (unsigned k = 0; k < sizeof ls / sizeof *ls; k++) sl_free(ls[k]);
    free(C->T.r); free(C->bl); free(C->win1.e); free(C->win2.e);
}
/* MCCECalcDynPock body (FUN_180011710 / FUN_1800124d0, mcce_engines.md B.5) on shape lists: S1 = A (motion), S2 = B (material),
   S3 = T3 (taken over); prev = the caller's previous-position entity P+0x118 (pock+0x120; NULL = none: FUN_180012390 fills it
   from AIR). Returns the finished Pock (records in C->T); the caller frees A / B and the Pock. */
static Pock *dyn_pock_run(const McceParm *pin, ShL A, ShL B, ShL T3, const Ent *prev) {
    Pock *C = calloc(1, sizeof(Pock));
    C->P = *pin; parm_derive(&C->P);
    McceParm *P = &C->P;
    C->z = 0; C->dir0 = P->flags & 1; C->T.last_cut = -1;
    g_chord = fmax(P->chain_tol, 0.002 * P->R); tess_reset(); g_mat_ver++;   /* (see above) */
    for (int i = 0; i < T3.n; i++) T3.s[i]->dir = 0;
    /* FUN_180090290 (l.79775-79863) */
    Sh *rect_keep = NULL;
    if (B.n) prep_material(P, &A, &B, 0, &C->M, &C->air);
    else if (P->flags & 0x200000) prep_ctour(P, &A, 0, &C->M, &C->air);
    else if (P->flags & 0x10) C->M = prep_inside(&A, 0);
    else if (P->flags & 0x40) {
        prep_facing(P, &A, 0, &C->M, &C->air);
        double rr = P->var_rad_avail ? P->var_rad : P->min_rad, m = 2 * (2 * P->R + 2 * rr + P->approach + P->first_off), bb[4]; sl_bbox(&A, bb);
        rect_keep = rect_sh(bb[0] - m, bb[1] - m, bb[2] + m, bb[3] + m, 0);
    }
    C->s3 = T3; T3.s = NULL; T3.n = 0;
    /* FUN_180012390 (l.15564-15617): previous position = first entity of the first non-island AIR shape, z = depth + 1
       (the AIR outer is the START_FACING rectangle, kept with its own entity order: FUN_180124c50) */
    memset(&C->prev, 0, sizeof C->prev);
    if (prev && (prev->id == 'A' || prev->id == 'L' || prev->id == 'P')) C->prev = *prev;   /* l.15564: given -> kept */
    else for (int i = 0; i < C->air.n; i++) if (!sh_island(C->air.s[i]) && C->air.s[i]->n) {
        const Sh *s = C->air.s[i];
        if (rect_keep && fabs(s->bb[0] - rect_keep->bb[0]) < 1e-6 && fabs(s->bb[2] - rect_keep->bb[2]) < 1e-6 && fabs(s->bb[1] - rect_keep->bb[1]) < 1e-6 && fabs(s->bb[3] - rect_keep->bb[3]) < 1e-6) C->prev = rect_keep->e[0];
        else C->prev = s->e[0];
        C->prev.z = 1.0; break;
    }
    sh_free(rect_keep);
    regions_build(C);
    while (!mcce_abort && !C->nd[0] && region_next(C)) region_machine(C);
    return C;
}
int mcce_calc_dyn_pock(const McceParm *pin, const McceBnd *s1, int n1, const McceBnd *s2, int n2, const McceBnd *s3, int n3,
                       McceRec **out, int *nout, char *err, int errn) {
    mcce_trace_input(pin, s1, n1, s2, n2, s3, n3);   /* ours: MCCE_DUMPIN (mcce_trace.c) */
    if (mcce_trace_recs_in(out, nout)) return 1;     /* ours: MCCE_RECSIN = records of the original engine (tools/peloader --dyn --raw) */
    ShL A = bnds_to_sl(s1, n1, 0), B = bnds_to_sl(s2, n2, 0), T3 = bnds_to_sl(s3, n3, 0);
    Pock *C = dyn_pock_run(pin, A, B, T3, NULL);
    McceParm *P = &C->P;
    if (C->nd[0]) { snprintf(err, errn, "%s", C->nd); *out = NULL; *nout = 0; sl_free(&A); sl_free(&B); pock_free(C); free(C); return 0; }   /* ours: NOT DECODED entry */
    if (mcce_abort) { snprintf(err, errn, "cancelled"); *out = NULL; *nout = 0; sl_free(&A); sl_free(&B); pock_free(C); free(C); return 0; }   /* ours: Cancel */
    /* records -> output with absolute Z */
    *out = malloc((C->T.n ? C->T.n : 1) * sizeof(McceRec)); *nout = 0;
    /* decisions batch dec2 (Q1): the first record starts at its own Z. Mastercam's OutputMotionToBnci writes every record
       straight to its own XYZ (decomp/mcmill_hst2d.c l.4445 G0 / G1 = record flag, l.4534-4540 gr_lin_mill), so its NCI goes
       feed plane -> G1 to the first record's Z; FUN_180012390's previous-position Z (depth + 1, l.15564-15617, C->prev) is the
       engine's own state, not a motion (it was used here as the first record's start Z: a plunge to depth + 1). */
    double zprev = C->T.n ? C->T.r[0].e.z : P->depth;
    for (int i = 0; i < C->T.n; i++) {
        const Rec *r = &C->T.r[i]; McceRec o; memset(&o, 0, sizeof o);
        o.id = r->e.id; o.x1 = r->e.p1.x; o.y1 = r->e.p1.y; o.x2 = r->e.p2.x; o.y2 = r->e.p2.y;
        o.cx = r->e.c.x; o.cy = r->e.c.y; o.r = r->e.r; o.sa = r->e.sa; o.sw = r->e.sw;
        o.z0 = zprev; o.z1 = r->e.z; zprev = o.z1;
        o.ce = r->e.ce; o.mf = r->mf; o.feed = r->feed;
        (*out)[(*nout)++] = o;
        if (getenv("MCCE_DEBUG") && atoi(getenv("MCCE_DEBUG")) >= 2)
            fprintf(stderr, "rec %d %c (%.3f,%.3f)-(%.3f,%.3f) z %.3f ce %02x mf %04x feed %.0f\n", i, o.id ? o.id : '?', o.id == 'A' ? o.cx + o.r * cos(o.sa) : o.x1, o.id == 'A' ? o.cy + o.r * sin(o.sa) : o.y1,
                    o.id == 'A' ? o.cx + o.r * cos(o.sa + o.sw) : o.x2, o.id == 'A' ? o.cy + o.r * sin(o.sa + o.sw) : o.y2, o.z1, o.ce, o.mf, o.feed);
        if (getenv("MCCE_DEBUG") && atoi(getenv("MCCE_DEBUG")) >= 3) {   /* ours: full precision, as tools/peloader --out */
            if (o.id == 'A') fprintf(stderr, "  c %.6f %.6f r %.6f sa %.6f sw %.6f\n", o.cx, o.cy, o.r, o.sa, o.sw);
            else fprintf(stderr, "  %.6f %.6f %.6f %.6f\n", o.x1, o.y1, o.x2, o.y2);
        }
    }
    if (getenv("MCCE_DEBUG")) fprintf(stderr, "mcce: %d records, %ld measures, fail_plunge %d, exact regions %ld (%ld Clipper fallbacks)\n", C->T.n, C->nmeasure, C->T.fail_plunge, g_exr_n, g_exr_fb);
    sl_free(&A); sl_free(&B); pock_free(C); free(C);
    return 1;
}
