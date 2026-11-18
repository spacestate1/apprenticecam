/*
 * mcam_load.c - Mastercam 2017 / X9 part file reader (see ../formats/mcam.md):
 *   - file = stream of [u32 len][entity record]; some system records are followed by out-of-band blocks
 *     (toolpath groups, material table, Parasolid solids) that must be skipped  (mccore FUN_1802c62d0)
 *   - entity header: +0x00 u32 class, +0x06 u8 colour, +0x08 i32 level, +0x2c i32 assoc subtype, body at +0x68
 *     P_ID(1) xyz | L_ID(2) xyz,xyz | A_ID(4) start,end,centre xyz, r, start angle, sweep (radians)
 *     ASSOC_ID(0x4000): subtype 1 OP_ID (+0x68 op id, +0x6c opcode, +0x6e comment, +0xe6 Operations Manager order,
 *                       +0x5ac tool slot)
 *                       subtype 3 TL_ID (tool struct at +0x68: i32 number, i32 type, f64 diameter, name at +0xbe,
 *                       +0x2cc TlMgr slot)
 *                       subtype 33 TlMgr archive: each tool's full definition, holder, assembly (part_tools.c)
 *     SOLIDXMT(0x20000): +0x49 version, +0x68 name, +0x168 u32 XT size; the XT bytes follow the record
 *   - after the last record: binary NCI ([u16 size][nci_bin], zero-filled to 0x370) (mccore bnci_read_from_disk)
 *     -2 op start (+0x1c op id), 0/1 move (+0x20 xyz, +0x38 feed; feed < 0 = rapid),
 *     2/3 arc (+0x18 plane, +0x20 end, +0x38 centre, plane-local u,v,depth), 1000-1002 tool change
 *     (+0x24 tool number, +0x50 first position), 81 drill point (+0x20 x, y, depth, +0x68 R plane)
 */
#include <sys/stat.h>
#include <errno.h>
#include "platform.h"
#include "mcam_load.h"
#include "mcam_grp.h"
/* McSysTol (mcam_load.h, formats/units.md 8): mccore's system tolerances; read from the configuration by mcam_saveops.c
   (mc_systol_units). The static values are the metric defaults (the program starts metric). */
McSysTol mc_systol = { 5e-05, 0.002, 0.02, 0.02, 0.02, 100.0, 0.02, 0.02, 0.002, 0.025 };
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opcodes.h"
#include "part_tools.h"
#include "mcsurf.h"

static uint32_t rd32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t rd16(const uint8_t *b) { return b[0] | b[1] << 8; }
static uint64_t rd64(const uint8_t *b) { return rd32(b) | (uint64_t)rd32(b + 4) << 32; }
static double rdd(const uint8_t *b) { double d; memcpy(&d, b, 8); return d; }
static V3 rdv(const uint8_t *b) { V3 v = { rdd(b), rdd(b + 8), rdd(b + 16) }; return v; }

const char *mcam_opcode_name(int code) {
    for (size_t i = 0; i < sizeof OPCODE_NAMES / sizeof OPCODE_NAMES[0]; i++)
        if (OPCODE_NAMES[i].code == code) return OPCODE_NAMES[i].name;
    return "?";
}

static void addpt(McamFile *f, V3 p) {
    if (f->npts == f->cappts) { if (f->cappts > (1 << 29)) { fprintf(stderr, "part too large\n"); exit(3); } f->cappts = f->cappts ? f->cappts * 2 : 4096; f->pts = xrealloc(f->pts, (size_t)f->cappts * sizeof(V3)); }
    f->pts[f->npts++] = p;
}
static int newpoly(McamFile *f, int kind, int op, int tool, uint8_t color, int level) {
    if (f->npolys == f->cappolys) { if (f->cappolys > (1 << 28)) { fprintf(stderr, "part too large\n"); exit(3); } f->cappolys = f->cappolys ? f->cappolys * 2 : 1024; f->polys = xrealloc(f->polys, (size_t)f->cappolys * sizeof(Poly)); }
    Poly *p = &f->polys[f->npolys];
    p->kind = kind; p->op = op; p->tool = tool; p->color = color; p->ln_style = 1; p->width = 1; p->level = level; p->first = f->npts; p->count = 0; p->blanked = 0;
    return f->npolys++;
}
static void polypt(McamFile *f, int pi, V3 v) { addpt(f, v); f->polys[pi].count++; }

static Op *find_op(McamFile *f, int id, int create) {
    for (int i = 0; i < f->nops; i++) if (f->ops[i].id == id) return &f->ops[i];
    if (!create) return NULL;
    f->ops = xrealloc(f->ops, (f->nops + 1) * sizeof(Op));
    Op *o = &f->ops[f->nops++];
    memset(o, 0, sizeof *o);
    o->id = id; o->seq = 1 << 30; o->slot = -1;
    return o;
}

static void add_arc(McamFile *f, int kind, int op, int tool, uint8_t color, int level, V3 c, double r, double a0, double sw,
                    int au, int av, int ad, double d0, double d1) {
    if (!isfinite(sw) || fabs(sw) > 4 * M_PI) sw = 0;                  /* damaged record */
    int n = (int)(fabs(sw) * 180 / M_PI / 3) + 2;
    if (n > 720) n = 720;
    int pi = newpoly(f, kind, op, tool, color, level);
    for (int i = 0; i <= n; i++) {
        double a = a0 + sw * i / n, q[3];
        q[au] = (&c.x)[au] + r * cos(a);
        q[av] = (&c.x)[av] + r * sin(a);
        q[ad] = d0 + (d1 - d0) * i / n;
        V3 v = { q[0], q[1], q[2] };
        polypt(f, pi, v);
    }
}

static void cstr(char *dst, size_t n, const uint8_t *src, size_t max) {
    size_t i = 0;
    for (; i + 1 < n && i < max && src[i]; i++) dst[i] = (src[i] >= 32 && src[i] < 127) ? src[i] : '?';
    dst[i] = 0;
}

static const uint8_t *cur_rec;              /* the record being parsed (for the entity identity fields) */
int mc_units_metric = 1;                     /* IsMetric() (mcam_load.h) */
int mcam_is_metric(const McamFile *f) { return f && (f->metric == 0 || f->metric == 1) ? f->metric : mc_units_metric; }
static int load_units = -1;                  /* header +0x6f of the part being read (1 mm, 0 inch, -1 unknown) */
/* while loading (pointers into the file buffer): each tool's TL_ID body (ent +0x68, NULL if shorter than 0x288) and
   the TlMgr records, applied after the last record (they may come before or after the TL_IDs) */
static const uint8_t *tl_body[256];
static const uint8_t **tlmgr_rec; static size_t *tlmgr_n; static int ntlmgr;
/* the CNC_MACHINE_ID (ASSOC sub 22), CNC_CONTROL_ID (23) and control page (24) records, pointers into the file buffer,
   resolved after the last record by resolve_machine (the machine group table may come before or after them) */
static struct { const uint8_t *r; uint32_t n; int sub; } *mcr; static int nmcr, cmcr;
static int g_in_load;                    /* inside mcam_load (a level made then is FUN_1802c4010's "in_file_get" case) */
static void lvl_store(McamFile *f, int level);
static struct McLevel *lvl_append(McamFile *f, int level, int vis);
struct McLevel *mcam_level(McamFile *f, int level);
static Ent *add_ent(McamFile *f, int type, int level, int color) {
    if (g_in_load) { if (level > 0 && !mcam_level(f, level)) lvl_append(f, level, 0); }   /* FUN_1802c4010 L123-131 while a file is read: hidden */
    else if (!cur_rec) lvl_store(f, level);
    f->ents = xrealloc(f->ents, (f->nents + 1) * sizeof(Ent));
    Ent *e = &f->ents[f->nents++]; memset(e, 0, sizeof *e);
    e->type = type; e->level = level; e->color = color; e->first_poly = f->npolys; e->planar = 1;
    e->crv = -1;                                  /* no curve record (an E_SPLINE gets its own: mcam_new_curve / the loader) */
    if (cur_rec) { e->idn = (int32_t)rd32(cur_rec + 0x34); e->ctime = (int32_t)rd32(cur_rec + 0x30); e->eptr = (int64_t)rd64(cur_rec + 0x20); e->cls = rd32(cur_rec);
                   e->blanked = (rd16(cur_rec + 4) & 0x100) != 0; e->rec3c = (int32_t)rd32(cur_rec + 0x3c); e->rec_sel = rd16(cur_rec + 4); }
    return e;
}

/* arc views (formats/mcam.md "Arc views"): A_ID +0xc8 u16 = view number; the centre (+0x98) is in that view's
   coordinates (u, v, depth), start / end (+0x68 / +0x80) are world. mastercam.dll view_to_world @180341520
   (decomp/mastercam_keyrot.c l.673): views < 2 = identity, else world = M^T * p with M = the view's 3x3 matrix
   (rows = view axes in world). Views are DB_ID (0x200) subtype 0 records: +0x6a u16 view number, +0x70 matrix_33
   (they can follow the geometry in the file, so view arcs are finished after the last record). */
typedef struct { int n; double m[9]; } ViewM;
static ViewM *views; static int nviews;
typedef struct { int ent; const uint8_t *rec; } PendArc;
static PendArc *pend_arcs; static int npend_arcs;
static void cons_poly(McamFile *f, int ei);   /* the surface curves' polylines (after the last record) */

/* the reserved views 1 Top .. 8 Axonometric (mastercam.dll ReservedPlanes::BuildStandardMatrices, decomp/mastercam_stdviews.c;
   the same rows as scene.c STD_VIEW): view_to_world (decomp/mastercam_keyrot.c l.673) reads the view table, whose first
   entries are these; a part file stores only its other views (DB_ID subtype 0, numbers 20 / 65535 in the user parts) */
static const double STDV[9][9] = {
    { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 1, 0, 0, 0, 0, 1, 0, -1, 0 }, { -1, 0, 0, 0, 0, 1, 0, 1, 0 },
    { -1, 0, 0, 0, 1, 0, 0, 0, -1 }, { 0, 1, 0, 0, 0, 1, 1, 0, 0 }, { 0, -1, 0, 0, 0, 1, -1, 0, 0 },
    { 0.70710678118655, 0.70710678118655, 0.0, -0.40824829046386, 0.40824829046386, 0.81649658092772,
      0.57735026918962, -0.57735026918962, 0.57735026918962 },
    { 0.5, -0.85355339059, 0.14644660942, 0.5, 0.14644660942, -0.85355339059, 0.70710678118655, 0.5, 0.5 } };
/* the built-in views 9 .. 18 (n_views = 18, mastercam.dll init decomp/mastercam_stdviews_init.c:28, the matrices it writes; a part stores them only when it uses them: 602 file / view pairs of the corpus reference 1 .. 18 with no record) */
static const double STDV2[10][9] = {
    { 0, -1, 0, 1, 0, 0, 0, 0, 1 }, { 0, 0, -1, 1, 0, 0, 0, -1, 0 }, { 0, 0, 1, 1, 0, 0, 0, 1, 0 }, { 0, 1, 0, 1, 0, 0, 0, 0, -1 }, { 0, -1, 0, 0, 0, -1, 1, 0, 0 }, { 0, 1, 0, 0, 0, -1, -1, 0, 0 },
    { 0, -0.70710678118655, -0.70710678118655, 0.81649658092772, 0.40824829046386, -0.40824829046386, 0.57735026918962, -0.57735026918962, 0.57735026918962 },
    { 0, -0.70710678118655, 0.70710678118655, 0.70710678118655, -0.5, -0.5, 0.70710678118655, 0.5, 0.5 },
    { 1, 0, 0, 0, -1, 0, 0, 0, -1 }, { -1, 0, 0, 0, -1, 0, 0, 0, 1 } };
int mcam_std_view(int n, double m[9]) { if (n >= 9 && n <= 18) { memcpy(m, STDV2[n - 9], sizeof STDV2[0]); return 1; } if (n < 1 || n > 8) return 0; memcpy(m, STDV[n], sizeof STDV[n]); return 1; }
V3 mcam_arc_pt(const Ent *e, double a) {
    if (!e->vw) return (V3){ e->c.x + e->r * cos(a), e->c.y + e->r * sin(a), e->c.z };
    double ca = e->r * cos(a), sa = e->r * sin(a); const double *m = e->vm;
    return (V3){ e->c.x + ca * m[0] + sa * m[3], e->c.y + ca * m[1] + sa * m[4], e->c.z + ca * m[2] + sa * m[5] };
}
/* the polyline of a view arc: add_arc's chord count (3 degrees), points by mcam_arc_pt */
static void add_view_arc(McamFile *f, const Ent *e, uint8_t color, int level) {
    double sw = e->sw; if (!isfinite(sw) || fabs(sw) > 4 * M_PI) sw = 0;
    int n = (int)(fabs(sw) * 180 / M_PI / 3) + 2;
    if (n > 720) n = 720;
    int pi = newpoly(f, K_GEOM, 0, 0, color, level);
    for (int i = 0; i <= n; i++) polypt(f, pi, mcam_arc_pt(e, e->a0 + sw * i / n));
}
static void arc_finish(McamFile *f, int ent_index, uint8_t color, int level, V3 s, V3 e, V3 c, double r, double a0, double sw, int vw, const double *vm) {
        double sx = c.x + r * cos(a0), sy = c.y + r * sin(a0);
        Ent *en = &f->ents[ent_index]; en->first_poly = f->npolys;
        en->a = s; en->b = e; en->c = c; en->r = r; en->a0 = a0; en->sw = sw;
        if (vw) {                        /* a view arc (normal not world Z): drawn in its view */
            en->vw = vw; memcpy(en->vm, vm, sizeof en->vm); en->planar = 0;
            add_view_arc(f, en, color, level);
            f->ents[ent_index].npoly = f->npolys - f->ents[ent_index].first_poly;
            for (int k = f->ents[ent_index].first_poly; k < f->npolys; k++) f->polys[k].ln_style = (uint8_t)f->ents[ent_index].ln_style;
            return;
        }
        int planar = fabs(sx - s.x) < 1e-4 * (1 + r) && fabs(sy - s.y) < 1e-4 * (1 + r) && fabs(s.z - e.z) < 1e-6;
        en->planar = planar;
        if (planar) {
            add_arc(f, K_GEOM, 0, 0, color, level, c, r, a0, sw, 0, 1, 2, c.z, c.z);
        } else {                         /* arc in another plane: rotate start vector about the arc normal */
            V3 u = { s.x - c.x, s.y - c.y, s.z - c.z }, w = { e.x - c.x, e.y - c.y, e.z - c.z };
            V3 nr = { u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x };
            double nl = sqrt(nr.x * nr.x + nr.y * nr.y + nr.z * nr.z);
            int p = newpoly(f, K_GEOM, 0, 0, color, level);
            if (nl < 1e-12) { polypt(f, p, s); polypt(f, p, e); }
            else {
                nr.x /= nl; nr.y /= nl; nr.z /= nl;
                V3 v = { nr.y * u.z - nr.z * u.y, nr.z * u.x - nr.x * u.z, nr.x * u.y - nr.y * u.x };
                double asw = fabs(sw);
                if (!isfinite(asw) || asw > 4 * M_PI) asw = 0;
                int k = (int)(asw * 180 / M_PI / 3) + 2;
                for (int i = 0; i <= k; i++) {
                    double a = asw * i / k;
                    V3 q = { c.x + u.x * cos(a) + v.x * sin(a), c.y + u.y * cos(a) + v.y * sin(a), c.z + u.z * cos(a) + v.z * sin(a) };
                    polypt(f, p, q);
                }
            }
            f->arc_fixups++;
        }
        f->ents[ent_index].npoly = f->npolys - f->ents[ent_index].first_poly;
        for (int k = f->ents[ent_index].first_poly; k < f->npolys; k++) f->polys[k].ln_style = (uint8_t)f->ents[ent_index].ln_style;
}

/* ---- the MastercamPlanes archive (record u32 +0 0x4000, i32 +0x2c 36, u8 +0x56 != 0; the CArchive at +0x68; spec
   formats/planes_archive.md, mastercam.dll MastercamPlanes::WriteToDisk 0x1802bef70 / ::Serialize 0x1802bc580, SystemPlanes
   0x180434090, PlaneBase 0x1803e0e20, PlaneID 0x1803f4990, Plane 0x1803ddb80, PlanePoint 0x180432650, Matrix33 0x18042a880,
   P3D 0x180430240, the map FUN_1802a3560; tools/planes_list.py is the reference reader). Only the PlaneID class / id of each
   plane are kept; any surprise = not read (planes_ok 0). MFC rules: one index table for classes and objects from 1. */
#define PAR_MAX 2048
typedef struct { const uint8_t *b; size_t n, p; int bad; int ncls; char cls[PAR_MAX][24]; int schema[PAR_MAX]; int nobj; } PAr;
static unsigned par_u8(PAr *a) { if (a->p + 1 > a->n) { a->bad = 1; return 0; } return a->b[a->p++]; }
static unsigned par_u16(PAr *a) { if (a->p + 2 > a->n) { a->bad = 1; return 0; } unsigned v = a->b[a->p] | a->b[a->p + 1] << 8; a->p += 2; return v; }
static uint32_t par_u32(PAr *a) { if (a->p + 4 > a->n) { a->bad = 1; return 0; } uint32_t v = rd32(a->b + a->p); a->p += 4; return v; }
static void par_skip(PAr *a, size_t k) { if (a->p + k > a->n) { a->bad = 1; a->p = a->n; } else a->p += k; }
static void par_str_to(PAr *a, char *out, size_t outn) {      /* AfxReadStringLength + the bytes (OUT: a copy, may be NULL) */
    int wide = 0; uint32_t k;
    for (;;) { k = par_u8(a); if (k == 0xff) { k = par_u16(a); if (k == 0xfffe) { wide = 1; continue; } if (k == 0xffff) k = par_u32(a); } break; }
    if (out && outn) out[0] = 0;
    if (!a->bad && out && outn && !wide && a->p + k <= a->n) { size_t c = k < outn - 1 ? k : outn - 1; memcpy(out, a->b + a->p, c); out[c] = 0; }
    if (!a->bad) par_skip(a, (size_t)k * (wide ? 2 : 1));
}
static void par_str(PAr *a) { par_str_to(a, NULL, 0); }
/* a tag: new class (0xffff schema len name) / class ref (0x8000 | i, or 0x7fff + u32 with bit 31) -> its schema and name; an
   object reference (incl. null) -> -1 / -2 */
static int par_tag(PAr *a, const char **name) {
    unsigned w = par_u16(a); uint32_t idx;
    if (a->bad) return -2;
    if (w == 0xffff) {
        unsigned sch = par_u16(a), ln = par_u16(a);
        int k = a->ncls + a->nobj + 1;
        if (a->bad || ln >= 24 || a->p + ln > a->n || k >= PAR_MAX) { a->bad = 1; return -2; }
        memcpy(a->cls[k], a->b + a->p, ln); a->cls[k][ln] = 0; a->schema[k] = (int)sch; a->p += ln; a->ncls++;
        *name = a->cls[k]; return (int)sch;
    }
    if (w == 0x7fff) { uint32_t d = par_u32(a); if (!(d & 0x80000000u)) return d ? -1 : -2; idx = d & 0x7fffffffu; }
    else if (w & 0x8000) idx = w & 0x7fff;
    else return w ? -1 : -2;
    if (idx < 1 || idx >= PAR_MAX || !a->cls[idx][0]) { a->bad = 1; return -2; }
    *name = a->cls[idx]; return a->schema[idx];
}
static int par_level(PAr *a, const char *want) {               /* CArchive::SerializeClass: the level tag of class want */
    const char *nm = ""; int v = par_tag(a, &nm);
    if (v < 0 || strcmp(nm, want)) { a->bad = 1; return -1; }
    return v;
}
/* P3D::Serialize 0x180430240: 4 x f64 (+8 x, +0x10 y, +0x18 z, +0x20 tolerance): out (may be NULL) takes x, y, z */
static void par_p3d(PAr *a, double *out) {
    int v = par_level(a, "P3D"); if (v != 1 && v != 2) { a->bad = 1; return; }
    if (out && a->p + 32 <= a->n) for (int i = 0; i < 3; i++) memcpy(&out[i], a->b + a->p + 8 * i, 8);
    par_skip(a, 32);
}
/* Matrix33::Serialize 0x18042a880: 3 x P3D rows (+8, +0x38, +0x68): the x / y / z axes in world coordinates */
static void par_m33(PAr *a, double *m) { int v = par_level(a, "Matrix33"); if (v != 1 && v != 2) { a->bad = 1; return; } for (int i = 0; i < 3 && !a->bad; i++) par_p3d(a, m ? m + 3 * i : NULL); }
/* PlanePoint::Serialize 0x180432650: P3D +0x10 (GetPointInWorldCoordinates 0x1800a9270), then the fields below */
static void par_ppt(PAr *a, double *org) { int v = par_level(a, "PlanePoint"); if (v != 1 && v != 2) { a->bad = 1; return; } par_p3d(a, org); par_skip(a, 8 + 4 + 16 + 1 + 16 + 1 + 4); }
/* NM (may be NULL): PlaneBase +0x110 name; Plane +0x358 the second CString, +0x360 the bool after it (formats/planes_archive.md;
   read by nci_write.c's bnci_wr_views port) */
typedef struct { char name[0x80], s358[0x80]; int b360; double m[9], org[3]; int wofs, disp, coord, ptype; int b118; } ParNames;
static void par_base(PAr *a, int *cls, int *id, ParNames *nm) {
    if (par_level(a, "PlaneBase") != 4) { a->bad = 1; return; }
    if (par_level(a, "PlaneID") != 1) { a->bad = 1; return; }
    par_skip(a, 16); *cls = (int32_t)par_u32(a); *id = (int32_t)par_u32(a);
    par_m33(a, nm ? nm->m : NULL); par_str_to(a, nm ? nm->name : NULL, nm ? sizeof nm->name : 0);
    if (nm && a->p < a->n) nm->b118 = a->b[a->p];                 /* PlaneBase +0x118 (then +0x119) */
    par_skip(a, 2);
}
static void par_plane(PAr *a, int *cls, int *id, ParNames *nm) {
    if (par_level(a, "Plane") != 4) { a->bad = 1; return; }    /* v1..3: no sample, not read */
    par_base(a, cls, id, nm); par_skip(a, 4);
    for (int i = 0; i < 4 && !a->bad; i++) par_ppt(a, i == 0 && nm ? nm->org : NULL);   /* +0x130: GetOriginPoint 0x1800a9220 */
    static const char T[] = "IIBIIIIBIBSBIIBII";               /* +0x330 .. +0x374; S = +0x358, the B after it = +0x360 */
    for (int i = 0; T[i] && !a->bad; i++) {
        if (T[i] == 'I') { uint32_t v = 0; if (a->p + 4 <= a->n) memcpy(&v, a->b + a->p, 4); par_skip(a, 4);
            if (nm && i == 0) nm->wofs = (int32_t)v;      /* +0x330 the plane's work offset (-1 = none) */
            if (nm && i == 1) nm->disp = (int32_t)v;      /* +0x334 Display (PlanesMgrBar::SetPlanesListIcons) */
            if (nm && i == 6) nm->coord = (int32_t)v;     /* +0x348 Coordinate: 0 Mill, 1 Lathe X, 2 Lathe D (opmanres 142 / 153 / 187) */
            if (nm && i == 13) nm->ptype = (int32_t)v; }  /* +0x368 PlaneType */
        else if (T[i] == 'B') { if (nm && i == 11 && a->p < a->n) nm->b360 = a->b[a->p]; par_skip(a, 1); }
        else par_str_to(a, nm ? nm->s358 : NULL, nm ? sizeof nm->s358 : 0);
    }
}
/* the archive's planes: f != NULL fills planes_ok / maxcls / hi2 / n (mcam_load); out != NULL lists them (mcam_planes_list) */
static int planes_archive_x(McamFile *f, const uint8_t *b, size_t n, McamPlaneInfo *out, int max);
static void planes_archive(McamFile *f, const uint8_t *b, size_t n) { planes_archive_x(f, b, n, NULL, 0); }
int mcam_planes_list(const uint8_t *b, size_t n, McamPlaneInfo *out, int max) { return planes_archive_x(NULL, b, n, out, max); }
static int planes_archive_x(McamFile *f, const uint8_t *b, size_t n, McamPlaneInfo *out, int max) {
    int nout = 0;
    PAr *a = calloc(1, sizeof *a); if (!a) return 0;
    a->b = b; a->n = n;
    int v = par_level(a, "MastercamPlanes"), sv = par_level(a, "SystemPlanes"), maxcls = 0, hi2 = 0, np = 0;
    if (v != 4 || sv < 1 || sv > 4) a->bad = 1;
    if (!a->bad) {
        if (sv < 4) par_skip(a, 6);
        double um[9] = { 0 }, uo[3] = { 0 };
        par_m33(a, um); par_p3d(a, uo); if (sv < 3) par_skip(a, 4);   /* SystemPlanes::Serialize 0x180434090 */
        { int ids[8]; for (int i = 0; i < 8; i++) ids[i] = (int)par_u32(a); if (f) for (int i = 0; i < 4; i++) { f->planes_act[i] = ids[i]; f->planes_org[i] = ids[i + 4]; } }   /* SystemPlanes::GetPlaneID / GetOriginID */
        if (f) { memcpy(f->planes_um, um, sizeof um); memcpy(f->planes_uo, uo, sizeof uo); }
        for (int i = 0; i < 4 && !a->bad; i++) par_m33(a, NULL);       /* +0x130: GetPreviousMatrix per type */
        for (int i = 0; i < 4 && !a->bad; i++) par_p3d(a, NULL);       /* +0x470: GetPreviousOrigin per type */
        par_skip(a, 16);
    }
    unsigned cnt = a->bad ? 0 : par_u16(a);                      /* ReadCount */
    if (cnt == 0xffff) cnt = par_u32(a);
    for (unsigned i = 0; i < cnt && !a->bad; i++) {
        par_str(a);                                              /* the key "{GUID}~class~id" */
        const char *nm = ""; int sch = par_tag(a, &nm), cls = 0, id = 0;   /* ReadObject: the value's class tag */
        if (sch == -2) { if (a->bad) break; continue; }          /* null */
        if (sch < 0) { a->bad = 1; break; }                      /* a back reference: not expected here */
        a->nobj++;                                               /* the object takes the next index */
        char cn[24]; snprintf(cn, sizeof cn, "%s", nm);
        ParNames pn; memset(&pn, 0, sizeof pn);
        int isplane = !strcmp(cn, "Plane");
        if (isplane) par_plane(a, &cls, &id, &pn); else if (!strcmp(cn, "PlaneBase")) par_base(a, &cls, &id, &pn); else a->bad = 1;
        if (!a->bad) { np++; if (cls > maxcls) { maxcls = cls; hi2 = 0; } if (cls == 2 && id > hi2) hi2 = id; }
        if (!a->bad && (f || (out && nout < max))) {
            McamPlaneInfo info; memset(&info, 0, sizeof info);
            info.cls = cls; info.id = id; info.is_plane = isplane; info.b360 = pn.b360; memcpy(info.m, pn.m, sizeof pn.m); memcpy(info.org, pn.org, sizeof pn.org);
            info.wofs = isplane ? pn.wofs : -1; info.disp = pn.disp; info.coord = pn.coord; info.ptype = pn.ptype; info.b118 = pn.b118;
            snprintf(info.name, sizeof info.name, "%s", pn.name); snprintf(info.s358, sizeof info.s358, "%s", pn.s358);
            if (f) { f->planes_list = xrealloc(f->planes_list, (f->nplanes_list + 1) * sizeof *f->planes_list); f->planes_list[f->nplanes_list++] = info; }
            if (out && nout < max) out[nout++] = info;
        }
    }
    int ok = !a->bad && a->p == a->n;
    if (ok && f) { f->planes_ok = 1; f->planes_maxcls = maxcls; f->planes_hi2 = hi2; f->planes_n = np; }
    else if (f) { free(f->planes_list); f->planes_list = NULL; f->nplanes_list = 0; }   /* an archive that did not parse to its end: nothing from it is used (ok = 0) */
    free(a);
    return ok ? nout : -1;
}

/* a spline curve (E_SPLINE) lies in a plane parallel to XY when every control point (NURBS: z / w of the homogeneous points) or
   the z polynomial of every segment (parametric: constant term at z, no higher terms) stays at the start's z - ours, the
   counterpart of the lines' test (ends within 1e-6 in z); dimension 2: z = 0 */
static int spline_planar(const CurveRec *c, double z) {
    const uint8_t *r = c->rec;
    if (c->cls == 0x8) {
        int ns = (int32_t)rd32(r + 0xb0), ord = r[0xb4], dim = r[0xb5];
        if (dim < 3) return fabs(z) < 1e-6;
        for (int i = 0; i < ns; i++) for (int k = 0; k < ord; k++) {
            double v = rdd(r + 0xc0 + 8 * ((size_t)ord * dim * i + (size_t)k * dim + 2));
            if (k ? fabs(v) >= 1e-9 : fabs(v - z) >= 1e-6) return 0;
        }
        return 1;
    }
    int nk = (int32_t)rd32(r + 0xc8), nc = (int32_t)rd32(r + 0xcc), vpp = r[0xd1], rat = !((r[0xd2] >> 2) & 1), dim = vpp - rat;
    if (dim < 3) return fabs(z) < 1e-6;
    const uint8_t *C = r + 0xd8 + 8 * (size_t)nk;
    for (int i = 0; i < nc; i++) {
        const uint8_t *q = C + 8 * (size_t)vpp * i;
        double w = rat ? rdd(q) : 1.0, cz = rdd(q + 8 * (rat + 2));
        if (!(fabs(w) > 1e-300) || fabs(cz / w - z) >= 1e-6) return 0;
    }
    return 1;
}
/* its polyline (display and picks): the stored start, the points at brk_spline_const_err's break parameters, the stored end -
   the toolpath breaking (cam2d.c cam_chain_polyline) at curve_chord_dev (config 1006: 0.001 in / 0.02 mm, mccore FUN_1803bc7d0),
   curve_min_step = the same, curve_max_step 100, tol 5e-5. Ours: Mastercam's own spline drawing (mastercam.dll) is NOT DECODED. */
static void spline_poly_dev(McamFile *f, int pi, const CurveRec *c, V3 a, V3 b, double dev);
static void spline_poly(McamFile *f, int pi, const CurveRec *c, V3 a, V3 b) { spline_poly_dev(f, pi, c, a, b, load_units == 0 ? 0.001 : 0.02); }
static void spline_poly_dev(McamFile *f, int pi, const CurveRec *c, V3 a, V3 b, double dev) {
    double t1, t2, t, o[3][3];
    mcs_spline_t1t2(c, &t1, &t2); t = t1;
    polypt(f, pi, a);
    for (int it = 0; it < 200000 && t <= t2 - 2e-7; it++) {
        double t0 = t; mcs_brk_spline_const_err(c, dev, 100.0, dev, 0, 5e-5, &t);
        if (!(t > t0)) break;
        if (t > t2 - 2e-7) break;                             /* the last piece ends at the stored end point */
        if (mcs_spline_eval_d(c, t, 0, o)) break;
        polypt(f, pi, (V3){ o[0][0], o[0][1], o[0][2] });
    }
    polypt(f, pi, b);
}

static void entity_rec(McamFile *f, const uint8_t *rec, uint32_t n);
/* every record's entities: the line width (ent +0x10, a double; mastercam.dll FUN_18046a280 kind 2 reads it as int) */
static void entity(McamFile *f, const uint8_t *rec, uint32_t n) {
    int e0 = f->nents, p0 = f->npolys;
    entity_rec(f, rec, n);
    if (n < 0x18) return;
    double w; memcpy(&w, rec + 0x10, 8); if (!(w >= 1 && w < 100)) w = 1;
    for (int i = e0; i < f->nents; i++) f->ents[i].width = w;
    for (int k = p0; k < f->npolys; k++) if (f->polys[k].kind == K_GEOM) f->polys[k].width = (uint8_t)w;
}
static void entity_rec(McamFile *f, const uint8_t *rec, uint32_t n) {
    if (n < 0x68) return;
    cur_rec = rec;
    uint32_t cls = rd32(rec);
    /* an ASSOC record is read only while its selection bit 1 is on (mccore FUN_1802d3990: sel_bit_is_on(ent, 1) before
       ProcessAssocEntity); lib_delete turns the bit off to delete an op / tool from a library or defaults file */
    if (cls == 0x4000 && !(rd16(rec + 4) & 1)) return;
    uint8_t color = rec[6];
    int level = (int32_t)rd32(rec + 8);
    if (cls == 0x1 || cls == 0x2 || cls == 0x4 || cls == 0x8 || cls == 0x80 || cls == 0x400) {   /* every curve record, by eptr (mcsurf.c; 0x400 surface curves) */
        f->crvs = xrealloc(f->crvs, (f->ncrvs + 1) * sizeof *f->crvs);
        CurveRec *cr = &f->crvs[f->ncrvs++];
        cr->eptr = (int64_t)rd64(rec + 0x20); cr->cls = cls; cr->rec = xmalloc(n); memcpy(cr->rec, rec, n); cr->n = n;
    }
    int ln_style = (int16_t)rd16(rec + 0x18);               /* line style (ent +0x18, set_gl_style; 1 = solid) */
    if (cls == 0x1 && n >= 0x68 + 24) {
        V3 p = rdv(rec + 0x68);
        Ent *en = add_ent(f, E_POINT, level, color); en->a = en->b = p; en->ln_style = ln_style;
        en->pt_style = (int16_t)rd16(rec + 0x1c);           /* point style (pt.c: ent+0x1c); its marker is drawn per view by
                                                               scene.c (draw_3d_point_style), the polylines are the bare point */
        int a = newpoly(f, K_GEOM, 0, 0, color, level); polypt(f, a, p); polypt(f, a, p);
        int b = newpoly(f, K_GEOM, 0, 0, color, level); polypt(f, b, p); polypt(f, b, p);
        f->polys[a].ln_style = f->polys[b].ln_style = (uint8_t)ln_style;
        f->npoints++;
        f->ents[f->nents - 1].npoly = 2;
    } else if (cls == 0x2 && n >= 0x68 + 48) {
        Ent *en = add_ent(f, E_LINE, level, color); en->a = rdv(rec + 0x68); en->b = rdv(rec + 0x80);
        en->planar = fabs(en->a.z - en->b.z) < 1e-6; en->ln_style = ln_style;
        int p = newpoly(f, K_GEOM, 0, 0, color, level);
        polypt(f, p, rdv(rec + 0x68)); polypt(f, p, rdv(rec + 0x80));
        f->polys[p].ln_style = (uint8_t)ln_style;
        f->nlines++;
        f->ents[f->nents - 1].npoly = 1;
    } else if (cls == 0x4 && n >= 0x68 + 96) {
        V3 s = rdv(rec + 0x68), e = rdv(rec + 0x80), c = rdv(rec + 0x98);
        double r = rdd(rec + 0xb0), a0 = rdd(rec + 0xb8), sw = rdd(rec + 0xc0);
        int view = n >= 0xca ? rd16(rec + 0xc8) : 1;
        add_ent(f, E_ARC, level, color);
        f->ents[f->nents - 1].ln_style = ln_style;
        if (view >= 2) {                         /* view arc: finished after the last record (views may follow) */
            pend_arcs = xrealloc(pend_arcs, (npend_arcs + 1) * sizeof *pend_arcs);
            pend_arcs[npend_arcs].ent = f->nents - 1; pend_arcs[npend_arcs].rec = rec; npend_arcs++;
        } else arc_finish(f, f->nents - 1, color, level, s, e, c, r, a0, sw, 0, NULL);
        f->ents[f->nents - 1].avw = view;
        f->narcs++;
    } else if (cls == 0x80 && n > 0xd1 && rec[0xd1] == 2) {
        /* a 2D NURBS (2 values per point): a surface curve's parameter curve, in (u, v) - no Ent (291 of 291 in the corpus are referenced
           by a CONS record's +0xd8, ent +0x28 refs 1; put_cons_in_db / inc_refs_eptr, decomp/curvesurf/cs2.c 171) */
    } else if (cls == 0x400 && n >= 0x68 + 0x88) {                                      /* surface curve (E_SCRV): its polyline after the last record */
        Ent *en = add_ent(f, E_SCRV, level, color); en->a = rdv(rec + 0x68); en->b = rdv(rec + 0x80); en->ln_style = ln_style;
        en->crv = f->ncrvs - 1; en->planar = 0; en->npoly = 0;
    } else if ((cls == 0x8 || cls == 0x80) && n >= 0xa8 && mcs_spline_ok(&f->crvs[f->ncrvs - 1])) {   /* spline curve (E_SPLINE) */
        const CurveRec *cr = &f->crvs[f->ncrvs - 1];
        Ent *en = add_ent(f, E_SPLINE, level, color); en->a = rdv(rec + 0x68); en->b = rdv(rec + 0x80); en->ln_style = ln_style;
        en->crv = f->ncrvs - 1; en->planar = spline_planar(cr, en->a.z);
        V3 a = en->a, b = en->b;
        int p = newpoly(f, K_GEOM, 0, 0, color, level);
        spline_poly(f, p, cr, a, b);
        f->polys[p].ln_style = (uint8_t)ln_style;
        f->ents[f->nents - 1].npoly = 1;
    } else if (cls == 0x200 && n >= 0xb8 && rd16(rec + 0x68) == 0) {                   /* view: number + matrix_33 */
        views = xrealloc(views, (nviews + 1) * sizeof *views);
        views[nviews].n = rd16(rec + 0x6a);
        for (int k = 0; k < 9; k++) views[nviews].m[k] = rdd(rec + 0x70 + 8 * k);
        nviews++;
    } else if (cls == 0x200 && n >= 0xc0 && rd16(rec + 0x68) == 1) {                   /* the view record, sub 1 (store_db_ent 0x1802c5dd0 applies it): the construction depth, the Gview, the window */
        f->view_rec = 1; f->view_gv = (int16_t)rd16(rec + 0x6c);
        f->view_center[0] = rdd(rec + 0xa0); f->view_center[1] = rdd(rec + 0xa8);
        f->constr_depth = rdd(rec + 0xb0); f->view_height = rdd(rec + 0xb8);
    } else if (cls == 0x100 && n >= 0x69) {                                             /* SURF_ID: kept as read (occt_mesh_surf meshes kind 0) */
        f->surfs = xrealloc(f->surfs, (f->nsurfs + 1) * sizeof *f->surfs);
        Surf *sf = &f->surfs[f->nsurfs++];
        sf->idn = (int32_t)rd32(rec + 0x34); sf->eptr = (int64_t)rd64(rec + 0x20); sf->level = level; sf->color = color; sf->kind = rec[0x68]; sf->blanked = (rd16(rec + 4) & 0x100) != 0;
        sf->rec = xmalloc(n); memcpy(sf->rec, rec, n); sf->n = n; sf->ent = -1; sf->off = 0; sf->edited = 0;
    } else if (cls == 0x100000 && n >= 0x128) {                                         /* PMESH_ID (formats/cad_import.md 7): the record without its
                                                                                           +0x70 (FUN_1802c62d0 reads it, FUN_1802c28d0 puts the entry
                                                                                           -1 back), then the entry's data, n - 0x128 bytes */
        f->meshes = xrealloc(f->meshes, (f->nmeshes + 1) * sizeof *f->meshes);
        McMesh *m = &f->meshes[f->nmeshes++]; memset(m, 0, sizeof *m);
        memcpy(m->rec, rec, 0x70); memset(m->rec + 0x70, 0xff, 8); memcpy(m->rec + 0x78, rec + 0x70, 0x130 - 0x78);
        m->kind = (int16_t)rd16(rec + 0x68); m->idn = (int32_t)rd32(rec + 0x34); m->eptr = (int64_t)rd64(rec + 0x20);
        m->level = level; m->color = color; m->blanked = (rd16(rec + 4) & 0x100) != 0; m->ent = -1;
        /* StorePolygonalMeshDataFromStream (0x1805cca70): kind 0 = CreateSTLEntryFromStream(stream, size, binary 1); kinds 2 (IFF) / 3 (MCO):
           NOT DECODED here - the record is kept (the writer copies it), nothing is drawn */
        if (m->kind == 0) { int c = mcam_stl_entry_bin(rec + 0x128, n - 0x128, &m->p, &m->mixed); m->np = c > 0 ? c : 0; }
    } else if (cls == 0x10000 && n >= 0xac) {                                           /* SOLID_ID: its XT blob is "sld_N" */
        f->sld_ids = xrealloc(f->sld_ids, (f->nsld_ids + 1) * sizeof *f->sld_ids);
        f->sld_ids[f->nsld_ids][0] = (int32_t)rd32(rec + 0x34); f->sld_ids[f->nsld_ids][1] = (int32_t)rd32(rec + 0xa8);
        f->sld_ids[f->nsld_ids][2] = (int32_t)rd32(rec + 8); f->sld_ids[f->nsld_ids][3] = rec[6]; f->nsld_ids++;
    } else if (cls == 0x4000 && n > 0x68 && (int32_t)rd32(rec + 0x2c) == 36 && rec[0x56] && !f->planes_ok) {   /* MastercamPlanes */
        planes_archive(f, rec + 0x68, n - 0x68);
    } else if (cls == 0x4000 && n >= 0x70) {
        int sub = (int32_t)rd32(rec + 0x2c);
        if (sub == 8 && n > 0x68) {                                           /* LTL_ID (lathe tool; mcam_load.h ltl) */
            f->ltl = xrealloc(f->ltl, sizeof *f->ltl * (size_t)(f->nltl + 1));
            struct McLtl *t = &f->ltl[f->nltl++];
            t->idn = (int32_t)rd32(rec + 0x34); t->n = n - 0x68; t->body = xmalloc(t->n); memcpy(t->body, rec + 0x68, t->n);
        }
        if ((sub == 4 || sub == 12 || sub == 13 || sub == 15 || sub == 21 || sub == 22 || sub == 26) && n > 0x68) {   /* machine definition (mcam_load.h mdrec); sub 4 = the part's materials (TpMatList, MATL ents) */
            f->mdrec = xrealloc(f->mdrec, sizeof *f->mdrec * (size_t)(f->nmdrec + 1));
            struct McMdRec *d = &f->mdrec[f->nmdrec++];
            d->sub = sub; d->idn = (int32_t)rd32(rec + 0x34); d->n = n - 0x68; d->body = xmalloc(d->n); memcpy(d->body, rec + 0x68, d->n);
        }
        if (sub == 10 && n >= 0x68 + 0x90) {                                  /* LBDRY_ID (lathe boundary; mcam_load.h) */
            f->lbdry = xrealloc(f->lbdry, sizeof *f->lbdry * (size_t)(f->nlbdry + 1));
            struct McLbdry *b = &f->lbdry[f->nlbdry++];
            b->idn = (int32_t)rd32(rec + 0x34); b->n = n - 0x68; b->rec = xmalloc(b->n); memcpy(b->rec, rec + 0x68, b->n);
        }
        if (sub == 3 && f->ntools < 256 && n >= 0x68 + 0xce) {                 /* TL_ID */
            Tool *t = &f->tools[f->ntools++];
            const uint8_t *b = rec + 0x68;                                        /* same layout as .tooldb OpToolInfo */
            memset(t, 0, sizeof *t);
            t->num = (int32_t)rd32(b);
            t->type = (int16_t)rd16(b + 0x4); t->rad_type = (int16_t)rd16(b + 0x6);
            t->dia = rdd(b + 0x8);
            t->corner_r = rdd(b + 0x10); t->pitch = rdd(b + 0x18); t->tip_angle = rdd(b + 0x20);
            t->dia_off = (int32_t)rd32(b + 0x28); t->len_off = (int32_t)rd32(b + 0x2c);
            t->feed = rdd(b + 0x30); t->plunge = rdd(b + 0x38); t->retract = rdd(b + 0x40);
            t->rpm = (int32_t)rd32(b + 0x48);
            t->flutes = (int16_t)rd16(b + 0x52); t->tool_mat = (int16_t)rd16(b + 0x54);   /* CheckNumberOfFlutes / CalcFpt */
            if (n >= 0x68 + 0x259) t->is_metric = b[0x258] != 0;                        /* toollib_net.md: IsMetric @0x258 */
            cstr(t->name, sizeof t->name, b + 0x56, 0x78);
            t->slot = n >= 0x2cc + 4 ? (int32_t)rd32(rec + 0x2cc) : -1;              /* part_tools.md 1.1: +0x264 TlMgr slot */
            tl_body[f->ntools - 1] = n >= 0x68 + 0x288 ? b : NULL;
            if (n >= 0x68 + 0x288) { memcpy(t->op_info, b, 0x288); t->has_op_info = 1; }
            if (n >= 0x68 + 0x480) {     /* tp_tool tail, TlToolMill::SetFromOldTool (decomp/mccore_tooladd.c); the TlMgr
                                            values (part_tools_apply) replace these when the archive has the tool */
                t->req_pilot_dia = rdd(b + 0x288); t->flute_len = rdd(b + 0x290); t->oal = rdd(b + 0x298);
                t->shoulder_len = rdd(b + 0x2a0); t->arbor_dia = rdd(b + 0x2a8); t->shoulder_dia = rdd(b + 0x470); t->taper_len = rdd(b + 0x478);
                t->tl_tip_dia = rdd(b + 0x3f8);   /* engrave.dll FUN_18001bb30: the tip diameter of a chamfer mill / engrave tool */
            }
            /* TlToolThreading::SetOpToolInfo (decomp/mccore_tooladd.c): +0x18 holds threads per inch for an inch tool
               (pitch = 1 / value when not metric and value > tol) */
            if ((t->type == 4 || t->type == 5 || t->type == 24) && !t->is_metric && t->pitch > 1e-9) t->pitch = 1.0 / t->pitch;
        } else if (sub == 33 && rec[0x56]) {                                   /* TlMgr archive (part_tools.md 1) */
            tlmgr_rec = xrealloc(tlmgr_rec, sizeof *tlmgr_rec * (ntlmgr + 1)); tlmgr_n = xrealloc(tlmgr_n, sizeof *tlmgr_n * (ntlmgr + 1));
            tlmgr_rec[ntlmgr] = rec; tlmgr_n[ntlmgr++] = n;
        } else if ((sub == 22 && n == 5000) || sub == 23 || (sub == 24 && n >= 0x6c)) {   /* machine / control / control page:
                                                                                  resolved after the last record (resolve_machine) */
            if (nmcr == cmcr) { cmcr = cmcr ? 2 * cmcr : 64; mcr = xrealloc(mcr, (size_t)cmcr * sizeof *mcr); }
            mcr[nmcr].r = rec; mcr[nmcr].n = n; mcr[nmcr].sub = sub; nmcr++;
        } else if (sub == 2 && n >= 0x100) {                                   /* TP_ID */
            f->tp = xrealloc(f->tp, (f->ntp + 1) * sizeof(TpRef));
            TpRef *t = &f->tp[f->ntp++]; memset(t, 0, sizeof *t);
            /* formats/surface_selection.md 2.1. tp_sh: a Mastercam 2018 record (0x398 bytes; file major 0x14) has every field from +0x98
               on 0x10 later than an X9 / 2017 one (0x388 bytes) [measured on all 7083 TP records of the corpus' 395 2018 parts: kinds 1,
               4, 6, 7, 0x17; kind 30 has no 2018 example and is read by the same rule] */
            uint32_t tp_sh = n >= 0x398 ? 0x10 : 0; const uint8_t *q = rec + tp_sh;
            t->op_id = (int32_t)rd32(rec + 0x68); t->ent = (int32_t)rd32(rec + 0x6c); t->cls = rd32(rec + 0x74);
            t->ecls = rd32(rec + 0x78); t->eptr = (int64_t)rd64(rec + 0x80);
            t->kind = (int16_t)rd16(rec + 0x90); t->chain = (int32_t)rd32(q + 0x98); t->index = (int32_t)rd32(q + 0x9c);
            if (t->kind == 4) { t->flags = rd16(q + 0x98); t->link = (int16_t)rd16(q + 0x9a); }
            t->rev = (int32_t)rd32(q + 0xa8); t->role = q[0xe0];
            t->xyz[0] = rdd(q + 0x98); t->xyz[1] = rdd(q + 0xa0); t->xyz[2] = rdd(q + 0xa8); t->t = rdd(q + 0xc8);
            t->pmode = q[0xb0];
            if (t->kind == 30 && n > 0x110 + tp_sh) { t->tab_w = rdd(q + 0xd0); t->tab_h = rdd(q + 0xd8); t->tab_full = q[0xe0];
                t->tab_ramp = (int16_t)rd16(q + 0xe4); t->tab_ang = rdd(q + 0xf8); t->tab_ed = q[0x110]; }
        } else if (sub == 1 && n >= 0xe8) {                                    /* OP_ID */
            Op *o = find_op(f, (int32_t)rd32(rec + 0x68), 1);
            free(o->raw); o->raw = xmalloc(n); memcpy(o->raw, rec, n); o->raw_n = n;
            o->opcode = rd16(rec + 0x6c);
            o->seq = rd16(rec + 0xe6);
            o->slot = n >= 0x5ac + 4 ? (int32_t)rd32(rec + 0x5ac) : -1;                 /* part_tools.md 1.1 */
            o->regen = n > 0xfc && rec[0xfc] != 0;   /* op +0x94 dirty as saved (record +0xfc; InitTvItem's X image, DirtyOperationWarning) */
            cstr(o->comment, sizeof o->comment, rec + 0x6e, 0x16a);
            snprintf(o->type, sizeof o->type, "%s", mcam_opcode_name(o->opcode));
        }
    }
}

static void nci(McamFile *f, const uint8_t *d, size_t len, size_t off) {
    static uint8_t buf[0x370];
    V3 cur = { 0, 0, 0 };
    int op = 0, tool = 0, line = -1, linekind = -1;
    while (off + 2 <= len) {
        uint16_t sz = rd16(d + off);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        memset(buf, 0, sizeof buf);
        memcpy(buf, d + off + 2, sz < sizeof buf ? sz : sizeof buf);
        off += 2 + sz;
        int16_t g = (int16_t)rd16(buf);
        if (g == -2) {
            op = (int32_t)rd32(buf + 0x1c);
            find_op(f, op, 1);
            line = -1;
        } else if (g == 1016) {                     /* misc assoc: +0x40 cc code, +0x42 work offset, +0x44 met_tool */
            int mt = (int16_t)rd16(buf + 0x44);
            if (mt == 0 || mt == 1) { if (f->metric < 0) f->metric = mt; else if (f->metric != mt) f->metric = 2;
                                      if (f->nci_metric < 0) f->nci_metric = mt; else if (f->nci_metric != mt) f->nci_metric = 2; }
        } else if (g >= 1000 && g <= 1002) {
            tool = (int32_t)rd32(buf + 0x24);
            Op *o = find_op(f, op, 1); if (!o->tool) o->tool = tool;
            cur = rdv(buf + 0x50);
            line = -1;
        } else if (g == 0 || g == 1) {
            V3 p = rdv(buf + 0x20);
            int kind = rdd(buf + 0x38) < 0 ? K_RAPID : K_CUT;
            if (line < 0 || linekind != kind) { line = newpoly(f, kind, op, tool, 0, 0); linekind = kind; polypt(f, line, cur); }
            polypt(f, line, p);
            cur = p; f->nmoves++;
        } else if (g == 2 || g == 3) {
            static const int AX[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };
            int pl = (int16_t)rd16(buf + 0x18);
            if (pl < 0 || pl > 2) pl = 0;
            int au = AX[pl][0], av = AX[pl][1], ad = 3 - au - av;
            V3 e = rdv(buf + 0x20), c = rdv(buf + 0x38);
            double end[3], cen[3], st[3] = { cur.x, cur.y, cur.z };
            end[au] = e.x; end[av] = e.y; end[ad] = e.z;
            cen[au] = c.x; cen[av] = c.y; cen[ad] = st[ad];
            double r = hypot(st[au] - cen[au], st[av] - cen[av]);
            double a0 = atan2(st[av] - cen[av], st[au] - cen[au]), a1 = atan2(end[av] - cen[av], end[au] - cen[au]);
            double sw = g == 3 ? fmod(a1 - a0 + 4 * M_PI, 2 * M_PI) : -fmod(a0 - a1 + 4 * M_PI, 2 * M_PI);
            if (fabs(sw) < 1e-9) sw = g == 3 ? 2 * M_PI : -2 * M_PI;
            V3 cv = { cen[0], cen[1], cen[2] };
            add_arc(f, K_CUT, op, tool, 0, 0, cv, r, a0, sw, au, av, ad, st[ad], end[ad]);
            cur.x = end[0]; cur.y = end[1]; cur.z = end[2];
            line = -1; f->nnciarcs++; f->nmoves++;
        } else if (g == 81) {
            double x = rdd(buf + 0x20), y = rdd(buf + 0x28), z = rdd(buf + 0x30), rp = rdd(buf + 0x68);
            int r1 = newpoly(f, K_RAPID, op, tool, 0, 0);
            V3 a = { x, y, cur.z }, b = { x, y, rp }, c = { x, y, z };
            polypt(f, r1, cur); polypt(f, r1, a); polypt(f, r1, b);
            int p = newpoly(f, K_CUT, op, tool, 0, 0);
            polypt(f, p, b); polypt(f, p, c); polypt(f, p, b);
            cur = b; line = -1; f->ndrill++;
        }
    }
}

static int cmp_seq(const void *a, const void *b) {
    const Op *x = a, *y = b;
    return x->seq != y->seq ? (x->seq < y->seq ? -1 : 1) : (x->id > y->id) - (x->id < y->id);   /* no int overflow */
}

/* reorder toolpath polylines into Operations Manager order, grouped per op (geometry first) */
static void group_by_op(McamFile *f) {
    if (f->nops > 1) qsort(f->ops, f->nops, sizeof(Op), cmp_seq);
    Poly *np = xmalloc((f->npolys ? f->npolys : 1) * sizeof(Poly));
    int k = 0;
    for (int i = 0; i < f->npolys; i++) if (f->polys[i].kind == K_GEOM) np[k++] = f->polys[i];
    for (int o = 0; o < f->nops; o++) {
        Op *op = &f->ops[o];
        op->first_poly = k; op->npolys = 0; op->npts = 0;
        for (int i = 0; i < f->npolys; i++)
            if (f->polys[i].kind != K_GEOM && f->polys[i].op == op->id) {
                np[k++] = f->polys[i]; op->npolys++; op->npts += f->polys[i].count;
            }
    }
    free(f->polys); f->polys = np; f->npolys = k; f->cappolys = k;
}

/* a string field copied as stored (up to its NUL or MAX bytes; ours: kept raw, not cstr's '?' for control characters, so
   a multi-line comment keeps its line ends) */
static void rawstr(char *dst, size_t n, const uint8_t *src, size_t max) {
    size_t i = 0; for (; i + 1 < n && i < max && src[i]; i++) dst[i] = (char)src[i];
    dst[i] = 0;
}
/* the active group's machine -> its control -> the control's pages (mcam_load.h "The active machine group's machine and
   control"): the machine record whose +0x34 is the group's +0x1170, the control (sub 23) whose +0x34 is the machine's
   +0xe94, the pages whose +0x34 = control + 1 + page with +0x68 = page. No group / no match: the first machine record and
   the first control page 9 (ours: the loader's earlier rule, e.g. a part with no machine group table) */
static void resolve_machine(McamFile *f) {
    const uint8_t *m = NULL; uint32_t mn = 0;
    for (int i = 0; i < nmcr && !m; i++) if (mcr[i].sub == 22 && f->grp_rec && f->grp_mach && (int32_t)rd32(mcr[i].r + 0x34) == f->grp_mach) { m = mcr[i].r; mn = mcr[i].n; }
    if (!m) for (int i = 0; i < nmcr && !m; i++) if (mcr[i].sub == 22) { m = mcr[i].r; mn = mcr[i].n; }
    if (m && mn >= 0xe98) {
        cstr(f->matlib, sizeof f->matlib, m + 0xce7, 0xff);   /* machine definition: material library */
        cstr(f->toollib[0], sizeof f->toollib[0], m + 0xab6, 0x104); cstr(f->toollib[1], sizeof f->toollib[1], m + 0xbbb, 0x104);
        f->mach_type = (int32_t)rd32(m + 0x68); f->mach_ok = 1;
        f->mach_id = (int32_t)rd32(m + 0x34);
        cstr(f->mach_comment, sizeof f->mach_comment, m + 0xdc5, 0xcf);
        cstr(f->mach_ctrl, sizeof f->mach_ctrl, m + 0x8ac, 0x104); cstr(f->mach_post, sizeof f->mach_post, m + 0x9b1, 0x104);
        memcpy(&f->mach_minarc[0], m + 0xf8, 4); memcpy(&f->mach_minarc[1], m + 0xfc, 4);
        f->ctl_id = (int32_t)rd32(m + 0xe94);
        /* batch 11: the coolant definition (mcam_load.h mach_cool_*) */
        for (int k = 0; k < 10; k++) {
            cstr(f->mach_cool_base[k], sizeof f->mach_cool_base[k], m + 0x228 + k * 0x50, 0x50);
            cstr(f->mach_cool_on[k], sizeof f->mach_cool_on[k], m + 0x228 + 0x320 + k * 0x28, 0x28);
            cstr(f->mach_cool_off[k], sizeof f->mach_cool_off[k], m + 0x228 + 0x4b0 + k * 0x28, 0x28);
        }
        cstr(f->mach_cool_btn, sizeof f->mach_cool_btn, m + 0x228 + 0x640, 0x28);
        f->mach_cool_frompost = m[0x228 + 0x669] != 0; f->mach_cool_ok = 1;
    }
    const uint8_t *pg[12] = { 0 }; uint32_t pgn[12] = { 0 };
    int have_ctl = 0, ci = -1;
    if (f->ctl_id) for (int i = 0; i < nmcr && ci < 0; i++) if (mcr[i].sub == 23 && (int32_t)rd32(mcr[i].r + 0x34) == f->ctl_id) { have_ctl = 1; ci = i; }
    if (have_ctl) {
        for (int i = 0; i < nmcr; i++) {
            if (mcr[i].sub != 24) continue;
            int p = (int32_t)rd32(mcr[i].r + 0x68), id = (int32_t)rd32(mcr[i].r + 0x34);
            if (p >= 0 && p < 12 && id == f->ctl_id + 1 + p && !pg[p]) { pg[p] = mcr[i].r; pgn[p] = mcr[i].n; }
        }
        /* a page the id rule does not find: the page records that follow the control record in the file, up to the next
           machine / control record, by their page number +0x68 (ours: the order every part shows; the link field NOT DECODED) */
        for (int i = ci + 1; i < nmcr && mcr[i].sub == 24; i++) {
            int p = (int32_t)rd32(mcr[i].r + 0x68);
            if (p >= 0 && p < 12 && !pg[p]) { pg[p] = mcr[i].r; pgn[p] = mcr[i].n; f->ctl_pgfb |= 1 << p; }
        }
    }
    if (pg[9] && pgn[9] >= 0x29e + 0xd) {                    /* control page 9 (NC path): the mill tool registers */
        f->ctl_found = 1; f->ctl_addreg = rd16(pg[9] + 0x29e); f->ctl_lofs = (int16_t)rd16(pg[9] + 0x29e + 4); f->ctl_dofs = (int16_t)rd16(pg[9] + 0x29e + 6); f->ctl_head = pg[9][0x29e + 0xc];
    } else if (!have_ctl)
        for (int i = 0; i < nmcr && !f->ctl_found; i++)
            if (mcr[i].sub == 24 && mcr[i].n >= 0x29e + 0xd && (int32_t)rd32(mcr[i].r + 0x68) == 9) {
                const uint8_t *r = mcr[i].r;
                f->ctl_found = 1; f->ctl_addreg = rd16(r + 0x29e); f->ctl_lofs = (int16_t)rd16(r + 0x29e + 4); f->ctl_dofs = (int16_t)rd16(r + 0x29e + 6); f->ctl_head = r[0x29e + 0xc];
            }
    if (pg[2] && pg[8] && pg[9] && pgn[2] >= 0x7b0 + 0x104 && pgn[8] >= 0x1ae && pgn[9] >= 0xa36) {
        f->ctl_ok = 1;
        cstr(f->ctl_ncpath, sizeof f->ctl_ncpath, pg[2] + 0x94, 0x104);
        if (pgn[2] >= 0xcc4 + 0x104) cstr(f->ctl_ncsuffix, sizeof f->ctl_ncsuffix, pg[2] + 0xcc4, 0x104);   /* file_ctrl +0xc36 (NCSuffix) */
        if (f->major == 0x13 && pgn[2] >= 0xdc9 + 15) { f->ctl_post_ok = 1; memcpy(f->ctl_post, pg[2] + 0xdc9, 15); }   /* post_dlg_settings (mc-test01.mcam) */
        cstr(f->ctl_oplib[0], sizeof f->ctl_oplib[0], pg[2] + 0x4a4, 0x104); cstr(f->ctl_oplib[1], sizeof f->ctl_oplib[1], pg[2] + 0x5a8, 0x104);
        cstr(f->ctl_opdef[0], sizeof f->ctl_opdef[0], pg[2] + 0x6ac, 0x104); cstr(f->ctl_opdef[1], sizeof f->ctl_opdef[1], pg[2] + 0x7b0, 0x104);
        for (int k = 0; k < 3; k++) f->ctl_seq[k] = rdd(pg[8] + 0x80 + 8 * k);
        f->ctl_ncout[0] = pg[8][0x1a9]; f->ctl_ncout[1] = pg[8][0x1aa]; f->ctl_ncout[2] = pg[8][0x1ab]; f->ctl_ncout[3] = pg[8][0x1ad];
        f->ctl_arcfeed = pg[9][0xa35];
    }
    free(mcr); mcr = NULL; nmcr = cmcr = 0;
}

static char load_err[512];
const char *mcam_error(void) { return load_err; }
static int load_code;
int mcam_error_code(void) { return load_code; }

/* a solid's ops block (McOpsFile, formats/solid_history.md 2.1 / 2.3; mcgeomsld FUN_18026b3c0 header, McOperation::Read
   FUN_1802649f0 base; tools/sld_hist.py): u8 needs-regen, u8 expanded, u32 solid id, u32 history position, u64 SOLID_ID eptr,
   u32 op count, then per op u32 type, u32 size, size bytes. The base of each op: i32 n1 + n1 x 38-byte McEptr records,
   i32 n2 + n2 x 38, i32 n3 + n3 x u64, u16 len + the name, u8 op+9 / +0xa / +0xb / +0xc, u32 op+0x10, u64 SOLID_ID eptr,
   u32 op id, u32 op type, u32 op version, u8, u8. The 38-byte McEptr record is the layout of ops version >= 0x18 (the
   SOLIDXMT +0x49 value, 0x1a in the user's X9 parts, 0x1c in 2017 files); older files keep only the raw bytes. */
static void solid_history(Solid *s, const uint8_t *b, size_t n, int ver) {
    s->ops_raw = xmalloc(n ? n : 1); memcpy(s->ops_raw, b, n); s->ops_n = n;
    if (n < 22 || ver < 0x18) return;
    s->regen = b[0]; s->expanded = b[1]; s->pos = (int32_t)rd32(b + 6);
    uint32_t cnt = rd32(b + 18);
    size_t o = 22;
    SolidOp *ops = xmalloc((cnt ? cnt : 1) * sizeof *ops); memset(ops, 0, (cnt ? cnt : 1) * sizeof *ops);
    for (uint32_t i = 0; i < cnt; i++) {
        if (o + 8 > n) { free(ops); return; }
        SolidOp *p = &ops[i]; p->type = (int32_t)rd32(b + o); p->size = rd32(b + o + 4); p->off = o + 8;
        if (p->size > n - p->off) { free(ops); return; }
        const uint8_t *q = b + p->off, *e = q + p->size;
        for (int k = 0; k < 2; k++) { if (q + 4 > e) { free(ops); return; } int32_t m = (int32_t)rd32(q); q += 4; if (m < 0 || (size_t)m * 38 > (size_t)(e - q)) { free(ops); return; } q += (size_t)m * 38; }
        { if (q + 4 > e) { free(ops); return; } int32_t m = (int32_t)rd32(q); q += 4; if (m < 0 || (size_t)m * 8 > (size_t)(e - q)) { free(ops); return; } q += (size_t)m * 8; }
        if (q + 2 > e) { free(ops); return; }
        size_t L = rd16(q); q += 2; if (L > (size_t)(e - q)) { free(ops); return; }
        cstr(p->name, sizeof p->name, q, L); q += L;
        if (q + 4 + 4 + 8 + 4 + 4 > e) { free(ops); return; }
        p->f9 = q[0]; p->f10 = q[1]; p->f11 = q[2]; p->f12 = q[3]; q += 4;
        p->x10 = rd32(q); q += 4 + (ver < 0x19 ? 4 : 8);          /* op+0x18 SOLID_ID eptr: u32 below ops version 0x19 */
        p->id = (int32_t)rd32(q); q += 8;                            /* op+0x34 id, op+0x30 type */
        q += 4 + 2;                                                  /* op+0x3c version (ver > 8), op+0x40 / +0x41 (ver > 0xf) */
        if (q > e) { free(ops); return; }
        p->base_n = (size_t)(q - (b + p->off));
        o = p->off + p->size;
    }
    s->ops = ops; s->nops = (int)cnt; s->hist_ok = 1;
}

/* notes (E_NOTE; formats/drafting/notes.md): the N_ID / W_ID records and the memory table while the file buffer is read (pointers
   into it), copied into F.notes after the last record */
typedef struct { int32_t id; const uint8_t *p; uint32_t n; } LdMt;
static LdMt *ld_mt; static int nld_mt;
static const uint8_t **ld_note; static int nld_note;
static const LdMt *ld_mt_find(int32_t id) { for (int i = 0; id && i < nld_mt; i++) if (ld_mt[i].id == id) return &ld_mt[i]; return NULL; }
static void note_add_mt(Note *nt, const LdMt *m) {
    for (int i = 0; i < nt->nmt; i++) if (nt->mt[i].id == m->id) return;
    nt->mt = xrealloc(nt->mt, sizeof *nt->mt * (size_t)(nt->nmt + 1));
    NoteMt *q = &nt->mt[nt->nmt++]; q->id = m->id; q->n = m->n; q->p = xmalloc(m->n ? m->n : 1); memcpy(q->p, m->p, m->n);
}
/* the entries a note references = the ones mTableDeleteNote @1802fd620 frees (decomp/draft/core_draft_rec2.c 2310-2370): the text
   (+0xf8) when the kind (+0x68) is > 1 - and only it when > 2 -, then the leader chain from +0x100 while its on byte is set: the
   aPoint array (wl +0x20) when wl +0x19, the next wl (mTableFindData(wl +0x84)) */
static void note_collect(Note *nt) {
    const uint8_t *E = nt->E;
    if (E[0x68] > 1) { const LdMt *m = ld_mt_find((int32_t)rd32(E + 0xf8)); if (m) note_add_mt(nt, m); if (E[0x68] > 2) return; }
    const uint8_t *wl = E + 0x100; uint32_t avail = 0x98;
    for (int guard = 0; wl && avail >= 0x88 && wl[0] && guard < 1000; guard++) {
        if (wl[0x19]) { const LdMt *m = ld_mt_find((int32_t)rd32(wl + 0x20)); if (m) note_add_mt(nt, m); }
        const LdMt *nx = ld_mt_find((int32_t)rd32(wl + 0x84));
        if (!nx) break;
        note_add_mt(nt, nx); wl = nx->p; avail = nx->n;
    }
}
/* ---- the level table (mcam_load.h) */
struct McLevel *mcam_level(McamFile *f, int level) { for (int i = 0; i < f->nlvls; i++) if (f->lvls[i].level == level) return &f->lvls[i]; return NULL; }
static struct McLevel *lvl_append(McamFile *f, int level, int vis) {   /* a LevelNode (ctor: name " ", visible 1) at the list end */
    f->lvls = xrealloc(f->lvls, sizeof *f->lvls * (size_t)(f->nlvls + 1)); struct McLevel *l = &f->lvls[f->nlvls++];
    memset(l, 0, sizeof *l); l->level = level; l->name[0] = ' '; l->vis = (uint8_t)vis; return l;
}
void mcam_level_set(McamFile *f, int level, int vis) {   /* LevelData::setLevel(level, vis, 1) @180246ba0 */
    if (level <= 0) return;
    struct McLevel *l = mcam_level(f, level);
    if (l) l->vis = (uint8_t)vis; else lvl_append(f, level, vis);
}
void mcam_level_setname(McamFile *f, int level, const char *name) {   /* LevelData::setName @180246f10 */
    if (level <= 0) return;
    struct McLevel *l = mcam_level(f, level); if (!l) l = lvl_append(f, level, 1);
    snprintf(l->name, sizeof l->name, "%s", name);
}
void mcam_set_levname(McamFile *f, int level, const char *name) {   /* set_levname @1803cfc10 */
    char b[0x134]; snprintf(b, 301, "%s", name);
    char *s = b; while (*s == ' ' || *s == '\t') s++;
    size_t k = strlen(s); while (k && (s[k - 1] == ' ' || s[k - 1] == '\t')) s[--k] = 0;
    mcam_level_setname(f, level, s); f->lvl_dirty = 1;
}
static void lvl_store(McamFile *f, int level) {   /* FUN_1802c4010 L123-131 outside a load: a missing level is made visible */
    if (level > 0 && !mcam_level(f, level)) { lvl_append(f, level, 1); f->lvl_dirty = 1; }
}
void mcam_level_store(McamFile *f, int level) { lvl_store(f, level); }
void mcam_ent_blank(McamFile *f, int e, int blank) {
    if (e < 0 || e >= f->nents) return;
    Ent *en = &f->ents[e]; en->blanked = blank != 0;
    for (int k = 0; k < en->npoly; k++) if (en->first_poly + k < f->npolys) f->polys[en->first_poly + k].blanked = (uint8_t)(blank != 0);
}
int mcam_level_visible(const McamFile *f, int level) {   /* LevelData::isVisible @180245fc0 (mcam_load.h) */
    for (int i = 0; i < f->nlvls; i++) if (f->lvls[i].level == level) return f->lvls[i].vis != 0;
    return 0;
}
int mcam_level_named(const struct McLevel *l) { return strcmp(l->name, " ") && l->name[0]; }   /* listNamed / numNamed @180246380 */
/* FUN_1802d5220 (decomp/levels/core_levels.c 1-105), run on the level record while the file is read. The state it starts from: a
   File > Open that is no merge runs dbinit first (mccore FUN_1803509a0, the native open of DoFileOpen: `if (!merge) dbinit()`
   before get_db; dbinit @1802c4770 -> LevelData::resetLevels @180246980: the list emptied, main_level = 1, mask_level = 0,
   setLevel(1, visible) - decomp/mastercam_filenew.c 437-461). So the current main level is 1 and:
   - the record's main level (+8) is not 1 -> level 1 hidden (L18-20: `if (rec main != main_level) setLevel(main_level, 0)`);
   - GetOnlyMainVisible (mccore DAT_1813c43fc: zero-initialised .bss, set only by the Level Manager's "only main visible" check box
     handlers, decomp/level_picker/d2.txt 135 / 485) is off at a load, so the record's visible levels are made visible (L47-61);
   - the named ones are made (hidden when new) and named (L64-80); set_main_level(rec main) (L83);
   - the record's main level hidden unless it is in the visible list, and then main_level_vis = 0 (L84-93). */
int mc_main_level_vis = 1;                  /* mccore main_level_vis (export ?main_level_vis@@3DA, .data initial value 1) */
static void lvl_record(McamFile *f, const uint8_t *rec, uint32_t n) {
    int32_t main = (int32_t)rd32(rec + 8), nv = (int32_t)rd32(rec + 0x7c), nn = (int32_t)rd32(rec + 0x80);
    if (nv < 0 || nn < 0 || 0x98 + (uint64_t)nv * 4 + (uint64_t)nn * 0x134 > n) return;
    if (main != 1) mcam_level_set(f, 1, 0);       /* main_level = 1 after dbinit */
    const uint8_t *t = rec + 0x98;
    for (int i = 0; i < nv; i++) mcam_level_set(f, (int32_t)rd32(t + 4 * i), 1);
    const uint8_t *nm = t + 4 * nv;
    for (int i = 0; i < nn; i++) {
        int32_t lv = (int32_t)rd32(nm + (size_t)i * 0x134);
        if (!mcam_level(f, lv)) mcam_level_set(f, lv, 0);
        char b[0x131]; memcpy(b, nm + (size_t)i * 0x134 + 4, 0x130); b[0x130] = 0;
        mcam_level_setname(f, lv, b);
    }
    f->main_level = main;                         /* set_main_level(rec main) */
    int in = 0; for (int i = 0; i < nv; i++) if ((int32_t)rd32(t + 4 * i) == main) in = 1;
    if (!in) { mcam_level_set(f, main, 0); mc_main_level_vis = 0; }
}

static int mcam_load_(McamFile *f, const char *path);
int mcam_load(McamFile *f, const char *path) { g_in_load = 1; load_code = MCAM_ERR_NONE; int r = mcam_load_(f, path); g_in_load = 0;
    if (!r && load_code == MCAM_ERR_NONE) load_code = MCAM_ERR_OTHER;
    return r; }
static int mcam_load_(McamFile *f, const char *path) {
    memset(f, 0, sizeof *f); f->metric = f->nci_metric = -1; f->main_level = 1;   /* dbinit: main_level = 1 */
    load_err[0] = 0;
    snprintf(f->path, sizeof f->path, "%s", path);
    FILE *fp = fopen(path, "rb");
    if (!fp) { snprintf(load_err, sizeof load_err, "cannot open %s: %s", path, strerror(errno)); return 0; }
    long flen = -1;
    if (fseek(fp, 0, SEEK_END) == 0) flen = ftell(fp);
    if (flen < 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); snprintf(load_err, sizeof load_err, "%s: not a regular file", path); return 0; }
    size_t len = (size_t)flen;
    { struct stat st; if (stat(path, &st) == 0) { f->file_size = (long long)st.st_size; f->file_mtime = (long long)st.st_mtime; } }
    if (len < 0x9c) { fclose(fp); snprintf(load_err, sizeof load_err, "%s: too small to be a part file (%zu bytes)", path, len); return 0; }
    uint8_t *d = xmalloc(len);
    if (!d) { fclose(fp); snprintf(load_err, sizeof load_err, "%s: out of memory (%zu bytes)", path, len); return 0; }
    if (fread(d, 1, len, fp) != len) { fclose(fp); free(d); snprintf(load_err, sizeof load_err, "%s: read error", path); return 0; }
    fclose(fp);
    /* header entity (formats/mcam.md): u32 0x98, class 0x200, subtype 8 = file header, version major/minor */
    uint32_t hl = rd32(d);
    if (hl == 0x88 && rd32(d + 4) == 0x200) {
        free(d); snprintf(load_err, sizeof load_err, "%s: older X-version part file (header 0x88) - this format is not supported yet; "
                          "re-save it as an X9 / 2017 part first", path); return 0;
    }
    if (hl != 0x98 || rd32(d + 4) != 0x200 || rd16(d + 4 + 0x68) != 8) {
        free(d); snprintf(load_err, sizeof load_err, "%s: not an X9 / 2017 part file (.mcx-9 / .mcam)", path); return 0;
    }
    if (mcam_part_newer(d, len)) {                           /* a newer release's part: refused as DoFileOpen does (mcam_grp.h), nothing read */
        free(d); snprintf(load_err, sizeof load_err, "%s", MCAM_NEWER_MSG); load_code = MCAM_ERR_NEWER; return 0;
    }
    /* part units: header +0x6f, 1 metric / 0 inch; mccore FUN_1802ae550 (decomp/mccore_dbread.c) switches the
       configuration to it (ChangeConfiguration) when it is < 2 and differs from the current one */
    int hdr_units = d[4 + 0x6f] < 2 ? d[4 + 0x6f] : -1;
    load_units = hdr_units;
    f->major = d[4 + 0x6a];                                   /* header +0x6a: 0x12 X9 (.mcx-9), 0x13 2017 (.mcam) */
    size_t off = 0;
    ntlmgr = 0; nmcr = 0; nld_mt = 0; nld_note = 0;
    while (off + 4 <= len) {
        uint32_t n = rd32(d + off);
        if (n == 0 || off + 4 + n > len) { f->nci_off = off; nci(f, d, len, off); break; }
        const uint8_t *rec = d + off + 4;
        entity(f, rec, n);
        if (n >= 0x98 && rd32(rec) == 0x200 && rd16(rec + 0x68) == 0x15) lvl_record(f, rec, n);
        if (n == 0x1c8 && (rd32(rec) == 0x10 || rd32(rec) == 0x20)) {   /* N_ID / W_ID: v8_note, fixed 0x160 body (records.md 0) */
            ld_note = xrealloc(ld_note, sizeof *ld_note * (size_t)(nld_note + 1)); ld_note[nld_note++] = rec; }
        off += 4 + n;
        if (n >= 0x6a && rd32(rec) == 0x200) {
            uint16_t sub = rd16(rec + 0x68);
            if (sub == 0xF && off + 4 <= len) {                                            /* toolpath groups */
                int ng = 0; size_t gl = mcam_grp_block(d + off, len - off, NULL, 0, &ng);   /* the records and their member entries (mcam_grp.h) */
                const uint8_t *gr[256];                                                    /* (the first 256 groups) */
                if (gl && ng > 0) { mcam_grp_block(d + off, len - off, gr, 256, &ng); if (ng > 256) ng = 256; } else ng = 0;
                if (ng) {
                    /* the active machine group (OMgetActiveMachineGroup -> RetrieveActiveGroup = the group holding the insert
                       arrow): on load COpMgrTree::LoadTree (mastercam.dll, decomp/opman_tree_all.c) puts the arrow after the
                       last op / group item of the tree, i.e. in the LAST machine group (parent id +0x10c = 0) of the list */
                    const uint8_t *g = gr[0];
                    for (int k = 1; k < ng; k++) if (rd32(gr[k] + 0x10c) == 0) g = gr[k];
                    for (int k = 0; k < 3; k++) f->stock[k] = rdd(g + 0x1230 + 8 * k);
                    for (int k = 0; k < 3; k++) f->stock_org[k] = rdd(g + 0x1248 + 8 * k);
                    f->stock_sel = rd16(g + 0x1690);                                /* origin corner selector */
                    if (!f->stock_rec) {                    /* group_pg3 (op_group +0x1228; mastercam.dll CGroupPg3 FUN_1801f2000 reads them) */
                        f->stock_rec = 1; f->stock_grp = (int32_t)rd32(g);
                        f->stock_fit = g[0x1228]; f->stock_disp = g[0x1260]; f->stock_shaded = g[0x1261];
                        f->stock_shape = (int32_t)rd32(g + 0x1264); f->stock_solid = (int32_t)rd32(g + 0x1268); f->stock_axis = (int32_t)rd32(g + 0x126c);
                        f->stock_center = g[0x1270]; f->stock_mtree = g[0x16a8]; f->stock_gtype = g[0x19e8];
                        cstr(f->stock_file, sizeof f->stock_file, g + 0x1271, 0x104);
                        f->stock_vid = (int16_t)rd16(g + 0x8a8); f->stock_vno = (int16_t)rd16(g + 0x8aa);
                        for (int k = 0; k < 9; k++) f->stock_vm[k] = rdd(g + 0x8b0 + 8 * k);
                        for (int k = 0; k < 3; k++) f->stock_vo[k] = rdd(g + 0x8f8 + 8 * k);
                        for (int l = 0; l < 12; l++) for (int k = 0; k < 6; k++) f->stock_lines[l][k] = rdd(g + 0x1380 + 0x30 * l + 8 * k);
                    }
                    if (!f->nlgrp) {                        /* every group's lathe component ids (mcam_load.h lgrp) */
                        f->lgrp = xcalloc((size_t)ng, sizeof *f->lgrp);
                        f->lgrp_mach = xcalloc((size_t)ng, sizeof *f->lgrp_mach);
                        for (int k = 0; k < ng; k++) {
                            struct McGrpLathe *q = &f->lgrp[f->nlgrp++];
                            q->id = (int32_t)rd32(gr[k]); q->parent = (int32_t)rd32(gr[k] + 0x10c); q->gtype = gr[k][0x19e8];
                            for (int c = 0; c < 6; c++) q->comp[c] = (int32_t)rd32(gr[k] + 0x16ac + 8 * c);
                            f->lgrp_mach[f->nlgrp - 1] = (int32_t)rd32(gr[k] + 0x1170);
                            memcpy(q->g1698, gr[k] + 0x1698, 8); memcpy(q->g16a0, gr[k] + 0x16a0, 8); q->g11c0 = (int32_t)rd32(gr[k] + 0x11c0); q->g119c = (int32_t)rd32(gr[k] + 0x119c); q->g11a0 = rdd(gr[k] + 0x11a0); q->g11b0 = rdd(gr[k] + 0x11b0);   /* reset_lathe_op_prm_defaults 0x1801d3170: op +0xb70 / +0xb78 */
                        }
                    }
                    if (!f->grp_rec) {                      /* Files / Tool Settings (formats/grpprops_tabs.md 2.1 / 3.1; mcam_load.h) */
                        f->grp_rec = 1; f->grp_id = (int32_t)rd32(g); f->grp_mach = (int32_t)rd32(g + 0x1170);
                        rawstr(f->grp_name, sizeof f->grp_name, g + 4, 0x104); rawstr(f->grp_mdef, sizeof f->grp_mdef, g + 0x355, 0x104);
                        rawstr(f->grp_comment, sizeof f->grp_comment, g + 0x980, 1999);
                        f->grp_prog = (int32_t)rd32(g + 0x1178);
                        for (int k = 0; k < 4; k++) { f->grp_ovr[k] = (int32_t)rd32(g + 0x115c + 4 * k); f->grp_tcfg[k] = (int32_t)rd32(g + 0x11b8 + 4 * k); }
                        f->grp_names = xcalloc((size_t)ng, sizeof *f->grp_names);
                        for (int k = 0; k < ng; k++) rawstr(f->grp_names[f->ngrp_names++], 0x105, gr[k] + 4, 0x104);
                    }
                    if (!f->have_grp) {                                                    /* feeds_speeds.md */
                        f->have_grp = 1; f->grp_fs_mode = (int32_t)rd32(g + 0x119c); cstr(f->grp_material, sizeof f->grp_material, g + 0x11d0, 0x51);
                        f->grp_plunge = rdd(g + 0x1180); f->grp_retract = rdd(g + 0x1188); f->grp_feed = rdd(g + 0x11a0); f->grp_rpm = rdd(g + 0x11b0);
                    }
                }
                off = gl ? off + gl : len + 1;                                             /* (a block that does not fit ends the read) */
            }
            if (off > len) break;
            else if (sub == 0xE) {                                                         /* material table */
                while (off + 0x28 <= len) {
                    int32_t id = (int32_t)rd32(d + off); uint64_t s = rd64(d + off + 0x10);
                    off += 0x28;
                    if (id == 0) break;
                    if (s > len - off) { off = len; break; }
                    ld_mt = xrealloc(ld_mt, sizeof *ld_mt * (size_t)(nld_mt + 1)); ld_mt[nld_mt++] = (LdMt){ id, d + off, (uint32_t)s };
                    if (id >= f->mt_next) f->mt_next = id + 1;
                    off += s;
                }
            }
        } else if (n >= 0x16c && rd32(rec) == 0x20000) {                                  /* Parasolid solid */
            uint8_t ver = rec[0x49];
            uint32_t xs = rd32(rec + 0x168);
            Solid *sv = NULL;
            if (off + xs <= len) {
                f->solids = xrealloc(f->solids, (f->nsolids + 1) * sizeof(Solid));
                Solid *s = sv = &f->solids[f->nsolids++]; memset(s, 0, sizeof *s);
                cstr(s->name, sizeof s->name, rec + 0x68, 0x100);
                s->ver = ver; s->xt_size = xs; s->xt = xmalloc(xs); memcpy(s->xt, d + off, xs);
            }
            if (xs > len - off) break;
            off += xs;
            size_t nl = ver > 0x1b ? 0x100 : 0x40;
            if (sv && off + 4 + nl <= len) { sv->sid = rd32(d + off); cstr(sv->desc, sizeof sv->desc, d + off + 4, nl); }
            off += 4 + nl;
            size_t hs = off;
            off += 1 + 1 + 4 + 4 + 8;
            if (off + 4 > len) break;
            uint32_t cnt = rd32(d + off); off += 4;
            for (uint32_t i = 0; i < cnt && off + 8 <= len; i++) { size_t sk = 8 + (size_t)rd32(d + off + 4); if (sk > len - off) { off = len; break; } off += sk; }
            if (sv) solid_history(sv, d + hs, off - hs, ver);
        }
    }
    for (int i = 0; i < npend_arcs; i++) {                 /* view arcs (arc_finish comment): centre to world */
        const uint8_t *rec = pend_arcs[i].rec;
        V3 s = rdv(rec + 0x68), e = rdv(rec + 0x80), c = rdv(rec + 0x98);
        double r = rdd(rec + 0xb0), a0 = rdd(rec + 0xb8), sw = rdd(rec + 0xc0);
        int view = rd16(rec + 0xc8);
        const double *m = NULL; double sm[9]; int vw = 0;
        for (int k = 0; k < nviews; k++) if (views[k].n == view) { m = views[k].m; break; }
        if (!m && mcam_std_view(view, sm)) m = sm;   /* a reserved view (not stored in the file) */
        if (m) {
            V3 w = { c.x * m[0] + c.y * m[3] + c.z * m[6], c.x * m[1] + c.y * m[4] + c.z * m[7], c.x * m[2] + c.y * m[5] + c.z * m[8] };
            c = w;
            /* view normal (row 3) along +-Z: the arc lies in XY; a view angle t maps to the world direction
               cos t * row1 + sin t * row2, i.e. world angle = atan2(row1) + t (normal +Z) or atan2(row1) - t (-Z) */
            if (fabs(fabs(m[8]) - 1) < 1e-9) {
                double b = atan2(m[1], m[0]);
                if (m[8] > 0) a0 = b + a0; else { a0 = b - a0; sw = -sw; }
                a0 = fmod(a0, 2 * M_PI); if (a0 < 0) a0 += 2 * M_PI;
            } else vw = view;            /* another plane: kept in its view (Ent.vw / vm) */
        }   /* a view number with no matrix (neither stored nor reserved): the record is used as stored */
        cur_rec = rec;
        arc_finish(f, pend_arcs[i].ent, rec[6], (int32_t)rd32(rec + 8), s, e, c, r, a0, sw, vw, m);
        f->ents[pend_arcs[i].ent].avw = view;
    }
    cur_rec = NULL;
    for (int i = 0; i < f->nents; i++) if (f->ents[i].type == E_SCRV) cons_poly(f, i);   /* surface curves: their surface and uv curve read by now */
    f->fviews = xrealloc(f->fviews, (nviews + 1) * sizeof *f->fviews); f->nfviews = nviews;
    for (int k = 0; k < nviews; k++) { f->fviews[k].n = views[k].n; memcpy(f->fviews[k].m, views[k].m, sizeof views[k].m); f->fviews[k].isnew = 0; }
    if (f->view_rec) {   /* store_db_ent 0x1802c5dd0: +0x6c == -1 = the mouse view (its matrix: the DB_ID sub 0 record numbered 65535 = -1, store_db_ent's first branch);
                            else GetNewViewNo(+0x6c): view number n = the reserved view 1..8 (CMastercamApp::InitializeStandardViews) or the part's sub 0 record n */
        double sm[9]; int gv = f->view_gv;
        if (gv >= 1 && gv <= 8 && mcam_std_view(gv, sm)) { memcpy(f->view_mat, sm, sizeof sm); f->view_mat_ok = 1; }
        else for (int k = 0; k < nviews; k++) if (views[k].n == (gv == -1 ? 65535 : gv)) { memcpy(f->view_mat, views[k].m, sizeof views[k].m); f->view_mat_ok = 1; break; }
    }
    free(pend_arcs); pend_arcs = NULL; npend_arcs = 0; free(views); views = NULL; nviews = 0;
    for (int i = 0; i < f->nents; i++) if (f->ents[i].blanked) {   /* blanked: kept, not drawn / picked; a part's blanked entity also has its
                                                                    polylines emptied as before (no command here unblanks it; the program's
                                                                    other polyline walks - e.g. the rotation box - then stay as they were) */
        mcam_ent_blank(f, i, 1);
        for (int k = 0; k < f->ents[i].npoly; k++) if (f->ents[i].first_poly + k < f->npolys) f->polys[f->ents[i].first_poly + k].count = 0;
    }
    for (int i = 0; i < ntlmgr; i++) part_tools_apply(f->tools, f->ntools, tl_body, tlmgr_rec[i], tlmgr_n[i]);
    if (!f->ntools && ntlmgr) {                  /* a 2017 part keeps its tools only in the TlMgr (no TL_ID records) */
        Tool *t; int k = part_tools_list(tlmgr_rec[0], tlmgr_n[0], &t);
        for (int i = 0; i < k && f->ntools < 256; i++) f->tools[f->ntools++] = t[i];
        for (int i = 256; i < k; i++) free(t[i].holder_rz);
        if (k >= 0) free(t);
    }
    if (hdr_units >= 0) f->metric = hdr_units;
    free(tlmgr_rec); free(tlmgr_n); tlmgr_rec = NULL; tlmgr_n = NULL; ntlmgr = 0;
    resolve_machine(f);                                    /* before the buffer goes: mcr points into it */
    if (nld_note) {                                        /* notes: the records and their memory-table entries copied */
        f->notes = xcalloc((size_t)nld_note, sizeof *f->notes);
        for (int i = 0; i < nld_note; i++) { Note *nt = &f->notes[f->nnotes++]; memcpy(nt->E, ld_note[i], 0x1c8); nt->ent = -1; note_collect(nt); }
    }
    if (f->mt_next < 1) f->mt_next = 1;
    free(ld_mt); ld_mt = NULL; nld_mt = 0; free(ld_note); ld_note = NULL; nld_note = 0;
    free(d);
    for (int i = 0; i < f->nsolids; i++)
        for (int k = 0; k < f->nsld_ids; k++) {
            char nm[32]; snprintf(nm, sizeof nm, "sld_%d", f->sld_ids[k][1]);
            if (!strcmp(f->solids[i].name, nm)) { f->solids[i].idn = f->sld_ids[k][0]; f->solids[i].level = f->sld_ids[k][2]; f->solids[i].color = f->sld_ids[k][3]; }
        }
    group_by_op(f);
    /* the surfaces as entities (E_SURF), after every wireframe entity: the wireframe keeps the indices it had before surfaces
       were entities (project files reference entities by index) */
    for (int i = 0; i < f->nsurfs; i++) {
        cur_rec = f->surfs[i].rec;
        Ent *en = add_ent(f, E_SURF, f->surfs[i].level, f->surfs[i].color);
        en->srf = i; en->planar = 0; en->npoly = 0; en->ln_style = 1; f->surfs[i].ent = f->nents - 1;
    }
    /* the notes as entities (E_NOTE), after the surfaces (the earlier entities keep their indices) */
    for (int i = 0; i < f->nnotes; i++) {
        cur_rec = f->notes[i].E;
        Ent *en = add_ent(f, E_NOTE, (int32_t)rd32(cur_rec + 8), cur_rec[6]);
        en->nt = i; en->planar = 0; en->npoly = 0; en->ln_style = (int16_t)rd16(cur_rec + 0x18); en->pt_style = (int16_t)rd16(cur_rec + 0x1c);   /* the two style
                                                                      fields as stored (Home > Point style compares +0x1c, cui_attr.c) */
        memcpy(&en->width, cur_rec + 0x10, 8);
        f->notes[i].ent = f->nents - 1;
    }
    /* the meshes as entities (E_MESH), after the notes (the earlier entities keep their indices) */
    for (int i = 0; i < f->nmeshes; i++) {
        cur_rec = f->meshes[i].rec;
        Ent *en = add_ent(f, E_MESH, f->meshes[i].level, f->meshes[i].color);
        en->msh = i; en->planar = 0; en->npoly = 0; en->ln_style = 1; f->meshes[i].ent = f->nents - 1;
    }
    cur_rec = NULL;
    V3 lo = { 1e300, 1e300, 1e300 }, hi = { -1e300, -1e300, -1e300 };
    for (int i = 0; i < f->npts; i++) {
        V3 p = f->pts[i];
        if (fabs(p.x) > 1e6 || fabs(p.y) > 1e6 || fabs(p.z) > 1e6) continue;
        if (p.x < lo.x) lo.x = p.x; if (p.y < lo.y) lo.y = p.y; if (p.z < lo.z) lo.z = p.z;
        if (p.x > hi.x) hi.x = p.x; if (p.y > hi.y) hi.y = p.y; if (p.z > hi.z) hi.z = p.z;
    }
    if (lo.x > hi.x) { lo.x = lo.y = lo.z = -10; hi.x = hi.y = hi.z = 10; }
    f->lo = lo; f->hi = hi;
    return 1;
}

/* X9 -> 2017 op record update. mcprvverfile.dll (Ghidra project ~/mastercam-re/ghidra_prv, decomp/sd1/): ReadPrevVerEntFromDisk
   (0x180003560) takes file major 0x12 to FUN_180065720 (decomp/sd1/prv_readent.c), which reads the X9 record and converts it with
   FUN_18005e7f0 (decomp/sd1/prv_x9conv.c): the X9 record copied whole (0x2938 bytes = ent + operation), then for ASSOC 1 (op) records
   by opcode (ent +0x6c) the blocks below moved - each memcpy_s reads the X9 record, so a block that grew from 0x68 to 0x70 bytes keeps
   the whole copy's 8 bytes at its end - and for opcodes 0x3c / 0x48 new fields set to fixed values by IsMetric(). Opcode 0x1b8
   (ModuleWorks ops) is renumbered by GetUpdatedOpcodeMW: NOT DECODED (returns 0). Not ported: FUN_180065720's opcode 0x6c step
   (+0x13e0 -> +0x138) inside its variable-data branch. The op struct version (+0x1218, 19.1) is kept (ProcessAssocOperation,
   decomp/mccore_tooladd.c FUN_1802cfb60, changes it only for opcodes 0x2d / 0x30 / 0x6e of versions < 9.1). Offsets: ent offsets. */
static void x9_new_fields(uint8_t *e, int b, int metric) {   /* FUN_18005e7f0 opcode 0x3c (B 0x1540) / 0x48 (B 0x14c8) */
    double d2 = 2.0, d1 = 1.0, a = metric ? 100.0 : 4.0, c = metric ? 2.0 : 0.1, g = metric ? 10.0 : 0.5;
    e[b] = e[b + 1] = 0; memcpy(e + b + 8, &d2, 8); e[b + 0x10] = 1; memcpy(e + b + 0x18, &a, 8); e[b + 0x20] = 0;
    memcpy(e + b + 0x28, &c, 8); e[b + 0x30] = 1; memcpy(e + b + 0x38, &d1, 8); memcpy(e + b + 0x40, &d2, 8); memcpy(e + b + 0x48, &g, 8);
}
int mcam_x9_op_update(uint8_t *e, int metric) {
    static uint8_t x[0x2938];
    memcpy(x, e, sizeof x);
    int oc = (int16_t)rd16(e + 0x6c);
#define MV(dst, src, n) memcpy(e + (dst), x + (src), (n))
    switch (oc) {
    case 0x1b8: return 0;
    case 2: case 0x1c: MV(0x1308, 0x1308, 0x50); MV(0x1358, 0x1358, 0x68); MV(0x13c8, 0x13c0, 8); e[0x13d0] = x[0x13c8]; break;
    case 4: case 0x50: MV(0x1468, 0x1468, 0x68); break;
    case 0x1d: MV(0x1528, 0x1528, 8); MV(0x1538, 0x1530, 0x18); MV(0x1550, 0x1548, 0x68); break;
    case 0x2d: case 0x70: MV(0x1558, 0x1558, 0x28); MV(0x1580, 0x1580, 8); MV(0x1588, 0x1588, 8); MV(0x1598, 0x1590, 0x68); break;
    case 0x2e: MV(0x15e0, 0x15e0, 8); MV(0x15f0, 0x15e8, 0x68); break;
    case 0x30: MV(0x1578, 0x1578, 8); MV(0x1588, 0x1580, 0x18); MV(0x15a0, 0x1598, 0x68); break;
    case 0x3c: x9_new_fields(e, 0x1540, metric); break;
    case 0x48: x9_new_fields(e, 0x14c8, metric); break;
    case 0x6e: MV(0x1678, 0x1678, 0x28); MV(0x16a0, 0x16a0, 8); MV(0x16a8, 0x16a8, 8); MV(0x16b8, 0x16b0, 0x68); break;
    case 0x6f: MV(0x1510, 0x1510, 8); MV(0x1520, 0x1518, 0x68); break;
    case 0x9a: e[0x14cc] = 2; e[0x14cd] = e[0x14ce] = e[0x14cf] = 0; break;
    }
#undef MV
    return 1;
}

void mcam_free(McamFile *f) {
    for (int i = 0; i < f->nops; i++) { free(f->ops[i].raw); free(f->ops[i].nci); }
    free(f->pts); free(f->polys); free(f->ops); free(f->ents); free(f->tp);
    for (int i = 0; i < f->nsolids; i++) { free(f->solids[i].xt); free(f->solids[i].brep); free(f->solids[i].ops_raw); free(f->solids[i].ops); }
    for (int i = 0; i < f->ntools; i++) free(f->tools[i].holder_rz);
    for (int i = 0; i < f->nsurfs; i++) free(f->surfs[i].rec);
    for (int i = 0; i < f->ncrvs; i++) free(f->crvs[i].rec);
    free(f->crvs); free(f->fviews); free(f->planes_list);
    free(f->solids); free(f->surfs); free(f->sld_ids); free(f->grp_names); free(f->dnew);
    for (int i = 0; i < f->nnotes; i++) { for (int k = 0; k < f->notes[i].nmt; k++) free(f->notes[i].mt[k].p); free(f->notes[i].mt); }
    free(f->notes); free(f->lvls);
    for (int i = 0; i < f->nlbdry; i++) free(f->lbdry[i].rec);
    free(f->lbdry); free(f->lgrp);
    for (int i = 0; i < f->nltl; i++) free(f->ltl[i].body);
    for (int i = 0; i < f->nmdrec; i++) free(f->mdrec[i].body);
    free(f->ltl); free(f->mdrec); free(f->lgrp_mach);
    for (int i = 0; i < f->nmeshes; i++) free(f->meshes[i].p);
    free(f->meshes);
    memset(f, 0, sizeof *f);
}

const Surf *mcam_surf_ref(const McamFile *f, int64_t e) {
    for (int i = 0; i < f->nsurfs; i++) if (f->surfs[i].eptr == e) return &f->surfs[i];
    for (int i = 0; i < f->nsurfs; i++) { int k = f->surfs[i].ent; if (k >= 0 && k < f->nents && f->ents[k].created && f->ents[k].septr == e) return &f->surfs[i]; }
    return NULL;
}
const Surf *mcam_surf_base(const McamFile *f, const Surf *s) {
    if (!s || s->kind != 10 || s->n < 0x108) return NULL;
    int64_t e = (int64_t)rd64(s->rec + 0x100);
    for (int i = 0; i < f->nsurfs; i++) if (f->surfs[i].eptr == e && &f->surfs[i] != s) return &f->surfs[i];
    return NULL;
}
const Tool *mcam_tool(const McamFile *f, int num) {
    for (int i = f->ntools - 1; i >= 0; i--) if (f->tools[i].num == num) return &f->tools[i];
    return NULL;
}

double mcam_tool_dia(const McamFile *f, int num) {
    for (int i = 0; i < f->ntools; i++) if (f->tools[i].num == num) return f->tools[i].dia;
    return 0;
}

int mcam_add_op(McamFile *f, const char *type, const char *comment, int tool, const Move *m, int n) {
    f->ops = xrealloc(f->ops, (f->nops + 1) * sizeof(Op));
    Op *o = &f->ops[f->nops]; memset(o, 0, sizeof *o);
    int maxid = 0, maxseq = 0;
    for (int i = 0; i < f->nops; i++) { if (f->ops[i].id > maxid) maxid = f->ops[i].id; if (f->ops[i].seq < (1 << 29) && f->ops[i].seq > maxseq) maxseq = f->ops[i].seq; }
    o->id = maxid + 1; o->seq = maxseq + 1; o->tool = tool; o->slot = -1;
    snprintf(o->type, sizeof o->type, "%s", type); snprintf(o->comment, sizeof o->comment, "%s", comment ? comment : "");
    o->first_poly = f->npolys;
    int line = -1, kind = -1;
    for (int i = 1; i < n; i++) {
        int k = m[i].rapid ? K_RAPID : K_CUT;
        if (line < 0 || k != kind) { line = newpoly(f, k, o->id, tool, 0, 0); kind = k; polypt(f, line, m[i - 1].p); }
        polypt(f, line, m[i].p);
    }
    o->npolys = f->npolys - o->first_poly;
    for (int i = o->first_poly; i < f->npolys; i++) o->npts += f->polys[i].count;
    f->nmoves += n;
    return f->nops++;
}

void mcam_move_op(McamFile *f, int from, int to) {     /* Operations Manager order = F.ops order (post, tree) */
    if (from < 0 || from >= f->nops || to < 0 || to >= f->nops || from == to) return;
    Op o = f->ops[from];
    if (from < to) memmove(&f->ops[from], &f->ops[from + 1], (size_t)(to - from) * sizeof(Op));
    else memmove(&f->ops[to + 1], &f->ops[to], (size_t)(from - to) * sizeof(Op));
    f->ops[to] = o;
}

void mcam_remove_op(McamFile *f, int index) {
    if (index < 0 || index >= f->nops) return;
    int id = f->ops[index].id, k = 0;
    for (int i = 0; i < f->npolys; i++)                      /* drop its polylines (their points stay unused) */
        if (f->polys[i].kind == K_GEOM || f->polys[i].op != id) f->polys[k++] = f->polys[i];
    f->npolys = k;
    free(f->ops[index].raw); free(f->ops[index].nci);
    memmove(f->ops + index, f->ops + index + 1, (f->nops - index - 1) * sizeof(Op));
    f->nops--;
    for (int o = 0; o < f->nops; o++) {                      /* polylines of each op are contiguous: re-find them */
        Op *op = &f->ops[o]; op->first_poly = -1; op->npolys = 0;
        for (int i = 0; i < f->npolys; i++) if (f->polys[i].kind != K_GEOM && f->polys[i].op == op->id) {
            if (op->first_poly < 0) op->first_poly = i;
            op->npolys++;
        }
        if (op->first_poly < 0) op->first_poly = f->npolys;
    }
}

void mcam_new_empty(McamFile *f) {
    memset(f, 0, sizeof *f); f->metric = f->nci_metric = -1; f->mt_next = 1;
    f->lo.x = f->lo.y = f->lo.z = -50; f->hi.x = f->hi.y = f->hi.z = 50;
}

void mcam_update_extents(McamFile *f) {
    V3 lo = { 1e300, 1e300, 1e300 }, hi = { -1e300, -1e300, -1e300 };
    for (int i = 0; i < f->npolys; i++) for (int k = 0; k < f->polys[i].count; k++) {
        V3 p = f->pts[f->polys[i].first + k];
        if (fabs(p.x) > 1e6 || fabs(p.y) > 1e6 || fabs(p.z) > 1e6) continue;
        if (p.x < lo.x) lo.x = p.x; if (p.y < lo.y) lo.y = p.y; if (p.z < lo.z) lo.z = p.z;
        if (p.x > hi.x) hi.x = p.x; if (p.y > hi.y) hi.y = p.y; if (p.z > hi.z) hi.z = p.z;
    }
    if (lo.x > hi.x) { lo.x = lo.y = lo.z = -50; hi.x = hi.y = hi.z = 50; }
    f->lo = lo; f->hi = hi;
}

/* ours: a session-unique stand-in eptr for an entity made here (Ent.septr), far above every file eptr (the save allocates from the
   file's highest + 0x1000) */
int64_t mcam_session_eptr(void) { static int64_t next = 0x7e00000000000000LL; next += 0x10; return next; }
/* a new surface made here (E_SURF): rec = a whole SURF_ID record (class 0x100, the surf header at +0x68 and its kind's data), copied;
   its header (+0..+0x67) is the writer's on save (mcam_saveops.c). Returns the Ent index. */
int mcam_new_surf(McamFile *f, const uint8_t *rec, uint32_t n, int level, int color) {
    f->surfs = xrealloc(f->surfs, (f->nsurfs + 1) * sizeof *f->surfs);
    Surf *sf = &f->surfs[f->nsurfs++]; memset(sf, 0, sizeof *sf);
    sf->rec = xmalloc(n); memcpy(sf->rec, rec, n); sf->n = n; sf->kind = n > 0x68 ? rec[0x68] : 0;
    sf->level = level; sf->color = color; sf->rec[6] = (uint8_t)color; memcpy(sf->rec + 8, &level, 4);
    cur_rec = NULL;
    Ent *en = add_ent(f, E_SURF, level, color);
    en->created = 1; en->ln_style = 1; en->cls = 0x100; en->planar = 0; en->npoly = 0; en->srf = f->nsurfs - 1;
    en->septr = mcam_session_eptr(); sf->eptr = en->septr;   /* what other new records reference until the save gives it a file eptr */
    sf->ent = f->nents - 1;
    return f->nents - 1;
}

/* ------------------------------------------------------------------------------------------------ polygonal meshes (formats/cad_import.md 7) */
int mcam_stl_entry_bin(const uint8_t *b, size_t n, StlPoly **out, int *mixed) {
    *out = NULL; *mixed = 0;
    /* CreateSTLEntryFromStream, binary branch: SysFile::Read 0x54 bytes, then param_2 (the size) must be count * 0x32 + 0x54 */
    if (n < 0x54) return -1;
    int32_t cnt = (int32_t)rd32(b + 0x50);
    if (cnt < 0 || (int64_t)n != (int64_t)cnt * 0x32 + 0x54) return -1;
    StlPoly *p = xcalloc(cnt ? (size_t)cnt : 1, sizeof *p);
    uint8_t a0 = 0;
    for (int32_t i = 0; i < cnt; i++) {
        const uint8_t *r = b + 0x54 + (size_t)i * 0x32;
        float q[12]; memcpy(q, r, 48);
        uint8_t at = r[0x30];                                /* (char)local_808: the attribute's low byte */
        if (i == 0) a0 = at; else if (at != a0) *mixed = 1;   /* bVar5: any facet's byte differs from the first's */
        float x = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];   /* float arithmetic as the DLL (sqrtf) */
        if ((double)x <= 1e-12) { q[0] = q[1] = q[2] = 0; }
        else { float l = sqrtf(x); q[0] /= l; q[1] /= l; q[2] /= l; }
        memcpy(p[i].n, q, 12); memcpy(p[i].v, q + 3, 36); p[i].attr = at;
    }
    *out = p; return cnt;
}
uint8_t *mcam_stl_entry_write(const McMesh *m, size_t *n) {
    *n = 0;
    if (m->kind != 0 || m->mixed) return NULL;
    /* WriteEnhancedSTL's runs, unsorted branch (param_3 false): one run of every facet with the first facet's byte (local_b0) */
    size_t len = 0x54 + (size_t)m->np * 0x32;
    uint8_t *o = xcalloc(len, 1);
    static const char H[] = "MASTERCAM STL DATA";              /* s_MASTERCAM_STL_DATA_181306720: 0x50 bytes, the text then spaces */
    memset(o, ' ', 0x50); memcpy(o, H, sizeof H - 1);
    uint32_t c = (uint32_t)m->np; memcpy(o + 0x50, &c, 4);
    uint8_t col = m->np ? m->p[0].attr : 0;
    for (int i = 0; i < m->np; i++) {
        uint8_t *r = o + 0x54 + (size_t)i * 0x32;
        memcpy(r, m->p[i].n, 12);                             /* the first vertex record's normal (the enhanced vertex: x y z nx ny nz) */
        memcpy(r + 12, m->p[i].v, 36);
        r[0x30] = col; r[0x31] = 0;                           /* local_1c8 = the run's colour, local_1c7 = 0 */
    }
    *n = len; return o;
}
int mcam_new_mesh(McamFile *f, const uint8_t *rec, StlPoly *p, int np, int mixed, int level, int color) {
    f->meshes = xrealloc(f->meshes, (f->nmeshes + 1) * sizeof *f->meshes);
    McMesh *m = &f->meshes[f->nmeshes++]; memset(m, 0, sizeof *m);
    memcpy(m->rec, rec, 0x130); m->kind = (int16_t)rd16(rec + 0x68); m->p = p; m->np = np; m->mixed = mixed;
    m->level = level; m->color = color; m->rec[6] = (uint8_t)color; memcpy(m->rec + 8, &level, 4);
    cur_rec = NULL;
    Ent *en = add_ent(f, E_MESH, level, color);
    en->created = 1; en->ln_style = 1; en->cls = 0x100000; en->planar = 0; en->npoly = 0; en->msh = f->nmeshes - 1;
    en->septr = mcam_session_eptr(); m->eptr = en->septr;
    m->ent = f->nents - 1;
    return f->nents - 1;
}

/* the Ent of a note made here (draft_note.c mcam_note_add): F.notes[k] must exist; E_NOTE, created, no polylines */
int mcam_new_note_ent(McamFile *f, int k) {
    const uint8_t *E = f->notes[k].E;
    cur_rec = NULL;
    Ent *en = add_ent(f, E_NOTE, (int32_t)rd32(E + 8), E[6]);
    en->created = 1; en->cls = rd32(E); en->planar = 0; en->npoly = 0; en->nt = k;
    en->ln_style = (int16_t)rd16(E + 0x18); en->pt_style = (int16_t)rd16(E + 0x1c); memcpy(&en->width, E + 0x10, 8);
    en->septr = mcam_session_eptr();
    f->notes[k].ent = f->nents - 1;
    return f->nents - 1;
}

/* a surface curve's polyline (E_SCRV): mcam_cons_poly on its CONS record, its uv curve (+0xd8) and its surface (+0xd0; the base of a
   trimmed one given too) - no polyline (not drawn, not picked) when the hook is not linked or the surface kind is not evaluated there */
int (*mcam_cons_poly)(const uint8_t *, uint32_t, const uint8_t *, uint32_t, const uint8_t *, uint32_t, const uint8_t *, uint32_t, double (**)[3]);
static void cons_poly(McamFile *f, int ei) {
    Ent *en = &f->ents[ei]; en->npoly = 0;
    if (!mcam_cons_poly || en->crv < 0 || en->crv >= f->ncrvs) return;
    const CurveRec *cr = &f->crvs[en->crv]; if (cr->n < 0x68 + 0x88) return;
    const CurveRec *uv = mcs_curve(f, (int64_t)rd64(cr->rec + 0xd8)); const Surf *sf = mcam_surf_ref(f, (int64_t)rd64(cr->rec + 0xd0));
    if (!uv || !sf) return;
    const Surf *b = sf->kind == 10 && sf->n >= 0x108 ? mcam_surf_ref(f, (int64_t)rd64(sf->rec + 0x100)) : NULL;
    double (*pts)[3] = NULL; int np = mcam_cons_poly(cr->rec, cr->n, uv->rec, uv->n, sf->rec, sf->n, b ? b->rec : NULL, b ? b->n : 0, &pts);
    if (np < 2) { free(pts); return; }
    int p = newpoly(f, K_GEOM, 0, 0, (uint8_t)en->color, en->level);
    for (int k = 0; k < np; k++) polypt(f, p, (V3){ pts[k][0], pts[k][1], pts[k][2] });
    free(pts);
    en = &f->ents[ei]; en->first_poly = p; en->npoly = 1; f->polys[p].ln_style = (uint8_t)(en->ln_style >= 1 ? en->ln_style : 1);
}
/* a new surface curve (Curve Surface, viewer/mccsurf.c): its uv curve's record (array_2d_to_nurbs 0x18023d330 + put_nurbs_in_db: 2D,
   degree 1, flags 0x14, knots 0, 0, 1, .., n - 1, n - 1, +0x98 / +0xa0 0 / n - 1, +0xa8 1e-9 (put_nurbs_in_db's curvature below degree
   2), +0xc0 1.0 - the record the DLL stores, tools/peloader --csurf with CSURF_RAW) and the CONS record (the body; +0xd0 the surface's
   eptr, its base for a trimmed one, +0xd8 the uv curve's), session eptrs (the save gives both new ones and remaps the references) */
int mcam_new_cons(McamFile *f, const uint8_t *body, const double (*uv)[2], int npts, int surf_ent, int level, int color) {
    if (npts < 2 || surf_ent < 0 || surf_ent >= f->nents || f->ents[surf_ent].type != E_SURF) return -1;
    const Surf *sf = &f->surfs[f->ents[surf_ent].srf];
    const Surf *b = sf->kind == 10 && sf->n >= 0x108 ? mcam_surf_ref(f, (int64_t)rd64(sf->rec + 0x100)) : NULL;
    int64_t sep = b ? b->eptr : sf->eptr;
    if (b && b->ent >= 0 && b->ent < f->nents && f->ents[b->ent].created) sep = f->ents[b->ent].septr;
    else if (!b && f->ents[surf_ent].created) sep = f->ents[surf_ent].septr;
    uint32_t nk = (uint32_t)npts + 2, nu = 0xd8 + 8 * (nk + 2 * (uint32_t)npts);
    uint8_t *u = xcalloc(1, nu), *c = xcalloc(1, 0x68 + 0x88);
    uint32_t cl = 0x80; memcpy(u, &cl, 4); u[6] = (uint8_t)color; memcpy(u + 8, &level, 4);
    double z = 0.0, t2 = npts - 1, k9 = 1e-09, one = 1.0;
    memcpy(u + 0x68, uv[0], 16); memcpy(u + 0x78, &z, 8); memcpy(u + 0x80, uv[npts - 1], 16); memcpy(u + 0x90, &z, 8);
    memcpy(u + 0x98, &z, 8); memcpy(u + 0xa0, &t2, 8); memcpy(u + 0xa8, &k9, 8); memcpy(u + 0xc0, &one, 8);
    int32_t nki = (int32_t)nk, nc = npts; memcpy(u + 0xc8, &nki, 4); memcpy(u + 0xcc, &nc, 4); u[0xd0] = 1; u[0xd1] = 2; u[0xd2] = 0x14;
    double K; for (int i = 0; i < (int)nk; i++) { K = i == 0 ? 0.0 : i - 1 < npts ? i - 1 : npts - 1; memcpy(u + 0xd8 + 8 * (size_t)i, &K, 8); }
    memcpy(u + 0xd8 + 8 * nk, uv, 16 * (size_t)npts);
    f->crvs = xrealloc(f->crvs, (f->ncrvs + 2) * sizeof *f->crvs);
    CurveRec *ur = &f->crvs[f->ncrvs++]; memset(ur, 0, sizeof *ur); ur->cls = 0x80; ur->rec = u; ur->n = nu; ur->eptr = mcam_session_eptr();
    cl = 0x400; memcpy(c, &cl, 4); c[6] = (uint8_t)color; memcpy(c + 8, &level, 4);
    memcpy(c + 0x68, body, 0x88);
    memcpy(c + 0xd0, &sep, 8); memcpy(c + 0xd8, &ur->eptr, 8);
    CurveRec *cr = &f->crvs[f->ncrvs++]; memset(cr, 0, sizeof *cr); cr->cls = 0x400; cr->rec = c; cr->n = 0x68 + 0x88;
    cur_rec = NULL;
    Ent *en = add_ent(f, E_SCRV, level, color);
    int k = f->nents - 1;
    en->created = 1; en->ln_style = 1; en->width = 1; en->cls = 0x400; en->septr = mcam_session_eptr(); cr->eptr = en->septr;
    en->a = rdv(c + 0x68); en->b = rdv(c + 0x80); en->crv = f->ncrvs - 1; en->planar = 0; en->npoly = 0;
    cons_poly(f, k);
    en = &f->ents[k];
    if (en->npoly == 1) {   /* the new polyline goes in front of the toolpaths, as mcam_new_curve does */
        int pi = f->npolys - 1, q = 0;
        while (q < f->npolys && f->polys[q].kind == K_GEOM) q++;
        int fp = pi;
        if (q < pi) {
            Poly t = f->polys[pi];
            memmove(f->polys + q + 1, f->polys + q, (pi - q) * sizeof(Poly));
            f->polys[q] = t; fp = q;
            for (int o = 0; o < f->nops; o++) if (f->ops[o].first_poly >= q) f->ops[o].first_poly++;
        }
        en->first_poly = fp;
    }
    mcam_update_extents(f);
    return k;
}
/* a new spline curve made here (Transform of a spline / an arc made a NURBS by a non-uniform scale, formats/wireframe.md "Transform"): the
   class 8 / 0x80 record as built (header bytes are the writer's, mcam_saveops.c), an E_SPLINE as the loader makes them; its polyline as
   spline_poly with curve_chord_dev of the part units (mcam_is_metric) */
int mcam_new_curve(McamFile *f, const uint8_t *rec, uint32_t n, int level, int color) {
    if (n < 0xa8) return -1;
    uint32_t cls; memcpy(&cls, rec, 4); if (cls != 0x8 && cls != 0x80) return -1;
    f->crvs = xrealloc(f->crvs, (f->ncrvs + 1) * sizeof *f->crvs);
    CurveRec *cr = &f->crvs[f->ncrvs++]; memset(cr, 0, sizeof *cr);
    cr->cls = cls; cr->rec = xmalloc(n); memcpy(cr->rec, rec, n); cr->n = n; cr->rec[6] = (uint8_t)color; memcpy(cr->rec + 8, &level, 4);
    if (!mcs_spline_ok(cr)) { free(cr->rec); f->ncrvs--; return -1; }
    cur_rec = NULL;
    Ent *en = add_ent(f, E_SPLINE, level, color);
    int k = f->nents - 1;
    en->created = 1; en->ln_style = 1; en->width = 1; en->cls = (int)cls; en->septr = mcam_session_eptr(); cr->eptr = en->septr;
    en->a = rdv(rec + 0x68); en->b = rdv(rec + 0x80); en->crv = f->ncrvs - 1; en->planar = spline_planar(cr, en->a.z);
    int p = newpoly(f, K_GEOM, 0, 0, (uint8_t)color, level);
    spline_poly_dev(f, p, &f->crvs[f->ncrvs - 1], en->a, en->b, mcam_is_metric(f) ? 0.02 : 0.001);
    /* the new polyline goes in front of the toolpaths, as mcam_new_ent does */
    int pi = f->npolys - 1, q = 0;
    while (q < f->npolys && f->polys[q].kind == K_GEOM) q++;
    int fp = pi;
    if (q < pi) {
        Poly t = f->polys[pi];
        memmove(f->polys + q + 1, f->polys + q, (pi - q) * sizeof(Poly));
        f->polys[q] = t; fp = q;
        for (int o = 0; o < f->nops; o++) if (f->ops[o].first_poly >= q) f->ops[o].first_poly++;
    }
    en = &f->ents[k]; en->first_poly = fp; en->npoly = 1; f->polys[fp].ln_style = 1;
    mcam_update_extents(f);
    return k;
}
/* a new entity drawn here: E_POINT (a), E_LINE (a -> b), E_ARC (centre c, radius r, start angle a0, sweep sw; XY plane) */
int mcam_new_ent(McamFile *f, int type, V3 a, V3 b, V3 c, double r, double a0, double sw, int level, int color) {
    cur_rec = NULL;
    Ent *en = add_ent(f, type, level, color);
    en->created = 1; en->ln_style = 1; en->width = 1;   /* new geometry: solid line style, width 1 (core_ui cui_attr.c attr_new_ent gives the main attributes) */
    en->septr = mcam_session_eptr();
    if (type == E_POINT) { en->a = en->b = a; int p = newpoly(f, K_GEOM, 0, 0, (uint8_t)color, level); polypt(f, p, a); polypt(f, p, a); en->cls = 1; en->pt_style = 1; }
    else if (type == E_LINE) { en->a = a; en->b = b; int p = newpoly(f, K_GEOM, 0, 0, (uint8_t)color, level); polypt(f, p, a); polypt(f, p, b); en->cls = 2; }
    else {
        en->c = c; en->r = r; en->a0 = a0; en->sw = sw; en->cls = 4;
        en->a = (V3){ c.x + r * cos(a0), c.y + r * sin(a0), c.z }; en->b = (V3){ c.x + r * cos(a0 + sw), c.y + r * sin(a0 + sw), c.z };
        add_arc(f, K_GEOM, 0, 0, (uint8_t)color, level, c, r, a0, sw, 0, 1, 2, c.z, c.z);
    }
    en->npoly = f->npolys - en->first_poly;
    /* geometry polylines must stay ahead of toolpath polylines (drawing / grouping assume it): move it to the front */
    int pi = f->npolys - 1, k = 0;
    while (k < f->npolys && f->polys[k].kind == K_GEOM) k++;
    if (k < pi) {
        Poly t = f->polys[pi];
        memmove(f->polys + k + 1, f->polys + k, (pi - k) * sizeof(Poly));
        f->polys[k] = t; en->first_poly = k;
        for (int o = 0; o < f->nops; o++) if (f->ops[o].first_poly >= k) f->ops[o].first_poly++;
    }
    mcam_update_extents(f);
    return f->nents - 1;
}

int mcam_new_arc_view(McamFile *f, int view, const double M[9], V3 cv, double r, double a0, double sw, int level, int color) {
    V3 cw = { cv.x * M[0] + cv.y * M[3] + cv.z * M[6], cv.x * M[1] + cv.y * M[4] + cv.z * M[7], cv.x * M[2] + cv.y * M[5] + cv.z * M[8] };   /* view_to_world */
    if (fabs(fabs(M[8]) - 1) < 1e-9) {            /* normal along world Z: an XY arc, angles as the loader maps them */
        double b = atan2(M[1], M[0]);
        if (M[8] > 0) a0 = b + a0; else { a0 = b - a0; sw = -sw; }
        a0 = fmod(a0, 2 * M_PI); if (a0 < 0) a0 += 2 * M_PI;
        if (sw < 0) { a0 += sw; sw = -sw; if (a0 < 0) a0 += 2 * M_PI; }   /* arcs made here are stored counter-clockwise (geo_add) */
        return mcam_new_ent(f, E_ARC, cw, cw, cw, r, a0, sw, level, color);
    }
    cur_rec = NULL;
    Ent *en = add_ent(f, E_ARC, level, color);
    int k = f->nents - 1;
    en->created = 1; en->ln_style = 1; en->cls = 4; en->planar = 0; en->septr = mcam_session_eptr();
    en->vw = view; memcpy(en->vm, M, sizeof en->vm);
    en->c = cw; en->r = r; en->a0 = a0; en->sw = sw;
    en->a = mcam_arc_pt(en, a0); en->b = mcam_arc_pt(en, a0 + sw);
    int fp = f->npolys; add_view_arc(f, en, (uint8_t)color, level);
    /* the new polyline goes in front of the toolpaths, as mcam_new_ent does */
    int pi = f->npolys - 1, q = 0;
    while (q < f->npolys && f->polys[q].kind == K_GEOM) q++;
    if (q < pi) {
        Poly t = f->polys[pi];
        memmove(f->polys + q + 1, f->polys + q, (pi - q) * sizeof(Poly));
        f->polys[q] = t; fp = q;
        for (int o = 0; o < f->nops; o++) if (f->ops[o].first_poly >= q) f->ops[o].first_poly++;
    }
    en = &f->ents[k]; en->first_poly = fp; en->npoly = 1; f->polys[fp].ln_style = 1;
    mcam_update_extents(f);
    return k;
}

/* the same with the WORLD centre (copies of an Ent.vw arc: clipboard, Analyze, projects): cv = world_to_view(cw) */
int mcam_new_arc_view_w(McamFile *f, int view, const double M[9], V3 cw, double r, double a0, double sw, int level, int color) {
    V3 cv = { M[0] * cw.x + M[1] * cw.y + M[2] * cw.z, M[3] * cw.x + M[4] * cw.y + M[5] * cw.z, M[6] * cw.x + M[7] * cw.y + M[8] * cw.z };
    return mcam_new_arc_view(f, view, M, cv, r, a0, sw, level, color);
}
void mcam_delete_ent(McamFile *f, int i) {
    if (i < 0 || i >= f->nents) return;
    Ent *en = &f->ents[i];
    for (int k = 0; k < en->npoly; k++) if (en->first_poly + k < f->npolys) f->polys[en->first_poly + k].count = 0;
    if (en->type == E_SURF && en->srf >= 0 && en->srf < f->nsurfs) f->surfs[en->srf].off = 1;
    if (en->type == E_MESH && en->msh >= 0 && en->msh < f->nmeshes) f->meshes[en->msh].off = 1;
    en->type = 0;
}
/* the eptr fields of a SURF_ID record that reference other entities, by kind (formats/surfaces.md 2: trimmed 10 base D+0, offset 8 base
   D+0x20, revolved 2 profile D+0x48, draft 6 curve D+0x20; D = ent +0x100); the kinds' other references (4 / 7) are added with the
   commands that build them. Returns the count. */
int mcam_surf_ref_offsets(const uint8_t *rec, uint32_t n, uint32_t *offs) {
    static const struct { int kind; uint32_t off; } R[] = { { 10, 0x100 }, { 8, 0x120 }, { 2, 0x148 }, { 6, 0x120 } };
    int c = 0;
    for (size_t r = 0; n > 0x68 && r < sizeof R / sizeof R[0]; r++) if (rec[0x68] == R[r].kind && R[r].off + 8 <= n) offs[c++] = R[r].off;
    return c;
}

/* polylines of each op are contiguous: find them again after polylines were removed / appended */
static void refind_polys(McamFile *f) {
    for (int o = 0; o < f->nops; o++) {
        Op *op = &f->ops[o]; op->first_poly = -1; op->npolys = 0; op->npts = 0;
        for (int i = 0; i < f->npolys; i++) if (f->polys[i].kind != K_GEOM && f->polys[i].op == op->id) {
            if (op->first_poly < 0) op->first_poly = i;
            op->npolys++; op->npts += f->polys[i].count;
        }
        if (op->first_poly < 0) op->first_poly = f->npolys;
    }
}
/* the binary NCI section (-2 .. -3 records) of one op: its rewritten one (ops[index].nci), else read from the part's file: the records from nci_off on (format of nci() above); 1 = ok */
int mcam_op_nci_section(const McamFile *f, int index, uint8_t **sec, size_t *n) {
    *sec = NULL; *n = 0;
    if (index < 0 || index >= f->nops) return 0;
    if (f->ops[index].nci) { *n = f->ops[index].nci_n; *sec = malloc(*n ? *n : 1); memcpy(*sec, f->ops[index].nci, *n); return 1; }
    if (!f->path[0] || !f->nci_off) return 0;
    FILE *fp = fopen(f->path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END); long len = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (len <= 0 || (size_t)len <= f->nci_off) { fclose(fp); return 0; }
    uint8_t *d = malloc((size_t)len); size_t got = fread(d, 1, (size_t)len, fp); fclose(fp);
    if (got != (size_t)len) { free(d); return 0; }
    size_t off = f->nci_off, start = 0, end = 0; int found = 0;
    while (off + 2 <= got) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > got) break;
        int16_t g = (int16_t)(d[off + 2] | d[off + 3] << 8);
        if (g == -2) {
            if (found) { end = off; break; }
            int32_t id = (int32_t)(d[off + 2 + 0x1c] | d[off + 2 + 0x1d] << 8 | d[off + 2 + 0x1e] << 16 | (uint32_t)d[off + 2 + 0x1f] << 24);
            if (id == f->ops[index].id) { found = 1; start = off; }
        }
        off += 2u + sz;
    }
    if (found && !end) end = off;
    if (found) { *n = end - start; *sec = malloc(*n ? *n : 1); memcpy(*sec, d + start, *n); }
    free(d);
    return found;
}
void mcam_op_set_nci(McamFile *f, int index, uint8_t *sec, size_t n) {
    if (index < 0 || index >= f->nops) { free(sec); return; }
    int id = f->ops[index].id, k = 0;
    free(f->ops[index].nci); f->ops[index].nci = sec; f->ops[index].nci_n = n;
    for (int i = 0; i < f->npolys; i++)                      /* its old polylines go (their points stay unused) */
        if (f->polys[i].kind == K_GEOM || f->polys[i].op != id) f->polys[k++] = f->polys[i];
    f->npolys = k;
    int metric = f->metric, ncim = f->nci_metric; long nm = f->nmoves, na = f->nnciarcs, nd = f->ndrill;
    nci(f, sec, n, 0);                                       /* appended at the end: contiguous */
    f->metric = metric; f->nci_metric = ncim; f->nmoves = nm; f->nnciarcs = na; f->ndrill = nd;
    refind_polys(f);
}

/* AddMCView (mastercam.dll 0x18031df40; find_view_k calls it with tol 2e-7): the first view whose matrix matches within tol element by element (mx33_match) is reused; else a new view numbered n_views + 1 (the highest view number in use:
   the 18 built in (n_views 18, decomp/mastercam_stdviews_init.c:28) and the part's own: stored numbers are not positions: the corpus stores gaps and out of order numbers in 89 of 1053 parts with views; reading goes through DbProcess::ProcessAssocViews -> MastercamPlanes::CheckViewEnt (decomp/mastercam_planes_serialize_mccore.c:186-205, the callee not read), so that a number here is free of the built-in ones is what is kept). LeftHandView (0x180322230) is only called to record the handedness: the matrix is kept. The record is written by the save (mcam_saveops.c, DB_ID 0x200 subtype 0). */
int mcam_add_view(McamFile *f, const double M[9], double tol) {
    int hi = 18;
    for (int v = 1; v <= 18; v++) { double T[9]; if (!mcam_std_view(v, T)) continue; int ok = 1; for (int k = 0; k < 9; k++) if (fabs(T[k] - M[k]) > tol) ok = 0; if (ok) return v; }
    for (int i = 0; i < f->nfviews; i++) { int ok = 1; for (int k = 0; k < 9; k++) if (fabs(f->fviews[i].m[k] - M[k]) > tol) ok = 0; if (ok) return f->fviews[i].n; if (f->fviews[i].n > hi) hi = f->fviews[i].n; }
    f->fviews = xrealloc(f->fviews, (f->nfviews + 1) * sizeof *f->fviews);
    f->fviews[f->nfviews].n = hi + 1; memcpy(f->fviews[f->nfviews].m, M, sizeof f->fviews[f->nfviews].m); f->fviews[f->nfviews].isnew = 1;
    return f->fviews[f->nfviews++].n;
}
