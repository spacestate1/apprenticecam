/* cui_cedge.c - the solid side of Curve One Edge / Curve All Edges (formats/wireframe.md "Curves on surfaces, solid edges"): the edge data per part solid (occt_edges.cpp),
 * the edge pick (FUN_1802a2940's event 0x4000), Mastercam's lists of solids / faces (FUN_1802c4a70, FUN_1802c3f10 select_ents mask 0x10100), the conversion sld_convert_tag_to_edges -> ps_edge_to_mc_curve
 * and surface_open_edges. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "core_ui.h"
#include "../occt_cad.h"
#include "../occt_mesh.h"
#include "../mcam_saveops.h"
#include "../mcsrfcrv.h"
#include "../occt_prim.h"
#include "../mcsurf_build.h"
#include "../mcam_write.h"

static struct { const void *key; OccEdge *e; int n, nf; } sc[16];
static int sc_nn;
static const OccEdge *ce_edges(int si, int *n, int *nf, char *msg, size_t msgn) {
    if (si < 0 || si >= F.nsolids) { snprintf(msg, msgn, "No such solid"); return NULL; }
    const void *k = F.solids[si].brep ? (const void *)F.solids[si].brep : (const void *)F.solids[si].xt;
    for (int i = 0; i < sc_nn; i++) if (sc[i].key == k) { *n = sc[i].n; if (nf) *nf = sc[i].nf; return sc[i].e; }
    double scale = !mcam_is_metric(&F) ? 1000.0 / 25.4 : 1000.0;   /* XT metres (srf_solid_exec) */
    OccEdge *e = NULL; int ne = 0, nfa = 0; char err[200] = "";
    int r = F.solids[si].brep ? occt_brep_edge_list(F.solids[si].brep, F.solids[si].brep_n, &e, &ne, &nfa, err, sizeof err)
                               : occt_xt_edges(F.solids[si].xt, F.solids[si].xt_size, scale, &e, &ne, &nfa, err, sizeof err);
    if (r) { snprintf(msg, msgn, "Solid edges: %s", err); return NULL; }
    if (sc_nn == 16) { occt_edges_free(sc[0].e, sc[0].n); memmove(sc, sc + 1, sizeof sc[0] * 15); sc_nn--; }
    sc[sc_nn].key = k; sc[sc_nn].e = e; sc[sc_nn].n = ne; sc[sc_nn].nf = nfa; sc_nn++;
    *n = ne; if (nf) *nf = nfa; return e;
}
/* the edge nearest the cursor within the pick aperture (ours: the sampled polylines, as sld_edge_hits_all over the part solids; FUN_1802a2940's event 0x4000) */
int ce_pick_edge(float mx, float my, int *sio, int *eio) {
    char m[200]; float sx = mx - vp_x, sy = my - vp_y; double best = 10.0 * 10.0;   /* SLD_PICK_PX of cui_snap.c */
     int found = 0;
    for (int si = 0; si < F.nsolids; si++) {
        int n; const OccEdge *E = ce_edges(si, &n, NULL, m, sizeof m); if (!E) continue;
        for (int i = 0; i < n; i++) {
            float pax = 0, pay = 0; int have = 0;
            for (int k = 0; k < E[i].np; k++) { float ax, ay; if (!scene_to_screen(vp_w, vp_h, E[i].pts[3 * k], E[i].pts[3 * k + 1], E[i].pts[3 * k + 2], &ax, &ay)) { have = 0; continue; }
                if (have) { double dx = ax - pax, dy = ay - pay, l2 = dx * dx + dy * dy, u = l2 > 0 ? ((sx - pax) * dx + (sy - pay) * dy) / l2 : 0; if (u < 0) u = 0; if (u > 1) u = 1; double ex = pax + u * dx - sx, ey = pay + u * dy - sy, dd = ex * ex + ey * ey;
                    if (dd < best) { double wx = E[i].pts[3 * (k - 1)] + u * (E[i].pts[3 * k] - E[i].pts[3 * (k - 1)]), wy = E[i].pts[3 * (k - 1) + 1] + u * (E[i].pts[3 * k + 1] - E[i].pts[3 * (k - 1) + 1]), wz = E[i].pts[3 * (k - 1) + 2] + u * (E[i].pts[3 * k + 2] - E[i].pts[3 * (k - 1) + 2]);
                        if (sld_pos_visible(wx, wy, wz)) { best = dd; *sio = si; *eio = i; found = 1; } } }
                pax = ax; pay = ay; have = 1; }
        }
    }
    return found;
}
int ce_emit_one(const OccEdge *E, int level, int color, int *made, char *msg, size_t msgn) {
    double systol = mc_systol_units(mcam_is_metric(&F))->tol;
    mcam_fresh_views(&F);
    int out[4], mk = 0; const char *why = "";
    int ok = mcsc_emit_edge(&F, E->c.type, E->c.p, E->c.c, E->c.n, E->c.r, E->c.deg, E->c.nk, E->c.nc, E->c.rat, E->c.k, E->c.cp, E->along, E->pk_along, systol, level, color, out, 4, &mk, &why);
    if (!ok) snprintf(msg, msgn, "Solid edge: %s", why);
    if (made) *made += mk;
    return ok;
}
int ce_sld_edge_one(int si, int ei, int level, int color, char *msg, size_t msgn) {
    int n, made = 0; const OccEdge *E = ce_edges(si, &n, NULL, msg, msgn);
    if (!E || ei < 0 || ei >= n) return 0;
    return ce_emit_one(&E[ei], level, color, &made, msg, msgn);
}
/* sld_convert_tag_to_edges (0x1800b1890) body (fi < 0) or face: open_only: PK_BODY_find_laminar_edges of a sheet (f[1] = -1, this solid is a sheet: occt_xt_body_type / occt_brep_body_type; a closed solid gives nothing);
   else the body's / face's edges, each once (FUN_180096570). Order: ascending XT EDGE (PK_BODY_ask_edges' order NOT DECODED). */
int ce_sld_edges(int si, int fi, int open_only, int level, int color, int *made, char *msg, size_t msgn) {
    int n; const OccEdge *E = ce_edges(si, &n, NULL, msg, msgn); if (!E) return 0;
    if (open_only) {                                       /* param_3 != 0: PK_BODY_ask_type == sheet (0x15e2) */
        int bt = F.solids[si].brep ? occt_brep_body_type(F.solids[si].brep, F.solids[si].brep_n) : occt_xt_body_type(F.solids[si].xt, F.solids[si].xt_size);
        if (bt != 3) return 1;
    }
    for (int i = 0; i < n; i++) {
        if (open_only && E[i].f[1] >= 0) continue;         /* laminar: one face only */
        if (fi >= 0 && E[i].f[0] != fi && E[i].f[1] != fi) continue;
        if (!ce_emit_one(&E[i], level, color, made, msg, msgn)) return 0;
    }
    return 1;
}
/* surface_open_edges (mcgeomsld 0x1800d0a30): the selected surfaces / sheets sewn (occt_open_edges), the free edges through ps_edge_to_mc_curve. tol = the Only Open Edges tolerance (its dialog value; FUN_180224880) */
/* decision N9-28 (formats/user_decisions.md): Only Open Edges refuses a selection holding a trimmed surface until true trim curves exist (ps_surf_to_ps_sheet sews the real trims; the stored uv polygon would break the free edges at every point). The whole selection, because stitching the rest alone would report the edges shared with the trimmed surface as open. 1 = refused */
int ce_open_refused(const int *surf_ents, int ns, char *msg, size_t msgn) {
    for (int i = 0; i < ns; i++) if (surf_ents[i] >= 0 && surf_ents[i] < F.nents && F.ents[surf_ents[i]].type == E_SURF && F.surfs[F.ents[surf_ents[i]].srf].kind == 10) { snprintf(msg, msgn, "Only Open Edges: a trimmed surface is NOT PORTED (its true trim curves are needed)"); return 1; }
    return 0;
}
int ce_open_edges(const int *surf_ents, int ns, double tol, int level, int color, int *made, char *msg, size_t msgn) {
    if (ce_open_refused(surf_ents, ns, msg, msgn)) return 0;
    OccOpenSurf *S = calloc((size_t)(ns ? ns : 1), sizeof(OccOpenSurf)); double **cps = calloc((size_t)(ns ? ns : 1), sizeof(double *));
    int nsurf = 0, nsheet = 0, rc = 1, skipped = 0;
    for (int i = 0; i < ns; i++) {
        if (surf_ents[i] < 0 || surf_ents[i] >= F.nents || F.ents[surf_ents[i]].type != E_SURF) continue;
        const Surf *s = &F.surfs[F.ents[surf_ents[i]].srf];
        int nu, nv, du, dv, nku, nkv; const double *ku, *kv; double *cp;
        if (!mcsc_open_surf(&F, s, &nu, &nv, &du, &dv, &nku, &nkv, &ku, &kv, &cp)) { skipped++; continue; }
        S[nsurf] = (OccOpenSurf){ nu, nv, du, dv, nku, nkv, ku, kv, cp }; cps[nsurf] = cp; nsurf++;
    }
    if (skipped) snprintf(msg, msgn, "Only Open Edges: %d surface(s) of a non-NURBS kind were skipped (NOT PORTED)", skipped);
    OccEdge *E = NULL; int n = 0; char err[200] = "";
    if (nsurf && occt_open_edges(S, nsurf, tol, &E, &n, &nsheet, err, sizeof err)) { snprintf(msg, msgn, "Only Open Edges: %s", err); rc = 0; }
    for (int i = 0; i < n && rc; i++) if (!ce_emit_one(&E[i], level, color, made, msg, msgn)) rc = 0;
    if (E) occt_edges_free(E, n);
    for (int i = 0; i < nsurf; i++) free(cps[i]);
    free(S); free(cps);
    return rc;
}
/* tests: a line "sedge si ei type along x0 y0 z0 x1 y1 z1 ..." per edge of the solid, in the conversion's order */
void ce_dump_edges(int si) {
    char m[200]; int n, nf; const OccEdge *E = ce_edges(si, &n, &nf, m, sizeof m);
    if (!E) { printf("      0 sedge err %s\n", m); return; }
    printf("      0 sedge solid %d edges %d faces %d\n", si, n, nf);
    for (int i = 0; i < n; i++) printf("      0 sedge %d t %d al %d nk %d nc %d deg %d rat %d p %.6f %.6f %.6f %.6f %.6f %.6f f %d %d\n", i, E[i].c.type, E[i].along, E[i].c.nk, E[i].c.nc, E[i].c.deg, E[i].c.rat, E[i].c.p[0], E[i].c.p[1], E[i].c.p[2], E[i].c.p[3], E[i].c.p[4], E[i].c.p[5], E[i].f[0], E[i].f[1]);
}

/* tests (hook sldscan): stored curves against the part's solid edges: for each stored line / arc / NURBS whose two ends equal a solid edge's (either order, 1e-5 of the part), how the two agree; arcs: the stored view's z . the XT circle's normal, the sweep's sign, the edge's along. Prints "sldscan ..." lines and a summary. */
void ce_scan(void) {
    int tot[4] = { 0 }, match[4] = { 0 }, cp_eq = 0, cp_ne = 0, knot_ne = 0; double tol = mcam_is_metric(&F) ? 0.0025 : 1e-4;
    for (int si = 0; si < F.nsolids; si++) {
        char m[200]; int n, nf; const OccEdge *E = ce_edges(si, &n, &nf, m, sizeof m);
        if (!E) { printf("sldscan solid %d err %s\n", si, m); continue; }
        for (int i = 0; i < n; i++) {
            int t = E[i].c.type; if (t < 1 || t > 3) continue;
            tot[t]++;
            const double *a = E[i].c.p, *b = E[i].c.p + 3;
            for (int e = 0; e < F.nents; e++) {
                const Ent *g = &F.ents[e]; if (g->type != E_LINE && g->type != E_ARC && g->type != E_SPLINE) continue;
                double s[3] = { g->a.x, g->a.y, g->a.z }, en[3] = { g->b.x, g->b.y, g->b.z };
                if (g->type == E_ARC) { const double *X = g->vw ? g->vm : (const double[9]){ 1, 0, 0, 0, 1, 0, 0, 0, 1 }; double ca = cos(g->a0), sa = sin(g->a0), ce2 = cos(g->a0 + g->sw), se = sin(g->a0 + g->sw); for (int k = 0; k < 3; k++) { s[k] = (k == 0 ? g->c.x : k == 1 ? g->c.y : g->c.z) + g->r * (ca * X[k] + sa * X[3 + k]); en[k] = (k == 0 ? g->c.x : k == 1 ? g->c.y : g->c.z) + g->r * (ce2 * X[k] + se * X[3 + k]); } }
                double d1 = hypot(hypot(s[0] - a[0], s[1] - a[1]), s[2] - a[2]) + hypot(hypot(en[0] - b[0], en[1] - b[1]), en[2] - b[2]), d2 = hypot(hypot(s[0] - b[0], s[1] - b[1]), s[2] - b[2]) + hypot(hypot(en[0] - a[0], en[1] - a[1]), en[2] - a[2]);
                int same = d1 < 2 * tol, swapped = d2 < 2 * tol;
                if (!same && !swapped) continue;
                if ((t == 1 && g->type != E_LINE) || (t == 2 && g->type != E_ARC) || (t == 3 && g->type != E_SPLINE)) continue;
                match[t]++;
                if (t == 1) printf("sldscan line solid %d edge %d ent %d along %d stored-order %d\n", si, i, e, E[i].along, same ? 1 : -1);
                if (t == 2) { const double *n2 = E[i].c.n; double vz[3] = { 0, 0, 1 }; if (g->vw) memcpy(vz, g->vm + 6, 24); double kz = n2[0] * vz[0] + n2[1] * vz[1] + n2[2] * vz[2];
                    printf("sldscan arc solid %d edge %d ent %d along %d stored-order %d kz %.3f sweep %.4f r %.4f vw %d pk %d\n", si, i, e, E[i].along, same ? 1 : -1, kz, g->sw, E[i].c.r, g->vw, E[i].pk_along); }
                if (t == 3) { const CurveRec *c = g->crv >= 0 && g->crv < F.ncrvs ? &F.crvs[g->crv] : NULL; if (!c || c->n < 0xd8) continue; int nk = *(const int *)(c->rec + 0xc8), ncp = *(const int *)(c->rec + 0xcc), deg = c->rec[0xd0], vpp = c->rec[0xd1]; const double *K = (const double *)(c->rec + 0xd8);
                    int ok = deg == E[i].c.deg && nk == E[i].c.nk && ncp == E[i].c.nc;
                    double kd = 0, pd = 0;
                    if (ok) { double ks0 = K[0], kss = K[nk - 1] - K[0], ke0 = E[i].c.k[0], kse = E[i].c.k[nk - 1] - E[i].c.k[0]; for (int j = 0; j < nk; j++) { double x = (K[j] - ks0) / kss, y = (E[i].c.k[j] - ke0) / kse, z = fabs(x - (same ? y : 1 - (E[i].c.k[nk - 1 - j] - ke0) / kse)); if (z > kd) kd = z; }
                        const double *Pc = K + nk; for (int j = 0; j < ncp; j++) { const double *q = Pc + (size_t)j * vpp, *w = E[i].c.cp + 4 * (same ? j : ncp - 1 - j); double wa = vpp == 4 ? q[0] : 1, wb = w[0]; const double *ax = vpp == 4 ? q + 1 : q; double dd = 0; for (int z = 0; z < 3; z++) dd += (ax[z] / wa - w[z + 1] / wb) * (ax[z] / wa - w[z + 1] / wb); if (sqrt(dd) > pd) pd = sqrt(dd); } }
                    printf("sldscan nurbs solid %d edge %d ent %d along %d stored-order %d deg %d %d nk %d %d ncp %d %d knotdiff %.3e cpdiff %.3e\n", si, i, e, E[i].along, same ? 1 : -1, deg, E[i].c.deg, nk, E[i].c.nk, ncp, E[i].c.nc, kd, pd);
                    if (ok && pd < 1e-3) cp_eq++; else cp_ne++; if (ok && kd > 1e-4) knot_ne++; }
                break;
            }
        }
    }
    printf("sldscan summary solids %d lines %d/%d arcs %d/%d nurbs %d/%d cp_eq %d cp_ne %d knot_ne %d\n", F.nsolids, match[1], tot[1], match[2], tot[2], match[3], tot[3], cp_eq, cp_ne, knot_ne);
}

/* Curve Dynamic / Constant Parameter: a solid face taken as a surface (FUN_180282390 / FUN_1802980f0 event 0x2000: sld_face_to_temp_file + read_next_solid_surf_from_temp_file, the same conversion as Line Normal: occt_xt_faces / occt_brep_faces, mcsb_prim_nurbs). Only a face whose loops fill the NURBS (mcsb_prim_untrimmed) is handled here; a trimmed face is refused with a message: the dyno / the iso curves need the trim loops as a stored kind 10 surface. */
static Surf ce_temp; const Surf *ce_temp_surf = &ce_temp;
int ce_face_temp(int si, int fi, char *msg, size_t msgn) {
    if (si < 0 || si >= F.nsolids) { snprintf(msg, msgn, "No such solid"); return 0; }
    double scale = !mcam_is_metric(&F) ? 1000.0 / 25.4 : 1000.0;
    OpFace *fs; int nf; char err[200];
    if (F.solids[si].brep ? occt_brep_faces(F.solids[si].brep, F.solids[si].brep_n, &fs, &nf, err, sizeof err) : occt_xt_faces(F.solids[si].xt, F.solids[si].xt_size, scale, &fs, &nf, err, sizeof err)) { snprintf(msg, msgn, "Solid face: %s", err); return 0; }
    if (fi < 0 || fi >= nf) { occt_prim_free(fs, nf); snprintf(msg, msgn, "Solid face: no such face"); return 0; }
    double tol = mc_systol_units(mcam_is_metric(&F))->tol; uint32_t n; uint8_t *b = mcsb_prim_nurbs(&F, &fs[fi], 0, tol, &n);
    if (!b) { occt_prim_free(fs, nf); snprintf(msg, msgn, "Solid face: this face has no NURBS form here"); return 0; }
    double **P = NULL; int *c = NULL; int nl = mcsb_prim_loops(&F, b, n, &fs[fi], &P, &c), un = nl <= 0 || mcsb_prim_untrimmed(&F, b, n, nl, P, c);
    for (int l = 0; l < (nl > 0 ? nl : 0); l++) free(P[l]); free(P); free(c);
    occt_prim_free(fs, nf);
    if (!un) { free(b); free(ce_temp.rec); memset(&ce_temp, 0, sizeof ce_temp); snprintf(msg, msgn, "Solid face: a trimmed solid face (its loops do not fill the patch) is NOT PORTED here"); return 0; }
    free(ce_temp.rec); memset(&ce_temp, 0, sizeof ce_temp); ce_temp.rec = b; ce_temp.n = n; ce_temp.kind = b[0x68];
    return 1;
}
