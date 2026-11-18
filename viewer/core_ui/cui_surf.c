/* cui_surf.c - the Surfaces tab (formats/surfaces_tab_plan.md): its commands by FT function, the surface edit transaction
   (undo) and the commands ported so far.
   - Normals > Change (mccadutil change_normals 0x18005b6b0, decomp/surfedit/normals.c): see srf_chnorm_pick. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mcsurf.h"
#include "mcsurf_build.h"
#include "mcsrfcrv.h"
#include "occt_prim.h"
#include "occt_cad.h"
#include "cad_import.h"
#include "mcam_saveops.h"
#include "core_ui.h"

/* every Surfaces-tab FT function (viewer/ui/layout.txt, TAB Surfaces): the wireframe / transform commands never take these, even when
   a label matches (Surfaces > Offset is not the wireframe Offset; Surfaces > Extrude not Solids > Extrude) */
static const char *SRF_FTS[] = {
    "create_cylinder_surface", "create_block_surface", "create_sphere_surface", "create_cone_surface", "create_torus_surface",
    "sld_convert_to_surfs", "FlatBoundarySurfaces", "RuledLoftedSurfaces", "ExtrudeSurface", "SweptSurfaces", "RevolvedSurfaces",
    "DraftSurface", "NetSurfCreation", "FenceSurfCreation", "OffsetSurfaces", "trim_surface_1", "trim_surface_2", "trim_surface_3",
    "FillHoles", "SurfaceExtend", "ExtendTrimmedEdge", "fillet_surface_1", "fillet_surface_2", "fillet_surface_3", "blend_2_surfs",
    "blend_3_surfs", "blend_3_fils", "UnTrimSurfaces", "RemoveBoundary", "SplitSurface", "change_normals", "set_normals", "orient_normals" };
int srf_is_ft(const char *ft) {
    for (size_t i = 0; ft && i < sizeof SRF_FTS / sizeof SRF_FTS[0]; i++) if (!strcmp(ft, SRF_FTS[i])) return 1;
    return 0;
}
/* the ported ones */
static const struct { const char *ft; int cmd; } SRF_CMDS[] = { { "change_normals", SRF_CHNORM }, { "RevolvedSurfaces", SRF_REVOLVE },
    { "create_block_surface", SRF_BLOCK }, { "create_cylinder_surface", SRF_CYL },
    { "create_sphere_surface", SRF_SPH }, { "create_cone_surface", SRF_CONE }, { "create_torus_surface", SRF_TOR },
    { "FlatBoundarySurfaces", SRF_FLAT }, { "sld_convert_to_surfs", SRF_SOLID },
    { "RuledLoftedSurfaces", SRF_LOFT },
    { "set_normals", SRF_SNSET }, { "UnTrimSurfaces", SRF_UNTRIM }, { "RemoveBoundary", SRF_RMBND },   /* cui_srfedit.c (G_SNSET / G_SUNTRIM) */
    { "SurfaceExtend", SRF_EXT }, { "trim_surface_1", SRF_TRIMS },   /* cui_srftrim.c */
    { "OffsetSurfaces", SRF_OFFSET } };   /* cui_srfedit.c (G_SOFFSET) */
static const struct { const char *ft; int cmd; } SRF_CMDS_CREATE[] = { { "DraftSurface", SRF_DRAFT }, { "ExtrudeSurface", SRF_EXTRUDE } };   /* Draft / Extrude */
int srf_find_ft(const char *ft) {
    for (size_t i = 0; ft && i < sizeof SRF_CMDS / sizeof SRF_CMDS[0]; i++) if (!strcmp(ft, SRF_CMDS[i].ft)) return SRF_CMDS[i].cmd;
    for (size_t i = 0; ft && i < sizeof SRF_CMDS_CREATE / sizeof SRF_CMDS_CREATE[0]; i++) if (!strcmp(ft, SRF_CMDS_CREATE[i].ft)) return SRF_CMDS_CREATE[i].cmd;
    return 0;
}

int srf_cmd;                                     /* the running Surfaces command (SRF_*), 0 none */
static int rev_axis = -1; static double rev_t;   /* Revolve's axis line (Ent index) and the pick's parameter along it */

/* ------------------------------------------------------------------------------------------------ edit transaction */
/* the records a command rewrote (put_special_undo(3, ent) before each rewrite_ent; UndoTransStart / UndoTransCommit around the
   command = one undo step) */
typedef struct { int n; int *srf; uint8_t **old, **neu; uint32_t *len; int *ed0; } SurfEdits;
static SurfEdits *tx;
void srf_edits_free(void *p) {
    SurfEdits *x = p; if (!x) return;
    for (int i = 0; i < x->n; i++) { free(x->old[i]); free(x->neu[i]); }
    free(x->srf); free(x->old); free(x->neu); free(x->len); free(x->ed0); free(x);
}
static void tx_save_old(int s) {                 /* put_special_undo(3): the record before the first change in this transaction */
    if (!tx) tx = calloc(1, sizeof *tx);
    for (int i = 0; i < tx->n; i++) if (tx->srf[i] == s) return;
    int n = tx->n + 1; const Surf *sf = &F.surfs[s];
    tx->srf = realloc(tx->srf, sizeof(int) * n); tx->old = realloc(tx->old, sizeof(void *) * n); tx->neu = realloc(tx->neu, sizeof(void *) * n);
    tx->len = realloc(tx->len, sizeof(uint32_t) * n); tx->ed0 = realloc(tx->ed0, sizeof(int) * n);
    tx->srf[tx->n] = s; tx->old[tx->n] = malloc(sf->n); memcpy(tx->old[tx->n], sf->rec, sf->n); tx->neu[tx->n] = NULL;
    tx->len[tx->n] = sf->n; tx->ed0[tx->n] = sf->edited; tx->n = n;
}
static void tx_commit(void) {                    /* UndoTransCommit: one step with every record's before / after */
    if (!tx) return;
    for (int i = 0; i < tx->n; i++) { const Surf *sf = &F.surfs[tx->srf[i]]; tx->neu[i] = malloc(sf->n); memcpy(tx->neu[i], sf->rec, sf->n); }
    URec r; memset(&r, 0, sizeof r); r.kind = U_SURFEDIT; r.sx = tx; tx = NULL;
    undo_push(r); doc_dirty = 1;
}
/* the transaction for other editors (Analyze): save a record before its first change, commit (one undo step), cancel (restore) */
void srf_tx_save(int s) { if (s >= 0 && s < F.nsurfs) tx_save_old(s); }
void srf_tx_commit(void) { tx_commit(); }
void *srf_tx_take(void) { void *p = tx; if (tx) for (int i = 0; i < tx->n; i++) { const Surf *sf = &F.surfs[tx->srf[i]]; tx->neu[i] = malloc(sf->n); memcpy(tx->neu[i], sf->rec, sf->n); } tx = NULL; return p; }   /* the after-images taken, as tx_commit */
void srf_tx_cancel(void) { if (!tx) return; for (int i = 0; i < tx->n; i++) tx->neu[i] = NULL; srf_edits_apply(tx, 1); srf_edits_free(tx); tx = NULL; }
void srf_edits_apply(void *p, int undo) {
    SurfEdits *x = p; if (!x) return;
    for (int i = 0; i < x->n; i++) {
        Surf *sf = &F.surfs[x->srf[i]]; if (sf->n != x->len[i]) continue;
        memcpy(sf->rec, undo ? x->old[i] : x->neu[i], sf->n);
        sf->edited = undo ? x->ed0[i] : 1;
        sf->blanked = (sf->rec[4] | sf->rec[5] << 8) & 0x100 ? 1 : 0;
        if (sf->ent >= 0 && sf->ent < F.nents) mcam_ent_blank(&F, sf->ent, sf->blanked);   /* the Ent's blanked flag follows the record (Untrim unblanks a base) */
        if (sf->ent >= 0 && sf->ent < F.nents) { int32_t lv; memcpy(&lv, sf->rec + 8, 4); F.ents[sf->ent].color = sf->rec[6]; F.ents[sf->ent].level = lv; sf->color = sf->rec[6]; sf->level = lv; }   /* attributes edited in the record */
    }
    scene_surfs_update(1);
}

/* ------------------------------------------------------------------------------------------------ the normal arrow */
/* auto_highlight_normal_arrow (mastercam.dll 0x180208b50, decomp/surfedit/mc_arrows.c): the arrow at the surface's parametric middle
   ((u0 + u1) / 2, (v0 + v1) / 2), along calc_surf_unit_norm (mccore 0x180164290: N = Su x Sv normalised, decomp/surfedit/core_norm.c),
   reversed when the surf header's +6 bit 4 (ent +0x6e) is set. DisplayArrow(p, n, 0, 0.1, -2, ...) -> FUN_1801ff3a0: colour -2 = 4
   when gr_back_color is 2 or 10, else 0x56; length = get_x_scr_scl * 0.1 * the view's +0x760 [the two scale factors NOT DECODED:
   ours, a tenth of the view height]. Ours: derivatives by central differences (as mcsurf.c); calc_surf_norm's degenerate-point
   fallback (stepping 1/100 of the range inward) is not ported - no arrow there. */
static int arrow_s = -1;                         /* the surface whose arrow is shown, -1 none */
static int srf_mid_normal(int s, double p[3], double n[3]) {
    const Surf *sf = &F.surfs[s]; double d[4]; mcs_surf_domain(sf, d);
    double u = (d[0] + d[1]) * 0.5, v = (d[2] + d[3]) * 0.5, hu = (d[1] - d[0]) * 1e-5, hv = (d[3] - d[2]) * 1e-5, a[3], b[3], c[3], e[3];
    if (mcs_surf_eval(&F, sf, u, v, p) || mcs_surf_eval(&F, sf, u - hu, v, a) || mcs_surf_eval(&F, sf, u + hu, v, b)
        || mcs_surf_eval(&F, sf, u, v - hv, c) || mcs_surf_eval(&F, sf, u, v + hv, e)) return 0;
    double su[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, sv[3] = { e[0] - c[0], e[1] - c[1], e[2] - c[2] };
    n[0] = su[1] * sv[2] - su[2] * sv[1]; n[1] = su[2] * sv[0] - su[0] * sv[2]; n[2] = su[0] * sv[1] - su[1] * sv[0];
    double l = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (l < 1e-300) return 0;
    double sg = (sf->rec[0x6e] & 4) ? -1.0 : 1.0;
    for (int k = 0; k < 3; k++) n[k] = n[k] / l * sg;
    return 1;
}
/* rev_dir_arrow (mcgeomsld 0x1800a5c60, decomp/surfrev/revarrow.c), shown while Revolve has its axis: a temporary full circle (colour
   10, radius 0.25 inch / 5 mm) about the axis at its base end (the pick's end, as create_revs), in the view make_view_k / find_view_m of
   the axis; the arrow (DisplayArrow 0.1, colour -2) at the circle's start along its tangent, reversed with Direction 2. Ours: the arrow
   length / head as srf_ov's normal arrow; the circle is an overlay, not a database entity. */
static void rev_arrow_ov(float **pv, int *pn, int *pcap) {
    if (geo_cmd != G_SREV || !srf_rev_axis_ok()) return;
    const Ent *L = &F.ents[rev_axis];
    V3 P = rev_t >= 0.5 ? L->b : L->a, Q = rev_t >= 0.5 ? L->a : L->b;
    double k[3] = { Q.x - P.x, Q.y - P.y, Q.z - P.z }, l = sqrt(k[0] * k[0] + k[1] * k[1] + k[2] * k[2]); if (l == 0) return;
    for (int i = 0; i < 3; i++) k[i] /= l;
    double Mv[9], y[3], x[3]; if (!mcsb_view_k(k, Mv)) return;   /* make_view_k */
    for (int i = 0; i < 3; i++) { x[i] = Mv[i]; y[i] = Mv[3 + i]; }
    double r = mcam_is_metric(&F) ? 5.0 : 0.25; float c[3]; mc_rgb(10, c);
    V3 prev = { 0 };
    for (int i = 0; i <= 64; i++) {
        double a = 2 * M_PI * i / 64; V3 q = { P.x + r * (cos(a) * x[0] + sin(a) * y[0]), P.y + r * (cos(a) * x[1] + sin(a) * y[1]), P.z + r * (cos(a) * x[2] + sin(a) * y[2]) };
        if (i) { ov_pt(pv, pn, pcap, prev, c[0], c[1], c[2]); ov_pt(pv, pn, pcap, q, c[0], c[1], c[2]); }
        prev = q;
    }
    double sg = gps[G_SREV].dir ? -1 : 1, L2 = scene_view_height() * 0.1, h = L2 * 0.2;
    float a3[3]; mc_rgb(cfg_back_color == 2 || cfg_back_color == 10 ? 4 : 0x56, a3);
    V3 s0 = { P.x + r * x[0], P.y + r * x[1], P.z + r * x[2] }, t = { s0.x + sg * y[0] * L2, s0.y + sg * y[1] * L2, s0.z + sg * y[2] * L2 };
    ov_pt(pv, pn, pcap, s0, a3[0], a3[1], a3[2]); ov_pt(pv, pn, pcap, t, a3[0], a3[1], a3[2]);
    for (int g = -1; g <= 1; g += 2) {
        V3 q = { t.x - sg * y[0] * h + g * x[0] * h * 0.5, t.y - sg * y[1] * h + g * x[1] * h * 0.5, t.z - sg * y[2] * h + g * x[2] * h * 0.5 };
        ov_pt(pv, pn, pcap, t, a3[0], a3[1], a3[2]); ov_pt(pv, pn, pcap, q, a3[0], a3[1], a3[2]);
    }
}
void srf_ov(float **pv, int *pn, int *pcap) {
    rev_arrow_ov(pv, pn, pcap);
    srf_sn_ov(pv, pn, pcap);                    /* Normals > Set: the listed surfaces' arrows (cui_srfedit.c) */
    srf_of_ov(pv, pn, pcap);                    /* Offset: Single flip / Cycle's arrows (cui_srfedit.c) */
    /* EnableAutoHighlightNormals(1): the auto-highlighted surface shows its arrow; else the last flipped one */
    int s = hover_ent >= 0 && hover_ent < F.nents && F.ents[hover_ent].type == E_SURF ? F.ents[hover_ent].srf : arrow_s;
    if (srf_cmd != SRF_CHNORM || s < 0 || s >= F.nsurfs) return;
    double p[3], n[3]; if (!srf_mid_normal(s, p, n)) return;
    float rgb[3]; mc_rgb(cfg_back_color == 2 || cfg_back_color == 10 ? 4 : 0x56, rgb);
    double L = scene_view_height() * 0.1, h = L * 0.2;
    V3 a = { p[0], p[1], p[2] }, t = { p[0] + n[0] * L, p[1] + n[1] * L, p[2] + n[2] * L };
    ov_pt(pv, pn, pcap, a, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, t, rgb[0], rgb[1], rgb[2]);
    /* the head (ours: GrArrow's shape is NOT DECODED): two strokes back from the tip in the plane of the view */
    double w[3] = { n[1] * vmat[8] - n[2] * vmat[7], n[2] * vmat[6] - n[0] * vmat[8], n[0] * vmat[7] - n[1] * vmat[6] };
    double wl = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (wl < 1e-9) { w[0] = vmat[0]; w[1] = vmat[1]; w[2] = vmat[2]; wl = 1; }
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        V3 q = { t.x - n[0] * h + sgn * w[0] / wl * h * 0.5, t.y - n[1] * h + sgn * w[1] / wl * h * 0.5, t.z - n[2] * h + sgn * w[2] / wl * h * 0.5 };
        ov_pt(pv, pn, pcap, t, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, q, rgb[0], rgb[1], rgb[2]);
    }
}

/* ------------------------------------------------------------------------------------------------ start / end */
static void srf_prompt(void) {
    /* mccadutilres 2383 (0x94f, change_normals' get_event prompt) */
    if (srf_cmd == SRF_CHNORM) snprintf(cam_msg, sizeof cam_msg, "Select a surface to flip the normal");
}
int srf_start(const char *ft) {
    int c = srf_find_ft(ft); if (!c) return 0;
    if (srf_cmd) srf_end();
    if (c == SRF_DRAFT) return geo_start("Draft Surface");         /* DraftSurface (G_SDRAFT; dialog mccadres 1084 title) */
    if (c == SRF_EXTRUDE) return geo_start("Extruded Surface");    /* ExtrudeSurface (G_SEXTR; dialog mccadres 1086 title) */
    if (c == SRF_REVOLVE) return geo_start("Revolved Surfaces");   /* a function-panel command in cui_geo.c (G_SREV) */
    if (c == SRF_BLOCK) return geo_start("Block");                 /* create_block (G_SBLOCK) */
    if (c == SRF_CYL) return geo_start("Cylinder");                /* create_cylinder (G_SCYL) */
    if (c == SRF_SPH) return geo_start("Sphere");                  /* create_sphere (G_SSPH) */
    if (c == SRF_CONE) return geo_start("Cone");                   /* create_cone (G_SCONE) */
    if (c == SRF_TOR) return geo_start("Torus");                   /* create_torus (G_STOR) */
    if (c == SRF_FLAT) return geo_start("Flat Boundary");          /* FlatBoundarySurfaces (G_SFLAT) */
    if (c == SRF_SOLID) return geo_start("Surface From Solid");    /* sld_convert_to_surfs (G_SSOLID) */
    if (c == SRF_LOFT) return geo_start("Ruled/Lofted Surfaces");  /* RuledLoftedSurfaces (G_SLOFT; mccoreres 4124) */
    if (c == SRF_SNSET) return geo_start("Set Normals");           /* set_normals (G_SNSET, cui_srfedit.c) */
    if (c == SRF_UNTRIM) return geo_start("Untrim");               /* UnTrimSurfaces (G_SUNTRIM, cui_srfedit.c) */
    if (c == SRF_RMBND) return geo_start("Remove Boundary");       /* RemoveBoundary (G_SRMBND, cui_srfedit.c) */
    if (c == SRF_EXT) return geo_start("Surface Extend");          /* SurfaceExtend (G_SEXT, cui_srftrim.c) */
    if (c == SRF_TRIMS) return geo_start("Trim to Surfaces");      /* trim_surface_1 (G_STRIMS, cui_srftrim.c) */
    if (c == SRF_OFFSET) return geo_start("Surface Offset");       /* OffsetSurfaces (G_SOFFSET, cui_srfedit.c) */
    srf_cmd = c; srf_pick_active = 1; arrow_s = -1;
    /* change_normals: UndoTransStart; EnableAutoHighlightNormals(1); surf_back_draw on while shading [backside drawing: ours draws
       both sides already] */
    srf_prompt(); overlay_dirty = 1;
    return 1;
}
void srf_end(void) {                             /* Esc (get_event: key event 8 / 0x4f ends the loop) */
    if (!srf_cmd) return;
    tx_commit();                                 /* UndoTransCommit */
    srf_cmd = 0; srf_pick_active = 0; arrow_s = -1; cam_msg[0] = 0;
    /* set_clr_sel_bits(0, 4, 0, 4, 0x100, 1, 0): the surfaces' selected bit off */
    for (int i = 0; ent_sel && i < ent_sel_n && i < F.nents; i++) if (ent_sel[i] && F.ents[i].type == E_SURF) { ent_sel[i] = 0; nselected--; }
    overlay_dirty = 1;
}

/* change_normals' pick (event 2 on a surface, get_event mask 0x100): put_special_undo(3); surf_flip_normal (mccore 0x1806fa810: the
   surf header's +6 bit 4 toggled = ent +0x6e); the selection word's bit 8 set -> off and bit 2 on, else bit 2 set -> off and bit 8 on;
   rewrite_ent; list_op_check_ent / mc_op_test_and_modify_entity (the operations using it: dirty_ent_users); then the arrow of the
   picked surface (auto_highlight_normal_arrow) */
static void srf_chnorm_pick(int e) {
    int s = F.ents[e].srf; if (s < 0 || s >= F.nsurfs) return;
    Surf *sf = &F.surfs[s]; if (sf->n < 0x70) return;
    tx_save_old(s);
    sf->rec[0x6e] ^= 4;
    uint16_t sb = (uint16_t)(sf->rec[4] | sf->rec[5] << 8);
    if (sb & 8) sb = (uint16_t)((sb & ~8) | 2); else if (sb & 2) sb = (uint16_t)((sb & ~2) | 8);
    sf->rec[4] = (uint8_t)sb; sf->rec[5] = (uint8_t)(sb >> 8);
    sf->edited = 1;
    dirty_ent_users(e);
    arrow_s = s; overlay_dirty = 1; doc_dirty = 1;
    scene_surfs_update(0);
}
int srf_pick_ent(int e) {                        /* the pick of entity e (tests: srfpick:N) */
    if (geo_cmd == G_SNSET && srf_sn_active()) return srf_sn_pick(e);    /* Normals > Set / Untrim (cui_srfedit.c) */
    if (geo_cmd == G_SUNTRIM && srf_ut_active()) return srf_ut_pick(e);
    if (geo_cmd == G_SOFFSET && srf_of_active()) return srf_of_pick(e);   /* Offset (cui_srfedit.c) */
    if (!srf_cmd || e < 0 || e >= F.nents || F.ents[e].type != E_SURF) return 0;
    if (srf_cmd == SRF_CHNORM) srf_chnorm_pick(e);
    srf_prompt(); return 1;
}
int srf_click(float mx, float my) {              /* a graphics click while a Surfaces command runs; 1 = taken */
    if (!srf_cmd) return 0;
    int e = pick_entity(mx, my);
    if (e < 0 || F.ents[e].type != E_SURF) return 1;   /* get_event's mask 0x100: nothing else is picked */
    if (srf_cmd == SRF_CHNORM) srf_chnorm_pick(e);
    srf_prompt();
    return 1;
}

/* ------------------------------------------------------------------------------------------------ Revolve (G_SREV in cui_geo.c) */
/* RevolvedSurfaces (mccad FUN_1802cc6c0, formats/surfaces_tab_plan.md "C. Revolve"): chain_it (mccadres 7418 "Select profile curve(s)"),
   then the axis (get_event mask 2 = lines, mccadres 7419 "Select the axis of rotation"; FUN_18008cc40 keeps the line and the pick's
   parameter), then FUN_18008c9e0 builds (create_revs) in the live colour. Panel (ISurfaceRevolvedManager, surfacerevolvedmanagerview.baml):
   Chains Reselect, Axis of Rotation Reselect, Start / End angle (FUN_1802af530 defaults 0 / 2 pi: degrees in the panel), Direction 1 /
   2 (Direction 2 = the axis reversed). */
int srf_rev_axis_ok(void) { return rev_axis >= 0 && rev_axis < F.nents && F.ents[rev_axis].type == E_LINE; }
void srf_rev_reset(void) { rev_axis = -1; }
int srf_rev_take(int e, V3 w) {                          /* the axis pick: a line only (get_event mask 2) */
    if (e < 0 || e >= F.nents || F.ents[e].type != E_LINE) return 0;
    const Ent *L = &F.ents[e];
    double d[3] = { L->b.x - L->a.x, L->b.y - L->a.y, L->b.z - L->a.z }, l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    rev_t = l2 > 0 ? ((w.x - L->a.x) * d[0] + (w.y - L->a.y) * d[1] + (w.z - L->a.z) * d[2]) / l2 : 0;   /* t_on_line of the pick */
    rev_axis = e; return 1;
}
/* the surface colour: put_surf_in_db gives surfaceFrontColor when the colour is main_color (not the background, not while reading /
   transforming); surfaceFrontColor = mastercam.dll .data 0x18087b86e = 56 (Home > Surface Color, FUN_1803af160 mode 2, sets it) */
int srf_front_color = 56;
int srf_surface_type(void) {                             /* config 10014 (mccore FUN_1801b7d30 case 0x271e): factory 2 (NURBS) */
    char v[32]; const char *s = mcam_config_value(mcam_is_metric(&F), 10014, v, sizeof v);
    return s && s[0] >= '0' && s[0] <= '2' ? s[0] - '0' : 2;
}
/* create_revs over every chain's entities except the axis: one surface each (mcam_new_surf); 0 = none made */
int srf_rev_exec(const Chain *ch, int nch, double sa_deg, double ea_deg, int dir2, int level, int color, char *msg, size_t msgn) {
    if (!srf_rev_axis_ok()) return 0;
    const V3 La = F.ents[rev_axis].a, Lb = F.ents[rev_axis].b;   /* the axis (copied: mcam_new_surf grows F.ents, a pointer would be stale
                                                                  for the second profile entity on) */
    const int64_t ae = F.ents[rev_axis].created ? F.ents[rev_axis].septr : F.ents[rev_axis].eptr;
    int st = srf_surface_type(), made = 0;
    int col = color == geo_color && color != cfg_back_color ? srf_front_color : color;
    double tol = mc_systol_units(mcam_is_metric(&F))->tol;
    for (int c = 0; c < nch; c++) for (int k = 0; k < ch[c].n; k++) {
        int e = ch[c].ent[k]; if (e < 0 || e >= F.nents || e == rev_axis || !F.ents[e].type) continue;
        const Ent *pf = &F.ents[e];
        uint32_t n; const char *err;
        int64_t pe = pf->created ? pf->septr : pf->eptr;
        uint8_t *r = mcsb_revolve(&F, pf, pe, La, Lb, ae, rev_t, sa_deg * M_PI / 180, ea_deg * M_PI / 180, dir2, st, solid_mode != 0, tol, &n, &err);
        if (!r) { if (msg && err) snprintf(msg, msgn, "Revolve: %s", err); continue; }
        mcam_new_surf(&F, r, n, level, col); free(r); made++;
    }
    if (made) scene_surfs_update(0);
    return made;
}

/* ------------------------------------------------------------------------------------------------ Draft (G_SDRAFT in cui_geo.c) */
static void cplane_dir(int k, double w[3]);
/* DraftSurface (mccad 0x1801273d0, decomp/surfcreate/create_cmds.c l.368; formats/surfaces_tab_plan.md "C. Draft"): chain_it (mccadres
   5731 "Select line, arc, or spline"), then FUN_180125d80 (decomp/surfextr/draftdlg.c) builds in a transaction: the angle a (the
   dialog's +0x5588, negated when the angle Flip state +0x5564 is 1), its slope FUN_180125d80: a = 90 -> 2e12, -90 / 270 -> -2e12,
   else -tan(a), negated again when ||a| - 180| < 1e-12; Split Draft (+0x1630) gives the second side -a and its slope. Per chain
   entity (the CHAIN element +0 == 0): +6 bit 0 = its ends within tol, rev = its direction differs from the chain's first entity's;
   FUN_180126b90 (decomp/surfextr/extr.c l.1959) with the length (negated for the length Flip state 1), and with Flip state 2 a second
   time with the length negated (and the split angle). FUN_180126b90, Length mode: a line whose ends coincide in the Cplane view (x and
   y within tol) is skipped; d = the slope (negated for rev, +6 bit 2 = rev); N = the Cplane's z (viewz of GetActiveViewNo) x length,
   |N| < 1e-12 skipped; a negative length toggles bit 2; the surface (mcsb_draft) put_surf_in_db(main_color, main_level) -> the
   surfaceFrontColor rule; UnSelectEptr(curve). Plane mode (get_plane_info, FUN_180125a40 / FUN_1801263c0, surf_slice): NOT PORTED. */
int srf_draft_exec(const Chain *ch, int nch, double len, double ang_deg, int lflip, int aflip, int split, int level, int color, char *msg, size_t msgn) {
    double tol = mc_systol_units(mcam_is_metric(&F))->tol;
    int st = srf_surface_type(), made = 0, col = color == geo_color && color != cfg_back_color ? srf_front_color : color;
    double a = aflip ? -ang_deg : ang_deg;
    double s1, s2 = 0, a2 = -a;
    #define SLOPE(A, S) do { if (fabs((A) - 90.0) >= 1e-12) { if (fabs((A) + 90.0) < 1e-12 || fabs((A) - 270.0) < 1e-12) S = -2000000000000.0; \
        else { S = -tan((A) * 0.01745329251994329); if (fabs(fabs(A) - 180.0) < 1e-12) S = -S; } } else S = 2000000000000.0; } while (0)
    SLOPE(a, s1);
    if (split) SLOPE(a2, s2);
    #undef SLOPE
    double vz[3], vx[3], vy[3]; cplane_dir(2, vz); cplane_dir(0, vx); cplane_dir(1, vy);
    const char *last_err = NULL;
    for (int c = 0; c < nch; c++) for (int k = 0; k < ch[c].n; k++) {
        int e = ch[c].ent[k]; if (e < 0 || e >= F.nents || !F.ents[e].type) continue;
        Ent cv = F.ents[e];                               /* a copy: mcam_new_surf grows F.ents */
        if (cv.type != E_LINE && cv.type != E_ARC && cv.type != E_SPLINE) continue;
        V3 p0 = cv.a, p1 = cv.b;
        if (cv.type == E_ARC) { p0 = mcam_arc_pt(&cv, cv.a0); p1 = mcam_arc_pt(&cv, cv.a0 + cv.sw); }
        if (cv.type == E_LINE) {                          /* FUN_180126b90: a line along the view z */
            double dl[3] = { cv.b.x - cv.a.x, cv.b.y - cv.a.y, cv.b.z - cv.a.z };
            if (fabs(dl[0] * vx[0] + dl[1] * vx[1] + dl[2] * vx[2]) < tol && fabs(dl[0] * vy[0] + dl[1] * vy[1] + dl[2] * vy[2]) < tol) continue;
        }
        int closed = sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y) + (p1.z - p0.z) * (p1.z - p0.z)) < tol;
        int rev = k > 0 && (ch[c].rev ? ch[c].rev[k] != ch[c].rev[0] : 0);
        int64_t ce = cv.created ? cv.septr : cv.eptr;
        for (int side = 0; side < (lflip == 2 ? 2 : 1); side++) {
            double L = side ? -len : (lflip == 1 ? -len : len), sl = side && split ? s2 : s1;
            double N[3] = { vz[0] * L, vz[1] * L, vz[2] * L };
            if (sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]) < 1e-12) continue;
            uint16_t fl = (uint16_t)(0x40 | (closed ? 1 : 0) | (rev ? 4 : 0));
            if (L < 0) fl ^= 4;
            uint32_t n; const char *err;
            uint8_t *r = mcsb_draft(&F, &cv, ce, N, rev ? -sl : sl, fl, st, solid_mode != 0, tol, &n, &err);
            if (!r) { last_err = err; continue; }
            mcam_new_surf(&F, r, n, level, col); free(r); made++;
        }
    }
    if (last_err && msg) snprintf(msg, msgn, "Draft: %s", last_err);
    if (made) scene_surfs_update(0);
    return made;
}
/* the dialog's Running Length (+0x5578) = Length / cos(angle) (cos > tol; else = Length): FUN_180124fd0 (Length kill-focus), FUN_180124f00
   (Angle), and Length = cos(angle) x Running Length (FUN_180125090); each compares its value with the one last seen (+0x5548 / +0x5550 /
   +0x5558) and acts when they differ by more than tol */
void srf_draft_sync(double *len, double *run, double ang, double *plen, double *prun, double *pang) {
    double tol = mc_systol_units(mcam_is_metric(&F))->tol, c = cos(ang * 0.01745329251994329);
    if (fabs(*plen - *len) > tol) { *plen = *len; *run = c > tol ? *len / c : *len; *prun = *run; }
    else if (fabs(*prun - *run) > tol) { *prun = *run; *len = c * *run; *plen = *len; }
    if (fabs(*pang - ang) > tol) { *pang = ang; *run = c > tol ? *len / c : *len; *prun = *run; }
}

/* ------------------------------------------------------------------------------------------------ Extrude (G_SEXTR in cui_geo.c) */
/* ExtrudeSurface (mccad 0x180025a70, decomp/surfcreate/create_cmds.c l.7; helpers decomp/surfextr/extr.c, extr2.c; formats/
   surfaces_tab_plan.md "C. Extrude"). The shape struct (FUN_18001c6d0 type 6, P = shape +0x40): P[0] height, P[1] taper (degrees),
   +0x10 the base point, +0x28 the axis, +0x58 the view, +0x5a the profile's orientation, P[0x11] / [0x12] / [0x13] the bottom / top /
   other-side profile lists (0x48-byte nodes 'L' x0 y0 x1 y1 / 'A' cx cy r start sweep, in the axis view, relative to the base point;
   +0x10 / +0x18 the walls made from the node). Splines in the profile ('S' / 'N' nodes: FUN_180018f50 / FUN_180019320, the walls
   FUN_180018c30 make_spline_from_pts + FUN_180023160): NOT PORTED. */
#include "cam2d_remach.h"
static void unit3(double v[3]); static void cross(const double a[3], const double b[3], double o[3]);
typedef struct { char id; double a[5]; } XEl;
typedef struct { int n; XEl *e; } XPr;
typedef struct { double h, sc, rot, off, tap, ax[3]; int hfl, ofl, tfl, uselen; } XPar;   /* EXTRUDESURF_PARAMS (getParams 0x18012a620): +0 height,
                                                          +0xc its Flip state, +0x10 scale, +0x20 rotate (rad), +0x30 offset, +0x3c its Flip, +0x40 taper
                                                          (rad), +0x4c its Flip, +0x58 the axis, +0x70 the Axis 2 Points length flag */
static struct {
    int ok, built, nent, *ent, *rev;                      /* the chain the profile came from; built: the last build succeeded (ExtrudeSurface's bVar5) */
    double axis[3], base[3], P0, P1;
    int ccw;                                              /* P+0x5a */
    XPr bot, top, oth; int top_b, oth_b;                  /* top_b / oth_b: that list is the bottom one (P[0x12] == P[0x11]) */
    XPar prev;
    char msg[160];
} xs;
static void xpr_free(XPr *p) { free(p->e); p->e = NULL; p->n = 0; }
static void xs_free_tops(void) { if (!xs.top_b) xpr_free(&xs.top); if (!xs.oth_b) xpr_free(&xs.oth); xs.top_b = xs.oth_b = 0; }
static const XPr *xs_top(void) { return xs.top_b ? &xs.bot : &xs.top; }
static const XPr *xs_oth(void) { return xs.oth_b ? &xs.bot : &xs.oth; }
static void xs_warn(int id) {                             /* post_warning (mccoreres) */
    static const char *t[] = { "Cannot offset the Extrusion with the current Offset Distance", "Cannot Taper the Extrusion with the Current Draft Angle",
                               "Chain cannot contain splines unless there is just one closed spline", "Chain is either empty or consists of 1 Line" };
    if (id >= 0x1d13 && id <= 0x1d16) snprintf(xs.msg, sizeof xs.msg, "%s", t[id - 0x1d13]);
}
static void xs_v2w(const double M[9], const double v[3], double w[3]) { for (int k = 0; k < 3; k++) w[k] = v[0] * M[k] + v[1] * M[3 + k] + v[2] * M[6 + k]; }
static void xs_w2v(const double M[9], const double w[3], double v[3]) { for (int r = 0; r < 3; r++) v[r] = M[3 * r] * w[0] + M[3 * r + 1] * w[1] + M[3 * r + 2] * w[2]; }
/* FUN_180019d30: a node at t (lines / arcs) */
static void xs_eval(const XEl *e, double t, double p[2]) {
    if (e->id == 'A') { p[0] = cos(t * e->a[4] + e->a[3]) * e->a[2] + e->a[0]; p[1] = sin(t * e->a[4] + e->a[3]) * e->a[2] + e->a[1]; }
    else { p[0] = (1 - t) * e->a[0] + t * e->a[2]; p[1] = t * e->a[3] + (1 - t) * e->a[1]; }
}
/* FUN_18001a460: the list's box (arcs 11 samples start + k sweep / 10, lines both ends) */
static void xs_box(const XPr *p, double b[4]) {
    b[0] = b[2] = 1e38; b[1] = b[3] = -1e38;
    for (int i = 0; i < p->n; i++) {
        const XEl *e = &p->e[i]; double q[2];
        int ns = e->id == 'A' ? 11 : 2;
        for (int k = 0; k < ns; k++) {
            if (e->id == 'A') { double a = e->a[3] + e->a[4] * 0.1 * k; q[0] = cos(a) * e->a[2] + e->a[0]; q[1] = sin(a) * e->a[2] + e->a[1]; }
            else { q[0] = e->a[2 * k]; q[1] = e->a[2 * k + 1]; }
            if (q[0] < b[0]) b[0] = q[0]; if (b[1] < q[0]) b[1] = q[0]; if (q[1] < b[2]) b[2] = q[1]; if (b[3] < q[1]) b[3] = q[1];   /* FUN_1800180b0 */
        }
    }
}
/* FUN_180019450: P+0x5a from the highest point (the largest y; the first one wins ties) and its neighbours along the list (arcs at t
   steps of 0.1, lines 1.0, the last node's end left out): d1 = previous - top, d2 = next - top (unit); with d1.y <= d2.y the next one
   decides (x > 0 -> 0), else the previous (x < 0 -> 0); a zero x -> 0, otherwise 1 */
static int xs_orient(const XPr *p) {
    if (!p->n) return 0;
    double st[2], prv[2], nxt[2], top[2], last[2] = { 0, 0 }, q[2]; xs_eval(&p->e[0], 0, st);
    double ymax = st[1]; top[0] = st[0]; top[1] = st[1]; nxt[0] = st[0]; nxt[1] = st[1]; prv[0] = prv[1] = 0;
    int first_is_max = 1, want_next = 1; double cur[2] = { st[0], st[1] };
    for (int i = 0; i < p->n; i++) {
        const XEl *e = &p->e[i]; int ns = e->id == 'A' ? 10 : 1; double dt = e->id == 'A' ? 0.1 : 1.0, t = 0;
        if (i == p->n - 1) ns--;
        for (int k = 0; k < ns; k++) {
            t += dt; xs_eval(e, t, q);
            if (q[1] <= ymax) { if (want_next) { nxt[0] = q[0]; nxt[1] = q[1]; want_next = 0; } }
            else { ymax = q[1]; top[0] = q[0]; top[1] = q[1]; prv[0] = cur[0]; prv[1] = cur[1];
                   if (i == p->n - 1 && k == ns - 1) { first_is_max = 0; nxt[0] = st[0]; nxt[1] = st[1]; } else { want_next = 1; first_is_max = 0; } }
            cur[0] = q[0]; cur[1] = q[1]; last[0] = q[0]; last[1] = q[1];
        }
    }
    if (first_is_max) { prv[0] = last[0]; prv[1] = last[1]; }
    double d1[2] = { prv[0] - top[0], prv[1] - top[1] }, d2[2] = { nxt[0] - top[0], nxt[1] - top[1] }, l;
    l = sqrt(d1[0] * d1[0] + d1[1] * d1[1]); if (l == 0) d1[0] = d1[1] = 0; else { d1[0] /= l; d1[1] /= l; }
    l = sqrt(d2[0] * d2[0] + d2[1] * d2[1]); if (l == 0) d2[0] = d2[1] = 0; else { d2[0] /= l; d2[1] /= l; }
    int b; double x;
    if (d1[1] <= d2[1]) { b = 0.0 < d2[0]; x = d2[0]; } else { b = d1[0] < 0.0; x = d1[0]; }
    return !b && x != 0.0;
}
/* FUN_180024290 -> FUN_180018a00 (the pock_bound: +0 1, +1 1, +2 0, +3 = P+0x5a) -> set_min_fil_angle(0) -> ccomp_pock(bound, dist, 'L' or
   'R' when flag == P+0x5a, the min fillet angle, 1, 1, 0) (rm_ccomp_pock, cam2d_remach.c) -> FUN_180019170 when the first result has as
   many entities; else nothing (0) */
static int xs_offset(const XPr *in, double dist, int flag, XPr *out) {
    RmB *b = rm_b_new(in->n);
    for (int i = 0; i < in->n; i++) { b->e[i].id = in->e[i].id; memcpy(b->e[i].a, in->e[i].a, sizeof b->e[i].a); }
    b->valid = 1; b->broken = 1; b->isl = 0; rm_b_info(b); b->ccw = (unsigned char)xs.ccw;
    rm_tol = mc_systol.tol; rm_nc_tol = mc_systol.nc_tol; rm_chain_tol = mc_systol.chain_tol;
    RmB *o = NULL; const char *why = NULL;
    rm_ccomp_pock(b, &o, dist, flag == xs.ccw ? 'R' : 'L', M_PI, 1, 1, 0, &why);   /* set_min_fil_angle(0): pi (mccore_pock2.c:7-20) */
    int ok = o && o->n == in->n;
    if (ok) { out->n = o->n; out->e = calloc(o->n, sizeof *out->e); for (int i = 0; i < o->n; i++) { out->e[i].id = o->e[i].id; memcpy(out->e[i].a, o->e[i].a, sizeof out->e[i].a); } }
    rm_b_free_list(&o); rm_b_free_list(&b);
    return ok;
}
#define XS_MINTAP 0.001745329251994329
/* FUN_18001f090: the top (and the other side's) list: taper >= XS_MINTAP degrees: offset(bottom, tan(taper) height, 0); both
   directions: P+0x5a toggled, offset(bottom, -(tan(-taper) height), 0) (disassembly 0x18001f185-0x18001f1b8), toggled back; a failure:
   7444, the taper 0, the lists the bottom one */
static void xs_tops(int both) {
    xs_free_tops();
    if (!xs.bot.n) return;
    xs.top_b = 1; if (both) xs.oth_b = 1;
    if (fabs(xs.P1) < XS_MINTAP) return;
    XPr t = { 0 }, o = { 0 };
    if (!xs_offset(&xs.bot, tan(xs.P1 * 0.01745329251994329) * xs.P0, 0, &t)) { xs_warn(0x1d14); xs.P1 = 0; return; }
    xs.top = t; xs.top_b = 0;
    if (!both) return;
    xs.ccw = !xs.ccw;
    int ok = xs_offset(&xs.bot, -(tan(-xs.P1 * 0.01745329251994329) * xs.P0), 0, &o);
    if (!ok) { xs_warn(0x1d14); xs.P1 = 0; xpr_free(&xs.top); xs.top_b = 1; xs.oth_b = 1; }
    else { xs.oth = o; xs.oth_b = 0; }
    xs.ccw = !xs.ccw;
}
/* FUN_1800168c0: the top (and other) lists again after a taper (which 0) / height (which 1) change; a failure: 7444, the old value back */
static int xs_retaper(double old, int which, int both) {
    XPr t = { 0 };
    if (!xs_offset(&xs.bot, tan(xs.P1 * 0.01745329251994329) * xs.P0, 0, &t)) { xs_warn(0x1d14); if (which == 0) xs.P1 = old; else xs.P0 = old; return 0; }
    if (!xs.top_b) xpr_free(&xs.top); xs.top = t; xs.top_b = 0;
    if (!both) return 1;
    xs.ccw = !xs.ccw;
    XPr o = { 0 }; int ok = xs_offset(&xs.bot, -(tan(-xs.P1 * 0.01745329251994329) * xs.P0), 0, &o);   /* (disassembly 0x180016987-0x1800169b4) */
    if (!ok) { xs_warn(0x1d14); if (which == 0) xs.P1 = old; else xs.P0 = old; }
    else { if (!xs.oth_b) xpr_free(&xs.oth); xs.oth = o; xs.oth_b = 0; }
    xs.ccw = !xs.ccw;
    return ok;
}
static void xs_scale(XPr *p, double s) {                  /* FUN_180024dc0 */
    for (int i = 0; i < p->n; i++) { XEl *e = &p->e[i]; if (e->id == 'A') { e->a[0] *= s; e->a[1] *= s; e->a[2] *= s; } else for (int k = 0; k < 4; k++) e->a[k] *= s; }
}
static void xs_rot2(double *p, double t) { double c = cos(t), s = sin(t), x = p[0] * c - p[1] * s, y = s * p[0] + p[1] * c; p[0] = x; p[1] = y; }   /* FUN_180024bf0 */
static void xs_rotate(XPr *p, double t) {                 /* FUN_180024ac0: arcs' start within -+2 pi */
    for (int i = 0; i < p->n; i++) { XEl *e = &p->e[i];
        if (e->id == 'A') { xs_rot2(e->a, t); e->a[3] += t; while (e->a[3] > 2 * M_PI) e->a[3] -= 2 * M_PI; while (e->a[3] < -2 * M_PI) e->a[3] += 2 * M_PI; }
        else { xs_rot2(e->a, t); xs_rot2(e->a + 2, t); } }
}
/* FUN_18001b2a0 for a chain of lines and arcs: FUN_180019fa0 (the normal), make_view_k, the nodes with their box, the closing line (7447,
   Yes), the base point = the box centre at the first point's depth, the nodes relative to it, FUN_180019450. 0 / -1 refused (xs.msg). */
static int xs_chain(const Chain *c) {
    double tol = mc_systol.tol;
    if (!c->n) { xs_warn(0x1d16); return -1; }
    for (int k = 0; k < c->n; k++) { const Ent *E = &F.ents[c->ent[k]];
        if (E->type == E_SPLINE) {
            V3 a = E->a, b = E->b; double d = sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
            if (k || c->n > 1 || d > tol) { xs_warn(0x1d15); return -1; }
            snprintf(xs.msg, sizeof xs.msg, "Extrude: a closed spline profile (FUN_180018f50 / FUN_180019320, FUN_180018c30, FUN_180023160) is NOT PORTED"); return -1; }
        if (E->type != E_LINE && E->type != E_ARC) { xs_warn(0x1d16); return -1; } }
    /* FUN_180019fa0: n points (a line 1, an arc 3; 2 -> 3, fewer -> 7446) in three groups (n / 3 each, the rest to the first two), each
       sum started at v_z_3d (= (0, 0, 0): mccore 0x181371ca8, zero in the image and never written - SymWrites / RefsTo on mccore.dll and
       mastercam.dll, decomp/surfextr/vz*.txt); a line gives its end, an arc its points at 1/3, 2/3 and its end (along the chain); the normal = UnitNormal(G1 - G0,
       G2 - G0) of the means (|cross| < tol: refused). Ours: find_view_m's snapping onto an existing view's z is left out. */
    int n = 0; for (int k = 0; k < c->n; k++) n += F.ents[c->ent[k]].type == E_LINE ? 1 : 3;
    if (n == 2) n = 3; else if (n < 3) { xs_warn(0x1d16); return -1; }
    int cnt[3] = { n / 3, n / 3, n / 3 }; if (n % 3 > 0) { cnt[0]++; if (n % 3 == 2) cnt[1]++; }
    double G[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } }; int g = 0, m = 0;
    for (int k = 0; k < c->n && g <= 2; k++) {
        const Ent *E = &F.ents[c->ent[k]]; int r = c->rev ? c->rev[k] : 0;
        V3 P[3]; int np = 0;
        if (E->type == E_ARC) { for (int j = 1; j <= 2; j++) P[np++] = mcam_arc_pt(E, r ? E->a0 + E->sw * (1 - j / 3.0) : E->a0 + E->sw * (j / 3.0)); P[np++] = mcam_arc_pt(E, r ? E->a0 : E->a0 + E->sw); }
        else P[np++] = r ? E->a : E->b;
        for (int j = 0; j < np && g <= 2; j++) { G[g][0] += P[j].x; G[g][1] += P[j].y; G[g][2] += P[j].z; if (++m == cnt[g]) { m = 0; g++; } }
    }
    for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) G[i][k] *= 1.0 / cnt[i];
    double u[3] = { G[1][0] - G[0][0], G[1][1] - G[0][1], G[1][2] - G[0][2] }, v[3] = { G[2][0] - G[0][0], G[2][1] - G[0][1], G[2][2] - G[0][2] }, N[3];
    cross(u, v, N); double l = sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
    if (l < tol) { xs_warn(0x1d16); return -1; }
    for (int k = 0; k < 3; k++) N[k] /= l;
    double M[9]; if (!mcsb_view_k(N, M)) { xs_warn(0x1d16); return -1; }
    /* the nodes (forward: start -> end; reversed: the arc's start + sweep, -sweep) and the box */
    XPr p = { 0, calloc((size_t)c->n * 2 + 1, sizeof(XEl)) };
    double bx[4] = { 1e38, -1e38, 1e38, -1e38 }, first[3] = { 0, 0, 0 }, last[3] = { 0, 0, 0 };
    #define XBOX(X, Y) do { if ((X) < bx[0]) bx[0] = (X); if (bx[1] < (X)) bx[1] = (X); if ((Y) < bx[2]) bx[2] = (Y); if (bx[3] < (Y)) bx[3] = (Y); } while (0)
    for (int k = 0; k < c->n; k++) {
        const Ent *E = &F.ents[c->ent[k]]; int r = c->rev ? c->rev[k] : 0;
        V3 s = E->type == E_ARC ? mcam_arc_pt(E, E->a0) : E->a, e = E->type == E_ARC ? mcam_arc_pt(E, E->a0 + E->sw) : E->b;
        V3 ps = r ? e : s, pe = r ? s : e;
        if (!k) { first[0] = ps.x; first[1] = ps.y; first[2] = ps.z; }
        last[0] = pe.x; last[1] = pe.y; last[2] = pe.z;
        if (E->type == E_LINE) {
            double w0[3] = { ps.x, ps.y, ps.z }, w1[3] = { pe.x, pe.y, pe.z }, a0[3], a1[3]; xs_w2v(M, w0, a0); xs_w2v(M, w1, a1);
            XEl *x = &p.e[p.n++]; x->id = 'L'; x->a[0] = a0[0]; x->a[1] = a0[1]; x->a[2] = a1[0]; x->a[3] = a1[1]; XBOX(a0[0], a0[1]); XBOX(a1[0], a1[1]);
            continue;
        }
        /* FUN_180018620 -> FUN_180015500: the arc in the axis view - copied when its own view is that view, otherwise re-expressed: the
           centre, the start angle of its start point (restrict_angle 0 .. 2 pi - 1e-5), the sweep the first of d, -d, 2 pi - d, d - 2 pi,
           d + 2 pi (d = the end's angle - start, restricted to -+2 pi) whose middle point (FUN_1800241e0 at 0.5) is the arc's within tol */
        double ax[3] = { 1, 0, 0 }, ay[3] = { 0, 1, 0 }, az[3] = { 0, 0, 1 };
        if (E->vw) for (int q = 0; q < 3; q++) { ax[q] = E->vm[q]; ay[q] = E->vm[3 + q]; az[q] = E->vm[6 + q]; }
        double cw[3] = { E->c.x, E->c.y, E->c.z }, cv[3]; xs_w2v(M, cw, cv);
        double st = E->a0, sw = E->sw;
        int same = 1; for (int q = 0; q < 3; q++) if (fabs(ax[q] - M[q]) > 1e-12 || fabs(ay[q] - M[3 + q]) > 1e-12 || fabs(az[q] - M[6 + q]) > 1e-12) same = 0;
        if (!same) {
            double w0[3] = { s.x, s.y, s.z }, w1[3] = { e.x, e.y, e.z }, q0[3], q1[3]; xs_w2v(M, w0, q0); xs_w2v(M, w1, q1);
            double dx = q0[0] - cv[0], dy = q0[1] - cv[1];
            st = dx * dx + dy * dy >= 1e-12 ? atan2(dy, dx) : 0; if (st < 0) st += 2 * M_PI;
            while (st > 6.283175307179587) st -= 2 * M_PI; while (st < 0) st += 2 * M_PI;
            dx = q1[0] - cv[0]; dy = q1[1] - cv[1];
            double ea = 0; if (dx * dx + dy * dy >= 1e-12) { ea = atan2(dy, dx); if (ea < 0) ea += 2 * M_PI; }
            double d = ea - st; V3 mid = mcam_arc_pt(E, E->a0 + E->sw * 0.5);
            double cand[5] = { d, -d, 2 * M_PI - d, d - 2 * M_PI, d }, got = d; int found = 0;
            for (int ci = 0; ci < 5 && !found; ci++) {
                double s2 = cand[ci]; while (s2 > 6.283195307179586) s2 -= 2 * M_PI; while (s2 < -6.283175307179587) s2 += 2 * M_PI;
                double mv[3] = { cv[0] + E->r * cos(st + 0.5 * s2), cv[1] + E->r * sin(st + 0.5 * s2), cv[2] }, mw[3]; xs_v2w(M, mv, mw);
                if (sqrt((mw[0] - mid.x) * (mw[0] - mid.x) + (mw[1] - mid.y) * (mw[1] - mid.y) + (mw[2] - mid.z) * (mw[2] - mid.z)) <= tol) { got = s2; found = 1; }
            }
            if (!found) { got = d + 2 * M_PI; while (got > 6.283195307179586) got -= 2 * M_PI; while (got < -6.283175307179587) got += 2 * M_PI;
                double mv[3] = { cv[0] + E->r * cos(st + 0.5 * got), cv[1] + E->r * sin(st + 0.5 * got), cv[2] }, mw[3]; xs_v2w(M, mv, mw);
                if (sqrt((mw[0] - mid.x) * (mw[0] - mid.x) + (mw[1] - mid.y) * (mw[1] - mid.y) + (mw[2] - mid.z) * (mw[2] - mid.z)) > tol) { got = d; while (got > 6.283195307179586) got -= 2 * M_PI; while (got < -6.283175307179587) got += 2 * M_PI; } }
            sw = got;
        }
        int full = !(fabs(fabs(sw) - 2 * M_PI) > 0.01745329251994329);
        for (int h = 0; h < (full ? 2 : 1); h++) {       /* a full circle: two halves */
            XEl *x = &p.e[p.n++]; x->id = 'A'; x->a[0] = cv[0]; x->a[1] = cv[1]; x->a[2] = E->r; x->a[3] = st; x->a[4] = sw;
            if (full) { x->a[4] = sw * 0.5; if (h) x->a[3] = st + sw * 0.5; }
            if (r) { if (!full) { x->a[3] += x->a[4]; x->a[4] = -x->a[4]; }
                     else if (!h) { x->a[3] = st + sw * 0.5; x->a[4] = -sw * 0.5; } else { x->a[3] = st + sw; x->a[4] = -sw * 0.5; } }
            for (int q = 0; q < 11; q++) { double a = x->a[3] + x->a[4] * 0.1 * q; XBOX(cos(a) * x->a[2] + x->a[0], sin(a) * x->a[2] + x->a[1]); }
        }
    }
    #undef XBOX
    double gap = sqrt((last[0] - first[0]) * (last[0] - first[0]) + (last[1] - first[1]) * (last[1] - first[1]) + (last[2] - first[2]) * (last[2] - first[2]));
    if (gap > tol) {                                      /* message_box_yes_no(7447, 7448) */
        int yes = cui_hooks.ask_yes_no ? cui_hooks.ask_yes_no("Primitive surface creation", "Chain is not closed. Close it(Yes) or Reject it(No)?") : 1;
        if (!yes) { xpr_free(&p); xs.msg[0] = 0; return -1; }
        double a0[3], a1[3]; xs_w2v(M, last, a0); xs_w2v(M, first, a1);
        XEl *x = &p.e[p.n++]; x->id = 'L'; x->a[0] = a0[0]; x->a[1] = a0[1]; x->a[2] = a1[0]; x->a[3] = a1[1];
    }
    double fv[3]; xs_w2v(M, first, fv);
    double cx = (bx[0] + bx[1]) * 0.5, cy = (bx[2] + bx[3]) * 0.5, bv[3] = { cx, cy, fv[2] };
    xs_v2w(M, bv, xs.base);
    for (int i = 0; i < p.n; i++) { XEl *x = &p.e[i]; x->a[0] -= cx; x->a[1] -= cy; if (x->id == 'L') { x->a[2] -= cx; x->a[3] -= cy; } }
    xpr_free(&xs.bot); xs_free_tops();
    xs.bot = p; memcpy(xs.axis, N, sizeof N);
    xs.ccw = xs_orient(&xs.bot);
    return 0;
}
/* FUN_180018100 (the first build's settings, after FUN_18001f090) */
static int xs_first(const XPar *p) {
    double tol = mc_systol.tol; int both = p->hfl == 2;
    xs_tops(both);
    double P1e = xs.P1 * 0.01745329251994329, P0e = xs.P0;    /* local_d8 is the taper in radians (FUN_1800168c0 restores it as such) */
    if (fabs(xs.P1 * 0.01745329251994329 - p->tap) > tol || p->tfl) {
        xs.P1 = p->tap * 57.29577951308233;
        if (fabs(xs.P1) < XS_MINTAP) { xs_free_tops(); xs.top_b = 1; if (both) xs.oth_b = 1; }
        else { if (p->tfl) xs.P1 = -xs.P1; if (!xs_retaper(P1e, 0, both)) return 0; }
    }
    if (fabs(P0e - p->h) > tol || p->hfl) {
        xs.P0 = p->h;
        if (p->hfl == 1) xs.P0 = -p->h; else xs_tops(both);
        if (fabs(xs.P1) >= XS_MINTAP && !xs_retaper(P0e, 1, both)) return 0;
    }
    if (fabs(p->sc - 1.0) > tol) { xs_scale(&xs.bot, p->sc); if (!xs.top_b) xs_tops(both); }
    if (fabs(p->rot) > tol) { xs_rotate(&xs.bot, p->rot); if (!xs.top_b) xs_tops(both); }
    if (fabs(p->off) > tol || p->ofl) {
        XPr o = { 0 }; if (!xs_offset(&xs.bot, p->off, p->ofl == 0, &o)) { xs_warn(0x1d13); return 0; }
        xs_free_tops(); xpr_free(&xs.bot); xs.bot = o; xs_tops(both);
    }
    double la = sqrt(p->ax[0] * p->ax[0] + p->ax[1] * p->ax[1] + p->ax[2] * p->ax[2]), lb = sqrt(xs.axis[0] * xs.axis[0] + xs.axis[1] * xs.axis[1] + xs.axis[2] * xs.axis[2]);
    double dt = la > 0 && lb > 0 ? (p->ax[0] * xs.axis[0] + p->ax[1] * xs.axis[1] + p->ax[2] * xs.axis[2]) / (la * lb) : 0;
    if (fabs(dt) != 1.0) { memcpy(xs.axis, p->ax, sizeof xs.axis); if (p->uselen) xs.P0 = p->h; }
    return 1;
}
/* FUN_1800254a0 (a dialog change: the differences against the last parameters, in this order); 0 = nothing changed */
static int xs_update(const XPar *o, XPar *p) {
    double tol = mc_systol.tol; int both = p->hfl == 2, ch = 0;
    if (o->tap != p->tap || o->tfl != p->tfl) {
        double old = xs.P1; xs.P1 = p->tap * 57.29577951308233; ch = 1;
        if (fabs(xs.P1) >= XS_MINTAP) { if (p->tfl) xs.P1 = -xs.P1; ch = xs_retaper(old, 0, both); }
        else { xs_free_tops(); xs.top_b = 1; if (both) xs.oth_b = 1; }
    }
    if (o->h != p->h || o->hfl != p->hfl) {
        double old = xs.P0; xs.P0 = p->h; ch = 1;
        if (p->hfl == 1) xs.P0 = -p->h;
        else if ((p->hfl == 2 && o->hfl == 1) || (p->hfl == 0 && o->hfl == 2)) xs_tops(both);
        if (fabs(xs.P1) >= XS_MINTAP) ch = xs_retaper(old, 1, both);
    }
    if (o->sc != p->sc) { ch = 1;
        if (fabs(o->sc) > tol) { xs_scale(&xs.bot, 1.0 / o->sc); xs_tops(both); }
        xs_scale(&xs.bot, p->sc); xs_tops(both); }
    if (o->rot != p->rot) { ch = 1;
        if (fabs(o->rot) > tol) { xs_rotate(&xs.bot, -o->rot); xs_tops(both); }
        xs_rotate(&xs.bot, p->rot); xs_tops(both); }
    if (o->off != p->off || o->ofl != p->ofl) { ch = 1;
        int fail = 0;
        if (fabs(o->off) > tol) {                         /* the old offset undone (flag: the old Flip == 1) */
            XPr q = { 0 }; if (!xs_offset(&xs.bot, o->off, o->ofl == 1, &q)) { xs_warn(0x1d13); p->off = o->off; ch = 0; fail = 1; }
            else { xs_free_tops(); xpr_free(&xs.bot); xs.bot = q; xs_tops(both); }
        }
        if (!fail && fabs(p->off) > tol) {
            XPr q = { 0 }; if (!xs_offset(&xs.bot, p->off, p->ofl == 0, &q)) { xs_warn(0x1d13); p->off = o->off; ch = 0; }
            else { xs_free_tops(); xpr_free(&xs.bot); xs.bot = q; xs_tops(both); }
        }
    }
    double d = sqrt((p->ax[0] - o->ax[0]) * (p->ax[0] - o->ax[0]) + (p->ax[1] - o->ax[1]) * (p->ax[1] - o->ax[1]) + (p->ax[2] - o->ax[2]) * (p->ax[2] - o->ax[2]));
    if (d <= tol) return ch;
    memcpy(xs.axis, p->ax, sizeof xs.axis); if (p->uselen) xs.P0 = p->h;
    return 1;
}
/* the placement (FUN_180018690 / FUN_1800187f0 / FUN_18001c200): the frame make_view_k(axis) (find_view_m), a view point (x, y, z) ->
   world + the base point; z = 0 at level 0, else P[0] (negated when the view's z . axis < 0) */
static void xs_place(const double M[9], double x, double y, double z, double w[3]) { double v[3] = { x, y, z }; xs_v2w(M, v, w); for (int k = 0; k < 3; k++) w[k] += xs.base[k]; }
static double xs_level_z(const double M[9], int level, double P0) {
    if (!level) return 0;
    double d = M[6] * xs.axis[0] + M[7] * xs.axis[1] + M[8] * xs.axis[2];
    return d >= 0 ? P0 : -P0;
}
static int xs_list_n;                                    /* the entities to recolour on commit (FUN_180017520) */
static int *xs_list, xs_list_cap;
static void xs_keep(int e) { if (xs_list_n == xs_list_cap) { xs_list_cap = xs_list_cap ? 2 * xs_list_cap : 64; xs_list = realloc(xs_list, sizeof(int) * xs_list_cap); } xs_list[xs_list_n++] = e; }
static int xs_new_surf(uint8_t *r, uint32_t n, int level, int col) { int e = mcam_new_surf(&F, r, n, level, col); free(r); return e; }
/* FUN_180020410: the walls of the bottom list against the top (other 0) or the other side's (other 1: P[0], P[1] negated and P+0x5a
   toggled meanwhile; that list at -height), node by node until either list ends: lines create_nurbsurf_from_4_points(b0, b1, t0, t1)
   with +6 bit 2 = P+0x5a & 1, arcs FUN_18001ddd0 */
static void xs_walls(int other, int level, int col, int shaded, double tol) {
    if (other) { xs.ccw = !xs.ccw; xs.P0 = -xs.P0; xs.P1 = -xs.P1; }
    double M[9]; mcsb_view_k(xs.axis, M);
    const XPr *t = other ? xs_oth() : xs_top();
    double zt = xs_level_z(M, 1, xs.P0);
    for (int i = 0; i < xs.bot.n && i < t->n; i++) {
        const XEl *b = &xs.bot.e[i], *u = &t->e[i]; uint32_t n; uint8_t *r = NULL;
        if (b->id != u->id) { snprintf(xs.msg, sizeof xs.msg, "a line paired with an arc after the offset: the wall is NOT DECODED (left out)"); continue; }
        if (b->id == 'L') {
            double c[4][3]; xs_place(M, b->a[0], b->a[1], 0, c[0]); xs_place(M, b->a[2], b->a[3], 0, c[1]);
            xs_place(M, u->a[0], u->a[1], zt, c[2]); xs_place(M, u->a[2], u->a[3], zt, c[3]);
            double z0[3] = { 0, 0, 0 }; r = mcsb_plane4(&F, c, z0, shaded, tol, &n);
            if (r) { uint16_t fl = (uint16_t)((r[0x6e] | r[0x6f] << 8) & ~4u); fl |= (uint16_t)((xs.ccw & 1) << 2); r[0x6e] = (uint8_t)fl; r[0x6f] = (uint8_t)(fl >> 8); }
        } else {
            double C0[3], C1[3]; xs_place(M, b->a[0], b->a[1], 0, C0); xs_place(M, u->a[0], u->a[1], zt, C1);
            r = mcsb_arc_wall(&F, C0, C1, M, M + 3, M + 6, b->a[2], u->a[2], b->a[3], b->a[4], u->a[3], u->a[4], xs.ccw, shaded, tol, &n);
        }
        if (r) xs_keep(xs_new_surf(r, n, level, col));
    }
    if (other) { xs.ccw = !xs.ccw; xs.P0 = -xs.P0; xs.P1 = -xs.P1; }
}
/* FUN_18001f270: a cap at level 0 (the base), 1 (the top) or 2 (the other side): FUN_18001c200's plane over the list's box (5 % of
   its x extent added on every side) at z = 0 / P[0] / -P[0], create_nurbsurf_from_4_points; +6 bit 2 from the middle normal . axis
   (top: set when >= 0, the others: clear when >= 0; flip (height Flip state 1) reverses); put_surf_in_db; the trim polygon: arcs from
   their start in steps of acos((r - surf_step_chord_dev) / r) (r < 2 dev: sweep / 4, the sign of the sweep), the last at end - step;
   lines their start; the first point again; the kind 10 record (+6 = the base's bit 2 | 0x40, one loop, the points' box); the base
   blanked, referenced */
static void xs_cap(int lev, int flip, int level, int col, int bcol, int shaded, double tol) {
    const XPr *p = lev == 0 ? &xs.bot : lev == 1 ? xs_top() : xs_oth();
    if (!p->n) return;
    double M[9]; mcsb_view_k(xs.axis, M);
    double z = lev == 0 ? 0 : lev == 1 ? xs.P0 : -xs.P0, dz = M[6] * xs.axis[0] + M[7] * xs.axis[1] + M[8] * xs.axis[2];
    if (lev && dz < 0) z = -z;
    double b[4]; xs_box(p, b); double m = (b[1] - b[0]) * 0.05;
    double c[4][3]; xs_place(M, b[0] - m, b[2] - m, z, c[0]); xs_place(M, b[1] + m, b[2] - m, z, c[1]); xs_place(M, b[0] - m, b[3] + m, z, c[2]); xs_place(M, b[1] + m, b[3] + m, z, c[3]);
    double z0[3] = { 0, 0, 0 }; uint32_t bn; uint8_t *br = mcsb_plane4(&F, c, z0, shaded, tol, &bn); if (!br) return;
    double su[3], sv[3], N[3]; for (int k = 0; k < 3; k++) { su[k] = c[1][k] - c[0][k]; sv[k] = c[2][k] - c[0][k]; }
    cross(su, sv, N); unit3(N);
    double d = N[0] * xs.axis[0] + N[1] * xs.axis[1] + N[2] * xs.axis[2];
    int set = lev == 1 ? (flip ? !(d >= 0) : d >= 0) : (flip ? d >= 0 : !(d >= 0));
    uint16_t fl = (uint16_t)((br[0x6e] | br[0x6f] << 8) & ~4u); if (set) fl |= 4; br[0x6e] = (uint8_t)fl; br[0x6f] = (uint8_t)(fl >> 8);
    int be = mcam_new_surf(&F, br, bn, level, bcol); free(br);
    double eu[3], ev[3], lu = 0, lv = 0; for (int k = 0; k < 3; k++) { eu[k] = c[1][k] - c[0][k]; ev[k] = c[2][k] - c[0][k]; lu += eu[k] * eu[k]; lv += ev[k] * ev[k]; }
    lu = sqrt(lu); lv = sqrt(lv); for (int k = 0; k < 3; k++) { eu[k] /= lu; ev[k] /= lv; }
    int cap = 1000, np = 0; double *uv = malloc(sizeof(double) * 2 * cap), dev = mc_systol.surf_step_chord_dev;
    #define XPT(W) do { if (np + 2 >= cap) { cap += 500; uv = realloc(uv, sizeof(double) * 2 * cap); } double d0 = (W)[0] - c[0][0], d1 = (W)[1] - c[0][1], d2 = (W)[2] - c[0][2]; \
        uv[2 * np] = (d0 * eu[0] + d1 * eu[1] + d2 * eu[2]) / lu; uv[2 * np + 1] = (d0 * ev[0] + d1 * ev[1] + d2 * ev[2]) / lv; np++; } while (0)
    for (int i = 0; i < p->n; i++) {
        const XEl *e = &p->e[i]; double w[3];
        if (e->id == 'L') { xs_place(M, e->a[0], e->a[1], z, w); XPT(w); continue; }
        double r = e->a[2], st = e->a[3], sw = e->a[4], end = st + sw, step = r >= dev * 2.0 ? fabs(acos((r - dev) / r)) : sw * 0.25;
        if (sw < 0) step = -step;
        double a = st;
        for (int last = 0; !last; a += step) {
            if ((step > 0 && end - step < a) || (step < 0 && a < end - step)) { a = end - step; last = 1; }
            xs_place(M, cos(a) * r + e->a[0], sin(a) * r + e->a[1], z, w); XPT(w);
            if (np > 200000) break;
        }
    }
    #undef XPT
    if (np) { uv[2 * np] = uv[0]; uv[2 * np + 1] = uv[1]; np++; }
    Surf *bs = &F.surfs[F.ents[be].srf]; int64_t bep = F.ents[be].septr;
    double *pts[1] = { uv }; int cnt[1] = { np }; uint32_t tn;
    uint8_t *tr = np >= 4 ? mcsb_trim_rec(&F, bep, bs->rec, bs->n, 1, pts, cnt, (uint16_t)((fl & 4) | 0x40), shaded, tol, &tn) : NULL;
    free(uv);
    if (tr) xs_keep(xs_new_surf(tr, tn, level, col));
    bs = &F.surfs[F.ents[be].srf]; bs->blanked = 1; bs->rec[5] |= 1; F.ents[be].blanked = 1;   /* sel | 0x100, the ref count +1 (fill_in_ent / rewrite_ent) */
}
/* ExtrudeSurface's build: the chain -> FUN_18001b2a0 (a new chain) or FUN_1800254a0 (the same one: the panel's changes), then
   FUN_18001f210(mode = the height Flip state): FUN_180020170 (a point at the base, a blanked line base -> base + axis, live colour),
   FUN_1800202e0 (mode 1: P+0x5a toggled; the walls; mode 2: the other side's walls too; the bottom cap (mode 2: the other side's) and
   the top cap, flip = mode 1). Colours: live_entity_color through put_surf_in_db; the commit (FUN_180017520) gives the point, the line,
   the walls and the trimmed caps the main colour (list: those entities), the blanked cap planes keep theirs. */
int srf_extr_exec(const Chain *ch, int nch, double prm[12], int level, int color, int **list, int *nlist, char *msg, size_t msgn) {
    double tol = mc_systol.tol; xs.msg[0] = 0; xs_list_n = 0; *list = NULL; *nlist = 0;
    if (nch < 1) return 0;
    const Chain *c = &ch[0];                              /* chain_1: one chain */
    XPar p = { prm[0], prm[1], prm[2] * 0.01745329251994329, prm[3], prm[4] * 0.01745329251994329, { prm[5], prm[6], prm[7] }, (int)prm[8], (int)prm[9], (int)prm[10], (int)prm[11] };
    int same = xs.ok && xs.nent == c->n;
    for (int k = 0; same && k < c->n; k++) if (xs.ent[k] != c->ent[k] || xs.rev[k] != (c->rev ? c->rev[k] : 0)) same = 0;
    int ok;
    if (!same) {                                          /* FUN_18001c960: P[0] = the height, P[1] = the taper in degrees (getParams before chaining) */
        xs.ok = 0; xs.P0 = p.h; xs.P1 = p.tap * 57.29577951308233;
        if (xs_chain(c)) { if (xs.msg[0]) snprintf(msg, msgn, "Extrude: %s", xs.msg); return 0; }
        free(xs.ent); free(xs.rev); xs.nent = c->n; xs.ent = malloc(sizeof(int) * (c->n ? c->n : 1)); xs.rev = malloc(sizeof(int) * (c->n ? c->n : 1));
        for (int k = 0; k < c->n; k++) { xs.ent[k] = c->ent[k]; xs.rev[k] = c->rev ? c->rev[k] : 0; }
        xs.ok = 1;
        memcpy(p.ax, xs.axis, sizeof p.ax);              /* setParams: the dialog's axis = the chain's */
        ok = xs_first(&p);
    } else if (!xs.built) return 0;                       /* 0x68 / 0x69 / 0x67 act only after a build (bVar5) */
    else { xs_update(&xs.prev, &p); ok = 1; prm[3] = p.off; }   /* an offset that failed: the old value back in the dialog (setParams) */
    xs.prev = p; xs.built = 0;
    srf_extr_axis_out(p.ax);
    if (!ok) { if (xs.msg[0]) snprintf(msg, msgn, "Extrude: %s", xs.msg); return 0; }
    int shaded = solid_mode != 0, live = cfg_live_color;
    int col = live == color && live != cfg_back_color ? srf_front_color : live;   /* put_surf_in_db's rule */
    int mode = p.hfl;
    V3 bp = { xs.base[0], xs.base[1], xs.base[2] }, z0 = { 0, 0, 0 };
    xs_keep(mcam_new_ent(&F, E_POINT, bp, bp, z0, 0, 0, 0, level, live));
    V3 be = { xs.base[0] + xs.axis[0], xs.base[1] + xs.axis[1], xs.base[2] + xs.axis[2] };
    int le = mcam_new_ent(&F, E_LINE, bp, be, z0, 0, 0, 0, level, live); mcam_ent_blank(&F, le, 1); xs_keep(le);
    if (mode == 1) xs.ccw = !xs.ccw;
    xs_walls(0, level, col, shaded, tol);
    if (mode == 2) xs_walls(1, level, col, shaded, tol);
    xs_cap(mode == 2 ? 2 : 0, mode == 1, level, col, col, shaded, tol);
    xs_cap(1, mode == 1, level, col, col, shaded, tol);
    if (mode == 1) xs.ccw = !xs.ccw;
    if (xs.msg[0]) snprintf(msg, msgn, "Extrude: %s", xs.msg);
    scene_surfs_update(0);
    *list = xs_list; *nlist = xs_list_n; xs.built = 1;
    return xs_list_n;
}
/* the axis the dialog shows (setParams): the panel's axis fields */
static double xs_axis_shown[3] = { 0, 0, 1 };
void srf_extr_axis_out(const double a[3]) { memcpy(xs_axis_shown, a, sizeof xs_axis_shown); }
void srf_extr_axis(double a[3]) { memcpy(a, xs_axis_shown, sizeof xs_axis_shown); }
void srf_extr_reset(void) { xs.ok = 0; xs.built = 0; }
int srf_extr_built(void) { return xs.built; }
/* the base point (0x67 + get_event: a point farther than tol from the base replaces it) */
int srf_extr_base(V3 w) {
    if (!xs.ok) return 0;
    double d = sqrt((w.x - xs.base[0]) * (w.x - xs.base[0]) + (w.y - xs.base[1]) * (w.y - xs.base[1]) + (w.z - xs.base[2]) * (w.z - xs.base[2]));
    if (!(mc_systol.tol <= d && d != mc_systol.tol)) return 0;
    xs.base[0] = w.x; xs.base[1] = w.y; xs.base[2] = w.z; return 1;
}

/* ------------------------------------------------------------------------------------------------ Simple: Block / Cylinder (G_SBLOCK / G_SCYL) */
/* create_block / create_cylinder (mccad create_sz_block 0x180013ae0 / create_sz_cylinder 0x18002ad50, decomp/surfcreate/prims.c): the
   panel loops FUN_180011b50 / FUN_180029390 build at their end FUN_18000dbc0 / FUN_1800270e0 - a point at the base point (store_ent in
   the live colour, then FUN_180025060: the main colour), sld_basic_primitive (block 0xf / cylinder 0x11), with Surfaces FUN_1800146a0
   (SolidBodiesToSurfaces, the solid deleted, every surface made gets surfaceFrontColor) and for the block FUN_18000d7d0. The solid and
   its faces: occt_prim.cpp (decision S-1); each face as ps_face_to_mc_surf (mcsurf_build.c mcsb_prim_*). */
static void unit3(double v[3]) { double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 0) for (int k = 0; k < 3; k++) v[k] /= l; }
static void cross(const double a[3], const double b[3], double o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static void cplane_dir(int k, double w[3]) {             /* the Cplane's axis k (view_to_world of a unit vector) */
    double z[3] = { 0, 0, 0 }, v[3] = { 0, 0, 0 }, o[3], w0[3]; v[k] = 1;
    int id = plane_active[0] > 0 ? plane_active[0] : 1;
    plane_v2w(id, z, v, o); plane_v2w(id, z, z, w0); for (int i = 0; i < 3; i++) w[i] = o[i] - w0[i]; unit3(w);
}
/* FUN_1800150a0: x made perpendicular to the axis when |x . a| > 0.01 - c = a x x (unit; below tol: x = (1, 0, 0) unless a is about
   x, then (0, 1, 0)), x = c x a */
static void perp_x(const double a[3], double x[3], double tol) {
    if (fabs(a[0] * x[0] + a[1] * x[1] + a[2] * x[2]) <= 0.01) return;
    double c[3]; cross(a, x, c); double l = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    if (l <= tol) { double e[3] = { 0, 0, 0 }; if (fabs(fabs(a[0]) - 1.0) >= tol) e[0] = 1; else e[1] = 1; cross(a, e, c); }
    unit3(c); cross(c, a, x);
}
/* FUN_18000d4f0 / FUN_18000d5e0: a negative length / width made positive, the anchor mirrored (1 <-> 7, 2 <-> 8, 3 <-> 9; 1 <-> 3,
   4 <-> 6, 7 <-> 9; the decoded tables' gaps kept: length case 7 (anchor 7 -> 1 without the sign change), width case 7 likewise) */
static void blk_norm_l(double *L, int *an) {
    if (*L >= 0) return;
    switch (*an) { case 1: *an = 7; break; case 2: *an = 8; break; case 3: *an = 9; break; case 7: *an = 1; return; case 8: *an = 2; break; case 9: *an = 3; break; default: break; }
    *L = -*L;
}
static void blk_norm_w(double *W, int *an) {
    if (*W >= 0) return;
    switch (*an) { case 1: *an = 3; break; case 3: *an = 1; break; case 4: *an = 6; break; case 6: *an = 4; break; case 7: *an = 9; return; case 9: *an = 7; break; default: break; }
    *W = -*W;
}
/* a Cplane vector (view_to_world of v; create_sz_*: SetActivePlane(WCS, Cplane) then GetActiveViewNo(WCS) - the Cplane's view) */
static void cplane_vec(const double v[3], double w[3]) {
    double x[3], y[3], z[3]; cplane_dir(0, x); cplane_dir(1, y); cplane_dir(2, z);
    for (int k = 0; k < 3; k++) w[k] = v[0] * x[k] + v[1] * y[k] + v[2] * z[k];
}
/* the frame of a Simple command: A = the axis, X = the reference, Y = A x X. Block (FUN_18000e200, axis radios FUN_18000ed50 / edd0 /
   ee50: X / +Y / Z): X = the Cplane's X made perpendicular (FUN_1800150a0). The others (radios FUN_180027bc0 / 27c50 / 27ce0
   cylinder, FUN_180009e80.. sphere, FUN_18002c440.. torus, FUN_180030cb0.. cone - decomp/surfcreate/prim_axis.c): X axis (1,0,0) ref
   (0,1,0); Y axis (0,-1,0) ref (1,0,0); Z axis (0,0,1) ref (1,0,0), in the Cplane */
void srf_prim_frame(int kind, int axis, double A[3], double X[3], double Y[3]) {
    double tol = mc_systol_units(mcam_is_metric(&F))->tol;
    if (axis < 0 || axis > 2) axis = 2;
    if (kind == 0) { cplane_dir(axis, A); cplane_dir(axis == 0 ? 1 : 0, X); perp_x(A, X, tol); }
    else { static const double av[3][3] = { { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, 1 } }, rv[3][3] = { { 0, 1, 0 }, { 1, 0, 0 }, { 1, 0, 0 } };
           cplane_vec(av[axis], A); cplane_vec(rv[axis], X); unit3(A); unit3(X); }
    cross(A, X, Y);
}
/* FUN_18000d360: the anchor follows the signs of the picked length (1/2/3 <-> 7/8/9) and width (1/4/7 <-> 3/6/9) */
void srf_blk_anchor_signs(int *an, double L, double W) {
    if (L < 0) { if (*an == 1) *an = 7; else if (*an == 2) *an = 8; else if (*an == 3) *an = 9; }
    if (L > 0) { if (*an == 7) *an = 1; else if (*an == 8) *an = 2; else if (*an == 9) *an = 3; }
    if (W < 0) { if (*an == 1) *an = 3; else if (*an == 4) *an = 6; else if (*an == 7) *an = 9; }
    if (W > 0) { if (*an == 3) *an = 1; else if (*an == 6) *an = 4; else if (*an == 9) *an = 7; }
}
/* ps_face_to_mc_surf over the faces (mcsurf_build.c mcsb_prim_*): a plain NURBS or a blanked base + its trimmed surface each; the base
   in put_surf_in_db's colour (main -> surfaceFrontColor), the face's surface in fcol (FUN_1800146a0 / sld_convert_sel_to_mc_ents).
   list: the surfaces made (up to cap). The count made. */
int srf_faces_make(const OpFace *fs, int nf, int level, int color, int fcol, int *list, int *nlist, int cap) {
    double tol = mc_systol_units(mcam_is_metric(&F))->tol;
    int shaded = solid_mode != 0, made = 0, bcol = color == geo_color && color != cfg_back_color ? srf_front_color : color;
    for (int i = 0; i < nf; i++) {
        uint32_t n; uint8_t *b = mcsb_prim_nurbs(&F, &fs[i], shaded, tol, &n); if (!b) continue;
        double **P = NULL; int *c = NULL; int nl = mcsb_prim_loops(&F, b, n, &fs[i], &P, &c);
        if (nl > 0 && !mcsb_prim_untrimmed(&F, b, n, nl, P, c)) {
            int be = mcam_new_surf(&F, b, n, level, bcol);
            Surf *bs = &F.surfs[F.ents[be].srf]; bs->blanked = 1; bs->rec[5] |= 1; F.ents[be].blanked = 1;   /* created blanked (create_nurb_surf) */
            uint32_t tn; uint8_t *t = mcsb_prim_trim(&F, F.ents[be].septr, F.surfs[F.ents[be].srf].rec, n, nl, P, c, shaded, tol, &tn);
            if (t) { int e = mcam_new_surf(&F, t, tn, level, fcol); free(t); if (*nlist < cap) list[(*nlist)++] = e; made++; }
        } else if (nl >= 0) { int e = mcam_new_surf(&F, b, n, level, fcol); if (*nlist < cap) list[(*nlist)++] = e; made++; }
        for (int l = 0; l < (nl > 0 ? nl : 0); l++) free(P[l]); free(P); free(c); free(b);
    }
    return made;
}
/* cad_import.c: the faces read from a STEP / IGES file (solids off, free shells or faces): trimmed surfaces in the main attributes (formats/cad_import.md) */
int cad_make_surfaces(McamFile *f, const OpFace *faces, int n, int level, int color) {
    (void)f; int nlist = 0, *list = malloc(sizeof(int) * (n > 0 ? n : 1));
    int made = srf_faces_make(faces, n, level, color, color == geo_color ? srf_front_color : color, list, &nlist, n > 0 ? n : 1);
    free(list); if (made) scene_surfs_update(0);
    return made;
}
/* kind 0 block: p = length, width, height, rotation (deg), anchor 1 .. 9, flip state (button 575: 0, 1 = height negative, 2 = both
   directions); 1 cylinder: radius, height, start, end angle (deg), -, flip; 2 sphere: radius, start, end; 3 cone: base radius, top
   radius, height, start, end, flip; 4 torus: major, minor radius, start, end. axis: 0 X, 1 Y, 2 Z (srf_prim_frame). */
int srf_prim_exec(int kind, V3 base, const double p[6], int axis, int level, int color, char *msg, size_t msgn) {
    double tol = mc_systol_units(mcam_is_metric(&F))->tol;
    double A[3], X[3], Yf[3]; srf_prim_frame(kind, axis, A, X, Yf);
    int flip = (int)p[5]; double org[3] = { base.x, base.y, base.z }, d[6] = { 0 }, ref[3];
    if (kind == 0) {
        double L = p[0], W = p[1], H = p[2], rot = p[3]; int an = (int)p[4]; if (an < 1 || an > 9) an = 1;   /* the height signed
                                                             (FUN_18000efd0 state 1 makes it -|height| in the panel) */
        blk_norm_l(&L, &an); blk_norm_w(&W, &an);         /* FUN_18000d450 */
        if (H < 0) { for (int k = 0; k < 3; k++) A[k] = -A[k]; H = -H; rot = -rot; W = -W; blk_norm_w(&W, &an); }
        if (fabs(L) <= tol || fabs(W) <= tol || fabs(H) <= tol) { snprintf(msg, msgn, "Enter the length, width and height"); return 0; }
        perp_x(A, X, tol);                                /* FUN_1800102d0 / FUN_18000e200's frame (again for a flipped axis) */
        double Y[3]; cross(A, X, Y);
        double c = cos(rot * M_PI / 180), s = sin(rot * M_PI / 180);
        for (int k = 0; k < 3; k++) ref[k] = X[k] * c + Y[k] * s;
        unit3(ref);
        /* FUN_1800112e0: the anchor's offset (frame X, Y), rotated by the angle (not for anchor 5) */
        static const double ox[10] = { 0, .5, .5, .5, 0, 0, 0, -.5, -.5, -.5 }, oy[10] = { 0, .5, 0, -.5, .5, 0, -.5, .5, 0, -.5 };
        double dx = ox[an] * L, dy = oy[an] * W;
        if (an != 5 && fabs(rot) > 1e-12) { double ndy = c * dy + s * dx; dx = c * dx - s * dy; dy = ndy; }
        for (int k = 0; k < 3; k++) org[k] += dx * X[k] + dy * Y[k];
        if (flip == 2) { for (int k = 0; k < 3; k++) org[k] -= H * A[k]; H *= 2; }   /* +0x90 both directions */
        d[0] = L; d[1] = W; d[2] = H; d[3] = 0;
    } else if (kind == 1 || kind == 3) {               /* FUN_180028a10 / FUN_180031f80: both directions: origin - h a, 2h; h < 0: the axis
                                                          negated (the angles kept: param_4 0) */
        double H = kind == 1 ? p[1] : p[2];
        memcpy(ref, X, sizeof ref);
        if (flip == 2) { for (int k = 0; k < 3; k++) org[k] -= H * A[k]; H *= 2; }
        if (H < 0) { H = -H; for (int k = 0; k < 3; k++) A[k] = -A[k]; }
        if (kind == 1) {
            double r = p[0], a0 = p[2], a1 = p[3];
            if (r < tol || H < tol || a0 < 0 || a0 > 360 || a1 < 0 || a1 > 360) { snprintf(msg, msgn, "Enter the radius and height (angles 0 .. 360)"); return 0; }   /* ps_create_cylinder's checks */
            d[0] = r; d[1] = H; d[2] = a0; d[3] = a1;
        } else {
            double r0 = p[0], r1 = p[1], a0 = p[3], a1 = p[4];
            if (r1 < 0 && r1 > -tol) r1 = 0;             /* the taper's top radius at the largest angle (FUN_1800340d0: atan2(r, h)) rounds to
                                                            about -1e-15; ours: 0 (OCCT refuses a negative radius) */
            if (!((tol < r0 || tol < r1) && tol <= r0 && tol <= H) || r1 < 0 || a0 < 0 || a0 > 360 || a1 < 0 || a1 > 360) {   /* ps_create_cone */
                snprintf(msg, msgn, "Enter the base radius and height (angles 0 .. 360)"); return 0; }
            d[0] = r0; d[1] = r1; d[2] = H; d[3] = a0; d[4] = a1;
        }
    } else if (kind == 2) {                              /* FUN_18000abf0 -> ps_create_sphere (radius >= tol) */
        memcpy(ref, X, sizeof ref);
        double r = p[0], a0 = p[1], a1 = p[2];
        if (r < tol || a0 < 0 || a0 > 360 || a1 < 0 || a1 > 360) { snprintf(msg, msgn, "Enter the radius (angles 0 .. 360)"); return 0; }
        d[0] = r; d[1] = a0; d[2] = a1;
    } else {                                             /* FUN_18002d8f0 -> ps_create_torus (tol <= minor <= major) */
        memcpy(ref, X, sizeof ref);
        double R = p[0], r = p[1], a0 = p[2], a1 = p[3];
        if (R < tol || r < tol || r > R || a0 < 0 || a0 > 360 || a1 < 0 || a1 > 360) { snprintf(msg, msgn, "Enter the radius and the minor radius (minor <= radius, angles 0 .. 360)"); return 0; }
        d[0] = R; d[1] = r; d[2] = a0; d[3] = a1;
    }
    OpFace *fs; int nf; char err[200];
    if (occt_prim_faces(kind, org, A, ref, d, &fs, &nf, err, sizeof err)) { snprintf(msg, msgn, "%s", err); return 0; }
    V3 z0 = { 0, 0, 0 }; geo_add(E_POINT, base, base, z0, 0, 0, 0);   /* the base point (main colour) */
    int list[64], nlist = 0;
    int made = srf_faces_make(fs, nf, level, color, srf_front_color, list, &nlist, 64);
    occt_prim_free(fs, nf);
    if (kind == 0) {                                     /* FUN_180014580 (the solid's box centre) + FUN_18000d7d0 */
        double C[3]; for (int k = 0; k < 3; k++) C[k] = org[k] + A[k] * d[2] * 0.5;
        for (int i = 0; i < nlist; i++) { Surf *s = &F.surfs[F.ents[list[i]].srf]; mcsb_prim_outward(&F, s->rec, s->n, C); }
    }
    if (made) scene_surfs_update(0);
    return made;
}

/* ------------------------------------------------------------------------------------------------ Flat Boundary (G_SFLAT in cui_geo.c) */
/* FlatBoundarySurfaces (mccad 0x18033fe60, SurfaceFlatBoundaryApp: decomp/surfcreate/create_cmds.c l.4139, decomp/surfflat/): chain_it
   (mccadres 7423), the build FUN_18033d800: open chains -> message_box_yes_no(7422 "Boundary Curve or Chain is not closed. Do you want to
   automatically close it?", 5073) - No drops them; one chain of one entity: trim_surf_from_flat_curve; else FUN_18033e460 groups the
   chains (outer + islands, by their boxes) and trim_surf_from_flat_chains makes one surface per group (mccore, decomp/surfflat/
   trimflat.c); FUN_18033f400: store_new_trim_surf(rec, 1 (the base blanked), 1 (main attributes)) in the live colour. */
typedef struct { const Ent *e; int rev; } FbEnt;
static double fb_dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static const CurveRec *fb_crv(const Ent *e) { return e->type == E_SPLINE && e->crv >= 0 && e->crv < F.ncrvs ? &F.crvs[e->crv] : NULL; }
static void fb_t1t2(const Ent *e, double *t1, double *t2) {        /* curve_t1t2 */
    const CurveRec *c = fb_crv(e); if (c) mcs_spline_t1t2(c, t1, t2); else { *t1 = 0; *t2 = 1; }
}
static int fb_eval(const Ent *e, double t, double p[3]) {         /* eval_curve mode 0 */
    if (e->type == E_LINE) { p[0] = e->a.x + t * (e->b.x - e->a.x); p[1] = e->a.y + t * (e->b.y - e->a.y); p[2] = e->a.z + t * (e->b.z - e->a.z); return 0; }
    if (e->type == E_ARC) { V3 q = mcam_arc_pt(e, e->a0 + e->sw * t); p[0] = q.x; p[1] = q.y; p[2] = q.z; return 0; }
    const CurveRec *c = fb_crv(e); return c ? mcs_curve_eval(&F, c, t, p) : -1;
}
static int fb_curve(const Ent *e) { return e->type == E_LINE || e->type == E_ARC || (e->type == E_SPLINE && fb_crv(e) && mcs_spline_ok(fb_crv(e))); }
/* find_chain_normal (mccore 0x1808a7f80): the chain's points (a line its end, an arc 2 inside + end, a spline 19 inside + end, as
   traversed) in 3 groups (n / 3, the remainder to the first ones), their centroids G0 G1 G2: n = unit((G1 - G0) x (G2 - G0)), the depth
   = the mean of Gi . n. FUN_1808a9200 (one entity): an arc = its view's z and depth; a spline 21 points from t1 in 3 groups of 7 */
static int fb_normal(const FbEnt *E, int n, double N[3], double *depth) {
    int tot = 0, single = n == 1;
    for (int i = 0; i < n; i++) tot += E[i].e->type == E_LINE ? 1 : E[i].e->type == E_ARC ? 3 : 20;
    if (single && E[0].e->type == E_ARC) {
        const Ent *a = E[0].e;
        if (a->vw) { N[0] = a->vm[6]; N[1] = a->vm[7]; N[2] = a->vm[8]; } else { N[0] = 0; N[1] = 0; N[2] = 1; }
        *depth = N[0] * a->c.x + N[1] * a->c.y + N[2] * a->c.z; return 1;
    }
    int grp[3]; double G[3][3] = { { 0 } };
    if (single) { grp[0] = grp[1] = grp[2] = 7; tot = 21; }
    else { if (tot < 3) return 0; int q = tot / 3, rem = tot - 3 * q; grp[0] = grp[1] = grp[2] = q; if (rem > 0) grp[0]++; if (rem == 2) grp[1]++; }
    int gi = 0, cnt = 0;
#define ADDP(P) do { if (gi < 3) { for (int k_ = 0; k_ < 3; k_++) G[gi][k_] += (P)[k_]; if (++cnt == grp[gi]) { gi++; cnt = 0; } } } while (0)
    if (single) {
        double t1, t2, p[3]; fb_t1t2(E[0].e, &t1, &t2);
        for (int s = 0; s < 21; s++) { if (fb_eval(E[0].e, t1 + s * (t2 - t1) / 21.0, p)) return 0; ADDP(p); }
    } else for (int i = 0; i < n; i++) {
        const Ent *e = E[i].e; double t1, t2, p[3]; fb_t1t2(e, &t1, &t2);
        if (e->type != E_LINE) {
            int k = e->type == E_ARC ? 3 : 20; double t = E[i].rev ? t2 : t1, d = E[i].rev ? t1 - t2 : t2 - t1;
            for (int s = 0; s < k - 1; s++) { t += d / k; if (fb_eval(e, t, p)) return 0; ADDP(p); }
        }
        if (fb_eval(e, E[i].rev ? t1 : t2, p)) return 0; ADDP(p);
    }
#undef ADDP
    for (int g = 0; g < 3; g++) for (int k = 0; k < 3; k++) G[g][k] /= grp[g];
    double u[3], v[3]; for (int k = 0; k < 3; k++) { u[k] = G[1][k] - G[0][k]; v[k] = G[2][k] - G[0][k]; }
    cross(u, v, N); double l = sqrt(fb_dot(N, N)); if (l < 1e-12) return 0;   /* UnitNormal */
    for (int k = 0; k < 3; k++) N[k] /= l;
    *depth = (fb_dot(G[0], N) + fb_dot(G[1], N) + fb_dot(G[2], N)) / 3.0;
    return 1;
}
/* the box samples (FUN_1808a85a0 / FUN_1808a9640): a line its 2 ends, a parametric spline (segments + 1) x 5, a NURBS curve points x 5,
   others (arcs) 9, evenly over t1 .. t2 */
static void fb_box_ent(const Ent *e, const double M[9], double b[4]) {
    const CurveRec *c = fb_crv(e); int k = e->type == E_LINE ? 2 : 9;
    if (c && c->cls == 8 && c->n >= 0xb4) { int ns; memcpy(&ns, c->rec + 0xb0, 4); k = (ns + 1) * 5; }
    else if (c && c->cls == 0x80 && c->n >= 0xd0) { int nc; memcpy(&nc, c->rec + 0xcc, 4); k = nc * 5; }
    double t1, t2, p[3]; fb_t1t2(e, &t1, &t2);
    for (int s = 0; s < k; s++) {
        if (fb_eval(e, k > 1 ? t1 + s * (t2 - t1) / (k - 1) : t1, p)) continue;
        double x = fb_dot(p, M), y = fb_dot(p, M + 3);
        if (x < b[0]) b[0] = x; if (x > b[1]) b[1] = x; if (y < b[2]) b[2] = y; if (y > b[3]) b[3] = y;
    }
}
/* the chain's length (length_ent: CurveLength for splines - MaCurve's length NOT DECODED, ours: 200 chords) */
static double fb_len(const Ent *e) {
    if (e->type == E_LINE) return sqrt((e->b.x - e->a.x) * (e->b.x - e->a.x) + (e->b.y - e->a.y) * (e->b.y - e->a.y) + (e->b.z - e->a.z) * (e->b.z - e->a.z));
    if (e->type == E_ARC) return fabs(e->sw) * e->r;
    double t1, t2, p[3], q[3], L = 0; fb_t1t2(e, &t1, &t2);
    if (fb_eval(e, t1, p)) return 0;
    for (int s = 1; s <= 200; s++) { if (fb_eval(e, t1 + s * (t2 - t1) / 200, q)) break; L += sqrt((q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2])); memcpy(p, q, sizeof p); }
    return L;
}
/* one loop's uv points (trim_surf_from_flat_chains l.~150-330, surf_use_chord_dev on - mccore's static 1, surf_step_dist 0.1 unused):
   a line 1 step, an arc round(|sweep| / sqrt(8 dev / r)) + 1 equal steps (1 when that angle < tol), the end exact; a spline from its
   start, brk_spline_const_err (tol x 10, its length, dev) until within 1e-4 of its end; each entity's first point only for the loop's
   first; then the loop's first point again when the last is more than 1e-6 away. uv = (view x - c0 x) / |c1 - c0|, (view y - c0 y) /
   |c2 - c0| */
static int fb_loop(const FbEnt *E, int n, const double M[9], const double c0[2], double du, double dv, double dev, double tol, double **P, int *m) {
    int cap = 64; *P = malloc(sizeof(double) * 2 * cap); *m = 0;
#define PUSHP(p) do { if (*m + 2 >= cap) { cap *= 2; *P = realloc(*P, sizeof(double) * 2 * cap); } (*P)[2 * *m] = (fb_dot((p), M) - c0[0]) / du; (*P)[2 * *m + 1] = (fb_dot((p), M + 3) - c0[1]) / dv; (*m)++; } while (0)
    for (int i = 0; i < n; i++) {
        const Ent *e = E[i].e; int rev = E[i].rev; double t1, t2, p[3]; fb_t1t2(e, &t1, &t2);
        if (e->type == E_SPLINE) {
            const CurveRec *c = fb_crv(e); double L = fb_len(e), t = rev ? t2 : t1, te = rev ? t1 : t2; int first = 1, guard = 0;
            while (fabs(te - t) > 0.0001 && guard++ < 100000) {
                if (!first) mcs_brk_spline_const_err(c, tol * 10.0, L, dev, rev, tol, &t);
                if (fb_eval(e, t, p)) return -1;
                if (!first || *m == 0) PUSHP(p);
                first = 0;
            }
            continue;
        }
        int k = 1;
        if (e->type == E_ARC) { double th = sqrt(dev * 8.0 / e->r); if (th >= tol) k = (int)floor(fabs(e->sw) / th + 0.5) + 1; }
        for (int s = 0; s <= k; s++) {
            if (s == 0 && *m > 0) continue;
            double t = s == k ? (rev ? t1 : t2) : rev ? t2 - s * (t2 - t1) / k : t1 + s * (t2 - t1) / k;   /* creep_along_ent: equal lengths */
            if (fb_eval(e, t, p)) return -1;
            PUSHP(p);
        }
    }
#undef PUSHP
    if (*m > 0 && hypot((*P)[2 * *m - 2] - (*P)[0], (*P)[2 * *m - 1] - (*P)[1]) > 1e-6) { (*P)[2 * *m] = (*P)[0]; (*P)[2 * *m + 1] = (*P)[1]; (*m)++; }
    return 0;
}
static int fb_chain_ents(const Chain *ch, FbEnt *E, int cap) {
    int n = 0;
    for (int k = 0; k < ch->n && n < cap; k++) { int e = ch->ent[k]; if (e < 0 || e >= F.nents || !F.ents[e].type || !fb_curve(&F.ents[e])) continue; E[n].e = &F.ents[e]; E[n].rev = ch->rev ? ch->rev[k] : 0; n++; }
    return n;
}
/* one group (an outer chain and its islands) -> base + trimmed surface; 0 = none (*why set) */
static int fb_group(const Chain **G, int ng, int single, int level, int shaded, double tol, double dev, const char **why) {
    FbEnt *E0 = malloc(sizeof *E0 * 4096); int n0 = fb_chain_ents(G[0], E0, 4096);
    double N[3], depth, M[9];
    if (!n0 || !fb_normal(E0, n0, N, &depth) || !mcsb_view_k(N, M)) { free(E0); *why = "the boundary's plane was not found"; return 0; }
    free(E0);
    /* FUN_1808a85a0: the box of every chain, the chain holding all four extremes (strictly first) = the outer one; 10 % margins */
    double b[4] = { 1e38, -1e38, 1e38, -1e38 }; int own[4] = { -1, -1, -1, -1 };
    FbEnt *E = malloc(sizeof *E * 4096);
    for (int g = 0; g < ng; g++) {
        int n = fb_chain_ents(G[g], E, 4096); double cb[4] = { 1e38, -1e38, 1e38, -1e38 };
        for (int i = 0; i < n; i++) fb_box_ent(E[i].e, M, cb);
        if (cb[0] < b[0]) { b[0] = cb[0]; own[0] = g; } if (cb[1] > b[1]) { b[1] = cb[1]; own[1] = g; }
        if (cb[2] < b[2]) { b[2] = cb[2]; own[2] = g; } if (cb[3] > b[3]) { b[3] = cb[3]; own[3] = g; }
    }
    if (own[0] < 0 || own[0] != own[1] || own[0] != own[2] || own[0] != own[3]) { free(E); *why = "no chain encloses the others"; return 0; }
    int outer = own[0];
    double mx = (b[1] - b[0]) * 0.1, my = (b[3] - b[2]) * 0.1, x0 = b[0] - mx, x1 = b[1] + mx, y0 = b[2] - my, y1 = b[3] + my;
    double C[4][3], cz[3]; const double xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
    for (int q = 0; q < 4; q++) for (int k = 0; k < 3; k++) C[q][k] = xs[q] * M[k] + ys[q] * M[3 + k] + depth * M[6 + k];
    cplane_dir(2, cz);
    uint32_t bn; uint8_t *base = mcsb_plane4(&F, (const double (*)[3])C, cz, shaded, tol, &bn);
    if (!base) { free(E); *why = "the plane could not be made"; return 0; }
    double c0[2] = { fb_dot(C[0], M), fb_dot(C[0], M + 3) }, du = x1 - x0, dv = y1 - y0;
    double **P = calloc(ng, sizeof *P); int *cnt = calloc(ng, sizeof *cnt), nl = 0, ok = 1;
    for (int pass = 0; pass < ng && ok; pass++) {    /* the outer chain first, then the others in order */
        int g = pass == 0 ? outer : pass <= outer ? pass - 1 : pass;
        int n = fb_chain_ents(G[g], E, 4096);
        if (fb_loop(E, n, M, c0, du, dv, dev, tol, &P[nl], &cnt[nl])) ok = 0; else if (cnt[nl] > 2) nl++; else free(P[nl]);
    }
    free(E);
    int made = 0;
    if (ok && nl) {
        int be = mcam_new_surf(&F, base, bn, level, srf_front_color);
        Surf *bs = &F.surfs[F.ents[be].srf]; bs->blanked = 1; bs->rec[5] |= 1; F.ents[be].blanked = 1;   /* store_new_trim_surf(.., 1, ..): blanked */
        uint32_t tn; uint8_t *t = mcsb_flat_trim(&F, F.ents[be].septr, F.surfs[F.ents[be].srf].rec, bn, nl, P, cnt, shaded, tol, &tn);
        if (t) { mcam_new_surf(&F, t, tn, level, srf_front_color); free(t); made = 1; } else *why = "the trimmed surface could not be made";
    } else *why = "the boundary could not be followed";
    for (int l = 0; l < nl; l++) free(P[l]); free(P); free(cnt); free(base);
    (void)single;
    return made;
}
/* FUN_18033e460: each chain's box (FUN_18033e460's samples: a line its ends, an arc 9, a spline its segment / knot points) in the first
   chain's view; a chain strictly inside a group's box joins it as an island; one strictly enclosing a group's box becomes its outer (a
   second group it encloses is merged in); else it starts a group */
int srf_flat_exec(Chain *ch, int nch, int level, int color, char *msg, size_t msgn) {
    (void)color;
    const McSysTol *st = mc_systol_units(mcam_is_metric(&F)); double tol = st->tol, dev = st->surf_step_chord_dev;
    int shaded = solid_mode != 0, nopen = 0;
    for (int i = 0; i < nch; i++) if (!ch[i].closed) nopen++;
    int keep_open = 1;
    if (nopen) keep_open = cui_hooks.ask_yes_no ? cui_hooks.ask_yes_no("Mastercam", "Boundary Curve or Chain is not closed. Do you want to automatically close it?") : 1;
    const Chain **L = malloc(sizeof *L * (nch ? nch : 1)); int nl = 0;
    for (int i = 0; i < nch; i++) if (ch[i].closed || keep_open) L[nl++] = &ch[i];
    if (!nl) { free(L); return 0; }
    int made = 0; const char *why = NULL;
    FbEnt E1[2];
    if (nl == 1 && fb_chain_ents(L[0], E1, 2) == 1) made = fb_group(L, 1, 1, level, shaded, tol, dev, &why);   /* trim_surf_from_flat_curve */
    else {
        FbEnt *E = malloc(sizeof *E * 4096); int n = fb_chain_ents(L[0], E, 4096); double N[3], depth, M[9];
        if (!n || !fb_normal(E, n, N, &depth) || !mcsb_view_k(N, M)) { free(E); free(L); snprintf(msg, msgn, "Flat Boundary: the chains' plane was not found"); return 0; }
        int ng = 0; const Chain ***grp = calloc(nl, sizeof *grp); int *gn = calloc(nl, sizeof *gn); double (*gb)[4] = calloc(nl, sizeof *gb); int *dead = calloc(nl, sizeof *dead);
        for (int i = 0; i < nl; i++) {
            int ne = fb_chain_ents(L[i], E, 4096); double b[4] = { 1e38, -1e38, 1e38, -1e38 };
            for (int k = 0; k < ne; k++) {           /* FUN_18033e460's samples */
                const Ent *e = E[k].e; double t1, t2, p[3]; fb_t1t2(e, &t1, &t2); int s = 9;
                const CurveRec *c = fb_crv(e);
                if (e->type == E_LINE) s = 2; else if (c && c->cls == 8 && c->n >= 0xb4) { int ns; memcpy(&ns, c->rec + 0xb0, 4); s = ns + 1; }
                else if (c && c->cls == 0x80 && c->n >= 0xd0) { int nc; memcpy(&nc, c->rec + 0xcc, 4); s = nc; }
                for (int j = 0; j < s; j++) {
                    double t = j == 0 ? t1 : j == s - 1 ? t2 : e->type == E_ARC ? t1 + j * (t2 - t1) / (s - 1) : c && c->cls == 8 ? (double)j : t1 + j * (t2 - t1) / (s - 1);
                    if (c && c->cls == 0x80 && j > 0 && j < s - 1) { int nk, deg = c->rec[0xd0]; memcpy(&nk, c->rec + 0xc8, 4); double kv; if (deg + j < nk) { memcpy(&kv, c->rec + 0xd8 + 8 * (deg + j), 8); if (kv >= t1 && kv <= t2) t = kv; } }
                    if (fb_eval(e, t, p)) continue;
                    double x = fb_dot(p, M), y = fb_dot(p, M + 3);
                    if (x < b[0]) b[0] = x; if (x > b[1]) b[1] = x; if (y < b[2]) b[2] = y; if (y > b[3]) b[3] = y;
                }
            }
            int placed = 0, outer_of = -1;
            for (int g = 0; g < ng; g++) {
                if (dead[g]) continue;
                double *G = gb[g];
                if (G[0] <= b[0]) {
                    if (G[0] < b[0] && b[1] < G[1] && G[2] < b[2] && b[3] < G[3]) { grp[g] = realloc(grp[g], sizeof **grp * (gn[g] + 1)); grp[g][gn[g]++] = L[i]; placed = 1; }
                } else if (G[1] < b[1] && b[2] < G[2] && G[3] < b[3]) {   /* the chain encloses the group */
                    if (outer_of >= 0) {                  /* merge this group into the one it is the outer of */
                        grp[outer_of] = realloc(grp[outer_of], sizeof **grp * (gn[outer_of] + gn[g]));
                        memcpy(grp[outer_of] + gn[outer_of], grp[g], sizeof **grp * gn[g]); gn[outer_of] += gn[g]; dead[g] = 1;
                    } else { memcpy(gb[g], b, sizeof b); grp[g] = realloc(grp[g], sizeof **grp * (gn[g] + 1)); grp[g][gn[g]++] = L[i]; placed = 1; outer_of = g; }
                }
            }
            if (!placed) { grp[ng] = malloc(sizeof **grp); grp[ng][0] = L[i]; gn[ng] = 1; memcpy(gb[ng], b, sizeof b); ng++; }
        }
        free(E);
        for (int g = 0; g < ng; g++) {
            if (!dead[g]) { if (fb_group(grp[g], gn[g], 0, level, shaded, tol, dev, &why)) made++; else break; }
        }
        for (int g = 0; g < ng; g++) free(grp[g]); free(grp); free(gn); free(gb); free(dead);
    }
    free(L);
    if (!made && why) snprintf(msg, msgn, "Flat Boundary: %s", why);
    if (made) scene_surfs_update(0);
    return made;
}

/* ------------------------------------------------------------------------------------------------ From Solid (G_SSOLID in cui_geo.c) */
/* sld_convert_to_surfs (mccad 0x180312db0, SurfaceFromSolidsApp, decomp/surfsolid/): the selection (FUN_1803112e0: get_event mask 0xb21a,
   mccadres 462; bodies (events 2 / 0x10 / 0x1000) and faces (0x2000) - here bodies only), then FUN_180310fa0: UndoTransStart,
   sld_convert_sel_to_mc_ents(surfaces, .., colour flag 1 = the face's colour (FUN_1800a0730), keep / preprocess) -> ps_face_to_mc_surf
   per face; FUN_180312040: Use current attributes (manager +0x50, default 1) -> surfaceFrontColor + the main level, else each surface
   its face's colour and the body's level (FUN_1800a0340); at OK FUN_180310e60 deletes the bodies unless Keep original solid (+0x60,
   default 0). Preprocess solid (+0x70, default 0: FUN_18002fc40's copy) NOT PORTED. */
int srf_solid_sel[64], srf_solid_nsel;
int srf_solid_exec(int use_current, int level, char *msg, size_t msgn) {
    double scale = !mcam_is_metric(&F) ? 1000.0 / 25.4 : 1000.0;   /* XT metres (cam3d_rough.c's scale) */
    int made = 0;
    for (int k = 0; k < srf_solid_nsel; k++) {
        int si = srf_solid_sel[k]; if (si < 0 || si >= F.nsolids) continue;
        OpFace *fs; int nf; char err[200];
        if (F.solids[si].brep ? occt_brep_faces(F.solids[si].brep, F.solids[si].brep_n, &fs, &nf, err, sizeof err) : occt_xt_faces(F.solids[si].xt, F.solids[si].xt_size, scale, &fs, &nf, err, sizeof err)) { snprintf(msg, msgn, "From Solid: solid %d: %s", si + 1, err); continue; }
        int list[4096], nlist = 0;
        int lv = use_current ? level : F.solids[si].level;   /* the face colour (FUN_1800a0730: the face's attribute) NOT DECODED: the solid's */
        made += srf_faces_make(fs, nf, lv, use_current ? geo_color : F.solids[si].color, use_current ? srf_front_color : F.solids[si].color, list, &nlist, 4096);
        occt_prim_free(fs, nf);
    }
    if (made) scene_surfs_update(0);
    return made;
}

/* ------------------------------------------------------------------------------------------------ Loft (G_SLOFT in cui_geo.c) */
/* RuledLoftedSurfaces (mccad 0x1802b31f0) -> mccore CreateLoftOrRuledSurface -> loft_ruled_maker (formats/surfaces_loft_decode.md;
   decomp/surfloft/). The general path (chains not "alike"): break_ctour_smart resamples every chain at the same number of points
   (with unit tangents), filter_ctour_dat drops the points every chain can do without, FUN_180702fc0 fits a cubic Hermite per span
   (pt_tan_to_sp_coefs) and joins the chains ruled (make_ruled_patch: linear) - the kind 1 surface converted to NURBS by
   create_nurbsurf_from_surf (Bezier spans: integer knots, full multiplicity). */
typedef struct { double p[3], t[3]; } LfPt;
static void wdd_(uint8_t *b, double v) { memcpy(b, &v, 8); }
static void w16_(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void lf_tan(const Ent *e, double t, int rev, double o[3]) {          /* eval_curve mode 1, unit, reversed with the entity */
    double d[3] = { 0, 0, 0 };
    if (e->type == E_LINE) { d[0] = e->b.x - e->a.x; d[1] = e->b.y - e->a.y; d[2] = e->b.z - e->a.z; }
    else if (e->type == E_ARC) { double h = 1e-6; double p0[3], p1[3]; fb_eval(e, t - h < 0 ? 0 : t - h, p0); fb_eval(e, t + h > 1 ? 1 : t + h, p1); for (int k = 0; k < 3; k++) d[k] = p1[k] - p0[k]; }
    else { const CurveRec *c = fb_crv(e); double q[3][3]; if (c && !mcs_spline_eval_d(c, t, 1, q)) for (int k = 0; k < 3; k++) d[k] = q[1][k]; }
    double l = sqrt(fb_dot(d, d)); for (int k = 0; k < 3; k++) o[k] = l > 0 ? d[k] / l * (rev ? -1 : 1) : 0;
}
/* the entity's parameter at arc length s from its traversed start (creep_along_ent: lines / arcs by length, splines by chords) */
static double lf_t_at(const Ent *e, int rev, double s, double len) {
    double t1, t2; fb_t1t2(e, &t1, &t2);
    if (e->type != E_SPLINE) { double f = len > 0 ? s / len : 0; if (f < 0) f = 0; if (f > 1) f = 1; return rev ? t2 - f * (t2 - t1) : t1 + f * (t2 - t1); }
    double acc = 0, p[3], q[3], tp = rev ? t2 : t1; fb_eval(e, tp, p);
    for (int k = 1; k <= 400; k++) { double t = rev ? t2 - k * (t2 - t1) / 400 : t1 + k * (t2 - t1) / 400; fb_eval(e, t, q);
        double d = sqrt((q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]));
        if (acc + d >= s) { double f = d > 0 ? (s - acc) / d : 0; return tp + f * (t - tp); }
        acc += d; tp = t; memcpy(p, q, sizeof p); }
    return rev ? t1 : t2;
}
/* pt_tan_to_sp_coefs (mccore 0x180665510) as a Bezier span: p0, p0 + t0 L / 3, p1 - t1 L / 3, p1 (L = the chord; < tol: constant) */
static void lf_span(const LfPt *a, const LfPt *b, double tol, double bz[4][3]) {
    double L = sqrt((b->p[0] - a->p[0]) * (b->p[0] - a->p[0]) + (b->p[1] - a->p[1]) * (b->p[1] - a->p[1]) + (b->p[2] - a->p[2]) * (b->p[2] - a->p[2]));
    for (int k = 0; k < 3; k++) {
        if (L < tol) { bz[0][k] = bz[1][k] = bz[2][k] = bz[3][k] = a->p[k]; continue; }   /* FUN_180700d90 */
        bz[0][k] = a->p[k]; bz[1][k] = a->p[k] + a->t[k] * L / 3; bz[2][k] = b->p[k] - b->t[k] * L / 3; bz[3][k] = b->p[k];
    }
}
static void lf_bez(const double bz[4][3], double u, double o[3]) {
    double a = 1 - u; for (int k = 0; k < 3; k++) o[k] = a * a * a * bz[0][k] + 3 * a * a * u * bz[1][k] + 3 * a * u * u * bz[2][k] + u * u * u * bz[3][k];
}
/* proj_pt_on_coeff (mccore, NOT DECODED - ours: 64 samples + Newton): the point's distance to the span */
static double lf_proj(const double bz[4][3], const double p[3]) {
    double best = 1e300, bu = 0, q[3];
    for (int s = 0; s <= 64; s++) { double u = s / 64.0; lf_bez(bz, u, q); double d = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]); if (d < best) { best = d; bu = u; } }
    for (int it = 0; it < 20; it++) {
        double h = 1e-6, q1[3], q2[3]; lf_bez(bz, bu, q); lf_bez(bz, bu + h > 1 ? 1 : bu + h, q1); lf_bez(bz, bu - h < 0 ? 0 : bu - h, q2);
        double d1 = 0, d2 = 0; for (int k = 0; k < 3; k++) { double dq = (q1[k] - q2[k]) / (2 * h), ddq = (q1[k] - 2 * q[k] + q2[k]) / (h * h); d1 += (q[k] - p[k]) * dq; d2 += dq * dq + (q[k] - p[k]) * ddq; }
        if (fabs(d2) < 1e-300) break; double nu = bu - d1 / d2; if (nu < 0) nu = 0; if (nu > 1) nu = 1; if (fabs(nu - bu) < 1e-14) break; bu = nu;
    }
    lf_bez(bz, bu, q); double d = sqrt((q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]));
    return d < sqrt(best) ? d : sqrt(best);
}
/* FUN_1806ff5f0: an entity's point count - a line 1, an arc |sweep| / (2 acos((r - dev) / r)) + 1, a spline FUN_1806ff960 (from its
   segments, more until every Hermite span of the equal-length points keeps its middle and its knots / integer parameters within dev) */
static int lf_npts(const Ent *e, double dev, double tol) {
    if (e->type == E_LINE) return 1;
    if (e->type == E_ARC) return (int)(short)(int)(fabs(e->sw) / (2 * acos((e->r - dev) / e->r))) + 1;
    const CurveRec *c = fb_crv(e); if (!c) return 1;
    int n; if (c->cls == 8) memcpy(&n, c->rec + 0xb0, 4); else { memcpy(&n, c->rec + 0xcc, 4); n--; }
    if (n < 1) n = 1;
    double L = fb_len(e), t1, t2; fb_t1t2(e, &t1, &t2);
    for (int guard = 0; guard < 2000; guard++, n++) {
        double step = L / n; if (step < dev) return n;   /* param_2 <= step: else the count stands */
        LfPt a, b; double ta = t1; fb_eval(e, ta, a.p); lf_tan(e, ta, 0, a.t); int ok = 1;
        for (int k = 1; k <= n && ok; k++) {
            double tb = k == n ? t2 : lf_t_at(e, 0, k * step, L); if (fabs(t2 - tb) < tol) tb = t2;
            fb_eval(e, tb, b.p); lf_tan(e, tb, 0, b.t);
            double bz[4][3], m[3]; lf_span(&a, &b, tol, bz); fb_eval(e, (ta + tb) * 0.5, m);
            if (lf_proj(bz, m) > dev) ok = 0;
            if (c->cls == 8) for (int s = (int)(ta + 1e-7) + 1; ok && s <= (int)(tb - 1e-7); s++) { fb_eval(e, s, m); if (lf_proj(bz, m) > dev) ok = 0; }
            else { int nk; memcpy(&nk, c->rec + 0xc8, 4); for (int s = 0; ok && s < nk; s++) { double kv; memcpy(&kv, c->rec + 0xd8 + 8 * s, 8);
                if (ta + 1e-7 <= kv && kv <= tb - 1e-7) { fb_eval(e, kv, m); if (lf_proj(bz, m) > dev) ok = 0; } } }
            a = b; ta = tb;
        }
        if (ok) return n;
    }
    return n;
}
/* the chain's points: count N at equal lengths (break_ctour_smart / FUN_1807002a0 / FUN_1806fe0f0), the joints staying on the entity
   they end (tol), the last exact */
static int lf_sample(const FbEnt *E, int n, int N, LfPt *out) {
    double *len = malloc(sizeof(double) * (n ? n : 1)), L = 0; for (int i = 0; i < n; i++) { len[i] = fb_len(E[i].e); L += len[i]; }
    double tol = mc_systol_units(mcam_is_metric(&F))->tol;
    int ei = 0; double base = 0;
    for (int j = 0; j < N; j++) {
        double s = N > 1 ? j * L / (N - 1) : 0;
        while (ei < n - 1 && s > base + len[ei] + tol) { base += len[ei]; ei++; }
        const Ent *e = E[ei].e; double t1, t2; fb_t1t2(e, &t1, &t2);
        double t = j == N - 1 ? (E[ei].rev ? t1 : t2) : lf_t_at(e, E[ei].rev, s - base, len[ei]);
        if (fb_eval(e, t, out[j].p)) { free(len); return -1; }
        lf_tan(e, t, E[ei].rev, out[j].t);
    }
    free(len); return 0;
}
/* loft_ruled_maker's general path, ruled: one NURBS surface (kind 0 record) over the chains; NULL with *why */
typedef struct { double a[4][3]; } LfSeg;
static uint8_t *lf_lofted(LfSeg **S, int nch, int M, int shaded, double tol, uint32_t *rn, const char **why);
static void lf_span_seg(const LfPt *a, const LfPt *b, double tol, LfSeg *s);
static uint8_t *lf_general(const Chain *ch, int nch, int lofted, int shaded, uint32_t *rn, const char **why) {
    const McSysTol *st = mc_systol_units(mcam_is_metric(&F)); double tol = st->tol, dev = st->surf_step_chord_dev;
    FbEnt **E = calloc(nch, sizeof *E); int *ne = calloc(nch, sizeof *ne), N = 0;
    for (int c = 0; c < nch; c++) {                    /* FUN_1806ff040: the section's counts */
        E[c] = malloc(sizeof(FbEnt) * 4096); ne[c] = fb_chain_ents(&ch[c], E[c], 4096);
        double L = 0, step = 1e38;
        for (int i = 0; i < ne[c]; i++) { double l = fb_len(E[c][i].e); int k = lf_npts(E[c][i].e, dev, tol); double s = l / k; if (tol < s && s < step) step = s; L += l; }
        int cnt = tol < L ? (int)(short)(int)(L / step) : 1; if (cnt > N) N = cnt;
    }
    N += 1; if (N < (lofted ? 3 : 2)) N = lofted ? 3 : 2;   /* FUN_1806ff040: at least 2 (3 with param_12) */
    LfPt **P = calloc(nch, sizeof *P);
    for (int c = 0; c < nch; c++) { P[c] = malloc(sizeof(LfPt) * N); if (lf_sample(E[c], ne[c], N, P[c])) { *why = "a chain could not be followed"; goto fail; } }
    /* filter_ctour_dat: the interior points every chain can drop */
    char *drop = calloc(N, 1); int run = 0;
    for (int j = 1; j <= N - 2; j++) {
        int a = j - 1 - run, ok = 1;
        for (int c = 0; c < nch && ok; c++) { double bz[4][3]; lf_span(&P[c][a], &P[c][j + 1], tol, bz); for (int q = a + 1; q <= j && ok; q++) if (!(lf_proj(bz, P[c][q].p) < dev)) ok = 0; }
        drop[j] = (char)ok; run = ok ? run + 1 : 0;
    }
    int *keep = malloc(sizeof(int) * N), M = 0; for (int j = 0; j < N; j++) if (!drop[j]) keep[M++] = j;
    free(drop);
    if (lofted) {                                        /* FUN_180702fc0's lofted branch: the chains' Hermite spans through the kept points */
        LfSeg **SG = calloc(nch, sizeof *SG);
        for (int c = 0; c < nch; c++) { SG[c] = malloc(sizeof(LfSeg) * (M - 1)); for (int s2 = 0; s2 < M - 1; s2++) lf_span_seg(&P[c][keep[s2]], &P[c][keep[s2 + 1]], tol, &SG[c][s2]); }
        uint8_t *R = lf_lofted(SG, nch, M - 1, shaded, tol, rn, why);
        for (int c = 0; c < nch; c++) free(SG[c]); free(SG); free(keep);
        for (int c = 0; c < nch; c++) { free(E[c]); free(P[c]); } free(E); free(ne); free(P);
        return R;
    }
    int segs = M - 1, ncu = 3 * segs + 1, ncv = nch, nku = (segs + 1) * 3 + 2, nkv = (nch - 1 + 1) * 1 + 2;
    *rn = 0x118 + 8 * (uint32_t)(nku + nkv + 3 * ncu * ncv);
    uint8_t *R = calloc(1, *rn); uint32_t cls = 0x100; memcpy(R, &cls, 4);
    memcpy(R + 0x100, &nku, 4); memcpy(R + 0x104, &nkv, 4); memcpy(R + 0x108, &ncu, 4); memcpy(R + 0x10c, &ncv, 4);
    R[0x110] = 3; R[0x111] = 3; R[0x112] = 1; R[0x113] = 1;
    double *K = (double *)(R + 0x118); int k = 0;       /* FUN_180265e70's knots */
    K[k++] = 0; for (int i = 0; i <= segs; i++) for (int r = 0; r < 3; r++) K[k++] = i; K[k++] = segs;
    K[k++] = 0; for (int i = 0; i <= nch - 1; i++) K[k++] = i; K[k++] = nch - 1;
    double *C = K + nku + nkv;
    for (int c = 0; c < nch; c++) for (int s = 0; s < segs; s++) {
        double bz[4][3]; lf_span(&P[c][keep[s]], &P[c][keep[s + 1]], tol, bz);
        for (int r = 0; r < 4; r++) { int iu = 3 * s + r; for (int q = 0; q < 3; q++) C[3 * ((size_t)c * ncu + iu) + q] = bz[r][q]; }
    }
    uint8_t *S = R + 0x68; S[0] = 0; wdd_(S + 8, 0); wdd_(S + 0x10, segs); wdd_(S + 0x18, 0); wdd_(S + 0x20, nch - 1);
    uint16_t fl = 0x40;
    {   /* the normal reversed when (Su x Sv) at the middle . Cplane z < 0 (FUN_180702fc0) */
        double su[3], sv[3], N3[3], cz[3]; int sm = segs / 2, cm = (nch - 1) / 2; double uf = segs * 0.5 - sm, vf = (nch - 1) * 0.5 - cm;
        if (sm >= segs) { sm = segs - 1; uf = 1; } if (cm >= nch - 1) { cm = nch - 2; vf = 1; }
        double b0[4][3], b1[4][3], p0[3], p1[3], q0[3], q1[3], h = 1e-6;
        lf_span(&P[cm][keep[sm]], &P[cm][keep[sm + 1]], tol, b0); lf_span(&P[cm + 1][keep[sm]], &P[cm + 1][keep[sm + 1]], tol, b1);
        lf_bez(b0, uf - h < 0 ? 0 : uf - h, p0); lf_bez(b0, uf + h > 1 ? 1 : uf + h, p1); lf_bez(b1, uf - h < 0 ? 0 : uf - h, q0); lf_bez(b1, uf + h > 1 ? 1 : uf + h, q1);
        for (int q = 0; q < 3; q++) { su[q] = (1 - vf) * (p1[q] - p0[q]) + vf * (q1[q] - q0[q]); double a[3], bb[3]; lf_bez(b0, uf, a); lf_bez(b1, uf, bb); sv[q] = bb[q] - a[q]; }
        cross(su, sv, N3); cplane_dir(2, cz); if (fb_dot(N3, cz) < 0) fl |= 4;
    }
    w16_(S + 6, fl);
    free(keep);
    for (int c = 0; c < nch; c++) { free(E[c]); free(P[c]); } free(E); free(ne); free(P);
    if (mcsb_finish(&F, R, *rn, shaded, tol)) { free(R); *why = "a parameter range below 1e-12"; return NULL; }
    return R;
fail:
    for (int c = 0; c < nch; c++) { free(E[c]); free(P[c]); } free(E); free(ne); free(P);
    return NULL;
}
static int lf_alike_lofted(const Ent *const *E, const int *rv, const int *ne, int nch);
static uint8_t *lf_alike_lofted_build(const Ent *const *E, const int *rv, const int *ne, int nch, int shaded, double tol, uint32_t *rn, const char **why);
int srf_loft_exec(Chain *ch, int nch, int ruled, int level, int color, char *msg, size_t msgn) {
    if (nch < 2) { snprintf(msg, msgn, "Loft: select at least 2 chains"); return 0; }
    if (!ruled && nch == 2) ruled = 1;                   /* loft_ruled_maker: two chains are ruled */
    const char *why = NULL; uint32_t n; uint8_t *R = NULL; int tried = 0;
    double tol = mc_systol_units(mcam_is_metric(&F))->tol, cz[3]; cplane_dir(2, cz);
    const Ent **E = calloc((size_t)nch * 4096, sizeof *E); int *rv = calloc((size_t)nch * 4096, sizeof *rv), *ne = calloc(nch, sizeof *ne);
    FbEnt *tmp = malloc(sizeof(FbEnt) * 4096);
    for (int c = 0; c < nch; c++) { ne[c] = fb_chain_ents(&ch[c], tmp, 4096); for (int i = 0; i < ne[c]; i++) { E[c * 4096 + i] = tmp[i].e; rv[c * 4096 + i] = tmp[i].rev; } }
    free(tmp);
    if (srf_surface_type() != 2) { snprintf(msg, msgn, "Loft: surface type %d (configuration 10014) is NOT PORTED", srf_surface_type()); free(E); free(rv); free(ne); return 0; }
    if (ruled) {                                         /* all_chains_alike (ruled, NURBS) -> FUN_180701c20 */
        int d, rat;
        if (mcsb_loft_alike(&F, E, rv, ne, nch, tol, &d, &rat)) { tried = 1; R = mcsb_loft_alike_ruled(&F, E, rv, ne, nch, d, rat, cz, solid_mode != 0, tol, &n); if (!R) why = "the chains could not be converted"; }
    } else if (lf_alike_lofted(E, rv, ne, nch)) {        /* all_chains_alike (lofted) -> FUN_1807012f0 */
        tried = 1; R = lf_alike_lofted_build(E, rv, ne, nch, solid_mode != 0, tol, &n, &why);
    }
    free(E); free(rv); free(ne);
    if (!R && !tried) R = lf_general(ch, nch, !ruled, solid_mode != 0, &n, &why);
    if (!R) { snprintf(msg, msgn, "Loft: %s", why ? why : "failed"); return 0; }
    int col = color == geo_color && color != cfg_back_color ? srf_front_color : color;   /* put_surf_in_db(main colour) */
    mcam_new_surf(&F, R, n, level, col); free(R);
    scene_surfs_update(0);
    return 1;
}
/* ---- Lofted (LfSeg: a cubic segment, power form on [0, 1]: a0 + a1 t + a2 t^2 + a3 t^3) */
static void lf_herm(const double p0[3], const double p1[3], const double m0[3], const double m1[3], LfSeg *s) {   /* pt_tan_to_sp_coefs' result */
    for (int k = 0; k < 3; k++) { s->a[0][k] = p0[k]; s->a[1][k] = m0[k]; s->a[2][k] = 3 * (p1[k] - p0[k]) - 2 * m0[k] - m1[k]; s->a[3][k] = 2 * (p0[k] - p1[k]) + m0[k] + m1[k]; }
}
static void lf_span_seg(const LfPt *a, const LfPt *b, double tol, LfSeg *s) {        /* FUN_180702b30 */
    double L = sqrt((b->p[0] - a->p[0]) * (b->p[0] - a->p[0]) + (b->p[1] - a->p[1]) * (b->p[1] - a->p[1]) + (b->p[2] - a->p[2]) * (b->p[2] - a->p[2]));
    if (L < tol) { memset(s, 0, sizeof *s); memcpy(s->a[0], a->p, 24); return; }       /* FUN_180700d90 */
    double m0[3], m1[3]; for (int k = 0; k < 3; k++) { m0[k] = a->t[k] * L; m1[k] = b->t[k] * L; }
    lf_herm(a->p, b->p, m0, m1, s);
}
/* make_spline_coefs_open (mccore 0x180664360) with free ends (param_5 / param_7 0): chord-length parameters t_i (FUN_1806630b0: one
   below tol fails, mccoreres 5870), the tangents from 2 D0 + D1 = 3 (Q1 - Q0) / t1, t_i+1 D_i-1 + 2 (t_i + t_i+1) D_i + t_i D_i+1 = the
   3-point sums, D_n-1 + 2 D_n = 3 (Q_n - Q_n-1) / t_n (pivots below 1e-6 skipped), each span pt_tan_to_sp_coefs(Q_i, Q_i+1, D_i, D_i+1,
   t_i+1) */
static int lf_natural(const double (*Q)[3], int n, double tol, LfSeg *out) {
    double *t = malloc(sizeof(double) * n), (*D)[3] = malloc(sizeof(double) * 3 * n), *lo = malloc(sizeof(double) * n), *di = malloc(sizeof(double) * n), *up = malloc(sizeof(double) * n), (*r)[3] = malloc(sizeof(double) * 3 * n);
    int ok = 1;
    for (int i = 1; i < n; i++) { t[i] = sqrt((Q[i][0] - Q[i - 1][0]) * (Q[i][0] - Q[i - 1][0]) + (Q[i][1] - Q[i - 1][1]) * (Q[i][1] - Q[i - 1][1]) + (Q[i][2] - Q[i - 1][2]) * (Q[i][2] - Q[i - 1][2])); if (t[i] < tol) ok = 0; }
    if (ok) {
        lo[0] = 0; di[0] = 2; up[0] = 1; for (int k = 0; k < 3; k++) r[0][k] = 3.0 / t[1] * (Q[1][k] - Q[0][k]);
        for (int i = 1; i < n - 1; i++) {
            double a = t[i], b = t[i + 1]; lo[i] = b; di[i] = 2 * (a + b); up[i] = a;
            for (int k = 0; k < 3; k++) r[i][k] = -3 * b / a * Q[i - 1][k] + (3 * b / a - 3 * a / b) * Q[i][k] + 3 * a / b * Q[i + 1][k];
        }
        lo[n - 1] = 1; di[n - 1] = 2; up[n - 1] = 0; for (int k = 0; k < 3; k++) r[n - 1][k] = 3.0 / t[n - 1] * (Q[n - 1][k] - Q[n - 2][k]);
        for (int i = 1; i < n; i++) { if (fabs(di[i - 1]) <= 1e-6) continue; double m = lo[i] / di[i - 1]; di[i] -= m * up[i - 1]; for (int k = 0; k < 3; k++) r[i][k] -= m * r[i - 1][k]; }
        for (int i = n - 1; i >= 0; i--) for (int k = 0; k < 3; k++) { double v = r[i][k] - (i < n - 1 ? up[i] * D[i + 1][k] : 0); D[i][k] = fabs(di[i]) <= 1e-6 ? 0 : v / di[i]; }
        for (int i = 0; i < n - 1; i++) { double m0[3], m1[3]; for (int k = 0; k < 3; k++) { m0[k] = D[i][k] * t[i + 1]; m1[k] = D[i + 1][k] * t[i + 1]; } lf_herm(Q[i], Q[i + 1], m0, m1, &out[i]); }
    }
    free(t); free(D); free(lo); free(di); free(up); free(r);
    return ok;
}
/* make_cubic_patch (mccore 0x180700e00): G (rows: A, B, the v derivatives at u = 0 / 1 and the twists, columns u = 0, 1, d/du at 0, 1;
   A / B = the chains' segments, C / D = the cross segments at the segment's ends) -> M G M^T (M = DAT_181307bf0), then the Bezier form
   (FUN_180265e70: FUN_18026c490(4) rows C(a, k) / C(3, k)) */
static void lf_patch(const LfSeg *A, const LfSeg *B, const LfSeg *C, const LfSeg *D, double bz[4][4][3]) {   /* bz[u][v] */
    static const double M[4][4] = { { 1, 0, 0, 0 }, { 0, 0, 1, 0 }, { -3, 3, -2, -1 }, { 2, -2, 1, 1 } };
    static const double W[4][4] = { { 1, 0, 0, 0 }, { 1, 1.0 / 3, 0, 0 }, { 1, 2.0 / 3, 1.0 / 3, 0 }, { 1, 1, 1, 1 } };
    for (int k = 0; k < 3; k++) {
        double a0 = A->a[0][k], a1 = A->a[0][k] + A->a[1][k] + A->a[2][k] + A->a[3][k], ad0 = A->a[1][k], ad1 = A->a[1][k] + 2 * A->a[2][k] + 3 * A->a[3][k];
        double b0 = B->a[0][k], b1 = B->a[0][k] + B->a[1][k] + B->a[2][k] + B->a[3][k], bd0 = B->a[1][k], bd1 = B->a[1][k] + 2 * B->a[2][k] + 3 * B->a[3][k];
        double c0 = C->a[1][k], c1 = C->a[1][k] + 2 * C->a[2][k] + 3 * C->a[3][k], d0 = D->a[1][k], d1 = D->a[1][k] + 2 * D->a[2][k] + 3 * D->a[3][k];
        double tw = a0 - b0 + b1 - a1;
        double G[4][4] = { { a0, a1, ad0, ad1 }, { b0, b1, bd0, bd1 },
                           { c0, d0, bd0 - ad0 + d0 - c0 - tw, bd0 - ad0 + d1 - c1 - tw },
                           { c1, d1, bd1 - ad1 + d0 - c0 - tw, bd1 - ad1 + d1 - c1 - tw } };
        double T[4][4], R[4][4];
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int q = 0; q < 4; q++) s += M[i][q] * G[q][j]; T[i][j] = s; }
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int q = 0; q < 4; q++) s += T[i][q] * M[j][q]; R[i][j] = s; }   /* rows v powers, columns u powers */
        for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) { double s = 0; for (int p = 0; p <= a; p++) for (int q = 0; q <= b; q++) s += W[a][p] * W[b][q] * R[q][p]; bz[a][b][k] = s; }
    }
}
/* FUN_180702fc0's / FUN_1807012f0's lofted surface: chains' segments S[c][j] (nch x M), the cross splines through the segments' nodes,
   the patches; a degree 3 x 3 NURBS record (create_nurbsurf_from_surf, kind 1) */
static uint8_t *lf_lofted(LfSeg **S, int nch, int M, int shaded, double tol, uint32_t *rn, const char **why) {
    int nodes = M + 1;
    LfSeg **X = calloc(nodes, sizeof *X); double (*Q)[3] = malloc(sizeof(double) * 3 * nch);
    for (int j = 0; j < nodes; j++) {
        for (int c = 0; c < nch; c++) for (int k = 0; k < 3; k++) Q[c][k] = j < M ? S[c][j].a[0][k] : S[c][M - 1].a[0][k] + S[c][M - 1].a[1][k] + S[c][M - 1].a[2][k] + S[c][M - 1].a[3][k];
        if (sqrt((Q[nch - 1][0] - Q[0][0]) * (Q[nch - 1][0] - Q[0][0]) + (Q[nch - 1][1] - Q[0][1]) * (Q[nch - 1][1] - Q[0][1]) + (Q[nch - 1][2] - Q[0][2]) * (Q[nch - 1][2] - Q[0][2])) < tol) {
            *why = "the chains close on themselves (make_spline_coefs_closed: NOT PORTED)"; goto fail; }
        X[j] = malloc(sizeof(LfSeg) * nch);
        if (!lf_natural((const double (*)[3])Q, nch, tol, X[j])) { *why = "two chains meet (Spline segment length too small)"; goto fail; }
    }
    int ncu = 3 * M + 1, ncv = 3 * (nch - 1) + 1, nku = (M + 1) * 3 + 2, nkv = nch * 3 + 2;
    *rn = 0x118 + 8 * (uint32_t)(nku + nkv + 3 * ncu * ncv);
    uint8_t *R = calloc(1, *rn); uint32_t cls = 0x100; memcpy(R, &cls, 4);
    memcpy(R + 0x100, &nku, 4); memcpy(R + 0x104, &nkv, 4); memcpy(R + 0x108, &ncu, 4); memcpy(R + 0x10c, &ncv, 4);
    R[0x110] = 3; R[0x111] = 3; R[0x112] = 3; R[0x113] = 1;
    double *K = (double *)(R + 0x118); int q = 0;
    K[q++] = 0; for (int i = 0; i <= M; i++) for (int r = 0; r < 3; r++) K[q++] = i; K[q++] = M;
    K[q++] = 0; for (int i = 0; i <= nch - 1; i++) for (int r = 0; r < 3; r++) K[q++] = i; K[q++] = nch - 1;
    double *C = K + nku + nkv;
    for (int c = 0; c < nch - 1; c++) for (int j = 0; j < M; j++) {
        double bz[4][4][3]; lf_patch(&S[c][j], &S[c + 1][j], &X[j][c], &X[j + 1][c], bz);
        for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) { size_t iu = 3 * j + a, iv = 3 * c + b; memcpy(C + 3 * (iv * ncu + iu), bz[a][b], 24); }
    }
    uint8_t *Sh = R + 0x68; Sh[0] = 0; wdd_(Sh + 8, 0); wdd_(Sh + 0x10, M); wdd_(Sh + 0x18, 0); wdd_(Sh + 0x20, nch - 1);
    w16_(Sh + 6, 0x40);
    { double cz[3]; cplane_dir(2, cz); mcsb_normal_by(R, *rn, M * 0.5, (nch - 1) * 0.5, cz); }
    for (int j = 0; j < nodes; j++) free(X[j]); free(X); free(Q);
    if (mcsb_finish(&F, R, *rn, shaded, tol)) { free(R); *why = "a parameter range below 1e-12"; return NULL; }
    return R;
fail:
    for (int j = 0; j < nodes; j++) free(X[j]); free(X); free(Q);
    return NULL;
}
/* all_chains_alike for Lofted (mccore 0x1806fc220, not ruled): the same curve counts, every curve a line or a parametric spline of order
   <= 4 (3D, over 0 .. its segments), the k-th curves of the chains the same class (splines the same segments and order) */
static int lf_alike_lofted(const Ent *const *E, const int *rv, const int *ne, int nch) {
    for (int c = 1; c < nch; c++) if (ne[c] != ne[0]) return 0;
    for (int k = 0; k < ne[0]; k++) for (int c = 0; c < nch; c++) {
        const Ent *e = E[c * 4096 + k], *e0 = E[k];
        if (e->type == E_LINE) { if (e0->type != E_LINE) return 0; continue; }
        if (e->type != E_SPLINE || e->cls != 8 || e->crv < 0 || e->crv >= F.ncrvs || e0->type != E_SPLINE || e0->cls != 8) return 0;
        const uint8_t *Rr = F.crvs[e->crv].rec, *R0 = F.crvs[e0->crv].rec; int ns; memcpy(&ns, Rr + 0xb0, 4); double t1, t2; mcs_spline_t1t2(&F.crvs[e->crv], &t1, &t2);
        if (Rr[0xb4] > 4 || Rr[0xb5] == 2 || fabs(t1) > 1e-6 || fabs(t2 - ns) > 1e-6) return 0;
        int ns0; memcpy(&ns0, R0 + 0xb0, 4); if (ns != ns0 || Rr[0xb4] != R0[0xb4]) return 0;
    }
    (void)rv; return 1;
}
/* FUN_1807012f0 (lofted): each chain's segments as they are - a line one (FUN_1806fdc50: a0 = start, a1 = end - start), a parametric
   spline its own (order <= 4, padded), reversed ones reparametrised t -> 1 - t (FUN_1806fd3a0 / FUN_180702f20) in reverse order; then the
   cross splines and patches as the general path's */
static uint8_t *lf_alike_lofted_build(const Ent *const *E, const int *rv, const int *ne, int nch, int shaded, double tol, uint32_t *rn, const char **why) {
    LfSeg **S = calloc(nch, sizeof *S); int M = 0;
    for (int c = 0; c < nch; c++) {
        int m = 0, cap = 64; S[c] = malloc(sizeof(LfSeg) * cap);
        for (int i = 0; i < ne[c]; i++) {
            const Ent *e = E[c * 4096 + i]; int rev = rv[c * 4096 + i];
            LfSeg tmp[512]; int nt = 0;
            if (e->type == E_LINE) { memset(&tmp[0], 0, sizeof tmp[0]); double a[3] = { e->a.x, e->a.y, e->a.z }, b[3] = { e->b.x, e->b.y, e->b.z };
                for (int k = 0; k < 3; k++) { tmp[0].a[0][k] = a[k]; tmp[0].a[1][k] = b[k] - a[k]; } nt = 1; }
            else { const uint8_t *Rr = F.crvs[e->crv].rec; int ns, ord = Rr[0xb4], dim = Rr[0xb5]; memcpy(&ns, Rr + 0xb0, 4);
                for (int sg = 0; sg < ns && nt < 512; sg++, nt++) { memset(&tmp[nt], 0, sizeof tmp[nt]);
                    for (int p = 0; p < ord; p++) for (int k = 0; k < dim && k < 3; k++) { double v; memcpy(&v, Rr + 0xc0 + 8 * (((size_t)sg * ord + p) * dim + k), 8); tmp[nt].a[p][k] = v; } } }
            if (rev) {                                   /* t -> 1 - t: a(1 - t) = sum_p a_p (1 - t)^p */
                for (int q = 0; q < nt / 2; q++) { LfSeg x = tmp[q]; tmp[q] = tmp[nt - 1 - q]; tmp[nt - 1 - q] = x; }
                static const double B[4][4] = { { 1, 0, 0, 0 }, { 1, -1, 0, 0 }, { 1, -2, 1, 0 }, { 1, -3, 3, -1 } };   /* (1 - t)^p */
                for (int q = 0; q < nt; q++) { LfSeg o; memset(&o, 0, sizeof o); for (int p = 0; p < 4; p++) for (int r = 0; r <= p; r++) for (int k = 0; k < 3; k++) o.a[r][k] += tmp[q].a[p][k] * B[p][r]; tmp[q] = o; }
            }
            for (int q = 0; q < nt; q++) { if (m == cap) { cap *= 2; S[c] = realloc(S[c], sizeof(LfSeg) * cap); } S[c][m++] = tmp[q]; }
        }
        if (c == 0) M = m; else if (m != M) { *why = "the chains' segment counts differ"; for (int q = 0; q <= c; q++) free(S[q]); free(S); return NULL; }
    }
    uint8_t *R = lf_lofted(S, nch, M, shaded, tol, rn, why);
    for (int c = 0; c < nch; c++) free(S[c]); free(S);
    return R;
}


/* ------------------------------------------------------------------------------------------------ Curve Constant Parameter (G_CCONST, sketcher_curve_2) */
/* FUN_1802980f0: the picked surface and the (u, v) dyno_surf (cui_dsurf.c) returned, CreateConstantParameterCurves in mcsrfcrv.c.
   msf_nurbs_splines = cfg_createNurbs while building (surface type config 10014 == 2). */
int srf_crv_supported(int e, char *msg, size_t msgn) {     /* kinds the iso curve is ported for: NURBS, trimmed on NURBS (mcsrfcrv.c iso_base) */
    if (e == DS_TEMP) return 1;                          /* cui_cedge.c: an untrimmed face's NURBS */
    const Surf *s = &F.surfs[F.ents[e].srf], *b = s;
    if (s->kind == 10 && s->n >= 0x108) { int64_t ep; memcpy(&ep, s->rec + 0x100, 8); b = mcs_surf(&F, ep); }
    if (b && b->kind == 0) return 1;
    snprintf(msg, msgn, "Curve Constant Parameter: this surface kind (%d) is NOT PORTED (MaBicubicSurf / MaSurface::Flowline)", b ? b->kind : -1);
    return 0;
}
int srf_crv_const_exec(int e, double u, double v, double tol, int direction, int level, int color, char *msg, size_t msgn) {
    if (e != DS_TEMP && (e < 0 || e >= F.nents || F.ents[e].type != E_SURF)) return 0;
    const Surf *s = ds_surf(e);
    mcam_fresh_views(&F);
    int out[256], n = 0;
    return mcsc_const_param(&F, s, u, v, direction, tol, mc_systol_units(mcam_is_metric(&F))->tol, level, color, srf_surface_type() == 2, out, 256, &n, msg, msgn);
}

/* tests (ours): srfpt:E,FU,FV the point of surface entity E at the fractions FU, FV of its domain; crvdev:C,E the curve C sampled at 200 points against
   surface E: the largest distance to the surface and the range of the nearest (u, v) */
void srf_test_pt(int e, double fu, double fv) {
    if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) return;
    const Surf *s = &F.surfs[F.ents[e].srf]; double d[4], p[3], u, v; mcs_surf_domain(s, d);
    u = d[0] + (d[1] - d[0]) * fu; v = d[2] + (d[3] - d[2]) * fv;
    if (!mcs_surf_eval(&F, s, u, v, p)) printf("      %d srfpt uv %.9g %.9g p %.9f,%.9f,%.9f\n", e, u, v, p[0], p[1], p[2]);
}
void srf_test_dev(int c, int e) {
    if (c < 0 || c >= F.nents || e < 0 || e >= F.nents || F.ents[e].type != E_SURF) return;
    const Ent *E = &F.ents[c]; const Surf *s = &F.surfs[F.ents[e].srf]; double dm = 0, lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    const CurveRec *cr = E->type == E_SPLINE && E->crv >= 0 && E->crv < F.ncrvs ? &F.crvs[E->crv] : NULL; double t1 = 0, t2 = 1; if (cr) mcs_spline_t1t2(cr, &t1, &t2);
    for (int i = 0; i <= 200; i++) {
        double t = t1 + (t2 - t1) * i / 200.0, p[3], u, v, q[3];
        if (cr) { if (mcs_curve_eval(&F, cr, t, p)) return; }
        else if (E->type == E_LINE) { p[0] = E->a.x + (E->b.x - E->a.x) * t; p[1] = E->a.y + (E->b.y - E->a.y) * t; p[2] = E->a.z + (E->b.z - E->a.z) * t; }
        else if (E->type == E_ARC) { V3 a = mcam_arc_pt(E, E->a0 + E->sw * t); p[0] = a.x; p[1] = a.y; p[2] = a.z; }
        else return;
        if (!mcsc_uv_of_point(&F, s, p, &u, &v) || mcs_surf_eval(&F, s, u, v, q)) return;
        double dd = sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
        if (dd > dm) dm = dd;
        if (u < lo[0]) lo[0] = u; if (u > hi[0]) hi[0] = u; if (v < lo[1]) lo[1] = v; if (v > hi[1]) hi[1] = v;
    }
    double rd = -1;
    if (cr) { double a[3], b[3], d[3]; mcs_curve_eval(&F, cr, t1, a); mcs_curve_eval(&F, cr, (t1 + t2) * 0.5, b); mcs_curve_eval(&F, cr, t2, d);
        double ab[3], ac[3], n[3], x1[3], x2[3], C[3]; for (int k = 0; k < 3; k++) { ab[k] = b[k] - a[k]; ac[k] = d[k] - a[k]; }
        cross(ab, ac, n); double n2 = n[0] * n[0] + n[1] * n[1] + n[2] * n[2], m1 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2], m2 = ac[0] * ac[0] + ac[1] * ac[1] + ac[2] * ac[2];
        if (n2 > 1e-20) { cross(n, ab, x1); cross(ac, n, x2); for (int k = 0; k < 3; k++) C[k] = a[k] + (m2 * x1[k] + m1 * x2[k]) / (2 * n2);
            double r = sqrt((a[0] - C[0]) * (a[0] - C[0]) + (a[1] - C[1]) * (a[1] - C[1]) + (a[2] - C[2]) * (a[2] - C[2])); rd = 0;
            for (int i = 0; i <= 200; i++) { double p[3]; mcs_curve_eval(&F, cr, t1 + (t2 - t1) * i / 200.0, p); double e = fabs(sqrt((p[0] - C[0]) * (p[0] - C[0]) + (p[1] - C[1]) * (p[1] - C[1]) + (p[2] - C[2]) * (p[2] - C[2])) - r); if (e > rd) rd = e; } } }
    printf("      %d crvdev max %.3e u %.6f..%.6f v %.6f..%.6f circle %.3e\n", c, dm, lo[0], hi[0], lo[1], hi[1], rd);
}
/* tests (ours): crviso:C,E,DIR,PAR the spline C against the iso line of surface E (DIR 1: u = PAR, the curve's parameter is v; 2: v = PAR) at 200 samples of its
   parameter range: the largest distance, comparing the two evaluators directly */
void srf_test_iso(int c, int e, int dir, double par) {
    if (c < 0 || c >= F.nents || e < 0 || e >= F.nents || F.ents[e].type != E_SURF || F.ents[c].type != E_SPLINE) return;
    const CurveRec *cr = &F.crvs[F.ents[c].crv]; const Surf *s = &F.surfs[F.ents[e].srf]; double t1, t2, dm = 0; mcs_spline_t1t2(cr, &t1, &t2);
    for (int i = 0; i <= 200; i++) {
        double t = t1 + (t2 - t1) * i / 200.0, p[3], q[3];
        double d4[4]; mcs_surf_domain(s, d4); double ft = dir == 1 ? d4[2] + (d4[3] - d4[2]) * t : d4[0] + (d4[1] - d4[0]) * t;   /* the curve carries the normalised parameter (mcsrfcrv.c iso_pieces) */
        if (mcs_curve_eval(&F, cr, t, p) || mcs_surf_eval(&F, s, dir == 1 ? par : ft, dir == 1 ? ft : par, q)) return;
        double d = sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2])); if (d > dm) dm = d;
    }
    printf("      %d crviso max %.3e t %.6f..%.6f\n", c, dm, t1, t2);
}

/* Curve Flowline (G_CFLOW, sketcher_curve_3): FUN_180287a40 -> FUN_180287870: CreateSurfaceFlowLines(surface, direction, tolerance, method, value). Number: mcsc_flow_number; Chord height and Distance: mcsc_flow_list (TessellateUntrimmed, GenUVListByArcLength). */
int srf_crv_flow_exec(int e, int method, int number, double chord, double dist, int direction, double tol, int level, int color, char *msg, size_t msgn) {
    if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) return 0;
    if (method != 2) { mcam_fresh_views(&F); int outl[1024], nl = 0; return mcsc_flow_list(&F, &F.surfs[F.ents[e].srf], method, method == 0 ? chord : dist, direction, tol, mc_systol_units(mcam_is_metric(&F))->tol, level, color, srf_surface_type() == 2, outl, 1024, &nl, msg, msgn); }
    mcam_fresh_views(&F);
    int out[1024], n = 0;
    return mcsc_flow_number(&F, &F.surfs[F.ents[e].srf], number, direction, tol, mc_systol_units(mcam_is_metric(&F))->tol, level, color, srf_surface_type() == 2, out, 1024, &n, msg, msgn);
}
/* Curve One Edge (sketcher_curve_1, FUN_1802a2940) / All Edges (sketcher_curve_9, FUN_1802c3f10 -> FUN_1802c4a70): the surface's boundary curves (mcsrfcrv.c): surf_step_chord_dev is the Simplify tolerance (MaCurve::Simplify), the panel's Break Angle only matters on trimmed surfaces (the loops of MaFace, mcsrfcrv.c fe_loop) */
int srf_crv_edge_supported(int e, char *msg, size_t msgn) { return mcsc_edge_supported(&F, &F.surfs[F.ents[e].srf], msg, msgn); }
int srf_crv_edge_one(int e, double u, double v, double ang, int level, int color, char *msg, size_t msgn) {
    if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) return 0;
    mcam_fresh_views(&F); int out[16], n = 0;
    return mcsc_edge_one(&F, &F.surfs[F.ents[e].srf], u, v, ang, mc_systol.surf_step_chord_dev, mc_systol_units(mcam_is_metric(&F))->tol, level, color, srf_surface_type() == 2, out, 16, &n, msg, msgn);
}
int srf_crv_edge_all(int e, double ang, int level, int color, char *msg, size_t msgn) {
    if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) return 0;
    mcam_fresh_views(&F); int out[16], n = 0;
    return mcsc_edge_all(&F, &F.surfs[F.ents[e].srf], ang, mc_systol.surf_step_chord_dev, mc_systol_units(mcam_is_metric(&F))->tol, level, color, srf_surface_type() == 2, out, 16, &n, msg, msgn);
}
void srf_test_scan(void) { mcsc_scan(&F); }
void srf_test_edgcase(int e, const char *path, double ang) { if (e >= 0 && e < F.nents && F.ents[e].type == E_SURF) { double ch = mc_systol.surf_step_chord_dev; int ok = mcsc_edg_case(&F, &F.surfs[F.ents[e].srf], path, ang, ch, mc_systol.tol); char p2[1100]; snprintf(p2, sizeof p2, "%s.ours", path); if (ok) mcsc_edg_ours(&F, &F.surfs[F.ents[e].srf], p2, ch, ang); printf("      0 edgcase %d kind %d ok %d chord %g\n", e, F.surfs[F.ents[e].srf].kind, ok, ch); } }
void srf_test_edgbatch(const char *dir, double ang) { mcsc_edg_batch(&F, dir, ang, mc_systol.surf_step_chord_dev, mc_systol.tol); }
void srf_test_edgscan(void) { mcsc_edg_scan(&F, mc_systol.surf_step_chord_dev); }
void srf_test_pmdcase(int e, const char *path) { if (e >= 0 && e < F.nents && F.ents[e].type == E_SURF) mcsc_pmd_case(&F, &F.surfs[F.ents[e].srf], path); }
