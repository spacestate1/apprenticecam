/* cui_overlay.c - the overlay lines over the graphics view: the selection / chained entities in the select colour and
   the auto-highlighted entity, both as FUN_180135ad0's two-pass contrast + dash stripes (config 4056), the base drawing
   of dashed line styles (the dash buffer), the AutoCursor snap marker. Moved from viewer/mcamui.c. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"

int overlay_dirty = 1;
int cfg_select_color = 14, cfg_back_color = 111;   /* config 5033 Select color, 5007 gr_back_color (read from the user's config) */
int cfg_stipple_hl = 1;                            /* config 4053 "Use stipple on solids/surfaces?" = StippleHighlight (mastercam.dll data 0x180876679, .data 1; mccore binding 0x1801ba3c9, key 0xfd5): the shaded face highlight (scene.c scene_srf_look) */
int cfg_dashed_wire = 1;                           /* config 4056 "Use dashed on wireframe?" (GetDashedWireHightlight); factory default ON (.data initializer of DAT_18088f3e6 = 1; only SetDashedWireHightlight writes it, via the config binding) */
int cfg_autohl_color = 15;                         /* config 5000 Auto-highlight color */
int cfg_autohl = 1;                                /* config 4036 Auto-highlight (factory default on, .data init) */
int cfg_presel = 1;                                /* config 4001 Allow pre-selection (factory default on) */
/* screen colours: Mastercam colour numbers through Mastercam's palette (mastercam.dll @0x8a6440, ../formats/mastercam_palette.tsv) */
int mc_rgb(int idx, float *rgb) {                 /* Mastercam colour number -> RGB (MC_PALETTE, scene.c) */
    if (idx < 0 || idx > 255) return 0;
    for (int k = 0; k < 3; k++) rgb[k] = MC_PALETTE[idx][k] / 255.f;
    return 1;
}

/* room for a GL upload of BYTES (ours): the GL driver copies every buffer it is given and some drivers crash instead of
   failing when that copy cannot be allocated (seen: libnvidia-eglcore under ulimit -v) - probe twice the size first */
static int gl_room(size_t bytes) { if (bytes > SIZE_MAX / 2) return 0; void *p = malloc(bytes * 2); if (!p) return 0; free(p); return 1; }
static const void *st_warned;                            /* the stock model the last "cannot draw" message was about */
int ov_oom;                                              /* an overlay buffer could not grow during this build */
void ov_pt(float **pv, int *pn, int *pcap, V3 p, float r, float g, float b) {
    if (!*pv || *pcap < 0) { ov_oom = 1; return; }      /* an earlier allocation failed: the rest is dropped (ours) */
    if (*pn == *pcap) {                                  /* grow x2, checked (ours: no memory = the remaining lines are not drawn) */
        float *q = *pcap <= 0x3fffffff / 2 ? realloc(*pv, (size_t)*pcap * 2 * 24) : NULL;
        if (!q) { *pcap = -*pcap; ov_oom = 1; return; } /* full for the rest of this build (no retry per point) */
        *pv = q; *pcap *= 2;
    }
    float *q = *pv + (*pn)++ * 6; q[0] = (float)p.x; q[1] = (float)p.y; q[2] = (float)p.z; q[3] = r; q[4] = g; q[5] = b;
}
/* the GL_LINE_STIPPLE patterns of set_gl_style (mastercam.dll 0x180226bc0, decomp/mastercam_hlpaint.c:643-682);
   unknown / invalid styles are solid (the original coerces them to 1 via FUN_180329df0) */
unsigned short ln_stipple(int style) {
    switch (style & 0xff) {
    case 2: case 0xfa: case 0xfe: return 0x3333;
    case 3: return 0x33ff;
    case 4: return 0x333f;
    case 5: return 0xffff;
    case 6: return 0xaffa;
    case 7: return 0xaaaa;
    case 0xf8: return 0x5555;
    case 0xf9: return 0x5005;
    case 0xfb: return 0xccc0;
    case 0xfc: return 0xcc00;
    case 0xfd: return 0xcccc;
    }
    return 0xffff;
}
/* the GL_LINE_STIPPLE factor of set_gl_style (0x180226bc0): x_pt_size / 2.9 rounded ((int)(d + 0.5)), where
   x_pt_size = the graphics viewport's pixel width / 90 (integer division; the view field +0x1098, written by
   FUN_1801ceb50 / FUN_1803260f0 = resize / fit, overwriting the GetDeviceCaps ASPECTX value FUN_1801c0eb0 puts
   there first in FUN_1801d0a70). set_gl_style's hardcopy branch (FUN_1801c0a50 scale x DAT_1809a6058 / 100) is
   not ported - no printing. GL clamps a 0 factor (< 90 px viewport) to 1. */
int stipple_px(void) { int xps = view_w / 90, f = (int)(xps / 2.9 + 0.5); return f < 1 ? 1 : f; }
/* one pattern pass over an entity's polylines. The pattern runs continuously along the entity (bit 0 first),
   like GL_LINE_STIPPLE within one begin (the original draws one call per entity); the length walk is in screen
   px (the stipple is screen-space), stipple_px() per bit. */
void ov_entity_pat(float **pv, int *pn, int *pcap, const Ent *en, unsigned short pat, const float rgb[3]) {
    double pos = 0; int dbg0 = *pn; double px = stipple_px();
    if (en->type == E_SURF || en->type == E_NOTE || en->type == E_MESH) {   /* a surface: its drawn wireframe (scene_surf_wire = the iso lines dist_to_surf
                                                       picks), the pattern run per segment; a note: its drawn strokes (cui_note.c note_segs); a mesh: its
                                                       triangles' outlines (FUN_180216c90 draws the three edges of each, draw_line_style) */
        float *nsg = NULL; const float *sg; int ns = en->type == E_NOTE ? note_segs((int)(en - F.ents), &nsg) : en->type == E_MESH ? scene_mesh_wire(en->msh, &sg) : scene_surf_wire(en->srf, &sg);
        if (en->type == E_NOTE) sg = nsg;
        for (int k = 0; k < ns; k++) {
            const float *a = sg + 12 * k, *b = a + 6; float ax, ay, bx, by;
            if (!scene_to_screen(view_w, view_h, a[0], a[1], a[2], &ax, &ay) || !scene_to_screen(view_w, view_h, b[0], b[1], b[2], &bx, &by)) continue;
            double Lpx = hypot(bx - ax, by - ay), off = 0; if (Lpx < 1e-9) continue;
            while (off < Lpx) {
                double nxt = (floor(pos / px) + 1) * px - pos; if (off + nxt > Lpx) nxt = Lpx - off;
                if ((pat >> ((int)(pos / px) & 15)) & 1) {
                    double t0 = off / Lpx, t1 = (off + nxt) / Lpx;
                    V3 p0 = { a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0, a[2] + (b[2] - a[2]) * t0 };
                    V3 p1 = { a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1, a[2] + (b[2] - a[2]) * t1 };
                    ov_pt(pv, pn, pcap, p0, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, p1, rgb[0], rgb[1], rgb[2]);
                }
                off += nxt; pos += nxt;
            }
        }
        free(nsg);
        return;
    }
    for (int pi = en->first_poly; pi < en->first_poly + en->npoly; pi++) {
        const Poly *p = &F.polys[pi];
        for (int k = 1; k < p->count; k++) {
            V3 a = F.pts[p->first + k - 1], b = F.pts[p->first + k];
            float ax, ay, bx, by;
            if (!scene_to_screen(view_w, view_h, a.x, a.y, a.z, &ax, &ay)) continue;
            if (!scene_to_screen(view_w, view_h, b.x, b.y, b.z, &bx, &by)) continue;
            double Lpx = hypot(bx - ax, by - ay); if (Lpx < 1e-9) continue;
            double off = 0;
            while (off < Lpx) {
                double nxt = (floor(pos / px) + 1) * px - pos; if (off + nxt > Lpx) nxt = Lpx - off;
                if ((pat >> ((int)(pos / px) & 15)) & 1) {
                    double t0 = off / Lpx, t1 = (off + nxt) / Lpx;
                    V3 p0 = { a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0, a.z + (b.z - a.z) * t0 };
                    V3 p1 = { a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1, a.z + (b.z - a.z) * t1 };
                    ov_pt(pv, pn, pcap, p0, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, p1, rgb[0], rgb[1], rgb[2]);
                }
                off += nxt; pos += nxt;
            }
        }
    }
    if (getenv("MCAMUI_OV_DEBUG")) fprintf(stderr, "  pat %04x: emitted %d verts (view %d x %d)\n", pat, *pn - dbg0, view_w, view_h);
}
/* the highlight / selection draw of one entity = FUN_180135ad0 (mastercam.dll 2017), the per-entity draw used by
   the auto-highlight (call 0x18031f4fd, r9 = 1, the config-5000 colour), the selected-list repaint (0x1801d54d1)
   and the display-list draw (0x18032b724). Pass start (0x180135baa): two passes when the entity is highlighted /
   selected AND config 4056 "Use dashed on wireframe?" (GetDashedWireHightlight 0x180320760 = DAT_18088f3e6) is
   on, else one pass in the colour with the entity's own style. The two passes (0x180135c01-0x180135c85):
     pass 1: the contrast colour (white 15, or black 0 when the colour's luma 0.30 R + 0.59 G + 0.11 B > 0x7f -
             Wd_colors is a COLORREF) with style s when s is 1 or 5 ((s - 1) & 0xfffb == 0), else ~s;
     pass 2: the highlight / select colour with ~s when s is 1 or 5, else s.
   So a SOLID line (s = 1) is a solid contrast line under 0x3333 dashes (~1 = 0xfe) of the colour - the user's
   Mastercam screenshot (X9: select-yellow dashes over black). X9 Mastercam.exe gets the same picture another way:
   draw_ent_nolist / 0x1400fd650 callers replace the style of a highlighted entity with the constant 2 at
   0x1407643c4 (.data init 2, never written; 0x14027acbd), pass 1 = contrast colour solid, pass 2 = colour 0x3333.
   The width bump to >= WfHighlightWidth is the caller's (covered by the overlay width). Drawn over everything (the
   original's EnableDepthDraw(false)) - our overlay always is. */
void ov_entity(float **pv, int *pn, int *pcap, int e, const float rgb[3]) {
    const Ent *en = &F.ents[e];
    int style = en->ln_style & 0xff;
    if (getenv("MCAMUI_OV_DEBUG")) fprintf(stderr, "ov_entity %d ln_style %d dashed %d\n", e, en->ln_style, cfg_dashed_wire);
    if (cfg_dashed_wire) {
        int cc = 15;                                  /* the contrast colour (white, or black on a light highlight colour) */
        float R = rgb[0] * 255, G = rgb[1] * 255, B = rgb[2] * 255;
        if (G * 0.59 + R * 0.3 + B * 0.11 > 0x7f) cc = 0;
        float crgb[3]; mc_rgb(cc, crgb);
        int s15 = ((style - 1) & 0xfffb) == 0;      /* style 1 or 5 (0x180135c6c / 0x180135c7c) */
        ov_entity_pat(pv, pn, pcap, en, ln_stipple(s15 ? style : (~style & 0xff)), crgb);
        ov_entity_pat(pv, pn, pcap, en, ln_stipple(s15 ? (~style & 0xff) : style), rgb);
    } else ov_entity_pat(pv, pn, pcap, en, ln_stipple(style), rgb);
}

/* draw_fast_marker (mastercam.dll 0x180331460): an axis-aligned square around the snapped point c, half-size
   sel_dist * 0.15 (view units), colour 15 (8 when the background is white), line width 1; the original draws it XOR -
   ours is plain. The overlay draws at 2.5 px. (moved out of mcamui.c build_overlay) */
void ov_fast_marker(float **pv, int *pn, int *pcap, V3 c) {
#define OV(P, R, G, B) ov_pt(pv, pn, pcap, P, R, G, B)
    double wpp = scene_view_height() / (vp_h > 0 ? vp_h : 1);     /* world units per pixel (approx.) */
    double d = 0.15 * SEL_APERTURE_PX * wpp;
    V3 r = { vmat[0] * d, vmat[1] * d, vmat[2] * d }, u = { vmat[3] * d, vmat[4] * d, vmat[5] * d };   /* view right / up */
    float mc[3]; mc_rgb(cfg_back_color == 15 ? 8 : 15, mc);
    V3 p1 = { c.x - r.x + u.x, c.y - r.y + u.y, c.z - r.z + u.z }, p2 = { c.x + r.x + u.x, c.y + r.y + u.y, c.z + r.z + u.z };
    V3 p3 = { c.x + r.x - u.x, c.y + r.y - u.y, c.z + r.z - u.z }, p4 = { c.x - r.x - u.x, c.y - r.y - u.y, c.z - r.z - u.z };
    OV(p1, mc[0], mc[1], mc[2]); OV(p2, mc[0], mc[1], mc[2]); OV(p2, mc[0], mc[1], mc[2]); OV(p3, mc[0], mc[1], mc[2]);
    OV(p3, mc[0], mc[1], mc[2]); OV(p4, mc[0], mc[1], mc[2]); OV(p4, mc[0], mc[1], mc[2]); OV(p1, mc[0], mc[1], mc[2]);
#undef OV
}
/* the shaded face highlight's entity state (scene.c scene_srf_look): bit 0 = the auto-highlighted entity, bit 1 = drawn selected
   (the selection / a picked entity, as build_overlay below) */
static int cui_ent_hl(int e) {
    int b = 0;
    if (e == hover_ent) b |= 1;
    if ((ent_sel && ent_sel_n == F.nents && ent_sel[e]) || (cui_hooks.ov_picked && cui_hooks.ov_picked(e))) b |= 2;
    return b;
}
void build_overlay(void) {
    scene_ent_hl = cui_ent_hl; scene_hl_colors[0] = cfg_autohl_color; scene_hl_colors[1] = cfg_select_color; scene_hl_stipple = cfg_stipple_hl;
    /* the dashed highlight is baked with screen-space pattern lengths (GL_LINE_STIPPLE semantics): any view
       change (zoom / pan / rotate / resize) must rebuild it */
    static double vm0[9], vh0 = -1, cx0, cy0; static int vw0 = -1, vhh0 = -1;
    if (vh0 != vht || vw0 != view_w || vhh0 != view_h || cx0 != vcx || cy0 != vcy || memcmp(vm0, vmat, sizeof vm0)) {
        memcpy(vm0, vmat, sizeof vm0); vh0 = vht; vw0 = view_w; vhh0 = view_h; cx0 = vcx; cy0 = vcy; overlay_dirty = 1;
    }
    if (!overlay_dirty) return;
    overlay_dirty = 0;
    int cap = 4096, n = 0; float *v = malloc(cap * 24);
    for (int e = 0; e < F.nents; e++) {
        int chained = cui_hooks.ov_chained && cui_hooks.ov_chained(e);   /* in a chain of the chaining (draw_chain_sel) */
        int selected = ent_sel && ent_sel_n == F.nents && ent_sel[e];
        if (cui_hooks.ov_picked && cui_hooks.ov_picked(e)) selected = 1;   /* select_ent: a picked entity is drawn selected until the command uses it */
        if (!chained && !selected) continue;
        /* draw_chain_sel / selection: the entity drawn with its selected bit -> config 5033 Select color, with
           config 4056 on as the two-pass contrast + dash (FUN_180135ad0, solid lines included) - ov_entity */
        float rgb[3] = { 1, 1, 0 }; mc_rgb(cfg_select_color, rgb); (void)chained;
        ov_entity(&v, &n, &cap, e, rgb);
    }
    if (hover_ent >= 0 && hover_ent < F.nents && F.ents[hover_ent].type) {
        /* auto-highlight (FUN_18031f620 / draw_ent_nolist): the entity in the config-5000 colour; the line width
           bump to >= WfHighlightWidth (default 2.0, config 8033) is covered by the overlay's 2.5 px. With config
           4056 on it is the two-pass contrast + dash of FUN_180135ad0 (hover call 0x18031f4fd) via ov_entity. An already-selected / chained entity
           keeps the select colour (ours). */
        int dup = ent_sel && ent_sel_n == F.nents && ent_sel[hover_ent];
        if (!dup && cui_hooks.ov_chained && cui_hooks.ov_chained(hover_ent)) dup = 1;
        if (!dup) {
            float rgb[3] = { 1, 1, 1 }; mc_rgb(cfg_autohl_color, rgb);
            ov_entity(&v, &n, &cap, hover_ent, rgb);
        }
    }
    if (sld_hover_body >= 0 && ex_mode && ex_pick == 2 && sld_hover_body < nsbodies) {
        /* the highlighted body (sld_repaint_hilite_ents -> FUN_180098b20: hit type != 4 -> FUN_1800bc970 (a face fill in
           the auto-highlight colour - a body is no face: nothing) + FUN_180042a00: the topol drawn as wire (FUN_18004e540
           render mode 1 -> PK_TOPOL_render_line) in the select colour, config 5033; no set_gl_style / stripes in that
           path). Its edges = what this program draws for the body (body_edges). The width is not set in that path (the
           current GL width, NOT DECODED): the overlay's. The shaded face highlight of the GAF renderer
           (FUN_180259770 -> SetGrRenderAutoHighlighted: gafgr's stipple list 0, decoded for surfaces - scene.c
           scene_srf_look) is not ported for solids. */
        float rgb[3] = { 1, 1, 0 }; mc_rgb(cfg_select_color, rgb);
        float *sg; int ns = body_edges(&sbodies[sld_hover_body], &sg);
        for (int i = 0; i < ns; i++) {
            V3 a = { sg[6 * i], sg[6 * i + 1], sg[6 * i + 2] }, b = { sg[6 * i + 3], sg[6 * i + 4], sg[6 * i + 5] };
            ov_pt(&v, &n, &cap, a, rgb[0], rgb[1], rgb[2]); ov_pt(&v, &n, &cap, b, rgb[0], rgb[1], rgb[2]);
        }
        free(sg);
    }
    sp_overlay(&v, &n, &cap);                           /* the Manual / Automatic spline's nodes (cui_spline.c) */
    sld_overlay(&v, &n, &cap);                          /* the solid selection's highlights (cui_sldchain.c) */
    ds_overlay(&v, &n, &cap);                           /* dyno_surf's arrow on the surface (cui_dsurf.c) */
    psel_overlay(&v, &n, &cap);                         /* the Plane Selection's plane (draw_plane, cui_plsel.c) */
    srf_ov(&v, &n, &cap);                               /* a Surfaces command's normal arrow (cui_surf.c) */
    tb_overlay(&v, &n, &cap);                           /* Contour Tabs Position... (cui_opdlg.c draw_tabs) */
    bb_ov(&v, &n, &cap);                                /* the Bounding Box panel's preview (cui_bbox.c; ours: an outline, not the live solid) */
    pv_overlay(&v, &n, &cap);                           /* Q20: the op dialog's Preview toolpath (cui_preview.c) */
    if (cui_hooks.ov_extra) cui_hooks.ov_extra(&v, &n, &cap);   /* the front end's lines (mcamui: drill points, drawing preview, Extrude arrows) */
    {   /* base drawing of non-solid line styles: draw_ent_nolist calls set_gl_style(ent +0x18) for every entity
           (decomp/mastercam_hldraw.c:255-256), so dashed entities draw dashed in the plain view too. Core GL has no
           GL_LINE_STIPPLE: the dash segments are baked here in screen px (ov_entity_pat's rule, rebuilt on every
           view change like the rest of this overlay); scene.c skips these entities' polylines in the static draw. */
        int dcap = 256, dn = 0; float *dv = malloc(dcap * 24);
        for (int e = 0; e < F.nents; e++) {
            const Ent *en = &F.ents[e];
            if (!en->type || !ENT_LV_SHOWN(en)) continue;     /* draw_ent: a visible level */
            int style = en->ln_style & 0xff;
            unsigned short pat = ln_stipple(style);
            if (style == 1 || pat == 0xffff) continue;      /* solid (invalid styles coerce to 1: FUN_180329df0) */
            float rgb[3]; mc_rgb(en->first_poly >= 0 && en->first_poly < F.npolys ? F.polys[en->first_poly].color : en->color, rgb);
            ov_entity_pat(&dv, &dn, &dcap, en, pat, rgb);
        }
        {   /* the machine group's stock (mastercam.dll draw_stock 0x1803360a0, called from the repaint FUN_1801dbc80 with
               mill_stock_color = config 5029): each of the 12 lines by draw_line_style(line, 4, color) = set_gl_style(4)
               (pattern 0x333f) at width 1, one pattern run per line (cui_stock.c stock_display_lines); a cylinder adds its
               two end circles (FUN_18021e1b0(arc, color, 4, 0)), one pattern run per circle, drawn as the arcs are
               (mcam_load.c add_arc: 3 degree chords) */
            int dn0 = dn; ov_oom = 0;                           /* the dashed geometry before the stock's */
            if (scene_gl_skipped) {                             /* the last frame could not hand everything to GL (scene.c, ours) */
                scene_gl_skipped = 0;
                if (F.stock_shape > 1) { const float *m0; const int *t0; stock_model(&m0, &t0);
                    if (st_warned != m0) { st_warned = m0; cui_message("Stock Setup", "Not enough memory to draw the stock model."); } }
            }
            double L[12][6]; int nl = stock_display_lines(L);
            float rgb[3]; mc_rgb(cfg_stock_color, rgb);
            unsigned short pat = ln_stipple(4); double px = stipple_px();
            /* one stipple run (the pattern restarts per glBegin) along the points q[0..n) */
            #define ST_RUN(Q, N) do { double pos = 0; for (int k = 1; k < (N); k++) { V3 a = (Q)[k - 1], b = (Q)[k]; float ax, ay, bx, by; \
                if (!scene_to_screen(view_w, view_h, a.x, a.y, a.z, &ax, &ay) || !scene_to_screen(view_w, view_h, b.x, b.y, b.z, &bx, &by)) continue; \
                double Lpx = hypot(bx - ax, by - ay), off = 0; if (!(Lpx >= 1e-9)) continue; \
                while (off < Lpx) { double nxt = (floor(pos / px) + 1) * px - pos; if (off + nxt > Lpx) nxt = Lpx - off; \
                    if ((pat >> ((int)(pos / px) & 15)) & 1) { double t0 = off / Lpx, t1 = (off + nxt) / Lpx; \
                        V3 p0 = { a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0, a.z + (b.z - a.z) * t0 }; \
                        V3 p1 = { a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1, a.z + (b.z - a.z) * t1 }; \
                        ov_pt(&dv, &dn, &dcap, p0, rgb[0], rgb[1], rgb[2]); ov_pt(&dv, &dn, &dcap, p1, rgb[0], rgb[1], rgb[2]); } \
                    off += nxt; pos += nxt; } } } while (0)
            for (int l = 0; l < nl; l++) { V3 q[2] = { { L[l][0], L[l][1], L[l][2] }, { L[l][3], L[l][4], L[l][5] } }; ST_RUN(q, 2); }
            double C[2][3], cr, M[9];
            if (stock_display_circles(C, &cr, M))
                for (int a = 0; a < 2; a++) {                   /* add_arc: sweep / 3 deg + 2 chords */
                    enum { NC = (int)(360 / 3) + 2 }; V3 q[NC + 1];
                    for (int i = 0; i <= NC; i++) {
                        double t = 2 * M_PI * i / NC, v[3] = { C[a][0] + cr * cos(t), C[a][1] + cr * sin(t), C[a][2] };
                        q[i] = (V3){ M[0] * v[0] + M[3] * v[1] + M[6] * v[2], M[1] * v[0] + M[4] * v[1] + M[7] * v[2], M[2] * v[0] + M[5] * v[1] + M[8] * v[2] };
                    }
                    ST_RUN(q, NC + 1);
                }
            /* shapes 2 / 3 (the solid / STL model, CreateShadedStockModel): draw_stock's OpenGL branch draws each triangle as
               one GL_LINE_STRIP p0 p1 p2 p0 in style 4 (one stipple run per triangle), read from the model in place (ours: no
               copy); with the Solid display (+0x1261) and a model, FUN_18033fe70 draws the model shaded instead of any lines */
            const float *mv = NULL; const int *mt = NULL; int shaded = stock_display_shaded();
            if (!shaded && F.stock_shape > 1 && stock_display_model() > 0) {
                int mnt = stock_model(&mv, &mt);
                for (int k = 0; k < mnt; k++) {
                    V3 q[4]; for (int j = 0; j < 4; j++) { const float *c = mv + 3 * mt[3 * k + (j % 3)]; q[j] = (V3){ c[0], c[1], c[2] }; }
                    ST_RUN(q, 4);
                }
            }
            #undef ST_RUN
            if (shaded) {                                       /* FUN_18033fe70: flat normals (FUN_180136e60), alpha 0.375 */
                int nt = stock_model(&mv, &mt);
                float *tv = nt > 0 && (size_t)nt <= SIZE_MAX / (18 * sizeof(float)) ? malloc(sizeof(float) * 18 * (size_t)nt) : NULL;
                if (tv && !gl_room((size_t)nt * 72)) { free(tv); tv = NULL; }
                if (!tv) {                                      /* no memory: nothing shaded, said once per model (ours) */
                    if (nt > 0 && st_warned != mv) { st_warned = mv; cui_message("Stock Setup", "Not enough memory to draw the shaded stock model (%d triangles).", nt); }
                    nt = 0;
                }
                for (int k = 0; k < nt; k++) {
                    const float *a = mv + 3 * mt[3 * k], *b = mv + 3 * mt[3 * k + 1], *c = mv + 3 * mt[3 * k + 2];
                    float f2 = a[2] - b[2], f3 = b[2] - c[2], f4 = a[1] - b[1], f7 = b[1] - c[1], f5 = a[0] - b[0], f6 = b[0] - c[0];
                    float nx = f3 * f4 - f7 * f2, ny = f6 * f2 - f3 * f5, nz = f7 * f5 - f6 * f4, l = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (l <= 1e-8f) nx = ny = nz = 0; else { nx /= l; ny /= l; nz /= l; }
                    const float *P[3] = { a, b, c };
                    for (int j = 0; j < 3; j++) { float *o = tv + 18 * (size_t)k + 6 * j; o[0] = P[j][0]; o[1] = P[j][1]; o[2] = P[j][2]; o[3] = nx; o[4] = ny; o[5] = nz; }
                }
                float rgba[4] = { rgb[0], rgb[1], rgb[2], 0.375f };
                scene_take_stock_mesh(tv, 3 * nt, rgba);       /* the buffer becomes the scene's (no copy) */
            } else scene_take_stock_mesh(NULL, 0, NULL);
            if (dcap < 0) dcap = -dcap;                         /* the buffer is whole again for the next builders */
            if (dv && dn > dn0 && (ov_oom || !gl_room((size_t)dn * 24))) {   /* the stock's dashes do not fit (memory / the GL upload): left out (ours) */
                if (F.stock_shape > 1 && st_warned != mv) { st_warned = mv; cui_message("Stock Setup", "Not enough memory to draw the stock model's outlines."); }
                dn = dn0;
            }
        }
        scene_take_dash(dv, dv ? dn : 0);                /* the buffer becomes the scene's (no copy) */
    }
    gn_upload();                                        /* Transform > Dynamic's gnomon (cui_xdyn.c) */
    scene_set_overlay(v, n);
    free(v);
}
