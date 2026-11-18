/* stl_mesh.c - STL files (formats/cad_import.md 7).
 * 1. Mastercam's triangle reader (mccore get_first_stl_triangle 0x1806d1070 / get_next_stl_triangle 0x1806d1280, decomp/mccore_stl_read.c):
 *    used by the Stock Setup's STL stock (core_ui/cui_stock.c) and by File > Open of an STL file as lines (rd_stl, cad_import.c).
 * 2. File > Open of an STL file as a mesh (mcstl.dll DoImport 0x180004910 -> FUN_1800040e0 -> mccore StoreSTLFileAsSTLMesh 0x1805ccac0):
 *    UtMMFDataMgr::CreateSTLEntryFromFile 0x1808c14a0 / CreateSTLEntryFromStream 0x1808c1610 and the PMESH_ID record.
 * Decompiles: ~/mastercam-re/work/stlimp/dump (mcstl_all.c, core_stl.c, core_mmf.c, core_pack.c). */
#include "stl_mesh.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* check_ascii_binary reads the 0x50-byte header and looks for "solid" / "SOLID" (5 characters) at each position before a NUL; found = ASCII,
   tried first (FUN_1806d0aa0), else binary (FUN_1806d0ec0); a failing first triangle switches to the other reader. ASCII (FUN_1806d0aa0):
   lines of at most 0xff characters; "facet normal" (or FACET NORMAL) = the normal (sscanf "%s %s %s" at +0xd, atof) and leaves the loop;
   "outer loop" / "OUTER LOOP" / "outerloop" / "OUTERLOOP" enter it; "endloop" / "end loop" / "ENDLOOP" / "END LOOP" leave it; inside,
   "vertex" / "VERTEX" + three atof values = a vertex; three vertices = a triangle. Binary (FUN_1806d0ec0): skip 0x50 + 4 bytes, then per
   triangle 12 bytes normal, 3 x 12 bytes vertices (floats), 2 bytes. NOT DECODED / ours: a header without NUL or keyword in its 0x50 bytes
   reads on into stack bytes in the original - here the header's end counts as a NUL (binary); the non-STL conversion branch
   (FUN_1806d1f40 -> convert_file_to_stl_file) is not taken for .stl files and is left out. */
int stl_ascii_tri(FILE *f, double t[3][3], double *nrm) {
    /* FUN_1806d0aa0 reads each line (SysFile::ReadString, at most 0xff characters) into a zeroed 0x150-byte stack area and scans p + 7 after
       "vertex": the zero tail keeps that read inside the buffer - the same here */
    char ln[0x150]; int nv = 0, in = 0;
    for (;;) {
        if (nv >= 3) break;
        memset(ln, 0, sizeof ln);
        if (!fgets(ln, 0x100, f)) break;
        char *p = strstr(ln, "facet normal"); if (!p) p = strstr(ln, "FACET NORMAL");
        if (p) {
            in = 0;
            if (nrm) { char a[80] = "", b[80] = "", c[80] = ""; sscanf(p + 0xd, "%79s %79s %79s", a, b, c); nrm[0] = atof(a); nrm[1] = atof(b); nrm[2] = atof(c); }
            continue;
        }
        if (strstr(ln, "outer loop") || strstr(ln, "OUTER LOOP") || strstr(ln, "outerloop") || strstr(ln, "OUTERLOOP")) { in = 1; continue; }
        if (strstr(ln, "endloop") || strstr(ln, "end loop") || strstr(ln, "ENDLOOP") || strstr(ln, "END LOOP")) { in = 0; continue; }
        p = strstr(ln, "vertex"); if (!p) p = strstr(ln, "VERTEX");
        if (in && p) { char a[80] = "", b[80] = "", c[80] = ""; sscanf(p + 7, "%79s %79s %79s", a, b, c); t[nv][0] = atof(a); t[nv][1] = atof(b); t[nv][2] = atof(c); nv++; }
    }
    return nv == 3;
}
int stl_bin_tri(FILE *f, double t[3][3]) {          /* FUN_1806d0ec0: a triangle only when all 50 bytes are there */
    unsigned char r[50]; float q;
    if (fread(r, 1, sizeof r, f) != sizeof r) return 0;
    for (int v = 0; v < 3; v++) for (int k = 0; k < 3; k++) { memcpy(&q, r + 12 + 12 * v + 4 * k, 4); t[v][k] = q; }
    return 1;
}
int stl_is_ascii(FILE *f) {                         /* check_ascii_binary (mccore 0x1806ce930) */
    unsigned char h[0x50]; size_t n = fread(h, 1, sizeof h, f); rewind(f);
    for (size_t s = 0; s < n; s++) {
        size_t k = 0; while (k < 5 && s + k < n && h[s + k]) k++;
        if (k < 5) return 0;                                /* a NUL (or the header's end) inside the window: binary */
        if (!memcmp(h + s, "solid", 5) || !memcmp(h + s, "SOLID", 5)) return 1;
    }
    return 0;
}
int stl_tri_first(StlTriReader *r, const char *path, double t[3][3]) {
    r->f = fopen(path, "rb"); r->ascii = 0;
    if (!r->f) return 0;
    r->ascii = stl_is_ascii(r->f);
    rewind(r->f); if (!r->ascii) fseek(r->f, 0x54, SEEK_SET);
    int ok = r->ascii ? stl_ascii_tri(r->f, t, NULL) : stl_bin_tri(r->f, t);
    if (!ok) { r->ascii = !r->ascii; rewind(r->f); if (!r->ascii) fseek(r->f, 0x54, SEEK_SET); ok = r->ascii ? stl_ascii_tri(r->f, t, NULL) : stl_bin_tri(r->f, t); }
    return ok;
}
int stl_tri_next(StlTriReader *r, double t[3][3]) { return r->f ? (r->ascii ? stl_ascii_tri(r->f, t, NULL) : stl_bin_tri(r->f, t)) : 0; }
void stl_tri_close(StlTriReader *r) { if (r->f) fclose(r->f); r->f = NULL; }

/* ------------------------------------------------------------------------------------------------ the mesh (StoreSTLFileAsSTLMesh) */
int stl_file_kind(const char *path) {
    /* FUN_1806ccb40: PathFileExistsA, SysFile::Open; 1, then SetPosition(0x50) + Read 4 = the count: 2 when count * 0x32 + 0x54 = the size */
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    int k = 1; uint8_t c[4];
    if (fseek(f, 0x50, SEEK_SET) == 0 && fread(c, 1, 4, f) == 4) {
        int32_t n; memcpy(&n, c, 4);
        fseek(f, 0, SEEK_END); long sz = ftell(f);
        if ((int64_t)n * 0x32 + 0x54 == (int64_t)sz) k = 2;     /* (iVar3 = count * 0x32 in 32-bit int in the DLL) */
    }
    fclose(f); return k;
}
/* CreateSTLEntryFromStream's ASCII branch (param_3 false, DAT_181311e62 = 1: the enhanced form): FUN_1806d0aa0 per facet until it fails;
   the facet normal and the vertices as floats (the normal as read, not normalised), attribute 0 (local_808); no facet = no entry */
static int ascii_entry(const char *path, StlPoly **out) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    StlPoly *p = NULL; int n = 0, cap = 0;
    double t[3][3], nrm[3] = { 0, 0, 0 };   /* local_858 keeps the last normal read (uninitialised before the first one in the DLL: ours 0) */
    while (stl_ascii_tri(f, t, nrm)) {
        if (n == cap) { cap = cap ? cap * 2 : 1024; p = realloc(p, sizeof *p * (size_t)cap); }
        StlPoly *q = &p[n++]; memset(q, 0, sizeof *q);
        for (int k = 0; k < 3; k++) q->n[k] = (float)nrm[k];
        for (int v = 0; v < 3; v++) for (int k = 0; k < 3; k++) q->v[3 * v + k] = (float)t[v][k];
    }
    fclose(f);
    if (!n) { free(p); return -1; }
    *out = p; return n;
}
int stl_mesh_from_file(const char *path, uint8_t rec[0x130], StlPoly **out, int *mixed, char *err, size_t errn) {
    *out = NULL; *mixed = 0; if (err && errn) err[0] = 0;
    /* CreateSTLEntryFromFile: the kind (1 / 2), the size from GetFileAttributesExA (0 -> no entry), CreateSTLEntryFromStream(file, size, kind == 2) */
    int kind = stl_file_kind(path);
    if (kind != 1 && kind != 2) { snprintf(err, errn, "%s: cannot open the file", path); return 0; }
    FILE *f = fopen(path, "rb"); if (!f) { snprintf(err, errn, "%s: cannot open the file", path); return 0; }
    fseek(f, 0, SEEK_END); long sz = ftell(f);
    if (sz <= 0) { fclose(f); snprintf(err, errn, "%s: the file is empty", path); return 0; }
    int n;
    if (kind == 2) {
        uint8_t *b = malloc((size_t)sz); rewind(f);
        if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); snprintf(err, errn, "%s: read error", path); return 0; }
        n = mcam_stl_entry_bin(b, (size_t)sz, out, mixed); free(b);
    } else n = ascii_entry(path, out);
    fclose(f);
    if (n <= 0) { free(*out); *out = NULL; snprintf(err, errn, "%s: no %s STL facet could be read", path, kind == 2 ? "binary" : "ASCII"); return 0; }
    /* the record (StoreSTLFileAsSTLMesh): class 0x100000, +0x68 kind 0, +0x70 the entry, +0x78 the matrix (DoImport: identity),
       FUN_1805cc510 -> FUN_1805cc560: the box of the vertices (MCLoadSTLMesh::GetSTLPoly with the matrix; min from 1e38, max from -1e38,
       strict comparisons), +0x128 = 1 (in_hardcopy off) */
    memset(rec, 0, 0x130);
    uint32_t c = 0x100000; memcpy(rec, &c, 4);
    int64_t e = -1; memcpy(rec + 0x70, &e, 8);
    for (int i = 0; i < 4; i++) { double one = 1.0; memcpy(rec + 0x78 + 8 * (4 * i + i), &one, 8); }
    double lo[3] = { 1e38, 1e38, 1e38 }, hi[3] = { -1e38, -1e38, -1e38 };
    for (int i = 0; i < n; i++) for (int v = 0; v < 3; v++) for (int k = 0; k < 3; k++) {
        double q = (double)(*out)[i].v[3 * v + k];
        if (q < lo[k]) lo[k] = q;
        if (q > hi[k]) hi[k] = q;
    }
    memcpy(rec + 0xf8, lo, 24); memcpy(rec + 0x110, hi, 24); rec[0x128] = 1;
    return n;
}
