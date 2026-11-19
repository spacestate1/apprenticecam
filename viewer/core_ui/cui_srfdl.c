/* cui_srfdl.c - the surfaces' display-list nodes (mcsrfdl.c) kept as Mastercam keeps them, and the shading-off surface pick on them
 * (formats/chaining.md "The display-list points (port)").
 * When Mastercam (re)builds a surface's points (decomp/chain_srfedge_dl3.c):
 *  - store_ent_dl (0x180340430) when an entity is stored, and FUN_18032d870 (the full rebuild) when a file is opened (init_db ->
 *    FUN_1801da060 -> FUN_1801da220: the rebuild before fit()) - both at the view of that moment. Here: scene_upload_hook (scene.c
 *    upload(), before a load's fit): a node for every surface without one, or whose record changed.
 *  - the repaint FUN_1801dadd0 (l.147-160, asm 1801db17e..1801db1d5): with "Surface outline refine" (registry GUI\Graphics, absent in
 *    the user's hive: DAT_180876dc5 = its .data 1) and the level floor(log2((int)get_y_scr_scl)) different from DAT_1809a0148 (.data 0):
 *    FUN_180130520(1) clears the built nodes whose points' view extents (FUN_18013e1e0) meet the box of 1.5 x the view (+0x6b8 / +0x6c0
 *    centre, 0.75 x +0x758 width / +0x760 height), and when it cleared one FUN_180130af0 rebuilds every unbuilt node and the level is
 *    kept. Not for render_cfg 3 (Shaded) / page setup / the view's +0x45a9 flag (taken clear). Here: scene_draw_hook and before
 *    a pick (the repaint that precedes it).
 *  - DoShadeShadedOutline (0x18046d540) when render_cfg was not 4, DoShadeWire / DoShadeWireDimmed / DoShadeWireHidden (0x18046d680 /
 *    0x18046d7d0 / 0x18046d8f0) when it was 4: every node cleared (FUN_180130520(0)) and rebuilt (FUN_180130af0) at the view of the
 *    switch; DoShadeShaded (0x18046d440) rebuilds none (ps_del_lists only), so the nodes keep the variant of the mode they were built
 *    in. Here: the render_cfg is compared at each check (decomp/shade15/handlers.c).
 * The pick (select_ent_univ 0x18020f4a0 l.403-411): shading off, the entity's node built (+0x10) -> FUN_18013cbc0 on its points; not
 * built -> dist_to_surf (cui_chain.c dist_to_surf_uv). Shading on: gl_Hit_Surfs on the shading nodes (srfhit_pick below, mcglhit.c). */
#include "core_ui.h"
#include "mcsrfdl.h"
#include "mcglhit.h"
#include "mccsurf.h"
#include "mcsurf.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { SdlNode nd; int tried, fail; const char *why; const uint8_t *rec; uint32_t n; uint64_t h; } Node;
static Node *nodes; static int nnodes, part_gen = -1, dl_cfg = -1;   /* dl_cfg: the render_cfg at the last check */
static int dl_level;                 /* DAT_1809a0148 (.data 0) */
#define REFINE 1                     /* DAT_180876dc5: registry "Surface outline refine" absent -> .data 1 */

static uint64_t fnv(const uint8_t *p, uint32_t n) { uint64_t h = 1469598103934665603ull; for (uint32_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; } return h; }
static void view_now(SdlView *v) {
    v->y_scr = view_h > 0 && vht > 0 ? view_h / vht : 1.0;   /* set_parms (FUN_1801dd950): +0x770 = client height / +0x760 */
    v->x_scr = v->y_scr;                                     /* +0x768 = +0x770 / +0x780 (the pixel aspect, 1) */
    v->wk = vht; v->render_cfg4 = scene_render_cfg() == 4; v->tol = mc_systol.tol;
}
static int level_now(void) { double y = view_h > 0 && vht > 0 ? view_h / vht : 1.0; unsigned u = (unsigned)(int)y; int c = 0; while ((u >>= 1) != 0) c++; return c; }
static void vm_now(double vm[6]) { for (int k = 0; k < 6; k++) vm[k] = vmat[k]; }
static int node_ok(int i) { return i >= 0 && i < F.nsurfs && F.surfs[i].ent >= 0 && F.surfs[i].ent < F.nents && !F.surfs[i].blanked && !F.surfs[i].off; }
static void build(int i) {
    SdlView v; view_now(&v); Node *q = &nodes[i];
    q->tried = 1; q->rec = F.surfs[i].rec; q->n = F.surfs[i].n; q->h = fnv(q->rec, q->n);
    const char *why = NULL; int r = sdl_build(&F, &F.surfs[i], &v, &q->nd, &why);
    q->fail = r < 0; q->why = why;
    scene_srfdl_gen++;                 /* the Outline Shaded outline is drawn from the nodes (scene.c build_surf_wire) */
}
static void reset(void) { for (int i = 0; i < nnodes; i++) sdl_free(&nodes[i].nd); free(nodes); nodes = NULL; nnodes = 0; }
/* store_ent_dl / FUN_18032d870: nodes for the surfaces that have none (a new part: all) at the current view */
static void store_sync(int chk) {   /* chk: also the records changed since (the upload hook) */
    if (part_gen != scene_part_gen) { reset(); part_gen = scene_part_gen; }
    if (dl_cfg < 0) dl_cfg = scene_render_cfg();
    if (nnodes < F.nsurfs) { nodes = realloc(nodes, sizeof *nodes * (size_t)F.nsurfs); memset(nodes + nnodes, 0, sizeof *nodes * (size_t)(F.nsurfs - nnodes)); nnodes = F.nsurfs; }
    for (int i = 0; i < F.nsurfs; i++) {
        Node *q = &nodes[i];
        if (!node_ok(i)) { if (q->tried) { sdl_free(&q->nd); memset(q, 0, sizeof *q); } continue; }
        if (!q->tried || q->rec != F.surfs[i].rec || q->n != F.surfs[i].n || (chk && q->h != fnv(F.surfs[i].rec, F.surfs[i].n))) build(i);
    }
}
/* FUN_180130af0: every unbuilt node again */
static void rebuild_unbuilt(void) { for (int i = 0; i < nnodes; i++) if (node_ok(i) && nodes[i].tried && !nodes[i].nd.built) build(i); }
/* the repaint's checks (see the top) */
static void repaint_sync(void) {
    store_sync(0);
    int cfg = scene_render_cfg();
    if (cfg != dl_cfg) {               /* DoShadeShadedOutline from render_cfg != 4, DoShadeWire* from 4: FUN_180130520(0) then FUN_180130af0 */
        int any = 0, re = (cfg == 4) != (dl_cfg == 4) && cfg != 3;
        if (re) for (int i = 0; i < nnodes; i++) if (node_ok(i) && nodes[i].tried) { any = 1; if (nodes[i].nd.built) nodes[i].nd.built = 0; }
        dl_cfg = cfg;
        if (any) rebuild_unbuilt();
    }
    int lv = level_now();
    if (REFINE && cfg != 3 && lv != dl_level) {    /* FUN_180130520(1) */
        double vm[6], W = view_h > 0 ? vht * view_w / view_h : vht, cx = vcx, cy = vcy, hw = W * 0.75, hh = vht * 0.75; int any = 0;
        vm_now(vm);
        for (int i = 0; i < nnodes; i++) {
            if (!node_ok(i) || !nodes[i].tried) continue;
            double b[4]; if (!sdl_extents(&nodes[i].nd, vm, b)) continue;
            if (cy - hh <= b[3] && b[2] <= hh + cy && cx - hw <= b[1] && b[0] <= hw + cx) { nodes[i].nd.built = 0; any = 1; }   /* FUN_180132370 */
        }
        if (any) { rebuild_unbuilt(); dl_level = lv; }
    }
}
static void hook_upload(void) { store_sync(1); }
static void hook_draw(void) { if (loaded) repaint_sync(); }
static int hook_pts(int i, const void **rec) {   /* surface i's built node (FUN_18031f620 draws it when +0x10 is set) */
    if (i < 0 || i >= nnodes || !node_ok(i) || !nodes[i].tried || nodes[i].fail || !nodes[i].nd.built) return 0;
    *rec = nodes[i].nd.r; return nodes[i].nd.n;
}
__attribute__((constructor)) static void srfdl_hooks(void) { scene_upload_hook = hook_upload; scene_draw_hook = hook_draw; scene_srfdl_pts = hook_pts; }

/* the shading-off pick of surface F.surfs[i] at the cursor (view x / y): 1 = the node is built and a segment is nearer than best (*d,
   uv); 0 = built, nothing nearer; -1 = no built node (select_ent_univ then takes dist_to_surf); -2 = this program cannot evaluate the
   surface (*why) */
int srfdl_pick(int i, const double cur[2], double best, double *d, double uv[2], const char **why) {
    if (loaded) repaint_sync();
    if (!node_ok(i) || i >= nnodes || !nodes[i].tried) return -1;
    if (nodes[i].fail) { if (why) *why = nodes[i].why; return -2; }
    if (!nodes[i].nd.built) return -1;
    double vm[6], W = view_h > 0 ? vht * view_w / view_h : vht, clip[4] = { vcx - W * 0.5, vcx + W * 0.5, vcy - vht * 0.5, vcy + vht * 0.5 };
    vm_now(vm);   /* view +0x788 / +0x790 / +0x798 / +0x7a0: scrn_to_uu of the client rectangle (FUN_1801dde20) */
    return sdl_pick(&nodes[i].nd, &F.surfs[i], vm, clip, cur, best, d, uv);
}
/* select_ent_univ's dist_to_surf for a trimmed surface without a built node: FUN_1802de180 (mcsrfdl.c sdl_dist_trimmed, oracle-checked:
   tools/tests/dts_oracle.sh) with surf_division's view inputs as the drawing has them (get_wk_height = vht, get_x / y_scr_scl = pixels per
   unit) and the view's x / y axes (proj_iso_pt). 1 found (*d, uv), 0 none, -1 not ported (*why) */
int srfdl_dist_trimmed(int i, const double cur[2], double best, double *d, double uv[2], const char **why) {
    if (i < 0 || i >= F.nsurfs) return 0;
    double vm[6], xs = view_h > 0 && vht > 0 ? view_h / vht : 1.0; vm_now(vm);
    return sdl_dist_trimmed(&F, &F.surfs[i], vm, cur, best, vht, xs, xs, mc_systol.tol, d, uv, why);
}
/* tests: the node's record count / built flag / loop records */
int srfdl_info(int i, int *n, int *nloop) {
    if (loaded) repaint_sync();
    if (i < 0 || i >= nnodes || !nodes[i].tried) return -1;
    *n = nodes[i].nd.n; *nloop = nodes[i].nd.nloop; return nodes[i].fail ? -2 : nodes[i].nd.built;
}

/* ------------------------------------------------------------------------------------------------ shading on: gl_Hit_Surfs (mcglhit.c) */
/* The surfaces' shading nodes (FUN_180224100's list DAT_18099e1c8): FUN_180137640 -> FUN_180136460 / FUN_180136cb0 -> FUN_180136510 ->
   FUN_180136f70 = make_surf_polygons(1, 1, ent, gl_tolerance, 0, 0) when a surface is drawn shaded (FUN_180229d90). Here a node per
   surface made at the first shaded pick that needs it (the triangles depend only on the record and gl_tolerance), again when the record
   or gl_tolerance changed. A surface whose mesh is not ported (glh_mesh -1: kinds other than 0 / 10 on a kind 0 base) has no node here:
   *noport counts those the cursor's bounding-circle / screen-box test passes. */
typedef struct { GlhMesh m; int tried, fail; const char *why; const uint8_t *rec; uint32_t n; double gl; } GNode;
static GNode *ghn; static int nghn, ghn_gen = -1;
static void ghn_reset(void) { for (int i = 0; i < nghn; i++) glh_free(&ghn[i].m); free(ghn); ghn = NULL; nghn = 0; }
static GNode *ghn_get(int i) {
    if (ghn_gen != scene_part_gen) { ghn_reset(); ghn_gen = scene_part_gen; }
    if (nghn < F.nsurfs) { ghn = realloc(ghn, sizeof *ghn * (size_t)F.nsurfs); memset(ghn + nghn, 0, sizeof *ghn * (size_t)(F.nsurfs - nghn)); nghn = F.nsurfs; }
    GNode *q = &ghn[i]; double gl = mc_systol.gl_tolerance;
    if (!q->tried || q->rec != F.surfs[i].rec || q->n != F.surfs[i].n || q->gl != gl) {
        glh_free(&q->m); q->tried = 1; q->rec = F.surfs[i].rec; q->n = F.surfs[i].n; q->gl = gl; q->why = NULL;
        q->fail = glh_mesh(&F, &F.surfs[i], gl, &q->m, &q->why) < 0;
    }
    return q;
}
/* the cursor ray against every displayed surface's node (gl_Hit_Surfs l.239-433): the hit nearest the viewer. ok(i) = the caller's
   candidates (the selection mask, level, sel bits). Returns the surface index (F.surfs) or -1; *b the hit. */
static int gl_scan(const double cur[2], int (*ok)(int), GlhBest *b, int *noport, const char **why) {
    GlhView v; glh_view(&v, vmat, cur); memset(b, 0, sizeof *b); if (noport) *noport = 0;
    for (int i = 0; i < F.nsurfs; i++) {
        if (!node_ok(i) || (ok && !ok(i))) continue;
        if (!glh_candidate(&F.surfs[i], &v)) continue;
        GNode *q = ghn_get(i);
        if (q->fail) { if (noport) (*noport)++; if (why && !*why) *why = q->why; continue; }
        glh_surface(&q->m, &v, mc_systol.tol, i, b);
    }
    return b->found ? b->idx : -1;
}
/* gl_Hit_Surfs with its (u, v) (the tail: mccsurf.c cs_glhit_uv): 1 = surface *srf hit, uv (*uv_set 0: no projection wrote one),
   0 = none, -2 = the hit surface's (u, v) is not ported (*why) */
int srfhit_pick(const double cur[2], int (*ok)(int), int *srf, double uv[2], int *uv_set, double *hitz, int *noport, const char **why) {
    GlhBest b; int i = gl_scan(cur, ok, &b, noport, why);
    *srf = i; *uv_set = 0; if (hitz) *hitz = b.z;
    if (i < 0) return 0;
    const Surf *s = &F.surfs[i], *bs = s->kind == 10 ? mcam_surf_base(&F, s) : NULL;
    CsSurf cs = { s->rec, s->n, bs ? bs->rec : NULL, bs ? bs->n : 0 };
    CsParams cp; memset(&cp, 0, sizeof cp); cp.tol = mc_systol.tol; cp.chord = mc_systol.surf_step_chord_dev; cp.min_step = mc_systol.curve_min_step; cp.metric = mcam_is_metric(&F) == 1;
    if (cs_glhit_uv(&cs, &cp, b.hit, mc_systol.gl_tolerance, uv, uv_set) < 0) { if (why) *why = "the (u, v) of a shaded pick on this surface kind (build_grid / project_pt_onto_surf) is NOT PORTED"; return -2; }
    return 1;
}
/* the surface has a shading node here (its make_surf_polygons is ported; a mesh of no triangles counts: no node in Mastercam either) */
int srfhit_ported(int i) { return node_ok(i) && !ghn_get(i)->fail; }
/* select_ent_univ's point of the shaded hit (l.820: eval_surf(surf, 0, u, v)): 0 ok */
int srfhit_point(int i, const double uv[2], double P[3]) {
    const Surf *s = &F.surfs[i], *bs = s->kind == 10 ? mcam_surf_base(&F, s) : NULL;
    CsSurf cs = { s->rec, s->n, bs ? bs->rec : NULL, bs ? bs->n : 0 };
    return cs_surf_point(&cs, uv[0], uv[1], P);
}
