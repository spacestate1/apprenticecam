/* cam3d_cscallop_edge.c - Constant Scallop (op 43) stage B, the scanner FUN_1801ea7e0's edge and corner primitives (formats/surf_classic3.md
 * 12.13). The model's border edges (tmpedg0) become cylinders of radius DAT_1804ea588 around the edge (cut by the line's plane x = pos:
 * an ellipse arc) plus four "edge quad" triangles per side mode; its corners (tmpcrn0) become spheres. Sources: decomp/mcmsurf_all.c (l.N)
 * and objdump of mcmsurf.dll (asm addresses); mccore's proj_pt_ln (180002780), proj_pt_ln_3d (1804b42c0), lp_ints (1804b3ed0) and
 * UnitNormal (1804b7480) from decomp/mccore_filter.c, decomp/draft/core_calcdim.c, decomp/mccore_inters.c. Every floating-point
 * expression keeps the DLL's operand order and association (build with -ffp-contract=off). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

void msvc_qsort(void *base, unsigned long long n, unsigned long long w, int (*cmp)(const void *, const void *));   /* nest/nest_rect.c */
void cs_vtx_offset(const CsScan *S, double P[3], double n[3]);
int cs_piece_emit(CsScan *S, const CsPiece *p);
extern long cs_cov2[32];

static double rd(const unsigned char *b, int o) { double v; memcpy(&v, b + o, 8); return v; }

/* ------------------------------------------------------------------ FUN_1801e7e20 (l.324267): the edge / corner groups of one surface */
static int cmp_edg(const void *a, const void *b)          /* LAB_1801df300 (asm 1801df300): by min(P1.x, P2.x) */
{
    const unsigned char *p = a, *q = b;
    double x = rd(p, 0), x2 = rd(p, 0x20); if (!(x2 > x)) x = x2;
    double y = rd(q, 0), y2 = rd(q, 0x20); if (!(y2 > y)) y = y2;
    if (y > x) return -1;
    return x > y;
}
static int cmp_crn(const void *a, const void *b)          /* LAB_1801df2e0 (asm 1801df2e0): by P.x (+8) */
{
    double x = rd(a, 8), y = rd(b, 8);
    if (y > x) return -1;
    return x > y;
}
static void grp_push(CsScan *S, double xmin, double xmax, int off, int n, unsigned char kind)
{
    if (S->ng >= S->cg) { S->cg = S->cg * 2 + 64; S->g = realloc(S->g, (size_t)S->cg * sizeof(CsGroup)); }
    CsGroup *G = &S->g[S->ng++]; memset(G, 0, sizeof *G);
    G->xmin = xmin; G->xmax = xmax; G->off = off; G->n = n; G->kind = kind;
}
void cs_edge_groups(CsScan *S, int side, int ne, const unsigned char *edg, const unsigned char *crn)
{
    if (ne <= 0) return;                                  /* l.324319: nothing without edges */
    if (S->nedg + (size_t)ne * 0xb0 > S->cedg) { S->cedg = (S->nedg + (size_t)ne * 0xb0) * 2; S->edg = realloc(S->edg, S->cedg); }
    if (S->ncrn + (size_t)ne * 0x70 > S->ccrn) { S->ccrn = (S->ncrn + (size_t)ne * 0x70) * 2; S->crn = realloc(S->crn, S->ccrn); }
    int eo = (int)S->nedg, co = (int)S->ncrn;            /* mms +0x3c / +0x38: the surface's offsets in the files */
    unsigned char *e = S->edg + eo, *c = S->crn + co;
    memcpy(e, edg, (size_t)ne * 0xb0); memcpy(c, crn, (size_t)ne * 0x70);
    S->nedg += (size_t)ne * 0xb0; S->ncrn += (size_t)ne * 0x70;
    /* 588 / 590 here: the drive values (l.326322-326340 sets them before FUN_1801e7e20; check surfaces are refused upstream) */
    double r = S->off_drive, h = S->hoff_drive;
    msvc_qsort(e, (unsigned long long)ne, 0xb0, cmp_edg);   /* l.324362, rewritten in place (l.324365) */
    double x0 = rd(e, 0); if (rd(e, 0x20) <= rd(e, 0)) x0 = rd(e, 0x20);
    const unsigned char *L = e + (size_t)(ne - 1) * 0xb0;
    double x1 = rd(L, 0); if (rd(L, 0) <= rd(L, 0x20)) x1 = rd(L, 0x20);
    grp_push(S, x0 - (r + h), x1 + r + h, eo, ne, (unsigned char)(side ? 0 : 1));   /* l.324368-324383: kind = (param_3 == 0) */
    msvc_qsort(c, (unsigned long long)ne, 0x70, cmp_crn);   /* l.324395 */
    L = c + (size_t)(ne - 1) * 0x70;
    grp_push(S, rd(c, 8) - (r + h), r + h + rd(L, 8), co, ne, (unsigned char)((side ? 0 : 1) + 2));   /* l.324399-324404 */
}

/* ------------------------------------------------------------------ small vector helpers (the DLL's operand order) */
static void nrm3(double *v)                               /* FUN_18002dcd0 (l.31710): v / sqrt((x^2 + y^2) + z^2), 0 stays 0 */
{
    double x = v[0], y = v[1], z = v[2], l = sqrt(x * x + y * y + z * z);
    if (l == 0.0) { v[2] = 0.0; v[1] = 0.0; v[0] = 0.0; } else { v[0] = x / l; v[1] = y / l; v[2] = z / l; }
}
/* FUN_18024d220 (l.388290): v turned about the unit axis a by the angle (c, s) */
static void rot_axis(const double *v, const double *a, double c, double s, double *o)
{
    double ay = a[1], ax = a[0], az = a[2];
    double d = ax * v[0] + ay * v[1] + az * v[2];
    double py = v[1] - ay * d, pz = v[2] - az * d, px = v[0] - ax * d;
    double oy = (az * px - ax * pz) * s + py * c + ay * d;
    o[0] = (ay * pz - az * py) * s + px * c + ax * d;
    o[1] = oy;
    o[2] = (ax * py - ay * px) * s + pz * c + az * d;
}

/* ------------------------------------------------------------------ the edge's ellipse (FUN_1801df770 l.318861) */
/* r = 588, v = the edge vector, P1y / P1z = the record's P1, pts (y, z) pairs, n of them; fills the node's +0x68..+0xa0. 0 when n < 3 */
static int ell_arc(CsScan *S, double r, const double *v, double P1y, double P1z, int n, const double *pts, CsEdge *N)
{
    if (n < 3) return 0;
    unsigned short idx[3] = { 0, (unsigned short)(n >> 1), (unsigned short)(n - 1) };
    double rx = r / N->dir[0];                           /* dVar4 */
    double vy = v[1], vz = v[2], l = sqrt(vz * vz + vy * vy), uy, uz;
    if (l == 0.0) { uy = 0.0; uz = 0.0; } else { uy = vy / l; uz = vz / l; }
    double ang;
    if (1e-12 <= uz * uz + uy * uy) { ang = atan2(uz, uy); if (ang < 0.0) ang = ang + 6.283185307179586; }
    else ang = 0.0;
    double c = cos(ang); N->c78 = c;
    double s = sin(ang); N->s80 = s;
    double ms = -s;
    double q[3][2];                                      /* local_108[4..9]: the three points turned by -ang about (P1y, P1z) */
    for (int k = 0; k < 3; k++) {
        double dz = pts[2 * idx[k] + 1] - P1z, dy = pts[2 * idx[k]] - P1y;
        q[k][0] = c * dy - ms * dz;
        q[k][1] = dz * c + ms * dy;
    }
    double a[2];                                         /* the angles of the first and the last point on the ellipse */
    for (int k = 0; k < 2; k++) {
        double u = q[k ? 2 : 0][0], w = q[k ? 2 : 0][1];
        double t = u / fabs(rx);
        if (-1.0 <= t) { if (1.0 < t) t = 1.0; } else t = -1.0;
        double g = acos(t);
        if (w <= 0.0 && w != 0.0) {
            if (0.0 <= u) { if (0.0 < u) g = 6.283185307179586 - g; }
            else g = (3.141592653589793 - g) + 3.141592653589793;
        }
        a[k] = g;
    }
    double d1u = q[1][0] - q[0][0], d1w = q[1][1] - q[0][1], d2u = q[2][0] - q[0][0], d2w = q[2][1] - q[0][1];
    int b5 = d1u * d2w - d2u * d1w <= 0.0;
    double st = a[b5], en = a[!b5];
    if (en < st) { en = en + 6.283185307179586; }
    double h = acos((fabs(rx) - S->cdev) / fabs(rx));
    double m = fabs((en - st) / (h * 2.0));
    if (0.0 <= m) m = floor(m + 0.5); else m = ceil(m - 0.5);
    N->nst = (unsigned short)(int)m;
    if (N->nst < 2) N->nst = 2;
    double step = (en - st) / (double)N->nst;
    N->c90 = cos(step);
    double ss = sin(step);
    N->s98 = (fabs(rx) / r) * ss;
    N->sa0 = (r / fabs(rx)) * ss;
    N->a = cos(st) * fabs(rx);
    N->b = sin(st) * r;
    return 1;
}
/* FUN_1801e74e0 (l.323849): the edge node's x range from a circle of radius r around the edge (r a turned about the direction) */
static void edge_xrange(CsScan *S, double r, const double *a, CsEdge *N)
{
    double t90 = S->t90;
    double ang = sqrt((S->t90 * 8.0) / r);
    double q = 6.283185307179586 / ang;
    if (0.0 <= q) q = floor(q + 0.5); else q = ceil(q - 0.5);
    int n = (int)(short)(int)q + 1;
    double x1 = r * a[0] + N->P1[0], x2 = r * a[0] + N->P2[0];
    double m = x1 - t90, m2 = x2 - t90;
    if (m2 <= m) m = m2;
    N->xmin = m;
    m = S->t90 + x1;
    if (S->t90 + x1 <= x2 + S->t90) m = x2 + S->t90;
    N->xmax = m;
    double c = cos(ang), s = sin(ang), cur[3] = { a[0], a[1], a[2] }, o[3];
    for (int i = 0; i < n; i++) {
        rot_axis(cur, N->dir, c, s, o);
        double px = o[0] * r;
        x2 = px + N->P2[0];
        x1 = px + N->P1[0];
        double lo, hi;
        if (x2 <= x1) { lo = x2 - S->t90; if (lo <= N->xmin) N->xmin = lo; hi = S->t90 + x1; }
        else { lo = x1 - S->t90; if (lo <= N->xmin) N->xmin = lo; hi = S->t90 + x2; }
        if (N->xmax <= hi) N->xmax = hi;
        cur[0] = o[0]; cur[1] = o[1]; cur[2] = o[2];
    }
}
/* FUN_1801ea2e0 (l.325823): the ellipse points at the plane x (into +0x90) */
static int ell_pts(CsScan *S, double x, const CsEdge *N)
{
    if (S->c90 < N->nst + 10) { S->c90 = N->nst + 10; S->b90 = realloc(S->b90, sizeof(double) * 2 * (size_t)S->c90); }
    double *o = S->b90;
    double A = N->a, B = N->b;
    double t = (x - N->P1[0]) / N->dir[0];
    double X = N->c78 * A - N->s80 * B, Y = N->s80 * A + N->c78 * B;
    double cy = t * N->dir[1] + N->P1[1], cz = t * N->dir[2] + N->P1[2];
    o[0] = X + cy; o[1] = Y + cz;
    int i = 1;
    if (N->nst != 0) {
        for (;;) {
            double A2 = N->c90 * A - B * N->s98;
            double B2 = N->sa0 * A + B * N->c90;
            X = A2 * N->c78 - B2 * N->s80;
            Y = B2 * N->c78 + A2 * N->s80;
            o[2 * i] = cy + X; o[2 * i + 1] = Y + cz;
            A = A2; B = B2;
            unsigned short k = (unsigned short)i; i = (unsigned short)(k + 1);
            if (!(k < N->nst)) break;
        }
    }
    S->n90 = i;
    return i;
}
/* FUN_1801e77c0 (l.323968): the ellipse points on the outer side of the edge's plane (rec +0x40 = q; all of them when q = 0) -> +0xc0 */
static int ell_keep(CsScan *S, const unsigned char *rec, const CsEdge *N, double x)
{
    double qx = rd(rec, 0x40), qy = rd(rec, 0x48), qz = rd(rec, 0x50);
    int all = fabs(qx) < S->e && fabs(qy) < S->e && fabs(qz) < S->e;
    if (S->cc0 < S->n90 + 10) { S->cc0 = S->n90 + 10; S->bc0 = realloc(S->bc0, sizeof(double) * 2 * (size_t)S->cc0); }
    double *o = S->bc0, *p = S->b90;
    double tl = S->ctol;
    double t = (x - N->P1[0]) / N->dir[0];
    double cz = t * N->dir[2] + N->P1[2];
    double cy = t * N->dir[1] + N->P1[1];
    int prev = all ? 1 : ((p[0] - cy) * qy + (x - x) * qx + (p[1] - cz) * qz < tl);
    int cnt = 0;
    for (int i = 1; i < S->n90; i++) {
        int cur = all ? 1 : ((p[2 * i] - cy) * qy + (x - x) * qx + (p[2 * i + 1] - cz) * qz < tl);
        if (cnt == 0) {
            if (prev || cur) { o[0] = p[2 * i - 2]; o[1] = p[2 * i - 1]; o[2] = p[2 * i]; o[3] = p[2 * i + 1]; cnt = 2; }
        } else if (prev || cur) { o[2 * cnt] = p[2 * i]; o[2 * cnt + 1] = p[2 * i + 1]; cnt++; }
        prev = cur;
    }
    S->nc0 = cnt;
    return cnt;
}

static CsEdge *edge_node(CsScan *S)
{
    CsEdge *N = S->epool;
    if (N) S->epool = N->next; else N = malloc(sizeof *N);
    memset(N, 0, sizeof *N);
    return N;
}
/* FUN_1801e0d10 (l.319727): an edge whose x extent is within e - kind 8 / 9 */
static void edge_flat(CsScan *S, double r, const double *v, const double *a, unsigned char kind, const unsigned char *rec, CsEdge *N)
{
    for (int i = 0; i < 3; i++) { N->P1[i] = rd(rec, 8 * i); N->P2[i] = rd(rec, 0x20 + 8 * i); }
    double dx = N->P1[0] - N->P2[0], dz = N->P1[2] - N->P2[2], dy = N->P1[1] - N->P2[1];
    N->len = sqrt(dy * dy + dx * dx + dz * dz);
    double ax = a[0], ay = a[1], az = a[2];
    double f[2][3], g[2][3];                             /* P1 +- r a, P2 +- r a */
    g[0][0] = ax * r + N->P2[0]; f[0][0] = ax * r + N->P1[0];
    g[0][1] = ay * r + N->P2[1]; f[0][1] = ay * r + N->P1[1];
    g[0][2] = az * r + N->P2[2]; f[0][2] = az * r + N->P1[2];
    f[1][1] = ay * -r + N->P1[1]; g[1][1] = ay * -r + N->P2[1];
    g[1][0] = ax * -r + N->P2[0]; f[1][0] = ax * -r + N->P1[0];
    g[1][2] = az * -r + N->P2[2]; f[1][2] = az * -r + N->P1[2];
    N->xmin = 1e38; N->xmax = -1e38;
    for (int k = 0; k < 2; k++) {
        double p = f[k][0], q = g[k][0], m = N->xmin;
        if (q <= p) { if (q <= m) m = q; N->xmin = m; if (N->xmax <= p) N->xmax = p; }
        else { if (p <= m) m = p; N->xmin = m; if (N->xmax <= q) N->xmax = q; }
    }
    double x = v[0], y = v[1], z = v[2], l = sqrt(x * x + y * y + z * z);
    if (l == 0.0) { N->dir[0] = N->dir[1] = N->dir[2] = 0.0; } else { N->dir[0] = x / l; N->dir[1] = y / l; N->dir[2] = z / l; }
    N->a = 0; N->b = 0; N->c78 = 1.0; N->s80 = 1.0; N->nst = 0; N->c90 = 0; N->s98 = 0; N->sa0 = 0;
    N->kind = (unsigned char)((kind != 0) + 8);
    N->next = S->eact; S->eact = N;                      /* FUN_1801da8c0: push front of +0x80 */
}
/* FUN_1801e1050 (l.319854, asm 1801e1050-1801e14c0): any other edge - kind 0 / 1 */
static int edge_gen(CsScan *S, double r, const double *v, const double *a, const double *b, unsigned char kind, const unsigned char *rec, CsEdge *N)
{
    for (int i = 0; i < 3; i++) { N->P1[i] = rd(rec, 8 * i); N->P2[i] = rd(rec, 0x20 + 8 * i); }
    double dx = N->P1[0] - N->P2[0], dy = N->P1[1] - N->P2[1], dz = N->P1[2] - N->P2[2];
    N->len = sqrt(dx * dx + dy * dy + dz * dz);
    double P1[3] = { N->P1[0], N->P1[1], N->P1[2] }, P2[3] = { N->P2[0], N->P2[1], N->P2[2] };
    double F[3][3], G[3][3];                             /* P1 + r a, P1 + r b, P1 - r a; the same at P2 */
    for (int i = 0; i < 3; i++) {
        double ra = r * a[i], rb = r * b[i], na = -r * a[i];
        F[0][i] = P1[i] + ra; G[0][i] = ra + P2[i];
        F[1][i] = P1[i] + rb; G[1][i] = rb + P2[i];
        F[2][i] = na + P1[i]; G[2][i] = na + P2[i];
    }
    double pts[6];                                       /* the three lines met with the plane x = P1.x (asm 1801e12a4..1801e1329) */
    for (int k = 0; k < 3; k++) {
        double Dx = G[k][0] - F[k][0], Dy = G[k][1] - F[k][1], Dz = G[k][2] - F[k][2];
        double t = (P1[0] - F[k][0]) / Dx;
        pts[2 * k] = t * Dy + F[k][1];
        pts[2 * k + 1] = t * Dz + F[k][2];
    }
    double x = v[0], y = v[1], z = v[2], l = sqrt(x * x + y * y + z * z);
    if (l == 0.0) { N->dir[0] = N->dir[1] = N->dir[2] = 0.0; } else { N->dir[0] = x / l; N->dir[1] = y / l; N->dir[2] = z / l; }
    edge_xrange(S, r, a, N);
    if (!ell_arc(S, r, v, P1[1], P1[2], 3, pts, N)) return 0;
    ell_pts(S, P1[0], N);
    int k = ell_keep(S, rec, N, P1[0]);
    if ((unsigned short)k > 2) cs_cov2[1]++;
    if ((unsigned short)k > 2 && !ell_arc(S, r, v, P1[1], P1[2], k, S->bc0, N)) return 0;
    N->kind = kind;
    N->next = S->eact; S->eact = N;
    return 1;
}

/* FUN_1801df420 (l.318704): the cylinder node of one edge record */
static int edge_add(CsScan *S, unsigned char kind, const unsigned char *rec)
{
    CsEdge *N = edge_node(S);                             /* FUN_1801da400: taken before the test below (and not given back) */
    double v[3] = { rd(rec, 0x20) - rd(rec, 0), rd(rec, 0x28) - rd(rec, 8), rd(rec, 0x30) - rd(rec, 0x10) };
    double dx = v[0], dy = v[1], dz = v[2];
    if (fabs(dx) <= S->e && fabs(dy) <= S->e) { cs_cov2[21]++; free(N); return 1; }   /* an edge along z: nothing */
    double a[3] = { dy - dz * 0.0, dz * 0.0 - dx, dx * 0.0 - dy * 0.0 };   /* v x (0, 0, 1) */
    double b[3] = { a[1] * dz - a[2] * dy, a[2] * dx - a[0] * dz, a[0] * dy - a[1] * dx };   /* a x v */
    nrm3(a); nrm3(b);
    if (fabs(dx) <= S->e) { cs_cov2[0]++; edge_flat(S, S->off, v, a, kind, rec, N); return 1; }
    if (edge_gen(S, S->off, v, a, b, kind, rec, N)) return 1;
    free(N);
    return 0;                                            /* error 0x196e */
}

/* FUN_1801da920 (l.315930) with param_1 = 0: the three vertices by x (insertion, carrying the normals) */
static void tri_sort_x(CsTri *T)
{
    for (int i = 1; i < 3; i++) {
        double kv[3] = { T->v[i][0], T->v[i][1], T->v[i][2] }, kn[3] = { T->n[i][0], T->n[i][1], T->n[i][2] };
        int j = i - 1;
        for (; j >= 0; j--) {
            if (T->v[j][0] <= kv[0]) break;
            memcpy(T->v[j + 1], T->v[j], 24); memcpy(T->n[j + 1], T->n[j], 24);
        }
        memcpy(T->v[j + 1], kv, 24); memcpy(T->n[j + 1], kn, 24);
    }
}
/* FUN_1801d9520 (l.314645): the four triangles between the offset edge and its two turned copies (the side mode in S->mode) */
static void edge_quads(CsScan *S, unsigned char kind, const unsigned char *rec, const double *u, double c, double s)
{
    double P1[3] = { rd(rec, 0), rd(rec, 8), rd(rec, 0x10) }, P2[3] = { rd(rec, 0x20), rd(rec, 0x28), rd(rec, 0x30) };
    double N1[3] = { rd(rec, 0x78), rd(rec, 0x80), rd(rec, 0x88) }, N2[3] = { rd(rec, 0x90), rd(rec, 0x98), rd(rec, 0xa0) };
    double O1[3], O2[3], R1[3], R2[3], Q[3], Q2[3], d = 0;
    for (int k = 0; k < 4; k++) {
        CsTri *T = S->pool;
        if (T) S->pool = T->next; else T = malloc(sizeof *T);
        memset(T, 0, sizeof *T);
        T->kind = (unsigned char)((kind != 0) + 12);
        if (k == 0 || k == 2) {
            if (k == 0) {
                memcpy(O1, P1, 24); memcpy(O2, P2, 24);
                cs_vtx_offset(S, O1, N1); cs_vtx_offset(S, O2, N2);   /* FUN_1801e99f0 (the normals flipped in place as the DLL's copies) */
                rot_axis(N1, u, c, s, R1);
                d = S->off - S->t90;
            } else rot_axis(N1, u, c, -s, R1);
            Q[2] = R1[2] * d + P1[2]; Q[0] = R1[0] * d + P1[0]; Q[1] = R1[1] * d + P1[1];
            memcpy(T->v[0], O1, 24); memcpy(T->v[1], O2, 24); memcpy(T->v[2], Q, 24);
            memcpy(T->n[0], N1, 24); memcpy(T->n[1], N2, 24); memcpy(T->n[2], R1, 24);
        } else {
            rot_axis(N2, u, c, k == 1 ? s : -s, R2);
            Q2[2] = R2[2] * d + P2[2]; Q2[0] = R2[0] * d + P2[0]; Q2[1] = R2[1] * d + P2[1];
            memcpy(T->v[0], O2, 24); memcpy(T->v[1], Q2, 24); memcpy(T->v[2], Q, 24);
            memcpy(T->n[0], N2, 24); memcpy(T->n[1], R2, 24); memcpy(T->n[2], R1, 24);
        }
        tri_sort_x(T);
        T->next = S->act; S->act = T;                     /* FUN_1801da8e0: push front of +0x60 */
    }
}
/* FUN_1801e0ae0 (l.319645): the edge quads for the side modes 0 or 1, and with the flag 2 and 4 */
static int edge_quad_modes(CsScan *S, unsigned char kind, const unsigned char *rec)
{
    int saved = S->mode;
    double dy = rd(rec, 0x28) - rd(rec, 8), dz = rd(rec, 0x30) - rd(rec, 0x10), dx = rd(rec, 0x20) - rd(rec, 0);
    double u[3] = { dx, dy, dz }, l = sqrt(dy * dy + dx * dx + dz * dz);
    if (l == 0.0) { u[0] = u[1] = u[2] = 0.0; } else { u[0] = dx / l; u[1] = dy / l; u[2] = dz / l; }
    if (S->e <= fabs(u[0]) || S->e <= fabs(u[1])) {
        double g = sqrt((S->t90 * 8.0) / S->off), c = cos(g), s = sin(g);
        unsigned char flag = rec[0xa8];
        double n1z = rd(rec, 0x88);
        S->mode = (flag != 0 || -1e-05 <= n1z) ? 0 : 1;
        edge_quads(S, kind, rec, u, c, s);
        if (flag) {
            cs_cov2[20]++;
            S->mode = 2; edge_quads(S, kind, rec, u, c, s);
            S->mode = 4; edge_quads(S, kind, rec, u, c, s);
        }
    }
    S->mode = saved;
    return 0;
}

/* FUN_1801df380 (l.318666): a corner node */
static void corner_add(CsScan *S, unsigned char kind, const unsigned char *rec)
{
    CsCorner *C = S->cpool;
    if (C) S->cpool = C->next; else C = malloc(sizeof *C);
    memset(C, 0, sizeof *C);
    for (int i = 0; i < 3; i++) { C->P[i] = rd(rec, 8 + 8 * i); C->d1[i] = rd(rec, 0x20 + 8 * i); C->d2[i] = rd(rec, 0x38 + 8 * i); }
    C->kind = kind;
    C->next = S->cact; S->cact = C;                       /* FUN_1801da8a0: push front of +0x100 */
}

/* FUN_1801daa60's edge and corner arms (asm 1801dacda-1801daf4c): the group's records read from its offset; a record wholly before the
   window (lo > max x + 588 + 590) is skipped, the first one starting beyond it (min x - (590 + 588) > hi) stops the group (its xmin moved
   there); the others become nodes (edges also their quads). Returns 0, or -1 (an error the DLL reports) */
int cs_act_edges(CsScan *S, CsGroup *G)
{
    while (G->n > 0) {
        const unsigned char *r = S->edg + G->off;
        double x1 = rd(r, 0), x2 = rd(r, 0x20);
        double mx = x1 > x2 ? x1 : x2, rr = S->hoff + S->off;
        if (S->lo > mx + rr) { G->n--; G->off += 0xb0; continue; }
        double mn = x2 > x1 ? x1 : x2;
        if (mn - rr > S->hi) { mn = x2 > x1 ? x1 : x2; G->xmin = mn - (S->hoff + S->off); break; }
        if (!edge_add(S, G->kind, r)) return -1;
        if (edge_quad_modes(S, G->kind, r)) return -1;
        G->n--; G->off += 0xb0;
    }
    return 0;
}
int cs_act_corners(CsScan *S, CsGroup *G)
{
    while (G->n > 0) {
        const unsigned char *r = S->crn + G->off;
        double x = rd(r, 8), rr = S->hoff + S->off;
        if (S->lo > x + rr) { G->n--; G->off += 0x70; continue; }
        if (x - rr > S->hi) { G->xmin = x - (S->hoff + S->off); break; }
        corner_add(S, G->kind, r);
        G->n--; G->off += 0x70;
    }
    return 0;
}

void cs_edge_bytes(const CsEdge *e, unsigned char out[0xb0])
{
    memset(out, 0, 0xb0);
    memcpy(out + 8, e->P1, 24); memcpy(out + 0x20, e->P2, 24); memcpy(out + 0x38, e->dir, 24);
    memcpy(out + 0x50, &e->len, 8); memcpy(out + 0x58, &e->xmin, 8); memcpy(out + 0x60, &e->xmax, 8);
    memcpy(out + 0x68, &e->a, 8); memcpy(out + 0x70, &e->b, 8); memcpy(out + 0x78, &e->c78, 8); memcpy(out + 0x80, &e->s80, 8);
    memcpy(out + 0x88, &e->nst, 2); memcpy(out + 0x90, &e->c90, 8); memcpy(out + 0x98, &e->s98, 8); memcpy(out + 0xa0, &e->sa0, 8);
    out[0xa8] = e->kind;
}
void cs_corner_bytes(const CsCorner *c, unsigned char out[0x58])
{
    memset(out, 0, 0x58);
    memcpy(out + 8, c->P, 24); memcpy(out + 0x20, c->d1, 24); memcpy(out + 0x38, c->d2, 24); out[0x50] = c->kind;
}
