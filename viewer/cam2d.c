/*
 * cam2d.c - chaining + 2D toolpaths on Mastercam part geometry.
 *   Contour: chain offset by tool radius (+ stock) to the outside / inside (Clipper 6.2, vendor/clipper6), climb direction
 *            (outside clockwise, inside counter-clockwise), depth passes, retract between passes.
 *   Pocket:  region = even-odd union of the selected closed chains (inner chains become islands); contour-parallel
 *            offsets every stepover; each offset level is grouped into polygons (outer + island holes) and arranged
 *            as a containment tree; cut innermost first with a ramp entry along the loop, linking at depth when the
 *            straight link stays inside the cleared region, otherwise retract.
 *   Drill:   G81 (or G83 peck) at points / circle centres.
 *   G-code:  the user's post style (N100.., 3 decimals half-up, modal words, T M6 / G0 G90 G54 / M3 S / G43 H0 Z).
 */
#include "platform.h"
#include "cam2d.h"
#include "cam_fileop.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "offset.h"
#include "mcce_ctour.h"
#include "area_mill.h"
#include "mcsurf.h"
#define LATHE_STOCK_MCAM  /* lathe_op_stock (lathe_stock.h) */
#include "lathe_face.h"   /* Lathe Face (opcode 66, CAM_LFACE) */
#include "lathe_newop.h"  /* a new Lathe Face of the menu / ribbon (CamParams.lf_new) */

/* ------------------------------------------------------------------------------------------------ chaining */
static double d2(V3 a, V3 b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z); }

/* cam_chain_views (ours): the caller's chaining is for an operation, so arcs in another plane (Ent.vw, their normal not world Z)
   may be chained too - cam_generate maps those whose normal is the Tplane's Z (2D chaining here otherwise works on XY entities) */
int cam_chain_views;
/* cam_chain_splines (formats/spline_chain.md): the caller's chaining is for an Engrave operation, so spline curves (E_SPLINE, in a
   plane parallel to XY) are chainable too - the only engine here that takes them (cam_chain_polyline breaks them as
   chain_to_ctour_array does); every other chaining (other ops, Extrude, the wireframe chain functions) leaves them out */
int cam_chain_splines;
/* cam_chain_all (formats/chaining.md "Curves outside the C-plane, splines and NURBS (CS-7)"): Mastercam's chaining takes every line, arc,
   parametric spline and NURBS curve whatever its plane - run_chain_dlg sets the entity mask DAT_1813c4cbc = 0x8f for every caller
   (mccore_chain_range.c l.17719), build_hash_table hashes the ends of every entity of mask 0x8f (rewind_dl_db(.., 0x8f, ..),
   mccore_chain_feat_opts.c l.5362-5450: ent +0x68 / +0x80, 3D), FUN_1801e3de0 takes any hashed end within chain_tol (3D, or 2D with
   chain_ignore_depths) of the entity mask and only the C-plane filter (chain_plane_filter, FUN_1801fa6e0 below) limits the plane; the
   consumer decides afterwards. Set by the UI while chaining for an operation (core_ui cui_chain.c cui_all_chaining), whose generators
   refuse what they do not take (cam_generate); the other consumers here (Extrude, the wireframe chain functions) keep the old rule
   (planar lines / arcs; ours) and say so on a pick of such a curve. Arcs of a view (Ent.vw) are taken; an arc whose stored XY data does
   not match its end points (vw 0, planar 0: arc_fixups) is not (its curve is not known here). */
int cam_chain_all;
int cam_ent_chainable_any(const Ent *e) { return (e->type == E_LINE || (e->type == E_ARC && (e->planar || e->vw)) || e->type == E_SPLINE) && !e->gen && !e->blanked; }
static int chainable(const Ent *e) {
    if (cam_chain_all) return cam_ent_chainable_any(e);
    return (((e->type == E_LINE || e->type == E_ARC) && (e->planar || (cam_chain_views && e->type == E_ARC && e->vw))) || (cam_chain_splines && e->type == E_SPLINE && e->planar)) && !e->gen && !e->blanked;   /* gen: a solid chain's curve (not wireframe); blanked: hidden (Ent.blanked) */
}
int cam_ent_chainable(const Ent *e) { return chainable(e); }

/* ---- spline curves (E_SPLINE) ---- */
/* the tolerances chain_to_ctour_array's spline breaking reads: curve_min_step (config 1004), curve_max_step (1005), tol (1000) -
   the current configuration's (McSysTol, cam2d.h; defaults FUN_1803bc7d0 0.001 in / 0.02 mm, 100) */
static double spl_min_step(const McamFile *f) { return mc_systol_units(mcam_is_metric(f))->curve_min_step; }
static double spl_max_step(const McamFile *f) { return mc_systol_units(mcam_is_metric(f))->curve_max_step; }
static const CurveRec *ent_crv(const McamFile *f, const Ent *e) { return e->type == E_SPLINE && e->crv >= 0 && e->crv < f->ncrvs ? &f->crvs[e->crv] : NULL; }
/* FUN_1801fa6e0 (mccore_chain_feat_opts.c l.5743-5826): the entity parallel to the C-plane within tol. The C-plane here is Top (the active
   view GetActiveViewNo(0) = view 1, its row at +0x60 = the z axis (0, 0, 1)): *depth = the start's depth (ent +0x68 . z axis); a point is
   always parallel; else the end's depth (ent +0x80) must be within tol of it; an arc whose view is not the active one must have its z axis
   parallel to the active one's (| |n . n_view| - 1 | <= 2e-7); a spline / NURBS: the point at the middle parameter (curve_t1t2, eval_curve
   at (t1 + t2) / 2) within tol of the depth too. */
int cam_ent_cplane(const McamFile *f, const Ent *e, double tol, double *depth) {
    *depth = e->a.z;
    if (e->type == E_POINT) return 1;
    double d = fabs(e->a.z - e->b.z);
    if (tol < d) return 0;
    if (e->type == E_ARC && e->vw) { double c = fabs(fabs(e->vm[8]) - 1.0); return c < 2e-07 || c == 2e-07; }
    if (e->type == E_SPLINE) {
        const CurveRec *cr = ent_crv(f, e); double t1, t2, o[3][3];
        if (!cr) return 0;
        mcs_spline_t1t2(cr, &t1, &t2);
        if (mcs_spline_eval_d(cr, (t1 + t2) * 0.5, 0, o)) return 0;
        double m = fabs(*depth - o[0][2]);
        return m < tol || m == tol;
    }
    return 1;
}
/* the spline's break points at chord error tol (brk_spline_const_err from t1 to t2): parameters and points, *n of them (the stored
   start / end points first / last); malloc'd, 0 when the record cannot be evaluated */
static int spl_samples(const McamFile *f, const Ent *e, double tol, double **ts, double **xyz) {
    const CurveRec *c = ent_crv(f, e); *ts = NULL; *xyz = NULL;
    if (!c) return 0;
    double t1, t2, t, o[3][3]; mcs_spline_t1t2(c, &t1, &t2); t = t1;
    int cap = 64, n = 0; double *T = malloc(cap * sizeof *T), *P = malloc(cap * 3 * sizeof *P);
#define SPUSH(TT, X, Y, Z) do { if (n == cap) { cap *= 2; T = realloc(T, cap * sizeof *T); P = realloc(P, cap * 3 * sizeof *P); } T[n] = (TT); P[3 * n] = (X); P[3 * n + 1] = (Y); P[3 * n + 2] = (Z); n++; } while (0)
    SPUSH(t1, e->a.x, e->a.y, e->a.z);
    for (int it = 0; it < 1000000 && t <= t2 - 2e-7; it++) {
        double t0 = t; mcs_brk_spline_const_err(c, spl_min_step(f), spl_max_step(f), tol, 0, CAM_SYS_TOL, &t);
        if (!(t > t0) || t > t2 - 2e-7 || mcs_spline_eval_d(c, t, 0, o)) break;
        SPUSH(t, o[0][0], o[0][1], o[0][2]);
    }
    SPUSH(t2, e->b.x, e->b.y, e->b.z);
#undef SPUSH
    *ts = T; *xyz = P; return n;
}
int cam_spline_samples(const McamFile *f, const Ent *e, double tol, double **ts, double **xyz) { return spl_samples(f, e, tol, ts, xyz); }
/* the length of an entity for chain_next's short-entity rule (length_ent 0x180016e60): a spline by its break points at chain_tol
   (ours: length_ent's spline branch is NOT DECODED) */
static double ent_len(const McamFile *f, const Ent *e) {
    if (e->type == E_ARC) return fabs(e->sw) * e->r;
    if (e->type == E_SPLINE) {
        double *T, *P, L = 0; int n = spl_samples(f, e, cam_chain_tol(mcam_is_metric(f)), &T, &P);
        for (int i = 1; i < n; i++) L += sqrt((P[3 * i] - P[3 * i - 3]) * (P[3 * i] - P[3 * i - 3]) + (P[3 * i + 1] - P[3 * i - 2]) * (P[3 * i + 1] - P[3 * i - 2]) + (P[3 * i + 2] - P[3 * i - 1]) * (P[3 * i + 2] - P[3 * i - 1]));
        free(T); free(P); if (n) return L;
    }
    return sqrt(d2(e->a, e->b));
}

/* next entity at a joint: mccore FUN_1801e3de0 (decomp/mccore_chaining_next.c, formats/chaining.md). Candidates are the
   entity ends within tol of the tip on every entity but the current one, chained or not (each end counts); only
   unchained ones can be taken. 0 -> end; 1 -> take it if free; 2-3 -> the nearest free one, but a branch (stop) unless
   the other candidate is the same entity, or there are 2 and the current entity is shorter than tol; 4+ -> branch.
   Returns 1 with the entity and the end of it that touches the tip. */
int cam_chain_color_filter, cam_chain_level_filter;
/* chain_plane_filter (the Chaining dialog's C-plane / 3D radios 1482 / 1499, FUN_1801ec7e0 / FUN_1801ec560; default
   config 6021), chain_plane_tol (config 1002 "Planar chaining tolerance": McSysTol, cam2d.h),
   chain_ignore_depths (Options 2466, config 6003), chain_start_at_point (Options 2468, config 6009); the C-plane depth
   (constr_depth) for the plane filter */
int cam_chain_plane_filter, cam_chain_ignore_depths, cam_chain_start_pt;
double cam_chain_plane_z;
/* FUN_1801dddb0: the joint distance - with chain_ignore_depths in the C-plane (world_to_2d of the active plane; XY
   here, the Top plane), else 3D */
static double jd2(V3 a, V3 b) { return cam_chain_ignore_depths ? (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) : d2(a, b); }
/* FUN_1801f9e90 (called by chain_area, chain_all_selected, the window FUN_1801dfae0): with the plane filter on, entities
   not parallel to the C-plane (FUN_1801fa6e0) or not at its depth (|depth - constr_depth| >= chain_plane_tol) are
   taken out of the selection */
static int plane_ok(const McamFile *f, const Ent *e) { double d; return !cam_chain_plane_filter || (cam_ent_cplane(f, e, cam_chain_plane_tol, &d) && fabs(d - cam_chain_plane_z) < cam_chain_plane_tol); }
static _Thread_local int chain_brk;   /* set by chain_next when it stops at a branch point (FUN_1801e3de0 param_8), not a dead end */
static int chain_next(const McamFile *f, const char *used, const char *allow, int cur, V3 tip, double tol, int *found, int *tend) {
    chain_brk = 0;
    double t2 = tol * tol;
    int ci[4], cend[4], cfree[4], nc = 0; double cd[4];
    for (int i = 0; i < f->nents; i++) {
        if (i == cur || !chainable(&f->ents[i])) continue;
        /* chain_color_filter / chain_level_filter: only entities of the current entity's colour / level */
        if (cam_chain_color_filter && f->ents[i].color != f->ents[cur].color) continue;
        if (cam_chain_level_filter && f->ents[i].level != f->ents[cur].level) continue;
        /* plane filter (FUN_1801e3de0 l.8902-8914): the candidate parallel to the C-plane (FUN_1801fa6e0) at the tip's depth */
        if (cam_chain_plane_filter) { double dep; if (!cam_ent_cplane(f, &f->ents[i], cam_chain_plane_tol, &dep) || fabs(tip.z - dep) > cam_chain_plane_tol) continue; }
        for (int k = 0; k < 2; k++) {
            double dd = jd2(k ? f->ents[i].b : f->ents[i].a, tip);
            if (dd < t2) { if (nc < 4) { ci[nc] = i; cend[nc] = k; cfree[nc] = !used[i] && (!allow || allow[i]); cd[nc] = dd; } nc++; }
        }
    }
    int pick = 0;
    if (nc == 0) return 0;
    if (nc >= 4) { chain_brk = 1; return 0; }
    if (nc >= 2) {
        double best = 1e300; for (int c = 0; c < nc; c++) if (cfree[c] && cd[c] < best) { best = cd[c]; pick = c; }
        int dup = -1, other = -1;
        for (int c = 0; c < nc; c++) if (c != pick) { if (ci[c] == ci[pick]) dup = c; else other = c; }
        if (dup < 0) {
            const Ent *ce = &f->ents[cur];
            double len = ent_len(f, ce);
            if (nc != 2 || len >= tol) { chain_brk = 1; return 0; }   /* branch point */
        } else if (other >= 0) {                               /* the same entity at both ends vs another one */
            const Ent *de = &f->ents[ci[dup]], *oe = &f->ents[ci[other]];
            /* both FUN_1801e61d0 calls use the dup's end number (edx is left from the first call) */
            V3 pd = cend[dup] ? de->b : de->a, po = cend[dup] ? oe->b : oe->a;
            if (d2(tip, po) < d2(pd, po)) { chain_brk = 1; return 0; }
        }
    }
    if (!cfree[pick]) return 0;
    *found = ci[pick]; *tend = cend[pick];
    return 1;
}

/* allow: NULL, or the entities that may be taken (window chaining: add_chain mask 0x200 - FUN_1801e3de0 takes only
   entities with that selection bit; all entities still count as candidates at a joint). used: entities already taken by
   earlier chains (window chaining), or NULL */
static int chain_walk(const McamFile *f, int seed, double tol, const char *allow, char *used_io, Chain *out) {
    memset(out, 0, sizeof *out);
    if (seed < 0 || seed >= f->nents || !chainable(&f->ents[seed])) return 0;
    char *used = calloc(f->nents, 1);
    if (used_io) memcpy(used, used_io, f->nents);
    int cap = 64, *ent = malloc(cap * sizeof(int)), *rev = malloc(cap * sizeof(int)), n = 0;
    double t2 = tol * tol;
    /* forward from the seed's end point */
    ent[n] = seed; rev[n] = 0; n++; used[seed] = 1;
    V3 start = f->ents[seed].a, end = f->ents[seed].b;
    int closed = 0, branch = 0;   /* add_chain sets CHAIN +9 when a pass stops at a branch */
    for (int pass = 0; pass < 2; pass++) {
        for (;;) {
            V3 tip = pass == 0 ? end : start;
            if (pass == 0 && n > 1 && d2(end, start) < t2) { closed = 1; break; }
            int found, tend;
            if (!chain_next(f, used, allow, pass == 0 ? ent[n - 1] : ent[0], tip, tol, &found, &tend)) { if (chain_brk) branch = 1; break; }
            int frev = pass == 0 ? tend : !tend;
            if (n == cap) { cap *= 2; ent = realloc(ent, cap * sizeof(int)); rev = realloc(rev, cap * sizeof(int)); }
            used[found] = 1;
            const Ent *e = &f->ents[found];
            if (pass == 0) { ent[n] = found; rev[n] = frev; n++; end = frev ? e->a : e->b; }
            else {                                        /* prepend */
                memmove(ent + 1, ent, n * sizeof(int)); memmove(rev + 1, rev, n * sizeof(int));
                ent[0] = found; rev[0] = frev; n++; start = frev ? e->b : e->a;
            }
        }
        if (closed) break;
    }
    if (!closed && n > 1 && d2(end, start) < t2) closed = 1;
    if (n == 1 && f->ents[seed].type == E_ARC && fabs(fabs(f->ents[seed].sw) - 2 * M_PI) < 1e-6) closed = 1;   /* full circle */
    /* a single spline whose ends meet: add_chain's set_chain_closed(chain, chain_tol) (0x1801e8590, decomp/mccore_chain_range.c
       l.12395: chain start / end closer than the tolerance) - applied here to splines only (lines / arcs keep the rule above) */
    if (n == 1 && f->ents[seed].type == E_SPLINE && d2(f->ents[seed].a, f->ents[seed].b) < t2) closed = 1;
    if (used_io) memcpy(used_io, used, f->nents);
    free(used);
    out->ent = ent; out->rev = rev; out->n = n; out->closed = closed; out->branch = branch && !closed;
    return 1;
}
int cam_chain(const McamFile *f, int seed, double tol, Chain *out) { return chain_walk(f, seed, tol, NULL, NULL, out); }

static void chain_reverse(Chain *c) {
    { double t = c->cut0; c->cut0 = c->cut1; c->cut1 = t; }
    for (int i = 0, j = c->n - 1; i < j; i++, j--) { int te = c->ent[i]; c->ent[i] = c->ent[j]; c->ent[j] = te; int tr = c->rev[i]; c->rev[i] = c->rev[j]; c->rev[j] = tr; }
    for (int i = 0; i < c->n; i++) c->rev[i] = !c->rev[i];
}
static V3 chain_end_pt(const McamFile *f, const Chain *c, int end) {   /* chain_endpoint: 0 start, 1 end */
    const Ent *e = &f->ents[c->ent[end ? c->n - 1 : 0]];
    int r = c->rev[end ? c->n - 1 : 0];
    return end ? (r ? e->a : e->b) : (r ? e->b : e->a);
}

/* Window chaining (mccore FUN_1801dfae0, decomp in formats/chaining.md): every window-selected entity not yet used
   seeds add_chain(t = 0: from its start, taking only window-selected entities); closed chains then get the configured
   direction (FUN_1801e8660: config 6001 = 1 -> CCW, 0 -> CW; add_to_area sums (y1 + y2)(x2 - x1)); the open / closed
   masks (config 6012 / 6013) keep or drop chains. Ordering from the search point: the nearest chain start - for a
   closed chain any entity's start (FUN_1801e8230 makes it the first), for an open chain its start, or its end (then
   reversed); the next search starts at that chain's end with Zigzag (config 6000) on, else at its start.
   Returns the number of chains written to out (at most max). */
int cam_chain_window(const McamFile *f, const char *sel, double sx, double sy, double tol, int dir_ccw, int zigzag,
                     int keep_open, int keep_closed, Chain *out, int max) {
    char *used = calloc(f->nents, 1), *sel2 = malloc(f->nents ? f->nents : 1);
    for (int i = 0; i < f->nents; i++) sel2[i] = sel[i] && plane_ok(f, &f->ents[i]);   /* FUN_1801f9e90(0x200) */
    sel = sel2;
    int cap = 64, nc = 0; Chain *cs = malloc(cap * sizeof(Chain));
    for (int i = 0; i < f->nents; i++) {
        if (!sel[i] || used[i] || !chainable(&f->ents[i])) continue;
        Chain c; if (!chain_walk(f, i, tol, sel, used, &c)) continue;
        if ((c.closed && !keep_closed) || (!c.closed && !keep_open)) { cam_chain_free(&c); continue; }
        if (c.closed && c.n > 0) cam_chain_set_dir(f, &c, dir_ccw);   /* FUN_1801e8660 */
        if (cam_chain_start_pt) cam_chain_start_at_point(f, &c, tol);   /* add_chain: FUN_1801e8be0 after FUN_1801e8660 */
        if (nc == cap) { cap *= 2; cs = realloc(cs, cap * sizeof(Chain)); }
        cs[nc++] = c;
    }
    free(used); free(sel2);
    int nout = cam_chain_order(f, cs, nc, sx, sy, zigzag, out, max);
    free(cs);
    return nout;
}

/* order chains from a search point (FUN_1801dfae0 / FUN_1801df560 tail): the nearest chain start - closed chains any
   entity start (made the first, FUN_1801e8230), open chains their start or end (then reversed); the next search starts
   at that chain's end (Zigzag) or start. Takes ownership of cs[0..nc). */
int cam_chain_order(const McamFile *f, Chain *cs, int nc, double sx, double sy, int zigzag, Chain *out, int max) {
    int nout = 0; double px = sx, py = sy;
    char *done = calloc(nc ? nc : 1, 1);
    for (int step = 0; step < nc; step++) {
        int best = -1, bent = -1, brev = 0; double bd = 1e300;
        for (int k = 0; k < nc; k++) {
            if (done[k]) continue;
            Chain *c = &cs[k];
            /* with chain_start_at_point on, a chain is measured from its start only (no new start, no reversal:
               FUN_1801dfae0 l.5947-5973, FUN_1801df560 l.5732-5758) */
            if (c->closed && !cam_chain_start_pt) {
                for (int j = 0; j < c->n; j++) { const Ent *e = &f->ents[c->ent[j]]; V3 q = c->rev[j] ? e->b : e->a;
                    double d = hypot(q.x - px, q.y - py); if (d < bd) { bd = d; best = k; bent = j; brev = 0; } }
            } else {
                V3 q0 = chain_end_pt(f, c, 0), q1 = chain_end_pt(f, c, 1);
                double d0 = hypot(q0.x - px, q0.y - py), d1 = hypot(q1.x - px, q1.y - py);
                if (d0 < bd) { bd = d0; best = k; bent = -1; brev = 0; }
                if (d1 < bd && !c->closed && !cam_chain_start_pt) { bd = d1; best = k; bent = -1; brev = 1; }
            }
        }
        if (best < 0) break;
        Chain *c = &cs[best]; done[best] = 1;
        if (brev) chain_reverse(c);
        if (bent > 0) { for (int r = 0; r < bent; r++) cam_chain_move_start(c, 1); }
        V3 q = chain_end_pt(f, c, zigzag ? 1 : 0); px = q.x; py = q.y;
        if (nout < max) out[nout++] = *c; else cam_chain_free(c);
    }
    for (int k = 0; k < nc; k++) if (!done[k]) cam_chain_free(&cs[k]);
    free(done);
    return nout;
}

/* Vector chaining (mccore FUN_1801df560; help: chains from the entities a vector line crosses, extended both ways to
   branch points): each crossed entity not already in a chain (database order) is chained by add_chain with the pick
   parameter of the crossing (start at the nearer end by that parameter); closed chains to config 6001; masks 6012 /
   6013; then ordered from the first vector point. cross_xy: the crossing point (world XY) per entity. */
int cam_chain_vector(const McamFile *f, const char *crossed, const double *cross_xy, double sx, double sy, double tol,
                     int dir_ccw, int zigzag, int keep_open, int keep_closed, Chain *out, int max) {
    int cap = 64, nc = 0; Chain *cs = malloc(cap * sizeof(Chain));
    for (int i = 0; i < f->nents; i++) {
        if (!crossed[i] || !chainable(&f->ents[i])) continue;
        int inside = 0;
        for (int k = 0; k < nc && !inside; k++) for (int j = 0; j < cs[k].n; j++) if (cs[k].ent[j] == i) { inside = 1; break; }
        if (inside) continue;
        Chain c; if (!chain_walk(f, i, tol, NULL, NULL, &c)) continue;
        if ((c.closed && !keep_closed) || (!c.closed && !keep_open)) { cam_chain_free(&c); continue; }
        cam_chain_orient(f, &c, i, cross_xy[2 * i], cross_xy[2 * i + 1]);
        if (c.closed) cam_chain_set_dir(f, &c, dir_ccw);   /* FUN_1801e8660 */
        if (cam_chain_start_pt) cam_chain_start_at_point(f, &c, tol);   /* add_chain: FUN_1801e8be0 */
        if (nc == cap) { cap *= 2; cs = realloc(cs, cap * sizeof(Chain)); }
        cs[nc++] = c;
    }
    int nout = cam_chain_order(f, cs, nc, sx, sy, zigzag, out, max);
    free(cs);
    return nout;
}

/* Mastercam chaining of a picked entity (mccore add_chain @0x1801da9b0 / chain_single FUN_1801df0b0, decomp in
   formats/chaining.md): ep_n_from_t_sel(ent, t) picks the start end from the pick parameter t on the entity (lines,
   arcs, points: t >= 0.5 -> end 1, else end 0); the chain starts there, runs through the entity to its other end
   (FUN_1801e6b50(n) = !n) and on, then grows backward from the start - open and closed chains alike. A closed chain
   so starts at the picked entity. With config 6002 "Use cursor position" = n, FUN_1801e8660 then forces CW / CCW
   (config 6001): NOT PORTED (the user's config has 6002 = y). */
/* the pick in the view (select_ent_univ measures every curve in the view: proj_iso_pt of its points against the cursor, mastercam.dll
   decomp/mastercam_seluniv.c l.420-470): cam_chain_pick_view = 1 with the view's x / y rows (vm) and the cursor's view x / y (pk), set by the UI
   for a click. Used for the curves outside the C-plane (a line not parallel to XY, an arc of another view, a spline not parallel to XY);
   those in it keep the XY measure below. */
int cam_chain_pick_view; double cam_chain_pick_vm[6], cam_chain_pick_pk[2];
static void cpv(const double *p, double o[2]) { const double *m = cam_chain_pick_vm; o[0] = m[0] * p[0] + m[1] * p[1] + m[2] * p[2]; o[1] = m[3] * p[0] + m[4] * p[1] + m[5] * p[2]; }
/* the nearest point of a projected polyline (parameters T) to the cursor: its parameter (ours: dist_to_arc / dist_to_spline / dist_to_nurbs
   are NOT DECODED) */
static double pv_near(const double *P, const double *T, int n) {
    double bd = 1e300, bt = n ? T[0] : 0, a[2], b[2];
    for (int i = 1; i < n; i++) {
        cpv(P + 3 * i - 3, a); cpv(P + 3 * i, b);
        double ux = b[0] - a[0], uy = b[1] - a[1], l2 = ux * ux + uy * uy;
        double u = l2 > 0 ? ((cam_chain_pick_pk[0] - a[0]) * ux + (cam_chain_pick_pk[1] - a[1]) * uy) / l2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
        double d = hypot(a[0] + u * ux - cam_chain_pick_pk[0], a[1] + u * uy - cam_chain_pick_pk[1]);
        if (d < bd) { bd = d; bt = T[i - 1] + (T[i] - T[i - 1]) * u; }
    }
    return bt;
}
/* a curve outside the XY planes: a line whose ends differ in z (mcam_load's planar test; lines drawn here keep planar 1), an arc of a tilted
   view, a spline not parallel to XY (mcam_load spline_planar) */
int cam_ent_tilted(const Ent *e) { return (e->type == E_LINE && !(fabs(e->a.z - e->b.z) < 1e-6)) || (e->type == E_ARC && e->vw) || (e->type == E_SPLINE && !e->planar); }
static int ent_off_cplane(const Ent *e) { return cam_ent_tilted(e); }
static double ent_pick_t(const McamFile *f, const Ent *e, double px, double py) {   /* the pick's parameter on the entity, 0..1 */
    if (cam_chain_pick_view && ent_off_cplane(e)) {
        if (e->type == E_LINE) {   /* select_ent_univ case 2: proj_iso_pt of both ends, t_on_line (0x1804b48a0) of the cursor */
            double A[3] = { e->a.x, e->a.y, e->a.z }, B[3] = { e->b.x, e->b.y, e->b.z }, a[2], b[2]; cpv(A, a); cpv(B, b);
            double ux = b[0] - a[0], uy = b[1] - a[1], l2 = ux * ux + uy * uy;
            return l2 > 0 ? ((cam_chain_pick_pk[0] - a[0]) * ux + (cam_chain_pick_pk[1] - a[1]) * uy) / l2 : 0;
        }
        if (e->type == E_ARC) {    /* the fraction of the sweep (eval_curve t) of the nearest of 3-degree chords (ours) */
            int n = (int)(fabs(e->sw) * 180 / M_PI / 3) + 2; if (n > 720) n = 720;
            double *P = malloc((n + 1) * 3 * sizeof *P), *T = malloc((n + 1) * sizeof *T);
            for (int i = 0; i <= n; i++) { T[i] = (double)i / n; V3 q = mcam_arc_pt(e, e->a0 + e->sw * T[i]); P[3 * i] = q.x; P[3 * i + 1] = q.y; P[3 * i + 2] = q.z; }
            double t = pv_near(P, T, n + 1); free(P); free(T); return t;
        }
        double *T, *P; int n = spl_samples(f, e, cam_chain_tol(mcam_is_metric(f)), &T, &P);   /* a spline: its break points at chain_tol (ours) */
        double t1 = n ? T[0] : 0, t2 = n ? T[n - 1] : 1, bt = pv_near(P, T, n); free(T); free(P);
        return t2 > t1 ? (bt - t1) / (t2 - t1) : 0;
    }
    if (e->type == E_SPLINE) {
        /* ep_n_from_t_sel's spline rule (end 1 when |t - t2| <= |t - t1|, formats/chaining.md) = this fraction >= 0.5. The pick's
           parameter: the nearest point of the spline's break points at chain_tol, interpolated (ours: the pick projection onto a
           spline is NOT DECODED) */
        double *T, *P, bd = 1e300, bt = 0; int n = spl_samples(f, e, cam_chain_tol(mcam_is_metric(f)), &T, &P);
        for (int i = 1; i < n; i++) {
            double ux = P[3 * i] - P[3 * i - 3], uy = P[3 * i + 1] - P[3 * i - 2], l2 = ux * ux + uy * uy;
            double u = l2 > 0 ? ((px - P[3 * i - 3]) * ux + (py - P[3 * i - 2]) * uy) / l2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
            double d = hypot(P[3 * i - 3] + u * ux - px, P[3 * i - 2] + u * uy - py);
            if (d < bd) { bd = d; bt = T[i - 1] + (T[i] - T[i - 1]) * u; }
        }
        double t1 = n ? T[0] : 0, t2 = n ? T[n - 1] : 1; free(T); free(P);
        return t2 > t1 ? (bt - t1) / (t2 - t1) : 0;
    }
    if (e->type == E_LINE) {
        double ux = e->b.x - e->a.x, uy = e->b.y - e->a.y, l2 = ux * ux + uy * uy;
        return l2 > 0 ? ((px - e->a.x) * ux + (py - e->a.y) * uy) / l2 : 0;
    }
    if (e->type == E_ARC && e->vw) return 0;                         /* a view arc: not measured in XY (ours; its picks are refused, cui_chain.c) */
    if (e->type == E_ARC && e->sw != 0) {                              /* arcs: fraction of the sweep (eval_curve t) */
        double d = atan2(py - e->c.y, px - e->c.x) - e->a0, s = fabs(e->sw);
        if (e->sw < 0) d = -d;
        d = fmod(d, 2 * M_PI); if (d < 0) d += 2 * M_PI;
        if (d > s) d = d - s < 2 * M_PI - d ? s : 0;                   /* just past an end: that end */
        return d / s;
    }
    return 0;
}
void cam_chain_orient(const McamFile *f, Chain *c, int seed, double px, double py) {
    int k = -1; for (int i = 0; i < c->n; i++) if (c->ent[i] == seed) { k = i; break; }
    if (k < 0) return;
    const Ent *e = &f->ents[seed];
    int ep_n = ent_pick_t(f, e, px, py) >= 0.5;                          /* ep_n_from_t_sel: 1 = start at e->b */
    int forward = c->rev[k] == ep_n;                                   /* the chain already enters the seed at end ep_n */
    if (!forward) {                                                     /* reverse the chain */
        for (int i = 0, j = c->n - 1; i < j; i++, j--) { int te = c->ent[i]; c->ent[i] = c->ent[j]; c->ent[j] = te; int tr = c->rev[i]; c->rev[i] = c->rev[j]; c->rev[j] = tr; }
        for (int i = 0; i < c->n; i++) c->rev[i] = !c->rev[i];
        k = c->n - 1 - k;
    }
    if (c->closed && k > 0) {                                           /* start at the picked entity */
        int *te = malloc(sizeof(int) * c->n), *tr = malloc(sizeof(int) * c->n);
        for (int i = 0; i < c->n; i++) { te[i] = c->ent[(k + i) % c->n]; tr[i] = c->rev[(k + i) % c->n]; }
        memcpy(c->ent, te, sizeof(int) * c->n); memcpy(c->rev, tr, sizeof(int) * c->n); free(te); free(tr);
    }
}

/* FUN_1801e8660 (decomp/mccore_chain_range.c l.12442-12562): a closed chain turned to the configured direction (config 6001: 1 = CCW, 0 = CW).
   Each chain entity is sampled with eval_curve at n parameters from its start to its end as traversed (CHAIN_ENT +0x18 = 1: swapped) and
   each step of the samples in the active view (world_to_2d of GetActiveViewNo(0): Top here, x / y) goes into add_to_area (the sum of
   (y1 + y2)(x2 - x1): < 0 counter-clockwise): lines (and anything else) n = 2 over 0..1, arcs n = floor(|sweep| / (pi / 2)) + 2 when
   |sweep| >= pi / 2 (else 2) over 0..1 (the fraction of the sweep), splines / NURBS n = floor(|t2 - t1|) + 3 over curve_t1t2 (ent +0x98 /
   +0xa0); the parameter steps by (end - start) / (n - 1) added each time. Reversed when (area < 0) != (chain_direction == 1). Every entity
   kind and plane alike (the chain's closure is set_chain_closed's, here Chain.closed). */
void cam_chain_set_dir(const McamFile *f, Chain *c, int ccw) {
    if (!c->closed || c->n < 1) return;
    double area = 0;
    for (int k = 0; k < c->n; k++) {
        const Ent *e = &f->ents[c->ent[k]];
        const CurveRec *cr = e->type == E_SPLINE ? ent_crv(f, e) : NULL;
        if (e->type == E_SPLINE && !cr) continue;   /* get_ent_from_eptr failed: skipped */
        double s0 = 0, s1 = 1; int n = 2;
        if (e->type == E_ARC) { if (1.570796326794896 <= fabs(e->sw)) n = (int)floor(fabs(e->sw) / 1.570796326794896) + 2; }
        else if (cr) { mcs_spline_t1t2(cr, &s0, &s1); n = (int)floor(fabs(s1 - s0)) + 3; }
        if (c->rev[k]) { double t = s0; s0 = s1; s1 = t; }
        double d = s1 - s0, t = s0, px = 0, py = 0;
        if (n < 2) n = 2;
        for (int i = 0; i < n; i++) {
            double x, y;
            if (e->type == E_LINE) { x = e->a.x + (e->b.x - e->a.x) * t; y = e->a.y + (e->b.y - e->a.y) * t; }
            else if (e->type == E_ARC) { V3 q = mcam_arc_pt(e, e->a0 + e->sw * t); x = q.x; y = q.y; }
            else if (cr) { double o[3][3]; if (mcs_spline_eval_d(cr, t, 0, o)) { o[0][0] = e->a.x; o[0][1] = e->a.y; } x = o[0][0]; y = o[0][1]; }
            else { x = e->a.x; y = e->a.y; }
            if (i > 0) area += (py + y) * (x - px);   /* add_to_area */
            px = x; py = y;
            t = t + d / (double)(n - 1);
        }
    }
    if ((area < 0.0) != (ccw != 0)) chain_reverse(c);
}

/* polygon helpers for Area chaining */
static int poly_has_pt(const double *xy, int n, double x, double y) {
    int in = 0;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if (((xy[2 * i + 1] > y) != (xy[2 * j + 1] > y)) && x < (xy[2 * j] - xy[2 * i]) * (y - xy[2 * i + 1]) / (xy[2 * j + 1] - xy[2 * i + 1]) + xy[2 * i]) in = !in;
    return in;
}
static int poly_inside(const double *a, int na, const double *b, int nb) {   /* every vertex of a inside b */
    for (int i = 0; i < na; i++) if (!poly_has_pt(b, nb, a[2 * i], a[2 * i + 1])) return 0;
    return na > 0;
}
static int segs_cross(const double *p, const double *q, const double *r, const double *t) {
    double d1 = (t[0] - r[0]) * (p[1] - r[1]) - (t[1] - r[1]) * (p[0] - r[0]), d2 = (t[0] - r[0]) * (q[1] - r[1]) - (t[1] - r[1]) * (q[0] - r[0]);
    double d3 = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]), d4 = (q[0] - p[0]) * (t[1] - p[1]) - (q[1] - p[1]) * (t[0] - p[0]);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}
static int poly_touch(const double *a, int na, int ca, const double *b, int nb) {   /* a's segments cross closed b */
    for (int i = 0; i + 1 < na + (ca ? 1 : 0); i++) for (int j = 0; j < nb; j++)
        if (segs_cross(a + 2 * i, a + 2 * ((i + 1) % na), b + 2 * j, b + 2 * ((j + 1) % nb))) return 1;
    return 0;
}

/* Area chaining (mccore chain_area @0x1801dd160): every chainable entity is chained (add_chain t = 0, closed chains to
   config 6001; open chains dropped unless 6012); the boundary is the innermost closed chain containing the picked point;
   other chains stay if open and nested in it, or closed and nested in / intersecting it; with infinite nesting off (6004)
   only first-level islands stay (each chain's parent = the boundary, or a closed chain it is nested in). Chains keep
   the database order. No containing boundary -> 0 (FUN_1801dd8c0, the open-area case, not ported). */
int cam_chain_area(const McamFile *f, double px, double py, double tol, int dir_ccw, int keep_open, int infinite,
                   Chain *out, int max) {
    char *used = calloc(f->nents ? f->nents : 1, 1), *allow = NULL;
    if (cam_chain_plane_filter) { allow = malloc(f->nents ? f->nents : 1); for (int i = 0; i < f->nents; i++) allow[i] = plane_ok(f, &f->ents[i]); }   /* FUN_1801f9e90(0x200) */
    int cap = 64, nc = 0; Chain *cs = malloc(cap * sizeof(Chain)); double **pl = malloc(cap * sizeof(double *)); int *pn = malloc(cap * sizeof(int));
    for (int i = 0; i < f->nents; i++) {
        if (used[i] || !chainable(&f->ents[i]) || (allow && !allow[i])) continue;
        Chain c; if (!chain_walk(f, i, tol, allow, used, &c)) continue;
        if (!c.closed && !keep_open) { cam_chain_free(&c); continue; }
        if (cam_chain_start_pt) cam_chain_start_at_point(f, &c, tol);
        if (c.closed) cam_chain_set_dir(f, &c, dir_ccw);   /* FUN_1801e8660 */
        double *xy, z; int n = cam_chain_polyline(f, &c, tol, &xy, &z);
        if (nc == cap) { cap *= 2; cs = realloc(cs, cap * sizeof(Chain)); pl = realloc(pl, cap * sizeof(double *)); pn = realloc(pn, cap * sizeof(int)); }
        cs[nc] = c; pl[nc] = xy; pn[nc] = n; nc++;
    }
    free(used); free(allow);
    int outer = -1;
    for (int k = 0; k < nc; k++) if (cs[k].closed && poly_has_pt(pl[k], pn[k], px, py))
        if (outer < 0 || poly_inside(pl[k], pn[k], pl[outer], pn[outer])) outer = k;
    int nout = 0;
    if (outer >= 0) {
        int *keep = calloc(nc, sizeof(int)), *par = malloc(nc * sizeof(int));
        for (int k = 0; k < nc; k++) {
            if (k == outer) { keep[k] = 1; continue; }
            int nested = poly_inside(pl[k], pn[k], pl[outer], pn[outer]);
            keep[k] = cs[k].closed ? (nested || poly_touch(pl[k], pn[k], 1, pl[outer], pn[outer])) : nested;
        }
        if (!infinite) {
            for (int k = 0; k < nc; k++) par[k] = outer;
            for (int b = 0; b < nc; b++) if (keep[b] && b != outer && cs[b].closed)
                for (int a = 0; a < nc; a++) if (keep[a] && a != b && par[a] == outer && par[b] != a && poly_inside(pl[a], pn[a], pl[b], pn[b])) par[a] = b;
            for (int k = 0; k < nc; k++) if (k != outer && par[k] != outer) keep[k] = 0;
        }
        for (int k = 0; k < nc; k++) if (keep[k] && nout < max) { out[nout++] = cs[k]; cs[k].ent = NULL; }
        free(keep); free(par);
    }
    for (int k = 0; k < nc; k++) { if (cs[k].ent) cam_chain_free(&cs[k]); free(pl[k]); }
    free(cs); free(pl); free(pn);
    return nout;
}

/* Partial mode (mccore FUN_1801db3f0 states 1-2 -> FUN_1801e54a0 find_path_between_ents_normal): the first pick sets the
   first entity and its start end (ep_n_from_t_sel); from its far end the chain walks with the next-entity rule
   (FUN_1801e3de0) until it reaches the last picked entity, which joins at the end touching the tip. No path ->
   0 (mccoreres 4171 "Partial chain: path between first and last entity not found"). */
int cam_chain_partial(const McamFile *f, int e1, double px, double py, int e2, double tol, Chain *out) {
    memset(out, 0, sizeof *out);
    if (e1 < 0 || e1 >= f->nents || e2 < 0 || e2 >= f->nents || !chainable(&f->ents[e1]) || !chainable(&f->ents[e2])) return 0;
    char *used = calloc(f->nents, 1);
    int cap = 64, *ent = malloc(cap * sizeof(int)), *rev = malloc(cap * sizeof(int)), n = 0;
    int ep = ent_pick_t(f, &f->ents[e1], px, py) >= 0.5;                 /* start at end ep: rev = ep */
    ent[n] = e1; rev[n] = ep; n++; used[e1] = 1;
    V3 tip = ep ? f->ents[e1].a : f->ents[e1].b;
    int ok = e1 == e2;
    while (!ok) {
        int found, tend;
        if (!chain_next(f, used, NULL, ent[n - 1], tip, tol, &found, &tend)) break;
        if (n == cap) { cap *= 2; ent = realloc(ent, cap * sizeof(int)); rev = realloc(rev, cap * sizeof(int)); }
        used[found] = 1; ent[n] = found; rev[n] = tend; n++;
        tip = tend ? f->ents[found].a : f->ents[found].b;
        if (found == e2) ok = 1;
    }
    free(used);
    if (!ok) { free(ent); free(rev); return 0; }
    out->ent = ent; out->rev = rev; out->n = n; out->partial = 1;
    V3 s0 = rev[0] ? f->ents[ent[0]].b : f->ents[ent[0]].a;
    out->closed = n > 1 && d2(s0, tip) < tol * tol;
    return 1;
}

/* Start Back / Start Forward (mccore FUN_1801e6980 / FUN_1801e69f0, closed chains): the last entity becomes the first /
   the first becomes the last; directions unchanged */
void cam_chain_move_start(Chain *c, int forward) {
    if (!c->closed || c->n < 2) return;
    c->cut0 = 0;                                                     /* FUN_1801e6980: +0x28 cleared */
    if (forward) { int e = c->ent[0], r = c->rev[0]; memmove(c->ent, c->ent + 1, (c->n - 1) * sizeof(int)); memmove(c->rev, c->rev + 1, (c->n - 1) * sizeof(int)); c->ent[c->n - 1] = e; c->rev[c->n - 1] = r; }
    else { int e = c->ent[c->n - 1], r = c->rev[c->n - 1]; memmove(c->ent + 1, c->ent, (c->n - 1) * sizeof(int)); memmove(c->rev + 1, c->rev, (c->n - 1) * sizeof(int)); c->ent[0] = e; c->rev[0] = r; }
}

static V3 ch_tip(const McamFile *f, const Chain *c, int end) {
    int i = end ? c->n - 1 : 0; const Ent *e = &f->ents[c->ent[i]];
    return end ? (c->rev[i] ? e->a : e->b) : (c->rev[i] ? e->b : e->a);
}
/* Continue a chain stopped at a branch (mccore FUN_1801e3850, prompt 4269 "Branch point reached. Select next branch."):
   the picked entity has to touch the chain end (else 4175 "The selected sub-chain does not touch the branch point");
   it is added, then the walk goes on (FUN_1801e3de0) to the next dead end or branch. 0 = does not touch. */
int cam_chain_extend(const McamFile *f, Chain *c, int pick, double tol) {
    if (pick < 0 || pick >= f->nents || !chainable(&f->ents[pick]) || c->n < 1) return 0;
    for (int i = 0; i < c->n; i++) if (c->ent[i] == pick) return 0;
    V3 tip = ch_tip(f, c, 1); double t2 = tol * tol; const Ent *pe = &f->ents[pick];
    int tend = d2(pe->a, tip) < t2 ? 0 : d2(pe->b, tip) < t2 ? 1 : -1;
    if (tend < 0) return 0;
    char *used = calloc(f->nents, 1); for (int i = 0; i < c->n; i++) used[c->ent[i]] = 1;
    int cap = c->n + 64; c->ent = realloc(c->ent, cap * sizeof(int)); c->rev = realloc(c->rev, cap * sizeof(int));
    int found = pick; c->branch = 0;
    for (;;) {
        if (c->n == cap) { cap *= 2; c->ent = realloc(c->ent, cap * sizeof(int)); c->rev = realloc(c->rev, cap * sizeof(int)); }
        used[found] = 1; c->ent[c->n] = found; c->rev[c->n] = tend; c->n++;
        tip = ch_tip(f, c, 1);
        if (d2(tip, ch_tip(f, c, 0)) < t2) { c->closed = 1; break; }
        if (!chain_next(f, used, NULL, found, tip, tol, &found, &tend)) { c->branch = chain_brk; break; }
    }
    free(used);
    return 1;
}
/* Partial with Wait (chain_wait_at_end, FUN_1801db3f0 state 3): a further pick extends the partial chain from its end along
   the next-entity rule up to the picked entity (FUN_1801e54a0 from the last curve); 0 = no path (4171). */
int cam_chain_partial_more(const McamFile *f, Chain *c, int pick, double tol) {
    if (pick < 0 || pick >= f->nents || !chainable(&f->ents[pick]) || c->n < 1) return 0;
    for (int i = 0; i < c->n; i++) if (c->ent[i] == pick) return 0;
    char *used = calloc(f->nents, 1); for (int i = 0; i < c->n; i++) used[c->ent[i]] = 1;
    int n0 = c->n, cap = c->n + 64, ok = 0;
    c->ent = realloc(c->ent, cap * sizeof(int)); c->rev = realloc(c->rev, cap * sizeof(int));
    V3 tip = ch_tip(f, c, 1); int cur = c->ent[c->n - 1];
    for (;;) {
        int found, tend;
        if (!chain_next(f, used, NULL, cur, tip, tol, &found, &tend)) break;
        if (c->n == cap) { cap *= 2; c->ent = realloc(c->ent, cap * sizeof(int)); c->rev = realloc(c->rev, cap * sizeof(int)); }
        used[found] = 1; c->ent[c->n] = found; c->rev[c->n] = tend; c->n++; cur = found;
        tip = tend ? f->ents[found].a : f->ents[found].b;
        if (found == pick) { ok = 1; break; }
    }
    free(used);
    if (!ok) c->n = n0;
    return ok;
}

/* Dynamic (mccore FUN_1801ec820 -> FUN_1801e25e0 start / FUN_1801e28e0 end / FUN_1801e2be0 closed; help
   Dynamic_move_start_end): the start or end moves to the picked position on one of the chain's entities, not limited to
   endpoints (the cut is kept on the chain, CHAIN +0x28 / +0x30; Break entities in Dynamic, config 6007, is not done).
   Open chains drop the entities before the new start / after the new end; a closed chain is rotated to start on that
   entity. 0 = the entity is not in the chain. */
int cam_chain_dynamic(const McamFile *f, Chain *c, int at_end, int pick, double px, double py) {
    int k = -1; for (int i = 0; i < c->n; i++) if (c->ent[i] == pick) { k = i; break; }
    if (k < 0) return 0;
    if (f->ents[pick].type == E_SPLINE) return 0;                   /* a start / end inside a spline: NOT PORTED (the cut needs
                                                                       chain_to_ctour_array's param_10 / param_11 path, FUN_180284b40) */
    double t = ent_pick_t(f, &f->ents[pick], px, py); if (t < 0) t = 0; if (t > 1) t = 1;
    double u = c->rev[k] ? 1 - t : t;                                /* along the chain's direction */
    if (c->closed) {
        if (at_end) return 0;
        for (int r = 0; r < k; r++) { int e = c->ent[0], rv = c->rev[0]; memmove(c->ent, c->ent + 1, (c->n - 1) * sizeof(int)); memmove(c->rev, c->rev + 1, (c->n - 1) * sizeof(int)); c->ent[c->n - 1] = e; c->rev[c->n - 1] = rv; }
        c->cut0 = u; c->cut1 = 0; return 1;
    }
    if (!at_end) {
        if (k == c->n - 1 && u >= 1 - c->cut1) return 0;
        memmove(c->ent, c->ent + k, (c->n - k) * sizeof(int)); memmove(c->rev, c->rev + k, (c->n - k) * sizeof(int)); c->n -= k;
        c->cut0 = u;
    } else {
        if (k == 0 && 1 - u <= 0) return 0;
        if (k == 0 && u <= c->cut0) return 0;
        c->n = k + 1; c->cut1 = 1 - u;
    }
    return 1;
}

/* Start / End Back / Forward on open (partial or branch) chains (mccore FUN_1801e7740 / FUN_1801e7890 / FUN_1801e7500 /
   FUN_1801e75d0): start back = one more entity before the start (next-entity rule from the start), start forward =
   drop the first entity, end back = drop the last entity, end forward = one more entity after the end. */
int cam_chain_end_move(const McamFile *f, Chain *c, int at_end, int forward, double tol) {
    if (c->n < 1) return 0;
    if (at_end) c->cut1 = 0; else c->cut0 = 0;                      /* FUN_1801e75d0 / FUN_1801e7740 need +0x30 / +0x28 = 0 */
    int grow = at_end ? forward : !forward;
    if (!grow) {
        if (c->n < 2) return 0;
        if (!at_end) { memmove(c->ent, c->ent + 1, (c->n - 1) * sizeof(int)); memmove(c->rev, c->rev + 1, (c->n - 1) * sizeof(int)); }
        c->n--; c->closed = 0; return 1;
    }
    char *used = calloc(f->nents, 1); for (int i = 0; i < c->n; i++) used[c->ent[i]] = 1;
    int found, tend, ok = chain_next(f, used, NULL, at_end ? c->ent[c->n - 1] : c->ent[0], ch_tip(f, c, at_end), tol, &found, &tend);
    free(used);
    if (!ok) return 0;
    c->ent = realloc(c->ent, (c->n + 1) * sizeof(int)); c->rev = realloc(c->rev, (c->n + 1) * sizeof(int));
    if (at_end) { c->ent[c->n] = found; c->rev[c->n] = tend; }
    else { memmove(c->ent + 1, c->ent, c->n * sizeof(int)); memmove(c->rev + 1, c->rev, c->n * sizeof(int)); c->ent[0] = found; c->rev[0] = !tend; }
    c->n++;
    return 1;
}

/* Single mode (mccore chain_single FUN_1801df0b0): the picked entity alone; set_chain_closed(chain_tol) closes it when its
   ends meet (a full circle) */
int cam_chain_single(const McamFile *f, int seed, double tol, Chain *out) {
    memset(out, 0, sizeof *out);
    if (seed < 0 || seed >= f->nents || !chainable(&f->ents[seed])) return 0;
    out->ent = malloc(sizeof(int)); out->rev = malloc(sizeof(int)); out->ent[0] = seed; out->rev[0] = 0; out->n = 1;
    out->closed = d2(f->ents[seed].a, f->ents[seed].b) < tol * tol;
    return 1;
}

void cam_chain_free(Chain *c) { free(c->ent); free(c->rev); memset(c, 0, sizeof *c); }

/* Point mode (mccore chain_append_point @0x1801dcc10): a chain of one point entity (CHAIN_ENT type 4 via
   add_curve_to_chain); picking the point that already is the last chain does nothing (the loop over the chains at its
   start: last chain, first entity == the pick, chain_is_point). Points given as a position are stored as a new point
   entity first (store_ent with the main attributes) - done by the caller. */
int cam_chain_point(const McamFile *f, int e, Chain *out) {
    memset(out, 0, sizeof *out);
    if (e < 0 || e >= f->nents || f->ents[e].type != E_POINT) return 0;
    out->ent = malloc(sizeof(int)); out->rev = malloc(sizeof(int)); out->ent[0] = e; out->rev[0] = 0; out->n = 1;
    return 1;
}
int cam_chain_is_point(const McamFile *f, const Chain *c) { return c->n == 1 && f->ents[c->ent[0]].type == E_POINT; }

/* FUN_1801e5180: a point entity at p within tol (FUN_1801dddb0 distance), colour / level masks against the curve ce,
   plane filter: the point at p's depth within chain_plane_tol */
static int point_at(const McamFile *f, const Ent *ce, V3 p, double tol) {
    for (int i = 0; i < f->nents; i++) {
        const Ent *pe = &f->ents[i];
        if (pe->type != E_POINT) continue;
        if (cam_chain_color_filter && pe->color != ce->color) continue;
        if (cam_chain_level_filter && pe->level != ce->level) continue;
        if (cam_chain_plane_filter && fabs(p.z - pe->a.z) > cam_chain_plane_tol) continue;
        if (jd2(pe->a, p) < tol * tol) return 1;
    }
    return 0;
}
/* Start chain at point (Options 2468, config 6009; mccore FUN_1801e8be0, run by add_chain after the direction):
   open chain - a point entity at its end reverses it (the start is checked first and only keeps its point);
   closed chain - the first entity (in chain order) whose start has a point entity becomes the first. Point chains
   are left alone. */
void cam_chain_start_at_point(const McamFile *f, Chain *c, double tol) {
    if (c->n < 1 || cam_chain_is_point(f, c)) return;
    if (!c->closed) {
        if (point_at(f, &f->ents[c->ent[c->n - 1]], chain_end_pt(f, c, 1), tol)) chain_reverse(c);
        return;
    }
    for (int j = 0; j < c->n; j++) {
        const Ent *e = &f->ents[c->ent[j]];
        if (!point_at(f, e, c->rev[j] ? e->b : e->a, tol)) continue;
        for (int r = 0; r < j; r++) cam_chain_move_start(c, 1);
        return;
    }
}

/* nesting of chains as in mccore sort_inside_to_outside @0x1801e98f0 / sort_outside_to_inside @0x1801e9be0 /
   FUN_1801e83c0 (Reverse inner chains): each chain as a polygon (chain_to_pock_bound at 2 / screen scale - ptol here);
   a's parent = b when a != b, a is not b's parent, not both points, the boxes overlap, is_ctour_nested(a, b)
   (mccore 0x1805e49e0: b not a point, a and b do not intersect, every vertex of a inside / on b) and a has no parent
   yet or b is nested in a's parent (the innermost one wins); level = length of the parent chain */
static int nest_in(const double *a, int na, const double *b, int nb) {
    if (nb < 2 || poly_touch(a, na, na > 1, b, nb)) return 0;
    return poly_inside(a, na, b, nb);
}
static void nest_levels(const McamFile *f, const Chain *cs, int n, double ptol, int *level) {
    double **pl = malloc((n ? n : 1) * sizeof(double *)); int *pn = malloc((n ? n : 1) * sizeof(int)), *par = malloc((n ? n : 1) * sizeof(int));
    double (*bb)[4] = malloc((n ? n : 1) * sizeof *bb);
    for (int k = 0; k < n; k++) {
        double z; pn[k] = cam_chain_polyline(f, &cs[k], ptol, &pl[k], &z); par[k] = -1;
        bb[k][0] = bb[k][1] = 1e300; bb[k][2] = bb[k][3] = -1e300;
        for (int i = 0; i < pn[k]; i++) { double x = pl[k][2 * i], y = pl[k][2 * i + 1];
            if (x < bb[k][0]) bb[k][0] = x; if (y < bb[k][1]) bb[k][1] = y; if (x > bb[k][2]) bb[k][2] = x; if (y > bb[k][3]) bb[k][3] = y; }
    }
    for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) {
        if (a == b || par[a] == b || par[b] == a) continue;
        if (cam_chain_is_point(f, &cs[a]) && cam_chain_is_point(f, &cs[b])) continue;
        if (bb[a][2] < bb[b][0] || bb[b][2] < bb[a][0] || bb[a][3] < bb[b][1] || bb[b][3] < bb[a][1]) continue;   /* disjoint_pock_bound_bbox */
        if (!nest_in(pl[a], pn[a], pl[b], pn[b])) continue;
        if (par[a] >= 0 && !nest_in(pl[b], pn[b], pl[par[a]], pn[par[a]])) continue;
        par[a] = b;
    }
    for (int k = 0; k < n; k++) { int l = 0; for (int q = par[k]; q >= 0 && l <= n; q = par[q]) l++; level[k] = l; }
    for (int k = 0; k < n; k++) free(pl[k]);
    free(pl); free(pn); free(par); free(bb);
}
/* Reverse inner chains (Options 2536, config 6005; FUN_1801e83c0 at the end of run_chain_dlg): chains at an odd
   nesting level are reversed */
void cam_chain_reverse_inner(const McamFile *f, Chain *cs, int n, double ptol) {
    int *lv = malloc((n ? n : 1) * sizeof(int)); nest_levels(f, cs, n, ptol, lv);
    for (int k = 0; k < n; k++) if (lv[k] & 1) chain_reverse(&cs[k]);
    free(lv);
}
/* Nested chains Sorting (Options combo 2901, config 6006 nested_chain_sort_mode; run_chain_dlg end): 1 / 3 =
   sort_inside_to_outside (repeatedly the first chain of the highest level), 2 / 4 = sort_outside_to_inside (level 0, 1,
   ... in list order). Every chain must be closed, else nothing changes and -1 is returned (post_warning 4221).
   Modes 3 / 4 then run optimize_inner_nested_order / optimize_outer_nested_order (0x1801e6b60 / 0x1801e70c0): NOT
   PORTED - those modes sort only. */
int cam_chain_sort_nested(const McamFile *f, Chain *cs, int n, int mode, double ptol) {
    if (mode < 1 || mode > 4 || n < 1) return 0;
    for (int k = 0; k < n; k++) if (!cs[k].closed) return -1;
    int *lv = malloc(n * sizeof(int)); nest_levels(f, cs, n, ptol, lv);
    Chain *o = malloc(n * sizeof(Chain)); char *done = calloc(n, 1); int no = 0;
    if (mode == 1 || mode == 3) {
        while (no < n) { int best = -1; for (int k = 0; k < n; k++) if (!done[k] && (best < 0 || lv[k] > lv[best])) best = k;
            done[best] = 1; o[no++] = cs[best]; }
    } else {
        for (int l = 0; no < n && l <= n; l++) for (int k = 0; k < n; k++) if (!done[k] && lv[k] == l) { done[k] = 1; o[no++] = cs[k]; }
    }
    memcpy(cs, o, n * sizeof(Chain)); free(o); free(done); free(lv);
    return 0;
}

/* a chain that is not flat in world Z (Mastercam's check_chains_3d_view -> ctour_3d: the contour then follows the chain's Z; ours: the
   test is entity end / centre heights against the first within tol, and an arc in a tilted view) */
int cam_chain_3d(const McamFile *f, const Chain *c) {
    if (!c->n) return 0;
    double z0 = f->ents[c->ent[0]].a.z, t = CAM_SYS_TOL;
    for (int i = 0; i < c->n; i++) {
        const Ent *e = &f->ents[c->ent[i]];
        if (e->type == E_ARC && e->vw >= 2) return 1;
        if (e->type == E_SPLINE && !e->planar) return 1;   /* check_chains_3d_view: a curve sampled off the first Z */
        if (fabs(e->a.z - z0) > t || fabs(e->b.z - z0) > t || (e->type == E_ARC && fabs(e->c.z - z0) > t)) return 1;
    }
    return 0;
}
int cam_chain_polyline(const McamFile *f, const Chain *c, double tol, double **xy, double *z) {
    int cap = 256, n = 0;
    double *p = malloc(cap * 2 * sizeof(double));
#define PUSH(X, Y) do { if (n == cap) { cap *= 2; p = realloc(p, cap * 2 * sizeof(double)); } \
                        if (!n || fabs(p[2 * n - 2] - (X)) > 1e-9 || fabs(p[2 * n - 1] - (Y)) > 1e-9) { p[2 * n] = (X); p[2 * n + 1] = (Y); n++; } } while (0)
    *z = c->n ? f->ents[c->ent[0]].a.z : 0;
    /* Dynamic start / end (CHAIN +0x28 / +0x30): cut0 = part of the first entity (as traversed) left out at the start,
       cut1 = part of the last entity left out at the end; a closed chain with cut0 ends with the first entity's left-out
       part, back at the start point */
    int extra = c->closed && c->cut0 > 1e-12 && c->n > 0;
    for (int ii = 0; ii < c->n + extra; ii++) {
        int i = ii < c->n ? ii : 0;
        const Ent *e = &f->ents[c->ent[i]];
        int r = c->rev[i];
        double u0 = ii == 0 ? c->cut0 : 0, u1 = ii == c->n - 1 ? 1 - c->cut1 : 1;
        if (ii == c->n) { u0 = 0; u1 = c->cut0; }
        if (e->type == E_POINT) { PUSH(e->a.x, e->a.y); continue; }   /* a point chain (Point mode, chain_append_point) */
        if (e->type == E_SPLINE) {
            /* chain_to_ctour_array_break (mccore 0x180283120) -> FUN_180282bd0 (decomp/mccore_spline_brk.c; formats/spline_chain.md):
               the chain's first record is the spline's stored start (its end when traversed backwards); then from that end's
               parameter brk_spline_const_err steps to the next break parameter (chord error tol); a point at least `tol` (config 1000)
               from the last point taken counts, and is recorded when it differs from the last record by more than tol in x or y;
               until the parameter passes the far end less 2e-7; the last record then becomes the stored far end point. Lines,
               no arcs. Dynamic cuts inside a spline: refused by cam_generate. */
            const CurveRec *cr = ent_crv(f, e);
            V3 s0 = r ? e->b : e->a, s1 = r ? e->a : e->b;
            if (!n) PUSH(s0.x, s0.y);
            if (cr) {
                double t1, t2, o[3][3], st = CAM_SYS_TOL; mcs_spline_t1t2(cr, &t1, &t2);
                double t = r ? t2 : t1, Pp[3];
                mcs_spline_eval_d(cr, t, 0, o); memcpy(Pp, o[0], sizeof Pp);
                for (int it = 0; it < 1000000; it++) {
                    mcs_brk_spline_const_err(cr, spl_min_step(f), spl_max_step(f), tol, r, st, &t);
                    mcs_spline_eval_d(cr, t, 0, o);
                    const double *P = o[0];
                    if (st <= sqrt((P[1] - Pp[1]) * (P[1] - Pp[1]) + (P[0] - Pp[0]) * (P[0] - Pp[0]) + (P[2] - Pp[2]) * (P[2] - Pp[2]))) {
                        if (fabs(P[0] - p[2 * n - 2]) > st || fabs(P[1] - p[2 * n - 1]) > st) PUSH(P[0], P[1]);
                        memcpy(Pp, P, sizeof Pp);
                    }
                    if (r ? !(t1 + 2e-7 < t || t1 + 2e-7 == t) : !(t < t2 - 2e-7 || t2 - 2e-7 == t)) break;
                }
            }
            p[2 * n - 2] = s1.x; p[2 * n - 1] = s1.y;                     /* the last record: the stored end point */
            continue;
        }
        if (e->type == E_LINE) {
            V3 a = r ? e->b : e->a, b = r ? e->a : e->b;
            PUSH(a.x + (b.x - a.x) * u0, a.y + (b.y - a.y) * u0); PUSH(a.x + (b.x - a.x) * u1, a.y + (b.y - a.y) * u1);
        } else {
            double step = e->r > tol ? 2 * acos(1 - tol / e->r) : M_PI / 8;
            if (step > M_PI / 18) step = M_PI / 18;
            int k = (int)ceil(fabs(e->sw) * (u1 - u0) / step); if (k < 2) k = 2;
            for (int j = 0; j <= k; j++) {
                double tt = u0 + (u1 - u0) * j / k, t = r ? 1.0 - tt : tt;
                double a = e->a0 + e->sw * t;
                PUSH(e->c.x + e->r * cos(a), e->c.y + e->r * sin(a));
            }
        }
    }
    if (c->closed && n > 1 && fabs(p[0] - p[2 * n - 2]) < 1e-6 && fabs(p[1] - p[2 * n - 1]) < 1e-6) n--;   /* drop duplicate close */
#undef PUSH
    *xy = p;
    return n;
}

/* ------------------------------------------------------------------------------------------------ helpers */
typedef struct { Move *m; int n, cap; } MB;
/* Smoothing (cam_smooth.c, formats/contour_smoothing.md) - what the point distribution's collector FUN_180945d50 needs of the NCI records
   that these moves stand for, recorded while gen_contour runs (g_ps_mb = its move list), without changing the moves:
   - the moves a contour marker precedes: bnci_wr_ctour_marker (-96) is written before every pass writer FUN_180056be0 (decomp/
     mcmill_ctour.c l.1750-1752 / 2343-2345) and the collector flushes its list at such a record (LAB_1809461c4); ps_mark before a pass,
     the marker goes before the pass's first move (mv: the pass may first take back the last pass's retract); g_ps_map: 1 = recorded
     (gen_contour's 2D passes), 0 = not mapped;
   - the arcs written here as untagged chords whose centre is known (the lead arcs, lead_in / lead_out): Mastercam writes them as arc
     records (the lead pieces of FUN_180051990), which the distribution keeps as arcs;
   - g_ps_arc: the toolpath has arcs that are chords here with no record of them (chain arcs, rolled corners, corner rounding, a lead
     arc rebuilt in a scratch list) */
typedef struct { int k0, k1; double cx, cy; int cw; } PsArc;
static _Thread_local MB *g_ps_mb;
static _Thread_local int *g_ps_mk, g_ps_nmk, g_ps_cmk, g_ps_map, g_ps_arc, g_ps_pend;
static _Thread_local PsArc *g_ps_arcs; static _Thread_local int g_ps_narc, g_ps_carc;
static void ps_mark(void) { if (g_ps_mb) g_ps_pend = 1; }
static void ps_mark_at(int at) {
    if (g_ps_nmk == g_ps_cmk) { g_ps_cmk = g_ps_cmk ? g_ps_cmk * 2 : 64; g_ps_mk = realloc(g_ps_mk, g_ps_cmk * sizeof *g_ps_mk); }
    g_ps_mk[g_ps_nmk++] = at;
}
static void ps_arc_rec(const MB *b, int k0, int k1, double cx, double cy, int cw) {   /* the moves [k0, k1] = one arc from the move before k0 */
    if (!g_ps_mb) return;
    if (b != g_ps_mb) { g_ps_arc = 1; return; }               /* a scratch list: the arc's moves are copied / changed later */
    if (k1 < k0) return;
    while (g_ps_narc > 0 && g_ps_arcs[g_ps_narc - 1].k1 >= k0) g_ps_narc--;   /* moves taken back since */
    if (g_ps_narc == g_ps_carc) { g_ps_carc = g_ps_carc ? g_ps_carc * 2 : 64; g_ps_arcs = realloc(g_ps_arcs, g_ps_carc * sizeof *g_ps_arcs); }
    g_ps_arcs[g_ps_narc++] = (PsArc){ k0, k1, cx, cy, cw };
}
static _Thread_local MB *g_sof_mb; static _Thread_local int g_sof_v;   /* a section-start block pending for the next move of g_sof_mb (Move.sof = g_sof_v) */
static void mv(MB *b, int rapid, double x, double y, double z, double feed) {
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 1024; b->m = realloc(b->m, b->cap * sizeof(Move)); }
    Move *m = &b->m[b->n++]; m->rapid = rapid; m->p.x = x; m->p.y = y; m->p.z = z; m->feed = feed; m->arc = 0; m->cw = 0; m->cx = m->cy = 0; m->cc = 0; m->apl = 0; m->cz = 0; m->ctf = 0; m->sof = 0;
    if (g_ps_pend && b == g_ps_mb) { g_ps_pend = 0; ps_mark_at(b->n - 1); }   /* Smoothing: the pending contour marker */
    /* (a rapid straight up at the last point still belongs to the retract: clear_move / retract_move write only at the current XY,
       decomp/mccore_moves.c l.8 / l.206 - the block follows them, before plunge_move's records) */
    if (g_sof_mb == b && !(rapid && b->n >= 2 && fabs(x - b->m[b->n - 2].p.x) < 1e-9 && fabs(y - b->m[b->n - 2].p.y) < 1e-9 && z > b->m[b->n - 2].p.z)) { m->sof = g_sof_v; g_sof_mb = NULL; }
}
static V3 lastp(const MB *b) { return b->m[b->n - 1].p; }
/* tag the moves [from, b->n) as one true arc of the engine (centre, direction) so the post writes it as a G2 / G3 */
static _Thread_local int g_arc_seq;
static void mb_tag_arc(MB *b, int from, double cx, double cy, int cw) {
    int id = ++g_arc_seq; if (g_arc_seq > 1000000000) g_arc_seq = 0;
    for (int i = from; i < b->n; i++) { b->m[i].arc = id; b->m[i].cx = cx; b->m[i].cy = cy; b->m[i].cw = cw; }
}

static int zlevels(const CamParams *p, double *z, int max) {
    double depth = p->top_z - p->final_z;
    if (depth <= 1e-9) { z[0] = p->final_z; return 1; }
    int n = (int)ceil(depth / (p->stepdown > 1e-6 ? p->stepdown : depth) - 1e-9);
    if (n > max) n = max;
    for (int i = 1; i <= n; i++) z[i - 1] = p->top_z - depth * i / n;
    return n;
}

/* 2D High Speed depth cuts (op 134; ../formats/dynamic.md "Depth cuts"). CHST2DDepthCuts in mcmill:
   - count: FUN_1802ca5f0 -> FUN_1802ca490 (decomp/mcmill_hst2d.c l.15538, decomp/mcmill_all.c l.486414), the same
     arithmetic as mccore calc_depth_cuts 0x18089e520 (decomp/mccore_depths.c): with depth cuts on (record 0x868),
     dz = (T - zf) - nf fs; T - zf < tol or dz < tol: nr = 0; else nr = (short)(dz / max) and the rough step is max
     when |dz / max - nr| <= tol, else nr + 1 cuts of dz / (nr + 1); total = nr + nf, 0 -> nr = total = 1.
     Off: one cut (nr = 1, rough 0). max = 0 (BRACKET4 ops 14 / 16): dz / 0 = inf, cvttsd2si -> 0x80000000 -> short 0,
     |inf - 0| > tol -> nr = 1, rough = dz.
   - level i (1-based, top first): FUN_1802ca590 (decomp/mcmill_hst2d.c l.15192): i > nr: z = zf + (total - i) fs,
     else z = zf + (nr - i) rough + max(0, total - nr) fs.
   max = record 0x870 (SetParams p21 -> process +0xb0), nf = S16 0x878 (+0xb8), fs = 0x880 (+0xc0), mm l.475366 /
   CHST2DProcess::SetOperationID decomp/mcmill_hst2d.c l.9200; here stepdown / mc_depth_nf / mc_depth_fs (cam_fileop.c),
   on = mc_depth_on or a max rough step (a new op: the step the dialog sets, as mcam_saveops writes 0x868).
   tol = tol_exref (config 1000, CAM_SYS_TOL). *nrough / *rough: for the depth copies (FUN_1802bb260). */
static int hst_depths(const CamParams *p, double *z, int max, int *nrough, double *rough) {
    const double tol = CAM_SYS_TOL;
    double T = p->top_z, zf = p->final_z, fs = p->mc_depth_fs, st = 0;
    int on = p->mc_depth_on || p->stepdown > 1e-6, nf = p->mc_depth_nf > 0 ? p->mc_depth_nf : 0, nr = 1, total = 1;
    if (on) {
        double dz = (T - zf) - nf * fs;
        if (T - zf < tol || dz < tol) { nr = 0; st = 0; }
        else if (p->stepdown <= 0) { nr = 1; st = dz; }
        else {
            double q = dz / p->stepdown;
            nr = q > 32000 ? 32000 : (int)q;
            if (fabs(q - nr) <= tol) st = p->stepdown; else { nr++; st = dz / nr; }
        }
        total = nr + nf;
        if (total == 0) { nr = 1; total = 1; }
    } else { nr = 1; st = 0; nf = 0; fs = 0; }
    if (total > max) total = max;
    for (int i = 1; i <= total; i++) {
        int sr, sf;
        if (nr < i) { sr = 0; sf = total - i; } else { sr = nr - i; sf = total - nr; if (sf < 0) sf = 0; }
        z[i - 1] = zf + sr * st + sf * fs;
    }
    if (nrough) *nrough = nr;
    if (rough) *rough = st;
    return total;
}

static void reverse_path(CPath *c) {
    for (int i = 0, j = c->n - 1; i < j; i++, j--) {
        double tx = c->xy[2 * i], ty = c->xy[2 * i + 1];
        c->xy[2 * i] = c->xy[2 * j]; c->xy[2 * i + 1] = c->xy[2 * j + 1]; c->xy[2 * j] = tx; c->xy[2 * j + 1] = ty;
    }
}
static void rotate_to(CPath *c, double x, double y) {       /* start the loop at the point nearest (x, y) (a new vertex if needed) */
    int best = 0; double bd = 1e300, bx = 0, by = 0;
    for (int i = 0; i < c->n; i++) {
        double ax = c->xy[2 * i], ay = c->xy[2 * i + 1], ex = c->xy[2 * ((i + 1) % c->n)] - ax, ey = c->xy[2 * ((i + 1) % c->n) + 1] - ay;
        double l2 = ex * ex + ey * ey, t = l2 > 0 ? ((x - ax) * ex + (y - ay) * ey) / l2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
        double px = ax + ex * t, py = ay + ey * t, d = hypot(px - x, py - y);
        if (d < bd - 1e-12) { bd = d; best = i; bx = px; by = py; }
    }
    int n = c->n, split = hypot(bx - c->xy[2 * best], by - c->xy[2 * best + 1]) > 1e-6;
    if (split && hypot(bx - c->xy[2 * ((best + 1) % n)], by - c->xy[2 * ((best + 1) % n) + 1]) <= 1e-6) { best = (best + 1) % n; split = 0; }
    double *t = malloc((n + 1) * 2 * sizeof(double)); int m = 0;
    if (split) { t[0] = bx; t[1] = by; m = 1; for (int i = 1; i <= n; i++) { int k = (best + i) % n; t[2 * m] = c->xy[2 * k]; t[2 * m + 1] = c->xy[2 * k + 1]; m++; } }
    else for (int i = 0; i < n; i++) { int k = (best + i) % n; t[2 * m] = c->xy[2 * k]; t[2 * m + 1] = c->xy[2 * k + 1]; m++; }
    free(c->xy); c->xy = t; c->n = m;
}
static int pip(const CPath *c, double x, double y) {       /* point in polygon (even-odd) */
    int in = 0;
    for (int i = 0, j = c->n - 1; i < c->n; j = i++) {
        double xi = c->xy[2 * i], yi = c->xy[2 * i + 1], xj = c->xy[2 * j], yj = c->xy[2 * j + 1];
        if (((yi > y) != (yj > y)) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}
static int in_region(const CPaths *r, double x, double y) { int in = 0; for (int i = 0; i < r->n; i++) in ^= pip(&r->p[i], x, y); return in; }
static int seg_inside(const CPaths *r, double x0, double y0, double x1, double y1) {
    double L = hypot(x1 - x0, y1 - y0); int k = (int)(L / 0.05) + 2;
    for (int i = 0; i <= k; i++) { double t = (double)i / k; if (!in_region(r, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t)) return 0; }
    return 1;
}
static void cut_loop(MB *b, const CPath *c, double z, double feed) {
    for (int i = 1; i <= c->n; i++) mv(b, 0, c->xy[2 * (i % c->n)], c->xy[2 * (i % c->n) + 1], z, feed);
}
static void ramp_loop(MB *b, const CPath *c, double z0, double z1, double feed, double ramp_deg) {
    /* descend from z0 to z1 while following the loop, then one full lap at z1 */
    double per = 0;
    for (int i = 0; i < c->n; i++) per += hypot(c->xy[2 * ((i + 1) % c->n)] - c->xy[2 * i], c->xy[2 * ((i + 1) % c->n) + 1] - c->xy[2 * i + 1]);
    double need = (z0 - z1) / tan((ramp_deg > 0.5 ? ramp_deg : 0.5) * M_PI / 180), done = 0;
    if (per < 1e-9) { mv(b, 0, c->xy[0], c->xy[1], z1, feed); return; }
    int i = 0;
    while (done < need && i < 100000) {
        int a = i % c->n, k = (i + 1) % c->n;
        done += hypot(c->xy[2 * k] - c->xy[2 * a], c->xy[2 * k + 1] - c->xy[2 * a + 1]);
        double z = z0 - (z0 - z1) * fmin(1, done / need);
        mv(b, 0, c->xy[2 * k], c->xy[2 * k + 1], z, feed);
        i++;
    }
    /* finish at the loop start, then a full lap at depth */
    int at = i % c->n;
    for (int j = at; j != 0; j = (j + 1) % c->n) mv(b, 0, c->xy[2 * ((j + 1) % c->n)], c->xy[2 * ((j + 1) % c->n) + 1], z1, feed);
    cut_loop(b, c, z1, feed);
}

/* ------------------------------------------------------------------------------------------------ operations */
void cam_defaults_base(CamParams *p, int type, const McamFile *f) {
    memset(p, 0, sizeof *p);
    p->type = type;
    p->wofs = p->wwofs = p->cwofs = -1;                  /* op_view +0x92: Plane::Init -1 (cui_planes.c) */
    p->tool_num = f->ntools ? f->tools[0].num : 1;
    p->tool_dia = f->ntools && f->tools[0].dia > 0 ? f->tools[0].dia : 6;
    p->rpm = 8000; p->feed = 600; p->plunge_feed = 200;
    p->top_z = 0; p->final_z = -3; p->stepdown = 1; p->clearance = 25; p->feed_plane = 2;
    p->ht_inc = 4;                                       /* the feed plane incremental (no record: cui_opdlg.c fp_inc = !drill) */
    p->side = SIDE_OUTSIDE; p->stock_xy = 0; p->stepover_pct = 40; p->ramp_deg = 3; p->retract_z = -1e30;
    p->r_plane = 2; p->peck = 0; p->tol = 0.005;
    p->lead_r = 0; p->lead_len = 0; p->lead_mid = 1; p->finish_n = 1; p->finish_step = 0.25;   /* leads: the default op's (none without it) */
    p->tabs = 0; p->tab_w = 4; p->tab_h = 1; p->arcs = 1; p->cleanup = 1;
    p->tab_ramp = 1; p->tab_ang = M_PI / 4;           /* ours for ops without a record: the 45 deg ramps cut_path made before batch 5 */
    p->tool_type = 10;
    p->chamfer_w = 0.5; p->chamfer_tip = 0.5;
    p->helix_pitch = 1; p->thread_internal = 1; p->thread_rh = 1; p->thread_passes = 1; p->thread_full = 1; p->thread_up = 1;
    p->face_style = 0; p->face_overlap = 50; p->face_along = 50; p->roll = 1;
    p->min_rad_pct = 10; p->micro_lift = 0.25; p->back_feed = 0; p->entry_r = 0; p->wall_stock = 3; p->slot_width = 0;
    if (type == CAM_FACE) { p->stepover_pct = 75; p->finish_n = 0; }
    if (type == CAM_DYNAMIC || type == CAM_AREA || type == CAM_DYNCONTOUR || type == CAM_PEEL) { p->stepover_pct = 25; p->finish_n = 0; p->ramp_deg = 3; p->stepdown = 1000; }
    if (type == CAM_AREA || type == CAM_DYNCONTOUR) p->dyn_outside = 1;
    if (type == CAM_SLOT) { p->stepover_pct = 50; p->ramp_deg = 3; }
    if (type == CAM_HELIX || type == CAM_CIRCLE || type == CAM_THREAD) { p->finish_n = type == CAM_THREAD ? 0 : 1; p->finish_step = 0.1; }
    if (f->ntools) cam_params_from_tool(p, &f->tools[0]);
}
/* contour_type 2 "Engrave" (ours, not a Mastercam Contour type: record 0x1308 holds 0 2D / 1 3D / 2 2D chamfer / 3 3D chamfer / 4 Ramp /
   5 Remachining / 6 Oscillate; formats/audit_2026-10-26_coreui.md 1.7) was a 2D contour on the chain without leads. regen-consistency
   2026-11-19: it is that contour in the record's own terms - Compensation Off (0x5d0 = 4 -> SIDE_ON) and Lead In/Out off (0x8f0 = 0) -
   which the save writes and a regeneration reads; cam_cli --engrave and older projects (the sidecar's P contour_type 2) come here */
void cam_contour_engrave_compat(CamParams *p) {
    p->contour_type = 0; p->side = SIDE_ON; p->side_rel = 0; p->lead_on = 0;
}
/* a new operation: the built-in base, then Mastercam's default operation of this type (cam_fileop.c) */
void cam_defaults(CamParams *p, int type, const McamFile *f) {
    cam_defaults_base(p, type, f);
    cam_defaults_from_file(p, type, mcam_is_metric(f), f->ntools ? &f->tools[0] : NULL);
}

static const char *TYPE_LABELS[CAM_NTYPES] = { "", "Contour", "Pocket", "Drill", "Face", "Slot Mill", "Helix Bore", "Circle Mill",
                                               "Thread Mill", "Dynamic Mill", "Area Mill", "Dynamic Contour", "Peel Mill",
                                               "OptiRough",     /* the 3D Mill Toolpaths gallery item (viewer/ui/layout.txt GITEM OptiRough) */
                                               "Surface Rough Pocket", "Surface Rough Parallel",
                                               "Waterline", "Surface Finish Parallel", "Raster", "Area Roughing", "Spiral", "Radial", "Scallop", "Surface Finish Contour", "Project", "Surface Rough Project",
                                               "Engrave",
                                               "Pencil",
                                               "Surface Finish Blend",
                                               "Multisurface Rough Pocket",
                                               "Surface Rough Plunge",
                                               "Surface Finish Radial",
                                               "Surface Finish Project",
                                               "Horizontal Area",
                                               "Surface Finish Pencil",
                                               "Lathe Face" };   /* CAM_LFACE: opcode 66 (mcam_opcode_name) */   /* CAM_SFPENCIL: classic op 39 (mccoreres 4360; surf_classic3.md 10) */   /* CAM_SFPROJECT: op 13 (mccoreres 4338) */   /* CAM_SFRADIAL: op 12 (mccoreres 4337) */   /* CAM_SRPLUNGE: op 44 (mccoreres 4365; surf_classic2.md 2) */   /* CAM_SRMPOCKET: op 131 (mccoreres 4432), op 10's engine (surf_classic2.md 1) */   /* CAM_BLEND: classic op 109 (mcmsurfres 142 "Finish blend parameters"; formats/blend.md) */   /* CAM_PENCIL: the 3D gallery item (HST op 132 style 6, formats/pencil.md) */   /* engraveres: the gallery / menu 3628 item (opcode 416) */   /* mccoreres 4635 (the HST name block, Raster = 4619) */   /* op 11: mccoreres op description (classic finish parallel) */   /* the 3D gallery item (HST op 132 style 2) */   /* srough: the op names
                                               mccoreres 4335 / 4330 (Mastercam's operation description for op 10 / 5) */
const char *cam_type_label(int t) { return t > 0 && t < CAM_NTYPES ? TYPE_LABELS[t] : "Op"; }
int cam_type_from_label(const char *s) {
    for (int t = 1; t < CAM_NTYPES; t++) if (!strcasecmp(s, TYPE_LABELS[t])) return t;
    if (!strcasecmp(s, "Chamfer")) return CAM_CONTOUR;
    /* "Engrave" (gallery / menu 3628) is CAM_ENGRAVE (the TYPE_LABELS row): Mastercam's engrave.dll module (opcode 416, the EStamp7
       engine ported in engrave.c), not a Contour; stored contours of type 2 still load as contours */
    return 0;
}
/* srough: a ribbon gallery item -> the type it makes here. The 2D galleries' items are the type labels (as before). The 3D galleries
   ("3D Mill / Router / Lathe Milling / Mill-Turn Milling Toolpaths Gallery", formats/ui_ribbon.json) name their items "Pocket",
   "Parallel", "Contour", "Project" ... which are not the 2D types of the same name: their Roughing group is OptiRough, Pocket,
   Project, Parallel, Plunge, Multisurface Pocket, Area Roughing (ui_ribbon.json 3D gallery groups "Roughing" 7 / "Finishing" 12;
   the descriptions "Remove a lot of stock quickly by creating a series of planar cuts (or constant Z)" = Rough Pocket,
   msurf_pocket_toolbar -> msurf_assoc(10) decomp/mcmsurf_all.c l.212258-212260, and "Remove stock quickly by using constant Z
   depth cuts at a specified angle in the tool plane" = Rough Parallel, msurf_rgh_parallel_toolbar -> msurf_assoc(5) l.212718-212720;
   formats/ui_commands.tsv "Rough Pocket" / "Rough Parallel"). Only the ported ones are live; every other 3D item is greyed (before
   srough the 3D "Pocket" / "Contour" items started the 2D types by their names - a bug). occurrence = 1 for the first item of
   that name in the gallery (the 3D "Project" repeats: rough 1, finish 2). */
int cam_type_from_gallery(const char *gallery, const char *item, int occurrence) {
    (void)occurrence;
    /* the Multiaxis galleries' items are the multiaxis toolpaths (viewer/ui/cmd_labels.tsv: Drill5axEntry "Drill", Circle5axEntry
       "Circle Mill" ...; formats/ui_commands.tsv "Multi-Axis Toolpaths", MCUI5AX.DLL; opcodes 28 Drill 5 Axis / 113 Circle 5 Axis),
       not the 2D types of the same name: none is ported (before 2026-11-19 Drill and Circle Mill started the 2D ops - a bug,
       formats/new_toolpath_audit.md S1) */
    if (gallery && strstr(gallery, "Multiaxis")) return 0;
    if (gallery && !strncmp(gallery, "3D ", 3)) {
        if (!strcasecmp(item, "OptiRough")) return CAM_OPTIROUGH;
        int mr = strstr(gallery, "3D Mill Toolpaths") || strstr(gallery, "3D Router Toolpaths");   /* the Mill and Router galleries only
                   (ours: the Lathe / Mill-Turn milling galleries' classic surface items stay greyed - not tried on those machine groups) */
        if (mr && !strcasecmp(item, "Pocket")) return CAM_SRPOCKET;
        if (mr && !strcasecmp(item, "Parallel")) return CAM_SRPARALLEL;
        if (mr && !strcasecmp(item, "Contour")) return CAM_SFCONTOUR;   /* ribbon XML l.10356: msurf_contour_toolbar -> msurf_assoc(0xf) (formats/surf_classic2.md) */
        if (!strcasecmp(item, "Waterline")) return CAM_WATERLINE;
        if (!strcasecmp(item, "Raster")) return CAM_RASTER;
        if (!strcasecmp(item, "Area Roughing")) return CAM_AREAROUGH;
        if (!strcasecmp(item, "Spiral")) return CAM_SPIRAL;
        if (!strcasecmp(item, "Radial")) return CAM_RADIAL;
        if (!strcasecmp(item, "Scallop")) return CAM_SCALLOP;
        if (!strcasecmp(item, "Pencil")) return CAM_PENCIL;
        if (!strcasecmp(item, "Horizontal Area")) return CAM_HORIZAREA;   /* msurf_hmm_horizontal_area_toolbar (ms l.211756-211762): op 132, style 4 */
        if (mr && !strcasecmp(item, "Blend")) return CAM_BLEND;
        if (mr && !strcasecmp(item, "Multisurface Pocket")) return CAM_SRMPOCKET;   /* op 131 (surf_classic2.md 1.7) */
        if (mr && !strcasecmp(item, "Plunge")) return CAM_SRPLUNGE;   /* op 44 (surf_classic2.md 2.9) */   /* msurf_blend_toolbar -> msurf_assoc(0x6d) (surf_classic3.md 0.1) */   /* HST Pencil (style 6); the classic Pencil (op 39) is a menu item of its own */
        if (mr && !strcasecmp(item, "Project") && occurrence == 1) return CAM_SRPROJECT;   /* the Roughing group's Project: msurf_rgh_project_toolbar (surf_classic3.md 0.1) */
        if (!strcasecmp(item, "Project") && occurrence == 2) return CAM_HSTPROJECT;   /* the Finishing group's Project (the Roughing one is the classic op 7) */
        return 0;
    }
    return cam_type_from_label(item);
}
int cam_type_uses_holes(int t) { return t == CAM_DRILL || t == CAM_HELIX || t == CAM_CIRCLE || t == CAM_THREAD; }

void cam_params_from_tool(CamParams *p, const Tool *t) {
    p->tool_num = t->num; p->tool_dia = t->dia; p->tool_type = t->type;
    p->corner_r = t->corner_r; p->tip_angle = t->tip_angle; p->coolant = t->coolant;
    if (t->rpm > 0) p->rpm = t->rpm;
    if (t->feed > 0) p->feed = t->feed;
    if (t->plunge > 0) p->plunge_feed = t->plunge;
    if (p->type == CAM_DRILL) {
        p->drill_cycle = t->cycle;                     /* the tool's TlHolemakingOpParams.CannedCycleType / Peck1 */
        p->peck = (t->cycle == 1 || t->cycle == 2) && t->peck1 > 0 ? t->peck1 : 0;
        if (t->plunge > 0 && t->feed > 0) p->plunge_feed = fmin(t->feed, t->plunge);   /* drill feed is the cutting feed */
    }
    /* leads follow the tool as % of its diameter: CToolProp::Recalc (mcmill 0x180244700, decomp/mcmill_all.c
       l.395670-395678: with leads on (op +0x888 = record 0x8f0) and op +0x8d8 (record 0x940), line +0x8e0 (0x948) =
       +0x910 (0x978) x 0.01 x D and arc +0x8f0 (0x958) = +0x918 (0x980) x 0.01 x D); bnci_ctour repeats it at generation
       (l.47474-47480) */
    cam_lead_recalc(p);
    p->flute_len = t->flute_len;
    if (t->pitch > 0) p->thread_pitch = t->pitch;
    if (t->type == 12 || t->type == 9 || t->type == 2 || t->type == 1) p->tip_dia = t->tip_dia;   /* pointed tools: chamfer geometry
       (the contour type stays the op's record 0x1308: no source switches it with the tool) */
}

/* the leads' absolute values from their % of D: bnci_ctour (decomp/mcmill_ctour.c l.394-406) with the leads on (op +0x888 =
   record 0x8f0): entry (op +0x890 = 0x8f8 on) line / arc = % 0x930 / 0x938 x 0.01 x D, exit (op +0x8d8 = 0x940 on) 0x978 /
   0x980; a side that is off keeps its values (lead_pct: the values came from a record, cam_fileop.c LEADS) */
void cam_lead_recalc(CamParams *p) {
    if (!p->lead_pct) return;
    if (p->lead_in_on) { p->lead_len = p->lead_len_pct * 0.01 * p->tool_dia; p->lead_r = p->lead_r_pct * 0.01 * p->tool_dia; }
    if (p->lead_out_on) { p->lead_xlen = p->lead_xlen_pct * 0.01 * p->tool_dia; p->lead_xr = p->lead_xr_pct * 0.01 * p->tool_dia; }
}

/* mccore tool_tip_length 0x1800a5440 (dis 0x1800a54bc..0x1800a5501; product bits & 6 taken as 0, as elsewhere): D > tol and
   angle > tol (tol 5e-5 = 0x1812b5b58): tan((90 - angle x 0.5) x pi / 180) x D x 0.5, else 0 */
double cam_tool_tip_length(double dia, double ang) {
    if (!(dia > CAM_SYS_TOL) || !(ang > CAM_SYS_TOL)) return 0;   /* tol (config 1000, McSysTol) */
    return tan((90 - ang * 0.5) * 0.017453292519943295) * dia * 0.5;
}
/* mccore Cnc::Tool::is_mill_tool 0x1800daa80 (decomp/mccore_toolfilt.c): type < 0x1a and bit set in 0x3affc00; is_drill_tool =
   not a mill tool */
static int cam_is_drill_tool(int t) { return !((unsigned)t < 0x1a && ((0x3affc00u >> (t & 0x1f)) & 1)); }
/* the hole bottom: bnci_drill_depth 0x1800694d0 (decomp/mcmill_all.c l.70421-70436): with tip comp (op +0x1200 = record 0x1268)
   the depth goes down by the breakthrough amount (op +0x11f8 = 0x1260) and, for a drill tool other than types 6-8 (reamer, boring
   bar, counterbore), by tool_tip_length(D, tip angle op +0x300). Batch 8: the breakthrough and the tool test (before: the tip
   length only, for every tool) - every user drill op has tip comp off */
double cam_drill_z(const CamParams *p) {
    if (!p->tip_comp) return p->final_z;
    double t = p->drill_bt;
    if (cam_is_drill_tool(p->tool_type) && (unsigned)(p->tool_type - 6) >= 3) t += cam_tool_tip_length(p->tool_dia, p->tip_angle);
    return p->final_z - t;
}

/* closed_only: 1 machining chains (closed), 2 avoidance chains (closed), 0 all */
static CPaths chain_paths(const McamFile *f, CamOp *op, int closed_only, double *ztop) {
    CPaths ps; ps.n = 0; ps.p = calloc(op->nchains ? op->nchains : 1, sizeof(CPath));
    *ztop = 0;
    for (int i = 0; i < op->nchains; i++) {
        if (closed_only && !op->chains[i].closed) continue;
        if (closed_only && (closed_only == 2) != (op->chains[i].avoid != 0)) continue;
        double *xy, z; int n = cam_chain_polyline(f, &op->chains[i], op->p.tol, &xy, &z);
        if (n < 2) { free(xy); continue; }
        ps.p[ps.n].xy = xy; ps.p[ps.n].n = n; ps.n++; *ztop = z;
    }
    return ps;
}

/* Mastercam "Roll cutter around corners" (record 0x5d3 = op +0x56b, contour page 2938 control 1335; CamParams.roll
 * comment for the record offset this program reads): 0 none, 1 sharp = only corners of 135 deg or less (turn >= 45 deg;
 * NAKTO op 5 rolls its 45.000 deg turns and leaves 44.99 deg ones sharp), 2 all */
static int rolls(int roll, double turn_rad) { return roll == 2 || (roll == 1 && fabs(turn_rad) >= M_PI / 4 - 1e-7); }

/* one-sided offset of an open polyline (left of its direction for d > 0): outside corners rolled (arc) or mitred per
 * `roll`, inside corners mitred */
static CPath offset_open(const double *xy, int n, double d, int roll) {
    CPath o; int cap = n * 4 + 16; o.n = 0; o.xy = malloc(cap * 2 * sizeof(double));
#define OP(X, Y) do { if (o.n == cap) { cap *= 2; o.xy = realloc(o.xy, cap * 2 * sizeof(double)); } o.xy[2 * o.n] = (X); o.xy[2 * o.n + 1] = (Y); o.n++; } while (0)
    for (int i = 0; i + 1 < n; i++) {
        double tx = xy[2 * i + 2] - xy[2 * i], ty = xy[2 * i + 3] - xy[2 * i + 1], L = hypot(tx, ty);
        if (L < 1e-12) continue;
        tx /= L; ty /= L;
        double nx = -ty * d, ny = tx * d;                     /* left normal scaled by d (negative d = right) */
        double ax = xy[2 * i] + nx, ay = xy[2 * i + 1] + ny, bx = xy[2 * i + 2] + nx, by = xy[2 * i + 3] + ny;
        if (o.n) {
            double px = o.xy[2 * o.n - 2], py = o.xy[2 * o.n - 1];
            double ptx = 0, pty = 0;                          /* previous direction */
            for (int k = i - 1; k >= 0; k--) { double qx = xy[2 * k + 2] - xy[2 * k], qy = xy[2 * k + 3] - xy[2 * k + 1], q = hypot(qx, qy); if (q > 1e-12) { ptx = qx / q; pty = qy / q; break; } }
            double turn = ptx * ty - pty * tx;                /* > 0 left turn */
            int reversal = fabs(turn) < 1e-6 && ptx * tx + pty * ty < 0;   /* path doubles back: both sides are outside */
            double tang = atan2(turn, ptx * tx + pty * ty);
            if (((turn < 0) == (d > 0) && fabs(turn) > 1e-9 && rolls(roll, tang)) || reversal) { /* outside corner: arc around the vertex */
                double cx = xy[2 * i], cy = xy[2 * i + 1];
                double a0 = atan2(py - cy, px - cx), a1 = atan2(ay - cy, ax - cx), sw = a1 - a0;
                while (sw > M_PI) sw -= 2 * M_PI; while (sw < -M_PI) sw += 2 * M_PI;
                if (reversal) sw = d > 0 ? -M_PI : M_PI;      /* half turn around the end, on the offset side */
                int k = (int)ceil(fabs(sw) / (M_PI / 36));
                for (int j = 1; j < k; j++) OP(cx + fabs(d) * cos(a0 + sw * j / k), cy + fabs(d) * sin(a0 + sw * j / k));
                OP(ax, ay);
            } else {                                          /* inside corner: intersect the two offset lines */
                double qx = ptx, qy = pty, den = qx * ty - qy * tx;
                if (fabs(den) > 1e-6) {                       /* (near-collinear: the segments simply join) */
                    double t = ((ax - px) * ty - (ay - py) * tx) / den;
                    o.xy[2 * o.n - 2] = px + qx * t; o.xy[2 * o.n - 1] = py + qy * t;
                }
            }
        } else OP(ax, ay);
        OP(bx, by);
    }
#undef OP
    return o;
}

/* nearest point of loop c to (x, y): segment index, parameter, distance */
static double loop_proj(const CPath *c, double x, double y, int *seg, double *par) {
    double bd = 1e300; *seg = 0; *par = 0;
    for (int i = 0; i < c->n; i++) {
        double ax = c->xy[2 * i], ay = c->xy[2 * i + 1], ux = c->xy[2 * ((i + 1) % c->n)] - ax, uy = c->xy[2 * ((i + 1) % c->n) + 1] - ay;
        double l2 = ux * ux + uy * uy, u = l2 > 0 ? ((x - ax) * ux + (y - ay) * uy) / l2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
        double d = hypot(ax + ux * u - x, ay + uy * u - y);
        if (d < bd) { bd = d; *seg = i; *par = u; }
    }
    return bd;
}

/* open-chain offset trimmed globally (Mastercam removes loops where the offset of a small concave arc inverts; an arc
 * of radius R collapses to its centre, buckle-v12 op 13): the boundary of the disc-swept region of the chain (with the
 * mitre wedges of corners left sharp) from the start's offset point to the end's, on the compensation side.  `raw` is the
 * per-segment offset (its ends are the exact end offsets); returned unchanged if the trim can't be done. */
static CPath offset_open_trim_x(const double *xy, int n, double d, int roll, double tol, CPath raw, int first_arrival);
static CPath offset_open_trim(const double *xy, int n, double d, int roll, double tol, CPath raw) { return offset_open_trim_x(xy, n, d, roll, tol, raw, 0); }
/* first_arrival (decisions batch dec2 Q5, ours - only Peel's stand-in for ccomp_pock, cam2d_dyn.c peel_side): when the chain's end
   points are more than 2 |d| apart the two end caps of the swept region do not touch, so walking the region's boundary from the
   start's offset point along the compensation side reaches the end's offset point before any cap: stop there. The 0.3 x
   raw-length rule below is for nearly closed chains; on a short inside offset (bracket4-dashed chain 8, Peel width 20) it walked
   round both caps and the other side, and the Peel loops crossed themselves ("side curves cross"). */
static CPath offset_open_trim_x(const double *xy, int n, double d, int roll, double tol, CPath raw, int first_arrival) {
    if (raw.n < 2 || n < 2) return raw;
    if (first_arrival && hypot(xy[2 * n - 2] - xy[0], xy[2 * n - 1] - xy[1]) <= 2 * fabs(d) + tol) first_arrival = 0;
    CPath in1 = { (double *)xy, n }; CPaths in = { &in1, 1 };
    CPaths reg = cl_offset_lines(&in, fabs(d), 0, tol);
    if (roll != 2) {                                          /* corners not rolled: their mitre wedges */
        CPaths w = { malloc(n * sizeof(CPath)), 0 };
        for (int i = 1; i + 1 < n; i++) {
            double ax_ = xy[2 * i] - xy[2 * i - 2], ay_ = xy[2 * i + 1] - xy[2 * i - 1], bx_ = xy[2 * i + 2] - xy[2 * i], by_ = xy[2 * i + 3] - xy[2 * i + 1];
            double l1 = hypot(ax_, ay_), l2 = hypot(bx_, by_); if (l1 < 1e-9 || l2 < 1e-9) continue;
            ax_ /= l1; ay_ /= l1; bx_ /= l2; by_ /= l2;
            double cr = ax_ * by_ - ay_ * bx_, tang = atan2(cr, ax_ * bx_ + ay_ * by_);
            if (fabs(tang) < M_PI / 360 || fabs(tang) > M_PI * 0.75) continue;
            if ((cr < 0) != (d > 0) || rolls(roll, tang)) continue;            /* only outside corners on our side */
            double s_ = d > 0 ? -1 : 1, n1x = s_ * ay_, n1y = -s_ * ax_, n2x = s_ * by_, n2y = -s_ * bx_, c = 1 + n1x * n2x + n1y * n2y, a = fabs(d);
            CPath q; q.n = 4; q.xy = malloc(8 * sizeof(double));
            double vx = xy[2 * i], vy = xy[2 * i + 1];
            q.xy[0] = vx; q.xy[1] = vy; q.xy[2] = vx + a * n1x; q.xy[3] = vy + a * n1y;
            q.xy[4] = vx + a * (n1x + n2x) / c; q.xy[5] = vy + a * (n1y + n2y) / c; q.xy[6] = vx + a * n2x; q.xy[7] = vy + a * n2y;
            if (cl_area(&q) < 0) reverse_path(&q);
            w.p[w.n++] = q;
        }
        if (w.n) { CPaths r = cl_bool(&reg, &w, 0); cl_free(&reg); reg = r; }
        cl_free(&w);
    }
    double Sx = raw.xy[0], Sy = raw.xy[1], Ex = raw.xy[2 * raw.n - 2], Ey = raw.xy[2 * raw.n - 1];
    int best = -1; double bd = 1e300;
    for (int L = 0; L < reg.n; L++) { int sg; double u, dd = loop_proj(&reg.p[L], Sx, Sy, &sg, &u); if (dd < bd) { bd = dd; best = L; } }
    CPath out = raw;
    if (best >= 0 && bd < 0.01 * fabs(d) + 2 * tol + 1e-3) {
        const CPath *c = &reg.p[best]; int m = c->n, iS, iE; double uS, uE;
        loop_proj(c, Sx, Sy, &iS, &uS);
        double dE = loop_proj(c, Ex, Ey, &iE, &uE);
        /* walking direction: towards a probe a little way along the raw offset (the start point is where the round
         * end cap begins, so the segment there can belong to the cap) */
        double rl = hypot(raw.xy[2] - Sx, raw.xy[3] - Sy), tt = rl > 0 ? fmin(0.5, 0.2 * fabs(d) / rl) : 0;
        int iq; double uq; loop_proj(c, Sx + (raw.xy[2] - Sx) * tt, Sy + (raw.xy[3] - Sy) * tt, &iq, &uq);
        int fw = (iq - iS + m) % m, bw = (iS - iq + m) % m, dir;
        if (iq == iS) dir = uq >= uS ? 1 : -1; else dir = fw <= bw ? 1 : -1;
        if (dE < 0.01 * fabs(d) + 2 * tol + 1e-3) {
            CPath o; int cap = m + 4; o.n = 0; o.xy = malloc(cap * 2 * sizeof(double));
#define OPT(X, Y) do { if (o.n == cap) { cap *= 2; o.xy = realloc(o.xy, cap * 2 * sizeof(double)); } o.xy[2 * o.n] = (X); o.xy[2 * o.n + 1] = (Y); o.n++; } while (0)
            OPT(Sx, Sy);
            int ok = 0;
            double lraw = 0; for (int k = 1; k < raw.n; k++) lraw += hypot(raw.xy[2 * k] - raw.xy[2 * k - 2], raw.xy[2 * k + 1] - raw.xy[2 * k - 1]);
            if (iE == iS && (dir > 0 ? uE >= uS : uE <= uS) && lraw < 2 * hypot(Ex - Sx, Ey - Sy) + fabs(d)) ok = 1;   /* (not a nearly closed chain) */
            else {   /* walk to the end's segment; a nearly closed chain's end is next to its start: go round first */
                int j = iS; double wl = 0;
#define WLEN() (o.n > 1 ? hypot(o.xy[2 * o.n - 2] - o.xy[2 * o.n - 4], o.xy[2 * o.n - 1] - o.xy[2 * o.n - 3]) : 0)
                for (int g = 0; g <= 2 * m + 1; g++) {
                    if (dir > 0) { j = (j + 1) % m; OPT(c->xy[2 * j], c->xy[2 * j + 1]); }
                    else { OPT(c->xy[2 * j], c->xy[2 * j + 1]); j = (j - 1 + m) % m; }
                    wl += WLEN();
                    if (j == iE && (first_arrival || wl >= 0.3 * lraw)) { ok = 1; break; }
                }
#undef WLEN
            }
            OPT(Ex, Ey);
#undef OPT
            if (ok) { free(raw.xy); out = o; } else free(o.xy);
        }
    }
    cl_free(&reg);
    return out;
}

/* tool-centre paths for a chain at a given offset distance (closed chains: possibly several loops) */
/* free_right: which side of the returned paths is away from the material (leads go there) */
/* exact tangent direction (unit; sign irrelevant) of the chain's first entity at (x, y): the entry is pushed out along its
 * normal, and a sampled polyline's chord normal is half a step off on arcs (buckle op 7: 0.11 mm on a R 3.25 circle) */
static void ent_tangent(const McamFile *f, const Chain *ch, int k, double x, double y, double *tx, double *ty) {
    const Ent *e = &f->ents[ch->ent[k]]; *tx = *ty = 0;
    if (e->type == E_LINE) { double l = hypot(e->b.x - e->a.x, e->b.y - e->a.y); if (l > 0) { *tx = (e->b.x - e->a.x) / l; *ty = (e->b.y - e->a.y) / l; } }
    else if (e->type == E_ARC && !e->vw) { double rx = x - e->c.x, ry = y - e->c.y, l = hypot(rx, ry); if (l > 0) { *tx = -ry / l; *ty = rx / l; } }
}   /* a view arc (Ent.vw) has no XY tangent: 0 (ours; the engines see it mapped into the Tplane, cam_generate) */
static void start_tangent(const McamFile *f, const Chain *ch, double x, double y, double *tx, double *ty) { ent_tangent(f, ch, 0, x, y, tx, ty); }

/* open-chain offset: put its first / last point on the exact normal of the end entities (the polyline's end chords are
 * half a sampling step off on arcs) */
static void fix_open_ends(const McamFile *f, const Chain *ch, const double *xy, int n, CPath *o, double d) {
    if (o->n < 2 || n < 2) return;
    for (int end = 0; end < 2; end++) {
        int k = end ? ch->n - 1 : 0, pi = end ? n - 1 : 0, oi = end ? o->n - 1 : 0, qi = end ? n - 2 : 1;
        double tx, ty; ent_tangent(f, ch, k, xy[2 * pi], xy[2 * pi + 1], &tx, &ty);
        double cx = end ? xy[2 * pi] - xy[2 * qi] : xy[2 * qi] - xy[2 * pi], cy = end ? xy[2 * pi + 1] - xy[2 * qi + 1] : xy[2 * qi + 1] - xy[2 * pi + 1];
        if ((tx == 0 && ty == 0) || tx * cx + ty * cy == 0) continue;
        if (tx * cx + ty * cy < 0) { tx = -tx; ty = -ty; }               /* along the path */
        double ex = xy[2 * pi] - ty * d, ey = xy[2 * pi + 1] + tx * d;    /* left normal for d > 0 */
        if (hypot(ex - o->xy[2 * oi], ey - o->xy[2 * oi + 1]) < 0.1 * fabs(d)) { o->xy[2 * oi] = ex; o->xy[2 * oi + 1] = ey; }
    }
}

static _Thread_local const McamFile *ofile;      /* the part, for exact entity directions in chain_offset (set by the generators) */
/* the start of a closed contour at a corner that is not rolled (concave, or convex left sharp): Mastercam offsets the chain entity by entity (the exact contour offset,
   ccomp_pock_from_ctour) and the trimmed loop starts where the offsets of the chain's last and first entities meet; the start
   pushed out along the first entity's normal lies in the trimmed part there. Ours draws the loop with chords (Clipper), whose
   corner chord cuts that point by up to the chord error - measured: buckle-v6 op 28 0.075, thread-test op 3 0.105 at the plunge.
   left = the offset is on the left of the chain's travel. Returns 1 with the exact meeting point. */
static int ent_travel(const Ent *e, int rev, int at_end, double *px, double *py, double *tx, double *ty) {
    if (e->type == E_LINE) {
        double ax = rev ? e->b.x : e->a.x, ay = rev ? e->b.y : e->a.y, bx = rev ? e->a.x : e->b.x, by = rev ? e->a.y : e->b.y, l = hypot(bx - ax, by - ay);
        if (l <= 0) return 0;
        *px = at_end ? bx : ax; *py = at_end ? by : ay; *tx = (bx - ax) / l; *ty = (by - ay) / l; return 1;
    }
    if (e->type == E_ARC && e->r > 0) {
        double a = e->a0 + ((at_end != rev) ? e->sw : 0); int ccw = (e->sw > 0) != (rev != 0);
        double rx = cos(a), ry = sin(a); *px = e->c.x + e->r * rx; *py = e->c.y + e->r * ry;
        *tx = ccw ? -ry : ry; *ty = ccw ? rx : -rx; return 1;
    }
    return 0;
}
static int ent_offset_carrier(const Ent *e, int rev, double d, int left, int *arc, double *g) {   /* g: line px py ux uy / circle cx cy r */
    if (e->type == E_LINE) {
        double px, py, tx, ty; if (!ent_travel(e, rev, 0, &px, &py, &tx, &ty)) return 0;
        double o = left ? d : -d; *arc = 0; g[0] = px - ty * o; g[1] = py + tx * o; g[2] = tx; g[3] = ty; return 1;
    }
    if (e->type == E_ARC) {
        int ccw = (e->sw > 0) != (rev != 0); double r = (left == ccw) ? e->r - d : e->r + d;
        if (r <= 1e-9) return 0;
        *arc = 1; g[0] = e->c.x; g[1] = e->c.y; g[2] = r; return 1;
    }
    return 0;
}
static int carrier_isect(int a1, const double *g1, int a2, const double *g2, double out[4]) {
    if (!a1 && !a2) { double den = g1[2] * g2[3] - g1[3] * g2[2]; if (fabs(den) < 1e-12) return 0;
        double t = ((g2[0] - g1[0]) * g2[3] - (g2[1] - g1[1]) * g2[2]) / den; out[0] = g1[0] + g1[2] * t; out[1] = g1[1] + g1[3] * t; return 1; }
    if (a1 && a2) { double dx = g2[0] - g1[0], dy = g2[1] - g1[1], D = hypot(dx, dy); if (D < 1e-12 || D > g1[2] + g2[2] || D < fabs(g1[2] - g2[2])) return 0;
        double a = (g1[2] * g1[2] - g2[2] * g2[2] + D * D) / (2 * D), h2 = g1[2] * g1[2] - a * a, h = h2 > 0 ? sqrt(h2) : 0;
        double mx = g1[0] + dx * a / D, my = g1[1] + dy * a / D; out[0] = mx - dy * h / D; out[1] = my + dx * h / D; out[2] = mx + dy * h / D; out[3] = my - dx * h / D; return 2; }
    const double *L = a1 ? g2 : g1, *C = a1 ? g1 : g2;
    double fx = L[0] - C[0], fy = L[1] - C[1], b = fx * L[2] + fy * L[3], c = fx * fx + fy * fy - C[2] * C[2], disc = b * b - c;
    if (disc < 0) return 0; double sq = sqrt(disc);
    out[0] = L[0] + L[2] * (-b - sq); out[1] = L[1] + L[3] * (-b - sq); out[2] = L[0] + L[2] * (-b + sq); out[3] = L[1] + L[3] * (-b + sq); return 2;
}
static int rolls(int roll, double turn);
static int concave_start(const McamFile *f, const Chain *ch, double d, double hx, double hy, int roll, double rtol, double *qx, double *qy) {   /* (hx, hy): the pushed-out start on the offset side */
    if (!f || ch->n < 2 || d <= 0) return 0;
    /* the corner is the contour records': a line whose end is within rtol of the record before it in x and y makes no record
       (chain_to_ctour_array_break, formats/spline_chain.md; buckle-v11 op 18: a 0.003 mm line closing the chain) */
    int iL = ch->n - 1;
    while (iL > 0 && rtol > 0) {
        const Ent *e = &f->ents[ch->ent[iL]]; if (e->type != E_LINE) break;
        if (fabs(e->b.x - e->a.x) > rtol || fabs(e->b.y - e->a.y) > rtol) break;
        iL--;
    }
    const Ent *e0 = &f->ents[ch->ent[0]], *eL = &f->ents[ch->ent[iL]];
    double sx, sy, t0x, t0y, lx, ly, tLx, tLy;
    if (!ent_travel(e0, ch->rev[0], 0, &sx, &sy, &t0x, &t0y) || !ent_travel(eL, ch->rev[iL], 1, &lx, &ly, &tLx, &tLy)) return 0;
    int left = t0x * (hy - sy) - t0y * (hx - sx) > 0;
    double turn = tLx * t0y - tLy * t0x, ang = atan2(turn, tLx * t0x + tLy * t0y);
    int concave = left ? turn > 1e-9 : turn < -1e-9;
    /* convex: a corner left sharp (not rolled - roll none, or "sharp" and the turn under its limit) is the two offsets extended to
       where they meet (the mitre point; for two lines d tan(turn / 2) back along the first, as measured on buckle op 13) */
    if (!concave && (fabs(ang) < 1e-9 || roll == 2 || rolls(roll, ang))) return 0;
    int a0, aL; double g0[4], gL[4], X[4];
    if (!ent_offset_carrier(e0, ch->rev[0], d, left, &a0, g0) || !ent_offset_carrier(eL, ch->rev[iL], d, left, &aL, gL)) return 0;
    int k = carrier_isect(aL, gL, a0, g0, X); if (!k) return 0;
    double o = left ? d : -d, px = sx - t0y * o, py = sy + t0x * o;   /* the pushed-out start */
    int bi = -1; double bd = 2 * d;
    for (int i = 0; i < k; i++) { double dd = hypot(X[2 * i] - px, X[2 * i + 1] - py); if (dd < bd) { bd = dd; bi = i; } }
    if (bi < 0) return 0;
    *qx = X[2 * bi]; *qy = X[2 * bi + 1];
    return 1;
}
/* ours (test): MCCE_CTOFF_DUMP=FILE appends this offset request as a case of tools/peloader/mcce_coff.c's COFF 1 input (the chain's exact
   entities, the flags / angle / gap / tolerances of ccomp_pock, decomp/mccore_ccomp_pock2.c l.150-175), for the DLL unit oracle */
static _Thread_local double g_nc_tol = 0.002;   /* cam_generate: config 1008 (defined below) */
static void ctoff_dump(const CamParams *p, const Chain *ch, const double *xy, int n, double dist) {
    const char *path = getenv("MCCE_CTOFF_DUMP");
    if (!path || !ch->n || !ofile || p->side == SIDE_ON) return;
    static _Thread_local int seq;
    FILE *f = fopen(path, "a");
    if (!f) return;
    CPath one = { (double *)xy, n };
    double a = cl_area(&one);
    int left = p->side == 1;
    if (ch->closed && !p->side_rel) left = (p->side == SIDE_INSIDE) == (a > 0);
    double d = left ? dist : -dist;
    double ang = p->roll == 2 ? 0.5729577951308232 : p->roll == 1 ? 44.99999063801583 : 179.1;
    fprintf(f, "case c%d_%d 1 %.17g %x %.17g %.17g %.17g %.17g 1\n", getpid(), seq++, d, p->ct_nolook ? 0x60027 : 0x40027, ang, cam_chain_tol(1), g_nc_tol, 0.75 * g_nc_tol);
    int spl = 0;
    for (int i = 0; i < ch->n; i++) if (ofile->ents[ch->ent[i]].type != E_LINE && ofile->ents[ch->ent[i]].type != E_ARC) spl = 1;
    if (spl) {
        fprintf(f, "sh %d 0 %d\n", a > 0 ? 1 : -1, n - 1);
        for (int i = 0; i + 1 < n; i++) fprintf(f, "L %.17g %.17g %.17g %.17g\n", xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3]);
    } else {
        fprintf(f, "sh %d 0 %d\n", a > 0 ? 1 : -1, ch->n);
        for (int i = 0; i < ch->n; i++) {
            const Ent *e = &ofile->ents[ch->ent[i]]; int r = ch->rev[i];
            if (e->type == E_LINE) { V3 u = r ? e->b : e->a, v = r ? e->a : e->b; fprintf(f, "L %.17g %.17g %.17g %.17g\n", u.x, u.y, v.x, v.y); }
            else { double sa = r ? e->a0 + e->sw : e->a0, sw = r ? -e->sw : e->sw; fprintf(f, "A %.17g %.17g %.17g %.17g %.17g\n", e->c.x, e->c.y, e->r, sa, sw); }
        }
    }
    fclose(f);
}
/* Infinite look ahead off (user decision D-5, 2026-11-08): only through the port; set by chain_offset when a pass needs anything else (a chain the port refuses, or
   MCCE_CTOFF=0), checked after gen_contour: the op is refused rather than cut with an offset that ignores the flag */
static _Thread_local int g_nolook_refused;
/* NS-3: the true arcs of the op's offset rings (ctoff_port; every result shape). Mastercam's contour pass writer emits one NCI record per
   ctour entity of the offset - an arc entity through gr_arc_mill / ct_arc_mill (formats/nci_writer.md 1, 3; decomp/nciw/core_grmill.c,
   core_ctarc.c) - so the moves this program cuts along a ring arc's tessellation are tagged as that arc (Move.arc: the post's G2 / G3
   and nci_write.c's bnci_wr_arc). Reset per op (gen_contour). */
typedef struct { double cx, cy, r, a0, sw; } CtArc;
static _Thread_local CtArc *g_ct_arcs; static _Thread_local int g_ct_narcs, g_ct_carcs;
static void ct_arcs_reset(void) { g_ct_narcs = 0; }
static void ct_arcs_add(const McceEnt *x) {
    if (g_ct_narcs == g_ct_carcs) { g_ct_carcs = g_ct_carcs ? 2 * g_ct_carcs : 64; g_ct_arcs = realloc(g_ct_arcs, (size_t)g_ct_carcs * sizeof *g_ct_arcs); }
    g_ct_arcs[g_ct_narcs++] = (CtArc){ x->a[0], x->a[1], x->a[2], x->a[3], x->a[4] };
}
static int ct_on_arc(const CtArc *a, double x, double y) {           /* on the circle and inside the arc's sweep */
    double d = hypot(x - a->cx, y - a->cy); if (fabs(d - a->r) > 1e-7 * fmax(1, a->r)) return 0;
    if (fabs(a->sw) >= 2 * M_PI - 1e-9) return 1;
    double t = atan2(y - a->cy, x - a->cx) - a->a0;
    if (a->sw > 0) { while (t < -1e-9) t += 2 * M_PI; while (t > 2 * M_PI - 1e-9) t -= 2 * M_PI; return t <= a->sw + 1e-9 || t >= 2 * M_PI - 1e-9; }
    while (t > 1e-9) t -= 2 * M_PI; while (t < -2 * M_PI + 1e-9) t += 2 * M_PI; return t >= a->sw - 1e-9 || t <= -2 * M_PI + 1e-9;
}
static int ct_seg_arc(const Move *u, const Move *v) {                /* the ring arc a feed move lies on, -1 = none */
    if (v->rapid || v->arc || fabs(u->p.z - v->p.z) > 1e-9 || hypot(v->p.x - u->p.x, v->p.y - u->p.y) < 1e-12) return -1;
    for (int i = 0; i < g_ct_narcs; i++) {
        const CtArc *a = &g_ct_arcs[i];
        if (!ct_on_arc(a, u->p.x, u->p.y) || !ct_on_arc(a, v->p.x, v->p.y)) continue;
        double ta = atan2(u->p.y - a->cy, u->p.x - a->cx), tb = atan2(v->p.y - a->cy, v->p.x - a->cx), dt = tb - ta;
        while (dt > M_PI) dt -= 2 * M_PI; while (dt < -M_PI) dt += 2 * M_PI;
        if ((dt > 0) != (a->sw > 0)) continue;                         /* the move runs along the arc's sweep direction */
        return i;
    }
    return -1;
}
static void ct_tag_arcs(MB *b, int from) {                           /* moves [from, b->n): runs on one ring arc tagged as that arc */
    for (int k = from > 0 ? from : 1; k < b->n; ) {
        int a = ct_seg_arc(&b->m[k - 1], &b->m[k]);
        if (a < 0) { k++; continue; }
        int j = k + 1; while (j < b->n && ct_seg_arc(&b->m[j - 1], &b->m[j]) == a) j++;
        int id = ++g_arc_seq; if (g_arc_seq > 1000000000) g_arc_seq = 0;
        for (int i = k; i < j; i++) { b->m[i].arc = id; b->m[i].cx = g_ct_arcs[a].cx; b->m[i].cy = g_ct_arcs[a].cy; b->m[i].cw = g_ct_arcs[a].sw < 0; }
        k = j;
    }
}
/* The contour offset through the port of MCCECalcContourOffset (mcce_ctour.c): the exact entities of the chain, the parameters of ccomp_pock
   (decomp/mccore_ccomp_pock2.c l.150-175; d > 0 = left of travel), true arcs tessellated as cam_chain_polyline does. 0 = not used (refused or switched off): the caller
   keeps the Clipper offset; 1 = *out is the offset (possibly empty). On by default (user decision D-4, 2026-11-08), MCCE_CTOFF=0 switches it off: formats/mcce_ctour_offset.md. */
static int ctoff_port(const CamParams *p, const Chain *ch, const double *xy, int n, double dist, CPaths *out, McceEnt **ring, int *nring) {
    const char *e = getenv("MCCE_CTOFF");
    if ((e && atoi(e) == 0) || !ofile || p->side == SIDE_ON) return 0;
    for (int i = 0; i < ch->n; i++) if (ofile->ents[ch->ent[i]].type != E_LINE && ofile->ents[ch->ent[i]].type != E_ARC) { if (getenv("MCCE_DEBUG")) fprintf(stderr, "ctoff: skipped: a chain entity is not a line or arc\n"); return 0; }
    if (ch->cut0 > 1e-12 || ch->cut1 > 1e-12) { if (getenv("MCCE_DEBUG")) fprintf(stderr, "ctoff: skipped: the chain's ends are trimmed\n"); return 0; }
    CPath one = { (double *)xy, n }; double a = cl_area(&one);
    int left = p->side == 1;
    if (ch->closed && !p->side_rel) left = (p->side == SIDE_INSIDE) == (a > 0);   /* (as ctoff_dump: an open chain's side is left / right of travel) */
    double d = left ? dist : -dist, ang = p->roll == 2 ? 0.5729577951308232 : p->roll == 1 ? 44.99999063801583 : 179.1;
    McceEnt *ents = malloc((size_t)ch->n * sizeof(McceEnt));
    for (int i = 0; i < ch->n; i++) {
        const Ent *E = &ofile->ents[ch->ent[i]]; int r = ch->rev[i]; McceEnt *x = &ents[i];
        memset(x, 0, sizeof *x);
        if (E->type == E_LINE) { V3 u = r ? E->b : E->a, v = r ? E->a : E->b; x->id = 'L'; x->a[0] = u.x; x->a[1] = u.y; x->a[2] = v.x; x->a[3] = v.y; }
        else { x->id = 'A'; x->a[0] = E->c.x; x->a[1] = E->c.y; x->a[2] = E->r; x->a[3] = r ? E->a0 + E->sw : E->a0; x->a[4] = r ? -E->sw : E->sw; }
    }
    McceShape in = { ents, ch->n, a > 0 ? 1 : -1, 0 };
    McceResult res = mcce_contour_offset(&in, 1, d, p->ct_nolook ? 0x60027 : 0x40027, ang, cam_chain_tol(1), g_nc_tol, 0.75 * g_nc_tol);
    free(ents);
    if (res.refused) { if (getenv("MCCE_DEBUG")) fprintf(stderr, "ctoff: refused: %s\n", res.refused); return 0; }
    if (ch->closed && res.n && res.s[0].dir == 0) { if (getenv("MCCE_DEBUG")) fprintf(stderr, "ctoff: refused: a chain this program calls closed that MCCE treats as open (gap over chain_tol)\n"); mcce_shapes_free(&res); return 0; }   /* ours: a refusal only, the Clipper path as before */
    if (getenv("MCCE_DEBUG")) fprintf(stderr, "ctoff: ported\n");
    for (int i = 0; i < res.n; i++) for (int k = 0; k < res.s[i].n; k++) if (res.s[i].e[k].id == 'A') ct_arcs_add(&res.s[i].e[k]);   /* NS-3 */
    if (res.n >= 1 && res.s[0].n > 0) { *nring = res.s[0].n; *ring = malloc((size_t)res.s[0].n * sizeof(McceEnt)); memcpy(*ring, res.s[0].e, (size_t)res.s[0].n * sizeof(McceEnt)); }   /* the true entities, for ctoff_set_dirs */
    /* ccomp_pock_from_ctour (decomp/mccore_ccomp_pock.c l.58-63, the caller for the Contour pass): only the first result bound (the head of ccomp_pock's list) becomes the pass (pock_bound_to_ctour); an open chain's first bound is the piece with the lowest source entity (FUN_1800cead0) */
    int npath = res.n > 1 ? 1 : res.n;
    out->n = npath; out->p = malloc((size_t)(npath ? npath : 1) * sizeof(CPath));
    for (int i = 0; i < npath; i++) {
        int cap = 256, m = 0; double *q = malloc((size_t)cap * 2 * sizeof(double));
#define CTP(X, Y) do { if (m == cap) { cap *= 2; q = realloc(q, (size_t)cap * 2 * sizeof(double)); } q[2 * m] = (X); q[2 * m + 1] = (Y); m++; } while (0)
        for (int k = 0; k < res.s[i].n; k++) {
            McceEnt *x = &res.s[i].e[k];
            if (x->id == 'L') CTP(x->a[0], x->a[1]);
            else {
                double step = x->a[2] > p->tol ? 2 * acos(1 - p->tol / x->a[2]) : M_PI / 8; if (step > M_PI / 90) step = M_PI / 90;   /* ours: finer than cam_chain_polyline so that the start-point test of chain_offset (0.01 d + 1e-3 from the loop) holds */
                int steps = (int)ceil(fabs(x->a[4]) / step); if (steps < 2) steps = 2;
                for (int j = 0; j < steps; j++) { double t = x->a[3] + x->a[4] * j / steps; CTP(x->a[0] + x->a[2] * cos(t), x->a[1] + x->a[2] * sin(t)); }
            }
        }
        if ((!ch->closed || res.s[i].dir == 0) && res.s[i].n) {         /* an open pass: its last end as well (a closed ring of a chain the program calls closed ends on its first point) */
            McceEnt *x = &res.s[i].e[res.s[i].n - 1];
            if (x->id == 'L') CTP(x->a[2], x->a[3]);
            else { double t = x->a[3] + x->a[4]; CTP(x->a[0] + x->a[2] * cos(t), x->a[1] + x->a[2] * sin(t)); }
        }
#undef CTP
        out->p[i].n = m; out->p[i].xy = q;
    }
    mcce_shapes_free(&res);
    return 1;
}
/* The exact directions of the pass's two ends (rings from the port). Mastercam builds the leads on the path's own first and last elements, which are true lines / arcs: FUN_180051990
   (decomp/mcmill_all.c l.56236-56246 first element of the path local_188, l.56277 / l.56294 FUN_1801c24a0 entry; l.56297-56310 last element, l.56347 / l.56365 exit), and
   FUN_1801c24a0 (l.311687-311688) takes eval_gt_pt / eval_gt_tan of that element at t = 0 / 1. The port's ring is tessellated for this program's polyline passes (ctoff_port),
   so its first / last chord is half a step off the true tangent on an arc (2 degree steps: 1 degree; the lead turns with it about the attach point): these are the tangents of
   the ring's own entities at the start point. Only the sign and the choice between two entities meeting there come from the chords (formats/mcce_ctour_offset.md 4). */
typedef struct { int valid; double x, y, t0x, t0y, t1x, t1y; } CtoffDirs;
static _Thread_local CtoffDirs g_ctoff_dirs;
/* the unit tangent (in the entity's own direction) of a ring entity at (x, y); 0 = the point is not on it (within tol) */
static int ctoff_ent_tangent(const McceEnt *e, double x, double y, double tol, double *tx, double *ty) {
    if (e->id == 'L') {
        double ux = e->a[2] - e->a[0], uy = e->a[3] - e->a[1], l2 = ux * ux + uy * uy;
        if (l2 <= 0) return 0;
        double u = ((x - e->a[0]) * ux + (y - e->a[1]) * uy) / l2; u = u < 0 ? 0 : u > 1 ? 1 : u;
        if (hypot(e->a[0] + ux * u - x, e->a[1] + uy * u - y) > tol) return 0;
        double l = sqrt(l2); *tx = ux / l; *ty = uy / l;
        return 1;
    }
    double dx = x - e->a[0], dy = y - e->a[1], d = hypot(dx, dy);
    if (d <= 0 || fabs(d - e->a[2]) > tol) return 0;
    double sw = e->a[4], t = fmod(atan2(dy, dx) - e->a[3], 2 * M_PI), eps = tol / e->a[2];       /* the angle along the sweep, [0, 2 pi) in the sweep's sense */
    t = sw >= 0 ? t : -t; if (t < 0) t += 2 * M_PI;
    if (t - fabs(sw) > eps && 2 * M_PI - t > eps) return 0;
    *tx = sw >= 0 ? -dy / d : dy / d; *ty = sw >= 0 ? dx / d : -dx / d;                          /* CCW: the left normal of the radius */
    return 1;
}
static void ctoff_set_dirs(const CPath *c, const McceEnt *e, int ne) {
    if (c->n < 2) return;
    double x = c->xy[0], y = c->xy[1], cd[2][2], t[2][2];
    cd[0][0] = c->xy[2] - x; cd[0][1] = c->xy[3] - y;                       /* the first chord, the closing chord */
    cd[1][0] = x - c->xy[2 * c->n - 2]; cd[1][1] = y - c->xy[2 * c->n - 1];
    for (int s = 0; s < 2; s++) {
        double l = hypot(cd[s][0], cd[s][1]), best = 0.9, bx = 0, by = 0;   /* (only a chord-sized correction, as exact_end_dirs) */
        if (l <= 0) return;
        for (int i = 0; i < ne; i++) {
            double tx, ty; if (!ctoff_ent_tangent(&e[i], x, y, 1e-5, &tx, &ty)) continue;
            double dot = (tx * cd[s][0] + ty * cd[s][1]) / l;               /* the pass may run the ring backwards (chain_offset's reverse_path) */
            if (dot < 0) { dot = -dot; tx = -tx; ty = -ty; }
            if (dot > best) { best = dot; bx = tx; by = ty; }
        }
        if (bx == 0 && by == 0) return;
        t[s][0] = bx; t[s][1] = by;
    }
    g_ctoff_dirs = (CtoffDirs){ 1, x, y, t[0][0], t[0][1], t[1][0], t[1][1] };
}
static void ctoff_exact_dirs(const CPath *c, double *t0x, double *t0y, double *t1x, double *t1y) {
    if (!g_ctoff_dirs.valid || c->n < 1 || c->xy[0] != g_ctoff_dirs.x || c->xy[1] != g_ctoff_dirs.y) return;
    *t0x = g_ctoff_dirs.t0x; *t0y = g_ctoff_dirs.t0y; *t1x = g_ctoff_dirs.t1x; *t1y = g_ctoff_dirs.t1y;
}
static CPaths chain_offset(const CamParams *p, const Chain *ch, const double *xy, int n, double dist, double sx, double sy, double ttx, double tty, int *free_right) {
    CPaths loops = { 0 };
    g_ctoff_dirs.valid = 0;
    ctoff_dump(p, ch, xy, n, dist);
    CPath one = { (double *)xy, n };
    int side = p->side, chain_ccw = cl_area(&one) > 0;
    *free_right = 0;
    if (ch->closed && p->side_rel && side != SIDE_ON)         /* left of a CCW loop = inside */
        side = (side == 1) == (cl_area(&one) > 0) ? SIDE_INSIDE : SIDE_OUTSIDE;
    if (ch->closed && side != SIDE_ON) {
        CPaths in = { &one, 1 };
        double delta = dist * (side == SIDE_OUTSIDE ? 1 : -1);
        McceEnt *ring = NULL; int nring = 0;
        int ported = ctoff_port(p, ch, xy, n, dist, &loops, &ring, &nring);
        if (!ported && p->ct_nolook) g_nolook_refused = 1;
        if (!ported) loops = cl_offset(&in, delta, p->tol);
        if (!ported && p->roll != 2 && dist > 0) {                       /* corners not rolled: add / cut their mitre wedges */
            CPaths w = { malloc(n * sizeof(CPath)), 0 };
            int right = (chain_ccw ? 1 : -1) * (delta > 0 ? 1 : -1) > 0;   /* offset side of each segment */
            for (int i = 0; i < n; i++) {
                int ip = i, k = 0;          /* vertex i: previous distinct point, next distinct point (closed: wrap) */
                double vx = xy[2 * i], vy = xy[2 * i + 1], ax_ = 0, ay_ = 0, bx_ = 0, by_ = 0, l1 = 0, l2 = 0;
                for (k = 1; k < n && l1 < 1e-9; k++) { ip = (i - k + n) % n; ax_ = vx - xy[2 * ip]; ay_ = vy - xy[2 * ip + 1]; l1 = hypot(ax_, ay_); }
                for (k = 1; k < n && l2 < 1e-9; k++) { ip = (i + k) % n; bx_ = xy[2 * ip] - vx; by_ = xy[2 * ip + 1] - vy; l2 = hypot(bx_, by_); }
                if (l1 < 1e-9 || l2 < 1e-9) continue;
                ax_ /= l1; ay_ /= l1; bx_ /= l2; by_ /= l2;
                double cr = ax_ * by_ - ay_ * bx_, tang = atan2(cr, ax_ * bx_ + ay_ * by_);
                if (fabs(tang) < M_PI / 360 || fabs(tang) > M_PI * 0.75) continue;      /* (tiny turns: arc ~ mitre) */
                if ((cr > 0) != right || rolls(p->roll, tang)) continue;              /* inside corner / rolled */
                double s_ = right ? 1 : -1, n1x = s_ * ay_, n1y = -s_ * ax_, n2x = s_ * by_, n2y = -s_ * bx_;   /* offset-side normals */
                double c = 1 + n1x * n2x + n1y * n2y, d = fabs(dist);
                CPath q; q.n = 4; q.xy = malloc(8 * sizeof(double));
                q.xy[0] = vx; q.xy[1] = vy; q.xy[2] = vx + d * n1x; q.xy[3] = vy + d * n1y;
                q.xy[4] = vx + d * (n1x + n2x) / c; q.xy[5] = vy + d * (n1y + n2y) / c; q.xy[6] = vx + d * n2x; q.xy[7] = vy + d * n2y;
                if (cl_area(&q) < 0) reverse_path(&q);
                w.p[w.n++] = q;
            }
            if (w.n) { CPaths r = cl_bool(&loops, &w, delta > 0 ? 0 : 2); cl_free(&loops); loops = r; }
            cl_free(&w);
        }
        int exact_start = 0; double exs = 0, eys = 0;   /* the exact concave-corner start (concave_start) */
        for (int li = 0; li < loops.n; li++) {
            CPath *c = &loops.p[li];
            int want_ccw = p->side_rel ? chain_ccw : side == SIDE_INSIDE;   /* Mastercam: the chain's direction; else climb */
            if ((cl_area(c) > 0) != want_ccw) reverse_path(c);
            *free_right = side == SIDE_INSIDE ? !want_ccw : want_ccw;
            if (li == 0) {          /* entry: the start point pushed out along the chain's normal (onto that entity's offset) */
                int k = 0; double bd = 1e300;
                for (int i = 0; i + 1 < n; i++) {   /* the segment the point lies on (the one leaving it, on a tie) */
                    double ax = xy[2 * i], ay = xy[2 * i + 1], ux = xy[2 * i + 2] - ax, uy = xy[2 * i + 3] - ay, l2 = ux * ux + uy * uy;
                    double u = l2 > 0 ? ((sx - ax) * ux + (sy - ay) * uy) / l2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
                    double d = hypot(ax + ux * u - sx, ay + uy * u - sy) * 1e6 + hypot(ax - sx, ay - sy);
                    if (d < bd) { bd = d; k = i; }
                }
                double tx = xy[2 * k + 2] - xy[2 * k], ty = xy[2 * k + 3] - xy[2 * k + 1], tl = hypot(tx, ty);
                if (ttx != 0 || tty != 0) { tx = ttx; ty = tty; tl = 1; }
                if (tl > 0) {
                    double best = 1e300, ex = sx, ey = sy;
                    for (int sg = -1; sg <= 1; sg += 2) {
                        double qx = sx - ty / tl * dist * sg, qy = sy + tx / tl * dist * sg, dmin = 1e300;
                        for (int i = 0; i < c->n; i++) {
                            double ax = c->xy[2 * i], ay = c->xy[2 * i + 1], ux = c->xy[2 * ((i + 1) % c->n)] - ax, uy = c->xy[2 * ((i + 1) % c->n) + 1] - ay;
                            double l2 = ux * ux + uy * uy, u = l2 > 0 ? ((qx - ax) * ux + (qy - ay) * uy) / l2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
                            double d = hypot(ax + ux * u - qx, ay + uy * u - qy); if (d < dmin) dmin = d;
                        }
                        if (dmin < best) { best = dmin; ex = qx; ey = qy; }
                    }
                    if (hypot(sx - xy[0], sy - xy[1]) < 1e-9 && concave_start(ofile, ch, dist, ex, ey, p->roll, p->ct_rec_tol, &exs, &eys)) exact_start = 1;
                    if (best < 0.01 * dist + 1e-3) {              /* only when that point is really on the loop */
                        int at_start = hypot(sx - xy[0], sy - xy[1]) < 1e-9;
                        sx = ex; sy = ey;
                        /* entering at the chain's start where the closing corner is an outside corner left sharp (not
                         * rolled): Mastercam starts at the mitre point, d tan(turn / 2) back along the first entity
                         * (buckle op 13: 0.25 mm before the pushed-out start at a 36.9 deg turn) */
                        if (at_start && n > 2 && !ofile) {   /* with the part: concave_start, on the exact entities (a smooth closed curve has no corner) */
                            double t1x = xy[2] - xy[0], t1y = xy[3] - xy[1], l1 = hypot(t1x, t1y), t0x = 0, t0y = 0, l0 = 0;
                            for (int k2 = n - 1; k2 > 0 && l0 < 1e-9; k2--) { t0x = xy[0] - xy[2 * k2]; t0y = xy[1] - xy[2 * k2 + 1]; l0 = hypot(t0x, t0y); }
                            if (ttx != 0 || tty != 0) { double sg = ttx * t1x + tty * t1y >= 0 ? 1 : -1; t1x = ttx * sg; t1y = tty * sg; l1 = 1; }
                            if (l1 > 1e-12 && l0 > 1e-12) {
                                t1x /= l1; t1y /= l1; t0x /= l0; t0y /= l0;
                                double cr = t0x * t1y - t0y * t1x, tang = atan2(cr, t0x * t1x + t0y * t1y);
                                double nx = sx - xy[0], ny = sy - xy[1];              /* offset side: left if n x t1 < 0 */
                                int off_right = t1x * ny - t1y * nx < 0;
                                if ((cr > 0) == off_right && fabs(tang) > 1e-6 && !rolls(p->roll, tang) && p->roll != 2) {
                                    double back = dist * tan(fabs(tang) / 2);
                                    sx -= t1x * back; sy -= t1y * back;
                                }
                            }
                        }
                    }
                }
            }
            if (li == 0 && exact_start) { sx = exs; sy = eys; }
            rotate_to(c, sx, sy);
            if (li == 0 && exact_start && c->n && hypot(c->xy[0] - sx, c->xy[1] - sy) < 0.05 * dist + 1e-3) { c->xy[0] = sx; c->xy[1] = sy; }   /* the exact corner */
            if (li == 0 && ring) ctoff_set_dirs(c, ring, nring);
        }
        free(ring);
    } else if (!ch->closed && p->side != SIDE_ON) {
        /* open chain: the port (user decisions D-4 / D-5); its first shape is the pass (ctoff_port) and runs along the chain, so no reversal */
        McceEnt *ring = NULL; int nring = 0;
        CPaths pl = { 0 };
        int ported = ctoff_port(p, ch, xy, n, dist, &pl, &ring, &nring);
        free(ring);
        if (ported) { *free_right = p->side == 2; return pl; }
        if (p->ct_nolook) g_nolook_refused = 1;
        loops.n = 1; loops.p = malloc(sizeof(CPath));
        loops.p[0] = offset_open(xy, n, p->side == 1 ? dist : -dist, p->roll);
        if (ofile) fix_open_ends(ofile, ch, xy, n, &loops.p[0], p->side == 1 ? dist : -dist);
        if (dist > 0) loops.p[0] = offset_open_trim(xy, n, p->side == 1 ? dist : -dist, p->roll, p->tol, loops.p[0]);
        *free_right = p->side == 2;                           /* tool right of the chain: material on its left */
    } else {
        loops.n = 1; loops.p = malloc(sizeof(CPath)); loops.p[0].n = n; loops.p[0].xy = malloc(n * 2 * sizeof(double));
        memcpy(loops.p[0].xy, xy, n * 2 * sizeof(double));
        /* compensation Off: along the chain as chained, from its start - Mastercam's contour runs along the stored chain (TP_BND +0xa8
           direction, formats/mcam.md "TP_BND", checked against Mastercam's NCI with tools/oracle.py; the side_rel path here). Was ours
           (regen-consistency 2026-11-19, user decision 4): a closed chain without a stored direction was cut CW */
    }
    return loops;
}

/* Contour Cut 2938 corner options around the offset (FUN_180051990, decomp/mcmill_all.c l.56077-56150; ccomp_pock_from_ctour
   with the roll's minimum fillet angle set_min_fil_angle(0x5d3): 0 None = pi, 1 Sharp = 0.785398, 2 All = 0.01, mccore
   0x1802c8930):
   - Optimize cutter comp in control (op +0x56c = record 0x5d4) with Control compensation (comp type 1), D > tol and contour
     type 2D / 2D chamfer / Ramp / Remachining (bVar18: 0x35 >> type): offset by d + 1.01 x D/2 on the cut side (the roll's
     angle), then back by 1.01 x D/2 with angle pi (sharp corners) - concave arcs and gaps the tool cannot reach become sharp
     corners (what a control's gouge check accepts);
   - else with an external corner break radius b (ctour_prm +0x240 = 0x1468) > tol: the chain offset by b to the side away
     from the cut, then back by b with angle 0.01 (every corner rolled): the chain's outside corners get radius b;
   - then with an internal corner rounding radius r (+0x228 = 0x1450) > tol: offset by r + d on the cut side, back by r to the
     other side with angle 0.01: the toolpath's inside corners get radius r (failing that, the plain offset by d).
   Closed chains here; open chains: refused (ours, the open offset of this program has no such step). Returns the loops, or
   n = 0 with *why set. */
static CPaths chain_offset(const CamParams *p, const Chain *ch, const double *xy, int n, double dist, double sx, double sy, double ttx, double tty, int *free_right);
static void loops_orient_like(CPaths *l, int ccw) { for (int i = 0; i < l->n; i++) if ((cl_area(&l->p[i]) > 0) != ccw) reverse_path(&l->p[i]); }
static int ct_optimize(const CamParams *p) {
    return p->ct_comp == 1 && p->ct_ccopt && p->tool_dia > CAM_SYS_TOL && (p->contour_type == 0 || p->contour_type == 1);
}
static int ct_corners(const CamParams *p) { return ct_optimize(p) || p->ct_crnd > CAM_SYS_TOL || p->ct_cbrk > CAM_SYS_TOL; }
static CPaths ct_offset(const CamParams *p, const Chain *ch, const double *xy, int n, double dist, double sx, double sy, double ttx, double tty, int *free_right, const char **why) {
    *why = NULL;
    if (!ct_corners(p) || p->side == SIDE_ON) return chain_offset(p, ch, xy, n, dist, sx, sy, ttx, tty, free_right);
    CPaths none = { 0 };
    if (!ch->closed) { *why = "internal corner rounding / external corner break / optimize cutter comp on an open chain are not generated here (ours)"; return none; }
    /* which way the cut side lies (chain_offset's rule): outside = growing the loop */
    CPath one = { (double *)xy, n }; int ccw = cl_area(&one) > 0, side = p->side;
    if (p->side_rel) side = (side == 1) == ccw ? SIDE_INSIDE : SIDE_OUTSIDE;
    double sg = side == SIDE_OUTSIDE ? 1 : -1, tol = p->tol;
    double *bx = NULL; int bn = n;
    if (!ct_optimize(p) && p->ct_cbrk > CAM_SYS_TOL) {        /* break the chain's outside corners */
        CPaths in = { &one, 1 }, a = cl_offset(&in, -sg * p->ct_cbrk, tol), b = { 0 };
        if (a.n) b = cl_offset(&a, sg * p->ct_cbrk, tol);
        cl_free(&a);
        if (b.n != 1) { cl_free(&b); *why = "the external corner break radius does not leave one contour (ours: refused)"; return none; }
        loops_orient_like(&b, ccw);
        CPath *q = &b.p[0]; rotate_to(q, xy[0], xy[1]);
        bn = q->n; bx = malloc((size_t)(bn + 1) * 2 * sizeof(double)); memcpy(bx, q->xy, (size_t)bn * 2 * sizeof(double));
        bx[2 * bn] = bx[0]; bx[2 * bn + 1] = bx[1]; bn++;      /* closed polylines repeat the first point (cam_chain_polyline) */
        cl_free(&b);
    }
    const double *cx = bx ? bx : xy;
    double rr = ct_optimize(p) ? p->tool_dia / 2 * 1.01 : p->ct_crnd > CAM_SYS_TOL ? p->ct_crnd : 0;
    CPaths l1 = chain_offset(p, ch, cx, bn, dist + rr, sx, sy, ttx, tty, free_right);
    if (rr <= 0 || !l1.n) { free(bx); return l1; }
    int lccw = cl_area(&l1.p[0]) > 0; double x0 = l1.p[0].xy[0], y0 = l1.p[0].xy[1];
    CPaths in = { malloc((size_t)l1.n * sizeof(CPath)), l1.n };
    for (int i = 0; i < l1.n; i++) { in.p[i].n = l1.p[i].n; in.p[i].xy = malloc((size_t)l1.p[i].n * 2 * sizeof(double)); memcpy(in.p[i].xy, l1.p[i].xy, (size_t)l1.p[i].n * 2 * sizeof(double)); }
    loops_orient_like(&in, 1);
    CPaths l2 = ct_optimize(p) ? cl_offset_join(&in, -sg * rr, 1, tol) : cl_offset(&in, -sg * rr, tol);   /* sharp (pi) / every corner rolled (0.01) */
    cl_free(&in);
    if (!l2.n) {                                              /* the rounding failed: the plain offset (FUN_180051990 l.56120) */
        cl_free(&l2); cl_free(&l1); CPaths r = chain_offset(p, ch, cx, bn, dist, sx, sy, ttx, tty, free_right); free(bx); return r; }
    cl_free(&l1); free(bx);
    g_ctoff_dirs.valid = 0;                                       /* l2 is not the port's ring */
    loops_orient_like(&l2, lccw);
    rotate_to(&l2.p[0], x0, y0);
    return l2;
}

static int lead_steps(double r, double sw, double tol) {   /* segments so the chords stay within tol of the arc */
    double t = fmax(tol, 1e-4), a = r > t ? 2 * acos(1 - t / r) : M_PI / 2;
    int k = (int)ceil(fabs(sw) / fmin(a, M_PI / 8)); return k < 2 ? 2 : k > 720 ? 720 : k;
}

/* lead geometry, one side (the piece builder FUN_1801c24a0, decomp/mcmill_all.c l.311657, as the contour lead application
   FUN_180051990 l.55862 calls it; formats/mcmill_2d_engines.md 1.4): an arc of radius r and sweep sw tangent to the path
   at s (direction t) with its centre on the free side (left, or right with lead_right: side 'L' / 'R'), and a line of
   length len on the arc's far end - along its tangent, or along the normal sg x leftnormal(travel) with Perpendicular
   (op +0x891 / +0x8d9). Without an arc the line is built on the path's end itself. Ramp heights: each piece's z rises by
   its height going away from the cut (the line dzl 0x908 / 0x950, the arc dza 0x920 / 0x968; FUN_180051990's +0x78 sums),
   the arc's z linear in its steps. */
typedef struct { double r, sw, len, dzl, dza; int perp; } LeadS;
static double lead_sweep_v(double sw) { return sw > 1e-6 ? sw : M_PI / 2; }   /* ours: 0 = a quarter (ops made without a record) */
static int lead_has(const LeadS *L) { return L->r > 0 || L->len > 0; }
/* the two sides as the op has them (FUN_180051990 bVar22..25, decomp/mcmill_all.c l.55976-56018: leads 0x8f0 and the side on;
   the arc with sweep > 1e-6 and radius > nc_tol, the line with |length| > nc_tol - review 2026-10-23: was > 0, and a sweep 0
   from the record / a patch was a quarter: the contour's is now no arc (lead_sw_rec); the pocket's lead application
   FUN_1801cd200 is NOT DECODED, its sweep 0 stays the quarter fallback) */
static void lead_side(const CamParams *p, LeadS *L, double r, double sw, double len, int rec) {
    double tol = g_nc_tol;
    L->sw = rec && p->type == CAM_CONTOUR ? sw : lead_sweep_v(sw);
    L->r = r > tol && L->sw > 1e-6 ? r : 0;
    L->len = len > tol ? len : 0;
}
static void lead_sides(const CamParams *p, LeadS *in, LeadS *out) {
    memset(in, 0, sizeof *in); memset(out, 0, sizeof *out);
    if (!p->lead_on) return;
    if (p->lead_in_on) { lead_side(p, in, p->lead_r, p->lead_sw, p->lead_len, p->lead_sw_rec & 1);
                         in->dzl = p->mc_lead_dz[0]; in->dza = p->mc_lead_dz[1]; in->perp = p->lead_in_perp != 0; }
    if (p->lead_out_on) { lead_side(p, out, p->lead_xr, p->lead_xsw, p->lead_xlen, p->lead_sw_rec & 2);
                          out->dzl = p->mc_lead_dz[2]; out->dza = p->mc_lead_dz[3]; out->perp = p->lead_out_perp != 0; }
}
static int lead_any(const CamParams *p) { LeadS i, o; lead_sides(p, &i, &o); return lead_has(&i) || lead_has(&o); }
static void lead_arc_start(const CamParams *p, const LeadS *L, double sx, double sy, double tx, double ty, double *cx, double *cy, double *a0) {
    double r = L->r, sg = p->lead_right ? -1 : 1, nx = -ty * sg, ny = tx * sg;   /* normal towards the free side */
    *cx = sx + nx * r; *cy = sy + ny * r;
    *a0 = atan2(sy - *cy, sx - *cx) - sg * L->sw;            /* the arc runs into s (CCW, or CW when mirrored) */
}
static void lead_in(MB *b, const CamParams *p, const LeadS *L, double sx, double sy, double tx, double ty, double z) {
    double lf = p->lead_fin_on && p->lead_fin_v > 0 && p->lead_on ? p->lead_fin_v : p->feed;   /* Override feed rate (the writer l.8139-8160) */
    double sg = p->lead_right ? -1 : 1;
    if (L->r <= 0) {                                         /* the line alone, on the path's first element */
        if (L->len > 0) mv(b, 0, sx + (L->perp ? -ty * sg : -tx) * L->len, sy + (L->perp ? tx * sg : -ty) * L->len, z + L->dzl, lf);
        mv(b, 0, sx, sy, z, lf); return;
    }
    double r = L->r, cx, cy, a0, sw = sg * L->sw; lead_arc_start(p, L, sx, sy, tx, ty, &cx, &cy, &a0);
    double ax = cx + r * cos(a0), ay = cy + r * sin(a0), ux = -sin(a0) * sg, uy = cos(a0) * sg;   /* arc start + travel direction */
    double za = z + L->dza;
    if (L->len > 0) {
        if (L->perp) mv(b, 0, ax - uy * sg * L->len, ay + ux * sg * L->len, za + L->dzl, lf);
        else mv(b, 0, ax - ux * L->len, ay - uy * L->len, za + L->dzl, lf);
    }
    mv(b, 0, ax, ay, za, lf);
    int k = lead_steps(r, sw, p->tol), k0 = b->n; for (int j = 1; j < k; j++) mv(b, 0, cx + r * cos(a0 + sw * j / k), cy + r * sin(a0 + sw * j / k), za + (z - za) * j / k, lf);
    mv(b, 0, sx, sy, z, lf);
    ps_arc_rec(b, k0, b->n - 1, cx, cy, sw < 0);               /* Smoothing: one arc record */
}
static void lead_out(MB *b, const CamParams *p, const LeadS *L, double ex, double ey, double tx, double ty, double z) {
    double lf = p->lead_fout_on && p->lead_fout_v > 0 && p->lead_on ? p->lead_fout_v : p->feed;   /* Override feed rate (the writer l.8139-8160) */
    double sg = p->lead_right ? -1 : 1;
    if (L->r <= 0) {                                         /* the line alone, on the path's last element */
        if (L->len > 0) mv(b, 0, ex + (L->perp ? -ty * sg : tx) * L->len, ey + (L->perp ? tx * sg : ty) * L->len, z + L->dzl, lf);
        return;
    }
    double r = L->r, nx = -ty * sg, ny = tx * sg, sw = sg * L->sw;
    double cx = ex + nx * r, cy = ey + ny * r;
    double a0 = atan2(ey - cy, ex - cx), a1 = a0 + sw, zx = z + L->dza;
    int k = lead_steps(r, sw, p->tol), k0 = b->n; for (int j = 1; j <= k; j++) mv(b, 0, cx + r * cos(a0 + sw * j / k), cy + r * sin(a0 + sw * j / k), z + (zx - z) * j / k, lf);
    ps_arc_rec(b, k0, b->n - 1, cx, cy, sw < 0);               /* Smoothing: one arc record */
    if (L->len > 0) { V3 e = lastp(b);
        if (L->perp) mv(b, 0, e.x - cos(a1) * sg * sg * L->len, e.y - sin(a1) * sg * sg * L->len, zx + L->dzl, lf);   /* sg x leftnormal(u), u = (-sin a1, cos a1) sg */
        else mv(b, 0, e.x - sin(a1) * sg * L->len, e.y + cos(a1) * sg * L->len, zx + L->dzl, lf); }
}
/* the entry's first point and its height above the cut (the plunge goes there) */
static void lead_start(const CamParams *p, const LeadS *L, double sx, double sy, double tx, double ty, double *ox, double *oy, double *dz) {
    double sg = p->lead_right ? -1 : 1;
    if (dz) *dz = 0;
    if (L->r <= 0) {
        if (L->len > 0) { *ox = sx + (L->perp ? -ty * sg : -tx) * L->len; *oy = sy + (L->perp ? tx * sg : -ty) * L->len; if (dz) *dz = L->dzl; }
        else { *ox = sx; *oy = sy; }
        return;
    }
    double cx, cy, a0; lead_arc_start(p, L, sx, sy, tx, ty, &cx, &cy, &a0);
    if (dz) *dz = L->dza + (L->len > 0 ? L->dzl : 0);
    if (L->perp && L->len > 0) {
        double ax = cx + L->r * cos(a0), ay = cy + L->r * sin(a0), ux = -sin(a0) * sg, uy = cos(a0) * sg;
        *ox = ax - uy * sg * L->len; *oy = ay + ux * sg * L->len; return;
    }
    *ox = cx + L->r * cos(a0) + sin(a0) * sg * L->len; *oy = cy + L->r * sin(a0) - cos(a0) * sg * L->len;
}

/* cut along a path at depth z; tabs (closed paths) raise the tool inside tab intervals. The tab profile (FUN_180056be0,
   decomp/mcmill_ctour.c l.8960-9640): each tab spans its width + the tool diameter (tab +0x38 + op +0x2e8) along the path; a
   partial tab's top = depth + break through + thickness (ctour_depth + dStack_12ac0 + tab +0x40); Vertical moves (record
   0x139c = 0) go straight up at the span's start and down at its end (gr_lin_mill at the tab point); Ramp moves (0x139c = 1):
   the writer's ramp branch (cut_path_ramp_tabs) */
static double tab_run(const CamParams *p, double dz) {   /* ours: the 45 deg run that decides whether a loop is long enough */
    if (!p->tab_ramp) return 0;                           /* for tabs (cut_path's per > 2 H + 2 run, kept from before the ramp port) */
    double deg = round(p->tab_ang * 180 / M_PI * 1e4) / 1e4;
    if (!(deg > 0.01 && deg < 89.99)) return dz;
    return fabs(deg - 45) < 1e-9 ? dz : dz / tan(deg * M_PI / 180);
}
/* Full tabs (record 0x1398 on; formats/contour_100_plan.md 2): the tab writer FUN_180056be0 (decomp/mcmill_ctour.c
   l.8979-9165; the arguments of its calls read from the disassembly, mcmill.dll 0x180059a40-0x180059b51 and
   0x18005ab70-0x18005ad3e) with Vertical moves (tab +0x4c = record 0x139c = 0; Ramp leaves for the writer's ramp branch,
   cut_path_ramp_tabs). At the tab's start the tool rises: "Use feed plane for full height tabs" (0x13b9) ->
   to the feed plane (init_depth) at the cutting feed, or a rapid with rapid retract (op +0x141 = record 0x1a9); else mccore
   retract_move (retract_up up to tab_rise_z). At the tab's end: without leads (op +0x888 off, or entry +0x890 and exit
   +0x8d8 off) a rapid at the raised height to the end point, then down to the cut depth (gr_lin_mill, the cut's motion
   type); with them FUN_18005ab70: the contour lead application FUN_180051990 on the two-record path from the tab's end
   (param_8 / param_10 = 1: the entry always, also with "first depth cut only"), a rapid at the raised height to the
   entry's start, down to the cut depth, the entry's lines / arcs AT the cut depth (its ramp heights are not used), onto
   the tab's end point; no entry piece -> the rapid to the end point. Full tabs apply at every depth (the writer's test:
   tab +0x48 set, l.8981). The span is the partial tabs' (checked against Mastercam's NCI: BRACKET4 op 15, NAKTO op 5).
   Unverified: no stored op with Full tabs exists in the part corpora (scan 2026-11-04). */
static void path_at(const CPath *c, int closed, double sv, double *x, double *y, double *tx, double *ty) {
    int n = closed ? c->n + 1 : c->n; double s = 0;
    for (int i = 1; i < n; i++) {
        double x0 = c->xy[2 * (i - 1)], y0 = c->xy[2 * (i - 1) + 1], x1 = c->xy[2 * (i % c->n)], y1 = c->xy[2 * (i % c->n) + 1], L = hypot(x1 - x0, y1 - y0);
        if (L <= 0) continue;
        if (sv <= s + L || i == n - 1) { double u = (sv - s) / L; u = u < 0 ? 0 : u > 1 ? 1 : u; *x = x0 + (x1 - x0) * u; *y = y0 + (y1 - y0) * u; *tx = (x1 - x0) / L; *ty = (y1 - y0) / L; return; }
        s += L;
    }
    *x = c->xy[0]; *y = c->xy[1]; *tx = 1; *ty = 0;
}
static void retract_up(MB *b, const CamParams *p, double zr);
static _Thread_local int g_tab_why;   /* why cut_path refused (-5): 5 a span over the path's start / end, 6 / 7 the ramp tabs (cut_path_ramp_tabs) */
static void mv_nd(MB *b, int rapid, double x, double y, double z, double feed) {   /* a move unless it repeats the last point */
    if (b->n) { V3 e = lastp(b); if (fabs(e.x - x) < 1e-12 && fabs(e.y - y) < 1e-12 && fabs(e.z - z) < 1e-12) return; }
    mv(b, rapid, x, y, z, feed);
}
static int cut_path_full_tabs(MB *b, const CamParams *p, const CPath *c, int closed, double z, const double *tc, int nt, double H, double per) {
    double sp[2 * CAM_TAB_MAX]; int ns = 0;
    for (int t = 0; t < nt; t++) { if (tc[t] - H < 0 || tc[t] + H > per) { g_tab_why = 5; return -5; }   /* a span over the path's start: NOT DECODED (refused) */
        sp[2 * ns] = tc[t] - H; sp[2 * ns + 1] = tc[t] + H; ns++; }
    for (int i = 1; i < ns; i++) for (int j = i; j > 0 && sp[2 * j] < sp[2 * j - 2]; j--) { double a = sp[2 * j], bb = sp[2 * j + 1]; sp[2 * j] = sp[2 * j - 2]; sp[2 * j + 1] = sp[2 * j - 1]; sp[2 * j - 2] = a; sp[2 * j - 1] = bb; }
    LeadS qi, qo; lead_sides(p, &qi, &qo); qi.dzl = qi.dza = 0;
    int leads = p->lead_on && (p->lead_in_on || p->lead_out_on);
    int n = closed ? c->n + 1 : c->n, k = 0; double s = 0;
    for (int i = 1; i < n; i++) {
        double x0 = c->xy[2 * (i - 1)], y0 = c->xy[2 * (i - 1) + 1], x1 = c->xy[2 * (i % c->n)], y1 = c->xy[2 * (i % c->n) + 1], L = hypot(x1 - x0, y1 - y0);
        while (k < ns && sp[2 * k] < s + L - 1e-9) {
            double ax, ay, bx, by, tx, ty, u = L > 0 ? (sp[2 * k] - s) / L : 0;
            ax = x0 + (x1 - x0) * u; ay = y0 + (y1 - y0) * u;
            mv_nd(b, 0, ax, ay, z, p->feed);
            double raised;
            if (p->tab_fplane) { raised = p->top_z + p->feed_plane; mv_nd(b, p->feed_retract ? 0 : 1, ax, ay, raised, p->feed_retract ? p->feed : 0); }
            else { retract_up(b, p, p->tab_rise_z); raised = lastp(b).z; }
            path_at(c, closed, sp[2 * k + 1], &bx, &by, &tx, &ty);
            if (leads && lead_has(&qi)) {
                double lx, ly; lead_start(p, &qi, bx, by, tx, ty, &lx, &ly, NULL);
                mv_nd(b, 1, lx, ly, raised, 0); mv_nd(b, 0, lx, ly, z, p->feed);
                int m0 = b->n; lead_in(b, p, &qi, bx, by, tx, ty, z);
                if (b->n > m0) { V3 f = b->m[m0].p; if (fabs(f.x - lx) < 1e-12 && fabs(f.y - ly) < 1e-12 && fabs(f.z - z) < 1e-12) { memmove(&b->m[m0], &b->m[m0 + 1], (b->n - m0 - 1) * sizeof(Move)); b->n--; } }
            } else { mv_nd(b, 1, bx, by, raised, 0); mv_nd(b, 0, bx, by, z, p->feed); }
            /* continue from the tab's end: skip the vertices inside the span */
            while (i < n && sp[2 * k + 1] > s + L + 1e-9) { s += L; i++; if (i >= n) break; x0 = c->xy[2 * (i - 1)]; y0 = c->xy[2 * (i - 1) + 1]; x1 = c->xy[2 * (i % c->n)]; y1 = c->xy[2 * (i % c->n) + 1]; L = hypot(x1 - x0, y1 - y0); }
            k++;
            if (i >= n) return 0;
        }
        mv_nd(b, 0, x1, y1, z, p->feed);
        s += L;
    }
    return 0;
}
/* Ramp tab moves (record 0x139c = 1; formats/contour_100_plan.md 2): the ramp branch of the tab writer FUN_180056be0
   (decomp/mcmill_ctour.c l.9256-9640; the comparisons read from the disassembly, mcmill.dll 0x180058d81-0x18005a5ad). Per tab,
   on the ctour entity holding its start (t0) and on the entities after it while the tab carries over an entity end (t0 = 0
   there, the distance walked from the tab's start kept):
   - R (dStack_12b60): Partial = break through + thickness (from the final depth ctour_depth, not the pass's); Full to the
     retract / clearance height H (bVar17 = Full, feed plane option 0x13b9 off, 0x189 or 0x179 on) = H - z; other Full =
     the feed plane - z; run = R / tan(ramp angle 0x13b0); span = width + D (tab +0x38 + op +0x2e8);
   - rise: the cut (the entity's line / arc, feed) to the run's end at the top R + ctour_depth (bVar17: a run to the feed
     plane at the feed plane), or to the span's middle at z + walked x R / run when that comes first (the peak, bVar21);
   - bVar17: then a straight rapid on to the CURRENT t + run (the peak / the top H - z + ctour_depth);
   - the top: to span - run at R + ctour_depth (bVar17: a straight rapid), skipped after a peak;
   - bVar17: a straight rapid down to span - (feed plane run) at the feed plane (after a peak only when above the feed plane);
   - descent: the cut to the span's end at z.
   A move that reaches the entity's end ends there: at R + ctour_depth - (walked - (span - run)) x R / run on the way down, at
   z + walked x R / run on the way up, and the next entity goes on. Those carried heights lie on the moves' own lines only
   at the final depth (and not on bVar17's rapid rise at all); the offset path here does not keep the ctour entities, so the
   profile is worked out twice - every vertex an entity end, and none - and refused where the two differ. Also refused (ours):
   a span over the path's start / end, overlapping spans, a tab ending exactly where it carried over (the writer's LAB_180059a04
   loop), a ramp angle outside (0, 90). */
typedef struct { double s, z; int rapid; } TKnot;
typedef struct { int b17; double R, run, runfp, span, top, init, z, tol; } TRampP;
typedef struct { int b21, b20; double w; TKnot *k; int nk, cap, bad; } TRampS;
static void tr_emit(TRampS *S, double s, double z, int rapid) {
    if (S->nk >= S->cap) { S->bad = 1; return; }
    S->k[S->nk].s = s; S->k[S->nk].z = z; S->k[S->nk].rapid = rapid; S->nk++;
}
/* one entity [es, es + L]; carry = the tab came over from the last entity; returns 1 = carries on, 0 = done (*tend = t) */
static int tr_ent(const TRampP *P, TRampS *S, double es, double L, double t0in, int carry, double *tend) {
    const double tol = P->tol, z = P->z, R = P->R, run = P->run, F = P->top - R, d7 = L, d10 = P->span;
    const int b16 = carry, b17 = P->b17;
    double t0 = carry ? 0 : t0in, d6 = t0;
    double d9 = d10 / d7, d54 = d9 * 0.5, a58 = b17 ? P->runfp : 0, a38 = a58 / d7, d52 = d10 * 0.5, d2, d51;
    if (carry) { d54 = (d52 - S->w) / d7; d2 = (run - S->w) / d7; d9 = (d10 - S->w) / d7; d51 = b17 ? (a58 - S->w) / d7 : 0; }
    else { S->w = 0; d2 = run / d7; d51 = a38; }
    double d3 = P->init;
    if (!b17) { d3 = R + F; d51 = d2; }
    if (carry) {
        if (!b17) { if (S->w < run - tol && S->w < d52 - tol) goto rise; goto top; }
        if (S->w < a58 - tol) goto rise;
        goto rapid_rise;
    }
rise:                                                         /* LAB_180058f8b: skipped in a tab (bVar20) */
    if (!S->b21 && !S->b20) {
        double tn, h = d3; int b11 = 0;
        if (d54 <= fabs(t0 - 1.0)) {
            double re = d51 + t0, mid = d54 + t0;
            if (mid <= re) { S->b21 = 1; S->w += fabs(mid - t0) * d7; h = S->w * R / run + z; tn = mid; }
            else { S->w += d51 * d7; tn = re; }
        } else {
            tn = d51 + t0;
            if (tn < 1.0 - tol) S->w += d51 * d7;
            else { S->w += fabs(t0 - 1.0) * d7; tn = 1.0; if (tol + 1.0 < d51 + 1.0) { b11 = 1; h = S->w * R / run + z; } }
        }
        tr_emit(S, es + tn * L, h, 0);
        if (b11) return 1;
        d6 = tn;
    }
    if (!b17) goto top;
rapid_rise:                                                   /* LAB_1800591c6 */
    if (b16) { if (run - tol <= S->w) goto top; if (d52 - tol <= S->w) goto top; }
    if (!S->b21) {
        double mid = d54 + t0, re = d6 + d2, tn, h = R + F; int cross = 0, peak = 0;
        if (mid > re) { if (tol + 1.0 > re) { S->w += d7 * d2; tn = re; } else cross = 1; }
        else if (tol + 1.0 > re || 1.0 > mid) peak = 1;
        else cross = 1;
        if (peak) { S->b21 = 1; tn = mid; S->w += fabs(mid - d6) * d7; h = S->w * R / run + z; }
        if (cross) { tn = 1.0; S->w += fabs(d6 - 1.0) * d7; h = S->w * R / run + z; }
        tr_emit(S, es + tn * L, h, 1);
        if (cross) return 1;
        d6 = tn;
    }
top:                                                          /* LAB_180059386 */
    S->b20 = 0;
    if (!((b16 && (d10 - run) - tol <= S->w) || S->b21)) {
        double ds = (d9 + t0) - run / d7, tn; int b11 = 0;
        if (1.0 <= ds) { S->w += fabs(d6 - 1.0) * d7; tn = 1.0; if (tol + 1.0 < ds) b11 = 1; }
        else { S->w += fabs(d6 - ds) * d7; tn = ds; }
        tr_emit(S, es + tn * L, R + F, b17);
        if (b11) return 1;
        d6 = tn;
    }
    if (b17 && (!S->b21 || (S->nk && S->k[S->nk - 1].z > P->init + tol)) && (!b16 || S->w < (d10 - a58) - tol)) {
        double dd = (d9 + t0) - a38, tn, h = P->init; int b19 = 0;
        if (1.0 <= dd) { S->w += fabs(d6 - 1.0) * d7; tn = 1.0; if (tol + 1.0 < dd) { b19 = 1; h = (R + F) - ((S->w - (d10 - run)) * R) / run; } }
        else { S->w += fabs(d6 - dd) * d7; tn = dd; }
        tr_emit(S, es + tn * L, h, 1);
        if (b19) return 1;
        d6 = tn;
    }
    if (b16 && d10 - tol <= S->w) { S->bad = 1; return 0; }  /* LAB_180059a04: the writer carries on with this tab (ours: refused) */
    {
        double te = d9 + t0, tn, h = z; int b19 = 0;
        if (1.0 <= te) { tn = 1.0; S->w += fabs(d6 - 1.0) * d7; if (tol + 1.0 < te) { b19 = 1; h = (R + F) - ((S->w - (d10 - run)) * R) / run; } }
        else { S->w += fabs(d6 - te) * d7; tn = te; }
        tr_emit(S, es + tn * L, h, 0);
        S->b21 = 0;
        if (b19) return 1;
        *tend = tn; return 0;
    }
}
/* the knots of all tabs over entities ending at ends[0..ne-1] (ends[ne-1] = the path's length); 0, -1 refused */
/* A tab over the path's start (2026-11-04, user parts buckle-v6..v8 / v11 / v12 op 16 / 19): Mastercam's NCI cuts it as two
   pieces - the tab rising on the path's last entities and carried over its end (the writer's entity loop simply ends: the path
   finishes at the carried height), and the pass starting inside the tab: the writer's record-1 case (decomp/mcmill_ctour.c
   l.9786-9817: the first tab on entity 2 with t < tol -> bVar20; a Partial tab: record 1 at ctour_depth + break through +
   thickness, i.e. the plunge stops at the tab's top) then the entity loop with bVar20 - no rise (l.9398 / l.9571), the top to
   span - run, the descent to the span's end; its span is what is left of the tab after the start (NCI: rise 0.98, top 4.0,
   descent 0.98 for the 6.0 span of buckle-v6 op 16). bspan: that span (0: none); the last tab may carry over the end. */
static int tr_profile(const TRampP *P, const double *ends, int ne, const double *sp, int ns, TKnot *k, int cap, int *nk, double bspan) {
    TRampS S = { 0, 0, 0, k, 0, cap, 0 };
    int e = 0; double pos = 0;                                /* where the cut stands (s) */
    if (bspan > 0) {                                          /* the pass starts in a tab */
        TRampP Q = *P; Q.span = bspan;
        if (bspan < P->run + P->tol) return -1;               /* ours: the top would run backwards (not in the user parts) */
        double L = ends[0]; if (L <= 0) return -1;
        S.b20 = 1; tr_emit(&S, 0, P->top, 0);
        double tend = 0; int carry = tr_ent(&Q, &S, 0, L, 0, 0, &tend);
        while (carry && !S.bad) {
            if (++e >= ne) return -1;
            double es = ends[e - 1]; L = ends[e] - es;
            if (L <= 0) return -1;
            carry = tr_ent(&Q, &S, es, L, 0, 1, &tend);
        }
        if (S.bad) return -1;
        pos = (e ? ends[e - 1] : 0) + tend * (ends[e] - (e ? ends[e - 1] : 0));
        e = 0;
    }
    for (int t = 0; t < ns; t++) {
        double s0 = sp[t];
        if (s0 < pos - 1e-9) return -1;                       /* overlapping spans (ours: refused) */
        while (e < ne - 1 && ends[e] <= s0) e++;
        double es = e ? ends[e - 1] : 0, L = ends[e] - es;
        if (L <= 0) return -1;
        tr_emit(&S, s0, P->z, 0);                             /* the cut to the tab's start */
        double tend = 0; int carry = tr_ent(P, &S, es, L, (s0 - es) / L, 0, &tend);
        while (carry && !S.bad) {
            if (++e >= ne) { if (t == ns - 1 && bspan > 0) { carry = 0; tend = 1; e = ne - 1; break; } return -1; }   /* over the path's end: the wrapping tab stops there */
            es = ends[e - 1]; L = ends[e] - es;
            if (L <= 0) return -1;
            carry = tr_ent(P, &S, es, L, 0, 1, &tend);
        }
        if (S.bad) return -1;
        pos = es + tend * L;
    }
    *nk = S.nk; return 0;
}
static double tr_zat(const TKnot *k, int nk, double s, double z, int *rapid) {   /* height of the profile at s (the cut z outside) */
    *rapid = 0;
    for (int i = 0; i + 1 < nk; i++) {
        if (s < k[i].s - 1e-9 || s > k[i + 1].s + 1e-9) continue;
        if (k[i + 1].s - k[i].s < 1e-12) continue;
        if (s > k[i].s + 1e-9 && s < k[i + 1].s - 1e-9) *rapid = k[i + 1].rapid;
        return k[i].z + (k[i + 1].z - k[i].z) * (s - k[i].s) / (k[i + 1].s - k[i].s);
    }
    return z;
}
/* the chain whose passes are being cut (gen_contour): a tab belongs to the chain it lies on - mccore tabs_from_chain takes the
   op's tab records of that chain only (decomp/mccore_tabs.c l.1014-1110: TP kind 30, chain number +0x98); the points here are on
   their chain entity (cam_fileop.c / ct_square_tabs), so a point off this chain by more than the polyline's chord error is
   another chain's (buckle-v10 op 13: a tab of the next chain 2.9 mm from this path was taken as this one's) */
static _Thread_local const double *g_tch_xy; static _Thread_local int g_tch_n;
static _Thread_local const Chain *pass_chain = NULL;     /* (tentative; below) */
static void ent_pt_t(const Ent *e, double u, double *x, double *y);
/* the ctour's entity ends on the offset path (2026-11-04): the writer carries a tab over the ends of the ctour's entities - the
   offsets of the chain's entities, with the corner arcs between them (FUN_180051990's ccomp) - while this path is a polyline
   (arcs as chords). A vertex is an entity end when its nearest point on the chain is an entity's end (an offset corner, rolled
   or not) or it lies as near two chain entities (the corner of a concave offset); a chord vertex of an offset arc is nearest
   to the inside of one chain arc. Returns the count, -1 without the chain. */
static int rt_chain_ends(const CPath *c, int closed, const double *sv, int nv, double *out) {
    const Chain *ch = pass_chain; const McamFile *f = ofile;
    if (!ch || !f || ch->n < 1) return -1;
    int n = closed ? c->n + 1 : c->n, k = 0, iv = 0;
    for (int i = 1; i < n && iv < nv; i++) {
        double x1 = c->xy[2 * (i % c->n)], y1 = c->xy[2 * (i % c->n) + 1];
        if (hypot(x1 - c->xy[2 * (i - 1)], y1 - c->xy[2 * (i - 1) + 1]) <= 0) continue;
        double d1 = 1e300, d2 = 1e300; int e1 = -1, at1 = 0, on1 = 0, on2 = 0;
        for (int j = 0; j < ch->n; j++) {
            const Ent *e = &f->ents[ch->ent[j]]; double u, len;
            if (e->type == E_LINE) { double ex = e->b.x - e->a.x, ey = e->b.y - e->a.y, L2 = ex * ex + ey * ey; u = L2 > 0 ? ((x1 - e->a.x) * ex + (y1 - e->a.y) * ey) / L2 : 0; len = sqrt(L2); }
            else if (e->type == E_ARC) { double a = atan2(y1 - e->c.y, x1 - e->c.x) - e->a0; a = fmod(a, 2 * M_PI); if (e->sw > 0 && a < 0) a += 2 * M_PI; if (e->sw < 0 && a > 0) a -= 2 * M_PI;
                u = e->sw != 0 ? a / e->sw : 0; if (u > 1 && e->sw != 0) { double a2 = a - (e->sw > 0 ? 2 * M_PI : -2 * M_PI); if (fabs(a2 / e->sw) < u - 1) u = a2 / e->sw; } len = e->r * fabs(e->sw); }
            else return -1;                                    /* ours: splines / points not handled */
            double ue = 1e-4 / (len > 1e-9 ? len : 1e-9);
            int at = fabs(u) <= ue || fabs(u - 1) <= ue, on = u >= -ue && u <= 1 + ue;   /* at an end / on the entity's perpendicular */
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            double x, y; ent_pt_t(e, u, &x, &y); double d = hypot(x - x1, y - y1);
            if (d < d1 - 1e-12) { d2 = d1; on2 = on1; d1 = d; e1 = j; at1 = at; on1 = on; } else if (d < d2) { d2 = d; on2 = on; }
        }
        if (e1 < 0) return -1;
        /* rolled corner: the arc's ends lie on the entities' end normals (at), its chord vertices beyond both (neither) */
        if (at1 || (on1 && on2 && d2 - d1 < 1e-4) || iv == nv - 1) out[k++] = sv[iv];
        iv++;
    }
    return k;
}
static int cut_path_ramp_tabs(MB *b, const CamParams *p, const CPath *c, int closed, double z, const double *tc, int nt, double H, double per) {
    double ang = p->tab_ang;
    if (!(ang > 0 && ang < M_PI / 2)) { g_tab_why = 6; return -5; }   /* open chains too: the writer walks the ctour's entities (buckle-v12 op 13 chain 1, NCI) */
    TRampP P; memset(&P, 0, sizeof P);
    P.tol = CAM_SYS_TOL; P.z = z; P.span = 2 * H; P.init = p->top_z + p->feed_plane;
    double F = p->final_z;                                    /* ctour_depth: the chain's final depth */
    P.b17 = p->tab_full && !p->tab_fplane && p->tab_rise_rc;
    P.R = !p->tab_full ? p->bt_eff + p->tab_h : P.b17 ? p->tab_rise_z - z : P.init - z;
    P.run = P.R / tan(ang); P.runfp = P.b17 ? (P.init - z) / tan(ang) : 0; P.top = P.R + F;
    if (!p->tab_full && !(z + P.tol < F + p->bt_eff + p->tab_h)) nt = 0;   /* a partial tab under the cut: none (l.8981) */
    if (nt && (P.R <= 0 || P.run <= 0)) { g_tab_why = 6; return -5; }
    double sp[CAM_TAB_MAX], bspan = 0; int ns = 0, nwrap = 0;
    for (int t = 0; t < nt; t++) {
        double s0 = tc[t] - H, s1 = tc[t] + H;
        if (s0 < 0 || s1 > per) {                            /* over the start: two pieces (tr_profile) */
            if (p->tab_full || ++nwrap > 1 || !closed) { g_tab_why = 5; return -5; }   /* ours: an open chain's end - not seen */   /* Full: the writer's other branch (dStack_12af0) not ported */
            if (s0 < 0) { bspan = s1; s0 += per; } else bspan = s1 - per;
            /* the split is made on the chain: the start piece's span = H + the tab point's signed distance along the chain from its
               start (NCI: buckle-v6 op 16 4.94 / 4.96 - the start on a line, both lengths alike; buckle-v11 op 19 2.96 / 2.96 where
               the offset arc before the start is longer than the chain's, 1.78 with offset lengths) */
            if (p->ntab_xy > 0 && g_tch_xy && g_tch_n >= 2) {
                double px, py, tx, ty, best = 1e300; int qi = -1; path_at(c, closed, tc[t], &px, &py, &tx, &ty);
                for (int q = 0; q < p->ntab_xy; q++) { double d = hypot(p->tab_xy[2 * q] - px, p->tab_xy[2 * q + 1] - py); if (d < best) { best = d; qi = q; } }
                double qx = p->tab_xy[2 * qi], qy = p->tab_xy[2 * qi + 1], bd = 1e300, bs = 0, s0c = 0;
                for (int i = 1; i <= g_tch_n; i++) {
                    double x0 = g_tch_xy[2 * (i - 1)], y0 = g_tch_xy[2 * (i - 1) + 1], ex = g_tch_xy[2 * (i % g_tch_n)] - x0, ey = g_tch_xy[2 * (i % g_tch_n) + 1] - y0;
                    double L = hypot(ex, ey), u = L > 0 ? ((qx - x0) * ex + (qy - y0) * ey) / (L * L) : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
                    double d = hypot(x0 + ex * u - qx, y0 + ey * u - qy);
                    if (d < bd) { bd = d; bs = s0c + u * L; }
                    s0c += L;
                }
                double sc = bs > s0c / 2 ? bs - s0c : bs;
                bspan = H + sc;
            }
        }
        sp[ns++] = s0;
    }
    for (int i = 1; i < ns; i++) for (int j = i; j > 0 && sp[j] < sp[j - 1]; j--) { double a = sp[j]; sp[j] = sp[j - 1]; sp[j - 1] = a; }
    if (getenv("MCAM_TAB_DEBUG")) { fprintf(stderr, "ramp tab starts (path at %g,%g, move %d) z %g per %g span %g run %g bspan %g:", c->xy[0], c->xy[1], b->n, z, per, 2 * H, P.run, bspan); for (int i = 0; i < ns; i++) fprintf(stderr, " %g", sp[i]); fprintf(stderr, "\n"); }   /* ours (tests) */
    if (bspan > 0) {                                         /* the plunge stops at the tab's top: the pass's record 1 (no lead before it) */
        V3 e0 = b->n ? lastp(b) : (V3){ 0, 0, 0 };
        if (!b->n || b->m[b->n - 1].rapid || hypot(e0.x - c->xy[0], e0.y - c->xy[1]) > 1e-9 || fabs(e0.z - z) > 1e-9) { g_tab_why = 5; return -5; }
        if (b->n >= 2 && hypot(b->m[b->n - 2].p.x - e0.x, b->m[b->n - 2].p.y - e0.y) > 1e-9) { g_tab_why = 5; return -5; }   /* a lead / link ends here */
    }
    int n = closed ? c->n + 1 : c->n, nv = 0;
    double *ends = malloc((size_t)n * sizeof(double)), s = 0;
    for (int i = 1; i < n; i++) { double L = hypot(c->xy[2 * (i % c->n)] - c->xy[2 * (i - 1)], c->xy[2 * (i % c->n) + 1] - c->xy[2 * (i - 1) + 1]);
        if (L <= 0) continue; s += L; ends[nv++] = s; }
    if (!nv) { free(ends); return 0; }
    int cap = 16 * (ns + 1) + 4 * n;
    TKnot *ka = malloc((size_t)cap * sizeof(TKnot)), *kb = malloc((size_t)cap * sizeof(TKnot)); int na = 0, nb = 0;
    double one = per;                                         /* run (b): the whole path one entity */
    double *ee = malloc((size_t)(nv > 0 ? nv : 1) * sizeof(double)); int ne = rt_chain_ends(c, closed, ends, nv, ee);
    if (ne >= 1 && fabs(ee[ne - 1] - per) < 1e-9) {           /* the ctour's entity ends known: one run on them */
        int r = tr_profile(&P, ee, ne, sp, ns, kb, cap, &nb, bspan);
        free(ee); free(ka);
        if (r) { free(kb); free(ends); g_tab_why = 6; return -5; }
        goto emit;
    }
    free(ee);
    int ra = tr_profile(&P, ends, nv, sp, ns, ka, cap, &na, bspan), rb = tr_profile(&P, &one, 1, sp, ns, kb, cap, &nb, bspan);
    int ok = ra == 0 && rb == 0;
    /* the two must agree: heights at every knot and vertex, and the kind of move between them; a straight rapid of (b) must
       run along a straight piece of the path */
    for (int pass_ = 0; ok && pass_ < 3; pass_++) {
        int m = pass_ == 0 ? na : pass_ == 1 ? nb : nv;
        for (int i = 0; ok && i < m; i++) {
            double sv = pass_ == 0 ? ka[i].s : pass_ == 1 ? kb[i].s : ends[i]; int r1, r2;
            if (fabs(tr_zat(ka, na, sv, z, &r1) - tr_zat(kb, nb, sv, z, &r2)) > 1e-6) ok = 0;
            if (i + 1 < m) { double sm = 0.5 * (sv + (pass_ == 0 ? ka[i + 1].s : pass_ == 1 ? kb[i + 1].s : ends[i + 1]));
                tr_zat(ka, na, sm, z, &r1); tr_zat(kb, nb, sm, z, &r2); if (r1 != r2) ok = 0; }
        }
    }
    for (int i = 1; ok && i < nb; i++) if (kb[i].rapid)
        for (int v = 0; v < nv; v++) if (ends[v] > kb[i - 1].s + 1e-9 && ends[v] < kb[i].s - 1e-9) {
            double ax, ay, bx, by, vx, vy, tx, ty; path_at(c, closed, kb[i - 1].s, &ax, &ay, &tx, &ty); path_at(c, closed, kb[i].s, &bx, &by, &tx, &ty);
            path_at(c, closed, ends[v], &vx, &vy, &tx, &ty);
            double lx = bx - ax, ly = by - ay, ll = hypot(lx, ly);
            if (ll <= 0 || fabs((vx - ax) * ly - (vy - ay) * lx) / ll > 1e-9) { ok = 0; break; }
        }
    free(ka);
    if (!ok) { free(kb); free(ends); g_tab_why = ra == 0 && rb == 0 ? 7 : 6; return -5; }
    /* the moves: along the path, vertices between knots at the profile's height; a rapid straight to its knot */
emit:;
    int k = 0; double sprev = 0;
    if (bspan > 0) { b->m[b->n - 1].p.z = P.top; k = 1; }    /* record 1 at the tab's top (l.9811) */
    if (getenv("MCAM_TAB_DEBUG")) { fprintf(stderr, "ramp tabs z %g per %g bspan %g:", z, per, bspan); for (int i = 0; i < nb; i++) fprintf(stderr, " (%g %g%s)", kb[i].s, kb[i].z, kb[i].rapid ? " r" : ""); fprintf(stderr, "\n"); }   /* ours (tests) */
    for (int i = 1; i < n; i++) {
        double x0 = c->xy[2 * (i - 1)], y0 = c->xy[2 * (i - 1) + 1], x1 = c->xy[2 * (i % c->n)], y1 = c->xy[2 * (i % c->n) + 1], L = hypot(x1 - x0, y1 - y0);
        if (L <= 0) continue;
        int at_end = 0;
        while (k < nb && kb[k].s <= sprev + L + 1e-9) {
            double u = (kb[k].s - sprev) / L; u = u < 0 ? 0 : u > 1 ? 1 : u;
            mv_nd(b, kb[k].rapid, x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, kb[k].z, kb[k].rapid ? 0 : p->feed);
            if (kb[k].s >= sprev + L - 1e-9) at_end = 1;
            k++;
        }
        if (!at_end) { int r; double hz = tr_zat(kb, nb, sprev + L, z, &r);
            if (!r) mv_nd(b, 0, x1, y1, hz, p->feed); }         /* inside a straight rapid the vertex is on its line */
        sprev += L;
    }
    free(kb); free(ends);
    return 0;
}
/* Tab Cutoff (page 3022: record 0x13f2 = ctour_prm +0x1ca 1 "After all contour chains" / 2 "After each contour chain" / 3
   "Separate operation", 0x13f4 = +0x1cc "Cut tabs with first finish pass"): the tab writer FUN_180056be0 with param_14 = 2
   (bVar50; mcmill.dll 0x18005d859 / 0x18005ed49 pass 2) on the chain's last path at the last depth (local_100 after the passes,
   local_1278; with control compensation, bVar22, the chain itself - refused here). It cuts only the tabs (decomp/mcmill_ctour.c
   l.9081-9120, 9160-9220): per tab, at its start - without leads (op +0x888 off, or entry +0x890 and exit +0x8d8 off) a rapid at
   dStack_12af0 (retract 0x189 -> its height, else clearance 0x179, else the feed plane: tab_rise_z) to the start, else
   FUN_18005ab70 (the entry lead as at a Full tab's end, at the cut depth); down to the depth (the cut's motion, feed); along the
   span at the depth; at its end - without the exit (+0x888 or +0x8d8 off) mccore retract_move, else FUN_18005afc0 (mcmill.dll
   0x18005afc0: the lead application on [the entity at t - 0.06 as a line, the end] with the flags of FUN_18005ab70, the exit
   pieces at the depth, then retract_move from the last; no piece -> no retract). A span carried over entity ends cuts the whole
   entities between (l.9145-9175). Ours: the exit's direction is taken only where the tab ends on the offset of one of the
   chain's lines (the chord from t - 0.06 is that line); elsewhere (an arc: the chord differs from the tangent) refused. */
static int tab_centres(const CamParams *p, const CPath *c, int closed, double per, double H, double *tc);
static double path_len(const CPath *c, int closed);
static _Thread_local const Ent **g_tcl; static _Thread_local int g_tcn; static _Thread_local double g_tcoff;   /* the chain's lines and the last pass's offset */
static int tab_line_dir(double x, double y, double tx, double ty, double *dx, double *dy) {
    for (int i = 0; i < g_tcn; i++) {
        const Ent *e = g_tcl[i]; double ux = e->b.x - e->a.x, uy = e->b.y - e->a.y, L = hypot(ux, uy);
        if (L <= 0) continue;
        ux /= L; uy /= L;
        double u = (x - e->a.x) * ux + (y - e->a.y) * uy, d = fabs((x - e->a.x) * uy - (y - e->a.y) * ux);
        if (u < -1e-9 || u > L + 1e-9 || fabs(d - g_tcoff) > 1e-6) continue;
        double sg = ux * tx + uy * ty >= 0 ? 1 : -1; *dx = ux * sg; *dy = uy * sg; return 1;
    }
    return 0;
}
static int cut_tab_cutoff(MB *b, const CamParams *p, const CPath *c, double z) {
    double per = path_len(c, 1), tab_z = p->final_z + p->bt_eff + p->tab_h, dz = tab_z - z, H = p->tab_w / 2 + p->tool_dia / 2;
    double rl = dz > 0 ? tab_run(p, dz) : 0, tc[CAM_TAB_MAX], sp[2 * CAM_TAB_MAX];
    if (p->tabs <= 0 || !(per > 2 * H + 2 * rl)) return 0;    /* (cut_path's test) */
    int nt = tab_centres(p, c, 1, per, H, tc), ns = 0;
    for (int t = 0; t < nt; t++) { if (tc[t] - H < 0 || tc[t] + H > per) { g_tab_why = 5; return -5; } sp[2 * ns] = tc[t] - H; sp[2 * ns + 1] = tc[t] + H; ns++; }
    for (int i = 1; i < ns; i++) for (int j = i; j > 0 && sp[2 * j] < sp[2 * j - 2]; j--) { double a = sp[2 * j], bb = sp[2 * j + 1]; sp[2 * j] = sp[2 * j - 2]; sp[2 * j + 1] = sp[2 * j - 1]; sp[2 * j - 2] = a; sp[2 * j - 1] = bb; }
    for (int i = 1; i < ns; i++) if (sp[2 * i] < sp[2 * i - 1] - 1e-9) { g_tab_why = 6; return -5; }   /* overlapping (ours) */
    LeadS qi, qo; lead_sides(p, &qi, &qo); qi.dzl = qi.dza = qo.dzl = qo.dza = 0;
    int ent = p->lead_on && (p->lead_in_on || p->lead_out_on), ext = p->lead_on && p->lead_out_on;
    double hz = p->tab_rise_z;
    int n = c->n + 1;
    for (int k = 0; k < ns; k++) {
        double ax, ay, tx, ty; path_at(c, 1, sp[2 * k], &ax, &ay, &tx, &ty);
        if (ent && lead_has(&qi)) {                            /* FUN_18005ab70 */
            double lx, ly; lead_start(p, &qi, ax, ay, tx, ty, &lx, &ly, NULL);
            if (!b->n) mv(b, 1, lx, ly, p->clearance, 0);       /* the 0x82 op's first move (ours: as pass() starts an op) */
            mv_nd(b, 1, lx, ly, hz, 0); mv_nd(b, 0, lx, ly, z, p->feed);
            int m0 = b->n; lead_in(b, p, &qi, ax, ay, tx, ty, z);
            if (b->n > m0) { V3 f0 = b->m[m0].p; if (fabs(f0.x - lx) < 1e-12 && fabs(f0.y - ly) < 1e-12 && fabs(f0.z - z) < 1e-12) { memmove(&b->m[m0], &b->m[m0 + 1], (b->n - m0 - 1) * sizeof(Move)); b->n--; } }
        } else { if (!b->n) mv(b, 1, ax, ay, p->clearance, 0); mv_nd(b, 1, ax, ay, hz, 0); mv_nd(b, 0, ax, ay, z, p->feed); }
        double s = 0;                                          /* along the span at the depth */
        for (int i = 1; i < n; i++) {
            double x0 = c->xy[2 * (i - 1)], y0 = c->xy[2 * (i - 1) + 1], x1 = c->xy[2 * (i % c->n)], y1 = c->xy[2 * (i % c->n) + 1], L = hypot(x1 - x0, y1 - y0);
            if (s + L > sp[2 * k] + 1e-9 && s + L < sp[2 * k + 1] - 1e-9) mv_nd(b, 0, x1, y1, z, p->feed);
            s += L;
        }
        double ex, ey; path_at(c, 1, sp[2 * k + 1], &ex, &ey, &tx, &ty);
        mv_nd(b, 0, ex, ey, z, p->feed);
        if (!ext) { retract_up(b, p, hz); continue; }
        double dx, dy; if (!tab_line_dir(ex, ey, tx, ty, &dx, &dy)) { g_tab_why = 8; return -5; }
        if (lead_has(&qo)) { lead_out(b, p, &qo, ex, ey, dx, dy, z); retract_up(b, p, hz); }   /* FUN_18005afc0 */
    }
    return 0;
}
/* the tab centres on a path (arc length): the tabs that belong to this loop projected onto it */
static int tab_centres(const CamParams *p, const CPath *c, int closed, double per, double H, double *tc) {
    int n = closed ? c->n + 1 : c->n, nt = 0;
    if (p->ntab_xy > 0) {                                       /* given centres: project onto this path */
        for (int t = 0; t < p->ntab_xy && nt < CAM_TAB_MAX; t++) {
            double qx = p->tab_xy[2 * t], qy = p->tab_xy[2 * t + 1], best = 1e300, bs = 0, s0 = 0;
            if (g_tch_xy && g_tch_n >= 2) {                     /* only this chain's tabs */
                double dc = 1e300;
                for (int i = 1; i <= g_tch_n; i++) {
                    double x0 = g_tch_xy[2 * (i - 1)], y0 = g_tch_xy[2 * (i - 1) + 1], ex = g_tch_xy[2 * (i % g_tch_n)] - x0, ey = g_tch_xy[2 * (i % g_tch_n) + 1] - y0;
                    double L2 = ex * ex + ey * ey, u = L2 > 0 ? ((qx - x0) * ex + (qy - y0) * ey) / L2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
                    double d = hypot(x0 + ex * u - qx, y0 + ey * u - qy); if (d < dc) dc = d;
                }
                if (dc > fmax(2 * p->tol, 1e-3)) continue;
            }
            for (int i = 1; i < n; i++) {
                double x0 = c->xy[2 * (i - 1)], y0 = c->xy[2 * (i - 1) + 1], ex = c->xy[2 * (i % c->n)] - x0, ey = c->xy[2 * (i % c->n) + 1] - y0;
                double L = hypot(ex, ey), u = L > 0 ? ((qx - x0) * ex + (qy - y0) * ey) / (L * L) : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
                double d = hypot(x0 + ex * u - qx, y0 + ey * u - qy);
                if (d < best) { best = d; bs = s0 + u * L; }
                s0 += L;
            }
            if (best < H + p->tool_dia) tc[nt++] = bs;          /* only tabs that belong to this loop */
        }
    } else for (int k = 0; k < p->tabs && k < CAM_TAB_MAX; k++) tc[nt++] = (k + 0.5) * per / p->tabs;   /* ours: an op without a record */
    return nt;
}
static double path_len(const CPath *c, int closed) {
    int n = closed ? c->n + 1 : c->n; double per = 0;
    for (int i = 1; i < n; i++) per += hypot(c->xy[2 * (i % c->n)] - c->xy[2 * (i - 1)], c->xy[2 * (i % c->n) + 1] - c->xy[2 * (i - 1) + 1]);
    return per;
}
static int cut_path(MB *b, const CamParams *p, const CPath *c, int closed, double z, int with_tabs) {
    int n = closed ? c->n + 1 : c->n;
    double per = path_len(c, closed);
    double tab_z = p->final_z + p->bt_eff + p->tab_h, dz = tab_z - z, H = p->tab_w / 2 + p->tool_dia / 2;
    double rl = dz > 0 ? tab_run(p, dz) : 0;
    double tc[CAM_TAB_MAX]; int nt = 0;
    if (with_tabs && (closed || p->tab_ramp) && p->tabs > 0 && (dz > 1e-9 || p->tab_full) && per > 2 * H + 2 * rl) nt = tab_centres(p, c, closed, per, H, tc);
    if (nt && p->tab_ramp) return cut_path_ramp_tabs(b, p, c, closed, z, tc, nt, H, per);
    if (nt && p->tab_full) return cut_path_full_tabs(b, p, c, closed, z, tc, nt, H, per);
    /* Vertical moves: the height over the path at arc length sv - dz strictly inside a span (sd < 0 / > 0: the limit from the
       left / right at a span end) */
#define TABZ(sv, sd) ({ double zz_ = z; for (int t_ = 0; t_ < nt; t_++) { double d_ = fabs((sv) - tc[t_]); if (d_ > per / 2) d_ = per - d_; \
                    double e_ = H - d_, h_ = e_ > 1e-9 || (fabs(e_) <= 1e-9 && (((sv) < tc[t_]) == ((sd) > 0))) ? dz : 0; \
                    if (z + h_ > zz_) zz_ = z + h_; } zz_; })
    double s = 0;
    for (int i = 1; i < n; i++) {
        double x0 = c->xy[2 * (i - 1)], y0 = c->xy[2 * (i - 1) + 1], x1 = c->xy[2 * (i % c->n)], y1 = c->xy[2 * (i % c->n) + 1];
        double L = hypot(x1 - x0, y1 - y0);
        if (!nt) { mv(b, 0, x1, y1, z, p->feed); s += L; continue; }
        double cuts[8 * CAM_TAB_MAX + 1]; int nc = 0;                     /* profile breakpoints inside this segment */
        for (int t = 0; t < nt; t++) for (int k = -1; k <= 1; k++) {
            double pts[2] = { tc[t] - H, tc[t] + H };
            for (int j = 0; j < 2; j++) { double v = pts[j] + k * per; if (v > s + 1e-9 && v < s + L - 1e-9 && nc < 8 * CAM_TAB_MAX) cuts[nc++] = v; }
        }
        for (int a2 = 0; a2 < nc; a2++) for (int b2 = a2 + 1; b2 < nc; b2++) if (cuts[b2] < cuts[a2]) { double t = cuts[a2]; cuts[a2] = cuts[b2]; cuts[b2] = t; }
        cuts[nc++] = s + L;
        for (int k = 0; k < nc; k++) {
            if (k > 0 && cuts[k] - cuts[k - 1] < 1e-9) continue;   /* a span end listed twice */
            double u = L > 0 ? (cuts[k] - s) / L : 1, x = x0 + (x1 - x0) * u, y = y0 + (y1 - y0) * u;
            double za = TABZ(cuts[k], -1), zb = TABZ(cuts[k], 1);   /* vertical: arrive at the left height, then up / down */
            if (b->n && fabs(b->m[b->n - 1].p.x - x) < 1e-12 && fabs(b->m[b->n - 1].p.y - y) < 1e-12 && fabs(b->m[b->n - 1].p.z - za) < 1e-12) {}
            else mv(b, 0, x, y, za, p->feed);
            if (fabs(zb - za) > 1e-12) mv(b, 0, x, y, zb, p->feed);
        }
        s += L;
    }
#undef TABZ
    return 0;
}

/* ---- automatic tabs (formats/contour_100_plan.md 2): mcmill bnci_ctour -> mccore add_auto_tabs @18021afe0 -> FUN_180223720
   @180223720 (decomp/mccore_tabs.c), the shape test is_rectangle_in_boundary @180223180 (decomp/mccore_rectbnd.c). */
/* a point at distance d from the traversal start of entity e (creep_along_ent; reversed = from its end) */
static void ent_pt_at(const McamFile *f, const Ent *e, int rev, double d, double L, double *x, double *y) {
    double u = L > 0 ? d / L : 0; if (u < 0) u = 0; if (u > 1) u = 1; if (rev) u = 1 - u;
    if (e->type == E_ARC) { double a = e->a0 + e->sw * u; *x = e->c.x + e->r * cos(a); *y = e->c.y + e->r * sin(a); return; }
    if (e->type == E_SPLINE) {                                   /* ours: along its break points (length_ent's spline: ent_len) */
        double *T, *P; int n = spl_samples(f, e, cam_chain_tol(mcam_is_metric(f)), &T, &P);
        if (n >= 2) {
            double tot = 0, want; for (int i = 1; i < n; i++) tot += hypot(P[3 * i] - P[3 * i - 3], P[3 * i + 1] - P[3 * i - 2]);
            want = u * tot; *x = P[3 * (n - 1)]; *y = P[3 * (n - 1) + 1];
            for (int i = 1; i < n; i++) { double l = hypot(P[3 * i] - P[3 * i - 3], P[3 * i + 1] - P[3 * i - 2]);
                if (want <= l || i == n - 1) { double v = l > 0 ? want / l : 0; if (v > 1) v = 1;
                    *x = P[3 * i - 3] + (P[3 * i] - P[3 * i - 3]) * v; *y = P[3 * i - 2] + (P[3 * i + 1] - P[3 * i - 2]) * v; break; }
                want -= l; }
            free(T); free(P); return;
        }
        free(T); free(P);
    }
    *x = e->a.x + (e->b.x - e->a.x) * u; *y = e->a.y + (e->b.y - e->a.y) * u;
}
/* the crossings of the horizontal line y with a closed polyline, sorted (pock_bound_line_inters, at most 20 = its 0x14;
   more -> none, as its error return) - ours: the boundary is the chain's polyline at tol x 10 (chain_to_pock_bound's) */
static int hline_x(const double *xy, int n, double y, double *xs) {
    int k = 0;
    for (int i = 0; i < n; i++) {
        double x0 = xy[2 * i], y0 = xy[2 * i + 1], x1 = xy[2 * ((i + 1) % n)], y1 = xy[2 * ((i + 1) % n) + 1];
        if ((y0 <= y) == (y1 <= y)) continue;
        if (k >= 20) return 0;
        xs[k++] = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
    }
    for (int a = 1; a < k; a++) for (int b2 = a; b2 > 0 && xs[b2] < xs[b2 - 1]; b2--) { double t = xs[b2]; xs[b2] = xs[b2 - 1]; xs[b2 - 1] = t; }
    return k;
}
/* is_rectangle_in_boundary(bound, X, Y, step 0.01): rotations 0..90 deg by 5; rows every step from the bottom (+ chain_tol);
   a run of rows over which an interval of one start row keeps overlapping intervals at least w wide, over the height h */
static int ct_rect_in_boundary(const double *xy, int n, double X, double Y, double step, double ctol) {
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    for (int i = 0; i < n; i++) for (int k = 0; k < 2; k++) { if (xy[2 * i + k] < lo[k]) lo[k] = xy[2 * i + k]; if (xy[2 * i + k] > hi[k]) hi[k] = xy[2 * i + k]; }
    int fit = 0; double (*rx)[20] = NULL; int *rn = NULL; double *ry = NULL;
    for (double ang = 0; ang <= 90.0 && !fit; ang += 5.0) {
        double c = cos(ang * M_PI / 180), sn = sin(ang * M_PI / 180), h = sn * X + c * Y, w = sn * Y + c * X;
        if (fabs(hi[1] - lo[1]) < h && fabs(hi[0] - lo[0]) < w) break;
        double y0 = lo[1] + ctol; int nr = (int)floor((hi[1] - y0) / step) + 1;
        if (nr < 2) break;
        free(rx); free(rn); free(ry);
        rx = calloc((size_t)nr, sizeof *rx); rn = calloc((size_t)nr, sizeof *rn); ry = calloc((size_t)nr, sizeof *ry);
        if (!rx || !rn || !ry) break;
        int mx = 0; double yy = y0;
        for (int r = 0; r < nr; r++, yy += step) { ry[r] = yy; rn[r] = hline_x(xy, n, yy, rx[r]); if (rn[r] > mx) mx = rn[r]; }
        int need = (int)floor(h / step) + 1;
        for (int pr = 0, k = 1; pr < mx / 2 && !fit; pr++, k += 2)
            for (int s = 0; s < nr && !fit; s++) {
                double wid = 0, hgt = 0;
                if (nr <= s + need) break;
                if (rn[s] < 2 || k >= rn[s]) continue;
                double a = rx[s][k - 1], bb = rx[s][k];
                for (int t = s + 1; t < nr; t++) {
                    if (rn[t] < 2) break;
                    int found = 0;
                    for (int j = 0; j < rn[t] - 1; j += 2) if (rx[t][j + 1] - a > 0 && bb - rx[t][j] > 0) { found = 1; if (rx[t][j] > a) a = rx[t][j]; if (rx[t][j + 1] < bb) bb = rx[t][j + 1]; }
                    if (!found) wid = 0; else { hgt = ry[t] - ry[s]; wid = bb - a; }
                    if (wid <= w || h <= hgt) break;
                }
                if (wid >= w && hgt >= h) fit = 1;
            }
    }
    free(rx); free(rn); free(ry);
    return fit;
}
/* ---- square-point tabs (formats/contour_100_plan.md 2): mccore make_geo_tabs @1802248a0 (decomp/mccore_tabs.c l.301-616; the calls'
   arguments from the disassembly, mccore.dll 0x180224a30-0x180224ea8). For each chain: every point entity of the part (get_ent mask 1) is
   tested by FUN_180222000 (l.3355): the chain's entities in chain order, the point projected on each, the first one where the 3D
   distance is <= chain_tol and t (from the entity's own start) is within [-tol, 1 + tol]. The tab's position then comes from
   FUN_1802256c0 (l.1931-2332) unless "Midpoint" (0x139a = 1): Start (0) moves it forward along the chain by half the tab width, End (2)
   back (param_6 = 1 half, param_7 = Start, param_8 = 0). On the entity: t +- (W / L) / 2, clamped at its end (and done when the half
   width is within tol). Past the end: the rest W - |the end - the point| (the FULL width, as coded) is walked over the next entities
   (wrapping around the chain; an entity of length <= tol ends the walk with the tab at the clamped end), and the tab lands at
   (W / L) / 2 from that entity's near end - the rest is not used for lines / arcs (only for splines, not ported: refused).
   The chain's tab list is replaced only when a point was found (else its stored tabs stay). FUN_18021b3c0 writes the record without
   the point (+0x10 = 0: the zero positions of some stored tabs). Ours: the tab as a centre point (CamParams tab_xy, as every tab). */
static int sq_on_ent(const McamFile *f, const Ent *e, const Ent *pt, double ctol, double tol, double *tu) {
    double px = pt->a.x, py = pt->a.y, pz = pt->a.z, u, qx, qy, qz;
    if (e->type == E_LINE) {
        double ex = e->b.x - e->a.x, ey = e->b.y - e->a.y, L2 = ex * ex + ey * ey; if (L2 <= 0) return 0;
        u = ((px - e->a.x) * ex + (py - e->a.y) * ey) / L2;
        qx = e->a.x + ex * u; qy = e->a.y + ey * u; qz = e->a.z + (e->b.z - e->a.z) * u;
    } else if (e->type == E_ARC && fabs(e->sw) > 1e-12) {
        double a = atan2(py - e->c.y, px - e->c.x) - e->a0, tw = 2 * M_PI; a = fmod(a, tw);
        if (e->sw > 0 && a < 0) a += tw; if (e->sw < 0 && a > 0) a -= tw;
        u = a / e->sw; if (u > 1 + tol) { double u2 = u - tw / fabs(e->sw); if (u2 >= -tol) u = u2; }
        double aa = e->a0 + e->sw * u; qx = e->c.x + e->r * cos(aa); qy = e->c.y + e->r * sin(aa); qz = e->c.z;
    } else return 0;
    (void)f;
    if (sqrt((qx - px) * (qx - px) + (qy - py) * (qy - py) + (qz - pz) * (qz - pz)) > ctol) return 0;
    if (u < -tol || u > 1 + tol) return 0;
    *tu = u; return 1;
}
static void ent_pt_t(const Ent *e, double u, double *x, double *y) {
    if (e->type == E_LINE) { *x = e->a.x + (e->b.x - e->a.x) * u; *y = e->a.y + (e->b.y - e->a.y) * u; }
    else { double aa = e->a0 + e->sw * u; *x = e->c.x + e->r * cos(aa); *y = e->c.y + e->r * sin(aa); }
}
/* FUN_1802256c0 for lines / arcs: returns 0 when a spline is reached (not ported) */
static int sq_shift(const McamFile *f, const Chain *ch, int *idx, double *t, double W, int fwd, double tol) {
    const Ent *e = &f->ents[ch->ent[*idx]]; if (e->type != E_LINE && e->type != E_ARC) return 0;
    double L = ent_len(f, e); if (L <= 0) return 1;
    double d = W / L * 0.5, px, py; ent_pt_t(e, *t, &px, &py);
    int rev = ch->rev[*idx] != 0, up = fwd ? !rev : rev;          /* t grows along the walk */
    double nt = up ? *t + d : *t - d, bnd = up ? 1.0 : 0.0;
    if (up ? nt < bnd - tol : nt > bnd + tol) { *t = nt; return 1; }
    *t = bnd; if (d <= tol) return 1;
    double bx, by; ent_pt_t(e, bnd, &bx, &by);
    double rest = W - hypot(bx - px, by - py);
    for (int g = 0; g < ch->n; g++) {
        *idx = fwd ? (*idx + 1 < ch->n ? *idx + 1 : 0) : (*idx > 0 ? *idx - 1 : ch->n - 1);
        const Ent *n2 = &f->ents[ch->ent[*idx]];
        if (n2->type != E_LINE && n2->type != E_ARC) return 0;
        double L2 = ent_len(f, n2);
        if (!(fabs(L2) > tol)) return 1;                        /* length <= tol: the walk ends (t stays the clamped end) */
        if (rest <= tol + L2) {
            double q = W / L2 * 0.5; int r2 = ch->rev[*idx] != 0;
            *t = fwd ? (r2 ? 1 - q : q) : (r2 ? q : 1 - q);
            return 1;
        }
        rest -= L2;
    }
    return 1;
}
/* the square-point tabs of one chain as centre points; -1 = a spline in the walk (not ported) */
static int ct_square_tabs(const McamFile *f, const CamParams *p, const Chain *ch, double *out, int cap) {
    const McSysTol *st = mc_systol_units(mcam_is_metric(f)); double tol = st->tol, ctol = st->chain_tol;
    int n = 0;
    for (int k = 0; k < f->nents && n < cap; k++) {
        const Ent *pt = &f->ents[k]; if (pt->type != E_POINT) continue;
        for (int j = 0; j < ch->n; j++) {
            double u; if (!sq_on_ent(f, &f->ents[ch->ent[j]], pt, ctol, tol, &u)) continue;
            int idx = j;
            if (p->tab_sqpos != 1 && !sq_shift(f, ch, &idx, &u, p->tab_w, p->tab_sqpos == 0, tol)) return -1;
            ent_pt_t(&f->ents[ch->ent[idx]], u, &out[2 * n], &out[2 * n + 1]); n++;
            break;
        }
    }
    return n;
}
/* the automatic tabs of one chain (add_auto_tabs + FUN_180223720) as points on the chain: returns their count */
static int ct_auto_tabs(const McamFile *f, const CamParams *p, const Chain *ch, double *out, int cap) {
    const McSysTol *st = mc_systol_units(mcam_is_metric(f)); double tol = st->tol, len = 0;
    for (int i = 0; i < ch->n; i++) { const Ent *e = &f->ents[ch->ent[i]]; if (e->type != E_POINT) len += ent_len(f, e); }   /* calc_chain_length */
    int n;
    if (p->tab_bydist) {                                          /* by max distance: length / (width + distance), rounded up */
        double q = p->tab_w + p->tab_maxd > 0 ? len / (p->tab_w + p->tab_maxd) : 0; n = (int)(short)(int)q;
        if (n == 0 || (double)n < q - tol) n++;
    } else n = p->tab_n;                                          /* Number of tabs (op +0x131a) */
    if (!p->tab_all) {                                            /* only shapes an X x Y rectangle does not fit in */
        double *bxy, bz; int bn = cam_chain_polyline(f, ch, tol * 10, &bxy, &bz);
        if (bn < 3) { free(bxy); return 0; }                      /* no boundary (an open chain): no tabs */
        if (bn > 3 && fabs(bxy[0] - bxy[2 * bn - 2]) < 1e-9 && fabs(bxy[1] - bxy[2 * bn - 1]) < 1e-9) bn--;   /* closing point */
        int fits = ct_rect_in_boundary(bxy, bn, p->tab_sx, p->tab_sy, 0.01, st->chain_tol); free(bxy);
        if (fits) return 0;
    }
    double step = n < 1 ? 1e38 : len / n;
    double pos = p->lead_on && p->lead_mid ? 0 : step * 0.5;     /* lead in / out on (op +0x888) and enter at midpoint (+0x928) */
    int k = 0, first = 1; double acc = 0;
    for (int i = 0; i < ch->n && k < cap; i++) {
        const Ent *e = &f->ents[ch->ent[i]]; double L = e->type == E_POINT ? 0 : ent_len(f, e);
        if (first) {
            if (pos < tol + acc && pos < len - tol) { if (e->type != E_POINT) { ent_pt_at(f, e, ch->rev[i], pos, L, &out[2 * k], &out[2 * k + 1]); k++; } pos += step; }
            first = 0;
        }
        double end = acc + L;
        while (pos < tol + end && k < cap) {
            if (len - tol <= pos) break;
            if (e->type != E_POINT) { ent_pt_at(f, e, ch->rev[i], pos - acc, L, &out[2 * k], &out[2 * k + 1]); k++; }
            pos += step;
        }
        acc = end;
    }
    return k;
}

static void path_dirs(const CPath *c, int closed, double *t0x, double *t0y, double *t1x, double *t1y) {
    int n = c->n;
    double ax = c->xy[2] - c->xy[0], ay = c->xy[3] - c->xy[1], l = hypot(ax, ay); *t0x = l ? ax / l : 1; *t0y = l ? ay / l : 0;
    double bx, by;
    if (closed) { bx = c->xy[0] - c->xy[2 * n - 2]; by = c->xy[1] - c->xy[2 * n - 1]; }
    else { bx = c->xy[2 * n - 2] - c->xy[2 * n - 4]; by = c->xy[2 * n - 1] - c->xy[2 * n - 3]; }
    l = hypot(bx, by); *t1x = l ? bx / l : *t0x; *t1y = l ? by / l : *t0y;
}

/* does a lead (in: from its start to (x, y); out: from (x, y)) stay on the free side of a closed tool-centre loop?
   Leads are built on the free side (left, or right with lead_right): inside a CCW loop, outside a CW one, mirrored for
   the right. Mastercam drops or shortens a lead that does not fit (e.g. a lead arc bigger than the hole). */

/* the lead's point at the middle of a (trimmed) piece: the far end of the half piece */
static void lead_far_pt(const CamParams *p, const LeadS *L, double x, double y, double tx, double ty, int out, double *px, double *py) {
    if (out) { MB t = { 0 }; mv(&t, 0, x, y, 0, 0); lead_out(&t, p, L, x, y, tx, ty, 0); V3 e = lastp(&t); *px = e.x; *py = e.y; free(t.m); }
    else lead_start(p, L, x, y, tx, ty, px, py, NULL);
}
static double path_dist(const CPath *c, double x, double y) {
    double bd = 1e300;
    for (int i = 0; i < c->n; i++) { double ax = c->xy[2 * i], ay = c->xy[2 * i + 1], ex = c->xy[2 * ((i + 1) % c->n)] - ax, ey = c->xy[2 * ((i + 1) % c->n) + 1] - ay;
        double l2 = ex * ex + ey * ey, u = l2 > 0 ? ((x - ax) * ex + (y - ay) * ey) / l2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u; double d = hypot(ax + ex * u - x, ay + ey * u - y); if (d < bd) bd = d; }
    return bd;
}
/* a trimmed piece is kept when its middle lies in the machinable area (FUN_1801c3d70 pt_in_machinable_area(path, pt, nc_tol, tol) at
   t 0.5; its internals NOT DECODED - ours: on the free side, or on the path within nc_tol: its nc_tol / tol arguments read as the
   boundary's tolerance - a tangent exit line on the path stays, rt_lead_b34fix) */
static int lead_mid_ok(const CamParams *p, const LeadS *H, const CPath *c, double x, double y, double tx, double ty, int out) {
    double mx, my; lead_far_pt(p, H, x, y, tx, ty, out, &mx, &my);
    int ccw = cl_area(c) > 0;
    return pip(c, mx, my) == (p->lead_right ? !ccw : ccw) || path_dist(c, mx, my) <= g_nc_tol;
}
/* the lead gouge check for one side (0x991): FUN_1801c46f0 -> FUN_1801c3d70 (decomp/mcmill_all.c l.312726-313160). The bounds
   (FUN_180051990 l.56176-56222): the path, and the path offset by dVar6 = the multipass finish spacing (op +0x840 / +0x850 / +0x858 =
   record 0x8a8 / 0x8b8 / 0x8c0, when on, >= 1 pass and > nc_tol), else 1 % of the tool diameter (op +0x2e8). Each piece is cut where it
   meets that offset curve on its way back to the path, keeping the part at the attach point (trim_gt_to_pt); the arc cut drops the
   line (flag[0] = 0); a cut piece whose middle is not in the machinable area is flagged: the arc flagged drops the side, the line
   flagged drops the line. Measured: BRACKET4 op 2 (D 3) / op 9 (D 4.9) - Mastercam's cut lead ends 0.03 / 0.049 before the path.
   Near the attach point the lead is inside that band by construction: it counts only once the lead has left it (ours: the
   crossings at the attach point, which Mastercam's intersection walk sorts away, are not taken). */
static void lead_gouge_side(const CamParams *p, LeadS *L, const CPath *c, double x, double y, double tx, double ty, int out) {
    if (!lead_has(L)) return;
    double d6 = (p->mc_mp_on && p->mc_mp_nf >= 1 && p->mc_mp_fs > g_nc_tol) ? p->mc_mp_fs : 0.01 * p->tool_dia;
    int ccw = cl_area(c) > 0, left = 0;
#define OKPT(px_, py_) (pip(c, (px_), (py_)) == (p->lead_right ? !ccw : ccw) && path_dist(c, (px_), (py_)) >= d6)
    /* the arc: from the attach point outwards */
    if (L->r > 0) {
        LeadS a = *L; a.len = 0; const int N = 720; double prev = 0;
        for (int k = 1; k <= N; k++) {
            a.sw = L->sw * k / N; double px, py; lead_far_pt(p, &a, x, y, tx, ty, out, &px, &py);
            int ok = OKPT(px, py);
            if (ok) left = 1;
            else if (left) {                                     /* back in the band: cut at the crossing */
                double lo = prev, hi = a.sw;
                for (int it = 0; it < 50 && hi - lo > 1e-10; it++) { a.sw = (lo + hi) / 2; lead_far_pt(p, &a, x, y, tx, ty, out, &px, &py); if (OKPT(px, py)) lo = a.sw; else hi = a.sw; }
                LeadS h = *L; h.sw = lo / 2; h.len = 0;
                if (!lead_mid_ok(p, &h, c, x, y, tx, ty, out)) { memset(L, 0, sizeof *L); return; }
                L->sw = lo; L->len = 0; return;
            }
            prev = a.sw;
        }
        if (!left) { LeadS h = *L; h.sw = L->sw / 2; h.len = 0; if (!lead_mid_ok(p, &h, c, x, y, tx, ty, out)) { memset(L, 0, sizeof *L); return; } }
    }
    /* the line: on from the arc's far end (or the attach point) */
    if (L->len > 0) {
        LeadS a = *L; const int N = 720; double prev = 0;
        for (int k = 1; k <= N; k++) {
            a.len = L->len * k / N; double px, py; lead_far_pt(p, &a, x, y, tx, ty, out, &px, &py);
            int ok = OKPT(px, py);
            if (ok) left = 1;
            else if (left) {
                double lo = prev, hi = a.len;
                for (int it = 0; it < 50 && hi - lo > 1e-10; it++) { a.len = (lo + hi) / 2; lead_far_pt(p, &a, x, y, tx, ty, out, &px, &py); if (OKPT(px, py)) lo = a.len; else hi = a.len; }
                L->len = lo;
                if (L->r <= 0) { LeadS h = *L; h.len = lo / 2; if (!lead_mid_ok(p, &h, c, x, y, tx, ty, out)) memset(L, 0, sizeof *L); }
                return;
            }
            prev = a.len;
        }
        if (!left && L->r <= 0) { LeadS h = *L; h.len = L->len / 2; if (!lead_mid_ok(p, &h, c, x, y, tx, ty, out)) memset(L, 0, sizeof *L); }
    }
#undef OKPT
}

/* one pass along a tool-centre path: approach at the feed plane, plunge off the part, lead in, cut, lead out, retract */
/* leave the cut upwards: mccore retract_move 0x18089f390 / clear_move 0x18089e6a0 (decomp/mccore_moves.c; the mcmill
   contour and pocket writers call them, decomp/mcmill_ctour.c l.1965 / l.6448, mcmill_all.c l.316005 / l.332856):
   without rapid retract (record 0x1a9 = 0) the tool feeds up at the retract rate (0x388) to init_depth (the feed
   plane), then rapids to the retract / clearance height. Checked: mc-test01 op 1 (G1 to the feed plane, then G0). */
static _Thread_local double g_hzf = NAN;          /* pass(): the feed plane of the heights in force (ht_rel), NAN = the op's */
static void retract_up(MB *b, const CamParams *p, double zr) {
    V3 e = lastp(b); double zf = isnan(g_hzf) ? p->top_z + p->feed_plane : g_hzf;
    if (p->feed_retract && e.z + CAM_SYS_TOL < zf) {
        if (p->mc_rrate > 0) mv(b, 0, e.x, e.y, zf, p->mc_rrate); else mv(b, 1, e.x, e.y, zf, 0);
    }
    e = lastp(b); if (e.z + CAM_SYS_TOL < zr || !p->feed_retract) mv(b, 1, e.x, e.y, zr, 0);
}

/* pass_chain (defined above, thread-local): the contour chain being cut (open-chain lead directions), set by gen_contour */
/* open chain: the lead directions are the exact tangents of the end entities at the path's ends (an offset keeps the
   tangent; the polyline's end chords are off by half a sampling step on arcs). Checked: mc-test01 op 1 (open arcs,
   leads 0.3 line + 0.3 / 90 deg arc): Mastercam's lead arc is tangent to the true offset arc. */
static void exact_end_dirs(const CPath *c, double *t0x, double *t0y, double *t1x, double *t1y) {
    const Chain *ch = pass_chain; const McamFile *f = ofile;
    if (!ch || !f || ch->n < 1 || c->n < 2) return;
    for (int end = 0; end < 2; end++) {
        int pi = end ? c->n - 1 : 0;
        double x = c->xy[2 * pi], y = c->xy[2 * pi + 1], best = 1e300; int k = 0;
        for (int kk = 0; kk < 2; kk++) {                 /* the chain end entity next to this path end */
            const Ent *e = &f->ents[ch->ent[kk ? ch->n - 1 : 0]];
            double d = fmin(hypot(e->a.x - x, e->a.y - y), hypot(e->b.x - x, e->b.y - y));
            if (d < best) { best = d; k = kk ? ch->n - 1 : 0; }
        }
        double tx, ty; ent_tangent(f, ch, k, x, y, &tx, &ty);
        double *ox = end ? t1x : t0x, *oy = end ? t1y : t0y;
        if (tx == 0 && ty == 0) continue;
        if (tx * *ox + ty * *oy < 0) { tx = -tx; ty = -ty; }
        if (tx * *ox + ty * *oy > 0.9) { *ox = tx; *oy = ty; }   /* only a chord-sized correction */
    }
}

/* the cut path when the leads adjust it (FUN_180051990, all with the leads on, op +0x888): overlap past the start of a
   closed contour (FUN_180050a10: 0x988 > nc_tol, adjust end off; the loop's elements appended from its start, the last one
   cut where the distance runs out), shorten start / end (FUN_1800566e0: the path trimmed by 0x1328 / 0x1340), extend start
   / end (FUN_180054420: a line of that length along the end tangent, the entry / exit arc built on it). Returns 0 when the
   path is not adjusted, 1 adjusted (w, end points / tangents) */
typedef struct { double *xy; int n, cap; } PLine;
static int pl_push(PLine *w, double x, double y) {
    if (w->n == w->cap) { int nc = w->cap ? 2 * w->cap : 64; double *t = realloc(w->xy, (size_t)nc * 2 * sizeof(double)); if (!t) return 0; w->xy = t; w->cap = nc; }
    w->xy[2 * w->n] = x; w->xy[2 * w->n + 1] = y; w->n++; return 1;
}
static double pl_len(const PLine *w) { double L = 0; for (int i = 1; i < w->n; i++) L += hypot(w->xy[2 * i] - w->xy[2 * i - 2], w->xy[2 * i + 1] - w->xy[2 * i - 1]); return L; }
/* a shortening as long as the path or longer (user decision 2026-10-23: do what Mastercam does, no refusal): FUN_1800566e0
   (decomp/mcmill_all.c l.58846) walks the ctour elements with FUN_1800549f0, each one consumed whole while its length <=
   the rest - nc_tol; when every element is consumed no point is cut: the start shortening then moves the LAST element up
   to index 2 (count = 2, l.58884-58912) - the path is element 1 (its start point) + the last element, i.e. one move from
   the start to the end; the end shortening stops at index 1 and sets count = 2 (l.58916-58922) - the path is its FIRST
   element, whole. A path of one element is left as it is (start: iVar1 <= 2). Ours: the elements are this program's path
   vertices - exact for line elements; an arc element (chorded here) keeps its first chord / gives a straight move where
   Mastercam keeps the arc's own data (NOT DECODED for chorded paths) */
static void pl_overtrim(PLine *w, int at_end) {
    if (w->n <= 2) return;
    if (!at_end) { w->xy[2] = w->xy[2 * w->n - 2]; w->xy[3] = w->xy[2 * w->n - 1]; }
    w->n = 2;
}
static int pl_trim(PLine *w, double d, int at_end) {         /* remove arc length d from the start (or the end) */
    if (d >= pl_len(w) - g_nc_tol) { pl_overtrim(w, at_end); return 1; }
    if (at_end) { for (int i = 0, j = w->n - 1; i < j; i++, j--) for (int k = 0; k < 2; k++) { double t = w->xy[2 * i + k]; w->xy[2 * i + k] = w->xy[2 * j + k]; w->xy[2 * j + k] = t; } }
    int i = 1; double rem = d;
    while (i < w->n) { double L = hypot(w->xy[2 * i] - w->xy[2 * i - 2], w->xy[2 * i + 1] - w->xy[2 * i - 1]); if (L > rem) break; rem -= L; i++; }
    double L = hypot(w->xy[2 * i] - w->xy[2 * i - 2], w->xy[2 * i + 1] - w->xy[2 * i - 1]), u = L > 0 ? rem / L : 0;
    w->xy[2 * (i - 1)] += (w->xy[2 * i] - w->xy[2 * (i - 1)]) * u; w->xy[2 * (i - 1) + 1] += (w->xy[2 * i + 1] - w->xy[2 * (i - 1) + 1]) * u;
    memmove(w->xy, w->xy + 2 * (i - 1), (size_t)(w->n - (i - 1)) * 2 * sizeof(double)); w->n -= i - 1;
    if (at_end) { for (int a = 0, b2 = w->n - 1; a < b2; a++, b2--) for (int k = 0; k < 2; k++) { double t = w->xy[2 * a + k]; w->xy[2 * a + k] = w->xy[2 * b2 + k]; w->xy[2 * b2 + k] = t; } }
    return 1;
}
static void pl_dir(const PLine *w, int end, double *tx, double *ty) {
    int i = end ? w->n - 1 : 1; double dx = w->xy[2 * i] - w->xy[2 * i - 2], dy = w->xy[2 * i + 1] - w->xy[2 * i - 1], l = hypot(dx, dy);
    if (l > 0) { *tx = dx / l; *ty = dy / l; }
}
static int adj_path(const CamParams *p, const CPath *c, int closed, PLine *w, double *t0x, double *t0y, double *t1x, double *t1y) {
    if (!p->lead_on) return 0;
    double tol = g_nc_tol;
    int ovl = closed && p->lead_ovl > tol && !p->adj_e;
    /* shorten start / end (FUN_1800566e0) only without the overlap: FUN_180051990 l.56164-56172 calls either FUN_180050a10 (overlap)
       or FUN_1800566e0 (review 2026-10-23: the start was shortened with the overlap as well) */
    int ss = !ovl && p->adj_s && !p->adj_s_ext && p->adj_s_len > tol, se = p->adj_s && p->adj_s_ext && p->adj_s_len > tol;
    int es = !ovl && p->adj_e && !p->adj_e_ext && p->adj_e_len > tol, ee = !ovl && p->adj_e && p->adj_e_ext && p->adj_e_len > tol;
    if (!(ovl || ss || se || es || ee)) return 0;
    w->n = 0;
    int n = closed ? c->n + 1 : c->n;
    for (int i = 0; i < n; i++) if (!pl_push(w, c->xy[2 * (i % c->n)], c->xy[2 * (i % c->n) + 1])) return -2;
    if (ovl) {                                               /* FUN_180050a10: on from the start until the overlap is used */
        double rem = p->lead_ovl; int i = 0;
        for (int guard = 0; rem > tol && guard < 100 * c->n + 10; guard++, i++) {
            double ax = c->xy[2 * (i % c->n)], ay = c->xy[2 * (i % c->n) + 1], bx = c->xy[2 * ((i + 1) % c->n)], by = c->xy[2 * ((i + 1) % c->n) + 1];
            double L = hypot(bx - ax, by - ay);
            if (L > rem + tol) { if (!pl_push(w, ax + (bx - ax) * rem / L, ay + (by - ay) * rem / L)) return -2; rem = 0; break; }
            if (!pl_push(w, bx, by)) return -2;
            rem -= L;
        }
        pl_dir(w, 1, t1x, t1y);
    }
    if (ss) { pl_trim(w, p->adj_s_len, 0); pl_dir(w, 0, t0x, t0y); if (w->n == 2) pl_dir(w, 1, t1x, t1y); }
    if (es) { pl_trim(w, p->adj_e_len, 1); pl_dir(w, 1, t1x, t1y); if (w->n == 2) pl_dir(w, 0, t0x, t0y); }
    if (se) {                                                /* FUN_180054420: the line before the start along -t0 */
        if (!pl_push(w, 0, 0)) return -2;
        memmove(w->xy + 2, w->xy, (size_t)(w->n - 1) * 2 * sizeof(double));
        w->xy[0] = w->xy[2] - *t0x * p->adj_s_len; w->xy[1] = w->xy[3] - *t0y * p->adj_s_len;
    }
    if (ee) { double ex = w->xy[2 * w->n - 2], ey = w->xy[2 * w->n - 1]; if (!pl_push(w, ex + *t1x * p->adj_e_len, ey + *t1y * p->adj_e_len)) return -2; }
    return 1;
}

/* cutter compensation in control (record 0x5d0 = 1 Control, 2 Wear, 3 Reverse wear: mccore cc_in_control): the contour NCI
   writer FUN_180056be0 (decomp/mcmill_all.c l.59875-59890) gives the pass's element 2 - the first move after the start
   point, i.e. after the plunge - the code cc_control_gcode (41 / 42; mccore cc_control_gcode_2: Control / Wear left 41, right
   42, Reverse wear swapped) and its last element 140 (comp off, stop_cc then writes NCI 1004). Left / right = the side the
   tool runs on relative to its travel (Mastercam's 'L' / 'R' of 0x5d2): here the pass's free side (lead_right). The tab
   cutoff pass (param_14 = 2) gets them too, by record (decomp/mcmill_ctour.c l.8164-8177: record 2, the last, and record 3 with
   Wear / Reverse wear and the entry lead) - but it writes only the spans' moves, so which written move carries them is not
   worked out here: a cutoff with Control / Wear / Reverse wear is refused (gen_contour) */
static _Thread_local int pass_cc;            /* gen_contour: 1 = tag this op's passes */
static void pass_cc_tag(MB *b, const CamParams *p, int first, int last) {
    if (!pass_cc || first < 1) return;
    /* the lead-in writes its line's start again (the plunge's end: the same XY, lead_in): element 2 = the first move that goes
       somewhere in XY */
    while (first < last && hypot(b->m[first].p.x - b->m[first - 1].p.x, b->m[first].p.y - b->m[first - 1].p.y) < 1e-9) first++;
    if (first >= b->n || last <= first) return;
    int left = !p->lead_right, g = left ? 41 : 42;
    if (p->ct_comp == 3) g = left ? 42 : 41;
    b->m[first].cc = g; b->m[last].cc = 140;
}
/* keep tool down between multi passes / depth cuts (Multi Passes 0x8c8 = op +0x860, Depth Cuts 0x891 = op +0x829): the contour loop
   FUN_18005b340 (decomp/mcmill_ctour.c l.1685-1700) keeps down into pass >= 2 / depth >= 2 when the next start is more than nc_tol
   away and FUN_180053e70 (decomp/mcmill_all.c l.57488) finds no crossing of the straight link (from the last NCI point to the
   start; with computer compensation the two lines +- D / 2 beside it) with the chain, strictly inside it and farther than nc_tol
   from its ends; bnci_wr_ctour_marker then carries "no retract". The motion (BLOCK op 1 / 2, X9 handbook): a G1 at the depth just
   cut to the next start, then down - no retract. */
static _Thread_local int g_pass_end = -1;                 /* b->n before the last pass's retract */
static _Thread_local double g_pass_z = NAN;               /* the last pass's depth (Move.sof placement) */
static _Thread_local const double *g_kd_xy; static _Thread_local int g_kd_n, g_kd_closed;   /* the chain (polyline) of the current passes */
static int kd_seg_x(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy, double *t) {
    double rx = bx - ax, ry = by - ay, sx = dx - cx, sy = dy - cy, den = rx * sy - ry * sx; if (fabs(den) < 1e-15) return 0;
    double u = ((cx - ax) * sy - (cy - ay) * sx) / den, v = ((cx - ax) * ry - (cy - ay) * rx) / den;
    if (u <= 0 || u >= 1 || v < 0 || v > 1) return 0; *t = u; return 1;
}
static int kd_link_ok(const CamParams *p, double ax, double ay, double bx, double by) {
    if (!g_kd_xy || g_kd_n < 2) return 1;
    double L = hypot(bx - ax, by - ay); if (L <= 0) return 1;
    int cc = p->side != SIDE_ON && p->ct_comp == 0;           /* cc_in_computer: Computer (else the control offsets) */
    double nx = -(by - ay) / L * p->tool_dia / 2, ny = (bx - ax) / L * p->tool_dia / 2;
    int nl = cc ? 2 : 1;
    for (int k = 0; k < nl; k++) {
        double o = cc ? (k ? -1 : 1) : 0, x0 = ax + nx * o, y0 = ay + ny * o, x1 = bx + nx * o, y1 = by + ny * o;
        int ns = g_kd_closed ? g_kd_n : g_kd_n - 1;
        for (int i = 0; i < ns; i++) {
            int j = (i + 1) % g_kd_n; double t;
            if (!kd_seg_x(x0, y0, x1, y1, g_kd_xy[2 * i], g_kd_xy[2 * i + 1], g_kd_xy[2 * j], g_kd_xy[2 * j + 1], &t)) continue;
            if (t * L > g_nc_tol && (1 - t) * L > g_nc_tol) return 0;
        }
    }
    return 1;
}

/* the heights after mccore set_heights(ref, ref) (decompiled 2026-11-04, mccore 0x18089fa80: feed plane init = incremental (+0x198) ?
   inc + ref : the absolute value, not below ref; retract = on (+0x189) ? (incremental +0x188 ? inc + ref : abs) : init; clearance =
   on (+0x179) ? (incremental +0x178 ? inc + ref : abs) : retract). The depth copies' writer FUN_180045770 calls it with the depth just
   cut before each retract between passes (l.6589 / l.6598: set_heights(local_bd0, local_bd0), then retract_move - the next pass of
   the same chain is approached at those heights - or, when the next pass is another chain's, clear_move and set_heights(depth, top)
   again, l.6618). buckle-v12 op 13 (retract 15 / feed plane 10 incremental, depth cuts): NCI 11.333 / 6.333, 9.967 / 4.967, then
   8.6 before the next chain's approach at 12.7 / 7.7. */
static void ht_rel(const CamParams *p, double ref, double *zf, double *zr, double *zc) {
    double top = p->top_z, f = (p->ht_inc & 4) ? ref + p->feed_plane : top + p->feed_plane;
    if (f < ref) f = ref;
    double r = p->retract_z > -1e29 ? ((p->ht_inc & 2) ? ref + (p->retract_z - top) : p->retract_z) : f;
    if (r < f) r = f;                                          /* ours (as zr = fmax(zf, retract) everywhere here) */
    *zf = f; *zr = r;
    if (zc) *zc = p->mc_clr_on ? ((p->ht_inc & 1) ? ref + (p->clearance - top) : p->clearance) : r;
}
static void pass_retract(MB *b, const CamParams *p, double z, double zr) {
    if (!p->hz_on) { retract_up(b, p, zr); return; }
    if (p->hz_opend) { retract_up(b, p, fmax(p->top_z + p->feed_plane, p->retract_z)); return; }   /* the op's end: its own heights */
    double f, r, c; ht_rel(p, z, &f, &r, &c);
    g_hzf = f; retract_up(b, p, p->hz_last ? c : r); g_hzf = NAN;
}
/* one pass; returns 0 (-2: out of memory) */
static int pass(MB *b, const CamParams *p, const CPath *c, int closed, double z, int first, int with_tabs) {
    if (c->n < 2) return 0;
    int depth_chg = !first && !isnan(g_pass_z) && z < g_pass_z - CAM_SYS_TOL; g_pass_z = z;   /* the next, deeper cut (not a new chain's first) */
    double t0x, t0y, t1x, t1y; path_dirs(c, closed, &t0x, &t0y, &t1x, &t1y);
    if (!closed) exact_end_dirs(c, &t0x, &t0y, &t1x, &t1y);
    else ctoff_exact_dirs(c, &t0x, &t0y, &t1x, &t1y);
    LeadS qi, qo; lead_sides(p, &qi, &qo);
    if (p->lead_skip & 1) memset(&qi, 0, sizeof qi);
    if (p->lead_skip & 2) memset(&qo, 0, sizeof qo);
    PLine w = { 0 }; int adj = adj_path(p, c, closed, &w, &t0x, &t0y, &t1x, &t1y);
    if (adj < 0) { free(w.xy); return adj == -2 ? -2 : -1; }
    double x0 = adj ? w.xy[0] : c->xy[0], y0 = adj ? w.xy[1] : c->xy[1];
    double xe = adj ? w.xy[2 * w.n - 2] : x0, ye = adj ? w.xy[2 * w.n - 1] : y0;   /* closed: the exit at the start unless adjusted */
    /* ours: a side whose path end is extended (adjust start / end Extend) starts off the loop, where the fit's free-side test
       does not apply - that side is not fitted */
    int chk_i = !(adj && p->adj_s && p->adj_s_ext && p->adj_s_len > g_nc_tol), chk_o = !(adj && p->adj_e && p->adj_e_ext && p->adj_e_len > g_nc_tol && !(p->lead_ovl > g_nc_tol));
    if (closed && p->lead_gouge && ((chk_i && qi.r > 0) || (chk_o && qo.r > 0))) {
        /* gouge check (0x991): lead_gouge_side, each side on its own (FUN_1801c46f0 is called per side, l.56368-56396) */
        if (chk_i) lead_gouge_side(p, &qi, c, x0, y0, t0x, t0y, 0);
        if (chk_o) lead_gouge_side(p, &qo, c, xe, ye, t1x, t1y, 1);
    }
    double sx = x0, sy = y0, lx, ly, lz, zf = p->top_z + p->feed_plane, zr = fmax(zf, p->retract_z);
    if (p->hz_on && !isnan(p->hz_in)) ht_rel(p, p->hz_in, &zf, &zr, NULL);   /* the next pass of the same chain */
    lead_start(p, &qi, sx, sy, t0x, t0y, &lx, &ly, &lz);
    /* the pass starts where the last one ended (next depth of a loop, or a chain continuing the previous one):
       stay down and feed on, as Mastercam does (the depth copies' writer FUN_180045770, decomp/mcmill_ctour.c l.6162: no
       retract when the next start is within nc_tol of the last point) */
    if (!first && !lead_has(&qi) && b->n >= 2) {
        Move *up = &b->m[b->n - 1], *bot = &b->m[b->n - 2];
        if (up->rapid && hypot(up->p.x - sx, up->p.y - sy) < 1e-6 && hypot(bot->p.x - sx, bot->p.y - sy) < 1e-6) {   /* up or down: FUN_180045770 retracts only beyond nc_tol (decomp/mcmill_ctour.c l.6650-6667), plunge_move with the tool down feeds to the new depth either way (decomp/mccore_moves.c l.188) */
            b->n--;
            /* the next depth while the tool stays down: its own section-start block all the same (FUN_180045770: markers -7 -5 -4 -6 for
               a new depth index at the same point, l.6170-6182, then wr_sof, l.6595-6634), the 1002 at the plunge's end (measured:
               BRACKET4 op 15 "-7 -5 -4 -6 S 1002(start, next depth) 1"); a continuation at the same depth gets none (l.6162-6166:
               -7 -6 only; BRACKET4 op 20) */
            if (fabs(bot->p.z - z) > 1e-9) { mv(b, 0, sx, sy, z, p->plunge_feed); b->m[b->n - 1].sof = 1; }   /* (buckle-v6 op 29: up to the next chain's depth, F = plunge) */
            int c0 = b->n;
            if (adj) for (int i = 1; i < w.n; i++) mv(b, 0, w.xy[2 * i], w.xy[2 * i + 1], z, p->feed);
            else if (cut_path(b, p, c, closed, z, with_tabs) < 0) { free(w.xy); return -5; }
            ct_tag_arcs(b, c0);   /* NS-3: the offset's true arcs (one record per ring arc) */
            { V3 e = lastp(b); if (lead_has(&qo)) lead_out(b, p, &qo, e.x, e.y, t1x, t1y, z); }
            pass_cc_tag(b, p, c0, b->n - 1);
            g_pass_end = b->n; pass_retract(b, p, z, zr);
            free(w.xy); return 0;
        }
    }
    int kd = 0;
    if (!first && p->kd_ok && g_pass_end >= 1 && g_pass_end <= b->n) {
        V3 e0 = b->m[g_pass_end - 1].p;
        if (hypot(lx - e0.x, ly - e0.y) > g_nc_tol && kd_link_ok(p, e0.x, e0.y, lx, ly)) {
            b->n = g_pass_end; kd = 1;                        /* the last pass's retract goes */
            mv(b, 0, lx, ly, e0.z, p->feed);
            if (e0.z > z + lz + 1e-9) mv(b, 0, lx, ly, z + lz, p->plunge_feed);
        }
    }
    /* Plunge after first move / Retract before last move (Lead In/Out 0x992 / 0x993): the depth copies' writer FUN_180045770
       (decomp/mcmill_ctour.c l.6137-6148, l.6379-6420, l.6880-6910, l.7287-7310): local_bf1 = leads, entry and 0x992 (with "first
       depth cut only": depth 1, pass 1) - the first entry move (flag 0x4000: record 2, the move after the start, not the path's own
       first record) is written at cur_depth (the height reached before the plunge) and plunge_move follows at its end; local_bf2 =
       leads, exit and 0x993 - at the start of the last move (flag 0x8000, not the path's last record) clear_move on the last depth,
       else retract_move, and that move at the raised height. A move = one record: the lead line, else the whole lead arc. */
    int pa = !kd && (p->lead_xflags & 16) && p->lead_on && p->lead_in_on && lead_has(&qi);
    /* Use entry point (0x929; FUN_180051990 local_196, decomp/mcmill_all.c l.55990-55996 / l.56530-56570, the call's arguments from
       mcmill.dll 0x18005ccb7-0x18005cd5d): the lead's records move up one and record 1 becomes the point - the plunge goes there, at
       the point's depth with "Use point depth" 0x92a, else at the lead start's (record 2's) z - and record 2 (the lead's start)
       follows as a line; record 2 is dropped when within nc_tol of the point at the same z */
    double ex0 = lx, ey0 = ly, ez0 = z + lz;
    if (p->pt_in) {
        if (pa || kd) { free(w.xy); return -6; }               /* ours: with Plunge after first move / keep tool down: not read */
        lx = p->pt_x; ly = p->pt_y; lz = ((p->lead_xflags & 64) ? p->pt_z : ez0) - z;
    }
    if (!kd) {
        /* the pass's section-start block (Move.sof): FUN_180045770 calls wr_sof at every pass start (decomp/mcmill_ctour.c l.6634 /
           l.6732) after the previous pass's retract and before its approach, and cancels the queued block (cancel_q_1000, l.6774) when
           the tool is still down at the start - the stay-down / keep-down links above, which return or set kd. The block's 1002 holds
           the approach point (measured: A001 op 1, BRACKET4 op 12 - "-7 -6 0 0 [S 1002 (next start, approach z)] 0 0 1") */
        int s0 = b->n;
        if (first) mv(b, 1, lx, ly, p->clearance, 0);
        else if (zr > zf) {                                    /* NCI (buckle-v12 op 13): up to the approach height first, then across */
            if (p->hz_on && b->n) { V3 e = lastp(b); if (e.z < zr - CAM_SYS_TOL) mv(b, 1, e.x, e.y, zr, 0); }
            s0 = b->n;
            mv(b, 1, lx, ly, zr, 0);
        }
        /* the next, deeper depth cut that starts elsewhere: the block after the traverse, before the rapid down to the feed plane (its 1002
           = that point) - measured: BRACKET4 op 2 "-7 -5 -4 -6 0(old, 25) 0(new, 25) [S 1002 (new, 10)] 0(new, 10) 1"; a new chain (its
           first, higher depth: BRACKET4 op 7 "-7 -5 -4 -6 0(old, 11.5) 0(old, 25) [S 1002 (new, 25)] 0(new, 25) 0(new, 10)") or the same
           depth (A001 op 1, BRACKET4 op 12): before the traverse. Which FUN_180045770 branch orders the records so is NOT DECODED
           (formats/nci_save.md 7) */
        if (depth_chg && zr > zf) s0 = b->n;
        mv(b, 1, lx, ly, zf, 0);
        if (!first && s0 < b->n) b->m[s0].sof = 1;
        if (!pa) mv(b, 0, lx, ly, z + lz, p->plunge_feed);
    }
    int c0 = b->n;
    if (pa) {
        MB t = { 0 }; lead_in(&t, p, &qi, sx, sy, t0x, t0y, z);   /* t.m[0] = the lead's start */
        int re = qi.len > 0 ? 1 : t.n - 1;                     /* the end of the first record */
        for (int i = 1; i <= re && i < t.n; i++) mv(b, 0, t.m[i].p.x, t.m[i].p.y, zf, t.m[i].feed);
        if (re < t.n) mv(b, 0, t.m[re].p.x, t.m[re].p.y, t.m[re].p.z, p->plunge_feed);
        for (int i = re + 1; i < t.n; i++) mv(b, 0, t.m[i].p.x, t.m[i].p.y, t.m[i].p.z, t.m[i].feed);
        free(t.m);
    } else {
        if (p->pt_in && !(hypot(ex0 - lx, ey0 - ly) < g_nc_tol && fabs(ez0 - (z + lz)) < g_nc_tol)) {
            double lf = p->lead_fin_on && p->lead_fin_v > 0 && p->lead_on ? p->lead_fin_v : p->feed;   /* an entry record (the lead feed) */
            mv(b, 0, ex0, ey0, ez0, lf);
        }
        if (lead_has(&qi)) lead_in(b, p, &qi, sx, sy, t0x, t0y, z); else mv(b, 0, sx, sy, z, p->feed);
    }
    if (adj) for (int i = 1; i < w.n; i++) mv(b, 0, w.xy[2 * i], w.xy[2 * i + 1], z, p->feed);   /* no tabs (gen_contour refuses them with an adjusted path) */
    else if (cut_path(b, p, c, closed, z, with_tabs) < 0) { free(w.xy); return -5; }
    ct_tag_arcs(b, c0);   /* NS-3: the offset's true arcs (one record per ring arc) */
    V3 e = lastp(b);
    if (p->pt_out && lead_has(&qo) && (p->lead_xflags & 32) && p->lead_on && p->lead_out_on) { free(w.xy); return -6; }   /* ours: not read */
    if (lead_has(&qo) && (p->lead_xflags & 32) && p->lead_on && p->lead_out_on) {
        MB t = { 0 }; mv(&t, 0, e.x, e.y, z, 0); lead_out(&t, p, &qo, e.x, e.y, t1x, t1y, z);
        int ls = qo.len > 0 ? t.n - 1 : 1;                     /* the first move of the last record */
        for (int i = 1; i < ls; i++) mv(b, 0, t.m[i].p.x, t.m[i].p.y, t.m[i].p.z, t.m[i].feed);
        retract_up(b, p, p->pass_last_depth ? p->clearance : (p->tab_rise_z > 0 ? p->tab_rise_z : zr));   /* clear_move / retract_move */
        double hz = lastp(b).z;
        for (int i = ls; i < t.n; i++) mv(b, 0, t.m[i].p.x, t.m[i].p.y, hz, t.m[i].feed);
        free(t.m);
        pass_cc_tag(b, p, c0, b->n - 1);
        g_pass_end = -1; retract_up(b, p, zr);                 /* (no keep-down from the air) */
        free(w.xy); return 0;
    }
    if (lead_has(&qo)) lead_out(b, p, &qo, e.x, e.y, t1x, t1y, z);
    /* Use exit point (0x971; FUN_180051990 bVar21, l.55998-56004 / l.56596-56640): a record appended - a line to the point at its
       depth with "Use point depth" 0x972, else at the last record's z; dropped within nc_tol at the same z */
    if (p->pt_out) { V3 q = lastp(b); double zx = (p->lead_xflags & 128) ? p->pt_z : q.z;
        if (!(hypot(q.x - p->pt_x, q.y - p->pt_y) < g_nc_tol && fabs(zx - q.z) < g_nc_tol)) {
            double lf = p->lead_fout_on && p->lead_fout_v > 0 && p->lead_on ? p->lead_fout_v : p->feed;
            mv(b, 0, p->pt_x, p->pt_y, zx, lf); } }
    pass_cc_tag(b, p, c0, b->n - 1);
    g_pass_end = b->n; pass_retract(b, p, z, zr);
    free(w.xy); return 0;
}

/* 2D chamfer with a pointed tool: tool angle a (per side) and tip diameter dt. The cone must pass through the chain at
 * depth w / tan a below the top, so the tip goes to top - w / tan a - tip_offset and the tool centre sits
 * dt / 2 + tip_offset * tan a from the chain. */
static int chamfer_geometry(const CamParams *p, double *r_eff, double *z_tip, char *err, size_t errn) {
    double a = p->tip_angle;
    if (p->tool_type != 12) a /= 2;                            /* drills / spot / countersink store the included angle */
    if (!(a > 1 && a < 89)) { snprintf(err, errn, "chamfer needs a pointed tool (chamfer mill, spot drill or countersink)"); return 0; }
    double t = tan(a * M_PI / 180), rt = p->tip_dia > 0 ? p->tip_dia / 2 : 0;
    *r_eff = rt + p->chamfer_tip * t;
    *z_tip = p->top_z - p->chamfer_w / t - p->chamfer_tip;
    if (*r_eff + p->chamfer_w > p->tool_dia / 2 + 1e-9) { snprintf(err, errn, "chamfer %.3f + tip offset too wide for a %.3f tool", p->chamfer_w, p->tool_dia); return 0; }
    return 1;
}

int chamfer_preview(const CamParams *p, double *r_eff, double *z_tip, char *err, size_t errn) { return chamfer_geometry(p, r_eff, z_tip, err, errn); }

/* tapered walls (Depth Cuts 25598 1381 / 1252): FUN_180056a70 (decomp/mcmill_ctour.c l.10101) - depth cuts on (op +0x800),
   contour taper on (ctour_prm +0xe2 = record 0x130a) and angle (+0xe8 = 0x1310) > tol; the wall stock of each depth is then
   FUN_1800569a0(chain Z, depth, angle) = tan(a) (Zc - z) + (rc + stock) / cos(a) - rc, rc = the tool's corner radius (op
   +0x2f0 = record 0x358), z + rc with tip comp (ctour_prm +9 = 0x1231); called from the contour loop l.982 with the chain
   Z (local_1228) and the current depth (local_1278) */
/* FUN_180056a70 gates on the opcode (1) alone, not the contour type (ctour_prm +0xe0): 2D and 2D chamfer alike (user decision
   2026-10-23: as Mastercam; was 2D only); Engrave is this program's type (ours, no taper) */
static int ct_tapered(const CamParams *p) { return (p->mc_depth_on || p->stepdown > 1e-6) && p->dc_taper && p->dc_taper_a > CAM_SYS_TOL; }
static double ct_taper_stock(const CamParams *p, double zc, double z) {
    double a = p->dc_taper_a, c = cos(a), rc = p->corner_r > 0 ? p->corner_r : 0;
    if (p->ct_tipcomp) z += rc;
    if (fabs(c) < 1e-12) c = 1;
    return tan(a) * (zc - z) + (rc + p->stock_xy) / c - rc;
}
typedef struct { double *xy, z, sx, sy, ttx, tty; int n; } CtChain;
/* Ramp contour (contour type 4, ctour_prm +0xe0; formats/contour_100_plan.md "Ramp contour"): FUN_1801fe810 -> FUN_1801ff170 (decomp/
   mcmill_all.c l.350715-351297): per chain the offset loop (ccomp_pock_from_ctour by D/2 with computer compensation + wall stock, the
   corner options as FUN_180051990), then laps of create_ramped_ctour FUN_1801fdcb0 (Rmpctour.cpp, l.349922-350220) written by
   FUN_1801fe8d0 (l.350442: a feed at the plunge rate op +0x318 to the lap's first point, then each record a line / helical arc at the
   feed op +0x310) until its state is 5. create_ramped_ctour, a closed loop (start within nc_tol of the end), Angle / Depth motion:
   state -1: z = top of stock, bottom = ctour_depth, slope = tan(ramp angle 0x12c8) (Angle) or ramp depth 0x12d0 / perimeter (Depth),
   none below 1e-12 (-2: refused); run = (top - bottom) / slope; length left = perimeter + op +0x920 (0x988) + run. Per entity of the
   lap: state 0 - z -= L x slope (not below the bottom); L <= the run left: the entity to z, else state 1: split at run left / L (both
   pieces > nc_tol, else whole) - to z, the rest at the bottom; state 2 - L > the length left: state 5, the piece up to it (> nc_tol)
   and stop; else the entity at z. After each: length left -= L, run left -= L in state 0, state 1 -> 2 at the bottom. After the lap:
   state 2 with length left < nc_tol -> 5; z within tol of the bottom below state 2 -> 2; Depth motion without "Make pass at final
   depth" 0x12e8 and state >= 2 -> 5. Leads: rmp_entry / rmp_exit (FUN_1801fcc30). Control / Wear / Reverse wear: rmp_cc_tag. Ours: refused - several loops; a lap
   with no record (the engine's failure). */
/* Ramp / Oscillate leads: add_lead_in_out FUN_1801fcc30 (Rmpctour.cpp; decomp/mcmill_all.c l.349035-349410), called by the
   Ramp driver FUN_1801ff170 (mcmill.dll 0x1801ffcfb / 0x180200183) and the Oscillate driver FUN_1801aa650 (0x1801ab299 / 0x1801ab6b8)
   with param_5 = the side 'L' / 'R' (op +0x56a), param_6 = entry (op +0x888 && +0x890), param_7 = exit (+0x888 && +0x8d8). The
   pieces are FUN_1801c24a0's, as the 2D contour's (entry: the arc on the lap's first element, the line on the arc; exit: the arc on
   its last element, the line on the arc), but the tests are > 1e-12, not > nc_tol: the entry line +0x898 (record 0x900), the arc
   +0x8a8 radius and +0x8b0 sweep both, the exit line +0x8e0, arc +0x8f0 / +0x8f8. The records' z: each entry piece = the next
   record's + its height (+0x78), each exit piece = the previous one's + its height (l.349330-349400); FUN_1801fe8d0 then feeds to
   the first record at the plunge rate, the pieces at the lead feed overrides (ctour_prm +0x130 / +0x128, +0x140 / +0x138). With the
   gouge check (op +0x929) FUN_1801c46f0 runs per side against the pass path and the path offset by +0x858 (0x840 on and > nc_tol)
   or 1 % of D (FUN_1801fe310 l.350426; the bounds are built for closed chains only) - lead_gouge_side, as the contour's. When the
   drivers call it: a closed chain (create_ramped_ctour's +2 byte = param_4, the start within nc_tol of the end) gets the entry on
   its first lap only and the exit on the lap that ends in state 5; an open chain both on every call (decomp l.351084-351091). */
static void rmp_lead_sides(const CamParams *p, LeadS *in, LeadS *out) {
    memset(in, 0, sizeof *in); memset(out, 0, sizeof *out);
    if (!p->lead_on) return;
    if (p->lead_in_on) {
        in->sw = (p->lead_sw_rec & 1) ? p->lead_sw : lead_sweep_v(p->lead_sw);
        in->r = p->lead_r > 1e-12 && in->sw > 1e-12 ? p->lead_r : 0; in->len = p->lead_len > 1e-12 ? p->lead_len : 0;
        in->dzl = p->mc_lead_dz[0]; in->dza = p->mc_lead_dz[1]; in->perp = p->lead_in_perp != 0;
    }
    if (p->lead_out_on) {
        out->sw = (p->lead_sw_rec & 2) ? p->lead_xsw : lead_sweep_v(p->lead_xsw);
        out->r = p->lead_xr > 1e-12 && out->sw > 1e-12 ? p->lead_xr : 0; out->len = p->lead_xlen > 1e-12 ? p->lead_xlen : 0;
        out->dzl = p->mc_lead_dz[2]; out->dza = p->mc_lead_dz[3]; out->perp = p->lead_out_perp != 0;
    }
}
/* the travel direction of the path's first element / of the last move written */
static void rmp_first_dir(const CPath *c, int closed, double *tx, double *ty) {
    int n = closed ? c->n + 1 : c->n;
    for (int i = 1; i < n; i++) {
        double dx = c->xy[2 * (i % c->n)] - c->xy[2 * (i - 1)], dy = c->xy[2 * (i % c->n) + 1] - c->xy[2 * (i - 1) + 1], L = hypot(dx, dy);
        if (L > 1e-9) { *tx = dx / L; *ty = dy / L; return; }
    }
    *tx = 1; *ty = 0;
}
static void rmp_last_dir(const MB *b, double *tx, double *ty) {
    V3 e = lastp(b);
    for (int i = b->n - 2; i >= 0; i--) {
        double dx = e.x - b->m[i].p.x, dy = e.y - b->m[i].p.y, L = hypot(dx, dy);
        if (L > 1e-9) { *tx = dx / L; *ty = dy / L; return; }
    }
    *tx = 1; *ty = 0;
}
/* entry: position over the lead's start, the first record at the plunge rate, then the pieces down to the lap's start */
/* the finish passes' keep tool down (Multi Passes 0x8c8 = op +0x860): the Ramp driver's FUN_1801fe3e0 (l.350446) - on a closed chain
   the straight link from the last point written to the pass's record 1 is cut with the bound (the chain offset by the finish
   spacing +0x858 when on and > nc_tol, else 1 % of D, plus D/2 and stock: FUN_1801fe310 at the start of FUN_1801ff170); a crossing
   at t in (nc_tol / L, 1 - nc_tol / L) (0.001 when L <= nc_tol) means retract; an open chain always keeps down. The Oscillate driver
   passes the flag itself (l.297022). The marker's "no retract" then gives the contour's link (cam2d.c pass: a feed at the depth
   just cut to the next start, then down). */
static int rmp_kd_ok(const CPath *kdb, int closed, double ax, double ay, double bx, double by) {
    if (!closed || !kdb || kdb->n < 2) return 1;
    double L = hypot(bx - ax, by - ay), tl = L <= g_nc_tol ? 0.001 : g_nc_tol / L;
    for (int i = 0; i < kdb->n; i++) {
        int j = (i + 1) % kdb->n; double t;
        if (!kd_seg_x(ax, ay, bx, by, kdb->xy[2 * i], kdb->xy[2 * i + 1], kdb->xy[2 * j], kdb->xy[2 * j + 1], &t)) continue;
        if (tl < t && t < 1.0 - tl) return 0;
    }
    return 1;
}
/* cutter compensation in control (0x5d0 = 1 / 2 / 3, side on) in the Ramp / Oscillate lap writer FUN_1801fe8d0 (decomp/mcmill_all.c
   l.350524-350535): record 2 gets cc_control_gcode when its 9th argument (rsp+0x48 at the call: "first lap") is set, the lap's last
   record 140 when its 10th (rsp+0x50: "last lap") is; the drivers pass (mcmill.dll, objdump of the calls 0x1801ffd65 / 0x18020027a,
   0x1801ab2f9 / 0x1801ab78d) Ramp: first = the pass's first lap of a closed chain (r13b / 0x70(rsp) set before the pass, cleared after
   a closed lap, 0x1801ffd98 / 0x1802002bb), every lap of an open chain; last = the lap ending in state 5 (closed), every lap (open,
   0x1801ffc91 / 0x180200117); Oscillate: its one lap both (0x1801ab22f-0x1801ab243 / 0x1801ab653-0x1801ab667). The code: left 41,
   right 42 (the side op +0x56a, which the Ramp driver flips per open lap - lead_right here), Reverse wear swapped (cc_control_gcode_2) */
static _Thread_local int g_rmp_e2 = -1;
static void rmp_cc_tag(MB *b, const CamParams *p, int first, int last) {
    if (p->side == SIDE_ON || p->ct_comp < 1 || p->ct_comp > 3) return;
    int left = !p->lead_right, g = left ? 41 : 42;
    if (p->ct_comp == 3) g = left ? 42 : 41;
    int e2 = g_rmp_e2;                                         /* lead_in writes its start again (ct_lin_mill drops such a repeat) */
    while (first && e2 >= 1 && e2 < b->n - 1 && hypot(b->m[e2].p.x - b->m[e2 - 1].p.x, b->m[e2].p.y - b->m[e2 - 1].p.y) < 1e-9
           && fabs(b->m[e2].p.z - b->m[e2 - 1].p.z) < 1e-9) e2++;
    if (first && e2 >= 0 && e2 < b->n) b->m[e2].cc = g;
    if (last && b->n > 0) b->m[b->n - 1].cc = 140;
}
static void rmp_entry(MB *b, const CamParams *p, LeadS *qi, const CPath *c, int closed, double sx, double sy, double z, int first, int lap0, int kd, const CPath *kdb) {
    double tx, ty, lx, ly, dz, zf = p->top_z + p->feed_plane, zr = fmax(zf, p->retract_z);
    rmp_first_dir(c, closed, &tx, &ty);
    if (closed && p->lead_gouge && qi->r > 0) lead_gouge_side(p, qi, c, sx, sy, tx, ty, 0);
    if (!lead_has(qi)) { lx = sx; ly = sy; dz = 0; } else lead_start(p, qi, sx, sy, tx, ty, &lx, &ly, &dz);
    if (lap0 && kd && !first && g_pass_end >= 1 && g_pass_end <= b->n) {
        V3 e0 = b->m[g_pass_end - 1].p;
        if (rmp_kd_ok(kdb, closed, e0.x, e0.y, lx, ly)) { b->n = g_pass_end; mv(b, 0, lx, ly, e0.z, p->feed); lap0 = 0; }
    }
    if (lap0) {                                                /* the chain's first lap: positioned over record 1 */
        if (first) mv(b, 1, lx, ly, p->clearance, 0); else if (zr > zf) mv(b, 1, lx, ly, zr, 0);
        mv(b, 1, lx, ly, zf, 0);
    }
    mv_nd(b, 0, lx, ly, z + dz, p->plunge_feed);
    g_rmp_e2 = b->n;                                           /* FUN_1801fe8d0's record 2: the next move */
    if (lead_has(qi)) lead_in(b, p, qi, sx, sy, tx, ty, z);
}
static void rmp_exit(MB *b, const CamParams *p, LeadS *qo, const CPath *c, int closed) {
    if (!lead_has(qo)) return;
    double tx, ty; V3 e = lastp(b); rmp_last_dir(b, &tx, &ty);
    if (closed && p->lead_gouge && qo->r > 0) lead_gouge_side(p, qo, c, e.x, e.y, tx, ty, 1);
    if (lead_has(qo)) lead_out(b, p, qo, e.x, e.y, tx, ty, e.z);
}
typedef struct { int st; double z, bot, slope, runr, rem; } CtRamp;
/* Open chains, Plunge motion, One way (2026-11-04): the driver FUN_1801ff170 (l.351063-351125; call args mcmill.dll 0x1801ffc12 -
   0x1801ffc64) calls FUN_1801fd530 when Plunge (ctour_prm +0x98 = 2, its param_5 = 1) or with One way (+0xb0 = 0x12d8) while the
   lap direction byte (+3) is set (param_5 = 0), else create_ramped_ctour. FUN_1801fd530 (l.349471-349920): state -1 - z = top,
   passes = max(1, (int)((top - bottom) / |ramp depth 0x12d0| + 0.999999)), step = (top - bottom) / passes; with param_5 the pass
   count + 1 and z - step; the records copied (reversed while +3 is set) all at z; an open chain toggles +3; then with param_5
   state 5 once the passes are done, without it state 3 -> 5. create_ramped_ctour on an open chain (+2 = 0): the path reversed
   while +3 is set, +3 toggled after each lap; state 1 -> 3 (not 2) at the bottom, entities in states 3 / 5 whole (no stop), a lap
   started in state 3 that ends in 3 -> 5. The driver flips the side (op +0x56a, the leads' side) after each lap of an open chain;
   leads on every lap of an open chain (FUN_1801fcc30 above). FUN_1801fe8d0 writes each lap the same way (its 3rd / 4th arguments
   are unused - 0x1801fe8d0 overwrites r8 / r9): a feed at the plunge rate to the lap's first record, then the records. */
static void rmp_lap_pts(const CPath *c, int closed, int rev, double **pts, int *np) {
    int n = closed ? c->n + 1 : c->n;
    double *q = malloc(sizeof(double) * 2 * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++) { int k = rev ? n - 1 - i : i; q[2 * i] = c->xy[2 * (k % c->n)]; q[2 * i + 1] = c->xy[2 * (k % c->n) + 1]; }
    *pts = q; *np = n;
}
/* Multi Passes (op +0x840 = record 0x8a8; FUN_1801ff170 l.350866-350905 / l.350925-351130 / l.351131-351285, FUN_1801aa650 alike):
   the first loop - rough passes 0x8aa (+ the finish passes 0x8b8 when "at every depth" ctour_prm +8 = 0x1230), each a whole
   ramp from the top (state -1, direction and side reset) on the chain offset by rough left x 0x8b0 + D/2 + stock + (finish
   passes x 0x8c0 when not every depth) + finish left x 0x8c0, the rough count taken first; a pass with no rough left gets the
   finish feed (FUN_1801fe8d0's 9th argument, op +0x13fc / +0x13f0 = 0x1464 / 0x1458), its marker no keep down. Then (not every
   depth) the finish loop k = 1..0x8b8 at (0x8b8 - k) x 0x8c0 + D/2 + stock: a closed chain create_ramped_ctour state -2 (-3 on
   the last: + the overlap 0x988) - z = the bottom, length = perimeter (+ overlap), state 2; an open chain FUN_1801fd530 with
   param_5 = 0 (one lap at the current z, the direction and side going on), the finish feed, the keep down check. fin: 0 a whole
   ramp pass, 2 / 3 a finish lap (3 the last); ff the finish feed; cr the open chain's direction / side carried into the finish
   loop. */
typedef struct { int dir, flip; } RmpCarry;
static int ct_ramp_chain(MB *b, const CamParams *p, const CPath *c, int closed, int first, int fin, int ff, RmpCarry *cr, const CPath *kdb, int kd) {
    double perim = path_len(c, closed), top = p->top_z, tol = CAM_SYS_TOL, nct = g_nc_tol;
    if (perim < 1e-12) return -1;
    int plunge = p->ct_rmode == 2, dir = fin ? cr->dir : 0, pass = 0, npass = 1;
    double step = 0;
    CtRamp S = { -1, 0, 0, 0, 0, 0 };
    S.bot = p->final_z; S.z = top;
    if (fin) {                                               /* create_ramped_ctour state -2 / -3, FUN_1801fd530 state 3 */
        S.st = closed ? 2 : 3; S.z = S.bot; S.rem = perim + (fin == 3 ? p->ct_ovl : 0);
    } else if (plunge) {                                     /* FUN_1801fd530 state -1 */
        if (!(fabs(p->ct_rdep) >= 1e-12)) return -1;           /* ours: a zero ramp depth divides by zero there */
        double k = (top - S.bot) / fabs(p->ct_rdep) + 0.999999;
        npass = (short)(int)(k >= 0 ? floor(k) : ceil(k)); if (npass < 1) npass = 1;
        step = (top - S.bot) / npass; S.st = 0;
    } else {                                                 /* create_ramped_ctour state -1 */
        S.slope = p->ct_rmode == 0 ? tan(p->ct_rang * 0.01745329251994329) : p->ct_rdep / perim;
        if (!(S.slope >= 1e-12)) return -1;
        double run = (top - S.bot) / S.slope;
        S.runr = run; S.rem = perim + p->ct_ovl + run; S.st = 0;
    }
    double zr = fmax(p->top_z + p->feed_plane, p->retract_z);
    CamParams q = *p;                                        /* the side flips per lap of an open chain */
    if (fin) q.lead_right = p->lead_right ^ cr->flip;
    if (ff && p->mp_ff_on) q.feed = p->mp_ff_v;              /* FUN_1801fe8d0: the finish feed for every record */
    LeadS qi0, qo0; rmp_lead_sides(p, &qi0, &qo0);
    int ent_on = 1, guard = 0;
    while (S.st < 5) {
        if (++guard > 100000) return -1;
        int st0 = S.st, nrec = 0, fd = fin ? !closed : plunge || (p->ct_r1way && dir), np;
        double *pts; rmp_lap_pts(c, closed, dir, &pts, &np);
        if (fd && plunge && !fin) { pass++; S.z -= step; }   /* FUN_1801fd530 with param_5 */
        CPath lc = { pts, np };
        LeadS qi = qi0, qo = qo0;
        if (closed && !ent_on) memset(&qi, 0, sizeof qi);    /* closed: the entry on the first lap only */
        g_rmp_e2 = -1;
        rmp_entry(b, &q, &qi, &lc, closed, pts[0], pts[1], S.z, first, guard == 1, kd, kdb);   /* record 1 at the plunge rate */
        for (int i = 1; i < np; i++) {
            double x0 = pts[2 * (i - 1)], y0 = pts[2 * (i - 1) + 1], x1 = pts[2 * i], y1 = pts[2 * i + 1], L = hypot(x1 - x0, y1 - y0);
            if (L <= 0) continue;
            if (fd) { mv(b, 0, x1, y1, S.z, q.feed); nrec++; continue; }
            int whole = 1, stop = 0;
            if (S.st == 0) {
                S.z -= L * S.slope; if (S.z < S.bot) S.z = S.bot;
                if (L > S.runr) {
                    S.st = 1;
                    double u = S.runr / L;
                    if (L * u > nct && (1 - u) * L > nct) {    /* split: to z, the rest at the bottom */
                        mv(b, 0, x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, S.z, q.feed); mv(b, 0, x1, y1, S.bot, q.feed); nrec += 2; whole = 0; }
                }
            } else if (S.st == 2 && L > S.rem) {
                S.st = 5;
                if (closed) {                                    /* the piece up to the length left, then stop */
                    whole = 0; stop = 1;
                    double u = S.rem / L;
                    if (nct < L * u) { mv(b, 0, x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, S.z, q.feed); nrec++; }
                }
            }
            if (whole) { mv(b, 0, x1, y1, S.z, q.feed); nrec++; }
            if (stop) break;
            S.rem -= L;
            if (S.st == 0) S.runr -= L;
            else if (S.st == 1) { S.st = closed ? 2 : 3; S.z = S.bot; }
        }
        if (fd) {
            if (plunge && !fin) { if (pass >= npass) S.st = 5; }
            else if (S.st == 3) S.st = 5;
        } else {
            if (S.st == 2) { if (S.rem < nct) S.st = 5; }
            else if (fabs(S.z - S.bot) < tol && S.st < 2) S.st = 2;
            if (st0 == 3 && S.st == 3) S.st = 5;
            else if (S.st >= 2 && p->ct_rmode == 1 && !p->ct_rfinal) S.st = 5;
        }
        if (!closed) dir = !dir;
        if (nrec && (!closed || S.st == 5)) rmp_exit(b, &q, &qo, &lc, closed);   /* closed: the exit on the lap ending in state 5 */
        if (nrec) rmp_cc_tag(b, &q, !closed || guard == 1, !closed || S.st == 5);
        free(pts);
        if (!nrec) return -1;
        if (closed) ent_on = 0; else q.lead_right = !q.lead_right;
    }
    if (cr) { cr->dir = dir; cr->flip = q.lead_right != p->lead_right; }
    g_pass_end = b->n; retract_up(b, p, zr);
    return 0;
}
/* Oscillate contour (type 6): bnci l.1199 -> FUN_1801aa5b0 -> FUN_1801aa650 (decomp/mcmill_all.c; the outer loop as the Ramp contour's:
   the offset, then create_linear_or_highspeed_ctour FUN_1801a9f50 (Oscillatectour.cpp) until state 5, written by FUN_1801fe8d0).
   FUN_1801a9f50 sets state 5 at once: one lap. Linear (0x1408 = 0): z starts at ctour_depth and moves by the maximum depth 0x1418 per
   distance 0x1410 (|.| of both, none below 1e-12: warning 0xa9c, refused), first down, turning every distance along the path: per
   entity, while the distance to the turn d < L - split at d / L when both pieces are > nc_tol (the piece to z -/+ piece / dist x depth,
   d -= piece, below nc_tol: turn, d = dist); a turn within nc_tol of the entity's start: turn there; within nc_tol of its end: d = L.
   Then the (rest of the) entity to z -/+ L / dist x depth, d -= L, below nc_tol: turn. Leads: rmp_entry / rmp_exit (FUN_1801fcc30). High speed: osc_piece (FUN_1801a9ab0). Control / Wear /
   Reverse wear: rmp_cc_tag. Ours: refused - several loops. */
/* High speed (0x1408 != 0, ctour_prm +0x1e0): FUN_1801a9ab0 (decomp/mcmill_all.c l.295979-296122), called by FUN_1801a9f50 after each
   piece it appends (l.296306 / l.296361) with hi = ctour_depth, lo = hi - |max depth|, the distance |0x1410|, 10, the direction (down).
   The piece's end z (linear) is mapped: off hi and lo, f = (z - lo) / (hi - lo), z = (hi + lo) / 2 - sin(f pi + pi / 2) (hi - lo) / 2
   (down: f -> 1 - f and the mirrored form - the same curve), the progress along the half wave = f (up) / 1 - f (down); at lo: 1 down,
   0 up; at hi: 0 down, 1 up. Then while (step + nc_tol) x 1.2 < the piece's length, step = distance / (10 - 1): the piece is split at
   step from its start (FUN_1801fd3e0), the first part's end z from the progress there ((progress x distance - length) + step) /
   distance on the linear ramp (down: hi - |hi - lo| x it, up: lo + ...), mapped on the curve; the rest goes on (its end z the
   mapped one). The record capacity x 11 (exceeded: -1, refused here as the engine fails). */
static void osc_piece(MB *b, const CamParams *p, double x0, double y0, double x1, double y1, double z, int down, double hi, double lo, double dist) {
    if (!p->ct_omode) { mv(b, 0, x1, y1, z, p->feed); return; }
    double H = hi - lo, mid = (hi + lo) * 0.5, prog, ze = z;
    if (fabs(z - hi) > 1e-12) {
        if (fabs(z - lo) > 1e-12) {
            prog = (z - lo) / H;
            if (!down) ze = mid - sin(prog * M_PI + M_PI / 2) * H * 0.5;
            else { prog = 1.0 - prog; ze = sin(prog * M_PI + M_PI / 2) * H * 0.5 + mid; }
        } else prog = down ? 1.0 : 0.0;
    } else prog = down ? 0.0 : 1.0;
    double step = dist / (10 - 1), Lp = hypot(x1 - x0, y1 - y0);
    while ((step + g_nc_tol) * 1.2 < Lp) {
        double t = step / Lp, px = x0 + (x1 - x0) * t, py = y0 + (y1 - y0) * t;
        double zf = ((prog * dist - Lp) + step) / dist, d = hi - lo;
        zf = down ? hi - fabs(d) * zf : fabs(d) * zf + lo;
        zf = (zf - lo) / d;
        double zm = down ? sin((1.0 - zf) * M_PI + M_PI / 2) * d * 0.5 + mid : mid - sin(zf * M_PI + M_PI / 2) * d * 0.5;
        mv(b, 0, px, py, zm, p->feed);
        x0 = px; y0 = py; Lp = hypot(x1 - x0, y1 - y0);
    }
    mv(b, 0, x1, y1, ze, p->feed);
}
/* Multi Passes: as the Ramp's (FUN_1801aa650 l.296735-297030): every pass a whole oscillation (FUN_1801a9f50 ignores the state);
   the first loop resets the direction (local_1a5 = 0), the finish loop does not - an open chain's finish passes run reversed
   (create_linear_or_highspeed_ctour leaves +3 = open); the side is not flipped (0x74(rsp) is written once, 0x1801aab27). rev: the
   lap reversed; ff the finish feed; kd the marker's keep down (op +0x860 itself on the finish passes). */
static int ct_osc_chain(MB *b, const CamParams *p, const CPath *c0, int closed, int first, int rev, int ff, int kd) {
    double dist = fabs(p->ct_odist), amp = fabs(p->ct_odep), nct = g_nc_tol;
    if (path_len(c0, closed) < 1e-12 || dist < 1e-12 || amp < 1e-12) return -1;
    double *pts; int np; rmp_lap_pts(c0, closed, rev, &pts, &np);
    CPath cl = { pts, closed ? np - 1 : np }; const CPath *c = &cl;
    CamParams q = *p; if (ff && p->mp_ff_on) q.feed = p->mp_ff_v; p = &q;
    double z = p->final_z, d = dist; int down = 1;
    double sx = c->xy[0], sy = c->xy[1], zr = fmax(p->top_z + p->feed_plane, p->retract_z);
    LeadS qi, qo; rmp_lead_sides(p, &qi, &qo);                 /* one lap (state 5): entry and exit, open or closed */
    g_rmp_e2 = -1;
    rmp_entry(b, p, &qi, c, closed, sx, sy, z, first, 1, kd, NULL);   /* FUN_1801fe8d0: record 1 at the plunge rate */
    int n = closed ? c->n + 1 : c->n;
    for (int i = 1; i < n; i++) {
        double x0 = c->xy[2 * (i - 1)], y0 = c->xy[2 * (i - 1) + 1], x1 = c->xy[2 * (i % c->n)], y1 = c->xy[2 * (i % c->n) + 1], L = hypot(x1 - x0, y1 - y0);
        if (L <= 0) continue;
        int guard = 0;
        while (d < L) {
            if (++guard > 1000000) { free(pts); return -1; }
            double u = d / L, p1 = L * u, p2 = (1 - u) * L;
            if (p1 <= nct || p2 <= nct) {
                if (p1 > nct || p2 <= nct) d = L; else { down = !down; d = dist; }
                continue;
            }
            double px = x0 + (x1 - x0) * u, py = y0 + (y1 - y0) * u;   /* the split point; the rest goes on */
            z += (down ? -1 : 1) * p1 / dist * amp;
            osc_piece(b, p, x0, y0, px, py, z, down, p->final_z, p->final_z - amp, dist);
            x0 = px; y0 = py;
            L = hypot(x1 - x0, y1 - y0); d -= p1;
            if (d < nct) { down = !down; d = dist; }
        }
        z += (down ? -1 : 1) * L / dist * amp;
        osc_piece(b, p, x0, y0, x1, y1, z, down, p->final_z, p->final_z - amp, dist);
        d -= L;
        if (d < nct) { down = !down; d = dist; }
    }
    rmp_exit(b, p, &qo, c, closed);
    rmp_cc_tag(b, p, 1, 1);
    free(pts);
    g_pass_end = b->n; retract_up(b, p, zr);
    return 0;
}
static int ct_cutoff(MB *b, CamParams *p, const McamFile *f, const Chain *ch, const CPath *c, double off, double z) {
    if (c->n < 0) { g_tab_why = 9; return -5; }
    if (c->n < 2) return 0;
    const Ent *ln[4096]; int nl = 0;
    for (int i = 0; i < ch->n && nl < 4096; i++) { const Ent *e = &f->ents[ch->ent[i]]; if (e->type == E_LINE) ln[nl++] = e; }
    g_tcl = ln; g_tcn = nl; g_tcoff = off;
    if (!p->ct_cutoff_op) retract_up(b, p, p->tab_rise_z);     /* retract_move at the path's end (l.1964; mode 2 only - not the 0x82 op's) */
    int r = cut_tab_cutoff(b, p, c, z);
    g_tcl = NULL; g_tcn = 0;
    return r;
}
/* ---- 3D contour, the narrow case (2026-11-08, formats/contour_3d_tabcutoff.md 1): bnci_ctour (decomp/mcmill_ctour.c l.413-421) sends
   3D chains (check_chains_3d -> ctour_3d) with incremental depth (op +0x140 = record 0x1a8), compensation Off (op +0x568 = 4), no leads
   (op +0x888) and |wall stock| (op +0x278 = record 0x2e0) <= tol to FUN_18005ef90 (decomp/mcmill_all.c l.63754-64210) instead of the
   contour loop: per chain its entities followed in 3D, every point's tool-view Z + D (D = floor stock op +0x820 + depth op +0x138, set_depths
   with the chain's start Z): lines to their ends (gr_lin_mill), arcs in a plane of the tool view as arcs (gr_arc_mill; XY-parallel = our
   tagged arcs, XZ / YZ ones - gr_arc_mill planes 1 / 2 - chorded here at tol, ours), other arcs FUN_18005fee0 (n = round(|sweep| /
   sqrt(8 ctour_tol / r)), < 2 -> 1, equal steps along the arc), splines FUN_1800601f0 (brk_spline_const_err from the chained start's
   parameter at ctour_tol until the far end within tol). Tabs, multi passes, chamfer, taper are not read there. The first pass is written
   at D + finish cuts x finish step + (rough cuts - 1) x rough step (calc_depth_cuts on the depth at the chain's start), and FUN_1800451f0
   (decomp/mcmill_ctour.c l.9892) copies it lower by the rough step, then the finish step (the copies' depth = gen_contour's levels); the
   depth copies' writer FUN_180045770 links them. Heights: set_depths (decomp/mccore_depths.c l.319-466) top of stock = incremental ?
   chain start Z + top : top; after the chain, if the chain's end at the first level lies above it by tol, start_top_of_stock takes that
   Z, and adjust_depths_3d / adjust_depths (mccore 0x18089e460 / 0x18089e310, decompiled 2026-11-08) put the incremental feed plane /
   retract / clearance on it. Between passes FUN_180045770 l.6672 (types 1 / 3; bnci_ctour l.354 made a 3D incremental op type 1) raises
   the reference to the last written point's Z, then set_heights(ref, ref) (ht_rel's rule: never below ref). */
typedef struct { double x, y, z, cx, cy; int arc, cw; } C3P;
static int c3_push(C3P **v, int *n, int *cap, double x, double y, double z, int arc, double cx, double cy, int cw) {
    if (*n == *cap) { int nc = *cap ? *cap * 2 : 256; C3P *q = realloc(*v, (size_t)nc * sizeof *q); if (!q) return 0; *v = q; *cap = nc; }
    C3P *e = &(*v)[(*n)++]; e->x = x; e->y = y; e->z = z; e->arc = arc; e->cx = cx; e->cy = cy; e->cw = cw; return 1;
}
static void c3_arc_pt(const Ent *e, double a, double *o) {        /* eval_curve of an arc: c + r (cos a X + sin a Y) */
    double X[3] = { 1, 0, 0 }, Y[3] = { 0, 1, 0 };
    if (e->vw) { memcpy(X, e->vm, sizeof X); memcpy(Y, e->vm + 3, sizeof Y); }
    o[0] = e->c.x + e->r * (cos(a) * X[0] + sin(a) * Y[0]); o[1] = e->c.y + e->r * (cos(a) * X[1] + sin(a) * Y[1]); o[2] = e->c.z + e->r * (cos(a) * X[2] + sin(a) * Y[2]);
}
/* the chain's feed records (z = the entity's own Z); 0 = out of memory / a spline without its curve */
static int c3_chain(const McamFile *f, const CamParams *p, const Chain *ch, C3P **v, int *n) {
    int cap = 0, aid = 0; *v = NULL; *n = 0;
    double ctol = p->ct_rec_tol;                               /* **ctour_prm = record 0x1228 */
    for (int i = 0; i < ch->n; i++) {
        const Ent *e = &f->ents[ch->ent[i]]; int r = ch->rev[i];
        V3 s = r ? e->b : e->a, t = r ? e->a : e->b;
        if (i == 0 && !c3_push(v, n, &cap, s.x, s.y, s.z, 0, 0, 0, 0)) return 0;   /* the first record: the chain's start (flag 0x4001) */
        if (e->type == E_POINT) continue;
        if (e->type == E_LINE) { if (!c3_push(v, n, &cap, t.x, t.y, t.z, 0, 0, 0, 0)) return 0; continue; }
        if (e->type == E_SPLINE) {                             /* FUN_1800601f0 */
            const CurveRec *cr = ent_crv(f, e); if (!cr) return 0;
            double t1, t2, o[3][3]; mcs_spline_t1t2(cr, &t1, &t2);
            double tt = r ? t2 : t1, te = r ? t1 : t2;
            for (int it = 0; it < 1000000; it++) {
                mcs_brk_spline_const_err(cr, spl_min_step(f), spl_max_step(f), ctol, r, CAM_SYS_TOL, &tt);
                mcs_spline_eval_d(cr, tt, 0, o);
                if (!c3_push(v, n, &cap, o[0][0], o[0][1], o[0][2], 0, 0, 0, 0)) return 0;
                if (fabs(tt - te) < CAM_SYS_TOL) break;
            }
            continue;
        }
        if (e->type != E_ARC) continue;
        double nz = e->vw ? e->vm[8] : 1, sw = e->sw, a0 = e->a0;
        if (fabs(fabs(nz) - 1) < 1e-9) {                       /* in the tool view's XY: one arc (gr_arc_mill plane 0) */
            double pt[3]; int k = (int)ceil(fabs(sw) / (e->r > p->tol ? fmin(2 * acos(1 - p->tol / e->r), M_PI / 18) : M_PI / 18)); if (k < 2) k = 2;
            int cw = ((sw * nz) < 0) != (r != 0); ++aid;
            for (int j = 1; j <= k; j++) { double u = (double)j / k; c3_arc_pt(e, a0 + sw * (r ? 1 - u : u), pt);
                if (!c3_push(v, n, &cap, pt[0], pt[1], pt[2], aid, e->c.x, e->c.y, cw)) return 0; }
            continue;
        }
        int xzyz = fabs(nz) < 1e-9 && (fabs(fabs(e->vm[6]) - 1) < 1e-9 || fabs(fabs(e->vm[7]) - 1) < 1e-9);
        int k;
        if (xzyz) { k = (int)ceil(fabs(sw) / (e->r > p->tol ? fmin(2 * acos(1 - p->tol / e->r), M_PI / 18) : M_PI / 18)); if (k < 2) k = 2; }   /* ours: chords */
        else {                                                 /* FUN_18005fee0 */
            double d = e->r > 0 ? sqrt(ctol * 8.0 / e->r) : 0;
            if (d == 0) k = 1; else { double q = fabs(sw) / d; k = (int)(q >= 0 ? floor(q + 0.5) : ceil(q - 0.5)); if (k < 2) k = 1; }
        }
        for (int j = 1; j <= k; j++) { double u = (double)j / k, pt[3]; c3_arc_pt(e, a0 + sw * (r ? 1 - u : u), pt);
            if (!c3_push(v, n, &cap, pt[0], pt[1], pt[2], 0, 0, 0, 0)) return 0; }
    }
    return 1;
}
/* set_heights(top, zmin) for the 3D contour: the incremental planes on `top`, the feed plane not below zmin (ht_rel's rule) */
static void ht3(const CamParams *p, double top, double zmin, double *zf, double *zr, double *zc) {
    double f = (p->ht_inc & 4) ? top + p->feed_plane : p->top_z + p->feed_plane;
    if (f < zmin) f = zmin;
    double r = p->retract_z > -1e29 ? ((p->ht_inc & 2) ? top + (p->retract_z - p->top_z) : p->retract_z) : f;
    if (r < f) r = f;
    *zf = f; *zr = r;
    *zc = p->mc_clr_on ? ((p->ht_inc & 1) ? top + (p->clearance - p->top_z) : p->clearance) : r;
    if (*zc < r) *zc = r;
}
static int gen_contour3d(const McamFile *f, CamOp *op, MB *b) {
    const CamParams *p = &op->p;
    if (p->dc_bydepth && op->nchains > 1) { snprintf(op->error, sizeof op->error, "3D contour with depth cuts by depth: the order of FUN_180045770's sort for it is not read (ours: refused)"); return 0; }
    int any = 0; double zc_last = p->clearance;
    for (int ci = 0; ci < op->nchains; ci++) {
        const Chain *ch = &op->chains[ci];
        if (!ch->n || cam_chain_is_point(f, ch)) continue;
        C3P *v; int n;
        if (!c3_chain(f, p, ch, &v, &n)) { free(v); snprintf(op->error, sizeof op->error, "out of memory"); return 0; }
        if (n < 2) { free(v); continue; }
        double z0 = v[0].z;                                    /* set_depths(op, the first entity's start Z, ..) */
        CamParams q = *p;                                      /* the chain's depth and top (incremental: on its start Z) */
        q.final_z = p->dep_inc ? z0 + (p->final_z - p->dep_cz) : p->final_z;
        q.top_z = p->top_inc ? z0 + (p->top_z - p->dep_cz) : p->top_z;
        double zl[200]; int depth_on = q.mc_depth_on || q.stepdown > 1e-6;
        int nz = q.mc_depth_nf > 0 && depth_on ? hst_depths(&q, zl, 200, NULL, NULL) : zlevels(&q, zl, 200);
        if (!depth_on) { nz = 1; zl[0] = q.final_z; }
        double top = q.top_z, ze = v[n - 1].z + (zl[0] - z0);  /* start_top_of_stock raised to the chain's end at the first level */
        if (ze > top + CAM_SYS_TOL) top = ze;
        int closed = hypot(v[n - 1].x - v[0].x, v[n - 1].y - v[0].y) < g_nc_tol;
        if (!closed && nz > 1 && p->mc_depth_on && p->mc_keep_down) { free(v); snprintf(op->error, sizeof op->error, "3D contour: keep tool down between depth cuts on an open chain (FUN_180045770 / FUN_180053e70 for 3D) is not read (ours: refused)"); return 0; }
        double zf, zr, zc; ht3(p, top, zl[0] + v[0].z - z0, &zf, &zr, &zc);
        for (int k = 0; k < nz; k++) {
            double off = zl[k] - z0, sz = v[0].z + off;
            if (k == 0) {                                      /* the chain's approach (pass(): clearance first, else across at the retract height) */
                if (!any) mv(b, 1, v[0].x, v[0].y, zc, 0);
                else if (zr > zf) { V3 e = lastp(b); if (e.z < zr - CAM_SYS_TOL) mv(b, 1, e.x, e.y, zr, 0); mv(b, 1, v[0].x, v[0].y, zr, 0); }
                mv(b, 1, v[0].x, v[0].y, fmax(zf, sz), 0);
                mv(b, 0, v[0].x, v[0].y, sz, p->plunge_feed);
            } else if (closed) {                               /* the next copy starts where this one ended: no retract (l.6162) */
                V3 e = lastp(b); if (e.z > sz + 1e-9) mv(b, 0, v[0].x, v[0].y, sz, p->plunge_feed);
            } else {
                double r2f, r2r, r2c; V3 e = lastp(b);
                double ref = zl[k - 1] + v[0].z - z0; if (e.z - ref > CAM_SYS_TOL) ref = e.z;   /* l.6672 */
                ht3(p, (p->ht_inc & 7) ? ref : top, ref, &r2f, &r2r, &r2c);
                g_hzf = r2f; retract_up(b, p, r2r); g_hzf = NAN;
                if (r2r > r2f) mv(b, 1, v[0].x, v[0].y, r2r, 0);
                mv(b, 1, v[0].x, v[0].y, fmax(r2f, sz), 0);
                mv(b, 0, v[0].x, v[0].y, sz, p->plunge_feed);
            }
            for (int i = 1; i < n; i++) {
                int from = b->n; mv(b, 0, v[i].x, v[i].y, v[i].z + off, p->feed);
                if (v[i].arc) { int j = i; while (j + 1 < n && v[j + 1].arc == v[i].arc) { j++; mv(b, 0, v[j].x, v[j].y, v[j].z + off, p->feed); }
                                mb_tag_arc(b, from, v[i].cx, v[i].cy, v[i].cw); i = j; }
            }
        }
        {   V3 e = lastp(b); double ref = zl[nz - 1] + v[0].z - z0; if (e.z - ref > CAM_SYS_TOL) ref = e.z;   /* the chain's end: clear_move */
            double r2f, r2r, r2c; ht3(p, (p->ht_inc & 7) ? ref : top, ref, &r2f, &r2r, &r2c);
            g_hzf = r2f; retract_up(b, p, r2c); g_hzf = NAN; zc_last = r2c; }
        any = 1; free(v);
    }
    if (!any) { snprintf(op->error, sizeof op->error, "no usable chains"); return 0; }
    V3 e = lastp(b); if (e.z < zc_last - CAM_SYS_TOL) mv(b, 1, e.x, e.y, zc_last, 0);
    return 1;
}
/* Smoothing (g_ps_arc): 1 when Mastercam's contour NCI holds arc records that are untagged chords here - a chain arc (its offset or the
   chain itself is an arc), corner rounding / break / optimize (ct_offset: arcs of radius r / b), or a corner the compensation rolls:
   ccomp_pock_from_ctour's minimum fillet angle set_min_fil_angle(0x5d3) - 0 None = pi, 1 Sharp = 0.785398, 2 All = 0.01 (ct_offset's
   comment) - at an outside corner of the offset side (chain_offset's / offset_open's convexity rule), for an offset of either sign the
   passes use (rs / fs: the largest rough / finish spacing added) */
static int ps_corner_rolls(const CamParams *p, const Chain *ch, const double *xy, int n, double sgn) {
    const double fil = p->roll == 2 ? 0.01 : p->roll == 1 ? 0.785398 : M_PI;
    CPath one = { (double *)xy, n };
    if (ch->closed) {
        int side = p->side, ccw = cl_area(&one) > 0;
        if (p->side_rel) side = (side == 1) == ccw ? SIDE_INSIDE : SIDE_OUTSIDE;
        double delta = sgn * (side == SIDE_OUTSIDE ? 1 : -1);
        int right = (ccw ? 1 : -1) * (delta > 0 ? 1 : -1) > 0;
        for (int i = 0; i < n; i++) {
            double vx = xy[2 * i], vy = xy[2 * i + 1], ax = 0, ay = 0, bx = 0, by = 0, l1 = 0, l2 = 0;
            for (int k = 1; k < n && l1 < 1e-9; k++) { int ip = (i - k + n) % n; ax = vx - xy[2 * ip]; ay = vy - xy[2 * ip + 1]; l1 = hypot(ax, ay); }
            for (int k = 1; k < n && l2 < 1e-9; k++) { int ip = (i + k) % n; bx = xy[2 * ip] - vx; by = xy[2 * ip + 1] - vy; l2 = hypot(bx, by); }
            if (l1 < 1e-9 || l2 < 1e-9) continue;
            double cr = (ax * by - ay * bx) / (l1 * l2), tang = atan2(cr, (ax * bx + ay * by) / (l1 * l2));
            if ((cr > 0) == right && fabs(cr) > 1e-12 && fabs(tang) >= fil - 1e-7) return 1;
        }
        return 0;
    }
    double d = p->side == 1 ? sgn : -sgn, ptx = 0, pty = 0; int have = 0;
    for (int i = 0; i + 1 < n; i++) {
        double tx = xy[2 * i + 2] - xy[2 * i], ty = xy[2 * i + 3] - xy[2 * i + 1], L = hypot(tx, ty);
        if (L < 1e-12) continue;
        tx /= L; ty /= L;
        if (have) {
            double turn = ptx * ty - pty * tx, tang = atan2(turn, ptx * tx + pty * ty);
            if (fabs(turn) < 1e-6 && ptx * tx + pty * ty < 0) return 1;   /* doubling back: a half turn around the end */
            if ((turn < 0) == (d > 0) && fabs(turn) > 1e-9 && fabs(tang) >= fil - 1e-7) return 1;
        }
        ptx = tx; pty = ty; have = 1;
    }
    return 0;
}
static int ps_contour_arcs(const McamFile *f, const CamOp *op, const CamParams *p, const CtChain *cc, double R, double rs, double fs, int taper) {
    for (int ci = 0; ci < op->nchains; ci++) {
        const Chain *ch = &op->chains[ci]; if (cc[ci].n < 2 || cam_chain_is_point(f, ch)) continue;
        for (int j = 0; j < ch->n; j++) if (f->ents[ch->ent[j]].type == E_ARC) return 1;
    }
    if (p->side == SIDE_ON) return 0;
    if (ct_corners(p)) return 1;
    double d0 = R + p->stock_xy, d1 = d0 + rs + fs;
    for (int ci = 0; ci < op->nchains; ci++) {
        const Chain *ch = &op->chains[ci]; if (cc[ci].n < 2 || cam_chain_is_point(f, ch)) continue;
        if ((d1 > 0 || taper) && ps_corner_rolls(p, ch, cc[ci].xy, cc[ci].n, 1)) return 1;
        if ((d0 < 0 || taper) && ps_corner_rolls(p, ch, cc[ci].xy, cc[ci].n, -1)) return 1;
    }
    return 0;
}
#include "cam2d_remach_gen.c"   /* Contour type 5 Remachining (formats/contour_remachining.md) */
static int gen_contour(const McamFile *f, CamOp *op, MB *b) {
    ct_arcs_reset();                                           /* NS-3: this op's offset arcs */
    if (op->p.side == SIDE_ON)                                 /* on the chain: the pass is the chain itself (no offset), its arcs the chain's own */
        for (int ci = 0; ci < op->nchains; ci++) for (int k = 0; k < op->chains[ci].n; k++) {
            const Ent *E = &f->ents[op->chains[ci].ent[k]]; if (E->type != E_ARC) continue;
            int r = op->chains[ci].rev[k]; McceEnt x; memset(&x, 0, sizeof x); x.id = 'A';
            x.a[0] = E->c.x; x.a[1] = E->c.y; x.a[2] = E->r; x.a[3] = r ? E->a0 + E->sw : E->a0; x.a[4] = r ? -E->sw : E->sw;
            ct_arcs_add(&x);
        }
    CamParams *p = &op->p;
    g_tab_why = 0; g_tch_xy = NULL; g_tch_n = 0;
    double R = p->tool_dia / 2, zl[200];
    /* depth levels: mccore calc_depth_cuts (cam2d.c hst_depths, decomp/mccore_depths.c; the contour loop calls it,
       decomp/mcmill_ctour.c l.1546 / l.1552) with the finish cuts S16 0x878 / step 0x880; without finish cuts its levels
       are the even ones of zlevels */
    int depth_on = p->mc_depth_on || p->stepdown > 1e-6;
    int nz = p->mc_depth_nf > 0 && depth_on ? hst_depths(p, zl, 200, NULL, NULL) : zlevels(p, zl, 200), any = 0;
    if (p->contour_type == 1) {                                /* 2D chamfer: one pass at the chamfer depth */
        double ztip;
        if (!chamfer_geometry(p, &R, &ztip, op->error, sizeof op->error)) return 0;
        if (p->side == SIDE_ON) R = 0;                         /* compensation off: the tool centre follows the chain */
        nz = 1; zl[0] = ztip;
    }
    /* compensation type (record 0x5d0): the offset includes D/2 only when cc_in_computer (0 Computer, 2 Wear, 3 Reverse wear;
       FUN_18005b340 decomp/mcmill_ctour.c l.1643-1648); Control (1) cuts along the chain (+ stock / multipass spacings) */
    if (p->side != SIDE_ON && p->contour_type == 0 && p->ct_comp == 1) R = 0;
    /* 2D chamfer (FUN_180053480: type 2 / 3): the offset is (cc_in_computer ? D/2 : 0) + dVar39 (l.1643-1655), dVar39 = tip
       offset x tan(half angle) + tip diameter / 2 - D/2 (l.1101-1115) = R (chamfer_geometry) - D/2: Wear / Reverse wear keep R, Control
       cuts at R - D/2 (on the material side of the chain; the control adds the tool radius back) */
    if (p->side != SIDE_ON && p->contour_type == 1 && p->ct_comp == 1) R -= p->tool_dia / 2;
    int ccon = p->side != SIDE_ON && (p->contour_type == 0 || p->contour_type == 1) && p->ct_comp >= 1 && p->ct_comp <= 3;
    /* ours (safety): with Control the program runs on the chain and the G40 move (the pass's last, FUN_180056be0) leaves the
       compensated path - LinuxCNC applies G40 to that block's own move (interp_convert.cc convert_cutter_compensation_off): it
       must be an exit line away from the part, not the last cut. Refused without the exit lead's line */
    if (ccon && p->ct_comp == 1 && !(p->lead_on && p->lead_out_on && p->lead_xlen > (g_nc_tol > 0 ? g_nc_tol : 1e-4))) {
        snprintf(op->error, sizeof op->error, "cutter compensation in control needs a Lead In/Out exit line (the G40 move) - ours, see formats/post.md"); return 0; }
    int nfin = p->side == SIDE_ON || p->contour_type == 1 ? 0 : (p->finish_n > 0 ? p->finish_n : 0);
    int taper = ct_tapered(p);
    /* XY multipasses, decoded from mcmill's contour loop (decomp/mcmill_ctour.c l.1506-1700; op +0x840.. = record
       0x8a8..): per depth the pass count is the rough count (S16 0x8aa; 1 when multipasses are off), plus the finish
       count (S16 0x8b8) at the last depth or at every depth when ctour_prm +8 (record 0x1230) is set; a depth with
       no pass is skipped. Before each pass the remaining rough count is decremented while it is >= 1, else the
       remaining finish count; the pass offset = remaining finish x finish spacing (0x8c0) + remaining rough x rough
       spacing (0x8b0) + R (computer compensation) + wall stock (0x2e0; the tapered stock above with tapered walls).
       Warning 0x93b rule: depth cuts on (0x868), finish not at every depth, multipasses on, rough 0, finish > 0 -> rough 1
       at spacing 0, finish - 1. Ops made here (no record): rough 1, finish = finish_n (mcam_saveops.c writes them that way). */
    CamParams q = *p;
    CamParams pr = q;                                          /* per pass: which side the leads go (chain_offset) */
    p = &pr;
    /* 3D chains (check_chains_3d sets ctour_3d for the whole op when one chain is not flat): Remachining first (bnci_ctour l.400), then
       the narrow case FUN_18005ef90 (gen_contour3d: incremental depth, compensation Off, no leads, no wall stock - bnci_ctour
       decomp/mcmill_ctour.c l.413-421); every other 3D case runs the contour loop with connect_3d_moves (decompiled 2026-11-08,
       formats/contour_3d_tabcutoff.md 1), which works on the ctour records' own chain entity / parameter (+0x28 / +0x30, from
       ccomp_pock_from_ctour) that the offset here (Clipper) does not keep - refused */
    {   int c3 = 0, spl = 0;
        for (int ci = 0; ci < op->nchains; ci++) if (!op->chains[ci].solid_idn && cam_chain_3d(f, &op->chains[ci])) c3 = 1;
        for (int ci = 0; ci < op->nchains; ci++) for (int k = 0; k < op->chains[ci].n; k++) if (f->ents[op->chains[ci].ent[k]].type == E_SPLINE) spl = 1;
        if (c3 && pr.ct_rtype != 5) {
            if (p->dep_inc && p->side == SIDE_ON && !p->lead_on && fabs(p->stock_xy) <= CAM_SYS_TOL) return gen_contour3d(f, op, b);
            snprintf(op->error, sizeof op->error, "a 3D chain with %s: Mastercam's 3D contour through connect_3d_moves needs the offset records' chain entity / parameter (ccomp_pock_from_ctour), not kept here - NOT PORTED",
                     !p->dep_inc ? "absolute depth" : p->side != SIDE_ON ? "compensation" : p->lead_on ? "Lead In/Out" : "wall stock");
            return 0; }
        if (spl) { snprintf(op->error, sizeof op->error, "this operation type does not take spline curves here (NOT PORTED; Engrave does, and the 3D contour)"); return 0; }
    }
    if ((pr.ct_rtype == 1 || pr.ct_rtype == 3) && (pr.tabs > 0 || (pr.lead_on && (pr.mc_lead_dz[0] > 0 || pr.mc_lead_dz[1] > 0 || pr.mc_lead_dz[2] > 0 || pr.mc_lead_dz[3] > 0)))) {
        snprintf(op->error, sizeof op->error, "contour type 3D / 3D chamfer on flat chains with tabs or lead heights: the link height test of 3D types (FUN_180045770 l.6672) is not ported"); return 0; }
    if (pr.ct_rtype == 5 && pr.contour_type == 0) return gen_remach(f, op, b, &pr, zl, nz);   /* bnci_ctour -> FUN_1800550c0 */
    if (pr.ct_rtype != 0 && pr.ct_rtype != 1 && pr.ct_rtype != 2 && pr.ct_rtype != 3 && pr.ct_rtype != 4 && pr.ct_rtype != 6) {   /* Remachining */
        static const char *tn[7] = { "2D", "3D", "2D chamfer", "3D chamfer", "Ramp", "Remachining", "Oscillate" };
        snprintf(op->error, sizeof op->error, "contour type %s: its engine is NOT DECODED here (not generated)", pr.ct_rtype >= 0 && pr.ct_rtype < 7 ? tn[pr.ct_rtype] : "?");
        return 0; }
    /* Multi Passes spindle speed override (0x1465 / 0x1460 = ctour_prm +0x23d / +0x238): the contour loop swaps op +0x328 for the finish
       passes (FUN_18005b340 decomp/mcmill_ctour.c l.1744-1748, restored l.1843-1845; the depth writer FUN_180045770 l.6968-6973), but
       the only reader of op +0x328 in the pass writer FUN_180056be0 (l.8122-8127, iStack_12ae4) feeds bnci_wr_4 (l.9779-9781) only for a
       chain point's own "change at point" speed (record flag 0x20, l.8551-8554 / 8909-8912; not made here); the motion writers
       gr_lin_mill / ct_lin_mill / ll_lin_move (mccore, decompiled 2026-11-07) carry no speed. So the override writes nothing: no
       refusal (until 2026-11-07 the op was refused); mp_fin_rpm is kept for the dialog / saving only */
    if (pr.tabs > 0 && pr.tab_rec) {                           /* Tabs 2834 with a record (formats/contour_100_plan.md 2) */
        if (pr.tab_square) {                                   /* make_geo_tabs (bnci_ctour l.343-346): per chain, the points on it replace its tabs */
            double nxy[2 * CAM_TAB_MAX], nown[5 * CAM_TAB_MAX]; unsigned char ned[CAM_TAB_MAX]; int nn = 0;
            for (int ci = 0; ci < op->nchains && nn < CAM_TAB_MAX; ci++) {
                const Chain *ch = &op->chains[ci]; if (ch->solid_idn > 0) continue;
                int k = ct_square_tabs(f, &pr, ch, nxy + 2 * nn, CAM_TAB_MAX - nn);
                if (k < 0) { snprintf(op->error, sizeof op->error, "tabs: a square-point tab moved onto a spline (FUN_1802256c0's spline branch is not ported)"); return 0; }
                if (k > 0) { memset(ned + nn, 0, (size_t)k); nn += k; continue; }   /* made by make_geo_tabs: the op's values */
                for (int t = 0; t < pr.ntab_xy && nn < CAM_TAB_MAX; t++) {   /* no point on this chain: its stored tabs stay */
                    double qx = pr.tab_xy[2 * t], qy = pr.tab_xy[2 * t + 1], bd = 1e300; int bc = -1;
                    for (int c2 = 0; c2 < op->nchains; c2++) { const Chain *c3 = &op->chains[c2];
                        for (int j = 0; j < c3->n; j++) { const Ent *e = &f->ents[c3->ent[j]]; if (e->type != E_LINE && e->type != E_ARC) continue;
                            double u, x, y; if (e->type == E_LINE) { double ex = e->b.x - e->a.x, ey = e->b.y - e->a.y, L2 = ex * ex + ey * ey; u = L2 > 0 ? ((qx - e->a.x) * ex + (qy - e->a.y) * ey) / L2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u; }
                            else { double a = atan2(qy - e->c.y, qx - e->c.x) - e->a0; a = fmod(a, 2 * M_PI); if (e->sw > 0 && a < 0) a += 2 * M_PI; if (e->sw < 0 && a > 0) a -= 2 * M_PI; u = a / e->sw; u = u < 0 ? 0 : u > 1 ? 1 : u; }
                            ent_pt_t(e, u, &x, &y); double d = hypot(x - qx, y - qy); if (d < bd) { bd = d; bc = c2; } } }
                    if (bc == ci) { nxy[2 * nn] = qx; nxy[2 * nn + 1] = qy; ned[nn] = pr.tab_ed[t]; memcpy(nown + 5 * nn, pr.tab_own + 5 * t, 5 * sizeof(double)); nn++; }
                }
            }
            memcpy(pr.tab_xy, nxy, sizeof(double) * 2 * nn); pr.ntab_xy = nn;
            memcpy(pr.tab_ed, ned, (size_t)nn); memcpy(pr.tab_own, nown, sizeof(double) * 5 * nn);
            if (getenv("MCAM_TAB_DEBUG")) for (int t = 0; t < pr.ntab_xy; t++) fprintf(stderr, "square tab %d: %.6f %.6f own %d w %g\n", t, pr.tab_xy[2 * t], pr.tab_xy[2 * t + 1], pr.tab_ed[t], pr.tab_own[5 * t]);   /* ours (tests) */
        }
        if (!pr.tab_auto && !pr.ntab_xy) pr.tabs = 0;          /* Manual with no tab placed (or found): none */
        if (!pr.tab_auto) {                                    /* the stored tabs' own values (cam2d.h tab_ow): mccore edit_operation
                                                                  (decomp/mccore_edit_operation.c l.3022-3046) gives Manual tabs the op's
                                                                  width / thickness / Full / Ramp / ramp angle on an edit of the op - a tab
                                                                  not edited (+0x110 = 0), or every tab with "Overwrite edited tabs" 0x13f1;
                                                                  the others keep theirs and the tab writer (FUN_180056be0) cuts each with
                                                                  its own. Only when the edit changed the tab block (cam_tabprm_edited);
                                                                  cut_path has one set of tab values, so a tab whose own differ is refused */
            int edited = cam_tabprm_edited(f, &pr);
            if (getenv("MCAM_TAB_DEBUG")) fprintf(stderr, "tab block edited %d ow %d\n", edited, pr.tab_ow);   /* ours (tests) */
            for (int t = 0; t < pr.ntab_xy; t++) {
                if (!(pr.tab_ed[t] & 1) || (edited && (!(pr.tab_ed[t] & 2) || pr.tab_ow))) continue;   /* the op's values */
                const double *ow = pr.tab_own + 5 * t;
                if (fabs(ow[0] - pr.tab_w) > 1e-9 || fabs(ow[1] - pr.tab_h) > 1e-9 || (ow[3] != 0) != (pr.tab_full != 0)
                    || (ow[4] != 0) != (pr.tab_ramp != 0) || (pr.tab_ramp && fabs(ow[2] - pr.tab_ang) > 1e-9)) {
                    snprintf(op->error, sizeof op->error, "tabs: tab %d has its own size / type (%s in Mastercam): not generated%s", t + 1,
                             pr.tab_ed[t] & 2 ? "edited" : "saved", !(pr.tab_ed[t] & 2) ? "" : pr.tab_ow ? " (edit the op: the op's values)" : " (Overwrite edited tabs + edit the op: the op's values)");
                    return 0;
                }
            }
        }
        if (pr.tab_auto) {                                     /* made again from the chains at every regeneration (add_auto_tabs, after make_geo_tabs) */
            pr.ntab_xy = 0;
            for (int ci = 0; ci < op->nchains && pr.ntab_xy < CAM_TAB_MAX; ci++)
                pr.ntab_xy += ct_auto_tabs(f, &pr, &op->chains[ci], pr.tab_xy + 2 * pr.ntab_xy, CAM_TAB_MAX - pr.ntab_xy);
            if (!pr.ntab_xy) pr.tabs = 0;                      /* no chain got tabs */
            if (getenv("MCAM_TAB_DEBUG")) fprintf(stderr, "auto tabs: n %d bydist %d maxd %g all %d sx %g sy %g w %g\n", pr.tab_n, pr.tab_bydist, pr.tab_maxd, pr.tab_all, pr.tab_sx, pr.tab_sy, pr.tab_w);
            if (getenv("MCAM_TAB_DEBUG")) for (int t = 0; t < pr.ntab_xy; t++) fprintf(stderr, "auto tab %d: %.6f %.6f\n", t, pr.tab_xy[2 * t], pr.tab_xy[2 * t + 1]);   /* ours (tests) */
        }
    }
    int nr = 1, nf = nfin, fin_all = 0; double rs = 0, fs = p->finish_step;
    if (p->ct_mp_rec && p->mc_mp_on) {
        nr = p->mc_mp_nr > 0 ? p->mc_mp_nr : 0; rs = p->mc_mp_rs; fin_all = p->ct_fin_all;
        if (p->mc_depth_on && !fin_all && nr == 0 && nf > 0) { nr = 1; rs = 0; nf--; }
    }
    if (nfin == 0 && nr == 0) nr = 1;                          /* compensation off / chamfer: one pass on the offset */
    CtChain *cc = op->nchains ? calloc((size_t)op->nchains, sizeof *cc) : NULL;
    if (op->nchains && !cc) { snprintf(op->error, sizeof op->error, "out of memory"); return 0; }
    for (int ci = 0; ci < op->nchains; ci++) {
        Chain *ch = &op->chains[ci]; CtChain *k = &cc[ci];
        k->n = cam_chain_polyline(f, ch, p->tol, &k->xy, &k->z);
        if (k->n < 2) continue;
        k->sx = k->xy[0]; k->sy = k->xy[1];                    /* entry: chain start, or the first entity's midpoint */
        if (ch->closed && op->p.lead_mid && lead_any(&op->p)) {   /* the op's own leads (an engrave type's too, as before) */
            const Ent *e0 = &f->ents[ch->ent[0]];
            if (e0->type == E_LINE) { k->sx = (e0->a.x + e0->b.x) / 2; k->sy = (e0->a.y + e0->b.y) / 2; }
            else { double a = e0->a0 + e0->sw / 2; k->sx = e0->c.x + e0->r * cos(a); k->sy = e0->c.y + e0->r * sin(a); }
        }
        start_tangent(f, ch, k->sx, k->sy, &k->ttx, &k->tty);
    }
    ofile = f;
    if ((pr.ct_rtype == 4 || pr.ct_rtype == 6) && pr.contour_type == 0) {   /* Ramp / Oscillate contour (ct_ramp_chain / ct_osc_chain) */
        int err = 0, osc = pr.ct_rtype == 6;
        g_ps_map = 0;                                          /* Smoothing: the drivers' markers (FUN_1801ff170 l.350924 / 351258) not mapped */
        const char *rw = NULL;                                 /* control compensation: R = 0 above (the drivers' cc_in_computer,
                                                                  l.350841-350848) and the codes rmp_cc_tag (2026-11-07; refused before) */
        /* the passes (Multi Passes, ct_ramp_chain's comment): n1 in the first loop, nfl finish laps after */
        /* op +0x840 on as the page shows it (cui_opdlg.c pf_mp_on): the finish passes finish_n / finish_step (0x8b8 / 0x8c0), the rough
           ones 0x8aa / 0x8b0 and "at every depth" 0x1230 from the record (an op made here: 1 rough pass, no spacing - as gen_contour) */
        int mp = pr.finish_n > 0 || (pr.ct_mp_rec && pr.mc_mp_on), rec = pr.ct_mp_rec && pr.mc_mp_on;
        int nr = rec ? (pr.mc_mp_nr > 0 ? pr.mc_mp_nr : 0) : 1, nf = mp ? (pr.finish_n > 0 ? pr.finish_n : 0) : 0, fa = rec && pr.ct_fin_all;
        double rs = rec ? pr.mc_mp_rs : 0, fs = pr.finish_step;
        int n1 = mp ? nr + (fa ? nf : 0) : 1, nfl = mp && !fa ? nf : 0;
        double d36 = mp && !fa ? nf * fs : 0;
        if (n1 <= 0) rw = !fa && nr == 0 && nf > 0 ? "no rough pass (warning 0x93b)" : "no pass (warning 0x966)";
        if (!rw && mp && pr.side == SIDE_ON) rw = "Multi Passes with compensation off are not handled (ours: the passes' side 0x56a is not kept)";
        for (int ci = 0; ci < op->nchains && !rw; ci++) {
            Chain *ch = &op->chains[ci]; CtChain *c = &cc[ci];
            if (c->n < 2) continue;
            CPath *kdb = NULL; CPaths kp = { 0 };
            if (!osc && ch->closed && pr.mc_mp_kd && nfl > 0) {    /* FUN_1801fe310 bound for FUN_1801fe3e0 */
                const char *why = NULL; int lr = pr.lead_right;
                double d35 = mp && fs > g_nc_tol ? fs : 0.01 * p->tool_dia;
                kp = ct_offset(p, ch, c->xy, c->n, R + p->stock_xy + d35, c->sx, c->sy, c->ttx, c->tty, &lr, &why);
                if (!why && kp.n == 1) kdb = &kp.p[0];
            }
            RmpCarry cr = { 0, 0 };
            int r = nr, dl = fa ? nf : 0, osc_rev = 0;
            for (int k = 1; k <= n1 + nfl && !rw && !err; k++) {
                int fin = 0, ffd;
                double off;
                if (k <= n1) {                                       /* the first loop */
                    ffd = mp && r == 0;
                    if (r < 1) dl--; else r--;
                    off = r * rs + R + p->stock_xy + d36 + dl * fs;
                } else {                                             /* the finish loop */
                    int kk = k - n1; ffd = 1; fin = kk == nfl ? 3 : 2;
                    off = (nfl - kk) * fs + R + p->stock_xy;
                }
                const char *why = NULL;
                CPaths pp = ct_offset(p, ch, c->xy, c->n, off, c->sx, c->sy, c->ttx, c->tty, &pr.lead_right, &why);
                if (why) { snprintf(op->error, sizeof op->error, "%s", why); cl_free(&pp); err = -4; break; }
                if (pp.n != 1) { cl_free(&pp); rw = "the offset gives several loops (ours: refused)"; break; }
                int bad;
                if (osc) {
                    bad = ct_osc_chain(b, &pr, &pp.p[0], ch->closed, !any, fin ? osc_rev : 0, ffd, fin && pr.mc_mp_kd) < 0;
                    osc_rev = !ch->closed;                           /* +3 = open after each call */
                } else bad = ct_ramp_chain(b, &pr, &pp.p[0], ch->closed, !any, fin, ffd, &cr, kdb, fin && pr.mc_mp_kd) < 0;
                if (bad) rw = osc ? "a zero distance, depth or length (warning 0xa9c)" : "the ramp gives no move (a zero slope or perimeter: create_ramped_ctour fails)";
                any = 1; cl_free(&pp);
            }
            cl_free(&kp);
            if (err) break;
        }
        for (int ci = 0; ci < op->nchains; ci++) free(cc[ci].xy);
        free(cc);
        if (err == -4) return 0;
        if (rw) { snprintf(op->error, sizeof op->error, "%s contour: %s", osc ? "Oscillate" : "Ramp", rw); return 0; }
        if (!any) { snprintf(op->error, sizeof op->error, "no usable chains"); return 0; }
        V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance, 0);
        return 1;
    }
    /* the order: by contour (each chain at all its depths) or By depth (ctour_prm +0xf0 = record 0x1318: the sort
       FUN_1800447e0, decomp/mcmill_all.c l.48183, keys depth first - deepest last - then the chain, then the pass) */
    int bydepth = p->dc_bydepth && op->nchains > 1, err = 0;
    /* Tab Cutoff (cut_tab_cutoff): 1 after all chains (the loop after the chains, decomp/mcmill_ctour.c l.2025-2582), 2 after each
       chain (l.1955-2010, while local_125d); "Cut tabs with first finish pass" 0x13f4 with mode 2 and Multi Passes (op +0x840, rough
       0x842 > 0 with spacing 0x848 > tol or finish 0x850 > 0 with 0x858 > tol): the finish passes (no rough pass left, l.1705-1723)
       are cut without tabs and local_125d = 0 - no cutoff for this or any later chain */
    int tcut = pr.tabs > 0 && pr.tab_rec ? pr.tab_cut : 0, t125d = 1;
    int mp_act = p->mc_mp_on && ((p->mc_mp_nr > 0 && p->mc_mp_rs > CAM_SYS_TOL) || (p->mc_mp_nf > 0 && p->mc_mp_fs > CAM_SYS_TOL));
    /* Tab Cutoff as a separate operation (3): the contour itself cuts its tabs (its loop has no cutoff for mode 3, l.1955-1957 / l.2025) and
       bnci_ctour's FUN_180044170 (decomp/mcmill_ctour.c l.3905-4307) makes the 0x82 operation (cui_ops.c cutoff_children). The 0x82 op
       (bnci_cutoff, decomp/mcmill_all.c l.47562: bnci_ctour without FUN_180044170, ctour_prm = op +0x11c0) runs the same loop, where
       FUN_180056be0 is skipped for its passes (l.1753) and the cutoff writer runs after each chain whatever its own mode (l.1955-1957: opcode
       0x82 || mode 2 ..., no retract_move before it: that is mode 2's, l.1963; no "cut tabs with first finish pass", local_125d is mode 2's) */
    if (tcut == 3) tcut = 0;
    if (p->ct_cutoff_op) tcut = pr.tabs > 0 && pr.tab_rec ? 2 : 0;
    if (tcut && bydepth) { snprintf(op->error, sizeof op->error, "Tab Cutoff with depth cuts by depth: the order of the cutoff in the sort (FUN_1800447e0) is not read (ours: refused)"); free(cc); return 0; }
    if (tcut && pr.ct_comp == 1 && pr.side != SIDE_ON) { snprintf(op->error, sizeof op->error, "Tab Cutoff with control compensation (the writer then cuts the chain itself, bVar22) is not ported"); free(cc); return 0; }
    /* Wear / Reverse wear: the cutoff pass's cc codes by record (l.8164-8177, pass_cc_tag's comment) are not placed here - refused
       (2026-11-07; before, the cutoff was cut without them) */
    if (tcut && (pr.ct_comp == 2 || pr.ct_comp == 3) && pr.side != SIDE_ON) { snprintf(op->error, sizeof op->error, "Tab Cutoff with Wear / Reverse wear compensation: where the cutoff pass's G41 / G42 / G40 codes go (the writer's records 2 / 3 / last, decomp/mcmill_ctour.c l.8164-8177, while it writes only the spans) is NOT DECODED"); free(cc); return 0; }
    CPath *tlast = tcut ? calloc((size_t)op->nchains, sizeof *tlast) : NULL; double *toff = tcut ? calloc((size_t)op->nchains, sizeof *toff) : NULL;
    pass_cc = ccon;                                            /* reset after the loop (pass() also serves the pocket finish) */
    g_ps_map = !tcut;                                          /* Smoothing: the Tab Cutoff pass's marker (l.2343) is not mapped */
    g_ps_mb = b;                                               /* Smoothing: record what cam_smooth needs (g_ps_mb's comment) */
    g_ps_arc = ps_contour_arcs(f, op, &pr, cc, R, nr > 0 ? nr * rs : 0, nfin ? nf * fs : 0, taper);
    int nouter = bydepth ? nz : op->nchains, ninner = bydepth ? op->nchains : nz;
    /* point chains (FUN_18005b340 l.63026-63040 / l.63615-63630, l.63699-63701): a point chain is a rapid to the point (FUN_1800534a0 ->
       mccore rapid_xy_move 0x18089f250: at the clearance height when on, else the retract height when on, else the feed plane); with
       leads and Use entry / exit point (0x888 && (0x890 && 0x8c1 || 0x8d8 && 0x909)) it is kept for the next contour (local_1108, its
       depth local_1178) instead - a point already kept gets its rapid first; the contour's passes start / end there (pass: param_12
       = kept, entry when 0x890 && 0x8c1 && (the first depth || !0x8c0), exit when 0x8d8 && 0x909 && (the last depth || !0x908)); the
       kept point is used up after the contour's last pass at its last depth when 0x888 && 0x8c1 (l.62934-62937); one left at the end:
       its rapid (l.63700). Ours: refused with depth cuts by depth (the sort FUN_1800447e0 not read for points). */
    int xf = p->lead_xflags, pend = 0, hz_ci = -1; double ppx = 0, ppy = 0, ppz = 0, hz_z = 0;
    int ci_last = -1;                                          /* the op's last pass: the writer's end (l.6983-6995) clears at the op's heights */
    for (int ci = 0; ci < op->nchains; ci++) if (cc[ci].n >= 2 && !cam_chain_is_point(f, &op->chains[ci])) ci_last = ci;
    double zh = pr.mc_clr_on ? pr.clearance : (pr.tab_rise_z != 0 || pr.tab_rise_rc ? pr.tab_rise_z : pr.top_z + pr.feed_plane);
    for (int o = 0; o < nouter && !err; o++) for (int in = 0; in < ninner && !err; in++) {
        int ci = bydepth ? in : o, kz = bydepth ? o : in;
        Chain *ch = &op->chains[ci]; CtChain *c = &cc[ci];
        if (cam_chain_is_point(f, ch) && c->n >= 1) {
            if (bydepth) { err = -7; break; }
            if (kz != 0) continue;
            if (pend) mv(b, 1, ppx, ppy, zh, 0);
            if (!(p->lead_on && ((p->lead_in_on && (xf & 2)) || (p->lead_out_on && (xf & 8))))) mv(b, 1, c->xy[0], c->xy[1], zh, 0);
            else { pend = 1; ppx = c->xy[0]; ppy = c->xy[1]; ppz = c->z; }
            continue;
        }
        if (c->n < 2) continue;
        pass_chain = ch;
        int last = kz == nz - 1, rr = nr, ff = (last || fin_all) ? nf : 0, n_pass = rr + ff;
        int ffo = nf;                                          /* remaining finish count used in the offset */
        double stock = taper ? ct_taper_stock(p, c->z, zl[kz]) : p->stock_xy;
        for (int j = 0; j < n_pass && !err; j++) {
            int fin_pass = 0;
            if (rr >= 1) rr--; else { ffo--; fin_pass = 1; }
            double off = R + stock + (nfin ? ffo * fs : 0) + rr * rs;
            const char *why = NULL;
            CPaths pp = ct_offset(p, ch, c->xy, c->n, off, c->sx, c->sy, c->ttx, c->tty, &pr.lead_right, &why);
            if (why) { snprintf(op->error, sizeof op->error, "%s", why); err = -4; cl_free(&pp); break; }
            for (int li = 0; li < pp.n && !err; li++) {
                int wt = fin_pass || p->contour_type == 0;
                if (tcut == 2 && p->tab_cut_ffp && mp_act && fin_pass && !p->ct_cutoff_op) { wt = 0; t125d = 0; }
                if (wt && ch->closed && p->tabs > 0 && lead_any(p)) {   /* ours: an adjusted path is cut without tabs - refused */
                    PLine w = { 0 }; double a0, a1, a2, a3; int adj = adj_path(p, &pp.p[li], 1, &w, &a0, &a1, &a2, &a3); free(w.xy);
                    if (adj) { err = adj == -2 ? -2 : -3; break; }
                }
                /* "Enter on first depth cut only" / "Exit on last depth cut only" (0x928 / 0x970): FUN_180051990 drops the side unless
                   param_8 && param_10 / param_9 && param_11 (l.55976-56018); the contour loop FUN_18005b340 passes them as (depth == 1),
                   (depth == the depth count), (pass == 1), (pass == the pass count) (mcmill.dll 0x18005ccb7-0x18005cd41, read from the
                   disassembly: Ghidra keeps 4 of FUN_1800518b0's 18 arguments) - the entry on the first depth's first pass, the exit on
                   the last depth's last pass */
                pr.lead_skip = ((p->lead_xflags & 1) && !(kz == 0 && j == 0) ? 1 : 0) | ((p->lead_xflags & 4) && !(kz == nz - 1 && j == n_pass - 1) ? 2 : 0);
                /* the overlap (FUN_180050a10) goes with the exit: local_197 holds the same (param_9 && param_11) || !0x970 test
                   (l.56019-56025) - BLOCK op 1: the rough pass ends at the start, the last pass overlaps */
                double ovl0 = pr.lead_ovl; if (pr.lead_skip & 2) pr.lead_ovl = 0;
                pr.kd_ok = (j >= 1 && p->mc_mp_on && p->mc_mp_kd) || (kz >= 1 && p->mc_depth_on && p->mc_keep_down);
                pr.pt_in = pend && p->lead_on && p->lead_in_on && (xf & 2) && (kz == 0 || !(xf & 1));
                pr.pt_out = pend && p->lead_on && p->lead_out_on && (xf & 8) && (kz == nz - 1 || !(xf & 4));
                pr.pt_x = ppx; pr.pt_y = ppy; pr.pt_z = ppz;
                pr.hz_on = (p->ht_inc & 7) != 0;               /* ht_rel: incremental heights follow the depth just cut */
                pr.hz_in = hz_ci == ci ? hz_z : NAN;
                pr.hz_last = bydepth ? (j == n_pass - 1) : (kz == nz - 1 && j == n_pass - 1);
                pr.hz_opend = kz == nz - 1 && j == n_pass - 1 && ci == ci_last;
                pr.pass_last_depth = kz == nz - 1;
                double feed0 = pr.feed; if (fin_pass && p->mc_mp_on && p->mp_ff_on && p->mp_ff_v > 0) pr.feed = p->mp_ff_v;   /* finish passes: the multipass feed override */
                g_kd_xy = c->xy; g_kd_n = c->n; g_kd_closed = ch->closed; g_tch_xy = c->xy; g_tch_n = c->n;
                ps_mark();                                     /* Smoothing: bnci_wr_ctour_marker before the pass writer (l.1750) */
                {   MB scratch = { 0 };                            /* the 0x82 op: its passes are not written (l.1753) */
                    if (p->ct_cutoff_op) { if (b->n) { scratch.m = malloc((size_t)b->n * sizeof(Move)); scratch.cap = scratch.n = b->n; if (scratch.m) memcpy(scratch.m, b->m, (size_t)b->n * sizeof(Move)); else scratch.cap = scratch.n = 0; } }
                    err = pass(p->ct_cutoff_op ? &scratch : b, p, &pp.p[li], ch->closed, zl[kz], !any, wt); any = 1;
                    free(scratch.m); }
                hz_ci = ci; hz_z = zl[kz];
                if (tlast && kz == nz - 1 && j == n_pass - 1 && ch->closed) {   /* the chain's last path (local_100) */
                    if (pp.n != 1) { free(tlast[ci].xy); tlast[ci].xy = NULL; tlast[ci].n = -1; }
                    else { free(tlast[ci].xy); tlast[ci].n = pp.p[0].n; tlast[ci].xy = malloc((size_t)pp.p[0].n * 2 * sizeof(double)); memcpy(tlast[ci].xy, pp.p[0].xy, (size_t)pp.p[0].n * 2 * sizeof(double)); toff[ci] = off; }
                }
                pr.lead_skip = 0; pr.lead_ovl = ovl0; pr.kd_ok = 0; pr.feed = feed0; pr.pt_in = pr.pt_out = 0;
            }
            cl_free(&pp);
        }
        if (kz == nz - 1 && p->lead_on && (xf & 2)) pend = 0;   /* the kept point used up (l.62934) */
        pass_chain = NULL;
        if (!err && tcut == 2 && t125d && kz == nz - 1 && tlast && tlast[ci].n != 0)
            err = ct_cutoff(b, &pr, f, ch, &tlast[ci], toff[ci], zl[nz - 1]);
    }
    if (!err && pend && p->lead_on && (xf & 2)) mv(b, 1, ppx, ppy, zh, 0);   /* l.63699-63701 */
    if (!err && tcut == 1 && tlast) for (int ci = 0; ci < op->nchains && !err; ci++) if (tlast[ci].n != 0) {
        g_tch_xy = cc[ci].xy; g_tch_n = cc[ci].n;
        err = ct_cutoff(b, &pr, f, &op->chains[ci], &tlast[ci], toff[ci], zl[nz - 1]);
    }
    g_tch_xy = NULL; g_tch_n = 0;
    if (tlast) { for (int ci = 0; ci < op->nchains; ci++) free(tlast[ci].xy); free(tlast); free(toff); }
    for (int ci = 0; ci < op->nchains; ci++) free(cc[ci].xy);
    free(cc);
    p = &op->p;
    pass_cc = 0;
    if (err == -4) return 0;                                   /* op->error set (ct_offset) */
    if (err == -2) { snprintf(op->error, sizeof op->error, "out of memory"); return 0; }
    if (err == -6) { snprintf(op->error, sizeof op->error, "Use entry / exit point with Plunge after first move, Retract before last move or keep tool down is not generated here (ours: not read)"); return 0; }
    if (err == -7) { snprintf(op->error, sizeof op->error, "a point chain with depth cuts by depth is not generated here (ours: the sort FUN_1800447e0 is not read for points)"); return 0; }
    if (err == -3) { snprintf(op->error, sizeof op->error, "tabs with an overlap or adjust start / end of contour are not generated here (ours)"); return 0; }
    if (err == -5 && g_tab_why == 7) { snprintf(op->error, sizeof op->error, "tabs: a ramp tab crosses an entity end where Mastercam's carried height leaves the move's line (ctour entities not kept here): not generated"); return 0; }
    if (err == -5 && g_tab_why == 6) { snprintf(op->error, sizeof op->error, "tabs: a ramp tab over the path's end, over another tab or with an angle outside 0-90 is not generated (not decoded)"); return 0; }
    if (err == -5 && g_tab_why == 8) { snprintf(op->error, sizeof op->error, "Tab Cutoff: an exit lead at a tab ending off a chain line (FUN_18005afc0's chord from t - 0.06: the ctour entities are not kept here)"); return 0; }
    if (err == -5 && g_tab_why == 9) { snprintf(op->error, sizeof op->error, "Tab Cutoff: the chain's last pass is not one loop (ours: refused)"); return 0; }
    if (err == -5) { snprintf(op->error, sizeof op->error, "tabs: a tab over the start of the path is not generated (what the tab writer does there is NOT DECODED)"); return 0; }
    if (!any) { snprintf(op->error, sizeof op->error, "no usable chains"); return 0; }
    if (!b->n) { snprintf(op->error, sizeof op->error, "Tab Cutoff: no tab to cut off"); return 0; }   /* the 0x82 op without a span (ours: an empty NCI) */
    V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance, 0);
    /* user decision NS-4: the standard 2D contour (type 2D, not chamfer / engrave / ramp / oscillate / 3D / remachining, no Tab Cutoff
       passes) went through pass(), whose Move.sof follows FUN_180045770's blocks (formats/nci_save.md 7): its NCI may be saved */
    op->nci_blocks = pr.ct_rtype == 0 && pr.contour_type == 0 && !p->ct_cutoff_op && tcut == 0;
    return 1;
}

static int gen_pocket_v2(const McamFile *f, CamOp *op, MB *b);   /* cam2d_pock.c */
static int pk_entry_helix(const McamFile *f, CamOp *op, MB *b, double sx, double sy, double z, double z_first, int first);

typedef struct { CPath outer; CPath *holes; int nh; int level, parent, done; } PNode;

/* Zigzag pocket, as Mastercam's Pocket with cutting method zigzag (checked on A001): passes across the region shrunk
   by R + wall stock + finish passes, spaced evenly (floor(range / step) + 1 intervals) from the low side and linked
   at depth; entry by a helix that ends pointing at the first pass's start (descending at ramp_deg from helix_zclr above
   the top); then the finish pass(es) at the walls, entered like a contour (midpoint of the first entity, fitted leads). */
static int gen_pocket_zigzag(const McamFile *f, CamOp *op, MB *b, const CPaths *region) {
    CamParams *p = &op->p;
    double R = p->tool_dia / 2, zl[200];
    int nfin = p->finish_n > 0 ? p->finish_n : 0, nz = hst_depths(p, zl, 200, NULL, NULL);   /* calc_depth_cuts (cam2d.c hst_depths) */
    double rough0 = R + p->stock_xy + nfin * p->finish_step, step = p->tool_dia * p->stepover_pct / 100;
    if (step < p->tol * 4) step = p->tool_dia * 0.4;
    CPaths rr = cl_offset(region, -rough0, p->tol);
    double a = p->pocket_angle * M_PI / 180, ca = cos(a), sa = sin(a), lo = 1e300, hi = -1e300;
    for (int L = 0; L < rr.n; L++) for (int j = 0; j < rr.p[L].n; j++) { double v = -rr.p[L].xy[2 * j] * sa + rr.p[L].xy[2 * j + 1] * ca; if (v < lo) lo = v; if (v > hi) hi = v; }
    int nint = rr.n && hi > lo + 1e-9 ? (int)floor((hi - lo) / step + 1e-9) + 1 : 0;
    double fp = p->top_z + p->feed_plane, zr = fmax(fp, p->retract_z);
#define W(U, V, Z, RAPID, F) mv(b, RAPID, (U) * ca - (V) * sa, (U) * sa + (V) * ca, Z, F)
    for (int k = 0; k < nz && rr.n; k++) {
        double z = zl[k]; int fwd = 1, started = 0;
        for (int i = 0; i <= nint; i++) {
            double v = nint ? lo + (hi - lo) * i / nint : (lo + hi) / 2, ulo = 1e300, uhi = -1e300;
            if (i == 0) v += 1e-7; if (i == nint) v -= 1e-7;   /* the extreme passes touch the boundary */
            for (int L = 0; L < rr.n; L++) for (int j = 0; j < rr.p[L].n; j++) {
                const double *P = &rr.p[L].xy[2 * j], *Q = &rr.p[L].xy[2 * ((j + 1) % rr.p[L].n)];
                double pv = -P[0] * sa + P[1] * ca, qv = -Q[0] * sa + Q[1] * ca;
                if ((pv - v) * (qv - v) > 0 || pv == qv) continue;
                double t = (v - pv) / (qv - pv), uu = (P[0] * ca + P[1] * sa) + ((Q[0] * ca + Q[1] * sa) - (P[0] * ca + P[1] * sa)) * t;
                if (uu < ulo) ulo = uu; if (uu > uhi) uhi = uu;
            }
            if (ulo > uhi) continue;
            double ua = fwd ? ulo : uhi, ub = fwd ? uhi : ulo;
            if (!started) {                                   /* helix entry (decoded, cam2d_pock.c pk_entry_helix) */
                double sxw = ua * ca - v * sa, syw = ua * sa + v * ca;
                if (b->n) { V3 e = lastp(b); mv(b, 1, e.x, e.y, zr, 0); }
                if (!pk_entry_helix(f, op, b, sxw, syw, z, zl[0], !b->n)) {
                    if (op->error[0]) { cl_free(&rr); return 0; }
                    mv(b, 1, sxw, syw, b->n ? zr : p->clearance, 0); mv(b, 1, sxw, syw, fp, 0); mv(b, 0, sxw, syw, z, p->plunge_feed);
                }
                mv(b, 0, sxw, syw, z, p->feed); started = 1;
            } else W(ua, v, z, 0, p->feed);
            W(ub, v, z, 0, p->feed);
            fwd = !fwd;
        }
    }
#undef W
    cl_free(&rr);
    /* finish passes: contour passes (leads fitted, entry at the first entity's midpoint) at depth */
    for (int ci = 0; ci < op->nchains && nfin > 0; ci++) {
        Chain *ch = &op->chains[ci]; if (!ch->closed) continue;
        double *xy, zc; int n = cam_chain_polyline(f, ch, p->tol, &xy, &zc);
        if (n < 3) { free(xy); continue; }
        double sx = xy[0], sy = xy[1];
        if (p->lead_mid && lead_any(p)) {
            const Ent *e0 = &f->ents[ch->ent[0]];
            if (e0->type == E_LINE) { sx = (e0->a.x + e0->b.x) / 2; sy = (e0->a.y + e0->b.y) / 2; }
            else { double am = e0->a0 + e0->sw / 2; sx = e0->c.x + e0->r * cos(am); sy = e0->c.y + e0->r * sin(am); }
        }
        CamParams q = *p; q.side = SIDE_INSIDE; q.side_rel = 0; q.lead_right = 0;   /* inside, climb (CCW) */
        /* the pocket's finish leads: this program's measured model (entry / exit sides, lengths, radii, sweeps); the pocket's
           lead application (FUN_1801cd200 lead checks) is NOT DECODED, so perpendicular lines, ramp heights, overlap and
           adjust start / end are not taken, and the fit always runs (2793 hides the gouge check, FUN_180171320) */
        q.lead_in_perp = q.lead_out_perp = 0; q.lead_ovl = 0; q.adj_s = q.adj_e = 0; q.lead_gouge = 1;
        for (int d = 0; d < 4; d++) q.mc_lead_dz[d] = 0;
        for (int j = nfin - 1; j >= 0; j--) {
            double ttx, tty; start_tangent(f, ch, sx, sy, &ttx, &tty); ofile = f;
            CPaths fin = chain_offset(&q, ch, xy, n, R + p->stock_xy + j * p->finish_step, sx, sy, ttx, tty, &q.lead_right);
            for (int li = 0; li < fin.n; li++) (void)pass(b, &q, &fin.p[li], 1, p->final_z, !b->n, 0);   /* q: no adjusted path, cannot fail */
            cl_free(&fin);
        }
        free(xy);
    }
    if (!b->n) { snprintf(op->error, sizeof op->error, "pocket: the tool does not fit"); return 0; }
    V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance, 0);
    return 1;
}


static int pk_isl_zs(const McamFile *f, const CamOp *op, double *zs, int cap, int *nzs, int all);
static int gen_pocket(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    double R = p->tool_dia / 2, ztop, zl[200];
    if (p->pk_ok && (int16_t)(p->pk_p[0x13fa - 0x1228] | p->pk_p[0x13fb - 0x1228] << 8) == 3) {   /* GeneratePocketToolpath l.313523-313527: types 3 and 4 go on with an open chain; l.334927, l.334948: Remachining then runs FUN_1801ec3c0 on its tool-centre bound */
        for (int i = 0; i < op->nchains; i++) if (!op->chains[i].avoid && !op->chains[i].closed) { snprintf(op->error, sizeof op->error, "pocket Remachining on an open chain (process_open_pocket l.334948 -> FUN_1801ec3c0): NOT PORTED"); return 0; }
    }
    if (p->pk_ok && (int16_t)(p->pk_p[0x13fa - 0x1228] | p->pk_p[0x13fb - 0x1228] << 8) == 4) return gen_pocket_v2(f, op, b);   /* Open pocket: chains may be open (cam2d_pock_open.c) */
    CPaths raw = chain_paths(f, op, 1, &ztop);
    if (!raw.n) { cl_free(&raw); int open = 0; for (int i = 0; i < op->nchains; i++) if (!op->chains[i].closed && !op->chains[i].avoid && !cam_chain_is_point(f, &op->chains[i])) open = 1;   /* a single open chain: l.313523-313527 warning 0x93f, mcmillres 2367, as the gen_pocket_v2 test */
        double *zs = malloc(sizeof(double) * (size_t)(op->nchains + 1)); int nz2 = 0, flat = open ? 1 : pk_isl_zs(f, op, zs, op->nchains, &nz2, 0); free(zs);   /* check_chains_3d l.313530-313541, after the open test: 0x2309 (mccoreres 8969) */
        snprintf(op->error, sizeof op->error, "%s", open ? "Warning - not all boundaries are closed.  Select Open pockets or close chains" : !flat ? "Warning - the boundary does not lie in the construction plane" : "pocket needs closed chains"); return 0; }
    CPaths region = cl_union_evenodd(&raw);
    /* every pocket goes to the ported engine (cam2d_pock.c): zigzag too since decisions batch dec1 (formats/user_decisions.md
       B6-2: the decoded Pockzigr engine cam2d_pock_zz.c; A001 op 13 p95 0.031 -> 0.028 vs its NCI). PK_ZIGZAG_DECODED=0 (ours,
       testing / comparison only) keeps the former CORRELATED zigzag engine below */
    { const char *zd = getenv("PK_ZIGZAG_DECODED");
      if (p->pocket_no_rough || p->pocket_style != 1 || !(zd && !strcmp(zd, "0"))) { cl_free(&region); cl_free(&raw); return gen_pocket_v2(f, op, b); } }
    if (p->pk_ok && (int16_t)(p->pk_p[0x13fa - 0x1228] | p->pk_p[0x13fb - 0x1228] << 8) != 0) {   /* batch 6: pocket type 0x13fa != Standard:
                                                       NOT DECODED (as gen_pocket_v2; A001 op 13 holds 0) */
        cl_free(&region); cl_free(&raw); snprintf(op->error, sizeof op->error, "pocket type %d: NOT DECODED", (int16_t)(p->pk_p[0x13fa - 0x1228] | p->pk_p[0x13fb - 0x1228] << 8)); return 0; }
    if (p->pocket_style == 1) { int ok = gen_pocket_zigzag(f, op, b, &region); cl_free(&region); cl_free(&raw); return ok; }
    double step = p->tool_dia * p->stepover_pct / 100;
    if (step < p->tol * 4) step = p->tool_dia * 0.4;
    /* offset levels from the wall inwards: level 0 = finish loop at R + stock */
    PNode *nodes = NULL; int nn = 0;
    int nfin = p->finish_n > 0 ? p->finish_n : 0;
    double rough0 = R + p->stock_xy + nfin * p->finish_step;
    CPaths allowed = cl_offset(&region, -(R + p->stock_xy), p->tol);
    for (int lev = 0; lev < 10000; lev++) {
        CPaths L = cl_offset(&region, -(rough0 + lev * step), p->tol);
        if (!L.n) { cl_free(&L); break; }
        int first = nn;
        for (int i = 0; i < L.n; i++) if (cl_area(&L.p[i]) > 0) {          /* outer loops (CCW) */
            nodes = realloc(nodes, (nn + 1) * sizeof(PNode)); memset(&nodes[nn], 0, sizeof(PNode));
            nodes[nn].outer.n = L.p[i].n; nodes[nn].outer.xy = L.p[i].xy; L.p[i].xy = NULL;
            nodes[nn].level = lev; nodes[nn].parent = -1; nn++;
        }
        for (int i = 0; i < L.n; i++) if (L.p[i].xy && cl_area(&L.p[i]) < 0) { /* holes (CW) -> owning outer */
            for (int k = first; k < nn; k++) if (pip(&nodes[k].outer, L.p[i].xy[0], L.p[i].xy[1])) {
                nodes[k].holes = realloc(nodes[k].holes, (nodes[k].nh + 1) * sizeof(CPath));
                nodes[k].holes[nodes[k].nh].n = L.p[i].n; nodes[k].holes[nodes[k].nh].xy = L.p[i].xy; L.p[i].xy = NULL;
                nodes[k].nh++; break;
            }
        }
        for (int i = 0; i < L.n; i++) free(L.p[i].xy);
        free(L.p);
        /* parents: the previous-level polygon containing this one */
        for (int k = first; k < nn; k++)
            for (int q = 0; q < first; q++)
                if (nodes[q].level == lev - 1 && pip(&nodes[q].outer, nodes[k].outer.xy[0], nodes[k].outer.xy[1])) { nodes[k].parent = q; break; }
    }
    if (!nn) { cl_free(&raw); cl_free(&region); cl_free(&allowed); snprintf(op->error, sizeof op->error, "tool does not fit in the pocket"); return 0; }
    int nz = zlevels(p, zl, 200);
    int started = 0;
    double zprev = p->top_z;
    /* corner clean-up: material inside the region not swept by any loop (rough + finish), visited after each level */
    Hole *tgt = NULL; int ntgt = 0;
    if (p->cleanup) {
        int cnt = 0; for (int i = 0; i < nn; i++) cnt += 1 + nodes[i].nh;
        CPaths fin_all = { 0 };
        for (int j = 1; j <= nfin; j++) {
            CPaths fl = cl_offset(&region, -(R + p->stock_xy + (nfin - j) * p->finish_step), p->tol);
            fin_all.p = realloc(fin_all.p, (fin_all.n + fl.n + 1) * sizeof(CPath));
            for (int i = 0; i < fl.n; i++) fin_all.p[fin_all.n++] = fl.p[i];
            free(fl.p);
        }
        CPaths lines; lines.n = 0; lines.p = malloc((cnt + fin_all.n + 1) * sizeof(CPath));
        for (int i = 0; i < nn; i++) { lines.p[lines.n++] = nodes[i].outer; for (int h = 0; h < nodes[i].nh; h++) lines.p[lines.n++] = nodes[i].holes[h]; }
        for (int i = 0; i < fin_all.n; i++) lines.p[lines.n++] = fin_all.p[i];
        CPaths swept = cl_offset_lines(&lines, R, 1, p->tol);
        CPaths shrunk = cl_offset(&region, -(p->stock_xy + 0.002), p->tol);   /* what should be removed */
        CPaths rest = cl_difference(&shrunk, &swept);
        for (int i = 0; i < rest.n; i++) {
            double a = fabs(cl_area(&rest.p[i]));
            if (a < p->tool_dia * p->tool_dia * 1e-3) continue;          /* negligible */
            double cx = 0, cy = 0; for (int k = 0; k < rest.p[i].n; k++) { cx += rest.p[i].xy[2 * k]; cy += rest.p[i].xy[2 * k + 1]; }
            cx /= rest.p[i].n; cy /= rest.p[i].n;
            if (!in_region(&allowed, cx, cy)) {                          /* nearest tool-centre position */
                double bd = 1e300, bx = cx, by = cy;
                for (int q = 0; q < allowed.n; q++) for (int k = 0; k < allowed.p[q].n; k++) {
                    double d = hypot(allowed.p[q].xy[2 * k] - cx, allowed.p[q].xy[2 * k + 1] - cy); if (d < bd) { bd = d; bx = allowed.p[q].xy[2 * k]; by = allowed.p[q].xy[2 * k + 1]; } }
                if (bd > R) continue;                                    /* the tool cannot reach it */
                cx = bx; cy = by;
            }
            tgt = realloc(tgt, (ntgt + 1) * sizeof(Hole)); tgt[ntgt].x = cx; tgt[ntgt].y = cy; ntgt++;
        }
        free(lines.p); cl_free(&fin_all); cl_free(&swept); cl_free(&shrunk); cl_free(&rest);
    }
    for (int k = 0; k < nz; k++) {
        for (int i = 0; i < nn; i++) nodes[i].done = 0;
        int have_pos = 0; double cx = 0, cy = 0;
        int remaining = nn;
        while (remaining > 0) {
            /* next node: a not-done node whose children are all done; prefer the nearest to the current position */
            int best = -1; double bd = 1e300;
            for (int i = 0; i < nn; i++) {
                if (nodes[i].done) continue;
                int ready = 1;
                for (int j = 0; j < nn && ready; j++) if (!nodes[j].done && nodes[j].parent == i) ready = 0;
                if (!ready) continue;
                double d = have_pos ? hypot(nodes[i].outer.xy[0] - cx, nodes[i].outer.xy[1] - cy) : 0;
                for (int v = 0; have_pos && v < nodes[i].outer.n; v++) { double dd = hypot(nodes[i].outer.xy[2 * v] - cx, nodes[i].outer.xy[2 * v + 1] - cy); if (dd < d) d = dd; }
                if (d < bd) { bd = d; best = i; }
            }
            if (best < 0) break;
            PNode *nd = &nodes[best];
            int leaf = 1;
            for (int j = 0; j < nn; j++) if (nodes[j].parent == best) leaf = 0;
            if (have_pos) rotate_to(&nd->outer, cx, cy);
            double sx = nd->outer.xy[0], sy = nd->outer.xy[1];
            if (leaf || !have_pos || !seg_inside(&allowed, cx, cy, sx, sy)) {
                /* retract, reposition, enter by ramping along this loop (or plunge if it is not a leaf) */
                if (started) { V3 e = lastp(b); mv(b, 1, e.x, e.y, p->top_z + p->feed_plane, 0); }
                else { mv(b, 1, sx, sy, p->clearance, 0); started = 1; }
                mv(b, 1, sx, sy, p->top_z + p->feed_plane, 0);
                mv(b, 0, sx, sy, zprev, p->plunge_feed);
                if (leaf) ramp_loop(b, &nd->outer, zprev, zl[k], p->plunge_feed, p->ramp_deg);
                else { mv(b, 0, sx, sy, zl[k], p->plunge_feed); cut_loop(b, &nd->outer, zl[k], p->feed); }
            } else {
                mv(b, 0, sx, sy, zl[k], p->feed);                     /* link at depth inside the cleared area */
                cut_loop(b, &nd->outer, zl[k], p->feed);
            }
            for (int h = 0; h < nd->nh; h++) {                       /* island walls of this level */
                CPath *hc = &nd->holes[h];
                V3 e = lastp(b);
                rotate_to(hc, e.x, e.y);
                if (seg_inside(&allowed, e.x, e.y, hc->xy[0], hc->xy[1])) mv(b, 0, hc->xy[0], hc->xy[1], zl[k], p->feed);
                else { mv(b, 1, e.x, e.y, p->top_z + p->feed_plane, 0); mv(b, 1, hc->xy[0], hc->xy[1], p->top_z + p->feed_plane, 0);
                       mv(b, 0, hc->xy[0], hc->xy[1], zl[k], p->plunge_feed); }
                cut_loop(b, hc, zl[k], p->feed);
            }
            V3 e = lastp(b); cx = e.x; cy = e.y; have_pos = 1;
            nd->done = 1; remaining--;
        }
        for (int t = 0; t < ntgt; t++) {                     /* corner clean-up at this depth */
            V3 e = lastp(b);
            if (seg_inside(&allowed, e.x, e.y, tgt[t].x, tgt[t].y)) { mv(b, 0, tgt[t].x, tgt[t].y, zl[k], p->feed); mv(b, 0, e.x, e.y, zl[k], p->feed); }
            else {
                mv(b, 1, e.x, e.y, p->top_z + p->feed_plane, 0); mv(b, 1, tgt[t].x, tgt[t].y, p->top_z + p->feed_plane, 0);
                mv(b, 0, tgt[t].x, tgt[t].y, zl[k], p->plunge_feed); mv(b, 1, tgt[t].x, tgt[t].y, p->top_z + p->feed_plane, 0);
            }
        }
        zprev = zl[k];
    }
    /* finish passes on the walls (outer CCW, islands CW) at final depth, with a lead arc when it fits */
    for (int j = 1; j <= nfin; j++) {
        CPaths fl = cl_offset(&region, -(R + p->stock_xy + (nfin - j) * p->finish_step), p->tol);
        for (int i = 0; i < fl.n; i++) {
            CPath *c = &fl.p[i];
            if (c->n < 3) continue;
            V3 e = lastp(b); rotate_to(c, e.x, e.y);
            CamParams q = *p;
            double t0x, t0y, t1x, t1y; path_dirs(c, 1, &t0x, &t0y, &t1x, &t1y);
            LeadS qi, qo; lead_sides(&q, &qi, &qo);
            double lx, ly; lead_start(&q, &qi, c->xy[0], c->xy[1], t0x, t0y, &lx, &ly, NULL);
            double cxl = c->xy[0] - t0y * q.lead_r, cyl = c->xy[1] + t0x * q.lead_r;
            if (q.lead_r > 0 && !(in_region(&allowed, lx, ly) && in_region(&allowed, cxl, cyl))) { q.lead_on = 0; q.lead_r = 0; q.lead_len = 0; }
            q.lead_ovl = 0; q.adj_s = q.adj_e = 0;
            (void)pass(b, &q, c, 1, p->final_z, 0, 0);
        }
        cl_free(&fl);
    }
    free(tgt);
    V3 e = lastp(b); mv(b, 1, e.x, e.y, p->top_z + p->feed_plane, 0); mv(b, 1, e.x, e.y, p->clearance, 0);
    for (int i = 0; i < nn; i++) { free(nodes[i].outer.xy); for (int h = 0; h < nodes[i].nh; h++) free(nodes[i].holes[h].xy); free(nodes[i].holes); }
    free(nodes); cl_free(&raw); cl_free(&region); cl_free(&allowed);
    return 1;
}

static int gen_drill(CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (!op->nholes) { snprintf(op->error, sizeof op->error, "no drill points"); return 0; }
    /* the points in the op's order: a stored op keeps Mastercam's point records (TP kinds 1 / 27, cam_fileop.c), which
       are the order its NCI drills; a new op the order the points were picked. The Drill Point Manager sorts (tp_pt_mgr)
       are NOT DECODED, so no sort is applied here */
    for (int i = 0; i < op->nholes; i++) {
        double x = op->holes[i].x, y = op->holes[i].y;
        mv(b, 1, x, y, i == 0 ? p->clearance : p->r_plane, 0);
        mv(b, 1, x, y, p->r_plane, 0);
        mv(b, 0, x, y, cam_drill_z(p), p->plunge_feed);
        mv(b, 1, x, y, p->r_plane, 0);
    }
    V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance, 0);
    return 1;
}

/* mccore nc_tol (config 1008 Toolpath tolerance) and min_arc_length (config 1003 Minimum arc length) of the current
   configuration (McSysTol, cam2d.h; defaults FUN_1803bc7d0: metric 0.002 / 0.02, inch 0.0001 / 0.0002); set by cam_generate */
static _Thread_local double g_min_arc = 0.02;   /* (g_nc_tol = 0.002: defined above, a thread-local has no tentative definition) */
#include "cam2d_ops.c"
#include "cam2d_dyn.c"
#include "cam2d_area.c"
#include "cam3d_rough.c"   /* 3drough: Dynamic OptiRough (Surface High Speed op 132 style 1) */
#include "pgeo.c"
#include "cam2d_pock.c"
#include "cam3d_srough.c"
#include "cam3d_finish.c"   /* HST: Waterline, Area Roughing, Raster (needs cam3d_rough.c, cam3d_srough.c's height field) */   /* srough: classic Surface Rough Pocket / Parallel (needs cam3d_rough.c and the 2D Pocket port) */
#include "cam3d_pencil.c"
#include "cam3d_horiz.c"    /* HST Horizontal Area (formats/horizontal_area.md) */
#include "cam3d_blend.c"    /* Surface Finish Blend (op 109): the engine of formats/blend.md; gen_blend below (needs cam3d_srough.c's sr_record, cam3d_rough.c's mesh) */
/* Surface Finish Blend (op 109): the op record (Mastercam's default op 109 with the dialog's edits, the tool's diameter / corner over its
   0x350 / 0x358), the two chains, the drive mesh -> blend_generate (cam3d_blend.c). FUN_180031990 (decomp/mcmsurf_all.c l.34169-34177): not
   exactly two chains -> mcmsurfres 369 */
static int gen_blend(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (op->nchains != 2) { snprintf(op->error, sizeof op->error, "Finish/Blend needs exactly two chains"); return 0; }
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "Blend here needs the Top tool plane at the origin (the lm frame FUN_1800cc7b0 is NOT DECODED)"); return 0; }
    uint32_t rn = 0; uint8_t *r = sr_record(f, p, &rn);
    if (!r || rn < 0x1448) { free(r); snprintf(op->error, sizeof op->error, "no Surface Finish Blend record (Mastercam's default op 109 is missing)"); return 0; }
    if (p->tool_dia > 0) { memcpy(r + 0x350, &p->tool_dia, 8); memcpy(r + 0x358, &p->corner_r, 8); }
    BlendParams bp; char why[200];
    if (!blend_params_from_record(r, rn, &bp, why, sizeof why)) { free(r); snprintf(op->error, sizeof op->error, "%s", why); return 0; }
    free(r);
    bp.clearance = p->clearance; bp.feed_plane = p->feed_plane > 0 ? p->feed_plane : 0;
    bp.feed = p->feed > 0 ? p->feed : 100; bp.plunge = p->plunge_feed > 0 ? p->plunge_feed : bp.feed;
    double cstep = bp.stepover * 0.02;                     /* as blend_test.c (the chord of the curve pieces; ours) */
    BlendPoly c0 = { 0 }, c1 = { 0 }; char err[200] = "";
    if (!blend_chain_from_ents(f, op->chains[0].ent, op->chains[0].rev, op->chains[0].n, cstep, &c0, err, sizeof err)
        || !blend_chain_from_ents(f, op->chains[1].ent, op->chains[1].rev, op->chains[1].n, cstep, &c1, err, sizeof err)) {
        blend_poly_free(&c0); blend_poly_free(&c1); snprintf(op->error, sizeof op->error, "Blend chains: %s", err); return 0; }
    C3Mesh M;
    if (!c3_mesh_op(f, op, bp.tol * 0.1, &M, err, sizeof err)) { blend_poly_free(&c0); blend_poly_free(&c1); snprintf(op->error, sizeof op->error, "%s", err); return 0; }
    Move *mvv = NULL; int nmv = 0;
    int ok = blend_generate(&bp, &c0, &c1, M.t, M.n, &mvv, &nmv, NULL, err, sizeof err);
    blend_poly_free(&c0); blend_poly_free(&c1); c3_mesh_free(&M);
    if (!ok) { free(mvv); snprintf(op->error, sizeof op->error, "Blend: %s", err); return 0; }
    for (int i = 0; i < nmv; i++) { if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 1024; b->m = realloc(b->m, b->cap * sizeof(Move)); } b->m[b->n++] = mvv[i]; }
    free(mvv);
    return 1;
}   /* HST Pencil (needs cam3d_rough.c's drive mesh and cam3d_finish.c's hst_grid_open; formats/pencil.md 8) */
#include "cam2d_slot_arc.c"    /* decisions batch dec1: the Slot Mill ramp on the exact loop (needs pgeo.c) */
#include "cam2d_face.c"
#include "cam_filter.c"
#include "cam_smooth.c"   /* Smoothing (BNCISection::PostProcess -> bnci_point_distribution_op) */
#include "cam2d_engrave.c"   /* Engrave (opcode 416): engrave.c */

static int cam_generate_op(const McamFile *f, CamOp *op) {
    MB b = { 0 };
    op->error[0] = 0; op->filtered = 0; op->warning[0] = 0;
    g_ps_mb = NULL; g_ps_nmk = g_ps_narc = 0; g_ps_map = g_ps_arc = g_ps_pend = 0;   /* Smoothing: set by gen_contour */
    { const McSysTol *st = mc_systol_units(mcam_is_metric(f)); g_nc_tol = st->nc_tol; g_min_arc = st->min_arc_length; }
    free(op->m); op->m = NULL; op->nm = 0; op->gen_units = mcam_is_metric(f) + 1; op->nci_blocks = 0;
    if (!(op->p.tool_dia > 0)) { snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); return 0; }
    {   /* depths: finite, and within the 200 levels the engines hold (more would silently enlarge the step) */
        const CamParams *q = &op->p;
        if (!isfinite(q->top_z) || !isfinite(q->final_z) || !isfinite(q->stepdown) || !isfinite(q->tool_dia)) {
            snprintf(op->error, sizeof op->error, "a depth / step value is not a number"); return 0; }
        double depth = q->top_z - q->final_z;
        if (depth > 1e-9 && q->stepdown > 1e-6 && ceil(depth / q->stepdown - 1e-9) > 200) {
            snprintf(op->error, sizeof op->error, "%.0f depth passes needed (depth %g / max step %g); at most 200 - use a larger step",
                     ceil(depth / q->stepdown - 1e-9), depth, q->stepdown); return 0; }
    }
    int ok = 0;
    cl_error_take();                                      /* offset.cpp: invalid input / Clipper failure since here */
    switch (op->p.type) {
    case CAM_CONTOUR:
        g_nolook_refused = 0; ok = gen_contour(f, op, &b);
        if (ok && g_nolook_refused) { snprintf(op->error, sizeof op->error, "Infinite look ahead off: only chains the MCCECalcContourOffset port handles are generated; this op has a chain with a refused path (NOT PORTED)"); ok = 0; }
        break;
    case CAM_POCKET: ok = gen_pocket(f, op, &b); break;
    case CAM_DRILL: ok = gen_drill(op, &b); break;
    case CAM_FACE: ok = gen_face(f, op, &b); break;
    case CAM_SLOT: ok = gen_slot(f, op, &b); break;
    case CAM_HELIX: case CAM_CIRCLE: ok = gen_helix(op, &b); break;
    case CAM_THREAD: ok = gen_thread(op, &b); break;
    case CAM_AREA:
        if (hst_has_patches(&op->p)) {   /* batch 9: the Area Cut / Transitions page edits (record patches) re-derive the McHmm macro values
                                            from the record with them (cam2d_dyn.c hst_record; unedited ops keep params_from_record's) */
            uint32_t rn = 0; uint8_t *rec = hst_record(f, &op->p, NULL, &rn);
            cam_area_macro_params(&op->p, rec, rn, mcam_is_metric(f)); free(rec);
        }
        ok = op->p.am_ok && op->p.am_mode == 1 ? gen_area_hmm(f, op, &b) : gen_dynamic(f, op, &b); break;   /* McHmm Area Clearance (cam2d_area.c) */
    case CAM_DYNAMIC: case CAM_DYNCONTOUR: case CAM_PEEL: ok = gen_dynamic(f, op, &b); break;
    case CAM_OPTIROUGH: ok = gen_optirough(f, op, &b); break;
    case CAM_SRPOCKET: case CAM_SRMPOCKET: ok = gen_srough_pocket(f, op, &b); break;   /* op 131 = op 10's engine (surf_classic2.md 1.1) */     /* srough: cam3d_srough.c */
    case CAM_SRPARALLEL: ok = gen_srough_parallel(f, op, &b); break;
    case CAM_WATERLINE: ok = gen_waterline(f, op, &b); break;
    case CAM_SFPARALLEL: ok = gen_sfinish_parallel(f, op, &b); break;
    case CAM_SFCONTOUR: ok = gen_sfinish_contour(f, op, &b); break;
    case CAM_HSTPROJECT: ok = gen_hstproject(f, op, &b); break;
    case CAM_SRPROJECT: ok = gen_srough_project(f, op, &b); break;
    case CAM_SRPLUNGE: ok = gen_srough_plunge(f, op, &b); break;
    case CAM_SFRADIAL: ok = sr_parallel(f, op, &b, 2); break;
    case CAM_SFPROJECT: ok = gen_sfinish_project(f, op, &b); break;   /* op 13: op 7's stages without levels */   /* op 12: op 5 / 11's stage 2 (FUN_180132430) on the rays of FUN_18013ae60 */
    case CAM_RASTER: ok = gen_raster(f, op, &b); break;
    case CAM_AREAROUGH: ok = gen_arearough(f, op, &b); break;
    case CAM_SPIRAL: ok = gen_spiral(f, op, &b); break;
    case CAM_RADIAL: ok = gen_radial(f, op, &b); break;
    case CAM_SCALLOP: ok = gen_scallop(f, op, &b); break;
    case CAM_PENCIL: ok = gen_pencil(f, op, &b); break;
    case CAM_HORIZAREA: ok = gen_horizarea(f, op, &b); break;
    case CAM_SFPENCIL: ok = gen_sfpencil(f, op, &b); break;   /* classic op 39: McHmm's Pencil Pass Macro (surf_classic3.md 10) */
    case CAM_BLEND: ok = gen_blend(f, op, &b); break;
    case CAM_ENGRAVE: ok = gen_engrave(f, op, &b); break;
    default: snprintf(op->error, sizeof op->error, "unknown operation type %d", op->p.type);
    }
    if (ok && cl_error_take()) {                          /* never a toolpath from geometry Clipper could not take */
        snprintf(op->error, sizeof op->error, "the chain geometry has invalid coordinates (not a number / out of range); no toolpath made"); ok = 0; }
    for (int k = 0; ok && k < b.n; k++)
        if (!isfinite(b.m[k].p.x) || !isfinite(b.m[k].p.y) || !isfinite(b.m[k].p.z) || fabs(b.m[k].p.x) > 1e6 || fabs(b.m[k].p.y) > 1e6 || fabs(b.m[k].p.z) > 1e6) {
            snprintf(op->error, sizeof op->error, "the toolpath came out with an invalid point (move %d); no toolpath made", k); ok = 0; }
    g_ps_mb = NULL; g_ps_pend = 0;                        /* Smoothing: the recording ends with the generator */
    if (!ok) { free(b.m); return 0; }
    op->filtered = cam_arc_filter(f, op, &b);   /* batch 10: Mastercam's Line/Arc filter (BNCISection::PostProcess -> bnci_filter_op) */
    {   /* Smoothing: the next step of BNCISection::PostProcess (decomp/mccore_filter.c l.3588-3601, record 0xca8) */
        int sr = cam_smooth(f, op, &b);
        if (sr < 0) { free(b.m); return 0; }
        if (sr > 0) op->filtered = 1;                     /* Mastercam posts the distributed points as lines (FUN_18094ce10 -> ct_lin_mill):
                                                             no arc fitting of ours (CamOp.filtered) */
    }
    op->m = b.m; op->nm = b.n;
    return 1;
}

/* ---- the operation's Tplane (core_ui cui_planes.c, formats/qt_port_plan.md "Stock Setup + planes"): the system planes
   = the reserved views 1 Top .. 7 Isometric (ReservedPlanes::BuildStandardMatrices; the same rows as scene.c STD_VIEW:
   the view's x / y / z axes in world coordinates). wpt_to_op (mccore 0x1800a5950): op = M (w - origin); op_to_wpt
   (0x1800a2230): w = M^T op + origin. The engines run in op coordinates (the NCI's frame); the moves come back in world
   coordinates for the display (the post maps them again). An Ent arc is an XY arc: only Top / Bottom (normal = world Z)
   keep arcs; in another Tplane an arc entity is refused (NOT PORTED). */
static const double CAM_PLANES[8][9] = {
    { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 1, 0, 0, 0, 0, 1, 0, -1, 0 }, { -1, 0, 0, 0, 0, 1, 0, 1, 0 },
    { -1, 0, 0, 0, 1, 0, 0, 0, -1 }, { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { 0, -1, 0, 0, 0, 1, -1, 0, 0 },
    { 0.70710678118655, 0.70710678118655, 0.0, -0.40824829046386, 0.40824829046386, 0.81649658092772, 0.57735026918962, -0.57735026918962, 0.57735026918962 } };
/* planes made here (core_ui cui_planes.c, the Bounding Box panel's Create Tool Plane): their ids (> 7) and matrices (ours
   registry; the part's MastercamPlanes archive is not read) */
static struct { int id; double m[9]; } CAM_UP[256]; static int cam_nup;
int cam_plane_register(int id, const double m[9]) {
    if (id <= 7) return 0;
    for (int i = 0; i < cam_nup; i++) if (CAM_UP[i].id == id) { memcpy(CAM_UP[i].m, m, sizeof CAM_UP[i].m); return 1; }
    if (cam_nup >= 256) return 0;
    CAM_UP[cam_nup].id = id; memcpy(CAM_UP[cam_nup].m, m, sizeof CAM_UP[cam_nup].m); cam_nup++; return 1;
}
void cam_plane_clear_user(void) { cam_nup = 0; }
const double *cam_plane_matrix(int id) {
    if (id > 7) for (int i = 0; i < cam_nup; i++) if (CAM_UP[i].id == id) return CAM_UP[i].m;
    return CAM_PLANES[id >= 1 && id <= 7 ? id : 1];
}
void cam_plane_w2v(int id, const double org[3], const double w[3], double v[3]) {
    const double *m = cam_plane_matrix(id); double d[3] = { w[0] - org[0], w[1] - org[1], w[2] - org[2] };
    for (int r = 0; r < 3; r++) v[r] = m[3 * r] * d[0] + m[3 * r + 1] * d[1] + m[3 * r + 2] * d[2];
}
void cam_plane_v2w(int id, const double org[3], const double v[3], double w[3]) {
    const double *m = cam_plane_matrix(id);
    for (int c = 0; c < 3; c++) w[c] = m[c] * v[0] + m[3 + c] * v[1] + m[6 + c] * v[2] + org[c];
}
/* a stored op_view's matrix -> the system plane (the reader's SearchPlanePtr by matrix, FUN_1802cfb60; only the seven
   system planes exist here) */
int cam_plane_from_matrix(const double m[9]) {
    for (int id = 1; id <= 7; id++) { const double *s = CAM_PLANES[id]; int k = 0; while (k < 9 && fabs(m[k] - s[k]) < 1e-9) k++; if (k == 9) return id; }
    return 0;
}
/* the three op_views of an OP_ID record (ent offsets 0x5d8 Tplane / 0x688 Cplane / 0x738 WCS; formats/mcam.md "Operation
   planes") <-> p. Reading: ProcessAssocOperation (FUN_1802cfb60, decomp/mccore_tooladd.c l.316-345) resolves a view by its
   plane id (+0x68) through the file's plane id map, by matrix + origin only when the id is missing, and overwrites the
   view with the plane's data when they differ (ConvertNoWorkOffset). The part's MastercamPlanes archive is not parsed
   here, so a view is taken only when it is unambiguous: its matrix is a system plane's (the reserved matrices of
   ReservedPlanes::BuildStandardMatrices, the exact doubles of mastercam.dll), its id is that system plane's id (the
   SystemPlanes ids 1..7 of the user parts) and its origin is the world origin; anything else = a user plane or a moved
   system plane, NOT PORTED (the caller leaves the op as stored). Work offsets: +0x92 of each view, < -1 -> -1
   (RefreshData). Writing: Plane::Convert(op_view) = InitOperationView (mastercam.dll 0x1803db1c0): zeroed, +0 = 1, +2
   view number, +8 matrix rows, +0x50 origin, +0x68 plane id, +0x92 the view's work offset. */
static int16_t rec16(const uint8_t *r, uint32_t o) { int16_t v; memcpy(&v, r + o, 2); return v; }
static double recd(const uint8_t *r, uint32_t o) { double v; memcpy(&v, r + o, 8); return v; }
int cam_planes_from_record(const uint8_t *r, uint32_t n, CamParams *p, char *why, size_t whyn) {
    static const uint32_t voff[3] = { 0x5d8, 0x688, 0x738 };
    static const char *nm[3] = { "tool plane", "construction plane", "WCS" };
    int *ids[3] = { &p->tplane, &p->cplane, &p->wcs }, *wo[3] = { &p->wofs, &p->cwofs, &p->wwofs }; double *org[3] = { p->torg, p->corg, p->worg };
    if (n < 0x738 + 0xb0) { if (why) snprintf(why, whyn, "operation record too short for its planes"); return 0; }
    for (int v = 0; v < 3; v++) {
        double m[9], o[3]; for (int k = 0; k < 9; k++) m[k] = recd(r, voff[v] + 8 + 8 * k); for (int k = 0; k < 3; k++) o[k] = recd(r, voff[v] + 0x50 + 8 * k);
        int id = cam_plane_from_matrix(m), sid = rec16(r, voff[v] + 0x68);
        if (!id || sid != id || o[0] != 0 || o[1] != 0 || o[2] != 0) {
            if (why) snprintf(why, whyn, "its %s (plane id %d) is not one of the system planes at the world origin (other planes are NOT PORTED)", nm[v], sid);
            return 0;
        }
        *ids[v] = id == 1 ? 0 : id; memcpy(org[v], o, sizeof o);
        int w = rec16(r, voff[v] + 0x92); *wo[v] = w < -1 ? -1 : w;
    }
    p->wofs_manual = p->wofs >= 0;                       /* RefreshData: Manual = the Tplane's +0x92 >= 0 */
    return 1;
}
void cam_planes_to_record(uint8_t *r, const CamParams *p) {
    static const uint32_t voff[3] = { 0x5d8, 0x688, 0x738 };
    const int ids[3] = { p->tplane, p->cplane, p->wcs }, wo[3] = { p->wofs_manual && p->wofs >= 0 ? p->wofs : -1, p->cwofs, p->wwofs };
    const double *org[3] = { p->torg, p->corg, p->worg };
    for (int v = 0; v < 3; v++) {
        uint8_t *ov = r + voff[v]; int id = ids[v] <= 0 ? 1 : ids[v]; const double *m = cam_plane_matrix(id); int16_t s;
        memset(ov, 0, 0xb0); ov[0] = 1;
        /* +2 view number = PlaneBase::GetShortViewNo: the reserved view whose matrix is the plane's (system planes: the id;
           a plane made here: its matrix's reserved view, 0 when none - FindViewNo's new views are NOT PORTED) */
        s = (int16_t)(id <= 7 ? id : cam_plane_from_matrix(m)); memcpy(ov + 2, &s, 2);
        s = (int16_t)id; memcpy(ov + 0x68, &s, 2);
        memcpy(ov + 8, m, 9 * sizeof(double)); memcpy(ov + 0x50, org[v], 3 * sizeof(double));
        s = (int16_t)(wo[v] < -1 ? -1 : wo[v]); memcpy(ov + 0x92, &s, 2);
    }
}
int cam_op_top(const CamParams *p) { return p->tplane <= 1 && p->torg[0] == 0 && p->torg[1] == 0 && p->torg[2] == 0; }
/* an arc lies in a plane with rows m when its normal is the plane's Z (either sense): an XY arc (Ent.vw 0) for Top / Bottom,
   a view arc when its view's z row is +-m's (ours: the engines take arcs in the plane only) */
int cam_arc_in_plane(const Ent *e, const double m[9]) {
    if (!e->vw) return fabs(fabs(m[8]) - 1) < 1e-9;
    return fabs(fabs(m[6] * e->vm[6] + m[7] * e->vm[7] + m[8] * e->vm[8]) - 1) < 1e-9;
}
static void cam_v3(int id, const double org[3], V3 *q) { double w[3] = { q->x, q->y, q->z }, v[3]; cam_plane_w2v(id, org, w, v); q->x = v[0]; q->y = v[1]; q->z = v[2]; }
/* Lathe Face (opcode 66, CAM_LFACE): the stored op's record with the op's record patches (as Engrave) and its two face points
   (TP kind 1) -> lathe_face.c. Moves in world coordinates: the contour frame's view is Top (lathe_face.c checks it), so world =
   (Z, X, 0); p->tplane = 5 (the tool plane's rows, cam_op_from_file) maps them to the NCI's (X, 0, Z). No arc filter / smoothing:
   BNCISection::PostProcess for lathe ops is NOT DECODED (the stored op's filter settings are not read). */
static double (*lf_made_out)[3]; static int *lf_nmade_out;   /* cam_lface_points: where the face points GetFaceBoundaryFromStock made go */
static int gen_lathe_face(const McamFile *f, CamOp *op) {
    const CamParams *p = &op->p; const Op *fo = NULL;
    free(op->m); op->m = NULL; op->nm = 0; op->error[0] = 0; op->warning[0] = 0; op->filtered = 0; op->gen_units = mcam_is_metric(f) + 1; op->nci_blocks = 0;
    if (p->file_op_id > 0) for (int i = 0; i < f->nops; i++) if (f->ops[i].id == p->file_op_id && f->ops[i].raw) { fo = &f->ops[i]; break; }
    /* a new op (formats/lathe.md 7.1): lathe_face (mclathe 0x1801a3290) -> init_operation(op, 0x42) = InitOperation's default_ops_get:
       the lathe defaults file's Face record (cam_lathe_default_record); with an earlier Face in the group and config 17028 "Get defaults
       from previous operation?" InitOperation copies that op instead (list_op_get_prev; its lathe branch NOT DECODED): refused.
       NOT applied here (lathe.md 7.1): reset_lathe_op_prm_defaults' op +0x1218..+0x1357 entry / exit vectors (read only with op +0x888,
       refused) and op +0xb3a / +0xb3c / +0xb70 / +0xb78; the tool (the dialog's Make_op_tool from the part's lathe tool: NOT DECODED) -
       the defaults record's own ltool_rec is kept; the active planes (InitOperationViewsToActivePlanes) - the record's are kept */
    const uint8_t *src = NULL; uint32_t srcn = 0;
    uint8_t *lfrec = NULL;
    if (fo) { src = fo->raw; srcn = fo->raw_n; }
    else if (p->file_op_id <= 0 && p->lf_new) {          /* a new Face of the menu / ribbon command (lathe_newop.c, formats/lathe.md 8): its
                                                            record = InitOperation + the tool pick + its planes / dialog values */
        char e2[200];
        if (!lfn_record(f, p, &lfrec, e2, sizeof e2)) { snprintf(op->error, sizeof op->error, "%s", e2); return 0; }
        src = lfrec; srcn = LFN_REC;
        /* GetOpStockBoundary 0x1801e9d60: OpActiveSpindle(op) = the op's axis combination's spindle (lathe_axis.c; for a new op the
           tool pick wrote it to op +0x541, SetOpActiveSpindleFromAxisCombo): 1 = the right-spindle stock, polygon 0x1d - NOT PORTED */
        if (lfrec[0x68 + 0x541] == 1) { free(lfrec); snprintf(op->error, sizeof op->error, "Lathe Face: the tool's axis combination works on the right spindle (op +0x541 = 1): the right-spindle stock (polygon 0x1d) is NOT PORTED"); return 0; }
    }
    else if (p->file_op_id <= 0) {
        for (int i = 0; i < f->nops; i++) if (f->ops[i].opcode == 0x42) {
            snprintf(op->error, sizeof op->error, "Lathe Face: a new Face after an earlier Face (op %d) takes that op's parameters (InitOperation list_op_get_prev, config 17028; lathe branch NOT DECODED)", f->ops[i].id); return 0; }
        src = cam_lathe_default_record(0x42, mcam_is_metric(f), &srcn);
        if (!src) { snprintf(op->error, sizeof op->error, "Lathe Face: no lathe operation defaults file with a Face op (lathe/Ops/LATHE_%s.mcam-defaults)", mcam_is_metric(f) ? "MM" : "INCH"); return 0; }
    }
    if (!src) { snprintf(op->error, sizeof op->error, "Lathe Face: operation %d is not in the part", p->file_op_id); return 0; }
    uint8_t *rec = malloc(srcn); memcpy(rec, src, srcn); free(lfrec); lfrec = NULL;
    for (int k = 0; k < p->nrpatch && !p->lf_new; k++) {     /* the op's record patches (cam_cli --rp / --rec; lfn_record applied a new Face's) */
        int o = p->rpatch[k].off, m = p->rpatch[k].n;
        if (!p->rpatch[k].height && (uint32_t)(o + m) <= srcn) memcpy(rec + o, p->rpatch[k].v, m);
    }
    double (*pts)[3] = malloc(sizeof *pts * (size_t)(f->ntp + 1)); int np = 0;
    for (int t = 0; fo && t < f->ntp; t++) if (f->tp[t].op_id == fo->id && f->tp[t].kind == 1) { memcpy(pts[np], f->tp[t].xyz, sizeof pts[np]); np++; }
    double tol = mc_systol_units(mcam_is_metric(f))->tol;
    /* the stock the op sees (lathe_stock.c: GetPrvOpBdrys) when it faces from the stock (op +0xb68 = record 0xbd0) */
    LsBdry stock; memset(&stock, 0, sizeof stock); int have_stock = 0, ssrc = 0; char serr[200] = "";
    if (srcn > 0xbd0 && rec[0xbd0]) have_stock = lathe_op_stock(f, fo ? fo->id : 0, tol, &stock, &ssrc, serr, sizeof serr);
    if (srcn > 0xbd0 && rec[0xbd0] && !have_stock && np < 2) { snprintf(op->error, sizeof op->error, "Lathe Face: %s", serr); free(rec); free(pts); return 0; }
    LfIn in = { rec, srcn, (const double (*)[3])pts, np, tol, have_stock ? &stock : NULL, 0, 0, lf_made_out, lf_nmade_out };
    if (!fo && p->lf_new != 1) {                               /* (lf_new 1: a menu-made op whose Face page was never opened - no
                                                                 CLFaceProp::OnOK, the bnci path GetAllGeometry makes the points)
                                                                 a new op: the Face page's OK made the face points (CLFaceProp::OnOK):
                                                                 geom +0x88 = latheZ_to_wcsX(Finish Z op +0x13b8 = record 0x1420); with the
                                                                 measured lathe plane (lathe.md 6.5: NCI Z = world x) wcsX = Z */
        double fz; memcpy(&fz, rec + 0x1420, 8);
        in.dialog_ok = 1; in.finish_x = fz;
    }
    LfMove *lm = NULL; int nl = 0; char err[sizeof op->error];
    int ok = lathe_face_gen(&in, &lm, &nl, err, sizeof err);
    free(rec); free(pts); ls_free(&stock);
    if (!ok) { snprintf(op->error, sizeof op->error, "%s", err); return 0; }
    MB b = { 0 };
    for (int i = 0; i < nl; i++) { mv(&b, lm[i].rapid, lm[i].z, lm[i].x, 0, lm[i].feed); b.m[b.n - 1].ctf = lm[i].ctf; }
    free(lm);
    op->m = b.m; op->nm = b.n;
    return 1;
}
/* the two face point entities a new Lathe Face makes (GetFaceBoundaryFromStock 0x1801e9410: create_point_ent + init_tp_ent kind 1 'P'
   + store_ent_assoc), world xyz: the op generated again with lathe_face.c's LfIn.made (the save writes them, mcam_saveops.c). 1 = two */
int cam_lface_points(const McamFile *f, const CamOp *op, double pts[2][3], char *err, size_t errn) {
    CamOp *c = calloc(1, sizeof *c); c->p = op->p;
    double made[2][3]; int nm = 0;
    lf_made_out = made; lf_nmade_out = &nm;
    int ok = gen_lathe_face(f, c);
    lf_made_out = NULL; lf_nmade_out = NULL;
    if (!ok) snprintf(err, errn, "%s", c->error);
    else if (nm != 2) { ok = 0; snprintf(err, errn, "Lathe Face: the face points come from the op itself (not from the stock)"); }
    else memcpy(pts, made, sizeof made);
    free(c->m); free(c);
    return ok;
}
static int chain_has_tilted_line(const McamFile *f, const Chain *c) {
    for (int k = 0; k < c->n; k++) if (c->ent[k] >= 0 && c->ent[k] < f->nents && f->ents[c->ent[k]].type == E_LINE && cam_ent_tilted(&f->ents[c->ent[k]])) return 1;
    return 0;
}
int cam_generate(const McamFile *f, CamOp *op) {
    CamParams *p = &op->p;
    if (p->type == CAM_LFACE) return gen_lathe_face(f, op);
    /* S4 (formats/surface_selection.md 6): solid faces picked one by one (CamOp.rface) - the 3D engines here take whole bodies only
       (cam3d_rough.c c3_mesh_op: a solid picked here = its whole body); single faces wait for stage S7: refused, never machined as
       the whole body */
    if (cam_type_is_3d(p->type) && op->nrface > 0) {
        snprintf(op->error, sizeof op->error, "solid faces picked one by one (%d): machining single solid faces is NOT PORTED yet (only whole bodies)", op->nrface);
        free(op->m); op->m = NULL; op->nm = 0; return 0;
    }
    /* spline curves in a chain (formats/spline_chain.md): Engrave only (its engine takes the lines chain_to_ctour_array breaks them
       into, cam_chain_polyline), in the Top plane (the breaking works on the curve as stored, in world XY); the other engines here
       take lines / arcs (their own spline handling - chains_to_pock_bounds, ctour arcs, the 3D boundaries - is NOT PORTED) */
    for (int c = 0; c < op->nchains; c++) for (int k = 0; k < op->chains[c].n; k++) {
        int ei = op->chains[c].ent[k];
        if (ei < 0 || ei >= f->nents || f->ents[ei].type != E_SPLINE) continue;
        if (p->type == CAM_CONTOUR && cam_op_top(p)) continue;   /* gen_contour: the 3D contour takes them (FUN_1800601f0), else refused there */
        const char *why = p->type != CAM_ENGRAVE ? "this operation type does not take spline curves here (NOT PORTED; Engrave does)"
                        : !cam_op_top(p) ? "a spline curve with a tool plane other than Top - not supported here (NOT PORTED)"
                        : !f->ents[ei].planar ? "a spline curve that is not parallel to XY - not supported here"
                        : (k == 0 && op->chains[c].cut0 > 1e-12) || (k == op->chains[c].n - 1 && op->chains[c].cut1 > 1e-12) ? "a Dynamic chain start / end inside a spline curve - not supported here (NOT PORTED)"
                        : NULL;
        if (why) { snprintf(op->error, sizeof op->error, "%s", why); free(op->m); op->m = NULL; op->nm = 0; return 0; }
    }
    /* CS-7 (formats/chaining.md "Curves outside the C-plane, splines and NURBS"): a chain that is not flat in world Z (a line not parallel to
       XY, an arc of a tilted view, a spline off XY - Mastercam's chaining takes them, cam_chain_all) goes only to Contour here
       (check_chains_3d -> the 3D contour: gen_contour, its own refusals); what the other operations do with one (Pocket / Facing / Slot /
       ...: chains_to_pock_bounds / chain_to_pock_bound's projection, the 3D engines' boundaries, Engrave's chain_to_ctour_array depth) is
       NOT DECODED: refused */
    if (p->type != CAM_CONTOUR && cam_op_top(p))   /* (splines / tilted view arcs: refused above / below with their own messages) */
        for (int c = 0; c < op->nchains; c++) if (!op->chains[c].solid_idn && op->chains[c].n && cam_chain_3d(f, &op->chains[c]) && chain_has_tilted_line(f, &op->chains[c])) {
            snprintf(op->error, sizeof op->error, "a chain that is not flat in the tool plane (chain %d): this operation's handling of 3D chains is NOT DECODED here (Contour takes them)", c + 1);
            free(op->m); op->m = NULL; op->nm = 0; return 0; }
    if (cam_op_top(p)) {                                  /* Top: the XY engines; a view arc (another plane) is refused */
        for (int c = 0; c < op->nchains; c++) for (int k = 0; k < op->chains[c].n; k++) {
            int ei = op->chains[c].ent[k];
            if (p->type == CAM_CONTOUR && ei >= 0 && ei < f->nents && f->ents[ei].type == E_ARC && f->ents[ei].vw && fabs(fabs(f->ents[ei].vm[8]) - 1) > 1e-9) continue;   /* a tilted arc: a 3D chain (gen_contour3d, else refused there) */
            if (ei >= 0 && ei < f->nents && f->ents[ei].type == E_ARC && f->ents[ei].vw) { snprintf(op->error, sizeof op->error, "an arc that is not in the tool plane - not supported here"); free(op->m); op->m = NULL; op->nm = 0; return 0; }
        }
        return cam_generate_op(f, op);
    }
    const double *m = cam_plane_matrix(p->tplane);
    int xy = fabs(fabs(m[8]) - 1) < 1e-9;
    for (int c = 0; c < op->nchains; c++) for (int k = 0; k < op->chains[c].n; k++) {
        int ei = op->chains[c].ent[k];
        if (ei >= 0 && ei < f->nents && f->ents[ei].type == E_ARC && !cam_arc_in_plane(&f->ents[ei], m)) { snprintf(op->error, sizeof op->error, "an arc that is not in the tool plane - not supported here"); free(op->m); op->m = NULL; op->nm = 0; return 0; }
    }
    if (op->nholes && !xy) {                              /* a Hole keeps x / y only: its world Z is not known here */
        snprintf(op->error, sizeof op->error, "points / circles in a tool plane whose Z is not the world Z - not supported here (NOT PORTED)");
        free(op->m); op->m = NULL; op->nm = 0; return 0; }
    McamFile T = *f;
    T.ents = malloc((f->nents ? f->nents : 1) * sizeof(Ent)); memcpy(T.ents, f->ents, f->nents * sizeof(Ent));
    for (int i = 0; i < f->nents; i++) { Ent *e = &T.ents[i]; if (!e->type) continue;
        cam_v3(p->tplane, p->torg, &e->a); cam_v3(p->tplane, p->torg, &e->b);
        if (e->type == E_ARC && e->vw) {                /* a view arc: its own x axis in op coordinates, the sense by the normals */
            const double *v = e->vm; double X[3], nz = m[6] * v[6] + m[7] * v[7] + m[8] * v[8];
            for (int r = 0; r < 3; r++) X[r] = m[3 * r] * v[0] + m[3 * r + 1] * v[1] + m[3 * r + 2] * v[2];
            double b = atan2(X[1], X[0]);
            cam_v3(p->tplane, p->torg, &e->c);
            if (nz > 0) e->a0 = b + e->a0; else { e->a0 = b - e->a0; e->sw = -e->sw; }
            e->vw = 0;
        } else if (e->type == E_ARC) { cam_v3(p->tplane, p->torg, &e->c); if (m[0] < 0) { e->a0 = M_PI - e->a0; e->sw = -e->sw; } }   /* Bottom: x mirrored */
        e->planar = 1; }
    Hole *hs = op->holes, *hv = NULL;
    if (op->nholes) { hv = malloc(op->nholes * sizeof(Hole)); memcpy(hv, hs, op->nholes * sizeof(Hole));
        for (int i = 0; i < op->nholes; i++) { V3 q = { hs[i].x, hs[i].y, 0 }; cam_v3(p->tplane, p->torg, &q); hv[i].x = q.x; hv[i].y = q.y; }
        op->holes = hv; }
    int ok = cam_generate_op(&T, op);
    op->holes = hs; free(hv); free(T.ents);
    if (!ok) return 0;
    for (int i = 0; i < op->nm; i++) { Move *mv = &op->m[i]; double v[3] = { mv->p.x, mv->p.y, mv->p.z }, w[3];
        cam_plane_v2w(p->tplane, p->torg, v, w); mv->p.x = w[0]; mv->p.y = w[1]; mv->p.z = w[2]; mv->arc = 0; mv->apl = 0; }
    op->filtered = 0;                                     /* the arc tags are gone in another plane: the post refits (as before) */
    return 1;
}

/* ------------------------------------------------------------------------------------------------ G-code */
static void fmtn(char *o, double v, int dec) {          /* 3 decimals half-up, no trailing zeros: "15.", ".299", "-1.2" */
    double s = pow(10, dec), r = floor(fabs(v) * s + 0.5 + 1e-9) / s;
    if (v < 0 && r != 0) r = -r;
    char t[48]; snprintf(t, sizeof t, "%.*f", dec, r);
    if (dec > 0) { char *e = t + strlen(t) - 1; while (*e == '0') *e-- = 0; }
    else strcat(t, ".");
    char *q = t; if (!strncmp(q, "0.", 2)) q++; else if (!strncmp(q, "-0.", 3)) { q[1] = '-'; q++; }
    if (!strcmp(q, "-.") || !strcmp(q, ".")) q = "0.";
    strcpy(o, q);
}

typedef struct { FILE *f; int n; int g; char x[24], y[24], z[24], fe[24]; } GW;
static int gdec = 3, fdec = 0;                         /* coordinate / feed decimals: mm 3 / 0, inch 4 / 2 */
static void blk(GW *w, const char *s) { fprintf(w->f, "N%d %s\n", w->n, s); w->n = w->n < 9998 ? w->n + 2 : 100; }
static void words(GW *w, int g, V3 p, double feed) {
    char line[160] = "", t[24];
    if (g != w->g) { sprintf(line + strlen(line), "G%d ", g); w->g = g; }
    fmtn(t, p.x, gdec); if (strcmp(t, w->x)) { sprintf(line + strlen(line), "X%s ", t); strcpy(w->x, t); }
    fmtn(t, p.y, gdec); if (strcmp(t, w->y)) { sprintf(line + strlen(line), "Y%s ", t); strcpy(w->y, t); }
    fmtn(t, p.z, gdec); if (strcmp(t, w->z)) { sprintf(line + strlen(line), "Z%s ", t); strcpy(w->z, t); }
    if (g == 1) { fmtn(t, feed, fdec); if (strcmp(t, w->fe)) { sprintf(line + strlen(line), "F%s ", t); strcpy(w->fe, t); } }
    size_t L = strlen(line); if (L && line[L - 1] == ' ') line[L - 1] = 0;
    if (line[0] && !(line[0] == 'G' && strlen(line) <= 3)) blk(w, line);
}

/* circle through three points (XY); 0 if collinear */
static int circ3(V3 a, V3 b, V3 c, double *cx, double *cy, double *r) {
    double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (fabs(d) < 1e-12) return 0;
    double a2 = a.x * a.x + a.y * a.y, b2 = b.x * b.x + b.y * b.y, c2 = c.x * c.x + c.y * c.y;
    *cx = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
    *cy = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
    *r = hypot(a.x - *cx, a.y - *cy);
    return 1;
}

/* longest arc starting at m[i0-1] through m[i0..k]: returns k (>= i0 + 2) or -1; *ccw, *cx, *cy, *sweep out */
static int fit_arc(const Move *m, int i0, int n, double tol, double *cx, double *cy, int *ccw, double *sweep) {
    /* XY arc; Z may change linearly with the swept angle (helix) */
    int best = -1;
    static double acc[1024];
    if (i0 + 1 < n && (m[i0 + 1].rapid || fabs(m[i0 + 1].feed - m[i0].feed) > 1e-9)) return -1;   /* the 2nd point too */
    for (int k = i0 + 2; k < n && k < i0 + 720; k++) {
        if (m[k].rapid || fabs(m[k].feed - m[i0].feed) > 1e-9) break;
        double x, y, r;
        if (!circ3(m[i0 - 1].p, m[(i0 - 1 + k) / 2].p, m[k].p, &x, &y, &r) || r > 5000 || r < tol * 4) break;
        int ok = 1, dir = 0; double sw = 0;
        for (int j = i0; j <= k && ok; j++) {
            if (fabs(hypot(m[j].p.x - x, m[j].p.y - y) - r) > tol) ok = 0;
            double ax = m[j - 1].p.x - x, ay = m[j - 1].p.y - y, bx = m[j].p.x - x, by = m[j].p.y - y;
            double cr = ax * by - ay * bx, dt = ax * bx + ay * by, ang = atan2(cr, dt);
            int s2 = cr > 0 ? 1 : -1;
            if (fabs(ang) > M_PI / 4) ok = 0;                 /* segments must be short relative to the radius */
            if (!dir) dir = s2; else if (s2 != dir) ok = 0;
            sw += fabs(ang); acc[j - i0] = sw;
            /* the chord must stay within tol of the arc as well */
            if (r - sqrt(fmax(0, r * r - pow(hypot(bx - ax, by - ay) / 2, 2))) > tol) ok = 0;   /* chord sagitta */
        }
        if (ok) {                                             /* Z linear in the angle (constant for plain arcs) */
            double z0 = m[i0 - 1].p.z, dz = m[k].p.z - z0;
            for (int j = i0; j <= k && ok; j++) if (fabs(z0 + dz * acc[j - i0] / sw - m[j].p.z) > tol) ok = 0;
        }
        if (!ok || sw > 2 * M_PI - 1e-6) break;
        best = k; *cx = x; *cy = y; *ccw = dir > 0; *sweep = sw;
    }
    return best;
}

static void arc_words(GW *w, int ccw, V3 s, V3 e, double cx, double cy, double feed) {
    char line[160] = "", t[24];
    int g = ccw ? 3 : 2;
    if (g != w->g) { sprintf(line + strlen(line), "G%d ", g); w->g = g; }
    fmtn(t, e.x, gdec); if (strcmp(t, w->x)) { sprintf(line + strlen(line), "X%s ", t); strcpy(w->x, t); }
    fmtn(t, e.y, gdec); if (strcmp(t, w->y)) { sprintf(line + strlen(line), "Y%s ", t); strcpy(w->y, t); }
    fmtn(t, e.z, gdec); if (strcmp(t, w->z)) { sprintf(line + strlen(line), "Z%s ", t); strcpy(w->z, t); }   /* helix */
    double r = hypot(s.x - cx, s.y - cy);
    fmtn(t, r, gdec); sprintf(line + strlen(line), "R%s ", t);
    fmtn(t, feed, fdec); if (strcmp(t, w->fe)) { sprintf(line + strlen(line), "F%s ", t); strcpy(w->fe, t); }
    size_t L = strlen(line); if (L && line[L - 1] == ' ') line[L - 1] = 0;
    blk(w, line);
}

static void emit_moves(GW *w, const CamOp *op) {
    const Move *m = op->m; int n = op->nm;
    for (int k = 1; k < n; k++) {
        if (op->p.arcs && !m[k].rapid && m[k].arc > 0 && !m[k].apl) {     /* an engine arc record (mb_tag_arc): its exact centre
                                                          (XZ / YZ arcs of the filter: chords here, nc_post.c writes them) */
            int e = k; while (e + 1 < n && m[e + 1].arc == m[k].arc && !m[e + 1].rapid) e++;
            V3 s0 = m[k - 1].p, e0 = m[e].p; double cx = m[k].cx, cy = m[k].cy; int ccw = !m[k].cw;
            double r = hypot(s0.x - cx, s0.y - cy), a0 = atan2(s0.y - cy, s0.x - cx), a1 = atan2(e0.y - cy, e0.x - cx);
            double sw = ccw ? a1 - a0 : a0 - a1;
            while (sw < 0) sw += 2 * M_PI; while (sw > 2 * M_PI) sw -= 2 * M_PI;
            if (sw < 1e-9 || hypot(e0.x - s0.x, e0.y - s0.y) < 1e-9) sw = 2 * M_PI;
            int pieces = (int)ceil(sw / (2 * M_PI / 3) - 1e-9); if (pieces < 1) pieces = 1;   /* R words: <= 120 deg (below) */
            V3 from = s0;
            for (int i = 1; i <= pieces; i++) {
                double a = a0 + (ccw ? 1 : -1) * sw * i / pieces;
                V3 to = i == pieces ? e0 : (V3){ cx + r * cos(a), cy + r * sin(a), s0.z + (e0.z - s0.z) * i / pieces };
                arc_words(w, ccw, from, to, cx, cy, m[k].feed); from = to;
            }
            k = e;
            continue;
        }
        if (op->p.arcs && !m[k].rapid && !op->filtered) {   /* batch 10: not after the Line/Arc filter */
            double cx = 0, cy = 0, sw = 0; int ccw = 0;
            int e = fit_arc(m, k, n, fmax(0.003, op->p.tol * 1.2), &cx, &cy, &ccw, &sw);   /* the paths are within tol of their arcs */
            if (e >= k + 2) {
                V3 s0 = m[k - 1].p;
                /* split into pieces of at most 120 degrees: R-format arcs near 180 degrees are ill-conditioned
                   (a 0.001 rounding of R moves the centre by ~sqrt(2 R 0.001)) */
                int pieces = (int)ceil(sw / (2 * M_PI / 3) - 1e-9); if (pieces < 1) pieces = 1;
                V3 from = s0; double acc = 0; int piece = 1;
                for (int j = k; j <= e; j++) {
                    double ax = m[j - 1].p.x - cx, ay = m[j - 1].p.y - cy, bx = m[j].p.x - cx, by = m[j].p.y - cy;
                    acc += fabs(atan2(ax * by - ay * bx, ax * bx + ay * by));
                    if ((acc >= sw * piece / pieces - 1e-9 && piece < pieces) || j == e) {
                        arc_words(w, ccw, from, m[j].p, cx, cy, m[k].feed); from = m[j].p; piece++;
                    }
                }
                k = e;
                continue;
            }
        }
        words(w, m[k].rapid ? 0 : 1, m[k].p, m[k].feed);
    }
}

int cam_write_gcode(FILE *out, const McamFile *f, CamOp **ops, int nops, const char *progname) {
    GW w = { out, 100, -1, "", "", "", "" };
    /* units: the part's (NCI 1016 met_tool); a part whose ops disagree is refused rather than posted at the wrong scale */
    if (f->metric == 2) { fprintf(out, "(ERROR: the part mixes inch and metric operations - not posted)\n"); return 0; }
    int inch = !mcam_is_metric(f);
    gdec = inch ? 4 : 3; fdec = inch ? 2 : 0;
    fprintf(out, "(%s)\n(generated by mcamui - 2D toolpaths, %s)\n", progname, inch ? "inch" : f->metric == 1 ? "mm" : "mm - part units unknown, assumed");
    for (int i = 0; i < nops; i++) {
        const Tool *tl = NULL;
        for (int k = 0; k < f->ntools; k++) if (f->tools[k].num == ops[i]->p.tool_num) tl = &f->tools[k];
        fprintf(out, "( T%d | %s | D%.3f", ops[i]->p.tool_num, tl ? tl->name : "tool", ops[i]->p.tool_dia);
        if (ops[i]->p.corner_r > 0) fprintf(out, " R%.3f", ops[i]->p.corner_r);
        fprintf(out, " )\n");
    }
    blk(&w, inch ? "G20" : "G21"); blk(&w, "G0 G17 G40 G49 G80 G90");
    int tool = -1, cool = 0;
    for (int i = 0; i < nops; i++) {
        CamOp *op = ops[i]; CamParams *p = &op->p;
        if (!op->nm) continue;
        const Tool *tl = NULL;
        for (int k = 0; k < f->ntools; k++) if (f->tools[k].num == p->tool_num) tl = &f->tools[k];
        char t1[24], t2[24], t3[24], line[160];
        if (op->name[0]) fprintf(out, "( %s )\n", op->name);   /* the op comment (op +6) only when it has one (L-5) */
        if (p->tool_num != tool) {
            if (tool >= 0) { if (cool) { blk(&w, "M9"); cool = 0; } blk(&w, "M5"); blk(&w, "G91 G28 Z0."); blk(&w, "M01"); }
            snprintf(line, sizeof line, "T%d M6", p->tool_num); blk(&w, line);
            fmtn(t1, op->m[0].p.x, gdec); fmtn(t2, op->m[0].p.y, gdec);
            snprintf(line, sizeof line, "G0 G90 G54 X%s Y%s", t1, t2); blk(&w, line);
            fprintf(out, "M3 S%d\n", p->rpm);                       /* (unnumbered, as the user's post writes it) */
            if (p->coolant) { blk(&w, "M8"); cool = 1; }
            /* tool length offset: H0 like the user's own post (LinuxCNC 4020: lengths come from touch-off, the tool
               table's Z values must not be applied); MCAM_H_OFFSET=tool writes the tool's H number instead */
            const char *hm = getenv("MCAM_H_OFFSET");
            int h = hm && !strcmp(hm, "tool") ? (tl && tl->len_off > 0 ? tl->len_off : p->tool_num) : 0;
            fmtn(t3, op->m[0].p.z, gdec); snprintf(line, sizeof line, "G43 H%d Z%s", h, t3); blk(&w, line);
            w.g = 0; strcpy(w.x, t1); strcpy(w.y, t2); strcpy(w.z, t3); w.fe[0] = 0;
            tool = p->tool_num;
        } else {
            snprintf(line, sizeof line, "S%d M3", p->rpm); blk(&w, line);
            if (p->coolant != cool) { blk(&w, p->coolant ? "M8" : "M9"); cool = p->coolant; }
        }
        if (p->type == CAM_DRILL) {
            char rz[24], fz[24], ff[24], q[24];
            fmtn(rz, p->r_plane, gdec); fmtn(fz, cam_drill_z(p), gdec); fmtn(ff, p->plunge_feed, fdec); fmtn(q, p->peck, gdec); int peck_on = (p->drill_cycle == 1 || p->drill_cycle == 2) && p->peck > 0;
            for (int h = 0; h < op->nholes; h++) {
                fmtn(t1, op->holes[h].x, gdec); fmtn(t2, op->holes[h].y, gdec);
                if (h == 0) {
                    V3 a = { op->holes[0].x, op->holes[0].y, p->clearance }; words(&w, 0, a, 0);
                    if (peck_on) snprintf(line, sizeof line, "G99 %s X%s Y%s Z%s R%s Q%s F%s", p->drill_cycle == 2 ? "G73" : "G83", t1, t2, fz, rz, q, ff);
                    else snprintf(line, sizeof line, "G99 G81 X%s Y%s Z%s R%s F%s", t1, t2, fz, rz, ff);
                } else snprintf(line, sizeof line, "X%s Y%s", t1, t2);
                blk(&w, line);
            }
            blk(&w, "G80");
            V3 e = { op->holes[op->nholes - 1].x, op->holes[op->nholes - 1].y, p->clearance };
            w.g = -1; w.x[0] = w.y[0] = w.z[0] = 0;          /* after the cycle the tool is at the R plane */
            { char tz[24], l2[64]; fmtn(tz, e.z, gdec); snprintf(l2, sizeof l2, "G0 Z%s", tz); blk(&w, l2); w.g = 0; strcpy(w.z, tz);
              fmtn(w.x, e.x, gdec); fmtn(w.y, e.y, gdec); }
            continue;
        }
        emit_moves(&w, op);
    }
    if (cool) blk(&w, "M9");
    blk(&w, "M5"); blk(&w, "G91 G28 Z0."); blk(&w, "G28 X0. Y0."); blk(&w, "M30");
    return 1;
}

/* ------------------------------------------------------------------------------------------------ sidecar */
/* text format:  OP <type> <name...>
 *               P <key> <value>          (one line per parameter)
 *               CHAIN <closed> [A] [S<solid idn>] <ent>:<rev> ...   (A avoidance region; S a solid chain, no ents)
 *               CHAIN <closed> L G<type>,<a xyz>,<b xyz>,<c xyz>,<r>,<a0>,<sw>:<rev> ...   (L a chain picked on a solid
 *                                                                  in the Chaining dialog's Solids mode: its curves, ours)
 *               HOLE <x> <y> [<d>]
 *               TAB <x> <y>
 *               END                                                               */
#define PD(k) { #k, offsetof(CamParams, k), 'd' }
#define PI_(k) { #k, offsetof(CamParams, k), 'i' }
#include <stddef.h>
static const struct { const char *k; size_t off; char t; } PKEYS[] = {
    PI_(tool_num), PD(tool_dia), PI_(rpm), PD(feed), PD(plunge_feed), PD(top_z), PD(final_z), PD(stepdown),
    PD(clearance), PD(feed_plane), PI_(side), PI_(roll), PD(stock_xy), PD(stepover_pct), PD(ramp_deg), PD(r_plane), PD(peck), PI_(drill_cycle),
    PD(tol), PD(lead_r), PD(lead_len), PI_(finish_n), PD(finish_step), PI_(tabs), PD(tab_w), PD(tab_h), PI_(arcs),
    PI_(cleanup), PI_(tool_type), PD(corner_r), PD(tip_angle), PI_(coolant), PI_(tip_comp),
    PI_(contour_type), PD(chamfer_w), PD(chamfer_tip), PD(tip_dia), PD(flute_len), PD(helix_pitch),
    PD(thread_major), PD(thread_pitch), PD(thread_depth), PI_(thread_internal), PI_(thread_rh), PI_(thread_passes), PI_(thread_full), PD(thread_step), PI_(thread_up),
    PI_(face_style), PD(face_angle), PD(face_overlap), PD(face_along), PI_(pocket_style), PD(pocket_angle), PD(helix_r), PD(helix_zclr), PI_(use_box), PD(box[0]), PD(box[1]), PD(box[2]), PD(box[3]),
    PI_(dyn_outside), PD(min_rad_pct), PD(micro_lift), PD(back_feed), PD(link_retract), PD(entry_r), PD(wall_stock), PD(slot_width), PI_(file_op_id),
    PD(retract_z), PD(lead_sw), PI_(lead_mid), PI_(side_rel), PI_(mc_depth_on), PI_(mc_depth_nf), PD(mc_depth_fs),
    PI_(lead_pct), PD(lead_len_pct), PD(lead_r_pct),
    PI_(feed_retract), PI_(mc_rapid_ret), PD(mc_rrate) };   /* Tool page (opparams_ui.inc tp_apply): rapid retract / retract rate */

/* the planes (cui_planes.c): written only for an op not entirely in Top (projects of Top ops stay as they were) */
static const struct { const char *k; size_t off; char t; } PKEYS_PL[] = {
    PI_(tplane), PI_(cplane), PI_(wcs), PD(torg[0]), PD(torg[1]), PD(torg[2]), PD(corg[0]), PD(corg[1]), PD(corg[2]),
    PD(worg[0]), PD(worg[1]), PD(worg[2]), PI_(wofs_manual), PI_(wofs), PI_(wwofs), PI_(cwofs) };
/* Facing on a cylindrical stock: written only when used */
static const struct { const char *k; size_t off; char t; } PKEYS_FC[] = { PI_(use_circ), PD(circ[0]), PD(circ[1]), PD(circ[2]) };
/* the separate Tab Cutoff link (cam2d.h ct_cutoff_op / ct_uid / ct_parent_uid): written only when used */
static const struct { const char *k; size_t off; char t; } PKEYS_CT[] = { PI_(ct_cutoff_op), PI_(ct_uid), PI_(ct_parent_uid) };
/* a new Lathe Face of the menu / ribbon command (lathe_newop.c): written only for such an op */
static const struct { const char *k; size_t off; char t; } PKEYS_LF[] = { PI_(lf_new), PI_(lf_slot) };
static int planes_default(const CamParams *p) {
    if (p->tplane > 1 || p->cplane > 1 || p->wcs > 1 || p->wofs_manual || p->wwofs >= 0 || p->cwofs >= 0) return 0;
    for (int k = 0; k < 3; k++) if (p->torg[k] != 0 || p->corg[k] != 0 || p->worg[k] != 0) return 0;
    return 1;
}
int cam_planes_default(const CamParams *p) { return planes_default(p); }
int cam_set_param(CamParams *p, const char *key, double v) {
    for (size_t k = 0; k < sizeof PKEYS_LF / sizeof PKEYS_LF[0]; k++) if (!strcmp(PKEYS_LF[k].k, key)) { if (!isfinite(v) || fabs(v) > 2e9) return 0; *(int *)((char *)p + PKEYS_LF[k].off) = (int)v; return 1; }
    for (size_t k = 0; k < sizeof PKEYS_CT / sizeof PKEYS_CT[0]; k++) if (!strcmp(PKEYS_CT[k].k, key)) { if (!isfinite(v) || fabs(v) > 2e9) return 0; *(int *)((char *)p + PKEYS_CT[k].off) = (int)v; return 1; }
    for (size_t k = 0; k < sizeof PKEYS_FC / sizeof PKEYS_FC[0]; k++) if (!strcmp(PKEYS_FC[k].k, key)) {
        char *base = (char *)p + PKEYS_FC[k].off;
        if (!isfinite(v) || fabs(v) > 1e9) break; if (PKEYS_FC[k].t == 'd') *(double *)base = v; else *(int *)base = (int)v;
        return 1;
    }
    for (size_t k = 0; k < sizeof PKEYS_PL / sizeof PKEYS_PL[0]; k++) if (!strcmp(PKEYS_PL[k].k, key)) {
        char *base = (char *)p + PKEYS_PL[k].off;
        if (!isfinite(v) || fabs(v) > 1e9) break; if (PKEYS_PL[k].t == 'd') *(double *)base = v; else *(int *)base = (int)v;
        return 1;
    }
    for (size_t k = 0; k < sizeof PKEYS / sizeof PKEYS[0]; k++) if (!strcmp(PKEYS[k].k, key)) {
        char *base = (char *)p + PKEYS[k].off;
        if (!isfinite(v) || fabs(v) > 1e9) break; if (PKEYS[k].t == 'd') *(double *)base = v; else *(int *)base = (int)v;   /* NaN / absurd: kept default */
        return 1;
    }
    return 0;
}

int cam_save_ops(const char *path, CamOp **ops, int nops) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# mcamui operations (regenerated when the part is opened)\n");
    int ok = cam_write_ops(f, ops, nops, NULL);
    return fclose(f) == 0 && ok;
}

void cam_op_free(CamOp *op) {
    if (!op) return;
    cam_op_free(op->edit);                               /* a dirty op's changed parameters (not computed yet) */
    for (int i = 0; i < op->nchains; i++) cam_chain_free(&op->chains[i]);
    free(op->chains); free(op->holes); free(op->m);
    free(op);
}

/* formats/surface_selection.md 2.1 / 2.2: one CamRef per TP kind 4 record of the op, in file order (tp_get_all's list order) */
int cam_refs_from_tp(const McamFile *f, int32_t op_id, CamRef *ref, int max, char *err, size_t errn) {
    int n = 0;
    for (int i = 0; i < f->ntp; i++) {
        const TpRef *t = &f->tp[i];
        if (t->op_id != op_id || t->kind != 4) continue;
        if (n >= max) { snprintf(err, errn, "operation %d: more than %d surface / solid references (a limit here)", op_id, max); return -1; }
        CamRef *r = &ref[n++]; memset(r, 0, sizeof *r);
        r->cls = t->ecls; r->idn = t->ent; r->flags = t->flags; r->link = t->link; r->index = -1;
        int ss = t->ecls == TP_ECLS_SURF ? tp_ref_subst(f, t) : -1;   /* tp_substitute_ent (Untrim / Remove Boundary in this session) */
        if (ss >= 0) { r->index = ss; r->idn = f->surfs[ss].idn; }
        else if (t->ecls == TP_ECLS_SURF) { for (int k = 0; k < f->nsurfs; k++) if (f->surfs[k].idn == t->ent) { r->index = k; break; } }
        else if (t->ecls == TP_ECLS_SOLID) { for (int k = 0; k < f->nsolids; k++) if (f->solids[k].idn == t->ent) { r->index = k; break; } }
    }
    return n;
}
void cam_op_set_drive(CamOp *op, const McamFile *f, const int *solids, int nsolids, const int *surfs, int nsurfs) {
    op->nref = 0; op->nrface = 0;
    for (int i = 0; i < nsolids && op->nref < CAM_REF_MAX; i++) { CamRef *r = &op->ref[op->nref++]; memset(r, 0, sizeof *r);
        r->cls = TP_ECLS_SOLID; r->index = solids[i]; r->idn = f && solids[i] >= 0 && solids[i] < f->nsolids ? f->solids[solids[i]].idn : 0; }
    for (int i = 0; i < nsurfs && op->nref < CAM_REF_MAX; i++) { CamRef *r = &op->ref[op->nref++]; memset(r, 0, sizeof *r);
        r->cls = TP_ECLS_SURF; r->index = surfs[i]; r->idn = f && surfs[i] >= 0 && surfs[i] < f->nsurfs ? f->surfs[surfs[i]].idn : 0; }
}

int cam_write_ops(FILE *f, CamOp **ops, int nops, const McamFile *mf) {
    for (int i = 0; i < nops; i++) {
        CamOp *op = ops[i];
        fprintf(f, "OP %d %s\n", op->p.type, op->name);
        for (size_t k = 0; k < sizeof PKEYS / sizeof PKEYS[0]; k++) {
            const char *base = (const char *)&op->p + PKEYS[k].off;
            if (PKEYS[k].t == 'd') fprintf(f, "P %s %.10g\n", PKEYS[k].k, *(const double *)base);
            else fprintf(f, "P %s %d\n", PKEYS[k].k, *(const int *)base);
        }
        if (!planes_default(&op->p)) for (size_t k = 0; k < sizeof PKEYS_PL / sizeof PKEYS_PL[0]; k++) {
            const char *base = (const char *)&op->p + PKEYS_PL[k].off;
            if (PKEYS_PL[k].t == 'd') fprintf(f, "P %s %.10g\n", PKEYS_PL[k].k, *(const double *)base);
            else fprintf(f, "P %s %d\n", PKEYS_PL[k].k, *(const int *)base);
        }
        if (op->p.ct_cutoff_op || op->p.ct_uid || op->p.ct_parent_uid) for (size_t k = 0; k < sizeof PKEYS_CT / sizeof PKEYS_CT[0]; k++)
            fprintf(f, "P %s %d\n", PKEYS_CT[k].k, *(const int *)((const char *)&op->p + PKEYS_CT[k].off));
        if (op->p.lf_new) for (size_t k = 0; k < sizeof PKEYS_LF / sizeof PKEYS_LF[0]; k++)
            fprintf(f, "P %s %d\n", PKEYS_LF[k].k, *(const int *)((const char *)&op->p + PKEYS_LF[k].off));
        if (op->p.use_circ) for (size_t k = 0; k < sizeof PKEYS_FC / sizeof PKEYS_FC[0]; k++) {
            const char *base = (const char *)&op->p + PKEYS_FC[k].off;
            if (PKEYS_FC[k].t == 'd') fprintf(f, "P %s %.10g\n", PKEYS_FC[k].k, *(const double *)base);
            else fprintf(f, "P %s %d\n", PKEYS_FC[k].k, *(const int *)base);
        }
        for (int c = 0; c < op->nchains; c++) {
            fprintf(f, "CHAIN %d", op->chains[c].closed);
            if (op->chains[c].avoid) fprintf(f, " A");
            else if (op->chains[c].role) fprintf(f, " R%d", op->chains[c].role);   /* roles 5 / 6 / 7 (2D High Speed: refused, kept) */
            if (op->chains[c].cut0 > 0 || op->chains[c].cut1 > 0) fprintf(f, " T%.10g,%.10g", op->chains[c].cut0, op->chains[c].cut1);   /* Dynamic start / end */
            if (op->chains[c].solid_idn > 0) fprintf(f, " S%d", op->chains[c].solid_idn);   /* its edges are rebuilt on load */
            else if (op->chains[c].sld && mf) {               /* solid chaining's curves (Ent.gen): their geometry */
                fprintf(f, " L");
                for (int e = 0; e < op->chains[c].n; e++) { int ei = op->chains[c].ent[e]; if (ei < 0 || ei >= mf->nents) continue; const Ent *en = &mf->ents[ei];
                    fprintf(f, " G%d,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.12g,%.12g:%d", en->type, en->a.x, en->a.y, en->a.z, en->b.x, en->b.y, en->b.z,
                            en->c.x, en->c.y, en->c.z, en->r, en->a0, en->sw, op->chains[c].rev[e]); }
            }
            else for (int e = 0; e < op->chains[c].n; e++) {   /* index:rev@type,start,end - the geometry is checked on load */
                int ei = op->chains[c].ent[e]; fprintf(f, " %d:%d", ei, op->chains[c].rev[e]);
                if (mf && ei >= 0 && ei < mf->nents) { const Ent *en = &mf->ents[ei];
                    fprintf(f, "@%d,%.6f,%.6f,%.6f,%.6f", en->type, en->a.x, en->a.y, en->b.x, en->b.y); }
            }
            fprintf(f, "\n");
        }
        for (int h = 0; h < op->nholes; h++) fprintf(f, "HOLE %.10g %.10g %.10g\n", op->holes[h].x, op->holes[h].y, op->holes[h].d);
        for (int d = 0; d < op->nref; d++) {   /* 3D references: REF <class hex> <index into F.surfs / F.solids> <idn as a check> <flags hex> <link> */
            const CamRef *r = &op->ref[d]; int32_t idn = r->idn;
            if (mf && r->cls == TP_ECLS_SOLID && r->index >= 0 && r->index < mf->nsolids) idn = mf->solids[r->index].idn;
            if (mf && r->cls == TP_ECLS_SURF && r->index >= 0 && r->index < mf->nsurfs) idn = mf->surfs[r->index].idn;
            fprintf(f, "REF %x %d %d %x %d\n", (unsigned)r->cls, r->index, idn, r->flags, r->link);
        }
        for (int d = 0; d < op->nrface; d++)   /* S4: a solid face picked here: RFACE <index into the REF lines> <face ordinal> <type hex> */
            fprintf(f, "RFACE %d %d %x\n", op->rface[d].ref, op->rface[d].face, (unsigned)op->rface[d].type);
        for (int t = 0; t < op->p.ntab_xy && t < CAM_TAB_MAX; t++) fprintf(f, "TAB %.10g %.10g\n", op->p.tab_xy[2 * t], op->p.tab_xy[2 * t + 1]);
        for (int k = 0; k < op->p.nrpatch; k++) {             /* RP <record offset> <height> <hex bytes> */
            fprintf(f, "RP %u %u ", op->p.rpatch[k].off, op->p.rpatch[k].height);
            for (int b = 0; b < op->p.rpatch[k].n; b++) fprintf(f, "%02x", op->p.rpatch[k].v[b]);
            fprintf(f, "\n");
        }
        fprintf(f, "END\n");
    }
    return !ferror(f);
}

/* entity K is (still) of type TY from (g0, g1) to (g2, g3) */
static int ent_matches(const McamFile *mf, int k, int ty, const double *g) {
    if (k < 0 || k >= mf->nents) return 0;
    const Ent *en = &mf->ents[k];
    return en->type == ty && fabs(en->a.x - g[0]) < 1e-5 && fabs(en->a.y - g[1]) < 1e-5 && fabs(en->b.x - g[2]) < 1e-5 && fabs(en->b.y - g[3]) < 1e-5;
}
int cam_load_remap_from, cam_load_remap_by;
int cam_load_ops(const char *path, McamFile *mf, CamOp ***ops_out) { return cam_load_ops_ord(path, mf, ops_out, NULL); }
/* ORD (optional): for each op returned, its position among the OP blocks of the file (ops that fail are skipped) */
int cam_load_ops_ord(const char *path, McamFile *mf, CamOp ***ops_out, int **ord) {
    FILE *f = fopen(path, "r");
    *ops_out = NULL; if (ord) *ord = NULL;
    if (!f) return 0;
    CamOp **ops = NULL; int n = 0, nth = -1; CamOp *cur = NULL; int saw_cycle = 0, bad = 0;
    char *line = NULL; size_t lcap = 0;                   /* any length: a long chain is one line */
    while (read_line(f, &line, &lcap)) {
        if (!strncmp(line, "OP ", 3)) {
            saw_cycle = 0; bad = 0; nth++;
            cur = xcalloc(1, sizeof(CamOp));
            int type = atoi(line + 3);
            cam_defaults(&cur->p, type, mf);
            const char *nm = strchr(line + 3, ' ');
            snprintf(cur->name, sizeof cur->name, "%s", nm ? nm + 1 : "op");
        } else if (cur && !strncmp(line, "P ", 2)) {
            char key[64]; double v;
            if (sscanf(line + 2, "%63s %lf", key, &v) == 2) {
                /* PKEYS, and the planes / Facing-circle keys written only when used (PKEYS_PL / PKEYS_FC: were written
                   but not read back before 2026-10-22) - cam_set_param; NaN / absurd: kept default */
                if (cam_set_param(&cur->p, key, v) && !strcmp(key, "drill_cycle")) saw_cycle = 1;
            }
        } else if (cur && !strncmp(line, "CHAIN ", 6)) {
            cur->chains = realloc(cur->chains, (cur->nchains + 1) * sizeof(Chain));
            Chain *c = &cur->chains[cur->nchains++]; memset(c, 0, sizeof *c);
            char *p = line + 6; c->closed = (int)strtol(p, &p, 10);
            int cap = 16; c->ent = malloc(cap * sizeof(int)); c->rev = malloc(cap * sizeof(int));
            while (*p) {
                while (*p == ' ') p++;
                if (!*p) break;
                if (*p == 'A') { c->avoid = 1; c->role = 4; p++; continue; }
                if (*p == 'R') { c->role = (int)strtol(p + 1, &p, 10); continue; }
                if (*p == 'L') { c->sld = 1; p++; continue; }
                if (*p == 'G') {                               /* a solid chain's curve: appended as an Ent.gen entity */
                    double g[13] = { 0 }; int ty = (int)strtol(p + 1, &p, 10), r = 0;
                    for (int k = 0; k < 13 && *p == ','; k++) g[k] = strtod(p + 1, &p);
                    if (*p == ':') r = (int)strtol(p + 1, &p, 10);
                    if (ty != E_LINE && ty != E_ARC) { bad = 1; continue; }
                    Ent *ne = realloc(mf->ents, sizeof(Ent) * (mf->nents + 1)); if (!ne) { bad = 1; continue; }
                    mf->ents = ne; Ent *en = &mf->ents[mf->nents]; memset(en, 0, sizeof *en);
                    en->type = ty; en->cls = ty; en->idn = -1; en->gen = 1; en->ln_style = 1; en->first_poly = mf->npolys;
                    en->a = (V3){ g[0], g[1], g[2] }; en->b = (V3){ g[3], g[4], g[5] }; en->c = (V3){ g[6], g[7], g[8] }; en->r = g[9]; en->a0 = g[10]; en->sw = g[11];
                    en->planar = ty == E_ARC || fabs(g[2] - g[5]) < 1e-9;
                    if (c->n == cap) { cap *= 2; c->ent = realloc(c->ent, cap * sizeof(int)); c->rev = realloc(c->rev, cap * sizeof(int)); }
                    c->ent[c->n] = mf->nents++; c->rev[c->n] = r; c->n++;
                    continue;
                }
                if (*p == 'T') { c->cut0 = strtod(p + 1, &p); if (*p == ',') c->cut1 = strtod(p + 1, &p); continue; }
                if (*p == 'S') {                               /* solid chain: the top-face loop of that solid */
                    int idn = (int)strtol(p + 1, &p, 10), m, first = cam_solid_chain_ents(mf, idn, &m);
                    for (int j = 0; j < m; j++) {
                        if (c->n == cap) { cap *= 2; c->ent = realloc(c->ent, cap * sizeof(int)); c->rev = realloc(c->rev, cap * sizeof(int)); }
                        c->ent[c->n] = first + j; c->rev[c->n] = 0; c->n++;
                    }
                    if (m > 0) c->solid_idn = idn;
                    continue;
                }
                char *p0 = p; int e = (int)strtol(p, &p, 10), r = 0;
                if (p == p0) { p++; continue; }                /* not a number: skip the character */
                e = cam_load_ent_remap(e);                     /* an older project's index (cam2d.h) */
                if (*p == ':') r = (int)strtol(p + 1, &p, 10);
                if (*p == '@') {                               /* the entity it was: find it again if indices moved */
                    int ty = 0; double g[4] = { 0 };
                    ty = (int)strtol(p + 1, &p, 10);
                    for (int k = 0; k < 4 && *p == ','; k++) g[k] = strtod(p + 1, &p);
                    if (!ent_matches(mf, e, ty, g)) { e = -1; for (int k = 0; k < mf->nents; k++) if (ent_matches(mf, k, ty, g)) { e = k; break; } }
                }
                if (e < 0 || e >= mf->nents) { bad = 1; continue; }   /* part changed: the op is not regenerated on other geometry */
                if (c->n == cap) { cap *= 2; c->ent = realloc(c->ent, cap * sizeof(int)); c->rev = realloc(c->rev, cap * sizeof(int)); }
                c->ent[c->n] = e; c->rev[c->n] = r; c->n++;
            }
        } else if (cur && !strncmp(line, "TAB ", 4)) {          /* tab centres (Mastercam's tab positions) */
            double x, y;
            if (cur->p.ntab_xy < CAM_TAB_MAX && sscanf(line + 4, "%lf %lf", &x, &y) == 2) { cur->p.tab_xy[2 * cur->p.ntab_xy] = x; cur->p.tab_xy[2 * cur->p.ntab_xy + 1] = y; cur->p.ntab_xy++; }
        } else if (cur && !strncmp(line, "RP ", 3)) {           /* Edit Common Parameters record bytes */
            unsigned off, ht; char hx[40]; int k = cur->p.nrpatch;
            if (k >= CAM_NRPATCH) cam_rpatch_full(-1);
            else if (sscanf(line + 3, "%u %u %39s", &off, &ht, hx) == 3 && strlen(hx) % 2 == 0 && strlen(hx) <= 16) {
                cur->p.rpatch[k].off = (uint16_t)off; cur->p.rpatch[k].height = (uint8_t)ht; cur->p.rpatch[k].n = (uint8_t)(strlen(hx) / 2);
                for (int b = 0; b < cur->p.rpatch[k].n; b++) { unsigned v; sscanf(hx + 2 * b, "%2x", &v); cur->p.rpatch[k].v[b] = (uint8_t)v; }
                cur->p.nrpatch++;
            }
        } else if (cur && !strncmp(line, "HOLE ", 5)) {
            cur->holes = realloc(cur->holes, (cur->nholes + 1) * sizeof(Hole));
            Hole *h = &cur->holes[cur->nholes]; h->d = 0;
            if (sscanf(line + 5, "%lf %lf %lf", &h->x, &h->y, &h->d) >= 2) cur->nholes++;
        } else if (cur && !strncmp(line, "REF ", 4)) {           /* a 3D reference (cam_write_ops); solids / surfaces by idn when the part has it, else the index */
            unsigned cls = 0, fl = 0; int ix = -1, idn = 0, link = 0;
            if (sscanf(line + 4, "%x %d %d %x %d", &cls, &ix, &idn, &fl, &link) == 5) {
                if (cur->nref >= CAM_REF_MAX) bad = 1;
                else {
                    if (cls == TP_ECLS_SOLID) { if (idn > 0) for (int s = 0; s < mf->nsolids; s++) if (mf->solids[s].idn == idn) { ix = s; break; }
                                                if (ix < 0 || ix >= mf->nsolids) bad = 1; }
                    else if (cls == TP_ECLS_SURF) { if (idn != 0) for (int s = 0; s < mf->nsurfs; s++) if (mf->surfs[s].idn == idn) { ix = s; break; }
                                                    if (ix < 0 || ix >= mf->nsurfs) bad = 1; }
                    CamRef *r = &cur->ref[cur->nref++]; r->cls = cls; r->index = ix; r->idn = idn; r->flags = fl; r->link = link;
                }
            } else bad = 1;
        } else if (cur && !strncmp(line, "RFACE ", 6)) {         /* S4: a picked solid face of the REF line given (cam_write_ops) */
            int rf = -1, fc = -1; unsigned ty = 0;
            if (sscanf(line + 6, "%d %d %x", &rf, &fc, &ty) == 3 && rf >= 0 && rf < cur->nref && cur->ref[rf].cls == TP_ECLS_SOLID && fc >= 0 && cur->nrface < CAM_REF_FACE_MAX) {
                CamRefFace *q = &cur->rface[cur->nrface++]; q->ref = rf; q->face = fc; q->type = (int)ty;
            } else bad = 1;
        } else if (cur && !strncmp(line, "DRIVE ", 6)) {         /* older files: a drive solid; by idn when the part has it, else the index */
            int ix = -1, idn = 0;
            if (sscanf(line + 6, "%d %d", &ix, &idn) >= 1 && cur->nref < CAM_REF_MAX) {
                if (idn > 0) for (int s = 0; s < mf->nsolids; s++) if (mf->solids[s].idn == idn) { ix = s; break; }
                if (ix >= 0 && ix < mf->nsolids) { CamRef *r = &cur->ref[cur->nref++]; memset(r, 0, sizeof *r); r->cls = TP_ECLS_SOLID; r->index = ix; r->idn = mf->solids[ix].idn; }
                else bad = 1;
            }
        } else if (cur && !strncmp(line, "DSURF ", 6)) {         /* older files: a drive surface; by idn when the part has it, else the index */
            int ix = -1, idn = 0;
            if (sscanf(line + 6, "%d %d", &ix, &idn) >= 1 && cur->nref < CAM_REF_MAX) {
                if (idn != 0) for (int s = 0; s < mf->nsurfs; s++) if (mf->surfs[s].idn == idn) { ix = s; break; }
                if (ix >= 0 && ix < mf->nsurfs) { CamRef *r = &cur->ref[cur->nref++]; memset(r, 0, sizeof *r); r->cls = TP_ECLS_SURF; r->index = ix; r->idn = mf->surfs[ix].idn; }
                else bad = 1;
            }
        } else if (cur && !strcmp(line, "END")) {
            if (!saw_cycle && cur->p.type == CAM_DRILL) cur->p.drill_cycle = cur->p.peck > 0;   /* older files: peck > 0 meant G83 */
            cur->p.lead_sw_rec = 0;                            /* the sweeps are the sidecar's keys (0 = a quarter); record patches
                                                                  set the bits again (cam_params_from_patches; cam2d.h) */
            cam_lead_sidecar_compat(&cur->p);                  /* files with one lead for both sides (cam_fileop.c) */
            if (cur->p.type == CAM_CONTOUR && cur->p.contour_type == 2) cam_contour_engrave_compat(&cur->p);   /* older projects' "Engrave" contours */
            cam_params_from_patches(&cur->p);                  /* the op dialog's record edits (cam_fileop.c; none in older files) */
            /* S3 (formats/new_toolpath_audit.md, user answer Q5): an op whose generation fails is kept, dirty, with its error and no
               moves - Mastercam keeps every operation of a part as its record (with its dirty flag op +0x94 = record +0xfc: InitTvItem's
               red X, DirtyOperationWarning; mcam_load.c) and never computes one on open. Missing references (bad) still drop it (ours) */
            if (!bad) {
                if (!cam_generate(mf, cur)) { free(cur->m); cur->m = NULL; cur->nm = 0; cur->dirty |= CAM_DIRTY_PARAMS; }
                ops = xrealloc(ops, (n + 1) * sizeof(CamOp *));
                if (ord) { *ord = xrealloc(*ord, (n + 1) * sizeof(int)); (*ord)[n] = nth; }
                ops[n++] = cur;
            }
            else { free(cur); }
            cur = NULL;
        }
    }
    free(line);
    fclose(f);
    *ops_out = ops;
    return n;
}
