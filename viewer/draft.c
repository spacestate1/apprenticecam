/* draft - drafting entities read and drawn as mccore draws them (see draft.h).
 *
 * Sources (Mastercam 2017 x64 mccore.dll; decompiles decomp/draft/, specs formats/drafting/):
 *   GetDraftWireframe @180312270, draw_dimension @1803134b0, draw_note @180313730, FUN_180313970 (wl chain), draw_wl_single
 *   (core_drawwl.c 74-444), FUN_1803126e0 (arrowheads, core_drawwl.c 1183-1563), FUN_180314cf0 (arc arrow offset), FUN_180313380
 *   (dimension text), drw_string -> FUN_18031a950 (stroke-font layout, text1.c 1180), FUN_18031d180 (start pen), FUN_18031dd60
 *   (widths), FUN_18031e120 (advance), FUN_18031ef90 (glyph point), FUN_1803199e0 (glyph commands), FUN_180368f10 (font file >= 1000),
 *   FUN_180365a90 (font discovery), mTableRead (memory table). draw.md / text.md / records.md cite the lines.
 * "ours" marks display glue: arcs are emitted as polylines (the callback receives arcs in Mastercam), filled arrowheads as filled
 * polygons (Mastercam fills them with OpenGL). NOT PORTED cases are skipped and counted (DraftStats). */
#include "draft.h"
#include "mcam_load.h"
#include "mcam_grp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TOL (mc_systol.tol)                                         /* tol_exref (config 1000, mc_systol) */
#define PI 3.14159265358979323846

static int16_t I16(const uint8_t *b, size_t o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static uint16_t U16(const uint8_t *b, size_t o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static int32_t I32(const uint8_t *b, size_t o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static double D(const uint8_t *b, size_t o) { double v; memcpy(&v, b + o, 8); return v; }

/* ------------------------------------------------------------------------------------------------ the part */
typedef struct { int32_t id; const uint8_t *p; size_t n; } MT;     /* memory table entry (DB_ID 0xE block) */
typedef struct { const uint8_t *e; uint32_t n; } Rec;
struct DraftDoc { uint8_t *d; size_t len; Rec *r; int nr; MT *mt; int nmt; };

static void add_mt(DraftDoc *D_, int32_t id, const uint8_t *p, size_t n) {
    D_->mt = realloc(D_->mt, sizeof *D_->mt * (size_t)(D_->nmt + 1)); D_->mt[D_->nmt++] = (MT){ id, p, n };
}
static const MT *mt_find(const DraftDoc *d, int32_t id) {        /* mTableFindData */
    for (int i = 0; id && i < d->nmt; i++) if (d->mt[i].id == id) return &d->mt[i];
    return NULL;
}
DraftDoc *draft_load(const char *path) {
    FILE *in = fopen(path, "rb"); if (!in) return NULL;
    DraftDoc *d = calloc(1, sizeof *d);
    fseek(in, 0, SEEK_END); long L = ftell(in); fseek(in, 0, SEEK_SET);
    if (L > 0) { d->d = malloc((size_t)L); d->len = fread(d->d, 1, (size_t)L, in); }
    fclose(in);
    if (mcam_part_newer(d->d, d->len)) { free(d->d); free(d); return NULL; }   /* a newer release's part: unreadable (mcam_grp.h) */
    /* the record framing of nci_write.c nciw_part_load (u32 length + record; 0x200 sub 0xF + op_group array, sub 0xE + the memory
       table [0x28 hdr: i32 id @0, u64 size @0x10][size bytes] until id 0 (mTableRead, records.md 0), 0x20000 + its Parasolid blob) */
    const uint8_t *b = d->d; size_t len = d->len, off = 0;
    while (off + 4 <= len) {
        uint32_t n; memcpy(&n, b + off, 4);
        if (n == 0 || off + 4 + n > len) break;
        const uint8_t *rec = b + off + 4; uint32_t cls = (uint32_t)I32(rec, 0);
        if (n >= 0x70 && (cls == 0x40 || cls == 0x10 || cls == 0x20)) { d->r = realloc(d->r, sizeof *d->r * (size_t)(d->nr + 1)); d->r[d->nr++] = (Rec){ rec, n }; }
        off += 4 + n;
        if (n >= 0x6a && cls == 0x200) {
            uint16_t sub = U16(rec, 0x68);
            if (sub == 0xF && off + 4 <= len) { size_t gl = mcam_grp_block(b + off, len - off, NULL, 0, NULL); off = gl ? off + gl : len; }   /* + member entries (mcam_grp.h) */
            else if (sub == 0xE) {
                while (off + 0x28 <= len) {
                    int32_t id = I32(b, off); uint64_t sz; memcpy(&sz, b + off + 0x10, 8); off += 0x28;
                    if (id == 0) break;
                    if (sz > len - off) { off = len; break; }
                    add_mt(d, id, b + off, (size_t)sz); off += sz;
                }
            }
        } else if (n >= 0x16c && cls == 0x20000) {
            uint8_t ver = rec[0x49]; uint32_t xs; memcpy(&xs, rec + 0x168, 4);
            if (xs > len - off) break;
            off += xs + 4 + (ver > 0x1b ? 0x100 : 0x40) + 1 + 1 + 4 + 4 + 8;
            if (off + 4 > len) break;
            uint32_t cnt; memcpy(&cnt, b + off, 4); off += 4;
            for (uint32_t i = 0; i < cnt && off + 8 <= len; i++) { uint32_t z; memcpy(&z, b + off + 4, 4); if (8 + (size_t)z > len - off) { off = len; break; } off += 8 + z; }
        }
    }
    return d;
}
DraftDoc *draft_doc_new(void) { return calloc(1, sizeof(DraftDoc)); }
void draft_doc_add_rec(DraftDoc *d, const uint8_t *E, uint32_t n) { d->r = realloc(d->r, sizeof *d->r * (size_t)(d->nr + 1)); d->r[d->nr++] = (Rec){ E, n }; }
void draft_doc_add_mt(DraftDoc *d, int32_t id, const uint8_t *p, size_t n) { add_mt(d, id, p, n); }
void draft_free(DraftDoc *d) { if (!d) return; free(d->d); free(d->r); free(d->mt); free(d); }
int draft_count(const DraftDoc *d) { return d ? d->nr : 0; }
const uint8_t *draft_record(const DraftDoc *d, int i, uint32_t *n) { if (!d || i < 0 || i >= d->nr) return NULL; if (n) *n = d->r[i].n; return d->r[i].e; }

/* ------------------------------------------------------------------------------------------------ fonts (text.md 2.1) */
typedef struct { uint8_t op, b[6]; } Cmd;
typedef struct { int code; Cmd *c; int n; } Glyph;
typedef struct { int ok; Glyph *g; int ng; } Font;
struct DraftFonts { Font f[8]; int n; };
static int font_read(Font *F, const char *path) {                 /* FUN_180368f10 (version >= 1000) */
    FILE *in = fopen(path, "rb"); if (!in) return 0;
    fseek(in, 0, SEEK_END); long L = ftell(in); fseek(in, 0, SEEK_SET);
    uint8_t *b = L > 0 ? malloc((size_t)L) : NULL; size_t n = b ? fread(b, 1, (size_t)L, in) : 0; fclose(in);
    if (n < 40 || U16(b, 0) < 1000) { free(b); return 0; }        /* the legacy (< 1000) layout is not used by the shipped fonts */
    size_t o = 32; int32_t nl = I32(b, o); o += 4;
    if (nl < 0 || o + (size_t)nl + 4 > n) { free(b); return 0; }
    o += (size_t)nl; int32_t ng = I32(b, o); o += 4;
    for (int i = 0; i < ng && o + 6 <= n; i++) {
        int code = U16(b, o); int32_t k = I32(b, o + 2); o += 6;
        if (k < 0 || o + (size_t)k * 7 > n) break;
        F->g = realloc(F->g, sizeof *F->g * (size_t)(F->ng + 1));
        Glyph *g = &F->g[F->ng++]; g->code = code; g->n = k; g->c = malloc(sizeof(Cmd) * (size_t)(k ? k : 1));
        for (int j = 0; j < k; j++) { g->c[j].op = b[o]; memcpy(g->c[j].b, b + o + 1, 6); o += 7; }
    }
    free(b); F->ok = F->ng > 0; return F->ok;
}
DraftFonts *draft_fonts_load(const char *dir) {                    /* FUN_180365a90: font0..7, stops at the first missing one */
    if (!dir) return NULL;
    DraftFonts *fs = calloc(1, sizeof *fs);
    for (int i = 0; i < 8; i++) {
        char p[1200]; int ok = 0;
        snprintf(p, sizeof p, "%s/font%d.fnt", dir, i); ok = font_read(&fs->f[i], p);
        if (!ok) { snprintf(p, sizeof p, "%s/FONT%d.FNT", dir, i); ok = font_read(&fs->f[i], p); }   /* ours: the Windows disk's case */
        if (!ok) break;
        fs->n = i + 1;
    }
    if (!fs->n) { free(fs); return NULL; }
    return fs;
}
void draft_fonts_free(DraftFonts *fs) {
    if (!fs) return;
    for (int i = 0; i < 8; i++) { for (int j = 0; j < fs->f[i].ng; j++) free(fs->f[i].g[j].c); free(fs->f[i].g); }
    free(fs);
}
static const Glyph *glyph(const Font *F, int code) { for (int i = 0; i < F->ng; i++) if (F->g[i].code == code) return &F->g[i]; return NULL; }

/* ------------------------------------------------------------------------------------------------ drawing state */
typedef struct {
    const DraftDoc *d; const DraftFonts *fs; const DraftOut *o; DraftStats *st;
    int view; double vm[9]; int vok; double depth; int color;
} G;
static void nd(G *g, const char *what) {
    size_t L = strlen(g->st->note);
    if (!strstr(g->st->note, what) && L + strlen(what) + 4 < sizeof g->st->note) snprintf(g->st->note + L, sizeof g->st->note - L, "%s%s", L ? "; " : "", what);
}
/* view_to_world: the view's rows transposed (mcam_std_view: reserved views 1..8) */
static void v2w(const G *g, const double v[3], double w[3]) {
    for (int i = 0; i < 3; i++) w[i] = g->vm[i] * v[0] + g->vm[3 + i] * v[1] + g->vm[6 + i] * v[2];
}
static void w2v(const G *g, const double w[3], double v[3]) {
    for (int i = 0; i < 3; i++) v[i] = g->vm[3 * i] * w[0] + g->vm[3 * i + 1] * w[1] + g->vm[3 * i + 2] * w[2];
}
static int set_view(G *g, int view) {
    g->view = view ? view : 1;
    g->vok = mcam_std_view(g->view, g->vm);
    return g->vok;
}
/* emit a polyline given in VIEW coordinates (x, y) at the depth */
static void emit2(G *g, const double *xy, int n, int closed, int fill) {
    double *w = malloc(sizeof(double) * 3 * (size_t)n);
    for (int i = 0; i < n; i++) { double v[3] = { xy[2 * i], xy[2 * i + 1], g->depth }; v2w(g, v, w + 3 * i); }
    g->o->poly(g->o->ctx, w, n, closed, fill, g->color, g->view);
    free(w);
}
static void line2(G *g, double ax, double ay, double bx, double by) {   /* FUN_1803144a0: emitted if length >= tol */
    if (hypot(bx - ax, by - ay) < TOL) return;
    double p[4] = { ax, ay, bx, by }; emit2(g, p, 2, 0, 0);
}
static void arc2(G *g, double cx, double cy, double r, double a0, double sw) {   /* FUN_180314360: emitted if radius >= tol */
    if (r < TOL) return;
    int n = (int)ceil(fabs(sw) / (2 * PI) * 72); if (n < 2) n = 2;      /* ours: display tessellation */
    double *p = malloc(sizeof(double) * 2 * (size_t)(n + 1));
    for (int i = 0; i <= n; i++) { double a = a0 + sw * i / n; p[2 * i] = cx + r * cos(a); p[2 * i + 1] = cy + r * sin(a); }
    emit2(g, p, n + 1, 0, 0); free(p);
}

/* ------------------------------------------------------------------------------------------------ arrowheads (draw.md 2.3) */
static void arrow(G *g, double x0, double y0, double tx, double ty, double h, double w, int type) {
    if (fabs(h) < TOL) return;
    double th = hypot(tx - x0, ty - y0) <= TOL ? 0 : atan2(ty - y0, tx - x0);
    if (th < 0) th += 2 * PI;
    switch (type) {
    case 0: case 1: case 2: case 10: {
        if (fabs(w) < TOL) return;
        double al = atan((w / 2) / h), l = h / cos(al);
        double w1x = tx - l * cos(th + al), w1y = ty - l * sin(th + al), w2x = tx - l * cos(th - al), w2y = ty - l * sin(th - al);
        if (type == 2) { double p[6] = { tx, ty, w1x, w1y, w2x, w2y }; emit2(g, p, 3, 1, 1); return; }   /* ours: filled polygon */
        line2(g, tx, ty, w1x, w1y); line2(g, tx, ty, w2x, w2y);
        if (type != 0) line2(g, w1x, w1y, w2x, w2y);               /* (type - 1) & 0xf6 == 0: 1, 2, 10 close the base */
        return; }
    case 4: case 5:
        if (type == 5) {                                           /* ours: filled circle as a polygon */
            double p[72]; for (int i = 0; i < 36; i++) { p[2 * i] = tx + h / 2 * cos(2 * PI * i / 36); p[2 * i + 1] = ty + h / 2 * sin(2 * PI * i / 36); }
            emit2(g, p, 36, 1, 1); return; }
        arc2(g, tx, ty, h / 2, 0, 2 * PI); return;
    case 6: case 7: {
        double bx = tx - h * cos(th), by = ty - h * sin(th), mx = (tx + bx) / 2, my = (ty + by) / 2, hx, hy;
        int vert = fabs(th - PI / 2) < TOL || fabs(th - 3 * PI / 2) < TOL;
        if (!vert) { hx = h / 2; hy = w / 2; } else { hx = w / 2; hy = h / 2; }
        if (fabs(th) < TOL || fabs(th - PI) < TOL || vert) th = 0;
        double c[8] = { mx - hx, my - hy, mx + hx, my - hy, mx + hx, my + hy, mx - hx, my + hy };
        for (int i = 0; i < 4; i++) { double dx = c[2 * i] - mx, dy = c[2 * i + 1] - my;
            c[2 * i] = mx + dx * cos(th) - dy * sin(th); c[2 * i + 1] = my + dx * sin(th) + dy * cos(th); }
        if (type == 7) { emit2(g, c, 4, 1, 1); return; }
        line2(g, c[6], c[7], c[0], c[1]); line2(g, c[2], c[3], c[4], c[5]); line2(g, c[0], c[1], c[2], c[3]); line2(g, c[6], c[7], c[4], c[5]);
        return; }
    case 8: case 9: {
        if (fabs(th) < TOL || fabs(th - PI) < TOL) th = 0;
        double ct = cos(th), s = sin(th);
#define RX(a, b) (tx + (a) * ct - (b) * s)
#define RY(a, b) (ty + (a) * s + (b) * ct)
        if (type == 8) { line2(g, RX(-h / 2, -h / 2), RY(-h / 2, -h / 2), RX(h / 2, h / 2), RY(h / 2, h / 2)); return; }
        arc2(g, RX(h / 2, 0), RY(h / 2, 0), h / 2, th + PI / 2, PI / 2);
        arc2(g, RX(-h / 2, 0), RY(-h / 2, 0), h / 2, th + 3 * PI / 2, PI / 2);
#undef RX
#undef RY
        return; }
    default: return;                                               /* 3: none */
    }
}

/* ------------------------------------------------------------------------------------------------ witness / leader (draw.md 2) */
static void wl_single(G *g, const uint8_t *wl, size_t avail, int force_none) {
    if (avail < 0x98 || !wl[0] || !wl[3]) return;
    int kind = wl[1], type = force_none ? 3 : wl[0x18];
    double h = D(wl, 8), w = D(wl, 0x10);
    if (kind == 2) {                                               /* arc (core_drawwl.c 331-434) */
        double cx = D(wl, 0x20), cy = D(wl, 0x28), r = D(wl, 0x38), a0 = D(wl, 0x40), sw = D(wl, 0x48);
        if (type == 3) { arc2(g, cx, cy, r, a0, sw); return; }
        int rev = wl[2] != 0; double a = rev ? a0 + sw : a0, tx = cx + r * cos(a), ty = cy + r * sin(a);
        double len = fabs(sw) * r;
        if (h > r || len <= TOL) { arc2(g, cx, cy, r, a0, sw); return; }
        double dd = (type == 4 || type == 5) ? h / 2 : h, t = rev ? (len - dd) / len : dd / len;
        double ab = a0 + sw * t, bx = cx + r * cos(ab), by = cy + r * sin(ab);
        double off = D(wl, 0x50);                                  /* FUN_180314cf0: the arrow moved along the arc */
        if (off > 1e-6) { double o2 = rev ? -off : off;
            double rb = atan2(by - cy, bx - cx) + o2, rt = atan2(ty - cy, tx - cx) + o2;
            bx = cx + r * cos(rb); by = cy + r * sin(rb); tx = cx + r * cos(rt); ty = cy + r * sin(rt); ab = rb; }
        if (type == 4 || type == 5 || type == 6 || type == 7 || type == 10) {   /* trimmed at the arrow base */
            double na0 = rev ? a0 : ab, nsw = rev ? (ab - a0) : (a0 + sw - ab);
            if (fabs(nsw) < fabs(sw)) arc2(g, cx, cy, r, na0, nsw);
        } else arc2(g, cx, cy, r, a0, sw);
        arrow(g, bx, by, tx, ty, h, w, type);
        return;
    }
    int n = I16(wl, 0x1a); if (n < 2) return;
    double *P = malloc(sizeof(double) * 2 * (size_t)n);
    if (wl[0x19]) {                                                /* aPoint array in the memory table, projected to the view plane */
        const MT *m = mt_find(g->d, I32(wl, 0x20));
        if (!m || m->n < (size_t)n * 0x98) { free(P); g->st->skipped++; nd(g, "a leader's associative points missing"); return; }
        for (int i = 0; i < n; i++) { double wp[3] = { D(m->p, i * 0x98 + 8), D(m->p, i * 0x98 + 0x10), D(m->p, i * 0x98 + 0x18) }, v[3];
            w2v(g, wp, v); P[2 * i] = v[0]; P[2 * i + 1] = v[1]; }
    } else {
        if (n > 4) n = 4;                                          /* inline p_3d[4] */
        for (int i = 0; i < n; i++) { P[2 * i] = D(wl, 0x20 + 0x18 * i); P[2 * i + 1] = D(wl, 0x28 + 0x18 * i); }
    }
    /* segment 0: A = P[1] -> B = P[0] (the arrow tip), shortened for types 4-7, 10 */
    double ax = P[2], ay = P[3], bx = P[0], by = P[1];
    if (kind == 1 && (type == 4 || type == 5 || type == 6 || type == 7 || type == 10)) {
        double s = (type == 4 || type == 5) ? h / 2 : h, th = atan2(by - ay, bx - ax);
        bx -= s * cos(th); by -= s * sin(th);
    }
    line2(g, ax, ay, bx, by);
    if (kind == 1) arrow(g, P[2], P[3], P[0], P[1], h, w, type);
    double psx = P[0], psy = P[1];                                 /* the previous segment's start */
    for (int i = 1; i + 1 < n; i++) {
        double sx = P[2 * i], sy = P[2 * i + 1];
        double prev = atan2(P[2 * i + 1] - P[2 * i - 1], P[2 * i] - P[2 * i - 2]) * 180 / PI;
        double cur = atan2(P[2 * i + 3] - sy, P[2 * i + 2] - sx) * 180 / PI;
        if (fabs(fabs(prev - cur) - 180) < 1e-6) { sx = psx; sy = psy; }   /* a fold-back drawn as one segment */
        line2(g, sx, sy, P[2 * i + 2], P[2 * i + 3]);
        psx = P[2 * i]; psy = P[2 * i + 1];
    }
    free(P);
}
static void wl_chain(G *g, const uint8_t *wl, size_t avail, int force_none) {   /* FUN_180313970: next id at wl+0x84 */
    for (int guard = 0; wl && guard < 200; guard++) {
        wl_single(g, wl, avail, force_none); force_none = 0;
        if (avail < 0x88) return;
        const MT *m = mt_find(g->d, I32(wl, 0x84));
        wl = m ? m->p : NULL; avail = m ? m->n : 0;
    }
}

/* ------------------------------------------------------------------------------------------------ text (text.md 3) */
static double cw(const Font *F, int c, int prop) {                 /* FUN_18031dd60: the width in cells */
    if (!prop) return 1.0;
    const Glyph *gl = glyph(F, c);
    if (!gl) return c == 0x20 ? 0.6 : 1.0;
    for (int i = 0; i < gl->n; i++) if (gl->c[i].op == 0) return gl->c[i].b[0] / 255.0;
    return 1.0;
}
static void draw_text(G *g, const double pos[3], const uint8_t *fa, const char *s) {
    if (!s || !*s) return;
    int font = fa[0x5c];
    if (!g->fs || font >= g->fs->n || !g->fs->f[font].ok) {
        g->st->skipped++; nd(g, font >= 8 ? "TrueType drafting text (NOT PORTED)" : "text without the Mastercam fonts (install path)"); return; }
    uint16_t w58 = U16(fa, 0x58), w5a = U16(fa, 0x5a);
    int ha = (w58 >> 8) & 3, va = (w58 >> 10) & 7, path = w58 >> 13, prop = w5a & 1, mir = (w5a >> 2) & 3;
    if (path != 0 || mir) { g->st->skipped++; nd(g, "text path / mirror other than Right / none (NOT PORTED)"); return; }
    if (fa[0x5e]) nd(g, "text box / underline lines (NOT PORTED, text drawn)");
    double ang = D(fa, 0x10), slant = D(fa, 0x18), crot = D(fa, 0x20), h = D(fa, 0x28), e = D(fa, 0x30), xs = D(fa, 0x38), xl = D(fa, 0x40);
    if (fabs(slant) >= 1.570796) slant = 0;                        /* FUN_18031de00 */
    const Font *F = &g->fs->f[font];
    /* lines and the longest one (FUN_18031d180: L over lines, n lines) */
    int nl = 1; double L = 0, cur = 0; int first = 1;
    for (const char *p = s; ; p++) {
        unsigned c = (unsigned char)*p;
        if (c == '\r') continue;
        if (c == 0 || c == '\n') { if (!first) cur -= xs; if (cur > L) L = cur; if (!c) break; nl++; cur = 0; first = 1; continue; }
        if (c == 0x0e || c == 0x0f) { nd(g, "alternate font switch (0x0E / 0x0F, NOT PORTED: main font kept)"); continue; }
        cur += cw(F, (int)c, prop) + xs; first = 0;
    }
    double u0 = ha == 1 ? -L / 2 : ha == 2 ? -L : 0, v;
    switch (va) {
    case 0: v = -1; break;
    case 1: v = -193.0 / 255; break;
    case 2: v = ((nl - 1) * xl + nl * 208.0 / 255) / 2 - 1; break;
    case 3: v = -47.0 / 255; break;
    default: v = 0; break;
    }
    double u = u0, ca = cos(ang), sa = sin(ang), sx = h * e, sy = h * 255.0 / 208.0, tsl = tan(slant);
    double gs = g->depth; g->depth = pos[2];
    for (const char *p = s; *p; p++) {
        unsigned c = (unsigned char)*p;
        if (c == '\r' || c == 0x0e || c == 0x0f) continue;
        if (c == '\n') { v -= (xl + 1) - 47.0 / 255; u = u0; continue; }
        double w = cw(F, (int)c, prop);
        if (prop) u -= (1 - w) / 2;
        const Glyph *gl = c >= 0x21 ? glyph(F, (int)c) : NULL;
        if (gl) {                                                  /* FUN_1803199e0: move / line; Bezier unused in the shipped fonts */
            double pts[2 * 512]; int np = 0;
            for (int k = 0; k < gl->n; k++) {
                int op = gl->c[k].op;
                if (op == 3) { nd(g, "Bezier glyph commands (NOT PORTED)"); continue; }
                if (op != 1 && op != 2) continue;
                double x = gl->c[k].b[0], y = gl->c[k].b[1], X, Y;
                if (fabs(crot) > TOL) { double dx = x - 128, dy = y - 104; X = 128 + dx * cos(crot) - dy * sin(crot); Y = 104 + dx * sin(crot) + dy * cos(crot); }
                else { X = x + tsl * (y - 47) * 255 / (208 * e); Y = y; }
                double cx = (u + X / 255) * sx, cy = (v + Y / 255) * sy;
                double wx = pos[0] + cx * ca - cy * sa, wy = pos[1] + cx * sa + cy * ca;
                if (op == 1) { if (np >= 4) emit2(g, pts, np / 2, 0, 0); np = 0; }   /* move: a new run (a line without a current
                                                                                      point only sets it: the run's first point) */
                if (np < 2 * 512) { pts[np++] = wx; pts[np++] = wy; }
            }
            if (np >= 4) emit2(g, pts, np / 2, 0, 0);
        }
        if (prop) u += 1 - (1 - w) / 2 + xs; else u += 1 + xs;
    }
    g->depth = gs;
}

/* ------------------------------------------------------------------------------------------------ the value (%v) */
/* FUN_18030e2d0 (zero suppression): leading zeros off and |v * lim| < 1 - tol * lim: drop a leading '0'; trailing zeros off:
   TrimRight('0') and a final '.' removed; empty -> "0" (DAT_180cf7cd4) */
static void zeros(char *s, double v, double lim, int lead, int trail) {
    double l = 1.0 - TOL * lim;
    if (!lead && fabs(v * lim) < l && s[0] == '0') memmove(s, s + 1, strlen(s));
    if (!trail) { size_t n = strlen(s); while (n && s[n - 1] == '0') s[--n] = 0; if (n && s[n - 1] == '.') s[--n] = 0; }
    if (!*s) strcpy(s, "0");
}
/* CString::Format("%.*f") of Mastercam 2017's CRT: an exact binary half rounds away from zero (stored dimension texts: 2.625 at
   2 places is "2.63"); glibc rounds it to even. Ours: the exact decimal expansion rounded half up in magnitude */
static void fixed(char *out, size_t on, int digits, double v) {
    char b[400]; if (digits < 0) digits = 0; if (digits > 15) digits = 15;
    snprintf(b, sizeof b, "%.60f", fabs(v));                       /* exact (glibc prints the binary value exactly) */
    char *dot = strchr(b, '.'); size_t ip = (size_t)(dot - b);
    char dg[400]; size_t n = 0; for (size_t i = 0; i < ip; i++) dg[n++] = b[i];
    for (int i = 0; i < digits; i++) dg[n++] = dot[1 + i];
    int up = dot[1 + digits] >= '5';
    for (long i = (long)n - 1; up && i >= 0; i--) { if (dg[i] == '9') dg[i] = '0'; else { dg[i]++; up = 0; } }
    char r[400]; size_t k = 0; if (up) r[k++] = '1';
    for (size_t i = 0; i < n; i++) { if (i == n - (size_t)digits && digits) r[k++] = '.'; r[k++] = dg[i]; }
    r[k] = 0; snprintf(out, on, "%s%s", v < 0 && strspn(r, "0.") != strlen(r) ? "-" : "", r);
}
/* FUN_18030ee50 (number) / FUN_18030ec10 (angle); 0 = a format NOT PORTED */
static int fmt_num(char *out, size_t on, int fmt, int digits, double v, int lead, int trail, int comma) {
    char b[300];
    if (fmt == 0) { fixed(b, sizeof b, digits, fabs(v)); zeros(b, v, 1.0, lead, trail);
        snprintf(out, on, "%s%s", v < 0 ? "-" : "", b); }
    else if (fmt == 1) snprintf(out, on, "%.*e", digits, v);
    else return 0;                                                 /* 2 engineering, 3 fractional, 4 architectural: NOT PORTED */
    if (comma) { char *c = strchr(out, '.'); if (c) *c = ','; }    /* comma_period */
    return 1;
}
static int fmt_ang(char *out, size_t on, int fmt, int digits, double rad, int lead, int trail, int comma) {
    char b[300];
    if (fmt == 0 || fmt == 4) { fixed(b, sizeof b, digits, rad * 57.29577951308233);
        zeros(b, rad, 57.29577951308233, lead, trail); snprintf(out, on, "%s\xb0", b); }   /* the degree sign 0xB0 */
    else if (fmt == 2) { fixed(b, sizeof b, digits, rad); zeros(b, rad, 1.0, lead, trail); snprintf(out, on, "%srad", b); }
    else if (fmt == 3) { fixed(b, sizeof b, digits, rad * 57.29577951308233 * 1.1111111111111112);
        zeros(b, rad, 63.66197723675813, lead, trail); snprintf(out, on, "%sg", b); }   /* 0x404fd4bbab8b494e */
    else return 0;                                                 /* 1 degrees / minutes / seconds (FUN_18030f070): NOT PORTED */
    if (comma) { char *c = strchr(out, '.'); if (c) *c = ','; }
    return 1;
}
/* calc_dim_note @18030e730 -> FUN_18030f410 without tolerances (tolMode 0): the value string of a dimension. Kind by type (E+0x308):
   0 linear (E[0x1a9] 1 radius / 2 diameter symbol), 1 angular (scale 1), 2 circular (E[0x101]: diameter), 3 ordinate, 5 / 6 linear;
   value E+0x338 x scale E+0x330; format E+0x7c0, digits E[0x7cd], leading / trailing zeros E[0x7cc] / E[0x7cf] (both kept when
   digits is 0, FUN_18030f410 0x18030f520..0x18030f549), symbol style E+0x7b2. Comma: E[0x7cb] [ND: Mastercam reads the session
   global comma_period]. 0 = a case NOT PORTED */
static int dim_value_t(const uint8_t *E, char *out, size_t on, int force_trail);
static int dim_value(const uint8_t *E, char *out, size_t on) { return dim_value_t(E, out, on, 0); }
/* calc_dim_note case 4 (core_draftwf.c 1224-1240) -> FUN_18030f410 kind 7 -> FUN_180310700 (core_calcdim.c 3094-3280; asm
   decomp/draft/pt_310700.s): a POINT dimension's value. Values: E[0x102] (ptdim_abs) -> the aPoint's world position E+0x70; else
   world_to_view(E+0x70, view E+0x328) less the active origin when MastercamPlanes::ActiveOriginIsOn(Cplane) [ours: the display has no
   active plane - the app's Cplane is Top at the world origin (core_ui cui_draft.c dr_view_no), so nothing is subtracted]; x the scale
   E+0x330. Label (short)(char)E[0x101], 3-D E[0x100]; the numbers as dim_value_t's (FUN_18030ee50 via fmt_num). FUN_180310700's branch:
   LatheXIsX = CoordDisp() == 0 -> "X a, Y b[, Z c]" (labels "X " "Y " "Z " when the label flag is set, ", " between); the lathe branch
   ("X"/"D" r or 2r, ", ", "Z" x: CoordDisp 1 / 2 = GetCoordinateDisplay of the active Cplane) [ours: not taken - the app's planes have no
   lathe coordinate display]. 0 = a number format NOT PORTED */
static int pt_value(const uint8_t *E, char *out, size_t on, int force_trail) {
    double vm[9], w[3], v[3];
    memcpy(w, E + 0x70, 24);
    if (E[0x102]) memcpy(v, w, 24);
    else { if (!mcam_std_view(I16(E, 0x328) ? I16(E, 0x328) : 1, vm)) return 0;
        for (int i = 0; i < 3; i++) v[i] = vm[3 * i] * w[0] + vm[3 * i + 1] * w[1] + vm[3 * i + 2] * w[2]; }
    int fmt = I16(E, 0x7c0), digits = E[0x7cd], lead = digits ? E[0x7cc] : 1, trail = digits && !force_trail ? E[0x7cf] : 1;
    int label = (int8_t)E[0x101], last = (E[0x100] != 0) + 1;
    static const char *lab[3] = { "X", "Y", "Z" };
    size_t k = 0; out[0] = 0;
    for (int i = 0; i <= last; i++) {
        char n[300];
        if (!fmt_num(n, sizeof n, fmt, digits, v[i] * D(E, 0x330), lead, trail, E[0x7cb])) return 0;
        int m = snprintf(out + k, on - k, "%s%s%s%s", label ? lab[i] : "", label ? " " : "", n, i < last ? ", " : "");
        if (m < 0 || (size_t)m >= on - k) return 0;
        k += (size_t)m;
    }
    return 1;
}
static int dim_value_t(const uint8_t *E, char *out, size_t on, int force_trail) {
    if (E[0x308] == 4) return pt_value(E, out, on, force_trail);  /* calc_dim_note case 4: kind 7 */
    int type = E[0x308], kind;
    switch (type) {
    case 0: kind = E[0x1a9] == 2 ? 4 : E[0x1a9] == 1 ? 3 : 1; break;
    case 1: kind = 2; break;
    case 2: kind = E[0x101] ? 4 : 3; break;
    case 3: kind = 5; break;
    case 5: case 6: kind = 1; break;
    default: return 0;                                             /* 4 point: pt_value above */
    }
    int fmt = I16(E, 0x7c0), digits = E[0x7cd], lead = digits ? E[0x7cc] : 1, trail = digits && !force_trail ? E[0x7cf] : 1, sym = I16(E, 0x7b2);
    double v = D(E, 0x338) * (kind == 2 ? 1.0 : D(E, 0x330));
    char n[300];
    if (kind == 2) return fmt_ang(out, on, fmt, digits, v, lead, trail, E[0x7cb]);
    if (!fmt_num(n, sizeof n, fmt, digits, v, lead, trail, E[0x7cb])) return 0;
    if (kind == 3) snprintf(out, on, sym == 0 ? "R %s" : sym == 1 ? "%s R." : "%s", n);
    else if (kind == 4) {
        if (sym == 0) snprintf(out, on, "\xd8 %s", n);            /* "%c %s" with 0xD8 */
        else snprintf(out, on, sym == 1 ? "D %s" : sym == 2 ? "%s Dia." : sym == 3 ? "%s D." : "%s", n);
    } else snprintf(out, on, "%s", n);
    return 1;
}
/* DfReplaceTokens: every "%v" replaced */
static void replace_v(const char *t, const char *v, char *out, size_t on) {
    size_t k = 0;
    for (const char *p = t; *p && k + 1 < on; ) {
        if (p[0] == '%' && p[1] == 'v') { for (const char *q = v; *q && k + 1 < on; q++) out[k++] = *q; p += 2; }
        else out[k++] = *p++;
    }
    out[k] = 0;
}

/* ------------------------------------------------------------------------------------------------ entities */
static void cstrn(char *dst, size_t dn, const uint8_t *src, size_t n) { size_t k = 0; while (k < n && k + 1 < dn && src[k]) { dst[k] = (char)src[k]; k++; } dst[k] = 0; }
static void draw_dim(G *g, const uint8_t *E, uint32_t n) {         /* draw_dimension (draw.md 1.2) */
    if (n < 0x810 || E[0x7ca] == 0x63) return;
    if (!set_view(g, I16(E, 0x328))) { g->st->skipped++; nd(g, "a dimension in a view other than the reserved 1..8 (view table NOT PORTED)"); return; }
    g->depth = D(E, 0x320); g->color = E[6];
    int force = E[0x4a8] && E[0x308] == 3 && E[0x125] == 0;        /* ordinate: leader 1 without its arrow */
    wl_chain(g, E + 0x378, n - 0x378, 0);
    wl_chain(g, E + 0x4a8, n - 0x4a8, force);
    /* FUN_180313380: the value text (DfCheckForTokens: %v replaced when E[0x7a0] - the value format is in draw.md 5, NOT PORTED here),
       then up to two tolerance lines */
    char t[0x60]; cstrn(t, sizeof t, E + 0x650, 0x58);
    double tp[3] = { D(E, 0x638), D(E, 0x640), D(E, 0x648) };
    if (E[0x7a0] && strstr(t, "%v")) {                             /* DfCheckForTokens */
        char v[300], r[700];
        if (dim_value(E, v, sizeof v)) { replace_v(t, v, r, sizeof r); draw_text(g, tp, E + 0x5d8, r); }
        else { g->st->skipped++; nd(g, "a %v value in a format NOT PORTED"); }
    } else draw_text(g, tp, E + 0x5d8, t);
    int ntol = I16(E, 0x7b8);
    if (ntol > 0) { double p[3] = { D(E, 0x708), D(E, 0x710), D(E, 0x718) }; cstrn(t, sizeof t, E + 0x738, 0x29); draw_text(g, p, E + 0x6a8, t); }
    if (ntol > 1) { double p[3] = { D(E, 0x720), D(E, 0x728), D(E, 0x730) }; cstrn(t, sizeof t, E + 0x761, 0x29); draw_text(g, p, E + 0x6a8, t); }
    wl_chain(g, E + 0x540, n - 0x540, 0);
    wl_chain(g, E + 0x410, n - 0x410, 0);
    g->st->dims++;
}
static void draw_note(G *g, const uint8_t *E, uint32_t n) {        /* draw_note (draw.md 1.3) */
    if (n < 0x1c8 || E[0x1a0] == 0x63) return;
    int kind = E[0x68];
    if (kind >= 2 && I32(E, 0xf8) == 0) return;
    if (!set_view(g, I16(E, 0x88))) { g->st->skipped++; nd(g, "a note in a view other than the reserved 1..8 (view table NOT PORTED)"); return; }
    g->depth = D(E, 0x80); g->color = E[6];
    if (kind > 1) {
        const MT *m = mt_find(g->d, I32(E, 0xf8));
        if (m) { char *s = malloc(m->n + 1); memcpy(s, m->p, m->n); s[m->n] = 0;
            double p[3] = { D(E, 0x70), D(E, 0x78), D(E, 0x80) }; draw_text(g, p, E + 0x98, s); free(s); }
        if (kind > 2) { g->st->notes++; return; }
    }
    wl_chain(g, E + 0x100, n - 0x100, 0);
    if (kind > 1) g->st->notes++; else g->st->leaders++;
}
static int (*draft_skip)(const uint8_t *E, uint32_t n);          /* ours: a part record the session replaced (draft_set_skip) */
void draft_set_skip(int (*skip)(const uint8_t *E, uint32_t n)) { draft_skip = skip; }
void draft_render(const DraftDoc *d, const DraftFonts *fonts, const DraftOut *out, DraftStats *st) {
    memset(st, 0, sizeof *st);
    if (!d) return;
    G g = { d, fonts, out, st, 1, { 0 }, 0, 0, 0 };
    for (int i = 0; i < d->nr; i++) {
        const uint8_t *E = d->r[i].e; uint32_t cls = (uint32_t)I32(E, 0);
        if (draft_skip && d->d && draft_skip(E, d->r[i].n)) continue;   /* a part's record (not the session's, draft_render_records) */
        if (cls == 0x40) draw_dim(&g, E, d->r[i].n);
        else draw_note(&g, E, d->r[i].n);                          /* 0x10 / 0x20 */
    }
}

void draft_render_records(const uint8_t *recs, int n, uint32_t len, const DraftFonts *fonts, const DraftOut *out, DraftStats *st) {
    DraftDoc d; memset(&d, 0, sizeof d);
    Rec *r = n > 0 ? calloc((size_t)n, sizeof *r) : NULL;
    for (int i = 0; i < n; i++) if (I32(recs + (size_t)i * len, 0) == 0x40 || I32(recs + (size_t)i * len, 0) == 0x10 || I32(recs + (size_t)i * len, 0) == 0x20) r[d.nr++] = (Rec){ recs + (size_t)i * len, len };
    d.r = r;
    draft_render(&d, fonts, out, st);
    free(r);
}

/* the value oracle (ours, tests): for dimensions whose stored text holds no %v, the stored text is what Mastercam formatted at
   creation - dim_value of the stored value against it (tools/tests/draft_svg --values) */
void draft_value_check(const DraftDoc *d, int *n, int *eq, char *diff, size_t dn) {
    *n = *eq = 0; if (diff && dn) diff[0] = 0;
    for (int i = 0; d && i < d->nr; i++) {
        const uint8_t *E = d->r[i].e;
        if (I32(E, 0) != 0x40 || d->r[i].n < 0x810) continue;
        char t[0x60], v[300]; cstrn(t, sizeof t, E + 0x650, 0x58);
        /* trailing zeros: E[0x7cf] = 1 -> kept in 469 / 469 corpus texts where it matters; E[0x7cf] = 0 -> 258 trimmed, 246 kept (texts
           made under another session setting and not regenerated): those count as differences here */
        if (strstr(t, "%v") || !dim_value(E, v, sizeof v)) continue;
        (*n)++;
        if (!strcmp(t, v)) (*eq)++;
        else if (diff && strlen(diff) + strlen(t) + strlen(v) + 16 < dn) { size_t L = strlen(diff); snprintf(diff + L, dn - L, "%s[%s|%s]", L ? " " : "", t, v); }
    }
}

/* ------------------------------------------------------------------------------------------------ for the drafting pick */
/* viewer/draft_pick.c (select_ent_univ's distance): the text strokes of one record alone, as draw_text emits them - draw_dimension's
   value text (DfCheckForTokens) and tolerance lines (FUN_180313380), draw_note's text (kind > 1); mccore distance_to_string draws the
   string through the same renderer in its distance mode (CDrawString mode 2: every emitted segment, decomp/draft/text1.c 559-570) */
void draft_render_text(const DraftDoc *d, const uint8_t *E, uint32_t n, const DraftFonts *fonts, const DraftOut *out) {
    DraftStats st; memset(&st, 0, sizeof st);
    G g = { d, fonts, out, &st, 1, { 0 }, 0, 0, 0 };
    uint32_t cls = (uint32_t)I32(E, 0);
    if (cls == 0x40 && n >= 0x810) {
        if (!set_view(&g, I16(E, 0x328))) return;
        g.depth = D(E, 0x320); g.color = E[6];
        char t[0x60]; cstrn(t, sizeof t, E + 0x650, 0x58);
        double tp[3] = { D(E, 0x638), D(E, 0x640), D(E, 0x648) };
        if (E[0x7a0] && strstr(t, "%v")) { char v[300], r[700]; if (dim_value(E, v, sizeof v)) { replace_v(t, v, r, sizeof r); draw_text(&g, tp, E + 0x5d8, r); } }
        else draw_text(&g, tp, E + 0x5d8, t);
        int ntol = I16(E, 0x7b8);
        if (ntol > 0) { double p[3] = { D(E, 0x708), D(E, 0x710), D(E, 0x718) }; cstrn(t, sizeof t, E + 0x738, 0x29); draw_text(&g, p, E + 0x6a8, t); }
        if (ntol > 1) { double p[3] = { D(E, 0x720), D(E, 0x728), D(E, 0x730) }; cstrn(t, sizeof t, E + 0x761, 0x29); draw_text(&g, p, E + 0x6a8, t); }
    } else if ((cls == 0x10 || cls == 0x20) && n >= 0x1c8 && E[0x68] > 1) {
        if (!set_view(&g, I16(E, 0x88))) return;
        g.depth = D(E, 0x80); g.color = E[6];
        const MT *m = d ? mt_find(d, I32(E, 0xf8)) : NULL;
        if (m) { char *s = malloc(m->n + 1); memcpy(s, m->p, m->n); s[m->n] = 0;
            double p[3] = { D(E, 0x70), D(E, 0x78), D(E, 0x80) }; draw_text(&g, p, E + 0x98, s); free(s); }
    }
}
const uint8_t *draft_mtable(const DraftDoc *d, int32_t id, size_t *n) {   /* mTableFindData */
    const MT *m = d ? mt_find(d, id) : NULL;
    if (n) *n = m ? m->n : 0;
    return m ? m->p : NULL;
}
