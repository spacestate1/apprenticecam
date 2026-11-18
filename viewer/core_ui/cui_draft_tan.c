/* cui_draft_tan.c - the Drafting tab's TANGENT (doDraft_DIMENSION_TANGENT 0x9480, type 6; command G_DIMT) and PERPENDICULAR
 * (doDraft_DIMENSION_PERPENDICULAR 0x947e, type 5; G_DIMPP) commands in the dimension command loop (cui_draft.c / cui_draft_dim.c
 * dispatch here; geo_cmd tells them apart). Mastercam (mccad): FUN_180040ed0 cases 0x9480 / 0x947e -> FUN_180048b10(6 / 5, -1)
 * (cad_dimcmd.c:441-525); picks FUN_18004a620 / FUN_18004b070 / FUN_180049ee0 (cad_linear.c), masks FUN_180049c60 (cad_linear.c:2685),
 * prompts FUN_180049560 (cad_linear.c:359; asm 0x18004977f-0x180049791); tangent set-up FUN_18004f420 -> FUN_18004ee50 (circ_cad.c:352),
 * drag FUN_18004d9e0 (cad_linear.c:2018), events FUN_18004d930 (cad_linear.c:1222), prompt FUN_18004e460, panel FUN_18004e6c0 /
 * FUN_18004e7f0 (decomp/draft/tan_cad.c); perpendicular set-up FUN_180048330 -> FUN_180048270 / FUN_1800480c0 / FUN_180048460, drag
 * FUN_180047320 (cad_linear.c:1884), events FUN_180047270 (cad_linear.c:1075), prompt FUN_180047cf0, panel FUN_180047f40 / FUN_180048020
 * (decomp/draft/perp_cad.c). The calcs: viewer/draft_create_tan.c. "ours" marks glue of this program. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "cui_draft_int.h"
#include "draft_create_tan.h"
#include "mcam_saveops.h"
#include "draft.h"
#include "scene.h"

#define TWOPI_ 6.283185307179586

/* ------------------------------------------------------------------------------------------------ state (mccad globals) */
static int dt_n;                                        /* DAT_1806960e8 picks */
static uint8_t dt_slot[4][0x98]; static int dt_sent[4]; /* DAT_180698a30 / ac8 / b60 / bf8 (+ the entity each references, -1) */
static int dt_ent0 = -1;                                /* the first pick's entity (arc / line) */
static double dt_cur[3], dt_prev[3]; static int dt_have_cur;   /* the last / previous sketched cursor (DAT_1806a0ed0 / ec0, 180695f70 / f60) */
/* tangent (FUN_18004ee50's globals) */
static DtTanInput tn;                                   /* mode DAT_18069b890, arcs DAT_18069b8c0 / e1f8, slots DAT_1806a0b30 .. */
static int t_orient, t_sides[2], t_lock, t_first, t_force, t_b896, t_6ee, t_b8b0;   /* b8ac, b8b4 / b8b6, a4c20, 6f0, b897, b896, 6ee, b8b0 */
static double t_mn1[2], t_mx1[2], t_mn2[2], t_mx2[2], t_mn3[2], t_mx3[2];        /* DAT_1806a0ee0 .. 0f30 */
static uint8_t t_o1[4][0x98], t_o2[4][0x98], t_res[2][0x98];                   /* DAT_1806a0f40 / 11a0, DAT_1806a0d90 / e28 */
static double t_hvp;                                    /* DfHvpAngle */
static int t_maintain;                                  /* DfMaintainTang: 0 at start-up (text4.c:150; no config item writes it) */
/* perpendicular (FUN_180048270's globals) */
static DtPerpInput pn;                                  /* mode DAT_180695a88, aps DAT_180695d00 .., the linear pair DAT_180695f80 / 6018 */
static int p_orient, p_lock, p_first, p_a87;            /* DAT_1806496a8, a84, 69e, a87 */
static double p_hvp;
/* the live entity's inputs (panel changes recompute it, as cui_draft.c) */
static DtTanInput tn_live; static DtPerpInput pn_live; static int live_type;

static int is_tan(void) { return geo_cmd == G_DIMT; }
static double sel_dist_w(void) { return SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1); }   /* as cui_draft.c dr_compute */
static void w2v_(const double w[3], double v[3]) { for (int i = 0; i < 3; i++) v[i] = dr_view[3 * i] * w[0] + dr_view[3 * i + 1] * w[1] + dr_view[3 * i + 2] * w[2]; }
static void tok(uint8_t ap[0x98], int e) {              /* the eptr of the entity an aPoint references: a new entity has none until the save -
                                                           the session token mcam_saveops.c replaces (ours, as cui_draft_ang.c) */
    uint64_t ep = e >= 0 && F.ents[e].eptr ? (uint64_t)F.ents[e].eptr : e >= 0 ? MCAM_EPTR_TOKEN((uint64_t)e) : 0; memcpy(ap + 0x28, &ep, 8);
}
static uint64_t ent_ep(int e) { return F.ents[e].eptr ? (uint64_t)F.ents[e].eptr : MCAM_EPTR_TOKEN((uint64_t)e); }
/* an XY arc (Ent vw 0) as the in-memory A_ID: a Top-view arc, view coordinates = world (formats/mcam.md "Arc views") */
static void arc_of(int e, DtArc *a) {
    const Ent *en = &F.ents[e]; memset(a, 0, sizeof *a);
    a->ep0[0] = en->a.x; a->ep0[1] = en->a.y; a->ep0[2] = en->a.z; a->ep1[0] = en->b.x; a->ep1[1] = en->b.y; a->ep1[2] = en->b.z;
    a->c[0] = en->c.x; a->c[1] = en->c.y; a->c[2] = en->c.z; a->r = en->r; a->start = en->a0; a->sweep = en->sw;
    a->view = 1; memcpy(a->vm, dr_view, sizeof a->vm); a->eptr = ent_ep(e);
}
/* get_ent_from_eptr over the session (the file's eptrs and the session tokens) for DfGetTangentArc (ours) */
static int ent_of(void *u, uint64_t eptr, DtArc *arc) {
    (void)u;
    for (int e = 0; e < F.nents; e++) {
        if (!F.ents[e].type || ent_ep(e) != eptr) continue;
        if (F.ents[e].type != E_ARC || F.ents[e].vw) return 1;
        if (arc) arc_of(e, arc);
        return 4;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ prompts */
/* FUN_180049560 (cad_linear.c:359-585; lines joined with "\n" = mccad 0x18040e028): tangent 27708 "Create dimension, tangent: " (0 picks:
   no second line - the 0-pick table covers the types -1..5 only, asm 0x18004977f-0x180049791) / 1 pick 27264 "Select an arc, line or
   point"; perpendicular 27727 "Create dimension, perpendicular:" + mccoreres 1892 "Select a line" / 27729 "Select a parallel line or a
   point". Placement: FUN_18004e460 27708 + 27054 "Sketch the text position for the dimension"; FUN_180047cf0 (no title line: its first
   string is never filled) 27053 "Sketch the text position for the perpendicular dimension"; both + 27070 "Hold '~' ...": left out, the
   '~' formatting pick is NOT PORTED (as cui_draft.c). The live entity: mccoreres 5887 */
void dt_prompt(void) {
    if (dr_placing) snprintf(cam_msg, sizeof cam_msg, is_tan() ? "Create dimension, tangent: \nSketch the text position for the dimension" : "Sketch the text position for the perpendicular dimension");
    else if (dr_live >= 0 && !dt_n) snprintf(cam_msg, sizeof cam_msg, "Modify current dimension or begin creating a new dimension");
    else if (is_tan()) snprintf(cam_msg, sizeof cam_msg, dt_n ? "Create dimension, tangent: \nSelect an arc, line or point" : "Create dimension, tangent: ");
    else snprintf(cam_msg, sizeof cam_msg, dt_n ? "Create dimension, perpendicular:\nSelect a parallel line or a point" : "Create dimension, perpendicular:\nSelect a line");
}
/* FUN_180048b10(6 / 5, -1) -> FUN_18004b820 (cad_linear.c:904): InitDIM(drag), DfHvpAngle 0, no picks */
void dt_restart(void) {
    dt_n = 0; dr_npick = 0; dr_placing = 0; dr_drag_ok = 0; dt_ent0 = -1; dr_Pent[0] = dr_Pent[1] = -1; dt_have_cur = 0;
    for (int i = 0; i < 4; i++) dt_sent[i] = -1;
    t_hvp = p_hvp = 0.0;
    dc_init_dim(dr_drag, &dr_S, dr_view_no());
    dt_prompt(); dr_scene();
}
/* FUN_180049c60 (cad_linear.c:2685-2790): tangent 0 picks flags 10 / mask 4 (arcs; entity events only), 1 pick flags 0xb / mask 7 (points,
   lines, arcs; point events); perpendicular 0 picks flags 10 / mask 2 (lines), 1 pick flags 0xb / mask 3; placement: FUN_18004e460 /
   FUN_180047cf0 flags 0xc, mask 0 (a position) */
int dt_need(void) { return dr_placing ? 't' : 'e'; }

/* ------------------------------------------------------------------------------------------------ tangent */
/* FUN_18004f420 (lock 0, DfHvpAngle 0) -> FUN_18004ee50 (circ_cad.c:352-605; param_4 = 0: asm 0x18004f43f-0x18004f444 moves the mask to the
   stack and zeroes r9) */
static int tan_setup(int mode, int a0, int a1) {
    t_lock = 0; t_hvp = 0.0;
    dc_init_dim(dr_drag, &dr_S, dr_view_no());
    t_orient = 0; t_sides[0] = t_sides[1] = 0; t_b896 = t_force = 0; t_b8b0 = 0; t_first = 1; t_6ee = 1;
    memset(&tn, 0, sizeof tn); tn.mode = mode;
    arc_of(a0, &tn.arc[0]); if (mode == 1) arc_of(a1, &tn.arc[1]);
    memcpy(tn.slot[0], dt_slot[0], 0x98); memcpy(tn.slot[1], dt_slot[1], 0x98); if (mode > 2) memcpy(tn.slot[2], dt_slot[2], 0x98);
    /* FUN_18004e070: no drag-edit entity -> 1; aPointTestTol (dc_apoint_world did it); views_parallel: XY arcs (Top) only here */
    double b8a0; int b8a8;
    dc_tan_maintain(mode, &tn.arc[0], &tn.arc[1], (const uint8_t (*)[0x98])tn.slot, 0, 0.0, &b8a0, &b8a8);   /* FUN_18004e290 */
    if (t_maintain) { t_orient = b8a8; t_hvp = b8a0; t_force = 1; }   /* DfCheckAngle: dc_tan_maintain's df_calc_orient_ang already checked it */
    t_6ee = t_sides[1] == 0;                            /* FUN_18004f8f0 (DAT_1806496fc = 1) */
    /* FUN_18004e980: df_phantom_on of the arc(s) / the point or line - the phantom display NOT PORTED */
    dr_placing = 1; dr_drag_ok = 0; dr_npick = 2; dt_have_cur = 0;
    dr_Pent[0] = a0; dr_Pent[1] = -1;
    dt_prompt();
    return 1;
}
/* FUN_18004d9e0 (cad_linear.c:2018-2154): one mouse move of the placement */
static int tan_compute(const double cur[3], int ctrl) {
    double tv[3]; w2v_(cur, tv);
    int old = t_orient;
    if (t_first || t_force) {                           /* calc_tangent_rbox -> the boxes and extreme points */
        dc_tan_rbox(tn.mode, tn.arc, (const uint8_t (*)[0x98])tn.slot, t_hvp, t_mn1, t_mx1, t_o1, t_mn2, t_mx2, t_o2, t_mn3, t_mx3);
    }
    if (!t_lock) {                                      /* DAT_18069b898 = DAT_1806a4c20 */
        double r[3]; dc_tan_rot_about(tv, tn.arc[0].c, -t_hvp, r);
        t_orient = dc_tan_orient_by_box(t_mn3, t_mx3, r, old);     /* FUN_18012ac50 */
        double d; int s = dc_tan_side(t_mn1, t_mx1, r, t_orient, &d);   /* FUN_18004e3b0 */
        if ((s != t_sides[0] || t_b8b0) && d <= sel_dist_w()) { t_b896 = 1; t_b8b0 = 0; t_sides[0] = s; }
    }
    if (t_first || t_force || t_b896 || t_orient != old) {
        int ok = 0;
        for (int k = 0; ; k++) {
            ok = dc_tan_eps(tn.mode, tn.arc, (const uint8_t (*)[0x98])tn.slot, t_orient, t_sides, 0, t_o1, t_o2, t_res, &t_hvp, dr_assoc);
            if (ok || k >= 1) break;
            t_6ee = !t_6ee; t_sides[1] = t_6ee ? 0 : 180; t_b896 = 1;   /* FUN_18004f910 (DAT_1806496fc = 1) */
        }
        if (!ok) goto abandon;
    }
    memcpy(tn.ap1, t_res[0], 0x98); memcpy(tn.ap2, t_res[1], 0x98); memcpy(tn.cursor, cur, sizeof tn.cursor);
    tn.view = dr_view_no(); memcpy(tn.vm, dr_view, sizeof tn.vm);
    tn.orient = t_orient; tn.sides[0] = t_sides[0]; tn.sides[1] = t_sides[1]; tn.hvp = t_hvp; tn.maintain = t_maintain; tn.assoc = dr_assoc;
    tn.ctrl = ctrl; tn.ent_of = ent_of; tn.u = NULL; tn.tmpl = NULL;
    {
        DcSettings S = dr_S; S.sel_dist = sel_dist_w(); S.color = geo_color; S.level = geo_level;   /* main_color / main_level */
        dc_init_dim(dr_drag, &S, dr_view_no());          /* the drag entity as InitDIM left it (ours: recomputed each move, as cui_draft.c) */
        DcResult R = dc_calc_tangent(dr_drag, &tn, &S, dr_fonts_get());
        if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); dr_drag_ok = 0; dr_scene(); return -1; }
        if (R.swapped) { uint8_t t[0x98]; memcpy(t, t_res[0], 0x98); memcpy(t_res[0], t_res[1], 0x98); memcpy(t_res[1], t, 0x98); }
        dr_drag_ok = R.valid;
        if (R.valid) { t_b896 = t_force = t_first = 0; dr_scene(); return 1; }
    }
abandon:                                                /* 27010 + FUN_18004d8d0 (phantom off, drag erased, DfHvpAngle 0): the dimension abandoned */
    snprintf(cam_msg, sizeof cam_msg, "Warning - cannot calculate dimension");
    dr_drag_ok = 0; dt_n = 0; dr_npick = 0; dr_placing = 0; t_hvp = 0.0; dr_scene();
    return 0;
}

/* ------------------------------------------------------------------------------------------------ perpendicular */
/* FUN_180048330 (cad_linear.c:2514) -> FUN_180048270 (perp_cad.c: FUN_18012ade0 = map_cursor_to_3d of the window centre - taken as
   succeeding; mode, InitDIM, orientation 1, indices 9 / 9, lock 0, DfHvpAngle 0, first move) -> FUN_180047880 (no drag-edit entity) ->
   FUN_1800480c0 (FUN_180048460: the four aPoints; DfHvpAngle = find_orient_angle(the first line), DfCheckAngle; df_phantom_on - the
   phantom display NOT PORTED; prompt) */
static int perp_setup(int mode) {
    dc_init_dim(dr_drag, &dr_S, dr_view_no());
    memset(&pn, 0, sizeof pn); pn.mode = mode;
    p_orient = 1; pn.idx[0] = pn.idx[1] = 9; p_lock = 0; p_first = 1; p_a87 = 0; p_hvp = 0.0;
    uint8_t s4[4][0x98]; memcpy(s4, dt_slot, sizeof s4);
    dc_perp_setup(mode, (const uint8_t (*)[0x98])s4, dr_view, pn.aps, &p_hvp);
    dr_placing = 1; dr_drag_ok = 0; dr_npick = 2; dt_have_cur = 0;
    dr_Pent[0] = dt_ent0; dr_Pent[1] = -1;
    dt_prompt();
    return 1;
}
/* FUN_180047320 (cad_linear.c:1884-1997): one mouse move */
static int perp_compute(const double cur[3], int ctrl) {
    double tv[3]; w2v_(cur, tv);
    int a84 = p_lock, old = p_orient, a86 = 0;
    if (!a84 || p_first) a86 = dc_perp_pick(pn.mode, (const uint8_t (*)[0x98])pn.aps, dr_view, tv, pn.idx, pn.ap1, pn.ap2);   /* FUN_180047aa0 */
    double p1[3], p2[3], w[3];
    memcpy(w, pn.ap1 + 8, 24); w2v_(w, p1); memcpy(w, pn.ap2 + 8, 24); w2v_(w, p2);
    if (a86 || p_first) {
        int h = dc_angle_hv_rel(dc_angle_of_line(p1, p2), p_hvp);   /* FUN_180047ca0 (asm 0x180047cca: unsigned < 2) */
        if (h == 0 || h == 1) { a84 = 1; p_orient = h; }
        int hv = dc_line_hv(p1, p2, p_hvp);             /* determine_h_or_v (core_create.c 3-44) */
        p_a87 = hv >= 0; if (hv >= 0) p_orient = hv;
    }
    if (!a84 && !p_a87) p_orient = dc_determine_hvp0(p1, p2, tv, old, p_hvp);   /* determine_hvp(.., sVar1, 0) */
    memcpy(pn.cursor, cur, sizeof pn.cursor); pn.view = dr_view_no(); memcpy(pn.vm, dr_view, sizeof pn.vm);
    pn.orient = p_orient; pn.hvp = p_hvp; pn.ctrl = ctrl; pn.tmpl = NULL;
    DcSettings S = dr_S; S.sel_dist = sel_dist_w(); S.color = geo_color; S.level = geo_level;
    dc_init_dim(dr_drag, &S, dr_view_no());
    DcResult R = dc_calc_perp(dr_drag, &pn, &S, dr_fonts_get());
    if (R.not_ported) { snprintf(cam_msg, sizeof cam_msg, "%s", R.why ? R.why : "NOT PORTED"); dr_drag_ok = 0; dr_scene(); return -1; }
    p_orient = R.orient;                                /* calc_linear_dimension's *param_6 = DAT_1806496a8 */
    if (R.swapped) { uint8_t t[0x98]; memcpy(t, pn.ap1, 0x98); memcpy(pn.ap1, pn.ap2, 0x98); memcpy(pn.ap2, t, 0x98); }
    dr_drag_ok = R.valid;
    if (!R.valid) {                                     /* 27010 + FUN_180047210 (phantom off, drag erased, DfHvpAngle 0) */
        snprintf(cam_msg, sizeof cam_msg, "Warning - cannot calculate dimension");
        dt_n = 0; dr_npick = 0; dr_placing = 0; p_hvp = 0.0; dr_scene();
        return 0;
    }
    p_first = 0;
    dr_scene();
    return 1;
}

static int compute(const double cur[3], int ctrl) { return is_tan() ? tan_compute(cur, ctrl) : perp_compute(cur, ctrl); }
int dt_move(const double world[3], int ctrl) {
    if (!dr_placing) return 0;
    double c[3] = { world[0], world[1], grid_z() };
    if (dt_have_cur) memcpy(dt_prev, dt_cur, sizeof dt_prev); else memcpy(dt_prev, c, sizeof dt_prev);
    memcpy(dt_cur, c, sizeof dt_cur); dt_have_cur = 1;
    compute(c, ctrl);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ picks */
/* the entity under the cursor of get_event's mask (pick_entity's rule, cui_sel.c: the nearest within the aperture; ours as dr_pick_line /
   dd_pick_arc_any, over the classes of MASK: 2 lines, 4 arcs) */
static int pick_ent(float mx, float my, int mask) {
    int best = -1; double bd = SEL_APERTURE_PX;
    for (int e = 0; e < F.nents; e++) {
        const Ent *en = &F.ents[e]; if (en->blanked || !ENT_LV_SHOWN(en) || !((en->type == E_LINE && (mask & 2)) || (en->type == E_ARC && (mask & 4)))) continue;
        if (en->type == E_LINE) {
            float ax, ay, bx, by;
            if (!scene_to_screen(vp_w, vp_h, en->a.x, en->a.y, en->a.z, &ax, &ay) || !scene_to_screen(vp_w, vp_h, en->b.x, en->b.y, en->b.z, &bx, &by)) continue;
            double d = seg_dist(mx, my, ax + vp_x, ay + vp_y, bx + vp_x, by + vp_y); if (d < bd) { bd = d; best = e; }
            continue;
        }
        for (int pi = en->first_poly; pi < en->first_poly + en->npoly; pi++) {
            const Poly *p = &F.polys[pi]; float ax = 0, ay = 0;
            for (int k = 0; k < p->count; k++) {
                V3 q = F.pts[p->first + k]; float sx, sy;
                if (!scene_to_screen(vp_w, vp_h, q.x, q.y, q.z, &sx, &sy)) continue;
                sx += vp_x; sy += vp_y;
                if (k) { double d = seg_dist(mx, my, ax, ay, sx, sy); if (d < bd) { bd = d; best = e; } }
                ax = sx; ay = sy;
            }
        }
    }
    return best;
}
/* a line picked (FUN_18004a620 case 2 / FUN_18004b070, cad_linear.c:2934-2964 / 3240-3300): 27013 when its ends coincide in the view
   (FUN_18012be10); slots S / S+1 = '1' / '2' (t 0 / 1, the line's eptr), 'K' when DfAssoc is off */
static int line_slots(int e, int s) {
    const Ent *en = &F.ents[e];
    double a[3] = { en->a.x, en->a.y, en->a.z }, b[3] = { en->b.x, en->b.y, en->b.z }, av[3], bv[3];
    w2v_(a, av); w2v_(b, bv);
    if (fabs(av[0] - bv[0]) < 5e-5 && fabs(av[1] - bv[1]) < 5e-5) { snprintf(cam_msg, sizeof cam_msg, "Line is parallel to Z axis."); return 0; }
    dc_apoint_world(dt_slot[s], a, dr_assoc ? '1' : 'K'); dc_apoint_world(dt_slot[s + 1], b, dr_assoc ? '2' : 'K');
    dt_sent[s] = dt_sent[s + 1] = -1;
    if (dr_assoc) { dc_apoint_ent(dt_slot[s], 0, 0, 0.0); dc_apoint_ent(dt_slot[s + 1], 0, 0, 1.0); tok(dt_slot[s], e); tok(dt_slot[s + 1], e); dt_sent[s] = dt_sent[s + 1] = e; }
    return 1;
}
/* an arc picked: views_parallel(arc view, active view) else 27012 (FUN_18004a620 case 4 / FUN_18004b070; arcs of other views: NOT PORTED,
   refused as cui_draft_dim.c); slot = 'C' at the centre (aPointInitView(arc view, +0x98)) + aPointSaveEntSel(slot 0, the arc, the
   event's parameter: NOT DECODED - 0 here; calc_tangent_dimension rebuilds +0x70 / +0x108 with t 0 anyway) */
static int arc_slot(int e, int s) {
    const Ent *en = &F.ents[e];
    if (en->vw || !en->planar) { snprintf(cam_msg, sizeof cam_msg, "Warning - arc is not parallel to construction plane."); return 0; }   /* 27012 */
    double c[3] = { en->c.x, en->c.y, en->c.z };
    dc_apoint_world(dt_slot[s], c, 'C'); dc_apoint_ent(dt_slot[s], 0, 0, 0.0); tok(dt_slot[s], e); dt_sent[s] = e;
    return 1;
}
/* a point event (FUN_180049ee0, cad_linear.c:615-760; a point entity picked = the same with kind 'P', FUN_18004b070): tangent, 1 pick ->
   slot 1, FUN_18004f420(2): mode 2; perpendicular, 1 pick -> slot 2, FUN_180048330(1): mode 1. No point events at 0 picks (flags 10) */
static void point_event(const uint8_t ap[0x98], int ent) {
    if (dt_n != 1) return;
    if (is_tan()) { memcpy(dt_slot[1], ap, 0x98); dt_sent[1] = ent; tan_setup(2, dt_ent0, -1); }
    else { memcpy(dt_slot[2], ap, 0x98); dt_sent[2] = ent; perp_setup(1); }
}
/* the second pick's entity event (FUN_18004b070, cad_linear.c:3202-3520) */
static void entity_event2(int e) {
    const Ent *en = &F.ents[e];
    if (en->type == E_LINE) {
        if (is_tan()) { if (line_slots(e, 1)) tan_setup(4, dt_ent0, -1); return; }   /* slots 1 / 2, FUN_18004f420(4) */
        if (!line_slots(e, 2)) return;
        /* (type - 1) & 0xfffb == 0: l_ints(line 1, line 2) in the active view; parallel (none) -> FUN_180048330(2); an intersection: the pick
           is refused without a message (UnSelectEntity, -1) - the slots stay overwritten (as compiled) */
        double p[4][3]; for (int k = 0; k < 4; k++) { double w[3]; memcpy(w, dt_slot[k] + 8, 24); w2v_(w, p[k]); }
        double L1[4] = { p[0][0], p[0][1], p[1][0], p[1][1] }, L2[4] = { p[2][0], p[2][1], p[3][0], p[3][1] }, ip[2];
        if (!dr_l_ints(L1, L2, ip)) perp_setup(2);
        return;
    }
    if (en->type == E_ARC && is_tan()) {               /* the same arc (eptr == DAT_180696110): refused without a message */
        if (e == dt_ent0) return;
        if (arc_slot(e, 1)) tan_setup(1, dt_ent0, e);
    }
}
/* a left click: placement -> event 4 (tangent FUN_18004ec40 -> FUN_18004df60(1); perpendicular FUN_180048180 -> FUN_180047770(1): no
   drag-edit entity -> UndoTransStart, df_save_drag_ent, UndoTransCommit = dr_store); else the get_event of FUN_180049c60 (FUN_1804e3b90:
   with point events (flags 0xb) the AutoCursor point wins, then an entity of the mask, then a 'K' point; flags 10: entities only) */
int dt_click(float mx, float my, const double w[3], int ctrl) {
    if (dr_placing) {
        if (!dr_drag_ok) { double c[3]; dr_sketched(mx, my, w, c); if (compute(c, ctrl) <= 0) return 1; }
        live_type = is_tan() ? 6 : 5; tn_live = tn; pn_live = pn;
        dr_store(); dt_restart();
        return 1;
    }
    if (dr_live >= 0) dr_commit_live();                 /* a new pick commits the live entity (cad_rundraft.c:3404-3434) */
    if (dt_n == 0) {                                    /* flags 10: entity events only */
        int e = pick_ent(mx, my, is_tan() ? 4 : 2);
        if (e >= 0) {
            if (is_tan() ? arc_slot(e, 0) : line_slots(e, 0)) { dt_ent0 = e; dt_n = 1; dr_npick = 1; dt_prompt(); }
        }
        dr_scene();
        return 1;
    }
    V3 p = { w[0], w[1], grid_z() };
    geo_snap_ptr(mx, my); int sn = !ctrl && geo_snap(&p, NULL, -1, 1); geo_snap_ptr_off();   /* FUN_1804e27d0: the AutoCursor */
    uint8_t ap[0x98]; int ent = -1;
    if (sn) { dr_ac_apoint(ap, &ent, p); if (ent >= 0) tok(ap, ent); point_event(ap, ent); }
    else {
        int e = pick_ent(mx, my, is_tan() ? 6 : 2);      /* mask 7 / 3 (point entities come through the AutoCursor's 'P') */
        if (e >= 0) entity_event2(e);
        else { double q[3] = { w[0], w[1], grid_z() }; dc_apoint_world(ap, q, 'K'); point_event(ap, -1); }
    }
    if (!dr_placing && strncmp(cam_msg, "Warning", 7) && strncmp(cam_msg, "Line", 4)) dt_prompt();
    dr_scene();
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the function panel */
/* refresh: tangent FUN_18004e6c0 (ids 1 arrows, 2 witness, 3 centred, 0xf box, 0xc digits, 5 / 6 the orientation, 7 lock, 0x12 (not mode
   2) = DAT_1806496ee, 8 angle; FUN_180038bb0: 0x1a Maintain, 0x1b Edit Mode), perpendicular FUN_180047f40 (1, 2, 3, 0xf, 0xc, 7). Keys:
   FUN_18004e7f0 / FUN_180048020 after FUN_18012af20 (common). Live here: Leaders _Inside / _Outside (1), _Centered text (3), Loc_k (7),
   and for the tangent the Mode _Horizontal / _Vertical (5 / 6: orientation, lock on, recompute). Greyed: witness (the Both / Right / Left
   order is a hypothesis, create.md 2.5), De_cimal places (0xc -> FUN_18012bca0: NOT DECODED), Orientation an_gle (8 -> FUN_18004eca0 with
   the panel's Maintain value: decoded, not wired - ours), _Maintain perpendicular to arc center (26: common FUN_18012af20; its effect
   reaches the drag only through id 8), 0x12 (the second side's flip, FUN_18004f910: which panel control posts 18 NOT DECODED), the rest
   as Circular. After a key: FUN_18004f850 / FUN_1800483c0 recompute at the cursor before the last move (DAT_1806a0ec0 / DAT_180695f60) */
static int pa_arrows, pa_center, pa_lock, pa_mode = -1, pa_dummy;
void dt_panel_body(const CuiPanelOps *ops, void *u) {
    int lock = is_tan() ? t_lock : p_lock;
    pa_arrows = dr_S.arrows_out == 1; pa_center = dr_S.auto_align; pa_lock = lock;
    pa_mode = is_tan() && dr_placing ? (t_orient == 1 ? 2 : 1) : -1;
    int t = -1, dg = dr_S.digits, mt = t_maintain; double ang = 0;
    if (!ops->tab || ops->tab(u, 0, "Basic")) {
        if (ops->exp(u, "Entity")) { ops->label(u, "Mode:"); ops->radio(u, "_Auto", &pa_mode, 0, 0);
            ops->radio(u, "_Horizontal", &pa_mode, 1, is_tan() && dr_placing); ops->radio(u, "_Vertical", &pa_mode, 2, is_tan() && dr_placing);
            ops->num(u, "Orientation an_gle:", "angle", &ang, NULL, 0, -360, 360); ops->check(u, "_Maintain perpendicular to arc center", &mt, 0);
            ops->check(u, "Loc_k", &pa_lock, 1); ops->end(u); }
        if (ops->exp(u, "Arc Symbol")) { ops->radio(u, "Radi_us", &t, 0, 0); ops->radio(u, "_Diameter", &t, 1, 0); ops->check(u, "A_pply to linear dimension", &pa_dummy, 0); ops->end(u); }
        if (ops->exp(u, "_Witness lines")) { ops->radio(u, "_Both", &t, 0, 0); ops->radio(u, "_Right", &t, 1, 0); ops->radio(u, "_Left", &t, 2, 0); ops->end(u); }
        if (ops->exp(u, "Leaders")) { ops->label(u, "Arrows:"); ops->radio(u, "_Inside", &pa_arrows, 0, 1); ops->radio(u, "_Outside", &pa_arrows, 1, 1); ops->end(u); }
        if (ops->exp(u, "Format")) { ops->toggle(u, "_Font", 1, -1); ops->toggle(u, "_Edit text", 1, -1); ops->toggle(u, "Heigh_t", 1, -1);
            ops->inum(u, "De_cimal places:", "digits", &dg, NULL, 0, 0, 8); ops->end(u); }
    }
    if (!ops->tab || ops->tab(u, 1, "Advanced")) {
        if (ops->exp(u, "Dimension Text")) { ops->check(u, "_Centered text", &pa_center, 1); ops->label(u, "Edit Mode:");
            ops->radio(u, "P_lace", &t, 0, 0); ops->radio(u, "_Edit", &t, 1, 0); ops->end(u); }
        if (ops->exp(u, "Dimension Box")) { ops->check(u, "_Show when dragging", &pa_dummy, 0); ops->end(u); }
        if (ops->exp(u, "Settings")) { ops->toggle(u, "_Options", 1, -1); ops->toggle(u, "_Update", 1, -1); ops->end(u); }
    }
}
void dt_panel_sync(void) {
    int ch = 0, re = 0;
    if ((pa_arrows ? 1 : 0) != (dr_S.arrows_out == 1)) { dr_S.arrows_out = pa_arrows ? 1 : 0; ch = 1; }   /* FUN_18012af20 case 1 */
    if (pa_center != dr_S.auto_align) { dr_S.auto_align = pa_center; ch = 1; }                             /* case 3 */
    if (is_tan()) {
        if (pa_lock != t_lock) { t_lock = pa_lock; re = 1; }                                                /* FUN_18004e7f0 case 7 */
        if (dr_placing && pa_mode >= 1 && pa_mode - 1 != t_orient) { t_orient = pa_mode - 1; t_force = 1; t_lock = 1; re = 1; }   /* cases 5 / 6 */
    } else if (pa_lock != p_lock) { p_lock = pa_lock; re = 1; }                                             /* FUN_180048020 case 7 */
    if (dr_placing && dt_have_cur && (ch || re)) { compute(dt_prev, 0); return; }   /* FUN_18004f850 / FUN_1800483c0 (param_7 = 1) */
    if (!ch) return;
    if (dr_live >= 0 && !dr_placing && F.dnew[dr_live].E[0x308] == live_type) {    /* the live entity recomputed (as cui_draft.c) */
        DcSettings S = dr_S; S.sel_dist = sel_dist_w(); S.color = F.dnew[dr_live].E[6]; S.level = (int)F.dnew[dr_live].E[8];
        uint8_t E[0x810]; memcpy(E, F.dnew[dr_live].E, 0x810);
        DcResult R = live_type == 6 ? dc_calc_tangent(E, &tn_live, &S, dr_fonts_get()) : dc_calc_perp(E, &pn_live, &S, dr_fonts_get());
        if (R.valid) memcpy(F.dnew[dr_live].E, E, 0x810);
        dr_scene();
    }
}

/* ------------------------------------------------------------------------------------------------ tests (ours) */
static double D8(const uint8_t *b, int o) { double v; memcpy(&v, b + o, 8); return v; }
static int I16_(const uint8_t *b, int o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static const uint8_t *file_dim(DraftDoc *dd, int type, int want, int *count) {   /* the open part's dimension records of TYPE */
    int k = 0; const uint8_t *hit = NULL; uint32_t n;
    for (int i = 0; i < draft_count(dd); i++) { const uint8_t *E = draft_record(dd, i, &n);
        if (E && n == 0x810 && E[0] == 0x40 && E[0x308] == type) { if (k == want) hit = E; k++; } }
    if (count) *count = k;
    return hit;
}
/* the session globals from a record (DraftGlobalsFromDim @180308cc0 core_draftwf.c 540-724: + DfMaintainTang = +0x248 for type 6) */
static void globals_from(const uint8_t *Es) {
    DcSettings *S = &dr_S;
    memcpy(S->fa, Es + 0x5d8, 0x60);
    const uint8_t *l1 = Es + 0x4a8, *l2 = Es + 0x540, *l = l1[0] && l1[0x18] != 3 ? l1 : l2[0] && l2[0x18] != 3 ? l2 : l1[0] ? l1 : l2;
    S->arrow_h = D8(l, 8); S->arrow_w = D8(l, 0x10); S->arrow_type = l[0x18];
    S->arrows_out = I16_(Es, 0x7b0); S->ldr_style = I16_(Es, 0x7b4); S->wit_vis = I16_(Es, 0x7ac); S->ldr_vis = I16_(Es, 0x7ae);
    S->text_orient = fabs(D8(Es, 0x5e8)) < 5e-5; S->auto_align = Es[0x7c9];
    S->gap = D8(Es, 0x350); S->ext = D8(Es, 0x358); S->scale = D8(Es, 0x330); S->frac_units = I16_(Es, 0x7c2); S->lead0 = Es[0x7cc]; S->trail0 = Es[0x7cf]; S->comma = Es[0x7cb];
    S->digits = Es[0x7cd]; S->coord_fmt = I16_(Es, 0x7c0);
    if (Es[0x308] == 6) t_maintain = Es[0x248];
    dr_saved = dr_S;
}
static void cmp_last(int type, int want) {
    DraftDoc *dd = draft_load(F.path); int n = 0; const uint8_t *Es = file_dim(dd, type, want, &n);
    if (!Es || !F.ndnew) { printf("    %scmp: nothing to compare (%d file dims, %d made)\n", type == 6 ? "tan" : "perp", n, F.ndnew); draft_free(dd); return; }
    const uint8_t *Eo = F.dnew[F.ndnew - 1].E; int nd = 0, nref = 0; char why[2000] = ""; size_t L = 0;
#define DF(...) do { nd++; L += (size_t)snprintf(why + L, L < sizeof why ? sizeof why - L : 0, __VA_ARGS__); } while (0)
#define CLOSE(a, b) (fabs((a) - (b)) <= 1e-6 * (1 + fabs(b)))
    for (int j = 0; j < (type == 5 ? 4 : 3); j++) { const uint8_t *a = Eo + 0x70 + 0x98 * j, *b = Es + 0x70 + 0x98 * j;
        for (int k = 0; k < 3; k++) if (!CLOSE(D8(a, 8 + 8 * k), D8(b, 8 + 8 * k))) { DF(" aP%d position;", j); break; }
        if (a[0] != b[0] || memcmp(a + 0x28, b + 0x28, 8)) nref++; }
    if (!CLOSE(D8(Eo, 0x338), D8(Es, 0x338))) DF(" value %.6f/%.6f;", D8(Eo, 0x338), D8(Es, 0x338));
    if (strncmp((const char *)Eo + 0x650, (const char *)Es + 0x650, 0x58)) DF(" text '%.20s'/'%.20s';", Eo + 0x650, Es + 0x650);
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x638 + 8 * k), D8(Es, 0x638 + 8 * k))) { DF(" textpos (%.5f,%.5f)/(%.5f,%.5f);", D8(Eo, 0x638), D8(Eo, 0x640), D8(Es, 0x638), D8(Es, 0x640)); break; }
    if (!CLOSE(D8(Eo, 0x5e8), D8(Es, 0x5e8))) DF(" textangle;");
    static const int wo[4] = { 0x378, 0x410, 0x4a8, 0x540 }; static const char *wn[4] = { "wit1", "wit2", "ldr1", "ldr2" };
    for (int q = 0; q < 4; q++) { const uint8_t *a = Eo + wo[q], *b = Es + wo[q];
        int bad = a[0] != b[0] || a[1] != b[1] || a[3] != b[3] || a[0x18] != b[0x18] || I16_(a, 0x1a) != I16_(b, 0x1a);
        int nv = (I16_(b, 0x1a) > 4 ? 4 : I16_(b, 0x1a)) * 3;
        for (int i = 0; !bad && i < nv; i++) bad = !CLOSE(D8(a, 0x20 + 8 * i), D8(b, 0x20 + 8 * i));
        if (bad) DF(" %s;", wn[q]); }
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x310 + 8 * k), D8(Es, 0x310 + 8 * k))) { DF(" placement;"); break; }
    for (int k = 0; k < 3; k++) if (!CLOSE(D8(Eo, 0x360 + 8 * k), D8(Es, 0x360 + 8 * k))) { DF(" offsets;"); break; }
    if (type == 6 && (Eo[0x68] != Es[0x68] || Eo[0x238] != Es[0x238] || Eo[0x249] != Es[0x249] || Eo[0x24a] != Es[0x24a] || Eo[0x24b] != Es[0x24b] || !CLOSE(D8(Eo, 0x240), D8(Es, 0x240))))
        DF(" mode %d/%d orient %d/%d anchor %d/%d sides %d,%d/%d,%d;", Eo[0x68], Es[0x68], Eo[0x238], Es[0x238], Eo[0x249], Es[0x249], Eo[0x24a], Eo[0x24b], Es[0x24a], Es[0x24b]);
    if (type == 5 && (Eo[0x68] != Es[0x68] || Eo[0x2d0] != Es[0x2d0] || Eo[0x2d1] != Es[0x2d1] || Eo[0x2d2] != Es[0x2d2] || Eo[0x2e0] != Es[0x2e0]))
        DF(" mode / idx / orient / anchor;");
#undef DF
#undef CLOSE
    printf("    %scmp %d: %s%s%s\n", type == 6 ? "tan" : "perp", want, nd ? "DIFFER" : nref ? "EQUAL-LAYOUT" : "EQUAL", why, nref ? " (aPoint references differ)" : "");
    draft_free(dd);
}
/* tanglobals:N / perpglobals:N (the session globals from the open part's tangent / perpendicular record N, DraftGlobalsFromDim),
   tanredo:N / perpredo:N (a placement mouse move at the point the regeneration FUN_180305ca0 / FUN_1803056d0 computes for record N
   from the session's picks), tancmp:N / perpcmp:N (the last dimension made against record N), tanmaint:0|1 (DfMaintainTang) */
int dt_script(const char *st, const char *arg) {
    int tan = !strncmp(st, "tan", 3), perp = !strncmp(st, "perp", 4);
    if (!tan && !perp) return 0;
    const char *v = st + (tan ? 3 : 4);
    if (strcmp(v, "globals") && strcmp(v, "redo") && strcmp(v, "cmp") && strcmp(v, "maint")) return 0;
    if (!arg) { printf("    (%s: argument missing)\n", st); return 1; }
    if (!strcmp(v, "maint")) { t_maintain = atoi(arg) != 0; return 1; }
    if (!strcmp(v, "cmp")) { if (loaded && F.path[0]) cmp_last(tan ? 6 : 5, atoi(arg)); else printf("    (no part file)\n"); return 1; }
    DraftDoc *dd = loaded && F.path[0] ? draft_load(F.path) : NULL; const uint8_t *Es = file_dim(dd, tan ? 6 : 5, atoi(arg), NULL);
    if (!Es) printf("    (no %s dimension %s)\n", tan ? "tangent" : "perpendicular", arg);
    else if (!strcmp(v, "globals")) { dr_globals_load(); globals_from(Es); }
    else if (!dr_placing || is_tan() != tan) printf("    (%s: no placement)\n", st);
    else if (tan) {
        double cw[3];
        if (dc_tan_regen_cursor(Es, tn.arc, (const uint8_t (*)[0x98])tn.slot, dr_assoc, cw)) dt_move(cw, 0);
        else printf("    (tanredo: calc_tangent_eps failed)\n");
    } else {                                            /* FUN_1803056d0's cursor over the session's aPoints */
        double h, cw[3];
        if (dc_perp_regen_cursor(Es, (const uint8_t (*)[0x98])pn.aps, dr_view, &h, cw)) dt_move(cw, 0);
        else printf("    (perpredo: the two lines intersect)\n");
    }
    draft_free(dd); return 1;
}
