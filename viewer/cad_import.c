/* cad_import.c - File > Open / import of DXF, STEP and IGES files (formats/cad_import.md). Decoded from Mastercam's converters:
 *   routing: mccore FUN_180345830 (DoFileOpen -> FUN_1803463b0, the DoImport export of the converter), the file-type table FUN_180358740;
 *   DXF: mcautocad.dll DoImport -> FUN_18003e660 -> FUN_18003e260 -> FUN_18007d090 -> DbDumper FUN_1800184f0 (block table: model space, layouts)
 *        -> per entity FUN_180085e50 (the Teigha class switch); layers FUN_18000a210 -> FUN_18003c130; every builder cited below;
 *   STEP: mcstep.dll DoImport -> FUN_18002d480; solids sld_import_solid (mcgeomsld), nameSolidsAndLevels, wireframe FUN_180029470;
 *   IGES: mciges.dll DoImport (no solids path: mciges imports no sld_import_solid);
 *   STL: mcstl.dll DoImport 0x180004910 -> FUN_1800040e0: an STL mesh (mccore StoreSTLFileAsSTLMesh, viewer/stl_mesh.c) or, with
 *        convSTLImportLines, lines (mccore rd_stl 0x1806d15f0, below).
 * OCCT parses the STEP / IGES files (user decision, formats/cad_import.md); viewer/dxf_read.c parses the public DXF format.
 * What is not decoded or not ported is reported to the user and left out, never approximated. */
#include "cad_import.h"
#include "dxf_read.h"
#include "occt_cad.h"
#include "stl_mesh.h"
#include "mcsurf.h"
#include "mcsurf_build.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG (180.0 / M_PI)

#include "aci_palette.inc"

/* the palette and the background (core_ui / scene.c) */
extern int cfg_back_color, cfg_pt_style;
#define cad_pt_style_main cfg_pt_style                                /* gr_back_color (core_ui) */
int mc_rgb(int idx, float *rgb);

const char *cad_kind_name(int k) { return k == CAD_DXF ? "DXF" : k == CAD_STEP ? "STEP" : k == CAD_IGES ? "IGES" : k == CAD_DWG ? "DWG" : k == CAD_STL ? "STL" : ""; }
int cad_import_kind(const char *p) {
    const char *d = strrchr(p, '.'); if (!d) return 0;
    if (!strcasecmp(d, ".dxf")) return CAD_DXF;
    if (!strcasecmp(d, ".stp") || !strcasecmp(d, ".step")) return CAD_STEP;
    if (!strcasecmp(d, ".igs") || !strcasecmp(d, ".iges")) return CAD_IGES;
    if (!strcasecmp(d, ".dwg") || !strcasecmp(d, ".dwf") || !strcasecmp(d, ".dwfx")) return CAD_DWG;
    if (!strcasecmp(d, ".stl")) return CAD_STL;                  /* FUN_180358740's McStl entry: mccoreres 2746 "StereoLithography Files" (.stl) */
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the report */
#define MAXSKIP 64
typedef struct { const char *what; int n; } Skip;
static struct { int n_pts, n_lines, n_arcs, n_splines, n_surfs, n_solids, n_meshes, n_tris; Skip sk[MAXSKIP]; int nsk; } R;
static void rp_reset(void) { memset(&R, 0, sizeof R); }
static void rp_skip(const char *what) {
    for (int i = 0; i < R.nsk; i++) if (!strcmp(R.sk[i].what, what)) { R.sk[i].n++; return; }
    if (R.nsk < MAXSKIP) { R.sk[R.nsk].what = what; R.sk[R.nsk].n = 1; R.nsk++; }
}
static void rp_message(char *msg, size_t n) {
    int k = snprintf(msg, n, "%d points, %d lines, %d arcs, %d splines, %d surfaces, %d solids", R.n_pts, R.n_lines, R.n_arcs, R.n_splines, R.n_surfs, R.n_solids);
    if (k > 0 && (size_t)k < n) k += R.n_meshes ? snprintf(msg + k, n - k, ", %d mesh (%d triangles).", R.n_meshes, R.n_tris) : snprintf(msg + k, n - k, ".");
    if (R.nsk && k > 0 && (size_t)k < n) { k += snprintf(msg + k, n - k, "\nNot imported:"); for (int i = 0; i < R.nsk && (size_t)k < n; i++) k += snprintf(msg + k, n - k, "\n  %s: %d", R.sk[i].what, R.sk[i].n); }
}

/* ------------------------------------------------------------------------------------------------ DXF */
static const Dxf *D;
static McamFile *Fp;
static double CTOL;
static int main_color, main_level;

/* FUN_18003c130: the level of each DXF layer, in table order (FUN_18000a210 -> FUN_18003c130). The first new layer gets the main level when the
   main level is empty (FUN_18003bff0: no entity on it); the others LevelData::nextAvailableLevelNumber (the next number with no level);
   an existing level of that name is reused. Layer off / frozen -> the level hidden (setLevel(level, on && !frozen, 1)). */
typedef struct { char name[80]; int level, color; } Lay;
static Lay *lays; static int nlays;
static void lay_reset(void) { free(lays); lays = NULL; nlays = 0; }
static int level_from_name(const char *name) {
    for (int i = 0; i < Fp->nlvls; i++) if (mcam_level_named(&Fp->lvls[i]) && !strncmp(Fp->lvls[i].name, name, 0x50)) return Fp->lvls[i].level;
    return -1;
}
static int level_empty(int lev) {
    for (int i = 0; i < Fp->nents; i++) if (Fp->ents[i].type && Fp->ents[i].level == lev) return 0;
    return 1;
}
static int next_level(void) { int L = 1; while (mcam_level(Fp, L)) L++; return L; }
static void lay_add(const char *name, int color, int on, int frozen) {
    lays = realloc(lays, sizeof(Lay) * (nlays + 1)); Lay *L = &lays[nlays++];
    memset(L, 0, sizeof *L); snprintf(L->name, sizeof L->name, "%s", name); L->color = color;
    int lev = level_from_name(name);
    if (lev < 1) {
        if (nlays == 1 && level_empty(main_level)) lev = main_level; else lev = next_level();
        mcam_level_set(Fp, lev, on && !frozen);
        mcam_set_levname(Fp, lev, name);
    }
    L->level = lev;
}
/* FUN_18003c8c0: the level of a layer name: 0xff when the layer is not in the table */
static const Lay *lay_of(const char *name) { for (int i = 0; i < nlays; i++) if (!strncmp(lays[i].name, name, 0x50)) return &lays[i]; return NULL; }

/* FUN_1800287b0: nearest of the 256 colours (squared distance of the 0-255 channels, FUN_18001f970) */
static int nearest_color(double r, double g, double b) {
    int best = 0; double bd = 1e300;
    for (int i = 0; i < 256; i++) { float c[3] = { 0, 0, 0 }; mc_rgb(i, c);
        double d = (c[0] * 255 - r) * (c[0] * 255 - r) + (c[1] * 255 - g) * (c[1] * 255 - g) + (c[2] * 255 - b) * (c[2] * 255 - b); if (i == 0 || d < bd) { bd = d; best = i; } }
    return best;
}
/* FUN_180028a40: ACI -> Mastercam colour: ACI 7 is white or black by the background (> 150 in all three channels -> 0, else 15); otherwise the
   table's RGB; if the result is the background colour, main_color */
static int color_of_aci(int aci) {
    int c;
    if (aci == 7) { float b[3] = { 0, 0, 0 }; mc_rgb(cfg_back_color, b); c = (b[0] * 255 > 150 && b[1] * 255 > 150 && b[2] * 255 > 150) ? 0 : 15; }
    else if (aci >= 1 && aci <= 255) c = nearest_color(ACI_RGB[aci - 1][0], ACI_RGB[aci - 1][1], ACI_RGB[aci - 1][2]);
    else c = 0;
    if (c == cfg_back_color) c = main_color;
    return c;
}
/* FUN_18003c6a0: the line type's name -> Mastercam line style */
static int style_of_ltype(const char *name) {
    char u[256]; int i = 0; for (; name[i] && i < 255; i++) u[i] = (char)((name[i] >= 'a' && name[i] <= 'z') ? name[i] - 32 : name[i]); u[i] = 0;
    if (strstr(u, "HIDDEN") || strstr(u, "DASHED")) return 2;
    if (strstr(u, "CENTER") || strstr(u, "CHAIN")) return 3;
    if (strstr(u, "PHANTOM")) return 4;
    if (strstr(u, "DOT")) return 5;
    if (strstr(u, "ACAD_I")) {
        if (strstr(u, "ACAD_ISO02W100") || strstr(u, "ACAD_ISO03W100") || strstr(u, "ACAD_ISO08W100") || strstr(u, "ACAD_ISO09W100")) return 2;
        if (strstr(u, "ACAD_ISO07W100")) return 4;
        return 3;
    }
    return 1;
}
/* FUN_180082ee0: the lineweight (1/100 mm) -> line width 1..5 (the decoded bands) */
static double width_of_weight(int w) { if (w >= 0x8d) return 5; if (w >= 0x65) return 4; if (w >= 0x33) return 3; if (w > 0x19) return 2; return 1; }

/* the block-reference stack (FUN_18002e400): up to 50 levels; FUN_18002e6f0 applies them last-in first-out */
typedef struct { double ins[3], sx, sy, sz, rot, base[3]; int view; double vm[9]; } Xf;
static Xf X[50]; static int nX;
static void vw_to_world(const double M[9], const double p[3], double o[3]) { for (int k = 0; k < 3; k++) o[k] = p[0] * M[0 + k] + p[1] * M[3 + k] + p[2] * M[6 + k]; }
static void world_to_vw(const double M[9], const double p[3], double o[3]) { o[0] = M[0] * p[0] + M[1] * p[1] + M[2] * p[2]; o[1] = M[3] * p[0] + M[4] * p[1] + M[5] * p[2]; o[2] = M[6] * p[0] + M[7] * p[1] + M[8] * p[2]; }
/* FUN_18002dfd0: the view number of a normal; 1 Top, 2 Front, 3 Back, 4 Bottom, 5 Right, 6 Left, else FUN_18002dd00 (the DXF arbitrary axis rule) */
static int view_of_normal(const double n[3], double M[9]) {
    int v = fabs(n[0]) <= 1e-6 && fabs(n[1]) <= 1e-6 && fabs(n[2] - 1) <= 1e-6 ? 1 : fabs(n[0]) <= 1e-6 && fabs(n[1] + 1) <= 1e-6 && fabs(n[2]) <= 1e-6 ? 2
          : fabs(n[0]) <= 1e-6 && fabs(n[1] - 1) <= 1e-6 && fabs(n[2]) <= 1e-6 ? 3 : fabs(n[0]) <= 1e-6 && fabs(n[1]) <= 1e-6 && fabs(n[2] + 1) <= 1e-6 ? 4
          : fabs(n[0] - 1) <= 1e-6 && fabs(n[1]) <= 1e-6 && fabs(n[2]) <= 1e-6 ? 5 : fabs(n[0] + 1) <= 1e-6 && fabs(n[1]) <= 1e-6 && fabs(n[2]) <= 1e-6 ? 6 : 0;
    if (v) { mcam_std_view(v, M); return v; }
    double ax[3] = { 0, 0, 0 };
    if (fabs(n[0]) < 0.015625 && fabs(n[1]) < 0.015625) { ax[0] = n[2]; ax[1] = 0; ax[2] = -n[0]; }   /* Wy x N */
    else { ax[0] = -n[1]; ax[1] = n[0]; ax[2] = 0; }                                                 /* Wz x N */
    double l = sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); for (int k = 0; k < 3; k++) ax[k] /= l;
    double ay[3] = { n[1] * ax[2] - n[2] * ax[1], n[2] * ax[0] - n[0] * ax[2], n[0] * ax[1] - n[1] * ax[0] };
    for (int k = 0; k < 3; k++) { M[k] = ax[k]; M[3 + k] = ay[k]; M[6 + k] = n[k]; }
    return 9;       /* a user view: not reserved. Our stand-in number (find_view_m's stored view is NOT PORTED) */
}
/* FUN_18002e6f0: a point through the block stack */
static void xf_pt(double p[3]) {
    for (int i = nX - 1; i >= 0; i--) { const Xf *x = &X[i];
        double a[3] = { (p[0] - x->base[0]) * x->sx, (p[1] - x->base[1]) * x->sy, (p[2] - x->base[2]) * x->sz };
        double c = cos(x->rot), s = sin(x->rot), r[3] = { a[0] * c - a[1] * s, a[0] * s + a[1] * c, a[2] };
        if (x->view <= 1) { for (int k = 0; k < 3; k++) p[k] = r[k] + x->ins[k]; }
        else { double b[3] = { r[0] + x->base[0], r[1] + x->base[1], r[2] + x->base[2] }, w[3]; vw_to_world(x->vm, b, w); for (int k = 0; k < 3; k++) p[k] = w[k] + x->ins[k]; }
    }
}
/* FUN_18002eb10: a direction through the block stack */
static void xf_dir(double p[3]) {
    for (int i = nX - 1; i >= 0; i--) { const Xf *x = &X[i];
        double a[3] = { p[0] * x->sx, p[1] * x->sy, p[2] * x->sz };
        double c = cos(x->rot), s = sin(x->rot), r[3] = { a[0] * c - a[1] * s, a[0] * s + a[1] * c, a[2] };
        if (x->view <= 1) memcpy(p, r, sizeof r); else vw_to_world(x->vm, r, p);
    }
}
static double xf_radius(double r) { for (int i = nX - 1; i >= 0; i--) if (fabs(fabs(X[i].sx) - fabs(X[i].sy)) < 0.0001) r *= fabs(X[i].sx); return r; }
/* FUN_18002ede0: a non-uniform stack */
static int xf_nonuniform(void) {
    double a = 1, b = 1, c = 1;
    for (int i = nX - 1; i >= 0; i--) { a *= fabs(X[i].sx); b *= fabs(X[i].sy); c *= fabs(X[i].sz); }
    return !(fabs(a - b) <= 0.0001 && fabs(a - c) <= 0.0001 && fabs(b - c) <= 0.0001);
}
/* FUN_18002f110 / FUN_18002f290: a start angle and sweep through the block stack */
static void xf_angles(double *a0, double *sw) {
    for (int i = nX - 1; i >= 0; i--) {
        if (X[i].sx < 0) { *a0 = M_PI - *a0; *sw = -*sw; }
        if (X[i].sy < 0) { *a0 = 2 * M_PI - *a0; *sw = -*sw; }
    }
    for (int i = nX - 1; i >= 0; i--) *a0 += X[i].rot;
}

/* FUN_180083240 + FUN_1800385f0: the entity's attributes */
typedef struct { int color, level, style; double width; int blank; } Att;

static int finish_ent(int k, const Att *a) {
    if (k < 0) return k;
    Ent *en = &Fp->ents[k];
    en->ln_style = a->style > 0 ? a->style : 1; en->width = a->width > 0 ? a->width : 1;
    if (a->blank) mcam_ent_blank(Fp, k, 1);
    return k;
}
static void store_point(const Att *a, const double p[3], int style, int blank) {
    if (Fp == NULL) return;
    V3 v = { p[0], p[1], p[2] }; V3 z = { 0, 0, 0 };
    int k = mcam_new_ent(Fp, E_POINT, v, v, z, 0, 0, 0, a->level, a->color);
    if (k >= 0) { Att b = *a; b.blank = a->blank || blank; finish_ent(k, &b); Fp->ents[k].pt_style = style; }
    R.n_pts++;
}
static double dist3(const double a[3], const double b[3]) { return sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }
/* FUN_180038a40: the line */
static void store_line_raw(const Att *a, const double p0[3], const double p1[3]) {
    V3 v0 = { p0[0], p0[1], p0[2] }, v1 = { p1[0], p1[1], p1[2] }, z = { 0, 0, 0 };
    finish_ent(mcam_new_ent(Fp, E_LINE, v0, v1, z, 0, 0, 0, a->level, a->color), a); R.n_lines++;
}
static void store_line(const Att *a, const double p0[3], const double p1[3], double thick, int view, const double VM[9]) {
    double q0[3], q1[3]; memcpy(q0, p0, sizeof q0); memcpy(q1, p1, sizeof q1); xf_pt(q0); xf_pt(q1);
    if (dist3(q0, q1) <= CTOL) return;
    store_line_raw(a, q0, q1);
    /* FUN_18002f010: the thickness through the stack; if |t| > tol, a copy offset along the view's z and the two end lines (FUN_18002fde0) */
    for (int i = nX - 1; i >= 0; i--) if (fabs(fabs(X[i].sx) - fabs(X[i].sy)) < 0.0001) thick *= fabs(X[i].sx);
    if (fabs(thick) > CTOL) {
        double z[3] = { 0, 0, 1 }; if (view >= 2) { z[0] = VM[6]; z[1] = VM[7]; z[2] = VM[8]; }
        double e0[3], e1[3]; for (int k = 0; k < 3; k++) { e0[k] = q0[k] + z[k] * thick; e1[k] = q1[k] + z[k] * thick; }
        store_line_raw(a, q0, e0);
        if (dist3(q0, q1) > CTOL) store_line_raw(a, q1, e1);
        store_line_raw(a, e0, e1);
    }
}
static void arc_store1(const Att *a, const double c[3], double r, const double n[3], double a0, double sw, int blank) {
    (void)blank;
    double M[9]; int v = view_of_normal(n, M);
    V3 cw = { c[0], c[1], c[2] };
    int k = v == 1 ? mcam_new_ent(Fp, E_ARC, cw, cw, cw, r, a0, sw, a->level, a->color) : mcam_new_arc_view_w(Fp, v, M, cw, r, a0, sw, a->level, a->color);
    finish_ent(k, a); R.n_arcs++;
}
/* FUN_180030980: the arc: param_1 = 1 centre in world, 0 centre in the normal's view; the start and sweep (radians); thickness: a copy and the two end lines */
static void store_arc(const Att *a, const double ci[3], int cen_world, const double n0[3], double r, double a0, double sw, double thick) {
    double n[3], c[3]; memcpy(n, n0, sizeof n); memcpy(c, ci, sizeof c);
    int nu = xf_nonuniform();
    if (nu) { rp_skip("ARC / CIRCLE in a non-uniformly scaled block (NOT PORTED)"); return; }
    if (cen_world == 0) { double M[9]; view_of_normal(n, M); double w[3]; vw_to_world(M, c, w); memcpy(c, w, sizeof c); }
    xf_pt(c); r = xf_radius(r); xf_angles(&a0, &sw); xf_dir(n);
    double ln = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (ln > 0) for (int k = 0; k < 3; k++) n[k] /= ln;
    if (fabs(r) <= CTOL || fabs(r * sw) <= CTOL) return;
    arc_store1(a, c, r, n, a0, sw, 0);
    /* thickness: a copy along the normal and the two end lines */
    for (int i = nX - 1; i >= 0; i--) if (fabs(fabs(X[i].sx) - fabs(X[i].sy)) < 0.0001) thick *= fabs(X[i].sx);
    if (fabs(thick) > CTOL) {
        double M[9]; int v = view_of_normal(n, M); double e0[3], e1[3], c2[3], p0v[3], p1v[3], cv[3];
        world_to_vw(M, c, cv); (void)v;
        p0v[0] = cv[0] + r * cos(a0); p0v[1] = cv[1] + r * sin(a0); p0v[2] = cv[2]; p1v[0] = cv[0] + r * cos(a0 + sw); p1v[1] = cv[1] + r * sin(a0 + sw); p1v[2] = cv[2];
        vw_to_world(M, p0v, e0); vw_to_world(M, p1v, e1);
        for (int k = 0; k < 3; k++) c2[k] = c[k] + n[k] * thick;
        double e0b[3], e1b[3]; for (int k = 0; k < 3; k++) { e0b[k] = e0[k] + n[k] * thick; e1b[k] = e1[k] + n[k] * thick; }
        store_line_raw(a, e0, e0b);
        if (dist3(e0, e1) > CTOL) store_line_raw(a, e1, e1b);
        arc_store1(a, c2, r, n, a0, sw, 0);
    }
}
/* FUN_180030fe0: the bulge arc from p0 to p1 (points in the normal's view), sweep > 0 about the normal; decoded as: included angle 4 atan|b|, centre on the chord's
   perpendicular at the distance sqrt(r^2 - d^2 / 4), the perpendicular flipped when |b| > 1; start at p1 for b <= 0 (the arc runs from p1 to p0) and p0 for b > 0 */
static void store_bulge(const Att *a, const double p0[3], const double p1[3], double b, const double n[3], double thick) {
    double th = 4 * atan(fabs(b)), dx = p1[0] - p0[0], dy = p1[1] - p0[1], d = hypot(dx, dy);
    if (!(d > 0)) return;
    double r = d * 0.5 / sin(th * 0.5), h = sqrt(fabs(r * r - d * d / 4));
    double px = -dy / d, py = dx / d;
    if (b < 0) { px = -px; py = -py; }                                         /* the left normal, mirrored for b < 0 */
    if (fabs(b) > 1) { px = -px; py = -py; }
    double cx = (p0[0] + p1[0]) * 0.5 + h * px, cy = (p0[1] + p1[1]) * 0.5 + h * py;
    double s;
    if (b <= 0) s = atan2(p1[1] - cy, p1[0] - cx); else s = atan2(p0[1] - cy, p0[0] - cx);
    double c[3] = { cx, cy, p0[2] };
    store_arc(a, c, 0, n, r, s, th, thick);
}
/* the view-frame polyline (FUN_1800399f0 / FUN_180039a80 / FUN_180039bf0): mode 1 = the points are x, y in the view of the normal at the elevation, mode 0 = world */
typedef struct { double (*p)[3]; double *b; int n, cap; int mode, closed; double elev, thick; double nrm[3]; } Poly2;
static void poly_begin(Poly2 *P, int mode, int closed, double elev, const double n[3], double thick) {
    memset(P, 0, sizeof *P); P->mode = mode; P->closed = closed; P->elev = mode ? elev : 0; P->thick = thick; memcpy(P->nrm, n, 3 * sizeof(double));
}
static void poly_add(Poly2 *P, const double p[3], double bulge) {
    if (P->n == P->cap) { P->cap = P->cap ? P->cap * 2 : 32; P->p = realloc(P->p, P->cap * sizeof *P->p); P->b = realloc(P->b, P->cap * sizeof *P->b); }
    if (P->mode) { P->p[P->n][0] = p[0]; P->p[P->n][1] = p[1]; P->p[P->n][2] = P->elev; }   /* FUN_180028140: the elevation replaces z */
    else memcpy(P->p[P->n], p, 3 * sizeof(double));
    P->b[P->n] = bulge; P->n++;
}
static void poly_end(Poly2 *P, const Att *a) {
    double M[9]; int v = P->mode ? view_of_normal(P->nrm, M) : 1; if (!P->mode) mcam_std_view(1, M);
    if (P->n >= 2) {
        int nseg = P->closed ? P->n : P->n - 1;
        for (int i = 0; i < nseg; i++) {
            int j = (i + 1) % P->n;
            double b = P->b[i], w0[3], w1[3];
            int line = fabs(b) <= CTOL; if (i == P->n - 1 && P->closed) line = b == 0.0;   /* the closing segment: exactly 0 */
            if (line) { if (P->mode) { vw_to_world(M, P->p[i], w0); vw_to_world(M, P->p[j], w1); } else { memcpy(w0, P->p[i], sizeof w0); memcpy(w1, P->p[j], sizeof w1); } store_line(a, w0, w1, P->thick, v, M); }
            else {
                double p0[3], p1[3];
                if (P->mode) { memcpy(p0, P->p[i], sizeof p0); memcpy(p1, P->p[j], sizeof p1); } else { world_to_vw(M, P->p[i], p0); world_to_vw(M, P->p[j], p1); }
                store_bulge(a, p0, p1, b, P->nrm, P->thick);
            }
        }
    }
    free(P->p); free(P->b); P->p = NULL; P->b = NULL; P->n = 0;
}

/* The NURBS record (put_nurbs_in_db: mcsb_arc_nurbs_rec's tail layout, ents at +0x68): knots, then (w, w x, w y, w z) */
static void wdd(uint8_t *b, double v) { memcpy(b, &v, 8); }
static int nurbs_store(int deg, int rat, int flags, int nk, const double *k, int nc, const double *cp, double t1, double t2, const Att *a) {
    uint32_t n = 0xd8 + 8 * (uint32_t)(nk + (rat ? 4 : 3) * nc);
    uint8_t *r = calloc(1, n); if (!r) return -1;
    uint32_t cls = 0x80; memcpy(r, &cls, 4);
    memcpy(r + 0xc8, &nk, 4); memcpy(r + 0xcc, &nc, 4); r[0xd0] = (uint8_t)deg; r[0xd1] = (uint8_t)(rat ? 4 : 3); r[0xd2] = (uint8_t)flags;
    for (int i = 0; i < nk; i++) wdd(r + 0xd8 + 8 * i, k[i]);
    int dim = rat ? 4 : 3;
    for (int i = 0; i < nc * dim; i++) wdd(r + 0xd8 + 8 * (nk + i), cp[i]);
    wdd(r + 0x98, t1); wdd(r + 0xa0, t2);
    CurveRec cr = { 0, 0x80, r, n };
    double d[3][3];
    if (!mcs_spline_ok(&cr) || mcs_spline_eval_d(&cr, t1, 0, d)) { free(r); return -1; }
    for (int i = 0; i < 3; i++) wdd(r + 0x68 + 8 * i, d[0][i]);
    if (mcs_spline_eval_d(&cr, t2, 0, d)) { free(r); return -1; }
    for (int i = 0; i < 3; i++) wdd(r + 0x80 + 8 * i, d[0][i]);
    /* put_nurbs_in_db: +0x40 (record 0xa8) := the largest nb_parm_curvature at t1 + i (t2 - t1) / (2 ncp - 2) below 1e9, at least 1e-9 (mcsb_arc_nurbs_rec's) */
    double cmax = 0, dt = (t2 - t1) / (double)(nc * 2 - 2), s = 1.0 / (t2 - t1);
    if (dt > 0) for (double tt = t1; tt <= t2 + 1e-12; tt += dt) {
        double q[3][3]; if (mcs_spline_eval_d(&cr, tt, 2, q)) continue;
        double aa[3] = { q[1][0] * s, q[1][1] * s, q[1][2] * s }, bb[3] = { q[2][0] * s * s, q[2][1] * s * s, q[2][2] * s * s };
        double cc[3] = { aa[1] * bb[2] - aa[2] * bb[1], aa[2] * bb[0] - aa[0] * bb[2], aa[0] * bb[1] - aa[1] * bb[0] };
        double la = sqrt(aa[0] * aa[0] + aa[1] * aa[1] + aa[2] * aa[2]), kk = 0;
        if (la > 1e-6) kk = sqrt(cc[0] * cc[0] + cc[1] * cc[1] + cc[2] * cc[2]) / la;
        if (cmax < kk && kk < 1e9) cmax = kk;
    }
    if (cmax < 1e-9) cmax = 1e-9;
    wdd(r + 0xa8, cmax);
    int e = mcam_new_curve(Fp, r, n, a->level, a->color);
    free(r);
    if (e >= 0) finish_ent(e, a);
    if (e >= 0) R.n_splines++;
    return e;
}
/* FUN_180037fb0: the DXF SPLINE as a NURBS curve: flags bit 1 closed, bit 2 non-rational, bit 3 periodic; the control points through the block stack
   (FUN_180039870); t1 / t2 = getStartParam / getEndParam (the knots' ends); points with |z| > 1e38 are z = 0 (FUN_180083c00) */
static void store_spline(const Att *a, const DxfEnt *e) {
    if (xf_nonuniform()) { rp_skip("SPLINE in a non-uniformly scaled block (NOT PORTED)"); return; }
    int flg = dxf_int(D, e, 70, 0), deg = dxf_int(D, e, 71, 3);
    int rat = (flg & 4) != 0; int closed = (flg & 1) != 0, per = (flg & 2) != 0;
    int nk = 0, nc = 0, nw = 0, cap = e->ng + 8;
    double *K = malloc(sizeof(double) * cap), *P = malloc(sizeof(double) * cap * 3), *W = malloc(sizeof(double) * cap);
    for (int i = 0; i < e->ng; i++) { const DxfG *g = &D->g[e->g0 + i]; double v = strtod(g->s, NULL);
        if (g->code == 40) K[nk++] = v; else if (g->code == 41) W[nw++] = v;
        else if (g->code == 10) { P[3 * nc] = v; P[3 * nc + 1] = 0; P[3 * nc + 2] = 0; nc++; }
        else if (g->code == 20 && nc) P[3 * nc - 2] = v;
        else if (g->code == 30 && nc) { P[3 * nc - 1] = fabs(v) > 1e38 ? 0 : v; } }
    if (nc < deg + 1 || nk != nc + deg + 1) { rp_skip("SPLINE without control points (fit points only, NOT PORTED)"); free(K); free(P); free(W); return; }
    if (rat && nw != nc) { rp_skip("SPLINE with bad weights"); free(K); free(P); free(W); return; }
    int dim = rat ? 4 : 3; double *cp = malloc(sizeof(double) * nc * dim);
    for (int i = 0; i < nc; i++) { double p[3] = { P[3 * i], P[3 * i + 1], P[3 * i + 2] }; xf_pt(p);
        if (rat) { double w = W[i]; cp[4 * i] = w; cp[4 * i + 1] = w * p[0]; cp[4 * i + 2] = w * p[1]; cp[4 * i + 3] = w * p[2]; }
        else { cp[3 * i] = p[0]; cp[3 * i + 1] = p[1]; cp[3 * i + 2] = p[2]; } }
    int fl = (rat ? 0 : 4) | (closed ? 2 : 0) | (per ? 8 : 0);
    nurbs_store(deg, rat, fl, nk, K, nc, cp, K[0], K[nk - 1], a);
    free(K); free(P); free(W); free(cp);
}
/* FUN_1800319e0: the ellipse as the rational degree 2 NURBS (knots 0 0 0 1 1 2 2 3 3 4 4 4, nine control points, the corners at weight 0.7071...),
   t1 / t2 in the knot range: the exact knot when the angle is 0 / 90 / 180 / 270 (within 0.01 degrees), else Newton on the curve towards the start / end point (FUN_180031630). */
static const double QUARTER_W = 0.70710678118654757;
static void store_ellipse_part(const Att *a, const double c[3], const double maj[3], double ratio, const double n[3], double a1, double a2, const double *ps, const double *pe, int full) {
    double M[9]; int v = view_of_normal(n, M); (void)v;
    double Lmaj = sqrt(maj[0] * maj[0] + maj[1] * maj[1] + maj[2] * maj[2]), Lmin = Lmaj * ratio;
    double mv[3]; world_to_vw(M, maj, mv);
    double rot = atan2(mv[1], mv[0]), cs = cos(rot), sn = sin(rot);
    static const double W0[9] = { 1, 0, 1, 0, 1, 0, 1, 0, 1 };
    double wv[9]; for (int i = 0; i < 9; i++) wv[i] = W0[i] ? 1 : QUARTER_W;
    double X0[9] = { 1, 1, 0, -1, -1, -1, 0, 1, 1 }, Y0[9] = { 0, 1, 1, 1, 0, -1, -1, -1, 0 };
    double pts[9][3];
    double cv[3]; world_to_vw(M, c, cv);
    double K[12] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4 };
    for (int i = 0; i < 9; i++) {
        double x = X0[i] * Lmaj, y = Y0[i] * Lmin;
        double q[3] = { x * cs - y * sn, x * sn + y * cs, 0 }, w[3];
        vw_to_world(M, q, w);
        for (int k = 0; k < 3; k++) pts[i][k] = w[k] + c[k];
    }
    double cp[36]; for (int i = 0; i < 9; i++) { cp[4 * i] = wv[i]; for (int k = 0; k < 3; k++) cp[4 * i + 1 + k] = wv[i] * pts[i][k]; }
    double t1 = 0, t2 = 4;
    /* a temporary evaluator: the same record */
    uint32_t nn = 0xd8 + 8 * (12 + 36);
    uint8_t *r = calloc(1, nn); uint32_t cls = 0x80; int nk = 12, nc = 9; memcpy(r, &cls, 4); memcpy(r + 0xc8, &nk, 4); memcpy(r + 0xcc, &nc, 4); r[0xd0] = 2; r[0xd1] = 4; r[0xd2] = 0x11;
    for (int i = 0; i < 12; i++) wdd(r + 0xd8 + 8 * i, K[i]);
    for (int i = 0; i < 36; i++) wdd(r + 0xd8 + 8 * (12 + i), cp[i]);
    wdd(r + 0x98, 0.0); wdd(r + 0xa0, 4.0);
    CurveRec cr = { 0, 0x80, r, nn };
    if (!(fabs(a1) <= 0.01)) {
        if (fabs(fabs(a1) - 90) < 0.01) t1 = 1; else if (fabs(fabs(a1) - 180) < 0.01) t1 = 2; else if (fabs(fabs(a1) - 270) < 0.01) t1 = 3;
        else if (ps) { t1 = a1 / 90.0; for (int it = 0; it < 50; it++) { double o[3][3]; if (mcs_spline_eval_d(&cr, t1, 1, o)) break;
            double dx = ps[0] - o[0][0], dy = ps[1] - o[0][1], dz = ps[2] - o[0][2], dq = sqrt(dx * dx + dy * dy + dz * dz); if (dq < CTOL) break;
            double dp = dx * o[1][0] + dy * o[1][1] + dz * o[1][2]; if (fabs(dp) < 2e-7) break;
            double l2 = o[1][0] * o[1][0] + o[1][1] * o[1][1] + o[1][2] * o[1][2]; if (!(l2 > 0)) break;
            t1 += dp / l2; if (t1 < 0) t1 = 0; else if (t1 > 4) t1 = 4; } }
    }
    if (a2 < 360.0) {
        if (fabs(a2 - 90) < 0.01) t2 = 1; else if (fabs(a2 - 180) < 0.01) t2 = 2; else if (fabs(a2 - 270) < 0.01) t2 = 3;
        else if (pe) { t2 = a2 / 90.0; for (int it = 0; it < 50; it++) { double o[3][3]; if (mcs_spline_eval_d(&cr, t2, 1, o)) break;
            double dx = pe[0] - o[0][0], dy = pe[1] - o[0][1], dz = pe[2] - o[0][2], dq = sqrt(dx * dx + dy * dy + dz * dz); if (dq < CTOL) break;
            double dp = dx * o[1][0] + dy * o[1][1] + dz * o[1][2]; if (fabs(dp) < 2e-7) break;
            double l2 = o[1][0] * o[1][0] + o[1][1] * o[1][1] + o[1][2] * o[1][2]; if (!(l2 > 0)) break;
            t2 += dp / l2; if (t2 < 0) t2 = 0; else if (t2 > 4) t2 = 4; } }
    }
    if (t2 <= t1) t1 = t2 - 1e-6;
    free(r);
    int fl = full ? 0x13 : 0x11;
    nurbs_store(2, 1, fl, 12, K, 9, cp, t1, t2, a);
}
/* FUN_180085e50's ELLIPSE branch and FUN_180039220: start / end parameter (radians) -> degrees, FUN_180085030's normalisation, the split at 360 */
static void store_ellipse(const Att *a, const double c0[3], const double maj0[3], double ratio, const double n0[3], double u0, double u1) {
    double a1 = u0 * DEG, a2 = u1 * DEG;
    /* FUN_180085030 */
    if (a1 >= 360.0 && a2 > 360.0000000001) { while (a2 > 360.0) { a1 -= 360.0; a2 -= 360.0; } }
    else if (a1 <= 0.0 && a2 <= -1e-10) { while (a2 <= -1e-10) { a1 += 360.0; a2 += 360.0; } }
    if (fabs(a1) < 1e-10 || fabs(a1 - 360.0) < 1e-10) a1 = 0;
    if (fabs(a2 - 360.0) < 1e-10) a2 = 360.0;
    if (a1 < 0 && a2 < 0) { a1 += 360.0; a2 += 360.0; }
    if (fabs(a2 - a1) < 1e-6) a2 = a1 + 360.0; else if (a2 < a1) a2 += 360.0;
    double c[3], n[3], maj[3]; memcpy(c, c0, sizeof c); memcpy(n, n0, sizeof n); memcpy(maj, maj0, sizeof maj);
    if (xf_nonuniform()) { rp_skip("ELLIPSE in a non-uniformly scaled block (NOT PORTED)"); return; }
    xf_pt(c); xf_dir(maj); xf_dir(n);
    double ln = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (ln > 0) for (int k = 0; k < 3; k++) n[k] /= ln;
    double ps[3], pe[3];
    { double L = sqrt(maj[0] * maj[0] + maj[1] * maj[1] + maj[2] * maj[2]);
      double mn[3] = { n[1] * maj[2] - n[2] * maj[1], n[2] * maj[0] - n[0] * maj[2], n[0] * maj[1] - n[1] * maj[0] };
      double lm = sqrt(mn[0] * mn[0] + mn[1] * mn[1] + mn[2] * mn[2]); if (lm > 0) for (int k = 0; k < 3; k++) mn[k] = mn[k] / lm * L * ratio;
      double u = a1 / DEG, vv = a2 / DEG;
      for (int k = 0; k < 3; k++) { ps[k] = c[k] + cos(u) * maj[k] + sin(u) * mn[k]; pe[k] = c[k] + cos(vv) * maj[k] + sin(vv) * mn[k]; } }
    if (a1 >= 0 && a2 <= 360.0) store_ellipse_part(a, c, maj, ratio, n, a1, a2, ps, pe, a2 == 360.0 && a1 == 0);
    else {
        double s1 = a1; while (s1 < 0) s1 += 360; double s2 = a2; while (s2 > 360) s2 -= 360;
        if (s1 < 359.999999) store_ellipse_part(a, c, maj, ratio, n, s1, 360.0, ps, NULL, 0);
        if (s2 > 1e-6) store_ellipse_part(a, c, maj, ratio, n, 0.0, s2, NULL, pe, 0);
    }
}

/* the mask (FUN_180040e80): every type on by default (mcautocadres dialog 502, DWG.MSK absent); ids as passed by FUN_180085e50 */
typedef struct { int color, level, style; double width; } Parent;
static int blank_ps;                                      /* convDWGBlankPS (config 3007) */
static const char *ent_layer(const DxfEnt *e) { const char *v = dxf_val(D, e, 8); return v ? v : "0"; }
static int style_for(const DxfEnt *e, const char *layer) {
    const char *lt = dxf_val(D, e, 6);
    if (lt && !strcasecmp(lt, "ByBlock")) return -1;
    if (!lt || !strcasecmp(lt, "ByLayer")) { const DxfLayer *L = dxf_layer(D, layer); return style_of_ltype(L && L->ltype && L->ltype[0] ? L->ltype : "Continuous"); }
    return style_of_ltype(lt);
}
static void attrs_of(const DxfEnt *e, const Parent *par, Att *a) {
    const char *layer = ent_layer(e); const Lay *L = lay_of(layer);
    int lev = L ? L->level : 0xff, col_i = -1; int c62 = dxf_int(D, e, 62, 256);
    const DxfLayer *DL = dxf_layer(D, layer);
    int layer_aci = DL ? abs(DL->color) : 7;
    if (dxf_val(D, e, 420)) { long rgb = strtol(dxf_val(D, e, 420), NULL, 10); col_i = nearest_color((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
    else if (c62 == 256) col_i = color_of_aci(layer_aci);
    else if (c62 == 0) col_i = -1;                    /* ByBlock */
    else if (c62 > 0 && c62 < 256) col_i = color_of_aci(c62);
    else col_i = -1;
    int sty = style_for(e, layer);
    /* FUN_180082f80: group 370; -1 ByLayer: the layer's; -3 (default): 1 */
    int w_ = dxf_int(D, e, 370, -1);
    if (w_ == -1 && DL) w_ = DL->lw;
    double wid = w_ == -3 ? 1 : width_of_weight(w_);
    if (par) {                                     /* in a block (FUN_180085e50's FUN_180083240 pair, param_2 = 1): layer "0" and ByBlock inherit the INSERT's */
        if (!strcmp(layer, "0")) { lev = par->level; if (c62 == 256) col_i = par->color; }
        if (c62 == 0) col_i = par->color;
        if (sty == -1) sty = par->style;
    }
    /* FUN_1800385f0: negative -> main; background -> main; still background -> +2; width outside 1..6 -> 1 */
    if (col_i < 0) col_i = main_color;
    if (col_i == cfg_back_color) col_i = main_color;
    if (col_i == cfg_back_color) col_i = (col_i + 2) & 0xff;
    if (lev < 0) lev = main_level;
    if (sty < 1) sty = 1;
    if (wid <= 1.0 || wid >= 6.0) wid = 1.0;
    a->color = col_i; a->level = lev; a->style = sty; a->width = wid; a->blank = 0;
}
static const double ZAX[3] = { 0, 0, 1 };
static void ocs_normal(const DxfEnt *e, double n[3]) {
    n[0] = dxf_num(D, e, 210, 0); n[1] = dxf_num(D, e, 220, 0); n[2] = dxf_num(D, e, 230, 1);
    double l = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (l > 0) for (int k = 0; k < 3; k++) n[k] /= l; else memcpy(n, ZAX, sizeof ZAX);
}
/* OCS -> WCS (the DXF arbitrary axis rule): Teigha gives WCS centres and positions (OdDbCircle::center etc.); the file stores OCS */
static void ocs_matrix(const double n[3], double M[9]) {
    double ax[3];
    if (fabs(n[0]) < 0.015625 && fabs(n[1]) < 0.015625) { ax[0] = n[2]; ax[1] = 0; ax[2] = -n[0]; } else { ax[0] = -n[1]; ax[1] = n[0]; ax[2] = 0; }
    double l = sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); for (int k = 0; k < 3; k++) ax[k] /= l;
    double ay[3] = { n[1] * ax[2] - n[2] * ax[1], n[2] * ax[0] - n[0] * ax[2], n[0] * ax[1] - n[1] * ax[0] };
    for (int k = 0; k < 3; k++) { M[k] = ax[k]; M[3 + k] = ay[k]; M[6 + k] = n[k]; }
}
static void ocs_to_wcs(const double n[3], const double p[3], double o[3]) { double M[9]; ocs_matrix(n, M); vw_to_world(M, p, o); }

static void do_entity(int ei, int in_block, const Parent *par, int pdmode);
/* POLYLINE and its VERTEX records (FUN_180085e50's OdDb2dPolyline / OdDb3dPolyline branches): vertex selection per poly type as decoded */
static int do_polyline(int ei, const Att *a, const Parent *par) {
    (void)par;
    const DxfEnt *e = &D->e[ei]; int flg = dxf_int(D, e, 70, 0);
    if (flg & 64) { rp_skip("POLYLINE polyface mesh (NOT PORTED)"); goto skipv; }
    if (flg & 16) { rp_skip("POLYLINE polygon mesh (NOT PORTED)"); goto skipv; }
    double n[3]; ocs_normal(e, n);
    int mode3d = (flg & 8) != 0; double elev = dxf_num(D, e, 30, 0), thick = dxf_num(D, e, 39, 0);
    int closed = flg & 1, curves = flg & 2, spl = flg & 4;
    Poly2 P; poly_begin(&P, mode3d ? 0 : 1, closed, elev, mode3d ? ZAX : n, mode3d ? 0 : thick);
    int k = ei + 1;
    for (; k < D->ne; k++) {
        const DxfEnt *v = &D->e[k];
        if (!strcasecmp(v->type, "SEQEND")) break;
        if (strcasecmp(v->type, "VERTEX")) break;
        int vf = dxf_int(D, v, 70, 0);
        /* the OdDb vertex type from the DXF flags: 16 = spline control point (1), 8 = spline-fit vertex (2), 1 = curve-fit vertex (3), else 0.
           FUN_180085e50 2D: polyType 0 simple keeps vertexType 0; 1 fit-curve 0 or 3; 2 / 3 quadratic / cubic spline the type 2 (fit) vertices: the visible spline polyline.
           3D: polyType 0: vertexType 0; 1 / 2: type 2. */
        int vt = (vf & 16) ? 1 : (vf & 8) ? 2 : (vf & 1) ? 3 : 0;
        int ok;
        if (mode3d) ok = spl ? vt == 2 : vt == 0;      /* 3D: polyType 0: vertexType 0; 1 / 2: 2 (quad / cubic spline control points) */
        else ok = spl ? vt == 2 : curves ? (vt == 0 || vt == 3) : vt == 0;
        if (ok) {
            double p[3] = { dxf_num(D, v, 10, 0), dxf_num(D, v, 20, 0), dxf_num(D, v, 30, 0) };
            if (mode3d) { poly_add(&P, p, 0.0); }
            else { poly_add(&P, p, dxf_num(D, v, 42, 0)); }
        }
    }
    poly_end(&P, a);
    return k;
skipv:
    k = ei + 1; while (k < D->ne && strcasecmp(D->e[k].type, "SEQEND")) k++;
    return k;
}

static int inserted_depth;
static void do_insert(int ei, const Att *a, const Parent *pp, int pdmode) {
    (void)pp;
    const DxfEnt *e = &D->e[ei];
    const char *nm = dxf_val(D, e, 2); const DxfBlock *B = nm ? dxf_block(D, nm) : NULL;
    if (!B) { rp_skip("INSERT of a missing block"); return; }
    if (B->flags & 4) { rp_skip("INSERT of an XREF block (NOT PORTED)"); return; }
    if (dxf_int(D, e, 70, 1) > 1 || dxf_int(D, e, 71, 1) > 1) { rp_skip("INSERT array (MINSERT, NOT PORTED)"); return; }
    if (nX >= 50 || inserted_depth > 16) { rp_skip("INSERT nesting too deep"); return; }
    double n[3]; ocs_normal(e, n);
    double ins[3] = { dxf_num(D, e, 10, 0), dxf_num(D, e, 20, 0), dxf_num(D, e, 30, 0) }, w[3]; ocs_to_wcs(n, ins, w);   /* OdDbBlockReference::position */
    Xf *x = &X[nX]; memset(x, 0, sizeof *x); memcpy(x->ins, w, sizeof w); memcpy(x->base, B->base, 3 * sizeof(double));
    x->sx = dxf_num(D, e, 41, 1); x->sy = dxf_num(D, e, 42, 1); x->sz = dxf_num(D, e, 43, 1); x->rot = dxf_num(D, e, 50, 0) / DEG;
    x->view = view_of_normal(n, x->vm);
    nX++; inserted_depth++;
    Parent mine = { a->color, a->level, a->style, a->width };
    for (int i = 0; i < B->ne; i++) {
        const DxfEnt *be = &D->e[B->e0 + i];
        if (!strcasecmp(be->type, "VERTEX") || !strcasecmp(be->type, "SEQEND") || !strcasecmp(be->type, "ATTRIB")) continue;
        do_entity(B->e0 + i, 1, &mine, pdmode);
    }
    nX--; inserted_depth--;
    /* ATTRIB records after INSERT (flag 66) are reported */
    if (dxf_int(D, e, 66, 0)) { for (int k = ei + 1; k < D->ne; k++) { const DxfEnt *v = &D->e[k]; if (!strcasecmp(v->type, "SEQEND")) break; if (!strcasecmp(v->type, "ATTRIB")) rp_skip("ATTRIB text (NOT PORTED)"); } }
}

/* one record of the ENTITIES section (or a block's, in_block = 1): FUN_180085e50's class switch */
static void do_entity(int ei, int in_block, const Parent *par, int pdmode) {
    const DxfEnt *e = &D->e[ei];
    const char *t = e->type;
    if (dxf_int(D, e, 60, 0) != 0) return;           /* OdDbEntity::visibility != visible: skipped (the top of FUN_180085e50) */
    Att a; attrs_of(e, par, &a);
    int is_ps = dxf_int(D, e, 67, 0) != 0 && !in_block;
    if (is_ps && blank_ps) a.blank = 1;                /* FUN_180081e60 == 2 (the paper-space block) with convDWGBlankPS: sel 0x101 (FUN_1800385f0) */
    double n[3]; ocs_normal(e, n);
    if (!strcasecmp(t, "POINT")) {
        double p[3] = { dxf_num(D, e, 10, 0), dxf_num(D, e, 20, 0), dxf_num(D, e, 30, 0) };
        /* FUN_180085e50: at the top level, a point with PDMODE 1 is not stored; in a block it is, blanked (FUN_1800388b0) */
        if (!in_block && pdmode == 1) return;
        xf_pt(p);
        /* FUN_180038810: PDMODE 0 -> style 2, 1 -> 1, 2 -> 3, 3 -> 4, 4 -> 1, else the main point style */
        store_point(&a, p, pdmode == 0 ? 2 : pdmode == 1 ? 1 : pdmode == 2 ? 3 : pdmode == 3 ? 4 : pdmode == 4 ? 1 : cad_pt_style_main, pdmode == 1);
    }
    else if (!strcasecmp(t, "LINE")) {
        double p0[3] = { dxf_num(D, e, 10, 0), dxf_num(D, e, 20, 0), dxf_num(D, e, 30, 0) }, p1[3] = { dxf_num(D, e, 11, 0), dxf_num(D, e, 21, 0), dxf_num(D, e, 31, 0) };
        double M[9]; mcam_std_view(1, M);
        store_line(&a, p0, p1, dxf_num(D, e, 39, 0), 1, M);
    }
    else if (!strcasecmp(t, "CIRCLE")) {
        double c[3] = { dxf_num(D, e, 10, 0), dxf_num(D, e, 20, 0), dxf_num(D, e, 30, 0) }, w[3]; ocs_to_wcs(n, c, w);
        store_arc(&a, w, 1, n, dxf_num(D, e, 40, 0), 0, 2 * M_PI, dxf_num(D, e, 39, 0));
    }
    else if (!strcasecmp(t, "ARC")) {
        double c[3] = { dxf_num(D, e, 10, 0), dxf_num(D, e, 20, 0), dxf_num(D, e, 30, 0) }, w[3]; ocs_to_wcs(n, c, w);
        double s = dxf_num(D, e, 50, 0) / DEG, en = dxf_num(D, e, 51, 0) / DEG;
        if (fabs(en) < 1e-6 && s > 1e-6) en = 2 * M_PI;   /* the decoded end param fix */
        double sw = en - s;
        if (sw < 0) sw += 2 * M_PI;                          /* FUN_180085e50: the area test on the arc makes the sweep the positive one */
        store_arc(&a, w, 1, n, dxf_num(D, e, 40, 0), s, sw, dxf_num(D, e, 39, 0));
    }
    else if (!strcasecmp(t, "ELLIPSE")) {
        double c[3] = { dxf_num(D, e, 10, 0), dxf_num(D, e, 20, 0), dxf_num(D, e, 30, 0) };         /* WCS in the file (DXF ELLIPSE) */
        double ma[3] = { dxf_num(D, e, 11, 0), dxf_num(D, e, 21, 0), dxf_num(D, e, 31, 0) };
        store_ellipse(&a, c, ma, dxf_num(D, e, 40, 1), n, dxf_num(D, e, 41, 0), dxf_num(D, e, 42, 2 * M_PI));
    }
    else if (!strcasecmp(t, "LWPOLYLINE")) {
        int flg = dxf_int(D, e, 70, 0); double elev = dxf_num(D, e, 38, 0), thick = dxf_num(D, e, 39, 0);
        Poly2 P; poly_begin(&P, 1, (flg & 1) != 0, elev, n, thick);
        double x = 0, y = 0, b = 0; int have = 0;
        for (int i = 0; i < e->ng; i++) { const DxfG *g = &D->g[e->g0 + i];
            if (g->code == 10) { if (have) { double p[3] = { x, y, 0 }; poly_add(&P, p, b); } x = strtod(g->s, NULL); y = 0; b = 0; have = 1; }
            else if (g->code == 20) y = strtod(g->s, NULL);
            else if (g->code == 42) b = strtod(g->s, NULL); }
        if (have) { double p[3] = { x, y, 0 }; poly_add(&P, p, b); }
        poly_end(&P, &a);
    }
    else if (!strcasecmp(t, "POLYLINE")) do_polyline(ei, &a, par);
    else if (!strcasecmp(t, "SPLINE")) store_spline(&a, e);
    else if (!strcasecmp(t, "INSERT")) do_insert(ei, &a, par, pdmode);
    else if (!strcasecmp(t, "VERTEX") || !strcasecmp(t, "SEQEND") || !strcasecmp(t, "ATTRIB")) return;
    else if (!strcasecmp(t, "VIEWPORT")) rp_skip("VIEWPORT (NOT PORTED)");
    else if (!strcasecmp(t, "TEXT") || !strcasecmp(t, "MTEXT") || !strcasecmp(t, "ATTDEF")) rp_skip("TEXT / MTEXT (NOT PORTED: Mastercam makes drafting notes)");
    else if (!strcasecmp(t, "DIMENSION")) rp_skip("DIMENSION (NOT PORTED)");
    else if (!strcasecmp(t, "LEADER") || !strcasecmp(t, "MULTILEADER") || !strcasecmp(t, "MLEADER")) rp_skip("LEADER (NOT PORTED)");
    else if (!strcasecmp(t, "HATCH")) rp_skip("HATCH (NOT PORTED)");
    else if (!strcasecmp(t, "SOLID") || !strcasecmp(t, "TRACE") || !strcasecmp(t, "3DFACE")) rp_skip("SOLID / TRACE / 3DFACE (NOT PORTED: Mastercam makes surfaces)");
    else if (!strcasecmp(t, "3DSOLID") || !strcasecmp(t, "REGION") || !strcasecmp(t, "BODY")) rp_skip("3DSOLID / REGION / BODY (ACIS data: NOT DECODED here)");
    else if (!strcasecmp(t, "RAY") || !strcasecmp(t, "XLINE") || !strcasecmp(t, "HELIX")) rp_skip("RAY / XLINE / HELIX (NOT PORTED)");
    else rp_skip(t);
}

static int import_dxf(const char *path, char *msg, size_t msgn) {
    Dxf d; char err[300] = "";
    if (dxf_read(path, &d, err, sizeof err)) { snprintf(msg, msgn, "%s", err); return 0; }
    D = &d; nX = 0; inserted_depth = 0; lay_reset();
    /* FUN_18000a210: the layer table in file order; Teigha's database always has layer "0" (colour 7), so a file with no LAYER table still gets it */
    if (!dxf_layer(&d, "0")) lay_add("0", 7, 1, 0);
    for (int i = 0; i < d.nl; i++) {
        const DxfLayer *L = &d.l[i];
        int off = L->color < 0, frozen = L->flags & 1;
        lay_add(L->name, abs(L->color), !off, frozen);
    }
    int pdmode = 0;                                  /* $PDMODE (OdDbDatabase::getPDMODE) */
    for (int k = 0; k < d.ne; k++) if (!strcasecmp(d.e[k].type, "SECTION")) { const DxfEnt *e = &d.e[k]; const char *nm = dxf_val(&d, e, 2); if (nm && !strcasecmp(nm, "HEADER")) {
        for (int j = e->g0 + 1; j + 1 < d.ng && d.g[j].code != 0; j++) if (d.g[j].code == 9 && !strcasecmp(d.g[j].s, "$PDMODE") && d.g[j + 1].code == 70) pdmode = atoi(d.g[j + 1].s);
        break; } }
    for (int i = d.ent0; i < d.ent0 + d.nent; i++) {
        const DxfEnt *e = &d.e[i];
        if (!strcasecmp(e->type, "POLYLINE")) { do_entity(i, 0, NULL, pdmode); while (i + 1 < d.ent0 + d.nent && strcasecmp(d.e[i].type, "SEQEND")) i++; continue; }
        if (!strcasecmp(e->type, "VERTEX") || !strcasecmp(e->type, "SEQEND") || !strcasecmp(e->type, "ATTRIB")) continue;
        do_entity(i, 0, NULL, pdmode);
    }
    lay_reset(); dxf_free(&d); D = NULL;
    return 1;
}

/* ------------------------------------------------------------------------------------------------ STEP / IGES */
/* FUN_180029470's curve path: a NURBS curve whose control points are collinear becomes a line (check_colinear on consecutive triples); others NURBS */
static void store_occ_curve(const OccCurve *c, const Att *a) {
    double p0[3] = { c->p[0], c->p[1], c->p[2] }, p1[3] = { c->p[3], c->p[4], c->p[5] };
    if (c->type == 1) { if (dist3(p0, p1) > CTOL) store_line_raw(a, p0, p1); return; }
    if (c->type == 2) { arc_store1(a, c->c, c->r, c->n, c->a0, c->sw, 0); return; }
    if (c->type == 3) {
        int dim = 4; double *cp = malloc(sizeof(double) * c->nc * dim); memcpy(cp, c->cp, sizeof(double) * c->nc * dim);
        nurbs_store(c->deg, 1, 0x10, c->nk, c->k, c->nc, cp, c->k[0], c->k[c->nk - 1], a);
        free(cp);
    }
}

static int import_occ(const char *path, int kind, double scale, const CadImportOpts *o, char *msg, size_t msgn) {
    char err[300] = "";
    OccCad *C = occt_cad_read(path, kind == CAD_STEP ? OCC_STEP : OCC_IGES, scale, err, sizeof err);
    if (!C) { snprintf(msg, msgn, "%s", err); return 0; }
    Att a = { main_color, main_level, 1, 1.0, 0 };
    int want_solids = kind == CAD_STEP && o->get_solids;     /* mciges.dll has no solids path (no sld_import_solid import) */
    int ns = occt_cad_nsolids(C);
    char base[300]; { const char *b = strrchr(path, '/'); b = b ? b + 1 : path; snprintf(base, sizeof base, "%s", b); char *dot = strrchr(base, '.'); if (dot) *dot = 0; }
    extern int cfg_solid_color;
    if (want_solids) {
        for (int i = 0; i < ns; i++) {
            size_t n = 0; uint8_t *b = occt_cad_solid_brep(C, i, &n);
            if (!b) { rp_skip("a solid that could not be stored"); continue; }
            Fp->solids = realloc(Fp->solids, (Fp->nsolids + 1) * sizeof(Solid));
            Solid *S = &Fp->solids[Fp->nsolids++]; memset(S, 0, sizeof *S);
            S->brep = b; S->brep_n = n;
            snprintf(S->name, sizeof S->name, "sld_imp_%d", Fp->nsolids);
            S->level = main_level;
            S->color = cfg_solid_color;                  /* dbput_solid; the file's own colours (Use System Color off) are NOT DECODED: the system's */
            /* nameSolidsAndLevels: description = the file name, " (n)" when several solids; the level's name when it is empty */
            if (ns > 1) snprintf(S->desc, sizeof S->desc, "%s (%d)", base, i + 1); else snprintf(S->desc, sizeof S->desc, "%s", base);
            struct McLevel *L = mcam_level(Fp, S->level);
            if (!L || !mcam_level_named(L)) mcam_set_levname(Fp, S->level, S->desc);
            R.n_solids++;
        }
    } else if (ns) {
        if (kind == CAD_IGES) rp_skip("IGES solid (Mastercam's IGES reader has no solids path): imported as surfaces");
        else rp_skip("solids imported as surfaces (Solids is off)");
    }
    if (ns && !want_solids && kind == CAD_IGES) { /* the solids' faces become surfaces below */ }
    /* free shells, free faces -> trimmed surfaces (FUN_18002ea60's path; the faces of a solid too when solids are off or IGES) */
    OpFace *f = NULL; int nf = 0;
    if (!want_solids && ns) { if (occt_cad_faces(C, 1, &f, &nf, err, sizeof err) == 0) { R.n_surfs += cad_make_surfaces(Fp, f, nf, main_level, main_color); occt_prim_free(f, nf); } }
    f = NULL; nf = 0;
    if (occt_cad_faces(C, 0, &f, &nf, err, sizeof err) == 0) { R.n_surfs += cad_make_surfaces(Fp, f, nf, main_level, main_color); occt_prim_free(f, nf); }
    /* edge curves (config 3000): the edges of solids, shells and faces as wireframe */
    if (o->get_edges) { OccCurve *cu; int nc; if (occt_cad_curves(C, 0, &cu, &nc, err, sizeof err) == 0) { for (int i = 0; i < nc; i++) store_occ_curve(&cu[i], &a); occt_cad_curves_free(cu, nc); } }
    /* free wireframe */
    { OccCurve *cu; int nc; if (occt_cad_curves(C, 1, &cu, &nc, err, sizeof err) == 0) { for (int i = 0; i < nc; i++) store_occ_curve(&cu[i], &a); occt_cad_curves_free(cu, nc); } }
    { double *pt; int np; occt_cad_points(C, &pt, &np); for (int i = 0; i < np; i++) { double p[3] = { pt[3 * i], pt[3 * i + 1], pt[3 * i + 2] }; store_point(&a, p, 1, 0); } free(pt); }
    occt_cad_free(C);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ STL (mcstl.dll) */
/* FUN_1800040e0, convSTLImportLines off: StoreSTLFileAsSTLMesh(file, identity, &eptr) -> one PMESH_ID entity (main level / colour / attributes,
   store_ent), drawn (draw_ent). A file that makes no entry makes nothing: DoImport returns 1 and Mastercam says nothing - the same here (the
   reason goes to stderr only, a log for tests / cam_cli). */
static int import_stl_mesh(const char *path, char *msg, size_t msgn) {
    uint8_t rec[0x130]; StlPoly *p = NULL; int mixed = 0; char err[400] = "";
    int n = stl_mesh_from_file(path, rec, &p, &mixed, err, sizeof err);
    if (n <= 0) { fprintf(stderr, "stl import: %s\n", err); return 1; }   /* DoImport returns 1, no message */
    mcam_new_mesh(Fp, rec, p, n, mixed, main_level, main_color);
    R.n_meshes++; R.n_tris += n;
    if (mixed) rp_skip("the facets' colour bytes differ: the mesh cannot be saved to a Mastercam part (WriteEnhancedSTL's std::sort: NOT PORTED)");
    return 1;
}
/* rd_stl (mccore 0x1806d15f0), convSTLImportLines on: each triangle's edges p0-p1, p1-p2, p2-p0 (get_first_stl_triangle / get_next_stl_triangle)
   become lines (store_ent: main level / colour / attributes) when longer than the system tolerance (tol < length) and not equal to a line
   already made from the file. The made lines are kept sorted by key = (x2 + x1 + y1 + y2 + z1 + z2) * 0.5 (FUN_1806d1330, FUN_1806d13a0);
   FUN_1806ceb90 bsearches the key (LAB_1806ced60: equal when |a - b| < 3 tol), widens to the neighbours whose key is within 3 tol and asks
   check_ent_match (mccadutil 0x180068310, flags 0: L_ID ends pt_equal in either order, FUN_18006c2f0 / pt_equal |dx|, |dy|, |dz| < tol;
   the view DAT_180255680 is 0). Esc stops it (KeyPress 0x1b: not here, ours); an allocation failure: mccoreres 5434 "STL memory
   allocation error". The status line ("Reading " + file, 5000) is not shown here. */
typedef struct { double k; double p[6]; } StlKey;
static StlKey *sk; static int nsk, csk;
static double stl_key(const double *l) { return (l[3] + l[0] + l[1] + l[4] + l[2] + l[5]) * 0.5; }
static int pt_eq(const double *a, const double *b) { return fabs(a[0] - b[0]) < CTOL && fabs(a[1] - b[1]) < CTOL && fabs(a[2] - b[2]) < CTOL; }
static int stl_dup(const double *l) {
    double k = stl_key(l), w = CTOL * 3.0;
    int lo = 0, hi = nsk;                                  /* the first key not below k - 3 tol (the run bsearch + widening finds) */
    while (lo < hi) { int m = (lo + hi) / 2; if (sk[m].k - k <= -w) lo = m + 1; else hi = m; }
    for (int i = lo; i < nsk && fabs(sk[i].k - k) < w; i++) {
        const double *q = sk[i].p;
        if (pt_eq(l, q) ? pt_eq(l + 3, q + 3) : (pt_eq(l, q + 3) && pt_eq(l + 3, q))) return 1;
    }
    return 0;
}
static int stl_add(const double *l) {                      /* FUN_1806d13a0: grows by 1000, inserted in key order */
    if (nsk + 1 >= csk) { int c = csk + 1000; StlKey *q = realloc(sk, sizeof *sk * (size_t)c); if (!q) return -1; sk = q; csk = c; }
    double k = stl_key(l); int at = nsk;
    { int lo = 0, hi = nsk; while (lo < hi) { int m = (lo + hi) / 2; if (k < sk[m].k) hi = m; else lo = m + 1; } at = lo; }
    memmove(sk + at + 1, sk + at, sizeof *sk * (size_t)(nsk - at)); sk[at].k = k; memcpy(sk[at].p, l, sizeof sk[at].p); nsk++;
    return 0;
}
static int import_stl_lines(const char *path, char *msg, size_t msgn) {
    StlTriReader rd; double t[3][3];
    int ok = stl_tri_first(&rd, path, t);
    if (!rd.f) { fprintf(stderr, "stl import: %s: cannot open the file\n", path); return 1; }   /* SysFile::Open failed: nothing, no message (DoImport returns 1) */
    Att a = { main_color, main_level, 1, 1.0, 0 };
    free(sk); nsk = 0; csk = 1000; sk = calloc((size_t)csk, sizeof *sk);   /* FUN_1806d1360: room for 1000 (calloc 24000); failure -> 5434 */
    int err = !sk;
    while (ok && !err) {
        for (int s = 0; s < 3 && !err; s++) {
            int s2 = s + 1 > 2 ? 0 : s + 1;
            double l[6] = { t[s][0], t[s][1], t[s][2], t[s2][0], t[s2][1], t[s2][2] };
            if (CTOL < dist3(l, l + 3) && !stl_dup(l)) {
                store_line_raw(&a, l, l + 3);
                if (stl_add(l)) err = 1;
            }
        }
        if (!err) ok = stl_tri_next(&rd, t);
    }
    stl_tri_close(&rd); free(sk); sk = NULL; nsk = csk = 0;
    if (err) rp_skip("STL memory allocation error");          /* post_warning(0x153a) */
    return 1;
}

int cad_tol_setup(double tol) { CTOL = tol; return 0; }

int cad_import_file(McamFile *f, const char *path, int kind, const CadImportOpts *o, int color, int level, char *msg, size_t msgn) {
    Fp = f; main_color = color; main_level = level; rp_reset();
    if (kind == CAD_DWG) { snprintf(msg, msgn, "%s: DWG / DWF files are not read here (Mastercam reads them with the Teigha library, not available): NOT DECODED.", path); return 0; }
    int ok = 0;
    if (kind == CAD_DXF) { blank_ps = o->blank_ps; ok = import_dxf(path, msg, msgn); }
    else if (kind == CAD_STL) ok = o->stl_lines ? import_stl_lines(path, msg, msgn) : import_stl_mesh(path, msg, msgn);   /* no units conversion in mcstl.dll */
    else ok = import_occ(path, kind, mcam_is_metric(f) ? 1.0 : 1.0 / 25.4, o, msg, msgn);
    if (ok) rp_message(msg, msgn);
    return ok;
}
