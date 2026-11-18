/* sil_test.c - checks for ifk_silhouette_walls on a box, a faceted cylinder (axis Z) and a UV sphere. */
#include "ifk_silhouette_in.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern int ifk_sil_exact_calls;

typedef struct { double *v; int nv, cv; int *t; int nt, ct; } Mesh;
static int addv(Mesh *m, double x, double y, double z)
{
    if (m->nv == m->cv) { m->cv = m->cv ? 2 * m->cv : 64; m->v = realloc(m->v, (size_t)m->cv * 3 * sizeof(double)); }
    m->v[3 * m->nv] = x; m->v[3 * m->nv + 1] = y; m->v[3 * m->nv + 2] = z;
    return m->nv++;
}
static void addt(Mesh *m, int a, int b, int c)
{
    if (m->nt == m->ct) { m->ct = m->ct ? 2 * m->ct : 64; m->t = realloc(m->t, (size_t)m->ct * 3 * sizeof(int)); }
    m->t[3 * m->nt] = a; m->t[3 * m->nt + 1] = b; m->t[3 * m->nt + 2] = c; m->nt++;
}
static void quad(Mesh *m, int a, int b, int c, int d) { addt(m, a, b, c); addt(m, a, c, d); }

/* expected edge list (input node pairs) vs result (unique nodes, unordered) */
static int check(const char *name, const Mesh *m, const int *exp, int nexp)
{
    IfkSilWalls w;
    ifk_sil_exact_calls = 0;
    int rc = ifk_silhouette_walls(m->v, m->nv, m->t, m->nt, 0.0, 90.0, 1, &w), ok = (rc == 0);
    if (rc != 0) printf("  rc = %d\n", rc);
    if (w.nsil != nexp) { ok = 0; printf("  nsil = %d, expected %d\n", w.nsil, nexp); }
    for (int i = 0; i < nexp; i++) {
        int a = w.node_of_input[exp[2 * i]], b = w.node_of_input[exp[2 * i + 1]], hit = 0;
        for (int j = 0; j < w.nsil; j++)
            if ((w.sil_edge[2 * j] == a && w.sil_edge[2 * j + 1] == b) ||
                (w.sil_edge[2 * j] == b && w.sil_edge[2 * j + 1] == a)) hit = 1;
        if (!hit) { ok = 0; printf("  missing edge %d-%d\n", exp[2 * i], exp[2 * i + 1]); }
    }
    /* wall triangles: (start(x,y,0), end(x,y,0), mid(z=1)), ids 1..n; each sil end node once in wnode */
    if (w.nwtri != w.nsil) ok = 0;
    for (int j = 0; j < w.nwtri; j++) {
        const double *s = w.wnode + 3 * w.wtri[3 * j], *e = w.wnode + 3 * w.wtri[3 * j + 1];
        const double *mm = w.wnode + 3 * w.wtri[3 * j + 2];
        const double *ns = w.node + 3 * w.sil_edge[2 * j], *ne = w.node + 3 * w.sil_edge[2 * j + 1];
        if (w.wtri_id[j] != j + 1 || s[2] != 0.0 || e[2] != 0.0 || mm[2] != 1.0 || s[0] != ns[0] || s[1] != ns[1] ||
            e[0] != ne[0] || e[1] != ne[1] || fabs(mm[0] - (s[0] + e[0]) * 0.5) > 1e-6 ||
            fabs(mm[1] - (s[1] + e[1]) * 0.5) > 1e-6) { ok = 0; printf("  bad wall triangle %d\n", j); }
    }
    printf("%s: exact-path %d, nodes %d, kept %d (vertical %d, angle %d), sil edges %d, wall nodes %d, wall tris %d, "
           "box [%g %g %g %g] -> %s\n", name, ifk_sil_exact_calls, w.nnode, w.nkept, w.ndrop_vertical, w.ndrop_angle, w.nsil, w.nwnode,
           w.nwtri, w.box[0], w.box[1], w.box[2], w.box[3], ok ? "PASS" : "FAIL");
    ifk_silhouette_walls_free(&w);
    return ok;
}

int main(void)
{
    int all = 1;
    { /* box 3 x 2 x 1.5 at (0.3, -0.7, 0.1) */
        Mesh m = { 0 }; int v[8];
        for (int i = 0; i < 8; i++) v[i] = addv(&m, 0.3 + 3.0 * (i & 1), -0.7 + 2.0 * ((i >> 1) & 1), 0.1 + 1.5 * (i >> 2));
        quad(&m, v[0], v[2], v[3], v[1]); quad(&m, v[4], v[5], v[7], v[6]);   /* bottom (down), top (up) */
        quad(&m, v[0], v[1], v[5], v[4]); quad(&m, v[2], v[6], v[7], v[3]);
        quad(&m, v[0], v[4], v[6], v[2]); quad(&m, v[1], v[3], v[7], v[5]);
        int exp[16] = { 0,1, 1,3, 3,2, 2,0, 4,5, 5,7, 7,6, 6,4 };
        all &= check("box", &m, exp, 8);
        free(m.v); free(m.t);
    }
    { /* cylinder r 2, z 0..5, 24 segments, centre fans */
        Mesh m = { 0 }; int n = 24, b[24], t[24];
        int cb = addv(&m, 0.0, 0.0, 0.0), ct = addv(&m, 0.0, 0.0, 5.0);
        for (int i = 0; i < n; i++) {
            double a = 2.0 * 3.141592653589793 * i / n;
            b[i] = addv(&m, 2.0 * cos(a), 2.0 * sin(a), 0.0); t[i] = addv(&m, 2.0 * cos(a), 2.0 * sin(a), 5.0);
        }
        int *exp = malloc(sizeof(int) * 4 * (size_t)n);
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            addt(&m, cb, b[j], b[i]); addt(&m, ct, t[i], t[j]); quad(&m, b[i], b[j], t[j], t[i]);
            exp[4 * i] = b[i]; exp[4 * i + 1] = b[j]; exp[4 * i + 2] = t[i]; exp[4 * i + 3] = t[j];
        }
        all &= check("cylinder", &m, exp, 2 * n);
        free(exp); free(m.v); free(m.t);
    }
    { /* UV sphere r 3, 32 segments, 16 stacks (equator = stack 8), centre (1, 2, 0) */
        Mesh m = { 0 }; int ns = 32, nst = 16, idx[17][32];
        int np = addv(&m, 1.0, 2.0, 3.0), sp = addv(&m, 1.0, 2.0, -3.0);
        for (int k = 1; k < nst; k++) {
            double ph = 3.141592653589793 * k / nst;
            for (int i = 0; i < ns; i++) {
                double th = 2.0 * 3.141592653589793 * i / ns;
                idx[k][i] = addv(&m, 1.0 + 3.0 * sin(ph) * cos(th), 2.0 + 3.0 * sin(ph) * sin(th),
                                 k == nst / 2 ? 0.0 : 3.0 * cos(ph));
            }
        }
        for (int i = 0; i < ns; i++) {
            int j = (i + 1) % ns;
            addt(&m, np, idx[1][i], idx[1][j]);
            addt(&m, sp, idx[nst - 1][j], idx[nst - 1][i]);
            for (int k = 1; k + 1 < nst; k++) quad(&m, idx[k][i], idx[k + 1][i], idx[k + 1][j], idx[k][j]);
        }
        int exp[64];
        for (int i = 0; i < ns; i++) { exp[2 * i] = idx[nst / 2][i]; exp[2 * i + 1] = idx[nst / 2][(i + 1) % ns]; }
        all &= check("sphere", &m, exp, ns);
        free(m.v); free(m.t);
    }
    printf("%s\n", all ? "ALL PASS" : "SOME FAIL");
    return all ? 0 : 1;
}
