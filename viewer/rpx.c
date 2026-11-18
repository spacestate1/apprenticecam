/* rpx - see rpx.h. Layout rules are ActiveReports 3's: a Field shows the record's child element named by DataField
 * (its Text when it has no DataField), a Label its Caption; a Subreport repeats its own report over the record's
 * children named by its DataField and grows (CanGrow, default on) to fit them, pushing the controls under it and
 * the section's bottom down by the same amount; Detail sections marked KeepTogether move to the next page when they
 * do not fit. Page: Letter 8.5 x 11 in, 1 in margins (empty PageSettings). Not measured against the real viewer. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "rpx.h"

#define PAGE_H (15840.0 - 2 * 1440.0)       /* printable height, twips */
#define PAGE_STEP 100000.0                  /* continuous y: page k starts at k * PAGE_STEP */

RNode *rnode_new(RNode *parent, const char *name, const char *text) {
    RNode *n = xcalloc(1, sizeof *n);
    snprintf(n->name, sizeof n->name, "%s", name);
    n->text = strdup(text ? text : "");
    if (parent) { parent->kid = xrealloc(parent->kid, (parent->nkid + 1) * sizeof *parent->kid); parent->kid[parent->nkid++] = n; }
    return n;
}
void rnode_free(RNode *n) {
    if (!n) return;
    for (int i = 0; i < n->nkid; i++) rnode_free(n->kid[i]);
    free(n->kid); free(n->text); free(n);
}

typedef struct {
    int type;                 /* 0 label, 1 field, 2 line, 3 subreport, 4 shape, 5 image */
    double l, t, w, h, x1, y1, x2, y2;
    char text[128], field[48], report[160], name[48];
    double pt; int bold, italic, align, cangrow;
    int vis;                  /* Visible (default 1) */
    int bl, bt, br, bb;       /* BorderLeftStyle / Top / Right / Bottom (0 none) */
    unsigned char *img; int nimg, sizemode;   /* AR.Image: the embedded picture (below) */
} Ctl;
typedef struct { double h; int keep; Ctl *c; int nc; } Sect;
typedef struct { char path[600]; double width; Sect *s; int ns; } Rpx;

static Rpx *cache[32]; static int ncache;

static int attr(const char *tag, const char *end, const char *name, char *out, size_t n) {
    char pat[64]; snprintf(pat, sizeof pat, " %s=\"", name);
    const char *p = strstr(tag, pat);
    if (!p || p > end) { out[0] = 0; return 0; }
    p += strlen(pat);
    const char *q = strchr(p, '"');
    if (!q || q > end) { out[0] = 0; return 0; }
    size_t k = 0;
    for (; p < q && k + 1 < n; p++) {                        /* the entities the templates use */
        if (*p == '&') {
            if (!strncmp(p, "&amp;", 5)) { out[k++] = '&'; p += 4; continue; }
            if (!strncmp(p, "&lt;", 4)) { out[k++] = '<'; p += 3; continue; }
            if (!strncmp(p, "&gt;", 4)) { out[k++] = '>'; p += 3; continue; }
            if (!strncmp(p, "&quot;", 6)) { out[k++] = '"'; p += 5; continue; }
        }
        out[k++] = *p;
    }
    out[k] = 0;
    return 1;
}
static double attrf(const char *tag, const char *end, const char *name, double dflt) {
    char b[64]; return attr(tag, end, name, b, sizeof b) ? atof(b) : dflt;
}
static void style(Ctl *c, const char *st) {
    const char *p;
    if ((p = strstr(st, "font-size:"))) c->pt = atof(p + 10);
    if ((p = strstr(st, "font-weight:"))) c->bold = !strncmp(p + 12 + strspn(p + 12, " "), "bold", 4);
    if ((p = strstr(st, "font-style:"))) c->italic = !strncmp(p + 11 + strspn(p + 11, " "), "italic", 6);
    if ((p = strstr(st, "text-align:"))) { p += 11 + strspn(p + 11, " "); c->align = !strncmp(p, "center", 6) ? 1 : !strncmp(p, "right", 5) ? 2 : 0; }
}

static Rpx *load(const char *path, char *err, size_t errn) {
    for (int i = 0; i < ncache; i++) if (!strcmp(cache[i]->path, path)) return cache[i];
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errn, "cannot open %s", path); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (1L << 28)) { fclose(f); snprintf(err, errn, "cannot read %s", path); return NULL; }
    char *d = xmalloc((size_t)n + 1); if (fread(d, 1, n, f) != (size_t)n) n = 0; d[n] = 0; fclose(f);
    Rpx *r = xcalloc(1, sizeof *r); snprintf(r->path, sizeof r->path, "%s", path);
    double normal_pt = 10;                                   /* StyleSheet "Normal" */
    const char *p = strstr(d, "<Style Name=\"Normal\"");
    if (p) { char st[1000]; const char *e = strchr(p, '>'); if (attr(p, e, "Value", st, sizeof st)) { Ctl t = { 0 }; t.pt = normal_pt; style(&t, st); normal_pt = t.pt; } }
    p = strstr(d, "<ActiveReportsLayout");
    if (p) r->width = attrf(p, strchr(p, '>'), "PrintWidth", 9360);
    for (const char *s = strstr(d, "<Section "); s; s = strstr(s + 1, "<Section ")) {
        const char *se = strchr(s, '>'), *send = strstr(s, "</Section>");
        if (!se) break;
        r->s = xrealloc(r->s, (r->ns + 1) * sizeof *r->s); Sect *S = &r->s[r->ns++]; memset(S, 0, sizeof *S);
        S->h = attrf(s, se, "Height", 0); S->keep = attrf(s, se, "KeepTogether", 0) != 0;
        for (const char *c = strstr(se, "<Control "); c && (!send || c < send); c = strstr(c + 1, "<Control ")) {
            const char *ce = strchr(c, '>'); char ty[32], st[1000];
            if (!ce) break;
            attr(c, ce, "Type", ty, sizeof ty);
            Ctl C; memset(&C, 0, sizeof C); C.pt = normal_pt; C.cangrow = attrf(c, ce, "CanGrow", 1) != 0;
            C.type = !strcmp(ty, "AR.Label") ? 0 : !strcmp(ty, "AR.Field") ? 1 : !strcmp(ty, "AR.Line") ? 2 : !strcmp(ty, "AR.Subreport") ? 3
                   : !strcmp(ty, "AR.Shape") ? 4 : !strcmp(ty, "AR.Image") ? 5 : -1;
            if (C.type < 0) continue;                         /* none other in the shipped templates */
            C.vis = attrf(c, ce, "Visible", 1) != 0;
            C.bl = (int)attrf(c, ce, "BorderLeftStyle", 0); C.bt = (int)attrf(c, ce, "BorderTopStyle", 0);
            C.br = (int)attrf(c, ce, "BorderRightStyle", 0); C.bb = (int)attrf(c, ce, "BorderBottomStyle", 0);
            attr(c, ce, "Name", C.name, sizeof C.name);
            if (C.type == 5 && ce[-1] != '/') {               /* AR.Image with a picture: the element's text is hex - a 9-byte header
                                                                 (0D 0D 00 04 02 + the u32 length of the rest) and the image file
                                                                 itself (the NST templates' Picture1: 7605 bytes of PNG) */
                const char *q = ce + 1, *qe = strstr(q, "</Control>"); size_t nh = 0;
                while (q + nh < qe && strchr("0123456789ABCDEFabcdef", q[nh]) && q[nh]) nh++;
                if (nh >= 18 && qe && (!send || qe < send)) {
                    unsigned char *b = xmalloc(nh / 2 + 1);
                    for (size_t k = 0; k + 1 < nh; k += 2) { char t[3] = { q[k], q[k + 1], 0 }; b[k / 2] = (unsigned char)strtol(t, NULL, 16); }
                    size_t nb = nh / 2; uint32_t len = (uint32_t)b[5] | (uint32_t)b[6] << 8 | (uint32_t)b[7] << 16 | (uint32_t)b[8] << 24;
                    if (nb > 9 && len == nb - 9) { C.img = xmalloc(len); memcpy(C.img, b + 9, len); C.nimg = (int)len; }
                    free(b);
                }
                C.sizemode = (int)attrf(c, ce, "SizeMode", 0);
            }
            C.l = attrf(c, ce, "Left", 0); C.t = attrf(c, ce, "Top", 0); C.w = attrf(c, ce, "Width", 0); C.h = attrf(c, ce, "Height", 0);
            C.x1 = attrf(c, ce, "X1", 0); C.y1 = attrf(c, ce, "Y1", 0); C.x2 = attrf(c, ce, "X2", 0); C.y2 = attrf(c, ce, "Y2", 0);
            attr(c, ce, C.type == 0 ? "Caption" : "Text", C.text, sizeof C.text);
            attr(c, ce, "DataField", C.field, sizeof C.field);
            attr(c, ce, "ReportName", C.report, sizeof C.report);
            if (attr(c, ce, "Style", st, sizeof st)) style(&C, st);
            S->c = xrealloc(S->c, (S->nc + 1) * sizeof *S->c); S->c[S->nc++] = C;
        }
    }
    free(d);
    if (ncache < 32) cache[ncache++] = r;
    return r;
}

static void emit(RLayout *L, int line, double x, double y, double w, double h, const char *text, const Ctl *c);
/* a line of style LS (a control border, a shape's outline) */
static void emit_ls(RLayout *L, double x1, double y1, double x2, double y2, int ls) {
    if (!L) return;
    emit(L, 1, x1, y1, x2, y2, NULL, NULL); L->it[L->n - 1].ls = ls;
}
static void emit(RLayout *L, int line, double x, double y, double w, double h, const char *text, const Ctl *c) {
    if (!L) return;
    if (L->n == L->cap) { L->cap = L->cap ? 2 * L->cap : 64; L->it = xrealloc(L->it, L->cap * sizeof *L->it); }
    RItem *it = &L->it[L->n++]; memset(it, 0, sizeof *it);
    int pg = (int)(y / PAGE_STEP);
    it->line = line; it->page = pg; it->x = x; it->y = y - pg * PAGE_STEP; it->w = w; it->h = line ? h - pg * PAGE_STEP : h;
    it->text = text ? strdup(text) : NULL;
    if (c) { it->pt = c->pt; it->bold = c->bold; it->italic = c->italic; it->align = c->align; }
    if (pg + 1 > L->pages) L->pages = pg + 1;
}
static const char *field_of(const RNode *r, const char *name) {
    for (int i = 0; r && i < r->nkid; i++) if (!strcmp(r->kid[i]->name, name)) return r->kid[i]->text;
    return "";
}

static double lay(Rpx *R, RNode **rec, int nrec, double x0, double y, RLayout *L, int paginate, char *err, size_t errn);

/* one Detail section for one record at continuous y; returns the y after it */
static double section(Rpx *R, const Sect *S, RNode *rec, double x0, double y, RLayout *L, int paginate, char *err, size_t errn) {
    char dir[600]; snprintf(dir, sizeof dir, "%s", R->path); char *sl = path_sep_r(dir); if (sl) sl[1] = 0; else dir[0] = 0;
    double grow[64] = { 0 }, bot[64]; int sub[64], nsub = 0;
    for (int i = 0; i < S->nc && nsub < 64; i++) if (S->c[i].type == 3) sub[nsub++] = i;
    for (int a = 0; a < nsub; a++) for (int b = a + 1; b < nsub; b++) if (S->c[sub[b]].t < S->c[sub[a]].t) { int t = sub[a]; sub[a] = sub[b]; sub[b] = t; }
#define SHIFT(yy) ({ double s_ = 0; for (int k_ = 0; k_ < nsub; k_++) if (bot[k_] <= (yy) + 1e-6 && grow[k_] > 0) s_ += grow[k_]; s_; })
    for (int k = 0; k < nsub; k++) bot[k] = 1e300;
    for (int k = 0; k < nsub; k++) {                          /* subreports first: their growth moves what is below */
        const Ctl *c = &S->c[sub[k]];
        double sh = 0; for (int j = 0; j < k; j++) if (bot[j] <= c->t + 1e-6) sh += grow[j];
        bot[k] = c->t + c->h;
        char path[800]; snprintf(path, sizeof path, "%s%s", dir, c->report);
        Rpx *R2 = load(path, err, errn);
        RNode *kids[512]; int nk = 0;
        for (int i = 0; rec && i < rec->nkid && nk < 512; i++) if (!strcmp(rec->kid[i]->name, c->field)) kids[nk++] = rec->kid[i];
        double y0 = y + c->t + sh, y1 = R2 && nk ? lay(R2, kids, nk, x0 + c->l, y0, L, paginate, err, errn) : y0;
        grow[k] = c->cangrow && y1 - y0 > c->h ? y1 - y0 - c->h : 0;
    }
    /* the report viewer's image rule (activereports_viewer.exe MainForm.cs 760-970): with IMAGE_FILENAME (a hidden TextBox),
       IMAGE_HOLDER (a Picture) and IMAGE_NOT_FOUND (a Label) in the section, the file named by IMAGE_FILENAME's value (".BMP"
       added without an extension) is shown in the holder and the label hidden; no file -> the label stays. Here no image is
       loaded (the caller's report data name none): the label stays, the holder draws nothing */
    for (int i = 0; i < S->nc; i++) {
        const Ctl *c = &S->c[i];
        if (c->type == 3 || !c->vis) continue;
        if (c->type == 2) {
            double sh = SHIFT(c->y1 < c->y2 ? c->y1 : c->y2);
            emit(L, 1, x0 + c->x1, y + c->y1 + sh, x0 + c->x2, y + c->y2 + sh, NULL, c);
            continue;
        }
        double X = x0 + c->l, Y = y + c->t + SHIFT(c->t);
        if (c->type == 4) {                                   /* AR.Shape: its outline (RoundingRadius ~10 twips: square corners here) */
            emit_ls(L, X, Y, X + c->w, Y, 1); emit_ls(L, X + c->w, Y, X + c->w, Y + c->h, 1);
            emit_ls(L, X + c->w, Y + c->h, X, Y + c->h, 1); emit_ls(L, X, Y + c->h, X, Y, 1);
            continue;
        }
        if (c->type == 5) {                                   /* AR.Image: IMAGE_HOLDER (above) draws nothing; an embedded picture is drawn */
            if (c->nimg && L) { emit(L, 0, X, Y, c->w, c->h, NULL, NULL); RItem *it = &L->it[L->n - 1]; it->line = 2; it->img = c->img; it->nimg = c->nimg; it->sizemode = c->sizemode; }
            continue;
        }
        /* a Label bound to a DataField shows the field (Data_NAME of SR1, read back as the Label's Text by its script) */
        const char *t = !c->field[0] ? c->text : field_of(rec, c->field);
        emit(L, 0, X, Y, c->w, c->h, t, c);
        if (c->bt) emit_ls(L, X, Y, X + c->w, Y, c->bt);
        if (c->bb) emit_ls(L, X, Y + c->h, X + c->w, Y + c->h, c->bb);
        if (c->bl) emit_ls(L, X, Y, X, Y + c->h, c->bl);
        if (c->br) emit_ls(L, X + c->w, Y, X + c->w, Y + c->h, c->br);
    }
    double end = y + S->h + SHIFT(S->h);
#undef SHIFT
    return end;
}

static double lay(Rpx *R, RNode **rec, int nrec, double x0, double y, RLayout *L, int paginate, char *err, size_t errn) {
    for (int ri = 0; ri < nrec; ri++)
        for (int si = 0; si < R->ns; si++) {
            const Sect *S = &R->s[si];
            if (paginate && S->keep) {                        /* KeepTogether: to the next page when it does not fit */
                double h = section(R, S, rec[ri], x0, 0, NULL, 0, err, errn);
                double in = y - (int)(y / PAGE_STEP) * PAGE_STEP;
                if (in > 0 && in + h > PAGE_H) y = ((int)(y / PAGE_STEP) + 1) * PAGE_STEP;
            }
            y = section(R, S, rec[ri], x0, y, L, paginate, err, errn);
        }
    return y;
}

int rpx_layout(const char *rpx, RNode **rec, int nrec, RLayout *out) {
    memset(out, 0, sizeof *out);
    Rpx *R = load(rpx, out->err, sizeof out->err);
    if (!R) return 0;
    out->width = R->width;
    lay(R, rec, nrec, 0, 0, out, 1, out->err, sizeof out->err);
    if (!out->pages) out->pages = 1;
    return 1;
}
void rpx_layout_free(RLayout *l) {
    for (int i = 0; i < l->n; i++) free(l->it[i].text);
    free(l->it); memset(l, 0, sizeof *l);
}

static int binds(Rpx *R, const char *field, int depth) {
    if (!R || depth > 8) return 0;
    char dir[600], err[200]; snprintf(dir, sizeof dir, "%s", R->path); char *sl = path_sep_r(dir); if (sl) sl[1] = 0; else dir[0] = 0;
    for (int si = 0; si < R->ns; si++)
        for (int i = 0; i < R->s[si].nc; i++) {
            const Ctl *c = &R->s[si].c[i];
            if (c->type != 3) continue;
            if (!strcmp(c->field, field)) return 1;
            char path[800]; snprintf(path, sizeof path, "%s%s", dir, c->report);
            if (binds(load(path, err, sizeof err), field, depth + 1)) return 1;
        }
    return 0;
}
int rpx_binds(const char *rpx, const char *field) { char err[200]; return binds(load(rpx, err, sizeof err), field, 0); }
