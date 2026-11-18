/* verify - stock-removal simulation for the 2D mill ops. THE ENGINE IS OURS (Mastercam's Verify is ModuleWorks -
 * no portable code; see verify.h and ../formats/simulator.md). Inputs are the decoded ones: the NcMotion list
 * (nc_post.c, what Backplot plays), the parametric tool profile (toolprofile.c, tlcore.dll decode) and the
 * machine group's stock setup (group record +0x1230..+0x1258). */
#include "verify.h"
#include "toolprofile.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

VF *vf_new(double xmin, double ymin, double xmax, double ymax, double ztop, double zbot, double cell) {
    VF *v = calloc(1, sizeof *v);
    v->xmin = xmin; v->ymin = ymin; v->xmax = xmax; v->ymax = ymax; v->ztop = ztop; v->zbot = zbot;
    v->cell = cell; v->x0 = xmin; v->y0 = ymin;
    v->nx = (int)ceil((xmax - xmin) / cell); v->ny = (int)ceil((ymax - ymin) / cell);
    if (v->nx < 2) v->nx = 2; if (v->ny < 2) v->ny = 2;
    v->z = malloc((size_t)v->nx * v->ny * sizeof(double));
    v->cut = malloc((size_t)v->nx * v->ny * sizeof(int));
    vf_reset(v);
    return v;
}
void vf_reset(VF *v) {
    for (long i = 0; i < (long)v->nx * v->ny; i++) { v->z[i] = v->ztop; v->cut[i] = -1; }
    v->nwarn = 0;
}
void vf_free(VF *v) { if (!v) return; free(v->z); free(v->cut); free(v->warn); free(v); }

/* zsurf(r): the lowest outline point at radius r, over VF_NS samples of [0, rmax] (heights above the tip) */
int vf_tool(const Tool *tl, VfTool *out) {
    if (!tl || tl->dia <= 0) return 0;
    double rmax = tl->dia / 2;
    TpParams pp; tp_params_from_tool(tl, &pp);
    TpSeg seg[64]; int ns = 0;
    double rr[512], zz[512]; int np = 0;
    if (tp_build(&pp, seg, 64, &ns) == TP_OK) np = tp_polyline(seg, ns, rr, zz, 512, 0.02);
    if (np < 2) { rr[0] = 0; zz[0] = 0; rr[1] = rmax; zz[1] = 0; np = 2; }   /* bare cylinder fallback (ours) */
    double dr = rmax / (VF_NS - 1);
    for (int i = 0; i < VF_NS; i++) out->zsurf[i] = 1e300;
    for (int k = 1; k < np; k++) {                              /* envelope: min z per radius sample */
        double r0 = rr[k - 1], r1 = rr[k], z0 = zz[k - 1], z1 = zz[k];
        double lo = fmin(r0, r1), hi = fmax(r0, r1);
        int a = (int)ceil(lo / dr - 0.5), b = (int)floor(hi / dr + 0.5);
        if (a < 0) a = 0; if (b >= VF_NS) b = VF_NS - 1;
        for (int i = a; i <= b; i++) {
            double rc = i * dr, t = fabs(r1 - r0) < 1e-12 ? 0 : (rc - r0) / (r1 - r0);
            if (t < -0.01 || t > 1.01) continue;
            if (t < 0) t = 0; if (t > 1) t = 1;
            double z = z0 + (z1 - z0) * t;
            if (z < out->zsurf[i]) out->zsurf[i] = z;
        }
    }
    for (int i = 0; i < VF_NS; i++) if (out->zsurf[i] > 1e200) out->zsurf[i] = i ? out->zsurf[i - 1] : 0;  /* gaps: extend (ours) */
    out->rmax = rmax;
    return 1;
}

static double zsurf_at(const VfTool *t, double r) {
    if (r >= t->rmax) return 1e300;
    double x = r / t->rmax * (VF_NS - 1); int i = (int)x;
    if (i >= VF_NS - 1) return t->zsurf[VF_NS - 1];
    return t->zsurf[i] + (t->zsurf[i + 1] - t->zsurf[i]) * (x - i);
}

static void vf_warn(VF *v, double x, double y, double z) {
    if (v->nwarn >= 4096) return;                       /* capped (ours) */
    if (v->nwarn == v->capwarn) { v->capwarn = v->capwarn ? v->capwarn * 2 : 64; v->warn = realloc(v->warn, v->capwarn * sizeof *v->warn); }
    v->warn[v->nwarn][0] = x; v->warn[v->nwarn][1] = y; v->warn[v->nwarn][2] = z; v->nwarn++;
}

/* one tool position: lower every cell in the footprint to tip_z + zsurf(r) */
static int cut_at(VF *v, const VfTool *t, double x, double y, double tipz) {
    int i0 = (int)((x - t->rmax - v->x0) / v->cell), i1 = (int)((x + t->rmax - v->x0) / v->cell);
    int j0 = (int)((y - t->rmax - v->y0) / v->cell), j1 = (int)((y + t->rmax - v->y0) / v->cell);
    if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0; if (i1 >= v->nx) i1 = v->nx - 1; if (j1 >= v->ny) j1 = v->ny - 1;
    int cut = 0;
    for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        double px = v->x0 + (i + 0.5) * v->cell - x, py = v->y0 + (j + 0.5) * v->cell - y;
        double rr = hypot(px, py); if (rr > t->rmax) continue;
        double zn = tipz + zsurf_at(t, rr);
        if (zn < v->z[(size_t)j * v->nx + i]) { v->z[(size_t)j * v->nx + i] = zn; v->cut[(size_t)j * v->nx + i] = v->key; cut = 1; }
    }
    return cut;
}

int vf_cut(VF *v, const VfTool *tool, const NcMotion *m) {
    if (m->kind == NCM_TOOL) return 0;
    int cut = 0;
    if (m->kind == NCM_DRILL) {                          /* R plane -> depth (the pecks don't change the geometry) */
        cut = cut_at(v, tool, m->b[0], m->b[1], m->b[2]);
        if (cut && m->feed < 0) vf_warn(v, m->b[0], m->b[1], m->b[2]);
        return cut;
    }
    double p0[3], p1[3]; nc_motion_point(m, 0, p0); nc_motion_point(m, 1, p1);
    double L = hypot(p1[0] - p0[0], hypot(p1[1] - p0[1], p1[2] - p0[2]));
    int steps = (int)(L / (v->cell * 0.5)) + 1;
    if (steps > 200000) steps = 200000;                  /* safety cap (ours) */
    for (int s = 1; s <= steps; s++) {
        double p[3]; nc_motion_point(m, (double)s / steps, p);
        if (cut_at(v, tool, p[0], p[1], p[2])) {
            cut = 1;
            if (m->feed < 0) vf_warn(v, p[0], p[1], p[2]);
        }
    }
    return cut;
}

double vf_volume_removed(const VF *v) {
    double vol = 0;
    for (long i = 0; i < (long)v->nx * v->ny; i++) if (v->z[i] < v->ztop) vol += v->ztop - v->z[i];
    return vol * v->cell * v->cell;
}

void vf_snap(const VF *v, VfSnap *s) {
    size_t nc = (size_t)v->nx * v->ny;
    s->z = malloc(nc * sizeof(double)); memcpy(s->z, v->z, nc * sizeof(double));
    s->cut = malloc(nc * sizeof(int)); memcpy(s->cut, v->cut, nc * sizeof(int));
}
void vf_restore(VF *v, const VfSnap *s) {
    size_t nc = (size_t)v->nx * v->ny;
    memcpy(v->z, s->z, nc * sizeof(double)); memcpy(v->cut, s->cut, nc * sizeof(int));
}
void vf_snap_free(VfSnap *s) { free(s->z); s->z = NULL; free(s->cut); s->cut = NULL; }

/* Minecraft-style mesh: flat top quads (X-runs of equal height and colour merged - uncut areas collapse to a few quads)
 * plus wall quads where neighbours differ, and the box walls at the grid border. Ours. Each vertex x y z nx ny nz r g b;
 * an inner wall takes the colour of the cell on its high side, or its low side with WALL_LOW (ours). */
static float *qemit(float *o, const float *c, double ax, double ay, double az, double bx, double by, double bz,
                    double cx, double cy, double cz, double dx, double dy, double dz) {
    double ux = bx - ax, uy = by - ay, uz = bz - az, vx = dx - ax, vy = dy - ay, vz = dz - az;
    double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    double l = sqrt(nx * nx + ny * ny + nz * nz); if (l < 1e-12) return o; nx /= l; ny /= l; nz /= l;
    const double *q[4] = { (double[]){ax, ay, az}, (double[]){bx, by, bz}, (double[]){cx, cy, cz}, (double[]){dx, dy, dz} };
    const int T[6] = { 0, 1, 2, 0, 2, 3 };
    for (int k = 0; k < 6; k++) {
        o[0] = (float)q[T[k]][0]; o[1] = (float)q[T[k]][1]; o[2] = (float)q[T[k]][2];
        o[3] = (float)nx; o[4] = (float)ny; o[5] = (float)nz; o[6] = c[0]; o[7] = c[1]; o[8] = c[2]; o += 9;
    }
    return o;
}

int vf_mesh(const VF *v, const float *cellrgb, const float rgb[3], float **out) { return vf_mesh_ex(v, cellrgb, rgb, 0, NULL, out); }
int vf_mesh_ex(const VF *v, const float *cellrgb, const float rgb[3], int wall_low, const float *side_rgb, float **out) {
    size_t cap = 4096;
    float *buf = malloc(cap * 9 * sizeof(float)), *o = buf;
    static const float white[3] = { 1, 1, 1 };
    if (!rgb) rgb = white;
#define CC(ci, cj) (cellrgb ? cellrgb + 3 * ((size_t)(cj) * v->nx + (ci)) : rgb)
#define SIDE(ci, cj) (side_rgb ? side_rgb : CC(ci, cj))
#define SAMEC(a, b) (!cellrgb || (cellrgb[3 * (a)] == cellrgb[3 * (b)] && cellrgb[3 * (a) + 1] == cellrgb[3 * (b) + 1] && cellrgb[3 * (a) + 2] == cellrgb[3 * (b) + 2]))
#define GROW() do { if ((size_t)(o - buf) + 18 * 9 > cap * 9) { size_t off = o - buf; cap *= 2; buf = realloc(buf, cap * 9 * sizeof(float)); o = buf + off; } } while (0)
#define EMIT(c, ...) do { GROW(); o = qemit(o, c, __VA_ARGS__); } while (0)
    for (int j = 0; j < v->ny; j++) {
        double ya = v->y0 + j * v->cell, yb = ya + v->cell;
        int i = 0;
        while (i < v->nx) {
            size_t c0 = (size_t)j * v->nx + i;
            double z = v->z[c0];
            int ie = i + 1;                                    /* the run of cells at this height (and colour) */
            while (ie < v->nx && v->z[(size_t)j * v->nx + ie] == z && SAMEC(c0, (size_t)j * v->nx + ie)) ie++;
            double xa = v->x0 + i * v->cell, xb = v->x0 + ie * v->cell;
            EMIT(CC(i, j), xa, ya, z, xb, ya, z, xb, yb, z, xa, yb, z);                             /* top */
            double zr = ie < v->nx ? v->z[(size_t)j * v->nx + ie] : v->zbot;              /* wall at the run end */
            const float *wc = ie >= v->nx ? SIDE(ie - 1, j) : zr < z ? (wall_low ? CC(ie, j) : CC(ie - 1, j)) : (wall_low ? CC(ie - 1, j) : CC(ie, j));
            if (zr < z - 1e-12) EMIT(wc, xb, ya, z, xb, yb, z, xb, yb, zr, xb, ya, zr);
            else if (zr > z + 1e-12) EMIT(wc, xb, yb, z, xb, ya, z, xb, ya, zr, xb, yb, zr);
            if (i == 0 && z > v->zbot + 1e-12) EMIT(SIDE(0, j), xa, yb, z, xa, ya, z, xa, ya, v->zbot, xa, yb, v->zbot);   /* -x border */
            i = ie;
        }
        for (int k = 0; k < v->nx; k++) {                  /* walls towards the +y row (per cell) */
            double z = v->z[(size_t)j * v->nx + k], zu = j + 1 < v->ny ? v->z[(size_t)(j + 1) * v->nx + k] : v->zbot;
            if (zu == z) continue;
            double xa = v->x0 + k * v->cell, xb = xa + v->cell;
            if (j + 1 >= v->ny) { if (zu < z) EMIT(SIDE(k, j), xb, yb, z, xa, yb, z, xa, yb, zu, xb, yb, zu); continue; }   /* +y border */
            const float *hc = zu < z ? CC(k, j) : CC(k, j + 1), *lc = zu < z ? CC(k, j + 1) : CC(k, j), *wc = wall_low ? lc : hc;
            if (zu < z) EMIT(wc, xb, yb, z, xa, yb, z, xa, yb, zu, xb, yb, zu);
            else EMIT(wc, xa, yb, z, xb, yb, z, xb, yb, zu, xa, yb, zu);
        }
    }
    for (int i = 0; i < v->nx; i++) {                       /* the -y border wall */
        double xa = v->x0 + i * v->cell, xb = xa + v->cell, z = v->z[(size_t)i];
        if (z > v->zbot + 1e-12) EMIT(SIDE(i, 0), xa, v->y0, z, xb, v->y0, z, xb, v->y0, v->zbot, xa, v->y0, v->zbot);
    }
#undef EMIT
#undef GROW
#undef SAMEC
#undef SIDE
#undef CC
    *out = buf;
    return (int)((o - buf) / 9);                 /* vertices (9 floats each) */
}

/* the target's top surface over the grid (ours: Mastercam's Compare is ModuleWorks McamVerification.SetTarget, NOT
   portable): every triangle (9 doubles: 3 vertices) rasterised at the cell centres in XY, the highest Z kept; cells no
   triangle covers get -1e300. Returns a malloc'd nx * ny array. */
double *vf_target_top(const VF *v, const double *tri, int ntri) {
    size_t nc = (size_t)v->nx * v->ny;
    double *t = malloc(nc * sizeof(double));
    if (!t) return NULL;
    for (size_t i = 0; i < nc; i++) t[i] = -1e300;
    for (int k = 0; k < ntri; k++) {
        const double *a = tri + 9 * k, *b = a + 3, *c = a + 6;
        double d = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
        if (fabs(d) < 1e-18) continue;                      /* vertical / degenerate in XY */
        double lx = fmin(a[0], fmin(b[0], c[0])), hx = fmax(a[0], fmax(b[0], c[0]));
        double ly = fmin(a[1], fmin(b[1], c[1])), hy = fmax(a[1], fmax(b[1], c[1]));
        int i0 = (int)ceil((lx - v->x0) / v->cell - 0.5), i1 = (int)floor((hx - v->x0) / v->cell - 0.5);
        int j0 = (int)ceil((ly - v->y0) / v->cell - 0.5), j1 = (int)floor((hy - v->y0) / v->cell - 0.5);
        if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0; if (i1 >= v->nx) i1 = v->nx - 1; if (j1 >= v->ny) j1 = v->ny - 1;
        for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
            double px = v->x0 + (i + 0.5) * v->cell, py = v->y0 + (j + 0.5) * v->cell;
            double w1 = ((px - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (py - a[1])) / d;
            double w2 = ((b[0] - a[0]) * (py - a[1]) - (px - a[0]) * (b[1] - a[1])) / d;
            double w0 = 1 - w1 - w2;
            if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
            double z = w0 * a[2] + w1 * b[2] + w2 * c[2];
            size_t ci = (size_t)j * v->nx + i;
            if (z > t[ci]) t[ci] = z;
        }
    }
    return t;
}

int vf_stock_box(const McamFile *f, const NcMotion *m, int n, double max_tool_r, double ztop_hint, double box[6]) {
    if (n <= 0) return 0;
    double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
    for (int i = 0; i < n; i++) {
        if (m[i].kind == NCM_TOOL) continue;
        for (int e = 0; e < 2; e++) { const double *p = e ? m[i].b : m[i].a;
            for (int k = 0; k < 3; k++) { if (p[k] < lo[k]) lo[k] = p[k]; if (p[k] > hi[k]) hi[k] = p[k]; } }
    }
    if (lo[0] > hi[0]) return 0;
    if (f->stock[0] > 0 && f->stock[1] > 0 && f->stock[2] > 0 && f->stock_shape == 0 && f->stock_rec && f->stock_vno >= 2 && f->stock_vno <= 6) {
        /* a stock plane other than Top (cui_stock.c): Front .. Left turn the box onto world axes, so the box of its 12 lines
           (stock_calc_geom, F.stock_lines) is the stock - ours, as the rest of this Verify; Iso (7) / other planes: no stock box */
        for (int k = 0; k < 3; k++) { box[k] = 1e300; box[3 + k] = -1e300; }
        for (int l = 0; l < 12; l++) for (int e = 0; e < 2; e++) for (int k = 0; k < 3; k++) {
            double q = f->stock_lines[l][3 * e + k]; if (q < box[k]) box[k] = q; if (q > box[3 + k]) box[3 + k] = q; }
        if (lo[2] < box[2]) box[2] = lo[2] - (mcam_is_metric(f) ? 1.0 : 0.05);
        return 1;
    }
    if (f->stock[0] > 0 && f->stock[1] > 0 && f->stock[2] > 0 && f->stock_shape == 0 && !(f->stock_rec && f->stock_vno > 1)) {   /* rectangular (group +0x1264 = 0), Top */
        /* job_setup_stock_corners (mccore 0x1805e4dc0): min = origin + OFF, max = min + size */
        double dx = 0, dy = 0, dz = 0;
        switch (f->stock_sel) {
        case 0: dx = -f->stock[0] / 2; dy = -f->stock[1] / 2; dz = -f->stock[2]; break;   /* centre of the top face */
        case 1: dx = 0;            dy = -f->stock[1]; dz = -f->stock[2]; break;
        case 2: dx = -f->stock[0]; dy = -f->stock[1]; dz = -f->stock[2]; break;
        case 3: dx = -f->stock[0]; dy = 0;            dz = -f->stock[2]; break;
        case 4: dx = 0;            dy = 0;            dz = -f->stock[2]; break;
        case 5: dx = 0;            dy = -f->stock[1]; dz = 0; break;
        case 6: dx = -f->stock[0]; dy = -f->stock[1]; dz = 0; break;
        case 7: dx = -f->stock[0]; dy = 0;            dz = 0; break;
        default: break;                                                                 /* 8: min/min/bottom */
        }
        box[0] = f->stock_org[0] + dx; box[3] = box[0] + f->stock[0];
        box[1] = f->stock_org[1] + dy; box[4] = box[1] + f->stock[1];
        box[2] = f->stock_org[2] + dz; box[5] = box[2] + f->stock[2];
        if (lo[2] < box[2]) box[2] = lo[2] - (mcam_is_metric(f) ? 1.0 : 0.05);   /* through-cuts (drill tips): ours */
        return 1;
    }
    /* no stock setup: motions' bounds + the largest tool radius; top = the ops' top of stock when known,
       else the highest feed-motion point (rapids / clearance excluded) - OURS */
    box[0] = lo[0] - max_tool_r; box[3] = hi[0] + max_tool_r;
    box[1] = lo[1] - max_tool_r; box[4] = hi[1] + max_tool_r;
    double zf = -1e300;
    for (int i = 0; i < n; i++) if (m[i].feed >= 0 && m[i].kind != NCM_TOOL) {
        if (m[i].a[2] > zf) zf = m[i].a[2]; if (m[i].b[2] > zf) zf = m[i].b[2]; }
    box[5] = ztop_hint == ztop_hint ? ztop_hint : zf > -1e200 ? zf : hi[2];
    box[2] = lo[2] - (mcam_is_metric(f) ? 1.0 : 0.05);
    if (box[5] - box[2] < 1e-6) box[2] = box[5] - (mcam_is_metric(f) ? 1.0 : 0.05);
    return 1;
}
