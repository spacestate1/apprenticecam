/* mcpartline.c - Wireframe > Curve Parting Line's kernel after the triangles: mccore parting_line (0x180566740) -> FUN_180565bc0 -> FUN_180566220 and the curves.
 * Spec: ../formats/wireframe.md "Curve Parting Line"; decompiles decomp/partline/core_pl.c, core_post.c; disassembly decomp/partline/core_pl_asm.txt
 * (parting_line, FUN_180565bc0, FUN_180566220, FUN_1805647a0, FUN_180564b70, FUN_180565990) and chain_asm.txt (FUN_180551e00, FUN_180534ce0).
 * Oracle: tools/peloader --partline (mccore_pl.c), tools/partline_port.c compares. Names: surf +8 / +0x10 / +0x18 / +0x20 = u0, u1, v0, v1 (ent +0x70 ..),
 * surf +6 = ent +0x6e: bit 1 u closed, bit 2 v closed, bit 4 the normal reversed. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcpartline.h"
#include "mcsurf.h"
#include "mcsurf_nrm.h"
#include "mcspline_fit.h"

#define BIG 1e38                 /* 0x47d2ced32a16a1b1 */

static double dist3(const double *a, const double *b) { double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return sqrt(x * x + y * y + z * z); }

/* eval_surf(surf, 1, u, v) + calc_surf_unit_norm(surf, Su, Sv, un, vn) (the u, v the normal gets are the caller's: FUN_180566220 passes a stale v, see pl_points),
   the normal negated with surf +6 bit 4 (FUN_180566220 1805663e1 .. 180566429) */
static void eval_pn(const McamFile *f, const Surf *s, double u, double v, double un, double vn, double P[3], double N[3])
{
    double D[10][3]; memset(D, 0, sizeof D);
    plm_eval(f, s, 1, u, v, D);
    P[0] = D[0][0]; P[1] = D[0][1]; P[2] = D[0][2];
    plm_unit_norm(f, s, D[1], D[2], un, vn, N);
    if (s->rec[0x6e] & 4) { N[0] = -N[0]; N[1] = -N[1]; N[2] = -N[2]; }
}
static void eval_p0(const McamFile *f, const Surf *s, double u, double v, double P[3])   /* eval_surf(surf, 0, u, v) */
{
    double D[10][3]; memset(D, 0, sizeof D);
    plm_eval(f, s, 0, u, v, D);
    P[0] = D[0][0]; P[1] = D[0][1]; P[2] = D[0][2];
}

/* FUN_180564740 (180564740): d = N . V; |angle| > 1e-4 ([0x180cf3ad0]): cos(acos(d) - angle) */
static double pl_g(const double N[3], const double V[3], double ang)
{
    double d = N[1] * V[1] + N[0] * V[0] + N[2] * V[2];
    if (fabs(ang) > 1e-4) d = cos(acos(d) - ang);
    return d;
}

/* FUN_1805647a0 (1805647a0, from the disassembly 18056482d .. 180564ae7: COMISD + JBE / SETBE, so a NaN g (acos of N . V just above 1) counts as
   "not near 0" and as negative, unlike the decompile's C reading): either end where g is near 0 (-1e-12 < g < 1e-12; the other not), else on a
   sign change regula falsi in (u, v) from the end A (g > 0: the first end when g0 > 0, else the second) towards B, at most 100 steps, until
   -1e-12 < g < 1e-12. 1 = found (Pout, uvout) */
#define NEAR0(x) ((x) > -1e-12 && 1e-12 > (x))   /* COMISD x, -1e-12; JBE not taken, then COMISD 1e-12, x; SETBE == 0 (false for NaN) */
static int sgn(double x) { return x > -1e-12 ? !(1e-12 > x) : -1; }   /* 1805648cf .. 1805648f5: -1 for x <= -1e-12 or NaN, 1 for x >= 1e-12, 0 between */
static int pl_cross(const McamFile *f, const Surf *s, const double V[3], double ang, const double P[6], const double uv[4], const double N0[3], const double N1[3], double Pout[3], double uvout[2])
{
    double f0 = pl_g(N0, V, ang), f1 = pl_g(N1, V, ang);
    if (NEAR0(f0)) {   /* 180564841 .. 180564892 */
        if (NEAR0(f1)) return 0;
        Pout[0] = P[0]; Pout[1] = P[1]; Pout[2] = P[2]; uvout[0] = uv[0]; uvout[1] = uv[1]; return 1;   /* the first end */
    }
    if (NEAR0(f1)) { Pout[0] = P[3]; Pout[1] = P[4]; Pout[2] = P[5]; uvout[0] = uv[2]; uvout[1] = uv[3]; return 1; }   /* 180564897 .. 1805648ca: the second end */
    if (sgn(f0) == sgn(f1)) return 0;
    double A[2], B[2], fa, fb;   /* A ([RSP+0x50]) with fa (XMM10), B ([RSP+0x40]) with fb (XMM6): 180564924 .. 180564954 */
    if (f0 > 0.0) { A[0] = uv[0]; A[1] = uv[1]; B[0] = uv[2]; B[1] = uv[3]; fa = f0; fb = f1; }
    else { A[0] = uv[2]; A[1] = uv[3]; B[0] = uv[0]; B[1] = uv[1]; fa = f1; fb = f0; }
    for (int it = 0; it < 100; it++) {
        double t = fabs(fa) / (fabs(fb) + fabs(fa));
        uvout[0] = A[0] * (1.0 - t) + B[0] * t; uvout[1] = A[1] * (1.0 - t) + B[1] * t;
        double Q[3], N[3]; eval_pn(f, s, uvout[0], uvout[1], uvout[0], uvout[1], Q, N);
        double g = pl_g(N, V, ang);
        if (NEAR0(g)) { Pout[0] = Q[0]; Pout[1] = Q[1]; Pout[2] = Q[2]; return 1; }
        if (g > 0.0) { A[0] = uvout[0]; A[1] = uvout[1]; fa = g; }   /* 180564ac3: COMISD g, 0; JBE: NaN to B */
        else { B[0] = uvout[0]; B[1] = uvout[1]; fb = g; }
    }
    return 0;
}

/* FUN_180554e40: a node within tol of P (strictly) */
static int near_node(const double *P, int n, const double *nodes, double tol)
{
    for (int i = 0; i < n; i++) if (dist3(nodes + 3 * i, P) < tol) return 1;
    return 0;
}
/* FUN_180554f10: a boundary point (bpts[] are node indices) within tol */
static int near_bpt(const double *P, int nb, const short *bpts, const double *nodes, double tol)
{
    for (int i = 0; i < nb; i++) if (dist3(nodes + 3 * bpts[i], P) < tol) return 1;
    return 0;
}
/* FUN_180535430: the nearest node (the first one under 1e-8 at once), -1 none */
static int nearest_node(const double *P, int n, const double *nodes, double *dist)
{
    int best = -1; *dist = 1e38;
    for (int i = 0; i < n; i++) { double d = dist3(P, nodes + 3 * i); if (d < *dist) { *dist = d; best = i; if (d < 1e-8) return i; } }
    return best;
}

/* FUN_180541730 / FUN_1805419b0 for a NURBS (case 0): the u / v iso counts (deg + 1) (ncp - 1), v times 4 when +0x113 bit 0 is clear, clamped to 9 .. 49, plus 1 */
static int iso_count(const Surf *s, int vdir)
{
    const uint8_t *r = s->rec; int nc, deg, n;
    memcpy(&nc, r + (vdir ? 0x10c : 0x108), 4); deg = r[vdir ? 0x112 : 0x111];
    n = (deg + 1) * (nc - 1);
    if (vdir && !(r[0x113] & 1)) n *= 4;
    if (n < 0x32) { if (n < 9) n = 9; } else n = 0x31;
    return n + 1;
}
/* FUN_180565990: the steps along a boundary edge A -> B: the iso count of the direction minus 1 (an edge in u or v; else 1) and the chord count by the
   distance of the ends (an edge of a closed direction whose ends meet: twice the count to its middle), the smaller */
static int edge_steps(const McamFile *f, const Surf *s, const double A[2], const double B[2], double step)
{
    int dir = -1, n;
    if (0.0001 <= fabs(B[0] - A[0])) { if (fabs(B[1] - A[1]) < 0.0001) dir = 1; }
    else dir = 0;
    n = 100; if (dir < 0) n = 1;
    if (dir == 0) n = iso_count(s, 1) - 1;
    else if (dir == 1) n = iso_count(s, 0) - 1;
    double Pa[3], Pb[3]; eval_p0(f, s, A[0], A[1], Pa); eval_p0(f, s, B[0], B[1], Pb);
    short k = (short)(int)(dist3(Pb, Pa) / step) + 1;
    if (k < 1) k = 1;
    else if (k != 1) goto done;
    {
        double u, v; int fl = s->rec[0x6e];
        if (!(fl & 2) || dir != 0) {
            if (!(fl & 1) || dir != 1) goto done;
            v = B[1]; u = (A[0] + B[0]) * 0.5;
        } else { u = B[0]; v = (B[1] + A[1]) * 0.5; }
        eval_p0(f, s, u, v, Pb);
        k = (short)(int)(dist3(Pb, Pa) / step) + 1;
        if (k < 1) k = 1;
        k = k * 2;
    }
done:
    if (k < n) n = k;
    return n;
}

/* CSplineNodeMgr's spline nodes 1 (CS-5, formats/chaining.md "The node buffer"): one buffer for the session, DAT_1813c8008 (p_3d records),
   made by CSplineNodeMgr::AllocateMem (mccore 0x18027c3e0, decomp/partline/core_nodemgr.c: DeallocateMem, then calloc(96000) = 4000
   zeroed records, DAT_1813c8000 = 4000), which runs at dbinit (mccore 0x1802c4770, the database init of a file open, FUN_1803509a0 l.256)
   and init_db (mastercam.dll 0x18033ae30, File > New, decomp/mastercam_filenew.c l.157): csn_reset, called by scene.c load_file /
   new_file. AllocateMemNPts (0x18027c4e0) grows it for more than DAT_1813c8000 records to the next 1000 above n + 5, keeping the records
   (FUN_18027c320: here realloc, the new tail zeroed). Every user here that Mastercam has on this buffer takes it: the trim walk of
   build_edgecrv_on_surf (ec_build_edgecrv_list: it reads one record past its count) and Curve Parting Line (plp_points' P). Ours: the other
   Mastercam users of nodes 1 (create_spline's nodes, Roll, Curve Surface, Loft, ...) keep their own arrays here - what they leave in
   Mastercam's buffer is not mirrored (NOT PORTED). */
static double (*csn_buf)[3]; static int csn_cap;
void csn_reset(void) { free(csn_buf); csn_cap = 4000; csn_buf = calloc((size_t)csn_cap, sizeof *csn_buf); }
double (*csn_nodes1(int n))[3] {
    if (!csn_buf) csn_reset();
    if (n > csn_cap) {   /* AllocateMemNPts */
        int c = csn_cap + ((n + 5 - csn_cap) / 1000 + 1) * 1000;
        double (*b)[3] = realloc(csn_buf, (size_t)c * sizeof *b); if (!b) return NULL;
        memset(b + csn_cap, 0, (size_t)(c - csn_cap) * sizeof *b); csn_buf = b; csn_cap = c;
    }
    return csn_buf;
}
int plp_alloc(PlPts *pp) { memset(pp, 0, sizeof *pp); pp->P = (double *)csn_nodes1(4100); pp->UV = calloc(4100, 2 * sizeof(double)); return pp->P && pp->UV ? 0 : -1; }   /* 96000-byte node arrays (4000 points); P = the session's nodes 1 */
void plp_free(PlPts *pp) { free(pp->UV); pp->P = pp->UV = NULL; }   /* (P is the session's buffer: kept) */

/* FUN_180564b70, untrimmed branch (1805653c2 .. 180565986): the four edges (u0 v0 -> u1 v0, -> u1 v1, -> u0 v1, -> u0 v0; the third skipped on a v-closed
   surface, the fourth on a u-closed one), marched in edge_steps steps; a crossing not within tol of a boundary point replaces the nearest node and becomes a
   boundary point; on edge 0 of a v-closed surface (edge 1 of a u-closed one) a copy at v1 (u0) is added as a new node and boundary point too. At most 100. */
static void pl_boundary(const McamFile *f, const Surf *s, const double V[3], double ang, PlPts *pp, int metric, double tol)
{
    double u0, u1, v0, v1; memcpy(&u0, s->rec + 0x70, 8); memcpy(&u1, s->rec + 0x78, 8); memcpy(&v0, s->rec + 0x80, 8); memcpy(&v1, s->rec + 0x88, 8);
    int fl = s->rec[0x6e];
    double A[2] = { 0, 0 }, B[2] = { 0, 0 };
    pp->nb = 0;
    for (int k = 0; k < 4; k++) {
        if (k == 2 && (fl & 2)) continue;
        if (k == 3 && (fl & 1)) continue;
        if (k == 0) { A[0] = u0; A[1] = v0; B[0] = u1; B[1] = v0; }
        else if (k == 1) { A[0] = u1; A[1] = v0; B[0] = u1; B[1] = v1; }
        else if (k == 2) { A[0] = u1; A[1] = v1; B[0] = u0; }   /* B.v kept (180565453: only B.u written) */
        else { A[0] = u0; A[1] = v1; B[0] = u0; B[1] = v0; }
        int n = edge_steps(f, s, A, B, metric ? 1.0 : 0.04);   /* IsMetric ? 1.0 : 0.04 (XMM12 / XMM13) */
        double du = (B[0] - A[0]) / (double)n, dv = (B[1] - A[1]) / (double)n;
        double PP[6], UV[4], N0[3], N1[3];
        eval_pn(f, s, A[0], A[1], A[0], A[1], PP, N0);
        UV[0] = A[0]; UV[1] = A[1];
        for (int st = 1; st <= n; st++) {
            if (st == n) { A[0] = B[0]; A[1] = B[1]; }
            else if (k == 0 || k == 2) A[0] += du;
            else A[1] += dv;
            eval_pn(f, s, A[0], A[1], A[0], A[1], PP + 3, N1);
            UV[2] = A[0]; UV[3] = A[1];
            double Q[3], q[2];
            if (pl_cross(f, s, V, ang, PP, UV, N0, N1, Q, q) && !near_bpt(Q, pp->nb, pp->bpts, pp->P, tol)) {
                double dd; pp->bpts[pp->nb] = -1;
                int i = nearest_node(Q, pp->np, pp->P, &dd);
                if (i >= 0) { memcpy(pp->P + 3 * i, Q, sizeof Q); memcpy(pp->UV + 2 * i, q, sizeof q); }   /* -1 (no node) cannot happen: the caller runs this with nodes */
                pp->bpts[pp->nb++] = (short)i;
                if (pp->nb == 100) return;
                int dup = 0;
                if (k == 0 && (fl & 2)) { q[1] = v1; dup = 1; }
                else if (k == 1 && (fl & 1)) { q[0] = u0; dup = 1; }
                if (dup) {
                    pp->bpts[pp->nb++] = (short)pp->np;
                    memcpy(pp->P + 3 * pp->np, Q, sizeof Q); memcpy(pp->UV + 2 * pp->np, q, sizeof q); pp->np++;
                    if (pp->nb == 100) return;
                }
            }
            UV[0] = UV[2]; UV[1] = UV[3]; memcpy(N0, N1, sizeof N0); memcpy(PP, PP + 3, 3 * sizeof(double));
        }
    }
}

#define PL_MAXPTS 4000           /* CSplineNodeMgr::get_max_n_spline_pts: DAT_1813c8000 = 4000 (CSplineNodeMgr::AllocateMem 0x18027c3e0) */

static void swap3(double *P, int a, int b) { double t[3]; memcpy(t, P + 3 * a, sizeof t); memcpy(P + 3 * a, P + 3 * b, sizeof t); memcpy(P + 3 * b, t, sizeof t); }
static void swap2(double *P, int a, int b) { double t[2]; memcpy(t, P + 2 * a, sizeof t); memcpy(P + 2 * a, P + 2 * b, sizeof t); memcpy(P + 2 * b, t, sizeof t); }
static void unit3(const double *a, const double *b, double o[3])   /* unit(a - b), 0 when the length is 0 */
{
    double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2], l = sqrt(y * y + x * x + z * z);
    if (l == 0.0) { o[0] = o[1] = o[2] = 0; return; }
    o[0] = x / l; o[1] = y / l; o[2] = z / l;
}

/* FUN_180551e00 (180551e00; parting_line passes surface 2 = NULL, so its param_8 / bVar32 parts are left out): the nodes ordered into chains in place.
   Starts: the boundary points first (bpts[i], i < nb), then every node in order; a chain grows by the nearest unused node (beyond tol; past
   chord / 2 * 50 the middle of the two must lie on the surface within chord / 2 * 10), turning back (direction cosine < 0) refused, a near-forward candidate
   (cos >= the best - 0.1) preferred when within 3 x the nearest distance; a chain longer than 3 nodes closes on its start; an interior start is grown the other
   way afterwards. cnt[] = the nodes per chain. Returns 0, -1 = too many nodes (post_warning 0x14e4) */
static int pl_chain(const McamFile *f, const Surf *s, PlPts *pp, double tol, double chord)
{
    double *P = pp->P, *UV = pp->UV;
    int np = pp->np, nb = pp->nb, nbm1 = nb - 1;
    short *bpts = pp->bpts;
    char *used = calloc((size_t)np + 0x32, 1);
    if (!used) return 0;
    int rc = 0, w = 0, l214 = 0, l1f6 = 0;
    pp->nch = 0;
    double prevdir[3] = { 0, 0, 0 };
    for (int i = 0; i < np; i++) {
        int start = i;
        if (i <= nbm1) start = bpts[i];
        l214 = w;
        if (used[start]) { w = l214; continue; }
        double S0[3]; memcpy(S0, P + 3 * start, sizeof S0);
        swap3(P, w, start);
        { char t = used[w]; used[w] = 0; used[start] = t; }
        int b14 = 0;
        if (i < nb) {
            int j;
            for (j = 0; j < nb; j++) if (i != j && bpts[j] == w) { bpts[j] = bpts[i]; break; }
            bpts[i] = (short)w;
        } else b14 = 1;
        double uvsave[2];
        int uva = w, uvb = start;   /* the first pass swaps UV[w] and UV[start]; the second UV[l214] with itself (1805530ad .. 180553123) */
        for (;;) {
            uvsave[0] = UV[2 * uvb]; uvsave[1] = UV[2 * uvb + 1];
            swap2(UV, uva, uvb);
            l214++;
            int b13 = 0, cntc = 1, l212 = 0, l1f4 = 0, l1f8 = 0;
            double dot = 0.0;   /* dVar7 */
            for (;;) {
                double l1f0 = BIG, best = BIG, bestback = BIG, d8 = 0.0; int b12 = 0;
                if (np <= w) break;
                for (int m = w; m < np; m++) {
                    if (used[m] || !(cntc > 2 || m != w)) continue;
                    int last = l214 - 1;
                    double d = dist3(P + 3 * last, P + 3 * m), dd = d;
                    if (!(tol <= d)) continue;
                    if (!(best <= d || dist3(P + 3 * m, P + 3 * last) <= chord * 0.5 * 50.0)) {
                        double uvm[2] = { (UV[2 * last] + UV[2 * m]) * 0.5, (UV[2 * last + 1] + UV[2 * m + 1]) * 0.5 }, Q[3], mid[3];
                        for (int k = 0; k < 3; k++) mid[k] = (P[3 * last + k] + P[3 * m + k]) * 0.5;
                        eval_p0(f, s, uvm[0], uvm[1], Q);
                        if (chord * 0.5 * 10.0 < dist3(mid, Q)) dd = BIG;   /* (kind 10: in_polygon of the middle; refused before this for trimmed surfaces) */
                    }
                    int to688 = 0;
                    if (dd >= BIG) to688 = 1;
                    else if (cntc > 1) { double dir[3]; unit3(P + 3 * m, P + 3 * last, dir); dot = prevdir[0] * dir[0] + prevdir[1] * dir[1] + prevdir[2] * dir[2]; to688 = 1; }
                    if (to688 && cntc > 1 && dd < BIG && (dd < best || d8 < fabs(dot))) {
                        if (dot < 0.0) {
                            dd = BIG;
                            if (cntc == 2 && i < nbm1) { double e = dist3(S0, P + 3 * m); if (e < bestback && tol < e) { b12 = 1; bestback = e; l1f8 = m; } }
                        } else if (d8 - 0.1 <= dot && dd < l1f0) { l1f0 = dd; d8 = dot; l1f4 = m; }
                    }
                    if (dd < best) { best = dd; l212 = m; }
                    if (best < 1e-8) break;
                }
                if (best >= BIG) break;
                int s16 = l212;
                if (b12 && bestback * 2.0 < best) {
                    swap3(P, np - 1, s16);
                    { char t = used[np - 1]; used[np - 1] = 1; used[s16] = t; }
                    for (int j = 0; j < nb; j++) if (bpts[j] == np - 1) { bpts[j] = (short)s16; break; }
                    l214--; cntc--; l212 = l1f8; best = bestback; s16 = l1f8;
                }
                if (cntc > 1) { l212 = s16; if (l1f0 < best * 3.0) { s16 = l1f4; l212 = l1f4; } }
                double Q[3]; memcpy(Q, P + 3 * s16, sizeof Q);
                int newnp = np;
                if (cntc > 3) {
                    int closeit = 1;
                    if (tol <= dist3(S0, Q)) {
                        int last = l214 - 1; double a = dist3(P + 3 * last, Q), b = dist3(S0, Q);
                        if (a <= b || a <= dist3(S0, P + 3 * last)) closeit = 0;
                        else { memcpy(P + 3 * l214, S0, sizeof S0); UV[2 * l214] = uvsave[0]; UV[2 * l214 + 1] = uvsave[1]; }
                    }
                    if (closeit) {
                        b14 = 0; b13 = 1;
                        if (PL_MAXPTS <= np) { rc = -1; goto out; }
                        memcpy(P + 3 * np, S0, sizeof S0); UV[2 * np] = uvsave[0]; UV[2 * np + 1] = uvsave[1];
                        s16 = np; newnp = np + 1; l212 = np;
                    }
                }
                np = newnp;
                used[s16] = used[l214]; used[l214] = 1;
                swap3(P, l214, s16); swap2(UV, l214, s16);
                if (!b13) {
                    int l206 = 0;
                    for (int j = 0; j < nb; j++) if (bpts[j] == s16) { b13 = 1; l206 = j; break; }
                    for (int j = 0; j < nb; j++) if (bpts[j] == l214) { bpts[j] = (short)s16; break; }
                    if (b13) bpts[l206] = (short)l214;
                }
                if (tol < dist3(P + 3 * (l214 - 1), P + 3 * l214)) { unit3(P + 3 * l214, P + 3 * (l214 - 1), prevdir); l214++; cntc++; }
                if (b13 || np <= l214) goto chain_end;
            }
            b13 = 1;
            if (cntc == 1) b14 = 0;
        chain_end:
            used[w] = 1;
            if (!b13 && cntc > 1) {
                double d1[3], d2[3]; unit3(P + 3 * (l214 - 1), P + 3 * (l214 - 2), d1); memcpy(prevdir, d1, sizeof d1); unit3(S0, P + 3 * (l214 - 1), d2);
                if (0.7071 < d1[0] * d2[0] + d1[1] * d2[1] + d1[2] * d2[2]) {
                    if (PL_MAXPTS <= np) { rc = -1; goto out; }
                    memcpy(P + 3 * np, S0, sizeof S0); UV[2 * np] = uvsave[0]; UV[2 * np + 1] = uvsave[1];
                    l214++; np++;
                }
            }
            if (0 < l214 - l1f6) {
                if (pp->nch < 4096) pp->cnt[pp->nch] = (short)(l214 - l1f6);
                pp->nch++;
                if (!b13) goto out;
            }
            l1f6 = l214;
            if (!b14 || PL_MAXPTS <= np) break;
            for (int k = np - 1; k >= l214; k--) { memcpy(P + 3 * (k + 1), P + 3 * k, 3 * sizeof(double)); memcpy(UV + 2 * (k + 1), UV + 2 * k, 2 * sizeof(double)); }
            memcpy(P + 3 * l214, P + 3 * start, 3 * sizeof(double)); memcpy(UV + 2 * l214, UV + 2 * start, 2 * sizeof(double));
            np++; b14 = 0; start = l214; uva = uvb = l214;
        }
        w = l214;
    }
out:
    free(used);
    pp->np = np;
    return rc;
}

/* FUN_180534ce0 (180534ce0): on a closed direction (surf +6 bit 1: u, bit 2: v) each chain's end node lying on the seam (within 1e-8 of u0 or u1) is
   moved to the other side when its neighbour in the chain is nearer to that side */
static void pl_seams(const Surf *s, PlPts *pp)
{
    double u0, u1, v0, v1; memcpy(&u0, s->rec + 0x70, 8); memcpy(&u1, s->rec + 0x78, 8); memcpy(&v0, s->rec + 0x80, 8); memcpy(&v1, s->rec + 0x88, 8);
    int bu = s->rec[0x6e] & 1, bv = (s->rec[0x6e] >> 1) & 1;
    if (!bu && !bv) return;
    double *UV = pp->UV; int st = 0;
    for (int c = 0; c < pp->nch; c++) {
        int n = pp->cnt[c];
        for (int k = 0; k < 2; k++) {   /* k 0: u (bit 1), 1: v (bit 2) */
            if (k == 0 ? !bu : !bv) continue;
            double lo = k ? v0 : u0, hi = k ? v1 : u1;
            for (int e = 0; e < 2; e++) {
                int a = e ? st + n - 1 : st, b = e ? st + n - 2 : st + 1;   /* the first node and its neighbour, then the last and its neighbour */
                double x = UV[2 * a + k], y = UV[2 * b + k];
                if (fabs(x - lo) < 1e-8 && !(fabs(y - lo) <= fabs(hi - y))) UV[2 * a + k] = hi;
                else if (fabs(x - hi) < 1e-8 && fabs(lo - y) < fabs(y - hi)) UV[2 * a + k] = lo;
            }
        }
        st += n;
    }
}

/* FUN_180566220 (180566220): every triangle edge (0-1, 1-2, 2-0) through FUN_1805647a0, the crossings not within tol of a node kept (at most 4000), then
   the boundary (FUN_180564b70), the chains (FUN_180551e00) and the seams (FUN_180534ce0) */
int plp_points(const McamFile *f, const Surf *s, const PlMesh *m, const double V[3], double ang, double chord, double tol, int metric, PlPts *pp)
{
    double stale_v = 0.0;   /* [RBP-0x60] (the previous triangle's last uv1.v): calc_surf_unit_norm of the first vertex gets it as its v (1805663ae .. 1805663dc);
                               uninitialised stack before the first triangle in the DLL: 0 here [inferred]; only a degenerate normal reads it */
    pp->np = pp->np0 = 0; pp->nb = 0; pp->nch = 0; pp->cancel = 0;
    for (int t = 0; t < m->ntri; t++) {
        const int *tr = m->tri + 3 * t;
        double PP[6], UV[4], N0[3], N1[3], Q[3];
        memcpy(PP, m->xyz + 3 * tr[0], 3 * sizeof(double)); memcpy(UV, m->uv + 2 * tr[0], 2 * sizeof(double));
        eval_pn(f, s, UV[0], UV[1], UV[0], stale_v, Q, N0);   /* the position eval_surf writes is not used (the mesh's xyz is) */
        for (int k = 1; k < 4; k++) {
            int j = k == 3 ? 0 : k;
            memcpy(PP + 3, m->xyz + 3 * tr[j], 3 * sizeof(double)); memcpy(UV + 2, m->uv + 2 * tr[j], 2 * sizeof(double));
            eval_pn(f, s, UV[2], UV[3], UV[2], UV[3], Q, N1);
            stale_v = UV[3];
            double X[3], x[2];
            if (pl_cross(f, s, V, ang, PP, UV, N0, N1, X, x) && !near_node(X, pp->np, pp->P, tol)) {
                memcpy(pp->P + 3 * pp->np, X, sizeof X); memcpy(pp->UV + 2 * pp->np, x, sizeof x);
                if (++pp->np == PL_MAXPTS) { pp->np0 = pp->np; return 0; }   /* 1805665de: the node count reached: return at once (no chains) */
            }
            UV[0] = UV[2]; UV[1] = UV[3]; memcpy(N0, N1, sizeof N0); memcpy(PP, PP + 3, 3 * sizeof(double));
        }
    }
    if (pp->np > 0) {
        pl_boundary(f, s, V, ang, pp, metric, tol);
        pp->np0 = pp->np;
        if (pp->cancel) return 0;
        if (pl_chain(f, s, pp, tol, chord) < 0) return -1;
        pl_seams(s, pp);
    }
    return 0;
}

/* ---- the curves: FUN_180565bc0 -> msf_create_spline (0x18054db20), then FUN_180538d30 -> FUN_18054d250 ---- */

/* eval_poly_deriv(c, 3, 3, 1, 1, '\x01', t, out) (0x1805eb630) on one cubic segment (12 doubles [power][xyz]): out = the point, then the first derivative */
static void seg_pd(const double *c, double t, double P[3], double D[3])
{
    for (int k = 0; k < 3; k++) {
        double d1 = 0.0, d0 = c[9 + k];
        for (int j = 2; j >= 0; j--) { d1 = t * d1 + d0; d0 = t * d0 + c[3 * j + k]; }
        P[k] = d0; D[k] = d1;
    }
}
static void seg_p(const double *c, double t, double P[3]) { double D[3]; seg_pd(c, t, P, D); }   /* eval_poly_deriv(.., 1, 0, '\0', t): the point (the same synthetic division) */
static void unit_inplace(double v[3])   /* FUN_18000f2f0: v / |v|, 0 stays 0 */
{
    double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l == 0.0) { v[0] = v[1] = v[2] = 0; return; }
    v[0] /= l; v[1] /= l; v[2] /= l;
}
/* proj_pt_on_coeff (0x180018200, order 4): Newton from s = 0.5 on the segment, clamped to 0 .. 1 (twice at the same end: that end), 100 steps, stops when the
   distance < tol or |D . e| < 2e-7; returns the distance */
static double proj_coeff(const double p[3], const double *c, double tol)
{
    int b3 = 0, b2 = 0; double s = 0.5, dist = 0;
    for (int i = 0; ; ) {
        double q[3], D[3]; seg_p(c, s, q);
        double e0 = p[0] - q[0], e1 = p[1] - q[1], e2 = p[2] - q[2];
        dist = sqrt(e1 * e1 + e0 * e0 + e2 * e2);
        if (dist < tol) return dist;
        seg_pd(c, s, q, D);
        double g = D[1] * e1 + D[0] * e0 + D[2] * e2;
        if (fabs(g) < 2e-07) return dist;
        double l = sqrt(D[1] * D[1] + D[0] * D[0] + D[2] * D[2]);
        s = g / (l * l) + s;
        if (s <= 1.0) {
            if (s < 0.0) { b2 = 0; s = 0.0; if (b3) { seg_p(c, 0.0, q); return sqrt((p[1] - q[1]) * (p[1] - q[1]) + (p[0] - q[0]) * (p[0] - q[0]) + (p[2] - q[2]) * (p[2] - q[2])); } b3 = 1; }
        } else { b3 = 0; s = 1.0; if (b2) { seg_p(c, 1.0, q); return sqrt((p[1] - q[1]) * (p[1] - q[1]) + (p[0] - q[0]) * (p[0] - q[0]) + (p[2] - q[2]) * (p[2] - q[2])); } b2 = 1; }
        if (99 < ++i) return dist;
    }
}
/* remove_nodes(ent, 0, tol9, out) (0x180665940) on nseg cubic segments C (12 doubles each): node k + 1 goes when the Hermite cubic from the start of the run
   (segment k - run at 0, unit tangent) to the end of segment k + 1 (at 1) passes within tol9 of every removed node and of the middles of the segments either side.
   Returns the count removed; with some removed, *out (malloc) = the new segments and *nout their count. */
static int remove_nodes(const double *C, int nseg, double tol9, double tol, double **out, int *nout)
{
    int nn = nseg + 1, run = 0, cnt = 0;
    char *fl = calloc((size_t)nn, 1);
    if (!fl) return 0;
    for (int k = 0; k < nn - 2; k++) {
        double P0[3], T0[3], P1[3], T1[3], H[12];
        seg_pd(C + 12 * (k - run), 0.0, P0, T0); unit_inplace(T0);
        seg_pd(C + 12 * (k + 1), 1.0, P1, T1); unit_inplace(T1);
        double h = sqrt((P1[1] - P0[1]) * (P1[1] - P0[1]) + (P1[0] - P0[0]) * (P1[0] - P0[0]) + (P1[2] - P0[2]) * (P1[2] - P0[2]));
        mcsf_pt_tan_coefs(P0, P1, T0, T1, h, H);
        int ok = 0;
        for (int j = 0; j <= run; j++) {
            int sg = j - run + k; double q[3];
            ok = 0;
            seg_p(C + 12 * (sg + 1), 0.0, q); if (tol9 <= proj_coeff(q, H, tol)) break;
            seg_p(C + 12 * sg, 0.5, q); if (tol9 <= proj_coeff(q, H, tol)) break;
            seg_p(C + 12 * (sg + 1), 0.5, q); if (tol9 <= proj_coeff(q, H, tol)) break;
            ok = 1;
        }
        fl[k + 1] = (char)ok;
        if (ok) { run++; cnt++; } else run = 0;
    }
    if (cnt < 1) { free(fl); return 0; }
    double *O = malloc(sizeof(double) * 12 * (size_t)nseg); int no = 0;
    double P0[3], T0[3];
    seg_pd(C, 0.0, P0, T0); unit_inplace(T0);
    for (int s = 1; s < nn; s++) {
        double P1[3], T1[3]; seg_pd(C + 12 * (s - 1), 1.0, P1, T1);
        if (!fl[s]) {
            unit_inplace(T1);
            double h = sqrt((P1[1] - P0[1]) * (P1[1] - P0[1]) + (P1[0] - P0[0]) * (P1[0] - P0[0]) + (P1[2] - P0[2]) * (P1[2] - P0[2]));
            mcsf_pt_tan_coefs(P0, P1, T0, T1, h, O + 12 * no); no++;
            if (s < nn - 1) { seg_pd(C + 12 * s, 0.0, P0, T0); unit_inplace(T0); }
        }
    }
    free(fl);
    *out = O; *nout = no;
    return cnt;
}
/* check_colinear (0x1806665c0): b - a and c - b both longer than tol and their cosine >= 0.99985 with b within 2 tol of the line a c (dist_pt_l_3d), or either within tol */
static int check_colinear(const double a[3], const double b[3], const double c[3], double tol)
{
    double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { c[0] - b[0], c[1] - b[1], c[2] - b[2] };
    double lu = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]), lv = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (lu <= tol || lv <= tol) return 1;
    if (!(0.99985 <= (v[0] * u[0] + v[1] * u[1] + v[2] * u[2]) / (lv * lu))) return 0;
    double d9 = c[0] - a[0], d7 = c[1] - a[1], d8 = c[2] - a[2];   /* dist_pt_l_3d (0x180013bd0) of b to the line a c */
    double x = (b[1] - a[1]) * d8 - (b[2] - a[2]) * d7, y = (b[2] - a[2]) * d9 - d8 * (b[0] - a[0]), z = (b[0] - a[0]) * d7 - (b[1] - a[1]) * d9;
    double l = sqrt(d9 * d9 + d7 * d7 + d8 * d8), d;
    if (fabs(l) <= tol) d = sqrt((a[1] - b[1]) * (a[1] - b[1]) + (a[0] - b[0]) * (a[0] - b[0]) + (a[2] - b[2]) * (a[2] - b[2]));
    else d = fabs(sqrt(y * y + x * x + z * z) / l);
    return d < tol * 2.0;
}

/* msf_create_spline (0x18054db20) on the nodes Q[0..n-1] (main colour and level: DAT_1812f3978 = 1, never written): n < 2 nothing; n == 2 a line when the ends are
   tol apart (its ok flag stays 0: FUN_180565bc0 does not list it); chord mode (surf_use_chord_dev): FUN_18054d160 drops nodes within tol of the previous one in
   place, but the count used after is still n (18054decc: R13, not the updated local_5348); closed when the last node is within tol of the first:
   make_spline_coefs_closed, else make_spline_coefs_open with natural ends; remove_nodes at chord * 0.9; one segment left and its 0 / 0.5 / 1 points colinear:
   the line; else the spline (put_spline_in_db). Distance mode: make_spline_from_pts (natural ends, closed as above), no node removal. */
int plc_curve(const double *Qin, int n, int chord_mode, double chord, double tol, PlCurve *o)
{
    memset(o, 0, sizeof *o);
    if (n < 2) return 0;
    if (PL_MAXPTS < n) return 0;   /* post_warning(0x1812 ..): get_max_n_spline_pts */
    if (n == 2) {
        double l = sqrt((Qin[4] - Qin[1]) * (Qin[4] - Qin[1]) + (Qin[3] - Qin[0]) * (Qin[3] - Qin[0]) + (Qin[5] - Qin[2]) * (Qin[5] - Qin[2]));
        if (tol <= l) { o->kind = 2; memcpy(o->line, Qin, 6 * sizeof(double)); o->listed = 0; return 1; }
        return 0;
    }
    double (*Q)[3] = calloc((size_t)n + 4, sizeof *Q);
    double *C = calloc((size_t)n + 2, 12 * sizeof(double));
    int made = 0;
    memcpy(Q, Qin, sizeof(double) * 3 * (size_t)n);
    if (chord_mode) {
        /* FUN_18054d160 (18054d160): node i2 within tol of the kept node i6 - 1 is dropped by moving the nodes after it down; its count is local (not written back) */
        int m = n, i2 = 1, i6 = 1;
        if (1 < m) do {
            double d = sqrt((Q[i6 - 1][0] - Q[i2][0]) * (Q[i6 - 1][0] - Q[i2][0]) + (Q[i6 - 1][1] - Q[i2][1]) * (Q[i6 - 1][1] - Q[i2][1]) + (Q[i6 - 1][2] - Q[i2][2]) * (Q[i6 - 1][2] - Q[i2][2]));
            if (tol <= d) i6++;
            else { int k = (m - i2) - 1; if (0 < k) memmove(Q[i6], Q[i2 + 1], sizeof(double) * 3 * (size_t)k); m--; }
            i2++;
        } while (i2 < m);
    }
    double cl = sqrt((Q[n - 1][1] - Q[0][1]) * (Q[n - 1][1] - Q[0][1]) + (Q[n - 1][0] - Q[0][0]) * (Q[n - 1][0] - Q[0][0]) + (Q[n - 1][2] - Q[0][2]) * (Q[n - 1][2] - Q[0][2]));
    int closed = cl < tol, ok;
    static const double z3[3] = { 0, 0, 0 };
    if (closed) ok = mcsf_coefs_closed(C, (const double (*)[3])Q, n, tol);   /* make_spline_coefs_closed (0x1806633a0) */
    else ok = mcsf_coefs_open(C, (const double (*)[3])Q, n, 0, z3, 0, z3, tol);   /* make_spline_coefs_open (0x180664360), end codes 0 / 0 */
    if (ok) {
        const double *S = C; int ns = n - 1; double *R = NULL; int nr = 0;
        if (chord_mode && remove_nodes(C, n - 1, chord * 0.9, tol, &R, &nr) != 0) {   /* remove_nodes(ent, 0, surf_step_chord_dev * 0.9 ([0x180d06378]), ..) */
            S = R; ns = nr;
            if (nr == 1) {   /* eval_curve at 0, 1, 0.5 of the one segment, check_colinear: the line from 0 to 1 when tol long (18054e007 .. 18054e0e8) */
                double a[3], b[3], c[3]; seg_p(R, 0.0, a); seg_p(R, 1.0, b); seg_p(R, 0.5, c);
                if (check_colinear(a, c, b, tol)) {
                    double l = sqrt((b[1] - a[1]) * (b[1] - a[1]) + (b[0] - a[0]) * (b[0] - a[0]) + (b[2] - a[2]) * (b[2] - a[2]));
                    if (tol <= l) { o->kind = 2; memcpy(o->line, a, sizeof a); memcpy(o->line + 3, b, sizeof b); o->listed = 1; made = 1; }
                    free(R); free(Q); free(C); return made;
                }
            }
        }
        double p0[3], p1[3]; seg_p(S, 0.0, p0); seg_p(S + 12 * (ns - 1), 1.0, p1);
        o->rec = mcsf_spline_rec(S, ns, p0, p1, closed, tol, &o->rn);   /* put_spline_in_db (0x1802daee0) */
        if (o->rec) { o->kind = 8; o->listed = 1; made = 1; }
        free(R);
    }
    free(Q); free(C);
    return made;
}
void plc_free(PlCurve *o) { free(o->rec); o->rec = NULL; o->rn = 0; }

/* FUN_18054d250 (18054d250), as FUN_180538d30 (180538d30) calls it in chord mode for each listed curve: GetProdEnableBits & 0xf002 is taken clear (Mill; as
   formats/align_ops.md's & 2 gate) [inferred]; a record of class 8 / 0x80 only (ent byte & 0x88; a line stays); convert_ent_to_line_or_arc(ent, surf_step_chord_dev)
   -> the line or the arc (store_ent, main colour / level); else class 8 with msf_nurbs_splines -> convert_entity_to_nurbs + put_nurbs_in_db; the spline itself is
   killed (FUN_180551960) */
int plc_convert(PlCurve *o, double chord, double tol, int nurbs, const McsSimpView *views, int nviews)
{
    if (o->kind != 8 || !o->rec) return 0;
    CurveRec cr; memset(&cr, 0, sizeof cr); cr.cls = 8; cr.rec = o->rec; cr.n = o->rn;
    McsSimplify s; memset(&s, 0, sizeof s);
    if (mcs_simplify_curve(&cr, chord, tol, views, nviews, &s)) {
        o->simp = s; o->kind = s.is_line ? 2 : 4;
        if (s.is_line) memcpy(o->line, s.line, sizeof o->line);
        free(o->rec); o->rec = NULL; o->rn = 0;
        return 1;
    }
    if (nurbs) {
        uint32_t rn = 0; uint8_t *r = mcsf_spline_to_nurbs(o->rec, o->rn, tol, &rn);
        if (r) { free(o->rec); o->rec = r; o->rn = rn; o->kind = 0x80; return 1; }
    }
    return 0;
}

/* ================================================================================================ build_edgecrv_on_surf (mccore 0x180273970), untrimmed branch
   Chaining's surface-edge pick (formats/chaining.md "Surfaces in the Chaining dialog"): chain_single FUN_1801df0b0 -> FUN_1801e1a50 ->
   build_edgecrv_on_surf(surf, (u, v), surf_step_chord_dev, chain_section_stop_angle in degrees, 0, list) with edgecrv_make_spline = 1,
   edgecrv_make_cons = 0 (the values every caller restores, decomp/curves/ed_cad1.c l.1016-1017). Dumps (2026-11-17): DumpDecomp of
   build_edgecrv_on_surf (depth 2: make_constant_spline 0x1800e33c0, calc_chord_ht 0x1805eca80, curv 0x1804b7990, pt_tan_to_sp_coefs
   0x180665510, FUN_1800e1b90) and FUN_1800e4680 / FUN_18054d250 / remove_nodes; the disassembly of make_constant_spline 0x1800e33c0..0x1800e4100 for
   the stack arguments of FUN_1800e4680 (1800e3fa9..1800e3fd3) and curv's second argument (1800e3c12..1800e3c27): decomp/chain_srfedge*.{c,asm}.
   The trimmed branch (surf kind 10: the nearest piece of the stored trim loops, check_edge / get_constant_uvpts / FUN_180274d90 / FUN_180275800,
   the section stop angle): ported below (ec_build_edgecrv_list). */

/* calc_chord_ht (0x1805eca80): the chord's angles to the two tangents, (|tan a1| + |tan a0|) * chord / 4 */
static double ec_chord_ht(const double P0[3], const double T0[3], const double P1[3], const double T1[3])
{
    double d0 = P1[0] - P0[0], d1 = P1[1] - P0[1], d2 = P1[2] - P0[2], L = sqrt(d0 * d0 + d1 * d1 + d2 * d2);
    double l0 = sqrt(T0[0] * T0[0] + T0[1] * T0[1] + T0[2] * T0[2]), c0 = (T0[0] * d0 + d1 * T0[1] + d2 * T0[2]) / (l0 * L);
    if (c0 <= 1.0) { if (c0 < -1.0) c0 = -1.0; } else c0 = 1.0;
    double a0 = acos(c0);
    double l1 = sqrt(T1[0] * T1[0] + T1[1] * T1[1] + T1[2] * T1[2]), c1 = (d1 * T1[1] + T1[0] * d0 + d2 * T1[2]) / (l1 * L);
    if (c1 <= 1.0) { if (c1 < -1.0) c1 = -1.0; } else c1 = 1.0;
    double a1 = acos(c1);
    return (fabs(tan(a1)) + fabs(tan(a0))) * L * 0.25;
}
/* curv (0x1804b7990): |d1 x d2| / |d1|^3 */
static double ec_curv(const double a[3], const double b[3])
{
    double X = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
    double x = b[1] * a[2] - b[2] * a[1], y = b[2] * a[0] - b[0] * a[2], z = b[0] * a[1] - b[1] * a[0];
    return sqrt(y * y + x * x + z * z) / (sqrt(X) * X);
}
/* FUN_1800e1b90: the last segment re-fitted to end at P (its start point and start derivative kept, its end derivative c1 + 2 c2 + 3 c3 kept) */
static void ec_seg_end(double *c, const double P[3])
{
    for (int k = 0; k < 3; k++) {
        double c0 = c[k], c1 = c[3 + k], te = c[6 + k] * 2.0 + c1 + c[9 + k] * 3.0;
        c[6 + k] = ((P[k] * 3.0 - c0 * 3.0) - c1 * 2.0) - te;
        c[9 + k] = (c0 * 2.0 - P[k] * 2.0) + c1 + te;
    }
}
static int ec_eval2(const McamFile *f, const Surf *s, double u, double v, double D[][3])   /* eval_surf(surf, 2, u, v): kind 0 bit-exact (plm_eval), others mcsn_eval */
{
    return s->kind == 0 ? plm_eval(f, s, 2, u, v, D) : mcsn_eval(f, s, 2, u, v, D);
}
/* make_constant_spline (0x1800e33c0) as build_edgecrv_on_surf calls it: param_1 0, param_2 msf_nurbs_splines (nurbs), param_3 1, param_5 2 (chord
   height), param_7 chord = surf_step_chord_dev, param_8 1 (edgecrv_make_spline; no uv list), param_9 0 (from param_12[0] to [1]), param_10 = along_u
   (1: v = cval, u runs), param_11 cval, param_12 {t0, t1}. eval_surf of order 2; the running derivative D[2 - along_u] (Su / Sv), its unit vector the
   tangent; curv's second argument D[4 - along_u] = Suu when u runs, **Suv** when v runs (1800e3c12: RBP + 0x2a60 + (4 - param_10) * 24 - as the DLL
   does). Every step: calc_chord_ht over chord * 1.05 halves the step and retries; a point within tol of the previous one re-fits the last segment
   (FUN_1800e1b90), else pt_tan_to_sp_coefs adds a segment; the next step from the curvature k (k >= 0.001: k <= 2 / chord -> 2 sqrt((2 / k - chord)
   chord) / |d1|, else 1 / (k |d1|); k < 0.001: (t1 - t0) / 10); a step over half the range is halved while the parameter is below the middle and the
   step reaches t1 - 1e-7; a running derivative shorter than tol ends it. End: no segment -> store_point of the first point (kind 1); else
   FUN_1800e4680(spline, nseg, closed = |last - first| < tol, 0, nurbs, 1, chord * 0.1): put_spline_in_db, remove_nodes at chord * 0.1 (the reduced
   spline replaces it), with nurbs convert_entity_to_nurbs. ESC (KeyPress 0x1b) is not polled here. Returns 1 made, 0 none / not evaluable. */
static int ec_const_spline(const McamFile *f, const Surf *s, int along_u, double cval, double t0, double t1, double chord, double tol, int nurbs, PlCurve *o)
{
    double uv[2]; int ri = along_u ? 0 : 1;
    uv[ri ^ 1] = cval; uv[ri] = t0;
    double mid = (t1 + t0) * 0.5, step = 0.0, tprev = t0;
    double P0[3] = { 0, 0, 0 }, Pl[3] = { 0, 0, 0 }, Pp[3] = { 0, 0, 0 }, Tp[3] = { 0, 0, 0 };
    int first = 1, atend = 0, nseg = 0, cap = 64;
    double *C = malloc(sizeof(double) * 12 * (size_t)cap);
    if (!C) return 0;
    for (int it = 0; it < 1000000; it++) {   /* (ours: an iteration cap) */
        double D[10][3]; memset(D, 0, sizeof D);
        if (ec_eval2(f, s, uv[0], uv[1], D)) { free(C); return 0; }
        const double *S = D[0], *d1 = D[2 - along_u];
        double L = sqrt(d1[1] * d1[1] + d1[0] * d1[0] + d1[2] * d1[2]), T[3] = { 0, 0, 0 };
        if (L != 0.0) { T[0] = d1[0] / L; T[1] = d1[1] / L; T[2] = d1[2] / L; }
        if (first) { memcpy(P0, S, sizeof P0); first = 0; }
        else {
            double ch = ec_chord_ht(Pp, Tp, S, T);
            if (chord * 1.05 < ch) { step *= 0.5; uv[ri] = tprev + step; atend = 0; continue; }
            double d = dist3(Pp, S);
            if (d <= tol) { if (nseg > 0) ec_seg_end(C + 12 * (nseg - 1), S); }
            else {
                if (nseg == cap) { cap *= 2; double *nc = realloc(C, sizeof(double) * 12 * (size_t)cap); if (!nc) { free(C); return 0; } C = nc; }
                mcsf_pt_tan_coefs(Pp, S, Tp, T, d, C + 12 * nseg); nseg++;
            }
        }
        int done = 0;
        tprev = uv[ri];
        if (atend) { memcpy(Pl, S, sizeof Pl); done = 1; }
        else {
            if (tol <= fabs(L)) {
                double k = ec_curv(d1, D[4 - along_u]);
                if (0.001 <= k) { if (k <= 2.0 / chord) step = sqrt((2.0 / k - chord) * chord) * 2.0 / L; else step = 1.0 / (k * L); }
                else step = (t1 - t0) / 10.0;
            } else { if (nseg == 0) memcpy(Pl, P0, sizeof Pl); done = 1; }
            if (fabs(t1 - t0) * 0.5 < fabs(step))
                for (int h = 0; h < 2000 && !(mid <= uv[ri]) && !(uv[ri] + step < t1 - 1e-07); h++) step *= 0.5;
            uv[ri] += step;
            if (t1 - 1e-07 <= uv[ri]) { atend = 1; uv[ri] = t1; step = t1 - tprev; }
        }
        memcpy(Pp, S, sizeof Pp); memcpy(Tp, T, sizeof Tp);
        if (done) break;
    }
    memset(o, 0, sizeof *o);
    if (nseg == 0) { o->kind = 1; memcpy(o->line, P0, sizeof P0); memcpy(o->line + 3, P0, sizeof P0); free(C); return 1; }   /* store_point (main attributes) */
    int closed = dist3(Pl, P0) < tol;
    const double *Sg = C; int ns = nseg; double *R = NULL; int nr = 0;
    if (remove_nodes(C, nseg, chord * 0.1, tol, &R, &nr) != 0) { Sg = R; ns = nr; }   /* FUN_1800e4680 param_6 / param_7 */
    double p0[3], p1[3]; seg_p(Sg, 0.0, p0); seg_p(Sg + 12 * (ns - 1), 1.0, p1);
    o->rec = mcsf_spline_rec(Sg, ns, p0, p1, closed, tol, &o->rn);   /* put_spline_in_db (0x1802daee0) */
    free(R); free(C);
    if (!o->rec) return 0;
    o->kind = 8; o->listed = 1;
    if (nurbs) {   /* FUN_1800e4680 param_5: convert_entity_to_nurbs + put_nurbs_in_db */
        uint32_t rn = 0; uint8_t *r = mcsf_spline_to_nurbs(o->rec, o->rn, tol, &rn);
        if (r) { free(o->rec); o->rec = r; o->rn = rn; o->kind = 0x80; }
    }
    return 1;
}
/* FUN_18054d250 (18054d250) as build_edgecrv_on_surf's curves get it: convert_ent_to_line_or_arc(surf_step_chord_dev) on a class 8 / 0x80 curve -> the
   line / arc (GetProdEnableBits & 0xf002 taken clear); with nurbs (msf_nurbs_splines) a class 8 spline that stays -> NURBS (done by the callers) */
int ec_no_simplify;   /* tests (tools/tests/ecv_test.c): the curves before FUN_18054d250's line / arc test (the oracle replaces it by a counter) */
static void ec_simplify(PlCurve *o, double chord, double tol, const McsSimpView *views, int nviews)
{
    if (ec_no_simplify || !(o->kind == 8 || o->kind == 0x80) || !o->rec) return;
    CurveRec cr; memset(&cr, 0, sizeof cr); cr.cls = o->kind; cr.rec = o->rec; cr.n = o->rn;
    McsSimplify sm; memset(&sm, 0, sizeof sm);
    if (mcs_simplify_curve(&cr, chord, tol, views, nviews, &sm)) {
        o->simp = sm; o->kind = sm.is_line ? 2 : 4;
        if (sm.is_line) memcpy(o->line, sm.line, sizeof o->line);
        free(o->rec); o->rec = NULL; o->rn = 0;
    }
}
/* build_edgecrv_on_surf, untrimmed branch (180273970 .. LAB_180273bf9 and the make_constant_spline loop): the boundary nearest (u, v) in the
   parameter plane, as signed differences (no abs) tested in the order V0 (v - v0), U1 (u1 - u), V1 (v1 - v), U0 (u - u0), the first of equal ones;
   V0 / V1: v constant, u from u0 to u1; U0 / U1: u constant, v from v0 to v1 (ent +0x70 u0, +0x78 u1, +0x80 v0, +0x88 v1). Then (param_5 0 and
   edgecrv_make_spline) FUN_18054d250(the curve, msf_nurbs_splines): convert_ent_to_line_or_arc(surf_step_chord_dev) -> the line / arc, else a class 8
   spline with nurbs -> NURBS (here already done by FUN_1800e4680; a NURBS record is not class 8: it stays). GetProdEnableBits & 2 (and & 0xf002 in
   FUN_18054d250) taken clear. Returns 1 with the curve in o (kind 1 point, 2 line, 4 arc (o->simp), 8 spline, 0x80 NURBS), 0 none, -1 not ported. */
static int ec_untrimmed(const McamFile *f, const Surf *s, double u, double v, double chord, double tol, int nurbs, const McsSimpView *views, int nviews, PlCurve *o, const char **why)
{
    memset(o, 0, sizeof *o);
    if (s->n < 0x90) { if (why) *why = "bad surface record"; return -1; }
    double u0, u1, v0, v1; memcpy(&u0, s->rec + 0x70, 8); memcpy(&u1, s->rec + 0x78, 8); memcpy(&v0, s->rec + 0x80, 8); memcpy(&v1, s->rec + 0x88, 8);
    { double D[10][3]; if (ec_eval2(f, s, u0, v0, D)) { if (why) *why = "eval_surf of order 2 is NOT PORTED for this surface kind"; return -1; } }
    int b = 1; double best = v - v0;
    if (u1 - u < best) { b = 2; best = u1 - u; }
    if (v1 - v < best) { b = 4; best = v1 - v; }
    if (u - u0 < best) { b = 8; best = u - u0; }
    int along_u = b == 1 || b == 4;
    double cval = b == 1 ? v0 : b == 2 ? u1 : b == 4 ? v1 : u0;
    if (!ec_const_spline(f, s, along_u, cval, along_u ? u0 : v0, along_u ? u1 : v1, chord, tol, nurbs, o)) return 0;
    ec_simplify(o, chord, tol, views, nviews);   /* FUN_18054d250 */
    return 1;
}

/* ================================================================================================ build_edgecrv_on_surf, trimmed branch (kind 10)
   mccore 0x180273d31 .. 0x180274d19, ported from the disassembly (decomp/chain_srfedge_ecv.asm) checked against the decompile (decomp/chain_srfedge.c
   l.180-855: Ghidra merges variables there - local_54f4 is the loop index in the search and the walk direction after, iVar33 the closing index and
   the boundary direction, local_5428 the list and then the point pointer; the walk's registers: [rbp-0x80] the node count, [rbp-0x78] the point,
   [rbp-0x48] the previous point, [rbp+0x80] the one before (check_colinear 2D), [rbp-0x18] the start, r12w the previous point's check_edge bits,
   [rbp-0x38] the current ones, [rbp-0x34] the start's). Callees: check_edge 0x18047b620, get_constant_uvpts 0x180274fa0, FUN_180274d90,
   FUN_180275800 -> msf_create_spline 0x18054db20 / store_linear_nurb_from_pts 0x1802636c0 (make_linear_nurb_from_pts 0x180262400), check_colinear
   0x1806665c0 (3D) / 0x180666430 (2D), dist_to_finite_line 0x1804b6680, t_on_line 0x1804b48a0, reverse_real_array 0x1804b87f0 (decomp/
   chain_srfedge.c, chain_srfedge_trim.c; the asm of get_constant_uvpts / FUN_180274d90 in chain_srfedge_ecv.asm). Globals as chaining has them:
   edgecrv_make_spline 1, edgecrv_make_cons 0 (no uv nodes, FUN_18054d4f0 not called), param_5 0, KeyPress(ESC) false, surf_use_chord_dev 1
   (msf_create_spline's chord mode: the mccore static), CSplineNodeMgr::get_max_n_spline_pts 4000, check_edge's surf_step_chord_dev = the chord
   the caller passes (chain_single passes that global itself). Records: trimmed +0x100 the base, +0x10c the uv point count, +0x128 (short) the
   loop count, the points (16 bytes) at +0x138, after them per loop (start, count): count + 2 points, the last a copy of the first. */
typedef struct {
    const McamFile *f; const Surf *b;
    double tol, chord, ang;                 /* ang = param_4 * 0.01745329251994329 (local_5478) */
    int nurbs; const McsSimpView *views; int nviews;
    double (*N)[3]; int cnt;                /* CSplineNodeMgr spline nodes 1 and local_54f8 */
    PlCurve *out; int nout, maxout;
    const char *why; int fail;
} EcW;
#define EC_MAXN PL_MAXPTS
static void ec_ev(EcW *w, int order, double u, double v, double D[][3])   /* eval_surf(base, order, u, v) (kind 0 bit-exact: plm_eval) */
{
    memset(D, 0, sizeof(double) * 30);
    int r = w->b->kind == 0 ? plm_eval(w->f, w->b, order, u, v, D) : mcsn_eval(w->f, w->b, order, u, v, D);
    if (r && !w->fail) { w->fail = 1; w->why = "eval_surf is NOT PORTED for this base surface kind"; }
}
/* eval_surf_extrap(base, 0, u, v, ..) (0x18047bf10; as viewer/mccsurf.c): outside a non-periodic direction the boundary point plus the boundary
   derivative times the excess (order 1 then); periodic (+6 bits 0 / 1) wraps; the 0.01 snapping after only changes the in-range flags */
static void ec_extrap0(EcW *w, double u, double v, double out[3])
{
    const uint8_t *r = w->b->rec; int fl = r[0x6e];
    double u0, u1, v0, v1; memcpy(&u0, r + 0x70, 8); memcpy(&u1, r + 0x78, 8); memcpy(&v0, r + 0x80, 8); memcpy(&v1, r + 0x88, 8);
    int inu = 1, inv = 1; double du = 0.0, dv = 0.0;
    if (u < u0) { if (!(fl & 1)) { inu = 0; du = u - u0; u = u0; } else { do { u += u1 - u0; du = 0.0; } while (u < u0); } }
    if (u1 < u) { if (!(fl & 1)) { inu = 0; du = u - u1; u = u1; } else { do u -= u1 - u0; while (u1 < u); } }
    if (v < v0) { if (!(fl & 2)) { inv = 0; dv = v - v0; v = v0; } else { do { v += v1 - v0; dv = 0.0; } while (v < v0); } }
    if (v1 < v) { if (!(fl & 2)) { inv = 0; dv = v - v1; v = v1; } else { do v -= v1 - v0; while (v1 < v); } }
    double D[10][3]; ec_ev(w, (!inu || !inv) ? 1 : 0, u, v, D);
    out[0] = D[0][0]; out[1] = D[0][1]; out[2] = D[0][2];
    if (!inu) for (int q = 0; q < 3; q++) out[q] += D[1][q] * du;
    if (!inv) for (int q = 0; q < 3; q++) out[q] += D[2][q] * dv;
}
/* check_edge (0x18047b620): (u, v) within 0.01 of u0 / u1 / v0 / v1 and the boundary point within surf_step_chord_dev of P -> the point and the
   parameter snapped to it; bits 8 u0, 2 u1, 1 v0, 4 v1 (u tested first: u0, else u1; then v0, else v1) */
static unsigned ec_check_edge(EcW *w, double uv[2], double P[3])
{
    const uint8_t *r = w->b->rec; unsigned bits = 0; double E[3];
    double u0, u1, v0, v1; memcpy(&u0, r + 0x70, 8); memcpy(&u1, r + 0x78, 8); memcpy(&v0, r + 0x80, 8); memcpy(&v1, r + 0x88, 8);
    double d = uv[0] - u0;
    if (0.01 < fabs(d) || fabs(d) == 0.01) {
        if (fabs(uv[0] - u1) < 0.01) { ec_extrap0(w, u1, uv[1], E); if (dist3(P, E) < w->chord) { bits = 2; memcpy(P, E, sizeof E); uv[0] = u1; } }
    } else { ec_extrap0(w, u0, uv[1], E); if (dist3(P, E) < w->chord) { bits = 8; memcpy(P, E, sizeof E); uv[0] = u0; } }
    d = uv[1] - v0;
    if (0.01 < fabs(d) || fabs(d) == 0.01) {
        if (fabs(uv[1] - v1) < 0.01) { ec_extrap0(w, uv[0], v1, E); if (dist3(P, E) < w->chord) { bits |= 4; memcpy(P, E, sizeof E); uv[1] = v1; } }
    } else { ec_extrap0(w, uv[0], v0, E); if (dist3(P, E) < w->chord) { bits |= 1; memcpy(P, E, sizeof E); uv[1] = v0; } }
    return bits;
}
/* FUN_180274d90 (asm 180274d90..180274f8f): n < 2 true; else the angle between the unit vectors N[n-1] - N[n-2] and P - N[n-1] (a zero vector
   stays 0; lengths sqrt((y y + x x) + z z), dot (bx ax + by ay) + bz az, acos) at most ang */
static int ec_angle_ok(const double P[3], double ang, int n, double (*N)[3])
{
    if (n < 2) return 1;
    const double *a = N[n - 2], *b = N[n - 1];
    double ax = b[0] - a[0], ay = b[1] - a[1], az = b[2] - a[2], l = sqrt((ay * ay + ax * ax) + az * az);
    if (l == 0.0) { ax = ay = az = 0.0; } else { ax /= l; ay /= l; az /= l; }
    double bx = P[0] - b[0], by = P[1] - b[1], bz = P[2] - b[2], m = sqrt((by * by + bx * bx) + bz * bz);
    if (m == 0.0) { bx = by = bz = 0.0; } else { bx /= m; by /= m; bz /= m; }
    double c = acos((bx * ax + by * ay) + bz * az);
    return fabs(c) <= ang;
}
/* check_colinear (0x180666430, 2D): b - a and c - b, either at most 1e-8 long -> 1; cosine >= 0.99985 and b within 1e-8 of the line a c (dist_pt_l
   0x1804b32a0, tol for the degenerate line) */
static int ec_colin2(const double a[2], const double b[2], const double c[2], double tol)
{
    double x1 = b[0] - a[0], y1 = b[1] - a[1], x2 = c[0] - b[0], y2 = c[1] - b[1];
    double l1 = sqrt(x1 * x1 + y1 * y1), l2 = sqrt(x2 * x2 + y2 * y2);
    if (l1 <= 1e-08 || l2 <= 1e-08) return 1;
    if (!(0.99985 <= (x2 * x1 + y2 * y1) / (l2 * l1))) return 0;
    double px = b[0] - a[0], py = b[1] - a[1], dy = c[1] - a[1], dx = c[0] - a[0], L = sqrt(dy * dy + dx * dx), d;
    if (L <= tol) d = sqrt(py * py + px * px);
    else { d = (px * dy - py * dx) / L; if (d < 0.0) d = -d; }
    return d < 1e-08;
}
/* FUN_180275800(p1, n, nodes, param_5 = 0, &eptr, &ok) (180275800): n < 2 nothing; p1 0 and n != 2: store_linear_nurb_from_pts (make_linear_nurb_from_pts:
   order 2, knots 0, 0, 1 .. n - 1, n - 1, the nodes as control points, closed bit when the ends are within tol; put_nurbs_in_db) then FUN_18054d250(ent, 0);
   else msf_create_spline (here plc_curve, chord mode = surf_use_chord_dev 1; its 2-node line ignores FUN_1805502a0's plane snaps, as plc_curve), and with
   msf_nurbs_splines FUN_18054d250(ent, 1) (plc_convert). Each stored curve goes into the list (FUN_180029800) */
static void ec_store(EcW *w, int p1, int n)
{
    if (n < 2 || w->fail) return;
    if (w->nout >= w->maxout) { w->fail = 1; w->why = "too many edge curves"; return; }
    PlCurve *o = &w->out[w->nout]; memset(o, 0, sizeof *o);
    if (!p1 && n != 2) {
        double *K = malloc(sizeof(double) * ((size_t)n + 2));
        if (!K) { w->fail = 1; w->why = "out of memory"; return; }
        K[0] = 0.0; for (int i = 0; i < n; i++) K[1 + i] = (double)i; K[n + 1] = (double)(n - 1);
        int closed = dist3(w->N[n - 1], w->N[0]) < w->tol;
        McsfNb nb = { n, n + 2, 1, 3, 0, closed, K, &w->N[0][0] };
        o->rec = mcsf_nurbs_rec(&nb, w->tol, &o->rn); free(K);
        if (!o->rec) { w->fail = 1; w->why = "out of memory"; return; }
        o->kind = 0x80; o->listed = 1; w->nout++;
        ec_simplify(o, w->chord, w->tol, w->views, w->nviews);   /* FUN_18054d250(ent, 0, ..) */
        return;
    }
    if (!plc_curve(&w->N[0][0], n, 1, w->chord, w->tol, o)) return;
    w->nout++;
    if (w->nurbs && !ec_no_simplify) plc_convert(o, w->chord, w->tol, 1, w->views, w->nviews);   /* FUN_18054d250(ent, msf_nurbs_splines) */
}
/* get_constant_uvpts (0x180274fa0; asm 180274fa0..1802757fb) with param_1 (make_cons) 0, param_9 = 4000, ESC never: the iso line along the base's
   boundary, along = param_5 (1: v = cval, u runs; 0: u = cval, v runs), from rng[0] to rng[1] (reverse: rng[1] to rng[0], the tangent negated);
   eval_surf order 2, the running derivative D[2 - along] (its length (x x + y y) + z z), curv's second argument D[4 - along] (Suu when u runs,
   Suv when v runs, as make_constant_spline); the first point appended unless within tol of the last node (then the next is first again), angle
   checked from the third node; later points: calc_chord_ht over chord x 1.05 halves the step from the last accepted parameter; within tol of the
   previous point or colinear (check_colinear 3D) replaces the last node, else FUN_180274d90 then appended (at 4000: *outp = this parameter, return);
   the step as make_constant_spline (k < 0.001: (rng[1] - rng[0]) / 10), no halving to the middle; a derivative below tol ends at the far end
   (the end point replaces the last node within tol, else appended). Returns with *ended 1 at the end, 0 when stopped (*outp = the parameter to
   restart from: the last accepted one on an angle failure). */
static void ec_gcu(EcW *w, int reverse, int along, double cval, const double rng[2], double *outp, int *ended)
{
    double uv[2], step = 0.0, cur, end, pP[3] = { 0, 0, 0 }, pT[3] = { 0, 0, 0 };
    int ri = along != 0 ? 0 : 1, first = 1, atend = 0;
    *ended = 0;
    uv[along != 1 ? 0 : 1] = cval;
    if (!reverse) { cur = rng[0]; end = rng[1]; } else { cur = rng[1]; end = rng[0]; }
    uv[ri] = cur;
    for (int it = 0; it < 10000000; it++) {   /* (ours: an iteration cap) */
        double D[10][3]; ec_ev(w, 2, uv[0], uv[1], D);
        if (w->fail) return;
        const double *d1 = D[2 - along];
        double L = sqrt((d1[0] * d1[0] + d1[1] * d1[1]) + d1[2] * d1[2]), T[3] = { 0, 0, 0 }, P[3] = { D[0][0], D[0][1], D[0][2] };
        if (L != 0.0) { T[0] = d1[0] / L; T[1] = d1[1] / L; T[2] = d1[2] / L; }
        if (reverse) { T[0] = T[0] * -1.0; T[1] = T[1] * -1.0; T[2] = T[2] * -1.0; }
        int done = 0;
        if (first) {
            int n = w->cnt;
            if (!(n != 0 && dist3(w->N[n - 1], P) <= w->tol)) {
                if (n > 1 && !ec_angle_ok(P, w->ang, n, w->N)) { *outp = cur; return; }
                memcpy(w->N[n], P, sizeof P); w->cnt++; first = 0;
            }
        } else {
            double ch = ec_chord_ht(pP, pT, P, T);
            if (w->chord * 1.05 < ch) { step = step * 0.5; uv[ri] = cur + step; atend = 0; continue; }
            int n = w->cnt;
            if (dist3(pP, P) <= w->tol || (1 < n && check_colinear(w->N[n - 2], w->N[n - 1], P, w->tol))) memcpy(w->N[n - 1], P, sizeof P);
            else {
                if (!ec_angle_ok(P, w->ang, n, w->N)) { *outp = cur; return; }
                memcpy(w->N[n], P, sizeof P); w->cnt++;
                if (EC_MAXN <= w->cnt) { *outp = uv[ri]; return; }
            }
        }
        cur = uv[ri];                                     /* 18027535b */
        if (atend) done = 1;
        else {
            double L2 = sqrt((d1[0] * d1[0] + d1[1] * d1[1]) + d1[2] * d1[2]);
            if (w->tol <= fabs(L2)) {
                double k = ec_curv(d1, D[4 - along]);
                if (0.001 <= k) { if (k <= 2.0 / w->chord) step = sqrt((2.0 / k - w->chord) * w->chord) * 2.0 / L2; else step = 1.0 / (k * L2); }
                else step = (rng[1] - rng[0]) / 10.0;
            } else {                                      /* 1802754ca: the far end */
                memcpy(pP, P, sizeof P); done = 1; uv[ri] = end;
                double E[10][3]; ec_ev(w, 2, uv[0], uv[1], E);
                if (w->fail) return;
                P[0] = E[0][0]; P[1] = E[0][1]; P[2] = E[0][2];
                int n = w->cnt;
                if (dist3(P, pP) <= w->tol) memcpy(w->N[n - 1], P, sizeof P); else { memcpy(w->N[n], P, sizeof P); w->cnt++; }
            }
            if (reverse) step = step * -1.0;
            uv[ri] = step + uv[ri];
            if (!reverse) { if (end - 1e-07 <= uv[ri]) { atend = 1; uv[ri] = end; } }
            else if (uv[ri] <= end + 1e-07) { atend = 1; uv[ri] = end; }
        }
        memcpy(pP, P, sizeof P); memcpy(pT, T, sizeof T);
        if (done) { *ended = 1; return; }
    }
    w->fail = 1; w->why = "get_constant_uvpts did not end";
}
/* dist_to_finite_line (0x1804b6680) / t_on_line (0x1804b48a0) with the system tol */
static int ec_dfl(const double p[2], const double a[2], const double b[2], double maxd, double tol, double *d)
{
    double dx = b[0] - a[0], dy = b[1] - a[1], L = sqrt(dx * dx + dy * dy), q;
    if (tol <= L) {
        double e = fabs((p[0] - a[0]) * dy - (p[1] - a[1]) * dx) / L;
        *d = e;
        if (maxd <= e) return 0;
        double t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L;
        if (0.0 <= t) {
            if (t <= L) return 1;
            if (L + maxd <= t) return 0;
            q = (b[1] - p[1]) * (b[1] - p[1]) + (b[0] - p[0]) * (b[0] - p[0]);
        } else {
            if (t <= -maxd) return 0;
            q = (a[1] - p[1]) * (a[1] - p[1]) + (a[0] - p[0]) * (a[0] - p[0]);
        }
    } else q = (a[1] - p[1]) * (a[1] - p[1]) + (a[0] - p[0]) * (a[0] - p[0]);
    *d = sqrt(q);
    return *d < maxd;
}
static double ec_tline(const double p[2], const double a[2], const double b[2], double tol)
{
    double dx = b[0] - a[0], dy = b[1] - a[1], L2 = dy * dy + dx * dx;
    if (tol * tol < L2) return (dy * (p[1] - a[1]) + dx * (p[0] - a[0])) / L2;
    return 0.0;
}
static int ec_trimmed(EcW *w, const Surf *s, double pu, double pv)
{
    const uint8_t *r = s->rec;
    if (s->n < 0x138) { w->fail = 1; w->why = "bad trimmed surface record"; return -1; }
    int64_t be; memcpy(&be, r + 0x100, 8);
    const Surf *b = mcs_surf(w->f, be);
    if (!b || b == s || b->n < 0x90) return 0;                          /* get_ent_from_eptr fails: nothing */
    w->b = b;
    int16_t nl16; memcpy(&nl16, r + 0x128, 2); int nl = nl16; int32_t m; memcpy(&m, r + 0x10c, 4);
    if (m < 1 || nl < 0 || 0x138 + 16 * (size_t)m + 8 * (size_t)nl > s->n) { w->fail = 1; w->why = "a trimmed surface whose loop table cannot be read"; return -1; }
    const double *pts = (const double *)(r + 0x138); const int32_t *info = (const int32_t *)(r + 0x138 + 16 * (size_t)m);
    /* the search over the loops' pieces (180273d55 .. 18027401a): a running point pointer that moves count + 2 per loop; piece j of a loop runs from its
       point j - 1 to j, the last (j = count + 1) back to the loop's first point; t from t_on_line within 1e-4 of an end takes that end (and its own
       distance), the point written even when that distance then loses; the search ends at a distance below tol */
    double pk[2] = { pu, pv }, best = BIG, pt[2] = { pts[0], pts[1] };
    int piece = 1, loop = 0;
    {   long p = 0;
        for (int l = 0; l < nl; l++) {
            int cl = info[2 * l + 1], lim = cl + 2;
            if (p < 0 || cl < 0 || p + cl >= m) { w->fail = 1; w->why = "a trimmed surface whose loop table cannot be read"; return -1; }
            double ls[2] = { pts[2 * p], pts[2 * p + 1] }, s0[2], e0[2] = { 0, 0 };
            for (int j = 1; j < lim; j++) {
                p++;
                if (j == 1) { s0[0] = ls[0]; s0[1] = ls[1]; } else { s0[0] = e0[0]; s0[1] = e0[1]; }
                if (j == cl + 1) { e0[0] = ls[0]; e0[1] = ls[1]; } else { e0[0] = pts[2 * p]; e0[1] = pts[2 * p + 1]; }
                double d = 0; int fl = ec_dfl(pk, s0, e0, best, w->tol, &d);
                if (fl) {
                    double t = ec_tline(pk, s0, e0, w->tol);
                    if (t <= 0.0001) { pt[0] = s0[0]; pt[1] = s0[1]; d = sqrt((s0[0] - pk[0]) * (s0[0] - pk[0]) + (s0[1] - pk[1]) * (s0[1] - pk[1])); fl = d < best; }
                    else if (t >= 0.9999) { pt[0] = e0[0]; pt[1] = e0[1]; d = sqrt((e0[0] - pk[0]) * (e0[0] - pk[0]) + (e0[1] - pk[1]) * (e0[1] - pk[1])); fl = d < best; }
                    else { pt[0] = s0[0] * (1.0 - t) + e0[0] * t; pt[1] = s0[1] * (1.0 - t) + e0[1] * t; }
                    if (fl) { best = d; piece = j; loop = l; }
                }
                if (w->tol > fabs(best)) break;
            }
            p++;
            if (w->tol > fabs(best)) break;
        }
    }
    double P[3], start[2], prev[2], prev2[2] = { 0, 0 };
    { double D[10][3]; ec_ev(w, 0, pt[0], pt[1], D); memcpy(P, D[0], sizeof P); }
    if (w->fail) return -1;
    unsigned startbits = ec_check_edge(w, pt, P), curbits = 0xffffffffu, prevbits;
    if (w->fail) return -1;
    memcpy(w->N[0], P, sizeof P); w->cnt = 1;
    prev[0] = start[0] = pt[0]; prev[1] = start[1] = pt[1];
    long bs = loop > 0 ? info[2 * loop] : 0; int closeidx = info[2 * loop + 1] + 1;
    if (bs < 0 || bs + closeidx > m) { w->fail = 1; w->why = "a trimmed surface whose loop table cannot be read"; return -1; }
    const double *base = pts + 2 * bs;
    /* the walk: backwards (direction -1) from the piece's first point down to the loop's point 0, the nodes reversed, then forwards (+1) from the
       piece's end up to the closing index (count + 1 = the loop's first point) */
    for (int dir = -1; dir <= 1; dir += 2) {              /* LAB_180274150 */
        prevbits = startbits;
        int idx = piece; if (dir == -1) idx--;
        for (;;) {
            if (w->fail) return -1;
            const double *q = idx == closeidx ? base : base + 2 * idx;
            pt[0] = q[0]; pt[1] = q[1];
            if (!(0 < w->cnt && 1e-10 > sqrt((prev[0] - pt[0]) * (prev[0] - pt[0]) + (prev[1] - pt[1]) * (prev[1] - pt[1])))) {
                { double D[10][3]; ec_ev(w, 0, pt[0], pt[1], D); memcpy(P, D[0], sizeof P); }
                unsigned bits = ec_check_edge(w, pt, P); curbits = bits;
                if (w->fail) return -1;
                if (w->cnt == 0) {                        /* 18027422b */
                    memcpy(w->N[0], P, sizeof P); start[0] = pt[0]; start[1] = pt[1]; w->cnt = 1;
                    goto b23;
                }
                int16_t common = (int16_t)(prevbits & bits);
                if (0 < common) {                         /* 18027427c: along the boundary both points are on */
                    int along = common == 1 || common == 4, ri = along ? 0 : 1, inc = pt[ri] > prev[ri];
                    double cval = along ? pt[1] : pt[0], rng[2];
                    if (inc) { rng[0] = prev[ri]; rng[1] = pt[ri]; } else { rng[0] = pt[ri]; rng[1] = prev[ri]; }
                    int esi = w->cnt;
                    for (;;) {
                        double outp = 0; int ended = 0;
                        ec_gcu(w, !inc, along, cval, rng, &outp, &ended);
                        if (w->fail) return -1;
                        if (1 < esi && !ec_angle_ok(w->N[esi], w->ang, esi, w->N)) { w->cnt = esi; goto pass_end; }   /* 180274b73 */
                        if (ended) break;
                        ec_store(w, 1, w->cnt);           /* edgecrv_make_spline: FUN_180275800(1, ..) */
                        memcpy(w->N[0], w->N[w->cnt - 1], sizeof w->N[0]);
                        w->cnt = 1; esi = 1;
                        if (inc) rng[0] = outp; else rng[1] = outp;
                    }
                    goto b12;
                }
                if (dist3(w->N[w->cnt - 1], P) <= w->tol) goto b12;   /* 180274507 */
                if (1 < w->cnt && check_colinear(w->N[w->cnt - 2], w->N[w->cnt - 1], P, w->tol) && 1 < w->cnt && ec_colin2(prev2, prev, pt, w->tol)) {
                    memcpy(w->N[w->cnt - 1], P, sizeof P);
                    goto a25;
                }
                {   double T[2] = { pt[0], pt[1] }, TP[3] = { P[0], P[1], P[2] };   /* 1802745fb: the piece bisected in uv */
                    for (int it = 0; ; it++) {
                        if (it > 1000000) { w->fail = 1; w->why = "the trim walk's bisection did not end"; return -1; }
                        const double *Nl = w->N[w->cnt - 1];
                        double m3[3] = { (P[0] + Nl[0]) * 0.5, (P[1] + Nl[1]) * 0.5, (P[2] + Nl[2]) * 0.5 };
                        double mu[2] = { (prev[0] + pt[0]) * 0.5, (prev[1] + pt[1]) * 0.5 }, M[3];
                        { double D[10][3]; ec_ev(w, 0, mu[0], mu[1], D); memcpy(M, D[0], sizeof M); }
                        if (w->fail) return -1;
                        if (sqrt((mu[0] - pt[0]) * (mu[0] - pt[0]) + (mu[1] - pt[1]) * (mu[1] - pt[1])) > 1e-08 && dist3(M, m3) > w->chord * 0.8) {
                            pt[0] = mu[0]; pt[1] = mu[1]; memcpy(P, M, sizeof M);
                            continue;
                        }
                        if (!ec_angle_ok(P, w->ang, w->cnt, w->N)) goto pass_end;   /* 180274b78 */
                        memcpy(w->N[w->cnt], P, sizeof P); w->cnt++;
                        if (EC_MAXN <= w->cnt) {
                            ec_store(w, 0, w->cnt);
                            memcpy(w->N[0], w->N[w->cnt - 1], sizeof w->N[0]); w->cnt = 1;
                        }
                        if (1e-06 > sqrt((T[0] - pt[0]) * (T[0] - pt[0]) + (T[1] - pt[1]) * (T[1] - pt[1]))) break;
                        if (w->tol > dist3(TP, P)) break;
                        prev[0] = pt[0]; prev[1] = pt[1]; pt[0] = T[0]; pt[1] = T[1]; memcpy(P, TP, sizeof P);
                    }
                }
            a25:
                if (EC_MAXN <= w->cnt) {
                    ec_store(w, 0, w->cnt);
                    memcpy(w->N[0], w->N[w->cnt - 1], sizeof w->N[0]); w->cnt = 1;
                }
            }
        b12:
            if (1 < w->cnt) { prev2[0] = prev[0]; prev2[1] = prev[1]; }
        b23:
            prev[0] = pt[0]; prev[1] = pt[1];
            idx += dir;
            prevbits = curbits & 0xffff;
            if (idx < 0 || closeidx < idx) break;
        }
    pass_end:                                             /* 180274b90 */
        if (dir <= 0) {
            if (0 < w->cnt) for (int i = 0, j = w->cnt - 1; i < j; i++, j--) { double t[3]; memcpy(t, w->N[i], sizeof t); memcpy(w->N[i], w->N[j], sizeof t); memcpy(w->N[j], t, sizeof t); }
        } else {
            ec_store(w, startbits != 0, w->cnt);
            double D[10][3]; ec_ev(w, 0, start[0], start[1], D); memcpy(w->N[0], D[0], sizeof w->N[0]);
            w->cnt = 1;
        }
        prev[0] = start[0]; prev[1] = start[1];
    }
    return w->fail ? -1 : (w->nout > 0);
}
/* build_edgecrv_on_surf(ent, (u, v), chord, ang_deg, 0, list): the curves in list order (o[0] = list[0], what chain_single chains). Returns the
   count (0 none), -1 not ported (why). Untrimmed: one curve; trimmed (kind 10): the trim walk above. */
int ec_build_edgecrv_list(const McamFile *f, const Surf *s, double u, double v, double chord, double ang_deg, double tol, int nurbs, const McsSimpView *views, int nviews, PlCurve *o, int maxo, const char **why)
{
    if (maxo < 1) return 0;
    if (s->kind != 10) return ec_untrimmed(f, s, u, v, chord, tol, nurbs, views, nviews, o, why);
    EcW w; memset(&w, 0, sizeof w);
    w.f = f; w.tol = tol; w.chord = chord; w.ang = ang_deg * 0.01745329251994329; w.nurbs = nurbs; w.views = views; w.nviews = nviews;
    w.out = o; w.maxout = maxo;
    /* CSplineNodeMgr's node buffer is one for the session (csn_nodes1 above, CS-5): the walk reads past its count in one place (the angle
       test of N[esi] after get_constant_uvpts stopped before adding a node, 1802743c6..1802743e2), so the stale contents of earlier calls -
       earlier walks, Curve Parting Line - matter; zeroed at a file open / File > New (AllocateMem) */
    double (*nodes)[3] = csn_nodes1(EC_MAXN + 4);
    if (!nodes) { if (why) *why = "out of memory"; return -1; }
    w.N = nodes;
    int r = ec_trimmed(&w, s, u, v);
    if (r < 0) { for (int i = 0; i < w.nout; i++) plc_free(&o[i]); if (why) *why = w.why ? w.why : "NOT PORTED"; return -1; }
    return w.nout;
}
int ec_build_edgecrv(const McamFile *f, const Surf *s, double u, double v, double chord, double tol, int nurbs, const McsSimpView *views, int nviews, PlCurve *o, const char **why)
{
    memset(o, 0, sizeof *o);
    if (s->kind == 10) { if (why) *why = "a trimmed surface: ec_build_edgecrv_list (it needs the section stop angle)"; return -1; }
    return ec_untrimmed(f, s, u, v, chord, tol, nurbs, views, nviews, o, why);
}
