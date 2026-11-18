/* mcpartline_trim3.inc - make_surf_polygons on a trimmed surface, part 3: each grid cell against the trim loops (FUN_1805913b0 ->
 * FUN_1805909d0: the pieces of the cell inside the loops as polygons of loop points and cell corners) and the triangles of the pieces
 * (FUN_180592030: ear clipping). Included from mcpartline_mesh.c (see mcpartline_trim.inc). */

typedef struct {
    const double *R;          /* the cell: 4 corners (u0 v0) (u1 v0) (u1 v1) (u0 v1) */
    int ci[4];                /* their vertex indices (FUN_180598730 replaces one by a loop point's when they coincide) */
    const double *p; const int *tb; int nl, base;
    short *mk;                /* FUN_1805909d0's per loop point state (kept from cell to cell, as the DLL's arena block local_2c70) */
    double *op; int *oi; int no, cap;   /* the pieces' points and vertex indices (local_2c68 / local_2c78, cap = loop points + 4) */
    int *pt; int np;          /* the pieces: start, count (local_2c80) */
} Cell;

/* FUN_180592eb0 (disassembly 180592eb0): the cell edge a boundary point is on: 0 bottom, 1 right, 2 top, 3 left (counter-clockwise) */
static int cell_edge(const double *P, const double *R)
{
    if (P[1] == R[1]) return P[0] == R[2] ? 1 : 0;
    if (P[1] == R[5]) return P[0] == R[6] ? 3 : 2;
    if (P[0] == R[2]) return 1;
    if (P[0] == R[6]) return 3;
    int e = 0;
    if (!(1e-10 < R[2] - P[0])) e = 1;
    if (!(1e-10 < R[7] - P[1])) e = 2;
    if (!(1e-10 < P[0] - R[0]) && P[1] - R[1] > 1e-10) e = 3;
    return e;
}
/* FUN_180592a30 (disassembly 180592a30): A at or past B along edge e */
static int edge_past(const double *A, const double *B, int e)
{
    switch (e) {
    case 0: return A[0] >= B[0];
    case 1: return A[1] >= B[1];
    case 2: return B[0] >= A[0];
    default: return B[1] >= A[1];
    }
}
/* FUN_180592c20 (disassembly 180592c20): P moved to the end corner of edge e */
static void edge_end(double *P, int e, const double *R)
{
    if (e == 0) P[0] = R[2]; else if (e == 1) P[1] = R[7]; else if (e == 2) P[0] = R[0]; else P[1] = R[1];
}
/* FUN_180592a90 (msp_trim.c l.1557): a point and its vertex index onto the current piece */
static void piece_add(Cell *C, const double *P, int vi)
{
    if (C->no < C->cap) {
        C->op[2 * C->no] = P[0]; C->op[2 * C->no + 1] = P[1]; C->oi[C->no] = vi;
        C->pt[2 * C->np + 1]++; C->no++;
    }
}
/* FUN_180598730 (msp_trim.c l.2299): a cell corner equal to loop point k takes its vertex index */
static void corner_idx(Cell *C, int k)
{
    for (int c = 0; c < 4; c++) if (C->R[2 * c] == PX(C->p, k) && C->R[2 * c + 1] == PY(C->p, k)) C->ci[c] = k + C->base;
}
/* FUN_180591130 (msp_trim.c l.2400): loop point i on the cell edge and j: does the loop at i go along the edge forwards (fwd 1: j after i
   on the edge counter-clockwise; fwd 0: j before i) - 0 when j is not on i's edge */
static int along_edge(const double *R, const double *p, int i, int j, int fwd)
{
    double a, b, y = R[1], yi = PY(p, i), xi;
    if (y == yi) {                                                             /* bottom */
        xi = PX(p, i);
        if (!fwd) { if (R[2] == xi) { a = 0.0; goto right; } }
        else if (R[0] == xi) { a = 1.0; goto left; }
        a = (xi - R[0]) / (R[2] - R[0]);
        if (y != PY(p, j)) return 0;
        b = (PX(p, j) - R[0]) / (R[2] - R[0]);
        goto cmp;
    }
    xi = PX(p, i);
    if (R[2] != xi) {
        if (R[5] == yi) {                                                      /* top */
            if (!fwd) { if (R[6] == xi) { a = 0.0; goto left; } }
            else if (R[4] == xi) { a = 1.0; goto right; }
            a = (xi - R[4]) / (R[6] - R[4]);
        } else {                                                               /* left */
            if (R[6] != xi) return 0;
            if (!fwd || R[7] != yi) {
                a = (yi - R[7]) / (y - R[7]);
                goto left;
            }
            a = 1.0;
        }
        if (R[5] != PY(p, j)) return 0;                                        /* LAB_1805912fd */
        b = (PX(p, j) - R[4]) / (R[6] - R[4]);
        goto cmp;
    }
    if (!fwd) {                                                                /* right */
        if (R[5] == yi) { a = 0.0; if (R[5] != PY(p, j)) return 0; b = (PX(p, j) - R[4]) / (R[6] - R[4]); goto cmp; }
    } else if (R[3] == yi) {
        a = 1.0;
        if (y != PY(p, j)) return 0;
        b = (PX(p, j) - R[0]) / (R[2] - R[0]);
        goto cmp;
    }
    a = (yi - R[3]) / (R[5] - R[3]);
right:
    if (R[2] != PX(p, j)) return 0;
    b = (PY(p, j) - R[3]) / (R[5] - R[3]);
    goto cmp;
left:
    if (R[6] != PX(p, j)) return 0;
    b = (PY(p, j) - R[7]) / (y - R[7]);
cmp:
    return fwd ? b < a : a < b;
}
/* FUN_180592ae0 (disassembly 180592ae0): entry point i between the exit point j and the piece's first point k, counter-clockwise */
static int entry_between(const double *p, const double *R, int i, int j, int k)
{
    const double *I = p + 2 * (size_t)i, *J = p + 2 * (size_t)j, *K = p + 2 * (size_t)k;
    int ej = cell_edge(J, R), ei = cell_edge(I, R), ek = cell_edge(K, R);
    if (ej == ek) {
        if (ei == ej) {
            if (edge_past(K, J, ej)) { if (!edge_past(I, J, ei)) return 0; }
            else if (edge_past(I, J, ei)) return 1;
            return edge_past(K, I, ei);
        }
        if (edge_past(K, J, ej)) return 0;
    } else if (ei == ej) return edge_past(I, J, ei);
    else if (ei == ek) return edge_past(K, I, ei);
    int r = 0;
    for (int e = ej;;) { e++; if (e > 3) e = 0; if (e == ei) r = 1; if (e == ek) break; }
    return r;
}
/* FUN_18059be40 (msp_trim.c l.2320): the cell corners from the piece's last point to P */
static void corners_to(Cell *C, const double *P)
{
    int st = C->pt[2 * C->np];
    double L[2] = { C->op[2 * (st + C->pt[2 * C->np + 1])], C->op[2 * (st + C->pt[2 * C->np + 1]) + 1] };
    int eP = cell_edge(P, C->R), e = cell_edge(L, C->R);
    for (int n = 0; n < 4; n++) {
        if (eP == e && edge_past(P, L, e)) return;
        edge_end(L, e, C->R);
        e++; if (e > 3) e = 0;
        int last = st + C->pt[2 * C->np + 1];
        if (L[0] != C->op[2 * last] || L[1] != C->op[2 * last + 1]) {
            if (L[0] == P[0] && L[1] == P[1]) continue;
            piece_add(C, L, C->ci[e]);
        }
    }
}
/* FUN_18059b950 (msp_trim.c l.2221): the piece closed (the corners from its last point to its first) and the next one begun */
static void piece_close(Cell *C)
{
    int st = C->pt[2 * C->np], cnt = C->pt[2 * C->np + 1];
    if (cnt < 1) { C->no = st; C->pt[2 * C->np + 1] = -1; return; }
    double L[2] = { C->op[2 * (st + cnt)], C->op[2 * (st + cnt) + 1] }, F[2] = { C->op[2 * st], C->op[2 * st + 1] };
    int eF = cell_edge(F, C->R), e = cell_edge(L, C->R);
    for (int n = 0; n < 4; n++) {
        if (eF == e && edge_past(F, L, e)) break;
        edge_end(L, e, C->R);
        e++; if (e > 3) e = 0;
        int last = st + C->pt[2 * C->np + 1];
        if ((L[0] != C->op[2 * last] || L[1] != C->op[2 * last + 1]) && (L[0] != F[0] || L[1] != F[1])) piece_add(C, L, C->ci[e]);
    }
    C->np++;
    C->pt[2 * C->np] = C->no; C->pt[2 * C->np + 1] = -1;
}

/* FUN_180592c60 (msp_trim.c l.1599): the first point of loop l outside the (closed) cell, -1 none */
static int first_out(const double *p, const int *tb, int l, const double *R)
{
    for (int k = tb[2 * l]; k <= tb[2 * l] + tb[2 * l + 1]; k++)
        if (PX(p, k) < R[0] || PY(p, k) < R[1] || R[4] < PX(p, k) || R[5] < PY(p, k)) return k;
    return -1;
}
/* -1 outside the closed cell, 0 on its edge, 1 inside (1805909d0 18059aab1..180590b19) */
static int cell_side(const double *P, const double *R)
{
    if (R[0] > P[0] || R[1] > P[1] || P[0] > R[4] || P[1] > R[5]) return -1;
    return (P[0] == R[0] || P[1] == R[1] || P[0] == R[4] || P[1] == R[5]) ? 0 : 1;
}

/* FUN_1805909d0 (msp_trim.c l.1266, disassembly 1805909d0..180591126): each loop leaving the cell marked (3 outside, 2 inside, 1 where it
   enters), then the pieces traced: from an entry along the loop to its exit, on to the nearest entry counter-clockwise along the cell edge
   (FUN_180592ae0, the cell corners between: FUN_18059be40), closed back to the start (FUN_18059b950) */
static void cell_pieces(Cell *C)
{
    const double *R = C->R, *p = C->p; const int *tb = C->tb;
    int nent = 0;
    for (int l = 0; l < C->nl; l++) {
        int k = first_out(p, tb, l, R);
        if (k < 0) continue;
        int s = tb[2 * l], e = s + tb[2 * l + 1], in = 0;
        C->mk[k] = 3;
        for (int it = 0; it < tb[2 * l + 1]; it++) {
            k++; if (k > e) k = s;
            int side = cell_side(p + 2 * (size_t)k, R);
            if (side == 0) {
                if (!in) {
                    int nx = k + 1 > e ? s : k + 1;
                    if (along_edge(R, p, k, nx, 1)) { C->mk[k] = 3; in = 0; continue; }
                    nent++; in = 1; C->mk[k] = 1; corner_idx(C, k); continue;   /* 180590c06 */
                }
                int pv = k - 1 < s ? e : k - 1;
                if (!along_edge(R, p, k, pv, 0)) { C->mk[k] = 2; corner_idx(C, k); continue; }   /* 180590c14: the exit */
                int nx = k + 1 > e ? s : k + 1;
                if (cell_side(p + 2 * (size_t)nx, R) == 0 && !along_edge(R, p, k, nx, 1)) { nent++; in = 1; C->mk[k] = 1; corner_idx(C, k); continue; }
                C->mk[k] = 3; in = 0;
            } else if (side < 0) {
                C->mk[k] = 3; in = 0;
            } else {
                C->mk[k] = 2;
                if (!in) {
                    int pv = k - 1 < s ? e : k - 1;
                    if (cell_side(p + 2 * (size_t)pv, R) == 0) { C->mk[pv] = 1; nent++; in = 1; corner_idx(C, pv); }
                }
            }
        }
    }
    int fl = 0, fk = 0;                                                        /* [rsp+0x60], [rsp+0x64] */
    while (0 < nent) {
        for (int l = 0, found = 0; l < C->nl && !found; l++)
            for (int k = 0; k <= tb[2 * l + 1]; k++) if (C->mk[tb[2 * l] + k] == 1) { fl = l; fk = k; found = 1; break; }
        int cl = fl, ck = fk, first = tb[2 * fl] + fk;
        for (;;) {
            nent--;                                                            /* 180590e30 */
            int idx = tb[2 * cl] + ck, last;
            C->mk[idx] = 2;
            do {
                piece_add(C, p + 2 * (size_t)idx, C->base + idx);
                last = idx;
                ck++; C->mk[idx] = 0;
                if (tb[2 * cl + 1] < ck) ck = 0;
                idx = tb[2 * cl] + ck;
            } while (C->mk[idx] == 2);
            int best = first, got = 0, bl = cl, bk = ck, left = nent;
            if (left < 1) break;
            for (int l = 0, k = 0; ; ) {                                       /* 180590f50 */
                if (C->mk[tb[2 * l] + k] == 1) {
                    left--;
                    if (entry_between(p, R, tb[2 * l] + k, last, best)) { got = 1; bl = l; bk = k; best = tb[2 * l] + k; }
                }
                k++;
                if (tb[2 * l + 1] < k) { l++; if (C->nl <= l) break; k = 0; }
                if (!(0 < left)) break;
            }
            if (!got) break;
            cl = bl; ck = bk;
            fl = bl;
            corners_to(C, p + 2 * (size_t)(tb[2 * cl] + ck));                  /* 18059106f */
        }
        piece_close(C);                                                        /* 1805910cb */
    }
}

/* FUN_1805913b0 (core_tess.c l.5551): the pieces of the cell; the outer loop wholly inside it is the one piece; no loop crossing it and
   its middle inside the loops: the cell itself. Returns the piece count */
static int cell_clip(Cell *C, int outer)
{
    C->no = 0; C->np = 0; C->pt[0] = 0; C->pt[1] = -1;
    if (first_out(C->p, C->tb, outer, C->R) == -1) {
        for (int k = C->tb[2 * outer]; k <= C->tb[2 * outer] + C->tb[2 * outer + 1]; k++) piece_add(C, C->p + 2 * (size_t)k, k + C->base);
        return C->pt[1] < 0 ? 0 : 1;
    }
    cell_pieces(C);
    if (C->np) return C->np;
    double m[2] = { (C->R[2] + C->R[0]) * 0.5, (C->R[7] + C->R[1]) * 0.5 };
    if (in_polygon(C->nl, C->p, C->tb, m) == 1) {
        for (int c = 0; c < 4; c++) piece_add(C, C->R + 2 * c, C->ci[c]);
        if (-1 < C->pt[1]) return 1;
    }
    return 0;
}

/* FUN_18058aea0 (msp_trim.c l.1021): point (x, y) against the polygon v[0..n-1]: 1 inside, -1 outside, 0 within t1 of a vertex or t2 of an
   edge. The DLL's walk skips runs of vertices beyond the point's box (t1) - kept as it is. */
static int pt_in_poly_tol(double t1, double t2, int n, const double *v, double x, double y)
{
    int odd = 0, i9 = -1; long u12 = 0, u16 = 0, u13 = 0;
    double xr = t1 + x, yt = t1 + y, yb = y - t1;
    int b21, b22;
af50:
    {
        const double *P = v + 2 * u16;
        if (P[0] <= xr) {
            if (yt < P[1]) {
                do { u13 = u16; i9 = (int)u12; u12++; u16++; if (u12 == n) goto b034; } while (yt < v[2 * u16 + 1]);
                goto b07d;
            b034:
                b22 = v[1] == yt; b21 = v[1] < yt;
                goto b075;
            }
            if (yb <= P[1]) goto b0a4;
            do { u13 = u16; i9 = (int)u12; u12++; u16++; if (u12 == n) goto b06f; } while (v[2 * u16 + 1] < yb);
            goto b07d;
        b06f:
            b22 = yb == v[1]; b21 = yb < v[1];
        b075:
            if (b21 || b22) goto b0a4;
            return odd ? 1 : -1;
        b07d:
            if (v[2 * u13] <= xr) goto b0a4;
            b22 = v[2 * u16] == xr; b21 = v[2 * u16] < xr;
            goto b09e;
        }
        do { u13 = u16; i9 = (int)u12; u12++; u16++; if (u12 == n) goto af93; } while (xr < v[2 * u16]);
        goto afb7;
    af93:
        if (v[0] <= xr) goto b0a4;
        return odd ? 1 : -1;
    }
afb7:
    if (v[2 * u13 + 1] <= yt || v[2 * u16 + 1] <= yt) {
        if (v[2 * u13 + 1] < yb) {
            b22 = yb == v[2 * u16 + 1]; b21 = yb < v[2 * u16 + 1];
        b09e:
            if (!b21 && !b22) goto af50;
        }
        goto b0a4;
    }
    goto af50;
b0a4:
    {
        long l19 = i9;
        for (;;) {
            int i11 = (int)u12;
            if (i9 != -1) {
                long c = 0; int i17 = 0;
                if (i11 != n) { c = u16; i17 = i11; }
                double x1 = v[2 * c], y1 = v[2 * c + 1], x0 = v[2 * l19], y0 = v[2 * l19 + 1];
                if (fabs(x - x1) < t1 && fabs(y - y1) < t1) return 0;
                if ((y < y1 && y0 < y) || (y1 < y && y < y0)) {
                    double xi = ((y - y1) * (x0 - x1)) / (y0 - y1) + x1;
                    if (xi < x) odd ^= 1;
                    if (!(fabs(y1 - y0) < fabs(x1 - x0))) { if (fabs(x - xi) < t2) return 0; goto b2ca; }
                } else if (y1 == y && x1 < x && y != y0) {
                    long c2 = c + 1; int i18 = i17 + 1;
                    if (i17 + 1 == n) { c2 = 0; i18 = 0; }
                    if (v[2 * c2 + 1] == y) { c2 = c2 + 1; if (i18 + 1 == n) c2 = 0; }
                    int keep = (v[2 * c2 + 1] < y && y0 < y) || (y < v[2 * c2 + 1] && y < y0);
                    if (!keep) odd ^= 1;
                }
                /* LAB_18058b221 */
                if (fabs(y1 - y0) < fabs(x1 - x0)) {
                    if ((x1 < x && x < x0) || (x < x1 && x0 < x)) { double yi = ((y0 - y1) * (x - x1)) / (x0 - x1) + y1; if (fabs(y - yi) < t2) return 0; }
                } else if ((y1 < y && y < y0) || (y < y1 && y0 < y)) {
                    if (fabs(x - (((y - y1) * (x0 - x1)) / (y0 - y1) + x1)) < t2) return 0;
                }
            }
        b2ca:
            if (i11 == n) return odd ? 1 : -1;
            if (xr < v[2 * u16] || yt < v[2 * u16 + 1] || v[2 * u16 + 1] < yb) break;
            l19 = u16; u16++; u12 = i11 + 1; i9 = i11;
        }
    }
    goto af50;
}

/* FUN_180592f60 (core_tess.c l.10289, disassembly 180592f60): point P inside the ear (a, b, c) (FUN_18058aea0, tolerances 9.9e-11 / 9.801e-11),
   or on it while strict. Returns 1 = not an ear; -1 not ported (the touching-point tests FUN_180599690 / FUN_1805999f0) */
static int ear_blocked(int strict, const double *P, const double *a, const double *b, const double *c)
{
    double tri[6] = { a[0], a[1], b[0], b[1], c[0], c[1] };
    int r = pt_in_poly_tol(9.900000000000001e-11, 9.801e-11, 3, tri, P[0], P[1]);
    if (r == 1) return 1;
    if (r != 0) return 0;
    if (strict) return 1;
    /* not strict: P on a corner of the ear (FUN_180599690) or on an edge (FUN_180592ce0 -> FUN_1805999f0): NOT PORTED; else 1 */
    if (fabs(a[0] - P[0]) < 1e-10 && fabs(a[1] - P[1]) < 1e-10) return -1;
    if (fabs(b[0] - P[0]) < 1e-10 && fabs(b[1] - P[1]) < 1e-10) return -1;
    if (fabs(c[0] - P[0]) < 1e-10 && fabs(c[1] - P[1]) < 1e-10) return -1;
    double q[8] = { a[0], a[1], b[0], b[1], c[0], c[1], P[0], P[1] };
    if (pt_on_seg(q, 3, 0, 1) || pt_on_seg(q, 3, 1, 2) || pt_on_seg(q, 3, 2, 0)) return -1;
    return 1;
}

/* make_surf_model's boundary edge records (surf_classic3.md 12.11): FUN_18059c040 (core_tess.c l.6083, disassembly 18059c040..18059c517)
   -> FUN_18059c520 (msm2.c l.729, disassembly 18059c520..18059c674). Edge i1 -> i2 of the vertices P / N (through the frame), i3 the
   triangle's third vertex, prm = *param_2, *param_3, *param_4, *param_5 (record +0x58, +0x60, +0x68, +0x70). tol = mccore's ?tol@@3NA
   (0x1812b5b58: the system tolerance). The plane normal: 0 when an edge side is shorter than tol, the vertex normals face apart, or the
   triangle is within acos 0.995 of degenerate; else q = N_k x a (k the end whose normal leans more along m = a x (b x a), each dot taken
   with its sign flipped when that normal points down), turned to m's side, kept when its z >= -0.999847. Sums in the DLL's order. */
typedef struct { const double *P, *N; double tol; const int *tb; int nl, base; PlEdge *e; int ne, cap; } EdgeOut;
static void unit_f2f0(double v[3])   /* FUN_18000f2f0 (disassembly 18000f2f0): sqrt((x^2 + y^2) + z^2), 0 -> 0, else each / it */
{
    double l = sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]);
    if (l == 0.0) { v[0] = v[1] = v[2] = 0.0; return; }
    v[0] /= l; v[1] /= l; v[2] /= l;
}
static void edge_rec(EdgeOut *eo, int i1, int i2, int i3, double p2, double p3, double p4, double p5)
{
    const double *P = eo->P, *N = eo->N, *A = P + 3 * (size_t)i1, *B = P + 3 * (size_t)i2, *C3 = P + 3 * (size_t)i3;
    const double *N1 = N + 3 * (size_t)i1, *N2 = N + 3 * (size_t)i2;
    double a[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, b[3] = { C3[0] - A[0], C3[1] - A[1], C3[2] - A[2] };
    double L1 = sqrt((a[0] * a[0] + a[1] * a[1]) + a[2] * a[2]);
    double L2 = sqrt((b[1] * b[1] + b[0] * b[0]) + b[2] * b[2]);
    double q[3] = { 0.0, 0.0, 0.0 };
    if (!(eo->tol > L1) && !(eo->tol > L2) && !(0.0 > (N1[1] * N2[1] + N2[0] * N1[0]) + N1[2] * N2[2])
        && !(fabs(((b[1] * a[1] + b[0] * a[0]) + b[2] * a[2]) / (L2 * L1)) > 0.995)) {
        double n0 = b[1] * a[2] - b[2] * a[1], n1 = b[2] * a[0] - a[2] * b[0], n2 = a[1] * b[0] - b[1] * a[0];
        double m[3] = { n2 * a[1] - n1 * a[2], n0 * a[2] - n2 * a[0], n1 * a[0] - n0 * a[1] };
        unit_f2f0(m);
        double d1 = (m[1] * N1[1] + m[0] * N1[0]) + m[2] * N1[2]; if (0.0 > N1[2]) d1 = -d1;
        double d2 = (m[1] * N2[1] + m[0] * N2[0]) + m[2] * N2[2]; if (0.0 > N2[2]) d2 = -d2;
        const double *Nk = N + 3 * (size_t)(d2 > d1 ? i2 : i1);
        if (!(fabs(((a[1] * Nk[1] + a[0] * Nk[0]) + a[2] * Nk[2]) / L1) > 0.995)) {
            q[0] = Nk[1] * a[2] - Nk[2] * a[1]; q[1] = Nk[2] * a[0] - Nk[0] * a[2]; q[2] = Nk[0] * a[1] - Nk[1] * a[0];
            unit_f2f0(q);
            if (0.0 > (q[0] * m[0] + q[1] * m[1]) + q[2] * m[2]) { q[0] = -q[0]; q[1] = -q[1]; q[2] = -q[2]; }
            if (-0.999847 > q[2]) q[0] = q[1] = q[2] = 0.0;
        }
    }
    if (eo->ne == eo->cap) { eo->cap = eo->cap ? 2 * eo->cap : 256; eo->e = realloc(eo->e, sizeof(PlEdge) * (size_t)eo->cap); }
    PlEdge *e = &eo->e[eo->ne++];
    memset(e, 0, sizeof *e);
    memcpy(e->P1, A, sizeof e->P1); e->i1 = i1; memcpy(e->P2, B, sizeof e->P2); e->i2 = i2; memcpy(e->pl, q, sizeof q);
    e->prm[0] = p2; e->prm[1] = p3; e->prm[2] = p4; e->prm[3] = p5;
    memcpy(e->N1, N1, sizeof e->N1); memcpy(e->N2, N2, sizeof e->N2);
    double z1 = N1[2], z2 = N2[2], z3 = N[3 * (size_t)i3 + 2];               /* FUN_18059c520: 0 when all three normals up or all down */
    e->flag = !((z1 > 1e-05 && z2 > 1e-05 && z3 > 1e-05) || (-1e-05 > z1 && -1e-05 > z2 && -1e-05 > z3));
}
/* FUN_180592030's edge test (disassembly 1805923d9..18059271a, param_12 = in_stack_d0 > 0): the ear's side ia -> ib is a boundary edge
   when both are loop points (>= base) and ib follows ia along its loop (the loop's last point followed by its first) */
static void ear_edge(EdgeOut *eo, int ia, int ib, int ic, const double *ua, const double *ub)
{
    if (ia < eo->base || ib < eo->base) return;
    int want = ia + 1;
    for (int l = 0; l < eo->nl; l++) if (ia == eo->tb[2 * l + 1] + eo->tb[2 * l] + eo->base) want = eo->tb[2 * l] + eo->base;
    if (ib == want) edge_rec(eo, ia, ib, ic, ua[0], ua[1], ub[0], ub[1]);
}

/* FUN_180592030 (core_tess.c l.4700, disassembly 180592030..): the pieces cut into triangles by ear clipping (the corner prev, cur, next taken
   when left-turning, or straight with cur between, once the strict passes stall, and no other live point in it); triangles (vertex index
   of prev, cur, next) appended; with eo (make_surf_model's in_stack_d0 > 0) the ear's boundary edges (pv, cur), (cur, nx), (nx, pv) */
static int cell_tris(const Cell *C, int npc, int **tri, int *ntri, int *cap, EdgeOut *eo)
{
    for (int q = 0; q < npc; q++) {
        int st = C->pt[2 * q], n = C->pt[2 * q + 1] + 1;
        if (!(2 < n)) continue;
        const double *P = C->op + 2 * (size_t)st;
        char act[n > 0 ? n : 1];
        for (int k = 0; k < n; k++) act[k] = 1;
        int nx = 2, cur = 1, pv = 0, left = n, stall = n, strict = 1, b4 = 1, b5 = 1;
        do {
            stall--;
            const double *A = P + 2 * pv, *B = P + 2 * cur, *Cc = P + 2 * nx;
            double cr = (Cc[0] - B[0]) * (A[1] - B[1]) - (A[0] - B[0]) * (Cc[1] - B[1]);
            int nx2 = nx;
            if ((fabs(cr) < 1e-06 && !b4 && pt_on_seg(C->op, st + cur, st + pv, st + nx)) || 0.0 < cr) {
                int blocked = 0;
                b4 = b5;
                for (int it = 0, k = nx; it < n; it++) {
                    k++; if (k == n) k = 0;
                    if (k == pv) break;
                    if (act[k] == 1) {
                        int r = ear_blocked(strict, P + 2 * k, A, B, Cc);
                        if (r < 0) return -1;
                        if (r) { blocked = 1; break; }
                    }
                }
                if (!blocked) {
                    if (eo) {
                        int a0 = C->oi[st + pv], a1 = C->oi[st + cur], a2 = C->oi[st + nx];
                        const double *ua = C->op + 2 * (size_t)(st + pv), *ub = C->op + 2 * (size_t)(st + cur), *uc = C->op + 2 * (size_t)(st + nx);
                        ear_edge(eo, a0, a1, a2, ua, ub); ear_edge(eo, a1, a2, a0, ub, uc); ear_edge(eo, a2, a0, a1, uc, ua);
                    }
                    if (*ntri == *cap) { *cap = *cap ? 2 * *cap : 256; *tri = realloc(*tri, sizeof(int) * 3 * (size_t)*cap); }
                    (*tri)[3 * *ntri] = C->oi[st + pv]; (*tri)[3 * *ntri + 1] = C->oi[st + cur]; (*tri)[3 * *ntri + 2] = C->oi[st + nx];
                    (*ntri)++;
                    left--; act[cur] = 0; strict = 1; stall = left;
                    cur = nx;
                    for (int it = 0; it < n; it++) { nx2++; if (nx2 == n) nx2 = 0; if (act[nx2] == 1) break; }
                    nx = nx2;
                }
            }
            /* LAB_180592831 */
            pv = cur;
            int t = nx;
            for (int it = 0; it < n; it++) { t++; if (t == n) t = 0; if (act[t] == 1) break; }
            cur = nx; nx = t;
            if (stall == 0) {
                stall = left;
                if (!strict || left < 4) {
                    if (!b4 || strict || left < 4) break;
                    b4 = 0; b5 = 0;
                    if (left == 0) break;
                } else {
                    strict = 0;
                    if (left == 0) {
                        if (!b4 || strict || left < 4) break;
                        b4 = 0; b5 = 0;
                        break;
                    }
                }
            }
        } while (2 < left);
    }
    return 0;
}

/* FUN_18057eeb0 (msm2.c l.452; fill_mach_model_buffer's border cells, called from FUN_18057ebd0 until the cell's pieces are done): the ear
   clipping of FUN_180592030 in resumable form, which differs in where it goes on after an ear: per piece a8 = 0, ac = 1, b0 = 2 (+0xa8 /
   +0xac / +0xb0), all points live, the stall count +0xb4 = live +0xb8; per step the count -1, the corner (a8, ac, b0) is an ear when
   left-turning (or straight with ac on a8-b0 while b4 is off) and no live point from b0 + 1 up to a8 lies in it (FUN_180592f60);
   an ear -> triangle (a8, ac, b0) (FUN_180591f80), ac dropped, ac = b0, b0 = the next live point, a8 KEPT, count = live, and the next
   call starts strict = b4 = 1 again; no ear -> a8 = ac, ac = b0, b0 = next live; at count 0: strict off (when live > 3), then b4 off
   (when live > 3), then the piece ends. 0 ok, -1 not ported (the touching-point tests while not strict) */
static int cell_tris_fill(const Cell *C, int npc, int **tri, int *ntri, int *cap)
{
    for (int q = 0; q < npc; q++) {
        int st = C->pt[2 * q], n = C->pt[2 * q + 1] + 1;
        const double *P = C->op + 2 * (size_t)st;
        char act[n > 0 ? n : 1];
        for (int k = 0; k < n; k++) act[k] = 1;
        int a8 = 0, ac = 1, b0 = 2, left = n, cnt = n, strict = 1, b4 = 1;
        while (2 < left) {
            cnt--;
            const double *A = P + 2 * a8, *B = P + 2 * ac, *Cc = P + 2 * b0;
            double cr = (Cc[0] - B[0]) * (A[1] - B[1]) - (A[0] - B[0]) * (Cc[1] - B[1]);
            int ear = 0;
            if ((fabs(cr) < 1e-06 && !b4 && pt_on_seg(C->op, st + ac, st + a8, st + b0)) || 0.0 < cr) {
                int blocked = 0;
                for (int it = 0, k = b0; it < n; it++) {
                    k++; if (k == n) k = 0;
                    if (k == a8) break;
                    if (act[k] == 1) {
                        int r = ear_blocked(strict, P + 2 * k, A, B, Cc);
                        if (r < 0) return -1;
                        if (r) { blocked = 1; break; }
                    }
                }
                ear = !blocked;
            }
            if (ear) {                                                         /* LAB_18057f2ee */
                if (*ntri == *cap) { *cap = *cap ? 2 * *cap : 256; *tri = realloc(*tri, sizeof(int) * 3 * (size_t)*cap); }
                (*tri)[3 * *ntri] = C->oi[st + a8]; (*tri)[3 * *ntri + 1] = C->oi[st + ac]; (*tri)[3 * *ntri + 2] = C->oi[st + b0];
                (*ntri)++;
                act[ac] = 0; left--; cnt = left; ac = b0;
                for (int it = 0; it < n; it++) { b0++; if (b0 == n) b0 = 0; if (act[b0] == 1) break; }
                strict = 1; b4 = 1;
                continue;
            }
            a8 = ac; ac = b0;                                                  /* LAB_18057f1a1 */
            for (int it = 0; it < n; it++) { b0++; if (b0 == n) b0 = 0; if (act[b0] == 1) break; }
            if (cnt == 0) {
                if (strict && 3 < left) { strict = 0; cnt = left; }
                if (cnt == 0) {
                    if (b4 && !strict && 3 < left) { b4 = 0; cnt = left; }
                    if (cnt == 0) left = 0;
                }
            }
        }
    }
    return 0;
}

/* 18057c9ec..18057d932 on a trimmed surface: the grid points (FUN_18057a510 -> eval_surf: init_surf_const has no case for kind 10, so the
   points are eval_surf's of the base, FUN_1805b08d0), then the loop points (vertex grid count + point index), then per cell (u outer, v
   inner; corners b, b + nv + 1, b + nv + 2, b + 1) FUN_1805913b0 and FUN_180592030. m->nv = the largest index used + 1 (surf_to_stl's). */
static int plt_mesh(Ctx *c, const PltTrim *T, int nu, int nv, const double *pu, const double *pv, PlMesh *m)
{
    int ng = (nu + 1) * (nv + 1), nlp = 0;
    for (int l = 0; l < T->nl; l++) nlp += T->tb[2 * l + 1] + 1;
    int tot = T->nl > 0 ? T->tb[2 * (T->nl - 1)] + T->tb[2 * (T->nl - 1) + 1] + 1 : 0;   /* local_2d48 */
    m->xyz = malloc(sizeof(double) * 3 * ((size_t)ng + nlp + 1)); m->uv = malloc(sizeof(double) * 2 * ((size_t)ng + nlp + 1));
    for (int i = 0, k = 0; i <= nu; i++) for (int j = 0; j <= nv; j++, k++) {
        if (plt_pt(c, pu[i], pv[j], m->xyz + 3 * (size_t)k)) return -1;
        m->uv[2 * (size_t)k] = pu[i]; m->uv[2 * (size_t)k + 1] = pv[j];      /* FUN_18057a510 LAB_18057a908: the uv output (make_surf_polygons(.., 1, ..)) */
    }
    for (int l = 0, k = ng; l < T->nl; l++)
        for (int q = T->tb[2 * l]; q <= T->tb[2 * l] + T->tb[2 * l + 1]; q++, k++) {
            if (plt_pt(c, PX(T->p, q), PY(T->p, q), m->xyz + 3 * (size_t)k)) return -1;
            m->uv[2 * (size_t)k] = PX(T->p, q); m->uv[2 * (size_t)k + 1] = PY(T->p, q);
        }
    Cell C; memset(&C, 0, sizeof C);
    C.p = T->p; C.tb = T->tb; C.nl = T->nl; C.base = ng;
    C.mk = calloc((size_t)tot + T->np + 1, sizeof(short));
    C.cap = tot + 4;
    C.op = malloc(sizeof(double) * 2 * (size_t)(C.cap + 1)); C.oi = malloc(sizeof(int) * (size_t)(C.cap + 1));
    C.pt = malloc(sizeof(int) * 2 * (size_t)(C.cap + 8));
    int *tri = NULL, ntri = 0, cap = 0, ret = 0;
    for (int i = 0, b = 0; i < nu && !ret; i++, b++)
        for (int j = 0; j < nv; j++, b++) {
            double R[8] = { pu[i], pv[j], pu[i + 1], pv[j], pu[i + 1], pv[j + 1], pu[i], pv[j + 1] };
            C.R = R; C.ci[0] = b; C.ci[1] = b + nv + 1; C.ci[2] = b + nv + 2; C.ci[3] = b + 1;
            int npc = cell_clip(&C, T->outer);
            if (cell_tris(&C, npc, &tri, &ntri, &cap, NULL)) { c->err = "trimmed surface mesh: an ear touching a point while not strict (FUN_180599690 / FUN_1805999f0) NOT PORTED"; ret = -1; break; }
        }
    free(C.mk); free(C.op); free(C.oi); free(C.pt);
    if (ret) { free(tri); return -1; }
    int mx = -1;
    for (int i = 0; i < 3 * ntri; i++) if (tri[i] > mx) mx = tri[i];
    m->tri = tri; m->ntri = ntri; m->nv = mx + 1;
    return 0;
}
