/* cui_feat.c - the Extrude feature list the document stores (EXTRUDE / XCHAIN project lines) and the bodies it
   rebuilds (formats/extrude.md; the command itself, ex_*, is still in the front end). Moved from viewer/mcamui.c. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"

ExFeat *feats; int nfeats;
Body *sbodies; int nsbodies;
/* default names sld 707 "Extrude", 706 "Extrude Cut", 705 "Extrude Boss" (FUN_1800db220) */
const char *ex_defname(int type) { return type == EX_CUT ? "Extrude Cut" : type == EX_BOSS ? "Extrude Boss" : "Extrude"; }
int ex_is_defname(const char *n) { return !strcmp(n, "Extrude") || !strcmp(n, "Extrude Cut") || !strcmp(n, "Extrude Boss"); }
/* sld_get_num_solids_visible: the bodies made here (solids read from the part file cannot be changed by this kernel) */
int ex_nvis(void) { return nsbodies; }

void chain_copy_into(Chain *d, const Chain *s) {
    memset(d, 0, sizeof *d);     /* fix 2026-10-21: cut0 / cut1 (read by cam_chain_polyline) were left uninitialised */
    d->n = s->n; d->closed = s->closed;
    d->ent = malloc(sizeof(int) * (s->n ? s->n : 1)); d->rev = malloc(sizeof(int) * (s->n ? s->n : 1));
    memcpy(d->ent, s->ent, sizeof(int) * s->n); memcpy(d->rev, s->rev, sizeof(int) * s->n);
}
ExFeat feat_dup(const ExFeat *f) {
    ExFeat r = *f; r.ch = malloc(sizeof(Chain) * (f->nch ? f->nch : 1)); r.cdir = malloc(sizeof(int) * (f->nch ? f->nch : 1));
    for (int i = 0; i < f->nch; i++) { chain_copy_into(&r.ch[i], &f->ch[i]); r.cdir[i] = f->cdir ? f->cdir[i] : 1; }
    return r;
}
void feat_free(ExFeat *f) { for (int i = 0; i < f->nch; i++) cam_chain_free(&f->ch[i]); free(f->ch); free(f->cdir); f->ch = NULL; f->cdir = NULL; f->nch = 0; }
/* undo record payload: the features one OK / Apply made (sec. 6: one per body / group) */
void feat_release(void *p) { FeatPack *k = p; if (!k) return; for (int i = 0; i < k->n; i++) feat_free(&k->f[i]); free(k->f); free(k); }

/* the chain lies in one plane parallel to XY (all entities planar, one Z within nc_tol); *z = that Z */
int ex_chain_xy(const Chain *c, double *z) {
    double z0 = 0; int first = 1;
    for (int k = 0; k < c->n; k++) {
        if (c->ent[k] < 0 || c->ent[k] >= F.nents) return 0;
        const Ent *en = &F.ents[c->ent[k]]; if (!en->type) return 0;
        if (en->type != E_POINT && !en->planar) return 0;
        double ez = en->type == E_ARC ? en->c.z : en->a.z;
        if (first) { z0 = ez; first = 0; } else if (fabs(ez - z0) > EX_NCTOL) return 0;
    }
    if (z) *z = z0;
    return 1;
}
/* chn_get_normal for a chain in an XY plane (decomp/extrude/b7.c): the plane's normal; a CLOSED chain (CHAIN+8) is
   flipped when its signed turning-angle sum (FUN_18000bf30: the tangent angle at every vertex plus each arc's sweep)
   is -2 pi within tol, strictly - i.e. it winds clockwise. The sum is taken on the flattened polyline (ours;
   FUN_18000bf30 evaluates exact per-entity curve tangents - identical +/- 2 pi for the simple loops chaining
   produces). Open chains keep the plane normal unflipped: 0 = no flip, the callers use the reference +Z. */
int ex_chain_normal(const Chain *c) {
    if (!c->closed) return 0;
    double *xy = NULL, z; int n = cam_chain_polyline(&F, c, 0.01, &xy, &z);
    if (n > 1 && fabs(xy[0] - xy[2 * n - 2]) < 1e-9 && fabs(xy[1] - xy[2 * n - 1]) < 1e-9) n--;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        int a = (i + n - 1) % n, b = (i + 1) % n;
        double ux = xy[2 * i] - xy[2 * a], uy = xy[2 * i + 1] - xy[2 * a + 1];
        double vx = xy[2 * b] - xy[2 * i], vy = xy[2 * b + 1] - xy[2 * i + 1];
        double ul = hypot(ux, uy), vl = hypot(vx, vy);
        if (ul < 1e-12 || vl < 1e-12) continue;
        sum += atan2((ux * vy - uy * vx) / (ul * vl), (ux * vx + uy * vy) / (ul * vl));
    }
    free(xy);
    return fabs(sum + 2 * M_PI) < EX_TOL ? -1 : 1;
}

/* the chain indices of one sheet group (sec. 4.5) */
int ex_wind(const double *p, int n, double x, double y) {        /* winding number of (x, y) in a closed polyline */
    int w = 0;
    for (int k = 0; k < n; k++) {
        double ax = p[2 * k], ay = p[2 * k + 1], bx = p[2 * ((k + 1) % n)], by = p[2 * ((k + 1) % n) + 1];
        double c = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
        if (ay <= y) { if (by > y && c > 0) w++; } else if (by <= y && c < 0) w--;
    }
    return w;
}
double ex_pdist(const double *p, int n, double x, double y) {    /* distance of (x, y) to a closed polyline */
    double best = 1e300;
    for (int k = 0; k < n; k++) {
        double ax = p[2 * k], ay = p[2 * k + 1], ex = p[2 * ((k + 1) % n)] - ax, ey = p[2 * ((k + 1) % n) + 1] - ay;
        double l2 = ex * ex + ey * ey, t = l2 > 1e-24 ? ((x - ax) * ex + (y - ay) * ey) / l2 : 0;
        if (t < 0) t = 0; if (t > 1) t = 1;
        double d = hypot(x - (ax + t * ex), y - (ay + t * ey));
        if (d < best) best = d;
    }
    return best;
}
/* even-depth loop i starts a group and takes its direct children (the holes); its grandchildren (islands) are
   emitted as groups of their own right after it (FUN_18003a650: odd depth joins the group, even depth appends
   a new top-level group) */
void ex_emit(ExGrp **gr, int *ngr, const int *par, int nch, int i) {
    ExGrp g = { malloc(sizeof(int) * nch), 0 };
    g.ix[g.n++] = i;
    for (int j = 0; j < nch; j++) if (par[j] == i) g.ix[g.n++] = j;
    *gr = realloc(*gr, sizeof(ExGrp) * (*ngr + 1)); (*gr)[(*ngr)++] = g;
    for (int j = 0; j < nch; j++) if (par[j] == i)
        for (int k = 0; k < nch; k++) if (par[k] == j) ex_emit(gr, ngr, par, nch, k);
}
/* sec. 4.5 grouping (ps_sort_multiplane_chains_for_sheets by plane, then FUN_180039370, decomp/extrude/b7.c).
   Non-thin: per plane the closed chains are nested by containment; every top-level loop starts a group and takes its
   direct children (holes), a loop at even depth (an island) starts a new group - so the groups of one plane union to
   the even-odd region of all its loops, and Create makes one body per group. OURS (sort_pock_bounds' algorithm is
   NOT DECODED, cam_engines_code.md): the containment test (a loop's container = the smallest-area loop whose winding
   contains the loop's boundary-farthest vertex; a vertex nearer than 0.025 mm / 0.001 in - the decoded tolerance
   value - is treated as undecidable = not contained) and the group emission order (roots in chain order, islands
   after their grandparent's subtree). Also ours: a degenerate loop (< 3 points) is dropped silently (the original
   fails the whole grouping when chain_to_pock_bound fails), and the hole-children dedup FUN_1800325c0 is not done
   (unexamined). Open chains are not converted in the original (they are reported, then a non-thin extrude fails with
   sld 680); here they share one group per plane (a non-thin one errors 680 in group_tool).
   Thin: the wire grouping (ps_sort_chains_for_wire) is NOT DECODED - a plane is one group. */
int ex_grouping(const ExFeat *f, ExGrp **out, int *ng, ExErr *e) {
    e->id = 0; e->caption = -1; *out = NULL; *ng = 0;
    int nch = f->nch, ngr = 0, ok = 0;
    double tol = 0.002 + ext * 2e-6;                       /* ours: chord tolerance of the polylines */
    double stol = mcam_is_metric(&F) ? 0.025 : 0.001;                /* sort_pock_bounds tolerance (IsMetric, FUN_180039370) */
    double **py = calloc(nch ? nch : 1, sizeof(double *));
    int *np = calloc(nch ? nch : 1, sizeof(int));
    int *pl = calloc(nch ? nch : 1, sizeof(int));          /* plane index per chain */
    double *pz = calloc(nch ? nch : 1, sizeof(double));    /* plane Z, first-seen order */
    int npl = 0;
    ExGrp *gr = NULL;
    for (int i = 0; i < nch; i++) {
        const Chain *c = &f->ch[i]; double z;
        for (int k = 0; k < c->n; k++) if (c->ent[k] < 0 || c->ent[k] >= F.nents || !F.ents[c->ent[k]].type) goto done;   /* geometry deleted: the regen of such an op is NOT DECODED (no message) */
        if (!ex_chain_xy(c, &z)) { e->id = 657; e->caption = 201; goto done; }
        int k = 0; while (k < npl && fabs(pz[k] - z) > EX_NCTOL) k++;
        if (k == npl) pz[npl++] = z;
        pl[i] = k;
        if (c->closed) {
            double *xy = NULL, zz; int n = cam_chain_polyline(&F, c, tol, &xy, &zz);
            if (n > 1 && fabs(xy[0] - xy[2 * n - 2]) < 1e-9 && fabs(xy[1] - xy[2 * n - 1]) < 1e-9) n--;
            if (n >= 3) { py[i] = xy; np[i] = n; } else free(xy);   /* a degenerate loop is no boundary */
        }
    }
    for (int p = 0; p < npl; p++) {
        if (f->p.thin) {                                    /* NOT DECODED wire grouping: the plane is one group */
            ExGrp g = { malloc(sizeof(int) * (nch ? nch : 1)), 0 };
            for (int i = 0; i < nch; i++) if (pl[i] == p) g.ix[g.n++] = i;
            if (g.n) { gr = realloc(gr, sizeof(ExGrp) * (ngr + 1)); gr[ngr++] = g; } else free(g.ix);
            continue;
        }
        int *par = calloc(nch ? nch : 1, sizeof(int));      /* -2 = not a loop of this plane, -1 = top level, else the parent */
        for (int i = 0; i < nch; i++) par[i] = pl[i] == p && np[i] ? -1 : -2;
        for (int i = 0; i < nch; i++) {
            if (par[i] != -1) continue;
            double best = 1e300;
            for (int j = 0; j < nch; j++) {
                if (j == i || par[j] < -1) continue;
                /* the vertex of i farthest from j's boundary decides; one nearer than stol is undecidable (touching) */
                double dm = -1; int iv = -1;
                for (int k = 0; k < np[i]; k++) {
                    double d = ex_pdist(py[j], np[j], py[i][2 * k], py[i][2 * k + 1]);
                    if (d > dm) { dm = d; iv = k; }
                }
                if (iv < 0 || dm < stol || !ex_wind(py[j], np[j], py[i][2 * iv], py[i][2 * iv + 1])) continue;
                CPath q = { py[j], np[j] }; double a = fabs(cl_area(&q));
                if (a < best) { best = a; par[i] = j; }
            }
        }
        for (int i = 0; i < nch; i++) if (par[i] == -1) ex_emit(&gr, &ngr, par, nch, i);
        ExGrp g = { malloc(sizeof(int) * (nch ? nch : 1)), 0 };     /* the plane's open chains share one group */
        for (int i = 0; i < nch; i++) if (pl[i] == p && !f->ch[i].closed) g.ix[g.n++] = i;
        if (g.n) { gr = realloc(gr, sizeof(ExGrp) * (ngr + 1)); gr[ngr++] = g; } else free(g.ix);
        free(par);
    }
    *out = gr; *ng = ngr; ok = 1;
done:
    for (int i = 0; i < nch; i++) free(py[i]);
    free(py); free(np); free(pl); free(pz);
    if (!ok) { for (int i = 0; i < ngr; i++) free(gr[i].ix); free(gr); }
    return ok;
}

/* the chains as groups (sec. 4.5, ex_grouping), one Profile per group. The group's direction is its first chain's
   (sec. 4.1: the FUN_1800d38f0 loop reads the chain data of the group's first chain). */
int feat_groups(const ExFeat *f, Profile **out, int *ng, ExErr *e) {
    e->id = 0; e->caption = -1; *out = NULL; *ng = 0;
    ExGrp *gr = NULL; int ngr = 0;
    if (!ex_grouping(f, &gr, &ngr, e)) return 0;
    Profile *g = calloc(ngr ? ngr : 1, sizeof(Profile));
    double tol = 0.002 + ext * 2e-6;                      /* ours: chord tolerance of the polylines */
    for (int k = 0; k < ngr; k++) {
        g[k].closed.p = calloc(gr[k].n, sizeof(CPath)); g[k].open.p = calloc(gr[k].n, sizeof(CPath));
        for (int m = 0; m < gr[k].n; m++) {
            int i = gr[k].ix[m]; const Chain *c = &f->ch[i]; double z;
            ex_chain_xy(c, &z);                           /* planarity was validated by ex_grouping */
            double *xy = NULL, pz; int np = cam_chain_polyline(&F, c, tol, &xy, &pz);
            if (np < 2) { free(xy); continue; }
            /* the chain's arcs (formats/extrude.md "Round edges"): counter-clockwise start / sweep, the chord
               tolerance they were flattened with (cam_chain_polyline keeps every facet within tol of the arc) */
            for (int q = 0; q < c->n; q++) {
                const Ent *en = &F.ents[c->ent[q]]; if (en->type != E_ARC || en->r <= 0) continue;
                BArc a = { en->c.x, en->c.y, en->r, en->sw < 0 ? en->a0 + en->sw : en->a0, fabs(en->sw), tol, en->sw < 0 };
                if (a.sw > 2 * M_PI) a.sw = 2 * M_PI;
                g[k].arc = realloc(g[k].arc, sizeof(BArc) * (g[k].narc + 1)); g[k].arc[g[k].narc++] = a;
            }
            if (!g[k].closed.n && !g[k].open.n) { g[k].z = z; g[k].dir = f->cdir && f->cdir[i] < 0 ? -1 : 1; g[k].first = i; }   /* the group's first usable chain (sec. 4.1) */
            if (c->closed) { if (np > 2 && fabs(xy[0] - xy[2 * np - 2]) < 1e-9 && fabs(xy[1] - xy[2 * np - 1]) < 1e-9) np--;
                             g[k].closed.p[g[k].closed.n].xy = xy; g[k].closed.p[g[k].closed.n++].n = np; }
            else {
                /* thin-wall side of an open chain (mcgeomsld ps_sweep_wire + FUN_180097650, decomp/mcgeomsld_draft_sweep.c):
                   the sheet face normal is compared with v = d x t (d = the chain's extrusion vector, t = the FIRST entity's
                   own tangent at its mid parameter, not the chain's direction); Direction 1 is thickened to the side away from
                   v (PK_BODY_thicken_3 front = along the face normal). open_band() puts Direction 1 on the left of the path, so
                   the path is reversed when d x t points left: cd * (first entity traversed reversed ? -1 : 1) > 0. */
                int cd = f->cdir && f->cdir[i] < 0 ? -1 : 1, r = c->rev && c->rev[0] ? -1 : 1;
                if (cd * r > 0) for (int a = 0, b = np - 1; a < b; a++, b--) { double tx = xy[2 * a], ty = xy[2 * a + 1]; xy[2 * a] = xy[2 * b]; xy[2 * a + 1] = xy[2 * b + 1]; xy[2 * b] = tx; xy[2 * b + 1] = ty; }
                g[k].open.p[g[k].open.n].xy = xy; g[k].open.p[g[k].open.n++].n = np;
            }
        }
    }
    for (int k = 0; k < ngr; k++) free(gr[k].ix);
    free(gr);
    *out = g; *ng = ngr; return 1;
}
void groups_free(Profile *g, int n) { for (int k = 0; k < n; k++) { cl_free(&g[k].closed); cl_free(&g[k].open); free(g[k].arc); } free(g); }

/* the body of a primitive (ours: this program's 2.5D kernel builds it as an extrusion of its base outline along +Z; the
   Parasolid placement of sld_basic_primitive's block / CRCYSO cylinder is NOT DECODED beyond decomp/mcgeomsld_basic_primitive.c
   / _basic_cyl.c: block = the zmin face centre + sizes along the view X / Y / Z, cylinder = the low-end centre, axis, r, h).
   Only a Z-axis cylinder can be made here (the kernel extrudes along Z). Returns 0 when the shape cannot be built. */
int prim_body(int prim, const double *pv, const char *name, Body *out) {
    memset(out, 0, sizeof *out);
    Profile g; memset(&g, 0, sizeof g);
    CPath cp = { NULL, 0 }; double tol = 0.002 + ext * 2e-6, z0, dist; BArc arc;
    if (prim == 15) {
        double x0 = fmin(pv[0], pv[1]), x1 = fmax(pv[0], pv[1]), y0 = fmin(pv[2], pv[3]), y1 = fmax(pv[2], pv[3]);
        z0 = fmin(pv[4], pv[5]); dist = fabs(pv[5] - pv[4]);
        if (x1 - x0 <= EX_TOL || y1 - y0 <= EX_TOL || dist <= EX_TOL) return 0;
        cp.xy = malloc(8 * sizeof(double)); if (!cp.xy) return 0;
        double q[8] = { x0, y0, x1, y0, x1, y1, x0, y1 }; memcpy(cp.xy, q, sizeof q); cp.n = 4;   /* counter-clockwise */
    } else if (prim == 17) {
        double r = pv[0], len = pv[1];
        if (!(r >= EX_TOL) || !(len > EX_TOL) || fabs(fabs(pv[7]) - 1) > 1e-9) return 0;   /* r, len, axis along Z */
        z0 = pv[7] > 0 ? pv[4] : pv[4] - len; dist = len;
        double step = r > tol ? 2 * acos(1 - tol / r) : M_PI / 8; if (step > M_PI / 18) step = M_PI / 18;
        int n = (int)ceil(2 * M_PI / step); if (n < 8) n = 8; if (n > 4096) n = 4096;
        cp.xy = malloc(2 * sizeof(double) * n); if (!cp.xy) return 0;
        for (int i = 0; i < n; i++) { cp.xy[2 * i] = pv[2] + r * cos(2 * M_PI * i / n); cp.xy[2 * i + 1] = pv[3] + r * sin(2 * M_PI * i / n); }
        cp.n = n;
        arc = (BArc){ pv[2], pv[3], r, 0, 2 * M_PI, tol, 0 }; g.arc = &arc; g.narc = 1;
    } else return 0;
    g.closed.p = &cp; g.closed.n = 1; g.z = z0; g.dir = 1;
    ExtrudeParams ep; memset(&ep, 0, sizeof ep);
    ep.type = EX_CREATE; ep.target = -1; ep.method = EXM_DISTANCE; ep.dist = dist; ep.vec[2] = 1; ep.split = 1; ep.draft_ang = 5;
    Body *bs = NULL; int nb = 0; ExErr e;
    int ok = solid_extrude(&bs, &nb, &g, 1, &ep, name, EX_TOL, mcam_is_metric(&F), &e) && nb == 1;
    free(cp.xy);
    if (ok) *out = bs[0]; else for (int i = 0; i < nb; i++) body_free(&bs[i]);
    free(bs);
    return ok;
}
int feat_apply(Body **bs, int *nb, ExFeat *f, ExErr *e) {
    if (f->prim) {                                       /* a Block / Cylinder: a new body */
        Body b; e->id = 0; e->caption = -1;
        if (!prim_body(f->prim, f->pv, f->name, &b)) return 0;
        Body *nbs = realloc(*bs, sizeof(Body) * (*nb + 1)); if (!nbs) { body_free(&b); return 0; }
        *bs = nbs; (*bs)[(*nb)++] = b; return 1;
    }
    if (f->p.legacy) {
        /* a project line written before this file followed formats/extrude.md: its direction came from an old rule
           of this program (Cut toward the target's middle, Boss away from it, Create +Z, then Reverse). Converted
           once into the per-chain directions, so old projects rebuild unchanged; new features never use it. */
        int up = 1; double zc = 0;
        if (f->p.type != EX_CREATE && f->p.target >= 0 && f->p.target < *nb && f->nch && ex_chain_xy(&f->ch[0], &zc)) {
            const Body *t = &(*bs)[f->p.target]; double zm = t->n ? (t->s[0].z0 + t->s[t->n - 1].z1) / 2 : zc;
            up = f->p.type == EX_CUT ? (zm >= zc ? 1 : -1) : (zm <= zc ? 1 : -1);
        }
        for (int i = 0; i < f->nch; i++) f->cdir[i] = up;
        f->p.legacy = 0;
    }
    Profile *g; int ng;
    if (!feat_groups(f, &g, &ng, e)) return 0;
    int ok = solid_extrude(bs, nb, g, ng, &f->p, f->name, EX_TOL, mcam_is_metric(&F), e);
    groups_free(g, ng);
    return ok;
}
void bodies_free(Body **bs, int *nb) { for (int i = 0; i < *nb; i++) body_free(&(*bs)[i]); free(*bs); *bs = NULL; *nb = 0; }
Body *bodies_dup(int *nt) {
    Body *t = malloc(sizeof(Body) * (nsbodies ? nsbodies : 1));
    for (int i = 0; i < nsbodies; i++) t[i] = body_copy(&sbodies[i]);
    *nt = nsbodies; return t;
}
void bodies_show(const Body *bs, int nb) {
    float *all = NULL; int nt = 0;
    for (int i = 0; i < nb; i++) { float *t; int k = body_mesh(&bs[i], &t);
        all = realloc(all, sizeof(float) * 18 * (nt + k + 1)); memcpy(all + 18 * nt, t, sizeof(float) * 18 * k); nt += k; free(t); }
    scene_set_user_solids(all, nt); free(all);
    float *eg = NULL; int ne = 0;                        /* the edges (sld_draw_solid_wire): solid25.c body_edges */
    for (int i = 0; i < nb; i++) { float *s; int k = body_edges(&bs[i], &s);
        if (k > 0) { eg = realloc(eg, sizeof(float) * 6 * (ne + k)); memcpy(eg + 6 * ne, s, sizeof(float) * 6 * k); ne += k; } free(s); }
    scene_set_user_edges(eg, ne); free(eg);
    ac_solids_set(bs, nb);                              /* the AutoCursor's solid passes (cui_snap.c) */
    scene_reupload();
}
/* the bodies + one extra body (the Bounding Box panel's live solid, cui_bbox.c) drawn; the AutoCursor keeps the bodies only.
   Allocation failures leave the drawing without the failed part (ours) */
void bodies_show_with(const Body *bs, int nb, const Body *extra) {
    if (!extra) { bodies_show(bs, nb); return; }
    float *all = NULL; size_t nt = 0; int okm = 1;
    for (int i = 0; i <= nb && okm; i++) { float *t = NULL; int k = body_mesh(i < nb ? &bs[i] : extra, &t);
        if (k > 0) { float *na = realloc(all, sizeof(float) * 18 * (nt + k)); if (!na) okm = 0; else { all = na; memcpy(all + 18 * nt, t, sizeof(float) * 18 * k); nt += k; } }
        free(t); }
    scene_set_user_solids(all, (int)nt); free(all);
    float *eg = NULL; size_t ne = 0; okm = 1;
    for (int i = 0; i <= nb && okm; i++) { float *t = NULL; int k = body_edges(i < nb ? &bs[i] : extra, &t);
        if (k > 0) { float *na = realloc(eg, sizeof(float) * 6 * (ne + k)); if (!na) okm = 0; else { eg = na; memcpy(eg + 6 * ne, t, sizeof(float) * 6 * k); ne += k; } }
        free(t); }
    scene_set_user_edges(eg, (int)ne); free(eg);
    ac_solids_set(bs, nb);
    scene_reupload();
}
/* replay the feature list; a feature that fails is skipped (Mastercam's regeneration and its messages are NOT
   DECODED, sec. 1.7, so nothing is reported) */
int feat_body[4096];                              /* the body each feature is listed under in the Solids manager */
void solids_rebuild(void) {
    bodies_free(&sbodies, &nsbodies); ExErr e;
    for (int i = 0; i < nfeats; i++) {
        int nb0 = nsbodies; feat_apply(&sbodies, &nsbodies, &feats[i], &e);
        if (i < 4096) feat_body[i] = nsbodies > nb0 ? nb0 : feats[i].p.target;   /* a new body, else the cut / boss target */
    }
    bodies_show(sbodies, nsbodies);
}
void solids_clear(void) {
    for (int i = 0; i < nfeats; i++) feat_free(&feats[i]);
    free(feats); feats = NULL; nfeats = 0;
    bodies_free(&sbodies, &nsbodies); scene_set_user_solids(NULL, 0); scene_set_user_edges(NULL, 0); ac_solids_set(NULL, 0);
    if (cui_hooks.solids_cleared) cui_hooks.solids_cleared();   /* mcamui: ex_mode = 0 */
}
void feat_push(const ExFeat *f) { feats = realloc(feats, sizeof(ExFeat) * (nfeats + 1)); feats[nfeats++] = feat_dup(f); }
void feat_pop(void) { if (nfeats) feat_free(&feats[--nfeats]); }
/* undo record (FeatPack): idx 0 = the features were added, 1 = they were removed (always the last ones),
   2 = the feature at position nd was replaced (edit; the pack holds the previous one - undo and redo swap) */
void feat_undo(URec *r, int undo) {
    if (r->idx == 2) {
        FeatPack *k = r->feat;
        if (k && k->n == 1 && r->nd >= 0 && r->nd < nfeats) { ExFeat t = feats[r->nd]; feats[r->nd] = k->f[0]; k->f[0] = t; }
        solids_rebuild(); return;
    }
    int add = (r->idx == 0) != undo; FeatPack *k = r->feat;
    for (int i = 0; i < k->n; i++) { if (add) feat_push(&k->f[i]); else feat_pop(); }
    solids_rebuild();
}

/* project lines: EXTRUDE <params> <nchains> ..., then XCHAIN <closed> <n> <ent rev>... <direction> per chain.
   The first 12 EXTRUDE fields keep their old meaning (type dist both reverse draft-angle-or-0 !draft-reverse thin
   thick thin_dir target nchains thick2); appended: method split draft draft_angle draft_reverse draft_ends trim
   single, then the name (rest of the line). XCHAIN appends the chain's direction (+1 / -1). */
void feats_write(FILE *f) {
    for (int i = 0; i < nfeats; i++) {
        if (feats[i].prim) {                             /* PRIM type v0..v7 name (ours: a Bounding Box Block / Cylinder) */
            fprintf(f, "PRIM %d", feats[i].prim);
            for (int k = 0; k < 8; k++) fprintf(f, " %.17g", feats[i].pv[k]);
            fprintf(f, " %s\n", feats[i].name);
            continue;
        }
        const ExtrudeParams *p = &feats[i].p;
        fprintf(f, "EXTRUDE %d %.17g %d %d %.17g %d %d %.17g %d %d %d %.17g %d %d %d %.17g %d %d %d %d %s\n", p->type, p->dist, p->both, p->reverse, p->draft ? p->draft_ang : 0.0,
                !p->draft_rev, p->thin, p->thick, p->thin_dir, p->target, feats[i].nch, p->thick2,
                p->method, p->split, p->draft, p->draft_ang, p->draft_rev, p->draft_ends, p->trim, feats[i].single, feats[i].name);
        for (int c = 0; c < feats[i].nch; c++) {
            fprintf(f, "XCHAIN %d %d", feats[i].ch[c].closed, feats[i].ch[c].n);
            for (int k = 0; k < feats[i].ch[c].n; k++) fprintf(f, " %d %d", feats[i].ch[c].ent[k], feats[i].ch[c].rev[k]);
            fprintf(f, " %d\n", feats[i].cdir[c] < 0 ? -1 : 1);
        }
    }
}
void feats_read(FILE *f) {
    char line[1 << 16]; ExFeat cur; int want = 0;
    memset(&cur, 0, sizeof cur);
    rewind(f);
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "PRIM ", 5)) {
            if (want == 0 && cur.nch) { feat_push(&cur); feat_free(&cur); }
            ExFeat pf; memset(&pf, 0, sizeof pf); int pos = 0;
            if (sscanf(line + 5, "%d %lf %lf %lf %lf %lf %lf %lf %lf%n", &pf.prim, &pf.pv[0], &pf.pv[1], &pf.pv[2], &pf.pv[3], &pf.pv[4], &pf.pv[5], &pf.pv[6], &pf.pv[7], &pos) == 9
                && (pf.prim == 15 || pf.prim == 17)) {
                char *nm = line + 5 + pos; if (*nm == ' ') nm++;
                nm[strcspn(nm, "\r\n")] = 0; snprintf(pf.name, sizeof pf.name, "%s", nm);
                feat_push(&pf);
            }
            want = 0; feat_free(&cur); memset(&cur, 0, sizeof cur);
        } else if (!strncmp(line, "EXTRUDE ", 8)) {
            if (want == 0 && cur.nch) { feat_push(&cur); feat_free(&cur); }
            memset(&cur, 0, sizeof cur); ExtrudeParams *p = &cur.p;
            int dir = 0, dout = 1, pos = 0; double dang = 0;
            int nf = sscanf(line + 8, "%d %lf %d %d %lf %d %d %lf %d %d %d %lf %d %d %d %lf %d %d %d %d%n", &p->type, &p->dist, &dir, &p->reverse, &dang, &dout, &p->thin, &p->thick, &p->thin_dir,
                            &p->target, &want, &p->thick2, &p->method, &p->split, &p->draft, &p->draft_ang, &p->draft_rev, &p->draft_ends, &p->trim, &cur.single, &pos);
            if (nf < 11 || want < 0 || want > 10000) want = 0;
            if (nf >= 20) {                                   /* this format */
                p->both = dir != 0;
                char *nm = line + 8 + pos; if (*nm == ' ') nm++;
                nm[strcspn(nm, "\r\n")] = 0; snprintf(cur.name, sizeof cur.name, "%s", nm);
            } else {                                          /* older lines: converted to the same geometry */
                if (nf == 11) { if (p->thin_dir == THIN_BOTH) p->thick /= 2; p->thick2 = p->thick; }   /* one thickness, Both = half each side */
                p->both = dir != 0; if (dir == 2) p->dist /= 2;  /* 2 = symmetric (Distance in total) = Both with half of it */
                p->method = EXM_DISTANCE; p->trim = 0;
                p->draft = dang > 0; p->draft_ang = dang > 0 ? dang : 5; p->draft_rev = !dout;
                p->split = 1; p->draft_ends = 1;                  /* the old draft tapered both halves from the chain plane and offset whole walls */
                p->legacy = 1; cur.single = 1;
                snprintf(cur.name, sizeof cur.name, "%s", ex_defname(p->type));
            }
            if (p->target < 0 && p->type != EX_CREATE) p->type = EX_CREATE;
            cur.ch = calloc(want ? want : 1, sizeof(Chain)); cur.cdir = calloc(want ? want : 1, sizeof(int));
        } else if (!strncmp(line, "XCHAIN ", 7) && want > 0) {
            char *q = line + 7; Chain c; memset(&c, 0, sizeof c);
            c.closed = (int)strtol(q, &q, 10); c.n = (int)strtol(q, &q, 10);
            if (c.n <= 0 || c.n > 100000) { want = 0; continue; }
            c.ent = malloc(sizeof(int) * c.n); c.rev = malloc(sizeof(int) * c.n);
            for (int k = 0; k < c.n; k++) { c.ent[k] = cam_load_ent_remap((int)strtol(q, &q, 10)); c.rev[k] = (int)strtol(q, &q, 10); if (c.ent[k] < 0 || c.ent[k] >= F.nents) c.ent[k] = 0; }
            char *q2; long d = strtol(q, &q2, 10);
            cur.cdir[cur.nch] = q2 != q && d < 0 ? -1 : 1;
            cur.ch[cur.nch++] = c;
            if (--want == 0) { feat_push(&cur); feat_free(&cur); }
        }
    }
    feat_free(&cur);
}
