/* cui_backplot.c - the classic Backplot's UI model (formats/backplot.md), moved out of viewer/mcamui.c (backplot section)
 * and viewer/backplot_ui.inc (Options dialog 1228, Operation List 17000, Details / Info pages) in phase 8 of
 * formats/qt_port_plan.md. Names and citations as they were there; both front ends draw this model (mcamui: nuklear,
 * viewer-qt: Qt widgets). The engine (position, stepping, stops, Info sums) is viewer/backplot.c.
 *
 * Classic Backplot: dialog 14000 "Backplot" (collapsed 139 px + caption, 14001 expands to >= 500, 16000 ToolpathControl at
 * y = 30: toggles 31001-31007 26 x 26 at x 0 / 26 / 52 / 78 / 104 / 130 / 164, row 2 at y = 25, OK / Help bottom right, tree
 * 17000 + info area 12000 (tabs Details 13000 / Info 4006) from y = 60) and the VCR bar 15000 (30 px strip at the top of the
 * graphics view, 24 x 24 buttons). Motions = what the post outputs (nc_motions). Options 31007 = dialog 1228 (13.1), motion
 * colours (13.2), endpoints (13.3), tool look / spin (13.4), Details pages display-only (13.7), tree Path level (13.8).
 * NOT DONE / NOT DECODED here (left out / greyed): Save as geometry 16001 / tool geometry 16007, Isolate 16003 / path view
 * 16004 (no-ops in Mastercam too), the 14000 dialog's screen position (over the Operations Manager here), quick verify
 * (13.5), the wire Options dialog 1229 (no wire operations here), rapid time (the machine record's axis rapid rates, 13.6),
 * the splitting of an operation into paths (13.8). */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "paths.h"
#include "core_ui.h"
#include "opcodes.h"

/* optree_text.inc's helpers (the op's own tool data): a stored op's record short, the op made here with that index */
static int otx_s16(const Op *o, int off) { return o->raw && off + 2 <= (int)o->raw_n ? (int16_t)(o->raw[off] | o->raw[off + 1] << 8) : 0; }
static CamOp *otx_camop(int i) { for (int k = 0; k < ncamops; k++) if (camops[k]->op_index == i) return camops[k]; return NULL; }

/* a monotonic clock in seconds (mcamui used SDL_GetTicks() / 1000: only differences are used - play, spin) */
double cui_clock(void) {
#ifdef _WIN32
    return (double)clock() / CLOCKS_PER_SEC;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

int bp_on; Backplot bpl;
/* [BACKPLOT] config = the mccore bp_* globals (formats/backplot.md 12.1; factory defaults FUN_1803ba450); edited in memory by
   the Options dialog (13.1) - the config file is never written here; read by cui_config_item (1000, 19000-19040) */
BpOptions bp_opt = BP_OPT_FACTORY;
int bpo_open;                                            /* Backplot Options (modal) is open */
int bp_exp, bp_tab, bp_show_cs, bp_new;
static float *bp_fv, *bp_rv; static int *bp_fend, *bp_rend;   /* motion vertices (feed / rapid) and counts after each motion */
int bp_playing, bp_speed_pos = 60; double bp_last_adv;
double bp_exp_h = 500;
char bp_cs_txt[4][32];
int bp_tr_grp_open, bp_tr_sel = -1; char bp_tr_op_open[1024];   /* Operation List: expanded items, selection */
static float *bp_epv; static double bp_spin_t;              /* endpoint vertices (13.3); spin clock (13.4) */

void bp_tree_init(void) { bp_tr_grp_open = 1; bp_tr_sel = -1; memset(bp_tr_op_open, 0, sizeof bp_tr_op_open); }   /* root expanded (FUN_66d2c4f0) */
void bp_stop(void) { if (bp_on) bp_free(&bpl); bp_on = 0; bp_tool_on = 0; bp_lines_on = 0; bp_playing = 0; bpo_open = 0; scene_reupload(); }
void bp_start(void) {
    if (bp_on) bp_stop();
    char only[1024] = { 0 }; int any = 0;
    for (int i = 0; i < F.nops && i < 1024; i++) if (op_selected(i)) { only[i] = 1; any = 1; }
    /* COpMgrTree::BackplotSelected @180352030: no selected op -> post_warning(opmanres 3964) */
    if (!any) { cui_message("apprenticeCAM", "No operations were selected.  Please select at least one operation."); return; }
    if (dop_check(DOP_BACKPLOT)) return;                     /* BackplotSelected / VerifySelected -> DirtyOperationWarning (dialog 977, cui_dirty.c) */
    NcMotion *m; int n = nc_motions(&F, camops, ncamops, only, &m);
    if (n <= 0) { free(m); return; }                     /* no motion: Mastercam's handling NOT DECODED, nothing said */
    bp_init(&bpl, m, n); bp_on = 1; bp_exp = 0; bp_tab = 0; bp_new = 1; bp_playing = 0;
    bpl.interp = bp_opt.interp; bpl.interp_dist = bp_opt.interp_dist;             /* bp_interpolate_steps / _distance */
    bp_speed_pos = bp_opt.speed ? bp_opt.speed : 75;                               /* 15006: a stored 0 becomes 75 */
    bp_build_lines();
    bp_step(&bpl, BP_FIRST, 0);
    bp_tree_init();
}
void bp_toggle(int byte) { bpl.flags[byte] = !bpl.flags[byte]; if (byte == 8 && bpl.flags[8]) bpl.flags[7] = 1;   /* holder on -> tool on */
    if (byte == 0) bp_build_lines(); }
static void bp_play_toggle(void) {                   /* Play: a running timer = stop (pause); at the end -> rewind */
    if (bp_playing) bp_playing = 0;
    else { if (bpl.pos >= bpl.vis1 - 1 || fabs(bpl.cum[bpl.vis1] - bpl.dist) <= 1e-4) bp_step(&bpl, BP_FIRST, 0);
           bp_playing = 1; bp_last_adv = cui_clock(); }
}
void bp_keys(int key) {                              /* VCR accelerators (FUN_66d1ed30): S B N P R */
    if (!bp_on) return;
    if (key == 'S') bp_step(&bpl, BP_FWD, 1); else if (key == 'B') bp_step(&bpl, BP_BACK, 1);
    else if (key == 'N') bp_step(&bpl, BP_NEXT_STOP, 0); else if (key == 'P') bp_step(&bpl, BP_PREV_STOP, 0);
    else if (key == 'R') { if (bp_playing) bp_playing = 0; else { if (bpl.pos >= bpl.vis1 - 1) bp_step(&bpl, BP_FIRST, 0); bp_playing = 1; bp_last_adv = cui_clock(); } }
}
/* tool / holder look (13.4): mill_tool_appearance 2 = Shaded (lit), 0 Plain / 1 Fluted = the same polygons as lines;
   colour Wd_colors[bp_tool_color] / [bp_holder_color], alpha = bp_opacity_active (blending only when shaded). With
   "Material" the colour word holds (material + 1) << 24 - how a material renders is NOT DECODED, the colour index is
   drawn. Spin (bp_spin_tool): 360 deg per second of wall-clock time about the tool axis; the sign comes from tool
   entity +0xa8 (NOT DECODED here: not negated). */
void bp_sync_tool(void) {                            /* tool drawn at the position when display flag 7 (tool) or 8 (holder) */
    if (!bp_on) { bp_tool_on = 0; return; }
    bp_point(&bpl, bp_tool_pos); bp_tool_num = bpl.m[bpl.pos].tool; bp_tool_on = bpl.flags[7] || bpl.flags[8];
    memset(&bp_tool_op, 0, sizeof bp_tool_op);       /* no such part tool: the op's own tool data (FUN_67d1a2e0 reads the op's) */
    if (!mcam_tool(&F, bp_tool_num)) {
        int oi = bpl.m[bpl.pos].op_index; CamOp *c = oi >= 0 && oi < F.nops ? otx_camop(oi) : NULL;
        bp_tool_op.num = bp_tool_num;
        if (c) { bp_tool_op.dia = c->p.tool_dia; bp_tool_op.type = c->p.tool_type; bp_tool_op.corner_r = c->p.corner_r; bp_tool_op.tip_angle = c->p.tip_angle; }
        else if (oi >= 0 && oi < F.nops && F.ops[oi].raw) {   /* op +0x2e0 tp_tool: +8 dia (record 0x350), +4 type (0x34c) */
            const Op *o = &F.ops[oi]; double dd = 0; if (o->raw_n >= 0x358) memcpy(&dd, o->raw + 0x350, 8);
            bp_tool_op.dia = dd; bp_tool_op.type = otx_s16(o, 0x34c); }
    }
    bp_draw_cutter = bpl.flags[7]; bp_draw_holder = bpl.flags[8];          /* FUN_66d35aa0: tool iff flag 7, holder iff flag 8 */
    bp_tool_shaded = bp_opt.tool_appearance >= 2;                          /* ModelVisualOptions +0xc = (appearance < 2) unlit */
    mc_col(bp_opt.tool_color, bp_tool_rgba); mc_col(bp_opt.holder_color, bp_holder_rgba);
    bp_tool_rgba[3] = bp_holder_rgba[3] = (float)bp_opt.opacity_active;    /* FUN_66d3c2f0 / FUN_66d3bd80: opacity x 255 */
    double now = cui_clock();
    if (bp_opt.spin_tool && bp_tool_on) bp_spin_deg = fmod(bp_spin_deg + (bp_spin_t > 0 ? now - bp_spin_t : 0) * 360.0, 360.0);
    bp_spin_t = now;
}
/* the motions as line vertices (12.3 / 13.2): rapids always in bp_rapid_color; feed moves (lines and arcs - the arc
   handler FUN_66da4920 also uses +0x28) in bp_linear_color with colour codes on, otherwise in their path record's colour
   (its writer is NOT DECODED - the toolpath colour, config 5045, is used). bp_cwarc / bp_ccwarc_color have no reader. */
void bp_build_lines(void) {
    free(bp_fv); free(bp_rv); free(bp_fend); free(bp_rend); bp_fv = bp_rv = NULL; bp_fend = bp_rend = NULL;
    int nf = 0, nr = 0, cf = 0, cr = 0;
    bp_fend = xcalloc((size_t)bpl.n + 1, sizeof(int)); bp_rend = xcalloc((size_t)bpl.n + 1, sizeof(int));
    float rc[3], fc[3]; mc_col(bp_opt.rapid_color, rc); mc_col(bpl.flags[0] ? bp_opt.linear_color : cfg_feed_color, fc);
    for (int i = 0; i < bpl.n; i++) {
        const NcMotion *m = &bpl.m[i];
        int rap = m->kind == NCM_RAPID, segs = m->kind == NCM_CW || m->kind == NCM_CCW ? (int)fmax(2, ceil(fabs(m->sweep) / (M_PI / 24))) : m->kind == NCM_DRILL ? 2 : m->kind == NCM_TOOL ? 0 : 1;
        float **v = rap ? &bp_rv : &bp_fv; int *n = rap ? &nr : &nf, *cap = rap ? &cr : &cf; const float *col = rap ? rc : fc;
        for (int k = 0; k < segs; k++) {
            double p0[3], p1[3];
            nc_motion_point(m, (double)k / segs, p0); nc_motion_point(m, (double)(k + 1) / segs, p1);
            if (*n + 2 > *cap) { *cap = *cap ? *cap * 2 : 4096; *v = xrealloc(*v, (size_t)*cap * 24); }
            float *q = *v + *n * 6;
            q[0] = (float)p0[0]; q[1] = (float)p0[1]; q[2] = (float)p0[2]; q[3] = col[0]; q[4] = col[1]; q[5] = col[2];
            q[6] = (float)p1[0]; q[7] = (float)p1[1]; q[8] = (float)p1[2]; q[9] = col[0]; q[10] = col[1]; q[11] = col[2];
            *n += 2;
        }
        bp_fend[i + 1] = nf; bp_rend[i + 1] = nr;
    }
}
/* the drawn portion: Trace = the whole (visible) path, Run = from its start up to the current motion; rapids only with
   display flag 4 (12.3). Screen cleanup on operation / tool change (bp_cleanup_scrn_on_op_change / _tl_change,
   FUN_66d77b70) limits it to the current operation / tool. Endpoints (flag 2): the end point of every drawn motion
   that is not suppressed (FUN_66da6f10, 13.3). */
void bp_set_lines(void) {
    if (!bp_on) { bp_lines_on = 0; bp_nep = 0; return; }
    int a = bpl.vis0, e = bpl.flags[0xd] ? bpl.pos + 1 : bpl.vis1;
    if (bp_opt.cleanup_op || bp_opt.cleanup_tl) {
        int p = bpl.pos, lo = p, hi = p + 1;
        if (bp_opt.cleanup_op) { while (lo > 0 && bpl.m[lo - 1].op_index == bpl.m[p].op_index) lo--;
                                 while (hi < bpl.n && bpl.m[hi].op_index == bpl.m[p].op_index) hi++; }
        if (bp_opt.cleanup_tl) { int l2 = p, h2 = p + 1;           /* from the last tool change up to the next one */
            while (l2 > 0 && bpl.m[l2].kind != NCM_TOOL) l2--;
            while (h2 < bpl.n && bpl.m[h2].kind != NCM_TOOL) h2++;
            if (!bp_opt.cleanup_op) { lo = l2; hi = h2; } else { if (l2 > lo) lo = l2; if (h2 < hi) hi = h2; } }
        if (lo > a) a = lo; if (hi < e) e = hi; if (e < a) e = a;
    }
    bp_lines_on = 1;
    bp_lv[0] = bp_fv + (size_t)bp_fend[a] * 6; bp_ln[0] = bp_fend[e] - bp_fend[a];
    bp_lv[1] = bp_rv + (size_t)bp_rend[a] * 6; bp_ln[1] = bpl.flags[4] ? bp_rend[e] - bp_rend[a] : 0;
    bp_nep = 0; bp_ep = NULL;
    if (bpl.flags[2] && e > a) {
        bp_epv = xrealloc(bp_epv, sizeof(float) * 3 * (size_t)(e - a));
        for (int i = a; i < e; i++) {
            const NcMotion *m = &bpl.m[i];
            if (m->kind == NCM_TOOL || (m->kind == NCM_RAPID && !bpl.flags[4])) continue;   /* suppressed: no endpoint */
            bp_epv[bp_nep * 3] = (float)m->b[0]; bp_epv[bp_nep * 3 + 1] = (float)m->b[1]; bp_epv[bp_nep * 3 + 2] = (float)m->b[2]; bp_nep++;
        }
        bp_ep = bp_epv;
    }
}
/* Play (section 2.4, FUN_66d84490): distance per update from elapsed time and speed; u = model length of one pixel */
void bp_advance(void) {
    if (!bp_playing || !bp_on) return;
    double now = cui_clock(), dt = now - bp_last_adv;
    if (dt < 0.02) return;                                   /* max 50 updates/s */
    bp_last_adv = now;
    double L = bpl.total, c = bpl.dist, u = view_h > 0 ? vht / view_h : 0;
    double maxStep = fmin(u * 50000, (dt / 5) * L), minStep = u * 5 * dt;
    if (maxStep < minStep) minStep = maxStep;
    int speed = bp_speed_pos; if (speed == 0) return;
    double f = (1 - (log(101 - speed) * log(101 - speed) / (log(10) * log(10))) * 0.25) * (speed / 100.0);
    double step = f * (maxStep - minStep) + minStep;
    if (step <= 1e-8) return;
    double end = bpl.cum[bpl.vis1];
    if (c + step <= end) bp_seek_dist(&bpl, c + step); else { bp_seek_dist(&bpl, end); bp_playing = 0; }
}
/* one frame of the VCR bar (mcamui draw_bp_vcr's end): play, the drawn portion, the tool; bp_frame_still = without play (the
   script steps: mcamui runs its script before the first frame, so nothing plays between steps) */
void bp_frame(void) { bp_advance(); bp_set_lines(); bp_sync_tool(); }
void bp_frame_still(void) { bp_set_lines(); bp_sync_tool(); }

/* the VCR bar 15000 (section 2.1): OnInitDialog FUN_66d1f330 bitmaps / WM_SIZE FUN_66d1ff40 x positions (y = height / 2
   - 12; the second group only when width > height); tooltips = the accelerators' letters (FUN_66d1ed30) */
const BpVcrBtn BP_VCR[BP_NVCR] = {
    { 30001, 0, "backplotres_BMP_29348", "Play (R)" }, { 30002, 24, "backplotres_BMP_29345", "Stop" },
    { 30003, 48, "backplotres_BMP_29350", "Previous stop (P)" }, { 30004, 72, "backplotres_BMP_29349", "Step back (B)" },
    { 30005, 96, "backplotres_BMP_29346", "Step forward (S)" }, { 30006, 120, "backplotres_BMP_29347", "Next stop (N)" },
    { 15002, 150, "backplotres_BMP_28002", "Trace mode" }, { 15009, 174, "backplotres_BMP_29358", "Run mode" },
    { 15007, -48, "backplotres_BMP_29360", "Set conditional stops" }, { 15008, -24, "backplotres_BMP_29361", "Help" } };
int bp_vcr_checked(int id) { return (id == 30001 && bp_playing) || (id == 15002 && bpl.flags[0xd] == 0) || (id == 15009 && bpl.flags[0xd] != 0); }
int bp_vcr_enabled(int id) { return id != 15008; }      /* Help (get_dialog_help 0xb, 15000): not ported */
void bp_vcr(int id) {
    if (!bp_on) return;
    if (id == 30001) bp_play_toggle();
    else if (id == 30002) bp_playing = 0;
    else if (id == 30003) bp_step(&bpl, BP_PREV_STOP, 0); else if (id == 30004) bp_step(&bpl, BP_BACK, 1);
    else if (id == 30005) bp_step(&bpl, BP_FWD, 1); else if (id == 30006) bp_step(&bpl, BP_NEXT_STOP, 0);
    else if (id == 15002) bpl.flags[0xd] = 0;
    else if (id == 15009) bpl.flags[0xd] = 1;
    else if (id == 15007) bp_cs_open();
}
/* position slider 15001: 0 .. visible motions - 1 (section 2.2); f = 0..1 along it */
void bp_slider_seek(double f) {
    int nv = bpl.vis1 - bpl.vis0; if (nv <= 0) return;
    if (f < 0) f = 0; if (f > 1) f = 1;
    bp_seek_motion(&bpl, bpl.vis0 + (int)lround(f * (nv - 1)));
}
double bp_slider_pos(void) { int nv = bpl.vis1 - bpl.vis0, cur = bpl.pos - bpl.vis0; return nv > 1 ? (double)cur / (nv - 1) : 0; }
double bp_stop_tick(int k) { int nv = bpl.vis1 - bpl.vis0; return nv > 1 ? (double)(bpl.stops[k] - bpl.vis0) / (nv - 1) : 0; }

/* Conditional Stops (dialog 6000, section 3) */
void bp_cs_open(void) {
    bp_show_cs = 1;
    snprintf(bp_cs_txt[0], 32, "%u", bpl.cs.step); snprintf(bp_cs_txt[1], 32, "%.4f", bpl.cs.x);
    snprintf(bp_cs_txt[2], 32, "%.4f", bpl.cs.y); snprintf(bp_cs_txt[3], 32, "%.4f", bpl.cs.z);
}
void bp_cs_at_ops(int on) { bpl.cs.at_ops = on; if (on) bpl.cs.at_toolchange = 1; }   /* forces toolchange on */
void bp_cs_ok(void) {
    BpStops *cs = &bpl.cs;
    cs->step = (unsigned)atoi(bp_cs_txt[0]); cs->x = atof(bp_cs_txt[1]); cs->y = atof(bp_cs_txt[2]); cs->z = atof(bp_cs_txt[3]);
    bp_compute_stops(&bpl); bp_show_cs = 0;
}

/* the 16000 ToolpathControl buttons (section 1.4, OnInitDialog FUN_66d182c0): the 7 toggles 26 x 26 at y = 0 of the control
   (x below), row 2 (FUN_66d19410) x from the right edge (w = 26), 16002 at its template x 76 du */
const BpTgl BP_TOGGLES[7] = {
    { 31001, 0, 0, "backplotres_BMP_31001", "Display with color codes" }, { 31002, 7, 26, "backplotres_BMP_31002", "Display tool" },
    { 31003, 8, 52, "backplotres_BMP_31003", "Display holder" }, { 31004, 4, 78, "backplotres_BMP_31004", "Display rapid moves" },
    { 31005, 2, 104, "backplotres_BMP_31005", "Display endpoints" }, { 31006, 0xb, 130, "backplotres_BMP_31006", "Quick verify" },
    { 31007, -1, 164, "backplotres_BMP_29354", "Options" } };
int bp_tgl_enabled(int id) { return id != 31006; }      /* quick verify's geometry is NOT DECODED (13.5): greyed */
void bp_tgl(int id) {
    for (int k = 0; k < 7; k++) if (BP_TOGGLES[k].id == id) { if (BP_TOGGLES[k].byte >= 0) bp_toggle(BP_TOGGLES[k].byte); else bp_options_open(); }   /* 31007: FUN_66d17fa0 -> BackplotOptions */
}
/* row 2: 16010 Restrict drawing, 16011 Turn off restricted drawing (FUN_66d19410 positions); 16007 / 16001 NOT DONE; 16002
   Add point -> the Details tab */
void bp_row2(int id) {
    if (id == 16010) bp_set_visible(&bpl, bpl.pos, bpl.n);
    else if (id == 16011) bp_set_visible(&bpl, 0, bpl.n);
    else if (id == 16002) bp_tab = 0;
}

/* ------------------------------------------------- Backplot Options: mccore BackplotOptions, CBackplotDlg dialog 1228
   (formats/backplot.md 13.1). Modal (DoModal). Controls at their template positions; bindings, enable rules (update-controls
   FUN_180707280), validation and OK (FUN_1807051d0) as decoded. Not here: the wire dialog 1229 (isWireOpAtPosition is never
   true - there are no wire operations), Help (MCDialog help, greyed as elsewhere). */
BpOptions bpo;                                     /* the dialog's DDX members (working copy of the globals) */
int bpo_use_verify;                                /* 3351 "Use Verify color" = !bp_verify_use_motion_color */
int *bpo_pick;                                     /* the member a Select button's colour picker writes */
static const int bpo_mat_count = 0;   /* material combos: gl_InitMaterialList / gl_mat_list names are NOT DECODED -> empty */
void bpo_update(void) {        /* update-controls FUN_180707280: the rules that change values */
    const int gs = 1;                 /* graphics_support: forced to 1 for the backplot session (section 1.1) */
    if (gs != 1 && bpo.tool_appearance == 2) bpo.tool_appearance = 0;              /* a stored Shaded becomes Plain */
    if (bpo.tool_appearance != 2) bpo.tool_material_on = bpo.holder_material_on = 0;   /* Material only when Shaded */
    if (bpo.cleanup_op) bpo.cleanup_tl = 1;                                         /* Operation change forces Tool change */
}
void bp_options_open(void) {
    /* backplot.dll FUN_66d19c90 first sets bp_cc_in_control_enabled / _radius from the first multiaxis operation (a wire
       operation first: disabled); there are no multiaxis operations here, so it stays disabled */
    bp_opt.cc_in_control_enabled = 0;
    if (bp_opt.cleanup_op) bp_opt.cleanup_tl = 1;                                   /* OnInitDialog FUN_180705ab0 (globals) */
    if (bp_opt.tool_material < 0 || bp_opt.tool_material >= bpo_mat_count) bp_opt.tool_material = 0;
    if (bp_opt.holder_material < 0 || bp_opt.holder_material >= bpo_mat_count) bp_opt.holder_material = 0;
    bpo = bp_opt; bpo_use_verify = !bp_opt.verify_use_motion_color;
    bpo_update(); bpo_open = 1;
}
void bpo_ok(void) {            /* IDOK FUN_1807051d0: UpdateData(TRUE), then every global of the table */
    double oa = bp_opt.opacity_active, oi = bp_opt.opacity_inactive;   /* the opacity edits / spins wrote these already */
    int ce = bp_opt.cc_in_control_enabled;
    bp_opt = bpo; bp_opt.opacity_active = oa; bp_opt.opacity_inactive = oi; bp_opt.cc_in_control_enabled = ce;
    bp_opt.verify_use_motion_color = !bpo_use_verify;
    bpo_open = 0;
    bpl.interp = bp_opt.interp; bpl.interp_dist = bp_opt.interp_dist;   /* backplot.dll: reload ModelVisualOptions (FUN_66d76180) and apply */
    bp_build_lines();
}
void bpo_cancel(void) { bpo_open = 0; }   /* Cancel / X: nothing written (except the opacity, which the edits / spins wrote at once) */
void bpo_color_done(int c) { if (bpo_pick) *bpo_pick = c; bpo_pick = NULL; bpo_update(); }
/* an edit: MCByte (DDX_ProcessEditByte: EvalToDouble, 0..255, else ClueWnd "%u to %u" and the old value back),
   MCInt / MCReal (UIEditInt / UIEditReal::Validate NOT DECODED: the +0xbf0 / +0xbf4 fields are taken as min / max as the
   spec infers, and a rejected value keeps the old one), opacity (FUN_180706820 / FUN_180706a00: writes the global at once).
   Expressions (EvalToDouble) are NOT DECODED here: plain numbers. Number format of MCReal NOT DECODED: n_places. */
void bpo_edit_text(int kind, const void *val, char *s, size_t n) {
    if (kind == BPE_BYTE || kind == BPE_INT) snprintf(s, n, "%d", *(const int *)val);
    else if (kind == BPE_REAL) bp_num(s, n, *(const double *)val);
    else { int k = (int)lround(*(const double *)val / 0.01); if (k < 1 || k > 100) k = 50; snprintf(s, n, "%.2f", k * 0.01); }   /* init: n = round(v / 0.01), outside 1..100 -> 50 */
}
int bpo_edit_commit(int kind, void *val, const char *s, double lo, double hi, char *clue, size_t nclue) {
    int shown = 0;
    char *e; double d = strtod(s, &e); while (*e == ' ') e++;
    int ok = e != s && !*e && isfinite(d);
    if (kind == BPE_BYTE) { if (ok && d >= 0 && d <= 255) *(int *)val = (int)d;
                            else { snprintf(clue, nclue, "%u to %u", 0u, 255u); shown = 1; } }
    else if (kind == BPE_INT) { if (ok && d >= lo && d <= hi) *(int *)val = (int)d; }
    else if (kind == BPE_REAL) { if (ok && d >= lo) *(double *)val = d; }
    else { double *g = val; if (ok && fabs(d - *g) > 0.001) { int n = (int)lround(d / 0.01); if (n >= 1 && n <= 100) *g = n * 0.01; } }
    bpo_update();
    return shown;
}
void bpo_spin(double *g, int up) {        /* up-down 359 / 363: range 1..100, writes the global */
    int n = (int)lround(*g / 0.01); if (n < 1 || n > 100) n = 50;
    if (up && n < 100) *g = (n + 1) * 0.01;
    if (!up && n > 1) *g = (n - 1) * 0.01;
}
/* dialog 1228's controls in mcamui's drawing order (template DLU positions: backplotres dialog 1228, formats/backplot.md 13.1).
   t: g group box, s static, r radio, c check, e edit, k colour row (edit at x, swatch at x2, Select (UIBitmapButton 0x61d2)
   at x3; edit 20 x 14, swatch 20 x 14, button 16 x 14), m material combo (empty), p spin, b push button, i bitmap */
const BpoCtl BPO_CTLS[] = {
    { 'g', 0, 7, 7, 134, 331, 0, 0, "General Settings" }, { 'g', 0, 13, 17, 122, 67, 0, 0, "Step mode" },
    { 's', BPK_FAST_T, 19, 30, 60, 14, 0, 0, "Fast steps/sec:" }, { 'e', BPK_FAST, 85, 28, 40, 14, 0, 0, NULL },
    { 'r', BPK_ENDPTS, 20, 43, 55, 10, 0, 0, "Endpoints" }, { 'r', BPK_INTERP, 20, 55, 55, 10, 0, 0, "Interpolate" },
    { 's', BPK_INC_T, 19, 67, 60, 14, 0, 0, "Step increment:" }, { 'e', BPK_INC, 85, 65, 40, 14, 0, 0, NULL },
    { 'g', 0, 13, 88, 122, 42, 0, 0, "Cleanup screen on" },
    { 'c', BPK_CL_OP, 19, 102, 109, 10, 0, 0, "Operation change" }, { 'c', BPK_CL_TL, 19, 114, 109, 10, 0, 0, "Tool change" },
    { 'c', BPK_FIT, 13, 221, 122, 10, 0, 0, "Fit Toolpath" }, { 'c', BPK_HOME_L, 13, 233, 122, 10, 0, 0, "Show Lathe Home Position" },
    { 'c', BPK_CAXIS, 13, 245, 122, 10, 0, 0, "Show C-Axis tools in top orientation" }, { 'c', BPK_HOME_M, 13, 257, 122, 10, 0, 0, "Show Mill / Router Home Position" },
    { 'c', BPK_SIMCC, 13, 269, 122, 10, 0, 0, "Simulate Cutter Compensation" }, { 'c', BPK_SIMAX, 13, 309, 122, 10, 0, 0, "Simulate Axis Substitution" },
    { 'c', BPK_SIMROT, 13, 321, 122, 10, 0, 0, "Simulate Rotary Axis" },
    { 'c', BPK_CC_CTL, 13, 281, 122, 10, 0, 0, "Cutter comp in control" }, { 's', BPK_CC_RAD_T, 24, 295, 42, 12, 0, 0, "Tool Radius:" },
    { 'e', BPK_CC_RAD, 69, 292, 40, 14, 0, 0, NULL },
    { 'g', 0, 147, 7, 259, 331, 0, 0, "Appearance" }, { 'g', 0, 153, 17, 247, 78, 0, 0, "Tool" },
    { 'r', BPK_PLAIN, 159, 28, 44, 10, 0, 0, "Plain" }, { 'r', BPK_FLUTED, 159, 41, 44, 10, 0, 0, "Fluted" }, { 'r', BPK_SHADED, 159, 54, 44, 10, 0, 0, "Shaded" },
    { 'r', BPK_T_COLOR, 210, 28, 40, 14, 0, 0, "Color" }, { 'r', BPK_T_MAT, 210, 54, 40, 12, 0, 0, "Material" },
    { 'k', BPK_T_COL, 254, 28, 0, 0, 277, 300, NULL }, { 'm', BPK_T_MATL, 254, 54, 63, 14, 0, 0, NULL },
    { 'g', 0, 335, 25, 59, 41, 0, 0, "Profile" },
    { 'r', BPK_AUTO, 340, 36, 50, 10, 0, 0, "Auto" }, { 'r', BPK_ASDEF, 340, 50, 50, 10, 0, 0, "As defined" },
    { 'c', BPK_SPIN, 340, 73, 50, 14, 0, 0, "Spin tool" },
    { 'g', 0, 153, 99, 247, 32, 0, 0, "Holder" },
    { 'r', BPK_H_COLOR, 159, 111, 42, 14, 0, 0, "Color" }, { 'r', BPK_H_MAT, 284, 111, 40, 14, 0, 0, "Material" },
    { 'k', BPK_H_COL, 207, 111, 0, 0, 230, 253, NULL }, { 'm', BPK_H_MATL, 328, 111, 64, 14, 0, 0, NULL },
    { 'g', 0, 153, 136, 247, 29, 0, 0, "Opacity" },
    { 's', BPK_STATIC, 197, 148, 22, 14, 0, 0, "Active" }, { 'e', BPK_OP_A, 225, 146, 40, 14, 0, 0, NULL }, { 'p', BPK_OP_A, 265, 146, 10, 14, 0, 0, NULL },
    { 's', BPK_STATIC, 281, 148, 25, 14, 0, 0, "Inactive" }, { 'e', BPK_OP_I, 311, 146, 40, 14, 0, 0, NULL }, { 'p', BPK_OP_I, 351, 146, 10, 14, 0, 0, NULL },
    { 'g', 0, 153, 169, 247, 27, 0, 0, "Color Loop" },
    { 'r', BPK_LOOP_OP, 207, 178, 57, 12, 0, 0, "By operation" }, { 'r', BPK_LOOP_TC, 284, 178, 70, 12, 0, 0, "By tool change" },
    { 'g', 0, 153, 200, 247, 79, 0, 0, "Colors" },
    { 'i', 0, 164, 210, 11, 10, 0, 0, "mccoreres_BMP_1235" },         /* static 3044, SS_BITMAP #1235 */
    { 's', BPK_STATIC, 180, 215, 21, 14, 0, 0, "Rapid" }, { 'k', BPK_C_RAPID, 207, 213, 0, 0, 230, 253, NULL },
    { 's', BPK_STATIC, 163, 236, 38, 14, 0, 0, "Linear feed" }, { 'k', BPK_C_LIN, 207, 234, 0, 0, 230, 253, NULL },
    { 's', BPK_STATIC, 280, 215, 43, 14, 0, 0, "CW Arc feed" }, { 'k', BPK_C_CW, 328, 213, 0, 0, 351, 374, NULL },
    { 's', BPK_STATIC, 275, 236, 48, 14, 0, 0, "CCW Arc feed" }, { 'k', BPK_C_CCW, 328, 234, 0, 0, 351, 374, NULL },
    { 'c', BPK_USEVF, 207, 257, 68, 14, 0, 0, "Use Verify color" }, { 'k', BPK_C_VF, 282, 257, 0, 0, 305, 328, NULL },
    { 'g', 0, 153, 283, 247, 48, 0, 0, "4-5 Axis tool vectors" },
    { 'c', BPK_VEC, 159, 293, 70, 14, 0, 0, "Display vectors" }, { 'k', BPK_C_VEC, 159, 310, 0, 0, 182, 205, NULL },
    { 's', BPK_VEC_T, 239, 295, 32, 18, 0, 0, "Connect Bottom" }, { 's', BPK_VEC_T, 275, 295, 32, 18, 0, 0, "Connect Top" },
    { 's', BPK_VEC_T, 311, 295, 28, 18, 0, 0, "Shade" },
    { 'c', BPK_VEC1, 251, 314, 10, 10, 0, 0, "" }, { 'c', BPK_VEC2, 286, 314, 10, 10, 0, 0, "" }, { 'c', BPK_VEC3, 320, 314, 10, 10, 0, 0, "" },
    { 'c', BPK_VECLEN, 351, 298, 40, 10, 0, 0, "Length" }, { 'e', BPK_VLEN, 351, 310, 40, 14, 0, 0, NULL },
    { 'b', BPK_OK, 290, 344, 36, 18, 0, 0, "OK" }, { 'b', BPK_CANCEL, 330, 344, 36, 18, 0, 0, "Cancel" }, { 'b', BPK_HELP, 370, 344, 36, 18, 0, 0, "Help" },
    { 0, 0, 0, 0, 0, 0, 0, 0, NULL } };
/* edit ids (the DDX controls) for the front ends' edit buffers */
int bpo_edit_id(int key) {
    switch (key) { case BPK_FAST: return 3414; case BPK_INC: return 2980; case BPK_CC_RAD: return 2981; case BPK_OP_A: return 358; case BPK_OP_I: return 360;
                   case BPK_VLEN: return 3338; case BPK_T_COL: return 3419; case BPK_H_COL: return 3361; case BPK_C_RAPID: return 3391; case BPK_C_LIN: return 3483;
                   case BPK_C_CW: return 3474; case BPK_C_CCW: return 3466; case BPK_C_VF: return 3460; case BPK_C_VEC: return 3324; }
    return 0;
}
int bpo_on(int key) {
    switch (key) {
    case BPK_ENDPTS: return !bpo.interp; case BPK_INTERP: return bpo.interp;
    case BPK_CL_OP: return bpo.cleanup_op; case BPK_CL_TL: return bpo.cleanup_tl;
    case BPK_FIT: return bpo.fit_toolpath; case BPK_HOME_L: return bpo.home_lathe; case BPK_CAXIS: return bpo.caxis_top;
    case BPK_HOME_M: return bpo.home_mill; case BPK_SIMCC: return bpo.sim_cutter_comp; case BPK_SIMAX: return bpo.simul_caxis; case BPK_SIMROT: return bpo.simul_rot;
    case BPK_CC_CTL: return bpo.cc_in_control_active;
    case BPK_PLAIN: return bpo.tool_appearance == 0; case BPK_FLUTED: return bpo.tool_appearance == 1; case BPK_SHADED: return bpo.tool_appearance == 2;
    case BPK_T_COLOR: return !bpo.tool_material_on; case BPK_T_MAT: return bpo.tool_material_on;
    case BPK_AUTO: return !bpo.tool_as_defined; case BPK_ASDEF: return bpo.tool_as_defined; case BPK_SPIN: return bpo.spin_tool;
    case BPK_H_COLOR: return !bpo.holder_material_on; case BPK_H_MAT: return bpo.holder_material_on;
    case BPK_LOOP_OP: return !bpo.color_loop_on_tc; case BPK_LOOP_TC: return bpo.color_loop_on_tc;
    case BPK_USEVF: return bpo_use_verify; case BPK_VEC: return bpo.vec_on;
    case BPK_VEC1: return bpo.vec_check1; case BPK_VEC2: return bpo.vec_check2; case BPK_VEC3: return bpo.vec_check3; case BPK_VECLEN: return bpo.vec_length_cb;
    }
    return 0;
}
int bpo_en(int key) {
    const int interp = bpo.interp, shaded = bpo.tool_appearance == 2, vec = bpo.vec_on, cc_en = bp_opt.cc_in_control_enabled;
    switch (key) {
    case BPK_INC_T: case BPK_INC: return interp;
    case BPK_CL_TL: return !bpo.cleanup_op;
    case BPK_CC_CTL: return cc_en;
    case BPK_CC_RAD_T: case BPK_CC_RAD: return cc_en && bpo.cc_in_control_active;
    case BPK_FLUTED: return bpo.tool_as_defined != 0;
    case BPK_SHADED: return 1;                                   /* graphics_support == 1 */
    case BPK_T_MAT: case BPK_H_MAT: return shaded;
    case BPK_T_COL: return !bpo.tool_material_on; case BPK_H_COL: return !bpo.holder_material_on;
    case BPK_T_MATL: return shaded && bpo.tool_material_on; case BPK_H_MATL: return shaded && bpo.holder_material_on;
    case BPK_SPIN: return !shaded;
    case BPK_C_VF: return bpo_use_verify;
    case BPK_C_VEC: case BPK_VEC_T: case BPK_VEC1: case BPK_VEC2: case BPK_VEC3: case BPK_VECLEN: return vec;
    case BPK_VLEN: return vec && bpo.vec_length_cb;
    case BPK_HELP: return 0;
    }
    return 1;
}
void bpo_click(int key) {
    switch (key) {
    case BPK_ENDPTS: bpo.interp = 0; break; case BPK_INTERP: bpo.interp = 1; break;
    case BPK_CL_OP: bpo.cleanup_op = !bpo.cleanup_op; break; case BPK_CL_TL: bpo.cleanup_tl = !bpo.cleanup_tl; break;
    case BPK_FIT: bpo.fit_toolpath = !bpo.fit_toolpath; break; case BPK_HOME_L: bpo.home_lathe = !bpo.home_lathe; break;
    case BPK_CAXIS: bpo.caxis_top = !bpo.caxis_top; break; case BPK_HOME_M: bpo.home_mill = !bpo.home_mill; break;
    case BPK_SIMCC: bpo.sim_cutter_comp = !bpo.sim_cutter_comp; break; case BPK_SIMAX: bpo.simul_caxis = !bpo.simul_caxis; break;
    case BPK_SIMROT: bpo.simul_rot = !bpo.simul_rot; break; case BPK_CC_CTL: bpo.cc_in_control_active = !bpo.cc_in_control_active; break;
    case BPK_PLAIN: bpo.tool_appearance = 0; break; case BPK_FLUTED: bpo.tool_appearance = 1; break; case BPK_SHADED: bpo.tool_appearance = 2; break;
    case BPK_T_COLOR: bpo.tool_material_on = 0; break; case BPK_T_MAT: bpo.tool_material_on = 1; break;
    case BPK_AUTO: bpo.tool_as_defined = 0; break; case BPK_ASDEF: bpo.tool_as_defined = 1; break; case BPK_SPIN: bpo.spin_tool = !bpo.spin_tool; break;
    case BPK_H_COLOR: bpo.holder_material_on = 0; break; case BPK_H_MAT: bpo.holder_material_on = 1; break;
    case BPK_LOOP_OP: bpo.color_loop_on_tc = 0; break; case BPK_LOOP_TC: bpo.color_loop_on_tc = 1; break;
    case BPK_USEVF: bpo_use_verify = !bpo_use_verify; break; case BPK_VEC: bpo.vec_on = !bpo.vec_on; break;
    case BPK_VEC1: bpo.vec_check1 = !bpo.vec_check1; break; case BPK_VEC2: bpo.vec_check2 = !bpo.vec_check2; break;
    case BPK_VEC3: bpo.vec_check3 = !bpo.vec_check3; break; case BPK_VECLEN: bpo.vec_length_cb = !bpo.vec_length_cb; break;
    case BPK_OK: bpo_ok(); return; case BPK_CANCEL: bpo_cancel(); return;
    default: return;
    }
    bpo_update();
}
/* an edit / colour row's binding: kind, the value, its bounds (MCInt / MCReal: lo / hi) */
void *bpo_val(int key, int *kind, double *lo, double *hi) {
    *lo = 0; *hi = 0; *kind = BPE_BYTE;
    switch (key) {
    case BPK_FAST: *kind = BPE_INT; *lo = 1; *hi = 100000; return &bpo.fast_steps;
    case BPK_INC: *kind = BPE_REAL; *lo = bp_opt.tol * 10; return &bpo.interp_dist;
    case BPK_CC_RAD: *kind = BPE_REAL; *lo = 0.0; return &bpo.cc_in_control_radius;
    case BPK_OP_A: *kind = BPE_OPAC; *lo = 0.0; *hi = 1.0; return &bp_opt.opacity_active;      /* the global, at once */
    case BPK_OP_I: *kind = BPE_OPAC; *lo = 0.01; *hi = 1.0; return &bp_opt.opacity_inactive;
    case BPK_VLEN: *kind = BPE_REAL; *lo = bp_opt.tol; return &bpo.vec_length;
    case BPK_T_COL: return &bpo.tool_color; case BPK_H_COL: return &bpo.holder_color;
    case BPK_C_RAPID: return &bpo.rapid_color; case BPK_C_LIN: return &bpo.linear_color; case BPK_C_CW: return &bpo.cwarc_color;
    case BPK_C_CCW: return &bpo.ccwarc_color; case BPK_C_VF: return &bpo.verify_color; case BPK_C_VEC: return &bpo.vec_color;
    }
    return NULL;
}
/* the colour row's Select button (UIBitmapButton 0x61d2 -> sel_color_dialog(dlg, &idx, 1); the meaning of its 3rd argument
   is NOT DECODED, so picking from an entity is not offered) */
void bpo_select(int key) { int kd; double lo, hi; int *v = bpo_val(key, &kd, &lo, &hi); if (!v) return; bpo_pick = v; color_dialog(*v, 0, bpo_color_done); }

/* ----------------------------------------------- Operation List (17000) with its Path level (sections 8 / 13.8) */
/* level 1 = the machine group (image 0x112 from the Mastercam resource DLL - not in the icon pack here, left out),
   level 2 = the operations present (FUN_66d2c4f0), level 3 = one item per path range with end != start, text
   "%s %d" with string 17001 "Path" and FUN_5c210 + 1 where the position is 1-based: the first path reads "Path 2".
   How an operation is split into path ranges is NOT DECODED: each operation has one path = its own motion range.
   Root expanded at the end; right click -> menu 19100 Expand All (19101) / Collapse All (19102). Selection -> the
   item's ranges become the visible ranges and the position jumps to their last motion (FUN_66d2bfc0). */
int bp_tree_rows(BpTreeRow *out, int cap) {
    int ops[1024], os[1024], oe[1024], nops = 0, k = 0;          /* operation records: first / end motion */
    for (int i = 0; i < bpl.n && nops < 1024; i++) {
        int oi = bpl.m[i].op_index;
        if (nops && ops[nops - 1] == oi) { oe[nops - 1] = i + 1; continue; }
        ops[nops] = oi; os[nops] = i; oe[nops] = i + 1; nops++;
    }
    for (int row = -1; row < nops; row++) {
        for (int lvl = row < 0 ? 0 : 1; lvl <= (row < 0 ? 0 : 2); lvl++) {
            if (lvl == 1 && !bp_tr_grp_open) break;
            int oi = row >= 0 ? ops[row] : -1;
            if (lvl == 2 && (oi < 0 || oi >= 1024 || !bp_tr_op_open[oi] || oe[row] == os[row])) break;
            if (k >= cap) return k;
            BpTreeRow *r = &out[k++];
            r->lvl = lvl; r->id = lvl == 0 ? -2 : lvl == 1 ? oi * 2 : oi * 2 + 1; r->op = oi;
            r->m0 = row >= 0 ? os[row] : 0; r->m1 = row >= 0 ? oe[row] : bpl.n;
            if (lvl == 0) snprintf(r->text, sizeof r->text, "%s", mach_grp_name);
            else if (lvl == 1) { const Op *op = oi >= 0 && oi < F.nops ? &F.ops[oi] : NULL;
                                 if (op) snprintf(r->text, sizeof r->text, "%d - %s%s%s", oi + 1, op->type, op->comment[0] ? " - " : "", op->comment); else snprintf(r->text, sizeof r->text, "%d", oi + 1); }
            else snprintf(r->text, sizeof r->text, "%s %d", "Path", 1 + 1);      /* FUN_66d2bc10: 1-based position + 1 */
            r->expandable = lvl == 0 || lvl == 1;
            r->open = lvl == 0 ? bp_tr_grp_open : lvl == 1 && oi >= 0 && oi < 1024 ? bp_tr_op_open[oi] : 0;
            r->selected = bp_tr_sel == r->id;
        }
    }
    return k;
}
void bp_tree_toggle(const BpTreeRow *r) { if (r->lvl == 0) bp_tr_grp_open = !r->open; else if (r->op >= 0 && r->op < 1024) bp_tr_op_open[r->op] = !r->open; }
void bp_tree_click(const BpTreeRow *r) {
    bp_tr_sel = r->id;
    if (r->lvl == 0) bp_set_visible(&bpl, 0, bpl.n); else bp_set_visible(&bpl, r->m0, r->m1);
}
void bp_tree_expand_all(int on) { if (on) { bp_tr_grp_open = 1; memset(bp_tr_op_open, 1, sizeof bp_tr_op_open); } else bp_tr_grp_open = 0; }

/* ------------------------------------------------ Info area pages (Details 13000 / Info 4006), display-only (13.7) */
const BpCtl BP_PG4000[] = {   /* backplotres 4000 "Rapid" (coolant label / combo 4104 hidden + disabled: left out) */
    {'s',0,42,1,20,8,"Rapid"},{'l',0,2,14,100,1,0},{'s',0,43,22,8,8,"X"},{'e',4100,56,21,37,12,0},{'s',0,43,39,8,8,"Y"},{'e',4101,56,38,37,12,0},
    {'s',0,43,56,8,8,"Z"},{'e',4102,56,55,37,12,0},{'s',0,22,73,29,8,"Feedrate"},{'e',25192,56,72,37,12,0},{'s',0,3,89,48,8,"Cutter comp"},{'e',4105,56,88,37,12,0},{0,0,0,0,0,0,0} };
const BpCtl BP_PG4001[] = {   /* 4001 "Feed" */
    {'s',0,38,1,37,8,"Linear Feed"},{'l',0,2,14,100,1,0},{'s',0,43,22,8,8,"X"},{'e',4100,56,21,37,12,0},{'s',0,43,39,8,8,"Y"},{'e',4101,56,38,37,12,0},
    {'s',0,43,56,8,8,"Z"},{'e',4102,56,55,37,12,0},{'s',0,22,73,29,8,"Feedrate"},{'e',25192,56,72,37,12,0},{'s',0,2,89,48,8,"Cutter comp"},{'e',4105,56,88,37,12,0},{0,0,0,0,0,0,0} };
const BpCtl BP_PG4002[] = {   /* 4002 "Arc" */
    {'s',0,49,1,11,8,"Arc"},{'l',0,2,14,100,1,0},{'g',0,6,13,91,54,"End"},{'g',0,6,69,91,54,"Center"},
    {'s',0,43,22,8,8,"X"},{'e',4100,56,21,37,12,0},{'s',0,43,37,8,8,"Y"},{'e',4101,56,36,37,12,0},{'s',0,43,52,8,8,"Z"},{'e',4102,56,51,37,12,0},
    {'s',0,43,78,8,8,"X"},{'e',4202,56,77,37,12,0},{'s',0,43,93,8,8,"Y"},{'e',4204,56,92,37,12,0},{'s',0,43,108,8,8,"Z"},{'e',4206,56,107,37,12,0},
    {'s',0,22,129,29,8,"Feedrate"},{'e',25192,56,128,37,12,0},{'s',0,22,144,29,8,"Circle"},{'c',4207,56,144,11,8,0},
    {'s',0,2,156,48,8,"Plane"},{'e',4201,56,154,37,12,0},{'s',0,3,169,48,8,"Cutter comp"},{'e',4105,56,168,37,12,0},{0,0,0,0,0,0,0} };
const BpCtl BP_PG4006[] = {   /* 4006 "Info Tab"; 25414-25421 = the cycle time statics */
    {'s',25417,48,2,42,8,"Cycle Time"},{'s',25414,1,14,42,8,"Feed"},{'s',25415,1,30,42,8,"Rapid"},{'s',25416,1,49,42,8,"Total"},{'l',25418,48,44,80,1,0},
    {'b',25419,48,14,10,10,"backplotres_BMP_4614"},{'b',25420,48,28,10,10,"backplotres_BMP_4615"},{'b',25421,48,49,10,10,"backplotres_BMP_4616"},
    {'s',4601,61,14,56,10,0},{'s',4602,61,28,56,10,0},{'s',4603,61,49,56,10,0},
    {'l',0,3,69,130,1,0},{'s',0,48,74,42,8,"Path Length"},{'s',0,1,86,42,8,"Feed"},{'b',0,48,86,8,8,"backplotres_BMP_4618"},{'s',4604,61,86,56,10,0},
    {'s',0,1,102,42,8,"Rapid"},{'b',0,48,102,9,9,"backplotres_BMP_4617"},{'s',4605,61,102,56,10,0},{'l',0,3,116,130,1,0},
    {'s',0,1,132,42,8,"Min/Max"},{'s',0,48,122,8,8,"X"},{'s',0,48,132,8,8,"Y"},{'s',0,48,142,8,8,"Z"},
    {'s',4606,55,122,31,8,0},{'s',4607,55,132,31,8,0},{'s',4608,55,142,31,8,0},{'s',4609,92,122,31,8,0},{'s',4610,92,132,31,8,0},{'s',4611,92,142,31,8,0},
    {'l',0,3,155,130,1,0},{'s',0,1,162,42,19,"Min/Max Feedrate"},{'s',4612,55,167,31,8,0},{'s',4613,92,167,31,8,0},{0,0,0,0,0,0,0} };
void bp_num(char *o, size_t n, double v) {    /* n_places decimals, trailing zeros removed (FUN_66d87620) */
    int pl = cfg_places >= 0 ? cfg_places : 4;
    snprintf(o, n, "%.*f", pl, v); char *e = o + strlen(o) - 1; if (strchr(o, '.')) { while (*e == '0') *e-- = 0; if (*e == '.') *e = 0; }
}
void bp_time(char *o, size_t n, double s) {   /* FUN_66d28710 */
    int h = (int)(s / 3600), mi = (int)((s - h * 3600) / 60); double sec = s - h * 3600 - mi * 60;
    if (!h && !mi) snprintf(o, n, "%1.2lfs", sec); else if (!h) snprintf(o, n, "%im:%1.2lfs", mi, sec); else snprintf(o, n, "%ih:%im:%1.2lfs", h, mi, sec);
}
/* Details values (FUN_67daf130 / FUN_67dae4f0 / FUN_67dafbe0). Number format NOT DECODED: n_places as the Info tab.
   Cutter comp 4105 (FUN_67dcdf80 codes 0/40/41/42) is not carried by the motions here: NOT DECODED, left empty.
   Plane 4201: value 0/1/2 -> "XY"/"XZ"/"YZ" as decoded (the post's motion keeps Mastercam's NCI plane number).
   Circle 4207 = entity +0x42: not carried here, unchecked. */
const char *bp_det_val(int id, char *b) {
    const NcMotion *m = &bpl.m[bpl.pos];
    switch (id) {
    case 4100: bp_num(b, 64, m->b[0]); return b; case 4101: bp_num(b, 64, m->b[1]); return b; case 4102: bp_num(b, 64, m->b[2]); return b;
    case 4202: bp_num(b, 64, m->c[0]); return b; case 4204: bp_num(b, 64, m->c[1]); return b; case 4206: bp_num(b, 64, m->c[2]); return b;
    case 25192: if (m->feed < 0) return ""; bp_num(b, 64, m->feed); return b;    /* rapid: feed < 0 -> "" */
    case 4201: return m->plane == 0 ? "XY" : m->plane == 1 ? "XZ" : m->plane == 2 ? "YZ" : "";
    }
    return "";
}
/* the Details page of the current motion: drill = its feed sub-motion's page (12.4); tool change: no page decoded (NULL) */
const BpCtl *bp_det_page(void) {
    const NcMotion *m = &bpl.m[bpl.pos];
    return m->kind == NCM_RAPID ? BP_PG4000 : m->kind == NCM_LINE || m->kind == NCM_DRILL ? BP_PG4001 : m->kind == NCM_CW || m->kind == NCM_CCW ? BP_PG4002 : NULL;
}
static BpInfo bp_inf;
/* the machine definition inputs of the rapid time (13.6): their location in the part's machine record is NOT DECODED */
static const BpMachine bp_machine = { 0 };
/* the Info tab's sums (computed on demand when shown); returns 1 when the cycle time statics are hidden (total exactly 0, or
   the feed time is invalid) */
int bp_info_refresh(void) {
    bp_info(&bpl, &bp_inf); bp_info_rapid(&bpl, &bp_machine, &bp_inf);
    double tot = bp_inf.feed_time_s + bp_inf.rapid_time_s;
    return !isfinite(bp_inf.feed_time_s) || tot == 0;    /* total exactly 0 (NaN feed -> 0): the cycle time statics are hidden */
}
const char *bp_info_val(int id, char *b) {             /* NCIInfoDialog FUN_28a10 / DDX FUN_28540 */
    const BpInfo *in = &bp_inf;
    switch (id) {
    case 4601: if (!isfinite(in->feed_time_s)) return "Invalid feedrate"; bp_time(b, 64, in->feed_time_s); return b;   /* string 29003 */
    case 4602: if (!in->have_rapid_time) return ""; bp_time(b, 64, in->rapid_time_s); return b;
    case 4603: if (!in->have_rapid_time) return ""; if (!isfinite(in->feed_time_s)) return "Invalid feedrate"; bp_time(b, 64, in->feed_time_s + in->rapid_time_s); return b;
    case 4604: bp_num(b, 64, in->feed_len); return b; case 4605: bp_num(b, 64, in->rapid_len); return b;
    case 4606: case 4607: case 4608: if (in->lo[id - 4606] > in->hi[id - 4606]) return ""; bp_num(b, 64, in->lo[id - 4606]); return b;
    case 4609: case 4610: case 4611: if (in->lo[id - 4609] > in->hi[id - 4609]) return ""; bp_num(b, 64, in->hi[id - 4609]); return b;
    case 4612: if (!in->have_feed) return ""; bp_num(b, 64, in->fmin); return b;
    case 4613: if (!in->have_feed) return ""; bp_num(b, 64, in->fmax); return b;
    }
    return "";
}
int bp_page_hidden(int id, int hide_cycle) { return hide_cycle && ((id >= 25414 && id <= 25421) || (id >= 4601 && id <= 4603)); }
/* UpdateStatusBarText "X:%1.4f   Y:%1.4f  Z:%1.4f" (backplot.md 4.4): the status bar while backplotting */
void bp_status_text(char *o, size_t n) { double p[3]; bp_point(&bpl, p); snprintf(o, n, "X:%1.4f   Y:%1.4f  Z:%1.4f", p[0], p[1], p[2]); }

/* tests: the backplot state (MCAMUI_MISCDUMP=1, both apps) */
void bp_dump(void) {
    if (!bp_on) return;
    double pp[3]; bp_point(&bpl, pp);
    printf("    backplot motion %d/%d vis %d..%d at %.4f %.4f %.4f stops %d flags", bpl.pos, bpl.n, bpl.vis0, bpl.vis1, pp[0], pp[1], pp[2], bpl.nstops);
    for (int k = 0; k < 17; k++) printf("%d", bpl.flags[k]);
    printf(" exp %d tab %d speed %d playing %d lines %d/%d ep %d tool %d", bp_exp, bp_tab, bp_speed_pos, bp_playing, bp_ln[0], bp_ln[1], bp_nep, bp_tool_on);
    printf(" cs %d %d %d:%u %d%d%d %.4f %.4f %.4f %d%d%d", bpl.cs.at_ops, bpl.cs.at_toolchange, bpl.cs.at_step, bpl.cs.step, bpl.cs.at_x, bpl.cs.at_y, bpl.cs.at_z,
           bpl.cs.x, bpl.cs.y, bpl.cs.z, bpl.cs.chg_x, bpl.cs.chg_y, bpl.cs.chg_z);
    printf(" opt %d %d %d %d %.4f %d %.2f %.2f%s%s\n", bp_opt.tool_appearance, bp_opt.rapid_color, bp_opt.linear_color, bp_opt.interp, bp_opt.interp_dist,
           bp_opt.cleanup_tl, bp_opt.opacity_active, bp_opt.opacity_inactive, bpo_open ? " (options open)" : "", bp_show_cs ? " (stops open)" : "");
    BpTreeRow rows[64]; int nr = bp_tree_rows(rows, 64);
    for (int i = 0; i < nr; i++) printf("      bptree %d %s%s%s\n", rows[i].lvl, rows[i].expandable ? rows[i].open ? "- " : "+ " : "  ", rows[i].text, rows[i].selected ? " (selected)" : "");
    char b[64];
    const BpCtl *pg = bp_tab == 0 ? bp_det_page() : BP_PG4006; int hide = bp_tab == 1 ? bp_info_refresh() : 0;
    if (pg) { printf("      bppage %s:", bp_tab ? "Info" : "Details");
        for (const BpCtl *c = pg; c->t; c++) if (c->id && !c->s && !bp_page_hidden(c->id, hide)) { const char *v = bp_tab ? bp_info_val(c->id, b) : bp_det_val(c->id, b); printf(" %d=%s", c->id, v); }
        printf("\n"); }
}
/* script steps (as mcamui run_script): bp, bpstep:MODE,COUNT, bptool, bpplay, bpopt, bptog:BYTE, bptab:N, bph:H, bpset:KEY=V,
   bpopen:OP; ours: bpvcr:ID (a VCR / toggle / row-2 button), bpslider:F, bpspeed:N, bpcs (open 6000), bpcsset:K=V, bpcsok,
   bpcscancel, bpoclick:KEY, bpoedit:KEY=V, bpook, bpocancel, bpexp, bptree:ROW[,toggle], bpexpandall:0|1, bpok (OK = stop) */
int cui_bp_script(const char *st, const char *arg) {
#define IS(x) (!strcmp(st, x))
    if (IS("bp")) { bp_start(); bp_frame_still(); return 1; }                       /* Backplot the selection */
    if (IS("bpstep") && arg && bp_on) { int md = 3, c = 1; sscanf(arg, "%d,%d", &md, &c); bp_step(&bpl, md, c);
        double pp[3]; bp_point(&bpl, pp); printf("    backplot motion %d/%d at %.4f %.4f %.4f, stops %d\n", bpl.pos, bpl.n, pp[0], pp[1], pp[2], bpl.nstops); bp_frame_still(); return 1; }
    if (IS("bptool") && bp_on) { bp_toggle(7); bp_exp = 1; bp_frame_still(); return 1; }
    if (IS("bpplay") && bp_on) { bp_keys('R'); return 1; }
    if (IS("bpopt") && bp_on) { bp_options_open(); return 1; }                /* Backplot: Options 31007 */
    if (IS("bptog") && arg && bp_on) { bp_toggle(atoi(arg)); bp_exp = 1; bp_frame_still(); return 1; }
    if (IS("bptab") && arg && bp_on) { bp_tab = atoi(arg); bp_exp = 1; return 1; }   /* 0 Details, 1 Info */
    if (IS("bph") && arg && bp_on) { bp_exp_h = atof(arg); return 1; }       /* expanded 14000 height (as a WM_SIZING drag) */
    if (IS("bpset") && arg) { int k = 0; const char *eq = strchr(arg, '='); if (eq && sscanf(arg, "%d", &k) == 1) bp_opt_cfg(&bp_opt, k, eq + 1); if (bp_on) { bp_build_lines(); bp_frame_still(); } return 1; }   /* KEY=VALUE as in mcamxm.config */
    if (IS("bpopen") && arg && bp_on) { int k = atoi(arg); if (k >= 0 && k < 1024) bp_tr_op_open[k] = 1; return 1; }   /* expand an operation in the Operation List */
    if (!bp_on) return 0;
    if (IS("bpvcr") && arg) { int id = atoi(arg); if (id >= 31001 && id <= 31007) bp_tgl(id); else if (id >= 16001 && id <= 16011) bp_row2(id); else if (id == 14001) bp_exp = !bp_exp; else bp_vcr(id); bp_frame_still(); return 1; }
    if (IS("bpslider") && arg) { bp_slider_seek(atof(arg)); bp_frame_still(); return 1; }
    if (IS("bpspeed") && arg) { bp_speed_pos = atoi(arg); return 1; }
    if (IS("bpcs")) { bp_cs_open(); return 1; }
    if (IS("bpcsset") && arg) { const char *eq = strchr(arg, '='); if (!eq) return 1; int v = atoi(eq + 1); char k[16]; snprintf(k, sizeof k, "%.*s", (int)(eq - arg), arg);
        if (!strcmp(k, "ops")) bp_cs_at_ops(v); else if (!strcmp(k, "tc")) bpl.cs.at_toolchange = v; else if (!strcmp(k, "step")) bpl.cs.at_step = v;
        else if (!strcmp(k, "x")) bpl.cs.at_x = v; else if (!strcmp(k, "y")) bpl.cs.at_y = v; else if (!strcmp(k, "z")) bpl.cs.at_z = v;
        else if (!strcmp(k, "cx")) bpl.cs.chg_x = v; else if (!strcmp(k, "cy")) bpl.cs.chg_y = v; else if (!strcmp(k, "cz")) bpl.cs.chg_z = v;
        else if (!strcmp(k, "stepn")) snprintf(bp_cs_txt[0], 32, "%s", eq + 1); else if (!strcmp(k, "xv")) snprintf(bp_cs_txt[1], 32, "%s", eq + 1);
        else if (!strcmp(k, "yv")) snprintf(bp_cs_txt[2], 32, "%s", eq + 1); else if (!strcmp(k, "zv")) snprintf(bp_cs_txt[3], 32, "%s", eq + 1);
        return 1; }
    if (IS("bpcsok")) { bp_cs_ok(); bp_frame_still(); return 1; }
    if (IS("bpcscancel")) { bp_show_cs = 0; return 1; }
    if (IS("bpoclick") && arg) { int k = atoi(arg); if (bpo_open && bpo_en(k)) bpo_click(k); bp_frame_still(); return 1; }
    if (IS("bpoedit") && arg) { const char *eq = strchr(arg, '='); if (!eq || !bpo_open) return 1; int k = atoi(arg), kd; double lo, hi; void *v = bpo_val(k, &kd, &lo, &hi);
        char clue[32] = ""; if (v && bpo_en(k)) { if (bpo_edit_commit(kd, v, eq + 1, lo, hi, clue, sizeof clue)) printf("    (clue %s)\n", clue); } return 1; }
    if (IS("bpook")) { if (bpo_open) bpo_ok(); bp_frame_still(); return 1; }
    if (IS("bpocancel")) { bpo_cancel(); return 1; }
    if (IS("bpexp")) { bp_exp = !bp_exp; return 1; }
    if (IS("bptree") && arg) { BpTreeRow rows[1100]; int nr = bp_tree_rows(rows, 1100), r = atoi(arg);
        if (r >= 0 && r < nr) { if (strstr(arg, "toggle")) bp_tree_toggle(&rows[r]); else bp_tree_click(&rows[r]); } bp_frame_still(); return 1; }
    if (IS("bpexpandall") && arg) { bp_tree_expand_all(atoi(arg)); return 1; }
    if (IS("bpok")) { bp_stop(); return 1; }
    return 0;
#undef IS
}
