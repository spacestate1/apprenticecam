/* cam2d_remach.c - port of mccore.dll's pock_bound / pbmap functions that Contour type 5 "Remachining" uses. Citations: pb = decomp/pocket_b6/decomp_tspiral_pbmap.c,
 * hlp = ~/mastercam-re/work/ctour100/remach_helpers.c, cal = remach_callees.c, core = remach_core.c, more = remach_more.c, more2 = remach_more2.c,
 * sys = systypes_ints.c (systypes.dll), pock = decomp/mccore_pock.c. Checked against the real mccore.dll by tools/tests/remach_oracle.sh (tools/peloader --remach). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cam2d_remach.h"

double rm_tol = 1e-4, rm_nc_tol = 2e-3, rm_chain_tol = 2e-3;
#define PI 3.141592653589793
#define TWO_PI 6.283185307179586

static void restrict_angle(double lo, double hi, double *a) {   /* pb l.3860-3880 */
    double d = *a; if (hi < d) { do d += -TWO_PI; while (hi < d); *a = d; }
    d = *a; if (d < lo) { do d += TWO_PI; while (d < lo); *a = d; }
}
static void arc_endpoints(const double a[5], double p0[2], double p1[2]) {   /* sys arc_endpoints l.56 */
    p0[0] = cos(a[3]) * a[2] + a[0]; p0[1] = sin(a[3]) * a[2] + a[1];
    p1[0] = cos(a[3] + a[4]) * a[2] + a[0]; p1[1] = sin(a[3] + a[4]) * a[2] + a[1];
}
static void norm2(double *x, double *y) { double l = sqrt(*x * *x + *y * *y); if (l != 0) { *x /= l; *y /= l; } else { *x = *y = 0; } }   /* FUN_18000f270 */

/* ---- map ---- */
int rm_map_init(RmMap *m, double ymin, double ymax, double cell) {   /* init_pbmap pb l.626 / FUN_1805cd760 */
    m->cell = cell; m->y0 = ymin; m->nrows = (int)((ymax - ymin) / cell);
    m->rows = calloc((size_t)(m->nrows > 0 ? m->nrows : 1), sizeof(RmRow *));
    return m->rows ? 0 : -2;
}
void rm_map_free(RmMap *m) {
    if (!m->rows) return;
    for (int i = 0; i < m->nrows; i++) if (m->rows[i]) { free(m->rows[i]->p); free(m->rows[i]); }
    free(m->rows); m->rows = NULL;
}
int rm_map_copy(RmMap *d, const RmMap *s) {                          /* copy_pbmap_ext pb l.23 */
    *d = *s; d->rows = calloc((size_t)(s->nrows > 0 ? s->nrows : 1), sizeof(RmRow *));
    for (int i = 0; i < s->nrows; i++) if (s->rows[i]) {
        d->rows[i] = malloc(sizeof(RmRow)); *d->rows[i] = *s->rows[i];
        d->rows[i]->p = malloc((size_t)s->rows[i]->cap * sizeof(RmPt));
        memcpy(d->rows[i]->p, s->rows[i]->p, (size_t)s->rows[i]->n * sizeof(RmPt));   /* cap entries in the DLL; the rest unused */
    }
    return 0;
}
double rm_map_area(const RmMap *m) {                                 /* pbmap_area pb l.668 */
    double s = 0;
    for (int i = 0; i < m->nrows; i++) { RmRow *r = m->rows[i]; if (r) for (int k = 0; k < r->n - 1; k += 2) s += r->p[k + 1].x - r->p[k].x; }
    return s * m->cell;
}
static RmRow *row_new(void) { RmRow *r = calloc(1, sizeof(RmRow)); r->n = 2; r->cap = 4; r->p = calloc(4, sizeof(RmPt)); return r; }
static void row_room(RmRow *r, int want) { if (r->cap < want) { r->cap = want + 2 - (want % 2 == 0 ? 0 : 0); r->p = realloc(r->p, (size_t)r->cap * sizeof(RmPt)); memset(r->p + r->n, 0, (size_t)(r->cap - r->n) * sizeof(RmPt)); } }

/* FUN_1805cfe40 (hlp l.305-508): union of [lo, hi] into the row */
static void row_union(RmRow **pr, double lo, double hi) {
    if (!*pr) { *pr = row_new(); (*pr)->p[0].x = lo; (*pr)->p[1].x = hi; return; }
    RmRow *r = *pr; int n = r->n, i14 = -1, i20 = -1, i18 = 0, u12 = 0, u19 = 0;
    if (0 < n - 1) {
        do {
            double d4 = r->p[u12 + 1].x, d25 = r->p[u12].x - hi, d7 = d4 - lo;
            int i8 = u12;
            if ((d7 * d25 <= 0.0) || (fabs(d25) < rm_tol) || (fabs(d7) <= rm_tol && rm_tol != fabs(d7))) { i20 = i8 + 1; if (i14 < 0) i14 = i8; }
            else if (d4 < lo) u19 = i8 + 2;
            i18 = u19; u12 = i8 + 2;
        } while (u12 < n - 1);
        if (i14 >= 0) {
            r->p[i14].x = (lo <= r->p[i14].x) ? lo : r->p[i14].x;
            r->p[i20].x = (r->p[i20].x <= hi) ? hi : r->p[i20].x;
            int del = i20 - i14 - 1;
            if (del > 0) { memmove(r->p + i20 - del, r->p + i20, (size_t)(n - i20) * sizeof(RmPt)); r->n = n - del; }
            return;
        }
    }
    row_room(r, n + 2); r->n = n + 2;
    if (i18 < n - 1) memmove(r->p + i18 + 2, r->p + i18, (size_t)(n - i18) * sizeof(RmPt));
    r->p[i18].x = lo; r->p[i18 + 1].x = hi;
}

/* edges: FUN_1805cd650 (pb l.3245), FUN_1805cd2c0 (pb l.3308) */
typedef struct { unsigned char type; double ymin, ymax, a, b, c, d; } Edge;   /* line: type 1, x = a + b y; arc: type 0, a = cx, b = side, c = r^2 - cy^2, d = 2 cy */
static int cmp_dbl(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return a < b ? -1 : a > b; }
static void edge_line(const double p0[2], const double p1[2], Edge *e, int *n) {
    double y0 = p0[1], x0 = p0[0], y1 = p1[1], dy = y1 - y0;
    Edge *g = &e[*n]; g->type = 1;
    g->ymin = y0 < y1 ? y0 : y1;
    g->ymax = y1 < y0 ? y0 : y1;
    if (fabs(dy) >= 1e-12) { g->b = (p1[0] - x0) / dy; g->a = x0 - y0 * g->b; }
    else { g->b = 0; g->a = x0 - y0 * g->b; }
    (*n)++;
}
static void edge_arc(const double a[5], Edge *e, int *n) {
    double f[4]; memset(f, 0, sizeof f);
    Edge *g = &e[*n]; g->type = 0; g->a = a[0]; g->c = a[2] * a[2] - a[1] * a[1]; g->d = a[1] * 2.0;
    if (a[4] <= 0.0) { f[0] = a[3] + a[4]; f[1] = a[3]; } else { f[0] = a[3]; f[1] = a[3] + a[4]; }
    while (f[0] < 0.0) { f[0] += TWO_PI; f[1] += TWO_PI; }
    while (TWO_PI < f[0]) { f[0] -= TWO_PI; f[1] -= TWO_PI; }
    double d14 = 1.570796326794896, d15 = 4.71238898;
    int cnt = 2;
    for (int rep = 0; rep < 2; rep++) {
        if (f[0] < d14 && d14 < f[1] && cnt < 4) f[cnt++] = d14;
        if (f[0] < d15 && d15 < f[1] && cnt < 4) f[cnt++] = d15;
        d14 += TWO_PI; d15 += TWO_PI;
    }
    if (cnt > 2) qsort(f, (size_t)cnt, 8, cmp_dbl);
    for (int i = 0; i < cnt - 1; i++) {
        Edge *ee = &e[*n];
        if (i > 0) *ee = e[*n - 1];
        double y0 = sin(f[i]) * a[2] + a[1], y1 = sin(f[i + 1]) * a[2] + a[1];
        double m = cos((f[i] + f[i + 1]) * 0.5);
        ee->b = m == 0.0 ? 0.0 : (0.0 <= m ? 1.0 : -1.0);
        ee->ymin = y0 < y1 ? y0 : y1;
        ee->ymax = y0 <= y1 ? y1 : y0;
        (*n)++;
    }
}
/* FUN_1805d2610 (hlp l.513-534) */
static int row_of(const RmMap *m, double y) {
    double d = (y - m->y0) / m->cell;
    d = 0.0 <= d ? floor(d + 0.5) : ceil(d - 0.5);
    int i = (int)d; if (i < 0) i = 0;
    if (m->nrows <= i) i = m->nrows - 1;
    return i;
}
/* FUN_1805d2bd0 (hlp l.573-699) */
static void edge_cross(const Edge *e, int ne, double y, double step, double *out, int *nout) {
    double acc = 0.0;
    for (int u = 0; u < 8; u++) {
        if (0 < u) { if ((u & 1) == 0) y -= acc; else { acc += step; y += acc; } }
        *nout = 0;
        for (int i = 0; i < ne; i++) {
            if (200 <= *nout) goto done;
            if (e[i].ymin <= y && y < e[i].ymax) {
                double v = y;
                if (e[i].type == 0) { v = (e[i].d - y) * y + e[i].c; if (v < 0.0) v = 0.0; v = sqrt(v); }
                out[(*nout)++] = v * e[i].b + e[i].a;
            }
        }
done:   if (0 < *nout && (*nout & 1) == 0) { qsort(out, (size_t)*nout, 8, cmp_dbl); return; }
    }
}
/* FUN_1805d3340 (pb l.3450-3589) */
static void fill(const Edge *e, int ne, RmMap *m) {
    double mn = 1e38, mx = -1e38;
    for (int i = 0; i < ne; i++) { if (e[i].ymin <= mn) mn = e[i].ymin; if (mx <= e[i].ymax) mx = e[i].ymax; }
    int i2 = row_of(m, mn) - 1; if (i2 < 0) i2 = 0;
    int i3 = row_of(m, mx), i5 = m->nrows - 1, i10 = i3 + 1 > i5 ? i5 : i3 + 1;
    for (; i2 <= i10; i2++) {
        double y = i2 * m->cell + m->y0, x[200]; int nx = 0;
        edge_cross(e, ne, y, rm_tol, x, &nx);
        if ((nx & 1) == 0 && 0 < nx - 1) {
            for (int k = 0; k < nx - 1; k += 2) {
                double a = x[k], b = x[k + 1], lo = a < b ? a : b, hi = a <= b ? b : a;
                if (1e-05 <= hi - lo) row_union(&m->rows[i2], lo, hi);
            }
        }
    }
}
void rm_verify_line(const double l[4], double r, RmMap *m) {         /* pbmap_verify_line_ext pb l.2090-2186 */
    double X = l[0] - l[2], Y = l[1] - l[3], s2 = Y * Y + X * X, d = sqrt(s2), sa = 0.0;
    if (rm_nc_tol <= d) { if (1e-12 <= s2) { sa = atan2(Y, X); if (sa < 0.0) sa += TWO_PI; } sa -= 1.570796326794896; }
    double a1[5] = { l[0], l[1], r, sa, PI }, a2[5] = { l[2], l[3], r, sa + PI, PI };
    double a1s[2], a1e[2], a2s[2], a2e[2];
    arc_endpoints(a1, a1s, a1e); arc_endpoints(a2, a2s, a2e);
    Edge e[12]; int n = 0;
    edge_line(a1e, a2s, e, &n); edge_arc(a2, e, &n); edge_line(a2e, a1s, e, &n); edge_arc(a1, e, &n);
    fill(e, n, m);
}
/* aa_ints pb l.3888-3975 (hlp restrict): two circles, centres / radii of a, b; both intersection points; ok = 0: none */
static void aa_ints(const double a[5], const double b[5], double p1[2], double p2[2], int *ok) {
    *ok = 0;
    double dx = a[0] - b[0], dy = a[1] - b[1], d2 = dy * dy + dx * dx, d = sqrt(d2), ra = a[2], rb = b[2];
    if (rm_tol <= d) {
        if (rm_tol < fabs(ra - rb) - d) return;
        if (rm_tol < (d - ra) - rb) return;
        double h = 0.0, l = ((ra * ra - rb * rb) + d2) / (d * 2.0), w = ra * ra - l * l;
        if (0.0 <= w) h = sqrt(w);
        else if (l == 0.0) l = ra * 0.0;
        else if (0.0 <= l) l = ra * 1.0;
        else l = ra * -1.0;
        double ux = b[0] - a[0], uy = b[1] - a[1];
        norm2(&ux, &uy);
        p1[1] = uy * l + ux * h + a[1];
        p1[0] = -uy * h + ux * l + a[0];
        p2[1] = uy * l + ux * -h + a[1];
        p2[0] = -uy * -h + ux * l + a[0];
    } else {
        if (rm_tol <= fabs(ra - rb)) return;
        arc_endpoints(a, p1, p2);
    }
    *ok = 1;
}
void rm_verify_arc(const double arc[5], double R, RmMap *m) {        /* pbmap_verify_arc_ext pb l.1831-2068 */
    double sw = arc[4];
    Edge e[32]; int n = 0;
    if (fabs(sw) < 6.283184307179586 || fabs(sw) == 6.283184307179586) {
        if (3.173008580125691 <= fabs(sw) && fabs(sw) != 3.173008580125691) {
            double h[5]; memcpy(h, arc, sizeof h); h[4] = sw * 0.5;
            rm_verify_arc(h, R, m); h[3] = arc[3] + h[4]; rm_verify_arc(h, R, m);
            return;
        }
        double s[2], en[2], low[2], high[2], d2, d8;
        arc_endpoints(arc, s, en);
        if (sw <= 0.0) { d8 = arc[3]; d2 = d8 + sw; memcpy(high, s, 16); memcpy(low, en, 16); }
        else { d2 = arc[3]; d8 = d2 + sw; memcpy(low, s, 16); memcpy(high, en, 16); }
        double cap_lo[5] = { low[0], low[1], R, 0, 0 }, cap_hi[5] = { high[0], high[1], R, 0, 0 }, inner[5]; int ring = 0;
        if (R < arc[2]) {
            cap_lo[3] = d2 - PI; restrict_angle(0.0, TWO_PI, &cap_lo[3]); cap_lo[4] = PI; cap_hi[4] = PI;
            inner[0] = arc[0]; inner[1] = arc[1]; inner[2] = arc[2] - R; inner[3] = d8 - fabs(sw);
            cap_hi[3] = d8; restrict_angle(0.0, TWO_PI, &inner[3]); inner[4] = fabs(sw); ring = 1;
        } else {
            double p1[2], p2[2]; int ok;
            aa_ints(cap_lo, cap_hi, p1, p2, &ok);
            if (!ok) return;
            double md = fabs(sw) * 0.5 + d2, my = sin(md) * arc[2] + arc[1], mx = cos(md) * arc[2] + arc[0];
            double v1x = mx - p1[0], v1y = my - p1[1], d4 = sqrt(v1y * v1y + v1x * v1x), v2x = mx - p2[0], v2y = my - p2[1], d5 = sqrt(v2y * v2y + v2x * v2x);
            const double *q = d5 < d4 ? p1 : p2;
            double ex = q[0] - cap_lo[0], ey = q[1] - cap_lo[1], ang = 0.0;
            if (1e-12 <= ex * ex + ey * ey) { ang = atan2(ey, ex); if (ang < 0.0) ang += TWO_PI; }
            cap_lo[3] = ang; cap_lo[4] = d2 - ang; restrict_angle(0.0, TWO_PI, &cap_lo[4]);
            ex = q[0] - cap_hi[0]; ey = q[1] - cap_hi[1]; ang = 0.0;
            cap_hi[3] = d8;
            if (1e-12 <= ex * ex + ey * ey) { ang = atan2(ey, ex); if (ang < 0.0) ang += TWO_PI; }
            cap_hi[4] = ang - d8; restrict_angle(0.0, TWO_PI, &cap_hi[4]);
        }
        double outer[5] = { arc[0], arc[1], R + arc[2], d2, fabs(sw) };
        edge_arc(cap_lo, e, &n); edge_arc(outer, e, &n); edge_arc(cap_hi, e, &n);
        if (ring) edge_arc(inner, e, &n);
    } else {
        double outer[5] = { arc[0], arc[1], R + arc[2], 0.0, TWO_PI };
        edge_arc(outer, e, &n);
        if (R < arc[2]) { double inner[5] = { arc[0], arc[1], arc[2] - R, 0.0, TWO_PI }; edge_arc(inner, e, &n); }
    }
    fill(e, n, m);
}
/* pbmap_fill_pock_bounds -> pbmap_fill_pock_bounds_ext (pb l.1239-1365): the edges of every entity of every bound (no matter which way round, FUN_1805cd650 / FUN_1805cd2c0) into one table, then the row fill FUN_1805d3340 */
void rm_fill_bounds(const RmB *l, RmMap *m) {
    int n = 0;
    for (const RmB *b = l; b; b = b->next) n += b->n * 3;
    if (!n) return;
    Edge *e = malloc((size_t)(n + 3) * sizeof(Edge)); int ne = 0;
    for (const RmB *b = l; b; b = b->next)
        for (int i = 0; i < b->n; i++) {
            const RmEnt *x = &b->e[i];
            if (x->id == 'L') edge_line(&x->a[0], &x->a[2], e, &ne);
            else if (x->id == 'A') edge_arc(x->a, e, &ne);
        }
    fill(e, ne, m);
    free(e);
}
void rm_verify_bounds(RmB *b, double r, RmMap *m) {                  /* verify_pock_bounds pb l.2286 / verify_pock_bound pb l.4014 */
    for (; b; b = b->next) for (int i = 0; i < b->n; i++) { if (b->e[i].id == 'L') rm_verify_line(b->e[i].a, r, m); else if (b->e[i].id == 'A') rm_verify_arc(b->e[i].a, r, m); }
}

/* pbmap_diff_ext pb l.1041-1215: A minus B */
int rm_map_diff(RmMap *A, const RmMap *B, double t) {
    for (int ri = 0; ri < A->nrows; ri++) {
        RmRow *a = A->rows[ri], *b = B->rows[ri];
        if (!a || !b) continue;
        for (int bi = 0; bi < b->n - 1; bi += 2) {
            double d5 = b->p[bi].x, d6 = b->p[bi + 1].x;
            for (int i = 0; i < a->n - 1; i += 2) {
                double d3 = a->p[i].x, d4 = a->p[i + 1].x;
                if ((d3 - d6) * (d4 - d5) < 0.0) {
                    double dr = d6 - d4;
                    if (t <= d5 - d3) {
                        if (dr < -t || dr == -t) {
                            int n0 = a->n; a->n = n0 + 2;
                            if (a->cap < n0 + 2) { a->cap = n0 + 4; a->p = realloc(a->p, (size_t)a->cap * sizeof(RmPt)); }
                            for (int k = a->n - 1; k >= i + 2 + 2; k--) a->p[k] = a->p[k - 2];
                            /* the two shifted records' bytes are as the DLL leaves them (only x is ever read before the next trace) */
                            a->p[i + 1].x = d5; a->p[i + 2].x = d6; a->p[i + 3].x = d4;
                        } else a->p[i + 1].x = d5;
                    } else if (dr < -t || dr == -t) a->p[i].x = d6;
                    else {
                        a->n -= 2;
                        for (int k = i; k < a->n; k++) a->p[k] = a->p[k + 2];
                        i -= 2;
                    }
                }
            }
        }
    }
    return 0;
}

static void map_flag(RmMap *m, char v) { for (int i = 0; i < m->nrows; i++) if (m->rows[i]) for (int k = 0; k < m->rows[i]->n; k++) m->rows[i]->p[k].flag = v; }   /* FUN_1805d2850 */
static void map_used(RmMap *m, char v) { for (int i = 0; i < m->nrows; i++) if (m->rows[i]) for (int k = 0; k < m->rows[i]->n; k++) m->rows[i]->p[k].used = v; }   /* set_pbmap_used */

/* FUN_1805cedf0 (pb l.3592-3755) */
static void cedf0(RmRow *r1, RmRow *r2, double thr, int inv, RmPt **a, RmPt **b, RmPt **c, RmPt **d, char *found) {
    double d14 = -1e38; RmPt *p11 = NULL, *p48 = NULL, *p40 = NULL, *p38 = NULL;
    *found = 0;
    if (inv < r1->n - 1) {
        int i7 = inv;
        do {
            if (*found) goto hit;
            p11 = &r1->p[i7];
            if (p11->flag) {
                p48 = p11 + 1;
                if (inv < r2->n - 1) {
                    int i9 = inv;
                    do {
                        if (*found) break;
                        RmPt *q = &r2->p[i9];
                        if (q->flag) {
                            double v = (q + 1)->x - p11->x;
                            if ((p11 + 1)->x - q->x <= (q + 1)->x - p11->x) v = (p11 + 1)->x - q->x;
                            if (thr < v && d14 < 0.0 && d14 < v) { *found = 1; d14 = v; p40 = q; p38 = q + 1; }
                        }
                        i9 += 2;
                    } while (i9 < r2->n - 1);
                }
            }
            i7 += 2;
        } while (i7 < r1->n - 1);
        if (*found) {
hit:;
            RmPt *p4 = NULL, *p3 = NULL;
            int idx = p11->idx;
            int bv1 = 0;
            int l12 = idx + 2, l10 = r1->n - 1;
            if (l12 < l10) {
                for (int off = l12; off < l10; off += 2) {
                    RmPt *q = &r1->p[off];
                    if (q->flag) {
                        double v = p38->x - q->x;
                        if ((q + 1)->x - p40->x <= p38->x - q->x) v = (q + 1)->x - p40->x;
                        if (0.0 < v) { p3 = q + 1; bv1 = 1; p4 = q; }
                    }
                }
            }
            RmPt *p6 = NULL, *p8 = NULL; int bv2 = 0;
            if (bv1) {
                int i7b = p40->idx;
                if (i7b < r2->n - 3) {
                    if (r2->p[i7b + 2].flag) {
                        double d13 = (p3)->x - r2->p[i7b + 2].x, d14b = r2->p[i7b + 3].x - p4->x;
                        if (d13 <= d14b) d14b = d13;
                        if (0.0 < d14b) bv1 = 0;
                    }
                }
            } else {
                int l10b = p40->idx + 2;
                if (l10b < r2->n - 1) {
                    p4 = NULL;
                    for (int off = l10b; off < r2->n - 1; off += 2) {
                        p8 = &r2->p[off];
                        if (p8->flag) {
                            double v = p8[1].x - p11->x;
                            if (p48->x - p8->x <= p8[1].x - p11->x) v = p48->x - p8->x;
                            if (0.0 < v) { p6 = p8 + 1; bv2 = 1; p4 = p8; }
                        }
                    }
                    p8 = p6;
                    if (bv2 && idx < r1->n - 3 && r1->p[l12].flag) {   /* iVar7 is still p11's index here */
                        double v = p6->x - r1->p[l12].x, w = r1->p[idx + 3].x - p4->x;
                        if (w <= v) v = w;
                        if (0.0 < v) bv2 = 0;
                    }
                }
            }
            *a = p11; if (bv1) p48 = p3; *b = p48;
            if (bv2) p38 = p8;
            *c = p40; *d = p38;
        }
    }
}
/* FUN_1805cfd00 (pb l.3757-3820) */
static void cfd00(RmRow *r1, RmRow *r2, RmPt *a, RmPt *b, RmPt *c, RmPt *d) {
    c->next = a; a->prev = c;
    for (int i = a->idx; i < b->idx; i++) { r1->p[i].next = &r1->p[i + 1]; r1->p[i + 1].prev = &r1->p[i]; }
    b->next = d; d->prev = b;
    for (int i = c->idx + 1; i < d->idx; i += 2) { r2->p[i + 1].next = &r2->p[i]; r2->p[i].prev = &r2->p[i + 1]; }
    c->flag = 0; d->flag = 0;
    for (int i = c->idx + 1; i < d->idx; i++) r2->p[i].flag = 0;
    a->flag = 0; b->flag = 0;
    for (int i = a->idx + 1; i < b->idx; i++) r1->p[i].flag = 0;
}
/* pbmap_to_pbmap_bnds_ext pb l.1592-1809 */
int rm_map_trace(RmMap *m, double dtol, int inv, RmBnd **out) {
    for (int r = 0; r < m->nrows; r++) { RmRow *row = m->rows[r]; if (!row) continue; double y = r * m->cell + m->y0; for (int k = 0; k < row->n; k++) { row->p[k].idx = k; row->p[k].row = r; row->p[k].y = y; } }
    int start = inv != 0;
    map_flag(m, 1);
    for (int i = 0; i < m->nrows; i++) if (m->rows[i]) for (int k = 0; k < m->rows[i]->n; k++) { m->rows[i]->p[k].ring = -1; m->rows[i]->p[k].prev = NULL; m->rows[i]->p[k].next = NULL; }   /* FUN_1805d28c0 */
    *out = NULL;
    int ring = 0;
    for (int r = 0; r < m->nrows; r++) {
        RmRow *row = m->rows[r];
        if (!row) continue;
        if (r > 0 && m->rows[r - 1]) {
            RmRow *prev = m->rows[r - 1];
            for (int k = 0; k < row->n; k++) row->p[k].flag = 1;
            for (int k = 0; k < prev->n; k++) prev->p[k].flag = 1;
            RmPt *a, *b, *c, *d; char fnd;
            cedf0(row, prev, 0.0, start, &a, &b, &c, &d, &fnd);
            while (fnd) { cfd00(row, prev, a, b, c, d); cedf0(row, prev, 0.0, start, &a, &b, &c, &d, &fnd); }
            cedf0(row, prev, -dtol, start, &a, &b, &c, &d, &fnd);   /* the disassembly 0x1805d1a36 flips the sign of dtol (the second pass joins gaps < dtol) */
            while (fnd) { cfd00(row, prev, a, b, c, d); cedf0(row, prev, -dtol, start, &a, &b, &c, &d, &fnd); }
        }
        for (int k = start; k < row->n - 1; k += 2) {
            RmPt *p = &row->p[k], *q = &row->p[k + 1];
            if (!p->next || !q->next) { p->prev = q; p->next = q; p->ring = ring; q->prev = p; q->next = p; q->ring = ring; ring++; }
        }
    }
    map_flag(m, 1);
    for (int r = 0; r < m->nrows; r++) {
        RmRow *row = m->rows[r];
        if (!row) continue;
        /* one ring per row: disassembly 0x1805d1be3-0x1805d1c5e */
        for (int k = start; k < row->n - 1; k++) {
            RmPt *p = &row->p[k];
            if (!p->flag) continue;
            RmBnd *n = malloc(sizeof(RmBnd)); n->on = 1; n->first = NULL; n->next = *out; *out = n;
            for (RmPt *q = p; q && q->flag; q = q->next) q->flag = 0;
            RmPt *head = p;
            for (RmPt *q = p->prev; q && q->flag; q = q->prev) { q->flag = 0; head = q; }
            n->first = head;
            break;
        }
    }
    return 0;
}
void rm_bnds_free(RmBnd *b) { while (b) { RmBnd *n = b->next; free(b); b = n; } }
void rm_verify_bnds(RmBnd *b, double r, RmMap *src, RmMap *dst) {    /* verify_pbmap_bounds_ext pb l.2337-2408 */
    double last[2] = { 0, 0 };
    map_used(src, 0);
    for (; b; b = b->next) {
        int have = 0;
        for (RmPt *p = b->first; p; p = p->next) {
            if (!have) { last[0] = p->x; last[1] = p->y; }
            double l[4] = { last[0], last[1], p->x, p->y };
            rm_verify_line(l, r, dst);
            last[0] = p->x; last[1] = p->y; have = 1;
            if (p->used) break;
            p->used = 1;
        }
    }
}
/* FUN_1805cd130 / FUN_1805ccff0 (more l.224-272) / GetNearestLineSegPt (more l.1264) */
static void near_seg(const double l[4], const double p[2], double q[2]) {
    double x0 = l[0], dx = l[2] - x0, dy = l[3] - l[1], d = dy * dy + dx * dx;
    if (fabs(d) <= 1.1920928955078125e-07 && fabs(d) != 1.1920928955078125e-07) { q[0] = (x0 + x0) * 0.5; q[1] = (l[1] + l[1]) * 0.5; return; }
    double t = -(((l[1] - p[1]) * dy + (x0 - p[0]) * dx) / d), u = 0.0;
    if (t < 0.0 || (u = 1.0, 1.0 < t)) t = u;
    q[0] = dx * t + x0; q[1] = dy * t + l[1];
}
static RmPt *nth(RmPt *p, int k) { while (k > 0 && p) { p = p->next; k--; } return p; }
static int chord_ok(double t, int n, RmPt *c) {
    RmPt *e = nth(c, n - 1), *p = c->next;
    double l[4] = { c->x, c->y, e->x, e->y }, q[2];
    if (p != e) do {
        double pt[2] = { p->x, p->y };
        near_seg(l, pt, q);
        double vx = q[0] - p->x, vy = q[1] - p->y;
        if (t * t < vx * vx + vy * vy) return 0;
        p = p->next;
    } while (p != e);
    return 1;
}
/* FUN_1805cd1b0 (more l.273-299) */
static int is_loop(RmPt *a, RmPt *b, int n) { int i = 0; for (; a && a != b; a = a->next) i++; if (i == n) return 0; return 1; }
/* FUN_1805cce60 (hlp l.152-231): the greedy chord walk; n = node count, kept = count of kept nodes */
static void approx(double t, RmBnd *bnd, int *n, int *kept) {
    *n = 0; *kept = 0;
    RmPt *p = bnd->first;
    while (p) {
        p->used = 0;
        if (*n) {
            if (p == bnd->first) { (*n)++; break; }
            if (is_loop(bnd->first, p, *n)) break;
        }
        p = p->next; (*n)++;
    }
    RmPt *c = bnd->first;
    if (c) c->used = 1;
    (*kept)++;
    int i3 = *n, i7 = i3 - 1;
    for (;;) {
        if (i7 < 1) return;
        int i4 = 0, i5 = i3;
        do {
            int ok = chord_ok(t, i3, c), i2;
            if (!ok) { i2 = -((i3 - i4) / 2); i5 = i3; }
            else { i2 = (i5 - i3) / 2; i4 = i3; }
            i3 += i2;
        } while (i3 < i5 && i4 < i3);
        if (i4 < 2) return;   /* _MCAssert ApproxBnd: not reachable with i3 >= 2 */
        c = nth(c, i4 - 1);
        c->used = 1;
        (*kept)++;
        i7 = i7 + (1 - i4);
        i3 = i7 + 1;
    }
}
/* FUN_1805cd250 (hlp l.236-278): the kept nodes relinked */
static void relink(RmBnd *b, int n) {
    RmPt *p = b->first, *h = NULL, *t = NULL;
    for (int i = 0; i < n; i++) {
        if (p->used) { p->used = 0; if (t) t->next = p; p->prev = t; t = p; if (!h) h = p; }
        p = p->next;
    }
    if (p == b->first) { if (t && h) { t->next = h; h->prev = t; b->first = h; return; } }
    else if (t) t->next = NULL;
    b->first = h;
}
void rm_filter_bnds(RmMap *m, RmBnd *b, double tol) {                /* filter_pbmap_bnds pb l.339-371 / FUN_1805ce6e0 */
    for (; b; b = b->next) {
        double t = tol; int n, kept;
        map_used(m, 0);
        approx(t, b, &n, &kept);
        for (int k = 1; 0x7ffe < kept && k < 0x11; k++) { t *= 10.0; approx(t, b, &n, &kept); }
        relink(b, n);
    }
}
RmB *rm_b_new(int n) { RmB *b = calloc(1, sizeof(RmB)); b->n = n; b->e = calloc((size_t)(n ? n : 1), sizeof(RmEnt)); return b; }
RmB *rm_b_copy(const RmB *b) { RmB *c = rm_b_new(b->n); RmEnt *e = c->e; *c = *b; c->e = e; memcpy(e, b->e, (size_t)b->n * sizeof(RmEnt)); c->prev = c->next = NULL; return c; }
/* ------------------------------------------------------------------------------------------------ pbmap tool / toolpath (Pocket Constant Overlap Spiral, mcmill FUN_1801e8650 l.335846) */
void rm_map_set_used(RmMap *m, char v) { map_used(m, v); }                                          /* set_pbmap_used pb l.2229 */
/* pbmap_init_tool pb l.1408-1567: w[k] = half chord at k rows from the centre, dw[k] = w[k] - w[k+1], w2 = half chords of the 2r circle; step = the row stride of the coarse sum */
int rm_tool_init(double r, double cell, RmTool *t) {
    memset(t, 0, sizeof *t);
    if (!(cell < r)) { t->n = 0; return 0; }
    double q = r / cell; q = q >= 0 ? floor(q + 0.5) : ceil(q - 0.5);
    short s = (short)(int)q; t->n = s;
    if (s == 0) return 0;
    if (s < 2) t->n = 2;
    int n = t->n; t->nm1 = n - 1;
    t->w = calloc((size_t)n, sizeof(double)); t->dw = calloc((size_t)(n > 1 ? n - 1 : 1), sizeof(double));
    t->area = 0; t->step = 1;
    int k = n / 2; if (8 < k) { int m = 1; do { m *= 2; k /= 2; } while (8 < k); t->step = m; }
    for (int i = 0; i < n; i++) {
        double d = r * r - (double)i * cell * (double)i * cell;
        double v = d <= 0.0 ? 0.0 : sqrt(d);
        t->w[i] = v;
        if (i == 0) t->area = v * 2.0 + t->area;
        else { t->area = v * 4.0 + t->area; t->dw[i - 1] = t->w[i - 1] - v; }
    }
    t->area_step = 0;
    for (int i = 0, j = 0; j < n; j += t->step, i += t->step) t->area_step = (i == 0 ? t->w[0] * 2.0 : t->w[i] * 4.0) + t->area_step;
    double r2 = r * 2.0; double q2 = r2 / cell; q2 = q2 >= 0 ? floor(q2 + 0.5) : ceil(q2 - 0.5);
    int nw = (int)(short)(int)q2; if (nw < 2) nw = 2;
    t->nw = nw; t->w2 = calloc((size_t)nw, sizeof(double));
    for (int i = 0; i < nw; i++) { double v = (double)i * cell; t->w2[i] = sqrt(r2 * r2 - v * v); }
    return 0;
}
void rm_tool_free(RmTool *t) { free(t->w); free(t->dw); free(t->w2); memset(t, 0, sizeof *t); }    /* pbmap_free_tool pb l.1372 */
void rm_tp_free(RmTp *t) { while (t) { RmTp *n = t->next; free(t); t = n; } }
/* FUN_1805d2690 (Ghidra, mccore 0x1805d2690): x lies in one of the row's gaps (odd intervals) */
static char row_in_gap(const RmMap *m, double x, int row) {
    if (row < 0 || row >= m->nrows || !m->rows[row]) return 0;
    RmRow *r = m->rows[row]; int n = r->n - 1;
    for (int k = 1; k < n; k += 2) if (r->p[k].x <= x && x <= r->p[k + 1].x) return 1;
    return 0;
}
static int row_of_y(const RmMap *m, double y) {   /* FUN_1805d2610 (Ghidra, mccore 0x1805d2610) */
    double q = (y - m->y0) / m->cell; q = q >= 0 ? floor(q + 0.5) : ceil(q - 0.5);
    int i = (int)q; if (i < 0) i = 0; if (m->nrows <= i) i = m->nrows - 1; return i;
}
/* FUN_1805d2f20 pb l.2683: material left (gaps) under the circle at (x, row), every stride-th chord */
static double tool_overlap(const RmMap *m, const RmTool *t, double x, int row, int stride) {
    double sum = 0.0;
    for (int side = 0; side < 2; side++) for (int k = 0; k < t->n; k += stride) {
        if (k == 0 && side == 0) continue;
        long rr = side == 0 ? (long)row - k : (long)row + k;
        if (rr < 0 || (int)rr >= m->nrows || !m->rows[rr]) continue;
        double w = t->w[k], lo = x - w, hi = w + x; RmRow *r = m->rows[rr];
        for (int j = 1; j < r->n - 1; j += 2) {
            double a = r->p[j].x, b = r->p[j + 1].x;
            if (a <= lo) a = lo;
            if (hi <= b) b = hi;
            if (a < b) sum += b - a;
        }
    }
    return sum;
}
/* FUN_1805cf870 pb l.2616: the tool position from the chord table along the ring's next point */
static void tp_along(const RmMap *base, const RmTool *t, double x, int row, int side, double nx, int nrow, double *ox, int *orow, char *ok) {
    *ok = 0; if (t->n == 0) return;
    int k = t->nm1;
    for (int i = 0; i < t->nm1; i++) if (fabs(nx - x) < t->dw[i]) { k = i; break; }
    if (nx - x < 0.0) k = -k;
    if (side == 0) k = -k;
    if (nrow < row) k = -k;
    k += row;
    int c = base->nrows - 1; if (k <= c) c = k < 0 ? 0 : k;
    int d = abs(c - row);
    *ox = side == 0 ? x + t->w[d] : x - t->w[d];
    *orow = c;
    *ok = row_in_gap(base, *ox, c);
}
/* FUN_1805cf180 pb l.2894: the tool centre from the centroid of the gaps around the point */
static void tp_centroid(const RmMap *cur, const RmMap *base, const RmTool *t, double x, int row, double *ox, int *orow, char *ok) {
    *ok = 0; int n = t->n; if (!n) return;
    double ax = 0, sw = 0, ay = 0;
    int r0 = row - t->nw + 1; if (r0 < 0) r0 = 0;
    int r1 = row - 1 + t->nw; if (cur->nrows - 1 < r1) r1 = cur->nrows - 1;
    if (r1 < r0) return;
    for (int r = r0; r <= r1; r++) {
        RmRow *rw = cur->rows[r];
        double hw = t->w2[abs(r - row)], lo = x - hw, hi = hw + x;
        if (!rw) continue;
        double y = (double)r * cur->cell + cur->y0;
        for (int j = 1; j < rw->n - 1; j += 2) {
            double a = rw->p[j].x; if (a <= lo) a = lo;
            double b = rw->p[j + 1].x; if (hi <= b) b = hi;
            double w = b - a;
            if (0.0 < w) { sw += w; ax += (b + a) * 0.5 * w; ay += w * y; }
        }
    }
    if (!(0.0 < sw)) return;
    double yr = (double)row * cur->cell + cur->y0;
    ax = ax / sw;
    double dy = ay / sw - yr, dx = ax - x, wr = t->w[0], dl = sqrt(dy * dy + dx * dx);
    char c[2] = { 0, 0 }; int ro[2] = { 0, 0 }; double xo[2] = { 0, 0 };
    int last = cur->nrows - 1;
    for (int it = 0; it < 2; it++) {
        double yy = it == 0 ? (dy * wr) / dl + yr : ay / sw;
        int r = row_of_y(cur, yy); if (r < 0) r = 0; if (last < r) r = last;
        int ad = abs(r - row);
        if (it == 0) {
            if (n <= ad) { r = row < r ? row - 1 + n : (row - n) + 1; ad = n - 1; }
            double sg = dx == 0.0 ? 0.0 : (0.0 <= dx ? 1.0 : -1.0);
            xo[0] = sg * t->w[ad] + x; ro[0] = r; c[0] = row_in_gap(base, xo[0], r);
        } else if (ad < n) {
            double hw = t->w[ad], lo = x - hw, hi = hw + x, v = lo;
            if (lo <= ax) { v = ax; if (hi < ax) v = hi; }
            xo[1] = v; ro[1] = r; c[1] = row_in_gap(base, v, r);
        }
    }
    int pick = 0;
    if (!c[0]) { if (!c[1]) return; pick = 1; }
    else if (c[1]) { double a0 = tool_overlap(cur, t, xo[0], ro[0], 1), a1 = tool_overlap(cur, t, xo[1], ro[1], 1); pick = a0 <= a1; }
    *ox = xo[pick]; *orow = ro[pick]; *ok = 1;
}
/* FUN_1805cf610 pb l.2772: search the chord table both ways: the position with the most material */
static void tp_search(const RmMap *cur, const RmMap *base, const RmTool *t, double x, int row, int stride, double *ox, int *orow, char *ok) {
    *ok = 0; if (!t->n) return;
    double best = 0.0;
    for (int s = 0; s < 2; s++) {
        double sg = s ? -1.0 : 1.0;
        for (int d = 0; d < 2; d++) for (int k = 0; k < t->n; k += stride) {
            int r = (d == 0 ? -k : k) + row;
            double xx = sg * t->w[k] + x;
            if (row_in_gap(base, xx, r)) {
                double v = tool_overlap(cur, t, xx, r, stride);
                if (best < v) { *ox = xx; *orow = r; *ok = 1; best = v; }
            }
        }
    }
}
/* FUN_1805cfc20 pb l.3094: clears the flag of the ring points under the circle */
static void tp_clear(RmMap *m, const RmTool *t, double x, int row) {
    int r0 = row - t->n + 1; if (r0 < 0) r0 = 0;
    int r1 = row - 1 + t->n; if (m->nrows <= r1) r1 = m->nrows - 1;
    for (int r = r0; r <= r1; r++) {
        int d = abs(r - row); if (d >= t->n) continue;
        double hw = t->w[d]; RmRow *rw = m->rows[r];
        if (!rw) continue;
        for (int k = 0; k < rw->n; k++) if (rw->p[k].flag && x - hw <= rw->p[k].x && rw->p[k].x <= hw + x) rw->p[k].flag = 0;
    }
}
/* FUN_1805d1cd0 pb l.2850: from the last record back, records closer than r to this one's position (measured to their boundary point) are dropped */
static void tp_shorten(RmTp **h, double r) {
    RmTp *p = *h; if (!p) return;
    while (p->next) p = p->next;
    while (p) {
        *h = p;
        RmTp *pv = p->prev, *q = pv;
        if (pv) while (q) { double d = sqrt((p->x - q->bx) * (p->x - q->bx) + (p->y - q->by) * (p->y - q->by)); if (r <= d) break; q = q->prev; }
        if (q != pv) { if (q) q->next = p; p->prev = q; }
        p = q;
    }
}
/* pbmap_bnd_to_tp pb l.705-887 */
int rm_bnd_to_tp(RmMap *cur, RmMap *base, RmTool *t, RmBnd *b, int inv, RmTp **out, double rtol) {
    *out = NULL;
    int seen = 0, d8 = inv != 0;
    RmTp *last = NULL;
    map_flag(cur, 1);                                                   /* FUN_1805d2850(map, 1): disassembly 0x1805d0813 mov $1, %dl */
    map_used(cur, 0);
    for (RmPt *p = b->first; p; p = p->next) {
        if (p->used) seen++;
        if (1 < seen) break;
        p->used = 1;
        int idx = p->idx, row = p->row;
        RmRow *rw = cur->rows[row];
        if (p->flag || seen == 1) {
            int odd = idx & 1, hi, lo;
            if (odd != d8) { lo = idx - 1; hi = idx; } else { hi = idx + 1; lo = idx; }
            double x0 = p->x, y0 = p->y, ox = 0; int orow = 0; char ok = 0;
            RmPt *nx = p->next;
            int got = 0;
            if (nx) {
                tp_along(base, t, x0, row, odd != d8, nx->x, nx->row, &ox, &orow, &ok);
                if (ok) { double v = tool_overlap(cur, t, ox, orow, t->step); ok = t->area_step * 0.99 < v; got = ok; }
            }
            if (!got) {
                tp_centroid(cur, base, t, x0, row, &ox, &orow, &ok);
                if (!ok) {
                    for (int st = t->step; 0 < st && !ok; st /= 2) tp_search(cur, base, t, x0, row, st, &ox, &orow, &ok);
                    if (!ok) {
                        orow = row;
                        double xlo = rw->p[lo].x, xhi = rw->p[hi].x;
                        if (rtol * 2.0 <= xhi - xlo) ox = odd == d8 ? rtol + xlo : xhi - rtol;
                        else ox = (xhi + xlo) * 0.5;
                    }
                }
            }
            RmTp *rec = calloc(1, sizeof(RmTp));
            rec->x = ox; rec->y = (double)orow * cur->cell + cur->y0; rec->bx = x0; rec->by = y0; rec->prev = last; rec->next = NULL;
            if (!*out) *out = rec; else if (last) last->next = rec;
            last = rec;
            tp_clear(cur, t, ox, orow);
        }
    }
    tp_shorten(out, rtol);
    return 0;
}
/* filter_pbmap_tp pb l.378-470 */
void rm_filter_tp(RmTp **h, double t) {
    RmTp *a = *h, *n6 = a->next, *n10 = NULL, *n5;
    while ((n5 = n6) != NULL) {
        n10 = n5;
        n6 = n5->next;
        if (!n6) break;
        double ax = a->x, ay = a->y, dx = n6->x - ax, dy = n6->y - ay, L = sqrt(dx * dx + dy * dy);
        if (t < L) {
            if (L == 0.0) { dy = 0.0; dx = 0.0; } else { dx /= L; dy /= L; }
            RmTp *p = a->next; int bad = 0;
            if (p != n6) {
                do {
                    if (bad) break;
                    double ex = p->x - ax, ey = p->y - ay, l2 = sqrt(ex * ex + ey * ey), ov = l2 <= L ? 0.0 : (l2 - L) * 2.0;
                    double c = ex * dy - ey * dx, dv = fabs(c);
                    if (fabs(c) < ov || fabs(c) == ov) dv = ov;
                    if (t < dv) bad = 1;
                    p = p->next;
                } while (p != n6);
                if (bad) { a->next = n5; a = n5; n6 = n5->next; }
            }
        }
    }
    if (a->next != n10) a->next = n10;
}
/* reverse_pbmap_bnds pb l.2193 / FUN_1805d26f0 l.3979: the ring reversed in place */
void rm_reverse_bnds(RmBnd *b) {
    for (; b; b = b->next) {
        RmPt *c = b->first, *n4 = NULL, *n1, *n2;
        if (!c) continue;
        n1 = c->next; c->next = NULL;
        do {
            n2 = n1;
            if (n2) n4 = n2->next;
            c->prev = n2;
            if (n2) n2->next = c;
            b->first = c;
            n1 = n4;
            c = n2;
        } while (n2);
    }
}

void rm_b_free(RmB *b) { if (b) { free(b->e); free(b); } }
void rm_b_free_list(RmB **b) { while (*b) { RmB *n = (*b)->next; rm_b_free(*b); *b = n; } }
void rm_b_append(RmB **h, RmB *b) { b->next = NULL; if (!*h) { *h = b; b->prev = NULL; return; } RmB *t = *h; while (t->next) t = t->next; t->next = b; b->prev = t; }   /* append_pock_bounds */
static void unlink_b(RmB **h, RmB *b) { RmB *p = b->prev, *n = b->next; if (!p) *h = n; else p->next = n; if (n) n->prev = p; }   /* unlink_pock_bound */
static void link_b(RmB **h, RmB *b, RmB *at) {   /* link_pock_bound cal l.1263 (read: insert b before at) */
    RmB *p = at ? at->prev : NULL;
    b->next = at; b->prev = p;
    if (at) at->prev = b;
    if (p) p->next = b; else *h = b;
}
int rm_bnds_to_bounds(RmMap *m, RmBnd *b, RmB **out) {               /* pbmap_bnds_to_pock_bounds pb l.895-1018 */
    RmB *list = NULL;
    for (; b; b = b->next) {
        map_used(m, 0);
        int cnt = 0;
        for (RmPt *p = b->first; p; p = p->next) { if (p->used) break; p->used = 1; cnt++; }
        if ((unsigned)cnt - 2u < 0x7ffd) {
            RmB *nb = rm_b_new(cnt);
            for (int i = 0; i < cnt; i++) nb->e[i].id = 'L';
            RmPt *first = b->first; map_used(m, 0);
            RmPt *p = b->first, *prev = NULL, *last = NULL; int k = 0;
            while (p) {
                if (prev) { nb->e[k].a[0] = prev->x; nb->e[k].a[1] = prev->y; nb->e[k].a[2] = p->x; nb->e[k].a[3] = p->y; k++; }
                last = p;
                if (p->used || k == cnt - 1) break;
                p->used = 1; prev = p; p = p->next;
            }
            nb->e[k].a[0] = last->x; nb->e[k].a[1] = last->y; nb->e[k].a[2] = first->x; nb->e[k].a[3] = first->y;
            nb->prev = nb->next = NULL;
            rm_b_append(&list, nb);
        }
    }
    *out = list;
    return 0;
}

/* ---- geometry of a pock_chn ---- */
void rm_gt_ends(const RmEnt *e, double s[2], double en[2]) { if (e->id == 'A') arc_endpoints(e->a, s, en); else { s[0] = e->a[0]; s[1] = e->a[1]; en[0] = e->a[2]; en[1] = e->a[3]; } }
static double eval_gt(const RmEnt *e, double t, double *y) {          /* eval_gt_pt (hlp l.26) */
    if (e->id == 'L') { *y = t * e->a[3] + (1 - t) * e->a[1]; return (1 - t) * e->a[0] + t * e->a[2]; }
    double ang = t * e->a[4] + e->a[3]; *y = sin(ang) * e->a[2] + e->a[1]; return cos(ang) * e->a[2] + e->a[0];
}
static double length_gt(const RmEnt *e) { if (e->id == 'A') return fabs(e->a[4]) * e->a[2]; double x = e->a[2] - e->a[0], y = e->a[3] - e->a[1]; return sqrt(x * x + y * y); }   /* length_gt more2 l.117 */
static void bbox_gt(const RmEnt *e, double b[4]) {                    /* bbox_gt more2 l.3-95 */
    double s[2], en[2]; rm_gt_ends(e, s, en);
    b[0] = s[0] < en[0] ? s[0] : en[0]; b[1] = s[1] < en[1] ? s[1] : en[1];
    b[2] = s[0] <= en[0] ? en[0] : s[0]; b[3] = s[1] <= en[1] ? en[1] : s[1];
    if (e->id == 'A') {
        double a0 = e->a[3], a1 = a0 + e->a[4], hi = a1, lo = a0;
        if (a1 < a0) { hi = a0; lo = a1; }
        double f = floor(hi / 1.570796326794896), c = ceil(lo / 1.570796326794896);
        short u2 = (short)(unsigned)c;
        while ((short)u2 <= (short)(int)f) {
            double t = (double)(int)u2 * 1.570796326794896;
            if (1e-06 < fabs(t - lo) && 1e-06 < fabs(t - hi)) {
                double x = cos(t) * e->a[2] + e->a[0], y = sin(t) * e->a[2] + e->a[1];
                if (x <= b[0]) b[0] = x;
                if (y <= b[1]) b[1] = y;
                if (b[2] <= x) b[2] = x;
                if (b[3] <= y) b[3] = y;
            }
            u2 = (short)u2 + 1;
        }
    }
}
static int on_angle(const double a[5], double t) {                    /* FUN_1805e2070 (more l.427) */
    double d1 = a[3], d3 = a[4] + d1, d2 = d1;
    if (a[4] <= 0.0 && a[4] != 0.0) { d2 = d3; d3 = d1; }
    for (; d2 + TWO_PI < t; t -= TWO_PI) { }
    for (; t < d2; t += TWO_PI) { }
    return d2 <= t && t <= d3;
}
/* FUN_1805e41e0 (hlp l.1396-1490): the lower / upper y points of an entity */
static void ext_y(const RmEnt *e, double lo[2], double hi[2]) {
    double pts[5][2]; int ns = 2;
    rm_gt_ends(e, pts[0], pts[1]);
    if (e->id == 'A') {
        double d3 = -TWO_PI; int bv1 = 0, bv2 = 0;
        for (int k = 0; k < 3; k++) {
            if (!bv1 && on_angle(e->a, d3 + 1.570796326794896)) { pts[ns][0] = e->a[0]; pts[ns][1] = e->a[1] + e->a[2]; ns++; bv1 = 1; }
            if (!bv2 && on_angle(e->a, d3 + 4.712388980384689)) { pts[ns][0] = e->a[0]; pts[ns][1] = e->a[1] - e->a[2]; ns++; bv2 = 1; }
            d3 += TWO_PI;
        }
    }
    memcpy(lo, pts[0], 16); memcpy(hi, pts[0], 16);
    for (int i = 1; i < ns; i++) {
        if (pts[i][1] < lo[1]) memcpy(lo, pts[i], 16);
        if (hi[1] <= pts[i][1] && pts[i][1] != hi[1]) memcpy(hi, pts[i], 16);
    }
}
/* pock_ccw (pock l.2419-2726) */
static double poly_term(double acc, const double p0[2], const double p1[2]) { return (p0[1] + p1[1]) * (p1[0] - p0[0]) * 0.5 + acc; }   /* FUN_1805e2040 */
static int pt_eq_tol(const double a[2], const double b[2]) {          /* Equal(p, p) mccore (more l.1154) */
    double dx = b[0] - a[0], dy = b[1] - a[1];
    return dx <= rm_tol && (-rm_tol < dx || -rm_tol == dx) && dy <= rm_tol && (-rm_tol < dy || -rm_tol == dy) && dy * dy + dx * dx < rm_tol * rm_tol;
}
static char pock_ccw(const RmB *b, double *area) {
    int n = b->n, done_area = 0; double a = 0.0;
    *area = 0.0;
    if (n < 3) {
        double sum = 0.0; int allarc = 0 < n, bv = 0;
        for (int i = 0; i < n && allarc; i++) {
            if (b->e[i].id != 'A') { allarc = 0; break; }
            if (i < 1) bv = 1;
            else { double d = b->e[i].a[2] - b->e[0].a[2]; if ((rm_nc_tol <= fabs(d) && fabs(d) != rm_nc_tol) || !bv) { allarc = 0; break; } }
            sum += b->e[i].a[4];
        }
        if (allarc && fabs(fabs(sum) - TWO_PI) <= 1e-06 && fabs(fabs(sum) - TWO_PI) != 1e-06) {
            double s = sum == 0.0 ? 0.0 : (0.0 <= sum ? 1.0 : -1.0);
            *area = b->e[0].a[2] * b->e[0].a[2] * s * -PI;
            done_area = 1;
        }
    }
    if (!done_area) {
        for (int i = 0; i < n; i++) {
            const RmEnt *e = &b->e[i];
            if (e->id == 'A') {
                double r = e->a[2], sa = e->a[3], sw = e->a[4], cy = e->a[1];
                if (rm_nc_tol < r) {
                    double f[4]; f[0] = sa; double d8 = sw + sa; f[1] = d8;
                    double d6 = floor(sa / PI), d7 = floor(d8 / PI); int ns = 2;
                    if (0.5 <= fabs(d6 - d7) && fabs(d6 - d7) != 0.5) {
                        double m = (d6 <= d7 ? d7 : d6) * PI;
                        ns = 2;
                        if ((1e-06 <= fabs(m - sa) && fabs(m - sa) != 1e-06) && (1e-06 <= fabs(m - d8) && fabs(m - d8) != 1e-06)) { ns = 4; f[1] = m; f[2] = m; f[3] = d8; }
                    }
                    for (int k = 0; k < ns; k += 2) {
                        double d5 = f[k + 1], d6b = f[k];
                        double d7b = floor((d5 + d6b) * 0.5 / PI);
                        double u = ((int)d7b & 1) == 0 ? 1.0 : -1.0;
                        double v = fabs((d5 - d6b) * 0.5 - (sin(d5 * 2.0) - sin(d6b * 2.0)) * 0.25) * r * r;
                        double w = (cos(d5) - cos(d6b)) * r;
                        double s = w == 0.0 ? 0.0 : (0.0 <= w ? 1.0 : -1.0);
                        a = s * u * v + w * cy + a;
                    }
                }
            } else if (e->id == 'L') a = poly_term(a, e->a, e->a + 2);
        }
        *area = a;
        if (0 < n) {
            double s0[2], s1[2], en0[2], en1[2];
            rm_gt_ends(&b->e[0], s0, en0); rm_gt_ends(&b->e[n - 1], s1, en1);
            if (!pt_eq_tol(s0, en1)) { *area = poly_term(*area, en1, s0); }
        }
    }
    return *area < 0.0;
}
void rm_b_info(RmB *b) {                                              /* build_pock_bound_info (core l.742-812) */
    b->len = 0.0;
    for (int i = 0; i < b->n; i++) {
        double lo[2], hi[2]; RmEnt *e = &b->e[i];
        ext_y(e, lo, hi);
        if ((lo[1] <= b->bbox[1] && b->bbox[1] != lo[1]) || i == 0) { b->bbox[0] = lo[0]; b->bbox[1] = lo[1]; }
        if ((b->bbox[3] <= hi[1] && hi[1] != b->bbox[3]) || i == 0) { b->bbox[2] = hi[0]; b->bbox[3] = hi[1]; }
        b->len += length_gt(e);
        bbox_gt(e, e->box);
    }
    double a; b->ccw = (unsigned char)pock_ccw(b, &a);
}
void rm_b_infos(RmB *b) { for (; b; b = b->next) rm_b_info(b); }
void rm_b_bbox(RmB *b) {                                              /* pock_bound_bbox (core l.989-1056) */
    for (int i = 0; i < b->n; i++) {
        double d1 = b->e[i].box[0], d2 = b->e[i].box[1], d3 = b->e[i].box[2], d4 = b->e[i].box[3];
        if (i == 0 || d1 < b->bbox[0]) b->bbox[0] = d1;
        if (i == 0 || d2 < b->bbox[1]) b->bbox[1] = d2;
        if (i == 0 || (b->bbox[2] <= d3 && d3 != b->bbox[2])) b->bbox[2] = d3;
        if (i == 0 || (b->bbox[3] <= d4 && d4 != b->bbox[3])) b->bbox[3] = d4;
    }
}
void rm_b_bboxs(RmB *b) { for (; b; b = b->next) rm_b_bbox(b); }

/* ---- intersections (systypes.dll, called by mccore's ints_on_gt_new hlp l.96) ---- */
static int pt_on_arc(const double p[2], const double a[5], double t) { /* pt_on_arc sys l.91 */
    double d5 = p[1], d1 = p[0], r = a[2], d4 = a[0] - d1, d6 = a[1] - d5;
    d4 = sqrt(d4 * d4 + d6 * d6);
    if (!(fabs(d4 - r) <= t)) return 0;
    d1 = d1 - a[0]; d5 = d5 - a[1];
    if (1e-12 <= d5 * d5 + d1 * d1) { d5 = atan2(d5, d1); if (d5 < 0.0) d5 += TWO_PI; } else d5 = 0.0;
    double d4b = 0.0, d6b, d7;
    if (a[4] <= 0.0) { d7 = a[3]; d6b = d7 + a[4]; } else { d6b = a[3]; d7 = d6b + a[4]; }
    if (1e-12 < r) d4b = t / r;
    for (; d7 + d4b < d5; d5 += -TWO_PI) { }
    for (; d5 < d6b - d4b; d5 += TWO_PI) { }
    return !(d5 < d6b - d4b || d7 + d4b < d5);
}
static int dist_to_finite_line(const double p[2], const double l[4], double t, double *dist) {   /* dist_to_finite_line more l.1175 */
    double d6 = l[1], d7 = l[0], d3 = l[2] - d7, d4 = l[3] - d6, d5 = sqrt(d3 * d3 + d4 * d4), dd;
    if (rm_tol <= d5) {
        double d1 = p[0], d2 = p[1], d8 = fabs((d1 - d7) * d4 - (d2 - d6) * d3) / d5;
        *dist = d8;
        if (t <= d8) return 0;
        double u = ((d1 - d7) * d3 + (d2 - d6) * d4) / d5;
        if (0.0 <= u) {
            if (u <= d5) return 1;
            if (d5 + t <= u) return 0;
            dd = (l[3] - p[1]) * (l[3] - p[1]) + (l[2] - p[0]) * (l[2] - p[0]);
        } else {
            if (u <= -t) return 0;
            dd = (d6 - p[1]) * (d6 - p[1]) + (d7 - p[0]) * (d7 - p[0]);
        }
    } else dd = (d6 - p[1]) * (d6 - p[1]) + (d7 - p[0]) * (d7 - p[0]);
    dd = sqrt(dd); *dist = dd;
    return dd < t;
}
static int l_hv_ints(int k, const double *l1, const double *l2, double p[2], double *s, double *u) {   /* l_hv_ints sys l.271-345 */
    const double *pl3 = l1 + 2, *pl4 = l1;
    if (k == 2) { pl3 = l1 + 3; pl4 = l1 + 1; }
    double d1 = *pl3, d2 = *pl4;
    if (1e-12 <= fabs(d1 - d2)) {
        double d7, d6;
        if (k == 1) d7 = l2[3] - l2[1]; else d7 = l2[2] - l2[0];
        if (1e-12 <= fabs(d7)) {
            double a = k == 2 ? l2[3] - l2[1] : l2[2] - l2[0];
            d6 = sqrt(a * a + d7 * d7);
            if (1e-12 <= d6 && 1.5e-10 <= fabs(d7 / d6)) {
                const double *a3 = k == 1 ? l2 + 1 : l2, *a4 = k == 1 ? l1 + 1 : l1;
                double v = (*a4 - *a3) / d7;
                *u = v;
                p[0] = (l2[2] - l2[0]) * v + l2[0];
                p[1] = (l2[3] - l2[1]) * *u + l2[1];
                *s = ((k == 2 ? p[1] : p[0]) - (k == 2 ? l1[1] : l1[0])) / (d1 - d2);
                return 1;
            }
        }
    }
    return 0;
}
/* l_ints_tol sys l.382-535; the collinear-overlap branch's t_on_line_tol is only reached with |det| below tol: those parameters are never read here (ints_on_gt_new
   uses the point, the two pt_on_gt_new tests decide) */
static int l_ints_tol(const double l1[4], const double l2[4], double t, double p[2]) {
    double d21 = l1[2] - l1[0], d20, d18, d6, d1, d19, d2, d3, d7, d17, s, u;
    if (!(1e-12 < fabs(d21) || fabs(d21) == 1e-12)) return l_hv_ints(2, l1, l2, p, &s, &u);
    d1 = l1[1]; d20 = l1[3] - d1;
    if (!(1e-12 < fabs(d20) || fabs(d20) == 1e-12)) return l_hv_ints(1, l1, l2, p, &s, &u);
    d19 = l2[0]; d2 = l2[2]; d18 = d2 - d19;
    if (!(1e-12 < fabs(d18) || fabs(d18) == 1e-12)) return l_hv_ints(2, l2, l1, p, &u, &s);
    d18 = l2[1]; d3 = l2[3]; d6 = d3 - d18;
    if (!(1e-12 < fabs(d6) || fabs(d6) == 1e-12)) return l_hv_ints(1, l2, l1, p, &u, &s);
    d6 = d19 - l1[0];
    d17 = t * t;
    d19 = d19 - d2;
    d3 = d18 - d3;
    d18 = d18 - d1;
    d2 = d3 * d21 - d19 * d20;
    d1 = d6 * d3 - d18 * d19;
    d7 = d19 * d21 + d3 * d20;
    int b15, b16;
    if (d17 < fabs(d7) || d17 == fabs(d7)) { b16 = t == fabs(d2 / d7); b15 = t < fabs(d2 / d7); }
    else { b16 = d17 == fabs(d2); b15 = d17 < fabs(d2); }
    if (b15 || b16) {
        d1 = d1 / d2;
        p[0] = l1[0] + d21 * d1;
        p[1] = d20 * d1 + l1[1];
        return 1;
    }
    d21 = sqrt(d19 * d19 + d3 * d3);
    if (t < fabs(d21)) {
        d1 = d1 / d21;
        if (fabs(d1) <= t && t != fabs(d1)) {
            /* parallel, within tolerance: the midpoint of the nearest end pair (sys l.487-507) */
            double e1[4] = { l1[0], l1[1], l1[2], l1[3] }, e2[4] = { l2[0], l2[1], l2[2], l2[3] }, best = 1e38;
            int s11 = 1, s10 = 1;
            for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
                double d = sqrt((e2[2 * b + 1] - e1[2 * a + 1]) * (e2[2 * b + 1] - e1[2 * a + 1]) + (e2[2 * b] - e1[2 * a]) * (e2[2 * b] - e1[2 * a]));
                if (d <= best && best != d) { best = d; s10 = b; s11 = a; }
            }
            p[0] = (e2[2 * s10] + e1[2 * s11]) * 0.5;
            p[1] = (e2[2 * s10 + 1] + e1[2 * s11 + 1]) * 0.5;
            return 1;
        }
    }
    return 0;
}
static int la_ints_new(const double l[4], const double a[5], double t, double p1[2], double p2[2]) {  /* la_ints_new sys l.631-700 */
    double d6 = l[1], d1 = l[0], d8 = l[3] - d6, d7 = l[2] - d1, d5 = d8 * d8 + d7 * d7, d4 = 0.0;
    if (d5 == 0.0) return 0;
    d5 = ((a[1] - d6) * d8 + (a[0] - d1) * d7) / d5;
    d7 = d1 * (1.0 - d5) + d5 * l[2];
    d1 = d6 * (1.0 - d5) + d5 * l[3];
    d6 = a[2];
    d5 = d6 * d6 - ((d1 - a[1]) * (d1 - a[1]) + (d7 - a[0]) * (d7 - a[0]));
    if (!(-(d6 * 2.0 * t) <= d5)) return 0;
    double h = 0.0;
    if (0.0 <= d5) h = sqrt(d5);
    double dx = l[2] - l[0], dy = l[3] - l[1], len = sqrt(dx * dx + dy * dy);
    if (len == 0.0) { dx = 0.0; d4 = 0.0; } else { dx = dx / len; d4 = dy / len; }
    p1[0] = d7 + dx * h; p1[1] = d4 * h + d1;
    p2[0] = d7 + dx * -h; p2[1] = d4 * -h + d1;
    return 1;
}
/* aa_ints_new (SysTypes.dll 0x180001fb0): two circles; concentric within t and of equal radius: the ends of the first arc */
static int aa_ints_new(const double a[5], const double b[5], double t, double p1[2], double p2[2]) {
    double cx0 = b[0], cy0 = b[1], r2 = b[2], cx = a[0], cy = a[1], r1 = a[2], dx = cx - cx0, dy = cy - cy0, d2 = dy * dy + dx * dx, d = sqrt(d2);
    if (t <= d) {
        if (t < fabs(r1 - r2) - d || t < (d - r1) - r2) return 0;
        double h2, h = 0.0, r, u = 0.0, m = ((r1 * r1 - r2 * r2) + d2) / (d * 2.0);
        h2 = r1 * r1 - m * m;
        if (0.0 <= h2) h = sqrt(h2);
        else { if (m == 0.0) m = r1 * 0.0; else if (0.0 <= m) m = r1 * 1.0; else m = r1 * -1.0; }
        double vx = cx0 - cx, vy = cy0 - cy;
        r = sqrt(vx * vx + vy * vy);
        if (r == 0.0) vx = 0.0; else { vx = vx / r; u = vy / r; }
        p1[0] = -u * h + vx * m + cx; p1[1] = u * m + vx * h + cy;
        p2[0] = -u * -h + vx * m + cx; p2[1] = u * m + vx * -h + cy;
        return 1;
    }
    if (t <= fabs(r1 - r2)) return 0;
    arc_endpoints(a, p1, p2);
    return 1;
}
/* ints_gt sys l.150-238, hint point (0, 0) (mccore passes v_z); only Line / Arc / Point entities reach it: bounds never hold 'P' here */
static int ints_gt(const RmEnt *a, const RmEnt *b, double t, double r[4]) {
    int k = (a->id == 'A' ? 2 : 0) + (b->id == 'A' ? 1 : 0), ok = 0;
    double p1[2] = { 0, 0 }, p2[2] = { 0, 0 };
    if (k == 0) { if (l_ints_tol(a->a, b->a, t, r)) { r[2] = r[0]; r[3] = r[1]; return 1; } return 0; }
    if (k == 1) ok = la_ints_new(a->a, b->a, t, p1, p2);
    else if (k == 2) ok = la_ints_new(b->a, a->a, t, p1, p2);
    else ok = aa_ints_new(a->a, b->a, t, p1, p2);
    if (ok) {
        double d9 = p1[0], d8 = p2[0], d6 = p1[1], d7 = p2[1];
        if (d8 * d8 + d7 * d7 <= d9 * d9 + d6 * d6) { r[0] = p2[0]; r[1] = p2[1]; r[2] = p1[0]; r[3] = p1[1]; }
        else { r[0] = p1[0]; r[1] = p1[1]; r[2] = p2[0]; r[3] = p2[1]; }
    }
    return ok;
}
static int pt_on_gt_new(const double p[2], const RmEnt *e, double t) {   /* pt_on_gt_new more l.194 */
    double d;
    if (e->id == 'L') return dist_to_finite_line(p, e->a, t, &d);
    return pt_on_arc(p, e->a, t);
}
/* ints_on_gt_new hlp l.96-148: the points of ints_gt that lie on both entities */
static int ints_on_gt(const RmEnt *a, const RmEnt *b, double out[4], double t) {
    double r[4];
    if (!ints_gt(a, b, t, r)) return 0;
    int cnt = 0;
    int np = (a->id == 'A' || b->id == 'A') ? 2 : 1;
    for (int i = 0; i < np; i++)
        if (pt_on_gt_new(r + 2 * i, a, t) && pt_on_gt_new(r + 2 * i, b, t)) { out[2 * cnt] = r[2 * i]; out[2 * cnt + 1] = r[2 * i + 1]; cnt++; }
    return cnt;
}
/* t_on_gt (hlp l.75) */
static double t_on_gt(const double p[2], const RmEnt *e) {
    if (e->id == 'L') {                                             /* t_on_line more l.168 */
        double dx = e->a[2] - e->a[0], dy = e->a[3] - e->a[1], d = dy * dy + dx * dx;
        if (rm_tol * rm_tol < d) return (dy * (p[1] - e->a[1]) + dx * (p[0] - e->a[0])) / d;
        return 0.0;
    }
    if (e->id != 'A') return 0.0;
    /* t_on_arc more l.102 */
    if (fabs(e->a[4]) < 1e-06) return 0.0;
    double d1 = p[0] - e->a[0], d3 = p[1] - e->a[1];
    if (1e-12 <= d3 * d3 + d1 * d1) { d1 = atan2(d3, d1); if (d1 < 0.0) d1 += TWO_PI; } else d1 = 0.0;
    double t = (d1 - e->a[3]) / e->a[4], out = t;
    if (t < 0.0 || 1.0 < t) {
        double d4 = ((d1 + TWO_PI) - e->a[3]) / e->a[4];
        if (0.0 <= d4 && d4 <= 1.0) return d4;
        double d = ((d1 - TWO_PI) - e->a[3]) / e->a[4];
        if (d < 0.0 || 1.0 < d) {
            double x = fabs(1.0 - t); if (fabs(t) < x) x = fabs(t);
            double y = fabs(1.0 - d4); if (fabs(d4) < y) y = fabs(d4);
            if (y < x) { out = d4; x = y; }
            double z = fabs(1.0 - d); if (fabs(d) < z) z = fabs(d);
            if (x <= z) return out;
        }
        out = d;
    }
    return out;
}
/* intersect_pock_bound more l.454-655 with p3 = 0 / 1 (p3: renumbers the entities +0; only the IsContourNested helper uses 1) */
typedef struct { double p[2]; int ra, rb; } Ins;
static void intersect(const RmB *a, const RmB *b, int renum, int maxn, Ins *out, short *pn) {
    *pn = 0;
    if (renum) { for (int i = 0; i < a->n; i++) ((RmB *)a)->e[i].rec = i; for (int i = 0; i < b->n; i++) ((RmB *)b)->e[i].rec = a->n + i; }
    for (int i = 0; i < a->n; i++) {
        const RmEnt *ea = &a->e[i]; if (ea->id == 'P') continue;
        for (int j = 0; j < b->n; j++) {
            const RmEnt *eb = &b->e[j]; if (eb->id == 'P') continue;
            if (!(eb->box[0] <= rm_nc_tol + ea->box[2] && rm_nc_tol + ea->box[2] != eb->box[0])) continue;
            if (!(eb->box[1] <= rm_nc_tol + ea->box[3] && rm_nc_tol + ea->box[3] != eb->box[1])) continue;
            if (!(ea->box[0] <= rm_nc_tol + eb->box[2] && rm_nc_tol + eb->box[2] != ea->box[0])) continue;
            if (!(ea->box[1] <= rm_nc_tol + eb->box[3] && rm_nc_tol + eb->box[3] != ea->box[1])) continue;
            double ip[4]; int np = ints_on_gt(ea, eb, ip, rm_nc_tol);
            if (np > 0 && *pn + np <= maxn) {
                for (int k = 0; k < np; k++) {
                    double d22 = ip[2 * k], d4 = ip[2 * k + 1]; int dup = 0;
                    for (int m = 0; m < *pn; m++) {
                        double d5 = sqrt((d22 - out[m].p[0]) * (d22 - out[m].p[0]) + (d4 - out[m].p[1]) * (d4 - out[m].p[1]));
                        if (d5 <= rm_tol && rm_tol != d5) { dup = 1; break; }
                    }
                    if (dup) continue;
                    out[*pn].p[0] = d22; out[*pn].p[1] = d4; out[*pn].ra = ea->rec; out[*pn].rb = eb->rec; (*pn)++;
                }
            }
        }
    }
}
static int pt_in_box(const RmB *b, const double p[2]) { return b->bbox[0] <= p[0] && p[0] <= b->bbox[2] && b->bbox[1] <= p[1] && p[1] <= b->bbox[3]; }   /* FUN_1805e7040 */

/* pt_in_ctour more l.664-704 / MCCEPtInBnd (decomp/mcce_all.c l.13851-13888, flags 6) */
int rm_pt_in_ctour(const RmB *b, double x, double y, double nc, double t) {
    for (int i = 0; i < b->n; i++) {
        double s[2], en[2]; rm_gt_ends(&b->e[i], s, en);
        if (sqrt((x - s[0]) * (x - s[0]) + (y - s[1]) * (y - s[1])) < nc) return 0;
    }
    McceShape sh; McceEnt *e = malloc((size_t)(b->n ? b->n : 1) * sizeof(McceEnt));
    memset(e, 0, (size_t)(b->n ? b->n : 1) * sizeof(McceEnt));
    for (int i = 0; i < b->n; i++) { e[i].id = b->e[i].id; memcpy(e[i].a, b->e[i].a, sizeof e[i].a); memcpy(e[i].box, b->e[i].box, sizeof e[i].box); }
    sh.e = e; sh.n = b->n; sh.dir = 1; sh.isl = 0;
    int r = mcce_pt_in_shape(x, y, &sh, t);
    free(e);
    return r == 1 ? 0 : r == 2 ? 1 : -1;
}
/* pock_bound_is_point: Remachining bounds have no 'P' entity (lines / arcs only) */
static int is_point(const RmB *b) { return b->n < 2 && b->e[0].id == 'P'; }
/* FUN_1805e7080 (hlp l.1494-1527) */
static int inside_rest(const RmB *rest, const double p[2], RmB **hit) {
    int res = -1;
    for (RmB *b = (RmB *)rest; b; b = b->next) {
        if (pt_in_box(b, p) && b->isl == 0 && rm_pt_in_ctour(b, p[0], p[1], rm_nc_tol, rm_tol) == 1) {
            int in_isl = 0;
            for (RmB *b2 = (RmB *)rest; b2; b2 = b2->next)
                if (b2 != b && b2->isl && b2->gid == b->gid && pt_in_box(b2, p) && rm_pt_in_ctour(b2, p[0], p[1], rm_nc_tol, rm_tol) == 1) in_isl = 1;
            if (!in_isl) { res = 1; *hit = b; }
        }
    }
    return res;
}
/* FUN_1805e4430 (cal l.1810-1860): the piece holding record idx whose hit is nearest t = 0.5 */
static int find_piece(const Ins *ins, RmB *list, RmB **piece, int *ent) {
    *piece = NULL; *ent = -1;
    double best = 10.0;
    for (RmB *b = list; b; b = b->next)
        for (int i = 0; i < b->n; i++)
            if (ins->ra == b->e[i].rec) {
                double d = fabs(0.5 - t_on_gt(ins->p, &b->e[i]));
                if (d <= best && best != d) { *piece = b; *ent = i; best = d; }
            }
    return *ent >= 0 ? 0 : -1;
}
/* get_ep */
static void trim_gt(RmEnt *e, const double p[2], int keep_start, int *ok) {   /* trim_gt_to_pt more l.301-400 */
    if (e->id == 'L') {
        double d1 = e->a[2], d9 = e->a[0], d10 = e->a[3], d2 = e->a[1];
        if (keep_start == 0) { e->a[0] = p[0]; e->a[1] = p[1]; } else { e->a[2] = p[0]; e->a[3] = p[1]; }
        *ok = 0.0 < (e->a[3] - e->a[1]) * (d10 - d2) + (d1 - d9) * (e->a[2] - e->a[0]);
        return;
    }
    if (e->id == 'A') {
        double d1 = p[0] - e->a[0], d9 = p[1] - e->a[1], ang = 0.0;
        if (1e-12 <= d9 * d9 + d1 * d1) { ang = atan2(d9, d1); if (ang < 0.0) ang += TWO_PI; }
        double d9b = e->a[3], res8 = ang;
        if (keep_start == 0) { res8 = e->a[4] + d9b; d9b = ang; }
        res8 = res8 - d9b;
        double lo = 0.0 <= e->a[4] ? 0.0 : -TWO_PI, hi = 0.0 <= e->a[4] ? TWO_PI : 0.0;
        restrict_angle(lo, hi, &res8);
        if ((fabs(res8) <= 6.195918844579867 && fabs(res8) != 6.195918844579867) && (fabs(e->a[4]) <= 6.195918844579867 && fabs(e->a[4]) != 6.195918844579867)) { e->a[3] = d9b; e->a[4] = res8; *ok = 1; return; }
        double d1b = e->a[2] * res8, d10b = fabs(e->a[2] * e->a[4]) + rm_nc_tol;
        if (fabs(d1b) <= d10b && d10b != fabs(d1b)) { e->a[3] = d9b; e->a[4] = res8; *ok = 1; return; }
    }
    *ok = 0;
}
/* split_pock_chn hlp l.970-1026 */
static void split_chn(const RmEnt *e, const double p[2], double t, RmEnt out[2], char ok[2]) {
    double ends[2][2];
    { double s[2], en[2]; rm_gt_ends(e, s, en); memcpy(ends[0], s, 16); memcpy(ends[1], en, 16); }
    for (int i = 0; i < 2; i++) {
        double d1 = sqrt((p[0] - ends[i][0]) * (p[0] - ends[i][0]) + (p[1] - ends[i][1]) * (p[1] - ends[i][1]));
        ok[i] = t < d1;
        if (t < d1) { out[i] = *e; int k; trim_gt(&out[i], p, i == 0, &k); ok[i] = (char)k; }
    }
}
static int is_closed(const RmB *b, double t) {                       /* IsBndClosed (hlp l.1139) */
    double s[2], en[2], s2[2], en2[2]; rm_gt_ends(&b->e[0], s, en); rm_gt_ends(&b->e[b->n - 1], s2, en2);
    return sqrt((s[0] - en2[0]) * (s[0] - en2[0]) + (s[1] - en2[1]) * (s[1] - en2[1])) < t;
}
static void reorder(RmB *b, int k) {                                 /* reorder_pock_bound hlp l.1049-1135 */
    if (k == 0) return;
    RmEnt *t = malloc((size_t)b->n * sizeof(RmEnt)); memcpy(t, b->e, (size_t)b->n * sizeof(RmEnt));
    for (int i = 0; i < b->n; i++) { int j = k + i; if (b->n <= j) j -= b->n; b->e[i] = t[j]; }
    free(t);
}
static RmB *b_alloc_like(int n) { RmB *r = rm_b_new(n); return r; }
/* break_pock_bound FUN_1805de250 (cal l.1298-1597) */
static void break_bound(RmB **list, RmB *pb, int k, const double p[2]) {
    double t = rm_nc_tol < 1.1920928955078125e-07 ? rm_nc_tol : 1.1920928955078125e-07;
    RmEnt a[2]; char o[2];
    split_chn(&pb->e[k], p, t, a, o);
    if (o[0]) { bbox_gt(&a[0], a[0].box); } if (o[1]) { bbox_gt(&a[1], a[1].box); }
    int iv15 = o[0] ? k + 1 : k, iv12 = o[1] ? pb->n - k : pb->n - k - 1;
    if (iv15 == 0 || iv12 == 0) { pb->broken = 1; return; }
    if (pb->broken != 0 || !is_closed(pb, rm_nc_tol)) {
        RmB *n1 = b_alloc_like(iv15), *n2 = b_alloc_like(iv12);
        for (int i = 0; i < k; i++) n1->e[i] = pb->e[i];
        if (o[0]) n1->e[k] = a[0];
        n1->isl = pb->isl; n1->n = iv15; n1->par = pb->par; n1->broken = 1;
        if (o[1]) n2->e[0] = a[1];
        int off = o[1] ? 1 : 0;
        for (int i = k + 1; i < pb->n; i++) n2->e[off++] = pb->e[i];
        n2->isl = pb->isl; n2->n = iv12; n2->broken = 1; n2->par = pb->par;
        link_b(list, n2, pb);   /* link_pock_bound(list, n2, pb) */
        link_b(list, n1, n2);
        unlink_b(list, pb); rm_b_free(pb);
        return;
    }
    RmB *pb2 = pb;
    if (o[0]) {
        if (!o[1]) { k = k + 1; if (pb->n <= k) k = 0; }
        else {
            RmB *r = rm_b_new(pb->n + 1);
            { RmEnt *e = r->e; *r = *pb; r->e = e; r->n = pb->n + 1; }
            for (int i = 0; i < k; i++) r->e[i] = pb->e[i];
            r->e[k] = a[0]; r->e[k + 1] = a[1];
            for (int i = k + 1; i < pb->n; i++) r->e[i + 1] = pb->e[i];
            link_b(list, r, pb);
            unlink_b(list, pb); rm_b_free(pb);
            pb2 = r; k = k + 1;
        }
    }
    reorder(pb2, k);
    pb2->broken = 1;
}
/* FUN_1805e5490 cal l.1151-1178 */
static int piece_in_rest(const RmB *rest, const RmB *piece, RmB **hit) {
    int in = 0, outc = 0, i = 0;
    if (0 < piece->n) {
        do {
            double y, x = eval_gt(&piece->e[i], 0.5, &y), p[2] = { x, y };
            int r = inside_rest(rest, p, hit);
            if (r == 1) in++; else if (r == -1) outc++;
        } while (in < 3 && outc < 3 && (i++, i < piece->n));
    }
    return outc < in;
}
static int disjoint(const RmB *a, const RmB *b) {                    /* disjoint_pock_bound_bbox cal l.1038-1078 */
    if (b->bbox[0] <= rm_nc_tol + a->bbox[2] && b->bbox[1] <= rm_nc_tol + a->bbox[3] &&
        (a->bbox[0] - rm_nc_tol < b->bbox[2] || a->bbox[0] - rm_nc_tol == b->bbox[2]) &&
        (a->bbox[1] - rm_nc_tol < b->bbox[3] || a->bbox[1] - rm_nc_tol == b->bbox[3])) return 0;
    return 1;
}
/* pock_trim_toolpath cal l.272-435 */
int rm_trim(RmB **passes, RmB *rest, RmB **out) {
    *out = NULL;
    rm_b_bboxs(*passes);
    rm_b_infos(rest);
    rm_b_bboxs(rest);
    short rec = 0;
    for (RmB *b = *passes; b; b = b->next) { for (int i = 0; i < b->n; i++) b->e[i].rec = rec++; b->par = NULL; }
    for (RmB *b = rest; b; b = b->next) for (int i = 0; i < b->n; i++) b->e[i].rec = rec++;
    Ins ins[500]; RmB *insr[500]; int nins = 0;
    for (RmB *r = rest; r; r = r->next) for (RmB *p = *passes; p; p = p->next) {
        if (disjoint(p, r)) continue;
        short m = 0; Ins it[512];
        intersect(p, r, 0, (short)(500 - nins), it, &m);
        for (int i = 0; i < m; i++) { insr[nins] = r; ins[nins++] = it[i]; }
    }
    for (RmB *b = *passes; b; b = b->next) b->broken = 0;
    for (int i = 0; i < nins; i++) {
        RmB *pc; int ent;
        if (find_piece(&ins[i], *passes, &pc, &ent) == 0) { pc->par = insr[i]; break_bound(passes, pc, ent, ins[i].p); }
    }
    for (RmB *b = *passes; b; ) {
        RmB *nx = b->next, *hit = NULL;
        if (piece_in_rest(rest, b, &hit)) {
            unlink_b(passes, b);
            if (!b->par) b->par = hit;
            link_b(out, b, *out);
        }
        b = nx;
    }
    return 0;
}
/* remach_merge_adj_segs cal l.442-607 */
int rm_merge(RmB **head) {
    for (RmB *b = *head; b; b = b->next) b->done = 0;
restart:;
    RmB *a = *head;
    while (a) { if (!a->done) break; a = a->next; }
    if (!a) return 0;
    RmB *b = a->next; int merged = 0;
    while (b && !merged) {
        if (!b->done && b != a) {
            RmB *p9 = b, *p13 = a, *p12, *p14;
            for (int rep = 0; rep < 2; rep++) {
                double s0[2], en0[2], s1[2], en1[2];
                rm_gt_ends(&p13->e[p13->n - 1], s0, en0); rm_gt_ends(&p9->e[0], s1, en1);
                double d = sqrt((s1[1] - en0[1]) * (s1[1] - en0[1]) + (s1[0] - en0[0]) * (s1[0] - en0[0]));
                p12 = p13; p14 = p9;
                if (d < rm_nc_tol) { merged = 1; p12 = p9; p14 = p13; }
                p9 = p12; p13 = p14;
            }
            if (merged) {
                RmB *r = rm_b_new(p12->n + p14->n);
                r->isl = p14->isl; r->par = p14->par;
                int m = 0;
                for (int i = 0; i < p14->n; i++) { r->e[m].id = p14->e[i].id; memcpy(r->e[m].a, p14->e[i].a, sizeof r->e[m].a); memcpy(r->e[m].box, p14->e[i].box, sizeof r->e[m].box); m++; }
                for (int i = 0; i < p12->n; i++) { r->e[m].id = p12->e[i].id; memcpy(r->e[m].a, p12->e[i].a, sizeof r->e[m].a); memcpy(r->e[m].box, p12->e[i].box, sizeof r->e[m].box); m++; }
                r->done = 1;
                unlink_b(head, p14); rm_b_free(p14);
                unlink_b(head, p12); rm_b_free(p12);
                rm_b_append(head, r);
                goto restart;
            }
        }
        b = b->next;
    }
    a->done = 1;
    goto restart;
}
/* add_remachine_bound cal l.8-57 + FUN_1805e20e0 */
int rm_rem_add(RmB *b, short idx, RmRem **list) {
    RmRem *n = calloc(1, sizeof(RmRem));
    double s[2], en[2];
    n->active = 1; n->b = rm_b_copy(b);
    rm_gt_ends(&b->e[0], s, en); n->start[0] = s[0]; n->start[1] = s[1];
    rm_gt_ends(&b->e[b->n - 1], s, en); n->end[0] = en[0]; n->end[1] = en[1];
    n->idx = idx; n->next = NULL;
    if (!*list) *list = n; else { RmRem *t = *list; while (t->next) t = t->next; t->next = n; }
    return 0;
}
/* FUN_1805e3d40 cal l.88-175: the "selected" flag bVar3 persistent across list items (disassembly 0x1805e3e03-0x1805e3e0d) */
static void pick(char cont, const double cur[2], const RmB *par, RmRem *head, RmRem **res) {
    int b3 = 0; short s5 = -1, s6 = -1; double d = 1e38, d2 = 1e38;
    *res = NULL;
    if (cont && head) {
        for (RmRem *p = head; p; p = p->next) {
            if (p->active && (p->b->par == par || par == NULL)) {
                double v = sqrt((p->start[0] - cur[0]) * (p->start[0] - cur[0]) + (p->start[1] - cur[1]) * (p->start[1] - cur[1]));
                if (par == NULL) {
                    short s1 = p->idx;
                    if (s1 <= s6) {
                        if (s1 == s6) goto eq;
                        if (!b3) continue;
                    } else { b3 = 1; s6 = s1; }
                } else {
eq:                 b3 = v <= d && d != v;
                    if (!b3) continue;
                }
                d = v; *res = p;
            }
        }
        if (*res) goto phase3;
    }
    for (RmRem *p = head; p; p = p->next) {
        if (p->active) {
            if (!cont) { if (s5 < p->idx) { *res = p; s5 = p->idx; } }
            else {
                double v = sqrt((p->start[0] - cur[0]) * (p->start[0] - cur[0]) + (p->start[1] - cur[1]) * (p->start[1] - cur[1]));
                if (v <= d2 && d2 != v) { d2 = v; *res = p; }
            }
        }
    }
    if (!*res) return;
phase3:
    for (RmRem *q = head; q; q = q->next)
        if (q->active && q != *res && q->b->par == (*res)->b->par && (*res)->idx < q->idx) *res = q;
}
/* find_remachine_group cal l.184-269 */
int rm_group(char *cont, double cur[2], RmRem *list, RmRem **group) {
    *group = NULL;
    RmRem *p; RmB *par = NULL; int have = 0;
    pick(*cont, cur, NULL, list, &p);
    while (p && (!have || p->b->par == par)) {
        p->active = 0;
        RmRem *c = calloc(1, sizeof(RmRem));
        *c = *p; c->next = NULL;
        if (!*group) *group = c; else { RmRem *t = *group; while (t->next) t = t->next; t->next = c; }
        par = p->b->par; have = 1;
        cur[0] = p->end[0]; cur[1] = p->end[1];
        *cont = 1;
        pick(1, cur, par, list, &p);
    }
    return 0;
}
void rm_rem_free(RmRem **l, int bounds) { while (*l) { RmRem *n = (*l)->next; if (bounds) rm_b_free((*l)->b); free(*l); *l = n; } }

/* ---- sort_pock_bounds (core l.107-362, p1 = NULL / p2 = 0: the driver's call; 'P' bounds never occur here) ---- */
static int is_pt_in_rect(const double p[2], const double lo[2], const double hi[2], double t) {   /* IsPtInRect more2 l.234 */
    return !(p[0] <= lo[0] - t || t + hi[0] <= p[0] || p[1] <= lo[1] - t || t + hi[1] <= p[1]);
}
static int intersect_any(const RmB *a, const RmB *b) {                /* pock_bound_intersect pock l.2183 */
    for (int i = 0; i < a->n; i++) for (int j = 0; j < b->n; j++) {
        const RmEnt *ea = &a->e[i], *eb = &b->e[j];
        if (eb->box[0] <= rm_nc_tol + ea->box[2] && rm_nc_tol + ea->box[2] != eb->box[0] && eb->box[1] <= rm_nc_tol + ea->box[3] && rm_nc_tol + ea->box[3] != eb->box[1] &&
            ea->box[0] <= rm_nc_tol + eb->box[2] && rm_nc_tol + eb->box[2] != ea->box[0] && ea->box[1] <= rm_nc_tol + eb->box[3] && rm_nc_tol + eb->box[3] != ea->box[1]) {
            double ip[4];
            if (ints_on_gt(ea, eb, ip, rm_nc_tol) > 0) return 1;
        }
    }
    return 0;
}
/* FUN_1805e0e00 more2 l.141-230: copies of a and b; a is cut at their crossings */
static int nest_pieces(const RmB *a, const RmB *b, RmB **pieces) {
    RmB *ca = rm_b_copy(a), *cb = rm_b_copy(b); ca->broken = 0;
    Ins it[1024]; short m;
    intersect(ca, cb, 1, 0x400, it, &m);
    *pieces = ca;
    for (int i = 0; i < m; i++) {
        RmB *pc; int ent;
        if (find_piece(&it[i], *pieces, &pc, &ent) == 0) break_bound(pieces, pc, ent, it[i].p);
    }
    rm_b_free(cb);
    return m;
}
/* IsContourNested pock l.2270 */
static int contour_nested(const RmB *a, const RmB *b, double t) {
    if (is_point(b) || !is_pt_in_rect(a->bbox, b->bbox, b->bbox + 2, t) || !is_pt_in_rect(a->bbox + 2, b->bbox, b->bbox + 2, t)) return 0;
    RmB *pieces; int nx = nest_pieces(a, b, &pieces), res = 0, done = 0;
    for (RmB *p = pieces; p && !done; p = p->next) {
        for (int i = 0; i < p->n; i++) {
            const RmEnt *e = &p->e[i]; double m[2];
            if (e->id == 'L') { m[0] = (e->a[0] + e->a[2]) * 0.5; m[1] = (e->a[1] + e->a[3]) * 0.5; }
            else if (e->id == 'A') { double x = e->a[4] * 0.5 + e->a[3]; m[0] = cos(x) * e->a[2] + e->a[0]; m[1] = sin(x) * e->a[2] + e->a[1]; }
            else continue;
            int s = rm_pt_in_ctour(b, m[0], m[1], rm_nc_tol, rm_tol);
            if (s == -1) { res = 0; done = 1; break; }
            if (s == 1) { if (nx == 0) { res = 1; done = 1; } break; }
        }
    }
    if (!done) res = 1;
    rm_b_free_list(&pieces);
    return res;
}
int rm_contour_nested(const RmB *a, const RmB *b, double t) { return contour_nested(a, b, t); }   /* mccore IsContourNested (a inside b) */
static int is_ctour_nested(const RmB *a, const RmB *b) {              /* is_ctour_nested pock l.2730 */
    if (is_point(b) || intersect_any(a, b)) return 0;
    for (int i = 0; i < a->n; i++) { double s[2], en[2]; rm_gt_ends(&a->e[i], s, en); if (rm_pt_in_ctour(b, s[0], s[1], rm_nc_tol, rm_tol) == -1) return 0; }
    return 1;
}
static double pock_perim(const RmB *b) { double s = 0; for (int i = 0; i < b->n; i++) s += length_gt(&b->e[i]); return s; }   /* pock_perim pock l.2935 */
/* bound_line_inters pock l.4094 / FUN_1805e1cc0 more2 l.267: the crossings of bound b with the horizontal line y (only lines reach this: the sort's bounds are the pbmap rings) */
typedef struct { double x, y, t; const RmB *b; } LineHit;
static void line_hits(const RmB *b, double l[4], int cap, LineHit *h, short *pn) {
    int horiz = fabs(l[3] - l[1]) <= 1e-12 && fabs(l[3] - l[1]) != 1e-12;
    double yy = l[3];
    for (int i = 0; i < b->n; i++) {
        const RmEnt *e = &b->e[i];
        if (horiz && !(yy < rm_nc_tol + e->box[3] || yy == rm_nc_tol + e->box[3])) continue;
        if (horiz && !(e->box[1] - rm_nc_tol < yy || e->box[1] - rm_nc_tol == yy)) continue;
        if (e->id != 'L') continue;
        /* l_ints (sys.dll, not read: the same equations as l_ints_tol without the tolerance branch; the crossing parameter t of the ring entity is what is tested) */
        double dx1 = l[2] - l[0], dy1 = l[3] - l[1], dx2 = e->a[2] - e->a[0], dy2 = e->a[3] - e->a[1];
        double det = dx1 * dy2 - dy1 * dx2;
        if (det == 0.0) continue;
        double s = ((e->a[0] - l[0]) * dy2 - (e->a[1] - l[1]) * dx2) / det, u = ((e->a[0] - l[0]) * dy1 - (e->a[1] - l[1]) * dx1) / det;
        double px = l[0] + s * dx1, py = l[1] + s * dy1;
        if (!((int)*pn < cap - 1) || !(-2e-07 < u && u < 1.0000002)) continue;
        /* FUN_1805e1cc0: sorted by t along the line; a point within tol of one of the two neighbours in order on the same bound is dropped */
        double tv = fabs(dx1) > fabs(dy1) ? (px - l[0]) / dx1 : (py - l[1]) / dy1;
        int s13 = 1; short n = *pn;
        for (int k = 1; k <= n; k++) if (0.0 < tv - h[k].t) s13 = k + 1;
        int s10 = s13 - 1 > 1 ? s13 - 1 : 1, s12 = s13 + 1 < n ? s13 + 1 : n, dup = 0;
        for (int k = s10; k <= s12; k++) {
            if (h[k].b == b) { double dv = sqrt((h[k].x - px) * (h[k].x - px) + (h[k].y - py) * (h[k].y - py)); if (dv < rm_tol) dup = 1; }
        }
        if (dup) continue;
        for (int k = n; k >= s13; k--) h[k + 1] = h[k];
        h[s13].x = px; h[s13].y = py; h[s13].t = tv; h[s13].b = b;
        (*pn)++;
    }
}
/* pock_bound_overlap pock l.3085 */
static int pock_overlap(const RmB *a, const RmB *b, double t) {
    double lo = a->bbox[1] <= b->bbox[1] ? b->bbox[1] : a->bbox[1], hi = b->bbox[3] <= a->bbox[3] ? b->bbox[3] : a->bbox[3];
    double d2 = lo + t, d1 = hi - t;
    if (!(d2 < d1)) return 0;
    double step = (d1 - d2) / 99.0;
    LineHit h[502];
    for (int s10 = 0; s10 < 0x65; s10++, d2 += step) {
        double l[4] = { -100.0 + 0 * d2, d2, 100.0, d2 };
        l[0] = 0.0; l[2] = 100.0;   /* local_5ea0[0] = 0, local_5e90[0] = 100.0 */
        short n = 0;
        line_hits(a, l, 500, h, &n);
        short u7 = n;
        if (u7 == 0 || (u7 & 1) != 0) continue;
        line_hits(b, l, 500, h, &n);
        if (n == u7 || (n & 1) != 0) continue;
        short s11 = 0, s12 = 0;
        for (;;) {
            int b3 = s11 == 0 || s11 < s12, b4 = s12 == 0 || s12 < s11;
            short s5 = s11, s6 = s12;
            if (!b3) break;
            do { s5++; if (n < s5) goto nextrow; s11 = s5; } while (h[s5].b != a);
            do { s11++; if (n < s11) goto nextrow; } while (h[s11].b != a);
            if (!b4) return 0;
            do { s6++; if (n < s6) return 0; s12 = s6; } while (h[s6].b != b);
            do { s12++; if (n < s12) return 0; } while (h[s12].b != b);
            double d18 = h[s6].x - h[s5].x, d17 = fabs(h[s12].x - h[s11].x);
            if (d18 * (h[s12].x - h[s11].x) < 0.0) {
                if (fabs(d18) <= d17) d17 = fabs(d18);
                if (t < d17) return 1;
            }
        }
nextrow:;
    }
    return 0;
}
static int same_ring(const RmB *a, const RmB *b, double t) {          /* FUN_1805d4970 pock l.2136 */
    for (int i = 0; i < a->n; i++) {
        double y, x = eval_gt(&a->e[i], 0.0, &y), best = 1e38;
        for (int j = 0; j < b->n; j++) {
            double yy, xx = eval_gt(&b->e[j], 0.0, &yy), d = sqrt((xx - x) * (xx - x) + (yy - y) * (yy - y));
            if (d <= best && best != d) best = d;
            if (!(t < best || t == best)) break;
        }
        if (t <= best && best != t) return 0;
    }
    return 1;
}
void rm_sort(RmB **head, int p3, double tolv, short *ng) {
    *ng = 0;
    for (RmB *b = *head; b; b = b->next) { b->valid = 1; b->gid = -1; b->isl = 0; b->par = NULL; b->done = 0; rm_b_info(b); rm_b_bbox(b); }
    for (RmB *a = *head; a; a = a->next) {
        if (!a->valid) continue;
        for (RmB *b = *head; b; b = b->next) {
            if (!b->valid || a == b || a->par == b || a == b->par) continue;
            if (is_point(a) && is_point(b)) continue;
            if (disjoint(a, b)) continue;
            int nested = contour_nested(a, b, rm_nc_tol);
            if (!p3 || !intersect_any(a, b) || !pock_overlap(a, b, tolv * 2.0)) {
                if (nested && (!a->par || is_ctour_nested(b, a->par))) { a->done = 0; a->par = b; }
            } else {
                double d16 = pock_perim(a), d17 = pock_perim(b);
                if (rm_nc_tol < fabs(d16 - d17) || rm_nc_tol == fabs(d16 - d17) || !same_ring(a, b, rm_nc_tol)) {
                    if (a->valid) {
                        double aa, ab;
                        pock_ccw(a, &aa); pock_ccw(b, &ab);
                        if (fabs(ab) <= fabs(aa)) { if (!a->done) { b->par = a; b->done = 1; } }
                        else if (!b->done) { a->done = 1; a->par = b; }
                    }
                } else a->valid = 0;
            }
        }
    }
    for (RmB *b = *head; b; ) { RmB *n = b->next; if (!b->valid) { unlink_b(head, b); rm_b_free(b); } b = n; }   /* the DLL draws it (warning 0x12c8) and unlinks */
    for (;;) {
        RmB *a = *head;
        while (a && (a->gid > -1 || (a->par && (a->par->isl == 0 || a->done)))) a = a->next;
        if (!a) break;
        a->gid = (*ng); (*ng)++; a->isl = 0;
        for (RmB *b = *head; b; b = b->next) if (b->par == a) { b->isl = 1; b->gid = a->gid; }
    }
    for (RmB *a = *head; a; a = a->next) {
        if (a->gid == -1) {
            RmB *p = a->par;
            while (p && p->gid < 0 && (p = p->par) != a->par) { }
            if (p) { a->isl = 1; a->gid = p->gid; }
        }
    }
    for (RmB *a = *head; a; a = a->next) a->broken = 0;
    /* (the point-bound pass and the p1 / p2 branch of the DLL: no 'P' bounds here, p2 = 0) */
}
/* FUN_1801c2b30 (decomp/mcmill_all.c l.311918): one lead element on the end of e: ln = 0 arc, 1 line; ex = 0 entry, 1 exit */
static RmEnt lead_gt(const RmEnt *e, int ex, int ln, char side, double len, double rad, double sw, int perp) {
    RmEnt out; memset(&out, 0, sizeof out);
    double s[2], en[2], pt[2], tx, ty; rm_gt_ends(e, s, en);
    if (ex) { pt[0] = en[0]; pt[1] = en[1]; } else { pt[0] = s[0]; pt[1] = s[1]; }
    if (e->id == 'L') { double dx = e->a[2] - e->a[0], dy = e->a[3] - e->a[1], l = sqrt(dx * dx + dy * dy); if (l == 0.0) { tx = ty = 0.0; } else { tx = dx / l; ty = dy / l; } }
    else { double a = ex ? e->a[4] + e->a[3] : e->a[3], sv = sin(a); ty = cos(a); tx = -sv; if (e->a[4] <= 0.0 && e->a[4] != 0.0) { ty = -ty; tx = sv; } }
    double nx = -ty, ny = tx;
    if (side == 'R') { nx = ty; ny = -tx; }
    if (!ln) {
        out.id = 'A'; out.a[2] = rad;
        out.a[0] = pt[0] + nx * rad; out.a[1] = ny * rad + pt[1];
        out.a[4] = side == 'R' ? -sw : sw;
        double a = 0.0;
        if (1e-12 <= (-ny) * (-ny) + (-nx) * (-nx)) { a = atan2(-ny, -nx); if (a < 0.0) a += TWO_PI; }
        if (!ex) a -= out.a[4];
        out.a[3] = a;
    } else {
        double vx = nx, vy = ny;
        if (!perp) { if (!ex) { ty = -ty; tx = -tx; } vx = tx; vy = ty; }
        out.id = 'L';
        double px = pt[0] + vx * len, py = pt[1] + vy * len;
        if (!ex) { out.a[0] = px; out.a[1] = py; out.a[2] = pt[0]; out.a[3] = pt[1]; }
        else { out.a[0] = pt[0]; out.a[1] = pt[1]; out.a[2] = px; out.a[3] = py; }
    }
    return out;
}
/* FUN_180050de0 l.55322-55835: the piece with its leads: entry line, entry arc, the records, exit arc, exit line; heights +0x78 (suffix sums entry, prefix sums exit); FUN_1801c46f0 (gouge check, op +0x929) NOT ported */
/* ---- the Lead In/Out gouge check of a remachining piece: FUN_1801c46f0 -> FUN_1801c3d70 (decomp/mcmill_all.c l.313097, l.312726; argument order of its
   calls from the disassembly 0x1801c4001-0x1801c408c, 0x1801c4459-0x1801c45c2). Called with param_1 = NULL, param_8 = param_10 = 0 (FUN_180050de0 l.55574 / l.55643). ---- */
/* pt_on_comped_side_of_open_ctour (mccore 0x1805e7190): the entity whose projection is nearest (overshoot past its ends first, within nc_tol the distance), the side of its tangent there */
static int on_comped_side(const RmB *b, char side, const double p[2]) {
    int found = 0; double best_over = 1e38, best_d = 1e38, bt = 0.0, foot[2] = { 0, 0 }; const RmEnt *be = NULL;
    for (int i = 0; i < b->n; i++) {
        const RmEnt *e = &b->e[i]; double f[2]; int ok = 0;
        if (e->id == 'L') {                                 /* proj_pt_ln: the infinite line */
            double dx = e->a[2] - e->a[0], dy = e->a[3] - e->a[1], d = dy * dy + dx * dx;
            if (d != 0.0) { double u = ((p[1] - e->a[1]) * dy + (p[0] - e->a[0]) * dx) / d; f[0] = (1.0 - u) * e->a[0] + u * e->a[2]; f[1] = (1.0 - u) * e->a[1] + u * e->a[3]; ok = 1; }
        } else if (e->id == 'A') {                          /* proj_pt_ar: the circle */
            double dx = p[0] - e->a[0], dy = p[1] - e->a[1], d = sqrt(dx * dx + dy * dy);
            if (d > 0.0) { f[0] = e->a[0] + dx / d * e->a[2]; f[1] = e->a[1] + dy / d * e->a[2]; ok = 1; }
        }
        if (!ok) continue;
        double t = t_on_gt(f, e), o = 1.0 < t ? t - 1.0 : (0.0 <= t ? 0.0 : -t), over = fabs(o) <= 1e-12 ? 0.0 : length_gt(e) * o;
        double d = sqrt((f[1] - p[1]) * (f[1] - p[1]) + (f[0] - p[0]) * (f[0] - p[0]));
        int no = (rm_nc_tol <= over || rm_nc_tol <= best_over) ? (best_over <= over) : (best_d <= d);
        if (!no) { foot[0] = f[0]; foot[1] = f[1]; bt = t; found = 1; be = e; best_d = d; best_over = over; }
    }
    if (!found) return 0;
    double tx, ty;                                          /* eval_gt_tan */
    if (be->id == 'L') { double dx = be->a[2] - be->a[0], dy = be->a[3] - be->a[1], l = sqrt(dx * dx + dy * dy); tx = l > 0 ? dx / l : 0; ty = l > 0 ? dy / l : 0; }
    else { double ang = bt * be->a[4] + be->a[3]; tx = -sin(ang); ty = cos(ang); if (be->a[4] < 0.0) { tx = -tx; ty = -ty; } }
    if (side == 'L') ty = -ty; else tx = -tx;
    return 0.0 < (p[1] - foot[1]) * tx + (p[0] - foot[0]) * ty;
}
/* GetNearestLineSegPt 0x1805e0b40 / FUN_1805e0a00, GetNearestPockBndPt 0x1805e0c20 */
static void nearest_bnd(const RmB *b, const double p[2], double out[2]) {
    int first = 1; double best = 0.0; out[0] = out[1] = 0.0;
    for (int i = 0; i < b->n; i++) {
        const RmEnt *e = &b->e[i]; double q[2] = { 0, 0 };
        if (e->id == 'A') {
            double u;
            if (1.1920928955078125e-07 <= fabs(p[0] - e->a[0]) || 1.1920928955078125e-07 <= fabs(p[1] - e->a[1])) {
                double v = atan2(p[1] - e->a[1], p[0] - e->a[0]) - e->a[3]; while (v < 0.0) v += 6.283185307179586;
                double sw = e->a[4]; if (sw < 0.0) v += -6.283185307179586;
                u = v;
                if (fabs(sw) < fabs(v)) { double m = 0.0 <= sw ? sw * 0.5 + 3.141592653589793 : sw * 0.5 - 3.141592653589793; u = fabs(m) < fabs(v) ? 0.0 : sw; }
            } else u = e->a[4] * 0.5;
            q[0] = cos(u + e->a[3]) * e->a[2] + e->a[0]; q[1] = sin(u + e->a[3]) * e->a[2] + e->a[1];
        } else if (e->id == 'L') {
            double dx = e->a[2] - e->a[0], dy = e->a[3] - e->a[1], d = dy * dy + dx * dx;
            if (fabs(d) < 1.1920928955078125e-07) { q[0] = e->a[0]; q[1] = e->a[1]; }
            else { double u = -(((e->a[1] - p[1]) * dy + (e->a[0] - p[0]) * dx) / d); if (u < 0.0) u = 0.0; else if (1.0 < u) u = 1.0; q[0] = dx * u + e->a[0]; q[1] = dy * u + e->a[1]; }
        } else { q[0] = e->a[0]; q[1] = e->a[1]; }
        double d = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]);
        if (first || d < best) { first = 0; best = d; out[0] = q[0]; out[1] = q[1]; }
    }
}
/* FUN_1801bc0d0 IsIsectAtEitherEnd (l.307458): 1 when, walking from the intersection towards either end of records i0..i1, every piece stays on the chain within t */
static int isect_at_either_end(const RmB *chain, const RmB *lead, int i0, int i1, int rec, const double p[2], double t) {
    for (int dir = -1; dir <= 1; dir += 2) {
        int off = 0;
        for (int k = rec; k >= i0 && k <= i1 && !off; k += dir) {
            RmEnt piece, two[2]; int use = 1;
            if (k == rec) { char ok[2] = { 0, 0 }; split_chn(&lead->e[k], p, t, two, ok); if (dir == -1 && ok[0]) piece = two[0]; else if (dir == 1 && ok[1]) piece = two[1]; else use = 0; }
            else piece = lead->e[k];
            if (use) { double q[2], n[2]; q[0] = eval_gt(&piece, 0.5, &q[1]); nearest_bnd(chain, q, n); if (t * t < (n[1] - q[1]) * (n[1] - q[1]) + (n[0] - q[0]) * (n[0] - q[0])) off = 1; }
        }
        if (!off) return 1;
    }
    return 0;
}
typedef struct { int rec; double key, t, p[2]; char same; } GIns;      /* 0x30 bytes: +0 rec, +8 the sort key, +0x10 t, +0x18 point, +0x28 same parent */
static void gins_add(GIns *l, short *n, const GIns *v) {               /* FUN_1801c37b0, capacity 8 */
    short at = *n; for (short i = (short)(*n - 1); i >= 0; i--) if (v->key < l[i].key) at = i;
    if (at >= 8) return;
    for (short k = *n; k > at; k--) if (k < 8) l[k] = l[k - 1];
    l[at] = *v; if (*n < 8) (*n)++;
}
/* FUN_1801bbc70 CalcSectMidPtViaM through mcce::CalcSectLen / CalcSectPt (FUN_180149d80 / FUN_18014a290): halfway along the lead between this hit and the next (or the lead's start / end) */
static void sect_mid(const RmB *b, const GIns *l, int n, int i, int to_start, double out[2]) {
    int r0 = l[i].rec, r1; double t0 = l[i].t, t1;
    if (i < n - 1) { r1 = l[i + 1].rec; t1 = l[i + 1].t; } else if (!to_start) { r1 = b->n - 1; t1 = 1.0; } else { r1 = 0; t1 = 0.0; }
    if (r1 < r0 || (r1 == r0 && t1 < t0)) { int a = r0; r0 = r1; r1 = a; double c = t0; t0 = t1; t1 = c; }
    t0 = t0 < 0.0 ? 0.0 : (1.0 < t0 ? 1.0 : t0); t1 = t1 < 0.0 ? 0.0 : (1.0 < t1 ? 1.0 : t1);
    double total = 0.0;
    for (int k = r0; k <= r1; k++) total += length_gt(&b->e[k]) * ((k == r1 ? t1 : 1.0) - (k == r0 ? t0 : 0.0));
    double left = total * 0.5;
    for (int k = r0; k <= r1; k++) {
        double a = k == r0 ? t0 : 0.0, c = k == r1 ? t1 : 1.0, len = length_gt(&b->e[k]) * (c - a);
        if (left <= len || k == r1) { double u = len > 0.0 ? a + (c - a) * left / len : a; out[0] = eval_gt(&b->e[k], u, &out[1]); return; }
        left -= len;
    }
}
/* FUN_1801c1b60: the first hit whose stretch of lead beyond it is not on the compensated side of the gouge bound (or of another parent) */
static int gouge_pick(const RmB *lead, const GIns *l, short n, const RmB *ref, int to_start, char side, GIns *out) {
    for (int i = 0; i < n; i++) {
        int pick = !l[i].same;
        if (!pick) { double m[2]; sect_mid(lead, l, n, i, to_start, m); pick = !on_comped_side(ref, side, m); }
        if (pick) { *out = l[i]; return 1; }
    }
    return 0;
}
static void lead_gouge(RmB *chain, RmB *ref, RmB *lead, int p5, int p7, char side, char *fl) {   /* FUN_1801c3d70 with p6 = 0 */
    int n = p7 + p5, trim_ok; short ns = 0, nx = 0; GIns ls[8], lx[8], a, c; memset(ls, 0, sizeof ls); memset(lx, 0, sizeof lx);
    for (int i = 0; i < n; i++) fl[i] = 1;
    for (RmB *q = ref; q; q = q->next) {                    /* GetProdEnableBits & 0x10 (meaning NOT DECODED, opdlg_fields_notes.md) taken as off, as elsewhere here */
        Ins it[8]; short m = 0; intersect(lead, q, 1, 8, it, &m);
        for (int i = 0; i < m; i++) {
            GIns v; memset(&v, 0, sizeof v); int r = it[i].ra; v.rec = r; v.same = lead->par == q->par; v.p[0] = it[i].p[0]; v.p[1] = it[i].p[1];
            v.t = t_on_gt(it[i].p, &lead->e[r]); v.key = v.t;
            if (r < p5) { if (0 < r) v.key = v.t + 1.0; v.key = 2.0 - v.key; gins_add(ls, &ns, &v); }
            else if (n - p7 <= r && r < n) { if (p5 < r) v.key = v.t + 1.0; gins_add(lx, &nx, &v); }
        }
    }
    int ha = gouge_pick(lead, ls, ns, ref, 1, side, &a), hc = gouge_pick(lead, lx, nx, ref, 0, side, &c);
    if (ha) { if (0 < a.rec) fl[0] = 0; trim_gt(&lead->e[a.rec], a.p, 0, &trim_ok); }
    if (hc) { if (c.rec < n - 1) fl[n - 1] = 0; trim_gt(&lead->e[c.rec], c.p, 1, &trim_ok); }
    for (int i = 0; i < n; i++) {
        RmEnt *e = &lead->e[i]; double q[2];
        if (length_gt(e) < rm_nc_tol) fl[i] = 0;
        if ((p5 <= i && i < n - p7) || !fl[i]) continue;
        q[0] = eval_gt(e, 0.5, &q[1]);
        if (!on_comped_side(chain, side, q)) { fl[i] = 0; continue; }
        q[0] = eval_gt(e, i < p5 ? 0.001 : 0.999, &q[1]);
        if (!on_comped_side(chain, side, q)) fl[i] = 0;
    }
    for (int u = 0; u < n; u++) if (fl[u]) {                 /* a lead that crosses the chain away from both of its ends goes */
        rm_b_info(chain); rm_b_bbox(chain);
        Ins it[8]; short m = 0; intersect(lead, chain, 1, 8, it, &m);
        for (int i = 0; i < m; i++) {
            int r = it[i].ra; if (!fl[r]) continue;
            int r0 = r < p5 ? 0 : p5, r1 = r < p5 ? p5 - 1 : n - 1;
            double s[2], en[2]; s[0] = eval_gt(&lead->e[r0], 0.0, &s[1]); en[0] = eval_gt(&lead->e[r1], 1.0, &en[1]);
            if (rm_nc_tol < sqrt((s[1] - it[i].p[1]) * (s[1] - it[i].p[1]) + (s[0] - it[i].p[0]) * (s[0] - it[i].p[0])) &&
                rm_nc_tol < sqrt((en[1] - it[i].p[1]) * (en[1] - it[i].p[1]) + (en[0] - it[i].p[0]) * (en[0] - it[i].p[0])) &&
                !isect_at_either_end(chain, lead, r0, r1, r, it[i].p, rm_nc_tol))
                for (int k = r0; k <= r1; k++) fl[k] = 0;
        }
        break;
    }
    for (int i = 1; i < n - 1; i++) if (!fl[i]) {            /* l.313060-313085: everything outward of a dropped entity goes too */
        if (i < p5) { for (int k = 0; k < i; k++) fl[k] = 0; }
        else if (i + 1 < n) { for (int k = i + 1; k < n; k++) fl[k] = 0; }
    }
}
/* FUN_1801c46f0: one side's temporary lead bound (entry: [line,] arc; exit: arc[, line]); *pn and ents are updated */
static void lead_gouge_side_rm(RmB *chain, RmB *ref, RmEnt *ents, int *pn, char side, int entry) {
    int n = *pn; if (n < 1 || n > 2) return;
    RmB *b = rm_b_new(n); memcpy(b->e, ents, sizeof(RmEnt) * (size_t)n); rm_b_info(b); rm_b_bbox(b);
    char fl[2] = { 1, 1 };
    lead_gouge(chain, ref, b, entry ? n : 0, entry ? 0 : n, side, fl);
    memcpy(ents, b->e, sizeof(RmEnt) * (size_t)n); rm_b_free(b);
    if (n == 1) { if (!fl[0]) *pn = 0; return; }
    int near_i = entry ? 1 : 0, far_i = entry ? 0 : 1;
    if (!fl[near_i]) { *pn = 0; return; }
    if (!fl[far_i]) { *pn = 1; if (near_i == 1) ents[0] = ents[1]; }
}

RmEnt rm_lead_gt(const RmEnt *e, int ex, int ln, char side, double len, double rad, double sw, int perp) { return lead_gt(e, ex, ln, side, len, rad, sw, perp); }
void rm_lead_gouge_side(RmB *chain, RmB *ref, RmEnt *ents, int *pn, char side, int entry) { lead_gouge_side_rm(chain, ref, ents, pn, side, entry); }
RmB *rm_add_leads(const RmB *pc, const RmLeads *l, char side, RmB *chain, RmB *ref, int gouge, int *n_entry, int *n_exit, int *t16, int *t17) {
    int a_in = !(l->sw_in <= 1e-06 || (l->r_in < rm_nc_tol || l->r_in == rm_nc_tol));
    int a_out = !(l->sw_out <= 1e-06 || (l->r_out < rm_nc_tol || l->r_out == rm_nc_tol));
    int b_in = rm_nc_tol <= fabs(l->len_in) && fabs(l->len_in) != rm_nc_tol, b_out = rm_nc_tol <= fabs(l->len_out) && fabs(l->len_out) != rm_nc_tol;
    int ni = b_in + a_in, no = a_out + b_out;
    *n_entry = 0; *n_exit = 0;
    RmEnt ent_i[2], ent_o[2]; memset(ent_i, 0, sizeof ent_i); memset(ent_o, 0, sizeof ent_o);
    if (ni) {
        RmEnt cur = pc->e[0], arc = cur, lin;
        if (a_in) { arc = lead_gt(&cur, 0, 0, side, 0, l->r_in, l->sw_in, 0); cur = arc; }
        int k = 0;
        if (b_in) { lin = lead_gt(&cur, 0, 1, side, l->len_in, 0, 0, l->perp_in); ent_i[k] = lin; ent_i[k].h = l->dz_in_line; k++; }
        if (a_in) { ent_i[k] = arc; ent_i[k].h = l->dz_in_arc; k++; }
    }
    if (no) {
        RmEnt cur = pc->e[pc->n - 1], arc, lin;
        int k = 0;
        if (a_out) { arc = lead_gt(&cur, 1, 0, side, 0, l->r_out, l->sw_out, 0); cur = arc; ent_o[k] = arc; ent_o[k].h = l->dz_out_arc; k++; }
        if (b_out) { lin = lead_gt(&cur, 1, 1, side, l->len_out, 0, 0, l->perp_out); ent_o[k] = lin; ent_o[k].h = l->dz_out_line; k++; }
    }
    if (gouge) { lead_gouge_side_rm(chain, ref, ent_i, &ni, side, 1); lead_gouge_side_rm(chain, ref, ent_o, &no, side, 0); }   /* op +0x929 (record 0x991), l.55570-55575 / l.55639-55644 */
    RmB *r = rm_b_new(pc->n + ni + no);
    r->isl = pc->isl; r->par = pc->par;
    int m = 0;
    for (int i = 0; i < ni; i++) r->e[m++] = ent_i[i];
    for (int i = 0; i < pc->n; i++) r->e[m++] = pc->e[i];
    for (int i = 0; i < no; i++) r->e[m++] = ent_o[i];
    double acc = 0.0;
    for (int i = ni - 1; i >= 0; i--) { acc += ent_i[i].h; r->e[i].h = acc; }            /* suffix sums (l.55780-55805) */
    acc = 0.0;
    for (int i = 0; i < no; i++) { acc += ent_o[i].h; r->e[m - no + i].h = acc; }       /* prefix sums (l.55806-55818) */
    for (int i = ni; i < ni + pc->n; i++) r->e[i].h = 0.0;                                /* ctour_depth (l.55772): depth records handled by the writer */
    if (ni) *n_entry = ni;
    if (no) *n_exit = no;
    *t16 = *n_entry + 2;
    *t17 = (m - *n_exit) + 1;
    rm_b_infos(r);
    return r;
}

/* ccomp_pock core l.487-665 through MCCECalcContourOffset (mcce_ctour.c) */
int rm_ccomp_pock(const RmB *b, RmB **out, double dist, char side, double p5, int p6, int p7, int p8, const char **why) {
    *out = NULL;
    double d = side == 'R' ? -dist : dist;
    double gap = p7 ? 1e38 : (p8 ? -1.0 : rm_chain_tol);
    double ang = fabs(p5 - 0.01) < rm_tol ? 0.5729577951308232 : fabs(p5 - PI) < rm_tol ? 179.1 : 44.99999063801583;
    unsigned fl = p6 ? 0x40027 : 0x60027;
    McceEnt *ents = calloc((size_t)(b->n ? b->n : 1), sizeof(McceEnt));
    for (int i = 0; i < b->n; i++) { ents[i].id = b->e[i].id; memcpy(ents[i].a, b->e[i].a, sizeof ents[i].a); }
    McceShape in = { ents, b->n, b->ccw ? 1 : -1, b->isl };
    McceResult res = mcce_contour_offset(&in, 1, d, fl, ang, gap, rm_nc_tol, 0.75 * rm_nc_tol);
    free(ents);
    if (res.refused) { if (why) *why = res.refused; return -1; }
    RmB *list = NULL;
    for (int i = 0; i < res.n; i++) {
        RmB *nb = rm_b_new(res.s[i].n);
        for (int k = 0; k < res.s[i].n; k++) { nb->e[k].id = res.s[i].e[k].id; memcpy(nb->e[k].a, res.s[i].e[k].a, sizeof nb->e[k].a); nb->e[k].rec = 0; }
        nb->ccw = res.s[i].dir == 1; nb->isl = b->isl;   /* FUN_1805d6f90 only copies the entity tags; copy_pock_bound_properties core l.651 */
        rm_b_append(&list, nb);
    }
    mcce_shapes_free(&res);
    for (RmB *a = list; a; a = a->next) a->broken = 0;
    for (RmB *a = list; a; a = a->next) for (RmB *c = list; c; c = c->next) if (c != a && is_ctour_nested(a, c)) { a->isl = c->isl == 0; a->broken = 1; break; }
    rm_b_infos(list);
    *out = list;
    return 0;
}

/* ---- merge_pock_bounds / merge_pock_bounds_facing_islands: mccore FUN_1805e02e0 (decomp/pocket_b6/pspiral_decomp_merge.c, POCKMERG.cpp), checked against the DLL by tools/tests/isl_face_oracle.sh ---- */
typedef struct { char on; double p[2]; RmB *b[2]; int r[2]; } MgI;              /* the 0x30-byte record of FUN_1805df8c0 */
typedef struct { char on, fl; int idx; double x, y, key; RmB *pc; } MgT;         /* the 0x28-byte record of FUN_1805dfc50 */

static int disjoint_box(const RmB *a, const RmB *b) {                            /* disjoint_pock_bound_bbox mccore 1805e3070 */
    double n = rm_nc_tol;
    if (b->bbox[0] <= n + a->bbox[2] && b->bbox[1] <= n + a->bbox[3] && (a->bbox[0] - n < b->bbox[2] || a->bbox[0] - n == b->bbox[2]) && (a->bbox[1] - n < b->bbox[3] || a->bbox[1] - n == b->bbox[3])) return 0;
    return 1;
}
/* reverse_pock_bound mccore 1805dda60: keep = param_2 (the first entity stays first) */
static void mg_reverse(RmB *b, int keep) {
    int n = b->n, top = keep ? (n - 1) / 2 + 1 : n / 2;
    for (int i = keep ? 1 : 0, j = n - 1; i < top; i++, j--) { RmEnt t = b->e[i]; b->e[i] = b->e[j]; b->e[j] = t; }
    for (int i = 0; i < n; i++) {
        RmEnt *e = &b->e[i];
        if (e->id == 'L') { double x = e->a[0], y = e->a[1]; e->a[0] = e->a[2]; e->a[1] = e->a[3]; e->a[2] = x; e->a[3] = y; }
        else if (e->id == 'A') { e->a[3] += e->a[4]; e->a[4] = -e->a[4]; }
    }
    b->ccw = !b->ccw;
}
void rm_b_reverse(RmB *b, int keep) { mg_reverse(b, keep); }
int rm_bound_intersect(const RmB *a, const RmB *b) { return intersect_any(a, b); }   /* pock_bound_intersect */

/* FUN_1805df8c0 l.308: the crossings of every pair of bounds whose boxes meet; a crossing strictly inside another island is switched off */
static int mg_inters(RmB *head, MgI **out, int *nout) {
    int nmax = 0, n = 0, cap = 500; *nout = 0; *out = NULL;
    for (RmB *b = head; b; b = b->next) if (nmax < b->n * 2) nmax = b->n * 2 + 10;
    if (!nmax) return 0;
    Ins *mem = calloc((size_t)nmax, sizeof(Ins)); MgI *r = calloc((size_t)cap, sizeof(MgI));
    for (RmB *a = head; a; a = a->next)
        for (RmB *b = a->next; b; b = b->next) {
            if (disjoint_box(a, b)) continue;
            short m; intersect(a, b, 0, nmax, mem, &m);
            if (cap < n + m) { while (cap < n + m) cap += 500; r = realloc(r, (size_t)cap * sizeof(MgI)); }
            for (int k = 0; k < m; k++) {
                char on = 1; double x = mem[k].p[0], y = mem[k].p[1], d = rm_nc_tol;
                for (RmB *c = head; c; c = c->next) {
                    if (!c->isl || c == a || c == b) continue;
                    if (x <= c->bbox[0] - d || d + c->bbox[2] <= x || y <= c->bbox[1] - d || d + c->bbox[3] <= y) continue;
                    if (rm_pt_in_ctour(c, x, y, d, rm_tol) == 1) { on = 0; break; }
                }
                r[n].on = on; r[n].p[0] = x; r[n].p[1] = y; r[n].b[0] = a; r[n].b[1] = b; r[n].r[0] = mem[k].ra; r[n].r[1] = mem[k].rb; n++;
            }
        }
    free(mem); *out = r; *nout = n;
    return 0;
}
/* FUN_1805df740 l.1025: the piece of bound rec->b[w] holding record rec->r[w] whose hit is nearest t = 0.5 */
static int mg_find(const MgI *rec, int w, RmB *list, RmB **pc, int *idx, double *dist) {
    *pc = NULL; *idx = -1; *dist = 10.0;
    for (RmB *b = list; b; b = b->next) {
        if (rec->b[w] != b->par) continue;
        for (int i = 0; i < b->n; i++)
            if (rec->r[w] == b->e[i].rec) { double d = fabs(0.5 - t_on_gt(rec->p, &b->e[i])); if (d < *dist) { *dist = d; *pc = b; *idx = i; } }
    }
    return *idx < 0 ? -1 : 0;
}
/* FUN_1805de0f0 l.5: copies of the bounds (+0x50 = the original, +1 cleared), broken at the crossings */
static void mg_break(RmB *head, RmB **out, const MgI *rec, int n) {
    *out = NULL;
    for (RmB *b = head; b; b = b->next) { RmB *c = rm_b_copy(b); c->par = b; c->broken = 0; rm_b_append(out, c); }
    for (int i = 0; i < n; i++) {
        if (!rec[i].on) continue;
        for (int w = 0; w < 2; w++) { RmB *pc; int k; double d; if (mg_find(&rec[i], w, *out, &pc, &k, &d) == 0) break_bound(out, pc, k, rec[i].p); }
    }
}
/* FUN_1805de9b0 l.56 (merge_pock_bounds only): the short pieces around a switched-off crossing */
static void mg_drop_off(const MgI *rec, int n, RmB **list) {
    for (int i = 0; i < n; i++) {
        if (rec[i].on) continue;
        for (int w = 0; w < 2; w++) { RmB *pc; int k; double d; mg_find(&rec[i], w, *list, &pc, &k, &d); if (d < 0.5 && pc) { unlink_b(list, pc); rm_b_free(pc); } }
    }
}
/* IsContourATrace pspiral_decomp_merge.c l.1363 (CalcIntersectRect / IsEqualRect mccore 1805e07d0 / 1805e1560) */
static int mg_trace(const RmB *a, const RmB *b, double t) {
    double lo[2], hi[2];
    lo[0] = a->bbox[0] <= b->bbox[0] ? b->bbox[0] : a->bbox[0]; hi[0] = b->bbox[2] <= a->bbox[2] ? b->bbox[2] : a->bbox[2];
    lo[1] = a->bbox[1] <= b->bbox[1] ? b->bbox[1] : a->bbox[1]; hi[1] = b->bbox[3] <= a->bbox[3] ? b->bbox[3] : a->bbox[3];
    if (hi[0] - lo[0] <= -fabs(t) || hi[1] - lo[1] <= -fabs(t)) return 0;
    if (hi[0] < lo[0]) { double s = lo[0]; lo[0] = hi[0]; hi[0] = s; }
    if (hi[1] < lo[1]) { double s = lo[1]; lo[1] = hi[1]; hi[1] = s; }
    if (t < fabs(a->bbox[0] - lo[0]) || t < fabs(a->bbox[1] - lo[1]) || t < fabs(a->bbox[2] - hi[0]) || t < fabs(a->bbox[3] - hi[1])) return 0;
    static const double u[4] = { 0.0, 0x1.5555555555555p-2, 0x1.5555555555555p-1, 1.0 };   /* 1/3, 2/3 (the table local_68 l.1381-1384) */
    for (int i = 0; i < a->n; i++)
        for (int k = i != 0; k < 4; k++) {
            double p[2], q[2]; p[0] = eval_gt(&a->e[i], u[k], &p[1]);
            nearest_bnd(b, p, q);
            if (t * t < (q[1] - p[1]) * (q[1] - p[1]) + (q[0] - p[0]) * (q[0] - p[0])) return 0;
        }
    return 1;
}
/* pt_to_pock_bound_dist mccore 1805e7510 */
static double mg_pt_dist(const double p[2], const RmB *b) {
    double best = 1e38;
    for (int i = 0; i < b->n; i++) {
        double t = t_on_gt(p, &b->e[i]);
        if (t < -1e-12 || 1.000000000001 < t) { if (t < 0.0) t = 0.0; else if (1.0 < t) t = 1.0; }
        double y, x = eval_gt(&b->e[i], t, &y), d = sqrt((x - p[0]) * (x - p[0]) + (y - p[1]) * (y - p[1]));
        if (d < best) best = d;
    }
    return best;
}
/* FUN_1805dec50 l.177: which broken pieces stay. facing = param_3 */
static void mg_keep(const RmB *orig, RmB **list, int facing) {
    for (RmB *b = *list; b; b = b->next) b->tr = 0;
    RmB *n;
    for (RmB *p = *list; p; p = n) {
        n = p->next;
        if (!p->broken) continue;
        int nonisl = p->isl == 0, outside = 0, inisl = 0; char tr = 0;
        for (const RmB *o = orig; o; o = o->next) {
            if (p->par == o || disjoint_box(o, p)) continue;
            int nest = contour_nested(p, o, rm_nc_tol), tc = mg_trace(p, o, rm_nc_tol);
            if (tc) { nest = 0; tr = 1; }
            if (o->isl == 0) { if (!nest && !tc) outside = 1; else nonisl = 1; }
            else if (nest) { inisl = 1; break; }
        }
        if (!facing ? (inisl || !nonisl) : (inisl || !nonisl || outside)) { unlink_b(list, p); rm_b_free(p); continue; }
        p->tr = (unsigned char)tr;
    }
}
/* LAB_1805de970 (qsort, 1e-12) and LAB_1805de930 (bsearch, 2 x nc_tol): by the key +0x18 */
static int mg_cmp_sort(const void *a, const void *b) { double d = ((const MgT *)a)->key - ((const MgT *)b)->key; if (fabs(d) < 1e-12) return 0; return d < 0.0 ? -1 : 1; }
static int mg_cmp_find(const void *a, const void *b) { double d = ((const MgT *)a)->key - ((const MgT *)b)->key; if (fabs(d) < rm_nc_tol * 2.0) return 0; return d < 0.0 ? -1 : 1; }
/* the MSVC CRT qsort / bsearch */
static void mg_swap(char *a, char *b, size_t w) { while (w--) { char t = *a; *a++ = *b; *b++ = t; } }
static void mg_qsort(void *base, size_t num, size_t w, int (*cmp)(const void *, const void *)) {
    char *lostk[64], *histk[64]; int sp = 0;
    if (num < 2) return;
    char *lo = base, *hi = (char *)base + w * (num - 1);
recurse:;
    size_t size = (size_t)(hi - lo) / w + 1;
    if (size <= 8) { char *h = hi; while (h > lo) { char *mx = lo; for (char *q = lo + w; q <= h; q += w) if (cmp(q, mx) > 0) mx = q; mg_swap(mx, h, w); h -= w; } }
    else {
        char *mid = lo + (size / 2) * w;
        if (cmp(lo, mid) > 0) mg_swap(lo, mid, w);
        if (cmp(lo, hi) > 0) mg_swap(lo, hi, w);
        if (cmp(mid, hi) > 0) mg_swap(mid, hi, w);
        char *l = lo, *h = hi;
        for (;;) {
            if (mid > l) { do l += w; while (l < mid && cmp(l, mid) <= 0); }
            if (mid <= l) { do l += w; while (l <= hi && cmp(l, mid) <= 0); }
            do h -= w; while (h > mid && cmp(h, mid) > 0);
            if (h < l) break;
            mg_swap(l, h, w);
            if (mid == h) mid = l;
        }
        h += w;
        if (mid < h) { do h -= w; while (h > mid && cmp(h, mid) == 0); }
        if (mid >= h) { do h -= w; while (h > lo && cmp(h, mid) == 0); }
        if (h - lo >= hi - l) { if (lo < h) { lostk[sp] = lo; histk[sp] = h; ++sp; } if (l < hi) { lo = l; goto recurse; } }
        else { if (l < hi) { lostk[sp] = l; histk[sp] = hi; ++sp; } if (lo < h) { hi = h; goto recurse; } }
    }
    if (--sp >= 0) { lo = lostk[sp]; hi = histk[sp]; goto recurse; }
}
static void *mg_bsearch(const void *key, void *base, size_t num, size_t w, int (*cmp)(const void *, const void *)) {
    char *lo = base, *hi = (char *)base + (num ? num - 1 : 0) * w;
    while (lo <= hi) {
        size_t half = num / 2;
        if (half) {
            char *mid = lo + ((num & 1) ? half : half - 1) * w; int r = cmp(key, mid);
            if (!r) return mid;
            if (r < 0) { hi = mid - w; num = (num & 1) ? half : half - 1; } else { lo = mid + w; num = half; }
        } else if (num) return cmp(key, lo) ? NULL : lo;
        else break;
    }
    return NULL;
}
/* FUN_1805defc0: the unused piece whose start is nearest pt (within t), flag = fl; when two are near, the one that can be continued (recursion one level deep) */
static void mg_next(MgT *T, int n, char fl, double t, const double pt[2], int rec, int *out) {
    int best = -1, second = -1, cnt = 0; double db = 1e38, ds = 1e38;
    *out = -1;
    MgT key; memset(&key, 0, sizeof key); key.key = pt[0] + pt[1];
    MgT *h = mg_bsearch(&key, T, (size_t)n, sizeof(MgT), mg_cmp_find);
    if (!h) return;
    int mid = h->idx, up = mid, lo = mid;
    for (int i = mid + 1; i < n && fabs(T[i].key - key.key) < t * 2.0; i++) up = i;
    for (int i = mid - 1; i >= 0 && fabs(T[i].key - key.key) < t * 2.0; i--) lo = i;
    for (int i = lo; i <= up; i++) {
        if (!T[i].on || T[i].fl != fl) continue;
        double d = sqrt((T[i].x - pt[0]) * (T[i].x - pt[0]) + (T[i].y - pt[1]) * (T[i].y - pt[1]));
        if (!(d < t)) continue;
        cnt++;
        if (db <= d) { if (d < ds || second == best) { ds = d; second = i; } }
        else if (best < 0) { best = i; ds = d; db = d; second = i; }
        else { second = best; ds = db; best = i; db = d; }
    }
    *out = best;
    if (1 < cnt && rec) {
        double e1[2], e2[2], s[2]; int a1, a2;
        rm_gt_ends(&T[best].pc->e[T[best].pc->n - 1], s, e1); rm_gt_ends(&T[second].pc->e[T[second].pc->n - 1], s, e2);
        mg_next(T, n, fl, t, e1, 0, &a1); mg_next(T, n, fl, t, e2, 0, &a2);
        if (a1 < 0 && -1 < a2) *out = second;
    }
}
/* FUN_1805dfc50 l.437: pieces joined end to start into loops */
static void mg_join(RmB **list, int second, double t, char *joined) {
    int n = 0, nfl = 0; RmB *done = NULL; *joined = 0;
    for (RmB *b = *list; b; b = b->next) if (b->broken) { n++; if (b->tr) nfl++; }
    if (!n || (second && !nfl)) return;
    MgT *T = calloc((size_t)n, sizeof(MgT)); int k = 0;
    for (RmB *b = *list; b; b = b->next) if (b->broken) { double e[2], s[2]; rm_gt_ends(&b->e[0], s, e); T[k].x = s[0]; T[k].y = s[1]; T[k].on = 1; T[k].fl = (char)b->tr; T[k].key = T[k].x + T[k].y; T[k].pc = b; k++; }
    mg_qsort(T, (size_t)n, sizeof(MgT), mg_cmp_sort);
    for (int i = 0; i < n; i++) T[i].idx = i;
    for (;;) {
        RmB *p = *list;
        while (p && !(p->broken && !p->tr)) p = p->next;
        if (!p) break;
        int ti = -1; for (int i = 0; i < n; i++) if (T[i].on && T[i].pc == p) { ti = i; break; }
        if (ti < 0) break;   /* FUN_1805dfc50 loops on: unreachable (every broken piece has a live record) */
        T[ti].on = 0; unlink_b(list, p); *joined = 1; p->next = p->prev = NULL;
        RmB *head = p, *tail = p; int cnt = 1;
        double st[2], en[2], s0[2], e0[2];
        rm_gt_ends(&p->e[0], st, e0); rm_gt_ends(&p->e[p->n - 1], s0, en);
        int nx; mg_next(T, n, (char)second, t, en, 1, &nx);
        while (nx >= 0) {
            RmB *q = T[nx].pc;
            double d5 = sqrt((en[0] - st[0]) * (en[0] - st[0]) + (en[1] - st[1]) * (en[1] - st[1]));
            if (d5 < t) {
                double qs[2], qe[2]; rm_gt_ends(&q->e[q->n - 1], qs, qe);
                double d6 = sqrt((qe[1] - st[1]) * (qe[1] - st[1]) + (qe[0] - st[0]) * (qe[0] - st[0]));
                if (d5 <= d6) break;
            }
            unlink_b(list, q); q->next = NULL; q->prev = tail; tail->next = q; tail = q; cnt++;
            T[nx].on = 0;
            { double qs[2]; rm_gt_ends(&q->e[q->n - 1], qs, en); }
            mg_next(T, n, (char)second, t, en, 1, &nx);
        }
        RmB *m = head;
        if (cnt > 1) {
            int tot = 0; for (RmB *b = head; b; b = b->next) tot += b->n;
            m = rm_b_new(tot); m->valid = 1; m->broken = 1; m->tr = 0; m->f45 = head->f45; m->isl = head->isl;
            int w = 0;
            for (RmB *b = head; b; b = b->next) {
                if (b->isl == 0) { m->isl = 0; m->f45 = b->f45; }
                for (int i = 0; i < b->n; i++) m->e[w++] = b->e[i];
            }
            RmB *b = head; while (b) { RmB *x = b->next; rm_b_free(b); b = x; }
        }
        m->next = m->prev = NULL;
        rm_b_append(&done, m);
    }
    free(T);
    if (done) { RmB *e = *list; if (!e) *list = done; else { while (e->next) e = e->next; e->next = done; done->prev = e; } }
}
/* FUN_1805dede0 l.245: open results and slivers removed; an island of the wrong direction becomes a non-island (+4 set) */
static void mg_finish(RmB **list, unsigned char want) {
    RmB *n;
    for (RmB *b = *list; b; b = n) {
        n = b->next; b->done = 0;
        double s0[2], e0[2], s1[2], e1[2], a; rm_gt_ends(&b->e[0], s0, e0); rm_gt_ends(&b->e[b->n - 1], s1, e1);
        int kill = 1;
        if (sqrt((e1[1] - s0[1]) * (e1[1] - s0[1]) + (e1[0] - s0[0]) * (e1[0] - s0[0])) < rm_nc_tol) {
            double p = pock_perim(b); b->len = p;
            unsigned char ccw = (unsigned char)pock_ccw(b, &a); b->ccw = ccw;
            if (b->isl && ccw != want && !is_point(b)) { b->isl = 0; b->done = 1; }
            double aa = fabs(a);
            if (p <= rm_nc_tol || 0.1 <= aa / (p * p) || rm_nc_tol * 0.5 <= aa / p) kill = 0;
        }
        if (kill) { unlink_b(list, b); rm_b_free(b); }
    }
}
/* FUN_1805de720 l.1269 */
static void mg_rel(const RmB *a, const RmB *b, int *inside, int *same) {
    *inside = 0; *same = 0;
    if (is_point(a) || is_point(b)) return;
    int on = 0, out = 0, in = 0, m = a->n < 5 ? a->n : 5;
    for (int i = 0; i < m; i++) {
        double p[2]; p[0] = eval_gt(&a->e[i], 0.5, &p[1]);
        if (mg_pt_dist(p, b) < rm_nc_tol) { on++; continue; }
        int s = rm_pt_in_ctour(b, p[0], p[1], rm_nc_tol, rm_tol);
        if (s == 1) in++; else if (s == 0) on++; else if (s == -1) out++;
    }
    if (!in && !out && 0 < on) {
        *same = 1;
        for (int i = 0; i < a->n; i++) { static const double t[3] = { 0.0, 0.3, 1.0 }; int stop = 0;
            for (int k = 0; k < 3; k++) { double p[2]; p[0] = eval_gt(&a->e[i], t[k], &p[1]); if (rm_nc_tol < mg_pt_dist(p, b)) { *same = 0; stop = 1; break; } }
            (void)stop; }
    }
    *inside = out < in;
}
/* FUN_1805dea90 l.91 (merge_pock_bounds only) */
static void mg_nested(RmB **list) {
    for (RmB *b = *list; b; b = b->next) { b->valid = 1; b->par = NULL; }
    for (RmB *a = *list; a; a = a->next) {
        if (!a->valid || is_point(a)) continue;
        for (RmB *c = *list; c; c = c->next) {
            if (!c->valid || is_point(c) || a == c || a->par == c || a == c->par) continue;
            if (a->isl && c->isl && disjoint_box(a, c)) continue;
            int in, same; mg_rel(a, c, &in, &same);
            if (!same) { if (in && (!a->par || is_ctour_nested(c, a->par))) a->par = c; }
            else { a->valid = 0; c->valid = 0; }
        }
    }
    for (RmB *a = *list; a; a = a->next)
        if (a->valid && !is_point(a) && a->par && a->par->isl) {         /* a non-island: only one that FUN_1805dede0 turned from an island (+4); an island: only of the other direction */
            int keep = a->isl == 0 ? a->done != 0 : a->ccw != a->par->ccw;
            if (!keep) a->valid = 0;
        }
    for (int t = 0; t < 2; t++) for (RmB *a = *list; a; a = a->next) if (a->valid && a->par && !a->par->valid) a->valid = 0;
    RmB *n; for (RmB *a = *list; a; a = n) { n = a->next; if (!a->valid) { unlink_b(list, a); rm_b_free(a); } }
}
/* FUN_1805e02e0 (mccore_pock.c l.3933): merge_pock_bounds (facing = 0) / merge_pock_bounds_facing_islands (facing = 1). The caller's bounds need entity boxes, bbox and ccw (rm_b_info, rm_b_bbox) */
int rm_merge_pock(RmB **head, int facing) {
    unsigned char want = 0; int rn = 0; short sdummy = 0; (void)sdummy;
    for (RmB *b = *head; b; b = b->next) if (b->isl == 0) want = (unsigned char)(b->ccw == 0);
    for (RmB *b = *head; b; b = b->next) {
        if (b->isl && b->ccw != want) mg_reverse(b, 0);
        rm_b_bbox(b);
        for (int i = 0; i < b->n; i++) b->e[i].rec = rn++;
    }
    MgI *rec; int nr; mg_inters(*head, &rec, &nr);
    RmB *list = NULL; mg_break(*head, &list, rec, nr);
    for (RmB *b = list; b; b = b->next) rm_b_bbox(b);
    if (!facing) mg_drop_off(rec, nr, &list);
    mg_keep(*head, &list, facing);
    for (int i = 0; i < 3; i++) { char j = 0; mg_join(&list, i == 1, rm_nc_tol, &j); if (i == 1 && !j) break; }
    rm_b_free_list(head); *head = list;
    mg_finish(head, want);
    if (!facing) mg_nested(head);
    free(rec);
    return 0;
}
/* the entry points of the Remachining port (cam2d_pock_rem.c): merge_pock_bounds = FUN_1805e02e0 with param_2 = 0, checked by remach_oracle.sh MERGEB and isl_face_oracle.sh */
int rm_merge_bounds(RmB **list) { return rm_merge_pock(list, 0); }
int rm_is_ctour_nested(const RmB *a, const RmB *b) { return is_ctour_nested(a, b); }
int rm_disjoint(const RmB *a, const RmB *b) { return disjoint(a, b); }
void rm_b_del(RmB **head, RmB *b) {                                  /* delete_pock_bound */
    if (b->prev) b->prev->next = b->next; else *head = b->next;
    if (b->next) b->next->prev = b->prev;
    b->next = b->prev = NULL; rm_b_free(b);
}
