/* ifk_mcside.c - Mastercam side of Turn Profile / Silhouette Boundary (see ifk_mcside.h). Decompile:
 * decomp/mcmsurf_all.c (mucbMakeIffSiluetBndy 180231800 and the functions named below), decomp/surfrev/unitnormal.c
 * (UnitNormal), decomp/xform/mccore_xform.c (xform_pt), decomp/interfacial/mccore_wr_stl.c (facet_normal),
 * decomp/mastercam_acsave.c (world_to_view). Write-up: formats/interfacial.md 10. */
#include "ifk_mcside.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* FUN_18022feb0 + FUN_180252860 (3D box update: v < min -> min, max < v -> max, per axis) */
int ifk_stl_box(const IfkMesh *m, double box[6])
{
    box[0] = box[1] = box[2] = 1e38;     /* 0x47d2ced32a16a1b1 */
    box[3] = box[4] = box[5] = -1e38;    /* 0xc7d2ced32a16a1b1 */
    for (int t = 0; t < m->ntri; t++)
        for (int k = 0; k < 3; k++) {
            const double *p = m->node + 3 * m->tri[3 * t + k];
            for (int a = 0; a < 3; a++) {
                if (p[a] < box[a]) box[a] = p[a];
                if (box[3 + a] < p[a]) box[3 + a] = p[a];
            }
        }
    return m->ntri;
}

/* mccore UnitNormal (1804b7480): out = (a x b) / |a x b|, the divisor 1e-12 when |a x b| < 1e-12. */
static void unit_normal(const double a[3], const double b[3], double out[3])
{
    double x = b[2] * a[1] - b[1] * a[2];
    double y = b[0] * a[2] - a[0] * b[2];
    double z = a[0] * b[1] - b[0] * a[1];
    double n = sqrt(x * x + y * y + z * z);
    if (n < 1e-12) n = 1e-12;
    out[0] = x / n; out[1] = y / n; out[2] = z / n;
}

/* FUN_180232e10 */
void ifk_axis_frame(const double ax[6], double m[12])
{
    double dz = ax[5] - ax[2], dx = ax[3] - ax[0], dy = ax[4] - ax[1];
    double len = sqrt(dy * dy + dx * dx + dz * dz);
    double d[3];
    if (len == 0.0) { d[0] = d[1] = d[2] = 0.0; }
    else { d[0] = dx / len; d[1] = dy / len; d[2] = dz / len; }
    double h[3];
    if (fabs(d[2]) < 0.999999) { h[0] = 0.0; h[1] = 0.0; h[2] = 1.0; }
    else { h[0] = -1.0; h[1] = 0.0; h[2] = 0.0; }
    unit_normal(h, d, m + 3);
    m[6] = m[5] * d[1] - m[4] * d[2];
    m[7] = m[3] * d[2] - m[5] * d[0];
    m[8] = m[4] * d[0] - m[3] * d[1];
    m[0] = d[0]; m[1] = d[1]; m[2] = d[2];
    m[9] = ax[0]; m[10] = ax[1]; m[11] = ax[2];
}

/* FUN_180230670 (the sums in the decompile's order) */
void ifk_axis_to_kernel(const double p[3], const double m[12], double out[3])
{
    double qy = p[1] - m[10], qz = p[2] - m[11], qx = p[0] - m[9];
    out[0] = qy * m[3] + qx * m[0] + qz * m[6];
    out[1] = qx * m[1] + qy * m[4] + qz * m[7];
    out[2] = qx * m[2] + qy * m[5] + qz * m[8];
}

/* mccore xform_pt (1808eba20): x_matrix rows at +0 / +0x18 / +0x30, translation at +0x48 */
void ifk_xform_pt(double p[3], const double m[12])
{
    double x = p[0], y = p[1], z = p[2];
    double ox = y * m[3] + x * m[0] + z * m[6];
    double oy = x * m[1] + y * m[4] + z * m[7];
    double oz = x * m[2] + y * m[5] + z * m[8];
    p[0] = m[9] + ox; p[1] = oy + m[10]; p[2] = m[11] + oz;
}

/* FUN_1802339f0 */
int ifk_axis_transform_surface(const char *in, const double axis[6], const char *out, int fill)
{
    FILE *fi = fopen(in, "rb"); if (!fi) return 0;
    FILE *fo = fopen(out, "wb"); if (!fo) { fclose(fi); return 0; }
    int ok = 0;
    unsigned char hdr[14]; int32_t nn, nt;
    if (fread(hdr, 1, 14, fi) != 14 || fwrite(hdr, 1, 14, fo) != 14) goto done;
    if (fread(&nn, 4, 1, fi) != 1 || fread(&nt, 4, 1, fi) != 1 || nt <= 0 || nn <= 0) goto done;
    int32_t n0 = nt;
    if (fill) { nn = nt * 9; nt = nt * 3; }
    if (fwrite(&nn, 4, 1, fo) != 1 || fwrite(&nt, 4, 1, fo) != 1) goto done;
    double m[12];
    ifk_axis_frame(axis, m);                        /* FUN_180232e10 + FUN_180232120 (a plain copy) */
    /* the node block is read as n0 triangles of 3 nodes (72 bytes each): the file must have 3 nodes per triangle */
    for (int i = 0; i < n0; i++) {
        double tri[9], t[9];
        if (fread(tri, 8, 9, fi) != 9) goto done;
        for (int k = 0; k < 3; k++) ifk_axis_to_kernel(tri + 3 * k, m, t + 3 * k);
        if (fill) {
            /* lowest-x and highest-x corner (strict compares, running), their feet on the axis (x, 0, 0) */
            const double *mn = t, *mx = t;
            if (t[3] < mn[0]) mn = t + 3;
            if (t[6] < mn[0]) mn = t + 6;
            if (mx[0] < t[3]) mx = t + 3;
            if (mx[0] < t[6]) mx = t + 6;
            double lo[3] = { mn[0], 0.0, 0.0 }, hi[3] = { mx[0], 0.0, 0.0 };
            double t1[9] = { mn[0], mn[1], mn[2], hi[0], hi[1], hi[2], lo[0], lo[1], lo[2] };
            double t2[9] = { mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], hi[0], hi[1], hi[2] };
            if (fwrite(t1, 8, 9, fo) != 9 || fwrite(t2, 8, 9, fo) != 9) goto done;
        }
        if (fwrite(t, 8, 9, fo) != 9) goto done;
    }
    int32_t rec[3] = { 0, 0, 0 }; unsigned char fl = 0;
    for (int i = 0; i < n0; i++) {
        if (fread(rec, 4, 3, fi) != 3 || fwrite(rec, 4, 3, fo) != 3) goto done;
        if (fread(&fl, 1, 1, fi) != 1 || fwrite(&fl, 1, 1, fo) != 1) goto done;
    }
    /* fill: the 2 x n0 extra triangle records continue from the last one read, +3 per index each */
    if (fill)
        for (int i = 0; i < nt - n0; i++) {
            rec[0] += 3; rec[1] += 3; rec[2] += 3;
            if (fwrite(rec, 4, 3, fo) != 3 || fwrite(&fl, 1, 1, fo) != 1) goto done;
        }
    ok = 1;
done:
    fclose(fi);
    if (fclose(fo) != 0) ok = 0;
    return ok;
}

/* FUN_1802340e0 */
int ifk_axis_back_hbe(const char *in, const double axis[6], const char *out)
{
    FILE *fi = fopen(in, "rb"); if (!fi) return 0;
    FILE *fo = fopen(out, "wb"); if (!fo) { fclose(fi); return 0; }
    double m[12];
    ifk_axis_frame(axis, m);
    int ok = 1;
    unsigned char r[0x68];
    for (;;) {
        size_t got = fread(r, 1, 0x68, fi);
        if (got == 0x68) {
            int np = r[0x60] == 1 ? 2 : r[0x60] == 2 ? 3 : 0;   /* other types: record not written */
            if (np) {
                for (int k = 0; k < np; k++) { double p[3]; memcpy(p, r + 0x18 * k, 24); ifk_xform_pt(p, m); memcpy(r + 0x18 * k, p, 24); }
                if (fwrite(r, 1, 0x68, fo) != 0x68) { ok = 0; break; }
            }
        }
        if (got < 0x68) break;   /* SysFile::IsEOF */
    }
    fclose(fi);
    if (fclose(fo) != 0) ok = 0;
    return ok;
}

/* mccore facet_normal (1806d0420) */
static void facet_normal(const double *a, const double *b, const double *c, double n[3])
{
    double d6 = a[1] - b[1], d2 = b[1] - c[1], d7 = a[0] - b[0], d1 = b[0] - c[0], d4 = a[2] - b[2], d5 = b[2] - c[2];
    n[0] = d5 * d6 - d2 * d4;
    double z = d2 * d7 - d1 * d6;
    n[1] = d1 * d4 - d5 * d7;
    n[2] = z;
    double l = sqrt(n[0] * n[0] + n[1] * n[1] + z * z);
    n[0] = n[0] / l; n[1] = n[1] / l; n[2] = n[2] / l;
}

/* mucbTransformStl (1802333b0) with ascii = 0, scale = 0, active view != 1 */
int ifk_transform_stl_view(const IfkMesh *m, const double vm[9], const char *out)
{
    FILE *f = fopen(out, "wb"); if (!f) return -1;
    char hdr[81];
    memset(hdr, ' ', 80); memcpy(hdr, "MASTERCAM STL DATA", 18); hdr[80] = 0;   /* builtin_strncpy, 0x50 bytes written */
    int32_t cnt = m->ntri;                                                     /* FUN_18022fd70: triangles in the input */
    fwrite(hdr, 1, 80, f); fwrite(&cnt, 4, 1, f);
    for (int t = 0; t < m->ntri; t++) {
        double p[3][3];
        for (int k = 0; k < 3; k++) {
            const double *w = m->node + 3 * m->tri[3 * t + k];
            for (int i = 0; i < 3; i++)   /* world_to_view (mastercam_acsave.c 180341b30): row i . w */
                p[k][i] = vm[3 * i] * w[0] + vm[3 * i + 1] * w[1] + vm[3 * i + 2] * w[2];
        }
        double n[3];
        facet_normal(p[0], p[1], p[2], n);
        double l = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l == 0.0) n[0] = n[1] = n[2] = 0.0;
        else { n[0] = n[0] / l; n[1] = n[1] / l; n[2] = n[2] / l; }
        float fl[12];
        for (int i = 0; i < 3; i++) { fl[i] = (float)n[i]; fl[3 + i] = (float)p[0][i]; fl[6 + i] = (float)p[1][i]; fl[9 + i] = (float)p[2][i]; }
        uint16_t attr = 0;
        fwrite(fl, 4, 12, f); fwrite(&attr, 2, 1, f);
    }
    return fclose(f) == 0 ? m->ntri : -2;
}

/* FUN_180213ee0 for one curve */
int ifk_hbe_records(const IfkCurveFile *cf, int curve, int spin, double systol, double scale, IfkHbeCurve *out)
{
    out->r = NULL; out->n = 0;
    if (curve < 0 || curve >= cf->ncurve) return -1;
    int s0 = cf->curve_start[curve], s1 = cf->curve_start[curve + 1];
    /* FUN_18021d5d0: room for count (x 2 when spin and dat[0x4c] == 2) + 1000 (+0x168); records beyond it are
     * refused by FUN_18021f540 / FUN_18020dbc0 / FUN_18020c1c0 (cannot happen with the + 1000) */
    int cap = 2 * (s1 - s0) + 1000;
    IfkHbeRec *R = calloc((size_t)cap, sizeof *R);
    int n = 0;
    int first = 1;              /* +0x10, set by FUN_180213ee0; +0x13 = 1 (gap bridging on) */
    double tl = fabs(systol);
    for (int i = s0; i < s1; i++) {
        /* FUN_18021d7b0: the segment's two nodes (0-based) -> +0x20 / +0x38 */
        int a = cf->cseg[2 * i], b = cf->cseg[2 * i + 1];
        if (a < 0 || b < 0 || a >= cf->nnode || b >= cf->nnode) { free(R); return -2; }
        double p0[3], p1[3];
        for (int k = 0; k < 3; k++) { p0[k] = scale * cf->node[3 * a + k]; p1[k] = scale * cf->node[3 * b + k]; }
        /* FUN_18021f540 */
        if (n >= cap) continue;
        if (spin) {
            if (fabs(p0[1] - 0.0) < tl) p0[1] = 0.0;
            if (fabs(p1[1] - 0.0) < tl) p1[1] = 0.0;
            if (p0[1] < 0.0 || p1[1] < 0.0) continue;    /* the mirrored half (r < 0) is not kept */
        }
        /* FUN_18020dbc0: a gap to the previous record along x only (|dx| >= tol, |dy| < tol, |dz| < tol) gets a
         * line from the previous end to this start */
        if (n > 0) {
            const double *q = R[n - 1].p[1];
            if (tl <= fabs(p0[0] - q[0] - 0.0) && fabs(p0[1] - q[1] - 0.0) < tl && fabs(p0[2] - q[2] - 0.0) < tl) {
                IfkHbeRec *r = &R[n++];
                r->first = (unsigned char)first; r->type = 1;
                memcpy(r->p[0], q, 24); memcpy(r->p[1], p0, 24);
            }
        }
        IfkHbeRec *r = &R[n++];
        r->first = (unsigned char)first; r->type = 1;
        memcpy(r->p[0], p0, 24); memcpy(r->p[1], p1, 24);
        first = 0;
    }
    /* FUN_18020c1c0 (+0x13 set): the same test between the last end and the first start closes the curve */
    if (n > 0 && n < cap) {
        const double *q = R[n - 1].p[1], *s = R[0].p[0];
        if (tl <= fabs(s[0] - q[0] - 0.0) && fabs(s[1] - q[1] - 0.0) < tl && fabs(s[2] - q[2] - 0.0) < tl) {
            IfkHbeRec *r = &R[n++];
            r->first = (unsigned char)first; r->type = 1;
            memcpy(r->p[0], q, 24); memcpy(r->p[1], s, 24);
        }
    }
    out->r = R; out->n = n;
    return 0;
}
