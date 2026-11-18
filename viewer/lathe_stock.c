/* lathe_stock.c - Mastercam 2017 lathe stock boundaries for the Lathe Face "from stock" geometry (../formats/lathe.md 7.2 / 7.3).
 * Sources: ~/mastercam-re/work/lathe/mdef/mdef_all.c (mcmachinedef.dll BOUNDARYCLASS, DumpDecomp of ~/mastercam-re/ghidra_mdef) and
 * work/lathe/dump/mclathe_all.c (mclathe.dll LATHEPROJECTCLASS); every rule names its function.
 * NOT TRACED (mccore geometry helpers, taken as the exact geometric result within tol; lathe.md 7.3 "assumed"): ln_ln_ints2 /
 * line_arc_ints / AddIntPt (BOUNDARYCLASS::IntersectLine 0x180037730 -> IntersectLineBdry 0x180045e80) and break_arc_3d (BdryEntMaxYPt
 * 0x180040110 / BdryEntMinX 0x1800402c0: an arc's extreme points = its ends and the quadrant points inside its sweep). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LATHE_STOCK_MCAM
#include "lathe_stock.h"

static double rdd(const uint8_t *p) { double v; memcpy(&v, p, 8); return v; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

void ls_free(LsBdry *b) { free(b->e); memset(b, 0, sizeof *b); }

/* an arc's points for the extents: start, the quadrant points (multiples of pi/2) strictly inside the sweep, end (break_arc_3d) */
static int arc_pts(const LsEnt *e, double (*q)[2]) {
    int n = 0; q[n][0] = e->p1[0]; q[n][1] = e->p1[1]; n++;
    double a0 = e->sa, a1 = e->sa + e->sw, lo = fmin(a0, a1), hi = fmax(a0, a1);
    for (int k = (int)floor(lo / (M_PI / 2)) - 1; k <= (int)ceil(hi / (M_PI / 2)) + 1 && n < 6; k++) {
        double a = k * (M_PI / 2);
        if (a > lo + 1e-12 && a < hi - 1e-12) { q[n][0] = e->c[0] + e->r * cos(a); q[n][1] = e->c[1] + e->r * sin(a); n++; }
    }
    q[n][0] = e->p2[0]; q[n][1] = e->p2[1]; n++;
    return n;
}
/* BdryEntMaxYPt 0x180040110 / BdryEntMinYPt 0x180040750: lines - the end with the greater (lesser) y, the second end on a tie;
   arcs - the first of break_arc_3d's points with the strictly greatest (least) y */
static void ent_ypt(const LsEnt *e, int mx, double *x, double *y) {
    if (e->type == 2) {
        int second = mx ? e->p1[1] <= e->p2[1] : e->p2[1] <= e->p1[1];
        *x = second ? e->p2[0] : e->p1[0]; *y = second ? e->p2[1] : e->p1[1];
        return;
    }
    double q[8][2]; int n = arc_pts(e, q); *x = 0; *y = mx ? -1e30 : 1e30;
    for (int i = 0; i < n; i++) if (mx ? q[i][1] > *y : q[i][1] < *y) { *x = q[i][0]; *y = q[i][1]; }
}
/* SetExtents 0x18003b710: BdryEntMinX / MinY / MaxX / MaxY over the entities */
static void set_extents(LsBdry *b) {
    b->minx = b->miny = 1e30; b->maxx = b->maxy = -1e30;
    for (int i = 0; i < b->n; i++) {
        const LsEnt *e = &b->e[i]; double q[8][2]; int n;
        if (e->type == 2) { q[0][0] = e->p1[0]; q[0][1] = e->p1[1]; q[1][0] = e->p2[0]; q[1][1] = e->p2[1]; n = 2; } else n = arc_pts(e, q);
        for (int k = 0; k < n; k++) {
            if (q[k][0] < b->minx) b->minx = q[k][0];
            if (q[k][1] < b->miny) b->miny = q[k][1];
            if (q[k][0] > b->maxx) b->maxx = q[k][0];
            if (q[k][1] > b->maxy) b->maxy = q[k][1];
        }
    }
}

int ls_from_lbdry(const uint8_t *rec, uint32_t n, double tol, LsBdry *b, char *err, size_t errn) {
    memset(b, 0, sizeof *b);
    if (n < 0x90) { snprintf(err, errn, "lathe stock: the lathe_bdry record is too short"); return 0; }
    int ne = rd32(rec);
    if (ne <= 0 || 0x90 + (uint32_t)ne * 0x78 > n) { snprintf(err, errn, "lathe stock: the lathe_bdry record has %d entities for %u bytes", ne, n); return 0; }
    /* GetComponentsFromDb: BOUNDARYCLASS::Valid (lathe_bdry +5, GetFrom_lbdry) 0 -> the polygons are invalidated */
    if (!rec[5]) { snprintf(err, errn, "lathe stock: the stock boundary is marked invalid (lathe_bdry +5 = 0; BOUNDARYCLASS::Invalidate) - NOT PORTED"); return 0; }
    double s = 1.0 / 1000.0;                     /* Scale(.., 1 / MathScale): MathScale = BOUNDARYCLASS +0x18 = 1000 (InitTolerances) */
    b->e = calloc((size_t)ne, sizeof *b->e);
    for (int i = 0; i < ne; i++) {
        const uint8_t *t = rec + 0x90 + 0x78 * i; int ty = rd32(t);
        LsEnt e; memset(&e, 0, sizeof e); e.type = ty;
        if (ty != 2 && ty != 4) { ls_free(b); snprintf(err, errn, "lathe stock: boundary entity type %d (not a line / arc) is NOT DECODED", ty); return 0; }
        /* ScaleBdryEnt 0x180046c20: every coordinate (and an arc's radius) times the scale */
        e.p1[0] = s * rdd(t + 0x10); e.p1[1] = s * rdd(t + 0x18); e.p2[0] = s * rdd(t + 0x28); e.p2[1] = s * rdd(t + 0x30);
        if (fabs(rdd(t + 0x20)) > 1e-9 || fabs(rdd(t + 0x38)) > 1e-9) { ls_free(b); snprintf(err, errn, "lathe stock: a boundary entity off the z = 0 plane is NOT DECODED"); return 0; }
        if (ty == 2) {
            /* ScaleBdryEnt: a line whose scaled ends are equal within tol (p3Equal) is dropped (Scale: the next entity keeps its start) */
            if (fabs(e.p1[0] - e.p2[0]) <= tol && fabs(e.p1[1] - e.p2[1]) <= tol) continue;
        } else {
            e.c[0] = s * rdd(t + 0x40); e.c[1] = s * rdd(t + 0x48); e.r = s * rdd(t + 0x58); e.sa = rdd(t + 0x60); e.sw = rdd(t + 0x68);
            /* ScaleBdryEnt: an arc shorter than min_arc_length (mccore global, value NOT DECODED) is dropped and its neighbour re-fit
               (PtsTo2DArc); refused rather than guessed when the scaled arc is shorter than tol */
            if (fabs(e.sw) * e.r < tol) { ls_free(b); snprintf(err, errn, "lathe stock: a boundary arc of length %g (ScaleBdryEnt's min_arc_length rule) is NOT DECODED", fabs(e.sw) * e.r); return 0; }
        }
        b->e[b->n++] = e;
    }
    if (!b->n) { ls_free(b); snprintf(err, errn, "lathe stock: the boundary has no entity left"); return 0; }
    /* SetDirCW / StartBdryAtXYLimit / Mirror only reorder or copy the entities (to polygons 1, 0x1c ...): the extents and the y queries
       below do not depend on the order (min / max over sets) */
    set_extents(b);
    return 1;
}

int ls_ystatus(const LsBdry *b, double tol) {
    /* YStatus 0x18003d790: over the entities' min / max y (BdryEntMinY / MaxY), each value farther than tol from 0 votes its sign;
       opposite votes -> 0 */
    int st = 0;
    for (int i = 0; i < b->n; i++) {
        double x, v[2]; ent_ypt(&b->e[i], 0, &x, &v[0]); ent_ypt(&b->e[i], 1, &x, &v[1]);
        for (int k = 0; k < 2; k++) {
            if (fabs(v[k]) < fabs(tol)) continue;
            int sg = v[k] < 0 ? -1 : 1;
            if (st == -sg) return 0;
            st = sg;
        }
    }
    return st;
}

/* IntersectLine 0x180037730 (param_4 1 -> IntersectLineBdry per entity) with the vertical segment x = X from y = miny - 1 to
   maxy + 1 (GetMaxY's l_2d): the y of every intersection point (NOT TRACED: geometric, header comment) */
static int vline_ys(const LsBdry *b, double X, double tol, double *ys, int cap) {
    int n = 0; double y0 = b->miny - 1, y1 = b->maxy + 1;
    for (int i = 0; i < b->n && n + 2 <= cap; i++) {
        const LsEnt *e = &b->e[i];
        if (e->type == 2) {
            double dx = e->p2[0] - e->p1[0];
            if (fabs(dx) <= tol) {               /* parallel: overlapping when on the line (ln_ln_ints2 codes -1..-4: both overlap ends) */
                if (fabs(e->p1[0] - X) <= tol) { ys[n++] = e->p1[1]; ys[n++] = e->p2[1]; }
                continue;
            }
            double t = (X - e->p1[0]) / dx;
            if (t < -tol / fabs(dx) || t > 1 + tol / fabs(dx)) continue;
            if (t < 0) t = 0; if (t > 1) t = 1;
            double y = e->p1[1] + t * (e->p2[1] - e->p1[1]);
            if (y >= y0 - tol && y <= y1 + tol) ys[n++] = y;
        } else {
            double dx = X - e->c[0];
            if (fabs(dx) > e->r + tol) continue;
            double h2 = e->r * e->r - dx * dx, h = h2 > 0 ? sqrt(h2) : 0;
            for (int s = -1; s <= 1; s += 2) {
                double y = e->c[1] + s * h, a = atan2(y - e->c[1], X - e->c[0]);
                double lo = fmin(e->sa, e->sa + e->sw), hi = fmax(e->sa, e->sa + e->sw), at = tol / (e->r > 0 ? e->r : 1);
                int in = 0;
                for (int k = -2; k <= 2 && !in; k++) { double aa = a + 2 * M_PI * k; if (aa >= lo - at && aa <= hi + at) in = 1; }
                if (in) ys[n++] = y;
                if (h == 0) break;
            }
        }
    }
    return n;
}

static int get_y(const LsBdry *b, int mx, double x1, double x2, double lim, double tol, double *out) {
    double r = mx ? -1e30 : 1e30, ys[512];
    if (b->n < 1) { *out = r; return 0; }
    int one = fabs(x1 - x2) <= fabs(tol) && fabs(tol) != fabs(x1 - x2);   /* after the first line: uVar8 = 3, the entity pass skipped */
    for (int k = 0; k < (one ? 1 : 2); k++) {
        int n = vline_ys(b, k == 0 ? x1 : x2, tol, ys, 512);
        for (int i = 0; i < n; i++) if (mx ? (ys[i] > r && ys[i] < lim) : (ys[i] < r && ys[i] > lim)) r = ys[i];
    }
    if (!one)
        for (int i = 0; i < b->n; i++) {
            double x, y; ent_ypt(&b->e[i], mx, &x, &y);
            double t = fabs(tol);
            int in_lo = (fabs(x - x1) < t) || x1 <= x, in_hi = (fabs(x - x2) < t) || x <= x2;
            if (in_lo && in_hi && (mx ? (y > r && y < lim) : (y < r && y > lim))) r = y;
        }
    *out = r;
    return mx ? r != -1e30 : r != 1e30;
}
int ls_get_max_y(const LsBdry *b, double x1, double x2, double lim, double tol, double *out) { return get_y(b, 1, x1, x2, lim, tol, out); }
int ls_get_min_y(const LsBdry *b, double x1, double x2, double lim, double tol, double *out) { return get_y(b, 0, x1, x2, lim, tol, out); }

static const struct McLbdry *lbdry_by_id(const McamFile *f, int32_t idn) {
    for (int i = 0; i < f->nlbdry; i++) if (f->lbdry[i].idn == idn) return &f->lbdry[i];
    return NULL;
}
static int in_group(const McamFile *f, int32_t gid, int32_t root) {
    /* GetOpsInGroup(group, 1, ..) (mccore, not read): the group and its sub-groups (parent chain op_group +0x10c) */
    for (int depth = 0; depth < 64 && gid > 0; depth++) {
        if (gid == root) return 1;
        int32_t p = 0;
        for (int k = 0; k < f->nlgrp; k++) if (f->lgrp[k].id == gid) { p = f->lgrp[k].parent; break; }
        gid = p;
    }
    return 0;
}
static int cmp_seq(const void *a, const void *b) {
    const Op *x = *(const Op *const *)a, *y = *(const Op *const *)b;
    return x->seq != y->seq ? (x->seq < y->seq ? -1 : 1) : (x < y ? -1 : x > y);
}

int lathe_op_stock(const McamFile *f, int op_id, double tol, LsBdry *b, int *src, char *err, size_t errn) {
    memset(b, 0, sizeof *b); *src = 0;
    /* GetActiveLatheGroup: the active machine group (the last machine group, mcam_load.c stock_grp) when it is a lathe group */
    const struct McGrpLathe *g = NULL;
    for (int k = 0; k < f->nlgrp; k++) if (f->lgrp[k].id == f->stock_grp) g = &f->lgrp[k];
    if (!g) { snprintf(err, errn, "lathe stock: the part has no active machine group"); return 0; }
    if (g->gtype != 1) { snprintf(err, errn, "lathe stock: the active machine group is not a lathe group (op_group +0x19e8 = %d; mill-turn 6 NOT DECODED)", g->gtype); return 0; }
    /* GetPrvOpBdrysOpIdn 0x18010deb0: the group's ops from the last to the first; after the op itself (a new op: appended, so from the
       start) the first op with lathe boundaries (OpHasLatheBoundaries 0x18010f890: op +0xb40 / +0xb44) - stock model ops with lathe
       stock (opcode 0x8b, IsStockModelOpWithLatheStock) are NOT PORTED */
    const Op **list = malloc(sizeof *list * (size_t)(f->nops + 1)); int nl = 0;
    for (int i = 0; i < f->nops; i++) {
        const Op *o = &f->ops[i];
        if (!o->raw || o->raw_n < 0x68 + 0xb88) continue;
        if (in_group(f, rd32(o->raw + 0x68 + 0x28c), g->id)) list[nl++] = o;
    }
    qsort(list, (size_t)nl, sizeof *list, cmp_seq);
    int passed = op_id <= 0, from = 0x7fffffff; const uint8_t *opr = NULL;
    for (int i = nl - 1; i >= 0 && from == 0x7fffffff; i--) {
        const Op *o = list[i];
        if (o->id == op_id) { passed = 1; continue; }
        if (!passed) continue;
        if (o->opcode == 0x8b) { free(list); snprintf(err, errn, "lathe stock: a Stock Model op (%d) before the op: its lathe stock (IsStockModelOpWithLatheStock) is NOT PORTED", o->id); return 0; }
        const uint8_t *op = o->raw + 0x68;
        if (rd32(op + 0xb40) || rd32(op + 0xb44)) { from = o->id; opr = op; }
    }
    free(list);
    /* GetComponentsFromDb 0x18010d120: component 0 (the left stock) = op +0xb40 (an op's) / group +0x16ac (the group's) */
    int32_t id = opr ? rd32(opr + 0xb40) : g->comp[0];
    *src = from;
    if (!id) { snprintf(err, errn, "lathe stock: no left-spindle stock is defined (%s component 0 is empty)", opr ? "the previous op's" : "the machine group's"); return 0; }
    const struct McLbdry *r = lbdry_by_id(f, id);
    if (!r) { snprintf(err, errn, "lathe stock: the stock boundary record %d is missing (get_lbdry_ent fails)", id); return 0; }
    return ls_from_lbdry(r->rec, r->n, tol, b, err, errn);
}
