/* ifk_curvexfer.c - binary IFF curve file writer / reader and the ComputeMC node-block rewrite (see ifk_curvexfer.h).
 * Decompile: decomp/interfacial/tpif_curvexfer.c, tpif_spinsil_silhouette.c (FUN_180330350), tpif_transform.c
 * (FUN_180005690, ReverseTransformation), decomp/mcmsurf_all.c (reader). Write-up: formats/interfacial.md 9. */
#include "ifk_curvexfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- CurveXferMC (tpiftools.dll, vftable 18033b8b0) ---------------------------------------------------------- */

/* One CList<IF::SegIpb> node's data (FUN_180330c10 local_38..uStack_2c): node numbers a, b (1-based, as stored by
 * AddSegment FUN_180331200), the segment index, and the flag byte (1 when made, cleared by FUN_180330850). */
typedef struct { int a, b, idx; unsigned char flag; } SegIpb;
typedef struct { SegIpb *v; int n, cap; } SegList;   /* CList: head = v[0], tail = v[n - 1] */

static void sl_push_front(SegList *l, SegIpb s)
{
    if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 8; l->v = realloc(l->v, sizeof *l->v * (size_t)l->cap); }
    memmove(l->v + 1, l->v, sizeof *l->v * (size_t)l->n); l->v[0] = s; l->n++;
}
static void sl_push_back(SegList *l, SegIpb s)
{
    if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 8; l->v = realloc(l->v, sizeof *l->v * (size_t)l->cap); }
    l->v[l->n++] = s;
}

/* The map (CMap<int, CList*> at +0x60, hash = Schrage's 16807 generator step, FUN_1803320c0) is used only through
 * operator[] (FUN_180330b80: map[head.a] = list, a later list replacing an earlier one with the same key) and lookup
 * (FUN_180330d60), so the hash function and table size (1000, FUN_180331650) do not change the result: a direct
 * table from key to the last list index with that head key gives the same answers. */

int ifk_write_bin_curves(const char *path, const IfkCurves *c, int format, int *ncurves_out)
{
    if (ncurves_out) *ncurves_out = 0;
    /* constructor FUN_180330350: fopen(path, "wb"); header "binary tree_curves" (18 bytes) for format 1,
     * "binary curves" (13 bytes, no NUL) for format 0, nothing for any other format. */
    FILE *f = fopen(path, "wb");
    if (!f) return IFK_CX_OPEN;
    if (format != 0 && format != 1) { fclose(f); return IFK_CX_FORMAT; }
    if (format == 1) fwrite("binary tree_curves", 1, 18, f); else fwrite("binary curves", 1, 13, f);

    /* AddNode FUN_180331110: index array +0x10 of 60000 ints (+0x98) is never grown, node index >= 60000 -> NULL. */
    if (c->nnode > 60000) { fclose(f); return IFK_CX_NODELIMIT; }
    int nn = c->nnode, ns = c->nseg;
    /* AddSegment FUN_180331200: stores *a + 1, *b + 1 (1-based) at +0x18. */
    int *seg = malloc(sizeof(int) * 2 * (size_t)(ns + 1));
    for (int i = 0; i < ns; i++) { seg[2 * i] = c->seg[2 * i] + 1; seg[2 * i + 1] = c->seg[2 * i + 1] + 1; }

    /* end FUN_1803312a0: int32 node count (+0xb4), int32 segment count (+0xb8), the nodes (24 bytes each). */
    int32_t v = nn; fwrite(&v, 4, 1, f);
    v = ns; fwrite(&v, 4, 1, f);
    if (nn > 0) fwrite(c->node, 24, (size_t)nn, f);

    int rc = 0, ncurve = 0;
    /* FUN_180330c10: returns false (nothing more written) when there is no segment. */
    if (ns > 0) {
        /* FUN_180330c10 + FUN_180330a30: each segment in order joins the first list (CArray order, +0x40) whose head
         * starts where it ends (AddHead) or, failing that on the same list, whose tail ends where it starts
         * (AddTail); else it starts a new list. */
        SegList *L = NULL; int nl = 0;
        for (int i = 0; i < ns; i++) {
            SegIpb s = { seg[2 * i], seg[2 * i + 1], i, 1 };
            int done = 0;
            for (int k = 0; k < nl && !done; k++) {
                if (L[k].n == 0) continue;
                if (s.b == L[k].v[0].a) { sl_push_front(&L[k], s); done = 1; }
                else if (s.a == L[k].v[L[k].n - 1].b) { sl_push_back(&L[k], s); done = 1; }
            }
            if (!done) {
                L = realloc(L, sizeof *L * (size_t)(nl + 1)); memset(&L[nl], 0, sizeof *L);
                sl_push_back(&L[nl], s); nl++;
            }
        }
        /* FUN_180330b80: map[head.a] = list, in CArray order (later lists replace earlier ones). Node numbers are
         * 1..nn (segments refer to added nodes). */
        int *map = malloc(sizeof(int) * (size_t)(nn + 2));
        for (int k = 0; k <= nn + 1; k++) map[k] = -1;
        for (int k = 0; k < nl; k++)
            if (L[k].n && L[k].v[0].a >= 0 && L[k].v[0].a <= nn + 1) map[L[k].v[0].a] = k;

        /* FUN_180330d60: buffer +0x28 of nseg + 10 ints, -1 for [0, nseg), sentinel -999999 (0xfff0bdc1) at nseg. */
        int *buf = malloc(sizeof(int) * (size_t)(ns + 10));
        for (int i = 0; i < ns; i++) buf[i] = -1;
        buf[ns] = -999999;
        int **cv = NULL, *cvn = NULL, cap = 0;
        int ok = 1;
        for (;;) {
            /* the first list (CArray order) with a non-empty head whose head flag is still set */
            int first = -1;
            for (int k = 0; k < nl; k++) if (L[k].n && L[k].v[0].flag) { first = k; break; }
            if (first < 0) break;
            int key = L[first].v[0].a, len = 0, start = 0, end = 0;
            for (;;) {
                int m = (key >= 0 && key <= nn + 1) ? map[key] : -1;
                if (m < 0) break;
                /* FUN_180330850: append the list's segment indices; clear the head and tail flags; +0x30 = first
                 * segment's start node, +0x34 = the last segment's end node. Sentinel reached -> failure. */
                SegList *M = &L[m];
                for (int j = 0; j < M->n; j++) {
                    if (buf[len] == -999999) { ok = 0; break; }
                    if (j == 0) M->v[0].flag = 0;
                    if (j == M->n - 1) M->v[M->n - 1].flag = 0;
                    if (len == 0) start = M->v[j].a;
                    end = M->v[j].b;
                    buf[len++] = M->v[j].idx;
                }
                if (!ok) break;
                key = M->v[M->n - 1].b;   /* the tail's end node */
                if (start == end) break;
            }
            if (!ok) break;
            /* The walk does not depend on the flags, so when it leaves the head flag of `first` set the next
             * iteration repeats it exactly: the DLL loops forever here (storing the same curve again each time).
             * The port stops and reports it. */
            if (L[first].v[0].flag) { rc = IFK_CX_HANG; ok = 0; break; }
            /* FUN_180330750: < 2 segments -> 4; sum of squared segment lengths (nodes 0-based = number - 1); once it
             * exceeds +0xd8 = 2.5e-7 (0x3e90c6f7a0b5ed8d) -> 1 if the first segment's start is the last one's end,
             * else 0; never -> 3. Only 1 is kept (FUN_180330920). */
            int cls;
            if (len < 2) cls = 4;
            else {
                double sum = 0.0; cls = 3;
                for (int i = 0; i < len; i++) {
                    const double *p = c->node + 3 * (seg[2 * buf[i]] - 1), *q = c->node + 3 * (seg[2 * buf[i] + 1] - 1);
                    double dx = q[0] - p[0], dy = q[1] - p[1], dz = q[2] - p[2];
                    sum = sum + dx * dx + dy * dy + dz * dz;
                    if (2.5e-07 < sum) { cls = seg[2 * buf[0]] == seg[2 * buf[len - 1] + 1]; break; }
                }
            }
            if (cls == 1) {
                if (ncurve == cap) { cap = cap ? 2 * cap : 100; cv = realloc(cv, sizeof *cv * (size_t)cap); cvn = realloc(cvn, sizeof *cvn * (size_t)cap); }
                cv[ncurve] = malloc(sizeof(int) * (size_t)len); memcpy(cv[ncurve], buf, sizeof(int) * (size_t)len);
                cvn[ncurve++] = len;
            }
        }
        /* end FUN_1803312a0: only when FUN_180330d60 returned true; per curve int32 count, format 1: +0xac and
         * +0xb0 (both 0: set in the constructor, no slot writes them), then FUN_180330ff0: the segments' two
         * 1-based node numbers each (8 bytes), stopping after a segment that does not start at the previous end. */
        if (ok) {
            for (int k = 0; k < ncurve; k++) {
                v = cvn[k]; fwrite(&v, 4, 1, f);
                if (format == 1) { int32_t z = 0; fwrite(&z, 4, 1, f); fwrite(&z, 4, 1, f); }
                for (int i = 0; i < cvn[k]; i++) {
                    fwrite(seg + 2 * cv[k][i], 4, 2, f);
                    if (i > 0 && seg[2 * cv[k][i - 1] + 1] != seg[2 * cv[k][i]]) break;
                }
            }
            if (ncurves_out) *ncurves_out = ncurve;
        }
        for (int k = 0; k < ncurve; k++) free(cv[k]);
        free(cv); free(cvn); free(buf); free(map);
        for (int k = 0; k < nl; k++) free(L[k].v);
        free(L);
    }
    free(seg);
    if (fclose(f) != 0 && rc == 0) rc = IFK_CX_OPEN;
    return rc;
}

/* ---- reader (mcmsurf CMutlCvrtIffToHbeFile) ------------------------------------------------------------------ */

void ifk_curvefile_free(IfkCurveFile *f)
{
    free(f->node); free(f->seg); free(f->curve_start); free(f->cseg); free(f->curve_x);
    memset(f, 0, sizeof *f);
}

int ifk_read_bin_curves(const char *path, int format, IfkCurveFile *cf)
{
    memset(cf, 0, sizeof *cf);
    cf->format = format;
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    /* FUN_180212b30: header of 18 bytes (+8 == 1) or 13 bytes (+8 == 0), only the first byte is checked ('b');
     * int32 at +0x134 (node count), int32 at +0x130 (segment count); the nodes, 24 bytes each. */
    char hdr[18]; size_t hl = format == 1 ? 18 : 13;
    int32_t nn, ns;
    if ((format != 0 && format != 1) || fread(hdr, 1, hl, f) != hl || hdr[0] != 'b' ||
        fread(&nn, 4, 1, f) != 1 || fread(&ns, 4, 1, f) != 1 || nn < 0 || ns < 0 || nn > 50000000) { fclose(f); return -2; }
    cf->nnode = nn; cf->nseg = ns;
    cf->node = malloc(24 * (size_t)(nn + 1));
    if (nn > 0 && fread(cf->node, 24, (size_t)nn, f) != (size_t)nn) { fclose(f); return -3; }
    /* FUN_18021d5d0: per curve int32 count (+0x140), format 1: two more int32 (+0x144, +0x148); FUN_18021d760: per
     * segment 8 bytes = two node numbers, made 0-based; FUN_18021d7b0 rejects a negative one. Read until EOF. */
    int cap = 16, ccap = 64, nc = 0, tot = 0;
    cf->curve_start = malloc(sizeof(int) * (size_t)(cap + 1)); cf->curve_x = malloc(sizeof(int) * 2 * (size_t)cap);
    cf->cseg = malloc(sizeof(int) * 2 * (size_t)ccap);
    cf->curve_start[0] = 0;
    for (;;) {
        int32_t cnt, x[2] = { 0, 0 };
        if (fread(&cnt, 4, 1, f) != 1) break;
        if (format == 1 && fread(x, 4, 2, f) != 2) break;
        if (cnt < 0 || cnt > 50000000) { fclose(f); return -4; }
        if (nc == cap) { cap *= 2; cf->curve_start = realloc(cf->curve_start, sizeof(int) * (size_t)(cap + 1)); cf->curve_x = realloc(cf->curve_x, sizeof(int) * 2 * (size_t)cap); }
        for (int i = 0; i < cnt; i++) {
            int32_t ab[2];
            if (fread(ab, 4, 2, f) != 2) { fclose(f); return -5; }
            ab[0]--; ab[1]--;
            if (ab[0] < 0 || ab[1] < 0) { fclose(f); return -6; }
            if (tot == ccap) { ccap *= 2; cf->cseg = realloc(cf->cseg, sizeof(int) * 2 * (size_t)ccap); }
            cf->cseg[2 * tot] = ab[0]; cf->cseg[2 * tot + 1] = ab[1]; tot++;
        }
        cf->curve_x[2 * nc] = x[0]; cf->curve_x[2 * nc + 1] = x[1];
        cf->curve_start[++nc] = tot;
    }
    cf->ncurve = nc;
    fclose(f);
    return 0;
}

/* ---- FUN_180005690: rewrite the node block of a binary IFF file ----------------------------------------------- */

static int iff_nodes_rewrite(const char *path, int scale, double s, const double t[3])
{
    FILE *f = fopen(path, "r+b");
    if (!f) return -1;
    /* reads 0x1a bytes (fails on a shorter file); 'b' and byte 7: 's' -> header 14, 'c' -> 13, 't' -> 18 */
    unsigned char h[0x1a];
    if (fread(h, 1, sizeof h, f) != sizeof h || h[0] != 'b') { fclose(f); return -2; }
    int hl = h[7] == 's' ? 14 : h[7] == 'c' ? 13 : h[7] == 't' ? 18 : 0;
    if (!hl) { fclose(f); return -2; }
    int32_t n; memcpy(&n, h + hl, 4);
    long off = hl + 8;
    for (int i = 0; i < n; i++) {
        double p[3];
        if (fseek(f, off, SEEK_SET) || fread(p, 8, 3, f) != 3) { fclose(f); return -3; }
        if (scale) { p[0] = p[0] * s; p[1] = p[1] * s; p[2] = p[2] * s; }          /* FUN_180005950 */
        else { p[0] = t[0] + p[0]; p[1] = t[1] + p[1]; p[2] = t[2] + p[2]; }      /* FUN_180005980 */
        if (fseek(f, off, SEEK_SET) || fwrite(p, 8, 3, f) != 3) { fclose(f); return -4; }
        off += 24;
    }
    return fclose(f) == 0 ? 0 : -4;
}
int ifk_bin_iff_scale(const char *path, double s) { return iff_nodes_rewrite(path, 1, s, NULL); }
int ifk_bin_iff_translate(const char *path, const double t[3]) { return iff_nodes_rewrite(path, 0, 0.0, t); }

int ifk_reverse_transform(const char *path, const double t[3], double s, int translated, int scaled)
{
    /* GetDataFileFormat (180313a70): 'b' + byte 7 's' + byte 8 'u' = 's'; byte 7 'c' = 'c'; byte 7 't' + byte 12 'c'
     * = 't'. Only those reach the IFF branch of ReverseTransformation here; the binary STL branch ('b') and the
     * others are not ported (return 10, the code of its "else" for unknown formats). */
    FILE *f = fopen(path, "rb"); if (!f) return 10;
    unsigned char h[0x54] = { 0 }; size_t got = fread(h, 1, sizeof h, f); fclose(f);
    int fmt = 0;
    if (got >= 13 && (h[0] | 0x20) == 'b') {
        int c7 = h[7] | 0x20;
        if (c7 == 's' && (h[8] | 0x20) == 'u') fmt = 's';
        else if (c7 == 'c') fmt = 'c';
        else if (c7 == 't' && (h[12] | 0x20) == 'c') fmt = 't';
    }
    if (!fmt) return 10;
    int rc = 0;
    if (scaled) rc = ifk_bin_iff_scale(path, 1.0 / s) ? 0x1a : 0;   /* ScaleBinIFFData(.., true): param = 1.0 / s */
    if (translated && rc == 0) {                                     /* TranslateBinIFFData(.., true): sign bits flipped */
        double nt[3] = { -t[0], -t[1], -t[2] };
        rc = ifk_bin_iff_translate(path, nt) ? 0x1b : 0;
    }
    return rc;
}
