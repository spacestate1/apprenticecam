/* core_ui: Wireframe commands of decision N9-16 (formats/wireframe.md "Point Nodes, Combine Views, Relief Groove"): Point Nodes
   (create_spline_points, mccad.dll 180008500), Combine Views (OnScreenCombineViews, mastercam.dll 1804703a0 -> combine_views
   18032f0e0) and Relief Groove (MakeReliefGrooves, mccad.dll 180076e90, dialog CDINGrv 2516). Every behaviour below is read from
   the decompile (function, DLL) cited at the code. */
#include "core_ui.h"
#include "../scene.h"
#include <ctype.h>
#include "../cam2d.h"
#include <math.h>
#include "mcsurf.h"
#include "../mcsurf_nrm.h"
#include "../mcsurf_build.h"
#include "../occt_prim.h"
#include "../occt_cad.h"
#include "../paths.h"
#include "../platform.h"
#include "mcam_saveops.h"
#include <dirent.h>
#include <strings.h>


/* ---------------------------------------------------------------------------------------- Point Nodes */
/* create_spline_points (mccad.dll 180008500; decomp/mccad_sketch.c:415): select_ent(5836 "Select a spline", mask 0x88), one spline,
   one undo transaction, then the command ends. mccore createPointsNodeOnParaSpline 18026d560: S_ID: ent +0xb0 = the number of
   segments, ent +0xb7 = closed: t = 0 .. n (n - 1 closed), pt_on_ent_from_t, proj_pt_cp, createPoint.
   createPointsCtrlOnNURBSpline 18026d3c0: NB_ID: +0xcc = number of control points, +0xd1 = doubles per point (3 / 4), +0xd2 bit 2
   = non-rational (x y z), otherwise w x y z with x/w; stride 3 needs the bit, stride 4 needs it clear (else no points) */
static int rd32le(const uint8_t *p) { int v; memcpy(&v, p, 4); return v; }
/* the construction depth in the Cplane's view (proj_pt_cp, mastercam.dll 18033c200: ConstrDepth + the z of GetActiveOriginInViewCoordinates(0), 0 with no active origin) */
static double wm_cp_depth(void) {
    int id = plane_active[0] > 0 ? plane_active[0] : 1; const double *R = cam_plane_matrix(id), *o = plane_active_origin(0);
    return constr_depth + (o ? o[0] * R[6] + o[1] * R[7] + o[2] * R[8] : 0);
}
/* proj_pt_cp: in 2D construction proj_pt_to_view (mccore 180018c10): world_to_view of the Cplane, z = the depth, view_to_world */
static V3 wm_proj_cp(V3 p) {
    if (cfg_constr_3d) return p;
    int id = plane_active[0] > 0 ? plane_active[0] : 1; const double *R = cam_plane_matrix(id);
    double x = p.x * R[0] + p.y * R[1] + p.z * R[2], y = p.x * R[3] + p.y * R[4] + p.z * R[5], z = wm_cp_depth();
    return (V3){ R[0] * x + R[3] * y + R[6] * z, R[1] * x + R[4] * y + R[7] * z, R[2] * x + R[5] * y + R[8] * z };
}
int wm_ptnodes(int e) {
    if (e < 0 || e >= F.nents || F.ents[e].type != E_SPLINE) return 0;
    const Ent *en = &F.ents[e]; if (en->crv < 0 || en->crv >= F.ncrvs) return 0;
    const CurveRec *c = &F.crvs[en->crv]; const uint8_t *r = c->rec;
    V3 z0 = { 0, 0, 0 }; int made = 0;
    if (en->cls == 8) {                                   /* createPointsNodeOnParaSpline */
        if (!mcs_spline_ok(c)) return 0;
        int n = rd32le(r + 0xb0); if (r[0xb7]) n--;       /* closed: the last node is the first */
        for (int i = 0; i <= n; i++) {
            double o[3][3]; if (mcs_spline_eval_d(c, (double)i, 0, o)) break;
            V3 p = wm_proj_cp((V3){ o[0][0], o[0][1], o[0][2] });   /* proj_pt_cp */
            geo_add(E_POINT, p, p, z0, 0, 0, 0); made++;
        }
    } else if (en->cls == 0x80) {                         /* createPointsCtrlOnNURBSpline */
        int nc = rd32le(r + 0xcc), vpp = r[0xd1], nonrat = r[0xd2] & 4;
        if ((vpp == 3 && !nonrat) || (vpp == 4 && nonrat) || (vpp != 3 && vpp != 4)) return 0;   /* the two returns of the test (no points) */
        const uint8_t *pts = NULL; size_t off;           /* the record's control array follows the knots: ent +0xc8 knots, +0xcc control points */
        int nk = rd32le(r + 0xc8); off = 0xd8 + (size_t)nk * 8;   /* mcspline.c: the knots at +0xd8, then the control points */
        if (off + (size_t)nc * vpp * 8 > c->n) return 0;
        pts = r + off;
        for (int i = 0; i < nc; i++) {
            double v[4]; memcpy(v, pts + (size_t)i * vpp * 8, sizeof(double) * vpp);
            V3 p; if (nonrat) { p.x = v[0]; p.y = v[1]; p.z = v[2]; } else { p.x = v[1] / v[0]; p.y = v[2] / v[0]; p.z = v[3] / v[0]; }
            p = wm_proj_cp(p);                            /* proj_pt_cp */
            geo_add(E_POINT, p, p, z0, 0, 0, 0); made++;
        }
    }
    return made;
}

/* ---------------------------------------------------------------------------------------- Combine Views */
/* combine_views (mastercam.dll 18032f0e0): the arcs of every view are moved to the first view in the table parallel to theirs
   (views_parallel 1803418c0). The map (one entry per view): views 1, 2, 5 and left-handed views never move (-1); a view not yet
   mapped looks from view 1 up while k != its own number, or k == n_views (the loop condition: a view that is the last in the table
   finds itself); the entity is rewritten in view k: the centre to view k through world, the start angle from the world start point
   in view k (atan2, 0 .. 2 pi), the sweep negated when the normals are opposed, pos_sa_sw. One undo transaction; the count is
   reported by a message box (title 1988, body "%d%s" of the count and 1987). The status bar shows 1986 during the walk. */
static int wm_left_handed(int v) {                    /* LeftHandView (mastercam.dll 180322230) */
    double R[9]; if (!mc_view_rows(v, R)) return 1;
    double x = R[1] * R[5] - R[2] * R[4], y = R[2] * R[3] - R[0] * R[5], z = R[0] * R[4] - R[1] * R[3];
    return x * R[6] + y * R[7] + z * R[8] <= 0;
}
void wm_combine_views(void) {
    int nv = mc_view_count(); short *map = calloc((size_t)nv + 9, sizeof(short));
    if (!map) return;
    map[1] = map[2] = map[5] = -1;
    int moved = 0, from_nu = nundo; (void)from_nu;
    snprintf(cam_msg, sizeof cam_msg, "Combining planes . . .");   /* 1986 */
    mod_begin();
    int nfrom = F.nents;
    for (int i = 0; i < nfrom; i++) {
        Ent *e = &F.ents[i];
        if (e->type != E_ARC || !e->vw || !ENT_LV_SHOWN(e) || e->blanked) continue;   /* get_ent(mask 1, A_ID) */
        int v = e->vw; if (v >= nv + 9) continue; int k = map[v];
        if (k == 0) {
            if (!wm_left_handed(v)) {
                for (int q = 1; q == nv || (q < nv && q != v); q++) {   /* :180: sVar21 == n_views, or sVar21 < n_views and != v */
                    if (!wm_left_handed(q)) { int anti; if (mc_views_parallel(q, v, &anti)) { map[v] = (short)q; k = q; break; } }
                    if (q == nv) break;
                }
            } else { map[v] = -1; k = -1; }
        }
        if (k <= 0) continue;
        double R[9], Rv[9]; mc_view_rows(k, R); mc_view_rows(v, Rv);
        double dot = R[6] * Rv[6] + R[7] * Rv[7] + R[8] * Rv[8];
        double dx = e->a.x - e->c.x, dy = e->a.y - e->c.y, dz = e->a.z - e->c.z;
        double vx = dx * R[0] + dy * R[1] + dz * R[2], vy = dx * R[3] + dy * R[4] + dz * R[5];
        double a0 = vx * vx + vy * vy >= 1e-12 ? atan2(vy, vx) : 0; if (a0 < 0) a0 += 2 * M_PI;
        double sw = e->sw; if (dot < 0) sw = -sw;
        if (sw < 0) { a0 += sw; sw = -sw; }               /* pos_sa_sw: arcs are stored counter-clockwise here (geo_add) */
        a0 = fmod(a0, 2 * M_PI); if (a0 < 0) a0 += 2 * M_PI;
        V3 cw = e->c; double r = e->r; int lv = e->level, co = e->color, ls = e->ln_style; double wd = e->width;
        int q = mcam_new_arc_view_w(&F, k, R, cw, r, a0, sw, lv, co);
        e = &F.ents[i];
        if (q >= 0) { F.ents[q].ln_style = ls >= 1 ? ls : 1; F.ents[q].width = wd >= 1 ? wd : 1; mod_del(i); mod_pair(i, q); moved++; }
    }
    mod_end();
    free(map);
    cam_msg[0] = 0; overlay_dirty = 1; scene_reupload();
    cui_message("Combine planes", "%d arcs moved to a new view", moved);   /* 1988 / "%d%s" of the count and 1987 */
}

/* ---------------------------------------------------------------------------------------- Relief Groove */
/* MakeReliefGrooves (mccad.dll 180076e90): the product check (GetProdEnableBits & 0x120 -> lathe_product_level_ok(1)) is not ported
   (no product levels here), the dialog CDINGrv (MCCADRES 2516, class dump decomp fam.c 1893ff) and its OK: FUN_1800765e0.
   The state is the dialog's: the per-shape dimensions (dialog +0x72e0: 4 rows of 5 doubles, row[0..4] = P+0x18 .. +0x38 in
   FUN_180077390), Z position, the shape, the side +0x72c8, Shoulder on right, Create geometry in, the trim mode, Select Z only,
   Preview at selected position. */
typedef struct { double dim[4][5], z; int shape, side, shoulder, in, trim, zonly, prevsel; } RgState;
static RgState rg;
int wm_rg_picking;
/* the parameter file (prm_prefix: the configuration folder; MCAMUI_PRB_DIR: tests, wf_compare.sh) */
static char *rg_prb(char *p, size_t n) { const char *nm = mcam_is_metric(&F) ? "rgmetric.prb" : "rginch.prb", *d = getenv("MCAMUI_PRB_DIR");
    if (d) { snprintf(p, n, "%s/%s", d, nm); return p; } return config_file(p, n, nm); }

/* the defaults (FUN_180077f80, dialog init) then rgmetric.prb / rginch.prb (240 bytes: the 4 x 5 doubles, the parameter struct P
   (shape, Z, in, side, shoulder, trim, ...), Select Z only, Preview) from the configuration folder (prm_prefix) */
static void rg_defaults(void) {
    memset(&rg, 0, sizeof rg);
    if (!mcam_is_metric(&F)) {
        static const double D[20] = { 0, 0.5, 0.386, 0.308, 0.05, 0, 0.39266, 0.52, 0.308, 0.05, 0.5, 0.008, 0.079, 0.055, 0.024, 0.5, 0.008, 0.079, 0.055, 0.024 };
        memcpy(rg.dim, D, sizeof D);
    } else {
        static const double D[20] = { 0, 16, 13, 7, 1, 0, 13.186, 16.5, 10.3, 1, 16, 0.2, 2, 1.4, 0.6, 16, 0.2, 2, 1.4, 0.6 };
        memcpy(rg.dim, D, sizeof D);
    }
    rg.zonly = rg.prevsel = 1;
    char p[1100]; FILE *f = fopen(rg_prb(p, sizeof p), "rb"); if (!f) return;
    uint8_t b[0xf0];
    if (fread(b, 1, sizeof b, f) == sizeof b && fgetc(f) == EOF) {   /* a wrong size: post_warning 632 "Invalid parameter file size" */
        memcpy(rg.dim, b, 0xa0);
        const uint8_t *P = b + 0xa0; int s = P[0]; rg.shape = s > 3 ? 3 : s; memcpy(&rg.z, P + 8, 8);   /* FUN_180077880: clamps */
        int ge = P[0x10]; rg.in = ge > 2 ? 2 : ge; rg.side = P[0x11] != 0; rg.shoulder = P[0x12] != 0; int t = P[0x13]; rg.trim = t > 2 ? 2 : t;
        int a; memcpy(&a, b + 0xe8, 4); rg.zonly = a != 0; memcpy(&a, b + 0xec, 4); rg.prevsel = a != 0;
    } else cui_message("Relief Groove", "Invalid parameter file size");
    fclose(f);
}
/* FUN_180078880 at OK: the same 240 bytes written */
static void rg_save_prb(void) {
    char p[1100]; FILE *f = fopen(rg_prb(p, sizeof p), "wb"); if (!f) return;
    uint8_t b[0xf0]; memset(b, 0, sizeof b); memcpy(b, rg.dim, 0xa0);
    uint8_t *P = b + 0xa0; P[0] = (uint8_t)rg.shape; memcpy(P + 8, &rg.z, 8); P[0x10] = (uint8_t)rg.in; P[0x11] = (uint8_t)rg.side; P[0x12] = (uint8_t)rg.shoulder; P[0x13] = (uint8_t)rg.trim;
    int a = rg.zonly; memcpy(b + 0xe8, &a, 4); a = rg.prevsel; memcpy(b + 0xec, &a, 4);
    fwrite(b, 1, sizeof b, f); fclose(f);
}
void wm_rg_begin(void) {
    rg_defaults();                                       /* FUN_1800765e0: FUN_180077f80 (the defaults, then the .prb) at every command */
    wm_rg_picking = 0;
}

/* the table (FUN_180078c60 / FUN_180079380 / FUN_1800797c0): rgmetric.tbl / rginch.tbl in the shared folder (GetApp +0x268 (3)): a
   line holding a double quote is a name (the quotes trimmed from both ends), then "shape d P dg g r" (Shape A/C) or "shape d r1 t1 f1 g" (E/F); a line whose first
   character is '#' is a comment; only rows of the current shape are listed (first number == the shape, exactly 6 numbers). Row dims are in P order: Shape A: d = [1], P = [0], dg = [2], g = [3], r = [4]; Shape C: d - P * 1.407;
   Shapes E/F: [0] d, [4] r1, [1] t1, [2] f1, [3] g */
#define RG_MAXROW 512
static struct { char name[64]; double dim[5]; } rg_tbl[RG_MAXROW]; static int rg_ntbl, rg_tbl_shape = -1, rg_tbl_units, rg_tbl_sel;
/* FUN_1800797c0: the numbers of a row. The delimiters DAT_1804149f8 (FindOneOf), each piece TrimLeft; FUN_180078b90 / FUN_1800792b0: a piece counts when it holds only "0123456789.-" and no '-' past the first character;
   a line with no delimiter at all gives none (the while's first test). */
static int rg_row(char *s, double *v, int max) {
    static const char D[] = " ";                         /* FindOneOf(DAT_1804149f8): the string " " (mccad.dll .rdata bytes 20 00) */
    int n = 0; char *p = s; while (isspace((unsigned char)*p)) p++;
    size_t k = strcspn(p, D); if (!p[k]) return 0;
    for (;;) {
        char c = p[k], *e = p + k; *e = 0;
        char *a = p; while (isspace((unsigned char)*a)) a++;
        size_t l = strlen(a); while (l && isspace((unsigned char)a[l - 1])) a[--l] = 0;
        int ok = l > 0; for (size_t i = 0; i < l && ok; i++) { if (!strchr("0123456789.-", a[i])) ok = 0; else if (a[i] == '-' && i > 0) ok = 0; }
        if (ok) v[n++] = atof(a);
        if (!c || n >= max) return n;
        p = e + 1; while (isspace((unsigned char)*p)) p++;
        k = strcspn(p, D);
    }
}
static char rg_tbl_test[512];                            /* tests (ours): rgtblf:PATH, a table file of the test data */
static FILE *rg_open_tbl(void) {
    if (rg_tbl_test[0]) return fopen(rg_tbl_test, "r");
    const char *sh = site_path("shared"), *nm = mcam_is_metric(&F) ? "rgmetric.tbl" : "rginch.tbl";
    if (!sh[0]) return NULL;
    DIR *d = opendir(sh); if (!d) return NULL;   /* NTFS name: case-insensitive */
    struct dirent *de; char p[1400]; FILE *f = NULL;
    while ((de = readdir(d))) if (!strcasecmp(de->d_name, nm)) { snprintf(p, sizeof p, "%s/%s", sh, de->d_name); f = fopen(p, "r"); break; }
    closedir(d); return f;
}
static void rg_load_table(void) {
    int m = mcam_is_metric(&F);
    if (rg_tbl_shape == rg.shape && rg_tbl_units == m) return;
    rg_tbl_shape = rg.shape; rg_tbl_units = m; rg_ntbl = 0; rg_tbl_sel = 0;
    FILE *f = rg_open_tbl(); if (!f) return;
    char line[512], name[64] = ""; int have = 0;
    while (fgets(line, sizeof line, f) && rg_ntbl < RG_MAXROW) {
        size_t ll = strlen(line); while (ll && (line[ll - 1] == '\n' || line[ll - 1] == '\r')) line[--ll] = 0;   /* ReadString */
        char *s = line; if (!*s || *s == '#') continue;      /* IsEmpty, s[0] != '#' (no trimming first) */
        if (!have && strchr(s, '"')) {                       /* Find('"') >= 0: TrimLeft('"'), TrimRight('"'); empty after either: stays a name to come */
            while (*s == '"') s++; size_t q = strlen(s); while (q && s[q - 1] == '"') s[--q] = 0;
            if (*s) { snprintf(name, sizeof name, "%s", s); have = 1; }
            continue;
        }
        if (!have) continue;
        have = 0; double v[6]; int n = rg_row(s, v, 6);
        if (n != 6 || v[0] != (double)rg.shape) continue;   /* sVar5 == 6, local_1b8 == param_1 */
        double *dst = rg_tbl[rg_ntbl].dim; memset(dst, 0, 5 * sizeof(double));
        if (rg.shape == 0) { dst[1] = v[1]; dst[0] = v[2]; dst[2] = v[3]; dst[3] = v[4]; dst[4] = v[5]; }
        else if (rg.shape == 1) { dst[1] = v[1] - v[2] * 1.407; dst[0] = v[2]; dst[2] = v[3]; dst[3] = v[4]; dst[4] = v[5]; }
        else { dst[0] = v[1]; dst[4] = v[2]; dst[1] = v[3]; dst[2] = v[4]; dst[3] = v[5]; }
        snprintf(rg_tbl[rg_ntbl].name, sizeof rg_tbl[rg_ntbl].name, "%s", name);
        rg_ntbl++;
    }
    fclose(f);
}

/* a groove piece (the a_2d / l_2d records of the ctour, FUN_180074810): 0 line (x0 y0 x1 y1), 1 arc (cx cy r sa sw) */
typedef struct { int arc; double v[5]; } RgEl;
/* fillet_line_line_2d(l1, l2, arc, r, 0, 1, &ok) (mcmachinedef.dll 1800eb0a0 / fl.c): l_ints; reverse_lines (l2 reversed, its end the corner);
   each line offset by r towards the other's first point (l_offset mccore 1804b3980: the foot of that point, the unit vector to it); their
   intersection = the centre; proj_pt_ln of the centre: t >= 0 on each, the foot is the line's new end; the arc from l1's foot to l2's foot
   (constr_ns_arc: clockwise when (l2 - c) x ... <= 0); reversed back. A failure restores the lines, ok = 0. */
typedef struct { double x0, y0, x1, y1; } L2;
static int l_int(L2 a, L2 b, double *x, double *y) {   /* l_ints: infinite lines */
    double d1x = a.x1 - a.x0, d1y = a.y1 - a.y0, d2x = b.x1 - b.x0, d2y = b.y1 - b.y0, c = d1x * d2y - d1y * d2x;
    if (fabs(c) < 1e-12) return 0;
    double t = ((b.x0 - a.x0) * d2y - (b.y0 - a.y0) * d2x) / c; *x = a.x0 + t * d1x; *y = a.y0 + t * d1y; return 1;
}
static int l_proj(double px, double py, L2 l, double *fx, double *fy, double *t) {   /* proj_pt_ln */
    double dx = l.x1 - l.x0, dy = l.y1 - l.y0, d = dx * dx + dy * dy; if (d < 1e-24) return 0;
    *t = ((px - l.x0) * dx + (py - l.y0) * dy) / d; *fx = l.x0 + *t * dx; *fy = l.y0 + *t * dy; return 1;
}
static int l_off(L2 l, double r, double px, double py, L2 *o) {   /* l_offset */
    double fx, fy, t; if (!l_proj(px, py, l, &fx, &fy, &t)) return 0;
    double dx = px - fx, dy = py - fy, h = sqrt(dx * dx + dy * dy); if (h < mc_systol.tol) return 0;
    dx /= h; dy /= h; o->x0 = l.x0 + dx * r; o->y0 = l.y0 + dy * r; o->x1 = l.x1 + dx * r; o->y1 = l.y1 + dy * r; return 1;
}
static int rg_fillet(RgEl *L1, RgEl *L2e, double r, RgEl *arc) {
    L2 a = { L1->v[0], L1->v[1], L1->v[2], L1->v[3] }, b = { L2e->v[2], L2e->v[3], L2e->v[0], L2e->v[1] };   /* l2 reversed */
    double x, y, cx, cy, fx, fy, t, gx, gy; L2 oa, ob;
    if (r < mc_systol.tol || !l_int(a, b, &x, &y)) return 0;
    if (!l_off(a, r, b.x0, b.y0, &oa) || !l_off(b, r, a.x0, a.y0, &ob) || !l_int(oa, ob, &cx, &cy)) return 0;
    if (!l_proj(cx, cy, a, &fx, &fy, &t) || t < 0) return 0;
    if (!l_proj(cx, cy, b, &gx, &gy, &t) || t < 0) return 0;
    double ux = fx - cx, uy = fy - cy, vx = gx - cx, vy = gy - cy;
    int cw = vy * ux - uy * vx <= 0;                     /* local_res10 */
    double a0 = atan2(uy, ux), a1 = atan2(vy, vx), sw = a1 - a0;
    if (cw) { while (sw > 0) sw -= 2 * M_PI; while (sw <= -2 * M_PI) sw += 2 * M_PI; } else { while (sw < 0) sw += 2 * M_PI; while (sw >= 2 * M_PI) sw -= 2 * M_PI; }
    L1->v[2] = fx; L1->v[3] = fy; L2e->v[0] = gx; L2e->v[1] = gy;
    arc->arc = 1; arc->v[0] = cx; arc->v[1] = cy; arc->v[2] = sqrt(ux * ux + uy * uy); arc->v[3] = a0 < 0 ? a0 + 2 * M_PI : a0; arc->v[4] = sw;
    return 1;
}
/* FUN_180074c80 / FUN_1800751c0 / FUN_180075700 / FUN_180075c60: one side (mirror_y: "Create geometry in -X" run). 0 = invalid. */
static int rg_build(int mirror_y, RgEl *out, int *nout, double ends[5][2]) {
    double tol = mc_systol.tol, h, Zadj, Dia;
    const double *D = rg.dim[rg.shape]; RgEl L[3]; memset(L, 0, sizeof L); double r;
    #define NEARZ(v) (fabs(v) < tol)
    if (rg.shape <= 1) {
        double d = D[1], dg = D[2], g = D[3]; r = D[4];
        if (NEARZ(d) || NEARZ(dg) || NEARZ(g) || d < 0 || dg < 0 || g < 0) return 0;
        double dd = dg - d;
        if (rg.shape == 0) { if (NEARZ(dd) || d < dg || d - dg < r || g < r) return 0; }
        else { if (NEARZ(dd) || dg < d || dd < r || g < r) return 0; }
        h = dd * 0.5; double slope = rg.shape == 0 ? 0.5773502691896 : -0.5773502691896;
        L[0].v[0] = 0; L[0].v[1] = 0; L[0].v[2] = h / slope; L[0].v[3] = h;
        L[1].v[0] = L[0].v[2]; L[1].v[1] = L[0].v[3]; L[1].v[2] = -g; L[1].v[3] = h;
        L[2].v[0] = -g; L[2].v[1] = h; L[2].v[2] = -g; L[2].v[3] = 0;
        Zadj = g; Dia = d;
    } else {
        double d = D[0], t1 = D[1], f1 = D[2], g = D[3]; r = D[4];
        if (NEARZ(d) || NEARZ(f1) || NEARZ(g) || NEARZ(t1) || d < 0 || f1 < 0 || t1 < 0 || r < 0 || g < 0 || g < r) return 0;
        L[0].v[0] = 0; L[0].v[1] = 0; L[0].v[2] = -t1 / 0.2679491924311; L[0].v[3] = -t1;
        L[1].v[0] = L[0].v[2]; L[1].v[1] = L[0].v[3];
        if (rg.shape == 2) { L[1].v[2] = -f1; } else L[1].v[2] = -f1 - g * 0.1405408347024;   /* Shape F: FUN_180075c60 */
        L[1].v[3] = -t1;
        L[2].v[0] = L[1].v[2]; L[2].v[1] = L[1].v[3]; L[2].v[2] = -f1; L[2].v[3] = g + L[1].v[3];
        Zadj = f1; Dia = d;
    }
    #undef NEARZ
    RgEl A0 = { 0 }, A1 = { 0 }; int have_a = 0;
    if (r > 0) { if (!rg_fillet(&L[0], &L[1], r, &A0) || !rg_fillet(&L[1], &L[2], r, &A1)) return 0; have_a = 1; }
    for (int i = 0; i < 3; i++) if (hypot(L[i].v[2] - L[i].v[0], L[i].v[3] - L[i].v[1]) < tol) return 0;   /* p2Equal of each line's ends, after the fillets (FUN_180074c80: :132-147 then :238) */
    int n = 0; out[n++] = L[0]; if (have_a) out[n++] = A0; out[n++] = L[1]; if (have_a) out[n++] = A1; out[n++] = L[2]; *nout = n;
    double zx = rg.z;                                     /* :660 the Z position, the shoulder */
    if (!rg.shoulder) { if (!rg.side) zx += Zadj; }
    else if (rg.side == 1) zx -= Zadj;
    double yy = Dia * 0.5;
    for (int i = 0; i < n; i++) {                         /* FUN_1800762e0: x mirror (shoulder), translate, y mirror (the -X side) */
        RgEl *e = &out[i];
        if (rg.shoulder) { if (e->arc) { e->v[0] = -e->v[0]; e->v[4] = -e->v[4]; e->v[3] = fmod(M_PI - e->v[3] + 4 * M_PI, 2 * M_PI); } else { e->v[0] = -e->v[0]; e->v[2] = -e->v[2]; } }
        if (e->arc) { e->v[0] += zx; e->v[1] += yy; } else { e->v[0] += zx; e->v[1] += yy; e->v[2] += zx; e->v[3] += yy; }
        if (mirror_y) { if (e->arc) { e->v[1] = -e->v[1]; e->v[4] = -e->v[4]; e->v[3] = fmod(-e->v[3] + 4 * M_PI, 2 * M_PI); } else { e->v[1] = -e->v[1]; e->v[3] = -e->v[3]; } }
    }
    /* FUN_1800768f0: the first / last ctour point (the arc's end is the following record's start here) ordered by x */
    double fx = out[0].v[0], fy = out[0].v[1], lx, ly;
    if (out[n - 1].arc) return 0;
    lx = out[n - 1].v[2]; ly = out[n - 1].v[3];
    if (lx <= fx) { ends[1][0] = fx; ends[1][1] = fy; ends[0][0] = lx; ends[0][1] = ly; }
    else { ends[0][0] = fx; ends[0][1] = fy; ends[1][0] = lx; ends[1][1] = ly; }
    ends[2][0] = out[n - 1].v[0]; ends[2][1] = out[n - 1].v[1];   /* ctour[n - 1]: the last element's start */
    ends[3][0] = fx; ends[3][1] = fy; ends[4][0] = lx; ends[4][1] = ly;   /* FUN_180074510: the three points walked are the ctour's first, last and (with the column test) last again */
    return 1;
}
/* FUN_180076970: Q = the groove point (param_2 ? the low-x point : the high-x point); a, b = the two copies of the line (param_3 / param_4) */
static void rg_cut(int low, const double ends[5][2], V3 *a0, V3 *a1, V3 *b0, V3 *b1) {
    double qx = ends[low ? 0 : 1][0], qy = ends[low ? 0 : 1][1], y7 = ends[2][1], tol = mc_systol.tol, ax = a0->x, ay = a0->y, by = b0->y;
    V3 Q = { qx, qy, 0 }, Q7 = { ends[2][0], ends[2][1], 0 };   /* V2ToV3(.., 0, ..): z = 0 */
    int c = rg.shape, tb, te;
#define B3D do { *a0 = Q; *b1 = Q; return; } while (0)
#define AD4 do { *a1 = Q; *b0 = Q; return; } while (0)
    if (fabs(ax - a1->x) >= tol) {                       /* the line runs in x (:1696) */
        if ((low ? qx < ax : ax < qx) || qx == ax) B3D; else AD4;
    }
    if (qy <= 0) {                                       /* :1709 */
        if (c == 1) goto bcf;
        if (c == 3) { if (!(ay < qy) && qy != ay) B3D; AD4; }
        if (qy < ay) { *a0 = Q; if (y7 <= by && by != y7) *b1 = Q7; else *b1 = Q; return; }
        goto cc8;
    }
    if (c == 1) {                                        /* :1745 */
        if (qy < ay) { *a0 = Q; if (by < y7 || by == y7) *b1 = Q; else *b1 = Q7; return; }
        goto cc8;
    }
    if (c == 3) { if (!(qy < ay) && qy != ay) B3D; AD4; }
bcf:                                                     /* LAB_180076bcf :1797 */
    if (ay <= qy && qy != ay) { *a0 = Q; if (by <= y7 && y7 != by) *b1 = Q7; else *b1 = Q; return; }
    *a1 = Q; te = b1->y == y7; tb = y7 < b1->y;         /* :1817-1823 */
    if (tb || te) *b0 = Q; else *b0 = Q7;
    return;
cc8:                                                     /* LAB_180076cc8 :1734 */
    *a1 = Q; te = b1->y == y7; tb = b1->y < y7;
    if (tb || te) *b0 = Q; else *b0 = Q7;
#undef B3D
#undef AD4
}
/* FUN_180074510: the lines through the two end points (lying in x) and the inner point (lying in y) at z = 0, cut by rg_cut (mode 1
   Break: both pieces, mode 2 Trim: the second piece dropped; FUN_180076730: not stored when a piece of this run has the same ends) */
static int *rg_del, rg_ndel, rg_ndcap;                   /* Trim: the second pieces (sel 0x45), deleted after all sides (delete_all_by_mask(0x44, 2)) */
/* an entity of this edit goes off without an undo entry (made and deleted inside the edit: undo must not bring it back, as cui_nest.c); one from before the edit: mod_del */
static void rg_del_ent(int x) { if (mod_from >= 0 && x >= mod_from) { if (F.ents[x].type) ent_off(x); } else mod_del(x); }
static void rg_trim_at(const double ends[5][2]) {
    double tol = mc_systol.tol;
    for (int pi = 0; pi < 3; pi++) {
        double px = ends[pi == 0 ? 3 : 4][0], py = ends[pi == 0 ? 3 : 4][1];   /* FUN_180074510: ctour[1], ctour[n], ctour[n] */
        int low = fabs(px - ends[0][0]) < tol && fabs(py - ends[0][1]) < tol;   /* p2Equal(+0x790, pt) */
        int nent = F.nents;
        for (int i = 0; i < nent; i++) {
            Ent *e = &F.ents[i];
            if (e->type != E_LINE || !ENT_LV_SHOWN(e) || e->blanked) continue;   /* get_ent(mask 1): get_ent_univ (mccore 1802c0ee0) skips blanked entities */
            if (!(fabs(e->a.z) < tol && fabs(e->b.z) < tol)) continue;   /* FUN_1800749a0: in the plane Z = 0 */
            if (pi == 2 ? !(fabs(e->a.x - e->b.x) < tol) : !(fabs(e->a.y - e->b.y) < tol)) continue;
            if ((fabs(px - e->a.x) < tol && fabs(py - e->a.y) < tol) || (fabs(px - e->b.x) < tol && fabs(py - e->b.y) < tol)) continue;
            double dx = e->b.x - e->a.x, dy = e->b.y - e->a.y, L = dx * dx + dy * dy; if (L < 1e-18) continue;
            double t = ((px - e->a.x) * dx + (py - e->a.y) * dy) / L;
            if (t < 0 || t > 1 || fabs(e->a.x + t * dx - px) >= tol || fabs(e->a.y + t * dy - py) >= tol) continue;   /* proj_pt_ln */
            V3 a0 = e->a, a1 = e->b, b0 = e->a, b1 = e->b;
            rg_cut(low, ends, &a0, &a1, &b0, &b1);
            GE p0 = ge_line((P2){ a0.x, a0.y }, (P2){ a1.x, a1.y }, a0.z);
            rg_del_ent(i); geo_add_like(i, &p0, 1);          /* delete_ent, store_ent(0x41) */
            /* FUN_180076730: get_ent(0x41, L_ID) = the lines of this run (the groove's 0x40, the first pieces 0x41, the second pieces 0x45), alive; p3Equal either way round */
            int dup = 0;
            for (int q = mod_from; q < F.nents && !dup; q++) { const Ent *m = &F.ents[q]; if (m->type != E_LINE) continue;
                if ((fabs(m->a.x - b0.x) < tol && fabs(m->a.y - b0.y) < tol && fabs(m->a.z - b0.z) < tol && fabs(m->b.x - b1.x) < tol && fabs(m->b.y - b1.y) < tol && fabs(m->b.z - b1.z) < tol)
                    || (fabs(m->a.x - b1.x) < tol && fabs(m->a.y - b1.y) < tol && fabs(m->a.z - b1.z) < tol && fabs(m->b.x - b0.x) < tol && fabs(m->b.y - b0.y) < tol && fabs(m->b.z - b0.z) < tol)) dup = 1; }
            if (dup) continue;
            GE p1 = ge_line((P2){ b0.x, b0.y }, (P2){ b1.x, b1.y }, b0.z);
            int from = F.nents; geo_add_like(i, &p1, 1);     /* store_ent(0x45) */
            if (rg.trim == 2 && F.nents == from + 1) {       /* the second piece goes at the end */
                if (rg_ndel == rg_ndcap) { rg_ndcap = rg_ndcap ? 2 * rg_ndcap : 16; rg_del = realloc(rg_del, sizeof(int) * rg_ndcap); }
                rg_del[rg_ndel++] = from; }
        }
    }
}
/* the plane's view number for store_arc: AddMCView (mcam_add_view: an existing view by mx33_match within 2e-7, else a new one written by the save, mcam_saveops.c DB_ID 0x200 subtype 0) */
static int wm_view_for(const double R[9]) { mcam_fresh_views(&F); return mcam_add_view(&F, R, 2e-7); }   /* a File > New part: the operation defaults library's stored views are in n_views (mcam_saveops.c) */
/* FUN_1800765e0 (OK) / FUN_180076420 (Preview): one undo transaction: the groove on each side asked (FUN_180074370 / FUN_1800744b0), then
   the trim */
int wm_rg_exec(void) {
    RgEl els[6]; int n; double ends[5][2]; int did = 0;
    rg_ndel = 0;
    mod_begin();
    for (int pass = 0; pass < 2; pass++) for (int side = 0; side < 2; side++) {   /* FUN_180074370: each side's groove; FUN_1800744b0: each side's trim */
        if (side == 0 ? (rg.in & ~2) != 0 : (rg.in != 1 && rg.in != 2)) continue;   /* (+0x10 & 0xfd) == 0 / (+0x10 - 1) < 2 */
        if (!rg_build(side, els, &n, ends)) { if (pass == 0) { mod_end(); return did; } continue; }
        if (pass == 1) { if (rg.trim) rg_trim_at(ends); continue; }
        V3 z0 = { 0, 0, 0 };
        /* FUN_180074370: Planes.CombineActivePlaneAndOrigin(PlaneType 3 = WCS, ...: disassembly 1800743ac MOV R8D,3; mastercam.dll 1802c4e20's strings), Plane::Convert;
           CLChainAndCtour::store_ctour_wcs (mclathe.dll 18000f930): the plane's origin in view coordinates (op_view +0x50) + (x, y, 0.0),
           view_to_world in the plane's view: lines store_line, arcs constr_arc_new + store_arc in the plane's view */
        int id = plane_active[3] > 0 ? plane_active[3] : 1; const double *R = cam_plane_matrix(id), *O = plane_active_origin(3);
        double oz[3] = { O ? O[0] * R[0] + O[1] * R[1] + O[2] * R[2] : 0, O ? O[0] * R[3] + O[1] * R[4] + O[2] * R[5] : 0, O ? O[0] * R[6] + O[1] * R[7] + O[2] * R[8] : 0 };
        #define W(px, py) (V3){ R[0] * (oz[0] + (px)) + R[3] * (oz[1] + (py)) + R[6] * oz[2], R[1] * (oz[0] + (px)) + R[4] * (oz[1] + (py)) + R[7] * oz[2], R[2] * (oz[0] + (px)) + R[5] * (oz[1] + (py)) + R[8] * oz[2] }
        int view = wm_view_for(R);
        for (int i = 0; i < n; i++) {
            if (!els[i].arc) { V3 a = W(els[i].v[0], els[i].v[1]), b = W(els[i].v[2], els[i].v[3]); geo_add(E_LINE, a, b, z0, 0, 0, 0); }
            else if (fabs(R[8] - 1) < 1e-9 && fabs(R[0] - 1) < 1e-9) { V3 c = W(els[i].v[0], els[i].v[1]); double r = els[i].v[2], a0 = els[i].v[3], sw = els[i].v[4]; V3 a = { c.x + r * cos(a0), c.y + r * sin(a0), c.z }, b = { c.x + r * cos(a0 + sw), c.y + r * sin(a0 + sw), c.z }; geo_add(E_ARC, a, b, c, r, a0, sw); }
            else { double a0 = els[i].v[3], sw = els[i].v[4]; if (sw < 0) { a0 += sw; sw = -sw; } V3 cv = { oz[0] + els[i].v[0], oz[1] + els[i].v[1], oz[2] }; int q = mcam_new_arc_view(&F, view, R, cv, els[i].v[2], a0, sw, geo_level, geo_color); if (q >= 0) attr_new_ent(q); doc_dirty = 1; overlay_dirty = 1; }
        }
        #undef W
        did = 1;
    }
    for (int i = 0; i < rg_ndel; i++) if (rg_del[i] < F.nents && F.ents[rg_del[i]].type) rg_del_ent(rg_del[i]);   /* FUN_1800765e0 :1500 delete_all_by_mask */
    mod_end();
    return did;
}
void wm_rg_ok(void) { rg_save_prb(); }                 /* FUN_1800765e0's FUN_180078880 */
/* the Select button (FUN_180077be0): point_proc_2d "Select groove position" (631; mclathe.dll 1800267a0: world_to_view of GetActiveViewNo(Planes, 3), the WCS);
   Z = x - the origin in view coordinates of CombineActivePlaneAndOrigin(3) (disassembly 180077cc1), the diameter (unless Select Z only) = 2 |y - origin y| */
void wm_rg_pick(double wx, double wy, double wz) {
    int id = plane_active[3] > 0 ? plane_active[3] : 1; double w[3] = { wx, wy, wz }, v[3]; plane_w2v(id, plane_active_origin(3), w, v);   /* point_proc_2d - GetOriginInViewCoordinates */
    rg.z = v[0];
    if (!rg.zonly) rg.dim[rg.shape][rg.shape <= 1 ? 1 : 0] = fabs(v[1]) * 2;
    wm_rg_picking = 0;
}
/* wait_for_space (mccore 1804ebbe0): "Press <ENTER> to continue" (25539), the dialog suspended (AfxSuspendWindow); FUN_180076420 then removes the preview */
int wm_rg_wait;
void wm_rg_wait_start(void) { wm_rg_wait = 1; snprintf(cam_msg, sizeof cam_msg, "Press <ENTER> to continue"); overlay_dirty = 1; }
void wm_rg_enter(void) { if (!wm_rg_wait) return; wm_rg_wait = 0; if (geo_live) geo_live_drop(); cam_msg[0] = 0; overlay_dirty = 1; scene_reupload(); }
int wm_rg_prevsel(void) { return rg.prevsel; }
void wm_rg_gpset(const char *k, double x) {              /* tests (gpset:K=V): the panel controls; d0..d4 = the row of the current shape */
    if (!strcmp(k, "shape")) rg.shape = (int)x; else if (!strcmp(k, "in")) rg.in = (int)x; else if (!strcmp(k, "side")) rg.side = (int)x;
    else if (!strcmp(k, "shoulder")) rg.shoulder = (int)x; else if (!strcmp(k, "trim")) rg.trim = (int)x; else if (!strcmp(k, "zonly")) rg.zonly = (int)x;
    else if (!strcmp(k, "prevsel")) rg.prevsel = (int)x; else if (!strcmp(k, "z")) rg.z = x;
    else if (k[0] == 100 && k[1] >= 48 && k[1] <= 52) rg.dim[rg.shape][k[1] - 48] = x;
}
void wm_rg_panel(const CuiPanelOps *ops, void *u) {
    double *D = rg.dim[rg.shape]; int sh01 = rg.shape <= 1;
    static int last = -1; if (last != rg.shape) { last = rg.shape; geo_panel_layout++; }   /* the labels (FUN_1800785d0) change with the shape */
    rg_load_table();
    /* the dialog CDINGrv (MCCADRES 2516); labels FUN_1800785d0: shape 0/1 dg (4029) g (4031) r (4034), 2/3 t1 (4030) f1 (4032) r1 (4033);
       the second g (control 1500, g) only for 2/3 */
    static char pic[32]; snprintf(pic, sizeof pic, "#img:mccadres_BMP_%d", 3178 + rg.shape);   /* static 1505: FUN_180078480, 0xc6a + shape */
    if (ops->exp(u, "Shape")) { ops->label(u, pic); static const char *t[4] = { "OD thread (DIN 76 T1 Shape A)", "ID thread (DIN 76 T1 Shape C)", "Shoulder on shaft (DIN 509 Shape E)", "Shoulder on shaft (high stress) (DIN 509 Shape F)" };
        for (int i = 0; i < 4; i++) ops->radio(u, t[i], &rg.shape, i, 1);
        ops->end(u); }
    if (ops->exp(u, "Dimensions")) {
        static const char *names[RG_MAXROW]; for (int i = 0; i < rg_ntbl; i++) names[i] = rg_tbl[i].name;
        if (rg_ntbl && ops->combo) { int before = rg_tbl_sel; ops->combo(u, names, rg_ntbl, &rg_tbl_sel, 0);
            if (rg_tbl_sel != before && rg_tbl_sel >= 0 && rg_tbl_sel < rg_ntbl) memcpy(D, rg_tbl[rg_tbl_sel].dim, 5 * sizeof(double)); }   /* Select From Table (OnThdtablePb): the row's values */
        ops->num(u, "d:", "d", &D[sh01 ? 1 : 0], NULL, 1, -1e6, 1e6);
        ops->num(u, sh01 ? "dg:" : "t1:", "dg", &D[sh01 ? 2 : 1], NULL, 1, -1e6, 1e6);
        ops->num(u, sh01 ? "r:" : "r1:", "r", &D[4], NULL, 1, -1e6, 1e6);
        ops->num(u, sh01 ? "g:" : "f1:", "g", &D[sh01 ? 3 : 2], NULL, 1, -1e6, 1e6);
        ops->num(u, "g:", "g2", &D[3], NULL, !sh01, -1e6, 1e6);   /* edit 1500 + label 1501: hidden for shapes A / C (Ordinal_13643 SW_HIDE, FUN_1800785d0) */
        ops->end(u); }
    if (ops->exp(u, "Orientation")) {
        ops->check(u, "Shoulder on right", &rg.shoulder, 1);
        ops->label(u, "Create geometry in:");
        ops->radio(u, "+X", &rg.in, 0, 1); ops->radio(u, "-X", &rg.in, 1, 1); ops->radio(u, "Both", &rg.in, 2, 1);
        ops->end(u); }
    if (ops->exp(u, "Trim/break horizontal or vertical lines")) {
        ops->radio(u, "None", &rg.trim, 0, 1); ops->radio(u, "Break lines at groove", &rg.trim, 1, 1); ops->radio(u, "Trim lines to groove", &rg.trim, 2, 1);
        ops->end(u); }
    if (ops->exp(u, "Position")) {
        ops->check(u, "Select Z only", &rg.zonly, 1);
        ops->check(u, "Preview groove at selected position", &rg.prevsel, 1);
        ops->toggle(u, "_Select...", 1, 7);
        ops->num(u, "Z Position", "z", &rg.z, NULL, 1, -1e6, 1e6);
        /* radios 1509 / 1511 have no text: statics 1510 / 1512 show MCCADRES bitmaps (FUN_180078480): 3182 / 3184, Shoulder on right 3183 / 3185, the cross = the Z position.
           1509: the left end of the groove at Z, 1511: the right end (+0x11 0 / 1: FUN_180074c80 :660) */
        ops->radio(u, rg.shoulder ? "#img:mccadres_BMP_3183" : "#img:mccadres_BMP_3182", &rg.side, 0, 1); ops->radio(u, rg.shoulder ? "#img:mccadres_BMP_3185" : "#img:mccadres_BMP_3184", &rg.side, 1, 1);
        ops->toggle(u, "Preview _Groove...", 1, 8);
        ops->end(u); }
}


/* ---------------------------------------------------------------------------------------- Create Letters */
/* create_letters (mccad.dll 18006ea40; dialog CLetDlg 434 FUN_180066980...): the letters_data struct (DAT_1806a4260): +0 Mode (combo, 1..5 Mastercam
   Box / Block / Roman / Slant / Other, 6..14 Drafting fonts, 15 TrueType; FUN_180068550, strings 5888..5902), +2 the text, +0x12e the drafting
   font index, +0x130 alignment (0 Horizontal, 1 Vertical, 2 Top of Arc, 3 Bottom of Arc), +0x138 Height, +0x140 the folder (Other), +0x270 Arc Radius, +0x278
   Spacing. Defaults (create_letters :36-62): height 20 / 1, spacing 5 / 0.2, arc radius 20 / 1, mode 1.
   Modes 6..14: drw_string, the drafting note path (let_draft); alignment Top of Chain: FUN_18006cce0; the arc pick of the arc alignments: FUN_18006d450.
   Mode 15 (GetGlyphOutline GGO_NATIVE at the hinted 96 ppem of a Windows font: create_tt_letters mccore 1808b3c80) is NOT PORTED (decision N9-21). */
#include "mcam_saveops.h"
#include "../draft.h"
#include "../draft_note.h"
typedef struct { char text[512], folder[300]; int mode, align, chain, userspc; double height, radius, spacing; } LetState;
static LetState let; static int let_units = -1;
static int let_cancelled;                                /* FUN_18006c0b0 returned 0 (the width prompt cancelled): FUN_18006d450's loop ends */
static const char *let_fonts[15] = { "Mastercam (Box) Font", "Mastercam (Block) Font", "Mastercam (Roman) Font", "Mastercam (Slant) Font", "Mastercam (Other) Font",
    "Drafting (Stick) Font", "Drafting (Roman) Font", "Drafting (European) Font", "Drafting (Swiss) Font", "Drafting (Hartford) Font", "Drafting (Old English) Font", "Drafting (Palatino) Font", "Drafting (Dayville) Font", "Drafting (Current) Font", "TrueType Font" };   /* 5888..5902 */
int wm_let_ported(void) { return let.mode >= 1 && let.mode <= 14; }
static int wm_let_avail(void) { return wm_let_ported(); }
static void let_prm_load(void);
int wm_let_mode(void) { return let.mode; }
void wm_let_begin(void) {
    int m = mcam_is_metric(&F) ? 2 : 1;
    if (let_units != m) { let_units = m; memset(&let, 0, sizeof let); let.mode = 1; let.height = m == 2 ? 20 : 1; let.spacing = m == 2 ? 5 : 0.2; let.radius = m == 2 ? 20 : 1;
        const char *k = site_path("fonts"); if (k[0]) snprintf(let.folder, sizeof let.folder, "%s", k); }
    let_prm_load();                                      /* FUN_180067fb0: at every dialog open */
}
/* a name in a folder, case-insensitive (the Windows file system); 1 = found (path filled) */
static int let_find(const char *dir, const char *name, char *path, size_t n) {
    DIR *d = opendir(dir); if (!d) return 0; struct dirent *de; int ok = 0;
    while ((de = readdir(d))) if (!strcasecmp(de->d_name, name)) { snprintf(path, n, "%s/%s", dir, de->d_name); ok = 1; break; }
    closedir(d); return ok;
}
/* the font folder: font_prefix + BOX / BLOCK / ROMAN / SLANT (FUN_18006c0b0: the path is made without checking the folder exists), the folder field for Other; 0 = paths.conf has no fonts entry (ours) */
static int let_font_dir(char *out, size_t n) {
    if (let.mode == 5) { snprintf(out, n, "%s", let.folder); while (strlen(out) > 1 && (out[strlen(out) - 1] == '/' || out[strlen(out) - 1] == '\\')) out[strlen(out) - 1] = 0; return 1; }
    const char *k = site_path("fonts"); static const char *sub[4] = { "BOX", "BLOCK", "ROMAN", "SLANT" };
    if (!k[0]) { out[0] = 0; return 0; }
    if (!let_find(k, sub[let.mode - 1], out, n)) snprintf(out, n, "%s/%s", k, sub[let.mode - 1]);
    return 1;
}
/* FUN_18006db00: letters.dat: the default width on the first line, then "code width" lines; a lowercase with no width takes its uppercase, an entry still 0 the default.
   No letters.dat: get_b_real(mccoreres 5480 "Enter default letter width", tol .. 1e38, start 0.8) -> RequestRealInputModal (mastercam.dll 180327430, the dialog's own range check NOT DECODED), every entry that width;
   0 = cancelled. */
static double let_w[256];
static int let_widths(const char *dir) {
    char p[1400]; for (int i = 0; i < 256; i++) let_w[i] = 0;
    FILE *f = let_find(dir, "letters.dat", p, sizeof p) ? fopen(p, "r") : NULL;
    if (!f) { double w = 0.8; int ok = cui_hooks.ask_real ? cui_hooks.ask_real("Enter default letter width", &w) : (cui_script_real_ok ? (w = cui_script_real, 1) : 0);
        if (!ok) return 0;
        for (int i = 0; i < 256; i++) let_w[i] = w;
        return 1; }
    char line[300]; double def = 0.8; int first = 1;
    while (fgets(line, sizeof line, f)) {
        if (first) { def = atof(line); first = 0; continue; }
        int c; double w; if (sscanf(line, "%d %lf", &c, &w) == 2 && c >= 0 && c < 256) let_w[c] = w;
    }
    fclose(f);
    double tol = mc_systol.tol;
    for (int i = 0; i < 256; i++) { if (i >= 'a' && i <= 'z' && fabs(let_w[i]) < tol) let_w[i] = let_w[toupper(i)]; if (fabs(let_w[i]) < tol) let_w[i] = def; }
    return 1;
}
/* check_letter_geometry_file_exist (mccad.dll 18006b620) + FUN_18006bb70: CHAR%03hu, then the letter's name (a - z upper case, the named punctuation, printable characters,
   otherwise A%03hu), trying .mcam .mcx-8 .mcx-7 .mcx-6 .mcx-5 in that order; the character reaches the format zero-extended (MOVZX R14D,DIL, 18006b6b0: 233, not 65513). OURS: + .mcx-9: the only letter files in this install
   are X9's (common/Fonts). Not found: path = the last name tried + .mcam (FUN_18006bb70's last lines). */
static int let_file(const char *dir, int ch, char *path, size_t n) {
    static const char *ext[6] = { ".mcam", ".mcx-8", ".mcx-7", ".mcx-6", ".mcx-5", ".mcx-9" };
    char nm[64], fn[80];
    snprintf(nm, sizeof nm, "CHAR%03hu", (unsigned short)ch);
    for (int i = 0; i < 6; i++) { snprintf(fn, sizeof fn, "%s%s", nm, ext[i]); if (let_find(dir, fn, path, n)) return 1; }
    nm[0] = 0;
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 0x20 && ch < 0x7f)) { nm[0] = (char)(ch >= 'a' && ch <= 'z' ? ch - 32 : ch); nm[1] = 0; }
    static const struct { int c; const char *s; } nam[] = { {0x22,"QUOTE"},{0x25,"PERCENT"},{0x27,"SQUOTES"},{0x2a,"ASTERISK"},{0x2b,"PLUS"},{0x2c,"COMMA"},{0x2d,"MINUS"},{0x2e,"PERIOD"},{0x2f,"SLASH"},{0x3a,"COLON"},{0x3b,"SEMICOLN"},{0x3c,"LTHAN"},{0x3d,"EQUALS"},{0x3e,"GTHAN"},{0x3f,"QUESTION"},{0x5c,"BACKSLSH"} };
    for (size_t k = 0; k < sizeof nam / sizeof nam[0]; k++) if (nam[k].c == ch) snprintf(nm, sizeof nm, "%s", nam[k].s);
    if (!nm[0]) snprintf(nm, sizeof nm, "A%03hu", (unsigned short)ch);
    for (int i = 0; i < 6; i++) { snprintf(fn, sizeof fn, "%s%s", nm, ext[i]); if (let_find(dir, fn, path, n)) return 1; }
    snprintf(path, n, "%s/%s.mcam", dir, nm);
    return 0;
}
/* the output of one letter, in active-view coordinates (FUN_18006e7f0 before its view_to_world) */
typedef struct { int t; double a[3], b[3], c[3], r, a0, sw; uint8_t *rec; uint32_t n; } LetEl;   /* t 0 line, 1 arc, 2 NURBS */
static LetEl *lb; static int nlb, lbcap;
static void lb_clear(void) { for (int i = 0; i < nlb; i++) free(lb[i].rec); nlb = 0; }
static LetEl *lb_new(void) { if (nlb == lbcap) { lbcap = lbcap ? 2 * lbcap : 256; lb = realloc(lb, sizeof(LetEl) * lbcap); } LetEl *e = &lb[nlb++]; memset(e, 0, sizeof *e); return e; }
/* FUN_180069e90 / FUN_18006a0e0: the list of the part files not found: FUN_18006de40's FUN_18006a2c0 text (mccoreres 13010 " %s part file not found", %s = folder + name without the extension), one line each: "%s)\n" (the format at mccad.dll 0x180412d7c, as stored), "%s (%d)\n" for a repeated one
   (dbmmess_mbok 5605: the empty string; the box shows the list) */
#define LET_MISS 64
static char let_miss_last[2048];                         /* tests: letmiss */
static struct { char s[1600]; int n; } let_miss[LET_MISS]; static int nlet_miss;
static void let_miss_add(const char *path) {
    char d[1500]; snprintf(d, sizeof d, "%s", path); char *dot = strrchr(d, '.'), *sl = strrchr(d, '/'); if (dot && (!sl || dot > sl)) *dot = 0;
    char t[1600]; snprintf(t, sizeof t, " %s part file not found", d);
    for (int i = 0; i < nlet_miss; i++) if (!strcmp(let_miss[i].s, t)) { let_miss[i].n++; return; }
    if (nlet_miss < LET_MISS) { snprintf(let_miss[nlet_miss].s, sizeof let_miss[nlet_miss].s, "%s", t); let_miss[nlet_miss++].n = 1; }
}
static void let_miss_show(void) {
    static char b[LET_MISS * 1640]; size_t k = 0; if (!nlet_miss) return;
    for (int i = 0; i < nlet_miss; i++) k += (size_t)(let_miss[i].n < 2 ? snprintf(b + k, sizeof b - k, "%s)\n", let_miss[i].s) : snprintf(b + k, sizeof b - k, "%s (%d)\n", let_miss[i].s, let_miss[i].n));
    cui_message("", "%s", b); nlet_miss = 0;
    snprintf(let_miss_last, sizeof let_miss_last, "%s", b);
}
/* the placement state (DAT_1806a3a10 ...: FUN_18006c0b0 / FUN_18006e7f0); lh = DAT_18064af20, the height after the tol test */
static double ls, la20, la28, la30, lr, la18, lpx, lpy, lh; static int larc, ltop;
static void let_pt(V3 p, double out[3]) {
    double x = ls * p.x + la20, y = ls * p.y + la28, z = ls * p.z + la30;
    if (larc) {
        if (ltop) y += lr; else y -= (lh + lr);
        if (fabs(la18) > 1e-6) { double dx = x - lpx, dy = y - lpy, c = cos(la18), s = sin(la18); x = lpx + c * dx - s * dy; y = lpy + s * dx + c * dy; }   /* rotate_pt mccore 1804b4550 */
    }
    out[0] = x; out[1] = y; out[2] = z;
}
static V3 let_world(const double v[3]) {                 /* view_to_world (active view) at the end of FUN_18006e7f0: no plane origin there */
    int id = plane_active[0] > 0 ? plane_active[0] : 1; const double *R = cam_plane_matrix(id);
    return (V3){ R[0] * v[0] + R[3] * v[1] + R[6] * v[2], R[1] * v[0] + R[4] * v[1] + R[7] * v[2], R[2] * v[0] + R[5] * v[1] + R[8] * v[2] };
}
extern int mc_main_level_vis;
/* one letter file (FUN_18006d910): read_ent_from_disk, skipping a record with ent +0x3c > 0 unless its sel bit 1 is set, and everything but L_ID, A_ID, NB_ID */
static void let_load(const char *path) {
    int vis = mc_main_level_vis; McamFile *L = calloc(1, sizeof *L);
    if (!L) return;
    if (!mcam_load(L, path)) { free(L); mc_main_level_vis = vis; return; }
    mc_main_level_vis = vis;
    for (int i = 0; i < L->nents; i++) {
        Ent *e = &L->ents[i];
        if (e->rec3c > 0 && !(e->rec_sel & 1)) continue;
        if (e->type == E_LINE) { LetEl *q = lb_new(); let_pt(e->a, q->a); let_pt(e->b, q->b); }
        else if (e->type == E_ARC) { LetEl *q = lb_new(); q->t = 1; let_pt(e->c, q->c); q->a0 = e->a0 + (larc ? la18 : 0); q->sw = e->sw; q->r = e->r * ls; }   /* FUN_18006e2a0 */
        else if (e->type == E_SPLINE && e->cls == 0x80 && e->crv >= 0 && e->crv < L->ncrvs) {   /* the NB_ID branch: control points, +0xa8 x scale, the end points */
            CurveRec *c = &L->crvs[e->crv]; uint8_t *r = malloc(c->n); if (!r) continue; memcpy(r, c->rec, c->n);
            int nk = rd32le(r + 0xc8), nc = rd32le(r + 0xcc), vpp = r[0xd1], nonrat = r[0xd2] & 4; size_t off = 0xd8 + (size_t)nk * 8;
            double sc; memcpy(&sc, r + 0xa8, 8); sc *= ls; memcpy(r + 0xa8, &sc, 8);
            for (int q = 0; q < nc; q++) { double *v = (double *)(r + off + (size_t)q * vpp * 8); int o = nonrat ? 0 : 1; V3 ip = { v[o], v[o + 1], v[o + 2] }; double t[3]; let_pt(ip, t); v[o] = t[0]; v[o + 1] = t[1]; v[o + 2] = t[2]; }
            for (int off2 = 0x68; off2 <= 0x80; off2 += 0x18) { double *v = (double *)(r + off2); V3 ip = { v[0], v[1], v[2] }; double t[3]; let_pt(ip, t); v[0] = t[0]; v[1] = t[1]; v[2] = t[2]; }
            LetEl *q = lb_new(); q->t = 2; q->rec = r; q->n = c->n;
        }
    }
    mcam_free(L); free(L);
}
/* the buffer -> entities in the main colour / level: optional rotation about the view origin (FUN_18006e060) and translation (FUN_18006e710) in view coordinates */
static void lb_flush(int from, double ang, const double tv[3]) {
    int id = plane_active[0] > 0 ? plane_active[0] : 1; const double *R = cam_plane_matrix(id); V3 z0 = { 0, 0, 0 };
    int top = fabs(R[8] - 1) < 1e-9 && fabs(R[0] - 1) < 1e-9; double c = cos(ang), sn = sin(ang);
#define XF(p) do { double x_ = (p)[0], y_ = (p)[1]; (p)[0] = c * x_ - sn * y_ + (tv ? tv[0] : 0); (p)[1] = sn * x_ + c * y_ + (tv ? tv[1] : 0); (p)[2] += tv ? tv[2] : 0; } while (0)
    for (int i = from; i < nlb; i++) {
        LetEl *e = &lb[i];
        if (e->t == 0) { XF(e->a); XF(e->b); V3 wa = let_world(e->a), wb = let_world(e->b); geo_add(E_LINE, wa, wb, z0, 0, 0, 0); }
        else if (e->t == 1) {
            XF(e->c); double a0 = e->a0 + ang, sw = e->sw, r = e->r;
            if (top) { V3 wc = { e->c[0], e->c[1], e->c[2] }; V3 a = { wc.x + r * cos(a0), wc.y + r * sin(a0), wc.z }, b = { wc.x + r * cos(a0 + sw), wc.y + r * sin(a0 + sw), wc.z }; geo_add(E_ARC, a, b, wc, r, a0, sw); }
            else { int view = wm_view_for(R); V3 cv = { e->c[0], e->c[1], e->c[2] }; int q = mcam_new_arc_view(&F, view, R, cv, r, a0, sw, geo_level, geo_color); if (q >= 0) attr_new_ent(q); doc_dirty = 1; overlay_dirty = 1; }
        } else {
            uint8_t *r = e->rec; int nk = rd32le(r + 0xc8), nc = rd32le(r + 0xcc), vpp = r[0xd1], nonrat = r[0xd2] & 4; size_t off = 0xd8 + (size_t)nk * 8;
            for (int q = 0; q < nc; q++) { double *v = (double *)(r + off + (size_t)q * vpp * 8); int o = nonrat ? 0 : 1; XF(v + o); V3 w = let_world(v + o); v[o] = w.x; v[o + 1] = w.y; v[o + 2] = w.z; }
            for (int off2 = 0x68; off2 <= 0x80; off2 += 0x18) { double *v = (double *)(r + off2); XF(v); V3 w = let_world(v); v[0] = w.x; v[1] = w.y; v[2] = w.z; }
            int q = mcam_new_curve(&F, r, e->n, geo_level, geo_color); if (q >= 0) attr_new_ent(q); doc_dirty = 1; overlay_dirty = 1;
        }
    }
#undef XF
}
/* the drafting fonts (drw_string -> FUN_18031a950: the note's font attributes init_attribs, font index +0x12e, height +0x138 when > tol; mode 1 stores the strokes as lines): the
   viewer's port of the stroke text (draft_render_text) to polylines, each segment a line (FUN_18006beb0 / FUN_18006a9b0 with "\x01") */
typedef struct { int first; } LetDraftCtx;
static void let_poly(void *ctx, const double *xyz, int n, int closed, int fill, int color, int view) {
    (void)ctx; (void)fill; (void)color; (void)view;
    int id = plane_active[0] > 0 ? plane_active[0] : 1; const double *R = cam_plane_matrix(id);
    for (int i = 0; i + 1 < n + (closed ? 1 : 0); i++) {
        const double *a = xyz + 3 * i, *b = xyz + 3 * ((i + 1) % n); LetEl *q = lb_new();
        for (int k = 0; k < 3; k++) { q->a[k] = a[0] * R[3 * k] + a[1] * R[3 * k + 1] + a[2] * R[3 * k + 2]; q->b[k] = b[0] * R[3 * k] + b[1] * R[3 * k + 1] + b[2] * R[3 * k + 2]; }
    }
}
const void *cui_draft_fonts(void);
/* the font index FUN_180066db0 stores at letters_data +0x12e: modes 6..13: mode - 6; mode 14: note_font (init_attribs keeps it) */
static int let_draft_font(void) { return let.mode == 14 ? -1 : let.mode - 6; }
/* chain: FUN_18006a9b0 sets the attribute word & 0xedff | 0xd00 (font_attr +0x58: bits 8-9 h_aln = 1 Center, bits 10-12 v_aln = 3 Base; draft_note.h); FUN_18006beb0 leaves init_attribs' */
static int let_draft(const double pv[3], const char *text, int *views, int chain) {
    int id = plane_active[0] > 0 ? plane_active[0] : 1; if (id < 1 || id > 8) { *views = 0; return 0; }   /* a reserved view (drw_string takes the active view number; a user plane's: not reached here) */
    *views = 1; const void *fonts = cui_draft_fonts(); if (!fonts) return 0;
    DnGlobals g; dn_globals_config(&g, mcam_is_metric(&F));
    int f = let_draft_font(); if (f >= 0) g.font = f;
    if (chain) { g.h_aln = 1; g.v_aln = 3; }
    if (let.height >= mc_systol.tol && let.height != mc_systol.tol) g.height = let.height;
    uint8_t E[0x1c8]; memset(E, 0, sizeof E); dn_calc_note(E, &g, id, pv);
    int32_t mid = 77; memcpy(E + 0xf8, &mid, 4);
    DraftDoc *d = draft_doc_new(); draft_doc_add_mt(d, mid, (const uint8_t *)text, strlen(text) + 1); draft_doc_add_rec(d, E, 0x1c8);
    DraftOut o = { NULL, let_poly };
    draft_render_text(d, E, 0x1c8, (const DraftFonts *)fonts, &o);
    draft_free(d);
    return 1;
}
static void let_prm_name(char *p, size_t n) { const char *d = getenv("MCAMUI_PRB_DIR"); if (d) snprintf(p, n, "%s/LETTERS.PRM", d); else config_file(p, n, "LETTERS.PRM"); }
/* FUN_180069100: wr_param_START("LETTERS.PRM", "LETTERS", 1), the keys sorted (wr_param_END) through WritePrivateProfileString: REAL "%.15f" with the trailing zeros stripped, BOOL Yes / No */
static void let_prm_fmt(char *b, size_t n, double v) { snprintf(b, n, "%.15f", v); int l = (int)strlen(b); while (l > 1 && b[l - 1] == '0' && b[l - 2] != '.') b[--l] = 0; }
static void let_prm_save(void) {
    char p[1100], h[64], r[64], s[64]; let_prm_name(p, sizeof p); FILE *f = fopen(p, "w"); if (!f) return;
    let_prm_fmt(h, sizeof h, let.height); let_prm_fmt(r, sizeof r, let.radius); let_prm_fmt(s, sizeof s, let.spacing);
    fprintf(f, "[LETTERS]\r\nAlignment=%d\r\nHeight=%s\r\nMode=%d\r\nRadius=%s\r\nSpacing=%s\r\nUserSpacing=%s\r\n", let.chain ? 4 : let.align, h, let.mode, r, s, let.userspc ? "Yes" : "No");
    fclose(f);
}
static void let_prm_load(void) {                         /* FUN_180067fb0: rd_param_* (rd_param_REAL: |v| < tol = 0) */
    char p[1100], l[200]; let_prm_name(p, sizeof p); FILE *f = fopen(p, "r"); if (!f) return; double tol = mc_systol.tol;
    while (fgets(l, sizeof l, f)) { char *e = strchr(l, '='); if (!e) continue; *e = 0; char *v = e + 1; double x = atof(v); if (fabs(x) < tol) x = 0; int a = 0;
        if (!strcmp(l, "Alignment")) { a = atoi(v); if (a == 4) { let.chain = 1; let.align = 0; } else { let.chain = 0; let.align = a; } }
        else if (!strcmp(l, "Height")) let.height = x; else if (!strcmp(l, "Mode")) { a = atoi(v); if (a >= 1 && a <= 15) let.mode = a; }
        else if (!strcmp(l, "Radius")) let.radius = x; else if (!strcmp(l, "Spacing")) let.spacing = x; else if (!strcmp(l, "UserSpacing")) let.userspc = !strncasecmp(v, "Yes", 3); }
    fclose(f);
}
/* FUN_18006c0b0: one placement at the world point W */
void wm_let_exec(V3 w) {
    if (!wm_let_avail() || !let.text[0]) return;
    let_prm_save();
    int id = plane_active[0] > 0 ? plane_active[0] : 1; const double *R = cam_plane_matrix(id);
    double vx = w.x * R[0] + w.y * R[1] + w.z * R[2], vy = w.x * R[3] + w.y * R[4] + w.z * R[5], vz = w.x * R[6] + w.y * R[7] + w.z * R[8];
    lb_clear();
    if (let.mode >= 6) {                                 /* FUN_18006bfa0 / FUN_18006beb0 */
        char txt[1100]; size_t j = 0; for (const char *q = let.text; *q && j + 2 < sizeof txt; q++) { if (q[0] == '\\' && q[1] == 'n') { txt[j++] = '\r'; txt[j++] = '\n'; q++; } else txt[j++] = *q; } txt[j] = 0;
        double pv[3] = { vx, vy, vz }; int vs; if (!let_draft(pv, txt, &vs, 0)) { if (!vs) cui_message("Create Letters", "NOT PORTED: a drafting font in a plane other than the reserved views"); else cui_message("Create Letters", "The drafting fonts are not in the fonts folder"); return; }
        mod_begin(); lb_flush(0, 0, NULL); mod_end(); lb_clear(); doc_dirty = 1; overlay_dirty = 1; return;
    }
    char dir[1400]; if (!let_font_dir(dir, sizeof dir)) { cui_message("Create Letters", "The fonts folder is not set (paths.conf: fonts)"); return; }   /* ours: the folder setting of this program */
    if (!let_widths(dir)) { let_cancelled = 1; return; }  /* FUN_18006c0b0: return 0 ends FUN_18006d450's loop */
    double H = let.height < mc_systol.tol ? 1.0 : let.height, S = let.spacing < mc_systol.tol ? H * 0.2 : let.spacing;   /* :1877 - :1884 */
    lh = H; ls = H / 1.4; larc = let.align > 1; ltop = let.align == 2; lr = let.radius; lpx = vx; lpy = vy;
    char buf[512]; snprintf(buf, sizeof buf, "%s", let.text); char *lines[64]; int nl = 0; char *q = buf; lines[nl++] = q;
    for (char *p = buf; *p; p++) { if (p[0] == '\\' && p[1] == 'n') { *p = 0; q = p + 2; if (nl < 64) lines[nl++] = q; p++; } else if (*p == '\n') { *p = 0; q = p + 1; if (nl < 64) lines[nl++] = q; } else if (*p == '\r') *p = 0; }
    if (larc && ltop) for (int i = 0, j = nl - 1; i < j; i++, j--) { char *t = lines[i]; lines[i] = lines[j]; lines[j] = t; }
    mod_begin();
    double px = vx, py = vy, pz = vz;
    for (int k = 0; k < nl; k++) {
        const unsigned char *s = (const unsigned char *)lines[k]; int n = (int)strlen(lines[k]);
        double wmax = n ? let_w[s[0]] : let_w['H'], arcw = 0;   /* FUN_18006c0b0 :339-345: an empty line: DAT_1806a3ca0 = the width of 'H' */
        if (!larc) { la20 = px; la28 = H * 0.5 + py; la30 = pz; }
        else { double total = n ? ls * let_w[s[0]] : 0; for (int i = 1; i < n; i++) total += ls * let_w[s[i]] + S;
            arcw = total * 0.5; la18 = (arcw - ls * wmax * 0.5) / lr; if (!ltop) la18 = -la18; la30 = pz; }   /* FUN_18006b440 */
        for (int i = 0; i < n; i++) {
            int c = s[i]; if (c == 10 || c == 13) continue;
            if (let_w[c] > wmax) wmax = let_w[c];
            double dx = 0, dy = 0, da = 0;
            if (!larc) { if (let.align == 0) dx = ls * let_w[c] + S; else dy = -(H + S); }
            else { if (i < n - 1) { da = ((let_w[c] + let_w[s[i + 1]]) * ls * 0.5 + S) / lr; if (ltop) da = -da; } la20 = let_w[c] * ls * -0.5 + px; la28 = H * 0.5 + py; la30 = pz; }
            if (c != ' ') { char p[1500]; if (let_file(dir, c, p, sizeof p)) let_load(p); else let_miss_add(p); }
            if (!larc) { la20 += dx; la28 += dy; } else la18 += da;
        }
        if (!larc) { if (let.align == 1) px += ls * wmax + S; else py -= H + S; }
        else lr += H + S;
    }
    lb_flush(0, 0, NULL); lb_clear();
    mod_end(); doc_dirty = 1; overlay_dirty = 1;
    let_miss_show();                                     /* FUN_18006a0e0 */
}
/* Top of Chain (FUN_18006cce0, FUN_18006a9b0): each character made at the origin (FUN_18006b0d0 / drw_string), its width the bounding box in the view (EntityMinMax); modes 1..5 centred on x
   (-width / 2); then along the chain: a space advances by the average width + spacing, a letter by half its width (FUN_18006b540: one step to the next chain entity when past the end of the
   current one, the rest of the line dropped when the chain ends), placed at the creep point (z 0 in the view, proj_pt_cp) rotated to the tangent (+ pi on a reversed entity), then half its width + spacing */
typedef struct { char c; int first, count; double w; } LetCh;
void wm_let_chain_exec(void) {
    if (!wm_let_avail() || !let.text[0] || !geo_nchains) return;
    let_prm_save();
    int id = plane_active[0] > 0 ? plane_active[0] : 1; const double *R = cam_plane_matrix(id);
    double H = let.height, S = let.spacing; char dir[1400]; dir[0] = 0;   /* FUN_18006b0d0 / FUN_18006cce0: +0x138 and +0x278 as they are (no tol test here) */
    if (let.mode < 6 && !let_font_dir(dir, sizeof dir)) { cui_message("Create Letters", "The fonts folder is not set (paths.conf: fonts)"); return; }   /* ours */
    lh = H; ls = H / 1.4; larc = 0; la18 = 0; lr = let.radius; lpx = lpy = 0;
    lb_clear(); LetCh ch[512]; int nch = 0, nlet = 0; double sumw = 0;
    for (const unsigned char *q = (const unsigned char *)let.text; *q && nch < 512; q++) {
        int c = *q; if (c == 13) continue;
        if (c == '\\' && q[1] == 'n') { ch[nch].c = 10; ch[nch].first = nlb; ch[nch].count = 0; ch[nch++].w = 0; q++; continue; }
        if (c == 10 || c == ' ') { ch[nch].c = (char)c; ch[nch].first = nlb; ch[nch].count = 0; ch[nch++].w = 0; continue; }
        int from = nlb;
        if (let.mode < 6) { char p[1500];                /* FUN_18006b0d0: FUN_18006db00 (and its width prompt) for each letter; a cancelled prompt: nothing listed, the letter dropped */
            if (!let_widths(dir)) continue;
            if (!let_file(dir, c, p, sizeof p)) { let_miss_add(p); continue; }
            la20 = 0; la28 = H * 0.5; la30 = 0; let_load(p); }   /* FUN_18006b0d0 */
        else { char t[2] = { (char)c, 0 }; double p0[3] = { 0, 0, 0 }; int vs; if (!let_draft(p0, t, &vs, 1)) { if (!vs) { cui_message("Create Letters", "NOT PORTED: a drafting font in a plane other than the reserved views"); lb_clear(); return; } continue; } }
        if (nlb == from) continue;
        double x0 = 1e38, x1 = -1e38;
        for (int i = from; i < nlb; i++) {                /* EntityMinMax on x */
            LetEl *e = &lb[i];
            if (e->t == 0) { x0 = fmin(x0, fmin(e->a[0], e->b[0])); x1 = fmax(x1, fmax(e->a[0], e->b[0])); }
            else if (e->t == 1) { double a = e->a0, b = e->a0 + e->sw, lo = fmin(a, b), hi = fmax(a, b); x0 = fmin(x0, e->c[0] + e->r * fmin(cos(a), cos(b))); x1 = fmax(x1, e->c[0] + e->r * fmax(cos(a), cos(b)));
                for (double k = ceil(lo / (2 * M_PI)); k * 2 * M_PI <= hi; k += 1) x1 = fmax(x1, e->c[0] + e->r); for (double k = ceil((lo - M_PI) / (2 * M_PI)); k * 2 * M_PI + M_PI <= hi; k += 1) x0 = fmin(x0, e->c[0] - e->r); }
            else { uint8_t *r = e->rec; int nk = rd32le(r + 0xc8), nc = rd32le(r + 0xcc), vpp = r[0xd1], o = (r[0xd2] & 4) ? 0 : 1; size_t off = 0xd8 + (size_t)nk * 8; for (int q2 = 0; q2 < nc; q2++) { double *v = (double *)(r + off + (size_t)q2 * vpp * 8); x0 = fmin(x0, v[o]); x1 = fmax(x1, v[o]); } }
        }
        double w = fabs(x1 - x0);
        if (let.mode < 6) for (int i = from; i < nlb; i++) { LetEl *e = &lb[i]; double d = -w * 0.5;   /* (ushort)(*param_1 - 1U) < 5: the letter centred */
            if (e->t == 0) { e->a[0] += d; e->b[0] += d; } else if (e->t == 1) e->c[0] += d;
            else { uint8_t *r = e->rec; int nk = rd32le(r + 0xc8), nc = rd32le(r + 0xcc), vpp = r[0xd1], o = (r[0xd2] & 4) ? 0 : 1; size_t off = 0xd8 + (size_t)nk * 8; for (int q2 = 0; q2 < nc; q2++) ((double *)(r + off + (size_t)q2 * vpp * 8))[o] += d; for (int k2 = 0x68; k2 <= 0x80; k2 += 0x18) ((double *)(r + k2))[0] += d; } }
        ch[nch].c = (char)c; ch[nch].first = from; ch[nch].count = nlb - from; ch[nch++].w = w; sumw += w; nlet++;
    }
    let_miss_show();
    if (!nlet) { lb_clear(); return; }
    double avg = sumw / nlet;
    mod_begin();
    int ci = 0, ei = 0; double d = 0; int stop = 0;
    const Chain *cn = &geo_chains[0];
    for (int i = 0; i < nch; i++) {
        LetCh *c = &ch[i];
        if (c->c == 10) { ci++; if (ci >= geo_nchains) break; cn = &geo_chains[ci]; ei = 0; d = 0; stop = 0; continue; }   /* FUN_18006da70: the next chain */
        if (stop) continue;
        double step = c->c == ' ' ? d + avg + S : d + c->w * 0.5;
        Ent *en = &F.ents[cn->ent[ei]]; GE g = ent_ge(cn->ent[ei]); double len = ge_len(&g);
        if (step > len) { step -= len; if (step < 0) step = 0; ei++; if (ei >= cn->n) { stop = 1; d = step; continue; } en = &F.ents[cn->ent[ei]]; g = ent_ge(cn->ent[ei]); len = ge_len(&g); }   /* FUN_18006b540 */
        d = step;
        if (c->c == ' ') continue;
        double t = cn->rev[ei] ? 1 - step / len : step / len;
        P2 pxy = ge_at(&g, t); P2 tg = ge_dir(&g, t); V3 wp = { pxy.x, pxy.y, en->a.z };
        (void)en;
        double tv[3] = { wp.x * R[0] + wp.y * R[1] + wp.z * R[2], wp.x * R[3] + wp.y * R[4] + wp.z * R[5], 0 };
        if (!cfg_constr_3d) tv[2] = wm_cp_depth();       /* proj_pt_cp */
        double tx = tg.x * R[0] + tg.y * R[1], ty = tg.x * R[3] + tg.y * R[4], ang = 0;
        if (tx * tx + ty * ty >= 1e-12) { ang = atan2(ty, tx); if (ang < 0) ang += 2 * M_PI; }
        if (cn->rev[ei]) ang += M_PI;
        int nb = nlb; nlb = c->first + c->count; lb_flush(c->first, ang, tv); nlb = nb;
        d = c->w * 0.5 + step + S;
    }
    mod_end(); doc_dirty = 1; overlay_dirty = 1; lb_clear();
}
/* FUN_18006d450: an arc picked for the arc alignments: the radius, the side from the pick against the centre +- r/2, the text about its centre */
int wm_let_arcpick(int e, V3 w) {
    if (let.mode >= 6 || let.align < 2 || e < 0 || e >= F.nents || F.ents[e].type != E_ARC || F.ents[e].vw) return 0;
    Ent *a = &F.ents[e]; let.radius = a->r;
    if (w.y > a->c.y + a->r * 0.5) let.align = 2;
    if (w.y < a->c.y - a->r * 0.5) let.align = 3;
    V3 c = wm_proj_cp(a->c);                             /* proj_pt_cp */
    geo_let_point(c);                                    /* wm_let_exec(c) built live: ours, user decision LET-1 (cui_geo.c) */
    return 1;
}
int wm_let_wants_arc(void) { return let.mode < 6 && let.align >= 2 && !let.chain; }
int wm_let_chain(void) { return let.chain; }
int wm_let_take_cancelled(void) { int c = let_cancelled; let_cancelled = 0; return c; }
const char *wm_let_prompt(void) { return let.mode < 6 && let.align >= 2 ? "Enter coordinates of arc center" : "Enter starting location of letters"; }   /* 6697 / 6696 */
void wm_let_gpset(const char *k, double x, const char *sv) {
    if (!strcmp(k, "font")) let.mode = (int)x; else if (!strcmp(k, "align")) let.align = (int)x; else if (!strcmp(k, "height")) { let.height = x; if (!let.userspc) let.spacing = 0.2 * x; }
    else if (!strcmp(k, "radius")) let.radius = x; else if (!strcmp(k, "spacing")) { let.spacing = x; let.userspc = 1; } else if (!strcmp(k, "chain")) { let.chain = (int)x; geo_let_chain_changed(); }
    else if (!strcmp(k, "folder") && sv) snprintf(let.folder, sizeof let.folder, "%s", sv);
    else if (!strcmp(k, "text") && sv) snprintf(let.text, sizeof let.text, "%s", sv);
}
void wm_let_panel(const CuiPanelOps *ops, void *u) {
    if (ops->exp(u, "Font")) { int ix = let.mode - 1, ix0 = ix; ops->combo(u, let_fonts, 15, &ix, 0); if (ix != ix0) let.mode = ix + 1;
        if (ops->text) ops->text(u, "Font folder", "letfolder", let.folder, (int)sizeof let.folder, let.mode == 5);
        ops->toggle(u, "TrueType(R) ...", 1, -1);            /* 3726: NOT PORTED (decision N9-21) */
        if (let.mode == 15) ops->label(u, "TrueType: NOT PORTED (decision N9-21)"); ops->end(u); }
    if (ops->exp(u, "Letters")) { if (ops->text) ops->text(u, NULL, "lettext", let.text, (int)sizeof let.text, 1);
        ops->toggle(u, "Load File...", 1, -1); ops->toggle(u, "Add Symbol...", 1, -1);   /* CLetDlg 434: 1358 / 1352 NOT PORTED: greyed */
        ops->label(u, "Load File... / Add Symbol...: NOT PORTED"); ops->end(u); }
    int fl = let.mode < 6;                               /* FUN_180068100: the alignment radios for the Mastercam (and TrueType) fonts, without Top of Chain */
    if (ops->exp(u, "Alignment")) { ops->radio(u, "Horizontal", &let.align, 0, fl && !let.chain); ops->radio(u, "Vertical", &let.align, 1, fl && !let.chain); ops->radio(u, "Top of Arc", &let.align, 2, fl && !let.chain); ops->radio(u, "Bottom of Arc", &let.align, 3, fl && !let.chain); int c0 = let.chain; ops->check(u, "Top of Chain", &let.chain, 1); if (c0 != let.chain) { geo_panel_layout++; geo_let_chain_changed(); } ops->end(u); }
    double h0 = let.height, s0 = let.spacing;
    if (ops->exp(u, "Parameters")) { ops->num(u, "Height:", "height", &let.height, NULL, 1, 0, 1e6); ops->num(u, "Arc Radius:", "radius", &let.radius, NULL, fl && let.align > 1 && !let.chain, 0, 1e6); ops->num(u, "Spacing:", "spacing", &let.spacing, NULL, fl || let.chain, 0, 1e6);   /* FUN_180068100: 0x3ed8 (the MCReal whose value is +0x4ad8, the value at object + 0xc00) enabled by Top of Chain or a Mastercam font */
        ops->toggle(u, "Drafting Options...", 1, -1); ops->label(u, "Drafting Options...: NOT PORTED"); ops->end(u); }   /* 3763 (FUN_180068100: drafting fonts without chain) */
    if (fabs(h0 - let.height) >= mc_systol.tol) { let.userspc = 0; let.spacing = 0.2 * let.height; }   /* FUN_1800674b0 */
    else if (fabs(s0 - let.spacing) >= mc_systol.tol) let.userspc = 1;   /* the Spacing box's handler */
}

/* ------------------------------------------------------------------------------------------------ Line Normal (G_LNORM)
   sketcher_line_normal (mccad 0x18035a920) -> FUN_1803584a0 (WireframeLineNormalApp, panel IWireframeLineNormalManager FUN_18035ea60, reader FUN_180360010,
   writer FUN_180360620). State 0: get_event(mccadres 6706 "Select a surface or face", mask 0x1000200a, 0x100): an entity pick with the surface mask or a solid
   face (0x2000); FUN_18035a670 starts the location. State 1: get_event(6707, 0x10008109, 0): the mouse move (0x100) moves the arrow, a click (1) moves it and makes
   a line (FUN_18035a3b0, live), the next click commits it. Panel codes (disasm): 4 Length, 6 Direction, 8 points - the live line rebuilt (LV_REBUILD); 7 Reselect;
   1 Apply / Enter - the live line committed, back to state 0; 0 / 2 / Esc: the exit commits (977c, geo_end / geo_cancel with LV_LIVE). The panel reader's flag +0 = CurrentlyEditing
   (the Reselect toggle), +0x10 = the OffsetDirection, +4 = the two check boxes.

   The location: mcsurf_nrm.c mcsn_track_t = FUN_1801c90d0's surface branch; `shown` = the arrow exists (+0x29e0): the first event after the pick only evaluates the pick's (u, v).
   The arrow: ours draws a segment ext * 0.15 long from S along N (GrTemp_AddArrow(S, N, 0, 0.15, -2): the drawing is gafgr's, NOT DECODED beyond the arguments).
   The state machine r13b / cVar6 of the mouse move (the position taken from the first event when the cursor leaves the surface and comes back) is not ported: preview only. */
static struct {
    int active, e, nsel, resel, has_trim, apply;   /* e: the surface's Ent or -1 for a solid face; apply: vf+0x30 EnableApply */
    double mS[3], mN[3];                    /* the point and normal of the line made (the location of the event that made it): the panel-code rebuilds use these (disassembly 180358e39-180358ec1: local_5e48 / local_5ef0), not the moving arrow */
    const Surf *s; Surf temp, trim;
    uint8_t *rec_temp, *rec_trim;
    McsnTrack tk;
} ln;
static void ln_free(void) { free(ln.rec_temp); free(ln.rec_trim); ln.rec_temp = NULL; ln.rec_trim = NULL; }
void wm_ln_begin(void) { ln_free(); memset(&ln, 0, sizeof ln); ln.e = -1; srf_pick_active = 1; }
void wm_ln_end(void) { ln_free(); memset(&ln, 0, sizeof ln); ln.e = -1; srf_pick_active = 0; }
int wm_ln_active(void) { return ln.active; }
int wm_ln_enable_reselect(void) { return ln.nsel != 0; }
int wm_ln_enable_apply(void) { return ln.apply; }
static void ln_cv(double V[9]) { if (!mc_view_rows(plane_active[0], V)) { static const double I[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }; memcpy(V, I, sizeof I); } }   /* MastercamPlanes::GetActiveViewNo(0): the Cplane */
/* (ours) the DLL's entity pick hands the (u, v) of the picked location (find_an_entity, the event's +0x10 / +0x18, FUN_18035a670 l.7367-7368); here: the 40 x 40 grid point nearest the
   click in the view plane, then the Newton step along the graphics view (project_pt_onto_surf's iteration); a click off the surface: the grid point */
static void ln_seed(const Surf *s, const double P[3], double uv[2]) {
    double d[4], best = 1e300, bu = 0, bv = 0; mcs_surf_domain(s, d);
    double vx = vmat[0] * P[0] + vmat[1] * P[1] + vmat[2] * P[2], vy = vmat[3] * P[0] + vmat[4] * P[1] + vmat[5] * P[2];
    for (int i = 0; i <= 40; i++) for (int j = 0; j <= 40; j++) {
        double u = d[0] + (d[1] - d[0]) * i / 40.0, v = d[2] + (d[3] - d[2]) * j / 40.0, q[3];
        if (mcs_surf_eval(&F, s, u, v, q)) continue;
        double dx = vmat[0] * q[0] + vmat[1] * q[1] + vmat[2] * q[2] - vx, dy = vmat[3] * q[0] + vmat[4] * q[1] + vmat[5] * q[2] - vy, dd = dx * dx + dy * dy;
        dd += 1e-6 * ((q[0] - P[0]) * (q[0] - P[0]) + (q[1] - P[1]) * (q[1] - P[1]) + (q[2] - P[2]) * (q[2] - P[2]));   /* a tie between the points of one view-plane position: the nearest in 3D */
        if (dd < best) { best = dd; bu = u; bv = v; }
    }
    double uv0[2] = { bu, bv }, nw[2], Sp[3];
    if (mcsn_project(&F, s, vmat, P, uv0, nw, Sp)) { uv[0] = nw[0]; uv[1] = nw[1]; } else { uv[0] = bu; uv[1] = bv; }
}
static void ln_prompt(void) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); overlay_dirty = 1; }
static int ln_kind_ok(const Surf *s) {
    if (s->kind == 10) { const Surf *b = s->n >= 0x108 ? mcs_surf(&F, *(const int64_t *)(s->rec + 0x100)) : NULL; if (!b || b == s) return 0; return ln_kind_ok(b); }
    return s->kind != 4;
}
/* FUN_18035a670 for a surface: the (u, v) of the event, +0x29a0 = it, no arrow yet */
static void ln_start(const Surf *s, const double P[3], int e) {
    ln.s = s; ln.e = e; ln.has_trim = 0; memset(&ln.tk, 0, sizeof ln.tk);
    ln_seed(s, P, ln.tk.uv);
    ln.active = 1; srf_pick_active = 0;                  /* the surface is taken: the next events are the location's */
    geo_ent[0] = e; geo_nent = 1; geo_npick = 0;
    ln.nsel = 0;
    ln_prompt();
}
/* FUN_18035a670 for a solid face (0x2000): sld_face_to_temp_file(solid, face, 0, 1, 0) (mcgeomsld 0x1800ee210) -> read_next_solid_surf_from_temp_file: the face as a surface, here OCCT's
   (occt_xt_faces / occt_brep_faces = BRepBuilderAPI_NurbsConvert, decision N9-23: OCCT stands in for Parasolid) as the record ps_face_to_mc_surf makes (mcsb_prim_nurbs + loops,
   srf_faces_make): a NURBS and, when the loops do not fill it, the trim polygon apart. (u, v) = the middle of the domain (+0x90 / +0x98, +0xa0 / +0xa8, l.7317-7322). */
static void ln_start_face(int si, int fi) {
    if (si < 0 || si >= F.nsolids) { snprintf(cam_msg, sizeof cam_msg, "Line Normal: no such solid"); return; }
    double scale = !mcam_is_metric(&F) ? 1000.0 / 25.4 : 1000.0;   /* XT metres (srf_solid_exec) */
    OpFace *fs; int nf; char err[200];
    if (F.solids[si].brep ? occt_brep_faces(F.solids[si].brep, F.solids[si].brep_n, &fs, &nf, err, sizeof err) : occt_xt_faces(F.solids[si].xt, F.solids[si].xt_size, scale, &fs, &nf, err, sizeof err)) { snprintf(cam_msg, sizeof cam_msg, "Line Normal: %s", err); return; }
    if (fi < 0 || fi >= nf) { occt_prim_free(fs, nf); snprintf(cam_msg, sizeof cam_msg, "Line Normal: no such face"); return; }
    double tol = mc_systol_units(mcam_is_metric(&F))->tol;
    uint32_t n, tn = 0; uint8_t *b = mcsb_prim_nurbs(&F, &fs[fi], 0, tol, &n);
    if (!b) { occt_prim_free(fs, nf); snprintf(cam_msg, sizeof cam_msg, "Line Normal: the face has no surface here"); return; }
    double **P = NULL; int *c = NULL; int nl = mcsb_prim_loops(&F, b, n, &fs[fi], &P, &c);
    uint8_t *t = NULL;
    if (nl > 0 && !mcsb_prim_untrimmed(&F, b, n, nl, P, c)) t = mcsb_prim_trim(&F, 0, b, n, nl, P, c, 0, tol, &tn);
    for (int l = 0; l < (nl > 0 ? nl : 0); l++) free(P[l]); free(P); free(c);
    occt_prim_free(fs, nf);
    ln_free();
    ln.rec_temp = b; ln.rec_trim = t;
    memset(&ln.temp, 0, sizeof ln.temp); ln.temp.rec = b; ln.temp.n = n; ln.temp.kind = b[0x68];
    ln.has_trim = t != NULL;
    if (t) { memset(&ln.trim, 0, sizeof ln.trim); ln.trim.rec = t; ln.trim.n = tn; ln.trim.kind = t[0x68]; }
    ln.s = &ln.temp; ln.e = -1; memset(&ln.tk, 0, sizeof ln.tk);
    double d[4]; mcs_surf_domain(&ln.temp, d);
    ln.tk.uv[0] = (d[1] + d[0]) * 0.5; ln.tk.uv[1] = (d[3] + d[2]) * 0.5;   /* l.7317-7322 */
    ln.active = 1; srf_pick_active = 0; geo_nent = 1; geo_ent[0] = -1; geo_npick = 0; ln.nsel = 0;
    ln_prompt();
}
/* FUN_1801c90d0 for the location: world point P (proj: its projection along the Cplane Z, else the view-plane step) */
static int ln_track(int proj, const double P[3]) {
    double CV[9], vxy[2] = { vmat[0] * P[0] + vmat[1] * P[1] + vmat[2] * P[2], vmat[3] * P[0] + vmat[4] * P[1] + vmat[5] * P[2] }; ln_cv(CV);
    if (!ln.tk.shown) ln.tk.last[0] = vxy[0] + 1;       /* ours: the first event after the pick evaluates (u, v) even at the view origin (+0x29d0 is 0 there; FUN_1801c90d0 l.3054 returns early and the location stays unset) */
    char msg[200] = "";
    int e = mcsn_track_t(&F, ln.s, ln.has_trim ? &ln.trim : NULL, &ln.tk, vmat, CV, proj, P, vxy, msg, sizeof msg);
    if (e) { if (e == 1) snprintf(cam_msg, sizeof cam_msg, "Line Normal: %s", msg); return e; }
    ln.tk.shown = 1; overlay_dirty = 1;
    return 0;
}
void wm_ln_take(int e, V3 w) {
    double P[3] = { w.x, w.y, w.z };
    if (!ln.active) {
        if (e < 0 || e >= F.nents || F.ents[e].type != E_SURF) { snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt()); return; }   /* the mask */
        const Surf *s = &F.surfs[F.ents[e].srf];
        if (!ln_kind_ok(s)) { snprintf(cam_msg, sizeof cam_msg, "Line Normal: a lofted surface (kind 4): the derivatives (FUN_18042cad0) are NOT DECODED"); return; }
        ln_start(s, P, e);
        return;
    }
    if (ln_track(1, P)) return;
    memcpy(ln.mS, ln.tk.S, sizeof ln.mS); memcpy(ln.mN, ln.tk.N, sizeof ln.mN);
    int ents0 = geo_ent[0];
    geo_nent = 0; geo_pick[0] = w; geo_npick = 1;        /* geo_steps' one 'p': the rebuild of LV_REBUILD (panel codes 4, 6, 8) runs from the same record */
    geo_run();                                           /* the previous live line committed (geo_run), UndoTransStart, FUN_18035a3b0 */
    ln.nsel = 1; ln.resel = 0; ln.apply = 1;             /* NumberSelected 1 (:33917), EnableApply(1); FUN_180358440 writes the Reselect toggle back off */
    geo_npick = 0; geo_nent = 1; geo_ent[0] = ents0;     /* the surface stays picked: more locations */
}
void wm_ln_click_pick(float mx, float my, V3 w) {
    double P[3] = { w.x, w.y, w.z };
    int e = pick_entity(mx, my);
    if (e >= 0 && F.ents[e].type == E_SURF) { wm_ln_take(e, w); return; }
    int si, fi; double hp[3];
    int k = sld_pick_part_face(mx, my, &si, &fi, hp);
    if (k == 1) { ln_start_face(si, fi); return; }       /* surface first: the order of the two NOT DECODED (get_event's 0x100 / 0x2000) */
    if (k == 2) { snprintf(cam_msg, sizeof cam_msg, "Line Normal: a solid made in this session: its faces are NOT PORTED (only the part's solids)"); return; }
    (void)P; snprintf(cam_msg, sizeof cam_msg, "%s", geo_prompt());
}
void wm_ln_hover(V3 w, int snapped) { if (ln.active) { double P[3] = { w.x, w.y, w.z }; ln_track(snapped ? 1 : 0, P); } }   /* fast_data[9] */
/* FUN_18035a3b0: L = the panel Length (+8), v = N L: |L| <= tol: nothing; contact point (+4 < 2) at P; the Normal side (+0x10 & ~2 == 0): end point (+4 - 1 < 2) at P + v, the line P -> P + v;
   the Reverse side (+0x10 - 1 < 2): end point at P - v, the line P -> P - v. P = proj_pt_cp(S) (FUN_1801c90d0 l.3191). */
void wm_ln_exec(void) {
    GeoParams *g = &gps[G_LNORM];
    double L = g->v_len, tol = mc_systol.tol;
    if (fabs(L) <= tol) return;
    V3 z0 = { 0, 0, 0 }, P = wm_proj_cp((V3){ ln.mS[0], ln.mS[1], ln.mS[2] });   /* proj_pt_cp (FUN_1801c90d0 l.3191) */
    double vx = ln.mN[0] * L, vy = ln.mN[1] * L, vz = ln.mN[2] * L;
    V3 Ea = { P.x + vx, P.y + vy, P.z + vz }, Eb = { P.x - vx, P.y - vy, P.z - vz };
    if (g->pts) geo_add(E_POINT, P, P, z0, 0, 0, 0);
    if ((g->dir & ~2) == 0) { if (g->ctr) geo_add(E_POINT, Ea, Ea, z0, 0, 0, 0); geo_line(P, Ea); }
    if (g->dir == 1 || g->dir == 2) { if (g->ctr) geo_add(E_POINT, Eb, Eb, z0, 0, 0, 0); geo_line(P, Eb); }
    overlay_dirty = 1;
}
void wm_ln_apply(void) {                                 /* code 1 (disasm 180358fbb-180359012, 180359688-180359750) */
    if (ln.resel) return;
    if (!ln.active || !geo_live) return;                 /* cmp [rsp+0x50],0: no line made: nothing */
    geo_live_commit(); ln_free(); ln.active = 0; ln.nsel = 0; ln.has_trim = 0; ln.apply = 0;   /* NumberSelected 0, EnableApply(0) */
    srf_pick_active = 1; geo_reset_picks();
    memset(&ln.tk, 0, sizeof ln.tk); ln_prompt();
}
void wm_ln_reselect(void) {                              /* code 7 (disasm 180358d70-180358db0): reselect armed and a live line: UndoTransTerminate */
    ln.resel = !ln.resel;
    if (ln.resel && geo_live) { geo_live_drop(); ln.apply = 0; }   /* UndoTransTerminate, EnableApply(0) (disassembly 180358d8a-180358dad) */
    ln_prompt();
}
void wm_ln_ov(float **pv, int *pn, int *pcap) {
    if (!ln.active || !ln.tk.shown) return;
    V3 a = { ln.tk.S[0], ln.tk.S[1], ln.tk.S[2] }, b = { ln.tk.S[0] + ln.tk.N[0] * ext * 0.15, ln.tk.S[1] + ln.tk.N[1] * ext * 0.15, ln.tk.S[2] + ln.tk.N[2] * ext * 0.15 };
    ov_pt(pv, pn, pcap, a, .9f, .3f, 0); ov_pt(pv, pn, pcap, b, .9f, .3f, 0);
}
/* tests (ours): lnoracle:PREFIX: for every kind 0 / 1 / 10-on-NURBS surface of the part (at most 40) writes PREFIX.in (the record and cases for `peloader mccore.dll --lnorm`) and PREFIX.port (what mcsurf_nrm.c gives for
   them: eval_surf order 3, calc_surf_unit_norm, project_pt_onto_surf in view 1 Top); tools/lnorm_cmp.py compares the two */
static void ln_oracle(const char *pre) {
    char nm[512]; snprintf(nm, sizeof nm, "%s.in", pre); FILE *in = fopen(nm, "w"); snprintf(nm, sizeof nm, "%s.port", pre); FILE *po = fopen(nm, "w"); if (!in || !po) return;
    fprintf(in, "TOL %.17g\n", mc_systol.tol);
    double V[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    int ns = 0;
    for (int i = 0; i < F.nsurfs && ns < 40; i++) {
        const Surf *s = &F.surfs[i]; const Surf *b = s;
        if (s->kind == 10) { b = s->n >= 0x108 ? mcs_surf(&F, *(const int64_t *)(s->rec + 0x100)) : NULL; if (!b || b == s) continue; }
        if (b->kind != 0 && b->kind != 1) continue;
        fprintf(in, "S %d %d %u ", ns, b->kind, b->n); for (uint32_t k = 0; k < b->n; k++) fprintf(in, "%02x", b->rec[k]); fprintf(in, "\n");
        double d[4]; mcs_surf_domain(b, d);
        for (int a = 0; a < 5; a++) for (int c = 0; c < 5; c++) {
            double u = d[0] + (d[1] - d[0]) * a / 4.0, v = d[2] + (d[3] - d[2]) * c / 4.0, D[10][3];
            fprintf(in, "E %d %.17g %.17g\n", ns, u, v);
            fprintf(po, "E %d %.17g %.17g", ns, u, v); int e = mcsn_eval(&F, b, 3, u, v, D);
            fprintf(po, " %d", e); for (int m = 0; m < 10; m++) for (int k = 0; k < 3; k++) fprintf(po, " %.15g", e ? 0.0 : D[m][k]); fprintf(po, "\n");
            double N[3] = { 0, 0, 0 }; int r = e ? 9 : mcsn_unit_norm(&F, b, D[1], D[2], u, v, N);
            fprintf(in, "N %d %.17g %.17g\n", ns, u, v);
            fprintf(po, "N %d %.17g %.17g %d %.15g %.15g %.15g\n", ns, u, v, r, N[0], N[1], N[2]);
            if (a && c && a < 4 && c < 4 && !e) {            /* the projection: from a point above a moved location, started at (u, v), along Z (view 1) */
                double u1 = u + (d[1] - d[0]) * 0.07, v1 = v - (d[3] - d[2]) * 0.05, P[10][3];
                if (!mcsn_eval(&F, b, 1, u1, v1, P)) {
                    double Pp[3] = { P[0][0], P[0][1], P[0][2] + 0.7 }, uv0[2] = { u, v }, uv[2], Sp[3] = { 0, 0, 0 };
                    fprintf(in, "P %d 1 %.17g %.17g %.17g %.17g %.17g\n", ns, Pp[0], Pp[1], Pp[2], u, v);
                    int got = mcsn_project(&F, b, V, Pp, uv0, uv, Sp);
                    fprintf(po, "P %d 1 %.17g %.17g %.17g %.17g %.17g %d %.15g %.15g %.15g %.15g %.15g\n", ns, Pp[0], Pp[1], Pp[2], u, v, got, got ? uv[0] : 0, got ? uv[1] : 0, got ? Sp[0] : 0, got ? Sp[1] : 0, got ? Sp[2] : 0);
                }
            }
        }
        ns++;
    }
    fclose(in); fclose(po); printf("    lnoracle %d surfaces\n", ns);
}
/* tests (ours): lnfd: the analytic Su, Sv of the kinds 2 / 6 / 7 / 8 (and 10 on them) against central differences of the positions, 3 x 3 interior points, per kind the worst |Su - fd| / max(|Su|, 1e-9): printed "rg lnfd kind K surfaces N max %g" */
static void ln_fd(void) {
    double worst[11] = { 0 }; int cnt[11] = { 0 };
    for (int i = 0; i < F.nsurfs; i++) {
        const Surf *s = &F.surfs[i]; const Surf *b = s;
        if (s->kind == 10) { b = s->n >= 0x108 ? mcs_surf(&F, *(const int64_t *)(s->rec + 0x100)) : NULL; if (!b || b == s) continue; }
        if (b->kind != 2 && b->kind != 6 && b->kind != 7 && b->kind != 8) continue;
        cnt[b->kind]++;
        double d[4]; mcs_surf_domain(b, d);
        for (int a = 1; a <= 3; a++) for (int c = 1; c <= 3; c++) {
            double u = d[0] + (d[1] - d[0]) * a / 4.0, v = d[2] + (d[3] - d[2]) * c / 4.0, D[10][3], p1[3], p0[3], h = 1e-6;
            if (mcsn_eval(&F, b, 1, u, v, D)) continue;
            double hu = (d[1] - d[0]) * h, hv = (d[3] - d[2]) * h; double e = 0;
            if (mcs_surf_eval(&F, b, u + hu, v, p1) == 0 && mcs_surf_eval(&F, b, u - hu, v, p0) == 0) { double n2 = 0, ex2 = 0; for (int k = 0; k < 3; k++) { double ex = D[1][k] - (p1[k] - p0[k]) / (2 * hu); ex2 += ex * ex; n2 += D[1][k] * D[1][k]; } double m = sqrt(n2) > 1e-9 ? sqrt(n2) : 1e-9, ee = sqrt(ex2) / m; if (ee > e) e = ee; }
            if (mcs_surf_eval(&F, b, u, v + hv, p1) == 0 && mcs_surf_eval(&F, b, u, v - hv, p0) == 0) { double n2 = 0, ex2 = 0; for (int k = 0; k < 3; k++) { double ex = D[2][k] - (p1[k] - p0[k]) / (2 * hv); ex2 += ex * ex; n2 += D[2][k] * D[2][k]; } double m = sqrt(n2) > 1e-9 ? sqrt(n2) : 1e-9, ee = sqrt(ex2) / m; if (ee > e) e = ee; }
            if (e > worst[b->kind]) worst[b->kind] = e;
        }
    }
    for (int k = 0; k <= 10; k++) if (cnt[k]) printf("    rg lnfd kind %d surfaces %d max %.3g\n", k, cnt[k], worst[k]);
}
/* tests (ours): lnfacechk:SOLID: through the real pick path (sld_pick_part_face at the screen position of each mesh face's largest triangle): each face the pick returns is compared with the face of that index from
   occt_xt_faces / occt_brep_faces (ln_start_face's list): the picked point's distance to it (nearest of a 41 x 41 grid of the NURBS) must be the smallest over all the faces. Prints "rg lnfacechk faces n picked p other o same s best b worst w" */
static double ln_facedist(const Surf *s, const double p[3]) {   /* a 41 x 41 grid, then 5 finer grids around the best (the point's distance from the untrimmed surface, to the grid's resolution) */
    double d[4], best = 1e300; mcs_surf_domain(s, d); double u0 = d[0], u1 = d[1], v0 = d[2], v1 = d[3], bu = u0, bv = v0;
    for (int pass = 0; pass < 6; pass++) {
        for (int i = 0; i <= 40; i++) for (int j = 0; j <= 40; j++) { double q[3], u = u0 + (u1 - u0) * i / 40.0, v = v0 + (v1 - v0) * j / 40.0; if (mcs_surf_eval(&F, s, u, v, q)) continue;
            double e = hypot(hypot(q[0] - p[0], q[1] - p[1]), q[2] - p[2]); if (e < best) { best = e; bu = u; bv = v; } }
        double du = (u1 - u0) / 20.0, dv = (v1 - v0) / 20.0; u0 = bu - du; u1 = bu + du; v0 = bv - dv; v1 = bv + dv;
        if (u0 < d[0]) u0 = d[0]; if (u1 > d[1]) u1 = d[1]; if (v0 < d[2]) v0 = d[2]; if (v1 > d[3]) v1 = d[3];
    }
    return best;
}
static void ln_facechk(int si) {
    if (si < 0 || si >= F.nsolids) return;
    double scale = !mcam_is_metric(&F) ? 1000.0 / 25.4 : 1000.0; OpFace *fs; int nf; char err[200];
    if (F.solids[si].brep ? occt_brep_faces(F.solids[si].brep, F.solids[si].brep_n, &fs, &nf, err, sizeof err) : occt_xt_faces(F.solids[si].xt, F.solids[si].xt_size, scale, &fs, &nf, err, sizeof err)) { printf("    rg lnfacechk error %s\n", err); return; }
    int nt = sld_part_nface(si), picked = 0, other = 0, same = 0, best_ok = 0, nosurf = 0; double worst = 0; double tol = mc_systol_units(mcam_is_metric(&F))->tol;
    Surf *S = calloc((size_t)nf, sizeof(Surf)); uint8_t **B = calloc((size_t)nf, sizeof(uint8_t *));
    for (int i = 0; i < nf; i++) { uint32_t n; B[i] = mcsb_prim_nurbs(&F, &fs[i], 0, tol, &n); if (B[i]) { S[i].rec = B[i]; S[i].n = n; S[i].kind = B[i][0x68]; } else nosurf++; }
    for (int f = 0; f < nt; f++) {
        float c[3], sx, sy; if (!sld_part_face_pt(si, f, c)) continue;
        if (!scene_to_screen(vp_w, vp_h, c[0], c[1], c[2], &sx, &sy)) continue;
        int s2 = -1, f2 = -1; double hp[3]; if (sld_pick_part_face(vp_x + sx, vp_y + sy, &s2, &f2, hp) != 1 || s2 != si) continue;
        picked++; if (f2 != f) other++; else same++;
        if (f2 < 0 || f2 >= nf || !B[f2]) continue;
        double df = ln_facedist(&S[f2], hp), mn = 1e300; for (int i = 0; i < nf; i++) if (B[i]) { double e = ln_facedist(&S[i], hp); if (e < mn) mn = e; }
        if (df <= mn + 1e-4) best_ok++;
        if (df > worst) worst = df;
    }
    printf("    rg lnfacechk occt faces %d mesh faces %d no-surface %d picked %d same %d other %d nearest-is-picked %d worst-dist-to-picked-face %.6f\n", nf, nt, nosurf, picked, same, other, best_ok, worst);
    for (int i = 0; i < nf; i++) free(B[i]); free(B); free(S); occt_prim_free(fs, nf);
}
static void ln_dump(void) {
    printf("    rg ln active %d nsel %d resel %d uv (%.9f, %.9f) S (%.9f, %.9f, %.9f) N (%.9f, %.9f, %.9f)\n", ln.active, ln.nsel, ln.resel, ln.tk.uv[0], ln.tk.uv[1], ln.tk.S[0], ln.tk.S[1], ln.tk.S[2], ln.tk.N[0], ln.tk.N[1], ln.tk.N[2]);
}

/* tests (ours): wmviewarc:VIEW,BASE,CX,CY,CZ,R,A0,SW - a stored view number VIEW with the rows of view BASE (added to the part's views), and an
   arc in it (centre in view coordinates, degrees): Combine Views needs a view the part holds twice */
int wm_script(const char *st, const char *arg) {
    if (!strcmp(st, "rgfil") && arg) { FILE *f = fopen(arg, "r"), *o = fopen("rgfil.out", "w"); double v[9]; if (f && o) while (fscanf(f, "%lf %lf %lf %lf %lf %lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) == 9) { RgEl a = { 0 }, b = { 0 }, c = { 0 }; memcpy(a.v, v, 32); memcpy(b.v, v + 4, 32); double saved[4]; memcpy(saved, b.v, 32); int ok = rg_fillet(&a, &b, v[8], &c); fprintf(o, "%.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %.12g %d\n", a.v[0], a.v[1], a.v[2], a.v[3], b.v[0], b.v[1], b.v[2], b.v[3], c.v[0], c.v[1], c.v[2], c.v[3], c.v[4], ok); } if (f) fclose(f); if (o) fclose(o); return 1; }   /* tests: compared with fillet_line_line_2d (tools/peloader mcfil.c) */
    if (!strcmp(st, "letdump")) { printf("    rg let mode %d align %d chain %d user %d h %.6f r %.6f s %.6f\n", let.mode, let.align, let.chain, let.userspc, let.height, let.radius, let.spacing); return 1; }
    if (!strcmp(st, "letreload")) { let_units = -1; return 1; }   /* the defaults, then the .PRM at the next start */
    if (!strcmp(st, "wmrec3c")) { int n3 = 0, nf = 0, nb = 0; for (int i = 0; i < F.nents; i++) { Ent *e = &F.ents[i]; if (e->type == E_LINE || e->type == E_ARC || e->type == E_SPLINE) { if (e->rec3c > 0) n3++; if (e->rec3c > 0 && !(e->rec_sel & 1)) nf++; if (e->type == E_SPLINE && e->cls == 0x80) nb++; } } printf("    rg rec3c %d filtered %d nurbs %d\n", n3, nf, nb); return 1; }
    if (!strcmp(st, "lndump")) { ln_dump(); return 1; }
    if (!strcmp(st, "lnfacechk") && arg) { ln_facechk(atoi(arg)); return 1; }
    if (!strcmp(st, "lnclick") && arg) { double x = 0, y = 0, z = 0; sscanf(arg, "%lf,%lf,%lf", &x, &y, &z); float sx, sy; if (scene_to_screen(vp_w, vp_h, x, y, z, &sx, &sy)) { V3 q = { x, y, z }; wm_ln_click_pick(vp_x + sx, vp_y + sy, q); } return 1; }   /* tests: the click of geo_click for a location not yet active (the real entity / solid-face pick) */
    if (!strcmp(st, "lnapply")) { printf("    rg lnapply %d reselect %d\n", ln.apply, ln.nsel != 0); return 1; }
    if (!strcmp(st, "letmiss")) { printf("    rg letmiss '%s'\n", let_miss_last); return 1; }
    if (!strcmp(st, "letsp")) { printf("    rg letsp spacing-enabled %d radius-enabled %d\n", (let.mode < 6) || let.chain, let.mode < 6 && let.align > 1 && !let.chain); return 1; }
    if (!strcmp(st, "wmblank") && arg) { int e = atoi(arg); if (e >= 0 && e < F.nents) { mcam_ent_blank(&F, e, 1); F.ents[e].blank_edited = 1; } return 1; }
    if (!strcmp(st, "rgtblf") && arg) { snprintf(rg_tbl_test, sizeof rg_tbl_test, "%s", arg); rg_tbl_shape = -1; return 1; }
    /* tests (ours): wmuplane:TYPE,ANGDEG,OX,OY,OZ - a named plane rotated about X with that origin (planes_add) made the active plane of PlaneType TYPE */
    if (!strcmp(st, "wmuplane") && arg) { int ty = 0; double an = 0, og[3] = { 0, 0, 0 }; if (sscanf(arg, "%d,%lf,%lf,%lf,%lf", &ty, &an, &og[0], &og[1], &og[2]) == 5) { double c = cos(an * M_PI / 180), s = sin(an * M_PI / 180), m[9] = { 1, 0, 0, 0, c, s, 0, -s, c }; int id = planes_add("wmtest", m, og); if (id > 0 && ty >= 0 && ty < 4) plane_set_active(ty, id); printf("    rg wmuplane %d\n", id); } return 1; }
    if (!strcmp(st, "lnfd")) { ln_fd(); return 1; }
    if (!strcmp(st, "lnoracle") && arg) { ln_oracle(arg); return 1; }
    if (!strcmp(st, "lnface") && arg) { int a = 0, b = 0; sscanf(arg, "%d,%d", &a, &b); if (geo_cmd == G_LNORM) ln_start_face(a, b); return 1; }   /* tests: a solid face (solid, face), as the 0x2000 pick */
    if (!strcmp(st, "lnhover") && arg) { double x = 0, y = 0, z = 0; int sn = 0; sscanf(arg, "%lf,%lf,%lf,%d", &x, &y, &z, &sn); wm_ln_hover((V3){ x, y, z }, sn); ln_dump(); return 1; }   /* tests: the mouse move (the view-plane step, or snapped: the projection) */
    if (!strcmp(st, "lnproj") && arg) { /* tests: lnproj:SURFENT,U0,V0,X,Y,Z - project_pt_onto_surf along the Cplane: "rg lnproj found u v Sx Sy Sz" */
        int e; double u0, v0, x, y, z; if (sscanf(arg, "%d,%lf,%lf,%lf,%lf,%lf", &e, &u0, &v0, &x, &y, &z) == 6 && e >= 0 && e < F.nents && F.ents[e].type == E_SURF) {
            double CV[9], P[3] = { x, y, z }, uv0[2] = { u0, v0 }, uv[2], Sp[3]; ln_cv(CV);
            int got = mcsn_project(&F, &F.surfs[F.ents[e].srf], CV, P, uv0, uv, Sp);
            printf("    rg lnproj %d %.9f %.9f %.9f %.9f %.9f\n", got, got ? uv[0] : 0, got ? uv[1] : 0, got ? Sp[0] : 0, got ? Sp[1] : 0, got ? Sp[2] : 0);
        } return 1; }
    if (!strcmp(st, "wmenter")) { wm_rg_enter(); return 1; }
    if (!strcmp(st, "wmplane") && arg) { plane_set_active(0, atoi(arg)); return 1; }   /* tests: the active Cplane (SetActivePlane: and its origin) */
    if (!strcmp(st, "wmwcs") && arg) { plane_set_active(3, atoi(arg)); return 1; }   /* tests: the active WCS (SetActivePlane: and its origin) */
    if (!strcmp(st, "rgdump")) { printf("    rg shape %d in %d side %d shoulder %d trim %d zonly %d prevsel %d z %.4f d", rg.shape, rg.in, rg.side, rg.shoulder, rg.trim, rg.zonly, rg.prevsel, rg.z); for (int i = 0; i < 5; i++) printf(" %.5f", rg.dim[rg.shape][i]); printf(" tbl %d\n", rg_ntbl); return 1; }
    if (!strcmp(st, "rgtbl") && arg) { rg_load_table(); int k = atoi(arg); if (k >= 0 && k < rg_ntbl) { memcpy(rg.dim[rg.shape], rg_tbl[k].dim, 5 * sizeof(double)); printf("    rgtbl %s\n", rg_tbl[k].name); } return 1; }
    if (strcmp(st, "wmviewarc") || !arg) return 0;
    int v = 0, base = 0; double c[3], r, a0, sw;
    if (sscanf(arg, "%d,%d,%lf,%lf,%lf,%lf,%lf,%lf", &v, &base, &c[0], &c[1], &c[2], &r, &a0, &sw) != 8) return 1;
    double R[9]; if (!mcam_std_view(base, R)) return 1;
    int have = 0; for (int i = 0; i < F.nfviews; i++) if (F.fviews[i].n == v) have = 1;
    if (!have) { F.fviews = realloc(F.fviews, sizeof(F.fviews[0]) * (size_t)(F.nfviews + 1)); F.fviews[F.nfviews].n = v; memcpy(F.fviews[F.nfviews].m, R, sizeof R); F.nfviews++; }
    V3 cv = { c[0], c[1], c[2] }; mcam_new_arc_view(&F, v, R, cv, r, a0 * M_PI / 180, sw * M_PI / 180, geo_level, geo_color);
    overlay_dirty = 1; scene_reupload();
    return 1;
}
