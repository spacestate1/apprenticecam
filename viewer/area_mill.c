/* area_mill.c - 2D High Speed Area Mill: decoded parameter plumbing to the NCG macro + boundary arc tessellation.
 * Spec and every citation: ../formats/area_mill.md. The McHmm Area Clearance pass positions (section 11) are at the
 * end of this file.
 *
 * Citation keys: "mm l.N" = decomp/mcmill_all.c line N, "mk l.N" = decomp/mckernel_all.c line N,
 * "core" = mccore.dll (Ghidra ~/mastercam-re/ghidra_core, DumpDecomp of run_mchutl_coreareamill, depth 1). */
#include "area_mill.h"
#include <math.h>
#include <string.h>

static double rd(const uint8_t *r, uint32_t o) { double v; memcpy(&v, r + o, 8); return v; }
static int16_t rs(const uint8_t *r, uint32_t o) { int16_t v; memcpy(&v, r + o, 2); return v; }

static void put(AreaMillMacro *m, const char *name, double v, char kind) {
    if (m->n < (int)(sizeof m->p / sizeof m->p[0])) { m->p[m->n].name = name; m->p[m->n].value = v; m->p[m->n].kind = kind; m->n++; }
}

int area_mill_macro(const uint8_t *r, uint32_t n, int metric, double sys_tol, int horizontal, double step_dz,
                    AreaMillMacro *m) {
    memset(m, 0, sizeof *m);
    if (!r || n < 0x17d8 || rs(r, 0x6c) != 134 || rs(r, 0x1228) != 1) return 0;

    /* CHST2DProcess::SetParams param_1 = record 0x1288 (SetOperationID mm l.475366, local_16d0 = ent + 0x1288);
       < 1e-12 -> 0.001 inch / 0.025 mm (mm l.475495-475504). It reaches the kernel as WI+0x78 -> FUN_1802a1670
       param_8 -> hmmCoreAreaMillDat +0x30 (mm l.460035 local_5c48) -> TP2DHSTCoreAreaMill +0x28 (core FUN_180917530). */
    double tol = rd(r, 0x1288);
    if (tol < 1e-12) tol = metric ? 0.025 : 0.001;
    m->tol = tol;
    m->scale = metric ? 1.0 : 25.4;                       /* H+0x38: 0x4039666666666666 inch, 1.0 metric (mk l.83399-83405) */
    const double S = m->scale, D = rd(r, 0x350);          /* hmmCoreAreaMillDat +0 = op 0x350 (mm l.460031) */

    /* mode = hmmCoreAreaMillDat +0x84 (mm l.460074-460093): param_3 (horizontal pass) -> 5; param_5 = record 0x17b8
       -> 4; else param_4 = record 0x1567 ? 0 : 1. param_4 / param_6 are 0x1567 for style 1: FUN_18029d3e0
       mm l.456007-456019 (S+0x88 / S+0x71 = CHST2DProcess[1] / [0x55], set from 0x1567 by SetParams mm l.475478,
       475522, 475551 when the process byte +0 is 0 - where +0 is set is NOT DECODED). */
    int from_out = r[0x1567] != 0, trim = r[0x17b8] != 0;
    m->mode = horizontal ? 5 : trim ? 4 : from_out ? 0 : 1;
    int core = m->mode == 0 || (m->mode == 4 && from_out);          /* P+0x74 == 0 || (== 4 && P+0x78) */

    /* passes / linking plans (mk FUN_18006bd90 l.83480-83565) */
    switch (m->mode) {
    case 5: m->passes_plan = "Horizontal Passes Plan"; m->passes_id = 0x143a; m->linking_plan = "Horizontal Linking Plan"; m->linking_id = 0x144c; break;
    case 0: m->passes_plan = "Core Roughing Passes Plan"; m->passes_id = 0x1460; m->linking_plan = "Core Roughing Linking Plan"; m->linking_id = 0x1461; break;
    case 4: m->passes_plan = from_out ? "Core Roughing Passes Plan" : "Area Clearance Passes Plan"; m->passes_id = from_out ? 0x1460 : 0x1435;
            m->linking_plan = from_out ? "Core Roughing Linking Plan" : "Area Clearance Linking Plan"; m->linking_id = from_out ? 0x1461 : 0x141d;
            m->edit_to_boundaries = 0x1416; break;
    default: m->passes_plan = "Area Clearance Passes Plan"; m->passes_id = 0x1435; m->linking_plan = "Area Clearance Linking Plan"; m->linking_id = 0x141d;
    }

    /* OffsetDistance before the Bounding Box plan (mk l.83444-83459): core modes only, 0.03937 inch / 1.0 metric */
    if (core) put(m, "OffsetDistance", (metric ? 1.0 : 0.03937007874015748) * S, 'd');

    /* ---- FUN_18006b750 (mk l.83223-83364); P = TP2DHSTCoreAreaMill +0x30 (core FUN_180917530) ---- */
    put(m, "ToolCornerRadius", D * 0.05 * S, 'd');                        /* l.83247: D x 0.05 */
    put(m, "ToolCutterDiameter", D * S, 'd');
    /* ToolThicknessXY = max(0, dat+0x38); dat+0x38 (mm l.460036-460071) is built from FUN_1802a1670 param_9 / 10 =
       WI+0x80 / 0x88, which FUN_18029d3e0 passes as 0 (mm l.456052-456054): 0 on this path */
    put(m, "ToolThicknessXY", 0.0, 'd');
    put(m, "ToolThicknessZ", 0.0, 'd');
    put(m, "StepMachTolerance", tol * S, 'd');                            /* T+0x28 */
    put(m, "PosZLo", 0.0, 'd'); put(m, "PosZHi", 0.0, 'd');
    put(m, "ToolToolType", 1, 'i');
    /* MinOffset / MaxOffset (l.83289-83310): P[0] = record 0x1590, P[1] = record 0x1598 (dat+0x08 / +0x10,
       mm l.460032-460033). Values in the user's parts: 0x1590 = 1.485, 0x1598 = 2.7 for D 6 at 45 % / 55 %:
       0x1598 is the maximum, 0x1590 the minimum (the MP texts 12973 / 12974 have them the other way round). */
    double mx = rd(r, 0x1598), mn = rd(r, 0x1590);
    if (mx < sys_tol) { mx = D * 0.45; mn = mx * 0.55; }
    else if (mn > mx) mn = mx * 0.55;
    if (sys_tol < mx) { put(m, "MinOffset", mn * S, 'd'); put(m, "MaxOffset", mx * S, 'd'); }
    /* ProfileSmoothing (l.83311-83337): P+0x73 = record 0x1566; P+0x10 = 0x15a8, P+0x20 = 0x15a0, P+0x18 = 0x15b0 */
    if (r[0x1566]) {
        put(m, "ProfileSmoothing", 1, 'b');
        put(m, "ProfileSmoothingCornerTolerance", rd(r, 0x15a8) * S, 'd');
        put(m, "ProfileSmoothingMaxRadius", rd(r, 0x15a0) * S, 'd');
        put(m, "ProfileSmoothingCornerOffsetTolerance", rd(r, 0x15b0) * S, 'd');
    } else put(m, "ProfileSmoothing", 0, 'b');
    if (core) {                                                            /* l.83339-83360 */
        double t = D * 0.5 - tol * 10.0;
        if (sys_tol < t) put(m, "CoreRoughingTrimOffset", t * S, 'd');
        /* P+0x60 / +0x68 = dat+0x70 / +0x78: 1.0 / 1.0 (mm l.460073-460074), except mode 0 where FUN_1802a9c60
           (mm l.464506-464640) rewrites them from the layer boxes via FUN_1802a9e80 / FUN_1802a9d80 (read; they feed only the linker's box, hp l.2995-3020;
           mode 0 is refused, cam2d_dyn.c: not computed here) */
        put(m, "CoreRoughingExpansionX", 1.0, 'd'); put(m, "CoreRoughingExpansionY", 1.0, 'd');
        m->expansion_not_decoded = m->mode == 0;
    }
    if (m->mode == 4) {                                                    /* l.83510-83521 */
        put(m, "OverThickness", 0.0, 'd'); put(m, "EnableTrimToZRange", 0, 'b'); put(m, "TrimBoundaryThickness", 0.005, 'd');
    }

    /* ---- FUN_18006af68 (mk l.83010-83221) ---- */
    double lin = metric ? 6.0 : 0.3;
    double ramp_ang = rd(r, 0x15e0) > sys_tol ? rd(r, 0x15e0) / M_PI * 180.0 : 3.0;   /* P+0x40 = 0x15e0 */
    double ramp_h = fabs(step_dz) + rd(r, 0x15d8);                         /* P+0x38 = |param_12| + 0x15d8 (mm l.460096) */
    if (r[0x15ca] == 1) {                                                  /* P+0x71 = 0x15ca entry style 1 helix */
        put(m, "ProfileRampStyle", 1, 'i');
        put(m, "HelixDiameter", (rd(r, 0x15d0) > sys_tol ? rd(r, 0x15d0) * 2.0 : lin) * S, 'd');   /* P+0x30 = 0x15d0 */
        put(m, "MaxRampAngle", ramp_ang, 'd');
        put(m, "RampHeightOffset", ramp_h * S, 'd');
    } else if (r[0x15ca] == 0) {
        put(m, "ProfileRampStyle", 0, 'i');
        put(m, "MinProfileRampDiameter", (rd(r, 0x15f0) > sys_tol ? rd(r, 0x15f0) : lin) * S, 'd');   /* P+0x50 = 0x15f0 */
        put(m, "MaxRampAngle", ramp_ang, 'd');
        put(m, "RampHeightOffset", ramp_h * S, 'd');
    }
    put(m, "LinkRetractStyle", 2, 'i');
    put(m, "LinkClimbMilling", r[0x1564] == 0, 'b');                      /* P+0x90 = 0x1564 */
    double stay = metric ? 2.5 : 0.15;                                     /* P+0x79 = 0x15cd */
    if (r[0x15cd] == 1) stay = rd(r, 0x1608) / 100.0 * D;                  /* P+0x88 = 0x1608 (% of D) */
    else if (r[0x15cd] == 0) stay = rd(r, 0x1600);                         /* P+0x80 = 0x1600 */
    put(m, "LinkMaxStayOnSurface", stay * S, 'd');
    double lr = metric ? 0.5 : 0.01968503937007874;                        /* ProfileLinkRadius: P[0] = 0x1590 */
    if (sys_tol < rd(r, 0x1590) && rd(r, 0x1590) * 0.25 <= lr) put(m, "ProfileLinkRadius", rd(r, 0x1590) * 0.25 * S, 'd');
    put(m, "MinProfileDiameter", rd(r, 0x15f8) * S, 'd');                 /* P+0x58 = 0x15f8 */
    int mc = r[0x16ba];                                                    /* P+0xb0 = 0x16ba */
    if ((mc == 1 && rd(r, 0x1640) > sys_tol) || (mc == 2 && rd(r, 0x378) > sys_tol && rd(r, 0x1648) > sys_tol)) {
        put(m, "EnableMaxCuttingDist", 1, 'b');
        put(m, "MaxCuttingDist", (mc == 2 ? rd(r, 0x1648) * rd(r, 0x378) : rd(r, 0x1640)) * S, 'd');
    }
    /* LinkStartX / Y: P+0xb8 / 0xc0 = CHST2DProcess +0x38 / +0x40 (FUN_1802ba330 param_7): set by FUN_1802b8d50 (mcmill_hst2d.c l.18218)
       from a one-point role-5 entry chain, else -10000 (mckernel then skips them). Role 5 is refused (hst_input_refused), so -10000 always */
    put(m, "LinkVerticalLeadInRadius", rd(r, 0x1798) * S, 'd');           /* P+0xc8 = 0x1798 */
    put(m, "LinkVerticalLeadOutRadius", rd(r, 0x17a0) * S, 'd');          /* P+0xd0 = 0x17a0 */
    return 1;
}

double area_mill_param(const AreaMillMacro *m, const char *name, double def) {
    for (int i = 0; i < m->n; i++) if (!strcmp(m->p[i].name, name)) return m->p[i].value;
    return def;
}

/* FUN_1802a1670 mm l.459473-459516 (and l.459858-459903): an arc of the boundary file becomes n chords,
   half-angle acos((r - tol) / r); n = ceil(sweep / (2 x half)) when 2 x half < sweep, else 1 */
int area_mill_arc_segments(double r, double sweep, double tol) {
    if (r <= 0) return 1;
    double h = acos((r - tol) / r);
    if (h * 2.0 < sweep) return (int)ceil(sweep / (h * 2.0));
    return 1;
}

/* FUN_18029caf0 mm l.455621-455658 */
double area_mill_arc_sweep(int cw, double a0, double a1, double *dir) {
    const double T = 6.283185307179586;
    if (a0 < 0) a0 += T; if (a0 > T) a0 -= T;
    if (a1 < 0) a1 += T; if (a1 > T) a1 -= T;
    if (!cw) { if (a1 < a0) a1 += T; *dir = 1.0; }
    else { if (a0 < a1) a1 -= T; *dir = -1.0; }
    return fabs(a0 - a1);
}

/* =====================================================================================================================
 * McHmm Area Clearance pass positions (formats/area_mill.md 11.3). Citations: hmm-vorlib = decomp/hmm_vorlib.c,
 * hmm-pass0 = decomp/hmm_pass0.c (McHmm.dll, Ghidra ~/mastercam-re/ghidra_hmm).
 * ===================================================================================================================*/
#include <stdlib.h>

typedef struct {
    int nx, ny; double x0, y0, h;
    float *c;          /* clearance (distance to T0's boundary), < 0 outside */
    float *fx, *fy;    /* nearest boundary point */
    unsigned char *ax; /* medial-axis cell */
    int *lab;          /* component label at the current threshold */
} AmGrid;

static void seg_near(double px, double py, double ax, double ay, double bx, double by, double *qx, double *qy) {
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = 0;
    if (l2 > 1e-24) { t = ((px - ax) * dx + (py - ay) * dy) / l2; if (t < 0) t = 0; if (t > 1) t = 1; }
    *qx = ax + t * dx; *qy = ay + t * dy;
}

/* clearance field of T0 (non-zero inside) and medial-axis cells: a cell is on the axis when its neighbour's nearest
   boundary point is more than 6 cells away from its own (two different boundary parts are equally near) */
static int am_grid_build(AmGrid *g, const CPaths *T0, double h) {
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    int nseg = 0;
    for (int i = 0; i < T0->n; i++) for (int k = 0; k < T0->p[i].n; k++) {
        double x = T0->p[i].xy[2 * k], y = T0->p[i].xy[2 * k + 1];
        if (x < lo[0]) lo[0] = x; if (y < lo[1]) lo[1] = y; if (x > hi[0]) hi[0] = x; if (y > hi[1]) hi[1] = y;
        nseg++;
    }
    if (!nseg) return 0;
    g->h = h; g->x0 = lo[0] - h; g->y0 = lo[1] - h;
    g->nx = (int)((hi[0] - lo[0]) / h) + 3; g->ny = (int)((hi[1] - lo[1]) / h) + 3;
    if ((double)g->nx * g->ny > 8e6) return 0;
    size_t N = (size_t)g->nx * g->ny;
    g->c = malloc(N * sizeof(float)); g->fx = malloc(N * sizeof(float)); g->fy = malloc(N * sizeof(float));
    g->ax = calloc(N, 1); g->lab = malloc(N * sizeof(int));
    if (!g->c || !g->fx || !g->fy || !g->ax || !g->lab) return 0;
    /* segments + a bucket grid over them */
    double *S = malloc(sizeof(double) * 4 * nseg); int ns = 0;
    for (int i = 0; i < T0->n; i++) { const CPath *p = &T0->p[i];
        for (int k = 0; k < p->n; k++) { int j = (k + 1) % p->n;
            S[4 * ns] = p->xy[2 * k]; S[4 * ns + 1] = p->xy[2 * k + 1]; S[4 * ns + 2] = p->xy[2 * j]; S[4 * ns + 3] = p->xy[2 * j + 1]; ns++; } }
    double B = 0.5; int bx = (int)((hi[0] - lo[0]) / B) + 1, by = (int)((hi[1] - lo[1]) / B) + 1;
    while ((double)bx * by > 2e5) { B *= 2; bx = (int)((hi[0] - lo[0]) / B) + 1; by = (int)((hi[1] - lo[1]) / B) + 1; }
    int *cnt = calloc((size_t)bx * by + 1, sizeof(int)), *idx;
    for (int pass = 0; pass < 2; pass++) {
        if (pass) { for (int i = 1; i <= bx * by; i++) cnt[i] += cnt[i - 1]; idx = malloc(sizeof(int) * (cnt[bx * by] + 1)); }
        int *fill = pass ? calloc((size_t)bx * by, sizeof(int)) : NULL;
        for (int s = 0; s < ns; s++) {
            double *q = S + 4 * s;
            int i0 = (int)((fmin(q[0], q[2]) - lo[0]) / B), i1 = (int)((fmax(q[0], q[2]) - lo[0]) / B);
            int j0 = (int)((fmin(q[1], q[3]) - lo[1]) / B), j1 = (int)((fmax(q[1], q[3]) - lo[1]) / B);
            for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
                int b = j * bx + i;
                if (!pass) cnt[b + 1]++; else idx[cnt[b] + fill[b]++] = s;
            }
        }
        free(fill);
    }
    /* inside test per row (non-zero winding over all loops) + nearest boundary point by ring search */
    double *xs = malloc(sizeof(double) * (ns + 1)); int *ws = malloc(sizeof(int) * (ns + 1));
    for (int j = 0; j < g->ny; j++) {
        double y = g->y0 + j * h; int nc = 0;
        for (int s = 0; s < ns; s++) { double *q = S + 4 * s;
            if ((q[1] <= y) != (q[3] <= y)) { xs[nc] = q[0] + (y - q[1]) * (q[2] - q[0]) / (q[3] - q[1]); ws[nc] = q[3] > q[1] ? 1 : -1; nc++; } }
        for (int a = 1; a < nc; a++) for (int b = a; b > 0 && xs[b] < xs[b - 1]; b--) {   /* insertion sort */
            double t = xs[b]; xs[b] = xs[b - 1]; xs[b - 1] = t; int w = ws[b]; ws[b] = ws[b - 1]; ws[b - 1] = w; }
        int k = 0, wind = 0;
        for (int i = 0; i < g->nx; i++) {
            double x = g->x0 + i * h;
            while (k < nc && xs[k] <= x) wind += ws[k++];
            size_t id = (size_t)j * g->nx + i;
            if (!wind) { g->c[id] = -1; g->fx[id] = g->fy[id] = 0; continue; }
            int ci = (int)((x - lo[0]) / B), cj = (int)((y - lo[1]) / B);
            double best = 1e300, bxp = x, byp = y;
            for (int ring = 0; ; ring++) {
                if ((ring - 1) * B > best && ring > 0) break;
                if (ring > bx + by) break;
                for (int jj = cj - ring; jj <= cj + ring; jj++) for (int ii = ci - ring; ii <= ci + ring; ii++) {
                    if (abs(jj - cj) != ring && abs(ii - ci) != ring) continue;
                    if (ii < 0 || jj < 0 || ii >= bx || jj >= by) continue;
                    int b = jj * bx + ii;
                    for (int e = cnt[b]; e < cnt[b + 1]; e++) { double *q = S + 4 * idx[e], qx, qy;
                        seg_near(x, y, q[0], q[1], q[2], q[3], &qx, &qy);
                        double d = hypot(qx - x, qy - y); if (d < best) { best = d; bxp = qx; byp = qy; } }
                }
            }
            g->c[id] = (float)best; g->fx[id] = (float)bxp; g->fy[id] = (float)byp;
        }
    }
    free(xs); free(ws); free(S); free(cnt); free(idx);
    static const int dd[4][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { -1, 1 } };
    for (int j = 0; j < g->ny; j++) for (int i = 0; i < g->nx; i++) {
        size_t a = (size_t)j * g->nx + i; if (g->c[a] <= 0) continue;
        for (int k = 0; k < 4; k++) {
            int i2 = i + dd[k][0], j2 = j + dd[k][1];
            if (i2 < 0 || j2 < 0 || i2 >= g->nx || j2 >= g->ny) continue;
            size_t b = (size_t)j2 * g->nx + i2; if (g->c[b] <= 0) continue;
            if (hypot(g->fx[a] - g->fx[b], g->fy[a] - g->fy[b]) > 6 * h) g->ax[a] = 1;
        }
    }
    return 1;
}
static void am_grid_free(AmGrid *g) { free(g->c); free(g->fx); free(g->fy); free(g->ax); free(g->lab); }

/* cells reachable from `seed` through 4-neighbours with c >= thr and (when `within` >= 0) mark == within;
   they get mark = stamp; returns the count and the cell of largest clearance in *best */
static int am_flood(AmGrid *g, int seed, double thr, int within, int stamp, int *stack, int *best) {
    int *mk = g->lab, sp = 0, n = 0;
    if (g->c[seed] < thr || (within >= 0 && mk[seed] != within)) return 0;
    *best = seed; stack[sp++] = seed; mk[seed] = stamp;
    while (sp) {
        int q = stack[--sp], i = q % g->nx, j = q / g->nx; n++;
        if (g->c[q] > g->c[*best]) *best = q;
        int nb[4] = { i > 0 ? q - 1 : -1, i < g->nx - 1 ? q + 1 : -1, j > 0 ? q - g->nx : -1, j < g->ny - 1 ? q + g->nx : -1 };
        for (int k = 0; k < 4; k++) { int b = nb[k];
            if (b < 0 || mk[b] == stamp || g->c[b] < thr || (within >= 0 && mk[b] != within)) continue;
            mk[b] = stamp; stack[sp++] = b; }
    }
    return n;
}

/* FUN_180555800 / FUN_180555490 (hmm-vorlib l.14828-14866 / 14716-14828) on the grid: from the axis points at
   clearance r of the component (cells with mark == L), follow the axis upwards for the straight-line distance e and
   take the clearance reached; the smallest over all start points. A branch that ends (a maximum / a node) before e
   gives no limit. Returns 1e300 when nothing limits. */
static double am_walk(const AmGrid *g, double r, double e, int L, int *queue, unsigned char *seen) {
    double h = g->h, gmin = 1e300;
    size_t N = (size_t)g->nx * g->ny;
    for (size_t s = 0; s < N; s++) {
        if (!g->ax[s] || g->lab[s] != L || fabs(g->c[s] - r) >= 0.75 * h) continue;
        double px = g->x0 + (s % g->nx) * h, py = g->y0 + (s / g->nx) * h;
        int qh = 0, qt = 0; double found = 1e300;
        queue[qt++] = (int)s; seen[s] = 1;
        while (qh < qt) {
            int a = queue[qh++], i = a % g->nx, j = a / g->nx;
            if (hypot(g->x0 + i * h - px, g->y0 + j * h - py) >= e) { if (g->c[a] < found) found = g->c[a]; continue; }
            for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
                int i2 = i + di, j2 = j + dj; if (i2 < 0 || j2 < 0 || i2 >= g->nx || j2 >= g->ny) continue;
                int b = j2 * g->nx + i2;
                if (seen[b] || !g->ax[b] || g->lab[b] != L || g->c[b] < g->c[a] - 0.2 * h) continue;
                seen[b] = 1; queue[qt++] = b;
            }
        }
        for (int k = 0; k < qt; k++) seen[queue[k]] = 0;
        if (found < gmin) gmin = found;
    }
    return gmin;
}

static void am_push(AreaMillPasses *ps, CPaths loops, double t) {
    if (ps->n == ps->cap) { ps->cap = ps->cap ? ps->cap * 2 : 16; ps->p = realloc(ps->p, sizeof(AreaMillPass) * ps->cap); }
    ps->p[ps->n].loops = loops; ps->p[ps->n].t = t; ps->n++;
}

static int am_inside(const CPath *c, double x, double y) {
    int in = 0;
    for (int i = 0, j = c->n - 1; i < c->n; j = i++) {
        double xi = c->xy[2 * i], yi = c->xy[2 * i + 1], xj = c->xy[2 * j], yj = c->xy[2 * j + 1];
        if ((yi > y) != (yj > y) && x < xj + (y - yj) * (xi - xj) / (yi - yj)) in = !in;
    }
    return in;
}

/* the pass at t of the component whose deepest point is (sx, sy): offset of T0 by t (FUN_180557f60 hmm-vorlib
   l.16248-16923 builds the loop from the offset spans of the boundary sites), the outer loop around the point
   with its holes */
static CPaths am_pass_loops(const CPaths *T0, double t, double sx, double sy, double arc_tol) {
    CPaths r = { calloc(1, sizeof(CPath)), 0 };
    CPaths o = t > 0 ? cl_offset_nz(T0, -t, arc_tol) : cl_offset_nz(T0, 0, arc_tol);
    int *own = NULL; CPaths gr = cl_group(&o, &own); cl_free(&o);
    int best = -1; double ba = 1e300;
    for (int i = 0; i < gr.n; i++) if (own[i] == i && am_inside(&gr.p[i], sx, sy)) {
        double a = fabs(cl_area(&gr.p[i])); if (a < ba) { ba = a; best = i; } }
    if (best >= 0) {
        r.p = realloc(r.p, sizeof(CPath) * gr.n);
        for (int i = 0; i < gr.n; i++) if (own[i] == best) { r.p[r.n++] = gr.p[i]; gr.p[i].xy = NULL; gr.p[i].n = 0; }
    }
    cl_free(&gr); free(own);
    return r;
}

typedef struct { const CPaths *T0; AmGrid *g; double s, e, tau, m, arc_tol; int *stack; unsigned char *seen;
                 int stamp; } AmCtx;

static void am_cell_xy(const AmGrid *g, int a, double *x, double *y) { *x = g->x0 + (a % g->nx) * g->h; *y = g->y0 + (a / g->nx) * g->h; }

/* one chain of FUN_180555960 (hmm-vorlib l.14866-15123): the component around cell `seed` above clearance lo,
   positions from r. Passes are appended to `out` inner first: the children of a split (processed first), then this
   chain's passes from the innermost outwards */
static void am_chain(AmCtx *X, double r, double lo, int seed, AreaMillPasses *out) {
    AmGrid *g = X->g; size_t N = (size_t)g->nx * g->ny;
    AreaMillPasses mine = { 0 };
    for (;;) {
        int best, P = ++X->stamp;
        if (!am_flood(g, seed, lo, -1, P, X->stack, &best)) break;
        /* the parts of the component above r: more than one = a split node; each part continues from r
           (hmm-vorlib l.15250-15260: both outgoing chains of a split start from the stored radius) */
        int kids[64], nk = 0, first = X->stamp + 1;
        for (size_t a = 0; a < N && nk < 64; a++) {
            if (g->lab[a] != P || g->c[a] < r) continue;
            int kb; X->stamp++;
            am_flood(g, (int)a, r, P, X->stamp, X->stack, &kb);
            kids[nk++] = kb;
        }
        (void)first;
        if (!nk) break;
        if (nk > 1) { for (int k = 0; k < nk; k++) am_chain(X, r, r, kids[k], out); break; }
        /* one component: re-mark it at threshold lo (the walk may use axis cells below r) */
        P = ++X->stamp; am_flood(g, seed, lo, -1, P, X->stack, &best);
        double hi = g->c[best], sx, sy; am_cell_xy(g, best, &sx, &sy);
        double m = fmin((hi - lo) / 3, X->m);                            /* m = min((hi - lo) / 3, RadAvoidBottleneck) l.14994 */
        if (r < lo + m) r = lo + m;                                      /* r = max(r, lo + m)  l.15004-15008 */
        if (r >= hi - m) break;                                          /* while r < hi - m    l.15011 */
        double gw = am_walk(g, r, X->e, P, X->stack, X->seen);
        if (gw > hi - m) gw = hi - m;                                  /* minimum starts at hi - m  l.15016 */
        double t = gw - X->tau; if (t < lo + m) t = lo + m;        /* l.15034-15038 */
        CPaths lp = am_pass_loops(X->T0, t, sx, sy, X->arc_tol);
        if (lp.n) am_push(&mine, lp, t); else cl_free(&lp);
        r = t + X->s;                                                    /* l.15070 */
        seed = best;
    }
    for (int i = mine.n - 1; i >= 0; i--) am_push(out, mine.p[i].loops, mine.p[i].t);
    free(mine.p);
}

int area_mill_passes(const CPaths *T0, double first_step, double step, double extra, double tau, double arc_tol, double unit,
                     AreaMillPasses *out) {
    memset(out, 0, sizeof *out);
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    for (int i = 0; i < T0->n; i++) for (int k = 0; k < T0->p[i].n; k++) {
        double x = T0->p[i].xy[2 * k], y = T0->p[i].xy[2 * k + 1];
        if (x < lo[0]) lo[0] = x; if (y < lo[1]) lo[1] = y; if (x > hi[0]) hi[0] = x; if (y > hi[1]) hi[1] = y; }
    if (lo[0] > hi[0]) return 0;
    double h = sqrt((hi[0] - lo[0]) * (hi[1] - lo[1]) / 2.0e6); if (h < 0.01 * unit) h = 0.01 * unit;   /* grid (OURS): >= 0.01 mm */
    AmGrid g; memset(&g, 0, sizeof g);
    if (!am_grid_build(&g, T0, h)) { am_grid_free(&g); return 0; }
    size_t N = (size_t)g.nx * g.ny;
    for (size_t a = 0; a < N; a++) g.lab[a] = 0;
    AmCtx X = { T0, &g, step, extra, tau, 0.00022 * unit, arc_tol,   /* RadAvoidBottleneck default 0.00022 mm (formats/area_mill.md 11.2) */ malloc(sizeof(int) * N), calloc(N, 1), 0 };
    /* every component of T0: its chain from the first step (hmm-vorlib l.14934: a boundary loop starts at param_6),
       then the boundary pass t = 0 (FUN_180556530 hmm-vorlib l.15219) as the outermost one */
    for (size_t a = 0; a < N; a++) {
        if (g.c[a] < 1e-9 || g.lab[a] != 0) continue;   /* the flood's own threshold: a cell in (0, 1e-9) left `best` unset (a crash) */
        int best, C = ++X.stamp;
        am_flood(&g, (int)a, 1e-9, -1, C, X.stack, &best);
        double sx, sy; am_cell_xy(&g, best, &sx, &sy);
        am_chain(&X, first_step, 0, best, out);
        CPaths b = am_pass_loops(T0, 0, sx, sy, arc_tol);
        if (b.n) am_push(out, b, 0); else cl_free(&b);
        /* keep the cells of this component marked non-zero so the scan skips them */
        int C2 = ++X.stamp; am_flood(&g, best, 1e-9, -1, C2, X.stack, &best);
    }
    free(X.stack); free(X.seen); am_grid_free(&g);
    return 1;
}

void area_mill_passes_free(AreaMillPasses *ps) {
    for (int i = 0; i < ps->n; i++) cl_free(&ps->p[i].loops);
    free(ps->p); memset(ps, 0, sizeof *ps);
}

/* FUN_18004cf60 "MeasureCurveDiameter" (decomp/hmm_meas: FUN_18004cb90 keeps the largest distance between antipodal
   points of the curve): the largest distance between two of its points */
double area_mill_curve_diameter(const CPath *c) {
    double best = 0;
    for (int i = 0; i < c->n; i++) for (int j = i + 1; j < c->n; j++) {
        double d = hypot(c->xy[2 * i] - c->xy[2 * j], c->xy[2 * i + 1] - c->xy[2 * j + 1]); if (d > best) best = d; }
    return best;
}
