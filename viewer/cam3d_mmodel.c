/* cam3d_mmodel.c - Constant Scallop stage A: what mcmsurf FUN_180229ba0 (build_machine_model's per-surface loop, decomp/mcmsurf_all.c
 * l.363762) does with one make_surf_model result (viewer/mcpartline_mesh.c plm_model) before the scanner reads it - formats/surf_classic3.md
 * 12.11. In memory: the files tmpedg0 / tmpcrn0 are kept as record arrays.
 *
 * FUN_180225000 (l.360776, disassembly 180225000..1802256bf), called at 18022bc4e when mms +0x54 (the edge count) is not 0:
 *   (ne = mms +0x54, nv = make_surf_model's vertex count, pts, nrm (reused as the per-vertex table, 0x18 bytes a vertex), f1, f2, f3,
 *    tol = chord x 0.9 x 0.01 ([rbp+0x90], 18022a2fe..18022a31c), tmpedg0 (positioned at mms +0x3c), tmpcrn0, &chained)
 *   1. per edge record (in file order) its position goes to the table entry of both its vertices: slot A (+0) when free, else slot B (+8).
 *   2. the chains (1802251f4..18022534e): per vertex k in order whose two slots are filled and not yet followed (+4 / +0xc = -1): from
 *      slot B, the vertex m >= k (searched from k each step) holding the same edge position in a slot not yet followed (A first); the
 *      record is copied to tmpedg1, both slots' second words (+4 / +0xc) get its new position (the start + the tmpedg1 offset), and the walk
 *      goes on from m's other slot; it ends when no such m is found.
 *   3. chained = (records copied == ne); when chained, tmpedg0's records of this surface are overwritten in the new order (1802253bc).
 *   4. the corners (1802253c6..18022565d): per vertex k in order with both second words set, the records at those two positions are read
 *      (read_edge_full, mccore 1805933e0: P1 P2 plane N1 N2, i1 i2, the four parameters, the flag) and FUN_180224ec0 writes the 0x70 corner
 *      record: k, P = A's end at k, d1 = A's other end - P, d2 = B's far end (B's P2 when k = B.i1, else P1) - P (each (0, 0, -2) when every
 *      |component| <= tol), N = A's normal at k, flag = A.flag || B.flag. Then (ne - corners) more copies of the first corner with flag 1.
 *   The positions are file offsets in the DLL; here record indices (the same order). When not chained the second words index records of
 *   tmpedg1's order while tmpedg0 keeps the old order: the corner pass then reads the old records at those indices, as the DLL does.
 * After it (l.364750-364768): not chained (or no edge) -> mms +0x74 = 0 and the tmpmm record rewritten. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mcam_load.h"
#include "mcpartline.h"
#include "cam3d_mmodel.h"

/* FUN_180224ec0 (disassembly 180224ec0..180224ff5): one corner record */
static void corner_rec(const double *P, const double *N, const double *Q, const double *R, int k, int flag, double tol, PlCorner *c)
{
    memset(c, 0, sizeof *c);
    c->vtx = k; c->flag = flag;
    for (int i = 0; i < 3; i++) { c->P[i] = P[i]; c->d1[i] = Q[i] - P[i]; c->d2[i] = R[i] - P[i]; c->N[i] = N[i]; }
    if (fabs(c->d1[0]) <= tol && fabs(c->d1[1]) <= tol && fabs(c->d1[2]) <= tol) { c->d1[0] = c->d1[1] = 0.0; c->d1[2] = -2.0; }
    if (fabs(c->d2[0]) <= tol && fabs(c->d2[1]) <= tol && fabs(c->d2[2]) <= tol) { c->d2[0] = c->d2[1] = 0.0; c->d2[2] = -2.0; }
}

int mm_corners(const PlEdge *E, int ne, int nv, double tol, MmCorners *o)
{
    memset(o, 0, sizeof *o);
    if (ne <= 0) return 0;
    int (*t)[4] = malloc(sizeof(int[4]) * (size_t)(nv > 0 ? nv : 1));   /* A, A', B, B' (+0, +4, +8, +0xc) */
    for (int k = 0; k < nv; k++) t[k][0] = t[k][1] = t[k][2] = t[k][3] = -1;
    for (int r = 0; r < ne; r++) {
        int v[2] = { E[r].i1, E[r].i2 };
        for (int q = 0; q < 2; q++) {
            if (v[q] < 0 || v[q] >= nv) { free(t); return -1; }       /* the DLL writes outside its table: NOT DECODED */
            if (t[v[q]][0] == -1) t[v[q]][0] = r; else t[v[q]][2] = r;
        }
    }
    int *ord = malloc(sizeof(int) * (size_t)ne), cnt = 0;              /* tmpedg1: the old index of each new record */
    for (int k = 0; k < nv; k++) {
        if (!(t[k][0] != -1 && t[k][1] == -1 && t[k][2] != -1 && t[k][3] == -1)) continue;
        int *cur = &t[k][2];
        for (;;) {
            int m, u = 0, found = 0;
            for (m = k; m < nv; m++) {
                if (&t[m][0] == cur || t[m][0] == -1 || &t[m][2] == cur || t[m][2] == -1) continue;
                if (t[m][0] != *cur && t[m][2] != *cur) continue;
                u = t[m][0] != *cur;
                if (t[m][2 * u + 1] != -1) continue;
                found = 1; break;
            }
            if (!found) break;
            if (cnt >= ne) { free(t); free(ord); return -1; }          /* tmpedg1 beyond the surface's records: NOT DECODED */
            ord[cnt] = *cur; cur[1] = cnt; t[m][2 * u + 1] = cnt; cnt++;
            cur = &t[m][u == 0 ? 2 : 0];
        }
    }
    o->chained = cnt == ne;
    o->order = ord; o->nord = cnt;
    /* the records tmpedg0 holds for the corner pass (and keeps): the new order when chained */
    const PlEdge **F = malloc(sizeof(PlEdge *) * (size_t)ne);
    for (int r = 0; r < ne; r++) F[r] = &E[o->chained ? ord[r] : r];
    o->c = malloc(sizeof(PlCorner) * (size_t)(ne > nv ? ne : nv) + sizeof(PlCorner));
    int first = -1; PlCorner c0; memset(&c0, 0, sizeof c0);
    for (int k = 0; k < nv; k++) {
        if (t[k][1] == -1 || t[k][3] == -1) continue;
        const PlEdge *A = F[t[k][1]], *B = F[t[k][3]];
        int a = k == A->i1 ? 0 : 1, b = k == B->i1 ? 1 : 0;
        const double *PA = a ? A->P2 : A->P1, *NA = a ? A->N2 : A->N1, *QA = a ? A->P1 : A->P2, *RB = b ? B->P2 : B->P1;
        corner_rec(PA, NA, QA, RB, k, A->flag || B->flag, tol, &o->c[o->nc]);
        if (first < 0) { first = k; c0 = o->c[o->nc]; }
        o->nc++;
    }
    int extra = ne - o->nc;
    if (extra > 0 && first < 0) { free(F); free(t); mm_corners_free(o); return -1; }   /* the DLL repeats an unset corner: NOT DECODED */
    for (int x = 0; x < extra; x++) {                                  /* 180225603..18022565d: the first corner again, flag 1 */
        PlCorner c = c0; c.flag = 1;
        const PlEdge *dummy = NULL; (void)dummy;
        o->c = realloc(o->c, sizeof(PlCorner) * (size_t)(o->nc + 1));
        o->c[o->nc++] = c;
    }
    o->edge = malloc(sizeof(PlEdge) * (size_t)ne);
    for (int r = 0; r < ne; r++) o->edge[r] = *F[r];
    o->nedge = ne;
    free(F); free(t);
    return 0;
}

void mm_corners_free(MmCorners *o) { free(o->order); free(o->c); free(o->edge); memset(o, 0, sizeof *o); }

void mm_corner_bytes(const PlCorner *c, unsigned char o[0x70])   /* FUN_180224ec0's record: +0 k, +8 P, +0x20 d1, +0x38 d2, +0x50 N, +0x68 flag */
{
    memset(o, 0, 0x70);
    memcpy(o, &c->vtx, 4); memcpy(o + 8, c->P, 24); memcpy(o + 0x20, c->d1, 24); memcpy(o + 0x38, c->d2, 24); memcpy(o + 0x50, c->N, 24);
    o[0x68] = (unsigned char)c->flag;
}

/* ---- the tmpmm / tmpmm1 files of one surface (FUN_180229ba0 l.364715-364768, asm 18022b9fc..18022bc6f) ----
 * tmpmm1 at offset mm1pos: u (nu + 1 doubles), v (nv + 1), the int n21 (make_surf_model's vertex count output, local_8258), then with trim
 * loops (mms +0x5c > 0) the loop table (nloops x 8) and its points (16 bytes each; count = last start + last count + 1 = mms +0x58).
 * tmpmm: the 0x78-byte mach_model_surf make_surf_model filled (box +8..+0x30, nu +0x48, nv +0x4c, triangles +0x50, edges +0x54, loops
 * +0x5c, mode +0x64), with FUN_180229ba0's own fields: +0x40 the tmpmm1 offset (8 bytes, local_7fc8), +0x58 the loop point count
 * (local_7fb0), +0x68 = 2 on the check_for_ridge arm (local_7fa0), +0x74 = 0 when FUN_180225000 did not chain the edges (LAB_18022bc61);
 * +0x3c = the surface's tmpedg0 offset (make_surf_model's; read back at 18022bbc7 to position the file for FUN_180225000). */
static void put32(unsigned char *o, int v) { memcpy(o, &v, 4); }
void mm_record(const PlModel *m, long long mm1pos, int edgpos, int ridge, unsigned char o[0x78])
{
    memset(o, 0, 0x78);
    memcpy(o + 8, m->box, 48);
    put32(o + 0x3c, edgpos); memcpy(o + 0x40, &mm1pos, 8);
    put32(o + 0x48, m->nu); put32(o + 0x4c, m->nv); put32(o + 0x50, m->ntri); put32(o + 0x54, m->nedge);
    put32(o + 0x58, m->nl ? m->nlp : 0); put32(o + 0x5c, m->nl); put32(o + 0x60, m->tb ? m->outer : 0);   /* +0x60 = local_2d44 (FUN_18057a980 l.1750) */
    short md = (short)m->mode; memcpy(o + 0x64, &md, 2);
    if (ridge) put32(o + 0x68, 2);
}
size_t mm_mm1_bytes(const PlModel *m, unsigned char **out)
{
    size_t n = 8 * ((size_t)m->nu + 1) + 8 * ((size_t)m->nv + 1) + 4 + (m->nl ? 8 * (size_t)m->nl + 16 * (size_t)m->nlp : 0);
    unsigned char *o = malloc(n), *q = o;
    memcpy(q, m->u, 8 * ((size_t)m->nu + 1)); q += 8 * ((size_t)m->nu + 1);
    memcpy(q, m->v, 8 * ((size_t)m->nv + 1)); q += 8 * ((size_t)m->nv + 1);
    put32(q, m->npts); q += 4;
    if (m->nl && m->tb) { for (int l = 0; l < m->nl; l++) { put32(q, m->tb[2 * l]); put32(q + 4, m->tb[2 * l + 1]); q += 8; } memcpy(q, m->lp, 16 * (size_t)m->nlp); }
    else if (m->nl) { put32(q, 0); put32(q + 4, m->nlp - 1); q += 8; memcpy(q, m->lp, 16 * (size_t)m->nlp); }
    *out = o;
    return n;
}

/* ---- fill_mach_model_buffer (mccore 18057e9c0, msm1.c l.3) for an untrimmed model (with or without the outline loop), one block: what the scanner gets ----
 * setup_mach_model_buffer (180598790, msm1.c l.154) reads tmpmm record k (FUN_1805935c0) and tmpmm1; FUN_1805798b0 (msm1.c l.2599) sizes the
 * block from the arena's room (DAT_1813fbec8 - the licence-dependent alloc_nc_all buffer in Mastercam, N13-23): with room for every grid
 * point and triangle ((ntri + ((nu + 1)(nv + 1) + loop points) x 4) x 0xc bytes) one block = the whole grid (+0x58 = nu, +0x5c = nv,
 * +0x60 = ntri) - the case ported (as plm_mesh assumes room). fill: FUN_18057db70 (the loop points: none here), FUN_18057f490 (FUN_18057a510
 * per grid point, u outer v inner: eval_surf order 1 + calc_surf_unit_norm through the scan frame F - for make_surf_model's own frame the
 * points of its last vertex pass),
 * FUN_18057ebd0 -> per cell FUN_18057f3c0 or, on the border of a model with the outline loop, FUN_18057ece0 / FUN_18057eeb0 (mcpartline_mesh.c
 * plm_model_tris); b = the cell's first grid vertex (+0x7c: +1 per cell, +1 more per u row), no orientation flip (param_3 = 0). */
static void rot3(double p[3], const double *F)   /* rot_view (mccore 180593a20): (y f.y + x f.x) + z f.z per row */
{
    double x = p[0], y = p[1], z = p[2];
    p[0] = y * F[1] + x * F[0] + z * F[2]; p[1] = y * F[4] + x * F[3] + z * F[5]; p[2] = y * F[7] + x * F[6] + z * F[8];
}
int mm_fill(const McamFile *f, const Surf *s, const PlModel *m, const double *F, MmFill *o, const char **why)
{
    memset(o, 0, sizeof *o);
    const Surf *h = s;          /* calc_surf_unit_norm gets the entity itself (its domain / size; plm_unit_norm_h) */
    if (s && s->kind == 10) {   /* FUN_18057f490: init_surf_const(.., 1) has no kind 10 case, so FUN_18057a510 evaluates the base (eval_surf -> find_base_surf) */
        s = mcam_surf_base(f, s);
        if (!s) { if (why) *why = "fill_mach_model_buffer: no base surface"; return -1; }
    }
    int nu = m->nu, nv = m->nv, ng = (nu + 1) * (nv + 1);
    o->np = ng + (m->nl ? m->nlp : 0);
    o->pts = malloc(sizeof(double) * 3 * (size_t)o->np); o->nrm = malloc(sizeof(double) * 3 * (size_t)o->np);
    /* FUN_18057a510 with param_6 = 1: the grid points (FUN_18057f490, u outer v inner) and, after them (base +0x64), the loop points
       (FUN_18057db70, evaluated by the first call) */
    for (int k = 0; k < o->np; k++) {
        double u = k < ng ? m->u[k / (nv + 1)] : m->lp[2 * (size_t)(k - ng)], v = k < ng ? m->v[k % (nv + 1)] : m->lp[2 * (size_t)(k - ng) + 1];
        double E[10][3], *P = o->pts + 3 * (size_t)k, *N = o->nrm + 3 * (size_t)k;
        if (plm_eval(f, h, 1, u, v, E)) { mm_fill_free(o); if (why) *why = "fill_mach_model_buffer: eval_surf failed"; return -1; }   /* eval_surf of the entity (kinds 10 / 8 followed) */
        N[0] = N[1] = N[2] = 0.0;
        plm_unit_norm(f, h, E[1], E[2], u, v, N);
        P[0] = E[0][0]; P[1] = E[0][1]; P[2] = E[0][2];
        if (F) { rot3(P, F); rot3(N, F); }   /* FUN_18057a510 param_8 (the scanner's DAT_1804ea4f2): no rot_view when 0 (F = NULL) */
    }
    if (plm_model_tris(m, &o->tri, &o->nt, why)) { mm_fill_free(o); return -1; }
    return 0;
}
void mm_fill_free(MmFill *o) { free(o->pts); free(o->nrm); free(o->tri); memset(o, 0, sizeof *o); }

/* ---- the HST drive mesh: mcmsurf FUN_180258310 (decomp/mcmsurf_all.c l.395467-396081) for one surface, formats/hst_linker.md 21 ----
 * For op 132 (Surface High Speed) msurf_use_emm = 1 (bnci_surface l.12265, every op type 0x84), so FUN_18012a310 (l.197248) builds the machine
 * model through FUN_180224190 (l.360106) = FUN_180229ba0(.., param_4 = 1, chord = msurf_chord_dev, d = 0, h = 0, c = 0, .., r = 1, ..): per drive
 * (and, with record 0x172c = 0, check) surface make_surf_model(frame, d = 0, h = 0, 0, 0, 0, r = 1, ent, tol = chord x 0.9, tol, ..) (l.364700;
 * local_824c = param_4 = 1 skips check_for_ridge and sets mms +0x68 = 2, l.364684 / l.364764). msurf_chord_dev = op +0x1238 = record 0x12a0 for
 * op 132 (msurf_map l.340012). process_3rd_party_algorithm FUN_1800199d0 (l.18215) -> FUN_180258310(uVar17, &zlo, &zhi) writes the binary
 * tmphardmillraw.stl mckernel's Database Loader reads (tools/hmm_macro.py), per tmpmm2 record of mms +0x68 == 2 (l.395787):
 *   - setup_mach_model_buffer / fill_mach_model_buffer (mm_fill) in the frame of the active view turned by cos / sin(pi / 2) (l.395718-395741;
 *     the Top view gives the rows (1, -c, -0), (c, 1, 0), (0, 0, 1), c = cos(pi / 2) in double, as tools/msm_case.py);
 *   - |mms zmax - zmin| < chord x 0.9 (strictly, l.395789-395790; mms +0x30 / +0x28): every point of the block gets z = mms zmin (l.395920-395926);
 *   - per triangle (a, b, c): op 0x84 with style 4 (Horizontal Area, op +0x1654 = record 0x16bc) and XY stock < Z stock (op +0x1358 / +0x1360 =
 *     record 0x13c0 / 0x13c8, l.395652-395661): a triangle whose corner z differ pairwise by <= record 0x12a0 x 0.01 (op +0x1238 x 0.01) is
 *     raised by (float)(Z - XY stock) (l.395939-395947);
 *   - the corners x 25.4 for an inch part (0x4039666666666666, l.395642-395649), FUN_1802284c0 (l.362853) writes them as floats (CVTSD2SS) with
 *     the facet normal; the lowest / highest corner z before the scaling go to hstJobIdDat +0x28 / +0x30 (l.395960-395988).
 * Here: the triangles in model units as 9 doubles each = the float the STL holds / the unit factor (mchmm works on those floats in mm); *zlo /
 * *zhi as FUN_180258310 returns them. A surface whose model has no triangle (mms +0x50 < 1) gives none (FUN_180229ba0 skips it, l.364707).
 * Returns the triangle count, -1 refused (why). */
static void mm_hst_frame(double F[9], const double *V)
{
    /* l.395718-395741 (FUN_180229ba0 l.364023-364060 the same) on the active Cplane's view (mcViews entry rows +8..+0x48; V, NULL = Top); the
       products in the decompile's order */
    double c = cos(1.570796326794896), s = sin(1.570796326794896);
    static const double TOP[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    if (!V) V = TOP;
    const double m0 = V[0], m1 = V[1], m2 = V[2], m3 = V[3], m4 = V[4], m5 = V[5], m6 = V[6], m7 = V[7], m8 = V[8];
    F[0] = -(-s * m0 + c * m3 + m6 * 0.0); F[1] = -(c * m4 + -s * m1 + m7 * 0.0); F[2] = -(c * m5 + -s * m2 + m8 * 0.0);   /* local_2e30 */
    F[3] = s * m3 + c * m0 + m6 * 0.0; F[4] = c * m1 + s * m4 + m7 * 0.0; F[5] = s * m5 + c * m2 + m8 * 0.0;                /* local_2e18 */
    F[6] = m6; F[7] = m7; F[8] = m8;                                                                                        /* +0x38..+0x48 */
}
int mm_hst_surface(const McamFile *f, const Surf *s, double chord, int metric, int style4, double sxy, double sz, double **out,
                   const double *V, double *zlo, double *zhi, int *first, const char **why)
{
    *out = NULL;
    PlModelIn in; memset(&in, 0, sizeof in);
    in.tol = chord * 0.9; in.r = 1;                                          /* FUN_180229ba0 l.364101: tol = chord x 0.9; r = param_12 = 1 */
    mm_hst_frame(in.frame, V);                                               /* FUN_180229ba0's frame (l.364023-364060): the same rows */
    PlModel m; const char *w = NULL;
    if (plm_model(f, s, &in, &m, &w)) { if (why) *why = w; return -1; }
    if (m.ntri < 1) { plm_model_free(&m); return 0; }
    MmFill mf; double F[9]; mm_hst_frame(F, V);
    if (mm_fill(f, s, &m, F, &mf, &w)) { plm_model_free(&m); if (why) *why = w; return -1; }
    if (fabs(m.box[5] - m.box[4]) < in.tol)                                  /* l.395789: ABS(zmax - zmin) <= tol && != tol */
        for (int k = 0; k < mf.np; k++) mf.pts[3 * (size_t)k + 2] = m.box[4];
    double t1 = chord * 0.01, dz = sz - sxy, k = metric ? 1.0 : 25.4;        /* dVar1 (op 0x84: op +0x1238 x 0.01), dVar2, the unit factor */
    int raise = style4 && sxy < sz;                                          /* local_2fa4 */
    double *o = malloc(sizeof(double) * 9 * ((size_t)mf.nt + 1));
    for (int t = 0; t < mf.nt; t++) {
        double V[3][3];
        for (int c = 0; c < 3; c++) memcpy(V[c], mf.pts + 3 * (size_t)mf.tri[3 * t + c], sizeof V[c]);
        if (raise && fabs(V[0][2] - V[1][2]) <= t1 && fabs(V[1][2] - V[2][2]) <= t1 && fabs(V[2][2] - V[0][2]) <= t1) {
            float fz = (float)dz;                                            /* fVar26 = (float)dVar2 */
            for (int c = 0; c < 3; c++) V[c][2] = V[c][2] + (double)fz;
        }
        for (int c = 0; c < 3; c++) {                                        /* *param_2 / *param_3: the first corner sets both, then min / max */
            double z = V[c][2];
            if (*first) { *zlo = *zhi = z; *first = 0; }
            if (*zlo <= z) { if (*zhi <= z && z != *zhi) *zhi = z; } else *zlo = z;
            for (int a = 0; a < 3; a++) o[9 * (size_t)t + 3 * c + a] = (double)(float)(V[c][a] * k) / k;   /* x 25.4, CVTSD2SS (FUN_1802284c0) */
        }
    }
    int nt = mf.nt;
    mm_fill_free(&mf); plm_model_free(&m);
    *out = o;
    return nt;
}
