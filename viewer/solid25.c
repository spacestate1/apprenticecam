/* solid25 - see solid25.h */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "solid25.h"
#include "occt_solid.h"
#include "xt_mesh.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static CPaths cp_empty(void) { CPaths r; r.n = 0; r.p = calloc(1, sizeof(CPath)); return r; }
static CPaths cp_copy(const CPaths *a) {
    CPaths r; r.n = a->n; r.p = calloc(a->n ? a->n : 1, sizeof(CPath));
    for (int i = 0; i < a->n; i++) { r.p[i].n = a->p[i].n; r.p[i].xy = malloc(sizeof(double) * 2 * (a->p[i].n ? a->p[i].n : 1)); memcpy(r.p[i].xy, a->p[i].xy, sizeof(double) * 2 * a->p[i].n); }
    return r;
}
static int cp_equal(const CPaths *a, const CPaths *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) { if (a->p[i].n != b->p[i].n || memcmp(a->p[i].xy, b->p[i].xy, sizeof(double) * 2 * a->p[i].n)) return 0; }
    return 1;
}
static double cp_area(const CPaths *a) { return a->n ? cl_total_area(a) : 0; }

void body_free(Body *b) { for (int i = 0; i < b->n; i++) cl_free(&b->s[i].reg); free(b->s); b->s = NULL; b->n = 0; free(b->arc); b->arc = NULL; b->narc = 0;
    if (b->occ) ocs_unref(b->occ); b->occ = NULL; free(b->xt); b->xt = NULL; b->xt_n = 0; }
/* the body's arc carriers (BArc): appended, an identical one is kept once */
static void arcs_add(BArc **arc, int *narc, const BArc *a, int n) {
    for (int i = 0; i < n; i++) {
        int k = 0; while (k < *narc && memcmp(&(*arc)[k], &a[i], sizeof(BArc))) k++;
        if (k < *narc) continue;
        *arc = realloc(*arc, sizeof(BArc) * (*narc + 1)); (*arc)[(*narc)++] = a[i];
    }
}
Body body_copy(const Body *b) {
    Body r = *b; r.s = malloc(sizeof(Slab) * (b->n ? b->n : 1));
    if (b->occ) ocs_ref(b->occ);
    if (b->xt) { r.xt = malloc(b->xt_n); memcpy(r.xt, b->xt, b->xt_n); }
    for (int i = 0; i < b->n; i++) { r.s[i] = b->s[i]; r.s[i].reg = cp_copy(&b->s[i].reg); }
    r.arc = NULL; r.narc = 0; arcs_add(&r.arc, &r.narc, b->arc, b->narc);
    return r;
}
static void body_add(Body *b, double z0, double z1, CPaths reg) {     /* takes reg; merges with an equal slab below */
    if (z1 - z0 < 1e-9 || fabs(cp_area(&reg)) < 1e-12) { cl_free(&reg); return; }
    if (b->n && fabs(b->s[b->n - 1].z1 - z0) < 1e-9 && cp_equal(&b->s[b->n - 1].reg, &reg)) { b->s[b->n - 1].z1 = z1; cl_free(&reg); return; }
    b->s = realloc(b->s, sizeof(Slab) * (b->n + 1));
    b->s[b->n].z0 = z0; b->s[b->n].z1 = z1; b->s[b->n].reg = reg; b->n++;
}
static const CPaths *region_at(const Body *b, double z) {
    for (int i = 0; i < b->n; i++) if (z > b->s[i].z0 && z < b->s[i].z1) return &b->s[i].reg;
    return NULL;
}
static int dcmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

/* A op B per Z interval (0 union, 2 difference) */
static Body body_bool(const Body *a, const Body *b, int op) {
    int nz = 0; double *z = malloc(sizeof(double) * 2 * (a->n + b->n + 1));
    for (int i = 0; i < a->n; i++) { z[nz++] = a->s[i].z0; z[nz++] = a->s[i].z1; }
    for (int i = 0; i < b->n; i++) { z[nz++] = b->s[i].z0; z[nz++] = b->s[i].z1; }
    qsort(z, nz, sizeof(double), dcmp);
    Body r = *a; r.s = NULL; r.n = 0;
    r.arc = NULL; r.narc = 0;                     /* a trimmed arc keeps its circle: both operands' carriers */
    arcs_add(&r.arc, &r.narc, a->arc, a->narc); arcs_add(&r.arc, &r.narc, b->arc, b->narc);
    CPaths none = cp_empty();
    for (int i = 0; i + 1 < nz; i++) {
        if (z[i + 1] - z[i] < 1e-9) continue;
        double m = (z[i] + z[i + 1]) / 2;
        const CPaths *ra = region_at(a, m), *rb = region_at(b, m);
        if (!ra && (op == 2 || !rb)) continue;
        CPaths res = cl_bool(ra ? ra : &none, rb ? rb : &none, op);
        body_add(&r, z[i], z[i + 1], res);
    }
    cl_free(&none); free(z);
    return r;
}

/* ---------------------------------------------------------------------------------------- sld_extrude */
#define EX_NSLAB 24     /* ours: slabs per drafted half (the kernel approximates a draft by slabs) */

/* the band of an open polyline: width a on the left of its travel, b on the right; the ends square to the chain.
   Corners mitred, the mitre limited to 4x the width (ours). Which side is "Direction 1" for an open chain is
   NOT DECODED (formats/extrude.md 4.6): the caller passes Direction 1 as the left side, the side that is inward for
   a closed chain after chain selection made it counter-clockwise (sec. 1.2). */
static CPaths open_band(const CPath *pl, double a, double b) {
    int n = pl->n; const double *p = pl->xy;
    if (n < 2 || a + b <= 1e-12) return cp_empty();
    double *m = malloc(sizeof(double) * 2 * n);
    for (int i = 0; i < n; i++) {
        double e[2][2]; int ne = 0;
        for (int s = -1; s <= 1; s += 2) {
            int j0 = s < 0 ? i - 1 : i, j1 = j0 + 1; if (j0 < 0 || j1 >= n) continue;
            double dx = p[2 * j1] - p[2 * j0], dy = p[2 * j1 + 1] - p[2 * j0 + 1], l = hypot(dx, dy);
            if (l > 1e-12) { e[ne][0] = -dy / l; e[ne][1] = dx / l; ne++; }
        }
        m[2 * i] = m[2 * i + 1] = 0;
        if (ne == 1) { m[2 * i] = e[0][0]; m[2 * i + 1] = e[0][1]; }
        else if (ne == 2) {
            double sx = e[0][0] + e[1][0], sy = e[0][1] + e[1][1], l = hypot(sx, sy);
            if (l < 1e-12) { m[2 * i] = e[0][0]; m[2 * i + 1] = e[0][1]; }
            else { double ux = sx / l, uy = sy / l, c = ux * e[0][0] + uy * e[0][1]; if (c < 0.25) c = 0.25; m[2 * i] = ux / c; m[2 * i + 1] = uy / c; }
        }
    }
    CPaths poly; poly.n = 1; poly.p = calloc(1, sizeof(CPath)); poly.p[0].n = 2 * n; poly.p[0].xy = malloc(sizeof(double) * 4 * n);
    double *q = poly.p[0].xy;
    for (int i = 0; i < n; i++) { q[2 * i] = p[2 * i] + a * m[2 * i]; q[2 * i + 1] = p[2 * i + 1] + a * m[2 * i + 1]; }
    for (int i = 0; i < n; i++) { int k = n - 1 - i; q[2 * (n + i)] = p[2 * k] - b * m[2 * k]; q[2 * (n + i) + 1] = p[2 * k + 1] - b * m[2 * k + 1]; }
    free(m);
    if (cl_area(&poly.p[0]) < 0)
        for (int i = 0, j = 2 * n - 1; i < j; i++, j--) { double tx = q[2 * i], ty = q[2 * i + 1]; q[2 * i] = q[2 * j]; q[2 * i + 1] = q[2 * j + 1]; q[2 * j] = tx; q[2 * j + 1] = ty; }
    CPaths none = cp_empty(), r = cl_bool(&poly, &none, 0);
    cl_free(&none); cl_free(&poly);
    return r;
}

/* the group's cross-section with its side faces moved out by k (k = 0: the swept profile; k != 0: drafted).
   Not thin: the closed chains' region (a root loop minus its holes; islands are their own groups, sec. 4.5). Thin (4.6): Direction 1 thickness used when dir != 1,
   Direction 2 when dir != 0; for closed chains Direction 1 is inward (WPF property ThinWallInwardDistance) and
   Direction 2 outward (ThinWallOutwardDistance). Draft ends (P+0x68) drafts the end faces of open chains too. */
static CPaths group_section(const Profile *pr, const ExtrudeParams *ep, double k, double tol) {
    double t1 = ep->thin_dir != THIN_DIR2 ? ep->thick : 0, t2 = ep->thin_dir != THIN_DIR1 ? ep->thick2 : 0;
    CPaths sec = pr->closed.n ? cl_union_evenodd(&pr->closed) : cp_empty();
    if (ep->thin) {                                       /* closed chains: the wall between the two offsets */
        CPaths band = cp_empty();
        if (sec.n && t1 + t2 > 0) {
            CPaths a = t2 > 0 ? cl_offset_join(&sec, t2, 1, tol) : cp_copy(&sec);
            CPaths b = t1 > 0 ? cl_offset_join(&sec, -t1, 1, tol) : cp_copy(&sec);
            cl_free(&band); band = cl_bool(&a, &b, 2); cl_free(&a); cl_free(&b);
        }
        cl_free(&sec); sec = band;
    }
    if (k != 0 && sec.n) { CPaths d = cl_offset_join(&sec, k, 1, tol); cl_free(&sec); sec = d; }
    if (ep->thin)                                         /* open chains: one wall each */
        for (int i = 0; i < pr->open.n; i++) {
            CPaths o;
            if (ep->draft_ends && k != 0) { CPaths w = open_band(&pr->open.p[i], t1, t2); o = cl_offset_join(&w, k, 1, tol); cl_free(&w); }
            else o = open_band(&pr->open.p[i], t1 + k, t2 + k);
            CPaths u = cl_bool(&sec, &o, 0); cl_free(&sec); cl_free(&o); sec = u;
        }
    return sec;
}

typedef struct { double z0, z1; CPaths reg; } ZS;
static int zs_cmp(const void *a, const void *b) { double x = ((const ZS *)a)->z0, y = ((const ZS *)b)->z0; return x < y ? -1 : x > y; }

/* the swept (and drafted) tool of one group */
static int group_tool(const Profile *pr, const ExtrudeParams *ep, const Body *target, double tol, int metric, Body *tool, ExErr *err) {
    memset(tool, 0, sizeof *tool);
    /* 4.5: a non-thin extrude needs closed chains; an open one gives sld 680 (message_box_ok(0x2a8, -1, -1)), a
       group without a closed area sld 337 + caption 201 */
    if (!ep->thin && pr->open.n) { err->id = 680; err->caption = -1; return 0; }
    CPaths base = group_section(pr, ep, 0, tol);
    if (fabs(cp_area(&base)) < 1e-12) { cl_free(&base); err->id = ep->thin ? 557 : 337; err->caption = 201; return 0; }  /* thin: the sweep failed, sld 557 */
    /* 4.1 / 4.2: direction = the group's first chain's direction, negated by Reverse all; d1 = d2 = Distance, except
       Cut + Through all: from the target's bounding box (extended by 1000e-5 metric / 39.37007874015748e-5 inch) */
    double zc = pr->z, d1 = ep->dist, d2 = ep->dist;
    int s = (pr->dir < 0 ? -1 : 1) * (ep->reverse ? -1 : 1);
    if (ep->type == EX_CUT && ep->method == EXM_THROUGH_ALL && target && target->n) {
        double lo = 1e300, hi = -1e300, mg = metric ? 1000e-5 : 39.37007874015748e-5;
        for (int i = 0; i < target->n; i++) { if (target->s[i].z0 < lo) lo = target->s[i].z0; if (target->s[i].z1 > hi) hi = target->s[i].z1; }
        lo -= mg; hi += mg;
        d1 = pr->dir < 0 ? zc - lo : hi - zc; d2 = pr->dir < 0 ? hi - zc : zc - lo;
        if (ep->reverse) { double t = d1; d1 = d2; d2 = t; }
        if (d1 <= tol) d1 = 1.0;
        if (d2 <= tol) d2 = 1.0;
    }
    if (d1 <= tol || (ep->both && d2 <= tol)) { cl_free(&base); err->id = 315; err->caption = 201; return 0; }   /* ps_sweep_sheet 0x13b */
    /* 4.3 draft: P+0x44 Reverse negates the angle; the second half of Both uses Split ? P+0x44 : !P+0x44.
       Which way a positive angle tapers is NOT DECODED: Reverse off keeps this program's earlier geometry (the
       section grows with the distance from the chain plane). */
    double tn = ep->draft ? tan(ep->draft_ang * M_PI / 180) : 0;
    int nh = ep->both ? 2 : 1, ns = ep->draft && tn != 0 ? EX_NSLAB : 1, nz = 0;
    ZS *zs = malloc(sizeof(ZS) * nh * ns);
    int bad = 0;
    for (int h = 0; h < nh; h++) {
        int sg = h == 0 ? s : -s; double d = h == 0 ? d1 : d2;
        int rev = h == 0 ? ep->draft_rev : (ep->split ? ep->draft_rev : !ep->draft_rev);
        double t = rev ? -tn : tn;
        for (int i = 0; i < ns; i++) {
            double h0 = d * i / ns, h1 = d * (i + 1) / ns, hm = (h0 + h1) / 2;
            ZS *z = &zs[nz++];
            z->z0 = sg > 0 ? zc + h0 : zc - h1; z->z1 = sg > 0 ? zc + h1 : zc - h0;
            z->reg = ns > 1 ? group_section(pr, ep, t * hm, tol) : cp_copy(&base);
            if (fabs(cp_area(&z->reg)) < 1e-12) bad = 1;
        }
    }
    cl_free(&base);
    qsort(zs, nz, sizeof(ZS), zs_cmp);
    for (int i = 0; i < nz; i++) body_add(tool, zs[i].z0, zs[i].z1, zs[i].reg);   /* takes the regions */
    free(zs);
    if (bad || !tool->n) { body_free(tool); err->id = 555; err->caption = 201; return 0; }   /* "Failure drafting faces." (0x22b) */
    /* the round edges' circles (formats/extrude.md "Round edges"): the chains' arcs; a thin wall's faces are the
       arcs offset by the thicknesses (concentric, radius r +- t). group_section offsets with sharp joins
       (cl_offset_join join 1), so an offset facet vertex lies t (1 / cos(step / 2) - 1) off the offset circle
       (step <= pi / 18, the faceting step of cam_chain_polyline): added to the carrier's tolerance. Drafted tools
       (slab approximations of a taper) get no carriers: their outlines stay faceted runs. */
    if (ns == 1) {
        arcs_add(&tool->arc, &tool->narc, pr->arc, pr->narc);
        if (ep->thin) {
            double th[2] = { ep->thin_dir != THIN_DIR2 ? ep->thick : 0, ep->thin_dir != THIN_DIR1 ? ep->thick2 : 0 };
            for (int i = 0; i < pr->narc; i++) for (int k = 0; k < 2; k++) for (int sg = -1; sg <= 1; sg += 2) {
                double t = th[k]; if (t <= 0) continue;
                BArc a = pr->arc[i]; a.r += sg * t; if (a.r <= 0) continue;
                a.tol += t * (1 / cos(M_PI / 36) - 1);
                arcs_add(&tool->arc, &tool->narc, &a, 1);
            }
        }
    }
    return 1;
}

int solid_extrude(Body **bodies, int *nbodies, const Profile *g, int ng, const ExtrudeParams *ep, const char *name, double tol, int metric, ExErr *err) {
    err->id = 0; err->caption = -1;
    const Body *target = NULL;
    if (ep->type != EX_CREATE) {
        if (ep->target < 0 || ep->target >= *nbodies) return 0;    /* the panel forces Create without a target (2.1) */
        target = &(*bodies)[ep->target];
    }
    if (ng < 1) { err->id = 337; err->caption = 201; return 0; }
    Body *tools = calloc(ng, sizeof(Body));
    for (int i = 0; i < ng; i++)
        if (!group_tool(&g[i], ep, target, tol, metric, &tools[i], err)) { for (int k = 0; k < i; k++) body_free(&tools[k]); free(tools); return 0; }
    if (ep->type == EX_CREATE) {
        /* 4.7: each group becomes its own new body; the name is the operation's (panel Name). How Mastercam names
           bodies (FUN_18012fa00) is NOT DECODED. */
        *bodies = realloc(*bodies, sizeof(Body) * (*nbodies + ng));
        for (int i = 0; i < ng; i++) { snprintf(tools[i].name, sizeof tools[i].name, "%s", name ? name : ""); (*bodies)[(*nbodies)++] = tools[i]; }
        free(tools); return 1;
    }
    /* Cut: ps_boolean_cut(target, tools); Add boss: ps_boolean_union; no resulting body -> message_box_ok(559, 553) */
    Body all = tools[0];
    for (int i = 1; i < ng; i++) { Body u = body_bool(&all, &tools[i], 0); body_free(&all); body_free(&tools[i]); all = u; }
    free(tools);
    Body *t = &(*bodies)[ep->target];
    Body r = body_bool(t, &all, ep->type == EX_CUT ? 2 : 0);
    snprintf(r.name, sizeof r.name, "%s", t->name);
    body_free(&all);
    if (!r.n) { body_free(&r); err->id = 559; err->caption = 553; return 0; }
    body_free(t); *t = r;
    return 1;
}

/* PsClashTest (mcgeomsld 0x1800308d0, disassembly 0x180030971-0x1800309da): PK_TOPOL_clash with a stop at the first clash that is
   not type 4 / 6. 1 = no clash, or a clash of type 0 among those read; 3 = any other clash (0 = the call failed; 2 is never returned).
   On slab bodies "clash" = the two share volume. A contact without shared volume counts as no clash: that type 0 is Parasolid's
   abut type is an INFERENCE (no Parasolid header here; PsClashTest handles type 0 as "no clash", the only type it does). */
static int body_clash(const Body *a, const Body *b) {
    for (int i = 0; i < a->n; i++) for (int k = 0; k < b->n; k++) {
        const Slab *p = &a->s[i], *q = &b->s[k];
        double lo = p->z0 > q->z0 ? p->z0 : q->z0, hi = p->z1 < q->z1 ? p->z1 : q->z1;
        if (hi - lo < 1e-9) continue;                     /* body_bool's own slab threshold */
        CPaths r = cl_bool(&p->reg, &q->reg, 1); int hit = fabs(cp_area(&r)) >= 1e-12;              /* body_add's empty-region test */
        cl_free(&r);
        if (hit) return 3;
    }
    return 1;
}
static void bbox_add(double *lo, double *hi, double x, double y, double z) {
    double v[3] = { x, y, z };
    for (int i = 0; i < 3; i++) { if (v[i] < lo[i]) lo[i] = v[i]; if (v[i] > hi[i]) hi[i] = v[i]; }
}
static void bbox_paths(double *lo, double *hi, const CPaths *r, double z) {
    for (int i = 0; i < r->n; i++) for (int k = 0; k < r->p[i].n; k++) bbox_add(lo, hi, r->p[i].xy[2 * k], r->p[i].xy[2 * k + 1], z);
}
int solid_target_search(const Body *bs, int nb, const Profile *g, int ng, const ExtrudeParams *ep0, double tol, int metric, int *flip) {
    if (nb < 1 || ng < 1) return -1;
    /* FUN_1800da210: the extents of the visible bodies and of the chains' entities; FUN_1800d8d70: the test op is the panel's with
       method Distance and Distance = that box's diagonal. FUN_1800d26d0 clears type, target and trim while it builds the tools. */
    double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
    for (int i = 0; i < nb; i++) for (int k = 0; k < bs[i].n; k++) { bbox_paths(lo, hi, &bs[i].s[k].reg, bs[i].s[k].z0); bbox_paths(lo, hi, &bs[i].s[k].reg, bs[i].s[k].z1); }
    for (int k = 0; k < ng; k++) { bbox_paths(lo, hi, &g[k].closed, g[k].z); bbox_paths(lo, hi, &g[k].open, g[k].z); }
    ExtrudeParams ep = *ep0; ep.type = EX_CREATE; ep.target = -1; ep.trim = 0; ep.method = EXM_DISTANCE;
    ep.dist = sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
    Body *fw = calloc(ng, sizeof(Body)), *rv = calloc(ng, sizeof(Body));
    int nf = 0, nr = 0, best = -1; ExErr e;
    for (; nf < ng; nf++) if (!group_tool(&g[nf], &ep, NULL, tol, metric, &fw[nf], &e)) goto done;      /* FUN_1800da960: no tools -> -1 */
    if (!ep.both) {                                       /* P+0x3a off and param_4: the tools again with Reverse all toggled; any failure -> none */
        ExtrudeParams er = ep; er.reverse = !er.reverse;
        for (; nr < ng; nr++) if (!group_tool(&g[nr], &er, NULL, tol, metric, &rv[nr], &e)) { for (int i = 0; i < nr; i++) body_free(&rv[i]); nr = 0; break; }
    }
    /* FUN_1800da960 l.1500-1555: a body stays only if every group's tool hits it, or (not Both) its reversed tool does - that group is flagged;
       l.1645-1730: of those, the smallest mean of min(|far side - chain plane|, |near side - chain plane|) (FUN_1800d9d40), within tol the fewest flags, first wins */
    int *fl = calloc(ng, sizeof(int)); double bv = 1e38; int bn = 0;
    for (int i = 0; i < nb; i++) {
        int ok = bs[i].n > 0, n = 0; memset(fl, 0, sizeof(int) * ng);
        for (int k = 0; ok && k < ng; k++) {
            if (body_clash(&bs[i], &fw[k]) >= 2) continue;
            if (nr != ng || body_clash(&bs[i], &rv[k]) < 2) ok = 0; else { fl[k] = 1; n++; }
        }
        if (!ok) continue;
        double zl = bs[i].s[0].z0, zh = bs[i].s[bs[i].n - 1].z1, sum = 0;
        for (int k = 0; k < ng; k++) { double a = fabs(zh - g[k].z), b = fabs(zl - g[k].z); sum += a < b ? a : b; }
        double v = sum / ng;
        if (best < 0 || (fabs(v - bv) >= tol ? v < bv : n < bn)) { best = i; bv = v; bn = n; memcpy(flip, fl, sizeof(int) * ng); }
    }
    free(fl);
    for (int i = 0; i < nr; i++) body_free(&rv[i]);
done:
    for (int i = 0; i < nf; i++) body_free(&fw[i]);
    free(fw); free(rv);
    return best;
}

/* ray picking (ours): caps and side walls of every slab */
static int reg_inside(const CPaths *r, double x, double y) {
    int w = 0;
    for (int i = 0; i < r->n; i++) { const CPath *p = &r->p[i];
        for (int k = 0; k < p->n; k++) {
            double ax = p->xy[2 * k], ay = p->xy[2 * k + 1], bx = p->xy[2 * ((k + 1) % p->n)], by = p->xy[2 * ((k + 1) % p->n) + 1];
            double c = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
            if (ay <= y) { if (by > y && c > 0) w++; } else if (by <= y && c < 0) w--;
        } }
    return w != 0;
}
int body_hit(const Body *b, const double o[3], const double d[3], double *tout) {
    if (b->occ) return ocs_hit(b->occ, o, d, tout);
    double best = 1e300;
    for (int i = 0; i < b->n; i++) {
        const Slab *s = &b->s[i];
        if (fabs(d[2]) > 1e-12) for (int c = 0; c < 2; c++) {
            double t = ((c ? s->z1 : s->z0) - o[2]) / d[2];
            if (t >= 0 && t < best && reg_inside(&s->reg, o[0] + t * d[0], o[1] + t * d[1])) best = t;
        }
        for (int pi = 0; pi < s->reg.n; pi++) { const CPath *p = &s->reg.p[pi];
            for (int k = 0; k < p->n; k++) {
                double ax = p->xy[2 * k], ay = p->xy[2 * k + 1], ex = p->xy[2 * ((k + 1) % p->n)] - ax, ey = p->xy[2 * ((k + 1) % p->n) + 1] - ay;
                double den = d[0] * ey - d[1] * ex; if (fabs(den) < 1e-12) continue;
                double t = ((ax - o[0]) * ey - (ay - o[1]) * ex) / den, u = ((ax - o[0]) * d[1] - (ay - o[1]) * d[0]) / den;
                double z = o[2] + t * d[2];
                if (t >= 0 && t < best && u >= 0 && u <= 1 && z >= s->z0 && z <= s->z1) best = t;
            } }
    }
    if (best == 1e300) return 0;
    *tout = best; return 1;
}

double body_volume(const Body *b) { if (b->occ) return ocs_volume(b->occ); double v = 0; for (int i = 0; i < b->n; i++) v += cp_area(&b->s[i].reg) * (b->s[i].z1 - b->s[i].z0); return v; }

/* ---------------------------------------------------------------------------------------- triangulation */
typedef struct { double x, y; } Q;
static double cr(Q a, Q b, Q c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }
static int in_tri(Q p, Q a, Q b, Q c) {      /* strictly inside (points equal to a corner are not) */
    if ((p.x == a.x && p.y == a.y) || (p.x == b.x && p.y == b.y) || (p.x == c.x && p.y == c.y)) return 0;
    return cr(a, b, p) >= 0 && cr(b, c, p) >= 0 && cr(c, a, p) >= 0;
}

/* splice hole h (clockwise) into polygon poly (counter-clockwise) with a bridge from its rightmost vertex */
static Q *bridge(Q *poly, int *n, const Q *h, int hn) {
    int m = 0; for (int i = 1; i < hn; i++) if (h[i].x > h[m].x) m = i;
    Q M = h[m]; int best = -1; double bx = 1e300;
    for (int i = 0; i < *n; i++) {           /* nearest edge to the right crossing the horizontal ray from M */
        Q a = poly[i], b = poly[(i + 1) % *n];
        if ((a.y > M.y) == (b.y > M.y) && a.y != M.y && b.y != M.y) continue;
        if (a.y == b.y) continue;
        double t = (M.y - a.y) / (b.y - a.y); if (t < 0 || t > 1) continue;
        double x = a.x + t * (b.x - a.x);
        if (x >= M.x && x < bx) { bx = x; best = a.x > b.x ? i : (i + 1) % *n; }
    }
    if (best < 0) return poly;                /* not inside: drop the hole */
    Q I = { bx, M.y }, P = poly[best];
    double bang = 1e300;                       /* a reflex vertex inside (M, I, P) is a better (visible) target */
    for (int i = 0; i < *n; i++) {
        Q v = poly[i];
        if (i == best || v.x < M.x) continue;
        Q pv = poly[(i + *n - 1) % *n], nv = poly[(i + 1) % *n];
        if (cr(pv, v, nv) >= 0) continue;     /* convex */
        int inside = (I.y < P.y) ? in_tri(v, M, I, P) || in_tri(v, M, P, I) : in_tri(v, M, P, I) || in_tri(v, M, I, P);
        if (!inside) continue;
        double ang = fabs(atan2(v.y - M.y, v.x - M.x));
        if (ang < bang) { bang = ang; best = i; }
    }
    Q *o = malloc(sizeof(Q) * (*n + hn + 2)); int k = 0;
    for (int i = 0; i <= best; i++) o[k++] = poly[i];
    for (int j = 0; j <= hn; j++) o[k++] = h[(m + j) % hn];
    o[k++] = poly[best];
    for (int i = best + 1; i < *n; i++) o[k++] = poly[i];
    free(poly); *n = k; return o;
}

/* ear clipping of a simple counter-clockwise polygon: emits index triples into tri (returns count) */
static int earclip(const Q *p, int n, int *tri) {
    int *v = malloc(sizeof(int) * n), m = n, nt = 0;
    for (int i = 0; i < n; i++) v[i] = i;
    int guard = 0, i = 0;
    while (m > 3 && guard < 2 * m) {
        int a = v[(i + m - 1) % m], b = v[i % m], c = v[(i + 1) % m];
        int ear = cr(p[a], p[b], p[c]) > 0;
        for (int k = 0; ear && k < m; k++) {
            int q = v[k]; if (q == a || q == b || q == c) continue;
            int qp = v[(k + m - 1) % m], qn = v[(k + 1) % m];
            if (cr(p[qp], p[q], p[qn]) > 0) continue;       /* only reflex vertices can be inside an ear */
            if (in_tri(p[q], p[a], p[b], p[c])) ear = 0;
        }
        if (ear) {
            tri[3 * nt] = a; tri[3 * nt + 1] = b; tri[3 * nt + 2] = c; nt++;
            memmove(v + i % m, v + i % m + 1, sizeof(int) * (m - i % m - 1)); m--; guard = 0;
            if (i >= m) i = 0;
        } else { i = (i + 1) % m; guard++; }
    }
    if (m > 3) for (int k = 1; k + 1 < m; k++) { tri[3 * nt] = v[0]; tri[3 * nt + 1] = v[k]; tri[3 * nt + 2] = v[k + 1]; nt++; }   /* degenerate rest */
    else if (m == 3 && cr(p[v[0]], p[v[1]], p[v[2]]) != 0) { tri[3 * nt] = v[0]; tri[3 * nt + 1] = v[1]; tri[3 * nt + 2] = v[2]; nt++; }
    free(v); return nt;
}

typedef struct { float *t; int n, cap; } TB;
static void tb_v(TB *b, double x, double y, double z, double nx, double ny, double nz) {
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 3072; b->t = realloc(b->t, sizeof(float) * 6 * b->cap); }
    float *q = b->t + 6 * b->n++; q[0] = (float)x; q[1] = (float)y; q[2] = (float)z; q[3] = (float)nx; q[4] = (float)ny; q[5] = (float)nz;
}

static void cap(TB *b, const CPaths *reg, double z, int up) {
    if (!reg->n) return;
    int *own; CPaths g = cl_group(reg, &own);
    for (int o = 0; o < g.n; o++) {
        if (own[o] != o) continue;
        int n = g.p[o].n; Q *poly = malloc(sizeof(Q) * (n ? n : 1));
        for (int i = 0; i < n; i++) { poly[i].x = g.p[o].xy[2 * i]; poly[i].y = g.p[o].xy[2 * i + 1]; }
        /* holes of this outer, rightmost first */
        int nh = 0, *hi = malloc(sizeof(int) * g.n); double *hx = malloc(sizeof(double) * g.n);
        for (int h = 0; h < g.n; h++) if (h != o && own[h] == o && g.p[h].n >= 3) {
            double mx = -1e300; for (int i = 0; i < g.p[h].n; i++) if (g.p[h].xy[2 * i] > mx) mx = g.p[h].xy[2 * i];
            int k = nh++; while (k > 0 && hx[k - 1] < mx) { hx[k] = hx[k - 1]; hi[k] = hi[k - 1]; k--; } hx[k] = mx; hi[k] = h;
        }
        for (int k = 0; k < nh; k++) {
            const CPath *hp = &g.p[hi[k]]; Q *hq = malloc(sizeof(Q) * hp->n);
            for (int i = 0; i < hp->n; i++) { hq[i].x = hp->xy[2 * i]; hq[i].y = hp->xy[2 * i + 1]; }
            poly = bridge(poly, &n, hq, hp->n); free(hq);
        }
        int *tri = malloc(sizeof(int) * 3 * (n ? n : 1)); int nt = earclip(poly, n, tri);
        for (int t = 0; t < nt; t++) for (int j = 0; j < 3; j++) {
            Q q = poly[tri[3 * t + (up ? j : 2 - j)]]; tb_v(b, q.x, q.y, z, 0, 0, up ? 1 : -1); }
        free(tri); free(poly); free(hi); free(hx);
    }
    free(own); cl_free(&g);
}

static void walls(TB *b, const CPaths *reg, double z0, double z1) {
    for (int pi = 0; pi < reg->n; pi++) {
        const CPath *p = &reg->p[pi]; int n = p->n; if (n < 2) continue;
        double *nx = malloc(sizeof(double) * 2 * n);       /* edge normals: right of travel = out of the material */
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n; double dx = p->xy[2 * j] - p->xy[2 * i], dy = p->xy[2 * j + 1] - p->xy[2 * i + 1], l = hypot(dx, dy);
            nx[2 * i] = l > 0 ? dy / l : 0; nx[2 * i + 1] = l > 0 ? -dx / l : 0;
        }
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n, ip = (i + n - 1) % n, jn = j;
            double ex = nx[2 * i], ey = nx[2 * i + 1], ax = ex, ay = ey, bx = ex, by = ey;
            /* smooth across shallow corners (tessellated arcs), sharp otherwise */
            if (ex * nx[2 * ip] + ey * nx[2 * ip + 1] > 0.9) { ax = ex + nx[2 * ip]; ay = ey + nx[2 * ip + 1]; double l = hypot(ax, ay); ax /= l; ay /= l; }
            if (ex * nx[2 * jn] + ey * nx[2 * jn + 1] > 0.9) { bx = ex + nx[2 * jn]; by = ey + nx[2 * jn + 1]; double l = hypot(bx, by); bx /= l; by /= l; }
            double x0 = p->xy[2 * i], y0 = p->xy[2 * i + 1], x1 = p->xy[2 * j], y1 = p->xy[2 * j + 1];
            tb_v(b, x0, y0, z0, ax, ay, 0); tb_v(b, x1, y1, z0, bx, by, 0); tb_v(b, x1, y1, z1, bx, by, 0);
            tb_v(b, x0, y0, z0, ax, ay, 0); tb_v(b, x1, y1, z1, bx, by, 0); tb_v(b, x0, y0, z1, ax, ay, 0);
        }
        free(nx);
    }
}

/* ours: the display tessellation of an OpenCASCADE body (0.02 mm / 0.001 in, as the imported solids' occt_brep_mesh, scene.c) */
static double occ_defl(const Body *bd) { return bd->xt_scale > 100 ? 0.02 : 0.001; }
int body_mesh(const Body *bd, float **out) {
    if (bd->occ) return ocs_mesh(bd->occ, occ_defl(bd), out);
    TB b = { 0 };
    CPaths none = cp_empty();
    for (int k = 0; k < bd->n; k++) {
        const Slab *s = &bd->s[k];
        walls(&b, &s->reg, s->z0, s->z1);
        int below = k > 0 && fabs(bd->s[k - 1].z1 - s->z0) < 1e-9, above = k + 1 < bd->n && fabs(bd->s[k + 1].z0 - s->z1) < 1e-9;
        CPaths dn = cl_bool(&s->reg, below ? &bd->s[k - 1].reg : &none, 2);     /* faces looking down: not covered below */
        CPaths upf = cl_bool(&s->reg, above ? &bd->s[k + 1].reg : &none, 2);    /* faces looking up: not covered above */
        cap(&b, &dn, s->z0, 0); cap(&b, &upf, s->z1, 1);
        cl_free(&dn); cl_free(&upf);
    }
    cl_free(&none);
    *out = b.t;
    return b.n / 3;
}

/* Body edges for display. sld_draw_solids (mcgeomsld 0x1800b4aa0) draws every solid once with _render_cfg = 1 before
   the shaded pass, and sld_draw_solid_wire (FUN_1800b46b0) draws the body's Parasolid edges (ps_gen_disp_list) in the
   solid's colour with lighting off (glDisable 0xb50); the shaded faces get glPolygonOffset(1, 1) (FUN_1800b4010,
   _render_cfg 4) so the edges stay on top. This kernel has no B-rep faces, so the edges are taken from the display
   mesh (ours): a triangle side used once (a slab outline) or shared by two triangles whose normals differ by more
   than EDGE_DEG. Faceted arcs turn by less than that per facet, so a cylinder wall shows no facet lines; a tangent
   line / arc joint (a real Parasolid edge) is not found this way. Returns the segment count, *out = 6 floats each. */
#define EDGE_DEG 20.0
typedef struct { float a[3], b[3]; int tri; } MEdge;
static int medge_cmp(const void *x, const void *y) {
    const MEdge *p = x, *q = y;
    for (int k = 0; k < 3; k++) { if (p->a[k] != q->a[k]) return p->a[k] < q->a[k] ? -1 : 1; }
    for (int k = 0; k < 3; k++) { if (p->b[k] != q->b[k]) return p->b[k] < q->b[k] ? -1 : 1; }
    return 0;
}
static int vless(const float *u, const float *v) { for (int k = 0; k < 3; k++) if (u[k] != v[k]) return u[k] < v[k]; return 0; }
int body_edges(const Body *bd, float **out) {
    if (bd->occ) return ocs_edges(bd->occ, occ_defl(bd), out);   /* the B-rep edges (sld_draw_solid_wire) */
    float *t; int nt = body_mesh(bd, &t);
    *out = NULL;
    if (nt <= 0) { free(t); return 0; }
    MEdge *e = malloc(sizeof(MEdge) * 3 * (size_t)nt); int ne = 0;
    for (int i = 0; i < nt; i++) for (int s = 0; s < 3; s++) {
        const float *u = t + i * 18 + s * 6, *v = t + i * 18 + ((s + 1) % 3) * 6;
        if (!memcmp(u, v, 12)) continue;
        MEdge m; const float *lo = vless(u, v) ? u : v, *hi = lo == u ? v : u;
        memcpy(m.a, lo, 12); memcpy(m.b, hi, 12); m.tri = i; e[ne++] = m;
    }
    qsort(e, ne, sizeof(MEdge), medge_cmp);
    double cmax = cos(EDGE_DEG * M_PI / 180);
    float *o = malloc(sizeof(float) * 6 * (size_t)(ne + 1)); int no = 0;
    for (int i = 0; i < ne; ) {
        int j = i + 1; while (j < ne && !medge_cmp(&e[i], &e[j])) j++;
        int draw = j - i != 2;                            /* used once (an outline), or a non-manifold side */
        if (!draw) {
            const float *n1 = t + e[i].tri * 18 + 3, *n2 = t + e[i + 1].tri * 18 + 3;
            draw = n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2] < cmax;
        }
        if (draw) { memcpy(o + no * 6, e[i].a, 12); memcpy(o + no * 6 + 3, e[i].b, 12); no++; }
        i = j;
    }
    free(e); free(t);
    *out = o;
    return no;
}

/* ---------------------------------------------------------------------------------------- edges for the AutoCursor */
/* body_brep_edges (solid25.h): Mastercam's AutoCursor walks the Parasolid edges of a body (sld_search_fast /
   sld_select -> ps_select -> PK_BODY_pick_topols, decomp/mcgeomsld_snap.c); this kernel has no B-rep, so the edges are
   rebuilt from the slabs (ours): the face outlines at each slab height (the caps of body_mesh: the region not covered
   above / below) and the vertical edges where the wall normals turn by more than EDGE_DEG (body_edges' rule), merged
   through degree-2 vertices that continue in line (one Parasolid edge across stacked slabs / a collinear split). */
typedef struct { double a[3], b[3]; } RSeg;
typedef struct { long long k[3]; int i; } VKey;
static int vkey_cmp(const void *x, const void *y) {
    const VKey *p = x, *q = y;
    for (int k = 0; k < 3; k++) if (p->k[k] != q->k[k]) return p->k[k] < q->k[k] ? -1 : 1;
    return 0;
}
static int ipair_cmp(const void *x, const void *y) {
    const int *p = x, *q = y; return p[0] != q[0] ? (p[0] < q[0] ? -1 : 1) : p[1] != q[1] ? (p[1] < q[1] ? -1 : 1) : 0;
}
static void rs_add(RSeg **s, int *n, int *cap, double ax, double ay, double az, double bx, double by, double bz) {
    if (ax == bx && ay == by && az == bz) return;
    if (*n == *cap) { *cap = *cap ? *cap * 2 : 256; *s = realloc(*s, sizeof(RSeg) * *cap); }
    RSeg *r = *s + (*n)++; r->a[0] = ax; r->a[1] = ay; r->a[2] = az; r->b[0] = bx; r->b[1] = by; r->b[2] = bz;
}
static void rs_outline(RSeg **s, int *n, int *cap, const CPaths *r, double z) {
    for (int pi = 0; pi < r->n; pi++) { const CPath *p = &r->p[pi];
        for (int k = 0; k < p->n; k++) { int j = (k + 1) % p->n;
            rs_add(s, n, cap, p->xy[2 * k], p->xy[2 * k + 1], z, p->xy[2 * j], p->xy[2 * j + 1], z); } }
}
/* the arc carrier a horizontal cap-outline segment lies on (-1 = none): both ends within the carrier's tol (+ 2e-5:
   the 1e5-per-unit integer grid of the Clipper booleans, offset.cpp) of the circle, the segment no deeper inside it
   than that (a chord of the faceting has sagitta <= tol), its angle at most the faceting step (cam2d.c
   cam_chain_polyline: step <= pi / 18) and both ends inside the arc's sweep. The nearest fitting carrier wins. */
static int seg_carrier(const Body *bd, const double *A, const double *B) {
    if (fabs(A[2] - B[2]) > 1e-12 || !bd->narc) return -1;
    int best = -1; double bdev = 1e300;
    for (int k = 0; k < bd->narc; k++) {
        const BArc *c = &bd->arc[k]; double t = c->tol + 2e-5;
        double ax = A[0] - c->cx, ay = A[1] - c->cy, bx = B[0] - c->cx, by = B[1] - c->cy;
        double ra = hypot(ax, ay), rb = hypot(bx, by);
        if (fabs(ra - c->r) > t || fabs(rb - c->r) > t) continue;
        double ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey; if (l2 <= 0) continue;
        double u = -(ax * ex + ay * ey) / l2; if (u < 0) u = 0; if (u > 1) u = 1;
        double dmin = hypot(ax + u * ex, ay + u * ey);
        if (dmin < c->r - t) continue;
        double aa = atan2(ay, ax), ab = atan2(by, bx), d = remainder(ab - aa, 2 * M_PI);
        if (fabs(d) < 1e-12 || fabs(d) > M_PI / 18 + 1e-9) continue;
        if (c->sw < 2 * M_PI - 1e-9) {
            double eps = t / c->r, da = fmod(aa - c->a0, 2 * M_PI), db = fmod(ab - c->a0, 2 * M_PI);
            if (da < 0) da += 2 * M_PI;
            if (db < 0) db += 2 * M_PI;
            if (!(da <= c->sw + eps || da >= 2 * M_PI - eps) || !(db <= c->sw + eps || db >= 2 * M_PI - eps)) continue;
        }
        double dev = fmax(fmax(fabs(ra - c->r), fabs(rb - c->r)), c->r - dmin);
        if (dev < bdev) { bdev = dev; best = k; }
    }
    return best;
}
/* an arc / ring edge on carrier c through the facet points pts[0..np-1] (kind 1 / 2): the circle about +Z (-Z when
   the entity's sweep was negative) with its parameter 0 along +X (ps_arc_to_ps_arc builds the chain arc's PK circle
   with the view's Z as axis - negated for a negative sweep - and X as ref_direction, mcgeomsld 0x18003e060,
   decomp/mcgeomsld_arcs.c). The interval: t0 = the counter-clockwise start angle restricted to [0, 2 pi) (as
   FUN_180043850 restricts an edge's start angle when it converts a PK circle edge), t1 = t0 + the angle the facets
   turn through; a ring edge 0 .. 2 pi */
static void arc_edge(BEdge *e, const BArc *c, const double *pts, int np, int kind) {
    double s = c->cw ? -1 : 1;                          /* ps_arc_to_ps_arc: axis -Z for a negative sweep */
    e->arc = kind; e->c[0] = c->cx; e->c[1] = c->cy; e->c[2] = pts[2]; e->ax[2] = s; e->xa[0] = 1; e->r = c->r;
    if (kind == 2) { e->t0 = 0; e->t1 = 2 * M_PI; return; }
    double sum = 0;
    for (int k = 0; k + 1 < np; k++)
        sum += remainder(atan2(pts[3 * k + 4] - c->cy, pts[3 * k + 3] - c->cx) - atan2(pts[3 * k + 1] - c->cy, pts[3 * k] - c->cx), 2 * M_PI);
    /* the start of the arc in the circle's sense (counter-clockwise about ax): about +Z the counter-clockwise start,
       about -Z the counter-clockwise end; its parameter = s x the world angle */
    const double *S = (sum >= 0) == (s > 0) ? pts : pts + 3 * (np - 1);
    double t0 = fmod(s * atan2(S[1] - c->cy, S[0] - c->cx), 2 * M_PI); if (t0 < 0) t0 += 2 * M_PI;
    e->t0 = t0; e->t1 = t0 + fabs(sum);
}
void bedges_free(BEdge *e, int n) { for (int i = 0; i < n; i++) free(e[i].pts); free(e); }
/* what body_brep_topo needs from the edge build: the unique vertex keys (sorted, with their vertex numbers), the
   segments as sorted vertex pairs (lo < hi), and per segment its edge (-1 = in no edge) and whether lo -> hi runs
   along that edge's direction (pts order) */
typedef struct { VKey *vk; int nvk, nv; int *sg, nsg, *seg_edge; char *seg_fwd; } BrepCx;
static int brep_edges_core(const Body *bd, BEdge **out, BrepCx *cx);
/* an OpenCASCADE body: its XT's edges, read exactly as a part solid's (xt_mesh.c xt_brep_edges: lines / circles) */
static int occ_brep_edges(const Body *bd, BEdge **out) {
    *out = NULL; if (!bd->xt) return 0;
    XtSchema *sc = xt_schema_for(bd->xt, bd->xt_n); if (!sc) return 0;
    XtBEdge *x = NULL; int sk = 0, n = xt_brep_edges(sc, bd->xt, bd->xt_n, bd->xt_scale, &x, &sk);
    *out = calloc(n > 0 ? n : 1, sizeof(BEdge));
    for (int k = 0; k < n; k++) {
        BEdge *E = &(*out)[k]; E->pts = x[k].pts; x[k].pts = NULL; E->np = x[k].np; E->arc = x[k].arc; E->r = x[k].r;
        memcpy(E->c, x[k].c, sizeof E->c); memcpy(E->ax, x[k].ax, sizeof E->ax); memcpy(E->xa, x[k].xa, sizeof E->xa); E->t0 = x[k].t0; E->t1 = x[k].t1;
    }
    xt_bedges_free(x, n);
    return n > 0 ? n : 0;
}
int body_brep_edges(const Body *bd, BEdge **out) { if (bd->occ) return occ_brep_edges(bd, out); return brep_edges_core(bd, out, NULL); }
static int brep_edges_core(const Body *bd, BEdge **out, BrepCx *cx) {
    RSeg *s = NULL; int ns = 0, cap = 0;
    CPaths none = cp_empty();
    const double cmax = cos(EDGE_DEG * M_PI / 180);
    for (int k = 0; k < bd->n; k++) {
        const Slab *sl = &bd->s[k];
        int below = k > 0 && fabs(bd->s[k - 1].z1 - sl->z0) < 1e-9, above = k + 1 < bd->n && fabs(bd->s[k + 1].z0 - sl->z1) < 1e-9;
        CPaths dn = cl_bool(&sl->reg, below ? &bd->s[k - 1].reg : &none, 2), upf = cl_bool(&sl->reg, above ? &bd->s[k + 1].reg : &none, 2);
        rs_outline(&s, &ns, &cap, &dn, sl->z0); rs_outline(&s, &ns, &cap, &upf, sl->z1);
        cl_free(&dn); cl_free(&upf);
        for (int pi = 0; pi < sl->reg.n; pi++) { const CPath *p = &sl->reg.p[pi]; int n = p->n; if (n < 3) continue;
            for (int i = 0; i < n; i++) {
                int ip = (i + n - 1) % n, j = (i + 1) % n;
                double ux = p->xy[2 * i] - p->xy[2 * ip], uy = p->xy[2 * i + 1] - p->xy[2 * ip + 1];
                double vx = p->xy[2 * j] - p->xy[2 * i], vy = p->xy[2 * j + 1] - p->xy[2 * i + 1];
                double lu = hypot(ux, uy), lv = hypot(vx, vy); if (lu <= 0 || lv <= 0) continue;
                if ((ux * vx + uy * vy) / (lu * lv) < cmax)      /* the wall normals turn by more than EDGE_DEG */
                    rs_add(&s, &ns, &cap, p->xy[2 * i], p->xy[2 * i + 1], sl->z0, p->xy[2 * i], p->xy[2 * i + 1], sl->z1);
            } }
    }
    cl_free(&none);
    *out = NULL;
    if (!ns) { free(s); return 0; }
    /* shared vertices: points equal within 1e-7 model units (ours) */
    VKey *vk = malloc(sizeof(VKey) * 2 * ns);
    for (int i = 0; i < ns; i++) for (int e = 0; e < 2; e++) { const double *q = e ? s[i].b : s[i].a; VKey *v = vk + 2 * i + e;
        for (int k = 0; k < 3; k++) v->k[k] = llround(q[k] * 1e7); v->i = 2 * i + e; }
    qsort(vk, 2 * ns, sizeof(VKey), vkey_cmp);
    int *vid = malloc(sizeof(int) * 2 * ns), nv = 0; double *vp = malloc(sizeof(double) * 3 * 2 * ns);
    VKey *uk = cx ? malloc(sizeof(VKey) * 2 * ns) : NULL;
    for (int i = 0; i < 2 * ns; i++) {
        if (i == 0 || vkey_cmp(&vk[i - 1], &vk[i])) { const double *q = vk[i].i & 1 ? s[vk[i].i / 2].b : s[vk[i].i / 2].a; memcpy(vp + 3 * nv, q, sizeof(double) * 3);
            if (uk) { uk[nv] = vk[i]; uk[nv].i = nv; } nv++; }
        vid[vk[i].i] = nv - 1;
    }
    free(vk);
    /* the segments as vertex pairs (start < end), duplicates dropped (an outline shared by an up and a down face) */
    int *sg = malloc(sizeof(int) * 2 * ns), nsg = 0;
    for (int i = 0; i < ns; i++) { int a = vid[2 * i], b = vid[2 * i + 1]; if (a == b) continue;
        sg[2 * nsg] = a < b ? a : b; sg[2 * nsg + 1] = a < b ? b : a; nsg++; }
    free(vid); free(s);
    qsort(sg, nsg, sizeof(int) * 2, ipair_cmp);
    int m = 0; for (int i = 0; i < nsg; i++) if (!m || ipair_cmp(sg + 2 * i, sg + 2 * (m - 1))) { sg[2 * m] = sg[2 * i]; sg[2 * m + 1] = sg[2 * i + 1]; m++; }
    nsg = m;
    /* vertex -> segments */
    int *deg = calloc(nv + 1, sizeof(int)), *adj, *fill = calloc(nv, sizeof(int));
    for (int i = 0; i < nsg; i++) { deg[sg[2 * i] + 1]++; deg[sg[2 * i + 1] + 1]++; }
    for (int v = 0; v < nv; v++) deg[v + 1] += deg[v];
    adj = malloc(sizeof(int) * (2 * nsg + 1));
    for (int i = 0; i < nsg; i++) for (int e = 0; e < 2; e++) { int v = sg[2 * i + e]; adj[deg[v] + fill[v]++] = i; }
    free(fill);
    /* the round edges: each horizontal segment's arc carrier (seg_carrier), -1 = none */
    int *carr = malloc(sizeof(int) * (nsg ? nsg : 1));
    for (int i = 0; i < nsg; i++) carr[i] = seg_carrier(bd, vp + 3 * sg[2 * i], vp + 3 * sg[2 * i + 1]);
    /* a vertex where an arc carrier ends (a trimmed arc) lies on a facet chord, up to the tolerance off the circle:
       it is moved to where the true curves meet (Parasolid's vertex lies on both edge curves) - the circle and the
       other horizontal segment's line, or the two circles - the intersection nearest the vertex, when it is within
       one faceting step (r pi / 18, cam_chain_polyline) of it; the vertex's other segments (the vertical edge,
       the straight edge) move with it */
    for (int v = 0; v < nv; v++) {
        int sa = -1, sb = -1;
        for (int a = deg[v]; a < deg[v + 1]; a++) {
            int si = adj[a], o = sg[2 * si] == v ? sg[2 * si + 1] : sg[2 * si];
            if (fabs(vp[3 * o + 2] - vp[3 * v + 2]) > 1e-12) continue;          /* horizontal segments only */
            if (carr[si] >= 0 && sa < 0) sa = si;
            else if (sb < 0 || (carr[sb] >= 0 && carr[si] < 0)) sb = si;
        }
        if (sa < 0 || sb < 0 || carr[sa] == carr[sb]) continue;
        const BArc *A = &bd->arc[carr[sa]]; double *P = vp + 3 * v, X[2][2]; int nx = 0;
        if (carr[sb] < 0) {                                /* circle x the straight segment's line */
            int o = sg[2 * sb] == v ? sg[2 * sb + 1] : sg[2 * sb];
            double dx = vp[3 * o] - P[0], dy = vp[3 * o + 1] - P[1], l = hypot(dx, dy); if (l <= 0) continue;
            dx /= l; dy /= l;
            double fx = P[0] - A->cx, fy = P[1] - A->cy, bq = fx * dx + fy * dy, cq = fx * fx + fy * fy - A->r * A->r, D = bq * bq - cq;
            if (D < 0) continue;
            for (int k = -1; k <= 1; k += 2) { double t = -bq + k * sqrt(D); X[nx][0] = P[0] + t * dx; X[nx][1] = P[1] + t * dy; nx++; }
        } else {                                           /* circle x circle */
            const BArc *B = &bd->arc[carr[sb]];
            double dx = B->cx - A->cx, dy = B->cy - A->cy, d = hypot(dx, dy);
            if (d <= 0 || d > A->r + B->r || d < fabs(A->r - B->r)) continue;
            double a = (A->r * A->r - B->r * B->r + d * d) / (2 * d), h2 = A->r * A->r - a * a, h = h2 > 0 ? sqrt(h2) : 0;
            double mx = A->cx + a * dx / d, my = A->cy + a * dy / d;
            for (int k = -1; k <= 1; k += 2) { X[nx][0] = mx - k * h * dy / d; X[nx][1] = my + k * h * dx / d; nx++; }
        }
        int bi = hypot(X[1][0] - P[0], X[1][1] - P[1]) < hypot(X[0][0] - P[0], X[0][1] - P[1]);
        if (hypot(X[bi][0] - P[0], X[bi][1] - P[1]) <= A->r * M_PI / 18) { P[0] = X[bi][0]; P[1] = X[bi][1]; }
    }
    /* a vertex is passed through when exactly two segments meet there and: both lie on the same carrier (3: an arc),
       they continue in line (1), or both are horizontal without a carrier and turn by less than EDGE_DEG (2: a
       faceted curve of unknown geometry); else it is a vertex of the body (0) - also where a carrier ends */
    char *pass = calloc(nv, 1);
    for (int v = 0; v < nv; v++) {
        if (deg[v + 1] - deg[v] != 2) continue;
        int s1 = adj[deg[v]], s2 = adj[deg[v] + 1];
        if (carr[s1] >= 0 || carr[s2] >= 0) { if (carr[s1] == carr[s2]) pass[v] = 3; continue; }
        int p = sg[2 * s1] == v ? sg[2 * s1 + 1] : sg[2 * s1], q = sg[2 * s2] == v ? sg[2 * s2 + 1] : sg[2 * s2];
        double d1[3], d2[3], l1 = 0, l2 = 0, dot = 0;
        for (int k = 0; k < 3; k++) { d1[k] = vp[3 * v + k] - vp[3 * p + k]; d2[k] = vp[3 * q + k] - vp[3 * v + k]; l1 += d1[k] * d1[k]; l2 += d2[k] * d2[k]; }
        l1 = sqrt(l1); l2 = sqrt(l2); if (l1 <= 0 || l2 <= 0) continue;
        for (int k = 0; k < 3; k++) dot += d1[k] * d2[k];
        dot /= l1 * l2;
        if (dot > 1 - 1e-12) pass[v] = 1;
        else if (fabs(d1[2]) < 1e-12 && fabs(d2[2]) < 1e-12 && dot > cmax) pass[v] = 2;
    }
    char *used = calloc(nsg ? nsg : 1, 1);
    BEdge *o = NULL; int no = 0, ocap = 0;
    double *pts = malloc(sizeof(double) * 3 * (nsg + 2));
    int *seg_edge = malloc(sizeof(int) * (nsg ? nsg : 1)), *walk = malloc(sizeof(int) * (nsg ? nsg : 1)); char *seg_fwd = calloc(nsg ? nsg : 1, 1);
    for (int i = 0; i < nsg; i++) seg_edge[i] = -1;
    for (int v0 = 0; v0 < nv; v0++) {
        if (pass[v0]) continue;
        for (int a = deg[v0]; a < deg[v0 + 1]; a++) {
            int sgi = adj[a]; if (used[sgi]) continue;
            int np = 0, curved = 0, v = v0, ca = carr[sgi], nw = 0;
            memcpy(pts, vp + 3 * v, sizeof(double) * 3); np = 1;
            for (;;) {
                used[sgi] = 1; walk[nw++] = sg[2 * sgi] == v ? sgi : ~sgi;   /* ~: traversed hi -> lo */
                int w = sg[2 * sgi] == v ? sg[2 * sgi + 1] : sg[2 * sgi];
                memcpy(pts + 3 * np, vp + 3 * w, sizeof(double) * 3); np++;
                v = w;
                if (!pass[v]) break;
                if (pass[v] == 2) curved = 1;
                int s1 = adj[deg[v]], s2 = adj[deg[v] + 1], nx = s1 == sgi ? s2 : s1;
                if (used[nx]) break;
                sgi = nx;
            }
            if (pass[v]) continue;                         /* ended on a used segment: part of a closed loop */
            for (int k = 0; k < nw; k++) { int q = walk[k] >= 0 ? walk[k] : ~walk[k]; seg_edge[q] = no; seg_fwd[q] = walk[k] >= 0; }
            if (no == ocap) { ocap = ocap ? ocap * 2 : 64; o = realloc(o, sizeof(BEdge) * ocap); }
            memset(&o[no], 0, sizeof(BEdge));
            if (ca >= 0) arc_edge(&o[no], &bd->arc[ca], pts, np, 1);
            else if (!curved) { np = 2; memcpy(pts + 3, vp + 3 * v, sizeof(double) * 3); }
            o[no].np = np; o[no].curved = ca < 0 && curved; o[no].pts = malloc(sizeof(double) * 3 * np); memcpy(o[no].pts, pts, sizeof(double) * 3 * np); no++;
        }
    }
    /* closed loops through passed vertices only: a full circle of one carrier is a ring edge (no vertex); other
       closed loops (faceted, no carrier) are left out */
    for (int i = 0; i < nsg; i++) {
        if (used[i] || carr[i] < 0) continue;
        int ca = carr[i], v = sg[2 * i], sgi = i, np = 1, ok = 1, nw = 0;
        memcpy(pts, vp + 3 * v, sizeof(double) * 3);
        for (;;) {
            used[sgi] = 1; if (carr[sgi] != ca) ok = 0; walk[nw++] = sg[2 * sgi] == v ? sgi : ~sgi;
            int w = sg[2 * sgi] == v ? sg[2 * sgi + 1] : sg[2 * sgi];
            memcpy(pts + 3 * np, vp + 3 * w, sizeof(double) * 3); np++;
            v = w;
            if (pass[v] != 3) { ok = 0; break; }
            int s1 = adj[deg[v]], s2 = adj[deg[v] + 1], nx = s1 == sgi ? s2 : s1;
            if (used[nx]) break;
            sgi = nx;
        }
        if (!ok || v != sg[2 * i] || np < 4) continue;
        for (int k = 0; k < nw; k++) { int q = walk[k] >= 0 ? walk[k] : ~walk[k]; seg_edge[q] = no; seg_fwd[q] = walk[k] >= 0; }
        if (no == ocap) { ocap = ocap ? ocap * 2 : 64; o = realloc(o, sizeof(BEdge) * ocap); }
        memset(&o[no], 0, sizeof(BEdge));
        arc_edge(&o[no], &bd->arc[ca], pts, np, 2);
        o[no].np = np; o[no].pts = malloc(sizeof(double) * 3 * np); memcpy(o[no].pts, pts, sizeof(double) * 3 * np); no++;
    }
    free(carr); free(walk);
    if (cx) { cx->vk = uk; cx->nvk = nv; cx->nv = nv; cx->sg = sg; cx->nsg = nsg; cx->seg_edge = seg_edge; cx->seg_fwd = seg_fwd; sg = NULL; seg_edge = NULL; seg_fwd = NULL; }
    free(seg_edge); free(seg_fwd);
    free(pts); free(used); free(pass); free(adj); free(deg); free(sg); free(vp);
    *out = o;
    return no;
}

/* ---------------------------------------------------------------------------------------- topology for solid chaining */
/* body_brep_topo (solid25.h, btopo.h): the faces / loops / fins of a body over body_brep_edges' edges, rebuilt from the
   slabs (ours: this kernel has no B-rep; Mastercam reads them from Parasolid, sld_loop_to_chain / ExtractToolPChainInput,
   formats/chaining.md "Solid chaining"). Faces: every cap region of body_mesh (an outer outline with its holes, one face
   per outer: cl_group) and the side walls - the wall quads of every slab ring segment joined across their sides that are
   no edge segment (smooth facet joints, the same wall continuing into the slab above), their horizontal sides split at the
   vertices of that height first (T-junctions of booleans). Loops: the face's edge segments chained head to tail in the
   face's outward-CCW sense (Parasolid: the face on the left seen from outside), cut into fins where the edge changes. */
typedef struct { long long k[3]; int id; } HVert;
typedef struct { HVert *t; int cap, n; } VHash;
static unsigned long long vh_code(const long long *k) { return (unsigned long long)k[0] * 73856093ULL ^ (unsigned long long)k[1] * 19349663ULL ^ (unsigned long long)k[2] * 83492791ULL; }
static int vh_find(VHash *h, const long long *k, int add_id) {   /* add_id >= 0: insert with that number when missing */
    if (h->n * 2 + 2 > h->cap) {
        int nc = h->cap ? h->cap * 2 : 1024; HVert *nt = malloc(sizeof(HVert) * nc); for (int i = 0; i < nc; i++) nt[i].id = -1;
        for (int i = 0; i < h->cap; i++) if (h->t[i].id >= 0) { unsigned long long c = vh_code(h->t[i].k) % (unsigned)nc; while (nt[c].id >= 0) c = (c + 1) % (unsigned)nc; nt[c] = h->t[i]; }
        free(h->t); h->t = nt; h->cap = nc;
    }
    unsigned long long c = vh_code(k) % (unsigned)h->cap;
    while (h->t[c].id >= 0) { if (!memcmp(h->t[c].k, k, sizeof h->t[c].k)) return h->t[c].id; c = (c + 1) % (unsigned)h->cap; }
    if (add_id < 0) return -1;
    memcpy(h->t[c].k, k, sizeof h->t[c].k); h->t[c].id = add_id; h->n++; return add_id;
}
static void vkey3(double x, double y, double z, long long *k) { k[0] = llround(x * 1e7); k[1] = llround(y * 1e7); k[2] = llround(z * 1e7); }
typedef struct { int u, v, sgi; } TSide;                     /* a directed side between vertex numbers; sgi = its edge segment or -1 */
typedef struct { int lo, hi, poly; } TAdj;
static int tadj_cmp(const void *x, const void *y) { const TAdj *p = x, *q = y; return p->lo != q->lo ? (p->lo < q->lo ? -1 : 1) : p->hi != q->hi ? (p->hi < q->hi ? -1 : 1) : p->poly - q->poly; }
static int uf_find(int *p, int i) { while (p[i] != i) { p[i] = p[p[i]]; i = p[i]; } return i; }
typedef struct { BTopo *t; int capf, capl, capi; const BrepCx *cx; } TB2;
static int seg_of(const BrepCx *cx, int u, int v) {
    int key[2] = { u < v ? u : v, u < v ? v : u };
    int *f = bsearch(key, cx->sg, cx->nsg, sizeof(int) * 2, ipair_cmp);
    return f ? (int)((f - cx->sg) / 2) : -1;
}
/* a closed sequence of sides -> one loop of face `face`: fins = runs of sides on one edge, started where the edge changes */
static void add_loop(TB2 *b, const TSide *sd, int n, int face) {
    if (n <= 0) return;
    BTopo *t = b->t; const BrepCx *cx = b->cx;
    int *E = malloc(sizeof(int) * n), *P = malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++) {
        int q = sd[i].sgi; E[i] = q >= 0 ? cx->seg_edge[q] : -1;
        P[i] = q >= 0 ? ((sd[i].u < sd[i].v) == (cx->seg_fwd[q] != 0)) : 0;
        if (E[i] < 0) E[i] = -1 - i;                         /* no edge: a fin of its own that cannot chain */
    }
    int s0 = 0; for (int i = 0; i < n; i++) if (E[i] != E[(i + n - 1) % n] || P[i] != P[(i + n - 1) % n]) { s0 = i; break; }
    if (t->nloop == b->capl) { b->capl = b->capl ? b->capl * 2 : 64; t->loop = realloc(t->loop, sizeof(TLoop) * b->capl); }
    TLoop *L = &t->loop[t->nloop++]; L->f0 = t->nfin; L->nf = 0; L->face = face;
    for (int k = 0; k < n; k++) {
        int i = (s0 + k) % n;
        if (k && E[i] == E[(i + n - 1) % n] && P[i] == P[(i + n - 1) % n]) continue;
        if (t->nfin == b->capi) { b->capi = b->capi ? b->capi * 2 : 256; t->fin = realloc(t->fin, sizeof(TFin) * b->capi); }
        t->fin[t->nfin].e = E[i] >= 0 ? E[i] : -1; t->fin[t->nfin].pos = P[i]; t->nfin++; L->nf++;
    }
    free(E); free(P);
}
static TFace *new_face(TB2 *b) {
    BTopo *t = b->t;
    if (t->nface == b->capf) { b->capf = b->capf ? b->capf * 2 : 32; t->face = realloc(t->face, sizeof(TFace) * b->capf); }
    TFace *f = &t->face[t->nface++]; memset(f, 0, sizeof *f); f->l0 = t->nloop; return f;
}
/* the cap group's triangles (cap()'s triangulation of one outer with its holes) as 9 floats each */
static void cap_group_tris(const CPaths *g, const int *own, int o, double z, int up, float **tri, int *ntri) {
    int n = g->p[o].n; Q *poly = malloc(sizeof(Q) * (n ? n : 1));
    for (int i = 0; i < n; i++) { poly[i].x = g->p[o].xy[2 * i]; poly[i].y = g->p[o].xy[2 * i + 1]; }
    int nh = 0, *hi = malloc(sizeof(int) * g->n); double *hx = malloc(sizeof(double) * g->n);
    for (int h = 0; h < g->n; h++) if (h != o && own[h] == o && g->p[h].n >= 3) {
        double mx = -1e300; for (int i = 0; i < g->p[h].n; i++) if (g->p[h].xy[2 * i] > mx) mx = g->p[h].xy[2 * i];
        int k = nh++; while (k > 0 && hx[k - 1] < mx) { hx[k] = hx[k - 1]; hi[k] = hi[k - 1]; k--; } hx[k] = mx; hi[k] = h;
    }
    for (int k = 0; k < nh; k++) {
        const CPath *hp = &g->p[hi[k]]; Q *hq = malloc(sizeof(Q) * hp->n);
        for (int i = 0; i < hp->n; i++) { hq[i].x = hp->xy[2 * i]; hq[i].y = hp->xy[2 * i + 1]; }
        poly = bridge(poly, &n, hq, hp->n); free(hq);
    }
    int *tr = malloc(sizeof(int) * 3 * (n ? n : 1)); int nt = earclip(poly, n, tr);
    *tri = malloc(sizeof(float) * 9 * (nt ? nt : 1)); *ntri = nt;
    for (int t = 0; t < nt; t++) for (int j = 0; j < 3; j++) { Q q = poly[tr[3 * t + (up ? j : 2 - j)]]; float *d = *tri + 9 * t + 3 * j; d[0] = (float)q.x; d[1] = (float)q.y; d[2] = (float)z; }
    free(tr); free(poly); free(hi); free(hx);
}
static double ring_area(const CPath *p) { double a = 0; for (int i = 0; i < p->n; i++) { int j = (i + 1) % p->n; a += p->xy[2 * i] * p->xy[2 * j + 1] - p->xy[2 * j] * p->xy[2 * i + 1]; } return a / 2; }
/* the cap faces of region r at height z (up = looking +Z): one face per outer, its ring and holes as loops */
static void cap_faces(TB2 *b, VHash *vh, const CPaths *r, double z, int up) {
    if (!r->n) return;
    int *own; CPaths g = cl_group(r, &own);
    for (int o = 0; o < g.n; o++) {
        if (own[o] != o) continue;
        int fi = b->t->nface; TFace *f = new_face(b); f->nrm[2] = up ? 1 : -1; f->has_nrm = 1;
        cap_group_tris(&g, own, o, z, up, &f->tri, &f->ntri);
        for (int h = 0; h < g.n; h++) {
            if (own[h] != o || g.p[h].n < 2) continue;
            const CPath *p = &g.p[h]; int n = p->n, outer = h == o;
            /* the face on the left seen from the outside: up face = outer CCW / holes CW, down face the reverse */
            int want_ccw = outer == up, rev = (ring_area(p) > 0) != want_ccw;
            TSide *sd = malloc(sizeof(TSide) * n); int ns = 0;
            for (int k = 0; k < n; k++) {
                int i = rev ? (n - k) % n : k, j = rev ? (n - k - 1 + n) % n : (k + 1) % n; long long ka[3], kb[3];
                vkey3(p->xy[2 * i], p->xy[2 * i + 1], z, ka); vkey3(p->xy[2 * j], p->xy[2 * j + 1], z, kb);
                int u = vh_find(vh, ka, -1), v = vh_find(vh, kb, -1); if (u < 0 || v < 0 || u == v) continue;
                sd[ns].u = u; sd[ns].v = v; sd[ns].sgi = seg_of(b->cx, u, v); ns++;
            }
            add_loop(b, sd, ns, fi); free(sd);
        }
        b->t->face[fi].nl = b->t->nloop - b->t->face[fi].l0;
    }
    free(own); cl_free(&g);
}
typedef struct { double z; double *xy; int n, cap; } ZPts;    /* the ring points at one height (for the T-junction splits) */
static ZPts *zpts_at(ZPts **zp, int *nz, double z) {
    for (int i = 0; i < *nz; i++) if (fabs((*zp)[i].z - z) < 1e-9) return &(*zp)[i];
    *zp = realloc(*zp, sizeof(ZPts) * (*nz + 1)); ZPts *q = &(*zp)[(*nz)++]; memset(q, 0, sizeof *q); q->z = z; return q;
}
static void zpts_add(ZPts *q, double x, double y) {
    if (q->n == q->cap) { q->cap = q->cap ? q->cap * 2 : 256; q->xy = realloc(q->xy, sizeof(double) * 2 * q->cap); }
    q->xy[2 * q->n] = x; q->xy[2 * q->n + 1] = y; q->n++;
}
typedef struct { int *v, n; double nrm[2]; float tri[18]; } WPoly;
static int dpar_cmp(const void *x, const void *y) { double a = ((const double *)x)[0], b = ((const double *)y)[0]; return a < b ? -1 : a > b; }
/* the vertex numbers of the points of zp strictly inside segment (ax, ay) - (bx, by), in order from a */
static int side_splits(const ZPts *zp, double ax, double ay, double bx, double by, double z, VHash *vh, int *nv, int *out, int max) {
    if (!zp) return 0;
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy; if (l2 <= 0) return 0;
    double *pr = malloc(sizeof(double) * 3 * (zp->n ? zp->n : 1)); int np = 0;
    for (int i = 0; i < zp->n; i++) {
        double px = zp->xy[2 * i] - ax, py = zp->xy[2 * i + 1] - ay, t = (px * dx + py * dy) / l2;
        if (t <= 1e-9 || t >= 1 - 1e-9) continue;
        if (fabs(px * dy - py * dx) / sqrt(l2) > 1e-7) continue;
        pr[3 * np] = t; pr[3 * np + 1] = zp->xy[2 * i]; pr[3 * np + 2] = zp->xy[2 * i + 1]; np++;
    }
    qsort(pr, np, sizeof(double) * 3, dpar_cmp);
    int no = 0, last = -1;
    for (int i = 0; i < np && no < max; i++) { long long k[3]; vkey3(pr[3 * i + 1], pr[3 * i + 2], z, k); int id = vh_find(vh, k, -1); if (id < 0) id = vh_find(vh, k, (*nv)++); if (id != last) out[no++] = last = id; }
    free(pr); return no;
}
int body_brep_topo(const Body *bd, BTopo *t) {
    memset(t, 0, sizeof *t);
    if (bd->occ) { XtSchema *sc = bd->xt ? xt_schema_for(bd->xt, bd->xt_n) : NULL; return sc ? xt_brep_topo(sc, bd->xt, bd->xt_n, bd->xt_scale, t) : 0; }
    BrepCx cx; memset(&cx, 0, sizeof cx);
    BEdge *E = NULL; int ne = brep_edges_core(bd, &E, &cx); bedges_free(E, ne);
    if (!cx.sg) { free(cx.vk); return 0; }
    VHash vh = { 0 }; int nv = cx.nv;
    for (int i = 0; i < cx.nvk; i++) vh_find(&vh, cx.vk[i].k, cx.vk[i].i);
    TB2 b = { t, 0, 0, 0, &cx };
    CPaths none = cp_empty();
    ZPts *zp = NULL; int nz = 0;
    for (int k = 0; k < bd->n; k++) { const Slab *sl = &bd->s[k];
        for (int pi = 0; pi < sl->reg.n; pi++) for (int i = 0; i < sl->reg.p[pi].n; i++) {
            zpts_add(zpts_at(&zp, &nz, sl->z0), sl->reg.p[pi].xy[2 * i], sl->reg.p[pi].xy[2 * i + 1]);
            zpts_add(zpts_at(&zp, &nz, sl->z1), sl->reg.p[pi].xy[2 * i], sl->reg.p[pi].xy[2 * i + 1]); } }
    for (int i = 0; i < cx.nvk; i++) zpts_add(zpts_at(&zp, &nz, cx.vk[i].k[2] / 1e7), cx.vk[i].k[0] / 1e7, cx.vk[i].k[1] / 1e7);
    /* caps (body_mesh's dn / upf of every slab) */
    for (int k = 0; k < bd->n; k++) {
        const Slab *sl = &bd->s[k];
        int below = k > 0 && fabs(bd->s[k - 1].z1 - sl->z0) < 1e-9, above = k + 1 < bd->n && fabs(bd->s[k + 1].z0 - sl->z1) < 1e-9;
        CPaths dn = cl_bool(&sl->reg, below ? &bd->s[k - 1].reg : &none, 2), upf = cl_bool(&sl->reg, above ? &bd->s[k + 1].reg : &none, 2);
        cap_faces(&b, &vh, &dn, sl->z0, 0); cap_faces(&b, &vh, &upf, sl->z1, 1);
        cl_free(&dn); cl_free(&upf);
    }
    cl_free(&none);
    /* wall polygons: A (Pi, z0) -> splits -> B (Pj, z0) -> C (Pj, z1) -> splits -> D (Pi, z1), outward-CCW (walls(): the
       normal is right of the ring's travel) */
    WPoly *wp = NULL; int nw = 0, wcap = 0;
    int *tmp = malloc(sizeof(int) * 4096);
    for (int k = 0; k < bd->n; k++) {
        const Slab *sl = &bd->s[k]; ZPts *z0p = zpts_at(&zp, &nz, sl->z0), *z1p = zpts_at(&zp, &nz, sl->z1);
        for (int pi = 0; pi < sl->reg.n; pi++) { const CPath *p = &sl->reg.p[pi]; int n = p->n; if (n < 2) continue;
            for (int i = 0; i < n; i++) {
                int j = (i + 1) % n; double xi = p->xy[2 * i], yi = p->xy[2 * i + 1], xj = p->xy[2 * j], yj = p->xy[2 * j + 1];
                double dx = xj - xi, dy = yj - yi, l = hypot(dx, dy); if (l <= 0) continue;
                long long kA[3], kB[3], kC[3], kD[3]; vkey3(xi, yi, sl->z0, kA); vkey3(xj, yj, sl->z0, kB); vkey3(xj, yj, sl->z1, kC); vkey3(xi, yi, sl->z1, kD);
                int A = vh_find(&vh, kA, -1); if (A < 0) A = vh_find(&vh, kA, nv++);
                int B = vh_find(&vh, kB, -1); if (B < 0) B = vh_find(&vh, kB, nv++);
                int C = vh_find(&vh, kC, -1); if (C < 0) C = vh_find(&vh, kC, nv++);
                int D = vh_find(&vh, kD, -1); if (D < 0) D = vh_find(&vh, kD, nv++);
                if (A == B) continue;
                if (nw == wcap) { wcap = wcap ? wcap * 2 : 256; wp = realloc(wp, sizeof(WPoly) * wcap); }
                WPoly *w = &wp[nw++]; int m = 0; w->v = malloc(sizeof(int) * 4100);
                w->v[m++] = A; m += side_splits(z0p, xi, yi, xj, yj, sl->z0, &vh, &nv, w->v + m, 2048);
                w->v[m++] = B; w->v[m++] = C;
                int ns = side_splits(z1p, xj, yj, xi, yi, sl->z1, &vh, &nv, tmp, 2048); memcpy(w->v + m, tmp, sizeof(int) * ns); m += ns;
                w->v[m++] = D; w->n = m;
                w->nrm[0] = dy / l; w->nrm[1] = -dx / l;
                float q[4][3] = { { (float)xi, (float)yi, (float)sl->z0 }, { (float)xj, (float)yj, (float)sl->z0 }, { (float)xj, (float)yj, (float)sl->z1 }, { (float)xi, (float)yi, (float)sl->z1 } };
                memcpy(w->tri, q[0], 12); memcpy(w->tri + 3, q[1], 12); memcpy(w->tri + 6, q[2], 12);
                memcpy(w->tri + 9, q[0], 12); memcpy(w->tri + 12, q[2], 12); memcpy(w->tri + 15, q[3], 12);
            } }
    }
    free(tmp);
    /* join the wall polygons across their sides that are no edge segment */
    int *par = malloc(sizeof(int) * (nw ? nw : 1)); for (int i = 0; i < nw; i++) par[i] = i;
    TAdj *aj = NULL; int na = 0, acap = 0;
    for (int i = 0; i < nw; i++) for (int s = 0; s < wp[i].n; s++) {
        int u = wp[i].v[s], v = wp[i].v[(s + 1) % wp[i].n]; if (u == v || seg_of(&cx, u, v) >= 0) continue;
        if (na == acap) { acap = acap ? acap * 2 : 1024; aj = realloc(aj, sizeof(TAdj) * acap); }
        aj[na].lo = u < v ? u : v; aj[na].hi = u < v ? v : u; aj[na].poly = i; na++;
    }
    if (na > 1) qsort(aj, na, sizeof(TAdj), tadj_cmp);
    for (int i = 0; i + 1 < na; i++) if (aj[i].lo == aj[i + 1].lo && aj[i].hi == aj[i + 1].hi) { int a = uf_find(par, aj[i].poly), c = uf_find(par, aj[i + 1].poly); if (a != c) par[a] = c; }
    free(aj);
    char *done = calloc(nw ? nw : 1, 1);
    for (int i = 0; i < nw; i++) {
        int r = uf_find(par, i); if (done[r]) continue; done[r] = 1;
        int fi = t->nface; TFace *f = new_face(&b); f->nrm[0] = wp[i].nrm[0]; f->nrm[1] = wp[i].nrm[1]; f->has_nrm = 1;
        int cnt = 0; for (int j = 0; j < nw; j++) if (uf_find(par, j) == r) cnt++;
        f->tri = malloc(sizeof(float) * 18 * cnt); f->ntri = 0;
        TSide *sd = NULL; int nsd = 0, scap = 0;
        for (int j = 0; j < nw; j++) { if (uf_find(par, j) != r) continue;
            memcpy(f->tri + 9 * f->ntri, wp[j].tri, sizeof(float) * 18); f->ntri += 2;
            for (int s = 0; s < wp[j].n; s++) { int u = wp[j].v[s], v = wp[j].v[(s + 1) % wp[j].n], q; if (u == v || (q = seg_of(&cx, u, v)) < 0) continue;
                if (nsd == scap) { scap = scap ? scap * 2 : 64; sd = realloc(sd, sizeof(TSide) * scap); }
                sd[nsd].u = u; sd[nsd].v = v; sd[nsd].sgi = q; nsd++; } }
        /* chain the boundary sides head to tail into loops */
        char *us = calloc(nsd ? nsd : 1, 1); TSide *lp = malloc(sizeof(TSide) * (nsd ? nsd : 1));
        for (int s0 = 0; s0 < nsd; s0++) {
            if (us[s0]) continue;
            int nl = 0, cur = s0;
            while (cur >= 0 && !us[cur]) {
                us[cur] = 1; lp[nl++] = sd[cur]; int nx = -1;
                for (int q = 0; q < nsd; q++) if (!us[q] && sd[q].u == sd[cur].v) { nx = q; break; }
                cur = nx;
            }
            if (nl && lp[nl - 1].v == lp[0].u) add_loop(&b, lp, nl, fi);
        }
        free(us); free(lp); free(sd);
        t->face[fi].nl = t->nloop - t->face[fi].l0;
    }
    free(done); free(par);
    for (int i = 0; i < nw; i++) free(wp[i].v);
    free(wp);
    for (int i = 0; i < nz; i++) free(zp[i].xy);
    free(zp); free(vh.t);
    free(cx.vk); free(cx.sg); free(cx.seg_edge); free(cx.seg_fwd);
    return t->nface;
}
