/* cui_snap.c - AutoCursor: the snap passes of FUN_1804e1bb0 (geo_snap), the consumer-side angular snap, the
   Temporary Midpoints capture, the AutoCursor Settings flags (formats/ui.md "Selection and AutoCursor"). Moved from
   viewer/mcamui.c; the cursor markers are built by ov_fast_marker (cui_overlay.c). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include <time.h>
#include "xt_mesh.h"
#include "occt_cad.h"

/* AutoCursor snap modes (opmanres dialog 779 "AutoCursor Settings", doAutocursorDlg @0x1800db460; the original's
   default enabled set is NOT DECODED - the implemented modes default on here). Mode index <-> checkbox from
   mastercam_acinit.c / mastercam_acok.c: Origin 333 = 1, Center 334 = 2, Endpoint 336 = 3, Intersect 337 = 4,
   Midpoint 339 = 5, Point 341 = 6, Quadrant 343 = 9, Angular 352 (+ degrees edit 289) = 0x18, Tangent 345 = 0x15,
   Perpendicular 347 = 0x16, Nearest 350 = 0x17, Horizontal-Vertical 359 = 0x19, Temporary Midpoints 360 = 0x1A.
   Fast Point 357 / power keys 362 are not ported (no AutoCursor bar). */
int ac_snap[ACS_N] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
double ac_ang_step = 1.0;                        /* Angular step degrees (dialog edit 289; clamped [1e-6, 90],
                                                           mastercam_acok.c:30. The original's default is NOT DECODED - 1.0 is ours) */
int geo_snap_feat_ent = -1, geo_snap_feat = -1;    /* the feature pass's winner: entity, candidate index (a line's 0 start / 1 end; cui_draft.c) */
int geo_snap_kind = 99;                          /* pass kind of the last geo_snap hit (the GK_ enum at geo_snap) */
/* captured Temporary Midpoints (mode 0x1A): two slots with the toggle / promote semantics of FUN_18001baa0
   (decomp/mccore_tempmid.c:705-765), cleared when a point-entry command ends (FUN_180708450 via
   decomp/mccore_getevent.c:105-107). The original's third "live" slot (FUN_18001b9e0) is NOT DECODED (its hover
   maintenance is undumped) and not ported. */
V3 ac_tmid[2]; int ac_tmid_ok[2];
void ac_tmid_clear(void) { ac_tmid_ok[0] = ac_tmid_ok[1] = 0; }   /* FUN_180708450: cleared when point entry ends */

/* AutoCursor snap of a world point: the decoded pass order of FUN_1804e1bb0 (decomp/mccore_autocursor2.c:381-800):
   (enum GK_* in core_ui.h)
   GK_TAN    tangent (dl_search_fast_tpn do_t, decomp/mastercam_dlsearch_tpn.c; needs the anchor; arcs only with a
             line-like rubber band - FUN_180131c50:604-609 refuses lines - and never on the anchor's own entity)
   GK_POINT..GK_CENTER  feature points (dl_search_fast, decomp/mastercam_dlsearch.c): point entities, endpoints,
             midpoints (of an arc: the mid-angle point), quadrants, arc centres; nearest wins
   GK_INT    intersections - inside the feature pass, ONLY when no feature hit (mastercam_dlsearch.c:963,1256-1258):
             the two entities closest to the cursor, carrier intersection (intersect_ents, decomp/mccore_inters.c;
             no segment / sweep clamping in the search phase), the hit nearer the midpoint of the two
             closest-approach points (mccore_inters.c:149-152)
   GK_ORIGIN the C-plane origin at the FULL sel_dist
   GK_TCTR   Temporary Center Point: the centre of the bounding box of all geometry
             (CalcAutoCursorTemporaryCenterPoint, decomp/mccore_tempmid.c:138-242; force-enabled during point entry,
             decomp/mccore_getevent.c:100-104; no dialog 779 checkbox)
   GK_TMID   the captured Temporary Midpoints: slot order, first within the aperture wins
             (mccore_autocursor2.c:610-640)
   GK_PERP   perpendicular (dl_search_fast_tpn do_p, FUN_180131620): the anchor's OWN entity snaps to the
             point at the drag distance along the in-plane normal (param_6 = 1, :845-876); any other entity
             snaps to the foot of the perpendicular from the anchor (:878-889)
   GK_GRID   the grid (sel_grid_search; the use_grid argument)
   GK_HV     horizontal / vertical about the anchor (FUN_1804e1bb0:558-606: H preferred, sel_dist in view units)
   GK_NEAR   nearest point on an entity (dl_search_fast_tpn do_n)
   Entity passes use half the selection aperture (sel_dist * 0.5: mastercam_dlsearch.c dVar6, dlsearch_tpn dVar2);
   the tangent / perpendicular candidates substitute their angle pre-check for the aperture (no distance re-test in
   the original). Per-mode on/off = ac_snap[]. geo_snap_kind = the winning pass.
   anchor = the rubber-band anchor (the previous picked point); rb = the command's rubber-band type (see
   geo_rb_type): the tpn passes need the {0,17,35} mask, H-V {0,17,32,35}, tangent also allows the arc-like
   {5,6,8,18,20} (FUN_180131c50:611-614, not ported - see geo_rb_type). */

P2 xy(V3 p) { P2 q = { p.x, p.y }; return q; }
GE ent_ge(int i) {
    const Ent *e = &F.ents[i];
    if (e->type == E_LINE) return ge_line(xy(e->a), xy(e->b), e->a.z);
    if (e->type == E_ARC) return ge_arc(xy(e->c), e->r, e->a0, e->sw, e->c.z);
    return ge_point(xy(e->a), e->a.z);
}

/* the AutoCursor angle pre-checks fold EACH atan2 angle to [0, pi] (+2pi when negative, -pi when above pi,
   then exact 0 / pi -> 0, strictly within 1e-6) and compare the plain difference (FUN_180131c50:651-699,
   FUN_180131620:792-844); the difference is NOT folded to [0, pi/2] - folded angles straddling 0 / pi never
   pass (the original's quirk) */
double angfold_pi(double a) {
    if (a < 0) a += 2 * M_PI;
    if (a > M_PI) a -= M_PI;
    if (fabs(a) < 1e-6 || fabs(a - M_PI) < 1e-6) a = 0;
    return a;
}
/* ---- AutoCursor on solids (formats/ui.md "AutoCursor on solids"; decomp/mcgeomsld_snap.c, mastercam_dlsearch.c,
   mastercam_dlsearch_tpn.c). The bodies shown last (bodies_show: the Extrude bodies or the panel's preview) are kept
   here with their edges (solid25.c body_brep_edges - rebuilt from the slabs, this kernel has no Parasolid B-rep). */
static Body *sn_b; static int sn_nb; static BEdge **sn_e; static int *sn_ne;
extern int ac_solids_gen;
void ac_solids_set(const Body *bs, int nb) {
    ac_solids_gen++;
    for (int i = 0; i < sn_nb; i++) { body_free(&sn_b[i]); bedges_free(sn_e[i], sn_ne[i]); }
    free(sn_b); free(sn_e); free(sn_ne); sn_b = NULL; sn_e = NULL; sn_ne = NULL; sn_nb = 0;
    if (!bs || nb <= 0) return;
    sn_b = malloc(sizeof(Body) * nb); sn_e = malloc(sizeof(BEdge *) * nb); sn_ne = malloc(sizeof(int) * nb);
    for (int i = 0; i < nb; i++) { sn_b[i] = body_copy(&bs[i]); sn_ne[i] = body_brep_edges(&bs[i], &sn_e[i]); }
    sn_nb = nb;
}
/* the part file's own solids (Parasolid XT, formats/xt.md "Edges"): their B-rep edges read from the XT (xt_mesh.c
   xt_brep_edges: LINE / CIRCLE edges, model units = metres x solid_scale) - edge sets sn_nb .. sn_nb + sp_n - 1,
   rebuilt when the part's solids change */
static BEdge **sp_e; static int *sp_ne, sp_n; static const void *sp_key; static int sp_keyn; static double sp_keys; static size_t sp_keysz;
int ac_part_skipped;                               /* XT edges left out (curve classes / sense not decoded) */
static void part_edges_sync(void) {
    size_t sz = 0; for (int i = 0; i < F.nsolids; i++) sz += F.solids[i].xt_size;
    if (sp_key == (const void *)F.solids && sp_keyn == F.nsolids && sp_keys == solid_scale && sp_keysz == sz) return;
    for (int i = 0; i < sp_n; i++) bedges_free(sp_e[i], sp_ne[i]);
    free(sp_e); free(sp_ne); sp_e = NULL; sp_ne = NULL; sp_n = 0; ac_part_skipped = 0;
    sp_key = F.solids; sp_keyn = F.nsolids; sp_keys = solid_scale; sp_keysz = sz; ac_solids_gen++;
    if (F.nsolids <= 0) return;
    sp_e = calloc(F.nsolids, sizeof(BEdge *)); sp_ne = calloc(F.nsolids, sizeof(int));
    for (int i = 0; i < F.nsolids; i++) {
        XtBEdge *x = NULL; int sk = 0, n;
        if (F.solids[i].brep) n = occt_brep_edges(F.solids[i].brep, F.solids[i].brep_n, &x, &sk);     /* an imported solid: lines / circles, the part's units */
        else { XtSchema *sc = xt_schema_for(F.solids[i].xt, F.solids[i].xt_size); n = sc ? xt_brep_edges(sc, F.solids[i].xt, F.solids[i].xt_size, solid_scale, &x, &sk) : 0; }
        ac_part_skipped += sk;
        sp_e[i] = calloc(n ? n : 1, sizeof(BEdge));
        for (int k = 0; k < n; k++) {
            BEdge *E = &sp_e[i][k]; E->pts = x[k].pts; x[k].pts = NULL; E->np = x[k].np; E->arc = x[k].arc; E->r = x[k].r;
            memcpy(E->c, x[k].c, sizeof E->c); memcpy(E->ax, x[k].ax, sizeof E->ax); memcpy(E->xa, x[k].xa, sizeof E->xa);
            E->t0 = x[k].t0; E->t1 = x[k].t1;
        }
        sp_ne[i] = n; xt_bedges_free(x, n);
    }
    sp_n = F.nsolids;
}
static int es_n(void) { return sn_nb + sp_n; }
static const BEdge *es(int b, int *n) {
    if (b < sn_nb) { if (n) *n = sn_ne[b]; return sn_e[b]; }
    if (n) *n = sp_ne[b - sn_nb]; return sp_e[b - sn_nb];
}
int ac_solid_edges(int b, const BEdge **e) { part_edges_sync(); int n = 0; if (b < 0 || b >= es_n()) { *e = NULL; return 0; } *e = es(b, &n); return n; }
/* the part solids' display mesh, for IsPosVisible: the nearest triangle along the ray (Moller-Trumbore) */
/* ours (speed): the triangles binned by their box in the view plane (rows 0 / 1 of the view), rebuilt when the mesh or the
   view direction changes; a ray along the view through a point can only hit triangles whose box holds the point, so testing
   the point's cell gives the same nearest hit as testing all (an unbuilt grid = all, as before) */
#define PG_N 64
static struct { const float *t; int nt; double v[6], x0, y0, sx, sy; int *start, *idx, ok; } pg;
static void pg_free(void) { free(pg.start); free(pg.idx); memset(&pg, 0, sizeof pg); }
static void pg_sync(const float *t, int nt) {
    double v[6] = { vmat[0], vmat[1], vmat[2], vmat[3], vmat[4], vmat[5] };
    if (pg.ok && pg.t == t && pg.nt == nt && !memcmp(pg.v, v, sizeof v)) return;
    pg_free(); pg.t = t; pg.nt = nt; memcpy(pg.v, v, sizeof v);
    if (nt < 256) return;                                  /* small meshes: test them all */
    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
    for (int i = 0; i < 3 * nt; i++) { const float *P = t + 6 * i; double x = v[0] * P[0] + v[1] * P[1] + v[2] * P[2], y = v[3] * P[0] + v[4] * P[1] + v[5] * P[2];
        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    double m = 1e-6 * (1 + fabs(x1 - x0) + fabs(y1 - y0)); x0 -= m; x1 += m; y0 -= m; y1 += m;
    pg.x0 = x0; pg.y0 = y0; pg.sx = (x1 - x0) / PG_N; pg.sy = (y1 - y0) / PG_N;
    if (!(pg.sx > 0) || !(pg.sy > 0)) return;
    int *cnt = calloc(PG_N * PG_N + 1, sizeof(int)); if (!cnt) return;
    size_t tot = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nt; i++) {
            double bx0 = 1e300, bx1 = -1e300, by0 = 1e300, by1 = -1e300;
            for (int k = 0; k < 3; k++) { const float *P = t + 18 * i + 6 * k; double x = v[0] * P[0] + v[1] * P[1] + v[2] * P[2], y = v[3] * P[0] + v[4] * P[1] + v[5] * P[2];
                if (x < bx0) bx0 = x; if (x > bx1) bx1 = x; if (y < by0) by0 = y; if (y > by1) by1 = y; }
            int cx0 = (int)((bx0 - m - x0) / pg.sx), cx1 = (int)((bx1 + m - x0) / pg.sx), cy0 = (int)((by0 - m - y0) / pg.sy), cy1 = (int)((by1 + m - y0) / pg.sy);
            if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0; if (cx1 >= PG_N) cx1 = PG_N - 1; if (cy1 >= PG_N) cy1 = PG_N - 1;
            for (int cy = cy0; cy <= cy1; cy++) for (int cx = cx0; cx <= cx1; cx++) {
                int c = cy * PG_N + cx;
                if (!pass) { cnt[c]++; tot++; } else pg.idx[pg.start[c] + cnt[c]++] = i;
            }
        }
        if (!pass) {
            pg.start = malloc(sizeof(int) * (PG_N * PG_N + 1)); pg.idx = malloc(sizeof(int) * (tot ? tot : 1));
            if (!pg.start || !pg.idx || tot > 0x7fffffff) { free(cnt); pg_free(); pg.t = t; pg.nt = nt; memcpy(pg.v, v, sizeof v); return; }
            int acc = 0; for (int c = 0; c < PG_N * PG_N; c++) { pg.start[c] = acc; acc += cnt[c]; cnt[c] = 0; } pg.start[PG_N * PG_N] = acc;
        }
    }
    free(cnt); pg.ok = 1;
}
static int part_hit(const double o[3], const double d[3], double *tout) {
    const float *t; int nt = scene_part_tris(&t); double best = 1e300;
    pg_sync(t, nt);
    int lo = 0, hi = nt; const int *ix = NULL;
    if (pg.ok) {                                           /* the cell of the ray's point (o lies on it: o = point + L vz) */
        double x = pg.v[0] * o[0] + pg.v[1] * o[1] + pg.v[2] * o[2], y = pg.v[3] * o[0] + pg.v[4] * o[1] + pg.v[5] * o[2];
        int cx = (int)floor((x - pg.x0) / pg.sx), cy = (int)floor((y - pg.y0) / pg.sy);
        if (cx < 0 || cy < 0 || cx >= PG_N || cy >= PG_N) return 0;
        int c = cy * PG_N + cx; lo = pg.start[c]; hi = pg.start[c + 1]; ix = pg.idx;
    }
    for (int j = lo; j < hi; j++) {
        int i = ix ? ix[j] : j;
        const float *A = t + 18 * i, *B = A + 6, *C = A + 12;
        double e1[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, e2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
        double p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
        double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2]; if (fabs(det) < 1e-18) continue;
        double s[3] = { o[0] - A[0], o[1] - A[1], o[2] - A[2] }, u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
        if (u < 0 || u > 1) continue;
        double q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
        double v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det; if (v < 0 || u + v > 1) continue;
        double tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
        if (tt >= 0 && tt < best) best = tt;
    }
    if (best == 1e300) return 0;
    *tout = best; return 1;
}

/* IsPosVisible (mastercam.dll 0x1802064a0, decomp/mastercam_dlsearch_tpn.c:899-962): when MayBeHidden (0x180206ad0:
   render_cfg 3 / 4 = shaded, not translucent - here solid_mode 1, the opaque shaded solids) the solid face under the
   point (FUN_180207a70 -> sld_pick_solid_from_cursor, the face nearest the viewer) hides it when it lies in front by
   more than |z * 1e-5 * 150| (z = the point's view depth; param_4 = 1e-5 at every AutoCursor call). Translucent
   solids (solid_mode 2): MayBeHidden also needs gl_translucent_selection, whose default is NOT DECODED - no hiding
   (left out). The part file's own (Parasolid XT) solids hide through their display mesh (part_hit, ours: the mesh stands in for the faces). */
static int pos_visible(double x, double y, double z) {
    const float *pt_; int npt = scene_part_tris(&pt_);
    if (solid_mode != 1 || (!sn_nb && !npt)) return 1;
    const double vz[3] = { vmat[6], vmat[7], vmat[8] }, L = 1e6;
    double o[3] = { x + L * vz[0], y + L * vz[1], z + L * vz[2] }, d[3] = { -vz[0], -vz[1], -vz[2] }, tb = 1e300;
    for (int i = 0; i < sn_nb; i++) { double t; if (body_hit(&sn_b[i], o, d, &t) && t < tb) tb = t; }
    if (npt) { double t; if (part_hit(o, d, &t) && t < tb) tb = t; }   /* the part's own solids: their display mesh (ours) */
    if (tb == 1e300) return 1;
    double zc = x * vz[0] + y * vz[1] + z * vz[2], zh = zc + L - tb;
    return !(zc < zh && fabs(zc * 1e-5 * 150.0) < fabs(zc - zh));
}

/* ps_select (mcgeomsld 0x18004fbd0, decomp/mcgeomsld_snap.c:3-430; :229 the distance, :349 the test): PK_BODY_pick_topols along the cursor ray with the maximum edge distance
   10 / get_x_scr_scl (get_x_scr_scl = view +0x768 = pixels per unit, decomp/mastercam_viewrot.c:332) = 10 px; each
   edge hit keeps the point of the edge nearest the ray (psHit +0x10) and its distance from the ray's start in front
   of the view (psHit +0x28: the depth order). Here: the edge's screen distance <= 10 px, the point at the closest
   screen parameter (parallel view), sorted front to back. PK's own result order and its 30-hit cap are NOT DECODED
   (sld_search_fast's / sld_select's sorts make it front to back; all hits are kept here). */
#define SLD_PICK_PX 10.0
static V3 arc_pt(const BEdge *E, double t);
static int arc_nearest(const BEdge *E, float sx0, float sy0, double *tout, double *dpx);
typedef struct { int b, e; double depth, dpx; V3 cp; } SHit;
static int shit_cmp(const void *x, const void *y) {
    const SHit *p = x, *q = y; return p->depth != q->depth ? (p->depth > q->depth ? -1 : 1) : (p->b != q->b ? p->b - q->b : p->e - q->e);
}
/* the world box of each edge set (ours, for sld_solid_extents' cull below): every edge point, a circle edge's whole circle */
static double (*es_box)[6]; static int es_box_n, es_box_gen = -1;
static void es_box_sync(void) {
    if (es_box_gen == ac_solids_gen && es_box_n == es_n()) return;
    free(es_box); es_box = NULL; es_box_n = 0; es_box_gen = -1;
    int n = es_n(); if (n <= 0) { es_box_gen = ac_solids_gen; return; }
    es_box = malloc(sizeof *es_box * (size_t)n); if (!es_box) return;   /* no boxes = no cull (identical results) */
    for (int b = 0; b < n; b++) {
        double *B = es_box[b]; B[0] = B[2] = B[4] = 1e300; B[1] = B[3] = B[5] = -1e300;
        int nE; const BEdge *EB = es(b, &nE);
        for (int e = 0; e < nE; e++) {
            const BEdge *E = &EB[e];
            for (int k = 0; k < E->np; k++) for (int c = 0; c < 3; c++) { double v = E->pts[3 * k + c]; if (v < B[2 * c]) B[2 * c] = v; if (v > B[2 * c + 1]) B[2 * c + 1] = v; }
            if (E->arc) for (int c = 0; c < 3; c++) { if (E->c[c] - E->r < B[2 * c]) B[2 * c] = E->c[c] - E->r; if (E->c[c] + E->r > B[2 * c + 1]) B[2 * c + 1] = E->c[c] + E->r; }
        }
    }
    es_box_n = n; es_box_gen = ac_solids_gen;
}
/* FUN_1800cd000 / sld_solid_extents (formats/ui.md "AutoCursor on solids"): a body is tried only when the cursor lies in its
   view-space box +-10 px (the 8 corners of its box through the view) - a pure cull */
static int es_box_near(int b, float sx0, float sy0) {
    if (!es_box || b >= es_box_n) return 1;
    const double *B = es_box[b]; if (B[0] > B[1]) return 0;            /* no edges */
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    for (int i = 0; i < 8; i++) { float x, y;
        if (!scene_to_screen(vp_w, vp_h, B[i & 1 ? 1 : 0], B[i & 2 ? 3 : 2], B[i & 4 ? 5 : 4], &x, &y)) return 1;
        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    const float m = (float)SLD_PICK_PX + 1.0f;                       /* + 1 px: float rounding of the corners (ours) */
    return sx0 >= x0 - m && sx0 <= x1 + m && sy0 >= y0 - m && sy0 <= y1 + m;
}
static int sld_edge_hits_all(float sx0, float sy0, SHit **out);
/* the hits of one edge set (body >= 0) or all (-1); the full list is computed once per cursor / view / edge-set state
   (geo_snap asks per body and again for the tpn pass) and filtered - the same lists and order as computing each */
static int sld_edge_hits(int body, float sx0, float sy0, SHit **out) {
    static SHit *ch; static int cn = -1, cgen = -1, cw, chh; static float cx, cy; static unsigned cser;
    unsigned ser = scene_view_serial(vp_w, vp_h);
    if (cn < 0 || cx != sx0 || cy != sy0 || cgen != ac_solids_gen || cser != ser || cw != vp_w || chh != vp_h) {
        free(ch); ch = NULL; cn = sld_edge_hits_all(sx0, sy0, &ch);
        cx = sx0; cy = sy0; cgen = ac_solids_gen; cser = ser; cw = vp_w; chh = vp_h;
    }
    int n = 0; SHit *h = malloc(sizeof(SHit) * (size_t)(cn > 0 ? cn : 1));
    if (!h) { *out = NULL; return 0; }
    for (int i = 0; i < cn; i++) if (body < 0 || ch[i].b == body) h[n++] = ch[i];
    *out = h; return n;
}
static int sld_edge_hits_all(float sx0, float sy0, SHit **out) {
    SHit *h = NULL; int nh = 0, cap = 0;
    es_box_sync();
    for (int b = 0; b < es_n(); b++) {
        if (!es_box_near(b, sx0, sy0)) continue;
        int nE; const BEdge *EB = es(b, &nE);
        for (int e = 0; e < nE; e++) {
            const BEdge *E = &EB[e]; double bd = 1e30; V3 bc = { 0, 0, 0 };
            for (int k = 0; k + 1 < E->np; k++) {
                const double *A = E->pts + 3 * k, *B = A + 3; float ax, ay, bx, by;
                if (!scene_to_screen(vp_w, vp_h, A[0], A[1], A[2], &ax, &ay) || !scene_to_screen(vp_w, vp_h, B[0], B[1], B[2], &bx, &by)) continue;
                double ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey, u = l2 > 0 ? ((sx0 - ax) * ex + (sy0 - ay) * ey) / l2 : 0;
                if (u < 0) u = 0; if (u > 1) u = 1;
                double dd = hypot(ax + u * ex - sx0, ay + u * ey - sy0);
                if (dd < bd) { bd = dd; bc = (V3){ A[0] + u * (B[0] - A[0]), A[1] + u * (B[1] - A[1]), A[2] + u * (B[2] - A[2]) }; }
            }
            if (E->arc) { double t, d; if (arc_nearest(E, sx0, sy0, &t, &d)) { bd = d; bc = arc_pt(E, t); } }   /* psHit +0x10 on the true curve */
            if (bd > SLD_PICK_PX) continue;
            if (nh == cap) { int nc = cap ? cap * 2 : 16; SHit *nh_ = realloc(h, sizeof(SHit) * nc); if (!nh_) break; h = nh_; cap = nc; }
            double cv[3], c3[3] = { bc.x, bc.y, bc.z }; world_to_view3(c3, cv);
            h[nh++] = (SHit){ b, e, cv[2], bd, bc };
        }
    }
    if (nh > 1) qsort(h, nh, sizeof(SHit), shit_cmp);
    *out = h; return nh;
}
/* solid chaining's edge pick (sld_select(cursor, mask, hits, 1) -> ps_select, decomp/mcgeomsld_snap.c:1654-1990, as the
   tpn pass above uses it): the edges within 10 px of the view point (sx0, sy0), each with its point nearest the cursor
   (psHit +0x10); hidden ones dropped (MayBeHidden + IsPosVisible on that point, solid_mode 1); front to back - back to
   front with the From back button (GetSolidSelectionBackButtonChecked -> ps_select flips the ray). body >= 0: that
   edge set only. Returns the count; b / e / depth / cp per hit (max entries). */
static int arc_visible(const BEdge *E, V3 q);
int sld_edge_pick(float sx0, float sy0, int body, int back, int *bo, int *eo, double *depth, double *cp, int max) {
    part_edges_sync();
    SHit *h; int nh = sld_edge_hits(body, sx0, sy0, &h), no = 0;
    for (int k = 0; k < nh; k++) {
        int i = back ? nh - 1 - k : k; int nE; const BEdge *E = &es(h[i].b, &nE)[h[i].e];
        if (!(E->arc ? arc_visible(E, h[i].cp) : pos_visible(h[i].cp.x, h[i].cp.y, h[i].cp.z))) continue;
        if (no < max) { bo[no] = h[i].b; eo[no] = h[i].e; depth[no] = h[i].depth; cp[3 * no] = h[i].cp.x; cp[3 * no + 1] = h[i].cp.y; cp[3 * no + 2] = h[i].cp.z; no++; }
    }
    free(h); return no;
}
/* the edge sets (Extrude bodies first, then the part's solids) for solid chaining: their count, the body of set b
   (NULL = a part solid: F.solids[b - nbodies]) and a counter that changes whenever the sets are rebuilt */
int ac_solid_sets(void) { part_edges_sync(); return es_n(); }
const Body *ac_solid_body(int b) { return b >= 0 && b < sn_nb ? &sn_b[b] : NULL; }
int ac_solid_nbodies(void) { return sn_nb; }
int ac_solids_gen;
int sld_pos_visible(double x, double y, double z) { return pos_visible(x, y, z); }

/* proj_2d_pt_to_curve for a line (mccore 0x1802dd9f0 -> dist_to_finite_line 0x1804b6680 + t_on_line 0x1804b48a0,
   decomp/mccore_proj2d.c, decomp/mccore_inters.c:2035): the distance to the FINITE segment in the view, the parameter of the carrier's foot
   (not clamped); in screen px here */
static int edge_line_proj(const BEdge *E, float sx0, float sy0, double *dpx, double *t) {
    const double *A = E->pts, *B = E->pts + 3 * (E->np - 1); float ax, ay, bx, by;
    if (!scene_to_screen(vp_w, vp_h, A[0], A[1], A[2], &ax, &ay) || !scene_to_screen(vp_w, vp_h, B[0], B[1], B[2], &bx, &by)) return 0;
    double ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey;
    *t = l2 > 1e-24 ? ((sx0 - ax) * ex + (sy0 - ay) * ey) / l2 : 0;
    double u = *t < 0 ? 0 : *t > 1 ? 1 : *t;
    *dpx = hypot(ax + u * ex - sx0, ay + u * ey - sy0);
    return 1;
}
static V3 edge_at(const BEdge *E, double t) {
    const double *A = E->pts, *B = E->pts + 3 * (E->np - 1);
    return (V3){ A[0] + t * (B[0] - A[0]), A[1] + t * (B[1] - A[1]), A[2] + t * (B[2] - A[2]) };
}
/* round edges (BEdge.arc, formats/extrude.md "Round edges"): the point of the circle at parameter t (PK_CURVE_eval:
   centre + r (cos t xa + sin t (ax x xa))) and its unit tangent */
static V3 arc_pt(const BEdge *E, double t) {
    double ya[3] = { E->ax[1] * E->xa[2] - E->ax[2] * E->xa[1], E->ax[2] * E->xa[0] - E->ax[0] * E->xa[2], E->ax[0] * E->xa[1] - E->ax[1] * E->xa[0] };
    double c = E->r * cos(t), sn = E->r * sin(t);
    return (V3){ E->c[0] + c * E->xa[0] + sn * ya[0], E->c[1] + c * E->xa[1] + sn * ya[1], E->c[2] + c * E->xa[2] + sn * ya[2] };
}
static int arc_in_xy(const BEdge *E) { return fabs(fabs(E->ax[2]) - 1) < 1e-9 && fabs(E->xa[2]) < 1e-9; }
/* an XY arc edge as a wireframe-style arc (geo2d GE): its start angle in the XY plane and signed sweep */
static GE arc_ge(const BEdge *E) {
    double s = E->ax[2] > 0 ? 1 : -1, th0 = atan2(E->xa[1], E->xa[0]) + s * E->t0;
    return ge_arc((P2){ E->c[0], E->c[1] }, E->r, th0, s * (E->t1 - E->t0), E->c[2]);
}
/* the parameter of the arc nearest the cursor in the view (proj_2d_pt_to_curve -> dist_to_arc on the view
   projection; dist_to_arc itself is not dumped - the point of the bounded arc nearest the cursor here): sampled at the
   facet resolution, then refined by golden-section search between the neighbouring samples (ours, numerical) */
static int arc_nearest(const BEdge *E, float sx0, float sy0, double *tout, double *dpx) {
    int n = E->np > 8 ? 4 * E->np : 32; double bt = E->t0, bd = 1e300, cvx, cvy;
    view_px_to_view(sx0, sy0, &cvx, &cvy);                    /* the view-plane distance in double (proj_iso_pt) */
    double ppu = (vp_h > 0 ? vp_h : 1) / (double)scene_view_height();
#define ADIST(T, D) do { V3 q_ = arc_pt(E, (T)); double w_[3] = { q_.x, q_.y, q_.z }, v_[3]; world_to_view3(w_, v_); (D) = hypot(v_[0] - cvx, v_[1] - cvy) * ppu; } while (0)
    for (int i = 0; i <= n; i++) { double t = E->t0 + (E->t1 - E->t0) * i / n, d; ADIST(t, d); if (d < bd) { bd = d; bt = t; } }
    if (bd >= 1e300) return 0;
    double h = (E->t1 - E->t0) / n, a = bt - h, b = bt + h, g = (sqrt(5.0) - 1) / 2;
    if (a < E->t0) a = E->t0; if (b > E->t1) b = E->t1;
    double x1 = b - g * (b - a), x2 = a + g * (b - a), f1, f2; ADIST(x1, f1); ADIST(x2, f2);
    for (int it = 0; it < 60; it++) {
        if (f1 < f2) { b = x2; x2 = x1; f2 = f1; x1 = b - g * (b - a); ADIST(x1, f1); }
        else { a = x1; x1 = x2; f1 = f2; x2 = a + g * (b - a); ADIST(x2, f2); }
    }
    double tm = (a + b) / 2, dm; ADIST(tm, dm);
    if (dm < bd) { bd = dm; bt = tm; }
#undef ADIST
    *tout = bt; *dpx = bd; return 1;
}
/* IsPosVisible for a point of a round edge: the faces here are the kernel's faceted slabs, so the point is tested
   where its facet polygon lies at the same angle (the facet chord crossed by the radius through it; ours - the true
   curve lies up to the faceting tolerance outside / inside those faces) */
static int pos_visible(double x, double y, double z);
static int arc_visible(const BEdge *E, V3 q) {
    if (E->np >= 2 && arc_in_xy(E)) {
        double dx = q.x - E->c[0], dy = q.y - E->c[1], l = hypot(dx, dy);
        if (l > 0) for (int k = 0; k + 1 < E->np; k++) {
            const double *A = E->pts + 3 * k, *B = A + 3;
            double ex = B[0] - A[0], ey = B[1] - A[1], den = dx * ey - dy * ex; if (fabs(den) < 1e-15) continue;
            double ax = A[0] - E->c[0], ay = A[1] - E->c[1];
            double s = (ax * ey - ay * ex) / den, u = (ax * dy - ay * dx) / den;     /* c + s (dx, dy) = A + u (ex, ey) */
            if (s > 0 && u >= -1e-9 && u <= 1 + 1e-9) return pos_visible(E->c[0] + s * dx, E->c[1] + s * dy, q.z);
        }
    }
    return pos_visible(q.x, q.y, q.z);
}

/* intersection candidates: the two closest curves (wireframe line / arc or a solid's straight edge) */
typedef struct { GE g; V3 a, b; int line, solid; double d; P2 cp; } ICand;
static void icand_put(ICand c[2], int *n, ICand x) {
    if (*n == 0 || x.d < c[0].d) { c[1] = c[0]; c[0] = x; if (*n < 2) (*n)++; }
    else if (*n == 1 || x.d < c[1].d) { c[1] = x; if (*n < 2) (*n)++; }
}

/* the cursor of a mouse event (window coordinates, ours): while armed, geo_snap searches around it instead of around the
   screen projection of the point it is given. The AutoCursor works from the cursor (FUN_1804e1bb0 / dl_search_fast take
   the cursor position; ps_select maps the cursor at view depth 0 and +-10000 m, mcgeomsld_snap.c map_cursor_to_3d); the
   world point handed in here is the cursor on the construction plane (scene_pick), whose projection is not the cursor
   when the view looks along that plane (Front / Side views: the ray never meets it) - the cause of the missed snaps */
static int ptr_armed; static float ptr_x, ptr_y;
void geo_snap_ptr(float mx, float my) { ptr_armed = 1; ptr_x = mx; ptr_y = my; }
void geo_snap_ptr_off(void) { ptr_armed = 0; }
float cui_ptr_x, cui_ptr_y; int cui_ptr_ok;      /* the last mouse position in the view (cui_cursor_world) */
/* a view drag (middle-button rotate / pan) is running: the AutoCursor is not re-run on its mouse moves (ours: Mastercam's
   behaviour during gr_dynamic is NOT DECODED for the AutoCursor - the auto-highlight is skipped then; the next plain move
   snaps again, so the results are the same) */
int cui_view_drag;
/* the construction-plane point of a click is the cursor (it projects to the armed mouse position): false in a view along
   the plane, where a click that snapped to nothing has no point (ours; the callers refuse it) */
int geo_cursor_on_plane(const V3 *p) {
    if (!ptr_armed) return 1;
    float x, y; return fabs(vmat[8]) > 1e-10 && scene_to_screen(vp_w, vp_h, p->x, p->y, p->z, &x, &y) && hypotf(x - (ptr_x - vp_x), y - (ptr_y - vp_y)) < 0.5f;   /* the ray meets the Cplane (Top) */
}
/* the point picks outside the wireframe commands that run get_event / point_proc with the AutoCursor (its marker drawn on
   every mouse move, draw_fast_marker via FUN_1804ea8a0): the Linking page "..." (tp_select_depth -> point_proc_assoc,
   decomp/mccore_tp_select_depth.c), Analyze's new position (5342), the Stock Setup origin / corner picks (FUN_1801f16f0 /
   FUN_1801f1840 point_proc), the Extrude panel's AutoCursor / direction picks (sld 905 / 485, mask 0x4003). Before, the
   marker was drawn only for the wireframe commands, so these picks showed no snap (ours: the same geo_snap as the click) */
int cui_point_pick_active(void) { return lk_pick || az_pick || st_point_pick() || (ex_mode && ex_phase && (ex_pick == 1 || ex_pick == 3 || ex_pick == 4)); }
static V3 pp_cur; static int pp_on, pp_snapped; static long pp_key_seen;
/* which pick is armed (a new / re-armed pick must not show the last pick's marker before the next mouse move) */
static long pp_key(void) { return cui_point_pick_active() ? 1 + lk_pick + 16L * az_pick + 256L * st_pick + 4096L * (ex_mode ? ex_pick : 0) : 0; }
static int pp_live(void) { if (pp_on && pp_key() != pp_key_seen) pp_on = 0; return pp_on && pp_key(); }
void pp_hover(const double world[3]) {
    if (cui_view_drag) return;
    if (!cui_point_pick_active()) { if (pp_on) { pp_on = 0; overlay_dirty = 1; } return; }
    V3 p = { world[0], world[1], world[2] };
    int four = ex_mode && ex_pick == 4, was_snapped = pp_live() && pp_snapped;
    geo_snap_ptr(cui_ptr_x, cui_ptr_y);
    pp_snapped = geo_snap(&p, four ? &ex_pick_p0 : NULL, four ? 0 : -1, 0);
    geo_snap_ptr_off();
    int changed = !pp_on || pp_snapped != was_snapped || (pp_snapped && (p.x != pp_cur.x || p.y != pp_cur.y || p.z != pp_cur.z));
    pp_cur = p; pp_on = 1; pp_key_seen = pp_key();
    if (changed) overlay_dirty = 1;                      /* only a changed marker redraws the overlay */
}
void pp_ov(float **pv, int *pn, int *pcap) { if (pp_live() && pp_snapped) ov_fast_marker(pv, pn, pcap, pp_cur); }
void pp_dump(void) {
    int live = pp_live();
    if (live && pp_snapped) printf("    pphover on 1 snapped 1 kind %d at %.4f %.4f %.4f\n", geo_snap_kind, pp_cur.x, pp_cur.y, pp_cur.z);
    else printf("    pphover on %d snapped 0\n", live);   /* the unsnapped cursor point depends on the window size */
    int k = F.nents - 1; while (k >= 0 && !F.ents[k].type) k--;
    if (geo_cmd && k >= 0) printf("    pphover last ent %d type %d a %.4f %.4f %.4f\n", k, F.ents[k].type, F.ents[k].a.x, F.ents[k].a.y, F.ents[k].a.z);
    if (geo_cmd && geo_cursor_ok && geo_snap_ent) printf("    pphover geocursor kind %d at %.4f %.4f %.4f\n", geo_snap_kind, geo_cursor.x, geo_cursor.y, geo_cursor.z);
    else if (geo_cmd) printf("    pphover geocursor none\n");
}
/* the point of a wireframe line / XY arc nearest the cursor in screen space (proj_2d_pt_to_curve: lines = the finite segment,
   dist_to_finite_line; arcs: dist_to_arc is not dumped - ours: the sweep sampled at 3 degree steps, then a golden-section
   refinement of the best interval), for the views along the construction plane; 0 if nothing projects */
static int scr_nearest(const Ent *en, float sx0, float sy0, V3 *q, double *dq) {
    float ax, ay, bx, by;
    if (en->type == E_LINE) {
        if (!scene_to_screen(vp_w, vp_h, en->a.x, en->a.y, en->a.z, &ax, &ay) || !scene_to_screen(vp_w, vp_h, en->b.x, en->b.y, en->b.z, &bx, &by)) return 0;
        double ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey, t = l2 > 0 ? ((sx0 - ax) * ex + (sy0 - ay) * ey) / l2 : 0;
        if (t < 0) t = 0; if (t > 1) t = 1;
        *q = (V3){ en->a.x + t * (en->b.x - en->a.x), en->a.y + t * (en->b.y - en->a.y), en->a.z + t * (en->b.z - en->a.z) };
        *dq = hypot(ax + t * ex - sx0, ay + t * ey - sy0); return 1;
    }
    if (en->type != E_ARC) return 0;
    int n = (int)(fabs(en->sw) / (M_PI / 60)) + 2; if (n > 240) n = 240;
    double bt = 0, bd = 1e300;
    for (int k = 0; k <= n; k++) { double t = (double)k / n; V3 w = mcam_arc_pt(en, en->a0 + en->sw * t); float x, y;
        if (scene_to_screen(vp_w, vp_h, w.x, w.y, w.z, &x, &y)) { double d = hypot(x - sx0, y - sy0); if (d < bd) { bd = d; bt = t; } } }
    if (bd > 1e299) return 0;
    double lo = bt - 1.0 / n, hi = bt + 1.0 / n; if (lo < 0) lo = 0; if (hi > 1) hi = 1;
    for (int it = 0; it < 60; it++) {
        double m1 = lo + (hi - lo) * 0.381966, m2 = hi - (hi - lo) * 0.381966, d1 = 1e300, d2 = 1e300; float x, y;
        V3 w1 = mcam_arc_pt(en, en->a0 + en->sw * m1), w2 = mcam_arc_pt(en, en->a0 + en->sw * m2);
        if (scene_to_screen(vp_w, vp_h, w1.x, w1.y, w1.z, &x, &y)) d1 = hypot(x - sx0, y - sy0);
        if (scene_to_screen(vp_w, vp_h, w2.x, w2.y, w2.z, &x, &y)) d2 = hypot(x - sx0, y - sy0);
        if (d1 < d2) hi = m2; else lo = m1;
    }
    double t = (lo + hi) * 0.5; float x, y; *q = mcam_arc_pt(en, en->a0 + en->sw * t);
    if (!scene_to_screen(vp_w, vp_h, q->x, q->y, q->z, &x, &y)) return 0;
    *dq = hypot(x - sx0, y - sy0); return 1;
}
int geo_snap(V3 *p, const V3 *anchor, int rb, int use_grid) {
    geo_snap_feat_ent = geo_snap_feat = -1;
    float px, py, best = 1e30f; int bk = GK_NONE; V3 bp = *p;
    float sx0, sy0;
    if (ptr_armed) { sx0 = ptr_x - vp_x; sy0 = ptr_y - vp_y; }
    else if (!scene_to_screen(vp_w, vp_h, p->x, p->y, p->z, &sx0, &sy0)) return 0;
    const float ap = (float)(SEL_APERTURE_PX * 0.5);
    double wpp = scene_view_height() / (vp_h > 0 ? vp_h : 1);   /* world units per pixel */
    double sel_w = SEL_APERTURE_PX * wpp;                       /* sel_dist in world (view) units */
    P2 cur = { p->x, p->y };
    /* the construction-plane point p is the cursor only when it projects to it (the cursor ray meets the plane: Top / Iso
       views). In a view along the plane (Front / Back / Side: viewZ . CplaneZ = 0) it lies at the edge of the view volume
       (scene_pick t = 0), so there the Nearest / intersection candidates are found in screen space from the cursor itself
       (proj_2d_pt_to_curve: proj_iso_pt + dist_to_finite_line / dist_to_arc, decomp/mccore_proj2d.c), the tangent /
       perpendicular passes are off by their own gate (dot(CplaneZ, viewZ) > 1e-10) and the grid is not searched (ours: the
       cursor has no point on the Cplane there, sel_grid_search NOT DECODED for that case) */
    int plane_cur = 1;
    { float qx_, qy_; plane_cur = fabs(vmat[8]) > 1e-10 && scene_to_screen(vp_w, vp_h, p->x, p->y, p->z, &qx_, &qy_) && hypotf(qx_ - sx0, qy_ - sy0) < 0.5f; }
    P2 anch = { 0, 0 }; int anch_ent = -1;
    int have_anchor = anchor != NULL && RB_TPN(rb);
    if (have_anchor) {
        anch = xy(*anchor);
        float ax, ay;                       /* find_an_entity(anchor, mask 0x8E) - via pick_entity here (ours) */
        if (scene_to_screen(vp_w, vp_h, anchor->x, anchor->y, anchor->z, &ax, &ay)) anch_ent = pick_entity(ax + vp_x, ay + vp_y);
    }
    double ad = have_anchor ? hypot(cur.x - anch.x, cur.y - anch.y) : 0;
    int got_tan = 0, got_perp = 0, got_near = 0;   /* a hit in that tpn pass (dl_search_fast_tpn is called once per pass,
                                                      mccore_autocursor2.c:476 / 537 / 643; its solid fallback runs only
                                                      without a wireframe hit, dlsearch_tpn:296-301) */
    /* TRY_SNAP: a later pass's candidate (vis: IsPosVisible on the snap point, dlsearch_tpn:249 / 359) */
#define TRY_SNAP(wx, wy, wz, knd, chk_ap, vis, got) do { \
        float qx_, qy_; double wx_ = (wx), wy_ = (wy), wz_ = (wz); \
        if (scene_to_screen(vp_w, vp_h, wx_, wy_, wz_, &qx_, &qy_)) { \
            float dd_ = hypotf(qx_ - sx0, qy_ - sy0); \
            if ((!(chk_ap) || dd_ < ap) && (!(vis) || pos_visible(wx_, wy_, wz_))) { \
                got = 1; \
                if ((knd) < bk || ((knd) == bk && dd_ < best)) { best = dd_; bk = (knd); bp = (V3){ wx_, wy_, wz_ }; } } \
        } } while (0)
    int dummy_got = 0;
    /* the feature pass (dl_search_fast): every candidate within sel_dist * 0.5, visible, strictly nearer than the best
       so far wins - one running distance for every feature kind (local_d078, mastercam_dlsearch.c:718-790, 866-935);
       it ranks after the tangent pass and before every later pass */
#define FEAT_WIN(d) ((d) < ap && bk >= GK_POINT && (bk > GK_CENTER || (d) < best))
    ICand ic[2]; int nic = 0;
    int fe_i = -1, fe_k = -1; V3 fe_p = { 0, 0, 0 };
    for (int i = 0; i < F.nents; i++) {
        const Ent *en = &F.ents[i]; if (!en->type || en->blanked || !ENT_LV_SHOWN(en)) continue;   /* blanked / a hidden level: not in the display list dl_search reads */
        if (en->type == E_NOTE) continue;                   /* notes: their AutoCursor points are NOT PORTED (dl_search_fast's drafting cases) */
        if (en->type == E_MESH) continue;                   /* meshes: their AutoCursor points are NOT PORTED */
        if (en->type == E_SURF) continue;                   /* surfaces: no AutoCursor candidates - dl_search_fast has no class 0x100 case (decomp/mastercam_dlsearch.c) */
        if (en->type == E_SPLINE) continue;                 /* spline curves: their AutoCursor points are NOT PORTED (no candidates, as
                                                               before they were entities) */
        V3 c[8]; int kind[8], nc = 0;
        if (en->type == E_POINT) { if (ac_snap[ACS_POINT]) { c[nc] = en->a; kind[nc++] = GK_POINT; } }
        else if (en->type == E_LINE) {
            if (ac_snap[ACS_END]) { c[nc] = en->a; kind[nc++] = GK_END; c[nc] = en->b; kind[nc++] = GK_END; }
            if (ac_snap[ACS_MID]) { c[nc] = (V3){ (en->a.x + en->b.x) / 2, (en->a.y + en->b.y) / 2, (en->a.z + en->b.z) / 2 }; kind[nc++] = GK_MID; }
        }
        else {                                              /* arc */
            if (ac_snap[ACS_END]) { c[nc] = en->a; kind[nc++] = GK_END; c[nc] = en->b; kind[nc++] = GK_END; }
            /* eval_curve at the mid parameter, quadrants at the arc's own (view) angles; the centre is world (view_to_world,
               mastercam_dlsearch.c l.775 / l.980) - mcam_arc_pt evaluates a view arc in its view (Ent.vw) */
            if (ac_snap[ACS_MID]) { double a = en->a0 + en->sw / 2; c[nc] = mcam_arc_pt(en, a); kind[nc++] = GK_MID; }
            if (ac_snap[ACS_QUAD]) for (int q = 0; q < 4 && nc < 7; q++) {   /* (a clockwise arc - sweep < 0 - from its end, as ge_arc normalises it) */
                double a0 = en->sw < 0 ? en->a0 + en->sw : en->a0, sw = fabs(en->sw);
                double a = q * M_PI / 2, d = fmod(a - a0, 2 * M_PI); if (d < 0) d += 2 * M_PI;
                if (d <= sw + 1e-12) { c[nc] = mcam_arc_pt(en, a); kind[nc++] = GK_QUAD; }
            }
            if (ac_snap[ACS_CENTER] && nc < 8) { c[nc] = en->c; kind[nc++] = GK_CENTER; }
        }
        for (int k = 0; k < nc; k++) {
            if (!scene_to_screen(vp_w, vp_h, c[k].x, c[k].y, c[k].z, &px, &py)) continue;
            float d = hypotf(px - sx0, py - sy0);
            if (FEAT_WIN(d) && pos_visible(c[k].x, c[k].y, c[k].z)) { best = d; bk = kind[k]; bp = c[k]; fe_i = i; fe_k = k; fe_p = c[k]; }   /* IsPosVisible(1) */
        }
        /* the tangent / perpendicular / nearest / intersection passes work on lines and arcs in an XY-parallel
           plane (the original's DL mask 0xC8E also covers splines / conics / copious data; those entity types
           don't exist here - arcs in other planes are NOT PORTED for these passes) */
        if (en->type != E_LINE && en->type != E_ARC) continue;
        if (en->type == E_ARC && !en->planar) continue;
        {
            GE ge = ent_ge(i);
            P2 q; double tq, qz; float qx, qy; double dq = 1e30;
            if (plane_cur) {
                q = ge_nearest(&ge, cur, 1);
                tq = ge_param(&ge, q);
                qz = en->type == E_LINE ? en->a.z + tq * (en->b.z - en->a.z) : en->c.z;
                if (scene_to_screen(vp_w, vp_h, q.x, q.y, qz, &qx, &qy)) dq = hypotf(qx - sx0, qy - sy0);
            } else {
                V3 q3; if (!scr_nearest(en, sx0, sy0, &q3, &dq)) continue;
                q = (P2){ q3.x, q3.y }; qz = q3.z; tq = ge_param(&ge, q);
            }
            /* an intersection candidate: within sel_dist * 0.5 and visible (mastercam_dlsearch.c:1033-1051) */
            if (dq < ap && pos_visible(q.x, q.y, qz)) icand_put(ic, &nic, (ICand){ ge, en->a, en->b, en->type == E_LINE, 0, dq, q });
            int elig = dq < ap || i == anch_ent;                        /* dlsearch_tpn:205 */
            if (getenv("MCAMUI_SNAP_DEBUG")) fprintf(stderr, "  ent %d type %d dq %.2f ap %.2f elig %d anchor %d ad %.2f\n", i, en->type, dq, ap, elig, have_anchor, ad);
            /* dot(CplaneZ, viewZ) > 1e-10 (signed) gates both anchor passes (FUN_180131c50:627 / FUN_180131620:782) */
            if (elig && have_anchor && ad > 1e-9 && vmat[8] > 1e-10) {
                /* the eval point: the anchor's own entity is evaluated AT THE ANCHOR (dlsearch_tpn:214-218),
                   any other entity at the cursor's closest point */
                P2 td = ge_dir(&ge, i == anch_ent ? ge_param(&ge, anch) : tq);
                double a_drag = atan2(anch.y - cur.y, anch.x - cur.x);   /* anchor - cursor, as the original (the fold makes it unoriented) */
                if (ac_snap[ACS_TAN] && en->type == E_ARC && i != anch_ent) {
                    /* FUN_180131c50: the drag direction within (sel_dist / radius) radians of the tangent at the
                       closest point, then the tangent point from the anchor (tang_ent_thru_pt is not dumped; the
                       tangent point nearer the closest point here - ours) */
                    double dang = fabs(angfold_pi(a_drag) - angfold_pi(atan2(td.y, td.x)));
                    P2 tp[2];
                    if (dang < sel_w / en->r && ge_tangent_points(ge.c, ge.r, anch, tp) == 2) {
                        P2 tpick = hypot(tp[1].x - q.x, tp[1].y - q.y) < hypot(tp[0].x - q.x, tp[0].y - q.y) ? tp[1] : tp[0];
                        TRY_SNAP(tpick.x, tpick.y, en->c.z, GK_TAN, 0, 1, got_tan);
                    }
                }
                if (ac_snap[ACS_PERP]) {
                    /* FUN_180131620: the drag direction within sel_dist / |anchor->cursor| radians of the in-plane
                       normal (CplaneZ x curve_tangent, :812-816), then:
                       param_6 == 0 (any other entity): the foot of the perpendicular from the anchor
                       (createLinePerpToAnEntity + proj_pt_on_ent, :878-889);
                       param_6 == 1 (the anchor's own entity - disasm 0x180135264: arg6 = the anchor-entity
                       compare): anchor + normal * the drag DISTANCE, on the side nearer the cursor (:845-876) -
                       the drag snaps to exactly perpendicular instead of collapsing onto the anchor; the side
                       test is the plain difference of the DIRECTED normal and anchor->cursor angles (no
                       wraparound handling - the original's quirk) */
                    double na = atan2(td.x, -td.y);              /* the normal (-td.y, td.x), CplaneZ x tangent */
                    if (fabs(angfold_pi(a_drag) - angfold_pi(na)) < sel_w / ad) {
                        if (i == anch_ent) {
                            double sa = na < 0 ? na + 2 * M_PI : na, da = a_drag < 0 ? a_drag + 2 * M_PI : a_drag;
                            double tol = sel_w / ad, sd = fabs(sa - da), dist = sd <= tol && sd != tol ? -ad : ad;
                            TRY_SNAP(anch.x - td.y * dist, anch.y + td.x * dist, anchor->z, GK_PERP, 0, 1, got_perp);
                        } else {
                            P2 f = ge_nearest(&ge, anch, 1);
                            double tf = ge_param(&ge, f);
                            TRY_SNAP(f.x, f.y, en->type == E_LINE ? en->a.z + tf * (en->b.z - en->a.z) : en->c.z, GK_PERP, 0, 1, got_perp);
                        }
                    }
                }
            }
            if (ac_snap[ACS_NEAR] && dq < ap) TRY_SNAP(q.x, q.y, qz, GK_NEAR, 1, 1, got_near);   /* dlsearch_tpn:233 */
        }
    }
    /* solids in the feature pass: sld_search_fast (mcgeomsld 0x1800c7bf0, decomp/mcgeomsld_snap.c:1163-1650) per body, after the wireframe here (the
       display list order of the original). The edges within 10 px of the cursor ray (ps_select), front to back; per
       edge the nearest of its start / end vertex (Endpoint, mode 3: PK_EDGE_ask_geometry ends, the end only when
       strictly nearer) and its parametric midpoint (Midpoint, mode 5: PK_CURVE_eval at (t0 + t1) / 2), each one
       visible (IsPosVisible(2)); the edge's nearest one is taken when within sel_dist * 0.5 and strictly nearer than
       the pass's best so far, and ends the body's search; else the next edge. Circle edges (class 0xbba; here the
       round edges BEdge.arc recovered from the chain's arcs, formats/extrude.md "Round edges"): Midpoint = the
       circle at (t0 + t1) / 2, Quadrants (mode 9) at t = 0, pi/2, pi, 3pi/2 inside [min, max] of the interval
       (:1495-1550), and when none of the edge's candidates is taken the Centre fallback (mode 2: the edge's point
       nearest the ray within sel_dist * 0.5, nearer than the best, visible -> PK_CIRCLE_ask's centre, :1557-1600).
       A ring edge (full circle, no vertex) offers no Endpoint: what PK_EDGE_ask_geometry returns as the ends of a
       ring edge is NOT DECODED. A faceted run of unknown geometry (BEdge.curved) offers only its end vertices. The
       two front-most edges are the body's intersection candidates when it gives no feature hit (param_11,
       mastercam_dlsearch.c:230-260). */
    int sld_feat = 0;
    part_edges_sync();
    for (int b = 0; b < es_n() && bk != GK_TAN; b++) {
        SHit *h; int nh = sld_edge_hits(b, sx0, sy0, &h); int hit = 0;
        for (int k = 0; k < nh && !hit; k++) {
            const BEdge *E = &es(b, NULL)[h[k].e]; double ed = 1e38; V3 ep = { 0, 0, 0 }; int ek = GK_NONE;
            V3 cand[7]; int ck[7], cv[7], ncand = 0;      /* cv: a point of the true circle (visibility at its facet) */
            if (ac_snap[ACS_END] && E->arc != 2) { cand[ncand] = (V3){ E->pts[0], E->pts[1], E->pts[2] }; cv[ncand] = 0; ck[ncand++] = GK_END;
                const double *B = E->pts + 3 * (E->np - 1); cand[ncand] = (V3){ B[0], B[1], B[2] }; cv[ncand] = 0; ck[ncand++] = GK_END; }
            if (ac_snap[ACS_MID] && E->arc) { cand[ncand] = arc_pt(E, (E->t0 + E->t1) * 0.5); cv[ncand] = 1; ck[ncand++] = GK_MID; }
            else if (ac_snap[ACS_MID] && !E->curved) { cand[ncand] = edge_at(E, 0.5); cv[ncand] = 0; ck[ncand++] = GK_MID; }
            if (ac_snap[ACS_QUAD] && E->arc) {            /* class 0xbba: t = 0, pi/2, pi, 3pi/2 inside the interval */
                double lo = E->t0 < E->t1 ? E->t0 : E->t1, hi = E->t0 < E->t1 ? E->t1 : E->t0;
                for (int q = 0; q < 4; q++) { double t = q * 1.5707963267948966; if (lo <= t && t <= hi) { cand[ncand] = arc_pt(E, t); cv[ncand] = 1; ck[ncand++] = GK_QUAD; } }
            }
            for (int c = 0; c < ncand; c++) {
                float qx, qy; if (!scene_to_screen(vp_w, vp_h, cand[c].x, cand[c].y, cand[c].z, &qx, &qy)) continue;
                double d = hypotf(qx - sx0, qy - sy0);
                if (d < ed && (cv[c] ? arc_visible(E, cand[c]) : pos_visible(cand[c].x, cand[c].y, cand[c].z))) { ed = d; ep = cand[c]; ek = ck[c]; }
            }
            if (getenv("MCAMUI_SNAP_DEBUG")) fprintf(stderr, "  sld b %d e %d arc %d np %d ncand %d ek %d ed %.3f dpx %.3f t %.3f..%.3f bk %d best %.3f\n", b, h[k].e, E->arc, E->np, ncand, ek, ed, h[k].dpx, E->t0, E->t1, bk, best);
            if (ek != GK_NONE && FEAT_WIN(ed)) { best = (float)ed; bk = ek; bp = ep; hit = 1; sld_feat = 1; }
            else if (E->arc && ac_snap[ACS_CENTER] && FEAT_WIN(h[k].dpx) && arc_visible(E, h[k].cp)) {
                /* a circle edge without a feature hit: the centre when the edge's point nearest the ray is within
                   sel_dist * 0.5, nearer than the best and visible (PK_CIRCLE_ask, snap kind 2, mcgeomsld_snap.c:1557-1600) */
                best = (float)h[k].dpx; bk = GK_CENTER; bp = (V3){ E->c[0], E->c[1], E->c[2] }; hit = 1; sld_feat = 1;
            }
        }
        if (!hit && ac_snap[ACS_INT])           /* the body's two front-most edges (mastercam_dlsearch.c:230-260) */
            for (int k = 0; k < nh && k < 2; k++) {
                const BEdge *E = &es(b, NULL)[h[k].e]; double dq, t;
                const double *A = E->pts, *B = E->pts + 3 * (E->np - 1);
                if (E->arc) {                         /* a circle edge in an XY plane: its true arc */
                    if (!arc_in_xy(E) || !arc_nearest(E, sx0, sy0, &t, &dq)) continue;
                    V3 q = arc_pt(E, t);
                    if (dq < ap && arc_visible(E, q)) icand_put(ic, &nic, (ICand){ arc_ge(E), q, q, 0, 1, dq, (P2){ q.x, q.y } });
                    continue;
                }
                /* straight edges in an XY plane only: vertical edges and faceted runs are NOT PORTED here */
                if (E->curved || fabs(A[2] - B[2]) > 1e-9 || hypot(B[0] - A[0], B[1] - A[1]) < 1e-12) continue;
                if (!edge_line_proj(E, sx0, sy0, &dq, &t)) continue;
                double u = t < 0 ? 0 : t > 1 ? 1 : t; V3 q = edge_at(E, u);     /* pt_on_ent_from_t (mastercam_dlsearch.c:243-245) */
                if (dq < ap && pos_visible(q.x, q.y, q.z)) {
                    P2 pa = { A[0], A[1] }, pb = { B[0], B[1] };
                    icand_put(ic, &nic, (ICand){ ge_line(pa, pb, A[2]), (V3){ A[0], A[1], A[2] }, (V3){ B[0], B[1], B[2] }, 1, 1, dq, (P2){ q.x, q.y } });
                }
            }
        free(h);
    }
    (void)sld_feat;
    /* solids in the tangent / perpendicular / nearest passes: without a wireframe hit in the pass, sld_select (mcgeomsld
       0x1800c93a0, decomp/mcgeomsld_snap.c:1654-1990: every visible body, ps_select edges, the hidden ones dropped - IsPosVisible(2) at the edge's point
       nearest the cursor - sorted front to back) and only its FIRST edge (dlsearch_tpn:301-367): its closest point
       (proj_2d_pt_to_curve) within sel_dist * 0.5, then FUN_180131c50 / FUN_180131620 / nearest as on wireframe, the
       snap point visible (IsPosVisible(2)). Tangent: a straight edge with a line-like rubber band is refused
       (FUN_180131c50:604-609); a round edge gets the arc tangent as on wireframe arcs (no own-entity exclusion on this
       path). Perpendicular: the edge is not the anchor's entity, but the solids path passes param_6 = 1 (disasm
       0x180135264), so the snap is anchor + normal x the drag distance (FUN_180131620:845-876); a vertical edge has
       no in-plane normal (CplaneZ x tangent = 0, what FUN_180130980 then does is NOT DECODED) - left out. A faceted
       run of unknown geometry (curved) is NOT PORTED. */
    if (es_n() && ((!got_tan && ac_snap[ACS_TAN] && have_anchor) || (!got_perp && ac_snap[ACS_PERP] && have_anchor) || (!got_near && ac_snap[ACS_NEAR]))) {
        SHit *h; int nh = sld_edge_hits(-1, sx0, sy0, &h), first = -1;
        for (int k = 0; k < nh && first < 0; k++) {
            const BEdge *E = &es(h[k].b, NULL)[h[k].e]; double dq, t;
            if (E->arc) { if (arc_visible(E, h[k].cp)) first = k; continue; }
            if (E->curved) { if (pos_visible(h[k].cp.x, h[k].cp.y, h[k].cp.z)) first = k; continue; }
            if (!edge_line_proj(E, sx0, sy0, &dq, &t)) continue;
            V3 q = edge_at(E, t);                                  /* pt_on_ent_from_t at the carrier parameter */
            if (pos_visible(q.x, q.y, q.z)) first = k;
        }
        const BEdge *E = first >= 0 ? &es(h[first].b, NULL)[h[first].e] : NULL;
        double dq = 1e30, t = 0; V3 q = { 0, 0, 0 }; int ok = 0, xy_arc = 0;
        if (E && E->arc) { if (arc_nearest(E, sx0, sy0, &t, &dq)) { q = arc_pt(E, t); ok = 1; xy_arc = arc_in_xy(E); } }
        else if (E && !E->curved && edge_line_proj(E, sx0, sy0, &dq, &t)) { q = edge_at(E, t); ok = 1; }   /* eval_curve at the (unclamped) parameter */
        if (ok && dq < ap) {
            /* the edge's tangent at q in the XY plane (straight edge: its direction; arc: CplaneZ-side tangent of the circle) */
            P2 td = { 0, 0 }; int have_td = 0;
            if (E->arc && xy_arc) { GE g = arc_ge(E); td = ge_dir(&g, ge_param(&g, (P2){ q.x, q.y })); have_td = 1; }
            else if (!E->arc) {
                const double *A = E->pts, *B = E->pts + 3 * (E->np - 1);
                double tx = B[0] - A[0], ty = B[1] - A[1], tl = hypot(tx, ty);
                if (tl > 1e-12) { td = (P2){ tx / tl, ty / tl }; have_td = 1; }
            }
            double a_drag = have_anchor ? atan2(anch.y - cur.y, anch.x - cur.x) : 0;
            /* FUN_180131c50 (tangent): a straight edge with the line-like rubber band is refused (:604-609); a circle
               edge: the drag direction within sel_dist / radius of the tangent at q, the tangent point from the
               anchor (as on wireframe arcs above) */
            if (!got_tan && ac_snap[ACS_TAN] && have_anchor && ad > 1e-9 && vmat[8] > 1e-10 && E->arc && xy_arc) {
                double dang = fabs(angfold_pi(a_drag) - angfold_pi(atan2(td.y, td.x)));
                P2 tp[2];
                if (dang < sel_w / E->r && ge_tangent_points((P2){ E->c[0], E->c[1] }, E->r, anch, tp) == 2) {
                    P2 tpick = hypot(tp[1].x - q.x, tp[1].y - q.y) < hypot(tp[0].x - q.x, tp[0].y - q.y) ? tp[1] : tp[0];
                    if (arc_visible(E, (V3){ tpick.x, tpick.y, E->c[2] })) TRY_SNAP(tpick.x, tpick.y, E->c[2], GK_TAN, 0, 0, got_tan);
                }
            }
            if (!got_perp && ac_snap[ACS_PERP] && have_anchor && ad > 1e-9 && vmat[8] > 1e-10 && have_td) {
                double na = atan2(td.x, -td.y);
                if (fabs(angfold_pi(a_drag) - angfold_pi(na)) < sel_w / ad) {
                    double sa = na < 0 ? na + 2 * M_PI : na, da = a_drag < 0 ? a_drag + 2 * M_PI : a_drag;
                    double tol = sel_w / ad, sd = fabs(sa - da), dist = sd <= tol && sd != tol ? -ad : ad;
                    TRY_SNAP(anch.x - td.y * dist, anch.y + td.x * dist, anchor->z, GK_PERP, 0, 1, got_perp);
                }
            }
            if (!got_near && ac_snap[ACS_NEAR] && (!E->arc || arc_visible(E, q))) TRY_SNAP(q.x, q.y, q.z, GK_NEAR, 0, !E->arc, got_near);
        }
        free(h);
    }
    (void)got_tan;
    /* intersections of the two closest curves, only when the feature pass found nothing (mastercam_dlsearch.c:963,
       1256-1258, intersect_ents); a pair with a solid edge must lie in one Z plane here (intersect_ents' handling of
       curves in different planes is NOT DECODED) */
    if (ac_snap[ACS_INT] && bk > GK_CENTER && nic == 2 && (!(ic[0].solid || ic[1].solid) || fabs(ic[0].g.z - ic[1].g.z) < 1e-9)) {
        P2 xi[2];
        int nx = ge_intersect(&ic[0].g, &ic[1].g, xi);
        if (nx) {
            P2 ref = { (ic[0].cp.x + ic[1].cp.x) / 2, (ic[0].cp.y + ic[1].cp.y) / 2 };
            int bi2 = nx == 2 && hypot(xi[1].x - ref.x, xi[1].y - ref.y) < hypot(xi[0].x - ref.x, xi[0].y - ref.y);
            double t = ge_param(&ic[0].g, xi[bi2]);
            double zz = ic[0].line ? ic[0].a.z + t * (ic[0].b.z - ic[0].a.z) : ic[0].g.z;
            TRY_SNAP(xi[bi2].x, xi[bi2].y, zz, GK_INT, 1, 0, dummy_got);   /* the aperture re-test is ours (NOT DECODED in the original) */
        }
    }
    if (ac_snap[ACS_ORIGIN]) {                              /* the C-plane origin pass uses the full sel_dist */
        float ox, oy;
        if (scene_to_screen(vp_w, vp_h, 0, 0, 0, &ox, &oy)) {
            float d = hypotf(ox - sx0, oy - sy0);
            if (d < (float)SEL_APERTURE_PX && (GK_ORIGIN < bk || (GK_ORIGIN == bk && d < best))) { best = d; bk = GK_ORIGIN; bp = (V3){ 0, 0, 0 }; }
        }
    }
    {   /* Temporary Center Point: the bounding-box centre (EntityMinMax per entity; arc extrema via the in-sweep
           quadrant points here - ours) */
        double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300, z0 = 1e300, z1 = -1e300; int nb = 0;
        for (int i = 0; i < F.nents; i++) {
            const Ent *en = &F.ents[i]; if (!en->type || en->type == E_SPLINE || en->type == E_NOTE || en->type == E_MESH || (en->type == E_SURF && en->blanked)) continue;   /* splines / notes: EntityMinMax NOT PORTED; a blanked surface (a base) is not displayed */
            V3 ex[8]; int nex = 0;
            if (en->type == E_SURF) {                       /* ExamineEntity_XYZ -> FUN_180151060: the 8 corners of the stored box (ent +0x90 / +0xa8) */
                const Surf *sf = &F.surfs[en->srf]; if (sf->off || sf->n < 0xc0) continue;
                double b[6]; memcpy(b, sf->rec + 0x90, 48);
                for (int q = 0; q < 8; q++) ex[nex++] = (V3){ b[(q & 1) ? 3 : 0], b[(q & 2) ? 4 : 1], b[(q & 4) ? 5 : 2] };
            } else { ex[nex++] = en->a; if (en->type != E_POINT) ex[nex++] = en->b; }
            if (en->type == E_ARC) for (int q = 0; q < 4 && nex < 6; q++) {   /* (a clockwise arc from its end, as above) */
                double a0 = en->sw < 0 ? en->a0 + en->sw : en->a0, sw = fabs(en->sw);
                double a = q * M_PI / 2, d = fmod(a - a0, 2 * M_PI); if (d < 0) d += 2 * M_PI;
                if (d <= sw + 1e-12) ex[nex++] = mcam_arc_pt(en, a);
            }
            for (int k = 0; k < nex; k++) {
                if (ex[k].x < x0) x0 = ex[k].x; if (ex[k].x > x1) x1 = ex[k].x;
                if (ex[k].y < y0) y0 = ex[k].y; if (ex[k].y > y1) y1 = ex[k].y;
                if (ex[k].z < z0) z0 = ex[k].z; if (ex[k].z > z1) z1 = ex[k].z;
            }
            nb++;
        }
        if (nb) TRY_SNAP((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2, GK_TCTR, 1, 0, dummy_got);
    }
    if (ac_snap[ACS_TMID]) for (int i = 0; i < 2; i++) {
        if (!ac_tmid_ok[i]) continue;
        float mx, my;
        if (scene_to_screen(vp_w, vp_h, ac_tmid[i].x, ac_tmid[i].y, ac_tmid[i].z, &mx, &my) && hypotf(mx - sx0, my - sy0) < ap) {
            TRY_SNAP(ac_tmid[i].x, ac_tmid[i].y, ac_tmid[i].z, GK_TMID, 1, 0, dummy_got); break;
        }
    }
    if (use_grid && grid.snap && plane_cur) {               /* sel_grid_search: Always, or Near = within half the pick distance */
        double gx = p->x, gy = p->y; grid_round(&gx, &gy);  /* (decomp/mastercam_grid.c: dist < sel_dist * 0.5, or mode 2) */
        TRY_SNAP(gx, gy, p->z, GK_GRID, grid.snap_mode == 1, 0, dummy_got);
    }
    if (ac_snap[ACS_HV] && anchor && RB_HV(rb) && fabs(vmat[8]) > 1e-10) {
        /* FUN_1804e1bb0:558-606: the cursor in view coordinates at the anchor's depth; H = (Cx, Ay) preferred when
           |Cy - Ay| <= sel_dist, else V = (Ax, Cy) when |Cx - Ax| <= sel_dist */
        double A[3], cvx, cvy, av[3] = { anchor->x, anchor->y, anchor->z };
        world_to_view3(av, A);
        view_px_to_view(sx0, sy0, &cvx, &cvy);
        double ch[3] = { 0, 0, A[2] }; int ok = 0;
        if (fabs(cvy - A[1]) <= sel_w) { ch[0] = cvx; ch[1] = A[1]; ok = 1; }
        else if (fabs(cvx - A[0]) <= sel_w) { ch[0] = A[0]; ch[1] = cvy; ok = 1; }
        if (ok) { double w_[3]; view_to_world3(ch[0], ch[1], ch[2], w_); TRY_SNAP(w_[0], w_[1], w_[2], GK_HV, 0, 0, dummy_got); }
    }
#undef TRY_SNAP
#undef FEAT_WIN
    (void)dummy_got;
    geo_snap_feat_ent = geo_snap_feat = -1;
    if (bk == GK_NONE) { geo_snap_kind = GK_NONE; return 0; }
    if (fe_i >= 0 && (bk == GK_POINT || bk == GK_END) && bp.x == fe_p.x && bp.y == fe_p.y && bp.z == fe_p.z) { geo_snap_feat_ent = fe_i; geo_snap_feat = fe_k; }
    *p = bp; geo_snap_kind = bk; return 1;
}

/* Angular snap is applied by the rubber-band consumers, not the snap search (decomp/mccad_lines_helpers.c:1240-1300,
   FUN_18018f1b0; only sketcher_line_1 calls it): with an anchor and no snap hit, the anchor -> cursor direction is
   rounded to the step (dialog edit 289; round half away from zero), the length kept. Ctrl suppresses it
   (GetAsyncKeyState(VK_CONTROL) >= 0); only when planar in the C-plane (|dz| < tol_exref - its value is NOT DECODED,
   1e-6 here). */
int geo_ang_snap(V3 *p, const V3 *anchor, int rb, int ctrl_down) {
    if (!anchor || rb != 0 || !ac_snap[ACS_ANG]) return 0;
    if (ctrl_down) return 0;                                /* the caller reads the Ctrl key */
    if (fabs(p->z - anchor->z) > 1e-6) return 0;
    double dx = p->x - anchor->x, dy = p->y - anchor->y, th = atan2(dy, dx);
    if (th < 0) th += 2 * M_PI;
    double st = fabs(ac_ang_step); if (st < 1e-6) st = 1e-6; if (st > 90) st = 90;    /* the dialog's clamp (mastercam_acok.c:30) */
    st *= M_PI / 180;
    double L = hypot(dx, dy);
    th = floor(th / st + 0.5) * st;
    p->x = anchor->x + L * cos(th); p->y = anchor->y + L * sin(th);
    return 1;
}

/* Temporary Midpoints capture (AutoCursorTemporaryMidpointOnKey -> FUN_18001baa0, decomp/mccore_tempmid.c:705-765):
   the midpoint of the entity under the cursor toggles in the two slots - capturing a stored point removes it (the
   other moves up), a new point takes a free slot, else overwrites the first. The original's tracking of the entity
   under the cursor is NOT DECODED (ours: pick_entity at the drawing cursor) as is its point compare (1e-9 here). */
void ac_tmid_capture(const V3 *cursor) {          /* cursor = the drawing cursor (mcamui geo_cursor), NULL = none */
    if (!cursor) return;
    float sx, sy; if (!scene_to_screen(vp_w, vp_h, cursor->x, cursor->y, cursor->z, &sx, &sy)) return;
    int e = pick_entity(sx + vp_x, sy + vp_y); if (e < 0) return;
    const Ent *en = &F.ents[e]; V3 m;
    if (en->type == E_LINE) m = (V3){ (en->a.x + en->b.x) / 2, (en->a.y + en->b.y) / 2, (en->a.z + en->b.z) / 2 };
    else if (en->type == E_ARC) { double a = en->a0 + en->sw / 2; m = mcam_arc_pt(en, a); }
    else return;
#define TM_EQ(a, b) (hypot((a).x - (b).x, (a).y - (b).y) < 1e-9 && fabs((a).z - (b).z) < 1e-9)
    if (ac_tmid_ok[0] && TM_EQ(ac_tmid[0], m)) { ac_tmid[0] = ac_tmid[1]; ac_tmid_ok[0] = ac_tmid_ok[1]; ac_tmid_ok[1] = 0; }
    else if (ac_tmid_ok[1] && TM_EQ(ac_tmid[1], m)) ac_tmid_ok[1] = 0;
    else if (!ac_tmid_ok[0]) { ac_tmid[0] = m; ac_tmid_ok[0] = 1; }
    else if (!ac_tmid_ok[1]) { ac_tmid[1] = m; ac_tmid_ok[1] = 1; }
    else ac_tmid[0] = m;
#undef TM_EQ
    overlay_dirty = 1;
}

/* ours (measurements, not in the scenes): N mouse moves over a raster of the view, each the hover snap of a point pick /
   the wireframe cursor (cui_cursor_world's path); prints the mean ms per move and a checksum of the results */
void snap_bench(int n) {
    if (n < 1) n = 1;
    struct timespec t0, t1; double sum = 0; int hits = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < n; i++) {
        float mx = vp_x + (float)vp_w * ((i * 37) % 101) / 101.0f, my = vp_y + (float)vp_h * ((i * 53) % 97) / 97.0f;
        double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, mx, my - vp_y, w);
        V3 p = { w[0], w[1], w[2] };
        geo_snap_ptr(mx, my); int k = geo_snap(&p, NULL, -1, 0); geo_snap_ptr_off();
        if (k) { hits++; sum += p.x + p.y + p.z; }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / n;
    printf("    snapbench %d moves: %.3f ms/move, %d snapped, checksum %.6f\n", n, ms, hits, sum);
}
