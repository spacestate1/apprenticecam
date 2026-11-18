/* nc_dev - max 3D deviation between the feed paths of two G-code programs (G1/G2/G3, R-format arcs, helical Z).
 * Both are densified to 0.01 mm; reports the larger directed max nearest-point distance (accurate to ~0.005 mm).
 * Usage: nc_dev A.nc B.nc */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { double x, y, z; } P;
typedef struct { P *p; long n, cap; } Pts;
static const double STEP = 0.01;
static void add(Pts *a, double x, double y, double z) {
    if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 65536; a->p = realloc(a->p, a->cap * sizeof(P)); }
    a->p[a->n++] = (P){ x, y, z };
}
static int word(const char *l, char k, double *v) {
    for (const char *q = l; *q && *q != '('; q++) if (*q == k && (q == l || q[-1] == ' ')) { *v = atof(q + 1); return 1; }
    return 0;
}
static Pts load(const char *fn) {
    Pts a = { 0 }; FILE *f = fopen(fn, "r"); if (!f) { perror(fn); exit(1); }
    char line[512]; double X = 0, Y = 0, Z = 0; int g = 0;
    while (fgets(line, sizeof line, f)) {
        double v, x = X, y = Y, z = Z, r = 0; int has = 0, hr;
        for (const char *q = line; *q && *q != '('; q++) if (*q == 'G' && (q == line || q[-1] == ' ')) { int gg = atoi(q + 1); if (gg >= 0 && gg <= 3) g = gg; }
        if (word(line, 'X', &v)) { x = v; has = 1; }
        if (word(line, 'Y', &v)) { y = v; has = 1; }
        if (word(line, 'Z', &v)) { z = v; has = 1; }
        hr = word(line, 'R', &r);
        if (!has) continue;
        if (g != 0) {
            if (!a.n || a.p[a.n - 1].x != X || a.p[a.n - 1].y != Y || a.p[a.n - 1].z != Z) add(&a, X, Y, Z);
            if ((g == 2 || g == 3) && hr) {
                double d = hypot(x - X, y - Y);
                if (d > 1e-12) {
                    double h = sqrt(fmax(r * r - d * d / 4, 0)), mx = (X + x) / 2, my = (Y + y) / 2, ux = (x - X) / d, uy = (y - Y) / d;
                    int s = g == 3 ? 1 : -1; if (r < 0) s = -s;
                    double cx = mx - s * uy * h, cy = my + s * ux * h, a0 = atan2(Y - cy, X - cx), a1 = atan2(y - cy, x - cx), sw;
                    if (g == 3) { sw = fmod(a1 - a0 + 4 * M_PI, 2 * M_PI); } else { sw = -fmod(a0 - a1 + 4 * M_PI, 2 * M_PI); }
                    int n = (int)(fabs(sw) * fabs(r) / STEP) + 2;
                    for (int i = 1; i <= n; i++) { double t = (double)i / n; add(&a, cx + fabs(r) * cos(a0 + sw * t), cy + fabs(r) * sin(a0 + sw * t), Z + (z - Z) * t); }
                }
            } else {
                double L = sqrt((x - X) * (x - X) + (y - Y) * (y - Y) + (z - Z) * (z - Z)); int n = (int)(L / STEP) + 1;
                for (int i = 1; i <= n; i++) { double t = (double)i / n; add(&a, X + (x - X) * t, Y + (y - Y) * t, Z + (z - Z) * t); }
            }
        }
        X = x; Y = y; Z = z;
    }
    fclose(f); return a;
}
/* uniform grid hash over B */
typedef struct { double h, x0, y0, z0; int nx, ny, nz; long *start, *idx; } Grid;
static Grid build(const Pts *b) {
    Grid g; g.h = 0.1; double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
    for (long i = 0; i < b->n; i++) { double *q = &b->p[i].x; for (int k = 0; k < 3; k++) { lo[k] = fmin(lo[k], q[k]); hi[k] = fmax(hi[k], q[k]); } }
    g.x0 = lo[0]; g.y0 = lo[1]; g.z0 = lo[2];
    g.nx = (int)((hi[0] - lo[0]) / g.h) + 1; g.ny = (int)((hi[1] - lo[1]) / g.h) + 1; g.nz = (int)((hi[2] - lo[2]) / g.h) + 1;
    long nc = (long)g.nx * g.ny * g.nz; g.start = calloc(nc + 1, sizeof(long)); g.idx = malloc(b->n * sizeof(long));
    long *cell = malloc(b->n * sizeof(long));
    for (long i = 0; i < b->n; i++) { int cx = (int)((b->p[i].x - g.x0) / g.h), cy = (int)((b->p[i].y - g.y0) / g.h), cz = (int)((b->p[i].z - g.z0) / g.h);
        cell[i] = ((long)cz * g.ny + cy) * g.nx + cx; g.start[cell[i] + 1]++; }
    for (long c = 0; c < nc; c++) g.start[c + 1] += g.start[c];
    long *fill = malloc(nc * sizeof(long)); memcpy(fill, g.start, nc * sizeof(long));
    for (long i = 0; i < b->n; i++) g.idx[fill[cell[i]]++] = i;
    free(fill); free(cell); return g;
}
static double nearest(const Grid *g, const Pts *b, P q) {
    int cx = (int)floor((q.x - g->x0) / g->h), cy = (int)floor((q.y - g->y0) / g->h), cz = (int)floor((q.z - g->z0) / g->h);
    for (int r = 1; r < 100000; r++) {
        double best = 1e300;
        for (int k = cz - r; k <= cz + r; k++) for (int j = cy - r; j <= cy + r; j++) for (int i = cx - r; i <= cx + r; i++) {
            if (i < 0 || j < 0 || k < 0 || i >= g->nx || j >= g->ny || k >= g->nz) continue;
            long c = ((long)k * g->ny + j) * g->nx + i;
            for (long t = g->start[c]; t < g->start[c + 1]; t++) { P p = b->p[g->idx[t]];
                double d = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z); if (d < best) best = d; }
        }
        if (best < 1e300 && sqrt(best) <= (r) * g->h) return sqrt(best);   /* anything closer would be within r cells */
        if (r > 50 && best < 1e300) return sqrt(best);
    }
    return 1e300;
}
static double directed(const Pts *a, const Pts *b, P *where) {
    Grid g = build(b); double m = 0;
    for (long i = 0; i < a->n; i++) { double d = nearest(&g, b, a->p[i]); if (d > m) { m = d; *where = a->p[i]; } }
    free(g.start); free(g.idx); return m;
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: nc_dev A.nc B.nc\n"); return 1; }
    Pts a = load(argv[1]), b = load(argv[2]); P wa = { 0 }, wb = { 0 };
    double dab = directed(&a, &b, &wa), dba = directed(&b, &a, &wb);
    printf("points %ld / %ld; A->B %.4f at (%.3f %.3f %.3f), B->A %.4f at (%.3f %.3f %.3f)\n", a.n, b.n, dab, wa.x, wa.y, wa.z, dba, wb.x, wb.y, wb.z);
    printf("max deviation: %.4f mm\n", fmax(dab, dba));
    return 0;
}
