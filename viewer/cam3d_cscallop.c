/* cam3d_cscallop.c - Constant Scallop (op 43) stage B, the scanner mcmsurf FUN_1801ea7e0 (formats/surf_classic3.md 12.3, 12.8, 12.12).
 * Sources: decomp/mcmsurf_all.c (l.N below) and objdump of mcmsurf.dll (asm addresses below). What is ported is listed in
 * cam3d_cscallop.h. Every floating-point expression keeps the DLL's operand order (build with
 * -ffp-contract=off as the rest of the engine ports). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

long cs_cov[8];   /* branch counts for the unit oracle's coverage (cs_port prints them): 0 span, 1 two crossings, 2 one-vertex /
                     outside returns, 3 two vertices on the line, 4 extended by the crossing, 5 vertex i2 below A */
void msvc_qsort(void *base, unsigned long long n, unsigned long long w, int (*cmp)(const void *, const void *));   /* nest/nest_rect.c */

/* ------------------------------------------------------------------ setup */
int cs_setup(CsScan *S, double tol, double p3, double p4, double p5, double corner_dev, double systol)
{
    memset(S, 0, sizeof *S);
    S->tol = tol;
    S->t90 = tol * 0.9;                                   /* l.326183 DAT_1804ea560 */
    S->half = (tol - S->t90) * 0.5;                       /* DAT_1804ea568 */
    S->e = S->t90 * 0.01;                                 /* DAT_1804ea570 */
    S->ctol = systol;                                     /* *tol_exref (mccore ?tol@@3NA) */
    S->systol = systol;                                   /* DAT_1804ea578 = *tol_exref, min with 0.0001 x 560 (l.326186) */
    if (S->t90 * 0.0001 < S->systol) S->systol = S->t90 * 0.0001;
    S->cdev = corner_dev <= tol ? corner_dev * 0.9 : corner_dev - S->half * 2.0;   /* l.326190-326195 DAT_1804ea580 */
    /* l.326224-326258: s = p3, or -p4 when p4 < -p3 */
    double s = p3;
    if (p4 < -p3) s = -p4;
    S->s = s;
    S->off = p4 + s;                                      /* DAT_1804ea588 */
    int le = S->off <= S->e;
    if (le) S->off = 0.0;
    S->off_on = !le;                                      /* DAT_1804ea4f3 */
    double d22 = (s * 2.0 + p5) * 0.5;
    S->hoff = d22 - S->off;                               /* DAT_1804ea590 */
    if (S->e < S->hoff) { S->hoff_on = 1; return -16; }   /* FUN_1801da070(0x27d8, 0xfffffff0): not a ball tool */
    S->hoff_on = 0; S->hoff = 0.0;
    S->hoff_drive = 0.0;                                  /* DAT_1804ea5a8 */
    S->off = d22; S->off_drive = d22;                     /* DAT_1804ea588 = DAT_1804ea5a0 = dVar22; DAT_1804ea4f7 = DAT_1804ea4f3 */
    /* DAT_1805034a1 (check surfaces) would recompute 588 / 590 from msurf_chk_offset (l.326268-326284): bnci_surface refuses check
       surfaces on this path (12.7), so 5b0 / 5b8 = the drive values (l.326286-326289) */
    S->off_chk = S->off; S->hoff_chk = S->hoff;
    S->mode = 0;                                          /* DAT_180501d4c's static value (.data 0x180501d4c = 0) */
    return 0;
}

void cs_free(CsScan *S)
{
    free(S->tf); free(S->g); free(S->pc); free(S->edg); free(S->crn); free(S->b90); free(S->bc0); free(S->arc.pts);
    free(S->est); free(S->batch); free(S->bptr); free(S->wall);
    for (CsTri *t = S->act, *n; t; t = n) { n = t->next; free(t); }
    for (CsTri *t = S->pool, *n; t; t = n) { n = t->next; free(t); }
    for (CsEdge *t = S->eact, *n; t; t = n) { n = t->next; free(t); }
    for (CsEdge *t = S->epool, *n; t; t = n) { n = t->next; free(t); }
    for (CsCorner *t = S->cact, *n; t; t = n) { n = t->next; free(t); }
    for (CsCorner *t = S->cpool, *n; t; t = n) { n = t->next; free(t); }
    memset(S, 0, sizeof *S);
}

/* ------------------------------------------------------------------ vertex offset (FUN_1801e99f0 l.325363) */
static void flips(int mode, double n[3])
{
    if (0 < mode) {
        if (mode == 2 || (n[2] <= 0.0 && n[2] != 0.0)) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        if (mode == 4) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
    }
}
void cs_vtx_offset(const CsScan *S, double P[3], double n[3])
{
    flips(S->mode, n);                                    /* the sign bits of x / y and z = -z (l.325375-325387) */
    double d = S->off;
    if (S->off_on) { P[0] = d * n[0] + P[0]; P[1] = d * n[1] + P[1]; P[2] = d * n[2] + P[2]; }
    if (S->hoff_on) {                                     /* l.325395-325420: the horizontal offset along n_xy / |n_xy| */
        double nx = n[0], a = 0.0, b = 0.0;
        if (S->e <= fabs(nx) && fabs(nx) != S->e) a = nx;
        double ny = n[1];
        int ok = (S->e <= fabs(ny) && fabs(ny) != S->e);
        b = ok ? ny : 0.0;
        if (ok || (S->e <= fabs(nx) && fabs(nx) != S->e)) {
            double r = sqrt(a * a + b * b), ux, uy;
            if (r == 0.0) { r = 0.0; ux = 0.0; } else { ux = a / r; r = b / r; }
            uy = r * S->hoff;
            double z0 = S->hoff * 0.0;
            P[0] = ux * S->hoff + P[0]; P[1] = uy + P[1]; P[2] = z0 + P[2];
        }
    }
}
/* FUN_1801ea5f0 (l.325900): the offset taken back (the same flips, then - 590 n_xy / |n_xy| and - 588 n) */
void cs_vtx_unoffset(const CsScan *S, double P[3], const double n0[3])
{
    double n[3] = { n0[0], n0[1], n0[2] };
    if (0 < S->mode) {
        if (S->mode == 2 || n0[2] < 0.0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        if (S->mode == 4) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
    }
    if (S->hoff_on) {
        int bx = S->e <= fabs(n[0]) && fabs(n[0]) != S->e;
        double a = bx ? n[0] : 0.0, b = n[1];
        int by = S->e <= fabs(b) && fabs(b) != S->e;
        if (!by) b = 0.0;
        if (by || bx) {
            double r = sqrt(a * a + b * b), ux, uy;
            if (r == 0.0) { uy = 0.0; ux = 0.0; } else { ux = a / r; uy = b / r; }
            double m = -S->hoff;
            P[0] = -S->hoff * ux + P[0]; P[1] = uy * m + P[1]; P[2] = m * 0.0 + P[2];
        }
    }
    if (S->off_on) {
        double m = -S->off;
        P[0] = n[0] * -S->off + P[0]; P[1] = n[1] * m + P[1]; P[2] = n[2] * m + P[2];
    }
}
/* FUN_1801e7db0 (l.324227): the normal as written to ID2371.tmp */
void cs_nrm_out(const CsScan *S, const double n[3], double out[3])
{
    out[0] = n[0]; out[1] = n[1]; out[2] = n[2];
    if (0 < S->mode) {
        if (S->mode == 2 || n[2] < 0.0) { out[0] = -out[0]; out[1] = -out[1]; out[2] = -out[2]; }
        if (S->mode == 4) { out[0] = -out[0]; out[1] = -out[1]; out[2] = -out[2]; }
    }
}

/* ------------------------------------------------------------------ phase A (FUN_1801e8360 l.324442) */
static const double *g_sortx;   /* DAT_1804ea4e8: the points the comparators index */
static void tri_order(int nt, int *t, const double *V)   /* FUN_1801db5c0 (l.316323): the three indices by x */
{
    for (int k = 0; k < nt; k++, t += 3) {
        int i8 = t[0], i6 = t[1], i7 = i8;
        if (V[3 * i6] < V[3 * i8]) { t[0] = i6; t[1] = i8; i7 = i6; i6 = i8; }
        i8 = t[2];
        if (V[3 * i8] < V[3 * i7]) { t[0] = i8; t[2] = i7; i8 = i7; }
        if (V[3 * i8] < V[3 * i6]) { t[1] = i8; t[2] = i6; }
    }
}
static int cmp_tri(const void *a, const void *b)         /* LAB_1801df340 (asm 1801df340): by the first vertex's x */
{
    double x = g_sortx[3 * *(const int *)a], y = g_sortx[3 * *(const int *)b];
    if (x < y) return -1;
    return y < x;
}
static int cmp_grp(const void *a, const void *b)         /* LAB_1801df2c0 (asm): by xmin */
{
    double x = ((const CsGroup *)a)->xmin, y = ((const CsGroup *)b)->xmin;
    if (x < y) return -1;
    return y < x;
}
static void tf_push(CsScan *S, const double *v, size_t n)
{
    if (S->ntf + n > S->ctf) { S->ctf = (S->ntf + n) * 2 + 1024; S->tf = realloc(S->tf, S->ctf * sizeof(double)); }
    memcpy(S->tf + S->ntf, v, n * sizeof(double)); S->ntf += n;
}
/* the write loop (l.324700-324740, repeated per pass): triangles in sorted order, 3 points then (has_n) the 3 normals through
   FUN_1801e7db0; the group record when nt > 0 */
static void write_group(CsScan *S, int has_n, unsigned char kind, int nt, const int *t, const double *V, const double *N)
{
    if (nt <= 0) return;
    CsGroup G; memset(&G, 0, sizeof G);
    G.kind = kind; G.off = S->toff; G.xmin = V[3 * t[0]]; G.xmax = V[3 * t[2]];
    for (int k = 0; k < nt; k++) {
        const int *q = t + 3 * k;
        for (int j = 0; j < 3; j++) tf_push(S, V + 3 * q[j], 3);
        if (has_n) for (int j = 0; j < 3; j++) { double o[3]; cs_nrm_out(S, N + 3 * q[j], o); tf_push(S, o, 3); }
        if (G.xmax < V[3 * q[2]]) G.xmax = V[3 * q[2]];
    }
    G.n = nt;
    if (S->ng >= S->cg) { S->cg = S->cg * 2 + 64; S->g = realloc(S->g, (size_t)S->cg * sizeof(CsGroup)); }
    S->g[S->ng++] = G;
    S->toff += (has_n ? 6 : 3) * nt * 0x18;               /* (short)local_3d8 x count x 0x18 */
}
static void sort_tris(int nt, int *t, const double *V)
{
    g_sortx = V;
    tri_order(nt, t, V);
    msvc_qsort(t, (unsigned long long)nt, 12, cmp_tri);
}
int cs_phase_a(CsScan *S, int has_n, int side, int mode, int np, const double *pts, const double *nrm, int nt, const int *tri0)
{
    unsigned char kind = side ? (has_n ? 10 : 4) : (has_n ? 0xb : 5);   /* uVar18 (l.324610-324622) = local_3dc */
    S->mode = mode;                                       /* setup_mach_model_buffer's param_21 = &DAT_180501d4c (mms +0x64) */
    double *V = malloc(sizeof(double) * 3 * (size_t)(np > 0 ? np : 1));
    int *t = malloc(sizeof(int) * 3 * (size_t)(nt > 0 ? nt : 1));
    memcpy(V, pts, sizeof(double) * 3 * (size_t)np); memcpy(t, tri0, sizeof(int) * 3 * (size_t)nt);
    const double *N = nrm;
    /* one block: inquire_mach_model_buffer(first, done) -> local_3b4 = 0, local_400 = the block's point count */
    for (int i = 0; i < np; i++) { double n[3] = { N[3 * i], N[3 * i + 1], N[3 * i + 2] }; cs_vtx_offset(S, V + 3 * i, n); }
    sort_tris(nt, t, V);
    write_group(S, has_n, kind, nt, t, V, N);
    if (S->mode == 1) {                                   /* l.324767-325329: both sides */
        for (int i = 0; i < np; i++) cs_vtx_unoffset(S, V + 3 * i, N + 3 * i);
        int w = 0;                                        /* keep the triangles whose normals are not all z >= 1e-5 / all <= -1e-5 */
        for (int k = 0; k < nt; k++) {
            int pz = 0, nz = 0;
            for (int j = 0; j < 3; j++) { double z = N[3 * t[3 * k + j] + 2]; if (z < 1e-05) { if (z <= -1e-05) nz++; } else pz++; }
            if (pz != 3 && nz != 3) { if (w != k) memcpy(t + 3 * w, t + 3 * k, 12); w++; }
        }
        nt = w;
        if (nt > 0) {
            static const int pm[3] = { 0, 2, 4 };          /* DAT_180501d4c = 0, 2, 4: one side, the other, the lower side */
            for (int p = 0; p < 3; p++) {
                S->mode = pm[p];
                for (int i = 0; i < np; i++) { double n[3] = { N[3 * i], N[3 * i + 1], N[3 * i + 2] }; cs_vtx_offset(S, V + 3 * i, n); }
                sort_tris(nt, t, V);
                write_group(S, has_n, kind, nt, t, V, N);
                for (int i = 0; i < np; i++) cs_vtx_unoffset(S, V + 3 * i, N + 3 * i);
            }
            S->mode = 1;
            /* l.325240-325290 (asm 1801e9209-1801e9358): the vertex signs; the two counters are NOT reset per triangle (bx / r11w are
               zeroed once before the loop), so only the triangles at which a running count is exactly 2 are kept; their two
               same-sign vertices first - but only when the write position differs from the read position */
            int pz = 0, nz = 0; w = 0;
            for (int k = 0; k < nt; k++) {
                int *q = t + 3 * k, sg[3];
                for (int j = 0; j < 3; j++) {
                    double z = N[3 * q[j] + 2];
                    if (z < 1e-05) { if (-1e-05 < z) sg[j] = 0; else { nz++; sg[j] = -1; } } else { pz++; sg[j] = 1; }
                }
                int want;
                if (pz == 2) want = 1; else if (nz == 2) want = -1; else continue;
                int *o = t + 3 * w;
                if (o != q) {
                    int a = q[0], b = q[1], c = q[2];
                    if (sg[0] == want && sg[1] == want) { o[0] = a; o[1] = b; o[2] = c; }
                    else if (sg[0] == want && sg[2] == want) { o[0] = a; o[1] = c; o[2] = b; }
                    else { o[0] = b; o[1] = c; o[2] = a; }
                }
                w++;
            }
            nt = w;
            if (nt > 0) {                                 /* l.325296-325330: per kept triangle the quad of its first edge offset to both sides */
                double *P2 = malloc(sizeof(double) * 12 * (size_t)nt), *N2 = malloc(sizeof(double) * 12 * (size_t)nt);
                int *t2 = malloc(sizeof(int) * 6 * (size_t)nt);
                for (int k = 0; k < nt; k++) {
                    int a = t[3 * k], b = t[3 * k + 1];
                    double *p = P2 + 12 * k, *n = N2 + 12 * k;
                    memcpy(p, V + 3 * a, 24); memcpy(p + 3, V + 3 * b, 24); memcpy(p + 6, V + 3 * a, 24); memcpy(p + 9, V + 3 * b, 24);
                    memcpy(n, N + 3 * a, 24); memcpy(n + 3, N + 3 * b, 24); memcpy(n + 6, N + 3 * a, 24); memcpy(n + 9, N + 3 * b, 24);
                    if (N[3 * a + 2] < 1e-05) { S->mode = 2; cs_vtx_offset(S, p, n); cs_vtx_offset(S, p + 3, n + 3); S->mode = 0; }
                    else { S->mode = 0; cs_vtx_offset(S, p, n); cs_vtx_offset(S, p + 3, n + 3); S->mode = 2; }
                    cs_vtx_offset(S, p + 6, n + 6); cs_vtx_offset(S, p + 9, n + 9);
                    int *q = t2 + 6 * k, i = 4 * k;
                    q[0] = i; q[1] = i + 1; q[2] = i + 2; q[3] = i + 1; q[4] = i + 2; q[5] = i + 3;
                }
                sort_tris(2 * nt, t2, P2);
                /* the normals go out through FUN_1801e7db0 with DAT_180501d4c as the last triangle left it (0 or 2) */
                write_group(S, has_n, kind, 2 * nt, t2, P2, N2);
                free(P2); free(N2); free(t2);
                S->mode = 1;
            }
        }
    }
    S->mode = 1;                                          /* l.325354: DAT_180501d4c = 1 after the block loop */
    free(V); free(t);
    return 0;
}
void cs_sort_groups(CsScan *S) { msvc_qsort(S->g, (unsigned long long)S->ng, sizeof(CsGroup), cmp_grp); }

/* ------------------------------------------------------------------ phase B: the triangle pieces of one line */
static void piece_out(CsScan *S, const CsPiece *p)       /* DAT_180501d90 = FUN_1801e40c0 (l.321785): the batch, flushed at 999 */
{
    if (S->npc >= S->cpc) { S->cpc = S->cpc * 2 + 256; S->pc = realloc(S->pc, (size_t)S->cpc * sizeof(CsPiece)); }
    S->pc[S->npc++] = *p;
    if (S->npc % 999 == 0) S->nflush++;                   /* (int)DAT_1804ea614 == DAT_1804d5fb8 - 1 -> FUN_1801e7350 */
}
int cs_piece_emit(CsScan *S, const CsPiece *p) { piece_out(S, p); return 1; }
static void nrm_interp(double t, const double *a, const double *b, double *o)   /* FUN_1802535d0 (l.?, mcmsurf 1802535d0) */
{
    double x = (b[0] - a[0]) * t + a[0], y = (b[1] - a[1]) * t + a[1], z = (b[2] - a[2]) * t + a[2];
    double r = sqrt(y * y + x * x + z * z);
    if (r == 0.0) { o[0] = 0.0; o[1] = 0.0; o[2] = 1.0; }  /* v_z_3d */
    else { o[0] = x / r; o[1] = y / r; o[2] = z / r; }
}
void cs_nrm_interp(double t, const double *a, const double *b, double *o) { nrm_interp(t, a, b, o); }
static short kind_code(unsigned char k, int *ok)
{
    *ok = 1;
    switch (k) { case 10: return 0; case 11: return 0x12; case 12: return 5; case 13: return 0x17; }
    *ok = 0; return 0;
}
/* FUN_1801dc360 (l.317063): a triangle strictly across the line window - the section from the edge v0-v2 and the edge v1-v2 or v0-v1 */
int cs_tri_span(CsScan *S, const CsTri *T)
{
    int ok; CsPiece p; memset(&p, 0, sizeof p);
    p.code = kind_code(T->kind, &ok);
    if (!ok) return 0;
    const double *v0 = T->v[0], *v1 = T->v[1], *v2 = T->v[2];
    double t = (S->pos - v0[0]) / (v2[0] - v0[0]);
    p.s[0] = (v2[1] - v0[1]) * t + v0[1];
    p.z[0] = (v2[2] - v0[2]) * t + v0[2];
    nrm_interp(t, T->n[0], T->n[2], p.n[0]);
    const double *na, *nb;
    if (v1[0] < S->pos) {
        na = T->n[1]; nb = T->n[2];
        t = (S->pos - v1[0]) / (v2[0] - v1[0]);
        p.s[1] = (v2[1] - v1[1]) * t + v1[1];
        p.z[1] = (v2[2] - v1[2]) * t + v1[2];
    } else {
        nb = T->n[1]; na = T->n[0];
        t = (S->pos - v0[0]) / (v1[0] - v0[0]);
        p.z[1] = (v1[2] - v0[2]) * t + v0[2];
        p.s[1] = (v1[1] - v0[1]) * t + v0[1];
    }
    nrm_interp(t, na, nb, p.n[1]);
    cs_cov[0]++;
    piece_out(S, &p);
    return 1;
}
/* FUN_1801dbc50 (l.316600): the general section. Edge crossings (half-open: xa <= p < xb, or p <= xa and xb < p, the edge longer than e
   in x) give up to two points; with fewer, the vertices within e of the line (at most two used) and the one crossing extend the piece */
int cs_tri_section(CsScan *S, unsigned char kind, const double V[3][3], const double (*N)[3], int i0, int i1, int i2)
{
    int ok; CsPiece p; memset(&p, 0, sizeof p);
    p.code = kind_code(kind, &ok);
    if (!ok) return 0;
    int hasn = N != NULL;
    double e = S->e, pos = S->pos;
    double ys[2] = { 0, 0 }, zs[2] = { 0, 0 }, ns[2][3] = { { 0, 0, 1 }, { 0, 0, 1 } };   /* local_e8 / local_d0 / local_138 (defaults: v_z) */
    int k = 0;
    double xmin = V[i0][0], xmax = V[i0][0];
    static const int ea[3] = { 0, 1, 2 }, eb[3] = { 1, 2, 0 };
    int id[3] = { i0, i1, i2 };
    for (int it = 0; it < 3; it++) {
        int a = id[ea[it]], b = id[eb[it]];
        if (it < 2) { double x = V[b][0]; if (x < xmin) xmin = x; if (xmax < x) xmax = x; }
        double xa = V[a][0], xb = V[b][0];
        if (e <= fabs(xa - xb)) {
            if (xa <= pos && pos < xb) {
                double t = (pos - xa) / (xb - xa);
                ys[k] = (V[b][1] - V[a][1]) * t + V[a][1];
                zs[k] = (V[b][2] - V[a][2]) * t + V[a][2];
                if (hasn) nrm_interp(t, N[a], N[b], ns[k]);
                k++;
            }
            xa = V[a][0];
            if (pos <= xa && (xb = V[b][0], xb < pos)) {
                double t = (pos - xb) / (xa - xb);
                ys[k] = (V[a][1] - V[b][1]) * t + V[b][1];
                zs[k] = (V[a][2] - V[b][2]) * t + V[b][2];
                if (hasn) nrm_interp(t, N[b], N[a], ns[k]);
                k++;
            }
        }
    }
    if (k == 2) {
        cs_cov[1]++;
        p.s[0] = ys[0]; p.s[1] = ys[1]; p.z[0] = zs[0]; p.z[1] = zs[1];
        memcpy(p.n[0], ns[0], 24); memcpy(p.n[1], ns[1], 24);
        piece_out(S, &p);
        return 1;
    }
    if (pos < xmin - e) return 1;
    if (e + xmax < pos) return 1;
    int cross = (k == 1);
    double cy = ys[0], cz = zs[0], cn[3] = { 0, 0, 0 };
    if (cross && hasn) memcpy(cn, ns[0], 24);
    /* A = (local_190, uVar31, A.n = uVar20.. / local_128), B = (dStack_188, uVar28, local_120 / local_110); P1 = the first point */
    double Ay, Az, An[3], By = ys[1], Bz = zs[1], Bn[3] = { 0, 0, 1 };
    double P1y = ys[0], P1z = zs[0], P1n[3] = { ns[0][0], ns[0][1], ns[0][2] };
    if (hasn) { Bn[0] = Bn[1] = Bn[2] = 0; }              /* local_120 / local_110 are only initialised without normals */
    int on = 0;
    if (!(e <= fabs(V[i0][0] - pos))) {                   /* vertex i0 on the line */
        P1y = V[i0][1]; P1z = V[i0][2]; on = 1;
        if (hasn) memcpy(P1n, N[i0], 24);
    }
    if (e <= fabs(V[i1][0] - pos)) {
        Ay = P1y; Az = P1z; memcpy(An, P1n, 24);
    } else if (on == 0) {
        P1y = V[i1][1]; P1z = V[i1][2]; on = 1;
        if (hasn) memcpy(P1n, N[i1], 24);
        Ay = P1y; Az = P1z; memcpy(An, P1n, 24);
    } else {
        on = 2;
        double y1 = V[i1][1];
        if (y1 <= P1y) {                                  /* B = P1, A = vertex i1 */
            By = P1y; Bz = P1z; memcpy(Bn, P1n, 24);
            Ay = y1; Az = V[i1][2]; memcpy(An, P1n, 24);
            if (hasn) memcpy(An, N[i1], 24);
        } else {
            Ay = P1y; Az = P1z; memcpy(An, P1n, 24);
            By = y1; Bz = V[i1][2];
            if (hasn) memcpy(Bn, N[i1], 24);
        }
    }
    if (e <= fabs(V[i2][0] - pos)) {
        if (on != 2) { cs_cov[2]++; return 1; }
    } else {
        double y2 = V[i2][1];
        if (on == 1 || (on == 2 && !(y2 <= By))) {
            By = y2; Bz = V[i2][2];
            if (hasn) memcpy(Bn, N[i2], 24);
        } else if (on == 2) {
            if (y2 < Ay) { cs_cov[5]++; Ay = y2; Az = V[i2][2]; if (hasn) memcpy(An, N[i2], 24); }
        } else { cs_cov[2]++; return 1; }
    }
    cs_cov[3]++;
    if (cross) {                                          /* LAB_1801dc267: the crossing extends [A, B] */
        int toB = 0, toA = 0;
        if (By <= Ay) { if (cy < By) toB = 1; else if (Ay < cy) toA = 1; }
        else { if (cy < Ay) toA = 1; else if (By < cy) toB = 1; }
        if (toA || toB) cs_cov[4]++;
        if (toB) { By = cy; Bz = cz; memcpy(Bn, cn, 24); }
        if (toA) { Ay = cy; Az = cz; memcpy(An, cn, 24); }
    }
    p.s[0] = Ay; p.s[1] = By; p.z[0] = Az; p.z[1] = Bz;
    memcpy(p.n[0], An, 24); memcpy(p.n[1], Bn, 24);
    piece_out(S, &p);
    return 1;
}

/* FUN_1801daa60 (asm 1801daa60-1801db002, jump table 1801db004): from the first live group (+0x118); a group starting beyond the window
   waits, one ending before it dies; else 588 / 590 are set by its kind (FUN_1801da0c0 / FUN_1801da130) and its arm runs:
   - triangles with normals (kinds 10 / 11, 1801dac15): read in file order: those ending before the window are skipped, the first starting
     beyond it stops the group (its xmin moved there), the others become active nodes (FUN_1801df620: pushed at the front of +0x60);
   - edges (0 / 1, 1801dacda) and corners (2 / 3, 1801dae5d): cam3d_cscallop_edge.c cs_act_edges / cs_act_corners;
   - triangles without normals (4 / 5, 1801dab67): phase A writes them only when the scanner's param_2 (has normals) is 0; FUN_1801cfb60
     passes 1 (asm 1801d01f0-1801d025b, surf_classic3.md 12.3), so they are not on the Constant Scallop path: NOT PORTED. */
int cs_act_edges(CsScan *S, CsGroup *G);
int cs_act_corners(CsScan *S, CsGroup *G);
int cs_sect_edges(CsScan *S);
int cs_sect_corners(CsScan *S);
static int activate(CsScan *S, double pos, double lo, double hi, const char **why)
{
    S->pos = pos;                                         /* DAT_1804ea5c0, local_30a8 / dStack_30a0: the caller starts them at x0, x0 - 2 e,
                                                             2 e + x0 (l.326507-326508) and adds the step to each per line (l.326590) */
    S->lo = lo; S->hi = hi;
    S->first_next = S->first;                             /* local_30ac = local_30b0 */
    for (int gi = S->first; gi < S->ng; gi++) {
        CsGroup *G = &S->g[gi];
        if (G->n == 0) { if (gi == S->first) S->first_next = gi + 1; continue; }
        if (G->xmin > S->hi) continue;
        if (S->lo > G->xmax) { if (gi == S->first) S->first_next = gi + 1; G->n = 0; continue; }
        if (G->kind & 1) { S->off = S->off_chk; S->hoff = S->hoff_chk; } else { S->off = S->off_drive; S->hoff = S->hoff_drive; }   /* FUN_1801da0c0 / 1801da130 */
        if (G->kind <= 1) { if (cs_act_edges(S, G)) { if (why) *why = "scanner: edge activation failed (FUN_1801df420 error 0x196e)"; return -1; } continue; }
        if (G->kind <= 3) { cs_act_corners(S, G); continue; }
        if (G->kind != 10 && G->kind != 11) { if (why) *why = "scanner: triangle groups without normals (kinds 4 / 5) NOT PORTED (not on the op 43 path)"; return -1; }
        while (G->n > 0) {
            const double *r = S->tf + G->off / 8;
            if (S->lo > r[6]) { G->n--; G->off += 0x90; continue; }   /* v2.x before the window */
            if (r[0] > S->hi) { G->xmin = r[0]; break; }
            CsTri *T = S->pool;
            if (T) S->pool = T->next; else T = malloc(sizeof *T);
            memcpy(T->v, r, 9 * sizeof(double)); memcpy(T->n, r + 9, 9 * sizeof(double)); T->kind = G->kind;
            T->next = S->act; S->act = T;
            G->n--; G->off += 0x90;
        }
    }
    S->first = S->first_next;                             /* local_30b0 = local_30ac */
    return 0;
}
/* FUN_1801dba80 (asm 1801dba80): the active triangles - one ending before the window leaves the list (FUN_1801da010), one strictly across
   it goes to FUN_1801dc360, the others (and a FUN_1801dc360 refusal) to FUN_1801dbc50 */
static int sect_tris(CsScan *S, const char **why)
{
    CsTri *prev = NULL, *T = S->act;
    while (T) {
        CsTri *nx = T->next;
        if (S->lo > T->v[2][0]) {                         /* FUN_1801da010: out of the list into the free nodes; prev unchanged */
            if (prev) prev->next = nx; else S->act = nx;
            T->next = S->pool; S->pool = T;
            T = nx; continue;
        }
        int done = 0;
        if (S->lo > T->v[0][0] && T->v[2][0] > S->hi) done = cs_tri_span(S, T);
        if (!done && !cs_tri_section(S, T->kind, (const double (*)[3])T->v, (const double (*)[3])T->n, 0, 1, 2)) {
            if (why) *why = "scanner: FUN_1801dbc50 error 7000 (triangle kind)"; return -1;
        }
        prev = T; T = nx;
    }
    return 0;
}
int cs_line_tris(CsScan *S, double pos, double lo, double hi, const char **why)
{
    if (activate(S, pos, lo, hi, why)) return -1;
    return sect_tris(S, why);
}
/* one line: FUN_1801daa60, then FUN_1801db8c0 (asm 1801db8c0-1801dba72): DAT_180501db0 = FUN_1801dba80 (the triangles), the edge loop,
   the corner loop; its last call, FUN_1801e7350 (the batch to the envelope), is the caller's */
int cs_line_act(CsScan *S, double pos, double lo, double hi, const char **why) { return activate(S, pos, lo, hi, why); }
int cs_line(CsScan *S, double pos, double lo, double hi, const char **why)
{
    if (activate(S, pos, lo, hi, why)) return -1;
    return cs_line_sect(S, why);
}
int cs_line_sect(CsScan *S, const char **why)
{
    if (sect_tris(S, why)) return -1;
    if (cs_sect_edges(S)) { if (why) *why = "scanner: edge section failed (FUN_1801db8c0 error 0x1ce8)"; return -1; }
    if (cs_sect_corners(S)) { if (why) *why = "scanner: corner section failed"; return -1; }
    return 0;
}

void cs_piece_bytes(const CsPiece *p, unsigned char out[0x58])
{
    memset(out, 0, 0x58);
    memcpy(out, &p->code, 2);
    memcpy(out + 8, p->s, 16); memcpy(out + 0x18, p->z, 16); memcpy(out + 0x28, p->n, 48);
}
