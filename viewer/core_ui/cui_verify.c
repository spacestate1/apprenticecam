/* cui_verify.c - the Verify window's model (stock-removal simulation), moved out of viewer/mcamui.c (Verify section) in
 * phase 8 of formats/qt_port_plan.md; names as they were there. The engine is viewer/verify.c.
 *
 * Mastercam 2017's Verify is the Mastercam Simulator (UBVS: ubvs.dll WPF window + ModuleWorks engine). 2026-10-24
 * (formats/simulator.md): the window's playback model is decoded from the managed code (ubvs.dll MainWindowViewModel /
 * Regulator, mwsimstreammanager.dll MachineSimulationPlayer.rst_StandardPlay / StepContextOperationIterator) and ported here:
 * the run modes (Play / Next Operation / End / Step), the stop conditions, Loop, the speed regulator, the Move List values,
 * the settings of mastercamsimulatordefaults.x. The stock-removal ENGINE (verify.c) stays OURS (ModuleWorks is not ported).
 * The launch is the decoded one (decomp/opman_toolbar.c case 0x823a): Toolpaths manager toolbar "Verify selected operations"
 * / an op sub-item right-click -> OMverifySelectedOperations. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "core_ui.h"
#include "opcodes.h"
#include "cam_fileop.h"
#include "verify.h"
#include "cam2d.h"

/* optree_text.inc's helpers (the op's own tool data): a stored op's record short, the op made here with that index */
static int otx_s16(const Op *o, int off) { return o->raw && off + 2 <= (int)o->raw_n ? (int16_t)(o->raw[off] | o->raw[off + 1] << 8) : 0; }
static CamOp *otx_camop(int i) { for (int k = 0; k < ncamops; k++) if (camops[k]->op_index == i) return camops[k]; return NULL; }

int vf_on, vf_newwin, vf_playing, vf_pos, vf_dirty;
int vf_run;                                             /* VF_RUN_*: the player job running (MachineSimulationPlayer) */
int vf_last_stop;                                       /* VF_STOP_* bits of the last stop (SimulationStateChangedEventArgs) */
VfSimSettings vf_set;                                   /* the Simulator settings (mastercamsimulatordefaults.x) */
static double vf_last_render;
static int vf_job_steps;                               /* steps of the running job (rst_StandardPlay num) */
static int vf_solid_was;                                /* solid_mode before verify (the part solid is hidden while
                                                           verifying - it would occlude the stock; OURS) */
static double vf_last_adv;
static int vf_show_was[3];                             /* show_tp / show_geom / show_axes before the Simulator (restored at close) */
VF *vf; NcMotion *vfm; int vfnm;
static VfTool vf_tl[256]; static signed char vf_tl_ok[256];           /* by tool number (0 = not built, 1 ok, -1 failed) */
static VfSnap *vf_snaps; static int *vf_snap_m, *vf_snap_w, vf_nsnap, vf_cap_snap;
/* the cut colours (formats/simulator.md 9): the verified operation's index per motion (ModuleWorks' operation index, the
   key of WorkpieceDrawMode 3 "Operation") and, per operation, ToolpathInfo.ComputeToolColorIndex's toolColorIdx (the
   colour of WorkpieceDrawMode 2 "Toolnumber") */
static int *vf_opseq, *vf_toolci, vf_nop;
static void vf_build_opseq(void) {
    free(vf_opseq); free(vf_toolci); vf_opseq = vf_toolci = NULL; vf_nop = 0;
    if (vfnm <= 0) return;
    vf_opseq = malloc(vfnm * sizeof(int));
    for (int i = 0; i < vfnm; i++) { if (i == 0 || vfm[i].op_index != vfm[i - 1].op_index) vf_nop++; vf_opseq[i] = vf_nop - 1; }
    int *tool = malloc(vf_nop * sizeof(int)); vf_toolci = malloc(vf_nop * sizeof(int));
    for (int k = 0; k < vf_nop; k++) tool[k] = -1;      /* ?NoTool@VerificationOperation@ModuleWorks@@2HB = -1 (-Module-.cs: data FF FF FF FF) */
    for (int i = 0; i < vfnm; i++) if (vfm[i].kind != NCM_TOOL && tool[vf_opseq[i]] < 0) tool[vf_opseq[i]] = vfm[i].tool;
    /* mwsimstreammanager ToolpathInfo.ComputeToolColorIndex: an operation whose tool is the same as an earlier operation's
       (the first such, scanning from 0) takes that one's index; else 1 + the highest index so far (NoTool = -1 to start).
       IsTheSameTool (ModuleWorks' tool-definition compare) -> ours: the same tool number. An operation without a tool keeps
       NoTool (-1). */
    for (int k = 0; k < vf_nop; k++) {
        vf_toolci[k] = -1;
        if (tool[k] < 0) continue;
        int hi = -1, found = -1;
        for (int j = 0; j < k; j++) { if (vf_toolci[j] > hi) hi = vf_toolci[j]; if (tool[j] >= 0 && tool[j] == tool[k]) { found = vf_toolci[j]; break; } }
        vf_toolci[k] = found >= 0 ? found : hi + 1;
    }
    free(tool);
}

static void vf_take_snap(int midx) {                    /* at an op boundary (backward seek restores here) */
    if (vf_nsnap == vf_cap_snap) { vf_cap_snap = vf_cap_snap ? vf_cap_snap * 2 : 16;
        vf_snaps = realloc(vf_snaps, vf_cap_snap * sizeof *vf_snaps); vf_snap_m = realloc(vf_snap_m, vf_cap_snap * sizeof(int));
        vf_snap_w = realloc(vf_snap_w, vf_cap_snap * sizeof(int)); }
    vf_snap_m[vf_nsnap] = midx; vf_snap_w[vf_nsnap] = vf->nwarn; vf_snap(vf, &vf_snaps[vf_nsnap]); vf_nsnap++;
}
static void vf_apply(int i) {                           /* cut motion i with its tool */
    const NcMotion *m = &vfm[i];
    if (m->kind == NCM_TOOL) return;
    const Tool *tl = mcam_tool(&F, m->tool);
    Tool fb;                                            /* no such part tool: the op's own tool data (as bp_sync_tool) */
    if (!tl) {
        memset(&fb, 0, sizeof fb); fb.num = m->tool;
        int oi = m->op_index; CamOp *c = oi >= 0 && oi < F.nops ? otx_camop(oi) : NULL;
        if (c) { fb.dia = c->p.tool_dia; fb.type = c->p.tool_type; fb.corner_r = c->p.corner_r; fb.tip_angle = c->p.tip_angle; }
        else if (oi >= 0 && oi < F.nops && F.ops[oi].raw) {
            const Op *o = &F.ops[oi]; if (o->raw_n >= 0x358) memcpy(&fb.dia, o->raw + 0x350, 8); fb.type = otx_s16(o, 0x34c); }
        tl = &fb;
    }
    if (tl->dia <= 0 || m->tool < 0 || m->tool >= 256) return;
    if (!vf_tl_ok[m->tool]) vf_tl_ok[m->tool] = vf_tool(tl, &vf_tl[m->tool]) ? 1 : -1;
    vf->key = vf_opseq ? vf_opseq[i] : 0;               /* the cut colour key: this operation's index */
    if (vf_tl_ok[m->tool] == 1) vf_cut(vf, &vf_tl[m->tool], m);
}
void vf_seek(int target) {                              /* motions [0, target) are applied */
    if (target < 0) target = 0; if (target > vfnm) target = vfnm;
    if (target < vf_pos) {
        int s = -1; for (int k = 0; k < vf_nsnap; k++) if (vf_snap_m[k] <= target) s = k;
        if (s >= 0) { vf_restore(vf, &vf_snaps[s]); vf_pos = vf_snap_m[s]; vf->nwarn = vf_snap_w[s]; vf_nsnap = s + 1; }
        else { vf_reset(vf); vf_pos = 0; vf_nsnap = 0; }
    }
    while (vf_pos < target) {
        if (vf_pos == 0 || vfm[vf_pos].op_index != vfm[vf_pos - 1].op_index) vf_take_snap(vf_pos);
        vf_apply(vf_pos); vf_pos++;
    }
    vf_dirty = 1;
}
/* ---- Compare (formats/simulator.md 8) --------------------------------------------------------------------------------
 * ubvs.dll: the "Compare" tool window (mainwindow.baml twStockCompare), MainWindowViewModel (StockCompareComboBoxSource,
 * OnUpdateStockCompareComboBoxSelectedItem, ApplyRefresh, StockToLeave), StockCompareGrid, CompareWindowViewModel.ApplyOk.
 * The deviation itself is ModuleWorks (McamVerification.SetTarget / DeviationOffset): OURS - the stock's height per cell
 * minus the target's top at that cell (vf_target_top) minus the applied stock to leave; no target under a cell = +inf.
 * The "Workpiece" item: Mastercam inserts it when MachSim sent a workpiece (OnSetupCompleted e.WorkpieceSet; machsim.dll
 * builds it from an entity list through get_raw_ent + wr_stl - WHICH entities is NOT DECODED); ours: the part's solids. */
int vf_cmp_open, vf_cmp_nitems, vf_cmp_sel = -1, vf_cmp_need_refresh, vf_cmp_applied;
VfCmpItem vf_cmp_items[16];
static double *vf_cmp_top;                              /* the target's top per cell (NULL = no target) */
static double vf_cmp_off;                               /* DeviationOffset (the applied stock to leave) */
static int vf_cmp_an; static double vf_cmp_aend[VF_CMP_MAX]; static float vf_cmp_argb[VF_CMP_MAX][3];   /* the applied colour scheme */
static void vf_cmp_set_target_tris(const double *t, int n) {
    free(vf_cmp_top); vf_cmp_top = NULL;
    if (vf && t && n > 0) vf_cmp_top = vf_target_top(vf, t, n);
    vf_cmp_need_refresh = 1;                            /* NeedRefresh = true */
    vf_dirty = 1;
}
static int vf_cmp_workpiece_tris(double **out) {        /* ours: the part solids + the solids made here */
    const float *a, *b; int na = scene_part_tris(&a), nb = scene_user_tris(&b);
    if (na + nb <= 0) { *out = NULL; return 0; }
    double *t = malloc(sizeof(double) * 9 * (size_t)(na + nb));
    for (int k = 0; k < na + nb; k++) { const float *q = k < na ? a + 18 * k : b + 18 * (k - na);
        for (int v = 0; v < 3; v++) for (int c = 0; c < 3; c++) t[9 * k + 3 * v + c] = q[6 * v + c]; }
    *out = t; return na + nb;
}
static void vf_cmp_setup(void) {                        /* the combo at setup: Browse..., plus Workpiece (inserted at 0, selected) */
    vf_cmp_nitems = 0; vf_cmp_sel = -1; vf_cmp_applied = 0; vf_cmp_need_refresh = 0; vf_cmp_off = 0;
    free(vf_cmp_top); vf_cmp_top = NULL;
    const float *a; int has_wp = scene_part_tris(&a) + scene_user_tris(&a) > 0;
    if (has_wp) { vf_cmp_items[vf_cmp_nitems].kind = VF_CMP_WORKPIECE; vf_cmp_items[vf_cmp_nitems].path[0] = 0; vf_cmp_nitems++; }
    vf_cmp_items[vf_cmp_nitems].kind = VF_CMP_BROWSE; vf_cmp_items[vf_cmp_nitems].path[0] = 0; vf_cmp_nitems++;
    vf_cmp_sel = has_wp ? 0 : 1;
    if (has_wp) vf_cmp_select(0);                        /* ours: the workpiece is the target from the start (MachSim's) */
}
void vf_cmp_show(int on) { vf_cmp_open = on != 0; vf_dirty = 1; }
int vf_cmp_select(int i) {
    if (i < 0 || i >= vf_cmp_nitems || vf_cmp_items[i].kind == VF_CMP_BROWSE) return 0;
    vf_cmp_sel = i;
    double *t = NULL; int n = 0;
    if (vf_cmp_items[i].kind == VF_CMP_WORKPIECE) n = vf_cmp_workpiece_tris(&t);
    else n = cam3d_stl_load(vf_cmp_items[i].path, &t);  /* ManagedFactory.CreateMesh(units, file); ours: in part units, world */
    vf_cmp_set_target_tris(t, n); free(t);
    return n > 0;
}
int vf_cmp_add_stl(const char *path) {                  /* SetStockCompareStl: the file once, at the end, selected */
    double *t = NULL; int n = cam3d_stl_load(path, &t); free(t);
    if (n <= 0) return 0;
    int at = -1;
    for (int i = 0; i < vf_cmp_nitems; i++) if (vf_cmp_items[i].kind == VF_CMP_STL && !strcmp(vf_cmp_items[i].path, path)) at = i;
    if (at >= 0) { for (int i = at; i + 1 < vf_cmp_nitems; i++) vf_cmp_items[i] = vf_cmp_items[i + 1]; vf_cmp_nitems--; }
    if (vf_cmp_nitems >= 16) return 0;
    vf_cmp_items[vf_cmp_nitems].kind = VF_CMP_STL; snprintf(vf_cmp_items[vf_cmp_nitems].path, sizeof vf_cmp_items[0].path, "%s", path);
    vf_cmp_nitems++;
    return vf_cmp_select(vf_cmp_nitems - 1);
}
int vf_cmp_can_refresh(void) { return vf_on && vf_cmp_need_refresh && vf_cmp_sel >= 0 && vf_cmp_items[vf_cmp_sel].kind != VF_CMP_BROWSE; }   /* ApplyCanRefresh */
void vf_cmp_refresh(void) {                             /* ApplyRefresh: DeviationOffset = StockToLeave, the colour scheme updated */
    if (!vf_cmp_can_refresh()) return;
    vf_cmp_off = vf_set.stock_to_leave;
    vf_cmp_an = vf_set.cmp_n;
    for (int i = 0; i < vf_cmp_an; i++) { vf_cmp_aend[i] = vf_set.cmp_end[i]; for (int c = 0; c < 3; c++) vf_cmp_argb[i][c] = vf_set.cmp_rgb[i][c] / 255.f; }
    vf_cmp_need_refresh = 0; vf_cmp_applied = 1; vf_dirty = 1;
}
void vf_cmp_set_stl(double v) { if (vf_set.stock_to_leave != v) { vf_set.stock_to_leave = v; vf_cmp_need_refresh = 1; } }
void vf_cmp_options(double tol) {                       /* CompareWindowViewModel.ApplyOk: only with Tolerance > 0; the count stays */
    if (!(tol > 0)) return;
    int n = vf_set.cmp_n, neg = n / 2, pos = n - neg - 1, k = 0;
    for (int i = 0; i < neg; i++) vf_set.cmp_end[k++] = -tol * pow(2.0, neg - i - 1);
    for (int j = 0; j < pos; j++) vf_set.cmp_end[k++] = tol * pow(2.0, j);
    vf_set.cmp_end[k] = INFINITY;
    vf_cmp_need_refresh = 1;
}
/* the grid lists the items last to first (StockCompareColors getter); a row's Start = the previous item's End ("Min" for the
   first item), its End = its own ("Max" for the last); editing pushes the neighbours to keep the order (Increase / Decrease) */
static int vf_cmp_item_of_row(int row) { return vf_set.cmp_n - 1 - row; }
void vf_cmp_row(int row, char *start, size_t ns, char *end, size_t ne, unsigned char rgb[3]) {
    int k = vf_cmp_item_of_row(row);
    if (k < 0 || k >= vf_set.cmp_n) { start[0] = end[0] = 0; return; }
    if (k == 0) snprintf(start, ns, "Min"); else snprintf(start, ns, "%g", (float)vf_set.cmp_end[k - 1]);   /* Resources.StockCompareMin; float.ToString() */
    if (k == vf_set.cmp_n - 1) snprintf(end, ne, "Max"); else snprintf(end, ne, "%g", (float)vf_set.cmp_end[k]);
    memcpy(rgb, vf_set.cmp_rgb[k], 3);
}
void vf_cmp_set_value(int row, int start, double v) {
    int k = vf_cmp_item_of_row(row);
    if (start) k--;                                      /* StartValue sets prevItem.EndValue */
    if (k < 0 || k >= vf_set.cmp_n - 1) return;          /* "Min" / "Max" are read-only */
    double old = vf_set.cmp_end[k];
    vf_set.cmp_end[k] = v;
    if (v == old) return;
    if (v > old) { for (int i = k + 1; i < vf_set.cmp_n; i++) if (vf_set.cmp_end[i] < v) vf_set.cmp_end[i] = v; }   /* Increase */
    else for (int i = k - 1; i >= 0; i--) if (vf_set.cmp_end[i] > v) vf_set.cmp_end[i] = v;                          /* Decrease */
    vf_cmp_need_refresh = 1;
}
void vf_cmp_set_color(int row, const unsigned char rgb[3]) {
    int k = vf_cmp_item_of_row(row);
    if (k < 0 || k >= vf_set.cmp_n) return;
    memcpy(vf_set.cmp_rgb[k], rgb, 3); vf_cmp_need_refresh = 1;
}
static float *vf_cmp_colors(void) {                      /* per cell: the applied band of z - (top + offset) (ours) */
    if (!vf || !vf_cmp_open || !vf_cmp_applied || !vf_cmp_top || vf_cmp_an <= 0) return NULL;
    size_t nc = (size_t)vf->nx * vf->ny;
    float *c = malloc(nc * 3 * sizeof(float)); if (!c) return NULL;
    for (size_t i = 0; i < nc; i++) {
        double d = vf_cmp_top[i] < -1e299 ? INFINITY : vf->z[i] - (vf_cmp_top[i] + vf_cmp_off);
        int b = vf_cmp_an - 1;
        for (int k = 0; k < vf_cmp_an; k++) if (d <= vf_cmp_aend[k]) { b = k; break; }
        memcpy(c + 3 * i, vf_cmp_argb[b], 12);
    }
    return c;
}
void vf_cmp_dump(void) {
    printf("    compare open %d items", vf_cmp_open);
    for (int i = 0; i < vf_cmp_nitems; i++) printf(" [%s%s]", vf_cmp_items[i].kind == VF_CMP_BROWSE ? "Browse..." : vf_cmp_items[i].kind == VF_CMP_WORKPIECE ? "Workpiece" : vf_cmp_items[i].path, i == vf_cmp_sel ? "*" : "");
    printf(" stl %g need_refresh %d applied %d target %d\n", vf_set.stock_to_leave, vf_cmp_need_refresh, vf_cmp_applied, vf_cmp_top != NULL);
    for (int r = 0; r < vf_set.cmp_n; r++) { char a[32], b[32]; unsigned char c[3]; vf_cmp_row(r, a, sizeof a, b, sizeof b, c);
        printf("    compare row %d #%02X%02X%02X %s .. %s\n", r, c[0], c[1], c[2], a, b); }
    if (vf && vf_cmp_open && vf_cmp_applied && vf_cmp_top) {   /* the cells per applied band */
        int cnt[VF_CMP_MAX] = { 0 }; size_t nc = (size_t)vf->nx * vf->ny;
        for (size_t i = 0; i < nc; i++) { double d = vf_cmp_top[i] < -1e299 ? INFINITY : vf->z[i] - (vf_cmp_top[i] + vf_cmp_off);
            int b = vf_cmp_an - 1; for (int k = 0; k < vf_cmp_an; k++) if (d <= vf_cmp_aend[k]) { b = k; break; } cnt[b]++; }
        printf("    compare cells"); for (int k = 0; k < vf_cmp_an; k++) printf(" %d", cnt[k]); printf("\n");
    }
}
/* ---- the cut colours (formats/simulator.md 9) ---------------------------------------------------------------------------
 * ubvs.dll MainWindowViewModel.UpdateColorScheme: Color Loop off -> WorkpieceDrawMode 12 with one colour, Scheme[0]
 * (UbvsCustomSettings.Scheme[0] = Colors.Yellow; OptionsXaml.resx GraphicsOptionsDefaultCutColorTextBlock_Text "First color
 * used as material removal color in verification mode if color loop is disabled."); Color Loop on -> drawMode Operation
 * (WorkpieceDrawMode 3: operation k -> Scheme[k % n]) or Toolnumber (WorkpieceDrawMode 2: the operation's tool ->
 * Scheme[toolColorIdx % n]). The faces never cut keep the mesh colour, RenderingSettings StockColor
 * (SimStreamAbstractRenderer.UpdateRenderSettings: verifier.MeshColor = GetStockColor()). Compare (ShowGouges) wins over
 * both (UpdateColorScheme's first branch). Which faces count as cut is ModuleWorks' (native); ours: a cell a cut lowered
 * (its top) and the inner wall on its low side (vf_mesh_ex wall_low); the stock's outer sides stay StockColor. */
static const unsigned char *vf_cut_rgb(int key) {
    if (vf_set.scheme_n <= 0) return vf_set.stock_rgb;
    if (!vf_set.color_loop) return vf_set.scheme_rgb[0];
    int ci = key;
    if (vf_set.loop_by_tool) { ci = vf_toolci && key >= 0 && key < vf_nop ? vf_toolci[key] : -1; if (ci < 0) ci = 0; }   /* UpdateOperationSlider: < 0 -> 0 */
    return vf_set.scheme_rgb[ci % vf_set.scheme_n];
}
static float *vf_cut_colors(void) {
    size_t nc = (size_t)vf->nx * vf->ny;
    float *c = malloc(nc * 3 * sizeof(float)); if (!c) return NULL;
    for (size_t i = 0; i < nc; i++) {
        const unsigned char *q = vf->cut[i] < 0 ? vf_set.stock_rgb : vf_cut_rgb(vf->cut[i]);
        for (int k = 0; k < 3; k++) c[3 * i + k] = q[k] / 255.f;
    }
    return c;
}
/* ours (tests): vfcolors - the cells per drawn colour */
static void vf_colors_dump(void) {
    if (!vf) return;
    size_t nc = (size_t)vf->nx * vf->ny; int ncut = 0;
    struct { unsigned char c[3]; int n; } t[VF_SCHEME_MAX + 1]; int nt = 0;
    for (size_t i = 0; i < nc; i++) {
        const unsigned char *q = vf->cut[i] < 0 ? vf_set.stock_rgb : vf_cut_rgb(vf->cut[i]);
        if (vf->cut[i] >= 0) ncut++;
        int k = 0; while (k < nt && memcmp(t[k].c, q, 3)) k++;
        if (k == nt) { if (nt > VF_SCHEME_MAX) continue; memcpy(t[nt].c, q, 3); t[nt].n = 0; nt++; }
        t[k].n++;
    }
    printf("    verify colours: color loop %d by %s, %d of %zu cells cut;", vf_set.color_loop, vf_set.loop_by_tool ? "Toolnumber" : "Operation", ncut, nc);
    for (int k = 0; k < nt; k++) printf(" #%02X%02X%02X %d", t[k].c[0], t[k].c[1], t[k].c[2], t[k].n);
    printf("\n");
}
void vf_push_mesh(void) {
    if (!vf_dirty) return;
    vf_dirty = 0;
    float *cc = vf_cmp_colors(), rgb[3] = { 1, 1, 1 };  /* the colours are per cell: the bands', or the cut / stock colours */
    int cut = !cc;
    if (cut) cc = vf_cut_colors();
    float side[3] = { vf_set.stock_rgb[0] / 255.f, vf_set.stock_rgb[1] / 255.f, vf_set.stock_rgb[2] / 255.f };   /* RenderingSettings StockColor */
    float *mv = NULL; int vn = vf_mesh_ex(vf, cc, cc ? NULL : side, cut, cut ? side : NULL, &mv);
    if (!cc) memcpy(rgb, side, sizeof rgb);
    if (getenv("MCAMUI_VF_DEBUG")) { float lo = 1e30f, hi = -1e30f; for (int i = 0; i < vn; i++) { if (mv[i * 6] < lo) lo = mv[i * 6]; if (mv[i * 6] > hi) hi = mv[i * 6]; }
        fprintf(stderr, "vf_push_mesh: %d verts, x %.2f..%.2f (ctr %.2f %.2f %.2f)\n", vn, lo, hi, ctrx, ctry, ctrz); }
    /* Stock checkbox (RenderingSettings StockVis): hidden = no mesh (ours: scene_set_verify n = 0) */
    /* no depth tint (ours, 2026-11-14): the colours are Mastercam's (cut / stock, or the Compare bands), not darkened */
    if (vf_set.stock_vis) scene_set_verify(mv, vn, rgb, 0, 0); else scene_set_verify(NULL, 0, NULL, 0, 0);
    free(mv); free(cc);
}
/* ---- the Simulator settings (formats/simulator.md 5) ------------------------------------------------------------------
 * UBVSLauncher: Instance = Helper.CreateDefaults(units) (= mwsimstreammanager SimulationSettings.SetDefaults +
 * UbvsCustomSettings field initialisers), then, when PathInfo.PathToXmlSettings exists, Deserialize(that file) over them.
 * Mastercam's path (MachSim.DLL FUN_1801f1020): GetDir(AppDir 4 = <My Documents>\my mcam2017\) +
 * "MastercamSimulatorDefaults.xml" (McamPathInfo.XmlSettingsFileName), CreateXmlFile = true (UBVS writes the defaults there
 * when missing - ours does not write it). Read once per Mastercam session (UBVSLauncher settingsGlobal) and kept for the
 * next launch. The shipped mastercamsimulatordefaults.x holds the same values as CreateDefaults for every key read here.
 * Only the keys our window / engine can use are read (ours); the rest is not interpreted. Colours: #RRGGBB or the WPF /
 * System.Drawing named colours the defaults use (ColorTranslator.FromHtml). */
static void vf_settings_builtin(void) {
    memset(&vf_set, 0, sizeof vf_set);
    vf_set.speed = 10;                                  /* CustomSettings PerformanceIndexVerification 10 (UbvsCustomSettings: 10) */
    vf_set.tool_vis = 1; vf_set.stock_vis = 1;          /* RenderingSettings Tool CV_SHOW, StockVis CV_SHOW */
    vf_set.initial_stock_vis = 0; vf_set.workpiece_vis = 0; vf_set.toolpath_vis = 0;   /* InitialStockVis / WorkpieceVis / ToolpathVis CV_HIDE */
    vf_set.fixture_vis = 1;                             /* FixtureVis CV_SHOW */
    vf_set.wire_vis = 1; vf_set.gnomon_vis = 1; vf_set.axes_vis = 1;   /* ShowWireframeGeometries / ShowGnomon / ShowCoordinateSystem True */
    vf_set.stock_rgb[0] = 0xF6; vf_set.stock_rgb[1] = 0x6D; vf_set.stock_rgb[2] = 0x6D;   /* StockColor #F66D6D */
    vf_set.flute_rgb[0] = 0xDD; vf_set.flute_rgb[1] = 0xBB; vf_set.flute_rgb[2] = 0x00;   /* MillingFluteColor #DDBB00 */
    vf_set.holder_rgb[0] = 0x33; vf_set.holder_rgb[1] = 0x66; vf_set.holder_rgb[2] = 0x99; /* MillingHolderColor #336699 */
    vf_set.precision = 5;                               /* VerifySettings PrecisionIndex 5 */
    vf_set.op_mode = 0; vf_set.tp_segment = 1;          /* BackplotSettings showOperation AllOperations, showToolpathSegment Follow */
    vf_set.show_rapid = 1; vf_set.show_points = 0;      /* showRapidMoves True, showPoints False */
    vf_set.focus = 0;                                   /* RenderingSettings autoFocus CF_NONE */
    vf_set.viewports = 0;                               /* CustomSettings Viewports SingleView */
    /* StopConditions: Enabled / OperationChange / ToolChange / Collision / ToolInspection / CheckChange?Delta all false
       (SetDefaults); LoopSimulation: MachineSimulationPlayer's field default (false), not a setting */
    /* UbvsCustomSettings StockToLeave 0, ToleranceUnit Imperial, StockCompareColors: -0.04 DarkRed, -0.02 Red, -0.01 Yellow,
       0.01 Green, 0.02 LightBlue, 0.04 Blue, +inf DarkBlue (System.Windows.Media.Colors) */
    static const double E[7] = { -0.04, -0.02, -0.01, 0.01, 0.02, 0.04, INFINITY };
    static const unsigned C[7] = { 0x8B0000, 0xFF0000, 0xFFFF00, 0x008000, 0xADD8E6, 0x0000FF, 0x00008B };
    vf_set.stock_to_leave = 0; vf_set.tol_metric = 0; vf_set.cmp_n = 7;
    for (int i = 0; i < 7; i++) { vf_set.cmp_end[i] = E[i]; vf_set.cmp_rgb[i][0] = C[i] >> 16 & 255; vf_set.cmp_rgb[i][1] = C[i] >> 8 & 255; vf_set.cmp_rgb[i][2] = C[i] & 255; }
    /* UbvsCustomSettings IsColorLoop false, drawMode Operation (enum default 0; the shipped file: Operation), Scheme: Yellow,
       Blue, DarkMagenta, Green, Pink, Cyan, Orange, Lime, Maroon, Navy, Gold, Fuchsia (System.Windows.Media.Colors) */
    static const unsigned S[12] = { 0xFFFF00, 0x0000FF, 0x8B008B, 0x008000, 0xFFC0CB, 0x00FFFF, 0xFFA500, 0x00FF00, 0x800000, 0x000080, 0xFFD700, 0xFF00FF };
    vf_set.color_loop = 0; vf_set.loop_by_tool = 0; vf_set.scheme_n = 12;
    for (int i = 0; i < 12; i++) { vf_set.scheme_rgb[i][0] = S[i] >> 16 & 255; vf_set.scheme_rgb[i][1] = S[i] >> 8 & 255; vf_set.scheme_rgb[i][2] = S[i] & 255; }
    snprintf(vf_set.source, sizeof vf_set.source, "built-in (SimulationSettings.SetDefaults / UbvsCustomSettings)");
}
static const char *xml_sect(const char *x, const char *name, const char **end) {   /* <name ...> .. </name> */
    char o[80], c[80]; snprintf(o, sizeof o, "<%s>", name); snprintf(c, sizeof c, "</%s>", name);
    const char *a = strstr(x, o); if (!a) return NULL;
    const char *b = strstr(a, c); if (!b) return NULL;
    *end = b; return a + strlen(o);
}
static int xml_val(const char *a, const char *b, const char *key, char *out, size_t n) {
    char o[80]; snprintf(o, sizeof o, "<%s>", key);
    const char *p = strstr(a, o); if (!p || p > b) return 0;
    p += strlen(o); const char *q = strchr(p, '<'); if (!q) return 0;
    size_t l = (size_t)(q - p); if (l >= n) l = n - 1;
    memcpy(out, p, l); out[l] = 0; return 1;
}
static void xml_bool(const char *a, const char *b, const char *key, int *v) { char t[32]; if (xml_val(a, b, key, t, sizeof t)) *v = (t[0] == 'T' || t[0] == 't');   /* "True" / "true" (XmlSerializer bool, the file uses both) */ }
static void xml_vis(const char *a, const char *b, const char *key, int *v) { char t[32]; if (xml_val(a, b, key, t, sizeof t)) *v = strcmp(t, "CV_HIDE") != 0; }
static void xml_int(const char *a, const char *b, const char *key, int *v) { char t[32]; if (xml_val(a, b, key, t, sizeof t)) *v = atoi(t); }
static void xml_col(const char *a, const char *b, const char *key, unsigned char rgb[3]) {
    /* System.Windows.Media.Colors values of the names the shipped file uses */
    static const struct { const char *n; unsigned c; } NC[] = { { "Goldenrod", 0xDAA520 }, { "Yellow", 0xFFFF00 }, { "Orange", 0xFFA500 },
        { "DarkRed", 0x8B0000 }, { "Red", 0xFF0000 }, { "Green", 0x008000 }, { "LightBlue", 0xADD8E6 }, { "Blue", 0x0000FF },
        { "DarkBlue", 0x00008B }, { "DarkMagenta", 0x8B008B }, { "Pink", 0xFFC0CB }, { "Aqua", 0x00FFFF }, { "Cyan", 0x00FFFF },
        { "Lime", 0x00FF00 }, { "Maroon", 0x800000 }, { "Navy", 0x000080 }, { "Gold", 0xFFD700 }, { "Fuchsia", 0xFF00FF },
        { "Magenta", 0xFF00FF } };
    char t[32]; if (!xml_val(a, b, key, t, sizeof t)) return;
    unsigned c = 0; int ok = 0;
    if (t[0] == '#' && strlen(t) == 7) { c = (unsigned)strtoul(t + 1, NULL, 16); ok = 1; }
    else for (size_t i = 0; i < sizeof NC / sizeof NC[0]; i++) if (!strcmp(t, NC[i].n)) { c = NC[i].c; ok = 1; }
    if (ok) { rgb[0] = c >> 16 & 255; rgb[1] = c >> 8 & 255; rgb[2] = c & 255; }
}
int vf_settings_read(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    char *x = malloc(1 << 20); size_t n = fread(x, 1, (1 << 20) - 1, f); fclose(f); x[n] = 0;
    if (!strstr(x, "<SimulationSettings")) { free(x); return 0; }
    const char *e, *a;
    if ((a = xml_sect(x, "StopConditions", &e))) {
        xml_bool(a, e, "Enabled", &vf_set.stop_enabled); xml_bool(a, e, "OperationChange", &vf_set.stop_opchange);
        xml_bool(a, e, "ToolChange", &vf_set.stop_toolchange); xml_bool(a, e, "Collision", &vf_set.stop_collision);
        xml_bool(a, e, "ToolInspection", &vf_set.stop_toolinsp);
        xml_bool(a, e, "CheckChangeXDelta", &vf_set.stop_dx); xml_bool(a, e, "CheckChangeYDelta", &vf_set.stop_dy);
        xml_bool(a, e, "CheckChangeZDelta", &vf_set.stop_dz);
    }
    if ((a = xml_sect(x, "VerifySettings", &e))) xml_int(a, e, "PrecisionIndex", &vf_set.precision);
    if ((a = xml_sect(x, "BackplotSettings", &e))) {
        char t[48];
        if (xml_val(a, e, "showOperation", t, sizeof t)) vf_set.op_mode = !strcmp(t, "CurrentOperation") ? 1 : !strcmp(t, "Segment") ? 2 : 0;
        if (xml_val(a, e, "showToolpathSegment", t, sizeof t)) vf_set.tp_segment = !strcmp(t, "Trace") ? 0 : !strcmp(t, "Complete") ? 2 : 1;
        xml_bool(a, e, "showRapidMoves", &vf_set.show_rapid); xml_bool(a, e, "showPoints", &vf_set.show_points);
    }
    if ((a = xml_sect(x, "RenderingSettings", &e))) {
        xml_vis(a, e, "Tool", &vf_set.tool_vis); xml_vis(a, e, "StockVis", &vf_set.stock_vis);
        xml_vis(a, e, "InitialStockVis", &vf_set.initial_stock_vis); xml_vis(a, e, "WorkpieceVis", &vf_set.workpiece_vis);
        xml_vis(a, e, "ToolpathVis", &vf_set.toolpath_vis); xml_vis(a, e, "FixtureVis", &vf_set.fixture_vis);
        xml_bool(a, e, "ShowWireframeGeometries", &vf_set.wire_vis); xml_bool(a, e, "ShowGnomon", &vf_set.gnomon_vis);
        xml_bool(a, e, "ShowCoordinateSystem", &vf_set.axes_vis);
        xml_col(a, e, "StockColor", vf_set.stock_rgb); xml_col(a, e, "MillingFluteColor", vf_set.flute_rgb);
        xml_col(a, e, "MillingHolderColor", vf_set.holder_rgb);
        char t[32]; if (xml_val(a, e, "autoFocus", t, sizeof t)) vf_set.focus = !strcmp(t, "CF_TOOL") ? 1 : !strcmp(t, "CF_STOCK") ? 2 : 0;
    }
    if ((a = strstr(x, "<CustomSettings")) && (e = strstr(a, "</CustomSettings>"))) {   /* <CustomSettings xsi:type="UbvsCustomSettings"> */
        xml_int(a, e, "PerformanceIndexVerification", &vf_set.speed);
        char t[32]; if (xml_val(a, e, "Viewports", t, sizeof t)) vf_set.viewports = !strcmp(t, "TwoViewsInRows") ? 1 : !strcmp(t, "TwoViewsInColumns") ? 2 : !strcmp(t, "FourViews") ? 3 : 0;
        if (xml_val(a, e, "StockToLeave", t, sizeof t)) vf_set.stock_to_leave = atof(t);
        if (xml_val(a, e, "ToleranceUnit", t, sizeof t)) vf_set.tol_metric = !strcmp(t, "Metric");
        xml_bool(a, e, "IsColorLoop", &vf_set.color_loop);
        if (xml_val(a, e, "drawMode", t, sizeof t)) vf_set.loop_by_tool = !strcmp(t, "Toolnumber");   /* ColorSchemeType Operation / Toolnumber */
        const char *se, *sa = xml_sect(a, "Scheme", &se);
        if (sa && se < e) {                              /* <Scheme><SchemeColor>name|#RRGGBB</SchemeColor>... */
            int k = 0; const char *p = sa;
            while (k < VF_SCHEME_MAX && (p = strstr(p, "<SchemeColor>")) && p < se) {
                unsigned char rgb[3] = { 0, 0, 0 }; const char *q = strstr(p, "</SchemeColor>"); if (!q || q > se) break;
                xml_col(p, q + 14, "SchemeColor", rgb); memcpy(vf_set.scheme_rgb[k], rgb, 3); k++; p = q;
            }
            if (k) vf_set.scheme_n = k;
        }
        const char *ce, *ca = xml_sect(a, "StockCompareColors", &ce);
        if (ca && ce < e) {                              /* <StockCompareGridItem><EndValue>v|INF</EndValue><col>name|#RRGGBB</col> */
            int k = 0; const char *p = ca, *ie;
            while (k < VF_CMP_MAX && (p = xml_sect(p, "StockCompareGridItem", &ie)) && ie < ce) {
                char v[32]; unsigned char rgb[3] = { 0, 0, 0 };
                if (xml_val(p, ie, "EndValue", v, sizeof v)) vf_set.cmp_end[k] = strstr(v, "INF") ? (v[0] == '-' ? -INFINITY : INFINITY) : atof(v);
                xml_col(p, ie, "col", rgb); memcpy(vf_set.cmp_rgb[k], rgb, 3);
                k++; p = ie;
            }
            if (k) vf_set.cmp_n = k;
        }
    }
    if (vf_set.speed < VF_SPEED_MIN) vf_set.speed = VF_SPEED_MIN; if (vf_set.speed > VF_SPEED_MAX) vf_set.speed = VF_SPEED_MAX;
    snprintf(vf_set.source, sizeof vf_set.source, "%s", path);
    free(x); return 1;
}
/* at the first launch of the session: CreateDefaults, then the per-user file - ours: MCAMUI_SIM_DEFAULTS (tests), else
   <config dir>/MastercamSimulatorDefaults.xml (our config dir stands in for "my mcam2017"); later launches keep vf_set */
void vf_settings_load(void) {
    static int loaded_once;
    if (loaded_once) return;
    loaded_once = 1;
    vf_settings_builtin();
    const char *env = getenv("MCAMUI_SIM_DEFAULTS"); char p[1100];
    if (env && *env) { vf_settings_read(env); return; }
    vf_settings_read(config_file(p, sizeof p, VF_SETTINGS_FILE));
}
/* the ribbon's visibility checkboxes -> our scene: Toolpath = show_tp, Wireframe = show_geom, Axes = show_axes (ours: the
   scene is shared with the main window; restored at close), Tool = the cutter drawn (vf_sync_tool), Stock = the mesh */
void vf_apply_visibility(void) {
    show_tp = vf_set.toolpath_vis != 0; show_geom = vf_set.wire_vis != 0; show_axes = vf_set.axes_vis != 0;
    vf_dirty = 1;
}
/* vfset:NAME=V (ours: the window's toggles by their UBVS ids) */
int vf_set_named(const char *nv) {
    char n[64]; const char *eq = strchr(nv, '='); if (!eq || eq - nv >= 63) return 0;
    memcpy(n, nv, eq - nv); n[eq - nv] = 0; int v = atoi(eq + 1);
    struct { const char *n; int *p; } T[] = { { "IsStopConditionsEnabled", &vf_set.stop_enabled }, { "StopOnOperationChange", &vf_set.stop_opchange },
        { "StopOnToolChange", &vf_set.stop_toolchange }, { "StopOnValueChangeX", &vf_set.stop_dx }, { "StopOnValueChangeY", &vf_set.stop_dy },
        { "StopOnValueChangeZ", &vf_set.stop_dz }, { "ToolpathVisibility", &vf_set.toolpath_vis }, { "ToolVisibility", &vf_set.tool_vis },
        { "StockVisibility", &vf_set.stock_vis }, { "WireframeGeometryVisibility", &vf_set.wire_vis },
        { "CoordinateSystemVisibility", &vf_set.axes_vis }, { "LoopSimulation", &vf_set.loop }, { "IsColorLoop", &vf_set.color_loop },
        { "drawMode", &vf_set.loop_by_tool } };   /* drawMode: 0 Operation, 1 Toolnumber (ours: the Options page is not ported) */
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) if (!strcmp(n, T[i].n)) { *T[i].p = v; vf_apply_visibility(); return 1; }
    return 0;
}
/* ---- the Move List (mainwindow.baml twMoveInfo PropertyGrid; values as MainWindowViewModel.UpdateMoveInfoPropertyGrid) -- */
static void vf_n3(char *o, size_t n, double v) {        /* .NET "N3": thousands separators, 3 decimals (invariant culture) */
    char t[64]; snprintf(t, sizeof t, "%.3f", fabs(v));
    char *dot = strchr(t, '.'); int il = (int)(dot - t), k = 0; char r[96];
    if (v < 0 && strcmp(t, "0.000")) r[k++] = '-';
    for (int i = 0; i < il; i++) { r[k++] = t[i]; if ((il - 1 - i) % 3 == 0 && i < il - 1) r[k++] = ','; }
    snprintf(r + k, sizeof r - k, "%s", dot); snprintf(o, n, "%s", r);
}
static void vf_time(char *o, size_t n, double sec) {     /* TypeToText.TimeToText */
    long t = lround(sec * 100); double s = (double)(t % 6000) / 100.0; t /= 6000; int mi = 0, h = 0;
    if (t > 0) { mi = (int)(t % 60); t /= 60; if (t > 0) h = (int)t; }
    char r[96] = ""; char q[96];
    if (fabs(s) >= 0.01 - 1e-9) { char sv[32]; snprintf(sv, sizeof sv, "%.2f", s); char *z = sv + strlen(sv) - 1; while (*z == '0') *z-- = 0; if (*z == '.') *z = 0; snprintf(r, sizeof r, "%ss ", sv); }   /* Math.Round(s, 2) + "s" */
    if (mi) { snprintf(q, sizeof q, "%dmin %s", mi, r); snprintf(r, sizeof r, "%s", q); }
    if (h) { snprintf(q, sizeof q, "%dh %s", h, r); snprintf(r, sizeof r, "%s", q); }
    snprintf(o, n, "%s", r);
}
static double vf_mlen(const NcMotion *m) {
    if (m->kind == NCM_TOOL) return 0;
    if (m->kind == NCM_CW || m->kind == NCM_CCW) {      /* arc in XY + helix Z (nc_motion_point's model), sampled (ours) */
        double L = 0, p0[3], p1[3]; nc_motion_point(m, 0, p0);
        for (int k = 1; k <= 64; k++) { nc_motion_point(m, k / 64.0, p1); L += hypot(p1[0] - p0[0], hypot(p1[1] - p0[1], p1[2] - p0[2])); memcpy(p0, p1, sizeof p0); }
        return L;
    }
    return hypot(m->b[0] - m->a[0], hypot(m->b[1] - m->a[1], m->b[2] - m->a[2]));
}
int vf_moveinfo(char out[][2][128]) {
    int k = 0;
#define ROW(lab, ...) do { snprintf(out[k][0], 128, "%s", lab); snprintf(out[k][1], 128, __VA_ARGS__); k++; } while (0)
    if (!vf_on || !vfnm) return 0;
    const NcMotion *m = vf_pos ? &vfm[vf_pos - 1] : NULL;
    int oi = m ? m->op_index : vfm[0].op_index, opk = 0, nops = 0, last = -2;
    for (int i = 0; i < vfnm; i++) if (vfm[i].op_index != last) { last = vfm[i].op_index; nops++; if (last == oi && !opk) opk = nops; }
    ROW("Move ID", "%d of %d", vf_pos, vfnm);                              /* MoveStatsMoveIdFormat "{0} of {1}" */
    const char *ty = !m ? "" : m->kind == NCM_TOOL ? "" : m->kind == NCM_RAPID ? "Rapid Linear Move"
                   : (m->kind == NCM_CW || m->kind == NCM_CCW) ? (m->feed < 0 ? "Rapid Arc Move" : "Feed Arc Move") : "Feed Linear Move";
    ROW("Move Type", "%s", ty);                                             /* TypeToText.MoveTypeToText */
    const Op *op = oi >= 0 && oi < F.nops ? &F.ops[oi] : NULL;
    ROW("Operation Name", "%s", op ? (op->comment[0] ? op->comment : op->type) : "");   /* ours: the comment, else the type (mcverify's OperationName NOT DECODED) */
    if (op) ROW("Operation Number", "ID: %d (%d of %d)", op->id, opk, nops); else ROW("Operation Number", "%s", "");   /* MoveStatsOperationNumberFormat */
    const Tool *tl = m ? mcam_tool(&F, m->tool) : NULL;
    if (m && m->kind != NCM_TOOL) ROW("Tool Number", "#%d - %s", m->tool, tl ? tl->name : ""); else ROW("Tool Number", "%s", "");   /* ToolNumberFormat */
    ROW("Tool Orientation", "%s", "");                                      /* ours: not tracked by our motions */
    char x[3][48]; if (m) { for (int i = 0; i < 3; i++) vf_n3(x[i], sizeof x[i], m->b[i]); ROW("Tool Tip Position", "%s; %s; %s", x[0], x[1], x[2]); }
    else ROW("Tool Tip Position", "%s", "");                                /* VectorFormat "{0:N3}; {1:N3}; {2:N3}" */
    double fl = 0, rl = 0, ft = 0, lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
    for (int i = 0; i < vfnm; i++) {
        const NcMotion *q = &vfm[i]; if (q->kind == NCM_TOOL) continue;
        double L = vf_mlen(q); if (q->feed < 0) rl += L; else { fl += L; if (q->feed > 0) ft += L / q->feed; }
        for (int e = 0; e < 2; e++) for (int c = 0; c < 3; c++) { double v = e ? q->b[c] : q->a[c]; if (v < lo[c]) lo[c] = v; if (v > hi[c]) hi[c] = v; }
    }
    ROW("Feed Length", "%.3f", fl);
    char tt[96]; vf_time(tt, sizeof tt, ft * 60.0); ROW("Feed Time", "%s", tt);   /* TimeToText(FeedTime * 60), FeedTime in minutes */
    ROW("Min/Max X", "%.3f / %.3f", lo[0], hi[0]); ROW("Min/Max Y", "%.3f / %.3f", lo[1], hi[1]); ROW("Min/Max Z", "%.3f / %.3f", lo[2], hi[2]);
    ROW("Rapid Length", "%.3f", rl);
    ROW("Rapid Time", "%s", "");                                            /* the rapid rate is NOT DECODED (backplot.md: rapid time) */
    ROW("Total Length", "%.3f", fl + rl);
    ROW("Total Time", "%s", "");
    ROW("Coolant", "%s", ""); ROW("Compensation Type", "%s", ""); ROW("Compensation Direction", "%s", "");   /* ours: not passed */
    if (m && m->kind != NCM_TOOL && m->feed >= 0) ROW("Feed Rate", "%.3f", m->feed); else ROW("Feed Rate", "%s", "");
    const char *g = !m ? "" : m->kind == NCM_RAPID ? "G0" : m->kind == NCM_LINE ? "G1" : m->kind == NCM_CW ? "G2" : m->kind == NCM_CCW ? "G3" : "";
    ROW("NC Code", "%s", g);                                                /* "G" + McamMoveInfo.gCode (NCI code: 0..3 here) */
    if (m && m->kind != NCM_TOOL) ROW("Spindle Speed", "%.3f", (double)m->rpm); else ROW("Spindle Speed", "%s", "");
#undef ROW
    return k;
}
void vf_stop(void) {
    if (!vf_on) return;
    vf_free(vf); vf = NULL; free(vfm); vfm = NULL; vfnm = 0; vf_pos = 0;
    free(vf_opseq); free(vf_toolci); vf_opseq = vf_toolci = NULL; vf_nop = 0;
    for (int i = 0; i < vf_nsnap; i++) vf_snap_free(&vf_snaps[i]);
    free(vf_snaps); vf_snaps = NULL; free(vf_snap_m); free(vf_snap_w); vf_snap_m = vf_snap_w = NULL; vf_nsnap = vf_cap_snap = 0;
    memset(vf_tl_ok, 0, sizeof vf_tl_ok);
    vf_on = 0; vf_playing = 0; vf_run = 0; bp_tool_on = 0;
    free(vf_cmp_top); vf_cmp_top = NULL; vf_cmp_open = 0; vf_cmp_applied = 0; vf_cmp_nitems = 0; vf_cmp_sel = -1;
    solid_mode = vf_solid_was;
    show_tp = vf_show_was[0]; show_geom = vf_show_was[1]; show_axes = vf_show_was[2];   /* the Simulator's visibility toggles end */
    scene_set_verify(NULL, 0, NULL, 0, 0);
}
void vf_start(void) {
    if (vf_on) vf_stop();
    char only[1024] = { 0 }; int any = 0;
    for (int i = 0; i < F.nops && i < 1024; i++) if (op_selected(i)) { only[i] = 1; any = 1; }
    /* the Backplot rule (COpMgrTree::BackplotSelected @180352030): no selected op -> opmanres 3964 */
    if (!any) { cui_message("apprenticeCAM", "No operations were selected.  Please select at least one operation."); return; }
    if (dop_check(DOP_VERIFY)) return;                     /* BackplotSelected / VerifySelected -> DirtyOperationWarning (dialog 977, cui_dirty.c) */
    NcMotion *m; int n = nc_motions(&F, camops, ncamops, only, &m);
    if (n <= 0) { free(m); return; }
    double maxr = 0, mind = 1e300;
    for (int i = 0; i < n; i++) { const Tool *tl = mcam_tool(&F, m[i].tool); if (tl && tl->dia > 0) {
        if (tl->dia / 2 > maxr) maxr = tl->dia / 2; if (tl->dia < mind) mind = tl->dia; } }
    double ztop = 0; int have_top = 0;                  /* the selected ops' highest top of stock */
    for (int i = 0; i < F.nops && i < 1024; i++) if (only[i]) {
        CamOp *c = otx_camop(i); double tz;
        if (c) tz = c->p.top_z;
        else { CamOp t; char e[64]; if (!cam_op_from_file(&F, i, &t, e, sizeof e)) continue; tz = t.p.top_z;
            for (int ci = 0; ci < t.nchains; ci++) cam_chain_free(&t.chains[ci]);   /* stack CamOp: free the arrays only */
            free(t.chains); free(t.holes); free(t.m); }
        if (!have_top || tz > ztop) ztop = tz; have_top = 1;
    }
    double box[6];
    if (!vf_stock_box(&F, m, n, maxr, have_top ? ztop : NAN, box)) { free(m); return; }
    double cell = hypot(box[3] - box[0], box[4] - box[1]) / 400;      /* resolution: ours (NOT DECODED in Mastercam) */
    if (mind < 1e200 && mind / 20 > cell) cell = mind / 20;
    vf = vf_new(box[0], box[1], box[3], box[4], box[5], box[2], cell);
    vfm = m; vfnm = n; vf_pos = 0;
    vf_build_opseq();
    vf_on = 1; vf_newwin = 1; vf_playing = 0; vf_run = 0; vf_last_stop = 0; vf_dirty = 1;
    vf_solid_was = solid_mode; if (solid_mode) { solid_mode = 0; scene_force_mesh = 0; }
    vf_settings_load();                                 /* mastercamsimulatordefaults.x, at every launch (formats/simulator.md 5) */
    if (vf_set.tol_metric != (mcam_is_metric(&F) != 0)) {   /* UbvsSimulationSettingsProviderHelper.UpdateUnits: the tolerances to the job's units */
        double k = mcam_is_metric(&F) ? 25.4 : 1 / 25.4;
        for (int i = 0; i < vf_set.cmp_n; i++) if (isfinite(vf_set.cmp_end[i])) vf_set.cmp_end[i] *= k;
        vf_set.stock_to_leave *= k; vf_set.tol_metric = mcam_is_metric(&F) != 0;
    }
    vf_cmp_setup();
    vf_show_was[0] = show_tp; vf_show_was[1] = show_geom; vf_show_was[2] = show_axes;
    vf_apply_visibility();
    snprintf(status, sizeof status, "verify: %d motions, stock %.3f x %.3f x %.3f", n, box[3] - box[0], box[4] - box[1], box[5] - box[2]);
}
/* ---- the Simulator player (formats/simulator.md 4) ------------------------------------------------------------------
 * One "step" = one motion of vfm (UBVS: one move of the toolpath; ours: tool-change markers NCM_TOOL count as steps too).
 * vf_step: mwsimstreammanager MachineSimulationPlayer.rst_StandardPlay's loop body - DoNextStep, then the stop conditions
 * between the previous step context and the new one: operation change (pOperationIter differs, or no previous context),
 * tool change (ToolId differs / none before), X / Y / Z change (|new - old| > 0.001, MathUtil.IsNotEq(.., 0.001)), each only
 * when GetStopConditionsEnabled(). Returns the VF_STOP_* bits; -1 at the end of the toolpath. */
static int vf_step(void) {
    if (vf_pos >= vfnm) return -1;
    if (vf_pos == 0 || vfm[vf_pos].op_index != vfm[vf_pos - 1].op_index) vf_take_snap(vf_pos);
    vf_apply(vf_pos); vf_pos++;
    const NcMotion *n = &vfm[vf_pos - 1], *o = vf_pos >= 2 ? &vfm[vf_pos - 2] : NULL;
    int st = 0;
    if (!vf_set.stop_enabled) return 0;
    if (vf_set.stop_opchange && (!o || o->op_index != n->op_index)) st |= VF_STOP_OPCHANGE;
    if (vf_set.stop_toolchange && (!o || o->tool != n->tool)) st |= VF_STOP_TOOLCHANGE;
    if (o) {
        if (vf_set.stop_dx && fabs(n->b[0] - o->b[0]) > 0.001) st |= VF_STOP_XCHANGE;
        if (vf_set.stop_dy && fabs(n->b[1] - o->b[1]) > 0.001) st |= VF_STOP_YCHANGE;
        if (vf_set.stop_dz && fabs(n->b[2] - o->b[2]) > 0.001) st |= VF_STOP_ZCHANGE;
    }
    return st;
}
/* the end of the toolpath during a job: LoopSimulation restarts it (rst_StandardPlay: "if (!LoopSimulation || (num > 0 &&
   simMode != RUN_PLAY)) break; ResetSimulation ...; if (simMode == RUN_SINGLE_OPERATION) break;"). Returns 1 = go on. */
static int vf_at_end(int mode, int steps) {
    if (!vf_set.loop || (steps > 0 && mode != VF_RUN_PLAY)) return 0;
    vf_seek(0);
    return mode != VF_RUN_SINGLE_OP;
}
/* one job step with its break rule (rst_StandardPlay: RUN_SINGLE_STEP stops after one step, RUN_SINGLE_OPERATION when the
   operation changed, every mode on a stop condition). Returns 1 = the job goes on. */
static int vf_job_step(int mode, int *steps) {
    int st = vf_step();
    if (st < 0) return vf_at_end(mode, *steps);
    (*steps)++;
    int opch = vf_pos == 1 || vfm[vf_pos - 1].op_index != vfm[vf_pos - 2].op_index;
    if (mode == VF_RUN_STEP || (mode == VF_RUN_SINGLE_OP && opch) || st) { vf_last_stop = st; return 0; }
    return 1;
}
/* the speed regulator (ubvs.dll UBVS.Regulator.UpdateValues; base ModuleWorks Regulator(minSpeed 0, maxSpeed 20,
   initialSpeed 10)): k = min(TotalMoveCount * Multiplicator(1) * 2 / 3000, 1); moves/s = 5k + (3000k - 5k) / 400 * speed^2;
   frames/s = 20 - 0.75 * speed. Pacing: our wall-clock budget per timer tick (the Regulate / Flush / Render timing of the
   original is not copied - ours). */
double vf_moves_per_second(void) {
    double k = fmin((double)vfnm * 1.0 * 2.0 / 3000.0, 1.0), lo = 5.0 * k, hi = 3000.0 * k;
    return lo + (hi - lo) / 400.0 * (double)vf_set.speed * (double)vf_set.speed;
}
double vf_frames_per_second(void) { return 20.0 + -0.75 * (double)vf_set.speed; }
void vf_advance(void) {
    if (!vf_run || !vf_on) { vf_playing = 0; return; }
    double now = cui_clock(), dt = now - vf_last_adv;
    if (dt < 0.02) return;
    vf_last_adv = now;
    double mps = vf_moves_per_second();
    if (mps <= 0) return;                                          /* speed 0 with a tiny toolpath: k = 0 */
    int todo = (int)(dt * mps) + 1;
    double t0 = cui_clock();
    while (todo-- > 0) {
        if (!vf_job_step(vf_run, &vf_job_steps)) { vf_run = 0; break; }
        if ((vf_pos & 63) == 0 && cui_clock() - t0 > 0.030) break;   /* keep the UI live (ours) */
    }
    vf_playing = vf_run != 0;
    /* a new frame at most framePerSecond times a second while running (Regulator: millisecondsPerFrame), always at a stop */
    if (!vf_run || now - vf_last_render >= 1.0 / vf_frames_per_second()) { vf_dirty = 1; vf_last_render = now; }
}
/* start a job (MainWindowViewModel commands -> MachineSimulationPlayer): RUN_PLAY / RUN_SINGLE_OPERATION run paced by
   vf_advance; RUN_SINGLE_STEP and RUN_FASTFORWARD (Regulate(fastForward): no pacing) run here at once */
void vf_job(int mode) {
    if (!vf_on) return;
    vf_last_stop = 0;
    if (mode == VF_RUN_STEP || mode == VF_RUN_FASTFORWARD) {
        int steps = 0; vf_run = 0;
        while (vf_job_step(mode, &steps)) { }
        vf_playing = 0; vf_dirty = 1;
        return;
    }
    vf_run = mode; vf_playing = 1; vf_last_adv = cui_clock(); vf_last_render = 0; vf_job_steps = 0;
    if (vf_pos >= vfnm && !vf_set.loop) { vf_run = 0; vf_playing = 0; }      /* at the end without Loop: DoNextStep fails at once */
}
/* Previous Operation (ApplyPrevOperation: StepContextOperationIterator.MoveBack): inside an operation -> its first move,
   at its first move -> the first move of the previous operation that has moves; JumpTo without forward motion */
void vf_prev_op(void) {
    if (!vf_on || vf_pos <= 0) return;
    vf_run = 0; vf_playing = 0;
    int cur = vf_pos - 1, first = cur;                              /* the current move and its operation's first move */
    while (first > 0 && vfm[first - 1].op_index == vfm[cur].op_index) first--;
    int target = first;
    if (cur == first && first > 0) { target = first - 1; while (target > 0 && vfm[target - 1].op_index == vfm[first - 1].op_index) target--; }
    vf_seek(target + 1);                                            /* "at move m" = moves [0, m] applied */
}
void vf_sync_tool(void) {                         /* the tool at the current motion's end (scene globals of Backplot) */
    if (!vf_on || !vfnm) { bp_tool_on = 0; return; }
    const NcMotion *m = &vfm[vf_pos ? vf_pos - 1 : 0];
    bp_tool_pos[0] = m->b[0]; bp_tool_pos[1] = m->b[1]; bp_tool_pos[2] = m->b[2];
    bp_tool_num = m->tool;
    memset(&bp_tool_op, 0, sizeof bp_tool_op);
    if (!mcam_tool(&F, bp_tool_num)) {
        int oi = m->op_index; CamOp *c = oi >= 0 && oi < F.nops ? otx_camop(oi) : NULL;
        bp_tool_op.num = bp_tool_num;
        if (c) { bp_tool_op.dia = c->p.tool_dia; bp_tool_op.type = c->p.tool_type; bp_tool_op.corner_r = c->p.corner_r; bp_tool_op.tip_angle = c->p.tip_angle; }
        else if (oi >= 0 && oi < F.nops && F.ops[oi].raw) {
            const Op *o = &F.ops[oi]; double dd = 0; if (o->raw_n >= 0x358) memcpy(&dd, o->raw + 0x350, 8);
            bp_tool_op.dia = dd; bp_tool_op.type = otx_s16(o, 0x34c); }
    }
    bp_tool_on = vf_set.tool_vis != 0; bp_draw_cutter = 1; bp_draw_holder = 0;   /* Tool checkbox (RenderingSettings Tool) */
    bp_tool_shaded = 1;                                 /* ours: the Simulator draws a shaded tool */
    for (int i = 0; i < 3; i++) bp_tool_rgba[i] = vf_set.flute_rgb[i] / 255.f;   /* RenderingSettings MillingFluteColor (the cutting part) */
    bp_tool_rgba[3] = 1;
}
/* one frame of the window (mcamui draw_verify's start); vf_frame_still = without play (script steps) */
void vf_frame(void) { vf_advance(); vf_push_mesh(); vf_sync_tool(); }
void vf_frame_still(void) { vf_push_mesh(); vf_sync_tool(); }
/* the playback bar buttons (ubvs.dll mainwindow.baml, the GlassButton row; formats/simulator.md 3.3) in their order:
   Start (ResetCommand), Previous Operation (P), Step Backward (B), Play (R), Step Forward (S), Next Operation (N), End, Loop.
   ids 1..6 keep the old script numbering (vfbtn:ID): 1 Play/Stop, 2 stop, 3 step back, 4 step forward, 5 End, 6 Start;
   7 Next Operation, 8 Previous Operation, 9 Loop toggle */
const VfBtn VF_BTNS[8] = {
    { 0, "ubvs_ubvsstart16", "Start", 6 }, { 1, "ubvs_ubvsfirst16", "Previous Operation (P)", 8 },
    { 2, "ubvs_ubvsprevious16", "Step Backward (B)", 3 }, { 3, "ubvs_ubvsplay16", "Play (R)", 1 },
    { 4, "ubvs_ubvsnext16", "Step Forward (S)", 4 }, { 5, "ubvs_ubvslast16", "Next Operation (N)", 7 },
    { 6, "ubvs_ubvsend16", "End", 5 }, { 7, "ubvs_ubvsloop16", "Loop", 9 } };
void vf_btn(int id) {
    if (!vf_on) return;
    if (id == 1) { if (vf_run) { vf_run = 0; vf_playing = 0; }           /* ApplyPlayStop: IsRunning -> RequestStop, else Play */
                   else vf_job(VF_RUN_PLAY); }
    else if (id == 2) { vf_run = 0; vf_playing = 0; }
    else if (id == 3) { vf_run = 0; vf_playing = 0; vf_seek(vf_pos - 1); }   /* ApplyPrevStep: moves iterator MoveBack, JumpTo */
    else if (id == 4) { vf_run = 0; vf_job(VF_RUN_STEP); }                    /* ApplyNextStep: player.NextMove (RUN_SINGLE_STEP) */
    else if (id == 5) { vf_run = 0; vf_job(VF_RUN_FASTFORWARD); }             /* ApplyEnd: player.FastForward */
    else if (id == 6) { vf_run = 0; vf_playing = 0; vf_seek(0); }            /* ApplyReset: ResetThisViewModel */
    else if (id == 7) { vf_run = 0; vf_job(VF_RUN_SINGLE_OP); }              /* ApplyNextOperation: RUN_SINGLE_OPERATION */
    else if (id == 8) vf_prev_op();
    else if (id == 9) vf_set.loop = !vf_set.loop;                              /* LoopSimulation */
    vf_dirty = 1;
}
/* the playback keys: Mastercam passes them in McamStartupInfo.KeyBindings (m_stepForward / m_stepBackwards / m_runAndPause /
   m_nextStop / m_previousStop; the button tips name S / B / R / N / P); Home = Reset, Space = End (MainWindow.SetContext
   AddInputBinding(ResetCommand, Key 22 = Home), (EndCommand, Key 18 = Space)); Numpad + / - = speed (Keys 85 / 87) */
void vf_keys(int key) {
    if (!vf_on) return;
    if (key == 'S') vf_btn(4); else if (key == 'B') vf_btn(3); else if (key == 'R') vf_btn(1);
    else if (key == 'N') vf_btn(7); else if (key == 'P') vf_btn(8);
    else if (key == VF_KEY_HOME) vf_btn(6); else if (key == VF_KEY_SPACE) vf_btn(5);
    else if (key == '+') { if (vf_set.speed < VF_SPEED_MAX) vf_set.speed++; }     /* ApplyIncreaseSpeed */
    else if (key == '-') { if (vf_set.speed > VF_SPEED_MIN) vf_set.speed--; }     /* ApplyDecreaseSpeed */
}
void vf_slider_seek(double f) { if (vfnm <= 1) return; if (f < 0) f = 0; if (f > 1) f = 1; vf_run = 0; vf_playing = 0; vf_seek((int)lround(f * (vfnm - 1))); }
/* the position slider (mainwindow.baml: Slider Minimum 1, Maximum OverallToolpathLength, Value SliderValueOneBased,
   IsEnabled CanDragSlider): value V = V moves done (ours: V of our steps) */
void vf_slider_set(int v) { if (!vf_on) return; vf_run = 0; vf_playing = 0; vf_seek(v); }
double vf_slider_pos(void) { return vfnm > 1 ? (double)vf_pos / (vfnm - 1) : 0; }
/* the two status lines under the sliders (ours); returns 1 when the warning count is not 0 (drawn red) */
int vf_status(char *l1, size_t n1, char *l2, size_t n2) {
    const NcMotion *m = &vfm[vf_pos ? vf_pos - 1 : 0];
    const char *opn = m->op_index >= 0 && m->op_index < F.nops ? F.ops[m->op_index].type : "?";
    char tx[64], ty[64], tz[64];
    bp_num(tx, 64, m->b[0]); bp_num(ty, 64, m->b[1]); bp_num(tz, 64, m->b[2]);
    snprintf(l1, n1, "%s   X:%s  Y:%s  Z:%s   motion %d/%d", opn, tx, ty, tz, vf_pos, vfnm);
    snprintf(l2, n2, "removed %.2f %s, %d rapid-collision warning(s)", vf_volume_removed(vf), mcam_is_metric(&F) ? "mm3" : "in3", vf ? vf->nwarn : 0);
    return vf && vf->nwarn;
}
void vf_dump(void) {
    if (!vf_on) return;
    char a[300], b[300]; vf_status(a, sizeof a, b, sizeof b);
    printf("    verify %d/%d playing %d speed %d | %s | %s\n", vf_pos, vfnm, vf_playing, vf_set.speed, a, b);
}
/* script steps (as mcamui run_script): verify, vfseek:N, vfplay; ours: vfbtn:ID, vfslider:F, vfspeed:N (0..20), vfkey:K, vfset:NAME=V,
   vfmoves (the Move List), vfclose */
int cui_vf_script(const char *st, const char *arg) {
#define IS(x) (!strcmp(st, x))
    if (IS("verify")) { vf_start(); if (vf_on) vf_frame_still(); return 1; }                  /* Verify the selection */
    if (IS("vfseek") && arg) { if (vf_on) { vf_seek(atoi(arg)); printf("    verify at motion %d/%d, %d warning(s)\n", vf_pos, vfnm, vf->nwarn); vf_frame_still(); } return 1; }
    if (IS("vfplay")) { if (vf_on) vf_job(VF_RUN_PLAY); return 1; }
    if (!vf_on) return 0;
    if (IS("vfbtn") && arg) { vf_btn(atoi(arg)); vf_frame_still(); return 1; }
    if (IS("vfslider") && arg) { vf_slider_seek(atof(arg)); vf_frame_still(); return 1; }
    if (IS("vfspeed") && arg) { int v = atoi(arg); vf_set.speed = v < VF_SPEED_MIN ? VF_SPEED_MIN : v > VF_SPEED_MAX ? VF_SPEED_MAX : v; return 1; }
    if (IS("vfwait")) { int guard = 0; while (vf_run && guard++ < 10000000) { if (!vf_job_step(vf_run, &vf_job_steps)) vf_run = 0; }   /* ours (tests): the running job to its stop, unpaced */
                        vf_playing = 0; vf_dirty = 1; printf("    verify job done at motion %d/%d, stop bits %d\n", vf_pos, vfnm, vf_last_stop); vf_frame_still(); return 1; }
    if (IS("vfkey") && arg) { vf_keys(!strcmp(arg, "Home") ? VF_KEY_HOME : !strcmp(arg, "Space") ? VF_KEY_SPACE : arg[0]); vf_frame_still(); return 1; }   /* ours: the playback keys */
    if (IS("vfset") && arg) { if (!vf_set_named(arg)) printf("    (vfset %s: no such setting)\n", arg); vf_frame_still(); return 1; }   /* ours: vfset:NAME=V (the ribbon toggles) */
    if (IS("vfcolors")) { vf_colors_dump(); return 1; }
    if (IS("vfmoves")) { char l[VF_MI_N][2][128]; int n = vf_moveinfo(l); for (int i = 0; i < n; i++) printf("    move list %s = %s\n", l[i][0], l[i][1]); return 1; }
    if (IS("vfclose")) { vf_stop(); return 1; }
    /* ours (tests): the Compare tool window - vfcmp:show|hide|refresh|sel=N|stl=PATH|tol=V|opt=V|end=ROW,V|start=ROW,V|dump */
    if (IS("vfcmp") && arg) {
        if (!strcmp(arg, "show")) vf_cmp_show(1); else if (!strcmp(arg, "hide")) vf_cmp_show(0);
        else if (!strcmp(arg, "refresh")) vf_cmp_refresh();
        else if (!strncmp(arg, "sel=", 4)) vf_cmp_select(atoi(arg + 4));
        else if (!strncmp(arg, "stl=", 4)) { if (!vf_cmp_add_stl(arg + 4)) printf("    (vfcmp: cannot read %s)\n", arg + 4); }
        else if (!strncmp(arg, "tol=", 4)) vf_cmp_set_stl(atof(arg + 4));
        else if (!strncmp(arg, "opt=", 4)) vf_cmp_options(atof(arg + 4));
        else if (!strncmp(arg, "end=", 4) || !strncmp(arg, "start=", 6)) { const char *q = strchr(arg, '=') + 1; int r = atoi(q); const char *c = strchr(q, ',');
            if (c) vf_cmp_set_value(r, arg[0] == 's', atof(c + 1)); }
        vf_cmp_dump(); vf_frame_still(); return 1;
    }
    return 0;
#undef IS
}
