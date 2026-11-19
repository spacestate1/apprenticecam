/* cui_feat.c - the Extrude feature list the document stores (EXTRUDE / XCHAIN project lines) and the bodies it
   rebuilds (formats/extrude.md; the command itself, ex_*, is still in the front end). Moved from viewer/mcamui.c. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "occt_solid_ent.h"

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
    r.fl = f->fl ? filfeat_dup(f->fl) : NULL;
    return r;
}
void feat_free(ExFeat *f) { for (int i = 0; i < f->nch; i++) cam_chain_free(&f->ch[i]); free(f->ch); free(f->cdir); f->ch = NULL; f->cdir = NULL; f->nch = 0;
                            filfeat_free(f->fl); f->fl = NULL; }
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

/* SOL-3 (2026-11-18): an OpenCASCADE body as a Body (solid25.h): its XT is written at once (viewer/occt/occt_xt.cpp, Parasolid 27; the
   AutoCursor / solid chaining read the bodies' edges and faces from it as from the part's solids) */
double feat_xt_scale(void) { return mcam_is_metric(&F) ? 1000.0 : 1000.0 / 25.4; }
Body body_of_occ(OcsBody *ob, const char *name) {
    Body b; memset(&b, 0, sizeof b);
    b.occ = ob; snprintf(b.name, sizeof b.name, "%s", name ? name : "");
    b.xt_scale = feat_xt_scale();
    char err[256] = "";
    b.xt = ocs_xt(ob, b.xt_scale, 27, "sld_1", &b.xt_n, err, sizeof err);
    if (!b.xt) ex_log("body %s: no XT (%s): its edges / faces are not offered to the AutoCursor", b.name, err);
    return b;
}
/* the body of a primitive: sld_basic_primitive's block (the zmin face centre + sizes along the view X / Y / Z) / CRCYSO cylinder (the low-end
   centre, axis, r, h) (decomp/mcgeomsld_basic_primitive.c / _basic_cyl.c), now on OpenCASCADE (BRepPrimAPI). Its op record (SldPrimitiveOp)
   is NOT DECODED, so such a solid is not written to a Mastercam part. Returns 0 when the shape cannot be built. */
int prim_body(int prim, const double *pv, const char *name, Body *out) {
    memset(out, 0, sizeof *out);
    if (prim == 15) {
        if (fabs(pv[1] - pv[0]) <= EX_TOL || fabs(pv[3] - pv[2]) <= EX_TOL || fabs(pv[5] - pv[4]) <= EX_TOL) return 0;
    } else if (prim == 17) {
        if (!(pv[0] >= EX_TOL) || !(pv[1] > EX_TOL)) return 0;
    } else return 0;
    char err[200]; OcsBody *ob = ocs_primitive(prim, pv, err, sizeof err);
    if (!ob) return 0;
    *out = body_of_occ(ob, name);
    return 1;
}
/* the feature's chain groups (sec. 4.5, ex_grouping) as the OpenCASCADE kernel's input: per group its chains (the root loop first, then its
   holes; thin: the plane's chains), each chain's line / arc entities exact (occt_solid_ent.h, ps_arc_to_ps_arc), and the group's direction =
   its first chain's (sec. 4.1: +-Z here, the chains lie in XY planes) */
void ocsin_free(OcsIn *in) { free(in->g); free(in->ch); free(in->seg); free(in->first); memset(in, 0, sizeof *in); }
int feat_ocs_groups(const ExFeat *f, OcsIn *in, ExErr *e) {
    memset(in, 0, sizeof *in);
    ExGrp *gr = NULL; int ngr = 0;
    if (!ex_grouping(f, &gr, &ngr, e)) return 0;
    int nc = 0, ns = 0;
    for (int k = 0; k < ngr; k++) for (int m = 0; m < gr[k].n; m++) { nc++; ns += f->ch[gr[k].ix[m]].n; }
    in->g = calloc(ngr ? ngr : 1, sizeof(OcsGroup)); in->ch = calloc(nc ? nc : 1, sizeof(OcsChain)); in->seg = calloc(ns ? ns : 1, sizeof(OcsSeg));
    in->first = calloc(ngr ? ngr : 1, sizeof(int));
    int ic = 0, is = 0, ok = 1;
    for (int k = 0; k < ngr && ok; k++) {
        OcsGroup *g = &in->g[in->ng]; g->ch = &in->ch[ic]; g->nch = 0;
        for (int m = 0; m < gr[k].n && ok; m++) {
            int i = gr[k].ix[m]; const Chain *c = &f->ch[i];
            OcsChain *oc = &in->ch[ic++]; oc->s = &in->seg[is]; oc->n = 0; oc->closed = c->closed;
            for (int q = 0; q < c->n; q++) {
                if (!ocs_seg_of_ent(&F.ents[c->ent[q]], c->rev[q], c->ent[q], &in->seg[is])) { ok = 0; break; }   /* a point / spline: no sheet */
                is++; oc->n++;
            }
            if (!g->nch) { g->dir[0] = 0; g->dir[1] = 0; g->dir[2] = f->cdir && f->cdir[i] < 0 ? -1 : 1; in->first[in->ng] = i; }
            g->nch++;
        }
        in->ng++;
    }
    for (int k = 0; k < ngr; k++) free(gr[k].ix);
    free(gr);
    if (!ok) { ocsin_free(in); e->id = 337; e->caption = 201; return 0; }   /* "No valid closed chains selected for non-thinwall extrude." */
    return 1;
}
void ocs_params_of(const ExtrudeParams *ep, int single, OcsParams *p) {
    memset(p, 0, sizeof *p);
    p->type = ep->type; p->method = ep->method; p->reverse = ep->reverse; p->both = ep->both; p->split = ep->split; p->draft = ep->draft;
    p->draft_rev = ep->draft_rev; p->thin = ep->thin; p->thin_dir = ep->thin_dir; p->draft_ends = ep->draft_ends; p->dist = ep->dist;
    p->draft_ang = ep->draft_ang * M_PI / 180; p->thick = ep->thick; p->thick2 = ep->thick2; p->single = single;
}
int feat_apply(Body **bs, int *nb, ExFeat *f, ExErr *e) {
    e->id = 0; e->caption = -1;
    if (f->fl) return fil_apply(bs, nb, f, e);           /* a Constant Fillet op (cui_fillet.c) */
    if (f->prim) {                                       /* a Block / Cylinder: a new body */
        Body b;
        if (!prim_body(f->prim, f->pv, f->name, &b)) return 0;
        Body *nbs = realloc(*bs, sizeof(Body) * (*nb + 1)); if (!nbs) { body_free(&b); return 0; }
        *bs = nbs; (*bs)[(*nb)++] = b; return 1;
    }
    if (f->p.legacy) {
        /* a project line written before this file followed formats/extrude.md: its direction came from an old rule
           of this program (Cut toward the target's middle, Boss away from it, Create +Z, then Reverse). Converted
           once into the per-chain directions, so old projects rebuild unchanged; new features never use it. */
        int up = 1; double zc = 0, lo[3], hi[3];
        if (f->p.type != EX_CREATE && f->p.target >= 0 && f->p.target < *nb && f->nch && ex_chain_xy(&f->ch[0], &zc)) {
            const Body *t = &(*bs)[f->p.target]; double zm = t->occ && ocs_bbox(t->occ, lo, hi) ? (lo[2] + hi[2]) / 2 : zc;
            up = f->p.type == EX_CUT ? (zm >= zc ? 1 : -1) : (zm <= zc ? 1 : -1);
        }
        for (int i = 0; i < f->nch; i++) f->cdir[i] = up;
        f->p.legacy = 0;
    }
    /* SOL-3: sld_extrude on OpenCASCADE (viewer/occt_solid.cpp) */
    OcsIn in;
    if (!feat_ocs_groups(f, &in, e)) return 0;
    OcsParams p; ocs_params_of(&f->p, f->single, &p);
    const OcsBody *tgt = NULL;
    if (f->p.type != EX_CREATE) {
        if (f->p.target < 0 || f->p.target >= *nb || !(*bs)[f->p.target].occ) { ocsin_free(&in); return 0; }   /* the panel forces Create without a target (2.1) */
        tgt = (*bs)[f->p.target].occ;
    }
    OcsBody **out = NULL; int nout = 0; char err[300] = "";
    int ok = ocs_extrude(tgt, in.g, in.ng, &p, EX_TOL, mcam_is_metric(&F), &out, &nout, &e->id, &e->caption, err, sizeof err);
    ocsin_free(&in);
    if (!ok) { if (err[0]) ex_log("kernel: %s", err); return 0; }
    /* the new ops' feature (the save writes each op from its feature: sld_save.c) */
    int tag = f >= feats && f < feats + nfeats ? (int)(f - feats) : -1, op0 = tgt ? ocs_nops(tgt) : 0;
    for (int i = 0; i < nout; i++) for (int k = op0; k < ocs_nops(out[i]); k++) ocs_op_set_tag(out[i], k, tag);
    if (f->p.type == EX_CREATE) {
        /* 4.7: each group becomes its own new body; the name is the operation's (panel Name). How Mastercam names bodies (FUN_18012fa00) is
           NOT DECODED. */
        Body *nbs = realloc(*bs, sizeof(Body) * (*nb + nout)); if (!nbs) { for (int i = 0; i < nout; i++) ocs_unref(out[i]); free(out); return 0; }
        *bs = nbs;
        for (int i = 0; i < nout; i++) (*bs)[(*nb)++] = body_of_occ(out[i], f->name);
    } else {
        Body *t = &(*bs)[f->p.target]; char nm[64]; snprintf(nm, sizeof nm, "%s", t->name);
        body_free(t); *t = body_of_occ(out[0], nm);
    }
    free(out);
    return 1;
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
            if (feats[i].level || feats[i].color) fprintf(f, "XATTR %d %d\n", feats[i].level, feats[i].color);
            continue;
        }
        if (feats[i].fl) {                               /* ours: FILLET body whole radius overflow mitered propagate nf ne name, then FREC / EREC lines */
            const FilFeat *q = feats[i].fl;
            fprintf(f, "FILLET %d %d %.17g %d %d %d %d %d %s\n", feats[i].p.target, q->whole, q->radius, q->overflow, q->mitered, q->propagate, q->nf, q->ne, feats[i].name);
            for (int k = 0; k < q->nf; k++) fprintf(f, "FREC %d %.17g %.17g %.17g\n", q->frec[k], q->fpt[k][0], q->fpt[k][1], q->fpt[k][2]);
            for (int k = 0; k < q->ne; k++) fprintf(f, "EREC %d %.17g %.17g %.17g\n", q->erec[k], q->ept[k][0], q->ept[k][1], q->ept[k][2]);
            if (feats[i].level || feats[i].color) fprintf(f, "XATTR %d %d\n", feats[i].level, feats[i].color);
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
        /* ours (SOL-5d): the attributes the new body got at creation, after the feature's chains */
        if (feats[i].level || feats[i].color) fprintf(f, "XATTR %d %d\n", feats[i].level, feats[i].color);
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
        } else if (!strncmp(line, "FILLET ", 7)) {       /* a Constant Fillet feature (feats_write) */
            if (want == 0 && cur.nch) { feat_push(&cur); feat_free(&cur); }
            want = 0; feat_free(&cur); memset(&cur, 0, sizeof cur);
            ExFeat ff; memset(&ff, 0, sizeof ff); FilFeat *q = calloc(1, sizeof *q); int pos = 0, nfr = 0, ner = 0;
            if (sscanf(line + 7, "%d %d %lf %d %d %d %d %d%n", &ff.p.target, &q->whole, &q->radius, &q->overflow, &q->mitered, &q->propagate, &nfr, &ner, &pos) == 8
                && nfr >= 0 && ner >= 0 && nfr < 100000 && ner < 100000) {
                char *nm = line + 7 + pos; if (*nm == ' ') nm++;
                nm[strcspn(nm, "\r\n")] = 0; snprintf(ff.name, sizeof ff.name, "%s", nm);
                q->frec = calloc(nfr + 1, sizeof(int)); q->erec = calloc(ner + 1, sizeof(int));
                q->fpt = calloc(nfr + 1, sizeof *q->fpt); q->ept = calloc(ner + 1, sizeof *q->ept);
                while ((q->nf < nfr || q->ne < ner) && fgets(line, sizeof line, f)) {
                    if (!strncmp(line, "FREC ", 5) && q->nf < nfr) { sscanf(line + 5, "%d %lf %lf %lf", &q->frec[q->nf], &q->fpt[q->nf][0], &q->fpt[q->nf][1], &q->fpt[q->nf][2]); q->nf++; }
                    else if (!strncmp(line, "EREC ", 5) && q->ne < ner) { sscanf(line + 5, "%d %lf %lf %lf", &q->erec[q->ne], &q->ept[q->ne][0], &q->ept[q->ne][1], &q->ept[q->ne][2]); q->ne++; }
                    else break;
                }
                ff.p.type = EX_CUT; ff.fl = q; feat_push(&ff);
            }
            filfeat_free(q);
        } else if (!strncmp(line, "XATTR ", 6) && nfeats > 0) {   /* the last complete feature's level / colour (SOL-5d) */
            sscanf(line + 6, "%d %d", &feats[nfeats - 1].level, &feats[nfeats - 1].color);
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
