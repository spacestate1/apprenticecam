/* ifk_surfpoly.c - wr_stl / surf_to_stl of mccore.dll (Mastercam 2017) as mucbMakeIffSiluetBndy calls them, formats/interfacial.md 10.2 and 12.
 * Decompiles: decomp/interfacial/mccore_wr_stl.c (wr_stl 0x1806d2e60, surf_to_stl 0x1806d22e0, make_surf_polygons 0x1806fbda0);
 * disassembly decomp/interfacial/surf_to_stl_asm.txt (the floating-point order below is the disassembly's, cited by address).
 * The mesh: viewer/mcpartline_mesh.c plm_mesh = make_surf_polygons on an untrimmed NURBS (kind 0) surface; the call there is
 * make_surf_polygons(1, 1, ent, step, 0, 0, ..), here make_surf_polygons(1, 0, ent, tol, 0, 0, ..): the second argument (the uv output) only
 * sizes the arena (FUN_18057a980 core_tess.c l.1145 / 1161 / 1185 / 1738: edge_fit / get_storage of the uv block, the uv pointer stored),
 * which make_surf_polygons' retry loop (FUN_1806fb740) grows until it fits - checked against the DLL by tools/tests/ifk_surfpoly_oracle.sh.
 * Oracle: tools/peloader --stl (tools/peloader/mccore_stl.c) runs the DLL's own wr_stl; the %f text is checked against msvcr120.dll's own
 * sprintf (tools/peloader --crtf). */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "ifk_surfpoly.h"
#include "../mcpartline.h"

/* ---- MSVCR120 "%f" -------------------------------------------------------------------------------------------------------------------
 * SysFile::Printf (0x180a3b2a0, decomp/interfacial/mccore_wr_stl.c l.1064) = vfprintf_s of MSVCR120 on the file. Its %f is _cftof:
 * _fltout2 -> $I10_OUTPUT(ld, 17, 0) and _fptostr, the same two routines as _fcvt_s (disassembled from msvcr120.dll in viewer/mp/mp_fmt.c's
 * header: $I10_OUTPUT @18007e848 generates 18 significant digits and rounds them half-up to 17 (cmp al,0x35 at 18007f22b), trailing zeros
 * stripped; _fptostr @1800251e0 copies decpt + prec digits and rounds half-up on the next digit (cmp [rcx],0x35 at 180025268)), then the text
 * is laid out as [-]int.frac (int "0" when decpt <= 0). The sign is the value's sign bit (-0.0 prints "-0.000000"). The 18 digits are taken
 * here from the exact binary expansion (glibc %.780e is exact), where $I10_OUTPUT uses 96-bit scaled arithmetic: they can only differ at a
 * near-tie in digits 18.. (mp_fmt.c). Checked against msvcr120.dll's own sprintf("%f") on every value of the oracle runs (crt_fmt.c). */
static int i10_17(double v, char man[24], int *e10)
{
    if (v == 0.0) { strcpy(man, "0"); *e10 = 0; return 1; }
    if (!isfinite(v)) return 0;   /* NOT DECODED: $I10_OUTPUT's 1#INF / 1#QNAN text */
    char big[1100], dig[800]; int nd = 0;
    snprintf(big, sizeof big, "%.780e", fabs(v));
    const char *p = big;
    for (; *p && *p != 'e'; p++) if (*p >= '0' && *p <= '9' && nd < (int)sizeof dig - 1) dig[nd++] = *p;
    int e = atoi(p + 1) + 1;
    char g[19];
    for (int i = 0; i < 18; i++) g[i] = i < nd ? dig[i] : '0';
    if (g[17] >= '5') {
        int i = 16;
        while (i >= 0 && g[i] == '9') g[i--] = '0';
        if (i < 0) { memmove(g + 1, g, 16); g[0] = '1'; e++; } else g[i]++;
    }
    g[17] = 0;
    int L = 17;
    while (L > 1 && g[L - 1] == '0') g[--L] = 0;
    memcpy(man, g, (size_t)L + 1);
    *e10 = e;
    return 1;
}
int ifk_msvc_f(char *out, size_t outsz, double v, int prec)
{
    char man[24], buf[512], res[600]; int e10;
    if (!i10_17(v, man, &e10)) return snprintf(out, outsz, "%f", v);   /* not reached on the STL path (finite coordinates) */
    long digits = (long)e10 + prec;
    if (digits > (long)sizeof buf - 2) digits = (long)sizeof buf - 2;
    char *b = buf; const char *m = man; long d = digits;   /* _fptostr */
    *b++ = '0';
    while (d > 0) { *b++ = *m ? *m++ : '0'; d--; }
    *b = 0;
    if (d >= 0 && *m >= '5') { b--; while (*b == '9') *b-- = '0'; *b += 1; }
    if (buf[0] == '1') e10++; else memmove(buf, buf + 1, strlen(buf + 1) + 1);
    int k = 0, bl = (int)strlen(buf);
    if (signbit(v)) res[k++] = '-';
    if (e10 <= 0) res[k++] = '0';
    else for (int i = 0; i < e10 && k < (int)sizeof res - 2; i++) res[k++] = i < bl ? buf[i] : '0';
    if (prec > 0) {
        res[k++] = '.';
        for (int i = 0; i < prec && k < (int)sizeof res - 1; i++) { int src = i + e10; res[k++] = (src >= 0 && src < bl) ? buf[src] : '0'; }
    }
    res[k] = 0;
    return snprintf(out, outsz, "%s", res);
}

/* SysFile::Printf with %f arguments: the text (raw: %a, as tools/peloader --stl) */
static void out_printf(IfkStlOut *o, const char *fmt, const double *a)
{
    int ia = 0;
    if (o->raw) fputs("P ", o->f);
    for (const char *p = fmt; *p; p++) {
        if (p[0] == '%' && p[1] == 'f') {
            if (o->raw) fprintf(o->f, "%a", a[ia]);
            else { char t[600]; ifk_msvc_f(t, sizeof t, a[ia], 6); fputs(t, o->f); }
            ia++; p++;
        } else if (*p == '\n' && o->raw) fputs("\\n", o->f);
        else fputc(*p, o->f);
    }
    if (o->raw) fputc('\n', o->f);
}

int ifk_surf_to_stl(const McamFile *F, const Surf *s, double tol, const double M[12], IfkStlOut *o, const char **why)
{
    if (why) *why = NULL;
    /* make_surf_polygons(1, 0, ent, tol, 0, draw, &tri, &xyz, NULL, &err, &esc) (surf_to_stl 1806d23c8) */
    PlMesh m;
    int nt = plm_mesh(F, s, tol, &m, why);
    if (nt < 0) return -1;
    if (nt == 0) { plm_free(&m); return 0; }   /* 1806d23e9: none -> 0 facets */
    /* 1806d23ef..1806d242c: the largest vertex index; 1806d2442..1806d2804: vertices 0..max through M in place (4 at a time from the top, then
       the rest: the same per-vertex formula): x' = (x M0 + y M3) + z M6, y' = (x M1 + y M4) + z M7, z' = (x M2 + y M5) + z M8, then + origin */
    int mx = 0;
    for (int i = 0; i < 3 * nt; i++) if (mx < m.tri[i]) mx = m.tri[i];
    for (int i = mx; i >= 0; i--) {
        double *P = m.xyz + 3 * (size_t)i, x = P[0], y = P[1], z = P[2];
        double X = (x * M[0] + y * M[3]) + z * M[6];
        double Y = (x * M[1] + y * M[4]) + z * M[7];
        double Z = (x * M[2] + y * M[5]) + z * M[8];
        P[0] = M[9] + X; P[1] = Y + M[10]; P[2] = M[11] + Z;
    }
    /* per triangle (a, b, c) = tri[3t..]: 1806d2840..1806d28c1 */
    int flip = (s->rec && s->n > 0x6f) ? ((s->rec[0x6e] >> 2) & 1) : 0;   /* 1806d2942: ent +0x6e bit 2 (the surface's normal reversed) */
    for (int t = 0; t < nt; t++) {
        const double *A = m.xyz + 3 * (size_t)m.tri[3 * t], *B = m.xyz + 3 * (size_t)m.tri[3 * t + 1], *C = m.xyz + 3 * (size_t)m.tri[3 * t + 2];
        double bx = B[0] - A[0], by = B[1] - A[1], bz = B[2] - A[2], cx = C[0] - A[0], cy = C[1] - A[1], cz = C[2] - A[2];
        double nx = cz * by - cy * bz, ny = bz * cx - cz * bx, nz = cy * bx - by * cx;
        double l = sqrt((nx * nx + ny * ny) + nz * nz);   /* 1806d28e2..1806d28f9 */
        if (l == 0.0) { nx = 0.0; ny = 0.0; nz = 0.0; }   /* 1806d2901: UCOMISD == 0 -> all +0 */
        else { nx = nx / l; ny = ny / l; nz = nz / l; }
        const double *V1 = B, *V2 = C;
        if (flip) { nx = -nx; ny = -ny; nz = -nz; V1 = C; V2 = B; }   /* 1806d2952: XORPS sign; 1806d29e6: corners a, c, b */
        float fn[3] = { (float)nx, (float)ny, (float)nz }, f0[3], f1[3], f2[3];   /* 1806d29ac..: CVTPD2PS */
        for (int k = 0; k < 3; k++) { f0[k] = (float)A[k]; f1[k] = (float)V1[k]; f2[k] = (float)V2[k]; }
        double a[3];
        a[0] = fn[0]; a[1] = fn[1]; a[2] = fn[2]; out_printf(o, "  facet normal %f %f %f\n", a);   /* 1806d2a9c: CVTPS2PD */
        out_printf(o, "    outer loop\n", NULL);
        a[0] = f0[0]; a[1] = f0[1]; a[2] = f0[2]; out_printf(o, "      vertex %f %f %f\n", a);
        a[0] = f1[0]; a[1] = f1[1]; a[2] = f1[2]; out_printf(o, "      vertex %f %f %f\n", a);
        a[0] = f2[0]; a[1] = f2[1]; a[2] = f2[2]; out_printf(o, "      vertex %f %f %f\n", a);
        out_printf(o, "    endloop\n", NULL);
        out_printf(o, "  endfacet\n", NULL);
    }
    plm_free(&m);
    return nt;
}

/* wr_stl (1806d2e60) with filter -1, ASCII, quiet, no view, param_8 = param_9 = 0. The walk (get_raw_ent class mask 0x110100: surfaces, solids,
 * polygon meshes) takes an entity when ((sel & 0x100) == 0 and its level is visible (or param_8)) or param_9, and (filter == 0 or (filter == -1
 * and sel bit 4) or its +0x34 == filter); the caller passes those, in database order. Solids (0x10000) go to sld_to_stl (mcgeomsld ps_stl_output
 * -> Parasolid PK_TOPOL_render_facet) and polygon meshes (0x100000) to copyPmeshToStlFile: NOT PORTED. The selected solid faces of the temp
 * file (sld_count_temp_file, read_next_solid_surf_from_temp_file) follow the database walk: NOT PORTED (formats/interfacial.md 12.3). */
int ifk_wr_stl(const McamFile *F, const Surf *const *sel, int n, double tol, IfkStlOut *o, const char **why)
{
    if (why) *why = NULL;
    if (n <= 0) return 0;   /* 1806d3010: nothing found -> (quiet) no file text at all */
    static const double M[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };   /* 1806d2fa0..: local_2c18 the identity, origin local_2bd0.. = 0 */
    out_printf(o, "solid ascii\n", NULL);
    int total = 0;
    for (int i = 0; i < n; i++) {
        int k = ifk_surf_to_stl(F, sel[i], tol, M, o, why);
        if (k < 0) return -1;
        total += k;
    }
    out_printf(o, "end solid\n", NULL);
    return total;
}
