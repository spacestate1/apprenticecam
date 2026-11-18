/* cam3d_cscallop_fd80.c - Constant Scallop (op 43) stage C: FUN_18018fd80, the endpoint flags of a line (formats/surf_classic3.md 12.20),
 * as FUN_1801cfb60 calls it for kinds 6 / 7 (asm 1801d2d63..1801d2d80 / 1801d2e3c..1801d2e54: param_1 = local_3647 = 1 (FUN_18018b500's
 * eleventh output for kinds 6 / 7, FUN_18018b500 l.264801 case 6 / 7, call asm 1801d06e8), param_2 = 0, param_3 = local_3448 (a SysFile never opened in FUN_1801cfb60:
 * constructed at l.309830, only IsOpen-tested), param_4 = (kind - 8) < 2 = 0).
 * Sources: decomp/mcmsurf_all.c (l.N) and objdump of mcmsurf.dll (asm addresses; where the decompile indexes p_3d fields through a bool
 * "== true" the asm indexes by the bool's value 0 / 1, e.g. FUN_1801cad70 asm 1801cae4a..1801cae7d - the asm is followed).
 * Build with -ffp-contract=off. acos / sqrt are the C library's (the DLL: MSVCR120's), used in comparisons only.
 *
 * The 0x58-byte array node (FUN_1801cad70's output, FUN_18018fd80's local_d0, cs_fd_node_bytes): +0 short link (a run's first node: the
 * index of its last node), +2 byte flag, +8 the line position (DAT_1804e92b8), +0x10 s, +0x18 z, +0x20 normal a, +0x38 normal b, +0x50
 * short code a, +0x52 short code b, +0x54 short store node a, +0x56 short store node b. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

#define CP3(d, s) ((d)[0] = (s)[0], (d)[1] = (s)[1], (d)[2] = (s)[2])

/* mutl_v_angle_3d (mcmsurf 18025ce50, l.398750) */
double cs_v_angle_3d(const double *a, const double *b)
{
    double b0 = b[0], b1 = b[1], a0 = a[0], a1 = a[1], b2 = b[2], a2 = a[2];
    double d = b0 * a0 + b1 * a1 + b2 * a2;
    if (1e-05 <= fabs(d)) {
        double la = sqrt(a0 * a0 + a1 * a1 + a2 * a2), lb = sqrt(b0 * b0 + b1 * b1 + b2 * b2);
        d = d / (la * lb);
        if (d < -1.0) d = -1.0; else if (1.0 < d) d = 1.0;
    } else {
        double la = sqrt(a0 * a0 + a1 * a1 + a2 * a2), z = 0.0;
        if (la == 0.0) { a2 = 0.0; a1 = 0.0; a0 = 0.0; } else { a0 = a0 / la; a1 = a1 / la; a2 = a2 / la; }
        double lb = sqrt(b0 * b0 + b1 * b1 + b2 * b2);
        if (lb == 0.0) { b1 = 0.0; b0 = 0.0; } else { b0 = b0 / lb; b1 = b1 / lb; z = b2 / lb; }
        d = b0 * a0 + b1 * a1 + z * a2;
        if (fabs(d) < 1e-05) return 1.570796326794896;
        if (d < -1.0) d = -1.0; else if (1.0 < d) d = 1.0;
    }
    return acos(d);
}

/* FUN_1801cf800(k) (l.309326): the op's angle limits 165 / 15 degrees unless op type 0x27 with tol < op+0x12d0 < 180 (op 43 is not 0x27);
   1: max(DAT_1804e9238 x 2, tol x 10), 2: the large angle (rad), 3: the small one, 4: 2.70526034059121, 5: 9238 / 2, 6: 9238 x 3 */
double cs_fd_limit(int k, const CsCfbConst *C, int optype, double op12d0)
{
    double big = 165.0, small = 15.0;
    if (optype == 0x27 && C->systol <= op12d0 && op12d0 != C->systol && op12d0 < 180.0) { small = 180.0 - op12d0; big = op12d0; }
    switch (k) {
    case 1: { double d = C->t9238 * 2.0; if (d <= C->systol * 10.0) return C->systol * 10.0; return d; }
    case 2: return big * 0.01745329251994329;
    case 3: return small * 0.01745329251994329;
    case 4: return 2.70526034059121;
    case 5: return C->t9238 * 0.5;
    case 6: return C->t9238 * 3.0;
    default: return 0.0;
    }
}

/* FUN_180185e50 (l.261571): the direction of the line - the first node not code 0x24 with |s1 - s0| > DAT_1804e9250: s0 < s1; none: 1 */
int cs_fd_dir(const CsEst *L, const CsCfbConst *C)
{
    for (short k = L->head; k != -1; k = L->nd[k].next) {
        const CsNode *N = &L->nd[k];
        if (N->code != 0x24 && C->t9250 < fabs(N->s1 - N->s0)) return N->s0 <= N->s1 && N->s1 != N->s0;
    }
    return 1;
}

/* FUN_1801a12f0 (l.277379): node b (after a, -1: the head) unlinked onto the free list */
static void est_unlink(CsEst *L, short a, short b)
{
    if (b == -1) return;
    if (a != -1) L->nd[a].next = L->nd[b].next;
    if (L->head == b) L->head = L->nd[b].next;
    L->nfree++;
    L->nd[b].next = L->freeh; L->freeh = b;
    if (L->tail == b) L->tail = a;
}
/* FUN_180182b00(code) (l.259651): every node of that code unlinked */
void cs_fd_drop_code(CsEst *L, short code)
{
    short p = -1, k = L->head;
    while (k != -1) {
        short nx = L->nd[k].next;
        if (L->nd[k].code == code) est_unlink(L, p, k); else p = k;
        k = nx;
    }
}
/* FUN_1801c5620(code) (l.302394): a node whose neighbours both step up (prev's z1 - its z0 > DAT_1804e9238 and next's z0 - its z1 >
   9238, strictly) and which is shorter than DAT_1804e9250 (sqrt(dz^2 + ds^2), z1 - z0 / s1 - s0) gets the code; returns the count */
int cs_fd_mark_pits(CsEst *L, const CsCfbConst *C, short code)
{
    CsNode *E = L->nd;
    int cnt = 0;
    if (L->head == -1) return 0;
    short b = E[L->head].next;
    if (b == -1) return 0;
    short c = E[b].next;
    if (c == -1) return 0;
    short a = L->head;
    for (;;) {
        double z0 = E[b].z0, d = E[a].z1 - z0;
        if (C->t9238 <= d && d != C->t9238) {
            double z1 = E[b].z1, e = E[c].z0 - z1;
            if (C->t9238 <= e && e != C->t9238) {
                double dz = z1 - z0, ds = E[b].s1 - E[b].s0;
                if (sqrt(dz * dz + ds * ds) < C->t9250) { E[b].code = code; cnt++; }
            }
        }
        short n = E[c].next;
        a = b; b = c; c = n;
        if (n == -1) break;
    }
    return cnt;
}

void cs_fd_node_bytes(const CsFdNode *n, unsigned char o[0x58])
{
    memset(o, 0, 0x58);
    memcpy(o, &n->link, 2); o[2] = n->flag; memcpy(o + 8, &n->pos, 8); memcpy(o + 0x10, &n->s, 8); memcpy(o + 0x18, &n->z, 8);
    memcpy(o + 0x20, n->na, 24); memcpy(o + 0x38, n->nb, 24); memcpy(o + 0x50, &n->ca, 2); memcpy(o + 0x52, &n->cb, 2);
    memcpy(o + 0x54, &n->a, 2); memcpy(o + 0x56, &n->b, 2);
}

/* the FUN_18015d700(1, 1, 1, dir, 0, DAT_1804e9238, FUN_1801cf800(3), &f) block FUN_18018fd80 builds (asm 18018fe95..18018fecd) and
   FUN_18015f2f0 reads in its mode 1 (l.233433: f[0] = 1): f[2] = 1 (so the direction search is skipped), f[3] = dir, f[4] = 0, f[5] = 1 */
typedef struct { int dir; double t9238, small; } FdFlags;

/* FUN_18015f2f0 mode 1 (f[0] = 1, f[2] = 1, f[4] = 0, f[5] = 1; asm 18015f2f0..18015f5b5): the normal between store nodes k and nx when
   their z differ: out[0] = 0, out[2] = 0; when |k.z1 - nx.z0| < 10 x 9238 and the angle of k.n1 and nx.n0 < the small limit: k.n1;
   else out[1] = -1 or +1 by the step direction (out[1] keeps its old value only in the first case's absence - every path writes it).
   Returns 1. */
static void conn_normal1(const CsNode *k, const CsNode *nx, const FdFlags *F, double *out)
{
    out[0] = 0.0;
    out[2] = 0.0;
    if (fabs(k->z1 - nx->z0) < F->t9238 * 10.0) {
        double a = cs_v_angle_3d(k->n1, nx->n0);
        if (a < F->small) { CP3(out, k->n1); return; }
    }
    double z5 = k->z1, z4 = nx->z0;
    if ((z5 <= z4 || !F->dir) && (z4 <= z5 || F->dir)) out[1] = -1.0;
    else out[1] = 1.0;
}

static void fd_put(CsFdNode *d, short link, double pos, double s, double z, const double *na, const double *nb, short ca, short cb, short a, short b)
{
    d->link = link; d->flag = 0; d->pos = pos; d->s = s; d->z = z;
    if (na) CP3(d->na, na);
    if (nb) CP3(d->nb, nb);
    d->ca = ca; d->cb = cb; d->a = a; d->b = b;
}
/* FUN_1801cad70(0, pos, &f, &nruns, arr, 0) (l.306117; FUN_18018fd80's call at asm 18018ff50..18018ff71, param_6 = FUN_18018fd80's
   param_4 = 0): the line list as runs of array nodes - per run its first point (store node a = k, its s0 / z0, both normals k.n0), a
   joint where the next node starts within DAT_1804e9250 in s and z (|dz| <= 9250, and not exactly = 9250: k.n1 / nx.n0, codes k / nx,
   store nodes nx / k), or a step (|dz| > 9250) as two points (k's end: normals k.n1 / FUN_18015f2f0's, codes k / nx; nx's start: that
   normal / nx.n0), and its last point (|ds| >= 9250, or the end of the list: k's end, k.n1 twice, store node b = k); the run's first node
   gets the index of its last (link). Code 0x24 nodes are skipped. Returns the node count, *nruns the runs. */
int cs_fd_to_array(const CsEst *L, const CsCfbConst *C, double pos, const FdFlags *F, short *nruns, CsFdNode *arr)
{
    const CsNode *E = L->nd;
    int first = 1, cnt = 0, rs = 0;
    *nruns = 0;
    for (short k = L->head; k != -1; k = E[k].next) {
        if (E[k].code == 0x24) continue;
        int n = cnt;
        if (first) {
            fd_put(&arr[cnt], -1, pos, E[k].s0, E[k].z0, E[k].n0, E[k].n0, E[k].code, E[k].code, k, -1);
            n = cnt + 1; rs = cnt; first = 0;
        }
        short nx = E[k].next;
        int endrun = 0;
        if (nx == -1) endrun = 1;
        else {
            double d = fabs(E[k].s1 - E[nx].s0);
            if (C->t9250 <= d) endrun = 1;
            else {
                double dz = E[k].z1 - E[nx].z0;
                if (fabs(dz) < C->t9250 || fabs(dz) == C->t9250) {
                    if (C->t9250 <= fabs(dz)) endrun = 1;   /* asm: |dz| == 9250 */
                    else {
                        fd_put(&arr[n], 0, pos, E[nx].s0, E[nx].z0, E[k].n1, E[nx].n0, E[k].code, E[nx].code, nx, k);
                        cnt = n + 1;
                    }
                } else {
                    double nb[3];                         /* every component written by FUN_18015f2f0 mode 1 */
                    conn_normal1(&E[k], &E[nx], F, nb);
                    fd_put(&arr[n], 0, pos, E[k].s1, E[k].z1, E[k].n1, nb, E[k].code, E[nx].code, -1, k);
                    fd_put(&arr[n + 1], 0, pos, E[nx].s0, E[nx].z0, nb, E[nx].n0, E[nx].code, E[nx].code, nx, -1);
                    arr[n + 1].ca = arr[n].cb;
                    cnt = n + 2;
                }
            }
        }
        if (endrun) {
            fd_put(&arr[n], 0, pos, E[k].s1, E[k].z1, E[k].n1, E[k].n1, E[k].code, E[k].code, -1, k);
            arr[rs].link = (short)n;
            (*nruns)++;
            cnt = n + 1; first = 1;
        }
    }
    return cnt;
}


/* FUN_1801ca500(a, b, arr, tol, ang) (l.305828): can a .. b be one straight piece: for every node strictly between, its normals' angle
   (na, nb) <= ang and its distance to the chord a -> b (in pos / s / z), or twice its overshoot past the chord's length, <= tol */
static int straight(short a, short b, const CsFdNode *arr, double tol, double ang)
{
    if ((int)b - (int)a < 2) return 1;
    const CsFdNode *A = &arr[a];
    double ds = arr[b].s - A->s, dx = arr[b].pos - A->pos, dz = arr[b].z - A->z;
    double L = sqrt(ds * ds + dx * dx + dz * dz), ux, us, uz;
    if (L == 0.0) { uz = 0.0; us = 0.0; ux = 0.0; } else { ux = dx / L; us = ds / L; uz = dz / L; }
    for (;;) {
        a++;
        if (b <= a) return 1;
        if (ang < cs_v_angle_3d(arr[a].na, arr[a].nb)) return 0;
        double qx = arr[a].pos - A->pos, qs = arr[a].s - A->s, qz = arr[a].z - A->z;
        double d = sqrt(qs * qs + qx * qx + qz * qz), over;
        if (d <= L) over = 0.0; else over = (d - L) * 2.0;
        double c0 = qs * uz - qz * us, c1 = qz * ux - uz * qx, c2 = us * qx - qs * ux;
        double dist = sqrt(c1 * c1 + c0 * c0 + c2 * c2);
        if (dist <= over) dist = over;
        if (!(dist <= tol)) return 0;
    }
}
/* FUN_1801cb510(a, b, arr, tol, ang, mark) (l.306471): mark[a..b] = 0, then (more than 2 nodes, tol >= 0) in windows of 100 nodes: from a,
   each j = a + 2 .. that FUN_1801ca500 accepts marks j - 1, else the next piece starts at j - 1 */
static void mark_straight(short a, short b, const CsFdNode *arr, double tol, double ang, unsigned char *mark)
{
    for (short i = a; i <= b; i++) mark[i] = 0;
    if (!(2 < (short)(b - a + 1) && 0.0 <= tol)) return;
    short e = (short)(a + 100);
    if (b < e) e = b;
    for (;;) {
        mark[a] = 0;
        for (short j = (short)(a + 2); j <= e; j++) {
            if (straight(a, j, arr, tol, ang)) mark[j - 1] = 1;
            else a = (short)(j - 1);
        }
        if (e == b) break;
        a = (short)(e - 1);
        e = (short)(e + 99);
        if (b < e) e = b;
    }
}
/* FUN_1801cb640(nruns, n, arr, DAT_1804e9240, FUN_1801cf800(3)) (l.306524) without its pl2 byte array (here an argument): per run
   FUN_1801cb510, then every marked node gets code a = 0x24. Returns 1 (the DLL: 0 when pl2 has no room - not a case here) */
int cs_fd_straights(short nruns, int n, CsFdNode *arr, double tol, double ang, unsigned char *mark)
{
    short e = -1;
    for (int r = 0; r < nruns; r++) {
        short st = (short)(e + 1);
        e = arr[st].link;
        mark_straight(st, e, arr, tol, ang, mark);
    }
    for (int i = 0; i < n; i++) if (mark[i]) arr[i].ca = 0x24;
    return 1;
}

/* FUN_1801616d0(st, c, en, arr, dmax, dmin, idx, v) (l.235050): from node c, back to the first node (not of code 0x24 at either end)
   whose accumulated distance passes dmax (idx[0], v[0..2] = it - c), and forward likewise (idx[1], v[3..5]); 1 when both sums >= dmin */
static int side_vectors(short st, short c, short en, const CsFdNode *arr, double dmax, double dmin, short *idx, double *v)
{
    double sb = 0.0, sf = 0.0;
    short i = c;
    for (;;) {
        do {
            i--;
            if (i < st) goto back_done;
        } while (arr[i].ca == 0x24 || arr[i].cb == 0x24);
        idx[0] = i;
        v[0] = arr[i].pos - arr[c].pos; v[1] = arr[i].s - arr[c].s; v[2] = arr[i].z - arr[c].z;
        sb = sb + sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!(sb <= dmax)) break;
    }
back_done:
    if (!(dmin <= sb)) return 0;
    i = c;
    for (;;) {
        do {
            i++;
            if (en < i) goto fwd_done;
        } while (arr[i].ca == 0x24 || arr[i].cb == 0x24);
        idx[1] = i;
        v[3] = arr[i].pos - arr[c].pos; v[4] = arr[i].s - arr[c].s; v[5] = arr[i].z - arr[c].z;
        sf = sf + sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]);
        if (!(sf <= dmax)) break;
    }
fwd_done:
    return dmin <= sf;
}
/* FUN_180157540(i, c, 1, arr, d2) (l.227678): node i has code 0x24 at an end and lies within sqrt(d2) of c in (s, z) */
static int near24(short i, short c, const CsFdNode *arr, double d2)
{
    if (arr[i].ca != 0x24 && arr[i].cb != 0x24) return 0;
    double dz = arr[i].z - arr[c].z, ds = arr[i].s - arr[c].s;
    return dz * dz + ds * ds < d2;
}
/* FUN_18015c590(c, idx, 1, arr, ang, d2, out) (l.231526): among the nodes idx[0] .. idx[1] near c with code 0x24 (FUN_180157540), the pair
   of normals with the largest angle >= pi - ang: first each node's own (nb, na) -> out = (na, nb); none: pairs of nodes i < j (i's na or nb
   against j's na, then j's nb) -> out = (i's, j's). Returns 1 when found */
static int fold_normals(short c, const short *idx, const CsFdNode *arr, double ang, double d2, double *out)
{
    double best = 0.0; int found = 0;
    for (short i = idx[0]; i <= idx[1]; i++) {
        if (!near24(i, c, arr, d2)) continue;
        double a = cs_v_angle_3d(arr[i].nb, arr[i].na);
        if (3.141592653589793 - ang <= a && best <= a && a != best) { found = 1; CP3(out, arr[i].na); CP3(out + 3, arr[i].nb); best = a; }
    }
    if (found) return 1;
    for (short i = idx[0]; i < idx[1]; i++) {
        short nx = (short)(i + 1);
        if (!near24(i, c, arr, d2)) continue;
        for (int h = 0; h < 2; h++) {
            double p[3];
            if (h == 0) CP3(p, arr[i].na); else CP3(p, arr[i].nb);
            for (short j = nx; j <= idx[1]; j++) {
                if (!near24(j, c, arr, d2)) continue;
                double a = cs_v_angle_3d(p, arr[j].na);
                if (3.141592653589793 - ang <= a && best <= a && a != best) { found = 1; CP3(out, p); CP3(out + 3, arr[j].na); best = a; }
                a = cs_v_angle_3d(p, arr[j].nb);
                if (3.141592653589793 - ang <= a && best <= a && a != best) { found = 1; CP3(out, p); CP3(out + 3, arr[j].nb); best = a; }
            }
        }
    }
    return found;
}
int cs_l_ints(double tol, const double *a0, const double *a1, const double *b0, const double *b1, double *pt, double *ta, double *tb);
/* FUN_1801cc0f0(p1, 0, st, c, en, arr, dmax, dmin, ang) (l.307112, asm 1801cc0f0..1801cc681): the kind of the corner at node c - 0: too
   close to the run's ends (FUN_1801616d0); 1: the side vectors' angle > ang, or a fold with no FUN_18015c590 normals; else the offset
   lines (each side point and c moved 0.01 along the side's (s, z) normal: (dz, -ds) normalised with its z part made >= 0 when the side's
   ds is >= DAT_1804e9250 or its dz <= 9250, else (+-1, 0) by p1 and the side) intersected by l_ints: 0 none, 3 inside both, 2 else;
   when c's own normals are not opposite within ang (angle(nb, na) < pi - ang) its normals are replaced by FUN_18015c590's */
static int corner_kind(int p1, short st, short c, short en, CsFdNode *arr, double dmax, double dmin, double ang, double t9250, double tol)
{
    short idx[2]; double v[6];
    if (!side_vectors(st, c, en, arr, dmax, dmin, idx, v)) return 0;
    if (ang < cs_v_angle_3d(v, v + 3)) return 1;
    double dc = cs_v_angle_3d(arr[c].nb, arr[c].na);
    double ln[2][4];
    for (int k = 0; k < 2; k++) {
        double a = v[3 * k + 1], b = v[3 * k + 2], d[2];
        if (t9250 > fabs(a) && fabs(b) > t9250) {                 /* asm 1801cc345..1801cc3be */
            int al = b > 0.0;
            if (k == 0) al = !al;
            d[0] = (al ? p1 : !p1) ? -1.0 : 1.0; d[1] = 0.0;
        } else {                                                  /* asm 1801cc3c3..1801cc476 */
            double x = b, y = -a, L = sqrt(x * x + y * y);
            if (L == 0.0) { d[0] = 0.0; d[1] = 0.0; } else { d[0] = x / L; d[1] = y / L; }
            if (0.0 > d[1]) { d[0] = d[0] * -1.0; d[1] = d[1] * -1.0; }
        }
        const CsFdNode *P = &arr[idx[k]];
        ln[k][0] = P->s + d[0] * 0.01; ln[k][1] = P->z + d[1] * 0.01;
        ln[k][2] = arr[c].s + d[0] * 0.01; ln[k][3] = arr[c].z + d[1] * 0.01;
    }
    double pt[2], t1 = 0, t2 = 0;
    int kind;
    if (!cs_l_ints(tol, ln[0], ln[0] + 2, ln[1], ln[1] + 2, pt, &t1, &t2)) kind = 0;
    else if (1.0 > t1 && 1.0 > t2) kind = 3;
    else kind = 2;
    if (3.141592653589793 - ang <= dc) return kind;               /* asm 1801cc5ea */
    double out[6];
    if (!fold_normals(c, idx, arr, ang, dmax * dmax, out)) return 1;
    CP3(arr[c].na, out); CP3(arr[c].nb, out + 3);
    return kind;
}
/* FUN_18014b830(i, n, list, 1, arr, d2, se) (l.218057): around list[i], the list entries of flag 3 within sqrt(d2) in (s, z) backwards and
   forwards (stopping at the first farther one): se = the first and last; 1 when any */
static int flag3_span(short i, short n, const short *list, const CsFdNode *arr, double d2, short *se)
{
    int r = 0;
    se[1] = i; se[0] = i;
    for (short j = (short)(i - 1); j >= 0; j--) {
        const CsFdNode *A = &arr[list[j]], *B = &arr[list[i]];
        if (A->flag != 3) continue;
        double dz = A->z - B->z;
        if (d2 <= dz * dz + (A->s - B->s) * (A->s - B->s)) break;
        se[0] = j; r = 1;
    }
    for (short j = (short)(i + 1); j < n; j++) {
        const CsFdNode *A = &arr[list[j]], *B = &arr[list[i]];
        if (A->flag != 3) continue;
        double dz = A->z - B->z;
        if (d2 <= dz * dz + (A->s - B->s) * (A->s - B->s)) break;
        se[1] = j; r = 1;
    }
    return r;
}
/* FUN_18015add0(n, list, 0, arr, d) (l.230308): per list entry of flag 3 with neighbours (FUN_18014b830, d^2), the lowest z of the span's
   flag-3 entries keeps flag 3, the others get 1. Returns the count of such spans */
static int keep_lowest(short n, short *list, CsFdNode *arr, double d)
{
    int cnt = 0;
    double d2 = d * d;
    for (short i = 0; i < n; i++) {
        short se[2];
        if (arr[list[i]].flag != 3 || !flag3_span(i, n, list, arr, d2, se)) continue;
        cnt++;
        double zmin = 1e38;                                       /* 0x47d2ced32a16a1b1 */
        short keep = (short)(se[1] + 1);
        for (short j = se[0]; j <= se[1]; j++) {
            double z = arr[list[j]].z;
            if (arr[list[j]].flag == 3 && z <= zmin && zmin != z) { zmin = z; keep = j; }
        }
        if (keep <= se[1])
            for (short j = se[0]; j <= se[1]; j++) if (j != keep) arr[list[j]].flag = 1;
    }
    return cnt;
}
/* FUN_1801cdcf0(dir, 0, 1, nruns, arr, dmax, dmin, ang, d6, list) (l.308284; FUN_18018fd80's call asm 18018ffad..18018ffe4: dmax =
   FUN_1801cf800(1), dmin = 2 x tol, ang = FUN_1801cf800(2), d6 = FUN_1801cf800(6)): every inner node of a run with at most one code
   0x24 gets flag = FUN_1801cc0f0's kind; the kind-3 nodes listed; more than one -> FUN_18015add0 and the list kept to its flag-3 entries.
   Returns the list's count */
int cs_fd_corners(int dir, short nruns, CsFdNode *arr, double dmax, double dmin, double ang, double d6, double t9250, double tol, short *list)
{
    short e = -1, n = 0;
    for (int r = 0; r < nruns; r++) {
        short st = (short)(e + 1);
        e = arr[st].link;
        for (short c = (short)(st + 1); c < e; c++) {
            int n24 = (arr[c].ca == 0x24) + (arr[c].cb == 0x24);
            if (n24 < 2 || (arr[c].ca < 0x12 && arr[c].cb < 0x12 && n24 == 0)) {
                int k = corner_kind(dir, st, c, e, arr, dmax, dmin, ang, t9250, tol);
                arr[c].flag = (unsigned char)k;
                if (k == 3) list[n++] = c;
            }
        }
    }
    if (n < 2 || keep_lowest(n, list, arr, d6) < 1) return n;
    short m = 0;
    for (short i = 0; i < n; i++)
        if (arr[list[i]].flag == 3) { if (m < i) list[m] = list[i]; m++; }
    return m;
}
/* FUN_1801cfa90(nruns, arr) (l.309461): the inner nodes' flags back to the store (node a's f0, node b's f1); returns the flag-3 count */
int cs_fd_flags_back(CsEst *L, short nruns, const CsFdNode *arr)
{
    short e = -1; int cnt = 0;
    for (int r = 0; r < nruns; r++) {
        short st = (short)(e + 1);
        e = arr[st].link;
        for (short c = (short)(st + 1); c < e; c++) {
            if (arr[c].flag == 3) cnt++;
            if (arr[c].a != -1) L->nd[arr[c].a].f0 = arr[c].flag;
            if (arr[c].b != -1) L->nd[arr[c].b].f1 = arr[c].flag;
        }
    }
    return cnt;
}
/* FUN_1801c5a50(3) (l.302550): a flag 3 spread across joints (prev's end = this start within DAT_1804e9250 in s and z) and across the two
   ends of a point-like node; returns the count of flag-3 ends */
int cs_fd_spread(CsEst *L, const CsCfbConst *C, unsigned char f)
{
    CsNode *E = L->nd;
    short p = -1, k = L->head; int cnt = 0;
    if (k == -1) return 0;
    short last = -1;
    for (;;) {
        last = k;
        if (p != -1 && fabs(E[p].s1 - E[k].s0) < C->t9250 && fabs(E[p].z1 - E[k].z0) < C->t9250) {
            if (E[p].f1 == f) E[k].f0 = f;
            else if (E[k].f0 == f) E[p].f1 = f;
        }
        if (fabs(E[k].s1 - E[k].s0) < C->t9250 && fabs(E[k].z1 - E[k].z0) < C->t9250) {
            if (E[k].f1 == f) E[k].f0 = f;
            else if (E[k].f0 == f) E[k].f1 = f;
        }
        if (p != -1) { if (E[p].f0 == f) cnt++; if (E[p].f1 == f) cnt++; }
        short nx = E[k].next;
        p = k;
        if (nx == -1) break;
        k = nx;
    }
    if (E[last].f0 == f) cnt++;
    if (E[last].f1 == f) cnt++;
    return cnt;
}

/* FUN_18018fd80(1, 0, closed file, 0) for kinds 6 / 7 (l.267871, asm 18018fd80..1801900a0). Its two pl2 arrays (FUN_1800cdfa0(2, 0, 1)
   (n + 10) x 2 0x58 nodes, FUN_1800ce260(n + 10) shorts) and FUN_1801cb640's (n + 10 bytes) are the caller's here (fd_arr / fd_list /
   fd_mark, room for 2 x (n + 10)). pos = DAT_1804e92b8 (the line's position). Returns 0 (the DLL: FUN_180268e20(2) when pl2 has no room,
   not a case of the port). Step results are left in *R for the unit checks. */
void (*cs_fd_trace)(int stage, const CsEst *L, const CsFdNode *arr, int n, const short *list, int k, int v);
#define TR(st, n, list, k, v) do { if (cs_fd_trace) cs_fd_trace(st, L, arr, n, list, k, v); } while (0)
int cs_fd_endflags(CsEst *L, const CsCfbConst *C, double pos, int optype, double op12d0, CsFdNode *arr, short *list, unsigned char *mark,
                   CsFdResult *R)
{
    int n = 0;
    for (short k = L->head; k != -1; k = L->nd[k].next) { n++; L->nd[k].f0 = 0; L->nd[k].f1 = 0; }
    memset(R, 0, sizeof *R);
    if (n == 0) return 0;
    double d1 = cs_fd_limit(1, C, optype, op12d0), d2 = cs_fd_limit(2, C, optype, op12d0), d3 = cs_fd_limit(3, C, optype, op12d0);
    double d6 = cs_fd_limit(6, C, optype, op12d0), dmin = C->systol + C->systol;
    int dir = cs_fd_dir(L, C);
    FdFlags F = { dir, C->t9238, d3 };
    R->dir = dir;
    R->pits = cs_fd_mark_pits(L, C, 0x24);
    if (R->pits > 0) cs_fd_drop_code(L, 0x24);
    TR(0, 0, NULL, R->pits, dir);
    short nruns = 0;
    R->n = cs_fd_to_array(L, C, pos, &F, &nruns, arr);
    R->nruns = nruns;
    TR(1, R->n, NULL, nruns, 0);
    cs_fd_straights(nruns, R->n, arr, C->t9240, d3, mark);
    TR(2, R->n, NULL, 0, 0);
    R->ncorners = cs_fd_corners(dir, nruns, arr, d1, dmin, d2, d6, C->t9250, C->systol, list);
    TR(3, R->n, list, R->ncorners, 0);
    R->n3 = cs_fd_flags_back(L, nruns, arr);
    if (R->n3 > 0) R->spread = cs_fd_spread(L, C, 3);
    TR(4, R->n, NULL, R->n3, R->spread);
    return 0;
}

/* ---- the temp.rgn line block (FUN_180182de0(2, &DAT_1804e9060, &DAT_1804e92ec, DAT_1804e92f8) -> FUN_1801a1a70, l.277668) ---- */
static void rgn_put(CsBuf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 4096; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, p, n); b->n += n;
}
static void rgn_pt(CsBuf *b, double s, double z, short code, unsigned char flag)
{
    unsigned char r[0x18]; memset(r, 0, sizeof r);   /* +0x13..+0x17: the DLL's stack bytes after local_86 (not written by it): 0 here */
    memcpy(r, &s, 8); memcpy(r + 8, &z, 8); memcpy(r + 0x10, &code, 2); r[0x12] = flag;
    rgn_put(b, r, 0x18);
}
/* FUN_1801a1a70(file, &off, table): the line list (code 0x24 left out) as 0x18-byte points {s, z, short code, byte flag}: the first node's
   start; at each joint either the previous end and this start (|ds| or |dz| > DAT_1804e9250) or one point, their midpoint, with this
   node's code + 0x27 and its start flag; the last node's end; then the block trailer {byte 2, short count (+2), int table[line] (+4, the
   line's previous block), double pos (+8, DAT_1804e92b8)}; off += count x 0x18, table[line] = off (the trailer's position), off += 0x18.
   Returns 0 (the DLL: FUN_180268e20(1) on a short write). Trailer bytes +1 and +0x10..+0x17 are the DLL's stack (local_80[1], local_70):
   0 here; an empty list writes nothing. */
int cs_rgn_block(const CsEst *L, const CsCfbConst *C, int line, double pos, int *off, int *table, CsBuf *out)
{
    const CsNode *E = L->nd;
    short prev = -1; int cnt = 0;
    for (short k = L->head; k != -1; k = E[k].next) {
        if (E[k].code == 0x24) continue;
        if (prev == -1) { rgn_pt(out, E[k].s0, E[k].z0, E[k].code, E[k].f0); cnt++; }
        else {
            double s1 = E[prev].s1, s0 = E[k].s0;
            if (C->t9250 < fabs(s1 - s0) || C->t9250 < fabs(E[prev].z1 - E[k].z0)) {
                rgn_pt(out, s1, E[prev].z1, E[prev].code, E[prev].f1);
                rgn_pt(out, s0, E[k].z0, E[k].code, E[k].f0);
                cnt += 2;
            } else {
                rgn_pt(out, (s1 + s0) * 0.5, (E[prev].z1 + E[k].z0) * 0.5, (short)(E[k].code + 0x27), E[k].f0);
                cnt++;
            }
        }
        prev = k;
    }
    if (prev == -1) return 0;
    rgn_pt(out, E[prev].s1, E[prev].z1, E[prev].code, E[prev].f1);
    unsigned char t[0x18]; memset(t, 0, sizeof t);
    short n = (short)(cnt + 1);
    t[0] = 2; memcpy(t + 2, &n, 2); memcpy(t + 4, &table[line], 4); memcpy(t + 8, &pos, 8);
    rgn_put(out, t, 0x18);
    *off = *off + n * 0x18;
    table[line] = *off;
    *off = *off + 0x18;
    return 0;
}
