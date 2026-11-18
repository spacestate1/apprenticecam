/* draft_pick - the pick of a drafting entity under the cursor (see draft_pick.h).
 *
 * mastercam.dll FUN_18020f4e0 (select_ent_univ's body, decomp/draft/ord_mc_seldim.c 63-951): every entity of the display list that
 * matches the selection mask gets a distance from the cursor (in proj_iso_pt's 2-D: the graphics view's x / y axes); the winner is the
 * strictly nearest below sel_dist (l.1802101c9: dist < best && dist < sel_dist; ties keep the first). Its drafting cases (l.576-660):
 *   N_ID / W_ID (0x10 / 0x20): in the graphics view (ent +0x88) or iso_view_dimensions; mask 0x10 / 0x20: the text if kind > 1
 *     (FUN_18020ae30 -> FUN_18020b380), the leader chain at +0x100 if kind < 3 and it is on (FUN_180208d20, depth +0x80)
 *   D_ID (0x40): in the graphics view (ent +0x328) or iso_view_dimensions; mask 0x40 / 0x10: the texts (FUN_18020b380: value text via
 *     DfCheckForTokens, tolerance texts 1 / 2); mask 0x40 / 0x20: the four witness / leader slots +0x378, +0x410, +0x4a8, +0x540 when
 *     on (FUN_180208d20, depth +0x320)
 * FUN_18020b380 -> mccore distance_to_string (decomp/draft/ord_core_pick.c 174): the string drawn by CDrawString in its distance mode
 *   (mode 2: each emitted segment, proj_iso_pt + dist_to_finite_line, text1.c 559-570) - here viewer/draft.c draft_render_text's
 *   segments; TrueType text (FUN_18020ae80) NOT PORTED (no text distance)
 * FUN_180208d20 (ord_mc_seldim2.c 26): the chain (+0x84 mTableFindData), each FUN_18020b5c0 (ord_mc_seldim2.c 321): on and visible:
 *   kind < 2: the polyline (FUN_18020bba0 inline points / FUN_18020bb00 memory-table aPoints -> FUN_18020b870: aPoint2Plane at the
 *   depth, proj_iso_pt, dist_to_finite_line per segment, ord_mc_pick.c, FUN_18020b870); kind 2: the arc (FUN_18020ba20 -> dist_to_arc,
 *   ord_core_pick2.c 77; another view: dist_to_arc_by_pieces NOT PORTED); then the arrowhead: calc_arrow_vec (ord_core_pick.c 3) ->
 *   FUN_18020a1a0 (ord_mc_pick.c) by arrow style
 * Not ported: the level mask / blanking / selection-bit tests of the display-list walk (every record given is taken as live and shown),
 * the arrowhead of an arc leader (calc_arrow_vec kind 2 + FUN_180314cf0), arrow styles 6 / 7 / 9, greeking. */
#include "draft_pick.h"
#include "mcam_load.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TOL (mc_systol.tol)                                         /* tol_exref (config 1000, mc_systol) */
#define TWOPI 6.283185307179586

static int16_t I16(const uint8_t *b, size_t o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static int32_t I32(const uint8_t *b, size_t o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static double D(const uint8_t *b, size_t o) { double v; memcpy(&v, b + o, 8); return v; }

typedef struct { const DpView *V; const double *C; double lim, best; int ok; int view; double vm[9]; double depth; } Acc;

static void iso(const DpView *V, const double w[3], double p[2]) {   /* proj_iso_pt (ord_core_pick2.c 50) */
    p[0] = V->gm[0] * w[0] + V->gm[1] * w[1] + V->gm[2] * w[2]; p[1] = V->gm[3] * w[0] + V->gm[4] * w[1] + V->gm[5] * w[2];
}
static void v2w(const double m[9], const double v[3], double w[3]) { for (int i = 0; i < 3; i++) w[i] = m[i] * v[0] + m[3 + i] * v[1] + m[6 + i] * v[2]; }
static void w2v(const double m[9], const double w[3], double v[3]) { for (int i = 0; i < 3; i++) v[i] = m[3 * i] * w[0] + m[3 * i + 1] * w[1] + m[3 * i + 2] * w[2]; }

/* dist_to_finite_line @1804b6680 (decomp/draft/text1.c 3154-3205): 1 = a distance below LIM */
static int dist_fl(const double C[2], const double a[2], const double b[2], double lim, double *d) {
    double x0 = a[0], y0 = a[1], dx = b[0] - x0, dy = b[1] - y0, L = sqrt(dx * dx + dy * dy), q;
    if (TOL <= L) {
        double h = fabs((C[0] - x0) * dy - (C[1] - y0) * dx) / L; *d = h;
        if (lim <= h) return 0;
        double s = ((C[0] - x0) * dx + (C[1] - y0) * dy) / L;
        if (0.0 <= s) { if (s <= L) return 1; if (L + lim <= s) return 0; x0 = b[0]; y0 = b[1]; }
        else if (s <= -lim) return 0;
        q = (y0 - C[1]) * (y0 - C[1]) + (x0 - C[0]) * (x0 - C[0]);
    } else q = (y0 - C[1]) * (y0 - C[1]) + (x0 - C[0]) * (x0 - C[0]);
    *d = sqrt(q);
    return *d < lim;
}
static void take(Acc *A, double d) { if (d < A->best) { A->best = d; A->ok = 1; } }   /* dist < best (the callers' running minimum) */
static void seg_world(Acc *A, const double w0[3], const double w1[3]) {
    double p[2], q[2], d; iso(A->V, w0, p); iso(A->V, w1, q);
    if (dist_fl(A->C, p, q, A->lim, &d)) take(A, d);
}
/* a view point at the depth -> world (aPointInitView 'K' + aPoint2Plane / view_to_world with z = the depth) */
static void vw(const Acc *A, double x, double y, double w[3]) { double v[3] = { x, y, A->depth }; v2w(A->vm, v, w); }

/* DraftOut callback: the text strokes (distance_to_string's segments) */
static void text_poly(void *ctx, const double *xyz, int n, int closed, int fill, int color, int view) {
    Acc *A = ctx; (void)fill; (void)color; (void)view;
    for (int i = 0; i + 1 < n; i++) seg_world(A, xyz + 3 * i, xyz + 3 * i + 3);
    if (closed && n > 2) seg_world(A, xyz + 3 * (n - 1), xyz);
}

/* dist_to_arc @1802db870 (ord_core_pick2.c 77): same view: |distance to the centre - r| below both limits and the angle inside the sweep */
static void arc_dist(Acc *A, double cx, double cy, double r, double st, double sw) {
    double C[2] = { A->C[0], A->C[1] };
    double e = sqrt((cy - C[1]) * (cy - C[1]) + (cx - C[0]) * (cx - C[0])) - r;
    if (!(fabs(e) < A->lim)) return;
    double dx = C[0] - cx, dy = C[1] - cy, a = 1e-12 <= dy * dy + dx * dx ? atan2(dy, dx) : 0.0;
    if (a < 0.0) a += TWOPI;
    if (!(st <= a) && 0.0 < sw) a += TWOPI;
    else if (st < a && sw < 0.0) a -= TWOPI;
    if ((0.0 < sw && st <= a && a <= sw + st) || (sw < 0.0 && a <= st && sw + st <= a)) take(A, fabs(e));
}

/* FUN_18020a1a0: the arrowhead's lines; L = calc_arrow_vec's line (view): [0..2] the point arrow-length along the leader, [3..5] the tip */
static void rotp(double px, double py, double cx, double cy, double c, double s, double *ox, double *oy) {   /* rotate_pt */
    double dx = px - cx, dy = py - cy, y = dy * c + dx * s; *ox = cx + (dx * c - dy * s); *oy = y + cy;
}
static void arrow_dist(Acc *A, const uint8_t *wl, const double L[6]) {
    double h = D(wl, 8), hw = D(wl, 0x10) * 0.5, hh = h * 0.5;
    double tx = L[3], ty = L[4], ex = tx - L[0], ey = ty - L[1], len = sqrt(ey * ey + ex * ex), dir = 0.0, w0[3], w1[3];
    if (!(len < TOL || len == TOL)) { dir = atan2(ey, ex); if (dir < 0.0) dir += TWOPI; }
    switch (wl[0x18]) {
    case 0: case 1: case 2: case 10: {                             /* the two sides from the tip */
        double a = atan(hw / h), r = h / cos(a);
        vw(A, tx, ty, w0); vw(A, tx - cos(a + dir) * r, ty - sin(a + dir) * r, w1); seg_world(A, w0, w1);
        vw(A, tx - cos(-a + dir) * r, ty - sin(-a + dir) * r, w1); seg_world(A, w0, w1);
        break; }
    case 4: case 5:                                                /* a circle of radius h / 2 about the tip (dist_to_arc, same view) */
        if (A->view == A->V->gview) arc_dist(A, tx, ty, hh, 0.0, TWOPI);
        break;
    case 8: {                                                      /* a slash through the tip */
        double d = dir;
        if (fabs(d) < TOL || fabs(d - 3.141592653589793) < TOL) d = 0.0;
        double s = sin(d), c = cos(d), ax, ay, bx, by;
        rotp(tx - hh, ty - hh, tx, ty, c, s, &ax, &ay); rotp(tx + hh, ty + hh, tx, ty, c, s, &bx, &by);
        vw(A, ax, ay, w0); vw(A, bx, by, w1); seg_world(A, w0, w1);
        break; }
    default: break;                                                /* 3 none; 6 / 7 / 9: NOT PORTED */
    }
}

/* FUN_18020b5c0 on one v8_wit_ldr (W: its bytes; DOC: the memory table) */
static void wl_dist(Acc *A, const uint8_t *W, const DpRec *r) {
    if (!W[0] || !W[3]) return;
    int kind = W[1], n = I16(W, 0x1a);
    double first[2][3]; int nf = 0;
    if (kind < 2) {
        double prev[3];
        for (int i = 0; i < n; i++) {
            double w[3];
            if (!W[0x19]) { if (i >= 4) break; vw(A, D(W, 0x20 + 0x18 * i), D(W, 0x28 + 0x18 * i), w); }
            else {                                                  /* aPoints: their world point, to the view plane at the depth */
                size_t mn; const uint8_t *m = draft_mtable(r->doc, I32(W, 0x20), &mn);
                if (!m || mn < (size_t)(i + 1) * 0x98) break;
                double v[3]; w2v(A->vm, (const double *)(const void *)(m + 0x98 * i + 8), v); vw(A, v[0], v[1], w);
            }
            if (i) seg_world(A, prev, w);
            if (nf < 2) memcpy(first[nf++], w, sizeof w);
            memcpy(prev, w, sizeof prev);
        }
        /* calc_arrow_vec, kind 1: the first segment at z 0 (Point2Plane), the point arrow-length from its start; view coordinates */
        if (kind == 1 && nf == 2) {
            double p0[3], p1[3]; w2v(A->vm, first[0], p0); w2v(A->vm, first[1], p1);
            double lx = p1[0] - p0[0], ly = p1[1] - p0[1], len = sqrt(lx * lx + ly * ly);
            if (TOL < len) { double t = D(W, 8) / len, L[6] = { p0[0] + lx * t, p0[1] + ly * t, 0, p0[0], p0[1], 0 }; arrow_dist(A, W, L); }
        }
    } else if (kind == 2 && A->view == A->V->gview)                /* the arc in its own view = the graphics view */
        arc_dist(A, D(W, 0x20), D(W, 0x28), D(W, 0x38), D(W, 0x40), D(W, 0x48));
}
static void chain_dist(Acc *A, const uint8_t *W, const DpRec *r) {   /* FUN_180208d20 */
    for (int guard = 0; W && guard < 200; guard++) {
        wl_dist(A, W, r);
        int32_t nx = I32(W, 0x84); size_t mn = 0;
        W = nx ? draft_mtable(r->doc, nx, &mn) : NULL;
        if (W && mn < 0x98) W = NULL;
    }
}

int dp_distance(const DpRec *r, const DraftFonts *F, const DpView *V, const double C[2], double lim, double *d) {
    Acc A; memset(&A, 0, sizeof A); A.V = V; A.C = C; A.lim = lim; A.best = 1e38;
    const uint8_t *E = r->E; uint32_t cls = (uint32_t)I32(E, 0);
    DraftOut o = { &A, text_poly };
    if (cls == 0x40 && r->n >= 0x810) {
        A.view = I16(E, 0x328) ? I16(E, 0x328) : 1;
        if (!mcam_std_view(A.view, A.vm)) return 0;
        A.depth = D(E, 0x320);
        if (E[0x5d8 + 0x5c] < 8) draft_render_text(r->doc, E, r->n, F, &o);   /* FUN_18020b380 (TrueType FUN_18020ae80: NOT PORTED) */
        static const int slot[4] = { 0x378, 0x410, 0x4a8, 0x540 };
        for (int k = 0; k < 4; k++) if (E[slot[k]]) chain_dist(&A, E + slot[k], r);
    } else if ((cls == 0x10 || cls == 0x20) && r->n >= 0x1c8) {
        A.view = I16(E, 0x88) ? I16(E, 0x88) : 1;
        if (!mcam_std_view(A.view, A.vm)) return 0;
        A.depth = D(E, 0x80);
        if (E[0x68] > 1 && cls == 0x10 && E[0x98 + 0x5c] < 8) draft_render_text(r->doc, E, r->n, F, &o);
        if (E[0x68] < 3 && E[0x100]) chain_dist(&A, E + 0x100, r);
    } else return 0;
    *d = A.best;
    return A.ok && A.best < lim;
}

int dp_pick(const DpRec *R, int nr, int mask, const DraftFonts *F, const DpView *V, const double C[2], double lim, double *dist) {
    int best = -1; double bd = 1e38;
    for (int i = 0; i < nr; i++) {
        const uint8_t *E = R[i].E; uint32_t cls = (uint32_t)I32(E, 0); int view;
        if (cls == 0x40) { if (!(mask & 0x40)) continue; view = I16(E, 0x328); }
        else if (cls == 0x10 || cls == 0x20) { if (!(mask & (0x10 | 0x20))) continue; view = I16(E, 0x88); }   /* ent_matches_mask by class */
        else continue;
        if (!V->iso && (view ? view : 1) != V->gview) continue;
        double d;
        if (dp_distance(&R[i], F, V, C, lim, &d) && d < bd && d < lim) { bd = d; best = i; }
    }
    if (dist) *dist = bd;
    return best;
}
