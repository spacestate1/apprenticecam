/* dxf_read - the DXF file structure (public Autodesk DXF reference): an ASCII file as group-code / value pairs, split into the
 * sections' records. It only reads the file: what becomes what is cad_import.c's (formats/cad_import.md).
 * Binary DXF ("AutoCAD Binary DXF" sentinel) is refused by dxf_read (not implemented here). */
#ifndef DXF_READ_H
#define DXF_READ_H
#include <stddef.h>
typedef struct { int code; const char *s; } DxfG;                       /* one group: code + value text (in Dxf.buf) */
typedef struct {
    const char *type;                  /* group 0 value */
    int g0, ng;                        /* its groups Dxf.g[g0 .. g0+ng-1] (group 0 excluded) */
} DxfEnt;
typedef struct {                       /* a BLOCKS section record */
    const char *name;
    double base[3];                    /* groups 10 / 20 / 30 of BLOCK */
    int flags;                         /* group 70 */
    int e0, ne;                        /* its entities Dxf.e[e0 .. e0+ne-1] */
} DxfBlock;
typedef struct { const char *name; int color; int flags; const char *ltype; int lw; } DxfLayer;   /* TABLES > LAYER: 2 name, 62 colour (negative = off), 70 flags, 6 linetype, 370 lineweight */
typedef struct {
    char *buf;
    DxfG *g; int ng;
    DxfEnt *e; int ne;
    int ent0, nent;                    /* the ENTITIES section's records in e */
    DxfBlock *b; int nb;
    DxfLayer *l; int nl;
    int insunits;                      /* $INSUNITS (70): -1 if absent */
    int measurement;                   /* $MEASUREMENT (70): -1 if absent */
    char acadver[16];                  /* $ACADVER (1) */
} Dxf;
/* 0 = ok; err filled otherwise */
int dxf_read(const char *path, Dxf *d, char *err, size_t errn);
void dxf_free(Dxf *d);
/* the first group of entity e with this code, its value, or NULL; dxf_num the value parsed with strtod or dflt */
const char *dxf_val(const Dxf *d, const DxfEnt *e, int code);
double dxf_num(const Dxf *d, const DxfEnt *e, int code, double dflt);
int dxf_int(const Dxf *d, const DxfEnt *e, int code, int dflt);
const DxfBlock *dxf_block(const Dxf *d, const char *name);
const DxfLayer *dxf_layer(const Dxf *d, const char *name);
#endif
