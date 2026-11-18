/* cam3d_cscallop_lines.c - Constant Scallop (op 43) stage C part 1: what mcmsurf FUN_180133f10 does before it calls FUN_1801cfb60
 * (formats/surf_classic3.md 9.1, 12.15, 12.16): the raster frame, the step, the machine model turned into that frame (FUN_18025dd50 ->
 * FUN_180261f40), the model's extents (FUN_18012f9c0) and the number / first position / step of the raster lines.
 * Sources: objdump of mcmsurf.dll (asm addresses below; the decompile of FUN_180133f10 loses the extents call's arguments and the line
 * count's registers) and decomp/mcmsurf_all.c (l.N). Every floating-point expression keeps the DLL's operand order (build with
 * -ffp-contract=off). The CRT's cos / sin / acos / ceil are the host's (the DLL oracle's harness runs them the same way). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cam3d_cscallop.h"

static double rd(const unsigned char *p) { double v; memcpy(&v, p, 8); return v; }
static void wd(unsigned char *p, double v) { memcpy(p, &v, 8); }
static int ri(const unsigned char *p) { int v; memcpy(&v, p, 4); return v; }

/* FUN_180133f10 asm 18013402b..180134227 (l.202737-202791): V = the active view's matrix (MCView +8..+0x48, row-major), ang =
   msurf_cut_angle. y = DAT_1804e8e08, x = DAT_1804e8dd8, z = DAT_1804e8df0; modes of the mask 0xad0 (4, 6, 7, 9, 0xb) turn it 90 deg
   (x' = y, y' = -x, asm 1801341e3..180134227) */
void cs_s1_frame(int mode, double ang, const double V[9], CsFrame *F)
{
    double c = cos(ang), s = sin(ang), ns = -s, z0 = 0.0;   /* xmm7, xmm5, xmm4 = -s (xorps), xmm8 = 0 */
    double y0 = (c * V[3] + ns * V[0]) + V[6] * z0;          /* m+0x20, m+0x08, m+0x38 */
    double y1 = (ns * V[1] + c * V[4]) + V[7] * z0;
    double y2 = (c * V[5] + ns * V[2]) + V[8] * z0;
    double x0 = (c * V[0] + s * V[3]) + V[6] * z0;
    double x1 = (s * V[4] + c * V[1]) + V[7] * z0;
    double x2 = (c * V[2] + s * V[5]) + V[8] * z0;
    F->y[0] = y0; F->y[1] = y1; F->y[2] = y2;
    F->x[0] = x0; F->x[1] = x1; F->x[2] = x2;
    F->z[0] = V[6]; F->z[1] = V[7]; F->z[2] = V[8];
    if (mode >= 0 && mode < 0xc && ((0xad0u >> mode) & 1)) {
        F->x[0] = y0; F->x[1] = y1; F->x[2] = y2;
        F->y[0] = -x0; F->y[1] = -x1; F->y[2] = -x2;
    }
}

/* FUN_1802465b0 (l.383345): the chord of a circle of diameter a at sagitta c, else f */
static double chord_of(double a, double c, double f) { c = (a - c) * c; return 0.0 < c ? sqrt(c) * 2.0 : f; }

/* FUN_180133f10 asm 18013432b..1801343f9 (method 2) / 1801343fb..180134469 (other methods), l.202819-202863 */
double cs_s1_step(int method, int optype, const unsigned char *op, double D, double chord, double *slice, double *pct, double *colaps_dist)
{
    if (method == 2) {
        int off = optype == 0x28 ? 0x1300 : optype == 0x2a ? 0x1310 : optype == 0x2b ? 0x12d0 : -1;
        if (off >= 0 && op[off]) {
            double lo = 0.05;                               /* 0x1803f04d0 */
            if (*slice > 1e-12) { double q = chord / *slice; if (lo > q) lo = q; }   /* 0x1803eb800 = 1e-12 (comisd), asm 180134391-1801343a8 */
            /* FUN_180246570(D, chord, slice, {lo, 0.8}) (l.383330): FUN_1802465b0(D, chord, slice) / slice clamped to the pair */
            double v = chord_of(D, chord, *slice) / *slice;
            if (v < lo) v = lo; else if (0.8 < v) v = 0.8;
            *pct = v;
        }
        *colaps_dist = *slice;                              /* msurf_colaps_dist = msurf_slice_dist (asm 1801343e5) */
        return *pct * *slice;                               /* asm 1801343ed-1801343f5 */
    }
    if (optype == 0xd || optype == 0x6d) return -1.0;       /* FUN_1802465b0(op+0x2e8, chord, IsMetric ? 1.27 : 0.05): NOT PORTED (not op 43) */
    return *slice;
}

/* FUN_180229ba0 l.364023-364060 (the machine model's frame, tmpmm3 rows a / b / c): the view turned by cos / sin(pi / 2); z17 = its
   param_17 = build_machine_model's third frame row = FUN_180129b40's view z (l.197413, l.197165-197168). 0, or -1 when z17 differs from the
   view's z (the cross-product branch l.364076-364095 and FUN_18002dcd0: NOT PORTED) */
int cs_mm_frame(const double V[9], const double z17[3], double a[3], double b[3], double c3[3])
{
    double c = cos(1.570796326794896), s = sin(1.570796326794896), ns = -s, z0 = 0.0;
    double d43 = (c * V[3] + ns * V[0]) + V[6] * z0, d41 = (ns * V[1] + c * V[4]) + V[7] * z0, d40 = (c * V[5] + ns * V[2]) + V[8] * z0;
    double d42 = (s * V[3] + c * V[0]) + V[6] * z0, d10 = (s * V[4] + c * V[1]) + V[7] * z0, d58 = (c * V[2] + s * V[5]) + V[8] * z0;
    if (V[6] != z17[0] || V[7] != z17[1] || V[8] != z17[2]) return -1;
    a[0] = -d43; a[1] = -d41; a[2] = -d40;
    b[0] = d42; b[1] = d10; b[2] = d58;
    c3[0] = V[6]; c3[1] = V[7]; c3[2] = V[8];
    return 0;
}

/* FUN_180261980: (x, y) turned by (c, s): x' = x c - y s, y' = x s + y c (asm 180261980..1802619b4) */
static void rot2(double c, double s, unsigned char *p)
{
    double x = rd(p), y = rd(p + 8);
    wd(p, x * c - y * s);
    wd(p + 8, x * s + y * c);
}

/* the tmpmm3 frame rows (FUN_18025dd50 l.399419-399427: +2 count, a line, one byte, FUN_180224290() = 0x96 bytes, then the rows) */
static long mm3_rows(const CsMM *mm)
{
    if (mm->n3 < 7) return -1;
    size_t k = 6; while (k < mm->n3 && mm->mm3[k] != '\n') k++;
    if (k >= mm->n3) return -1;
    k += 1 + 1 + 0x96;
    if (k + 0x48 > mm->n3) return -1;
    return (long)k;
}

/* FUN_180261f40 (l.401306; asm 180261f40..1802632c0): the model turned about z by the angle between the view's base row and p1 */
static int mm_turn(CsMM *mm, const double *p1, const double *p2, const double *p3, const double V[9], long pos, const char **why)
{
    const unsigned char *r = mm->mm3 + pos;
    double sc[3] = { rd(r + 0x30), rd(r + 0x38), rd(r + 0x40) };
    if (!(p3[0] == sc[0] && p3[1] == sc[1] && p3[2] == sc[2])) return 0;   /* asm 1802621b8-1802621cb: nothing */
    double c = cos(1.570796326794896), s = sin(1.570796326794896), ns = -s, z0 = 0.0;   /* asm 1802621e3-180262203 */
    double d9 = (c * V[3] + ns * V[0]) + V[6] * z0, d11 = (c * V[4] + ns * V[1]) + V[7] * z0, d12 = (c * V[5] + ns * V[2]) + V[8] * z0;
    double row[3] = { -d9, -d11, -d12 };                 /* asm 180262332-18026234e */
    if (!(V[6] == sc[0] && V[7] == sc[1] && V[8] == sc[2])) { *why = "FUN_180261f40 with the view z != the model's z (cross products + FUN_18002dcd0, asm 1802623ab..18026248a) NOT PORTED"; return -1; }
    double dot = (row[1] * p1[1] + row[0] * p1[0]) + row[2] * p1[2];   /* asm 18026248f-1802624c3 */
    if (dot > 1.0) dot = 1.0; else if (-1.0 > dot) dot = -1.0;
    double ang = acos(dot);
    if (0.0 > (row[0] * p2[0] + row[1] * p2[1]) + row[2] * p2[2]) ang = -ang;   /* asm 1802624f1-180262514 */
    double sn = sin(ang), cs = cos(ang);
    int n3 = ri(mm->mm3 + 2), i = 0;
    long ep = pos + 0x48;
    for (int j = 0; j < n3; j++, ep += 0x20) {
        if ((size_t)ep + 0x20 > mm->n3) { *why = "tmpmm3 shorter than its count"; return -1; }
        unsigned char *rec = mm->rec + (size_t)i * 0x78;
        if (i < mm->cnt2 && i < mm->nrec && !memcmp(rec, mm->mm3 + ep, 8)) {
            /* FUN_180261c30(cos, sin, flag, record, tmpedg0, tmpcrn0) (asm 180261c30..180261ec2): flag 0 = 0xb0 edge records */
            if (mm->flag) { *why = "tmpmm3 flag != 0 (0x120-byte edge records, FUN_180261c30 asm 180261c9f) NOT PORTED"; return -1; }
            int ne = ri(rec + 0x54), eo = ri(rec + 0x3c), co = ri(rec + 0x38);
            for (int e = 0; e < ne; e++) {
                if ((size_t)eo + 0xb0 * (size_t)(e + 1) > mm->nedg || (size_t)co + 0x70 * (size_t)(e + 1) > mm->ncrn) { *why = "edge / corner offsets beyond the files"; return -1; }
                unsigned char *q = mm->edg + eo + 0xb0 * (size_t)e;
                rot2(cs, sn, q); rot2(cs, sn, q + 0x20); rot2(cs, sn, q + 0x40); rot2(cs, sn, q + 0x78); rot2(cs, sn, q + 0x90);
            }
            for (int e = 0; e < ne; e++) {
                unsigned char *q = mm->crn + co + 0x70 * (size_t)e;
                rot2(cs, sn, q + 8); rot2(cs, sn, q + 0x20); rot2(cs, sn, q + 0x38); rot2(cs, sn, q + 0x50);
            }
            /* the box's four corners turned (asm 1802628b9..180262952), the new box their min / max (asm 180262957..180262a54) */
            unsigned char cn[4][16];
            wd(cn[0], rd(rec + 8)); wd(cn[0] + 8, rd(rec + 0x18));
            wd(cn[1], rd(rec + 0x10)); wd(cn[1] + 8, rd(rec + 0x18));
            wd(cn[2], rd(rec + 0x10)); wd(cn[2] + 8, rd(rec + 0x20));
            wd(cn[3], rd(rec + 8)); wd(cn[3] + 8, rd(rec + 0x20));
            for (int k = 0; k < 4; k++) rot2(cs, sn, cn[k]);
            double xmin = rd(cn[0]), xmax = xmin, ymin = rd(cn[0] + 8), ymax = ymin;
            for (int k = 1; k < 4; k++) {
                double x = rd(cn[k]), y = rd(cn[k] + 8);
                if (xmin > x) xmin = x; else if (x > xmax) xmax = x;
                if (ymin > y) ymin = y; else if (y > ymax) ymax = y;
            }
            wd(rec + 8, xmin); wd(rec + 0x10, xmax); wd(rec + 0x18, ymin); wd(rec + 0x20, ymax);
            i++;
        }
    }
    unsigned char *w = mm->mm3 + pos;                    /* l.401590-401591: rows 1 and 2 rewritten */
    for (int k = 0; k < 3; k++) { wd(w + 8 * k, p1[k]); wd(w + 0x18 + 8 * k, p2[k]); }
    /* tmpmm5 / tmpmm5_rgh / tmpmn5 / tmpmn5_rgh (containment points): not written on the op 43 path without containment */
    mm->turned = 1; mm->ang = ang;
    return 0;
}

/* FUN_180261940(t, p) (asm 180261940..180261973): t = -1: x' = -y, y' = x; else x' = y, y' = -x (sign flips by xorps) */
static void quarter(int t, unsigned char *p)
{
    double x = rd(p), y = rd(p + 8);
    if (t == -1) { wd(p, -y); wd(p + 8, x); } else { wd(p, y); wd(p + 8, -x); }
}
/* FUN_18025dd50's +-90 deg arm (l.399459-399560): per tmpmm3 entry matching the next record (+0), the record's box turned (t = 1:
   (+8, +0x10, +0x18, +0x20) = (y0, y1, -x1, -x0); t = -1: (-y1, -y0, x0, x1)) and FUN_1802619c0(t, flag, record, tmpedg0, tmpcrn0)
   (asm 1802619c0..180261c2a: edges +0, +0x20, +0x40, +0x78, +0x90; corners +8, +0x20, +0x38, +0x50); then rows 1 / 2 = p1 / p2 */
static int mm_quarter(CsMM *mm, int t, const double *p1, const double *p2, long pos, const char **why)
{
    int n3 = ri(mm->mm3 + 2), i = 0;
    long ep = pos + 0x48;
    for (int j = 0; j < n3; j++, ep += 0x20) {
        if ((size_t)ep + 0x20 > mm->n3) { *why = "tmpmm3 shorter than its count"; return -1; }
        unsigned char *rec = mm->rec + (size_t)i * 0x78;
        if (i < mm->cnt2 && i < mm->nrec && !memcmp(rec, mm->mm3 + ep, 8)) {
            double x0 = rd(rec + 8), x1 = rd(rec + 0x10), y0 = rd(rec + 0x18), y1 = rd(rec + 0x20);
            if (t == 1) { wd(rec + 8, y0); wd(rec + 0x10, y1); wd(rec + 0x18, -x1); wd(rec + 0x20, -x0); }
            else { wd(rec + 8, -y1); wd(rec + 0x10, -y0); wd(rec + 0x18, x0); wd(rec + 0x20, x1); }
            if (mm->flag) { *why = "tmpmm3 flag != 0 (0x120-byte edge records, FUN_1802619c0 asm 180261a1e) NOT PORTED"; return -1; }
            int ne = ri(rec + 0x54), eo = ri(rec + 0x3c), co = ri(rec + 0x38);
            for (int e = 0; e < ne; e++) {
                if ((size_t)eo + 0xb0 * (size_t)(e + 1) > mm->nedg || (size_t)co + 0x70 * (size_t)(e + 1) > mm->ncrn) { *why = "edge / corner offsets beyond the files"; return -1; }
                unsigned char *q = mm->edg + eo + 0xb0 * (size_t)e;
                quarter(t, q); quarter(t, q + 0x20); quarter(t, q + 0x40); quarter(t, q + 0x78); quarter(t, q + 0x90);
            }
            for (int e = 0; e < ne; e++) {
                unsigned char *q = mm->crn + co + 0x70 * (size_t)e;
                quarter(t, q + 8); quarter(t, q + 0x20); quarter(t, q + 0x38); quarter(t, q + 0x50);
            }
            i++;
        }
    }
    unsigned char *w = mm->mm3 + pos;
    for (int k = 0; k < 3; k++) { wd(w + 8 * k, p1[k]); wd(w + 0x18 + 8 * k, p2[k]); }
    mm->turned = 2; mm->ang = t;
    return 0;
}

/* FUN_18025dd50 (l.399269): p1 = -y, p2 = x, p3 = z of FUN_180133f10's frame (asm 180134242-18013428d). Sets *count (tmpmm2's record
   count, its param_5) when the model's z row equals p3; 0, or -1 (why) for paths not ported */
int cs_mm_reframe(CsMM *mm, const double *p1, const double *p2, const double *p3, const double V[9], int *count, const char **why)
{
    mm->turned = 0; mm->ang = 0.0;
    long pos = mm3_rows(mm);
    if (pos < 0) { *count = 0; return 0; }               /* no tmpmm3: count stays 0 (FUN_180133f10 then errors -0xb) */
    const unsigned char *r = mm->mm3 + pos;
    double a[3] = { rd(r), rd(r + 8), rd(r + 0x10) }, b[3] = { rd(r + 0x18), rd(r + 0x20), rd(r + 0x28) }, c[3] = { rd(r + 0x30), rd(r + 0x38), rd(r + 0x40) };
    { size_t k = 6; while (mm->mm3[k] != '\n') k++; mm->flag = mm->mm3[k + 1]; }
    if (!(p3[0] == c[0] && p3[1] == c[1] && p3[2] == c[2])) return 0;   /* l.399419 */
    int same = p1[0] == a[0] && p1[1] == a[1] && p1[2] == a[2] && p2[0] == b[0] && p2[1] == b[1] && p2[2] == b[2];
    if (!same) {
        if (p1[0] == b[0] && p1[1] == b[1] && p1[2] == b[2] && p2[0] == -a[0] && p2[1] == -a[1] && p2[2] == -a[2]) { *count = mm->cnt2; return mm_quarter(mm, 1, p1, p2, pos, why); }   /* sVar19 = 1 */
        if (p1[0] == -b[0] && p1[1] == -b[1] && p1[2] == -b[2] && p2[0] == a[0] && p2[1] == a[1] && p2[2] == a[2]) { *count = mm->cnt2; return mm_quarter(mm, -1, p1, p2, pos, why); }   /* sVar19 = -1 */
        *count = mm->cnt2;                               /* FUN_180261f40 reads tmpmm2's count into param_4 (l.401497) */
        return mm_turn(mm, p1, p2, p3, V, pos, why);
    }
    *count = mm->cnt2;                                   /* l.399459 */
    return 0;
}

/* FUN_18012f9c0 (asm 18012f9c0..18012fec0; l.200261): d = xmm0, stock = xmm1, n records. Out: lo4 / hi5 = the records' x range (+8 / +0x10,
   records with +0x66 != 1) grown by |stock| + d / 2; lo6 / hi7 = the x of the edges along the line direction (|dx| < chord, |dy| > 10
   chord) or, when none lies within chord of the edges' overall x range, that range with f8 / f9 set. Returns 0, or -1 (a short file) */
int cs_extents(const CsMM *mm, double d, double stock, int n, double chord, CsExt *E)
{
    memset(E, 0, sizeof *E);
    int first = 1;
    for (int i = 0; i < n; i++) {
        if (i >= mm->nrec) return -1;
        const unsigned char *r = mm->rec + (size_t)i * 0x78;
        short k; memcpy(&k, r + 0x66, 2);
        if (k == 1) continue;
        if (first) { E->lo4 = rd(r + 8); E->hi5 = rd(r + 0x10); first = 0; }
        else { if (E->lo4 > rd(r + 8)) E->lo4 = rd(r + 8); if (rd(r + 0x10) > E->hi5) E->hi5 = rd(r + 0x10); }
    }
    double g = fabs(stock) + d * 0.5;                    /* asm 18012fb7b-18012fbae (andps with 0x7fff..) */
    E->lo4 = E->lo4 - g; E->hi5 = g + E->hi5;
    int f1 = 1, f2 = 1; double z = 0.0, mn = 0.0, mx = 0.0;   /* r12b, r15b, xmm8, xmm7, xmm6 */
    E->lo6 = 0.0; E->hi7 = 0.0;
    for (int i = 0; i < n; i++) {
        const unsigned char *r = mm->rec + (size_t)i * 0x78;
        short k; memcpy(&k, r + 0x66, 2);
        if (k == 1) continue;
        int eo = ri(r + 0x3c), ne = ri(r + 0x54);
        for (int e = 0; e < ne; e++) {
            if ((size_t)eo + 0xb0 * (size_t)(e + 1) > mm->nedg) return -1;
            const unsigned char *q = mm->edg + eo + 0xb0 * (size_t)e;   /* read_edge (mccore 180593320): P1 = +0, P2 = +0x20 */
            double px = rd(q), py = rd(q + 8), qx = rd(q + 0x20), qy = rd(q + 0x28);
            double x3 = px - py * z, y4 = px * z + py, x5 = qx - qy * z, y1 = qx * z + qy;   /* asm 18012fd0f-18012fd4a */
            double x2 = x5 > x3 ? x3 : x5;                  /* min (asm 18012fd4f) */
            x3 = x3 > x5 ? x3 : x5;                         /* max */
            double y5 = y1 > y4 ? y4 : y1;
            y4 = y4 > y1 ? y4 : y1;
            if (f1) { mn = x2; f1 = 0; mx = x3; }
            else { if (mn > x2) mn = x2; if (x3 > mx) mx = x3; }
            if (chord > fabs(x2 - x3) && fabs(y5 - y4) > chord * 10.0) {   /* asm 18012fd9c-18012fdc6 */
                double m = (x3 + x2) * 0.5;
                if (f2) { E->lo6 = m; f2 = 0; E->hi7 = m; }
                else { if (E->lo6 > m) E->lo6 = m; if (m > E->hi7) E->hi7 = m; }
            }
        }
    }
    /* asm 18012fe10-18012fe56 */
    if (f2 || E->lo6 > chord + mn) {
        E->lo6 = mn; E->f8 = 1;
        if (f2) { E->hi7 = mx; E->f9 = 1; return 0; }
    }
    if (mx - chord > E->hi7) { E->hi7 = mx; E->f9 = 1; }
    return 0;
}

/* FUN_180133f10 asm 18013463b..1801346fc (no swap): the raster lines from the extents. Out: n (esi), first (xmm6, FUN_1801cfb60's
   param_16; the scanner's line i lies at -first - i step) and the step (xmm7, param_17) */
void cs_s1_lines(double step, const CsExt *E, int *n, double *first, double *stepo)
{
    double lo = E->lo6, hi = E->hi7;                     /* xmm8 = [rbp-0x58], xmm6 = [rbp-0x60] */
    if (E->f8) lo = lo - step * 0.5;
    if (E->f9) hi = hi + step * 0.5;
    hi = -hi; lo = -lo;
    double w = lo - hi;                                   /* xmm11 */
    int k = (int)ceil(w / step);                          /* 0x1803c7e7e, cvttsd2si */
    if (k <= 1) { k = 1; hi = (hi + lo) * 0.5; lo = hi; }
    else { step = w / (double)k; k++; }
    double t = -E->hi5;
    while (hi > t) { hi = hi - step; k++; }
    t = -E->lo4;
    while (t > lo) { lo = lo + step; k++; }
    *n = k; *first = hi; *stepo = step;
}

/* msurf_map (FUN_180205a90, l.339935) for op 0x2b, the values stage 1 reads (op = the operation struct = record + 0x68):
   chord l.340017 (op+0x11d8; < 1e-10 -> nc_tol l.340492), corner deviation l.340045-340047 / l.340482-340483 (op+0x126a: op+0x1278 x
   chord, else op+0x1270; < 1e-10 -> nc_tol l.340524), case 0x2b l.340292-340300 (slice op+0x1298, cut angle op+0x12c8, collapse % op+0x12a8;
   slice < 1e-10 -> 0.05 l.340495), the collapse % clamps l.340504-340522, stock l.340003 (op+0x278), D / r op+0x2e8 / +0x2f0 */
void cs_op43_params(const unsigned char *op, double nc_tol, CsOp43 *P)
{
    memset(P, 0, sizeof *P);
    P->chord = rd(op + 0x11d8);
    int cpct = op[0x126a] != 0;
    P->cdev = rd(op + 0x1270);
    double cdp = rd(op + 0x1278);
    P->slice = rd(op + 0x1298);
    P->cut_angle = rd(op + 0x12c8);
    P->pct = rd(op + 0x12a8);
    if (cpct) P->cdev = cdp * P->chord;
    /* msurf_max_short_gap (FUN_1801cfb60's param_22: FUN_180133f10 passes the export 1804d6f90, asm 180135203..18013520b): op+0x1228
       (l.340022); with op+0x1220 (msurf_use_short_gap_pct, l.340020) and op type 0x2b (none of the lists of l.340446-340474) the slice
       (before the 0.05 clamp of l.340495) x op+0x1230 (l.340476). CURVE 5 AXIS op 2: 0.05 x 3.0 = 0x1.3333333333334p-3 = the S1 record */
    P->gap = op[0x1220] ? P->slice * rd(op + 0x1230) : rd(op + 0x1228);
    double cd = P->cdev;
    if (P->chord < 1e-10) P->chord = nc_tol;
    if (P->slice < 1e-10) P->slice = 0.05;
    if (!(1e-10 <= P->pct)) P->pct = 0.8;                /* uVar15:uVar14 = 0x3fe99999:9999999a from l.340007 (not reassigned in between) */
    else if (!(P->pct < 0.9999999999)) { unsigned long long b = 0x3feffffffff24190ull; memcpy(&P->pct, &b, 8); }   /* l.340508-340509 */
    double d2 = P->pct * P->slice;
    if (1e-10 <= d2) { if (P->slice - 1e-10 < d2) P->pct = (P->slice - 1e-10) / P->slice; }
    else P->pct = 1e-10 / P->slice;
    if (cd < 1e-10) P->cdev = nc_tol;
    P->stock = rd(op + 0x278);
    P->D = rd(op + 0x2e8);
    P->r = rd(op + 0x2f0);
}

/* ---- stage C part 2: the scan file's frame (surf_classic3.md 12.17) ---- */
/* FUN_180229640(a, b, c, h) (l.363720): the 0x38-byte header: +0 a, +4 c, +8 b, +0xc..+0x23 -1, +0x28 -1.0, +0x30 0; +0x24..+0x27 and
   +0x31..+0x37 are left as they were (the caller's stack in the DLL: zero here) */
void cs_scan_header(unsigned char h[0x38], int a, int b, int c)
{
    memcpy(h, &a, 4); memcpy(h + 4, &c, 4); memcpy(h + 8, &b, 4);
    memset(h + 0xc, 0xff, 0x18);
    unsigned long long m1 = 0xbff0000000000000ull; memcpy(h + 0x28, &m1, 8);
    h[0x30] = 0;
}
static void put(CsBuf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 4096; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, p, n); b->n += n;
}
/* the scanner's trailer, FUN_1801ea7e0 l.326713-326734 (no containment): the frame rows DAT_1804ea500 / 518 / 530, 560, 568 / 5.0, 570, 578,
   param_5 (D), DAT_1804ea4f8 = 5a0 - s, 550 = -param_15, 548 = -param_16, int (param_18 != 0), int n, the n-int line table, int -1 (no
   containment: DAT_1804ea4f0 = 0). F = the rows (-y, x, z) */
void cs_scan_trailer(CsBuf *out, const CsScan *S, const double F[9], double D, double first, double step, int p18, int n, const int *table)
{
    put(out, F, 24); put(out, F + 3, 24); put(out, F + 6, 24);
    put(out, &S->t90, 8);
    double h5 = S->half / 5.0; put(out, &h5, 8);
    put(out, &S->e, 8); put(out, &S->systol, 8); put(out, &D, 8);
    double f8 = S->off_drive - S->s; put(out, &f8, 8);        /* l.326722: DAT_1804ea5a0 - s (the CONCAT44(uVar27, uVar26) of l.326226-326237) */
    double a = -first, b = -step; put(out, &a, 8); put(out, &b, 8);
    int f = p18 != 0; put(out, &f, 4); put(out, &n, 4);
    put(out, table, (size_t)n * 4);
    int m1 = -1; put(out, &m1, 4);
}
/* FUN_18022c850(hdr, &ok) (l.365146; the scanner's last step l.326767-326768, and FUN_1801cfb60's kind 6 end): appended to the scan file
   (FUN_18025d6d0(1, ..): the same file reopened, positioned at its end): the part name line, tmpmm2's record count and list head, mccore's
   tol / chain_tol / chain_plane_tol / min_arc_length / curve_min_step / curve_max_step / curve_chord_dev / surf_step_chord_dev / nc_tol
   (tols[9]), tmpmm2's ne 0x20-byte entries; then -1, the op id, the op entity's +0x30 (FUN_1802287f0 = TpPartOpList::DatabaseRetrieve),
   the op id, 0x28d0 (8 bytes), the operation (0x28d0), 0x698 (8 bytes), the tool (tool_manager(.., 5): 0x698), the interfacial tool's 13
   doubles (zero for tool types other than 0x19 / 0xe / 0x17: Pl2ConvertMCTooltoInterfacialTool* NOT PORTED - refused by the caller). The
   header gets +0x10 = the appendix's start, +0x14 = the position after the entries, +0x1c / +0x20 = -1 (no swap: DAT_1805039d4 = 0),
   +0x24 = the 13 doubles' position, and is written at 0. ok = 0 when tmpmm2 is missing (count 0, head -1: l.365215-365218) */
void cs_scan_appendix(CsBuf *out, const char *name, int cnt2, int head2, int ne, const unsigned char *ent, const double tols[9], int opid,
                      int ent30, const unsigned char *op, const unsigned char *tool, const double itool[13])
{
    int l3 = (int)out->n;
    put(out, name, strlen(name)); put(out, "\n", 1);
    put(out, &cnt2, 4); put(out, &head2, 4);
    put(out, tols, 72);
    put(out, ent, (size_t)ne * 0x20);
    int l4 = (int)out->n, m1 = -1;
    put(out, &m1, 4); put(out, &opid, 4); put(out, &ent30, 4); put(out, &opid, 4);
    long long k = 0x28d0; put(out, &k, 8); put(out, op, 0x28d0);
    k = 0x698; put(out, &k, 8); put(out, tool, 0x698);
    int l5 = (int)out->n;
    put(out, itool, 13 * 8);
    memcpy(out->p + 0x10, &l3, 4); memcpy(out->p + 0x14, &l4, 4); memcpy(out->p + 0x1c, &m1, 4); memcpy(out->p + 0x20, &m1, 4);
    memcpy(out->p + 0x24, &l5, 4);
}
