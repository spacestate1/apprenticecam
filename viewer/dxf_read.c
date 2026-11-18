/* dxf_read.c - see dxf_read.h. The DXF ASCII format (group code line, value line; SECTION / ENDSEC; TABLE / ENDTAB; BLOCK / ENDBLK) is
 * the public Autodesk DXF reference. */
#include "dxf_read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static char *rtrim(char *s) { size_t n = strlen(s); while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0; return s; }
static char *ltrim(char *s) { while (*s && isspace((unsigned char)*s)) s++; return s; }
static const char *nz(const char *s) { return s ? s : ""; }

const char *dxf_val(const Dxf *d, const DxfEnt *e, int code) {
    for (int i = 0; i < e->ng; i++) if (d->g[e->g0 + i].code == code) return d->g[e->g0 + i].s;
    return NULL;
}
double dxf_num(const Dxf *d, const DxfEnt *e, int code, double dflt) { const char *v = dxf_val(d, e, code); return v ? strtod(v, NULL) : dflt; }
int dxf_int(const Dxf *d, const DxfEnt *e, int code, int dflt) { const char *v = dxf_val(d, e, code); return v ? (int)strtol(v, NULL, 10) : dflt; }
const DxfBlock *dxf_block(const Dxf *d, const char *name) { for (int i = 0; i < d->nb; i++) if (!strcasecmp(d->b[i].name, name)) return &d->b[i]; return NULL; }
const DxfLayer *dxf_layer(const Dxf *d, const char *name) { for (int i = 0; i < d->nl; i++) if (!strcasecmp(d->l[i].name, name)) return &d->l[i]; return NULL; }

void dxf_free(Dxf *d) { free(d->buf); free(d->g); free(d->e); free(d->b); free(d->l); memset(d, 0, sizeof *d); }

int dxf_read(const char *path, Dxf *d, char *err, size_t errn) {
    memset(d, 0, sizeof *d); d->insunits = d->measurement = -1;
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errn, "cannot open %s", path); return -1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    d->buf = malloc((size_t)n + 2); if (!d->buf) { fclose(f); snprintf(err, errn, "out of memory"); return -1; }
    size_t rd = fread(d->buf, 1, (size_t)n, f); fclose(f); d->buf[rd] = 0;
    if (rd >= 22 && !memcmp(d->buf, "AutoCAD Binary DXF", 18)) { snprintf(err, errn, "binary DXF is not supported"); dxf_free(d); return -1; }
    int gcap = (int)(rd / 8) + 16, ecap = 256;
    d->g = malloc(gcap * sizeof *d->g); d->e = malloc(ecap * sizeof *d->e);
    char *p = d->buf; int have_eof = 0;
    while (*p) {
        char *ln1 = p; char *e1 = strchr(p, '\n'); if (!e1) break; *e1 = 0; p = e1 + 1;
        char *ln2 = p; char *e2 = strchr(p, '\n');
        if (e2) { *e2 = 0; p = e2 + 1; } else p += strlen(p);
        rtrim(ln1); rtrim(ln2);
        char *c = ltrim(ln1); char *q; long code = strtol(c, &q, 10);
        if (q == c || *ltrim(q)) { snprintf(err, errn, "bad group code line: %.40s", ln1); dxf_free(d); return -1; }
        if (d->ng >= gcap) { gcap *= 2; d->g = realloc(d->g, gcap * sizeof *d->g); }
        d->g[d->ng].code = (int)code; d->g[d->ng].s = ln2; d->ng++;
    }
    /* split into records (group 0 starts one) */
    for (int i = 0; i < d->ng; i++) if (d->g[i].code == 0) {
        if (d->ne >= ecap) { ecap *= 2; d->e = realloc(d->e, ecap * sizeof *d->e); }
        DxfEnt *e = &d->e[d->ne++]; e->type = ltrim((char *)d->g[i].s); e->g0 = i + 1; e->ng = 0;
        int j = i + 1; while (j < d->ng && d->g[j].code != 0) j++; e->ng = j - i - 1;
        if (!strcasecmp(e->type, "EOF")) { have_eof = 1; break; }
    }
    if (!have_eof && !d->ne) { snprintf(err, errn, "not a DXF file (no group 0 records)"); dxf_free(d); return -1; }
    /* HEADER variables, TABLES > LAYER, BLOCKS, ENTITIES */
    enum { S_NONE, S_HEADER, S_TABLES, S_BLOCKS, S_ENTITIES, S_OTHER } sec = S_NONE;
    int in_layer_table = 0; DxfBlock *cb = NULL; int lcap = 0, bcap = 0;
    d->ent0 = -1;
    for (int i = 0; i < d->ne; i++) {
        DxfEnt *e = &d->e[i];
        if (!strcasecmp(e->type, "SECTION")) {
            const char *nm = dxf_val(d, e, 2);
            sec = !nm ? S_OTHER : !strcasecmp(nm, "HEADER") ? S_HEADER : !strcasecmp(nm, "TABLES") ? S_TABLES : !strcasecmp(nm, "BLOCKS") ? S_BLOCKS : !strcasecmp(nm, "ENTITIES") ? S_ENTITIES : S_OTHER;
            if (sec == S_ENTITIES) d->ent0 = i + 1;
            if (sec == S_HEADER) {           /* HEADER: $NAME (group 9) value (following group) */
                for (int k = e->g0 + 1; k + 1 < d->ng && d->g[k].code != 0; k++) {
                    if (d->g[k].code != 9) continue;
                    const char *nv = d->g[k].s; const char *v = d->g[k + 1].s;
                    if (!strcasecmp(nv, "$INSUNITS") && d->g[k + 1].code == 70) d->insunits = atoi(v);
                    else if (!strcasecmp(nv, "$MEASUREMENT") && d->g[k + 1].code == 70) d->measurement = atoi(v);
                    else if (!strcasecmp(nv, "$ACADVER") && d->g[k + 1].code == 1) snprintf(d->acadver, sizeof d->acadver, "%s", nz(v));
                }
            }
            continue;
        }
        if (!strcasecmp(e->type, "ENDSEC")) { if (sec == S_ENTITIES && d->ent0 >= 0) d->nent = i - d->ent0; sec = S_NONE; continue; }
        if (sec == S_TABLES) {
            if (!strcasecmp(e->type, "TABLE")) { const char *nm = dxf_val(d, e, 2); in_layer_table = nm && !strcasecmp(nm, "LAYER"); }
            else if (!strcasecmp(e->type, "ENDTAB")) in_layer_table = 0;
            else if (in_layer_table && !strcasecmp(e->type, "LAYER")) {
                if (d->nl >= lcap) { lcap = lcap ? lcap * 2 : 16; d->l = realloc(d->l, lcap * sizeof *d->l); }
                d->l[d->nl].name = nz(dxf_val(d, e, 2)); d->l[d->nl].color = dxf_int(d, e, 62, 7);
                d->l[d->nl].flags = dxf_int(d, e, 70, 0); d->l[d->nl].ltype = nz(dxf_val(d, e, 6)); d->l[d->nl].lw = dxf_int(d, e, 370, -3); d->nl++;
            }
        } else if (sec == S_BLOCKS) {
            if (!strcasecmp(e->type, "BLOCK")) {
                if (d->nb >= bcap) { bcap = bcap ? bcap * 2 : 16; d->b = realloc(d->b, bcap * sizeof *d->b); }
                cb = &d->b[d->nb++]; cb->name = nz(dxf_val(d, e, 2)); cb->base[0] = dxf_num(d, e, 10, 0); cb->base[1] = dxf_num(d, e, 20, 0);
                cb->base[2] = dxf_num(d, e, 30, 0); cb->flags = dxf_int(d, e, 70, 0); cb->e0 = i + 1; cb->ne = 0;
            } else if (!strcasecmp(e->type, "ENDBLK")) { if (cb) cb->ne = i - cb->e0; cb = NULL; }
        }
    }
    if (d->ent0 < 0) { snprintf(err, errn, "no ENTITIES section"); dxf_free(d); return -1; }
    return 0;
}
