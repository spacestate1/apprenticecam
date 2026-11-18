/* cam_xformop.c - Mastercam 2017 Transform toolpath (opcode 4) NCI engine, ported from the decompiled mccore CBnciXfm
 * (decomp/xformop/, gitignored; citations [file:line] point there) - see ../formats/transform_toolpath.md.
 *
 * Ported path: Type Rotate (0xd, CalcRotateR) and Translate Rectangular (0x10 / 0x11, CalcTranslateDataRectangular), Method
 * Tool plane (+0x68 = 0), Source NCI, no new operations, work offset
 * numbering Automatic / Maintain, every record kind the tool-plane transform copies; with "Include origin" off the
 * motion goes through TransformBnciLineByCoordinate for the point records (lines, 5-axis G11, drill points, the tool
 * change's first point). Everything else is refused with the reason (never approximated). */
#include "cam_xformop.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TOL 0.0001            /* mccore `tol` (the system tolerance) - only used where the decompile compares with tol [ND value: 1e-4,
                                 as nci_reverse.c's TOL] */

static int16_t gh(const uint8_t *r, int o) { int16_t v; memcpy(&v, r + o, 2); return v; }
static void sh(uint8_t *r, int o, int16_t v) { memcpy(r + o, &v, 2); }
static int32_t gi(const uint8_t *r, int o) { int32_t v; memcpy(&v, r + o, 4); return v; }
static void si(uint8_t *r, int o, int32_t v) { memcpy(r + o, &v, 4); }
static double gd(const uint8_t *r, int o) { double v; memcpy(&v, r + o, 8); return v; }
static void sd(uint8_t *r, int o, double v) { memcpy(r + o, &v, 8); }
#define GC(r) gh(r, 0)

static int ob(const uint8_t *op, uint32_t n, int o) { return (uint32_t)o < n ? op[o] : 0; }
static int oh(const uint8_t *op, uint32_t n, int o) { return (uint32_t)o + 2 <= n ? gh(op, o) : 0; }
static int oi(const uint8_t *op, uint32_t n, int o) { return (uint32_t)o + 4 <= n ? gi(op, o) : 0; }
static double od(const uint8_t *op, uint32_t n, int o) { return (uint32_t)o + 8 <= n ? gd(op, o) : 0; }
#define PRM 0x11c0            /* prm_xform in the operation (init_operation_xform @18004cdb0: list_op_get_prev copy at op +0x11c0) */

static void nd(XfStats *st, const char *what) {
    st->not_decoded++;
    size_t l = strlen(st->note);
    if (l + strlen(what) + 3 < sizeof st->note && !strstr(st->note, what)) snprintf(st->note + l, sizeof st->note - l, "%s%s", l ? "; " : "", what);
}

/* ------------------------------------------------------------------------------------------------------ parameters */
void xfop_prm(const uint8_t *op, uint32_t n, XfPrm *p) {
    memset(p, 0, sizeof *p);
    int b = PRM;
    p->type = oh(op, n, b + 0x00); p->method_sub = oh(op, n, b + 0x70); p->plane_flag = ob(op, n, b + 0x72);
    p->count = oh(op, n, b + 0x74); p->count_y = oh(op, n, b + 0x76);
    for (int k = 0; k < 3; k++) p->pt[k] = od(op, n, b + 0x78 + 8 * k);
    p->angle_step = od(op, n, b + 0x90);
    for (int k = 0; k < 9; k++) p->plane_m[k] = od(op, n, b + 0x98 + 8 * k);
    p->total_sweep = ob(op, n, b + 0x101); p->rot_translate = ob(op, n, b + 0x102); p->angle_start = od(op, n, b + 0x120);
    p->custom = ob(op, n, b + 0x18); p->remove_comments = ob(op, n, b + 0x50); p->source_nci = ob(op, n, b + 0x51);
    p->keep_xform = ob(op, n, b + 0x52); p->wo_mode = oh(op, n, b + 0x54); p->wo_start = oi(op, n, b + 0x58);
    p->wo_inc = oi(op, n, b + 0x5c); p->match_wo = ob(op, n, b + 0x60); p->unique_subpgm = ob(op, n, b + 0x61);
    p->disable_src_post = ob(op, n, b + 0x62); p->copy_src = ob(op, n, b + 0x63); p->group_by_op = ob(op, n, b + 0x64);
    p->incremental = ob(op, n, b + 0x65); p->save_planes = ob(op, n, b + 0x66); p->new_ops = ob(op, n, b + 0x67);
    p->coord_method = ob(op, n, b + 0x68); p->subprogram = ob(op, n, b + 0x69); p->include_origin = ob(op, n, b + 0x6a);
    p->include_wcs = ob(op, n, b + 0x2b0); p->by_toolplane = !ob(op, n, b + 0x17);
}

/* ----------------------------------------------------------------------------------------------------------- views */
void xf_views_init(XfViews *V, const McamFile *f) {
    memset(V, 0, sizeof *V);
    int nf = f ? f->nfviews : 0;
    V->cap = 8 + nf + 16; V->num = malloc(V->cap * sizeof *V->num); V->m = malloc(V->cap * sizeof *V->m);
    for (int k = 1; k <= 8; k++) { V->num[V->n] = k; mcam_std_view(k, V->m[V->n]); V->n++; }
    V->nmax = 8;
    for (int i = 0; i < nf; i++) {
        if (f->fviews[i].n <= 8) continue;
        V->num[V->n] = f->fviews[i].n; memcpy(V->m[V->n], f->fviews[i].m, sizeof V->m[0]); V->n++;
        if (f->fviews[i].n > V->nmax) V->nmax = f->fviews[i].n;
    }
}
void xf_views_free(XfViews *V) { free(V->num); free(V->m); memset(V, 0, sizeof *V); }
static const double *view_m(const XfViews *V, int v) {
    for (int i = 0; i < V->n; i++) if (V->num[i] == v) return V->m[i];
    return NULL;
}
/* mx33_match @180017880 [mx.c]: every element within TOL */
static int mx33_match(const double *a, const double *b, double tol) {
    for (int k = 0; k < 9; k++) if (fabs(a[k] - b[k]) > tol) return 0;
    return 1;
}
/* find_view_m(m, &v, create = 1) @mastercam 180337b10 [mc_view.c]: views 1..n_views in number order, mx33_match 2e-7; not
   found -> AddMCView @18031df40: n_views + 1 */
static int find_view_m(XfViews *V, const double *m, XfStats *st) {
    for (int v = 1; v <= V->nmax; v++) { const double *w = view_m(V, v); if (w && mx33_match(m, w, 2e-7)) return v; }
    if (V->n == V->cap) { V->cap *= 2; V->num = realloc(V->num, V->cap * sizeof *V->num); V->m = realloc(V->m, V->cap * sizeof *V->m); }
    V->nmax++; V->num[V->n] = V->nmax; memcpy(V->m[V->n], m, sizeof V->m[0]); V->n++;
    V->added++; if (st) st->views_added++;
    return V->nmax;
}
/* view_to_world @mastercam 180341520 / world_to_view @180341b30: views < 2 the identity; else rows^T . p / rows . p */
static void view_to_world(const XfViews *V, const double p[3], int v, double out[3]) {
    const double *m = v >= 2 ? view_m(V, v) : NULL; double q[3] = { p[0], p[1], p[2] };
    if (!m) { memcpy(out, q, sizeof q); return; }
    for (int j = 0; j < 3; j++) out[j] = q[0] * m[j] + q[1] * m[3 + j] + q[2] * m[6 + j];
}
static void world_to_view(const XfViews *V, const double p[3], int v, double out[3]) {
    const double *m = v >= 2 ? view_m(V, v) : NULL; double q[3] = { p[0], p[1], p[2] };
    if (!m) { memcpy(out, q, sizeof q); return; }
    for (int i = 0; i < 3; i++) out[i] = m[3 * i] * q[0] + m[3 * i + 1] * q[1] + m[3 * i + 2] * q[2];
}
/* make_reals_0(double *, n) @18013ac10 [helpers2.c]: |x| < 1e-9 -> 0, |x| > 1e64 -> 0 */
static void make_reals_0(double *p, int n) {
    for (int k = 0; k < n; k++) { double a = fabs(p[k]); if ((a < 1e-9 && a > 0) || a > 1e64) p[k] = 0; }
}
/* x_matrix: 9 doubles (rows) + translation; xform_pt @1808eba20: p' = p . M + t (row vector) */
typedef struct { double m[9], t[3]; } Xm;
static void xform_pt(double p[3], const Xm *x) {
    double q[3];
    for (int j = 0; j < 3; j++) q[j] = p[0] * x->m[j] + p[1] * x->m[3 + j] + p[2] * x->m[6 + j] + x->t[j];
    memcpy(p, q, sizeof q);
}
/* SYSTYPES operator*(a, b) @180002a10 [sys_ops.c]: result[i][j] = sum_k b[i][k] a[k][j] = (b . a) */
static void mmul(const double *a, const double *b, double *r) {     /* r = b . a */
    double t[9];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) t[3 * i + j] = b[3 * i] * a[j] + b[3 * i + 1] * a[3 + j] + b[3 * i + 2] * a[6 + j];
    memcpy(r, t, sizeof t);
}

/* ----------------------------------------------------------------------------------------------- instance matrices */
/* CBnciXfmCalcXm::CalcRotateR @180151660 [core_range2.c]: COUNT + 1 instances, angle 0, START, START + STEP, ...; in the view
   R = [[c, s, 0], [-s, c, 0], [0, 0, 1]], t = (s cy + (1 - c) cx, (1 - c) cy - s cx, 0) with c = world_to_view(centre);
   ViewXmToWorld @180154cc0 (views >= 2; operand order from the disassembly vx_dis.txt): M = V^T . R . V, t = V^T t */
static int calc_rotate_r(XfViews *V, int count, double step, double start, int view, const double centre[3], Xm **out) {
    if (count < 1) return -1;
    Xm *x = calloc((size_t)count + 1, sizeof *x);
    double cv[3]; world_to_view(V, centre, view, cv);
    double a = start;
    for (int k = 0; k < count + 1; k++) {
        if (k == 0) a = 0; else if (k == 1) a = start;
        double c = cos(a * 0.01745329251994329), s = sin(a * 0.01745329251994329);
        Xm r = { { c, s, 0, -s, c, 0, 0, 0, 1 }, { s * cv[1] + (1 - c) * cv[0], (1 - c) * cv[1] - s * cv[0], 0 } };
        if ((unsigned short)view < 2) x[k] = r;
        else {
            const double *vm = view_m(V, view), vt[9] = { vm[0], vm[3], vm[6], vm[1], vm[4], vm[7], vm[2], vm[5], vm[8] };
            double t1[9]; mmul(vm, r.m, t1);              /* operator*(V, R) = R . V */
            mmul(t1, vt, x[k].m);                          /* operator*(R.V, V^T) = V^T . R . V */
            view_to_world(V, r.t, view, x[k].t);
        }
        a += step;
    }
    *out = x;
    return count + 1;
}

/* CalcTranslateDataRectangular @180152450: grid (i dx, j dy, 0), j outer, i inner, # < 1 -> 1, in view coordinates of
   FROM; CalcTranslateXMs @1801525e0: p0 = view_to_world(grid[0], FROM); each p = view_to_world(grid[k], FROM);
   Plane::CalcXmatrix(F = view FROM, p0, T = view TO, p) @mastercam 1803d7b90: M = operator*(T, F^T) = F^T . T (operands
   from cxm_dis.txt), t = p - p0 . M; |vec - 0| > tol (this +0xa8 = 0 after ZeroMemberVariables) -> t += vec. Views < 2
   included: CalcTranslateXMs reads mcViews[v] for both. */
static int calc_translate_rect(XfViews *V, int from, int to, int nx, double dx, int ny, double dy, const double vec[3], Xm **out) {
    if (nx < 1) nx = 1;
    if (ny < 1) ny = 1;
    const double *F = view_m(V, from), *T = view_m(V, to);
    if (!F || !T) return -1;
    Xm *x = calloc((size_t)nx * ny, sizeof *x); int n = 0;
    double ft[9] = { F[0], F[3], F[6], F[1], F[4], F[7], F[2], F[5], F[8] }, M[9];
    mmul(T, ft, M);                                               /* operator*(T, F^T) = F^T . T */
    int addv = sqrt(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]) > TOL;
    double g0[3] = { 0, 0, 0 }, p0[3];
    for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++) {
        double g[3] = { i * dx, j * dy, 0 }, pw[3];
        if (n == 0) { memcpy(g0, g, sizeof g); }
        view_to_world(V, g0, from, p0); view_to_world(V, g, from, pw);
        memcpy(x[n].m, M, sizeof M);
        for (int c = 0; c < 3; c++) x[n].t[c] = pw[c] - (p0[0] * M[c] + p0[1] * M[3 + c] + p0[2] * M[6 + c]) + (addv ? vec[c] : 0);
        n++;
    }
    *out = x;
    return n;
}

/* ------------------------------------------------------------------------------------------------- planes (1014 ..) */
static void cross(const double *a, const double *b, double *c) { c[0] = a[1] * b[2] - a[2] * b[1]; c[1] = a[2] * b[0] - a[0] * b[2]; c[2] = a[0] * b[1] - a[1] * b[0]; }
/* CBnciXfm::AdjustNciMatrixAndOrigin FUN_18013f3b0 [fk_FUN_18013f3b0 / core_range.c] with Plane::Transform @mastercam
   1803df750 [mc_view.c]: the plane (MATRIX rows, ORIGIN in view coordinates of *VIEW) moved by X; INCLUDE = the origin moves
   too. MATRIX / ORIGIN / *VIEW are updated. */
static void adjust_plane(XfViews *V, const Xm *x, double *matrix, double origin[3], int include, int16_t *view, XfStats *st) {
    int v = find_view_m(V, matrix, st);
    double w[3]; view_to_world(V, origin, v, w);
    int ident = 1; static const double I[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    for (int k = 0; k < 9; k++) if (x->m[k] != I[k]) ident = 0;      /* Matrix33::operator== (IsEqual tol 0: the Matrix33 tol, ND) */
    if (ident && sqrt(x->t[0] * x->t[0] + x->t[1] * x->t[1] + x->t[2] * x->t[2]) < 2e-7) {
        nd(st, "Plane::Transform identity test uses Matrix33::GetTol (taken as 2e-7)");
    } else {
        double n[9]; mmul(x->m, matrix, n);                          /* MultiplyMatrix33(xm, plane) = plane . xm [cc_mm.txt] */
        /* Matrix33::OrthoNormalize(2): an exact rotation keeps the rows orthonormal - not ported (ND), counted when the rows are not */
        for (int i = 0; i < 3; i++) { double l = n[3 * i] * n[3 * i] + n[3 * i + 1] * n[3 * i + 1] + n[3 * i + 2] * n[3 * i + 2];
            if (fabs(l - 1) > 1e-12) nd(st, "Matrix33::OrthoNormalize on a non-orthonormal product"); }
        double c[3]; cross(n, n + 3, c);
        if (c[0] * n[6] + c[1] * n[7] + c[2] * n[8] <= 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }   /* IsLeftHand: row 0 negated */
        memcpy(matrix, n, sizeof n);
        if (include) xform_pt(w, x);
        v = find_view_m(V, matrix, st);                              /* PlaneBase::GetViewNo -> FindViewNo (create) */
    }
    world_to_view(V, w, v, origin);
    *view = (int16_t)v;
}

/* --------------------------------------------------------------------------------------------- the output records */
typedef struct { uint8_t *b; size_t n, cap; long base; long nrec; } Out;
static size_t rec_bytes(int g, const uint16_t *src_sz) {   /* bnci_bytes @18003a800 (nci_write.c nciw_bytes) for the records made here */
    switch (g) {
    case -2: return 0x2a; case -3: case 999: return 0x22; case 0x3f0: return 0x93; case 0x407: return 0x6a; case 0x408: return 0x42;
    case 0x3fa: return 0xc2; case 0x3fb: return 0x4a;
    }
    return src_sz ? (size_t)*src_sz + 2 : 0;
}
/* bnci_write_to_disk: +0x10 = the file position, +0x14 = bnci_bytes, +4 = cur_op when 0; stored: u16 (bytes - 2) + record. Returns
   the record's byte offset in the section. */
static size_t put(Out *o, uint8_t *r, size_t bytes, int cur_op) {
    if (!gi(r, 4)) si(r, 4, cur_op);
    size_t at = o->n;
    si(r, 0x10, (int32_t)(o->base + (long)at)); si(r, 0x14, (int32_t)bytes);
    if (o->n + bytes > o->cap) { o->cap = (o->n + bytes) * 2 + 4096; o->b = realloc(o->b, o->cap); }
    o->b[o->n] = (uint8_t)(bytes - 2); o->b[o->n + 1] = (uint8_t)((bytes - 2) >> 8);
    size_t m = bytes - 2; memcpy(o->b + o->n + 2, r, m < XF_RS ? m : XF_RS); if (m > XF_RS) memset(o->b + o->n + 2 + XF_RS, 0, m - XF_RS);
    o->n += bytes; o->nrec++;
    return at;
}
static void reput(Out *o, size_t at, const uint8_t *r) {   /* fseek(saved +0x10) + rewrite (d300 LAB_18014d60a) */
    size_t m = (size_t)(o->b[at] | o->b[at + 1] << 8);
    memcpy(o->b + at + 2, r, m < XF_RS ? m : XF_RS);
}

/* FUN_180132ca0 [helpers.c] for the mill product (record +0xc = 0): the point fields of a record, -1 = no points / not ported */
static int rec_points(const uint8_t *r, int off[3]) {
    switch (GC(r)) {
    case 0: case 1: case 0x14: case 0x15: case 0x1e: case 0x1f: off[0] = 0x20; return 1;
    case 0xb: off[0] = 0x18; off[1] = 0x30; off[2] = 0x58; return 3;
    case 0x51: off[0] = 0x20; off[1] = 0x88; return 2;
    case 0x53: off[0] = 0x18; off[1] = 0x30; return 2;
    case 100: off[0] = 0x20; off[1] = 0x58; return 2;
    case 0x3ea: off[0] = 0x50; return 1;
    }
    return 0;
}

/* ----------------------------------------------------------------------------------------------------- the engine */
typedef struct { int src; int inst; int number; } Run;

int xfop_section(const uint8_t *xop, uint32_t xopn, const XfSrc *src, int nsrc, XfViews *V, const XfOpts *o,
                 uint8_t **out, size_t *outn, XfStats *st, char *err, size_t errn) {
    XfStats dummy; if (!st) st = &dummy;
    memset(st, 0, sizeof *st); err[0] = 0; *out = NULL; *outn = 0;
    int xid = oi(xop, xopn, 0), xcode = oh(xop, xopn, 4);
    XfPrm p; xfop_prm(xop, xopn, &p);
#define REFUSE(...) do { snprintf(err, errn, __VA_ARGS__); goto fail; } while (0)
    Xm **inst = calloc((size_t)(nsrc > 0 ? nsrc : 1), sizeof *inst); int *ninst = calloc((size_t)(nsrc > 0 ? nsrc : 1), sizeof *ninst);
    Run *runs = NULL; Out O = { 0 }; O.base = o ? o->base : 0;
    if (xcode != 4) REFUSE("opcode %d: only the mill Transform (4) is ported (0x50 wire / 0x6c nesting / 0x9a: NOT PORTED)", xcode);
    if (nsrc < 1) REFUSE("Transform op %d has no source operations", xid);
    /* BnciXform FUN_18013fb40 / FUN_1801482e0 [fk_FUN_1801482e0.c]: a604 = new ops (+0x67) -> FUN_1801464b0, a600 = +0x68 */
    if (p.new_ops) REFUSE("\"Create new operations and geometry\" (prm +0x67, FUN_1801464b0): NOT PORTED");
    if (p.coord_method) REFUSE("Method Coordinate (prm +0x68, every record through TransformBnciLineByCoordinate FUN_18014a300): NOT PORTED");
    if (p.source_nci) REFUSE("Source Geometry (prm +0x51, FUN_18014a0a0): NOT PORTED");
    if (p.remove_comments) REFUSE("Remove comments (prm +0x50: the gcode list removed is NOT DECODED)");
    if (p.type != 0xd && p.type != 0x10) REFUSE("Type %s (prm +0x00 = %d): NOT PORTED - Rotate and Translate Rectangular are",
                              p.type == 8 ? "Mirror" : "?", p.type);
    if (p.type == 0xd && p.method_sub != 0xe && p.method_sub != 0xf) REFUSE("Rotate method_sub %d (prm +0x70): NOT DECODED", p.method_sub);
    if (p.type == 0xd && p.rot_translate) REFUSE("Rotate method Translate (prm +0x102, CalcRotateT + CalcBnciExtents): NOT PORTED");
    if (p.type == 0x10 && p.method_sub != 0x11)
        REFUSE("Translate %s (prm +0x70 = %#x): NOT PORTED - only Rectangular is", p.method_sub == 0x12 ? "Delta / polar" : p.method_sub == 0x13 ? "Between points" : p.method_sub == 0x14 ? "Between planes" : "?", p.method_sub);
    if (p.type == 0x10 && ob(xop, xopn, PRM + 0x111)) REFUSE("Translate From plane (prm +0x111: Init's from view + ctx+0x5678 plane in the setup): NOT PORTED");
    if (p.type == 0x10 && ob(xop, xopn, PRM + 0xc8)) REFUSE("Translate sorted grid (prm +0xc8, CBnciXfmCalcXm::SortGridPts): NOT PORTED");
    /* FUN_1801470f0 [fk_FUN_1801470f0.c]: work offset modes >= 2 (Assign new / match planes) */
    int womode = p.wo_mode; if (womode == 2 && p.match_wo) womode = 3; else if (womode == 3 && !p.match_wo) womode = 2;   /* GetNumberingMode */
    if (womode < 0 || womode > 3) womode = 0;
    if (womode >= 2) REFUSE("work offset numbering Assign new (prm +0x54 = %d, WorkOffsetUtil): NOT PORTED", p.wo_mode);
    /* subpgm_on = op_is_subpgm(xform op) (subpgm_set @1800a50b0; nci_reverse.c op_is_subpgm case 4) */
    int subpgm_on = p.subprogram && !p.new_ops && (p.type != 8 || ob(xop, xopn, PRM + 0x111));

    /* setup FUN_1801422b0 / CBnciXfmCalcTransformOperationXM::Init @180152a80 / FUN_180140f50: per source op its instances */
    for (int s = 0; s < nsrc; s++) {
        const XfSrc *S = &src[s]; int sc = oh(S->op, S->opn, 4);
        if (sc == 4 || sc == 0x50 || sc == 0x6c || sc == 0x9a) REFUSE("source op %d is itself a transform (FUN_180147760 / FUN_180147e50): NOT PORTED", S->id);
        if (sc == 0x11 || sc == 0x45 || sc == 0x4d) REFUSE("source op %d is a manual entry (its own path in the setup): NOT PORTED", S->id);
        int n1014 = 0, i1014 = -1;
        for (int i = 0; i < S->nrec; i++) {
            int g = GC(S->recs + (size_t)i * XF_RS);
            if (g == 0x3fa || g == 0x3fb) REFUSE("source op %d has subprogram records (1018 / 1019): NOT PORTED", S->id);
            if (g == 2 || g == 3) { if (!p.include_origin) REFUSE("source op %d has arcs and Include origin is off (arcs in TransformBnciLineByCoordinate: constr_ns_arc / xform_ent): NOT PORTED", S->id); }
            if (g == 0x3f6) { n1014++; if (i1014 < 0) i1014 = i; }
        }
        if (n1014 != 1) REFUSE("source op %d has %d tool planes (1014): a section per plane set (FUN_1801422b0) - only one is ported", S->id, n1014);
        /* the source's view: op +0x572 when !op_has_no_view && op[0x570], else find_view_m of its first 1014 [Init l.l_15f0] */
        int vs1014 = find_view_m(V, (double *)(void *)(S->recs + (size_t)i1014 * XF_RS + 0x18), st), vs = vs1014;
        if (ob(S->op, S->opn, 0x570)) {
            vs = oh(S->op, S->opn, 0x572);
            if (vs != vs1014) REFUSE("source op %d: its tool plane view %d (op +0x572) differs from its 1014's (%d) - op_is_solid_ctrl is NOT DECODED", S->id, vs, vs1014);
        }
        int vr = vs;
        if (p.type == 0xd && p.plane_flag) vr = find_view_m(V, p.plane_m, st);
        if (p.type == 0x10 && p.plane_flag) { double tm[9]; for (int k = 0; k < 9; k++) tm[k] = od(xop, xopn, PRM + 0x180 + 8 * k); vr = find_view_m(V, tm, st); }
        if (vs < 1 || vs > V->nmax || vr < 1 || vr > V->nmax) REFUSE("view %d / %d not valid (ViewNumberOK)", vs, vr);
        if (p.type == 0xd) {
            double centre[3] = { 0, 0, 0 };
            if (p.method_sub == 0xf) memcpy(centre, p.pt, sizeof centre);
            double step = p.angle_step; if (p.total_sweep == 1) step /= (double)p.count;
            ninst[s] = calc_rotate_r(V, p.count, step, p.angle_start, vr, centre, &inst[s]);
            if (ninst[s] < 1) REFUSE("Rotate: # of instances %d", p.count);
        } else {
            /* Init 0x11: vec = prm +0xa0 - prm +0x88 (VectorSubtract3D @180154c40), dx / dy = +0x78 / +0x80 (/ # with +0xc9) */
            int nx = p.count, ny = p.count_y; double dx = od(xop, xopn, PRM + 0x78), dy = od(xop, xopn, PRM + 0x80);
            if (ob(xop, xopn, PRM + 0xc9) == 1) { dx /= (double)nx; dy /= (double)ny; }
            double vec[3]; for (int k = 0; k < 3; k++) vec[k] = od(xop, xopn, PRM + 0xa0 + 8 * k) - od(xop, xopn, PRM + 0x88 + 8 * k);
            ninst[s] = calc_translate_rect(V, vs, vr, nx, dx, ny, dy, vec, &inst[s]);
            if (ninst[s] < 1) REFUSE("Translate: no instances");
        }
        for (int k = 0; k < ninst[s]; k++) make_reals_0(inst[s][k].m, 12);   /* FUN_18013f150: make_reals_0(12 doubles) */
        /* Init: the identity is kept (FUN_180140f50 sets op[0x1223] = 1 on its copy, local_1795) */
    }
    /* BuildRunList FUN_180141400: a606 = prm +0x64; numbers from 1 per section */
    int nruns = 0, maxi = 0; for (int s = 0; s < nsrc; s++) { nruns += ninst[s]; if (ninst[s] > maxi) maxi = ninst[s]; }
    runs = calloc((size_t)nruns, sizeof *runs); int nr = 0;
    if (p.group_by_op) { for (int s = 0; s < nsrc; s++) for (int k = 0; k < ninst[s]; k++) runs[nr++] = (Run){ s, k, k + 1 }; }
    else for (int k = 0; k < maxi; k++) for (int s = 0; s < nsrc; s++) if (k < ninst[s]) runs[nr++] = (Run){ s, k, k + 1 };
    int keep_ident = p.copy_src;          /* run +0x1e4 = a602 >= 0 = prm +0x63 */

    /* the -2 record: bnci_wr_sos @180097170 (nci_writer.md section 3) */
    uint8_t R[XF_RS], W[XF_RS];
    memset(R, 0, sizeof R); sh(R, 0, -2); si(R, 4, xid); si(R, 0x18, 1); si(R, 0x1c, xid); si(R, 0x20, xcode);
    nd(st, "-2 +0x24 / -3 +0x1c time stamps (_time32) written 0");
    put(&O, R, rec_bytes(-2, NULL), xid);

    /* the 1018 global (subpgm_n1018, set_n1018(0,0,0) = zeroes) and the per-section constants (CreateBnci l.70-100) */
    int16_t s1e, a5664v;
    {
        int a5664 = p.type == 0xd ? 1 : (p.plane_flag ? 1 : 3);   /* FUN_1801482e0: Rotate 1; Translate 0x11: 3, 1 with +0x72 */
        s1e = 0;                          /* the global's +0x1e as left by set_n1018 */
        if (a5664 != 0 && p.coord_method != 1) { if (p.incremental && !p.coord_method) s1e = 1; }
        else if (p.coord_method == 1) s1e = 2;
        a5664v = (int16_t)a5664;
    }
    for (int ri = 0; ri < nr; ri++) {
        const Run *ru = &runs[ri]; const XfSrc *S = &src[ru->src]; const Xm *x = &inst[ru->src][ru->inst];
        if (ru->number == 1 && !keep_ident) continue;          /* CreateBnci: run number 1 skipped unless run +0x1e4 */
        st->runs++;
        int scode = oh(S->op, S->opn, 4);
        /* 999: +0x18 source op, +0x1c its opcode, +0x1e op[0x11b8] (FUN_18013f8c0) */
        memset(R, 0, sizeof R); sh(R, 0, 999); si(R, 4, xid); si(R, 0x18, S->id); sh(R, 0x1c, (int16_t)scode); R[0x1e] = (uint8_t)ob(S->op, S->opn, 0x11b8);
        put(&O, R, rec_bytes(999, NULL), xid);
        /* FUN_180148970: saved records cleared per run */
        uint8_t sv13[XF_RS], sv14[XF_RS], sv16[XF_RS], sv17[XF_RS]; size_t at13 = 0, at16 = 0; int has13 = 0, has16 = 0;
        memset(sv13, 0, XF_RS); memset(sv14, 0, XF_RS); memset(sv16, 0, XF_RS); memset(sv17, 0, XF_RS);
        uint8_t src13[XF_RS]; memset(src13, 0, XF_RS);            /* ctx+0xa208: the source's 1013 (original) */
        int bVar47 = !(o && o->output_op_comment) || p.remove_comments, wrote1018 = 0;
        for (int i = 0; i < S->nrec; i++) {
            const uint8_t *sr = S->recs + (size_t)i * XF_RS; int g = GC(sr);
            if (g == 0x38f || g == 0x390) continue;             /* RemoveGcodeLines {0x38f, 0x390} */
            memcpy(R, sr, XF_RS); si(R, 4, xid);
            size_t bytes = rec_bytes(g, &S->rsz[i]);
            if (g == 0x3fc && !bVar47) {                        /* the op comment 1008 before the 1020 */
                const char *c = (const char *)S->op + 6; size_t cl = strnlen(c, 0x78);
                if (cl) { memset(W, 0, sizeof W); sh(W, 0, 0x3f0); memcpy(W + 0x18, c, cl); put(&O, W, rec_bytes(0x3f0, NULL), xid); }
                bVar47 = 1;
            }
            if (g == 0x3f5) memcpy(src13, sr, XF_RS);
            if (g == 0x3f8) { si(R, 0x18, xid); si(R, (R[0xc] & 2) ? 0x68 : 0x78, S->id); }   /* CreateBnci l.664-683 */
            /* TransformBnciLineByToolplane FUN_18014d300 */
            switch (g) {
            case 0x3f6: {                                       /* 1014: Tplane; then the saved 1013 rewritten */
                int inc = p.include_origin && GC(sv13) == 0x3f5;
                double org[3] = { gd(sv13, 0x38), gd(sv13, 0x40), gd(sv13, 0x48) }; int16_t v = gh(sv13, 0x32);
                double m[9]; memcpy(m, R + 0x18, sizeof m);
                adjust_plane(V, x, m, org, inc, &v, st);
                memcpy(R + 0x18, m, sizeof m);
                sh(sv13, 0x32, v); sd(sv13, 0x38, org[0]); sd(sv13, 0x40, org[1]); sd(sv13, 0x48, org[2]);
                if (has13) reput(&O, at13, sv13);
                break; }
            case 0x3f9: {                                       /* 1017: Cplane; then the saved 1016 rewritten */
                int inc = p.include_origin && GC(sv16) == 0x3f8;
                double org[3] = { gd(sv16, 0x28), gd(sv16, 0x30), gd(sv16, 0x38) }; int16_t v = gh(sv16, 0x20);
                double m[9]; memcpy(m, R + 0x18, sizeof m);
                adjust_plane(V, x, m, org, inc, &v, st);
                memcpy(R + 0x18, m, sizeof m);
                sh(sv16, 0x20, v); sd(sv16, 0x28, org[0]); sd(sv16, 0x30, org[1]); sd(sv16, 0x38, org[2]);
                if (has16) reput(&O, at16, sv16);
                break; }
            case 0x403:                                         /* 1027: WCS, only with Include WCS (ctx+0x4168 = prm +0x2b0) */
                if (p.include_wcs) {
                    double org[3] = { gd(R, 0x60), gd(R, 0x68), gd(R, 0x70) }; int16_t v = 0;
                    double m[9]; memcpy(m, R + 0x18, sizeof m);
                    adjust_plane(V, x, m, org, p.include_origin, &v, st);
                    memcpy(R + 0x18, m, sizeof m); sd(R, 0x60, org[0]); sd(R, 0x68, org[1]); sd(R, 0x70, org[2]);
                }
                if (p.save_planes) nd(st, "Save planes (prm +0x66, FUN_180146a00): the planes it adds to the part are not made here");
                break;
            case 0x3f8: {                                       /* 1016: work offsets FUN_1801470f0 */
                int16_t w = gh(R, 0x42); if (w < -1) w = -1; sh(R, 0x42, w);
                if (womode == 0) sh(R, 0x42, -1);
                break; }
            case 0x3f5: case 0x3fc: case 0x407: case 0x408:
                break;
            case 0x53: REFUSE("source op %d: gcode 83 (view_to_view in TransformBnciLineByToolplane): NOT PORTED", S->id);
            default:
                if (!p.include_origin) {                        /* TransformBnciLineByCoordinate FUN_18014a300 (non-arc points) */
                    if (g == 2 || g == 3 || g == 0x16 || g == 0x17 || g == 0x20 || g == 0x21 || g == 0xc9 || g == 0x3f7)
                        REFUSE("source op %d: gcode %d through TransformBnciLineByCoordinate: NOT PORTED", S->id, g);
                    if (g == 0x3eb || g == 0x3e8 || g == 0x3e9 || (g >= 0x3f0 && g < 0x3fc) || g >= 20000 || g < 0) break;
                    int off[3]; int np = rec_points(R, off);
                    if (np > 0) {
                        if (R[0xc] != 0) REFUSE("record product byte %d (lathe / wire point layout): NOT PORTED", R[0xc]);
                        int v0 = gh(src13, 0x32); if (v0 == 0) v0 = 1;
                        double o0[3] = { gd(src13, 0x38), gd(src13, 0x40), gd(src13, 0x48) }, o0w[3];
                        view_to_world(V, o0, v0, o0w);
                        int v1 = gh(sv13, 0x32);
                        double o1[3] = { gd(sv13, 0x38), gd(sv13, 0x40), gd(sv13, 0x48) }, o1w[3];
                        view_to_world(V, o1, v1, o1w);                 /* ctx+0xa5a8 (d300 LAB_18014d6ce) */
                        double pts[3][3];
                        for (int k = 0; k < np; k++) {                 /* FUN_180128890 BnciWorldPoints on the record as read */
                            double q[3] = { gd(sr, off[k]), gd(sr, off[k] + 8), gd(sr, off[k] + 16) };
                            view_to_world(V, q, v0, pts[k]);
                            for (int j = 0; j < 3; j++) pts[k][j] += o0w[j];
                        }
                        for (int k = 0; k < np; k++) {
                            double q[3] = { pts[k][0], pts[k][1], pts[k][2] }, l[3];
                            xform_pt(q, x);
                            for (int j = 0; j < 3; j++) q[j] -= o1w[j];
                            world_to_view(V, q, v1, l);
                            sd(R, off[k], l[0]); sd(R, off[k] + 8, l[1]); sd(R, off[k] + 16, l[2]);
                        }
                    }
                }
                break;
            }
            size_t at = put(&O, R, bytes, xid);
            if (g == 0x3f5) { memcpy(sv13, R, XF_RS); at13 = at; has13 = 1; }
            else if (g == 0x3f6) memcpy(sv14, R, XF_RS);
            else if (g == 0x3f8) { memcpy(sv16, R, XF_RS); at16 = at; has16 = 1; }
            else if (g == 0x3f9) memcpy(sv17, R, XF_RS);
            if (g == 0x3fc) {                                   /* CreateBnci l.745-789: 1031 / 1032, then 1018 */
                memset(W, 0, sizeof W); sh(W, 0, 0x407); si(W, 4, xid);
                if (p.custom) memcpy(W + 0x18, xop + PRM + 0x30, 0x20);                 /* 4 doubles prm +0x30 .. */
                put(&O, W, rec_bytes(0x407, NULL), xid);
                memset(W, 0, sizeof W); sh(W, 0, 0x408); si(W, 4, xid);
                if (p.custom) memcpy(W + 0x18, xop + PRM + 0x1c, 0x10);                 /* 4 ints prm +0x1c .. */
                put(&O, W, rec_bytes(0x408, NULL), xid);
                if (!wrote1018) {                               /* bnci_wr_1018 @18008f440 */
                    memset(W, 0, sizeof W); sh(W, 0, 0x3fa); si(W, 4, xid); si(W, 8, xid);
                    si(W, 0x1c, xid); si(W, 0x20, S->id); sh(W, 0x28, (int16_t)ru->number);
                    if (!subpgm_on) sh(W, 0x2a, 2);
                    sh(W, 0x30, (int16_t)(keep_ident != 0)); W[0x32] = (uint8_t)(p.incremental != 0); sh(W, 0x34, a5664v); sh(W, 0x36, s1e);
                    for (int k = 0; k < 9; k++) sd(W, 0x38 + 8 * k, x->m[k]);
                    for (int k = 0; k < 3; k++) sd(W, 0x80 + 8 * k, x->t[k]);   /* 0x5664 != 0: the instance's translation */
                    put(&O, W, rec_bytes(0x3fa, NULL), xid);
                    wrote1018 = 1;
                }
            }
        }
        if (wrote1018) {                                        /* bnci_wr_1019 @18008f590 */
            memset(W, 0, sizeof W); sh(W, 0, 0x3fb); si(W, 4, xid); si(W, 8, xid);
            si(W, 0x1c, xid); si(W, 0x20, S->id); sh(W, 0x28, (int16_t)ru->number); if (!subpgm_on) sh(W, 0x2a, 2);
            put(&O, W, rec_bytes(0x3fb, NULL), xid);
        }
    }
    memset(R, 0, sizeof R); sh(R, 0, -3); si(R, 4, xid); si(R, 0x18, xid);    /* bnci_wr_eos @180092820 */
    put(&O, R, rec_bytes(-3, NULL), xid);
    st->records = O.nrec;
    for (int s = 0; s < nsrc; s++) free(inst[s]);
    free(inst); free(ninst); free(runs);
    *out = O.b; *outn = O.n;
    return 1;
fail:
    for (int s = 0; s < nsrc; s++) free(inst[s]);
    free(inst); free(ninst); free(runs); free(O.b);
    return 0;
#undef REFUSE
}

/* ----------------------------------------------------------------------------------------------- the part wrapper */
static int read_section(const uint8_t *d, size_t len, size_t off, int id, XfSrc *S) {
    int in = 0, cap = 0;
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || off + 2 + sz > len) break;
        const uint8_t *r = d + off + 2; int16_t g = gh(r, 0);
        if (g == -2 && sz >= 0x20) in = gi(r, 0x1c) == id;
        else if (in && g == -3) return 1;
        else if (in) {
            if (S->nrec == cap) { cap = cap ? cap * 2 : 1024; S->recs = realloc(S->recs, (size_t)cap * XF_RS); S->rsz = realloc(S->rsz, (size_t)cap * 2); }
            memset(S->recs + (size_t)S->nrec * XF_RS, 0, XF_RS); memcpy(S->recs + (size_t)S->nrec * XF_RS, r, sz < XF_RS ? sz : XF_RS);
            S->rsz[S->nrec++] = sz;
        }
        off += 2 + sz;
    }
    return 0;
}
int xfop_from_part(McamFile *f, int index, const XfOpts *o, uint8_t **out, size_t *outn, XfStats *st, char *err, size_t errn) {
    *out = NULL; *outn = 0;
    if (index < 0 || index >= f->nops || !f->ops[index].raw) { snprintf(err, errn, "no such stored operation"); return 0; }
    if (f->major != 0x12 && f->major != 0x13) { snprintf(err, errn, "file version %#x: only X9 / 2017 parts (the 2018 record layout is read by the test only)", f->major); return 0; }
    Op *X = &f->ops[index];
    /* sources: TP_ID kind 23 of the op, +0x98 = source op id (2017; ent +0x90 == 0x17, FUN_180140bd0), Operations Manager order */
    int ns = 0; XfSrc *S = calloc((size_t)f->ntp + 1, sizeof *S);
    for (int i = 0; i < f->ntp; i++) if (f->tp[i].op_id == X->id && f->tp[i].kind == 23) {
        int sid = f->tp[i].chain;
        for (int j = 0; j < f->nops; j++) if (f->ops[j].id == sid && f->ops[j].raw) { S[ns].id = sid; S[ns].op = f->ops[j].raw + 0x68; S[ns].opn = f->ops[j].raw_n - 0x68; ns++; }
    }
    for (int a = 1; a < ns; a++) for (int b = a; b > 0; b--) {        /* SortIDsByOpMgrTreeOrder */
        int sa = 0, sb = 0; for (int j = 0; j < f->nops; j++) { if (f->ops[j].id == S[b].id) sa = f->ops[j].seq; if (f->ops[j].id == S[b - 1].id) sb = f->ops[j].seq; }
        if (sa < sb) { XfSrc t = S[b]; S[b] = S[b - 1]; S[b - 1] = t; }
    }
    uint8_t *d = NULL; size_t len = 0; int ok = 0;
    FILE *in = fopen(f->path, "rb");
    if (in) { fseek(in, 0, SEEK_END); long L = ftell(in); fseek(in, 0, SEEK_SET); if (L > 0) { len = (size_t)L; d = malloc(len); if (fread(d, 1, len, in) != len) { free(d); d = NULL; } } fclose(in); }
    if (!d || !f->nci_off) { snprintf(err, errn, "the part has no stored NCI"); goto done; }
    for (int s = 0; s < ns; s++) if (!read_section(d, len, f->nci_off, S[s].id, &S[s])) { snprintf(err, errn, "source op %d has no NCI in the part", S[s].id); goto done; }
    XfViews V; xf_views_init(&V, f);
    ok = xfop_section(X->raw + 0x68, X->raw_n - 0x68, S, ns, &V, o, out, outn, st, err, errn);
    xf_views_free(&V);
done:
    for (int s = 0; s < ns; s++) { free(S[s].recs); free(S[s].rsz); }
    free(S); free(d);
    return ok;
}
