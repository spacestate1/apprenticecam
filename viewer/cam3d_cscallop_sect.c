/* cam3d_cscallop_sect.c - Constant Scallop (op 43) stage B: the per-line sections of the scanner FUN_1801ea7e0's edge cylinders and corner
 * spheres (FUN_1801db8c0's edge and corner loops, formats/surf_classic3.md 12.13). Sources: decomp/mcmsurf_all.c (l.N) and objdump of
 * mcmsurf.dll (asm addresses); mccore's UnitNormal (1804b7480, decomp/mccore_filter.c), proj_pt_ln_3d (1804b42c0,
 * decomp/draft/core_calcdim.c l.5703), proj_pt_ln (180002780, decomp/mccore_filter.c l.4912), lp_ints (1804b3ed0, decomp/mccore_inters.c
 * l.2806). Every floating-point expression keeps the DLL's operand order and association (build with -ffp-contract=off). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

int cs_piece_emit(CsScan *S, const CsPiece *p);
long cs_cov2[32];   /* branch counts for the unit oracle's coverage (cs_port prints them with their names, cs_cov2_name) */
const char *cs_cov2_name[32] = { "flat edges", "refitted arcs", "edge pieces 0", "edge pieces 4", "edge pieces 1", "edge pieces 2",
    "edge pieces 3", "edge spans 5", "cap point", "cap plane", "cap failed", "flat sections", "corner sections", "corner pieces",
    "near pieces", "corner cut 3", "corner cut 2", "corner cut 0", "inner prev", "inner cur", "flagged quads", "vertical edges", NULL };

/* FUN_1801db170 (l.316146): the piece's end normals - from the nearest point of the edge segment (its midpoint when the edge is not longer
   than the system tolerance) to the end (pos, s, z) */
static void edge_nrm(const CsScan *S, const CsEdge *N, CsPiece *p)
{
    for (int k = 0; k < 2; k++) {
        double x = S->pos, y = p->s[k], z = p->z[k], f[3];
        if (N->len <= S->ctol) {
            f[0] = (N->P2[0] + N->P1[0]) * 0.5; f[1] = (N->P2[1] + N->P1[1]) * 0.5; f[2] = (N->P2[2] + N->P1[2]) * 0.5;
        } else {
            double t = ((y - N->P1[1]) * (N->P2[1] - N->P1[1]) + (x - N->P1[0]) * (N->P2[0] - N->P1[0]) + (z - N->P1[2]) * (N->P2[2] - N->P1[2]))
                       / (N->len * N->len);
            double u = 1.0 - t;
            f[1] = u * N->P1[1] + t * N->P2[1];
            f[0] = u * N->P1[0] + t * N->P2[0];
            f[2] = u * N->P1[2] + t * N->P2[2];
        }
        double dx = x - f[0], dy = y - f[1], dz = z - f[2], l = sqrt(dy * dy + dx * dx + dz * dz);
        if (l == 0.0) { p->n[k][0] = p->n[k][1] = p->n[k][2] = 0.0; }
        else { p->n[k][0] = dx / l; p->n[k][1] = dy / l; p->n[k][2] = dz / l; }
    }
}
/* UnitNormal (mccore 1804b7480): a x b normalised; the error code (0 = fine) */
static int unit_normal(const CsScan *S, const double *a, const double *b, double *o)
{
    double ox = b[2] * a[1] - b[1] * a[2], oy = b[0] * a[2] - a[0] * b[2], oz = a[0] * b[1] - b[0] * a[1];
    double l = sqrt(ox * ox + oy * oy + oz * oz), tl = S->ctol;
    int err = 0;
    if (l < tl) {
        double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        if (tl <= la) { double lb = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]); err = (tl <= lb) + 2; } else err = 1;
    }
    double d = l < 1e-12 ? 1e-12 : l;                     /* 0x3d719799812dea11 */
    o[0] = ox / d; o[1] = oy / d; o[2] = oz / d;
    return err;
}
/* FUN_180274990 (asm 180274990-180274b62): the line where the planes (n1, p1) and (n2, p2) meet: L[0..2] a point, L[3..5] = it + the unit
   direction. 0 when UnitNormal fails */
static int plane_plane(const CsScan *S, const double *n1, const double *p1, const double *n2, const double *p2, double *L)
{
    double u[3];
    if (unit_normal(S, n1, n2, u)) return 0;
    double d1 = n1[1] * p1[1] + n1[0] * p1[0] + n1[2] * p1[2];
    double d2 = n2[1] * p2[1] + n2[0] * p2[0] + n2[2] * p2[2];
    double c = n2[0] * n1[0] + n2[1] * n1[1] + n2[2] * n1[2];
    double den = c * c - 1.0, a = (c * d2 - d1) / den, b = (c * d1 - d2) / den;
    L[0] = n1[0] * a + n2[0] * b; L[1] = n1[1] * a + n2[1] * b; L[2] = n1[2] * a + n2[2] * b;
    L[3] = u[0] + L[0]; L[4] = u[1] + L[1]; L[5] = u[2] + L[2];
    return 1;
}
/* proj_pt_ln_3d (mccore 1804b42c0) */
static int proj3(const CsScan *S, const double *P, const double *L, double *o)
{
    double dx = L[3] - L[0], dy = L[4] - L[1], dz = L[5] - L[2];
    double l = sqrt(dy * dy + dx * dx + dz * dz);
    if (!(S->ctol <= l)) return 0;
    double u = ((P[1] - L[1]) * dy + (P[0] - L[0]) * dx + (P[2] - L[2]) * dz) / (l * l);
    double w = 1.0 - u;
    o[1] = w * L[1] + u * L[4]; o[0] = w * L[0] + u * L[3]; o[2] = w * L[2] + u * L[5];
    return 1;
}
/* proj_pt_ln (mccore 180002780): P (2D) on the line L (a, b) */
static int proj2(const double *P, const double *L, double *o, double *t)
{
    double dx = L[2] - L[0], dy = L[3] - L[1], d = dy * dy + dx * dx;
    if (d == 0.0) return 0;
    double u = ((P[1] - L[1]) * dy + (P[0] - L[0]) * dx) / d;
    *t = u;
    o[1] = (1.0 - u) * L[1] + u * L[3];
    o[0] = (1.0 - u) * L[0] + u * L[2];
    return 1;
}
/* lp_ints (mccore 1804b3ed0): the line L met with the plane (n, p) */
static int lp_ints(const CsScan *S, const double *n, const double *p, const double *L, double *o)
{
    double tl = S->ctol;
    double dx = L[3] - L[0], dy = L[4] - L[1], dz = L[5] - L[2];
    double d = n[1] * dy + n[0] * dx + n[2] * dz;
    if (fabs(d) <= tl * tl && tl * tl != fabs(d)) {
        double ln = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), ll = sqrt(dx * dx + dy * dy + dz * dz);
        if (ln == 0.0 || ll == 0.0 || fabs(d) / (ll * ln) < tl) return 0;
    }
    double t = (n[0] * (p[0] - L[0]) + n[1] * (p[1] - L[1]) + n[2] * (p[2] - L[2])) / d;
    o[1] = dy * t + L[1]; o[0] = dx * t + L[0]; o[2] = dz * t + L[2];
    return 1;
}
/* FUN_1801ecfb0 (l.327190): the end cap circle (radius 588 about P1 (which 1) or P2, in the plane normal to the edge) met with the line's
   plane x = pos: 0 none, 1 touching (one point), 2 two points ((y, z) pairs) */
static int cap_pts(const CsScan *S, const CsEdge *N, int which, double *o)
{
    const double *P = which == 1 ? N->P1 : N->P2;
    double n1[3] = { 1.0, 0.0, 0.0 }, p1[3] = { S->pos, 0.0, 0.0 }, L[6], q[3];
    if (!plane_plane(S, n1, p1, N->dir, P, L)) return 0;
    if (!proj3(S, P, L, q)) return 0;
    double dx = q[0] - P[0], dy = q[1] - P[1], dz = q[2] - P[2];
    double d = sqrt(dy * dy + dx * dx + dz * dz);
    double h2 = S->off * S->off - d * d;
    if (h2 <= 1e-12) { o[0] = q[1]; o[1] = q[2]; return 1; }
    double h = sqrt(h2), ey = (L[4] - L[1]) * h, ez = (L[5] - L[2]) * h;
    o[0] = ey + q[1]; o[1] = ez + q[2]; o[2] = q[1] - ey; o[3] = q[2] - ez;
    return 2;
}
/* FUN_1801ed4a0 (mcmsurf 1801ed4a0): a piece end beyond an edge end (cls 3 before P1, 4 after P2) moved onto that end's cap: the cap point
   whose foot on the piece's line lies within it (t in (-2e-7, 1 + 2e-7)), the nearest -> 1 / 2; else the line met with the end's plane
   -> 0; 99 failed */
static int cap_end(const CsScan *S, const CsEdge *N, const double *p, int cls, const double *other, double *o)
{
    int w;
    if (cls == 3) w = 1; else if (cls == 4) w = 2; else return 99;
    double c[4];
    if (cap_pts(S, N, w, c) == 2) {
        double L[4] = { p[0], p[1], other[0], other[1] }, best = 1e38;
        for (int k = 0; k < 2; k++) {
            double q[2], t;
            if (proj2(c + 2 * k, L, q, &t) && -2e-07 < t && t < 1.0000002) {
                double ex = q[0] - c[2 * k], d = (q[1] - c[2 * k + 1]) * (q[1] - c[2 * k + 1]) + ex * ex;
                if (d < best) { o[0] = c[2 * k]; o[1] = c[2 * k + 1]; best = d; }
            }
        }
        if (best < 1e38) { cs_cov2[8]++; return (cls != 3) + 1; }
    }
    double L[6] = { S->pos, p[0], p[1], S->pos, other[0], other[1] }, q[3];
    if (!lp_ints(S, N->dir, w == 1 ? N->P1 : N->P2, L, q)) { cs_cov2[10]++; return 99; }
    o[0] = q[1]; o[1] = q[2];
    cs_cov2[9]++;
    return 0;
}
/* FUN_1801e7440 (asm 1801e7440): where (pos, y, z) projects on the edge: 1 at P1, 3 before, 2 at P2, 4 after, 0 between */
static int edge_cls(const CsScan *S, const CsEdge *N, const double *p)
{
    double d = (p[0] - N->P1[1]) * N->dir[1] + (S->pos - N->P1[0]) * N->dir[0] + (p[1] - N->P1[2]) * N->dir[2];
    if (fabs(d) < 1e-12) return 1;
    if (0.0 > d) return 3;
    if (fabs(d - N->len) < 1e-12) return 2;
    return d > N->len ? 4 : 0;
}
/* FUN_1801e0050 (asm 1801e0050-1801e0160): the piece between two ellipse points by their classes (FUN_1801e7400 = 5 a + b); 0-4 a piece,
   5 none, 99 failed */
static int edge_span(const CsScan *S, const CsEdge *N, const double *a, int ca, const double *b, int cb, double *o1, double *o2)
{
    o1[0] = a[0]; o1[1] = a[1]; o2[0] = b[0]; o2[1] = b[1];
    if (ca >= 5 || cb >= 5) return 99;
    int r, both = 0, k;
    switch (ca * 5 + cb) {
    case 0: case 1: case 2: case 5: case 7: case 10: case 11: return 0;
    case 6: case 12: return 4;
    case 8: case 14: case 16: case 18: case 22: case 24: return 5;
    case 3: case 4: case 9: case 13: r = 1; goto second;
    case 19: case 23: both = 1; r = 3; break;
    case 15: case 17: case 20: case 21: r = 2; break;
    default: return 99;
    }
    k = cap_end(S, N, a, ca, b, o1);
    if (k > 2) return (unsigned char)(k - 3) > 1 ? 99 : 5;
    if (!both) return r;
second:
    k = cap_end(S, N, b, cb, a, o2);
    if (k > 2) return (unsigned char)(k - 3) > 1 ? 99 : 5;
    return r;
}
/* FUN_1801e1ac0 (asm 1801e1ac0-1801e1ed5): the ellipse of a kind 0 / 1 edge at x = pos stepped over its arc; consecutive points make
   pieces (code 4 drive, 0x16 check) through edge_span. 1 fine, 0 failed */
static int edge_sect(CsScan *S, const CsEdge *N)
{
    double A = N->a, B = N->b;
    double t = (S->pos - N->P1[0]) / N->dir[0];
    double X = N->c78 * A - N->s80 * B, Y = N->s80 * A + N->c78 * B;
    double cz = t * N->dir[2] + N->P1[2], cy = t * N->dir[1] + N->P1[1];
    double prev[2] = { X + cy, Y + cz };
    int pc = edge_cls(S, N, prev);
    short code = N->kind == 0 ? 4 : 0x16;
    for (unsigned short i = 0; i < N->nst; i++) {
        double A2 = A * N->c90 - N->s98 * B;
        double B2 = A * N->sa0 + B * N->c90;
        double X2 = A2 * N->c78 - B2 * N->s80, Y2 = A2 * N->s80 + B2 * N->c78;
        double cur[2] = { X2 + cy, cz + Y2 };
        int cc = edge_cls(S, N, cur);
        double o1[2], o2[2];
        int r = edge_span(S, N, prev, pc, cur, cc, o1, o2);
        if (r <= 5) cs_cov2[r == 0 ? 2 : r == 4 ? 3 : r == 1 ? 4 : r == 2 ? 5 : r == 3 ? 6 : 7]++;
        if (r < 5) {
            CsPiece p; memset(&p, 0, sizeof p);
            p.code = code; p.s[0] = o1[0]; p.s[1] = o2[0]; p.z[0] = o1[1]; p.z[1] = o2[1];
            edge_nrm(S, N, &p);
            cs_piece_emit(S, &p);
        }
        if (r > 5) return 0;
        prev[0] = cur[0]; prev[1] = cur[1]; pc = cc; A = A2; B = B2;
    }
    return 1;
}
/* FUN_1801e2030 (asm 1801e2030..): a kind 8 / 9 edge: one piece between the caps, each cap's higher point. 0 failed */
static int edge_sect_flat(CsScan *S, const CsEdge *N)
{
    double a[4] = { 0, 0, 0, 0 }, b[4] = { 0, 0, 0, 0 };
    int ca = cap_pts(S, N, 1, a), cb = cap_pts(S, N, 2, b);
    if (!ca || !cb) return 0;
    int ia = ca == 2 ? (a[1] <= a[3]) : (ca == 1 ? 0 : 2);
    int ib = cb == 2 ? (b[1] <= b[3]) : (cb == 1 ? 0 : 2);
    if (ia >= 2 || ib >= 2) return 0;
    CsPiece p; memset(&p, 0, sizeof p);
    p.code = N->kind == 8 ? 4 : 0x16;
    cs_cov2[11]++;
    p.s[0] = a[2 * ia]; p.z[0] = a[2 * ia + 1]; p.s[1] = b[2 * ib]; p.z[1] = b[2 * ib + 1];
    edge_nrm(S, N, &p);
    cs_piece_emit(S, &p);
    return 1;
}
/* FUN_1801db8c0's edge loop (asm 1801db8e4-1801db98f): an edge whose x range ends before the window leaves the list (FUN_1801d9fe0); one
   whose range starts at or before hi is sectioned (588 / 590 by its kind, FUN_1801da0c0 / FUN_1801da130). 0, or -1 (error 0x1ce8) */
int cs_sect_edges(CsScan *S)
{
    CsEdge *prev = NULL, *N = S->eact;
    while (N) {
        CsEdge *nx = N->next;
        if (S->lo > N->xmax) {
            if (prev) prev->next = nx; else S->eact = nx;
            N->next = S->epool; S->epool = N;
            N = nx; continue;
        }
        if (!(S->hi < N->xmin)) {
            if (N->kind & 1) { S->off = S->off_chk; S->hoff = S->hoff_chk; } else { S->off = S->off_drive; S->hoff = S->hoff_drive; }
            if (!(N->kind > 1 ? edge_sect_flat(S, N) : edge_sect(S, N))) return -1;
        }
        prev = N; N = nx;
    }
    return 0;
}

/* ------------------------------------------------------------------ the corner spheres */
/* FUN_1801e9990 (asm 1801e9990-1801e99e6): the number of points of an arc of angle a on a circle of radius r within the deviation dev */
static int arc_cnt(double r, double a, double dev)
{
    int n = 2;
    if (r > dev) { n = (int)ceil(fabs(a / (acos((r - dev) / r) * 2.0))); if (n < 2) n = 2; }
    return n + 1;
}
/* FUN_1801e14c0 (l.320004): n points of the unit half circle from (1, 0), stepped by the turn pi / (n - 1) */
static void half_circle(int n, double *o)
{
    double a = 3.141592653589793 / (double)(n - 1), c = cos(a), s = sin(a);
    o[0] = 1.0; o[1] = 0.0;
    for (int k = 1; k < n; k++) {
        double x = o[2 * k - 2], y = o[2 * k - 1];
        o[2 * k] = c * x - s * y;
        o[2 * k + 1] = s * x + c * y;
    }
}
/* the table DAT_180501dc0 (the drive surfaces' corners): FUN_1801da800(100, r = 5a0, msurf corner deviation) (l.326251; 315842),
   FUN_1801d9e20(2, ..) (l.326287; 315122: the room, 100 x FUN_1801e9990(r, pi, dev) points), FUN_1801db440 (l.326394, asm 1801db440-
   1801db5b7: level i at the height h = i r / 99 has the circle radius sqrt(r^2 - h^2) (h accumulated), its half circle of
   FUN_1801e9990(radius, pi, dev) points, shared with the level before when the count is the same). 0, or -1 (error 0x1518: no room) */
int cs_arc_setup(CsScan *S)
{
    CsArcs *A = &S->arc;
    free(A->pts); memset(A, 0, sizeof *A);
    A->nlev = 100; A->r = S->off_drive; A->cdev = S->cdev;
    A->step = A->r / (double)(int)(100 - 1);
    int room = A->nlev * arc_cnt(A->r, 3.141592653589793, A->cdev);
    A->pts = calloc((size_t)room * 2 + 2, sizeof(double));
    int used = 0, last = -1, base = 0;
    double r = A->r, h = 0.0;
    for (int i = 0; i < A->nlev; i++) {
        int n = arc_cnt(r, 3.141592653589793, A->cdev);
        if (n == last) { A->cnt[i] = A->cnt[i - 1]; A->off[i] = A->off[i - 1]; }
        else {
            used += n;
            if (used > room) return -1;
            half_circle(n, A->pts + 2 * base);
            A->cnt[i] = n; A->off[i] = base; base += n;
        }
        A->rad[i] = r;
        h = h + A->step;
        if (h > A->r) break;
        r = sqrt(A->r * A->r - h * h);
        last = n;
    }
    A->npts = base;
    return 0;
}
/* FUN_1801da6b0 (l.315632): the level for the distance d from the sphere's centre plane: ceil(d / step), at most nlev - 1, one lower when
   that level has fewer points than the one before */
static const double *arc_level(const CsArcs *A, double d, int *cnt)
{
    double q = ceil(d / A->step);
    int top = A->nlev - 1, i = (int)q, neg = 0;
    if (top < i) { neg = top < 0; i = top; }
    else if (i < 0) { i = 0; goto out; }
    if (i != 0 && !neg && A->cnt[i] < A->cnt[i - 1]) i--;
out:
    *cnt = A->cnt[i];
    return A->cnt[i] ? A->pts + 2 * A->off[i] : NULL;
}
/* FUN_1801e4200 (asm 1801e4200-..): a sphere piece's end normals (from the centre), then DAT_180501d90 */
static void corner_emit(CsScan *S, const CsCorner *C, CsPiece *p)
{
    for (int k = 0; k < 2; k++) {
        double dx = S->pos - C->P[0], dy = p->s[k] - C->P[1], dz = p->z[k] - C->P[2];
        double l = sqrt(dx * dx + dy * dy + dz * dz);
        if (l == 0.0) { p->n[k][0] = p->n[k][1] = p->n[k][2] = 0.0; }
        else { p->n[k][0] = dx / l; p->n[k][1] = dy / l; p->n[k][2] = dz / l; }
    }
    cs_piece_emit(S, p);
}
/* FUN_1801dfd10 (l.319032): where the sphere's section arc from a (outside) to b crosses the corner's side plane which (1: normal d1,
   2: d2): the cap circle of that plane (FUN_1801ecfb0 on a node at P with the unit d) - of its two points the one whose foot on the chord
   a b lies within it, the nearest; 0 when none. Then 3 when that point is at or before P along the other side's direction (FUN_1801e7440
   class 1 or 3 on a node P, that unit d, length 1), else 2 */
static int corner_cut(const CsScan *S, int which, const CsCorner *C, const double *a, const double *b, double *o)
{
    CsEdge F; memset(&F, 0, sizeof F);
    const double *d = which == 1 ? C->d1 : C->d2, *e = which == 1 ? C->d2 : C->d1;
    memcpy(F.P1, C->P, 24);
    double x = d[0], y = d[1], z = d[2], l = sqrt(x * x + y * y + z * z);
    if (l == 0.0) { F.dir[0] = F.dir[1] = F.dir[2] = 0.0; } else { F.dir[0] = x / l; F.dir[2] = z / l; F.dir[1] = y / l; }
    double c[4];
    if (cap_pts(S, &F, 1, c) != 2) return 0;
    double L[4] = { a[1], a[2], b[1], b[2] }, best = 1e38;
    for (int k = 0; k < 2; k++) {
        double q[2], t;
        if (proj2(c + 2 * k, L, q, &t) && -2e-07 < t && t < 1.0000002) {
            double ex = q[0] - c[2 * k], dd = (q[1] - c[2 * k + 1]) * (q[1] - c[2 * k + 1]) + ex * ex;
            if (dd < best) { o[0] = c[2 * k]; o[1] = c[2 * k + 1]; best = dd; }
        }
    }
    if (!(best < 1e38)) { cs_cov2[17]++; return 0; }
    CsEdge G; memset(&G, 0, sizeof G);
    memcpy(G.P1, C->P, 24);
    x = e[0]; y = e[1]; z = e[2]; l = sqrt(x * x + y * y + z * z);
    if (l == 0.0) { G.dir[0] = G.dir[1] = G.dir[2] = 0.0; } else { G.dir[0] = x / l; G.dir[2] = z / l; G.dir[1] = y / l; }
    G.len = 1.0;
    int k = edge_cls(S, &G, o);
    cs_cov2[((unsigned char)(k - 1) & 0xfd) == 0 ? 15 : 16]++;
    return ((unsigned char)(k - 1) & 0xfd) == 0 ? 3 : 2;
}
/* FUN_1801ec6b0 (asm 1801ec6b0-1801ecfaa): the section of a corner's sphere (radius 588) with the plane x = pos - a circle of radius rho,
   its points from the arc table; per arc step the part inside the corner's wedge (outside both side planes: d1 . p >= 0 and d2 . p >= 0)
   becomes pieces (code 0xb drive, 0x1d check), the wedge crossings moved onto the side planes (corner_cut) or onto the inner circle of
   radius rho - t90 (rho / 2 when rho <= t90); when no piece came out, the steps near a side plane (|d . p| < 2 t90, at most 10) */
static void corner_sect(CsScan *S, const CsCorner *C)
{
    double t90 = S->t90, dx = S->pos - C->P[0], tt = t90 + t90;
    if (fabs(dx) > S->off) return;
    double rho = sqrt(S->off * S->off - dx * dx), ri;
    if (rho > t90) ri = rho - t90; else ri = rho * 0.5;
    int cnt;
    const double *T = arc_level(&S->arc, fabs(dx), &cnt);   /* kind 2; kind 3 would use DAT_1805025f0 (check surfaces: none) */
    short code = C->kind == 2 ? 0xb : 0x1d;
    if (cnt < 2) return;
    cs_cov2[12]++;
    double d1[3], d2[3];
    {
        double x = C->d1[0], y = C->d1[1], z = C->d1[2], l = sqrt(x * x + y * y + z * z);   /* FUN_18002fbd0 */
        if (l == 0.0) { d1[0] = d1[1] = d1[2] = 0.0; } else { d1[0] = x / l; d1[1] = y / l; d1[2] = z / l; }
        x = C->d2[0]; y = C->d2[1]; z = C->d2[2]; l = sqrt(x * x + y * y + z * z);
        if (l == 0.0) { d2[0] = d2[1] = d2[2] = 0.0; } else { d2[0] = x / l; d2[1] = y / l; d2[2] = z / l; }
    }
    double y0 = rho * T[0], z0 = rho * T[1];
    double e1 = d1[0] * dx + d1[1] * y0 + d1[2] * z0;      /* dot1 / dot2 of the previous point */
    double e2 = d2[0] * dx + d2[1] * y0 + d2[2] * z0;
    int p1 = 0.0 > e1, p2 = 0.0 > e2;
    double prv[3] = { S->pos, y0 + C->P[1], z0 + C->P[2] }; /* local_5a0 */
    double A2[2] = { prv[1], prv[2] };                    /* local_558 */
    int emitted = 0, nnear = 0;
    CsPiece nearp[10];
    for (int i = 1; i < cnt; i++) {
        double ae1 = fabs(e1);
        double cur[3], B2[2];                             /* local_580, local_540 */
        double Yr = rho * T[2 * i], Zr = rho * T[2 * i + 1];
        double f2 = Yr * d2[1] + dx * d2[0] + Zr * d2[2];
        double f1 = Yr * d1[1] + dx * d1[0] + Zr * d1[2];
        int n1 = 0.0 > f1;
        cur[0] = S->pos; cur[1] = Yr + C->P[1];
        int n2 = 0.0 > f2;
        cur[2] = C->P[2] + Zr;
        B2[0] = cur[1]; B2[1] = cur[2];
        if ((tt > ae1 || tt > fabs(e2) || tt > fabs(f1) || tt > fabs(f2)) && nnear < 10) {
            CsPiece *q = &nearp[nnear++]; memset(q, 0, sizeof *q);
            q->code = code; q->s[0] = prv[1]; q->z[0] = prv[2]; q->s[1] = cur[1]; q->z[1] = cur[2];
        }
        int bl = 0, in2 = 0, in1 = 0, cap2 = 0, cap1 = 0, both_in = 0, both_out = 0, dl;
        if (p1 && p2) { in1 = 1; both_in = 1; bl = 1; }
        else {
            dl = 2;
            if (!p1 && n1) {
                dl = corner_cut(S, 1, C, prv, cur, A2);
                if (dl != 2) { in1 = 1; if (dl == 3) { cap1 = 1; bl = 2; } else bl = 1; }
            }
            if (!p2 && n2 && dl == 2) {
                int r = corner_cut(S, 2, C, prv, cur, A2);
                if (r != 2) { in1 = 1; if (r == 3) { cap1 = 1; bl += 2; } else bl += 1; }
            }
        }
        if (n1 && n2) { both_out = 1; in2 = 1; bl += 1; }
        else {
            dl = 2;
            if (!n1 && p1) {
                dl = corner_cut(S, 1, C, prv, cur, B2);
                if (dl != 2) { in2 = 1; if (dl == 3) { cap2 = 1; bl += 2; } else bl += 1; }
            }
            if (!n2 && p2 && dl == 2) {
                int r = corner_cut(S, 2, C, prv, cur, B2);
                if (r != 2) { in2 = 1; if (r == 3) { cap2 = 1; bl += 2; } else bl += 1; }
            }
        }
        if ((unsigned char)bl > 1) {
            CsPiece p; memset(&p, 0, sizeof p); p.code = code;
            int k = 0;
            if (in1) {
                if (!both_in) { cs_cov2[18]++; prv[1] = ri * T[2 * i - 2] + C->P[1]; prv[2] = ri * T[2 * i - 1] + C->P[2]; }
                p.s[0] = prv[1]; p.z[0] = prv[2]; k = 1;
            }
            if (cap1) {
                p.s[k] = A2[0]; p.z[k] = A2[1]; k++;
                if (k == 2) { corner_emit(S, C, &p); emitted++; p.s[0] = A2[0]; p.z[0] = A2[1]; k = 1; }
            }
            if (cap2) {
                p.s[k] = B2[0]; p.z[k] = B2[1]; k++;
                if (k == 2) { corner_emit(S, C, &p); emitted++; k = 1; p.s[0] = B2[0]; p.z[0] = B2[1]; }
            }
            if (in2) {
                if (!both_out) { cs_cov2[19]++; cur[1] = ri * T[2 * i] + C->P[1]; cur[2] = ri * T[2 * i + 1] + C->P[2]; }
                p.s[k] = cur[1]; p.z[k] = cur[2]; k++;
                if (k == 2) { corner_emit(S, C, &p); emitted++; }
            }
        }
        prv[1] = cur[1]; prv[2] = cur[2];
        A2[0] = B2[0]; A2[1] = B2[1];
        p1 = n1; p2 = n2; e1 = f1; e2 = f2;
    }
    cs_cov2[13] += emitted;
    if ((short)emitted == 0) for (int k = 0; k < nnear; k++) { cs_cov2[14]++; corner_emit(S, C, &nearp[k]); }
}
/* FUN_1801db8c0's corner loop (asm 1801db995-1801dba55): a corner whose x + 590 + 588 is before the window leaves the list (FUN_1801d9fb0);
   one whose x - (590 + 588) is at or before hi is sectioned (FUN_1801ec6b0's result is not looked at) */
int cs_sect_corners(CsScan *S)
{
    CsCorner *prev = NULL, *C = S->cact;
    while (C) {
        CsCorner *nx = C->next;
        if (C->kind & 1) { S->off = S->off_chk; S->hoff = S->hoff_chk; } else { S->off = S->off_drive; S->hoff = S->hoff_drive; }
        double r = S->hoff + S->off;
        if (S->lo > C->P[0] + r) {
            if (prev) prev->next = nx; else S->cact = nx;
            C->next = S->cpool; S->cpool = C;
            C = nx; continue;
        }
        if (!(S->hi < C->P[0] - r)) corner_sect(S, C);
        prev = C; C = nx;
    }
    return 0;
}
