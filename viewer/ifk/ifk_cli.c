/* ifk_cli - run the Interfacial kernel ports (viewer/ifk) on files.
 *   ifk_cli MESH(.stl|.iff|.tmp) TOL [--svg OUT.svg] [--curves OUT.tmp [FORMAT]]
 *       Turn Profile kernel (ifk_spinsil.c); prints the status line and the profile as "node i x r" / "seg j a b"
 *       lines (%.17g); --curves also writes the binary IFF curve file as CurveXferMC does (ifk_curvexfer.c).
 *   ifk_cli --write-curves IN.txt OUT.tmp FORMAT
 *       CurveXferMC on a hand-made list: lines "n x y z" (AddNode) and "s a b" (AddSegment, 0-based).
 *   ifk_cli --read-curves FILE FORMAT       print a curve file in mcmsurf's reader layout
 *   ifk_cli --scale FILE S | --translate FILE TX TY TZ | --reverse FILE TX TY TZ S TRANSLATED SCALED
 *       ComputeMC's node-block rewrite (FUN_180005690) and ReverseTransformation on a binary IFF file.
 *   ifk_cli --hbe CURVEFILE FORMAT SPIN SYSTOL     HBE records per curve before the filter (ifk_mcside.c)
 *   ifk_cli --axis X0 Y0 Z0 X1 Y1 Z1 PX PY PZ      axis frame, the point to the kernel frame and back
 *   ifk_cli --axis-surface IN OUT FILL X0 Y0 Z0 X1 Y1 Z1 | --axis-hbe IN OUT X0 Y0 Z0 X1 Y1 Z1
 *   ifk_cli --stl-box STL | --stl-view STL OUT VM0..VM8      FUN_18022feb0 / mucbTransformStl
 *   ifk_cli --silhouette MESH(.stl|.iff|.tmp) TOL OUT
 *       Silhouette Boundary kernel (ifk_silhouette.c: Silhouette::Run with IFSilhouette's blob): writes the binary
 *       tree curve file OUT (plain), prints the status line and the diagnostics.
 *   ifk_cli --mc spinsil|silbndy IN.iff OUT TOL [--format F] [--metric M] [--box X0 Y0 Z0 X1 Y1 Z1]
 *       the kernel with tpiftools ComputeMC's data transform around it (peloader --tpif does the same with the DLLs). */
#include "ifk_spinsil.h"
#include "ifk_silhouette.h"
#include "ifk_curvexfer.h"
#include "ifk_mcside.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int print_curvefile(const char *path, int fmt)
{
    IfkCurveFile cf;
    int rc = ifk_read_bin_curves(path, fmt, &cf);
    if (rc) { printf("read error %d\n", rc); return 1; }
    printf("nodes %d segs %d curves %d\n", cf.nnode, cf.nseg, cf.ncurve);
    for (int i = 0; i < cf.nnode; i++) printf("node %d %.17g %.17g %.17g\n", i, cf.node[3 * i], cf.node[3 * i + 1], cf.node[3 * i + 2]);
    for (int k = 0; k < cf.ncurve; k++) {
        printf("curve %d n=%d x=%d,%d:", k, cf.curve_start[k + 1] - cf.curve_start[k], cf.curve_x[2 * k], cf.curve_x[2 * k + 1]);
        for (int i = cf.curve_start[k]; i < cf.curve_start[k + 1]; i++) printf(" %d-%d", cf.cseg[2 * i], cf.cseg[2 * i + 1]);
        printf("\n");
    }
    ifk_curvefile_free(&cf);
    return 0;
}

/* --mc: what tpiftools' ComputeMC does around the kernel (formats/interfacial.md 6.1 / 9.3), for the oracle comparison
 * (tools/peloader --tpif): the box (default: of the input nodes, as tpif.c), getTransVec / CheckDataLocation /
 * CheckDataSize (ifk_mc_transform), the input rewritten on a work copy (ExecDataTranslation, then ExecDataScaling),
 * the kernel at tol x scale (runIfModule), its curve file (CurveXferMC / Surfer::WriteCurves), ReverseTransformation. */
static int mc_run(int argc, char **argv)
{
    const char *kind = argv[2], *in = argv[3], *out = argv[4];
    double tol = atof(argv[5]), lo[3], hi[3];
    int format = 0, metric = 0, havebox = 0, spin = !strcmp(kind, "spinsil");
    for (int i = 6; i < argc; i++) {
        if (!strcmp(argv[i], "--format") && i + 1 < argc) format = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--metric") && i + 1 < argc) metric = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--box") && i + 6 < argc) { for (int k = 0; k < 6; k++) (k < 3 ? lo : hi)[k % 3] = atof(argv[++i]); havebox = 1; }
    }
    IfkMesh m;
    if (ifk_read_bin_surface(in, &m)) { fprintf(stderr, "cannot read %s\n", in); return 1; }
    if (!havebox) {
        for (int k = 0; k < 3; k++) { lo[k] = 1e38; hi[k] = -1e38; }
        for (int i = 0; i < m.nnode; i++) for (int k = 0; k < 3; k++) {
            double v = m.node[3 * i + k]; if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v;
        }
    }
    double t[3], s; int tr, sc;
    double ktol = ifk_mc_transform(lo, hi, tol, metric, spin, t, &s, &tr, &sc);
    if (tr || sc) {   /* the work copy, rewritten in place like ExecDataTranslation / ExecDataScaling */
        char wk[4200]; snprintf(wk, sizeof wk, "%s.in.tmp", out);
        if (ifk_write_bin_surface(wk, &m, 0)) return 1;
        ifk_mesh_free(&m);
        if ((tr && ifk_bin_iff_translate(wk, t)) || (sc && ifk_bin_iff_scale(wk, s)) || ifk_read_bin_surface(wk, &m)) { fprintf(stderr, "transform failed\n"); return 1; }
        remove(wk);
    }
    int rc, nc = 0;
    if (spin) {
        IfkCurves c;
        if (m.ntri == 0) { fprintf(stderr, "empty mesh (mcmsurf FUN_18022feb0 fails the run before the kernel)\n"); ifk_mesh_free(&m); return 1; }
        if (ifk_spinsil_run(&m, ktol, &c) == IFK_SPIN_FAULT) {   /* the DLL faults: no file */
            printf("mc spinsil kernel tol %.17g: %s\n", ktol, c.err);
            ifk_curves_free(&c); ifk_mesh_free(&m); return 3;
        }
        rc = ifk_write_bin_curves(out, &c, format, &nc);
        printf("mc spinsil kernel tol %.17g scale %.17g t %.17g %.17g %.17g: nodes %d segs %d cracked %d curves %d rc %d err \"%s\"\n",
               ktol, s, t[0], t[1], t[2], c.nnode, c.nseg, c.cracked, nc, rc, c.err);
        ifk_curves_free(&c);
    } else {
        IfkSilResult r;
        if (m.ntri == 0) { fprintf(stderr, "empty mesh (mcmsurf FUN_18022feb0 fails the run before the kernel)\n"); ifk_mesh_free(&m); return 1; }
        rc = ifk_silhouette_run(&m, ktol, out, &r);
        printf("mc silbndy kernel tol %.17g scale %.17g t %.17g %.17g %.17g: rc %d curves %d msg \"%s\"\n", ktol, s, t[0], t[1], t[2], rc, r.ncurve, r.msg);
        ifk_sil_result_free(&r);
    }
    ifk_mesh_free(&m);
    if (!rc && (tr || sc)) rc = ifk_reverse_transform(out, t, s, tr, sc);
    return rc != 0;
}

int main(int argc, char **argv)
{
    if (argc >= 6 && !strcmp(argv[1], "--mc")) return mc_run(argc, argv);
    if (argc >= 4 && !strcmp(argv[1], "--read-curves")) return print_curvefile(argv[2], atoi(argv[3]));
    if (argc >= 6 && !strcmp(argv[1], "--hbe")) {
        IfkCurveFile cf;
        if (ifk_read_bin_curves(argv[2], atoi(argv[3]), &cf)) { printf("read error\n"); return 1; }
        for (int k = 0; k < cf.ncurve; k++) {
            IfkHbeCurve h;
            ifk_hbe_records(&cf, k, atoi(argv[4]), atof(argv[5]), 1.0, &h);
            printf("curve %d records %d\n", k, h.n);
            for (int i = 0; i < h.n; i++)
                printf("rec %d type %d first %d %.17g %.17g %.17g -> %.17g %.17g %.17g\n", i, h.r[i].type, h.r[i].first,
                       h.r[i].p[0][0], h.r[i].p[0][1], h.r[i].p[0][2], h.r[i].p[1][0], h.r[i].p[1][1], h.r[i].p[1][2]);
            free(h.r);
        }
        ifk_curvefile_free(&cf);
        return 0;
    }
    if (argc >= 11 && !strcmp(argv[1], "--axis")) {
        double ax[6], m[12], p[3], q[3];
        for (int i = 0; i < 6; i++) ax[i] = atof(argv[2 + i]);
        for (int i = 0; i < 3; i++) p[i] = atof(argv[8 + i]);
        ifk_axis_frame(ax, m);
        printf("frame"); for (int i = 0; i < 12; i++) printf(" %.17g", m[i]); printf("\n");
        ifk_axis_to_kernel(p, m, q);
        printf("kernel %.17g %.17g %.17g\n", q[0], q[1], q[2]);
        ifk_xform_pt(q, m);
        printf("back %.17g %.17g %.17g\n", q[0], q[1], q[2]);
        return 0;
    }
    if (argc >= 11 && !strcmp(argv[1], "--axis-surface")) {
        double ax[6]; for (int i = 0; i < 6; i++) ax[i] = atof(argv[5 + i]);
        int r = ifk_axis_transform_surface(argv[2], ax, argv[3], atoi(argv[4])); printf("ok %d\n", r); return !r;
    }
    if (argc >= 10 && !strcmp(argv[1], "--axis-hbe")) {
        double ax[6]; for (int i = 0; i < 6; i++) ax[i] = atof(argv[4 + i]);
        int r = ifk_axis_back_hbe(argv[2], ax, argv[3]); printf("ok %d\n", r); return !r;
    }
    if (argc >= 3 && !strcmp(argv[1], "--stl-box")) {
        IfkMesh m; if (ifk_read_stl(argv[2], &m)) { printf("read error\n"); return 1; }
        double b[6]; int n = ifk_stl_box(&m, b);
        printf("tris %d box %.17g %.17g %.17g %.17g %.17g %.17g\n", n, b[0], b[1], b[2], b[3], b[4], b[5]);
        ifk_mesh_free(&m); return 0;
    }
    if (argc >= 13 && !strcmp(argv[1], "--stl-view")) {
        IfkMesh m; if (ifk_read_stl(argv[2], &m)) { printf("read error\n"); return 1; }
        double vm[9]; for (int i = 0; i < 9; i++) vm[i] = atof(argv[4 + i]);
        printf("tris %d\n", ifk_transform_stl_view(&m, vm, argv[3]));
        ifk_mesh_free(&m); return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "--scale")) { int rc = ifk_bin_iff_scale(argv[2], atof(argv[3])); printf("rc %d\n", rc); return rc != 0; }
    if (argc >= 6 && !strcmp(argv[1], "--translate")) {
        double t[3] = { atof(argv[3]), atof(argv[4]), atof(argv[5]) };
        int rc = ifk_bin_iff_translate(argv[2], t); printf("rc %d\n", rc); return rc != 0;
    }
    if (argc >= 9 && !strcmp(argv[1], "--reverse")) {
        double t[3] = { atof(argv[3]), atof(argv[4]), atof(argv[5]) };
        int rc = ifk_reverse_transform(argv[2], t, atof(argv[6]), atoi(argv[7]), atoi(argv[8])); printf("rc %d\n", rc); return rc != 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "--write-curves")) {
        FILE *f = fopen(argv[2], "r"); if (!f) { fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
        IfkCurves c; memset(&c, 0, sizeof c);
        char line[256];
        while (fgets(line, sizeof line, f)) {
            double x, y, z; int a, b;
            if (sscanf(line, "n %lf %lf %lf", &x, &y, &z) == 3) {
                c.node = realloc(c.node, sizeof(double) * 3 * (size_t)(c.nnode + 1));
                c.node[3 * c.nnode] = x; c.node[3 * c.nnode + 1] = y; c.node[3 * c.nnode + 2] = z; c.nnode++;
            } else if (sscanf(line, "s %d %d", &a, &b) == 2) {
                c.seg = realloc(c.seg, sizeof(int) * 2 * (size_t)(c.nseg + 1));
                c.seg[2 * c.nseg] = a; c.seg[2 * c.nseg + 1] = b; c.nseg++;
            }
        }
        fclose(f);
        int nc, rc = ifk_write_bin_curves(argv[3], &c, atoi(argv[4]), &nc);
        printf("rc %d curves %d\n", rc, nc);
        ifk_curves_free(&c);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "--silhouette")) {
        IfkMesh m; const char *p = argv[2]; size_t n = strlen(p);
        int rc = (n > 4 && (!strcmp(p + n - 4, ".iff") || !strcmp(p + n - 4, ".tmp"))) ? ifk_read_bin_surface(p, &m) : ifk_read_stl(p, &m);
        if (rc) { fprintf(stderr, "cannot read %s (%d)\n", p, rc); return 1; }
        IfkSilResult r;
        rc = ifk_silhouette_run(&m, atof(argv[3]), argv[4], &r);
        printf("rc %d tree nodes %d (removed %d) file nodes %d edges %d curves %d msg \"%s\"\n", rc, r.nnode - 1,
               2 * r.nremoved, r.fnode, r.fedge, r.ncurve, r.msg);
        printf("stitch levels %d points %d moved %d degenerate %d split %d ties %d cancelled pairs %d unions %d "
               "warnings 59/29 %d/%d planecut messages %d\n", r.nlevel, r.nstitch_pt, r.nmoved, r.ndegenerate, r.nsplit,
               r.nties, r.ncancel, r.nunion, r.nwarn59, r.nwarn29, r.pc_nerr);
        ifk_sil_result_free(&r); ifk_mesh_free(&m);
        return rc != 0;
    }
    if (argc < 3) { fprintf(stderr, "usage: ifk_cli MESH TOL [--svg OUT] [--curves OUT [FORMAT]] | --write-curves IN OUT FMT | --read-curves FILE FMT | --scale / --translate / --reverse | --silhouette MESH TOL OUT\n"); return 2; }
    IfkMesh m; IfkCurves c;
    const char *p = argv[1]; size_t n = strlen(p);
    int rc = (n > 4 && (!strcmp(p + n - 4, ".iff") || !strcmp(p + n - 4, ".tmp"))) ? ifk_read_bin_surface(p, &m) : ifk_read_stl(p, &m);
    if (rc) { fprintf(stderr, "cannot read %s (%d)\n", p, rc); return 1; }
    double tol = atof(argv[2]);
    ifk_spinsil_run(&m, tol, &c);
    printf("nodes %d segs %d cracked %d overflow %d err \"%s\"\n", c.nnode, c.nseg, c.cracked, c.overflow, c.err);
    for (int i = 0; i < c.nnode; i++) printf("node %d %.17g %.17g\n", i, c.node[3 * i], c.node[3 * i + 1]);
    for (int i = 0; i < c.nseg; i++) printf("seg %d %d %d\n", i, c.seg[2 * i], c.seg[2 * i + 1]);
    for (int a = 3; a + 1 < argc; a++) {
        if (!strcmp(argv[a], "--curves")) {
            int fmt = (a + 2 < argc && argv[a + 2][0] != '-') ? atoi(argv[a + 2]) : 0, nc;
            int wrc = ifk_write_bin_curves(argv[a + 1], &c, fmt, &nc);
            fprintf(stderr, "curve file %s: rc %d curves %d\n", argv[a + 1], wrc, nc);
        }
        if (!strcmp(argv[a], "--svg")) {
            FILE *f = fopen(argv[a + 1], "w");
            if (f) {
                double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
                for (int i = 0; i < c.nnode; i++) {
                    double x = c.node[3 * i], y = c.node[3 * i + 1];
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
                double w = x1 - x0 + 1e-9, h = y1 - y0 + 1e-9, s = 800.0 / (w > h ? w : h);
                fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d'>\n", (int)(w * s) + 20, (int)(h * s) + 20);
                for (int i = 0; i < c.nseg; i++) {
                    const double *pa = c.node + 3 * c.seg[2 * i], *pb = c.node + 3 * c.seg[2 * i + 1];
                    fprintf(f, "<line x1='%.3f' y1='%.3f' x2='%.3f' y2='%.3f' stroke='black' stroke-width='1'/>\n",
                            10 + (pa[0] - x0) * s, 10 + (y1 - pa[1]) * s, 10 + (pb[0] - x0) * s, 10 + (y1 - pb[1]) * s);
                }
                fprintf(f, "</svg>\n");
                fclose(f);
            }
        }
    }
    ifk_curves_free(&c); ifk_mesh_free(&m);
    return 0;
}
