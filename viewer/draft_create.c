/* draft_create - Mastercam 2017's LINEAR dimension creation (see draft_create.h).
 *
 * Port of mccore.dll (Ghidra VAs, image base 0x180000000); decompiles in decomp/draft/ ("file:line"), argument order and constants
 * checked in `objdump -d mccore.dll` where Ghidra truncated the call (noted "asm"):
 *   calc_linear_dimension @18024c480 (core_calcdim.c 797-1232)        -> dc_calc_linear
 *   FUN_18030f410 value text + extents (core_calcdim.c 5805-6412)      -> f410 (tolerance mode 0 only)
 *   get_ext @18031d040 -> FUN_180316750 extents (text1.c 238-449)      -> get_ext
 *   FUN_18024fc30 text half-widths (core_calcdim.c 5185-5260)          -> fc30
 *   FUN_180250410 layout driver (core_calcdim.c 5314-5598)             -> layout
 *   FUN_180252850 text + lines (core_dimlayout.c 2131-2241)            -> layout
 *   FUN_18024c160 text box, box_limits @18024b680 (core_dimgeo.c 3-37, 396-468)
 *   FUN_180255b80 dimension-line halves (core_dimgeo.c 40-308, asm 0x180255b80-0x180256449)
 *   FUN_18024b250 / FUN_180257270 / FUN_18024b570 inside / outside / extra segment (core_dimgeo.c 499-596, 612-825, 470-497)
 *   FUN_180254bc0 dim line x box edges (core_dimlayout2.c 207-294), l_ints @1804b3480 (core_dimgeo.c 826-990),
 *   l_hv_ints (exported, same body as decomp/pocket_b6/zigzag_decomp_zz_sys.c 68)
 *   FUN_180256450 witness lines (core_dimgeo.c 310-371)
 *   FUN_180252e50 record assembly (core_dimlayout.c 2245-2474), make_wl_single @180254950 (core_calcdim.c 2526-2648)
 *   FUN_180251460 text slot position, FUN_180252d60 / FUN_180252790 slot writer (core_calcdim.c 6585-6706, core_dimlayout2.c 165,
 *   asm 0x180252790)
 *   FUN_180251290 post-check / orientation re-derivation (core_calcdim.c 5640-5700), find_orient_angle, df_angle_hv,
 *   df_calc_orient_ang, df_angle_between_lines_rads, FUN_180241810 (core_createport.c), df_angle_of_line_rads @180241aa0 and
 *   AbsAngleTheSame @18023e3a0 (asm)
 *   FUN_18023d590 anchor / text offsets (core_calcdim.c 3618-3820), FUN_18023dad0 / FUN_18023e0b0 (core_createport.c 3-117),
 *   calc_linear_angle @18024c280 (asm)
 * Not ported (calc_linear_dimension calls them; see create_port.md): tolerance modes 1-3 (FUN_18030f410 / FUN_180251460 branches),
 * TrueType text extents (FUN_180316d40), DraftIsAssoc, FUN_1802579d0's graphics-view fallback, GetProdEnableBits two_sig rounding. */
#include "draft_create_int.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>




/* ------------------------------------------------------------------------------------------------ fonts (text.md 2.1) */
struct DcFonts { int n; int ok[8]; int16_t wid[8][256]; uint8_t has[8][256]; };
static int font_read(DcFonts *F, int k, const char *path) {     /* FUN_180368f10 (version >= 1000): u16 code, i32 k, k x 7-byte cmds */
    FILE *in = fopen(path, "rb"); if (!in) return 0;
    fseek(in, 0, SEEK_END); long L = ftell(in); fseek(in, 0, SEEK_SET);
    uint8_t *b = L > 0 ? malloc((size_t)L) : NULL; size_t n = b ? fread(b, 1, (size_t)L, in) : 0; fclose(in);
    if (n < 40 || U16(b, 0) < 1000) { free(b); return 0; }       /* legacy (< 1000) layout: not used by the shipped fonts */
    size_t o = 32; int32_t nl; memcpy(&nl, b + o, 4); o += 4;
    if (nl < 0 || o + (size_t)nl + 4 > n) { free(b); return 0; }
    o += (size_t)nl; int32_t ng; memcpy(&ng, b + o, 4); o += 4;
    for (int i = 0; i < 256; i++) F->wid[k][i] = -1;
    int got = 0;
    for (int i = 0; i < ng && o + 6 <= n; i++) {
        int code = U16(b, o); int32_t c; memcpy(&c, b + o + 2, 4); o += 6;
        if (c < 0 || o + (size_t)c * 7 > n) break;
        if (code < 256) { F->has[k][code] = 1; got = 1;
            for (int j = 0; j < c; j++) if (b[o + 7 * j] == 0) { F->wid[k][code] = b[o + 7 * j + 1]; break; } }   /* FUN_18031ca90: b1 of the first op 0 */
        o += (size_t)c * 7;
    }
    free(b); F->ok[k] = got; return got;
}
DcFonts *dc_fonts_load(const char *dir) {                          /* FUN_180365a90: font0..7, stops at the first missing one */
    if (!dir) return NULL;
    DcFonts *F = calloc(1, sizeof *F);
    for (int i = 0; i < 8; i++) {
        char p[1200]; int ok;
        snprintf(p, sizeof p, "%s/font%d.fnt", dir, i); ok = font_read(F, i, p);
        if (!ok) { snprintf(p, sizeof p, "%s/FONT%d.FNT", dir, i); ok = font_read(F, i, p); }   /* ours: the Windows disk's case */
        if (!ok) break;
        F->n = i + 1;
    }
    if (!F->n) { free(F); return NULL; }
    return F;
}
void dc_fonts_free(DcFonts *F) { free(F); }

/* FUN_18031dd60 (text1.c 2470): width in cells. Proportional: FUN_18031ca90 width byte / 255 (a missing glyph >= 0x21 falls back to
   0x20's, codes < 0x21 have none); none: 0.6 for a space, else 1.0. Fixed: 1.0 */
static double cw(const DcFonts *F, int font, int prop, int c) {
    if (!prop) return 1.0;
    int code = c;
    if (!F->has[font][code]) { if (code < 0x21 || !F->has[font][0x20]) code = -1; else code = 0x20; }
    if (code >= 0 && F->wid[font][code] >= 0) return F->wid[font][code] / 255.0;
    return c == 0x20 ? 0.6 : 1.0;
}

/* get_ext @18031d040 -> FUN_180316750 (text1.c 238-449), stroke fonts: *ph = block height (param_7), *pw = width (param_8).
   Returns 1 on failure (no fonts / TrueType font: FUN_180316d40 NOT PORTED) like get_ext. */
int get_ext(const DcFonts *F, const char *s, const uint8_t *fa, double *ph, double *pw) {
    if (!F || !F->n) return 1;                                     /* num_fnts == 0 */
    int font = fa[0x5c];
    if (font >= 8) return 1;                                       /* >= TRUETYPE_FONT: FUN_180316d40, NOT PORTED */
    if (font >= F->n || !F->ok[font]) {                            /* FUN_1803667b0 fallback (text.md 2.1): alternate font, then font 0 */
        int alt = fa[0x5d];
        if (alt < F->n && F->ok[alt]) font = alt; else if (F->ok[0]) font = 0; else return 1;
    }
    double h = D(fa, 0x28), e = D(fa, 0x30), xs = D(fa, 0x38), xl = D(fa, 0x40), ah = D(fa, 0x48), ae = D(fa, 0x50);
    int path = U16(fa, 0x58) >> 13, hz = path != 3 && path != 2, prop = fa[0x5a] & 1;
    size_t len = strlen(s); int nl = 0;
    for (const char *p = strchr(s, '\n'); p; p = strchr(p + 1, '\n')) nl++;
    if (!len || s[len - 1] != '\n') nl++;
    double cell = h / 0.8156862745098039, H, W;
    if (hz) { H = cell * xl * (nl - 1) + (nl - 1) * h + cell; W = -(h * e * xs); }
    else { double he = h * e; W = he * xs * (nl - 1) + nl * he; H = (cell - h) - cell * xl; }
    double mh = -1.0, mw = -1.0; int alt = 0;                      /* uVar16/18, uVar17/19 = -1.0 */
    const unsigned char *p = (const unsigned char *)s; unsigned c = *p;
    while (c) {
        if (c == 0x0e) alt = 1;
        if (c == 0x0f) alt = 0;
        if (c == '\r') p++;
        double d4, d20;
        if (alt) { d4 = ae * ah; d20 = hz ? ah / 0.8156862745098039 : ah; }
        else { d20 = hz ? cell : h; d4 = h * e; }
        c = *p;
        if ((unsigned char)(c - 0x0e) < 2) { }
        else if (c == '\n') {
            if (hz) { if (mw <= W) mw = W; W = -(d4 * xs); }
            else { if (mh <= H) mh = H; H = (cell - h) - d20 * xl; }
        } else if (hz) W = cw(F, font, prop, (int)c) * d4 + d4 * xs + W;
        else H = d20 * xl + d20 + H;
        if (!c) break;
        p++; c = *p;
    }
    if (hz) { if (mw > 0) { if (mw <= W) mw = W; W = mw; } }
    else if (mh > 0) { if (mh <= H) mh = H; H = mh; }
    *ph = H; *pw = W;
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the value string */
/* FUN_18030ee50 decimal / scientific (copied from viewer/draft.c fmt_num, the corpus-checked port of draw.md 5): FUN_18030e2d0 zero
   suppression; CString::Format("%.*f") rounding an exact binary half away from zero; comma_period */
static void zeros(char *s, double v, double lim, int lead, int trail) {
    double l = 1.0 - TOL * lim;
    if (!lead && fabs(v * lim) < l && s[0] == '0') memmove(s, s + 1, strlen(s));
    if (!trail) { size_t n = strlen(s); while (n && s[n - 1] == '0') s[--n] = 0; if (n && s[n - 1] == '.') s[--n] = 0; }
    if (!*s) strcpy(s, "0");
}
static void fixed(char *out, size_t on, int digits, double v) {
    char b[400]; if (digits < 0) digits = 0; if (digits > 15) digits = 15;
    snprintf(b, sizeof b, "%.60f", fabs(v));
    char *dot = strchr(b, '.'); size_t ip = (size_t)(dot - b);
    char dg[400]; size_t n = 0; for (size_t i = 0; i < ip; i++) dg[n++] = b[i];
    for (int i = 0; i < digits; i++) dg[n++] = dot[1 + i];
    int up = dot[1 + digits] >= '5';
    for (long i = (long)n - 1; up && i >= 0; i--) { if (dg[i] == '9') dg[i] = '0'; else { dg[i]++; up = 0; } }
    char r[400]; size_t k = 0; if (up) r[k++] = '1';
    for (size_t i = 0; i < n; i++) { if (i == n - (size_t)digits && digits) r[k++] = '.'; r[k++] = dg[i]; }
    r[k] = 0; snprintf(out, on, "%s%s", v < 0 && strspn(r, "0.") != strlen(r) ? "-" : "", r);
}
int fmt_num(char *out, size_t on, int fmt, int digits, double v, int lead, int trail, int comma) {
    char b[300];
    if (fmt == 0) { fixed(b, sizeof b, digits, fabs(v)); zeros(b, v, 1.0, lead, trail); snprintf(out, on, "%s%s", v < 0 ? "-" : "", b); }
    else if (fmt == 1) snprintf(out, on, "%.*e", digits, v);
    else return 0;                                                 /* 2 engineering, 3 fractional, 4 architectural: NOT PORTED here */
    if (comma) { char *c = strchr(out, '.'); if (c) *c = ','; }
    return 1;
}
void replace_v(const char *t, const char *v, char *out, size_t on) {   /* DfReplaceTokens: every "%v" */
    size_t k = 0;
    for (const char *p = t; *p && k + 1 < on; ) {
        if (p[0] == '%' && p[1] == 'v') { for (const char *q = v; *q && k + 1 < on; q++) out[k++] = *q; p += 2; }
        else out[k++] = *p++;
    }
    out[k] = 0;
}

/* FUN_18030f410, tolerance mode (param_7) 0: value = *param_14 x param_25 (draft_scale); kind 1 linear / 3 radius ("R %s", "%s R."
   by radius_format, core_calcdim.c 5945-5951) / 4 diameter ("%c %s" 0xD8, "D %s", "%s Dia.", "%s D." by the diameter style,
   5966-5978); digits 0 keeps leading / trailing zeros (draft.c dim_value_t, 0x18030f520..549); the template (DAT_1813d8d40):
   the fresh value replaces "%v" for get_ext and the template is what is kept (6013-6024). Outputs the widths / heights arrays,
   their running sum / max (param_18 / param_19) and the string count 1. Returns 1, 0 = get_ext failed (*param_26 = 0), -1 = NOT PORTED */
int f410(Ctx *X, int kind, int sym, double value, char *text, double *w, double *h, double *maxw, double *toth, int *nstr) {
    const DcSettings *S = X->S;
    double v = value * S->scale;                                   /* GetProdEnableBits() & 1 (demo two_sig rounding): not modelled */
    memset(text, 0, TXT); *maxw = *toth = 0; w[0] = w[1] = w[2] = h[0] = h[1] = h[2] = 0; *nstr = 1;
    int digits = S->digits, lead = digits ? S->lead0 : 1, trail = digits ? S->trail0 : 1;
    char num[200];
    if (!fmt_num(num, sizeof num, S->coord_fmt, digits, v, lead, trail, S->comma)) { X->why = "coordinate format 2-4 (NOT PORTED)"; return -1; }
    if (kind == 3) snprintf(text, 0x12d, sym == 0 ? "R %s" : sym == 1 ? "%s R." : "%s", num);
    else if (kind == 4) {
        if (sym == 0) snprintf(text, 0x12d, "\xd8 %s", num);
        else snprintf(text, 0x12d, sym == 1 ? "D %s" : sym == 2 ? "%s Dia." : sym == 3 ? "%s D." : "%s", num);
    } else snprintf(text, 0x12d, "%s", num);
    char meas[TXT];
    if (X->in->tmpl) { replace_v(X->in->tmpl, text, meas, sizeof meas); snprintf(text, 0x12d, "%s", X->in->tmpl); }
    else snprintf(meas, sizeof meas, "%s", text);
    double eh, ew;
    if (get_ext(X->F, meas, X->fa, &eh, &ew)) return 0;
    w[0] = ew; h[0] = eh; *toth += eh; *maxw += ew;
    return 1;
}

/* ------------------------------------------------------------------------------------------------ geometry helpers */
void rot(double px, double py, double cx, double cy, double c, double s, double *ox, double *oy) {   /* rotate_pt @1804b4550 */
    double dx = px - cx, dy = py - cy, y = dy * c + dx * s;
    *ox = cx + (dx * c - dy * s); *oy = y + cy;
}
void rot_deg(P3 p, double deg, P3 *o) {                    /* df_rotate_pt -> df_rotate_pt_about_pt (core_draftwf.c 6077) about 0 */
    if (fabs(deg) >= TOL) { double c = cos(deg * RAD), s = sin(deg * RAD); rot(p.x, p.y, 0, 0, c, s, &o->x, &o->y); o->z = p.z; }
    else *o = p;
}
void w2v(const double m[9], const double w[3], P3 *v) {     /* world_to_view */
    v->x = m[0] * w[0] + m[1] * w[1] + m[2] * w[2]; v->y = m[3] * w[0] + m[4] * w[1] + m[5] * w[2]; v->z = m[6] * w[0] + m[7] * w[1] + m[8] * w[2];
}
int abs_same(double a, double b, int deg) { return fabs(fabs(a) - fabs(b)) < (deg ? ANGTOL : 1e-6); }   /* AbsAngleTheSame (asm) */
double ang_rads(double x0, double y0, double x1, double y1) {   /* df_angle_of_line_rads @180241aa0 (asm) */
    double dx = x1 - x0, dy = y1 - y0, a;
    if (dx * dx + dy * dy < 1e-12) a = 0;
    else { a = atan2(dy, dx); if (a < 0) a += TWOPI; while (a >= TWOPI) a -= TWOPI; while (a < 0) a += TWOPI; }
    if (abs_same(a, TWOPI, 0)) a = 0;
    if (fabs(a) < 1e-6) return 0;
    return a;
}

/* l_hv_ints (exported; decomp/pocket_b6/zigzag_decomp_zz_sys.c 68): K = 1: A horizontal, 2: A vertical; infinite lines */
static int l_hv_ints(int k, const double *A, const double *B, double *pt) {
    double a1 = k == 2 ? A[3] : A[2], a0 = k == 2 ? A[1] : A[0];
    if (fabs(a1 - a0) < 1e-12) return 0;
    double d7 = k == 1 ? B[3] - B[1] : B[2] - B[0];
    if (fabs(d7) < 1e-12) return 0;
    double o = k == 2 ? B[3] - B[1] : B[2] - B[0], d6 = sqrt(o * o + d7 * d7);
    if (d6 < 1e-12 || fabs(d7 / d6) < 1.5e-10) return 0;
    double t = k == 1 ? (A[1] - B[1]) / d7 : (A[0] - B[0]) / d7;
    pt[0] = (B[2] - B[0]) * t + B[0]; pt[1] = (B[3] - B[1]) * t + B[1];
    return 1;
}
/* l_ints @1804b3480 (core_dimgeo.c 826-990): infinite lines L1, L2 (x0, y0, x1, y1); 1 = intersection in PT */
int l_ints(const double *L1, const double *L2, double *pt) {
    double dy1 = L1[3] - L1[1], dx1 = L1[2] - L1[0], n1 = dy1 * dy1 + dx1 * dx1;
    if (n1 < 1e-24) return 0;
    if (dx1 * dx1 / n1 < 1e-24) return l_hv_ints(2, L1, L2, pt);
    if (dy1 * dy1 / n1 < 1e-24) return l_hv_ints(1, L1, L2, pt);
    double by0 = L2[1], by1 = L2[3], bx0 = L2[0], dx2s = (L2[2] - bx0) * (L2[2] - bx0), dy2s = (by1 - by0) * (by1 - by0), n2 = dy2s + dx2s;
    if (n2 < 1e-24) return 0;
    if (dx2s / n2 < 1e-24) return l_hv_ints(2, L2, L1, pt);
    if (dy2s / n2 < 1e-24) return l_hv_ints(1, L2, L1, pt);
    double d5 = bx0 - L1[0], t2 = TOL * TOL, d21 = bx0 - L2[2], d1 = by0 - by1;
    double det = d1 * dx1 - d21 * dy1, d15 = d5 * d1 - (by0 - L1[1]) * d21, d16 = d1 * dy1 + d21 * dx1;
    int ok = fabs(d16) >= t2 ? fabs(det / d16) >= TOL : fabs(det) >= t2;
    if (ok) { double t = d15 / det; pt[0] = L1[0] + dx1 * t; pt[1] = L1[1] + dy1 * t; return 1; }
    double l2 = sqrt(d21 * d21 + d1 * d1);                         /* collinear: the middle of the closest pair of end points */
    if (!(TOL < fabs(l2)) || !(fabs(d15 / l2) < TOL)) return 0;
    double best = 1e38; int i2 = 1, i1 = 1;                        /* 0x47d2ced32a16a1b1 */
    for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
        double d = hypot(L2[2 * b + 1] - L1[2 * a + 1], L2[2 * b] - L1[2 * a]);
        if (d < best) { best = d; i2 = b; i1 = a; }
    }
    pt[0] = (L2[2 * i2] + L1[2 * i1]) * 0.5; pt[1] = (L2[2 * i2 + 1] + L1[2 * i1 + 1]) * 0.5;
    return 1;
}
/* FUN_180254bc0 (core_dimlayout2.c 207; args asm 0x180254bc0): the horizontal line y = Y (x from XA to XB) against the 4 edges of BOX
   (corner order 0,1,3,2: table 0x1812d7944); DIR 0: the nearest edge x >= REF, 1: the nearest x <= REF; REF if none */
double f254bc0(int dir, double ref, double xa, double xb, const double *box, double y) {
    static const int ord[4] = { 0, 1, 3, 2 };
    int first = 1; double r = ref;
    for (int k = 0; k < 4; k++) {
        double L1[4] = { xa, y, xb, y }, L2[4] = { box[2 * ord[k]], box[2 * ord[k] + 1], box[2 * ord[(k + 1) & 3]], box[2 * ord[(k + 1) & 3] + 1] }, pt[2];
        if (!l_ints(L1, L2, pt)) continue;
        double x = pt[0];
        if (dir == 0) { if (ref <= x) { if (first) { first = 0; r = x; } else if (!(r - ref <= x - ref)) r = x; } }
        else if (x <= ref) { if (first) { first = 0; r = x; } else if (!(ref - r <= ref - x)) r = x; }
    }
    return r;
}
/* box_limits @18024b680 (core_dimgeo.c 396): half extents of the text block */
void box_limits(int mode, int nstr, double pad, const double *hs, const double *ws, double *bx, double *by) {
    if (mode == 0) { *bx = ws[0]; *by = hs[0]; }
    else if (mode == 1 || (mode == 2 && nstr == 3)) {
        if (nstr == 2) { *bx = pad + ws[0] + ws[1]; *by = hs[0] > hs[1] ? hs[0] : hs[1]; }
        else { *bx = pad + ws[0] + (ws[1] > ws[2] ? ws[1] : ws[2]); *by = pad + hs[1] + hs[2]; }
    } else if (mode == 2) { *bx = ws[0] > ws[1] ? ws[0] : ws[1]; *by = pad + hs[0] + hs[1]; }
    *bx *= 0.5; *by *= 0.5;
}

/* ------------------------------------------------------------------------------------------------ the record */
int16_t *npts(WL *w) { return (int16_t *)(void *)(w->b + 0x1a); }
/* make_wl_single @180254950: active 1, kind, reversed 0, arrow 3 + assoc 0 (u16 at +0x18), has-arrow, h, w, +0x88 = 0, next 0;
   kind > 0: arrow = has_arrow ? type : 3; kind < 2: aPointTestTol on each point (|coord| < tol -> 0) */
void make_wl_single(WL *w, int kind, int has, int type, double h, double wd) {
    w->b[0] = 1; w->b[1] = (uint8_t)kind; w->b[2] = 0; P16(w->b, 0x18, 3); w->b[4] = (uint8_t)has;
    PD(w->b, 8, h); PD(w->b, 0x10, wd); memset(w->b + 0x88, 0, 8); P32(w->b, 0x84, 0);
    if (kind > 0) w->b[0x18] = (uint8_t)(has ? type : 3);
    int n = *npts(w);
    for (int i = 0; kind < 2 && i < n && i < 4; i++) for (int k = 0; k < 3; k++) {
        double v = w->pts[i][k]; if (fabs(v) < TOL) v = 0; PD(w->b, 0x20 + 0x18 * i + 8 * k, v); }
}

/* FUN_18024fc30 (core_calcdim.c 5185): half widths the text needs, from the extents (H total height, W max width) of the first value
   string; out 9..12 feed FUN_180250410's one-sided placement. fa height 0x1813d5658 / extra_ln_sp 0x1813d5670 */
static void fc30(const Ctx *X, double ang, int tor, int ao, int orient, double hvp, double H, double W, double *o9, double *o10, double *o11, double *o12) {
    const DcSettings *S = X->S;
    double th = D(X->fa, 0x28), pad = D(X->fa, 0x40) * th, d1 = W * 0.5;
    if (!tor) {
        d1 += pad; *o9 = d1; if (S->arrow_type != 3) *o9 = d1 + S->arrow_h; *o10 = d1; *o11 = d1; *o12 = W;
    } else if (orient == 2 || TOL < fabs(hvp)) {
        if (orient == 1) { d1 += pad; *o9 = *o10 = *o11 = d1; }
        else {
            if (!ao) { *o9 = d1 + pad; *o10 = th * 2.0 + pad * 2.0 + TOL * 2.0 + d1; }
            else { *o9 = th * 2.0 + pad * 2.0 + TOL * 2.0 + d1; *o10 = d1 + pad; }
            *o11 = d1 + pad;
        }
        *o12 = W;
        double *o[4] = { o9, o10, o11, o12 };
        for (int i = 0; i < 4; i++) if (TOL < fabs(ang)) { double x, y; rot(*o[i], 0, 0, 0, cos(-ang), sin(-ang), &x, &y); *o[i] = fabs(x); }   /* FUN_1802571c0 */
        if (S->arrow_type != 3) *o9 = S->arrow_h + *o9;
    } else if (orient == 1) {
        double d2 = H * 0.5 + pad; *o9 = d2; if (S->arrow_type != 3) *o9 = d2 + S->arrow_h; *o10 = d2; *o11 = d2; *o12 = H;
    } else {
        if (!ao) { *o9 = d1 + pad; if (S->arrow_type != 3) *o9 = d1 + pad + S->arrow_h; }
        else *o9 = th * 2.0 + pad * 2.0 + TOL * 2.0 + d1;
        *o10 = d1 + pad; *o11 = d1 + pad; *o12 = W;
    }
}

/* FUN_18024b250 (core_dimgeo.c 499; args asm 0x180256077..0x180256118): text between the witness lines. A, B = witness x, TX text x,
   Y line y; x1 / x2 = the half lines' x (from the witness outward), n1 / n2 their counts */
static void f24b250(double a, double b, double tx, double y, int16_t *n1, double *x1, int16_t *n2, double *x2,
                    int ao, int nt, const double *box, int bx, double h, double pad, int lv) {
    double t5 = TOL * 5.0, d3, d4;
    h *= 2.0; x1[0] = a; x2[0] = b; *n2 = 2; *n1 = 2;
    if (bx == 0) {
        if (!ao) {
            d3 = f254bc0(1, tx, a, b, box, y); d4 = t5 + a; if (t5 + a <= d3 - pad) d4 = d3 - pad; x1[1] = d4;
            d3 = f254bc0(0, tx, a, b, box, y); d4 = b - t5; if (d3 + pad <= b - t5) d4 = d3 + pad; x2[1] = d4;
        } else { x1[1] = a - h; x2[1] = h + b; }
    } else if (!ao) {
        d4 = (b - a) * 0.5 + a; x1[1] = d4; x2[1] = d4;
        if (nt) {
            if (bx == 1) {
                d4 = f254bc0(0, tx, a, b, box, y); x1[1] = t5 + a; x2[1] = t5 + a; if (d4 + pad <= a) a = d4 + pad; x1[2] = a; *n1 = 3;
            } else {
                d4 = f254bc0(1, tx, a, b, box, y); x1[1] = b - t5; x2[1] = b - t5; if (b <= d4 - pad) b = d4 - pad; x2[2] = b; *n2 = 3;
            }
        }
        if (lv == 1) x1[1] = x2[0];
        else if (lv == 2) x2[1] = x1[0];
    } else if (bx == 1) {
        d3 = f254bc0(0, tx, a, b, box, y); d4 = a - t5; if (d3 + pad <= a - t5) d4 = d3 + pad; x1[1] = d4; x2[1] = h + b;
    } else {
        x1[1] = a - h; d3 = f254bc0(1, tx, a, b, box, y); d4 = t5 + b; if (t5 + b <= d3 - pad) d4 = d3 - pad; x2[1] = d4;
    }
}
/* FUN_180257270 (core_dimgeo.c 612; args asm 0x180255ded..0x180255e88): leader style 1 (dimension line under the text box) */
static void f257270(double p1, double p2, double p3, double p4, int16_t *n1, double *x1, int16_t *n2, double *x2,
                    int lv, int ao, const double *box, int bx, double h, int *v1, int *v2, double pad) {
    int *z = v2; double d1, d2, d3;
    h *= 2.0;
    if (bx == 0) {
        if (!ao) {
            x1[0] = p1; d1 = (p2 - p1) * 0.5 + p1; x1[1] = d1; *n1 = 2; x2[0] = p2; x2[1] = d1; *n2 = 2;
            if (lv == 1) { x1[1] = p2; *n2 = 2; }
            else { if (lv != 2) return; x2[1] = p1; *n1 = 2; z = v1; }
        } else {
            x1[0] = p1; x1[1] = p1 - h; d1 = (p2 - p1) * 0.5 + p1; x1[2] = d1; *n1 = 3; x2[0] = p2; x2[1] = h + p2; x2[2] = d1; *n2 = 3;
            if (lv == 1) { x1[2] = p2; *n2 = 2; }
            else { if (lv != 2) return; x2[2] = p1; *n1 = 2; z = v1; }
        }
    } else if (!ao) {
        if (bx == 1) {
            d1 = f254bc0(1, p3, p1, p2, box, p4); d2 = h + p1; x1[0] = p1; d1 -= pad; x1[1] = d2;
            if (p2 <= d2) x1[1] = p2 - TOL * 10.0;
            p1 = p1 - h; d3 = d1; if (p1 < d1) d3 = p1; x1[2] = d3; *n1 = 3;
            x2[0] = p2; x2[1] = d2; if (p2 <= d2) x2[1] = p2 - TOL * 10.0; *n2 = 2;
            if (lv == 1) x1[1] = p2;
            else { if (lv != 2) return; x2[0] = p2; if (d1 <= p1) p1 = d1; x2[1] = p1; *n2 = 2; *n1 = 2; z = v1; }
        } else {
            d1 = f254bc0(0, p3, p1, p2, box, p4) + pad; d2 = p2 - h; x1[0] = p1; x1[1] = d2; if (d2 <= p1) x1[1] = TOL * 10.0 + p1; *n1 = 2;
            x2[0] = p2; x2[1] = d2; if (d2 <= p1) x2[1] = TOL * 10.0 + p1;
            h = h + p2; d2 = d1; if (d1 < h) d2 = h; x2[2] = d2; *n2 = 3;
            if (lv == 1) { if (h <= d1) h = d1; x1[2] = h; *n1 = 3; *n2 = 2; }
            else { if (lv != 2) return; x2[1] = p1; z = v1; }
        }
    } else if (bx == 1) {
        d1 = f254bc0(1, p3, p1, p2, box, p4); d3 = p1 - h; d1 -= pad; x1[0] = p1; d2 = d1; if (d3 < d1) d2 = d3; x1[1] = d2; *n1 = 2;
        x2[0] = p2; x2[1] = h + p2; x2[2] = p1; *n2 = 3;
        if (lv == 1) { x1[2] = p2; *n1 = 3; *n2 = 2; }
        else { if (lv != 2) return; x2[0] = p2; x2[1] = h + p2; if (d3 < d1) d1 = d3; x2[2] = d1; *n2 = 3; x1[0] = p1; x1[1] = d3; *n1 = 2; z = v1; }
    } else {
        d2 = f254bc0(0, p3, p1, p2, box, p4); d1 = h + p2; d2 += pad; x1[0] = p1; x1[1] = p1 - h; x1[2] = p2; *n1 = 3;
        x2[0] = p2; d3 = d2; if (d2 < d1) d3 = d1; x2[1] = d3; *n2 = 2;
        if (lv == 1) { if (d2 < d1) d2 = d1; x1[2] = d2; x2[0] = p2; x2[1] = d1; *n2 = 2; }
        else { if (lv != 2) return; x2[2] = p1; *n2 = 3; *n1 = 2; z = v1; }
    }
    *z = 0;
}

/* FUN_180255b80 (core_dimgeo.c 40; control flow from the asm 0x180255b80-0x180256449): the two dimension-line halves (wl points in
   the frame rotated by -ANG about T, then rotated back) into L1->pts / L2->pts, their counts and visibility */
static void f255b80(const Ctx *X, int lv, int ao, int ls, double pad, double ang, int nt, double h,
                    const P3 *Plo, const P3 *Phi, const P3 *T, const double *box, WL *L1, WL *L2) {
    double c = cos(-ang), s = sin(-ang), angdeg = ang * DEG, hvp = X->in->hvp;
    double ax, ay, bxx, by_; rot(Plo->x, Plo->y, T->x, T->y, c, s, &ax, &ay); rot(Phi->x, Phi->y, T->x, T->y, c, s, &bxx, &by_);
    double rb[8]; for (int k = 0; k < 4; k++) rot(box[2 * k], box[2 * k + 1], T->x, T->y, c, s, &rb[2 * k], &rb[2 * k + 1]);
    double miny = rb[1]; for (int k = 1; k < 4; k++) if (!(rb[2 * k + 1] > miny)) miny = rb[2 * k + 1];
    double tx = T->x, x1[4] = { 0 }, x2[4] = { 0 }, y, e[4] = { 0 };
    int bx, vis1, vis2, extra = 0; WL *ew = NULL;
    int outside = tx < ax || bxx < tx;
    int flip = !nt && fabs(hvp) > TOL && fabs((fabs(hvp) + 90.0) - angdeg) < TOL;   /* 0x180255f23..f43 / 0x180256045..6071 */
    if (ls) {                                                      /* 0x180255de5: leader style -> FUN_180257270 */
        bx = !outside ? 0 : tx > ax ? 2 : 1;
        int v1 = 1, v2 = 1; y = miny - pad;
        f257270(ax, bxx, tx, y, npts(L1), x1, npts(L2), x2, lv, ao, rb, bx, h, &v1, &v2, pad);
        vis1 = v1; vis2 = v2;
    } else {
        if (!outside) bx = 0;
        else {                                                     /* 0x180255e9b */
            double mid;
            if (fabs(angdeg - 270.0) < TOL || fabs(angdeg - 180.0) < TOL || fabs(angdeg - 90.0) < TOL || fabs(angdeg) < TOL) mid = Plo->x + Phi->x;
            else mid = bxx + ax;
            bx = tx > mid * 0.5 ? 2 : 1;
            if (flip) bx = bx == 1 ? 2 : 1;
        }
        if (bx != 0 && (!nt || flip)) {                            /* 0x180255f71: horizontal text outside: an extra segment */
            int use1 = (ax >= tx && bxx >= tx) ? !(lv == 2 && !ao) : (lv == 1 && !ao);
            ew = use1 ? L1 : L2;
            /* FUN_18024b570 (core_dimgeo.c 470) with the unrotated box, param_10 = param_11 = 0 */
            double xb1, xb0;
            if (bx == 1) { xb1 = pad + box[2]; xb0 = xb1 + h * 2.0; } else { xb1 = box[0] - pad; xb0 = xb1 - h * 2.0; }
            rot(xb0, T->y, T->x, T->y, c, s, &e[0], &e[1]); rot(xb1, T->y, T->x, T->y, c, s, &e[2], &e[3]);
            y = e[1]; extra = 1;
        } else y = T->y;
        if (bx != 0) {                                             /* 0x180256028 */
            bx = (ax >= tx && bxx >= tx) ? 1 : 2;
            if (flip) bx = bx == 1 ? 2 : 1;
        }
        vis1 = ((lv - 1) & 0xfffd) == 0; vis2 = (uint16_t)(lv - 2) <= 1;
        f24b250(ax, bxx, tx, y, npts(L1), x1, npts(L2), x2, ao, nt, rb, bx, h, pad, lv);
    }
    L1->b[3] = (uint8_t)vis1; L2->b[3] = (uint8_t)vis2;
    /* 0x180256120: the perpendicular through A (angle param_8 - pi/2 = 0) meets the line: shift (0 when x1[0] = A.x) */
    double shift = 0, l1[4], l2[4] = { x1[0], y, x2[0], y }, pt[2];
    rot(x1[0], y, ax, ay, cos(0.0), sin(0.0), &l1[0], &l1[1]); l1[2] = ax; l1[3] = ay;
    if (l_ints(l1, l2, pt)) shift = pt[0] - x1[0];
    double ca = cos(ang), sa = sin(ang);
    for (int i = 0; i < *npts(L1) && i < 4; i++) { rot(shift + x1[i], y, T->x, T->y, ca, sa, &L1->pts[i][0], &L1->pts[i][1]); L1->pts[i][2] = T->z; }
    for (int i = 0; i < *npts(L2) && i < 4; i++) { rot(shift + x2[i], y, T->x, T->y, ca, sa, &L2->pts[i][0], &L2->pts[i][1]); L2->pts[i][2] = T->z; }
    if (extra) {
        if (ao) (*npts(ew))--;
        for (int k = 0; k < 2; k++) { int i = *npts(ew); if (i > 3) break;
            rot(e[2 * k] + shift, e[2 * k + 1], T->x, T->y, ca, sa, &ew->pts[i][0], &ew->pts[i][1]); ew->pts[i][2] = T->z; (*npts(ew))++; }
    }
}

/* FUN_180256450 (core_dimgeo.c 310): witness i from Pi + gap.u to Di + ext.u (u = unit(Di - Pi)); start = end when gap >= |Di - Pi|;
   z = D1.z; visibility from the witness mode; 2 points */
static void f256450(double g, double x, const P3 *P1, const P3 *P2, const double *D1, const double *D2, int wv, WL *W1, WL *W2) {
    const P3 *P[2] = { P1, P2 }; const double *Dd[2] = { D1, D2 }; WL *W[2] = { W1, W2 };
    for (int i = 0; i < 2; i++) {
        double ux = Dd[i][0] - P[i]->x, uy = Dd[i][1] - P[i]->y, L = sqrt(ux * ux + uy * uy);
        if (L == 0.0) ux = uy = 0; else { ux /= L; uy /= L; }
        W[i]->pts[0][0] = ux * g + P[i]->x; W[i]->pts[0][1] = uy * g + P[i]->y;
        W[i]->pts[1][0] = W[i]->pts[0][0]; W[i]->pts[1][1] = W[i]->pts[0][1];
        if (g < L) { W[i]->pts[1][0] = ux * x + Dd[i][0]; W[i]->pts[1][1] = uy * x + Dd[i][1]; }
        W[i]->pts[0][2] = W[i]->pts[1][2] = D1[2];
        *npts(W[i]) = 2;
    }
    W1->b[3] = ((wv - 1) & 0xfffd) == 0; W2->b[3] = (uint16_t)(wv - 2) < 2;
}

/* FUN_180252790 (asm): the slot font = FA, angle (+2 pi when negative) at +0x10, h-align 1 / v-align 2 (& 0xe9ff | 0x900) */
void slot_font(uint8_t *dst, const uint8_t *fa, double ang) {
    memcpy(dst, fa, 0x60);
    if (ang < 0.0) ang += TWOPI;
    PD(dst, 0x10, ang);
    P16(dst, 0x58, (U16(dst, 0x58) & 0xe9ff) | 0x900);
}

/* FUN_180250410 + FUN_180252850 + FUN_180252e50: text, halves, witnesses and the record (tolerance mode 0) */
static int layout(Ctx *X, uint8_t *E, int vm, const P3 *P1, const P3 *P2, P3 *T, double len, double ang, int autoal,
                  int tor, int wv, int lv, int view, int ao, int ls, int sym, double f18, double f19, double f20, double f21) {
    const DcSettings *S = X->S;
    double fang = tor ? 0.0 : ang;                                 /* DAT_1813d5640 (core_calcdim.c 5419-5422) */
    double c = cos(-ang), s = sin(-ang);
    if (autoal) {                                                  /* centred: T to the middle of the dimension line (5424-5441) */
        double Ax, Ay, Bx, By; rot(P1->x, P1->y, T->x, T->y, c, s, &Ax, &Ay); rot(P2->x, P2->y, T->x, T->y, c, s, &Bx, &By); (void)Bx; (void)By;
        double nx, ny; rot(len * 0.5 + Ax, T->y, T->x, T->y, cos(ang), sin(ang), &nx, &ny); T->x = nx; T->y = ny;
    }
    /* one-sided witness / leader placement (asm 0x1802505f7-0x18025086e) */
    double thr = f18;
    if (!ls && !ao && S->arrow_type != 3) thr = S->arrow_h * 2.1 + f18;
    if (!X->in->ctrl && (wv > 0 || (lv > 0 && S->arrow_type != 3))) {
        double Ax, Ay, Bx, By; rot(P1->x, P1->y, T->x, T->y, c, s, &Ax, &Ay); rot(P2->x, P2->y, T->x, T->y, c, s, &Bx, &By);
        double hh = f18 * 0.5, tx = T->x, hi_t = tx + hh, lo_t = tx - hh;
        int side = fabs(Bx - tx) <= fabs(Ax - tx) ? 2 : 1, go = 1; double v = 0;
        if (wv == side || wv == 3) v = ((lv == side || lv == 3) && S->arrow_type != 3) ? f19 : f21;
        else if ((lv == side || lv == 3) && S->arrow_type != 3) v = f19;
        else go = 0;
        if (go) {
            double lo, hi, base = side == 1 ? Ax : Bx;
            if ((side == 1) == !ao) { lo = base - f20; hi = base + v; } else { lo = base - v; hi = base + f20; }
            if (!(hi_t < hh + lo) && !(hi - hh < lo_t)) {
                double nx = (thr > len) ? (side == 1 ? lo : hi) : (fabs(hi - tx) > fabs(lo - tx) ? lo : hi), ox, oy;
                rot(nx, T->y, T->x, T->y, cos(ang), sin(ang), &ox, &oy); T->x = ox; T->y = oy;
            }
        }
    }
    const P3 *lo = ang >= 0.0 ? P1 : P2, *hi = ang >= 0.0 ? P2 : P1;   /* 5463-5481 */
    /* FUN_180252850: the value text (DAT_1813d6212: keep the buffer), box, halves, witnesses */
    double pad6 = D(X->fa, 0x40) * D(X->fa, 0x28), ws[3] = { 0 }, hs[3] = { 0 }, maxw, toth; int nstr;
    char *text = X->text;                                          /* DAT_1813d5690 (as calc_linear's first call left it) */
    if (!S->keep_text) {
        int kind = vm == 1 ? 3 : vm == 2 ? 4 : 1;
        int r = f410(X, kind, sym, len, text, ws, hs, &maxw, &toth, &nstr);
        if (r < 0) return -1;
        if (r == 0) return 0;                                      /* get_ext failed: *param_26 = 0 (the layout continues in Mastercam with
                                                                      stale extents; ours stops) */
    } else {
        if (get_ext(X->F, text, X->fa, &hs[0], &ws[0])) return 0;
        nstr = 1;
    }
    double pad4 = ls ? 0.0 : pad6, bx, by, box[8];                 /* FUN_18024c160 */
    box_limits(S->tol_mode, nstr, pad6, hs, ws, &bx, &by);
    box[0] = box[4] = T->x - (bx + pad4); box[1] = box[3] = T->y - (by + pad4);
    box[2] = box[6] = bx + pad4 + T->x; box[5] = box[7] = by + pad4 + T->y;
    for (int k = 0; k < 4; k++) rot(box[2 * k], box[2 * k + 1], T->x, T->y, cos(fang), sin(fang), &box[2 * k], &box[2 * k + 1]);
    WL L1, L2, W1, W2; memset(&L1, 0, sizeof L1); memset(&L2, 0, sizeof L2); memset(&W1, 0, sizeof W1); memset(&W2, 0, sizeof W2);
    P3 Tc = *T;
    f255b80(X, lv, ao, ls, pad6, ang, !tor, D(X->fa, 0x28), lo, hi, &Tc, box, &L1, &L2);
    f256450(S->gap, S->ext, lo, hi, L1.pts[0], L2.pts[0], wv, &W1, &W2);
    /* FUN_180252e50 (core_dimlayout.c 2245-2474) */
    E[0x7c9] = (uint8_t)autoal; E[0x308] = 0;
    PD(E, 0x310, T->x); PD(E, 0x318, T->y); PD(E, 0x320, T->z);
    P16(E, 0x328, view); E[0x7ca] = 0; PD(E, 0x330, S->scale); PD(E, 0x338, len); PD(E, 0x350, S->gap); PD(E, 0x358, S->ext);
    E[0x7cb] = (uint8_t)S->comma; E[0x7cc] = (uint8_t)S->lead0; E[0x7cf] = (uint8_t)S->trail0; E[0x7cd] = (uint8_t)S->digits;
    P32(E, 0x7a2, 0x10001); P32(E, 0x7a6, 0x10001); P16(E, 0x7ac, wv); E[0x7d0] = (uint8_t)S->constr3d; P16(E, 0x7ae, lv);
    int t1 = S->arrow_type, t2 = S->arrow_type;
    if (ls) { t1 = ((lv - 1) & 0xfffd) == 0 ? S->arrow_type : 3; if (lv != 2 && lv != 3) t2 = 3; }
    P16(E, 0x7c0, S->coord_fmt); P16(E, 0x7b0, ao); P16(E, 0x7c2, S->frac_units); P16(E, 0x7b2, sym); P16(E, 0x7b4, ls);
    P16(E, 0x7b6, S->tol_mode); P16(E, 0x7b8, 0);                  /* tolerance mode 0: no tolerance strings */
    make_wl_single(&W1, 0, 0, t1, S->arrow_h, S->arrow_w); memcpy(E + 0x378, W1.b, 0x98);
    make_wl_single(&L1, 1, 1, t1, S->arrow_h, S->arrow_w); memcpy(E + 0x4a8, L1.b, 0x98);
    /* FUN_180251460 slot 0: x = T.x - bx + w0 / 2 (y = T.y, tolerance mode 0), rotated about T by the text angle */
    double tbx, tby, px, py; box_limits(S->tol_mode, nstr, D(X->fa, 0x40) * D(X->fa, 0x28), hs, ws, &tbx, &tby);
    rot((T->x - tbx) + ws[0] * 0.5, T->y, T->x, T->y, cos(fang), sin(fang), &px, &py);
    slot_font(E + 0x5d8, X->fa, fang); PD(E, 0x638, px); PD(E, 0x640, py); PD(E, 0x648, T->z);
    memset(E + 0x650, 0, 0x58); { size_t n = strlen(text); memcpy(E + 0x650, text, n < 0x57 ? n : 0x57); } memset(E + 0x790, 0, 8);   /* strcpy_bb; slot +0x1b8 = 0 */
    make_wl_single(&L2, 1, 1, t2, S->arrow_h, S->arrow_w); memcpy(E + 0x540, L2.b, 0x98);
    make_wl_single(&W2, 0, 0, t2, S->arrow_h, S->arrow_w); memcpy(E + 0x410, W2.b, 0x98);
    return 1;
}

/* calc_linear_angle @18024c280 (asm): the reference angle (degrees) of a linear dimension */
double dc_linear_angle(const uint8_t ap1[0x98], const uint8_t ap2[0x98], const double vm[9], int orient, double hvp) {
    P3 v1, v2; w2v(vm, (const double *)(const void *)(ap1 + 8), &v1); w2v(vm, (const double *)(const void *)(ap2 + 8), &v2); v1.z = v2.z = 0;   /* aPoint2Plane(.., 0.0) */
    double r = 0.0;
    if (orient == 2) {
        P3 r1, r2; rot_deg(v1, -hvp, &r1); rot_deg(v2, -hvp, &r2);
        if (fabs(r1.x - r2.x) < TOL) orient = 1;
        if (fabs(r1.y - r2.y) < TOL) return hvp;
    }
    if (orient == 1) r = 90.0;
    else if (orient == 2) {
        double dx = v2.x - v1.x, dy = v2.y - v1.y, a = 0.0;
        if (!(1e-12 > dx * dx + dy * dy)) { a = atan2(dy, dx); if (a < 0) a += TWOPI; }
        r = a * DEG - hvp;
    }
    return r + hvp;
}
/* FUN_18023dad0 (core_createport.c 3): the placement point from the text slot (tolerance mode 0: the text position) */
int placement(const Ctx *X, const uint8_t *E, double *pl) {
    pl[0] = D(E, 0x638); pl[1] = D(E, 0x640); pl[2] = D(E, 0x648);
    double h[3] = { 0 }, w[3] = { 0 }, bx, by;
    if (get_ext(X->F, (const char *)E + 0x650, E + 0x5d8, &h[0], &w[0])) return 0;
    int m = I16(E, 0x7b6); if (m == 3) m = 1;
    box_limits(m, I16(E, 0x7b8) + 1, D(E, 0x618) * D(E, 0x600), h, w, &bx, &by);
    double a = D(E, 0x5e8), c = cos(a), s = sin(a);
    pl[0] -= (c * w[0] + s * h[0]) * 0.5; pl[1] -= (s * w[0] + c * h[0]) * 0.5;
    pl[0] = s * by + c * bx + pl[0]; pl[1] = s * bx + c * by + pl[1];
    return 1;
}
/* FUN_18023e0b0 (core_createport.c 79): text offset from definition point P (rotated by -angle) -> body+0x2f8 / +0x300 / +0x308 */
void offsets(const Ctx *X, uint8_t *E, const P3 *P, double ang) {
    if (fabs(ang) < ANGTOL)                                        /* FUN_18023de60 (core_createport.c): linear -> calc_linear_angle; types
                                                                      1-4 -> 0 (5 / 6 perpendicular / tangent: not ported, callers refuse) */
        ang = E[0x308] == 0 ? dc_linear_angle(E + 0x70, E + 0x108, X->in->vm, E[0x68], D(E, 0x1a0)) : 0.0;
    double pl[3]; placement(X, E, pl);
    if (fabs(ang) > ANGTOL) { double ox, oy; rot(pl[0], pl[1], P->x, P->y, cos(ang * -RAD), sin(ang * -RAD), &ox, &oy); pl[0] = ox; pl[1] = oy; }
    double dx = fabs(P->x - pl[0]), dy = fabs(P->y - pl[1]);
    PD(E, 0x360, pl[0] < P->x ? -dx : dx); PD(E, 0x368, pl[1] < P->y ? -dy : dy); PD(E, 0x370, D(E, 0x320));
}

DcResult dc_calc_linear(uint8_t E[0x810], const DcInput *in, const DcSettings *S, const DcFonts *F) {
    DcResult R; memset(&R, 0, sizeof R); R.orient = in->orient;
    Ctx X; memset(&X, 0, sizeof X); X.S = S; X.F = F; X.in = in; memcpy(X.fa, S->fa, 0x60);
    if (S->tol_mode != 0) { R.not_ported = 1; R.why = "linear tolerance mode 1-3 (NOT PORTED)"; return R; }
    if (!in->view) { R.not_ported = 1; R.why = "view 0 (graphics-view fallback NOT PORTED)"; return R; }
    int vm = in->value_mode > 2 ? 0 : in->value_mode;              /* l.897-900 */
    int orient = in->orient, ao = S->arrows_out == 1;              /* local_2c0 = (_dd_global == 1) */
    int autoal = S->auto_align;
    uint8_t ap1[0x98], ap2[0x98]; memcpy(ap1, in->ap1, 0x98); memcpy(ap2, in->ap2, 0x98);
    /* l.913-924: view coordinates; the depth from the cursor (P1 when the construction view is 3-D) */
    P3 P1, P2, T, Dp;
    w2v(in->vm, (const double *)(const void *)(ap1 + 8), &P1); w2v(in->vm, (const double *)(const void *)(ap2 + 8), &P2); w2v(in->vm, in->cursor, &T);
    w2v(in->vm, S->constr3d ? (const double *)(const void *)(ap1 + 8) : in->cursor, &Dp);
    P1.z = P2.z = T.z = Dp.z;
    double len = sqrt((P2.y - P1.y) * (P2.y - P1.y) + (P2.x - P1.x) * (P2.x - P1.x));
    if (len < TOL) return R;                                       /* invalid (l.927-930) */
    if (orient == 2) {                                             /* l.931-940: parallel collapses to V / H */
        P3 a, b; rot_deg(P1, -in->hvp, &a); rot_deg(P2, -in->hvp, &b);
        if (fabs(a.x - b.x) < TOL) orient = 1;
        if (fabs(a.y - b.y) < TOL) orient = 0;
    }
    /* header and body (l.941-966; InitDIM / FUN_180249820 defaults for the rest are not reproduced: zeros) */
    P32(E, 0, 0x40); memset(E + 0x20, 0, 8); E[6] = (uint8_t)S->color; P32(E, 8, (uint32_t)S->level); memcpy(E + 0x10, S->attrib, 16);
    P16(E, 4, 1); E[0x48] = S->ver[0]; E[0x49] = S->ver[1];
    E[0x68] = (uint8_t)orient; memcpy(E + 0x70, ap1, 0x98); memcpy(E + 0x108, ap2, 0x98);
    PD(E, 0x1a0, in->hvp); E[0x1a9] = (uint8_t)vm; E[0x308] = 0; P16(E, 0x328, in->view); E[0x7ca] = 0; PD(E, 0x330, S->scale);
    E[0x7cb] = (uint8_t)S->comma; E[0x7cc] = (uint8_t)S->lead0; E[0x7cf] = (uint8_t)S->trail0; E[0x7cd] = (uint8_t)S->digits; E[0x7d0] = (uint8_t)S->constr3d;
    /* l.968-1003: the text angle (radians); text orientation off: into (-90, 90] degrees */
    double a = 0.0;
    if (orient == 1) a = 90.0;
    if (orient == 2) {
        double dx = P2.x - P1.x, dy = P2.y - P1.y;
        if (1e-12 <= dy * dy + dx * dx) { a = atan2(dy, dx); if (a < 0) a += TWOPI; } else a = 0.0;
        a = a * DEG - in->hvp;
    }
    a += in->hvp;
    double ang;
    if (S->text_orient == 0) {
        ang = a * RAD;
        if (TWOPI < ang) { do ang -= TWOPI; while (TWOPI < ang); }  /* restrict_angle(0, 2 pi) */
        if (ang < 0.0) { do ang += TWOPI; while (ang < 0.0); }
        a = ang * DEG;
        if (90.0 < a && a < 180.0) a -= 180.0;
        if (180.0 < a && a < 270.0) a -= 180.0;
        if (fabs(a - TWOPI) < 1e-06) a = 0.0;                      /* degrees against the radian constants, as compiled */
        if (fabs(a - 3.141592653589793) < 1e-06) a = 0.0;
        if (fabs(a) < 1e-06) a = 0.0;
    }
    ang = a * RAD;
    /* l.1009-1091: P1 left / bottom (swap; the record keeps the pick order, the caller's aPoints are swapped in place) */
    P3 r1, r2, rt; rot_deg(P1, -in->hvp, &r1); rot_deg(P2, -in->hvp, &r2); rot_deg(T, -in->hvp, &rt);
    int sw = 0;
    if (orient == 0 && r2.x < r1.x) sw = 1;
    if (orient == 1 && r2.y < r1.y) sw = 1;
    if (orient == 2) {
        double dx = r1.x - r2.x;
        int keep = r1.x <= r2.x && (fabs(dx) >= TOL || ((r1.y < r2.y || r1.x <= rt.x) && (fabs(dx) >= TOL || (r2.y < r1.y || rt.x <= r1.x))));
        if (!keep) sw = 1;
    }
    if (sw) { P3 t = r1; r1 = r2; r2 = t; t = P1; P1 = P2; P2 = t; }
    R.swapped = sw;
    /* l.1093-1105: FUN_18030f410 on the view distance (asm 0x18024cb9e-0x18024cd10: value = local_2f8 = |P1P2|) -> extents for
       FUN_18024fc30 */
    int sym = vm == 1 ? S->radius_fmt : S->diam_fmt, kind = vm == 1 ? 3 : vm == 2 ? 4 : 1;
    double ws[3], hs[3], W, H; int nstr;
    int r = f410(&X, kind, sym, len, X.text, ws, hs, &W, &H, &nstr);
    if (r < 0) { R.not_ported = 1; R.why = X.why; return R; }
    if (r == 0) { R.why = "get_ext failed (fonts)"; return R; }
    /* l.1106-1167 / asm 0x18024cd1f-0x18024cec4: the measured length and "text outside" */
    double dl; int outside;
    if (orient == 0) { dl = r2.x - r1.x; outside = rt.x < r1.x || r2.x < rt.x; }
    else if (orient == 1) { dl = r2.y - r1.y; outside = rt.y < r1.y || r2.y < rt.y; }
    else {
        double dx = P2.x - P1.x, dy = P2.y - P1.y;                 /* asm 0x18024cd41-0x18024cd8b: the angle becomes the raw view angle */
        if (1e-12 > dx * dx + dy * dy) ang = 0.0; else { ang = atan2(dy, dx); if (ang < 0.0) ang += TWOPI; }
        dl = sqrt((r2.x - r1.x) * (r2.x - r1.x) + (r2.y - r1.y) * (r2.y - r1.y) + (r2.z - r1.z) * (r2.z - r1.z));
        double d1 = sqrt((r1.x - rt.x) * (r1.x - rt.x) + (r1.y - rt.y) * (r1.y - rt.y) + (r1.z - rt.z) * (r1.z - rt.z));
        double d2 = sqrt((r2.x - rt.x) * (r2.x - rt.x) + (r2.y - rt.y) * (r2.y - rt.y) + (r2.z - rt.z) * (r2.z - rt.z));
        outside = dl < d1 || dl < d2;
    }
    (void)outside;                                                 /* uVar16: only FUN_18024fc30's unused param_3 */
    if (!(TOL < dl)) goto done;
    {
        int aout = ao;
        if (dl < S->arrow_h * 2.1 && S->arrow_type != 3) aout = 1;  /* l.1171-1175 */
        if (!autoal && !in->ctrl) {                                /* l.1176-1208: centring snap */
            if (orient == 0) { if (fabs(rt.x - (r2.x - dl * 0.5)) < S->sel_dist * 0.5) autoal = 1; }
            else if (orient == 1) { if (fabs(rt.y - (r2.y - dl * 0.5)) < S->sel_dist * 0.5) autoal = 1; }
            else {
                double mx = (r1.x + r2.x) * 0.5, my = (r1.y + r2.y) * 0.5, mz = (r1.z + r2.z) * 0.5, ux = r2.x - r1.x, uy = r2.y - r1.y, uz = r2.z - r1.z;
                double L = sqrt(uy * uy + ux * ux + uz * uz);      /* proj_pt_ln_3d (core_calcdim.c 5703) */
                if (TOL <= L) {
                    double t = ((rt.y - r1.y) * uy + (rt.x - r1.x) * ux + (rt.z - r1.z) * uz) / (L * L);
                    double px = (1 - t) * r1.x + t * r2.x, py = (1 - t) * r1.y + t * r2.y, pz = (1 - t) * r1.z + t * r2.z;
                    if (0.0 < t && t < 1.0 && sqrt((py - my) * (py - my) + (px - mx) * (px - mx) + (pz - mz) * (pz - mz)) < S->sel_dist * 0.5) autoal = 1;
                }
            }
        }
        double o9 = 0, o10 = 0, o11 = 0, o12 = 0;
        if (S->text_orient == 0 || !in->ctrl) fc30(&X, ang, S->text_orient, aout, orient, in->hvp, H, W, &o9, &o10, &o11, &o12);
        r = layout(&X, E, vm, &P1, &P2, &T, dl, ang, autoal, S->text_orient, S->wit_vis, S->ldr_vis, in->view, aout, S->ldr_style, sym, o12, o9, o10, o11);
        if (r < 0) { R.not_ported = 1; R.why = X.why; return R; }
        if (r == 0) { R.why = "get_ext failed (fonts)"; goto done; }
        /* FUN_180251290 (core_calcdim.c 5640): leader starts apart, P1 P2 apart; orientation re-derived from the dimension line */
        double l1x = D(E, 0x4c8), l1y = D(E, 0x4d0), l1z = D(E, 0x4d8), l2x = D(E, 0x560), l2y = D(E, 0x568), l2z = D(E, 0x570);
        if (sqrt((l2y - l1y) * (l2y - l1y) + (l2x - l1x) * (l2x - l1x) + (l2z - l1z) * (l2z - l1z)) < TOL) goto done;
        P3 q1, q2; w2v(in->vm, (const double *)(const void *)(E + 0x78), &q1); w2v(in->vm, (const double *)(const void *)(E + 0x110), &q2);
        if (sqrt((q2.y - q1.y) * (q2.y - q1.y) + (q2.x - q1.x) * (q2.x - q1.x) + 0.0) < TOL) goto done;
        double a1 = ang_rads(l1x, l1y, l2x, l2y), a2 = ang_rads(q1.x, q1.y, q2.x, q2.y), d = a1 <= a2 ? a2 - a1 : a1 - a2;
        if (3.141592653589793 < d) d -= TWOPI;                     /* FUN_180241810 */
        d = fabs(d) * DEG;                                         /* df_angle_between_lines */
        if (fabs(d - 180.0) < ANGTOL || fabs(d) < ANGTOL) { E[0x68] = 2; PD(E, 0x1a0, 0); }
        else {
            double la = ang_rads(l1x, l1y, l2x, l2y) * DEG; int hv;   /* find_orient_angle -> df_angle_of_line, df_angle_hv */
            if (abs_same(la, 270.0, 1)) hv = 1; else if (abs_same(la, 180.0, 1)) hv = 0; else if (abs_same(la, 90.0, 1)) hv = 1; else if (abs_same(la, 0.0, 1)) hv = 0; else hv = -1;
            if (hv < 0) {                                          /* df_calc_orient_ang (core_createport.c 510) */
                double v = la;
                while (v <= -360.0) v += 360.0;
                while (360.0 <= v) v -= 360.0;
                if (180.0 <= v) v -= 180.0;
                if (90.0 < v) v -= 180.0;
                if (abs_same(v, 90.0, 1)) v = 0.0;
                else if (fabs(v) >= ANGTOL) {                      /* DfCheckAngle (core_create2.c 3) */
                    double o = v;
                    if (abs_same(o, 360.0, 1)) o = v < 0 ? -360.0 : 360.0;
                    if (abs_same(o, 270.0, 1)) o = v < 0 ? -270.0 : 270.0;
                    if (abs_same(o, 180.0, 1)) o = v < 0 ? -180.0 : 180.0;
                    if (abs_same(o, 90.0, 1)) o = v < 0 ? -90.0 : 90.0;
                    v = fabs(o) >= ANGTOL ? o : 0.0;
                } else v = 0.0;
                PD(E, 0x1a0, v); E[0x68] = 0;
            } else { E[0x68] = (uint8_t)hv; PD(E, 0x1a0, 0); }
        }
        /* FUN_18023d590 case 0 (core_calcdim.c 3637-3672): placement point, text offsets from the nearer definition point, anchor */
        double pl[3];
        if (!placement(&X, E, pl)) goto done;
        PD(E, 0x310, pl[0]); PD(E, 0x318, pl[1]); PD(E, 0x320, pl[2]);
        P3 v1, v2; w2v(in->vm, (const double *)(const void *)(E + 0x78), &v1); w2v(in->vm, (const double *)(const void *)(E + 0x110), &v2);
        double ha = D(E, 0x1a0), d1, d2;
        offsets(&X, E, &v1, ha);
        d1 = E[0x68] == 0 ? fabs(D(E, 0x360)) : E[0x68] == 1 ? fabs(D(E, 0x368)) : sqrt(D(E, 0x360) * D(E, 0x360) + D(E, 0x368) * D(E, 0x368));
        E[0x1a8] = 1;
        offsets(&X, E, &v2, ha);
        d2 = E[0x68] == 0 ? fabs(D(E, 0x360)) : E[0x68] == 1 ? fabs(D(E, 0x368)) : sqrt(D(E, 0x360) * D(E, 0x360) + D(E, 0x368) * D(E, 0x368));
        E[0x1a8] = 2;
        if (d1 <= d2) { offsets(&X, E, &v1, ha); E[0x1a8] = 1; }
        R.valid = 1;                                               /* DraftIsAssoc: NOT PORTED */
        { double th, tw; if (!get_ext(F, (const char *)E + 0x650, E + 0x5d8, &th, &tw)) { R.text_w = tw; R.text_h = th; } }
    }
done:
    P16(E, 0x7b0, (int8_t)S->arrows_out);                         /* l.1221 (short)(char)dd_global */
    E[0x7c9] = (uint8_t)S->auto_align;                             /* l.1223 */
    R.orient = orient;
    return R;
}

/* ------------------------------------------------------------------------------------------------ the empty record (creation) */
/* init_attribs_dim @1802524b0 (core_create.c 607-645), statement by statement */
void dc_attribs_dim(uint8_t fa[0x60], const DcDimFont *g) {
    PD(fa, 0x50, g->alt_aspect); fa[0x5d] = (uint8_t)g->alt_font; PD(fa, 0x48, g->alt_h); PD(fa, 0, 0); PD(fa, 8, 0);
    PD(fa, 0x30, g->aspect); PD(fa, 0x20, g->rot); PD(fa, 0x40, g->extra_ln_sp); PD(fa, 0x38, g->extra_sp); fa[0x5c] = (uint8_t)g->font;
    PD(fa, 0x28, g->height); PD(fa, 0x18, g->slant); PD(fa, 0x10, g->angle); fa[0x5e] = (uint8_t)g->text_bx;
    uint16_t w58 = U16(fa, 0x58), w5a = U16(fa, 0x5a), u1, u2;
    w58 &= 0xfdff; w58 |= 0x100;                                   /* horizontal alignment 1 */
    w5a &= 0xfff3;                                                 /* mirror 0 */
    w58 = (uint16_t)((w58 & 0x1fff) | (g->text_path << 13));
    u1 = (uint16_t)(w5a ^ g->space_sty);
    w58 &= 0xebff; w58 |= 0x800;                                   /* vertical alignment 2 */
    u2 = (uint16_t)((u1 & 1) ^ w5a); w5a = u2;
    u1 = (uint16_t)(((w58 ^ g->alt_color) & 0xf) ^ w58); w58 = u1;
    w5a = (uint16_t)((g->fill & 1) * 2 | (u2 & 0xd));
    w58 = (uint16_t)(w58 ^ (((g->fill_color << 4) ^ u1) & 0xf0));
    P16(fa, 0x58, (int16_t)w58); P16(fa, 0x5a, (int16_t)w5a);
}
void dc_apoint_world(uint8_t ap[0x98], const double p[3], int kind) {
    memset(ap, 0, 0x98);                                           /* (the 0x98 buffer: aPointInitWorld writes +0 .. +0x8f; +0x20 / +0x90.. stay 0) */
    ap[0] = (uint8_t)kind;
    for (int k = 0; k < 3; k++) PD(ap, 8 + 8 * k, fabs(p[k]) < TOL ? 0.0 : p[k]);   /* aPointTestTol @1800246f0 (core_draft_rec.c 2109-2123) */
    PD(ap, 0x78, 1.0); PD(ap, 0x80, 1.0);
}
void dc_apoint_ent(uint8_t ap[0x98], int slot, uint64_t eptr, double t) {
    memcpy(ap + 0x28 + 0x18 * slot, &eptr, 8); PD(ap, 0x30 + 0x18 * slot, t); PD(ap, 0x38 + 0x18 * slot, 0.0);   /* not a SURF_ID: t2 = 0 */
}
/* InitDimWL @180249e40 (core_draftwf.c 181-231) */
static void init_wl(uint8_t *w, int kind, const DcSettings *S) {
    memset(w, 0, 0x98); w[1] = (uint8_t)kind; w[2] = 0; w[3] = 1; w[4] = 1;
    PD(w, 8, S->arrow_h); PD(w, 0x10, S->arrow_w); w[0x18] = (uint8_t)S->arrow_type; P16(w, 0x1a, 2);
}
void dc_init_dim(uint8_t E[0x810], const DcSettings *S, int view) {
    memset(E, 0, 0x810);                                           /* InitDIM: memset(ent, 0, 0x2938) */
    E[0x48] = S->ver[0]; E[0x49] = S->ver[1]; P32(E, 0, 0x40); P16(E, 4, 1); E[6] = (uint8_t)S->color; P32(E, 8, (uint32_t)S->level);
    memcpy(E + 0x10, S->attrib, 16);                               /* +0x20 eptr 0, +0x28 0 */
    uint8_t *b = E + 0x68;                                         /* FUN_180249820 */
    P16(b, 0x2c0, view ? view : 1);                                /* GetActiveViewNo, 0 -> 1 */
    PD(b, 0x2c8, S->scale); PD(b, 0x2e0, S->neg_tol); PD(b, 0x2d8, S->pos_tol); PD(b, 0x2e8, S->gap); PD(b, 0x2f0, S->ext);
    double o[3] = { 0, 0, 0 };                                     /* aPointInitWorld(body+8 / +0xa0, &body+0x2a8 (zeroed), 'K') */
    dc_apoint_world(b + 8, o, 'K'); dc_apoint_world(b + 0xa0, o, 'K');
    P16(b, 0x140, 1);                                              /* anchor 1, value mode 0 */
    init_wl(b + 0x310, 0, S); init_wl(b + 0x3a8, 0, S); init_wl(b + 0x440, 1, S); init_wl(b + 0x4d8, 1, S);
    uint8_t *dt = b + 0x570;                                       /* InitDT @180249b60 (core_draftwf.c 61-103): InitDimFA = the same
                                                                      font_attr as init_attribs_dim from zero (core_draftwf.c 110-174) */
    memcpy(dt, S->fa, 0x60); dt[0x5f] = 0; P16(dt, 0x5a, U16(dt, 0x5a) & 0xf);
    memcpy(dt + 0xd0, dt, 0x60); PD(dt, 0xf8, S->tol_h);
    uint8_t *dm = b + 0x738;                                       /* InitDM @180249a10 (core_draft_rec2.c 581-647) */
    P32(dm, 0, 0x10000); P32(dm, 4, 0x10001); P16(dm, 8, 1); P32(dm, 0xc, 0x30003);
    P16(dm, 0x10, S->arrows_out); P16(dm, 0x12, S->radius_fmt); P16(dm, 0x14, S->ldr_style); P16(dm, 0x16, S->tol_mode); P16(dm, 0x18, 1);
    P16(dm, 0x1a, S->metric != 0); P32(dm, 0x1c, 0); P16(dm, 0x20, S->coord_fmt); P16(dm, 0x22, S->frac_units); P16(dm, 0x24, 0);
    b[0x75e] = (uint8_t)S->din_tol; b[0x75f] = (uint8_t)S->din_chr; b[0x760] = (uint8_t)S->din_val;
    b[0x763] = (uint8_t)S->comma; b[0x764] = (uint8_t)S->lead0; b[0x767] = (uint8_t)S->trail0; b[0x765] = (uint8_t)S->digits;
    b[0x768] = (uint8_t)S->constr3d; P16(b, 0x761, 1);
}
/* determine_h_or_v @180240300 (core_create.c 3-44) with DfHvpAngle HVP: the definition points rotated by -HVP, df_angle_of_line ->
   df_angle_hv (core_createport.c 577-603): 0 horizontal, 1 vertical, -1 neither (the caller's orientation is then left alone) */
int dc_line_hv(const double p1v[3], const double p2v[3], double hvp) {
    P3 a = { p1v[0], p1v[1], p1v[2] }, b = { p2v[0], p2v[1], p2v[2] }, ra, rb;
    rot_deg(a, -hvp, &ra); rot_deg(b, -hvp, &rb);
    double la = ang_rads(ra.x, ra.y, rb.x, rb.y) * DEG;
    if (abs_same(la, 270.0, 1)) return 1;
    if (abs_same(la, 180.0, 1)) return 0;
    if (abs_same(la, 90.0, 1)) return 1;
    if (abs_same(la, 0.0, 1)) return 0;
    return -1;
}

/* ------------------------------------------------------------------------------------------------ shared with draft_ord.c */
/* the helpers above, for the ORDINATE port (viewer/draft_ord.c: calc_ordinate_dimension calls the same mccore functions) */
int dc_text_ext(const DcFonts *F, const char *s, const uint8_t fa[0x60], double *h, double *w) { return get_ext(F, s, fa, h, w); }
int dc_value_text(const DcSettings *S, const DcFonts *F, int kind, int sym, double value, const char *tmpl, char text[0x387],
                  double ws[3], double hs[3], double *W, double *H, int *nstr, const char **why) {
    DcInput in; memset(&in, 0, sizeof in); in.tmpl = tmpl;
    Ctx X; memset(&X, 0, sizeof X); X.S = S; X.F = F; X.in = &in; memcpy(X.fa, S->fa, 0x60);
    int r = f410(&X, kind, sym, value, text, ws, hs, W, H, nstr);
    if (why) *why = X.why;
    return r;
}
void dc_box_limits(int mode, int nstr, double pad, const double *hs, const double *ws, double *bx, double *by) { box_limits(mode, nstr, pad, hs, ws, bx, by); }
void dc_make_wl(uint8_t wl[0x98], int kind, int has, int type, double h, double wd) {
    WL w; memcpy(w.b, wl, 0x98);
    for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) w.pts[i][k] = D(wl, 0x20 + 0x18 * i + 8 * k);
    make_wl_single(&w, kind, has, type, h, wd);
    memcpy(wl, w.b, 0x98);
}
void dc_slot_font(uint8_t dst[0x60], const uint8_t fa[0x60], double ang) { slot_font(dst, fa, ang); }
