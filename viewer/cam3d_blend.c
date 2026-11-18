/* cam3d_blend.c - classic Surface Finish Blend (op 109). See cam3d_blend.h and formats/blend.md ("bl N.N"; "l.N" = decomp/mcmsurf_all.c,
 * "core" = mccore.dll break_chain_to_points & co (decomp/blend/loft_core.c), "mill" = mcmill.dll make_loft_surf (decomp/mcmill_all.c)). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cam3d_blend.h"
#include "mcsurf.h"

static double rdd(const uint8_t *r, uint32_t n, uint32_t o) { double v = 0; if (o + 8 <= n) memcpy(&v, r + o, 8); return v; }
static int rdb(const uint8_t *r, uint32_t n, uint32_t o) { return o < n ? r[o] : 0; }
static int rds(const uint8_t *r, uint32_t n, uint32_t o) { int16_t v = 0; if (o + 2 <= n) memcpy(&v, r + o, 2); return v; }

void blend_poly_free(BlendPoly *p) { free(p->p); p->p = NULL; p->n = 0; }
static void bp_push(BlendPoly *p, int *cap, double x, double y, double z) {
    if (p->n == *cap) { *cap = *cap ? *cap * 2 : 256; p->p = realloc(p->p, sizeof(double) * 3 * *cap); }
    p->p[3 * p->n] = x; p->p[3 * p->n + 1] = y; p->p[3 * p->n + 2] = z; p->n++;
}

/* ------------------------------------------------------------------------------------------------ record (bl 2) */
int blend_params_from_record(const uint8_t *r, uint32_t n, BlendParams *bp, char *why, size_t whyn) {
    memset(bp, 0, sizeof *bp);
    bp->stepover = rdd(r, n, 0x1420);
    if (!(bp->stepover > 1e-6)) bp->stepover = 0.05;            /* FUN_1800caff0 l.133470-133473 (lm +0x88 = 1e-6) */
    bp->frac = rdd(r, n, 0x1438);
    if (!(bp->frac > mc_systol.tol)) bp->frac = 1.0;            /* l.133458-133461 (tol_exref = the system tolerance, config 1000) */
    bp->method = rds(r, n, 0x1428);
    bp->along = rdb(r, n, 0x142a);
    bp->two_d = rdb(r, n, 0x142b);
    bp->quick = rdb(r, n, 0x1440);
    bp->tol = rdd(r, n, 0x1240);
    bp->tool_type = rds(r, n, 0x34c);
    bp->D = rdd(r, n, 0x350); bp->r = rdd(r, n, 0x358);
    bp->stock = rdd(r, n, 0x2e0);
    bp->tip = rdb(r, n, 0x1239);
    double pct = rdd(r, n, 0x1298);
    bp->clr_h = rdd(r, n, 0x170); bp->clr_inc = rdb(r, n, 0x178); bp->clr_on = rdb(r, n, 0x179);   /* op +0x108 / +0x110 / +0x111 (the record = op + 0x68) */
    bp->ret_h = rdd(r, n, 0x180); bp->ret_inc = rdb(r, n, 0x188); bp->ret_on = rdb(r, n, 0x189);   /* op +0x118 / +0x120 / +0x121 */
    bp->fp_h = rdd(r, n, 0x190); bp->fp_inc = rdb(r, n, 0x198); bp->rapid_ret = rdb(r, n, 0x1a9); bp->ret_feed = rdd(r, n, 0x388);   /* op +0x128 / +0x130 / +0x141 / +0x320 */
    bp->long_gapchk = rdb(r, n, 0x12a3);                                                               /* msurf_long_gapchk = op +0x123b, l.340029 */
    bp->max_gap = rdb(r, n, 0x1288) ? pct * bp->D : rdd(r, n, 0x1290);   /* msurf_map l.340021-340023, l.340443-340448 */
    int gt = rds(r, n, 0x12a0); bp->gap_type = (gt % 100) % 10;           /* l.340024-340026 */
    const char *w = NULL;
    if (bp->gap_type != 1) w = "short-gap motion other than Broken is NOT DECODED";
    else if (!(bp->D > 0) || fabs(bp->r - bp->D * 0.5) > 1e-9) w = "the projection substitute is checked with ball tools only";
    else if (!(bp->tol > 0)) w = "tolerance must be > 0";
    if (w) { snprintf(why, whyn, "Blend: %s", w); return 0; }
    return 1;
}

/* ------------------------------------------------------------------------------------------------ chains from a stored op */
static const Ent *ent_by_idn(const McamFile *f, int32_t idn) { for (int i = 0; i < f->nents; i++) if (f->ents[i].idn == idn && f->ents[i].type) return &f->ents[i]; return NULL; }
static const CurveRec *crv_by_idn(const McamFile *f, int32_t idn) {
    for (int i = 0; i < f->ncrvs; i++) { const CurveRec *c = &f->crvs[i]; int32_t v = 0; if (c->n >= 0x38) memcpy(&v, c->rec + 0x34, 4); if (v == idn) return c; }
    return NULL;
}
static void arc_pt(const Ent *e, double t, double q[3]) {
    if (e->vw >= 2) { for (int k = 0; k < 3; k++) q[k] = (&e->c.x)[k] + e->r * (cos(t) * e->vm[k] + sin(t) * e->vm[3 + k]); }
    else { q[0] = e->c.x + e->r * cos(t); q[1] = e->c.y + e->r * sin(t); q[2] = e->c.z; }
}
int blend_chain_from_tp(const McamFile *f, int op_id, int chain_no, double step, BlendPoly *out, char *err, size_t errn) {
    memset(out, 0, sizeof *out); int cap = 0;
    /* kind 7 references of the chain, in index order */
    int idx[4096], ni = 0;
    for (int i = 0; i < f->ntp && ni < 4096; i++) if (f->tp[i].op_id == op_id && f->tp[i].kind == 7 && f->tp[i].chain == chain_no) idx[ni++] = i;
    for (int a = 1; a < ni; a++) for (int b = a; b > 0 && f->tp[idx[b]].index < f->tp[idx[b - 1]].index; b--) { int t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t; }
    if (!ni) { snprintf(err, errn, "chain %d of op %d not found", chain_no, op_id); return 0; }
    for (int k = 0; k < ni; k++) {
        const TpRef *t = &f->tp[idx[k]]; int rev = t->rev == 1;
        double q[3]; int m; double (*pts)[3] = NULL;
        if (t->cls == 1 || t->cls == 2 || t->cls == 4) {
            const Ent *e = ent_by_idn(f, t->ent);
            if (!e) { snprintf(err, errn, "chain %d: entity %d not found", chain_no, t->ent); blend_poly_free(out); return 0; }
            if (e->type == E_POINT) { m = 1; pts = malloc(sizeof *pts); pts[0][0] = e->a.x; pts[0][1] = e->a.y; pts[0][2] = e->a.z; }
            else if (e->type == E_LINE) { m = 2; pts = malloc(2 * sizeof *pts); pts[0][0] = e->a.x; pts[0][1] = e->a.y; pts[0][2] = e->a.z; pts[1][0] = e->b.x; pts[1][1] = e->b.y; pts[1][2] = e->b.z; }
            else { int s = (int)ceil(fabs(e->sw) * e->r / step); if (s < 2) s = 2; m = s + 1; pts = malloc(m * sizeof *pts);
                for (int i = 0; i < m; i++) arc_pt(e, e->a0 + e->sw * i / s, pts[i]); }
        } else if (t->cls == 0x80 || t->cls == 0x8) {
            const CurveRec *c = crv_by_idn(f, t->ent);
            if (!c || c->n < 0xa8) { snprintf(err, errn, "chain %d: curve %d not found", chain_no, t->ent); blend_poly_free(out); return 0; }
            double t1 = rdd(c->rec, c->n, 0x98), t2 = rdd(c->rec, c->n, 0xa0);   /* curve_t1t2 (mcsurf.c) */
            /* sample: 64 pieces to get the length, then pieces of at most `step` */
            double L = 0, prev[3]; int s = 64;
            for (int i = 0; i <= s; i++) { if (mcs_curve_eval(f, c, t1 + (t2 - t1) * i / s, q)) { snprintf(err, errn, "chain %d: curve %d not evaluable", chain_no, t->ent); blend_poly_free(out); return 0; }
                if (i) L += sqrt((q[0] - prev[0]) * (q[0] - prev[0]) + (q[1] - prev[1]) * (q[1] - prev[1]) + (q[2] - prev[2]) * (q[2] - prev[2]));
                memcpy(prev, q, sizeof q); }
            s = (int)ceil(L / step) * 4; if (s < 64) s = 64; if (s > 200000) s = 200000;
            m = s + 1; pts = malloc(m * sizeof *pts);
            for (int i = 0; i < m; i++) mcs_curve_eval(f, c, t1 + (t2 - t1) * i / s, pts[i]);
        } else { snprintf(err, errn, "chain %d: entity class 0x%x NOT PORTED", chain_no, t->cls); blend_poly_free(out); return 0; }
        for (int i = 0; i < m; i++) { const double *p = pts[rev ? m - 1 - i : i];
            if (out->n && i == 0 && fabs(out->p[3 * out->n - 3] - p[0]) + fabs(out->p[3 * out->n - 2] - p[1]) + fabs(out->p[3 * out->n - 1] - p[2]) < 1e-9) continue;
            bp_push(out, &cap, p[0], p[1], p[2]); }
        free(pts);
    }
    return 1;
}

/* a chain of F.ents (cam2d.h Chain: ent indices, rev flags; points / lines / arcs) as a polyline, for an op made here */
int blend_chain_from_ents(const McamFile *f, const int *ent, const int *rev, int n, double step, BlendPoly *out, char *err, size_t errn) {
    memset(out, 0, sizeof *out); int cap = 0;
    for (int k = 0; k < n; k++) {
        if (ent[k] < 0 || ent[k] >= f->nents || !f->ents[ent[k]].type) { snprintf(err, errn, "chain entity %d missing", ent[k]); blend_poly_free(out); return 0; }
        const Ent *e = &f->ents[ent[k]]; int m; double (*pts)[3];
        if (e->type == E_POINT) { m = 1; pts = malloc(sizeof *pts); pts[0][0] = e->a.x; pts[0][1] = e->a.y; pts[0][2] = e->a.z; }
        else if (e->type == E_LINE) { m = 2; pts = malloc(2 * sizeof *pts); pts[0][0] = e->a.x; pts[0][1] = e->a.y; pts[0][2] = e->a.z; pts[1][0] = e->b.x; pts[1][1] = e->b.y; pts[1][2] = e->b.z; }
        else if (e->type != E_ARC) { snprintf(err, errn, "chain entity %d: a spline curve - not taken here (NOT PORTED)", ent[k]); blend_poly_free(out); return 0; }
        else { int sN = (int)ceil(fabs(e->sw) * e->r / step); if (sN < 2) sN = 2; m = sN + 1; pts = malloc(m * sizeof *pts);
            for (int i = 0; i < m; i++) arc_pt(e, e->a0 + e->sw * i / sN, pts[i]); }
        for (int i = 0; i < m; i++) { const double *p = pts[rev && rev[k] ? m - 1 - i : i];
            if (out->n && i == 0 && fabs(out->p[3 * out->n - 3] - p[0]) + fabs(out->p[3 * out->n - 2] - p[1]) + fabs(out->p[3 * out->n - 1] - p[2]) < 1e-9) continue;
            bp_push(out, &cap, p[0], p[1], p[2]); }
        free(pts);
    }
    if (!out->n) { snprintf(err, errn, "empty chain"); return 0; }
    return 1;
}

int blend_drive_from_tp(const McamFile *f, int op_id, double defl, double **tri, char *err, size_t errn) {
    *tri = NULL; int n = 0;
    /* the op's TP kind 4 records (formats/surface_selection.md 2.1 / 2.2): the drive ones (tp_ref_role: no check 0x4 / gouge 0x20) by the
       referenced entity's class +0x78. S7 (surface_selection.md 8): check surfaces change the Blend path (FUN_180268e10 = !DAT_1805034a1: Along +
       3D + Thorough only without them, blend.md 2; DAT_1805034a1 = a check count > 0, bnci_surface l.12202-12203) and their own use (the check
       model, FUN_180290fb0) is NOT DECODED: refused; gouge references likewise. A containment (role 2 chains: mcmsurf writes every chain of the
       op to tmptc.tmp unclosed, FUN_18012f750 l.200203-200209 / FUN_1800135a0) has no decoded use in the lm engine: refused. */
    for (int i = 0; i < f->ntp; i++) if (f->tp[i].op_id == op_id && f->tp[i].kind == 6 && f->tp[i].role == 2) {
        snprintf(err, errn, "containment: its use by Blend (tmptc.tmp in the lm engine) is NOT DECODED"); return -1; }
    for (int i = 0; i < f->ntp; i++) if (f->tp[i].op_id == op_id && f->tp[i].kind == 4) {
        int role = tp_ref_role(f->tp[i].flags);
        if (role == TP_REF_FLAG1) { snprintf(err, errn, "a reference with flag 0x1 without 0x2 (msurf_ent_count: not a drive; its meaning NOT DECODED)"); free(*tri); return -1; }
        if (role == TP_REF_CHECK) { snprintf(err, errn, "check surfaces: their use by Blend (FUN_180268e10's path, the check model) is NOT DECODED"); free(*tri); return -1; }
        if (role == TP_REF_GOUGE) { snprintf(err, errn, "a gouge reference (flag 0x20, multiaxis): its use by Blend is NOT DECODED"); free(*tri); return -1; }
        if (f->tp[i].ecls != TP_ECLS_SURF) { snprintf(err, errn, "a drive %s reference is NOT PORTED here", f->tp[i].ecls == TP_ECLS_SOLID ? "solid" : f->tp[i].ecls == TP_ECLS_MESH ? "mesh" : "non-surface"); free(*tri); return -1; }
        int si = -1; for (int k = 0; k < f->nsurfs; k++) if (f->surfs[k].idn == f->tp[i].ent) { si = k; break; }
        if (si < 0) { snprintf(err, errn, "drive surface %d not found", f->tp[i].ent); free(*tri); return -1; }
        double *t = NULL; char e[200] = "";
        int nt = mcs_mesh(f, &f->surfs[si], defl, &t, NULL, NULL, e, sizeof e);
        if (nt < 0) { snprintf(err, errn, "surface %d: %s", f->tp[i].ent, e); free(*tri); return -1; }
        *tri = realloc(*tri, sizeof(double) * 9 * (n + nt + 1)); memcpy(*tri + 9 * n, t, sizeof(double) * 9 * nt); n += nt; free(t);
    }
    if (!n) snprintf(err, errn, "no drive surfaces");
    return n;
}

/* ------------------------------------------------------------------------------------------------ the drop (ours, bl 6.1) */
/* SUBSTITUTE for Mastercam's projection (FUN_18028e8b0, and on the default EMM path the raster model of FUN_180133f10(0xb / 10) read
   by FUN_180278890 - NOT DECODED): the ball (radius R + stock) dropped along -Z onto the triangles, highest contact; the result is the
   tool tip (centre - R) or the centre. Triangles binned in an XY grid. */
typedef struct { const double *t; int n; double R, x0, y0, h; int nx, ny; int *cs, *ci; } Drop;
static void drop_init(Drop *d, const double *t, int n, double R) {
    memset(d, 0, sizeof *d); d->t = t; d->n = n; d->R = R;
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    for (int i = 0; i < 3 * n; i++) for (int k = 0; k < 2; k++) { double v = t[3 * i + k]; if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v; }
    d->h = fmax(R, fmax(hi[0] - lo[0], hi[1] - lo[1]) / 400); d->x0 = lo[0] - R - d->h; d->y0 = lo[1] - R - d->h;
    d->nx = (int)((hi[0] - d->x0 + R) / d->h) + 2; d->ny = (int)((hi[1] - d->y0 + R) / d->h) + 2;
    int nc = d->nx * d->ny; d->cs = calloc(nc + 1, sizeof(int));
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) { const double *a = t + 9 * i;
            double bx0 = fmin(a[0], fmin(a[3], a[6])) - R, bx1 = fmax(a[0], fmax(a[3], a[6])) + R, by0 = fmin(a[1], fmin(a[4], a[7])) - R, by1 = fmax(a[1], fmax(a[4], a[7])) + R;
            int i0 = (int)((bx0 - d->x0) / d->h), i1 = (int)((bx1 - d->x0) / d->h), j0 = (int)((by0 - d->y0) / d->h), j1 = (int)((by1 - d->y0) / d->h);
            for (int j = j0; j <= j1; j++) for (int ii = i0; ii <= i1; ii++) { int c = j * d->nx + ii; if (c < 0 || c >= nc) continue;
                if (pass == 0) d->cs[c + 1]++; else d->ci[d->cs[c]++] = i; } }
        if (pass == 0) { for (int c = 0; c < nc; c++) d->cs[c + 1] += d->cs[c]; d->ci = malloc(sizeof(int) * (d->cs[nc] + 1)); }
        else { for (int c = nc; c > 0; c--) d->cs[c] = d->cs[c - 1]; d->cs[0] = 0; }
    }
}
static void drop_free(Drop *d) { free(d->cs); free(d->ci); }
/* highest sphere-centre z over triangle i at (x, y); -1e300 = no contact */
static double tri_drop(const double *a, double x, double y, double R) {
    double best = -1e300;
    const double *v[3] = { a, a + 3, a + 6 };
    for (int k = 0; k < 3; k++) { double dx = x - v[k][0], dy = y - v[k][1], d2 = dx * dx + dy * dy; if (d2 <= R * R) { double z = v[k][2] + sqrt(R * R - d2); if (z > best) best = z; } }
    for (int k = 0; k < 3; k++) { const double *p = v[k], *q = v[(k + 1) % 3];
        double ux = q[0] - p[0], uy = q[1] - p[1], uz = q[2] - p[2], L2 = ux * ux + uy * uy; if (L2 < 1e-24) continue;
        double L = sqrt(L2), ex = ux / L, ey = uy / L;
        double lc = (x - p[0]) * ex + (y - p[1]) * ey, dperp = (x - p[0]) * -ey + (y - p[1]) * ex;
        if (fabs(dperp) > R) continue;
        double s = sqrt(R * R - dperp * dperp), slope = uz / L, w = sqrt(1 + slope * slope);
        double lcon = lc + s * slope / w;   /* along-line position of the contact (the foot of the perpendicular from the centre) */
        if (lcon < 0 || lcon > L) continue;
        double z = p[2] + slope * lc + s * w; if (z > best) best = z; }
    double e1[3] = { v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2] }, e2[3] = { v[2][0] - v[0][0], v[2][1] - v[0][1], v[2][2] - v[0][2] };
    double nn[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
    double nl = sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
    if (nl > 1e-30) { for (int k = 0; k < 3; k++) nn[k] /= nl; if (nn[2] < 0) for (int k = 0; k < 3; k++) nn[k] = -nn[k];
        if (nn[2] > 1e-9) { double qx = x - R * nn[0], qy = y - R * nn[1];
            double det = e1[0] * e2[1] - e1[1] * e2[0];
            if (fabs(det) > 1e-30) { double rx = qx - v[0][0], ry = qy - v[0][1], s = (rx * e2[1] - ry * e2[0]) / det, t = (e1[0] * ry - e1[1] * rx) / det;
                if (s >= 0 && t >= 0 && s + t <= 1) { double z = v[0][2] + s * e1[2] + t * e2[2] + R * nn[2]; if (z > best) best = z; } } } }
    return best;
}
static double drop_at(const Drop *d, double x, double y) {
    int i = (int)((x - d->x0) / d->h), j = (int)((y - d->y0) / d->h);
    if (i < 0 || j < 0 || i >= d->nx || j >= d->ny) return -1e300;
    int c = j * d->nx + i; double best = -1e300;
    for (int k = d->cs[c]; k < d->cs[c + 1]; k++) { double z = tri_drop(d->t + 9 * d->ci[k], x, y, d->R); if (z > best) best = z; }
    return best;
}

/* ------------------------------------------------------------------------------------------------ polylines */
static double plen(const double *p, int n, int dim) { double L = 0; for (int i = 1; i < n; i++) { double s = 0; for (int k = 0; k < dim; k++) { double d = p[3 * i + k] - p[3 * i - 3 + k]; s += d * d; } L += sqrt(s); } return L; }
/* the point at length s along the polyline (dim 3: 3D lengths, FUN_1800c7b60; dim 2: XY lengths, FUN_1800c7860) */
static void pat(const double *p, int n, int dim, double s, double q[3]) {
    if (n == 1 || s <= 0) { memcpy(q, p, 3 * sizeof(double)); return; }
    double acc = 0;
    for (int i = 1; i < n; i++) { double l = 0; for (int k = 0; k < dim; k++) { double d = p[3 * i + k] - p[3 * i - 3 + k]; l += d * d; } l = sqrt(l);
        if (acc + l >= s && l > 0) { double t = (s - acc) / l; for (int k = 0; k < 3; k++) q[k] = p[3 * i - 3 + k] + (p[3 * i + k] - p[3 * i - 3 + k]) * t; return; }
        acc += l; }
    memcpy(q, p + 3 * (n - 1), 3 * sizeof(double));
}

/* break_chain_to_points (core 0x1801ea480) for the two chains with sync mode 0 (lm +0x3c = 0, FUN_1800caff0 l.133452 -> one segment
   per chain, FUN_1801ea9c0): n = (int)(1.0 / step x max length + 2.0), clamped to 2 .. 0x7fff; each chain at equal spacing len / (n - 1)
   from its start (creep_along_ent); the same count on both chains */
static int break_chains(const BlendPoly *c0, const BlendPoly *c1, double step, double **P0, double **P1) {
    double L0 = plen(c0->p, c0->n, 3), L1 = plen(c1->p, c1->n, 3), mx = fmax(L0, L1);
    double v = 1.0 / step * mx + 2.0; int n = v > 32767 ? 32767 : (int)v; if (n < 2) n = 2;
    *P0 = malloc(sizeof(double) * 3 * n); *P1 = malloc(sizeof(double) * 3 * n);
    double d0 = L0 / (n - 1), d1 = L1 / (n - 1), s0 = 0, s1 = 0;
    for (int j = 0; j < n; j++) { pat(c0->p, c0->n, 3, s0, *P0 + 3 * j); pat(c1->p, c1->n, 3, s1, *P1 + 3 * j); s0 += d0; s1 += d1; }   /* dVar1 += dVar2 */
    return n;
}

/* FUN_1800c7390 (l.130729-130944): drops the points where the direction from the last kept point to P[j] and from P[j] to P[j + 1] turn by less than the angle a (radians) */
static int bl_collinear_filter(double *p, int n, double a) {
    if (n < 3) return n;
    char *del = calloc(n, 1); double ca = cos(a);
    for (int i = 0; i < n - 2; i++) {
        if (del[i]) continue;
        for (int j = i + 1; j < n - 1; j++) {
            double u[3], v[3]; for (int k = 0; k < 3; k++) { u[k] = p[3 * j + k] - p[3 * i + k]; v[k] = p[3 * (j + 1) + k] - p[3 * j + k]; }
            double lu = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]), lv = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (lu == 0.0) u[0] = u[1] = u[2] = 0.0; else { u[0] /= lu; u[1] /= lu; u[2] /= lu; }
            if (lv == 0.0) v[0] = v[1] = v[2] = 0.0; else { v[0] /= lv; v[1] /= lv; v[2] /= lv; }
            if (v[0] * u[0] + v[1] * u[1] + v[2] * u[2] <= ca) break;
            del[j] = 1;
        }
    }
    int o = 0; for (int i = 0; i < n; i++) if (!del[i]) { memmove(p + 3 * o, p + 3 * i, 3 * sizeof(double)); o++; }
    free(del); return o;
}
/* FUN_1800c53a0 (l.129412-129641): point i of pass P moves to (1 - s / L) P[i] + (s / L) Q[i], s = the 3D length of P up to the old point i, L = P's total; the first point stays, the last becomes Q's */
static void bl_spiral(double *P, const double *Q, int n) {
    if (n < 2) return;
    double L = 0; for (int i = 1; i < n; i++) { double d = 0; for (int k = 0; k < 3; k++) d += (P[3 * i + k] - P[3 * i - 3 + k]) * (P[3 * i + k] - P[3 * i - 3 + k]); L += sqrt(d); }
    if (L < mc_systol.tol) return;   /* FUN_1800c53a0 l.129494: only when tol_exref <= L, else unchanged */
    double old[3] = { P[0], P[1], P[2] }, s = 0;
    for (int i = 1; i < n; i++) { double d = 0, cur[3]; for (int k = 0; k < 3; k++) { cur[k] = P[3 * i + k]; d += (cur[k] - old[k]) * (cur[k] - old[k]); }
        s += sqrt(d); memcpy(old, cur, sizeof old);
        double t = s / L, u = 1.0 - t; for (int k = 0; k < 3; k++) P[3 * i + k] = u * cur[k] + t * Q[3 * i + k]; }
}

/* the heights of a long gap at a tool point of output height z (FUN_180142420 l.210545-210720 for the plunge, FUN_180142ee0 for the retract). The plane is the retract height unless retract is off
   and clearance on (FUN_180140d80 l.209853). All in the output's tool coordinate: with comp to tip FUN_180142420 adds the corner radius to an absolute height and the tool point is the centre, so
   the tip reaches the height itself. Feed height f = absolute plane - or point + increment (op +0x130 / +0x128, l.210582-210589: with an absolute plane below the point and msurf_long_gapchk, the
   point). The plane on: *plane = max(its height, f) (l.210628-210635). Returns 1 when the tool goes through the plane (param_5 >= the feed offset, bVar5 / bVar2). */
int blend_gap_planes(const BlendParams *bp, double z, double *plane, double *feed) {
    int use_clr = !bp->ret_on && bp->clr_on;
    int on = use_clr ? bp->clr_on : bp->ret_on;
    int inc = use_clr ? bp->clr_inc : bp->ret_inc;
    double h = use_clr ? bp->clr_h : bp->ret_h;
    double df = bp->fp_inc ? bp->fp_h : bp->fp_h - z;
    if (!bp->fp_inc && df < 0.0 && bp->long_gapchk) df = 0.0;
    *feed = z + df; *plane = *feed;
    if (!on) return 0;
    double dp = inc ? h : h - z;
    *plane = z + fmax(dp, df);
    return dp >= df;
}

/* ------------------------------------------------------------------------------------------------ moves */
typedef struct { Move *m; int n, cap; } MV;
static void mvp(MV *b, int rapid, double x, double y, double z, double feed) {
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 4096; b->m = realloc(b->m, sizeof(Move) * b->cap); }
    Move *m = &b->m[b->n++]; memset(m, 0, sizeof *m); m->rapid = rapid; m->p.x = x; m->p.y = y; m->p.z = z; m->feed = feed;
}

/* the projection of one pass (ours, bl 6.2): the decoded pass points, each re-dropped, plus points where the dropped surface leaves the
   chord by more than tol / 2 between two of them (midpoint subdivision); then the points within tol of the chord through their
   neighbours are left out (the NCI keeps every 2nd .. nth pass point on straight runs) */
typedef struct { double *p; int n, cap; } BlPL;
static void plp(BlPL *l, const double *q) { if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 1024; l->p = realloc(l->p, sizeof(double) * 3 * l->cap); } memcpy(l->p + 3 * l->n++, q, 3 * sizeof(double)); }
static double zof(const Drop *d, double x, double y, double off) { double z = drop_at(d, x, y); return z < -1e299 ? z : z - off; }
static void subdiv(const Drop *d, double off, const double *a, const double *b, double tol, int depth, BlPL *out) {
    double m[3] = { (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, 0 };
    m[2] = zof(d, m[0], m[1], off);
    if (depth > 0 && m[2] > -1e299 && fabs(m[2] - (a[2] + b[2]) * 0.5) > tol * 0.5) { subdiv(d, off, a, m, tol, depth - 1, out); plp(out, m); subdiv(d, off, m, b, tol, depth - 1, out); }
}
static double seg_dev(const double *a, const double *b, const double *q) {
    double u[3], w[3]; for (int k = 0; k < 3; k++) { u[k] = b[k] - a[k]; w[k] = q[k] - a[k]; }
    double L2 = u[0] * u[0] + u[1] * u[1] + u[2] * u[2]; if (L2 < 1e-30) return sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    double t = (w[0] * u[0] + w[1] * u[1] + w[2] * u[2]) / L2; if (t < 0) t = 0; if (t > 1) t = 1;
    double s = 0; for (int k = 0; k < 3; k++) { double e = w[k] - t * u[k]; s += e * e; } return sqrt(s);
}
static void filter(BlPL *l, double tol) {
    if (l->n < 3) return;
    int o = 1, a = 0;
    for (int i = 1; i < l->n - 1; i++) {
        int ok = 1; for (int k = a + 1; k <= i; k++) if (seg_dev(l->p + 3 * a, l->p + 3 * (i + 1), l->p + 3 * k) > tol) { ok = 0; break; }
        if (!ok) { memcpy(l->p + 3 * o, l->p + 3 * i, 3 * sizeof(double)); a = i; o++; }
    }
    memcpy(l->p + 3 * o, l->p + 3 * (l->n - 1), 3 * sizeof(double)); l->n = o + 1;
}

int blend_generate(const BlendParams *bp, const BlendPoly *c0, const BlendPoly *c1, const double *tri, int ntri,
                   Move **mvout, int *nmv, BlendInfo *info, char *err, size_t errn) {
    *mvout = NULL; *nmv = 0;
    if (c0->n < 1 || c1->n < 1) { snprintf(err, errn, "Blend needs two chains"); return 0; }   /* FUN_180031990 l.34186-34190: warning 0x171 */
    if (ntri <= 0) { snprintf(err, errn, "Blend: no drive surfaces"); return 0; }
    double R = bp->D * 0.5, Rs = R + bp->stock, off = bp->tip ? R : 0.0;
    if (!(Rs > 0)) { snprintf(err, errn, "Blend: stock to leave below -R"); return 0; }
    /* the chain point spacing (FUN_180031990 l.34196-34201): stepover x fraction for Along (3D with lm +0x4c = 0, or 2D) */
    double step = bp->stepover; if (bp->along && bp->frac > 1e-6) step *= bp->frac;
    double *P0, *P1; int n = break_chains(c0, c1, step, &P0, &P1);
    Drop D; drop_init(&D, tri, ntri, Rs);
    /* the rulings (FUN_180031ed0: the loft surface's first and last row at each column = the straight line P0[j] -> P1[j]); Along 3D and Across: each sampled every
       tol x 4 in XY and dropped (ours for FUN_18028e8b0, the substitute of bl 6.1), Along 3D measures its 3D length after the filter FUN_1800c7390 (tol / 10, l.33695; FUN_1800c16c0).
       Along 2D: the straight ruling, its XY length (FUN_180030a90 / FUN_1800c1520). Across (FUN_180031610, l.33877-33957): the passes ARE the rulings, one per chain point,
       through the lm frame (identity here), projected by the later stages like every other pass. */
    double **rul = calloc(n, sizeof(double *)); int *rn = calloc(n, sizeof(int)); double *len = calloc(n, sizeof(double)), mx = 0;
    double sstep = fmax(bp->tol * 4, 1e-4);
    int straight = bp->along && bp->two_d;
    for (int j = 0; j < n; j++) {
        const double *a = P0 + 3 * j, *b = P1 + 3 * j;
        if (straight) { rul[j] = malloc(sizeof(double) * 6); memcpy(rul[j], a, 24); memcpy(rul[j] + 3, b, 24); rn[j] = 2; len[j] = plen(rul[j], 2, 2); }
        else {
            double L = hypot(b[0] - a[0], b[1] - a[1]); int s = (int)ceil(L / sstep); if (s < 1) s = 1;
            rul[j] = malloc(sizeof(double) * 3 * (s + 1)); int k = 0;
            for (int i = 0; i <= s; i++) { double x = a[0] + (b[0] - a[0]) * i / s, y = a[1] + (b[1] - a[1]) * i / s, z = zof(&D, x, y, off);
                if (z < -1e299) continue;                                            /* off the drive: no projection there */
                rul[j][3 * k] = x; rul[j][3 * k + 1] = y; rul[j][3 * k + 2] = z; k++; }
            rn[j] = k;
            if (bp->along) { k = bl_collinear_filter(rul[j], k, bp->tol / 10.0); rn[j] = k; }
            len[j] = k ? plen(rul[j], k, 3) : 0;
        }
        if (len[j] > mx) mx = len[j];
    }
    /* Along: the pass count (FUN_180030fb0 l.33717-33730 / FUN_180030a90): m = (int)(max / stepover) >= 1, one more when max / m exceeds max(1.05 stepover, 2 tol); m + 1 passes,
       pass i at i x len_j / m along ruling j. Across: one pass per ruling. */
    int m = 0;
    if (bp->along) { m = (int)(mx / bp->stepover); if (m < 1) m = 1;
        if (fmax(bp->stepover * 1.05, bp->tol * 2.0) < mx / m) m++; }
    if (info) { info->npts = n; info->npass = bp->along ? m + 1 : n; info->maxlen = mx; }
    /* the decoded pass points, per pass (Along) */
    int nvalid = 0; for (int j = 0; j < n; j++) if (rn[j]) nvalid++;
    double *Qp = bp->along ? malloc(sizeof(double) * 3 * (size_t)(m + 1) * (nvalid ? nvalid : 1)) : NULL;
    if (bp->along) {
        for (int i = 0; i <= m; i++) { int v = 0;
            for (int j = 0; j < n; j++) { if (!rn[j]) continue; pat(rul[j], rn[j], bp->two_d ? 2 : 3, i * (len[j] / m), Qp + 3 * ((size_t)i * nvalid + v)); v++; } }
        /* Spiral (FUN_1800307f0 l.33312-33336, method 2 and Along only): each pass morphs into the next one's points along its own length (FUN_1800c53a0 l.129412) */
        if (bp->method == 2) for (int i = 0; i < m; i++) bl_spiral(Qp + 3 * ((size_t)i * nvalid), Qp + 3 * ((size_t)(i + 1) * nvalid), nvalid);
    }
    MV b = { 0 }; BlPL pl = { 0 };
    double clr = bp->clearance, fpd = bp->feed_plane, feed = bp->feed > 0 ? bp->feed : 1, plunge = bp->plunge > 0 ? bp->plunge : feed;
    int started = 0; double last[3] = { 0, 0, 0 };
    int npasses = bp->along ? m + 1 : n;
    for (int i = 0; i < npasses; i++) {
        pl.n = 0;
        if (bp->along) {
            double prev[3]; int have = 0;
            for (int v = 0; v < nvalid; v++) {
                double q[3]; memcpy(q, Qp + 3 * ((size_t)i * nvalid + v), sizeof q);
                q[2] = zof(&D, q[0], q[1], off);   /* the projection of the pass point (ours; on the 3D path the same drop as its ruling) */
                if (q[2] < -1e299) continue;
                if (have) subdiv(&D, off, prev, q, bp->tol, 6, &pl);
                plp(&pl, q); memcpy(prev, q, sizeof prev); have = 1;
            }
        } else for (int v = 0; v < rn[i]; v++) plp(&pl, rul[i] + 3 * v);
        filter(&pl, bp->tol * 0.5);
        if (!pl.n) continue;
        /* Zigzag: FUN_1800307f0 l.33278-33310 reverses every second pass (FUN_1800c4a50), Along or Across; Spiral with Across and One way: no change */
        if (bp->method == 0 && (i & 1)) for (int a = 0, z = pl.n - 1; a < z; a++, z--) for (int k = 0; k < 3; k++) { double t = pl.p[3 * a + k]; pl.p[3 * a + k] = pl.p[3 * z + k]; pl.p[3 * z + k] = t; }   
        const double *s = pl.p;
        if (!started) { mvp(&b, 1, s[0], s[1], clr, 0); mvp(&b, 1, s[0], s[1], s[2] + fpd, 0); mvp(&b, 0, s[0], s[1], s[2], plunge); started = 1; }
        else {
            double g = hypot(s[0] - last[0], s[1] - last[1]);
            if (g <= bp->max_gap) {   /* short gap, Broken (measured bl 5.3): up first when the next start is higher, else across then down */
                if (s[2] > last[2]) { mvp(&b, 0, last[0], last[1], s[2], feed); mvp(&b, 0, s[0], s[1], s[2], feed); }
                else { mvp(&b, 0, s[0], s[1], last[2], feed); mvp(&b, 0, s[0], s[1], s[2], feed); }
            } else {   /* long gap: FUN_180140d80's retract branch: FUN_180142ee0 (up), FUN_180142420 (over and down); no corpus case */
                double p0, f0, p1, f1; int via0 = blend_gap_planes(bp, last[2], &p0, &f0), via1 = blend_gap_planes(bp, s[2], &p1, &f1);
                double rr = bp->ret_feed > 0 ? bp->ret_feed : feed;
                if (!bp->rapid_ret) mvp(&b, 0, last[0], last[1], f0, rr);          /* op +0x141 off: feed up at the retract rate (op +0x320) */
                else if (!via0 || p0 <= f0) mvp(&b, 1, last[0], last[1], f0, 0);
                if (via0) mvp(&b, 1, last[0], last[1], p0, 0);
                mvp(&b, 1, s[0], s[1], via1 ? p1 : f1, 0);
                if (via1) mvp(&b, 1, s[0], s[1], f1, 0);
                mvp(&b, 0, s[0], s[1], s[2], plunge);
            }
        }
        for (int k = 1; k < pl.n; k++) mvp(&b, 0, pl.p[3 * k], pl.p[3 * k + 1], pl.p[3 * k + 2], feed);
        memcpy(last, pl.p + 3 * (pl.n - 1), sizeof last);
    }
    if (started) { mvp(&b, 1, last[0], last[1], last[2] + fpd, 0); mvp(&b, 1, last[0], last[1], clr, 0); }   /* measured (bl 5.4) */
    free(Qp); for (int j = 0; j < n; j++) free(rul[j]);
    free(rul); free(rn); free(len); free(P0); free(P1); free(pl.p); drop_free(&D);
    if (!started) { free(b.m); snprintf(err, errn, "Blend: the passes miss the drive surfaces"); return 0; }
    *mvout = b.m; *nmv = b.n; return 1;
}
