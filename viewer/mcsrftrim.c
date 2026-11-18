/* mcsrftrim.c - Mastercam's face trimming core: mccore.dll MaFace::TrimToLoopsNew 0x1804196d0 and what it calls (MaFace::SplitToLoop,
 * MaLoop::TrimExtendToSurfEdges / ExtendToSurfEdges / Traverse / Active / MergeInnerLoop, MaFace::FindNeighbors), MaLoop::ReconstituteLoops,
 * MaTrimSurfToPlane::DoTrim / GenUVKeepPoint (formats/surfaces_trim_decode.md section 2). A transliteration of the decompiled functions named
 * at each function, on the MaCurve / MaLoop / MaFace objects of mcsrfint.c (mcsrfint_p.h; every object lives in mcsrfint.c's arena).
 * Oracle: tools/tests/srftrim_oracle.sh (tools/peloader --trim, mccore_trim.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mcsrfint_p.h"
#include "mcsrftrim.h"
#include "mcspline_refit.h"


/* ---- small growing arrays (CArray) in the arena ---- */
typedef struct { int n, cap; void **p; } PArr;
static void pa_add(PArr *a, void *x) { if (a->n == a->cap) { int c = a->cap ? a->cap * 2 : 10; void **q = ta(sizeof(void *) * c); if (a->n) memcpy(q, a->p, sizeof(void *) * a->n); a->p = q; a->cap = c; } a->p[a->n++] = x; }
static void pa_del(PArr *a, int i) { memmove(a->p + i, a->p + i + 1, sizeof(void *) * (a->n - i - 1)); a->n--; }   /* RemoveAt (FUN_180416db0) */
static int pa_has(const PArr *a, const void *x) { for (int i = 0; i < a->n; i++) if (a->p[i] == x) return 1; return 0; }

/* ---- the pcurves ---- */
/* ---- MaLoop / MaFace helpers ---- */
/* MaLoop::Reverse 0x180433e30: the pcurves' order reversed, each MaParameterCurve::Reverse 0x1804671a0 (the uv curve's Flip: MaCubicSpline::Flip
   0x1803f6d60 / MaNURBSpline::Flip 0x18044c770, range [TMax - t1, TMax - t0]; MaPolyLine::Reverse 0x180473460: points reversed, u' = 1 - u reversed) */
static void cv_flip(ICv *c) {
    double a = c->tmax - c->t1, b = c->tmax - c->t0;
    if (c->kind == 0) {
        int ns = c->nseg; double *N = ta(sizeof(double) * 12 * ns);
        for (int i = 0; i < ns; i++) {
            const double *g = c->C + 12 * (ns - 1 - i); double *o = N + 12 * i;
            for (int k = 0; k < 3; k++) {   /* algebraic -> geometric (P0 P1 D0 D1; MaCubicSpline::ConvertToGeometric 0x1803f3be0: P1 = c + d + b + a,
                                               D1 = b 2 + c + a 3), swapped and negated, back (ConvertToAlgebraic 0x1803f2c60) */
                double A = g[4 * k], B = g[4 * k + 1], Cc = g[4 * k + 2], D = g[4 * k + 3];
                double p0 = D, p1 = Cc + D + B + A, d0 = Cc, d1 = B * 2.0 + Cc + A * 3.0;
                double q0 = p1, q1 = p0, e0 = -d1, e1 = -d0;
                o[4 * k] = (q0 - q1) * 2.0 + e0 + e1; o[4 * k + 1] = ((q1 - q0) * 3.0 - e0 * 2.0) - e1; o[4 * k + 2] = e0; o[4 * k + 3] = q0;
            }
        }
        double *U = ta(sizeof(double) * (ns + 1)); for (int j = 0; j <= ns; j++) U[j] = c->U[ns] - c->U[ns - j];
        c->C = N; c->U = U;
    } else if (c->kind == 1) {
        double *K = ta(sizeof(double) * c->nk), *P = ta(sizeof(double) * 3 * c->ncp);
        for (int i = 0; i < c->nk; i++) K[i] = c->K[c->nk - 1] + c->K[0] - c->K[c->nk - 1 - i];
        for (int i = 0; i < c->ncp; i++) memcpy(P + 3 * i, c->P + 3 * (c->ncp - 1 - i), 24);
        if (c->W) { double *W = ta(sizeof(double) * c->ncp); for (int i = 0; i < c->ncp; i++) W[i] = c->W[c->ncp - 1 - i]; c->W = W; }
        c->K = K; c->P = P;
    } else refuse("MaCurve::Flip of this curve kind");
    cv_dirty(c); c->t0 = a; c->t1 = b; c->dt = b - a;
}
static void pc_reverse(ICv *q) {
    cv_flip(q->inner);
    int n = q->poly.n; double (*p)[2] = ta(sizeof(double[2]) * n), *u = ta(sizeof(double) * n);
    for (int i = 0; i < n; i++) { p[i][0] = q->poly.p[n - 1 - i][0]; p[i][1] = q->poly.p[n - 1 - i][1]; u[i] = 1.0 - q->poly.u[n - 1 - i]; }
    q->poly.p = p; q->poly.u = u; q->poly.hasbox = 0;
    cv_dirty(q); q->t0 = q->inner->t0; q->t1 = q->inner->t1; q->dt = q->inner->dt;
}
static void loop_reverse(ILoop *L) {
    for (int i = 0; i < L->n / 2; i++) { ICv *t = L->pc[i]; L->pc[i] = L->pc[L->n - 1 - i]; L->pc[L->n - 1 - i] = t; }
    for (int i = 0; i < L->n; i++) pc_reverse(L->pc[i]);
    if (L->lp) {   /* the cached loop polyline reversed too (its u list stays NULL for several pcurves) */
        int n = L->lp->n; double (*p)[2] = ta(sizeof(double[2]) * n); for (int i = 0; i < n; i++) { p[i][0] = L->lp->p[n - 1 - i][0]; p[i][1] = L->lp->p[n - 1 - i][1]; }
        double *u = NULL; if (L->lp->u) { u = ta(sizeof(double) * n); for (int i = 0; i < n; i++) u[i] = 1.0 - L->lp->u[n - 1 - i]; }
        L->lp->p = p; L->lp->u = u; L->lp->hasbox = 0;
    }
}
/* MaLoop::Direction 0x1804313b0: the loop polyline's winding number; |w| <= 3 pi / 2: -1, w < 0: 1 (clockwise), w > 0: 0 */
static int loop_dir(ILoop *L) { double w = pl_winding(loop_poly(L)); if (fabs(w) <= 4.71238898038469) return -1; return w < 0.0 ? 1 : (0.0 < w ? 0 : -1); }
/* MaLoop::IsUVActive 0x180433120 */
static int loop_active_uv(ILoop *L, const double *uv, int on_counts) { int r = uv_rel(L, uv); return on_counts ? (unsigned)(r - 1) < 2 : (r & ~2) == 0; }
static int loop_intersects(ILoop *L, ILoop *o) { if (!o) return 0; LID *d = loop_loop(L, o); return 0 < d->n; }   /* MaLoop::IntersectsWith 0x180432380 */
static void loop_startpt(const ILoop *L, double *uv) { const IPoly *p = &L->pc[0]->poly; uv[0] = p->p[0][0]; uv[1] = p->p[0][1]; }   /* MaLoop::StartPoint 0x180434f50 */
static void loop_endpt(const ILoop *L, double *uv) { const IPoly *p = &L->pc[L->n - 1]->poly; uv[0] = p->p[p->n - 1][0]; uv[1] = p->p[p->n - 1][1]; }   /* EndPoint 0x1804314e0 */

/* MaLoop(MaSurface *) 0x18042e630: a trimmed surface's face outer loop copied, else BuildUntrimmedSurfLoop 0x18042ff80 (the 4 range edges
   u0v0 -> u1v0 -> u1v1 -> u0v1 -> u0v0, each MaPolyLine::RefineByChordHeightToSurface(chord) + MaParameterCurve(surface, polyline, chord, chord));
   byte +0 set */
static MiFace *g_srf_face;   /* the surface's MaFace (MaSurface +0x70 with +0x11 set): a trimmed surface's own face, else NULL */
/* the faces of the surfaces (MaSurface +0x70 / +0x11) for MaLoop(MaSurface *), several surfaces (Trim to Surfaces) */
static struct { ISrf *s; MiFace *f; } g_sface[64]; static int g_nsface;
static void set_srf_face(ISrf *s, MiFace *f) { for (int i = 0; i < g_nsface; i++) if (g_sface[i].s == s) { g_sface[i].f = f; return; } if (g_nsface < 64) { g_sface[g_nsface].s = s; g_sface[g_nsface++].f = f; } }
static MiFace *srf_face(ISrf *s) { for (int i = 0; i < g_nsface; i++) if (g_sface[i].s == s) return g_sface[i].f; return NULL; }
static ILoop *surf_loop(ISrf *s) {
    MiFace *ff = srf_face(s); if (!ff && g_srf_face && g_srf_face->s == s) ff = g_srf_face;
    if (ff && !ff->f9) { ILoop *L = loop_copy(ff->outer); L->surf = 1; return L; }
    ILoop *L = ta(sizeof(ILoop)); L->n = 4; L->pc = ta(sizeof(ICv *) * 4); L->surf = 1;
    double c[4][2] = { { s->d[0], s->d[2] }, { s->d[1], s->d[2] }, { s->d[1], s->d[3] }, { s->d[0], s->d[3] } };
    for (int e = 0; e < 4; e++) {
        FPoly fp; fp.n = 2; fp.p = malloc(sizeof(double[2]) * 2); memcpy(fp.p[0], c[e], 16); memcpy(fp.p[1], c[(e + 1) & 3], 16);
        flo_refine(s, &fp, g_chord_dev);
        L->pc[e] = pc_poly_cubic(s, (const double (*)[2])fp.p, fp.n, g_chord_dev);
        free(fp.p);
    }
    return L;
}
/* MaFace(MaSurface *) 0x180414180: the outer loop MaLoop(surface), InitFaceData (the uv box) */
static MiFace *face_of_surf(ISrf *s) {
    MiFace *f = ta(sizeof(MiFace)); f->s = s; f->outer = surf_loop(s); f->inner = NULL; f->ninner = 0; face_uvminmax(f); return f;
}
/* MaFace(const MaFace &) 0x1804139c0: the loops copied, +8 / +9, the uv box */
static MiFace *face_copy(const MiFace *a) {
    MiFace *f = ta(sizeof(MiFace)); *f = *a; f->outer = loop_copy(a->outer); f->inner = ta(sizeof(ILoop *) * (a->ninner ? a->ninner : 1));
    for (int i = 0; i < a->ninner; i++) f->inner[i] = loop_copy(a->inner[i]);
    return f;
}
static void face_set_outer(MiFace *f, ILoop *L) { if (L) { f->outer = L; face_uvminmax(f); } }   /* MaFace::SetOuterLoop 0x1804172f0 */
static void face_add_inner(MiFace *f, ILoop *L) {   /* MaFace::AddInnerLoop 0x1804145f0 */
    ILoop **n = ta(sizeof(ILoop *) * (f->ninner + 1)); if (f->ninner) memcpy(n, f->inner, sizeof(ILoop *) * f->ninner); n[f->ninner++] = L; f->inner = n;
}
static void face_set_inners(MiFace *f, const PArr *a) {   /* MaFace::SetInnerLoops(CArray &) 0x180417130 (after DeleteInnerLoops) */
    f->ninner = a->n; f->inner = ta(sizeof(ILoop *) * (a->n ? a->n : 1)); for (int i = 0; i < a->n; i++) f->inner[i] = a->p[i];
}
static int face_valid(MiFace *f) {   /* MaFace::IsValid 0x180416200 */
    if (!(1e-06 < f->uvbox[1] - f->uvbox[0] && 1e-06 < f->uvbox[3] - f->uvbox[2] && f->s)) return 0;
    if (!loop_valid(f->outer)) return 0;
    for (int i = 0; i < f->ninner; i++) if (!loop_valid(f->inner[i])) return 0;
    return 1;
}

/* ---- MaLoop: extend, trim, traverse ---- */
/* MaLoop::ExtendToSurfEdges 0x180431510: mode 0 the start, 1 the end, 2 both: a straight (u, v) piece from the end along its tangent to the
   surface's boundary (MaSurface::BoundaryClosestToUVDirection, MaCubicCurve::MakeLinear, MaParameterCurve(surface, curve, FacetTol)) */
static ILoop *loop_extend(ILoop *L, int mode) {
    int cnt = 0; if (0 <= mode) { if (mode < 2) cnt = L->n + 1; else if (mode == 2) cnt = L->n + 2; }
    ICv **pc = ta(sizeof(ICv *) * (cnt > 0 ? cnt : 1)); int k = 0; ISrf *s = loop_srf(L); double tol = L->pc[0]->tol;   /* MaLoop::FacetTol 0x1804319f0 */
    if ((mode & ~2) == 0) {
        ICv *a = L->pc[0]; double o[9]; cv_eval(a, 1, a->t0, o);
        double P[3] = { o[0], o[1], o[2] }, D[3] = { o[3], o[4], o[5] }, l = sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
        if (l == 0.0) D[0] = D[1] = D[2] = 0.0; else { D[0] /= l; D[1] /= l; D[2] /= l; }
        double dir[2] = { D[0] * -1.0, D[1] * -1.0 }, hit[2]; int e; bound_dir(s, P, dir, &e, hit);
        double h3[3] = { hit[0], hit[1], 0 };
        pc[k++] = pc_wrap(s, cubic_line(h3, P, s->metric), tol);
    }
    for (int i = 0; i < L->n; i++) pc[k++] = cv_copy(L->pc[i]);
    if ((unsigned)(mode - 1) < 2) {
        ICv *b = L->pc[L->n - 1]; double o[9]; cv_eval(b, 1, b->t1, o);
        double P[3] = { o[0], o[1], o[2] }, D[3] = { o[3], o[4], o[5] }, l = sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
        if (l == 0.0) D[0] = D[1] = D[2] = 0.0; else { D[0] /= l; D[1] /= l; D[2] /= l; }
        double dir[2] = { D[0], D[1] }, hit[2]; int e; bound_dir(s, P, dir, &e, hit);
        double h3[3] = { hit[0], hit[1], 0 };
        pc[k++] = pc_wrap(s, cubic_line(P, h3, s->metric), tol);
    }
    return loop_new(pc, cnt);
}
/* MaLoop::TrimExtendToSurfEdges 0x180436220: the open cut against MaLoop(Surface) (byte +0: the surface's range): trimmed to the parts inside
   and, when extending is allowed (MaFace +8), the ends inside extended to the boundary. 0 = *pn loops in *out, 1 = none, 2 = would need extending */
#ifdef MI_TRACE
static int trim_extend_raw(ILoop *L, int ext, int *pn, ILoop ***out);
static int trim_extend(ILoop *L, int ext, int *pn, ILoop ***out) { int r = trim_extend_raw(L, ext, pn, out); if (mi_ztr()) { fprintf(mi_trf, "ZE %d %d %d\n", r, ext, *pn); if (r == 0) for (int i = 0; i < *pn; i++) mi_zl(" l", (*out)[i]); } return r; }
#define Z_TRIM_EXTEND trim_extend_raw
#else
#define Z_TRIM_EXTEND trim_extend
#endif
static int Z_TRIM_EXTEND(ILoop *L, int ext, int *pn, ILoop ***out) {
    *pn = 0; *out = NULL;
    ISrf *s = loop_srf(L); ILoop *B = surf_loop(s);
    LID *X = loop_loop(L, B); int n = X->n;
    double sp[2], ep[2]; loop_startpt(L, sp); loop_endpt(L, ep);
    int r1 = uv_rel(B, sp), r2 = uv_rel(B, ep), mode = 0;
    #define ONE(loop) do { *pn = 1; *out = ta(sizeof(ILoop *)); (*out)[0] = (loop); return 0; } while (0)
    if (n == 0) {
        if (r1 == 1) return 1;
        if (r1 == 2) {
            if (r2 == 1) return 1;
            if (r2 == 2) { if (sample_start(L, B) == 0) ONE(loop_copy(L)); return 1; }
            mode = 1;   /* r2 == 0 */
        } else {   /* r1 == 0 */
            if (r2 == 0) mode = 2; else if (r2 != 2) return 1; else mode = 0;
        }
        if (!ext) return 2;
        ONE(loop_extend(L, mode));
    }
    if (n == 1) {
        if (r1 == 2) {
            if (r2 == 2) ONE(loop_copy(L));
            if (r2 == 1) return 1;
            if (r2 == 0) { if (!ext) return 2; ONE(loop_extend(L, 1)); }
        } else if (r1 == 1) { if (r2 == 2) return 1; }
        else if (r1 == 0 && r2 == 2) { if (!ext) return 2; ONE(loop_extend(L, 0)); }
        if (!ext) return 2;
        LTD *d = td_new(2);
        if (r1 == 1 && r2 == 0) { d->a[0] = X->a[0]; d->t[0] = X->ta[0]; d->a[1] = L->n - 1; d->t[1] = L->pc[L->n - 1]->t1; }
        else if (r1 == 0 && r2 == 1) { d->a[0] = 0; d->t[0] = L->pc[0]->t0; d->a[1] = X->a[0]; d->t[1] = X->ta[0]; }
        else return 1;
        int np = 0; ILoop **R = NULL;
        if (loop_trim(L, d, &np, &R, 1) != 0) return 1;
        ONE(loop_extend(R[0], (r1 == 1 && r2 == 0) ? 1 : 0));
    }
    int cnt = n + (r1 == 0) + (r2 == 0), j0 = 0, j1 = n;
    LTD *d = td_new(cnt);
    if (n == 2 && r1 == 2 && r2 == 2) {
        int q = sample_start(L, B);
        if (q == 0) ONE(loop_copy(L));
        if (!(1 < (unsigned)(q - 1))) return 1;   /* q 1 or 2 */
    }
    if (r1 == 2 && sample_start(L, B) == 1) { j0 = 1; d->n--; }
    if (r2 == 2 && sample_end(L, B) == 1) { j1--; d->n--; X->n--; }
    int k = 0;
    if (r1 == 0) { d->a[0] = 0; d->t[0] = L->pc[0]->t0; k = 1; }
    for (int j = j0; j < j1; j++) { d->a[k] = X->a[j]; d->t[k] = X->ta[j]; k++; }
    if (r2 == 0) { d->a[k] = L->n - 1; d->t[k] = L->pc[L->n - 1]->t1; }
    int np = 0; ILoop **R = NULL;
    if (loop_trim(L, d, &np, &R, 1) != 0) return 1;
    if (r1 == 0) R[0] = loop_extend(R[0], 0);
    if (r2 == 0) R[np - 1] = loop_extend(R[np - 1], 1);
    *pn = np; *out = R; return 0;
    #undef ONE
}
/* MaLoop::Traverse 0x180435ac0: this (an open cut whose ends lie on the closed loop o, within 0.001) closed with the part of o from the cut's end
   to its start (MaLoop::Trim of o, through o's seam when the start comes first); 3 when an end is off o */
#ifdef MI_TRACE
static int loop_traverse_raw(ILoop *L, ILoop *o, ILoop **out);
static int loop_traverse(ILoop *L, ILoop *o, ILoop **out) { int r = loop_traverse_raw(L, o, out); if (mi_ztr()) { fprintf(mi_trf, "ZV %d\n", r); if (r == 0) mi_zl(" l", *out); } return r; }
#define Z_LOOP_TRAVERSE loop_traverse_raw
#else
#define Z_LOOP_TRAVERSE loop_traverse
#endif
static int Z_LOOP_TRAVERSE(ILoop *L, ILoop *o, ILoop **out) {
    *out = NULL;
    double sp[2], ep[2], d0, d1, f[6], t0 = 0, t1 = 0; int i0, i1; loop_startpt(L, sp); loop_endpt(L, ep);
    loop_pmd(o, sp, &d0, &i0, f, &t0); loop_pmd(o, ep, &d1, &i1, f, &t1);
    if (0.001 < d0 || 0.001 < d1) return 3;
    LTD *d; int last = o->n - 1;
    if (i0 < i1 || (i1 == i0 && t0 < t1)) {
        d = td_new(4); d->a[0] = i1; d->t[0] = t1; d->a[1] = last; d->t[1] = o->pc[last]->t1; d->a[2] = 0; d->t[2] = o->pc[0]->t0; d->a[3] = i0; d->t[3] = t0;
    } else { d = td_new(2); d->a[0] = i1; d->t[0] = t1; d->a[1] = i0; d->t[1] = t0; }
    int np = 0; ILoop **R = NULL; loop_trim(o, d, &np, &R, 1);
    int cnt = L->n; for (int i = 0; i < np; i++) cnt += R[i]->n;
    ICv **pc = ta(sizeof(ICv *) * (cnt ? cnt : 1)); int k = 0;
    for (int i = 0; i < L->n; i++) pc[k++] = cv_copy(L->pc[i]);
    for (int i = 0; i < np; i++) for (int j = 0; j < R[i]->n; j++) pc[k++] = cv_copy(R[i]->pc[j]);
    *out = loop_new(pc, cnt); return 0;
}
/* MaLoop::SplitToLoop 0x180434e70: the two closed loops on either side of the cut (Traverse, then the cut reversed and Traverse again) */
static int loop_split(ILoop *L, ILoop *o, PArr *res) {
    res->n = 0; ILoop *two[2] = { NULL, NULL };
    for (int i = 0; i < 2; i++) { if (i == 1) loop_reverse(L); if (loop_traverse(L, o, &two[i]) == 3) return 3; }
    pa_add(res, two[0]); pa_add(res, two[1]); return 0;
}
/* MaLoop::Active 0x18042efc0 (param_2 false): both traversals (the cut reversed for the second); the one whose region holds the keep (u, v)
   (MaLoop::IsUVActive); both or neither: by the distances to the keep point (on_counts: the first unless it is the nearer, as read) */
static int loop_active(ILoop *L, ILoop *o, int one, const double *keep, int on_counts, ILoop **out) {
    *out = NULL;
    if (one) { int r = loop_traverse(L, o, out); if (r == 3) { *out = loop_copy(o); return 3; } return 0; }
    ILoop *t[2] = { NULL, NULL };
    for (int i = 0; i < 2; i++) { if (i == 1) loop_reverse(L); if (loop_traverse(L, o, &t[i]) == 3) { *out = loop_copy(o); return 3; } }
    int a0 = loop_active_uv(t[0], keep, on_counts), a1 = loop_active_uv(t[1], keep, on_counts); ILoop *pick = NULL;
    if (a0 == 1 && a1 == 0) pick = t[0];
    else if (a0 == 0 && a1 == 1) pick = t[1];
    else {
        double d0, d1, f[6], tt = 0; int k; loop_pmd(t[0], keep, &d0, &k, f, &tt); loop_pmd(t[1], keep, &d1, &k, f, &tt);
        if (on_counts) pick = d0 < d1 ? t[1] : t[0];
        else pick = d1 <= d0 ? t[1] : t[0];
    }
    *out = loop_copy(pick); return 0;
}
/* MaLoop::MergeInnerLoop 0x1804333c0: this trimmed to o (TrimToLoop with param_2 true), each piece traversed around the running merged copy of o */
static int loop_merge_inner(ILoop *L, ILoop *o, LID *X, ILoop **out) {
    *out = NULL; int n = 0; ILoop **R = NULL;
    if (trim_to_loop(L, o, 1, X, &n, &R) != 0) return 1;
    ILoop *m = loop_copy(o);
    for (int i = 0; i < n; i++) { ILoop *t = NULL; if (loop_traverse(R[i], m, &t) != 3) m = t; }
    if (m) *out = m;
    return m == NULL;
}
/* MaCurve::SharesBothEndPointsOpenOrClosed 0x180408da0 (squared distances against tol^2) */
static int cv_shares_both(ICv *a, ICv *b, double tol, int *rev) {
    double as[9], ae[9], bs[9], be[9]; *rev = 0; cv_eval(a, 0, a->t0, as); cv_eval(a, 0, a->t1, ae); cv_eval(b, 0, b->t0, bs); cv_eval(b, 0, b->t1, be);
    double t2 = tol * tol;
    double d3 = (bs[1] - as[1]) * (bs[1] - as[1]) + (bs[0] - as[0]) * (bs[0] - as[0]) + (bs[2] - as[2]) * (bs[2] - as[2]);
    double d2 = (be[1] - as[1]) * (be[1] - as[1]) + (be[0] - as[0]) * (be[0] - as[0]) + (be[2] - as[2]) * (be[2] - as[2]);
    if ((t2 <= d3 && t2 <= d2) || (t2 <= (bs[1] - ae[1]) * (bs[1] - ae[1]) + (bs[0] - ae[0]) * (bs[0] - ae[0]) + (bs[2] - ae[2]) * (bs[2] - ae[2]) &&
                                   t2 <= (be[1] - ae[1]) * (be[1] - ae[1]) + (be[0] - ae[0]) * (be[0] - ae[0]) + (be[2] - ae[2]) * (be[2] - ae[2]))) return 0;
    if (d3 < d2) *rev = 1;
    return 1;
}
/* MaLoop::SharesPCurve 0x1804346f0: a pcurve with both ends within 1e-6 whose uv curves are of the same type and size (cubic: segments
   (+0x128), NURBS: control points (+300)) */
static int loop_shares_pc(ILoop *L, ICv *q) {
    for (int i = 0; i < L->n; i++) {
        int rev; ICv *a = L->pc[i];
        if (!cv_shares_both(a, q, 1e-06, &rev)) continue;
        ICv *x = a->inner, *y = q->inner; if (x->kind != y->kind) continue;
        if (x->kind == 0 ? x->nseg == y->nseg : x->kind == 1 ? x->ncp == y->ncp : 0) return 1;
    }
    return 0;
}
/* MaFace::FindNeighbors 0x180415510: the faces of all (not this, not in skip, not yet found) sharing a pcurve with this face's outer loop
   (their outer or inner loops) or with this face's inner loops (their outer loop) */
static void face_neighbors(MiFace *f, const PArr *all, const PArr *skip, PArr *found) {
    found->n = 0;
    if (all->n == 0) return;
    for (int i = 0; i < f->outer->n; i++) {
        ICv *q = f->outer->pc[i];
        for (int j = 0; j < all->n; j++) {
            MiFace *g = all->p[j]; if (g == f || pa_has(skip, g) || pa_has(found, g)) continue;
            if (loop_shares_pc(g->outer, q)) { pa_add(found, g); break; }
            for (int k = 0; k < g->ninner; k++) if (loop_shares_pc(g->inner[k], q)) { pa_add(found, g); break; }
        }
    }
    for (int m = 0; m < f->ninner; m++) for (int i = 0; i < f->inner[m]->n; i++) {
        ICv *q = f->inner[m]->pc[i];
        for (int j = 0; j < all->n; j++) {
            MiFace *g = all->p[j]; if (g == f || pa_has(skip, g) || pa_has(found, g)) continue;
            if (loop_shares_pc(g->outer, q)) { pa_add(found, g); break; }
        }
    }
}


/* ---- MaLoop::ReconstituteLoops 0x180433c60: the intersection loops' pcurves joined into chains (MaCompositeCurve::GenCompositeCurveArray
   0x1803e71b0), each chain a new MaLoop(surface, composite) 0x18042e6c0 ---- */
/* MaCubicSpline::Length 0x1803fa380 over [a, b]: per segment 9-point Gauss-Legendre (mccore .data 0x1812f07e0 nodes / 0x1812f0830 weights) of
   the segment's derivative over the covered local range */
static double cubic_len(const ICv *c, double a, double b) {
    static const double X[9] = { 0.015919880246187, 0.081984446336682, 0.193314283649705, 0.337873288298096, 0.5, 0.662126711701904, 0.806685716350295, 0.918015553663318, 0.984080119753813 };
    static const double W[9] = { 0.040637194180787, 0.090324080347428, 0.130305348201467, 0.156173538520001, 0.16511967750063, 0.156173538520001, 0.130305348201467, 0.090324080347428, 0.040637194180787 };
    double L = 0.0;
    for (int i = 0; i < c->nseg; i++) {
        double u0 = c->U[i], u1 = c->U[i + 1], hi = 1.0, lo;
        if (!(a < u1 && u0 < b)) continue;
        if (u0 < a || b < u1) {
            if (a < u0) { if (u0 < a || u1 < b) continue; hi = (b - u0) / (u1 - u0); lo = 0.0; }
            else if (b < u1) { lo = (a - u0) / (u1 - u0); hi = (b - u0) / (u1 - u0); }
            else lo = (a - u0) / (u1 - u0);
        } else lo = 0.0;
        const double *C = c->C + 12 * i;
        for (int k = 0; k < 9; k++) {
            double s = (hi - lo) * X[k] + lo, D[3];
            for (int j = 0; j < 3; j++) D[j] = (C[4 * j] * s * 1.5 + C[4 * j + 1]) * (s * 2.0) + C[4 * j + 2];   /* MaCubicCurve, parameter 0 .. 1 */
            L = sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]) * (hi - lo) * W[k] + L;
        }
    }
    return L;
}
/* MaCurve::GetClosedInfo 0x1804022b0 with a tolerance, cached as the DLL caches it (+0x23: the first tolerance asked wins) */
static int cv_closed_tol(ICv *c, double tol) {
    if (c->ci) return c->cl;
    double a[9], b[9], m[9], ts[3], te[3];
    c->ci = 1; c->cl = 0; c->per = 0;
    cv_eval(c, 1, c->t0, a); memcpy(ts, a + 3, 24); unit(ts);
    cv_eval(c, 1, c->t1, b); memcpy(te, b + 3, 24); unit(te);
    cv_eval(c, 0, (c->t1 + c->t0) * 0.5, m);
    double gap = dist3(a, b);
    if (tol < gap || dist3(a, m) <= gap) return 0;
    c->cl = 1; if (cos(0.1745329251994329) <= te[1] * ts[1] + te[0] * ts[0] + te[2] * ts[2]) c->per = 1;
    return 1;
}
static int cv_degen(ICv *c, double tol) { double o[9]; cv_eval(c, 1, (c->t1 + c->t0) * 0.5, o); return sqrt(o[3] * o[3] + o[4] * o[4] + o[5] * o[5]) <= tol; }   /* MaCurve::IsDegenerate 0x180404570 */
/* MaParameterCurve::MoveStartPoint 0x180467140 / MoveEndPoint 0x1804670e0: when not trimmed, the uv curve's end and the polyline's end */
static void pc_move_end(ICv *q, const double *v, int end) {
    if (q->dt != q->tmax - q->tmin) return;
    ICv *c = q->inner; cv_dirty(q); cv_dirty(c);
    if (c->kind == 0) cub_move(c->C + 12 * (end ? c->nseg - 1 : 0), v, end);
    else if (c->kind == 1) { int i = end ? c->ncp - 1 : 0; c->P[3 * i] = v[0]; c->P[3 * i + 1] = v[1]; c->P[3 * i + 2] = v[2]; }
    int i = end ? q->poly.n - 1 : 0; q->poly.p[i][0] = v[0]; q->poly.p[i][1] = v[1]; q->poly.hasbox = 0;
}
static double sqd(const double *a, const double *b) { return (a[1] - b[1]) * (a[1] - b[1]) + (a[0] - b[0]) * (a[0] - b[0]) + (a[2] - b[2]) * (a[2] - b[2]); }
static void cv_ends(ICv *c, double *s, double *e) { double o[9]; cv_eval(c, 0, c->t0, o); memcpy(s, o, 24); cv_eval(c, 0, c->t1, o); memcpy(e, o, 24); }
/* MaCurve::SnapEndPoints 0x1804090b0: each end of a curve (not degenerate, not closed within 1e-6 inch / 2.5e-5 mm) and the nearest end of another
   within (0, tol]: both moved to their middle */
static void snap_curves(ICv **C, int n, double tol, int metric) {
    double ct = metric ? 2.5e-05 : 1e-06; int *fl = calloc(n, sizeof(int));
    for (int i = 0; i < n; i++) if (cv_degen(C[i], ct) || cv_closed_tol(C[i], ct)) fl[i] = 1;
    for (int i = 0; i < n; i++) {
        if (fl[i] == 1) continue;
        for (int e = 0; e < 2; e++) {
            double P[3], s[3], x[3]; cv_ends(C[i], s, x); if (e) memcpy(P, x, 24); else memcpy(P, s, 24);
            double best = 1e31, M[3] = { 0, 0, 0 }; ICv *mc = NULL; int mstart = 0;
            for (int j = 0; j < n; j++) {
                if (j == i || fl[j] == 1) continue;
                double S[3], E[3]; cv_ends(C[j], S, E);
                double d = sqrt(sqd(S, P)); if (d <= best && best != d) { mstart = 1; memcpy(M, S, 24); mc = C[j]; best = d; }
                d = sqrt(sqd(E, P)); if (d <= best && best != d) { mstart = 0; memcpy(M, E, 24); mc = C[j]; best = d; }
            }
            if (0.0 <= best && best != 0.0 && best <= tol && mc) {
                double mid[3] = { (M[0] + P[0]) * 0.5, (M[1] + P[1]) * 0.5, (M[2] + P[2]) * 0.5 };
                pc_move_end(C[i], mid, e);
                pc_move_end(mc, mid, mstart ? 0 : 1);
            }
        }
    }
    free(fl);
}
/* MaCurve::SharesBothEndPoints / FindNeighbor 0x1804019b0 / FindLastCrvInChain / FindAllCrvsInChain / FindConnectedCurveSets 0x180401560 (as
   mcsrfint.c's mc_* on the model-space cuts), on the pcurves; the reversal is MaParameterCurve::Reverse */
static int pc_shares(ICv *a, ICv *b, double tol, int *rev) {
    double s[3], e[3], bs[3], be[3]; cv_ends(a, s, e); cv_ends(b, bs, be); double t2 = tol * tol; *rev = 0;
    double ss = sqd(bs, s), es = sqd(be, s), se = sqd(bs, e), ee = sqd(be, e);
    if ((ss < t2 || es < t2) && (se < t2 || ee < t2) && !cv_closed_tol(a, a->metric ? 0.0025 : 0.0001) && !cv_closed_tol(b, b->metric ? 0.0025 : 0.0001)) { if (ss < es) *rev = 1; return 1; }
    return 0;
}
static ICv *pc_neighbor(ICv *a, ICv **L, int n, double tol, const int *used) {
    int rev = 0;
    if (n == 2) for (int i = 0; i < 2; i++) if (L[i] != a && used[i] != 1 && pc_shares(a, L[i], tol, &rev)) { ICv *o = L[i]; if (rev) pc_reverse(o); return o; }
    double aS[3], aE[3]; cv_ends(a, aS, aE); double t2 = tol * tol, bestE = 1e31, bestS = 1e31; ICv *to_e = NULL, *to_s = NULL; int count = 0;
    for (int i = 0; i < n; i++) {
        ICv *o = L[i]; if (!o || o == a || used[i] == 1) continue;
        count++; double s[3], e[3]; cv_ends(o, s, e);
        double d1 = sqd(s, aE), d2 = sqd(e, aE); if (d2 <= d1) d1 = d2; if (d1 <= bestE && d1 != bestE) { bestE = d1; to_e = o; }
        d1 = sqd(s, aS); d2 = sqd(e, aS); if (d2 <= d1) d1 = d2; if (d1 <= bestS && d1 != bestS) { bestS = d1; to_s = o; }
    }
    if (((t2 + bestS <= bestE && bestE != t2 + bestS) && to_e == to_s) || (bestS <= bestE && t2 <= bestE && bestE != t2) || (bestE <= bestS && t2 <= bestE && bestE != t2) || to_e == NULL) return NULL;
    if (count == 1 && pc_shares(a, to_e, tol, &rev)) { if (rev) pc_reverse(to_e); return to_e; }
    { double s[3], e[3]; cv_ends(to_e, s, e); if (sqd(e, aE) < sqd(s, aE)) pc_reverse(to_e); }
    return to_e;
}
static int pc_idx(ICv **L, int n, const ICv *c) { for (int i = 0; i < n; i++) if (L[i] == c) return i; return -1; }
static ICv *pc_last(ICv *a, ICv **L, int n, double tol, const int *used) {
    ICv *q = pc_neighbor(a, L, n, tol, used); if (!q) return a;
    for (int i = 0; i < n; i++) { ICv *p = q; q = pc_neighbor(p, L, n, tol, used); if (!q || q == a) return p; }
    return NULL;
}
static int pc_all(ICv *a, ICv **L, int n, double tol, int *used, ICv ***out) {
    ICv **S = ta(sizeof(ICv *) * (n + 2)); int ns = 0, guard = 0; ICv *q = pc_neighbor(a, L, n, tol, used), *p;
    if (!q) { S[0] = a; used[pc_idx(L, n, a)] = 1; *out = S; return 1; }
    S[ns++] = a; p = q; q = pc_neighbor(p, L, n, tol, used);
    while (q && q != a) { int dup = 0; for (int i = 0; i < ns; i++) if (S[i] == p) dup = 1; if (!dup) S[ns++] = p; guard++; p = q; if (n < guard) break; q = pc_neighbor(q, L, n, tol, used); }
    { int dup = 0; for (int i = 0; i < ns; i++) if (S[i] == p) dup = 1; if (!dup) S[ns++] = p; }
    for (int i = 0; i < ns; i++) { int k = pc_idx(L, n, S[i]); if (k >= 0) used[k] = 1; }
    *out = S; return ns;
}
static int reconstitute(PArr *loops) {
    ISrf *s = loop_srf(loops->p[0]); int n = 0;
    for (int i = 0; i < loops->n; i++) n += ((ILoop *)loops->p[i])->n;
    ICv **C = ta(sizeof(ICv *) * (n ? n : 1)); int k = 0;
    for (int i = 0; i < loops->n; i++) { ILoop *L = loops->p[i]; for (int j = 0; j < L->n; j++) C[k++] = cv_copy(L->pc[j]); }
    double mn = 1e31, tol = 0.02;
    if (0 < n) {
        for (int i = 0; i < n; i++) {
            if (C[i]->inner->kind != 0) { refuse("MaCompositeCurve::GenCompositeCurveArray: the length of a non-cubic pcurve (MaNURBSpline::Length)"); return 1; }
            double l = cubic_len(C[i]->inner, C[i]->inner->t0, C[i]->inner->t1); if (l < mn) mn = l;   /* MaParameterCurve::Length 0x1804670c0 -> MaCurve::Length 0x180405530 */
        }
        tol = 0.02; if (mn < 0.02) tol = mn * 0.5;
    }
    snap_curves(C, n, g_chain_tol, s->metric);
    double ct = s->metric ? 2.5e-05 : 1e-06; int *used = calloc(n, sizeof(int)), nsets = 0, total = n; ICv ***sets = ta(sizeof(ICv **) * (n + 1)); int *cnt = ta(sizeof(int) * (n + 1));
    for (int i = 0; i < n; i++) if (cv_degen(C[i], ct)) { used[i] = 1; total--; }
    for (int i = 0; i < n; i++) if (used[i] != 1 && cv_closed_tol(C[i], ct)) { used[i] = 1; sets[nsets] = ta(sizeof(ICv *)); sets[nsets][0] = C[i]; cnt[nsets++] = 1; }
    for (int pass = 0;; pass++) {
        for (int i = 0; i < n; i++) {
            if (used[i] == 1) continue;
            ICv *last = pc_last(C[i], C, n, tol, used); if (!last) continue;
            pc_reverse(last);
            ICv **S; int ns = pc_all(last, C, n, tol, used, &S); sets[nsets] = S; cnt[nsets++] = ns;
        }
        int sum = 0; for (int i = 0; i < nsets; i++) sum += cnt[i];
        if (sum == total) break;
        if (n < pass) { free(used); refuse("MaCurve::FindConnectedCurveSets does not finish (the DLL loops)"); return 1; }
    }
    free(used);
    if (nsets == 0) return 1;
    loops->n = 0;
    for (int i = 0; i < nsets; i++) {
        ICv **pc = ta(sizeof(ICv *) * cnt[i]); for (int j = 0; j < cnt[i]; j++) { pc[j] = cv_copy(sets[i][j]); pc[j]->s = s; }
        pa_add(loops, loop_new(pc, cnt[i]));
    }
    return 0;
}
/* ---- MaFace::SplitToLoop 0x180417950: the face split by one loop (a closed loop inside, or a cut trimmed to the outer loop); out: the faces;
   0 ok, 2 a closed loop outside / inside a hole, 3 no split. param_3: MaLoop::IsUVActive's "on counts" (TrimToLoopsNew's !param_3), param_4:
   the interference check of the new outer loop against the inner loops (MaTrimSurf +0x41) ---- */
#ifdef MI_TRACE
static int face_split_raw(MiFace *f, ILoop *L, const double *keep, int on_counts, int interf, PArr *F);
static int face_split(MiFace *f, ILoop *L, const double *keep, int on_counts, int interf, PArr *F) { if (mi_ztr()) { fprintf(mi_trf, "ZF %d %d %.17g %.17g\n", on_counts, interf, keep[0], keep[1]); mi_zf(" F", f); mi_zl(" L", L); } int r = face_split_raw(f, L, keep, on_counts, interf, F); if (mi_ztr()) { fprintf(mi_trf, "ZFE %d %d\n", r, F->n); if (r == 0) for (int i = 0; i < F->n; i++) mi_zf(" F", F->p[i]); } return r; }
#define Z_FACE_SPLIT face_split_raw
#else
#define Z_FACE_SPLIT face_split
#endif
static int Z_FACE_SPLIT(MiFace *f, ILoop *L, const double *keep, int on_counts, int interf, PArr *F) {
    F->n = 0; int nfl = 0, cap = 16; char *flags = malloc(cap); int closed_case = 0;
    #define FLAG(v) do { if (nfl == cap) { cap *= 2; flags = realloc(flags, cap); } flags[nfl++] = (char)(v); } while (0)
    LID *X = loop_loop(L, f->outer); int c = X->n; if (0 < c && (c / 2) * 2 != c) { X->n--; c--; }   /* MakeEvenNumber 0x180438d40 */
    if (c < 2) {
        if (c == 0 && loop_closed(L)) {
            double sp[2]; loop_startpt(L, sp);
            if (uv_rel(f->outer, sp) != 1) {
                int act = loop_active_uv(L, keep, on_counts);
                for (int i = 0; i < f->ninner; i++) if (uv_rel(f->inner[i], sp) == 0 && !loop_intersects(L, f->inner[i])) { free(flags); return 2; }
                MiFace *a = face_copy(f), *b = face_copy(f); pa_add(F, a); pa_add(F, b);
                if (act) { face_set_outer(a, loop_copy(L)); FLAG(1); face_add_inner(b, loop_copy(L)); FLAG(0); }
                else { face_add_inner(a, loop_copy(L)); FLAG(0); face_set_outer(b, loop_copy(L)); FLAG(1); }
                closed_case = 1;
                goto post;
            }
        }
        if (c == 0) { free(flags); return 2; }
        free(flags); return 3;
    }
    {
        int np = 0; ILoop **R = NULL;
        if (trim_to_loop(L, f->outer, 0, X, &np, &R) != 0) { F->n = 0; free(flags); return 3; }
        pa_add(F, face_copy(f));
        for (int i = 0; i < np + 1; i++) FLAG(1);
        for (int k = 0; k < np; k++) {
            for (int u = 0; u < F->n; u++) {
                PArr two = { 0 };
                if (loop_split(R[k], ((MiFace *)F->p[u])->outer, &two) == 0) {
                    face_set_outer(F->p[u], two.p[0]);
                    pa_add(F, face_copy(f)); face_set_outer(F->p[F->n - 1], two.p[1]);
                    break;
                }
            }
        }
    }
post:
    for (int fi = 0; fi < F->n; fi++) {
        if (fi >= nfl || !flags[fi]) continue;
        MiFace *g = F->p[fi]; if (g->ninner <= 0) continue;
        PArr keepl = { 0 };
        for (int iv = 0; iv < g->ninner; iv++) {
            ILoop *Lc = loop_copy(g->inner[iv]);
            if (!interf) { pa_add(&keepl, Lc); continue; }
            if (loop_dir(Lc) != 1) loop_reverse(Lc);
            if (loop_dir(g->outer) != 0) loop_reverse(g->outer);
            LID *X2 = loop_loop(Lc, g->outer); int c2 = X2->n; if (0 < c2 && (c2 / 2) * 2 != c2) { X2->n--; c2--; }
            if (c2 < 2) { double sp[2]; loop_startpt(Lc, sp); if (uv_rel(g->outer, sp) == 0) pa_add(&keepl, Lc); continue; }
            int np = 0; ILoop **R = NULL;
            if (trim_to_loop(Lc, g->outer, 0, X2, &np, &R) == 0)
                for (int i = 0; i < np; i++) { ILoop *o = NULL; if (loop_active(R[i], g->outer, 0, keep, on_counts, &o) == 0) face_set_outer(g, o); }
        }
        face_set_inners(g, &keepl);
    }
    if (closed_case) for (int fi = 0; fi < F->n; fi++) {
        if (fi < nfl && flags[fi]) continue;
        MiFace *g = F->p[fi]; int n = g->ninner;
        if (n <= 1) continue;
        int merged = 0, more;
        do {
            PArr list = { 0 }; int cnt = merged; more = 0;
            ILoop *last = loop_copy(g->inner[n - 1]);
            for (int j = 0; j < g->ninner - 1; j++) {
                ILoop *Lj = loop_copy(g->inner[j]);
                if (!interf) { pa_add(&list, Lj); continue; }
                if (loop_dir(Lj) != loop_dir(last)) loop_reverse(last);
                LID *X3 = loop_loop(Lj, last); int c3 = X3->n; if (0 < c3 && (c3 / 2) * 2 != c3) { X3->n--; c3--; }
                if (c3 < 2) { double sp[2]; loop_startpt(Lj, sp); if (uv_rel(last, sp) != 0) pa_add(&list, Lj); continue; }
                ILoop *m = NULL;
                if (loop_merge_inner(Lj, last, X3, &m) == 0) {
                    j++;
                    if (j < g->ninner - 1) { more = 1; for (; j < g->ninner - 1; j++) pa_add(&list, loop_copy(g->inner[j])); }
                    pa_add(&list, m);
                    merged++; cnt = merged;
                    goto merged_out;
                }
            }
            more = 0;
        merged_out:
            if (cnt == 0) {
                int drop = 0;
                if (interf) for (int k = 0; k < g->ninner - 1; k++) { double sp[2]; loop_startpt(last, sp); if (uv_rel(g->inner[k], sp) == 0) { drop = 1; break; } }
                if (!drop) pa_add(&list, last);
            }
            face_set_inners(g, &list);
            n = g->ninner;
        } while (n != 0 && more);
    }
    free(flags);
    return 0;
    #undef FLAG
}

/* ---- MaFace::TrimToLoopsNew 0x1804196d0: the face (copied) split by each loop in turn (closed loops: SplitToLoop; open cuts: SnapEndPoints, the
   intersections with the face's outer loop, TrimExtendToSurfEdges, SplitToLoop per piece); one face left = nothing trimmed (1). Split model
   (param_4): every face; else the face holding (param_3) / not holding the keep (u, v), with keep multiple regions (param_5) every second ring
   of neighbours (FindNeighbors) ---- */
static int trim_to_loops_new(MiFace *self, ILoop **loops, int nl, const double *keep, int p3, int split, int keepm, int interf, int *nout, MiFace ***out) {
    *nout = 0; *out = NULL;
    PArr F = { 0 }; pa_add(&F, face_copy(self));
    for (int li = 0; li < nl; li++) {
        ILoop *L = loops[li]; PArr done = { 0 }; int closed = loop_closed(L);
        for (int fi = 0; fi < F.n; fi++) {
            MiFace *g = F.p[fi]; if (pa_has(&done, g)) continue;
            int direct = closed;
            if (!closed) {
                loop_snap(L, g->outer, loop_srf(L));
                LID *X = loop_loop(L, g->outer); int c = X->n, even = 0 < c ? (c / 2) * 2 : 0, viaext = 0;
                if (c < 1) {
                    double o[9]; cv_eval(L->pc[0], 0, L->pc[0]->t0, o);
                    if (!face_active(g, o) && g->ext == 0 && nl == 1) g->ext = 1;
                    viaext = 1;
                } else if (c != even) viaext = 1;
                if (viaext) {
                    int np = 0, r2 = 0; ILoop **R = NULL;
                    if (trim_extend(L, g->ext, &np, &R) == 0) {
                        for (int k = 0; k < np; k++) {
                            PArr sub = { 0 };
                            r2 = face_split(g, R[k], keep, !p3, interf, &sub);
                            if (r2 == 0) {
                                pa_del(&F, fi);
                                for (int q = 0; q < sub.n; q++) if (face_valid(sub.p[q])) { pa_add(&F, sub.p[q]); pa_add(&done, sub.p[q]); }
                            } else pa_add(&done, g);
                            if (fi >= F.n) { refuse("MaFace::TrimToLoopsNew: a face index past the list (the DLL's CArray bounds check)"); return 1; }
                            g = F.p[fi];
                        }
                        if (r2 == 0) break;
                    }
                    continue;
                }
                direct = 1;
            }
            if (direct) {
                PArr sub = { 0 };
                if (face_split(g, L, keep, !p3, interf, &sub) == 0) {
                    pa_del(&F, fi); fi--;
                    for (int q = 0; q < sub.n; q++) if (face_valid(sub.p[q])) { pa_add(&F, sub.p[q]); pa_add(&done, sub.p[q]); }
                } else pa_add(&done, g);
            }
        }
    }
    if (F.n == 1) return 1;
    if (split) {
        if (F.n == 0) return 1;
        *nout = F.n; *out = (MiFace **)F.p; return 0;
    }
    if (F.n < 1) { refuse("MaFace::TrimToLoopsNew: no face (the DLL's CArray bounds check)"); return 1; }
    MiFace *pick = NULL;
    for (int k = 0; k < F.n; k++) { int a = face_active(F.p[k], keep); if (p3 ? a : !a) { pick = F.p[k]; break; } }
    if (!pick) pick = F.p[0];
    PArr res = { 0 }; pa_add(&res, pick);
    if (keepm) {
        PArr skip = { 0 }, nb = { 0 }; face_neighbors(pick, &F, &skip, &nb);
        if (nb.n != 0) {
            pa_add(&skip, pick); for (int i = 0; i < nb.n; i++) if (!pa_has(&skip, nb.p[i])) pa_add(&skip, nb.p[i]);
            int toggle = 1;
            for (;;) {
                int count = 0; PArr next = { 0 };
                for (int i = 0; i < nb.n; i++) {
                    PArr tmp = { 0 }; face_neighbors(nb.p[i], &F, &skip, &tmp); count += tmp.n;
                    for (int j = 0; j < tmp.n; j++) {
                        if (pa_has(&res, tmp.p[j])) continue;
                        if (toggle) pa_add(&res, tmp.p[j]);
                        pa_add(&skip, tmp.p[j]); pa_add(&next, tmp.p[j]);
                    }
                }
                nb = next; toggle = !toggle;
                if (count < 1) break;
            }
        }
    }
    if (res.n == 0) return 1;
    *nout = res.n; *out = (MiFace **)res.p; return 0;
}

/* ---- the keep point ---- */
/* MaPolyLine::Sort2DPointsWrtVector 0x180473c20: by p . v, an insertion sort (stable) */
static void sort2d(double (*p)[2], int n, const double *v) {
    double *key = malloc(sizeof(double) * n); int *idx = malloc(sizeof(int) * n); double (*q)[2] = malloc(sizeof(double[2]) * n);
    for (int i = 0; i < n; i++) { key[i] = p[i][1] * v[1] + p[i][0] * v[0]; idx[i] = i; }
    for (int i = 1; i < n; i++) { double k = key[i]; int id = idx[i], j = i - 1; while (0 <= j && k < key[j]) { key[j + 1] = key[j]; idx[j + 1] = idx[j]; j--; } key[j + 1] = k; idx[j + 1] = id; }
    for (int i = 0; i < n; i++) { q[i][0] = p[idx[i]][0]; q[i][1] = p[idx[i]][1]; }
    memcpy(p, q, sizeof(double[2]) * n); free(key); free(idx); free(q);
}
/* MaFace::UVLineTrimPoints 0x18041a3c0: the (u, v) line's crossings with the face's loops (MaPolyLine::PlaneIntersection of each loop polyline
   with the plane through the line, normal (dy, -dx, 0)), duplicates within 1e-10 dropped, sorted along the line, an odd last dropped: pairs */
static int uvline_trim(MiFace *f, const double *A, const double *B, double (**out)[2]) {
    *out = NULL;
    double d[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, c = 0.0;   /* MaLine::DirCosines 0x18042ae30: end - start */
    double a0 = d[1] - d[2] * 0.0, b0 = d[2] * 0.0 - d[0], c0 = d[0] * 0.0 - d[1] * 0.0, l = sqrt(b0 * b0 + a0 * a0 + c0 * c0), a = 0, b = 0;
    if (l != 0.0) { a = a0 / l; b = b0 / l; c = c0 / l; }
    double pl[4] = { a, b, c, b * A[1] + a * A[0] + c * A[2] };
    int n = 0, cap = 64; double (*P)[2] = malloc(sizeof(double[2]) * cap);
    for (int li = 0; li < f->ninner + 1; li++) {
        ILoop *L = li == 0 ? f->outer : f->inner[li - 1];
        PPts q = { 0 }; pl_plane(loop_poly(L), pl, L->pc[0]->metric, &q);
        for (int i = 0; i < q.n; i++) {
            int dup = 0; for (int j = 0; j < n; j++) { double dy = q.p[i][1] - P[j][1], dx = q.p[i][0] - P[j][0]; if (sqrt(dy * dy + dx * dx) <= 1e-10) { dup = 1; break; } }
            if (dup) continue;
            if (n == cap) { cap *= 2; P = realloc(P, sizeof(double[2]) * cap); }
            P[n][0] = q.p[i][0]; P[n][1] = q.p[i][1]; n++;
        }
        free(q.p);
    }
    if (1 < n) { double v[2] = { d[0], d[1] }; sort2d(P, n, v); if ((n / 2) * 2 != n) n--; *out = P; return n / 2; }
    free(P); return 0;
}
/* MaTrimSurfToPlane::GenUVKeepPoint 0x1804cb210 (this[0x3f] = 1: GenKeepPtStatus 0x1804cb200): a point of a loop inside the face (the middle of
   its first crossing with the face's outer loop, a loop start inside, or 20 samples per pcurve), the (u, v) line across it along the normal of
   the loop (2 x the larger range each way), its pieces inside the face (UVLineTrimPoints) sampled 10 times (MaCurveOnSurface over the MaLine):
   the first sample more than 0.001 on the plane's positive side, nudged 0.001 x the line's (u, v) derivative inward at the piece's ends */
static void gen_keep_plane(ISrf *s, MiFace *face, ILoop **loops, int nl, const double *plane, double *keep) {
    keep[0] = keep[1] = 0.0;
    ICv *pc = loops[0]->pc[0]; double t = 0.0;
    for (int l = 0; l < nl; l++) {
        LID *X = loop_loop(loops[l], face->outer);
        if (X->n != 0) {
            if (X->n == 1) { pc = loops[l]->pc[X->a[0]]; t = (pc->t1 + pc->t0) * 0.5; t = (t + X->ta[0]) * 0.5; }
            else { int i0 = X->a[0], i1 = X->a[1]; pc = loops[l]->pc[i0]; if (i0 != i1) t = (pc->t1 + X->ta[0]) * 0.5; else t = (X->ta[1] + X->ta[0]) * 0.5; }
            goto eval;
        }
        double sp[2]; loop_startpt(loops[l], sp); int r = uv_rel(face->outer, sp);
        if (r == 1) { if (nl == 1) return; }
        else if (nl == 1) { pc = loops[l]->pc[0]; t = pc->t0; goto eval; }
    }
    {
        double t2 = 0.0;
        for (int l = 0; l < nl; l++) {
            t = t2;
            for (int i = 0; i < loops[l]->n; i++) {
                pc = loops[l]->pc[i]; t = pc->t0; double st = pc->dt;
                for (int k = 0; k < 20; k++) { double o[9]; cv_eval(pc, 0, t, o); if (face_active(face, o)) goto eval; t += st / 19.0; }
            }
            t2 = t;
        }
    }
eval:;
    double o[9]; cv_eval(pc, 1, t, o);
    double nx = o[4] - o[5] * 0.0, ny = o[5] * 0.0 - o[3], nz = o[3] * 0.0 - o[4] * 0.0, l = sqrt(ny * ny + nx * nx + nz * nz), ux = 0, uy = 0, uz = 0;
    if (l != 0.0) { ux = nx / l; uy = ny / l; uz = nz / l; }
    double h = s->d[4]; if (s->d[4] <= s->d[5]) h = s->d[5];
    double A[3] = { o[0] - ux * 2.0 * h, o[1] - uy * 2.0 * h, o[2] - uz * 2.0 * h }, B[3] = { ux * 2.0 * h + o[0], uy * 2.0 * h + o[1], uz * 2.0 * h + o[2] };
    double (*pairs)[2]; int np = uvline_trim(face, A, B, &pairs);
    if (np == 0) return;
    for (int i = 0; i < np; i++) {
        double a[3] = { pairs[2 * i][0], pairs[2 * i][1], 0 }, b[3] = { pairs[2 * i + 1][0], pairs[2 * i + 1][1], 0 }, du = b[0] - a[0], dv = b[1] - a[1];
        double tt = 0.0, st = 1.0 / 9.0;   /* the MaLine's range 0 .. 1 (MaCurve::MaCurve), MaCurveOnSurface::SetUVCurve */
        for (int k = 0; k < 10; k++) {
            double uv[2] = { du * tt + a[0], dv * tt + a[1] }, E[21];   /* MaLine::EvaluateCrv 0x18042aeb0: (end - start) t + start */
            ext_eval(s, 0, uv[0], uv[1], E);
            double dist = (E[1] * plane[1] + E[0] * plane[0] + E[2] * plane[2]) - plane[3];
            keep[0] = uv[0]; keep[1] = uv[1];
            if (0.001 < dist) {
                if (k == 0) { keep[0] = du * 0.001 + uv[0]; keep[1] = dv * 0.001 + uv[1]; }
                else if (k == 9) { keep[0] = uv[0] - du * 0.001; keep[1] = uv[1] - dv * 0.001; }
                free(pairs); return;
            }
            tt += st;
        }
    }
    free(pairs);
}

/* ---- MaTrimSurfToPlane::DoTrim 0x1804cacd0 on one surface ---- */
typedef struct { ISrf *s; PArr loops; } HookCtx;
/* MaSurfPlaneIntersect::BuildUnTrimmedIntersectionLoops 0x180495c00: CheckUVEndPointsClose 0x1804a3310, BuildUnTrimmedPartLoops 0x1804a3220
   (MaParameterCurve(the part surface, P, T, UV, 0, n, chord) 0x180464390, a MaLoop of it when valid) */
static void part_loop_hook(ISrf *piece, MaInt *m, void *ctx) {
    HookCtx *h = ctx; uv_ends_close(piece, m->UV, m->n);
    ICv *pc = pc_of_march(h->s, m->P, m->T, m->UV, m->n, g_chord_dev);
    if (pc_valid(pc)) { ICv **a = ta(sizeof(ICv *)); a[0] = pc; pa_add(&h->loops, loop_new(a, 1)); }
}
/* MaTrimSurfToPlane::CheckForSurfOnNegativeSideOfPlane 0x1804caba0: the face's first loop start (a trimmed surface) or the range's middle more
   than 1e-6 inch / 2.5e-5 mm on the negative side */
static int neg_side(ISrf *s, MiFace *face, const double *plane) {
    double uv[2];
    if (face) { double o[9]; ICv *pc = face->outer->pc[0]; cv_eval(pc, 0, pc->t0, o); uv[0] = o[0]; uv[1] = o[1]; }
    else { uv[0] = (s->d[1] + s->d[0]) * 0.5; uv[1] = (s->d[3] + s->d[2]) * 0.5; }
    double E[21]; is_eval(s, 0, uv[0], uv[1], E);
    double d = (E[1] * plane[1] + E[0] * plane[0] + E[2] * plane[2]) - plane[3], lim = s->metric ? -2.5e-05 : -1e-06;
    return d < lim;
}
static void poly_out(ILoop *L, MiPoly *o) { IPoly *p = loop_poly(L); o->n = p->n; o->p = malloc(sizeof(double[2]) * (p->n ? p->n : 1)); memcpy(o->p, p->p, sizeof(double[2]) * p->n); }
int mi_trim_to_plane(ISrf *s, int nl, const int *np, double (*const *lp)[2], double chord, double tol, const double plane[4], const double keep3[3],
                     int delun, int split, int keepm, int extend, int interf, MiTrimRes *R) {
    (void)tol; (void)keep3;
    memset(R, 0, sizeof *R); g_refused = NULL; g_srf_face = NULL;
    double chain = s->metric ? 0.002 : 0.0001;
    MiFace *F = NULL;
    if (nl > 0) {
        F = mi_face_build(s, nl, np, lp, chord, 30.0, chain);   /* LoadSurf(eptr, true, 30.0): MaFace(DB_LIST_ENT) 0x180413ba0 */
        if (!F) { R->refused = "the stored loops give no face"; return 1; }
        if (mi_face_reparam(F)) { mi_face_free(F); R->refused = "MaFace::CheckFaceToSurfaceDeltaUVRatios reparameterisation (NOT PORTED)"; return 1; }
        mi_face_subset(F, 0.05);   /* trim_surfs_to_a_plane: SubsetToFace(0.05) for a trimmed surface */
        F->ext = 1; F->f9 = 1;      /* InitFaceData 0x180415e10: +8 = +9 = 1 (the DB constructor's face keeps +9: IsOuterLoopTrimmed) */
        g_srf_face = F;
    } else { g_chord_dev = chord; g_chain_tol = chain; }
    int del = delun && !split;   /* MaTrimSurfToPlane ctor 0x1804caa00: +0x108 = Delete untrimmed and not Split */
    HookCtx h = { s, { 0 } };
    mi_march_hook = part_loop_hook; mi_march_ctx = &h;
    MiCurve *cv = NULL; int ncv = mi_surf_plane(s, plane, chord, chord, s->metric ? 12.0 : 0.5, s->metric ? 0.0025 : 1e-4, 1, &cv);
    mi_march_hook = NULL; mi_march_ctx = NULL;
    for (int i = 0; i < ncv; i++) { free(cv[i].k); free(cv[i].p); } free(cv);
    int ret = 0;
    if (h.loops.n == 0) { if (del && neg_side(s, F, plane)) R->deleted = 1; goto done; }
    if (1 < h.loops.n && reconstitute(&h.loops) != 0) goto done;   /* DoTrim: ReconstituteLoops failed: the surface keeps its count 0 */
    {
        MiFace *face = F ? F : face_of_surf(s);
        if (!F) { face->ext = 1; face->f9 = 0; g_srf_face = face; }   /* MaFace(MaSurface *) 0x180414180: InitFaceData (+8 +9 = 1, SetFace) then +9 = 0 */
        double keep[2]; gen_keep_plane(s, face, (ILoop **)h.loops.p, h.loops.n, plane, keep);
        R->has_keep = 1; R->keep[0] = keep[0]; R->keep[1] = keep[1];
        R->nin = h.loops.n; R->in = calloc(h.loops.n, sizeof(MiPoly)); for (int j = 0; j < h.loops.n; j++) poly_out(h.loops.p[j], &R->in[j]);
        face->ext = extend;
        int nf = 0; MiFace **faces = NULL;
        int r = trim_to_loops_new(face, (ILoop **)h.loops.p, h.loops.n, keep, 1, split, keepm, interf, &nf, &faces);
        if (r == 0) {
            R->count = nf; R->nfaces = nf; R->faces = calloc(nf, sizeof(MiTrimFace));
            for (int k = 0; k < nf; k++) {
                MiFace *g = faces[k]; R->faces[k].nl = g->ninner + 1; R->faces[k].l = calloc(g->ninner + 1, sizeof(MiPoly));
                poly_out(g->outer, &R->faces[k].l[0]); for (int j = 0; j < g->ninner; j++) poly_out(g->inner[j], &R->faces[k].l[j + 1]);
            }
        } else if (!split && del && neg_side(s, F ? F : face, plane)) R->deleted = 1;
    }
done:
    R->refused = g_refused;
    ta_free(); g_srf_face = NULL;
    return ret;
}

void mi_trim_res_free(MiTrimRes *R) {
    for (int k = 0; k < R->nfaces; k++) { for (int j = 0; j < R->faces[k].nl; j++) free(R->faces[k].l[j].p); free(R->faces[k].l); }
    free(R->faces); for (int j = 0; j < R->nin; j++) free(R->in[j].p); free(R->in);
    memset(R, 0, sizeof *R);
}
/* ======================================================================================================================================
 * Trim to Surfaces: MaTrimSurfToSurf::DoTrim 0x1804cbc30 with MaSurfSurfIntersect's trimmed-face branch of ComputeIntersections(false)
 * (BuildTrimmedModelSpaceIntersectionCurves 0x1804a2aa0: CheckUVEndPointsClose, BuildTrimmedModelSpacePartCurves 0x1804a2bd0 /
 * DriveCurves 0x1804a2560, UnionModelSpaceIntersectionCurves 0x1804a6320 -> GenDriveSrfUnionedLoop 0x1804a4350)
 * ====================================================================================================================================== */
/* CheckUVEndPointsClose 0x1804a3310, the drive surface's half (+0xc0, the march's (u, v) on B at +0x80): the ends made equal when within 1e-4
   inch / 0.0025 mm in space (<=, not equal), nearer than the middle, within 1e-4 in (u, v) */
static void uv_ends_close_b(ISrf *pb, double **UV, int n) {
    if (n <= 2) return;
    double E0[21], E1[21], Em[21]; is_eval(pb, 0, UV[0][0], UV[0][1], E0); is_eval(pb, 0, UV[n - 1][0], UV[n - 1][1], E1); is_eval(pb, 0, UV[n / 2][0], UV[n / 2][1], Em);
    double dx = E1[0] - E0[0], dy = E1[1] - E0[1], dz = E1[2] - E0[2], d1 = sqrt(dx * dx + dy * dy + dz * dz);
    double mx = Em[0] - E0[0], my = Em[1] - E0[1], mz = Em[2] - E0[2], d2 = sqrt(mx * mx + my * my + mz * mz);
    double du = UV[n - 1][0] - UV[0][0], dv = UV[n - 1][1] - UV[0][1], d3 = sqrt(du * du + dv * dv + 0.0), tol = pb->metric ? 0.0025 : 0.0001;
    if (d1 <= tol && tol != d1 && d1 < d2 && d3 < 0.0001) { UV[n - 1][0] = UV[0][0]; UV[n - 1][1] = UV[0][1]; }
}

typedef struct {
    ISrf *A, *B;              /* this +0xa8, +0xb0: the whole part / drive surfaces */
    PArr parts, drives, uparts, udrives;   /* +0x160 part loops, the drive loops (+0x1b0 count), the unioned part loops, +0x1e8 the unioned drive loops */
    int interrupted;
} SsCtx;
/* a model-space copy of the march's curve (+0x218 Copy), trimmed to the (u, v) ends of a pcurve piece mapped onto it (MaSurface::Evaluate,
   MaCurve::PointMinDist with hunting; a closed untrimmed curve: the ends within 1e-6 of the far end moved) - the body of BuildTrimmedModelSpace
   Part / DriveCurves for each piece of the loop trimmed to the face */
#ifdef MI_TRACE
static const ICv *zn_c;
static void zn(int hunt, const double *P, double t0, double t, double d) { static int on = -1; if (on < 0) on = getenv("TRIMSS_ZN") != NULL; if (on && mi_ztr()) { fprintf(mi_trf, "ZN %d %.17g %.17g %.17g t %.17g -> %.17g d %.17g\n", hunt, P[0], P[1], P[2], hunt ? 0.0 : t0, t, d);
    if (zn_c && getenv("TRIMSS_ZNC")) { const ICv *c = zn_c; fprintf(mi_trf, " NC %d %d K", c->ncp, c->nk); for (int k = 0; k < c->nk; k++) fprintf(mi_trf, " %.17g", c->K[k]); fprintf(mi_trf, " P"); for (int k = 0; k < c->ncp && k < 6; k++) fprintf(mi_trf, " %.17g %.17g %.17g %.17g", c->P[3 * k], c->P[3 * k + 1], c->P[3 * k + 2], c->W ? c->W[k] : 1.0); fprintf(mi_trf, "\n"); } } }
#define ZN(h, P, t0, t, d) zn(h, P, t0, t, d)
#define ZNC(c) (zn_c = (c))
#define ZEC_ON (mi_zec = getenv("TRIMSS_ZEC") != NULL)
#define ZEC_OFF (mi_zec = 0)
#else
#define ZEC_ON ((void)0)
#define ZNC(c) ((void)0)
#define ZEC_OFF ((void)0)
#define ZN(h, P, t0, t, d) ((void)0)
#endif
static ICv *model_piece(ISrf *srf, ICv *q, const MiCurve *whole) {
    double a[9], b[9], E1[21], E2[21], d, foot[6], t0 = 0.0, t1 = 0.0;
    cv_eval(q, 0, q->t0, a); cv_eval(q, 0, q->t1, b);
    is_eval(srf, 0, a[0], a[1], E1); is_eval(srf, 0, b[0], b[1], E2);
    ICv *c = cv_new(2, srf->metric); c->deg = whole->deg; c->ncp = whole->ncp; c->nk = whole->nk;
    c->K = ta(sizeof(double) * whole->nk); memcpy(c->K, whole->k, sizeof(double) * whole->nk); c->P = ta(sizeof(double) * 3 * whole->ncp); memcpy(c->P, whole->p, sizeof(double) * 3 * whole->ncp);
    c->tmin = whole->k[0]; c->tmax = whole->k[whole->nk - 1]; c->t0 = c->tmin; c->t1 = c->tmax; c->dt = c->t1 - c->t0;
    ZNC(c); ZEC_ON; cv_pmd(c, 1, E1, &d, foot, &t0); ZEC_OFF; ZN(1, E1, 0, t0, d); ZEC_ON; cv_pmd(c, 1, E2, &d, foot, &t1); ZEC_OFF; ZN(1, E2, 0, t1, d);
    if (cv_closed(c) && c->dt == (c->tmax - c->tmin)) {
        if (fabs(t0 - c->t1) <= 1e-06 && fabs(t0 - c->t1) != 1e-06) t0 = c->t0;
        if (fabs(t1 - c->t0) <= 1e-06 && fabs(t1 - c->t0) != 1e-06) t1 = c->t1;
        if (fabs(t1 - t0) <= 1e-06 && fabs(t1 - t0) != 1e-06) { t0 = c->t0; t1 = c->t1; }
    }
    cv_trim(c, t0, t1);
    return c;
}
static ICv *model_whole(const MiCurve *whole, int metric) {
    ICv *c = cv_new(2, metric); c->deg = whole->deg; c->ncp = whole->ncp; c->nk = whole->nk;
    c->K = ta(sizeof(double) * whole->nk); memcpy(c->K, whole->k, sizeof(double) * whole->nk); c->P = ta(sizeof(double) * 3 * whole->ncp); memcpy(c->P, whole->p, sizeof(double) * 3 * whole->ncp);
    c->tmin = whole->k[0]; c->tmax = whole->k[whole->nk - 1]; c->t0 = c->tmin; c->t1 = c->tmax; c->dt = c->t1 - c->t0; return c;
}
/* BuildTrimmedModelSpacePartCurves 0x1804a2bd0 / BuildTrimmedModelSpaceDriveCurves 0x1804a2560 on one side: a surface without a face gives the
   whole curve; the march's MaParameterCurve 0x180464390 as a MaLoop added to the loops (part / drive); with a face the loop is snapped to the
   face's outer loop (SnapEndPoints), a copy trimmed to the face (TrimToFace: 2 = nothing, other non-zero = the whole loop) and every piece's
   model curve added */
static void build_side(ISrf *srf, MaInt *m, double **UV, const MiCurve *whole, PArr *loops, PArr *curves) {
    MiFace *F = srf_face(srf);
    if (!F) pa_add(curves, model_whole(whole, srf->metric));
    ICv *pc = pc_of_march(srf, m->P, m->T, UV, m->n, g_chord_dev);
    if (!pc_valid(pc)) return;
    ICv **a = ta(sizeof(ICv *)); a[0] = pc; ILoop *L = loop_new(a, 1);
    pa_add(loops, L);
    if (!F) return;
    loop_snap(L, F->outer, srf);
    ILoop *cp = loop_copy(L); ILoop **res = NULL; int nres = 0;
    int r = trim_to_face(cp, F, &nres, &res);
    if (r != 0) { if (r == 2) return; res = ta(sizeof(ILoop *)); res[0] = loop_copy(L); nres = 1; }
    for (int l = 0; l < nres; l++) for (int i = 0; i < res[l]->n; i++) pa_add(curves, model_piece(srf, res[l]->pc[i], whole));
}
/* MaSurface::UBoundaryClosestToUVPoint 0x18048efa0 / VBoundaryClosestToUVPoint: the nearer of the two u (v) range edges as MaLines, 1 or 2 */
static int boundary_side(ISrf *s, int vdir, const double *uv) {
    double best = 1e31, q[3] = { uv[0], uv[1], 0 }; int side = 1;
    for (int i = 0; i < 2; i++) {
        double a[3], b[3], dist, t, ft[3];
        if (!vdir) { a[0] = b[0] = i ? s->d[1] : s->d[0]; a[1] = s->d[2]; b[1] = s->d[3]; }
        else { a[1] = b[1] = i ? s->d[3] : s->d[2]; a[0] = s->d[0]; b[0] = s->d[1]; }
        a[2] = b[2] = 0; maline_pmd(a, b, q, &dist, &t, ft);
        if (dist <= best && best != dist) { best = dist; side = i + 1; }
    }
    return side;
}
/* GenPartSrfUnionedLoop 0x1804a4940 (part: the part surface A +0xa8, the part loops, the unioned part loops +0x1c8 - DoTrim never reads them, made
   for the same steps as the DLL) / GenDriveSrfUnionedLoop 0x1804a4350 (B +0xb0, the drive loops, the unioned drive loops; the two decompiles are
   the same code): the latest loop trimmed between the surface points of the union curve's ends (MaSurface::PointMinDist; near a periodic seam
   the coordinate taken from the loop's end when they lie on different seam sides; MaLoop::PointMinDist; a closed loop: an end within 0.001 of
   the loop's own end snaps to it); the first piece of MaLoop::Trim added to the unioned loops */
static void gen_unioned(SsCtx *h, ICv *c, int part) {
    PArr *loops = part ? &h->parts : &h->drives;
    if (loops->n == 0) return;
    ILoop *L = loops->p[loops->n - 1]; ISrf *B = part ? h->A : h->B;
    if (B->obase) { refuse("GenPart / GenDriveSrfUnionedLoop on an offset surface (the base's PointMinDist retry) NOT PORTED"); return; }
    if (!B->edges) is_edges(B);
    int idx[2]; double tt[2];
    for (int e = 0; e < 2; e++) {
        double P[9], out[6], dist, u = 0, v = 0; cv_eval(c, 0, e ? c->t1 : c->t0, P);
        is_pmd(B, 1, P, out, &dist, &u, &v, 1); ZQ(1, P, u, v, dist);
        double proj[2] = { u, v }, lp[2]; if (e) loop_endpt(L, lp); else loop_startpt(L, lp);
        if ((B->per[0] || B->per[1]) && uv_near_seam(B, proj[0], proj[1])) {
            if (B->per[0] && boundary_side(B, 0, lp) != boundary_side(B, 0, proj)) proj[0] = lp[0];
            if (B->per[1] && boundary_side(B, 1, lp) != boundary_side(B, 1, proj)) proj[1] = lp[1];
        }
        double d, f[6], t = 0; int k; loop_pmd(L, proj, &d, &k, f, &t);
        if (loop_closed(L)) {
            double g = sqrt((lp[1] - proj[1]) * (lp[1] - proj[1]) + (lp[0] - proj[0]) * (lp[0] - proj[0]));
            if (g <= 0.001) { if (e == 0) { t = L->pc[0]->t0; k = 0; } else { t = L->pc[L->n - 1]->t1; k = L->n - 1; } }
        }
        idx[e] = k; tt[e] = t;
    }
    int i0 = idx[0], i1 = idx[1]; if (idx[1] < idx[0]) { i0 = idx[1]; i1 = idx[0]; }
    double a = tt[0], b = tt[1]; if (idx[0] == idx[1] && tt[1] < tt[0]) { a = tt[1]; b = tt[0]; }
    LTD *d = td_new(2); d->a[0] = i0; d->t[0] = a; d->a[1] = i1; d->t[1] = b;
    int np = 0; ILoop **R = NULL;
    if (loop_trim(L, d, &np, &R, 1) == 0 && np != 0 && R) pa_add(part ? &h->uparts : &h->udrives, R[0]);
}
static double dot_tan(const double *foot, const double *p) {   /* |T . unit(p - foot)|, the projection across the curve */
    double dx = p[0] - foot[0], dy = p[1] - foot[1], dz = p[2] - foot[2], l = sqrt(dx * dx + dy * dy + dz * dz);
    if (l == 0.0) dx = dy = dz = 0.0; else { dx /= l; dy /= l; dz /= l; }
    return fabs(foot[3] * dx + foot[4] * dy + foot[5] * dz);
}
/* UnionModelSpaceIntersectionCurves 0x1804a6320: for each part curve and drive curve, the part curve's range shared with the drive curve (the
   ends of each projected onto the other: within 1e-6 or perpendicular within 1e-4); a range above 1e-6: AddIntersectionCurve and the unioned
   loops (GenPartSrfUnionedLoop, then GenDriveSrfUnionedLoop) */
static void union_curves(SsCtx *h, PArr *pc, PArr *dc) {
    if (pc->n == 0 || dc->n == 0) return;
    for (int i = 0; i < pc->n; i++) {
        ICv *P = pc->p[i]; double Ps[9], Pe[9]; cv_eval(P, 0, P->t0, Ps); cv_eval(P, 0, P->t1, Pe);
        for (int j = 0; j < dc->n; j++) {
            ICv *D = dc->p[j]; double Ds[9], De[9]; cv_eval(D, 0, D->t0, Ds); cv_eval(D, 0, D->t1, De);
            double d1, d2, d3, d4, f1[6], f2[6], f3[6], f4[6], tA = 0, tB = 0, tC = 0, tD = 0;
            ZNC(P); ZEC_ON; cv_pmd(P, 1, Ds, &d1, f1, &tA); ZEC_OFF; ZN(1, Ds, 0, tA, d1); ZEC_ON; cv_pmd(P, 1, De, &d2, f2, &tB); ZEC_OFF; ZN(1, De, 0, tB, d2); ZNC(D); ZEC_ON; cv_pmd(D, 1, Ps, &d3, f3, &tC); ZEC_OFF; ZN(1, Ps, 0, tC, d3); ZEC_ON; cv_pmd(D, 1, Pe, &d4, f4, &tD); ZEC_OFF; ZN(1, Pe, 0, tD, d4);
            int b7, b8, b9 = 0, b10 = 0, go = 0; double t0 = -1.0, t1 = -1.0;
            if (1e-06 <= d1) {   /* 0x1804a6400.. (the order of the components as read) */
                double dx = Ds[0] - f1[0], dy = Ds[1] - f1[1], dz = Ds[2] - f1[2], l = sqrt(dy * dy + dx * dx + dz * dz);
                if (l == 0.0) dx = dy = dz = 0.0; else { dx /= l; dy /= l; dz /= l; }
                b7 = fabs(f1[3] * dx + f1[4] * dy + f1[5] * dz) < 0.0001;
            } else b7 = 1;
            if (1e-06 <= d2) b8 = dot_tan(f2, De) < 0.0001; else b8 = 1;
            if (1e-06 <= d3) { if (dot_tan(f3, Ps) < 0.0001 && !b7) b10 = 1; }
            else { b10 = 1; b7 = 0; }
            if (1e-06 <= d4) {
                if (dot_tan(f4, Pe) < 0.0001 && !b8) { b9 = 1; goto l7d1; }
                if (!b8 || !b10) goto l7d1;
                t0 = P->t0; t1 = tB; go = 1;
            } else {
                b9 = 1; b8 = 0;
            l7d1:
                t0 = tA;
                if (b7 && b9) { t1 = P->t1; go = 1; }
                else if (b10 && b9) { t0 = P->t0; t1 = P->t1; go = 1; }
                else if (b7 && b8) { t1 = tB; go = 1; }
            }
            if (go && t0 != -1.0 && t1 != -1.0 && 1e-06 < t1 - t0) {
                ICv *c = cv_copy(P); cv_trim(c, t0, t1);
                gen_unioned(h, c, 1); gen_unioned(h, c, 0);   /* AddIntersectionCurve, GenPartSrfUnionedLoop, GenDriveSrfUnionedLoop */
            }
        }
    }
}
static void ss_hook(ISrf *pa, ISrf *pb, MaInt *m, const MiCurve *whole, void *ctx) {
    SsCtx *h = ctx;
    uv_ends_close(pa, m->UV, m->n); uv_ends_close_b(pb, m->UV2, m->n);   /* CheckUVEndPointsClose 0x1804a3310 */
    PArr pc = { 0 }, dc = { 0 };
    build_side(h->A, m, m->UV, whole, &h->parts, &pc);
    build_side(h->B, m, m->UV2, whole, &h->drives, &dc);
    union_curves(h, &pc, &dc);
}
/* MaTrimSurfToSurf::GenKeepPtStatus 0x1804cc430: keep point A on the positive side of B's normal at its foot (MaSurface::PointMinDist) */
static int ss_keep_status(ISrf *B, const double *kA) {
    double out[6], dist, u = 0, v = 0; is_pmd(B, 1, kA, out, &dist, &u, &v, 1); ZQ(1, kA, u, v, dist);
    double n[3] = { out[3], out[4], out[5] }, l = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l == 0.0) n[0] = n[1] = n[2] = 0.0; else { n[0] /= l; n[1] /= l; n[2] /= l; }
    return 0.0 <= (n[1] * kA[1] + n[0] * kA[0] + n[2] * kA[2]) - (out[1] * n[1] + out[0] * n[0] + out[2] * n[2]);
}
/* MaTrimSurfToSurf::GenUVKeepPoint 0x1804cc5b0: one A surface: the keep point projected onto it; several: a point on a loop (the middle of its
   first crossing with the face's outer loop, or the start of the only / last loop inside), the (u, v) line across it, its pieces inside the face
   sampled 10 times: the first sample on the keep point's side of B (more than 0.001 from B's tangent plane at its foot), nudged at the ends */
static void gen_keep_ss(ISrf *s, MiFace *face, ILoop **loops, int nl, int nA, const double *kA, ISrf *B, int status, double *keep) {
    keep[0] = keep[1] = 0.0;
    if (nA == 1) { double out[6], dist, u = 0, v = 0; is_pmd(s, 1, kA, out, &dist, &u, &v, 1); ZQ(1, kA, u, v, dist); keep[0] = u; keep[1] = v; return; }
    ICv *pc = NULL; double t = 0.0; int found = 0;
    for (int l = 0; l < nl && !found; l++) {
        LID *X = loop_loop(loops[l], face->outer);
        if (X->n != 0) {
            if (X->n == 1) { pc = loops[l]->pc[X->a[0]]; t = (pc->t1 + pc->t0) * 0.5; t = (t + X->ta[0]) * 0.5; }
            else { int i0 = X->a[0], i1 = X->a[1]; pc = loops[l]->pc[i0]; if (i0 != i1) t = (pc->t1 + X->ta[0]) * 0.5; else t = (X->ta[1] + X->ta[0]) * 0.5; }
            found = 1; break;
        }
        double sp[2]; loop_startpt(loops[l], sp); int r = uv_rel(face->outer, sp);
        if (r == 1) { if (nl == 1) return; }
        else if (nl == 1 || l == nl - 1) { pc = loops[l]->pc[0]; t = pc->t0; found = 1; break; }
    }
    if (!found) return;
    double o[9]; cv_eval(pc, 1, t, o);
    double nx = o[4] - o[5] * 0.0, ny = o[5] * 0.0 - o[3], nz = o[3] * 0.0 - o[4] * 0.0, l = sqrt(ny * ny + nx * nx + nz * nz), ux = 0, uy = 0, uz = 0;
    if (l != 0.0) { ux = nx / l; uy = ny / l; uz = nz / l; }
    double h = s->d[4]; if (s->d[4] <= s->d[5]) h = s->d[5];
    double A[3] = { o[0] - ux * 2.0 * h, o[1] - uy * 2.0 * h, o[2] - uz * 2.0 * h }, Bp[3] = { ux * 2.0 * h + o[0], uy * 2.0 * h + o[1], uz * 2.0 * h + o[2] };
    double (*pairs)[2]; int np = uvline_trim(face, A, Bp, &pairs);
    if (np == 0) return;
    for (int i = 0; i < np; i++) {
        double a[2] = { pairs[2 * i][0], pairs[2 * i][1] }, b[2] = { pairs[2 * i + 1][0], pairs[2 * i + 1][1] }, du = b[0] - a[0], dv = b[1] - a[1], tt = 0.0, st = 1.0 / 9.0;
        for (int k = 0; k < 10; k++) {
            double uv[2] = { du * tt + a[0], dv * tt + a[1] }, E[21], out[6], dist, u = 0, v = 0;
            ext_eval(s, 0, uv[0], uv[1], E);
            is_pmd(B, 1, E, out, &dist, &u, &v, 1); ZQ(1, E, u, v, dist);
            double n[3] = { out[3], out[4], out[5] }, ln = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (ln == 0.0) n[0] = n[1] = n[2] = 0.0; else { n[0] /= ln; n[1] /= ln; n[2] /= ln; }
            double off = out[1] * n[1] + n[0] * out[0] + n[2] * out[2], d = (E[1] * n[1] + E[0] * n[0] + E[2] * n[2]) - off;
            keep[0] = uv[0]; keep[1] = uv[1];
            int take = status == 0 ? !(-0.001 < d) : !(d < 0.001);
            if (take) {
                if (k == 0) { keep[0] = du * 0.001 + uv[0]; keep[1] = dv * 0.001 + uv[1]; }
                else if (k == 9) { keep[0] = uv[0] - du * 0.001; keep[1] = uv[1] - dv * 0.001; }
                free(pairs); return;
            }
            tt += st;
        }
    }
    free(pairs);
}
static MiFace *in_face(MiTrimIn *x, double chord, double chain) {
#ifdef MI_TRACE
    if (mi_ztr() && getenv("TRIMSS_ZB")) for (int l = 0; l < x->nl; l++) { if (l > 0 && x->np[l] == 2) continue; uint64_t h = 0xcbf29ce484222325ull; for (int k = 0; k < x->np[l]; k++) { const uint8_t *b = (const uint8_t *)x->lp[l][k]; for (int q = 0; q < 16; q++) { h ^= b[q]; h *= 0x100000001b3ull; } } fprintf(mi_trf, "ZL %d %d %016llx\n", l, x->np[l], (unsigned long long)h); }
#endif
    MiFace *F = mi_face_build(x->s, x->nl, x->np, x->lp, chord, 30.0, chain);   /* LoadSurf(eptr, true, 30.0) */
    if (!F) return NULL;
    if (mi_face_reparam(F)) { refuse("MaFace::CheckFaceToSurfaceDeltaUVRatios reparameterisation (NOT PORTED)"); return NULL; }
    mi_face_subset(F, 0.05);   /* trim_surfs_to_surfs: SubsetToFace(0.05) for a trimmed surface */
    F->ext = 1; F->f9 = 1;
    return F;
}
static void res_faces(MiTrimRes *R, MiFace **faces, int nf) {
    R->count = nf; R->nfaces = nf; R->faces = calloc(nf, sizeof(MiTrimFace));
    for (int k = 0; k < nf; k++) {
        MiFace *g = faces[k]; R->faces[k].nl = g->ninner + 1; R->faces[k].l = calloc(g->ninner + 1, sizeof(MiPoly));
        poly_out(g->outer, &R->faces[k].l[0]); for (int j = 0; j < g->ninner; j++) poly_out(g->inner[j], &R->faces[k].l[j + 1]);
    }
}
int mi_trim_to_surfs(MiTrimIn *A, int nA, const double keepA[3], MiTrimIn *B, const double keepB[3], int trimA, int trimB, int split, int keepm,
                     int extend, double chord, MiTrimRes *RA, MiTrimRes *RB) {
    for (int i = 0; i < nA; i++) memset(&RA[i], 0, sizeof RA[i]);
    memset(RB, 0, sizeof *RB); g_refused = NULL; g_srf_face = NULL; g_nsface = 0;
    ISrf *sB = B->s; double chain = sB->metric ? 0.002 : 0.0001;
    g_chord_dev = chord; g_chain_tol = chain;
    int ret = 0;
    for (int i = 0; i <= nA; i++) {   /* the stored faces (LoadSurf) */
        MiTrimIn *x = i < nA ? &A[i] : B;
        if (x->nl > 0) { MiFace *F = in_face(x, chord, chain); if (!F) { if (!g_refused) refuse("the stored loops give no face"); goto done; } set_srf_face(x->s, F);
#ifdef MI_TRACE
            if (mi_ztr() && getenv("TRIMSS_ZB")) mi_zf("ZB", F);
#endif
        }
    }
    g_chord_dev = chord; g_chain_tol = chain;
    int status = ss_keep_status(sB, keepA);   /* vtable +0x20 GenKeepPtStatus */
    if (trimB && !srf_face(sB)) { MiFace *f = face_of_surf(sB); f->ext = 1; f->f9 = 0; set_srf_face(sB, f); }   /* MaFace(B) 0x180414180 */
    PArr ball = { 0 };
    for (int i = 0; i < nA; i++) {
        ISrf *sA = A[i].s; RA[i].count = 0;
        if (trimA && !srf_face(sA)) { MiFace *f = face_of_surf(sA); f->ext = 1; f->f9 = 0; set_srf_face(sA, f); }
        SsCtx h; memset(&h, 0, sizeof h); h.A = sA; h.B = sB;
        mi_ss_hook = ss_hook; mi_march_ctx = &h;
        MiCurve *cv = NULL; int ncv = mi_surf_surf(sA, sB, chord, chord, sB->metric ? 12.0 : 0.5, sB->metric ? 0.0025 : 1e-4, 1, &cv);   /* MaSurfSurfIntersect(NULL, NULL, chord, chord, 1e-4 / 0.0025, 0.5 / 12, true) */
        mi_ss_hook = NULL; mi_march_ctx = NULL;
        for (int k = 0; k < ncv; k++) { free(cv[k].k); free(cv[k].p); } free(cv);
        if (g_refused) goto done;
        if (h.parts.n == 0) continue;
        if (trimB) for (int k = 0; k < h.udrives.n; k++) pa_add(&ball, loop_copy(h.udrives.p[k]));
        if (!trimA) continue;
        PArr list = { 0 }; for (int k = 0; k < h.parts.n; k++) pa_add(&list, loop_copy(h.parts.p[k]));
        if (list.n == 0) continue;
        if (1 < list.n && reconstitute(&list) != 0) { if (g_refused) goto done; continue; }
        MiFace *face = srf_face(sA);
        double keep[2]; gen_keep_ss(sA, face, (ILoop **)list.p, list.n, nA, keepA, sB, status, keep);
        RA[i].has_keep = 1; RA[i].keep[0] = keep[0]; RA[i].keep[1] = keep[1];
        RA[i].nin = list.n; RA[i].in = calloc(list.n, sizeof(MiPoly)); for (int j = 0; j < list.n; j++) poly_out(list.p[j], &RA[i].in[j]);
        face->ext = extend;
        int nf = 0; MiFace **faces = NULL;
        if (trim_to_loops_new(face, (ILoop **)list.p, list.n, keep, 1, split, keepm, 1, &nf, &faces) == 0) res_faces(&RA[i], faces, nf);
        if (g_refused) goto done;
    }
    if (trimB && 0 < ball.n && (ball.n < 2 || reconstitute(&ball) == 0)) {
        MiFace *face = srf_face(sB);
        double out[6], dist, u = 0, v = 0; is_pmd(sB, 1, keepB, out, &dist, &u, &v, 1); ZQ(1, keepB, u, v, dist);   /* GetDriveSrfKeepUV 0x1804ccee0 */
        double keep[2] = { u, v };
        RB->has_keep = 1; RB->keep[0] = u; RB->keep[1] = v;
        RB->nin = ball.n; RB->in = calloc(ball.n, sizeof(MiPoly)); for (int j = 0; j < ball.n; j++) poly_out(ball.p[j], &RB->in[j]);
        face->ext = extend;
        int nf = 0; MiFace **faces = NULL;
        if (trim_to_loops_new(face, (ILoop **)ball.p, ball.n, keep, 1, split, keepm, 1, &nf, &faces) == 0) res_faces(RB, faces, nf);
    }
done:
    if (g_refused) { for (int i = 0; i < nA; i++) RA[i].refused = g_refused; RB->refused = g_refused; }
    ta_free(); g_nsface = 0;
    return ret;
}
/* debug (tools/tests/srftrim_test.c TRIM_SRFLOOP): MaLoop(MaSurface *) of the untrimmed surface, each pcurve's polyline and u list */
void mi_trim_dbg_srfloop(ISrf *s, double chord, void *fp) {
    FILE *o = fp; g_chord_dev = chord; g_srf_face = NULL; ILoop *L = surf_loop(s);
    for (int i = 0; i < L->n; i++) { IPoly *p = &L->pc[i]->poly; fprintf(o, "srfpc %d %d\n", i, p->n); for (int k = 0; k < p->n; k++) fprintf(o, "%.17g %.17g %.17g\n", p->p[k][0], p->p[k][1], p->u ? p->u[k] : -1.0); }
    ta_free();
}
